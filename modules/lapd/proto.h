/*
 * vISDN LAPD/q.921 protocol implementation
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LAPD_PROTO_H
#define _LAPD_PROTO_H

#ifdef __KERNEL__

#include "device.h"

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

struct lapd_data_hdr
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
#endif
		 };

		u8 control;

		struct lapd_u_frame u;
	};

	u8 data[0];
} __attribute__ ((__packed__));

struct lapd_data_hdr_e
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

struct lapd_frmr
{
	u8 control;
	u8 control2;

#if defined(__BIG_ENDIAN_BITFIELD)
	u8 v_s:7;
	u8 :1;

	u8 v_r:7;
	u8 c_r:1;

	u8 :4;
	u8 z:1;
	u8 y:1;
	u8 x:1;
	u8 w:1;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 :1;
	u8 v_s:7;

	u8 c_r:1;
	u8 v_r:7;

	u8 w:1;
	u8 x:1;
	u8 y:1;
	u8 z:1;
	u8 :4;
#endif
} __attribute__ ((__packed__));

enum lapd_cr
{
	LAPD_COMMAND = 0,
	LAPD_RESPONSE = 1,
};

enum lapd_frame_type
{
	LAPD_FRAME_TYPE_IFRAME,
	LAPD_FRAME_TYPE_SFRAME,
	LAPD_FRAME_TYPE_UFRAME,
};

enum lapd_sframe_function
{
	LAPD_SFRAME_FUNC_INVALID = -1,
	LAPD_SFRAME_FUNC_RR	= 0x00,
	LAPD_SFRAME_FUNC_RNR	= 0x04,
	LAPD_SFRAME_FUNC_REJ	= 0x08,
};

enum lapd_uframe_function
{
	LAPD_UFRAME_FUNC_INVALID = -1,
	LAPD_UFRAME_FUNC_SABME	= 0x6C,
	LAPD_UFRAME_FUNC_DM	= 0x0C,
	LAPD_UFRAME_FUNC_UI	= 0x00,
	LAPD_UFRAME_FUNC_DISC	= 0x40,
	LAPD_UFRAME_FUNC_UA	= 0x60,
	LAPD_UFRAME_FUNC_FRMR	= 0x84,
	LAPD_UFRAME_FUNC_XID	= 0xAC,
};

#define LAPD_UFRAME_FUNCTIONS_MASK 0xEC
#define LAPD_SFRAME_FUNCTIONS_MASK 0xFE

static inline int lapd_rx_is_command(
	struct lapd_device *dev, int c_r)
{
	return (dev->role == LAPD_INTF_ROLE_TE) != !c_r;
}

static inline int lapd_rx_is_response(
	struct lapd_device *dev, int c_r)
{
	return (dev->role == LAPD_INTF_ROLE_TE) == !c_r;
}

static inline u8 lapd_make_cr(
	struct lapd_device *dev, int c_r)
{
	return ((dev->role == LAPD_INTF_ROLE_TE) ==
		!(c_r == LAPD_COMMAND)) ? 1 : 0;
}

static inline enum lapd_frame_type lapd_frame_type(u8 control)
{
	if (!(control & 0x01))
		return LAPD_FRAME_TYPE_IFRAME;
	else if(!(control & 0x02))
		return LAPD_FRAME_TYPE_SFRAME;
	else
		return LAPD_FRAME_TYPE_UFRAME;
}

static inline enum lapd_uframe_function lapd_uframe_function(u8 control)
{
	switch (control & LAPD_UFRAME_FUNCTIONS_MASK) {
	case LAPD_UFRAME_FUNC_SABME:
		return LAPD_UFRAME_FUNC_SABME;
	case LAPD_UFRAME_FUNC_DM:
		return LAPD_UFRAME_FUNC_DM;
	case LAPD_UFRAME_FUNC_UI:
		return LAPD_UFRAME_FUNC_UI;
	case LAPD_UFRAME_FUNC_DISC:
		return LAPD_UFRAME_FUNC_DISC;
	case LAPD_UFRAME_FUNC_UA:
		return LAPD_UFRAME_FUNC_UA;
	case LAPD_UFRAME_FUNC_FRMR:
		return LAPD_UFRAME_FUNC_FRMR;
	case LAPD_UFRAME_FUNC_XID:
		return LAPD_UFRAME_FUNC_XID;
	default:
		return LAPD_UFRAME_FUNC_INVALID;
	}
}

static inline enum lapd_sframe_function lapd_sframe_function(u8 control)
{
	switch (control & LAPD_SFRAME_FUNCTIONS_MASK) {
	case LAPD_SFRAME_FUNC_RR:
		return LAPD_SFRAME_FUNC_RR;
	case LAPD_SFRAME_FUNC_RNR:
		return LAPD_SFRAME_FUNC_RNR;
	case LAPD_SFRAME_FUNC_REJ:
		return LAPD_SFRAME_FUNC_REJ;
	default:
		return LAPD_SFRAME_FUNC_INVALID;
	}
}

static inline const char *lapd_sframe_function_name(
	enum lapd_sframe_function func)
{
	switch (func) {
	case LAPD_SFRAME_FUNC_RR:
		return "RR";
	case LAPD_SFRAME_FUNC_RNR:
		return "RNR";
	case LAPD_SFRAME_FUNC_REJ:
		return "REJ";
	case LAPD_SFRAME_FUNC_INVALID:
		return "INVALID";
	}

	return "*UNKNOWN*";
}

static inline u8 lapd_uframe_make_control(
	enum lapd_uframe_function function, int p_f)
{
	return 0x03 | function | (p_f ? 0x10 : 0);
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
