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

#include <linux/vgsm2.h>

#include "chan_vgsm.h"
#include "util.h"
#include "timer.h"
#include "mesim.h"
#include "mesim_local.h"
#include "mesim_clnt.h"
#include "mesim_impl.h"

static void vgsm_mesim_timers_updated(struct vgsm_timerset *set);
static void vgsm_mesim_timer(struct vgsm_timer *timer, enum vgsm_timer_action action, void *start_data);

int vgsm_mesim_create(
	struct vgsm_mesim *mesim,
	struct vgsm_me *me,
	const char *name)
{
//	int err;

	mesim->fd = -1;
	mesim->state = VGSM_MESIM_CLOSED;
	mesim->me = me;
	mesim->name = name;

	mesim->is_reset = TRUE;

	ast_mutex_init(&mesim->state_lock);
	ast_cond_init(&mesim->state_cond, NULL);

	vgsm_timerset_init(&mesim->timerset, vgsm_mesim_timers_updated);
	vgsm_timer_create(&mesim->timer, &mesim->timerset, "mesim",
			vgsm_mesim_timer);

	return 0;
}

void vgsm_mesim_destroy(struct vgsm_mesim *mesim)
{
}

struct vgsm_mesim *vgsm_mesim_get(
	struct vgsm_mesim *mesim)
{
	vgsm_me_get(mesim->me);

	return mesim;
}

void _vgsm_mesim_put(struct vgsm_mesim *mesim)
{
	vgsm_me_put(mesim->me);
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
	case VGSM_MESIM_MSG_HOLDER_REMOVED:
		return "HOLDER REMOVED";
	case VGSM_MESIM_MSG_HOLDER_INSERTED:
		return "HOLDER INSERTED";
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
	case VGSM_MESIM_SETTING_MODE:
		return "SETTING_MODE";
	case VGSM_MESIM_UNCONFIGURED:
		return "UNCONFIGURED";
	case VGSM_MESIM_DIRECTLY_ROUTED:
		return "DIRECTLY_ROUTED";
	case VGSM_MESIM_HOLDER_REMOVED:
		return "HOLDER_REMOVED";
	case VGSM_MESIM_HOLDER_CHANGING:
		return "HOLDER_CHANGING";
	case VGSM_MESIM_SIM_MISSING:
		return "SIM_MISSING";
	case VGSM_MESIM_RESET:
		return "RESET";
	case VGSM_MESIM_READY:
		return "READY";
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

	assert(pthread_self() != mesim->comm_thread);

	msg = alloca(sizeof(*msg) + len);
	msg->type = mt;
	msg->len = len;

	if (data && len)
		memcpy(&msg->data, data, len);

	if (write(mesim->cmd_pipe_write, msg, sizeof(*msg) + len) < 0) {
		ast_log(LOG_ERROR,
			"Cannot write on cmd_pipe pipe: %s\n",
			strerror(errno));
	}
}

void vgsm_mesim_get_ready_for_poweron(struct vgsm_mesim *mesim)
{
	vgsm_mesim_send_message(mesim, VGSM_MESIM_MSG_ME_POWERING_ON, NULL, 0);

	ast_mutex_lock(&mesim->state_lock);
	while(!mesim->insert_override) {
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

	close(mesim->fd);
	mesim->fd = -1;
}

static void *vgsm_mesim_thread_main(void *data);
static void *vgsm_mesim_modem_thread_main(void *data);

int vgsm_mesim_open(struct vgsm_mesim *mesim, const char *devname)
{
	int err;

	assert(mesim->state == VGSM_MESIM_CLOSED);

	mesim->fd = open(devname, O_RDWR | O_NOCTTY | O_NDELAY);
	if (mesim->fd < 0) {
		char tmpstr[64];
		snprintf(tmpstr, sizeof(tmpstr),
			"Error opening device: open(%s): %s",
				devname,
				strerror(errno));

		err = -errno;
		goto err_open;
	}

	int flags;
	flags = fcntl(mesim->fd, F_GETFL, 0);

	flags &= ~O_NONBLOCK;

	if (fcntl(mesim->fd, F_SETFL, flags) < 0) {
		ast_log(LOG_ERROR,
			"Cannot set MESIM fd to non-blocking: %s\n",
			strerror(errno));
		err = -errno;
		goto err_fcntl;
	}

	struct termios newtio;
	memset(&newtio, 0, sizeof(newtio));

	newtio.c_cflag = B38400 | CS8 | CLOCAL | CREAD |
			 PARENB | HUPCL | CSTOPB;
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
	
	if (tcflush(mesim->fd, TCIOFLUSH) < 0) {
		ast_log(LOG_ERROR,
			"%s: tcflush(TCIOFLUSH):  %s\n",
			mesim->name,
			strerror(errno));

		err = -errno;
		goto err_tcflush;
	}

	if (tcsetattr(mesim->fd, TCSANOW, &newtio) < 0) {
		char tmpstr[64];
		snprintf(tmpstr, sizeof(tmpstr),
			"Error setting tty's attributes: "
			"tcsetattr(%s): %s",
				devname,
				strerror(errno));

		err = -errno;
		goto err_tcsetattr;
	}

	if (tcflush(mesim->fd, TCIOFLUSH) < 0) {
		ast_log(LOG_ERROR,
			"%s: tcflush(TCIOFLUSH):  %s\n",
			mesim->name,
			strerror(errno));

		err = -errno;
		goto err_tcflush;
	}

	struct serial_struct ss;
	if (ioctl(mesim->fd, TIOCGSERIAL, &ss) < 0) {
		ast_log(LOG_ERROR,
			"%s: ioctl(TIOCGSERIAL):  %s\n",
			mesim->name,
			strerror(errno));

		err = -errno;
		goto err_ioctl_gserial;
	}

	ss.custom_divisor = ss.baud_base / 8643;
	ss.flags &= ~ASYNC_SPD_MASK;
	ss.flags |= ASYNC_SPD_CUST;

	if (ioctl(mesim->fd, TIOCSSERIAL, &ss) < 0) {
		ast_log(LOG_ERROR,
			"%s: ioctl(TIOCSSERIAL):  %s\n",
			mesim->name,
			strerror(errno));

		err = -errno;
		goto err_ioctl_sserial;
	}

	int filedes[2];
	if (pipe(filedes) < 0) {
		ast_log(LOG_ERROR,
			"Cannot create cmd_pipe pipe: %s\n",
			strerror(errno));
		err = -errno;
		goto err_pipe;
	}

	mesim->cmd_pipe_read = filedes[0];
	mesim->cmd_pipe_write = filedes[1];

	pthread_attr_t attr;
	pthread_attr_init(&attr);
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

	pthread_kill(mesim->modem_thread, SIGTERM);
	pthread_join(mesim->modem_thread, NULL);
err_pthread_create_modem:
	vgsm_mesim_send_message(mesim, VGSM_MESIM_MSG_CLOSE, NULL, 0);

	ast_mutex_lock(&mesim->state_lock);
	while(mesim->state != VGSM_MESIM_CLOSED) {
		ast_cond_wait(&mesim->state_cond, &mesim->state_lock);
	}
	ast_mutex_unlock(&mesim->state_lock);
err_pthread_create_comm:
	close(mesim->cmd_pipe_read);
	close(mesim->cmd_pipe_write);
err_pipe:
err_ioctl_sserial:
err_ioctl_gserial:
err_tcflush:
err_tcsetattr:
err_fcntl:
	close(mesim->fd);
	mesim->fd = -1;
err_open:

	return err;
}

void vgsm_mesim_change_state(
	struct vgsm_mesim *mesim,
	enum vgsm_mesim_state newstate,
	longtime_t timeout)
{
	vgsm_mesim_debug(mesim,
		"State change from %s to %s\n",
		vgsm_mesim_state_to_text(mesim->state),
		vgsm_mesim_state_to_text(newstate));

	ast_mutex_lock(&mesim->state_lock);
	mesim->state = newstate;
	ast_mutex_unlock(&mesim->state_lock);

	ast_cond_broadcast(&mesim->state_cond);

	if (timeout >= 0)
		vgsm_timer_start_delta(&mesim->timer, timeout, mesim);
	else
		vgsm_timer_stop(&mesim->timer);
}

#if 0
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
#endif

int vgsm_mesim_write(
	struct vgsm_mesim *mesim,
	void *data, int size)
{
	char *hextext = alloca(size * 2 + 1);

	int i;
	for(i=0; i<size; i++)
		sprintf(&hextext[i*2], "%02x", ((__u8 *)data)[i]);

	vgsm_mesim_debug(mesim, "MESIM TX (%2d): %s\n", size, hextext);

	return write(mesim->fd, data, size);
}

void vgsm_mesim_set_inserted(struct vgsm_mesim *mesim)
{
	int status = TIOCM_RTS;

	if (ioctl(mesim->fd, TIOCMSET, &status) < 0) {
		ast_log(LOG_WARNING, "ioctl(TIOCMSET): %s\n", strerror(errno));
		return;
	}
}

void vgsm_mesim_set_removed(struct vgsm_mesim *mesim)
{
	if (mesim->insert_override)
		return;

	int status = 0;

	if (ioctl(mesim->fd, TIOCMSET, &status) < 0) {
		ast_log(LOG_WARNING, "ioctl(TIOCMSET): %s\n", strerror(errno));
		return;
	}
}

static void vgsm_mesim_deactivate(struct vgsm_mesim *mesim)
{
	if (mesim->driver->deactivate)
		mesim->driver->deactivate(mesim->driver);

	if (mesim->driver_type == VGSM_MESIM_DRIVER_LOCAL) {
		vgsm_mesim_local_destroy(mesim->driver);
	} else if (mesim->driver_type == VGSM_MESIM_DRIVER_CLIENT) {
		vgsm_mesim_clnt_destroy(mesim->driver);
	} else if (mesim->driver_type == VGSM_MESIM_DRIVER_IMPLEMENTA) {
		vgsm_mesim_impl_destroy(mesim->driver);
	}

	mesim->driver = NULL;

	vgsm_mesim_set_removed(mesim);
}

static void vgsm_mesim_activate(struct vgsm_mesim *mesim)
{
	if (mesim->driver->activate)
		mesim->driver->activate(mesim->driver);
}

static void vgsm_mesim_set_mode(
	struct vgsm_mesim *mesim,
	struct vgsm_mesim_message *msg)
{
	struct vgsm_mesim_set_mode *sm = (void *)msg->data;

	assert(msg->len == sizeof(*sm));

	if (mesim->driver) {
		if (mesim->driver_type == VGSM_MESIM_DRIVER_LOCAL) {
			vgsm_mesim_local_destroy(mesim->driver);
		} else if (mesim->driver_type == VGSM_MESIM_DRIVER_CLIENT) {
			vgsm_mesim_clnt_destroy(mesim->driver);
		} else if (mesim->driver_type == VGSM_MESIM_DRIVER_IMPLEMENTA) {
			vgsm_mesim_impl_destroy(mesim->driver);
		}

		mesim->driver = NULL;
	}

	mesim->driver_type = sm->driver_type;

	if (mesim->driver_type == VGSM_MESIM_DRIVER_LOCAL) {
		mesim->driver = vgsm_mesim_local_create(mesim,
						&mesim->timerset);
	} else if (mesim->driver_type == VGSM_MESIM_DRIVER_CLIENT) {
		mesim->driver = vgsm_mesim_clnt_create(mesim,
						&mesim->timerset);
	} else if (mesim->driver_type == VGSM_MESIM_DRIVER_IMPLEMENTA) {
		mesim->driver = vgsm_mesim_impl_create(mesim,
						&mesim->timerset);
	} else
		assert(0);

	if (mesim->driver->set_mode)
		mesim->driver->set_mode(mesim->driver, msg);
}

static int vgsm_mesim_receive_data(struct vgsm_mesim *mesim)
{
	__u8 buf[32];
	int nread = read(mesim->fd, buf, sizeof(buf));

	char hextext[sizeof(buf)*2 + 1];

	int i;
	for(i=0; i<nread; i++)
		sprintf(&hextext[i*2], "%02x", buf[i]);

	vgsm_mesim_debug(mesim, "MESIM RX (%2d): %s\n", nread, hextext);

	switch(mesim->state) {
	case VGSM_MESIM_CLOSED:
	case VGSM_MESIM_CLOSING:
	case VGSM_MESIM_SETTING_MODE:
	case VGSM_MESIM_UNCONFIGURED:
	case VGSM_MESIM_DIRECTLY_ROUTED:
	case VGSM_MESIM_HOLDER_REMOVED:
	case VGSM_MESIM_HOLDER_CHANGING:
	case VGSM_MESIM_SIM_MISSING:
	case VGSM_MESIM_RESET:
	break;

	case VGSM_MESIM_READY:
		if (mesim->driver->send)
			mesim->driver->send(mesim->driver, buf, nread);
	break;
	}

	return 0;
}

static void vgsm_mesim_receive_message_refresh(
	struct vgsm_mesim *mesim,
	struct vgsm_mesim_message *msg)
{
}

static void vgsm_mesim_receive_message_close(
	struct vgsm_mesim *mesim,
	struct vgsm_mesim_message *msg)
{
	vgsm_mesim_set_removed(mesim);
	vgsm_mesim_change_state(mesim, VGSM_MESIM_CLOSING, -1);
}

static void vgsm_mesim_receive_message_set_mode(
	struct vgsm_mesim *mesim,
	struct vgsm_mesim_message *msg)
{
	switch(mesim->state) {
	case VGSM_MESIM_CLOSED:
	case VGSM_MESIM_CLOSING:
	case VGSM_MESIM_SETTING_MODE:
		assert(0);
	break;

	case VGSM_MESIM_UNCONFIGURED:
		vgsm_mesim_set_removed(mesim);
		vgsm_mesim_set_mode(mesim, msg);
		vgsm_mesim_activate(mesim);
	break;

	case VGSM_MESIM_HOLDER_CHANGING:
		/* Should stop timer, however it is restarted anyway */
	case VGSM_MESIM_DIRECTLY_ROUTED:
	case VGSM_MESIM_HOLDER_REMOVED:
	case VGSM_MESIM_SIM_MISSING:
	case VGSM_MESIM_RESET:
	case VGSM_MESIM_READY:
		vgsm_mesim_deactivate(mesim);
		vgsm_mesim_set_mode(mesim, msg);
		vgsm_mesim_change_state(mesim, VGSM_MESIM_SETTING_MODE,
								3 * SEC);
	break;
	}
}

static void vgsm_mesim_receive_message_powering_on(
	struct vgsm_mesim *mesim,
	struct vgsm_mesim_message *msg)
{
	vgsm_mesim_set_inserted(mesim);
	ast_mutex_lock(&mesim->state_lock);
	mesim->insert_override = TRUE;
	ast_mutex_unlock(&mesim->state_lock);

	ast_cond_broadcast(&mesim->state_cond);
}

static void vgsm_mesim_receive_message_powered_on(
	struct vgsm_mesim *mesim,
	struct vgsm_mesim_message *msg)
{
	ast_mutex_lock(&mesim->state_lock);
	mesim->insert_override = FALSE;
	ast_mutex_unlock(&mesim->state_lock);

	ast_cond_broadcast(&mesim->state_cond);

	if (mesim->state == VGSM_MESIM_HOLDER_CHANGING ||
	    mesim->state == VGSM_MESIM_HOLDER_REMOVED ||
	    mesim->state == VGSM_MESIM_DIRECTLY_ROUTED)
		vgsm_mesim_set_removed(mesim);
	else
		vgsm_mesim_set_inserted(mesim);
}

static void vgsm_mesim_receive_message_reset_asserted(
	struct vgsm_mesim *mesim,
	struct vgsm_mesim_message *msg)
{
	mesim->is_reset = TRUE;

	switch(mesim->state) {
	case VGSM_MESIM_CLOSED:
	case VGSM_MESIM_CLOSING:
		assert(0);
	break;

	case VGSM_MESIM_UNCONFIGURED:
	case VGSM_MESIM_SETTING_MODE:
	case VGSM_MESIM_DIRECTLY_ROUTED:
	case VGSM_MESIM_HOLDER_REMOVED:
	case VGSM_MESIM_HOLDER_CHANGING:
	case VGSM_MESIM_SIM_MISSING:
	case VGSM_MESIM_RESET:
	break;

	case VGSM_MESIM_READY:
		vgsm_mesim_change_state(mesim, VGSM_MESIM_RESET, -1);

		if (mesim->driver->reset_asserted)
			mesim->driver->reset_asserted(mesim->driver);
	break;
	}
}

static void vgsm_mesim_receive_message_reset_removed(
	struct vgsm_mesim *mesim,
	struct vgsm_mesim_message *msg)
{
	mesim->is_reset = FALSE;

	switch(mesim->state) {
	case VGSM_MESIM_CLOSED:
	case VGSM_MESIM_CLOSING:
		assert(0);
	break;

	case VGSM_MESIM_UNCONFIGURED:
	case VGSM_MESIM_SETTING_MODE:
	case VGSM_MESIM_DIRECTLY_ROUTED:
	case VGSM_MESIM_HOLDER_REMOVED:
	case VGSM_MESIM_HOLDER_CHANGING:
	case VGSM_MESIM_SIM_MISSING:
	case VGSM_MESIM_READY:
	break;

	case VGSM_MESIM_RESET:
/*		if (tcflush(mesim->fd, TCIFLUSH) < 0) {
			ast_log(LOG_ERROR,
				"%s: tcflush(TCIOFLUSH):  %s\n",
				mesim->name,
				strerror(errno));

			return;
		}*/

		vgsm_mesim_change_state(mesim, VGSM_MESIM_READY, -1);

		if (mesim->driver->reset_removed)
			mesim->driver->reset_removed(mesim->driver);
	break;
	}
}

static void vgsm_mesim_receive_message_holder_removed(
	struct vgsm_mesim *mesim,
	struct vgsm_mesim_message *msg)
{
	switch(mesim->state) {
	case VGSM_MESIM_CLOSED:
	case VGSM_MESIM_CLOSING:
	case VGSM_MESIM_DIRECTLY_ROUTED:
	case VGSM_MESIM_UNCONFIGURED:
	case VGSM_MESIM_SETTING_MODE:
		assert(0);
	break;

	case VGSM_MESIM_HOLDER_REMOVED:
	break;

	case VGSM_MESIM_HOLDER_CHANGING:
	case VGSM_MESIM_SIM_MISSING:
	case VGSM_MESIM_READY:
	case VGSM_MESIM_RESET:
		vgsm_mesim_change_state(mesim, VGSM_MESIM_HOLDER_REMOVED, -1);
		vgsm_mesim_set_removed(mesim);
	break;
	}

	if (mesim->driver->holder_removed)
		mesim->driver->holder_removed(mesim->driver);
}

static void vgsm_mesim_receive_message_holder_inserted(
	struct vgsm_mesim *mesim,
	struct vgsm_mesim_message *msg)
{
	switch(mesim->state) {
	case VGSM_MESIM_CLOSED:
	case VGSM_MESIM_CLOSING:
	case VGSM_MESIM_UNCONFIGURED:
	case VGSM_MESIM_SETTING_MODE:
	case VGSM_MESIM_DIRECTLY_ROUTED:
	case VGSM_MESIM_HOLDER_CHANGING:
	case VGSM_MESIM_SIM_MISSING:
	case VGSM_MESIM_READY:
	case VGSM_MESIM_RESET:
		assert(0);
	break;

	case VGSM_MESIM_HOLDER_REMOVED:
		if (mesim->is_reset) {
			vgsm_mesim_change_state(mesim, VGSM_MESIM_RESET, -1);
		} else {
			vgsm_mesim_change_state(mesim,
				VGSM_MESIM_HOLDER_CHANGING, 3 * SEC);
			vgsm_mesim_set_removed(mesim);
		}

		vgsm_mesim_set_inserted(mesim);
	break;
	}

	if (mesim->driver->holder_inserted)
		mesim->driver->holder_inserted(mesim->driver);
}

static BOOL vgsm_mesim_receive_message(struct vgsm_mesim *mesim)
{
	struct vgsm_mesim_message msgh;

	if (read(mesim->cmd_pipe_read, &msgh, sizeof(msgh)) < 0) {
		ast_log(LOG_WARNING, "Error reading from command pipe: %s\n",
			strerror(errno));
		return FALSE;
	}

	struct vgsm_mesim_message *msg;

	if (msgh.len) {
		msg = alloca(sizeof(*msg) + msgh.len);
		if (!msg) {
			assert(0);
			// FIXME
		}

		memcpy(msg, &msgh, sizeof(*msg));

		if (read(mesim->cmd_pipe_read, &msg->data, msg->len) < 0) {
			ast_log(LOG_WARNING,
				"Error reading from command pipe: %s\n",
				strerror(errno));
			return FALSE;
		}
	} else
		msg = &msgh;

	vgsm_mesim_debug(mesim, "Received message %s (len=%d)\n",
		vgsm_mesim_message_type_to_text(msg->type), msg->len);

	switch(msg->type) {
	case VGSM_MESIM_MSG_REFRESH:
		vgsm_mesim_receive_message_refresh(mesim, msg);
	break;

	case VGSM_MESIM_MSG_CLOSE:
		vgsm_mesim_receive_message_close(mesim, msg);
		return TRUE;
	break;

	case VGSM_MESIM_MSG_SET_MODE:
		vgsm_mesim_receive_message_set_mode(mesim, msg);
	break;

	case VGSM_MESIM_MSG_ME_POWERING_ON:
		vgsm_mesim_receive_message_powering_on(mesim, msg);
	break;

	case VGSM_MESIM_MSG_ME_POWERED_ON:
		vgsm_mesim_receive_message_powered_on(mesim, msg);
	break;

	case VGSM_MESIM_MSG_RESET_ASSERTED:
		vgsm_mesim_receive_message_reset_asserted(mesim, msg);
	break;

	case VGSM_MESIM_MSG_RESET_REMOVED:
		vgsm_mesim_receive_message_reset_removed(mesim, msg);
	break;

	case VGSM_MESIM_MSG_HOLDER_REMOVED:
		vgsm_mesim_receive_message_holder_removed(mesim, msg);
	break;

	case VGSM_MESIM_MSG_HOLDER_INSERTED:
		vgsm_mesim_receive_message_holder_inserted(mesim, msg);
	break;
	}

	return FALSE;
}

static void vgsm_mesim_timer_fired(struct vgsm_mesim *mesim);
static void vgsm_mesim_timer(struct vgsm_timer *timer, enum vgsm_timer_action action, void *start_data)
{
	struct vgsm_mesim *mesim = timer->data;

	switch(action) {
	case VGSM_TIMER_STOPPED:
		vgsm_mesim_put(mesim);
		timer->data = NULL;
	break;

	case VGSM_TIMER_STARTED:
		timer->data = start_data;
		vgsm_mesim_get((struct vgsm_mesim *)start_data);
	break;

	case VGSM_TIMER_FIRED:
		timer->data = NULL;
		vgsm_mesim_timer_fired(mesim);
		vgsm_mesim_put(mesim);
	}
}

static void vgsm_mesim_timer_fired(struct vgsm_mesim *mesim)
{
	vgsm_mesim_debug(mesim, "Timer fired in state %s\n",
			vgsm_mesim_state_to_text(mesim->state));

	switch(mesim->state) {
	case VGSM_MESIM_CLOSED:
	case VGSM_MESIM_CLOSING:
	case VGSM_MESIM_UNCONFIGURED:
	case VGSM_MESIM_DIRECTLY_ROUTED:
	case VGSM_MESIM_HOLDER_REMOVED:
	case VGSM_MESIM_SIM_MISSING:
	case VGSM_MESIM_RESET:
	case VGSM_MESIM_READY:
		assert(0);
	break;

	case VGSM_MESIM_HOLDER_CHANGING:
		if (mesim->is_reset)
			vgsm_mesim_change_state(mesim, VGSM_MESIM_RESET, -1);
		else
			vgsm_mesim_change_state(mesim, VGSM_MESIM_READY, -1);

		vgsm_mesim_set_inserted(mesim);
	break;

	case VGSM_MESIM_SETTING_MODE:
		vgsm_mesim_activate(mesim);
	break;
	}
}

static void vgsm_mesim_timers_updated(struct vgsm_timerset *set)
{
	struct vgsm_mesim *mesim = container_of(set, struct vgsm_mesim,
								timerset);

	/* If the timers have been updated in the handling thread we are
	 * already going to recalculate before select so threre is no need
	 * to send a message to ourselves risking a deadlock if the pipe does
	 * not have space
	 */

	if (pthread_self() != mesim->comm_thread)
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

	vgsm_mesim_change_state(mesim, VGSM_MESIM_UNCONFIGURED, -1);

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

		npolls = 2;
		if (mesim->driver &&
		    mesim->driver->get_polls)
			npolls += mesim->driver->get_polls(mesim->driver,
							&polls[2]);

		int res = poll(polls, npolls, timeout_ms);
		if (res < 0) {
			if (errno != EINTR) {
				ast_log(LOG_WARNING,
					"vgsm: Error polling: %s\n",
					strerror(errno));
			}

			continue;
		}

		if (polls[0].revents & (POLLIN | POLLERR | POLLHUP)) {
			if (vgsm_mesim_receive_message(mesim))
				break;
		}

		if (polls[1].revents & (POLLIN | POLLERR | POLLHUP))
			vgsm_mesim_receive_data(mesim);

		if (npolls > 2 && (polls[2].revents & (POLLIN | POLLERR |
								POLLHUP))) {
			assert(mesim->driver->receive);
			mesim->driver->receive(mesim->driver);
		}
	}

	mesim->modem_thread_has_to_exit = TRUE;

	vgsm_mesim_deactivate(mesim);

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

	vgsm_mesim_change_state(mesim, VGSM_MESIM_CLOSED, -1);

	return NULL;
}

static void vgsm_mesim_sigterm_handler(int sig)
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
		ast_log(LOG_ERROR, "sigaction(): %s\n", strerror(errno));
		return NULL;
	}

	sigset_t sigs;
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGTERM);
	if (pthread_sigmask(SIG_UNBLOCK, &sigs, NULL)) {
		ast_log(LOG_ERROR, "pthread_sigmask(): %s\n",
					strerror(errno));
		return NULL;
	}

	if (ioctl(mesim->fd, TIOCGICOUNT, &mesim->icount) < 0) {
		ast_log(LOG_ERROR, "ioctl(TIOCGICOUNT): %s\n",
					strerror(errno));
		return NULL;
	}

	int status;
	if (ioctl(mesim->fd, TIOCMGET, &status) < 0) {
		ast_log(LOG_ERROR, "ioctl(TIOCMGET): %s\n",
					strerror(errno));
		return NULL;
	}

	mesim->vcc = !!(status & TIOCM_DSR);
	mesim->rst = !(status & TIOCM_CTS);

	struct serial_icounter_struct icount;
	if (ioctl(mesim->fd, TIOCGICOUNT, &icount) < 0) {
		ast_log(LOG_ERROR, "ioctl(TIOCGICOUNT): %s\n", strerror(errno));
		return NULL;
	}

	mesim->icount.dsr = icount.dsr;
	mesim->icount.cts = icount.cts;

	for(;;) {
		if (ioctl(mesim->fd, TIOCMIWAIT, TIOCM_CTS | TIOCM_DSR) < 0) {
			if (errno != EINTR) {
				ast_log(LOG_ERROR, "ioctl(TIOCMIWAIT): %s\n",
							strerror(errno));
				break;
			}
		}

		if (mesim->modem_thread_has_to_exit)
			break;

		if (ioctl(mesim->fd, TIOCGICOUNT, &icount) < 0) {
			ast_log(LOG_ERROR, "ioctl(TIOCGICOUNT): %s\n",
						strerror(errno));
			break;
		}

		if (ioctl(mesim->fd, TIOCMGET, &status) < 0) {
			ast_log(LOG_ERROR, "ioctl(TIOCMGET): %s\n",
						strerror(errno));
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

			if (vcc && !rst) {
#if 0
				if (mesim->proto == VGSM_MESIM_PROTO_IMPLEMENTA) {
					vgsm_mesim_debug(mesim, "Sending ATR\n");

					__u8 atr[] = { 0x3b, 0xb0, 0x11, 0x00, 0xC0,
							0xFF, 0x1F, 0xC3, 0x42};

					if (vgsm_mesim_write(mesim, atr, sizeof(atr)) < 0) {
						ast_log(LOG_WARNING,
							"Error writing ATR to MESIM: %s\n",
							strerror(errno));
					}
				}

#endif

				vgsm_mesim_send_message(
					mesim,
					VGSM_MESIM_MSG_RESET_REMOVED, NULL, 0);
			} else {
				vgsm_mesim_send_message(
					mesim,
					VGSM_MESIM_MSG_RESET_ASSERTED, NULL, 0);
			}

			mesim->icount.dsr = icount.dsr;
			mesim->icount.cts = icount.cts;
		}

		mesim->vcc = vcc;
		mesim->rst = rst;
	}

	vgsm_mesim_debug(mesim, "modem thread exiting\n");

	return NULL;
}
