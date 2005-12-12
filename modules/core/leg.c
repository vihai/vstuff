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
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>

#include <kernel_config.h>

#include "visdn_mod.h"
#include "chan.h"
#include "leg.h"
#include "port.h"
#include "cxc.h"

int visdn_leg_frame_xmit(
	struct visdn_leg *leg,
	struct sk_buff *skb)
{
	if (!leg->cxc->ops || !leg->cxc->ops->frame_xmit) {
		WARN_ON(1);
		return -ENODEV;
	}

	return leg->cxc->ops->frame_xmit(leg->cxc, leg, skb);
}
EXPORT_SYMBOL(visdn_leg_frame_xmit);

void visdn_leg_start_queue(
	struct visdn_leg *leg)
{
	if (!leg->cxc->ops || !leg->cxc->ops->start_queue) {
		WARN_ON(1);
		return;
	}

	leg->cxc->ops->start_queue(leg->cxc, leg);
}
EXPORT_SYMBOL(visdn_leg_start_queue);

void visdn_leg_stop_queue(
	struct visdn_leg *leg)
{
	if (!leg->cxc->ops || !leg->cxc->ops->stop_queue) {
		WARN_ON(1);
		return;
	}

	leg->cxc->ops->stop_queue(leg->cxc, leg);
}
EXPORT_SYMBOL(visdn_leg_stop_queue);

void visdn_leg_wake_queue(
	struct visdn_leg *leg)
{
	if (!leg->cxc->ops || !leg->cxc->ops->wake_queue) {
		WARN_ON(1);
		return;
	}

	leg->cxc->ops->wake_queue(leg->cxc, leg);
}
EXPORT_SYMBOL(visdn_leg_wake_queue);

void visdn_leg_rx_error(
	struct visdn_leg *leg,
	enum visdn_leg_rx_error_code code)
{
	if (!leg->cxc->ops || !leg->cxc->ops->rx_error) {
		WARN_ON(1);
		return;
	}

	leg->cxc->ops->rx_error(leg->cxc, leg, code);
}
EXPORT_SYMBOL(visdn_leg_rx_error);

void visdn_leg_tx_error(
	struct visdn_leg *leg,
	enum visdn_leg_tx_error_code code)
{
	if (!leg->cxc->ops || !leg->cxc->ops->tx_error) {
		WARN_ON(1);
		return;
	}

	leg->cxc->ops->tx_error(leg->cxc, leg, code);
}
EXPORT_SYMBOL(visdn_leg_tx_error);

static const char *visdn_framing_to_string(int framing)
{
	switch (framing) {
	case VISDN_LEG_FRAMING_NONE:
		return "none";
	break;

	case VISDN_LEG_FRAMING_HDLC:
		return "hdlc";
	break;

	case VISDN_LEG_FRAMING_MTP2:
		return "mtp2";
	break;

	default:
		WARN_ON(1);
		return "*UNKNOWN*";
	}

}

//----------------------------------------------------------------------------

static ssize_t visdn_leg_show_framing(
	struct visdn_leg *leg,
	struct visdn_leg_attribute *attr,
	char *buf)
{
	int len;

	if (visdn_chan_lock_interruptible(leg->chan))
		return -ERESTARTSYS;

	len = snprintf(buf, PAGE_SIZE, "%s\n",
			visdn_framing_to_string(leg->framing));

	visdn_chan_unlock(leg->chan);

	return len;
}

static ssize_t visdn_leg_store_framing(
	struct visdn_leg *leg,
	struct visdn_leg_attribute *attr,
	const char *buf,
	size_t count)
{
	int err;
	int len = count;

	while(len > 0) {
		if (buf[len - 1] != '\r' && buf[len - 1] != '\n')
			break;

		len--;
	}

	if (visdn_chan_lock_interruptible(leg->chan))
		return -ERESTARTSYS;

	if (!strncmp(buf, "none", len) &&
			leg->framing_avail & VISDN_LEG_FRAMING_NONE)
		leg->framing = VISDN_LEG_FRAMING_NONE;
	else if (!strncmp(buf, "async", len) &&
			leg->framing_avail & VISDN_LEG_FRAMING_ASYNC)
		leg->framing = VISDN_LEG_FRAMING_ASYNC;
	else if (!strncmp(buf, "hdlc", len) &&
			leg->framing_avail & VISDN_LEG_FRAMING_HDLC)
		leg->framing = VISDN_LEG_FRAMING_HDLC;
	else if (!strncmp(buf, "mtp2", len) &&
			leg->framing_avail & VISDN_LEG_FRAMING_MTP2)
		leg->framing = VISDN_LEG_FRAMING_HDLC;
	else {
		err = -EINVAL;
		goto err_invalid_framing;
	}
		
	visdn_chan_unlock(leg->chan);

	return count;

err_invalid_framing:

	visdn_chan_unlock(leg->chan);

	return err;
}

static VISDN_LEG_ATTR(framing, S_IRUGO | S_IWUSR,
		visdn_leg_show_framing,
		visdn_leg_store_framing);

//----------------------------------------------------------------------------

static ssize_t visdn_leg_show_framing_avail(
	struct visdn_leg *leg,
	struct visdn_leg_attribute *attr,
	char *buf)
{
	int len = 0;

	if (visdn_chan_lock_interruptible(leg->chan))
		return -ERESTARTSYS;

	if (leg->framing_avail & VISDN_LEG_FRAMING_NONE)
		len += snprintf(buf + len, PAGE_SIZE - len, "none\n");

	if (leg->framing_avail & VISDN_LEG_FRAMING_ASYNC)
		len += snprintf(buf + len, PAGE_SIZE - len, "async\n");

	if (leg->framing_avail & VISDN_LEG_FRAMING_HDLC)
		len += snprintf(buf + len, PAGE_SIZE - len, "hdlc\n");

	if (leg->framing_avail & VISDN_LEG_FRAMING_MTP2)
		len += snprintf(buf + len, PAGE_SIZE - len, "mtp2\n");

	visdn_chan_unlock(leg->chan);

	return len;
}

static VISDN_LEG_ATTR(framing_avail, S_IRUGO,
		visdn_leg_show_framing_avail,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_leg_show_mtu(
	struct visdn_leg *leg,
	struct visdn_leg_attribute *attr,
	char *buf)
{
	int len;

	if (visdn_chan_lock_interruptible(leg->chan))
		return -ERESTARTSYS;

	len = snprintf(buf, PAGE_SIZE, "%d\n", leg->mtu);

	visdn_chan_unlock(leg->chan);

	return len;
}

static VISDN_LEG_ATTR(mtu, S_IRUGO,
		visdn_leg_show_mtu,
		NULL);

//----------------------------------------------------------------------------

static struct attribute *visdn_leg_default_attrs[] =
{
	&visdn_leg_attr_framing.attr,
	&visdn_leg_attr_framing_avail.attr,
	&visdn_leg_attr_mtu.attr,
	NULL,
};

#define to_visdn_leg_attr(_attr) \
	container_of(_attr, struct visdn_leg_attribute, attr)

static ssize_t visdn_leg_attr_show(
	struct kobject *kobj,
	struct attribute *attr,
	char *buf)
{
	struct visdn_leg_attribute *visdn_leg_attr =
					to_visdn_leg_attr(attr);
	struct visdn_leg *leg = to_visdn_leg(kobj);
	ssize_t err;

	if (visdn_leg_attr->show)
		err = visdn_leg_attr->show(leg, visdn_leg_attr, buf);
	else
		err = -EIO;

	return err;
}

static ssize_t visdn_leg_attr_store(
	struct kobject *kobj,
	struct attribute *attr,
	const char *buf,
	size_t count)
{
	struct visdn_leg_attribute *visdn_leg_attr =
					to_visdn_leg_attr(attr);
	struct visdn_leg *leg = to_visdn_leg(kobj);
	ssize_t err;

	if (visdn_leg_attr->store)
		err = visdn_leg_attr->store(leg, visdn_leg_attr,
					buf, count);
	else
		err = -EIO;

	return err;
}

static struct sysfs_ops visdn_leg_sysfs_ops = {
	.show   = visdn_leg_attr_show,
	.store  = visdn_leg_attr_store,
};

static void visdn_leg_release(struct kobject *kobj)
{
}

struct kobj_type ktype_visdn_leg = {
	.release	= visdn_leg_release,
	.sysfs_ops	= &visdn_leg_sysfs_ops,
	.default_attrs	= visdn_leg_default_attrs,
};

void visdn_leg_init(struct visdn_leg *leg)
{
	BUG_ON(!leg);

	memset(leg, 0, sizeof(*leg));

	kobject_init(&leg->kobj);
	leg->kobj.ktype = &ktype_visdn_leg;

	INIT_LIST_HEAD(&leg->cxc_legs_node);
}
EXPORT_SYMBOL(visdn_leg_init);

int visdn_leg_create_file(
	struct visdn_leg *leg,
	struct visdn_leg_attribute *attr)
{
	return sysfs_create_file(&leg->kobj, &attr->attr);
		visdn_leg_put(leg);
}
EXPORT_SYMBOL(visdn_leg_create_file);

void visdn_leg_remove_file(
	struct visdn_leg *leg,
	struct visdn_leg_attribute *attr)
{
	sysfs_remove_file(&leg->kobj, &attr->attr);
}
EXPORT_SYMBOL(visdn_leg_remove_file);

struct visdn_leg *visdn_leg_get(
	struct visdn_leg *leg)
{
	return visdn_chan_get(leg->chan) ? leg : NULL;
}
EXPORT_SYMBOL(visdn_leg_get);

void visdn_leg_put(
	struct visdn_leg *leg)
{
	visdn_chan_put(leg->chan);
}
EXPORT_SYMBOL(visdn_leg_put);

