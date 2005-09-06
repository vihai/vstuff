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
#include "cxc.h"

#include <lapd.h>

struct visdn_cxc visdn_cxc;
EXPORT_SYMBOL(visdn_cxc);

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
	BUG_ON(!ops->owner);

	memset(chan, 0x00, sizeof(*chan));
	chan->ops = ops;

	init_MUTEX(&chan->sem);
}
EXPORT_SYMBOL(visdn_chan_init);

int visdn_frame_rx(struct visdn_chan *chan, struct sk_buff *skb)
{
	int res = -ENODEV;

	struct visdn_chan *dst;
	dst = visdn_cxc_get_by_src(&visdn_cxc, chan);

	if (dst) {
		if (dst->ops->frame_xmit)
			res = dst->ops->frame_xmit(dst, skb);

		visdn_chan_put(dst);
	}

	return res;
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
	struct visdn_chan *chan;
};

static int visdn_search_chan_cback(struct device *device, void *data)
{
	struct visdn_search_pars *pars = data;

	if (!strcmp(device->bus_id, pars->chanid)) {
		struct visdn_chan *chan = to_visdn_chan(device);

		visdn_chan_get(chan);
		pars->chan = chan;

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

	return pars.chan;
}
EXPORT_SYMBOL(visdn_search_chan);

static int visdn_default_update_parameters(
	struct visdn_chan *chan,
	struct visdn_chan_pars *pars)
{
	memcpy(&chan->pars, pars, sizeof(chan->pars));

	return 0;
}

int visdn_negotiate_parameters(
	struct visdn_chan *chan1,
	struct visdn_chan *chan2)
{
	int err;

	visdn_chan_lock2(chan1, chan2);

	// Negotiate channel parameters -------------------------

	struct visdn_chan_pars pars;

	memset(&pars, 0, sizeof(pars));

	if (chan1->max_mtu && chan2->max_mtu)
		pars.mtu = min(chan1->max_mtu, chan2->max_mtu);
	else if (chan1->max_mtu)
		pars.mtu = chan1->max_mtu;
	else if (chan2->max_mtu)
		pars.mtu = chan2->max_mtu;

	if (chan1->bitrate_selection ==
			VISDN_CHAN_BITRATE_SELECTION_MAX &&
	    chan2->bitrate_selection ==
			VISDN_CHAN_BITRATE_SELECTION_MAX) {
		pars.bitrate = 1000000000;
	} else if (chan1->bitrate_selection ==
				VISDN_CHAN_BITRATE_SELECTION_MAX &&
	           chan2->bitrate_selection ==
				VISDN_CHAN_BITRATE_SELECTION_LIST) {
		BUG_ON(chan2->bitrates_cnt <= 0);
		pars.bitrate = chan2->bitrates[chan2->bitrates_cnt - 1];
	} else if (chan1->bitrate_selection ==
				VISDN_CHAN_BITRATE_SELECTION_LIST &&
	           chan2->bitrate_selection ==
				 VISDN_CHAN_BITRATE_SELECTION_MAX) {
		BUG_ON(chan1->bitrates_cnt <= 0);
		pars.bitrate = chan1->bitrates[chan1->bitrates_cnt - 1];
	} else {
		int c1_idx = 0;
		int c2_idx = 0;

		// Do a merge-search on the two lists finding the biggest
		// common bitrate
		while (c1_idx < chan1->bitrates_cnt &&
		       c2_idx < chan2->bitrates_cnt) {
			if (chan1->bitrates[c1_idx] ==
					chan2->bitrates[c2_idx]) {
				pars.bitrate = chan1->bitrates[c1_idx];
				c1_idx++;
				c2_idx++;
			} else if (chan1->bitrates[c1_idx] >
					chan2->bitrates[c2_idx]) {
				c2_idx++;
			} else {
				c1_idx++;
			}
		}
	}

	if (!pars.bitrate) {
		err = -ENOTSUPP;
		goto err_bitrate;
	}

	unsigned long framing;
	framing = chan1->framing_supported & chan2->framing_supported &
		 (chan1->framing_preferred | chan2->framing_preferred);

	if (!framing)
		framing = chan1->framing_supported & chan2->framing_supported;

	if (!framing) {
		err = -ENOTSUPP;
		goto err_bitorder;
		goto err_framing;
	}

	pars.framing = 1 << find_first_bit(&framing, sizeof(framing));

	unsigned long bitorder;
	bitorder = chan1->bitorder_supported & chan2->bitorder_supported &
		  (chan1->bitorder_preferred | chan2->bitorder_preferred);

	if (!bitorder)
		bitorder = chan1->bitorder_supported & chan2->bitorder_supported;

	if (!bitorder) {
		err = -ENOTSUPP;
		goto err_bitorder;
	}

	pars.bitorder = 1 << find_first_bit(&bitorder, sizeof(bitorder));

	// Apply negotiated parameters ------------------------------

	if (chan1->ops->update_parameters)
		chan1->ops->update_parameters(chan1, &pars);
	else
		visdn_default_update_parameters(chan1, &pars);

	if (chan2->ops->update_parameters)
		chan2->ops->update_parameters(chan2, &pars);
	else
		visdn_default_update_parameters(chan2, &pars);

	visdn_chan_unlock(chan1);
	visdn_chan_unlock(chan2);

	return 0;

err_bitorder:
err_framing:
err_bitrate:

	visdn_chan_unlock(chan1);
	visdn_chan_unlock(chan2);

	return err;
}
EXPORT_SYMBOL(visdn_negotiate_parameters);

int visdn_connect(
	struct visdn_chan *chan1,
	struct visdn_chan *chan2,
	int flags)
{
	int err;

	BUG_ON(!chan1);
	BUG_ON(!chan1->ops);
	BUG_ON(!chan2);
	BUG_ON(!chan2->ops);

	printk(KERN_DEBUG "Connecting chan '%s' to chan '%s'\n",
		chan1->device.bus_id,
		chan2->device.bus_id);

	// Check that the channels are disconnected
	visdn_cxc_del(&visdn_cxc, chan1);
	visdn_cxc_del(&visdn_cxc, chan2);

	err = visdn_negotiate_parameters(chan1, chan2);
	if (err < 0)
		goto err_negotiate_parameters;

	// Connect the channels -------------------------------------
	err = visdn_cxc_add(&visdn_cxc, chan1, chan2);
	if (err < 0)
		goto err_cxc_add_chan1;

	err = visdn_cxc_add(&visdn_cxc, chan2, chan1);
	if (err < 0)
		goto err_cxc_add_chan2;

	if (chan1->autoopen && chan2->autoopen) {
		err = visdn_open(chan1);
		if (err < 0)
			goto err_open;

		if (err == VISDN_CHAN_OPEN_RENEGOTIATE)
			visdn_negotiate_parameters(chan1, chan2);
			// What if negotiation fails??

		visdn_open(chan2);
		if (err < 0) {
			visdn_close(chan1);
			goto err_open;
		}

		if (err == VISDN_CHAN_OPEN_RENEGOTIATE)
			visdn_negotiate_parameters(chan1, chan2);
			// What if negotiation fails??
	}

	sysfs_create_link(
		&chan1->device.kobj,
		&chan2->device.kobj,
		"connected");

	sysfs_create_link(
		&chan2->device.kobj,
		&chan1->device.kobj,
		"connected");

	return 0;

err_open:
	// cxc_del
err_cxc_add_chan2:
err_cxc_add_chan1:
err_negotiate_parameters:

	return err;
}
EXPORT_SYMBOL(visdn_connect);

int visdn_disconnect(
	struct visdn_chan *chan)
{
	printk(KERN_DEBUG "visdn_disconnect()\n");

	struct visdn_chan *chan2;
	chan2 = visdn_cxc_get_by_src(&visdn_cxc, chan);

	if (chan2) {
		visdn_cxc_del(&visdn_cxc, chan2);

		sysfs_remove_link(&chan2->device.kobj, "connected");

		if (test_bit(VISDN_CHAN_STATE_OPEN, &chan2->state))
			visdn_close(chan2);

		visdn_chan_put(chan2);
	}

	visdn_cxc_del(&visdn_cxc, chan);

	sysfs_remove_link(&chan->device.kobj, "connected");

	if (test_bit(VISDN_CHAN_STATE_OPEN, &chan->state))
		visdn_close(chan);

	return 0;
}
EXPORT_SYMBOL(visdn_disconnect);





int visdn_pass_open(
	struct visdn_chan *chan)
{
	int err;
	struct visdn_chan *dst;
	dst = visdn_cxc_get_by_src(&visdn_cxc, chan);
	if (!dst) {
		err = -ENODEV;
		goto err_no_dst;
	}

	if (!try_module_get(dst->ops->owner)) {
		err = -ENODEV;
		goto err_module_get;
	}

	err = visdn_open(dst);
	printk(KERN_DEBUG "visdn_open() = %d\n", err);

	module_put(dst->ops->owner);
err_module_get:
	visdn_chan_put(dst);
err_no_dst:

	return err;
}
EXPORT_SYMBOL(visdn_pass_open);

int visdn_pass_close(
	struct visdn_chan *chan)
{
	int err;
	struct visdn_chan *dst;
	dst = visdn_cxc_get_by_src(&visdn_cxc, chan);
	if (!dst) {
		err = -ENODEV;
		goto err_no_dst;
	}

	if (!try_module_get(dst->ops->owner)) {
		err = -ENODEV;
		goto err_module_get;
	}

	if (dst->ops->close)
		err = dst->ops->close(dst);
	else
		err = 0;

	module_put(dst->ops->owner);
err_module_get:
	visdn_chan_put(dst);
err_no_dst:

	return err;
}
EXPORT_SYMBOL(visdn_pass_close);

int visdn_pass_frame_xmit(
	struct visdn_chan *chan,
	struct sk_buff *skb)
{
	int err;
	struct visdn_chan *dst;
	dst = visdn_cxc_get_by_src(&visdn_cxc, chan);
	if (!dst) {
		err = -ENODEV;
		goto err_no_dst;
	}

	if (!try_module_get(dst->ops->owner)) {
		err = -ENODEV;
		goto err_module_get;
	}

	if (dst->ops->frame_xmit)
		err = dst->ops->frame_xmit(dst, skb);
	else
		err = -ENOTSUPP;

	module_put(dst->ops->owner);
err_module_get:
	visdn_chan_put(dst);
err_no_dst:

	return err;
}
EXPORT_SYMBOL(visdn_pass_frame_xmit);

static struct net_device_stats vnd_dummy_stats = { };

struct net_device_stats *visdn_pass_get_stats(
	struct visdn_chan *chan)
{
	struct net_device_stats *stats = &vnd_dummy_stats;

	struct visdn_chan *dst;
	dst = visdn_cxc_get_by_src(&visdn_cxc, chan);
	if (!dst)
		goto err_no_dst;

	if (!try_module_get(dst->ops->owner))
		goto err_module_get;

	if (dst->ops->get_stats)
		stats = dst->ops->get_stats(dst);

	module_put(dst->ops->owner);
err_module_get:
	visdn_chan_put(dst);
err_no_dst:

	return stats;
}
EXPORT_SYMBOL(visdn_pass_get_stats);

int visdn_pass_stop_queue(struct visdn_chan *chan)
{
	int err;
	struct visdn_chan *dst;
	dst = visdn_cxc_get_by_src(&visdn_cxc, chan);
	if (!dst) {
		err = -ENODEV;
		goto err_no_dst;
	}

	if (!try_module_get(dst->ops->owner)) {
		err = -ENODEV;
		goto err_module_get;
	}

	if (dst->ops->stop_queue) {
		dst->ops->stop_queue(dst);
		err = 0;
	} else
		err = -ENOTSUPP;

	module_put(dst->ops->owner);
err_module_get:
	visdn_chan_put(dst);
err_no_dst:

	return err;
}
EXPORT_SYMBOL(visdn_pass_stop_queue);

int visdn_pass_start_queue(struct visdn_chan *chan)
{
	int err;
	struct visdn_chan *dst;
	dst = visdn_cxc_get_by_src(&visdn_cxc, chan);
	if (!dst) {
		err = -ENODEV;
		goto err_no_dst;
	}

	if (!try_module_get(dst->ops->owner)) {
		err = -ENODEV;
		goto err_module_get;
	}

	if (dst->ops->start_queue) {
		dst->ops->start_queue(dst);
		err = 0;
	} else
		err = -ENOTSUPP;

	module_put(dst->ops->owner);
err_module_get:
	visdn_chan_put(dst);
err_no_dst:

	return err;
}
EXPORT_SYMBOL(visdn_pass_start_queue);

int visdn_pass_wake_queue(struct visdn_chan *chan)
{
	int err;
	struct visdn_chan *dst;
	dst = visdn_cxc_get_by_src(&visdn_cxc, chan);
	if (!dst) {
		err = -ENODEV;
		goto err_no_dst;
	}

	if (!try_module_get(dst->ops->owner)) {
		err = -ENODEV;
		goto err_module_get;
	}

	if (dst->ops->wake_queue) {
		dst->ops->wake_queue(dst);
		err = 0;
	} else
		err = -ENOTSUPP;

	module_put(dst->ops->owner);
err_module_get:
	visdn_chan_put(dst);
err_no_dst:
return err;
}
EXPORT_SYMBOL(visdn_pass_wake_queue);

int visdn_pass_frame_input_error(struct visdn_chan *chan, int code)
{
	int err;
	struct visdn_chan *dst;
	dst = visdn_cxc_get_by_src(&visdn_cxc, chan);
	if (!dst) {
		err = -ENODEV;
		goto err_no_dst;
	}

	if (!try_module_get(dst->ops->owner)) {
		err = -ENODEV;
		goto err_module_get;
	}

	if (dst->ops->frame_input_error) {
		dst->ops->frame_input_error(dst, code);
		err = 0;
	} else
		err = -ENOTSUPP;

	module_put(dst->ops->owner);
err_module_get:
	visdn_chan_put(dst);
err_no_dst:

	return err;
}
EXPORT_SYMBOL(visdn_pass_frame_input_error);

ssize_t visdn_pass_samples_read(
	struct visdn_chan *chan,
	char __user *buf,
	size_t count)
{
	int err;
	struct visdn_chan *dst;
	dst = visdn_cxc_get_by_src(&visdn_cxc, chan);
	if (!dst) {
		err = -ENODEV;
		goto err_no_dst;
	}

	if (!try_module_get(dst->ops->owner)) {
		err = -ENODEV;
		goto err_module_get;
	}

	if (dst->ops->samples_read)
		err = dst->ops->samples_read(dst, buf, count);
	else
		err = -ENOTSUPP;

	module_put(dst->ops->owner);
err_module_get:
	visdn_chan_put(dst);
err_no_dst:

	return err;
}
EXPORT_SYMBOL(visdn_pass_samples_read);

ssize_t visdn_pass_samples_write(
	struct visdn_chan *chan,
	const char __user *buf,
	size_t count)
{
	int err;
	struct visdn_chan *dst;
	dst = visdn_cxc_get_by_src(&visdn_cxc, chan);
	if (!dst) {
		err = -ENODEV;
		goto err_no_dst;
	}

	if (!try_module_get(dst->ops->owner)) {
		err = -ENODEV;
		goto err_module_get;
	}

	if (dst->ops->samples_write)
		err = dst->ops->samples_write(dst, buf, count);
	else
		err = -ENOTSUPP;

	module_put(dst->ops->owner);
err_module_get:
	visdn_chan_put(dst);
err_no_dst:

	return err;
}
EXPORT_SYMBOL(visdn_pass_samples_write);



//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_refcnt(
        struct device *device,
        char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
		atomic_read(&to_visdn_chan(device)->device.kobj.kref.refcount));
}

static DEVICE_ATTR(refcnt, S_IRUGO,
		visdn_chan_show_refcnt,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_port_name(
        struct device *device,
        char *buf)
{
	// FIXME: Lock chan and port!?

	return snprintf(buf, PAGE_SIZE, "%s\n",
		to_visdn_port(device->parent)->port_name);
}

static DEVICE_ATTR(port_name, S_IRUGO,
		visdn_chan_show_port_name,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_mtu(
	struct device *device,
	char *buf)
{
	struct visdn_chan *chan = to_visdn_chan(device);

	if (visdn_chan_lock_interruptible(chan))
		return -ERESTARTSYS;

	int len = snprintf(buf, PAGE_SIZE, "%d\n", chan->pars.mtu);

	visdn_chan_unlock(chan);

	return len;
}

static DEVICE_ATTR(mtu, S_IRUGO,
		visdn_chan_show_mtu,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_bitrate(
	struct device *device,
	char *buf)
{
	struct visdn_chan *chan = to_visdn_chan(device);

	if (visdn_chan_lock_interruptible(chan))
		return -ERESTARTSYS;

	int len = snprintf(buf, PAGE_SIZE, "%d\n", chan->pars.bitrate);

	visdn_chan_unlock(chan);

	return len;
}

static DEVICE_ATTR(bitrate, S_IRUGO,
		visdn_chan_show_bitrate,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_framing(
	struct device *device,
	char *buf)
{
	struct visdn_chan *chan = to_visdn_chan(device);
	int err;

	if (visdn_chan_lock_interruptible(chan)) {
		err = -ERESTARTSYS;
		goto err_lock;
	}

	const char *name;

	switch (chan->pars.framing) {
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
		err = -EINVAL;
		goto err_invalid;
	}

	int len = snprintf(buf, PAGE_SIZE, "%s\n", name);

	visdn_chan_unlock(chan);

	return len;

err_invalid:
	visdn_chan_unlock(chan);
err_lock:

	return err;
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

	if (visdn_chan_lock_interruptible(chan))
		return -ERESTARTSYS;

	int len = snprintf(buf, PAGE_SIZE, "%s\n",
			chan->pars.bitorder == VISDN_CHAN_BITORDER_LSB ?
				"lsb" : "msb");

	visdn_chan_unlock(chan);

	return len;
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

	if (visdn_chan_lock_interruptible(chan))
		return -ERESTARTSYS;

	int len = snprintf(buf, PAGE_SIZE, "%d\n",
			chan->autoopen ? 1 : 0);

	visdn_chan_unlock(chan);

	return len;
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
			&dev_attr_refcnt);
	if (err < 0)
		goto err_device_create_file_refcnt;

	err = device_create_file(
			&chan->device,
			&dev_attr_port_name);
	if (err < 0)
		goto err_device_create_file_port_name;

	err = device_create_file(
			&chan->device,
			&dev_attr_mtu);
	if (err < 0)
		goto err_device_create_file_mtu;

	err = device_create_file(
			&chan->device,
			&dev_attr_bitrate);
	if (err < 0)
		goto err_device_create_file_bitrate;

	err = device_create_file(
			&chan->device,
			&dev_attr_framing);
	if (err < 0)
		goto err_device_create_file_framing;

	err = device_create_file(
			&chan->device,
			&dev_attr_bitorder);
	if (err < 0)
		goto err_device_create_file_bitorder;

	err = device_create_file(
			&chan->device,
			&dev_attr_autoopen);
	if (err < 0)
		goto err_device_create_file_autoopen;

	sysfs_create_link(
		&chan->device.parent->kobj,
		&chan->device.kobj,
		(char *)name); // should sysfs_create_link(...., const char *name) ?

	chan->port = port;

	return 0;

	device_remove_file(&chan->device, &dev_attr_autoopen);
err_device_create_file_autoopen:
	device_remove_file(&chan->device, &dev_attr_bitorder);
err_device_create_file_bitorder:
	device_remove_file(&chan->device, &dev_attr_framing);
err_device_create_file_framing:
	device_remove_file(&chan->device, &dev_attr_bitrate);
err_device_create_file_bitrate:
	device_remove_file(&chan->device, &dev_attr_mtu);
err_device_create_file_mtu:
	device_remove_file(&chan->device, &dev_attr_port_name);
err_device_create_file_port_name:
	device_remove_file(&chan->device, &dev_attr_refcnt);
err_device_create_file_refcnt:
	device_unregister(&chan->device);
err_device_register:

	return err;
}
EXPORT_SYMBOL(visdn_chan_register);

void visdn_chan_unregister(
	struct visdn_chan *chan)
{
	printk(KERN_DEBUG visdn_MODULE_PREFIX
		"visdn_chan_unregister(%s) called\n",
		chan->device.bus_id);

	visdn_disconnect(chan);

	device_remove_file(&chan->device, &dev_attr_autoopen);
	device_remove_file(&chan->device, &dev_attr_bitorder);
	device_remove_file(&chan->device, &dev_attr_framing);
	device_remove_file(&chan->device, &dev_attr_bitrate);
	device_remove_file(&chan->device, &dev_attr_mtu);
	device_remove_file(&chan->device, &dev_attr_port_name);
	device_remove_file(&chan->device, &dev_attr_refcnt);

	device_unregister(&chan->device);

	/* RCU (and other?) may still have references to this object.
	 * Sleep until everyone has put his reference back.
	 * Maybe there's a better way to handle this.
	 */

	if (atomic_read(&chan->device.kobj.kref.refcount) > 1) {

		/* Usually 50ms are enough */
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout((50 * HZ) / 1000);

		while(atomic_read(&chan->device.kobj.kref.refcount) > 1) {
			printk(KERN_DEBUG "Waiting for %s refcnt to become 1"
					" (now %d)\n",
				chan->device.bus_id,
				atomic_read(&chan->device.kobj.kref.refcount));

			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout((1000 * HZ) / 1000);
		}
	}
}
EXPORT_SYMBOL(visdn_chan_unregister);

int visdn_chan_lock2(
	struct visdn_chan *chan1,
	struct visdn_chan *chan2)
{
	// We acquire the semaphore on both channels. To avoid deadlocks we
	// will acquire them always in the same order, using the structure
	// address.

	struct visdn_chan *chan_lock_1;
	struct visdn_chan *chan_lock_2;

	if (chan1 < chan2) {
		chan_lock_1 = chan1;
		chan_lock_2 = chan2;
	} else {
		chan_lock_1 = chan2;
		chan_lock_2 = chan1;
	}

	visdn_chan_lock(chan_lock_1);
	visdn_chan_lock(chan_lock_2);

	return 0;
}
EXPORT_SYMBOL(visdn_chan_lock2);

int visdn_chan_lock2_interruptible(
	struct visdn_chan *chan1,
	struct visdn_chan *chan2)
{
	int err;

	// We acquire the semaphore on both channels. To avoid deadlocks we
	// will acquire them always in the same order, using the structure
	// address.

	struct visdn_chan *chan_lock_1;
	struct visdn_chan *chan_lock_2;

	if (chan1 < chan2) {
		chan_lock_1 = chan1;
		chan_lock_2 = chan2;
	} else {
		chan_lock_1 = chan2;
		chan_lock_2 = chan1;
	}

	if (visdn_chan_lock_interruptible(chan_lock_1)) {
		err = -ERESTARTSYS;
		goto err_lock_1;
	}

	if (visdn_chan_lock_interruptible(chan_lock_2)) {
		err = -ERESTARTSYS;
		goto err_lock_2;
	}

	return 0;

	visdn_chan_unlock(chan_lock_2);
err_lock_2:
	visdn_chan_unlock(chan_lock_1);
err_lock_1:

	return err;
}
EXPORT_SYMBOL(visdn_chan_lock2_interruptible);

int visdn_open(struct visdn_chan *chan)
{
	int err = 0;

	if (test_bit(VISDN_CHAN_STATE_OPEN, &chan->state)) {
		WARN_ON(1);
		return -EINVAL;
	}

	if (chan->ops->open) {
		err = chan->ops->open(chan);
		if (err < 0)
			return err;
	}

	set_bit(VISDN_CHAN_STATE_OPEN, &chan->state);

	return err;
}
EXPORT_SYMBOL(visdn_open);

int visdn_close(struct visdn_chan *chan)
{
	int err = 0;

	if (!test_bit(VISDN_CHAN_STATE_OPEN, &chan->state)) {
		WARN_ON(1);
		return -EINVAL;
	}

	if (chan->ops->close) {
		err = chan->ops->close(chan);
		if (err < 0)
			return err;
	}

	clear_bit(VISDN_CHAN_STATE_OPEN, &chan->state);

	return err;
}
EXPORT_SYMBOL(visdn_close);

static ssize_t visdn_bus_connect_store(
	struct bus_type *bus_type,
	const char *buf,
	size_t count)
{
	int err;
	char locbuf[100];

	char *locbuf_p = locbuf;

	strncpy(locbuf, buf, sizeof(locbuf));
	char *chan1_id = strsep(&locbuf_p, ",\n\r");
	if (!chan1_id)
		return -EINVAL;

	char *chan2_id = strsep(&locbuf_p, ",\n\r");
	if (!chan2_id)
		return -EINVAL;

	struct visdn_chan *chan1 = visdn_search_chan(chan1_id);
	if (!chan1) {
		err = -ENODEV;
		goto err_search_src;
	}

	struct visdn_chan *chan2 = visdn_search_chan(chan2_id);
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
	visdn_chan_put(chan1);
	visdn_chan_put(chan2);

        return count;

	// visdn_disconnect
err_connect:
err_connect_self:
	visdn_chan_put(chan2);
err_search_dst:
	visdn_chan_put(chan1);
err_search_src:

	return err;
}

BUS_ATTR(connect, S_IWUSR, NULL, visdn_bus_connect_store);

int visdn_chan_modinit(void)
{
	int err;

	visdn_cxc_init(&visdn_cxc);

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
