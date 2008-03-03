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

#ifndef _VGSM_MESIM_H
#define _VGSM_MESIM_H

#include <linux/serial.h>

#include <asterisk/lock.h>

#include <list.h>

#include "timer.h"
#include "util.h"

#ifdef DEBUG_CODE
#define vgsm_mesim_debug(mesim, format, arg...)	\
	if ((mesim)->debug)				\
		vgsm_debug("mesim %s: "			\
			format,				\
			(mesim)->name,			\
			## arg)
#else
#define vgsm_mesim_debug(mesim, format, arg...)	\
	do {} while(0);
#endif

enum vgsm_mesim_state
{
	VGSM_MESIM_CLOSED,
	VGSM_MESIM_CLOSING,
	VGSM_MESIM_UNCONFIGURED,
	VGSM_MESIM_SETTING_MODE,
	VGSM_MESIM_DIRECTLY_ROUTED,
	VGSM_MESIM_HOLDER_REMOVED,
	VGSM_MESIM_HOLDER_CHANGING,
	VGSM_MESIM_SIM_MISSING,
	VGSM_MESIM_READY,
	VGSM_MESIM_RESET,
};

enum vgsm_mesim_message_type
{
	VGSM_MESIM_MSG_CLOSE,
	VGSM_MESIM_MSG_REFRESH,
	VGSM_MESIM_MSG_RESET_ASSERTED,
	VGSM_MESIM_MSG_RESET_REMOVED,
	VGSM_MESIM_MSG_HOLDER_REMOVED,
	VGSM_MESIM_MSG_HOLDER_INSERTED,
	VGSM_MESIM_MSG_SET_MODE,
	VGSM_MESIM_MSG_ME_POWERING_ON,
	VGSM_MESIM_MSG_ME_POWERED_ON,
};

struct vgsm_mesim_message
{
	enum vgsm_mesim_message_type type;

	int len;
	__u8 data[];
};

enum vgsm_mesim_driver_type
{
        VGSM_MESIM_DRIVER_NONE,
        VGSM_MESIM_DRIVER_LOCAL,
        VGSM_MESIM_DRIVER_CLIENT,
        VGSM_MESIM_DRIVER_IMPLEMENTA,
};

const char *vgsm_mesim_message_type_to_text(
	enum vgsm_mesim_message_type mt);

struct vgsm_mesim_set_mode
{
	enum vgsm_mesim_driver_type driver_type;

	char device_filename[PATH_MAX];
	struct sockaddr bind_addr;
	struct sockaddr client_addr;
};

struct vgsm_mesim_driver
{
	void (*release)(struct vgsm_mesim_driver *driver);
	void (*deactivate)(struct vgsm_mesim_driver *driver);
	void (*activate)(struct vgsm_mesim_driver *driver);
	void (*set_mode)(struct vgsm_mesim_driver *driver,
				struct vgsm_mesim_message *msg);
	void (*reset_asserted)(struct vgsm_mesim_driver *driver);
	void (*reset_removed)(struct vgsm_mesim_driver *driver);
	void (*holder_removed)(struct vgsm_mesim_driver *driver);
	void (*holder_inserted)(struct vgsm_mesim_driver *driver);
	int (*send)(struct vgsm_mesim_driver *driver, void *buf, int len);
	int (*receive)(struct vgsm_mesim_driver *driver);
	int (*get_polls)(struct vgsm_mesim_driver *driver,
						struct pollfd *polls);
	void (*cli_show)(struct vgsm_mesim_driver *driver, int fd);
};

struct vgsm_me;

struct vgsm_mesim
{
	int fd;

	const char *name;

	enum vgsm_mesim_state state;
	ast_mutex_t state_lock;
	ast_cond_t state_cond;

	BOOL insert_override;

	struct vgsm_me *me;

	struct vgsm_timerset timerset;
	struct vgsm_timer timer;

	int cmd_pipe_read;
	int cmd_pipe_write;

	pthread_t comm_thread;
	pthread_t modem_thread;
	BOOL modem_thread_has_to_exit;

	BOOL is_reset;

	BOOL vcc;
	BOOL rst;

	struct serial_icounter_struct icount;

	BOOL debug;

        enum vgsm_mesim_driver_type driver_type;
	struct vgsm_mesim_driver *driver;
};

const char *vgsm_mesim_state_to_text(
	enum vgsm_mesim_state state);

int vgsm_mesim_create(
	struct vgsm_mesim *mesim,
	struct vgsm_me *me,
	const char *name);
void vgsm_mesim_destroy(struct vgsm_mesim *mesim);

struct vgsm_mesim *vgsm_mesim_get(
	struct vgsm_mesim *mesim);
void _vgsm_mesim_put(struct vgsm_mesim *mesim);
#define vgsm_mesim_put(sim) \
	do { _vgsm_mesim_put(sim); (sim) = NULL; } while(0)

int vgsm_mesim_open(struct vgsm_mesim *mesim, const char *devname);
void vgsm_mesim_close(struct vgsm_mesim *mesim);

void vgsm_mesim_get_ready_for_poweron(struct vgsm_mesim *mesim);

void vgsm_mesim_send_message(
	struct vgsm_mesim *mesim,
	enum vgsm_mesim_message_type mt,
	void *data, int len);

#ifdef VGSM_MESIM_DRIVER_PRIVATE

void vgsm_mesim_change_state(
	struct vgsm_mesim *mesim,
	enum vgsm_mesim_state newstate,
	longtime_t timeout);

void vgsm_mesim_set_inserted(struct vgsm_mesim *mesim);
void vgsm_mesim_set_removed(struct vgsm_mesim *mesim);

int vgsm_mesim_write(
	struct vgsm_mesim *mesim,
	void *data, int size);

#endif

#endif
