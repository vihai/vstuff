/*
 * vISDN LAPD/q.931 protocol implementation
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/termios.h> 
#include <linux/tcp.h>
#include <linux/if_arp.h>
#include <linux/random.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/if.h>
#include <net/datalink.h>
#include <net/sock.h>

#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>

#include "lapd.h"
#include "lapd_in.h"
#include "lapd_out.h"
#include "tei_mgmt_nt.h"
#include "tei_mgmt_te.h"
#include "multiframe.h"

struct hlist_head lapd_hash[LAPD_HASHSIZE];
rwlock_t lapd_hash_lock = RW_LOCK_UNLOCKED;

static kmem_cache_t *lapd_sk_cachep;

#ifdef CONFIG_PROC_FS
struct lapd_iter_state {
	int bucket;
};

#define lapd_seq_private(seq) ((struct lapd_iter_state *)(seq)->private)

static struct sock *lapd_get_first(struct seq_file *seq)
{
	return hlist_entry(lapd_hash[0].first, struct sock, sk_node);
}

static struct sock *lapd_get_next(struct seq_file *seq, struct sock *sk)
{
	struct lapd_iter_state *state = seq->private;
 
	do {
		sk = sk_next(sk);
 try_again:
		;
	} while (sk);
 
	if (!sk && ++state->bucket < ARRAY_SIZE(lapd_hash)) {
		sk = sk_head(&lapd_hash[state->bucket]);
		goto try_again;
	}

	return sk;
}

static struct sock *lapd_get_idx(struct seq_file *seq, loff_t pos)
{
	struct sock *sk = lapd_get_first(seq);

	if (sk) {
		while (pos && (sk = lapd_get_next(seq, sk)) != NULL)
			--pos;
	}

	return pos ? NULL : sk;
}

static void *lapd_seq_start(struct seq_file *seq, loff_t *pos)
{
	read_lock(&lapd_hash_lock);
	return *pos ? lapd_get_idx(seq, *pos - 1) : SEQ_START_TOKEN;
}

static void *lapd_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sock *sk;

	if (v == SEQ_START_TOKEN)
		sk = lapd_get_first(seq);
	else
		sk = lapd_get_next(seq, v);

	++*pos;

	return sk;
}

static void lapd_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock(&lapd_hash_lock);
}

static __inline__ char *get_lapd_sock(struct sock *sp, char *tmpbuf, int i)
{
	struct lapd_opt *lo = lapd_sk(sp);

	sprintf(tmpbuf, "%4d: %02X %02X:%02X"
		" %02X %02X %02X %c%c%c%c  %08X:%08X %5d %5lu %3d %p",
		i,
		lo->state,
		lo->sapi,
		lo->tei,
		lo->v_s,
		lo->v_a,
		lo->v_r,
		lo->peer_busy?'B':' ',
		lo->me_busy?'M':' ',
		lo->rejection_exception?'R':' ',
		lo->in_timer_recovery?'T':' ',
		atomic_read(&sp->sk_wmem_alloc),
		atomic_read(&sp->sk_rmem_alloc),
		sock_i_uid(sp), sock_i_ino(sp),
		atomic_read(&sp->sk_refcnt), sp);
	return tmpbuf;
}


static int lapd_seq_show(struct seq_file *seq, void *v)
{
	char tmpbuf[129];

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "%-127s\n",
			"  sl  st sa:te vs va vr ecxpt"
			" tx_queue rx_queue   uid inode ref"
			);
	} else {
		struct lapd_iter_state *state = lapd_seq_private(seq);

		seq_printf(seq, "%-127s\n",
			get_lapd_sock(v, tmpbuf, state->bucket));
	}

	return 0;
}

static struct seq_operations lapd_seq_ops = {
	.start = lapd_seq_start,
	.next  = lapd_seq_next,
	.stop  = lapd_seq_stop,
	.show  = lapd_seq_show,
};

static int lapd_seq_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int rc = -ENOMEM;
	struct lapd_iter_state *s = kmalloc(sizeof(*s), GFP_KERNEL);

	if (!s)
		goto out;
	rc = seq_open(file, &lapd_seq_ops);
	if (rc)
		goto out_kfree;

	seq = file->private_data;
	seq->private = s;
	memset(s, 0, sizeof(*s));
out:
	return rc;
out_kfree:
	kfree(s);
	goto out;
}

static struct file_operations lapd_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = lapd_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release_private,
};

int __init lapd_proc_init(void)
{
	if (!proc_net_fops_create("lapd", S_IRUGO, &lapd_seq_fops))
		return -ENOMEM;
	return 0;
}

void __exit lapd_proc_exit(void)
{
	proc_net_remove("lapd");
}

#endif

static int lapd_change_mtu(struct net_device *dev, int mtu)
{
	return -EINVAL;
}

static int lapd_mac_addr(struct net_device *dev, void *addr)
{
	return -EINVAL;
}

static int lapd_hard_header_parse(struct sk_buff *skb, unsigned char *haddr)
{
	if(!skb->dev) return 0;

	haddr[0]=skb->dev->dev_addr[0];

	return 1;
}

void setup_lapd(struct net_device *netdev)
{
	/* Fill in the fields of the device structure with localtalk-generic values. */

	netdev->change_mtu         = lapd_change_mtu;
	netdev->hard_header        = NULL;
	netdev->rebuild_header     = NULL;
	netdev->set_mac_address    = lapd_mac_addr;
	netdev->hard_header_cache  = NULL;
	netdev->header_cache_update= NULL;
        netdev->hard_header_parse  = lapd_hard_header_parse;

	netdev->type               = ARPHRD_LAPD;
	netdev->hard_header_len    = 0;
	netdev->addr_len           = 1;
	netdev->tx_queue_len       = 10;

	memset(netdev->broadcast, 0x00, sizeof(netdev->broadcast));

	netdev->flags              = IFF_NOARP;
}
EXPORT_SYMBOL(setup_lapd);

static void lapd_sock_destruct(struct sock *sk)
{
	lapd_debug_sk(sk, "Socket destruct\n");

	if (!sock_flag(sk, SOCK_DEAD)) {
		lapd_printk_sk(KERN_CRIT, sk,
			"Attempt to release alive socket %p\n", sk);
		return;
	}

	WARN_ON(!sk_unhashed(sk));

	struct lapd_opt *lo = lapd_sk(sk);

	if (!hlist_empty(&lo->new_dlcs)) {
		struct lapd_new_dlc *new_dlc;
		struct hlist_node *pos, *t;

		hlist_for_each_entry_safe(new_dlc, pos, t, &lo->new_dlcs, node) {
			WARN_ON(sk_unhashed(new_dlc->sk));

			sk_del_node_init(new_dlc->sk);

			sock_orphan(new_dlc->sk);

			sock_put(new_dlc->sk);
			new_dlc->sk = NULL;

			hlist_del(&new_dlc->node);
			kfree(new_dlc);
		}
	}

	if (lo->usr_tme) {
		hlist_del(&lo->usr_tme->node);
		lapd_utme_put(lo->usr_tme);

		lapd_utme_put(lo->usr_tme);
		lo->usr_tme = NULL;
	}

	if (lo->dev) {
		dev_put(lo->dev);
		lo->dev = NULL;
	}

	if (lo->ppp_master_dev) {
		dev_put(lo->ppp_master_dev);
		lo->ppp_master_dev = NULL;
	}

	__skb_queue_purge(&sk->sk_write_queue);
	__skb_queue_purge(&sk->sk_receive_queue);
	__skb_queue_purge(&sk->sk_error_queue);
	__skb_queue_purge(&lo->u_queue);

	WARN_ON(!skb_queue_empty(&sk->sk_write_queue));
	WARN_ON(!skb_queue_empty(&sk->sk_receive_queue));
	WARN_ON(!skb_queue_empty(&sk->sk_error_queue));
	WARN_ON(!skb_queue_empty(&lo->u_queue));

	WARN_ON(atomic_read(&sk->sk_rmem_alloc));
	WARN_ON(atomic_read(&sk->sk_wmem_alloc));

/*	{
	struct lapd_te *te, *n;
	write_lock_bh(&lo->nt.tes_lock);

	list_for_each_entry_safe(te, n, &lo->nt.tes, hash_list) {

		lapd_printk_sk(KERN_ERR, sk, "List del %p\n", &te->hash_list);

		list_del(&te->hash_list);
		kfree(te);
	}

	write_unlock_bh(&lo->nt.tes_lock);
	}
*/
}

static int lapd_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct lapd_opt *lo = lapd_sk(sk);

	if (!sk)
		return 0;

	lapd_debug_sk(sk, "Socket release\n");

	lock_sock(sk);
	sk->sk_shutdown = SHUTDOWN_MASK;

	if (sk->sk_state == TCP_LISTEN) {
		sk->sk_state = TCP_CLOSE;

		write_lock_bh(&lapd_hash_lock);
		sk_del_node_init(sk);
		write_unlock_bh(&lapd_hash_lock);
	} else {
		switch (lo->state) {
		case LAPD_DLS_AWAITING_REESTABLISH:
			lapd_change_state(sk, LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE);
			sk->sk_state = TCP_CLOSING;
		break;

		case LAPD_DLS_LINK_CONNECTION_ESTABLISHED:
			lapd_discard_iqueue(sk);
			lo->retrans_cnt = 0;
			lapd_send_uframe(sk, LAPD_UFRAME_FUNC_DISC, 1, NULL, 0);
			lapd_start_t200(sk);

			if (!lo->in_timer_recovery)
				lapd_stop_t203(sk);

			lapd_change_state(sk, LAPD_DLS_AWAITING_RELEASE);
			sk->sk_state = TCP_CLOSING;
		break;

		default:
			sk->sk_state = TCP_CLOSE;

			write_lock_bh(&lapd_hash_lock);
			sk_del_node_init(sk);
			write_unlock_bh(&lapd_hash_lock);
		break;
		}
	}

	release_sock(sk);

	local_bh_disable();
        bh_lock_sock(sk);
        WARN_ON(sock_owned_by_user(sk));

        sock_orphan(sk);

	sock->sk = NULL;

	bh_unlock_sock(sk);
	local_bh_enable();

	sock_put(sk);

	return 0;
}

#define VISDN_SET_BEARER_PPP  SIOCDEVPRIVATE
#define VISDN_PPP_GET_CHAN  (SIOCDEVPRIVATE+1)
#define VISDN_PPP_GET_UNIT  (SIOCDEVPRIVATE+2)

static int lapd_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	int rc = -EINVAL;
	struct sock *sk = sock->sk;
	void __user *argp = (void __user *)arg;
	struct lapd_opt *lo = lapd_sk(sk);

	switch (cmd) {
	/* Protocol layer */
	case SIOCOUTQ: {
		long amount = sk->sk_sndbuf -
			      atomic_read(&sk->sk_wmem_alloc);

		if (amount < 0)
			amount = 0;
		rc = put_user(amount, (int __user *)argp);
	break;
	}

	case SIOCINQ: {
		spin_lock_bh(&sk->sk_receive_queue.lock);
		struct sk_buff *skb = skb_peek(&sk->sk_receive_queue);
		int size = 0;

		if (skb) {
			struct lapd_hdr *hdr = (struct lapd_hdr *)skb->mac.raw;

		        if (lapd_frame_type(hdr->control) ==
				LAPD_FRAME_TYPE_UFRAME)
				size = skb->len - sizeof(struct lapd_hdr);
			else
				size = skb->len - sizeof(struct lapd_hdr_e);
		}
		spin_unlock_bh(&sk->sk_receive_queue.lock);

		rc = put_user(size, (int __user *)argp);
	break;
	}
	case SIOCGSTAMP:
		rc = sock_get_timestamp(sk, argp);
		break;
	/* Routing */
	case SIOCADDRT:
	case SIOCDELRT:
		rc = -ENOSYS;
		break;
	/* Interface */
	case SIOCGIFADDR:
	case SIOCSIFADDR:
	case SIOCGIFBRDADDR:
	case SIOCDIFADDR:
	case SIOCSARP:		/* proxy AARP */
	case SIOCDARP:		/* proxy AARP */
//		rtnl_lock();
// FIXME		rc = isdnif_ioctl(cmd, argp);
//		rtnl_unlock();
		rc = -ENOSYS;
		break;
	/* Physical layer ioctl calls */
	case SIOCSIFLINK:
	case SIOCGIFHWADDR:
	case SIOCSIFHWADDR:
	case SIOCGIFFLAGS:
	case SIOCSIFFLAGS:
	case SIOCGIFMTU:
	case SIOCGIFCONF:
	case SIOCADDMULTI:
	case SIOCDELMULTI:
	case SIOCGIFCOUNT:
	case SIOCGIFINDEX:
	case SIOCGIFNAME:
		rc = dev_ioctl(cmd, argp);
	break;

	case 12345678:
		{
		struct ifreq ifreq;

		if (copy_from_user(&ifreq, argp, sizeof(ifreq)))
			return -EFAULT;

lapd_printk_sk(KERN_DEBUG, sk, "IOCTL 12345678 %s\n", ifreq.ifr_name);

			struct net_device *dev = dev_get_by_name(ifreq.ifr_name);

			if (!dev)
				return -ENODEV;

			lo->ppp_master_dev = dev;

			rc = dev_ioctl(VISDN_SET_BEARER_PPP, argp);
			}
	break;

	case PPPIOCGCHAN:
		{
lapd_printk_sk(KERN_DEBUG, sk, "IOCTL PPPIOCGCHAN\n");

		if (!lo->ppp_master_dev)
			return -ENODEV;

		struct ifreq ifreq;
		strlcpy(ifreq.ifr_name, lo->ppp_master_dev->name,
			sizeof(ifreq.ifr_name));

		ifreq.ifr_data = argp;

		return lo->ppp_master_dev->do_ioctl(
			lo->ppp_master_dev,
			&ifreq,
			VISDN_PPP_GET_CHAN);
		}
	break;

	case PPPIOCGUNIT:
		{
lapd_printk_sk(KERN_DEBUG, sk, "IOCTL PPPIOCGUNIT\n");

		if (!lo->ppp_master_dev)
			return -ENODEV;

		struct ifreq ifreq;
		strlcpy(ifreq.ifr_name, lo->ppp_master_dev->name,
			sizeof(ifreq.ifr_name));

		ifreq.ifr_data = argp;

		return lo->ppp_master_dev->do_ioctl(
			lo->ppp_master_dev,
			&ifreq,
			VISDN_PPP_GET_UNIT);
		}
	break;
	}

	return rc;
}

static int lapd_sendmsg(
	struct kiocb *iocb, struct socket *sock,
	struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct lapd_opt *lo = lapd_sk(sk);
	int err;

	lock_sock(sk);

	err = sock_error(sk);
	if (err)
		goto err_socket_error;

	if(sk->sk_shutdown & SEND_SHUTDOWN) {
		err = -EPIPE;
		goto err_shutting_down;
	}

	if (sk->sk_state != TCP_ESTABLISHED &&
	    sk->sk_state != TCP_LISTEN) {
		err = -EPIPE;
		goto err_not_established;
	}

	if (!lo->dev) {
		err = -ENODEV;
		goto err_no_dev;
	}

	if (len > lo->dev->mtu) {
		err = -EMSGSIZE;
		goto err_over_mtu;
	}

	// TODO, finish async operation
	/* This should be in poll */
	clear_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);

	struct sk_buff *skb;
	if (msg->msg_flags & MSG_OOB) {

		release_sock(sk);
		skb = sock_alloc_send_skb(sk,
        		sizeof(struct lapd_hdr_e) + len,
			(msg->msg_flags & MSG_DONTWAIT), &err);
		lock_sock(sk);
		if (!skb) {
			err = -ENOMEM;
			goto err_sock_alloc_send_skb;
		}

		err = lapd_prepare_uframe(sk, skb, LAPD_UFRAME_FUNC_UI, 0);
		if(err < 0)
			goto err_prepare_frame;

		err = memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len);
		if (err)
			goto err_memcpy_fromiovec;

		switch (lo->state) {
		case LAPD_DLS_TEI_UNASSIGNED:
			lapd_change_state(sk, LAPD_DLS_AWAITING_TEI);

			// Cannot be in this state in NT mode
			lapd_utme_start_tei_request(lo->usr_tme);

		case LAPD_DLS_AWAITING_TEI:
		case LAPD_DLS_ESTABLISH_AWAITING_TEI:
			lapd_queue_completed_uframe(sk, skb);
		break;

		case LAPD_DLS_NULL:
		case LAPD_DLS_LISTENING:
		case LAPD_DLS_TEI_ASSIGNED:
		case LAPD_DLS_LINK_CONNECTION_ESTABLISHED:
		case LAPD_DLS_AWAITING_ESTABLISH:
		case LAPD_DLS_AWAITING_REESTABLISH:
		case LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE:
		case LAPD_DLS_AWAITING_RELEASE:
			lapd_send_completed_uframe(skb);
		}
	} else {
		// FIXME TODO
		// sock_alloc_send_skb may sleep, sleeping with sk lock
		// no new frames may be received, including acks that may
		// free the output queue
		release_sock(sk);
		skb = sock_alloc_send_skb(sk,
        		sizeof(struct lapd_hdr_e) + len,
			(msg->msg_flags & MSG_DONTWAIT), &err);
		lock_sock(sk);
		if (!skb)
			goto err_sock_alloc_send_skb;

		if (lo->state != LAPD_DLS_LINK_CONNECTION_ESTABLISHED &&
		    lo->state != LAPD_DLS_AWAITING_REESTABLISH) {
			err = -ENOTCONN;
			goto err_notconn;
		}

		err = lapd_prepare_iframe(sk, skb);
		if(err < 0)
			goto err_prepare_frame;

		err = memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len);
		if (err)
			goto err_memcpy_fromiovec;

		lapd_queue_completed_iframe(sk, skb);
	}

	release_sock(sk);

	return len;

err_memcpy_fromiovec:
err_prepare_frame:
err_notconn:
	kfree_skb(skb);
err_sock_alloc_send_skb:
err_over_mtu:
err_no_dev:
err_not_established:
err_socket_error:
err_shutting_down:

	release_sock(sk);

	return err;
}


static int lapd_recvmsg(struct kiocb *iocb, struct socket *sock,
	struct msghdr *msg, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct lapd_opt *lo = lapd_sk(sk);
	int err;
	int copied;

	lock_sock(sk);

	if (sk->sk_state != TCP_ESTABLISHED) {
		err = -ENOTCONN;
		goto err_not_established;
	}

	if (sk->sk_shutdown & RCV_SHUTDOWN) {
		err = -EPIPE;
		goto err_shutting_down;
	}

	if (!lo->dev) {
		err = -ENODEV;
		goto err_no_dev;
	}

	release_sock(sk);

	struct sk_buff *skb;
	skb = skb_recv_datagram(sk, flags, flags & MSG_DONTWAIT, &err);
	if (!skb)
		goto err_recv_datagram;

        struct lapd_hdr *hdr = (struct lapd_hdr *)skb->mac.raw;
	int hdrsize;

        if (lapd_frame_type(hdr->control) == LAPD_FRAME_TYPE_UFRAME) {
		msg->msg_flags |= MSG_OOB;
		hdrsize = sizeof(struct lapd_hdr);
	} else {
		hdrsize = sizeof(struct lapd_hdr_e);
	}

	copied = skb->len - hdrsize;
	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}

	skb_copy_datagram_iovec(skb, hdrsize, msg->msg_iov, copied);

	skb_free_datagram(sk, skb);
	err = copied;

err_recv_datagram:
err_no_dev:
err_shutting_down:
err_not_established:

	return err;
}

static int lapd_bind_to_device(struct sock *sk, const char *devname)
{
	struct lapd_opt *lo = lapd_sk(sk);
	int err = 0;

	struct net_device *dev = dev_get_by_name(devname);
	if (dev == NULL) {
		err = -ENODEV;
		goto err_nodev;
	}

	if (dev->type != ARPHRD_LAPD) {
		err = -ENOPROTOOPT;
		goto err_invalid_type;
	}

	if (sk->sk_type != SOCK_SEQPACKET) {
		err = -EINVAL;
		goto err_invalid_socket_type;
	}

	if (!(dev->flags & IFF_UP)) {
		err = -ENETDOWN;
		goto err_dev_not_up;
	}

	if (dev->flags & IFF_ALLMULTI) {
		lo->nt_mode = TRUE;

		struct sock *othersk = NULL;
		struct hlist_node *node;
		// Do not allow binding more than one socket to the
		// same interface.

		read_lock_bh(&lapd_hash_lock);

		int i;
		for (i=0; i<ARRAY_SIZE(lapd_hash); i++) {
			sk_for_each(othersk, node, &lapd_hash[i]) {
				if (lapd_sk(othersk)->nt_mode &&
				    lapd_sk(othersk)->dev == dev) {
					read_unlock_bh(&lapd_hash_lock);
					err = -EBUSY;
					goto err_socket_already_present;
				}
			}
		}
		read_unlock_bh(&lapd_hash_lock);

		sk->sk_state = TCP_SYN_SENT;
	} else {
		lo->nt_mode = FALSE;

		if (dev->flags & IFF_POINTOPOINT)
			lapd_utme_set_static_tei(lo->usr_tme, 0);

		sk->sk_state = TCP_ESTABLISHED;
	}

	if (lo->nt_mode) {
		lo->state = LAPD_DLS_TEI_ASSIGNED;
	} else {
		lo->state = LAPD_DLS_TEI_UNASSIGNED;

		lo->usr_tme = lapd_utme_alloc(dev);

		lapd_utme_hold(lo->usr_tme);
		hlist_add_head(&lo->usr_tme->node, &lapd_utme_hash);
	}

	// No need to dev_hold() since we already held dev by calling
	// dev_get_by_name() and "dev" is thrown away
	sk->sk_bound_dev_if = dev->ifindex;
	lo->dev = dev;

	if (lo->sapi == LAPD_SAPI_Q931) {
		lo->sap = &lapd_dev(lo->dev)->q931;
	} else if(lo->sapi == LAPD_SAPI_X25) {
		lo->sap = &lapd_dev(lo->dev)->x25;
	}

	write_lock_bh(&lapd_hash_lock);
	sk_add_node(sk, lapd_get_hash(lo->dev));
	write_unlock_bh(&lapd_hash_lock);

	return 0;

err_socket_already_present:
err_dev_not_up:
err_invalid_type:
err_invalid_socket_type:
	dev_put(dev);
err_nodev:

	return err;
}

unsigned int lapd_poll(struct file *file,
	struct socket *sock, poll_table *wait)
{
	unsigned int mask;
	struct sock *sk = sock->sk;

	poll_wait(file, sk->sk_sleep, wait);

	if (sk->sk_state == TCP_LISTEN) {
		struct lapd_opt *lo = lapd_sk(sk);

		return !hlist_empty(&lo->new_dlcs) ?
			(POLLIN | POLLRDNORM) : 0;
	}

	/* Socket is not locked. We are protected from async events
	   by poll logic and correct handling of state changes
	   made by another threads is impossible in any case.
	 */

	mask = 0;

	/* exceptional events? */
	if (sk->sk_err || !skb_queue_empty(&sk->sk_error_queue))
		mask |= POLLERR;
	if (sk->sk_shutdown == SHUTDOWN_MASK)
		mask |= POLLHUP;

	/* readable? */
	if (!skb_queue_empty(&sk->sk_receive_queue) ||
	    (sk->sk_shutdown & RCV_SHUTDOWN))
		mask |= POLLIN | POLLRDNORM;

	if (sk->sk_state == TCP_CLOSE || sk->sk_state == TCP_CLOSING)
		mask |= POLLHUP;

	/* writable? */
	if (sock_writeable(sk))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;
	else
		set_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);

	return mask;
}

static int lapd_setsockopt(struct socket *sock, int level, int optname,
	char __user *optval_u, int optlen)
{
	struct sock *sk = sock->sk;
	struct lapd_opt *lo = lapd_sk(sk);
	int err = 0;

	if (level != SOL_LAPD)
		return -ENOPROTOOPT;

	int intoptval = 0;
	if (optlen == sizeof(int)) {
		if (get_user(intoptval, (int __user *)optval_u)) {
			err = -EFAULT;
			goto err_copy_from_user;
		}
	}

	lock_sock(sk);

	switch (optname) {
	case SO_BINDTODEVICE: {
		char devname[IFNAMSIZ];
		if (optlen > IFNAMSIZ) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		if (copy_from_user(devname, optval_u, optlen)) {
			err = -EFAULT;
			goto err_copy_from_user;
		}

		// Is this really needed?
		devname[sizeof(devname)-1] = '\0';

		err = lapd_bind_to_device(sk, devname);
		if (err < 0) goto err_bind_to_device;
	}
	break;

	case LAPD_TEI:
		// release TEI?
		// check static TEI?

		if (optlen != sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		// Static TEIs are 0-63
		if (intoptval < 0 || intoptval > 63) {
			err = -EINVAL;
			goto err_invalid_optval;
		}

		lapd_utme_set_static_tei(lo->usr_tme, intoptval);
	break;

	case LAPD_TEI_MGMT_T201:
		if (optlen != sizeof(int)) {
			err = -EINVAL;
			break;
		}

		if (intoptval <= 0 || intoptval > 30) {
			err = -EINVAL;
			break;
		}

		if (!lo->nt_mode) {
			err = -EINVAL;
			break;
		}

		lo->net_tme->T201 = intoptval * HZ / 1000;
	break;

	case LAPD_TEI_MGMT_N202:
		if (optlen != sizeof(int)) {
			err = -EINVAL;
			break;
		}

		if (intoptval <= 0 || intoptval > 30) {
			err = -EINVAL;
			break;
		}

		if (lo->nt_mode) {
			err = -EINVAL;
			break;
		}

		lo->usr_tme->N202 = intoptval;
	break;

	case LAPD_TEI_MGMT_T202:
		if (optlen != sizeof(int)) {
			err = -EINVAL;
			break;
		}

		if (intoptval <= 0 || intoptval > 30) {
			err = -EINVAL;
			break;
		}

		if (lo->nt_mode) {
			err = -EINVAL;
			break;
		}

		lo->usr_tme->N202 = intoptval * HZ / 1000;
	break;

	case LAPD_T200:
		if (optlen != sizeof(int)) {
			err = -EINVAL;
			break;
		}

		if (intoptval <= 0 || intoptval > 30) {
			err = -EINVAL;
			break;
		}

		lo->sap->T200 = intoptval * HZ / 1000;
	break;

	case LAPD_N200:
		if (optlen != sizeof(int)) {
			err = -EINVAL;
			break;
		}

		if (intoptval <= 0 || intoptval > 30) {
			err = -EINVAL;
			break;
		}

		lo->sap->N200 = intoptval;
	break;

	case LAPD_T203:
		if (optlen != sizeof(int)) {
			err = -EINVAL;
			break;
		}

		if (intoptval <= 0 || intoptval > 30) {
			err = -EINVAL;
			break;
		}

		lo->sap->T203 = intoptval * HZ / 1000;
	break;

	case LAPD_N201:
		if (optlen != sizeof(int)) {
			err = -EINVAL;
			break;
		}

		if (intoptval <= 0 || intoptval > 512) {
			err = -EINVAL;
			break;
		}

		lo->sap->N201 = intoptval;
	break;

	case LAPD_K:
		if (optlen != sizeof(int)) {
			err = -EINVAL;
			break;
		}

		if (intoptval <= 0 || intoptval > 30) {
			err = -EINVAL;
			break;
		}

		lo->sap->k = intoptval;
	break;

	default:
		err = -ENOPROTOOPT;
	}

err_bind_to_device:
err_copy_from_user:
err_invalid_optlen:
err_invalid_optval:

	release_sock(sk);

	return err;
}

static int lapd_getsockopt(struct socket *sock, int level, int optname,
	char __user *optval_u, int __user *optlen_u)
{
	int err = 0;
	struct sock *sk = sock->sk;

	if (level != SOL_LAPD)
		return -ENOPROTOOPT;

	int optlen;
	if (get_user(optlen, optlen_u)) {
		err = -EFAULT;
		goto err_get_user;
	}

	// By default use an int
	int val = 0;
	void *optval = (void *)&val;
	int length = min_t(unsigned int, optlen, sizeof(int));

	lock_sock(sk);

	struct lapd_opt *lo = lapd_sk(sk);
	switch (optname) {
	case SO_BINDTODEVICE: {
		struct net_device *dev;
		char devname[IFNAMSIZ];

		dev = lo->dev;

		if (dev) {
			strlcpy(devname, dev->name, sizeof(devname));
			length = strlen(devname) + 1;
		} else {
			*devname = '\0';
			length = 1;
		}

		optval = (void *) devname;
		}
	break;

	case LAPD_ROLE:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lo->nt_mode;
	break;

	case LAPD_TEI:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		if (lo->nt_mode)
			val = lo->tei;
		else
			val = lo->usr_tme->tei;
	break;

	case LAPD_TEI_MGMT_STATUS:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		if (lo->nt_mode) {
			err = -EINVAL;
			goto err_invalid_request;
		}

		val = lo->usr_tme->status;
	break;

	case LAPD_TEI_MGMT_T201:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		if (!lo->nt_mode) {
			err = -EINVAL;
			goto err_invalid_request;
		}

		// Locking?
		val = lo->net_tme->T201;
	break;

	case LAPD_TEI_MGMT_N202:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		if (lo->nt_mode) {
			err = -EINVAL;
			goto err_invalid_request;
		}

		val = lo->usr_tme->N202;
	break;

	case LAPD_TEI_MGMT_T202:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		if (lo->nt_mode) {
			err = -EINVAL;
			goto err_invalid_request;
		}

		val = lo->usr_tme->T202;
	break;

	case LAPD_T200:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lo->sap->T200;
	break;

	case LAPD_N200:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lo->sap->N200;
	break;

	case LAPD_T203:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lo->sap->T203;
	break;

	case LAPD_N201:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lo->sap->N201;
	break;

	case LAPD_K:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lo->sap->k;
	break;

	case LAPD_DLC_STATE:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lo->state;
	break;

	default:
		release_sock(sk);
		return -ENOPROTOOPT;
	}

	if (put_user(length, optlen_u)) {
		err = -EFAULT;
		goto err_put_user;
	}

	release_sock(sk);

	return copy_to_user(optval_u,
		optval,
		min_t(unsigned int, optlen, length)) ? -EFAULT : 0;

err_put_user:
err_invalid_optlen:
err_invalid_request:
	release_sock(sk);
err_get_user:

	return err;
}

static void lapd_unhash_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;

	lapd_debug_sk(sk, "Unhash timer\n");

	BUG_ON(sk->sk_state != TCP_CLOSING);

	write_lock_bh(&lapd_hash_lock);
	sk_del_node_init(sk);
	write_unlock_bh(&lapd_hash_lock);

	sock_put(sk);
}

struct sock *lapd_new_sock(struct sock *parent_sk, u8 tei, int sapi)
{
	struct sock *sk;
	sk = sk_alloc(PF_LAPD, GFP_ATOMIC,
		sizeof(struct lapd_sock),
		parent_sk->sk_slab);
	if (!sk)
		return NULL;

	sock_init_data(NULL, sk);
	sk_set_owner(sk, THIS_MODULE);

	sk->sk_destruct = lapd_sock_destruct;
	sk->sk_backlog_rcv = lapd_backlog_rcv;
	sk->sk_zapped = parent_sk->sk_zapped;
	sk->sk_type = parent_sk->sk_type;
	sk->sk_priority = parent_sk->sk_priority;
	sk->sk_protocol = parent_sk->sk_protocol;
	sk->sk_rcvbuf = parent_sk->sk_rcvbuf;
	sk->sk_sndbuf = parent_sk->sk_sndbuf;
	sk->sk_debug = parent_sk->sk_debug;
	sk->sk_state = TCP_ESTABLISHED;
	sk->sk_sleep = parent_sk->sk_sleep;

	init_timer(&sk->sk_timer);
	sk->sk_timer.function = &lapd_unhash_timer;
	sk->sk_timer.data = (unsigned long)sk;

	struct lapd_opt *lo = lapd_sk(sk);
	struct lapd_opt *parent_lo = lapd_sk(parent_sk);

	BUG_ON(!parent_lo->dev);
	lo->dev = parent_lo->dev;
	dev_hold(lo->dev);

	lo->nt_mode = parent_lo->nt_mode;

	skb_queue_head_init(&lo->u_queue);

	lo->usr_tme = NULL;
	lo->peer_busy = FALSE;
	lo->me_busy = FALSE;
	lo->rejection_exception = FALSE;
	lo->in_timer_recovery = FALSE;

	lo->state = LAPD_DLS_TEI_ASSIGNED;

	INIT_HLIST_HEAD(&lo->new_dlcs);

	init_timer(&lo->T200_timer);
	lo->T200_timer.function = lapd_T200_timer;
	lo->T200_timer.data = (unsigned long)sk;

	init_timer(&lo->T203_timer);
	lo->T203_timer.function = lapd_T203_timer;
	lo->T203_timer.data = (unsigned long)sk;

	lo->tei = tei;
	lo->sapi = sapi;

	if (sapi == LAPD_SAPI_Q931) {
		lo->sap = &lapd_dev(lo->dev)->q931;
	} else if(sapi == LAPD_SAPI_X25) {
		lo->sap = &lapd_dev(lo->dev)->x25;
	}

	return sk;
}

const char *lapd_state_to_text(enum lapd_datalink_state state)
{
	switch (state) {
	case LAPD_DLS_NULL:
		return "NULL";
	case LAPD_DLS_LISTENING:
		return "LISTENING";
	case LAPD_DLS_TEI_UNASSIGNED:
		return "TEI_UNASSIGNED";
	case LAPD_DLS_AWAITING_TEI:
		return "AWAITING_TEI";
	case LAPD_DLS_ESTABLISH_AWAITING_TEI:
		return "ESTABLISH_AWAITING_TEI";
	case LAPD_DLS_TEI_ASSIGNED:
		return "TEI_ASSIGNED";
	case LAPD_DLS_AWAITING_ESTABLISH:
		return "AWAITING_ESTABLISH";
	case LAPD_DLS_AWAITING_REESTABLISH:
		return "AWAITING_REESTABLISH";
	case LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE:
		return "ESTABLISH_PENDING_RELEASE";
	case LAPD_DLS_AWAITING_RELEASE:
		return "AWAITING_RELEASE";
	case LAPD_DLS_LINK_CONNECTION_ESTABLISHED:
		return "LINK_CONNECTION_ESTABLISHED";
	}

	return NULL;
}

void lapd_change_state(struct sock *sk, enum lapd_datalink_state newstate)
{
	struct lapd_opt *lo = lapd_sk(sk);
	const char *oldstate;
	oldstate = lapd_state_to_text(lapd_sk(sk)->state);

	lo->state = newstate;

	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_state_change(sk);

	lapd_debug_sk(sk, "Changed state from %s to %s\n",
		oldstate, lapd_state_to_text(lo->state));
}

// This must be called holding socket lock
void lapd_mdl_assign_request(struct sock *sk, int tei)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lapd_debug_sk(sk, "MDL-ASSIGN-REQUEST %d\n", sk->sk_state);

	switch (lo->state) {
	case LAPD_DLS_TEI_UNASSIGNED:
	case LAPD_DLS_AWAITING_TEI:
		lo->tei = tei;

		lapd_change_state(sk, LAPD_DLS_TEI_ASSIGNED);
		lapd_flush_uqueue(sk);
	break;

	case LAPD_DLS_ESTABLISH_AWAITING_TEI:
		lo->tei = tei;

		lo->retrans_cnt = 0;
		lapd_send_uframe(sk, LAPD_UFRAME_FUNC_SABME, 1, NULL, 0);
		lapd_start_t200(sk);

		lapd_change_state(sk, LAPD_DLS_AWAITING_ESTABLISH);

		lapd_flush_uqueue(sk);
	break;

	case LAPD_DLS_NULL:
	case LAPD_DLS_LISTENING:
	case LAPD_DLS_TEI_ASSIGNED:
	case LAPD_DLS_LINK_CONNECTION_ESTABLISHED:
	case LAPD_DLS_AWAITING_ESTABLISH:
	case LAPD_DLS_AWAITING_REESTABLISH:
	case LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE:
	case LAPD_DLS_AWAITING_RELEASE:
		lapd_printk(KERN_ERR,
			"Unexpected MDL-ASSIGN-REQUEST in state %d\n",
			lo->state);
	break;
	}
}

// This must be called holding socket lock
void lapd_mdl_remove_request(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lapd_debug_sk(sk, "MDL-REMOVE-REQUEST");

	switch (lo->state) {
	case LAPD_DLS_TEI_ASSIGNED:
		lapd_discard_uqueue(sk);
		lapd_change_state(sk, LAPD_DLS_TEI_UNASSIGNED);
	break;

	case LAPD_DLS_AWAITING_ESTABLISH:
		lapd_dl_release_indication(sk);
		lapd_discard_uqueue(sk);
		lapd_stop_t200(sk);
		lapd_change_state(sk, LAPD_DLS_TEI_UNASSIGNED);
	break;

	case LAPD_DLS_AWAITING_REESTABLISH:
		lapd_dl_release_indication(sk);
		lapd_discard_uqueue(sk);
		lapd_discard_iqueue(sk);
		lapd_stop_t200(sk);
		lapd_change_state(sk, LAPD_DLS_TEI_UNASSIGNED);
	break;

	case LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE:
		lapd_dl_release_confirm(sk);
		lapd_discard_uqueue(sk);
		lapd_discard_iqueue(sk);
		lapd_stop_t200(sk);
		lapd_change_state(sk, LAPD_DLS_TEI_UNASSIGNED);
	break;

	case LAPD_DLS_AWAITING_RELEASE:
		lapd_dl_release_confirm(sk);
		lapd_discard_uqueue(sk);
		lapd_stop_t200(sk);
		lapd_change_state(sk, LAPD_DLS_TEI_UNASSIGNED);
	break;

	case LAPD_DLS_LINK_CONNECTION_ESTABLISHED:
	break;

	case LAPD_DLS_NULL:
	case LAPD_DLS_LISTENING:
	case LAPD_DLS_TEI_UNASSIGNED:
	case LAPD_DLS_AWAITING_TEI:
	case LAPD_DLS_ESTABLISH_AWAITING_TEI:
		lapd_printk(KERN_ERR,
			"Unexpected MDL-REMOVE-REQUEST in state %d\n",
			lo->state);
	break;
	}
}

// This must be called holding socket lock
void lapd_mdl_error_response(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);

	lapd_debug_sk(sk, "MDL-ERROR-RESPONSE\n");

	switch(lo->state) {
	case LAPD_DLS_AWAITING_TEI:
		lapd_change_state(sk, LAPD_DLS_TEI_UNASSIGNED);
	break;

	case LAPD_DLS_ESTABLISH_AWAITING_TEI:
		lapd_dl_release_indication(sk);
		sk->sk_err = EIO;

		lapd_change_state(sk, LAPD_DLS_TEI_UNASSIGNED);
	break;

	case LAPD_DLS_NULL:
	case LAPD_DLS_LISTENING:
	case LAPD_DLS_TEI_UNASSIGNED:
	case LAPD_DLS_TEI_ASSIGNED:
	case LAPD_DLS_AWAITING_ESTABLISH:
	case LAPD_DLS_AWAITING_REESTABLISH:
	case LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE:
	case LAPD_DLS_AWAITING_RELEASE:
	case LAPD_DLS_LINK_CONNECTION_ESTABLISHED:
		lapd_printk(KERN_ERR,
			"Unexpected MDL-ERROR-RESPONSE in state %s\n",
			lapd_state_to_text(lo->state));
	break;
	}
}

int lapd_multiframe_wait_for_establishment(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);
	DEFINE_WAIT(wait);
        int err = 0;
	int timeout = 60 * HZ;

	for (;;) {
		prepare_to_wait_exclusive(sk->sk_sleep, &wait,
			TASK_INTERRUPTIBLE);

		release_sock(sk);
		// Timeout is used only to detect abnormal cases
		timeout = schedule_timeout(timeout);
		lock_sock(sk);

		err = sock_error(sk);
		if (err)
			break;

		if (sk->sk_state != TCP_ESTABLISHED)
			break;

		if (lo->state == LAPD_DLS_LINK_CONNECTION_ESTABLISHED)
			break;

		if (signal_pending(current)) {
			err = sock_intr_errno(timeout);
			break;
		}

		if (!timeout) {
			lapd_printk_sk(KERN_ERR, sk,
				"Connect timed out?!?\n");

			err = -EAGAIN;
			break;
		}
	}

	finish_wait(sk->sk_sleep, &wait);

	return err;
}

int lapd_multiframe_wait_for_release(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);
	DEFINE_WAIT(wait);
        int err = 0;
	int timeout = 60 * HZ;

	for (;;) {
		prepare_to_wait_exclusive(sk->sk_sleep, &wait,
			TASK_INTERRUPTIBLE);

		release_sock(sk);
		// Timeout is used only to detect abnormal cases
		timeout = schedule_timeout(timeout);
		lock_sock(sk);

		if (sk->sk_err) {
			err = -sk->sk_err;
			break;
		}

		if (lo->state == LAPD_DLS_TEI_ASSIGNED)
			break;

		if (signal_pending(current)) {
			err = sock_intr_errno(timeout);
			break;
		}

		if (!timeout) {
			lapd_printk_sk(KERN_ERR, sk,
				"Connect timed out?!?\n");

			err = -EAGAIN;
			break;
		}
	}

	finish_wait(sk->sk_sleep, &wait);

	return err;
}

static int lapd_connect(struct socket *sock, struct sockaddr *uaddr,
	int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	int err = 0;

	lock_sock(sk);

	if (sk->sk_state != TCP_ESTABLISHED) {
		err = -EIO;
		goto err_listening;
	}

	struct lapd_opt *lo = lapd_sk(sk);

	switch(lo->state) {
	case LAPD_DLS_TEI_UNASSIGNED:
		lapd_change_state(sk, LAPD_DLS_ESTABLISH_AWAITING_TEI);

		lapd_utme_start_tei_request(lo->usr_tme);

		if (flags & O_NONBLOCK) {
			err = -EWOULDBLOCK;
			goto err_tei_unassigned;
		}

		err = lapd_multiframe_wait_for_establishment(sk);
		if (err) {
			if (err == -ECONNRESET)
				err = -ETIMEDOUT;

			goto err_multiframe_wait_for_establishment;
		}
	break;

	case LAPD_DLS_AWAITING_TEI:
		lapd_change_state(sk, LAPD_DLS_ESTABLISH_AWAITING_TEI);
	break;

	case LAPD_DLS_ESTABLISH_AWAITING_TEI:
		err = -EINPROGRESS;
	break;

	case LAPD_DLS_TEI_ASSIGNED:
		lo->retrans_cnt = 0;
		lapd_send_uframe(sk, LAPD_UFRAME_FUNC_SABME, 1, NULL, 0);
		lapd_start_t200(sk);
		lapd_change_state(sk, LAPD_DLS_AWAITING_ESTABLISH);

		if (flags & O_NONBLOCK) {
			err = -EWOULDBLOCK;
			goto wouldblock;
		}

		err = lapd_multiframe_wait_for_establishment(sk);
		if (err) {
			if (err == -ECONNRESET)
				err = -ETIMEDOUT;

			goto err_multiframe_wait_for_establishment;
		}
	break;

	case LAPD_DLS_AWAITING_ESTABLISH:
		err = -EINPROGRESS;
		goto err_already_establishing;
	break;

	case LAPD_DLS_AWAITING_REESTABLISH:
		lapd_discard_iqueue(sk);

		lapd_change_state(sk, LAPD_DLS_AWAITING_ESTABLISH);

		if (flags & O_NONBLOCK) {
			err = -EWOULDBLOCK;
			goto wouldblock;
		}

		err = lapd_multiframe_wait_for_establishment(sk);
		if (err) {
			if (err == -ECONNRESET)
				err = -ETIMEDOUT;

			goto err_multiframe_wait_for_establishment;
		}
	break;

	case LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE:
		err = -ECONNABORTED;
	break;

	case LAPD_DLS_AWAITING_RELEASE:
		err = -ECONNABORTED;
	break;

	case LAPD_DLS_LINK_CONNECTION_ESTABLISHED:
		lapd_discard_iqueue(sk);
		lo->retrans_cnt = 0;
		lapd_send_uframe(sk, LAPD_UFRAME_FUNC_SABME, 1, NULL, 0);
		lapd_start_t200(sk);

		if (!lo->in_timer_recovery)
			lapd_stop_t203(sk);

		lapd_change_state(sk, LAPD_DLS_AWAITING_ESTABLISH);
	break;

	case LAPD_DLS_NULL:
	case LAPD_DLS_LISTENING:
		lapd_printk(KERN_ERR,
			"Unexpected MDL-ERROR-RESPONSE in state %d\n",
			lo->state);
	break;
	}

wouldblock:
err_multiframe_wait_for_establishment:
err_already_establishing:
err_tei_unassigned:
err_listening:

	release_sock(sk);

	return err;
}

static int lapd_wait_for_new_dlc(struct sock *sk)
{
	int err = 0;
	DEFINE_WAIT(wait);
	int timeout = 10 * HZ;

	for (;;) {
		prepare_to_wait_exclusive(sk->sk_sleep, &wait,
					  TASK_INTERRUPTIBLE);
		release_sock(sk);

		timeout = schedule_timeout(timeout);

		lock_sock(sk);

		struct lapd_opt *lo = lapd_sk(sk);
		if (!hlist_empty(&lo->new_dlcs))
			break;

		if (signal_pending(current)) {
			err = sock_intr_errno(timeout);
			break;
		}

		if (!timeout) {
			err = -EAGAIN;
			break;
		}
	}
	finish_wait(sk->sk_sleep, &wait);

	return err;
}

static int lapd_accept(struct socket *sock,
	struct socket *newsock, int flags)
{
	struct sock *sk = sock->sk;
	int err = 0;

	lock_sock(sk);

	if (sk->sk_state != TCP_LISTEN) {
		err = -EINVAL;
		goto err_notlistening;
	}

	struct lapd_opt *lo = lapd_sk(sk);
	if (hlist_empty(&lo->new_dlcs)) {
		err = lapd_wait_for_new_dlc(sk);
		if (err < 0)
			goto err_wait_for_new_dlc;
	}

	struct lapd_new_dlc *new_dlc;

	new_dlc = hlist_entry(lo->new_dlcs.first, struct lapd_new_dlc, node);

	sock_graft(new_dlc->sk, newsock);

	hlist_del(&new_dlc->node);
	kfree(new_dlc);

err_wait_for_new_dlc:
err_notlistening:
	release_sock(sk);

	return err;
}

static int lapd_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	struct lapd_opt *lo = lapd_sk(sk);
	int err = 0;

	lock_sock(sk);

	if (!lo->dev) {
		err = -ENODEV;
		goto err_no_dev;
	}

	if (!lo->nt_mode) {
		err = -EOPNOTSUPP;
		goto err_no_nt_mode;
	}

	if (sk->sk_state == TCP_CLOSING) {
		err = -EIO;
		goto err_closing;
	}

	if (sk->sk_state == TCP_LISTEN) {
		err = -EOPNOTSUPP;
		goto err_already_listening;
	}

	sk->sk_max_ack_backlog = backlog;
	sk->sk_state = TCP_LISTEN;

	lo->state = LAPD_DLS_LISTENING;

err_already_listening:
err_closing:
err_no_nt_mode:
err_no_dev:

	release_sock(sk);

	return err;
}

static int lapd_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	struct lapd_opt *lo = lapd_sk(sk);
	int err = 0;

	lapd_debug_multiframe(sk,
		"Starting multiframe release\n");

	lock_sock(sk);

	switch (lo->state) {
	case LAPD_DLS_TEI_ASSIGNED:
		lapd_dl_release_confirm(sk);
	break;

	case LAPD_DLS_AWAITING_REESTABLISH:
		lapd_change_state(sk, LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE);
	break;

	case LAPD_DLS_LINK_CONNECTION_ESTABLISHED:
		lapd_discard_iqueue(sk);
		lo->retrans_cnt = 0;
		lapd_send_uframe(sk, LAPD_UFRAME_FUNC_DISC, 1, NULL, 0);
		lapd_start_t200(sk);

		if (!lo->in_timer_recovery)
			lapd_stop_t203(sk);

		lapd_change_state(sk, LAPD_DLS_AWAITING_RELEASE);
	break;

	case LAPD_DLS_NULL:
	case LAPD_DLS_LISTENING:
	case LAPD_DLS_TEI_UNASSIGNED:
	case LAPD_DLS_AWAITING_TEI:
	case LAPD_DLS_ESTABLISH_AWAITING_TEI:
	case LAPD_DLS_AWAITING_ESTABLISH:
	case LAPD_DLS_AWAITING_ESTABLISH_PENDING_RELEASE:
	case LAPD_DLS_AWAITING_RELEASE:
		err = -ENOTSUPP;
		goto err_not_supported;
	break;
	}

	err = lapd_multiframe_wait_for_release(sk);
	if (err)
		goto err_multiframe_wait_for_release;

err_multiframe_wait_for_release:
err_not_supported:

	release_sock(sk);

	return err;
}

static struct proto_ops SOCKOPS_WRAPPED(lapd_dgram_ops) = {
	.family		= PF_LAPD,
	.owner		= THIS_MODULE,
	.release	= lapd_release,
	.bind		= sock_no_bind,
	.connect	= lapd_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= lapd_accept,
	.getname	= sock_no_getname,
	.poll		= lapd_poll,
	.ioctl		= lapd_ioctl,
	.listen		= lapd_listen,
	.shutdown	= lapd_shutdown,
	.setsockopt	= lapd_setsockopt,
	.getsockopt	= lapd_getsockopt,
	.sendmsg	= lapd_sendmsg,
	.recvmsg	= lapd_recvmsg,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage,
};

#include <linux/smp_lock.h>
SOCKOPS_WRAP(lapd_dgram, PF_LAPD);



static int lapd_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	int err;

	// LAPD is a privileged socket
	if (!capable(CAP_NET_BIND_SERVICE)) {
		err = -EPERM;
		goto err_no_cap;
	}

	if (sock->type != SOCK_SEQPACKET) {
		err = -ESOCKTNOSUPPORT;
		goto err_no_type;
	}

	if (protocol != LAPD_SAPI_Q931 &&
	    protocol != LAPD_SAPI_X25) {
		err = -EINVAL;
		goto err_invalid_protocol;
	}

	sk = sk_alloc(PF_LAPD, GFP_KERNEL, sizeof(struct lapd_sock),
			lapd_sk_cachep);
	if (!sk) {
		err = -ENOMEM;
		goto err_sk_alloc;
	}

	sock->ops = &lapd_dgram_ops;
	sock->state = SS_UNCONNECTED;

	sock_init_data(sock, sk);
	sk_set_owner(sk, THIS_MODULE);

	sk->sk_destruct = lapd_sock_destruct;
	sk->sk_backlog_rcv = lapd_backlog_rcv;
	sk->sk_zapped = 0;
	sk->sk_protocol = protocol;
	sk->sk_state = TCP_CLOSE;

	init_timer(&sk->sk_timer);
	sk->sk_timer.function = &lapd_unhash_timer;
	sk->sk_timer.data = (unsigned long)sk;

	struct lapd_opt *lo = lapd_sk(sk);

	skb_queue_head_init(&lo->u_queue);

	// We use ->sapi as a temporary until SO_BINDTODEVICE
	lo->sapi = protocol;
 
	// TE mode section

	lo->nt_mode = FALSE;

	lo->usr_tme = NULL;

	init_timer(&lo->T200_timer);
	lo->T200_timer.function = lapd_T200_timer;
	lo->T200_timer.data = (unsigned long)sk;

	init_timer(&lo->T203_timer);
	lo->T203_timer.function = lapd_T203_timer;
	lo->T203_timer.data = (unsigned long)sk;

	lo->state = LAPD_DLS_NULL;

	lo->peer_busy = FALSE;
	lo->me_busy = FALSE;
	lo->rejection_exception = FALSE;
	lo->in_timer_recovery = FALSE;

	INIT_HLIST_HEAD(&lo->new_dlcs);

	return 0;

//	sk_free(sk);
err_sk_alloc:
err_invalid_protocol:
err_no_type:
err_no_cap:

	return err;
}



static struct notifier_block lapd_notifier = {
	.notifier_call	= lapd_device_event,
};

static struct net_proto_family lapd_family_ops = {
	.family		= PF_LAPD,
	.create		= lapd_create,
	.owner		= THIS_MODULE,
};

struct packet_type lapd_packet_type = {
	.type		= __constant_htons(ETH_P_LAPD),
	.dev		= NULL,
	.func		= lapd_rcv,
};

static int __init lapd_init(void)
{
	int err;

	int i;
	for (i=0; i< ARRAY_SIZE(lapd_hash); i++) {
		INIT_HLIST_HEAD(&lapd_hash[i]);
	}

	lapd_sk_cachep = kmem_cache_create("lapd_sock",
				sizeof(struct lapd_sock), 0,
				SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!lapd_sk_cachep) {
		lapd_printk(KERN_CRIT,
			"Can't create protocol sock SLAB caches!\n");

		err = -ENOMEM;
		goto err_kmem_cache_create;
	}

	sock_register(&lapd_family_ops);
	dev_add_pack(&lapd_packet_type);

	register_netdevice_notifier(&lapd_notifier);

	lapd_proc_init();

	return 0;

//	kmem_cache_destroy(lapd_sk_cachep);
err_kmem_cache_create:

	return err;
}
module_init(lapd_init);

static void __exit lapd_exit(void)
{
	// Free device structures

	lapd_proc_exit();

	BUG_TRAP(hlist_empty(&lapd_ntme_hash));
	BUG_TRAP(hlist_empty(&lapd_utme_hash));

	unregister_netdevice_notifier(&lapd_notifier);
	dev_remove_pack(&lapd_packet_type);

	sock_unregister(PF_LAPD);

	kmem_cache_destroy(lapd_sk_cachep);
}
module_exit(lapd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniele Orlandi <daniele@orlandi.com>");
MODULE_DESCRIPTION("LAPD 0.0.0\n");
MODULE_ALIAS_NETPROTO(PF_LAPD);
