#ifndef _LAPD_IN_H
#define _LAPD_IN_H

int lapd_rcv(struct sk_buff *skb, struct net_device *dev,
		     struct packet_type *pt);

#endif
