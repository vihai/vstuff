/*
 * vISDN channel driver for Asterisk
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>

#include <libq931/list.h>

struct visdn_suspended_call
{
	struct list_head node;

	struct ast_channel *ast_chan;
	struct q931_channel *q931_chan;

	char call_identity[10];
	int call_identity_len;

	time_t old_when_to_hangup;
};

struct visdn_chan {
	struct ast_channel *ast_chan;
	struct q931_call *q931_call;
	struct visdn_suspended_call *suspended_call;

	char visdn_chanid[30];
	int is_voice;
	int channel_fd;

	char calling_number[21];
	int sending_complete;

	int may_send_digits;
	char queued_digits[21];
};
