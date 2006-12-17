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

static dev_t ksup_first_dev;

static struct cdev ksup_cdev;
static struct class_device ksup_stream_class_dev;
static struct class_device ksup_frame_class_dev;

struct list_head ksup_chans_list = LIST_HEAD_INIT(ksup_chans_list);
DECLARE_RWSEM(ksup_chans_list_sem);

static void ksup_class_release(struct class_device *cd)
{
}

struct class ksup_class = {
	.name = "ks_userport",
	.release = ksup_class_release,
};
EXPORT_SYMBOL(ksup_class);

static struct ksup_chan *ksup_chan_get(struct ksup_chan *chan)
{
	if (ks_node_get(&chan->ks_node))
		return chan;
	else
		return NULL;
}

static void ksup_chan_put(struct ksup_chan *chan)
{
	ks_node_put(&chan->ks_node);
}

struct ksup_chan *_ksup_chan_search_by_id(int id)
{
	struct ksup_chan *chan;
	list_for_each_entry(chan, &ksup_chans_list, node) {
		if (chan->id == id)
			return chan;
	}

	return NULL;
}

struct ksup_chan *ksup_chan_get_by_id(int id)
{
	struct ksup_chan *chan;

	down_read(&ksup_chans_list_sem);
	chan = ksup_chan_get(_ksup_chan_search_by_id(id));
	up_read(&ksup_chans_list_sem);

	return chan;
}


static int _ksup_chan_new_id(void)
{
	static int cur_id;

	for (;;) {
		/* Maybe reusing userport ids would be better */

		if (++cur_id <= 0)
			cur_id = 1;

		if (!_ksup_chan_search_by_id(cur_id))
			return cur_id;
	}
}

static void ksup_node_release(struct ks_node *ks_node)
{
	struct ksup_chan *chan = container_of(ks_node,
					struct ksup_chan, ks_node);

	ksup_debug(3, "ksup_node_release()\n");

	kfree(chan);
}

static struct ks_node_ops ksup_chan_node_ops = {
	.owner		= THIS_MODULE,

	.release	= ksup_node_release,
};

/*---------------------------------------------------------------------------*/

static void ksup_chan_rx_chan_release(struct ks_chan *ks_chan)
{
	ksup_debug(3, "ksup_chan_rx_chan_release()\n");

	kfree(ks_chan);
}

static int ksup_chan_rx_chan_connect(struct ks_chan *ks_chan)
{
	ksup_debug(3, "ksup_chan_rx_chan_connect()\n");

	return 0;
}

static void ksup_chan_rx_chan_disconnect(struct ks_chan *ks_chan)
{
	ksup_debug(3, "ksup_chan_rx_chan_disconnect()\n");
}

static int ksup_chan_rx_chan_open(struct ks_chan *ks_chan)
{
	ksup_debug(3, "ksup_chan_rx_chan_open()\n");

	return 0;
}

static void ksup_chan_rx_chan_close(struct ks_chan *ks_chan)
{
	ksup_debug(3, "ksup_chan_rx_chan_close()\n");
}

static void ksup_timer_func(unsigned long data)
{
	struct ksup_chan *chan = (struct ksup_chan *)data;

	chan->stimulus_timer.expires += HZ / chan->stimulus_frequency;

	/* Some locking to access .pipeline??? XXX FIXME TODO */
	ks_pipeline_stimulate(chan->ks_chan_rx->pipeline);

	add_timer(&chan->stimulus_timer);
}

static int ksup_chan_rx_chan_start(struct ks_chan *ks_chan)
{
	struct ksup_chan *chan = ks_chan->driver_data;

	ksup_debug(3, "ksup_chan_rx_chan_start()\n");

	chan->stimulus_timer.expires = jiffies;
	chan->stimulus_timer.function = ksup_timer_func;
	chan->stimulus_timer.data = (unsigned long)chan;

	add_timer(&chan->stimulus_timer);

	return 0;
}

static void ksup_chan_rx_chan_stop(struct ks_chan *ks_chan)
{
	struct ksup_chan *chan = ks_chan->driver_data;

	ksup_debug(3, "ksup_chan_rx_chan_stop()\n");

	del_timer_sync(&chan->stimulus_timer);
}

struct ks_chan_ops ksup_chan_rx_chan_ops = {
	.owner		= THIS_MODULE,

	.release	= ksup_chan_rx_chan_release,
	.connect	= ksup_chan_rx_chan_connect,
	.disconnect	= ksup_chan_rx_chan_disconnect,
	.open		= ksup_chan_rx_chan_open,
	.close		= ksup_chan_rx_chan_close,
	.start		= ksup_chan_rx_chan_start,
	.stop		= ksup_chan_rx_chan_stop,
};

/*---------------------------------------------------------------------------*/

static void ksup_chan_tx_chan_release(struct ks_chan *ks_chan)
{
	ksup_debug(3, "ksup_chan_tx_chan_release()\n");

	kfree(ks_chan);
}

static int ksup_chan_tx_chan_connect(struct ks_chan *ks_chan)
{
	ksup_debug(3, "ksup_chan_tx_chan_connect()\n");

	return 0;
}

static void ksup_chan_tx_chan_disconnect(struct ks_chan *ks_chan)
{
	ksup_debug(3, "ksup_chan_tx_chan_disconnect()\n");
}

static int ksup_chan_tx_chan_open(struct ks_chan *ks_chan)
{
	ksup_debug(3, "ksup_chan_tx_chan_open()\n");

	return 0;
}

static void ksup_chan_tx_chan_close(struct ks_chan *ks_chan)
{
	ksup_debug(3, "ksup_chan_tx_chan_close()\n");
}

static int ksup_chan_tx_chan_start(struct ks_chan *ks_chan)
{
	ksup_debug(3, "ksup_chan_tx_chan_start()\n");

	return 0;
}

static void ksup_chan_tx_chan_stop(struct ks_chan *ks_chan)
{
	ksup_debug(3, "ksup_chan_tx_chan_stop()\n");
}

struct ks_chan_ops ksup_chan_tx_chan_ops = {
	.owner		= THIS_MODULE,

	.release	= ksup_chan_tx_chan_release,
	.connect	= ksup_chan_tx_chan_connect,
	.disconnect	= ksup_chan_tx_chan_disconnect,
	.open		= ksup_chan_tx_chan_open,
	.close		= ksup_chan_tx_chan_close,
	.start		= ksup_chan_tx_chan_start,
	.stop		= ksup_chan_tx_chan_stop,
};

/*---------------------------------------------------------------------------*/

/*
static inline void ksup_chan_h223_put(struct ksup_chan *chan, u8 c)
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

static int ksup_chan_rx_chan_push_raw(
	struct ks_chan *ks_chan,
	struct ks_streamframe *sf)
{
	struct ksup_chan *chan = ks_chan->driver_data;

#if 0
	if (chan->type == VISDN_LINK_FRAMING_H223A &&
	    ks_chan->pipeline->framing == VISDN_LINK_FRAMING_NONE) {

		/* H.223A framer */

		int i;
		for(i=0; i<sf->len; i++) {
			ksup_chan_h223_put(chan, ((u8 *)data)[i]);
		}
	} else {
#endif
		if (__kfifo_put(chan->read_fifo, sf->data, sf->len))
			wake_up(&chan->read_wait_queue);
//	}


	return 0;
}

static int ksup_chan_rx_chan_push_frame(
	struct ks_chan *ks_chan,
	struct sk_buff *skb)
{
	struct ksup_chan *chan = ks_chan->driver_data;

	skb_queue_tail(&chan->read_queue, skb);
	wake_up(&chan->read_wait_queue);

	return 0;
}

struct kss_chan_from_ops ksup_chan_rx_chan_node_ops =
{
	.push_frame	= ksup_chan_rx_chan_push_frame,
	.push_raw	= ksup_chan_rx_chan_push_raw,
};


/*static ssize_t ksup_chan_read(
	struct ks_leg *ks_leg,
	void *buf, size_t count)
{
	struct ksup_chan *chan = to_ksup_chan(ks_leg->chan);

	return __kfifo_get(chan->tx_fifo, buf, count);
}

static ssize_t ksup_chan_write(
	struct ks_leg *ks_leg,
	const void *buf, size_t count)

{
	struct ksup_chan *chan = to_ksup_chan(ks_leg->chan);

	return __kfifo_put(chan->read_fifo, (void *)buf, count);
}

static void ksup_chan_rx_error(
	struct ks_leg *ks_leg,
	enum ks_leg_rx_error_code code)
{
}

static void ksup_chan_tx_error(
	struct ks_leg *ks_leg,
	enum ks_leg_tx_error_code code)
{
}

static int ksup_chan_connect(
	struct ks_leg *ks_leg1,
	struct ks_leg *ks_leg2)
{
	ksup_debug(2, "Streamport %06d connected to %06d\n",
		ks_leg1->chan->id,
		ks_leg2->chan->id);

	return 0;
}

static void ksup_chan_disconnect(
	struct ks_leg *ks_leg1,
	struct ks_leg *ks_leg2)
{
	ksup_debug(2, "Streamport %06d disconnected from %06d\n",
		ks_leg1->chan->id,
		ks_leg2->chan->id);
}

static struct ks_chan_ops ksup_chan_ops = {
	.owner		= THIS_MODULE,

	.release	= ksup_chan_release,
	.open		= ksup_chan_open,
	.close		= ksup_chan_close,
};

static struct ks_leg_ops ksup_leg_ops = {
	.owner		= THIS_MODULE,

	.connect	= ksup_chan_connect,
	.disconnect	= ksup_chan_disconnect,

	.read		= ksup_chan_read,
	.write		= ksup_chan_write,

	.rx_error	= ksup_chan_rx_error,
	.tx_error	= ksup_chan_tx_error,
};
*/
/*---------------------------------------------------------------------------*/

/*static ssize_t ksup_show_read_fifo_usage(
	struct ks_chan *ks_chan,
	struct ks_chan_attribute *attr,
	char *buf)
{
	struct ksup_chan *chan = to_ksup_chan(ks_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		kfifo_len(chan->read_fifo));
}

static ssize_t ksup_store_read_fifo_usage(
	struct ks_chan *ks_chan,
	struct ks_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct ksup_chan *chan = to_ksup_chan(ks_chan);

	kfifo_reset(chan->read_fifo);

	return count;
}

static VISDN_LINK_ATTR(read_fifo_usage, S_IRUGO | S_IWUSR,
		ksup_show_read_fifo_usage,
		ksup_store_read_fifo_usage);
*/
/*---------------------------------------------------------------------------*/
/*
static ssize_t ksup_show_tx_fifo_usage(
	struct ks_chan *ks_chan,
	struct ks_chan_attribute *attr,
	char *buf)
{
	struct ksup_chan *chan = to_ksup_chan(ks_chan);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		kfifo_len(chan->tx_fifo));
}

static ssize_t ksup_store_tx_fifo_usage(
	struct ks_chan *ks_chan,
	struct ks_chan_attribute *attr,
	const char *buf,
	size_t count)
{
	struct ksup_chan *chan = to_ksup_chan(ks_chan);

	kfifo_reset(chan->tx_fifo);

	return count;
}

static VISDN_LINK_ATTR(tx_fifo_usage, S_IRUGO | S_IWUSR,
		ksup_show_tx_fifo_usage,
		ksup_store_tx_fifo_usage);
*/

static void ksup_chan_init(
	struct ksup_chan *chan,
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

	ks_node_init(&chan->ks_node, &ksup_chan_node_ops, "",
			&ks_system_device.kobj);
}

static struct ksup_chan *ksup_chan_alloc(int framed)
{
	struct ksup_chan *chan;

	chan = kmalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return NULL;

	ksup_chan_init(chan, framed);

	return chan;
}

static int ksup_chan_register(struct ksup_chan *chan)
{
	int err;

	down_write(&ksup_chans_list_sem);
	chan->id = _ksup_chan_new_id();
	list_add_tail(&ksup_chan_get(chan)->node,
		&ksup_chans_list);
	up_write(&ksup_chans_list_sem);

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
	down_write(&ksup_chans_list_sem);
	list_del(&chan->node);
	ksup_chan_put(chan);
	up_write(&ksup_chans_list_sem);

	return err;
}

static void ksup_chan_unregister(struct ksup_chan *chan)
{
	if (chan->ks_chan_tx)
		ks_chan_unregister(chan->ks_chan_tx);

	if (chan->ks_chan_rx)
		ks_chan_unregister(chan->ks_chan_rx);

	ks_node_unregister(&chan->ks_node);

	kfifo_free(chan->read_fifo);

	down_write(&ksup_chans_list_sem);
	list_del(&chan->node);
	ksup_chan_put(chan);
	up_write(&ksup_chans_list_sem);
}

/*---------------------------------------------------------------------------*/

static int ksup_cdev_open(
	struct inode *inode,
	struct file *file)
{
	int err;
	struct ksup_chan *chan;

	nonseekable_open(inode, file);

	chan = ksup_chan_alloc(inode->i_rdev - ksup_first_dev == 1);
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

		ks_chan_init(chan->ks_chan_rx, &ksup_chan_rx_chan_ops,
				"rx", NULL,
				&chan->ks_node.kobj,
				&kss_softswitch.ks_node,
				&chan->ks_node);

		chan->ks_chan_rx->driver_data = chan;
		chan->ks_chan_rx->from_ops = &ksup_chan_rx_chan_node_ops;
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

		ks_chan_init(chan->ks_chan_tx, &ksup_chan_tx_chan_ops,
				"tx", NULL,
				&chan->ks_node.kobj,
				&chan->ks_node,
				&kss_softswitch.ks_node);

		chan->ks_chan_tx->driver_data = chan;
/*		chan->ks_chan_tx.framed_mtu = -1;
		chan->ks_chan_tx.framing_avail = framing;*/
	}

	err = ksup_chan_register(chan);
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

	file->private_data = ksup_chan_get(chan);

	ksup_debug(2, "Userport %06d opened\n", chan->id);

	ksup_chan_put(chan);

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
	ksup_chan_unregister(chan);
err_chan_register:
	ksup_chan_put(chan);
err_alloc_chan:

	return err;
}

static int ksup_cdev_release(
	struct inode *inode, struct file *file)
{
	struct ksup_chan *chan = file->private_data;

	ksup_debug(3, "ksup_cdev_release()\n");

	ksup_chan_unregister(chan);

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

	ksup_chan_put(chan);
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
	if (copy_to_user(buffer, fifo->buffer + (fifo->out & (fifo->size - 1)),
									l))
		return -EFAULT;

	/* then get the rest (if any) from the beginning of the buffer */
	if (copy_to_user(buffer + l, fifo->buffer, len - l))
		return -EFAULT;

	fifo->out += len;

	return len;
}

static ssize_t ksup_cdev_read(
	struct file *file,
	char __user *buf,
	size_t count,
	loff_t *offp)
{
	struct ksup_chan *chan = file->private_data;
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
	if (copy_from_user(fifo->buffer + (fifo->in & (fifo->size - 1)),
								buffer, l))
		return -EFAULT;

	/* then put the rest (if any) at the beginning of the buffer */
	if (copy_from_user(fifo->buffer, buffer + l, len - l))
		return -EFAULT;

	fifo->in += len;

	return len;
}

static ssize_t ksup_cdev_write_stream(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *offp)
{
	struct ksup_chan *chan = file->private_data;
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

	err = kss_chan_push_raw(chan->ks_chan_tx, sf);
	if (err < 0)
		goto err_kss_chan_push_raw;

	ks_sf_put(sf);

	return copied_bytes;

err_kss_chan_push_raw:
err_copy_from_user:
	ks_sf_put(sf);
err_sf_alloc:
err_no_write:

	return err;
}

static ssize_t ksup_cdev_write_frame(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *offp)
{
	struct ksup_chan *chan = file->private_data;
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

	res = kss_chan_push_frame(chan->ks_chan_tx, skb);
	switch(res) {
	case KSS_TX_OK:
		return count;
	case KSS_TX_BUSY:
	case KSS_TX_LOCKED:
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

static ssize_t ksup_cdev_write(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *offp)
{
	struct ksup_chan *chan = file->private_data;

	if (!chan->ks_chan_tx)
		return -EBADF;

	if (!chan->ks_chan_tx->pipeline ||
	     chan->ks_chan_tx->pipeline->status != KS_PIPELINE_STATUS_FLOWING)
		return -ENOTCONN;

	if (chan->framed)
		return ksup_cdev_write_frame(file, buf, count, offp);
	else
		return ksup_cdev_write_stream(file, buf, count, offp);
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

static int ksup_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct ksup_chan *chan = file->private_data;

	switch(cmd) {
	case KS_UP_GET_NODEID: {
		return put_user(chan->ks_node.id, (int __user *)arg);
	}
	break;

	case KS_UP_GET_PRESSURE: {
		int pressure;

		if (chan->ks_chan_tx)
			pressure = kss_chan_get_pressure(chan->ks_chan_tx);
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

static unsigned int ksup_cdev_poll(
	struct file *file,
	poll_table *wait)
{
	struct ksup_chan *chan = file->private_data;

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

static struct file_operations ksup_fops =
{
	.owner		= THIS_MODULE,
	.read		= ksup_cdev_read,
	.write		= ksup_cdev_write,
	.ioctl		= ksup_cdev_ioctl,
	.open		= ksup_cdev_open,
	.release	= ksup_cdev_release,
	.llseek		= no_llseek,
	.poll		= ksup_cdev_poll,
};

#ifndef HAVE_CLASS_DEV_DEVT
static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	if (!strcmp(class_dev->class_id, "userport_stream"))
		return print_dev_t(buf, ksup_first_dev);
	else
		return print_dev_t(buf, ksup_first_dev + 1);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);
#endif

static int __init ksup_init_module(void)
{
	int err;

	ksup_msg(KERN_INFO, ksup_MODULE_DESCR " loading\n");

	err = class_register(&ksup_class);
	if (err < 0)
		goto err_class_register;

	err = alloc_chrdev_region(&ksup_first_dev, 0, 3, ksup_MODULE_NAME);
	if (err < 0)
		goto err_register_chrdev;

	cdev_init(&ksup_cdev, &ksup_fops);
	ksup_cdev.owner = THIS_MODULE;

	err = cdev_add(&ksup_cdev, ksup_first_dev, 3);
	if (err < 0)
		goto err_cdev_add;

	/* Stream */
	snprintf(ksup_stream_class_dev.class_id,
		sizeof(ksup_stream_class_dev.class_id),
		"userport_stream");
	ksup_stream_class_dev.class = &ksup_class;
	ksup_stream_class_dev.dev = NULL;
#ifdef HAVE_CLASS_DEV_DEVT
	ksup_stream_class_dev.devt = ksup_first_dev;
#endif

	err = class_device_register(&ksup_stream_class_dev);
	if (err < 0)
		goto err_stream_class_device_register;

#ifndef HAVE_CLASS_DEV_DEVT
	err = class_device_create_file(
		&ksup_stream_class_dev,
		&class_device_attr_dev);
	if (err < 0)
		goto err_stream_class_device_create_file;
#endif

	/* Frame */
	snprintf(ksup_frame_class_dev.class_id,
		sizeof(ksup_frame_class_dev.class_id),
		"userport_frame");
	ksup_frame_class_dev.class = &ksup_class;
	ksup_frame_class_dev.dev = NULL;
#ifdef HAVE_CLASS_DEV_DEVT
	ksup_frame_class_dev.devt = ksup_first_dev + 1;
#endif

	err = class_device_register(&ksup_frame_class_dev);
	if (err < 0)
		goto err_frame_class_device_register;

#ifndef HAVE_CLASS_DEV_DEVT
	err = class_device_create_file(
		&ksup_frame_class_dev,
		&class_device_attr_dev);
	if (err < 0)
		goto err_frame_class_device_create_file;
#endif

	return 0;

	class_device_unregister(&ksup_frame_class_dev);
err_frame_class_device_register:
#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&ksup_frame_class_dev,
		&class_device_attr_dev);
err_frame_class_device_create_file:
#endif
	class_device_unregister(&ksup_stream_class_dev);
err_stream_class_device_register:
#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&ksup_stream_class_dev,
		&class_device_attr_dev);
err_stream_class_device_create_file:
#endif
	cdev_del(&ksup_cdev);
err_cdev_add:
	unregister_chrdev_region(ksup_first_dev, 3);
err_register_chrdev:
	class_unregister(&ksup_class);
err_class_register:

	return err;
}

module_init(ksup_init_module);

static void __exit ksup_module_exit(void)
{
#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&ksup_frame_class_dev,
		&class_device_attr_dev);
#endif

	class_device_unregister(&ksup_frame_class_dev);

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&ksup_stream_class_dev,
		&class_device_attr_dev);
#endif

	class_device_unregister(&ksup_stream_class_dev);

	cdev_del(&ksup_cdev);
	unregister_chrdev_region(ksup_first_dev, 3);

	class_unregister(&ksup_class);

	ksup_msg(KERN_INFO, ksup_MODULE_DESCR " unloaded\n");
}

module_exit(ksup_module_exit);

MODULE_DESCRIPTION(ksup_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

#ifdef DEBUG_CODE
module_param(debug_level, int, 0444);
MODULE_PARM_DESC(debug_level, "Initial debug level");
#endif
