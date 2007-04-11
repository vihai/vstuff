/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2004-2007 Daniele Orlandi
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
#else
#include <asterisk.h>
#endif

#include <res_kstreamer.h>

#include "util.h"
#include "chan_vgsm.h"
#include "module.h"
#include "huntgroup.h"
#include "comm.h"
#include "causes.h"
#include "sms_submit.h"
#include "cbm.h"
#include "operators.h"
#include "pin.h"
#include "number.h"
#include "sim.h"
#include "base64.h"
#include "quotprint.h"

#define FAILED_RETRY_TIME (5 * SEC)
#define READY_UPDATE_TIME (30 * SEC)
#define CLOSED_POSTPONE (3 * SEC)
#define POWERING_ON_TIMEOUT (8 * SEC)
#define POWERING_OFF_TIMEOUT (7 * SEC)
#define WAITING_INITIALIZATION_DELAY (2 * SEC)

/*
 * Locking guidelines:
 *
 * Locking in vGSM is bit complex, adding the fact that Asterisk's locking
 * is brain-dead, the risk of making a big mess is very high.
 *
 * The most used and dangerous locks are the Asterisk's channel lock and
 * the module's lock. The former is the lock we all know and we also know
 * that Asterisk acquires it on callback invocation.
 * The latter is a lock protecting access to the "vgsm_module" structure.
 * A vgsm_module describes everything belonging to a GSM module.
 *
 * There no one-to-one relationship between them as the module exists even
 * if no call is present, a ast_chan is actually a call... or a channel?
 * or half a call? who knows....
 *
 * Anyway, since our callbacks do get invoked with the ast_chan->lock acquired,
 * Asterisk is forcing us to follow its locking model.
 *
 * GUIDELINES:
 *
 * Locking order is ast_chan->lock, then module->lock.
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 * NEVER ever attempt to acquire locks in reverse order, otherwise deadlocks
 * _will_ occour.
 *
 * vgsm_module callbacks are invoked without locks, it is their
 * responsibility to acquire the needed locks.
 *
 * Some field in module may be accessed without locking, either because it is
 * read-only after module initialization or being meaningful only after
 * the module changes into a particular state.
 *
 * Furthermore, the module contains module->comm which is another object
 * protected by its own lock, so, it has its own access policy.
 *
 * vgsm_module_config structures are instantiated on configuration and a
 * pointer to the current configuration is set into vgsm_module.
 * The values contained in a vgsm_module_config instance are read-only,
 * procedures which want a stable configuration may simply take a reference
 * to the vgsm_module_config object and keep it for as long as it is needed.
 * For example, a snapshot of the configuration is taken into the channel
 * structure and kept for as long as the call lasts.
 *
 */

struct vgsm_state vgsm = {
	.usecnt = 0,
#ifdef DEBUG_DEFAULTS
	.debug_generic = TRUE,
	.debug_timer = FALSE,
	.debug_jitbuf = TRUE,
#else
	.debug_generic = FALSE,
	.debug_timer = FALSE,
	.debug_jitbuf = FALSE,
#endif

};

static const struct ast_channel_tech vgsm_tech;

static struct ast_channel *vgsm_ast_chan_alloc(
	struct vgsm_chan *vgsm_chan,
	int state,
	struct vgsm_module *module,
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
		module ? module->name : "null",
		line);
#else
	ast_chan = ast_channel_alloc(TRUE, state, NULL, NULL,
				"VGSM/%s/%d",
				module ? module->name : "null",
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

	if (module->interface_version == 1) {
		ast_chan->nativeformats = AST_FORMAT_ALAW;
		ast_chan->readformat = AST_FORMAT_ALAW;
		ast_chan->rawreadformat = AST_FORMAT_ALAW;
		ast_chan->writeformat = AST_FORMAT_ALAW;
		ast_chan->rawwriteformat = AST_FORMAT_ALAW;
	} else {
		ast_chan->nativeformats =
			AST_FORMAT_SLINEAR | AST_FORMAT_ALAW | AST_FORMAT_ULAW;

		if (format & AST_FORMAT_SLINEAR) {
			ast_chan->readformat = AST_FORMAT_SLINEAR;
			ast_chan->rawreadformat = AST_FORMAT_SLINEAR;
			ast_chan->writeformat = AST_FORMAT_SLINEAR;
			ast_chan->rawwriteformat = AST_FORMAT_SLINEAR;
		} else if (format & AST_FORMAT_ALAW) {
			ast_chan->readformat = AST_FORMAT_ALAW;
			ast_chan->rawreadformat = AST_FORMAT_ALAW;
			ast_chan->writeformat = AST_FORMAT_ALAW;
			ast_chan->rawwriteformat = AST_FORMAT_ALAW;
		} else if (format & AST_FORMAT_ULAW) {
			ast_chan->readformat = AST_FORMAT_ULAW;
			ast_chan->rawreadformat = AST_FORMAT_ULAW;
			ast_chan->writeformat = AST_FORMAT_ULAW;
			ast_chan->rawwriteformat = AST_FORMAT_ULAW;
		}
	}

	ast_set_read_format(ast_chan, ast_chan->readformat);
	ast_set_write_format(ast_chan, ast_chan->writeformat);

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
		assert(!vgsm_chan->module);

		if (vgsm_chan->dsp) {
			ast_dsp_free(vgsm_chan->dsp);
			vgsm_chan->dsp = NULL;
		}

		if (vgsm_chan->huntgroup) {
			vgsm_hg_put(vgsm_chan->huntgroup);
			vgsm_chan->huntgroup = NULL;
		}

		if (vgsm_chan->hg_first_module) {
			vgsm_module_put(vgsm_chan->hg_first_module);
			vgsm_chan->hg_first_module = NULL;
		}

		if (vgsm_chan->mc) {
			vgsm_module_config_put(vgsm_chan->mc);
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

static int do_vgsm_pin_set(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 4) {
		ast_cli(fd, "Missing module name\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_module;
	}

	if (argc < 5) {
		ast_cli(fd, "Missing OLDPIN\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_oldpin;
	}

	if (!vgsm_pin_valid(argv[4])) {
		ast_cli(fd, "OLDPIN contains invalid characters\n");
		err = RESULT_SHOWUSAGE;
		goto err_oldpin_invalid;
	}

	if (argc < 6) {
		ast_cli(fd, "Missing NEWPIN\n");
		err = RESULT_SHOWUSAGE;
		goto err_missing_newpin;
	}

	struct vgsm_module *module;
	module = vgsm_module_get_by_name(argv[3]);
	if (!module) {
		ast_cli(fd, "Cannot find module '%s'\n", argv[3]);
		err = RESULT_SHOWUSAGE;
		goto err_module_not_found;
	}

	int res;
	if (!strcasecmp(argv[5], "enabled")) {
		res = vgsm_req_make_wait_result(&module->comm, 180 * SEC,
			"AT+CLCK=SC,1,\"%s\"", argv[4]);
	} else if (!strcasecmp(argv[5], "disabled")) {
		res = vgsm_req_make_wait_result(&module->comm, 180 * SEC,
			"AT+CLCK=SC,0,\"%s\"", argv[4]);
	} else {
		if (!vgsm_pin_valid(argv[5])) {
			ast_cli(fd, "NEWPIN contains invalid characters\n");
			err = RESULT_FAILURE;
			goto err_newpin_invalid;
		}

		res = vgsm_req_make_wait_result(&module->comm, 180 * SEC,
			"AT+CPWD=SC,\"%s\",\"%s\"",
			argv[4], argv[5]);
	}

	if (res != VGSM_RESP_OK) {
		ast_cli(fd, "Unable to complete command: %s (%d)\n",
			vgsm_module_error_to_text(res),
			res);
		err = RESULT_FAILURE;
		goto err_response;
	}

	vgsm_module_put(module);

	return RESULT_SUCCESS;

err_response:
err_newpin_invalid:
	vgsm_module_put(module);
err_module_not_found:
err_missing_newpin:
err_oldpin_invalid:
err_missing_oldpin:
err_missing_module:

	return err;
}

static char *vgsm_pin_set_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	char *commands[] = { "enabled", "disabled" };
	int i;

	switch(pos) {
	case 3:
		return vgsm_module_completion(line, word, state);
	case 5:
		for(i=state; i<ARRAY_SIZE(commands); i++) {
			if (!strncasecmp(word, commands[i], strlen(word)))
				return strdup(commands[i]);
		}
	break;
	}

	return NULL;
}

static char vgsm_pin_set_help[] =
"Usage: vgsm pin set <module> <OLDPIN> <NEWPIN|enabled|disabled>\n"
"\n"
"	Set, enable or disable the PIN on the SIM installed in module\n"
"	<module>.\n";

static struct ast_cli_entry vgsm_pin_set =
{
	{ "vgsm", "pin", "set", NULL },
	do_vgsm_pin_set,
	"Set, enable or disable PIN on the selected module",
	vgsm_pin_set_help,
	vgsm_pin_set_complete,
};

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

static void vgsm_reload_config(void)
{
	struct ast_config *cfg;
	cfg = ast_config_load(VGSM_CONFIG_FILE);
	if (!cfg) {
		ast_log(LOG_WARNING,
			"Unable to load config %s, VGSM disabled\n",
			VGSM_CONFIG_FILE);

		return;
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

	vgsm_module_reload(cfg);
	vgsm_hg_reload(cfg);

	ast_config_destroy(cfg);

	vgsm_operators_init();
}

/*---------------------------------------------------------------------------*/

static int do_debug_vgsm_generic(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug_generic = TRUE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging enabled\n");

	return RESULT_SUCCESS;
}

static char debug_vgsm_generic_help[] =
"Usage: debug vgsm generic\n"
"\n"
"	Debug generic vGSM events, including modules state change\n";

static struct ast_cli_entry debug_vgsm_generic =
{
	{ "debug", "vgsm", "generic", NULL },
	do_debug_vgsm_generic,
	"Enables generic vGSM debugging",
	debug_vgsm_generic_help,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_no_debug_vgsm_generic(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug_generic = FALSE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging disabled\n");

	return RESULT_SUCCESS;
}

static struct ast_cli_entry no_debug_vgsm_generic =
{
	{ "no", "debug", "vgsm", "generic", NULL },
	do_no_debug_vgsm_generic,
	"Disables generic vGSM debugging",
	NULL,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_debug_vgsm_timer(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug_timer = TRUE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging enabled\n");

	return RESULT_SUCCESS;
}

static char debug_vgsm_timer_help[] =
"Usage: debug vgsm timer\n"
"\n"
"	Enable debugging of vGSM timer events\n";

static struct ast_cli_entry debug_vgsm_timer =
{
	{ "debug", "vgsm", "timer", NULL },
	do_debug_vgsm_timer,
	"Enables vGSM timer debugging",
	debug_vgsm_timer_help,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_no_debug_vgsm_timer(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug_timer = FALSE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging disabled\n");

	return RESULT_SUCCESS;
}

static struct ast_cli_entry no_debug_vgsm_timer =
{
	{ "no", "debug", "vgsm", "timer", NULL },
	do_no_debug_vgsm_timer,
	"Disables debuggin of vGSM timer events",
	NULL,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_debug_vgsm_jitbuf(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug_jitbuf = TRUE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging enabled\n");

	return RESULT_SUCCESS;
}

static char debug_vgsm_jitbuf_help[] =
"Usage: debug vgsm jitbuf\n"
"\n"
"	Debug vGSM jitter buffer\n";

static struct ast_cli_entry debug_vgsm_jitbuf =
{
	{ "debug", "vgsm", "jitbuf", NULL },
	do_debug_vgsm_jitbuf,
	"Enables jitbuf vGSM debugging",
	debug_vgsm_jitbuf_help,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_no_debug_vgsm_jitbuf(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug_jitbuf = FALSE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging disabled\n");

	return RESULT_SUCCESS;
}

static struct ast_cli_entry no_debug_vgsm_jitbuf =
{
	{ "no", "debug", "vgsm", "jitbuf", NULL },
	do_no_debug_vgsm_jitbuf,
	"Disables vGSM jitter buffer debugging",
	NULL,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_vgsm_send_sms(int fd, int argc, char *argv[])
{
	int err = RESULT_SUCCESS;

	if (argc < 4) {
		ast_cli(fd, "Missing module");
		err = RESULT_SHOWUSAGE;
		goto err_missing_module;
	}

	if (argc < 5) {
		ast_cli(fd, "Missing phone number");
		err = RESULT_SHOWUSAGE;
		goto err_missing_number;
	}

	if (argc < 6) {
		ast_cli(fd, "Missing text");
		err = RESULT_SHOWUSAGE;
		goto err_missing_text;
	}

	struct vgsm_module *module;
	module = vgsm_module_get_by_name(argv[3]);
	if (!module) {
		ast_cli(fd, "Cannot find module '%s'\n", argv[3]);
		err = RESULT_FAILURE;
		goto err_module_not_found;
	}

	ast_mutex_lock(&module->lock);

	if (module->status != VGSM_MODULE_STATUS_READY) {
		ast_cli(fd, "Module '%s' is not ready\n", module->name);
		ast_mutex_unlock(&module->lock);
		err = RESULT_FAILURE;
		goto err_module_not_ready;
	}

	if (module->net.status != VGSM_NET_STATUS_REGISTERED_HOME &&
	    module->net.status != VGSM_NET_STATUS_REGISTERED_ROAMING) {
		ast_mutex_unlock(&module->lock);

		ast_cli(fd, "Module %s not registered\n", module->name);
		err = RESULT_FAILURE;
		goto err_module_not_registered;
	}

	if (module->sending_sms) {
		ast_cli(fd, "Module '%s' is already sending a SMS\n",
				module->name);
		ast_mutex_unlock(&module->lock);
		err = RESULT_FAILURE;
		goto err_already_sending_sms;
	}

	module->sending_sms = TRUE;

	struct vgsm_sms_submit *sms;
	sms = vgsm_sms_submit_alloc();
	if (!sms) {
		ast_cli(fd,  "Cannot allocate SMS\n");
		err = RESULT_FAILURE;
		goto err_sms_alloc;
	}

	sms->module = module;

	if (strlen(module->current_config->smcc_address.digits)) {
		vgsm_number_copy(&sms->smcc_address,
				&module->current_config->smcc_address);
	} else if (strlen(module->sim.smcc_address.digits)) {
		vgsm_number_copy(&sms->smcc_address, &module->sim.smcc_address);
	} else {
		ast_cli(fd, "Services Center number not set");
		ast_mutex_unlock(&module->lock);
		err = RESULT_FAILURE;
		goto err_no_smcc;
	}

	if (vgsm_number_parse(&sms->dest, argv[4]) < 0) {
		ast_cli(fd, "Number %s is invalid", argv[4]);
		ast_mutex_unlock(&module->lock);
		err = RESULT_FAILURE;
		goto err_invalid_number;
	}

	ast_mutex_unlock(&module->lock);

	if (argc >= 7)
		sms->message_class = atoi(argv[6]);
	else
		sms->message_class = 1;

	size_t slen;
	slen = mbstowcs(NULL, argv[5], 0);
	if(slen == -1)
		goto err_invalid_mbstring;

	sms->text = malloc((slen + 1) * sizeof(wchar_t));
	if(!sms->text)
		goto err_malloc_sms_text;

	mbstowcs(sms->text, argv[5], slen);
	sms->text[slen] = L'\0';

	vgsm_sms_submit_prepare(sms);

	struct vgsm_req *req = vgsm_req_make_sms(
		&module->comm, 30 * SEC, sms->pdu, sms->pdu_len,
		"AT+CMGS=%d", sms->pdu_tp_len);
	vgsm_req_wait(req);
	int res = vgsm_req_status(req);
	if (res != VGSM_RESP_OK) {
		vgsm_req_put(req);
		ast_cli(fd,
			"Error sending SMS: %s (%d)\n",
			vgsm_module_error_to_text(res),
			res);
		err = RESULT_FAILURE;
		goto err_make_req;
	}
	vgsm_req_put(req);

	module->sending_sms = FALSE;

	return RESULT_SUCCESS;

err_make_req:
err_invalid_mbstring:
err_malloc_sms_text:
err_invalid_number:
err_no_smcc:
	vgsm_sms_submit_put(sms);
err_sms_alloc:
	module->sending_sms = FALSE;
err_already_sending_sms:
err_module_not_registered:
err_module_not_ready:
err_module_not_found:
err_missing_text:
err_missing_number:
err_missing_module:

	return err;
}

static char *vgsm_send_sms_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	switch(pos) {
	case 3:
		return vgsm_module_completion(line, word,state);
	}

	return NULL;
}

static char vgsm_send_sms_help[] =
"Usage: vgsm send sms <module> <number> <text> [class]\n"
"\n"
"	Send short message to <number> using module <module>.\n"
"\n"
"	<text> is the text to send, in 7-bit ASCII format.\n"
"	This is meant to be just a testing command, other charsets beside\n"
"	ASCII are not supported, neither are various other SMS parameters;\n"
"\n"
"	The full SMS interface is implemented throught the manager\n"
"	interface.\n";

static struct ast_cli_entry vgsm_send_sms =
{
	{ "vgsm", "send", "sms", NULL },
	do_vgsm_send_sms,
	"Send a SMS message",
	vgsm_send_sms_help,
	vgsm_send_sms_complete,
};

/*---------------------------------------------------------------------------*/

static int do_vgsm_reload(int fd, int argc, char *argv[])
{
	vgsm_reload_config();

	return RESULT_SUCCESS;
}

static char vgsm_vgsm_reload_help[] =
"Usage: vgsm reload\n"
"\n"
"	Reloads the vGSM configuration.\n"
"\n"
"	The module's configuration is loaded using a multiversion approach;\n"
"	Calls using the old configuration will still use it, while new calls\n"
"	will use the newly loaded configuration\n";

static struct ast_cli_entry vgsm_reload =
{
	{ "vgsm", "reload", NULL },
	do_vgsm_reload,
	"Reloads vGSM configuration",
	vgsm_vgsm_reload_help,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_vgsm_pin_input(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 4) {
		ast_cli(fd, "Missing module name\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_module_name;
	}

	if (argc < 5) {
		ast_cli(fd, "Missing PIN\n");
		err =  RESULT_SHOWUSAGE;
		goto err_no_pin;
	}

	if (!vgsm_pin_valid(argv[4])) {
		ast_cli(fd, "PIN contains invalid characters\n");
		err = RESULT_SHOWUSAGE;
		goto err_pin_invalid;
	}

	struct vgsm_module *module;
	module = vgsm_module_get_by_name(argv[3]);
	if (!module) {
		ast_cli(fd, "Cannot find module '%s'\n", argv[3]);
		err = RESULT_FAILURE;
		goto err_module_not_found;
	}

	struct vgsm_comm *comm = &module->comm;
	struct vgsm_req *req;

	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CPIN?");
	if (req->err != VGSM_RESP_OK) {
		vgsm_module_failure(module, req->err);
		err = RESULT_FAILURE;
		goto err_req_make;
	}

	const struct vgsm_req_line *first_line;
	first_line = vgsm_req_first_line(req);

	if (!strcmp(first_line->text, "+CPIN: READY")) {
		ast_cli(fd, "SIM is ready and not waiting for PIN\n");
		err = RESULT_FAILURE;
		goto err_not_waiting_pin;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN")) {

		int res = vgsm_req_make_wait_result(comm, 20 * SEC,
				"AT+CPIN=\"%s\"", argv[4]);
		if (res != VGSM_RESP_OK) {
			ast_cli(fd, "Error: %s (%d)\n",
				vgsm_module_error_to_text(res),
				res);
			err = RESULT_FAILURE;
			goto err_send_pin;
		}

		vgsm_module_set_status(module,
			VGSM_MODULE_STATUS_WAITING_INITIALIZATION, -1);

	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN2")) {
		ast_cli(fd, "SIM requires PIN2");
		err = RESULT_FAILURE;
		goto err_not_waiting_pin;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK")) {
		ast_cli(fd, "SIM requires PUK");
		err = RESULT_FAILURE;
		goto err_not_waiting_pin;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK2")) {
		ast_cli(fd, "SIM requires PUK2");
		err = RESULT_FAILURE;
		goto err_not_waiting_pin;
	} else {
		ast_cli(fd, "Unknown response '%s'", first_line->text);
		err = RESULT_FAILURE;
		goto err_unknown_response;
	}

	vgsm_req_put(req);
	vgsm_module_put(module);

	return RESULT_SUCCESS;

err_unknown_response:
err_send_pin:
err_not_waiting_pin:
	vgsm_req_put(req);
err_req_make:
	vgsm_module_put(module);
err_module_not_found:
err_pin_invalid:
err_no_pin:
err_no_module_name:

	return err;
}

static char *vgsm_pin_input_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	switch(pos) {
	case 3:
		return vgsm_module_completion(line, word, state);
	}

	return NULL;
}

static char vgsm_pin_input_help[] =
"Usage: vgsm pin input <module> <PIN>\n"
"\n"
"	Manually input PIN to selected module\n";

static struct ast_cli_entry vgsm_pin_input =
{
	{ "vgsm", "pin", "input", NULL },
	do_vgsm_pin_input,
	"Manually input PIN to selected module",
	vgsm_pin_input_help,
	vgsm_pin_input_complete,
};

/*---------------------------------------------------------------------------*/

static int do_vgsm_puk_input(int fd, int argc, char *argv[])
{
	int err;

	if (argc < 4) {
		ast_cli(fd, "Missing module name\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_module_name;
	}

	if (argc < 5) {
		ast_cli(fd, "Missing PUK\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_puk;
	}

	if (!vgsm_pin_valid(argv[4])) {
		ast_cli(fd, "PUK contains invalid characters\n");
		err = RESULT_SHOWUSAGE;
		goto err_puk_invalid;
	}

	if (argc < 6) {
		ast_cli(fd, "Missing NEWPIN\n");
		err = RESULT_SHOWUSAGE;
		goto err_no_newpin;
	}

	if (!vgsm_pin_valid(argv[4])) {
		ast_cli(fd, "NEWPIN contains invalid characters\n");
		err = RESULT_FAILURE;
		goto err_newpin_invalid;
	}

	struct vgsm_module *module;
	module = vgsm_module_get_by_name(argv[3]);
	if (!module) {
		ast_cli(fd, "Cannot find module '%s'\n", argv[3]);
		err = RESULT_FAILURE;
		goto err_module_not_found;
	}

	struct vgsm_comm *comm = &module->comm;
	struct vgsm_req *req;

	req = vgsm_req_make_wait(comm, 20 * SEC, "AT+CPIN?");
	err = vgsm_req_status(req);
	if (err != VGSM_RESP_OK) {
		vgsm_module_failure(module, err);
		err = RESULT_FAILURE;
		goto err_req_make;
	}

	const struct vgsm_req_line *first_line;
	first_line = vgsm_req_first_line(req);

	if (!strcmp(first_line->text, "+CPIN: READY")) {
		ast_cli(fd, "SIM is ready and not waiting for PIN\n");
		err = RESULT_FAILURE;
		goto err_invalid_state;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN")) {
		ast_cli(fd, "SIM requires PIN");
		err = RESULT_FAILURE;
		goto err_invalid_state;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PIN2")) {
		ast_cli(fd, "SIM requires PIN2");
		err = RESULT_FAILURE;
		goto err_invalid_state;
	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK")) {

		int err = vgsm_req_make_wait_result(comm, 20 * SEC,
				"AT+CPIN=\"%s\",\"%s\"", argv[4], argv[5]);
		if (err != VGSM_RESP_OK) {
			ast_cli(fd, "Error: %s (%d)\n",
				vgsm_module_error_to_text(err),
				err);
			err = RESULT_FAILURE;
		goto err_invalid_state;
		}

		vgsm_module_set_status(module,
			VGSM_MODULE_STATUS_WAITING_INITIALIZATION, -1);

	} else if (!strcmp(first_line->text, "+CPIN: SIM PUK2")) {
		ast_cli(fd, "SIM requires PUK2");
		err = RESULT_FAILURE;
		goto err_invalid_state;
	} else {
		ast_cli(fd, "Unknown response '%s'", first_line->text);

		err = RESULT_FAILURE;
		goto err_unknown_response;
	}

	vgsm_req_put(req);
	vgsm_module_put(module);

	return RESULT_SUCCESS;

err_unknown_response:
err_invalid_state:
	vgsm_req_put(req);
err_req_make:
	vgsm_module_put(module);
err_module_not_found:
err_newpin_invalid:
err_no_newpin:
err_puk_invalid:
err_no_puk:
err_no_module_name:

	return err;
}

static char *vgsm_puk_input_complete(
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	char *line, char *word,
#else
	const char *line, const char *word,
#endif
	int pos, int state)
{
	switch(pos) {
	case 3:
		return vgsm_module_completion(line, word, state);
	}

	return NULL;
}

static char vgsm_puk_input_help[] =
"Usage: vgsm puk input <module> <PUK>\n"
"\n"
"	Manually input PUK to selected module\n"
"\n"
"	WARNING: Inputing the wrong PUK for 10 times will render the SIM card\n"
"	         useless, you will need to have it replaced from your\n"
"	         operator.\n";

static struct ast_cli_entry vgsm_puk_input =
{
	{ "vgsm", "puk", "input", NULL },
	do_vgsm_puk_input,
	"Manually input PUK to selected module",
	vgsm_puk_input_help,
	vgsm_puk_input_complete,
};

/*---------------------------------------------------------------------------*/

static int vgsm_pipeline_set_amu_compander(
	struct ks_pipeline *pipeline,
	BOOL enabled,
	BOOL mu_mode)
{
	/* TODO: Do this only once */
	struct ks_dynattr *amu_compander_attr;

	amu_compander_attr = ks_dynattr_get_by_name(ks_conn, "amu_compander");
	if (!amu_compander_attr) {
		ast_log(LOG_ERROR,
			"Cannot find amu_compander attr\n");
		goto err_missing_amu_compander;
	}

	struct ks_amu_compander_descr *amu_compander = NULL;

	int i;
	for(i=0; i<pipeline->chans_cnt; i++) {
		struct ks_chan *chan = pipeline->chans[i];
		struct ks_dynattr_instance *dynattr;

		list_for_each_entry(dynattr, &chan->dynattrs, node) {

			if (dynattr->dynattr == amu_compander_attr) {

				struct ks_amu_compander_descr *descr =
					(struct ks_amu_compander_descr *)
					dynattr->payload;

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

	return 0;

err_missing_amu_compander_in_pipeline:
err_missing_amu_compander:

	return -1;
}

static int vgsm_pipeline_set_amu_decompander(
	struct ks_pipeline *pipeline,
	BOOL enabled,
	BOOL mu_mode)
{
	/* TODO: Do this only once */
	struct ks_dynattr *amu_decompander_attr;

	amu_decompander_attr = ks_dynattr_get_by_name(ks_conn,
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
		struct ks_dynattr_instance *dynattr;

		list_for_each_entry(dynattr, &chan->dynattrs, node) {

			if (dynattr->dynattr == amu_decompander_attr) {

				struct ks_amu_decompander_descr *descr =
					(struct ks_amu_decompander_descr *)
					dynattr->payload;

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

	return 0;

err_missing_amu_decompander_in_pipeline:
err_missing_amu_decompander:

	return -1;
}

static int vgsm_connect_channel(struct vgsm_chan *vgsm_chan)
{
	__u32 node_id;
	int err;

	err = ioctl(vgsm_chan->module->fd, VGSM_IOC_GET_NODEID,
						(caddr_t)&node_id);
	if (err < 0) {

		ast_log(LOG_ERROR,
			"ioctl(VGSM_IOC_GET_NODEID): %s\n",
			strerror(errno));
		goto err_get_module_node_id;
	}

	vgsm_chan->node_module = ks_node_get_by_id(ks_conn, node_id);
	if (!vgsm_chan->node_module) {
		ast_log(LOG_ERROR, "Module's node not found\n");
		goto err_module_node_not_found;
	}

	vgsm_chan->up_fd = open("/dev/ks/userport_stream", O_RDWR);
	if (vgsm_chan->up_fd < 0) {
		ast_log(LOG_ERROR,
			"Cannot open userport: %s\n",
			strerror(errno));
		goto err_open_userport;
	}

	vgsm_chan->ast_chan->fds[0] = vgsm_chan->up_fd;

	err = ioctl(vgsm_chan->up_fd, KS_UP_GET_NODEID, (caddr_t)&node_id);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"ioctl(KS_UP_GET_NODEID): %s\n",
			strerror(errno));
		goto err_get_up_node_id;
	}

	ks_conn_sync(ks_conn);

	vgsm_chan->node_userport = ks_node_get_by_id(ks_conn, node_id);
	if (!vgsm_chan->node_userport) {
		ast_log(LOG_ERROR, "Userport's node not found\n");
		goto err_up_node_not_found;
	}

	vgsm_debug_generic("Connecting userport %06d to chan %06d\n",
			vgsm_chan->node_userport->id,
			vgsm_chan->node_module->id);

	/* Create RX pipeline */
	vgsm_chan->pipeline_rx = ks_pipeline_alloc();
	if (!vgsm_chan->pipeline_rx) {
		ast_log(LOG_ERROR,
			"Cannot allocate pipeline\n");

		goto err_pipeline_rx_alloc;
	}

	err = ks_pipeline_autoroute(vgsm_chan->pipeline_rx, ks_conn,
				vgsm_chan->node_module,
				vgsm_chan->node_userport);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot connect nodes: %s\n", strerror(-err));

		goto err_pipeline_rx_connect;
	}

	err = ks_pipeline_create(vgsm_chan->pipeline_rx, ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot create RX pipeline: %s\n",
			strerror(-err));
		goto err_pipeline_rx_create;
	}

	if (vgsm_pipeline_set_amu_compander(vgsm_chan->pipeline_rx,
			vgsm_chan->ast_chan->readformat != AST_FORMAT_SLINEAR,
			vgsm_chan->ast_chan->readformat == AST_FORMAT_ULAW)) {
		ast_log(LOG_ERROR,
			"Cannot enable RX amu_compander\n");
		goto err_pipeline_rx_amu_compander_enable;
	}

	ks_pipeline_update_chans(vgsm_chan->pipeline_rx, ks_conn);

	/* Create TX pipeline */
	vgsm_chan->pipeline_tx = ks_pipeline_alloc();
	if (!vgsm_chan->pipeline_tx) {
		ast_log(LOG_ERROR,
			"Cannot allocate TX pipeline\n");

		goto err_pipeline_tx_alloc;
	}

	err = ks_pipeline_autoroute(vgsm_chan->pipeline_tx, ks_conn,
				vgsm_chan->node_userport,
				vgsm_chan->node_module);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot connect nodes: %s\n", strerror(-err));

		goto err_pipeline_tx_connect;
	}

	err = ks_pipeline_create(vgsm_chan->pipeline_tx, ks_conn);
	if (err < 0) {
		ast_log(LOG_ERROR,
			"Cannot create pipeline: %s\n",
			strerror(-err));
		goto err_pipeline_tx_create;
	}

	if (vgsm_pipeline_set_amu_decompander(vgsm_chan->pipeline_tx,
			vgsm_chan->ast_chan->writeformat != AST_FORMAT_SLINEAR,
			vgsm_chan->ast_chan->writeformat == AST_FORMAT_ULAW)) {
		ast_log(LOG_ERROR,
			"Cannot enable TX amu_decompander\n");
		goto err_pipeline_tx_decompander_enable;
	}

	ks_pipeline_update_chans(vgsm_chan->pipeline_tx, ks_conn);

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
				"Cannot start the pipeline\n");
		goto err_pipeline_tx_update;
	}

	return 0;

err_pipeline_tx_update:
err_pipeline_rx_update:
err_pipeline_tx_decompander_enable:
err_pipeline_tx_create:
err_pipeline_tx_connect:
	ks_pipeline_put(vgsm_chan->pipeline_tx);
	vgsm_chan->pipeline_tx = NULL;
err_pipeline_tx_alloc:
err_pipeline_rx_amu_compander_enable:
err_pipeline_rx_create:
err_pipeline_rx_connect:
	ks_pipeline_put(vgsm_chan->pipeline_rx);
	vgsm_chan->pipeline_rx = NULL;
err_pipeline_rx_alloc:
err_up_node_not_found:
err_get_up_node_id:
	close(vgsm_chan->up_fd);
err_open_userport:
err_module_node_not_found:
err_get_module_node_id:

	return -1;
}

struct vgsm_chan *vgsm_alloc_inbound_call(struct vgsm_module *module)
{
	struct vgsm_chan *vgsm_chan;
	vgsm_chan = vgsm_chan_alloc();
	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "Cannot allocate vgsm_chan\n");
		goto err_vgsm_chan_alloc;
	}

	vgsm_chan->ast_chan = vgsm_ast_chan_alloc(vgsm_chan,
						AST_STATE_RESERVED,
						module, 1, AST_FORMAT_SLINEAR);

	if (!vgsm_chan->ast_chan)
		goto err_vgsm_ast_chan_alloc;

	vgsm_chan->outbound = FALSE;

	vgsm_chan->module = vgsm_module_get(module);
	vgsm_chan->mc = vgsm_module_config_get(module->current_config);

	ast_dsp_digitmode(vgsm_chan->dsp,
		DSP_DIGITMODE_DTMF |
		vgsm_chan->mc->dtmf_quelch ? 0 : DSP_DIGITMODE_NOQUELCH |
		vgsm_chan->mc->dtmf_mutemax ? DSP_DIGITMODE_MUTEMAX : 0 |
		vgsm_chan->mc->dtmf_relax ? DSP_DIGITMODE_RELAXDTMF : 0);

	struct ast_channel *ast_chan = vgsm_chan->ast_chan;

	strcpy(ast_chan->exten, "s");
	strncpy(ast_chan->context, vgsm_chan->mc->context,
					sizeof(ast_chan->context));
	ast_chan->priority = 1;

	return vgsm_chan;

	vgsm_chan_put(vgsm_chan->ast_chan->tech_pvt);
	vgsm_chan->ast_chan->tech_pvt = NULL;
	ast_channel_free(vgsm_chan->ast_chan);
	vgsm_chan->ast_chan = NULL;
err_vgsm_ast_chan_alloc:
	vgsm_module_put(vgsm_chan->module);
	vgsm_chan->module = NULL;
	vgsm_module_config_put(vgsm_chan->mc);
	vgsm_chan->mc = NULL;
	vgsm_chan_put(vgsm_chan);
err_vgsm_chan_alloc:

	return NULL;
}

static void vgsm_atd_complete(struct vgsm_req *req, void *data)
{
	struct vgsm_module *module = data;

	ast_mutex_lock(&module->lock);

	module->call_present = TRUE;

	struct vgsm_chan *vgsm_chan = vgsm_chan_get(module->vgsm_chan);
	ast_mutex_unlock(&module->lock);

	if (req->err != VGSM_RESP_OK) {
		if (vgsm_chan)
			ast_softhangup(vgsm_chan->ast_chan, AST_SOFTHANGUP_DEV);

		ast_log(LOG_DEBUG, "%s: Unable to dial: ATD failed\n",
			module->name);
	} else {
		if (vgsm_chan) {
			ast_mutex_lock(&vgsm_chan->ast_chan->lock);
			/* If there hasn't been an hangup in between */

			if (vgsm_connect_channel(vgsm_chan) < 0)
				ast_softhangup(vgsm_chan->ast_chan,
						AST_SOFTHANGUP_DEV);
			else
				ast_queue_control(vgsm_chan->ast_chan,
					AST_CONTROL_PROCEEDING);

			ast_mutex_unlock(&vgsm_chan->ast_chan->lock);
		} else {
			_vgsm_req_put(vgsm_req_make_callback(&module->comm,
					vgsm_module_chup_complete,
					vgsm_module_get(module),
					5 * SEC, "AT+CHUP"));
		}
	}

	vgsm_chan_put(vgsm_chan);

	vgsm_module_put(module);
}

static int vgsm_call(
	struct ast_channel *ast_chan,
	char *orig_dest,
	int timeout)
{
	int err;

	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	struct vgsm_module *module = vgsm_chan->module;

	assert(ast_chan->_state == AST_STATE_DOWN);

	if (module->status != VGSM_MODULE_STATUS_READY) {
		ast_mutex_unlock(&module->lock);

		ast_log(LOG_DEBUG, "Module %s is not ready\n", module->name);
		err = -1;
		goto err_module_not_ready;
	}

	if (module->net.status != VGSM_NET_STATUS_REGISTERED_HOME &&
	    module->net.status != VGSM_NET_STATUS_REGISTERED_ROAMING) {
		ast_mutex_unlock(&module->lock);

		ast_log(LOG_DEBUG, "Module %s not registered\n",
			module->name);
		err = -1;
		goto err_module_not_registered;
	}

	if (module->vgsm_chan) {
		ast_mutex_unlock(&module->lock);

		ast_log(LOG_DEBUG, "Module %s is busy (call present)\n",
			module->name);
		err = -1;
		goto err_module_busy;
	}

	module->stats.outbound++;

	module->vgsm_chan = vgsm_chan_get(vgsm_chan);
	vgsm_chan->module = vgsm_module_get(module);
	vgsm_chan->mc = vgsm_module_config_get(module->current_config);

	ast_dsp_digitmode(vgsm_chan->dsp,
		DSP_DIGITMODE_DTMF |
		vgsm_chan->mc->dtmf_quelch ? 0 : DSP_DIGITMODE_NOQUELCH |
		vgsm_chan->mc->dtmf_mutemax ? DSP_DIGITMODE_MUTEMAX : 0 |
		vgsm_chan->mc->dtmf_relax ? DSP_DIGITMODE_RELAXDTMF : 0);

	if (option_debug)
		ast_log(LOG_DEBUG, "Calling %s on %s\n", vgsm_chan->called_number, ast_chan->name);

	char newname[40];
	snprintf(newname, sizeof(newname), "VGSM/%s/%d", module->name, 1);

	ast_change_name(ast_chan, newname);

	ast_setstate(ast_chan, AST_STATE_DIALING);

	module->call_present = FALSE;

	ast_mutex_unlock(&module->lock);

	struct vgsm_req *req;
	// 'timeout' instead of 20s ?
	req = vgsm_req_make_callback(&module->comm,
			vgsm_atd_complete,
			vgsm_module_get(module),
			180 * SEC, "ATD%c%s;",
			((ast_chan->cid.cid_pres & AST_PRES_RESTRICTION) ==
				AST_PRES_ALLOWED) ? 'i' : 'I',
			vgsm_chan->called_number);
	if (!req) {
		ast_log(LOG_ERROR, "%s: Unable to dial: ATD failed\n",
			module->name);

		err = -1;
		goto err_atd_failed;
	}

	vgsm_req_put(req);

	vgsm_module_put(module);

	return 0;

err_atd_failed:
	vgsm_module_config_put(vgsm_chan->mc);
	vgsm_module_put(vgsm_chan->module);
	vgsm_chan_put(module->vgsm_chan);
err_module_busy:
err_module_not_registered:
err_module_not_ready:

	return err;
}

static int vgsm_answer(struct ast_channel *ast_chan)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	struct vgsm_module *module = vgsm_chan->module;
	int err;

	vgsm_debug_generic("vgsm_answer\n");

	assert(vgsm_chan);

	ast_mutex_lock(&module->lock);
	if (module->status != VGSM_MODULE_STATUS_READY) {
		ast_mutex_unlock(&module->lock);

		ast_log(LOG_NOTICE, "Module is not ready anymore\n");
		return -1;
	}

	vgsm_connect_channel(vgsm_chan);

	ast_mutex_unlock(&module->lock);

	ast_indicate(ast_chan, -1);

	err = vgsm_req_make_wait_result(&module->comm, 1 * SEC, "ATA");
	if (err != VGSM_RESP_OK) {

		if (err != VGSM_RESP_NO_CARRIER)
			ast_log(LOG_WARNING, "Couldn't answer: %s\n",
				vgsm_module_error_to_text(err));

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
	//struct vgsm_module *module = vgsm_chan->module;

	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "NO VGSM_CHAN!!\n");
		return 1;
	}

	vgsm_debug_generic("vgsm_indicate %d\n", condition);

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

	case AST_CONTROL_ANSWER:
		ast_playtones_stop(ast_chan);

		return 0;
	break;

	case AST_CONTROL_HANGUP:
		ast_log(LOG_ERROR, "Unexpected AST_CONTROL_HANGUP\n");
		return 0;
	break;

	case AST_CONTROL_CONGESTION:
/*		vgsm_module_counter_inc(module,
			vgsm_chan->outbound,
			VGSM_CAUSE_LOCATION_LOCAL,
			42);

		vgsm_module_hangup(module);*/
	break;

	case AST_CONTROL_BUSY:
/*		vgsm_module_counter_inc(module,
			vgsm_chan->outbound,
			VGSM_CAUSE_LOCATION_LOCAL,
			17);

		vgsm_module_hangup(module);*/
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
	struct vgsm_module *module = vgsm_chan->module;

	if (duration < 300)
		duration = 300;

	/* VTS takes duration in 1/100 sec */
	_vgsm_req_put(vgsm_req_make(
		&module->comm, 2 * SEC, "AT+VTS=%c,%d", digit, duration / 100));

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
	vgsm_debug_generic("%s: hangup\n", ast_chan->name);

	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	assert(vgsm_chan);

	ast_mutex_lock(&ast_chan->lock);

	ast_setstate(ast_chan, AST_STATE_DOWN);

	if (vgsm_chan->module) {

		ast_mutex_lock(&vgsm_chan->module->lock);

		vgsm_module_counter_inc(vgsm_chan->module,
			vgsm_chan->outbound,
			VGSM_CAUSE_LOCATION_LOCAL,
			ast_chan->hangupcause != 0 ?
				ast_chan->hangupcause : 16);

		/* Send deferred hangup */

		if (vgsm_chan->module->call_present) {
			_vgsm_req_put(vgsm_req_make_callback(
				&vgsm_chan->module->comm,
				vgsm_module_chup_complete,
				vgsm_module_get(vgsm_chan->module),
				5 * SEC, "AT+CHUP"));
		}

		/* Detach module and channel */
		vgsm_chan_put(vgsm_chan->module->vgsm_chan);
		vgsm_chan->module->vgsm_chan = NULL;

		ast_mutex_unlock(&vgsm_chan->module->lock);

		vgsm_module_put(vgsm_chan->module);
		vgsm_chan->module = NULL;
	}

	if (vgsm_chan->up_fd >= 0)
		vgsm_disconnect_channel(vgsm_chan);

	/* Okay, here we should have done our work, but, there may be
	 * references to vgsm_chan held in other threads, so, before giving
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

	ast_mutex_lock(&vgsm.usecnt_lock);
	while(vgsm_chan->refcnt > 1 && res == 0) {
		res = ast_cond_timedwait(
				&vgsm_chan->refcnt_decremented_cond,
				&vgsm.usecnt_lock, &timeout);
	}
	ast_mutex_unlock(&vgsm.usecnt_lock);
	ast_mutex_lock(&ast_chan->lock);

	assert(vgsm_chan->refcnt > 0);

	if (res == ETIMEDOUT)
		ast_log(LOG_WARNING,
			"%s: Timeout waiting for references to"
			" vgsm_chan to be released, possible reference"
			" leak, %d references left\n",
			ast_chan->name,
			vgsm_chan->refcnt);

	vgsm_chan->ast_chan = NULL;

	vgsm_chan_put(ast_chan->tech_pvt);
	ast_chan->tech_pvt = NULL;

	ast_mutex_unlock(&ast_chan->lock);

	vgsm_debug_generic("%s: hangup complete\n", ast_chan->name);

	return 0;
}

static struct ast_frame *vgsm_read(struct ast_channel *ast_chan)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);

	struct ast_frame *f = &vgsm_chan->frame_out;

	f->src = VGSM_CHAN_TYPE;
	f->mallocd = 0;
	f->delivery.tv_sec = 0;
	f->delivery.tv_usec = 0;

	if (vgsm_chan->up_fd < 0) {
		f->frametype = AST_FRAME_NULL;
		f->subclass = 0;
		f->samples = 0;
		f->datalen = 0;
		f->data = NULL;
		f->offset = 0;

		return f;
	}

	int nread = read(vgsm_chan->up_fd, vgsm_chan->frame_out_buf,
					sizeof(vgsm_chan->frame_out_buf));
	if (nread < 0) {
//		ast_log(LOG_WARNING, "read error: %s\n", strerror(errno));
		return f;
	}

#if 0
{
__u8 *buf = vgsm_chan->frame_out_buf;
struct timeval tv;
gettimeofday(&tv, NULL);
unsigned long long t = tv.tv_sec * 1000000ULL + tv.tv_usec;
ast_verbose(VERBOSE_PREFIX_3 "R %.3f %02x%02x%02x%02x%02x%02x%02x%02x %d\n",
	t/1000000.0,
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
#endif

	f->frametype = AST_FRAME_VOICE;
	f->subclass = ast_chan->readformat;
	f->samples = nread / 2;
	f->datalen = nread;
	f->data = vgsm_chan->frame_out_buf;
	f->offset = 0;

	struct ast_frame *f2;
	f2 = ast_dsp_process(ast_chan, vgsm_chan->dsp, f);

	return f2;
}

#define FIFO_JITTBUFF_LOW 10
#define FIFO_JITTBUFF_HIGH 100
#define FIFO_JITTBUFF_AVG \
		((FIFO_JITTBUFF_LOW + FIFO_JITTBUFF_HIGH) / 2)

#define vgsm_debug_jitbuf(format, arg...)			\
	if (vgsm.debug_jitbuf)					\
		ast_verbose("vgsm: "				\
			format,					\
			## arg)

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

	if (frame->subclass != AST_FORMAT_ALAW &&
	    frame->subclass != AST_FORMAT_ULAW &&
	    frame->subclass != AST_FORMAT_SLINEAR) {
		ast_log(LOG_WARNING,
			"Cannot handle frames in %d format\n",
			frame->subclass);
		return 0;
	}

	if (vgsm_chan->up_fd < 0) {
//		ast_log(LOG_WARNING,
//			"Attempting to write on unconnected channel\n");
		return 0;
	}

#if 0
struct timeval tv;
gettimeofday(&tv, NULL);
unsigned long long t = tv.tv_sec * 1000000ULL + tv.tv_usec;
ast_verbose(VERBOSE_PREFIX_3 "W %.3f %02x%02x%02x%02x%02x%02x%02x%02x %d\n",
	t/1000000.0,
	*(__u8 *)(frame->data + 0),
	*(__u8 *)(frame->data + 1),
	*(__u8 *)(frame->data + 2),
	*(__u8 *)(frame->data + 3),
	*(__u8 *)(frame->data + 4),
	*(__u8 *)(frame->data + 5),
	*(__u8 *)(frame->data + 6),
	*(__u8 *)(frame->data + 7),
	frame->datalen);
#endif

        int len = frame->datalen;
	__u8 *buf = frame->data;

	if (!len)
		return 0;

	int pressure;
	if (ioctl(vgsm_chan->up_fd, KS_UP_GET_PRESSURE, &pressure)) {
		ast_log(LOG_ERROR, "ioctl(KS_UP_GET_PRESSURE): %s\n",
						strerror(errno));
	}

	if (pressure < FIFO_JITTBUFF_LOW) {
		int diff = (FIFO_JITTBUFF_LOW - pressure);
		buf = alloca(len + diff);
		memset(buf, 0x2a, diff);
		memcpy(buf + diff, frame->data, len);
		len += diff;

		vgsm_debug_jitbuf("TX under low-mark: added %d samples\n",
				diff);
	}

	if (pressure > FIFO_JITTBUFF_HIGH && len > 0) {
		vgsm_debug_jitbuf(
			"TX %d over high-mark: dropped %d samples\n",
			pressure - FIFO_JITTBUFF_HIGH,
			min(len, pressure - FIFO_JITTBUFF_HIGH));

		len = max(0, len - (pressure - FIFO_JITTBUFF_HIGH));
	}

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
		err = -1;
		goto err_unsupported_format;
	}

	// Parse destination and obtain module name + number
	const char *module_name;
	const char *number;
	char *stringp = data;

	module_name = strsep(&stringp, "/");
	if (!module_name) {
		ast_log(LOG_WARNING,
			"Invalid destination '%s' format (module/number)\n",
			(char *)data);

		err = -1;
		goto err_invalid_destination;
	}

	number = strsep(&stringp, "/");
	if (!number) {
		ast_log(LOG_WARNING,
			"Invalid destination '%s' format (module/number)\n",
			(char *)data);

		err = -1;
		goto err_invalid_format;
	}

	struct vgsm_module *module;
	struct vgsm_module *hg_first_module = NULL;
	struct vgsm_huntgroup *huntgroup = NULL;

	if (!strncasecmp(module_name, VGSM_HUNTGROUP_PREFIX,
			strlen(VGSM_HUNTGROUP_PREFIX))) {

		const char *hg_name = module_name +
					strlen(VGSM_HUNTGROUP_PREFIX);
		struct vgsm_huntgroup *hg;
		hg = vgsm_hg_get_by_name(hg_name);
		if (!hg) {
			ast_log(LOG_ERROR, "Cannot find huntgroup '%s'\n",
				hg_name);

			err = -1;
			goto err_huntgroup_not_found;
		}

		module = vgsm_hg_hunt(hg, NULL, NULL);
		if (!module) {
			vgsm_debug_generic("Cannot hunt in huntgroup %s\n",
					hg_name);

			err = -1;
			goto err_no_module_available;
		}

		hg_first_module = vgsm_module_get(module);
		huntgroup = vgsm_hg_get(hg);

		vgsm_hg_put(hg);
	} else {
		module = vgsm_module_get_by_name(module_name);
		if (!module) {
			ast_log(LOG_WARNING, "Module %s not found\n",
				module_name);
			err = -1;
			goto err_module_not_found;
		}
	}

	ast_mutex_lock(&module->lock);








	struct vgsm_chan *vgsm_chan;
	vgsm_chan = vgsm_chan_alloc();
	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "Cannot allocate vgsm_chan\n");
		goto err_vgsm_chan_alloc;
	}

	vgsm_chan->ast_chan = vgsm_ast_chan_alloc(vgsm_chan, AST_STATE_DOWN, module, 1, format);
	if (!vgsm_chan->ast_chan)
		goto err_vgsm_ast_chan_alloc;

	vgsm_chan->outbound = TRUE;

	struct ast_channel *ast_chan = vgsm_chan->ast_chan;



	snprintf(vgsm_chan->called_number, sizeof(vgsm_chan->called_number),
		"%s", number);

	vgsm_chan->module = module;
	vgsm_chan->hg_first_module = hg_first_module;
	vgsm_chan->huntgroup = huntgroup;

	vgsm_chan_put(vgsm_chan);

	return ast_chan;

	vgsm_chan_put(vgsm_chan->ast_chan->tech_pvt);
	vgsm_chan->ast_chan->tech_pvt = NULL;
	ast_channel_free(vgsm_chan->ast_chan);
	vgsm_chan->ast_chan = NULL;
err_vgsm_ast_chan_alloc:
	vgsm_chan_put(vgsm_chan);
err_vgsm_chan_alloc:
	vgsm_module_put(module);
err_module_not_found:
err_no_module_available:
err_huntgroup_not_found:
err_invalid_format:
err_invalid_destination:
err_unsupported_format:

	return NULL;
}

static const struct ast_channel_tech vgsm_tech = {
	.type		= VGSM_CHAN_TYPE,
	.description	= VGSM_DESCRIPTION,
	.capabilities	= AST_FORMAT_ALAW |
			  AST_FORMAT_ULAW |
			  AST_FORMAT_SLINEAR,
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

/***********************************************/

/*! \brief  manager_vgsm_sms_tx: Send a text sms with VGSM card ---*/

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

#if ASTERISK_VERSION_NUM >= 10402
static int manager_vgsm_sms_tx(struct mansession *s, const struct message *m)
#else
static int manager_vgsm_sms_tx(struct mansession *s, struct message *m)
#endif
{
	const char *number = astman_get_header(m, "To");
	if (!strlen(number)) {
		astman_append(s, "Status: 510\n");
		astman_send_error(s, m, "To: header missing");
		goto err_missing_destination;
	}

	const char *content = astman_get_header(m, "Content");
	if (!strlen(content)) {
		astman_append(s, "Status: 509\n");
		astman_send_error(s, m, "Content: header missing");
		goto err_missing_content;
	}

	const char *content_type = astman_get_header(m, "Content-type");
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

	char *charset = "utf-8";
	char *par;
	while((par = strtok_r(NULL, " ;", &lasts))) {
		if (!strncasecmp(par, "charset=", strlen("charset="))) {
			charset = par + strlen("charset=");
			break;
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

	const char *module_str = astman_get_header(m, "X-SMS-Module");
	struct vgsm_module *module = NULL;

	if (!strncasecmp(module_str, VGSM_HUNTGROUP_PREFIX,
			strlen(VGSM_HUNTGROUP_PREFIX))) {

		ast_mutex_lock(&vgsm.lock);

		const char *hg_name = module_str +
					strlen(VGSM_HUNTGROUP_PREFIX);
		struct vgsm_huntgroup *hg;
		hg = vgsm_hg_get_by_name(hg_name);
		if (!hg) {
			astman_append(s, "Status: 508\n");
			astman_send_error(s, m, "Cannot find huntgroup");

			ast_mutex_unlock(&vgsm.lock);
			goto err_huntgroup_not_found;
		}

		struct vgsm_huntgroup_member *hgm;
		list_for_each_entry(hgm, &hg->members, node) {
		        ast_mutex_lock(&hgm->module->lock);
		        if (hgm->module->status == VGSM_MODULE_STATUS_READY &&
			    !hgm->module->sending_sms &&
			    (hgm->module->net.status ==
					VGSM_NET_STATUS_REGISTERED_HOME ||
			    hgm->module->net.status ==
					VGSM_NET_STATUS_REGISTERED_ROAMING)) {

				module = vgsm_module_get(hgm->module);
				break;
			}
			ast_mutex_unlock(&hgm->module->lock);
		}

		vgsm_hg_put(hg);

		ast_mutex_unlock(&vgsm.lock);

		if (!module) {
			astman_append(s, "Status: 404\n");
			astman_send_error(s, m,
				"Cannot find an available module");
			goto err_module_not_found;
		}

	} else if(strlen(module_str)) {
		/* A specific module has been requested */

		module = vgsm_module_get_by_name(module_str);
		if (!module) {
			astman_append(s, "Status: 501\n");
			astman_send_error(s, m, "501 Cannot find module");
			goto err_module_not_found;
		}

		ast_mutex_lock(&module->lock);

		if (module->status != VGSM_MODULE_STATUS_READY) {
			ast_mutex_unlock(&module->lock);

			astman_append(s, "Status: 401\n");
			astman_send_error(s, m, "Module is not ready");
			goto err_module_not_ready;
		}

		if (module->net.status != VGSM_NET_STATUS_REGISTERED_HOME &&
		    module->net.status != VGSM_NET_STATUS_REGISTERED_ROAMING) {
			ast_mutex_unlock(&module->lock);

			astman_append(s, "Status: 402\n");
			astman_send_error(s, m, "Module is not registered");
			goto err_module_not_registered;
		}

		if (module->sending_sms) {
			ast_mutex_unlock(&module->lock);

			astman_append(s, "Status: 403\n");
			astman_send_error(s, m,
				"403 Module is already sending a message");
			goto err_module_sending_sms;
		}

	} else {
		/* Find a ready module */

		struct vgsm_module *tm;

		ast_mutex_lock(&vgsm.lock);
		list_for_each_entry(tm, &vgsm.ifs, ifs_node) {
		        ast_mutex_lock(&tm->lock);
		        if (tm->status == VGSM_MODULE_STATUS_READY &&
			    !tm->sending_sms &&
			    (tm->net.status ==
					VGSM_NET_STATUS_REGISTERED_HOME ||
			    tm->net.status ==
					VGSM_NET_STATUS_REGISTERED_ROAMING)) {

				module = vgsm_module_get(tm);
				break;
			}
			ast_mutex_unlock(&tm->lock);
		}
		ast_mutex_unlock(&vgsm.lock);

		if (!module) {
			astman_append(s, "Status: 404\n");
			astman_send_error(s, m,
				"Cannot find an available module");
			goto err_module_not_found;
		}
	}

	module->sending_sms = TRUE;

	struct vgsm_sms_submit *sms;
	sms = vgsm_sms_submit_alloc();
	if (!sms) {
		ast_mutex_unlock(&module->lock);

		astman_append(s, "Status: 405\n");
		astman_send_error(s, m, "Cannot allocate message");
		goto err_sms_alloc;
	}

	sms->module = module;

	const char *smcc_str = astman_get_header(m, "X-SMS-SMCC-Number");
	if (strlen(smcc_str)) {
		vgsm_number_parse(&sms->smcc_address, smcc_str);
	} else if (strlen(module->current_config->smcc_address.digits)) {
		vgsm_number_copy(&sms->smcc_address,
				&module->current_config->smcc_address);
	} else if (strlen(module->sim.smcc_address.digits)) {
		vgsm_number_copy(&sms->smcc_address, &module->sim.smcc_address);
	} else {
		ast_mutex_unlock(&module->lock);

		astman_append(s, "Status: 502\n");
		astman_send_error(s, m, "Services Center number not set");
		goto err_no_smcc;
	}

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

	ast_mutex_unlock(&module->lock);

	const char *content_te =
		astman_get_header(m, "Content-Transfer-Encoding");
	size_t content_size;
	__u8 *content_raw;

	if (!strlen(content_te)) {
		content_size = strlen(content);
		content_raw = (__u8 *)content; // Okay to drop const

	} else if(!strcasecmp(content_te, "hex")) {
		content_size = strlen(content) / 2;
		content_raw = alloca(content_size + 1);

		int i;
		for(i=0; i<content_size; i++) {
			content_raw[i] =
				char_to_hexdigit(content[i * 2]) << 4 |
				char_to_hexdigit(content[i * 2 + 1]);
		}

	} else if (!strcasecmp(content_te, "base64")) {
		content_size = strlen(content);
		content_raw = alloca(content_size + 1);

		base64_decode(content, content_raw, content_size + 1);

	} else if (!strcasecmp(content_te, "quoted-printable")) {
		content_size = strlen(content);
		content_raw = alloca(content_size + 1);

		quoted_printable_decode(content, content_raw, content_size + 1);

	} else {
		astman_append(s, "Status: 506\n");
		astman_send_error(s, m,
			"Unsupported Content-Transfer-Encoding");
		goto err_unsupported_cte;
	}

	char *inbuf = (char *)content_raw;
	size_t inbytes = content_size + 1;
	size_t outbytes_avail = (content_size + 1) * sizeof(wchar_t);
	size_t outbytes_left = outbytes_avail;

	sms->text = malloc(outbytes_avail);
	if (!sms->text) {
		astman_append(s, "Status: 406\n");
		astman_send_error(s, m, "Out of memory");
		iconv_close(cd);
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
		iconv_close(cd);
		goto err_iconv;
	}

	vgsm_sms_submit_prepare(sms);

	if (module->debug_sms)
		vgsm_sms_submit_dump(sms);

	struct vgsm_req *req = vgsm_req_make_sms(
		&module->comm, 30 * SEC, sms->pdu, sms->pdu_len,
		"AT+CMGS=%d", sms->pdu_tp_len);
	vgsm_req_wait(req);
	int res = vgsm_req_status(req);
	if (res != VGSM_RESP_OK) {
		vgsm_req_put(req);

		astman_append(s, "Status: %c00\n",
			vgsm_cms_error_fatal(res) ? '5' : '4');

		char tmpstr[256];
		snprintf(tmpstr, sizeof(tmpstr),
			"%s\n",
			vgsm_module_error_to_text(res));

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

	module->sending_sms = FALSE;

	iconv_close(cd);

	return 0;

err_make_req:
	module->sending_sms = FALSE;
err_iconv:
	free(sms->text);
	sms->text = NULL;
err_text_malloc:
err_unsupported_cte:
err_unsupported_content_type:
err_invalid_content_type:
err_no_smcc:
	vgsm_sms_submit_put(sms);
err_sms_alloc:
err_module_sending_sms:
err_module_not_registered:
err_module_not_ready:
err_module_not_found:
err_huntgroup_not_found:
	iconv_close(cd);
err_iconv_open:
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
	vgsm_module_shutdown_all();
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
	ast_mutex_init(&vgsm.lock);

	ast_mutex_init(&vgsm.usecnt_lock);

	INIT_LIST_HEAD(&vgsm.ifs);
	INIT_LIST_HEAD(&vgsm.op_list);
	INIT_LIST_HEAD(&vgsm.op_countries_list);
	INIT_LIST_HEAD(&vgsm.huntgroups_list);

	vgsm.default_mc = vgsm_module_config_alloc();
	vgsm_module_config_default(vgsm.default_mc);

	vgsm_reload_config();

	if (ast_channel_register(&vgsm_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n",
			VGSM_CHAN_TYPE);
		err = -1;
		goto err_channel_register;
	}

	err = vgsm_module_module_load();
	if (err < 0)
		goto err_module_module_load;

	ast_cli_register(&debug_vgsm_generic);
	ast_cli_register(&no_debug_vgsm_generic);
	ast_cli_register(&debug_vgsm_timer);
	ast_cli_register(&no_debug_vgsm_timer);
	ast_cli_register(&debug_vgsm_jitbuf);
	ast_cli_register(&no_debug_vgsm_jitbuf);
	ast_cli_register(&vgsm_reload);
	ast_cli_register(&vgsm_send_sms);
	ast_cli_register(&vgsm_pin_input);
	ast_cli_register(&vgsm_puk_input);
	ast_cli_register(&vgsm_pin_set);

	vgsm_hg_cli_register();

	/* Register manager commands */
	ast_manager_register2("VGSMsmstx", EVENT_FLAG_SYSTEM,
			manager_vgsm_sms_tx,
			"Send sms with VGSM (text format)",
			mandescr_vgsm_sms_tx);

	ast_register_atexit(vgsm_shutdown);

	return 0;

	vgsm_module_module_unload();
err_module_module_load:
	ast_channel_unregister(&vgsm_tech);
err_channel_register:
	vgsm_hg_cli_unregister();

	ast_cli_unregister(&vgsm_pin_set);
	ast_cli_unregister(&vgsm_puk_input);
	ast_cli_unregister(&vgsm_pin_input);
	ast_cli_unregister(&vgsm_send_sms);
	ast_cli_unregister(&vgsm_reload);
	ast_cli_unregister(&no_debug_vgsm_jitbuf);
	ast_cli_unregister(&debug_vgsm_jitbuf);
	ast_cli_unregister(&no_debug_vgsm_timer);
	ast_cli_unregister(&debug_vgsm_timer);
	ast_cli_unregister(&no_debug_vgsm_generic);
	ast_cli_unregister(&debug_vgsm_generic);

	vgsm_module_config_put(vgsm.default_mc);

	return err;
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
int unload_module(void)
#else
static int vgsm_unload_module(void)
#endif
{
	vgsm_module_module_unload();

	vgsm_hg_cli_unregister();

	ast_cli_unregister(&vgsm_pin_set);
	ast_cli_unregister(&vgsm_puk_input);
	ast_cli_unregister(&vgsm_pin_input);
	ast_cli_unregister(&vgsm_send_sms);
	ast_cli_unregister(&vgsm_reload);
	ast_cli_unregister(&no_debug_vgsm_jitbuf);
	ast_cli_unregister(&debug_vgsm_jitbuf);
	ast_cli_unregister(&no_debug_vgsm_timer);
	ast_cli_unregister(&debug_vgsm_timer);
	ast_cli_unregister(&no_debug_vgsm_generic);
	ast_cli_unregister(&debug_vgsm_generic);

	ast_channel_unregister(&vgsm_tech);

	vgsm_module_config_put(vgsm.default_mc);

	return 0;
}

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
int reload(void)
#else
static int vgsm_reload_module(void)
#endif
{
	vgsm_reload_config();

	return 0;
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
		"Kstreamer handler",
		.load = vgsm_load_module,
		.unload = vgsm_unload_module,
		.reload = vgsm_reload_module,
	);
#endif
