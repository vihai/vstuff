/*
 * vISDN gateway between vISDN's switch and userland for stream access
 *
 * Copyright (C) 2005-2006 Daniele Orlandi
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

#include "userport.h"

#ifdef DEBUG_CODE
#ifdef DEBUG_DEFAULTS
int debug_level = 3;
#else
int debug_level = 0;
#endif
#endif

static dev_t vup_first_dev;

static struct cdev vup_cdev;
static struct class_device vup_stream_class_dev;
static struct class_device vup_frame_class_dev;

struct list_head vup_chans_list = LIST_HEAD_INIT(vup_chans_list);
DECLARE_RWSEM(vup_chans_list_sem);

static void vup_class_release(struct class_device *cd)
{
}

struct class vup_class = {
	.name = "ks_userport",
	.release = vup_class_release,
};
EXPORT_SYMBOL(vup_class);

static struct vup_chan *vup_chan_get(struct vup_chan *chan)
{
	if (ks_node_get(&chan->ks_node))
		return chan;
	else
		return NULL;
}

static void vup_chan_put(struct vup_chan *chan)
{
	ks_node_put(&chan->ks_node);
}

struct vup_chan *_vup_chan_search_by_id(int id)
{
	struct vup_chan *chan;
	list_for_each_entry(chan, &vup_chans_list, node) {
		if (chan->id == id)
			return chan;
	}

	return NULL;
}

struct vup_chan *vup_chan_get_by_id(int id)
{
	struct vup_chan *chan;

	down_read(&vup_chans_list_sem);
	chan = vup_chan_get(_vup_chan_search_by_id(id));
	up_read(&vup_chans_list_sem);

	return chan;
}


static int _vup_chan_new_id(void)
{
	static int cur_id;

	for (;;) {
		/* Maybe reusing userport ids would be better */

		if (++cur_id <= 0)
			cur_id = 1;

		if (!_vup_chan_search_by_id(cur_id))
			return cur_id;
	}
}

static void vup_node_release(struct ks_node *ks_node)
{
	struct vup_chan *chan = container_of(ks_node,
					struct vup_chan, ks_node);

	vup_debug(3, "vup_node_release()\n");

	kfree(chan);
}

static struct ks_node_ops vup_chan_node_ops = {
	.owner		= THIS_MODULE,

	.release	= vup_node_release,
};

/*---------------------------------------------------------------------------*/

static void vup_chan_rx_chan_release(struct ks_chan *ks_chan)
{
	vup_debug(3, "vup_chan_rx_chan_release()\n");

	kfree(ks_chan);
}

static int vup_chan_rx_chan_connect(struct ks_chan *ks_chan)
{
	vup_debug(3, "vup_chan_rx_chan_connect()\n");

	return 0;
}

static void vup_chan_rx_chan_disconnect(struct ks_chan *ks_chan)
{
	vup_debug(3, "vup_chan_rx_chan_disconnect()\n");
}

static int vup_chan_rx_chan_open(struct ks_chan *ks_chan)
{
	vup_debug(3, "vup_chan_rx_chan_open()\n");

	return 0;
}

static void vup_chan_rx_chan_close(struct ks_chan *ks_chan)
{
	vup_debug(3, "vup_chan_rx_chan_close()\n");
}

static void vup_timer_func(unsigned long data)
{
	struct vup_chan *chan = (struct vup_chan *)data;

	chan->stimulus_timer.expires += HZ / chan->stimulus_frequency;

	/* Some locking to access .pipeline??? XXX FIXME TODO */
	ks_pipeline_stimulate(chan->ks_chan_rx->pipeline);

	add_timer(&chan->stimulus_timer);
}

static int vup_chan_rx_chan_start(struct ks_chan *ks_chan)
{
	struct vup_chan *chan = ks_chan->driver_data;

	vup_debug(3, "vup_chan_rx_chan_start()\n");

	chan->stimulus_timer.expires = jiffies;
	chan->stimulus_timer.function = vup_timer_func;
	chan->stimulus_timer.data = (unsigned long)chan;

	add_timer(&chan->stimulus_timer);

	return 0;
}

static void vup_chan_rx_chan_stop(struct ks_chan *ks_chan)
{
	struct vup_chan *chan = ks_chan->driver_data;

	vup_debug(3, "vup_chan_rx_chan_stop()\n");

	del_timer_sync(&chan->stimulus_timer);
}

struct ks_chan_ops vup_chan_rx_chan_ops = {
	.owner		= THIS_MODULE,

	.release	= vup_chan_rx_chan_release,
	.connect	= vup_chan_rx_chan_connect,
	.disconnect	= vup_chan_rx_chan_disconnect,
	.open		= vup_chan_rx_chan_open,
	.close		= vup_chan_rx_chan_close,
	.start		= vup_chan_rx_chan_start,
	.stop		= vup_chan_rx_chan_stop,
};

/*---------------------------------------------------------------------------*/

static void vup_chan_tx_chan_release(struct ks_chan *ks_chan)
{
	vup_debug(3, "vup_chan_tx_chan_release()\n");

	kfree(ks_chan);
}

static int vup_chan_tx_chan_connect(struct ks_chan *ks_chan)
{
	vup_debug(3, "vup_chan_tx_chan_connect()\n");

	return 0;
}

static void vup_chan_tx_chan_disconnect(struct ks_chan *ks_chan)
{
	vup_debug(3, "vup_chan_tx_chan_disconnect()\n");
}

static int vup_chan_tx_chan_open(struct ks_chan *ks_chan)
{
	vup_debug(3, "vup_chan_tx_chan_open()\n");

	return 0;
}

static void vup_chan_tx_chan_close(struct ks_chan *ks_chan)
{
	vup_debug(3, "vup_chan_tx_chan_close()\n");
}

static int vup_chan_tx_chan_start(struct ks_chan *ks_chan)
{
	vup_debug(3, "vup_chan_tx_chan_start()\n");

	return 0;
}

static void vup_chan_tx_chan_stop(struct ks_chan *ks_chan)
{
	vup_debug(3, "vup_chan_tx_chan_stop()\n");
}

struct ks_chan_ops vup_chan_tx_chan_ops = {
	.owner		= THIS_MODULE,

	.release	= vup_chan_tx_chan_release,
	.connect	= vup_chan_tx_chan_connect,
	.disconnect	= vup_chan_tx_chan_disconnect,
	.open		= vup_chan_tx_chan_open,
	.close		= vup_chan_tx_chan_close,
	.start		= vup_chan_tx_chan_start,
	.stop		= vup_chan_tx_chan_stop,
};

/*---------------------------------------------------------------------------*/

/*
static inline void vup_chan_h223_put(struct vup_chan *chan, u8 c)
{
	switch(chan->h223_rx_state) {
	case VUP_H223_STATE_HUNTING1:
		if (c == 0xe1)
			chan->h223_rx_state = VUP_H223_STATE_HUNTING1;
	break;

	case VUP_H223_STATE_HUNTING2:
		if (c == 0x4d)
			chan->h223_rx_state = VUP_H223_STATE_READING_FRAME;
	break;

	case VUP_H223_STATE_READING_FRAME:
		if (c == 0xe1)
			chan->h223_rx_state = VUP_H223_STATE_CLOSING;
		else {
			if (chan->h223_rx_frame_pos ==
					sizeof(chan->h223_rx_frame_buf) - 1)
				chan->h223_rx_state = VUP_H223_STATE_DROPPING;
			else {
				chan->h223_frame_buf[chan->h223_frame_pos] = c;
				chan->h223_frame_pos++;
			}
		}
	break;

	case VUP_H223_STATE_CLOSING:
		if (c == 0x4d) {
			chan->h223_rx_state = VUP_H223_STATE_READING_FRAME;

			if (chan->h223_frame_pos) {
				struct sk_buff *skb;
				skb = alloc_skb(count, GFP_KERNEL);
				if (skb) {
					skb_queue_tail(&chan->read_queue, skb);
					wake_up(&chan->read_wait_queue);
				}
			}
		} else {
	break;

	case VUP_H223_STATE_DROPPING:
		if (c == 0xe1)
			chan->h223_rx_state = VUP_H223_STATE_CLOSING;
	break;
	}
}
*/

static int vup_chan_rx_chan_push_raw(
	struct ks_chan *ks_chan,
	struct ks_streamframe *sf)
{
	struct vup_chan *chan = ks_chan->driver_data;

#if 0
	if (chan->type == VISDN_LINK_FRAMING_H223A &&
	    ks_chan->pipeline->framing == VISDN_LINK_FRAMING_NONE) {

		/* H.223A framer */

		int i;
		for(i=0; i<sf->len; i++) {
			vup_chan_h223_put(chan, ((u8 *)data)[i]);
		}
	} else {
#endif
		if (__kfifo_put(chan->read_fifo, sf->data, sf->len))
			wake_up(&chan->read_wait_queue);
//	}


	return 0;
}

static int vup_chan_rx_chan_push_frame(
	struct ks_chan *ks_chan,
	struct sk_buff *skb)
{
	struct vup_chan *chan = ks_chan->driver_data;

	skb_queue_tail(&chan->read_queue, skb);
	wake_up(&chan->read_wait_queue);

	return 0;
}

struct vss_chan_ops vup_chan_rx_chan_node_ops =
{
	.push_frame	= vup_chan_rx_chan_push_frame,
	.push_raw	= vup_chan_rx_chan_push_raw,
};


/*static ssize_t vup_chan_read(
	struct ks_leg *ks_leg,
	void *buf, size_t count)
{
	struct vup_chan *chan = to_vup_chan(ks_leg->chan);

	return __kfifo_get(chan->tx_fifo, buf, count);
}

static ssize_t vup_chan_write(
	struct ks_leg *ks_leg,
	const void *buf, size_t count)

{
	struct vup_chan *chan = to_vup_chan(ks_leg->chan);

	return __kfifo_put(chan->read_fifo, (void *)buf, count);
}

static void vup_chan_rx_error(
	struct ks_leg *ks_leg,
	enum ks_leg_rx_error_code code)
{
}

static void vup_chan_tx_error(
	struct ks_leg *ks_leg,
	enum ks_leg_tx_error_code code)
{
}

static int vup_chan_connect(
	struct ks_leg *ks_leg1,
	struct ks_leg *ks_leg2)
{
	vup_debug(2, "Streamport %06d connected to %06d\n",
		ks_leg1->chan->id,
		ks_leg2->chan->id);

	return 0;
}

static void vup_chan_disconnect(
	struct ks_leg *ks_leg1,
	struct ks_leg *ks_leg2)
{
	vup_debug(2, "Streamport %06d disconnected from %06d\n",
		ks_leg1->chan->id,
		ks_leg2->chan->id);
}

static struct ks_chan_ops vup_chan_ops = {
	.owner		= THIS_MODULE,

	.release	= vup_chan_release,
	.open		= vup_chan_open,
	.close		= vup_chan_close,
};

static struct ks_leg_ops vup_leg_ops = {
	.owner		= THIS_MODULE,

	.connect	= vup_chan_connect,
	.disconnect	= vup_chan_disconnect,

	.read		= vup_chan_read,
	.write		= vup_chan_write,

	.rx_error	= vup_chan_rx_error,
	.tx_error	= vup_chan_tx_error,
};
*/
/*---------------------------------------------------------------------------*/

/*static ssize_t vup_show_read_fifo_usage(
	struct ks_chan *ks_chan,
	struct ks_chan_attribute *attr,
	char *buf)
{
	struct vup_chan *chan = to_vup_chan(ks_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		kfifo_len(chan->read_fifo));
}

static ssize_t vup_store_read_fifo_usage(
	struct ks_chan *ks_chan,
	struct ks_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct vup_chan *chan = to_vup_chan(ks_chan);

	kfifo_reset(chan->read_fifo);

	return count;
}

static VISDN_LINK_ATTR(read_fifo_usage, S_IRUGO | S_IWUSR,
		vup_show_read_fifo_usage,
		vup_store_read_fifo_usage);
*/
/*---------------------------------------------------------------------------*/
/*
static ssize_t vup_show_tx_fifo_usage(
	struct ks_chan *ks_chan,
	struct ks_chan_attribute *attr,
	char *buf)
{
	struct vup_chan *chan = to_vup_chan(ks_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		kfifo_len(chan->tx_fifo));
}

static ssize_t vup_store_tx_fifo_usage(
	struct ks_chan *ks_chan,
	struct ks_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct vup_chan *chan = to_vup_chan(ks_chan);

	kfifo_reset(chan->tx_fifo);

	return count;
}

static VISDN_LINK_ATTR(tx_fifo_usage, S_IRUGO | S_IWUSR,
		vup_show_tx_fifo_usage,
		vup_store_tx_fifo_usage);
*/

static void vup_chan_init(
	struct vup_chan *chan,
	int framed)
{
	memset(chan, 0, sizeof(*chan));

	init_timer(&chan->stimulus_timer);
	chan->stimulus_frequency = 50;
	chan->framed = framed;

        skb_queue_head_init(&chan->read_queue);
	init_waitqueue_head(&chan->read_wait_queue);

/*	spin_lock_init(&chan->tx_fifo_lock);
	chan->tx_fifo = kfifo_alloc(1024, GFP_KERNEL, &chan->tx_fifo_lock);
	if (!chan->tx_fifo) {
		err = -EFAULT;
		goto err_fifo_tx_alloc;
	}*/

	ks_node_init(&chan->ks_node, &vup_chan_node_ops, "",
			&ks_system_device.kobj);
}

static struct vup_chan *vup_chan_alloc(int framed)
{
	struct vup_chan *chan;

	chan = kmalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return NULL;

	vup_chan_init(chan, framed);

	return chan;
}

static int vup_chan_register(struct vup_chan *chan)
{
	int err;

	down_write(&vup_chans_list_sem);
	chan->id = _vup_chan_new_id();
	list_add_tail(&vup_chan_get(chan)->node,
		&vup_chans_list);
	up_write(&vup_chans_list_sem);

	kobject_set_name(&chan->ks_node.kobj, "%d", chan->id);

	spin_lock_init(&chan->read_fifo_lock);
	chan->read_fifo = kfifo_alloc(1024, GFP_KERNEL, &chan->read_fifo_lock);
	if (!chan->read_fifo) {
		err = -EFAULT;
		goto err_fifo_rx_alloc;
	}

	err = ks_node_register(&chan->ks_node);
	if (err < 0)
		goto err_node_register;

	if (chan->ks_chan_rx) {
		err = ks_chan_register(chan->ks_chan_rx);
		if (err < 0)
			goto err_chan_rx_register;
	}

	if (chan->ks_chan_tx) {
		err = ks_chan_register(chan->ks_chan_tx);
		if (err < 0)
			goto err_chan_tx_register;
	}

	return 0;

	if (chan->ks_chan_tx)
		ks_chan_unregister(chan->ks_chan_tx);
err_chan_tx_register:
	if (chan->ks_chan_rx)
		ks_chan_unregister(chan->ks_chan_rx);
err_chan_rx_register:
	ks_node_unregister(&chan->ks_node);
err_node_register:
	kfifo_free(chan->read_fifo);
err_fifo_rx_alloc:
	down_write(&vup_chans_list_sem);
	list_del(&chan->node);
	vup_chan_put(chan);
	up_write(&vup_chans_list_sem);

	return err;
}

static void vup_chan_unregister(struct vup_chan *chan)
{
	if (chan->ks_chan_tx)
		ks_chan_unregister(chan->ks_chan_tx);

	if (chan->ks_chan_rx)
		ks_chan_unregister(chan->ks_chan_rx);

	ks_node_unregister(&chan->ks_node);

	kfifo_free(chan->read_fifo);

	down_write(&vup_chans_list_sem);
	list_del(&chan->node);
	vup_chan_put(chan);
	up_write(&vup_chans_list_sem);
}

/*---------------------------------------------------------------------------*/

static int vup_cdev_open(
	struct inode *inode,
	struct file *file)
{
	int err;
	struct vup_chan *chan;

	nonseekable_open(inode, file);

	chan = vup_chan_alloc(inode->i_rdev - vup_first_dev == 1);
	if (!chan) {
		err = -ENOMEM;
		goto err_alloc_chan;
	}

	if ((file->f_flags & O_ACCMODE) == O_RDONLY ||
	    (file->f_flags & O_ACCMODE) == O_RDWR) {
		chan->ks_chan_rx = kmalloc(sizeof(*chan->ks_chan_rx),
						GFP_KERNEL);
		if (!chan->ks_chan_rx) {
			err = -ENOMEM;
			goto err_alloc_chan_rx;
		}
	
		ks_chan_init(chan->ks_chan_rx, &vup_chan_rx_chan_ops,
				"rx", NULL,
				&chan->ks_node.kobj,
				&vss_softswitch.ks_node,
				&chan->ks_node);

		chan->ks_chan_rx->driver_data = chan;
		chan->ks_chan_rx->from_ops = &vup_chan_rx_chan_node_ops;
/*		chan->ks_chan_rx->framed_mtu = -1;
		chan->ks_chan_rx->framing_avail = framing;*/
	}

	if ((file->f_flags & O_ACCMODE) == O_WRONLY ||
	    (file->f_flags & O_ACCMODE) == O_RDWR) {
		chan->ks_chan_tx = kmalloc(sizeof(*chan->ks_chan_tx),
						GFP_KERNEL);
		if (!chan->ks_chan_tx) {
			err = -ENOMEM;
			goto err_alloc_chan_tx;
		}
	
		ks_chan_init(chan->ks_chan_tx, &vup_chan_tx_chan_ops,
				"tx", NULL,
				&chan->ks_node.kobj,
				&chan->ks_node,
				&vss_softswitch.ks_node);

		chan->ks_chan_tx->driver_data = chan;
/*		chan->ks_chan_tx.framed_mtu = -1;
		chan->ks_chan_tx.framing_avail = framing;*/
	}

	err = vup_chan_register(chan);
       	if (err < 0)
		goto err_chan_register;

/*	err = ks_chan_create_file(&chan->ks_chan,
					&ks_chan_attr_read_fifo_usage);
	if (err < 0)
		goto err_create_file_read_fifo;

	err = ks_chan_create_file(&chan->ks_chan,
					&ks_chan_attr_tx_fifo_usage);
	if (err < 0)
		goto err_create_file_tx_fifo;*/

	file->private_data = vup_chan_get(chan);

	vup_debug(2, "Userport %06d opened\n", chan->id);

	vup_chan_put(chan);

	return 0;

/*	ks_chan_remove_file(&chan->ks_chan,
				&ks_chan_attr_tx_fifo_usage);
err_create_file_tx_fifo:
	ks_chan_remove_file(&chan->ks_chan,
				&ks_chan_attr_read_fifo_usage);
err_create_file_read_fifo:*/
/*	kfifo_free(chan->tx_fifo);
err_fifo_tx_alloc:
	kfifo_free(chan->read_fifo);
err_fifo_rx_alloc:*/

	if (chan->ks_chan_tx)
		ks_chan_put(chan->ks_chan_tx);
err_alloc_chan_tx:
	if (chan->ks_chan_rx)
		ks_chan_put(chan->ks_chan_rx);
err_alloc_chan_rx:
	vup_chan_unregister(chan);
err_chan_register:
	vup_chan_put(chan);
err_alloc_chan:

	return err;
}

static int vup_cdev_release(
	struct inode *inode, struct file *file)
{
	struct vup_chan *chan = file->private_data;

	vup_debug(3, "vup_cdev_release()\n");

	vup_chan_unregister(chan);

	if (chan->ks_chan_tx) {
		ks_chan_put(chan->ks_chan_tx);
		chan->ks_chan_tx = NULL;
	}

	if (chan->ks_chan_rx) {
		ks_chan_put(chan->ks_chan_rx);
		chan->ks_chan_rx = NULL;
	}

//	kfifo_free(chan->tx_fifo);
//	kfifo_free(chan->read_fifo);

	vup_chan_put(chan);
	file->private_data = NULL;

	return 0;
}

ssize_t __kfifo_get_user(
	struct kfifo *fifo,
	void __user *buffer,
	ssize_t len)
{
	ssize_t l;

	len = min(len, (ssize_t)(fifo->in - fifo->out));

	/* first get the data from fifo->out until the end of the buffer */
	l = min(len, (ssize_t)(fifo->size - (fifo->out & (fifo->size - 1))));
	if (copy_to_user(buffer, fifo->buffer + (fifo->out & (fifo->size - 1)), l))
		return -EFAULT;

	/* then get the rest (if any) from the beginning of the buffer */
	if (copy_to_user(buffer + l, fifo->buffer, len - l))
		return -EFAULT;

	fifo->out += len;

	return len;
}

static ssize_t vup_cdev_read(
	struct file *file,
	char __user *buf,
	size_t count,
	loff_t *offp)
{
	struct vup_chan *chan = file->private_data;
	int copied;

	if (!chan->ks_chan_rx)
		return -EBADF;

	if (!chan->ks_chan_rx->pipeline ||
	     chan->ks_chan_rx->pipeline->status != KS_PIPELINE_STATUS_FLOWING)
		return -ENOTCONN;

	if (chan->framed) {
		struct sk_buff *skb;

		skb = skb_dequeue(&chan->read_queue);
		if (!skb)
			return 0;

		copied = min(count, skb->len);

		if (copy_to_user(buf, skb->data, copied)) {
			kfree_skb(skb);
			return -EFAULT;
		}
	} else {
		copied = __kfifo_get_user(chan->read_fifo, buf, count);
		if (copied < 0)
			return -EFAULT;
	}

	return copied;
}

ssize_t __kfifo_put_user(
	struct kfifo *fifo,
	const void __user *buffer,
	ssize_t len)
{
	ssize_t l;

	len = min(len, (ssize_t)(fifo->size - fifo->in + fifo->out));

	/* first put the data starting from fifo->in to buffer end */
	l = min(len, (ssize_t)(fifo->size - (fifo->in & (fifo->size - 1))));
	if (copy_from_user(fifo->buffer + (fifo->in & (fifo->size - 1)), buffer, l))
		return -EFAULT;

	/* then put the rest (if any) at the beginning of the buffer */
	if (copy_from_user(fifo->buffer, buffer + l, len - l))
		return -EFAULT;

	fifo->in += len;

	return len;
}

static ssize_t vup_cdev_write_stream(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *offp)
{
	struct vup_chan *chan = file->private_data;
	struct ks_streamframe *sf;
	ssize_t copied_bytes;
	int err;

	if (!chan->ks_chan_tx) {
		err = -EBADF;
		goto err_no_write;
	}

	sf = ks_sf_alloc();
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

	err = vss_chan_push_raw(chan->ks_chan_tx, sf);
	if (err < 0)
		goto err_vss_chan_push_raw;

	ks_sf_put(sf);

	return copied_bytes;

err_vss_chan_push_raw:
err_copy_from_user:
	ks_sf_put(sf);
err_sf_alloc:
err_no_write:

	return err;
}

static ssize_t vup_cdev_write_frame(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *offp)
{
	struct vup_chan *chan = file->private_data;
	struct sk_buff *skb;
	int res;
	int err;

	if (!chan->ks_chan_tx) {
		err = -EBADF;
		goto err_no_write;
	}

	skb = alloc_skb(count, GFP_KERNEL);
	if (!skb) {
		err = -ENOMEM;
		goto err_alloc_skb;
	}

	if (copy_from_user(skb->data, buf, count)) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	res = vss_chan_push_frame(chan->ks_chan_tx, skb);
	switch(res) {
	case KS_TX_OK:
		return count;
	case KS_TX_BUSY:
	case KS_TX_LOCKED:
		kfree_skb(skb);
		return -EBUSY;
	}

	return -EINVAL;

err_copy_from_user:
	kfree_skb(skb);
err_alloc_skb:
err_no_write:

	return err;
}

static ssize_t vup_cdev_write(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *offp)
{
	struct vup_chan *chan = file->private_data;

	if (!chan->ks_chan_tx)
		return -EBADF;

	if (!chan->ks_chan_tx->pipeline ||
	     chan->ks_chan_tx->pipeline->status != KS_PIPELINE_STATUS_FLOWING)
		return -ENOTCONN;

	if (chan->framed)
		return vup_cdev_write_frame(file, buf, count, offp);
	else
		return vup_cdev_write_stream(file, buf, count, offp);
}

/*static void ks_make_kobj_path(
	struct kobject *kobj, char *path, int max_length)
{
	struct kobject *cur;
	int length = 0;
	int pos;

	for (cur = kobj; cur; cur = cur->parent)
		length += strlen(kobject_name(cur)) + 1;

	if (length >= max_length) {
		path[0] = '\0';
		return;
	}

	pos = length;
	path[length] = '\0';

	for (cur = kobj; cur; cur = cur->parent) {

		int len = strlen(kobject_name(cur));

		pos -= len;

		memcpy(path + pos, kobject_name(cur), len);
		*(path + --pos) = '/';
	}
}*/

static int vup_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct vup_chan *chan = file->private_data;

	switch(cmd) {
	case KS_UP_GET_NODEID: {
		return put_user(chan->ks_node.id, (int __user *)arg);
	}
	break;

	case KS_UP_GET_PRESSURE: {
		int pressure;

		if (chan->ks_chan_tx)
			pressure = vss_chan_get_pressure(chan->ks_chan_tx);
		else
			pressure = 0;

		return put_user(pressure, (int __user *)arg);
	}
	break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static unsigned int vup_cdev_poll(
	struct file *file,
	poll_table *wait)
{
	struct vup_chan *chan = file->private_data;

	BUG_ON(!file->private_data);

	poll_wait(file, &chan->read_wait_queue, wait);

	if (chan->ks_chan_rx) {
		if (chan->framed) {
			if (!skb_queue_empty(&chan->read_queue))
				return POLLIN | POLLRDNORM;
		} else {
			if (kfifo_len(chan->read_fifo))
				return POLLIN | POLLRDNORM;
		}
	}

	// TODO FIXME XXX XXX Implement outbound waiting!!!!!!!!!!!!!!

	return 0;
}

static struct file_operations vup_fops =
{
	.owner		= THIS_MODULE,
	.read		= vup_cdev_read,
	.write		= vup_cdev_write,
	.ioctl		= vup_cdev_ioctl,
	.open		= vup_cdev_open,
	.release	= vup_cdev_release,
	.llseek		= no_llseek,
	.poll		= vup_cdev_poll,
};

#ifndef HAVE_CLASS_DEV_DEVT
static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	if (!strcmp(class_dev->class_id, "userport_stream"))
		return print_dev_t(buf, vup_first_dev);
	else
		return print_dev_t(buf, vup_first_dev + 1);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);
#endif

static int __init vup_init_module(void)
{
	int err;

	vup_msg(KERN_INFO, vup_MODULE_DESCR " loading\n");

	err = class_register(&vup_class);
	if (err < 0)
		goto err_class_register;

	err = alloc_chrdev_region(&vup_first_dev, 0, 3, vup_MODULE_NAME);
	if (err < 0)
		goto err_register_chrdev;

	cdev_init(&vup_cdev, &vup_fops);
	vup_cdev.owner = THIS_MODULE;

	err = cdev_add(&vup_cdev, vup_first_dev, 3);
	if (err < 0)
		goto err_cdev_add;

	/* Stream */
	snprintf(vup_stream_class_dev.class_id,
		sizeof(vup_stream_class_dev.class_id),
		"userport_stream");
	vup_stream_class_dev.class = &vup_class;
	vup_stream_class_dev.dev = NULL;
#ifdef HAVE_CLASS_DEV_DEVT
	vup_stream_class_dev.devt = vup_first_dev;
#endif

	err = class_device_register(&vup_stream_class_dev);
	if (err < 0)
		goto err_stream_class_device_register;

#ifndef HAVE_CLASS_DEV_DEVT
	err = class_device_create_file(
		&vup_stream_class_dev,
		&class_device_attr_dev);
	if (err < 0)
		goto err_stream_class_device_create_file;
#endif

	/* Frame */
	snprintf(vup_frame_class_dev.class_id,
		sizeof(vup_frame_class_dev.class_id),
		"userport_frame");
	vup_frame_class_dev.class = &vup_class;
	vup_frame_class_dev.dev = NULL;
#ifdef HAVE_CLASS_DEV_DEVT
	vup_frame_class_dev.devt = vup_first_dev + 1;
#endif

	err = class_device_register(&vup_frame_class_dev);
	if (err < 0)
		goto err_frame_class_device_register;

#ifndef HAVE_CLASS_DEV_DEVT
	err = class_device_create_file(
		&vup_frame_class_dev,
		&class_device_attr_dev);
	if (err < 0)
		goto err_frame_class_device_create_file;
#endif

	return 0;

	class_device_unregister(&vup_frame_class_dev);
err_frame_class_device_register:
#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&vup_frame_class_dev,
		&class_device_attr_dev);
err_frame_class_device_create_file:
#endif
	class_device_unregister(&vup_stream_class_dev);
err_stream_class_device_register:
#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&vup_stream_class_dev,
		&class_device_attr_dev);
err_stream_class_device_create_file:
#endif
	cdev_del(&vup_cdev);
err_cdev_add:
	unregister_chrdev_region(vup_first_dev, 3);
err_register_chrdev:
	class_unregister(&vup_class);
err_class_register:

	return err;
}

module_init(vup_init_module);

static void __exit vup_module_exit(void)
{
#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&vup_frame_class_dev,
		&class_device_attr_dev);
#endif

	class_device_unregister(&vup_frame_class_dev);

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&vup_stream_class_dev,
		&class_device_attr_dev);
#endif

	class_device_unregister(&vup_stream_class_dev);

	cdev_del(&vup_cdev);
	unregister_chrdev_region(vup_first_dev, 3);

	class_unregister(&vup_class);

	vup_msg(KERN_INFO, vup_MODULE_DESCR " unloaded\n");
}

module_exit(vup_module_exit);

MODULE_DESCRIPTION(vup_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

#ifdef DEBUG_CODE
module_param(debug_level, int, 0444);
MODULE_PARM_DESC(debug_level, "Initial debug level");
#endif
