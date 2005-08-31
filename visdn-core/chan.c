/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>

#include "visdn.h"
#include "visdn_mod.h"
#include "chan.h"
#include "port.h"

#include <lapd.h>

static int visdn_chan_hotplug(struct device *device, char **envp,
	int num_envp, char *buf, int size)
{
//	struct visdn_chan *visdn_chan = to_visdn_chan(cd);

	envp[0] = NULL;

	printk(KERN_DEBUG visdn_MODULE_PREFIX "visdn_chan_hotplug called\n");

	return 0;
}

static void visdn_chan_release(struct device *device)
{
	printk(KERN_DEBUG visdn_MODULE_PREFIX "visdn_chan_release called\n");

	struct visdn_chan *chan = to_visdn_chan(device);

	BUG_ON(!chan->ops);

	if (chan->ops->release)
		chan->ops->release(chan);
}

struct visdn_chan *visdn_chan_alloc(void)
{
	struct visdn_chan *visdn_chan;

	visdn_chan = kmalloc(sizeof(*visdn_chan), GFP_KERNEL);
	if (!visdn_chan)
		return NULL;

	memset(visdn_chan, 0x00, sizeof(*visdn_chan));

	return visdn_chan;
}
EXPORT_SYMBOL(visdn_chan_alloc);

void visdn_chan_init(
	struct visdn_chan *chan,
	struct visdn_chan_ops *ops)
{
	BUG_ON(!chan);
	BUG_ON(!ops);

	memset(chan, 0x00, sizeof(*chan));
	chan->ops = ops;
}
EXPORT_SYMBOL(visdn_chan_init);

int visdn_frame_rx(struct visdn_chan *chan, struct sk_buff *skb)
{
	if (chan->connected_chan)
		return chan->connected_chan->ops->frame_xmit(
				chan->connected_chan, skb);

	return -EOPNOTSUPP;
}

EXPORT_SYMBOL(visdn_frame_rx);

static int visdn_suspend(struct device *device, pm_message_t state)
{
	printk(KERN_INFO "######### visdn_suspend()\n");

	return -EINVAL;
}

static int visdn_resume(struct device *dev)
{
	printk(KERN_INFO "######### visdn_resume()\n");

	return -EINVAL;
}

struct bus_type visdn_bus_type = {
	.name           = "visdn",
	.suspend        = visdn_suspend,
	.resume         = visdn_resume,
	.hotplug	= visdn_chan_hotplug,
};

struct visdn_search_pars
{
	char chanid[60];
	struct device *device;
};

static int visdn_search_chan_cback(struct device *device, void *data)
{
	struct visdn_search_pars *pars = data;

	if (!strcmp(device->bus_id, pars->chanid)) {
		get_device(device);
		pars->device = device;

		get_device(device);

		return 1;
	}

	return 0;
}

struct visdn_chan *visdn_search_chan(const char *chanid)
{
	struct visdn_search_pars pars;
	memset(&pars, 0x00, sizeof(pars));

	printk(KERN_DEBUG "Searching chan %s\n", chanid);

	snprintf(pars.chanid, sizeof(pars.chanid), "%s", chanid);

	bus_for_each_dev(&visdn_bus_type, NULL, &pars,
		visdn_search_chan_cback);

	if (pars.device)
		return to_visdn_chan(pars.device);
	else
		return NULL;
}
EXPORT_SYMBOL(visdn_search_chan);

int visdn_connect(
	struct visdn_chan *chan1,
	struct visdn_chan *chan2,
	int flags)
{
	int err = 0;

	BUG_ON(!chan1);
	BUG_ON(!chan1->ops);
	BUG_ON(!chan2);
	BUG_ON(!chan2->ops);

	// Check compatibility
	if (!(chan1->framing_supported & chan2->framing_supported))
		return -EXDEV;

	// Bitorder may be converted manually inside stream copying routines TODO
	if (!(chan1->bitorder_supported & chan2->bitorder_supported))
		return -EXDEV;

	// Disconnect previously connected chans
	if (chan1->connected_chan)
		visdn_disconnect(chan1, chan1->connected_chan);

	if (chan2->connected_chan)
		visdn_disconnect(chan2, chan2->connected_chan);

	printk(KERN_DEBUG "Connecting chan '%s' to chan '%s'\n",
		chan1->device.bus_id,
		chan2->device.bus_id);

	printk(KERN_DEBUG "chan1 fsup=%#x fpref=%#x bsup=%#x bpref=%#x\n",
		chan1->framing_supported,
		chan1->framing_preferred,
		chan1->bitorder_supported,
		chan1->bitorder_preferred);

	printk(KERN_DEBUG "chan2 fsup=%#x fpref=%#x bsup=%#x bpref=%#x\n",
		chan1->framing_supported,
		chan1->framing_preferred,
		chan1->bitorder_supported,
		chan1->bitorder_preferred);

	unsigned long framing;
	framing = chan1->framing_supported & chan2->framing_supported &
		 (chan1->framing_preferred | chan2->framing_preferred);

	if (framing)
		framing = 1 << find_first_bit(&framing, sizeof(framing));
	else {
		framing = chan1->framing_supported & chan2->framing_supported;
		framing = 1 << find_first_bit(&framing, sizeof(framing));
	}

	unsigned long bitorder;
	bitorder = chan1->bitorder_supported & chan2->bitorder_supported &
		  (chan1->bitorder_preferred | chan2->bitorder_preferred);

	if (bitorder)
		bitorder = 1 << find_first_bit(&bitorder, sizeof(bitorder));
	else {
		bitorder = chan1->bitorder_supported & chan2->bitorder_supported;
		bitorder = 1 << find_first_bit(&bitorder, sizeof(bitorder));
	}

	printk(KERN_DEBUG "select f=%#lx b=%#lx\n",
		framing,
		bitorder);

	chan1->framing_current = framing;
	chan1->bitorder_current = bitorder;

	chan2->framing_current = framing;
	chan2->bitorder_current = bitorder;

	if (chan1->ops->connect_to)
		err = chan1->ops->connect_to(chan1, chan2, flags);

	if (err < 0)
		return err;

	if (err == VISDN_CONNECT_BRIDGED)

	if (!(flags & VISDN_CONNECT_FLAG_SIMPLEX)) {
		if (chan2->ops->connect_to)
			err = chan2->ops->connect_to(chan2, chan1, flags);

		if (err < 0) {
			if (chan1->ops->disconnect)
				chan1->ops->disconnect(chan1);
			return err;
		}
	}

	get_device(&chan1->device);
	chan2->connected_chan = chan1;

	get_device(&chan2->device);
	chan1->connected_chan = chan2;

	sysfs_create_link(
		&chan1->device.kobj,
		&chan2->device.kobj,
		"connected");

	sysfs_create_link(
		&chan2->device.kobj,
		&chan1->device.kobj,
		"connected");

	if (chan1->autoopen && chan2->autoopen) {
		visdn_open(chan1);
		visdn_open(chan2);
	}

	return 0;
}
EXPORT_SYMBOL(visdn_connect);

int visdn_disconnect(
	struct visdn_chan *chan1,
	struct visdn_chan *chan2)
{
	BUG_ON(chan1->connected_chan != chan2);
	BUG_ON(chan2->connected_chan != chan1);

	if (chan1->ops->disconnect)
		chan1->ops->disconnect(chan1);

	if (chan2->ops->disconnect)
		chan2->ops->disconnect(chan2);

	sysfs_remove_link(&chan1->device.kobj, "connected");
	sysfs_remove_link(&chan2->device.kobj, "connected");

	chan1->connected_chan = NULL;
	put_device(&chan2->device);

	chan2->connected_chan = NULL;
	put_device(&chan1->device);

	if (chan1->open)
		visdn_close(chan1);

	if (chan2->open)
		visdn_close(chan2);

	return 0;
}
EXPORT_SYMBOL(visdn_disconnect);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_port_name(
        struct device *device,
        char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n",
		to_visdn_port(device->parent)->port_name);
}

static DEVICE_ATTR(port_name, S_IRUGO,
		visdn_chan_show_port_name,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_role(
	struct device *device,
	char *buf)
{
	switch(to_visdn_chan(device)->role) {
		case VISDN_CHAN_ROLE_B:
			return snprintf(buf, PAGE_SIZE, "B\n");
		case VISDN_CHAN_ROLE_D:
			return snprintf(buf, PAGE_SIZE, "D\n");
		case VISDN_CHAN_ROLE_E:
			return snprintf(buf, PAGE_SIZE, "E\n");
		case VISDN_CHAN_ROLE_S:
			return snprintf(buf, PAGE_SIZE, "S\n");
		case VISDN_CHAN_ROLE_Q:
			return snprintf(buf, PAGE_SIZE, "Q\n");
		default:
			return snprintf(buf, PAGE_SIZE, "UNKNOWN!\n");
	}
}

static ssize_t visdn_chan_store_role(
	struct device *device,
	const char *buf,
	size_t count)
{
	return -EOPNOTSUPP;
}

static DEVICE_ATTR(role, S_IRUGO | S_IWUSR,
		visdn_chan_show_role,
		visdn_chan_store_role);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_roles(
	struct device *device,
	char *buf)
{
	struct visdn_chan *chan = to_visdn_chan(device);
	ssize_t curpos = 0;

	if (chan->roles & VISDN_CHAN_ROLE_B)
		curpos += snprintf(buf + curpos, PAGE_SIZE - curpos, "B\n");

	if (chan->roles & VISDN_CHAN_ROLE_D)
		curpos += snprintf(buf + curpos, PAGE_SIZE - curpos, "D\n");

	if (chan->roles & VISDN_CHAN_ROLE_E)
		curpos += snprintf(buf + curpos, PAGE_SIZE - curpos, "E\n");

	if (chan->roles & VISDN_CHAN_ROLE_S)
		curpos += snprintf(buf + curpos, PAGE_SIZE - curpos, "S\n");

	if (chan->roles & VISDN_CHAN_ROLE_Q)
		curpos += snprintf(buf + curpos, PAGE_SIZE - curpos, "Q\n");

	return curpos;
}

static DEVICE_ATTR(roles, S_IRUGO,
		visdn_chan_show_roles,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_speed(
	struct device *device,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
		to_visdn_chan(device)->speed);
}

static ssize_t visdn_chan_store_speed(
	struct device *device,
	const char *buf,
	size_t count)
{
	return -EINVAL;
}

static DEVICE_ATTR(speed, S_IRUGO | S_IWUSR,
		visdn_chan_show_speed,
		visdn_chan_store_speed);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_framing(
	struct device *device,
	char *buf)
{
	struct visdn_chan *chan = to_visdn_chan(device);
	const char *name;

	switch (chan->framing_current) {
	case VISDN_CHAN_FRAMING_TRANS:
		name = "trans";
	break;

	case VISDN_CHAN_FRAMING_HDLC:
		name = "hdlc";
	break;

	case VISDN_CHAN_FRAMING_MTP:
		name = "mtp";
	break;

	default:
		WARN_ON(1);
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE, "%s\n", name);
}

static DEVICE_ATTR(framing, S_IRUGO,
		visdn_chan_show_framing,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_bitorder(
	struct device *device,
	char *buf)
{
	struct visdn_chan *chan = to_visdn_chan(device);

	return snprintf(buf, PAGE_SIZE, "%s\n",
		chan->bitorder_current == VISDN_CHAN_BITORDER_LSB ? "lsb" : "msb");
}

static DEVICE_ATTR(bitorder, S_IRUGO,
		visdn_chan_show_bitorder,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_autoopen(
	struct device *device,
	char *buf)
{
	struct visdn_chan *chan = to_visdn_chan(device);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		chan->autoopen ? 1 : 0);
}

static DEVICE_ATTR(autoopen, S_IRUGO,
		visdn_chan_show_autoopen,
		NULL);

//----------------------------------------------------------------------------



int visdn_chan_register(
	struct visdn_chan *chan,
	const char *name,
	struct visdn_port *port)
{
	int err;

	BUG_ON(!chan);
	BUG_ON(!name);
	BUG_ON(!port);

	chan->device.parent = &port->device;

	snprintf(chan->device.bus_id, sizeof(chan->device.bus_id),
		"%d.%s", port->index, name);

	chan->device.bus = &visdn_bus_type;
//	chan->device.driver = port->device.driver;
	chan->device.driver_data = NULL;
	chan->device.release = visdn_chan_release;

	err = device_register(&chan->device);
	if (err < 0)
		goto err_device_register;

	err = device_create_file(
			&chan->device,
			&dev_attr_port_name);
	if (err < 0)
		goto err_device_create_file;

	err = device_create_file(
			&chan->device,
			&dev_attr_role);
	if (err < 0)
		goto err_device_create_file;

	err = device_create_file(
			&chan->device,
			&dev_attr_roles);
	if (err < 0)
		goto err_device_create_file;

	err = device_create_file(
			&chan->device,
			&dev_attr_framing);
	if (err < 0)
		goto err_device_create_file;

	err = device_create_file(
			&chan->device,
			&dev_attr_bitorder);
	if (err < 0)
		goto err_device_create_file;

	err = device_create_file(
			&chan->device,
			&dev_attr_speed);
	if (err < 0)
		goto err_device_create_file;

	err = device_create_file(
			&chan->device,
			&dev_attr_autoopen);
	if (err < 0)
		goto err_device_create_file;

	sysfs_create_link(
		&chan->device.parent->kobj,
		&chan->device.kobj,
		(char *)name); // should sysfs_create_link(...., const char *name) ?

	chan->port = port;

	return 0;

err_device_create_file:
	device_unregister(&chan->device);
err_device_register:

	return err;
}
EXPORT_SYMBOL(visdn_chan_register);

void visdn_chan_unregister(
	struct visdn_chan *visdn_chan)
{
	printk(KERN_DEBUG visdn_MODULE_PREFIX "visdn_chan_unregister called\n");

	if (visdn_chan->connected_chan)
		visdn_disconnect(visdn_chan, visdn_chan->connected_chan);

	device_unregister(&visdn_chan->device);
}
EXPORT_SYMBOL(visdn_chan_unregister);

static ssize_t visdn_bus_connect_store(
	struct bus_type *bus_type,
	const char *buf,
	size_t count)
{
	int err;
	char locbuf[100];

	char *locbuf_p = locbuf;

	strncpy(locbuf, buf, sizeof(locbuf));
	char *chan1_name = strsep(&locbuf_p, ",\n\r");
	if (!chan1_name)
		return -EINVAL;

	char *chan2_name = strsep(&locbuf_p, ",\n\r");
	if (!chan2_name)
		return -EINVAL;

printk(KERN_INFO "CONNECT(%s,%s)\n", chan1_name, chan2_name);

	struct visdn_chan *chan1 = visdn_search_chan(chan1_name);
	if (!chan1) {
		err = -ENODEV;
		goto err_search_src;
	}

	struct visdn_chan *chan2 = visdn_search_chan(chan2_name);
	if (!chan2) {
		err = -ENODEV;
		goto err_search_dst;
	}

	if (chan1 == chan2) {
		err = -EINVAL;
		goto err_connect_self;
	}

	err = visdn_connect(chan1, chan2, 0);
	if (err < 0)
		goto err_connect;

	// Release references returned by visdn_search_chan()
	put_device(&chan1->device);
	put_device(&chan2->device);

        return count;

err_connect:
err_connect_self:
	put_device(&chan2->device);
err_search_dst:
	put_device(&chan1->device);
err_search_src:

	return err;
}

BUS_ATTR(connect, S_IWUSR, NULL, visdn_bus_connect_store);

int visdn_chan_modinit(void)
{
	int err;

	err = bus_register(&visdn_bus_type);
	if (err < 0)
		goto err_bus_register;

	err = bus_create_file(&visdn_bus_type, &bus_attr_connect);
	if (err < 0)
		goto err_bus_create_file;

	return 0;

	bus_remove_file(&visdn_bus_type, &bus_attr_connect);
err_bus_create_file:
	bus_unregister(&visdn_bus_type);
err_bus_register:

	return err;
}

void visdn_chan_modexit(void)
{
	bus_unregister(&visdn_bus_type);
}
