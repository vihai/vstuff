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

#include "chan_vgsm.h"
#include "util.h"
#include "comm.h"

static pthread_t vgsm_comm_thread = AST_PTHREADT_NULL;

void vgsm_comm_init(struct vgsm_comm *comm, struct vgsm_urc *urcs)
{
	comm->fd = -1;
	comm->urcs = urcs;
	ast_mutex_init(&comm->lock);
	ast_cond_init(&comm->state_change_cond, NULL);
	comm->state = VGSM_PS_BITBUCKET;

	INIT_LIST_HEAD(&comm->urm_queue);
	ast_mutex_init(&comm->urm_queue_lock);
}

void vgsm_respone_get(struct vgsm_response *resp)
{
	ast_mutex_lock(&vgsm.usecnt_lock);
	resp->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);
}

void vgsm_response_put(struct vgsm_response *resp)
{
	ast_mutex_lock(&vgsm.usecnt_lock);
	resp->refcnt--;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!resp->refcnt) {
		struct vgsm_response_line *line, *n;

		list_for_each_entry_safe(line, n, &resp->lines, node) {
			list_del(&line->node);
			free(line);
		}

		free(resp);
	}
}

static const char *vgsm_comm_state_to_text(
	enum vgsm_comm_state state)
{
	switch(state) {
	case VGSM_PS_BITBUCKET:
		return "BITBUCKET";
	case VGSM_PS_IDLE:
		return "IDLE";
	case VGSM_PS_RECOVERING:
		return "RECOVERING";
	case VGSM_PS_AWAITING_RESPONSE:
		return "AWAITING_RESPONSE";
	case VGSM_PS_READING_RESPONSE:
		return "READING_RESPONSE";
	case VGSM_PS_RESPONSE_READY:
		return "RESPONSE_READY";
	case VGSM_PS_READING_URC:
		return "READING_URC";
	case VGSM_PS_AWAITING_RESPONSE_READING_URC:
		return "AWAITING_RESPONSE_READING_URC";
	case VGSM_PS_RESPONSE_READY_READING_URC:
		return "RESPONSE_READY_READING_URC";
	}

	return "*UNKNOWN*";
}

static void vgsm_parser_change_state(
	struct vgsm_comm *comm,
	enum vgsm_comm_state newstate)
{
	vgsm_debug_verb("State change from %s to %s\n",
		vgsm_comm_state_to_text(comm->state),
		vgsm_comm_state_to_text(newstate));

	comm->state = newstate;

	ast_cond_broadcast(&comm->state_change_cond);
}

void vgsm_comm_set_bitbucket(
	struct vgsm_comm *comm)
{
	vgsm_parser_change_state(comm, VGSM_PS_BITBUCKET);
}

void vgsm_comm_reset(
	struct vgsm_comm *comm)
{
	vgsm_parser_change_state(comm, VGSM_PS_IDLE);
}

int vgsm_comm_start_recovery(struct vgsm_comm *comm)
{
	vgsm_parser_change_state(comm, VGSM_PS_RECOVERING);

	sleep(1);

	if (write(comm->fd, "+", 1) < 0) {
		ast_log(LOG_WARNING,
			"write to module failed: %s\n",
			strerror(errno));

		return -1;
	}

	usleep(100000);

	if (write(comm->fd, "+", 1) < 0) {
		ast_log(LOG_WARNING,
			"write to module failed: %s\n",
			strerror(errno));

		return -1;
	}

	usleep(100000);

	if (write(comm->fd, "+", 1) < 0) {
		ast_log(LOG_WARNING,
			"write to module failed: %s\n",
			strerror(errno));

		return -1;
	}

	sleep(1);

	const char *cmd = "AT Z0 &F E1 V1 Q0 &K0\r";
	if (write(comm->fd, cmd, strlen(cmd)) < 0) {
		ast_log(LOG_WARNING,
			"write to module failed: %s\n",
			strerror(errno));

		return -1;
	}

	ast_mutex_lock(&comm->lock);
	while(comm->state != VGSM_PS_IDLE)
		ast_cond_wait(&comm->state_change_cond,
			       	&comm->lock);
	ast_mutex_unlock(&comm->lock);

	return 0;
}

const struct vgsm_response_line *vgsm_response_first_line(
	const struct vgsm_response *resp)
{
	return list_entry(resp->lines.next, struct vgsm_response_line, node);
}

const struct vgsm_response_line *vgsm_response_last_line(
	const struct vgsm_response *resp)
{
	return list_entry(resp->lines.prev, struct vgsm_response_line, node);
}

static struct vgsm_response *vgsm_response_alloc()
{
	struct vgsm_response *resp;

	resp = malloc(sizeof(*resp));
	if (!resp)
		return NULL;

	memset(resp, 0, sizeof(*resp));

	resp->refcnt = 1;

	INIT_LIST_HEAD(&resp->lines);

	return resp;
}

struct vgsm_response *vgsm_read_response(
	struct vgsm_comm *comm)
{
	struct vgsm_response *resp;

	ast_mutex_lock(&comm->lock);
	while(comm->state != VGSM_PS_RESPONSE_READY &&
	      comm->state != VGSM_PS_RESPONSE_READY_READING_URC)
		ast_cond_wait(&comm->state_change_cond,
			      &comm->lock);

	vgsm_parser_change_state(comm, VGSM_PS_IDLE);
	resp = comm->response;
	comm->response = NULL;

	ast_mutex_unlock(&comm->lock);

	return resp;
}

int vgsm_expect_ok(struct vgsm_comm *comm)
{
	ast_mutex_lock(&comm->lock);
	while(comm->state != VGSM_PS_RESPONSE_READY &&
	      comm->state != VGSM_PS_RESPONSE_READY_READING_URC)
		ast_cond_wait(&comm->state_change_cond,
			      &comm->lock);

	int error_code = comm->response_error;

	vgsm_parser_change_state(comm, VGSM_PS_IDLE);
	vgsm_response_put(comm->response);
	comm->response = NULL;

	ast_mutex_unlock(&comm->lock);

	return error_code;
}

static longtime_t longtime_now()
{
	struct timeval now_tv;
	gettimeofday(&now_tv, NULL);

	return now_tv.tv_sec * 1000000LL + now_tv.tv_usec;
}

int sanprintf(char *buf, int bufsize, const char *fmt, ...)
{
	int len = strlen(buf);
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(buf + len, bufsize - len, fmt, ap);
	va_end(ap);

	return len;
}

static char *unprintable_escape(const char *str, char *buf, int bufsize)
{
	const char *c = str;

	assert(bufsize);

	buf[0] = '\0';

	while(*c) {

		switch(*c) {
		case '\r':
			sanprintf(buf, bufsize, "<cr>");
		break;
		case '\n':
			sanprintf(buf, bufsize, "<lf>");
		break;

		default:
			if (isprint(*c))
				sanprintf(buf, bufsize, "%c", *c);
			else
				sanprintf(buf, bufsize, ".");
		}

		c++;
	}

	return buf;
}

int vgsm_send_request(
	struct vgsm_comm *comm,
	int timeout,
	const char *fmt, ...)
{
	va_list ap;
	char buf[82];

	va_start(ap, fmt);

	if (vsnprintf(buf, sizeof(buf), fmt, ap) >= sizeof(buf) - 1)
		return -1;

	ast_mutex_lock(&comm->lock);
	while(comm->state != VGSM_PS_IDLE)
		ast_cond_wait(&comm->state_change_cond,
			      &comm->lock);

	strncpy(comm->request, buf, sizeof(comm->request));
	vgsm_parser_change_state(comm, VGSM_PS_AWAITING_RESPONSE);
	comm->response = vgsm_response_alloc();
	comm->timeout = longtime_now() + timeout * 1000;
	ast_mutex_unlock(&comm->lock);

	strcat(buf, "\r");

	char tmpstr[80];
	vgsm_debug_verb("TX: '%s'\n",
		unprintable_escape(buf, tmpstr, sizeof(tmpstr)));

	vgsm_comm_awake(comm);

	return write(comm->fd, buf, strlen(buf));
}

static void vgsm_retransmit_request(struct vgsm_comm *comm)
{
	char buf[82];
	strncpy(buf, comm->request, sizeof(buf));
	strcat(buf, "\r");

	char tmpstr[80];
	vgsm_debug_verb("TX: '%s'\n",
		unprintable_escape(buf, tmpstr, sizeof(tmpstr)));

	if (write(comm->fd, buf, strlen(buf)) < 0)
		ast_log(LOG_WARNING,
			"Cannot write to module: %s\n",
			strerror(errno));
}

static void handle_response_line(
	struct vgsm_comm *comm,
	const char *line)
{
	// Handle response
	char tmpstr[80];
	vgsm_debug_verb("RX: '%s' (crlf)\n",
		unprintable_escape(line, tmpstr, sizeof(tmpstr)));

	struct vgsm_response_line *resp_line;
	resp_line = malloc(sizeof(struct vgsm_response_line) +
			 strlen(line) + 1);

	strcpy(resp_line->text, line);

	list_add_tail(&resp_line->node, &comm->response->lines);

	if (!strcmp(line, "OK")) {
		comm->response_error = VGSM_RESP_OK;
		vgsm_parser_change_state(comm, VGSM_PS_RESPONSE_READY);
	} else if (!strcmp(line, "CONNECT")) {
		comm->response_error = VGSM_RESP_CONNECT;
		vgsm_parser_change_state(comm, VGSM_PS_RESPONSE_READY);
	} else if (!strcmp(line, "NO CARRIER")) {
		comm->response_error = VGSM_RESP_NO_CARRIER;
		vgsm_parser_change_state(comm, VGSM_PS_RESPONSE_READY);
	} else if (!strcmp(line, "ERROR")) {
		comm->response_error = VGSM_RESP_ERROR;
		vgsm_parser_change_state(comm, VGSM_PS_RESPONSE_READY);
	} else if (!strcmp(line, "NO DIALTONE")) {
		comm->response_error = VGSM_RESP_NO_DIALTONE;
		vgsm_parser_change_state(comm, VGSM_PS_RESPONSE_READY);
	} else if (!strcmp(line, "BUSY")) {
		comm->response_error = VGSM_RESP_BUSY;
		vgsm_parser_change_state(comm, VGSM_PS_RESPONSE_READY);
	} else if (!strcmp(line, "NO ANSWER")) {
		comm->response_error = VGSM_RESP_NO_ANSWER;
		vgsm_parser_change_state(comm, VGSM_PS_RESPONSE_READY);
	} else if (strstr(line, "+CME ERROR: ") == line) {
		comm->response_error = atoi(line + strlen("+CME ERROR: "));
		vgsm_parser_change_state(comm, VGSM_PS_RESPONSE_READY);
	} else if (strstr(line, "+CMS ERROR: ") == line) {
		comm->response_error = atoi(line + strlen("+CMS ERROR: "));
		vgsm_parser_change_state(comm, VGSM_PS_RESPONSE_READY);
	}
}

static void handle_unsolicited_response(
	struct vgsm_comm *comm,
	const char *line)
{
	comm->urm = NULL;

	int i;
	for (i=0; comm->urcs[i].code; i++) {
		if (!strncmp(line, comm->urcs[i].code,
					strlen(comm->urcs[i].code))) {

			comm->urm = vgsm_response_alloc();
			comm->urm->urc = &comm->urcs[i];

			break;
		}
	}

	if (!comm->urm) {
		vgsm_debug("Unhandled URC '%s'\n", line);
		return;
	}

	struct vgsm_response_line *resp_line;
	resp_line = malloc(sizeof(struct vgsm_response_line) +
			 strlen(line) + 1);

	strcpy(resp_line->text, line);

	list_add_tail(&resp_line->node, &comm->urm->lines);

	if (!comm->urm->urc->multiline) {
		list_add_tail(&comm->urm->queue_node, &comm->urm_queue);
		comm->urm = NULL;
		return;
	}

	switch(comm->state) {
	case VGSM_PS_IDLE:
		vgsm_parser_change_state(comm, VGSM_PS_READING_URC);
	break;

	case VGSM_PS_RESPONSE_READY:
		vgsm_parser_change_state(comm,
			VGSM_PS_RESPONSE_READY_READING_URC);
	break;

	case VGSM_PS_AWAITING_RESPONSE:
		vgsm_parser_change_state(comm,
			VGSM_PS_AWAITING_RESPONSE_READING_URC);
	break;

	case VGSM_PS_BITBUCKET:
	case VGSM_PS_RECOVERING:
	case VGSM_PS_READING_RESPONSE:
	case VGSM_PS_READING_URC:
	case VGSM_PS_AWAITING_RESPONSE_READING_URC:
	case VGSM_PS_RESPONSE_READY_READING_URC:
		ast_log(LOG_ERROR,
			"Unexpected handle_unsolicited_message"
		       	" in state %s (%d)\n",
			vgsm_comm_state_to_text(comm->state),
			comm->state);
	break;
	}
}

static int handle_crlf_msg_crlf(struct vgsm_comm *comm)
{
	char *begin, *end;

	begin = comm->buf;

	if (*(begin + 1) == '\0')
		return 0;

	if (*(begin + 1) == '\r')
		return 1;

	if (*(begin + 1) != '\n') {
		ast_log(LOG_WARNING, "Unexpected char 0x%02x after <cr>\n",
			*(begin + 1));

		return 1;
	}

	end = strchr(begin + 2, '\n');
	if (!end)
		return 0;

	if (*(end - 1) != '\r') {
		ast_log(LOG_WARNING, "Unexpected char 0x%02x before <lf>\n",
			*(end - 1));

		return end - begin + 1;
	}

	*(end - 1) = '\0';

	char tmpstr[80];
	vgsm_debug_verb("RX: '%s'\n",
		unprintable_escape(begin + 2, tmpstr, sizeof(tmpstr)));

	switch(comm->state) {
	case VGSM_PS_READING_RESPONSE:
	case VGSM_PS_AWAITING_RESPONSE:
		handle_response_line(comm, begin + 2);
	break;

	case VGSM_PS_IDLE:
	case VGSM_PS_RESPONSE_READY:
	case VGSM_PS_READING_URC:
	case VGSM_PS_AWAITING_RESPONSE_READING_URC:
	case VGSM_PS_RESPONSE_READY_READING_URC:
		handle_unsolicited_response(comm, begin + 2);
	break;

	case VGSM_PS_BITBUCKET:
	case VGSM_PS_RECOVERING:
		assert(1);
	break;
	}

	return end - comm->buf + 1;
}

static int handle_msg_cr(struct vgsm_comm *comm)
{
	char *firstcr;
	firstcr = strchr(comm->buf, '\r');

	*firstcr = '\0';

	if (comm->state != VGSM_PS_AWAITING_RESPONSE)
		return firstcr - comm->buf + 1;

	char tmpstr[80];
	vgsm_debug_verb("RX: '%s'\n",
		unprintable_escape(comm->buf, tmpstr, sizeof(tmpstr)));

	if (!strncmp(comm->buf, comm->request,
			       strlen(comm->request))) {
		
		vgsm_parser_change_state(comm, VGSM_PS_READING_RESPONSE);
	} else {
		char *dropped = malloc(firstcr - comm->buf + 1);

		memcpy(dropped, comm->buf, firstcr - comm->buf);
		dropped[firstcr - comm->buf] = '\0';

		char tmpstr[80];
		ast_log(LOG_WARNING,
			"Dropped spurious bytes '%s' from serial\n",
			unprintable_escape(dropped, tmpstr, sizeof(tmpstr)));

		free(dropped);
	}

	return firstcr - comm->buf + 1;
}

static int handle_msg_crlf(struct vgsm_comm *comm)
{
	char *lf;
	lf = strchr(comm->buf, '\n');

	if (lf == comm->buf) {
		ast_log(LOG_WARNING, "Unexpected <lf>\n");
		return 1;
	}

	*(lf - 1) = '\0';

	char tmpstr[80];
	vgsm_debug_verb("RX: '%s'\n",
		unprintable_escape(comm->buf, tmpstr, sizeof(tmpstr)));

	switch(comm->state) {
	case VGSM_PS_READING_URC:
		vgsm_parser_change_state(comm, VGSM_PS_IDLE);
	break;

	case VGSM_PS_AWAITING_RESPONSE_READING_URC:
		vgsm_parser_change_state(comm, VGSM_PS_AWAITING_RESPONSE);
	break;

	case VGSM_PS_RESPONSE_READY_READING_URC:
		vgsm_parser_change_state(comm, VGSM_PS_RESPONSE_READY);
	break;

	case VGSM_PS_IDLE:
	case VGSM_PS_RESPONSE_READY:
	case VGSM_PS_AWAITING_RESPONSE:
	case VGSM_PS_BITBUCKET:
	case VGSM_PS_RECOVERING:
	case VGSM_PS_READING_RESPONSE:
		ast_log(LOG_ERROR,
			"Unexpected handle_unsolicited_message"
			" in state %s (%d)\n",
			vgsm_comm_state_to_text(comm->state),
			comm->state);
	}

	struct vgsm_response_line *resp_line;
	resp_line = malloc(sizeof(struct vgsm_response_line) +
			 strlen(comm->buf) + 1);

	strcpy(resp_line->text, comm->buf);

	ast_mutex_lock(&comm->urm_queue_lock);
	list_add_tail(&resp_line->node, &comm->urm->lines);
	ast_mutex_unlock(&comm->urm_queue_lock);

	return lf - comm->buf + 1;
}

static int vgsm_receive(struct vgsm_comm *comm)
{
	int buflen = strlen(comm->buf);
	int nread;

	nread = read(comm->fd, comm->buf + buflen,
			sizeof(comm->buf) - buflen - 1);
	if (nread < 0) {
		ast_log(LOG_WARNING, "Error reading from serial: %s\n",
			strerror(errno));

		return -1;
	}

	comm->buf[buflen + nread] = '\0';

	while(1) {
		int nread = 0;

#if 0
char tmpstr[180];
ast_verbose("BUF='%s'\n",
	unprintable_escape(comm->buf, tmpstr, sizeof(tmpstr)));
#endif

		switch(comm->state) {
		case VGSM_PS_BITBUCKET:
			/* Throw away everything */
			nread = strlen(comm->buf);
		break;

		case VGSM_PS_RECOVERING:
			if (strstr(comm->buf, "\r\nOK\r\n")) {
				nread = strlen(comm->buf);

				vgsm_parser_change_state(comm,
					VGSM_PS_IDLE);
			}
		break;

		case VGSM_PS_READING_URC:
		case VGSM_PS_AWAITING_RESPONSE_READING_URC:
		case VGSM_PS_RESPONSE_READY_READING_URC: {
			const char *firstlf = strchr(comm->buf, '\n');
			if (firstlf)
				nread = handle_msg_crlf(comm);
		}
		break;

		case VGSM_PS_IDLE:
		case VGSM_PS_AWAITING_RESPONSE:
		case VGSM_PS_READING_RESPONSE:
		case VGSM_PS_RESPONSE_READY: {
			const char *firstcr;
			firstcr = strchr(comm->buf, '\r');

			if (firstcr > comm->buf)
				nread = handle_msg_cr(comm);
			else if (firstcr == comm->buf)
				nread = handle_crlf_msg_crlf(comm);
		}
		break;
		}

		if (nread)
			memmove(comm->buf, comm->buf + nread,
				strlen(comm->buf + nread) + 1);
		else
			break;
	}

	return 0;
}

static int vgsm_comm_thread_do_stuff()
{
	struct pollfd polls[64];
	struct vgsm_comm *comms[64];
	int npolls = 0;

	{
	struct vgsm_interface *intf;
	list_for_each_entry(intf, &vgsm.ifs, ifs_node) {

		if (intf->comm.fd < 0)
			continue;

		polls[npolls].fd = intf->comm.fd;
		polls[npolls].events = POLLERR | POLLIN;

		comms[npolls] = &intf->comm;

		npolls++;
	}
	}

	for(;;) {
		int i;
		longtime_t now = longtime_now();
		longtime_t timeout = -1;

		for (i=0; i<npolls; i++) {
			ast_mutex_lock(&comms[i]->lock);
			if (comms[i]->state != VGSM_PS_IDLE &&
			    comms[i]->timeout - now > 0 &&
			    (comms[i]->timeout - now < timeout ||
			     timeout == -1))
				timeout = comms[i]->timeout - now;
			ast_mutex_unlock(&comms[i]->lock);
		}

		int timeout_ms;
		if (timeout == -1)
			timeout_ms = -1;
		else
			timeout_ms = timeout / 1000 + 1;

		vgsm_debug_verb("poll timeout = %d\n", timeout_ms);

		int res = poll(polls, npolls, timeout_ms);
		if (res < 0) {
			if (errno == EINTR) {
				/* Force reload of polls */
				return 1;
			}

			ast_log(LOG_WARNING, "Error polling serial: %s\n",
				strerror(errno));

			return 0;
		}

		now = longtime_now();

		for (i=0; i<npolls; i++) {
			ast_mutex_lock(&comms[i]->lock);
			if (comms[i]->state ==
				       	VGSM_PS_AWAITING_RESPONSE &&
			    comms[i]->timeout < now) {
				vgsm_retransmit_request(comms[i]);
			}

			if (polls[i].revents & POLLIN)
				vgsm_receive(comms[i]);

			ast_mutex_unlock(&comms[i]->lock);

			ast_mutex_lock(&comms[i]->urm_queue_lock);
			struct vgsm_response *resp, *tpos;
			list_for_each_entry_safe(resp, tpos,
					&comms[i]->urm_queue, queue_node) {

				assert(resp->urc);

				resp->urc->handler(resp, comms[i]);

				list_del(&resp->queue_node);
				vgsm_response_put(resp);
			}
			ast_mutex_unlock(&comms[i]->urm_queue_lock);
		}
	}
}

static void *vgsm_comm_thread_main(void *data)
{
	while(vgsm_comm_thread_do_stuff());

	return NULL;
}

void vgsm_comm_awake(struct vgsm_comm *comm)
{
	pthread_kill(vgsm_comm_thread, SIGURG);
}

int vgsm_comm_thread_create()
{
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	return ast_pthread_create(&vgsm_comm_thread, &attr,
			vgsm_comm_thread_main, NULL);
}
