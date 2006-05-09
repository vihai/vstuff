/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
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

#include "core.h"
#include "visdn_mod.h"
#include "chan.h"
#include "port.h"
#include "cxc.h"
#include "router.h"
#include "pipeline.h"

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_name(
	struct visdn_chan *chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n",
			chan->name);
}

static VISDN_CHAN_ATTR(name, S_IRUGO,
		visdn_chan_show_name,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_open(
	struct visdn_chan *chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
		test_bit(VISDN_CHAN_STATE_OPEN, &chan->state));
}

static VISDN_CHAN_ATTR(open, S_IRUGO,
		visdn_chan_show_open,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_playing(
	struct visdn_chan *chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
		test_bit(VISDN_CHAN_STATE_PLAYING, &chan->state));
}

static VISDN_CHAN_ATTR(playing, S_IRUGO,
		visdn_chan_show_playing,
		NULL);

//----------------------------------------------------------------------------

static ssize_t visdn_chan_show_bitrate(
	struct visdn_chan *chan,
	struct visdn_chan_attribute *attr,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", chan->bitrate);
}

static VISDN_CHAN_ATTR(bitrate, S_IRUGO,
		visdn_chan_show_bitrate,
		NULL);

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

static struct attribute *visdn_chan_default_attrs[] =
{
	&visdn_chan_attr_name.attr,
	&visdn_chan_attr_open.attr,
	&visdn_chan_attr_playing.attr,
	&visdn_chan_attr_bitrate.attr,
	&visdn_chan_attr_refcnt.attr,
	NULL,
};

#define VISDN_CHAN_HASHBITS 8

static struct hlist_head visdn_chan_id_hash[1 << VISDN_CHAN_HASHBITS];
static spinlock_t visdn_chan_id_hash_lock = SPIN_LOCK_UNLOCKED;

static inline struct hlist_head *visdn_chan_hash_id(int id)
{
	return &visdn_chan_id_hash[id & ((1 << VISDN_CHAN_HASHBITS) - 1)];
}

struct visdn_chan *_visdn_chan_search_by_id(int id)
{
	struct hlist_node *t;
	struct visdn_chan *chan;

	hlist_for_each_entry(chan, t, visdn_chan_hash_id(id), node) {
		if (chan->id == id)
			return chan;
	}

	return NULL;
}

struct visdn_chan *visdn_chan_get_by_id(int id)
{
	struct visdn_chan *chan;

	spin_lock(&visdn_chan_id_hash_lock);

	chan = visdn_chan_get(_visdn_chan_search_by_id(id));

	spin_unlock(&visdn_chan_id_hash_lock);

	return chan;
}

static int _visdn_chan_new_id(void)
{
	static int cur_id;

	for (;;) {
		/* Maybe reusing channel ids would be better */

		if (++cur_id <= 0)
			cur_id = 1;

		if (!_visdn_chan_search_by_id(cur_id))
			return cur_id;
	}
}

void visdn_chan_init(struct visdn_chan *chan)
{
	BUG_ON(!chan);

	memset(chan, 0, sizeof(*chan));

	kobj_set_kset_s(chan, visdn_channels_subsys);
	kobject_init(&chan->kobj);

	visdn_leg_init(&chan->leg_a);
	chan->leg_a.id = 0;
	chan->leg_a.chan = chan;
	chan->leg_a.other_leg = &chan->leg_b;

	visdn_leg_init(&chan->leg_b);
	chan->leg_b.id = 1;
	chan->leg_b.chan = chan;
	chan->leg_b.other_leg = &chan->leg_a;

	INIT_HLIST_NODE(&chan->node);

	init_MUTEX(&chan->sem);
}
EXPORT_SYMBOL(visdn_chan_init);

int visdn_chan_register(struct visdn_chan *chan)
{
	char chanid_str[16];
	int err;

	BUG_ON(!chan);

	BUG_ON(!chan->port);
	BUG_ON(!chan->ops);
	BUG_ON(!chan->ops->owner);

	if (!visdn_port_get(chan->port)) {
		err = -EINVAL;
		goto err_port_get;
	}

	spin_lock(&visdn_chan_id_hash_lock);
	chan->id = _visdn_chan_new_id();
	visdn_chan_get(chan);
	hlist_add_head(&chan->node, visdn_chan_hash_id(chan->id));
	spin_unlock(&visdn_chan_id_hash_lock);

	kobject_set_name(&chan->kobj, "%06d", chan->id);
	chan->kobj.parent = &chan->port->kobj;

	err = kobject_add(&chan->kobj);
	if (err < 0)
		goto err_kobject_add;

	BUG_ON(chan->leg_b.cxc && !chan->leg_a.cxc);

	/*----------- LEG A -------*/

	kobject_init(&chan->leg_a.kobj);
	kobject_set_name(&chan->leg_a.kobj, "leg_a");
	chan->leg_a.kobj.parent = &chan->kobj;

	err = kobject_add(&chan->leg_a.kobj);
	if (err < 0)
		goto err_kobject_add_leg_a;

	if (chan->leg_a.cxc) {
		err = visdn_cxc_add(chan->leg_a.cxc, &chan->leg_a);
		if (err < 0)
			goto err_cxc_add_a;

		chan->leg_a.router_arch.src_node =
			&chan->leg_a.cxc->router_node;
		chan->leg_b.router_arch.dst_node =
			&chan->leg_a.cxc->router_node;
	} else {
		chan->leg_a.router_arch.src_node = &chan->router_node;
		chan->leg_b.router_arch.dst_node = &chan->router_node;
	}

	chan->leg_a.router_arch.cost = 100;
	chan->leg_a.router_arch.src_leg = &chan->leg_a;
	visdn_router_add_arch(&chan->leg_a.router_arch);

	/*----------- LEG B -------*/

	kobject_init(&chan->leg_b.kobj);
	kobject_set_name(&chan->leg_b.kobj, "leg_b");
	chan->leg_b.kobj.parent = &chan->kobj;

	err = kobject_add(&chan->leg_b.kobj);
	if (err < 0)
		goto err_kobject_add_leg_b;

	if (chan->leg_b.cxc) {
		err = visdn_cxc_add(chan->leg_b.cxc, &chan->leg_b);
		if (err < 0)
			goto err_cxc_add_b;

		chan->leg_a.router_arch.dst_node =
			&chan->leg_b.cxc->router_node;
		chan->leg_b.router_arch.src_node =
			&chan->leg_b.cxc->router_node;
	} else {
		chan->leg_b.router_arch.src_node = &chan->router_node;
		chan->leg_a.router_arch.dst_node = &chan->router_node;
	}

	chan->leg_b.router_arch.cost = 100;
	chan->leg_b.router_arch.src_leg = &chan->leg_b;
	visdn_router_add_arch(&chan->leg_b.router_arch);

	/*-------------------------*/

	chan->router_node.is_channel = TRUE;
	visdn_router_add_node(&chan->router_node);

	err = sysfs_create_link(
		&chan->leg_a.kobj,
		&chan->leg_b.kobj,
		"other_leg");
	if (err < 0)
		goto err_create_link_other_leg_a_b;

	err = sysfs_create_link(
		&chan->leg_b.kobj,
		&chan->leg_a.kobj,
		"other_leg");
	if (err < 0)
		goto err_create_link_other_leg_b_a;

	snprintf(chanid_str, sizeof(chanid_str), "%06d", chan->id);

	err = sysfs_create_link(
		&visdn_channels_subsys.kset.kobj,
		&chan->kobj,
		chanid_str);
	if (err < 0)
		goto err_create_link_cxc_id;

	if (!strlen(chan->name))
		snprintf(chan->name, sizeof(chan->name), "%06d", chan->id);

	if (strcmp(chan->name, chan->kobj.name)) {
		err = sysfs_create_link(
			&chan->port->kobj,
			&chan->kobj,
			chan->name);
		if (err < 0)
			goto err_create_link_name;
	}

	visdn_call_notifiers(VISDN_EVENT_CHAN_REGISTERED, chan);

	return 0;

	if (strcmp(chan->name, chan->kobj.name))
		sysfs_remove_link(&chan->port->kobj, chan->name);
err_create_link_name:
	sysfs_remove_link(&visdn_channels_subsys.kset.kobj, chanid_str);
err_create_link_cxc_id:
	sysfs_remove_link(&chan->leg_b.kobj, "other_leg");
err_create_link_other_leg_b_a:
	sysfs_remove_link(&chan->leg_a.kobj, "other_leg");
err_create_link_other_leg_a_b:
	visdn_router_del_node(&chan->router_node);

	visdn_router_del_arch(&chan->leg_b.router_arch);

	if (chan->leg_b.cxc)
		visdn_cxc_del(chan->leg_b.cxc, &chan->leg_b);
err_cxc_add_b:
	kobject_del(&chan->leg_b.kobj);
err_kobject_add_leg_b:
	visdn_router_del_arch(&chan->leg_a.router_arch);

	if (chan->leg_a.cxc)
		visdn_cxc_del(chan->leg_a.cxc, &chan->leg_a);
err_cxc_add_a:
	kobject_del(&chan->leg_a.kobj);
err_kobject_add_leg_a:
	kobject_del(&chan->kobj);
err_kobject_add:
	visdn_port_put(chan->port);
err_port_get:
	spin_lock(&visdn_chan_id_hash_lock);
	hlist_del(&chan->node);
	visdn_chan_put(chan);
	spin_unlock(&visdn_chan_id_hash_lock);

	return err;
}
EXPORT_SYMBOL(visdn_chan_register);

void visdn_chan_unregister(
	struct visdn_chan *chan)
{
	visdn_debug(3, "visdn_chan_unregister(%06d) called\n",
		chan->id);

	visdn_call_notifiers(VISDN_EVENT_CHAN_UNREGISTERED, chan);

	if (strcmp(chan->name, chan->kobj.name))
		sysfs_remove_link(&chan->port->kobj, chan->name);

	{
	char chanid_str[16];
	snprintf(chanid_str, sizeof(chanid_str), "%06d", chan->id);

	sysfs_remove_link(&visdn_channels_subsys.kset.kobj, chanid_str);
	}

	sysfs_remove_link(
		&chan->leg_b.kobj,
		"other_leg");

	sysfs_remove_link(
		&chan->leg_a.kobj,
		"other_leg");

	BUG_ON(!chan->leg_a.cxc);

	{
	struct visdn_cxc_connection *conn;
	struct hlist_node *pos;
	struct visdn_pipeline *pipeline = NULL;

	down(&chan->leg_a.cxc->sem);

	hlist_for_each_entry(conn, pos,
			visdn_cxc_get_hash(chan->leg_a.cxc,
					&chan->leg_a),
			hash_node) {

		if (conn->src == &chan->leg_a) {
			pipeline = visdn_pipeline_get(conn->pipeline);
			break;
		}

	}

	up(&chan->leg_a.cxc->sem);

	if (pipeline) {
		visdn_pipeline_disconnect(pipeline);
		visdn_pipeline_put(pipeline);
	}
	}

	visdn_router_del_node(&chan->router_node);

	visdn_router_del_arch(&chan->leg_b.router_arch);

	if (chan->leg_b.cxc)
		visdn_cxc_del(chan->leg_b.cxc, &chan->leg_b);

	kobject_del(&chan->leg_b.kobj);
	kobject_put(&chan->leg_b.kobj);

	visdn_router_del_arch(&chan->leg_a.router_arch);

	visdn_cxc_del(chan->leg_a.cxc, &chan->leg_a);

	kobject_del(&chan->leg_a.kobj);
	kobject_put(&chan->leg_a.kobj);

	kobject_del(&chan->kobj);

	visdn_port_put(chan->port);

	/* RCU (and other?) may still have references to this object.
	 * Sleep until everyone has put his reference back.
	 * Maybe there's a better way to handle this.
	 */

	spin_lock(&visdn_chan_id_hash_lock);
	hlist_del(&chan->node);
	visdn_chan_put(chan);
	spin_unlock(&visdn_chan_id_hash_lock);

	if (visdn_chan_refcount(chan) > 1) {

		/* Usually 50ms are enough */
		msleep(50);

		while(visdn_chan_refcount(chan) > 1) {
			visdn_msg(KERN_WARNING, "Waiting for %06d refcnt to"
					" become 1 (now %d)\n",
				chan->id,
				visdn_chan_refcount(chan));

			dump_stack();

			msleep(5000);
		}
	}
}
EXPORT_SYMBOL(visdn_chan_unregister);

int visdn_chan_open(struct visdn_chan *chan)
{
	visdn_debug(1, "visnd_chan_open(%06d)\n", chan->id);

	if (!test_and_set_bit(VISDN_CHAN_STATE_OPEN, &chan->state) &&
	    chan->ops->open) {
		int err;

		err = chan->ops->open(chan);
		if (err < 0) {
			clear_bit(VISDN_CHAN_STATE_OPEN, &chan->state);
			visdn_msg(KERN_WARNING,
				"visnd_chan_open(%06d) FAILED: %d\n",
				chan->id, err);
			return err;
		}

		visdn_call_notifiers(VISDN_EVENT_CHAN_OPENED, chan);
	}

	return 0;
}

int visdn_chan_stop(struct visdn_chan *chan)
{
	visdn_debug(1, "visnd_chan_stop(%06d)\n", chan->id);

	if (test_and_clear_bit(VISDN_CHAN_STATE_PLAYING, &chan->state) &&
	    chan->ops->stop) {
		int err = 0;
		err = chan->ops->stop(chan);
		if (err < 0)
			return err;

		visdn_call_notifiers(VISDN_EVENT_CHAN_STOPPED, chan);
	}

	return 0;
}

int visdn_chan_start(struct visdn_chan *chan)
{
	visdn_debug(1, "visnd_chan_start(%06d)\n", chan->id);

	if (!test_bit(VISDN_CHAN_STATE_OPEN, &chan->state)) {
		WARN_ON(1);
		return -EINVAL;
	}

	if (!test_and_set_bit(VISDN_CHAN_STATE_PLAYING, &chan->state) &&
	    chan->ops->start) {
		int err;

		err = chan->ops->start(chan);
		if (err < 0) {
			clear_bit(VISDN_CHAN_STATE_PLAYING, &chan->state);
			visdn_msg(KERN_WARNING,
				"visnd_chan_start(%06d) FAILED: %d\n",
				chan->id, err);
			return err;
		}

		visdn_call_notifiers(VISDN_EVENT_CHAN_STARTED, chan);
	}

	return 0;
}

int visdn_chan_close(struct visdn_chan *chan)
{
	visdn_debug(1, "visnd_chan_close(%06d)\n", chan->id);

	if (test_and_clear_bit(VISDN_CHAN_STATE_OPEN, &chan->state) &&
	    chan->ops->close) {
		int err = 0;
		err = chan->ops->close(chan);
		if (err < 0)
			return err;

		visdn_call_notifiers(VISDN_EVENT_CHAN_CLOSED, chan);
	}

	return 0;
}

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
	struct visdn_chan *chan = to_visdn_chan(kobj);

	visdn_debug(3, "visdn_chan_release()\n");

	if (chan->ops->release)
		chan->ops->release(chan);
	else {
		visdn_msg(KERN_ERR, "vISDN channel '%06d' does not have a"
			" release() function, it is broken and must be"
			" fixed.\n",
			chan->id);
		WARN_ON(1);
	}
}

struct kobj_type ktype_visdn_chan = {
	.release	= visdn_chan_release,
	.sysfs_ops	= &visdn_chan_sysfs_ops,
	.default_attrs	= visdn_chan_default_attrs,
};

decl_subsys_name(visdn_channels, channels, &ktype_visdn_chan, NULL);

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

	visdn_channels_subsys.kset.kobj.parent = &visdn_subsys.kset.kobj;

	err = subsystem_register(&visdn_channels_subsys);
	if (err < 0)
		goto err_subsystem_register;

	return 0;

	subsystem_unregister(&visdn_channels_subsys);
err_subsystem_register:

	return err;
}

void visdn_chan_modexit(void)
{
	int i;
	for (i=0; i<ARRAY_SIZE(visdn_chan_id_hash); i++)
		WARN_ON(!hlist_empty(&visdn_chan_id_hash[i]));

	subsystem_unregister(&visdn_channels_subsys);
}
