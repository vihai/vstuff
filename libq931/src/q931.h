#ifndef _Q931_H
#define _Q931_H

#include <sys/socket.h>
#include <lapd_user.h>

#include "list.h"

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

enum q931_user_state {
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

enum q931_network_state {
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

enum q931_mode {
	UNKNOWN_MODE,
	CIRCUIT_MODE,
	PACKET_MODE
};

struct q931_header {
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

struct q931_message_header {
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

union q931_callref_onwire
{
#if __BYTE_ORDER == __BIG_ENDIAN
  unsigned long longval;
  __u8 octets[4];
  int direction:1;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
  unsigned long longval;
  __u8 octets[4];

  struct
   {
    int :7;
    int direction:1;
    int :24;
   };
#endif
};


struct q931_interface
{
	int socket;

	enum lapd_role role;

	q931_callref next_call_reference;
	int call_reference_size;

	struct list_head calls;
};

struct q931_datalink
{
	int socket;
	struct q931_interface *interface;
};

enum q931_call_direction
{
	Q931_CALL_DIRECTION_OUTBOUND	= 0x0,
	Q931_CALL_DIRECTION_INBOUND	= 0x1,
};

struct q931_call
{
	struct list_head node;

	struct q931_interface *interface;
	enum q931_call_direction direction;
	q931_callref call_reference;

	enum q931_user_state user_status;
	enum q931_network_state network_status;
};

static inline int q931_intcmp(int a, int b)
{
 if (a==b) return 0;
 else if (a>b) return 1;
 else return -1;
}

void q931_init();
void q931_receive(struct q931_datalink *dlc);
struct q931_interface *q931_open_interface(const char *name);
void q931_close_interface(struct q931_interface *interface);
struct q931_call *q931_alloc_call(enum q931_call_direction direction);
int q931_make_call(struct q931_interface *interface, struct q931_call *call);
void q931_free_call(struct q931_call *call);
struct q931_datalink *q931_user_datalink(struct q931_interface *interface);


#endif
