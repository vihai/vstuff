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

#include "../config.h"

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

#include <linux/vgsm.h>

#include <linux/visdn/userport.h>
#include <linux/visdn/router.h>

#include "util.h"
#include "chan_vgsm.h"
#include "module.h"
#include "comm.h"
#include "causes.h"
#include "sms.h"
#include "cbm.h"
#include "operators.h"
#include "pin.h"
#include "number.h"

#define FAILED_RETRY_TIME (30 * SEC)
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
	.debug_serial = TRUE,
#else
	.debug_generic = FALSE,
	.debug_serial = FALSE,
#endif

};

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

void vgsm_chan_put(struct vgsm_chan *vgsm_chan)
{
	if (!vgsm_chan)
		return;

	assert(vgsm_chan->refcnt > 0);
	assert(vgsm_chan->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	int refcnt = --vgsm_chan->refcnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!refcnt) {
		/* Put code here when (if) asterisk will be fixed */
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

	vgsm_module_put_null(module);

	return RESULT_SUCCESS;

err_response:
err_newpin_invalid:
	vgsm_module_put_null(module);
err_module_not_found:
err_missing_newpin:
err_oldpin_invalid:
err_missing_oldpin:
err_missing_module:

	return err;
}

static char *vgsm_pin_set_complete(char *line, char *word, int pos, int state)
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
"	Set, enable or disable the PIN on the SIM installed in module \n"
"	<module>.\n";

static struct ast_cli_entry vgsm_pin_set =
{
	{ "vgsm", "pin", "set", NULL },
	do_vgsm_pin_set,
	"Set PIN on the selected module",
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

	ast_config_destroy(cfg);

	vgsm_operators_init();
}

/*---------------------------------------------------------------------------*/

static int do_debug_vgsm_serial(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug_serial = TRUE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging enabled\n");

	return RESULT_SUCCESS;
}

static char debug_vgsm_serial_help[] =
"Usage: debug vgsm serial\n"
"	Debug vGSM's serial communication\n";

static struct ast_cli_entry debug_vgsm_serial =
{
	{ "debug", "vgsm", "serial", NULL },
	do_debug_vgsm_serial,
	"Enables serial vGSM debugging",
	debug_vgsm_serial_help,
	NULL
};

/*---------------------------------------------------------------------------*/

static int do_no_debug_vgsm_serial(int fd, int argc, char *argv[])
{
	ast_mutex_lock(&vgsm.lock);
	vgsm.debug_serial = FALSE;
	ast_mutex_unlock(&vgsm.lock);

	ast_cli(fd, "vGSM debugging disabled\n");

	return RESULT_SUCCESS;
}

static struct ast_cli_entry no_debug_vgsm_serial =
{
	{ "no", "debug", "vgsm", "serial", NULL },
	do_no_debug_vgsm_serial,
	"Disables serial vGSM debugging",
	NULL,
	NULL
};

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
"	Debug generic vGSM events\n";

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
		ast_cli(fd, "Interface '%s' is not ready\n", module->name);
		ast_mutex_unlock(&module->lock);
		err = RESULT_FAILURE;
		goto err_module_not_ready;
	}

	if (module->sending_sms) {
		ast_cli(fd, "Interface '%s' is already sending a SMS\n",
				module->name);
		ast_mutex_unlock(&module->lock);
		err = RESULT_FAILURE;
		goto err_already_sending_sms;
	}

	module->sending_sms = TRUE;

	struct vgsm_sms *sms;
	sms = vgsm_sms_alloc();
	if (!sms) {
		ast_cli(fd,  "Cannot allocate SMS\n");
		err = RESULT_FAILURE;
		goto err_sms_alloc;
	}

	sms->module = module;

	vgsm_number_parse(
		module->current_config->sms_service_center,
		sms->smcc, sizeof(sms->smcc),
		&sms->smcc_np, &sms->smcc_ton);

	vgsm_number_parse(
		argv[4],
		sms->dest, sizeof(sms->dest),
		&sms->dest_np,
		&sms->dest_ton);

	ast_mutex_unlock(&module->lock);

	sms->timestamp = time(NULL);

	tzset();
	sms->timezone = timezone;

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

	vgsm_sms_prepare(sms);

	struct vgsm_req *req = vgsm_req_make_sms(
		&module->comm, 30 * SEC, sms->pdu, sms->pdu_len,
		"AT+CMGS=%d", sms->pdu_tp_len);
	if (!req) {
		ast_cli(fd, "Error sending SMS\n");
		err = RESULT_FAILURE;
		goto err_req_make;
	}

	vgsm_req_wait(req);

	if (req->err != VGSM_RESP_OK) {
		ast_cli(fd, "Error sending SMS: %s (%d)\n",
			vgsm_module_error_to_text(req->err),
			req->err);
		goto err_req_result;
	}

	module->sending_sms = FALSE;
 
	vgsm_req_put_null(req);

	return RESULT_SUCCESS;

err_req_result:
	vgsm_req_put_null(req);
err_req_make:
err_invalid_mbstring:
err_malloc_sms_text:
	vgsm_sms_put_null(sms);
err_sms_alloc:
	module->sending_sms = FALSE;
err_already_sending_sms:
err_module_not_ready:
err_module_not_found:
err_missing_text:
err_missing_number:
err_missing_module:

	return err;
}

static char *vgsm_send_sms_complete(char *line, char *word, int pos, int state)
{
	switch(pos) {
	case 3:
		return vgsm_module_completion(line, word,state);
	}

	return NULL;
}

static char vgsm_send_sms_help[] =
	"Usage: vgsm send sms <module> <number> <text> [class]\n"
	"	Send SMS\n";

static struct ast_cli_entry vgsm_send_sms =
{
	{ "vgsm", "send", "sms", NULL },
	do_vgsm_send_sms,
	"Send SMS",
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
	"	Reloads vGSM config\n";

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
		vgsm_module_unexpected_error(module, req->err);
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

	vgsm_req_put_null(req);
	vgsm_module_put_null(module);

	return RESULT_SUCCESS;

err_unknown_response:
err_send_pin:
err_not_waiting_pin:
	vgsm_req_put_null(req);
err_req_make:
	vgsm_module_put_null(module);
err_module_not_found:
err_pin_invalid:
err_no_pin:
err_no_module_name:

	return err;
}

static char *vgsm_pin_input_complete(char *line, char *word, int pos, int state)
{
	switch(pos) {
	case 3:
		return vgsm_module_completion(line, word, state);
	}

	return NULL;
}

static char vgsm_pin_input_help[] =
	"Usage: vgsm pin input <module> <PIN>\n"
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
		vgsm_module_unexpected_error(module, err);
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

	vgsm_req_put_null(req);
	vgsm_module_put_null(module);

	return RESULT_SUCCESS;

err_unknown_response:
err_invalid_state:
	vgsm_req_put_null(req);
err_req_make:
	vgsm_module_put_null(module);
err_module_not_found:
err_newpin_invalid:
err_no_newpin:
err_puk_invalid:
err_no_puk:
err_no_module_name:

	return err;
}

static char *vgsm_puk_input_complete(char *line, char *word, int pos, int state)
{
	switch(pos) {
	case 3:
		return vgsm_module_completion(line, word, state);
	}

	return NULL;
}

static char vgsm_puk_input_help[] =
	"Usage: vgsm puk input <module> <PUK>\n"
	"	Manually input PUK to selected module\n";

static struct ast_cli_entry vgsm_puk_input =
{
	{ "vgsm", "puk", "input", NULL },
	do_vgsm_puk_input,
	"Manually input PUK to selected module",
	vgsm_puk_input_help,
	vgsm_puk_input_complete,
};

/*---------------------------------------------------------------------------*/

static int vgsm_connect_channel(struct vgsm_chan *vgsm_chan)
{
	if (ioctl(vgsm_chan->module->comm.fd, VGSM_IOC_GET_CHANID,
		(caddr_t)&vgsm_chan->module_channel_id) < 0) {

		ast_log(LOG_ERROR,
			"ioctl(VGSM_IOC_GET_CHANID): %s\n",
			strerror(errno));
		goto err_ioctl_vgsm_get_chanid;
	}

	vgsm_chan->sp_fd = open("/dev/visdn/streamport", O_RDWR);
	if (vgsm_chan->sp_fd < 0) {
		ast_log(LOG_ERROR,
			"Cannot open streamport: %s\n",
			strerror(errno));
		goto err_open_streamport;
	}

	if (ioctl(vgsm_chan->sp_fd, VISDN_SP_GET_CHANID,
		(caddr_t)&vgsm_chan->sp_channel_id) < 0) {

		ast_log(LOG_ERROR,
			"ioctl(VISDN_IOC_GET_CHANID): %s\n",
			strerror(errno));
		goto err_ioctl_visdn_get_chanid;
	}

	vgsm_debug_generic("Connecting streamport %06d to chan %06d\n",
			vgsm_chan->sp_channel_id,
			vgsm_chan->module_channel_id);

	struct visdn_connect vc;
	vc.src_chan_id = vgsm_chan->sp_channel_id;
	vc.dst_chan_id = vgsm_chan->module_channel_id;
	vc.flags = 0;

	if (ioctl(vgsm.router_control_fd, VISDN_IOC_CONNECT,
						(caddr_t) &vc) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VISDN_CONNECT, sp, isdn): %s\n",
			strerror(errno));
		goto err_ioctl_connect_pipeline;
	}

	vgsm_chan->pipeline_id = vc.pipeline_id;

	memset(&vc, 0, sizeof(vc));
	vc.pipeline_id = vgsm_chan->pipeline_id;

	if (ioctl(vgsm.router_control_fd, VISDN_IOC_PIPELINE_OPEN,
						(caddr_t)&vc) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VISDN_PIPELINE_OPEN, isdn): %s\n",
			strerror(errno));
		goto err_ioctl_enable_pipeline;
	}

	memset(&vc, 0, sizeof(vc));
	vc.pipeline_id = vgsm_chan->pipeline_id;

	if (ioctl(vgsm.router_control_fd, VISDN_IOC_PIPELINE_START,
						(caddr_t)&vc) < 0) {
		ast_log(LOG_ERROR,
			"ioctl(VISDN_PIPELINE_START, isdn): %s\n",
			strerror(errno));
		goto err_ioctl_enable_pipeline;
	}

	return 0;

err_ioctl_enable_pipeline:
err_ioctl_connect_pipeline:
err_ioctl_visdn_get_chanid:
	close(vgsm_chan->sp_fd);
err_open_streamport:
err_ioctl_vgsm_get_chanid:

	return -1;
}

static void vgsm_destroy(struct vgsm_chan *vgsm_chan)
{
	if (vgsm_chan->module)
		vgsm_module_put(vgsm_chan->module);

	free(vgsm_chan);
}

static struct vgsm_chan *vgsm_alloc()
{
	struct vgsm_chan *vgsm_chan;

	vgsm_chan = malloc(sizeof(*vgsm_chan));
	if (!vgsm_chan)
		return NULL;

	memset(vgsm_chan, 0, sizeof(*vgsm_chan));

	vgsm_chan->refcnt = 1;

	vgsm_chan->sp_fd = -1;

	return vgsm_chan;
}

static const struct ast_channel_tech vgsm_tech;

static struct ast_channel *vgsm_new(
	struct vgsm_chan *vgsm_chan,
	int state)
{
	struct ast_channel *ast_chan;
	ast_chan = ast_channel_alloc(1);
	if (!ast_chan) {
		ast_log(LOG_WARNING, "Unable to allocate channel\n");
		goto err_channel_alloc;
	}

	ast_chan->type = VGSM_CHAN_TYPE;

	ast_chan->fds[0] = open("/dev/visdn/timer", O_RDONLY);
	if (ast_chan->fds[0] < 0) {
		ast_log(LOG_ERROR, "Unable to open timer: %s\n",
			strerror(errno));
		goto err_open_timer;
	}

	if (state == AST_STATE_RING)
		ast_chan->rings = 1;

	ast_chan->adsicpe = AST_ADSI_UNAVAILABLE;

	ast_chan->nativeformats = AST_FORMAT_ALAW;
	ast_chan->readformat = AST_FORMAT_ALAW;
	ast_chan->rawreadformat = AST_FORMAT_ALAW;
	ast_chan->writeformat = AST_FORMAT_ALAW;
	ast_chan->rawwriteformat = AST_FORMAT_ALAW;

	vgsm_chan->ast_chan = ast_chan;
	ast_chan->tech_pvt = vgsm_chan;
	ast_chan->tech = &vgsm_tech;

	vgsm_chan->dsp = ast_dsp_new();
	ast_dsp_set_features(vgsm_chan->dsp, DSP_FEATURE_DTMF_DETECT);

	ast_setstate(ast_chan, state);

	return ast_chan;

	close(ast_chan->fds[0]);
err_open_timer:
	ast_hangup(ast_chan);
err_channel_alloc:

	return NULL;
}

struct vgsm_chan *vgsm_alloc_inbound_call(struct vgsm_module *module)
{
	struct vgsm_chan *vgsm_chan;
	vgsm_chan = vgsm_alloc();
	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "Cannot allocate vgsm_chan\n");
		goto err_vgsm_alloc;
	}

	ast_mutex_lock(&module->lock);

	vgsm_chan->module = vgsm_module_get(module);
	vgsm_chan->mc = vgsm_module_config_get(module->current_config);

	ast_mutex_unlock(&module->lock);

	struct ast_channel *ast_chan;
	ast_chan = vgsm_new(vgsm_chan, AST_STATE_OFFHOOK);
	if (!ast_chan)
		goto err_vgsm_new;

	ast_dsp_digitmode(vgsm_chan->dsp,
		DSP_DIGITMODE_DTMF |
		vgsm_chan->mc->dtmf_quelch ? 0 : DSP_DIGITMODE_NOQUELCH |
		vgsm_chan->mc->dtmf_mutemax ? DSP_DIGITMODE_MUTEMAX : 0 |
		vgsm_chan->mc->dtmf_relax ? DSP_DIGITMODE_RELAXDTMF : 0);

	ast_mutex_lock(&vgsm.usecnt_lock);
	vgsm.usecnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);
	ast_update_use_count();

	strcpy(ast_chan->exten, "s");
	strncpy(ast_chan->context, vgsm_chan->mc->context,
					sizeof(ast_chan->context));
	ast_chan->priority = 1;

	snprintf(ast_chan->name, sizeof(ast_chan->name),
		"VGSM/%s/%d", module->name, 1);

	ast_setstate(ast_chan, AST_STATE_RING);

	return vgsm_chan;

err_vgsm_new:
	vgsm_destroy(vgsm_chan);
err_vgsm_alloc:

	return NULL;
}

static int vgsm_call(
	struct ast_channel *ast_chan,
	char *orig_dest,
	int timeout)
{
	int err;
	char dest[256];

	strncpy(dest, orig_dest, sizeof(dest));

	// Parse destination and obtain module name + number
	const char *module_name;
	const char *number;
	char *stringp = dest;

	module_name = strsep(&stringp, "/");
	if (!module_name) {
		ast_log(LOG_WARNING,
			"Invalid destination '%s' format (module/number)\n",
			dest);

		err = -1;
		goto err_invalid_destination;
	}

	number = strsep(&stringp, "/");
	if (!number) {
		ast_log(LOG_WARNING,
			"Invalid destination '%s' format (module/number)\n",
			dest);

		err = -1;
		goto err_invalid_format;
	}

	struct vgsm_module *module;
	module = vgsm_module_get_by_name(module_name);
	if (!module) {
		ast_log(LOG_WARNING, "Interface %s not found\n", module_name);
		err = -1;
		goto err_module_not_found;
	}

	if ((ast_chan->_state != AST_STATE_DOWN) &&
	    (ast_chan->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING,
			"vgsm_call called on %s,"
			" neither down nor reserved\n",
			ast_chan->name);

		err = -1;
		goto err_channel_not_down;
	}

	ast_mutex_lock(&module->lock);

	if (!module->status == VGSM_MODULE_STATUS_READY) {
		ast_mutex_unlock(&module->lock);

		ast_log(LOG_DEBUG, "Interface %s is not ready\n", module_name);
		err = -1;
		goto err_module_not_ready;
	}

	if (module->net.status != VGSM_NET_STATUS_REGISTERED_HOME &&
	    module->net.status != VGSM_NET_STATUS_REGISTERED_ROAMING) {
		ast_mutex_unlock(&module->lock);

		ast_log(LOG_DEBUG, "Interface %s not registered\n",
			module_name);
		err = -1;
		goto err_module_not_registered;
	}

	if (module->vgsm_chan) {
		ast_mutex_unlock(&module->lock);

		ast_log(LOG_DEBUG, "Interface %s is busy (call present)\n",
			module_name);
		err = -1;
		goto err_module_busy;
	}

	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);

	module->vgsm_chan = vgsm_chan_get(vgsm_chan);
	vgsm_chan->module = vgsm_module_get(module);
	vgsm_chan->mc = vgsm_module_config_get(module->current_config);

	ast_dsp_digitmode(vgsm_chan->dsp,
		DSP_DIGITMODE_DTMF |
		vgsm_chan->mc->dtmf_quelch ? 0 : DSP_DIGITMODE_NOQUELCH |
		vgsm_chan->mc->dtmf_mutemax ? DSP_DIGITMODE_MUTEMAX : 0 |
		vgsm_chan->mc->dtmf_relax ? DSP_DIGITMODE_RELAXDTMF : 0);

	if (option_debug)
		ast_log(LOG_DEBUG, "Calling %s on %s\n", dest, ast_chan->name);

	char newname[40];
	snprintf(newname, sizeof(newname), "VGSM/%s/%d", module->name, 1);

	ast_change_name(ast_chan, newname);

	ast_setstate(ast_chan, AST_STATE_DIALING);

	ast_mutex_unlock(&module->lock);

	struct vgsm_req *req;
	// 'timeout' instead of 20s ?
	req = vgsm_req_make_wait(
			&module->comm, 180 * SEC,
			"ATD%c%s;",
			((ast_chan->cid.cid_pres & AST_PRES_RESTRICTION) ==
				AST_PRES_ALLOWED) ? 'i' : 'I',
			number);
	if (!req) {
		ast_log(LOG_DEBUG, "%s: Unable to dial: ATD failed\n",
			module_name);
		err = -1;
		goto err_atd_failed;
	}

	if (req->err != VGSM_RESP_OK) {
		ast_verbose("Unable to dial: %s (%d)\n",
			vgsm_module_error_to_text(req->err),
			req->err);
		vgsm_req_put_null(req);

		err = -1;
		goto err_atd_failed;
	}

	vgsm_req_put_null(req);

	vgsm_connect_channel(vgsm_chan);

	ast_queue_control(ast_chan, AST_CONTROL_PROCEEDING);

	vgsm_module_put(module);

	return 0;

err_atd_failed:
	vgsm_module_config_put(vgsm_chan->mc);
	vgsm_chan->mc = NULL;
	vgsm_module_put(vgsm_chan->module);
	vgsm_chan->module = NULL;
err_module_busy:
err_module_not_registered:
err_module_not_ready:
err_channel_not_down:
	vgsm_module_put(module);
err_module_not_found:
err_invalid_format:
err_invalid_destination:

	return err;
}

static int vgsm_answer(struct ast_channel *ast_chan)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	struct vgsm_module *module = vgsm_chan->module;
	int err;

	vgsm_debug_generic("vgsm_answer\n");

	ast_mutex_lock(&module->lock);
	if (module->status != VGSM_MODULE_STATUS_READY) {
		ast_mutex_unlock(&module->lock);

		ast_log(LOG_NOTICE, "Interface is not ready anymore\n");
		return -1;
	}

	vgsm_connect_channel(vgsm_chan);

	ast_mutex_unlock(&module->lock);

	ast_indicate(ast_chan, -1);

	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "NO VGSM_CHAN!!\n");
		return -1;
	}

	err = vgsm_req_make_wait_result(&module->comm, 1 * SEC, "ATA");
	if (err != VGSM_RESP_OK) {
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
static int vgsm_indicate(struct ast_channel *ast_chan, int condition)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	struct vgsm_module *module = vgsm_chan->module;

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
		vgsm_module_counter_inc(module,
			VGSM_CAUSE_LOCATION_LOCAL,
			VGSM_CAUSE_REASON_NORMAL_CALL_CLEARING);

		vgsm_module_hangup(module);
	break;

	case AST_CONTROL_CONGESTION:
		vgsm_module_counter_inc(module,
			VGSM_CAUSE_LOCATION_LOCAL,
			VGSM_CAUSE_REASON_CONGESTION);

		vgsm_module_hangup(module);
	break;

	case AST_CONTROL_BUSY:
		vgsm_module_counter_inc(module,
			VGSM_CAUSE_LOCATION_LOCAL,
			VGSM_CAUSE_REASON_USER_BUSY);

		vgsm_module_hangup(module);
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

static int vgsm_send_digit(struct ast_channel *ast_chan, char digit)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	struct vgsm_module *module = vgsm_chan->module;

	vgsm_req_put(vgsm_req_make(&module->comm, 2 * SEC, "AT+VTS=%c", digit));

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
	if (vgsm_chan->sp_fd < 0)
		return;

	struct visdn_connect vc;

	memset(&vc, 0, sizeof(vc));
	vc.pipeline_id = vgsm_chan->sp_module_pipeline_id;
	strcpy(vc.from_endpoint, "");
	strcpy(vc.to_endpoint, "");
	vc.flags = 0;

	if (ioctl(vgsm.router_control_fd,
			VISDN_IOC_DISCONNECT,
			(caddr_t)&vc) < 0) {

		ast_log(LOG_ERROR,
			"ioctl(VISDN_IOC_DISCONNECT, sp=>me):"
			" %s\n",
			strerror(errno));
	}

	memset(&vc, 0, sizeof(vc));
	vc.pipeline_id = vgsm_chan->module_sp_pipeline_id;
	strcpy(vc.from_endpoint, "");
	strcpy(vc.to_endpoint, "");
	vc.flags = 0;

	if (ioctl(vgsm.router_control_fd,
			VISDN_IOC_DISCONNECT,
			(caddr_t)&vc) < 0) {

		ast_log(LOG_ERROR,
			"ioctl(VISDN_IOC_DISCONNECT, me=>sp):"
			" %s\n",
			strerror(errno));
	}

	if (close(vgsm_chan->sp_fd) < 0) {
		ast_log(LOG_ERROR,
			"close(vgsm_chan->sp_fd): %s\n",
			strerror(errno));
	}

	vgsm_chan->sp_fd = -1;
}

static int vgsm_hangup(struct ast_channel *ast_chan)
{
	vgsm_debug_generic("vgsm_hangup %s\n", ast_chan->name);

	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);

	ast_mutex_lock(&ast_chan->lock);

	if (vgsm_chan) {
		if (vgsm_chan->module) {
			vgsm_module_hangup(vgsm_chan->module);

			vgsm_module_counter_inc(vgsm_chan->module,
				VGSM_CAUSE_LOCATION_LOCAL,
				VGSM_CAUSE_REASON_NORMAL_CALL_CLEARING);
		}

		if (vgsm_chan->sp_fd >= 0)
			vgsm_disconnect_channel(vgsm_chan);

		ast_mutex_unlock(&ast_chan->lock);
		/* Wait for references to vgsm_chan to be released */
		int refcnt;
		do {
			ast_mutex_lock(&vgsm.usecnt_lock);
			refcnt = vgsm_chan->refcnt;
			ast_mutex_unlock(&vgsm.usecnt_lock);

			usleep(100000);
		} while(refcnt > 1);
		ast_mutex_lock(&ast_chan->lock);

		vgsm_destroy(vgsm_chan);
		ast_chan->tech_pvt = NULL;
	}

	if (vgsm_chan->dsp) {
		ast_dsp_free(vgsm_chan->dsp);
		vgsm_chan->dsp = NULL;
	}

	close(ast_chan->fds[0]);

	ast_setstate(ast_chan, AST_STATE_DOWN);

	ast_mutex_unlock(&ast_chan->lock);

	vgsm_debug_generic("vgsm_hangup complete\n");

	return 0;
}

static struct ast_frame *vgsm_read(struct ast_channel *ast_chan)
{
	struct vgsm_chan *vgsm_chan = to_vgsm_chan(ast_chan);
	static struct ast_frame f;
	char buf[512];

	read(ast_chan->fds[0], buf, 1);

	f.src = VGSM_CHAN_TYPE;
	f.mallocd = 0;
	f.delivery.tv_sec = 0;
	f.delivery.tv_usec = 0;

	if (vgsm_chan->sp_fd < 0) {
		f.frametype = AST_FRAME_NULL;
		f.subclass = 0;
		f.samples = 0;
		f.datalen = 0;
		f.data = NULL;
		f.offset = 0;

		return &f;
	}

	int nread = read(vgsm_chan->sp_fd, buf, sizeof(buf));
	if (nread < 0) {
		ast_log(LOG_WARNING, "read error: %s\n", strerror(errno));
		return &f;
	}

#if 0
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
#endif

	f.frametype = AST_FRAME_VOICE;
	f.subclass = AST_FORMAT_ALAW;
	f.samples = nread;
	f.datalen = nread;
	f.data = buf;
	f.offset = 0;

	struct ast_frame *f2 = ast_dsp_process(ast_chan, vgsm_chan->dsp, &f);

	return f2;
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

	if (frame->subclass != AST_FORMAT_ALAW) {
		ast_log(LOG_WARNING,
			"Cannot handle frames in %d format\n",
			frame->subclass);
		return 0;
	}

	if (vgsm_chan->sp_fd < 0) {
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

	write(vgsm_chan->sp_fd, frame->data, frame->datalen);

	return 0;
}

static struct ast_channel *vgsm_request(
	const char *type, int format, void *data, int *cause)
{
	struct vgsm_chan *vgsm_chan;

	if (!(format & AST_FORMAT_ALAW)) {
		ast_log(LOG_NOTICE,
			"Asked to get a channel of unsupported format '%d'\n",
			format);
		goto err_unsupported_format;
	}

	vgsm_chan = vgsm_alloc();
	if (!vgsm_chan) {
		ast_log(LOG_ERROR, "Cannot allocate vgsm_chan\n");
		goto err_vgsm_alloc;
	}

	struct ast_channel *ast_chan;
	ast_chan = vgsm_new(vgsm_chan, AST_STATE_DOWN);

	if (!ast_chan)
		goto err_vgsm_new;

	snprintf(ast_chan->name, sizeof(ast_chan->name), "VGSM/null");

	ast_mutex_lock(&vgsm.usecnt_lock);
	vgsm.usecnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);
	ast_update_use_count();

	return ast_chan;

err_vgsm_new:
	vgsm_destroy(vgsm_chan);
err_vgsm_alloc:
err_unsupported_format:

	return NULL;
}

static const struct ast_channel_tech vgsm_tech = {
	.type		= VGSM_CHAN_TYPE,
	.description	= VGSM_DESCRIPTION,
	.capabilities	= AST_FORMAT_ALAW,
	.requester	= vgsm_request,
	.call		= vgsm_call,
	.hangup		= vgsm_hangup,
	.answer		= vgsm_answer,
	.read		= vgsm_read,
	.write		= vgsm_write,
	.indicate	= vgsm_indicate,
	.transfer	= vgsm_transfer,
	.fixup		= vgsm_fixup,
	.send_digit	= vgsm_send_digit,
	.bridge		= vgsm_bridge,
	.send_text	= vgsm_sendtext,
	.setoption	= vgsm_setoption,
};

/***********************************************/

/*! \brief  manager_vgsm_sms_tx: Send a text sms with VGSM card ---*/

static int manager_vgsm_sms_tx(struct mansession *s, struct message *m)
{
	char *module_str = astman_get_header(m, "Interface");
	char *number = astman_get_header(m, "Destination");
	char *class_str = astman_get_header(m, "Class");
	char *content = astman_get_header(m, "Content");
	char *hex_content = astman_get_header(m, "HexContent");

	if (!strlen(number)) {
		astman_send_error(s, m, "Destination missing");
		goto err_missing_destination;
	}

	if (!strlen(content) && !strlen(hex_content)) {
		astman_send_error(s, m, "Content missing");
		goto err_missing_content;
	}

	/* start module guessing */
	struct vgsm_module *module;
	int found = 0;

	/* if user has specified an module, let's find it */
	if(strlen(module_str)) {
		module = vgsm_module_get_by_name(module_str);
		if (!module) {
			astman_send_error(s, m, "Cannot find module");
			goto err_module_not_found;
		}
	} else {
		/* if user has not specified an module, get the first available */
		ast_mutex_lock(&vgsm.lock);
		list_for_each_entry(module, &vgsm.ifs, ifs_node) {
			        ast_mutex_lock(&module->lock);
			        if (module->status == VGSM_MODULE_STATUS_READY) {
	                		ast_mutex_unlock(&module->lock);
					found = 1;
					break;
	        		}
	        		ast_mutex_unlock(&module->lock);
		}
		ast_mutex_unlock(&vgsm.lock);
		if (!found) {
			astman_send_error(s, m, "Cannot find any module");
			goto err_module_not_found;
		}
	}

	/* check again that the module is available */
	ast_mutex_lock(&module->lock);
	if (module->status == VGSM_MODULE_STATUS_READY) {
		if (module->net.status != VGSM_NET_STATUS_REGISTERED_HOME &&
		    module->net.status != VGSM_NET_STATUS_REGISTERED_ROAMING) {
			ast_log(LOG_DEBUG, "Interface %s not registered\n",
				module->name);
			goto err_module_not_registered;
		}

		module->sending_sms = TRUE;
	} else {
		astman_send_error(s, m,
			"Interface is busy");
		ast_mutex_unlock(&module->lock);
		goto err_module_not_ready;
	}

	/* now the module is ok, starting sms setup */
	struct vgsm_sms *sms;
	sms = vgsm_sms_alloc();
	if (!sms) {
		astman_send_error(s, m,
			"Cannot allocate SMS");
		goto err_sms_alloc;
	}

	sms->module = module;

	vgsm_number_parse(
		module->current_config->sms_service_center,
		sms->smcc, sizeof(sms->smcc),
		&sms->smcc_np, &sms->smcc_ton);

	vgsm_number_parse(
		number,
		sms->dest, sizeof(sms->dest),
		&sms->dest_np,
		&sms->dest_ton);

	ast_mutex_unlock(&module->lock);

	sms->timestamp = time(NULL);

	tzset();
	sms->timezone = timezone;

	if (strlen(class_str))
		sms->message_class = atoi(class_str);
	else
		sms->message_class = 1;

	size_t content_size;
	char *content_raw;

	if (strlen(hex_content)) {
		content_size = strlen(hex_content) / 2;
		content_raw = alloca(content_size + 1);
		if (!content_raw)
			goto err_alloc_content;

		int i;
		for(i=0; i<content_size; i++) {
			content_raw[i] =
				char_to_hexdigit(hex_content[i * 2]) << 4 |
				char_to_hexdigit(hex_content[i * 2 + 1]);
		}

		content_raw[content_size] = '\0';
	} else {
		content_size = strlen(content);
		content_raw = content;
	}

	iconv_t cd = iconv_open("WCHAR_T", "UTF-8");
	if (cd < 0) {
		ast_log(LOG_ERROR, "Cannot open iconv context; %s\n",
			strerror(errno));
		goto err_iconv_open;
	}

	char *inbuf = (char *)content_raw;
	size_t inbytes = content_size;
	size_t outbytes = 1024;

	inbuf = (char *)content_raw;
	inbytes = content_size;

	sms->text = malloc(outbytes + sizeof(wchar_t));
	if (!sms->text) {
		iconv_close(cd);
		goto err_text_malloc;
	}

	char *outbuf = (char *)sms->text;
	if (iconv(cd, &inbuf, &inbytes, &outbuf, &outbytes) < 0) {
		iconv_close(cd);
		goto err_iconv;
	}

	iconv_close(cd);

	outbuf[outbytes] = L'\0';

	vgsm_sms_prepare(sms);

	struct vgsm_req *req = vgsm_req_make_sms(
		&module->comm, 30 * SEC, sms->pdu, sms->pdu_len,
		"AT+CMGS=%d", sms->pdu_tp_len);
	if (!req) {
		ast_log(LOG_NOTICE,"vgsm: Error requesting SMS\n");
		astman_send_error(s, m, "Error sending SMS");
		goto err_req_make;
	}

	vgsm_req_wait(req);

	if (req->err != VGSM_RESP_OK) {
		ast_log(LOG_NOTICE,"SMStx: Request Response is not OK\n");
		astman_send_error(s, m, "Error sending SMS");
		goto err_req_result;
	}

	vgsm_req_put_null(req);

	astman_send_ack(s, m, "VGSMsmstx: SMS sent");

       module->sending_sms = FALSE;

	return 0;

err_req_result:
	vgsm_req_put_null(req);
err_req_make:
	module->sending_sms = FALSE;
err_iconv:
	free(sms->text);
	sms->text = NULL;
err_text_malloc:
err_iconv_open:
err_alloc_content:
	vgsm_sms_put_null(sms);
err_sms_alloc:
err_module_not_ready:
err_module_not_registered:
err_module_not_found:
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

int load_module()
{
	int err;

	// Initialize q.931 library.
	// No worries, internal structures are read-only and thread safe
	ast_mutex_init(&vgsm.lock);

	ast_mutex_init(&vgsm.usecnt_lock);

	INIT_LIST_HEAD(&vgsm.ifs);
	INIT_LIST_HEAD(&vgsm.op_list);

	vgsm.default_mc = vgsm_module_config_alloc();
	vgsm_module_config_default(vgsm.default_mc);

	vgsm.router_control_fd = open("/dev/visdn/router-control", O_RDWR);
	if (vgsm.router_control_fd < 0) {
		ast_log(LOG_ERROR, "Unable to open router-control: %s\n",
			strerror(errno));
		err = -1;
		goto err_open_router_control;
	}

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
	ast_cli_register(&debug_vgsm_serial);
	ast_cli_register(&no_debug_vgsm_serial);
	ast_cli_register(&vgsm_reload);
	ast_cli_register(&vgsm_send_sms);
	ast_cli_register(&vgsm_pin_input);
	ast_cli_register(&vgsm_puk_input);
	ast_cli_register(&vgsm_pin_set);

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
	ast_cli_unregister(&vgsm_pin_set);
	ast_cli_unregister(&vgsm_puk_input);
	ast_cli_unregister(&vgsm_pin_input);
	ast_cli_unregister(&vgsm_send_sms);
	ast_cli_unregister(&vgsm_reload);
	ast_cli_unregister(&no_debug_vgsm_serial);
	ast_cli_unregister(&debug_vgsm_serial);
	ast_cli_unregister(&no_debug_vgsm_generic);
	ast_cli_unregister(&debug_vgsm_generic);

	close(vgsm.router_control_fd);
err_open_router_control:
	vgsm_module_config_put(vgsm.default_mc);

	return err;
}

int unload_module(void)
{
	vgsm_module_module_unload();

	ast_cli_unregister(&vgsm_pin_set);
	ast_cli_unregister(&vgsm_puk_input);
	ast_cli_unregister(&vgsm_pin_input);
	ast_cli_unregister(&vgsm_send_sms);
	ast_cli_unregister(&vgsm_reload);
	ast_cli_unregister(&no_debug_vgsm_serial);
	ast_cli_unregister(&debug_vgsm_serial);
	ast_cli_unregister(&no_debug_vgsm_generic);
	ast_cli_unregister(&debug_vgsm_generic);

	ast_channel_unregister(&vgsm_tech);

	close(vgsm.router_control_fd);

	vgsm_module_config_put(vgsm.default_mc);

	return 0;
}

int reload(void)
{
	vgsm_reload_config();

	return 0;
}

int usecount()
{
	int res;
	ast_mutex_lock(&vgsm.usecnt_lock);
	res = vgsm.usecnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);
	return res;
}

char *description()
{
	return VGSM_DESCRIPTION;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
