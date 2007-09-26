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

#include <linux/vgsm2.h>

#include "chan_vgsm.h"
#include "util.h"
#include "timer.h"
#include "mesim.h"

static void vgsm_mesim_timers_updated(struct vgsm_timerset *set);
static void vgsm_mesim_timer(void *data);
static void vgsm_mesim_impl_timer(void *data);

int vgsm_mesim_create(struct vgsm_mesim *mesim, struct vgsm_module *module)
{
//	int err;

	mesim->fd = -1;
	mesim->state = VGSM_MESIM_CLOSED;
	mesim->module = module;

	ast_mutex_init(&mesim->state_lock);
	ast_cond_init(&mesim->state_cond, NULL);

	vgsm_timerset_init(&mesim->timerset, vgsm_mesim_timers_updated);
	vgsm_timer_init(&mesim->timer, &mesim->timerset, "mesim",
			vgsm_mesim_timer, mesim);

	mesim->local_fd = -1;

	mesim->clnt_listen_fd = -1;
	mesim->clnt_sock_fd = -1;

	mesim->impl_sock_fd = -1;
	mesim->impl_state = VGSM_MESIM_IMPL_STATE_NULL;
	vgsm_timer_init(&mesim->impl_timer, &mesim->timerset, "mesim_impl",
			vgsm_mesim_impl_timer, mesim);

	return 0;
}

void vgsm_mesim_destroy(struct vgsm_mesim *mesim)
{
}

struct vgsm_mesim *vgsm_mesim_get(
	struct vgsm_mesim *mesim)
{
	vgsm_module_get(mesim->module);

	return mesim;
}

void _vgsm_mesim_put(struct vgsm_mesim *mesim)
{
	vgsm_module_put(mesim->module);
}

const char *vgsm_mesim_message_type_to_text(
	enum vgsm_mesim_message_type mt)
{
	switch(mt) {
	case VGSM_MESIM_MSG_CLOSE:
		return "CLOSE";
	case VGSM_MESIM_MSG_REFRESH:
		return "REFRESH";
	case VGSM_MESIM_MSG_RESET_ASSERTED:
		return "RESET ASSERTED";
	case VGSM_MESIM_MSG_RESET_REMOVED:
		return "RESET REMOVED";
	case VGSM_MESIM_MSG_SET_MODE:
		return "SET_MODE";
	case VGSM_MESIM_MSG_ME_POWERING_ON:
		return "ME_POWERING_ON";
	case VGSM_MESIM_MSG_ME_POWERED_ON:
		return "ME_POWERED_ON";
	}

	return "*UNKNOWN*";
}

const char *vgsm_mesim_state_to_text(
	enum vgsm_mesim_state state)
{
	switch(state) {
	case VGSM_MESIM_CLOSED:
		return "CLOSED";
	case VGSM_MESIM_CLOSING:
		return "CLOSING";
	case VGSM_MESIM_ME_POWERING_ON:
		return "ME_POWERING_ON";
	case VGSM_MESIM_SETTING_MODE:
		return "SETTING_MODE";
	case VGSM_MESIM_UNCONFIGURED:
		return "UNCONFIGURED";
	case VGSM_MESIM_DIRECTLY_ROUTED:
		return "DIRECTLY_ROUTED";
	case VGSM_MESIM_HOLDER_REMOVED:
		return "HOLDER_REMOVED";
	case VGSM_MESIM_SIM_MISSING:
		return "SIM_MISSING";
	case VGSM_MESIM_RESET:
		return "RESET";
	case VGSM_MESIM_READY:
		return "READY";
	}

	return "*UNKNOWN*";
}

const char *vgsm_mesim_impl_state_to_text(
	enum vgsm_mesim_impl_state state)
{
	switch(state) {
	case VGSM_MESIM_IMPL_STATE_NULL:
		return "NULL";
	case VGSM_MESIM_IMPL_STATE_TRYING:
		return "TRYING";
	case VGSM_MESIM_IMPL_STATE_CONNECTED:
		return "CONNECTED";
	}

	return "*UNKNOWN*";
}

void vgsm_mesim_send_message(
	struct vgsm_mesim *mesim,
	enum vgsm_mesim_message_type mt,
	void *data,
	int len)
{
	struct vgsm_mesim_message *msg;

	msg = alloca(sizeof(*msg) + len);
	msg->type = mt;
	msg->len = len;

	if (data && len)
		memcpy(&msg->data, data, len);

	write(mesim->cmd_pipe_write, msg, sizeof(*msg) + len);
}

void vgsm_mesim_get_ready_for_poweron(struct vgsm_mesim *mesim)
{
	vgsm_mesim_send_message(mesim, VGSM_MESIM_MSG_ME_POWERING_ON, NULL, 0);

	ast_mutex_lock(&mesim->state_lock);
	while(mesim->state != VGSM_MESIM_ME_POWERING_ON) {
		ast_cond_wait(&mesim->state_cond, &mesim->state_lock);
	}
	ast_mutex_unlock(&mesim->state_lock);
}

void vgsm_mesim_close(struct vgsm_mesim *mesim)
{
	vgsm_mesim_send_message(mesim, VGSM_MESIM_MSG_CLOSE, NULL, 0);

	ast_mutex_lock(&mesim->state_lock);
	while(mesim->state != VGSM_MESIM_CLOSED) {
		ast_cond_wait(&mesim->state_cond, &mesim->state_lock);
	}
	ast_mutex_unlock(&mesim->state_lock);
}

static void *vgsm_mesim_thread_main(void *data);
static void *vgsm_mesim_modem_thread_main(void *data);

int vgsm_mesim_open(struct vgsm_mesim *mesim, int fd)
{
	int err;

//	assert(mesim->state == VGSM_MESIM_CLOSED);

	mesim->fd = fd;
	mesim->enabled = TRUE;

	int filedes[2];
	if (pipe(filedes) < 0) {
		ast_log(LOG_ERROR,
			"Cannot create cmd_pipe pipe: %s\n",
			strerror(errno));
		err = -errno;
		goto err_pipe;
	}

	if (fcntl(filedes[0], F_SETFL, O_NONBLOCK) < 0) {
		ast_log(LOG_ERROR,
			"Cannot set pipe to non-blocking: %s\n",
			strerror(errno));
		err = -errno;
		goto err_fcntl_0;
	}

	if (fcntl(filedes[1], F_SETFL, O_NONBLOCK) < 0) {
		ast_log(LOG_ERROR,
			"Cannot set pipe to non-blocking: %s\n",
			strerror(errno));
		err = -errno;
		goto err_fcntl_1;
	}

	mesim->cmd_pipe_read = filedes[0];
	mesim->cmd_pipe_write = filedes[1];

	pthread_attr_t attr;
	pthread_attr_init(&attr);
//	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	err = ast_pthread_create(&mesim->comm_thread, &attr,
					vgsm_mesim_thread_main,
					mesim);
	if (err < 0)
		goto err_pthread_create_comm;

	mesim->modem_thread_has_to_exit = FALSE;
	err = ast_pthread_create(&mesim->modem_thread, &attr,
					vgsm_mesim_modem_thread_main,
					mesim);
	if (err < 0)
		goto err_pthread_create_modem;

	pthread_attr_destroy(&attr);

	return 0;

	pthread_kill(mesim->modem_thread, SIGURG);
err_pthread_create_modem:
	pthread_kill(mesim->comm_thread, SIGURG);
err_pthread_create_comm:
err_fcntl_1:
err_fcntl_0:
	close(mesim->cmd_pipe_read);
	close(mesim->cmd_pipe_write);
err_pipe:

	return err;
}

static void vgsm_mesim_change_state(
	struct vgsm_mesim *mesim,
	enum vgsm_mesim_state newstate)
{
	vgsm_mesim_debug(mesim,
		"State change from %s to %s\n",
		vgsm_mesim_state_to_text(mesim->state),
		vgsm_mesim_state_to_text(newstate));

	ast_mutex_lock(&mesim->state_lock);
	mesim->state = newstate;
	ast_mutex_unlock(&mesim->state_lock);

	ast_cond_broadcast(&mesim->state_cond);
}

static void vgsm_mesim_impl_change_state(
	struct vgsm_mesim *mesim,
	enum vgsm_mesim_impl_state newstate)
{
	vgsm_mesim_debug(mesim,
		"Implementa state changed from %s to %s\n",
		vgsm_mesim_impl_state_to_text(mesim->impl_state),
		vgsm_mesim_impl_state_to_text(newstate));

	ast_mutex_lock(&mesim->state_lock);
	mesim->impl_state = newstate;
	ast_mutex_unlock(&mesim->state_lock);

	ast_cond_broadcast(&mesim->state_cond);
}

static void vgsm_mesim_dump(struct vgsm_mesim *mesim)
{
	__u8 dump[32];
	int dumped;

	do {
		dumped = read(mesim->fd, dump, sizeof(dump));

		vgsm_mesim_debug(mesim,
				"Dumped %d bytes\n", dumped);
	} while(dumped == sizeof(dump));
}

static int vgsm_mesim_receive(struct vgsm_mesim *mesim)
{
	__u8 buf[32];
	int nread = read(mesim->fd, buf, sizeof(buf));

	char hextext[sizeof(buf)*2 + 1];

	int i;
	for(i=0; i<nread; i++)
		sprintf(&hextext[i*2], "%02x", buf[i]);

	vgsm_mesim_debug(mesim,
			"ME=>SIM (%2d): %s\n", nread, hextext);

	return 0;
}

int vgsm_mesim_write(
	struct vgsm_mesim *mesim,
	void *data, int size)
{
	char *hextext = alloca(size * 2 + 1);

	int i;
	for(i=0; i<size; i++)
		sprintf(&hextext[i*2], "%02x", ((__u8 *)data)[i]);

	return write(mesim->fd, data, size);
}

static void vgsm_mesim_set_inserted(struct vgsm_mesim *mesim)
{
	__u8 status = TIOCM_RTS;

	if (ioctl(mesim->fd, TIOCMSET, &status) < 0) {
		ast_log(LOG_WARNING, "ioctl(TIOCMSET): %s\n", strerror(errno));
		return;
	}
}

static void vgsm_mesim_set_removed(struct vgsm_mesim *mesim)
{
	__u8 status = 0;

	if (ioctl(mesim->fd, TIOCMSET, &status) < 0) {
		ast_log(LOG_WARNING, "ioctl(TIOCMSET): %s\n", strerror(errno));
		return;
	}
}

static void vgsm_mesim_deactivate(struct vgsm_mesim *mesim)
{

	if (mesim->proto == VGSM_MESIM_PROTO_LOCAL) {
		if (mesim->local_fd != -1) {
			close(mesim->local_fd);
			mesim->local_fd = -1;
		}
	} else if (mesim->proto == VGSM_MESIM_PROTO_CLIENT) {
	} else if (mesim->proto == VGSM_MESIM_PROTO_IMPLEMENTA) {
		
	}

/*
	if (ioctl(mesim->fd, VGSM_IOC_SIM_ROUTE, VGSM_SIM_ROUTE_EXTERNAL) < 0) {
		ast_log(LOG_ERROR,
			"%s: ioctl(IOC_SIM_ROUTE, EXTERN) failed: %s\n",
			mesim->name,
			strerror(errno));
	}
*/

	vgsm_mesim_set_removed(mesim);
}

static void vgsm_mesim_activate(struct vgsm_mesim *mesim)
{
	if (mesim->proto == VGSM_MESIM_PROTO_LOCAL) {

		assert(mesim->local_fd == -1);

		mesim->local_fd = open(mesim->local_device_filename, O_RDONLY);
		if (mesim->local_fd < 0) {
			ast_log(LOG_ERROR,
				"%s: open(%s) failed: %s\n",
				mesim->name,
				mesim->local_device_filename,
				strerror(errno));
			return;
		}

		struct termios newtio;
		bzero(&newtio, sizeof(newtio));

		newtio.c_cflag = B9600 | CS8 | CLOCAL | CREAD | PARENB | HUPCL;
		newtio.c_iflag = IGNBRK | IGNPAR;
		newtio.c_oflag = 0;
		newtio.c_lflag = 0;
		
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
		
		tcflush(mesim->local_fd, TCIOFLUSH);

		if (tcsetattr(mesim->local_fd, TCSANOW, &newtio) < 0) {
			ast_log(LOG_ERROR,
				"Error setting tty's attributes: "
				"tcsetattr(%s): %s",
					mesim->local_device_filename,
					strerror(errno));
			return;
		}

		if (ioctl(mesim->local_fd, VGSM_IOC_SIM_GET_ID,
						&mesim->local_sim_id) < 0) {
			ast_log(LOG_ERROR,
				"%s: ioctl(IOC_SIM_GET_ID) failed: %s\n",
				mesim->name,
				strerror(errno));
			return;
		}

		if (ioctl(mesim->fd, VGSM_IOC_SIM_ROUTE,
						mesim->local_sim_id) < 0) {
			ast_log(LOG_ERROR,
				"%s: ioctl(IOC_SIM_ROUTE, %d) failed: %s\n",
				mesim->name,
				mesim->local_sim_id,
				strerror(errno));
			return;
		}

		vgsm_mesim_change_state(mesim, VGSM_MESIM_DIRECTLY_ROUTED);

	} else if (mesim->proto == VGSM_MESIM_PROTO_CLIENT) {

		assert(mesim->clnt_listen_fd == -1);
		assert(mesim->clnt_sock_fd == -1);

		if (ioctl(mesim->fd, VGSM_IOC_SIM_ROUTE,
					VGSM_SIM_ROUTE_EXTERNAL) < 0) {
			ast_log(LOG_ERROR,
				"%s: ioctl(IOC_SIM_ROUTE, EXTERN) failed: %s\n",
				mesim->name,
				strerror(errno));
			return;
		}

		mesim->clnt_bind_addr.sin_family = AF_INET;

		mesim->clnt_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (mesim->clnt_listen_fd < 0) {
			ast_log(LOG_WARNING,
				"Unable to create MESIM socket: %s\n",
				strerror(errno));
			return;
		}

		int on = 1;
		if(setsockopt(mesim->clnt_listen_fd, SOL_SOCKET, SO_REUSEADDR,
						&on, sizeof(on)) == -1) {
			ast_log(LOG_ERROR,
				"Set Socket Options failed: %s\n",
				strerror(errno));
			return;
		}

		if (bind(mesim->clnt_listen_fd,
			(struct sockaddr *)&mesim->clnt_bind_addr,
			sizeof(mesim->clnt_bind_addr)) < 0) {
				ast_log(LOG_WARNING,
					"Unable to bind MESIM socket to"
					" %s:%d: %s\n",
					ast_inet_ntoa(mesim->clnt_bind_addr.
							sin_addr),
					ntohs(mesim->clnt_bind_addr.sin_port),
					strerror(errno));
				return;
			}

		if (listen(mesim->clnt_listen_fd, 10)) {
			ast_log(LOG_WARNING,
				"Unable to start listening on"
				" %s:%d: %s\n",
					ast_inet_ntoa(mesim->clnt_bind_addr.
							sin_addr),
					ntohs(mesim->clnt_bind_addr.sin_port),
					strerror(errno));
			return;
		}

		vgsm_mesim_change_state(mesim, VGSM_MESIM_HOLDER_REMOVED);

	} else if (mesim->proto == VGSM_MESIM_PROTO_IMPLEMENTA) {

		assert(mesim->impl_sock_fd == -1);

		if (ioctl(mesim->fd, VGSM_IOC_SIM_ROUTE,
					VGSM_SIM_ROUTE_EXTERNAL) < 0) {
			ast_log(LOG_ERROR,
				"%s: ioctl(IOC_SIM_ROUTE, EXTERN) failed: %s\n",
				mesim->name,
				strerror(errno));
			return;
		}

		mesim->impl_simclient_addr.sin_family = AF_INET;

		mesim->impl_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (mesim->impl_sock_fd < 0) {
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
		if (setsockopt(mesim->impl_sock_fd, proto->p_proto, TCP_NODELAY,
				(char *)&on, sizeof(on) ) < 0 ) {
			ast_log(LOG_WARNING,
				"Failed to set TCP_NODELAY option on"
				" connection: %s\n",
				strerror(errno));
		}

		vgsm_mesim_change_state(mesim, VGSM_MESIM_HOLDER_REMOVED);

		vgsm_mesim_impl_change_state(mesim,
				VGSM_MESIM_IMPL_STATE_TRYING);
		vgsm_timer_start_delta(&mesim->impl_timer, 1 * SEC);
	}

}

static void vgsm_mesim_set_mode(
	struct vgsm_mesim *mesim,
	struct vgsm_mesim_message *msg)
{
	struct vgsm_mesim_set_mode *sm = (void *)msg->data;

	assert(msg->len == sizeof(*sm));

	mesim->proto = sm->proto;

	if (mesim->proto == VGSM_MESIM_PROTO_LOCAL) {
		strncpy(mesim->local_device_filename,
			sm->local.device_filename,
			sizeof(mesim->local_device_filename));
	} else if (mesim->proto == VGSM_MESIM_PROTO_IMPLEMENTA) {
		memcpy(&mesim->impl_simclient_addr,
			&sm->impl.simclient_addr,
			sizeof(mesim->impl_simclient_addr));
	} else
		assert(0);
}

static void vgsm_mesim_direct_routing(struct vgsm_mesim *mesim, int sim_id)
{
	vgsm_mesim_debug(mesim, "Direcly routing to SIM holder %d\n",
			sim_id);

	if (ioctl(mesim->fd, VGSM_IOC_SIM_ROUTE,
		sim_id) < 0) {
		ast_log(LOG_ERROR,
			"%s: ioctl(IOC_SIM_ROUTE, %d) failed: %s\n",
			mesim->name,
			sim_id,
			strerror(errno));
	}
}

static BOOL vgsm_mesim_receive_message(
	struct vgsm_mesim *mesim,
	struct pollfd polls[])
{
	struct vgsm_mesim_message msgh;

	read(mesim->cmd_pipe_read, &msgh, sizeof(msgh));

	struct vgsm_mesim_message *msg;

	if (msgh.len) {
		msg = alloca(sizeof(*msg) + msgh.len);
		if (!msg) {
			assert(0);
			// FIXME
		}

		memcpy(msg, &msgh, sizeof(*msg));

		read(mesim->cmd_pipe_read, &msg->data, msg->len);
	} else
		msg = &msgh;

	vgsm_mesim_debug(mesim, "Received message %s (len=%d)\n",
		vgsm_mesim_message_type_to_text(msg->type), msg->len);

	if (msg->type == VGSM_MESIM_MSG_CLOSE) {
		vgsm_mesim_set_removed(mesim);
		vgsm_mesim_change_state(mesim, VGSM_MESIM_CLOSING);
		return TRUE;
	} else if (msg->type == VGSM_MESIM_MSG_REFRESH)
		return FALSE;

	switch(mesim->state) {
	case VGSM_MESIM_CLOSED:
	case VGSM_MESIM_CLOSING:
		assert(0);
	break;

	case VGSM_MESIM_UNCONFIGURED:
		switch(msg->type) {
		case VGSM_MESIM_MSG_REFRESH:
		case VGSM_MESIM_MSG_CLOSE:
			/* Handled regardless of state */
		break;

		case VGSM_MESIM_MSG_SET_MODE:
			vgsm_mesim_deactivate(mesim);
			vgsm_mesim_set_mode(mesim, msg);
			vgsm_mesim_activate(mesim);
		break;

		case VGSM_MESIM_MSG_ME_POWERING_ON:
			mesim->prev_state = mesim->state;
			vgsm_mesim_set_inserted(mesim);
			vgsm_mesim_change_state(mesim,
				VGSM_MESIM_ME_POWERING_ON);
		break;

		case VGSM_MESIM_MSG_ME_POWERED_ON:
			assert(0);
		break;

		case VGSM_MESIM_MSG_RESET_ASSERTED:
		case VGSM_MESIM_MSG_RESET_REMOVED:
		break;

		}
	break;

	case VGSM_MESIM_ME_POWERING_ON:
		switch(msg->type) {
		case VGSM_MESIM_MSG_REFRESH:
		case VGSM_MESIM_MSG_CLOSE:
			/* Handled regardless of state */
		break;

		case VGSM_MESIM_MSG_SET_MODE:
			assert(0);
		break;

		case VGSM_MESIM_MSG_ME_POWERING_ON:
		break;

		case VGSM_MESIM_MSG_ME_POWERED_ON:
			vgsm_mesim_set_removed(mesim);
			vgsm_mesim_change_state(mesim, mesim->prev_state);
		break;

		case VGSM_MESIM_MSG_RESET_ASSERTED:
		case VGSM_MESIM_MSG_RESET_REMOVED:
		break;
		}
	break;

	case VGSM_MESIM_SETTING_MODE:
		switch(msg->type) {
		case VGSM_MESIM_MSG_REFRESH:
		case VGSM_MESIM_MSG_CLOSE:
			/* Handled regardless of state */
		break;


		case VGSM_MESIM_MSG_SET_MODE:
			assert(0);
		break;

		case VGSM_MESIM_MSG_ME_POWERING_ON:
			assert(0);
		break;

		case VGSM_MESIM_MSG_ME_POWERED_ON:
			assert(0);
		break;

		case VGSM_MESIM_MSG_RESET_ASSERTED:
		case VGSM_MESIM_MSG_RESET_REMOVED:
		break;
		}
	break;

	case VGSM_MESIM_DIRECTLY_ROUTED:
		switch(msg->type) {
		case VGSM_MESIM_MSG_REFRESH:
		case VGSM_MESIM_MSG_CLOSE:
			/* Handled regardless of state */
		break;


		case VGSM_MESIM_MSG_SET_MODE:
			vgsm_mesim_deactivate(mesim);
			vgsm_mesim_set_mode(mesim, msg);
			vgsm_mesim_change_state(mesim, VGSM_MESIM_SETTING_MODE);
			vgsm_timer_start_delta(&mesim->timer, 3 * SEC);
		break;

		case VGSM_MESIM_MSG_ME_POWERING_ON:
			mesim->prev_state = mesim->state;
			vgsm_mesim_set_inserted(mesim);
			vgsm_mesim_change_state(mesim,
				VGSM_MESIM_ME_POWERING_ON);
		break;

		case VGSM_MESIM_MSG_ME_POWERED_ON:
		case VGSM_MESIM_MSG_RESET_ASSERTED:
		case VGSM_MESIM_MSG_RESET_REMOVED:
		break;
		}
	break;

	case VGSM_MESIM_HOLDER_REMOVED:
		switch(msg->type) {
		case VGSM_MESIM_MSG_REFRESH:
		case VGSM_MESIM_MSG_CLOSE:
			/* Handled regardless of state */
		break;


		case VGSM_MESIM_MSG_SET_MODE:
			vgsm_mesim_deactivate(mesim);
			vgsm_mesim_set_mode(mesim, msg);
			vgsm_mesim_change_state(mesim, VGSM_MESIM_SETTING_MODE);
			vgsm_timer_start_delta(&mesim->timer, 3 * SEC);
		break;

		case VGSM_MESIM_MSG_ME_POWERING_ON:
			mesim->prev_state = mesim->state;
			vgsm_mesim_set_inserted(mesim);
			vgsm_mesim_change_state(mesim,
				VGSM_MESIM_ME_POWERING_ON);
		break;

		case VGSM_MESIM_MSG_ME_POWERED_ON:
			assert(0);
		break;


		case VGSM_MESIM_MSG_RESET_ASSERTED:
		break;

		case VGSM_MESIM_MSG_RESET_REMOVED:
		break;
		}
	break;

	case VGSM_MESIM_SIM_MISSING:
		switch(msg->type) {
		case VGSM_MESIM_MSG_REFRESH:
		case VGSM_MESIM_MSG_CLOSE:
			/* Handled regardless of state */
		break;


		case VGSM_MESIM_MSG_SET_MODE:
			vgsm_mesim_deactivate(mesim);
			vgsm_mesim_set_mode(mesim, msg);
			vgsm_mesim_change_state(mesim, VGSM_MESIM_SETTING_MODE);
			vgsm_timer_start_delta(&mesim->timer, 3 * SEC);
		break;

		case VGSM_MESIM_MSG_ME_POWERING_ON:
			mesim->prev_state = mesim->state;
			vgsm_mesim_set_inserted(mesim);
			vgsm_mesim_change_state(mesim,
				VGSM_MESIM_ME_POWERING_ON);
		break;

		case VGSM_MESIM_MSG_ME_POWERED_ON:
			assert(0);
		break;


		case VGSM_MESIM_MSG_RESET_ASSERTED:
		break;

		case VGSM_MESIM_MSG_RESET_REMOVED:
		break;
		}
	break;

	case VGSM_MESIM_RESET:
		switch(msg->type) {
		case VGSM_MESIM_MSG_REFRESH:
		case VGSM_MESIM_MSG_CLOSE:
			/* Handled regardless of state */
		break;


		case VGSM_MESIM_MSG_SET_MODE:
			vgsm_mesim_deactivate(mesim);
			vgsm_mesim_set_mode(mesim, msg);
			vgsm_mesim_change_state(mesim, VGSM_MESIM_SETTING_MODE);
			vgsm_timer_start_delta(&mesim->timer, 3 * SEC);
		break;

		case VGSM_MESIM_MSG_ME_POWERING_ON:
			mesim->prev_state = mesim->state;
			vgsm_mesim_set_inserted(mesim);
			vgsm_mesim_change_state(mesim,
				VGSM_MESIM_ME_POWERING_ON);
		break;

		case VGSM_MESIM_MSG_ME_POWERED_ON:
			assert(0);
		break;

		case VGSM_MESIM_MSG_RESET_ASSERTED:
			assert(0);
		break;

		case VGSM_MESIM_MSG_RESET_REMOVED:	
		break;
		}
	break;

	case VGSM_MESIM_READY:
		switch(msg->type) {
		case VGSM_MESIM_MSG_REFRESH:
		case VGSM_MESIM_MSG_CLOSE:
			/* Handled regardless of state */
		break;

		case VGSM_MESIM_MSG_SET_MODE:
			vgsm_mesim_deactivate(mesim);
			vgsm_mesim_set_mode(mesim, msg);
			vgsm_mesim_change_state(mesim, VGSM_MESIM_SETTING_MODE);
			vgsm_timer_start_delta(&mesim->timer, 3 * SEC);
		break;

		case VGSM_MESIM_MSG_ME_POWERING_ON:
			mesim->prev_state = mesim->state;
			vgsm_mesim_set_inserted(mesim);
			vgsm_mesim_change_state(mesim,
				VGSM_MESIM_ME_POWERING_ON);
		break;

		case VGSM_MESIM_MSG_ME_POWERED_ON:
			assert(0);
		break;

		case VGSM_MESIM_MSG_RESET_ASSERTED:
/*			vgsm_mesim_change_state(mesim,
				VGSM_MESIM_RESET);*/
		break;

		case VGSM_MESIM_MSG_RESET_REMOVED:
		break;

		}
	break;
	}

	return FALSE;
}

static void vgsm_mesim_timer(void *data)
{
	struct vgsm_mesim *mesim = data;

	vgsm_mesim_debug(mesim, "Timer fired in state %s\n",
			vgsm_mesim_state_to_text(mesim->state));

	switch(mesim->state) {
	case VGSM_MESIM_CLOSED:
	case VGSM_MESIM_CLOSING:
	case VGSM_MESIM_ME_POWERING_ON:
	case VGSM_MESIM_UNCONFIGURED:
	case VGSM_MESIM_DIRECTLY_ROUTED:
	case VGSM_MESIM_HOLDER_REMOVED:
	case VGSM_MESIM_SIM_MISSING:
	case VGSM_MESIM_RESET:
	case VGSM_MESIM_READY:
		assert(0);
	break;

	case VGSM_MESIM_SETTING_MODE:
		vgsm_mesim_activate(mesim);
	break;
	}
}

static void vgsm_mesim_impl_timer(void *data)
{
	struct vgsm_mesim *mesim = data;

	vgsm_mesim_debug(mesim, "Implementa timer fired in state %s\n",
			vgsm_mesim_impl_state_to_text(mesim->impl_state));

	switch(mesim->impl_state) {
	case VGSM_MESIM_IMPL_STATE_NULL:
		assert(0);
	break;

	case VGSM_MESIM_IMPL_STATE_TRYING:
		if (connect(mesim->impl_sock_fd,
				(struct sockaddr *)&mesim->impl_simclient_addr,
				sizeof(mesim->impl_simclient_addr)) < 0) {

			vgsm_mesim_debug(mesim,
				"Unable to connect MESIM socket to"
				" %s:%d: %s\n",
				ast_inet_ntoa(
					mesim->impl_simclient_addr.
					sin_addr),
				ntohs(mesim->impl_simclient_addr.
					sin_port),
				strerror(errno));

			vgsm_mesim_impl_change_state(mesim,
					VGSM_MESIM_IMPL_STATE_TRYING);
			vgsm_timer_start_delta(&mesim->impl_timer, 5 * SEC);
		} else {
			vgsm_mesim_impl_change_state(mesim,
					VGSM_MESIM_IMPL_STATE_CONNECTED);
		}

	break;

	case VGSM_MESIM_IMPL_STATE_CONNECTED:
	break;
	}
}

#if 0
static void vgsm_mesim_clnt_accept(struct vgsm_mesim *mesim)
{
	struct sockaddr_in sin;
	socklen_t sinlen;

	sinlen = sizeof(sin);
	mesim->impl_sock_fd = accept(mesim->impl_listen_fd,
				(struct sockaddr *)&sin, &sinlen);
	if (mesim->impl_sock_fd < 0) {
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
	if (setsockopt(mesim->impl_sock_fd, proto->p_proto, TCP_NODELAY,
			(char *)&on, sizeof(on) ) < 0 ) {
		ast_log(LOG_WARNING,
			"Failed to set TCP_NODELAY option on connection: %s\n",
			strerror(errno));
	}

	vgsm_mesim_debug(mesim, "Implementa connection from %s\n",
			ast_inet_ntoa(sin.sin_addr));
}
#endif

static void vgsm_mesim_impl_receive(struct vgsm_mesim *mesim)
{
	__u8 buf[128];
	int nread;

	nread = read(mesim->impl_sock_fd, buf, sizeof(buf));
	if (nread == 0) {
		close(mesim->impl_sock_fd);
		mesim->impl_sock_fd = -1;

		return;
	} else if (nread < 0) {
		ast_log(LOG_WARNING, "Error reading from socket: %s\n",
			strerror(errno));
	}

	char hextext[sizeof(buf)*2 + 1];

	int i;
	for(i=0; i<nread; i++)
		sprintf(&hextext[i*2], "%02x", buf[i]);

	vgsm_mesim_debug(mesim,
			"SIM=>ME (%2d): %s\n", nread, hextext);
}

static void vgsm_mesim_timers_updated(struct vgsm_timerset *set)
{
	struct vgsm_mesim *mesim = container_of(set, struct vgsm_mesim,
								timerset);

	vgsm_mesim_send_message(mesim, VGSM_MESIM_MSG_REFRESH, NULL, 0);
}

static void *vgsm_mesim_thread_main(void *data)
{
	struct vgsm_mesim *mesim = data;

	struct pollfd polls[4];
	int npolls = 2;

	polls[0].fd = mesim->cmd_pipe_read;
	polls[0].events = POLLHUP | POLLERR | POLLIN;

	polls[1].fd = mesim->fd;
	polls[1].events = POLLHUP | POLLERR | POLLIN;

	vgsm_mesim_change_state(mesim, VGSM_MESIM_UNCONFIGURED);

	for(;;) {

		vgsm_timerset_run(&mesim->timerset);

		longtime_t timeout = vgsm_timerset_next(&mesim->timerset);

		int timeout_ms;
		if (timeout == -1)
			timeout_ms = -1;
		else
			timeout_ms = max(timeout / 1000, 1LL);

		vgsm_mesim_debug(mesim, "set poll timeout = %d ms\n",
			timeout_ms);

		if (mesim->proto == VGSM_MESIM_PROTO_LOCAL) {
		} else if (mesim->proto == VGSM_MESIM_PROTO_CLIENT) {
/*			polls[2].fd = mesim->impl_listen_fd;
			polls[2].events = POLLHUP | POLLERR | POLLIN;

			if (mesim->impl_sock_fd != -1) {
				polls[3].fd = mesim->impl_sock_fd;
				polls[3].events = POLLHUP | POLLERR | POLLIN;
				npolls = 4;
			} else
				npolls = 3;*/
		} else if (mesim->proto == VGSM_MESIM_PROTO_IMPLEMENTA) {

			if (mesim->impl_state ==
					VGSM_MESIM_IMPL_STATE_CONNECTED) {
				polls[2].fd = mesim->impl_sock_fd;
				polls[2].events = POLLHUP | POLLERR | POLLIN;
				npolls = 3;
			}
		}

		int res = poll(polls, npolls, timeout_ms);
		if (res < 0) {
			ast_log(LOG_WARNING, "vgsm: Error polling: %s\n",
				strerror(errno));
			continue;
		}

		if (polls[0].revents & (POLLIN | POLLERR | POLLHUP)) {
			if (vgsm_mesim_receive_message(mesim, polls))
				break;
		}

		if (polls[1].revents & (POLLIN | POLLERR | POLLHUP))
			vgsm_mesim_receive(mesim);

		if (npolls > 2 && (polls[2].revents & (POLLIN | POLLERR |
								POLLHUP))) {
			if (mesim->proto == VGSM_MESIM_PROTO_LOCAL)
				assert(0);
			else if (mesim->proto == VGSM_MESIM_PROTO_CLIENT)
				;//vgsm_mesim_clnt_accept(mesim);
			else if (mesim->proto == VGSM_MESIM_PROTO_IMPLEMENTA)
				vgsm_mesim_impl_receive(mesim);
		}

		if (npolls > 3 && (polls[3].revents & (POLLIN | POLLERR |
								POLLHUP))) {
			if (mesim->proto == VGSM_MESIM_PROTO_LOCAL)
				assert(0);
			else if (mesim->proto == VGSM_MESIM_PROTO_IMPLEMENTA)
				assert(0);
		}

	}

	mesim->modem_thread_has_to_exit = TRUE;

	int err;
	err = -pthread_kill(mesim->modem_thread, SIGTERM);
	if (err) {
		ast_log(LOG_WARNING, "Cannot kill modem thread: %s\n",
			strerror(-err));
	}

	err = -pthread_join(mesim->modem_thread, NULL);
	if (err) {
		ast_log(LOG_WARNING, "Cannot join modem thread: %s\n",
			strerror(-err));
	}

	close(mesim->cmd_pipe_read);
	close(mesim->cmd_pipe_write);

	vgsm_mesim_change_state(mesim, VGSM_MESIM_CLOSED);

	return NULL;
}

void vgsm_mesim_sigterm_handler(int sig)
{
}

static void *vgsm_mesim_modem_thread_main(void *data)
{
	struct vgsm_mesim *mesim = data;

	struct sigaction sigterm = {
		.sa_handler = vgsm_mesim_sigterm_handler,
		.sa_flags = 0
	};

	if (sigaction(SIGTERM, &sigterm, NULL) < 0) {
		ast_log(LOG_ERROR, "sigaction()\n");
		return NULL;
	}

	sigset_t sigs;
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGTERM);
	if (pthread_sigmask(SIG_UNBLOCK, &sigs, NULL)) {
		ast_log(LOG_ERROR, "pthread_sigmask()\n");
		return NULL;
	}

	if (ioctl(mesim->fd, TIOCGICOUNT, &mesim->icount) < 0) {
		ast_log(LOG_ERROR, "ioctl(TIOCGICOUNT)\n");
		return NULL;
	}

	int status;
	if (ioctl(mesim->fd, TIOCMGET, &status) < 0) {
		ast_log(LOG_ERROR, "ioctl(TIOCMGET)\n");
		return NULL;
	}

	mesim->vcc = !!(status & TIOCM_DSR);
	mesim->rst = !(status & TIOCM_CTS);

	for(;;) {
		if (ioctl(mesim->fd, TIOCMIWAIT,
					TIOCM_CTS | TIOCM_DSR) < 0) {

			if (errno != EINTR) {
				ast_log(LOG_ERROR, "ioctl(TIOCMIWAIT)\n");
				break;
			}
		}

		if (mesim->modem_thread_has_to_exit)
			break;

		struct serial_icounter_struct icount;
		if (ioctl(mesim->fd, TIOCGICOUNT, &icount) < 0) {
			ast_log(LOG_ERROR, "ioctl(TIOCGICOUNT)\n");
			break;
		}

		if (ioctl(mesim->fd, TIOCMGET, &status) < 0) {
			ast_log(LOG_ERROR, "ioctl(TIOCMGET)\n");
			break;
		}

		BOOL vcc = !!(status & TIOCM_DSR);
		BOOL rst = !(status & TIOCM_CTS);

		if (icount.dsr != mesim->icount.dsr)
			vgsm_mesim_debug(mesim,
				"VCC transition: current=%s\n",
				vcc ? "on" : "off");

		if (icount.cts != mesim->icount.cts)
			vgsm_mesim_debug(mesim,
				"RST transition: current=%s\n",
				rst ? "on" : "off");

		if ((icount.dsr != mesim->icount.dsr ||
		     icount.cts != mesim->icount.cts)) {

			if (mesim->vcc && !mesim->rst)
				vgsm_mesim_send_message(
					mesim,
					VGSM_MESIM_MSG_RESET_REMOVED, NULL, 0);
			else
				vgsm_mesim_send_message(
					mesim,
					VGSM_MESIM_MSG_RESET_ASSERTED, NULL, 0);
		}

		mesim->vcc = vcc;
		mesim->rst = rst;
	}

	vgsm_mesim_debug(mesim, "modem thread exiting\n");

	return NULL;
}
