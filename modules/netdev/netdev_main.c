/*
 * vISDN gateway between vISDN's crossconnector and Linux's netdev subsystem
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
#include <linux/netdevice.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/crc32.h>

#include <linux/kstreamer/kstreamer.h>
#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/duplex.h>
#include <linux/kstreamer/pipeline.h>
#include <linux/kstreamer/softswitch.h>

#include <linux/visdn/visdn.h>
#include <linux/visdn/port.h>

#include <linux/lapd.h>

#include "netdev.h"

#ifdef DEBUG_CODE
#ifdef DEBUG_DEFAULTS
int debug_level = 3;
#else
int debug_level = 0;
#endif
#endif

static dev_t vnd_first_dev;
static struct cdev vnd_cdev;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
static struct class_device vnd_control_device;
#else
static struct device vnd_control_device;
#endif

static struct list_head vnd_netdevices_list =
					LIST_HEAD_INIT(vnd_netdevices_list);
static spinlock_t vnd_netdevices_list_lock = SPIN_LOCK_UNLOCKED;

#if 0
static void visdn_make_kobj_path(
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
}
#endif

static void vnd_netdevice_release(
	struct kref *kref)
{
	struct vnd_netdevice *netdevice =
		container_of(kref, struct vnd_netdevice, kref);

	vnd_debug(3, "vnd_netdevice_put(): releasing\n");

	if (netdevice->netdev) {
		free_netdev(netdevice->netdev);
		netdevice->netdev = NULL;
	}

	kfree(netdevice);
}

static struct vnd_netdevice *vnd_netdevice_get(
	struct vnd_netdevice *netdevice)
{
	if (netdevice) {
		WARN_ON(atomic_read(&netdevice->kref.refcount) <= 0);

		kref_get(&netdevice->kref);
	}

	return netdevice;
}

static void vnd_netdevice_put(
	struct vnd_netdevice *netdevice)
{
	if (netdevice)
		WARN_ON(atomic_read(&netdevice->kref.refcount) <= 0);

	kref_put(&netdevice->kref, vnd_netdevice_release);
}

static struct vnd_netdevice *_vnd_netdevice_get_by_name(const char *name)
{
	struct vnd_netdevice *netdevice;

	list_for_each_entry(netdevice, &vnd_netdevices_list, list_node) {
		if (!strcmp(netdevice->netdev->name, name))
			return vnd_netdevice_get(netdevice);
	}

	return NULL;
}

static struct vnd_netdevice *vnd_netdevice_get_by_name(const char *name)
{
	struct vnd_netdevice *netdevice;
	unsigned long flags;

	spin_lock_irqsave(&vnd_netdevices_list_lock, flags);
	netdevice = _vnd_netdevice_get_by_name(name);
	spin_unlock_irqrestore(&vnd_netdevices_list_lock, flags);

	return netdevice;
}

/*---------------------------------------------------------------------------*/

static void vnd_chan_d_rx_release(struct ks_chan *ks_chan)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_chan, struct vnd_netdevice, ks_chan_d_rx);

	vnd_debug_nd(netdevice, 3, "vnd_chan_d_rx_release()\n");

	vnd_netdevice_put(netdevice);
}

static int vnd_chan_d_rx_open(struct ks_chan *ks_chan)
{
//	struct vnd_netdevice *netdevice =
//		container_of(ks_chan, struct vnd_netdevice, ks_chan_d_rx);

	vnd_debug_nd(netdevice, 3, "vnd_chan_d_rx_open()\n");

	return 0;
}

static void vnd_chan_d_rx_close(struct ks_chan *ks_chan)
{
//	struct vnd_netdevice *netdevice =
//		container_of(ks_chan, struct vnd_netdevice, ks_chan_d_rx);

	vnd_debug_nd(netdevice, 3, "vnd_chan_d_rx_close()\n");
}

static int vnd_chan_d_rx_push_frame(
	struct ks_chan *ks_chan,
	struct sk_buff *skb)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_chan, struct vnd_netdevice, ks_chan_d_rx);
	struct lapd_prim_hdr *prim_hdr;

	netdevice->netdev->last_rx = jiffies;

	netdevice->stats.rx_packets++;
	netdevice->stats.rx_bytes += skb->len;

	skb->protocol = htons(ETH_P_LAPD);
	skb->dev = netdevice->netdev;
	skb->pkt_type = PACKET_HOST;
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	skb_push(skb, sizeof(struct lapd_prim_hdr));
	prim_hdr = (struct lapd_prim_hdr *)skb->data;
	prim_hdr->primitive_type = LAPD_PH_DATA_INDICATION;

	return netif_rx(skb);
}

static int vnd_chan_d_rx_connect(struct ks_chan *ks_chan)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_chan, struct vnd_netdevice, ks_chan_d_rx);

	vnd_debug_nd(netdevice, 2, "D RX connected\n");

	/* Ensure the queue status is restored */
	netif_wake_queue(netdevice->netdev);

	return 0;
}

static void vnd_chan_d_rx_disconnect(struct ks_chan *ks_chan)
{
//	struct vnd_netdevice *netdevice =
//		container_of(ks_chan, struct vnd_netdevice, ks_chan_d_rx);

	vnd_debug_nd(netdevice, 2, "disconnected\n");

/*	SCHEDULE NETDEV DOWN OR CHECK A FLAG AND DON'T LOCK AGAIN IN
 *	vnd_netdev_stop, otherwise we deadlock
 *	FIXME FIXME
 *
	rtnl_lock();
	dev_change_flags(netdevice->netdev,
			netdevice->netdev->flags & ~IFF_UP);
	rtnl_unlock();
	*/
}

/*static void vnd_chan_wake_queue(struct ks_chan *ks_chan)
{
	struct vnd_netdevice *netdevice = ks_chan->chan->driver_data;

	netif_wake_queue(netdevice->netdev);
}

static void vnd_chan_rx_error(
	struct ks_chan *ks_chan,
	enum ks_chan_rx_error_code code)
{
	struct vnd_netdevice *netdevice = ks_chan->chan->driver_data;

	switch(code) {
	case KSS_RX_ERROR_DROPPED:
		netdevice->stats.rx_errors++;
		netdevice->stats.rx_dropped++;
	break;

	case KSS_RX_ERROR_LENGTH:
		netdevice->stats.rx_errors++;
		netdevice->stats.rx_length_errors++;
	break;

	case KSS_RX_ERROR_CRC:
		netdevice->stats.rx_errors++;
		netdevice->stats.rx_crc_errors++;
	break;

	case KSS_RX_ERROR_FR_ABORT:
		netdevice->stats.rx_errors++;
		netdevice->stats.collisions++;
	break;
	}
}

static void vnd_chan_tx_error(
	struct ks_chan *ks_chan,
	enum ks_chan_tx_error_code code)
{
	struct vnd_netdevice *netdevice = ks_chan->chan->driver_data;

	switch(code) {
	case KSS_TX_ERROR_FIFO_FULL:
		netdevice->stats.tx_errors++;
		netdevice->stats.tx_fifo_errors++;
	break;
	}
}*/

static struct ks_chan_ops vnd_chan_d_rx_ops = {
	.owner			= THIS_MODULE,
	.release	  	= vnd_chan_d_rx_release,
	.connect	  	= vnd_chan_d_rx_connect,
	.disconnect		= vnd_chan_d_rx_disconnect,
	.open		  	= vnd_chan_d_rx_open,
	.close			= vnd_chan_d_rx_close,
};

static struct kss_chan_from_ops vnd_chan_d_rx_softswitch_ops = {
	.push_frame		= vnd_chan_d_rx_push_frame,

//	.rx_error		= vnd_chan_rx_error,
//	.tx_error		= vnd_chan_tx_error,
};

/*---------------------------------------------------------------------------*/

static void vnd_chan_d_tx_release(struct ks_chan *ks_chan)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_chan, struct vnd_netdevice, ks_chan_d_tx);

	vnd_debug_nd(netdevice, 3, "vnd_chan_d_tx_release()\n");

	vnd_netdevice_put(netdevice);
}

static int vnd_chan_d_tx_open(struct ks_chan *ks_chan)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_chan, struct vnd_netdevice, ks_chan_d_tx);

	struct ks_pipeline *pipeline = netdevice->ks_chan_d_tx.pipeline;

	vnd_debug_nd(netdevice, 3, "vnd_chan_d_tx_open()\n");

	if (!test_bit(VND_NETDEVICE_STATE_RTNL_HELD, &netdevice->state)) {
		rtnl_lock();
		dev_set_mtu(netdevice->netdev, pipeline->mtu);
		rtnl_unlock();
	} else {
		dev_set_mtu(netdevice->netdev, pipeline->mtu);
	}

	return 0;
}

static void vnd_chan_d_tx_close(struct ks_chan *ks_chan)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_chan, struct vnd_netdevice, ks_chan_d_tx);

	vnd_debug_nd(netdevice, 3, "vnd_chan_d_tx_close()\n");

	if (netdevice->remote_port)
		visdn_port_put(netdevice->remote_port);
}

#if 0
static void print_kobj_path(struct kobject *kobj)
{
	struct kobject *parent;

	printk(KERN_DEBUG "KOBJ: ");

	for (parent = kobj; parent; parent = parent->parent) {
		printk(kobject_name(parent));
		printk("/");
	}

	printk("\n");
}
#endif

static int vnd_chan_d_tx_connect(struct ks_chan *ks_chan)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_chan, struct vnd_netdevice, ks_chan_d_tx);
	struct ks_pipeline *pipeline = netdevice->ks_chan_d_tx.pipeline;
	struct ks_node *remote_node;
	struct kobject *cur;
	int err;

	vnd_debug_nd(netdevice, 2, "D TX connected\n");

	remote_node = ks_pipeline_last_node(pipeline);

	err = sysfs_create_link(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
		&netdevice->netdev->class_dev.kobj,
#else
		&netdevice->netdev->dev.kobj,
#endif
		&remote_node->kobj,
			VND_CONNECTED_NODE_SYMLINK_D);
	if (err < 0)
		goto err_create_connected_node_chan;

	cur = &remote_node->kobj;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
	while(cur && cur->kset->ktype != &visdn_port_ktype)
		cur = cur->parent;
#else
	while(cur && cur->kset->kobj.ktype != &visdn_port_ktype)
		cur = cur->parent;
#warning "This code has to be checked"
#endif

	if (!cur) {
		vnd_msg_nd(netdevice, KERN_ERR, "no port found\n");
		err = -EINVAL;
		goto err_no_port;
	}

	netdevice->remote_port = visdn_port_get(to_visdn_port(cur));

	err = sysfs_create_link(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
		&netdevice->netdev->class_dev.kobj,
#else
		&netdevice->netdev->dev.kobj,
#endif
		&netdevice->remote_port->kobj,
			VND_CONNECTED_PORT_SYMLINK);
	if (err < 0)
		goto err_create_port_chan;

	/* Ensure the queue status is restored */
	netif_wake_queue(netdevice->netdev);

	return 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
	sysfs_remove_link(&netdevice->netdev->class_dev.kobj,
			VND_CONNECTED_PORT_SYMLINK);
#else
	sysfs_remove_link(&netdevice->netdev->dev.kobj,
			VND_CONNECTED_PORT_SYMLINK);
#endif

err_create_port_chan:
err_no_port:
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
	sysfs_remove_link(&netdevice->netdev->class_dev.kobj,
			VND_CONNECTED_NODE_SYMLINK_D);
#else
	sysfs_remove_link(&netdevice->netdev->dev.kobj,
			VND_CONNECTED_NODE_SYMLINK_D);
#endif
err_create_connected_node_chan:

	return err;
}

static void vnd_chan_d_tx_disconnect(struct ks_chan *ks_chan)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_chan, struct vnd_netdevice, ks_chan_d_tx);

	vnd_debug_nd(netdevice, 2, "disconnected\n");

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
	if (netdevice->remote_port)
		sysfs_remove_link(&netdevice->netdev->class_dev.kobj,
				VND_CONNECTED_PORT_SYMLINK);

	sysfs_remove_link(&netdevice->netdev->class_dev.kobj,
			VND_CONNECTED_NODE_SYMLINK_D);
#else
	if (netdevice->remote_port)
		sysfs_remove_link(&netdevice->netdev->dev.kobj,
				VND_CONNECTED_PORT_SYMLINK);

	sysfs_remove_link(&netdevice->netdev->dev.kobj,
			VND_CONNECTED_NODE_SYMLINK_D);
#endif

/*	SCHEDULE NETDEV DOWN OR CHECK A FLAG AND DON'T LOCK AGAIN IN
 *	vnd_netdev_stop, otherwise we deadlock
 *	FIXME FIXME
 *
 * 	rtnl_lock();
	dev_change_flags(netdevice->netdev,
			netdevice->netdev->flags & ~IFF_UP);
	rtnl_unlock();*/
}

static int vnd_chan_d_tx_start(struct ks_chan *ks_chan)
{
//	struct vnd_netdevice *netdevice =
//		container_of(ks_chan, struct vnd_netdevice, ks_chan_d_tx);

//	netif_start_queue(netdevice->netdev);

	return 0;
}

static void vnd_chan_d_tx_stop(struct ks_chan *ks_chan)
{
//	struct vnd_netdevice *netdevice =
//		container_of(ks_chan, struct vnd_netdevice, ks_chan_d_tx);
}

static void vnd_chan_d_tx_wake_queue(struct ks_chan *ks_chan)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_chan, struct vnd_netdevice, ks_chan_d_tx);

	netif_wake_queue(netdevice->netdev);
}

/*static void vnd_chan_wake_queue(struct ks_chan *ks_chan)
{
	struct vnd_netdevice *netdevice = ks_chan->chan->driver_data;

	netif_wake_queue(netdevice->netdev);
}

static void vnd_chan_tx_error(
	struct ks_chan *ks_chan,
	enum ks_chan_tx_error_code code)
{
	struct vnd_netdevice *netdevice = ks_chan->chan->driver_data;

	switch(code) {
	case KSS_RX_ERROR_DROPPED:
		netdevice->stats.tx_errors++;
		netdevice->stats.tx_dropped++;
	break;

	case KSS_RX_ERROR_LENGTH:
		netdevice->stats.tx_errors++;
		netdevice->stats.tx_length_errors++;
	break;

	case KSS_RX_ERROR_CRC:
		netdevice->stats.tx_errors++;
		netdevice->stats.tx_crc_errors++;
	break;

	case KSS_RX_ERROR_FR_ABORT:
		netdevice->stats.tx_errors++;
		netdevice->stats.collisions++;
	break;
	}
}

static void vnd_chan_tx_error(
	struct ks_chan *ks_chan,
	enum ks_chan_tx_error_code code)
{
	struct vnd_netdevice *netdevice = ks_chan->chan->driver_data;

	switch(code) {
	case KSS_TX_ERROR_FIFO_FULL:
		netdevice->stats.tx_errors++;
		netdevice->stats.tx_fifo_errors++;
	break;
	}
}*/

static struct ks_chan_ops vnd_chan_d_tx_ops = {
	.owner			= THIS_MODULE,
	.release		= vnd_chan_d_tx_release,
	.connect		= vnd_chan_d_tx_connect,
	.disconnect		= vnd_chan_d_tx_disconnect,
	.open			= vnd_chan_d_tx_open,
	.close			= vnd_chan_d_tx_close,
	.start			= vnd_chan_d_tx_start,
	.stop			= vnd_chan_d_tx_stop,
};

static struct kss_chan_to_ops vnd_chan_d_tx_softswitch_ops = {
	.wake_queue		= vnd_chan_d_tx_wake_queue,

//	.rx_error		= vnd_chan_rx_error,
//	.tx_error		= vnd_chan_tx_error,
};

/*---------------------------------------------------------------------------*/

static void vnd_chan_e_rx_release(struct ks_chan *ks_chan)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_chan, struct vnd_netdevice, ks_chan_e_rx);

	vnd_debug_nd(netdevice, 3, "vnd_chan_e_rx_release()\n");

	vnd_netdevice_put(netdevice);
}

static int vnd_chan_e_rx_open(struct ks_chan *ks_chan)
{
//	struct vnd_netdevice *netdevice =
//		container_of(ks_chan, struct vnd_netdevice, ks_chan_e_rx);

	vnd_debug_nd(netdevice, 3, "vnd_chan_e_rx_open()\n");

	return 0;
}

static void vnd_chan_e_rx_close(struct ks_chan *ks_chan)
{
//	struct vnd_netdevice *netdevice =
//		container_of(ks_chan, struct vnd_netdevice, ks_chan_e_rx);

	vnd_debug_nd(netdevice, 3, "vnd_chan_e_rx_close()\n");
}

static int vnd_chan_e_rx_push_frame(
	struct ks_chan *ks_chan,
	struct sk_buff *skb)
{
	struct lapd_prim_hdr *prim_hdr;
	struct vnd_netdevice *netdevice =
		container_of(ks_chan, struct vnd_netdevice, ks_chan_e_rx);

	/* If frame matches a previously sent frame, it is our echo, so,
	 * we may ignore it */
	if (crc32_le(0, skb->data, skb->len) == netdevice->last_crc)
		return 0;

	netdevice->netdev->last_rx = jiffies;

	netdevice->stats.rx_packets++;
	netdevice->stats.rx_bytes += skb->len;

	skb->protocol = htons(ETH_P_LAPD);
	skb->dev = netdevice->netdev;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	skb_push(skb, sizeof(struct lapd_prim_hdr));
	prim_hdr = (struct lapd_prim_hdr *)skb->data;
	prim_hdr->primitive_type = LAPD_PH_DATA_INDICATION;

	return netif_rx(skb);
}

static int vnd_chan_e_rx_connect(struct ks_chan *ks_chan)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_chan, struct vnd_netdevice, ks_chan_e_rx);
	struct ks_pipeline *pipeline = netdevice->ks_chan_d_tx.pipeline;
	struct ks_node *remote_node;
	int err;

	vnd_debug_nd(netdevice, 3, "E RX connected\n");

	remote_node = ks_pipeline_last_node(pipeline);

	err = sysfs_create_link(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
		&netdevice->netdev->class_dev.kobj,
#else
		&netdevice->netdev->dev.kobj,
#endif
		&remote_node->kobj,
			VND_CONNECTED_NODE_SYMLINK_E);
	if (err < 0)
		goto err_create_connected_node_chan;

	/* Ensure the queue status is restored */
	netif_wake_queue(netdevice->netdev);

	return 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
	sysfs_remove_link(&netdevice->netdev->class_dev.kobj,
			VND_CONNECTED_NODE_SYMLINK_E);
#else
	sysfs_remove_link(&netdevice->netdev->dev.kobj,
			VND_CONNECTED_NODE_SYMLINK_E);
#endif
err_create_connected_node_chan:

	return err;
}

static void vnd_chan_e_rx_disconnect(struct ks_chan *ks_chan)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_chan, struct vnd_netdevice, ks_chan_e_rx);

	vnd_debug_nd(netdevice, 3, "vnd_chan_e_rx_disconnect()\n");

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
	sysfs_remove_link(&netdevice->netdev->class_dev.kobj,
			VND_CONNECTED_NODE_SYMLINK_E);
#else
	sysfs_remove_link(&netdevice->netdev->dev.kobj,
			VND_CONNECTED_NODE_SYMLINK_E);
#endif
}

static struct ks_chan_ops vnd_chan_e_rx_ops = {
	.owner			= THIS_MODULE,
	.release		= vnd_chan_e_rx_release,
	.connect		= vnd_chan_e_rx_connect,
	.disconnect		= vnd_chan_e_rx_disconnect,
	.open			= vnd_chan_e_rx_open,
	.close			= vnd_chan_e_rx_close,
};

static struct kss_chan_from_ops vnd_chan_e_rx_node_ops = {
	.push_frame		= vnd_chan_e_rx_push_frame,

//	.rx_error		= vnd_chan_rx_error,
//	.tx_error		= vnd_chan_tx_error,
};

/*---------------------------------------------------------------------------*/

static void vnd_node_d_release(struct ks_node *ks_node)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_node, struct vnd_netdevice, ks_node_d);

	vnd_debug_nd(netdevice, 3, "vnd_node_d_release()\n");

	vnd_netdevice_put(netdevice);
}

static struct ks_node_ops vnd_node_d_ops = {
	.owner			= THIS_MODULE,

	.release		= vnd_node_d_release,
};

/*---------------------------------------------------------------------------*/

static void vnd_node_e_release(struct ks_node *ks_node)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_node, struct vnd_netdevice, ks_node_e);

	vnd_debug_nd(netdevice, 3, "vnd_node_e_release()\n");

	vnd_netdevice_put(netdevice);
}

static struct ks_node_ops vnd_node_e_ops = {
	.owner			= THIS_MODULE,

	.release		= vnd_node_e_release,
};

/*---------------------------------------------------------------------------*/

static void vnd_duplex_d_release(struct ks_duplex *ks_duplex)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_duplex, struct vnd_netdevice,
						ks_duplex_d);

	vnd_debug_nd(netdevice, 3, "vnd_duplex_d_release()\n");

	vnd_netdevice_put(netdevice);
}

static struct ks_duplex_ops vnd_duplex_d_ops = {
	.owner			= THIS_MODULE,

	.release		= vnd_duplex_d_release,
};

/*---------------------------------------------------------------------------*/

static void vnd_duplex_e_release(struct ks_duplex *ks_duplex)
{
	struct vnd_netdevice *netdevice =
		container_of(ks_duplex, struct vnd_netdevice,
						ks_duplex_e);

	vnd_debug_nd(netdevice, 3, "vnd_duplex_e_release()\n");

	vnd_netdevice_put(netdevice);
}

static struct ks_duplex_ops vnd_duplex_e_ops = {
	.owner			= THIS_MODULE,

	.release		= vnd_duplex_e_release,
};

/*---------------------------------------------------------------------------*/

static int vnd_netdev_open(struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;
	struct ks_pipeline *pipeline_rx;
	struct ks_pipeline *pipeline_tx;
	int err;

	set_bit(VND_NETDEVICE_STATE_RTNL_HELD, &netdevice->state);

	/* RX */
	pipeline_rx = netdevice->ks_chan_d_rx.pipeline;
	if (!pipeline_rx) {
		err = -ENODEV;
		goto err_no_pipeline_rx;
	}

	err = ks_pipeline_change_status(pipeline_rx,
			KS_PIPELINE_STATUS_FLOWING);
	if (err < 0)
		goto err_pipeline_start_rx;

	/* TX */
	pipeline_tx = netdevice->ks_chan_d_tx.pipeline;
	if (!pipeline_tx) {
		err = -ENODEV;
		goto err_no_pipeline_tx;
	}

	err = ks_pipeline_change_status(pipeline_tx,
			KS_PIPELINE_STATUS_FLOWING);
	if (err < 0)
		goto err_pipeline_start_tx;

	/******/

	clear_bit(VND_NETDEVICE_STATE_RTNL_HELD, &netdevice->state);

	vnd_debug(3, "vnd_netdev_open()\n");

	return 0;

	ks_pipeline_change_status(pipeline_tx, KS_PIPELINE_STATUS_CONNECTED);
err_pipeline_start_tx:
//	ks_pipeline_put(pipeline_tx);
err_no_pipeline_tx:
	ks_pipeline_change_status(pipeline_rx, KS_PIPELINE_STATUS_CONNECTED);
err_pipeline_start_rx:
//	ks_pipeline_put(pipeline_rx);
err_no_pipeline_rx:

	return err;
}

static int vnd_netdev_stop(struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	vnd_debug(3, "vnd_netdev_stop()\n");

	cancel_delayed_work(&netdevice->promiscuity_change_work);
	flush_scheduled_work();

	if (netdevice->ks_chan_d_rx.pipeline)
		ks_pipeline_change_status(netdevice->ks_chan_d_rx.pipeline,
				KS_PIPELINE_STATUS_CONNECTED);

	if (netdevice->ks_chan_d_tx.pipeline)
		ks_pipeline_change_status(netdevice->ks_chan_d_tx.pipeline,
				KS_PIPELINE_STATUS_CONNECTED);

	if (netdevice->ks_chan_e_rx.pipeline)
		ks_pipeline_change_status(netdevice->ks_chan_e_rx.pipeline,
				KS_PIPELINE_STATUS_CONNECTED);

	return 0;
}

static int vnd_netdev_hard_start_xmit(
	struct sk_buff *skb,
	struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;
	struct lapd_prim_hdr *prim_hdr;
	int res;

	netdev->trans_start = jiffies;

	netdevice->stats.tx_packets++;
	netdevice->stats.tx_bytes += skb->len + 2;

	if (netdevice->netdev->flags & IFF_PROMISC)
		netdevice->last_crc = crc32_le(0, skb->data, skb->len);

	prim_hdr = (struct lapd_prim_hdr *)skb->data;

	switch(prim_hdr->primitive_type) {
	case LAPD_PH_DATA_REQUEST:
		skb_pull(skb, sizeof(struct lapd_prim_hdr));
		res = kss_chan_push_frame(&netdevice->ks_chan_d_tx, skb);
		switch(res) {
		case KSS_TX_OK:
			return NETDEV_TX_OK;
		case KSS_TX_FULL:
			netif_stop_queue(netdevice->netdev);
			skb_push(skb, sizeof(struct lapd_prim_hdr));
			return NETDEV_TX_BUSY;
		case KSS_TX_BUSY:
			skb_push(skb, sizeof(struct lapd_prim_hdr));
			return NETDEV_TX_BUSY;
		case KSS_TX_LOCKED:
			skb_push(skb, sizeof(struct lapd_prim_hdr));
			return NETDEV_TX_LOCKED;
		default:
			kfree_skb(skb);
			return NETDEV_TX_OK;
		}
	break;

	case LAPD_PH_ACTIVATE_REQUEST:
		visdn_port_activate(netdevice->remote_port);
		kfree_skb(skb);
		return NETDEV_TX_OK;
	break;

	case LAPD_MPH_DEACTIVATE_REQUEST:
		visdn_port_deactivate(netdevice->remote_port);
		kfree_skb(skb);
		return NETDEV_TX_OK;
	break;

	default:
		printk(KERN_ERR "Unexpected primitive %d\n",
				prim_hdr->primitive_type);
		WARN_ON(1);
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}
}

static struct net_device_stats *vnd_netdev_get_stats(
	struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	return &netdevice->stats;
}

static void vnd_netdev_set_multicast_list(
	struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	vnd_netdevice_get(netdevice);
	if (!schedule_delayed_work(&netdevice->promiscuity_change_work, 0))
		vnd_netdevice_put(netdevice);
}

/* set_multicast_list is called in atomic context, so, we need a deferred
   work in order to be able to call functions like visdn_open which may
   sleep.

   This function is a little racy but races shouldn't be much harmful
*/

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void vnd_promiscuity_change_work(void *data)
{
	struct vnd_netdevice *netdevice = data;
#else
static void vnd_promiscuity_change_work(struct work_struct *work)
{
	struct vnd_netdevice *netdevice =
		container_of((struct delayed_work *)work,
				struct vnd_netdevice,
				promiscuity_change_work);
#endif
	struct ks_pipeline *pipeline;

	pipeline = netdevice->ks_chan_e_rx.pipeline;
	if (pipeline) {
		if (netdevice->netdev->flags & IFF_PROMISC) {
			// FIXME: Handle failures
			ks_pipeline_change_status(pipeline,
					KS_PIPELINE_STATUS_FLOWING);
		} else if(!(netdevice->netdev->flags & IFF_PROMISC)) {
			ks_pipeline_change_status(pipeline,
					KS_PIPELINE_STATUS_CONNECTED);
		}
	}

	vnd_netdevice_put(netdevice);
}

static int vnd_netdev_do_ioctl(
	struct net_device *netdev,
	struct ifreq *ifr, int cmd)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	if (!netdevice->remote_port) {
		WARN_ON(1);
		return -EIO;
	}

	switch(cmd) {
	case LAPD_DEV_IOC_ACTIVATE:
		visdn_port_activate(netdevice->remote_port);
	break;

	case LAPD_DEV_IOC_DEACTIVATE:
		visdn_port_deactivate(netdevice->remote_port);
	break;
	}

	return -EOPNOTSUPP;
}

static int vnd_netdev_change_mtu(
	struct net_device *netdev,
	int new_mtu)
{
	struct vnd_netdevice *netdevice = netdev->priv;

	// LOCKING*********** FIXME
	if (netdevice->ks_chan_d_tx.pipeline &&
	    new_mtu <= netdevice->ks_chan_d_tx.pipeline->mtu)
		netdev->mtu = new_mtu;
	else
		return -EINVAL;

	return 0;
}

static int vnd_cdev_open(
	struct inode *inode,
	struct file *file)
{
	nonseekable_open(inode, file);

	return 0;
}

static int vnd_cdev_release(
	struct inode *inode, struct file *file)
{
	return 0;
}

static int lapd_mac_addr(struct net_device *dev, void *addr)
{
	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
static int lapd_hard_header_parse(struct sk_buff *skb, unsigned char *haddr)
#else
static int lapd_hard_header_parse(
	const struct sk_buff *skb,
	unsigned char *haddr)
#endif
{
	if(!skb->dev)
		return 0;

	haddr[0] = skb->dev->dev_addr[0];

	return 1;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
#else
const struct header_ops lapd_header_ops ____cacheline_aligned = {
	.create		= NULL,
	.parse		= lapd_hard_header_parse,
	.rebuild	= NULL,
	.cache		= NULL,
	.cache_update	= NULL,
};
#endif

static void setup_lapd(struct net_device *netdev)
{
	netdev->change_mtu = NULL;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	netdev->rebuild_header = NULL;
	netdev->hard_header = NULL;
	netdev->hard_header_cache = NULL;
	netdev->hard_header_len = 0;
	netdev->hard_header_parse = lapd_hard_header_parse;
	netdev->header_cache_update = NULL;
#else
	netdev->header_ops = &lapd_header_ops;
#endif

	netdev->set_mac_address = lapd_mac_addr;

	netdev->type = ARPHRD_LAPD;
	netdev->addr_len = 1;
	netdev->tx_queue_len = 10;

	memset(netdev->broadcast, 0, sizeof(netdev->broadcast));

	netdev->flags = 0;
}

static void vnd_send_primitive(
	struct visdn_port *port,
	u8 primitive_type, u8 param1)
{
	struct vnd_netdevice *netdevice;
	unsigned long flags;

	spin_lock_irqsave(&vnd_netdevices_list_lock, flags);

	list_for_each_entry(netdevice, &vnd_netdevices_list, list_node) {
		struct sk_buff *skb;
		struct lapd_prim_hdr *prim_hdr;
		struct lapd_ctrl_hdr *ctrl_hdr;

		if (netdevice->remote_port != port)
			continue;

		skb = alloc_skb(sizeof(struct lapd_ctrl_hdr), GFP_ATOMIC);
		if (!skb) {
			spin_unlock_irqrestore(&vnd_netdevices_list_lock,
								flags);
			return;
		}

		skb->protocol = __constant_htons(ETH_P_LAPD);
		skb->dev = netdevice->netdev;
		skb->pkt_type = PACKET_HOST;
		skb->ip_summed = CHECKSUM_UNNECESSARY;

		prim_hdr = (struct lapd_prim_hdr *)
			skb_put(skb, sizeof(struct lapd_prim_hdr));

		ctrl_hdr = (struct lapd_ctrl_hdr *)
			skb_put(skb, sizeof(struct lapd_ctrl_hdr));

		prim_hdr->primitive_type = primitive_type;
		ctrl_hdr->param = param1;

		netif_rx(skb);
	}

	spin_unlock_irqrestore(&vnd_netdevices_list_lock, flags);
}

static void vnd_port_connected(struct visdn_port *visdn_port)
{
	vnd_send_primitive(visdn_port,
		LAPD_MPH_INFORMATION_INDICATION,
		LAPD_MPH_II_CONNECTED);
}

static void vnd_port_disconnected(struct visdn_port *visdn_port)
{
	vnd_send_primitive(visdn_port,
		LAPD_MPH_INFORMATION_INDICATION,
		LAPD_MPH_II_DISCONNECTED);
}

static void vnd_port_activated(struct visdn_port *visdn_port)
{
	vnd_send_primitive(visdn_port, LAPD_PH_ACTIVATE_INDICATION, 0);
	vnd_send_primitive(visdn_port, LAPD_MPH_ACTIVATE_INDICATION, 0);
}

static void vnd_port_deactivated(struct visdn_port *visdn_port)
{
	vnd_send_primitive(visdn_port, LAPD_PH_DEACTIVATE_INDICATION, 0);
	vnd_send_primitive(visdn_port, LAPD_MPH_DEACTIVATE_INDICATION, 0);
}

static void vnd_port_error_indication(struct visdn_port *visdn_port, u8 value)
{
	vnd_send_primitive(visdn_port, LAPD_MPH_ERROR_INDICATION, value);
}

static int vnd_notifier_event(
	struct notifier_block *this,
	unsigned long event,
	void *ptr)
{
	struct visdn_port *port = (struct visdn_port *)ptr;

	switch(event) {
	case VISDN_EVENT_PORT_CONNECTED:
		vnd_port_connected(port);
	break;

	case VISDN_EVENT_PORT_DISCONNECTED:
		vnd_port_disconnected(port);
	break;

	case VISDN_EVENT_PORT_ACTIVATED:
		vnd_port_activated(port);
	break;

	case VISDN_EVENT_PORT_DEACTIVATED:
		vnd_port_deactivated(port);
	break;

	case VISDN_EVENT_PORT_ERROR_INDICATION_0:
		vnd_port_error_indication(port, 0);
	break;

	case VISDN_EVENT_PORT_ERROR_INDICATION_1:
		vnd_port_error_indication(port, 1);
	break;

	case VISDN_EVENT_PORT_ERROR_INDICATION_2:
		vnd_port_error_indication(port, 2);
	break;

	case VISDN_EVENT_PORT_ERROR_INDICATION_3:
		vnd_port_error_indication(port, 3);
	break;

	case VISDN_EVENT_PORT_ERROR_INDICATION_4:
		vnd_port_error_indication(port, 4);
	break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block vnd_notifier = {
	.notifier_call = vnd_notifier_event,
};

static void vnd_netdevice_init(
	struct vnd_netdevice *netdevice,
	const char *name)
{
	memset(netdevice, 0, sizeof(*netdevice));

	kref_init(&netdevice->kref);

	snprintf(netdevice->name, sizeof(netdevice->name), "%s", name);

	/*************** D channel ***************/

	{
	char tmpstr[32];

	snprintf(tmpstr,
		sizeof(tmpstr),
		"%s_d",
		name);

	ks_node_create(&netdevice->ks_node_d,
			&vnd_node_d_ops, tmpstr,
			&visdn_system_device.kobj);
	}

	ks_duplex_create(&netdevice->ks_duplex_d,
			&vnd_duplex_d_ops,
			"duplex",
			&netdevice->ks_node_d.kobj);

	ks_chan_create(&netdevice->ks_chan_d_rx,
			&vnd_chan_d_rx_ops, "rx",
			&netdevice->ks_duplex_d,
			&netdevice->ks_duplex_d.kobj,
			&kss_softswitch.ks_node,
			&netdevice->ks_node_d);

	netdevice->ks_chan_d_rx.from_ops = &vnd_chan_d_rx_softswitch_ops;

	ks_chan_create(&netdevice->ks_chan_d_tx,
			&vnd_chan_d_tx_ops, "tx",
			&netdevice->ks_duplex_d,
			&netdevice->ks_duplex_d.kobj,
			&netdevice->ks_node_d,
			&kss_softswitch.ks_node);

	netdevice->ks_chan_d_tx.to_ops = &vnd_chan_d_tx_softswitch_ops;

	/*************** E channel ***************/

	{
	char tmpstr[32];

	snprintf(tmpstr,
		sizeof(tmpstr),
		"%s_e",
		name);

	ks_node_create(&netdevice->ks_node_e,
			&vnd_node_e_ops, tmpstr,
			&visdn_system_device.kobj);
	}

	ks_duplex_create(&netdevice->ks_duplex_e,
			&vnd_duplex_e_ops,
			"duplex",
			&netdevice->ks_node_e.kobj);

	ks_chan_create(&netdevice->ks_chan_e_rx,
			&vnd_chan_e_rx_ops, "rx",
			&netdevice->ks_duplex_e,
			&netdevice->ks_duplex_e.kobj,
			&kss_softswitch.ks_node,
			&netdevice->ks_node_e);

	netdevice->ks_chan_e_rx.from_ops = &vnd_chan_e_rx_node_ops;

	/*****************************************/

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	INIT_WORK(&netdevice->promiscuity_change_work,
		vnd_promiscuity_change_work,
		netdevice);
#else
	INIT_DELAYED_WORK(&netdevice->promiscuity_change_work,
		vnd_promiscuity_change_work);
#endif
}

static struct vnd_netdevice *vnd_netdevice_alloc(const char *name)
{
	struct vnd_netdevice *netdevice;

	netdevice = kmalloc(sizeof(*netdevice), GFP_KERNEL);
	if (!netdevice)
		return NULL;

	vnd_netdevice_init(netdevice, name);

	return netdevice;
}

static int vnd_netdevice_register(
	struct vnd_netdevice *netdevice,
	struct vnd_create *create)
{
	void (*setup_func)(struct net_device *) = NULL;
	unsigned long flags;
	int err;

	switch(create->protocol) {
	case ARPHRD_LAPD:
		setup_func = setup_lapd;
	break;

/*	case ARPHRD_MTP2:
		setup_func = setup_mtp2;
	break;*/

	default:
		err = -EINVAL;
		goto err_unsupported_protocol;
	}

	{
	struct vnd_netdevice *nd;
	spin_lock_irqsave(&vnd_netdevices_list_lock, flags);
	nd = _vnd_netdevice_get_by_name(create->devname);
	if (nd) {
		err = -EEXIST;
		vnd_netdevice_put(nd);
		spin_unlock_irqrestore(&vnd_netdevices_list_lock, flags);
		goto err_already_exists;
	}

	list_add_tail(&vnd_netdevice_get(netdevice)->list_node,
			&vnd_netdevices_list);

	spin_unlock_irqrestore(&vnd_netdevices_list_lock, flags);
	}

	/* D channel */

	vnd_netdevice_get(netdevice); /* Container is implicitly used */
	err = ks_node_register(&netdevice->ks_node_d);
	if (err < 0)
		goto err_ks_node_d_register;

	vnd_netdevice_get(netdevice); /* Container is implicitly used */
	err = ks_duplex_register(&netdevice->ks_duplex_d);
	if (err < 0)
		goto err_ks_duplex_d_register;

	vnd_netdevice_get(netdevice); /* Container is implicitly used */
	err = ks_chan_register(&netdevice->ks_chan_d_rx);
	if (err < 0)
		goto err_ks_chan_d_rx_register;

	vnd_netdevice_get(netdevice); /* Container is implicitly used */
	err = ks_chan_register(&netdevice->ks_chan_d_tx);
	if (err < 0)
		goto err_ks_chan_d_tx_register;

	/* E channel */

	vnd_netdevice_get(netdevice); /* Container is implicitly used */
	err = ks_node_register(&netdevice->ks_node_e);
	if (err < 0)
		goto err_ks_node_e_register;

	vnd_netdevice_get(netdevice); /* Container is implicitly used */
	err = ks_duplex_register(&netdevice->ks_duplex_e);
	if (err < 0)
		goto err_ks_duplex_e_register;

	vnd_netdevice_get(netdevice); /* Container is implicitly used */
	err = ks_chan_register(&netdevice->ks_chan_e_rx);
	if (err < 0)
		goto err_ks_chan_e_rx_register;

	{
	netdevice->type = create->protocol;
	netdevice->state = 0;
	netdevice->netdev = alloc_netdev(0, create->devname, setup_func);
	if(!netdevice->netdev) {
		vnd_msg(KERN_ERR, "net_device alloc failed, abort.\n");
		err = -ENOMEM;
		goto err_alloc_netdev;
	}
	}

	netdevice->netdev->priv = netdevice;
	netdevice->netdev->open = vnd_netdev_open;
	netdevice->netdev->stop = vnd_netdev_stop;
	netdevice->netdev->hard_start_xmit = vnd_netdev_hard_start_xmit;
	netdevice->netdev->get_stats = vnd_netdev_get_stats;
	netdevice->netdev->set_multicast_list = vnd_netdev_set_multicast_list;
	netdevice->netdev->do_ioctl = vnd_netdev_do_ioctl;
	netdevice->netdev->change_mtu = vnd_netdev_change_mtu;
	netdevice->netdev->features = 0;

	memset(netdevice->netdev->dev_addr, 0,
		sizeof(netdevice->netdev->dev_addr));

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	SET_MODULE_OWNER(netdevice->netdev);
#endif

	netdevice->netdev->irq = 0;
	netdevice->netdev->base_addr = 0;

	err = register_netdev(netdevice->netdev);
	if (err < 0) {
		vnd_msg(KERN_ERR, "Cannot register net device %s, aborting.\n",
			netdevice->netdev->name);
		goto err_register_netdev;
	}

	err = sysfs_create_link(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
		&netdevice->netdev->class_dev.kobj,
#else
		&netdevice->netdev->dev.kobj,
#endif
		&netdevice->ks_node_d.kobj,
			VND_NODE_SYMLINK_D);
	if (err < 0)
		goto err_create_symchan_d;

	err = sysfs_create_link(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
		&netdevice->netdev->class_dev.kobj,
#else
		&netdevice->netdev->dev.kobj,
#endif
		&netdevice->ks_node_e.kobj,
			VND_NODE_SYMLINK_E);
	if (err < 0)
		goto err_create_symchan_e;

/*	{
	char tmpstr[80];
	d_path(netdevice->ks_chan_d_tx.kobj.dentry, sysfs_mount, tmpstr, sizeof(tmpstr));

	printk(KERN_CRIT, "AAAAAAAAAAAAAAAAa %s\n", tmpstr);
	}*/

	return 0;

	sysfs_remove_link(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
		&netdevice->netdev->class_dev.kobj,
#else
		&netdevice->netdev->dev.kobj,
#endif
		VND_NODE_SYMLINK_E);
err_create_symchan_e:
	sysfs_remove_link(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
		&netdevice->netdev->class_dev.kobj,
#else
		&netdevice->netdev->dev.kobj,
#endif
		VND_NODE_SYMLINK_D);
err_create_symchan_d:
	unregister_netdev(netdevice->netdev);
err_register_netdev:
	free_netdev(netdevice->netdev);
err_alloc_netdev:
	ks_chan_unregister(&netdevice->ks_chan_e_rx);
	ks_chan_put(&netdevice->ks_chan_e_rx);
err_ks_chan_e_rx_register:
	ks_duplex_unregister(&netdevice->ks_duplex_e);
	ks_duplex_put(&netdevice->ks_duplex_e);
err_ks_duplex_e_register:
	ks_node_unregister(&netdevice->ks_node_e);
	ks_node_put(&netdevice->ks_node_e);
err_ks_node_e_register:
	ks_chan_unregister(&netdevice->ks_chan_d_tx);
	ks_chan_put(&netdevice->ks_chan_d_tx);
err_ks_chan_d_tx_register:
	ks_chan_unregister(&netdevice->ks_chan_d_rx);
	ks_chan_put(&netdevice->ks_chan_d_rx);
err_ks_chan_d_rx_register:
	ks_duplex_unregister(&netdevice->ks_duplex_d);
	ks_duplex_put(&netdevice->ks_duplex_d);
err_ks_duplex_d_register:
	ks_node_unregister(&netdevice->ks_node_d);
	ks_node_put(&netdevice->ks_node_d);
err_ks_node_d_register:
	spin_lock_irqsave(&vnd_netdevices_list_lock, flags);
	list_del(&netdevice->list_node);
	spin_unlock_irqrestore(&vnd_netdevices_list_lock, flags);
err_already_exists:
err_unsupported_protocol:

	return err;
}

static void vnd_netdevice_unregister(struct vnd_netdevice *netdevice)
{
	unsigned long flags;

	sysfs_remove_link(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
		&netdevice->netdev->class_dev.kobj,
#else
		&netdevice->netdev->dev.kobj,
#endif
		VND_NODE_SYMLINK_E);

	sysfs_remove_link(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
		&netdevice->netdev->class_dev.kobj,
#else
		&netdevice->netdev->dev.kobj,
#endif
		VND_NODE_SYMLINK_D);

	unregister_netdev(netdevice->netdev);

	/* E channel */
	ks_chan_unregister(&netdevice->ks_chan_e_rx);
	ks_chan_put(&netdevice->ks_chan_e_rx);

	ks_duplex_unregister(&netdevice->ks_duplex_e);
	ks_duplex_put(&netdevice->ks_duplex_e);

	ks_node_unregister(&netdevice->ks_node_e);
	ks_node_put(&netdevice->ks_node_e);

	/* D channel */
	ks_chan_unregister(&netdevice->ks_chan_d_tx);
	ks_chan_put(&netdevice->ks_chan_d_tx);

	ks_chan_unregister(&netdevice->ks_chan_d_rx);
	ks_chan_put(&netdevice->ks_chan_d_rx);

	ks_duplex_unregister(&netdevice->ks_duplex_d);
	ks_duplex_put(&netdevice->ks_duplex_d);

	ks_node_unregister(&netdevice->ks_node_d);
	ks_node_put(&netdevice->ks_node_d);

	spin_lock_irqsave(&vnd_netdevices_list_lock, flags);
	list_del_init(&netdevice->list_node);
	vnd_netdevice_put(netdevice);
	spin_unlock_irqrestore(&vnd_netdevices_list_lock, flags);
}

static int vnd_cdev_ioctl_do_create(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	int err;
	struct vnd_create create;
	struct vnd_netdevice *netdevice;

	if (arg < sizeof(create)) {
		err = -EINVAL;
		goto err_invalid_sizeof_create;
	}

	if (copy_from_user(&create, (void *)arg, sizeof(create))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	netdevice = vnd_netdevice_alloc(create.devname);
	if (!netdevice) {
		err = -ENOMEM;
		goto err_netdevice_alloc;
	}

	err = vnd_netdevice_register(netdevice, &create);
	if (err < 0)
		goto err_netdevice_register;

	create.d_chan = netdevice->ks_node_d.id;
	create.e_chan = netdevice->ks_node_e.id;

	if (copy_to_user((void *)arg, &create, sizeof(create))) {
		err = -EFAULT;
		goto err_copy_to_user;
	}

	vnd_netdevice_put(netdevice);

	return 0;

err_copy_to_user:
	vnd_netdevice_unregister(netdevice);
err_netdevice_register:
	vnd_netdevice_put(netdevice);
err_netdevice_alloc:
err_copy_from_user:
err_invalid_sizeof_create:

	return err;
}

static int vnd_cdev_ioctl_do_destroy(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct vnd_create create;
	struct vnd_netdevice *netdevice = NULL;
	int err;

	if (arg < sizeof(create)) {
		err = -EINVAL;
		goto err_invalid_sizeof_create;
	}

	if (copy_from_user(&create, (void *)arg, sizeof(create))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	netdevice = vnd_netdevice_get_by_name(create.devname);
	if (!netdevice) {
		err = -ENODEV;
		goto err_no_device;
	}

	vnd_netdevice_unregister(netdevice);
	vnd_netdevice_put(netdevice);
	netdevice = NULL;

	return 0;

	vnd_netdevice_put(netdevice);
err_no_device:
err_copy_from_user:
err_invalid_sizeof_create:

	return err;
}

static int vnd_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	switch(cmd) {
	case VND_IOC_CREATE:
		return vnd_cdev_ioctl_do_create(inode, file, cmd, arg);
	break;

	case VND_IOC_DESTROY:
		return vnd_cdev_ioctl_do_destroy(inode, file, cmd, arg);
	break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static struct file_operations vnd_fops =
{
	.owner		= THIS_MODULE,
	.open		= vnd_cdev_open,
	.release	= vnd_cdev_release,
	.llseek		= no_llseek,
	.ioctl		= vnd_cdev_ioctl,
};

#ifndef HAVE_CLASS_DEV_DEVT
static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	return print_dev_t(buf, vnd_first_dev);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);
#endif

static int __init vnd_init_module(void)
{
	int err;

	vnd_msg(KERN_INFO, vnd_MODULE_DESCR " loading\n");

	err = alloc_chrdev_region(&vnd_first_dev, 0, 1, vnd_MODULE_NAME);
	if (err < 0)
		goto err_register_chrdev;

	cdev_init(&vnd_cdev, &vnd_fops);
	vnd_cdev.owner = THIS_MODULE;

	err = cdev_add(&vnd_cdev, vnd_first_dev, 1);
	if (err < 0)
		goto err_cdev_add;

	vnd_control_device.class = &visdn_system_class;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)
	vnd_control_device.class_data = NULL;

	snprintf(vnd_control_device.class_id,
		sizeof(vnd_control_device.class_id),
		"netdev-control");
#endif

#ifdef HAVE_CLASS_DEV_DEVT
	vnd_control_device.devt = vnd_first_dev;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	err = class_device_register(&vnd_control_device);
	if (err < 0)
		goto err_control_device_register;
#else
	err = device_register(&vnd_control_device);
	if (err < 0)
		goto err_control_device_register;
#endif

#ifndef HAVE_CLASS_DEV_DEVT
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	class_device_create_file(
		&vnd_control_device,
		&class_device_attr_dev);
#else
	device_create_file(
		&vnd_control_device,
		&device_attr_dev);
#endif
#endif

	visdn_register_notifier(&vnd_notifier);

	return 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	class_device_del(&vnd_control_device);
#else
	device_del(&vnd_control_device);
#endif
err_control_device_register:
	cdev_del(&vnd_cdev);
err_cdev_add:
	unregister_chrdev_region(vnd_first_dev, 1);
err_register_chrdev:

	return err;
}

module_init(vnd_init_module);

static void __exit vnd_module_exit(void)
{
	struct vnd_netdevice *netdevice, *t;

	visdn_unregister_notifier(&vnd_notifier);

#ifndef HAVE_CLASS_DEV_DEVT
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	class_device_remove_file(
		&vnd_control_device,
		&class_device_attr_dev);
#else
	device_remove_file(
		&vnd_control_device,
		&device_attr_dev);
#endif
#endif

	/* Ensure no one can open/ioctl cdevs before removing netdevices */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	class_device_del(&vnd_control_device);
#else
	device_del(&vnd_control_device);
#endif
	cdev_del(&vnd_cdev);
	unregister_chrdev_region(vnd_first_dev, 1);

	/* No one else can access vnd_netdevices_list, so, no locking */
	list_for_each_entry_safe(netdevice, t,
			&vnd_netdevices_list, list_node) {

		vnd_netdevice_get(netdevice);
		vnd_netdevice_unregister(netdevice);

		if (atomic_read(&netdevice->kref.refcount) > 1) {

			/* Usually 50ms are enough */
			msleep(50);

			while(atomic_read(&netdevice->kref.refcount) > 1) {
				vnd_msg(KERN_WARNING,
					"Waiting for netdevice"
					" refcnt to become 1"
					" (now %d)\n",
					atomic_read(&netdevice->kref.refcount));

				msleep(5000);
			}
		}

		vnd_netdevice_put(netdevice);
	}

	vnd_msg(KERN_INFO, vnd_MODULE_DESCR " unloaded\n");
}

module_exit(vnd_module_exit);

MODULE_DESCRIPTION(vnd_MODULE_DESCR);
MODULE_AUTHOR("Daniele (Vihai) Orlandi <daniele@orlandi.com>");
MODULE_LICENSE("GPL");

#ifdef DEBUG_CODE
module_param(debug_level, int, 0444);
MODULE_PARM_DESC(debug_level, "Initial debug level");
#endif
