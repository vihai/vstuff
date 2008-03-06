/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2007-2008 Daniele Orlandi
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

#include <asterisk/lock.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/version.h>
#include <asterisk/cli.h>

#include <linux/vgsm2.h>

#define VGSM_MESIM_DRIVER_PRIVATE

#include "chan_vgsm.h"
#include "util.h"
#include "timer.h"
#include "mesim.h"
#include "mesim_impl.h"

static void vgsm_mesim_impl_timer(void *data);

static struct vgsm_mesim_driver vgsm_mesim_driver_impl;

struct vgsm_mesim_driver *vgsm_mesim_impl_create(
	struct vgsm_mesim *mesim,
	struct vgsm_timerset *timerset)
{
	struct vgsm_mesim_impl *mesim_impl;

	mesim_impl = malloc(sizeof(*mesim_impl));
	if (!mesim_impl)
		return NULL;

	memcpy(&mesim_impl->driver, &vgsm_mesim_driver_impl,
				sizeof(mesim_impl->driver));

	mesim_impl->mesim = mesim;

	mesim_impl->sock_fd = -1;

	mesim_impl->state = VGSM_MESIM_IMPL_STATE_NULL;
	ast_mutex_init(&mesim_impl->state_lock);

	mesim_impl->parser_state = VGSM_MESIM_IMPL_PARSER_STATE_IDLE;

	vgsm_timer_init(&mesim_impl->timer, timerset, "mesim_impl",
			vgsm_mesim_impl_timer, mesim_impl);

	return &mesim_impl->driver;
}

void vgsm_mesim_impl_destroy(struct vgsm_mesim_driver *driver)
{
	if (driver->release)
		driver->release(driver);
}

static void vgsm_mesim_impl_release(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_impl *mesim_impl =
			container_of(driver, struct vgsm_mesim_impl, driver);

	free(mesim_impl);
}

static const char *vgsm_mesim_impl_state_to_text(
	enum vgsm_mesim_impl_state state)
{
	switch(state) {
	case VGSM_MESIM_IMPL_STATE_NULL:
		return "NULL";
	case VGSM_MESIM_IMPL_STATE_TRYING:
		return "TRYING";
	case VGSM_MESIM_IMPL_STATE_READY:
		return "READY";
	}

	return "*UNKNOWN*";
}

static void vgsm_mesim_impl_change_state(
	struct vgsm_mesim_impl *mesim_impl,
	enum vgsm_mesim_impl_state newstate)
{
	vgsm_mesim_debug(mesim_impl->mesim,
		"Implementa state changed from %s to %s\n",
		vgsm_mesim_impl_state_to_text(mesim_impl->state),
		vgsm_mesim_impl_state_to_text(newstate));

	ast_mutex_lock(&mesim_impl->state_lock);
	mesim_impl->state = newstate;
	ast_mutex_unlock(&mesim_impl->state_lock);
}

static int vgsm_mesim_impl_write_reset_asserted(
	struct vgsm_mesim_impl *mesim_impl)
{
	vgsm_mesim_debug(mesim_impl->mesim,
		"Sending '-' on implementa socket\n");

	return write(mesim_impl->sock_fd, "-", 1);
}

static int vgsm_mesim_impl_write_reset_removed(
	struct vgsm_mesim_impl *mesim_impl)
{
	vgsm_mesim_debug(mesim_impl->mesim,
		"Sending '+' on implementa socket\n");

	return write(mesim_impl->sock_fd, "+", 1);
}

static int vgsm_mesim_impl_write(
	struct vgsm_mesim_impl *mesim_impl,
	void *data, int size)
{
	vgsm_mesim_debug(mesim_impl->mesim,
		"IMPL TX (%2d): %s\n", size, (char *)data);

	return write(mesim_impl->sock_fd, data, size);
}

static int vgsm_mesim_impl_write_message(
	struct vgsm_mesim_impl *mesim_impl,
	void *data, int size)
{
	char *hextext = alloca(size * 2 + 1);

	int i;
	for(i=0; i<size; i++)
		sprintf(&hextext[i*2], "%02X", ((__u8 *)data)[i]);

	vgsm_mesim_debug(mesim_impl->mesim,
		"IMPL TX (%2d): %s\n", size, hextext);

	return write(mesim_impl->sock_fd, hextext, size * 2);
}

static void vgsm_mesim_impl_deactivate(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_impl *mesim_impl =
			container_of(driver, struct vgsm_mesim_impl, driver);

	if (mesim_impl->sock_fd != -1) {
		vgsm_mesim_debug(mesim_impl->mesim,
			"Disconnecting from Implementa's SIM client socket\n");

		shutdown(mesim_impl->sock_fd, SHUT_RDWR);
		close(mesim_impl->sock_fd);
		mesim_impl->sock_fd = -1;
	}

	vgsm_mesim_impl_change_state(mesim_impl, VGSM_MESIM_IMPL_STATE_NULL);
	vgsm_timer_stop(&mesim_impl->timer);
}

static void vgsm_mesim_impl_activate(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_impl *mesim_impl =
			container_of(driver, struct vgsm_mesim_impl, driver);

	vgsm_mesim_change_state(mesim_impl->mesim,
				VGSM_MESIM_HOLDER_REMOVED, -1);

	vgsm_mesim_impl_change_state(mesim_impl, VGSM_MESIM_IMPL_STATE_TRYING);
	vgsm_timer_start_delta(&mesim_impl->timer, 1 * SEC);
}

static void vgsm_mesim_impl_set_mode(
	struct vgsm_mesim_driver *driver,
	struct vgsm_mesim_message *msg)
{
	struct vgsm_mesim_impl *mesim_impl =
			container_of(driver, struct vgsm_mesim_impl, driver);

	struct vgsm_mesim_set_mode *sm = (void *)msg->data;

	assert(msg->len == sizeof(*sm));

	memcpy(&mesim_impl->client_addr,
		&sm->client_addr,
		sizeof(mesim_impl->client_addr));
}

static void vgsm_mesim_impl_reset_asserted(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_impl *mesim_impl =
			container_of(driver, struct vgsm_mesim_impl, driver);

	if (mesim_impl->state == VGSM_MESIM_IMPL_STATE_READY) {
		if (vgsm_mesim_impl_write_reset_asserted(mesim_impl) < 0) {
			ast_log(LOG_WARNING,
				"Error writing '-' to socket:"
				" %s\n",
				strerror(errno));
		}
	}
}

static void vgsm_mesim_impl_reset_removed(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_impl *mesim_impl =
			container_of(driver, struct vgsm_mesim_impl, driver);
	struct vgsm_mesim *mesim = mesim_impl->mesim;

	vgsm_mesim_debug(mesim, "Sending ATR\n");

	__u8 atr[] = { 0x3b, 0xb0, 0x11, 0x00, 0xC0,
			0xFF, 0x1F, 0xC3, 0x42};

	if (vgsm_mesim_write(mesim, atr, sizeof(atr)) < 0) {
		ast_log(LOG_WARNING,
			"Error writing ATR to MESIM: %s\n",
			strerror(errno));
	}

	if (vgsm_mesim_impl_write_reset_removed(mesim_impl) < 0) {
		ast_log(LOG_WARNING,
			"Error writing '+' to socket: %s\n",
			strerror(errno));
	}
}

static void vgsm_mesim_impl_timer(void *data)
{
	struct vgsm_mesim_impl *mesim_impl = data;
	struct vgsm_mesim *mesim = mesim_impl->mesim;

	vgsm_mesim_debug(mesim, "Implementa timer fired in state %s\n",
			vgsm_mesim_impl_state_to_text(mesim_impl->state));

	switch(mesim_impl->state) {
	case VGSM_MESIM_IMPL_STATE_NULL:
		assert(0);
	break;

	case VGSM_MESIM_IMPL_STATE_TRYING:

		assert(mesim_impl->sock_fd == -1);

		if (ioctl(mesim->fd, VGSM_IOC_SET_SIM_ROUTE,
					VGSM_SIM_ROUTE_EXTERNAL) < 0) {
			ast_log(LOG_ERROR,
				"%s: ioctl(IOC_SET_SIM_ROUTE, EXTERN)"
				" failed: %s\n",
				mesim->name,
				strerror(errno));
			return;
		}

		mesim_impl->client_addr.sin_family = AF_INET;

		mesim_impl->sock_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (mesim_impl->sock_fd < 0) {
			ast_log(LOG_WARNING,
				"Unable to create MESIM socket: %s\n",
				strerror(errno));
			return;
		}

		struct protoent *proto;
		proto = getprotobyname("tcp");
		if (!proto) {
			ast_log(LOG_NOTICE,
				"Cannot find protocol 'tcp': %s\n",
				strerror(errno));
			return;
		}

		int on = 1;
		if (setsockopt(mesim_impl->sock_fd, proto->p_proto, TCP_NODELAY,
				(char *)&on, sizeof(on) ) < 0 ) {
			ast_log(LOG_WARNING,
				"Failed to set TCP_NODELAY option on"
				" connection: %s\n",
				strerror(errno));
		}


		if (connect(mesim_impl->sock_fd,
				(struct sockaddr *)&mesim_impl->client_addr,
				sizeof(mesim_impl->client_addr)) < 0) {

#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
			{
			char tmpstr[32];
			ast_inet_ntoa(tmpstr, sizeof(tmpstr),
				mesim_impl->client_addr.sin_addr);
			vgsm_mesim_debug(mesim,
				"Unable to connect MESIM socket to"
				" %s:%d: %s\n",
				tmpstr,
				ntohs(mesim_impl->client_addr.sin_port),
				strerror(errno));
			}
#else
			vgsm_mesim_debug(mesim,
				"Unable to connect MESIM socket to"
				" %s:%d: %s\n",
				ast_inet_ntoa(
					mesim_impl->client_addr.sin_addr),
				ntohs(mesim_impl->client_addr.sin_port),
				strerror(errno));
#endif
			close(mesim_impl->sock_fd);
			mesim_impl->sock_fd = -1;

			vgsm_mesim_impl_change_state(mesim_impl,
					VGSM_MESIM_IMPL_STATE_TRYING);
			vgsm_timer_start_delta(&mesim_impl->timer, 5 * SEC);
		} else {
			vgsm_mesim_change_state(mesim,
				VGSM_MESIM_HOLDER_CHANGING, 6 * SEC);
			vgsm_mesim_set_removed(mesim);

			vgsm_mesim_impl_change_state(mesim_impl,
				VGSM_MESIM_IMPL_STATE_READY);

			if (vgsm_mesim_impl_write_reset_asserted(mesim_impl)
									< 0) {
				ast_log(LOG_WARNING,
					"Error writing '-' to socket: %s\n",
					strerror(errno));
			}
		}

	break;

	case VGSM_MESIM_IMPL_STATE_READY:
	break;
	}
}

static int vgsm_mesim_impl_send(struct vgsm_mesim_driver *driver,
	void *buf, int len)
{
	struct vgsm_mesim_impl *mesim_impl =
			container_of(driver, struct vgsm_mesim_impl, driver);

	return vgsm_mesim_impl_write_message(mesim_impl, buf, len);
}

static int vgsm_mesim_impl_receive_ready(
	struct vgsm_mesim_impl *mesim_impl,
	__u8 *buf,
	int len)
{
	struct vgsm_mesim *mesim = mesim_impl->mesim;

	vgsm_mesim_debug(mesim,
			"IMPL RX (%2d): %.*s\n", len, len, buf);

	__u8 *bufo = alloca(len / 2);
	int bufo_len = 0;
	int i;
	for(i=0; i<len; i++) {

		switch(mesim_impl->parser_state) {
		case VGSM_MESIM_IMPL_PARSER_STATE_IDLE:

			if (isxdigit(buf[i])) {
				mesim_impl->hexval =
					char_to_hexdigit(buf[i]) << 4;

				mesim_impl->parser_state =
				VGSM_MESIM_IMPL_PARSER_STATE_READING_HEX_OCTET;
			} else if (buf[i] == '+') {
				vgsm_mesim_change_state(mesim,
					VGSM_MESIM_HOLDER_CHANGING, 3 * SEC);

			} else if (buf[i] == '-') {

				vgsm_mesim_change_state(mesim,
					VGSM_MESIM_HOLDER_REMOVED, -1);
				vgsm_mesim_set_removed(mesim);

			} else if (buf[i] == '.') {
				vgsm_mesim_impl_write(mesim_impl, ".", 1);
			} else if (buf[i] == 'i') {

				char ibuf[256];
				snprintf(ibuf, sizeof(ibuf),
					"<info boardSN=\"%d\""
					" boardPCISlot=\"%d\""
					" numModules=\"%d\""
					" typeModules=\"MC55\""
					" thisModule=\"%d\""
					" thisModuleStatus=\"%s\""
					" />",
					mesim->me->card.serial,
					mesim->me->card.id,
					-1,
					mesim->me->id,
					mesim->is_reset ? "off" : "on");

				vgsm_mesim_impl_write(mesim_impl, ibuf,
							strlen(ibuf));
			}
		break;

		case VGSM_MESIM_IMPL_PARSER_STATE_READING_HEX_OCTET:

			if (!isxdigit(buf[i])) {
				ast_log(LOG_ERROR,
					"Unextpected character '%c'"
					" while waiting for hex"
					" digit\n", buf[i]);

				mesim_impl->parser_state =
				    VGSM_MESIM_IMPL_PARSER_STATE_IDLE;
			}

			mesim_impl->parser_state =
				VGSM_MESIM_IMPL_PARSER_STATE_IDLE;

			mesim_impl->hexval |= char_to_hexdigit(buf[i]);

			*(bufo + bufo_len) = mesim_impl->hexval;
			bufo_len++;
		break;
		}
	}

	if (bufo_len)
		vgsm_mesim_write(mesim, bufo, bufo_len);

	return 0;
}

static int vgsm_mesim_impl_receive(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_impl *mesim_impl =
			container_of(driver, struct vgsm_mesim_impl, driver);
	struct vgsm_mesim *mesim = mesim_impl->mesim;

	__u8 buf[128];
	int nread;

	nread = read(mesim_impl->sock_fd, buf, sizeof(buf));
	if (nread <= 0) {
		if (nread < 0) {
			ast_log(LOG_WARNING, "Error reading from socket: %s\n",
				strerror(errno));
		}

		close(mesim_impl->sock_fd);
		mesim_impl->sock_fd = -1;

		vgsm_mesim_set_removed(mesim);
		vgsm_mesim_impl_change_state(mesim_impl,
				VGSM_MESIM_IMPL_STATE_TRYING);
		vgsm_timer_start_delta(&mesim_impl->timer, 1 * SEC);

		return -errno;
	}

	switch(mesim_impl->state) {
	case VGSM_MESIM_IMPL_STATE_NULL:
	case VGSM_MESIM_IMPL_STATE_TRYING: {
		vgsm_mesim_debug(mesim,
				"SIM=>ME DUMPED (%2d): %.*s\n",
				nread, nread, buf);
	}
	break;

	case VGSM_MESIM_IMPL_STATE_READY:
		return vgsm_mesim_impl_receive_ready(mesim_impl, buf, nread);
	break;
	}

	return 0;
}

static int vgsm_mesim_impl_get_polls(
	struct vgsm_mesim_driver *driver,
	struct pollfd *polls)
{
	struct vgsm_mesim_impl *mesim_impl =
			container_of(driver, struct vgsm_mesim_impl, driver);

	if (mesim_impl->state == VGSM_MESIM_IMPL_STATE_READY) {
		polls[0].fd = mesim_impl->sock_fd;
		polls[0].events = POLLHUP | POLLERR | POLLIN;
		return 1;
	}

	return 0;
}

static void vgsm_mesim_impl_cli_show(
	struct vgsm_mesim_driver *driver, int fd)
{
	struct vgsm_mesim_impl *mesim_impl =
			container_of(driver, struct vgsm_mesim_impl, driver);

	ast_cli(fd, "  Implementa interface status: %s\n",
		vgsm_mesim_impl_state_to_text(mesim_impl->state));
#if ASTERISK_VERSION_NUM < 010400 || (ASTERISK_VERSION_NUM >= 10200 && ASTERISK_VERSION_NUM < 10400)
	{
	char tmpstr[32];
	ast_inet_ntoa(tmpstr, sizeof(tmpstr),
		mesim_impl->client_addr.sin_addr);

	ast_cli(fd, "  Implementa SIM client addr: %s:%d\n",
		tmpstr,
		ntohs(mesim_impl->client_addr.sin_port));
	}
#else
	ast_cli(fd, "  Implementa SIM client addr: %s:%d\n",
		ast_inet_ntoa(mesim_impl->client_addr.sin_addr),
		ntohs(mesim_impl->client_addr.sin_port));
#endif
}

static struct vgsm_mesim_driver vgsm_mesim_driver_impl =
{
	.release	= vgsm_mesim_impl_release,
	.deactivate	= vgsm_mesim_impl_deactivate,
	.activate	= vgsm_mesim_impl_activate,
	.set_mode	= vgsm_mesim_impl_set_mode,
	.reset_asserted	= vgsm_mesim_impl_reset_asserted,
	.reset_removed 	= vgsm_mesim_impl_reset_removed,
	.send		= vgsm_mesim_impl_send,
	.receive	= vgsm_mesim_impl_receive,
	.get_polls	= vgsm_mesim_impl_get_polls,
	.cli_show	= vgsm_mesim_impl_cli_show,
};
