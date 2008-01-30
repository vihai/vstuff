/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2006-2008 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <linux/types.h>
#include <netinet/in.h>
#include <errno.h>
#include <locale.h>
#include <iconv.h>

#include <asterisk/lock.h>
#include <asterisk/logger.h>
#include <asterisk/cli.h>

#include "chan_vgsm.h"
#include "util.h"
#include "sms.h"
#include "sms_status_report.h"
#include "operators.h"
#include "bcd.h"
#include "7bit.h"
#include "gsm_charset.h"

const char *vgsm_sms_sr_status_to_text(__u8 value)
{
	switch(value) {
	case 0x00:
		return "Short message received by the SME";
	case 0x01:
		return "Short message forwarded by the SC to the SME but the"
			" SC is unable to confirm delivery";
	case 0x02:
		return "Short message replaced by the SC";
	case 0x20:
		return "Congestion";
	case 0x21:
		return "SME busy";
	case 0x22:
		return "No response from SME";
	case 0x23:
		return "Service rejected";
	case 0x24:
		return "Quality of service not available";
	case 0x25:
		return "Error in SME";
	case 0x40:
		return "Remote procedure error";
	case 0x41:
		return "Incompatible destination";
	case 0x42:
		return "Connection rejected by SME";
	case 0x43:
		return "Not obtainable";
	case 0x44:
		return "Quality of service not available";
	case 0x45:
		return "No interworking available";
	case 0x46:
		return "SM Validity Period Expired";
	case 0x47:
		return "SM Deleted by originating SME";
	case 0x48:
		return "SM Deleted by SC Administration";
	case 0x49:
		return "SM does not exist";
	}

	if (value >= 0x03 && value <= 0x0f)
		return "Reserved";
	else if (value >= 0x10 && value <= 0x1f)
		return "Value specific to SC";
	else if (value >= 0x26 && value <= 0x2f)
		return "Reserved";
	else if (value >= 0x30 && value <= 0x3f)
		return "Value specific to SC";
	else if (value >= 0x4a && value <= 0x4f)
		return "Reserved";
	else if (value >= 0x50 && value <= 0x5f)
		return "Value specific to SC";

	return "Reserved";
}

struct vgsm_sms_status_report *vgsm_sms_status_report_alloc(void)
{
	struct vgsm_sms_status_report *sms;
	sms = malloc(sizeof(*sms));
	if (!sms)
		return NULL;

	memset(sms, 0, sizeof(*sms));

	sms->refcnt = 1;

	return sms;
};

struct vgsm_sms_status_report *vgsm_sms_status_report_get(struct vgsm_sms_status_report *sms)
{
	assert(sms->refcnt > 0);
	assert(sms->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	sms->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return sms;
}

void _vgsm_sms_status_report_put(struct vgsm_sms_status_report *sms)
{
	assert(sms->refcnt > 0);
	assert(sms->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	int refcnt = --sms->refcnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!refcnt) {
		if (sms->me)
			vgsm_me_put(sms->me);

		if (sms->text)
			free(sms->text);

		if (sms->pdu)
			free(sms->pdu);

		free(sms);
	}
}

struct vgsm_sms_status_report *vgsm_sms_status_report_init_from_pdu(
	const char *text_pdu)
{
	struct vgsm_sms_status_report *sms;
	sms = vgsm_sms_status_report_alloc();
	if (!sms)
		goto err_sms_alloc;

	if (strlen(text_pdu) % 2) {
		ast_log(LOG_NOTICE, "PDU string invalid");
		goto err_invalid_pdu;
	}

	sms->pdu_len = strlen(text_pdu) / 2;
	sms->pdu = malloc(sms->pdu_len);
	if (!sms->pdu)
		goto err_malloc_pdu;

	__u8 *pdu = sms->pdu;

	int i;
	for(i=0; i<sms->pdu_len; i++) {
		pdu[i] = char_to_hexdigit(text_pdu[i * 2]) << 4 |
			 char_to_hexdigit(text_pdu[i * 2 + 1]);
	}

	int pos = 0;

	/* SMCC Address */

	int smcc_len = *(pdu + pos++);

	if (smcc_len > sms->pdu_len - pos) {
		ast_log(LOG_ERROR,
			"Invalid message PDU: smcc_len > len\n");
		goto err_invalid_smcc;
	}

	if (smcc_len > sizeof(sms->smcc_address.digits) - 1) {
		ast_log(LOG_ERROR,
			"Invalid message PDU: smcc_len too big (%d)\n",
			smcc_len);
		goto err_invalid_smcc;
	}

	struct vgsm_type_of_address *smcc_toa =
		(struct vgsm_type_of_address *)(pdu + pos++);

	sms->smcc_address.ton = smcc_toa->type_of_number;
	sms->smcc_address.np = smcc_toa->numbering_plan_identificator;

	if (vgsm_bcd_to_text(pdu + pos, (smcc_len - 1) * 2,
			sms->smcc_address.digits,
			sizeof(sms->smcc_address.digits)) < 0)
		goto err_invalid_smcc;

	pos += smcc_len - 1;

	/* First Octet */

	struct vgsm_sms_status_report_pdu *sdp =
		(struct vgsm_sms_status_report_pdu *)(pdu + pos++);

	sms->user_data_header_indicator = sdp->tp_udhi;
	sms->more_messages_to_send = sdp->tp_mms;
	sms->status_report_qualifier = sdp->tp_srq;

	/* Message reference */

	sms->message_reference = *(__u8 *)(pdu + pos++);

	/* Sender Address */

	int recipient_address_len = *(pdu + pos++);

	struct vgsm_type_of_address *recipient_address_toa =
		(struct vgsm_type_of_address *)(pdu + pos++);

	sms->recipient_address.ton = recipient_address_toa->type_of_number;
	sms->recipient_address.np =
		recipient_address_toa->numbering_plan_identificator;

	if (vgsm_bcd_to_text(pdu + pos, recipient_address_len,
				sms->recipient_address.digits,
				sizeof(sms->recipient_address.digits)) < 0)
		goto err_invalid_recipient_address;

	pos += ((recipient_address_len + 1) / 2);

	/* Service Center Timestamp */

	struct tm tm;
	tm.tm_year = vgsm_nibbles_to_decimal(*(pdu + pos++)) + 100;
	tm.tm_mon = vgsm_nibbles_to_decimal(*(pdu + pos++)) - 1;
	tm.tm_mday = vgsm_nibbles_to_decimal(*(pdu + pos++));
	tm.tm_hour = vgsm_nibbles_to_decimal(*(pdu + pos++));
	tm.tm_min = vgsm_nibbles_to_decimal(*(pdu + pos++));
	tm.tm_sec = vgsm_nibbles_to_decimal(*(pdu + pos++));

	sms->smcc_timestamp = mktime(&tm);
	if (sms->smcc_timestamp == -1) {
		ast_log(LOG_NOTICE, "SMS smcc_timestamp is invalid\n");
		goto err_smcc_timestamp_invalid;
	}

	if (*(pdu + pos) & 0x80)
		sms->smcc_timestamp_tz =
			vgsm_nibbles_to_decimal(*(pdu + pos) & 0x7f);
	else
		sms->smcc_timestamp_tz =
			-vgsm_nibbles_to_decimal(*(pdu + pos) & 0x7f);
	pos++;

	sms->smcc_timestamp -= sms->smcc_timestamp_tz;

	/* Discharge Time */

	tm.tm_year = vgsm_nibbles_to_decimal(*(pdu + pos++)) + 100;
	tm.tm_mon = vgsm_nibbles_to_decimal(*(pdu + pos++)) - 1;
	tm.tm_mday = vgsm_nibbles_to_decimal(*(pdu + pos++));
	tm.tm_hour = vgsm_nibbles_to_decimal(*(pdu + pos++));
	tm.tm_min = vgsm_nibbles_to_decimal(*(pdu + pos++));
	tm.tm_sec = vgsm_nibbles_to_decimal(*(pdu + pos++));

	sms->discharge_time = mktime(&tm);
	if (sms->discharge_time == -1) {
		ast_log(LOG_NOTICE, "SMS discharge_time is invalid\n");
		goto err_discharge_time_invalid;
	}

	if (*(pdu + pos) & 0x80)
		sms->discharge_time_tz =
			vgsm_nibbles_to_decimal(*(pdu + pos) & 0x7f);
	else
		sms->discharge_time_tz =
			-vgsm_nibbles_to_decimal(*(pdu + pos) & 0x7f);
	pos++;

	sms->discharge_time -= sms->discharge_time_tz;

	/* Status */

	sms->status = *(__u8 *)(pdu + pos++);

	assert(pos <= sms->pdu_len);

	if (pos == sms->pdu_len)
		goto done;

	/* Parameter Indicator */

	sms->parameter_indicator = *(__u8 *)(pdu + pos++);

	/* Protocol identifier */

	if (!(sms->parameter_indicator & 0x01))
		goto done;

	struct vgsm_sms_protocol_identifier *pi =
		(struct vgsm_sms_protocol_identifier *)(pdu + pos++);
	pi = pi; //XXX

	/* Data coding scheme */

	if (!(sms->parameter_indicator & 0x02))
		goto done;

	struct vgsm_sms_data_coding_scheme *dcs =
		(struct vgsm_sms_data_coding_scheme *)
		(pdu + pos++);

	if (!(sms->parameter_indicator & 0x04))
		goto done;

	/* User Data Length */

	int tp_udl = *(pdu + pos++);

	sms->coding_group = dcs->coding_group;
	sms->compressed = FALSE;
	sms->message_class = -1;

	if (dcs->coding_group == 0x0 ||
	    dcs->coding_group == 0x1 ||
	    dcs->coding_group == 0x2 ||
	    dcs->coding_group == 0x3) {
		struct vgsm_sms_data_coding_scheme_00xx *dcs2 =
			(struct vgsm_sms_data_coding_scheme_00xx *)dcs;

		sms->compressed = dcs2->compressed;
		sms->alphabet = dcs2->message_alphabet;

		if (dcs2->has_class)
			sms->message_class = dcs2->message_class;
		else
			sms->message_class = -1;

		if (dcs2->compressed) {
			ast_verbose(
				"Compressed messages are not supported,"
				" ignoring\n");

			goto err_unsupported_compression;

		} else if (dcs2->message_alphabet ==
				VGSM_SMS_DCS_ALPHABET_DEFAULT) {

			sms->text = malloc(sizeof(wchar_t) * (tp_udl + 1));
			if (!sms->text)
				goto err_sms_text_alloc;

			vgsm_7bit_to_wc(pdu + pos, tp_udl, 0,
				sms->text, tp_udl + 1);

		} else if (dcs2->message_alphabet ==
				VGSM_SMS_DCS_ALPHABET_8_BIT_DATA) {

			/* What shall we do with 8-bit encoded msgs? */

		} else if (dcs2->message_alphabet ==
				VGSM_SMS_DCS_ALPHABET_UCS2) {

			// What if tp_udl + header != msg_len ?
			// What if tp_udl % 2 ?
			
			sms->text = malloc(sizeof(wchar_t) * (tp_udl / 2 + 1));
			if (!sms->text)
				goto err_sms_text_alloc;

			int i;
			for (i=0; i<tp_udl / 2; i++) {
				sms->text[i] = (*(pdu + pos + i * 2) << 8) |
						*(pdu + pos + i * 2 + 1);
			}

			sms->text[tp_udl / 2] = L'\0';
		}

	} else if (dcs->coding_group == 0xc) {

		struct vgsm_sms_data_coding_scheme_110x *dcs2 =
			(struct vgsm_sms_data_coding_scheme_110x *)dcs;

		ast_verbose(
			"======> DCS (group 110x) store=%d ind_active=%d"
			" ind_type=%d",
			dcs2->store,
			dcs2->mwi_active,
			dcs2->indication_type);

	} else if (dcs->coding_group == 0xe) {

		struct vgsm_sms_data_coding_scheme_110x *dcs2 =
			(struct vgsm_sms_data_coding_scheme_110x *)dcs;

		ast_verbose(
			"======> DCS (group 110x) store=%d ind_active=%d"
			" ind_type=%d",
			dcs2->store,
			dcs2->mwi_active,
			dcs2->indication_type);

	} else if (dcs->coding_group == 0xf) {

		struct vgsm_sms_data_coding_scheme_1111 *dcs2 =
			(struct vgsm_sms_data_coding_scheme_1111 *)dcs;

		ast_verbose("=====> DCS (group 1111) coding=%s class=%d\n",
			dcs2->message_coding ? "8-bit data" : "Default",
			dcs2->message_class);

		if (dcs2->message_coding) {

			/* What shall we do with 8-bit encoded msgs? */

		} else {
			sms->text = malloc(sizeof(wchar_t) * (tp_udl + 1));
			if (!sms->text)
				goto err_sms_text_alloc;

			vgsm_7bit_to_wc(pdu + pos, tp_udl, 0,
				sms->text, tp_udl + 1);
		}

	} else {
//		ast_verbose("Unsupported coding group %02x, ignoring message\n",
//			dcs->coding_group);

//		goto err_unsupported_coding;
	}

done:

	return sms;

	if (sms->text) {
		free(sms->text);
		sms->text = NULL;
	}
err_sms_text_alloc:
//err_unsupported_coding:
err_unsupported_compression:
err_discharge_time_invalid:
err_smcc_timestamp_invalid:
err_invalid_recipient_address:
err_invalid_smcc:
	free(sms->pdu);
	sms->pdu = NULL;
err_malloc_pdu:
err_invalid_pdu:
	vgsm_sms_status_report_put(sms);
err_sms_alloc:

	return NULL;
}

int vgsm_sms_status_report_spool(struct vgsm_sms_status_report *sms)
{
	struct vgsm_me *me = sms->me;
	char spooler[PATH_MAX];

	snprintf(spooler, sizeof(spooler), "%s %s",
			vgsm.sms_spooler,
			vgsm.sms_spooler_pars);

	FILE *f;
	f = popen(spooler, "w");
	if (!f) {
		ast_log(LOG_ERROR, "Cannot spawn spooler: %s\n",
			strerror(errno));
		return -1;
	}

	char *loc = setlocale(LC_CTYPE, "C");
	if (!loc) {
		ast_log(LOG_ERROR, "Cannot set locale: %s\n",
			strerror(errno));
		return -1;
	}

	ast_mutex_lock(&me->lock);
	struct vgsm_me_config *mc = me->current_config;

	fprintf(f, "Received: from GSM me %s", me->name);

	if (me->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
	    me->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {
		struct vgsm_operator_info *op_info;
		op_info = vgsm_operators_search(me->net.mcc,
						me->net.mnc);

		fprintf(f,
			", registered on %03hu%02hu",
			me->net.mcc,
			me->net.mnc);

		if (op_info) {
			fprintf(f," (%s, %s)\n",
				op_info->name,
				op_info->country ? op_info->country->name :
								"Unknown");
		}
	}
	ast_mutex_unlock(&me->lock);

	struct tm tm;
	time_t tim = time(NULL);
	ast_localtime(&tim, &tm, NULL);
	char tmpstr[40];
	strftime(tmpstr, sizeof(tmpstr), "%a, %d %b %Y %H:%M:%S %z", &tm);
	fprintf(f,"; %s \n", tmpstr);

	fprintf(f, "From: <%s%s@%s>\n",
		vgsm_number_prefix(&sms->recipient_address),
		sms->recipient_address.digits,
		mc->sms_sender_domain);
	fprintf(f, "Subject: SMS Status Report\n");
	fprintf(f, "MIME-Version: 1.0\n");
	fprintf(f, "Content-Type: text/plain\n\tcharset=\"UTF-8\"\n");
	fprintf(f, "Content-Transfer-Encoding: 8bit\n");

	if (strchr(mc->sms_recipient_address, '<'))
		fprintf(f, "To: %s\n", mc->sms_recipient_address);
	else
		fprintf(f, "To: <%s>\n", mc->sms_recipient_address);

	ast_localtime(&sms->smcc_timestamp, &tm, NULL);
	strftime(tmpstr, sizeof(tmpstr), "%a, %d %b %Y %H:%M:%S %z", &tm);
	fprintf(f, "Date: %s\n", tmpstr);

	fprintf(f, "X-SMS-Message-Type: SMS-STATUS-REPORT\n");
	fprintf(f, "X-SMS-Message-Reference: %d\n", sms->message_reference);

	ast_localtime(&sms->discharge_time, &tm, NULL);
	strftime(tmpstr, sizeof(tmpstr), "%a, %d %b %Y %H:%M:%S %z", &tm);
	fprintf(f, "X-SMS-Discharge-Time: %s\n", tmpstr);

	fprintf(f, "X-SMS-Recipient-NP: %s\n",
		vgsm_numbering_plan_to_text(sms->recipient_address.np));
	fprintf(f, "X-SMS-Recipient-TON: %s\n",
		vgsm_type_of_number_to_text(sms->recipient_address.ton));
	fprintf(f, "X-SMS-Recipient-Address: %s%s\n",
		vgsm_number_prefix(&sms->recipient_address),
		sms->recipient_address.digits);

	fprintf(f, "X-SMS-SMCC-NP: %s\n",
		vgsm_numbering_plan_to_text(sms->smcc_address.np));
	fprintf(f, "X-SMS-SMCC-TON: %s\n",
		vgsm_type_of_number_to_text(sms->smcc_address.ton));
	fprintf(f, "X-SMS-SMCC-Number: %s%s\n",
		vgsm_number_prefix(&sms->smcc_address),
		sms->smcc_address.digits);
	fprintf(f, "X-SMS-More-Messages-To-Send: %s\n",
		sms->more_messages_to_send ? "yes" : "no");
	fprintf(f, "X-SMS-User-Data-Header-Indicator: %s\n",
		sms->user_data_header_indicator ? "yes" : "no");
	fprintf(f, "X-SMS-Status-Report-Qualifier: %s\n",
		sms->status_report_qualifier ? "SMS-COMMAND" : "SMS-SUBMIT");

	if (sms->parameter_indicator & 0x02)
		fprintf(f, "X-SMS-Class: %d\n", sms->message_class);

	fprintf(f, "\n");

	if ((sms->parameter_indicator & 0x04) && sms->text) {
		char outbuffer[1000];
		char *inbuf = (char *)sms->text;
		char *outbuf = outbuffer;
		size_t inbytes = (wcslen(sms->text) + 1) * sizeof(wchar_t);
		size_t outbytes_avail = sizeof(outbuffer);
		size_t outbytes_left = outbytes_avail;

		iconv_t cd = iconv_open("UTF-8", "WCHAR_T");
		if (cd < 0) {
			ast_log(LOG_ERROR, "Cannot open iconv context; %s\n",
				strerror(errno));
			return -1;
		}

		if (iconv(cd, &inbuf, &inbytes, &outbuf, &outbytes_left) < 0) {
			ast_log(LOG_ERROR, "Cannot iconv; %s\n",
				strerror(errno));
			return -1;
		}

		iconv_close(cd);
	
		fprintf(f, "%s\n", outbuffer);
	} else {
		fprintf(f, "Message successfully delivered\n");
	}

	setlocale(LC_CTYPE, loc);

	fclose(f);

	return 0;
}

void vgsm_sms_status_report_dump(struct vgsm_sms_status_report *sms)
{
	char smcc_timestamp[26];
	ctime_r(&sms->smcc_timestamp, smcc_timestamp);
	char discharge_time[26];
	ctime_r(&sms->discharge_time, discharge_time);

	ast_verbose(
		"---- Status Report: ----------\n"
		"  To = %s/%s/%s%s\n"
		"  Service Center = %s/%s/%s%s\n"
		"  User Data Header Indicator: %s\n"
		"  More Messages To Send: %s\n"
		"  Status Report Qualifier = %s\n"
		"  Message Reference = %d\n"
		"  SMCC Timestamp = %s"
		"  Discharge Time = %s"
		"  Status = %s (%02x)\n"
		"  Parameter Indicator = %02x\n",
		vgsm_numbering_plan_to_text(sms->recipient_address.np),
		vgsm_type_of_number_to_text(sms->recipient_address.ton),
		vgsm_number_prefix(&sms->recipient_address),
		sms->recipient_address.digits,
		vgsm_numbering_plan_to_text(sms->smcc_address.np),
		vgsm_type_of_number_to_text(sms->smcc_address.ton),
		vgsm_number_prefix(&sms->smcc_address),
		sms->smcc_address.digits,
		sms->user_data_header_indicator ? "Yes" : "No",
		sms->more_messages_to_send ? "Yes" : "No",
		sms->status_report_qualifier ? "SMS-COMMAND" : "SMS-SUBMIT",
		sms->message_reference,
		smcc_timestamp,
		discharge_time,
		vgsm_sms_sr_status_to_text(sms->status),
		sms->status,
		sms->parameter_indicator);

	if (sms->parameter_indicator & 0x02) {
		ast_verbose(
			"  Data Coding Scheme Group = %01x\n"
			"  Compression = %s\n"
			"  Class = %d\n"
			"  Alphabet = %s\n",
			sms->coding_group,
			sms->compressed ? "Yes" : "No",
			sms->message_class,
			vgsm_sms_alphabet_to_text(sms->alphabet));
	}

	if ((sms->parameter_indicator & 0x04) && sms->text) {
		wchar_t tmpstr[170];
		w_unprintable_remove(tmpstr, sms->text, ARRAY_SIZE(tmpstr));

		const wchar_t *tmpstr_p = tmpstr;
		mbstate_t ps = {};

		int len = wcsrtombs(NULL, &tmpstr_p, 0, &ps);
		if (len < 0) {
			ast_log(LOG_ERROR, "Error converting string: %s\n",
				strerror(errno));
			return;
		}

		tmpstr_p = tmpstr;
		char *mbs = malloc(len + 1);

		wcsrtombs(mbs, &tmpstr_p, len + 1, &ps);

		ast_verbose(" '%s'\n", mbs);

		free(mbs);
	}

	ast_verbose("-------------------------------\n");
}

