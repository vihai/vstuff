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
#include "disconnect.h"

static int visdn_exec_disconnect(struct ast_channel *chan, void *data)
{
	ast_indicate(chan, AST_CONTROL_DISCONNECT);

	return 0;
}

static char *visdn_disconnect_descr =
"  Disconnect():\n";

void visdn_disconnect_register(void)
{
	ast_register_application(
		"Disconnect",
		visdn_exec_disconnect,
		"Send a Disconnect frame",
		visdn_disconnect_descr);
}

void visdn_disconnect_unregister(void)
{
	ast_unregister_application("Disconnect");
}
