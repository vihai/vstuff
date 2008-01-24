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
#include "atr.h"

static const char *vgsm_mesim_local_type_to_text(
	enum vgsm_mesim_local_type type)
{
	switch(type) {
	case VGSM_MESIM_LOCAL_TYPE_VGSM_DIRECT:
		return "VGSM_DIRECT";
	case VGSM_MESIM_LOCAL_TYPE_VGSM:
		return "VGSM";
	case VGSM_MESIM_LOCAL_TYPE_USB:
		return "USB";
	}

	return "*UNKNOWN*";
}

static const char *vgsm_mesim_local_state_to_text(
	enum vgsm_mesim_local_state state)
{
	switch(state) {
	case VGSM_MESIM_LOCAL_STATE_NULL:
		return "NULL";
	case VGSM_MESIM_LOCAL_STATE_HOLDER_REMOVED:
		return "HOLDER_REMOVED";
	case VGSM_MESIM_LOCAL_STATE_RESET:
		return "RESET";
	case VGSM_MESIM_LOCAL_STATE_READING_ATR:
		return "READING_ATR";
	case VGSM_MESIM_LOCAL_STATE_READY:
		return "READY";
	case VGSM_MESIM_LOCAL_STATE_FAILED:
		return "FAILED";
	}

	return "*UNKNOWN*";
}

static void vgsm_mesim_local_change_state(
	struct vgsm_mesim_local *mesim_local,
	enum vgsm_mesim_local_state newstate,
	longtime_t timeout)
{
	vgsm_mesim_debug(mesim_local->mesim,
		"Local SIM state changed from %s to %s\n",
		vgsm_mesim_local_state_to_text(mesim_local->state),
		vgsm_mesim_local_state_to_text(newstate));

//	ast_mutex_lock(&mesim_local->state_lock);
	mesim_local->state = newstate;
//	ast_mutex_unlock(&mesim_local->state_lock);
//
	if (timeout >= 0)
		vgsm_timer_start_delta(&mesim_local->timer, timeout);
	else
		vgsm_timer_stop(&mesim_local->timer);
}

static BOOL vgsm_mesim_local_is_inserted(
	struct vgsm_mesim_local *mesim_local,
	int status)
{
	if (mesim_local->type == VGSM_MESIM_LOCAL_TYPE_VGSM)
		return (status & TIOCM_CTS);
	else
		return !(status & TIOCM_CTS);
}

static void vgsm_mesim_local_set_lines(
	struct vgsm_mesim_local *mesim_local,
	BOOL vcc, BOOL reset)
{
	struct vgsm_mesim *mesim = mesim_local->mesim;
	int status;

	if (mesim_local->type == VGSM_MESIM_LOCAL_TYPE_USB) {
		status = (vcc ? 0 : TIOCM_DTR) | (reset ? TIOCM_RTS : 0);
	} else {
		status = (vcc ? TIOCM_DTR : 0) | (reset ? 0: TIOCM_RTS);
	}

	if (ioctl(mesim_local->fd, TIOCMSET, &status) < 0) {
		ast_log(LOG_WARNING,
			"ioctl(TIOCMSET): %s\n",
			strerror(errno));
		return;
	}

	vgsm_mesim_debug(mesim, "Local SIM lines set as: VCC=%s, RST=%s\n",
		vcc ? "ON" : "off",
		reset ? "ON" : "off");
}



static void vgsm_mesim_local_sigterm_handler(int sig)
{
}

static void *vgsm_mesim_local_modem_thread_main(void *data)
{
	struct vgsm_mesim_local *mesim_local = data;
	struct vgsm_mesim *mesim = mesim_local->mesim;

	struct sigaction sigterm = {
		.sa_handler = vgsm_mesim_local_sigterm_handler,
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
		ast_log(LOG_ERROR, "pthread_sigmask(): %s\n", strerror(errno));
		return NULL;
	}

	int status;
	if (ioctl(mesim_local->fd, TIOCMGET, &status) < 0) {
		ast_log(LOG_ERROR, "ioctl(TIOCMGET): %s\n", strerror(errno));
		return NULL;
	}

	for(;;) {
		if (ioctl(mesim_local->fd, TIOCMIWAIT, TIOCM_CTS) < 0) {
			if (errno != EINTR) {
				ast_log(LOG_ERROR,
					"ioctl(TIOCMIWAIT): %s\n",
					strerror(errno));
				break;
			}
		}

		if (mesim_local->modem_thread_has_to_exit)
			break;

		if (ioctl(mesim_local->fd, TIOCMGET, &status) < 0) {
			ast_log(LOG_ERROR, "ioctl(TIOCMGET): %s\n",
						strerror(errno));
			break;
		}

		BOOL holder_in = vgsm_mesim_local_is_inserted(mesim_local,
								status);

		if (holder_in) {
			vgsm_mesim_send_message(
				mesim,
				VGSM_MESIM_MSG_HOLDER_INSERTED,
				NULL, 0);
		} else {
			vgsm_mesim_send_message(
				mesim,
				VGSM_MESIM_MSG_HOLDER_REMOVED,
				NULL, 0);
		}
	}

	vgsm_mesim_debug(mesim, "Local SIM modem thread exiting\n");

	return NULL;
}

static void vgsm_mesim_local_timer(void *data)
{
	struct vgsm_mesim_local *mesim_local = data;
	struct vgsm_mesim *mesim = mesim_local->mesim;

	vgsm_mesim_debug(mesim, "Local SIM timer fired in state %s\n",
			vgsm_mesim_local_state_to_text(mesim_local->state));

	switch(mesim_local->state) {
	case VGSM_MESIM_LOCAL_STATE_NULL:
	case VGSM_MESIM_LOCAL_STATE_HOLDER_REMOVED:
	case VGSM_MESIM_LOCAL_STATE_RESET:
	case VGSM_MESIM_LOCAL_STATE_READY:
	case VGSM_MESIM_LOCAL_STATE_FAILED:
		assert(0);
	break;

	case VGSM_MESIM_LOCAL_STATE_READING_ATR:
		vgsm_mesim_local_set_lines(mesim_local, TRUE, TRUE);

		vgsm_mesim_local_change_state(mesim_local,
					VGSM_MESIM_LOCAL_STATE_FAILED, -1);
	break;
	}
}

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

	mesim_local->state = VGSM_MESIM_LOCAL_STATE_NULL;

	vgsm_timer_init(&mesim_local->timer, timerset, "mesim_local",
			vgsm_mesim_local_timer, mesim_local);

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

	mesim_local->modem_thread_has_to_exit = TRUE;

	int err;
	err = -pthread_kill(mesim_local->modem_thread, SIGTERM);
	if (err) {
		ast_log(LOG_WARNING, "Cannot kill modem thread: %s\n",
			strerror(-err));
	}

	err = -pthread_join(mesim_local->modem_thread, NULL);
	if (err) {
		ast_log(LOG_WARNING, "Cannot join modem thread: %s\n",
			strerror(-err));
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

	int flags;
	flags = fcntl(mesim_local->fd, F_GETFL, 0);

	flags &= ~O_NONBLOCK;

	if (fcntl(mesim_local->fd, F_SETFL, flags) < 0) {
		ast_log(LOG_ERROR,
			"Cannot set Local SIM fd to non-blocking: %s\n",
			strerror(errno));
		return;
	}

	if (ioctl(mesim_local->fd, VGSM_IOC_SIM_GET_CARD_ID,
					&mesim_local->card_id) < 0) {
		if (errno != EINVAL) {
			ast_log(LOG_ERROR,
				"%s: ioctl(IOC_SIM_GET_ID)"
				" failed: %s\n",
				mesim->name,
				strerror(errno));
			return;
		}

		mesim_local->type = VGSM_MESIM_LOCAL_TYPE_USB;

		vgsm_mesim_debug(mesim, "Guessing USB SIM reader\n");
	} else {
		if (ioctl(mesim_local->fd, VGSM_IOC_SIM_GET_ID,
						&mesim_local->sim_id) < 0) {
			ast_log(LOG_ERROR,
				"%s: ioctl(IOC_SIM_GET_ID)"
				" failed: %s\n",
				mesim->name,
				strerror(errno));
			return;
		}

		if (mesim_local->card_id == mesim->me->card.id) {
			vgsm_mesim_debug(mesim,
				"SIM reader is vGSM-II on the same card,"
				" connecting directly\n");

			mesim_local->type = VGSM_MESIM_LOCAL_TYPE_VGSM_DIRECT;
		} else {
			vgsm_mesim_debug(mesim,
				"SIM reader is vGSM-II on a different card\n");

			mesim_local->type = VGSM_MESIM_LOCAL_TYPE_VGSM;
		}
	}

	struct termios newtio;
	memset(&newtio, 0, sizeof(newtio));

	newtio.c_cflag = B38400 | CS8 | CLOCAL | CREAD |
			 PARENB | HUPCL | CSTOPB;
	newtio.c_iflag = IGNBRK | IGNPAR;
	newtio.c_oflag = 0;
	newtio.c_lflag = 0;
	
//	newtio.c_ospeed = 8636;
//	newtio.c_ispeed = 8636;

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
		ast_log(LOG_ERROR,
			"Error getting serial parameters: %s\n",
			strerror(errno));

		return;
	}

	if (mesim_local->type == VGSM_MESIM_LOCAL_TYPE_USB)
		ss.custom_divisor = ss.baud_base / 9909;
	else
		ss.custom_divisor = ss.baud_base / 9600;

	ss.flags &= ~ASYNC_SPD_MASK;
	ss.flags |= ASYNC_SPD_CUST;

	if (ioctl(mesim_local->fd, TIOCSSERIAL, &ss) < 0) {
		ast_log(LOG_ERROR,
			"Error setting serial parameters: %s\n",
			strerror(errno));

		return;
	}

	int sim_id;
	if (mesim_local->type == VGSM_MESIM_LOCAL_TYPE_VGSM_DIRECT)
		sim_id = mesim_local->sim_id;
	else
		sim_id = VGSM_SIM_ROUTE_EXTERNAL;

	if (ioctl(mesim->fd, VGSM_IOC_SET_SIM_ROUTE, sim_id) < 0) {
		ast_log(LOG_ERROR,
			"%s: ioctl(IOC_SET_SIM_ROUTE, %d) failed: %s\n",
			mesim->name,
			sim_id,
			strerror(errno));
		return;
	}

	if (mesim_local->type == VGSM_MESIM_LOCAL_TYPE_VGSM_DIRECT) {
		vgsm_mesim_set_removed(mesim);
		vgsm_mesim_change_state(mesim, VGSM_MESIM_DIRECTLY_ROUTED, -1);

		vgsm_mesim_local_change_state(mesim_local,
					VGSM_MESIM_LOCAL_STATE_READY, -1);
	} else {
		vgsm_mesim_local_set_lines(mesim_local, FALSE, TRUE);
		usleep(10000);
		vgsm_mesim_local_set_lines(mesim_local, TRUE, TRUE);

		int status;
		if (ioctl(mesim_local->fd, TIOCMGET, &status) < 0) {
			ast_log(LOG_ERROR, "ioctl(TIOCMGET): %s\n",
						strerror(errno));
			return;
		}

		if (vgsm_mesim_local_is_inserted(mesim_local, status)) {
			vgsm_mesim_local_change_state(mesim_local,
					VGSM_MESIM_LOCAL_STATE_RESET, -1);

			vgsm_mesim_change_state(mesim,
				VGSM_MESIM_HOLDER_CHANGING, 6 * SEC);
			vgsm_mesim_set_removed(mesim);
		} else {
			vgsm_mesim_local_change_state(mesim_local,
				VGSM_MESIM_LOCAL_STATE_HOLDER_REMOVED, -1);
			vgsm_mesim_set_removed(mesim);
			vgsm_mesim_change_state(mesim,
					VGSM_MESIM_HOLDER_REMOVED, -1);
		}

		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

		int err;
		mesim_local->modem_thread_has_to_exit = FALSE;
		err = ast_pthread_create(&mesim_local->modem_thread, &attr,
					vgsm_mesim_local_modem_thread_main,
					mesim_local);
		if (err < 0) {
			ast_log(LOG_WARNING,
				"Cannot create SIM Local modem thread: %s\n",
				strerror(errno));
		}

		return;

		pthread_attr_destroy(&attr);
	}
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
	struct vgsm_mesim_local *mesim_local =
			container_of(driver, struct vgsm_mesim_local, driver);

	switch(mesim_local->state) {
	case VGSM_MESIM_LOCAL_STATE_NULL:
		assert(0);
	break;

	case VGSM_MESIM_LOCAL_STATE_HOLDER_REMOVED:
	case VGSM_MESIM_LOCAL_STATE_RESET:
	case VGSM_MESIM_LOCAL_STATE_FAILED:
	break;

	case VGSM_MESIM_LOCAL_STATE_READY:
	case VGSM_MESIM_LOCAL_STATE_READING_ATR:
		vgsm_mesim_local_set_lines(mesim_local, TRUE, TRUE);

		vgsm_mesim_local_change_state(mesim_local,
					VGSM_MESIM_LOCAL_STATE_RESET, -1);
	break;
	}
}

static void vgsm_mesim_local_reset_removed(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_local *mesim_local =
			container_of(driver, struct vgsm_mesim_local, driver);
	struct vgsm_mesim *mesim = mesim_local->mesim;

	switch(mesim_local->state) {
	case VGSM_MESIM_LOCAL_STATE_NULL:
	case VGSM_MESIM_LOCAL_STATE_READY:
		assert(0);
	break;

	case VGSM_MESIM_LOCAL_STATE_HOLDER_REMOVED:
	case VGSM_MESIM_LOCAL_STATE_RESET:
	case VGSM_MESIM_LOCAL_STATE_FAILED:
	case VGSM_MESIM_LOCAL_STATE_READING_ATR:
		/* Remove reset on the Local SIM side */
		vgsm_mesim_local_set_lines(mesim_local, TRUE, FALSE);

		mesim_local->atr_buf_len = 0;
		memset(mesim_local->atr_buf, 0, sizeof(mesim_local->atr_buf));
		mesim_local->out_buf_len = 0;
		memset(mesim_local->out_buf, 0, sizeof(mesim_local->out_buf));
		vgsm_mesim_local_change_state(mesim_local,
				VGSM_MESIM_LOCAL_STATE_READING_ATR, 1 * SEC);

		/* Send a fake ATR to the ME */
		vgsm_mesim_debug(mesim, "Sending ATR to MESIM\n");

		__u8 atr[] = { 0x3b, 0xb0, 0x11, 0x00, 0xC0,
				0xFF, 0x1F, 0xC3, 0x42};

		if (vgsm_mesim_write(mesim, atr, sizeof(atr)) < 0) {
			ast_log(LOG_WARNING,
				"Error writing ATR to MESIM: %s\n",
				strerror(errno));
		}
	}
}

static void vgsm_mesim_local_holder_inserted(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_local *mesim_local =
			container_of(driver, struct vgsm_mesim_local, driver);

	switch(mesim_local->state) {
	case VGSM_MESIM_LOCAL_STATE_NULL:
	case VGSM_MESIM_LOCAL_STATE_READY:
	case VGSM_MESIM_LOCAL_STATE_READING_ATR:
	case VGSM_MESIM_LOCAL_STATE_RESET:
	case VGSM_MESIM_LOCAL_STATE_FAILED:
		assert(0);
	break;

	case VGSM_MESIM_LOCAL_STATE_HOLDER_REMOVED:
		vgsm_mesim_local_set_lines(mesim_local, FALSE, TRUE);
		usleep(10000);
		vgsm_mesim_local_set_lines(mesim_local, TRUE, TRUE);

		vgsm_mesim_local_change_state(mesim_local,
					VGSM_MESIM_LOCAL_STATE_RESET, -1);
	break;
	}
}

static void vgsm_mesim_local_holder_removed(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_local *mesim_local =
			container_of(driver, struct vgsm_mesim_local, driver);

	switch(mesim_local->state) {
	case VGSM_MESIM_LOCAL_STATE_NULL:
	case VGSM_MESIM_LOCAL_STATE_HOLDER_REMOVED:
		assert(0);
	break;

	case VGSM_MESIM_LOCAL_STATE_READY:
	case VGSM_MESIM_LOCAL_STATE_READING_ATR:
	case VGSM_MESIM_LOCAL_STATE_RESET:
	case VGSM_MESIM_LOCAL_STATE_FAILED:
		/* The holder in VoiSmart SIM server must receive VCC,
		 * otherwise we wouldn't be able to detect holder insertion
		 */

		vgsm_mesim_local_set_lines(mesim_local, FALSE, TRUE);

		vgsm_mesim_local_change_state(mesim_local,
				VGSM_MESIM_LOCAL_STATE_HOLDER_REMOVED, -1);
	break;
	}
}

static int vgsm_mesim_local_write(
	struct vgsm_mesim_local *mesim_local,
	void *buf,
	int len)
{
	struct vgsm_mesim *mesim = mesim_local->mesim;

	char *hextext = alloca(len * 2 + 1);
	int i;
	for(i=0; i<len; i++)
		sprintf(&hextext[i*2], "%02X", *(__u8 *)(buf + i));

	vgsm_mesim_debug(mesim,
			"Local SIM Send (%2d): %s\n",
			len, hextext);

#if 0
	for(i=0; i<len ; i++) {
		if (write(mesim_local->fd, buf + i, 1) < 0) {
			ast_log(LOG_ERROR,
				"Error sending to local SIM: %s\n",
				strerror(errno));
			return -errno;
		}

		/* Guard time implemented in software */
		usleep(200);
	}
#endif

	if (write(mesim_local->fd, buf, len) < 0) {
		ast_log(LOG_ERROR,
			"Error sending to local SIM: %s\n",
			strerror(errno));
		return -errno;
	}

	return 0;
}

static int vgsm_mesim_local_send(
	struct vgsm_mesim_driver *driver,
	void *buf, int len)
{
	struct vgsm_mesim_local *mesim_local =
			container_of(driver, struct vgsm_mesim_local, driver);

	if (mesim_local->state == VGSM_MESIM_LOCAL_STATE_READING_ATR) {

		if (mesim_local->out_buf_len + len >
					sizeof(mesim_local->out_buf)) {
			ast_log(LOG_ERROR,
				"SIM local output buffer overflow\n");
			return -ENOMEM;
		}

		memcpy(mesim_local->out_buf + mesim_local->out_buf_len,
							buf, len);
		mesim_local->out_buf_len += len;

	} else if (mesim_local->state == VGSM_MESIM_LOCAL_STATE_READY) {
		int err = vgsm_mesim_local_write(mesim_local, buf, len);
		if (err < 0)
			return err;
	}

	return 0;
}

static BOOL vgsm_sim_atr_is_complete(__u8 *atr, int len)
{
	int pos = 0;

	/* TS */
	if (len < pos + 1)
		return FALSE;
	pos++;

	/* Decode T0 */
	if (len < pos + 1)
		return FALSE;

	struct vgsm_sim_atr_t0_onwire *t0 = (void *)(atr + pos);
	pos++;

	/* Decode TAi, TBi, TCi, TDi */
	BOOL ta_pres;
	BOOL tb_pres;
	BOOL tc_pres;
	BOOL td_pres;

	ta_pres = !!t0->ta1;
	tb_pres = !!t0->tb1;
	tc_pres = !!t0->tc1;
	td_pres = !!t0->td1;

	BOOL tck_needed = FALSE;

	while(1) {
		if (ta_pres) {
			if (len < pos + 1)
				return FALSE;
			pos++;
		}

		if (tb_pres) {
			if (len < pos + 1)
				return FALSE;
			pos++;
		}

		if (tc_pres) {
			if (len < pos + 1)
				return FALSE;
			pos++;
		}

		if (td_pres) {
			if (len < pos + 1)
				return FALSE;

			struct vgsm_sim_atr_td_onwire *td = (void *)(atr + pos);

			ta_pres = !!td->ta;
			tb_pres = !!td->tb;
			tc_pres = !!td->tc;
			td_pres = !!td->td;

			if (td->protocol_type != 0)
				tck_needed = TRUE;

			pos++;
		} else
			break;
	}

	if (len < pos + t0->k)
		return FALSE;
	pos += t0->k;

	/* TCK */
	if (tck_needed) {
		if (len < pos + 1)
			return FALSE;
	}

	return TRUE;
}

static int vgsm_mesim_local_receive_atr(
	struct vgsm_mesim_local *mesim_local)
{
	struct vgsm_mesim *mesim = mesim_local->mesim;

	int nread = read(mesim_local->fd,
		mesim_local->atr_buf + mesim_local->atr_buf_len,
		sizeof(mesim_local->atr_buf) - mesim_local->atr_buf_len);
	if (nread < 0) {
		ast_log(LOG_WARNING,
			"Error reading from local SIM: %s\n",
			strerror(errno));
	}

	mesim_local->atr_buf_len += nread;

	if (mesim_local->atr_buf_len == sizeof(mesim_local->atr_buf))
		ast_log(LOG_WARNING, "ATR buffer is full!\n");

	char *hextext = alloca(nread * 2 + 1);
	int i;
	for(i=0; i<nread; i++)
		sprintf(&hextext[i*2], "%02X",
			mesim_local->atr_buf[
				mesim_local->atr_buf_len - nread + i]);

	vgsm_mesim_debug(mesim,
			"Local SIM ATR (%2d): %s\n",
			nread, hextext);

	if (!vgsm_sim_atr_is_complete(mesim_local->atr_buf,
					mesim_local->atr_buf_len)) {
		vgsm_mesim_debug(mesim, "ATR is not yet complete\n");
		return 0;
	}

	/* Begin ATR decoding */

	__u8 *atr = mesim_local->atr_buf;
	int pos = 0;

	/* Decode TS */
	vgsm_mesim_debug(mesim, "Receiving ATR: TS=%02x\n", *(atr + pos));

	if (*(atr + pos) != 0x3b) {
		ast_log(LOG_WARNING, "Unexpected TS=0x%02x\n",
					*(atr + pos));
		return 0;
	}

	pos++;

	/* Decode T0 */
	struct vgsm_sim_atr_t0_onwire *t0 = (void *)(atr + pos);

	vgsm_mesim_debug(mesim, "T0: TA1=%d TB1=%d TC1=%d TD1=%d k=%d\n",
		t0->ta1,
		t0->tb1,
		t0->tc1,
		t0->td1,
		t0->k);

	pos++;

	/* Decode TAi, TBi, TCi, TDi */
	BOOL ta_pres;
	BOOL tb_pres;
	BOOL tc_pres;
	BOOL td_pres;

	i = 1;
	ta_pres = !!t0->ta1;
	tb_pres = !!t0->tb1;
	tc_pres = !!t0->tc1;
	td_pres = !!t0->td1;

	while(1) {
		if (ta_pres) {
			vgsm_mesim_debug(mesim, "TA%d: 0x%02x\n", i, *(atr + pos));

			pos++;
		}

		if (tb_pres) {
			vgsm_mesim_debug(mesim, "TB%d: 0x%02x\n", i, *(atr + pos));

			pos++;
		}

		if (tc_pres) {
			vgsm_mesim_debug(mesim, "TC%d: 0x%02x\n", i, *(atr + pos));

			pos++;
		}

		if (td_pres) {
			vgsm_mesim_debug(mesim, "TD%d: 0x%02x\n", i, *(atr + pos));

			struct vgsm_sim_atr_td_onwire *td = (void *)(atr + pos);

			vgsm_mesim_debug(mesim, "Protocol %d type = %d\n",
						i+1, td->protocol_type);

			ta_pres = !!td->ta;
			tb_pres = !!td->tb;
			tc_pres = !!td->tc;
			td_pres = !!td->td;

			pos++;
		} else
			break;
	}

	/*		struct vgsm_sim_atr_td_onwire td;

			__u8 protocol_type = 0;
			nread = read_exactly(fd, &td, 1);
			if (nread < 0) {
				perror("read_exactly(td)");
				return 1;
			}

			vgsm_mesim_debug(mesim, "TD=%02x\n", *(__u8 *)&td);

			protocol_type = td.protocol_type;

			vgsm_mesim_debug(mesim, "Protocol type = %d\n", td.protocol_type);*/

#if 0
	vgsm_mesim_debug(mesim, "Historic data: ");
	for(i=0; i<t0->k; i++)
		vgsm_mesim_debug(mesim, "%02x ", tk[i]);
	vgsm_mesim_debug(mesim, "\n");

	struct vgsm_sim_atr_global_interface
	{
		__u8 fi;
		__u8 di;
		__u8 ii;
		__u8 pi1;
		__u8 n;
		__u8 pi2;
	} gi = {};

	if (tchars[0].ta_pres) {
		struct vgsm_sim_atr_ta1_onwire *ta1 = (void *)&tchars[0].ta;

		gi.fi = ta1->fi;
		gi.di = ta1->di;
	} else {
		gi.fi = 1;
		gi.di = 1;
	}

	vgsm_mesim_debug(mesim, "FI=%x\n", gi.fi);
	vgsm_mesim_debug(mesim, "DI=%x\n", gi.di);

	if (tchars[0].tb_pres) {
		struct vgsm_sim_atr_tb1_onwire *tb1 = (void *)&tchars[0].tb;
		gi.ii = tb1->ii;
		gi.pi1 = tb1->pi1;
	} else {
		gi.ii = 0;
		gi.pi1 = 0;
	}

	vgsm_mesim_debug(mesim, "II=%x\n", gi.ii);
	vgsm_mesim_debug(mesim, "PI1=%x\n", gi.pi1);

	if (tchars[0].tc_pres) {
		struct vgsm_sim_atr_tc1_onwire *tc1 = (void *)&tchars[0].tc;
		gi.n = tc1->n;
	} else {
		gi.n = 0;
	}

	vgsm_mesim_debug(mesim, "N=%x\n", gi.n);

	__u8 tck;
	read_exactly(fd, &tck, 1);

	for(i=0; i<mesim_local->atr_buf_len - 1; i++)
		csum ^= tk[i];

	vgsm_mesim_debug(mesim, "TCK=%02x\n", tck);

	if (csum != tck)
		vgsm_mesim_debug(mesim, "Checksum (%02x) != TCK (%02x)\n", csum, tck);

#if 0
	if (t0->ta1) {
//		struct sim_ta_onwire *ta = (void *);

		vgsm_mesim_debug(mesim,
			"TA1: 0x%02x\n", *(__u8 *)(buf + bufpos++));
	}

	if (t0->tb1) {
		vgsm_mesim_debug(mesim,
			"TB1: 0x%02x\n", *(__u8 *)(buf + bufpos++));
	}

	if (t0->tc1) {
		vgsm_mesim_debug(mesim,
			"TC1: 0x%02x\n", *(__u8 *)(buf + bufpos++));
	}

	__u8 protocol_type = 0;
	if (t0->td1) {
		struct sim_td_onwire *td = (void *)(buf + bufpos++);

		protocol_type = td->protocol_type;

		vgsm_mesim_debug(mesim, "Protocol type = %d\n", td->protocol_type);
	}
#endif


#endif

	vgsm_mesim_local_change_state(mesim_local,
				VGSM_MESIM_LOCAL_STATE_READY, -1);
	if (mesim_local->out_buf_len) {
		int err = vgsm_mesim_local_write(mesim_local,
					mesim_local->out_buf,
					mesim_local->out_buf_len);
		if (err < 0)
			return err;

		mesim_local->out_buf_len = 0;
		memset(mesim_local->out_buf, 0, sizeof(mesim_local->out_buf));
	}

	return 0;
}

static int vgsm_mesim_local_receive_data(
	struct vgsm_mesim_local *mesim_local)
{
	struct vgsm_mesim *mesim = mesim_local->mesim;
	__u8 buf[128];

	int nread = read(mesim_local->fd, buf, sizeof(buf));
	if (nread < 0) {
		ast_log(LOG_WARNING,
			"Error reading from local SIM: %s\n",
			strerror(errno));
	}

	char *hextext = alloca(nread * 2 + 1);

	int i;
	for(i=0; i<nread; i++)
		sprintf(&hextext[i*2], "%02X", buf[i]);

	vgsm_mesim_debug(mesim,
			"Local SIM read (%2d): %s\n",
			nread, hextext);

	vgsm_mesim_write(mesim, buf, nread);

	return 0;
}

static int vgsm_mesim_local_receive_and_dump(
	struct vgsm_mesim_local *mesim_local)
{
	struct vgsm_mesim *mesim = mesim_local->mesim;
	__u8 buf[128];

	int nread = read(mesim_local->fd, buf, sizeof(buf));
	if (nread < 0) {
		ast_log(LOG_WARNING,
			"Error reading from local SIM: %s\n",
			strerror(errno));
	}

	char *hextext = alloca(nread * 2 + 1);

	int i;
	for(i=0; i<nread; i++)
		sprintf(&hextext[i*2], "%02X", buf[i]);

	vgsm_mesim_debug(mesim,
			"Local SIM DUMP (%2d): %s\n",
			nread, hextext);

	return 0;
}

static int vgsm_mesim_local_receive(struct vgsm_mesim_driver *driver)
{
	struct vgsm_mesim_local *mesim_local =
			container_of(driver, struct vgsm_mesim_local, driver);

	if (mesim_local->type == VGSM_MESIM_LOCAL_TYPE_VGSM_DIRECT)
		return vgsm_mesim_local_receive_and_dump(mesim_local);
	else if (mesim_local->state == VGSM_MESIM_LOCAL_STATE_READING_ATR)
		return vgsm_mesim_local_receive_atr(mesim_local);
	else if (mesim_local->state == VGSM_MESIM_LOCAL_STATE_READY)
		return vgsm_mesim_local_receive_data(mesim_local);
	else
		return vgsm_mesim_local_receive_and_dump(mesim_local);

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

	ast_cli(fd, "  SIM state: %s\n",
		vgsm_mesim_local_state_to_text(mesim_local->state));

	ast_cli(fd, "  SIM type: %s\n",
		vgsm_mesim_local_type_to_text(mesim_local->type));

	if (mesim_local->type == VGSM_MESIM_LOCAL_TYPE_VGSM_DIRECT) {
		ast_cli(fd, "  Directly routed to local SIM holder: %d\n",
			mesim_local->sim_id);
	} else if (mesim_local->type == VGSM_MESIM_LOCAL_TYPE_VGSM) {
		ast_cli(fd, "  Connected to vGSM-II SIM holder\n");
	} else if (mesim_local->type == VGSM_MESIM_LOCAL_TYPE_USB) {
		ast_cli(fd, "  Connected to USB reader\n");
	}
}

static struct vgsm_mesim_driver vgsm_mesim_driver_local =
{
	.release		= vgsm_mesim_local_release,
	.deactivate		= vgsm_mesim_local_deactivate,
	.activate		= vgsm_mesim_local_activate,
	.set_mode		= vgsm_mesim_local_set_mode,
	.reset_asserted		= vgsm_mesim_local_reset_asserted,
	.reset_removed 		= vgsm_mesim_local_reset_removed,
	.holder_inserted	= vgsm_mesim_local_holder_inserted,
	.holder_removed		= vgsm_mesim_local_holder_removed,
	.send			= vgsm_mesim_local_send,
	.receive		= vgsm_mesim_local_receive,
	.get_polls		= vgsm_mesim_local_get_polls,
	.cli_show		= vgsm_mesim_local_cli_show,
};
