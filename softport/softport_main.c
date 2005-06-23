/*
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/device.h>

#include "streambus.h"

static dev_t sb_first_dev;
static struct cdev sb_cdev;

struct hlist_head sb_chan_index_hash[1 << SB_CHAN_HASHBITS];

static inline struct hlist_head *sb_chan_index_get_hash(int index)
{
	return &sb_chan_index_hash[index & ((1 << SB_CHAN_HASHBITS) - 1)];
}

struct sb_chan *__sb_chan_get_by_index(int index)
{
	struct hlist_node *t;
	struct sb_chan *sb_chan;

	hlist_for_each_entry(sb_chan, t, sb_chan_index_get_hash(index),
			index_hlist) {
		if (sb_chan->index == index)
			return sb_chan;
	}

	return NULL;
}

static int sb_chan_new_index(void)
{
	static int index;
	for (;;) {
		if (++index <= 0)
			index = 1;
		if (!__sb_chan_get_by_index(index))
			return index;
	}
}


struct sb_cdev_data
{
	struct sb_chan *chan;
};

int sb_cdev_open(
	struct inode *inode,
	struct file *file)
{
	nonseekable_open(inode, file);



//	int chan_idx = (inode->i_rdev - card->first_dev) / 2;


	struct sb_cdev_data *cd = kmalloc(sizeof(struct sb_cdev_data), GFP_KERNEL);
	if (!cd)
		return -ENOMEM;

	memset(cd, 0x00, sizeof(*cd));

	file->private_data = cd;

	return 0;
}

int sb_cdev_release(
	struct inode *inode, struct file *file)
{
	BUG_ON(!file->private_data);

	kfree(file->private_data);

	return 0;
}

ssize_t sb_cdev_read(
	struct file *file,
	char __user *buf,
	size_t count,
	loff_t *offp)
{
	int err = 0;

	return err;
}

ssize_t sb_cdev_write(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *offp)
{
	int err = 0;

	return err;
}

#define SB_IOC_FIFO_IN_AVAIL	_IOR(0xd0, 1, int)
#define SB_IOC_CONNECT		_IOR(0xd0, 2, unsigned int)

ssize_t sb_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	switch(cmd) {
	case SB_IOC_CONNECT:
		
	break;
	}

	return -EINVAL;
}

struct file_operations sb_fops =
{
	.owner		= THIS_MODULE,
	.read		= sb_cdev_read,
	.write		= sb_cdev_write,
	.ioctl		= sb_cdev_ioctl,
	.open		= sb_cdev_open,
	.release	= sb_cdev_release,
	.llseek		= no_llseek,
};

static struct class_device_attribute *sb_class_attributes[] = {
	NULL
};

#define to_chan(class) container_of(class, struct sb_chan, class_dev)

static int sb_hotplug(struct class_device *cd, char **envp,
	int num_envp, char *buf, int size)
{
	struct sb_chan *chan = to_chan(cd);

	envp[0] = NULL;

	return 0;
}

static void sb_release(struct class_device *cd)
{
	struct sb_chan *chan = to_chan(cd);

	printk(KERN_DEBUG sb_DRIVER_PREFIX "sb_release called\n");

	// kfree ??
}

static struct class sb_class = {
	.name = "streambus",
	.release = sb_release,
#ifdef CONFIG_HOTPLUG
	.hotplug = sb_hotplug,
#endif
};

static int sb_open(struct visdn_chan *visdn_chan, int mode)
{
	printk(KERN_INFO "sb_open()\n");

	return -EINVAL;
}

static int sb_close(struct visdn_chan *visdn_chan)
{
	printk(KERN_INFO "sb_close()\n");

	return -EINVAL;
}

/******************************************
 * Module stuff
 ******************************************/

struct visdn_port_ops sb_port_ops = {
	.set_role	= NULL,
};

struct visdn_chan_ops sb_chan_ops = {
	.open		= sb_open,
	.close		= sb_close,
	.frame_xmit	= NULL,
	.get_stats	= NULL,
	.do_ioctl	= NULL,
};

struct sb_chan chans[256];

static int __init sb_init_module(void)
{
	int err;

	printk(KERN_INFO sb_DRIVER_DESCR " loading\n");

	int i;
	for (i=0; i< ARRAY_SIZE(sb_chan_index_hash); i++) {
		INIT_HLIST_HEAD(&sb_chan_index_hash[i]);
	}

	struct visdn_port sb_visdn_port;
	visdn_port_init(&sb_visdn_port, &sb_port_ops);
	visdn_port_register(&sb_visdn_port, "softport", NULL);

	for (i=0; i<256; i++) {
		INIT_HLIST_NODE(&chans[i].index_hlist);

		chans[i].index = sb_chan_new_index();

		visdn_chan_init(&chans[i].visdn_chan, &sb_chan_ops);
		chans[i].visdn_chan.priv = &chans[i];
		chans[i].visdn_chan.speed = 0;
		chans[i].visdn_chan.role = VISDN_CHAN_ROLE_B;
		chans[i].visdn_chan.roles = VISDN_CHAN_ROLE_B;
		chans[i].visdn_chan.protocol = 0;
		chans[i].visdn_chan.flags = 0;

		char chanid[10];
		snprintf(chanid, sizeof(chanid), "%d", i);

		visdn_chan_register(&chans[i].visdn_chan, chanid,
			&sb_visdn_port);
	}

	err = class_register(&sb_class);
	if (err < 0)
		goto err_class_register;

	err = alloc_chrdev_region(&sb_first_dev, 0, 256, sb_DRIVER_NAME);
	if (err < 0)
		goto err_register_chrdev;

	cdev_init(&sb_cdev, &sb_fops);
	sb_cdev.owner = THIS_MODULE;

	err = cdev_add(&sb_cdev, sb_first_dev, 256);
	if (err < 0)
		goto err_cdev_add;

	return 0;

err_cdev_add:
	unregister_chrdev_region(sb_first_dev, 256);
err_register_chrdev:
	class_unregister(&sb_class);
err_class_register:

	return err;
}

module_init(sb_init_module);

static void __exit sb_module_exit(void)
{
	class_unregister(&sb_class);

	cdev_del(&sb_cdev);

	unregister_chrdev_region(sb_first_dev, 256);

	printk(KERN_INFO sb_DRIVER_DESCR " unloaded\n");
}

module_exit(sb_module_exit);

MODULE_DESCRIPTION(sb_DRIVER_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
