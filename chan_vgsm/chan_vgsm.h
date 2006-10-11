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

#include <asterisk/channel.h>

#include <list.h>

#include "module.h" 
#include "comm.h" 

#ifndef AST_CONTROL_DISCONNECT
#define AST_CONTROL_DISCONNECT 43
#endif

struct vgsm_chan {
	int refcnt; /* workaround for missing asterisk refcounting */

	struct ast_channel *ast_chan;

	struct vgsm_module *module;
	struct vgsm_module_config *mc;

	int sp_fd;

	char sp_node_id[80];
	char module_node_id[80];

	int sp_module_pipeline_id;
	int module_sp_pipeline_id;

	char calling_number[21];

	struct ast_dsp *dsp;
};

struct vgsm_state
{
	ast_mutex_t lock;

	struct vgsm_module_config *default_mc;
	struct list_head ifs;

	struct list_head op_list;

	ast_mutex_t usecnt_lock;
	int usecnt;

	int router_control_fd;

	int debug_generic;
	int debug_serial;

	char sms_spooler[32];
	char sms_spooler_pars[32];
};

struct vgsm_chan *vgsm_chan_get(struct vgsm_chan *vgsm_chan);
void vgsm_chan_put(struct vgsm_chan *vgsm_chan);
struct vgsm_chan *vgsm_alloc_inbound_call(struct vgsm_module *module);

static inline struct vgsm_chan *to_vgsm_chan(struct ast_channel *ast_chan)
{
	return ast_chan->tech_pvt;
}

#endif
