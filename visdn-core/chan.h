/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_CHAN_H
#define _VISDN_CHAN_H

#define VISDN_IOC_CONNECT	_IOR(0xd0, 2, unsigned int)
#define VISDN_IOC_DISCONNECT	_IOR(0xd0, 3, unsigned int)

enum visdn_connect_flags
{
	VISDN_CONNECT_FLAG_SIMPLEX	= (1 << 0),
};

#define VISDN_CHANID_SIZE	64

struct visdn_connect
{
        char src_chanid[VISDN_CHANID_SIZE];
        char dst_chanid[VISDN_CHANID_SIZE];
	int flags;
};

#ifdef __KERNEL__

#include <linux/skbuff.h>
#include <linux/if.h>
#include <linux/device.h>
#include <linux/netdevice.h>

#define VISDN_CHAN_HASHBITS 8

#define VISDN_CONNECT_OK	0
#define VISDN_CONNECT_BRIDGED	1

extern struct bus_type visdn_bus_type;

struct visdn_chan;
struct visdn_chan_ops
{
	void (*release)(struct visdn_chan *chan);

	int (*open)(struct visdn_chan *chan);
	int (*close)(struct visdn_chan *chan);

	int (*frame_xmit)(struct visdn_chan *chan, struct sk_buff *skb);
	void (*frame_input_error)(struct visdn_chan *chan, int code);

	struct net_device_stats *(*get_stats)(struct visdn_chan *chan);
	void (*set_promisc)(struct visdn_chan *chan, int enabled);
	int (*do_ioctl)(struct visdn_chan *chan, struct ifreq *ifr, int cmd);

	int (*connect_to)(struct visdn_chan *chan,
				struct visdn_chan *chan2,
				int flags);
	int (*disconnect)(struct visdn_chan *chan);

	// bridge() callback should actually belong to a "card" structure
	int (*bridge)(struct visdn_chan *chan,
				struct visdn_chan *chan2);
	int (*unbridge)(struct visdn_chan *chan);

	ssize_t (*samples_read)(struct visdn_chan *chan,
		char __user *buf, size_t count);
	ssize_t (*samples_write)(struct visdn_chan *chan,
		const char __user *buf, size_t count);

	void (*stop_queue)(struct visdn_chan *chan);
	void (*start_queue)(struct visdn_chan *chan);
	void (*wake_queue)(struct visdn_chan *chan);
};

#define VISDN_CHAN_ROLE_B	(1<<0)
#define VISDN_CHAN_ROLE_D	(1<<1)
#define VISDN_CHAN_ROLE_E	(1<<2)
#define VISDN_CHAN_ROLE_S	(1<<3)
#define VISDN_CHAN_ROLE_Q	(1<<4)

#define VISDN_CHAN_FRAMING_TRANS	(1 << 0)
#define VISDN_CHAN_FRAMING_HDLC		(1 << 1)
#define VISDN_CHAN_FRAMING_MTP		(1 << 2)

#define VISDN_CHAN_BITORDER_LSB		(1 << 0)
#define VISDN_CHAN_BITORDER_MSB		(1 << 1)

struct visdn_chan
{
	struct device device;

	int index;
	struct hlist_node index_hlist;

	struct visdn_port *port;
	struct visdn_chan_ops *ops;

	struct visdn_chan *connected_chan;

	void *priv;

	int speed;
	int role;
	int roles;
	int flags;

	int framing_current;
	int framing_supported;
	int framing_preferred;

	int bitorder_current;
	int bitorder_supported;
	int bitorder_preferred;
};

int visdn_chan_modinit(void);
void visdn_chan_modexit(void);

#define to_visdn_chan(class) container_of(class, struct visdn_chan, device)

extern void visdn_chan_init(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_ops *ops);

extern struct visdn_chan *visdn_chan_alloc(void);

extern int visdn_disconnect(
	struct visdn_chan *chan1,
	struct visdn_chan *chan2);

extern int visdn_chan_register(
	struct visdn_chan *visdn_chan,
	const char *name,
	struct visdn_port *visdn_port);

extern void visdn_chan_unregister(
	struct visdn_chan *visdn_chan);

extern struct visdn_chan *visdn_search_chan(const char *chanid);

extern int visdn_connect(struct visdn_chan *chan1,
		struct visdn_chan *chan2,
		int flags);


#endif

#endif
