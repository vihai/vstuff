#ifndef _Q931_H
#define _Q931_H

#include <sys/socket.h>

#include <lapd.h>

#include "timer.h"
#include "list.h"

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#define Q931_MAX_DIGITS 20

#define report_dlc(dlc, lvl, format, arg...)				\
	(dlc)->interface->lib->report((lvl), format, ## arg)
#define report_if(intf, lvl, format, arg...)				\
	(intf)->lib->report((lvl), format, ## arg)
#define report_call(call, lvl, format, arg...)				\
	(call)->interface->lib->report((lvl), format, ## arg)

#define q931_call_start_timer(call, timer)			\
	q931_start_timer_delta(					\
		(call)->interface->lib,				\
		&(call)->timer,					\
		(call)->interface->timer)

#define q931_call_stop_timer(call, timer)			\
	q931_stop_timer(&(call)->timer)

enum q931_log_level
{
	Q931_LOG_DEBUG,
	Q931_LOG_INFO,
	Q931_LOG_NOTICE,
	Q931_LOG_WARNING,
	Q931_LOG_ERR,
	Q931_LOG_CRIT,
	Q931_LOG_ALERT,
	Q931_LOG_EMERG,
};

enum q931_mode
{
	UNKNOWN_MODE,
	CIRCUIT_MODE,
	PACKET_MODE
};

enum q931_setup_mode
{
	Q931_SETUP_POINT_TO_POINT,
	Q931_SETUP_BROADCAST,
};

struct q931_header
{
	__u8 protocol_discriminator;

#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 :4;
	__u8 call_reference_size:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 call_reference_size:4;
	__u8 :4;
#else
#error Unsupported byte order
#endif

	__u8 call_reference[0];

} __attribute__ ((__packed__));

struct q931_message_header
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 f:1;
	__u8 msg:7;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 msg:7;
	__u8 f:1;
#else
#error Unsupported byte order
#endif
	__u8 data[0];
} __attribute__ ((__packed__));

enum q931_protocol_discriminators
{
 Q931_PROTOCOL_DISCRIMINATOR_Q931	= 0x08,
 Q931_PROTOCOL_DISCRIMINATOR_GR303	= 0x4f,
};

typedef signed long q931_callref;

enum q931_dlc_status
{
	DLC_DISCONNECTED,
	DLC_AWAITING_CONNECTION,
	DLC_AWAITING_DISCONNECTION,
	DLC_CONNECTED,
};

struct q931_lib
{
	struct list_head timers;

	void (*report)(int level, const char *format, ...);
};

struct q931_interface;
struct q931_dlc
{
	struct q931_lib *lib;

	int socket;
	int poll_id;
	struct q931_interface *interface;
	enum q931_dlc_status status;
};

struct q931_call;
struct q931_interface
{
	struct q931_lib *lib;

	char *name;

	// NT mode, socket is the master socket
	int nt_socket;
	int nt_poll_id;

	// TE mode, use DLC
	struct q931_dlc te_dlc;

	enum lapd_role role;

	q931_callref next_call_reference;
	int call_reference_size;

	int ncalls;
	// TODO: Use a HASH for improved scalability
	struct list_head calls;

	longtime_t T301;
	longtime_t T302;
	longtime_t T303;
	longtime_t T304;
	longtime_t T305;
	longtime_t T306;
	longtime_t T307;
	longtime_t T308;
	longtime_t T309;
	longtime_t T310;
	longtime_t T312;
	longtime_t T314;
	longtime_t T316;
	longtime_t T317;
	longtime_t T320;
	longtime_t T321;
	longtime_t T322;

	void (*alerting_indication)(struct q931_call *call);
	void (*disconnect_indication)(struct q931_call *call);
	void (*error_indication)(struct q931_call *call); // TE
	void (*info_indication)(struct q931_call *call);
	void (*more_info_indication)(struct q931_call *call);
	void (*notify_indication)(struct q931_call *call);
	void (*proceeding_indication)(struct q931_call *call);
	void (*progress_indication)(struct q931_call *call);
	void (*reject_indication)(struct q931_call *call);
	void (*release_confirm)(struct q931_call *call);//TE
	void (*release_indication)(struct q931_call *call);
	void (*resume_confirm)(struct q931_call *call);//TE
	void (*resume_indication)(struct q931_call *call);
	void (*setup_complete_indication)(struct q931_call *call);//TE
	void (*setup_confirm)(struct q931_call *call);
	void (*setup_indication)(struct q931_call *call);
	void (*status_indication)(struct q931_call *call);
	void (*suspend_confirm)(struct q931_call *call);//TE
	void (*suspend_indication)(struct q931_call *call);
	void (*timeout_indication)(struct q931_call *call);
};

enum q931_call_direction
{
	Q931_CALL_DIRECTION_OUTBOUND	= 0x0,
	Q931_CALL_DIRECTION_INBOUND	= 0x1,
};

enum q931_callref_flag
{
	Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE = 0x0,
	Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE = 0x1,
};

static inline int q931_intcmp(int a, int b)
{
 if (a==b) return 0;
 else if (a>b) return 1;
 else return -1;
}

static inline void q931_init_dlc(
	struct q931_dlc *dlc,
	struct q931_interface *interface,
	int socket)
{
	dlc->socket = socket;
	dlc->interface = interface;
}

static inline void q931_set_logger_func(
	struct q931_lib *lib,
	void (*report)(int level, const char *format, ...))
{
	lib->report = report;
}

struct q931_lib *q931_init();
void q931_leave(struct q931_lib *lib);
void q931_receive(struct q931_dlc *dlc);
struct q931_interface *q931_open_interface(
	struct q931_lib *lib,
	const char *name);
void q931_close_interface(struct q931_interface *interface);

struct q931_call *q931_alloc_call();
int q931_make_call(struct q931_interface *interface, struct q931_call *call);

void q931_free_call(struct q931_call *call);

void q931_alerting_request(struct q931_call *call);
void q931_disconnect_request(struct q931_call *call);
void q931_info_request(struct q931_call *call);
void q931_more_info_request(struct q931_call *call);
void q931_notify_request(struct q931_call *call);
void q931_proceeding_request(struct q931_call *call);
void q931_progress_request(struct q931_call *call);
void q931_reject_request(struct q931_call *call);
void q931_release_request(struct q931_call *call);
void q931_resume_reject_request(struct q931_call *call);
void q931_resume_response(struct q931_call *call); //NT
void q931_setup_complete_request(struct q931_call *call); //NT
void q931_setup_request(struct q931_call *call);
void q931_setup_response(struct q931_call *call);
void q931_status_enquiry_request(struct q931_call *call);
void q931_suspend_reject_request(struct q931_call *call);
void q931_suspend_response(struct q931_call *call); //NT
void q931_suspend_request(struct q931_call *call); // TE

#endif
