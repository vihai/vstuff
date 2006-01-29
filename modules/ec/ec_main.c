/*
 * vISDN echo canceller module, based on kb1ec.h by Kris Boutilier
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
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/device.h>
#include <linux/list.h>

#include <linux/visdn/softcxc.h>
#include <linux/visdn/leg.h>
#include <linux/visdn/port.h>
#include <linux/visdn/router.h>

#include "kb1ec.h"
//#include "mec2.h"
//#include "mg2ec.h"

#include "ec.h"
#include "xlaw.h"

#ifdef DEBUG_CODE
#ifdef DEBUG_DEFAULTS
int debug_level = 1;
#else
int debug_level = 0;
#endif
#endif

static dev_t vec_first_dev;
static struct cdev vec_cdev;
static struct class_device vec_class_dev;

static struct list_head vec_ec_list = LIST_HEAD_INIT(vec_ec_list);
static spinlock_t vec_ec_list_lock = SPIN_LOCK_UNLOCKED;

/* ----------------------------- NEAR end ----------------------------------*/

static void vec_chan_ne_release(struct visdn_chan *visdn_chan)
{
	vec_debug(3, "vec_chan_ne_release()\n");
}

static int vec_chan_ne_open(struct visdn_chan *visdn_chan)
{
	vec_debug(3, "vec_chan_ne_open()\n");

	return 0;
}

static int vec_chan_ne_close(struct visdn_chan *visdn_chan)
{
	vec_debug(3, "vec_chan_ne_close()\n");

	return 0;
}

static ssize_t vec_chan_ne_read(
	struct visdn_leg *visdn_leg,
	void *buf, size_t count)
{
	struct vec_ec *ec = ne_to_vec_ec(visdn_leg->chan);

	int nread = __kfifo_get(ec->ne_r_fifo, buf, count);

	return nread;
}

unsigned int __kfifo_drop(struct kfifo *fifo, unsigned int len)
{
	fifo->out += min(len, fifo->in - fifo->out);

	return len;
}

unsigned int __kfifo_putbyte(struct kfifo *fifo,
			 unsigned char byte, unsigned int len)
{
	unsigned int l;

	len = min(len, fifo->size - fifo->in + fifo->out);

	/* first put the data starting from fifo->in to buffer end */
	l = min(len, fifo->size - (fifo->in & (fifo->size - 1)));
	memset(fifo->buffer + (fifo->in & (fifo->size - 1)), byte, l);

	/* then put the rest (if any) at the beginning of the buffer */
	memset(fifo->buffer, byte, len - l);

	fifo->in += len;

	return len;
}

static ssize_t vec_chan_ne_write(
	struct visdn_leg *visdn_leg,
	const void *buf, size_t count)
{
	struct vec_ec *ec = ne_to_vec_ec(visdn_leg->chan);
	int nwrote = 0;
	int nref;
	int i;
	u8 ref[512];

	memset(ref, 0, sizeof(ref));

	nref = __kfifo_get(ec->fe_w_fifo, ref, count);
	if (nref == count) {
		/* Right number samples in other direction */
	} else if (nref < count) {
		/* Too few samples in other direction */

		printk(KERN_DEBUG "Detected %d slips\n",
			nref - count);

		// FIXME :) (filled with undetermined samples)
		nref = count;
	}

#if 0
	if (kfifo_len(ec->fe_w_fifo)) {

		/* Too many samples in other direction */

		

		printk(KERN_DEBUG "Detected %d slips\n",
			kfifo_len(ec->fe_w_fifo));
	}
#endif

	for (i=0; i<count; i++) {
		u8 octet;
		s16 sample = alaw_to_linear(((u8 *)buf)[i]);

		switch (ec->ec_state) {
		case VEC_PRE_TRAINING:
			// Mute NE output
			ref[i] = linear_to_alaw(0);

			// Mute FE output
			sample = 0;

			ec->pre_training_timer++;

			if (ec->pre_training_timer > 3200) {
				ec->training_pos = 0;
				ec->ec_state = VEC_TRAINING;
printk(KERN_DEBUG "Echo canceller started training\n");
			}
		break;

		case VEC_TRAINING:
			if (ec->training_pos == 0)
				ref[i] = linear_to_alaw(32000);
			else
				ref[i] = linear_to_alaw(0);

			// Train from NE->FE
			if (echo_can_traintap(ec->ec,
					ec->training_pos,
					sample)) {
				ec->ec_state = VEC_ACTIVE;

			// Mute FE output
			sample = 0;

printk(KERN_DEBUG "Echo canceller active\n");
{
int i;
printk(KERN_DEBUG "Training complete: a_s = ");
for(i=0; i<ec->ec->N_d; i++) {
	printk("%d ", ec->ec->a_s[i]);
}
printk("\n");}
			}

			ec->training_pos++;
		break;

		case VEC_ACTIVE:
			sample = echo_can_update(ec->ec,
					alaw_to_linear(ref[i]),
					sample);
		break;

		case VEC_OFF:
		break;
		}

		octet = linear_to_alaw(sample);
		__kfifo_put(ec->fe_r_fifo, &octet, sizeof(octet));
	}

	nwrote = __kfifo_put(ec->ne_r_fifo, ref, count);

	return nwrote;
}

static int vec_chan_ne_connect(
	struct visdn_leg *visdn_leg1,
	struct visdn_leg *visdn_leg2)
{
	vec_debug(2, "Echo canceller near end %06d connected to %06d\n",
		visdn_leg1->chan->id,
		visdn_leg2->chan->id);

	return 0;
}

static void vec_chan_ne_disconnect(
	struct visdn_leg *visdn_leg1,
	struct visdn_leg *visdn_leg2)
{
	vec_debug(2, "Echo canceller near end %06d disconnected from %06d\n",
		visdn_leg1->chan->id,
		visdn_leg2->chan->id);
}

static struct visdn_chan_ops vec_chan_ne_ops = {
	.owner		= THIS_MODULE,

	.release	= vec_chan_ne_release,
	.open		= vec_chan_ne_open,
	.close		= vec_chan_ne_close,
};

static struct visdn_leg_ops vec_leg_ne_ops = {
	.owner		= THIS_MODULE,

	.connect	= vec_chan_ne_connect,
	.disconnect	= vec_chan_ne_disconnect,

	.read		= vec_chan_ne_read,
	.write		= vec_chan_ne_write,
};

/* ------------------------------ FAR end ----------------------------------*/

static void vec_chan_fe_release(struct visdn_chan *visdn_chan)
{
	vec_debug(3, "vec_chan_fe_release()\n");
}

static int vec_chan_fe_open(struct visdn_chan *visdn_chan)
{
	vec_debug(3, "vec_chan_fe_open()\n");

	return 0;
}

static int vec_chan_fe_close(struct visdn_chan *visdn_chan)
{
	vec_debug(3, "vec_chan_fe_close()\n");

	return 0;
}

static ssize_t vec_chan_fe_read(
	struct visdn_leg *visdn_leg,
	void *buf, size_t count)
{
	struct vec_ec *ec = fe_to_vec_ec(visdn_leg->chan);

	return __kfifo_get(ec->fe_r_fifo, buf, count);
}

static ssize_t vec_chan_fe_write(
	struct visdn_leg *visdn_leg,
	const void *buf, size_t count)

{
	struct vec_ec *ec = fe_to_vec_ec(visdn_leg->chan);

	if (!count)
		return 0;

	return __kfifo_put(ec->fe_w_fifo, (void *)buf, count);
}

static int vec_chan_fe_connect(
	struct visdn_leg *visdn_leg1,
	struct visdn_leg *visdn_leg2)
{
	vec_debug(2, "Echo canceller far-end %06d connected to %06d\n",
		visdn_leg1->chan->id,
		visdn_leg2->chan->id);

	return 0;
}

static void vec_chan_fe_disconnect(
	struct visdn_leg *visdn_leg1,
	struct visdn_leg *visdn_leg2)
{
	vec_debug(2, "Echo canceller far-end %06d disconnected from %06d\n",
		visdn_leg1->chan->id,
		visdn_leg2->chan->id);
}

static struct visdn_chan_ops vec_chan_fe_ops = {
	.owner		= THIS_MODULE,

	.release	= vec_chan_fe_release,
	.open		= vec_chan_fe_open,
	.close		= vec_chan_fe_close,
};

static struct visdn_leg_ops vec_leg_fe_ops = {
	.owner		= THIS_MODULE,

	.connect	= vec_chan_fe_connect,
	.disconnect	= vec_chan_fe_disconnect,

	.read		= vec_chan_fe_read,
	.write		= vec_chan_fe_write,
};

/*---------------------------------------------------------------------------*/

struct visdn_port_ops vec_port_ops = {
	.owner	= THIS_MODULE,
};

/*---------------------------------------------------------------------------*/

static ssize_t vec_show_fe_r_fifo_usage(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct vec_ec *ec = to_vec_ec(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		kfifo_len(ec->fe_r_fifo));
}

static ssize_t vec_store_fe_r_fifo_usage(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct vec_ec *ec = to_vec_ec(visdn_port);

	kfifo_reset(ec->fe_r_fifo);

	return count;
}

static VISDN_PORT_ATTR(fe_r_fifo_usage, S_IRUGO | S_IWUSR,
		vec_show_fe_r_fifo_usage,
		vec_store_fe_r_fifo_usage);

/*---------------------------------------------------------------------------*/

static ssize_t vec_show_fe_w_fifo_usage(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct vec_ec *ec = to_vec_ec(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		kfifo_len(ec->fe_w_fifo));
}

static ssize_t vec_store_fe_w_fifo_usage(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct vec_ec *ec = to_vec_ec(visdn_port);

	kfifo_reset(ec->fe_w_fifo);

	return count;
}

static VISDN_PORT_ATTR(fe_w_fifo_usage, S_IRUGO | S_IWUSR,
		vec_show_fe_w_fifo_usage,
		vec_store_fe_w_fifo_usage);

/*---------------------------------------------------------------------------*/

static ssize_t vec_show_ne_r_fifo_usage(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct vec_ec *ec = to_vec_ec(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n",
		kfifo_len(ec->ne_r_fifo));
}

static ssize_t vec_store_ne_r_fifo_usage(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct vec_ec *ec = to_vec_ec(visdn_port);

	kfifo_reset(ec->ne_r_fifo);

	return count;
}

static VISDN_PORT_ATTR(ne_r_fifo_usage, S_IRUGO | S_IWUSR,
		vec_show_ne_r_fifo_usage,
		vec_store_ne_r_fifo_usage);

/*---------------------------------------------------------------------------*/

static ssize_t vec_port_show_ec_state(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct vec_ec *ec = to_vec_ec(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n", ec->ec_state);
}

static void vec_start(struct vec_ec *ec)
{
	if (ec->ec)
		echo_can_free(ec->ec);

	ec->ec = echo_can_create(128, 0);
	ec->ec_state = VEC_PRE_TRAINING;
	ec->pre_training_timer = 0;

	printk(KERN_DEBUG "Echo canceller start pre-training\n");
}

static void vec_stop(struct vec_ec *ec)
{
	ec->ec_state = VEC_OFF;
}

static ssize_t vec_port_store_ec_state(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct vec_ec *ec = to_vec_ec(visdn_port);
	int value;

	sscanf(buf, "%d", &value);

	if (value) {
		vec_start(ec);
	} else {
		vec_stop(ec);
	}

	return count;
}

static VISDN_PORT_ATTR(ec_state, S_IRUGO | S_IWUSR,
		vec_port_show_ec_state,
		vec_port_store_ec_state);

//----------------------------------------------------------------------------

static ssize_t vec_port_show_ec_coeffs(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct vec_ec *ec = to_vec_ec(visdn_port);
	int len = 0;

	if (!ec->ec)
		return 0;

	int i;
	for (i=0; i<ec->ec->N_d; i++) {
		len += snprintf(buf + len, PAGE_SIZE - len,
			"%d\n",
			ec->ec->a_s[i]);
	}

	len += snprintf(buf + len, PAGE_SIZE - len, "\n");

	return len;
}

static VISDN_PORT_ATTR(ec_coeffs, S_IRUGO,
		vec_port_show_ec_coeffs,
		NULL);

//----------------------------------------------------------------------------

static struct vec_ec *_vec_ec_search_by_id(int id)
{
	struct vec_ec *ec;

	list_for_each_entry(ec, &vec_ec_list, node) {
		if (ec->id == id)
			return ec;
	}

	return NULL;
}

static int _vec_ec_alloc_id(void)
{
	struct vec_ec *ec;
	static int id;

retry:
	id++;

	list_for_each_entry(ec, &vec_ec_list, node) {
		if (ec->id == id)
			goto retry;
	}

	return id;
}

static int vec_cdev_open(
	struct inode *inode,
	struct file *file)
{
	int err;
	struct vec_ec *ec;

	nonseekable_open(inode, file);

	ec = kmalloc(sizeof(*ec), GFP_KERNEL);
	if (!ec) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	memset(ec, 0, sizeof(*ec));

	spin_lock(&vec_ec_list_lock);
	ec->id = _vec_ec_alloc_id();
	list_add_tail(&ec->node, &vec_ec_list);
	spin_unlock(&vec_ec_list_lock);

	file->private_data = ec;

	ec->ec = NULL;

	spin_lock_init(&ec->fe_r_fifo_lock);
	ec->fe_r_fifo = kfifo_alloc(1024, GFP_KERNEL, &ec->fe_r_fifo_lock);
	if (!ec->fe_r_fifo) {
		err = -EFAULT;
		goto err_fifo_fe_r_alloc;
	}

	spin_lock_init(&ec->fe_w_fifo_lock);
	ec->fe_w_fifo = kfifo_alloc(1024, GFP_KERNEL, &ec->fe_w_fifo_lock);
	if (!ec->fe_w_fifo) {
		err = -EFAULT;
		goto err_fifo_fe_w_alloc;
	}

	spin_lock_init(&ec->ne_r_fifo_lock);
	ec->ne_r_fifo = kfifo_alloc(1024, GFP_KERNEL, &ec->ne_r_fifo_lock);
	if (!ec->ne_r_fifo) {
		err = -EFAULT;
		goto err_fifo_ne_r_alloc;
	}

	visdn_port_init(&ec->visdn_port);
	ec->visdn_port.ops = &vec_port_ops;
	ec->visdn_port.device = &visdn_system_device;
	snprintf(ec->visdn_port.name, sizeof(ec->visdn_port.name),
		"%d", ec->id);

	err = visdn_port_register(&ec->visdn_port);
	if (err < 0)
		goto err_port_register;

	err = visdn_port_create_file(&ec->visdn_port,
					&visdn_port_attr_fe_r_fifo_usage);
	if (err < 0)
		goto err_create_file_fe_r_fifo;

	err = visdn_port_create_file(&ec->visdn_port,
					&visdn_port_attr_fe_w_fifo_usage);
	if (err < 0)
		goto err_create_file_fe_w_fifo;

	err = visdn_port_create_file(&ec->visdn_port,
					&visdn_port_attr_ne_r_fifo_usage);
	if (err < 0)
		goto err_create_file_ne_r_fifo;

	err = visdn_port_create_file(&ec->visdn_port,
					&visdn_port_attr_ec_state);
	if (err < 0)
		goto err_create_file_ec_state;

	err = visdn_port_create_file(&ec->visdn_port,
					&visdn_port_attr_ec_coeffs);
	if (err < 0)
		goto err_create_file_ec_coeffs;

	/* ----------- NEAR END ------------- */

	visdn_chan_init(&ec->visdn_chan_ne);

	ec->visdn_chan_ne.ops = &vec_chan_ne_ops;
	ec->visdn_chan_ne.chan_class = NULL;
	ec->visdn_chan_ne.port = &ec->visdn_port;

	ec->visdn_chan_ne.leg_a.ops = &vec_leg_ne_ops;
	ec->visdn_chan_ne.leg_a.cxc = &vsc_softcxc.cxc;
	ec->visdn_chan_ne.leg_a.framing = VISDN_LEG_FRAMING_NONE;
	ec->visdn_chan_ne.leg_a.framing_avail = VISDN_LEG_FRAMING_NONE;
	ec->visdn_chan_ne.leg_a.mtu = -1;

	ec->visdn_chan_ne.leg_b.ops = NULL;
	ec->visdn_chan_ne.leg_b.cxc = NULL;
	ec->visdn_chan_ne.leg_b.framing = VISDN_LEG_FRAMING_NONE;
	ec->visdn_chan_ne.leg_b.framing_avail = VISDN_LEG_FRAMING_NONE;
	ec->visdn_chan_ne.leg_b.mtu = -1;

	strcpy(ec->visdn_chan_ne.name, "near_end");

	ec->visdn_chan_fe.driver_data = ec;

	err = visdn_chan_register(&ec->visdn_chan_ne);
	if (err < 0)
		goto err_chan_register_ne;

	/* ----------- FAR END ------------- */

	visdn_chan_init(&ec->visdn_chan_fe);

	ec->visdn_chan_fe.ops = &vec_chan_fe_ops;
	ec->visdn_chan_fe.chan_class = NULL;
	ec->visdn_chan_fe.port = &ec->visdn_port;

	ec->visdn_chan_fe.leg_a.ops = &vec_leg_fe_ops;
	ec->visdn_chan_fe.leg_a.cxc = &vsc_softcxc.cxc;
	ec->visdn_chan_fe.leg_a.framing = VISDN_LEG_FRAMING_NONE;
	ec->visdn_chan_fe.leg_a.framing_avail = VISDN_LEG_FRAMING_NONE;
	ec->visdn_chan_fe.leg_a.mtu = -1;

	ec->visdn_chan_fe.leg_b.ops = NULL;
	ec->visdn_chan_fe.leg_b.cxc = NULL;
	ec->visdn_chan_fe.leg_b.framing = VISDN_LEG_FRAMING_NONE;
	ec->visdn_chan_fe.leg_b.framing_avail = VISDN_LEG_FRAMING_NONE;
	ec->visdn_chan_fe.leg_b.mtu = -1;

	strcpy(ec->visdn_chan_fe.name, "far_end");

	ec->visdn_chan_fe.driver_data = ec;

	err = visdn_chan_register(&ec->visdn_chan_fe);
	if (err < 0)
		goto err_chan_register_fe;

	/* ------------------------------- */

	vec_debug(2, "Echo canceller opened\n", ec->visdn_chan.id);

	return 0;

	visdn_chan_unregister(&ec->visdn_chan_fe);
err_chan_register_fe:
	visdn_chan_unregister(&ec->visdn_chan_ne);
err_chan_register_ne:
	visdn_port_remove_file(&ec->visdn_port,
				&visdn_port_attr_ec_coeffs);
err_create_file_ec_coeffs:
	visdn_port_remove_file(&ec->visdn_port,
				&visdn_port_attr_ec_state);
err_create_file_ec_state:
	visdn_port_remove_file(&ec->visdn_port,
				&visdn_port_attr_ne_r_fifo_usage);
err_create_file_ne_r_fifo:
	visdn_port_remove_file(&ec->visdn_port,
				&visdn_port_attr_fe_w_fifo_usage);
err_create_file_fe_w_fifo:
	visdn_port_remove_file(&ec->visdn_port,
				&visdn_port_attr_fe_r_fifo_usage);
err_create_file_fe_r_fifo:
	visdn_port_unregister(&ec->visdn_port);
err_port_register:
	kfifo_free(ec->ne_r_fifo);
err_fifo_ne_r_alloc:
	kfifo_free(ec->fe_w_fifo);
err_fifo_fe_w_alloc:
	kfifo_free(ec->fe_r_fifo);
err_fifo_fe_r_alloc:
	spin_lock(&vec_ec_list_lock);
	list_del(&ec->node);
	spin_unlock(&vec_ec_list_lock);

	kfree(ec);
err_kmalloc:

	return err;
}

static int vec_cdev_release(
	struct inode *inode, struct file *file)
{
	struct vec_ec *ec = file->private_data;

	vec_debug(3, "vec_cdev_release()\n");

	visdn_disconnect_path_with_id(ec->visdn_chan_ne.id);
	visdn_disconnect_path_with_id(ec->visdn_chan_fe.id);

	visdn_chan_unregister(&ec->visdn_chan_fe);
	visdn_chan_unregister(&ec->visdn_chan_ne);

	visdn_port_remove_file(&ec->visdn_port,
				&visdn_port_attr_ec_coeffs);
	visdn_port_remove_file(&ec->visdn_port,
				&visdn_port_attr_ec_state);
	visdn_port_remove_file(&ec->visdn_port,
				&visdn_port_attr_ne_r_fifo_usage);
	visdn_port_remove_file(&ec->visdn_port,
				&visdn_port_attr_fe_w_fifo_usage);
	visdn_port_remove_file(&ec->visdn_port,
				&visdn_port_attr_fe_r_fifo_usage);
	visdn_port_unregister(&ec->visdn_port);

	kfifo_free(ec->ne_r_fifo);
	kfifo_free(ec->fe_w_fifo);
	kfifo_free(ec->fe_r_fifo);

	spin_lock(&vec_ec_list_lock);
	list_del(&ec->node);
	spin_unlock(&vec_ec_list_lock);

	if (ec->ec)
		echo_can_free(ec->ec);

	kfree(ec);

	return 0;
}

static int vec_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct vec_ec *ec = file->private_data;

	switch(cmd) {
	case VEC_GET_FAREND_CHANID:
		return put_user(ec->visdn_chan_fe.id, (unsigned int *)arg);
	break;

	case VEC_GET_NEAREND_CHANID:
		return put_user(ec->visdn_chan_ne.id, (unsigned int *)arg);
	break;

	case VEC_START:
		vec_start(ec);
		return 0;
	break;

	case VEC_STOP:
		vec_stop(ec);
		return 0;
	break;

	}

	return -EOPNOTSUPP;
}

static struct file_operations vec_fops =
{
	.owner		= THIS_MODULE,
	.read		= NULL,
	.write		= NULL,
	.ioctl		= vec_cdev_ioctl,
	.open		= vec_cdev_open,
	.release	= vec_cdev_release,
	.llseek		= no_llseek,
};

#ifndef HAVE_CLASS_DEV_DEVT
static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	return print_dev_t(buf, vec_first_dev);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);
#endif

#ifdef DEBUG_CODE
static ssize_t vec_show_debug_level(
	struct visdn_port *port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", debug_level);
}

static ssize_t vec_store_debug_level(
	struct visdn_port *port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	unsigned int value;
	if (sscanf(buf, "%01x", &value) < 1)
		return -EINVAL;

	debug_level = value;

	vec_msg(KERN_INFO, "Debug level set to '%d'\n", debug_level);

	return count;
}

VISDN_PORT_ATTR(debug_level, S_IRUGO | S_IWUSR,
	vec_show_debug_level,
	vec_store_debug_level);
#endif

static int __init vec_init_module(void)
{
	int err;

	vec_msg(KERN_INFO, vec_MODULE_DESCR " loading\n");

	err = alloc_chrdev_region(&vec_first_dev, 0, 1, vec_MODULE_NAME);
	if (err < 0)
		goto err_register_chrdev;

	cdev_init(&vec_cdev, &vec_fops);
	vec_cdev.owner = THIS_MODULE;

	err = cdev_add(&vec_cdev, vec_first_dev, 1);
	if (err < 0)
		goto err_cdev_add;

	snprintf(vec_class_dev.class_id,
		sizeof(vec_class_dev.class_id),
		"ec-control");
	vec_class_dev.class = &visdn_system_class;
	vec_class_dev.dev = NULL;
#ifdef HAVE_CLASS_DEV_DEVT
	vec_class_dev.devt = vec_first_dev;
#endif

	err = class_device_register(&vec_class_dev);
	if (err < 0)
		goto err_class_device_register;

#ifndef HAVE_CLASS_DEV_DEVT
	err = class_device_create_file(
		&vec_class_dev,
		&class_device_attr_dev);
	if (err < 0)
		goto err_class_device_create_file;
#endif

	return 0;

	class_device_unregister(&vec_class_dev);
err_class_device_register:
#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&vec_class_dev,
		&class_device_attr_dev);
err_class_device_create_file:
#endif
	cdev_del(&vec_cdev);
err_cdev_add:
	unregister_chrdev_region(vec_first_dev, 1);
err_register_chrdev:

	return err;
}

module_init(vec_init_module);

static void __exit vec_module_exit(void)
{
	// We should free all channels, here!

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&vec_class_dev,
		&class_device_attr_dev);
#endif

	class_device_unregister(&vec_class_dev);

	cdev_del(&vec_cdev);
	unregister_chrdev_region(vec_first_dev, 1);

	vec_msg(KERN_INFO, vec_MODULE_DESCR " unloaded\n");
}

module_exit(vec_module_exit);

MODULE_DESCRIPTION(vec_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

#ifdef DEBUG_CODE
module_param(debug_level, int, 0444);
MODULE_PARM_DESC(debug_level, "Initial debug level");
#endif
