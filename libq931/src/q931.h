#ifndef _Q931_H
#define _Q931_H

#include <sys/socket.h>

#include <lapd.h>

#include "list.h"

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#define Q931_MAX_DIGITS 20

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

enum q931_user_state
{
	U0_NULL_STATE,
	U1_CALL_INITIATED,
	U2_OVERLAP_SENDING,
	U3_OUTGOING_CALL_PROCEEDING,
	U4_CALL_DELIVERED,
	U6_CALL_PRESENT,
	U7_CALL_RECEIVED,
	U8_CONNECT_REQUEST,
	U9_INCOMING_CALL_PROCEEDING,
	U10_ACTIVE,
	U11_DISCONNECT_REQUEST,
	U12_DISCONNECT_INDICATION,
	U15_SUSPEND_REQUEST,
	U17_RESUME_REQUEST,
	U19_RELEASE_REQUEST,
	U25_OVERLAP_RECEIVING,
};

enum q931_network_state
{
	N0_NULL_STATE,
	N1_CALL_INITIATED,
	N2_OVERLAP_SENDING,
	N3_OUTGOING_CALL_PROCEEDING,
	N4_CALL_DELIVERED,
	N6_CALL_PRESENT,
	N7_CALL_RECEIVED,
	N8_CONNECT_REQUEST,
	N9_INCOMING_CALL_PROCEEDING,
	N10_ACTIVE,
	N11_DISCONNECT_REQUEST,
	N12_DISCONNECT_INDICATION,
	N15_SUSPEND_REQUEST,
	N17_RESUME_REQUEST,
	N19_RELEASE_REQUEST,
	N22_CALL_ABORT,
	N25_OVERLAP_RECEIVING
};

enum q931_mode
{
	UNKNOWN_MODE,
	CIRCUIT_MODE,
	PACKET_MODE
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

struct q931_libstate
{
	void (*report)(int level, const char *format, ...);
};

struct q931_interface;
struct q931_dlc
{
	struct q931_libstate *libstate;

	int socket;
	int poll_id;
	struct q931_interface *interface;
	enum q931_dlc_status status;
};

struct q931_call;
struct q931_interface
{
	struct q931_libstate *libstate;

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
	struct list_head calls;

	void (*setup_callback)(struct q931_call *call);
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

#define Q931_MAX_TES_ON_BUS 16

// q931_call.interface is used when the call doesn't yet have an associated DLC
// This happens when the call
struct q931_call
{
	struct list_head calls_node;

	struct q931_interface *interface;

	const struct q931_dlc *dlcs[Q931_MAX_TES_ON_BUS];
	int ndlcs;
	const struct q931_dlc *selected_dlc;

	enum q931_call_direction direction;
	q931_callref call_reference;

	enum q931_user_state user_state;
	enum q931_network_state net_state;

	char calling_number[Q931_MAX_DIGITS + 1];
	char called_number[Q931_MAX_DIGITS + 1];
	int sending_complete;

	void *pvt;

	void (*alerting_callback)(struct q931_call *call);
	void (*release_callback)(struct q931_call *call);
	void (*connect_callback)(struct q931_call *call);
	void (*disconnect_callback)(struct q931_call *call);
	void (*information_callback)(struct q931_call *call);
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
	struct q931_libstate *libstate,
	void (*report)(int level, const char *format, ...))
{
	libstate->report = report;
}

struct q931_libstate *q931_init();
void q931_leave(struct q931_libstate *libstate);
void q931_receive(struct q931_dlc *dlc);
struct q931_interface *q931_open_interface(
	struct q931_libstate *libstate,
	const char *name);
void q931_close_interface(struct q931_interface *interface);

struct q931_call *q931_alloc_call();
int q931_make_call(struct q931_interface *interface, struct q931_call *call);

static inline void q931_call_set_calling_number(
	struct q931_call *call,
	const char *calling_number)
{
	strncpy(call->calling_number, calling_number,
		sizeof(call->calling_number));
	call->called_number[sizeof(call->calling_number)-1]='\0';
}

static inline void q931_call_set_called_number(
	struct q931_call *call,
	const char *called_number)
{
	strncpy(call->called_number, called_number,
		sizeof(call->called_number));
	call->called_number[sizeof(call->called_number)-1]='\0';
}

void q931_free_call(struct q931_call *call);
void q931_call_connect(struct q931_call *call);
void q931_call_disconnect(struct q931_call *call);
void q931_call_alerting(struct q931_call *call);
void q931_call_proceeding(struct q931_call *call);


#endif
