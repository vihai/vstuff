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
#include "router.h"
#include "visdn_mod.h"

static struct cdev visdn_router_cdev;
struct class_device visdn_router_control_class_dev;

struct list_head visdn_paths_list = LIST_HEAD_INIT(visdn_paths_list);
DECLARE_MUTEX(visdn_paths_list_sem);

struct subsystem visdn_paths_subsys;

struct visdn_path *visdn_path_alloc()
{
	struct visdn_path *path;

	path = kmalloc(sizeof(*path), GFP_KERNEL);
	if (!path)
		return NULL;

	memset(path, 0, sizeof(*path));

	INIT_LIST_HEAD(&path->node);

	kobject_init(&path->kobj);

	return path;
}

struct visdn_path *_visdn_path_search_by_id(int id)
{
	struct visdn_path *path;

	list_for_each_entry(path, &visdn_paths_list, node) {
		if (path->id == id)
			return path;
	}

	return NULL;
}

struct visdn_path *visdn_path_get_by_id(int id)
{
	struct visdn_path *path;

	down(&visdn_paths_list_sem);

	path = visdn_path_get(_visdn_path_search_by_id(id));

	up(&visdn_paths_list_sem);

	return path;
}

static int _visdn_path_new_id(void)
{
	static int cur_id;

	for (;;) {
		/* Maybe reusing path ids would be better */

		if (++cur_id <= 0)
			cur_id = 1;

		if (!_visdn_path_search_by_id(cur_id))
			return cur_id;
	}
}

struct visdn_path *visdn_path_get_by_endpoint(struct visdn_chan *chan)
{
	struct visdn_path *path;

	down(&visdn_paths_list_sem);

	list_for_each_entry(path, &visdn_paths_list, node) {
		if (path->ep1 == chan ||
		    path->ep2 == chan) {
			up(&visdn_paths_list_sem);

			return path;
		}
	}

	up(&visdn_paths_list_sem);

	return NULL;
}
EXPORT_SYMBOL(visdn_path_get_by_endpoint);

int visdn_path_find_lowest_mtu(struct visdn_path *path)
{
	struct visdn_leg *cur_leg;
	struct visdn_leg *next_leg;
	int min_mtu = 65536;

	cur_leg = visdn_leg_get(&path->ep1->leg_a);

	while(cur_leg->cxc) {
		next_leg = visdn_cxc_get_leg_by_src(cur_leg->cxc, cur_leg);
		if (!next_leg) {
			printk(KERN_ERR
				"vnd_find_lowest_mtu on unconnected leg?\n");
			break;
		}

		if (next_leg->mtu != -1 &&
		    next_leg->mtu < min_mtu)
			min_mtu = next_leg->mtu;

		visdn_leg_put(cur_leg);
		cur_leg = visdn_leg_get(next_leg->other_leg);
		visdn_leg_put(next_leg);
	}

	visdn_leg_put(cur_leg);

	return min_mtu;
}
EXPORT_SYMBOL(visdn_path_find_lowest_mtu);

struct visdn_chan *visdn_path_get_other_endpoint(
	struct visdn_path *path,
	struct visdn_chan *chan)
{
	struct visdn_leg *cur_leg;
	struct visdn_leg *next_leg;
	struct visdn_chan *other_ep;

	BUG_ON(chan != path->ep1 && chan != path->ep2);

	cur_leg = visdn_leg_get(&chan->leg_a);

	while(cur_leg->cxc) {
		next_leg = visdn_cxc_get_leg_by_src(cur_leg->cxc, cur_leg);
		BUG_ON(!next_leg);

		visdn_leg_put(cur_leg);
		cur_leg = visdn_leg_get(next_leg->other_leg);
		visdn_leg_put(next_leg);
	}

	other_ep = visdn_chan_get(cur_leg->chan);
	visdn_leg_put(cur_leg);

	return other_ep;
}
EXPORT_SYMBOL(visdn_path_get_other_endpoint);

struct visdn_path *visdn_path_connect(
	struct visdn_chan *src_chan,
	struct visdn_chan *dst_chan,
	struct file *file,
	unsigned long flags,
	int *err)
{
	struct visdn_path *path;
	int done_entries = 0;

	if (src_chan->leg_a.cxc && src_chan->leg_b.cxc) {
		printk(KERN_ERR "Channel '%06d' is not an endpoint\n",
			src_chan->id);
		*err = -EINVAL;
		return NULL;
	}

	if (dst_chan->leg_a.cxc && dst_chan->leg_b.cxc) {
		printk(KERN_ERR "Channel '%06d' is not an endpoint\n",
			dst_chan->id);
		*err = -EINVAL;
		return NULL;
	}

	path = visdn_path_alloc();
	if (!path) {
		*err = -EFAULT;
		goto err_path_alloc;
	}

	path->ep1 = src_chan;
	path->ep2 = dst_chan;

	if (flags & VISDN_CONNECT_FLAG_PERMANENT)
		path->file = NULL;
	else
		path->file = file;

	down(&visdn_paths_list_sem);
	path->id = _visdn_path_new_id();

	visdn_router_lock();
	visdn_router_run(&src_chan->router_node);

	{
	struct visdn_router_arch *prev_arch = NULL, *next_arch = NULL;
	struct visdn_router_node *node = &dst_chan->router_node;
	while(node) {
		visdn_router_print_node_name(node);

		if (!node->prev_thru)
			break;
//		else
//			verbose(" ===(%06d)===> ", node->prev_thru->id);

		next_arch = node->prev_thru;

		if (prev_arch && next_arch) {
			WARN_ON(node->is_channel);

			*err = visdn_cxc_connect(
				container_of(node, struct visdn_cxc,
							router_node),
				next_arch->src_leg->other_leg,
				prev_arch->src_leg,
				path);
			if (*err < 0) {
				up(&visdn_paths_list_sem);
				goto err_router_connect;
			}

			done_entries++;
		}

		prev_arch = node->prev_thru;

		node = node->prev;
	}
	}

	visdn_router_unlock();

	list_add(&path->node, &visdn_paths_list);

	up(&visdn_paths_list_sem);

	kobject_set_name(&path->kobj, "%06d", path->id);
	kobj_set_kset_s(path, visdn_paths_subsys);

	*err = kobject_add(&path->kobj);
	if (*err < 0)
		goto err_kobject_add;

	*err = 0;
	return path;

	kobject_del(&path->kobj);
err_kobject_add:
	down(&visdn_paths_list_sem);
	list_del(&path->node);
	up(&visdn_paths_list_sem);
err_router_connect:
	{
	struct visdn_router_arch *prev_arch = NULL, *next_arch = NULL;
	struct visdn_router_node *node = &dst_chan->router_node;
	int i = 0;
	while(node) {
		if (!node->prev_thru)
			break;

		next_arch = node->prev_thru;

		if (prev_arch && next_arch) {
			if (i >= done_entries)
				break;

			visdn_cxc_disconnect(
				container_of(node, struct visdn_cxc,
							router_node),
				next_arch->src_leg->other_leg,
				prev_arch->src_leg);

			i++;
		}

		prev_arch = node->prev_thru;

		node = node->prev;
	}
	}

	visdn_router_unlock();

err_path_alloc:

	return NULL;
}

struct visdn_path *visdn_path_connect_by_id(
	int chan1_id,
	int chan2_id,
	struct file *file,
	unsigned long flags,
	int *err)
{
	struct visdn_chan *chan1;
	struct visdn_chan *chan2;
	struct visdn_path *path;

	chan1 = visdn_chan_get_by_id(chan1_id);
	if (!chan1) {
		visdn_debug(1, "Channel '%06d' not found\n", chan1_id); 
		*err = -ENODEV;
		goto err_search_src;
	}

	chan2 = visdn_chan_get_by_id(chan2_id);
	if (!chan2) {
		visdn_debug(1, "Channel '%06d' not found\n", chan2_id); 
		*err = -ENODEV;
		goto err_search_dst;
	}

	if (chan1 == chan2) {
		*err = -EINVAL;
		goto err_connect_self;
	}

	path = visdn_path_connect(chan1, chan2, file, flags, err);
	if (!path)
		goto err_connect;

	/* Release references returned by visdn_chan_get_by_id() */
	visdn_chan_put(chan1);
	visdn_chan_put(chan2);

	return path;

	// visdn_disconnect
err_connect:
err_connect_self:
	visdn_chan_put(chan2);
err_search_dst:
	visdn_chan_put(chan1);
err_search_src:

	return NULL;
}
EXPORT_SYMBOL(visdn_path_connect_by_id);

int visdn_path_disconnect(
	struct visdn_path *path)
{
	struct visdn_leg *cur_leg, *next_leg;

	down(&visdn_paths_list_sem);
	kobject_del(&path->kobj);
	list_del(&path->node);
	up(&visdn_paths_list_sem);

	cur_leg = &path->ep1->leg_a;

	visdn_leg_get(cur_leg);

	while(cur_leg->cxc) {
		next_leg = visdn_cxc_get_leg_by_src(cur_leg->cxc, cur_leg);
		if (!next_leg)
			break;

		visdn_cxc_disconnect(cur_leg->cxc, cur_leg, next_leg);

		visdn_leg_put(cur_leg);
		cur_leg = visdn_leg_get(next_leg->other_leg);
		visdn_leg_put(next_leg);
	}

	visdn_leg_put(cur_leg);

	return 0;
}
EXPORT_SYMBOL(visdn_path_disconnect);

int visdn_path_disconnect_by_id(int id)
{
	struct visdn_path *path;
	int err;

	path = visdn_path_get_by_id(id);
	if (!path) {
		visdn_debug(1, "Path '%d' not found\n", id);

		err = -ENODEV;
		goto err_search_src;
	}

	err = visdn_path_disconnect(path);
	if (err < 0)
		goto err_disconnect;

	/* Release reference returned by visdn_path_get_by_id() */
	visdn_path_put(path);

	return 0;

	// visdn_disconnect
err_disconnect:
	visdn_path_put(path);
err_search_src:

	return err;
}
EXPORT_SYMBOL(visdn_path_disconnect_by_id);

int visdn_path_enable(
	struct visdn_path *path)
{
	struct visdn_leg *cur_leg, *next_leg;
	int err;

	err = visdn_chan_enable(path->ep1);
	if (err < 0)
		return err;

	cur_leg = &path->ep1->leg_a;

	visdn_leg_get(cur_leg);

	while(cur_leg->cxc) {
		next_leg = visdn_cxc_get_leg_by_src(cur_leg->cxc, cur_leg);
		if (!next_leg)
			break;

		err = visdn_chan_enable(next_leg->chan);
		if (err < 0) {
			visdn_leg_put(next_leg);
			visdn_leg_put(cur_leg);

			return err;
		}

		visdn_leg_put(cur_leg);
		cur_leg = visdn_leg_get(next_leg->other_leg);
		visdn_leg_put(next_leg);
	}

	visdn_leg_put(cur_leg);

	return 0;
}
EXPORT_SYMBOL(visdn_path_enable);

int visdn_path_disable(
	struct visdn_path *path)
{
	struct visdn_leg *cur_leg, *next_leg;

	visdn_chan_disable(path->ep1);

	cur_leg = &path->ep1->leg_a;

	visdn_leg_get(cur_leg);

	while(cur_leg->cxc) {
		next_leg = visdn_cxc_get_leg_by_src(cur_leg->cxc, cur_leg);
		if (!next_leg)
			break;

		visdn_chan_disable(next_leg->chan);

		visdn_leg_put(cur_leg);
		cur_leg = visdn_leg_get(next_leg->other_leg);
		visdn_leg_put(next_leg);
	}

	visdn_leg_put(cur_leg);

	return 0;
}
EXPORT_SYMBOL(visdn_path_disable);

static int visdn_router_cdev_open(
	struct inode *inode,
	struct file *file)
{
	nonseekable_open(inode, file);

	return 0;
}

static int visdn_router_cdev_release(
	struct inode *inode, struct file *file)
{
//	visdn_router_disconnect_by_file(file);

	return 0;
}

static int visdn_router_cdev_do_connect(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	int err;
	struct visdn_connect connect;
	struct visdn_path *path;

	if (copy_from_user(&connect, (void __user *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	visdn_debug(1,
		"Connecting '%06d' to '%06d'\n",
		connect.src_chan_id,
		connect.dst_chan_id);

	path = visdn_path_connect_by_id(
		connect.src_chan_id,
		connect.dst_chan_id,
		file,
		connect.flags,
		&err);
	if (!path)
		goto err_router_connect;

	connect.path_id = path->id;

	if (copy_to_user((void __user *)arg, &connect, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_to_user;
	}

	return 0;

err_copy_to_user:
	visdn_path_disconnect(path);
err_router_connect:
err_copy_from_user:
	visdn_msg(KERN_NOTICE,
		"Connection between '%06d' and '%06d' failed: %d\n",
		connect.src_chan_id,
		connect.dst_chan_id,
		err);

	return err;
}

static int visdn_router_cdev_do_disconnect(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct visdn_connect connect;
	struct visdn_path *path;
	int err;

	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	path = visdn_path_get_by_id(connect.path_id);
	if (!path) {
		err = -ENOENT;
		goto err_no_path;
	}

	err = visdn_path_disconnect(path);
	if (err < 0)
		goto err_path_disconnect;

	/* Release reference returned by visdn_path_get_by_id() */
	visdn_path_put(path);

	return 0;

err_no_path:
err_path_disconnect:
err_copy_from_user:

	return err;
}

static int visdn_router_cdev_do_disconnect_endpoint(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct visdn_connect connect;
	struct visdn_path *path;
	struct visdn_chan *chan;
	int err;

	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	chan = visdn_chan_get_by_id(connect.src_chan_id);
	if (!chan) {
		err = -ENOENT;
		goto err_no_chan;
	}

	path = visdn_path_get_by_endpoint(chan);
	if (!path) {
		err = -ENOTCONN;
		goto err_no_path;
	}

	err = visdn_path_disconnect(path);
	if (err < 0)
		goto err_path_disconnect;

	visdn_path_put(path);
	visdn_chan_put(chan);

	return 0;

err_no_path:
	visdn_chan_put(chan);
err_no_chan:
err_path_disconnect:
err_copy_from_user:

	return err;
}

static int visdn_router_cdev_do_enable_path(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct visdn_connect connect;
	struct visdn_path *path;
	int err;

	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	path = visdn_path_get_by_id(connect.path_id);
	if (!path) {
		err = -ENOENT;
		goto err_no_path;
	}

	err = visdn_path_enable(path);
	if (err < 0)
		goto err_enable;

	/* Release reference returned by visdn_path_get_by_id() */
	visdn_path_put(path);

	return 0;

err_no_path:
err_enable:
err_copy_from_user:

	return err;
}

static int visdn_router_cdev_do_disable_path(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct visdn_connect connect;
	struct visdn_path *path;
	int err;

	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	path = visdn_path_get_by_id(connect.path_id);
	if (!path) {
		err = -ENOENT;
		goto err_no_path;
	}

	err = visdn_path_disable(path);
	if (err < 0)
		goto err_disable;

	/* Release reference returned by visdn_path_get_by_id() */
	visdn_path_put(path);

	return 0;

err_no_path:
err_disable:
err_copy_from_user:

	return err;
}

static int visdn_router_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	switch(cmd) {
	case VISDN_IOC_CONNECT:
		return visdn_router_cdev_do_connect(inode, file, cmd, arg);
	break;

	case VISDN_IOC_DISCONNECT:
		return visdn_router_cdev_do_disconnect(inode, file, cmd, arg);
	break;

	case VISDN_IOC_DISCONNECT_ENDPOINT:
		return visdn_router_cdev_do_disconnect_endpoint(
							inode, file, cmd, arg);
	break;

	case VISDN_IOC_ENABLE_PATH:
		return visdn_router_cdev_do_enable_path(inode, file, cmd, arg);
	break;

	case VISDN_IOC_DISABLE_PATH:
		return visdn_router_cdev_do_disable_path(inode, file, cmd, arg);
	break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static struct file_operations visdn_router_fops =
{
	.owner		= THIS_MODULE,
	.open		= visdn_router_cdev_open,
	.release	= visdn_router_cdev_release,
	.ioctl		= visdn_router_cdev_ioctl,
	.llseek		= no_llseek,
};

#define to_visdn_path_attr(_attr) \
	container_of(_attr, struct visdn_path_attribute, attr)

static ssize_t visdn_path_attr_show(
	struct kobject *kobj,
	struct attribute *attr,
	char *buf)
{
	struct visdn_path_attribute *visdn_path_attr =
					to_visdn_path_attr(attr);
	struct visdn_path *visdn_path = to_visdn_path(kobj);
	ssize_t err;

	if (visdn_path_attr->show)
		err = visdn_path_attr->show(visdn_path, visdn_path_attr, buf);
	else
		err = -EIO;

	return err;
}

static ssize_t visdn_path_attr_store(
	struct kobject *kobj,
	struct attribute *attr,
	const char *buf,
	size_t count)
{
	struct visdn_path_attribute *visdn_path_attr =
					to_visdn_path_attr(attr);
	struct visdn_path *visdn_path = to_visdn_path(kobj);
	ssize_t err;

	if (visdn_path_attr->store)
		err = visdn_path_attr->store(visdn_path, visdn_path_attr,
							buf, count);
	else
		err = -EIO;

	return err;
}

static struct sysfs_ops visdn_path_sysfs_ops = {
	.show   = visdn_path_attr_show,
	.store  = visdn_path_attr_store,
};

static void visdn_path_release(struct kobject *kobj)
{
}

static struct attribute *visdn_path_default_attrs[] =
{
	NULL,
};

static struct kobj_type ktype_visdn_path = {
	.release	= visdn_path_release,
	.sysfs_ops	= &visdn_path_sysfs_ops,
	.default_attrs	= visdn_path_default_attrs,
};

decl_subsys_name(visdn_paths, paths, &ktype_visdn_path, NULL);

int visdn_path_modinit()
{
	int err;

	cdev_init(&visdn_router_cdev, &visdn_router_fops);
	visdn_router_cdev.owner = THIS_MODULE;

	err = cdev_add(&visdn_router_cdev, visdn_first_dev + 1, 1);
	if (err < 0)
		goto err_cdev_add;

	visdn_paths_subsys.kset.kobj.parent = &visdn_subsys.kset.kobj;

	err = subsystem_register(&visdn_paths_subsys);
	if (err < 0)
		goto err_subsystem_register;

	visdn_router_control_class_dev.class = &visdn_system_class;
	visdn_router_control_class_dev.class_data = NULL;
#ifdef HAVE_CLASS_DEV_DEVT
	visdn_router_control_class_dev.devt = visdn_first_dev + 1;
#endif
	snprintf(visdn_router_control_class_dev.class_id,
		sizeof(visdn_router_control_class_dev.class_id),
		"router-control");

	err = class_device_register(&visdn_router_control_class_dev);
	if (err < 0)
		goto err_control_class_device_register;

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_create_file(
		&visdn_router_control_class_dev,
		&class_device_attr_dev);
#endif

	return 0;

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&visdn_router_control_class_dev,
		&class_device_attr_dev);
#endif

	class_device_del(&visdn_router_control_class_dev);
err_control_class_device_register:
	cdev_del(&visdn_router_cdev);
err_cdev_add:
	subsystem_unregister(&visdn_paths_subsys);
err_subsystem_register:

	return err;
}

void visdn_path_modexit()
{
#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&visdn_router_control_class_dev,
		&class_device_attr_dev);
#endif

	class_device_del(&visdn_router_control_class_dev);
	cdev_del(&visdn_router_cdev);
	subsystem_unregister(&visdn_paths_subsys);
}
