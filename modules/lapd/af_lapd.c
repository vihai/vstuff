/*
 * vISDN LAPD/q.921 protocol implementation
 *
 * Copyright (C) 2004-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#if defined(DEBUG_CODE) && !defined(SOCK_DEBUGGING)
#define SOCK_DEBUGGING
#endif

#include <linux/autoconf.h>
#include <linux/module.h>
#include <linux/termios.h>
#include <linux/tcp.h>
#include <linux/if_arp.h>
#include <linux/random.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/if.h>
#include <linux/version.h>
#include <net/datalink.h>
#include <net/sock.h>

#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>

#include <kernel_config.h>

#include "lapd.h"
#include "input.h"
#include "output.h"
#include "device.h"
#include "tei_mgmt_nt.h"
#include "tei_mgmt_te.h"
#include "datalink.h"
#include "sock_inline.h"

struct hlist_head lapd_hash[LAPD_HASHSIZE];
rwlock_t lapd_hash_lock = RW_LOCK_UNLOCKED;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
static kmem_cache_t *lapd_sk_cachep;
#else
static struct proto lapd_proto = {
	.name = lapd_MODULE_NAME,
	.owner = THIS_MODULE,
	.obj_size = sizeof(struct lapd_sock),
};
#endif

#ifdef CONFIG_PROC_FS
struct lapd_iter_state
{
	int bucket;
};

#define lapd_seq_private(seq) ((struct lapd_iter_state *)(seq)->private)

static struct sock *lapd_get_first(struct seq_file *seq)
{
	struct lapd_iter_state *state = seq->private;

	for (state->bucket = 0;
	     state->bucket < ARRAY_SIZE(lapd_hash);
	     state->bucket++) {
		struct sock *sk = sk_head(&lapd_hash[state->bucket]);
		if (sk)
			return sk;
	}

	return NULL;
}

static struct sock *lapd_get_next(struct seq_file *seq, struct sock *sk)
{
	struct lapd_iter_state *state = seq->private;

	do {
		sk = sk_next(sk);
		if (sk)
			return sk;

		do {
			state->bucket++;
			if (state->bucket >= ARRAY_SIZE(lapd_hash))
				return NULL;

			sk = sk_head(&lapd_hash[state->bucket]);
		} while(!sk);
	} while(!sk);

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

static void *lapd_seq_next(struct seq_file *seq, void *data, loff_t *pos)
{
	struct sock *sk;

	if (data == SEQ_START_TOKEN)
		sk = lapd_get_first(seq);
	else
		sk = lapd_get_next(seq, data);

	++*pos;

	return sk;
}

static void lapd_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock(&lapd_hash_lock);
}

static int lapd_seq_show(struct seq_file *seq, void *data)
{
	struct sock *sk = data;
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);

	if (data == SEQ_START_TOKEN) {
		seq_printf(seq, "%-127s\n",
			" interface   st sa:te vs va vr ecxpt"
			" tx_queue rx_queue   uid inode ref"
			);

		return 0;
	}

	seq_printf(seq, "%-12s %02X %02X:%02X"
			" %02X %02X %02X %c%c%c   %08X:%08X %5d %5lu %3d\n",
			(lapd_sock->dev ? lapd_sock->dev->dev->name : "NULL"),
			lapd_sock->state,
			lapd_sock->sapi,
			lapd_sock->tei,
			lapd_sock->v_s,
			lapd_sock->v_a,
			lapd_sock->v_r,
			lapd_sock->own_receiver_busy ? 'B' : ' ',
			lapd_sock->peer_receiver_busy ? 'M' : ' ',
			lapd_sock->reject_exception ? 'R' : ' ',
			atomic_read(&sk->sk_wmem_alloc),
			atomic_read(&sk->sk_rmem_alloc),
			sock_i_uid(sk), sock_i_ino(sk),
			atomic_read(&sk->sk_refcnt));

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
	int err;

	struct lapd_iter_state *iter_state;
	iter_state = kmalloc(sizeof(*iter_state), GFP_KERNEL);
	if (!iter_state) {
		err = -ENOMEM;
		goto err_kmalloc;
	}

	err = seq_open(file, &lapd_seq_ops);
	if (err)
		goto err_seq_open;

	seq = file->private_data;
	seq->private = iter_state;
	memset(iter_state, 0, sizeof(*iter_state));

	return 0;

err_seq_open:
	kfree(iter_state);
err_kmalloc:

	return err;
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
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	if (!proc_net_fops_create(lapd_MODULE_NAME, S_IRUGO, &lapd_seq_fops))
		return -ENOMEM;
#else
	if (!proc_net_fops_create(&init_net, lapd_MODULE_NAME, S_IRUGO,
							&lapd_seq_fops))
		return -ENOMEM;
#endif
	return 0;
}

void __exit lapd_proc_exit(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	proc_net_remove(lapd_MODULE_NAME);
#else
	proc_net_remove(&init_net, lapd_MODULE_NAME);
#endif
}

#endif

static void lapd_sock_destruct(struct sock *sk)
{
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);

	lapd_debug_ls(lapd_sock, "lapd_sock_destruct()\n");

	if (!sock_flag(sk, SOCK_DEAD)) {
		lapd_msg_ls(lapd_sock, KERN_CRIT,
			"Attempt to release alive socket %p\n", sk);

		return;
	}

	WARN_ON(!sk_unhashed(sk));

	if (!hlist_empty(&lapd_sock->new_dlcs)) {
		struct lapd_new_dlc *new_dlc;
		struct hlist_node *pos, *t;

		hlist_for_each_entry_safe(new_dlc, pos, t,
					&lapd_sock->new_dlcs, node) {
			struct sock *newsk = &new_dlc->lapd_sock->sk;

			WARN_ON(sk_unhashed(newsk));

			sk_del_node_init(newsk);

			sock_orphan(newsk);

			sock_put(&lapd_sock->sk);
			new_dlc->lapd_sock = NULL;

			hlist_del(&new_dlc->node);
			kfree(new_dlc);
		}
	}

	if (lapd_sock->usr_tme) {
		write_lock_bh(&lapd_utme_hash_lock);
		hlist_del(&lapd_sock->usr_tme->node);
		write_unlock_bh(&lapd_utme_hash_lock);

		lapd_utme_put(lapd_sock->usr_tme);

		lapd_utme_put(lapd_sock->usr_tme);
		lapd_sock->usr_tme = NULL;
	}

	if (lapd_sock->dev) {
		lapd_dev_put(lapd_sock->dev);
		lapd_sock->dev = NULL;
	}

	__skb_queue_purge(&sk->sk_write_queue);
	__skb_queue_purge(&sk->sk_receive_queue);
	__skb_queue_purge(&sk->sk_error_queue);
	__skb_queue_purge(&lapd_sock->u_queue);

	WARN_ON(!skb_queue_empty(&sk->sk_write_queue));
	WARN_ON(!skb_queue_empty(&sk->sk_receive_queue));
	WARN_ON(!skb_queue_empty(&sk->sk_error_queue));
	WARN_ON(!skb_queue_empty(&lapd_sock->u_queue));

	WARN_ON(atomic_read(&sk->sk_rmem_alloc));
	WARN_ON(atomic_read(&sk->sk_wmem_alloc));
}

static int lapd_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);

	lapd_debug_ls(lapd_sock, "lapd_release()\n");

	if (!sk)
		return 0;

	lapd_lock_sock(lapd_sock);

	if (sk->sk_state == LAPD_SK_STATE_NORMAL_DLC &&
	    (lapd_sock->state == LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED ||
	     lapd_sock->state == LAPD_DLS_8_TIMER_RECOVERY)) {

		/* Be kind and bring down the DLC.
		 * Since we need to receive responses the unhash is deferred
		 * after DL-RELEASE-CONFIRM
		 */

		sk->sk_state = LAPD_SK_STATE_NORMAL_DLC_CLOSING;
		lapd_dl_release_request(lapd_sock);
	} else {
		sk->sk_state = LAPD_SK_STATE_CLOSE;

		write_lock_bh(&lapd_hash_lock);
		sk_del_node_init(sk);
		write_unlock_bh(&lapd_hash_lock);
	}

	lapd_release_sock(lapd_sock);

	local_bh_disable();
	lapd_bh_lock_sock(lapd_sock);
	WARN_ON(sock_owned_by_user(sk));

	sock_orphan(sk);

	sock->sk = NULL;

	lapd_bh_unlock_sock(lapd_sock);
	local_bh_enable();

	sock_put(sk);

	return 0;
}

static int lapd_ioctl(
	struct socket *sock,
	unsigned int cmd,
	unsigned long arg)
{
	int err = -EINVAL;
	struct sock *sk = sock->sk;
	void __user *argp = (void __user *)arg;

	switch (cmd) {

	case SIOCOUTQ: {
		long amount = sk->sk_sndbuf -
			      atomic_read(&sk->sk_wmem_alloc);

		if (amount < 0)
			amount = 0;
		err = put_user(amount, (int __user *)argp);
	}
	break;

	case SIOCINQ: {
		struct sk_buff *skb;
		int size = 0;

		spin_lock_bh(&sk->sk_receive_queue.lock);
		skb = skb_peek(&sk->sk_receive_queue);
		if (skb && skb->dev) {
			struct lapd_data_hdr *hdr =
				(struct lapd_data_hdr *)skb->data;

		        if (lapd_frame_type(hdr->control) ==
				LAPD_FRAME_TYPE_UFRAME)
				size = skb->len -
					sizeof(struct lapd_data_hdr) -
					sizeof(u16);
			else
				size = skb->len -
					sizeof(struct lapd_data_hdr_e) -
					sizeof(u16);
		}
		spin_unlock_bh(&sk->sk_receive_queue.lock);

		err = put_user(size, (int __user *)argp);
	}
	break;

	case SIOCGSTAMP:
		err = sock_get_timestamp(sk, argp);
	break;

	default:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16)
		if (sk->sk_prot->ioctl)
			err = sk->sk_prot->ioctl(sk, cmd, arg);
		else
			err = -ENOIOCTLCMD;
#else
			err = dev_ioctl(cmd, argp);
#endif
	break;
	}

	return err;
}

static int lapd_sendmsg(
	struct kiocb *iocb, struct socket *sock,
	struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);
	struct sk_buff *skb;
	int err;

	lapd_lock_sock(lapd_sock);

	err = sock_error(sk);
	if (err)
		goto err_socket_error;

	if (sk->sk_state != LAPD_SK_STATE_NORMAL_DLC &&
	    sk->sk_state != LAPD_SK_STATE_BROADCAST_DLC &&
	    sk->sk_state != LAPD_SK_STATE_MGMT) {
		err = -EINVAL;
		goto err_not_valid;
	}

	if (sk->sk_shutdown & SEND_SHUTDOWN) {
		err = -EPIPE;
		goto err_shutting_down;
	}

	if (!lapd_sock->dev) {
		err = -ENODEV;
		goto err_no_dev;
	}

	if (len > lapd_sock->dev->dev->mtu) {
		err = -EMSGSIZE;
		goto err_over_mtu;
	}

	/* TODO, finish async operation
	 * This should be in poll */
	clear_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);

	if (msg->msg_flags & MSG_OOB) {

		lapd_release_sock(lapd_sock);
		skb = sock_alloc_send_skb(sk,
			sizeof(struct lapd_prim_hdr) +
			sizeof(struct lapd_data_hdr_e) + len,
			(msg->msg_flags & MSG_DONTWAIT), &err);

		lapd_lock_sock(lapd_sock);
		if (!skb) {
			err = -ENOMEM;
			goto err_sock_alloc_send_skb;
		}

		skb_reserve(skb, sizeof(struct lapd_prim_hdr));

		err = lapd_prepare_uframe(lapd_sock, skb,
					LAPD_UFRAME_FUNC_UI, 0);
		if(err < 0)
			goto err_prepare_frame;

		err = memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len);
		if (err)
			goto err_memcpy_fromiovec;

		lapd_dl_unit_data_request(lapd_sock, skb);

	} else {
		/* FIXME TODO
		 * sock_alloc_send_skb may sleep, sleeping with sk lock
		 * no new frames may be received, including acks that may
		 * free the output queue
		 */

		lapd_release_sock(lapd_sock);

		skb = sock_alloc_send_skb(sk,
			sizeof(struct lapd_prim_hdr) +
			sizeof(struct lapd_data_hdr_e) + len,
			(msg->msg_flags & MSG_DONTWAIT), &err);

		lapd_lock_sock(lapd_sock);
		if (!skb)
			goto err_sock_alloc_send_skb;

		skb_reserve(skb, sizeof(struct lapd_prim_hdr));

		err = lapd_prepare_iframe(lapd_sock, skb);
		if(err < 0)
			goto err_prepare_frame;

		err = memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len);
		if (err)
			goto err_memcpy_fromiovec;

		lapd_dl_data_request(lapd_sock, skb);
	}

	lapd_release_sock(lapd_sock);

	return len;

err_memcpy_fromiovec:
err_prepare_frame:
	kfree_skb(skb);
err_sock_alloc_send_skb:
err_over_mtu:
err_no_dev:
err_shutting_down:
err_not_valid:
err_socket_error:

	lapd_release_sock(lapd_sock);

	return err;
}

void lapd_dl_primitive(
	struct lapd_sock *lapd_sock,
	enum lapd_dl_primitive_type type,
	int param)
{
	struct sk_buff *skb;
	struct lapd_dl_primitive *pri;
	int skb_len;

	if (lapd_sock->sk.sk_state == LAPD_SK_STATE_NORMAL_DLC_CLOSING &&
	    type == LAPD_DL_RELEASE_CONFIRM) {
		lapd_debug_ls(lapd_sock, "Scheduling unhash\n");

		/* Defer unhash */
		sk_reset_timer(&lapd_sock->sk,
			&lapd_sock->sk.sk_timer,
			jiffies + 5 * HZ);

		return;
	}

	skb = alloc_skb(sizeof(struct lapd_dl_primitive), GFP_ATOMIC);
	if (!skb) {
		lapd_msg(KERN_ERR,
			"Cannot queue primitive %d to socket\n",
			type);

		return;
	}

	skb->dev = NULL;

	pri = (struct lapd_dl_primitive *)
		skb_put(skb, sizeof(struct lapd_dl_primitive));
	pri->type = type;
	pri->param = param;

	skb_set_owner_r(skb, &lapd_sock->sk);

	skb_len = skb->len;

	skb_queue_tail(&lapd_sock->sk.sk_receive_queue, skb);

	if (!sock_flag(&lapd_sock->sk, SOCK_DEAD))
		lapd_sock->sk.sk_data_ready(&lapd_sock->sk, skb_len);
}

int lapd_dl_unit_data_indication(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	int skb_len = skb->len;

	skb_set_owner_r(skb, &lapd_sock->sk);

	skb_queue_tail(&lapd_sock->sk.sk_receive_queue, skb);

	if (!sock_flag(&lapd_sock->sk, SOCK_DEAD))
		lapd_sock->sk.sk_data_ready(&lapd_sock->sk, skb_len);

	return TRUE;
}

void lapd_dl_data_indication(
	struct lapd_sock *lapd_sock,
	struct sk_buff *skb)
{
	int skb_len = skb->len;

	skb_set_owner_r(skb, &lapd_sock->sk);

	skb_queue_tail(&lapd_sock->sk.sk_receive_queue, skb);

	if (!sock_flag(&lapd_sock->sk, SOCK_DEAD))
		lapd_sock->sk.sk_data_ready(&lapd_sock->sk, skb_len);
}

static int lapd_recvmsg(struct kiocb *iocb, struct socket *sock,
	struct msghdr *msg, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);
	struct sk_buff *skb;
	struct lapd_data_hdr *hdr;
	int hdrsize;
	int err;
	int copied;

	lapd_lock_sock(lapd_sock);

	if (sk->sk_state != LAPD_SK_STATE_NORMAL_DLC &&
	    sk->sk_state != LAPD_SK_STATE_BROADCAST_DLC &&
	    sk->sk_state != LAPD_SK_STATE_MGMT) {
		err = -EINVAL;
		goto err_not_valid;
	}

	if (sk->sk_shutdown & RCV_SHUTDOWN) {
		err = -EPIPE;
		goto err_shutting_down;
	}

	if (!lapd_sock->dev) {
		err = -ENODEV;
		goto err_no_dev;
	}

	lapd_release_sock(lapd_sock);

	skb = skb_recv_datagram(sk, flags, flags & MSG_DONTWAIT, &err);
	if (!skb)
		goto err_recv_datagram;

	if (sk->sk_protocol == LAPD_SAPI_MGMT) {
		skb_copy_datagram_iovec(skb, 0, msg->msg_iov, skb->len);
		copied = skb->len;
	} else if (skb->dev) {
		hdr = (struct lapd_data_hdr *)skb->data;

		if (lapd_frame_type(hdr->control) == LAPD_FRAME_TYPE_UFRAME) {
			msg->msg_flags |= MSG_OOB;
			hdrsize = sizeof(struct lapd_data_hdr);
		} else {
			hdrsize = sizeof(struct lapd_data_hdr_e);
		}

		/* Remove header and CRC */
		copied = skb->len - hdrsize - sizeof(u16);
		if (copied > size) {
			copied = size;
			msg->msg_flags |= MSG_TRUNC;
		}

		skb_copy_datagram_iovec(skb, hdrsize, msg->msg_iov, copied);

	} else {
		struct lapd_dl_primitive *pri =
			(struct lapd_dl_primitive *)skb->data;

		switch(pri->type) {
		case LAPD_DL_ESTABLISH_INDICATION:
			copied = -EALREADY;
		break;

		case LAPD_DL_ESTABLISH_CONFIRM:
			copied = -EISCONN;
		break;

		case LAPD_DL_RELEASE_INDICATION:
			copied = -ECONNRESET;
		break;

		case LAPD_DL_RELEASE_CONFIRM:
			copied = -ENOTCONN;
		break;

		default:
			BUG();
			copied = 0;
		}
	}

	skb_free_datagram(sk, skb);

	return copied;

	skb_free_datagram(sk, skb);
err_recv_datagram:
	lapd_lock_sock(lapd_sock);
err_no_dev:
err_shutting_down:
err_not_valid:

	lapd_release_sock(lapd_sock);

	return err;
}

unsigned int lapd_poll(struct file *file,
	struct socket *sock, poll_table *wait)
{
	unsigned int mask;
	struct sock *sk = sock->sk;

	poll_wait(file, sk->sk_sleep, wait);

	if (sk->sk_state == LAPD_SK_STATE_LISTEN) {
		struct lapd_sock *lapd_sock = to_lapd_sock(sk);

		return !hlist_empty(&lapd_sock->new_dlcs) ?
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
	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= POLLHUP;

	/* readable? */
	if (!skb_queue_empty(&sk->sk_receive_queue))
		mask |= POLLIN | POLLRDNORM;

	/* writable? */
	if (sock_writeable(sk))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;
	else
		set_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);

	return mask;
}

static int lapd_bind_to_device(struct sock *sk, const char *devname)
{
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);
	int err = 0;
	struct lapd_device *dev;

	if (sk->sk_type != SOCK_SEQPACKET) {
		err = -EINVAL;
		goto err_invalid_socket_type;
	}

	dev = lapd_dev_get_by_name(devname);
	if (!dev) {
		err = -ENODEV;
		goto err_nodev;
	}

	if (!(dev->dev->flags & IFF_UP)) {
		err = -ENETDOWN;
		goto err_dev_not_up;
	}

	lapd_sock->state = LAPD_DLS_1_TEI_UNASSIGNED;

	/* Useless? */
	sk->sk_bound_dev_if = dev->dev->ifindex;

	/* The reference is passed to lapd_sock->dev */
	lapd_sock->dev = dev;

	if (lapd_sock->sapi == LAPD_SAPI_Q931)
		lapd_sock->sap = &lapd_sock->dev->q931;
	else if(lapd_sock->sapi == LAPD_SAPI_X25)
		lapd_sock->sap = &lapd_sock->dev->x25;
	else if (sk->sk_protocol == LAPD_SAPI_MGMT)
		sk->sk_state = LAPD_SK_STATE_MGMT;

	write_lock_bh(&lapd_hash_lock);
	sk_add_node(sk, lapd_get_hash(lapd_sock->dev));
	write_unlock_bh(&lapd_hash_lock);

	return 0;

err_dev_not_up:
	lapd_dev_put(dev);
err_nodev:
err_invalid_socket_type:

	return err;
}


static int lapd_setsockopt(struct socket *sock, int level, int optname,
	char __user *optval_u, int optlen)
{
	struct sock *sk = sock->sk;
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);
	int err = 0;
	int intoptval = 0;

	if (level != SOL_LAPD)
		return -ENOPROTOOPT;

	if (sk->sk_shutdown & SHUTDOWN_MASK)
		return -EPIPE;

	if (optlen == sizeof(int)) {
		if (get_user(intoptval, (int __user *)optval_u)) {
			err = -EFAULT;
			goto err_copy_from_user;
		}
	}

	lapd_lock_sock(lapd_sock);

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

		/* Is this really needed? */
		devname[sizeof(devname)-1] = '\0';

		err = lapd_bind_to_device(sk, devname);
		if (err < 0)
			goto err_bind_to_device;
	}
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

		if (!lapd_sock->dev) {
			err = -ENODEV;
			break;
		}

		if (lapd_sock->dev->role == LAPD_INTF_ROLE_TE) {
			err = -EINVAL;
			break;
		}

		lapd_sock->dev->net_tme->T201 = intoptval * HZ / 1000;
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

		if (!lapd_sock->dev) {
			err = -ENODEV;
			break;
		}

		if (lapd_sock->dev->role == LAPD_INTF_ROLE_NT) {
			err = -EINVAL;
			break;
		}

		lapd_sock->usr_tme->N202 = intoptval;
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

		if (!lapd_sock->dev) {
			err = -ENODEV;
			break;
		}

		if (lapd_sock->dev->role == LAPD_INTF_ROLE_NT) {
			err = -EINVAL;
			break;
		}

		lapd_sock->usr_tme->N202 = intoptval * HZ / 1000;
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

		lapd_sock->sap->T200 = intoptval * HZ / 1000;
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

		lapd_sock->sap->N200 = intoptval;
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

		lapd_sock->sap->T203 = intoptval * HZ / 1000;
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

		lapd_sock->sap->N201 = intoptval;
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

		lapd_sock->sap->k = intoptval;
	break;

	default:
		err = -ENOPROTOOPT;
	}

err_bind_to_device:
err_copy_from_user:
err_invalid_optlen:

	lapd_release_sock(lapd_sock);

	return err;
}

static int lapd_getsockopt(
	struct socket *sock, int level, int optname,
	char __user *optval_u, int __user *optlen_u)
{
	int err = 0;
	struct sock *sk = sock->sk;
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);
	int optlen;
	int val = 0;
	void *optval = (void *)&val;
	int length;

	if (level != SOL_LAPD)
		return -ENOPROTOOPT;

	if (get_user(optlen, optlen_u)) {
		err = -EFAULT;
		goto err_get_user;
	}

	length = min_t(unsigned int, optlen, sizeof(int));

	/* By default use an int */

	lapd_lock_sock(lapd_sock);

	switch (optname) {
	case SO_BINDTODEVICE: {
		char devname[IFNAMSIZ];

		if (lapd_sock->dev) {
			strlcpy(devname, lapd_sock->dev->dev->name,
				sizeof(devname));
			length = strlen(devname) + 1;
		} else {
			*devname = '\0';
			length = 1;
		}

		optval = (void *) devname;
		}
	break;

	case LAPD_INTF_TYPE:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lapd_sock->dev->type;
	break;

	case LAPD_INTF_MODE:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lapd_sock->dev->mode;
	break;

	case LAPD_INTF_ROLE:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		if (!lapd_sock->dev) {
			err = -ENODEV;
			break;
		}

		val = lapd_sock->dev->role;
	break;

	case LAPD_TEI:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lapd_sock->tei;
	break;

	case LAPD_TEI_MGMT_STATUS:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		if (!lapd_sock->usr_tme) {
			err = -EINVAL;
			goto err_invalid_request;
		}

		val = lapd_sock->usr_tme->state;
	break;

	case LAPD_TEI_MGMT_T201:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		if (!lapd_sock->dev) {
			err = -ENODEV;
			goto err_invalid_request;
		}

		if (lapd_sock->dev->role != LAPD_INTF_ROLE_NT) {
			err = -EINVAL;
			goto err_invalid_request;
		}

		if (!lapd_sock->dev->net_tme) {
			err = -ENODEV;
			goto err_invalid_request;
		}

		val = lapd_sock->dev->net_tme->T201;
	break;

	case LAPD_TEI_MGMT_N202:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		if (!lapd_sock->dev) {
			err = -ENODEV;
			goto err_invalid_request;
		}

		if (lapd_sock->dev->role != LAPD_INTF_ROLE_TE) {
			err = -ENODEV;
			goto err_invalid_request;
		}

		val = lapd_sock->usr_tme->N202;
	break;

	case LAPD_TEI_MGMT_T202:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		if (!lapd_sock->dev) {
			err = -ENODEV;
			goto err_invalid_request;
		}

		if (lapd_sock->dev->role != LAPD_INTF_ROLE_TE) {
			err = -ENODEV;
			goto err_invalid_request;
		}

		val = lapd_sock->usr_tme->T202;
	break;

	case LAPD_T200:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lapd_sock->sap->T200;
	break;

	case LAPD_N200:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lapd_sock->sap->N200;
	break;

	case LAPD_T203:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lapd_sock->sap->T203;
	break;

	case LAPD_N201:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lapd_sock->sap->N201;
	break;

	case LAPD_K:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lapd_sock->sap->k;
	break;

	case LAPD_DLC_STATE:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lapd_sock->state;
	break;

	default:
		lapd_release_sock(lapd_sock);
		return -ENOPROTOOPT;
	}

	if (put_user(length, optlen_u)) {
		err = -EFAULT;
		goto err_put_user;
	}

	lapd_release_sock(lapd_sock);

	return copy_to_user(optval_u,
		optval,
		min_t(unsigned int, optlen, length)) ? -EFAULT : 0;

err_put_user:
err_invalid_optlen:
err_invalid_request:
	lapd_release_sock(lapd_sock);
err_get_user:

	return err;
}

static void lapd_unhash_timer(unsigned long data)
{
	struct sock *sk = (struct sock *)data;
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);

	if (lapd_sock)
		lapd_debug_ls(lapd_sock, "Unhash timer\n");

	WARN_ON(sk->sk_state != LAPD_SK_STATE_NORMAL_DLC_CLOSING);

	write_lock_bh(&lapd_hash_lock);
	sk_del_node_init(sk);
	write_unlock_bh(&lapd_hash_lock);

	sock_put(sk);
}

struct lapd_sock *lapd_new_sock(
	struct lapd_sock *parent_lapd_sock,
	u8 tei, int sapi)
{
	struct sock *new_sk;
	struct sock *parent_sk = &parent_lapd_sock->sk;
	struct lapd_sock *new_lapd_sock;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
	new_sk = sk_alloc(PF_LAPD, GFP_ATOMIC, sizeof(struct lapd_sock),
						parent_sk->sk_slab);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	new_sk = sk_alloc(PF_LAPD, GFP_ATOMIC, parent_sk->sk_prot, 1);
#else
	new_sk = sk_alloc(&init_net, PF_LAPD, GFP_ATOMIC, parent_sk->sk_prot);
#endif
	if (!new_sk)
		return NULL;

	sock_init_data(NULL, new_sk);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
	sk_set_owner(new_sk, THIS_MODULE);
#else
#endif

	new_sk->sk_destruct = lapd_sock_destruct;
	new_sk->sk_backlog_rcv = lapd_backlog_rcv;
	new_sk->sk_type = parent_sk->sk_type;
	new_sk->sk_priority = parent_sk->sk_priority;
	new_sk->sk_protocol = parent_sk->sk_protocol;
	new_sk->sk_rcvbuf = parent_sk->sk_rcvbuf;
	new_sk->sk_sndbuf = parent_sk->sk_sndbuf;
	new_sk->sk_state = LAPD_SK_STATE_NORMAL_DLC;
	new_sk->sk_sleep = parent_sk->sk_sleep;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
	new_sk->sk_zapped = parent_sk->sk_zapped;
	new_sk->sk_debug = parent_sk->sk_debug;
#else
	if (sock_flag(parent_sk, SOCK_ZAPPED))
		sock_set_flag(new_sk, SOCK_ZAPPED);

	if (sock_flag(parent_sk, SOCK_DBG))
		sock_set_flag(new_sk, SOCK_DBG);
#endif

	init_timer(&new_sk->sk_timer);
	new_sk->sk_timer.function = &lapd_unhash_timer;
	new_sk->sk_timer.data = (unsigned long)new_sk;

	new_lapd_sock = to_lapd_sock(new_sk);

	BUG_ON(!parent_lapd_sock->dev);
	new_lapd_sock->dev = parent_lapd_sock->dev;
	lapd_dev_get(new_lapd_sock->dev);

	skb_queue_head_init(&new_lapd_sock->u_queue);

	new_lapd_sock->usr_tme = NULL;

	INIT_HLIST_HEAD(&new_lapd_sock->new_dlcs);

	lapd_datalink_state_init(new_lapd_sock);
	new_lapd_sock->state = LAPD_DLS_4_TEI_ASSIGNED;

	new_lapd_sock->tei = tei;
	new_lapd_sock->sapi = sapi;

	if (sapi == LAPD_SAPI_Q931)
		new_lapd_sock->sap = &new_lapd_sock->dev->q931;
	else if(sapi == LAPD_SAPI_X25)
		new_lapd_sock->sap = &new_lapd_sock->dev->x25;

	return new_lapd_sock;
}

void lapd_mdl_error_indication(
	struct lapd_sock *lapd_sock,
	unsigned long indication)
{
	lapd_msg_ls(lapd_sock, KERN_WARNING,
		"MDL-ERROR-INDICATION(%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s)\n",
		(indication & LAPD_MDL_ERROR_INDICATION_A) ? "A" : "",
		(indication & LAPD_MDL_ERROR_INDICATION_B) ? "B" : "",
		(indication & LAPD_MDL_ERROR_INDICATION_C) ? "C" : "",
		(indication & LAPD_MDL_ERROR_INDICATION_D) ? "D" : "",
		(indication & LAPD_MDL_ERROR_INDICATION_E) ? "E" : "",
		(indication & LAPD_MDL_ERROR_INDICATION_F) ? "F" : "",
		(indication & LAPD_MDL_ERROR_INDICATION_G) ? "G" : "",
		(indication & LAPD_MDL_ERROR_INDICATION_H) ? "H" : "",
		(indication & LAPD_MDL_ERROR_INDICATION_I) ? "I" : "",
		(indication & LAPD_MDL_ERROR_INDICATION_J) ? "J" : "",
		(indication & LAPD_MDL_ERROR_INDICATION_K) ? "K" : "",
		(indication & LAPD_MDL_ERROR_INDICATION_L) ? "L" : "",
		(indication & LAPD_MDL_ERROR_INDICATION_M) ? "M" : "",
		(indication & LAPD_MDL_ERROR_INDICATION_N) ? "N" : "",
		(indication & LAPD_MDL_ERROR_INDICATION_O) ? "O" : "");

	if (indication & LAPD_MDL_ERROR_INDICATION_C ||
	    indication & LAPD_MDL_ERROR_INDICATION_D ||
	    indication & LAPD_MDL_ERROR_INDICATION_G ||
	    indication & LAPD_MDL_ERROR_INDICATION_H) {

		if (lapd_sock->dev->role == LAPD_INTF_ROLE_NT) {
			if (lapd_sock->dev->net_tme)
				lapd_ntme_start_tei_check(
					lapd_sock->dev->net_tme,
					lapd_sock->tei);
		} else {
			if (lapd_sock->usr_tme)
				lapd_utme_tei_remove(lapd_sock->usr_tme);
		}
	}
}

int lapd_multiframe_wait_for_establishment(
	struct lapd_sock *lapd_sock,
	int nonblock)
{
	DEFINE_WAIT(wait);
	int err = 0;
	int timeout = 60 * HZ;

	if (lapd_sock->state == LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED)
		return -EISCONN;

	if (nonblock)
		return -EWOULDBLOCK;

	for (;;) {
		prepare_to_wait_exclusive(lapd_sock->sk.sk_sleep, &wait,
			TASK_INTERRUPTIBLE);

		lapd_release_sock(lapd_sock);
		/* Timeout is used only to detect abnormal cases */
		timeout = schedule_timeout(timeout);
		lapd_lock_sock(lapd_sock);

		err = sock_error(&lapd_sock->sk);
		if (err)
			break;

		if (lapd_sock->sk.sk_state != LAPD_SK_STATE_NORMAL_DLC)
			break;

		if (lapd_sock->state == LAPD_DLS_7_LINK_CONNECTION_ESTABLISHED)
			break;

		if (lapd_sock->state == LAPD_DLS_4_TEI_ASSIGNED) {
			err = -ECONNRESET;
			break;
		}

		if (signal_pending(current)) {
			err = sock_intr_errno(timeout);
			break;
		}

		if (!timeout) {
			lapd_msg_ls(lapd_sock, KERN_ERR,
				"Connect timed out?!?\n");

			err = -ETIMEDOUT;
			break;
		}
	}

	finish_wait(lapd_sock->sk.sk_sleep, &wait);

	return err;
}

static int lapd_bind(
	struct socket *sock,
	struct sockaddr *uaddr,
	int addr_len)
{
	struct sock *sk = sock->sk;
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);
	struct sockaddr_lapd *sal = (struct sockaddr_lapd *)uaddr;
	int err;

	if (addr_len < sizeof(struct sockaddr_lapd)) {
		err = -EINVAL;
		goto err_inv_sockaddr;
	}

	lapd_lock_sock(lapd_sock);

	if (sk->sk_state != LAPD_SK_STATE_NULL) {
		err = -EINVAL;
		goto err_not_null;
	}

	if (sk->sk_shutdown & SHUTDOWN_MASK) {
		err = -EPIPE;
		goto err_shutting_down;
	}

	if (!lapd_sock->dev) {
		err = -ENODEV;
		goto err_no_dev;
	}

	if (sal->sal_tei == LAPD_BROADCAST_TEI) {

		if (lapd_sock->usr_tme)
			lapd_utme_put(lapd_sock->usr_tme);

		lapd_sock->usr_tme = NULL; 
		lapd_sock->tei = sal->sal_tei;
		lapd_sock->state = LAPD_DLS_4_TEI_ASSIGNED;

		sk->sk_state = LAPD_SK_STATE_BROADCAST_DLC;

	} else  if (sal->sal_tei == LAPD_DYNAMIC_TEI ||
		  (/*sal->sal_tei >= LAPD_MIN_STA_TEI && */
	           sal->sal_tei <= LAPD_MAX_STA_TEI)) {

		sk->sk_state = LAPD_SK_STATE_NORMAL_DLC;

		if (lapd_sock->dev->role == LAPD_INTF_ROLE_NT) {
			if (sal->sal_tei == LAPD_DYNAMIC_TEI) {
				err = -EINVAL;
				goto err_dyn_and_nt;
			}

			lapd_sock->state = LAPD_DLS_4_TEI_ASSIGNED;
		} else {
			lapd_sock->state = LAPD_DLS_1_TEI_UNASSIGNED;

			if (lapd_sock->usr_tme) {
				write_lock_bh(&lapd_utme_hash_lock);
				hlist_del(&lapd_sock->usr_tme->node);
				write_unlock_bh(&lapd_utme_hash_lock);

				lapd_utme_put(lapd_sock->usr_tme);

				lapd_utme_put(lapd_sock->usr_tme);
				lapd_sock->usr_tme = NULL;
			}

			lapd_sock->usr_tme = lapd_utme_alloc(lapd_sock->dev);

			write_lock_bh(&lapd_utme_hash_lock);
			hlist_add_head(&lapd_utme_get(lapd_sock->usr_tme)->node,
					&lapd_utme_hash);
			write_unlock_bh(&lapd_utme_hash_lock);

			if (/*sal->sal_tei >= LAPD_MIN_STA_TEI && */
			    sal->sal_tei <= LAPD_MAX_STA_TEI)
				lapd_utme_assign_static_tei(
					lapd_sock->usr_tme, sal->sal_tei);
		}

	} else {
		err = -EINVAL;
		goto err_inv_tei;
	}

/*
	struct sock *othersk = NULL;
	struct hlist_node *node;
	int i;
	int bound_socket_present = FALSE;

	read_lock_bh(&lapd_hash_lock);
	for (i=0; i<ARRAY_SIZE(lapd_hash); i++) {
		sk_for_each(othersk, node, &lapd_hash[i]) {
			if (to_lapd_sock(othersk)->dev == dev &&
			    !to_lapd_sock(othersk)->usr_tme

				bound_socket_present = TRUE;

				break;
			}
		}
	}
	read_unlock_bh(&lapd_hash_lock);
*/

	lapd_release_sock(lapd_sock);

	return 0;

err_inv_tei:
err_dyn_and_nt:
err_no_dev:
err_shutting_down:
err_not_null:
	lapd_release_sock(lapd_sock);
err_inv_sockaddr:

	return err;
}

static int lapd_connect(
	struct socket *sock, struct sockaddr *uaddr,
	int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);

	int err;

	lapd_lock_sock(lapd_sock);

	if (sk->sk_state != LAPD_SK_STATE_NORMAL_DLC) {
		err = -EINVAL;
		goto err_not_valid;
	}

	if (sk->sk_shutdown & SHUTDOWN_MASK) {
		err = -EPIPE;
		goto err_shutting_down;
	}

	err = lapd_dl_establish_request(lapd_sock);
	if (err < 0)
		goto err_establish_request;

	err = lapd_multiframe_wait_for_establishment(
				lapd_sock, flags & O_NONBLOCK);
	if (err < 0)
		goto err_multiframe_wait_for_establishment;

	lapd_release_sock(lapd_sock);

	return 0;

err_multiframe_wait_for_establishment:
err_establish_request:
err_shutting_down:
err_not_valid:

	lapd_release_sock(lapd_sock);

	return err;
}

static int lapd_wait_for_new_dlc(struct lapd_sock *lapd_sock)
{
	int err = 0;
	DEFINE_WAIT(wait);
	int timeout = 10 * HZ;

	for (;;) {
		prepare_to_wait_exclusive(lapd_sock->sk.sk_sleep, &wait,
					  TASK_INTERRUPTIBLE);
		lapd_release_sock(lapd_sock);

		timeout = schedule_timeout(timeout);

		lapd_lock_sock(lapd_sock);

		if (!hlist_empty(&lapd_sock->new_dlcs))
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
	finish_wait(lapd_sock->sk.sk_sleep, &wait);

	return err;
}

static int lapd_accept(struct socket *sock,
	struct socket *newsock, int flags)
{
	struct sock *sk = sock->sk;
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);
	int err = 0;

	lapd_lock_sock(lapd_sock);

	if (lapd_sock->sk.sk_state != LAPD_SK_STATE_LISTEN) {
		err = -EINVAL;
		goto err_notlistening;
	}

	if (sk->sk_shutdown & SHUTDOWN_MASK) {
		err = -EPIPE;
		goto err_shutting_down;
	}

	if (hlist_empty(&lapd_sock->new_dlcs)) {
		if (flags & O_NONBLOCK) {
			err = -EWOULDBLOCK;
			goto wouldblock;
		} else {
			err = lapd_wait_for_new_dlc(lapd_sock);
			if (err < 0)
				goto err_wait_for_new_dlc;
		}
	}

	{
	struct lapd_new_dlc *new_dlc;

	new_dlc = hlist_entry(lapd_sock->new_dlcs.first,
				struct lapd_new_dlc, node);

	sock_graft(&new_dlc->lapd_sock->sk, newsock);

	hlist_del(&new_dlc->node);
	kfree(new_dlc);
	}

err_wait_for_new_dlc:
wouldblock:
err_shutting_down:
err_notlistening:
	lapd_release_sock(lapd_sock);

	return err;
}

static int lapd_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);
	int err;

	lapd_lock_sock(lapd_sock);

	if (sk->sk_state != LAPD_SK_STATE_NULL) {
		err = -EINVAL;
		goto err_closing;
	}

	if (sk->sk_shutdown & SHUTDOWN_MASK) {
		err = -EPIPE;
		goto err_shutting_down;
	}

	if (!lapd_sock->dev) {
		err = -ENODEV;
		goto err_no_dev;
	}

	sk->sk_max_ack_backlog = backlog;
	sk->sk_state = LAPD_SK_STATE_LISTEN;

	lapd_sock->state = LAPD_DLS_LISTENING;

	lapd_release_sock(lapd_sock);

	return 0;

err_no_dev:
err_shutting_down:
err_closing:

	lapd_release_sock(lapd_sock);

	return err;
}

int lapd_multiframe_wait_for_release(
	struct lapd_sock *lapd_sock,
	int nonblock)
{
	int err = 0;
	int timeout = 10 * HZ;
	DEFINE_WAIT(wait);

	if (lapd_sock->state == LAPD_DLS_4_TEI_ASSIGNED ||
	    lapd_sock->state == LAPD_DLS_1_TEI_UNASSIGNED)
		return -ENOTCONN;

	for (;;) {

		if (nonblock)
			return -EWOULDBLOCK;

		prepare_to_wait_exclusive(lapd_sock->sk.sk_sleep, &wait,
			TASK_INTERRUPTIBLE);

		lapd_release_sock(lapd_sock);
		/* Timeout is used only to detect abnormal cases */
		timeout = schedule_timeout(timeout);
		lapd_lock_sock(lapd_sock);

		if (lapd_sock->sk.sk_err) {
			err = -lapd_sock->sk.sk_err;
			break;
		}

		if (lapd_sock->state == LAPD_DLS_4_TEI_ASSIGNED ||
		    lapd_sock->state == LAPD_DLS_1_TEI_UNASSIGNED)
			break;

		if (signal_pending(current)) {
			err = sock_intr_errno(timeout);
			break;
		}

		if (!timeout) {
			lapd_msg_ls(lapd_sock, KERN_ERR,
				"Release timed out?!?\n");

			err = -ETIMEDOUT;
			break;
		}
	}

	finish_wait(lapd_sock->sk.sk_sleep, &wait);

	return err;
}

static int lapd_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	struct lapd_sock *lapd_sock = to_lapd_sock(sk);
	int err;

	lapd_debug_ls(lapd_sock,
		"shutdown()\n");

	lapd_lock_sock(lapd_sock);

	lapd_dl_release_request(lapd_sock);

	err = lapd_multiframe_wait_for_release(
			lapd_sock, sock->file->f_flags & O_NONBLOCK);
	if (err != -ENOTCONN)
		goto err_multiframe_wait_for_release;

	lapd_release_sock(lapd_sock);

	return 0;

err_multiframe_wait_for_release:

	lapd_release_sock(lapd_sock);

	return err;
}

static struct proto_ops SOCKOPS_WRAPPED(lapd_dgram_ops) = {
	.family		= PF_LAPD,
	.owner		= THIS_MODULE,
	.release	= lapd_release,
	.bind		= lapd_bind,
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
static int lapd_create(struct socket *sock, int protocol)
#else
static int lapd_create(struct net *net, struct socket *sock, int protocol)
#endif
{
	struct sock *sk;
	struct lapd_sock *lapd_sock;
	int err;

	/* LAPD is a privileged socket */
	if (!capable(CAP_NET_BIND_SERVICE)) {
		err = -EPERM;
		goto err_no_cap;
	}

	if (sock->type != SOCK_SEQPACKET) {
		err = -ESOCKTNOSUPPORT;
		goto err_no_type;
	}

	if (protocol != LAPD_SAPI_Q931 &&
	    protocol != LAPD_SAPI_X25 &&
	    protocol != LAPD_SAPI_MGMT) {
		err = -EINVAL;
		goto err_invalid_protocol;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
	sk = sk_alloc(PF_LAPD, GFP_KERNEL, sizeof(struct lapd_sock),
			lapd_sk_cachep);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	sk = sk_alloc(PF_LAPD, GFP_KERNEL, &lapd_proto, 1);
#else
	sk = sk_alloc(net, PF_LAPD, GFP_KERNEL, &lapd_proto);
#endif
	if (!sk) {
		err = -ENOMEM;
		goto err_sk_alloc;
	}

	sock->ops = &lapd_dgram_ops;
	sock->state = SS_UNCONNECTED;

	sock_init_data(sock, sk);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
	sk_set_owner(sk, THIS_MODULE);
	sk->sk_zapped = 0;
#endif

	if (protocol == LAPD_SAPI_MGMT)
		sk->sk_backlog_rcv = lapd_mgmt_backlog_rcv;
	else
		sk->sk_backlog_rcv = lapd_backlog_rcv;

	sk->sk_destruct = lapd_sock_destruct;
	sk->sk_protocol = protocol;
	sk->sk_state = LAPD_SK_STATE_NULL;

	init_timer(&sk->sk_timer);
	sk->sk_timer.function = &lapd_unhash_timer;
	sk->sk_timer.data = (unsigned long)sk;

	lapd_sock = to_lapd_sock(sk);

	skb_queue_head_init(&lapd_sock->u_queue);

	/* We use ->sapi as a temporary until SO_BINDTODEVICE */
	lapd_sock->sapi = protocol;

	lapd_sock->usr_tme = NULL;

	lapd_datalink_state_init(lapd_sock);

	INIT_HLIST_HEAD(&lapd_sock->new_dlcs);

	return 0;

	sk_free(sk);
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void lapd_watchdog(void *data);
#else
static void lapd_watchdog(struct work_struct *work);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
DECLARE_WORK(lapd_watchdog_work, lapd_watchdog, NULL);
#else
DECLARE_DELAYED_WORK(lapd_watchdog_work, lapd_watchdog);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void lapd_watchdog(void *data)
#else
static void lapd_watchdog(struct work_struct *work)
#endif
{
	lapd_ntme_audit();

	schedule_delayed_work(&lapd_watchdog_work, 60 * 60 * HZ);
}

static int __init lapd_init(void)
{
	int err;

	int i;
	for (i=0; i< ARRAY_SIZE(lapd_hash); i++)
		INIT_HLIST_HEAD(&lapd_hash[i]);

	lapd_out_init();

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
	lapd_sk_cachep = kmem_cache_create("lapd_sock",
				sizeof(struct lapd_sock), 0,
				SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!lapd_sk_cachep) {
		lapd_msg(KERN_CRIT,
			"Can't create protocol sock SLAB caches!\n");

		err = -ENOMEM;
		goto err_kmem_cache_create;
	}
#else
	err = proto_register(&lapd_proto, 1);
	if (err < 0)
		goto err_proto_register;
#endif

	sock_register(&lapd_family_ops);

	dev_add_pack(&lapd_packet_type);

	register_netdevice_notifier(&lapd_notifier);

	lapd_proc_init();

	schedule_delayed_work(&lapd_watchdog_work, 0);

	return 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
	kmem_cache_destroy(lapd_sk_cachep);
err_kmem_cache_create:
#else
	proto_unregister(&lapd_proto);
err_proto_register:
#endif

	return err;
}
module_init(lapd_init);

static void __exit lapd_exit(void)
{
	cancel_delayed_work(&lapd_watchdog_work);
	flush_scheduled_work();

	lapd_proc_exit();

	BUG_TRAP(hlist_empty(&lapd_ntme_hash));
	BUG_TRAP(hlist_empty(&lapd_utme_hash));

	unregister_netdevice_notifier(&lapd_notifier);
	dev_remove_pack(&lapd_packet_type);

	sock_unregister(PF_LAPD);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
	kmem_cache_destroy(lapd_sk_cachep);
#else
	proto_unregister(&lapd_proto);
#endif

	lapd_out_exit();
}
module_exit(lapd_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniele Orlandi <daniele@orlandi.com>");
MODULE_DESCRIPTION("LAPD protocol stack\n");
MODULE_ALIAS_NETPROTO(PF_LAPD);
