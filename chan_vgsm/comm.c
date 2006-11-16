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

#include "../config.h"

#include <asterisk/lock.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>

#include <linux/vgsm.h> // REMOVE ME FIXME XXX

#include "chan_vgsm.h"
#include "util.h"
#include "comm.h"

#define ECHO_TIMEOUT (400 * MILLISEC)
#define URC_TIMEOUT (500 * MILLISEC)
#define SMS_ECHO_TIMEOUT (1 * SEC)
#define READING_URC_TIMEOUT (3 * SEC)

static pthread_t vgsm_comm_thread = AST_PTHREADT_NULL;
static pthread_t vgsm_comm_urc_thread = AST_PTHREADT_NULL;
static pthread_t vgsm_comm_completion_thread = AST_PTHREADT_NULL;

static struct list_head vgsm_comm_urc_queue =
			LIST_HEAD_INIT(vgsm_comm_urc_queue);
AST_MUTEX_DEFINE_STATIC(vgsm_comm_urc_queue_lock);
ast_cond_t vgsm_comm_urc_queue_cond;

static struct list_head vgsm_comm_completion_queue =
			LIST_HEAD_INIT(vgsm_comm_completion_queue);
AST_MUTEX_DEFINE_STATIC(vgsm_comm_completion_queue_lock);
ast_cond_t vgsm_comm_completion_queue_cond;

void vgsm_comm_init(
	struct vgsm_comm *comm,
	struct vgsm_urc_class *urc_classes)
{
	comm->fd = -1;
	comm->urc_classes = urc_classes;
	comm->state = VGSM_PS_CLOSED;
	comm->timer_expiration = -1;

	ast_mutex_init(&comm->state_lock);

	ast_cond_init(&comm->close_cond, NULL);

	INIT_LIST_HEAD(&comm->requests_queue);
}

struct vgsm_req *vgsm_req_get(struct vgsm_req *req)
{
	if (!req)
		return NULL;

	assert(req->refcnt > 0);
	assert(req->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	req->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return req;
}

void _vgsm_req_put(struct vgsm_req *req)
{
	if (!req)
		return;

	assert(req->refcnt > 0);
	assert(req->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	int refcnt = --req->refcnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!refcnt) {
		struct vgsm_req_line *line, *n;

		list_for_each_entry_safe(line, n, &req->lines, node) {
			list_del(&line->node);
			free(line);
		}

		if (req->sms_text_pdu) {
			free(req->sms_text_pdu);
			req->sms_text_pdu = NULL;
		}

		free(req);
	}
}

int vgsm_req_status(struct vgsm_req *req)
{
	if (!req)
		return VGSM_RESP_FAILED;

	assert(req->ready);

	return req->err;
}

static const char *vgsm_comm_state_to_text(
	enum vgsm_comm_state state)
{
	switch(state) {
	case VGSM_PS_CLOSED:
		return "CLOSED";
	case VGSM_PS_FAILED:
		return "FAILED";
	case VGSM_PS_RECOVERING:
		return "RECOVERING";
	case VGSM_PS_IDLE:
		return "IDLE";
	case VGSM_PS_READING_URC:
		return "READING_URC";
	case VGSM_PS_AWAITING_SMS_ECHO:
		return "AWAITING_SMS_ECHO";
	case VGSM_PS_AWAITING_SMS_ECHO_1A:
		return "AWAITING_SMS_ECHO_1A";
	case VGSM_PS_AWAITING_ECHO:
		return "AWAITING_ECHO";
	case VGSM_PS_AWAITING_ECHO_READING_URC:
		return "AWAITING_ECHO_READING_URC";
	case VGSM_PS_READING_RESPONSE:
		return "READING_RESPONSE";
	}

	return "*UNKNOWN*";
}

static void vgsm_parser_change_state(
	struct vgsm_comm *comm,
	enum vgsm_comm_state newstate,
	longtime_t timeout)
{
	char tmpstr[16] = "";

	if (timeout == -1) {
		strcpy(tmpstr, ", no timeout");

		comm->timer_expiration = -1;
	} else if (timeout != -2) {
		snprintf(tmpstr, sizeof(tmpstr),
			", timeout = %lldms",
			timeout / 1000);

		comm->timer_expiration = longtime_now() + timeout;
	}

	vgsm_comm_debug_characters(comm,
		"State change from %s to %s%s\n",
		vgsm_comm_state_to_text(comm->state),
		vgsm_comm_state_to_text(newstate),
		tmpstr);

	ast_mutex_lock(&comm->state_lock);
	comm->state = newstate;
	ast_mutex_unlock(&comm->state_lock);
}

void vgsm_comm_wakeup(struct vgsm_comm *comm)
{
	pthread_kill(vgsm_comm_thread, SIGURG);
}

BOOL comm_refresh_pending;

void vgsm_comm_refresh(struct vgsm_comm *comm)
{
	comm_refresh_pending = TRUE;
	vgsm_comm_wakeup(comm);
}

void vgsm_comm_close(struct vgsm_comm *comm)
{
	comm->enabled = FALSE;
	vgsm_comm_refresh(comm);

	ast_mutex_lock(&comm->state_lock);
	while(comm->state != VGSM_PS_CLOSED) {
		ast_cond_wait(&comm->close_cond, &comm->state_lock);
	}
	ast_mutex_unlock(&comm->state_lock);
}

void vgsm_comm_open(struct vgsm_comm *comm, int fd)
{
	assert(comm->state == VGSM_PS_CLOSED);

	comm->fd = fd;
	comm->enabled = TRUE;
	vgsm_comm_refresh(comm);
}

int vgsm_comm_send_recovery_sequence(struct vgsm_comm *comm)
{
/*
	sleep(1);

	if (write(comm->fd, "+", 1) < 0) {
		ast_log(LOG_WARNING,
			"%s: write to module failed: %s\n",
			comm->name,
			strerror(errno));

		return -1;
	}

	usleep(100000);

	if (write(comm->fd, "+", 1) < 0) {
		ast_log(LOG_WARNING,
			"%s: write to module failed: %s\n",
			comm->name,
			strerror(errno));

		return -1;
	}

	usleep(100000);

	if (write(comm->fd, "+", 1) < 0) {
		ast_log(LOG_WARNING,
			"%s: write to module failed: %s\n",
			comm->name,
			strerror(errno));

		return -1;
	}

	sleep(1);
*/
	const char *cmd = "AT E1 V1 Q0 \\Q1\r";
	if (write(comm->fd, cmd, strlen(cmd)) < 0) {
		ast_log(LOG_WARNING,
			"%s: write to module failed: %s\n",
			comm->name,
			strerror(errno));

		return -1;
	}

	return 0;
}

const struct vgsm_req_line *vgsm_req_first_line(
	const struct vgsm_req *req)
{
	return list_entry(req->lines.next, struct vgsm_req_line, node);
}

const struct vgsm_req_line *vgsm_req_last_line(
	const struct vgsm_req *req)
{
	return list_entry(req->lines.prev, struct vgsm_req_line, node);
}

static struct vgsm_req *vgsm_req_alloc(struct vgsm_comm *comm)
{
	struct vgsm_req *req;

	assert(comm);

	req = malloc(sizeof(*req));
	if (!req)
		return NULL;

	memset(req, 0, sizeof(*req));

	req->refcnt = 1;

	INIT_LIST_HEAD(&req->lines);

	ast_mutex_init(&req->ready_lock);
	req->ready = FALSE;
	ast_cond_init(&req->ready_cond, NULL);

	req->comm = comm;

	return req;
}


void vgsm_req_wait(struct vgsm_req *req)
{
	if (!req)
		return;

	ast_mutex_lock(&req->ready_lock);
	while(!req->ready) {
		ast_cond_wait(&req->ready_cond, &req->ready_lock);
	}
	ast_mutex_unlock(&req->ready_lock);
}

struct vgsm_req *vgsm_req_make_va(
	struct vgsm_comm *comm,
	int timeout,
	const char *sms_pdu,
	int sms_pdu_len,
	void (*completion_func)(struct vgsm_req *req, void *data),
	void *completion_data,
	const char *fmt,
	va_list ap)
{
	struct vgsm_req *req;

	req = vgsm_req_alloc(comm);
	if (!req)
		return NULL;

	if (vsnprintf(req->request, sizeof(req->request), fmt, ap) >=
						sizeof(req->request) - 2)
		return NULL;

	strcat(req->request, "\r");

	req->ready = FALSE;
	req->timeout = timeout + 100 * MILLISEC;
	req->retransmit_cnt = 3;
	req->completion_func = completion_func;
	req->completion_data = completion_data;

	if (sms_pdu && sms_pdu_len) {
		req->sms_text_pdu = malloc((sms_pdu_len * 2) + 2);
		if (!req->sms_text_pdu) {
			vgsm_req_put(req);
			return NULL;
		}

		int i;
		for (i=0; i < sms_pdu_len; i++) {
			sprintf(req->sms_text_pdu + i * 2, "%02x",
				*((__u8 *)sms_pdu + i));
		}

//		strcat(req->sms_text_pdu, "\x1a");
	}

	ast_mutex_lock(&comm->requests_queue_lock);
	list_add_tail(&vgsm_req_get(req)->node, &comm->requests_queue);
	ast_mutex_unlock(&comm->requests_queue_lock);

	vgsm_comm_wakeup(comm);

	return req;
}

struct vgsm_req *vgsm_req_make_callback(
	struct vgsm_comm *comm,
	void (*completion_func)(struct vgsm_req *req, void *data),
	void *completion_data,
	int timeout,
	const char *fmt, ...)
{
	va_list ap;
	struct vgsm_req *req;

	va_start(ap, fmt);
	req = vgsm_req_make_va(comm, timeout, NULL, 0,
				completion_func, completion_data, fmt, ap);
	va_end(ap);

	return req;
}

struct vgsm_req *vgsm_req_make(
	struct vgsm_comm *comm,
	int timeout,
	const char *fmt, ...)
{
	va_list ap;
	struct vgsm_req *req;

	va_start(ap, fmt);
	req = vgsm_req_make_va(comm, timeout, NULL, 0, NULL, NULL, fmt, ap);
	va_end(ap);

	return req;
}

struct vgsm_req *vgsm_req_make_sms(
	struct vgsm_comm *comm,
	int timeout,
	const char *sms_pdu,
	int sms_pdu_len,
	const char *fmt, ...)
{
	va_list ap;
	struct vgsm_req *req;

	va_start(ap, fmt);
	req = vgsm_req_make_va(comm, timeout, sms_pdu,
				sms_pdu_len, NULL, NULL, fmt, ap);
	va_end(ap);

	return req;
}

struct vgsm_req *vgsm_req_make_wait(
	struct vgsm_comm *comm,
	int timeout,
	const char *fmt, ...)
{
	va_list ap;
	struct vgsm_req *req;

	va_start(ap, fmt);
	req = vgsm_req_make_va(comm, timeout, NULL, 0, NULL, NULL, fmt, ap);
	va_end(ap);

	if (!req)
		return NULL;

	vgsm_req_wait(req);

	return req;
}

int vgsm_req_make_wait_result(
	struct vgsm_comm *comm,
	int timeout,
	const char *fmt, ...)
{
	va_list ap;
	struct vgsm_req *req;
	int res;

	va_start(ap, fmt);
	req = vgsm_req_make_va(comm, timeout, NULL, 0, NULL, NULL, fmt, ap);
	va_end(ap);

	if (!req)
		return VGSM_RESP_FAILED;

	vgsm_req_wait(req);

	res = req->err;

	vgsm_req_put(req);

	return res;
}

int vgsm_req_final_response_code(const char *line)
{
	if (!strcmp(line, "OK"))
		return VGSM_RESP_OK;
	else if (!strcmp(line, "CONNECT"))
		return VGSM_RESP_CONNECT;
	else if (!strcmp(line, "NO CARRIER"))
		return VGSM_RESP_NO_CARRIER;
	else if (!strcmp(line, "ERROR"))
		return VGSM_RESP_ERROR;
	else if (!strcmp(line, "NO DIALTONE"))
		return VGSM_RESP_NO_DIALTONE;
	else if (!strcmp(line, "BUSY"))
		return VGSM_RESP_BUSY;
	else if (!strcmp(line, "NO ANSWER"))
		return VGSM_RESP_NO_ANSWER;
	else if (strstr(line, "+CME ERROR: ") == line)
		return atoi(line + strlen("+CME ERROR: ")) + CME_ERROR_BASE;
	else if (strstr(line, "+CMS ERROR: ") == line)
		return atoi(line + strlen("+CMS ERROR: ")) + CMS_ERROR_BASE;
	else
		return VGSM_RESP_UNKNOWN;
}

static int vgsm_match_response(
	struct vgsm_comm *comm)
{
	assert(comm->current_req);

	char *begin = comm->buf;

	if (*begin == '\0')
		return 0;

	if (*begin == '\r')
		begin++;

	if (*begin == '\0')
		return 0;

	if (*begin == '\n')
		begin++;

	if (!strncmp(begin, "> ", 2)) {
		struct vgsm_req *req = comm->current_req;

		if (!req->sms_text_pdu) {
			ast_log(LOG_WARNING,
				"%s responded with '>' prompt but no "
				"outstanding PDU is present\n", comm->name);
			write(comm->fd, "\x1b", 1);

			return 4;
		}

		write(comm->fd, req->sms_text_pdu, strlen(req->sms_text_pdu));

		char tmpstr[360];
		vgsm_comm_debug_messages(comm,
			"TX: '%s'\n",
			unprintable_escape(req->sms_text_pdu,
						tmpstr, sizeof(tmpstr)));

		vgsm_parser_change_state(comm, VGSM_PS_AWAITING_SMS_ECHO,
					SMS_ECHO_TIMEOUT);

		return 4;
	}

	char *end = strstr(comm->buf + 2, "\r\n");
	if (!end)
		return 0;

	*end = '\0';

	char tmpstr[200];
	vgsm_comm_debug_messages(comm,
		"RX: '%s<cr><lf>'\n",
		unprintable_escape(comm->buf, tmpstr, sizeof(tmpstr)));

	struct vgsm_req_line *req_line;
	req_line = malloc(sizeof(struct vgsm_req_line) +
			 strlen(begin) + 1);

	strcpy(req_line->text, begin);

	list_add_tail(&req_line->node, &comm->current_req->lines);

	int err = vgsm_req_final_response_code(begin);
	if (err != VGSM_RESP_UNKNOWN) {
		vgsm_parser_change_state(comm, VGSM_PS_IDLE, -1);

		comm->current_req->err = err;

		ast_mutex_lock(&comm->current_req->ready_lock);
		comm->current_req->ready = TRUE;
		ast_mutex_unlock(&comm->current_req->ready_lock);

		ast_mutex_lock(&vgsm_comm_completion_queue_lock);
		list_add_tail(&vgsm_req_get(comm->current_req)->node,
				&vgsm_comm_completion_queue);
		ast_mutex_unlock(&vgsm_comm_completion_queue_lock);
		ast_cond_broadcast(&vgsm_comm_completion_queue_cond);

		ast_cond_broadcast(&comm->current_req->ready_cond);
		vgsm_req_put(comm->current_req);
	}

	return end - comm->buf + 2;
}

static int match_echo(struct vgsm_comm *comm, const char *sent)
{
	int n = 0;
	const char *buf = comm->buf;
	const char *snt = sent;

//printf("Match1 = '%s'\n", r);
//printf("Match2 = '%s'\n", b);

	while(*buf && *snt && *buf == *snt) {
		buf++;
		snt++;
		n++;
	}

	if (!*snt)
		return n;	// Matches fully
	else if (!*buf)
		return 0;	// May match
	else
		return -1;	// Cannot match
}

static int vgsm_match_urc(struct vgsm_comm *comm)
{
	char *begin = comm->buf;

	if (*begin == '\0')
		return 0;

	if (*begin != '\r') {

		/* Siemens MC55 send a second CUSD without <cr><lf> */
		if (*begin == '+')
			goto cusd_workaround;

		/* ^SYSSTART is not prepended by <cr><lf> */
		if (*begin == '^')
			goto sysstart_workaround;

		ast_log(LOG_WARNING, "%s: Unexpected char 0x%02x\n",
			comm->name,
			*begin);
		return 1;
	}

	begin++;

	if (*begin == '\0')
		return 0;

	if (*begin != '\n') {
		ast_log(LOG_WARNING, "%s: Unexpected char 0x%02x after <cr>\n",
			comm->name,
			*begin);
		return 1;
	}

	begin++;

	if (*begin == '\0')
		return 0;

	if (*begin == '\r') {
		begin++;

		if (*begin == '\0')
			return 0;

		if (*begin != '\n') {
			ast_log(LOG_WARNING,
				"%s: Unexpected char 0x%02x after <cr>\n",
				comm->name,
				*begin);
			return 1;
		}

		begin++;
	}

sysstart_workaround:;
cusd_workaround:;

	char *end = strstr(begin, "\r\n");
	if (!end)
		return 0;

	*end = '\0';

	assert(!comm->current_urc);

	vgsm_comm_debug_messages(comm,
		"RX URC: '%s'\n",
		begin);

	int i;
	struct vgsm_req *urc = NULL;
	for (i=0; comm->urc_classes[i].code; i++) {

		struct vgsm_urc_class *cls = &comm->urc_classes[i];

		if (!strncmp(begin, cls->code, strlen(cls->code))) {
			urc = vgsm_req_alloc(comm);
			urc->urc_class = cls;

			break;
		}
	}

	if (!urc) {
		ast_log(LOG_WARNING,
			"%s: Unhandled URC '%s'\n",
			comm->name,
			begin);
		return end - comm->buf + 2;
	}

	struct vgsm_req_line *req_line;
	req_line = malloc(sizeof(struct vgsm_req_line) +
			 strlen(begin) + 1);

	strcpy(req_line->text, begin);

	list_add_tail(&req_line->node, &urc->lines);

	if (comm->state == VGSM_PS_AWAITING_ECHO)
		vgsm_parser_change_state(comm, VGSM_PS_AWAITING_ECHO,
					ECHO_TIMEOUT);

	if (urc->urc_class->detect_end) {
		comm->current_urc = urc;

		if (comm->state == VGSM_PS_AWAITING_ECHO)
			vgsm_parser_change_state(comm,
				VGSM_PS_AWAITING_ECHO_READING_URC,
				ECHO_TIMEOUT);
		else
			vgsm_parser_change_state(comm,
				VGSM_PS_READING_URC,
				READING_URC_TIMEOUT);

		comm->timer_expiration = longtime_now() + URC_TIMEOUT;
	} else {
		ast_mutex_lock(&vgsm_comm_urc_queue_lock);
		list_add_tail(&urc->node, &vgsm_comm_urc_queue);
		ast_mutex_unlock(&vgsm_comm_urc_queue_lock);
		ast_cond_broadcast(&vgsm_comm_urc_queue_cond);
	}

	return end - comm->buf + 2;
}

static int vgsm_match_urc_cont(struct vgsm_comm *comm)
{
	char *begin = comm->buf;
	char *end = strstr(begin, "\r\n");
	if (!end)
		return 0;

	if (end == begin) {
		end = strstr(begin, "\r\n");
		if (!end)
			return 0;
	}

	*end = '\0';

	assert(comm->current_urc);

	struct vgsm_req_line *req_line;
	req_line = malloc(sizeof(struct vgsm_req_line) +
			 strlen(begin) + 1);

	strcpy(req_line->text, begin);

	list_add_tail(&req_line->node, &comm->current_urc->lines);

	if (comm->state == VGSM_PS_AWAITING_ECHO_READING_URC)
		comm->timer_expiration = longtime_now() + ECHO_TIMEOUT;

	assert(comm->current_urc->urc_class->detect_end);

	if (comm->current_urc->urc_class->detect_end(comm->current_urc)) {
		ast_mutex_lock(&vgsm_comm_urc_queue_lock);
		list_add_tail(&comm->current_urc->node, &vgsm_comm_urc_queue);
		ast_mutex_unlock(&vgsm_comm_urc_queue_lock);
		ast_cond_broadcast(&vgsm_comm_urc_queue_cond);

		comm->current_urc = NULL;

		if (comm->state == VGSM_PS_AWAITING_ECHO_READING_URC) {
			vgsm_parser_change_state(comm, VGSM_PS_AWAITING_ECHO,
						ECHO_TIMEOUT);
		} else {
			vgsm_parser_change_state(comm, VGSM_PS_IDLE, -1);
		}
	}

	return end - comm->buf + 2;
}

static int vgsm_receive(struct vgsm_comm *comm)
{
	int buflen = strlen(comm->buf);
	int nread;

	nread = read(comm->fd, comm->buf + buflen,
			sizeof(comm->buf) - buflen - 1);
	if (nread < 0) {
		ast_log(LOG_WARNING, "%s: Error reading from serial: %s\n",
			comm->name,
			strerror(errno));

		return -1;
	}

	comm->buf[buflen + nread] = '\0';

	{
	char tmpstr[200];
	vgsm_comm_debug_characters(comm,
		"read()='%s'\n",
		unprintable_escape(comm->buf + buflen, tmpstr, sizeof(tmpstr)));
	}

	if (strchr(comm->buf, 0x11))
		ast_log(LOG_ERROR, "%s: XON?\n",
			comm->name);

	if (strchr(comm->buf, 0x13))
		ast_log(LOG_ERROR, "%s: XOFF?\n",
			comm->name);

	while(1) {
		int npull = 0;

		{
		char tmpstr[200];
		vgsm_comm_debug_characters(comm,
			"BUF='%s'\n",
			unprintable_escape(comm->buf, tmpstr, sizeof(tmpstr)));
		}

		switch(comm->state) {
		case VGSM_PS_CLOSED:
			ast_log(LOG_ERROR, "%s: Unexpected vgsm_receive()"
				" in CLOSED state\n", comm->name);
		break;

		case VGSM_PS_FAILED:
		case VGSM_PS_RECOVERING:
			/* Throw away everything */
			npull = nread;
		break;

		case VGSM_PS_IDLE:
			npull = vgsm_match_urc(comm);
		break;

		case VGSM_PS_AWAITING_ECHO: {
			int nmatch = match_echo(comm,
					comm->current_req->request);
			if (nmatch > 0) {
				vgsm_parser_change_state(comm,
					VGSM_PS_READING_RESPONSE,
					comm->current_req->timeout);

				npull = nmatch;
			} else if (nmatch != 0) {
				npull = vgsm_match_urc(comm);
			}
		}
		break;

		case VGSM_PS_READING_URC:
		case VGSM_PS_AWAITING_ECHO_READING_URC:
			npull = vgsm_match_urc_cont(comm);
		break;

		case VGSM_PS_AWAITING_SMS_ECHO: {
			int nmatch = match_echo(comm,
					comm->current_req->sms_text_pdu);
			if (nmatch > 0) {
				write(comm->fd, "\x1a", 1);

				vgsm_parser_change_state(comm,
					VGSM_PS_AWAITING_SMS_ECHO_1A,
					-2);

				npull = nmatch;
			}
		}
		break;

		case VGSM_PS_AWAITING_SMS_ECHO_1A: {
			int nmatch = match_echo(comm, "\x1a");
			if (nmatch > 0) {
				vgsm_parser_change_state(comm,
					VGSM_PS_READING_RESPONSE,
					comm->current_req->timeout);

				npull = nmatch;
			}
		}
		break;

		case VGSM_PS_READING_RESPONSE:
			npull = vgsm_match_response(comm);
		break;
		}

		nread = 0;

		if (npull)
			memmove(comm->buf, comm->buf + npull,
				strlen(comm->buf + npull) + 1);
		else
			break;
	}

	return 0;
}

static struct vgsm_req *vgsm_comm_dequeue_req(struct vgsm_comm *comm)
{
	struct vgsm_req *req;

	ast_mutex_lock(&comm->requests_queue_lock);
	if (list_empty(&comm->requests_queue)) {
		ast_mutex_unlock(&comm->requests_queue_lock);
		return NULL;
	}

	req = list_entry(comm->requests_queue.next, struct vgsm_req, node);

	list_del(&req->node);

	ast_mutex_unlock(&comm->requests_queue_lock);

	return req;
}

static void vgsm_process_next_request(struct vgsm_comm *comm)
{
	assert(!comm->current_req);

	comm->current_req = vgsm_comm_dequeue_req(comm);
	if (!comm->current_req)
		return;

	char tmpstr[200];
	vgsm_comm_debug_messages(comm,
		"TX: '%s'\n",
		unprintable_escape(comm->current_req->request,
					tmpstr, sizeof(tmpstr)));

	write(comm->fd, comm->current_req->request,
		strlen(comm->current_req->request));

	vgsm_parser_change_state(comm, VGSM_PS_AWAITING_ECHO, ECHO_TIMEOUT);
}

static void vgsm_flush_requests(struct vgsm_comm *comm)
{
	struct vgsm_req *req;

	while ((req = vgsm_comm_dequeue_req(comm))) {

		req->err = VGSM_RESP_FAILED;

		ast_mutex_lock(&req->ready_lock);
		req->ready = TRUE;
		ast_mutex_unlock(&req->ready_lock);

		ast_mutex_lock(&vgsm_comm_completion_queue_lock);
		list_add_tail(&vgsm_req_get(req)->node,
				&vgsm_comm_completion_queue);
		ast_mutex_unlock(&vgsm_comm_completion_queue_lock);
		ast_cond_broadcast(&vgsm_comm_completion_queue_cond);

		ast_cond_broadcast(&req->ready_cond);
		vgsm_req_put(req);
	}
}

static void vgsm_comm_timed_out(struct vgsm_comm *comm)
{
	switch(comm->state) {
	case VGSM_PS_RECOVERING:
		if (comm->current_req) {
			comm->current_req->retransmit_cnt--;

			if (comm->current_req->retransmit_cnt == 0) {
				vgsm_parser_change_state(comm,
					VGSM_PS_FAILED, -1);

				comm->current_req->err = VGSM_RESP_FAILED;

				ast_mutex_lock(&comm->current_req->ready_lock);
				comm->current_req->ready = TRUE;
				ast_mutex_unlock(&comm->current_req->
								ready_lock);

				ast_mutex_lock(
					&vgsm_comm_completion_queue_lock);
				list_add_tail(
					&vgsm_req_get(comm->current_req)->node,
					&vgsm_comm_completion_queue);
				ast_mutex_unlock(
					&vgsm_comm_completion_queue_lock);
				ast_cond_broadcast(
					&vgsm_comm_completion_queue_cond);

				ast_cond_broadcast(
					&comm->current_req->ready_cond);
				vgsm_req_put(comm->current_req);
				comm->current_req = NULL;

				return;
			}

			char buf[90];
			assert(strlen(comm->current_req->request) <= 80);
			strcpy(buf, comm->current_req->request);

			char tmpstr[200];
			vgsm_comm_debug_messages(comm,
				"TX: '%s'\n",
				unprintable_escape(buf, tmpstr,
					sizeof(tmpstr)));

			write(comm->fd, buf, strlen(buf));

			vgsm_parser_change_state(comm, VGSM_PS_AWAITING_ECHO,
						ECHO_TIMEOUT);
		} else {
			vgsm_parser_change_state(comm, VGSM_PS_IDLE, -1);
		}
	break;

	case VGSM_PS_READING_URC:
	case VGSM_PS_AWAITING_SMS_ECHO:
	case VGSM_PS_AWAITING_SMS_ECHO_1A:
	case VGSM_PS_AWAITING_ECHO:
	case VGSM_PS_AWAITING_ECHO_READING_URC:
	case VGSM_PS_READING_RESPONSE:
		ast_log(LOG_NOTICE, "%s: Serial lost synchronization\n",
			comm->name);

		vgsm_parser_change_state(comm, VGSM_PS_RECOVERING, 1 * SEC);
		comm->buf[0] = '\0';
		vgsm_comm_send_recovery_sequence(comm);
	break;

	case VGSM_PS_CLOSED:
	case VGSM_PS_FAILED:
	case VGSM_PS_IDLE:
		ast_log(LOG_WARNING,
			"%s: Unexpected timeout in state %s\n",
			comm->name,
			vgsm_comm_state_to_text(comm->state));
	break;
	}
}

static void *vgsm_comm_thread_main(void *data)
{
	struct pollfd polls[64];
	struct vgsm_module *modules[64];
	int npolls = 0;
	int i;

refresh:

	comm_refresh_pending = FALSE;

	for(i=0; i<npolls; i++) {
		vgsm_module_put(modules[i]);
	}

	npolls = 0;

	ast_mutex_lock(&vgsm.lock);
	struct vgsm_module *module;
	list_for_each_entry(module, &vgsm.ifs, ifs_node) {

		struct vgsm_comm *comm = &module->comm;

		if (!comm->enabled && comm->state != VGSM_PS_CLOSED) {
			vgsm_parser_change_state(comm, VGSM_PS_CLOSED, -1);

			ast_cond_broadcast(&comm->close_cond);
		}

		if (comm->state == VGSM_PS_CLOSED) {

			vgsm_flush_requests(comm);

			if (comm->enabled) {
				vgsm_parser_change_state(comm,
						VGSM_PS_RECOVERING, 1 * SEC);
				comm->buf[0] = '\0';
				vgsm_comm_send_recovery_sequence(comm);
			} else
				continue;

		} else if (comm->state == VGSM_PS_FAILED) {
			vgsm_flush_requests(comm);

			vgsm_parser_change_state(comm,
					VGSM_PS_RECOVERING, 1 * SEC);
			comm->buf[0] = '\0';
			vgsm_comm_send_recovery_sequence(comm);
		}

		polls[npolls].fd = comm->fd;
		polls[npolls].events = POLLHUP | POLLERR | POLLIN;

		modules[npolls] = vgsm_module_get(module);

		npolls++;
	}
	ast_mutex_unlock(&vgsm.lock);

	for(;;) {
		int i;
		longtime_t now = longtime_now();
		longtime_t timeout = -1;

		for (i=0; i<npolls; i++) {
			struct vgsm_comm *comm = &modules[i]->comm;

			if (comm->state == VGSM_PS_IDLE)
				vgsm_process_next_request(comm);
			else if (comm->state == VGSM_PS_FAILED ||
			         comm->state == VGSM_PS_CLOSED)
				vgsm_flush_requests(comm);
		}

		for (i=0; i<npolls; i++) {
			struct vgsm_comm *comm = &modules[i]->comm;

			if (comm->timer_expiration != -1 &&
			    comm->timer_expiration - now > 0 &&
			    (comm->timer_expiration - now < timeout ||
			     timeout == -1))
				timeout = comm->timer_expiration - now;
		}

		int timeout_ms;
		if (timeout == -1)
			timeout_ms = -1;
		else
			timeout_ms = max(timeout / 1000, 1LL);

		if (vgsm.debug_timer)
			ast_verbose("vgsm: set poll timeout = %d ms\n",
				timeout_ms);

		if (comm_refresh_pending)
			goto refresh;

		int res = poll(polls, npolls, timeout_ms);
		if (res < 0 && errno != EINTR) {

			ast_log(LOG_WARNING, "vgsm: Error polling: %s\n",
				strerror(errno));

			goto refresh;
		}

		now = longtime_now();

		for (i=0; i<npolls; i++) {
			struct vgsm_comm *comm = &modules[i]->comm;

			if (comm->timer_expiration != -1 &&
			    comm->timer_expiration < now)
				vgsm_comm_timed_out(comm);

			if (polls[i].revents & POLLIN)
				vgsm_receive(comm);

			if (polls[i].revents & POLLHUP ||
			    polls[i].revents & POLLERR ||
			    polls[i].revents & POLLNVAL)
				comm_refresh_pending = TRUE;
		}

		if (comm_refresh_pending)
			goto refresh;
	}
}

static struct vgsm_req *vgsm_comm_dequeue_urc(void)
{
	ast_mutex_lock(&vgsm_comm_urc_queue_lock);

	while(list_empty(&vgsm_comm_urc_queue)) {
		ast_cond_wait(
			&vgsm_comm_urc_queue_cond,
			&vgsm_comm_urc_queue_lock);
	}

	struct vgsm_req *req;
	req = list_entry(vgsm_comm_urc_queue.next, struct vgsm_req, node);

	list_del(&req->node);

	ast_mutex_unlock(&vgsm_comm_urc_queue_lock);

	return req;
}

static void *vgsm_comm_urc_thread_main(void *data)
{
	for(;;) {
		struct vgsm_req *req;

		req = vgsm_comm_dequeue_urc();

		assert(req->urc_class);

		if (req->urc_class->handler)
			req->urc_class->handler(req);

		vgsm_req_put(req);
	}
}

static struct vgsm_req *vgsm_comm_dequeue_completion(void)
{
	ast_mutex_lock(&vgsm_comm_completion_queue_lock);

	while(list_empty(&vgsm_comm_completion_queue)) {
		ast_cond_wait(
			&vgsm_comm_completion_queue_cond,
			&vgsm_comm_completion_queue_lock);
	}

	struct vgsm_req *req;
	req = list_entry(vgsm_comm_completion_queue.next,
					struct vgsm_req, node);

	list_del(&req->node);

	ast_mutex_unlock(&vgsm_comm_completion_queue_lock);

	return req;
}

static void *vgsm_comm_completion_thread_main(void *data)
{
	for(;;) {
		struct vgsm_req *req;

		req = vgsm_comm_dequeue_completion();

		if (req->completion_func)
			req->completion_func(req,
					req->completion_data);

		vgsm_req_put(req);
	}
}

int vgsm_comm_thread_create()
{
	ast_cond_init(&vgsm_comm_urc_queue_cond, NULL);
	ast_cond_init(&vgsm_comm_completion_queue_cond, NULL);

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	int err;
	err = ast_pthread_create(&vgsm_comm_thread, &attr,
						vgsm_comm_thread_main, NULL);
	if (err < 0)
		return err;

	err = ast_pthread_create(&vgsm_comm_urc_thread, &attr,
					vgsm_comm_urc_thread_main, NULL);
	if (err < 0)
		return err;

	err = ast_pthread_create(&vgsm_comm_completion_thread, &attr,
					vgsm_comm_completion_thread_main, NULL);
	if (err < 0)
		return err;

	return 0;
}
