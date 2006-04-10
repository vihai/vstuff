#ifndef _VGSM_COMM_H
#define _VGSM_COMM_H

#include <asterisk/lock.h>

#include <list.h>

#define SEC 1000000
#define MILLISEC 1000

enum vgsm_response_codes
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
};

enum vgsm_comm_state
{
	VGSM_PS_BITBUCKET,
	VGSM_PS_IDLE,
	VGSM_PS_RECOVERING,
	VGSM_PS_RECOVERING_PENDING,
	VGSM_PS_AWAITING_ECHO,
	VGSM_PS_READING_RESPONSE,
	VGSM_PS_RESPONSE_READY,
	VGSM_PS_RESPONSE_FAILED,
	VGSM_PS_READING_URC,
	VGSM_PS_AWAITING_ECHO_READING_URC,
	VGSM_PS_RESPONSE_READY_READING_URC,
};

struct vgsm_response
{
	struct list_head queue_node;

	int refcnt;

	struct list_head lines;

	struct vgsm_urc *urc;
	struct vgsm_comm *comm;
};

struct vgsm_response_line
{
	struct list_head node;

	char text[0];
};

struct vgsm_comm;
struct vgsm_urc
{
	const char *code;

	int multiline;

	void (*handler)(const struct vgsm_response *urm);
};

typedef long long longtime_t;

struct vgsm_comm
{
	ast_mutex_t lock;

	const char *name;

	struct vgsm_urc *urcs;

	int fd;

	enum vgsm_comm_state state;
	ast_cond_t state_change_cond;

	longtime_t timer_expiration;

	char request[82];
	int request_timeout;
	int request_retransmit_cnt;

	char buf[2048];
	struct vgsm_response *response;
	int response_error;

	struct vgsm_response *urm;
};

void vgsm_comm_init(struct vgsm_comm *comm, struct vgsm_urc *urcs);

int vgsm_send_request(
	struct vgsm_comm *comm,
	int timeout,
	const char *fmt, ...)
	__attribute__ ((format (printf, 3, 4)));

struct vgsm_response *vgsm_read_response(
	struct vgsm_comm *comm);

int vgsm_expect_ok(struct vgsm_comm *comm);
int vgsm_comm_line_error(const char *line);

const struct vgsm_response_line *vgsm_response_first_line(
	const struct vgsm_response *resp);
const struct vgsm_response_line *vgsm_response_last_line(
	const struct vgsm_response *resp);

void vgsm_respone_get(struct vgsm_response *resp);
void vgsm_response_put(struct vgsm_response *resp);

void vgsm_comm_awake(struct vgsm_comm *comm);

void vgsm_comm_set_bitbucket(struct vgsm_comm *comm);
void vgsm_comm_reset(struct vgsm_comm *comm);
int vgsm_comm_start_recovery(struct vgsm_comm *comm);
int vgsm_comm_thread_create();

#endif
