/*
 * vISDN gateway between vISDN's switch and userland for stream access
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/device.h>
#include <linux/list.h>

#include <linux/kstreamer/kstreamer.h>
#include <linux/kstreamer/node.h>
#include <linux/kstreamer/pipeline.h>
#include <linux/kstreamer/streamframe.h>
#include <linux/kstreamer/softswitch.h>

//#include <linux/visdn/visdn.h>

#include "milliwatt.h"

#ifdef DEBUG_CODE
#ifdef DEBUG_DEFAULTS
int debug_level = 3;
#else
int debug_level = 0;
#endif
#endif

struct list_head vmw_chans_list = LIST_HEAD_INIT(vmw_chans_list);
DECLARE_RWSEM(vmw_chans_list_sem);

static struct vmw_chan *vmw_chan_get(struct vmw_chan *chan)
{
	if (ks_node_get(&chan->ks_node))
		return chan;
	else
		return NULL;
}

static void vmw_chan_put(struct vmw_chan *chan)
{
	ks_node_put(&chan->ks_node);
}


struct vmw_chan *_vmw_chan_search_by_id(int id)
{
	struct vmw_chan *chan;
	list_for_each_entry(chan, &vmw_chans_list, node) {
		if (chan->id == id)
			return chan;
	}

	return NULL;
}

struct vmw_chan *vmw_chan_get_by_id(int id)
{
	struct vmw_chan *chan;

	down_read(&vmw_chans_list_sem);
	chan = vmw_chan_get(_vmw_chan_search_by_id(id));
	up_read(&vmw_chans_list_sem);

	return chan;
}


static int _vmw_chan_new_id(void)
{
	static int cur_id;

	for (;;) {
		/* Maybe reusing milliwatt ids would be better */

		if (++cur_id <= 0)
			cur_id = 1;

		if (!_vmw_chan_search_by_id(cur_id))
			return cur_id;
	}
}


static void vmw_node_release(struct ks_node *ks_node)
{
	struct vmw_chan *chan = container_of(ks_node,
					struct vmw_chan, ks_node);

	vmw_debug(3, "vmw_node_release()\n");

	vmw_chan_put(chan);
}

static struct ks_node_ops vmw_chan_node_ops = {
	.owner		= THIS_MODULE,

	.release	= vmw_node_release,
};

/*---------------------------------------------------------------------------*/

static void vmw_chan_tx_chan_release(struct ks_chan *ks_chan)
{
	vmw_debug(3, "vmw_chan_tx_chan_connect()\n");
}

static int vmw_chan_tx_chan_connect(struct ks_chan *ks_chan)
{
	vmw_debug(3, "vmw_chan_tx_chan_connect()\n");

	return 0;
}

static void vmw_chan_tx_chan_disconnect(struct ks_chan *ks_chan)
{
	vmw_debug(3, "vmw_chan_tx_chan_disconnect()\n");
}

static int vmw_chan_tx_chan_open(struct ks_chan *ks_chan)
{
	vmw_debug(3, "vmw_chan_tx_chan_open()\n");

	return 0;
}

static void vmw_chan_tx_chan_close(struct ks_chan *ks_chan)
{
	vmw_debug(3, "vmw_chan_tx_chan_close()\n");
}

static int vmw_chan_tx_chan_start(struct ks_chan *ks_chan)
{
	vmw_debug(3, "vmw_chan_tx_chan_start()\n");

	return 0;
}

static void vmw_chan_tx_chan_stop(struct ks_chan *ks_chan)
{
	vmw_debug(3, "vmw_chan_tx_chan_stop()\n");
}

struct ks_chan_ops vmw_chan_tx_chan_ops = {
	.owner		= THIS_MODULE,

	.release	= vmw_chan_tx_chan_release,
	.connect	= vmw_chan_tx_chan_connect,
	.disconnect	= vmw_chan_tx_chan_disconnect,
	.open		= vmw_chan_tx_chan_open,
	.close		= vmw_chan_tx_chan_close,
	.start		= vmw_chan_tx_chan_start,
	.stop		= vmw_chan_tx_chan_stop,
};

/*---------------------------------------------------------------------------*/

static void vmw_chan_init(struct vmw_chan *chan)
{
	memset(chan, 0, sizeof(*chan));

	ks_node_init(&chan->ks_node, &vmw_chan_node_ops, "milliwatt",
			&ks_system_device.kobj);

	ks_chan_init(&chan->ks_chan, &vmw_chan_tx_chan_ops, "tx", NULL,
			&chan->ks_node.kobj,
			&chan->ks_node,
			&vss_softswitch.ks_node);

/*	chan->ks_chan.framed_mtu = -1;
	chan->ks_chan.framing_avail = VISDN_LINK_FRAMING_NONE;*/
}

static int vmw_chan_register(struct vmw_chan *chan)
{
	int err;

	down_write(&vmw_chans_list_sem);
	chan->id = _vmw_chan_new_id();
	list_add_tail(&vmw_chan_get(chan)->node,
		&vmw_chans_list);
	up_write(&vmw_chans_list_sem);

	err = ks_node_register(&chan->ks_node);
	if (err < 0)
		goto err_node_register;

	vmw_chan_get(chan);
	err = ks_chan_register(&chan->ks_chan);
	if (err < 0)
		goto err_chan_register;

	return 0;

	ks_chan_unregister(&chan->ks_chan);
err_chan_register:
	ks_node_unregister(&chan->ks_node);
err_node_register:
	down_write(&vmw_chans_list_sem);
	list_del(&chan->node);
	vmw_chan_put(chan);
	up_write(&vmw_chans_list_sem);

	return err;
}

static void vmw_chan_unregister(struct vmw_chan *chan)
{
	ks_chan_unregister(&chan->ks_chan);
	ks_node_unregister(&chan->ks_node);

	down_write(&vmw_chans_list_sem);
	list_del(&chan->node);
	vmw_chan_put(chan);
	up_write(&vmw_chans_list_sem);
}

/*---------------------------------------------------------------------------*/

/*static ssize_t vmw_cdev_write(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *offp)
{
	struct vmw_chan *chan = file->private_data;
	struct ks_streamframe *sf;
	ssize_t copied_bytes;
	int err;

	sf = visdn_sf_alloc();
	if (!sf) {
		err = -ENOMEM;
		goto err_sf_alloc;
	}

	copied_bytes = min(count, (size_t)sf->size);

	if (copy_from_user(sf->data, buf, copied_bytes)) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	sf->len = copied_bytes;

	err = vss_chan_push_raw(&chan->ks_chan, sf);
	if (err < 0)
		goto err_vss_chan_push_raw;

	visdn_sf_put(sf);

	return copied_bytes;

err_vss_chan_push_raw:
err_copy_from_user:
	visdn_sf_put(sf);
err_sf_alloc:

	return err;
}*/

/******************************************
 * Module stuff
 ******************************************/

struct vmw_chan *chan;

static int __init vmw_init_module(void)
{
	int err;

	vmw_msg(KERN_INFO, vmw_MODULE_DESCR " loading\n");

	chan = kmalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan) {
		err = -ENOMEM;
		goto err_alloc_chan;
	}

	vmw_chan_init(chan);

	err = vmw_chan_register(chan);
       	if (err < 0)
		goto err_chan_register;

	return 0;

	vmw_chan_unregister(chan);
err_chan_register:
	vmw_chan_put(chan);
err_alloc_chan:

	return err;
}

module_init(vmw_init_module);

static void __exit vmw_module_exit(void)
{
	vmw_chan_unregister(chan);
	vmw_chan_put(chan);

	vmw_msg(KERN_INFO, vmw_MODULE_DESCR " unloaded\n");
}

module_exit(vmw_module_exit);

MODULE_DESCRIPTION(vmw_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

#ifdef DEBUG_CODE
module_param(debug_level, int, 0444);
MODULE_PARM_DESC(debug_level, "Initial debug level");
#endif
