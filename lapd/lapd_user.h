/*
 * lapd.c - net_device LAPD link layer support functionss
 *
 * Copyright (C) 2004 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 *
 */

#ifndef _LAPD_USER_H
#define _LAPD_USER_H

#include <linux/socket.h>

#ifndef ARPHRD_ISDN_DCHAN
#define ARPHRD_ISDN_DCHAN 1000
#endif

#ifndef ETH_P_LAPD
#define ETH_P_LAPD 0x0030	/* LAPD pseudo type */
#endif

#ifndef AF_LAPD
#define AF_LAPD 30
#endif

#ifndef PF_LAPD
#define PF_LAPD AF_LAPD
#endif

#ifndef ETH_P_LAPD
#define ETH_P_LAPD 0x0030
#endif

#ifndef SOL_LAPD
#define SOL_LAPD 300
#endif

#ifdef __KERNEL__

enum
{
	LAPD_ROLE		= 0,
	LAPD_TE_TEI		= 1,
	LAPD_TE_STATUS		= 2,
	LAPD_TEI_MGMT_T201	= 3,
	LAPD_TEI_MGMT_N202	= 4,
	LAPD_TEI_MGMT_T202	= 5,
	LAPD_DLC_STATUS		= 6,
	LAPD_Q931_T200		= 7,
	LAPD_Q931_N200		= 8,
	LAPD_Q931_T203		= 9,
	LAPD_Q931_N201		= 10,
	LAPD_Q931_K		= 11,
};

enum lapd_role {
	LAPD_ROLE_TE		= 0,
	LAPD_ROLE_NT		= 1
};

enum {
	LAPD_PROTO_UFRAME = 0,
	LAPD_PROTO_IFRAME = 1,
};

struct sockaddr_lapd {
	sa_family_t	sal_family;
	__u8		sal_sapi;

 // TEI is valid only in NT mode, in TE mode it is assigned by net and
 // ignored here
	__u8		sal_tei;
	char		sal_zero[8];
};

enum lapd_datalink_status
{
	LINK_CONNECTION_RELEASED,
	LINK_CONNECTION_ESTABLISHED,
	AWAITING_ESTABLISH,
	AWAITING_RELEASE,
};

#endif /* __KERNEL__ */

#endif
