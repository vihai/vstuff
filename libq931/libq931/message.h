/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LIBQ931_MESSAGE_H
#define _LIBQ931_MESSAGE_H

#include <libq931/list.h>
#include <libq931/call.h>
#include <libq931/dlc.h>
#include <libq931/ies.h>

struct q931_ie;
struct q931_message
{
	int refcnt;

	__u8 raw[512];
	int rawlen;

	struct q931_dlc *dlc;

	__u8 raw_message_type;
	enum q931_message_type message_type;

	q931_callref callref;
	int callref_len;
	enum q931_callref_flag callref_direction;

	__u8 *rawies;
	int rawies_len;

	struct q931_ies ies;

	struct list_head outgoing_queue_node;
};

#ifdef Q931_PRIVATE

#define report_msg(msg, lvl, format, arg...)				\
	do {								\
	       	if ((msg)->dlc)						\
			report_dlc((msg)->dlc, (lvl),			\
				format,					\
				## arg);				\
		else							\
			q931_report((lvl),				\
				format,					\
				## arg);				\
	} while(0)

#define report_msg_cont(msg, lvl, format, arg...)			\
	q931_report((lvl),						\
			format,						\
			## arg)

struct q931_message *q931_msg_get(struct q931_message *msg);
void q931_msg_put(struct q931_message *msg);
struct q931_message *q931_msg_alloc(struct q931_dlc *dlc);
struct q931_message *q931_msg_alloc_nodlc(void);

#endif

#endif
