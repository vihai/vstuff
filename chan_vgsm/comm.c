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

#include <asterisk/lock.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>

#include <linux/vgsm.h> // REMOVE ME FIXME XXX

#include "chan_vgsm.h"
#include "util.h"
#include "comm.h"

#define ECHO_TIMEOUT (800 * MILLISEC)
#define URC_TIMEOUT (500 * MILLISEC)
#define SMS_ECHO_TIMEOUT (1 * SEC)
#define READING_URC_TIMEOUT (3 * SEC)

static void vgsm_comm_timers_updated(struct vgsm_timerset *set);
static void vgsm_comm_timer(void *data);

int vgsm_comm_init(
	struct vgsm_comm *comm,
	struct vgsm_urc_class *urc_classes)
{
	comm->fd = -1;
	comm->urc_classes = urc_classes;
	comm->state = VGSM_COMM_CLOSED;

	ast_mutex_init(&comm->state_lock);

	ast_cond_init(&comm->state_cond, NULL);

	comm->comm_thread = AST_PTHREADT_NULL;
	INIT_LIST_HEAD(&comm->requests_queue);

	comm->urc_thread = AST_PTHREADT_NULL;
	INIT_LIST_HEAD(&comm->urc_queue);
	ast_mutex_init(&comm->urc_queue_lock);
	ast_cond_init(&comm->urc_queue_cond, NULL);

	comm->completion_thread = AST_PTHREADT_NULL;
	INIT_LIST_HEAD(&comm->completion_queue);
	ast_mutex_init(&comm->completion_queue_lock);
	ast_cond_init(&comm->completion_queue_cond, NULL);

	vgsm_timerset_init(&comm->timerset, vgsm_comm_timers_updated);
	vgsm_timer_init(&comm->timer, &comm->timerset, "comm",
			vgsm_comm_timer, comm);

	return 0;
}

void vgsm_comm_destroy(struct vgsm_comm *comm)
{
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
	case VGSM_COMM_CLOSED:
		return "CLOSED";
	case VGSM_COMM_CLOSING:
		return "CLOSING";
	case VGSM_COMM_FAILED:
		return "FAILED";
	case VGSM_COMM_RECOVERING:
		return "RECOVERING";
	case VGSM_COMM_IDLE:
		return "IDLE";
	case VGSM_COMM_READING_URC:
		return "READING_URC";
	case VGSM_COMM_AWAITING_SMS_ECHO:
		return "AWAITING_SMS_ECHO";
	case VGSM_COMM_AWAITING_SMS_ECHO_1A:
		return "AWAITING_SMS_ECHO_1A";
	case VGSM_COMM_AWAITING_ECHO:
		return "AWAITING_ECHO";
	case VGSM_COMM_AWAITING_ECHO_READING_URC:
		return "AWAITING_ECHO_READING_URC";
	case VGSM_COMM_READING_RESPONSE:
		return "READING_RESPONSE";
	}

	return "*UNKNOWN*";
}

const char *vgsm_comm_message_type_to_text(
	enum vgsm_comm_message_type mt)
{
	switch(mt) {
	case VGSM_COMM_MSG_CLOSE:
		return "CLOSE";
	case VGSM_COMM_MSG_REFRESH:
		return "REFRESH";
	case VGSM_COMM_MSG_INITIALIZE:
		return "INITIALIZE";
	}

	return "*UNKNOWN*";
}

void vgsm_comm_send_message(
	struct vgsm_comm *comm,
	enum vgsm_comm_message_type mt,
	void *data,
	int len)
{
	struct vgsm_comm_message *msg;

	assert(pthread_self() != comm->comm_thread);

	msg = alloca(sizeof(*msg) + len);
	msg->type = mt;
	msg->len = len;

	if (data && len)
		memcpy(&msg->data, data, len);

	if (write(comm->cmd_pipe_write, msg, sizeof(*msg) + len) < 0) {
		ast_log(LOG_WARNING,
			"%s: Error writing to command pipe: %s\n",
			comm->name,
			strerror(errno));
	}
}

static void vgsm_comm_timers_updated(struct vgsm_timerset *set)
{
	struct vgsm_comm *comm = container_of(set, struct vgsm_comm, timerset);

	/* If the timers have been updated in the handling thread we are
	 * already going to recalculate before select so threre is no need
	 * to send a message to ourselves risking a deadlock if the pipe does
	 * not have space
	 */

	if (pthread_self() != comm->comm_thread)
		vgsm_comm_send_message(comm, VGSM_COMM_MSG_REFRESH, NULL, 0);
}

static void vgsm_comm_change_state(
	struct vgsm_comm *comm,
	enum vgsm_comm_state newstate,
	longtime_t timeout)
{
	char tmpstr[16] = "";

	if (timeout == -1) {
		strcpy(tmpstr, ", no timeout");

		vgsm_timer_stop(&comm->timer);
	} else if (timeout != -2) {
		snprintf(tmpstr, sizeof(tmpstr),
			", timeout = %lldms",
			timeout / 1000);

		vgsm_timer_start_delta(&comm->timer, timeout);
	}

	vgsm_comm_debug_characters(comm,
		"State change from %s to %s%s\n",
		vgsm_comm_state_to_text(comm->state),
		vgsm_comm_state_to_text(newstate),
		tmpstr);

	ast_mutex_lock(&comm->state_lock);
	comm->state = newstate;
	ast_mutex_unlock(&comm->state_lock);
	ast_cond_broadcast(&comm->state_cond);
}

void vgsm_comm_close(struct vgsm_comm *comm)
{
	vgsm_comm_send_message(comm, VGSM_COMM_MSG_CLOSE, NULL, 0);

	ast_mutex_lock(&comm->state_lock);
	while(comm->state != VGSM_COMM_CLOSED) {
		ast_cond_wait(&comm->state_cond, &comm->state_lock);
	}
	ast_mutex_unlock(&comm->state_lock);

	free(comm->name);
	comm->name = "***";
}

static void *vgsm_comm_thread_main(void *data);
static void *vgsm_comm_urc_thread_main(void *data);
static void *vgsm_comm_completion_thread_main(void *data);

int vgsm_comm_open(struct vgsm_comm *comm, int fd, const char *name)
{
	int err;

	assert(comm->state == VGSM_COMM_CLOSED);

	comm->fd = fd;
	comm->name = strdup(name);

	int filedes[2];
	if (pipe(filedes) < 0) {
		ast_log(LOG_ERROR,
			"Cannot create cmd_pipe pipe: %s\n",
			strerror(errno));
		err = -errno;
		goto err_pipe;
	}

	comm->cmd_pipe_read = filedes[0];
	comm->cmd_pipe_write = filedes[1];

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	err = ast_pthread_create(&comm->comm_thread, &attr,
					vgsm_comm_thread_main, comm);
	if (err < 0)
		goto err_pthread_create_comm;

	comm->urc_thread_has_to_exit = FALSE;
	err = ast_pthread_create(&comm->urc_thread, &attr,
					vgsm_comm_urc_thread_main, comm);
	if (err < 0)
		goto err_pthread_create_urc;

	comm->completion_thread_has_to_exit = FALSE;
	err = ast_pthread_create(&comm->completion_thread, &attr,
					vgsm_comm_completion_thread_main, comm);
	if (err < 0)
		goto err_pthread_create_completion;

	pthread_attr_destroy(&attr);

	return 0;

	pthread_kill(comm->completion_thread, SIGTERM);
err_pthread_create_completion:
	pthread_kill(comm->urc_thread, SIGTERM);
err_pthread_create_urc:
	pthread_kill(comm->comm_thread, SIGTERM);
err_pthread_create_comm:
	close(comm->cmd_pipe_read);
	close(comm->cmd_pipe_write);
err_pipe:

	return err;
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
	const char *cmd = "AT E1 V1 Q0\r";
	if (write(comm->fd, cmd, strlen(cmd)) < 0) {
		ast_log(LOG_WARNING,
			"%s: error writing to serial: %s\n",
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
		goto err_req_alloc;

	if (vsnprintf(req->request, sizeof(req->request), fmt, ap) >=
						sizeof(req->request) - 2)
		goto err_too_big;

	strcat(req->request, "\r");

	req->ready = FALSE;
	req->timeout = timeout + 100 * MILLISEC;
	req->retransmit_cnt = 3;
	req->completion_func = completion_func;
	req->completion_data = completion_data;

	if (sms_pdu && sms_pdu_len) {
		req->sms_text_pdu = malloc((sms_pdu_len * 2) + 2);
		if (!req->sms_text_pdu)
			goto err_malloc_pdu;

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

	vgsm_comm_send_message(comm, VGSM_COMM_MSG_REFRESH, NULL, 0);

	return req;

	free(req->sms_text_pdu);
err_malloc_pdu:
err_too_big:
	vgsm_req_put(req);
err_req_alloc:

	return NULL;
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
			if (write(comm->fd, "\x1b", 1) < 0) {
				ast_log(LOG_WARNING,
					"%s: Error writing to serial: %s\n",
					comm->name,
					strerror(errno));
			}

			return 4;
		}

		if (write(comm->fd, req->sms_text_pdu,
					strlen(req->sms_text_pdu)) < 0) {
			ast_log(LOG_WARNING,
				"%s: Error writing to serial: %s\n",
				comm->name,
				strerror(errno));
		}

		char tmpstr[360];
		vgsm_comm_debug_messages(comm,
			"TX: '%s' (sms)\n",
			unprintable_escape(req->sms_text_pdu,
						tmpstr, sizeof(tmpstr)));

		vgsm_comm_change_state(comm, VGSM_COMM_AWAITING_SMS_ECHO,
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
		vgsm_comm_change_state(comm, VGSM_COMM_IDLE, -1);

		comm->current_req->err = err;
		ast_mutex_lock(&comm->current_req->ready_lock);
		comm->current_req->ready = TRUE;
		ast_mutex_unlock(&comm->current_req->ready_lock);

		ast_mutex_lock(&comm->completion_queue_lock);
		list_add_tail(&vgsm_req_get(comm->current_req)->node,
				&comm->completion_queue);
		ast_mutex_unlock(&comm->completion_queue_lock);
		ast_cond_broadcast(&comm->completion_queue_cond);

		ast_cond_broadcast(&comm->current_req->ready_cond);

		vgsm_req_put(comm->current_req);
		comm->current_req = NULL;
	}

	return end - comm->buf + 2;
}

static int vgsm_comm_match_echo(struct vgsm_comm *comm, const char *sent)
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

static int vgsm_comm_match_urc(struct vgsm_comm *comm)
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

		vgsm_comm_debug_messages(comm,
			"Unexpected char 0x%02x\n",
			*begin);
		return 1;
	}

	begin++;

	if (*begin == '\0')
		return 0;

	if (*begin != '\n') {
		vgsm_comm_debug_messages(comm,
			"Unexpected char 0x%02x after <cr>\n",
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
			vgsm_comm_debug_messages(comm,
				"Unexpected char 0x%02x after <cr>\n",
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

	if (comm->state == VGSM_COMM_AWAITING_ECHO)
		vgsm_comm_change_state(comm, VGSM_COMM_AWAITING_ECHO,
					ECHO_TIMEOUT);

	if (urc->urc_class->detect_end) {
		comm->current_urc = urc;

		if (comm->state == VGSM_COMM_AWAITING_ECHO)
			vgsm_comm_change_state(comm,
				VGSM_COMM_AWAITING_ECHO_READING_URC,
				ECHO_TIMEOUT);
		else
			vgsm_comm_change_state(comm,
				VGSM_COMM_READING_URC,
				READING_URC_TIMEOUT);

		vgsm_timer_start_delta(&comm->timer, URC_TIMEOUT);
	} else {
		ast_mutex_lock(&comm->urc_queue_lock);
		list_add_tail(&urc->node, &comm->urc_queue);
		ast_mutex_unlock(&comm->urc_queue_lock);
		ast_cond_broadcast(&comm->urc_queue_cond);
	}

	return end - comm->buf + 2;
}

static int vgsm_comm_match_urc_cont(struct vgsm_comm *comm)
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

	if (comm->state == VGSM_COMM_AWAITING_ECHO_READING_URC)
		vgsm_timer_start_delta(&comm->timer, ECHO_TIMEOUT);

	assert(comm->current_urc->urc_class->detect_end);

	if (comm->current_urc->urc_class->detect_end(comm->current_urc)) {
		ast_mutex_lock(&comm->urc_queue_lock);
		list_add_tail(&comm->current_urc->node, &comm->urc_queue);
		ast_mutex_unlock(&comm->urc_queue_lock);
		ast_cond_broadcast(&comm->urc_queue_cond);

		comm->current_urc = NULL;

		if (comm->state == VGSM_COMM_AWAITING_ECHO_READING_URC) {
			vgsm_comm_change_state(comm, VGSM_COMM_AWAITING_ECHO,
						ECHO_TIMEOUT);
		} else {
			vgsm_comm_change_state(comm, VGSM_COMM_IDLE, -1);
		}
	}

	return end - comm->buf + 2;
}

static int vgsm_comm_receive(struct vgsm_comm *comm)
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
		case VGSM_COMM_CLOSED:
		case VGSM_COMM_CLOSING:
			ast_log(LOG_ERROR, "%s: Unexpected vgsm_receive()"
				" in state %s\n", comm->name,
				vgsm_comm_state_to_text(comm->state));
		break;

		case VGSM_COMM_FAILED:
		case VGSM_COMM_RECOVERING:
			/* Throw away everything */
			npull = nread;
		break;

		case VGSM_COMM_IDLE:
			npull = vgsm_comm_match_urc(comm);
		break;

		case VGSM_COMM_AWAITING_ECHO: {
			int nmatch = vgsm_comm_match_echo(comm,
					comm->current_req->request);
			if (nmatch > 0) {
				vgsm_comm_change_state(comm,
					VGSM_COMM_READING_RESPONSE,
					comm->current_req->timeout);

				npull = nmatch;
			} else if (nmatch != 0) {
				npull = vgsm_comm_match_urc(comm);
			}
		}
		break;

		case VGSM_COMM_READING_URC:
		case VGSM_COMM_AWAITING_ECHO_READING_URC:
			npull = vgsm_comm_match_urc_cont(comm);
		break;

		case VGSM_COMM_AWAITING_SMS_ECHO: {
			int nmatch = vgsm_comm_match_echo(comm,
					comm->current_req->sms_text_pdu);
			if (nmatch > 0) {
				if (write(comm->fd, "\x1a", 1) < 0) {
					ast_log(LOG_WARNING,
						"%s: Error writing to serial:"
						" %s\n",
						comm->name,
						strerror(errno));
				}

				vgsm_comm_change_state(comm,
					VGSM_COMM_AWAITING_SMS_ECHO_1A,
					-2);

				npull = nmatch;
			}
		}
		break;

		case VGSM_COMM_AWAITING_SMS_ECHO_1A: {
			int nmatch = vgsm_comm_match_echo(comm, "\x1a");
			if (nmatch > 0) {
				vgsm_comm_change_state(comm,
					VGSM_COMM_READING_RESPONSE,
					comm->current_req->timeout);

				npull = nmatch;
			}
		}
		break;

		case VGSM_COMM_READING_RESPONSE:
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

	if (write(comm->fd, comm->current_req->request,
			strlen(comm->current_req->request)) < 0) {
		ast_log(LOG_WARNING,
			"%s: Error writing to serial: %s\n",
			comm->name,
			strerror(errno));
	}

	vgsm_comm_change_state(comm, VGSM_COMM_AWAITING_ECHO, ECHO_TIMEOUT);
}

static void vgsm_flush_requests(struct vgsm_comm *comm)
{
	struct vgsm_req *req;

	while ((req = vgsm_comm_dequeue_req(comm))) {

		req->err = VGSM_RESP_FAILED;

		ast_mutex_lock(&req->ready_lock);
		req->ready = TRUE;
		ast_mutex_unlock(&req->ready_lock);

		ast_mutex_lock(&comm->completion_queue_lock);
		list_add_tail(&vgsm_req_get(req)->node,
				&comm->completion_queue);
		ast_mutex_unlock(&comm->completion_queue_lock);
		ast_cond_broadcast(&comm->completion_queue_cond);

		ast_cond_broadcast(&req->ready_cond);
		vgsm_req_put(req);
	}
}

static void vgsm_comm_timer(void *data)
{
	struct vgsm_comm *comm = data;

	switch(comm->state) {
	case VGSM_COMM_RECOVERING:
		if (comm->current_req) {
			comm->current_req->retransmit_cnt--;

			if (comm->current_req->retransmit_cnt == 0) {
				vgsm_comm_change_state(comm,
					VGSM_COMM_FAILED, -1);

				comm->current_req->err = VGSM_RESP_FAILED;

				ast_mutex_lock(&comm->current_req->ready_lock);
				comm->current_req->ready = TRUE;
				ast_mutex_unlock(&comm->current_req->
								ready_lock);

				ast_mutex_lock(&comm->completion_queue_lock);
				list_add_tail(
					&vgsm_req_get(comm->current_req)->node,
					&comm->completion_queue);
				ast_mutex_unlock(
					&comm->completion_queue_lock);
				ast_cond_broadcast(
					&comm->completion_queue_cond);

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
				"TX: '%s' (retrans)\n",
				unprintable_escape(buf, tmpstr,
					sizeof(tmpstr)));

			if (write(comm->fd, buf, strlen(buf)) < 0) {
				ast_log(LOG_WARNING,
					"%s: Error writing to serial: %s\n",
					comm->name,
					strerror(errno));
			}

			vgsm_comm_change_state(comm, VGSM_COMM_AWAITING_ECHO,
						ECHO_TIMEOUT);
		} else {
			vgsm_comm_change_state(comm, VGSM_COMM_IDLE, -1);
		}
	break;

	case VGSM_COMM_READING_URC:
	case VGSM_COMM_AWAITING_SMS_ECHO:
	case VGSM_COMM_AWAITING_SMS_ECHO_1A:
	case VGSM_COMM_AWAITING_ECHO:
	case VGSM_COMM_AWAITING_ECHO_READING_URC:
	case VGSM_COMM_READING_RESPONSE:
		ast_log(LOG_NOTICE,
			"%s: Serial lost synchronization in state %s\n",
			comm->name,
			vgsm_comm_state_to_text(comm->state));

		if (comm->current_urc) {
			vgsm_req_put(comm->current_urc);
			comm->current_urc = NULL;
		}

		vgsm_comm_change_state(comm, VGSM_COMM_RECOVERING, 1 * SEC);

		if (tcflush(comm->fd, TCIOFLUSH) < 0) {
			ast_log(LOG_NOTICE,
				"%s: tcflush(TCIOFLUSH):  %s\n",
				comm->name,
				strerror(errno));
		}

		comm->buf[0] = '\0';
		vgsm_comm_send_recovery_sequence(comm);
	break;

	case VGSM_COMM_CLOSED:
	case VGSM_COMM_CLOSING:
	case VGSM_COMM_FAILED:
	case VGSM_COMM_IDLE:
		ast_log(LOG_WARNING,
			"%s: Unexpected timeout in state %s\n",
			comm->name,
			vgsm_comm_state_to_text(comm->state));
	break;
	}
}

static int vgsm_comm_receive_message(
	struct vgsm_comm *comm,
	struct pollfd polls[])
{
	struct vgsm_comm_message msgh;

	if (read(comm->cmd_pipe_read, &msgh, sizeof(msgh)) < 0) {
		ast_log(LOG_WARNING,
			"%s: Error reading from command pipe: %s\n",
			comm->name,
			strerror(errno));
		return FALSE;
	}

	struct vgsm_comm_message *msg;

	if (msgh.len) {
		msg = alloca(sizeof(*msg) + msgh.len);
		if (!msg) {
			assert(0);
			// FIXME
		}

		memcpy(msg, &msgh, sizeof(*msg));

		read(comm->cmd_pipe_read, &msg->data, msg->len);
	} else
		msg = &msgh;

	vgsm_comm_debug_messages(comm, "Received message %s (len=%d)\n",
		vgsm_comm_message_type_to_text(msg->type), msg->len);

	if (msg->type == VGSM_COMM_MSG_CLOSE) {
		vgsm_comm_change_state(comm, VGSM_COMM_CLOSING, -1);
		return TRUE;
	} else if (msg->type == VGSM_COMM_MSG_REFRESH)
		return FALSE;

	switch(comm->state) {
	case VGSM_COMM_CLOSED:
	case VGSM_COMM_CLOSING:
		assert(0);
	break;

	case VGSM_COMM_FAILED:
		switch(msg->type) {
		case VGSM_COMM_MSG_CLOSE:
		case VGSM_COMM_MSG_REFRESH:
		break;

		case VGSM_COMM_MSG_INITIALIZE:
		break;
		}
	break;

	case VGSM_COMM_RECOVERING:
		switch(msg->type) {
		case VGSM_COMM_MSG_CLOSE:
		case VGSM_COMM_MSG_REFRESH:
		break;

		case VGSM_COMM_MSG_INITIALIZE:
		break;
		}
	break;

	case VGSM_COMM_IDLE:
		switch(msg->type) {
		case VGSM_COMM_MSG_CLOSE:
		case VGSM_COMM_MSG_REFRESH:
			vgsm_comm_change_state(comm,
					VGSM_COMM_RECOVERING, 1 * SEC);

			comm->buf[0] = '\0';
		break;

		case VGSM_COMM_MSG_INITIALIZE:
		break;
		}
	break;

	case VGSM_COMM_READING_URC:
		switch(msg->type) {
		case VGSM_COMM_MSG_CLOSE:
		case VGSM_COMM_MSG_REFRESH:
		break;

		case VGSM_COMM_MSG_INITIALIZE:
		break;
		}
	break;

	case VGSM_COMM_AWAITING_SMS_ECHO:
		switch(msg->type) {
		case VGSM_COMM_MSG_CLOSE:
		case VGSM_COMM_MSG_REFRESH:
		break;

		case VGSM_COMM_MSG_INITIALIZE:
		break;
		}
	break;

	case VGSM_COMM_AWAITING_SMS_ECHO_1A:
		switch(msg->type) {
		case VGSM_COMM_MSG_CLOSE:
		case VGSM_COMM_MSG_REFRESH:
		break;

		case VGSM_COMM_MSG_INITIALIZE:
		break;
		}
	break;

	case VGSM_COMM_AWAITING_ECHO:
		switch(msg->type) {
		case VGSM_COMM_MSG_CLOSE:
		case VGSM_COMM_MSG_REFRESH:
		break;

		case VGSM_COMM_MSG_INITIALIZE:
		break;
		}
	break;

	case VGSM_COMM_AWAITING_ECHO_READING_URC:
		switch(msg->type) {
		case VGSM_COMM_MSG_CLOSE:
		case VGSM_COMM_MSG_REFRESH:
		break;

		case VGSM_COMM_MSG_INITIALIZE:
		break;
		}
	break;

	case VGSM_COMM_READING_RESPONSE:
		switch(msg->type) {
		case VGSM_COMM_MSG_CLOSE:
		case VGSM_COMM_MSG_REFRESH:
		break;

		case VGSM_COMM_MSG_INITIALIZE:
		break;
		}
	break;
	}

	return 0;
}

static void *vgsm_comm_thread_main(void *data)
{
	struct vgsm_comm *comm = data;

	struct pollfd polls[2];

	polls[0].fd = comm->cmd_pipe_read;
	polls[0].events = POLLHUP | POLLERR | POLLIN;

	polls[1].fd = comm->fd;
	polls[1].events = POLLHUP | POLLERR | POLLIN;

	vgsm_comm_change_state(comm, VGSM_COMM_IDLE, -1);

	for(;;) {
		vgsm_timerset_run(&comm->timerset);

		if (comm->state == VGSM_COMM_IDLE)
			vgsm_process_next_request(comm);
		else if (comm->state == VGSM_COMM_FAILED ||
		         comm->state == VGSM_COMM_CLOSING ||
		         comm->state == VGSM_COMM_CLOSED)
			vgsm_flush_requests(comm);

		longtime_t timeout = vgsm_timerset_next(&comm->timerset);

		int timeout_ms;
		if (timeout == -1)
			timeout_ms = -1;
		else
			timeout_ms = max(timeout / 1000, 1LL);

		vgsm_comm_debug_characters(comm,
				"poll (timeout = %d ms)\n",
				timeout_ms);

		int res = poll(polls, ARRAY_SIZE(polls), timeout_ms);
		if (res < 0) {
			if (errno != EINTR) {
				ast_log(LOG_WARNING,
					"vgsm: Error polling: %s\n",
					strerror(errno));
			}
		}

		if (res == 0)
			continue;

		if (polls[0].revents & (POLLIN | POLLERR | POLLHUP)) {
			if (vgsm_comm_receive_message(comm, polls))
				break;
		}

		if (polls[1].revents & (POLLIN | POLLERR | POLLHUP))
			vgsm_comm_receive(comm);
	}

	ast_mutex_lock(&comm->completion_queue_lock);
	comm->completion_thread_has_to_exit = TRUE;
	ast_mutex_unlock(&comm->completion_queue_lock);
	ast_cond_broadcast(&comm->completion_queue_cond);

	ast_mutex_lock(&comm->urc_queue_lock);
	comm->urc_thread_has_to_exit = TRUE;
	ast_mutex_unlock(&comm->urc_queue_lock);
	ast_cond_broadcast(&comm->urc_queue_cond);

	int err;
	err = -pthread_join(comm->completion_thread, NULL);
	if (err) {
		ast_log(LOG_WARNING, "Cannot join completion thread: %s\n",
			strerror(-err));
	}

	err = -pthread_join(comm->urc_thread, NULL);
	if (err) {
		ast_log(LOG_WARNING, "Cannot join URC thread: %s\n",
			strerror(-err));
	}

	close(comm->cmd_pipe_read);
	close(comm->cmd_pipe_write);

	vgsm_comm_change_state(comm, VGSM_COMM_CLOSED, -1);

	return NULL;
}

static struct vgsm_req *vgsm_comm_dequeue_urc(struct vgsm_comm *comm)
{
	ast_mutex_lock(&comm->urc_queue_lock);
	while(list_empty(&comm->urc_queue)) {
		if (comm->urc_thread_has_to_exit) {
			ast_mutex_unlock(&comm->urc_queue_lock);
			return NULL;
		}

		ast_cond_wait(
			&comm->urc_queue_cond,
			&comm->urc_queue_lock);
	}

	struct vgsm_req *req;
	req = list_entry(comm->urc_queue.next, struct vgsm_req, node);

	list_del(&req->node);
	ast_mutex_unlock(&comm->urc_queue_lock);

	return req;
}

static void *vgsm_comm_urc_thread_main(void *data)
{
	struct vgsm_comm *comm = data;

	for(;;) {
		struct vgsm_req *req;

		req = vgsm_comm_dequeue_urc(comm);
		if (!req)
			break;

		assert(req->urc_class);

		if (req->urc_class->handler)
			req->urc_class->handler(req);

		vgsm_req_put(req);
	}

	vgsm_comm_debug_messages(comm, "URC thread exiting\n");

	return NULL;
}

static struct vgsm_req *vgsm_comm_dequeue_completion(struct vgsm_comm *comm)
{
	ast_mutex_lock(&comm->completion_queue_lock);
	while(list_empty(&comm->completion_queue)) {

		if (comm->completion_thread_has_to_exit) {
			ast_mutex_unlock(&comm->completion_queue_lock);
			return NULL;
		}

		ast_cond_wait(
			&comm->completion_queue_cond,
			&comm->completion_queue_lock);
	}

	struct vgsm_req *req;
	req = list_entry(comm->completion_queue.next,
					struct vgsm_req, node);

	list_del(&req->node);
	ast_mutex_unlock(&comm->completion_queue_lock);

	return req;
}

static void *vgsm_comm_completion_thread_main(void *data)
{
	struct vgsm_comm *comm = data;

	for(;;) {
		struct vgsm_req *req;

		req = vgsm_comm_dequeue_completion(comm);
		if (!req)
			break;

		if (req->completion_func)
			req->completion_func(req, req->completion_data);

		vgsm_req_put(req);
	}

	vgsm_comm_debug_messages(comm, "Completion thread exiting\n");

	return NULL;
}
