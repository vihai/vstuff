#include <linux/kernel.h>
#include <linux/spinlock.h>

#include "hfc-4s.h"
#include "pcm_port.h"
#include "pcm_port_sysfs.h"
#include "card.h"
#include "card_inline.h"

static ssize_t hfc_show_f0io_counter(
	struct device *device,
	char *buf)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_pcm_port *port = to_pcm_port(visdn_port);
	struct hfc_card *card = port->card;

	unsigned long flags;
	spin_lock_irqsave(&card->lock, flags);

	u16 counter;

	counter = hfc_inb(card, hfc_R_F0_CNTL);
	counter += hfc_inb(card, hfc_R_F0_CNTH) << 8;

	spin_unlock_irqrestore(&card->lock, flags);

	return snprintf(buf, PAGE_SIZE, "%d\n", counter);

}

static DEVICE_ATTR(f0io_counter, S_IRUGO,
		hfc_show_f0io_counter,
		NULL);

int hfc_pcm_port_sysfs_create_files(
        struct hfc_pcm_port *port)
{
	int err;

	err = device_create_file(
		&port->visdn_port.device,
		&dev_attr_f0io_counter);
	if (err < 0)
		goto err_device_create_file_f0io_counter;

	return 0;

	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_f0io_counter);
err_device_create_file_f0io_counter:

	return err;

	return 0;
}

void hfc_pcm_port_sysfs_delete_files(
        struct hfc_pcm_port *port)
{
	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_f0io_counter);
}
