/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2004-2008 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _CHAN_VGSM_H
#define _CHAN_VGSM_H

#define VGSM_DESCRIPTION "VoiSmart vGSM-I and vGSM-II channel driver"
#define VGSM_CHAN_TYPE "VGSM"
#define VGSM_CONFIG_FILE "vgsm.conf"
#define VGSM_OP_CONFIG_FILE "vgsm_operators.conf"
#define VGSM_OP_COUNTRY_CONFIG_FILE "vgsm_countries.conf"

#define VGSM_MINIMUM_FIRMWARE (0x020800)

#include <asterisk/channel.h>
#include <asterisk/version.h>

#include <list.h>

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
#include "rwlock_compat.h"
#endif

#include "me.h"
#include "comm.h"

#ifndef AST_CONTROL_DISCONNECT
#define AST_CONTROL_DISCONNECT 43
#endif

struct vgsm_chan {
	int refcnt; /* workaround for missing asterisk refcounting */
	ast_cond_t refcnt_decremented_cond;

	struct ast_channel *ast_chan;

	BOOL outbound;

	struct vgsm_me *me;
	struct vgsm_me_config *mc;

	struct vgsm_huntgroup *huntgroup;
	struct vgsm_me *hg_first_me;

	int up_fd;

	struct ks_node *node_userport;
	struct ks_node *node_me;

	struct ks_pipeline *pipeline_rx;
	struct ks_pipeline *pipeline_tx;

	char called_number[21];

	char calling_number[21];

	struct ast_frame frame_out;
	__u8 frame_out_buf[AST_FRIENDLY_OFFSET + 512];

	longtime_t last_rx;
	longtime_t last_tx;

	__u16 pressure_average;

	struct ast_dsp *dsp;

	int prev_rawwriteformat;
	int prev_rawreadformat;
};

struct vgsm_state
{
	ast_mutex_t state_lock;
	struct vgsm_me_config *default_mc;

	ast_rwlock_t mes_list_lock;
	struct list_head mes_list;

	ast_rwlock_t huntgroups_list_lock;
	struct list_head huntgroups_list;
//	struct list_head sim_holders_list;

	ast_rwlock_t operators_lock;
	struct list_head op_countries_list;
	struct list_head op_list;

	ast_mutex_t usecnt_lock;
	int usecnt;

	BOOL debug_timer;

	char sms_spooler[PATH_MAX];
	char sms_spooler_pars[512];
};

struct vgsm_chan *vgsm_chan_get(struct vgsm_chan *vgsm_chan);
void _vgsm_chan_put(struct vgsm_chan *vgsm_chan);
#define vgsm_chan_put(vgsm_chan) \
	do { _vgsm_chan_put(vgsm_chan); (vgsm_chan) = NULL; } while(0)

struct vgsm_chan *vgsm_alloc_inbound_call(struct vgsm_me *me);

static inline struct vgsm_chan *to_vgsm_chan(struct ast_channel *ast_chan)
{
	return ast_chan->tech_pvt;
}

int vgsm_connect_channel(struct vgsm_chan *vgsm_chan);

#endif
