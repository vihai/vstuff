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
#include <net/datalink.h>
#include <net/sock.h>

#include "lapd_user.h"
#include "lapd.h"
#include "tei_mgmt_nt.h"
#include "tei_mgmt_te.h"

struct hlist_head lapd_hash = HLIST_HEAD_INIT;
rwlock_t lapd_hash_lock = RW_LOCK_UNLOCKED;

static struct proc_dir_entry *lapd_proc_entry;

static kmem_cache_t *lapd_sk_cachep;

static int lapd_proc_read(char *page, char **start,
		off_t off, int count, 
		int *eof, void *data)
{
	int len = 0;
	struct sock *sk;
	struct hlist_node *node;

	read_lock_bh(&lapd_hash_lock);
	sk_for_each(sk, node, &lapd_hash) {
		struct lapd_opt *lo = lapd_sk(sk);

		len += snprintf(page + len, PAGE_SIZE - len,
			"interface: %s\n",
			lo->dev->name);

		if (lo->nt_mode) {
			len += snprintf(page + len, PAGE_SIZE - len,
				"role      : NT\n"
				"TEs\n");

/*			read_lock_bh(&lo->nt.tes);
			struct lapd_te *te;
			list_for_each_entry(te, &lo->nt.tes, hash_list) {
				len += snprintf(page + len, PAGE_SIZE - len,
					"TEI : %u\n",
					te->tei);
			}

			read_unlock_bh(&lo->nt.tes);*/

		} else {
/*			len += snprintf(page + len, PAGE_SIZE - len,
				"role      : TE\n"
				"TEI       : %u\n"
				"TEI status: %d\n",
				lo->te.tei,
				lo->te.status);*/
		}
	}
	read_unlock_bh(&lapd_hash_lock);

	return len;
}

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

static int lapd_device_event(struct notifier_block *this, unsigned long event,
			    void *ptr)
{
//	if (event == NETDEV_DOWN)
		/* Discard any use of this */
//		lapd_dev_down(ptr);

	printk(KERN_INFO "lapd: notification: %lu\n",event);

	return NOTIFY_DONE;
}

static void lapd_sock_destruct(struct sock *sk)
{

printk(KERN_INFO "lapd: sock_destruct\n");

	if (!sock_flag(sk, SOCK_DEAD)) {
		printk("Attempt to release alive inet socket %p\n", sk);
		return;
	}

	BUG_TRAP(sk_unhashed(sk));

	struct lapd_opt *lo = lapd_sk(sk);

	if (lo->dev) {
		dev_put(lo->dev);
	}

	sk_stream_kill_queues(sk);

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

	BUG_TRAP(!atomic_read(&sk->sk_rmem_alloc));
	BUG_TRAP(!atomic_read(&sk->sk_wmem_alloc));
	BUG_TRAP(!sk->sk_wmem_queued);
	BUG_TRAP(!sk->sk_forward_alloc);
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
	sock->sk = NULL;

//	sk_stop_timer(sk, &lo->tei_mgmt.T201_timer);
//	sk_stop_timer(sk, &lo->tei_mgmt.T202_timer);

	if (lo->status == LINK_CONNECTION_ESTABLISHED) {
		// Request disconnection, unhash will be delayed
		// until multiframe has been released

		lapd_start_multiframe_release(lo);
	} else lapd_unhash(sk);

	sock_put(sk);

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

static int lapd_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg,
			size_t len)
{
	struct sock *sk = sock->sk;
	struct lapd_opt *lo = lapd_sk(sk);
	int err;

	if (!lo->dev) {
		err = -ENETUNREACH;
		goto err_no_dev;
	}

	if (len > lo->dev->mtu) {
		err = -EMSGSIZE;
		goto err_over_mtu;
	}

	// if this is an u-frame
	// if staus != TEI_ASSIGNED ?

	u8 tei;
	if (lo->nt_mode) {
		if (!msg->msg_name ||
		     msg->msg_namelen != sizeof(struct sockaddr_lapd)) {
			err = -EINVAL;
			goto err_no_msg_name;
		}

		tei = ((struct sockaddr_lapd *)msg->msg_name)->sal_tei;
	}
	else {
		BUG_TRAP(lo->usr_tme);

		if (lo->usr_tme->status == TEI_UNASSIGNED) {
//			err = lapd_wait_for_tei_assignment(sk);
			if (err < 0) goto err_wait_for_tei_assignment;
		}

		tei = lo->usr_tme->tei;
	}

	struct sk_buff *skb;
	if (msg->msg_flags & MSG_OOB) {
		skb = sock_alloc_send_skb(sk,
        		sizeof(struct lapd_hdr_e) + len,
			(msg->msg_flags & MSG_DONTWAIT), &err);
		if (!skb)
			goto err_sock_alloc_send_skb;

		err = lapd_prepare_uframe(skb, 0, tei, UI);
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

		if (lo->status != LINK_CONNECTION_ESTABLISHED) {
			err = -ENOTCONN;
			goto err_notconn;
		}

		err = lapd_prepare_iframe(skb, 0, tei);
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
err_no_msg_name:
err_wait_for_tei_assignment:
err_over_mtu:
err_no_dev:

	return err;
}


static int lapd_recvmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg,
			size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct lapd_opt *lo = lapd_sk(sk);
	int err;
	int copied;

	lock_sock(sk);

	if (!lo->dev) {
		err = -ENETUNREACH;
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
	}
	else {
		lo->nt_mode = FALSE;

		if (dev->flags & IFF_POINTOPOINT)
			lapd_usr_tme_set_static_tei(lo->usr_tme, 0);
	}

	if (sk->sk_type != SOCK_DGRAM) {
		err = -EINVAL;
		goto err_invalid_socket_type;
	}

	sk->sk_bound_dev_if = dev->ifindex;
	lo->dev = dev;

	// TODO: We mey need to postpone tei request when L2 transfers
	// are needed, cfr. ETSI 300 125
	if (!lo->nt_mode) {
		if (lo->usr_tme->status != TEI_ASSIGNED)
			lapd_start_tei_request(sk);
		else
			lo->usr_tme->status = TEI_ASSIGNED;
	}

	return 0;

err_invalid_socket_type:
err_dev_not_up:
err_invalid_type:
	dev_put(dev);
err_nodev:

	return err;
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

	case LAPD_TE_TEI:
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

		lapd_usr_tme_set_static_tei(lo->usr_tme, intoptval);
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

		// FIXME
//		lo->usr_tme->T201 = intoptval;
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

//		lo->usr_tme->N202 = intoptval;
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

//		lo->usr_tme->N202 = intoptval;
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

// FIXME
//		lo->q931.T200 = intoptval;
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
//FIXME
//		lo->q931.N200 = intoptval;
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
//FIXME
//		lo->q931.T203 = intoptval;
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
// FIXME
//		lo->q931.N201 = intoptval;
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

// FIXME
//		lo->q931.k = intoptval;
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

	case LAPD_TE_TEI:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

// FIXME
//		val = lo->te.tei;
	break;

	case LAPD_TE_STATUS:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

// FIXME
//		val = lo->te.status?LAPD_ROLE_NT:LAPD_ROLE_TE;
	break;

	case LAPD_TEI_MGMT_T201:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

// FIXME
//		val = lo->tei_mgmt.T201;
	break;

	case LAPD_TEI_MGMT_N202:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

// FIXME
//		val = lo->tei_mgmt.N202;
	break;

	case LAPD_TEI_MGMT_T202:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

// FIXME
//		val = lo->tei_mgmt.T202;
	break;

	case LAPD_Q931_T200:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

// FIXME
//		val = lo->q931.T200;
	break;

	case LAPD_Q931_N200:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

// FIXME
//		val = lo->q931.N200;
	break;

	case LAPD_Q931_T203:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

// FIXME
//		val = lo->q931.T203;
	break;

	case LAPD_Q931_N201:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

// FIXME
//		val = lo->q931.N201;
	break;

	case LAPD_Q931_K:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto err_invalid_optlen;
		}

// FIXME
//		val = lo->q931.k;
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
	sk->sk_family = PF_LAPD;
	sk->sk_protocol = protocol;

	struct lapd_opt *lo = lapd_sk(sk);

	// TE mode section

	lo->nt_mode = FALSE;

	// FIXME: This should be moved to SO_BINDTODEVICE
	lo->usr_tme = lapd_usr_tei_mgmt_entity_alloc();

	init_timer(&lo->T200_timer);
	lo->T200_timer.function = lapd_T200_timer;
	lo->T200_timer.data = (unsigned long)sk;

	init_timer(&lo->T203_timer);
	lo->T203_timer.function = lapd_T203_timer;
	lo->T203_timer.data = (unsigned long)sk;

	lo->status = LINK_CONNECTION_RELEASED;

	lo->peer_busy = FALSE;
	lo->me_busy = FALSE;
	lo->peer_waiting_for_ack = FALSE;
	lo->rejection_exception = FALSE;
	lo->in_timer_recovery = FALSE;

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

static int lapd_rcv(struct sk_buff *skb, struct net_device *dev,
		     struct packet_type *pt)
{
/*	// Don't mangle buffer if shared
	if (!(skb = skb_share_check(skb, GFP_ATOMIC)))
		goto err_share_check;*/

	// Minimum frame is header + 2 CRC <- not sent yet by driver
	if (skb->len < sizeof(struct lapd_hdr)) // + 2)
		goto err_small_frame;

	// Size check and make sure header is contiguous
	if (!pskb_may_pull(skb, sizeof(struct lapd_hdr)))
		goto err_pskb_may_pull;

	BUG_TRAP(skb->dev);

	struct lapd_hdr *hdr = (struct lapd_hdr *)skb->h.raw;
	if (hdr->addr.ea1 || !hdr->addr.ea2) {
		printk(KERN_WARNING
			"lapd: int %s: "
			"improper ea bits in received frame\n",
			skb->dev->name);
		goto err_improper_ea;
	}

	if (hdr->addr.sapi == LAPD_SAPI_TEI_MGMT) {
		lapd_handle_tei_mgmt(skb);
		goto frame_handled;
	}

	// in NT mode -> search the socket by dev
	// in TE mode -> search the socket by (dev, TE)
	struct sock *sk;
	struct hlist_node *node;

	read_lock_bh(&lapd_hash_lock);
	sk_for_each(sk, node, &lapd_hash) {
		struct lapd_opt *lo = lapd_sk(sk);
		if (lo->dev == dev &&
		    (hdr->addr.tei == LAPD_BROADCAST_TEI ||
		      (!lo->nt_mode && lo->usr_tme->tei == hdr->addr.tei) ||
		       lo->nt_mode)) {

			skb->sk = sk;

			lapd_handle_socket_frame(sk, skb);
		}
	}

	read_unlock_bh(&lapd_hash_lock);

	return 0;

//err_skb_clone:
frame_handled:
err_small_frame:
err_improper_ea:
err_pskb_may_pull:
//err_sock_queue_rcv_skb:

	kfree_skb(skb);

//err_share_check:

	return 0;
}

static int lapd_multiframe_wait_for_establishment(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);
	DEFINE_WAIT(wait);
        int err = 0;
	int timeout = 10 * HZ;

	for (;;) {
		prepare_to_wait_exclusive(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);

		release_sock(sk);

		// Timeout is used only to detect abnormal cases
		timeout = schedule_timeout(timeout);

		lock_sock(sk);

		if (lo->status == LINK_CONNECTION_ESTABLISHED)
			break;

		if (signal_pending(current)) {
			err = sock_intr_errno(timeout);
			break;
		}

		if (!timeout) {
			printk(KERN_ERR "lapd: Connect timed out?!?\n");

			err = -EAGAIN;
			break;
		}

		if (sk->sk_err < 0) {
			err = sk->sk_err;
			break;
		}
	}

	finish_wait(sk->sk_sleep, &wait);

	return err;
}


static int lapd_multiframe_wait_for_release(struct sock *sk)
{
	struct lapd_opt *lo = lapd_sk(sk);
	DEFINE_WAIT(wait);
        int err = 0;
	int timeout = 10 * HZ;

	for (;;) {
		prepare_to_wait_exclusive(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);

		release_sock(sk);

		// Timeout is used only to detect abnormal cases
		timeout = schedule_timeout(timeout);

		lock_sock(sk);

		if (lo->status == LINK_CONNECTION_RELEASED)
			break;

		if (signal_pending(current)) {
			err = sock_intr_errno(timeout);
			break;
		}

		if (!timeout) {
			printk(KERN_ERR "lapd: Connect timed out?!?\n");

			err = -EAGAIN;
			break;
		}

		err = sock_error(sk);
		if (err)
			break;
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

	// copied from x25 :=)

	struct lapd_opt *lo = lapd_sk(sk);
	if (lo->status == LINK_CONNECTION_ESTABLISHED &&
	    sock->state == SS_CONNECTING) {
		 /* Connect completed during a ERESTARTSYS event */
		sock->state = SS_CONNECTED;
		goto established;
	}

	if (lo->status == LINK_CONNECTION_RELEASED &&
		 sock->state == SS_CONNECTING) {
		sock->state = SS_UNCONNECTED;
		err = -ECONNREFUSED;
		goto err_refused;
	}

	if (lo->status == LINK_CONNECTION_ESTABLISHED) {
		err = -EISCONN;
		goto err_already_connected;
	}

/*	if (lo->usr_tme->status == TEI_UNASSIGNED) {
		err = lapd_wait_for_tei_assignment(sk);
		if (err < 0) goto err_wait_for_tei_assignment;
	}
FIXME*/

        sock->state = SS_CONNECTING;
	lapd_start_multiframe_establishment(sk);

	if (sk->sk_state != TCP_ESTABLISHED && (flags & O_NONBLOCK)) {
		err = -EINPROGRESS;
		goto err_inprogress;
	}

	err = lapd_multiframe_wait_for_establishment(sk);
	if (err) {
	        sock->state = SS_UNCONNECTED;
		goto err_multiframe_wait_for_establishment;
	}

	sock->state = SS_CONNECTED;

err_multiframe_wait_for_establishment:
err_inprogress:
err_wait_for_tei_assignment:
err_already_connected:
err_refused:
established:

	release_sock(sk);

	return err;
}

static int lapd_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;

	if (sk->sk_state == TCP_LISTEN)
		return -EOPNOTSUPP;

	sk->sk_max_ack_backlog = backlog;
	sk->sk_state = TCP_LISTEN;

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

	lapd_start_multiframe_release(lo);

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
	.accept		= sock_no_accept,
	.getname	= sock_no_getname,
	.poll		= datagram_poll,
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

	lapd_proc_entry = create_proc_read_entry(
				"lapd", 0444, proc_net,
				lapd_proc_read, NULL);
	if (!lapd_proc_entry) {
		printk(KERN_CRIT "lapd: Cannot create proc entry\n");
		err = -ENOMEM;
		goto err_create_proc_read_entry;
	}

	lapd_proc_entry->owner = THIS_MODULE;

	return 0;

err_create_proc_read_entry:
	kmem_cache_destroy(lapd_sk_cachep);
err_kmem_cache_create:

	return err;
}
module_init(lapd_init);

static void __exit lapd_exit(void)
{
	remove_proc_entry("lapd", proc_net);

	// Free device structures

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
