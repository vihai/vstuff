/*
 * Copyright (C) 2004 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/usb.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>

#include <lapd.h>

#include "hfc-usb.h"
#include "fifo.h"

#define D 0
#define B1 1
#define B2 2
#define E 3

#define D_FIFO_OFF 2
#define B1_FIFO_OFF 0
#define B2_FIFO_OFF 1
#define E_FIFO_OFF 3

static int nt_mode = 0;
static struct proc_dir_entry *hfc_proc_hfc_dir;

#ifdef DEBUG
int debug_level = 0;
#endif

#define HFC_VENDOR_ID_COLOGNE	0x0959
#define HFC_VENDOR_ID_TRUST	0x07b0

static struct usb_device_id hfc_table [] = {
	{ USB_DEVICE(HFC_VENDOR_ID_COLOGNE, 0x2bd0) },
	{ USB_DEVICE(HFC_VENDOR_ID_TRUST, 0x0006) },
	{ }
};
MODULE_DEVICE_TABLE (usb, hfc_table);

/******************************************
 * HW routines
 ******************************************/

static void hfc_reset_card(struct hfc_card *card)
{
	hfc_write(card, HFC_REG_CIRM, HFC_REG_CIRM_RESET);
	hfc_write(card, HFC_REG_CIRM, 0);
	hfc_write(card, HFC_REG_CIRM, HFC_REG_CIRM_AUX_FIXED);

	hfc_write(card, HFC_REG_P_DATA, 0x80); //0x40|0x20|0x10|0x04

	hfc_write(card, HFC_REG_USB_SIZE,
		HFC_REG_USB_SIZE_OUT(card->chans[D].tx.int_endpoint->desc.wMaxPacketSize) |
		HFC_REG_USB_SIZE_IN(card->chans[D].rx.int_endpoint->desc.wMaxPacketSize));

//	hfc_write(card, HFC_REG_USB_SIZE_I,
//		HFC_REG_USB_SIZE_I_VAL(card->chans[D].tx.iso_endpoint->desc.wMaxPacketSize));

	hfc_write(card, HFC_REG_MST_MODE0,
		HFC_REG_MST_MODE0_MASTER);

	hfc_write(card, HFC_REG_MST_MODE1, 0);

	hfc_write(card, HFC_REG_F_THRESH,
		HFC_REG_F_THRESH_TX(15) |
		HFC_REG_F_THRESH_RX(15));

	hfc_write(card, HFC_REG_SCTRL,
		HFC_REG_SCTRL_TE |
		HFC_REG_SCTRL_NON_CAPACITIVE);

	hfc_write(card, HFC_REG_SCTRL_R, 0);

	hfc_write(card, HFC_REG_FIFO, HFC_REG_FIFO_D_RX);
	hfc_write(card, HFC_REG_HDLC_PAR,
		HFC_REG_HDLC_PAR_PROC_2BITS);

	hfc_write(card, HFC_REG_FIFO, HFC_REG_FIFO_D_TX);
	hfc_write(card, HFC_REG_HDLC_PAR,
		HFC_REG_HDLC_PAR_PROC_2BITS);

	hfc_write(card, HFC_REG_FIFO, HFC_REG_FIFO_PCM_RX);
	hfc_write(card, HFC_REG_HDLC_PAR,
		HFC_REG_HDLC_PAR_PROC_2BITS);

	hfc_write(card, HFC_REG_FIFO, HFC_REG_FIFO_PCM_TX);
	hfc_write(card, HFC_REG_HDLC_PAR,
		HFC_REG_HDLC_PAR_PROC_2BITS);

///////////////////////
/*
	hfc_write(card, HFC_REG_FIFO, HFC_REG_FIFO_B1_RX);
	hfc_write(card, HFC_REG_HDLC_PAR,
		HFC_REG_HDLC_PAR_PROC_8BITS);
	hfc_write(card, HFC_REG_CON_HDLC,
		HFC_REG_CON_HDLC_TRANS |
		HFC_REG_CON_HDLC_TRANS_64 |
		HFC_REG_CON_FIFO_from_ST |
		HFC_REG_CON_ST_from_FIFO |
		HFC_REG_CON_PCM_from_FIFO);
	hfc_write(card, HFC_REG_INC_RES_F,
		HFC_REG_INC_RES_F_RESET);
	hfc_write(card, HFC_REG_INC_RES_F,
		0);

	hfc_write(card, HFC_REG_FIFO, HFC_REG_FIFO_B1_TX);
	hfc_write(card, HFC_REG_HDLC_PAR,
		HFC_REG_HDLC_PAR_PROC_8BITS);

	hfc_write(card, HFC_REG_FIFO, HFC_REG_FIFO_B2_RX);
	hfc_write(card, HFC_REG_HDLC_PAR,
		HFC_REG_HDLC_PAR_PROC_8BITS);
	hfc_write(card, HFC_REG_CON_HDLC,
		HFC_REG_CON_HDLC_TRANS |
		HFC_REG_CON_HDLC_TRANS_64 |
		HFC_REG_CON_FIFO_from_ST |
		HFC_REG_CON_ST_from_FIFO |
		HFC_REG_CON_PCM_from_FIFO);
	hfc_write(card, HFC_REG_INC_RES_F,
		HFC_REG_INC_RES_F_RESET);
	hfc_write(card, HFC_REG_INC_RES_F,
		0);

	hfc_write(card, HFC_REG_FIFO, HFC_REG_FIFO_B2_TX);
	hfc_write(card, HFC_REG_HDLC_PAR,
		HFC_REG_HDLC_PAR_PROC_8BITS);
*/
///////////////////////////////////

	// RX FIFO
	hfc_write(card, HFC_REG_FIFO, HFC_REG_FIFO_PCM_RX);

	hfc_write(card, HFC_REG_CON_HDLC,
		HFC_REG_CON_HDLC_HDLC |
		HFC_REG_CON_HDLC_HDLC_ENA |
		HFC_REG_CON_FIFO_from_ST |
		HFC_REG_CON_ST_from_FIFO |
		HFC_REG_CON_PCM_from_FIFO);

	hfc_write(card, HFC_REG_INC_RES_F,
		HFC_REG_INC_RES_F_RESET);
//	hfc_write(card, HFC_REG_INC_RES_F,
//		0);

//	hfc_write(card, HFC_REG_INT_M1, 0xff);
//	hfc_write(card, HFC_REG_INT_M2, 0xa1);
}

/******************************************
 * /proc interface functions
 ******************************************/

struct hfc_status_to_name_names {
	enum hfc_chan_status status;
	char *name;
};

static char *hfc_status_to_name(int status)
{
	struct hfc_status_to_name_names names[] = {
		{ free, "free" },
		{ open_framed, "framed" },
		{ open_voice, "voice" },
		{ sniff_aux, "sniff aux" },
		{ loopback, "loopback" },
		};

	int i;

	for (i=0; i<sizeof(names); i++) {
		if (names[i].status == status)
			return names[i].name;
	}

	return "*UNK*";
}

static void hfc_hexdump(char *src,char *dst, int size)
{
	static char hexchars[] = "0123456789ABCDEF";

	int i;
	for(i=0;i<size;i++) {
		dst[i*2]  = hexchars[(src[i] & 0xf0) >> 4];
		dst[i*2+1]= hexchars[src[i] & 0xf];
	}

	dst[size*2]='\0';
}

static int hfc_proc_read_info(char *page, char **start,
		off_t off, int count, 
		int *eof, void *data)
{
	struct hfc_card *card = data;

	u8 chip_id;
	chip_id = hfc_read(card, HFC_REG_CHIP_ID);

	int len;

u8 l1_state = hfc_read(card, HFC_REG_STATES) & 0x07;

	len=0;
	len = snprintf(page, PAGE_SIZE,
		"Cardnum   : %d\n"
		"Mode      : %s\n"
		"CHIP_ID   : %#02x\n"
		"L1 State  : %c%d\n"
		"S1S2 : %d %d\n"
		"\nChannel     %12s %12s %12s %12s %4s %4s %4s\n"
		"D         : %12llu %12llu %12llu %12llu %4llu %4llu %4llu %s\n"
		"B1        : %12llu %12llu %12llu %12llu %4llu %4llu %4llu %s\n"
		"B2        : %12llu %12llu %12llu %12llu %4llu %4llu %4llu %s\n"
		,card->id
		,card->nt_mode?"NT":"TE"
		,chip_id
		,card->nt_mode?'G':'F'
//		,card->l1_state
		,l1_state
		,hfc_read(card, HFC_REG_INT_S1)
		,hfc_read(card, HFC_REG_INT_S2)


		,"RX Frames","TX Frames","RX Bytes","TX Bytes","RXFF","TXFF","CRC"
		,card->chans[D].rx.frames
		,card->chans[D].tx.frames
		,card->chans[D].rx.bytes
		,card->chans[D].tx.bytes
		,card->chans[D].rx.fifo_full
		,card->chans[D].tx.fifo_full
		,card->chans[D].rx.crc
		,hfc_status_to_name(card->chans[D].status)

		,card->chans[B1].rx.frames
		,card->chans[B1].tx.frames
		,card->chans[B1].rx.bytes
		,card->chans[B1].tx.bytes
		,card->chans[B1].rx.fifo_full
		,card->chans[B1].tx.fifo_full
		,card->chans[B1].rx.crc
		,hfc_status_to_name(card->chans[B1].status)

		,card->chans[B2].rx.frames
		,card->chans[B2].tx.frames
		,card->chans[B2].rx.bytes
		,card->chans[B2].tx.bytes
		,card->chans[B2].rx.fifo_full
		,card->chans[B2].tx.fifo_full
		,card->chans[B2].rx.crc
		,hfc_status_to_name(card->chans[B2].status)
		);

	return len;
}

static int hfc_proc_read_fifos(char *page, char **start,
		off_t off, int count, 
		int *eof, void *data)
{
	struct hfc_card *card = data;

	int len = 0;
	len += snprintf(page + len, PAGE_SIZE - len,
		"\n            Receive            Transmit\n"
		"Channel     F1 F2 Z1 Z2 Used   F1 F2 Z1 Z2 Used\n");

	int i;
	for (i=0; i<4; i++) {
		hfc_write(card, HFC_REG_FIFO, card->chans[i].rx.fifo_id);

		u8 rx_f1 = hfc_read(card, HFC_REG_FIF_F1);
		u8 rx_f2 = hfc_read(card, HFC_REG_FIF_F2);
		u8 rx_z1 = hfc_read(card, HFC_REG_FIF_Z1);
		u8 rx_z2 = hfc_read(card, HFC_REG_FIF_Z2);

		hfc_write(card, HFC_REG_FIFO, card->chans[i].tx.fifo_id);

		u8 tx_f1 = hfc_read(card, HFC_REG_FIF_F1);
		u8 tx_f2 = hfc_read(card, HFC_REG_FIF_F2);
		u8 tx_z1 = hfc_read(card, HFC_REG_FIF_Z1);
		u8 tx_z2 = hfc_read(card, HFC_REG_FIF_Z2);

		len += snprintf(page + len, PAGE_SIZE - len,
			"%2s        : %02x %02x %02x %02x        %02x %02x %02x %02x\n",
			card->chans[i].name,
			rx_f1, rx_f2, rx_z1, rx_z2,
			tx_f1, tx_f2, tx_z1, tx_z2);
	}

	return len;
}

/******************************************
 * net_device interface functions
 ******************************************/

static void hfc_rx_complete(struct urb *urb, struct pt_regs *regs)
{
	struct hfc_chan_simplex *chan = urb->context;
	struct hfc_card *card = chan->chan->card;

	printk(KERN_DEBUG hfc_DRIVER_PREFIX
		"rx_complete! %d %04x %d\n", urb->status, *(u16 *)chan->buffer, urb->actual_length);

	int err;
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		printk(KERN_ERR hfc_DRIVER_PREFIX
			"can't resubmit urb, error %d\n", err);
	}
}

static void hfc_tx_complete(struct urb *urb, struct pt_regs *regs)
{
	struct hfc_chan_simplex *chan = urb->context;

	printk(KERN_DEBUG hfc_DRIVER_PREFIX
		"tx_complete! %d %04x %d\n", urb->interval, *(u16 *)chan->buffer, urb->actual_length);

}

static int hfc_configure_int_chan(struct hfc_chan_simplex *chan)
{
	int err;

	chan->pipe = usb_rcvintpipe(
		chan->chan->card->usb_dev,
		chan->int_endpoint->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);

	printk(KERN_ERR hfc_DRIVER_PREFIX "--------------------------> %s %d %d %d\n", chan->chan->name, chan->pipe, chan->int_endpoint->desc.bEndpointAddress, chan->int_endpoint->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);

	chan->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!chan->urb) {
		printk(KERN_ERR hfc_DRIVER_PREFIX
			"usb_alloc_urb() error\n");
		err = -ENOMEM;
		return err;
	}

	usb_fill_int_urb(
		chan->urb,
		chan->chan->card->usb_dev,
		chan->pipe,
		chan->buffer,
		chan->int_endpoint->desc.wMaxPacketSize,
		hfc_rx_complete,
		chan, 1);

	err = usb_submit_urb(chan->urb, GFP_KERNEL);
	if (err < 0) {
		printk(KERN_ERR hfc_DRIVER_PREFIX
			"usb_submit_urb() error\n");
		return err;
	}

	return 0;
}

static int hfc_open(struct net_device *netdev)
{
	struct hfc_chan_duplex *chan = netdev->priv;
	struct hfc_card *card = chan->card;
	int err = 0;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	if (chan->status != free &&
		(chan->id != D || chan->status != open_framed)) {
		spin_unlock_irqrestore(&card->lock, flags);
		return -EBUSY;
	}

	chan->status = open_framed;

	spin_unlock_irqrestore(&card->lock, flags);

	// Ok, now the chanel is ours we need to configure it

	switch (chan->id) {
	case D:
		if (netdev->flags & IFF_ALLMULTI) {
			card->nt_mode = TRUE;

			chan->netdev->dev_addr[0] = 0x01;

			hfc_write(card, HFC_REG_SCTRL,
				HFC_REG_SCTRL_NT |
				HFC_REG_SCTRL_NON_CAPACITIVE);

			hfc_write(card, HFC_REG_CLKDEL, 0x6c);
		} else {
			card->nt_mode = FALSE;

			chan->netdev->dev_addr[0] = 0x00;

			hfc_write(card, HFC_REG_SCTRL,
				HFC_REG_SCTRL_TE |
				HFC_REG_SCTRL_NON_CAPACITIVE);

			hfc_write(card, HFC_REG_CLKDEL, 0x0f);
		}

		// RX FIFO
		hfc_write(card, HFC_REG_FIFO, HFC_REG_FIFO_D_RX);

		hfc_write(card, HFC_REG_CON_HDLC,
			HFC_REG_CON_HDLC_HDLC |
			HFC_REG_CON_HDLC_HDLC_ENA |
			HFC_REG_CON_FIFO_from_ST |
			HFC_REG_CON_ST_from_FIFO |
			HFC_REG_CON_PCM_from_FIFO);

		hfc_write(card, HFC_REG_INC_RES_F,
			HFC_REG_INC_RES_F_RESET);
//		hfc_write(card, HFC_REG_INC_RES_F,
//			0);

		// TX FIFO
		hfc_write(card, HFC_REG_FIFO, HFC_REG_FIFO_D_TX);

		hfc_write(card, HFC_REG_CON_HDLC,
			HFC_REG_CON_HDLC_IFF |
			HFC_REG_CON_HDLC_HDLC |
			HFC_REG_CON_HDLC_HDLC_ENA |
			HFC_REG_CON_FIFO_from_ST |
			HFC_REG_CON_ST_from_FIFO |
			HFC_REG_CON_PCM_from_FIFO);

		hfc_write(card, HFC_REG_INC_RES_F,
			HFC_REG_INC_RES_F_RESET);
//		hfc_write(card, HFC_REG_INC_RES_F,
//			0);

		hfc_configure_int_chan(&card->chans[D].rx);
		hfc_configure_int_chan(&card->chans[B1].rx);
		hfc_configure_int_chan(&card->chans[B2].rx);
		hfc_configure_int_chan(&card->chans[E].rx);

		// Unlock the state machine
		hfc_write(card, HFC_REG_STATES, 0);

/*
		u8 data[] = { 0x01, 0x00, 0x83 };
		memcpy(card->chans[D].tx.buffer, data, sizeof(data));

		card->chans[D].tx.pipe = usb_sndintpipe(
			card->usb_dev,
			card->chans[D].tx.int_endpoint->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);

		card->chans[D].tx.urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!card->chans[D].tx.urb) {
			printk(KERN_ERR hfc_DRIVER_PREFIX
				"usb_alloc_urb() error\n");
			err = -ENOMEM;
			return err;
		}

		usb_fill_int_urb(
			card->chans[D].tx.urb,
			card->usb_dev,
			card->chans[D].tx.pipe,
			card->chans[D].tx.buffer,
			sizeof(data),
			hfc_tx_complete,
			&card->chans[D].tx,
			1);


		err = usb_submit_urb(card->chans[D].tx.urb, GFP_KERNEL);
		if (err < 0) {
			printk(KERN_ERR hfc_DRIVER_PREFIX
				"usb_submit_urb() error %d\n", err);
			return err;
		}*/
	break;

	case B1:
	break;

	case B2:
	break;

	case E:
		BUG();
	break;
	}

	printk(KERN_INFO hfc_DRIVER_PREFIX
		"card %d: "
		"chan %s opened.\n",
		card->id,
		chan->name);

	return 0;
}

static int hfc_close(struct net_device *netdev)
{
	struct hfc_chan_duplex *chan = netdev->priv;
	struct hfc_card *card = chan->card;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	if (chan->status != open_framed) {
		spin_unlock_irqrestore(&card->lock, flags);
		return -EINVAL;
	}

	chan->status = free;

	switch (chan->id) {
	case D:
	break;

	case B1:
	break;

	case B2:
	break;

	case E:
		BUG();
	break;
	}

	spin_unlock_irqrestore(&card->lock, flags);

	printk(KERN_INFO hfc_DRIVER_PREFIX
		"card %d: "
		"chan %s closed.\n",
		card->id,
		chan->name);

	return 0;
}

static int hfc_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct hfc_chan_duplex *chan = netdev->priv;
	struct hfc_card *card = chan->card;

	netdev->trans_start = jiffies;

//	hfc_check_l1_up(chan->port);

//	hfc_fifo_put_frame(&chan->tx, skb->data, skb->len);

	dev_kfree_skb(skb);

	return 0;
}

static struct net_device_stats *hfc_get_stats(struct net_device *netdev)
{
	struct hfc_chan_duplex *chan = netdev->priv;
//	struct hfc_card *card = chan->card;

	return &chan->net_device_stats;
}

static void hfc_set_multicast_list(struct net_device *netdev)
{
	struct hfc_chan_duplex *chan = netdev->priv;
	struct hfc_card *card = chan->card;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

        if(netdev->flags & IFF_PROMISC && !card->echo_enabled) {
		if (card->nt_mode) {
			printk(KERN_INFO hfc_DRIVER_PREFIX
				"card %d: "
				"is in NT mode. Promiscuity is useless\n",
				card->id);

			spin_unlock_irqrestore(&card->lock, flags);
			return;
		}

		printk(KERN_INFO hfc_DRIVER_PREFIX
			"card %d: "
			"entered in promiscuous mode\n",
			card->id);
        } else if(!(netdev->flags & IFF_PROMISC) && card->echo_enabled) {
		if (!card->echo_enabled) {
			spin_unlock_irqrestore(&card->lock, flags);
			return;
		}

		card->echo_enabled = FALSE;

		printk(KERN_INFO hfc_DRIVER_PREFIX
			"card %d: "
			"left promiscuous mode.\n",
			card->id);
	}

	spin_unlock_irqrestore(&card->lock, flags);

//	hfc_update_fifo_state(card);
}

/******************************************
 * Module initialization and cleanup
 ******************************************/

static void hfc_setup_lapd(struct hfc_chan_duplex *chan)
{
	chan->netdev->priv = chan;
	chan->netdev->open = hfc_open;
	chan->netdev->stop = hfc_close;
	chan->netdev->hard_start_xmit = hfc_xmit_frame;
	chan->netdev->get_stats = hfc_get_stats;
	chan->netdev->set_multicast_list = hfc_set_multicast_list;
	chan->netdev->features = NETIF_F_NO_CSUM;

	chan->netdev->mtu = chan->tx.fifo_size;

	memset(chan->netdev->dev_addr, 0x00, sizeof(chan->netdev->dev_addr));

	SET_MODULE_OWNER(chan->netdev);
}

/******************************************
 * Module stuff
 ******************************************/

static void hfc_delete(struct kref *kref)
{	
	struct hfc_card *card = to_hfc_dev(kref);

	usb_put_dev(card->usb_dev);
	kfree(card);
}

static int hfc_probe(struct usb_interface *usb_intf,
	const struct usb_device_id *id)
{
	struct hfc_card *card;
        static int card_ids_counter = 0;
	int err = 0;

	struct usb_host_interface *usb_host_intf;
	int alt_idx = 0;
	for (alt_idx = 0; alt_idx < usb_intf->num_altsetting; alt_idx++) {
		usb_host_intf = &usb_intf->altsetting[alt_idx];

		printk(KERN_DEBUG "PPPPP %d %d %d\n",usb_host_intf->desc.bInterfaceClass, usb_host_intf->desc.bNumEndpoints, usb_intf->altsetting[alt_idx].endpoint[0].desc.bEndpointAddress);

		if (usb_host_intf->desc.bInterfaceClass == 0x0a &&
			usb_host_intf->desc.bNumEndpoints == 8 &&
			usb_intf->altsetting[alt_idx].endpoint[0].desc.bEndpointAddress == 0x01) goto found;
	}
	return -ENODEV;

found:

	card = kmalloc(sizeof(*card), GFP_KERNEL);
	if (card == NULL) {
		err("Out of memory");
		err = -ENOMEM;
		goto err_kmalloc;
	}

	memset(card, 0x00, sizeof(*card));
	kref_init(&card->kref);

	card->usb_dev = usb_get_dev(interface_to_usbdev(usb_intf));
	card->usb_interface = usb_intf;

	usb_set_interface(card->usb_dev,
		usb_host_intf->desc.bInterfaceNumber, alt_idx);

	// save our data pointer in this interface device
	usb_set_intfdata(usb_intf, card);

	spin_lock_init(&card->lock);
	card->id = card_ids_counter;
	struct hfc_chan_duplex *chan;
//---------------------------------- D

	chan = &card->chans[D];

	chan->card = card;
	chan->name = "D";
	chan->status = free;
	chan->id = D;
	chan->protocol = ETH_P_LAPD;

	chan->tx.chan      = chan;
	chan->tx.fifo_id = 4;
	chan->rx.chan    = chan;
	chan->rx.fifo_id = 5;

	chan->netdev = alloc_netdev(0, "isdn%dd", setup_lapd);
	if(!chan->netdev) {
		printk(KERN_ERR hfc_DRIVER_PREFIX
			"net_device alloc failed, abort.\n");
		err = -ENOMEM;
		goto err_alloc_netdev_d;
	}

	hfc_setup_lapd(chan);

	SET_NETDEV_DEV(chan->netdev, &usb_intf->dev);

	if((err = register_netdev(chan->netdev))) {
		printk(KERN_INFO hfc_DRIVER_PREFIX
			"card %d: "
			"Cannot register net device %s, aborting.\n",
			card->id,
			chan->netdev->name);
		goto err_register_netdev_d;
	}

//---------------------------------- B1
	chan = &card->chans[B1];

	chan->card = card;
	chan->name = "B1";
	chan->status = free;
	chan->id = B1;
	chan->protocol = 0;

	chan->tx.chan    = chan;
	chan->tx.fifo_id = 0;
	chan->rx.chan    = chan;
	chan->rx.fifo_id = 1;

//---------------------------------- B2
	chan = &card->chans[B2];

	chan->card = card;
	chan->name = "B2";
	chan->status = free;
	chan->id = B2;
	chan->protocol = 0;

	chan->tx.chan    = chan;
	chan->tx.fifo_id = 2;
	chan->rx.chan    = chan;
	chan->rx.fifo_id = 3;

//---------------------------------- E
	chan = &card->chans[E];

	chan->card = card;
	chan->name = "E";
	chan->status = free;
	chan->id = E;
	chan->protocol = 0;

	chan->tx.chan    = chan;
	chan->tx.fifo_id = 6;
	chan->rx.chan    = chan;
	chan->rx.fifo_id = 7;

	int i;
	for(i = 0; i < card->usb_interface->cur_altsetting->desc.bNumEndpoints; i++) {
		struct usb_host_endpoint *ep;

		ep = &card->usb_interface->cur_altsetting->endpoint[i];

		switch(ep->desc.bEndpointAddress) {
		case 0x01: card->chans[B1].tx.int_endpoint = ep; break;
		case 0x81: card->chans[B1].rx.int_endpoint = ep; break;
		case 0x02: card->chans[B2].tx.int_endpoint = ep; break;
		case 0x82: card->chans[B2].rx.int_endpoint = ep; break;
		case 0x03: card->chans[D].tx.int_endpoint = ep; break;
		case 0x83: card->chans[D].rx.int_endpoint = ep; break;
		case 0x04: card->chans[E].tx.int_endpoint = ep; break;
		case 0x84: card->chans[E].rx.int_endpoint = ep; break;

		case 0x05: card->chans[B1].tx.iso_endpoint = ep; break;
		case 0x85: card->chans[B1].rx.iso_endpoint = ep; break;
		case 0x06: card->chans[B2].tx.iso_endpoint = ep; break;
		case 0x86: card->chans[B2].rx.iso_endpoint = ep; break;
		case 0x07: card->chans[D].tx.iso_endpoint = ep; break;
		case 0x87: card->chans[D].rx.iso_endpoint = ep; break;
		case 0x08: card->chans[E].tx.iso_endpoint = ep; break;
		case 0x88: card->chans[E].rx.iso_endpoint = ep; break;
		}
	}

	for(i=0; i<4; i++)
	{
		printk(KERN_DEBUG "------> %s %p %p %p %p\n",card->chans[i].name, card->chans[i].rx.int_endpoint, card->chans[i].tx.int_endpoint, card->chans[i].rx.iso_endpoint, card->chans[i].tx.iso_endpoint);
	}

// -------------------------------------------------------

	snprintf(card->proc_dir_name,
			sizeof(card->proc_dir_name),
			"%d", card->id);
	card->proc_dir = proc_mkdir(card->proc_dir_name, hfc_proc_hfc_dir);
	card->proc_dir->owner = THIS_MODULE;

	card->proc_info = create_proc_read_entry(
			"info", 0444, card->proc_dir,
			hfc_proc_read_info, card);
	card->proc_info->owner = THIS_MODULE;

	card->proc_fifos = create_proc_read_entry(
			"fifos", 0400, card->proc_dir,
			hfc_proc_read_fifos, card);
	card->proc_fifos->owner = THIS_MODULE;

	card->pipe_in = usb_rcvctrlpipe(card->usb_dev, 0);
	card->pipe_out = usb_sndctrlpipe(card->usb_dev, 0);

	hfc_reset_card(card);

	printk(KERN_INFO hfc_DRIVER_PREFIX
		"device %d configured\n",
		card->id);

	card_ids_counter++;

	return 0;

//	unregister_netdev(card->chans[D].netdev);
err_register_netdev_d:
	free_netdev(card->chans[D].netdev);
err_alloc_netdev_d:
	usb_set_intfdata(usb_intf, NULL);
	kref_put(&card->kref, hfc_delete);
err_kmalloc:
	return err;
}

static void hfc_disconnect(struct usb_interface *usb_intf)
{
	struct hfc_card *card;

	card = usb_get_intfdata(usb_intf);
	usb_set_intfdata(usb_intf, NULL);

	unregister_netdev(card->chans[D].netdev);

	printk(KERN_INFO hfc_DRIVER_PREFIX
		"card %d: "
		"shutting down.\n",
		card->id);

	unsigned long flags;
	spin_lock_irqsave(&card->lock,flags);

	// softreset clears all pending interrupts
//	hfc_softreset(card);

	spin_unlock_irqrestore(&card->lock,flags);

	remove_proc_entry("fifos", card->proc_dir);
	remove_proc_entry("info", card->proc_dir);
	remove_proc_entry(card->proc_dir_name, hfc_proc_hfc_dir);

	free_netdev(card->chans[D].netdev);

	/* decrement our usage count */
	kref_put(&card->kref, hfc_delete);
}

static struct usb_driver hfc_driver = {
	.owner =	THIS_MODULE,
	.name =		hfc_DRIVER_NAME,
	.probe =	hfc_probe,
	.disconnect =	hfc_disconnect,
	.id_table =	hfc_table,
};

static int __init hfc_init_module(void)
{
	int err;

	printk(KERN_INFO hfc_DRIVER_PREFIX
		hfc_DRIVER_DESCR " loading\n");

	hfc_proc_hfc_dir = proc_mkdir(hfc_DRIVER_NAME, proc_root_driver);

	err = usb_register(&hfc_driver);
	if (err)
		err("usb_register failed. Error number %d", err);

	return err;
}
module_init(hfc_init_module);

static void __exit hfc_module_exit(void)
{
	usb_deregister(&hfc_driver);

	remove_proc_entry(hfc_DRIVER_NAME, proc_root_driver);

	printk(KERN_INFO hfc_DRIVER_PREFIX
		hfc_DRIVER_DESCR " unloaded\n");
}

module_exit(hfc_module_exit);

MODULE_DESCRIPTION(hfc_DRIVER_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

#ifdef LINUX26
module_param(nt_mode, int, 0444);
#ifdef DEBUG
module_param(debug_level, int, 0444);
#endif

#else

MODULE_PARM(nt_mode,"i");
#ifdef DEBUG
MODULE_PARM(debug_level,"i");
#endif

#endif // LINUX26

MODULE_PARM_DESC(nt_mode, "Don't allow L1 to go down");
