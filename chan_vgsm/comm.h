/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
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

#include "util.h"

#define SEC 1000000LL
#define MILLISEC 1000LL

/* Error codes allocation:
 *
 * 0-999	CME ERROR: n
 * 1000-1999	CMS ERROR: n - 1000
 * 10000-10006	Final messages
 * 11000-11999	Transaction errors
 *
 */

enum vgsm_req_codes
{
	VGSM_RESP_OK		= 10000,
	VGSM_RESP_CONNECT	= 10001,
	VGSM_RESP_NO_CARRIER	= 10002,
	VGSM_RESP_ERROR		= 10003,
	VGSM_RESP_NO_DIALTONE	= 10004,
	VGSM_RESP_BUSY		= 10005,
	VGSM_RESP_NO_ANSWER	= 10006,
	VGSM_RESP_UNKNOWN	= 11000,
	VGSM_RESP_TIMEOUT	= 11001,
	VGSM_RESP_FAILED	= 11002,
};

enum vgsm_comm_state
{
	VGSM_PS_BITBUCKET,
	VGSM_PS_RECOVERING,
	VGSM_PS_IDLE,
	VGSM_PS_READING_URC,
	VGSM_PS_AWAITING_SMS_ECHO,
	VGSM_PS_AWAITING_ECHO,
	VGSM_PS_AWAITING_ECHO_READING_URC,
	VGSM_PS_READING_RESPONSE,
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

	int ready;
	ast_cond_t ready_cond;

	int timeout;

	struct list_head lines;
	int response_error;

	struct vgsm_urc_class *urc_class;
};

struct vgsm_req_line
{
	struct list_head node;

	char text[0];
};

struct vgsm_comm
{
	ast_mutex_t lock;

	const char *name;

	int fd;

	enum vgsm_comm_state state;
	ast_cond_t state_change_cond;

	longtime_t timer_expiration;

	char buf[2048];

	struct list_head requests_queue;

	struct vgsm_req *current_req;
	struct vgsm_req *current_urc;

	struct vgsm_urc_class *urc_classes;
};

void vgsm_comm_init(struct vgsm_comm *comm, struct vgsm_urc_class *urcs);

struct vgsm_req *vgsm_req_make_va(
	struct vgsm_comm *comm,
	int timeout,
	const char *sms_pdu,
	int sms_pdu_len,
	const char *fmt,
	va_list ap);

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

int vgsm_comm_line_error(const char *line);

const struct vgsm_req_line *vgsm_req_first_line(
	const struct vgsm_req *req);
const struct vgsm_req_line *vgsm_req_last_line(
	const struct vgsm_req *req);

void vgsm_req_get(struct vgsm_req *req);
void vgsm_req_put(struct vgsm_req *req);

void vgsm_comm_wakeup(struct vgsm_comm *comm);

void vgsm_comm_set_bitbucket(struct vgsm_comm *comm);
void vgsm_comm_reset(struct vgsm_comm *comm);
int vgsm_comm_start_recovery(struct vgsm_comm *comm);
int vgsm_comm_thread_create();

#endif
