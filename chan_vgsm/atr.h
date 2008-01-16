/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2008 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_ATR_H
#define _VGSM_ATR_H

struct vgsm_sim_atr_t0_onwire
{
#if __BYTE_ORDER == __BIG_ENDIAN
	BOOL td1:1;
	BOOL tc1:1;
	BOOL tb1:1;
	BOOL ta1:1;
	__u8 k:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 k:4;
	BOOL ta1:1;
	BOOL tb1:1;
	BOOL tc1:1;
	BOOL td1:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sim_atr_td_onwire
{
#if __BYTE_ORDER == __BIG_ENDIAN
	BOOL td:1;
	BOOL tc:1;
	BOOL tb:1;
	BOOL ta:1;
	__u8 protocol_type:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 protocol_type:4;
	BOOL ta:1;
	BOOL tb:1;
	BOOL tc:1;
	BOOL td:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sim_atr_ta1_onwire
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 fi:4;
	__u8 di:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 di:4;
	__u8 fi:4;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sim_atr_tb1_onwire
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 :1;
	__u8 ii:2;
	__u8 pi1:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 pi1:4;
	__u8 ii:2;
	__u8 :1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sim_atr_tc1_onwire
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 n; 
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 n; 
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

#endif
