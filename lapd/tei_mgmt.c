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
#include "tei_mgmt.h"

static inline int lapd_tm_send(
	struct lapd_tei_mgmt_entity *tme,
	u8 message_type, u16 ri, u8 ai)
{
	int err;

	struct sk_buff *skb = alloc_skb(sizeof(struct lapd_hdr) +
					sizeof(struct lapd_tei_mgmt_body),
					0);

	err = lapd_prepare_uframe(
		skb,
		LAPD_SAPI_TEI_MGMT,
		LAPD_BROADCAST_TEI,
		UI);
	if (err < 0) {
		kfree_skb(skb);
		return err;
	}

	struct lapd_tei_mgmt_body *tm =
		 (struct lapd_tei_mgmt_body *)skb_put(skb,
			 sizeof(struct lapd_tei_mgmt_body));

	tm->entity = LAPD_TEI_ENTITY;
	tm->message_type = message_type;
	tm->ri = ri;
	tm->ai = ai;
	tm->ai_ext = 1;

	return lapd_send_frame(skb);
}
