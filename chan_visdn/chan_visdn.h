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

#include "../config.h"

#include <asterisk/channel.h>

#ifdef HAVE_ASTERISK_VERSION_H
#include <asterisk/version.h>
#endif

#ifndef ASTERISK_VERSION_NUM
#include <asterisk/channel_pvt.h>
#endif

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

	int visdn_chan_id;
	int is_voice;
	int channel_fd;

	int sending_complete;

	int inband_info;

	char number[32];
	char options[16];

	char dtmf_queue[20];
	int dtmf_deferred;

	struct ast_dsp *dsp;

	struct visdn_interface *intf;

	struct visdn_huntgroup *huntgroup;
	struct visdn_interface *hg_first_intf;
};

#ifndef ASTERISK_VERSION_NUM

#define ast_config_load ast_load
#define ast_config_destroy ast_destroy

static inline struct visdn_chan *to_visdn_chan(struct ast_channel *ast_chan)
{
	return ast_chan->pvt->pvt;
}

#else

static inline struct visdn_chan *to_visdn_chan(struct ast_channel *ast_chan)
{
	return ast_chan->tech_pvt;
}

#endif
