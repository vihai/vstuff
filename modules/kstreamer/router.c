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
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <asm/uaccess.h>

#include "kstreamer.h"
#include "kstreamer_priv.h"
#include "node.h"
#include "link.h"
#include "pipeline.h"
#include "router.h"

dev_t ks_first_dev;

static struct cdev ks_router_cdev;
struct class_device ks_router_control_class_dev;

//#define verbose(...) do {} while(0)

#define verbose(fmt, arg...) printk(fmt, ## arg)

//#define DIJ_DEBUG

#if 0
//

static struct list_head ks_nodes =
			LIST_HEAD_INIT(ks_nodes);
static struct list_head ks_links =
			LIST_HEAD_INIT(ks_links);
#endif

static DECLARE_MUTEX(ks_router_sem);

#if 0
void ks_router_add_node(
	struct ks_node *node)
{
	down(&ks_router_sem);
	list_add_tail(&node->node, &ks_nodes);
	up(&ks_router_sem);
}

void ks_router_del_node(
	struct ks_node *node)
{
	down(&ks_router_sem);
	list_del(&node->node);
	up(&ks_router_sem);
}

void ks_router_add_arch(
	struct ks_link *arch)
{
	down(&ks_router_sem);
	list_add_tail(&arch->node, &ks_links);
	up(&ks_router_sem);
}

void ks_router_del_arch(
	struct ks_link *arch)
{
	down(&ks_router_sem);
	list_del(&arch->node);
	up(&ks_router_sem);
}
#endif

static int ks_router_cdev_open(
	struct inode *inode,
	struct file *file)
{
	nonseekable_open(inode, file);

	return 0;
}

static int ks_router_cdev_release(
	struct inode *inode, struct file *file)
{
//	ks_router_disconnect_by_file(file);

	return 0;
}

static void ks_router_run(
	struct ks_node *start,
	struct ks_node *to)
{
	down_read(&ks_nodes_subsys.rwsem);
	down_read(&ks_links_subsys.rwsem);

	{
	struct ks_node *node;
	list_for_each_entry(node, &ks_nodes_subsys.kset.list, kobj.entry) {
		node->router_cost = INT_MAX;
		node->router_done = FALSE;
		node->router_prev = NULL;
		node->router_prev_thru = NULL;
	}
	}

	start->router_cost = 0;

	while(1) {
		/* Extract the node with lowest cost */
		struct ks_node *min_cost_node = NULL;
		struct ks_node *node;
		list_for_each_entry(node, &ks_nodes_subsys.kset.list,
						kobj.entry) {
			if (!node->router_done &&
			    (!min_cost_node ||
			    node->router_cost < min_cost_node->router_cost))
				min_cost_node = node;
		}

		if (!min_cost_node)
			break;

		if (min_cost_node == to)
			break;

#ifdef DIJ_DEBUG
		verbose(KERN_DEBUG
			"Min cost (%d) node = %s/%s/%s\n",
			min_cost_node->router_cost,
			min_cost_node->kobj.parent->parent->name,
			min_cost_node->kobj.parent->name,
			min_cost_node->kobj.name);
#endif

		min_cost_node->router_done = TRUE;

		/* For each arch exiting from node 'min_cost_node' */

		{
		struct ks_link *arch;
		list_for_each_entry(arch, &ks_links_subsys.kset.list,
							kobj.entry) {

			if (arch->from != min_cost_node)
				continue;

			if (arch->pipeline)
				continue;

#ifdef DIJ_DEBUG
			verbose(KERN_DEBUG
				"    Arch (%s/%s/%s) from"
				" node (%s/%s/%s) to node (%s/%s/%s),"
				" cost = %d\n",
				arch->kobj.parent->parent->name,
				arch->kobj.parent->name,
				arch->kobj.name,
				arch->from->kobj.parent->parent->name,
				arch->from->kobj.parent->name,
				arch->from->kobj.name,
				arch->to->kobj.parent->parent->name,
				arch->to->kobj.parent->name,
				arch->to->kobj.name,
				arch->cost);
#endif

			if (arch->cost != INT_MAX &&
			    min_cost_node->router_cost != INT_MAX &&
			    arch->to->router_cost >
			    	min_cost_node->router_cost + arch->cost) {

				arch->to->router_cost =
					min_cost_node->router_cost +
					arch->cost;

				arch->to->router_prev = min_cost_node;
				arch->to->router_prev_thru = arch;

#ifdef DIJ_DEBUG
				verbose(KERN_DEBUG
					"        => Relaxing node (%s/%s/%s)"
					" new cost = %d\n",
					arch->to->kobj.parent->parent->name,
					arch->to->kobj.parent->name,
					arch->to->kobj.name,
					arch->to->router_cost);
#endif
			}
		}
		}
	}

	up_read(&ks_links_subsys.rwsem);
	up_read(&ks_nodes_subsys.rwsem);
}

struct ks_pipeline *ks_router_connect(
	struct ks_node *from,
	struct ks_node *to,
	struct file *file,
	unsigned long flags,
	int *err)
{
	struct ks_pipeline *pipeline;

#if 0
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
#endif

	pipeline = ks_pipeline_alloc();
	if (!pipeline) {
		*err = -EFAULT;
		goto err_pipeline_alloc;
	}

	if (flags & KS_CONNECT_FLAG_PERMANENT)
		pipeline->file = NULL;
	else
		pipeline->file = file;

	down(&ks_router_sem);

	ks_router_run(from, to);

	{
	struct ks_node *node = to;
	do {
		if (!node->router_prev_thru)
			break;

		node->router_prev_thru->pipeline = ks_pipeline_get(pipeline);

		list_add(&ks_link_get(node->router_prev_thru)->
							pipeline_entry,
			&pipeline->entries);

	} while((node = node->router_prev));
	}

	ks_pipeline_connect(pipeline);
	ks_pipeline_dump(pipeline);

	up(&ks_router_sem);

	*err = ks_pipeline_register(pipeline);
	if (*err < 0)
		goto err_pipeline_register;

	*err = 0;
	return pipeline;

	ks_pipeline_unregister(pipeline);
err_pipeline_register:

	// Disconnect and release links
	
	ks_pipeline_put(pipeline);
err_pipeline_alloc:

	return NULL;
}


struct ks_pipeline *ks_router_connect_by_path(
	const char *node1_path,
	const char *node2_path,
	struct file *file,
	unsigned long flags,
	int *err)
{
	struct ks_node *node1;
	struct ks_node *node2;
	struct ks_pipeline *pipeline;

	node1 = ks_node_get_by_path(node1_path);
	if (!node1) {
		ks_debug(1, "Endpoint '%s' not found\n", node1_path); 
		*err = -ENODEV;
		goto err_search_src;
	}

	node2 = ks_node_get_by_path(node2_path);
	if (!node2) {
		ks_debug(1, "Endpoint '%s' not found\n", node2_path); 
		*err = -ENODEV;
		goto err_search_dst;
	}

	if (node1 == node2) {
		*err = -EINVAL;
		goto err_connect_self;
	}

	pipeline = ks_router_connect(node1, node2, file, flags, err);
	if (!pipeline)
		goto err_connect;

	/* Release references returned by ks_node_get_by_id() */
	ks_node_put(node1);
	ks_node_put(node2);

	return pipeline;

	ks_pipeline_disconnect(pipeline);
	ks_pipeline_put(pipeline);
err_connect:
err_connect_self:
	ks_node_put(node2);
err_search_dst:
	ks_node_put(node1);
err_search_src:

	return NULL;
}

static int ks_router_cdev_do_connect(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	int err;
	struct ks_connect connect;
	struct ks_pipeline *pipeline;

	if (copy_from_user(&connect, (void __user *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	ks_debug(1,
		"Connecting '%s' to '%s'\n",
		connect.from_endpoint,
		connect.to_endpoint);

	pipeline = ks_router_connect_by_path(
		connect.from_endpoint,
		connect.to_endpoint,
		file,
		connect.flags,
		&err);
	if (!pipeline)
		goto err_router_connect;

	connect.pipeline_id = pipeline->id;

	if (copy_to_user((void __user *)arg, &connect, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_to_user;
	}

	ks_pipeline_put(pipeline);

	return 0;

err_copy_to_user:
	ks_pipeline_disconnect(pipeline);
	ks_pipeline_put(pipeline);
err_router_connect:
err_copy_from_user:
	ks_msg(KERN_NOTICE,
		"Connection between '%s' and '%s' failed: %d\n",
		connect.from_endpoint,
		connect.to_endpoint,
		err);

	return err;
}

static int ks_router_cdev_do_disconnect(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct ks_connect connect;
	struct ks_pipeline *pipeline;
	int err;

	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	pipeline = ks_pipeline_get_by_id(connect.pipeline_id);
	if (!pipeline) {
		err = -ENOENT;
		goto err_no_pipeline;
	}

	ks_pipeline_disconnect(pipeline);

	/* Release reference returned by ks_pipeline_get_by_id() */
	ks_pipeline_put(pipeline);

	return 0;

err_no_pipeline:
err_copy_from_user:

	return err;
}

static int ks_router_cdev_do_disconnect_endpoint(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct ks_connect connect;
	struct ks_pipeline *pipeline;
	int err;

	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

#if 0
	chan = ks_chan_get_by_id(connect.from_endpoint_id);
	if (!chan) {
		err = -ENOENT;
		goto err_no_chan;
	}

	pipeline = ks_pipeline_get_by_endpoint(chan);
	if (!pipeline) {
		err = -ENOTCONN;
		goto err_no_pipeline;
	}

	err = ks_pipeline_disconnect(pipeline);
	if (err < 0)
		goto err_pipeline_disconnect;

	ks_pipeline_put(pipeline);
	ks_chan_put(chan);
#endif

	return 0;

err_pipeline_disconnect:
err_no_pipeline:
	ks_pipeline_put(pipeline);
err_no_chan:
//	ks_chan_put(chan);
err_copy_from_user:

	return err;
}

static int ks_router_cdev_do_open_pipeline(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct ks_connect connect;
	struct ks_pipeline *pipeline;
	int err;

	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	pipeline = ks_pipeline_get_by_id(connect.pipeline_id);
	if (!pipeline) {
		err = -ENOENT;
		goto err_no_pipeline;
	}

	err = ks_pipeline_open(pipeline);
	if (err < 0)
		goto err_open;

	/* Release reference returned by ks_pipeline_get_by_id() */
	ks_pipeline_put(pipeline);

	return 0;

err_no_pipeline:
err_open:
err_copy_from_user:

	return err;
}

static int ks_router_cdev_do_close_pipeline(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct ks_connect connect;
	struct ks_pipeline *pipeline;
	int err;

	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	pipeline = ks_pipeline_get_by_id(connect.pipeline_id);
	if (!pipeline) {
		err = -ENOENT;
		goto err_no_pipeline;
	}

	ks_pipeline_close(pipeline);

	/* Release reference returned by ks_pipeline_get_by_id() */
	ks_pipeline_put(pipeline);

	return 0;

err_no_pipeline:
err_copy_from_user:

	return err;
}

static int ks_router_cdev_do_start_pipeline(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct ks_connect connect;
	struct ks_pipeline *pipeline;
	int err;

	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	pipeline = ks_pipeline_get_by_id(connect.pipeline_id);
	if (!pipeline) {
		err = -ENOENT;
		goto err_no_pipeline;
	}

	err = ks_pipeline_start(pipeline);
	if (err < 0)
		goto err_start;

	/* Release reference returned by ks_pipeline_get_by_id() */
	ks_pipeline_put(pipeline);

	return 0;

err_no_pipeline:
err_start:
err_copy_from_user:

	return err;
}

static int ks_router_cdev_do_stop_pipeline(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct ks_connect connect;
	struct ks_pipeline *pipeline;
	int err;

	if (copy_from_user(&connect, (void *)arg, sizeof(connect))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	pipeline = ks_pipeline_get_by_id(connect.pipeline_id);
	if (!pipeline) {
		err = -ENOENT;
		goto err_no_pipeline;
	}

	ks_pipeline_stop(pipeline);

	/* Release reference returned by ks_pipeline_get_by_id() */
	ks_pipeline_put(pipeline);

	return 0;

err_no_pipeline:
err_copy_from_user:

	return err;
}

static int ks_router_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	switch(cmd) {
	case KS_IOC_CONNECT:
		return ks_router_cdev_do_connect(inode, file, cmd, arg);
	break;

	case KS_IOC_DISCONNECT:
		return ks_router_cdev_do_disconnect(inode, file, cmd, arg);
	break;

	case KS_IOC_DISCONNECT_ENDPOINT:
		return ks_router_cdev_do_disconnect_endpoint(
						inode, file, cmd, arg);
	break;

	case KS_IOC_PIPELINE_OPEN:
		return ks_router_cdev_do_open_pipeline(
					inode, file, cmd, arg);
	break;

	case KS_IOC_PIPELINE_CLOSE:
		return ks_router_cdev_do_close_pipeline(
					inode, file, cmd, arg);
	break;

	case KS_IOC_PIPELINE_START:
		return ks_router_cdev_do_start_pipeline(
					inode, file, cmd, arg);
	break;

	case KS_IOC_PIPELINE_STOP:
		return ks_router_cdev_do_stop_pipeline(
					inode, file, cmd, arg);
	break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static struct file_operations ks_router_fops =
{
	.owner		= THIS_MODULE,
	.open		= ks_router_cdev_open,
	.release	= ks_router_cdev_release,
	.ioctl		= ks_router_cdev_ioctl,
	.llseek		= no_llseek,
};

static void ks_system_class_release(struct class_device *cd)
{
}

struct class ks_system_class = {
	.name = "kstreamer",
	.release = ks_system_class_release,
};
EXPORT_SYMBOL(ks_system_class);

#ifndef HAVE_CLASS_DEV_DEVT
static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	return print_dev_t(buf, ks_first_dev);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);
#endif

int ks_router_modinit()
{
	int err;

	err = alloc_chrdev_region(&ks_first_dev, 0, 1, ks_MODULE_NAME);
	if (err < 0)
		goto err_alloc_chrdev_region;

	cdev_init(&ks_router_cdev, &ks_router_fops);
	ks_router_cdev.owner = THIS_MODULE;

	err = cdev_add(&ks_router_cdev, ks_first_dev, 1);
	if (err < 0)
		goto err_cdev_add;

	err = class_register(&ks_system_class);
	if (err < 0)
		goto err_class_register;

	ks_router_control_class_dev.class = &ks_system_class;
	ks_router_control_class_dev.class_data = NULL;
#ifdef HAVE_CLASS_DEV_DEVT
	ks_router_control_class_dev.devt = ks_first_dev;
#endif
	snprintf(ks_router_control_class_dev.class_id,
		sizeof(ks_router_control_class_dev.class_id),
		"router-control");

	err = class_device_register(&ks_router_control_class_dev);
	if (err < 0)
		goto err_control_class_device_register;

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_create_file(
		&ks_router_control_class_dev,
		&class_device_attr_dev);
#endif

	return 0;

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&ks_router_control_class_dev,
		&class_device_attr_dev);
#endif

	class_device_del(&ks_router_control_class_dev);
err_control_class_device_register:
	class_unregister(&ks_system_class);
err_class_register:
	cdev_del(&ks_router_cdev);
err_cdev_add:
	unregister_chrdev_region(ks_first_dev, 1);
err_alloc_chrdev_region:

	return err;
}

void ks_router_modexit()
{
#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&ks_router_control_class_dev,
		&class_device_attr_dev);
#endif

	class_unregister(&ks_system_class);
	class_device_del(&ks_router_control_class_dev);
	cdev_del(&ks_router_cdev);
	unregister_chrdev_region(ks_first_dev, 1);
}
