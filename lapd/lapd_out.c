/*

*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/termios.h> 
#include <linux/tcp.h>
#include <linux/if_arp.h>
#include <linux/random.h>
#include <linux/proc_fs.h>
#include <net/datalink.h>
#include <net/sock.h>

#include "lapd_user.h"
#include "lapd.h"
#include "tei_mgmt_nt.h"
#include "tei_mgmt_te.h"

inline int lapd_send_frame(struct sk_buff *skb)
{
	int err;

	if((err = dev_queue_xmit(skb)) < 0) {
		printk(KERN_ERR "lapd: dev_queue_xmit: %d\n", err);
		kfree_skb(skb);
		return err;
	}

	return skb->len;
}

int lapd_prepare_uframe(struct sock *sk,
	struct sk_buff *skb,
	enum lapd_uframe_function function)
{
	struct lapd_opt *lo = lapd_sk(sk);

	BUG_ON(!lo->dev);

	skb->h.raw = skb->nh.raw = skb->mac.raw = skb->data;
	skb->dev = lo->dev;
	skb->protocol = __constant_htons(ETH_P_LAPD);

	struct lapd_hdr *hdr =
		(struct lapd_hdr *)skb_put(skb, sizeof(struct lapd_hdr));

	hdr->addr.sapi = lo->sapi;

	enum lapd_cr cr = COMMAND;
	switch (function) {
		case SABME: cr = COMMAND; break;
		case DM:    cr = RESPONSE; break;
		case UI:    cr = COMMAND; break;
		case DISC:  cr = COMMAND; break;
		case UA:    cr = RESPONSE; break;
		case FRMR:  cr = RESPONSE; break;
		case XID: printk(KERN_ERR "lapd: unsupported XID\n"); break;
	}

	hdr->addr.c_r = ((cr == RESPONSE) == !lo->nt_mode)?1:0;
	hdr->addr.ea1 = 0;
	hdr->addr.ea2 = 1;
	hdr->addr.tei = lapd_get_tei(lo);

	hdr->control = lapd_uframe_make_control(function, 0/* p_f*/);

	return 0;
}

int lapd_send_uframe(struct sock *sk, u8 sapi,
	enum lapd_uframe_function function, void *data, int datalen)
{
	int err;

	struct sk_buff *skb;
		skb = sock_alloc_send_skb(sk,
			sizeof(struct lapd_hdr_e),
			0, &err);
// FIXME		(msg->msg_flags & MSG_DONTWAIT), &err);
	if (!skb) return err;

	err = lapd_prepare_uframe(sk, skb, function);
	if (err < 0)
		return err;

	if (data && datalen)
		memcpy(skb_put(skb, datalen), data, datalen);

	return lapd_send_frame(skb);
}

int lapd_send_completed_uframe(struct sk_buff *skb)
{
	return lapd_send_frame(skb);
}

void lapd_T203_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
//	struct lapd_opt *lo = lapd_sk(sk);

	sock_put(sk);
}

