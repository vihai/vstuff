/*
 * lapd.h
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

#include <linux/types.h>
#include <linux/socket.h>

#ifndef ARPHRD_LAPD
#define ARPHRD_LAPD 1000
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

enum
{
	LAPD_ROLE		= 0,
	LAPD_TEI		= 1,
	LAPD_SAPI		= 2,
	LAPD_TEI_MGMT_STATUS	= 3,
	LAPD_TEI_MGMT_T201	= 4,
	LAPD_TEI_MGMT_N202	= 5,
	LAPD_TEI_MGMT_T202	= 6,
	LAPD_DLC_STATUS		= 7,
	LAPD_Q931_T200		= 8,
	LAPD_Q931_N200		= 9,
	LAPD_Q931_T203		= 10,
	LAPD_Q931_N201		= 11,
	LAPD_Q931_K		= 12,
};

enum lapd_role {
	LAPD_ROLE_TE		= 0,
	LAPD_ROLE_NT		= 1
};

struct sockaddr_lapd {
	sa_family_t	sal_family;
	__u8            sal_bcast;
	char		sal_zero[8];
};

#ifdef __KERNEL__

enum {
	LAPD_PROTO_UFRAME = 0,
	LAPD_PROTO_IFRAME = 1,
};

enum lapd_datalink_status
{
	LAPD_DLS_LISTENING,
	LAPD_DLS_LINK_CONNECTION_RELEASED,
	LAPD_DLS_LINK_CONNECTION_ESTABLISHED,
	LAPD_DLS_AWAITING_ESTABLISH,
	LAPD_DLS_AWAITING_RELEASE,
};

#endif /* __KERNEL__ */

#endif
