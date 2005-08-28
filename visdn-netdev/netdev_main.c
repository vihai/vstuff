#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/device.h>
#include <linux/list.h>

#include <visdn.h>
#include <lapd.h>

#include "netdev.h"

static dev_t vnd_first_dev;
static struct cdev vnd_cdev;
static struct class_device vnd_control_class_dev;

struct hlist_head vnd_chan_index_hash[VND_CHAN_HASHSIZE];

static inline struct hlist_head *vnd_chan_index_get_hash(int index)
{
	return &vnd_chan_index_hash[index & (VND_CHAN_HASHSIZE - 1)];
}

struct vnd_chan *__vnd_chan_get_by_index(int index)
{
	struct hlist_node *t;
	struct vnd_chan *vnd_chan;

	hlist_for_each_entry(vnd_chan, t, vnd_chan_index_get_hash(index),
			index_hlist_node) {
		if (vnd_chan->index == index)
			return vnd_chan;
	}

	return NULL;
}

static int vnd_chan_new_index(void)
{
	static int index;
	for (;;) {
		if (++index <= 0)
			index = 1;
		if (!__vnd_chan_get_by_index(index))
			return index;
	}
}

struct visdn_port vnd_port;

static void vnd_chan_release(struct visdn_chan *visdn_chan)
{
	struct vnd_chan *chan = to_vnd_chan(visdn_chan);

	kfree(chan);
}

static int vnd_chan_open(struct visdn_chan *visdn_chan)
{
	printk(KERN_INFO "vnd_open()\n");

	return 0;
}

static int vnd_chan_close(struct visdn_chan *visdn_chan)
{
	printk(KERN_INFO "vnd_close()\n");

	return 0;
}

static int vnd_chan_frame_xmit(struct visdn_chan *visdn_chan, struct sk_buff *skb)
{
	struct vnd_chan *chan = to_vnd_chan(visdn_chan);

	skb->protocol = htons(ETH_P_LAPD); // FIXME chan->protocol);
	skb->dev = chan->netdev;
	skb->pkt_type = PACKET_HOST;
	skb->ip_summed = CHECKSUM_UNNECESSARY;

/*	// Oh... this is the echo channel... redirect to D
	// channel's netdev
	if (fdchan->id == E) {
		skb->protocol = htons(port->chans[D].protocol);
		skb->dev = port->chans[D].netdev;
		skb->pkt_type = PACKET_OTHERHOST;
	} else {
		skb->protocol = htons(fdchan->protocol);
		skb->dev = fdchan->netdev;
		skb->pkt_type = PACKET_HOST;
	}*/

	return netif_rx(skb);
}

static int vnd_chan_connect_to(
	struct visdn_chan *visdn_chan,
	struct visdn_chan *visdn_chan2,
	int flags)
{
	if (visdn_chan2->connected_chan ||
	    visdn_chan->connected_chan)
		return -EBUSY;

	printk(KERN_INFO "%s connected to %s\n",
		visdn_chan->device.bus_id,
		visdn_chan2->device.bus_id);

	return 0;
}

static int vnd_chan_disconnect(struct visdn_chan *visdn_chan)
{
	if (!visdn_chan->connected_chan)
		return 0;

	printk(KERN_INFO "%s disconnected from %s\n",
		visdn_chan->device.bus_id,
		visdn_chan->connected_chan->device.bus_id);

	return 0;
}

void vnd_chan_stop_queue(struct visdn_chan *visdn_chan)
{
	struct vnd_chan *chan = to_vnd_chan(visdn_chan);

	netif_stop_queue(chan->netdev);
}

void vnd_chan_start_queue(struct visdn_chan *visdn_chan)
{
	struct vnd_chan *chan = to_vnd_chan(visdn_chan);

	netif_start_queue(chan->netdev);
}

void vnd_chan_wake_queue(struct visdn_chan *visdn_chan)
{
	struct vnd_chan *chan = to_vnd_chan(visdn_chan);

	netif_wake_queue(chan->netdev);
}

struct visdn_chan_ops vnd_chan_ops = {
	.release		= vnd_chan_release,
	.open			= vnd_chan_open,
	.close			= vnd_chan_close,
	.frame_xmit		= vnd_chan_frame_xmit,
	.frame_input_error	= NULL,
	.get_stats		= NULL,
	.do_ioctl		= NULL,

	.connect_to     	= vnd_chan_connect_to,
	.disconnect		= vnd_chan_disconnect,

	.samples_read   	= NULL,
	.samples_write  	= NULL,

	.stop_queue		= vnd_chan_stop_queue,
	.start_queue		= vnd_chan_start_queue,
	.wake_queue		= vnd_chan_wake_queue,
};

static int vnd_netdev_open(struct net_device *netdev)
{
//	struct visdn_chan *chan = netdev->priv;

	printk(KERN_INFO "vnd_netdev_open()\n");

	return 0;
}

static int vnd_netdev_stop(struct net_device *netdev)
{
//	struct visdn_chan *chan = netdev->priv;

	printk(KERN_INFO "vnd_netdev_stop()\n");

	return 0;
}

static int vnd_netdev_frame_xmit(
	struct sk_buff *skb,
	struct net_device *netdev)
{
	struct vnd_chan *chan = netdev->priv;

	netdev->trans_start = jiffies;

	if (chan->visdn_chan.connected_chan &&
	    chan->visdn_chan.connected_chan->ops->frame_xmit)
		return chan->visdn_chan.connected_chan->ops->frame_xmit(
				chan->visdn_chan.connected_chan, skb);

	return -EOPNOTSUPP;
}

static struct net_device_stats *vnd_netdev_get_stats(struct net_device *netdev)
{
	struct vnd_chan *chan = netdev->priv;

	if (chan->visdn_chan.connected_chan &&
	    chan->visdn_chan.connected_chan->ops->frame_xmit)
		return chan->visdn_chan.connected_chan->ops->get_stats(
				chan->visdn_chan.connected_chan);

	return NULL;
}

static void vnd_netdev_set_multicast_list(struct net_device *netdev)
{
//	struct vnd_chan *chan = netdev->priv;

//	if(netdev->flags & IFF_PROMISC && !port->echo_enabled) {
//	}
}

static int vnd_netdev_do_ioctl(struct net_device *netdev,
	struct ifreq *ifr, int cmd)
{
	struct visdn_chan *chan = netdev->priv;

	if (chan->ops->do_ioctl)
		return chan->ops->do_ioctl(chan, ifr, cmd);
	else
		return -EOPNOTSUPP;
}

static int vnd_read_done = FALSE;

int vnd_cdev_open(
	struct inode *inode,
	struct file *file)
{
	nonseekable_open(inode, file);

	vnd_read_done = FALSE;

	return 0;
}

int vnd_cdev_release(
	struct inode *inode, struct file *file)
{
	return 0;
}

ssize_t vnd_cdev_read(
	struct file *file,
	char __user *buf,
	size_t count,
	loff_t *offp)
{
	int err;

	if (vnd_read_done)
		return 0;

	struct vnd_chan *chan = NULL;
	chan = kmalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	visdn_chan_init(&chan->visdn_chan, &vnd_chan_ops);

	chan->index = vnd_chan_new_index();

	chan->visdn_chan.priv = chan;
	chan->visdn_chan.speed = 0;
	chan->visdn_chan.role = VISDN_CHAN_ROLE_B;
	chan->visdn_chan.roles = VISDN_CHAN_ROLE_B;
	chan->visdn_chan.flags = 0;

	chan->visdn_chan.framing_supported = VISDN_CHAN_FRAMING_HDLC |
					     VISDN_CHAN_FRAMING_MTP;
	chan->visdn_chan.framing_preferred = 0;

	chan->visdn_chan.bitorder_supported = VISDN_CHAN_BITORDER_LSB |
					      VISDN_CHAN_BITORDER_MSB;
	chan->visdn_chan.bitorder_preferred = 0;

//	err = sysfs_create_link(&chan->netdev->class_dev.kobj, &chan->class_dev.kobj, "visdn_chan");

	hlist_add_head(&chan->index_hlist_node, vnd_chan_index_get_hash(chan->index));

	char chanid[60];
	snprintf(chanid, sizeof(chanid), "%d", chan->index);

	// We need chan registered to register netddev but someone may use
	// the channel before netdev is registered. This is a short race
	// condition, we MAY be able to avoid just disabling preemption.
	// Every callback is in user context except frame_tx but in order
	// to call frame_tx the channel must be connected with connect_to
	// which is in user context.
	// Anyway, this needs further investigation

	preempt_disable();

	err = visdn_chan_register(&chan->visdn_chan, chanid, &vnd_port);
	if (err < 0)
		goto err_visdn_chan_register;

	char ifname[60];
	snprintf(ifname, sizeof(ifname), "visdn%dd", chan->index);

	chan->netdev = alloc_netdev(0, ifname, setup_lapd);
	if(!chan->netdev) {
		printk(KERN_CRIT
			"net_device alloc failed, abort.\n");
		err = -ENOMEM;
		goto err_alloc_netdev;
	}

	chan->netdev->priv = chan;
	chan->netdev->open = vnd_netdev_open;
	chan->netdev->stop = vnd_netdev_stop;
	chan->netdev->hard_start_xmit = vnd_netdev_frame_xmit;
	chan->netdev->get_stats = vnd_netdev_get_stats;
	chan->netdev->set_multicast_list = vnd_netdev_set_multicast_list;
	chan->netdev->do_ioctl = vnd_netdev_do_ioctl;
	chan->netdev->features = NETIF_F_NO_CSUM;

	chan->netdev->mtu = 200;
//	chan->netdev->mtu = chan->tx.fifo_size;

	memset(chan->netdev->dev_addr, 0x00, sizeof(chan->netdev->dev_addr));

	SET_MODULE_OWNER(chan->netdev);
	SET_NETDEV_DEV(chan->netdev, &chan->visdn_chan.device);

	chan->netdev->irq = 0;
	chan->netdev->base_addr = 0;

	err = register_netdev(chan->netdev);
	if(err) {
		printk(KERN_CRIT
			"Cannot register net device %s, aborting.\n",
			chan->netdev->name);
		goto err_register_netdev;
	}

	preempt_enable();

	char busid[30];
	int len = snprintf(busid, sizeof(busid), "%s\n",
		chan->visdn_chan.device.bus_id);

	if (copy_to_user(buf, busid, len)) {
		err = -EFAULT;
		goto err_copy_to_user;
	}

	vnd_read_done = TRUE;

	return len;

err_copy_to_user:
	visdn_chan_unregister(&chan->visdn_chan);
err_visdn_chan_register:
	unregister_netdev(chan->netdev);
err_register_netdev:
	free_netdev(chan->netdev);
err_alloc_netdev:
	kfree(chan);
err_kmalloc:

	return err;
}

struct file_operations vnd_fops =
{
	.owner		= THIS_MODULE,
	.read		= vnd_cdev_read,
	.write		= NULL,
	.ioctl		= NULL,
	.open		= vnd_cdev_open,
	.release	= vnd_cdev_release,
	.llseek		= no_llseek,
};

/******************************************
 * Module stuff
 ******************************************/

struct visdn_port_ops vnd_port_ops = {
	.enable		= NULL,
	.disable	= NULL,
};

#ifdef NO_CLASS_DEV_DEVT
static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	return print_dev_t(buf, vnd_first_dev);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);
#endif

static int __init vnd_init_module(void)
{
	int err;

	printk(KERN_INFO vnd_MODULE_DESCR " loading\n");

	int i;
	for (i=0; i< ARRAY_SIZE(vnd_chan_index_hash); i++) {
		INIT_HLIST_HEAD(&vnd_chan_index_hash[i]);
	}

	visdn_port_init(&vnd_port, &vnd_port_ops);
	err = visdn_port_register(&vnd_port,
		"netdev", "netdev",
		&visdn_system_device);
	if (err < 0)
		goto err_visdn_port_register;

	err = alloc_chrdev_region(&vnd_first_dev, 0, 1, vnd_MODULE_NAME);
	if (err < 0)
		goto err_register_chrdev;

	cdev_init(&vnd_cdev, &vnd_fops);
	vnd_cdev.owner = THIS_MODULE;

	err = cdev_add(&vnd_cdev, vnd_first_dev, 1);
	if (err < 0)
		goto err_cdev_add;

	class_device_initialize(&vnd_control_class_dev);
	vnd_control_class_dev.class = &visdn_system_class;
	vnd_control_class_dev.class_data = NULL;
	vnd_control_class_dev.dev = &vnd_port.device;
#ifndef NO_CLASS_DEV_DEVT
	vnd_control_class_dev.devt = vnd_first_dev;
#endif
	snprintf(vnd_control_class_dev.class_id,
		sizeof(vnd_control_class_dev.class_id),
		"netdev-control");

	err = class_device_register(&vnd_control_class_dev);
	if (err < 0)
		goto err_control_class_device_register;

#ifdef NO_CLASS_DEV_DEVT
	class_device_create_file(
		&vnd_control_class_dev,
		&class_device_attr_dev);
#endif


	return 0;

	class_device_del(&vnd_control_class_dev);
err_control_class_device_register:
	cdev_del(&vnd_cdev);
err_cdev_add:
	unregister_chrdev_region(vnd_first_dev, 1);
err_register_chrdev:
	visdn_port_unregister(&vnd_port);
err_visdn_port_register:

	return err;
}

module_init(vnd_init_module);

static void __exit vnd_module_exit(void)
{
	struct hlist_node *t;
	struct vnd_chan *chan;

	int i;
	for (i=0; i<ARRAY_SIZE(vnd_chan_index_hash); i++) {
		hlist_for_each_entry(chan, t, &vnd_chan_index_hash[i],
				index_hlist_node) {

			visdn_chan_unregister(&chan->visdn_chan);
			unregister_netdev(chan->netdev);
			//free_netdev(chan->netdev);

			//kfree(chan);
		}
	}

	class_device_del(&vnd_control_class_dev);

	visdn_port_unregister(&vnd_port);

	printk(KERN_INFO vnd_MODULE_DESCR " unloaded\n");
}

module_exit(vnd_module_exit);

MODULE_DESCRIPTION(vnd_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
