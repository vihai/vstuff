/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2004-2008 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_COMM_H
#define _VGSM_COMM_H

#include <asterisk/lock.h>

#include <list.h>

#include "longtime.h"
#include "timer.h"
#include "util.h"
#include "debug.h"

#ifdef DEBUG_CODE
#define vgsm_comm_debug_messages(comm, format, arg...)		\
	if ((comm)->debug_messages)				\
		vgsm_debug("%s: "				\
			format,					\
			(comm)->name,				\
			## arg)

#define vgsm_comm_debug_characters(comm, format, arg...)	\
	if ((comm)->debug_characters)				\
		vgsm_debug("%s: "				\
			format,					\
			(comm)->name,				\
			## arg)

#define vgsm_comm_debug_timer(comm, format, arg...)		\
	if ((comm)->debug_timer)				\
		vgsm_debug("%s: "				\
			format,					\
			(comm)->name,				\
			## arg)
#else
#define vgsm_comm_debug_messages(comm, format, arg...)		\
	do {} while(0);
#define vgsm_comm_debug_characters(comm, format, arg...)	\
	do {} while(0);
#define vgsm_comm_debug_timer(format, arg...)			\
	do {} while(0);
#endif

/* Error codes allocation:
 *
 *    0 to  999		Final messages
 * 1000 to 1999		CME ERROR: n + 1000
 * 1999 to 2999		CMS ERROR: n + 2000
 *
 */

#define CME_ERROR_BASE	1000
#define CME_ERROR_SIZE	1000
#define CMS_ERROR_BASE	2000
#define CMS_ERROR_SIZE	1000
#define CME_ERROR(x)	((x) + CME_ERROR_BASE)
#define CMS_ERROR(x)	((x) + CMS_ERROR_BASE)

enum vgsm_req_codes
{
	VGSM_RESP_OK		= 0,
	VGSM_RESP_CONNECT	= 1,
	VGSM_RESP_NO_CARRIER	= 2,
	VGSM_RESP_ERROR		= 3,
	VGSM_RESP_NO_DIALTONE	= 4,
	VGSM_RESP_BUSY		= 5,
	VGSM_RESP_NO_ANSWER	= 6,
	VGSM_RESP_UNKNOWN	= 100,
	VGSM_RESP_TIMEOUT	= 101,
	VGSM_RESP_FAILED	= 102,
};

enum vgsm_comm_state
{
	VGSM_COMM_CLOSED,
	VGSM_COMM_CLOSING,
	VGSM_COMM_FAILED,
	VGSM_COMM_RECOVERING,
	VGSM_COMM_IDLE,
	VGSM_COMM_READING_URC,
	VGSM_COMM_AWAITING_SMS_ECHO,
	VGSM_COMM_AWAITING_SMS_ECHO_1A,
	VGSM_COMM_AWAITING_ECHO,
	VGSM_COMM_AWAITING_ECHO_READING_URC,
	VGSM_COMM_READING_RESPONSE,
};

enum vgsm_comm_message_type
{
	VGSM_COMM_MSG_CLOSE,
	VGSM_COMM_MSG_REFRESH,
	VGSM_COMM_MSG_INITIALIZE,
};

struct vgsm_comm_message
{
	enum vgsm_comm_message_type type;

	int len;
	__u8 data[];
};

struct vgsm_req;
struct vgsm_urc_class
{
	const char *code;

	void (*handler)(const struct vgsm_req *urm);
	int (*detect_end)(const struct vgsm_req *urm);
};

struct vgsm_comm;
struct vgsm_req
{
	struct list_head node;

	int refcnt;

	struct vgsm_comm *comm;

	char request[82];
	char *sms_text_pdu;

	int retransmit_cnt;

	ast_mutex_t ready_lock;
	int ready;
	ast_cond_t ready_cond;

	void (*completion_func)(struct vgsm_req *req, void *data);
	void *completion_data;

	int timeout;

	struct list_head lines;
	int err;

	struct vgsm_urc_class *urc_class;
};

struct vgsm_req_line
{
	struct list_head node;

	char text[0];
};

struct vgsm_comm
{
	const char *name;

	int fd;

	enum vgsm_comm_state state;
	ast_mutex_t state_lock;
	ast_cond_t state_cond;

	struct vgsm_timerset timerset;
	struct vgsm_timer timer;

	char buf[2048];

	pthread_t comm_thread;
	ast_mutex_t requests_queue_lock;
	struct list_head requests_queue;

	int cmd_pipe_read;
	int cmd_pipe_write;

	struct vgsm_req *current_req;
	struct vgsm_req *current_urc;

	pthread_t urc_thread;
	struct list_head urc_queue;
	ast_mutex_t urc_queue_lock;
	ast_cond_t urc_queue_cond;
	BOOL urc_thread_has_to_exit;

	pthread_t completion_thread;
	struct list_head completion_queue;
	ast_mutex_t completion_queue_lock;
	ast_cond_t completion_queue_cond;
	BOOL completion_thread_has_to_exit;

	struct vgsm_urc_class *urc_classes;

	BOOL debug_messages;
	BOOL debug_characters;
	BOOL debug_timer;
};

int vgsm_comm_init(struct vgsm_comm *comm, struct vgsm_urc_class *urcs);
void vgsm_comm_destroy(struct vgsm_comm *comm);

struct vgsm_req *vgsm_req_make_va(
	struct vgsm_comm *comm,
	int timeout,
	const char *sms_pdu,
	int sms_pdu_len,
	void (*completion_func)(struct vgsm_req *req, void *data),
	void *completion_data,
	const char *fmt,
	va_list ap);

struct vgsm_req *vgsm_req_make_callback(
	struct vgsm_comm *comm,
	void (*completion_func)(struct vgsm_req *req, void *data),
	void *completion_data,
	int timeout,
	const char *fmt, ...)
	__attribute__ ((format (printf, 5, 6)));

struct vgsm_req *vgsm_req_make(
	struct vgsm_comm *comm,
	int timeout,
	const char *fmt, ...)
	__attribute__ ((format (printf, 3, 4)));

struct vgsm_req *vgsm_req_make_sms(
	struct vgsm_comm *comm,
	int timeout,
	const char *sms_pdu,
	int sms_pdu_len,
	const char *fmt, ...)
	__attribute__ ((format (printf, 5, 6)));

struct vgsm_req *vgsm_req_make_wait(
	struct vgsm_comm *comm,
	int timeout,
	const char *fmt, ...)
	__attribute__ ((format (printf, 3, 4)));

int vgsm_req_make_wait_result(
	struct vgsm_comm *comm,
	int timeout,
	const char *fmt, ...)
	__attribute__ ((format (printf, 3, 4)));

void vgsm_req_wait(struct vgsm_req *req);

int vgsm_req_final_response_code(const char *line);

const struct vgsm_req_line *vgsm_req_first_line(
	const struct vgsm_req *req);
const struct vgsm_req_line *vgsm_req_last_line(
	const struct vgsm_req *req);

struct vgsm_req *vgsm_req_get(struct vgsm_req *req);
void _vgsm_req_put(struct vgsm_req *req);
#define vgsm_req_put(req) \
	do { _vgsm_req_put(req); (req) = NULL; } while(0)

int vgsm_req_status(struct vgsm_req *req);

int vgsm_comm_open(struct vgsm_comm *comm, int fd);
void vgsm_comm_close(struct vgsm_comm *comm);

void vgsm_comm_send_message(
	struct vgsm_comm *comm,
	enum vgsm_comm_message_type mt,
	void *data, int len);

#endif
