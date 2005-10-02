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
#include <linux/kobject.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>

#include <kernel_config.h>

#include "visdn.h"
#include "visdn_mod.h"
#include "chan.h"
#include "port.h"
#include "cxc.h"

int visdn_frame_rx(struct visdn_chan *chan, struct sk_buff *skb)
{
	int res = -ENODEV;

	struct visdn_chan *dst;
	dst = visdn_cxc_get_by_src(chan->cxc, chan);

	if (dst) {
		if (dst->ops->frame_xmit)
			res = dst->ops->frame_xmit(dst, skb);

		visdn_chan_put(dst);
	}

	return res;
}

EXPORT_SYMBOL(visdn_frame_rx);

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
	else
		pars.mtu = 65536;

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

int visdn_disconnect(
	struct visdn_chan *chan)
{
	visdn_debug(3, "visdn_disconnect()\n");

	struct visdn_chan *chan2;
	chan2 = visdn_cxc_get_by_src(chan->cxc, chan);

	if (chan2) {
		visdn_cxc_disconnect(chan2->cxc, chan2);

		sysfs_remove_link(&chan2->kobj, "connected");

		if (test_bit(VISDN_CHAN_STATE_OPEN, &chan2->state))
			visdn_close(chan2);

		visdn_chan_put(chan2);
	}

	visdn_cxc_disconnect(chan->cxc, chan);

	sysfs_remove_link(&chan->kobj, "connected");

	if (test_bit(VISDN_CHAN_STATE_OPEN, &chan->state))
		visdn_close(chan);

	return 0;
}
EXPORT_SYMBOL(visdn_disconnect);

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

	if (chan1->cxc != chan2->cxc)
		return -EINVAL;

	// cxc_get ???? FIXME TODO
	struct visdn_cxc *cxc = chan1->cxc;

#if 0
	// Check that the channels are disconnected
	visdn_disconnect(chan1);
	visdn_disconnect(chan2);
#else
	struct visdn_chan *tmp_chan;
	tmp_chan = visdn_cxc_get_by_src(cxc, chan1);
	if (tmp_chan) {
		visdn_chan_put(tmp_chan);
		err = -EBUSY;
		goto err_already_connected;
	}

	tmp_chan = visdn_cxc_get_by_src(cxc, chan2);
	if (tmp_chan) {
		visdn_chan_put(tmp_chan);
		err = -EBUSY;
		goto err_already_connected;
	}
#endif

	visdn_debug(2, "Connecting chan '%s' to chan '%s'\n",
		chan1->cxc_id,
		chan2->cxc_id);

	err = visdn_negotiate_parameters(chan1, chan2);
	if (err < 0)
		goto err_negotiate_parameters;

	// Connect the channels -------------------------------------
	err = visdn_cxc_connect(cxc, chan1, chan2);
	if (err < 0)
		goto err_cxc_add_chan1;

	err = visdn_cxc_connect(cxc, chan2, chan1);
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
		&chan1->kobj,
		&chan2->kobj,
		"connected");

	sysfs_create_link(
		&chan2->kobj,
		&chan1->kobj,
		"connected");

	return 0;

err_open:
	// cxc_del
err_cxc_add_chan2:
err_cxc_add_chan1:
err_negotiate_parameters:
err_already_connected:

	return err;
}
EXPORT_SYMBOL(visdn_connect);

int visdn_pass_open(
	struct visdn_chan *chan)
{
	int err;
	struct visdn_chan *dst;
	dst = visdn_cxc_get_by_src(chan->cxc, chan);
	if (!dst) {
		err = -ENODEV;
		goto err_no_dst;
	}

	if (!try_module_get(dst->ops->owner)) {
		err = -ENODEV;
		goto err_module_get;
	}

	err = visdn_open(dst);

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
	dst = visdn_cxc_get_by_src(chan->cxc, chan);
	if (!dst) {
		err = -ENODEV;
		goto err_no_dst;
	}

	if (!try_module_get(dst->ops->owner)) {
		err = -ENODEV;
		goto err_module_get;
	}

	err = visdn_close(dst);

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
	dst = visdn_cxc_get_by_src(chan->cxc, chan);
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
	dst = visdn_cxc_get_by_src(chan->cxc, chan);
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
	dst = visdn_cxc_get_by_src(chan->cxc, chan);
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
	dst = visdn_cxc_get_by_src(chan->cxc, chan);
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
	dst = visdn_cxc_get_by_src(chan->cxc, chan);
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
	dst = visdn_cxc_get_by_src(chan->cxc, chan);
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
	dst = visdn_cxc_get_by_src(chan->cxc, chan);
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
	dst = visdn_cxc_get_by_src(chan->cxc, chan);
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
	struct visdn_chan *chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
			visdn_chan_refcount(chan));
}

static VISDN_CHAN_ATTR(refcnt, S_IRUGO,
		visdn_chan_show_refcnt,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_port_name(
        struct visdn_chan *chan,
	struct visdn_chan_attribute *attr,
        char *buf)
{
	// FIXME: Lock chan and port!?

	return snprintf(buf, PAGE_SIZE, "%s\n",
		chan->port->name);
}

static VISDN_CHAN_ATTR(port_name, S_IRUGO,
		visdn_chan_show_port_name,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_mtu(
	struct visdn_chan *chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	if (visdn_chan_lock_interruptible(chan))
		return -ERESTARTSYS;

	int len = snprintf(buf, PAGE_SIZE, "%d\n", chan->pars.mtu);

	visdn_chan_unlock(chan);

	return len;
}

static VISDN_CHAN_ATTR(mtu, S_IRUGO,
		visdn_chan_show_mtu,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_bitrate(
	struct visdn_chan *chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	if (visdn_chan_lock_interruptible(chan))
		return -ERESTARTSYS;

	int len = snprintf(buf, PAGE_SIZE, "%d\n", chan->pars.bitrate);

	visdn_chan_unlock(chan);

	return len;
}

static VISDN_CHAN_ATTR(bitrate, S_IRUGO,
		visdn_chan_show_bitrate,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_framing(
	struct visdn_chan *chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
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

static VISDN_CHAN_ATTR(framing, S_IRUGO,
		visdn_chan_show_framing,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_bitorder(
	struct visdn_chan *chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	if (visdn_chan_lock_interruptible(chan))
		return -ERESTARTSYS;

	int len = snprintf(buf, PAGE_SIZE, "%s\n",
			chan->pars.bitorder == VISDN_CHAN_BITORDER_LSB ?
				"lsb" : "msb");

	visdn_chan_unlock(chan);

	return len;
}

static VISDN_CHAN_ATTR(bitorder, S_IRUGO,
		visdn_chan_show_bitorder,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_autoopen(
	struct visdn_chan *chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	if (visdn_chan_lock_interruptible(chan))
		return -ERESTARTSYS;

	int len = snprintf(buf, PAGE_SIZE, "%d\n",
			chan->autoopen ? 1 : 0);

	visdn_chan_unlock(chan);

	return len;
}

static VISDN_CHAN_ATTR(autoopen, S_IRUGO,
		visdn_chan_show_autoopen,
		NULL);

//----------------------------------------------------------------------------

static struct attribute *visdn_chan_default_attrs[] =
{
	&visdn_chan_attr_refcnt.attr,
	&visdn_chan_attr_port_name.attr,
	&visdn_chan_attr_mtu.attr,
	&visdn_chan_attr_bitrate.attr,
	&visdn_chan_attr_framing.attr,
	&visdn_chan_attr_bitorder.attr,
	&visdn_chan_attr_autoopen.attr,
	NULL,
};

#define VISDN_CHAN_HASHBITS 8

struct hlist_head visdn_chan_index_hash[1 << VISDN_CHAN_HASHBITS];

static inline struct hlist_head *visdn_chan_index_get_hash(int index)
{
	return &visdn_chan_index_hash[index & ((1 << VISDN_CHAN_HASHBITS) - 1)];
}

struct visdn_chan *__visdn_chan_get_by_index(int index)
{
	struct hlist_node *t;
	struct visdn_chan *visdn_chan;

	hlist_for_each_entry(visdn_chan, t, visdn_chan_index_get_hash(index),
			index_node) {
		if (visdn_chan->index == index)
			return visdn_chan;
	}

	return NULL;
}

static int visdn_chan_new_index(void)
{
	static int index;
	for (;;) {
		if (++index <= 0)  // FIXME
			index = 1;
		if (!__visdn_chan_get_by_index(index))
			return index;
	}
}

void visdn_chan_init(struct visdn_chan *chan)
{
	BUG_ON(!chan);

	memset(chan, 0x00, sizeof(*chan));

	kobj_set_kset_s(chan, visdn_channels_subsys);
	kobject_init(&chan->kobj);
	INIT_LIST_HEAD(&chan->cxc_channels_node);
	INIT_HLIST_NODE(&chan->index_node);

	init_MUTEX(&chan->sem);
}
EXPORT_SYMBOL(visdn_chan_init);

int visdn_chan_register(struct visdn_chan *chan)
{
	int err;

	BUG_ON(!chan);

	BUG_ON(!chan->port);
	BUG_ON(!chan->ops);
	BUG_ON(!chan->ops->owner);
	BUG_ON(!chan->cxc);

	if (!visdn_chan_get(chan)) {
		err  = -EINVAL;
		goto err_chan_get;
	}

	if (!visdn_port_get(chan->port)) {
		err = -EINVAL;
		goto err_port_get;
	}

	chan->index = visdn_chan_new_index();

	snprintf(chan->cxc_id, sizeof(chan->cxc_id),
		"%06d", chan->index);

	kobject_set_name(&chan->kobj, "%s", chan->cxc_id);
	chan->kobj.parent = &chan->port->kobj;

	err = kobject_add(&chan->kobj);
	if (err < 0)
		goto err_kobject_add;

	err = visdn_cxc_add(chan->cxc, chan);
	if (err < 0)
		goto err_cxc_add;

	err = sysfs_create_link(
		&visdn_channels_subsys.kset.kobj,
		&chan->kobj,
		chan->cxc_id);
	if (err < 0)
		goto err_create_link_cxc_id;

	err = sysfs_create_link(
		&chan->port->kobj,
		&chan->kobj,
		chan->name);
	if (err < 0)
		goto err_create_link_chan_name;

        visdn_chan_put(chan);

	return 0;

	sysfs_remove_link(&chan->port->kobj, chan->name);
err_create_link_chan_name:
	sysfs_remove_link(&visdn_channels_subsys.kset.kobj, chan->cxc_id);
err_create_link_cxc_id:
	visdn_cxc_del(chan->cxc, chan);
err_cxc_add:
	kobject_del(&chan->kobj);
err_kobject_add:
	visdn_port_put(chan->port);
err_port_get:
	visdn_chan_put(chan);
err_chan_get:

	return err;
}
EXPORT_SYMBOL(visdn_chan_register);

void visdn_chan_unregister(
	struct visdn_chan *chan)
{
	visdn_debug(3, "visdn_chan_unregister(%s) called\n",
		chan->cxc_id);

	visdn_disconnect(chan);

	sysfs_remove_link(&chan->port->kobj, chan->name);
	sysfs_remove_link(&visdn_channels_subsys.kset.kobj, chan->cxc_id);

	visdn_cxc_del(chan->cxc, chan);
	kobject_del(&chan->kobj);
	visdn_port_put(chan->port);

	/* RCU (and other?) may still have references to this object.
	 * Sleep until everyone has put his reference back.
	 * Maybe there's a better way to handle this.
	 */

	if (visdn_chan_refcount(chan) > 1) {

		/* Usually 50ms are enough */
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout((50 * HZ) / 1000);

		while(visdn_chan_refcount(chan) > 1) {
			visdn_msg(KERN_WARNING, "Waiting for %s refcnt to"
					" become 1 (now %d)\n",
				chan->cxc_id,
				visdn_chan_refcount(chan));

			dump_stack();

			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1 * HZ);
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

	if (test_and_set_bit(VISDN_CHAN_STATE_OPEN, &chan->state)) {
		WARN_ON(1);
		return -EBUSY;
	}

	if (chan->ops->open) {
		err = chan->ops->open(chan);
		if (err < 0)
			return err;
	}

	return err;
}
EXPORT_SYMBOL(visdn_open);

int visdn_close(struct visdn_chan *chan)
{
	if (test_and_clear_bit(VISDN_CHAN_STATE_OPEN, &chan->state)) {
		if (chan->ops->close) {
			int err = 0;
			err = chan->ops->close(chan);
			if (err < 0)
				return err;
		}
	}

	return 0;
}
EXPORT_SYMBOL(visdn_close);

#define to_visdn_chan_attr(_attr) \
	container_of(_attr, struct visdn_chan_attribute, attr)


static ssize_t visdn_chan_attr_show(
	struct kobject *kobj,
	struct attribute *attr,
	char *buf)
{
	struct visdn_chan_attribute *visdn_chan_attr =
					to_visdn_chan_attr(attr);
	struct visdn_chan *visdn_chan = to_visdn_chan(kobj);
	ssize_t err;

	if (visdn_chan_attr->show)
		err = visdn_chan_attr->show(visdn_chan, visdn_chan_attr, buf);
	else
		err = -EIO;

	return err;
}

static ssize_t visdn_chan_attr_store(
	struct kobject *kobj,
	struct attribute *attr,
	const char *buf,
	size_t count)
{
	struct visdn_chan_attribute *visdn_chan_attr =
					to_visdn_chan_attr(attr);
	struct visdn_chan *visdn_chan = to_visdn_chan(kobj);
	ssize_t err;

	if (visdn_chan_attr->store)
		err = visdn_chan_attr->store(visdn_chan, visdn_chan_attr,
							buf, count);
	else
		err = -EIO;

	return err;
}

static struct sysfs_ops visdn_chan_sysfs_ops = {
	.show   = visdn_chan_attr_show,
	.store  = visdn_chan_attr_store,
};

static void visdn_chan_release(struct kobject *kobj)
{
	visdn_debug(3, "visdn_chan_release()\n");

	struct visdn_chan *chan = to_visdn_chan(kobj);

	if (chan->ops->release)
		chan->ops->release(chan);
	else {
		visdn_msg(KERN_ERR, "vISDN channel '%s' does not have a"
			" release() function, it is broken and must be"
			" fixed.\n",
			chan->cxc_id);
		WARN_ON(1);
	}
}

struct kobj_type ktype_visdn_chan = {
	.release	= visdn_chan_release,
	.sysfs_ops	= &visdn_chan_sysfs_ops,
	.default_attrs	= visdn_chan_default_attrs,
};

decl_subsys(visdn_channels, &ktype_visdn_chan, NULL);

int visdn_chan_create_file(
	struct visdn_chan *chan,
	struct visdn_chan_attribute *attr)
{
	int err = 0;

	if (visdn_chan_get(chan)) {
		err = sysfs_create_file(&chan->kobj, &attr->attr);
		visdn_chan_put(chan);
	}

	return err;
}
EXPORT_SYMBOL(visdn_chan_create_file);

void visdn_chan_remove_file(
	struct visdn_chan *chan,
	struct visdn_chan_attribute *attr)
{
	if (visdn_chan_get(chan)) {
		sysfs_remove_file(&chan->kobj, &attr->attr);
		visdn_chan_put(chan);
	}
}
EXPORT_SYMBOL(visdn_chan_remove_file);

int visdn_chan_modinit(void)
{
	int err;

	err = subsystem_register(&visdn_channels_subsys);
	if (err < 0)
		goto err_subsystem_register;

	return 0;

	subsystem_unregister(&visdn_channels_subsys);
err_subsystem_register:

	return err;
/*
	err = bus_create_file(&visdn_bus_type, &bus_attr_connect);
	if (err < 0)
		goto err_bus_create_file;*/
//	bus_remove_file(&visdn_bus_type, &bus_attr_connect);
//err_bus_create_file:
}

void visdn_chan_modexit(void)
{
	subsystem_unregister(&visdn_channels_subsys);
}
