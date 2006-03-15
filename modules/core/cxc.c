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
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#include "core.h"
#include "chan.h"
#include "cxc.h"
#include "path.h"
#include "visdn_mod.h"

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
	struct visdn_path *path)
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
			err = -EALREADY;
			goto err_already_connected;
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
	conn->path = path;

	down(&cxc->sem);

	hlist_add_head_rcu(&conn->hash_node,
			visdn_cxc_get_hash(cxc, src));

	{
	struct list_head *add_before = &cxc->connections_list;
	struct visdn_cxc_connection *tc;
	list_for_each_entry(tc, &cxc->connections_list, list_node) {
		if (tc->dst->chan->write_priority <
				conn->dst->chan->write_priority) {
			add_before = &tc->list_node;
			break;
		}
	}

	list_add_tail_rcu(&conn->list_node, add_before);
	}

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
	struct visdn_path *path)
{
	int err;

	BUG_ON(!cxc);
	BUG_ON(!leg1);
	BUG_ON(!leg2);
	BUG_ON(leg1->cxc != cxc);
	BUG_ON(leg2->cxc != cxc);

	visdn_debug(1, "Connecting chan '%s' to chan '%s'\n",
		leg1->chan->kobj.name,
		leg2->chan->kobj.name);

	// Connect the channels -------------------------------------
	err = visdn_cxc_connect_simplex(cxc, leg1, leg2, path);
	if (err < 0)
		goto err_cxc_add_leg1;

	err = visdn_cxc_connect_simplex(cxc, leg2, leg1, path);
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

#if 0
static ssize_t visdn_cxc_connect_store(
	struct visdn_cxc *cxc,
	struct visdn_cxc_attribute *attr,
	const char *buf,
	size_t count)
{
	int err;
	char locbuf[100];
	struct visdn_path *path;
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

	path = visdn_path_connect_by_id(chan1_id, chan2_id, NULL,
					VISDN_CONNECT_FLAG_PERMANENT, &err);
	if (!path)
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
#endif

static struct attribute *visdn_cxc_default_attrs[] =
{
//	&visdn_cxc_attr_connect.attr,
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

decl_subsys_name(visdn_tdm, tdm, &ktype_visdn_cxc, NULL);

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

int visdn_cxc_modinit()
{
	int err;

	visdn_tdm_subsys.kset.kobj.parent = &visdn_subsys.kset.kobj;

	err = subsystem_register(&visdn_tdm_subsys);
	if (err < 0)
		goto err_subsystem_register;

	return 0;

	subsystem_unregister(&visdn_tdm_subsys);
err_subsystem_register:

	return err;
}

void visdn_cxc_modexit()
{
	subsystem_unregister(&visdn_tdm_subsys);
}
