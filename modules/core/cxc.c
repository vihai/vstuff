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
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#include "core.h"
#include "chan.h"
#include "cxc.h"
#include "router.h"
#include "visdn_mod.h"

static struct cdev visdn_cxc_cdev;
struct class_device visdn_cxc_control_class_dev;

struct list_head visdn_cxc_list = LIST_HEAD_INIT(visdn_cxc_list);
DECLARE_MUTEX(visdn_cxc_list_sem);

static void visdn_cxc_delete_rcu(struct rcu_head *head)
{
	struct visdn_cxc_connection *conn =
		container_of(head, struct visdn_cxc_connection, rcu);

	visdn_leg_put(conn->src);
	visdn_leg_put(conn->dst);

	kfree(conn);
}

static void _visdn_cxc_disconnect_simplex(
	struct visdn_cxc_connection *conn);

static int visdn_cxc_connect_simplex(
	struct visdn_cxc *cxc,
	struct visdn_leg *src,
	struct visdn_leg *dst,
	struct file *file,
	unsigned long flags)
{
	int err;
	struct visdn_cxc_connection *conn;

	visdn_debug(2,
		"Simplex connect '%06d' with '%06d' through CXC '%d'\n",
		src->chan->id, dst->chan->id,
		cxc->id);

	down(&cxc->sem);

	{
	struct visdn_cxc_connection *conn, *n;
	list_for_each_entry_safe(conn, n,
			&cxc->connections_list,
			list_node) {

		if (conn->src != src &&
		    conn->dst == dst) {
			if (flags & VISDN_CONNECT_FLAG_OVERRIDE) {
				_visdn_cxc_disconnect_simplex(conn);
			} else {
				err = -EALREADY;
				goto err_already_connected;
			}	
		}
	}
	}

	up(&cxc->sem);

	conn = kmalloc(sizeof(*conn), GFP_KERNEL);
	if (!conn) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	memset(conn, 0, sizeof(*conn));

	INIT_RCU_HEAD(&conn->rcu);
	conn->cxc = cxc;
	conn->src = visdn_leg_get(src);
	conn->dst = visdn_leg_get(dst);

	if (flags & VISDN_CONNECT_FLAG_PERMANENT)
		conn->file = NULL;
	else
		conn->file = file;

	down(&cxc->sem);

	hlist_add_head_rcu(&conn->hash_node,
			visdn_cxc_get_hash(cxc, src));

	list_add_tail_rcu(&conn->list_node,
			&cxc->connections_list);

	up(&cxc->sem);

	if (src->ops && src->ops->connect) {
		err = src->ops->connect(src, dst);
		if (err < 0)
			goto err_connect;
	}

	sysfs_create_link(&src->kobj, &dst->kobj, "connected");

	return 0;

	sysfs_remove_link(&src->kobj, "connected");

	if (src->ops && src->ops->disconnect)
		src->ops->disconnect(src, dst);
err_connect:
	down(&cxc->sem);
	hlist_del_rcu(&conn->hash_node);
	list_del_rcu(&conn->list_node);
	up(&cxc->sem);

	visdn_leg_put(dst);
	visdn_leg_put(src);
err_kmalloc:
err_already_connected:

	return err;
}

static void _visdn_cxc_disconnect_simplex(
	struct visdn_cxc_connection *conn)
{
	visdn_debug(2,
		"Simplex disconnect '%06d' with '%06d' through CXC '%d'\n",
		conn->src->chan->id, conn->dst->chan->id,
		conn->cxc->id);

	visdn_chan_disable(conn->src->chan);
	visdn_chan_disable(conn->dst->chan);

	hlist_del_rcu(&conn->hash_node);
	list_del_rcu(&conn->list_node);

	if (conn->src->ops && conn->src->ops->disconnect)
		conn->src->ops->disconnect(conn->src, conn->dst);

	if (conn->cxc->ops && conn->cxc->ops->disconnect)
		conn->cxc->ops->disconnect(conn->cxc, conn->src, conn->dst);

	sysfs_remove_link(&conn->src->kobj, "connected");

	/* Use synchronize_rcu in 2.6.14+ */
	call_rcu(&conn->rcu, visdn_cxc_delete_rcu);
}

static void _visdn_cxc_disconnect_simplex_by_src_dst(
	struct visdn_cxc *cxc,
	struct visdn_leg *src,
	struct visdn_leg *dst)
{
	struct visdn_cxc_connection *conn;
	struct hlist_node *pos, *n;

	hlist_for_each_entry_safe(conn, pos, n,
			visdn_cxc_get_hash(cxc, src),
			hash_node) {

		if (conn->cxc == cxc &&
		    conn->src == src &&
		    conn->dst == dst) {

			_visdn_cxc_disconnect_simplex(conn);
		}
	}
}

static void visdn_cxc_disconnect_simplex_by_src_dst(
	struct visdn_cxc *cxc,
	struct visdn_leg *src,
	struct visdn_leg *dst)
{
	down(&cxc->sem);

	_visdn_cxc_disconnect_simplex_by_src_dst(cxc, src, dst);

	up(&cxc->sem);
}

int visdn_cxc_connect(
	struct visdn_cxc *cxc,
	struct visdn_leg *leg1,
	struct visdn_leg *leg2,
	struct file *file,
	unsigned long flags)
{
	int err;

	BUG_ON(!cxc);
	BUG_ON(!leg1);
	BUG_ON(!leg2);
	BUG_ON(leg1->cxc != cxc);
	BUG_ON(leg2->cxc != cxc);

	visdn_debug(2, "Connecting chan '%s' to chan '%s'\n",
		leg1->chan->kobj.name,
		leg2->chan->kobj.name);

	// Connect the channels -------------------------------------
	err = visdn_cxc_connect_simplex(cxc, leg1, leg2, file, flags);
	if (err < 0)
		goto err_cxc_add_leg1;

	err = visdn_cxc_connect_simplex(cxc, leg2, leg1, file, flags);
	if (err < 0)
		goto err_cxc_add_leg2;

	if (cxc->ops->connect)
		cxc->ops->connect(cxc, leg1, leg2);

	if (!(leg1->framing_avail &
	      leg2->framing_avail)) {
		printk(KERN_ERR
			"Incompatible framing capabilities"
			" (channel %06d(%c) - %08x /"
			" channel %06d(%c) - %08x)\n",
			leg1->chan->id,
			leg1->id ? 'b' : 'a',
			leg1->framing_avail,
			leg2->chan->id,
			leg2->id ? 'b' : 'a',
			leg2->framing_avail);

		return -EINVAL;
	}

	{
	int framing = 1 << (ffs(leg1->framing_avail &
				leg2->framing_avail) - 1);

	leg1->framing = framing;
	leg2->framing = framing;
	}

	return 0;

	visdn_cxc_disconnect_simplex_by_src_dst(cxc, leg2, leg1);
err_cxc_add_leg2:
	visdn_cxc_disconnect_simplex_by_src_dst(cxc, leg1, leg2);
err_cxc_add_leg1:

	return err;
}
EXPORT_SYMBOL(visdn_cxc_connect);

int visdn_cxc_disconnect(
	struct visdn_cxc *cxc,
	struct visdn_leg *leg1,
	struct visdn_leg *leg2)
{
	visdn_debug(3, "visdn_cxc_disconnect()\n");

	visdn_cxc_disconnect_simplex_by_src_dst(cxc, leg1, leg2);
	visdn_cxc_disconnect_simplex_by_src_dst(cxc, leg2, leg1);

	return 0;
}
EXPORT_SYMBOL(visdn_cxc_disconnect);

int visdn_cxc_disconnect_leg(struct visdn_leg *leg)
{
	struct visdn_cxc *cxc = leg->cxc;

	BUG_ON(!leg->cxc);

	down(&cxc->sem);

	{
	struct visdn_cxc_connection *conn;
	struct hlist_node *pos, *n;
	/* Find destination leg and disconnect it */
	hlist_for_each_entry_safe(conn, pos, n,
			visdn_cxc_get_hash(cxc, leg),
			hash_node) {

		if (conn->src == leg) {
			_visdn_cxc_disconnect_simplex(conn);
		}
	}
	}

	{
	struct visdn_cxc_connection *conn, *n;
	/* Now find legs pointing to us and disconnect them*/
	list_for_each_entry_safe(conn, n,
			&cxc->connections_list,
			list_node) {

		if (conn->dst == leg) {
			_visdn_cxc_disconnect_simplex(conn);
		}
	}
	}

	up(&cxc->sem);

	return 0;
}

int visdn_cxc_disconnect_by_file(struct file *file)
{
	struct visdn_cxc *cxc;

	down(&visdn_cxc_list_sem);

	list_for_each_entry(cxc, &visdn_cxc_list, cxc_list_node) {
		struct visdn_cxc_connection *conn, *n;

		down(&cxc->sem);

		/* Find destination leg and disconnect it */
		list_for_each_entry_safe(conn, n, &cxc->connections_list,
								list_node) {

			if (conn->file == file)
				_visdn_cxc_disconnect_simplex(conn);
		}

		up(&cxc->sem);
	}

	up(&visdn_cxc_list_sem);

	return 0;
}

int visdn_cxc_add(
	struct visdn_cxc *cxc,
	struct visdn_leg *leg)
{
	int err;

	BUG_ON(!cxc);
	BUG_ON(!leg);
	BUG_ON(leg->cxc != cxc);

	if (!visdn_cxc_get(cxc)) {
		err = -EINVAL;
		goto err_cxc_get;
	}

	down_write(&cxc->subsys.rwsem);
	list_add_tail(&leg->cxc_legs_node, &cxc->legs);
	up_write(&cxc->subsys.rwsem);

	sysfs_create_link(
		&cxc->subsys.kset.kobj,
		&leg->chan->kobj,
		leg->chan->kobj.name);

	sysfs_create_link(
		&leg->kobj,
		&cxc->subsys.kset.kobj,
		"cxc");

	return 0;

	visdn_cxc_put(cxc);
err_cxc_get:

	return err;
}

void visdn_cxc_del(
	struct visdn_cxc *cxc,
	struct visdn_leg *leg)
{
	BUG_ON(!cxc);
	BUG_ON(!leg);
	BUG_ON(leg->cxc != cxc);

	sysfs_remove_link(
		&leg->kobj,
		"cxc");

	sysfs_remove_link(
		&cxc->subsys.kset.kobj,
		leg->chan->kobj.name);

	down_write(&cxc->subsys.rwsem);
	list_del_init(&leg->cxc_legs_node);
	up_write(&cxc->subsys.rwsem);

	visdn_cxc_put(cxc);
}

struct visdn_cxc *visdn_cxc_get_by_id(
	int cxc_id)
{
	struct visdn_cxc *cxc;

	down(&visdn_cxc_list_sem);

	list_for_each_entry(cxc, &visdn_cxc_list, cxc_list_node) {
		if (cxc->id == cxc_id) {
			up(&visdn_cxc_list_sem);
			return cxc;
		}
	}

	up(&visdn_cxc_list_sem);

	return NULL;
}

struct visdn_leg *visdn_cxc_get_leg_by_id(
	struct visdn_cxc *cxc,
	int chan_id)
{
	struct visdn_leg *leg, *found_leg = NULL;

	down_read(&cxc->subsys.rwsem);
	if (!visdn_cxc_get(cxc))
		goto err_cxc_get;

	list_for_each_entry(leg,
			&cxc->legs, cxc_legs_node) {
		visdn_leg_get(leg);


		if (leg->chan->id == chan_id) {
			found_leg = leg;
			break;
		}

		visdn_leg_put(leg);
	}

	up_read(&cxc->subsys.rwsem);

	return found_leg;

err_cxc_get:

	up_read(&cxc->subsys.rwsem);

	return NULL;
}
EXPORT_SYMBOL(visdn_cxc_get_leg_by_id);

struct visdn_leg *visdn_cxc_get_leg_by_src(
	struct visdn_cxc *cxc,
	struct visdn_leg *leg)
{
	struct visdn_cxc_connection *conn;
	struct hlist_node *tpos;

	rcu_read_lock();

	hlist_for_each_entry_rcu(conn, tpos,
			visdn_cxc_get_hash(cxc, leg),
			hash_node) {

		if (conn->src == leg) {
			visdn_leg_get(conn->dst);
			rcu_read_unlock();

			return conn->dst;
		}
	}

	rcu_read_unlock();

	return NULL;
}
EXPORT_SYMBOL(visdn_cxc_get_leg_by_src);

int visdn_cxc_leg_connected(
	struct visdn_cxc *cxc,
	struct visdn_leg *leg)
{

	struct hlist_node *tpos;
	struct visdn_cxc_connection *conn;

	rcu_read_lock();

	hlist_for_each_entry_rcu(conn, tpos,
			visdn_cxc_get_hash(cxc, leg),
			hash_node) {

		if (conn->src == leg) {
			rcu_read_unlock();
			return TRUE;
		}
	}

	rcu_read_unlock();

	return FALSE;
}
EXPORT_SYMBOL(visdn_cxc_leg_connected);

#define to_visdn_cxc(kobj) \
	container_of(kobj, struct visdn_cxc, subsys.kset.kobj)

#define to_visdn_cxc_attr(_attr) \
	container_of(_attr, struct visdn_cxc_attribute, attr)

static ssize_t visdn_cxc_attr_show(
	struct kobject *kobj,
	struct attribute *attr,
	char *buf)
{
	struct visdn_cxc_attribute *visdn_cxc_attr =
					to_visdn_cxc_attr(attr);
	struct visdn_cxc *visdn_cxc = to_visdn_cxc(kobj);
	ssize_t err;

	if (visdn_cxc_attr->show)
		err = visdn_cxc_attr->show(visdn_cxc, visdn_cxc_attr, buf);
	else
		err = -EIO;

	return err;
}

static ssize_t visdn_cxc_attr_store(
	struct kobject *kobj,
	struct attribute *attr,
	const char *buf,
	size_t count)
{
	struct visdn_cxc_attribute *visdn_cxc_attr =
					to_visdn_cxc_attr(attr);
	struct visdn_cxc *visdn_cxc = to_visdn_cxc(kobj);
	ssize_t err;

	if (visdn_cxc_attr->store)
		err = visdn_cxc_attr->store(visdn_cxc, visdn_cxc_attr,
							buf, count);
	else
		err = -EIO;

	return err;
}

static struct sysfs_ops visdn_cxc_sysfs_ops = {
	.show   = visdn_cxc_attr_show,
	.store  = visdn_cxc_attr_store,
};

static void visdn_cxc_release(struct kobject *kobj)
{
	struct visdn_cxc *cxc = to_visdn_cxc(kobj);

	visdn_debug(3, "visdn_cxc_release()\n");

	if (cxc->ops->release)
		cxc->ops->release(cxc);
	else {
		visdn_msg(KERN_ERR, "vISDN cxc '%s' does not have a release()"
			" function, it is broken and must be fixed.\n",
			cxc->name);
		WARN_ON(1);
	}
}

static ssize_t visdn_cxc_connect_store(
	struct visdn_cxc *cxc,
	struct visdn_cxc_attribute *attr,
	const char *buf,
	size_t count)
{
	int err;
	char locbuf[100];

	char *locbuf_p = locbuf;
	char *chan1_id_str;
	char *chan2_id_str;
	int chan1_id;
	int chan2_id;

	strncpy(locbuf, buf, sizeof(locbuf));
	chan1_id_str = strsep(&locbuf_p, ",\n\r");
	if (!chan1_id_str) {
		err = -EINVAL;
		goto err_chan1_untok;
	}

	if (sscanf(chan1_id_str, "%d", &chan1_id) < 1) {
		err = -EINVAL;
		goto err_chan1_unpars;
	}

	if (!chan1_id) {
		err = -EINVAL;
		goto err_chan1_inval;
	}

	chan2_id_str = strsep(&locbuf_p, ",\n\r");
	if (!chan2_id_str) {
		err = -EINVAL;
		goto err_chan2_untok;
	}

	if (sscanf(chan2_id_str, "%d", &chan2_id) < 1) {
		err = -EINVAL;
		goto err_chan2_unpars;
	}

	if (!chan2_id) {
		err = -EINVAL;
		goto err_chan2_inval;
	}

	err = visdn_cxc_connect_with_id(chan1_id, chan2_id, NULL,
					VISDN_CONNECT_FLAG_PERMANENT |
					VISDN_CONNECT_FLAG_OVERRIDE);
	if (err < 0)
		goto err_connect;

	return count;

	// visdn_disconnect
err_connect:
err_chan2_inval:
err_chan2_unpars:
err_chan2_untok:
err_chan1_inval:
err_chan1_unpars:
err_chan1_untok:

	return err;
}

VISDN_CXC_ATTR(connect, S_IWUSR, NULL, visdn_cxc_connect_store);

static struct attribute *visdn_cxc_default_attrs[] =
{
	&visdn_cxc_attr_connect.attr,
	NULL,
};

static struct kobj_type ktype_visdn_cxc = {
	.release	= visdn_cxc_release,
	.sysfs_ops	= &visdn_cxc_sysfs_ops,
	.default_attrs	= visdn_cxc_default_attrs,
};

void visdn_cxc_init(struct visdn_cxc *cxc)
{
	int i;

	memset(cxc, 0, sizeof(*cxc));

	init_MUTEX(&cxc->sem);

	INIT_LIST_HEAD(&cxc->legs);

	for (i=0; i<ARRAY_SIZE(cxc->connections_hash); i++)
		INIT_HLIST_HEAD(&cxc->connections_hash[i]);

	INIT_LIST_HEAD(&cxc->connections_list);
	INIT_LIST_HEAD(&cxc->cxc_list_node);
}
EXPORT_SYMBOL(visdn_cxc_init);

#ifndef HAVE_CLASS_DEV_DEVT
static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	return print_dev_t(buf, visdn_first_dev + 1);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);
#endif

int visdn_cxc_register(struct visdn_cxc *cxc)
{
	int err;

	{
	int cur_id = 0;
	struct visdn_cxc *cxc_tmp;

retry:
	if (cur_id >= 256) {
		err = -ENOSPC;
		goto err_id_not_found;
	}

	down(&visdn_cxc_list_sem);
	list_for_each_entry(cxc_tmp, &visdn_cxc_list, cxc_list_node) {
		if (cxc_tmp->id == cur_id) {
			up(&visdn_cxc_list_sem);
			cur_id++;
			goto retry;
		}
	}
	cxc->id = cur_id;
	up(&visdn_cxc_list_sem);
	}

	err = kobject_set_name(&cxc->subsys.kset.kobj, "%02d", cxc->id);
	if (err < 0)
		goto err_kobject_set_name;

	subsys_set_kset(cxc, visdn_tdm_subsys);

	err = subsystem_register(&cxc->subsys);
	if (err < 0)
		goto err_subsystem_register;

	cxc->router_node.is_channel = FALSE;
	visdn_router_add_node(&cxc->router_node);

	down(&visdn_cxc_list_sem);
	list_add_tail(&cxc->cxc_list_node, &visdn_cxc_list);
	up(&visdn_cxc_list_sem);

	return 0;

	subsystem_unregister(&cxc->subsys);
err_subsystem_register:
err_kobject_set_name:
err_id_not_found:

	return err;
}
EXPORT_SYMBOL(visdn_cxc_register);

int visdn_cxc_create_file(
	struct visdn_cxc *cxc,
	struct visdn_cxc_attribute *attr)
{
	int err = 0;

	if (visdn_cxc_get(cxc)) {
		err = sysfs_create_file(&cxc->subsys.kset.kobj, &attr->attr);
		visdn_cxc_put(cxc);
	}

	return err;
}
EXPORT_SYMBOL(visdn_cxc_create_file);

void visdn_cxc_remove_file(
	struct visdn_cxc *cxc,
	struct visdn_cxc_attribute *attr)
{
	if (visdn_cxc_get(cxc)) {
		sysfs_remove_file(&cxc->subsys.kset.kobj, &attr->attr);
		visdn_cxc_put(cxc);
	}
}
EXPORT_SYMBOL(visdn_cxc_remove_file);

void visdn_cxc_unregister(struct visdn_cxc *cxc)
{
	down(&visdn_cxc_list_sem);
	list_del_init(&cxc->cxc_list_node);
	up(&visdn_cxc_list_sem);

	subsystem_unregister(&cxc->subsys);
}
EXPORT_SYMBOL(visdn_cxc_unregister);

decl_subsys(visdn_tdm, &ktype_visdn_cxc, NULL);

static int visdn_cxc_cdev_open(
	struct inode *inode,
	struct file *file)
{
	nonseekable_open(inode, file);

	return 0;
}

static int visdn_cxc_cdev_release(
	struct inode *inode, struct file *file)
{
	visdn_cxc_disconnect_by_file(file);

	return 0;
}

int visdn_cxc_connect_with_id(
	int chan1_id,
	int chan2_id,
	struct file *file,
	unsigned long flags)
{
	struct visdn_chan *chan1;
	struct visdn_chan *chan2;
	struct visdn_leg *leg1;
	struct visdn_leg *leg2;
	struct visdn_cxc *cxc;
	int err;

	chan1 = visdn_chan_get_by_id(chan1_id);
	if (!chan1) {
		printk(KERN_DEBUG "Channel '%06d' not found\n",
			chan1_id);

		err = -ENODEV;
		goto err_search_src;
	}

	chan2 = visdn_chan_get_by_id(chan2_id);
	if (!chan2) {
		printk(KERN_DEBUG "Channel '%06d' not found\n",
			chan2_id);

		err = -ENODEV;
		goto err_search_dst;
	}

	if (chan1 == chan2) {
		err = -EINVAL;
		goto err_connect_self;
	}

	if (chan1->leg_a.cxc && chan1->leg_a.cxc == chan2->leg_a.cxc) {
		leg1 = &chan1->leg_a;
		leg2 = &chan2->leg_a;
		cxc = chan1->leg_a.cxc;
	} else if (chan1->leg_a.cxc && chan1->leg_a.cxc == chan2->leg_b.cxc) {
		leg1 = &chan1->leg_a;
		leg2 = &chan2->leg_b;
		cxc = chan1->leg_a.cxc;
	} else if (chan1->leg_b.cxc && chan1->leg_b.cxc == chan2->leg_a.cxc) {
		leg1 = &chan1->leg_b;
		leg2 = &chan2->leg_a;
		cxc = chan1->leg_b.cxc;
	} else if (chan1->leg_b.cxc && chan1->leg_b.cxc == chan2->leg_b.cxc) {
		leg1 = &chan1->leg_b;
		leg2 = &chan2->leg_b;
		cxc = chan1->leg_b.cxc;
	} else {
		err = -EINVAL;
		goto err_not_same_cxc;
	}

	err = visdn_cxc_connect(cxc, leg1, leg2, file, flags);
	if (err < 0)
		goto err_connect;

	// Release references returned by visdn_chan_get_by_id()
	visdn_chan_put(chan1);
	visdn_chan_put(chan2);

	return 0;

	// visdn_disconnect
err_connect:
err_not_same_cxc:
err_connect_self:
	visdn_chan_put(chan2);
err_search_dst:
	visdn_chan_put(chan1);
err_search_src:

	return err;
}
EXPORT_SYMBOL(visdn_cxc_connect_with_id);

static int visdn_cxc_cdev_do_connect(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg,
	int do_path)
{
	int err;
	struct visdn_connect connect;

	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	printk(KERN_DEBUG "Connecting '%06d' to '%06d' mode=%d\n",
		connect.src_chan_id,
		connect.dst_chan_id,
		do_path);

	if (do_path) {
		err = visdn_connect_path_with_id(
			connect.src_chan_id,
			connect.dst_chan_id,
			file,
			connect.flags);
	} else {
		err = visdn_cxc_connect_with_id(
			connect.src_chan_id,
			connect.dst_chan_id,
			file,
			connect.flags);
	}

	if (err < 0)
		goto err_cxc_connect;

	return 0;

err_cxc_connect:
err_copy_from_user:

	printk(KERN_DEBUG "Connection between '%06d' and '%06d' failed: %d\n",
		connect.src_chan_id,
		connect.dst_chan_id,
		err);

	return err;
}

int visdn_cxc_disconnect_with_id(
	int chan1_id,
	int chan2_id)
{
	struct visdn_chan *chan1;
	struct visdn_chan *chan2;
	struct visdn_leg *leg1;
	struct visdn_leg *leg2;
	struct visdn_cxc *cxc;
	int err;

	chan1 = visdn_chan_get_by_id(chan1_id);
	if (!chan1) {
		printk(KERN_DEBUG "Channel '%06d' not found\n",
			chan1_id);

		err = -ENODEV;
		goto err_search_src;
	}

	chan2 = visdn_chan_get_by_id(chan2_id);
	if (!chan2) {
		printk(KERN_DEBUG "Channel '%06d' not found\n",
			chan2_id);

		err = -ENODEV;
		goto err_search_dst;
	}

	if (chan1 == chan2) {
		err = -EINVAL;
		goto err_disconnect_self;
	}

	if (chan1->leg_a.cxc && chan1->leg_a.cxc == chan2->leg_a.cxc) {
		leg1 = &chan1->leg_a;
		leg2 = &chan2->leg_a;
		cxc = chan1->leg_a.cxc;
	} else if (chan1->leg_a.cxc && chan1->leg_a.cxc == chan2->leg_b.cxc) {
		leg1 = &chan1->leg_a;
		leg2 = &chan2->leg_b;
		cxc = chan1->leg_a.cxc;
	} else if (chan1->leg_b.cxc && chan1->leg_b.cxc == chan2->leg_a.cxc) {
		leg1 = &chan1->leg_b;
		leg2 = &chan2->leg_a;
		cxc = chan1->leg_b.cxc;
	} else if (chan1->leg_b.cxc && chan1->leg_b.cxc == chan2->leg_b.cxc) {
		leg1 = &chan1->leg_b;
		leg2 = &chan2->leg_b;
		cxc = chan1->leg_b.cxc;
	} else {
		err = -EINVAL;
		goto err_not_same_cxc;
	}

	err = visdn_cxc_disconnect(cxc, leg1, leg2);
	if (err < 0)
		goto err_disconnect;

	/* Release references returned by visdn_chan_get_by_id() */
	visdn_chan_put(chan1);
	visdn_chan_put(chan2);

	return 0;

	// visdn_disconnect
err_disconnect:
err_not_same_cxc:
err_disconnect_self:
	visdn_chan_put(chan2);
err_search_dst:
	visdn_chan_put(chan1);
err_search_src:

	return err;
}
EXPORT_SYMBOL(visdn_cxc_disconnect_with_id);

static int visdn_cxc_cdev_do_disconnect(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg,
	int do_path)
{
	struct visdn_connect connect;
	int err;

	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	if (do_path) {
		err = visdn_disconnect_path_with_id(
			connect.src_chan_id);
	} else {
		err = visdn_cxc_disconnect_with_id(
			connect.src_chan_id,
			connect.dst_chan_id);
	}
	if (err < 0)
		goto err_cxc_disconnect;

	return 0;

err_copy_from_user:
err_cxc_disconnect:

	return err;
}

static int visdn_cxc_cdev_do_enable_path(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct visdn_connect connect;
	int err;

	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	err = visdn_enable_path_with_id(connect.src_chan_id);
	if (err < 0)
		goto err_enable;

	return 0;

err_copy_from_user:
err_enable:

	return err;
}

static int visdn_cxc_cdev_do_disable_path(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct visdn_connect connect;
	int err;

	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	err = visdn_disable_path_with_id(connect.src_chan_id);
	if (err < 0)
		goto err_disable;

	return 0;

err_copy_from_user:
err_disable:

	return err;
}

static int visdn_cxc_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	switch(cmd) {
	case VISDN_IOC_CONNECT:
		return visdn_cxc_cdev_do_connect(inode, file, cmd, arg, 0);
	break;

	case VISDN_IOC_DISCONNECT:
		return visdn_cxc_cdev_do_disconnect(inode, file, cmd, arg, 0);
	break;

	case VISDN_IOC_CONNECT_PATH:
		return visdn_cxc_cdev_do_connect(inode, file, cmd, arg, 1);
	break;

	case VISDN_IOC_DISCONNECT_PATH:
		return visdn_cxc_cdev_do_disconnect(inode, file, cmd, arg, 1);
	break;

	case VISDN_IOC_ENABLE_PATH:
		return visdn_cxc_cdev_do_enable_path(inode, file, cmd, arg);
	break;

	case VISDN_IOC_DISABLE_PATH:
		return visdn_cxc_cdev_do_disable_path(inode, file, cmd, arg);
	break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static struct file_operations visdn_cxc_fops =
{
	.owner		= THIS_MODULE,
	.open		= visdn_cxc_cdev_open,
	.release	= visdn_cxc_cdev_release,
	.ioctl		= visdn_cxc_cdev_ioctl,
	.llseek		= no_llseek,
};

int visdn_cxc_modinit()
{
	int err;

	cdev_init(&visdn_cxc_cdev, &visdn_cxc_fops);
	visdn_cxc_cdev.owner = THIS_MODULE;

	err = cdev_add(&visdn_cxc_cdev, visdn_first_dev + 1, 1);
	if (err < 0)
		goto err_cdev_add;

	err = subsystem_register(&visdn_tdm_subsys);
	if (err < 0)
		goto err_subsystem_register;

	visdn_cxc_control_class_dev.class = &visdn_system_class;
	visdn_cxc_control_class_dev.class_data = NULL;
#ifdef HAVE_CLASS_DEV_DEVT
	visdn_cxc_control_class_dev.devt = visdn_first_dev + 1;
#endif
	snprintf(visdn_cxc_control_class_dev.class_id,
		sizeof(visdn_cxc_control_class_dev.class_id),
		"cxc-control");

	err = class_device_register(&visdn_cxc_control_class_dev);
	if (err < 0)
		goto err_control_class_device_register;

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_create_file(
		&visdn_cxc_control_class_dev,
		&class_device_attr_dev);
#endif

	return 0;

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&visdn_cxc_control_class_dev,
		&class_device_attr_dev);
#endif

	class_device_del(&visdn_cxc_control_class_dev);
err_control_class_device_register:
	cdev_del(&visdn_cxc_cdev);
err_cdev_add:
	subsystem_unregister(&visdn_tdm_subsys);
err_subsystem_register:

	return err;
}

void visdn_cxc_modexit()
{
#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&visdn_cxc_control_class_dev,
		&class_device_attr_dev);
#endif

	class_device_del(&visdn_cxc_control_class_dev);
	cdev_del(&visdn_cxc_cdev);
	subsystem_unregister(&visdn_tdm_subsys);
}
