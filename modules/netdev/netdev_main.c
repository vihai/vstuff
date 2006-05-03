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

#include <linux/visdn/core.h>
#include <linux/visdn/cxc.h>
#include <linux/visdn/softcxc.h>
#include <linux/visdn/port.h>
#include <linux/visdn/chan.h>
#include <linux/visdn/path.h>
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
static struct class_device vnd_control_class_dev;

static struct list_head vnd_netdevices_list =
					LIST_HEAD_INIT(vnd_netdevices_list);
static spinlock_t vnd_netdevices_list_lock = SPIN_LOCK_UNLOCKED;

static struct visdn_chan_class vnd_class_dchan =
{
	.name	= "d_channel",
};

static struct visdn_chan_class vnd_class_echan =
{
	.name	= "e_channel",
};

static struct visdn_port vnd_port;

static inline struct vnd_netdevice *vnd_netdevice_get(
	struct vnd_netdevice *netdevice)
{
	WARN_ON(atomic_read(&netdevice->refcnt) <= 0);

	atomic_inc(&netdevice->refcnt);

	return netdevice;
}

static inline void vnd_netdevice_put(
	struct vnd_netdevice *netdevice)
{
	WARN_ON(atomic_read(&netdevice->refcnt) <= 0);

	vnd_debug(3, "vnd_netdevice_put() refcnt=%d\n",
		atomic_read(&netdevice->refcnt) - 1);

	if (atomic_dec_and_test(&netdevice->refcnt)) {
		vnd_debug(3, "vnd_netdevice_put(): releasing\n");

		if (netdevice->netdev) {
			free_netdev(netdevice->netdev);
			netdevice->netdev = NULL;
		}

		kfree(netdevice);
	}
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

static void vnd_chan_release(struct visdn_chan *visdn_chan)
{
	struct vnd_netdevice *netdevice = visdn_chan->driver_data;

	vnd_debug(3, "vnd_chan_release()\n");

	vnd_netdevice_put(netdevice);
}

static int vnd_chan_open(struct visdn_chan *visdn_chan)
{
	struct vnd_netdevice *netdevice = visdn_chan->driver_data;
	struct visdn_path *path;
	struct visdn_chan *other_ep;

	vnd_debug(3, "vnd_chan_open()\n");

	path = visdn_path_get_by_endpoint(visdn_chan);
	if (!path)
		return -ENOTCONN;

	netdevice->mtu = visdn_path_find_lowest_mtu(path);

	other_ep = visdn_path_get_other_endpoint(path, visdn_chan);

	netdevice->remote_port = visdn_port_get(other_ep->port);

	visdn_chan_put(other_ep);
	visdn_path_put(path);

	if (!test_bit(VND_NETDEVICE_STATE_RTNL_HELD, &netdevice->state)) {
		rtnl_lock();
		dev_set_mtu(netdevice->netdev, netdevice->mtu);
		rtnl_unlock();
	} else {
		dev_set_mtu(netdevice->netdev, netdevice->mtu);
	}

	return 0;
}

static int vnd_chan_close(struct visdn_chan *visdn_chan)
{
	struct vnd_netdevice *netdevice = visdn_chan->driver_data;

	vnd_debug(3, "vnd_chan_close()\n");

	if (netdevice->remote_port)
		visdn_port_put(netdevice->remote_port);

	return 0;
}

static int vnd_chan_frame_xmit(
	struct visdn_leg *visdn_leg,
	struct sk_buff *skb)
{
	struct vnd_netdevice *netdevice = visdn_leg->chan->driver_data;
	struct lapd_prim_hdr *prim_hdr;

	netdevice->netdev->last_rx = jiffies;

	netdevice->stats.rx_packets++;
	netdevice->stats.rx_bytes += skb->len;

	skb->protocol = htons(ETH_P_LAPD);
	skb->dev = netdevice->netdev;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_HOST;

	skb_push(skb, sizeof(struct lapd_prim_hdr));
	prim_hdr = (struct lapd_prim_hdr *)skb->data;
	prim_hdr->primitive_type = LAPD_PH_DATA_INDICATION;

	return netif_rx(skb);
}

static int vnd_chan_connect(
	struct visdn_leg *visdn_leg1,
	struct visdn_leg *visdn_leg2)
{
	struct vnd_netdevice *netdevice = visdn_leg1->chan->driver_data;

	vnd_debug(2, "%06d connected to %06d\n",
		visdn_leg1->chan->id,
		visdn_leg2->chan->id);

	/* Ensure the queue status is restored */
	netif_wake_queue(netdevice->netdev);

	return 0;
}

static void vnd_chan_disconnect(
	struct visdn_leg *visdn_leg1,
	struct visdn_leg *visdn_leg2)
{
	struct vnd_netdevice *netdevice = visdn_leg1->chan->driver_data;

	vnd_debug(2, "%06d disconnected from %06d\n",
		visdn_leg1->chan->id,
		visdn_leg2->chan->id);

	rtnl_lock();
	dev_change_flags(netdevice->netdev,
			netdevice->netdev->flags & ~IFF_UP);
	rtnl_unlock();
}

static void vnd_chan_stop_queue(struct visdn_leg *visdn_leg)
{
	struct vnd_netdevice *netdevice = visdn_leg->chan->driver_data;

	netif_stop_queue(netdevice->netdev);
}

static void vnd_chan_start_queue(struct visdn_leg *visdn_leg)
{
	struct vnd_netdevice *netdevice = visdn_leg->chan->driver_data;

	netif_start_queue(netdevice->netdev);
}

static void vnd_chan_wake_queue(struct visdn_leg *visdn_leg)
{
	struct vnd_netdevice *netdevice = visdn_leg->chan->driver_data;

	netif_wake_queue(netdevice->netdev);
}

static void vnd_chan_rx_error(
	struct visdn_leg *visdn_leg,
	enum visdn_leg_rx_error_code code)
{
	struct vnd_netdevice *netdevice = visdn_leg->chan->driver_data;

	switch(code) {
	case VISDN_RX_ERROR_DROPPED:
		netdevice->stats.rx_errors++;
		netdevice->stats.rx_dropped++;
	break;

	case VISDN_RX_ERROR_LENGTH:
		netdevice->stats.rx_errors++;
		netdevice->stats.rx_length_errors++;
	break;

	case VISDN_RX_ERROR_CRC:
		netdevice->stats.rx_errors++;
		netdevice->stats.rx_crc_errors++;
	break;

	case VISDN_RX_ERROR_FR_ABORT:
		netdevice->stats.rx_errors++;
		netdevice->stats.collisions++;
	break;
	}
}

static void vnd_chan_tx_error(
	struct visdn_leg *visdn_leg,
	enum visdn_leg_tx_error_code code)
{
	struct vnd_netdevice *netdevice = visdn_leg->chan->driver_data;

	switch(code) {
	case VISDN_TX_ERROR_FIFO_FULL:
		netdevice->stats.tx_errors++;
		netdevice->stats.tx_fifo_errors++;
	break;
	}
}

static void vnd_chan_e_release(struct visdn_chan *visdn_chan)
{
	struct vnd_netdevice *netdevice = visdn_chan->driver_data;

	vnd_debug(3, "vnd_chan_e_release()\n");

	vnd_netdevice_put(netdevice);
}

static int vnd_chan_e_open(struct visdn_chan *visdn_chan)
{
	vnd_debug(3, "vnd_chan_e_open()\n");

	return 0;
}

static int vnd_chan_e_close(struct visdn_chan *visdn_chan)
{
	vnd_debug(3, "vnd_chan_e_close()\n");

	return 0;
}

static int vnd_chan_e_frame_xmit(
	struct visdn_leg *visdn_leg,
	struct sk_buff *skb)
{
	struct lapd_prim_hdr *prim_hdr;
	struct vnd_netdevice *netdevice = visdn_leg->chan->driver_data;

	/* If frame matches a previously sent frame, it is our echo, so,
	 * we may ignore it */
	if (crc32_le(0, skb->data, skb->len) == netdevice->last_crc)
		return 0;

	netdevice->netdev->last_rx = jiffies;

	netdevice->stats.rx_packets++;
	netdevice->stats.rx_bytes += skb->len;

	skb->protocol = htons(ETH_P_LAPD);
	skb->dev = netdevice->netdev;
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;

	skb_push(skb, sizeof(struct lapd_prim_hdr));
	prim_hdr = (struct lapd_prim_hdr *)skb->data;
	prim_hdr->primitive_type = LAPD_PH_DATA_INDICATION;

	return netif_rx(skb);
}

static void vnd_chan_e_rx_error(
	struct visdn_leg *visdn_leg,
	enum visdn_leg_rx_error_code code)
{
	return;
}

static void vnd_chan_e_tx_error(
	struct visdn_leg *visdn_leg,
	enum visdn_leg_tx_error_code code)
{
	return;
}

struct visdn_chan_ops vnd_chan_ops = {
	.owner			= THIS_MODULE,

	.release		= vnd_chan_release,
	.open			= vnd_chan_open,
	.close			= vnd_chan_close,
};

struct visdn_leg_ops vnd_chan_leg_ops = {
	.owner			= THIS_MODULE,

	.connect	  	= vnd_chan_connect,
	.disconnect		= vnd_chan_disconnect,

	.frame_xmit		= vnd_chan_frame_xmit,

	.stop_queue		= vnd_chan_stop_queue,
	.start_queue		= vnd_chan_start_queue,
	.wake_queue		= vnd_chan_wake_queue,

	.rx_error		= vnd_chan_rx_error,
	.tx_error		= vnd_chan_tx_error,
};

struct visdn_chan_ops vnd_chan_ops_e = {
	.owner			= THIS_MODULE,

	.release		= vnd_chan_e_release,
	.open			= vnd_chan_e_open,
	.close			= vnd_chan_e_close,
};

struct visdn_leg_ops vnd_chan_leg_e_ops = {
	.owner			= THIS_MODULE,

	.rx_error		= vnd_chan_e_rx_error,
	.tx_error		= vnd_chan_e_tx_error,

	.frame_xmit		= vnd_chan_e_frame_xmit,
};

static int vnd_netdev_open(struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;
	struct visdn_path *path;
	int err;

	set_bit(VND_NETDEVICE_STATE_RTNL_HELD, &netdevice->state);

	path = visdn_path_get_by_endpoint(&netdevice->visdn_chan);
	if (!path) {
		err = -ENODEV;
		goto err_no_path;
	}

	err = visdn_path_enable(path);
	if (err < 0)
		goto err_enable_path;

	visdn_path_put(path);

	clear_bit(VND_NETDEVICE_STATE_RTNL_HELD, &netdevice->state);

	vnd_debug(3, "vnd_netdev_open()\n");

	return 0;

err_enable_path:
	visdn_path_put(path);
err_no_path:

	return err;
}

static int vnd_netdev_stop(struct net_device *netdev)
{
	struct vnd_netdevice *netdevice = netdev->priv;
	struct visdn_path *path;
	int err;

	vnd_debug(3, "vnd_netdev_stop()\n");

	cancel_delayed_work(&netdevice->promiscuity_change_work);
	flush_scheduled_work();

	path = visdn_path_get_by_endpoint(&netdevice->visdn_chan);
	if (path) {
		err = visdn_path_disable(path);
		if (err < 0)
			goto err_disable_path;

		visdn_path_put(path);
	}

	return 0;

err_disable_path:
	visdn_path_put(path);

	return err;
}

static int vnd_netdev_frame_xmit(
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
		res = visdn_leg_frame_xmit(&netdevice->visdn_chan.leg_a, skb);
		switch(res) {
		case VISDN_TX_OK:
			return NETDEV_TX_OK;
		case VISDN_TX_BUSY:
			skb_push(skb, sizeof(struct lapd_prim_hdr));
			return NETDEV_TX_BUSY;
		case VISDN_TX_LOCKED:
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
	if (!schedule_work(&netdevice->promiscuity_change_work))
		vnd_netdevice_put(netdevice);
}

/* set_multicast_list is called in atomic context, so, we need a deferred
   work in order to be able to call functions like visdn_open which may
   sleep.

   This function is a little racy but races shouldn't be much harmful
*/

static void vnd_promiscuity_change_work(void *data)
{
	struct vnd_netdevice *netdevice = data;
	struct visdn_path *path;

	path = visdn_path_get_by_endpoint(&netdevice->visdn_chan_e);
	if (path) {
		if (netdevice->netdev->flags & IFF_PROMISC) {
			visdn_path_enable(path);
		} else if(!(netdevice->netdev->flags & IFF_PROMISC)) {
			visdn_path_disable(path);
		}

		visdn_path_put(path);
	}

	vnd_netdevice_put(netdevice);
}

static int vnd_netdev_do_ioctl(
	struct net_device *netdev,
	struct ifreq *ifr, int cmd)
{
	struct vnd_netdevice *netdevice = netdev->priv;

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
	if (new_mtu > netdevice->mtu)
		return -EINVAL;

	netdev->mtu = new_mtu;

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

static int lapd_hard_header_parse(struct sk_buff *skb, unsigned char *haddr)
{
	if(!skb->dev)
		return 0;

	haddr[0] = skb->dev->dev_addr[0];

	return 1;
}

static void setup_lapd(struct net_device *netdev)
{
	netdev->change_mtu = NULL;
	netdev->hard_header = NULL;
	netdev->rebuild_header = NULL;
	netdev->set_mac_address = lapd_mac_addr;
	netdev->hard_header_cache = NULL;
	netdev->header_cache_update= NULL;
	netdev->hard_header_parse = lapd_hard_header_parse;

	netdev->type = ARPHRD_LAPD;
	netdev->hard_header_len = 0;
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
		skb->h.raw = skb->nh.raw = skb->mac.raw = skb->data;

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

static int vnd_cdev_ioctl_do_create(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	int err;
	struct vnd_create create;
	void (*setup_func)(struct net_device *) = NULL;
	struct vnd_netdevice *netdevice = NULL;
	unsigned long flags;

	if (arg < sizeof(create)) {
		err = -EINVAL;
		goto err_invalid_sizeof_create;
	}

	if (copy_from_user(&create, (void *)arg, sizeof(create))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	switch(create.protocol) {
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

	netdevice = kmalloc(sizeof(*netdevice), GFP_KERNEL);
	if (!netdevice) {
		err = -EFAULT;
		goto err_kmalloc;
	}

	memset(netdevice, 0, sizeof(*netdevice));

	atomic_set(&netdevice->refcnt, 1);

	{
	struct vnd_netdevice *nd;
	spin_lock_irqsave(&vnd_netdevices_list_lock, flags);
	nd = _vnd_netdevice_get_by_name(create.devname);
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

	/*************** D channel ***************/
	visdn_chan_init(&netdevice->visdn_chan);

	snprintf(netdevice->visdn_chan.name,
		sizeof(netdevice->visdn_chan.name),
		"%s_d",
		create.devname);

	netdevice->visdn_chan.driver_data = netdevice;

	netdevice->visdn_chan.ops = &vnd_chan_ops;
	netdevice->visdn_chan.chan_class = &vnd_class_dchan;
	netdevice->visdn_chan.port = &vnd_port;

	netdevice->visdn_chan.leg_a.ops = &vnd_chan_leg_ops;
	netdevice->visdn_chan.leg_a.cxc = &vsc_softcxc.cxc;
	netdevice->visdn_chan.leg_a.framing = VISDN_LEG_FRAMING_HDLC;
	netdevice->visdn_chan.leg_a.framing_avail = VISDN_LEG_FRAMING_HDLC;
	netdevice->visdn_chan.leg_a.mtu = -1;

	netdevice->visdn_chan.leg_b.ops = NULL;
	netdevice->visdn_chan.leg_b.cxc = NULL;
	netdevice->visdn_chan.leg_b.framing = VISDN_LEG_FRAMING_HDLC;
	netdevice->visdn_chan.leg_b.framing_avail = VISDN_LEG_FRAMING_HDLC;
	netdevice->visdn_chan.leg_b.mtu = -1;

	/*************** E channel ***************/
	visdn_chan_init(&netdevice->visdn_chan_e);

	snprintf(netdevice->visdn_chan_e.name,
		sizeof(netdevice->visdn_chan_e.name),
		"%s_e",
		create.devname);

	netdevice->visdn_chan_e.driver_data = netdevice;

	netdevice->visdn_chan_e.ops = &vnd_chan_ops_e;
	netdevice->visdn_chan_e.chan_class = &vnd_class_echan;
	netdevice->visdn_chan_e.port = &vnd_port;

	netdevice->visdn_chan_e.leg_a.ops = &vnd_chan_leg_e_ops;
	netdevice->visdn_chan_e.leg_a.cxc = &vsc_softcxc.cxc;
	netdevice->visdn_chan_e.leg_a.framing = VISDN_LEG_FRAMING_HDLC;
	netdevice->visdn_chan_e.leg_a.framing_avail = VISDN_LEG_FRAMING_HDLC;
	netdevice->visdn_chan_e.leg_a.mtu = -1;

	netdevice->visdn_chan_e.leg_b.ops = NULL;
	netdevice->visdn_chan_e.leg_b.cxc = NULL;
	netdevice->visdn_chan_e.leg_b.framing = VISDN_LEG_FRAMING_HDLC;
	netdevice->visdn_chan_e.leg_b.framing_avail = VISDN_LEG_FRAMING_HDLC;
	netdevice->visdn_chan_e.leg_b.mtu = -1;

	INIT_WORK(&netdevice->promiscuity_change_work,
		vnd_promiscuity_change_work,
		netdevice);

	vnd_netdevice_get(netdevice); // Reference in visdn_chan->driver_data
	err = visdn_chan_register(&netdevice->visdn_chan);
	if (err < 0)
		goto err_visdn_chan_register;

	vnd_netdevice_get(netdevice); // Reference in visdn_chan->driver_data
	err = visdn_chan_register(&netdevice->visdn_chan_e);
	if (err < 0)
		goto err_visdn_chan_e_register;

	{
	netdevice->type = create.protocol;
	netdevice->state = 0;
	netdevice->netdev = alloc_netdev(0, create.devname, setup_func);
	if(!netdevice->netdev) {
		vnd_msg(KERN_ERR, "net_device alloc failed, abort.\n");
		err = -ENOMEM;
		goto err_alloc_netdev;
	}
	}

	netdevice->netdev->priv = netdevice;
	netdevice->netdev->open = vnd_netdev_open;
	netdevice->netdev->stop = vnd_netdev_stop;
	netdevice->netdev->hard_start_xmit = vnd_netdev_frame_xmit;
	netdevice->netdev->get_stats = vnd_netdev_get_stats;
	netdevice->netdev->set_multicast_list = vnd_netdev_set_multicast_list;
	netdevice->netdev->do_ioctl = vnd_netdev_do_ioctl;
	netdevice->netdev->change_mtu = vnd_netdev_change_mtu;
	netdevice->netdev->features = NETIF_F_NO_CSUM;

	memset(netdevice->netdev->dev_addr, 0,
		sizeof(netdevice->netdev->dev_addr));

	SET_MODULE_OWNER(netdevice->netdev);

	netdevice->netdev->irq = 0;
	netdevice->netdev->base_addr = 0;

	err = register_netdev(netdevice->netdev);
	if (err < 0) {
		vnd_msg(KERN_ERR, "Cannot register net device %s, aborting.\n",
			netdevice->netdev->name);
		goto err_register_netdev;
	}

	err = sysfs_create_link(
		&netdevice->netdev->class_dev.kobj,
		&netdevice->visdn_chan.kobj,
			VND_CHANNEL_SYMLINK);
	if (err < 0)
		goto err_create_symlink;

	err = sysfs_create_link(
		&netdevice->netdev->class_dev.kobj,
		&netdevice->visdn_chan_e.kobj,
			VND_CHANNEL_SYMLINK_E);
	if (err < 0)
		goto err_create_symlink_e;

	create.d_chan = netdevice->visdn_chan.id;
	create.e_chan = netdevice->visdn_chan_e.id;

	if (copy_to_user((void *)arg, &create, sizeof(create))) {
		err = -EFAULT;
		goto err_copy_to_user;
	}

	vnd_netdevice_put(netdevice);

	return 0;

err_copy_to_user:
	sysfs_remove_link(
		&netdevice->netdev->class_dev.kobj,
		VND_CHANNEL_SYMLINK_E);
err_create_symlink_e:
	sysfs_remove_link(
		&netdevice->netdev->class_dev.kobj,
		VND_CHANNEL_SYMLINK);
err_create_symlink:
	unregister_netdev(netdevice->netdev);
err_register_netdev:
	free_netdev(netdevice->netdev);
err_alloc_netdev:
	visdn_chan_unregister(&netdevice->visdn_chan_e);
err_visdn_chan_e_register:
	visdn_chan_unregister(&netdevice->visdn_chan);
err_visdn_chan_register:
	spin_lock_irqsave(&vnd_netdevices_list_lock, flags);
	list_del(&netdevice->list_node);
	spin_unlock_irqrestore(&vnd_netdevices_list_lock, flags);
err_already_exists:
	kfree(netdevice);
err_kmalloc:
err_unsupported_protocol:
err_copy_from_user:
err_invalid_sizeof_create:

	return err;
}

static void vnd_do_destroy(struct vnd_netdevice *netdevice)
{
	unsigned long flags;

	sysfs_remove_link(
		&netdevice->netdev->class_dev.kobj,
		VND_CHANNEL_SYMLINK_E);

	sysfs_remove_link(
		&netdevice->netdev->class_dev.kobj,
		VND_CHANNEL_SYMLINK);

	unregister_netdev(netdevice->netdev);

	visdn_chan_unregister(&netdevice->visdn_chan);
	visdn_chan_put(&netdevice->visdn_chan);

	visdn_chan_unregister(&netdevice->visdn_chan_e);
	visdn_chan_put(&netdevice->visdn_chan_e);

	spin_lock_irqsave(&vnd_netdevices_list_lock, flags);
	list_del_init(&netdevice->list_node);
	vnd_netdevice_put(netdevice);
	spin_unlock_irqrestore(&vnd_netdevices_list_lock, flags);
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

	vnd_do_destroy(netdevice);
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

struct visdn_port_ops vnd_port_ops = {
	.owner		= THIS_MODULE,
	.enable		= NULL,
	.disable	= NULL,
};

#ifndef HAVE_CLASS_DEV_DEVT
static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	return print_dev_t(buf, vnd_first_dev);
}
static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);
#endif

#ifdef DEBUG_CODE
static ssize_t vnd_show_debug_level(
	struct visdn_port *port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", debug_level);
}

static ssize_t vnd_store_debug_level(
	struct visdn_port *port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	unsigned int value;
	if (sscanf(buf, "%01x", &value) < 1)
		return -EINVAL;

	debug_level = value;

	vnd_msg(KERN_INFO, "Debug level set to '%d'\n", debug_level);

	return count;
}

VISDN_PORT_ATTR(debug_level, S_IRUGO | S_IWUSR,
	vnd_show_debug_level,
	vnd_store_debug_level);
#endif

static int __init vnd_init_module(void)
{
	int err;

	vnd_msg(KERN_INFO, vnd_MODULE_DESCR " loading\n");

	visdn_port_init(&vnd_port);
	vnd_port.ops = &vnd_port_ops;
	vnd_port.device = &visdn_system_device;
	strncpy(vnd_port.name, "netdev", sizeof(vnd_port.name));

	err = visdn_port_register(&vnd_port);
	if (err < 0)
		goto err_visdn_port_register;

#ifdef DEBUG_CODE
	err = visdn_port_create_file(
		&vnd_port,
		&visdn_port_attr_debug_level);
	if (err < 0)
		goto err_create_file_debug_level;
#endif

	err = alloc_chrdev_region(&vnd_first_dev, 0, 1, vnd_MODULE_NAME);
	if (err < 0)
		goto err_register_chrdev;

	cdev_init(&vnd_cdev, &vnd_fops);
	vnd_cdev.owner = THIS_MODULE;

	err = cdev_add(&vnd_cdev, vnd_first_dev, 1);
	if (err < 0)
		goto err_cdev_add;

	vnd_control_class_dev.class = &visdn_system_class;
	vnd_control_class_dev.class_data = NULL;
	vnd_control_class_dev.dev = vnd_port.device;
#ifdef HAVE_CLASS_DEV_DEVT
	vnd_control_class_dev.devt = vnd_first_dev;
#endif
	snprintf(vnd_control_class_dev.class_id,
		sizeof(vnd_control_class_dev.class_id),
		"netdev-control");

	err = class_device_register(&vnd_control_class_dev);
	if (err < 0)
		goto err_control_class_device_register;

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_create_file(
		&vnd_control_class_dev,
		&class_device_attr_dev);
#endif

	visdn_register_notifier(&vnd_notifier);

	return 0;

	class_device_del(&vnd_control_class_dev);
err_control_class_device_register:
	cdev_del(&vnd_cdev);
err_cdev_add:
	unregister_chrdev_region(vnd_first_dev, 1);
err_register_chrdev:
#ifdef DEBUG_CODE
	visdn_port_remove_file(
		&vnd_port,
		&visdn_port_attr_debug_level);
err_create_file_debug_level:
#endif
	visdn_port_unregister(&vnd_port);
err_visdn_port_register:

	return err;
}

module_init(vnd_init_module);

static void __exit vnd_module_exit(void)
{
	struct vnd_netdevice *netdevice, *t;

	visdn_unregister_notifier(&vnd_notifier);

#ifndef HAVE_CLASS_DEV_DEVT
	class_device_remove_file(
		&vnd_control_class_dev,
		&class_device_attr_dev);
#endif

	/* Ensure no one can open/ioctl cdevs before removing netdevices */
	class_device_del(&vnd_control_class_dev);
	cdev_del(&vnd_cdev);
	unregister_chrdev_region(vnd_first_dev, 1);

	/* No one else can access vnd_netdevices_list, so, no locking */
	list_for_each_entry_safe(netdevice, t,
			&vnd_netdevices_list, list_node) {

		vnd_netdevice_get(netdevice);

		vnd_do_destroy(netdevice);

		if (atomic_read(&netdevice->refcnt) > 1) {

			/* Usually 50ms are enough */
			msleep(50);

			while(atomic_read(&netdevice->refcnt) > 1) {
				vnd_msg(KERN_WARNING,
					"Waiting for netdevice"
					" refcnt to become 1"
					" (now %d)\n",
					atomic_read(&netdevice->refcnt));

				msleep(5000);
			}
		}

		vnd_netdevice_put(netdevice);
	}

#ifdef DEBUG_CODE
	visdn_port_remove_file(
		&vnd_port,
		&visdn_port_attr_debug_level);
#endif

	visdn_port_unregister(&vnd_port);

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
