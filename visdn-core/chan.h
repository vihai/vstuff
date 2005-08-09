#ifndef _CHAN_H
#define _CHAN_H

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
#include <linux/ppp_channel.h>
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
	int (*samples_write)(struct visdn_chan *chan,
		const char __user *buf, size_t count);
};

#define VISDN_CHAN_ROLE_B	(1<<0)
#define VISDN_CHAN_ROLE_D	(1<<1)
#define VISDN_CHAN_ROLE_E	(1<<2)
#define VISDN_CHAN_ROLE_S	(1<<3)
#define VISDN_CHAN_ROLE_Q	(1<<4)

enum visdn_chan_framing
{
	VISDN_CHAN_FRAMING_TRANS,
	VISDN_CHAN_FRAMING_HDLC,
	VISDN_CHAN_FRAMING_MTP,
};

enum visdn_chan_bitorder
{
	VISDN_CHAN_BITORDER_LSB,
	VISDN_CHAN_BITORDER_MSB,
};

struct visdn_chan
{
	struct device device;

	int index;
	struct hlist_node index_hlist;

	struct visdn_port *port;
	struct visdn_chan_ops *ops;

	struct ppp_channel ppp_chan;

	struct net_device *netdev;
	unsigned short protocol;

	struct visdn_chan *connected_chan;

	void *priv;

	int speed;
	int role;
	int roles;
	int flags;

	enum visdn_chan_framing framing;
	enum visdn_chan_bitorder bitorder;
};

int visdn_chan_modinit(void);
void visdn_chan_modexit(void);

#define to_visdn_chan(class) container_of(class, struct visdn_chan, device)

void visdn_chan_init(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_ops *ops);

struct visdn_chan *visdn_chan_alloc(void);

int visdn_disconnect(
	struct visdn_chan *chan1,
	struct visdn_chan *chan2);

int visdn_chan_register(
	struct visdn_chan *visdn_chan,
	const char *name,
	struct visdn_port *visdn_port);

void visdn_chan_unregister(
	struct visdn_chan *visdn_chan);

struct visdn_chan *visdn_search_chan(const char *chanid);

int visdn_connect(struct visdn_chan *chan1,
		struct visdn_chan *chan2,
		int flags);


#endif

#endif
