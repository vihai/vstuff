#ifndef LAPD_H
#define LAPD_H

#include <net/sock.h>

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define LAPD_SAPI_Q931		0x00
#define LAPD_SAPI_X25		0x0f

#define LAPD_BROADCAST_TEI	127

extern struct hlist_head lapd_hash;
extern rwlock_t lapd_hash_lock;

struct lapd_te
{
	struct list_head hash_list;
	u8 tei;
};

struct lapd_sap_tei_mgmt
{
	int T201;
	struct timer_list T201_timer;

	int T202;
	struct timer_list T202_timer;

	int N202;
	int N202_cnt;
};

struct lapd_sap_q931
{
	int k;

	int N200;
	int N200_cnt;
	int N201;

	int T200;
	struct timer_list T200_timer;

	int T203;
	struct timer_list T203_timer;

	u8 v_s;
	u8 v_r;
	u8 v_a;

	int peer_busy;
	int me_busy;
	int peer_waiting_for_ack;
	int rejection_exception;
	int in_timer_recovery;

	enum lapd_datalink_status status;
};

struct lapd_sap_x25
{
	int k;

	int N200;
	int N200_cnt;
	int N201;

	int T200;
	struct timer_list T200_timer;

	int T203;
	struct timer_list T203_timer;
};



struct lapd_opt
{
	struct net_device *dev;

	int nt_mode;
	struct {
		struct {
			u8 tei;
			u16 tei_request_ri;
			int tei_request_pending;
			enum lapd_tei_status status;
		} te;

		struct {
			int cur_dyn_tei;
			struct list_head tes;
			rwlock_t tes_lock;

			int tei_check_outstanding;
			int tei_check_count;
			int tei_check_responses[2];
			int tei_check_tei;
		} nt;

		struct lapd_sap_q931 q931_sap;
		struct lapd_sap_x25 x25_sap;
		struct lapd_sap_tei_mgmt tei_mgmt_sap;
	};


};

/* WARNING: don't change the layout of the members in lapd_sock! */
struct lapd_sock {
	struct sock sk;
	struct lapd_opt lapd;
};

struct lapd_device {
	struct net_device *dev;
	
};

struct lapd_u_frame
{
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 m3:3;
	u8 p_f:1;
	u8 m2:2;
	u8 ft2:1;
	u8 ft1:1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 ft1:1;
	u8 ft2:1;
	u8 m2:2;
	u8 p_f:1;
	u8 m3:3;
#endif
} __attribute__ ((__packed__));

struct lapd_i_frame
{
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 n_s:7;	// Number sent
	u8 ft:1;	// Frame type (0)

	u8 n_r:7;	// Number received
	u8 p:1;		// Poll bit
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 ft:1;
	u8 n_s:7;

	u8 p:1;
	u8 n_r:7;
#endif
} __attribute__ ((__packed__));

struct lapd_s_frame
{
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 :4;		// Unused
	u8 ss:2;	// Supervisory frame bits
	u8 ft:2;	// Frame type bits (01)

	u8 n_r:7;	// Number Received
	u8 p_f:1;	// Poll/Final bit
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 ft:2;
	u8 ss:2;
	u8 :4;

	u8 p_f:1;
	u8 n_r:7;
#endif
} __attribute__ ((__packed__));

struct lapd_address
{
#if defined(__BIG_ENDIAN_BITFIELD)
	u8	sapi:6;	// Service Access Point Indentifier
	u8	c_r:1;	// Command/Response
	u8	ea1:1;	// Extended Address (0)

	u8	tei:7;	// Terminal Endpoint Identifier
	u8	ea2:1;	// Extended Address Bit (1)
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8	ea1:1;
	u8	c_r:1;
	u8	sapi:6;

	u8	ea2:1;
	u8	tei:7;
#endif

} __attribute__ ((__packed__));

struct lapd_hdr
{
	struct lapd_address addr;

	union {
		struct
		 {
#if defined(__BIG_ENDIAN_BITFIELD)
			u8 pad:6;
			u8 ft2:1;
			u8 ft1:1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
			u8 ft1:1;
			u8 ft2:1;
			u8 pad:6;
		 };
#endif

		u8 control;

		struct lapd_u_frame u;
	};

	u8 data[0];
} __attribute__ ((__packed__));

struct lapd_hdr_e
{
	struct lapd_address addr;

	union {
		struct
		 {
			u8 control;
			u8 control2;
		 };

		struct lapd_i_frame i;
		struct lapd_s_frame s;
	};

	u8 data[0];
} __attribute__ ((__packed__));

enum lapd_cr
{
	COMMAND = 0,
	RESPONSE = 1,
};

enum lapd_frame_type
{
	IFRAME,
	SFRAME,
	UFRAME,
};

enum lapd_sframe_function
{
	RR	= 0x00,
	RNR	= 0x04,
	REJ	= 0x08,
};

enum lapd_uframe_function
{
	SABME	= 0x6C,
	DM	= 0x0C,
	UI	= 0x00,
	DISC	= 0x40,
	UA	= 0x60,
	FRMR	= 0x84,
	XID	= 0xAC,
};

#define LAPD_UFRAME_FUNCTIONS_MASK 0xEC
#define LAPD_SFRAME_FUNCTIONS_MASK 0xFE

// These two functions apply only to RECEIVED frames
static inline int lapd_is_command(int nt_mode, int c_r)
{
	return !nt_mode != !c_r;
}

static inline u8 lapd_make_cr(int nt_mode, int c_r)
{
	return (!nt_mode == !c_r) ? 1 : 0;
}

static inline enum lapd_frame_type lapd_frame_type(u8 control)
{ 
	if (!(control & 0x01)) return IFRAME;
	else if(!(control & 0x02)) return SFRAME;
	else return UFRAME;
}

static inline int lapd_is_valid_nr(struct lapd_opt *lo, int n_r)
{
	// V(A) <= N(R) <= V(S)

	return (n_r - lo->q931_sap.v_a) % 128 <=
		(lo->q931_sap.v_s - lo->q931_sap.v_a) % 128;
}

static inline enum lapd_uframe_function lapd_uframe_function(u8 control)
{
	return control & LAPD_UFRAME_FUNCTIONS_MASK;
}

static inline enum lapd_sframe_function lapd_sframe_function(u8 control)
{
	return control & LAPD_SFRAME_FUNCTIONS_MASK;
}

static inline u8 lapd_uframe_make_control(
	enum lapd_uframe_function function, int p_f)
{
	return 0x03 | function | (p_f?0x08:0);
}

static inline u8 lapd_sframe_make_control(
	enum lapd_sframe_function function)
{
	return 0x01 | function;
}

static inline u8 lapd_sframe_make_control2(u8 n_r, int p_f)
{
	return (n_r << 1) | (p_f ? 1 : 0);
}

static inline struct lapd_opt *lapd_sk(const struct sock *__sk)
{
	return &((struct lapd_sock *)__sk)->lapd;
}

extern void setup_lapd(struct net_device *netdev);

struct sk_buff *lapd_prepare_uframe(struct sock *sk, u8 sapi, u8 tei,
	enum lapd_uframe_function function, int datalen, int *err);

int lapd_send_uframe(struct sock *sk, u8 sapi, u8 tei,
	enum lapd_uframe_function function, void *data, int datalen);

inline int lapd_send_frame(struct sk_buff *skb);

// INOUT

void lapd_unhash(struct sock *sk);
void lapd_start_multiframe_establishment(struct sock *sk);
void lapd_start_multiframe_release(struct sock *sk);
void lapd_q931_T200_timer(unsigned long data);
inline int lapd_handle_socket_frame(struct sock *sk, struct sk_buff *skb);
void lapd_q931_T203_timer(unsigned long data);
void lapd_q931_T203_timer(unsigned long data);
void lapd_x25_T200_timer(unsigned long data);
void lapd_x25_T203_timer(unsigned long data);

int lapd_send_iframe(struct sock *sk, u8 sapi, u8 tei,
	void *data, int datalen);
struct sk_buff *lapd_prepare_iframe(struct sock *sk, u8 sapi, u8 tei,
	int datalen, int *err);
int lapd_send_completed_iframe(struct sk_buff *skb);

int lapd_send_uframe(struct sock *sk, u8 sapi, u8 tei,
	enum lapd_uframe_function function, void *data, int datalen);
struct sk_buff *lapd_prepare_uframe(struct sock *sk, u8 sapi, u8 tei,
	enum lapd_uframe_function function, int datalen, int *err);
int lapd_send_completed_uframe(struct sk_buff *skb);


#endif
