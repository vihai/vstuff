/*
 * vISDN channel driver for Asterisk
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

/*#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <signal.h>
#include <ctype.h>

#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <ifaddrs.h>
#include <netinet/in.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <net/if_arp.h>

#include <linux/rtc.h>

#include <asterisk/lock.h>*/
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
/*#include <asterisk/config.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <asterisk/callerid.h>
#include <asterisk/indications.h>
#include <asterisk/cli.h>
#include <asterisk/musiconhold.h>

#include <streamport.h>
#include <lapd.h>
#include <libq931/q931.h>

#include <visdn.h>

//#include "echo.h"

#include "../config.h"*/

#include <libq931/list.h>

struct visdn_suspended_call
{
	struct list_head node;

	struct ast_channel *ast_chan;
	struct q931_channel *q931_chan;

	char call_identity[10];
	int call_identity_len;

	time_t old_when_to_hangup;
};

struct visdn_chan {
	struct ast_channel *ast_chan;
	struct q931_call *q931_call;
	struct visdn_suspended_call *suspended_call;

	int channel_fd;

	char calling_number[21];
	char called_number[21];
	int sending_complete;

	char visdn_chanid[30];

//	echo_can_state_t *ec;
};
