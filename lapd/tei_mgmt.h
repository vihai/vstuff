/*
 * tei_mgmt.h - TEI management entity implementation
 *
 * Author: Daniele Orlandi <daniele@orlandi.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */

#ifndef _LAPD_TEI_MGMT_H
#define _LAPD_TEI_MGMT_H

#include "lapd_proto.h"

#ifdef __KERNEL__

enum lapd_tei_status
{
	TEI_UNASSIGNED,
	TEI_ASSIGNED,
};

#define LAPD_SAPI_TEI_MGMT	0x3f

#define LAPD_TEI_ENTITY		0x0f

#define LAPD_TEI_MT_REQUEST	0x01
#define LAPD_TEI_MT_ASSIGNED	0x02
#define LAPD_TEI_MT_DENIED	0x03
#define LAPD_TEI_MT_CHK_REQ	0x04
#define LAPD_TEI_MT_CHK_RES	0x05
#define LAPD_TEI_MT_REMOVE	0x06
#define LAPD_TEI_MT_VERIFY	0x07

#define LAPD_MAX_TES		16
#define LAPD_MIN_STA_TEI	0
#define LAPD_MAX_STA_TEI	63
#define LAPD_NUM_STA_TEIS	(LAPD_MAX_STA_TEI - LAPD_MIN_STA_TEI + 1)
#define LAPD_MIN_DYN_TEI	64
#define LAPD_MAX_DYN_TEI	126
#define LAPD_NUM_DYN_TEIS	(LAPD_MAX_DYN_TEI - LAPD_MIN_DYN_TEI + 1)

struct lapd_tei_mgmt_body
{
	u8 entity;
	u16 ri;
	u8 message_type;

#if defined(__BIG_ENDIAN_BITFIELD)
	u8 ai:7;
	u8 ai_ext:1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 ai_ext:1;
	u8 ai:7;
#endif
} __attribute__ ((__packed__));

struct lapd_tei_mgmt_frame
{
	struct lapd_hdr hdr;
	struct lapd_tei_mgmt_body body;
} __attribute__ ((__packed__));

void lapd_start_tei_request(struct sock *sk);

void lapd_tei_mgmt_T201_timer(unsigned long data);
void lapd_tei_mgmt_T202_timer(unsigned long data);
int lapd_handle_tei_mgmt(struct sk_buff *skb);

#endif /* __KERNEL__ */
#endif
