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

/*
 * Locking guidelines:
 *
 * Locking in vGSM is bit complex, adding the fact that Asterisk's locking
 * is brain-dead, the risk of making a big mess is very high.
 *
 * The most used and dangerous locks are the Asterisk's channel lock and
 * the me's lock. The former is the lock we all know and we also know
 * that Asterisk acquires it on callback invocation.
 * The latter is a lock protecting access to the "vgsm_me" structure.
 * A vgsm_me describes everything belonging to a GSM me.
 *
 * There no one-to-one relationship between them as the me exists even
 * if no call is present, a ast_chan is actually a call... or a channel?
 * or half a call? who knows....
 *
 * Anyway, since our callbacks do get invoked with the ast_chan->lock acquired,
 * Asterisk is forcing us to follow its locking model.
 *
 * GUIDELINES:
 *
 * Locking order for nested locks:
 *
 * ast_chan => me->lock
 * ast_chan => vgsm.huntgroups_list_lock => me->lock
 * ast_chan => vgsm.mes_list_lock => me->lock
 *
 * Leaf locks:
 *
 * vgsm.usecnt_lock
 * vgsm.operators_list_lock
 * timers_lock
 *
 * vgsm_me callbacks are invoked without locks, it is their
 * responsibility to acquire the needed locks.
 *
 * Some field in me may be accessed without locking, either because it is
 * read-only after me initialization or being meaningful only after
 * the me changes into a particular state.
 *
 * Furthermore, the me contains me->comm which is another object
 * protected by its own lock, so, it has its own access policy.
 *
 * vgsm_me_config structures are instantiated on configuration and a
 * pointer to the current configuration is set into vgsm_me.
 * The values contained in a vgsm_me_config instance are read-only,
 * procedures which want a stable configuration may simply take a reference
 * to the vgsm_me_config object and keep it for as long as it is needed.
 * For example, a snapshot of the configuration is taken into the channel
 * structure and kept for as long as the call lasts.
 *
 */


#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <signal.h>
#include <ctype.h>
#include <iconv.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/termios.h>
#include <sys/signal.h>

#include <asm/types.h>

#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <asterisk/callerid.h>
#include <asterisk/indications.h>
#include <asterisk/cli.h>
#include <asterisk/musiconhold.h>
#include <asterisk/dsp.h>
#include <asterisk/causes.h>
#include <asterisk/manager.h>
#include <asterisk/version.h>

#include <linux/vgsm.h>

#include <linux/kstreamer/userport.h>
#include <linux/kstreamer/amu_compander.h>

/* FUCK YOU ASTERSISK */
#undef pthread_mutex_t
#undef pthread_mutex_lock
#undef pthread_mutex_unlock
#undef pthread_mutex_trylock
#undef pthread_mutex_init
#undef pthread_mutex_destroy
#undef pthread_cond_t
#undef pthread_cond_init
#undef pthread_cond_destroy
#undef pthread_cond_signal
#undef pthread_cond_broadcast
#undef pthread_cond_wait
#undef pthread_cond_timedwait

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
#include "rwlock_compat.h"
#else
#include <asterisk.h>
#include <asterisk/abstract_jb.h>
#endif

#include <res_kstreamer.h>

#include "util.h"
#include "chan_vgsm.h"
#include "me.h"
#include "huntgroup.h"
#include "comm.h"
#include "causes.h"
#include "sms_submit.h"
#include "operators.h"
#include "pin.h"
#include "number.h"
#include "base64.h"
#include "quotprint.h"
#include "debug.h"

#define FAILED_RETRY_TIME (5 * SEC)
#define READY_UPDATE_TIME (30 * SEC)
#define CLOSED_POSTPONE (3 * SEC)
#define POWERING_ON_TIMEOUT (8 * SEC)
#define POWERING_OFF_TIMEOUT (7 * SEC)
#define WAITING_INITIALIZATION_DELAY (2 * SEC)

#ifdef DEBUG_CODE
#define vgsm_debug_jitbuf(me, format, arg...)	\
	if ((me)->debug_jitbuf)			\
		vgsm_debug("%s: "		\
			format,			\
			(me)->name,		\
			## arg)
#else
#define vgsm_debug_jitbuf(me, format, arg...)	\
	do {} while(0);
#endif

struct vgsm_state vgsm = {
	.usecnt = 0,
#ifdef DEBUG_CODE
#ifdef DEBUG_DEFAULTS
	.debug_timer = FALSE,
#else
	.debug_timer = FALSE,
#endif
#endif
};

static const struct ast_channel_tech vgsm_tech;

static struct ast_channel *vgsm_ast_chan_alloc(
	struct vgsm_chan *vgsm_chan,
	int state,
	struct vgsm_me *me,
	int line,
	int format)
{
	struct ast_channel *ast_chan;

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	ast_chan = ast_channel_alloc(1);
	if (!ast_chan) {
		ast_log(LOG_WARNING, "Unable to allocate channel\n");
		goto err_channel_alloc;
	}

	ast_setstate(ast_chan, state);

	snprintf(ast_chan->name, sizeof(ast_chan->name),
		"VGSM/%s/%d",
		me ? me->name : "null",
		line);
#elif ASTERISK_VERSION_NUM < 10403
	ast_chan = ast_channel_alloc(TRUE, state, NULL, NULL,
				"VGSM/%s/%d",
				me ? me->name : "null",
				line);
	if (!ast_chan) {
		ast_log(LOG_WARNING, "Unable to allocate channel\n");
		goto err_channel_alloc;
	}
#else
	ast_chan = ast_channel_alloc(TRUE, state, NULL, NULL,
				NULL, NULL, NULL, 0,
				"VGSM/%s/%d",
				me ? me->name : "null",
				line);
	if (!ast_chan) {
		ast_log(LOG_WARNING, "Unable to allocate channel\n");
		goto err_channel_alloc;
	}
#endif

	/* Reference is not taken, as ast_chan is an embedded object */
	ast_chan->tech_pvt = vgsm_chan_get(vgsm_chan);
	ast_chan->tech = &vgsm_tech;

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	ast_chan->type = VGSM_CHAN_TYPE;
#endif

	ast_chan->fds[0] = -1;

	ast_chan->adsicpe = AST_ADSI_UNAVAILABLE;

	if (me->interface_version == 1) {
		ast_chan->nativeformats = AST_FORMAT_ALAW;
		ast_chan->readformat = AST_FORMAT_ALAW;
		ast_chan->rawreadformat = AST_FORMAT_ALAW;
		ast_chan->writeformat = AST_FORMAT_ALAW;
		ast_chan->rawwriteformat = AST_FORMAT_ALAW;
		vgsm_chan->prev_rawreadformat = AST_FORMAT_ALAW;
		vgsm_chan->prev_rawwriteformat = AST_FORMAT_ALAW;
	} else {
		ast_chan->nativeformats =
			AST_FORMAT_SLINEAR | AST_FORMAT_ALAW | AST_FORMAT_ULAW;

		if (format & AST_FORMAT_SLINEAR) {
			ast_chan->readformat = AST_FORMAT_SLINEAR;
			ast_chan->rawreadformat = AST_FORMAT_SLINEAR;
			ast_chan->writeformat = AST_FORMAT_SLINEAR;
			ast_chan->rawwriteformat = AST_FORMAT_SLINEAR;
			vgsm_chan->prev_rawreadformat = AST_FORMAT_SLINEAR;
			vgsm_chan->prev_rawwriteformat = AST_FORMAT_SLINEAR;
		} else if (format & AST_FORMAT_ALAW) {
			ast_chan->readformat = AST_FORMAT_ALAW;
			ast_chan->rawreadformat = AST_FORMAT_ALAW;
			ast_chan->writeformat = AST_FORMAT_ALAW;
			ast_chan->rawwriteformat = AST_FORMAT_ALAW;
			vgsm_chan->prev_rawreadformat = AST_FORMAT_ALAW;
			vgsm_chan->prev_rawwriteformat = AST_FORMAT_ALAW;
		} else if (format & AST_FORMAT_ULAW) {
			ast_chan->readformat = AST_FORMAT_ULAW;
			ast_chan->rawreadformat = AST_FORMAT_ULAW;
			ast_chan->writeformat = AST_FORMAT_ULAW;
			ast_chan->rawwriteformat = AST_FORMAT_ULAW;
			vgsm_chan->prev_rawreadformat = AST_FORMAT_ULAW;
			vgsm_chan->prev_rawwriteformat = AST_FORMAT_ULAW;
		}	
	}

	pbx_builtin_setvar_helper(ast_chan,
		"VGSM_ME_IMEI", me->me.imei);
	ast_cdr_setvar(ast_chan->cdr,
		"VGSM_ME_IMEI", me->me.imei, 0);

	pbx_builtin_setvar_helper(ast_chan,
		"VGSM_SIM_IMSI", me->sim.imsi);
	ast_cdr_setvar(ast_chan->cdr,
		"VGSM_SIM_IMSI", me->sim.imsi, 0);

	pbx_builtin_setvar_helper(ast_chan,
		"VGSM_SIM_ID", me->sim.card_id);
	ast_cdr_setvar(ast_chan->cdr,
		"VGSM_SIM_ID", me->sim.card_id, 0);

	char tmpstr[32];
	snprintf(tmpstr, sizeof(tmpstr), "%03u%02u",
		me->net.mcc, me->net.mnc);
	pbx_builtin_setvar_helper(ast_chan,
		"VGSM_NET_ID", tmpstr);
	ast_cdr_setvar(ast_chan->cdr,
		"VGSM_NET_ID", tmpstr, 0);

	return ast_chan;

	vgsm_chan_put(ast_chan->tech_pvt);
	ast_chan->tech_pvt = NULL;
	ast_channel_free(ast_chan);
err_channel_alloc:

	return NULL;
}

static struct vgsm_chan *vgsm_chan_alloc(void)
{
	struct vgsm_chan *vgsm_chan;

	vgsm_chan = malloc(sizeof(*vgsm_chan));
	if (!vgsm_chan)
		goto err_malloc_vgsm_chan;

	memset(vgsm_chan, 0, sizeof(*vgsm_chan));

	vgsm_chan->refcnt = 1;
	ast_cond_init(&vgsm_chan->refcnt_decremented_cond, NULL);

	vgsm_chan->up_fd = -1;

	vgsm_chan->dsp = ast_dsp_new();
	if (!vgsm_chan->dsp)
		goto err_dsp_new;

	ast_dsp_set_features(vgsm_chan->dsp, DSP_FEATURE_DTMF_DETECT);

	ast_mutex_lock(&vgsm.usecnt_lock);
	vgsm.usecnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);
	ast_update_use_count();

	return vgsm_chan;

	ast_dsp_free(vgsm_chan->dsp);
	vgsm_chan->dsp = NULL;
err_dsp_new:
	free(vgsm_chan);
err_malloc_vgsm_chan:

	return NULL;
}

struct vgsm_chan *vgsm_chan_get(struct vgsm_chan *vgsm_chan)
{
	if (!vgsm_chan)
		return NULL;

	assert(vgsm_chan->refcnt > 0);
	assert(vgsm_chan->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	vgsm_chan->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return vgsm_chan;
}

void _vgsm_chan_put(struct vgsm_chan *vgsm_chan)
{
	if (!vgsm_chan)
		return;

	assert(vgsm_chan->refcnt > 0);
	assert(vgsm_chan->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	int refcnt = --vgsm_chan->refcnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	ast_cond_broadcast(&vgsm_chan->refcnt_decremented_cond);

	if (!refcnt) {
		assert(!vgsm_chan->ast_chan);
		assert(!vgsm_chan->me);

		if (vgsm_chan->dsp) {
			ast_dsp_free(vgsm_chan->dsp);
			vgsm_chan->dsp = NULL;
		}

		if (vgsm_chan->huntgroup) {
			vgsm_hg_put(vgsm_chan->huntgroup);
			vgsm_chan->huntgroup = NULL;
		}

		if (vgsm_chan->hg_first_me) {
			vgsm_me_put(vgsm_chan->hg_first_me);
			vgsm_chan->hg_first_me = NULL;
		}

		if (vgsm_chan->mc) {
			vgsm_me_config_put(vgsm_chan->mc);
			vgsm_chan->mc = NULL;
		}

		free(vgsm_chan);

		ast_mutex_lock(&vgsm.usecnt_lock);
		vgsm.usecnt--;
		ast_mutex_unlock(&vgsm.usecnt_lock);
		ast_update_use_count();
	}
}

//-----------------------------------------------------------------------------

static int vgsm_state_from_var(
	struct vgsm_state *state,
	struct ast_variable *var)
{
	if (!strcasecmp(var->name, "sms_spooler")) { 
		strncpy(state->sms_spooler, var->value,
			sizeof(state->sms_spooler));
	} else if (!strcasecmp(var->name, "sms_spooler_pars")) {
		strncpy(state->sms_spooler_pars, var->value,
			sizeof(state->sms_spooler_pars));
	} else {
		return -1;
	}

	return 0;
}

static struct vgsm_urc_class urc_classes[];

static int vgsm_reload_config(void)
{
	struct ast_config *cfg;
	cfg = ast_config_load(VGSM_CONFIG_FILE);
	if (!cfg) {
		ast_log(LOG_WARNING,
			"Unable to load vgsm config file '%s'\n",
			VGSM_CONFIG_FILE);

		return -1;
	}

	struct ast_variable *var;
	var = ast_variable_browse(cfg, "general");
	while (var) {
		if (vgsm_state_from_var(&vgsm, var) < 0) {
			ast_log(LOG_WARNING,
				"Unknown configuration variable %s\n",
				var->name);
		}

		var = var->next;
	}

	vgsm_me_reload(cfg);
	vgsm_hg_reload(cfg);

	ast_config_destroy(cfg);

	vgsm_operators_init();

	return 0;
}

/*---------------------------------------------------------------------------*/

#ifdef DEBUG_CODE
static int vgsm_debug_timer_func(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.state_lock);
	vgsm.debug_timer = TRUE;
	ast_mutex_unlock(&vgsm.state_lock);

	ast_cli(fd, "vGSM debugging enabled\n");

	return RESULT_SUCCESS;
}

static char vgsm_debug_timer_help[] =
"Usage: vgsm debug timer\n"
"\n"
"	Enable debugging of vGSM timer events\n";

static struct ast_cli_entry vgsm_debug_timer =
{
	{ "vgsm", "debug", "timer", NULL },
	vgsm_debug_timer_func,
	"Enables vGSM timer debugging",
	vgsm_debug_timer_help,
	NULL
};
#endif

/*---------------------------------------------------------------------------*/

#ifdef DEBUG_CODE
static int vgsm_no_debug_timer_func(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.state_lock);
	vgsm.debug_timer = FALSE;
	ast_mutex_unlock(&vgsm.state_lock);

	ast_cli(fd, "vGSM debugging disabled\n");

	return RESULT_SUCCESS;
}

static struct ast_cli_entry vgsm_no_debug_timer =
{
	{ "vgsm", "no", "debug", "timer", NULL },
	vgsm_no_debug_timer_func,
	"Disables debuggin of vGSM timer events",
	NULL,
	NULL
};
#endif

/*---------------------------------------------------------------------------*/

static int vgsm_reload_func(int fd, int argc, char *argv[])
{
	if (vgsm_reload_config() < 0) {
		ast_cli(fd, "Error reloading configuration\n");
		return RESULT_FAILURE;
	}

	return RESULT_SUCCESS;
}

static char vgsm_vgsm_reload_help[] =
"Usage: vgsm reload\n"
"\n"
"	Reloads the vGSM configuration.\n"
"\n"
"	The me's configuration is loaded using a multiversion approach;\n"
"	Calls using the old configuration will still use it, while new calls\n"
"	will use the newly loaded configuration\n";

static struct ast_cli_entry vgsm_reload =
{
	{ "vgsm", "reload", NULL },
	vgsm_reload_func,
	"Reloads vGSM configuration",
	vgsm_vgsm_reload_help,
	NULL
};

/*---------------------------------------------------------------------------*/

static int vgsm_pipeline_set_amu_compander(
	struct ks_pipeline *pipeline,
	BOOL enabled,
	BOOL mu_mode,
	BOOL debug)
{
	/* TODO: Do this only once */
	struct ks_feature *amu_compander_attr;

	amu_compander_attr = ks_feature_get_by_name(ks_conn, "amu_compander");
	if (!amu_compander_attr) {
		ast_log(LOG_ERROR,
			"Cannot find amu_compander attr\n");
		goto err_missing_amu_compander;
	}

	struct ks_amu_compander_descr *amu_compander = NULL;

	int i;
	for(i=0; i<pipeline->chans_cnt; i++) {
		struct ks_chan *chan = pipeline->chans[i];
		struct ks_feature_value *featval;

		list_for_each_entry(featval, &chan->features, node) {

			if (featval->feature == amu_compander_attr) {
				struct ks_amu_compander_descr *descr =
					(struct ks_amu_compander_descr *)
					featval->payload;

				if (!amu_compander || descr->hardware)
					amu_compander = descr;
			}
		}
	}

	if (!amu_compander) {
		ast_log(LOG_ERROR,
			"Cannot find amu_compander along the pipeline\n");
		goto err_missing_amu_compander_in_pipeline;
	}

	amu_compander->enabled = enabled;
	amu_compander->mu_mode = mu_mode;

	if (debug)
		ast_verbose("Compander set to %s/%s\n",
				enabled ? "enabled" : "disabled",
				mu_mode ? "u-law" : "a-law");

	return 0;

err_missing_amu_compander_in_pipeline:
err_missing_amu_compander:

	return -1;
}

static int vgsm_pipeline_set_amu_decompander(
	struct ks_pipeline *pipeline,
	BOOL enabled,
	BOOL mu_mode,
	BOOL debug)
{
	/* TODO: Do this only once */
	struct ks_feature *amu_decompander_attr;

	amu_decompander_attr = ks_feature_get_by_name(ks_conn,
						"amu_decompander");
	if (!amu_decompander_attr) {
		ast_log(LOG_ERROR,
			"Cannot find amu_decompander attr\n");
		goto err_missing_amu_decompander;
	}

	struct ks_amu_decompander_descr *amu_decompander = NULL;

	int i;
	for(i=0; i<pipeline->chans_cnt; i++) {
		struct ks_chan *chan = pipeline->chans[i];
		struct ks_feature_value *featval;

		list_for_each_entry(featval, &chan->features, node) {

			if (featval->feature == amu_decompander_attr) {

				struct ks_amu_decompander_descr *descr =
					(struct ks_amu_decompander_descr *)
					featval->payload;

				if (!amu_decompander || descr->hardware)
					amu_decompander = descr;
			}
		}
	}

	if (!amu_decompander) {
		ast_log(LOG_ERROR,
			"Cannot find amu_decompander along the pipeline\n");
		goto err_missing_amu_decompander_in_pipeline;
	}

	amu_decompander->enabled = enabled;
	amu_decompander->mu_mode = mu_mode;

	if (debug)
		ast_verbose("Decompander set to %s/%s\n",
				enabled ? "enabled" : "disabled",
				mu_mode ? "u-law" : "a-law");

	return 0;

err_missing_amu_decompander_in_pipeline:
err_missing_amu_decompander:

	return -1;
}

int vgsm_connect_channel(struct vgsm_chan *vgsm_chan)
{
	int err;

	__u32 me_node_id;
	if (ioctl(vgsm_chan->me->me_fd, VGSM_IOC_GET_NODEID,
					(caddr_t)&me_node_id) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VGSM_IOC_GET_NODEID): %s\n",
			strerror(errno));
		err = -errno;
		goto err_get_me_node_id;
	}

	vgsm_chan->up_fd = open("/dev/ks/userport_stream", O_RDWR);
	if (vgsm_chan->up_fd < 0) {
		ast_log(LOG_ERROR,
			"Cannot open userport: %s\n",
			strerror(errno));
		err = -errno;
		goto err_open_userport;
	}

	vgsm_chan->ast_chan->fds[0] = vgsm_chan->up_fd;

	__u32 up_node_id;
	if (ioctl(vgsm_chan->up_fd, KS_UP_GET_NODEID,
					(caddr_t)&up_node_id) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(KS_UP_GET_NODEID): %s\n",
			strerror(errno));
		err = -errno;
		goto err_get_up_node_id;
	}

	err = ks_conn_remote_topology_lock(ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot lock kstreamer topology: %s\n", strerror(-err));
		goto err_kstreamer_lock;
	}

	vgsm_chan->node_userport = ks_node_get_by_id(ks_conn, up_node_id);
	if (!vgsm_chan->node_userport) {
		ast_log(LOG_ERROR, "Userport's node not found\n");
		err = -ENOENT;
		goto err_up_node_not_found;
	}

	vgsm_chan->node_me = ks_node_get_by_id(ks_conn, me_node_id);
	if (!vgsm_chan->node_me) {
		ast_log(LOG_ERROR, "ME's node not found\n");
		err = -ENOENT;
		goto err_me_node_not_found;
	}

	vgsm_me_debug_state(vgsm_chan->me,
			"Connecting userport %06d to chan %06d\n",
			vgsm_chan->node_userport->id,
			vgsm_chan->node_me->id);

	/* Create RX pipeline */
	vgsm_chan->pipeline_rx = ks_pipeline_alloc();
	if (!vgsm_chan->pipeline_rx) {
		ast_log(LOG_ERROR,
			"Cannot allocate RX pipeline\n");
		err = -ENOMEM;
		goto err_pipeline_rx_alloc;
	}

	err = ks_pipeline_autoroute(vgsm_chan->pipeline_rx, ks_conn,
				vgsm_chan->node_me,
				vgsm_chan->node_userport);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot connect RX pipeline's nodes: %s\n",
			strerror(-err));
		goto err_pipeline_rx_connect;
	}

	err = ks_pipeline_create(vgsm_chan->pipeline_rx, ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot create RX pipeline: %s\n",
			strerror(-err));
		goto err_pipeline_rx_create;
	}

	if (vgsm_chan->me->interface_version == 2) {

		err = vgsm_pipeline_set_amu_compander(vgsm_chan->pipeline_rx,
				vgsm_chan->ast_chan->rawreadformat !=
							AST_FORMAT_SLINEAR,
				vgsm_chan->ast_chan->rawreadformat ==
							AST_FORMAT_ULAW,
				vgsm_chan->me->debug_frames);
		if (err < 0) {
			ast_log(LOG_ERROR,
				"Cannot enable RX amu_compander\n");
			goto err_pipeline_rx_amu_compander_enable;
		}
	}

	err = ks_pipeline_update_chans(vgsm_chan->pipeline_rx, ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot update RX pipeline's channels\n");
		goto err_pipeline_rx_update_chans;
	}

	/* Create TX pipeline */
	vgsm_chan->pipeline_tx = ks_pipeline_alloc();
	if (!vgsm_chan->pipeline_tx) {
		ast_log(LOG_ERROR,
			"Cannot allocate TX pipeline\n");
		err = -ENOMEM;
		goto err_pipeline_tx_alloc;
	}

	err = ks_pipeline_autoroute(vgsm_chan->pipeline_tx, ks_conn,
				vgsm_chan->node_userport,
				vgsm_chan->node_me);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot connect TX pipeline's nodes: %s\n",
			strerror(-err));

		goto err_pipeline_tx_connect;
	}

	err = ks_pipeline_create(vgsm_chan->pipeline_tx, ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot create TX pipeline: %s\n",
			strerror(-err));
		goto err_pipeline_tx_create;
	}

	err = ks_conn_remote_topology_unlock(ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Error unlocking kstreamer's topology\n");
	}

	if (vgsm_chan->me->interface_version == 2) {
		err = vgsm_pipeline_set_amu_decompander(vgsm_chan->pipeline_tx,
				vgsm_chan->ast_chan->rawwriteformat !=
							AST_FORMAT_SLINEAR,
				vgsm_chan->ast_chan->rawwriteformat ==
							AST_FORMAT_ULAW,
				vgsm_chan->me->debug_frames);
		if (err < 0) {
			ast_log(LOG_ERROR,
				"Cannot enable TX amu_decompander\n");
			goto err_pipeline_tx_decompander_enable;
		}
	}

	err = ks_pipeline_update_chans(vgsm_chan->pipeline_tx, ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot update TX pipeline's channels\n");
		goto err_pipeline_tx_update_chans;
	}

	/* Start RX pipelines */
	vgsm_chan->pipeline_rx->status = KS_PIPELINE_STATUS_FLOWING;

	err = ks_pipeline_update(vgsm_chan->pipeline_rx, ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
				"Cannot start the RX pipeline\n");
		goto err_pipeline_rx_update;
	}

	/* Start TX pipelines */
	vgsm_chan->pipeline_tx->status = KS_PIPELINE_STATUS_FLOWING;

	err = ks_pipeline_update(vgsm_chan->pipeline_tx, ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
				"Cannot start the TX pipeline\n");
		goto err_pipeline_tx_update;
	}

	return 0;

err_pipeline_tx_update:
err_pipeline_rx_update:
err_pipeline_tx_update_chans:
err_pipeline_tx_decompander_enable:
err_pipeline_tx_create:
err_pipeline_tx_connect:
	ks_pipeline_put(vgsm_chan->pipeline_tx);
	vgsm_chan->pipeline_tx = NULL;
err_pipeline_tx_alloc:
err_pipeline_rx_update_chans:
err_pipeline_rx_amu_compander_enable:
err_pipeline_rx_create:
err_pipeline_rx_connect:
	ks_pipeline_put(vgsm_chan->pipeline_rx);
	vgsm_chan->pipeline_rx = NULL;
err_pipeline_rx_alloc:
	ks_node_put(vgsm_chan->node_me);
	vgsm_chan->node_me = NULL;
err_me_node_not_found:
	ks_node_put(vgsm_chan->node_userport);
	vgsm_chan->node_userport = NULL;
err_up_node_not_found:
	ks_conn_remote_topology_unlock(ks_conn);
err_kstreamer_lock:
err_get_up_node_id:
	vgsm_chan->ast_chan->fds[0] = -1;
	close(vgsm_chan->up_fd);
	vgsm_chan->up_fd = -1;
err_open_userport:
err_get_me_node_id:

	return err;
}

struct vgsm_chan *vgsm_alloc_inbound_call(struct vgsm_me *me)
{
	struct vgsm_chan *vgsm_chan;
	vgsm_chan = vgsm_chan_alloc();
	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "Cannot allocate vgsm_chan\n");
		goto err_vgsm_chan_alloc;
	}

	vgsm_chan->me = vgsm_me_get(me);
	vgsm_chan->mc = vgsm_me_config_get(me->current_config);

	vgsm_chan->outbound = FALSE;

	vgsm_chan->ast_chan = vgsm_ast_chan_alloc(vgsm_chan,
						AST_STATE_RESERVED,
						me, 1, AST_FORMAT_SLINEAR);

	if (!vgsm_chan->ast_chan)
		goto err_vgsm_ast_chan_alloc;

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
#else
	ast_jb_configure(vgsm_chan->ast_chan, &vgsm_chan->mc->jbconf);
#endif

	ast_dsp_digitmode(vgsm_chan->dsp,
		DSP_DIGITMODE_DTMF |
		(vgsm_chan->mc->dtmf_quelch ? 0 : DSP_DIGITMODE_NOQUELCH) |
		(vgsm_chan->mc->dtmf_mutemax ? DSP_DIGITMODE_MUTEMAX : 0) |
		(vgsm_chan->mc->dtmf_relax ? DSP_DIGITMODE_RELAXDTMF : 0));

	struct ast_channel *ast_chan = vgsm_chan->ast_chan;

	strcpy(ast_chan->exten, "s");
	strncpy(ast_chan->context, vgsm_chan->mc->context,
					sizeof(ast_chan->context));
	ast_chan->priority = 1;
	strncpy(ast_chan->language, vgsm_chan->mc->language, sizeof(ast_chan->language));
	return vgsm_chan;

	vgsm_chan_put(vgsm_chan->ast_chan->tech_pvt);
	vgsm_chan->ast_chan->tech_pvt = NULL;
	ast_channel_free(vgsm_chan->ast_chan);
	vgsm_chan->ast_chan = NULL;
err_vgsm_ast_chan_alloc:
	vgsm_me_put(vgsm_chan->me);
	vgsm_chan->me = NULL;
	vgsm_me_config_put(vgsm_chan->mc);
	vgsm_chan->mc = NULL;
	vgsm_chan_put(vgsm_chan);
err_vgsm_chan_alloc:

	return NULL;
}

static void vgsm_atd_complete(struct vgsm_req *req, void *data)
{
	struct vgsm_me *me = data;

	if (req->err != VGSM_RESP_OK) {
		ast_mutex_lock(&me->lock);
		struct vgsm_chan *vgsm_chan = vgsm_chan_get(me->vgsm_chan);
		ast_mutex_unlock(&me->lock);

		if (vgsm_chan)
			ast_softhangup(vgsm_chan->ast_chan, AST_SOFTHANGUP_DEV);

		vgsm_me_debug_call(me, "Unable to dial: ATD failed\n");

		vgsm_chan_put(vgsm_chan);
	}

	vgsm_me_put(me);
}
static int vgsm_call(
	struct ast_channel *ast_chan,
	char *orig_dest,
	int timeout)
{
	int err;

	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	struct vgsm_me *me = vgsm_me_get(vgsm_chan->me);

	assert(ast_chan->_state == AST_STATE_DOWN);

	ast_mutex_lock(&me->lock);

	if (me->status != VGSM_ME_STATUS_READY) {
		ast_mutex_unlock(&me->lock);

		vgsm_me_debug_call(me, "ME not ready\n");
		ast_chan->hangupcause = AST_CAUSE_NETWORK_OUT_OF_ORDER;
		err = -1;
		goto err_me_not_ready;
	}

	if (me->net.status != VGSM_NET_STATUS_REGISTERED_HOME &&
	    me->net.status != VGSM_NET_STATUS_REGISTERED_ROAMING) {
		ast_mutex_unlock(&me->lock);

		vgsm_me_debug_call(me, "ME is not registered\n");
		ast_chan->hangupcause = AST_CAUSE_NETWORK_OUT_OF_ORDER;
		err = -1;
		goto err_me_not_registered;
	}

	if (me->vgsm_chan) {
		ast_mutex_unlock(&me->lock);

		vgsm_me_debug_call(me, "ME is busy (call present)\n");
		ast_chan->hangupcause = AST_CAUSE_NORMAL_CIRCUIT_CONGESTION;
		err = -1;
		goto err_me_busy;
	}

	me->stats.outbound++;
	me->vgsm_chan = vgsm_chan_get(vgsm_chan);
	me->call_present = FALSE;

	ast_dsp_digitmode(vgsm_chan->dsp,
		DSP_DIGITMODE_DTMF |
		(vgsm_chan->mc->dtmf_quelch ? 0 : DSP_DIGITMODE_NOQUELCH) |
		(vgsm_chan->mc->dtmf_mutemax ? DSP_DIGITMODE_MUTEMAX : 0) |
		(vgsm_chan->mc->dtmf_relax ? DSP_DIGITMODE_RELAXDTMF : 0));

	char newname[40];
	snprintf(newname, sizeof(newname), "VGSM/%s/%d", me->name, 1);

	ast_mutex_unlock(&me->lock);

	ast_change_name(ast_chan, newname);
	ast_setstate(ast_chan, AST_STATE_DIALING);

	vgsm_me_debug_call(me, "Calling %s on %s\n",
			vgsm_chan->called_number,
			ast_chan->name);

	me->call_present = TRUE;

	char options[5] = "";
	if (vgsm_chan->called_number[0] != '*') {
		if ((ast_chan->cid.cid_pres & AST_PRES_RESTRICTION) ==
					AST_PRES_ALLOWED)
			strcpy(options, "i");
		else
			strcpy(options, "I");
	}

	struct vgsm_req *req;
	// 'timeout' instead of 20s ?
	req = vgsm_req_make_callback(&me->comm,
			vgsm_atd_complete,
			vgsm_me_get(me),
			180 * SEC, "ATD%s%s;",
			options,
			vgsm_chan->called_number);
	if (!req) {
		ast_log(LOG_ERROR, "%s: Unable to dial: ATD failed\n",
			me->name);

		err = -1;
		ast_chan->hangupcause = AST_CAUSE_NETWORK_OUT_OF_ORDER;
		goto err_atd_failed;
	}

	vgsm_req_put(req);

	vgsm_me_put(me);

	return 0;

err_atd_failed:
	vgsm_me_config_put(vgsm_chan->mc);
	vgsm_me_put(vgsm_chan->me);

	ast_mutex_lock(&me->lock);
	vgsm_chan_put(me->vgsm_chan);
	me->vgsm_chan = NULL;
	ast_mutex_unlock(&me->lock);
err_me_busy:
err_me_not_registered:
err_me_not_ready:

	return err;
}

static int vgsm_answer(struct ast_channel *ast_chan)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	struct vgsm_me *me = vgsm_chan->me;
	int err;

	assert(vgsm_chan);

	ast_mutex_lock(&me->lock);
	if (me->status != VGSM_ME_STATUS_READY) {
		ast_mutex_unlock(&me->lock);

		ast_log(LOG_NOTICE, "ME is not ready anymore\n");
		return -1;
	}
	ast_mutex_unlock(&me->lock);

	ast_mutex_lock(&ast_chan->lock);
	if (vgsm_connect_channel(vgsm_chan) < 0) {
		ast_mutex_unlock(&ast_chan->lock);
		return -1;
	}
	ast_mutex_unlock(&ast_chan->lock);

	ast_indicate(ast_chan, -1);

	err = vgsm_req_make_wait_result(&me->comm, 1 * SEC, "ATA");
	if (err != VGSM_RESP_OK) {

		if (err != VGSM_RESP_NO_CARRIER)
			ast_log(LOG_WARNING, "Couldn't answer: %s\n",
				vgsm_me_error_to_text(err));

		return -1;
	}

	return 0;
}

static int vgsm_bridge(
	struct ast_channel *c0,
	struct ast_channel *c1,
	int flags, struct ast_frame **fo,
	struct ast_channel **rc,
	int timeoutms)
{
	return AST_BRIDGE_FAILED_NOWARN;
}

struct ast_frame *vgsm_exception(struct ast_channel *ast_chan)
{
	ast_log(LOG_WARNING, "vgsm_exception\n");

	return NULL;
}

/* We are called with chan->lock'ed */
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
static int vgsm_indicate(struct ast_channel *ast_chan, int condition)
#else
static int vgsm_indicate(
	struct ast_channel *ast_chan,
	int condition,
	const void *data,
	size_t datalen)
#endif
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
		
	//struct vgsm_me *me = vgsm_chan->me;
	struct vgsm_me_config *mc = vgsm_chan->mc;
	
	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "NO VGSM_CHAN!!\n");
		return 1;
	}

	switch(condition) {
	case AST_CONTROL_RING:
	case AST_CONTROL_TAKEOFFHOOK:
	case AST_CONTROL_FLASH:
	case AST_CONTROL_WINK:
	case AST_CONTROL_OPTION:
	case AST_CONTROL_RADIO_KEY:
	case AST_CONTROL_RADIO_UNKEY:
	case AST_CONTROL_OFFHOOK:
	case AST_CONTROL_RINGING:
	case AST_CONTROL_PROGRESS:
	case AST_CONTROL_PROCEEDING:
		return 1;
	break;

	case -1:
		ast_playtones_stop(ast_chan);

		return 0;
	break;
	
		
	case AST_CONTROL_HOLD:
		ast_moh_start(ast_chan, data, mc->mohinterpret);
		break;

	case AST_CONTROL_UNHOLD:
		ast_moh_stop(ast_chan);
		break;
	
	case AST_CONTROL_ANSWER:
		ast_playtones_stop(ast_chan);

		return 0;
	break;

	case AST_CONTROL_HANGUP:
		ast_log(LOG_ERROR, "Unexpected AST_CONTROL_HANGUP\n");
		return 0;
	break;

	case AST_CONTROL_CONGESTION:
/*		vgsm_me_counter_inc(me,
			vgsm_chan->outbound,
			VGSM_CAUSE_LOCATION_LOCAL,
			42);

		vgsm_me_hangup(me);*/
	break;

	case AST_CONTROL_BUSY:
/*		vgsm_me_counter_inc(me,
			vgsm_chan->outbound,
			VGSM_CAUSE_LOCATION_LOCAL,
			17);

		vgsm_me_hangup(me);*/
	break;
	}

	return 1;
}

static int vgsm_fixup(
	struct ast_channel *oldchan,
	struct ast_channel *newchan)
{
	struct vgsm_chan *chan = to_vgsm_chan(newchan);

	if (chan->ast_chan != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n",
				oldchan, chan->ast_chan);
		return -1;
	}

	chan->ast_chan = newchan;

	return 0;
}

static int vgsm_setoption(
	struct ast_channel *ast_chan,
	int option,
	void *data,
	int datalen)
{
	ast_log(LOG_ERROR, "%s\n", __FUNCTION__);

	return -1;
}

static int vgsm_transfer(
	struct ast_channel *ast,
	const char *dest)
{
	ast_log(LOG_ERROR, "%s\n", __FUNCTION__);

	return -1;
}

#if ASTERISK_VERSION_NUM < 10402 
static int vgsm_send_digit(struct ast_channel *ast_chan, char digit)
{
	int duration = 300;
#else
static int vgsm_send_digit(
	struct ast_channel *ast_chan,
	char digit,
	unsigned int duration)
{
#endif
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	struct vgsm_me *me = vgsm_chan->me;

	if (duration < 300)
		duration = 300;

	/* VTS takes duration in 1/100 sec */
	_vgsm_req_put(vgsm_req_make(
		&me->comm, 2 * SEC, "AT+VTS=%c,%u", digit, duration / 100));

	return 0;
}

static int vgsm_sendtext(struct ast_channel *ast, const char *text)
{
	ast_log(LOG_WARNING, "%s\n", __FUNCTION__);

	return -1;
}

static void vgsm_disconnect_channel(
	struct vgsm_chan *vgsm_chan)
{
	int err;

	if (vgsm_chan->up_fd < 0)
		return;

	if (vgsm_chan->pipeline_rx) {
		vgsm_chan->pipeline_rx->status = KS_PIPELINE_STATUS_CONNECTED;
		err = ks_pipeline_update(vgsm_chan->pipeline_rx, ks_conn);
		if (err < 0) {
			ast_log(LOG_ERROR, "Cannot stop the pipeline\n");
			return;
		}

		err = ks_pipeline_destroy(vgsm_chan->pipeline_rx, ks_conn);
		if (err < 0) {
			ast_log(LOG_ERROR, "Cannot destroy the pipeline\n");
			return;
		}

		ks_pipeline_put(vgsm_chan->pipeline_rx);
		vgsm_chan->pipeline_rx = NULL;
	}

	if (vgsm_chan->pipeline_tx) {
		vgsm_chan->pipeline_tx->status = KS_PIPELINE_STATUS_CONNECTED;
		err = ks_pipeline_update(vgsm_chan->pipeline_tx, ks_conn);
		if (err < 0) {
			ast_log(LOG_ERROR, "Cannot stop the pipeline\n");
			return;
		}

		err = ks_pipeline_destroy(vgsm_chan->pipeline_tx, ks_conn);
		if (err < 0) {
			ast_log(LOG_ERROR, "Cannot destroy the pipeline\n");
			return;
		}

		ks_pipeline_put(vgsm_chan->pipeline_tx);
		vgsm_chan->pipeline_tx = NULL;
	}

	if (close(vgsm_chan->up_fd) < 0) {
		ast_log(LOG_ERROR,
			"close(vgsm_chan->up_fd): %s\n",
			strerror(errno));
	}

	vgsm_chan->up_fd = -1;
}

static int vgsm_hangup(struct ast_channel *ast_chan)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	assert(vgsm_chan);

	ast_setstate(ast_chan, AST_STATE_DOWN);

	/* We are assigned to a ME AND we are the ME's current call */
	if (vgsm_chan->me) {
		ast_mutex_lock(&vgsm_chan->me->lock);

		if (vgsm_chan->me->vgsm_chan == vgsm_chan) {
			vgsm_me_counter_inc(vgsm_chan->me,
				vgsm_chan->outbound,
				VGSM_CAUSE_LOCATION_LOCAL,
				ast_chan->hangupcause != 0 ?
					ast_chan->hangupcause : 16);

			/* Send deferred hangup */

			if (vgsm_chan->me->call_present) {
				_vgsm_req_put(vgsm_req_make_callback(
					&vgsm_chan->me->comm,
					vgsm_me_chup_complete,
					vgsm_me_get(vgsm_chan->me),
					5 * SEC, "AT+CHUP"));
			}

			/* Detach ME and channel */
			vgsm_chan_put(vgsm_chan->me->vgsm_chan);
			vgsm_chan->me->vgsm_chan = NULL;
		}

		ast_mutex_unlock(&vgsm_chan->me->lock);

		vgsm_me_put(vgsm_chan->me);
		vgsm_chan->me = NULL;
	}

	if (vgsm_chan->up_fd >= 0)
		vgsm_disconnect_channel(vgsm_chan);

	/* Okay, here we should have done our work, but, there may be
	 * references to vgsm_chan held in other threads, so, before giving
	 * control back to Assterisk (which will free the ast_chan structure
	 * we have to wait until all threads have released their reference.
	 */
	int res = 0;
	struct timespec timeout;
	struct timeval now;
	gettimeofday( &now, NULL );
	timeout.tv_sec = now.tv_sec + 30;
	timeout.tv_nsec = now.tv_usec * 1000;

	/* We are called holding ast_chan->lock, only the FSM knows why.
	 * Of course if another thread holding a reference to vgsm_chan needs
	 * to lock the ast_chan we get stuck in a near-deadlock situation.
	 *
	 * Thus, we release the lock before waiting.
	 */
	ast_mutex_unlock(&ast_chan->lock);

	ast_mutex_lock(&vgsm.usecnt_lock);
	while(vgsm_chan->refcnt > 1 && res == 0) {
		res = ast_cond_timedwait(
				&vgsm_chan->refcnt_decremented_cond,
				&vgsm.usecnt_lock, &timeout);
	}
	ast_mutex_unlock(&vgsm.usecnt_lock);

	ast_mutex_lock(&ast_chan->lock);

	assert(vgsm_chan->refcnt > 0);

	if (res == ETIMEDOUT) {
		ast_log(LOG_ERROR,
			"%s: Timeout waiting for references to"
			" vgsm_chan to be released, possible reference"
			" leak, %d references left. Expect a crash.\n",
			ast_chan->name,
			vgsm_chan->refcnt);
	}

	vgsm_chan->ast_chan = NULL;

	vgsm_chan_put(ast_chan->tech_pvt);
	ast_chan->tech_pvt = NULL;

	return 0;
}

static struct ast_frame *vgsm_read(struct ast_channel *ast_chan)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	struct ast_frame *frame = &vgsm_chan->frame_out;

	frame->src = VGSM_CHAN_TYPE;
	frame->mallocd = 0;
	frame->delivery.tv_sec = 0;
	frame->delivery.tv_usec = 0;

	if (vgsm_chan->up_fd < 0) {
		frame->frametype = AST_FRAME_NULL;
		frame->subclass = 0;
		frame->samples = 0;
		frame->datalen = 0;
		frame->data = NULL;
		frame->offset = 0;

		return frame;
	}

	if (ast_chan->rawreadformat != vgsm_chan->prev_rawreadformat) {
		int err;
		err = vgsm_pipeline_set_amu_compander(vgsm_chan->pipeline_rx,
				ast_chan->rawreadformat != AST_FORMAT_SLINEAR,
				ast_chan->rawreadformat == AST_FORMAT_ULAW,
				vgsm_chan->me->debug_frames);
		if (err < 0)
			ast_log(LOG_ERROR,
				"Cannot set amu_decompander\n");

		vgsm_chan->prev_rawreadformat = ast_chan->rawreadformat;

		err = ks_pipeline_update_chans(vgsm_chan->pipeline_rx, ks_conn);
		if (err < 0) {
			ast_log(LOG_ERROR,
				"Cannot update pipeline's channels\n");
		}

		err = ks_pipeline_restart(vgsm_chan->pipeline_rx, ks_conn);
		if (err < 0)
			ast_log(LOG_ERROR, "Cannot restart the pipeline\n");

		if (vgsm_chan->me->debug_frames)
			ast_verbose("Read format changed to %02x\n",
					ast_chan->rawreadformat);
	}

	int nread = read(vgsm_chan->up_fd,
			vgsm_chan->frame_out_buf + AST_FRIENDLY_OFFSET,
			sizeof(vgsm_chan->frame_out_buf) - AST_FRIENDLY_OFFSET);
	if (nread < 0) {
//		ast_log(LOG_WARNING, "read error: %s\n", strerror(errno));
		return NULL;
	}

	int sample_size;
	if (ast_chan->rawreadformat == AST_FORMAT_ALAW ||
	    ast_chan->rawreadformat == AST_FORMAT_ULAW)
		sample_size = sizeof(__u8);
	else if (ast_chan->rawreadformat == AST_FORMAT_SLINEAR)
		sample_size = sizeof(__s16);
	else {
		ast_log(LOG_WARNING,
			"Cannot generate frames in %d format\n",
			ast_chan->rawreadformat);
		return NULL;
	}

	longtime_t now = longtime_now();

	if (vgsm_chan->me->debug_frames) {
		__u8 *buf = vgsm_chan->frame_out_buf + AST_FRIENDLY_OFFSET;

		ast_verbose(
			"R %7.3f     F%02x R%02x N%02x"
			" %02x%02x%02x%02x%02x%02x%02x%02x"
			" %d(%d)\n",
			(now - vgsm_chan->last_rx) / 1000.0,
			ast_chan->readformat, ast_chan->rawreadformat,
			ast_chan->nativeformats,
			*(__u8 *)(buf + 0),
			*(__u8 *)(buf + 1),
			*(__u8 *)(buf + 2),
			*(__u8 *)(buf + 3),
			*(__u8 *)(buf + 4),
			*(__u8 *)(buf + 5),
			*(__u8 *)(buf + 6),
			*(__u8 *)(buf + 7),
			nread,
			nread / sample_size);
	}

	vgsm_chan->last_rx = longtime_now();

	frame->frametype = AST_FRAME_VOICE;
	frame->subclass = ast_chan->rawreadformat;
	frame->samples = nread / sample_size;
	frame->data = vgsm_chan->frame_out_buf + AST_FRIENDLY_OFFSET;
	frame->datalen = nread;
	frame->offset = AST_FRIENDLY_OFFSET;

	frame = ast_dsp_process(ast_chan, vgsm_chan->dsp, frame);

	return frame;
}

static int vgsm_write(
	struct ast_channel *ast_chan,
	struct ast_frame *frame)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);

	if (frame->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING,
			"Don't know what to do with frame type '%d'\n",
			frame->frametype);

		return 0;
	}

	if (!(frame->subclass & ast_chan->nativeformats)) {
		char s1[128], s2[128], s3[128];
		ast_log(LOG_WARNING, "Asked to transmit frame type %d,"
			" while native formats is %s(%d)"
			" read/write = %s(%d)/%s(%d)\n",
			frame->subclass, 
			ast_getformatname_multiple(s1, sizeof(s1) - 1,
				ast_chan->nativeformats),
			ast_chan->nativeformats,
			ast_getformatname_multiple(s2, sizeof(s2) - 1,
				ast_chan->readformat),
			ast_chan->readformat,
			ast_getformatname_multiple(s3, sizeof(s3) - 1,
				ast_chan->writeformat),
			ast_chan->writeformat);

		return 0;
	}

	if (!vgsm_chan->pipeline_tx)
		return 0;

	if (ast_chan->rawwriteformat != vgsm_chan->prev_rawwriteformat) {
		int err;
		err = vgsm_pipeline_set_amu_decompander(vgsm_chan->pipeline_tx,
				vgsm_chan->ast_chan->rawwriteformat !=
							AST_FORMAT_SLINEAR,
				vgsm_chan->ast_chan->rawwriteformat ==
							AST_FORMAT_ULAW,
				vgsm_chan->me->debug_frames);
		if (err < 0)
			ast_log(LOG_ERROR,
				"Cannot set amu_decompander\n");

		vgsm_chan->prev_rawwriteformat = ast_chan->rawwriteformat;

		err = ks_pipeline_update_chans(vgsm_chan->pipeline_tx, ks_conn);
		if (err < 0) {
			ast_log(LOG_ERROR,
				"Cannot update pipeline's channels\n");
		}

		err = ks_pipeline_restart(vgsm_chan->pipeline_rx, ks_conn);
		if (err < 0)
			ast_log(LOG_ERROR, "Cannot restart the pipeline\n");

		if (vgsm_chan->me->debug_frames)
			ast_verbose("Write format changed to %02x\n",
					ast_chan->rawwriteformat);
	}

	if (frame->subclass != ast_chan->rawwriteformat) {
		ast_log(LOG_WARNING, "Frame subclass %02x does not match"
				" rawwriteformat %02x\n", 
				frame->subclass, ast_chan->rawwriteformat);
		return 0;
	}

	int sample_size;
	if (frame->subclass == AST_FORMAT_ALAW ||
	    frame->subclass == AST_FORMAT_ULAW)
		sample_size = sizeof(__u8);
	else if (frame->subclass == AST_FORMAT_SLINEAR)
		sample_size = sizeof(__s16);
	else
		return 0;

	if (vgsm_chan->up_fd < 0) {
		if (vgsm_chan->me->debug_frames)
			ast_verbose(
				"Dropped frame write on unconnected channel\n");

		return 0;
	}

	if (!frame->datalen)
		return 0;

	struct vgsm_me_config *mc = vgsm_chan->mc;

	int pressure;
	if (ioctl(vgsm_chan->up_fd, KS_UP_GET_PRESSURE, &pressure)) {
		ast_log(LOG_ERROR, "ioctl(KS_UP_GET_PRESSURE): %s\n",
						strerror(errno));
	}

	pressure = pressure / sample_size;

	int len = frame->datalen;
	__u8 *buf = frame->data;

	vgsm_chan->pressure_average =
		((mc->jitbuf_average * vgsm_chan->pressure_average) +
		pressure) / (mc->jitbuf_average + 1);

	longtime_t now = longtime_now();
	if (now - vgsm_chan->last_tx > frame->samples * 125 *
							mc->jitbuf_maxhole) {

		int diff = (mc->jitbuf_high + mc->jitbuf_low) / 2;
		int diff_octs = diff * sample_size;

		vgsm_chan->pressure_average = diff;

		buf = alloca(len + diff_octs);

		if (frame->subclass == AST_FORMAT_SLINEAR)
			memset(buf, 0x00, diff_octs);
		else
			memset(buf, 0x2a, diff_octs);

		memcpy(buf + diff_octs, frame->data, len);
		len += diff_octs;

		vgsm_debug_jitbuf(vgsm_chan->me,
			"TX delivery late (%lld ms), adding %d samples and"
			" resetting pressure average\n",
			(now - vgsm_chan->last_tx) / 1000,
			diff);

	} else if (pressure < mc->jitbuf_hardlow) {
		int diff = (mc->jitbuf_hardlow - pressure);
		int diff_octs = diff * sample_size;

		buf = alloca(len + diff_octs);

		if (frame->subclass == AST_FORMAT_SLINEAR)
			memset(buf, 0x00, diff_octs);
		else
			memset(buf, 0x2a, diff_octs);

		memcpy(buf + diff_octs, frame->data, len);
		len += diff_octs;

		vgsm_debug_jitbuf(vgsm_chan->me,
			"TX under hard low-mark: added %d samples\n",
			diff);

	} else if (vgsm_chan->pressure_average < mc->jitbuf_low &&
					    pressure < mc->jitbuf_low) {
		int diff = (mc->jitbuf_low - vgsm_chan->pressure_average);
		int diff_octs = diff * sample_size;

		buf = alloca(len + diff_octs);

		if (frame->subclass == AST_FORMAT_SLINEAR)
			memset(buf, 0x00, diff_octs);
		else
			memset(buf, 0x2a, diff_octs);

		memcpy(buf + diff_octs, frame->data, len);
		len += diff_octs;

		vgsm_debug_jitbuf(vgsm_chan->me,
			"TX under low-mark: added %d samples\n",
			diff);

	} else if (pressure + len > mc->jitbuf_hardhigh) {

		int drop = min(len / sample_size, (pressure + len -
							mc->jitbuf_hardhigh));

		vgsm_debug_jitbuf(vgsm_chan->me,
			"TX %d over hard high-mark: dropped %d samples\n",
			pressure + len - mc->jitbuf_hardhigh,
			drop);

		len = max(0, len - drop * sample_size);

	} else if (vgsm_chan->pressure_average > mc->jitbuf_high &&
					    pressure > mc->jitbuf_high) {

		int drop = min(len / sample_size, (vgsm_chan->pressure_average -
							mc->jitbuf_high));

		vgsm_debug_jitbuf(vgsm_chan->me,
			"TX %d over high-mark: dropped %d samples\n",
			vgsm_chan->pressure_average - mc->jitbuf_high,
			drop);

		len = max(0, len - drop * sample_size);
	}

	if (vgsm_chan->me->debug_frames) {
		ast_verbose(
			"W %7.3f"
			" S%02x F%02x R%02x N%02x"
			" %02x%02x%02x%02x%02x%02x%02x%02x"
			" %3d(%-3d) P%-3d PA%-3d\n",
			(now - vgsm_chan->last_tx) / 1000.0,
			frame->subclass,
			ast_chan->writeformat, ast_chan->rawwriteformat,
			ast_chan->nativeformats,
			*(__u8 *)(frame->data + 0),
			*(__u8 *)(frame->data + 1),
			*(__u8 *)(frame->data + 2),
			*(__u8 *)(frame->data + 3),
			*(__u8 *)(frame->data + 4),
			*(__u8 *)(frame->data + 5),
			*(__u8 *)(frame->data + 6),
			*(__u8 *)(frame->data + 7),
			frame->datalen,
			frame->samples,
			pressure,
			vgsm_chan->pressure_average);
	}

	vgsm_chan->last_tx = now;

	if (write(vgsm_chan->up_fd, buf, len) < 0) {
		ast_log(LOG_ERROR, "write(): %s\n", strerror(errno));
	}

	return 0;
}

static struct ast_channel *vgsm_request(
	const char *type, int format, void *data, int *cause)
{
	int err;

	if (!(format & (AST_FORMAT_ALAW |
			AST_FORMAT_ULAW |
			AST_FORMAT_SLINEAR))) {
		ast_log(LOG_NOTICE,
			"Asked to get a channel of unsupported format '%d'\n",
			format);
		*cause = AST_CAUSE_FAILURE;
		err = -1;
		goto err_unsupported_format;
	}

	// Parse destination and obtain me name + number
	const char *me_name;
	const char *number;
	char *stringp = data;

	me_name = strsep(&stringp, "/");
	if (!me_name) {
		ast_log(LOG_WARNING,
			"Invalid destination '%s' format (me/number)\n",
			(char *)data);

		*cause = AST_CAUSE_FAILURE;
		err = -1;
		goto err_invalid_destination;
	}

	number = strsep(&stringp, "/");
	if (!number) {
		ast_log(LOG_WARNING,
			"Invalid destination '%s' format (me/number)\n",
			(char *)data);

		*cause = AST_CAUSE_FAILURE;
		err = -1;
		goto err_invalid_format;
	}

	struct vgsm_me *me;
	struct vgsm_me *hg_first_me = NULL;
	struct vgsm_huntgroup *huntgroup = NULL;

	if (!strncasecmp(me_name, VGSM_HUNTGROUP_PREFIX,
			strlen(VGSM_HUNTGROUP_PREFIX))) {

		const char *hg_name = me_name +
					strlen(VGSM_HUNTGROUP_PREFIX);
		struct vgsm_huntgroup *hg;
		hg = vgsm_hg_get_by_name(hg_name);
		if (!hg) {
			ast_log(LOG_ERROR, "Cannot find huntgroup '%s'\n",
				hg_name);

			*cause = AST_CAUSE_FAILURE;
			err = -1;
			goto err_huntgroup_not_found;
		}

		me = vgsm_hg_hunt(hg, NULL, NULL);
		if (!me) {
			*cause = AST_CAUSE_CONGESTION;
			err = -1;
			goto err_no_me_available;
		}

		hg_first_me = vgsm_me_get(me);
		huntgroup = vgsm_hg_get(hg);

		vgsm_hg_put(hg);
	} else {
		me = vgsm_me_get_by_name(me_name);
		if (!me) {
			ast_log(LOG_WARNING, "ME %s not found\n",
				me_name);
			*cause = AST_CAUSE_FAILURE;
			err = -1;
			goto err_me_not_found;
		}
	}

	struct vgsm_chan *vgsm_chan;
	vgsm_chan = vgsm_chan_alloc();
	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "Cannot allocate vgsm_chan\n");
		*cause = AST_CAUSE_FAILURE;
		err = -1;
		goto err_vgsm_chan_alloc;
	}

	vgsm_chan->ast_chan = vgsm_ast_chan_alloc(vgsm_chan, AST_STATE_DOWN,
							me, 1, format);
	if (!vgsm_chan->ast_chan) {
		*cause = AST_CAUSE_FAILURE;
		err = -1;
		goto err_vgsm_ast_chan_alloc;
	}

	vgsm_chan->outbound = TRUE;

	struct ast_channel *ast_chan = vgsm_chan->ast_chan;

	snprintf(vgsm_chan->called_number, sizeof(vgsm_chan->called_number),
		"%s", number);

	assert(!vgsm_chan->me);

	vgsm_chan->me = vgsm_me_get(me);
	vgsm_chan->mc = vgsm_me_config_get(me->current_config);
	vgsm_chan->hg_first_me = hg_first_me;
	vgsm_chan->huntgroup = huntgroup;

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
#else
	ast_jb_configure(ast_chan, &vgsm_chan->mc->jbconf);
#endif

	vgsm_me_put(me);

	vgsm_chan_put(vgsm_chan);

	return ast_chan;

	vgsm_chan_put(vgsm_chan->ast_chan->tech_pvt);
	vgsm_chan->ast_chan->tech_pvt = NULL;
	ast_channel_free(vgsm_chan->ast_chan);
	vgsm_chan->ast_chan = NULL;
err_vgsm_ast_chan_alloc:
	vgsm_chan_put(vgsm_chan);
err_vgsm_chan_alloc:
	vgsm_me_put(me);
err_me_not_found:
err_no_me_available:
err_huntgroup_not_found:
err_invalid_format:
err_invalid_destination:
err_unsupported_format:

	return NULL; 
}

static const struct ast_channel_tech vgsm_tech = {
	.type		= VGSM_CHAN_TYPE,
	.description	= VGSM_DESCRIPTION,
	.capabilities	= AST_FORMAT_SLINEAR |
			  AST_FORMAT_ALAW |
			  AST_FORMAT_ULAW,
	.requester	= vgsm_request,
	.call		= vgsm_call,
	.hangup		= vgsm_hangup,
	.answer		= vgsm_answer,
	.read		= vgsm_read,
	.write		= vgsm_write,
	.indicate	= vgsm_indicate,
	.transfer	= vgsm_transfer,
	.fixup		= vgsm_fixup,
	.bridge		= vgsm_bridge,
	.send_text	= vgsm_sendtext,
	.setoption	= vgsm_setoption,

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	.send_digit	= vgsm_send_digit,
#else
	.send_digit_end	= vgsm_send_digit,
#endif
};

BOOL vgsm_cms_error_fatal(int res)
{
	if (res < CMS_ERROR_BASE || res > CMS_ERROR_BASE + CMS_ERROR_SIZE)
		return TRUE;

	res -= CMS_ERROR_BASE;

	switch(res) {
	case 1: return TRUE;
	case 28: return TRUE;
	case 30: return TRUE;
	case 50: return TRUE;
	case 69: return TRUE;
	case 81: return TRUE;
	case 95: return TRUE;
	case 96: return TRUE;
	case 97: return TRUE;
	case 98: return TRUE;
	case 99: return TRUE;
	case 111: return TRUE;
	case 128: return TRUE;
	case 129: return TRUE;
	case 130: return TRUE;
	case 143: return TRUE;
	case 144: return TRUE;
	case 145: return TRUE;
	case 159: return TRUE;
	case 160: return TRUE;
	case 161: return TRUE;
	case 175: return TRUE;
	case 176: return TRUE;
	case 193: return TRUE;
	case 194: return TRUE;
	case 195: return TRUE;
	case 196: return TRUE;
	case 197: return TRUE;
	case 198: return TRUE;
	case 199: return TRUE;
	case 209: return TRUE;
	case 210: return TRUE;
	case 213: return TRUE;
	case 255: return TRUE;
	case 300: return TRUE;
	case 301: return TRUE;
	case 302: return TRUE;
	case 303: return TRUE;
	case 304: return TRUE;
	case 305: return TRUE;
	case 313: return TRUE;
	case 315: return TRUE;
	case 321: return TRUE;
	case 330: return TRUE;
	case 500: return TRUE;
	case 514: return TRUE;
	case 515: return TRUE;
	case 516: return TRUE;
	case 517: return TRUE;
	case 518: return TRUE;
	case 519: return TRUE;
	case 520: return TRUE;
	}

	return FALSE;
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
static void astman_append(struct mansession *s, const char *fmt, ...)
{
	va_list ap;
	char tmpstr[256];

	va_start(ap, fmt);
	vsnprintf(tmpstr, sizeof(tmpstr), fmt, ap);
	va_end(ap);
}
#endif

/***********************************************/

/*! \brief manager_vgsm_sms_tx: Send a text sms with VGSM card ---*/

#if ASTERISK_VERSION_NUM >= 10402
static int manager_vgsm_sms_tx(struct mansession *s, const struct message *m)
#else
static int manager_vgsm_sms_tx(struct mansession *s, struct message *m)
#endif
{
	int err;

	const char *number = astman_get_header(m, "To");
	if (!strlen(number)) {
		astman_append(s, "Status: 510\n");
		astman_send_error(s, m, "To: header missing");
		goto err_missing_destination;
	}

	char *content = strdup(astman_get_header(m, "Content"));
	if (!strlen(content)) {
		astman_append(s, "Status: 509\n");
		astman_send_error(s, m, "Content: header missing");
		goto err_missing_content;
	}

	int i = 2;
	while(TRUE) {
		char tmpstr[10];
		snprintf(tmpstr, sizeof(tmpstr), "Content%d", i);
		const char *content_n = astman_get_header(m, tmpstr);

		if (!strlen(content_n))
			break;

		content = realloc(content, strlen(content) +
					strlen(content_n) + 1);
		if (!content) {
			astman_append(s, "Status: 503\n");
			astman_send_error(s, m, "Cannot realloc");
			goto err_missing_content;
		}

		strcat(content, content_n);

		i++;
	}

	const char *content_type = astman_get_header(m, "Content-type");
	char *charset = "utf-8";

	if (strlen(content_type)) {
		char *content_type_a = strdupa(content_type);
		char *lasts;
		char *type = strtok_r(content_type_a, " \t;", &lasts);
		if (!type) {
			astman_append(s, "Status: 504\n");
			astman_send_error(s, m, "Invalid content-type");
			goto err_invalid_content_type;
		}

		if (strcasecmp(type, "text/plain")) {
			astman_append(s, "Status: 505\n");
			astman_send_error(s, m, "Unsupported content-type");
			goto err_unsupported_content_type;
		
		}

		char *par;
		while((par = strtok_r(NULL, " ;", &lasts))) {
			if (!strncasecmp(par, "charset=", strlen("charset="))) {
				charset = par + strlen("charset=");
				break;
			}
		}
	}

	iconv_t cd;
	cd = iconv_open("WCHAR_T", charset);
	if (cd < 0) {
		astman_append(s, "Status: 503\n");

		char tmpstr[64];
		snprintf(tmpstr, sizeof(tmpstr),
			"Cannot open iconv context; %s\n",
			strerror(errno));

		astman_send_error(s, m, tmpstr);

		goto err_iconv_open;
	}

	const char *me_str = astman_get_header(m, "X-SMS-ME");
	struct vgsm_me *me = NULL;

	if (!strncasecmp(me_str, VGSM_HUNTGROUP_PREFIX,
			strlen(VGSM_HUNTGROUP_PREFIX))) {

		const char *hg_name = me_str +
					strlen(VGSM_HUNTGROUP_PREFIX);
		struct vgsm_huntgroup *hg;
		hg = vgsm_hg_get_by_name(hg_name);
		if (!hg) {
			astman_append(s, "Status: 508\n");
			astman_send_error(s, m, "Cannot find huntgroup");

			goto err_huntgroup_not_found;
		}

		ast_rwlock_rdlock(&vgsm.huntgroups_list_lock);
		struct vgsm_huntgroup_member *hgm;
		list_for_each_entry(hgm, &hg->members, node) {
			ast_mutex_lock(&hgm->me->lock);
			if (hgm->me->status == VGSM_ME_STATUS_READY &&
				!hgm->me->sending_sms &&
				(hgm->me->net.status ==
					VGSM_NET_STATUS_REGISTERED_HOME ||
				hgm->me->net.status ==
					VGSM_NET_STATUS_REGISTERED_ROAMING)) {

				me = vgsm_me_get(hgm->me);

				ast_mutex_unlock(&hgm->me->lock);
				break;
			}
			ast_mutex_unlock(&hgm->me->lock);
		}
		ast_rwlock_unlock(&vgsm.huntgroups_list_lock);

		vgsm_hg_put(hg);

		if (!me) {
			astman_append(s, "Status: 404\n");
			astman_send_error(s, m,
				"Cannot find an available me");
			goto err_me_not_found;
		}

	} else if(strlen(me_str)) {
		/* A specific me has been requested */

		me = vgsm_me_get_by_name(me_str);
		if (!me) {
			astman_append(s, "Status: 501\n");
			astman_send_error(s, m, "501 Cannot find me");
			goto err_me_not_found;
		}

	} else {
		/* Find a ready me */

		struct vgsm_me *tm;

		ast_rwlock_rdlock(&vgsm.mes_list_lock);
		list_for_each_entry(tm, &vgsm.mes_list, node) {
			ast_mutex_lock(&tm->lock);
			if (tm->status == VGSM_ME_STATUS_READY &&
				!tm->sending_sms &&
				(tm->net.status ==
					VGSM_NET_STATUS_REGISTERED_HOME ||
				tm->net.status ==
					VGSM_NET_STATUS_REGISTERED_ROAMING)) {

				me = vgsm_me_get(tm);

				ast_mutex_unlock(&tm->lock);
				break;
			}
			ast_mutex_unlock(&tm->lock);
		}
		ast_rwlock_unlock(&vgsm.mes_list_lock);

		if (!me) {
			astman_append(s, "Status: 404\n");
			astman_send_error(s, m,
				"Cannot find an available ME");
			goto err_me_not_found;
		}
	}

	ast_mutex_lock(&me->lock);

	if (me->status != VGSM_ME_STATUS_READY) {
		ast_mutex_unlock(&me->lock);

		astman_append(s, "Status: 401\n");
		astman_send_error(s, m, "ME is not ready");
		goto err_me_not_ready;
	}

	if (me->net.status != VGSM_NET_STATUS_REGISTERED_HOME &&
	    me->net.status != VGSM_NET_STATUS_REGISTERED_ROAMING) {
		ast_mutex_unlock(&me->lock);

		astman_append(s, "Status: 402\n");
		astman_send_error(s, m, "ME is not registered");
		goto err_me_not_registered;
	}

	if (me->sending_sms) {
		ast_mutex_unlock(&me->lock);

		astman_append(s, "Status: 403\n");
		astman_send_error(s, m,
			"403 ME is already sending a message");
		goto err_me_sending_sms;
	}

	struct vgsm_sms_submit *sms;
	sms = vgsm_sms_submit_alloc();
	if (!sms) {
		ast_mutex_unlock(&me->lock);

		astman_append(s, "Status: 405\n");
		astman_send_error(s, m, "Cannot allocate message");
		goto err_sms_alloc;
	}

	me->sending_sms = TRUE;
	sms->me = vgsm_me_get(me);

	const char *smcc_str = astman_get_header(m, "X-SMS-SMCC-Number");
	if (strlen(smcc_str)) {
		vgsm_number_parse(&sms->smcc_address, smcc_str);
	} else if (strlen(me->current_config->smcc_address.digits)) {
		vgsm_number_copy(&sms->smcc_address,
				&me->current_config->smcc_address);
	} else if (strlen(me->sim.smcc_address.digits)) {
		vgsm_number_copy(&sms->smcc_address, &me->sim.smcc_address);
	} else {
		ast_mutex_unlock(&me->lock);

		astman_append(s, "Status: 502\n");
		astman_send_error(s, m, "Services Center number not set");
		goto err_no_smcc;
	}

	ast_mutex_unlock(&me->lock);

	const char *reject_duplicates_str =
		astman_get_header(m, "X-SMS-Reject-Duplicates");
	if (strlen(reject_duplicates_str))
		sms->reject_duplicates = ast_true(reject_duplicates_str);

	const char *reply_path_str =
		astman_get_header(m, "X-SMS-Reply-Path");
	if (strlen(reply_path_str))
		sms->reply_path = ast_true(reply_path_str);

	const char *status_report_request_str =
		astman_get_header(m, "X-SMS-Status-Report-Request");
	if (strlen(status_report_request_str))
		sms->status_report_request =
			ast_true(status_report_request_str);

	const char *message_reference_str =
		astman_get_header(m, "X-SMS-Message-Reference");
	if (strlen(message_reference_str))
		sms->message_reference = atoi(message_reference_str);

	const char *validity_period_str =
		astman_get_header(m, "X-SMS-Validity-Period");
	if (strlen(validity_period_str))
		sms->validity_period = atoi(validity_period_str);

	const char *class_str = astman_get_header(m, "X-SMS-Class");
	if (strlen(class_str))
		sms->message_class = atoi(class_str);
	else
		sms->message_class = 1;

	const char *concatenate_refid_str =
		astman_get_header(m, "X-SMS-Concatenate-RefID");
	if (strlen(concatenate_refid_str))
		sms->udh_concatenate_refnum = atoi(concatenate_refid_str);

	const char *concatenate_maxmsg_str =
		astman_get_header(m, "X-SMS-Concatenate-Total-Messages");
	if (strlen(concatenate_maxmsg_str))
		sms->udh_concatenate_maxmsg = atoi(concatenate_maxmsg_str);

	const char *concatenate_seqnum_str =
		astman_get_header(m, "X-SMS-Concatenate-Sequence-Number");
	if (strlen(concatenate_seqnum_str))
		sms->udh_concatenate_seqnum = atoi(concatenate_seqnum_str);

	vgsm_number_parse(&sms->dest, number);

	const char *content_te =
		astman_get_header(m, "Content-Transfer-Encoding");
	size_t content_size;
	__u8 *content_raw;

	if (!strlen(content_te)) {
		content_size = strlen(content);
		content_raw = (__u8 *)content; // Okay to drop const

	} else if(!strcasecmp(content_te, "hex")) {
		content_size = strlen(content) / 2;
		content_raw = alloca(content_size);

		int i;
		for(i=0; i<content_size; i++) {
			content_raw[i] =
				char_to_hexdigit(content[i * 2]) << 4 |
				char_to_hexdigit(content[i * 2 + 1]);
		}
	} else if (!strcasecmp(content_te, "base64")) {
		content_size = strlen(content);
		content_raw = alloca(content_size);

		base64_decode(content, content_raw, content_size);

	} else if (!strcasecmp(content_te, "quoted-printable")) {
		content_size = strlen(content);
		content_raw = alloca(content_size);

		quoted_printable_decode(content, content_raw, content_size);

	} else {
		astman_append(s, "Status: 506\n");
		astman_send_error(s, m,
			"Unsupported Content-Transfer-Encoding");
		goto err_unsupported_cte;
	}

	char *inbuf = (char *)content_raw;
	size_t inbytes = content_size;
	size_t outbytes_avail = (content_size + 1) * sizeof(wchar_t);
	size_t outbytes_left = outbytes_avail;

	sms->text = malloc(outbytes_avail);
	if (!sms->text) {
		astman_append(s, "Status: 406\n");
		astman_send_error(s, m, "Out of memory");
		goto err_text_malloc;
	}

	char *outbuf = (char *)sms->text;
	if (iconv(cd, &inbuf, &inbytes, &outbuf, &outbytes_left) < 0) {
		astman_append(s, "Status: 507\n");

		char tmpstr[64];
		snprintf(tmpstr, sizeof(tmpstr),
			"Charset conversion error: %s",
			strerror(errno));

		astman_send_error(s, m, tmpstr);
		goto err_iconv;
	}

	*((wchar_t *)outbuf) = L'\0';

	err = vgsm_sms_submit_prepare(sms);
	if (err == -ENOSPC) {
		astman_append(s, "Status: 511\n");
		astman_send_error(s, m, "Message too big");
		goto err_submit_prepare;
	} else if (err < 0) {
		astman_append(s, "Status: 512\n");
		astman_send_error(s, m,
			"Unspecified message preparation error");
		goto err_submit_prepare;
	}

	if (me->debug_sms)
		vgsm_sms_submit_dump(sms);

	struct vgsm_req *req = vgsm_req_make_sms(
		&me->comm, 30 * SEC, sms->pdu, sms->pdu_len,
		"AT+CMGS=%d", sms->pdu_tp_len);

	vgsm_req_wait(req);

	int res = vgsm_req_status(req);
	if (res != VGSM_RESP_OK) {
		astman_append(s, "Status: %c00\n",
			vgsm_cms_error_fatal(res) ? '5' : '4');

		char tmpstr[256];
		snprintf(tmpstr, sizeof(tmpstr),
			"%s\n",
			vgsm_me_error_to_text(res));

		astman_send_error(s, m, tmpstr);

		goto err_make_req;
	}

	const char *pars = vgsm_req_first_line(req)->text + strlen("+CMGS: ");
	const char *pars_ptr = pars;
	char field[32];

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_WARNING, "Cannot parse +CMGS: '%s'\n", pars);
	} else {
		astman_append(s, "X-SMS-Reference: %u\n", atoi(field));
	}

	vgsm_req_put(req);

	astman_append(s, "Status: 201\n");
	astman_send_ack(s, m, "Message sent");

	ast_mutex_lock(&me->lock);
	me->sending_sms = FALSE;
	ast_mutex_unlock(&me->lock);

	iconv_close(cd);

	free(content);

	return 0;

err_make_req:
err_submit_prepare:
err_iconv:
	free(sms->text);
	sms->text = NULL;
err_text_malloc:
err_unsupported_cte:
err_no_smcc:
	vgsm_sms_submit_put(sms);
err_sms_alloc:
	ast_mutex_lock(&me->lock);
	me->sending_sms = FALSE;
	ast_mutex_unlock(&me->lock);
err_me_sending_sms:
err_me_not_registered:
err_me_not_ready:
err_me_not_found:
err_huntgroup_not_found:
	iconv_close(cd);
err_iconv_open:
err_unsupported_content_type:
err_invalid_content_type:
	free(content);
err_missing_content:
err_missing_destination:

	return 0;
}

/***********************************************/

static char mandescr_vgsm_sms_tx[] =
"Description: Send and SMS in text format with VGSM channel driver.\n"
"Variables: \n"
"  Content: <text>      Text of the message, max 160 chars (will be cut if longer).\n"
"  ActionID: <id>       Action ID for this transaction. Will be returned.\n";

static void vgsm_shutdown(void)
{
	vgsm_me_shutdown_all();
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
int load_module(void)
#else
static int vgsm_load_module(void)
#endif
{
	int err;

	// Initialize q.931 library.
	// No worries, internal structures are read-only and thread safe
	ast_mutex_init(&vgsm.state_lock);

	ast_mutex_init(&vgsm.usecnt_lock);

	INIT_LIST_HEAD(&vgsm.mes_list);
	ast_rwlock_init(&vgsm.mes_list_lock);

	ast_rwlock_init(&vgsm.operators_lock);
	INIT_LIST_HEAD(&vgsm.op_list);
	INIT_LIST_HEAD(&vgsm.op_countries_list);

	ast_rwlock_init(&vgsm.huntgroups_list_lock);
	INIT_LIST_HEAD(&vgsm.huntgroups_list);

	vgsm.default_mc = vgsm_me_config_alloc();
	vgsm_me_config_default(vgsm.default_mc);

	strcpy(vgsm.sms_spooler, "/usr/sbin/sendmail");
	strcpy(vgsm.sms_spooler_pars, "-it");

	vgsm_reload_config();

	if (ast_channel_register(&vgsm_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n",
			VGSM_CHAN_TYPE);
		err = -1;
		goto err_channel_register;
	}

	err = vgsm_me_load();
	if (err < 0)
		goto err_me_load;

	err = vgsm_hg_load();
	if (err < 0)
		goto err_hg_load;

#ifdef DEBUG_CODE
	ast_cli_register(&vgsm_debug_timer);
	ast_cli_register(&vgsm_no_debug_timer);
#endif

	ast_cli_register(&vgsm_reload);

	/* Register manager commands */
	ast_manager_register2("VGSMsmstx", EVENT_FLAG_CALL,
			manager_vgsm_sms_tx,
			"Send sms with vGSM (obsolete)",
			mandescr_vgsm_sms_tx);

	ast_manager_register2("vgsm_sms_tx", EVENT_FLAG_CALL,
			manager_vgsm_sms_tx,
			"Send sms with vGSM",
			mandescr_vgsm_sms_tx);

	ast_register_atexit(vgsm_shutdown);

	return 0;

	vgsm_hg_unload();
err_hg_load:
	vgsm_me_unload();
err_me_load:
	ast_channel_unregister(&vgsm_tech);
err_channel_register:

	ast_cli_unregister(&vgsm_reload);

#ifdef DEBUG_CODE
	ast_cli_unregister(&vgsm_no_debug_timer);
	ast_cli_unregister(&vgsm_debug_timer);
#endif

	vgsm_me_config_put(vgsm.default_mc);

	return err;
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
int unload_module(void)
#else
static int vgsm_unload_module(void)
#endif
{

	vgsm_hg_unload();
	vgsm_me_unload();

	ast_cli_unregister(&vgsm_reload);

#ifdef DEBUG_CODE
	ast_cli_unregister(&vgsm_no_debug_timer);
	ast_cli_unregister(&vgsm_debug_timer);
#endif

	ast_channel_unregister(&vgsm_tech);

	vgsm_me_config_put(vgsm.default_mc);

	return 0;
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
int reload(void)
#else
static int vgsm_reload_module(void)
#endif
{
	return vgsm_reload_config();
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)

int usecount(void)
{
	int res;
	ast_mutex_lock(&vgsm.usecnt_lock);
	res = vgsm.usecnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);
	return res;
}

char *description(void)
{
	return VGSM_DESCRIPTION;
}

char *key(void)
{
	return ASTERISK_GPL_KEY;
}

#else

AST_MODULE_INFO(ASTERISK_GPL_KEY,
		AST_MODFLAG_GLOBAL_SYMBOLS,
		"vGSM-II driver",
		.load = vgsm_load_module,
		.unload = vgsm_unload_module,
		.reload = vgsm_reload_module,
	);
#endif
