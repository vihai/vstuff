/*

  Add AF_LAPD to linux/include/linux/socket.h
  Add PF_LAPD to linux/include/linux/socket.h
  Add SOL_LAPD to linux/inclode/linx/socket.h ????

  Add ARPHRD_ISDN_DCHAN to linux/include/linux/if_arp.h
  Add ETH_P_LAPD to linux/include/linux/if_ether.h

  TODO: Determine where the information "I am a NT" or "I am a TE" should
  reside (lowlevel driver, socket layer) and how each layer can pass it
  to other layers.
  BRI chipsets must know if they act as NT or TE since it influences how
  the phisycal layer work. PRI cards do not need to know their role since
  they are perfectly symmetrical g.703 endpoints.
  IMHO it should be a card configuration parameter.

*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/termios.h> 
#include <linux/tcp.h>
#include <linux/if_arp.h>
#include <linux/random.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/datalink.h>
#include <net/sock.h>

#include "lapd_user.h"
#include "lapd.h"
#include "lapd_in.h"
#include "lapd_out.h"
#include "tei_mgmt_nt.h"
#include "tei_mgmt_te.h"
#include "multiframe.h"

struct hlist_head lapd_hash = HLIST_HEAD_INIT;
rwlock_t lapd_hash_lock = RW_LOCK_UNLOCKED;

static kmem_cache_t *lapd_sk_cachep;





#ifdef CONFIG_PROC_FS
struct lapd_iter_state {
	int bucket;
};

#define lapd_seq_private(seq) ((struct lapd_iter_state *)(seq)->private)

static struct sock *lapd_get_first(struct seq_file *seq)
{
	struct sock *sk;

	struct hlist_node *node;

	sk_for_each(sk, node, &lapd_hash) {
		if (sk->sk_family == PF_LAPD)
			goto found;
	}

	sk = NULL;

found:
	return sk;
}

static struct sock *lapd_get_next(struct seq_file *seq, struct sock *sk)
{
	sk = sk_next(sk);

	return sk;
}

static struct sock *lapd_get_idx(struct seq_file *seq, loff_t pos)
{
	struct sock *sk = lapd_get_first(seq);

	if (sk)
		while (pos && (sk = lapd_get_next(seq, sk)) != NULL)
			--pos;
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
		" %02X %02X %02X %c%c%c%c%c %08X:%08X %5d %5lu %3d %p",
		i,
		lo->status,
		lo->sapi,
		lo->tei,
		lo->v_s,
		lo->v_a,
		lo->v_r,
		lo->peer_busy?'B':' ',
		lo->me_busy?'M':' ',
		lo->peer_waiting_for_ack?'A':' ',
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

void setup_lapd(struct net_device *netdev)
{
	/* Fill in the fields of the device structure with localtalk-generic values. */

	netdev->change_mtu         = lapd_change_mtu;
	netdev->hard_header        = NULL;
	netdev->rebuild_header     = NULL;
	netdev->set_mac_address    = lapd_mac_addr;
	netdev->hard_header_cache  = NULL;
	netdev->header_cache_update= NULL;

	netdev->type               = ARPHRD_ISDN_DCHAN;
	netdev->hard_header_len    = 0;
	netdev->mtu                = 512;
	netdev->addr_len           = 0;
	netdev->tx_queue_len       = 10;

	memset(netdev->broadcast, 0x00, sizeof(netdev->broadcast));

	netdev->flags              = IFF_NOARP;
}
EXPORT_SYMBOL(setup_lapd);

static void lapd_sock_destruct(struct sock *sk)
{

printk(KERN_INFO "lapd: sock_destruct\n");

	if (!sock_flag(sk, SOCK_DEAD)) {
		printk("Attempt to release alive inet socket %p\n", sk);
		return;
	}

	BUG_TRAP(sk_unhashed(sk));

	struct lapd_opt *lo = lapd_sk(sk);

	if (!hlist_empty(&lo->new_dlcs)) {
		struct hlist_node *node, *t;

		hlist_for_each_safe(node, t, &lo->new_dlcs) {
			hlist_del(node);
			kfree(node);
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

	__skb_queue_purge(&sk->sk_receive_queue);
	__skb_queue_purge(&sk->sk_error_queue);

	BUG_TRAP(skb_queue_empty(&sk->sk_write_queue));
	BUG_TRAP(!sk->sk_wmem_queued);
	BUG_TRAP(!sk->sk_forward_alloc);
	BUG_TRAP(!atomic_read(&sk->sk_rmem_alloc));
	BUG_TRAP(!atomic_read(&sk->sk_wmem_alloc));

/*	{
	struct lapd_te *te, *n;
	write_lock_bh(&lo->nt.tes_lock);

	list_for_each_entry_safe(te, n, &lo->nt.tes, hash_list) {

		printk(KERN_ERR "List del %p\n", &te->hash_list);

		list_del(&te->hash_list);
		kfree(te);
	}

	write_unlock_bh(&lo->nt.tes_lock);
	}
*/
}

void lapd_unhash(struct sock *sk)
{
	write_lock_bh(&lapd_hash_lock);
	sk_del_node_init(sk);
	write_unlock_bh(&lapd_hash_lock);
}

static int lapd_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct lapd_opt *lo = lapd_sk(sk);

	if (!sk) return 0;

printk(KERN_INFO "lapd: release\n");

	BUG_TRAP(!sock_owned_by_user(sk));

	sock_orphan(sk);

	if (lo->status == LAPD_DLS_LINK_CONNECTION_ESTABLISHED) {
		// Request disconnection, unhash will be delayed
		// until multiframe has been released

		lapd_start_multiframe_release(sk);
	} else lapd_unhash(sk);

	sock_put(sk);
	sock->sk = NULL;

	if (atomic_read(&sk->sk_refcnt) != 0) {
		printk(KERN_ERR "lapd: sk refcount=%d\n",
			atomic_read(&sk->sk_refcnt));
	}

	return 0;
}

static int lapd_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	int rc = -EINVAL;
	struct sock *sk = sock->sk;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
		/* Protocol layer */
		case TIOCOUTQ: {
			long amount = sk->sk_sndbuf -
				      atomic_read(&sk->sk_wmem_alloc);

			if (amount < 0)
				amount = 0;
			rc = put_user(amount, (int __user *)argp);
		break;
		}

		case TIOCINQ: {
			/*
			 * These two are safe on a single CPU system as only
			 * user tasks fiddle here
			 */
			struct sk_buff *skb = skb_peek(&sk->sk_receive_queue);
			long amount = 0;

			if (skb)
//	FIXME			amount = skb->len - sizeof(struct ddpehdr);
				amount = skb->len;
			rc = put_user(amount, (int __user *)argp);
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
//			rtnl_lock();
// FIXME			rc = isdnif_ioctl(cmd, argp);
//			rtnl_unlock();
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
	}

	return rc;
}

static int lapd_sendmsg(struct kiocb *iocb, struct socket *sock,
	struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct lapd_opt *lo = lapd_sk(sk);
	int err;

	if (!lo->dev) {
		err = -ENODEV;
		goto err_no_dev;
	}

	if (len > lo->dev->mtu) {
		err = -EMSGSIZE;
		goto err_over_mtu;
	}

	lapd_tei_t tei;

	if (!lo->nt_mode && lo->usr_tme->status != TEI_ASSIGNED) {
		if (msg->msg_flags & O_NONBLOCK) {
			err = -EWOULDBLOCK;
			goto err_tei_unassigned;
		}

		// Ok, this is quite racy. We could wait for TEI assignment
		// return from wait_for_tei_assignment and have TEI removed
		// in the meantime, that's the reason for the while
		do {
			err = lapd_utme_wait_for_tei_assignment(
				lo->usr_tme);
			if (err < 0)
				goto err_wait_for_tei_assignment;

			spin_lock_bh(&lo->usr_tme->lock);
			if (lo->usr_tme->status == TEI_ASSIGNED)
				tei = lo->usr_tme->tei;
			else
				tei = LAPD_TEI_UNASSIGNED;
			spin_unlock_bh(&lo->usr_tme->lock);

		} while (tei == LAPD_TEI_UNASSIGNED);
	}

	struct sk_buff *skb;
	if (msg->msg_flags & MSG_OOB) {
		skb = sock_alloc_send_skb(sk,
        		sizeof(struct lapd_hdr_e) + len,
			(msg->msg_flags & MSG_DONTWAIT), &err);
		if (!skb)
			goto err_sock_alloc_send_skb;

		err = lapd_prepare_uframe(sk, skb, UI, 0);
		if(err < 0) goto err_prepare_frame;

		err = memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len);
		if (err) goto err_memcpy_fromiovec;

		lapd_send_completed_uframe(skb);
	} else {
		skb = sock_alloc_send_skb(sk,
        		sizeof(struct lapd_hdr) + len,
			(msg->msg_flags & MSG_DONTWAIT), &err);
		if (!skb)
			goto err_sock_alloc_send_skb;

		if (lo->status != LAPD_DLS_LINK_CONNECTION_ESTABLISHED) {
			err = -ENOTCONN;
			goto err_notconn;
		}

		err = lapd_prepare_iframe(sk, skb);
		if(err < 0) goto err_prepare_frame;

		err = memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len);
		if (err) goto err_memcpy_fromiovec;

		lapd_send_completed_iframe(skb);
	}

	return len;

err_memcpy_fromiovec:
	kfree_skb(skb);
err_sock_alloc_send_skb:
err_notconn:
err_prepare_frame:
err_wait_for_tei_assignment:
err_tei_unassigned:
err_over_mtu:
err_no_dev:

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

	if (!lo->dev) {
		err = -ENODEV;
		goto err_no_dev;
	}

	struct sk_buff *skb = skb_recv_datagram(sk, flags,
						flags & MSG_DONTWAIT, &err);
	if (!skb) goto err_recv_datagram;

	skb->h.raw = skb->data;
	copied     = skb->len;

	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}

	skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	skb_free_datagram(sk, skb);
	err = copied;

err_recv_datagram:
err_no_dev:
	release_sock(sk);

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

	if (dev->type != ARPHRD_ISDN_DCHAN) {
		err = -ENOPROTOOPT;
		goto err_invalid_type;
	}

	if (!(dev->flags & IFF_UP)) {
		err = -ENETDOWN;
		goto err_dev_not_up;
	}

	if (dev->flags & IFF_ALLMULTI) {
		lo->nt_mode = TRUE;

		struct sock *sk;
		struct hlist_node *node;
		// Do not allow binding more than one socket to the
		// same interface.

		read_lock_bh(&lapd_hash_lock);
		sk_for_each(sk, node, &lapd_hash) {
			struct lapd_opt *lo = lapd_sk(sk);
			if (lo->nt_mode && lo->dev == dev) {
				read_unlock_bh(&lapd_hash_lock);
				err = -ENODEV;
				goto err_socket_already_present;
			}
		}
		read_unlock_bh(&lapd_hash_lock);
	} else {
		lo->nt_mode = FALSE;

		if (dev->flags & IFF_POINTOPOINT)
			lapd_utme_set_static_tei(lo->usr_tme, 0);
	}

	if (sk->sk_type != SOCK_DGRAM) {
		err = -EINVAL;
		goto err_invalid_socket_type;
	}

	if (!lo->nt_mode) {
		lo->usr_tme = lapd_utme_alloc(dev);

		lapd_utme_hold(lo->usr_tme);
		hlist_add_head(&lo->usr_tme->node, &lapd_utme_hash);
	}

	// No need to dev_hold() since we already held dev by calling
	// dev_get_by_name()
	sk->sk_bound_dev_if = dev->ifindex;
	lo->dev = dev;

	// TODO: We may need to postpone tei request when L2 transfers
	// are needed, cfr. ETSI 300 125
	if (!lo->nt_mode) {
		lo->usr_tme->dev = dev;

		if (lo->usr_tme->status != TEI_ASSIGNED)
			lapd_utme_start_tei_request(lo->usr_tme);
	}

	lo->sap = &lapd_dev(lo->dev)->q931;

	return 0;

err_socket_already_present:
err_invalid_socket_type:
err_dev_not_up:
err_invalid_type:
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
	if (sk->sk_err || !skb_queue_empty(&sk->sk_error_queue))
		mask |= POLLERR;

	/* readable? */
	if (!skb_queue_empty(&sk->sk_receive_queue) ||
	    (sk->sk_shutdown & RCV_SHUTDOWN))
		mask |= POLLIN | POLLRDNORM;

	if (sk->sk_shutdown == SHUTDOWN_MASK)
		mask |= POLLHUP;

	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= POLLIN | POLLRDNORM;

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

		lo->net_tme->T201 = intoptval;
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

		lo->usr_tme->N202 = intoptval;
	break;

	case LAPD_Q931_T200:
		if (optlen != sizeof(int)) {
			err = -EINVAL;
			break;
		}

		if (intoptval <= 0 || intoptval > 30) {
			err = -EINVAL;
			break;
		}

		lapd_dev(lo->dev)->q931.T200 = intoptval;
	break;

	case LAPD_Q931_N200:
		if (optlen != sizeof(int)) {
			err = -EINVAL;
			break;
		}

		if (intoptval <= 0 || intoptval > 30) {
			err = -EINVAL;
			break;
		}

		lapd_dev(lo->dev)->q931.N200 = intoptval;
	break;

	case LAPD_Q931_T203:
		if (optlen != sizeof(int)) {
			err = -EINVAL;
			break;
		}

		if (intoptval <= 0 || intoptval > 30) {
			err = -EINVAL;
			break;
		}

		lapd_dev(lo->dev)->q931.T203 = intoptval;
	break;

	case LAPD_Q931_N201:
		if (optlen != sizeof(int)) {
			err = -EINVAL;
			break;
		}

		if (intoptval <= 0 || intoptval > 512) {
			err = -EINVAL;
			break;
		}

		lapd_dev(lo->dev)->q931.N201 = intoptval;
	break;

	case LAPD_Q931_K:
		if (optlen != sizeof(int)) {
			err = -EINVAL;
			break;
		}

		if (intoptval <= 0 || intoptval > 30) {
			err = -EINVAL;
			break;
		}

		lapd_dev(lo->dev)->q931.k = intoptval;
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

	case LAPD_Q931_T200:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lapd_dev(lo->dev)->q931.T200;
	break;

	case LAPD_Q931_N200:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lapd_dev(lo->dev)->q931.N200;
	break;

	case LAPD_Q931_T203:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lapd_dev(lo->dev)->q931.T203;
	break;

	case LAPD_Q931_N201:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lapd_dev(lo->dev)->q931.N201;
	break;

	case LAPD_Q931_K:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lapd_dev(lo->dev)->q931.k;
	break;

	case LAPD_DLC_STATUS:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

		val = lo->status;
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

static struct proto_ops SOCKOPS_WRAPPED(lapd_dgram_ops);

static int lapd_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	int err;

	// LAPD is a privileged socket
	if (!capable(CAP_NET_BIND_SERVICE)) {
		err = -EPERM;
		goto err_no_cap;
	}

	if (sock->type != SOCK_DGRAM) {
		err = -ESOCKTNOSUPPORT;
		goto err_no_type;
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
	sk->sk_zapped = 0;
	sk->sk_protocol = protocol;

	struct lapd_opt *lo = lapd_sk(sk);

	// TE mode section

	lo->nt_mode = FALSE;

	lo->usr_tme = NULL;

	init_timer(&lo->T200_timer);
	lo->T200_timer.function = lapd_T200_timer;
	lo->T200_timer.data = (unsigned long)sk;

	init_timer(&lo->T203_timer);
	lo->T203_timer.function = lapd_T203_timer;
	lo->T203_timer.data = (unsigned long)sk;

	lo->status = LAPD_DLS_LINK_CONNECTION_RELEASED;

	lo->peer_busy = FALSE;
	lo->me_busy = FALSE;
	lo->peer_waiting_for_ack = FALSE;
	lo->rejection_exception = FALSE;
	lo->in_timer_recovery = FALSE;

	INIT_HLIST_HEAD(&lo->new_dlcs);

	write_lock_bh(&lapd_hash_lock);
	sk_add_node(sk, &lapd_hash);
	write_unlock_bh(&lapd_hash_lock);

	return 0;

//	sk_free(sk);
err_sk_alloc:
err_no_type:
err_no_cap:

	return err;
}

struct sock *lapd_new_sock(struct sock *parent_sk, lapd_tei_t tei, int sapi)
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
	sk->sk_zapped = parent_sk->sk_zapped;
	sk->sk_type = parent_sk->sk_type;
	sk->sk_priority = parent_sk->sk_priority;
	sk->sk_protocol = parent_sk->sk_protocol;
	sk->sk_rcvbuf = parent_sk->sk_rcvbuf;
	sk->sk_sndbuf = parent_sk->sk_sndbuf;
	sk->sk_debug = parent_sk->sk_debug;
	sk->sk_state = TCP_ESTABLISHED;
	sk->sk_sleep = parent_sk->sk_sleep;

	struct lapd_opt *lo = lapd_sk(sk);
	struct lapd_opt *parent_lo = lapd_sk(parent_sk);

	BUG_ON(!parent_lo->dev);
	lo->dev = parent_lo->dev;
	dev_hold(lo->dev);

	lo->nt_mode = parent_lo->nt_mode;

	lo->usr_tme = NULL;
	lo->peer_busy = FALSE;
	lo->me_busy = FALSE;
	lo->peer_waiting_for_ack = FALSE;
	lo->rejection_exception = FALSE;
	lo->in_timer_recovery = FALSE;

	lo->status = LAPD_DLS_LINK_CONNECTION_RELEASED;

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

static int lapd_connect(struct socket *sock, struct sockaddr *uaddr,
	int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	int err = 0;

	lock_sock(sk);

	struct lapd_opt *lo = lapd_sk(sk);

	if (!lo->nt_mode && lo->usr_tme->status != TEI_ASSIGNED) {
		if (flags & O_NONBLOCK) {
			err = -EWOULDBLOCK;
			goto err_tei_unassigned;
		}

		lapd_utme_wait_for_tei_assignment(lo->usr_tme);
	}

	if (lo->status == LAPD_DLS_LINK_CONNECTION_ESTABLISHED &&
	    sock->state == SS_CONNECTING) {
		 /* Connect completed during a ERESTARTSYS event */
		sock->state = SS_CONNECTED;
		goto established;
	}

	if (lo->status == LAPD_DLS_LINK_CONNECTION_RELEASED &&
		 sock->state == SS_CONNECTING) {
		sock->state = SS_UNCONNECTED;
		err = -ECONNREFUSED;
		goto err_refused;
	}

	if (lo->status == LAPD_DLS_LINK_CONNECTION_ESTABLISHED) {
		err = -EISCONN;
		goto err_already_connected;
	}

	if (lo->status == LAPD_DLS_AWAITING_ESTABLISH && (flags & O_NONBLOCK)) {
		err = -EINPROGRESS;
		goto err_inprogress;
	}

        sock->state = SS_CONNECTING;
	lapd_start_multiframe_establishment(sk);

	err = lapd_multiframe_wait_for_establishment(sk);
	if (err) {
	        sock->state = SS_UNCONNECTED;
		goto err_multiframe_wait_for_establishment;
	}

	sock->state = SS_CONNECTED;

err_multiframe_wait_for_establishment:
err_inprogress:
err_already_connected:
err_refused:
established:
err_tei_unassigned:

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

	BUG_ON(!sk);

	if (sk->sk_state != TCP_LISTEN) {
		err = -EINVAL;
		goto err_notlistening;
	}

	lock_sock(sk);

	struct lapd_opt *lo = lapd_sk(sk);
	if (hlist_empty(&lo->new_dlcs)) {
		err = lapd_wait_for_new_dlc(sk);
		if (err < 0) goto err_wait_for_new_dlc;
	}

	struct lapd_new_dlc *new_dlc;

	new_dlc = hlist_entry(lo->new_dlcs.first, struct lapd_new_dlc, node);

	sock_graft(new_dlc->sk, newsock);

	hlist_del(&new_dlc->node);
	kfree(new_dlc);

err_wait_for_new_dlc:
	release_sock(sk);
err_notlistening:

	return err;
}

static int lapd_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	struct lapd_opt *lo = lapd_sk(sk);

	if (sk->sk_state == TCP_LISTEN)
		return -EOPNOTSUPP;

	sk->sk_max_ack_backlog = backlog;
	sk->sk_state = TCP_LISTEN;

	lo->status = LAPD_DLS_LISTENING;

	return 0;
}

static int lapd_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	struct lapd_opt *lo = lapd_sk(sk);
	int err = 0;

	lock_sock(sk);

	if (lo->nt_mode)
		return -EINVAL;

	lapd_start_multiframe_release(sk);

	err = lapd_multiframe_wait_for_release(sk);
	if (err) goto err_multiframe_wait_for_release;

err_multiframe_wait_for_release:

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

	lapd_sk_cachep = kmem_cache_create("lapd_sock",
				sizeof(struct lapd_sock), 0,
				SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!lapd_sk_cachep) {
		printk(KERN_CRIT
			"lapd: Can't create protocol sock SLAB caches!\n");

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
