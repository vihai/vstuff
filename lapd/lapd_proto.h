#ifndef _LAPD_PROTO_H
#define _LAPD_PROTO_H


#ifdef __KERNEL__

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

#endif
#endif
