/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_SMS_STATUS_REPORT_H
#define _VGSM_SMS_STATUS_REPORT_H

#include "number.h"

struct vgsm_sms_status_report_pdu
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 :3;
	__u8 tp_srq:1;
	__u8 tp_mms:1;
	__u8 tp_udhi:1;
	__u8 tp_mti:2;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 tp_mti:2;
	__u8 tp_udhi:1;
	__u8 tp_mms:1;
	__u8 tp_srq:1;
	__u8 :3;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sms_status_report
{
	int refcnt;

	struct vgsm_me *me;

	struct vgsm_number smcc_address;

	BOOL user_data_header_indicator;
	BOOL more_messages_to_send;
	BOOL status_report_qualifier;

	__u8 message_reference;

	struct vgsm_number recipient_address;

	time_t smcc_timestamp;
	int smcc_timestamp_tz;

	time_t discharge_time;
	int discharge_time_tz;

	__u8 status;
	__u8 parameter_indicator;

	// TP Protocol Identifier
	// TP Data Coding Scheme

	int coding_group;
	BOOL compressed;
	int message_class;
	enum vgsm_sms_dcs_alphabet alphabet;

	wchar_t *text;

	int pdu_len;
	int pdu_tp_len;
	void *pdu;
};

struct vgsm_sms_status_report *vgsm_sms_status_report_alloc(void);
struct vgsm_sms_status_report *vgsm_sms_status_report_get(
	struct vgsm_sms_status_report *sms);
void _vgsm_sms_status_report_put(struct vgsm_sms_status_report *sms);
#define vgsm_sms_status_report_put(sms) \
	do { _vgsm_sms_status_report_put(sms); (sms) = NULL; } while(0)

int vgsm_sms_status_report_spool(struct vgsm_sms_status_report *sms);
struct vgsm_sms_status_report *vgsm_sms_status_report_init_from_pdu(
	const char *text_pdu);
void vgsm_sms_status_report_dump(struct vgsm_sms_status_report *sms);

#endif
