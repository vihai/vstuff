/*
 * vISDN channel driver for Asterisk
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _CHAN_VISDN_H
#define _CHAN_VISDN_H

#include <asterisk/channel.h>
#include <asterisk/version.h>

#include <libq931/list.h>

#include "intf.h"

#ifndef AST_CONTROL_INBAND_INFO
#define AST_CONTROL_INBAND_INFO 42
#endif

#ifndef AST_CONTROL_DISCONNECT
#define AST_CONTROL_DISCONNECT 43
#endif

#define VISDN_DESCRIPTION "VISDN Channel For Asterisk"
#define VISDN_CHAN_TYPE "VISDN"
#define VISDN_CONFIG_FILE "visdn.conf"

enum poll_info_type
{
	POLL_INFO_TYPE_MGMT,
	POLL_INFO_TYPE_ACCEPT,
	POLL_INFO_TYPE_BC_DLC,
	POLL_INFO_TYPE_DLC,
	POLL_INFO_TYPE_NETLINK,
	POLL_INFO_TYPE_CCB_Q931,
	POLL_INFO_TYPE_Q931_CCB,
};

struct poll_info
{
	enum poll_info_type type;
	struct visdn_intf *intf;
	struct q931_dlc *dlc;
};

struct visdn_suspended_call
{
	struct list_head node;

	struct visdn_chan *visdn_chan;
	struct q931_channel *q931_chan;

	char call_identity[10];
	int call_identity_len;

	time_t old_when_to_hangup;
};

struct visdn_chan {

	int refcnt; /* workaround for missing asterisk refcounting */
	ast_cond_t refcnt_decremented_cond;

	struct ast_channel *ast_chan;
	struct q931_call *q931_call;
	struct visdn_suspended_call *suspended_call;

	int is_framed;
	int supports_inband_tones;

	struct q931_ie_bearer_capability *bc;
	int ast_frame_type;
	int ast_frame_subclass;

	int handle_stream;

	int up_fd;
//	int ec_fd;

	struct ks_node *node_userport;
	struct ks_node *node_bearer;

	struct ks_pipeline *pipeline_rx;
	struct ks_pipeline *pipeline_tx;

//	int up_bearer_pipeline_started;

	int sending_complete;

	int channel_has_been_connected;
	int inband_info;

	char number[32];
	int sent_digits;

	char options[16];

	char dtmf_queue[20];
	int dtmf_deferred;

	struct visdn_ic *ic;

	struct visdn_huntgroup *huntgroup;
	struct visdn_intf *hg_first_intf;

	struct ast_frame frame_out;
	__u8 frame_out_buf[AST_FRIENDLY_OFFSET + 512];

	struct ast_dsp *dsp;
};

struct visdn_state
{
	pthread_t q931_thread;

	ast_mutex_t lock;

	int usecnt;
	ast_mutex_t usecnt_lock;

	int have_to_exit;

	struct list_head ccb_q931_queue;
	ast_mutex_t ccb_q931_queue_lock;
	int ccb_q931_queue_pipe_read;
	int ccb_q931_queue_pipe_write;

	struct list_head q931_ccb_queue;
	ast_mutex_t q931_ccb_queue_lock;
	int q931_ccb_queue_pipe_read;
	int q931_ccb_queue_pipe_write;

	struct list_head ifs;
	struct list_head huntgroups_list;

	struct pollfd polls[100];
	struct poll_info poll_infos[100];
	int npolls;

	int netlink_socket;

	int router_control_fd;

	int debug;
	int debug_q931;
	int debug_q921;

	struct visdn_ic *default_ic;
};

extern struct visdn_state visdn;

void refresh_polls_list();

struct visdn_chan *visdn_chan_get(struct visdn_chan *visdn_chan);
#define visdn_chan_put(chan) \
	do { _visdn_chan_put(chan); (chan) = NULL; } while(0)
void _visdn_chan_put(struct visdn_chan *visdn_chan);

static inline struct visdn_chan *to_visdn_chan(struct ast_channel *ast_chan)
{
	return ast_chan->tech_pvt;
}

static inline struct ast_channel *callpvt_to_astchan(
	struct q931_call *call)
{
	return (struct ast_channel *)call->pvt;
}

#endif
