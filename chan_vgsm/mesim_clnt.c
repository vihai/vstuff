/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
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
#include <sys/termios.h>
#include <sys/signal.h>
#include <ctype.h>
#include <netinet/tcp.h>

#include "../config.h"

#include <asterisk/lock.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/version.h>

#include <linux/vgsm2.h>

#define VGSM_MESIM_DRIVER_PRIVATE

#include "chan_vgsm.h"
#include "util.h"
#include "timer.h"
#include "mesim.h"
#include "mesim_clnt.h"

static struct vgsm_mesim_driver vgsm_mesim_driver_clnt;

struct vgsm_mesim_driver *vgsm_mesim_clnt_create(
	struct vgsm_mesim *mesim,
	struct vgsm_timerset *timerset)
{
	struct vgsm_mesim_clnt *mesim_clnt;

	mesim_clnt = malloc(sizeof(*mesim_clnt));
	if (!mesim_clnt)
		return NULL;

	memcpy(&mesim_clnt->driver, &vgsm_mesim_driver_clnt,
				sizeof(mesim_clnt->driver));

	mesim_clnt->mesim = mesim;

	mesim_clnt->listen_fd = -1;
	mesim_clnt->sock_fd = -1;

	return &mesim_clnt->driver;
}

void vgsm_mesim_clnt_destroy(struct vgsm_mesim_driver *driver)
{
	if (driver->release)
		driver->release(driver);
}

static void vgsm_mesim_clnt_release(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_clnt *mesim_clnt =
			container_of(driver, struct vgsm_mesim_clnt, driver);

	free(mesim_clnt);
}

static void vgsm_mesim_clnt_deactivate(struct vgsm_mesim_driver *driver)
{
//	struct vgsm_mesim_clnt *mesim_clnt =
//			container_of(driver, struct vgsm_mesim_clnt, driver);
}

static void vgsm_mesim_clnt_activate(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_clnt *mesim_clnt =
			container_of(driver, struct vgsm_mesim_clnt, driver);
	struct vgsm_mesim *mesim = mesim_clnt->mesim;

	assert(mesim_clnt->listen_fd == -1);
	assert(mesim_clnt->sock_fd == -1);

	if (ioctl(mesim->fd, VGSM_IOC_SET_SIM_ROUTE,
				VGSM_SIM_ROUTE_EXTERNAL) < 0) {
		ast_log(LOG_ERROR,
			"%s: ioctl(IOC_SET_SIM_ROUTE, EXTERN)"
			" failed: %s\n",
			mesim->name,
			strerror(errno));
		return;
	}

	mesim_clnt->bind_addr.sin_family = AF_INET;

	mesim_clnt->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (mesim_clnt->listen_fd < 0) {
		ast_log(LOG_WARNING,
			"Unable to create MESIM socket: %s\n",
			strerror(errno));
		return;
	}

	int on = 1;
	if(setsockopt(mesim_clnt->listen_fd, SOL_SOCKET, SO_REUSEADDR,
					&on, sizeof(on)) == -1) {
		ast_log(LOG_ERROR,
			"Set Socket Options failed: %s\n",
			strerror(errno));
		return;
	}

	if (bind(mesim_clnt->listen_fd,
		(struct sockaddr *)&mesim_clnt->bind_addr,
		sizeof(mesim_clnt->bind_addr)) < 0) {
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
		{
		char tmpstr[32];
		ast_inet_ntoa(tmpstr, sizeof(tmpstr),
			mesim_clnt->bind_addr.sin_addr);

			ast_log(LOG_WARNING,
				"Unable to bind MESIM socket to"
				" %s:%d: %s\n",
				tmpstr,
				ntohs(mesim_clnt->bind_addr.sin_port),
				strerror(errno));
		}
#else
			ast_log(LOG_WARNING,
				"Unable to bind MESIM socket to"
				" %s:%d: %s\n",
				ast_inet_ntoa(mesim_clnt->bind_addr.
						sin_addr),
				ntohs(mesim_clnt->bind_addr.sin_port),
				strerror(errno));
#endif

			return;
		}

	if (listen(mesim_clnt->listen_fd, 10)) {
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
		{
		char tmpstr[32];
		ast_inet_ntoa(tmpstr, sizeof(tmpstr),
			mesim_clnt->bind_addr.sin_addr);

		ast_log(LOG_WARNING,
			"Unable to start listening on"
			" %s:%d: %s\n",
				tmpstr,
				ntohs(mesim_clnt->bind_addr.sin_port),
				strerror(errno));
		}
#else
		ast_log(LOG_WARNING,
			"Unable to start listening on"
			" %s:%d: %s\n",
				ast_inet_ntoa(mesim_clnt->bind_addr.
						sin_addr),
				ntohs(mesim_clnt->bind_addr.sin_port),
				strerror(errno));
#endif
		return;
	}

	vgsm_mesim_change_state(mesim, VGSM_MESIM_HOLDER_REMOVED, -1);
}

static void vgsm_mesim_clnt_set_mode(
	struct vgsm_mesim_driver *driver,
	struct vgsm_mesim_message *msg)
{
//	struct vgsm_mesim_clnt *mesim_clnt =
//			container_of(driver, struct vgsm_mesim_clnt, driver);

//	struct vgsm_mesim_set_mode *sm = (void *)msg->data;

//	assert(msg->len == sizeof(*sm));

//	memcpy(&mesim_clnt->simclient_addr,
//		&sm->clnt.simclient_addr,
//		sizeof(mesim_clnt->simclient_addr));
}

static void vgsm_mesim_clnt_reset_asserted(struct vgsm_mesim_driver *driver)
{
//	struct vgsm_mesim_clnt *mesim_clnt =
//			container_of(driver, struct vgsm_mesim_clnt, driver);

}

static void vgsm_mesim_clnt_reset_removed(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_clnt *mesim_clnt =
			container_of(driver, struct vgsm_mesim_clnt, driver);
	struct vgsm_mesim *mesim = mesim_clnt->mesim;

	vgsm_mesim_debug(mesim, "Sending ATR\n");

	__u8 atr[] = { 0x3b, 0xb0, 0x11, 0x00, 0xC0,
			0xFF, 0x1F, 0xC3, 0x42};

	if (vgsm_mesim_write(mesim, atr, sizeof(atr)) < 0) {
		ast_log(LOG_WARNING,
			"Error writing ATR to MESIM: %s\n",
			strerror(errno));
	}
}

static int vgsm_mesim_clnt_send(
	struct vgsm_mesim_driver *driver,
	void *buf, int len)
{
//	struct vgsm_mesim_clnt *mesim_clnt =
//			container_of(driver, struct vgsm_mesim_clnt, driver);

	return 0;
}

static int vgsm_mesim_clnt_receive(struct vgsm_mesim_driver *driver)
{
//	struct vgsm_mesim_clnt *mesim_clnt =
//			container_of(driver, struct vgsm_mesim_clnt, driver);

	return 0;
}

static int vgsm_mesim_clnt_get_polls(
	struct vgsm_mesim_driver *driver,
	struct pollfd *polls)
{
//	struct vgsm_mesim_clnt *mesim_clnt =
//			container_of(driver, struct vgsm_mesim_clnt, driver);

/*	polls[2].fd = mesim->impl.listen_fd;
	polls[2].events = POLLHUP | POLLERR | POLLIN;

	if (mesim->impl.sock_fd != -1) {
		polls[3].fd = mesim->impl.sock_fd;
		polls[3].events = POLLHUP | POLLERR | POLLIN;
		npolls = 4;
	} else
		npolls = 3;*/

	return 0;
}

static void vgsm_mesim_clnt_cli_show(
	struct vgsm_mesim_driver *driver, int fd)
{
//	struct vgsm_mesim_clnt *mesim_clnt =
//			container_of(driver, struct vgsm_mesim_clnt, driver);

}

static struct vgsm_mesim_driver vgsm_mesim_driver_clnt =
{
	.release	= vgsm_mesim_clnt_release,
	.deactivate	= vgsm_mesim_clnt_deactivate,
	.activate	= vgsm_mesim_clnt_activate,
	.set_mode	= vgsm_mesim_clnt_set_mode,
	.reset_asserted	= vgsm_mesim_clnt_reset_asserted,
	.reset_removed 	= vgsm_mesim_clnt_reset_removed,
	.send		= vgsm_mesim_clnt_send,
	.receive	= vgsm_mesim_clnt_receive,
	.get_polls	= vgsm_mesim_clnt_get_polls,
	.cli_show	= vgsm_mesim_clnt_cli_show,
};
