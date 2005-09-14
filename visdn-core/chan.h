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

#define VISDN_CHANID_SIZE	32

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
#include <linux/list.h>
#include <linux/sysfs.h>

#define VISDN_CONNECT_OK	0
#define VISDN_CONNECT_BRIDGED	1

struct visdn_chan;
struct visdn_chan_pars;

struct visdn_chan_ops
{
	struct module *owner;

	void (*release)(struct visdn_chan *chan);

	int (*open)(struct visdn_chan *chan);
	int (*close)(struct visdn_chan *chan);

	int (*frame_xmit)(struct visdn_chan *chan, struct sk_buff *skb);
	void (*frame_input_error)(struct visdn_chan *chan, int code);

	struct net_device_stats *(*get_stats)(struct visdn_chan *chan);
	void (*set_promisc)(struct visdn_chan *chan, int enabled);

	int (*connect_to)(struct visdn_chan *chan,
				struct visdn_chan *chan2,
				int flags);
	int (*disconnect)(struct visdn_chan *chan);

	int (*update_parameters)(
		struct visdn_chan *chan,
		struct visdn_chan_pars *pars);

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

#define VISDN_CHAN_FRAMING_TRANS	(1 << 0)
#define VISDN_CHAN_FRAMING_HDLC		(1 << 1)
#define VISDN_CHAN_FRAMING_MTP		(1 << 2)

#define VISDN_CHAN_BITORDER_LSB		(1 << 0)
#define VISDN_CHAN_BITORDER_MSB		(1 << 1)

enum visdn_chan_bitrate_selection
{
	VISDN_CHAN_BITRATE_SELECTION_MAX,
	VISDN_CHAN_BITRATE_SELECTION_LIST,
	VISDN_CHAN_BITRATE_SELECTION_RANGE, // Not implemented yet
};

enum visdn_chan_open
{
	VISDN_CHAN_OPEN_OK = 0,
	VISDN_CHAN_OPEN_RENEGOTIATE,
};

struct visdn_chan_pars
{
	int mtu;
	int bitrate;
	int framing;
	int bitorder;
};

#define to_visdn_chan(obj) container_of(obj, struct visdn_chan, kobj)

struct visdn_chan
{
	struct kobject kobj;

	struct list_head port_channels_node;
	struct list_head cxc_channels_node;
	struct hlist_node index_node;
	char cxc_id[KOBJ_NAME_LEN];
	struct visdn_cxc *cxc;
	struct visdn_port *port;

	char name[KOBJ_NAME_LEN];

	int index;

	void *driver_data;

	struct visdn_chan_ops *ops;

	struct semaphore sem;

	unsigned long state;

	int autoopen;

	struct visdn_chan_pars pars;

	int max_mtu;

	enum visdn_chan_bitrate_selection bitrate_selection;
	int bitrates[32];
	int bitrates_cnt;

	int framing_supported;
	int framing_preferred;

	int bitorder_supported;
	int bitorder_preferred;
};

struct visdn_chan_attribute {
	struct attribute attr;

	ssize_t (*show)(
		struct visdn_chan *chan,
		struct visdn_chan_attribute *attr,
		char *buf);

	ssize_t (*store)(
		struct visdn_chan *chan,
		struct visdn_chan_attribute *attr,
		const char *buf,
		size_t count);
};

#define VISDN_CHAN_ATTR(_name,_mode,_show,_store) \
	struct visdn_chan_attribute visdn_chan_attr_##_name = \
		__ATTR(_name,_mode,_show,_store)

extern struct subsystem visdn_channels_subsys;

extern int visdn_chan_create_file(
	struct visdn_chan *chan,
	struct visdn_chan_attribute *entry);

extern void visdn_chan_remove_file(
	struct visdn_chan *chan,
	struct visdn_chan_attribute * attr);

int visdn_chan_modinit(void);
void visdn_chan_modexit(void);

static inline struct visdn_chan *visdn_chan_get(
	struct visdn_chan *chan)
{
	return chan ? to_visdn_chan(kobject_get(&chan->kobj)) : NULL;
}

static inline void visdn_chan_put(
	struct visdn_chan *chan)
{
	if (chan)
		kobject_put(&chan->kobj);
}

static inline void visdn_chan_lock(
	struct visdn_chan *chan)
{
	down(&chan->sem);
}

static inline int visdn_chan_lock_interruptible(
	struct visdn_chan *chan)
{
	return down_interruptible(&chan->sem);
}

static inline void visdn_chan_unlock(
	struct visdn_chan *chan)
{
	up(&chan->sem);
}

enum visdn_chan_state
{
	VISDN_CHAN_STATE_OPEN = 0,
};

extern int visdn_open(struct visdn_chan *chan);
extern int visdn_close(struct visdn_chan *chan);

extern int visdn_pass_stop_queue(struct visdn_chan *chan);
extern int visdn_pass_start_queue(struct visdn_chan *chan);
extern int visdn_pass_wake_queue(struct visdn_chan *chan);
extern int visdn_pass_frame_input_error(struct visdn_chan *chan, int code);
extern int visdn_pass_open(struct visdn_chan *chan);
extern int visdn_pass_close(struct visdn_chan *chan);
extern int visdn_pass_frame_xmit(
	struct visdn_chan *chan,
	struct sk_buff *skb);
extern struct net_device_stats *visdn_pass_get_stats(
	struct visdn_chan *chan);
extern ssize_t visdn_pass_samples_read(
	struct visdn_chan *chan,
	char __user *buf,
	size_t count);
extern ssize_t visdn_pass_samples_write(
	struct visdn_chan *chan,
	const char __user *buf,
	size_t count);

int visdn_chan_lock2(
	struct visdn_chan *chan1,
	struct visdn_chan *chan2);

int visdn_chan_lock2_interruptible(
	struct visdn_chan *chan1,
	struct visdn_chan *chan2);

extern void visdn_chan_init(struct visdn_chan *chan);

extern int visdn_disconnect(
	struct visdn_chan *chan);

extern int visdn_chan_register(struct visdn_chan *chan);
extern void visdn_chan_unregister(struct visdn_chan *chan);

extern int visdn_connect(struct visdn_chan *chan1,
		struct visdn_chan *chan2,
		int flags);
extern int visdn_negotiate_parameters(
	struct visdn_chan *chan1,
	struct visdn_chan *chan2);

#endif

#endif
