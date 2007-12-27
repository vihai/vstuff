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
#include <asterisk/cli.h>

#include <linux/vgsm2.h>

#define VGSM_MESIM_DRIVER_PRIVATE

#include "chan_vgsm.h"
#include "util.h"
#include "mesim.h"
#include "mesim_local.h"

static struct vgsm_mesim_driver vgsm_mesim_driver_local;

struct vgsm_mesim_driver *vgsm_mesim_local_create(
	struct vgsm_mesim *mesim,
	struct vgsm_timerset *timerset)
{
	struct vgsm_mesim_local *mesim_local;

	mesim_local = malloc(sizeof(*mesim_local));
	if (!mesim_local)
		return NULL;

	memcpy(&mesim_local->driver, &vgsm_mesim_driver_local,
				sizeof(mesim_local->driver));

	mesim_local->mesim = mesim;

	mesim_local->fd = -1;

	return &mesim_local->driver;
}

void vgsm_mesim_local_destroy(struct vgsm_mesim_driver *driver)
{
	if (driver->release)
		driver->release(driver);
}

static void vgsm_mesim_local_release(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_local *mesim_local =
			container_of(driver, struct vgsm_mesim_local, driver);

	free(mesim_local);
}

static void vgsm_mesim_local_deactivate(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_local *mesim_local =
			container_of(driver, struct vgsm_mesim_local, driver);

	if (mesim_local->fd != -1) {
		close(mesim_local->fd);
		mesim_local->fd = -1;
	}
}

static void vgsm_mesim_local_activate(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_local *mesim_local =
			container_of(driver, struct vgsm_mesim_local, driver);
	struct vgsm_mesim *mesim = mesim_local->mesim;

	assert(mesim_local->fd == -1);

	mesim_local->fd = open(mesim_local->device_filename,
				O_RDWR | O_NOCTTY | O_NDELAY);
	if (mesim_local->fd < 0) {
		ast_log(LOG_ERROR,
			"%s: open(%s) failed: %s\n",
			mesim->name,
			mesim_local->device_filename,
			strerror(errno));
		return;
	}

//	mesim_local->type = VGSM_MESIM_LOCAL_TYPE_VGSM_LOCAL;

	if (ioctl(mesim_local->fd, VGSM_IOC_SIM_GET_ID,
					&mesim_local->sim_id) < 0) {

		if (errno != EINVAL) {
			ast_log(LOG_ERROR,
				"%s: ioctl(IOC_SIM_GET_ID)"
				" failed: %s\n",
				mesim->name,
				strerror(errno));
			return;
		}

//		mesim_local->type = VGSM_MESIM_LOCAL_TYPE_VOISMART;
	}

	struct termios newtio;
	memset(&newtio, 0, sizeof(newtio));

	newtio.c_cflag = B38400 | CS8 | CLOCAL | CREAD | PARENB | HUPCL;
	newtio.c_iflag = IGNBRK | IGNPAR;
	newtio.c_oflag = 0;
	newtio.c_lflag = 0;
	
	newtio.c_ospeed = 8736;
	newtio.c_ispeed = 8736;

	newtio.c_cc[VINTR]	= 0;
	newtio.c_cc[VQUIT]	= 0;
	newtio.c_cc[VERASE]	= 0;
	newtio.c_cc[VKILL]	= 0;
	newtio.c_cc[VEOF]	= 4;
	newtio.c_cc[VTIME]	= 0;
	newtio.c_cc[VMIN]	= 1;
	newtio.c_cc[VSWTC]	= 0;
	newtio.c_cc[VSTART]	= 0;
	newtio.c_cc[VSTOP]	= 0;
	newtio.c_cc[VSUSP]	= 0;
	newtio.c_cc[VEOL]	= 0;
	newtio.c_cc[VREPRINT]	= 0;
	newtio.c_cc[VDISCARD]	= 0;
	newtio.c_cc[VWERASE]	= 0;
	newtio.c_cc[VLNEXT]	= 0;
	newtio.c_cc[VEOL2]	= 0;
	
	if (tcflush(mesim_local->fd, TCIOFLUSH) < 0) {
		ast_log(LOG_ERROR,
			"%s: tcflush(TCIOFLUSH):  %s\n",
			mesim->name,
			strerror(errno));

		return;
	}

	if (tcsetattr(mesim_local->fd, TCSANOW, &newtio) < 0) {
		ast_log(LOG_ERROR,
			"Error setting tty's attributes: "
			"tcsetattr(%s): %s",
				mesim_local->device_filename,
				strerror(errno));
		return;
	}

	if (tcflush(mesim_local->fd, TCIOFLUSH) < 0) {
		ast_log(LOG_ERROR,
			"%s: tcflush(TCIOFLUSH):  %s\n",
			mesim->name,
			strerror(errno));

		return;
	}

	struct serial_struct ss;
	if (ioctl(mesim_local->fd, TIOCGSERIAL, &ss) < 0) {
		perror( "Error getting serial parameters");

		return;
	}

	ss.custom_divisor = ss.baud_base / 8736;
	ss.flags &= ~ASYNC_SPD_MASK;
	ss.flags |= ASYNC_SPD_CUST;

	if (ioctl(mesim_local->fd, TIOCSSERIAL, &ss) < 0) {
		perror( "Error setting serial parameters");

		return;
	}

	if (ioctl(mesim->fd, VGSM_IOC_SET_SIM_ROUTE,
					mesim_local->sim_id) < 0) {
		ast_log(LOG_ERROR,
			"%s: ioctl(IOC_SET_SIM_ROUTE, %d) failed: %s\n",
			mesim->name,
			mesim_local->sim_id,
			strerror(errno));
		return;
	}

	vgsm_mesim_set_removed(mesim);

	vgsm_mesim_change_state(mesim, VGSM_MESIM_DIRECTLY_ROUTED);
}

static void vgsm_mesim_local_set_mode(
	struct vgsm_mesim_driver *driver,
	struct vgsm_mesim_message *msg)
{
	struct vgsm_mesim_local *mesim_local =
			container_of(driver, struct vgsm_mesim_local, driver);

	struct vgsm_mesim_set_mode *sm = (void *)msg->data;

	assert(msg->len == sizeof(*sm));

	strncpy(mesim_local->device_filename,
		sm->device_filename,
		sizeof(mesim_local->device_filename));
}

static void vgsm_mesim_local_reset_asserted(struct vgsm_mesim_driver *driver)
{
//	struct vgsm_mesim_local *mesim_local =
//			container_of(driver, struct vgsm_mesim_local, driver);

}

static void vgsm_mesim_local_reset_removed(struct vgsm_mesim_driver *driver)
{
//	struct vgsm_mesim_local *mesim_local =
//			container_of(driver, struct vgsm_mesim_local, driver);

}

#if 0
static void vgsm_mesim_clnt.accept(struct vgsm_mesim *mesim)
{
	struct sockaddr_in sin;
	socklen_t sinlen;

	sinlen = sizeof(sin);
	mesim->impl.sock_fd = accept(mesim->impl.listen_fd,
				(struct sockaddr *)&sin, &sinlen);
	if (mesim->impl.sock_fd < 0) {
		ast_log(LOG_NOTICE,
			"Cannot accept incoming connection: %s\n",
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
	if (setsockopt(mesim->impl.sock_fd, proto->p_proto, TCP_NODELAY,
			(char *)&on, sizeof(on) ) < 0 ) {
		ast_log(LOG_WARNING,
			"Failed to set TCP_NODELAY option on connection: %s\n",
			strerror(errno));
	}

	vgsm_mesim_debug(mesim, "Implementa connection from %s\n",
			ast_inet_ntoa(sin.sin_addr));
}
#endif

static int vgsm_mesim_local_send(
	struct vgsm_mesim_driver *driver,
	void *buf, int len)
{
//	struct vgsm_mesim_local *mesim_local =
//			container_of(driver, struct vgsm_mesim_local, driver);

	return 0;
}

static int vgsm_mesim_local_receive(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_local *mesim_local =
			container_of(driver, struct vgsm_mesim_local, driver);
	struct vgsm_mesim *mesim = mesim_local->mesim;

	__u8 buf[128];
	int nread;

	nread = read(mesim_local->fd, buf, sizeof(buf));
	if (nread < 0) {
		ast_log(LOG_WARNING,
			"Error reading from local SIM: %s\n",
			strerror(errno));
	}

	vgsm_mesim_debug(mesim,
			"SIM=>ME DUMPED (%2d): %.*s\n",
			nread, nread, buf);

	return 0;
}

static int vgsm_mesim_local_get_polls(
	struct vgsm_mesim_driver *driver,
	struct pollfd *polls)
{
	struct vgsm_mesim_local *mesim_local =
			container_of(driver, struct vgsm_mesim_local, driver);

	polls[0].fd = mesim_local->fd;
	polls[0].events = POLLHUP | POLLERR | POLLIN;
	return 1;
}

static void vgsm_mesim_local_cli_show(
	struct vgsm_mesim_driver *driver, int fd)
{
	struct vgsm_mesim_local *mesim_local =
			container_of(driver, struct vgsm_mesim_local, driver);

	ast_cli(fd, "  Routed to local SIM holder: %d\n",
		mesim_local->sim_id);
}

static struct vgsm_mesim_driver vgsm_mesim_driver_local =
{
	.release	= vgsm_mesim_local_release,
	.deactivate	= vgsm_mesim_local_deactivate,
	.activate	= vgsm_mesim_local_activate,
	.set_mode	= vgsm_mesim_local_set_mode,
	.reset_asserted	= vgsm_mesim_local_reset_asserted,
	.reset_removed 	= vgsm_mesim_local_reset_removed,
	.send		= vgsm_mesim_local_send,
	.receive	= vgsm_mesim_local_receive,
	.get_polls	= vgsm_mesim_local_get_polls,
	.cli_show	= vgsm_mesim_local_cli_show,
};
