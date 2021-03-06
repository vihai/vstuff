/*
 * vISDN channel driver for Asterisk
 *
 * Copyright (C) 2004-2008 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
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

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <net/if_arp.h>

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
#include <asterisk/causes.h>
#include <asterisk/dsp.h>
#include <asterisk/version.h>

#include <linux/lapd.h>
#include <linux/kstreamer/userport.h>

#include <linux/kstreamer/hdlc_framer.h>
#include <linux/kstreamer/octet_reverser.h>

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
#endif

#include <res_kstreamer.h>

#include <libq931/lib.h>
#include <libq931/dlc.h>
#include <libq931/list.h>
#include <libq931/logging.h>
#include <libq931/call.h>
#include <libq931/intf.h>
#include <libq931/ces.h>
#include <libq931/ccb.h>
#include <libq931/input.h>

#include <libq931/ie.h>
#include <libq931/ie_bearer_capability.h>
#include <libq931/ie_call_state.h>
#include <libq931/ie_cause.h>
#include <libq931/ie_called_party_number.h>
#include <libq931/ie_calling_party_number.h>
#include <libq931/ie_channel_identification.h>
#include <libq931/ie_call_identity.h>
#include <libq931/ie_display.h>
#include <libq931/ie_low_layer_compatibility.h>
#include <libq931/ie_high_layer_compatibility.h>
#include <libq931/ie_notification_indicator.h>
#include <libq931/ie_progress_indicator.h>
#include <libq931/ie_restart_indicator.h>
#include <libq931/ie_sending_complete.h>

#include "chan_visdn.h"
#include "util.h"
#include "huntgroup.h"
#include "overlap.h"
#include "disconnect.h"
#include "numbers_list.h"
#include "debug.h"

#ifdef DEBUG_CODE
#define visdn_debug_netlink(format, arg...)				\
	if (visdn.debug_netlink)					\
		visdn_debug(						\
			format,						\
			## arg)

#define visdn_debug_q931(format, arg...)				\
	if (visdn.debug_q931)						\
		visdn_debug(						\
			format,						\
			## arg)

#define visdn_chan_debug(chan, format, arg...)				\
	if((chan)->debug_generic)					\
		visdn_debug("%s: "					\
			format,						\
			(chan)->ast_chan->name,				\
			## arg)

#define visdn_chan_debug_frames(chan, format, arg...)			\
	if((chan)->debug_frames)					\
		visdn_debug("%s: "					\
			format,						\
			(chan)->ast_chan->name,				\
			## arg)

#define visdn_chan_debug_jitbuf(chan, format, arg...)			\
	if((chan)->debug_jitbuf)					\
		visdn_debug("%s: "					\
			format,						\
			(chan)->ast_chan->name,				\
			## arg)

#else
#define visdn_chan_debug_netlink(format, arg...)	\
	do {} while(0)

#define visdn_chan_debug_q931(format, arg...)		\
	do {} while(0)

#define visdn_chan_debug(chan, format, arg...)	\
	do {} while(0)

#define visdn_chan_debug_frames(chan, format, arg...)	\
	do {} while(0)

#define visdn_chan_debug_jitbuf(chan, format, arg...)	\
	do {} while(0)
#endif

struct visdn_state visdn = {
	.usecnt = 0,

	.q931_thread = AST_PTHREADT_NULL,

#ifdef DEBUG_CODE
#ifdef DEBUG_DEFAULTS
	.debug_netlink = TRUE,
	.debug_q921 = FALSE,
	.debug_q931 = TRUE,
#else
	.debug_netlink = FALSE,
	.debug_q921 = FALSE,
	.debug_q931 = FALSE,
#endif
#endif
};

static struct ast_channel *visdn_ast_chan_alloc(
	struct visdn_chan *visdn_chan,
	int state,
	struct q931_call *call);

static struct visdn_chan *visdn_chan_alloc(void)
{
	struct visdn_chan *visdn_chan;

	visdn_chan = malloc(sizeof(*visdn_chan));
	if (!visdn_chan)
		goto err_malloc_visdn_chan;

	memset(visdn_chan, 0, sizeof(*visdn_chan));

	visdn_chan->refcnt = 1;
	ast_cond_init(&visdn_chan->refcnt_decremented_cond, NULL);

	visdn_chan->up_fd = -1;

	visdn_chan->dsp = ast_dsp_new();
	if (!visdn_chan->dsp)
		goto err_dsp_new;

	ast_dsp_set_features(visdn_chan->dsp, DSP_FEATURE_DTMF_DETECT);

	ast_mutex_lock(&visdn.usecnt_lock);
	visdn.usecnt++;
	ast_mutex_unlock(&visdn.usecnt_lock);
	ast_update_use_count();

	return visdn_chan;

	ast_dsp_free(visdn_chan->dsp);
	visdn_chan->dsp = NULL;
err_dsp_new:
	free(visdn_chan);
err_malloc_visdn_chan:

	return NULL;
}

struct visdn_chan *visdn_chan_get(struct visdn_chan *visdn_chan)
{
	if (!visdn_chan)
		return NULL;

	assert(visdn_chan->refcnt > 0);
	assert(visdn_chan->refcnt < 100000);

	ast_mutex_lock(&visdn.usecnt_lock);
	visdn_chan->refcnt++;
	ast_mutex_unlock(&visdn.usecnt_lock);

	return visdn_chan;
}

void _visdn_chan_put(struct visdn_chan *visdn_chan)
{
	if (!visdn_chan)
		return;

	assert(visdn_chan->refcnt > 0);
	assert(visdn_chan->refcnt < 100000);

	ast_mutex_lock(&visdn.usecnt_lock);
	int refcnt = --visdn_chan->refcnt;
	ast_mutex_unlock(&visdn.usecnt_lock);

	ast_cond_broadcast(&visdn_chan->refcnt_decremented_cond);

	if (!refcnt) {
		assert(!visdn_chan->ast_chan);

		if (visdn_chan->dsp) {
			ast_dsp_free(visdn_chan->dsp);
			visdn_chan->dsp = NULL;
		}

		if (visdn_chan->huntgroup) {
			visdn_hg_put(visdn_chan->huntgroup);
			visdn_chan->huntgroup = NULL;
		}

		if (visdn_chan->hg_first_intf) {
			visdn_intf_put(visdn_chan->hg_first_intf);
			visdn_chan->hg_first_intf = NULL;
		}

		if (visdn_chan->ic) {
			visdn_ic_put(visdn_chan->ic);
			visdn_chan->ic = NULL;
		}

		if (visdn_chan->bc) {
			_q931_ie_put(&visdn_chan->bc->ie);
			visdn_chan->bc = NULL;
		}

		free(visdn_chan);

		ast_mutex_lock(&visdn.usecnt_lock);
		visdn.usecnt--;
		ast_mutex_unlock(&visdn.usecnt_lock);
		ast_update_use_count();
	}
}

static const struct ast_channel_tech visdn_tech;

static struct ast_channel *visdn_ast_chan_alloc(
	struct visdn_chan *visdn_chan,
	int state,
	struct q931_call *call)
{
	struct ast_channel *ast_chan;

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	ast_chan = ast_channel_alloc(1);
	if (!ast_chan) {
		ast_log(LOG_WARNING, "Unable to allocate channel\n");
		goto err_channel_alloc;
	}

	ast_setstate(ast_chan, state);

	if (call) {
		snprintf(ast_chan->name, sizeof(ast_chan->name),
			"VISDN/%s/%d.%c",
			call->intf->name,
			call->call_reference,
			call->direction == Q931_CALL_DIRECTION_INBOUND ?
					'I' : 'O');
	} else {
		snprintf(ast_chan->name, sizeof(ast_chan->name),
			"VISDN/null");
	}

#elif ASTERISK_VERSION_NUM <= 10402
	if (call) {
		ast_chan = ast_channel_alloc(TRUE, state, NULL, NULL,
			"VISDN/%s/%d.%c",
			call->intf->name,
			call->call_reference,
			call->direction == Q931_CALL_DIRECTION_INBOUND ?
					'I' : 'O');
	} else {
		ast_chan = ast_channel_alloc(TRUE, state, NULL, NULL,
							"VISDN/null");
	}

	if (!ast_chan) {
		ast_log(LOG_WARNING, "Unable to allocate channel\n");
		goto err_channel_alloc;
	}

#else
	if (call) {
		ast_chan = ast_channel_alloc(TRUE, state, NULL, NULL,
			NULL, NULL, NULL, 0,
			"VISDN/%s/%d.%c",
			call->intf->name,
			call->call_reference,
			call->direction == Q931_CALL_DIRECTION_INBOUND ?
					'I' : 'O');
	} else {
		ast_chan = ast_channel_alloc(TRUE, state, NULL, NULL,
					NULL, NULL, NULL, 0, "VISDN/null");
	}

	if (!ast_chan) {
		ast_log(LOG_WARNING, "Unable to allocate channel\n");
		goto err_channel_alloc;
	}
#endif

	/* Reference is not taken, as ast_chan is an embedded object */
	ast_chan->tech_pvt = visdn_chan_get(visdn_chan);
	ast_chan->tech = &visdn_tech;

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	ast_chan->type = VISDN_CHAN_TYPE;
#endif

	ast_chan->fds[0] = -1;

	ast_chan->adsicpe = AST_ADSI_UNAVAILABLE;

	ast_chan->nativeformats = AST_FORMAT_ALAW;
	ast_chan->readformat = AST_FORMAT_ALAW;
	ast_chan->rawreadformat = AST_FORMAT_ALAW;
	ast_chan->writeformat = AST_FORMAT_ALAW;
	ast_chan->rawwriteformat = AST_FORMAT_ALAW;

	return ast_chan;

	visdn_chan_put(ast_chan->tech_pvt);
	ast_chan->tech_pvt = NULL;
	ast_channel_free(ast_chan);
err_channel_alloc:

	return NULL;
}

static void visdn_set_socket_debug(int on)
{
	struct visdn_intf *intf;
	list_for_each_entry(intf, &visdn.intfs_list, node) {
		if (!intf->q931_intf)
			continue;

		if (intf->q931_intf->accept_socket >= 0) {
			setsockopt(intf->q931_intf->accept_socket,
				SOL_SOCKET, SO_DEBUG,
				&on, sizeof(on));
		}

		if (intf->q931_intf->dlc.socket >= 0) {
			setsockopt(intf->q931_intf->dlc.socket,
				SOL_SOCKET, SO_DEBUG,
				&on, sizeof(on));
		}

		if (intf->q931_intf->bc_dlc.socket >= 0) {
			setsockopt(intf->q931_intf->bc_dlc.socket,
				SOL_SOCKET, SO_DEBUG,
				&on, sizeof(on));
		}

		struct q931_dlc *dlc;
		list_for_each_entry(dlc, &intf->q931_intf->dlcs,
							intf_node) {
			setsockopt(dlc->socket,
				SOL_SOCKET, SO_DEBUG,
				&on, sizeof(on));
		}
	}

}

void refresh_polls_list(void)
{
	visdn.npolls = 0;

	visdn.polls[visdn.npolls].fd = visdn.q931_ccb_queue_pipe_read;
	visdn.polls[visdn.npolls].events = POLLIN | POLLERR;
	visdn.poll_infos[visdn.npolls].type = POLL_INFO_TYPE_Q931_CCB;
	visdn.npolls++;

	visdn.polls[visdn.npolls].fd = visdn.ccb_q931_queue_pipe_read;
	visdn.polls[visdn.npolls].events = POLLIN | POLLERR;
	visdn.poll_infos[visdn.npolls].type = POLL_INFO_TYPE_CCB_Q931;
	visdn.npolls++;

	visdn.polls[visdn.npolls].fd = visdn.netlink_socket;
	visdn.polls[visdn.npolls].events = POLLIN | POLLERR;
	visdn.poll_infos[visdn.npolls].type = POLL_INFO_TYPE_NETLINK;
	visdn.npolls++;

	struct visdn_intf *intf;
	ast_rwlock_rdlock(&visdn.intfs_list_lock);
	list_for_each_entry(intf, &visdn.intfs_list, node) {

		if (intf->status == VISDN_INTF_STATUS_FAILED)
			continue;

		if (!intf->q931_intf)
			continue;

		if (intf->mgmt_fd >= 0) {
			visdn.polls[visdn.npolls].fd = intf->mgmt_fd;
			visdn.polls[visdn.npolls].events = POLLIN | POLLERR;
			visdn.poll_infos[visdn.npolls].type =
					POLL_INFO_TYPE_MGMT;
			visdn.poll_infos[visdn.npolls].intf = intf;
			visdn.npolls++;
		}

		if (intf->q931_intf->accept_socket >= 0) {
			visdn.polls[visdn.npolls].fd =
					intf->q931_intf->accept_socket;
			visdn.polls[visdn.npolls].events =
					POLLIN | POLLERR;
			visdn.poll_infos[visdn.npolls].type =
					POLL_INFO_TYPE_ACCEPT;
			visdn.poll_infos[visdn.npolls].intf = intf;
			visdn.npolls++;
		}

		if (intf->q931_intf->dlc.socket >= 0) {
			visdn.polls[visdn.npolls].fd =
					intf->q931_intf->dlc.socket;
			visdn.polls[visdn.npolls].events =
					POLLIN | POLLERR;
			visdn.poll_infos[visdn.npolls].type =
					POLL_INFO_TYPE_DLC;
			visdn.poll_infos[visdn.npolls].dlc =
					&intf->q931_intf->dlc;
			visdn.poll_infos[visdn.npolls].intf = intf;
			visdn.npolls++;
		}

		if (intf->q931_intf->bc_dlc.socket >= 0) {
			visdn.polls[visdn.npolls].fd =
					intf->q931_intf->bc_dlc.socket;
			visdn.polls[visdn.npolls].events =
					POLLIN | POLLERR;
			visdn.poll_infos[visdn.npolls].type =
					POLL_INFO_TYPE_BC_DLC;
			visdn.poll_infos[visdn.npolls].dlc =
					&intf->q931_intf->bc_dlc;
			visdn.poll_infos[visdn.npolls].intf = intf;
			visdn.npolls++;
		}

		struct q931_dlc *dlc;
		list_for_each_entry(dlc, &intf->q931_intf->dlcs, intf_node) {
			visdn.polls[visdn.npolls].fd = dlc->socket;
			visdn.polls[visdn.npolls].events = POLLIN | POLLERR;
			visdn.poll_infos[visdn.npolls].type =
							POLL_INFO_TYPE_DLC;
			visdn.poll_infos[visdn.npolls].dlc = dlc;
			visdn.poll_infos[visdn.npolls].intf = intf;
			visdn.npolls++;
		}
	}
	ast_rwlock_unlock(&visdn.intfs_list_lock);
}

static void visdn_accept(
	struct q931_interface *intf,
	int accept_socket)
{
	struct q931_dlc *newdlc;

	newdlc = q931_accept(intf, accept_socket);
	if (!newdlc)
		return;

	visdn_intf_debug_state((struct visdn_intf *)intf->pvt,
			"New DLC (TEI=%d) accepted on interface %s\n",
			newdlc->tei,
			intf->name);

	refresh_polls_list();
}

static void visdn_reload_config(void)
{
	struct ast_config *cfg;
	cfg = ast_config_load(VISDN_CONFIG_FILE);
	if (!cfg) {
		ast_log(LOG_WARNING,
			"Unable to load config %s, VISDN disabled\n",
			VISDN_CONFIG_FILE);

		return;
	}

	visdn_intf_reload(cfg);
	visdn_hg_reload(cfg);

	ast_config_destroy(cfg);
}

static enum q931_ie_called_party_number_type_of_number
	visdn_type_of_number_to_cdpn(enum visdn_type_of_number type_of_number)
{
	switch(type_of_number) {
	case VISDN_TYPE_OF_NUMBER_UNSET:
		assert(0);
	case VISDN_TYPE_OF_NUMBER_UNKNOWN:
		return Q931_IE_CDPN_TON_UNKNOWN;
	case VISDN_TYPE_OF_NUMBER_INTERNATIONAL:
		return Q931_IE_CDPN_TON_INTERNATIONAL;
	case VISDN_TYPE_OF_NUMBER_NATIONAL:
		return Q931_IE_CDPN_TON_NATIONAL;
	case VISDN_TYPE_OF_NUMBER_NETWORK_SPECIFIC:
		return Q931_IE_CDPN_TON_NETWORK_SPECIFIC;
	case VISDN_TYPE_OF_NUMBER_SUBSCRIBER:
		return Q931_IE_CDPN_TON_SUBSCRIBER;
	case VISDN_TYPE_OF_NUMBER_ABBREVIATED:
		return Q931_IE_CDPN_TON_ABBREVIATED;
	}

	assert(0);
}

static enum q931_ie_calling_party_number_type_of_number
	visdn_type_of_number_to_cgpn(enum visdn_type_of_number type_of_number)
{
	switch(type_of_number) {
	case VISDN_TYPE_OF_NUMBER_UNSET:
		assert(0);
	case VISDN_TYPE_OF_NUMBER_UNKNOWN:
		return Q931_IE_CGPN_TON_UNKNOWN;
	case VISDN_TYPE_OF_NUMBER_INTERNATIONAL:
		return Q931_IE_CDPN_TON_INTERNATIONAL;
	case VISDN_TYPE_OF_NUMBER_NATIONAL:
		return Q931_IE_CGPN_TON_NATIONAL;
	case VISDN_TYPE_OF_NUMBER_NETWORK_SPECIFIC:
		return Q931_IE_CGPN_TON_NETWORK_SPECIFIC;
	case VISDN_TYPE_OF_NUMBER_SUBSCRIBER:
		return Q931_IE_CGPN_TON_SUBSCRIBER;
	case VISDN_TYPE_OF_NUMBER_ABBREVIATED:
		return Q931_IE_CGPN_TON_ABBREVIATED;
	}

	assert(0);
	return 0;
}

void q931_send_primitive(
	struct q931_call *call,
	enum q931_primitive primitive,
	struct q931_ies *ies)
{
	struct q931_ccb_message *msg;
	msg = malloc(sizeof(*msg));
	if (!msg)
		return;

	memset(msg, 0, sizeof(*msg));

	if (call)
		msg->call = q931_call_get(call);

	msg->primitive = primitive;

	q931_ies_init(&msg->ies);

	if (ies)
		q931_ies_copy(&msg->ies, ies);

	ast_mutex_lock(&visdn.ccb_q931_queue_lock);
	list_add_tail(&msg->node, &visdn.ccb_q931_queue);
	ast_mutex_unlock(&visdn.ccb_q931_queue_lock);

	if (write(visdn.ccb_q931_queue_pipe_write, " ", 1) < 0) {
		ast_log(LOG_WARNING,
			"Cannot write on ccb_q931_pipe_write\n");
	}
}

void visdn_queue_primitive(
	struct q931_call *call,
	enum q931_primitive primitive,
	const struct q931_ies *ies,
	unsigned long par1,
	unsigned long par2)
{
	struct q931_ccb_message *msg;
	msg = malloc(sizeof(*msg));
	if (!msg)
		return;

	memset(msg, 0, sizeof(*msg));

	if (call)
		msg->call = q931_call_get(call);

	msg->primitive = primitive;
	msg->par1 = par1;
	msg->par2 = par2;

	q931_ies_init(&msg->ies);

	if (ies)
		q931_ies_copy(&msg->ies, ies);

	ast_mutex_lock(&visdn.q931_ccb_queue_lock);
	list_add_tail(&msg->node, &visdn.q931_ccb_queue);
	ast_mutex_unlock(&visdn.q931_ccb_queue_lock);

	if (write(visdn.q931_ccb_queue_pipe_write, " ", 1) < 0) {
		ast_log(LOG_WARNING,
			"Cannot write on q931_ccb_pipe_write\n");
	}
}

static int visdn_q931_is_number_complete(
	struct q931_call *q931_call)
{
	struct visdn_chan *visdn_chan = q931_call->pvt;

	if (!visdn_chan)
		return FALSE;

	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	ast_queue_control(ast_chan, AST_CONTROL_ANSWER);

	return ast_exists_extension(NULL,
			ast_chan->context,
			visdn_chan->number, 1,
			ast_chan->cid.cid_num);
}

static void visdn_pres_to_pi_si(
	int pres,
	enum q931_ie_calling_party_number_presentation_indicator *pi,
	enum q931_ie_calling_party_number_screening_indicator *si)
{
	switch(pres) {
	case AST_PRES_ALLOWED_USER_NUMBER_NOT_SCREENED:
		*pi = Q931_IE_CGPN_PI_PRESENTATION_ALLOWED;
		*si = Q931_IE_CGPN_SI_USER_PROVIDED_NOT_SCREENED;
	break;

	case AST_PRES_ALLOWED_USER_NUMBER_PASSED_SCREEN:
		*pi = Q931_IE_CGPN_PI_PRESENTATION_ALLOWED;
		*si = Q931_IE_CGPN_SI_USER_PROVIDED_VERIFIED_AND_PASSED;
	break;

	case AST_PRES_ALLOWED_USER_NUMBER_FAILED_SCREEN:
		*pi = Q931_IE_CGPN_PI_PRESENTATION_ALLOWED;
		*si = Q931_IE_CGPN_SI_USER_PROVIDED_VERIFIED_AND_FAILED;
	break;

	case AST_PRES_ALLOWED_NETWORK_NUMBER:
		*pi = Q931_IE_CGPN_PI_PRESENTATION_ALLOWED;
		*si = Q931_IE_CGPN_SI_NETWORK_PROVIDED;
	break;

	case AST_PRES_PROHIB_USER_NUMBER_NOT_SCREENED:
		*pi = Q931_IE_CGPN_PI_PRESENTATION_RESTRICTED;
		*si = Q931_IE_CGPN_SI_USER_PROVIDED_NOT_SCREENED;
	break;

	case AST_PRES_PROHIB_USER_NUMBER_PASSED_SCREEN:
		*pi = Q931_IE_CGPN_PI_PRESENTATION_RESTRICTED;
		*si = Q931_IE_CGPN_SI_USER_PROVIDED_VERIFIED_AND_PASSED;
	break;

	case AST_PRES_PROHIB_USER_NUMBER_FAILED_SCREEN:
		*pi = Q931_IE_CGPN_PI_PRESENTATION_RESTRICTED;
		*si = Q931_IE_CGPN_SI_USER_PROVIDED_VERIFIED_AND_FAILED;
	break;

	case AST_PRES_PROHIB_NETWORK_NUMBER:
		*pi = Q931_IE_CGPN_PI_PRESENTATION_RESTRICTED;
		*si = Q931_IE_CGPN_SI_NETWORK_PROVIDED;
	break;

	case AST_PRES_NUMBER_NOT_AVAILABLE:
		*pi = Q931_IE_CGPN_PI_NOT_AVAILABLE;
		*si = Q931_IE_CGPN_SI_USER_PROVIDED_NOT_SCREENED;
	break;

	default:
		*pi = Q931_IE_CGPN_PI_PRESENTATION_ALLOWED;
		*si = Q931_IE_CGPN_SI_USER_PROVIDED_NOT_SCREENED;
	}
}

static enum q931_ie_calling_party_number_type_of_number
	visdn_ast_ton_to_cgpn(int ton)
{
	return ton; /* Ahhrgh */
}

static void visdn_defer_dtmf_in(
	struct visdn_chan *visdn_chan)
{

	ast_mutex_lock(&visdn_chan->ast_chan->lock);
	visdn_chan->dtmf_deferred = TRUE;
	ast_mutex_unlock(&visdn_chan->ast_chan->lock);
}

static enum q931_ie_called_party_number_numbering_plan_identificator
	visdn_cdpn_numbering_plan_by_ton(
		enum q931_ie_called_party_number_type_of_number ton)
{
	if (ton == Q931_IE_CDPN_TON_UNKNOWN)
		return Q931_IE_CDPN_NPI_UNKNOWN;
	else
		return Q931_IE_CDPN_NPI_ISDN_TELEPHONY;
}

static enum q931_ie_calling_party_number_numbering_plan_identificator
	visdn_cgpn_numbering_plan_by_ton(
		enum q931_ie_calling_party_number_type_of_number ton)
{
	if (ton == Q931_IE_CGPN_TON_UNKNOWN)
		return Q931_IE_CGPN_NPI_UNKNOWN;
	else
		return Q931_IE_CGPN_NPI_ISDN_TELEPHONY;
}

static void visdn_undefer_dtmf_in(
	struct visdn_chan *visdn_chan)
{
	struct visdn_ic *ic = visdn_chan->ic;

	ast_mutex_lock(&visdn_chan->ast_chan->lock);
	visdn_chan->dtmf_deferred = FALSE;

	/* Flush queue */
	if (strlen(visdn_chan->dtmf_queue)) {
		Q931_DECLARE_IES(ies);

		struct q931_ie_called_party_number *cdpn =
			q931_ie_called_party_number_alloc();
		cdpn->type_of_number = visdn_type_of_number_to_cdpn(
						ic->outbound_called_ton);
		cdpn->numbering_plan_identificator =
			visdn_cdpn_numbering_plan_by_ton(
				cdpn->type_of_number);

		strncpy(cdpn->number, visdn_chan->dtmf_queue,
			sizeof(cdpn->number));
		q931_ies_add_put(&ies, &cdpn->ie);

		q931_send_primitive(visdn_chan->q931_call,
			Q931_CCB_INFO_REQUEST, &ies);

		visdn_chan->dtmf_queue[0] = '\0';

		Q931_UNDECLARE_IES(ies);
	}

	ast_mutex_unlock(&visdn_chan->ast_chan->lock);
}

static void visdn_queue_dtmf(
	struct visdn_chan *visdn_chan,
	char digit)
{
	ast_mutex_lock(&visdn_chan->ast_chan->lock);

	int len = strlen(visdn_chan->dtmf_queue);

	if (len >= sizeof(visdn_chan->dtmf_queue) - 1) {
		ast_log(LOG_WARNING, "DTMF queue is full, dropping digit\n");
		ast_mutex_unlock(&visdn_chan->ast_chan->lock);
		return;
	}

	visdn_chan->dtmf_queue[len] = digit;
	visdn_chan->dtmf_queue[len + 1] = '\0';

	ast_mutex_unlock(&visdn_chan->ast_chan->lock);
}

static int char_to_hexdigit(char c)
{
	switch(c) {
		case '0': return 0;
		case '1': return 1;
		case '2': return 2;
		case '3': return 3;
		case '4': return 4;
		case '5': return 5;
		case '6': return 6;
		case '7': return 7;
		case '8': return 8;
		case '9': return 9;
		case 'a': return 10;
		case 'b': return 11;
		case 'c': return 12;
		case 'd': return 13;
		case 'e': return 14;
		case 'f': return 15;
		case 'A': return 10;
		case 'B': return 11;
		case 'C': return 12;
		case 'D': return 13;
		case 'E': return 14;
		case 'F': return 15;
	}

	return -1;
}

#define AST_FORMAT_RAW_H223 (1 << 21)

static int visdn_chan_update_bc_cache(
	struct visdn_chan *visdn_chan,
	struct q931_ie_bearer_capability *bc)
{
	if (bc->information_transfer_capability ==
			Q931_IE_BC_ITC_UNRESTRICTED_DIGITAL &&
	    bc->user_information_layer_1_protocol ==
			Q931_IE_BC_UIL1P_UNUSED) {

		ast_log(LOG_DEBUG,
			"Bearer capability is PPP, not handling stream\n");

		visdn_chan->supports_inband_tones = FALSE;
		visdn_chan->is_framed = TRUE;
		visdn_chan->handle_stream = FALSE;

		visdn_chan->ast_frame_type = 0; // Not handling stream
		visdn_chan->ast_frame_subclass = 0;

	} else if (bc->information_transfer_capability ==
			Q931_IE_BC_ITC_UNRESTRICTED_DIGITAL &&
	           bc->user_information_layer_1_protocol ==
			Q931_IE_BC_UIL1P_G7XX_VIDEO) {

ast_log(LOG_NOTICE, "Video going through!\n");

/*		if (visdn_numbers_list_match(&ic->trans_numbers_list,
						called_number)) {*/

		visdn_chan->supports_inband_tones = FALSE;
		visdn_chan->is_framed = FALSE;
		visdn_chan->handle_stream = TRUE;

		visdn_chan->ast_frame_type = AST_FRAME_VIDEO;
		visdn_chan->ast_frame_subclass = AST_FORMAT_RAW_H223;

	} else if (bc->information_transfer_capability ==
			Q931_IE_BC_ITC_SPEECH ||
		    bc->information_transfer_capability ==
			Q931_IE_BC_ITC_3_1_KHZ_AUDIO) {

		visdn_chan->supports_inband_tones = TRUE;
		visdn_chan->is_framed = FALSE;
		visdn_chan->handle_stream = TRUE;

		visdn_chan->ast_frame_type = AST_FRAME_VOICE;

		if (bc->user_information_layer_1_protocol ==
						Q931_IE_BC_UIL1P_G711_ULAW)
			visdn_chan->ast_frame_subclass = AST_FORMAT_ULAW;
		else
			visdn_chan->ast_frame_subclass = AST_FORMAT_ALAW;

	} else
		return FALSE;

	return TRUE;
}

static int visdn_request_call(
	struct visdn_chan *visdn_chan,
	struct visdn_intf *intf,
	const char *number,
	const char *options)
{
	struct ast_channel *ast_chan = visdn_chan->ast_chan;
	struct visdn_ic *ic = intf->current_ic;
	int err;

	visdn_intf_debug(intf, "Calling on interface '%s'\n", intf->name);

	visdn_chan->ic = visdn_ic_get(intf->current_ic);

	Q931_DECLARE_IES(ies);

	/* ------------- Bearer Capability ---------------- */
	const char *raw_bc = pbx_builtin_getvar_helper(ast_chan,
							"BEARERCAP_RAW");
	if (raw_bc) {
		visdn_intf_debug(intf,
			"Taking bearer capability from bridged channel\n");

		struct q931_ie_bearer_capability *bc;
		bc = q931_ie_bearer_capability_alloc();
		char buf[20];

		if (strlen(raw_bc) % 2) {
			ast_log(LOG_WARNING, "BEARERCAP_RAW is invalid\n");
			goto hlc_failure;
		}

		int len = strlen(raw_bc) / 2;

		if (len > sizeof(buf)) {
			ast_log(LOG_WARNING,
				"BEARERCAP_RAW is too long\n");
			goto bc_failure;
		}

		int i;
		for (i=0; i<len; i++) {
			if (char_to_hexdigit(raw_bc[i * 2]) < 0 ||
			    char_to_hexdigit(raw_bc[i * 2 + 1]) < 0) {
				ast_log(LOG_WARNING,
					"BEARERCAP_RAW is invalid\n");
				goto bc_failure;
			}

			buf[i] = char_to_hexdigit(raw_bc[i * 2]) << 4;
			buf[i] |= char_to_hexdigit(raw_bc[i * 2 + 1]);
		}

		buf[len] = '\0';

		if (!q931_ie_bearer_capability_read_from_buf(&bc->ie,
					buf, len, NULL, NULL)) {
			ast_log(LOG_WARNING, "BEARERCAP_RAW is not valid\n");
			goto bc_failure;
		}

		if (!visdn_chan_update_bc_cache(visdn_chan, bc)) {
			ast_log(LOG_WARNING,
					"BEARERCAP_RAW is not supported\n");
			goto bc_failure;
		}

		visdn_chan->bc = bc;

		q931_ies_add(&ies, &bc->ie);
	} else {
bc_failure:;
		struct q931_ie_bearer_capability *bc;
		bc = q931_ie_bearer_capability_alloc();

		bc->coding_standard = Q931_IE_BC_CS_CCITT;
		bc->information_transfer_capability = Q931_IE_BC_ITC_SPEECH;
		bc->transfer_mode = Q931_IE_BC_TM_CIRCUIT;
		bc->information_transfer_rate = Q931_IE_BC_ITR_64;
		bc->user_information_layer_1_protocol =
			Q931_IE_BC_UIL1P_G711_ALAW;
		bc->user_information_layer_2_protocol = Q931_IE_BC_UIL2P_UNUSED;
		bc->user_information_layer_3_protocol = Q931_IE_BC_UIL3P_UNUSED;

		if (options) {
			if (strchr(options, 'D')) {
				bc->information_transfer_capability =
					Q931_IE_BC_ITC_UNRESTRICTED_DIGITAL;
				bc->user_information_layer_1_protocol =
					Q931_IE_BC_UIL1P_UNUSED;
			}

			if (strchr(options, 'V')) {
				bc->information_transfer_capability =
					Q931_IE_BC_ITC_UNRESTRICTED_DIGITAL;
				bc->user_information_layer_1_protocol =
					Q931_IE_BC_UIL1P_G7XX_VIDEO;
			}
		}

		visdn_chan->bc = bc;

		q931_ies_add(&ies, &bc->ie);

		BOOL supported = visdn_chan_update_bc_cache(visdn_chan, bc);

		assert(supported);
	}

	/* ------------- High Layer Compatibility ---------------- */
	const char *raw_hlc = pbx_builtin_getvar_helper(ast_chan, "HLC_RAW");
	if (raw_hlc) {
		visdn_intf_debug(intf, "Taking HLC from bridged channel\n");

		struct q931_ie_high_layer_compatibility *hlc;
		hlc = q931_ie_high_layer_compatibility_alloc();
		char buf[20];

		if (strlen(raw_hlc) % 2) {
			ast_log(LOG_WARNING, "HLC_RAW is invalid\n");
			goto hlc_failure;
		}

		int len = strlen(raw_hlc) / 2;

		if (len > sizeof(buf)) {
			ast_log(LOG_WARNING, "HLC_RAW is too long\n");
			goto hlc_failure;
		}

		int i;
		for (i=0; i<len; i++) {
			if (char_to_hexdigit(raw_hlc[i * 2]) < 0 ||
			    char_to_hexdigit(raw_hlc[i * 2 + 1]) < 0) {
				ast_log(LOG_WARNING, "HLC_RAW is invalid\n");
				goto hlc_failure;
			}

			buf[i] = char_to_hexdigit(raw_hlc[i * 2]) << 4;
			buf[i] |= char_to_hexdigit(raw_hlc[i * 2 + 1]);
		}

		buf[len] = '\0';

		if (!q931_ie_high_layer_compatibility_read_from_buf(&hlc->ie,
					buf, len, NULL, NULL)) {
			ast_log(LOG_WARNING, "HLC_RAW is not valid\n");
			goto hlc_failure;
		}

		q931_ies_add_put(&ies, &hlc->ie);

	} else {
hlc_failure:;

		struct q931_ie_high_layer_compatibility *hlc;
		hlc = q931_ie_high_layer_compatibility_alloc();

		hlc->coding_standard = Q931_IE_HLC_CS_CCITT;
		hlc->interpretation = Q931_IE_HLC_P_FIRST;
		hlc->presentation_method =
			Q931_IE_HLC_PM_HIGH_LAYER_PROTOCOL_PROFILE;
		hlc->characteristics_identification = Q931_IE_HLC_CI_TELEPHONY;
		q931_ies_add_put(&ies, &hlc->ie);
	}

	/* ------------- Low Layer Compatibility ---------------- */
	const char *raw_llc = pbx_builtin_getvar_helper(ast_chan, "LLC_RAW");
	if (raw_llc) {
		visdn_intf_debug(intf, "Taking LLC from bridged channel\n");

		struct q931_ie_low_layer_compatibility *llc;
		llc = q931_ie_low_layer_compatibility_alloc();
		char buf[20];

		if (strlen(raw_llc) % 2) {
			ast_log(LOG_WARNING, "LLC_RAW is invalid\n");
			goto llc_failure;
		}

		int len = strlen(raw_llc) / 2;

		if (len > sizeof(buf)) {
			ast_log(LOG_WARNING, "LLC_RAW is too long\n");
			goto llc_failure;
		}

		int i;
		for (i=0; i<len; i++) {
			if (char_to_hexdigit(raw_llc[i * 2]) < 0 ||
			    char_to_hexdigit(raw_llc[i * 2 + 1]) < 0) {
				ast_log(LOG_WARNING, "LLC_RAW is invalid\n");
				goto llc_failure;
			}

			buf[i] = char_to_hexdigit(raw_llc[i * 2]) << 4;
			buf[i] |= char_to_hexdigit(raw_llc[i * 2 + 1]);
		}

		buf[len] = '\0';

		if (!q931_ie_low_layer_compatibility_read_from_buf(&llc->ie,
					buf, len, NULL, NULL)) {
			ast_log(LOG_WARNING, "LLC_RAW is not valid\n");
			goto llc_failure;
		}

		q931_ies_add_put(&ies, &llc->ie);

	}
llc_failure:;

	/* ------------- END HLC ---------------- */

	struct q931_call *q931_call;
	q931_call = q931_call_alloc_out(intf->q931_intf);
	if (!q931_call) {
		ast_log(LOG_WARNING, "Cannot allocate outbound call\n");
		err = -1;
		goto err_call_alloc;
	}

	q931_call->pvt = visdn_chan_get(visdn_chan);
	visdn_chan->q931_call = q931_call_get(q931_call);

	char newname[40];
	snprintf(newname, sizeof(newname), "VISDN/%s/%d.%c",
		q931_call->intf->name,
		q931_call->call_reference,
		q931_call->direction ==
			Q931_CALL_DIRECTION_INBOUND ? 'I' : 'O');

	ast_change_name(ast_chan, newname);

	ast_setstate(ast_chan, AST_STATE_DIALING);

	struct q931_ie_called_party_number *cdpn =
		q931_ie_called_party_number_alloc();
	cdpn->type_of_number =
		visdn_type_of_number_to_cdpn(ic->outbound_called_ton);

	cdpn->numbering_plan_identificator =
		visdn_cdpn_numbering_plan_by_ton(
			cdpn->type_of_number);

	snprintf(cdpn->number, sizeof(cdpn->number), "%s", number);

	visdn_chan->sent_digits = strlen(cdpn->number);

	q931_ies_add_put(&ies, &cdpn->ie);

	visdn_defer_dtmf_in(visdn_chan);

	if (visdn_chan->sent_digits < strlen(number)) {
		if (!ic->overlap_sending) {
			ast_log(LOG_WARNING,
				"Number too big and overlap sending "
				"disabled\n");
			err = -1;
			goto err_too_many_digits;
		}
	}

	if ((intf->q931_intf->role == LAPD_INTF_ROLE_NT &&
	    !ic->overlap_receiving) ||
	    (intf->q931_intf->role == LAPD_INTF_ROLE_TE &&
	    !ic->overlap_sending)) {
		struct q931_ie_sending_complete *sc =
			q931_ie_sending_complete_alloc();

		q931_ies_add_put(&ies, &sc->ie);
	}

	if (ic->clip_enabled) {
		struct q931_ie_calling_party_number *cgpn =
			q931_ie_calling_party_number_alloc();

		if (ast_chan->cid.cid_num &&
		    strlen(ast_chan->cid.cid_num)) {

			if ((ast_chan->cid.cid_pres & AST_PRES_RESTRICTION) ==
					AST_PRES_ALLOWED ||
			    ic->clip_override) {

				/* Send subaddress if provided */

				visdn_pres_to_pi_si(ast_chan->cid.cid_pres,
					&cgpn->presentation_indicator,
					&cgpn->screening_indicator);

				if (ic->force_outbound_cli_ton !=
						VISDN_TYPE_OF_NUMBER_UNSET) {
					cgpn->type_of_number =
						visdn_type_of_number_to_cgpn(
						ic->force_outbound_cli_ton);
				} else {
					cgpn->type_of_number =
						visdn_ast_ton_to_cgpn(
							ast_chan->cid.cid_ton);
				}

				cgpn->numbering_plan_identificator =
					visdn_cgpn_numbering_plan_by_ton(
						cgpn->type_of_number);

				if (strlen(ic->force_outbound_cli))
					strncpy(cgpn->number,
					ic->force_outbound_cli,
						sizeof(cgpn->number));
				else {
					strncpy(cgpn->number,
						ast_chan->cid.cid_num,
						sizeof(cgpn->number));
				}

				if (ast_chan->cid.cid_name &&
				    strlen(ast_chan->cid.cid_name)) {
					struct q931_ie_display *disp =
						q931_ie_display_alloc();
					strcpy(disp->text,
						ast_chan->cid.cid_name);
					q931_ies_add_put(&ies, &disp->ie);
				}
			} else {
				cgpn->type_of_number =
					Q931_IE_CGPN_TON_UNKNOWN;
				cgpn->numbering_plan_identificator =
					Q931_IE_CGPN_NPI_UNKNOWN;
				cgpn->presentation_indicator =
					Q931_IE_CGPN_PI_PRESENTATION_RESTRICTED;
				cgpn->screening_indicator =
					Q931_IE_CGPN_SI_NETWORK_PROVIDED;
			}
		} else {
			cgpn->type_of_number =
				Q931_IE_CGPN_TON_UNKNOWN;
			cgpn->numbering_plan_identificator =
				Q931_IE_CGPN_NPI_UNKNOWN;
			cgpn->presentation_indicator =
				Q931_IE_CGPN_PI_NOT_AVAILABLE;
			cgpn->screening_indicator =
				Q931_IE_CGPN_SI_NETWORK_PROVIDED;
		}

		q931_ies_add_put(&ies, &cgpn->ie);

		/* NOTE: There is no provision for sending a second CGPN
		 * if the caller is using the special arrangements, since
		 * Asterisk does not support more than one CID
		 */
	}

	ast_dsp_set_features(visdn_chan->dsp,
		DSP_FEATURE_DTMF_DETECT |
		DSP_FEATURE_FAX_DETECT);

	q931_send_primitive(q931_call, Q931_CCB_SETUP_REQUEST, &ies);

	q931_call_put(q931_call);

	Q931_UNDECLARE_IES(ies);

	return 0;

err_too_many_digits:
	q931_call_release_reference(q931_call);
	q931_call_put(q931_call);
err_call_alloc:
	visdn_ic_put(visdn_chan->ic);

	Q931_UNDECLARE_IES(ies);

	return err;
}

static int visdn_resume_call(
	struct visdn_chan *visdn_chan,
	struct visdn_intf *intf,
	const char *number,
	const char *options)
{
	struct ast_channel *ast_chan = visdn_chan->ast_chan;
	int err;

	visdn_intf_debug(intf, "Resuming call\n");

	visdn_chan->ic = visdn_ic_get(intf->current_ic);

	Q931_DECLARE_IES(ies);

	struct q931_call *q931_call;
	q931_call = q931_call_alloc_out(intf->q931_intf);
	if (!q931_call) {
		ast_log(LOG_WARNING, "Cannot allocate outbound call\n");
		err = -1;
		goto err_call_alloc;
	}

	q931_call->pvt = visdn_chan_get(visdn_chan);
	visdn_chan->q931_call = q931_call_get(q931_call);

	char newname[40];
	snprintf(newname, sizeof(newname), "VISDN/%s/%d.%c",
		q931_call->intf->name,
		q931_call->call_reference,
		q931_call->direction ==
			Q931_CALL_DIRECTION_INBOUND ? 'I' : 'O');

	ast_change_name(ast_chan, newname);

	ast_setstate(ast_chan, AST_STATE_DIALING);

	q931_send_primitive(q931_call, Q931_CCB_RESUME_REQUEST, &ies);

	q931_call_put(q931_call);

	Q931_UNDECLARE_IES(ies);

	return 0;

	visdn_ic_put(visdn_chan->ic);

	q931_call_release_reference(q931_call);
	q931_call_put(q931_call);
err_call_alloc:

	Q931_UNDECLARE_IES(ies);

	return err;
}

static int visdn_call(
	struct ast_channel *ast_chan,
	char *orig_dest,
	int timeout)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(ast_chan);
	int err;

	if ((ast_chan->_state != AST_STATE_DOWN) &&
	    (ast_chan->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING,
			"visdn_call called on %s,"
			" neither down nor reserved\n",
			ast_chan->name);

		err = -1;
		goto err_channel_not_down;
	}

	// Parse destination and obtain interface name + number
	char dest[64];
	char *dest_pos = dest;
	const char *token;

	strncpy(dest, orig_dest, sizeof(dest));

	const char *intf_name = strsep(&dest_pos, "/");
	if (!intf_name) {
		ast_log(LOG_WARNING,
			"Invalid destination '%s' format (interface/number)\n",
			dest);

		err = -1;
		goto err_invalid_destination;
	}

	token = strsep(&dest_pos, "/");
	if (!token) {
		ast_log(LOG_WARNING,
			"Invalid destination '%s' format (interface/number)\n",
			dest);

		err = -1;
		goto err_invalid_format;
	}

	strncpy(visdn_chan->number, token, sizeof(visdn_chan->number));

	token = strsep(&dest_pos, "/");
	if (token)
		strncpy(visdn_chan->options, token,
			sizeof(visdn_chan->options));

	struct visdn_intf *intf = NULL;
	if (!strncasecmp(intf_name, VISDN_HUNTGROUP_PREFIX,
			strlen(VISDN_HUNTGROUP_PREFIX))) {

		if (strchr(visdn_chan->options, 'R')) {
			ast_log(LOG_WARNING,
				"Resume on huntgroup not supported\n");
			err = -1;
			goto err_resume_in_huntgroup;
		}

		const char *hg_name = intf_name +
					strlen(VISDN_HUNTGROUP_PREFIX);
		struct visdn_huntgroup *hg;
		hg = visdn_hg_get_by_name(hg_name);
		if (!hg) {
			ast_log(LOG_ERROR, "Cannot find huntgroup '%s'\n",
				hg_name);

			ast_chan->hangupcause = AST_CAUSE_BUSY;

			err = -1;
			goto err_huntgroup_not_found;
		}

		intf = visdn_hg_hunt(hg, NULL, NULL);
		if (!intf) {
			visdn_chan_debug(visdn_chan,
				"Cannot hunt in huntgroup %s\n", hg_name);

			err = -1;
			goto err_no_channel_available;
		}

		visdn_chan->hg_first_intf = visdn_intf_get(intf);
		visdn_chan->huntgroup = visdn_hg_get(hg);
	} else {
		intf = visdn_intf_get_by_name(intf_name);

		if (!intf) {
			ast_log(LOG_WARNING,
				"Interface %s not found\n",
				intf_name);
			err = -1;
			goto err_intf_not_found;
		}

		if (intf->status != VISDN_INTF_STATUS_ACTIVE) {
			ast_log(LOG_WARNING,
				"Interface %s is not online\n",
				intf_name);
			err = -1;
			goto err_intf_not_found;
		}

		visdn_chan->hg_first_intf = NULL;
		visdn_chan->huntgroup = NULL;
	}

	visdn_chan->intf = visdn_intf_get(intf);
	visdn_chan->debug_generic = intf->debug_generic;
	visdn_chan->debug_jitbuf = intf->debug_jitbuf;
	visdn_chan->debug_frames = intf->debug_frames;

	visdn_chan_debug(visdn_chan, "Calling %s on %s\n",
			dest, ast_chan->name);

	if (strchr(visdn_chan->options, 'R')) {
		visdn_resume_call(visdn_chan, intf,
			visdn_chan->number,
			visdn_chan->options);
	} else {
		visdn_request_call(visdn_chan, intf,
			visdn_chan->number,
			visdn_chan->options);
	}

	visdn_intf_put(intf);
	intf = NULL;

	return 0;

err_intf_not_found:
err_no_channel_available:
err_huntgroup_not_found:
err_resume_in_huntgroup:
err_invalid_format:
err_invalid_destination:
err_channel_not_down:

	return err;
}

static int visdn_answer(struct ast_channel *ast_chan)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(ast_chan);

	ast_indicate(ast_chan, -1);

	if (!visdn_chan) {
		ast_log(LOG_ERROR, "NO VISDN_CHAN!!\n");
		return -1;
	}

	if (visdn_chan->q931_call->state == U6_CALL_PRESENT ||
	    visdn_chan->q931_call->state == U7_CALL_RECEIVED ||
	    visdn_chan->q931_call->state == U9_INCOMING_CALL_PROCEEDING ||
	    visdn_chan->q931_call->state == U25_OVERLAP_RECEIVING ||
	    visdn_chan->q931_call->state == N2_OVERLAP_SENDING ||
	    visdn_chan->q931_call->state == N3_OUTGOING_CALL_PROCEEDING ||
	    visdn_chan->q931_call->state == N4_CALL_DELIVERED) {
		q931_send_primitive(visdn_chan->q931_call,
			Q931_CCB_SETUP_RESPONSE, NULL);

		ast_dsp_set_features(visdn_chan->dsp,
			DSP_FEATURE_DTMF_DETECT |
			DSP_FEATURE_FAX_DETECT);
	}

	return 0;
}

static int visdn_bridge(
	struct ast_channel *c0,
	struct ast_channel *c1,
	int flags, struct ast_frame **fo,
	struct ast_channel **rc,
	int timeoutms)
{
	return AST_BRIDGE_FAILED_NOWARN;

#if 0
	/* if need DTMF, cant native bridge (at least not yet...) */
	if (flags & (AST_BRIDGE_DTMF_CHANNEL_0 | AST_BRIDGE_DTMF_CHANNEL_1))
		return AST_BRIDGE_FAILED;

	struct visdn_chan *visdn_chan1 = to_visdn_chan(c0);
	struct visdn_chan *visdn_chan2 = to_visdn_chan(c1);

	char pipeline[100], dest1[100], dest2[100];

	snprintf(pipeline, sizeof(pipeline),
		"/sys/class/net/%s/visdn_channel/connected/../B%d",
		visdn_chan1->q931_call->intf->name,
		visdn_chan1->q931_call->channel->id+1);

	memset(dest1, 0, sizeof(dest1));
	if (readlink(pipeline, dest1, sizeof(dest1) - 1) < 0) {
		ast_log(LOG_ERROR, "readlink(%s): %s\n", pipeline, strerror(errno));
		return AST_BRIDGE_FAILED;
	}

	char *chanid1 = strrchr(dest1, '/');
	if (!chanid1 || !strlen(chanid1 + 1)) {
		ast_log(LOG_ERROR,
			"Invalid chanid found in symlink %s\n",
			dest1);
		return AST_BRIDGE_FAILED;
	}

	chanid1++;

	snprintf(pipeline, sizeof(pipeline),
		"/sys/class/net/%s/visdn_channel/connected/../B%d",
		visdn_chan2->q931_call->intf->name,
		visdn_chan2->q931_call->channel->id+1);

	memset(dest2, 0, sizeof(dest2));
	if (readlink(pipeline, dest2, sizeof(dest2) - 1) < 0) {
		ast_log(LOG_ERROR, "readlink(%s): %s\n", pipeline, strerror(errno));
		return AST_BRIDGE_FAILED;
	}

	char *chanid2 = strrchr(dest2, '/');
	if (!chanid2 || !strlen(chanid2 + 1)) {
		ast_log(LOG_ERROR,
			"Invalid chanid found in symlink %s\n",
			dest2);
		return AST_BRIDGE_FAILED;
	}

	chanid2++;

	visdn_debug("Connecting chan %s to chan %s\n", chanid1, chanid2);

	int fd = open("/sys/visdn_tdm/internal-cxc/connect", O_WRONLY);
	if (fd < 0) {
		ast_log(LOG_ERROR,
			"Cannot open /sys/visdn_tdm/internal-cxc/connect: %s\n",
			strerror(errno));
		return AST_BRIDGE_FAILED;
	}

	if (ioctl(visdn_chan1->up_fd,
			VISDN_IOC_DISCONNECT, NULL) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VISDN_IOC_DISCONNECT): %s\n",
			strerror(errno));
	}

	close(visdn_chan1->up_fd);
	visdn_chan1->up_fd = -1;

	if (ioctl(visdn_chan2->up_fd,
			VISDN_IOC_DISCONNECT, NULL) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VISDN_IOC_DISCONNECT): %s\n",
			strerror(errno));
	}

	close(visdn_chan2->up_fd);
	visdn_chan2->up_fd = -1;

	char command[256];
	snprintf(command, sizeof(command),
		"%s\n%s\n",
		chanid1,
		chanid2);

	if (write(fd, command, strlen(command)) < 0) {
		ast_log(LOG_ERROR,
			"Cannot write to /sys/: %s\n",
			strerror(errno));

		close(fd);

		return AST_BRIDGE_FAILED;
	}

	close(fd);

	struct ast_channel *cs[2];
	cs[0] = c0;
	cs[1] = c1;

	struct ast_channel *who = NULL;
	for (;;) {
		int to = -1;
		who = ast_waitfor_n(cs, 2, &to);
		if (!who) {
			ast_log(LOG_DEBUG, "Ooh, empty read...\n");
			continue;
		}

		struct ast_frame *f;
		f = ast_read(who);
		if (!f)
			break;

		if (f->frametype == AST_FRAME_DTMF) {
			if (((who == c0) && (flags & AST_BRIDGE_DTMF_CHANNEL_0)) ||
			    ((who == c1) && (flags & AST_BRIDGE_DTMF_CHANNEL_1))) {

				*fo = f;
				*rc = who;

				// Disconnect channels
				return AST_BRIDGE_COMPLETE;
			}

			if (who == c0)
				ast_write(c1, f);
			else
				ast_write(c0, f);
		}

		ast_frfree(f);

		// Braindead anyone?
		struct ast_channel *t;
		t = cs[0];
		cs[0] = cs[1];
		cs[1] = t;
	}

	// Really braindead
	*fo = NULL;
	*rc = who;

#endif

	return AST_BRIDGE_COMPLETE;
}

struct ast_frame *visdn_exception(struct ast_channel *ast_chan)
{
	ast_log(LOG_WARNING, "visdn_exception\n");

	return NULL;
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
static int visdn_indicate(struct ast_channel *ast_chan, int condition)
#else
static int visdn_indicate(
	struct ast_channel *ast_chan,
	int condition,
	const void *data,
	size_t datalen)
#endif
{
	struct visdn_chan *visdn_chan = to_visdn_chan(ast_chan);
	const struct tone_zone_sound *tone = NULL;
	int res = 0;

	switch(condition) {
	case -1:
		ast_playtones_stop(ast_chan);
	break;

	case AST_CONTROL_RING:
	case AST_CONTROL_TAKEOFFHOOK:
	case AST_CONTROL_FLASH:
	case AST_CONTROL_WINK:
	case AST_CONTROL_OPTION:
	case AST_CONTROL_RADIO_KEY:
	case AST_CONTROL_RADIO_UNKEY:
		res = 1;
	break;

	case AST_CONTROL_ANSWER:
	break;

	case AST_CONTROL_INBAND_INFO:
		visdn_chan->inband_info = TRUE;
	break;

	case AST_CONTROL_DISCONNECT: {
		Q931_DECLARE_IES(ies);

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(
							visdn_chan->q931_call);

		if (ast_chan->hangupcause)
			cause->value = ast_chan->hangupcause;
		else
			cause->value = Q931_IE_C_CV_NORMAL_CALL_CLEARING;

		q931_ies_add_put(&ies, &cause->ie);

		q931_send_primitive(visdn_chan->q931_call,
					Q931_CCB_DISCONNECT_REQUEST, &ies);

		if (!visdn_chan->inband_info)
			tone = ast_get_indication_tone(ast_chan->zone, "busy");

		Q931_UNDECLARE_IES(ies);
	break;
	}

	case AST_CONTROL_OFFHOOK: {
		if (!visdn_chan->inband_info)
			tone = ast_get_indication_tone(ast_chan->zone, "dial");
	}
	break;

	case AST_CONTROL_HANGUP: {
		if (!visdn_chan->inband_info)
			tone = ast_get_indication_tone(ast_chan->zone,
								"congestion");
	}
	break;

	case AST_CONTROL_RINGING: {
		Q931_DECLARE_IES(ies);

		struct q931_ie_progress_indicator *pi = NULL;

		if (ast_bridged_channel(ast_chan) &&
		   strcmp(ast_bridged_channel(ast_chan)->tech->type,
						VISDN_CHAN_TYPE)) {

			visdn_chan_debug(visdn_chan,
					"Channel is not VISDN, sending"
					" progress indicator\n");

			pi = q931_ie_progress_indicator_alloc();
			pi->coding_standard = Q931_IE_PI_CS_CCITT;
			pi->location = q931_ie_progress_indicator_location(
						visdn_chan->q931_call);
			pi->progress_description =
				Q931_IE_PI_PD_CALL_NOT_END_TO_END;
			q931_ies_add_put(&ies, &pi->ie);
		}

		pi = q931_ie_progress_indicator_alloc();
		pi->coding_standard = Q931_IE_PI_CS_CCITT;
		pi->location = q931_ie_progress_indicator_location(
					visdn_chan->q931_call);
		pi->progress_description = Q931_IE_PI_PD_IN_BAND_INFORMATION;
		q931_ies_add_put(&ies, &pi->ie);

		switch(visdn_chan->q931_call->state) {
		case N1_CALL_INITIATED:
			q931_send_primitive(visdn_chan->q931_call,
				Q931_CCB_PROCEEDING_REQUEST, &ies);

		case N2_OVERLAP_SENDING:
		case N3_OUTGOING_CALL_PROCEEDING:
		case U6_CALL_PRESENT:
		case U9_INCOMING_CALL_PROCEEDING:
		case U25_OVERLAP_RECEIVING:
			q931_send_primitive(visdn_chan->q931_call,
				Q931_CCB_ALERTING_REQUEST, &ies);

			ast_dsp_set_features(visdn_chan->dsp,
				DSP_FEATURE_DTMF_DETECT |
				DSP_FEATURE_FAX_DETECT);
		break;

		default:
		break;
		}

		if (!visdn_chan->inband_info)
			tone = ast_get_indication_tone(ast_chan->zone, "ring");

		Q931_UNDECLARE_IES(ies);
	}
	break;

	case AST_CONTROL_BUSY: {
		Q931_DECLARE_IES(ies);

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(
							visdn_chan->q931_call);

		if (ast_chan->hangupcause)
			cause->value = ast_chan->hangupcause;
		else
			cause->value = Q931_IE_C_CV_USER_BUSY;

		q931_ies_add_put(&ies, &cause->ie);

		q931_send_primitive(visdn_chan->q931_call,
					Q931_CCB_DISCONNECT_REQUEST, &ies);

		if (!visdn_chan->inband_info)
			tone = ast_get_indication_tone(ast_chan->zone, "busy");

		Q931_UNDECLARE_IES(ies);
	}
	break;

	case AST_CONTROL_CONGESTION: {

		Q931_DECLARE_IES(ies);
		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(
							visdn_chan->q931_call);

		if (ast_chan->hangupcause)
			cause->value = ast_chan->hangupcause;
		else
			cause->value =
				Q931_IE_C_CV_SWITCHING_EQUIPMENT_CONGESTION;

		q931_ies_add_put(&ies, &cause->ie);

		q931_send_primitive(visdn_chan->q931_call,
					Q931_CCB_DISCONNECT_REQUEST, &ies);

		if (!visdn_chan->inband_info)
			tone = ast_get_indication_tone(ast_chan->zone, "busy");

		Q931_UNDECLARE_IES(ies);
	}
	break;

	case AST_CONTROL_PROGRESS: {
		Q931_DECLARE_IES(ies);

		struct q931_ie_progress_indicator *pi =
			q931_ie_progress_indicator_alloc();
		pi->coding_standard = Q931_IE_PI_CS_CCITT;
		pi->location = q931_ie_progress_indicator_location(
					visdn_chan->q931_call);

		if (ast_bridged_channel(ast_chan) &&
		   strcmp(ast_bridged_channel(ast_chan)->tech->type,
							VISDN_CHAN_TYPE)) {
			pi->progress_description =
				Q931_IE_PI_PD_CALL_NOT_END_TO_END; // FIXME
		} else if (visdn_chan->supports_inband_tones) {
			pi->progress_description =
				Q931_IE_PI_PD_IN_BAND_INFORMATION;
		}

		q931_ies_add_put(&ies, &pi->ie);

		q931_send_primitive(visdn_chan->q931_call,
				Q931_CCB_PROGRESS_REQUEST, &ies);

		Q931_UNDECLARE_IES(ies);
	}
	break;

	case AST_CONTROL_PROCEEDING:
		if (visdn_chan->q931_call->state == N1_CALL_INITIATED ||
		    visdn_chan->q931_call->state == N2_OVERLAP_SENDING ||
		    visdn_chan->q931_call->state == U6_CALL_PRESENT ||
		    visdn_chan->q931_call->state == U25_OVERLAP_RECEIVING) {
			q931_send_primitive(visdn_chan->q931_call,
				Q931_CCB_PROCEEDING_REQUEST, NULL);

			ast_dsp_set_features(visdn_chan->dsp,
				DSP_FEATURE_DTMF_DETECT |
				DSP_FEATURE_FAX_DETECT);
		}
	break;
	}

	if (tone)
		ast_playtones_start(ast_chan, 0, tone->data, 1);

	return res;
}

static int visdn_fixup(
	struct ast_channel *oldchan,
	struct ast_channel *newchan)
{
	struct visdn_chan *chan = to_visdn_chan(newchan);

	if (chan->ast_chan != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n",
				oldchan, chan->ast_chan);
		return -1;
	}

	chan->ast_chan = newchan;

	return 0;
}

static int visdn_setoption(
	struct ast_channel *ast_chan,
	int option,
	void *data,
	int datalen)
{
	ast_log(LOG_ERROR, "%s\n", __FUNCTION__);

	return -1;
}

static int visdn_transfer(
	struct ast_channel *ast,
	const char *dest)
{
	ast_log(LOG_ERROR, "%s\n", __FUNCTION__);

	return -1;
}

#if ASTERISK_VERSION_NUM < 10402 
static int visdn_send_digit(struct ast_channel *ast_chan, char digit)
{
#else
static int visdn_send_digit(
	struct ast_channel *ast_chan,
	char digit,
	unsigned int duration)
{
#endif
	struct visdn_chan *visdn_chan = to_visdn_chan(ast_chan);
	struct q931_call *q931_call = visdn_chan->q931_call;
	struct visdn_ic *ic = visdn_chan->ic;

	if (visdn_chan->dtmf_deferred) {
		visdn_queue_dtmf(visdn_chan, digit);
		return 0;
	}

	Q931_DECLARE_IES(ies);

	/* Needed for Tilab compliance */
	if (q931_call->state != U3_OUTGOING_CALL_PROCEEDING &&
	    q931_call->state != U4_CALL_DELIVERED &&
	    q931_call->state != U10_ACTIVE) {

		struct q931_ie_called_party_number *cdpn =
			q931_ie_called_party_number_alloc();
		cdpn->type_of_number = visdn_type_of_number_to_cdpn(
						ic->outbound_called_ton);
		cdpn->numbering_plan_identificator =
			visdn_cdpn_numbering_plan_by_ton(
				cdpn->type_of_number);

		cdpn->number[0] = digit;
		cdpn->number[1] = '\0';
		q931_ies_add_put(&ies, &cdpn->ie);

		q931_send_primitive(visdn_chan->q931_call,
			Q931_CCB_INFO_REQUEST, &ies);
	}

	Q931_UNDECLARE_IES(ies);

	/* IMPORTANT: Since Asterisk is a bug made software, if there
	 * are DTMF frames queued and we start generating DTMF tones
	 * the queued frames are discarded and we fail completing
	 * overlap dialing.
	 */

	if (q931_call->state != U1_CALL_INITIATED &&
	    q931_call->state != U2_OVERLAP_SENDING &&
	    q931_call->state != U6_CALL_PRESENT &&
	    q931_call->state != U25_OVERLAP_RECEIVING &&
	    q931_call->state != N1_CALL_INITIATED &&
	    q931_call->state != N2_OVERLAP_SENDING &&
	    q931_call->state != N6_CALL_PRESENT &&
	    q931_call->state != N25_OVERLAP_RECEIVING)
		return 1;
	else
		return 0;
}

static int visdn_sendtext(struct ast_channel *ast, const char *text)
{
	ast_log(LOG_WARNING, "%s\n", __FUNCTION__);

	return -1;
}

static void visdn_disconnect_chan_from_visdn(
	struct visdn_chan *visdn_chan)
{
	if (visdn_chan->up_fd < 0)
		return;

/*	if (visdn_chan->up_bearer_pipeline_id) {


		visdn_chan->up_bearer_pipeline_id = 0;
	}

	if (visdn_chan->up_bearer_pipeline_id) {

		visdn_chan->bearer_up_pipeline_id = 0;
	}*/

#if 0
	if (visdn_chan->bearer_pipeline_id >= 0 &&
	    visdn_chan->bearer_pipeline_id != visdn_chan->up_pipeline_id) {
		memset(&vc, 0, sizeof(vc));
		vc.pipeline_id = visdn_chan->bearer_pipeline_id;

		if (ioctl(visdn.router_control_fd,
				VISDN_IOC_DISCONNECT,
				(caddr_t)&vc) < 0) {

			ast_log(LOG_ERROR,
				"ioctl(VISDN_IOC_DISCONNECT):"
				" %s\n",
				strerror(errno));
		}

		visdn_chan->bearer_pipeline_id = 0;
	}
#endif

	if (close(visdn_chan->up_fd) < 0) {
		ast_log(LOG_ERROR,
			"close(visdn_chan->up_fd): %s\n",
			strerror(errno));
	}

	visdn_chan->up_fd = -1;

/*	if (visdn_chan->ec_fd >= 0) {
		if (close(visdn_chan->ec_fd) < 0) {
			ast_log(LOG_ERROR,
				"close(visdn_chan->ec_fd): %s\n",
				strerror(errno));
		}

		visdn_chan->ec_fd = -1;
	}*/
}

static int visdn_hangup(struct ast_channel *ast_chan)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(ast_chan);

	ast_setstate(ast_chan, AST_STATE_DOWN);

	ast_mutex_lock(&ast_chan->lock);
	if (visdn_chan->q931_call) {

		struct q931_call *q931_call = visdn_chan->q931_call;

		Q931_DECLARE_IES(ies);

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(q931_call);

		if (ast_chan->hangupcause)
			cause->value = ast_chan->hangupcause;
		else
			cause->value =
				Q931_IE_C_CV_NORMAL_CALL_CLEARING;

		q931_ies_add_put(&ies, &cause->ie);

		switch(q931_call->state) {
		case N0_NULL_STATE:
		case U0_NULL_STATE:
		break;

		case U1_CALL_INITIATED:
		case U2_OVERLAP_SENDING:
		case U3_OUTGOING_CALL_PROCEEDING:
		case U4_CALL_DELIVERED:
		case U7_CALL_RECEIVED:
		case U8_CONNECT_REQUEST:
		case U9_INCOMING_CALL_PROCEEDING:
		case U10_ACTIVE:
		case U25_OVERLAP_RECEIVING:
		case N2_OVERLAP_SENDING:
		case N3_OUTGOING_CALL_PROCEEDING:
		case N4_CALL_DELIVERED:
		case N6_CALL_PRESENT:
		case N7_CALL_RECEIVED:
		case N8_CONNECT_REQUEST:
		case N9_INCOMING_CALL_PROCEEDING:
		case N10_ACTIVE:
		case N15_SUSPEND_REQUEST:
		case N25_OVERLAP_RECEIVING:
			q931_send_primitive(q931_call,
				Q931_CCB_DISCONNECT_REQUEST, &ies);
		break;

		case U15_SUSPEND_REQUEST:
			/* Suspend reject and disconnect request ??? */
		break;

		case U6_CALL_PRESENT:
		case U17_RESUME_REQUEST:
		case N1_CALL_INITIATED:
		case N17_RESUME_REQUEST:
			/* No ast_chan has been created yet */
		break;

		case U11_DISCONNECT_REQUEST:
		case N12_DISCONNECT_INDICATION:
		case U19_RELEASE_REQUEST:
		case N19_RELEASE_REQUEST:
		case N22_CALL_ABORT:
			/* Do nothing, already releasing */
		break;

		case U12_DISCONNECT_INDICATION:
		case N11_DISCONNECT_REQUEST:
			q931_send_primitive(q931_call,
				Q931_CCB_RELEASE_REQUEST, &ies);
		break;
		}

		Q931_UNDECLARE_IES(ies);

		/* Detach module and channel */
		visdn_chan_put(visdn_chan->q931_call->pvt);
		visdn_chan->q931_call->pvt = NULL;

		q931_call_put(visdn_chan->q931_call);
		visdn_chan->q931_call = NULL;
	}

	if (visdn_chan->suspended_call) {
		// We are responsible for the channel
		q931_channel_release(visdn_chan->suspended_call->q931_chan);

		list_del(&visdn_chan->suspended_call->node);
		free(visdn_chan->suspended_call);
		visdn_chan->suspended_call = NULL;
	}

	if (visdn_chan->up_fd >= 0) {
		// Disconnect the softport since we cannot rely on
		// libq931 (see above)

		visdn_disconnect_chan_from_visdn(visdn_chan);
	}

	/* Okay, here we should have done our work, but, there may be
	 * references to visdn_chan held in other threads, so, before giving
	 * control back to Assterisk (which will free the ast_chan structure
	 * we have to wait until all threads have released their reference.
	 */
	ast_mutex_unlock(&ast_chan->lock);
	int res = 0;
	struct timespec timeout;
	struct timeval now;
	gettimeofday( &now, NULL );
	timeout.tv_sec = now.tv_sec + 5;
	timeout.tv_nsec = now.tv_usec * 1000;

	ast_mutex_lock(&visdn.usecnt_lock);
	while(visdn_chan->refcnt > 1 && res == 0) {
		res = ast_cond_timedwait(
				&visdn_chan->refcnt_decremented_cond,
				&visdn.usecnt_lock, &timeout);
	}
	ast_mutex_unlock(&visdn.usecnt_lock);
	ast_mutex_lock(&ast_chan->lock);

	assert(visdn_chan->refcnt > 0);

	if (res == ETIMEDOUT)
		ast_log(LOG_WARNING,
			"%s: Timeout waiting for references to"
			" visdn_chan to be released, possible reference"
			" leak, %d references left\n",
			ast_chan->name,
			visdn_chan->refcnt);

	if (visdn_chan->intf) {
		visdn_intf_put(visdn_chan->intf);
		visdn_chan->intf = NULL;
	}

	visdn_chan->ast_chan = NULL;

	visdn_chan_put(ast_chan->tech_pvt);
	ast_chan->tech_pvt = NULL;

	ast_mutex_unlock(&ast_chan->lock);

	return 0;
}

static struct ast_frame *visdn_read(struct ast_channel *ast_chan)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(ast_chan);
	struct ast_frame *frame = &visdn_chan->frame_out;

	frame->src = VISDN_CHAN_TYPE;
	frame->mallocd = 0;
	frame->delivery.tv_sec = 0;
	frame->delivery.tv_usec = 0;

	if (visdn_chan->up_fd < 0) {
		frame->frametype = AST_FRAME_NULL;
		frame->subclass = 0;
		frame->samples = 0;
		frame->datalen = 0;
		frame->data = NULL;
		frame->offset = 0;

		return frame;
	}

	int nread = read(visdn_chan->up_fd,
			visdn_chan->frame_out_buf + AST_FRIENDLY_OFFSET,
			sizeof(visdn_chan->frame_out_buf) -
						AST_FRIENDLY_OFFSET);
	if (nread < 0) {
		ast_log(LOG_WARNING, "read error: %s\n", strerror(errno));
		return frame;
	}

	struct visdn_intf *intf = visdn_chan->q931_call->intf->pvt;

	longtime_t now = longtime_now();

	if (intf->debug_frames) {
		__u8 *buf = visdn_chan->frame_out_buf + AST_FRIENDLY_OFFSET;

		ast_verbose(
			"R %7.3f     F%02x R%02x N%02x"
			" %02x%02x%02x%02x%02x%02x%02x%02x"
			" %d\n",
			(now - visdn_chan->last_rx) / 1000.0,
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
			nread);
	}

	visdn_chan->last_rx = longtime_now();

	frame->frametype = visdn_chan->ast_frame_type;
	frame->subclass = visdn_chan->ast_frame_subclass;
	frame->samples = nread;
	frame->datalen = nread;
	frame->data = visdn_chan->frame_out_buf + AST_FRIENDLY_OFFSET;
	frame->offset = AST_FRIENDLY_OFFSET;

/*		struct ast_frame *f2 =
			ast_dsp_process(ast_chan, visdn_chan->dsp, &f);
		return f2;*/

	return frame;
}

static int visdn_write(
	struct ast_channel *ast_chan,
	struct ast_frame *frame)
{
	struct visdn_chan *visdn_chan = to_visdn_chan(ast_chan);

	if (frame->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING,
			"Don't know what to do with frame type '%d'\n",
			frame->frametype);

		return 0;
	}

	if (frame->subclass != AST_FORMAT_ALAW) {
		ast_log(LOG_WARNING,
			"Cannot handle frames in %d format\n",
			frame->subclass);
		return 0;
	}

	if (visdn_chan->up_fd < 0)
		return 0;

	if (!visdn_chan->pipeline_tx)
		return 0;

	if (!frame->datalen)
		return 0;

	int len = frame->datalen;
	__u8 *buf = frame->data;

	struct visdn_ic *ic = visdn_chan->ic;

	int pressure;
	if (ioctl(visdn_chan->up_fd, KS_UP_GET_PRESSURE, &pressure)) {
		ast_log(LOG_ERROR, "ioctl(): %s\n", strerror(errno));
	}

	visdn_chan->pressure_average =
		((ic->jitbuf_average * visdn_chan->pressure_average) +
		pressure) / (ic->jitbuf_average + 1);

	longtime_t now = longtime_now();
	if (now - visdn_chan->last_tx > frame->samples * 125 * 2) {

		int diff = (ic->jitbuf_high + ic->jitbuf_low) / 2;

		visdn_chan->pressure_average = diff;

		buf = alloca(len + diff);

		if (frame->subclass == AST_FORMAT_SLINEAR)
			memset(buf, 0x00, diff);
		else
			memset(buf, 0x2a, diff);

		memcpy(buf + diff, frame->data, len);
		len += diff;

		visdn_chan_debug_jitbuf(visdn_chan,
			"TX delivery late (%lld ms), adding %d samples and"
			" resetting pressure average\n",
			(now - visdn_chan->last_tx) / 1000,
			diff);

	} else if (pressure < ic->jitbuf_hardlow) {
		int diff = (ic->jitbuf_hardlow - pressure);

		buf = alloca(len + diff);

		memset(buf, 0x2a, diff);
		memcpy(buf + diff, frame->data, len);
		len += diff;

		visdn_chan_debug_jitbuf(visdn_chan,
			"TX under hard low-mark: added %d samples\n",
			diff);

	} else if (visdn_chan->pressure_average < ic->jitbuf_low &&
					    pressure < ic->jitbuf_low) {
		int diff = (ic->jitbuf_low - visdn_chan->pressure_average);

		buf = alloca(len + diff);

		memset(buf, 0x2a, diff);
		memcpy(buf + diff, frame->data, len);
		len += diff;

		visdn_chan_debug_jitbuf(visdn_chan,
			"TX under low-mark: added %d samples\n",
			diff);
	} else if (pressure + len > ic->jitbuf_hardhigh) {

		int drop = min(len, (pressure + len - ic->jitbuf_hardhigh));

		visdn_chan_debug_jitbuf(visdn_chan,
			"TX %d over hard high-mark: dropped %d samples\n",
			pressure + len - ic->jitbuf_hardhigh,
			drop);

		len = max(0, len - drop);

	} else if (visdn_chan->pressure_average > ic->jitbuf_high &&
					    pressure > ic->jitbuf_high) {

		int drop = min(len, (visdn_chan->pressure_average -
						ic->jitbuf_high));

		visdn_chan_debug_jitbuf(visdn_chan,
			"TX %d over high-mark: dropped %d samples\n",
			visdn_chan->pressure_average > ic->jitbuf_high,
			drop);

		len = max(0, len - drop);
	}

	struct visdn_intf *intf = visdn_chan->q931_call->intf->pvt;

	if (intf->debug_frames) {
		ast_verbose(
			"W %7.3f"
			" S%02x F%02x R%02x N%02x"
			" %02x%02x%02x%02x%02x%02x%02x%02x"
			" %3d(%-3d) P%-3d PA%-3d\n",
			(now - visdn_chan->last_tx) / 1000.0,
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
			visdn_chan->pressure_average);
	}

	visdn_chan->last_tx = now;

#if 0
	/* Octet reverser */
	{
	int i;
	for(i=0; i<len; i++) {
		buf[i] =
			((buf[i] & 0x80) >> 7) |
			((buf[i] & 0x40) >> 5) |
			((buf[i] & 0x20) >> 3) |
			((buf[i] & 0x10) >> 1) |
			((buf[i] & 0x08) << 1) |
			((buf[i] & 0x04) << 3) |
			((buf[i] & 0x02) << 5) |
			((buf[i] & 0x01) << 7);
	}
	}
#endif

	if (write(visdn_chan->up_fd, buf, len) < 0) {
		ast_log(LOG_ERROR, "write(): %s\n", strerror(errno));
	}

#if 0
struct timeval tv;
gettimeofday(&tv, NULL);
ast_verbose("W %.3f %4d %3d %02x%02x%02x%02x%02x%02x%02x%02x\n",
	tv.tv_sec + tv.tv_usec/1000000.0,
	len,
	pressure,
	buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
#endif

	return 0;
}

static struct ast_channel *visdn_request(
	const char *type, int format, void *data, int *cause)
{
	struct visdn_chan *visdn_chan;

	if (!(format & AST_FORMAT_ALAW)) {
		ast_log(LOG_NOTICE,
			"Asked to get a channel of unsupported format '%d'\n",
			format);
		goto err_unsupported_format;
	}

	visdn_chan = visdn_chan_alloc();
	if (!visdn_chan) {
		ast_log(LOG_ERROR, "Cannot allocate visdn_chan\n");
		goto err_visdn_chan_alloc;
	}

	visdn_chan->ast_chan = visdn_ast_chan_alloc(visdn_chan,
					AST_STATE_DOWN, NULL);
	if (!visdn_chan->ast_chan)
		goto err_visdn_ast_chan_alloc;

	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	visdn_chan_put(visdn_chan);

	return ast_chan;

	visdn_chan_put(visdn_chan->ast_chan->tech_pvt);
	visdn_chan->ast_chan->tech_pvt = NULL;
	ast_channel_free(visdn_chan->ast_chan);
	visdn_chan->ast_chan = NULL;
err_visdn_ast_chan_alloc:
	visdn_chan_put(visdn_chan);
err_visdn_chan_alloc:
err_unsupported_format:

	return NULL;
}

static const struct ast_channel_tech visdn_tech = {
	.type		= VISDN_CHAN_TYPE,
	.description	= VISDN_DESCRIPTION,
	.capabilities	= AST_FORMAT_ALAW,
	.requester	= visdn_request,
	.call		= visdn_call,
	.hangup		= visdn_hangup,
	.answer		= visdn_answer,
	.read		= visdn_read,
	.write		= visdn_write,
	.indicate	= visdn_indicate,
	.transfer	= visdn_transfer,
	.fixup		= visdn_fixup,
	.bridge		= visdn_bridge,
	.send_text	= visdn_sendtext,
	.setoption	= visdn_setoption,

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	.send_digit	= visdn_send_digit,
#else
	.send_digit_end	= visdn_send_digit,
#endif
};

#define MAX_PAYLOAD 1024

static void visdn_netlink_receive(void)
{
	struct sockaddr_nl tonl;
	tonl.nl_family = AF_NETLINK;
	tonl.nl_pid = 0;
	tonl.nl_groups = 0;

	struct msghdr skmsg;
	struct sockaddr_nl dest_addr;
	struct cmsghdr cmsg;
	struct iovec iov;

	__u8 data[NLMSG_SPACE(MAX_PAYLOAD)];

	struct nlmsghdr *hdr = (struct nlmsghdr *)data;

	iov.iov_base = data;
	iov.iov_len = sizeof(data);

	skmsg.msg_name = &dest_addr;
	skmsg.msg_namelen = sizeof(dest_addr);
	skmsg.msg_iov = &iov;
	skmsg.msg_iovlen = 1;
	skmsg.msg_control = &cmsg;
	skmsg.msg_controllen = sizeof(cmsg);
	skmsg.msg_flags = 0;

	if(recvmsg(visdn.netlink_socket, &skmsg, 0) < 0) {
		ast_log(LOG_WARNING, "recvmsg: %s\n", strerror(errno));
		return;
	}

	// Implement multipart messages FIXME FIXME TODO

	if (hdr->nlmsg_type == RTM_NEWLINK) {
		struct ifinfomsg *ifi = NLMSG_DATA(hdr);

		if (ifi->ifi_type == ARPHRD_LAPD) {

			char ifname[IFNAMSIZ] = "";
			int len = hdr->nlmsg_len -
				NLMSG_LENGTH(sizeof(struct ifinfomsg));

			struct rtattr *rtattr;
			for (rtattr = IFLA_RTA(ifi);
			     RTA_OK(rtattr, len);
			     rtattr = RTA_NEXT(rtattr, len)) {

				if (rtattr->rta_type == IFLA_IFNAME) {
					strncpy(ifname,
						RTA_DATA(rtattr),
						sizeof(ifname));
				}
			}

			if (ifi->ifi_flags & IFF_UP) {
				visdn_debug_netlink("Netlink msg: %s UP %s\n",
					ifname,
					(ifi->ifi_flags & IFF_ALLMULTI) ?
				 		"NT": "TE");

				//visdn_intf_now_up(ifname);XXX
			} else {
				visdn_debug_netlink("Netlink msg: %s DOWN %s\n",
					ifname,
					(ifi->ifi_flags & IFF_ALLMULTI) ?
						"NT": "TE");

				//visdn_intf_now_down(ifname);XXX
			}
		}
	}
}

static void visdn_ccb_q931_receive(void)
{
	struct q931_ccb_message *msg;

	while(1) {
		ast_mutex_lock(&visdn.ccb_q931_queue_lock);

		if (list_empty(&visdn.ccb_q931_queue)) {
			ast_mutex_unlock(&visdn.ccb_q931_queue_lock);
			break;
		}

		msg = list_entry(visdn.ccb_q931_queue.next,
				struct q931_ccb_message, node);

		char buf[1];
		int res = read(visdn.ccb_q931_queue_pipe_read, buf, 1);
		if (res < 0);

		list_del_init(&msg->node);
		ast_mutex_unlock(&visdn.ccb_q931_queue_lock);

		q931_ccb_dispatch(msg);

		if (msg->call)
			q931_call_put(msg->call);

		q931_ies_destroy(&msg->ies);
		free(msg);
	}
}

struct mgmt_prim
{
	struct lapd_prim_hdr prim;
	struct lapd_ctrl_hdr ctrl;
};

static int visdn_mgmt_receive(struct visdn_intf *intf)
{
	struct msghdr skmsg;
	struct cmsghdr cmsg;
	struct iovec iov;
	struct mgmt_prim prim;
	int len;

	iov.iov_base = &prim;
	iov.iov_len = sizeof(prim);

	skmsg.msg_name = NULL;
	skmsg.msg_namelen = 0;
	skmsg.msg_iov = &iov;
	skmsg.msg_iovlen = 1;
	skmsg.msg_control = &cmsg;
	skmsg.msg_controllen = sizeof(cmsg);
	skmsg.msg_flags = 0;

	len = recvmsg(intf->mgmt_fd, &skmsg, 0);
	if(len < 0) {
		ast_log(LOG_ERROR, "recvmsg error: %s\n",
			strerror(errno));

		return len;
	}

	switch(prim.prim.primitive_type) {
	case LAPD_MPH_ERROR_INDICATION:
		visdn_intf_debug(intf, "%s: MPH-ERROR-INDICATION: %d\n",
			intf->name, prim.ctrl.param);
	break;

	case LAPD_MPH_ACTIVATE_INDICATION:
		visdn_intf_debug(intf, "%s: MPH-ACTIVATE-INDICATION\n",
			intf->name);
	break;

	case LAPD_MPH_DEACTIVATE_INDICATION:
		visdn_intf_debug(intf, "%s: MPH-DEACTIVATE-INDICATION\n",
			intf->name);
	break;

	case LAPD_MPH_INFORMATION_INDICATION:
		visdn_intf_debug(intf, "%s: MPH-INFORMATION-INDICATION: %s\n",
			intf->name,
			prim.ctrl.param == LAPD_MPH_II_CONNECTED ?
				"CONNECTED" :
				"DISCONNECTED");
	break;

	default:
		ast_log(LOG_NOTICE, "Unexpected primitive %d\n",
			prim.prim.primitive_type);
		return -EBADMSG;
	}

	return 0;
}

static void visdn_q931_ccb_receive(void);

static int visdn_q931_thread_do_poll()
{
	longtime_t usec_to_wait;

	usec_to_wait = q931_run_timers();
	usec_to_wait = visdn_intf_run_timers(usec_to_wait);

	int msec_to_wait = (usec_to_wait < 0) ? -1 :
				usec_to_wait / 1000 + 1;

	visdn_debug_q931("poll(%d ms)\n", msec_to_wait);

	// Uhm... we should lock, copy polls and unlock before poll()
	if (poll(visdn.polls, visdn.npolls, msec_to_wait) < 0) {
		if (errno == EINTR)
			return TRUE;

		ast_log(LOG_WARNING, "poll error: %s\n", strerror(errno));
		exit(1);
	}

	int i;
	for(i = 0; i < visdn.npolls; i++) {
		if (visdn.poll_infos[i].type == POLL_INFO_TYPE_NETLINK) {
			if (visdn.polls[i].revents &
					(POLLIN | POLLPRI | POLLERR |
					 POLLHUP | POLLNVAL)) {
				visdn_netlink_receive();
				break; // polls list may have been changed
			}
		} else if (visdn.poll_infos[i].type ==
						POLL_INFO_TYPE_Q931_CCB) {

			if (visdn.polls[i].revents &
					(POLLIN | POLLPRI | POLLERR |
					 POLLHUP | POLLNVAL)) {
				visdn_q931_ccb_receive();
			}
		} else if (visdn.poll_infos[i].type ==
						POLL_INFO_TYPE_CCB_Q931) {

			if (visdn.polls[i].revents &
					(POLLIN | POLLPRI | POLLERR |
					 POLLHUP | POLLNVAL)) {
				visdn_ccb_q931_receive();
			}
		} else if (visdn.poll_infos[i].type ==
						POLL_INFO_TYPE_MGMT) {

			if (visdn.polls[i].revents &
					(POLLIN | POLLPRI | POLLERR |
					 POLLHUP | POLLNVAL)) {

				int err = visdn_mgmt_receive(
					visdn.poll_infos[i].intf);

				if (err < 0) {

					ast_log(LOG_ERROR,
						"Interface '%s' has been put "
						"in FAILED mode\n",
						visdn.poll_infos[i].
								intf->name);

					visdn.poll_infos[i].intf->status =
						VISDN_INTF_STATUS_FAILED;

					visdn_intf_close(
						visdn.poll_infos[i].intf);

					refresh_polls_list();

					break;
				}
			}

		} else if (visdn.poll_infos[i].type ==
						POLL_INFO_TYPE_ACCEPT) {

			if (visdn.polls[i].revents &
					(POLLIN | POLLPRI | POLLERR |
					 POLLHUP | POLLNVAL)) {

				visdn_accept(
					visdn.poll_infos[i].intf->q931_intf,
					visdn.polls[i].fd);

				break; // polls list may have been changed
			}
		} else if (visdn.poll_infos[i].type == POLL_INFO_TYPE_DLC ||
		           visdn.poll_infos[i].type == POLL_INFO_TYPE_BC_DLC) {
			if (visdn.polls[i].revents &
					(POLLIN | POLLPRI | POLLERR |
					 POLLHUP | POLLNVAL)) {

				int err;
				err = q931_receive(visdn.poll_infos[i].dlc);
				if (err < 0 && err != -EBADMSG) {

					ast_log(LOG_ERROR,
						"Interface '%s' has been put "
						"in FAILED mode\n",
						visdn.poll_infos[i].intf->name);

					visdn.poll_infos[i].intf->status =
						VISDN_INTF_STATUS_FAILED;

					visdn_intf_close(
						visdn.poll_infos[i].intf);

					refresh_polls_list();

					break;
				}
			}
		}
	}

	int active_calls_cnt = 0;
	if (visdn.have_to_exit) {
		active_calls_cnt = 0;

		struct visdn_intf *intf;
		ast_rwlock_rdlock(&visdn.intfs_list_lock);
		list_for_each_entry(intf, &visdn.intfs_list, node) {
			if (intf->q931_intf) {
				struct q931_call *call;
				list_for_each_entry(call,
						&intf->q931_intf->calls,
						calls_node)
					active_calls_cnt++;
			}
		}
		ast_rwlock_unlock(&visdn.intfs_list_lock);

		ast_log(LOG_WARNING,
			"There are still %d active calls, waiting...\n",
			active_calls_cnt);
	}

	return (!visdn.have_to_exit || active_calls_cnt > 0);
}


static void *visdn_q931_thread_main(void *data)
{
	visdn.npolls = 0;
	refresh_polls_list();
	visdn.have_to_exit = 0;

	while(visdn_q931_thread_do_poll());

	return NULL;
}

static void visdn_handle_eventual_progind(
	struct visdn_chan *visdn_chan,
	const struct q931_ies *ies)
{
	int i;
	for (i=0; i<ies->count; i++) {
		if (ies->ies[i]->cls->id == Q931_IE_PROGRESS_INDICATOR) {
			struct q931_ie_progress_indicator *pi =
				container_of(ies->ies[i],
					struct q931_ie_progress_indicator, ie);

			if (pi->progress_description ==
					Q931_IE_PI_PD_IN_BAND_INFORMATION) {

				visdn_chan_debug(visdn_chan,
					"In-band informations available\n");

				visdn_chan->inband_info = TRUE;
				break;
			}
		}
	}
}

static void visdn_q931_alerting_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	struct visdn_chan *visdn_chan = q931_call->pvt;

	if (!visdn_chan)
		return;

	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	ast_mutex_lock(&ast_chan->lock);
	visdn_handle_eventual_progind(visdn_chan, ies);
	ast_mutex_unlock(&ast_chan->lock);

	ast_queue_control(ast_chan, AST_CONTROL_RINGING);
	ast_setstate(ast_chan, AST_STATE_RINGING);
}

static void visdn_q931_connect_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	q931_send_primitive(q931_call, Q931_CCB_SETUP_COMPLETE_REQUEST, NULL);

	struct visdn_chan *visdn_chan = q931_call->pvt;

	if (!visdn_chan)
		return;

	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	ast_queue_control(ast_chan, AST_CONTROL_ANSWER);

//	struct visdn_chan *visdn_chan = to_visdn_chan(ast_chan);

/*	if (visdn_chan->ec_fd >= 0) {
		visdn_debug("Activating echo canceller\n");

		if (ioctl(visdn_chan->ec_fd, VEC_START,
			(caddr_t)&visdn_chan->ec_ne_channel_id) < 0) {

			ast_log(LOG_ERROR,
				"ioctl(VEC_START): %s\n",
				strerror(errno));
		}
	}*/
}

static int visdn_ie_to_ast_hangupcause(
	const struct q931_ie_cause *cause)
{
	/* Asterisk uses the same q931 causes. Is it guaranteed? */

	return cause->value;
}

static void visdn_set_hangupcause_by_ies(
	struct visdn_chan *visdn_chan,
	const struct q931_ies *ies)
{
	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	int i;
	for (i=0; i<ies->count; i++) {
		if (ies->ies[i]->cls->id == Q931_IE_CAUSE) {
			 ast_chan->hangupcause =
				visdn_ie_to_ast_hangupcause(
					container_of(ies->ies[i],
						struct q931_ie_cause, ie));
		}
	}
}


static void visdn_q931_disconnect_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	struct visdn_chan *visdn_chan = q931_call->pvt;

	if (!visdn_chan)
		return;

	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	ast_mutex_lock(&ast_chan->lock);
	visdn_handle_eventual_progind(visdn_chan, ies);
	visdn_set_hangupcause_by_ies(visdn_chan, ies);
	ast_mutex_unlock(&ast_chan->lock);

	if (visdn_chan->inband_info &&
	    visdn_chan->channel_has_been_connected) {
		ast_queue_control(ast_chan, AST_CONTROL_INBAND_INFO);
	} else {
		q931_send_primitive(q931_call, Q931_CCB_RELEASE_REQUEST, NULL);
	}

	ast_queue_control(ast_chan, AST_CONTROL_DISCONNECT);
}

static void visdn_q931_error_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
}

static void visdn_q931_info_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	struct visdn_chan *visdn_chan = q931_call->pvt;

	if (!visdn_chan)
		return;

	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	if (q931_call->state != U2_OVERLAP_SENDING &&
	    q931_call->state != N2_OVERLAP_SENDING &&
	    q931_call->state != U25_OVERLAP_RECEIVING &&
	    q931_call->state != N25_OVERLAP_RECEIVING) {
		return;
	}

	struct q931_ie_called_party_number *cdpn = NULL;

	int i;
	for(i=0; i<ies->count; i++) {
		if (ies->ies[i]->cls->id == Q931_IE_SENDING_COMPLETE) {
			visdn_chan->sending_complete = TRUE;
		} else if (ies->ies[i]->cls->id ==
					 Q931_IE_CALLED_PARTY_NUMBER) {
			cdpn = container_of(ies->ies[i],
				struct q931_ie_called_party_number, ie);
		}
	}

	if (cdpn) {
		for(i=0; cdpn->number[i]; i++) {
			struct ast_frame f =
				{ AST_FRAME_DTMF, cdpn->number[i] };
			ast_queue_frame(ast_chan, &f);
		}
	} else {
		/* Better to use a specific frame */
		struct ast_frame f =
			{ AST_FRAME_DTMF, 0 };
		ast_queue_frame(ast_chan, &f);
	}
}

static void visdn_q931_more_info_indication(
	struct q931_call *q931_call,
	const struct q931_ies *user_ies)
{
	struct visdn_chan *visdn_chan = q931_call->pvt;

	if (!visdn_chan)
		return;

	if (visdn_chan->sent_digits < strlen(visdn_chan->number)) {
		/* There are still digits to be sent in INFO */

		Q931_DECLARE_IES(ies);

		struct q931_ie_called_party_number *cdpn =
			q931_ie_called_party_number_alloc();
		cdpn->type_of_number = visdn_type_of_number_to_cdpn(
					visdn_chan->ic->outbound_called_ton);
		cdpn->numbering_plan_identificator =
			visdn_cdpn_numbering_plan_by_ton(
				cdpn->type_of_number);

		strncpy(cdpn->number,
			visdn_chan->number + visdn_chan->sent_digits,
			sizeof(cdpn->number));

		q931_ies_add_put(&ies, &cdpn->ie);

		q931_send_primitive(visdn_chan->q931_call,
			Q931_CCB_INFO_REQUEST, &ies);

		Q931_UNDECLARE_IES(ies);
	}

	visdn_undefer_dtmf_in(visdn_chan);
}

static void visdn_q931_notify_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
}

static void visdn_q931_proceeding_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	struct visdn_chan *visdn_chan = q931_call->pvt;

	if (!visdn_chan)
		return;

	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	ast_mutex_lock(&ast_chan->lock);
	visdn_handle_eventual_progind(visdn_chan, ies);
	ast_mutex_unlock(&ast_chan->lock);

	visdn_undefer_dtmf_in(visdn_chan);

	ast_queue_control(ast_chan, AST_CONTROL_PROCEEDING);
}

static void visdn_q931_progress_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	struct visdn_chan *visdn_chan = q931_call->pvt;

	if (!visdn_chan)
		return;

	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	ast_mutex_lock(&ast_chan->lock);
	visdn_handle_eventual_progind(visdn_chan, ies);
	ast_mutex_unlock(&ast_chan->lock);

	ast_queue_control(ast_chan, AST_CONTROL_PROGRESS);
}

static void visdn_hunt_next_or_hangup(
	struct visdn_chan *visdn_chan,
	const struct q931_ies *ies)
{
	struct ast_channel *ast_chan = visdn_chan->ast_chan;
	struct q931_ie_cause *cause = NULL;

	int i;
	for(i=0; i<ies->count; i++) {
		if (ies->ies[i]->cls->id == Q931_IE_CAUSE) {
			cause = container_of(ies->ies[i],
				struct q931_ie_cause, ie);
		}
	}

	if (visdn_chan->huntgroup &&
	    cause &&
	    (cause->value == Q931_IE_C_CV_NO_CIRCUIT_CHANNEL_AVAILABLE ||
	    cause->value == Q931_IE_C_CV_NETWORK_OUT_OF_ORDER ||
	    cause->value == Q931_IE_C_CV_TEMPORARY_FAILURE ||
	    cause->value == Q931_IE_C_CV_SWITCHING_EQUIPMENT_CONGESTION ||
	    cause->value == Q931_IE_C_CV_ACCESS_INFORMATION_DISCARDED ||
	    cause->value ==
			Q931_IE_C_CV_REQUESTED_CIRCUIT_CHANNEL_NOT_AVAILABLE ||
	    cause->value == Q931_IE_C_CV_RESOURCES_UNAVAILABLE)) {

		struct visdn_intf *intf;

		intf = visdn_hg_hunt(visdn_chan->huntgroup,
				visdn_chan->ic->intf,
				visdn_chan->hg_first_intf);
		if (!intf) {
			ast_mutex_lock(&ast_chan->lock);
			visdn_set_hangupcause_by_ies(visdn_chan, ies);
			ast_chan->_softhangup |= AST_SOFTHANGUP_DEV;
			ast_mutex_unlock(&ast_chan->lock);

			return;
		}

		visdn_request_call(visdn_chan, intf,
			visdn_chan->number, visdn_chan->options);

		visdn_intf_put(intf);
		intf = NULL;
	} else {
		ast_mutex_lock(&ast_chan->lock);
		visdn_set_hangupcause_by_ies(visdn_chan, ies);
		ast_chan->_softhangup |= AST_SOFTHANGUP_DEV;
		ast_mutex_unlock(&ast_chan->lock);
	}
}

static void visdn_q931_reject_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	struct visdn_chan *visdn_chan = q931_call->pvt;

	if (!visdn_chan)
		return;

	visdn_hunt_next_or_hangup(visdn_chan, ies);
}

static void visdn_q931_release_confirm(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_release_confirm_status status)
{
	struct visdn_chan *visdn_chan = q931_call->pvt;

	if (!visdn_chan)
		return;

	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	ast_mutex_lock(&ast_chan->lock);
	visdn_set_hangupcause_by_ies(visdn_chan, ies);
	ast_chan->_softhangup |= AST_SOFTHANGUP_DEV;
	ast_mutex_unlock(&ast_chan->lock);
}

static void visdn_q931_release_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	struct visdn_chan *visdn_chan = q931_call->pvt;

	if (!visdn_chan)
		return;

	visdn_hunt_next_or_hangup(visdn_chan, ies);
}

static void visdn_q931_resume_confirm(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_resume_confirm_status status)
{
	if (status == Q931_RESUME_CONFIRM_ERROR) {
		// Detach q.931 call from channel and hangup channel
	}
}

static void visdn_q931_resume_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	enum q931_ie_cause_value cause;

	if (callpvt_to_astchan(q931_call)) {
		ast_log(LOG_WARNING, "Unexpexted ast_chan\n");
		cause = Q931_IE_C_CV_RESOURCES_UNAVAILABLE;
		goto err_ast_chan;
	}

	struct q931_ie_call_identity *ci = NULL;

	int i;
	for (i=0; i<ies->count; i++) {
		if (ies->ies[i]->cls->id == Q931_IE_CALL_IDENTITY) {
			ci = container_of(ies->ies[i],
				struct q931_ie_call_identity, ie);
		}
	}

	struct visdn_intf *intf = q931_call->intf->pvt;
	struct visdn_suspended_call *suspended_call;

	int found = FALSE;
	list_for_each_entry(suspended_call, &intf->suspended_calls, node) {
		if ((!ci && suspended_call->call_identity_len == 0) ||
		    (suspended_call->call_identity_len == ci->data_len &&
		     !memcmp(suspended_call->call_identity, ci->data,
					ci->data_len))) {

			found = TRUE;

			break;
		}
	}

	if (!found) {
		ast_log(LOG_NOTICE, "Unable to find suspended call\n");

		if (list_empty(&intf->suspended_calls))
			cause = Q931_IE_C_CV_SUSPENDED_CALL_EXISTS_BUT_NOT_THIS;
		else
			cause = Q931_IE_C_CV_NO_CALL_SUSPENDED;

		goto err_call_not_found;
	}

	assert(suspended_call->visdn_chan);

	struct visdn_chan *visdn_chan = suspended_call->visdn_chan;

	q931_call->pvt = suspended_call->visdn_chan;
	visdn_chan->q931_call = q931_call_get(q931_call);
	visdn_chan->suspended_call = NULL;

	if (ast_bridged_channel(suspended_call->visdn_chan->ast_chan)) {
		if (!strcmp(ast_bridged_channel(
				suspended_call->visdn_chan->ast_chan)->
								tech->type,
							VISDN_CHAN_TYPE)) {
			// Wow, the remote channel is ISDN too, let's notify it!

			Q931_DECLARE_IES(response_ies);

			struct visdn_chan *remote_visdn_chan =
				to_visdn_chan(
					ast_bridged_channel(suspended_call->
						visdn_chan->ast_chan));

			struct q931_call *remote_call =
					remote_visdn_chan->q931_call;

			struct q931_ie_notification_indicator *notify =
				q931_ie_notification_indicator_alloc();
			notify->description = Q931_IE_NI_D_USER_RESUMED;
			q931_ies_add_put(&response_ies, &notify->ie);

			q931_send_primitive(remote_call,
				Q931_CCB_NOTIFY_REQUEST, &response_ies);

			Q931_UNDECLARE_IES(response_ies);
		}

		ast_moh_stop(ast_bridged_channel(
				suspended_call->visdn_chan->ast_chan));
	}

	{
	Q931_DECLARE_IES(response_ies);

	struct q931_ie_channel_identification *ci =
		q931_ie_channel_identification_alloc();
	ci->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
	ci->interface_type =
		q931_ie_channel_identification_intftype(q931_call->intf);
	ci->preferred_exclusive = Q931_IE_CI_PE_EXCLUSIVE;
	ci->coding_standard = Q931_IE_CI_CS_CCITT;
	q931_chanset_init(&ci->chanset);
	q931_chanset_add(&ci->chanset, suspended_call->q931_chan);
	q931_ies_add_put(&response_ies, &ci->ie);

	q931_send_primitive(q931_call, Q931_CCB_RESUME_RESPONSE, &response_ies);

	Q931_UNDECLARE_IES(response_ies);
	}

	list_del(&suspended_call->node);
	free(suspended_call);

	return;

err_call_not_found:
err_ast_chan:
	;
	Q931_DECLARE_IES(resp_ies);
	struct q931_ie_cause *c = q931_ie_cause_alloc();
	c->coding_standard = Q931_IE_C_CS_CCITT;
	c->location = q931_ie_cause_location_call(q931_call);
	c->value = cause;
	q931_ies_add_put(&resp_ies, &c->ie);

	q931_send_primitive(q931_call,
		Q931_CCB_RESUME_REJECT_REQUEST, &resp_ies);

	Q931_UNDECLARE_IES(resp_ies);

	return;
}

static void visdn_q931_setup_complete_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_setup_complete_indication_status status)
{
	struct visdn_chan *visdn_chan = q931_call->pvt;
	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	ast_queue_control(ast_chan, AST_CONTROL_ANSWER);
}

static void visdn_q931_setup_confirm(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_setup_confirm_status status)
{
	struct visdn_chan *visdn_chan = q931_call->pvt;

	if (!visdn_chan)
		return;

	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	if (q931_call->intf->role == LAPD_INTF_ROLE_NT)
		q931_send_primitive(q931_call,
				Q931_CCB_SETUP_COMPLETE_REQUEST, NULL);

	ast_queue_control(ast_chan, AST_CONTROL_ANSWER);

	visdn_undefer_dtmf_in(visdn_chan);

/*	if (visdn_chan->ec_fd >= 0) {

		visdn_debug("Activating echo canceller\n");

		if (ioctl(visdn_chan->ec_fd, VEC_START,
			(caddr_t)&visdn_chan->ec_ne_channel_id) < 0) {

			ast_log(LOG_ERROR,
				"ioctl(VEC_START): %s\n",
				strerror(errno));
		}
	}*/
}

static int visdn_cgpn_to_pres(
	struct q931_ie_calling_party_number *cgpn)
{
	switch(cgpn->presentation_indicator) {
	case Q931_IE_CGPN_PI_PRESENTATION_ALLOWED:
		return AST_PRES_ALLOWED;
	break;

	case Q931_IE_CGPN_PI_PRESENTATION_RESTRICTED:
		return AST_PRES_RESTRICTED;
	break;

	case Q931_IE_CGPN_PI_NOT_AVAILABLE:
		return AST_PRES_UNAVAILABLE;
	break;
	}

	return 0;
}

static const char *visdn_get_prefix_by_cdpn_ton(
	struct visdn_ic *ic,
	enum q931_ie_called_party_number_type_of_number ton)
{
	switch(ton) {
	case Q931_IE_CDPN_TON_UNKNOWN:
	case Q931_IE_CDPN_TON_RESERVED_FOR_EXT:
		return "";

	case Q931_IE_CDPN_TON_INTERNATIONAL:
		return ic->international_prefix;
	break;

	case Q931_IE_CDPN_TON_NATIONAL:
		return ic->national_prefix;
	break;

	case Q931_IE_CDPN_TON_NETWORK_SPECIFIC:
		return ic->network_specific_prefix;
	break;

	case Q931_IE_CDPN_TON_SUBSCRIBER:
		return ic->subscriber_prefix;
	break;

	case Q931_IE_CDPN_TON_ABBREVIATED:
		return ic->abbreviated_prefix;
	break;
	}

	assert(0);
	return NULL;
}

static const char *visdn_get_prefix_by_cgpn_ton(
	struct visdn_ic *ic,
	enum q931_ie_calling_party_number_type_of_number ton)
{
	switch(ton) {
	case Q931_IE_CGPN_TON_UNKNOWN:
	case Q931_IE_CGPN_TON_RESERVED_FOR_EXT:
		return "";

	case Q931_IE_CGPN_TON_INTERNATIONAL:
		return ic->international_prefix;
	break;

	case Q931_IE_CGPN_TON_NATIONAL:
		return ic->national_prefix;
	break;

	case Q931_IE_CGPN_TON_NETWORK_SPECIFIC:
		return ic->network_specific_prefix;
	break;

	case Q931_IE_CGPN_TON_SUBSCRIBER:
		return ic->subscriber_prefix;
	break;

	case Q931_IE_CGPN_TON_ABBREVIATED:
		return ic->abbreviated_prefix;
	break;
	}

	assert(0);
	return NULL;
}

static void visdn_rewrite_and_assign_cli(
	struct visdn_chan *visdn_chan,
	struct visdn_ic *ic,
	struct q931_ie_calling_party_number *cgpn)
{
	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	assert(!ast_chan->cid.cid_num);

	if (ic->cli_rewriting) {
		char rewritten_num[32];

		snprintf(rewritten_num, sizeof(rewritten_num),
			"%s%s",
			visdn_get_prefix_by_cgpn_ton(ic,
						cgpn->type_of_number),
			cgpn->number);

		ast_chan->cid.cid_num = strdup(rewritten_num);
	} else {
		ast_chan->cid.cid_num = strdup(cgpn->number);
	}
}

static void visdn_handle_clip_nt(
	struct visdn_chan *visdn_chan,
	struct visdn_ic *ic,
	struct q931_ie_calling_party_number *cgpn)
{
	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	/* If the numbering plan is incorrect ignore the information
	 * element. ETS 300 092 Par. 9.3.1
	 */

	if (!cgpn) {
		ast_chan->cid.cid_num = strdup(ic->clip_default_number);
		ast_chan->cid.cid_pres = AST_PRES_NETWORK_NUMBER;

		return;
	}

	if (cgpn->numbering_plan_identificator !=
			Q931_IE_CGPN_NPI_UNKNOWN &&
	    cgpn->numbering_plan_identificator !=
	    		Q931_IE_CGPN_NPI_ISDN_TELEPHONY) {

		ast_chan->cid.cid_num = strdup(ic->clip_default_number);
		ast_chan->cid.cid_pres = AST_PRES_NETWORK_NUMBER;

		return;
	}

	if (ic->clip_special_arrangement) {
		ast_chan->cid.cid_pres |=
			AST_PRES_USER_NUMBER_UNSCREENED;
	} else {
		if (visdn_numbers_list_match(&ic->clip_numbers_list,
							cgpn->number)) {
			if (0) { /* Sequence is valid but incomplete */
				/* Complete sequence TODO FIXME */
			}

			visdn_rewrite_and_assign_cli(visdn_chan, ic, cgpn);

			ast_chan->cid.cid_pres |=
				AST_PRES_USER_NUMBER_PASSED_SCREEN;
		} else {
			visdn_chan_debug(visdn_chan,
				"Specified CLI did not pass screening\n");

			ast_chan->cid.cid_num =
				strdup(ic->clip_default_number);
			ast_chan->cid.cid_pres |= AST_PRES_NETWORK_NUMBER;
		}
	}
}

struct visdn_chan *visdn_alloc_inbound_call(struct q931_call *q931_call)
{
	struct visdn_chan *visdn_chan;
	visdn_chan = visdn_chan_alloc();
	if (!visdn_chan) {
		ast_log(LOG_ERROR, "Cannot allocate visdn_chan\n");
		goto err_visdn_chan_alloc;
	}

	visdn_chan->ast_chan = visdn_ast_chan_alloc(visdn_chan,
					AST_STATE_RING, q931_call);
	if (!visdn_chan->ast_chan)
		goto err_visdn_ast_chan_alloc;

	struct visdn_intf *intf = q931_call->intf->pvt;
	struct visdn_ic *ic = intf->current_ic;

	visdn_chan->q931_call = q931_call_get(q931_call);
	visdn_chan->ic = visdn_ic_get(ic);

/*	ast_dsp_digitmode(visdn_chan->dsp,
		DSP_DIGITMODE_DTMF |
		visdn_chan->ic->dtmf_quelch ? 0 : DSP_DIGITMODE_NOQUELCH |
		visdn_chan->ic->dtmf_mutemax ? DSP_DIGITMODE_MUTEMAX : 0 |
		visdn_chan->ic->dtmf_relax ? DSP_DIGITMODE_RELAXDTMF : 0);*/

	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	ast_chan->rings = 1;

	q931_call->pvt = visdn_chan_get(visdn_chan);

	strncpy(ast_chan->context,
		ic->context,
		sizeof(ast_chan->context)-1);

	strcpy(ast_chan->exten, "s");
	ast_chan->priority = 1;

	//strncpy(ast_chan->language, ic->language, sizeof(ast_chan->language)); FIXME

	return visdn_chan;

	visdn_chan_put(visdn_chan->ast_chan->tech_pvt);
	visdn_chan->ast_chan->tech_pvt = NULL;
	ast_channel_free(visdn_chan->ast_chan);
	visdn_chan->ast_chan = NULL;
err_visdn_ast_chan_alloc:
	visdn_chan_put(q931_call->pvt);
	q931_call->pvt = NULL;
	visdn_ic_put(visdn_chan->ic);
	visdn_chan->ic = NULL;
	q931_call_put(visdn_chan->q931_call);
	visdn_chan->q931_call = NULL;
err_visdn_chan_alloc:

	return NULL;
}

static void visdn_q931_setup_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	assert(!q931_call->pvt);

	struct visdn_chan *visdn_chan;
	visdn_chan = visdn_alloc_inbound_call(q931_call);
	if (!visdn_chan) {

		Q931_DECLARE_IES(ies);

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(q931_call);
		cause->value = Q931_IE_C_CV_RESOURCES_UNAVAILABLE;
		q931_ies_add_put(&ies, &cause->ie);

		q931_send_primitive(visdn_chan->q931_call,
			Q931_CCB_REJECT_REQUEST, &ies);

		Q931_UNDECLARE_IES(ies);

		goto err_visdn_chan_alloc;
	}

	struct visdn_ic *ic = visdn_chan->ic;
	struct ast_channel *ast_chan = visdn_chan->ast_chan;
	struct visdn_intf *intf = q931_call->intf->pvt;

	struct q931_ie_calling_party_number *cgpn = NULL;
	struct q931_ie_called_party_number *cdpn = NULL;
	struct q931_ie_bearer_capability *bc = NULL;
	struct q931_ie_high_layer_compatibility *hlc = NULL;
	struct q931_ie_low_layer_compatibility *llc = NULL;

	int i;
	for(i=0; i<ies->count; i++) {
		if (ies->ies[i]->cls->id == Q931_IE_SENDING_COMPLETE) {
			visdn_chan->sending_complete = TRUE;
		} else if (ies->ies[i]->cls->id ==
				Q931_IE_CALLED_PARTY_NUMBER) {

			cdpn = container_of(ies->ies[i],
				struct q931_ie_called_party_number, ie);

		} else if (ies->ies[i]->cls->id ==
				Q931_IE_CALLING_PARTY_NUMBER) {

			cgpn = container_of(ies->ies[i],
				struct q931_ie_calling_party_number, ie);

		} else if (ies->ies[i]->cls->id == Q931_IE_BEARER_CAPABILITY) {

			bc = container_of(ies->ies[i],
				struct q931_ie_bearer_capability, ie);
		} else if (ies->ies[i]->cls->id ==
					Q931_IE_HIGH_LAYER_COMPATIBILITY) {

			hlc = container_of(ies->ies[i],
				struct q931_ie_high_layer_compatibility, ie);
		} else if (ies->ies[i]->cls->id ==
					Q931_IE_LOW_LAYER_COMPATIBILITY) {

			llc = container_of(ies->ies[i],
				struct q931_ie_low_layer_compatibility, ie);
		}
	}

	/* Bearer Capability is a mandatory IE. q.931 should never let us
	 * receive a message coming from an invalid frame
	 */
	assert(bc);

	char called_number[32] = "";

	if (cdpn) {
		snprintf(called_number, sizeof(called_number),
			"%s%s",
			visdn_get_prefix_by_cdpn_ton(ic,
						cdpn->type_of_number),
			cdpn->number);

		if (cdpn->number[strlen(cdpn->number) - 1] == '#')
			visdn_chan->sending_complete = TRUE;
	}

	/* ------ Handle Bearer Capability ------ */

	/* We should check the destination bearer capability
	 * unfortunately we don't know if the destination is
	 * compatible until we start the PBX... this is a
	 * design flaw in Asterisk
	 */

	{
		__u8 buf[20];
		char raw_bc_text[sizeof(buf) + 1];

		assert(bc->ie.cls->write_to_buf);

		int len = bc->ie.cls->write_to_buf(&bc->ie, buf, sizeof(buf));
		assert(len >= 0);

		for (i=0; i<len; i++)
			sprintf(raw_bc_text + i * 2, "%02x", buf[i]);

		pbx_builtin_setvar_helper(ast_chan,
			"_BEARERCAP_RAW", raw_bc_text);
	}

	if (!visdn_chan_update_bc_cache(visdn_chan, bc)) {

		visdn_chan_debug(visdn_chan,
			"Unsupported bearer capability, rejecting call\n");

		Q931_DECLARE_IES(ies);

		struct q931_ie_cause *cause = q931_ie_cause_alloc();
		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(q931_call);
		cause->value = Q931_IE_C_CV_INCOMPATIBLE_DESTINATION;
		q931_ies_add_put(&ies, &cause->ie);

		q931_send_primitive(visdn_chan->q931_call,
			Q931_CCB_REJECT_REQUEST, &ies);

		Q931_UNDECLARE_IES(ies);

		goto err_unsupported_bearercap;
	}

	if (visdn_chan->supports_inband_tones)
		q931_call->tones_option = ic->tones_option;
	else
		q931_call->tones_option = FALSE;

	q931_ie_get(&bc->ie);
	visdn_chan->bc = bc;

	/* ------ Handle HLC ------ */
	if (hlc) {
		__u8 buf[20];
		char raw_hlc_text[sizeof(buf) + 1];

		assert(hlc->ie.cls->write_to_buf);

		int len = hlc->ie.cls->write_to_buf(&hlc->ie, buf, sizeof(buf));

		for (i=0; i<len; i++)
			sprintf(raw_hlc_text + i * 2, "%02x", buf[i]);

		pbx_builtin_setvar_helper(ast_chan,
			"_HLC_RAW", raw_hlc_text);
	}

	/* ------ Handle LLC ------ */
	if (llc) {
		__u8 buf[20];
		char raw_llc_text[sizeof(buf) + 1];

		assert(llc->ie.cls->write_to_buf);

		int len = llc->ie.cls->write_to_buf(&llc->ie, buf, sizeof(buf));

		for (i=0; i<len; i++)
			sprintf(raw_llc_text + i * 2, "%02x", buf[i]);

		pbx_builtin_setvar_helper(ast_chan,
			"_LLC_RAW", raw_llc_text);
	}

	/* ------ Handle Calling Line Presentation/Restriction ------ */

	assert(!ast_chan->cid.cid_name);
	assert(!ast_chan->cid.cid_num);

	ast_chan->cid.cid_pres = 0;
	ast_chan->cid.cid_name = strdup(ic->clip_default_name);

	if (intf->q931_intf->role == LAPD_INTF_ROLE_NT) {

		visdn_handle_clip_nt(visdn_chan, ic, cgpn);

		/* Handle CLIR */
		if (ic->clir_mode == VISDN_CLIR_MODE_RESTRICTED)
			ast_chan->cid.cid_pres |= AST_PRES_RESTRICTED;
		else if (ic->clir_mode == VISDN_CLIR_MODE_UNRESTRICTED)
			ast_chan->cid.cid_pres |= AST_PRES_ALLOWED;
		else {
			if (cgpn)
				ast_chan->cid.cid_pres |=
					visdn_cgpn_to_pres(cgpn);
			else if (ic->clir_mode ==
					VISDN_CLIR_MODE_RESTRICTED_DEFAULT)
				ast_chan->cid.cid_pres |= AST_PRES_RESTRICTED;
			else
				ast_chan->cid.cid_pres |= AST_PRES_ALLOWED;
		}

	} else {
		if (!cgpn) {
			ast_chan->cid.cid_pres =
				AST_PRES_UNAVAILABLE |
				AST_PRES_NETWORK_NUMBER;

			goto no_cgpn;
		}

		visdn_rewrite_and_assign_cli(visdn_chan, ic, cgpn);

		switch(cgpn->screening_indicator) {
		case Q931_IE_CGPN_SI_USER_PROVIDED_NOT_SCREENED:
			ast_chan->cid.cid_pres |=
				AST_PRES_USER_NUMBER_UNSCREENED;
		break;

		case Q931_IE_CGPN_SI_USER_PROVIDED_VERIFIED_AND_PASSED:
			ast_chan->cid.cid_pres |=
				AST_PRES_USER_NUMBER_PASSED_SCREEN;
		break;

		case Q931_IE_CGPN_SI_USER_PROVIDED_VERIFIED_AND_FAILED:
			ast_chan->cid.cid_pres |=
				AST_PRES_USER_NUMBER_FAILED_SCREEN;
		break;

		case Q931_IE_CGPN_SI_NETWORK_PROVIDED:
			ast_chan->cid.cid_pres |=
				AST_PRES_NETWORK_NUMBER;
		break;
		}

		switch(cgpn->presentation_indicator) {
		case Q931_IE_CGPN_PI_PRESENTATION_ALLOWED:
			ast_chan->cid.cid_pres |=
				AST_PRES_ALLOWED;
		break;

		case Q931_IE_CGPN_PI_PRESENTATION_RESTRICTED:
			ast_chan->cid.cid_pres |=
				AST_PRES_RESTRICTED;
		break;

		case Q931_IE_CGPN_PI_NOT_AVAILABLE:
			ast_chan->cid.cid_pres |=
				AST_PRES_UNAVAILABLE;
		break;
		}

no_cgpn:;
	}

	if (cgpn) {
		/* They appear to have the same values (!) */
		ast_chan->cid.cid_ton = cgpn->type_of_number;
	}

	/* ------ ----------------------------- ------ */

	if (!ic->overlap_sending ||
	    visdn_chan->sending_complete) {

		if (!strlen(called_number))
			strcpy(called_number, "s");

		if (ast_exists_extension(NULL, ic->context,
				called_number, 1,
				ast_chan->cid.cid_num)) {

			strncpy(ast_chan->exten,
				called_number,
				sizeof(ast_chan->exten)-1);

			assert(!ast_chan->cid.cid_dnid);
			ast_chan->cid.cid_dnid = strdup(called_number);

			ast_setstate(ast_chan, AST_STATE_RING);

			// Prevents race conditions after pbx_start
			ast_mutex_lock(&ast_chan->lock);

			if (ast_pbx_start(ast_chan)) {
				ast_log(LOG_ERROR,
					"Unable to start PBX on %s\n",
					ast_chan->name);
				ast_mutex_unlock(&ast_chan->lock);
				ast_hangup(ast_chan);

				Q931_DECLARE_IES(ies);

				struct q931_ie_cause *cause =
					q931_ie_cause_alloc();
				cause->coding_standard = Q931_IE_C_CS_CCITT;
				cause->location =
					q931_ie_cause_location_call(q931_call);
				cause->value =
					Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER;

				q931_ies_add_put(&ies, &cause->ie);

				q931_send_primitive(visdn_chan->q931_call,
					Q931_CCB_REJECT_REQUEST, &ies);

				Q931_UNDECLARE_IES(ies);
			} else {
				q931_send_primitive(visdn_chan->q931_call,
					Q931_CCB_PROCEEDING_REQUEST, NULL);

				ast_dsp_set_features(visdn_chan->dsp,
					DSP_FEATURE_DTMF_DETECT |
					DSP_FEATURE_FAX_DETECT);

				ast_setstate(ast_chan, AST_STATE_RING);

				ast_mutex_unlock(&ast_chan->lock);
			}
		} else {
			ast_log(LOG_NOTICE,
				"No extension '%s' in context '%s',"
				" rejecting call\n",
				called_number,
				ic->context);

			Q931_DECLARE_IES(ies);

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location =
				q931_ie_cause_location_call(q931_call);

			cause->value = Q931_IE_C_CV_INCOMPATIBLE_DESTINATION;
			q931_ies_add_put(&ies, &cause->ie);

			q931_send_primitive(visdn_chan->q931_call,
				Q931_CCB_REJECT_REQUEST, &ies);

			ast_hangup(ast_chan);

			Q931_UNDECLARE_IES(ies);
		}
	} else {
		strncpy(ast_chan->exten, "s",
			sizeof(ast_chan->exten)-1);

		ast_mutex_lock(&ast_chan->lock);

		if (ast_pbx_start(ast_chan)) {
			ast_log(LOG_ERROR,
				"Unable to start PBX on %s\n",
				ast_chan->name);
			ast_mutex_unlock(&ast_chan->lock);
			ast_hangup(ast_chan);

			Q931_DECLARE_IES(ies_proc);
			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location =
				q931_ie_cause_location_call(q931_call);

			cause->value = Q931_IE_C_CV_DESTINATION_OUT_OF_ORDER;
			q931_ies_add_put(&ies_proc, &cause->ie);

			Q931_DECLARE_IES(ies_disc);
			if (visdn_chan->supports_inband_tones) {
				struct q931_ie_progress_indicator *pi =
					q931_ie_progress_indicator_alloc();
				pi->coding_standard = Q931_IE_PI_CS_CCITT;
				pi->location =
					q931_ie_progress_indicator_location(
							visdn_chan->q931_call);
				pi->progress_description =
					Q931_IE_PI_PD_IN_BAND_INFORMATION;
				q931_ies_add_put(&ies_disc, &pi->ie);
			}

			q931_send_primitive(visdn_chan->q931_call,
				Q931_CCB_PROCEEDING_REQUEST, &ies_proc);
			q931_send_primitive(visdn_chan->q931_call,
				Q931_CCB_DISCONNECT_REQUEST, &ies_disc);

			Q931_UNDECLARE_IES(ies_disc);
			Q931_UNDECLARE_IES(ies_proc);

			return;
		}

#if 0 // Don't be tempted :^)
		Q931_DECLARE_IES(ies);
		struct q931_ie_display *disp = q931_ie_display_alloc();
		strcpy(disp->text, "Mark Spencer Sucks");
		q931_ies_add_put(&ies, &disp->ie);

		q931_send_primitive(visdn_chan->q931_call,
			Q931_CCB_MORE_INFO_REQUEST, &ies);

		Q931_UNDECLARE_IES(ies);
#endif

		q931_send_primitive(visdn_chan->q931_call,
			Q931_CCB_MORE_INFO_REQUEST, NULL);

		for(i=0; called_number[i]; i++) {
			struct ast_frame f =
				{ AST_FRAME_DTMF, called_number[i] };

			ast_queue_frame(ast_chan, &f);
		}

		ast_mutex_unlock(&ast_chan->lock);
	}

	visdn_chan_put(visdn_chan);

	return;

err_unsupported_bearercap:
	visdn_chan_put(visdn_chan);
err_visdn_chan_alloc:

	return;
}

static void visdn_q931_status_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_status_indication_status status)
{
}

static void visdn_q931_suspend_confirm(
	struct q931_call *q931_call,
	const struct q931_ies *ies,
	enum q931_suspend_confirm_status status)
{
	if (status == Q931_SUSPEND_CONFIRM_OK) {
		// Detach q.931 call from channel and hangup channel
	}
}

struct visdn_dual {
	struct visdn_chan *visdn_chan_1;
	struct visdn_chan *visdn_chan_2;
};

static void visdn_q931_suspend_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
	struct visdn_chan *visdn_chan = q931_call->pvt;
	enum q931_ie_cause_value cause;

	if (!visdn_chan) {
		ast_log(LOG_WARNING, "Unexpexted ast_chan missing\n");
		cause = Q931_IE_C_CV_RESOURCES_UNAVAILABLE;
		goto err_ast_chan;
	}

	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	struct q931_ie_call_identity *ci = NULL;

	int i;
	for (i=0; i<ies->count; i++) {
		if (ies->ies[i]->cls->id == Q931_IE_CALL_IDENTITY) {
			ci = container_of(ies->ies[i],
				struct q931_ie_call_identity, ie);
		}
	}

	struct visdn_ic *ic = visdn_chan->ic;
	struct visdn_intf *intf = ic->intf;
	struct visdn_suspended_call *suspended_call;
	list_for_each_entry(suspended_call, &intf->suspended_calls, node) {
		if ((!ci && suspended_call->call_identity_len == 0) ||
		    (ci && suspended_call->call_identity_len == ci->data_len &&
		     !memcmp(suspended_call->call_identity,
					ci->data, ci->data_len))) {

			cause = Q931_IE_C_CV_CALL_IDENITY_IN_USE;

			goto err_call_identity_in_use;
		}
	}

	suspended_call = malloc(sizeof(*suspended_call));
	if (!suspended_call) {
		cause = Q931_IE_C_CV_RESOURCES_UNAVAILABLE;
		goto err_suspend_alloc;
	}

	suspended_call->visdn_chan = visdn_chan;
	suspended_call->q931_chan = q931_call->channel;

	if (ci) {
		suspended_call->call_identity_len = ci->data_len;
		memcpy(suspended_call->call_identity, ci->data, ci->data_len);
	} else {
		suspended_call->call_identity_len = 0;
	}

	suspended_call->old_when_to_hangup = ast_chan->whentohangup;

	list_add_tail(&suspended_call->node, &intf->suspended_calls);

	q931_send_primitive(q931_call, Q931_CCB_SUSPEND_RESPONSE, NULL);

	if (ast_bridged_channel(ast_chan)) {
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
		ast_moh_start(ast_bridged_channel(ast_chan), NULL);
#else
		ast_moh_start(ast_bridged_channel(ast_chan), NULL, NULL);
#endif

		if (!strcmp(ast_bridged_channel(ast_chan)->tech->type,
							VISDN_CHAN_TYPE)) {
			// Wow, the remote channel is ISDN too, let's notify it!

			Q931_DECLARE_IES(response_ies);

			struct visdn_chan *remote_visdn_chan =
				to_visdn_chan(ast_bridged_channel(ast_chan));

			struct q931_call *remote_call =
					remote_visdn_chan->q931_call;

			struct q931_ie_notification_indicator *notify =
				q931_ie_notification_indicator_alloc();
			notify->description = Q931_IE_NI_D_USER_SUSPENDED;
			q931_ies_add_put(&response_ies, &notify->ie);

			q931_send_primitive(remote_call,
				Q931_CCB_NOTIFY_REQUEST, &response_ies);

			Q931_UNDECLARE_IES(response_ies);
		}
	}

	if (!ast_chan->whentohangup ||
	    time(NULL) + 45 < ast_chan->whentohangup)
		ast_channel_setwhentohangup(ast_chan, ic->T307);

	q931_call->pvt = NULL;
	visdn_chan->q931_call = NULL;
	visdn_chan->suspended_call = suspended_call;
	q931_call_put(q931_call);

	return;

err_suspend_alloc:
err_call_identity_in_use:
err_ast_chan:
	;
	Q931_DECLARE_IES(resp_ies);
	struct q931_ie_cause *c = q931_ie_cause_alloc();
	c->coding_standard = Q931_IE_C_CS_CCITT;
	c->location = q931_ie_cause_location_call(q931_call);
	c->value = cause;
	q931_ies_add_put(&resp_ies, &c->ie);

	q931_send_primitive(visdn_chan->q931_call,
		Q931_CCB_SUSPEND_REJECT_REQUEST, &resp_ies);

	Q931_UNDECLARE_IES(resp_ies);

	return;
}

static void visdn_q931_timeout_indication(
	struct q931_call *q931_call,
	const struct q931_ies *ies)
{
}

#if 0
static int visdn_connect_channels_with_ec(
	struct visdn_chan *visdn_chan)
{
	visdn_debug("Connecting userport %06d to chan %06d via EC\n",
			visdn_chan->up_channel_id,
			visdn_chan->bearer_channel_id);

	visdn_chan->ec_fd = open("/dev/visdn/ec-control", O_RDWR);
	if (visdn_chan->ec_fd < 0) {
		ast_log(LOG_ERROR,
			"Cannot open ec-control: %s\n",
			strerror(errno));
		goto err_open;
	}

/*	if (ioctl(visdn_chan->ec_fd, VEC_SET_TAPS,
		intf->echocancel_taps) < 0) {

		ast_log(LOG_ERROR,
			"ioctl(VEC_SET_TAPS): %s\n",
			strerror(errno));
		goto err_ioctl;
	}*/

	if (ioctl(visdn_chan->ec_fd, VEC_GET_NEAREND_CHANID,
		(caddr_t)&visdn_chan->ec_ne_channel_id) < 0) {

		ast_log(LOG_ERROR,
			"ioctl(VEC_GET_NEAREND_ID): %s\n",
			strerror(errno));
		goto err_ioctl;
	}

	if (ioctl(visdn_chan->ec_fd, VEC_GET_FAREND_CHANID,
		(caddr_t)&visdn_chan->ec_fe_channel_id) < 0) {

		ast_log(LOG_ERROR,
			"ioctl(VEC_GET_FAREND_ID): %s\n",
			strerror(errno));
		goto err_ioctl;
	}

	visdn_debug("EC near_end=%06d far_end=%06d\n",
			visdn_chan->ec_ne_channel_id,
			visdn_chan->ec_fe_channel_id);

	struct visdn_connect vc;
	memset(&vc, 0, sizeof(vc));
	vc.from_endpoint_id = visdn_chan->up_channel_id;
	vc.to_endpoint_id = visdn_chan->ec_fe_channel_id;

	if (ioctl(visdn.router_control_fd, VISDN_IOC_CONNECT,
	    (caddr_t) &vc) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VISDN_CONNECT, sp, ec_fe): %s\n",
			strerror(errno));
		goto err_ioctl_connect_up_ec;
	}

	visdn_chan->up_pipeline_id = vc.pipeline_id;

	memset(&vc, 0, sizeof(vc));
	vc.from_endpoint_id = visdn_chan->ec_ne_channel_id;
	vc.to_endpoint_id = visdn_chan->bearer_channel_id;

	if (ioctl(visdn.router_control_fd, VISDN_IOC_CONNECT,
	    (caddr_t) &vc) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VISDN_CONNECT, ec_ne, bearer): %s\n",
			strerror(errno));
		goto err_ioctl_connect_ec_b;
	}

	visdn_chan->bearer_pipeline_id = vc.pipeline_id;

	memset(&vc, 0, sizeof(vc));
	vc.pipeline_id = visdn_chan->up_pipeline_id;

	if (ioctl(visdn.router_control_fd, VISDN_IOC_PIPELINE_OPEN,
						 (caddr_t)&vc) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VISDN_PIPELINE_OPEN, sp): %s\n",
			strerror(errno));
		goto err_ioctl_enable_up;
	}

	memset(&vc, 0, sizeof(vc));
	vc.pipeline_id = visdn_chan->bearer_pipeline_id;

	if (ioctl(visdn.router_control_fd, VISDN_IOC_PIPELINE_OPEN,
						 (caddr_t)&vc) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VISDN_PIPELINE_OPEN, bearer): %s\n",
			strerror(errno));
		goto err_ioctl_enable_b;
	}



	memset(&vc, 0, sizeof(vc));
	vc.pipeline_id = visdn_chan->up_pipeline_id;

	if (ioctl(visdn.router_control_fd, VISDN_IOC_PIPELINE_START,
						 (caddr_t)&vc) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VISDN_PIPELINE_START, sp): %s\n",
			strerror(errno));
		goto err_ioctl_enable_up;
	}

	memset(&vc, 0, sizeof(vc));
	vc.pipeline_id = visdn_chan->bearer_pipeline_id;

	if (ioctl(visdn.router_control_fd, VISDN_IOC_PIPELINE_START,
						 (caddr_t)&vc) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VISDN_PIPELINE_START, bearer): %s\n",
			strerror(errno));
		goto err_ioctl_enable_b;
	}

	if (visdn_chan->q931_call->state == N10_ACTIVE ||
	    visdn_chan->q931_call->state == U10_ACTIVE) {
		visdn_debug("Activating echo canceller\n");

		if (ioctl(visdn_chan->ec_fd, VEC_START,
			(caddr_t)&visdn_chan->ec_ne_channel_id) < 0) {

			ast_log(LOG_ERROR,
				"ioctl(VEC_START): %s\n",
				strerror(errno));
		}
	}

	return 0;

err_ioctl_connect_up_ec:
err_ioctl_connect_ec_b:
err_ioctl_enable_up:
err_ioctl_enable_b:
err_ioctl:
	close(visdn_chan->up_fd);
err_open:

	return -1;
}
#endif

static int visdn_pipeline_set_octet_reverser(
	struct ks_pipeline *pipeline,
	BOOL enabled)
{
	/* TODO: Do this only once */
	struct ks_feature *octet_reverser_attr;

	octet_reverser_attr = ks_feature_get_by_name(ks_conn, "octet_reverser");
	if (!octet_reverser_attr) {
		ast_log(LOG_ERROR,
			"Cannot find octet reverser attr\n");
		goto err_missing_octet_reverser;
	}

	struct ks_octet_reverser_descr *octet_reverser = NULL;

	int i;
	for(i=0; i<pipeline->chans_cnt; i++) {
		struct ks_chan *chan = pipeline->chans[i];
		struct ks_feature_value *featval;

		list_for_each_entry(featval, &chan->features, node) {

			if (featval->feature == octet_reverser_attr) {

				struct ks_octet_reverser_descr *descr =
					(struct ks_octet_reverser_descr *)
					featval->payload;

				if (!octet_reverser || descr->hardware)
					octet_reverser = descr;
			}
		}
	}

	if (!octet_reverser) {
		ast_log(LOG_ERROR,
			"Cannot find octet reverser along the pipeline\n");
		goto err_missing_octet_reverser_in_pipeline;
	}

	octet_reverser->enabled = enabled;

	return 0;

err_missing_octet_reverser_in_pipeline:
err_missing_octet_reverser:

	return -1;
}


static void visdn_q931_connect_channel(
	struct q931_channel *channel)
{
	assert(channel);
	assert(channel->call);

	struct visdn_chan *visdn_chan = channel->call->pvt;
	struct ast_channel *ast_chan = visdn_chan->ast_chan;
	struct visdn_ic *ic = visdn_chan->ic;
	int err;

	if (!visdn_chan)
		return;

	ast_mutex_lock(&ast_chan->lock);

	visdn_chan->channel_has_been_connected = TRUE;

	if (!visdn_chan->handle_stream) {
		ast_mutex_unlock(&ast_chan->lock);
		return;
	}

	char dest[100];
	snprintf(dest, sizeof(dest),
		"%s/%s%d",
		ic->intf->remote_port,
		ic->intf->q931_intf->type == LAPD_INTF_TYPE_BRA ? "B" : "",
		channel->id+1);

//	strncpy(visdn_chan->bearer_node_id, dest,
//			sizeof(visdn_chan->bearer_node_id));

	if (visdn_chan->is_framed)
		visdn_chan->up_fd = open(
			"/dev/ks/userport_framed", O_RDWR);
	else
		visdn_chan->up_fd = open(
			"/dev/ks/userport_stream", O_RDWR);

	if (visdn_chan->up_fd < 0) {
		ast_log(LOG_ERROR,
			"Cannot open userport: %s\n",
			strerror(errno));
		goto err_open_userport;
	}

	ast_chan->fds[0] = visdn_chan->up_fd;

	__u32 up_node_id;
	if (ioctl(visdn_chan->up_fd, KS_UP_GET_NODEID,
				(caddr_t)&up_node_id) < 0) {

		ast_log(LOG_ERROR,
			"ioctl(KS_UP_GET_NODEID): %s\n",
			strerror(errno));
		goto err_get_up_node_id;
	}

	err = ks_conn_remote_topology_lock(ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot lock kstreamer topology: %s\n", strerror(-err));
		goto err_kstreamer_lock;
	}

	visdn_chan->node_bearer = ks_node_get_by_path(ks_conn, dest);
	if (!visdn_chan->node_bearer) {
		ast_log(LOG_ERROR, "Bearer's node not found\n");
		goto err_bearer_node_not_found;
	}

	visdn_chan->node_userport = ks_node_get_by_id(ks_conn, up_node_id);
	if (!visdn_chan->node_userport) {
		ast_log(LOG_ERROR, "Userport's node not found\n");
		goto err_up_node_not_found;
	}

	visdn_chan_debug(visdn_chan,
			"Connecting userport %06d to chan %06d\n",
			visdn_chan->node_userport->id,
			visdn_chan->node_bearer->id);

	/* Create RX pipeline */
	visdn_chan->pipeline_rx = ks_pipeline_alloc();
	if (!visdn_chan->pipeline_rx) {
		ast_log(LOG_ERROR,
			"Cannot allocate pipeline\n");

		goto err_pipeline_rx_alloc;
	}

	err = ks_pipeline_autoroute(visdn_chan->pipeline_rx, ks_conn,
				visdn_chan->node_bearer,
				visdn_chan->node_userport);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot connect nodes: %s\n", strerror(-err));

		goto err_pipeline_rx_connect;
	}

	err = ks_pipeline_create(visdn_chan->pipeline_rx, ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot create RX pipeline: %s\n",
			strerror(-err));
		goto err_pipeline_rx_create;
	}

	err = visdn_pipeline_set_octet_reverser(visdn_chan->pipeline_rx, TRUE);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot enable RX octet reverser\n");
		goto err_pipeline_rx_octet_reverser_enable;
	}

	err = ks_pipeline_update_chans(visdn_chan->pipeline_rx, ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot update pipeline's channels\n");
		goto err_pipeline_rx_update_chans;
	}

	/* Create TX pipeline */
	visdn_chan->pipeline_tx = ks_pipeline_alloc();
	if (!visdn_chan->pipeline_tx) {
		ast_log(LOG_ERROR,
			"Cannot allocate TX pipeline\n");

		goto err_pipeline_tx_alloc;
	}

	err = ks_pipeline_autoroute(visdn_chan->pipeline_tx, ks_conn,
				visdn_chan->node_userport,
				visdn_chan->node_bearer);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot connect TX pipeline's nodes: %s\n",
			strerror(-err));

		goto err_pipeline_tx_connect;
	}

	err = ks_pipeline_create(visdn_chan->pipeline_tx, ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot create pipeline: %s\n",
			strerror(-err));
		goto err_pipeline_tx_create;
	}

	err = ks_conn_remote_topology_unlock(ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Error unlocking kstreamer's topology\n");
	}

	err = visdn_pipeline_set_octet_reverser(visdn_chan->pipeline_tx, TRUE);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot enable TX octet reverser\n");
		goto err_pipeline_tx_octet_reverser_enable;
	}

	err = ks_pipeline_update_chans(visdn_chan->pipeline_tx, ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot update TX pipeline's channels\n");
		goto err_pipeline_tx_update_chans;
	}

	/* Start RX pipelines */
	visdn_chan->pipeline_rx->status = KS_PIPELINE_STATUS_FLOWING;

	err = ks_pipeline_update(visdn_chan->pipeline_rx, ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot start RX pipeline\n");
		goto err_pipeline_rx_update;
	}

	/* Start TX pipelines */
	visdn_chan->pipeline_tx->status = KS_PIPELINE_STATUS_FLOWING;

	err = ks_pipeline_update(visdn_chan->pipeline_tx, ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot start TX pipeline\n");
		goto err_pipeline_tx_update;
	}

		/* FIXME TODO FIXME XXX Handle return value */
/*		if (ic->echocancel)
			visdn_connect_channels_with_ec(visdn_chan);
		else*/
//			visdn_connect_channels(visdn_chan);

	ast_mutex_unlock(&ast_chan->lock);

	return;

err_pipeline_tx_update:
err_pipeline_rx_update:
err_pipeline_tx_update_chans:
err_pipeline_tx_octet_reverser_enable:
err_pipeline_tx_create:
err_pipeline_tx_connect:
	ks_pipeline_put(visdn_chan->pipeline_tx);
	visdn_chan->pipeline_tx = NULL;
err_pipeline_tx_alloc:
err_pipeline_rx_update_chans:
err_pipeline_rx_octet_reverser_enable:
err_pipeline_rx_create:
err_pipeline_rx_connect:
	ks_pipeline_put(visdn_chan->pipeline_rx);
	visdn_chan->pipeline_rx = NULL;
err_pipeline_rx_alloc:

err_up_node_not_found:
err_bearer_node_not_found:
	ks_conn_remote_topology_unlock(ks_conn);
err_kstreamer_lock:
err_get_up_node_id:
	close(visdn_chan->up_fd);
	visdn_chan->up_fd = -1;
err_open_userport:

	ast_mutex_unlock(&ast_chan->lock);

	return;
}

static void visdn_q931_disconnect_channel(
	struct q931_channel *channel)
{
	if (!channel->call)
		return;

	struct visdn_chan *visdn_chan = channel->call->pvt;

	if (!visdn_chan)
		return;

	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	ast_mutex_lock(&ast_chan->lock);
	visdn_disconnect_chan_from_visdn(visdn_chan);
	ast_mutex_unlock(&ast_chan->lock);
}

static void visdn_q931_start_tone(struct q931_channel *channel,
	enum q931_tone_type tone)
{
	struct visdn_chan *visdn_chan = channel->call->pvt;

	// Unfortunately, after ast_hangup the channel is not valid
	// anymore and we cannot generate further tones thought we should
	if (!visdn_chan)
		return;

	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	switch (tone) {
	case Q931_TONE_DIAL:
		ast_indicate(ast_chan, AST_CONTROL_OFFHOOK);
	break;

	case Q931_TONE_HANGUP:
		ast_indicate(ast_chan, AST_CONTROL_HANGUP);
	break;

	case Q931_TONE_BUSY:
		ast_indicate(ast_chan, AST_CONTROL_BUSY);
	break;

	case Q931_TONE_FAILURE:
		ast_indicate(ast_chan, AST_CONTROL_CONGESTION);
	break;
	default:;
	}
}

static void visdn_q931_stop_tone(struct q931_channel *channel)
{
	if (!channel->call)
		return;

	struct visdn_chan *visdn_chan = channel->call->pvt;
	struct ast_channel *ast_chan = visdn_chan->ast_chan;

	if (!visdn_chan)
		return;

	ast_indicate(ast_chan, -1);
}

static void visdn_q931_management_restart_confirm(
	struct q931_global_call *gc,
	const struct q931_chanset *chanset)
{
}

static void visdn_q931_timeout_management_indication(
	struct q931_global_call *gc)
{
}

static void visdn_q931_status_management_indication(
	struct q931_global_call *gc)
{
}

static void visdn_logger(int level, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);

	char msg[200];
	vsnprintf(msg, sizeof(msg), format, ap);
	va_end(ap);

	switch(level) {
	case Q931_LOG_DEBUG:
		visdn_debug_q931("%s", msg);
	break;

	case Q931_LOG_INFO:
		ast_verbose(VERBOSE_PREFIX_2  "%s", msg);
	break;

	case Q931_LOG_NOTICE:
		ast_log(__LOG_NOTICE, "libq931", 0, "", "%s", msg);
	break;

	case Q931_LOG_WARNING:
		ast_log(__LOG_WARNING, "libq931", 0, "", "%s", msg);
	break;

	case Q931_LOG_ERR:
	case Q931_LOG_CRIT:
	case Q931_LOG_ALERT:
	case Q931_LOG_EMERG:
		ast_log(__LOG_ERROR, "libq931", 0, "", "%s", msg);
	break;
	}
}

void visdn_q931_timer_update(void)
{
	pthread_kill(visdn.q931_thread, SIGURG);
}

static void visdn_q931_ccb_receive(void)
{
	struct q931_ccb_message *msg;

	while(1) {
		ast_mutex_lock(&visdn.q931_ccb_queue_lock);

		if (list_empty(&visdn.q931_ccb_queue)) {
			ast_mutex_unlock(&visdn.q931_ccb_queue_lock);
			break;
		}

		msg = list_entry(visdn.q931_ccb_queue.next,
				struct q931_ccb_message, node);

		char buf[1];
		int res = read(visdn.q931_ccb_queue_pipe_read, buf, 1);
		if (res < 0);

		list_del_init(&msg->node);
		ast_mutex_unlock(&visdn.q931_ccb_queue_lock);

		switch (msg->primitive) {
		case Q931_CCB_ALERTING_INDICATION:
			visdn_q931_alerting_indication(msg->call, &msg->ies);
		break;

		case Q931_CCB_CONNECT_INDICATION:
			visdn_q931_connect_indication(msg->call, &msg->ies);
		break;

		case Q931_CCB_DISCONNECT_INDICATION:
			visdn_q931_disconnect_indication(msg->call, &msg->ies);
		break;

		case Q931_CCB_ERROR_INDICATION:
			visdn_q931_error_indication(msg->call, &msg->ies);
		break;

		case Q931_CCB_INFO_INDICATION:
			visdn_q931_info_indication(msg->call, &msg->ies);
		break;

		case Q931_CCB_MORE_INFO_INDICATION:
			visdn_q931_more_info_indication(msg->call, &msg->ies);
		break;

		case Q931_CCB_NOTIFY_INDICATION:
			visdn_q931_notify_indication(msg->call, &msg->ies);
		break;

		case Q931_CCB_PROCEEDING_INDICATION:
			visdn_q931_proceeding_indication(msg->call, &msg->ies);
		break;

		case Q931_CCB_PROGRESS_INDICATION:
			visdn_q931_progress_indication(msg->call, &msg->ies);
		break;

		case Q931_CCB_REJECT_INDICATION:
			visdn_q931_reject_indication(msg->call, &msg->ies);
		break;

		case Q931_CCB_RELEASE_CONFIRM:
			visdn_q931_release_confirm(msg->call, &msg->ies,
								msg->par1);
		break;

		case Q931_CCB_RELEASE_INDICATION:
			visdn_q931_release_indication(msg->call, &msg->ies);
		break;

		case Q931_CCB_RESUME_CONFIRM:
			visdn_q931_resume_confirm(msg->call, &msg->ies,
							msg->par1);
		break;

		case Q931_CCB_RESUME_INDICATION:
			visdn_q931_resume_indication(msg->call, &msg->ies);
		break;

		case Q931_CCB_SETUP_COMPLETE_INDICATION:
			visdn_q931_setup_complete_indication(msg->call,
						&msg->ies, msg->par1);
		break;

		case Q931_CCB_SETUP_CONFIRM:
			visdn_q931_setup_confirm(msg->call, &msg->ies,
						msg->par1);
		break;

		case Q931_CCB_SETUP_INDICATION:
			visdn_q931_setup_indication(msg->call, &msg->ies);
		break;

		case Q931_CCB_STATUS_INDICATION:
			visdn_q931_status_indication(msg->call, &msg->ies,
						msg->par1);
		break;

		case Q931_CCB_SUSPEND_CONFIRM:
			visdn_q931_suspend_confirm(msg->call, &msg->ies,
						msg->par1);
		break;

		case Q931_CCB_SUSPEND_INDICATION:
			visdn_q931_suspend_indication(msg->call, &msg->ies);
		break;

		case Q931_CCB_TIMEOUT_INDICATION:
			visdn_q931_timeout_indication(msg->call, &msg->ies);
		break;

		case Q931_CCB_TIMEOUT_MANAGEMENT_INDICATION:
			visdn_q931_timeout_management_indication(
				(struct q931_global_call *)msg->par1);
		break;

		case Q931_CCB_STATUS_MANAGEMENT_INDICATION:
			visdn_q931_status_management_indication(
				(struct q931_global_call *)msg->par1);
		break;

		case Q931_CCB_MANAGEMENT_RESTART_CONFIRM:
			visdn_q931_management_restart_confirm(
				(struct q931_global_call *)msg->par1,
				(struct q931_chanset *)msg->par2);
		break;

		case Q931_CCB_CONNECT_CHANNEL:
			visdn_q931_connect_channel(
				(struct q931_channel *)msg->par1);
		break;

		case Q931_CCB_DISCONNECT_CHANNEL:
			visdn_q931_disconnect_channel(
				(struct q931_channel *)msg->par1);
		break;

		case Q931_CCB_START_TONE:
			visdn_q931_start_tone(
				(struct q931_channel *)msg->par1, msg->par2);
		break;

		case Q931_CCB_STOP_TONE:
			visdn_q931_stop_tone((struct q931_channel *)msg->par1);
		break;

		default:
			ast_log(LOG_WARNING, "Unexpected primitive %d\n",
				msg->primitive);
		}

		if (msg->call)
			q931_call_put(msg->call);

		q931_ies_destroy(&msg->ies);
		free(msg);
	}
}

/*---------------------------------------------------------------------------*/

static int do_visdn_debug_netlink(int fd, int argc, char *argv[])
{
	visdn.debug_netlink = TRUE;

	ast_cli(fd, "vISDN Netlink debugging enabled\n");

	return RESULT_SUCCESS;
}

static char visdn_debug_netlink_help[] =
"Usage: visdn debug netlink\n"
"\n"
"	Debug NetLink vISDN events\n";

static struct ast_cli_entry visdn_debug_netlink =
{
	{ "visdn", "debug", "netlink", NULL },
	do_visdn_debug_netlink,
	"Enables netlink vISDN debugging",
	visdn_debug_netlink_help,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_visdn_no_debug_netlink(int fd, int argc, char *argv[])
{
	visdn.debug_netlink = FALSE;

	ast_cli(fd, "vISDN NetLink debugging disabled\n");

	return RESULT_SUCCESS;
}

static struct ast_cli_entry visdn_no_debug_netlink =
{
	{ "visdn", "no", "debug", "netlink", NULL },
	do_visdn_no_debug_netlink,
	"Disables NetLink vISDN debugging",
	NULL,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_visdn_debug_q921(int fd, int argc, char *argv[])
{
	// Enable debugging on new DLCs FIXME TODO

	visdn.debug_q921 = TRUE;
	visdn_set_socket_debug(1);

	ast_cli(fd, "vISDN q.921 debugging enabled\n");

	return RESULT_SUCCESS;
}

static char visdn_debug_q921_help[] =
"Usage: visdn debug q921\n"
"\n"
"	Enabled q.921 debugging messages. Since q.921 runs in kernel mode,\n"
"	those messages will appear in the kernel log (dmesg) or syslog.\n";

static struct ast_cli_entry visdn_debug_q921 =
{
	{ "visdn", "debug", "q921", NULL },
	do_visdn_debug_q921,
	"Enables q.921 debugging",
	visdn_debug_q921_help,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_visdn_no_debug_q921(int fd, int argc, char *argv[])
{
	// Disable debugging on new DLCs FIXME TODO

	visdn.debug_q921 = FALSE;
	visdn_set_socket_debug(0);

	ast_cli(fd, "vISDN q.921 debugging disabled\n");

	return RESULT_SUCCESS;
}

static struct ast_cli_entry visdn_no_debug_q921 =
{
	{ "visdn", "no", "debug", "q921", NULL },
	do_visdn_no_debug_q921,
	"Disables q.921 debugging",
	NULL,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_visdn_debug_q931(int fd, int argc, char *argv[])
{
	visdn.debug_q931 = TRUE;

	ast_cli(fd, "vISDN q.931 debugging enabled\n");

	return RESULT_SUCCESS;
}

static char visdn_debug_q931_help[] =
"Usage: visdn debug q931 [interface]\n"
"\n"
"	Enable q.931 process debugging. Messages and state machine events\n"
"	will be directed to the console\n";

static struct ast_cli_entry visdn_debug_q931 =
{
	{ "visdn", "debug", "q931", NULL },
	do_visdn_debug_q931,
	"Enables q.931 debugging",
	visdn_debug_q931_help,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_visdn_no_debug_q931(int fd, int argc, char *argv[])
{
	visdn.debug_q931 = FALSE;

	ast_cli(fd, "vISDN q.931 debugging disabled\n");

	return RESULT_SUCCESS;
}

static struct ast_cli_entry visdn_no_debug_q931 =
{
	{ "visdn", "no", "debug", "q931", NULL },
	do_visdn_no_debug_q931,
	"Disables q.931 debugging",
	NULL,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_visdn_reload(int fd, int argc, char *argv[])
{
	visdn_reload_config();

	return RESULT_SUCCESS;
}

static char visdn_visdn_reload_help[] =
"Usage: visdn reload\n"
"\n"
"	Reloads vISDN's configuration.\n"
"	The reload process is fully non-blocking and can be done while calls\n"
"	are active. Old calls will retain the previous configuration while\n"
"	new ones inherit the new configuration\n";

static struct ast_cli_entry visdn_reload =
{
	{ "visdn", "reload", NULL },
	do_visdn_reload,
	"Reloads vISDN configuration",
	visdn_visdn_reload_help,
	NULL
};

/*---------------------------------------------------------------------------*/

static void visdn_print_call_summary_entry(
	int fd,
	struct q931_call *call)
{
	char idstr[20];
	snprintf(idstr, sizeof(idstr), "%s/%d.%c",
		call->intf->name,
		call->call_reference,
		(call->direction ==
			Q931_CALL_DIRECTION_INBOUND)
				? 'I' : 'O');

	ast_cli(fd, "%-17s %-25s",
		idstr,
		q931_call_state_to_text(call->state));

	struct visdn_chan *visdn_chan = call->pvt;
	if (visdn_chan) {
		ast_cli(fd, "%s%s",
			visdn_chan->number,
			visdn_chan->sending_complete ? "(SC)" : "");
	}

	ast_cli(fd, "\n");
}

static int visdn_cli_print_call_list(
	int fd,
	struct q931_interface *filter_intf)
{
	int first_call;

	struct visdn_intf *intf;
	ast_rwlock_rdlock(&visdn.intfs_list_lock);
	list_for_each_entry(intf, &visdn.intfs_list, node) {

		if (!intf->q931_intf)
			continue;

		struct q931_call *call;
		first_call = TRUE;

		list_for_each_entry(call, &intf->q931_intf->calls, calls_node) {

			if (!filter_intf || call->intf == filter_intf) {

				if (first_call) {
					ast_cli(fd,
						"ID                "
						"State                    "
						"Number\n");
					first_call = FALSE;
				}

				visdn_print_call_summary_entry(fd, call);
			}
		}
	}
	ast_rwlock_unlock(&visdn.intfs_list_lock);

	return RESULT_SUCCESS;
}

static void visdn_cli_print_call_timer_info(
	int fd, struct q931_timer *timer,
	const char *name)
{
	if (timer->pending) {
		longtime_t delay = timer->expires - longtime_now();
		ast_cli(fd, "%s (in %.1f s) ", name, delay / 1000000.0);
	}
}

static void visdn_cli_print_call(int fd, struct q931_call *call)
{
	ast_cli(fd, "--------- Call %s/%d.%s\n",
		call->intf->name,
		call->call_reference,
		call->direction == Q931_CALL_DIRECTION_INBOUND ?
			"inbound" : "outbound");

	if (call->dlc)
		ast_cli(fd, "DLC (TEI)       : %d\n", call->dlc->tei);

	ast_cli(fd, "State           : %s\n",
		q931_call_state_to_text(call->state));

	ast_cli(fd, "Broadcast setup : %s\n",
		call->broadcast_setup ? "Yes" : "No");

	ast_cli(fd, "Tones option    : %s\n",
		call->tones_option ? "Yes" : "No");

	ast_cli(fd, "Active timers   : ");

	visdn_cli_print_call_timer_info(fd, &call->T301, "T301");
	visdn_cli_print_call_timer_info(fd, &call->T302, "T302");
	visdn_cli_print_call_timer_info(fd, &call->T303, "T303");
	visdn_cli_print_call_timer_info(fd, &call->T304, "T304");
	visdn_cli_print_call_timer_info(fd, &call->T305, "T305");
	visdn_cli_print_call_timer_info(fd, &call->T306, "T306");
	visdn_cli_print_call_timer_info(fd, &call->T308, "T308");
	visdn_cli_print_call_timer_info(fd, &call->T309, "T309");
	visdn_cli_print_call_timer_info(fd, &call->T310, "T310");
	visdn_cli_print_call_timer_info(fd, &call->T312, "T312");
	visdn_cli_print_call_timer_info(fd, &call->T313, "T313");
	visdn_cli_print_call_timer_info(fd, &call->T314, "T314");
	visdn_cli_print_call_timer_info(fd, &call->T316, "T316");
	visdn_cli_print_call_timer_info(fd, &call->T318, "T318");
	visdn_cli_print_call_timer_info(fd, &call->T319, "T319");
	visdn_cli_print_call_timer_info(fd, &call->T320, "T320");
	visdn_cli_print_call_timer_info(fd, &call->T321, "T321");
	visdn_cli_print_call_timer_info(fd, &call->T322, "T322");

	ast_cli(fd, "\n");

	ast_cli(fd, "CES:\n");
	struct q931_ces *ces;
	list_for_each_entry(ces, &call->ces, node) {
		ast_cli(fd, "%d %s %s ",
			ces->dlc->tei,
			q931_ces_state_to_text(ces->state),
			(ces == call->selected_ces ? "presel" :
			  (ces == call->preselected_ces ? "sel" : "")));

		if (ces->T304.pending) ast_cli(fd, "T304 ");
		if (ces->T308.pending) ast_cli(fd, "T308 ");
		if (ces->T322.pending) ast_cli(fd, "T322 ");

		ast_cli(fd, "\n");
	}

	struct visdn_chan *visdn_chan = call->pvt;
	if (visdn_chan) {
		ast_cli(fd, "------ Asterisk Channel\n");

		ast_cli(fd,
			"Number               : %s%s\n"
			"Options              : %s\n"
			"Supports inband tones: %s\n"
			"Handle stream        : %s\n"
			"Streamport NodeID    : 0x%08x\n"
//			"EC NearEnd Chanid    : %06d\n"
//			"EC FarEnd Chanid     : %06d\n"
			"Bearer NodeID        : 0x%08x\n"
			"In-band informations : %s\n"
			"DTMF Deferred        : %s\n",
			visdn_chan->number,
			visdn_chan->sending_complete ? " (SC)" : "",
			visdn_chan->options,
			visdn_chan->supports_inband_tones ? "Yes" : "No",
			visdn_chan->handle_stream ? "Yes" : "No",
			visdn_chan->node_userport ?
				visdn_chan->node_userport->id : -1,
//			visdn_chan->ec_ne_channel_id,
//			visdn_chan->ec_fe_channel_id,
			visdn_chan->node_bearer ?
				visdn_chan->node_bearer->id : -1,
			visdn_chan->inband_info ? "Yes" : "No",
			visdn_chan->dtmf_deferred ? "Yes" : "No");

		if (strlen(visdn_chan->dtmf_queue))
			ast_cli(fd, "DTMF Queue           : %s\n",
				visdn_chan->dtmf_queue);
	}

}

static char *visdn_show_calls_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	int which = 0;

	if (pos != 3)
		return NULL;

	char *word_dup = strdupa(word);
	char *slashpos = strchr(word_dup, '/');

	if (!slashpos) {
		struct visdn_intf *intf;
		ast_rwlock_rdlock(&visdn.intfs_list_lock);
		list_for_each_entry(intf, &visdn.intfs_list, node) {

			if (!strncasecmp(word, intf->name, strlen(word))) {
				if (++which > state) {
					ast_rwlock_unlock(
						&visdn.intfs_list_lock);
					return strdup(intf->name);
				}
			}
		}
		ast_rwlock_unlock(&visdn.intfs_list_lock);
	} else {
		*slashpos = '\0';
		struct visdn_intf *intf = visdn_intf_get_by_name(word_dup);
		if (!intf)
			return NULL;

		struct q931_call *call;
		list_for_each_entry(call, &intf->q931_intf->calls, calls_node) {
			char callid[64];
			snprintf(callid, sizeof(callid), "%s/%d.%c",
				intf->name,
				call->call_reference,
				call->direction ==
				Q931_CALL_DIRECTION_INBOUND ? 'I' : 'O');

			if (!strncasecmp(word, callid, strlen(word))) {
				if (++which > state)
					return strdup(callid);
			}
		}
	}

	return NULL;
}

static int visdn_show_calls_func(int fd, int argc, char *argv[])
{
	if (argc < 4) {
		visdn_cli_print_call_list(fd, NULL);
		return RESULT_SUCCESS;
	}

	const char *intf_name;
	char *callid = NULL;

	const char *slashpos = strchr(argv[3], '/');
	if (slashpos) {
		intf_name = strndupa(argv[3], slashpos - argv[3]);
		callid = strdupa(slashpos + 1);
	} else {
		intf_name = argv[3];
	}

	struct visdn_intf *filter_intf = NULL;

	{
	struct visdn_intf *intf;
	ast_rwlock_rdlock(&visdn.intfs_list_lock);
	list_for_each_entry(intf, &visdn.intfs_list, node) {
		if (intf->q931_intf &&
		    !strcasecmp(intf->name, intf_name)) {
			filter_intf = intf;
			break;
		}
	}
	ast_rwlock_unlock(&visdn.intfs_list_lock);
	}

	if (!filter_intf) {
		ast_cli(fd, "Interface '%s' not found\n", argv[3]);
		return RESULT_FAILURE;
	}

	if (!callid) {
		visdn_cli_print_call_list(fd, filter_intf->q931_intf);
		return RESULT_SUCCESS;
	}

	/*---------------------*/

	struct q931_call *call = NULL;

	char *dirpos = strchr(callid, '.');
	if (dirpos) {
		*dirpos = '\0';
		dirpos++;
	} else {
		ast_cli(fd, "Invalid call reference\n");
		return RESULT_SHOWUSAGE;
	}

	if (*dirpos == 'i' || *dirpos == 'I') {
		call = q931_get_call_by_reference(
				filter_intf->q931_intf,
				Q931_CALL_DIRECTION_INBOUND,
				atoi(callid));
	} else if (*dirpos == 'o' || *dirpos == 'O') {
		call = q931_get_call_by_reference(
				filter_intf->q931_intf,
				Q931_CALL_DIRECTION_OUTBOUND,
				atoi(callid));
	} else {
		ast_cli(fd, "Invalid call reference\n");
		return RESULT_SHOWUSAGE;
	}

	if (!call) {
		ast_cli(fd, "Call '%s.%s' not found\n", callid, dirpos);
		return RESULT_FAILURE;
	}

	visdn_cli_print_call(fd, call);
	q931_call_put(call);

	return RESULT_SUCCESS;
}

static char visdn_show_calls_help[] =
"Usage: visdn show calls [<interface>|<callid>]\n"
"\n"
"	Show detailed call informations if <callid> is specified, otherwise\n"
"	lists all the available calls, limited to <interface> if provided.\n";

static struct ast_cli_entry visdn_show_calls =
{
	{ "visdn", "show", "calls", NULL },
	visdn_show_calls_func,
	"Show vISDN's calls informations",
	visdn_show_calls_help,
	visdn_show_calls_complete
};

/*---------------------------------------------------------------------------*/

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
int load_module(void)
#else
static int visdn_load_module(void)
#endif
{
	// Initialize q.931 library.
	// No worries, internal structures are read-only and thread safe

	ast_mutex_init(&visdn.state_lock);
	ast_mutex_init(&visdn.usecnt_lock);

	INIT_LIST_HEAD(&visdn.ccb_q931_queue);
	ast_mutex_init(&visdn.ccb_q931_queue_lock);

	INIT_LIST_HEAD(&visdn.q931_ccb_queue);
	ast_mutex_init(&visdn.q931_ccb_queue_lock);

	visdn.default_ic = visdn_ic_alloc();
	visdn_ic_setdefault(visdn.default_ic);

	int filedes[2];
	if (pipe(filedes) < 0) {
		ast_log(LOG_ERROR, "Unable to create pipe: %s\n",
			strerror(errno));
		goto err_pipe_ccb_q931;
	}

	visdn.ccb_q931_queue_pipe_read = filedes[0];
	visdn.ccb_q931_queue_pipe_write = filedes[1];

	if (pipe(filedes) < 0) {
		ast_log(LOG_ERROR, "Unable to create pipe: %s\n",
			strerror(errno));
		goto err_pipe_q931_ccb;
	}

	visdn.q931_ccb_queue_pipe_read = filedes[0];
	visdn.q931_ccb_queue_pipe_write = filedes[1];

	ast_rwlock_init(&visdn.intfs_list_lock);
	INIT_LIST_HEAD(&visdn.intfs_list);

	ast_rwlock_init(&visdn.huntgroups_list_lock);
	INIT_LIST_HEAD(&visdn.huntgroups_list);

	q931_init();
	q931_set_report_func(visdn_logger);
	q931_set_timer_update_func(visdn_q931_timer_update);
	q931_set_queue_primitive_func(visdn_queue_primitive);
	q931_set_is_number_complete_func(visdn_q931_is_number_complete);

	visdn_reload_config();

	visdn.netlink_socket = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if(visdn.netlink_socket < 0) {
		ast_log(LOG_ERROR, "Unable to open netlink socket: %s\n",
			strerror(errno));
		goto err_socket_netlink;
	}

	struct sockaddr_nl snl;
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = RTMGRP_LINK;

	if (bind(visdn.netlink_socket,
			(struct sockaddr *)&snl,
			sizeof(snl)) < 0) {
		ast_log(LOG_ERROR, "Unable to bind netlink socket: %s\n",
			strerror(errno));
		goto err_bind_netlink;
	}

#if 0
	// Enum interfaces and open them
	struct ifaddrs *ifaddrs;
	struct ifaddrs *ifaddr;

	if (getifaddrs(&ifaddrs) < 0) {
		ast_log(LOG_ERROR, "getifaddr: %s\n", strerror(errno));
		goto err_getifaddrs;
	}

	int fd;
	fd = socket(PF_LAPD, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		ast_log(LOG_ERROR, "socket: %s\n", strerror(errno));
		goto err_socket_lapd;
	}

	for (ifaddr = ifaddrs ; ifaddr; ifaddr = ifaddr->ifa_next) {
		struct ifreq ifreq;

		memset(&ifreq, 0, sizeof(ifreq));

		strncpy(ifreq.ifr_name,
			ifaddr->ifa_name,
			sizeof(ifreq.ifr_name));

		if (ioctl(fd, SIOCGIFHWADDR, &ifreq) < 0) {
			ast_log(LOG_ERROR, "ioctl (%s): %s\n",
				ifaddr->ifa_name, strerror(errno));
			continue;
		}

		if (ifreq.ifr_hwaddr.sa_family != ARPHRD_LAPD)
			continue;

		if (!(ifaddr->ifa_flags & IFF_UP))
			continue;

		// visdn_add_interface(ifreq.ifr_name);XXX

	}
	close(fd);
	freeifaddrs(ifaddrs);
#endif

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	if (ast_pthread_create(&visdn.q931_thread, &attr,
					visdn_q931_thread_main, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to start q931 thread.\n");
		goto err_thread_create;
	}

	if (ast_channel_register(&visdn_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n",
			VISDN_CHAN_TYPE);
		goto err_channel_register;
	}

	ast_cli_register(&visdn_debug_netlink);
	ast_cli_register(&visdn_no_debug_netlink);
	ast_cli_register(&visdn_debug_q921);
	ast_cli_register(&visdn_no_debug_q921);
	ast_cli_register(&visdn_debug_q931);
	ast_cli_register(&visdn_no_debug_q931);
	ast_cli_register(&visdn_reload);
	ast_cli_register(&visdn_show_calls);

	visdn_intf_cli_register();
	visdn_hg_cli_register();

	visdn_overlap_register();
	visdn_disconnect_register();

	return 0;

err_channel_register:
err_thread_create:
//err_socket_lapd:
//err_getifaddrs:
err_bind_netlink:
	close(visdn.netlink_socket);
err_socket_netlink:
	close(visdn.q931_ccb_queue_pipe_write);
	close(visdn.q931_ccb_queue_pipe_read);
err_pipe_q931_ccb:
	close(visdn.ccb_q931_queue_pipe_write);
	close(visdn.ccb_q931_queue_pipe_read);
err_pipe_ccb_q931:

	return -1;
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
int unload_module(void)
#else
static int visdn_unload_module(void)
#endif
{
	visdn_ic_put(visdn.default_ic);

	visdn_disconnect_unregister();
	visdn_overlap_unregister();

	visdn_intf_cli_unregister();
	visdn_hg_cli_unregister();

	ast_cli_unregister(&visdn_show_calls);
	ast_cli_unregister(&visdn_reload);
	ast_cli_unregister(&visdn_no_debug_q931);
	ast_cli_unregister(&visdn_debug_q931);
	ast_cli_unregister(&visdn_no_debug_q921);
	ast_cli_unregister(&visdn_debug_q921);
	ast_cli_unregister(&visdn_no_debug_netlink);
	ast_cli_unregister(&visdn_debug_netlink);

	ast_channel_unregister(&visdn_tech);

	q931_leave();

	close(visdn.netlink_socket);

	return 0;
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
int reload(void)
#else
static int visdn_reload_module(void)
#endif
{
	visdn_reload_config();

	return 0;
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)

int usecount(void)
{
	int res;
	ast_mutex_lock(&visdn.usecnt_lock);
	res = visdn.usecnt;
	ast_mutex_unlock(&visdn.usecnt_lock);
	return res;
}

char *description(void)
{
	return VISDN_DESCRIPTION;
}

char *key(void)
{
	return ASTERISK_GPL_KEY;
}

#else

AST_MODULE_INFO(ASTERISK_GPL_KEY,
		AST_MODFLAG_GLOBAL_SYMBOLS,
		"vISDN channel",
		.load = visdn_load_module,
		.unload = visdn_unload_module,
		.reload = visdn_reload_module,
	);
#endif
