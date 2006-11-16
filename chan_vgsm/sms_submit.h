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

#ifndef _VGSM_SMS_SUBMIT_H
#define _VGSM_SMS_SUBMIT_H

#include "number.h"
#include "sms.h"

struct vgsm_sms_submit_pdu
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 tp_rp:1;
	__u8 tp_udhi:1;
	__u8 tp_srr:1;
	__u8 tp_vpf:2;
	__u8 tp_rd:1;
	__u8 tp_mti:2;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 tp_mti:2;
	__u8 tp_rd:1;
	__u8 tp_vpf:2;
	__u8 tp_srr:1;
	__u8 tp_udhi:1;
	__u8 tp_rp:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sms_submit
{
	int refcnt;

	struct vgsm_module *module;

	struct vgsm_number smcc_address;

	BOOL reject_duplicates;
	BOOL reply_path;
	BOOL user_data_header_indicator;
	BOOL status_report_request;

	__u8 message_reference;

	struct vgsm_number dest;

	time_t validity_period;

	int coding_group;
	BOOL compressed;
	int message_class;
	enum vgsm_sms_dcs_alphabet alphabet;

	__u8 udh_concatenate_refnum;
	__u8 udh_concatenate_maxmsg;
	__u8 udh_concatenate_seqnum;

	wchar_t *text;

	int pdu_len;
	int pdu_tp_len;
	void *pdu;
};

struct vgsm_sms_submit *vgsm_sms_submit_alloc(void);
struct vgsm_sms_submit *vgsm_sms_submit_get(struct vgsm_sms_submit *sms);
void _vgsm_sms_submit_put(struct vgsm_sms_submit *sms);
#define vgsm_sms_submit_put(sms) \
	do { _vgsm_sms_submit_put(sms); (sms) = NULL; } while(0)

struct vgsm_sms_submit *vgsm_decode_sms_pdu(const char *text_pdu);
int vgsm_sms_submit_prepare(struct vgsm_sms_submit *sms);
void vgsm_sms_submit_dump(struct vgsm_sms_submit *sms);

#endif
