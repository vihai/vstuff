/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_CBM_H
#define _VGSM_CBM_H

enum vgsm_cbm_geoscopes
{
	VGSM_CBM_GS_IMMEDIATE_CELL		= 0x0,
	VGSM_CBM_GS_NORMAL_PLMN			= 0x1,
	VGSM_CBM_GS_NORMAL_LOCATION_AREA	= 0x2,
	VGSM_CBM_GS_NORMAL_CELL			= 0x3
};

struct vgsm_cbm_serial_number
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 geoscope:2;
	__u16 message_code:10;
	__u8 update_number:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 update_number:4;
	__u16 message_code:10;
	__u8 geoscope:2;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_cbm_data_coding_scheme
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 coding_group:4;
	__u8 :4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 :4;
	__u8 coding_group:4;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

enum vgsm_cbm_dcs_language
{
	VGSM_CBM_DCS_LANG_GERMAN	= 0x0,
	VGSM_CBM_DCS_LANG_ENGLISH	= 0x1,
	VGSM_CBM_DCS_LANG_ITALIAN	= 0x2,
	VGSM_CBM_DCS_LANG_FRENCH	= 0x3,
	VGSM_CBM_DCS_LANG_SPANISH	= 0x4,
	VGSM_CBM_DCS_LANG_DUTCH		= 0x5,
	VGSM_CBM_DCS_LANG_SWEDISH	= 0x6,
	VGSM_CBM_DCS_LANG_DANISH	= 0x7,
	VGSM_CBM_DCS_LANG_PORTUGUESE	= 0x8,
	VGSM_CBM_DCS_LANG_FINNISH	= 0x9,
	VGSM_CBM_DCS_LANG_NORWEGIAN	= 0xa,
	VGSM_CBM_DCS_LANG_GREEK		= 0xb,
	VGSM_CBM_DCS_LANG_TURKISH	= 0xc,
	VGSM_CBM_DCS_LANG_HUNGARIAN	= 0xd,
	VGSM_CBM_DCS_LANG_POLISH	= 0xe,
	VGSM_CBM_DCS_LANG_UNSPECIFIED	= 0xf
};

struct vgsm_cbm_data_coding_scheme_0000
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 coding_group:4;
	__u8 language:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 language:4;
	__u8 coding_group:4;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_cbm_data_coding_scheme_0001
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 coding_group:4;
	__u8 mode:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 mode:4;
	__u8 coding_group:4;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_cbm_data_coding_scheme_0010
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 coding_group:4;
	__u8 special_handling:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 special_handling:4;
	__u8 coding_group:4;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

enum vgsm_cbm_dcs_alphabet
{
	VGSM_CBM_DCS_ALPHABET_DEFAULT		= 0x0,
	VGSM_CBM_DCS_ALPHABET_8_BIT_DATA	= 0x1,
	VGSM_CBM_DCS_ALPHABET_UCS2		= 0x2,
	VGSM_CBM_DCS_ALPHABET_RESERVED		= 0x3
};

struct vgsm_cbm_data_coding_scheme_01xx
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 coding_group:2;
	__u8 compressed:1;
	__u8 has_class:1;
	__u8 message_alphabet:2;
	__u8 message_class:2;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 message_class:2;
	__u8 message_alphabet:2;
	__u8 has_class:1;
	__u8 compressed:1;
	__u8 coding_group:2;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_cbm_data_coding_scheme_1111
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 coding_group:4;
	__u8 :1;
	__u8 message_coding:1;
	__u8 message_class:2;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 message_class:2;
	__u8 message_coding:1;
	__u8 :1;
	__u8 coding_group:4;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_cbm_page_parameter
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 page:4;
	__u8 pages:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 pages:4;
	__u8 page:4;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_cbm
{
	int refcnt;

	struct vgsm_module *module;

	int pdu_len;
	void *pdu;

	enum vgsm_cbm_geoscopes geoscope;
	int message_code;
	int update_number;

	int message_id;

	int pages;
	int page;


	int coding_group;
	enum vgsm_cbm_dcs_language language;
	BOOL compressed;
	int message_class;
	enum vgsm_cbm_dcs_alphabet alphabet;

	wchar_t *text;
};

struct vgsm_cbm *vgsm_cbm_alloc(void);
struct vgsm_cbm *vgsm_cbm_get(struct vgsm_cbm *cbm);
void vgsm_cbm_put(struct vgsm_cbm *cbm);
struct vgsm_cbm *vgsm_decode_cbm_pdu(const char *text_pdu);
void vgsm_cbm_dump(struct vgsm_cbm *cbm);

const char *vgsm_cbm_geoscope_to_text(enum vgsm_cbm_geoscopes gs);
const char *vgsm_cbm_language_to_text(enum vgsm_cbm_dcs_language value);
const char *vgsm_cbm_alphabet_to_text(enum vgsm_cbm_dcs_alphabet value);

#endif
