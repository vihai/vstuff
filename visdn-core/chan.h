#ifndef _CHAN_H
#define _CHAN_H

#include <linux/skbuff.h>
#include <linux/ppp_channel.h>
#include <linux/netdevice.h>

#define VISDN_CHAN_HASHBITS 8

struct visdn_chan;
struct visdn_chan_ops
{
	int (*open)(struct visdn_chan *chan, int mode);
	int (*close)(struct visdn_chan *chan);

	int (*frame_xmit)(struct visdn_chan *chan, struct sk_buff *skb);
	struct net_device_stats *(*get_stats)(struct visdn_chan *chan);
	void (*set_promisc)(struct visdn_chan *chan, int enabled);
	int (*do_ioctl)(struct visdn_chan *chan, struct ifreq *ifr, int cmd);
};

#define VISDN_CHAN_ROLE_B	(1<<0)
#define VISDN_CHAN_ROLE_D	(1<<1)
#define VISDN_CHAN_ROLE_E	(1<<2)
#define VISDN_CHAN_ROLE_S	(1<<3)
#define VISDN_CHAN_ROLE_Q	(1<<4)

struct visdn_chan
{
	char name[64]; // FIXME

	struct device device;

	int index;
	struct hlist_node index_hlist;

	struct visdn_port *port;
	struct visdn_chan_ops *ops;

	struct ppp_channel ppp_chan;

	struct net_device *netdev;
	unsigned short protocol;

	void *priv;

	int speed;
	int role;
	int roles;
	int flags;
};

int visdn_chan_modinit(void);
void visdn_chan_modexit(void);

#define to_visdn_chan(class) container_of(class, struct visdn_chan, device)

void visdn_chan_init(
	struct visdn_chan *visdn_chan,
	struct visdn_chan_ops *ops);

struct visdn_chan *visdn_chan_alloc(void);

int visdn_chan_register(
	struct visdn_chan *visdn_chan,
	const char *name,
	struct visdn_port *visdn_port);

void visdn_chan_unregister(
	struct visdn_chan *visdn_chan);

#endif
