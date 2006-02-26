/*
 * vISDN channel driver for Asterisk
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

//#include <asterisk/astmm.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#include "../config.h"

#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/cli.h>
#include <asterisk/causes.h>
#include <asterisk/version.h>

#include "chan_visdn.h"
#include "overlap.h"

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int new_digit(
	struct ast_channel *chan,
	char *called_number,
	int called_number_size,
	char digit, int *retval)
{
	ast_setstate(chan, AST_STATE_DIALING);

	if (digit) {
		if(strlen(called_number) >= called_number_size - 1) {
			ast_log(LOG_NOTICE,
				"Maximum number of digits exceeded\n");

			chan->hangupcause =
				AST_CAUSE_INVALID_NUMBER_FORMAT;
			*retval = -1;
			return TRUE;
		}

		called_number[strlen(called_number)] = digit;
	}

	int sending_complete = FALSE;
	if (!strcmp(chan->tech->type, "VISDN")) {
		struct visdn_chan *visdn_chan = to_visdn_chan(chan);

		sending_complete = visdn_chan->sending_complete;
	}

	if (sending_complete) {
		if (!strlen(called_number))
			strcpy(called_number, "s");

		if (ast_exists_extension(NULL,
				chan->context,
				called_number, 1,
				chan->cid.cid_num)) {

			ast_indicate(chan,
				AST_CONTROL_PROCEEDING);

			chan->priority = 0;
			strncpy(chan->exten, called_number,
					sizeof(chan->exten));

			*retval = 0;
			return TRUE;
#if 0
		} else if (ast_canmatch_extension(NULL,
				chan->context,
				"pippo", 1,
//				called_number, 1, // FIXME!!!!!!!!!!!!1
				chan->cid.cid_num)) {

			ast_indicate(chan,
				AST_CONTROL_PROCEEDING);

			chan->priority = 0;
//			strncpy(chan->exten, called_number,
			strncpy(chan->exten, "pippo",
					sizeof(chan->exten));

			*retval = 0;
			return TRUE;
#endif
		} else {
			chan->hangupcause =
				AST_CAUSE_INVALID_NUMBER_FORMAT;
			*retval = -1;
			return TRUE;
		}
	} else {
		char overlap_number[32];
		snprintf(overlap_number, sizeof(overlap_number),
			"o-%s", called_number);
		if (ast_canmatch_extension(NULL,
				chan->context,
				overlap_number, 1,
				chan->cid.cid_num)) {

			if (ast_exists_extension(NULL,
					chan->context,
					overlap_number, 1,
					chan->cid.cid_num)) {

				chan->priority = 0;
				strncpy(chan->exten, overlap_number,
						sizeof(chan->exten));

				*retval = 0;
				return TRUE;
			}
		} else {
			if (!ast_canmatch_extension(NULL,
					chan->context,
					called_number, 1,
					chan->cid.cid_num)) {

				chan->hangupcause =
					AST_CAUSE_INVALID_NUMBER_FORMAT;
				*retval = -1;
				return TRUE;
			}

			if (!ast_matchmore_extension(NULL,
				chan->context,
				called_number, 1,
				chan->cid.cid_num)) {

				ast_setstate(chan, AST_STATE_RING);
				ast_indicate(chan,
					AST_CONTROL_PROCEEDING);

				chan->priority = 0;
				strncpy(chan->exten, called_number,
						sizeof(chan->exten));

				*retval = 0;
				return TRUE;
			}
		}
	}

	return FALSE;
}

static int visdn_exec_overlap_dial(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	int retval = -1;
	int do_exit = FALSE;
	LOCAL_USER_ADD(u);

	char called_number[32] = "";

	while(ast_waitfor(chan, -1) > -1 && !do_exit) {
		struct ast_frame *f;
		f = ast_read(chan);
		if (!f)
			break;

		if (f->frametype == AST_FRAME_DTMF)
			 do_exit = new_digit(chan, called_number,
					 sizeof(called_number),
					 f->subclass, &retval);

		ast_frfree(f);
	}

	LOCAL_USER_REMOVE(u);
	return retval;
}

static char *visdn_overlap_dial_descr =
"  vISDNOverlapDial():\n";

void visdn_overlap_register(void)
{
	ast_register_application(
		"VISDNOverlapDial",
		visdn_exec_overlap_dial,
		"Plays dialtone and waits for digits",
		visdn_overlap_dial_descr);
}

void visdn_overlap_unregister(void)
{
	ast_unregister_application("VISDNOverlapDial");
}
