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

#ifndef _VGSM_SMS_DELIVERY_H
#define _VGSM_SMS_DELIVERY_H

#include "number.h"

struct vgsm_sms_deliver_pdu
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 tp_rp:1;
	__u8 tp_udhi:1;
	__u8 tp_sri:1;
	__u8 :2;
	__u8 tp_mms:1;
	__u8 tp_mti:2;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 tp_mti:2;
	__u8 tp_mms:1;
	__u8 :2;
	__u8 tp_sri:1;
	__u8 tp_udhi:1;
	__u8 tp_rp:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct vgsm_sms_deliver
{
	int refcnt;

	struct vgsm_me *me;

	struct vgsm_number smcc_address;

	BOOL more_messages_to_send;
	BOOL reply_path;
	BOOL user_data_header_indicator;
	BOOL status_report_indication;

	struct vgsm_number originating_address;

	int coding_group;
	BOOL compressed;
	int message_class;
	enum vgsm_sms_dcs_alphabet alphabet;

	time_t timestamp;
	int timezone;

	__u8 udh_concatenate_refnum;
	__u8 udh_concatenate_maxmsg;
	__u8 udh_concatenate_seqnum;

	int pdu_len;
	int pdu_tp_len;
	void *pdu;

	wchar_t *text;
};

struct vgsm_sms_deliver *vgsm_sms_deliver_alloc(void);
struct vgsm_sms_deliver *vgsm_sms_deliver_get(struct vgsm_sms_deliver *sms);
void _vgsm_sms_deliver_put(struct vgsm_sms_deliver *sms);
#define vgsm_sms_deliver_put(sms) \
	do { _vgsm_sms_deliver_put(sms); (sms) = NULL; } while(0)

struct vgsm_sms_deliver *vgsm_sms_deliver_init_from_pdu(const char *text_pdu);
int vgsm_sms_deliver_spool(struct vgsm_sms_deliver *sms);
int vgsm_sms_deliver_manager(struct vgsm_sms_deliver *sms);
void vgsm_sms_deliver_dump(struct vgsm_sms_deliver *sms);

#endif
