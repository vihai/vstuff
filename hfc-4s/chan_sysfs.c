#include <linux/kernel.h>
#include <linux/spinlock.h>

#include "hfc-4s.h"
#include "chan.h"
#include "chan_sysfs.h"
#include "card.h"
#include "fsm.h"
#include "fifo_inline.h"
#include "st_port_inline.h"

static int hfc_bert_enable(struct hfc_chan_duplex *chan)
{
	struct hfc_card *card = chan->port->card;

	int err;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	if (chan->status != HFC_STATUS_FREE) {
		err = -EBUSY;
		goto err_busy;
	}

	struct hfc_fifo *fifo_rx;
	fifo_rx = hfc_allocate_fifo(card, RX);
	if (!fifo_rx) {
		err = -ENOMEM;
		goto err_allocate_fifo_rx;
	}

	struct hfc_fifo *fifo_tx;
	fifo_tx = hfc_allocate_fifo(card, TX);
	if (!fifo_tx) {
		err = -ENOMEM;
		goto err_allocate_fifo_tx;
	}

	chan->rx.fifo = fifo_rx;
	fifo_rx->connected_chan = &chan->rx;

	chan->tx.fifo = fifo_tx;
	fifo_tx->connected_chan = &chan->tx;

	hfc_upload_fsm(card);

	// RX
	chan->rx.fifo->bit_reversed = FALSE;
	hfc_fifo_select(chan->rx.fifo);
	hfc_fifo_reset(chan->rx.fifo);
	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_from_ST);

	hfc_outb(card, hfc_A_IRQ_MSK,
		hfc_A_IRQ_MSK_V_BERT_EN);

	// TX
	chan->tx.fifo->bit_reversed = FALSE;
	hfc_fifo_select(chan->tx.fifo);
	hfc_fifo_reset(chan->tx.fifo);
	hfc_outb(card, hfc_A_CON_HDLC,
		hfc_A_CON_HDCL_V_HDLC_TRP_TRP|
		hfc_A_CON_HDCL_V_TRP_IRQ_FIFO_ENABLED|
		hfc_A_CON_HDCL_V_DATA_FLOW_FIFO_to_ST_FIFO_to_PCM);

	hfc_outb(card, hfc_A_IRQ_MSK,
		hfc_A_IRQ_MSK_V_BERT_EN);

	hfc_chan_enable(chan);

	chan->status = HFC_STATUS_OPEN_BERT;

	spin_unlock_irqrestore(&card->lock, flags);

	hfc_msg_chan(chan, KERN_INFO, "BERT enabled\n");

	return 0;

	hfc_deallocate_fifo(fifo_tx);
err_allocate_fifo_tx:
	hfc_deallocate_fifo(fifo_rx);
err_allocate_fifo_rx:
err_busy:

	spin_unlock_irqrestore(&card->lock, flags);

	return err;
}

static int hfc_bert_disable(
	struct hfc_chan_duplex *chan)
{
	struct hfc_card *card = chan->port->card;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	if (chan->status == HFC_STATUS_OPEN_BERT) {
		hfc_chan_disable(chan);

		chan->status = HFC_STATUS_FREE;

		hfc_msg_chan(chan, KERN_INFO, "BERT disabled\n");
	}

	spin_unlock_irqrestore(&card->lock, flags);

	return 0;
}


static ssize_t hfc_show_bert_enabled(
	struct device *device,
	char *buf)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(device);
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		chan->status == HFC_STATUS_OPEN_BERT ? 1 : 0);
}

static ssize_t hfc_store_bert_enabled(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(device);
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);

	int err;

	int enabled;
	sscanf(buf, "%d", &enabled);

// FIXME TODO one-way only

	if (enabled)
		err = hfc_bert_enable(chan);
	else
		err = hfc_bert_disable(chan);

	if (err < 0)
		return err;

	return count;
}

static DEVICE_ATTR(bert_enabled, S_IRUGO | S_IWUSR,
		hfc_show_bert_enabled,
		hfc_store_bert_enabled);

//----------------------------------------------------------------------------

static ssize_t hfc_show_sq_bits(
	struct device *device,
	char *buf)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(device);
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	hfc_st_port_select(card, port->id);

	int bits = hfc_A_ST_SQ_RD_V_ST_SQ_RD(hfc_inb(card, hfc_A_ST_SQ_RD));

	spin_unlock_irqrestore(&card->lock, flags);

	return snprintf(buf, PAGE_SIZE, "%01x\n", bits);

}

static ssize_t hfc_store_sq_bits(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(device);
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;
	
	unsigned long flags;

	unsigned int value;
	if (sscanf(buf, "%01x", &value) < 1)
		return -EINVAL;

	if (value > 0x0f)
		return -EINVAL;

	spin_lock_irqsave(&card->lock, flags);
	hfc_st_port_select(card, port->id);
	hfc_outb(card, hfc_A_ST_SQ_WR, value);
	spin_unlock_irqrestore(&card->lock, flags);

	return count;
}

static DEVICE_ATTR(sq_bits, S_IRUGO | S_IWUSR,
		hfc_show_sq_bits,
		hfc_store_sq_bits);

//----------------------------------------------------------------------------

static ssize_t hfc_show_sq_enabled(
	struct device *device,
	char *buf)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(device);
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		(chan->port->regs.st_ctrl_0 & hfc_A_ST_CTRL0_V_SQ_EN) ? 1 : 0);

}

static ssize_t hfc_store_sq_enabled(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(device);
	struct hfc_chan_duplex *chan = to_chan_duplex(visdn_chan);
	struct hfc_st_port *port = chan->port;
	struct hfc_card *card = port->card;
	
	unsigned int value;
	if (sscanf(buf, "%d", &value) < 1)
		return -EINVAL;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	hfc_st_port_select(card, port->id);

	if (value)
		port->regs.st_ctrl_0 |= hfc_A_ST_CTRL0_V_SQ_EN;
	else
		port->regs.st_ctrl_0 &= ~hfc_A_ST_CTRL0_V_SQ_EN;

	hfc_outb(port->card, hfc_A_ST_CTRL0,
		port->regs.st_ctrl_0);

	spin_unlock_irqrestore(&card->lock, flags);

	return count;
}

static DEVICE_ATTR(sq_enabled, S_IRUGO | S_IWUSR,
		hfc_show_sq_enabled,
		hfc_store_sq_enabled);


static int hfc_chan_sysfs_create_files_DB(
	struct hfc_chan_duplex *chan)
{
	int err;

	err = device_create_file(
		&chan->visdn_chan.device,
		&dev_attr_bert_enabled);
	if (err < 0)
		goto err_device_create_file_bert_enabled;

	return 0;

	device_remove_file(&chan->visdn_chan.device, &dev_attr_bert_enabled);
err_device_create_file_bert_enabled:

	return err;
}

int hfc_chan_sysfs_create_files_D(
	struct hfc_chan_duplex *chan)
{
	return hfc_chan_sysfs_create_files_DB(chan);
}

int hfc_chan_sysfs_create_files_B(
	struct hfc_chan_duplex *chan)
{
	return hfc_chan_sysfs_create_files_DB(chan);
}

int hfc_chan_sysfs_create_files_SQ(
	struct hfc_chan_duplex *chan)
{
	int err;

	err = device_create_file(
		&chan->visdn_chan.device,
		&dev_attr_sq_bits);
	if (err < 0)
		goto err_device_create_file_sq_bits;

	err = device_create_file(
		&chan->visdn_chan.device,
		&dev_attr_sq_enabled);
	if (err < 0)
		goto err_device_create_file_sq_enabled;

	device_remove_file(&chan->visdn_chan.device, &dev_attr_sq_enabled);
err_device_create_file_sq_enabled:
	device_remove_file(&chan->visdn_chan.device, &dev_attr_sq_bits);
err_device_create_file_sq_bits:

	return err;
}

void hfc_chan_sysfs_delete_files_DB(
	struct hfc_chan_duplex *chan)
{
	device_remove_file(&chan->visdn_chan.device, &dev_attr_bert_enabled);
}

void hfc_chan_sysfs_delete_files_D(
	struct hfc_chan_duplex *chan)
{
	hfc_chan_sysfs_delete_files_DB(chan);
}

void hfc_chan_sysfs_delete_files_B(
	struct hfc_chan_duplex *chan)
{
	hfc_chan_sysfs_delete_files_DB(chan);
}

void hfc_chan_sysfs_delete_files_SQ(
	struct hfc_chan_duplex *chan)
{
	device_remove_file(&chan->visdn_chan.device, &dev_attr_sq_enabled);
	device_remove_file(&chan->visdn_chan.device, &dev_attr_sq_bits);
}

