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
#include <asterisk/version.h>

#include "chan_visdn.h"
#include "overlap.h"

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int visdn_exec_overlap_dial(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	LOCAL_USER_ADD(u);

	char called_number[32] = "";

	while(ast_waitfor(chan, -1) > -1) {
		struct ast_frame *f;
		f = ast_read(chan);
		if (!f)
			break;

		if (f->frametype == AST_FRAME_DTMF) {
			ast_setstate(chan, AST_STATE_DIALING);

			if(strlen(called_number) >= sizeof(called_number)-1)
				break;

			called_number[strlen(called_number)] = f->subclass;

			if (!ast_canmatch_extension(NULL,
					chan->context,
					called_number, 1,
					chan->cid.cid_num)) {

				ast_indicate(chan, AST_CONTROL_CONGESTION);
				ast_safe_sleep(chan, 30000);
				return -1;
			}

			if (ast_exists_extension(NULL,
					chan->context,
					called_number, 1,
					chan->cid.cid_num)) {

				if (!ast_matchmore_extension(NULL,
					chan->context,
					called_number, 1,
					chan->cid.cid_num)) {

					ast_setstate(chan, AST_STATE_RING);
					ast_indicate(chan,
						AST_CONTROL_PROCEEDING);
				}

				chan->priority = 0;
				strncpy(chan->exten, called_number,
						sizeof(chan->exten));

				ast_frfree(f);

				return 0;
			}
		}

		ast_frfree(f);
	}

	LOCAL_USER_REMOVE(u);
	return -1;
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
