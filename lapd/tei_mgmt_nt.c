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

struct hlist_head lapd_ntme_hash = HLIST_HEAD_INIT;
rwlock_t lapd_ntme_hash_lock = RW_LOCK_UNLOCKED;

static inline int lapd_ntme_send_tei_assigned(
	struct lapd_ntme *tme, u16 ri, int tei)
{
	return lapd_tm_send(tme->dev, LAPD_TEI_MT_ASSIGNED, ri, tei);
}

static inline int lapd_ntme_send_tei_denied(
	struct lapd_ntme *tme, u16 ri, int tei)
{
	return lapd_tm_send(tme->dev, LAPD_TEI_MT_DENIED, ri, tei);
}

static inline int lapd_ntme_send_tei_check_request(
	struct lapd_ntme *tme, int tei)
{
	return lapd_tm_send(tme->dev, LAPD_TEI_MT_CHK_REQ, 0, tei);
}

static inline int lapd_ntme_send_tei_remove(
	struct lapd_ntme *tme, int tei)
{
	return lapd_tm_send(tme->dev, LAPD_TEI_MT_REMOVE, 0, tei);
}

static inline int lapd_ntme_send_tei_verify(
	struct lapd_ntme *tme, int tei)
{
	return lapd_tm_send(tme->dev, LAPD_TEI_MT_VERIFY, 0, tei);
}

static void lapd_ntme_start_tei_check(
	struct lapd_ntme *tme, int tei)
{
	BUG_TRAP(tme->T201 > 0);

	lapd_ntme_reset_timer(tme, &tme->T201_timer,
		jiffies + tme->T201);

	tme->tei_check_outstanding = TRUE;
	tme->tei_check_count = 0;
	tme->tei_check_responses[0] = 0;
	tme->tei_check_responses[1] = 0;
	tme->tei_check_tei = tei;

	lapd_ntme_send_tei_check_request(tme, tei);
}

void lapd_ntme_T201_timer(unsigned long data)
{
	struct lapd_ntme *tme =
		(struct lapd_ntme *)data;

	printk(KERN_DEBUG "lapd: tei_mgmt T201\n");

	BUG_TRAP(tme->tei_check_outstanding);

	if (tme->tei_check_count == 0) {
		// End of first T201 window
		if (tme->tei_check_responses[0] < 2) {
			// Multiple TEIs assigned

			tme->tei_check_outstanding = FALSE;
			lapd_ntme_send_tei_remove(tme, tme->tei_check_tei);
		} else {
			// We need to try a second time

			tme->tei_check_count++;

			lapd_ntme_send_tei_check_request(tme, tme->tei_check_tei);
		        lapd_ntme_reset_timer(tme, &tme->T201_timer,
				jiffies + tme->T201);
		}
	} else {
		tme->tei_check_outstanding = FALSE;

		// End of second T201 window
		if (tme->tei_check_responses[0] == 0 &&
		    tme->tei_check_responses[1] == 0) {
			// TEI is unused

			tme->teis[tme->tei_check_tei] = LAPD_TEI_UNASSIGNED;
		} else if (tme->tei_check_responses[1] == 1 ||
		           tme->tei_check_responses[1] == 1) {
			// TEI in use
		} else {
			// Multiple TEIs assigned

			lapd_ntme_send_tei_remove(tme, tme->tei_check_tei);
		}
	}

	lapd_ntme_put(tme);
}

static u8 lapd_ntme_find_available_tei(
	struct lapd_ntme *tme)
{
	int i;
	for (i = 0; i < LAPD_NUM_DYN_TEIS; i++) {

		tme->cur_dyn_tei++;
		if(tme->cur_dyn_tei > LAPD_MAX_DYN_TEI)
			tme->cur_dyn_tei = LAPD_MAX_DYN_TEI;

		if (tme->teis[tme->cur_dyn_tei - LAPD_MIN_DYN_TEI] ==
			LAPD_TEI_UNASSIGNED)
			return tme->cur_dyn_tei;
	}

	return LAPD_TEI_UNASSIGNED;
}

static void lapd_ntme_recv_tei_request(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->h.raw;

	printk(KERN_INFO
		"lapd: tei_mgmt: TEI request\n");

	if (tm->hdr.addr.c_r) {
		printk(KERN_WARNING
			"lapd: tei_mgmt: int %s: TEI request with C/R=1 ?\n",
			skb->dev->name);
	}

	struct lapd_ntme *tme;
	struct hlist_node *node;

	read_lock_bh(&lapd_ntme_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_ntme_hash, node) {
		if (tme->dev != skb->dev) continue;

		if (/*tm->body.ai >= LAPD_MIN_STA_TEI && // always true */
		    tm->body.ai <= LAPD_MAX_STA_TEI) {
			break;
		} else if (tm->body.ai >= LAPD_MIN_DYN_TEI &&
		           tm->body.ai <= LAPD_MAX_DYN_TEI) {
			lapd_ntme_send_tei_denied(tme, tm->body.ri, tm->body.ai);
			break;
		}

		lapd_tei_t tei = lapd_ntme_find_available_tei(tme);

		lapd_ntme_send_tei_assigned(tme, tm->body.ri, tei);

		goto found;
	}

//	lapd_start_tei_check(tme, tm->body.ai);

	found:

	read_unlock_bh(&lapd_hash_lock);
}

static void lapd_ntme_recv_tei_check_response(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->h.raw;

	printk(KERN_INFO
		"lapd: tei_mgmt: int %s: TEI check response\n",
		skb->dev->name);

	if (tm->hdr.addr.c_r) {
		printk(KERN_WARNING
			"lapd: tei_mgmt: int %s: TEI request with C/R=0 ?\n",
			skb->dev->name);
	}

	struct hlist_node *node;
	struct lapd_ntme *tme;
	read_lock_bh(&lapd_ntme_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_ntme_hash, node) {
		if (tme->dev != skb->dev) continue;

		if (!tme->tei_check_outstanding) {
			printk(KERN_WARNING
				"lapd: tei_mgmt: "
				"unexpected/late check response\n");
			break;
		}

		tme->tei_check_responses[tme->tei_check_count]++;
	}

	read_unlock_bh(&lapd_ntme_hash_lock);
}

static void lapd_ntme_recv_tei_verify(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->h.raw;

	printk(KERN_INFO
		"lapd: tei_mgmt: int %s: TEI verify received: tei=%d\n",
		skb->dev->name,
		tm->body.ai);

	if (tm->hdr.addr.c_r) {
		printk(KERN_WARNING
			"lapd: tei_mgmt: int %s: TEI verify with C/R=1 ?\n",
			skb->dev->name);
	}

	if (tm->body.ai == LAPD_BROADCAST_TEI) {
		printk(KERN_INFO
			"lapd: tei_mgmt: received invalid verify"
			" request with tei=127\n");
		return;
	}

	struct hlist_node *node;
	struct lapd_ntme *tme;
	read_lock_bh(&lapd_ntme_hash_lock);
	hlist_for_each_entry(tme, node, &lapd_ntme_hash, node) {
		if (tme->dev == skb->dev) continue;

		printk(KERN_INFO
			"lapd: tei_mgmt: starting TEI check\n");

		lapd_ntme_start_tei_check(tme, tm->body.ai);
		break;
	}

	read_unlock_bh(&lapd_hash_lock);
}

int lapd_ntme_handle_frame(struct sk_buff *skb)
{
	struct lapd_tei_mgmt_frame *tm =
		(struct lapd_tei_mgmt_frame *)skb->h.raw;

	if (skb->len < sizeof(struct lapd_tei_mgmt_frame)) {
		printk(KERN_ERR
			"lapd: tei management: frame too small (%d bytes)\n",
			skb->len);

		return 0;
	}

	if (lapd_frame_type(tm->hdr.control) != UFRAME) {
		printk(KERN_ERR
			"lapd: tei management: not an U-Frame (%u%u)\n",
			tm->hdr.ft2,
			tm->hdr.ft1);

		return 0;
	}

	if (lapd_uframe_function(tm->hdr.control) != UI) {
		printk(KERN_ERR
			"lapd: tei management: not an Unnumbered Information"
			" (%u%u)\n",
			tm->hdr.u.m3,
			tm->hdr.u.m2);

		return 0;
	}

// TODO what to do with P bit ?
// FIXME answer with F bit! :)

	if (tm->body.entity != 0x0f) {
		printk(KERN_ERR
			"lapd: tei management: invalid entity %u\n",
			tm->body.entity);

		return 0;
	}

	switch (tm->body.message_type) {
	case LAPD_TEI_MT_CHK_RES:
		lapd_ntme_recv_tei_check_response(skb);
	break;

	case LAPD_TEI_MT_VERIFY:
		lapd_ntme_recv_tei_verify(skb);
	break;

	case LAPD_TEI_MT_REQUEST:
		lapd_ntme_recv_tei_request(skb);
	break;

	case LAPD_TEI_MT_REMOVE:
	case LAPD_TEI_MT_ASSIGNED:
	case LAPD_TEI_MT_DENIED:
	case LAPD_TEI_MT_CHK_REQ:
	default:
		printk(KERN_INFO
			"lapd: tei_mgmt: unknown/unimplemented message_type %u\n",
			tm->body.message_type);
	}

	return 0;
}

void lapd_ntme_destroy(struct lapd_ntme *tme)
{
	lapd_ntme_stop_timer(tme, &tme->T201_timer);
}

struct lapd_ntme *lapd_ntme_alloc()
{
	struct lapd_ntme *tme;
	tme = kmalloc(sizeof(struct lapd_ntme), GFP_ATOMIC);
	if (!tme) return NULL;

	memset(tme, 0x00, sizeof(struct lapd_ntme));

	tme->destroy = lapd_ntme_destroy;

	atomic_set(&tme->refcnt, 1);

	spin_lock_init(&tme->lock);

	tme->cur_dyn_tei = LAPD_MIN_DYN_TEI;
	tme->tei_check_outstanding = FALSE;

	int i;
	for (i=0; i<LAPD_NUM_DYN_TEIS; i++)
		tme->teis[i] = LAPD_TEI_UNASSIGNED;

	tme->T201 = 1 * HZ;

	init_timer(&tme->T201_timer);
	tme->T201_timer.function = lapd_ntme_T201_timer;
	tme->T201_timer.data = (unsigned long)tme;

	return tme;
}
