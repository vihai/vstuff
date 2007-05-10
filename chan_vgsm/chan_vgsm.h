/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _CHAN_VGSM_H
#define _CHAN_VGSM_H

#define VGSM_DESCRIPTION "VoiSmart VGSM Channel For Asterisk"
#define VGSM_CHAN_TYPE "VGSM"
#define VGSM_CONFIG_FILE "vgsm.conf"
#define VGSM_OP_CONFIG_FILE "vgsm_operators.conf"
#define VGSM_OP_COUNTRY_CONFIG_FILE "vgsm_countries.conf"

#include <asterisk/channel.h>

#include <list.h>

#include "module.h"
#include "comm.h"

#ifndef AST_CONTROL_DISCONNECT
#define AST_CONTROL_DISCONNECT 43
#endif

struct vgsm_chan {
	int refcnt; /* workaround for missing asterisk refcounting */
	ast_cond_t refcnt_decremented_cond;

	struct ast_channel *ast_chan;

	BOOL outbound;

	struct vgsm_module *module;
	struct vgsm_module_config *mc;

	struct vgsm_huntgroup *huntgroup;
	struct vgsm_module *hg_first_module;

	int up_fd;

	struct ks_node *node_userport;
	struct ks_node *node_module;

	struct ks_pipeline *pipeline_rx;
	struct ks_pipeline *pipeline_tx;

	char called_number[21];

	char calling_number[21];

	struct ast_frame frame_out;
	__u8 frame_out_buf[512];

	struct ast_dsp *dsp;

	__u16 pressure_average;
};

struct vgsm_state
{
	ast_mutex_t lock;

	struct vgsm_module_config *default_mc;
	struct list_head ifs;

	struct list_head huntgroups_list;

	struct list_head op_countries_list;
	struct list_head op_list;

	ast_mutex_t usecnt_lock;
	int usecnt;

	BOOL debug_generic;
	BOOL debug_jitbuf;
	BOOL debug_timer;

	char sms_spooler[32];
	char sms_spooler_pars[32];
};

struct vgsm_chan *vgsm_chan_get(struct vgsm_chan *vgsm_chan);
void _vgsm_chan_put(struct vgsm_chan *vgsm_chan);
#define vgsm_chan_put(vgsm_chan) \
	do { _vgsm_chan_put(vgsm_chan); (vgsm_chan) = NULL; } while(0)

struct vgsm_chan *vgsm_alloc_inbound_call(struct vgsm_module *module);

static inline struct vgsm_chan *to_vgsm_chan(struct ast_channel *ast_chan)
{
	return ast_chan->tech_pvt;
}

#endif
