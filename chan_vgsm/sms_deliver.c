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
#include <asterisk/manager.h>

#include "chan_vgsm.h"
#include "util.h"
#include "sms.h"
#include "sms_deliver.h"
#include "operators.h"
#include "bcd.h"
#include "7bit.h"
#include "base64.h"
#include "gsm_charset.h"

struct vgsm_sms_deliver *vgsm_sms_deliver_alloc(void)
{
	struct vgsm_sms_deliver *sms;
	sms = malloc(sizeof(*sms));
	if (!sms)
		return NULL;

	memset(sms, 0, sizeof(*sms));

	sms->refcnt = 1;

	sms->message_class = -1;

	return sms;
};

struct vgsm_sms_deliver *vgsm_sms_deliver_get(struct vgsm_sms_deliver *sms)
{
	assert(sms->refcnt > 0);
	assert(sms->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	sms->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return sms;
}

void _vgsm_sms_deliver_put(struct vgsm_sms_deliver *sms)
{
	assert(sms->refcnt > 0);
	assert(sms->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	int refcnt = --sms->refcnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!refcnt) {
		if (sms->module)
			vgsm_module_put(sms->module);

		if (sms->text)
			free(sms->text);

		if (sms->pdu)
			free(sms->pdu);

		free(sms);
	}
}

struct vgsm_sms_deliver *vgsm_sms_deliver_init_from_pdu(
	const char *text_pdu)
{
	struct vgsm_sms_deliver *sms;
	sms = vgsm_sms_deliver_alloc();
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

	/* Delivery PDU */

	struct vgsm_sms_deliver_pdu *sdp =
		(struct vgsm_sms_deliver_pdu *)(pdu + pos++);

	sms->more_messages_to_send = sdp->tp_mms;
	sms->user_data_header_indicator = sdp->tp_udhi;
	sms->status_report_indication = sdp->tp_sri;

	/* Sender Address */

	int originating_address_len = *(pdu + pos++);

	struct vgsm_type_of_address *originating_address_toa =
		(struct vgsm_type_of_address *)(pdu + pos++);

	sms->originating_address.ton =
		originating_address_toa->type_of_number;
	sms->originating_address.np =
		originating_address_toa->numbering_plan_identificator;

	if (vgsm_bcd_to_text(pdu + pos, originating_address_len,
				sms->originating_address.digits,
				sizeof(sms->originating_address.digits)) < 0)
		goto err_invalid_originating_address;

	pos += ((originating_address_len + 1) / 2);

	/* Protocol identifier */

	struct vgsm_sms_protocol_identifier *pi =
		(struct vgsm_sms_protocol_identifier *)(pdu + pos++);
	pi = pi; //XXX

	/* Data coding scheme */

	struct vgsm_sms_data_coding_scheme *dcs =
		(struct vgsm_sms_data_coding_scheme *)(pdu + pos++);

	/* Timestamp */

	struct tm tm;
	tm.tm_year = vgsm_nibbles_to_decimal(*(pdu + pos++)) + 100;
	tm.tm_mon = vgsm_nibbles_to_decimal(*(pdu + pos++)) - 1;
	tm.tm_mday = vgsm_nibbles_to_decimal(*(pdu + pos++));
	tm.tm_hour = vgsm_nibbles_to_decimal(*(pdu + pos++));
	tm.tm_min = vgsm_nibbles_to_decimal(*(pdu + pos++));
	tm.tm_sec = vgsm_nibbles_to_decimal(*(pdu + pos++));

	sms->timestamp = mktime(&tm);
	if (sms->timestamp == -1) {
		ast_log(LOG_NOTICE, "SMS timestamp is invalid\n");
		goto err_timestamp_invalid;
	}

	if (*(pdu + pos)  & 0x80)
		sms->timezone = vgsm_nibbles_to_decimal(*(pdu + pos) & 0x7f);
	else
		sms->timezone = -vgsm_nibbles_to_decimal(*(pdu + pos) & 0x7f);
	pos++;

	sms->timestamp -= sms->timezone;

	/* User data length */

	int tp_udl = *(pdu + pos++);

	/* User data header (if present) */

	int udhl = 0;
	int udhl_septets = 0;

	if (sms->user_data_header_indicator) {

		udhl = *(pdu + pos);

		int udh_pos = pos + 1;

		/* Convert octets in septets */
		udhl_septets = vgsm_octets_to_septets(udhl + 1);

		while(udh_pos < pos + udhl) {
			__u8 iei_id = *(pdu + udh_pos++);
			__u8 iei_len = *(pdu + udh_pos++);

			ast_log(LOG_DEBUG, "IEI = %s (0x%02x), len=%d\n",
				vgsm_sms_udh_iei_to_text(iei_id), iei_id,
				iei_len);

			switch(iei_id) {
			case VGSM_SMS_UDH_IEI_CONCATENATED_SMS:
				if (iei_len < 3) {
					ast_log(LOG_WARNING,
						"Unsupported concatenated SMS"
						" information element"
						" (size < 3)\n");
					break;
				}

				sms->udh_concatenate_refnum =
						*(pdu + udh_pos);
				sms->udh_concatenate_maxmsg =
						*(pdu + udh_pos + 1);
				sms->udh_concatenate_seqnum =
						*(pdu + udh_pos + 2);

				break;
			}

			udh_pos += iei_len;
		}

		if (udh_pos - pos != udhl + 1)
			ast_log(LOG_WARNING, "UDH_POS (%d) != UDHL (%d)\n",
				udh_pos - pos, udhl + 1);
	}

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

			int text_len = tp_udl - udhl_septets;

			sms->text = malloc(sizeof(wchar_t) * (text_len + 1));
			if (!sms->text)
				goto err_sms_text_alloc;

			vgsm_7bit_to_wc(pdu + pos, text_len, udhl_septets,
				sms->text, text_len + 1);

		} else if (dcs2->message_alphabet ==
				VGSM_SMS_DCS_ALPHABET_8_BIT_DATA) {

			/* What shall we do with 8-bit encoded msgs? */

		} else if (dcs2->message_alphabet ==
				VGSM_SMS_DCS_ALPHABET_UCS2) {

			// What if tp_udl + header != msg_len ?
			// What if tp_udl % 2  ?

			int text_len = (tp_udl - udhl) / 2;
			
			sms->text = malloc(sizeof(wchar_t) * (text_len + 1));
			if (!sms->text)
				goto err_sms_text_alloc;

			int i;
			for (i=0; i<text_len; i++) {
				sms->text[i] = (*(pdu + pos + i * 2) << 8) |
						*(pdu + pos + i * 2 + 1);
			}

			sms->text[text_len] = L'\0';
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
			int text_len = tp_udl - udhl_septets;

			sms->text = malloc(sizeof(wchar_t) * (text_len + 1));
			if (!sms->text)
				goto err_sms_text_alloc;

			vgsm_7bit_to_wc(pdu + pos, text_len, udhl_septets,
				sms->text, text_len + 1);
		}

	} else {
		ast_verbose("Unsupported coding group %02x, ignoring message\n",
			dcs->coding_group);

		goto err_unsupported_coding;
	}

	return sms;

	if (sms->text) {
		free(sms->text);
		sms->text = NULL;
	}
err_sms_text_alloc:
err_unsupported_compression:
err_unsupported_coding:
err_timestamp_invalid:
err_invalid_originating_address:
err_invalid_smcc:
	free(sms->pdu);
	sms->pdu = NULL;
err_malloc_pdu:
err_invalid_pdu:
	vgsm_sms_deliver_put(sms);
err_sms_alloc:

	return NULL;
}

int vgsm_sms_deliver_spool(struct vgsm_sms_deliver *sms)
{
	struct vgsm_module *module = sms->module;
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

	ast_mutex_lock(&module->lock);
	struct vgsm_module_config *mc = module->current_config;

	fprintf(f, "Received: from GSM module %s", module->name);

	if (module->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
            module->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {
		struct vgsm_operator_info *op_info;
		op_info = vgsm_operators_search(module->net.mcc,
						module->net.mnc);

		fprintf(f,
			", registered on %03hu%02hu",
			module->net.mcc,
			module->net.mnc);

		if (op_info) {
			fprintf(f," (%s, %s)",
				op_info->name,
				op_info->country ? op_info->country->name :
								"Unknown");
		}
	}
	ast_mutex_unlock(&module->lock);

	struct tm tm;
	time_t tim = time(NULL);
        localtime_r(&tim, &tm);
	char tmpstr[40];
	strftime(tmpstr, sizeof(tmpstr), "%a, %d %b %Y %H:%M:%S %z", &tm);
	fprintf(f,"; %s\n", tmpstr);

	fprintf(f, "From: <%s%s@%s>\n",
		vgsm_number_prefix(&sms->originating_address),
		sms->originating_address.digits,
		mc->sms_sender_domain);
	fprintf(f, "Subject: SMS message\n");
	fprintf(f, "MIME-Version: 1.0\n");
	fprintf(f, "Content-Type: text/plain\n\tcharset=\"UTF-8\"\n");
	fprintf(f, "Content-Transfer-Encoding: 8bit\n");

	if (strchr(mc->sms_recipient_address, '<'))
		fprintf(f, "To: %s\n", mc->sms_recipient_address);
	else
		fprintf(f, "To: <%s>\n", mc->sms_recipient_address);

        localtime_r(&sms->timestamp, &tm);
	strftime(tmpstr, sizeof(tmpstr), "%a, %d %b %Y %H:%M:%S %z", &tm);
	fprintf(f, "Date: %s\n", tmpstr);

	fprintf(f, "X-SMS-Message-Type: SMS-DELIVER\n");
	fprintf(f, "X-SMS-Sender-NP: %s\n",
		vgsm_numbering_plan_to_text(sms->originating_address.np));
	fprintf(f, "X-SMS-Sender-TON: %s\n",
		vgsm_type_of_number_to_text(sms->originating_address.ton));
	fprintf(f, "X-SMS-Sender-Number: %s%s\n",
		vgsm_number_prefix(&sms->originating_address),
		sms->originating_address.digits);
	fprintf(f, "X-SMS-SMCC-NP: %s\n",
		vgsm_numbering_plan_to_text(sms->smcc_address.np));
	fprintf(f, "X-SMS-SMCC-TON: %s\n",
		vgsm_type_of_number_to_text(sms->smcc_address.ton));
	fprintf(f, "X-SMS-SMCC-Number: %s%s\n",
		vgsm_number_prefix(&sms->smcc_address),
		sms->smcc_address.digits);

	if (sms->message_class != -1)
		fprintf(f, "X-SMS-Class: %d\n", sms->message_class);

	fprintf(f, "X-SMS-More-Messages-To-Send: %s\n",
		sms->more_messages_to_send ? "yes" : "no");
	fprintf(f, "X-SMS-Reply-Path: %s\n", sms->reply_path ? "yes" : "no");
	fprintf(f, "X-SMS-User-Data-Header-Indicator: %s\n",
		sms->user_data_header_indicator ? "yes" : "no");
	fprintf(f, "X-SMS-Status-Report-Indication: %s\n",
		sms->status_report_indication ? "yes" : "no");

	if (sms->user_data_header_indicator &&
	    sms->udh_concatenate_maxmsg > 1) {
		fprintf(f, "X-SMS-Concatenate-RefID: %d\n",
			sms->udh_concatenate_refnum);
		fprintf(f, "X-SMS-Concatenate-Total-Messages: %d\n",
			sms->udh_concatenate_maxmsg);
		fprintf(f, "X-SMS-Concatenate-Sequence-Number: %d\n",
			sms->udh_concatenate_seqnum);
	}

	fprintf(f, "\n");

	if (sms->text) {
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
	}

	setlocale(LC_CTYPE, loc);

	fclose(f);

	return 0;
}

void vgsm_sms_deliver_dump(struct vgsm_sms_deliver *sms)
{
	ast_verbose(
		"---- Short Message: ----------\n"
		"  From = %s/%s/%s%s\n"
		"  Service Center = %s/%s/%s%s\n"
		"  More Messages To Send: %s\n"
		"  Reply Path: %s\n"
		"  User Data Header Indicator: %s\n"
		"  Status Report Indication: %s\n"
		"  Data Coding Scheme Group = %01x\n"
		"  Compression = %s\n"
		"  Class = %d\n"
		"  Alphabet = %s\n",
		vgsm_numbering_plan_to_text(sms->smcc_address.np),
		vgsm_type_of_number_to_text(sms->smcc_address.ton),
		vgsm_number_prefix(&sms->smcc_address),
		sms->smcc_address.digits,
		vgsm_numbering_plan_to_text(sms->originating_address.np),
		vgsm_type_of_number_to_text(sms->originating_address.ton),
		vgsm_number_prefix(&sms->originating_address),
		sms->originating_address.digits,
		sms->more_messages_to_send ? "Yes" : "No",
		sms->reply_path ? "Yes" : "No",
		sms->user_data_header_indicator ? "Yes" : "No",
		sms->status_report_indication ? "Yes" : "No",
		sms->coding_group,
		sms->compressed ? "Yes" : "No",
		sms->message_class,
		vgsm_sms_alphabet_to_text(sms->alphabet));

	if (sms->user_data_header_indicator &&
	    sms->udh_concatenate_maxmsg > 1) {
		ast_verbose(
			"Concatenate-RefID: %d\n"
			"Concatenate-Total-Messages: %d\n"
			"Concatenate-Sequence-Number: %d\n",
			sms->udh_concatenate_refnum,
			sms->udh_concatenate_maxmsg,
			sms->udh_concatenate_seqnum);
	}

	if (sms->text) {
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

int vgsm_sms_deliver_manager(struct vgsm_sms_deliver *sms)
{
	struct vgsm_module *module = sms->module;

	char text[2000];

	char *loc = setlocale(LC_CTYPE, "C");
	if (!loc) {
		ast_log(LOG_ERROR, "Cannot set locale: %s\r\n",
			strerror(errno));
		return -1;
	}

	ast_mutex_lock(&module->lock);
	struct vgsm_module_config *mc = module->current_config;

	sanprintf(text, sizeof(text), 
		"Received: from GSM module %s", module->name);

	if (module->net.status == VGSM_NET_STATUS_REGISTERED_HOME ||
            module->net.status == VGSM_NET_STATUS_REGISTERED_ROAMING) {
		struct vgsm_operator_info *op_info;
		op_info = vgsm_operators_search(module->net.mcc,
						module->net.mnc);

		sanprintf(text, sizeof(text),
			", registered on %03hu%02hu",
			module->net.mcc,
			module->net.mnc);

		if (op_info) {
			sanprintf(text, sizeof(text)," (%s, %s)",
				op_info->name,
				op_info->country ? op_info->country->name :
								"Unknown");
		}
	}
	ast_mutex_unlock(&module->lock);

	struct tm tm;
	time_t tim = time(NULL);
        localtime_r(&tim, &tm);
	char tmpstr[40];
	strftime(tmpstr, sizeof(tmpstr), "%a, %d %b %Y %H:%M:%S %z", &tm);
	sanprintf(text, sizeof(text),"; %s\r\n", tmpstr);

	sanprintf(text, sizeof(text), 
		"From: <%s%s@%s>\r\n",
		vgsm_number_prefix(&sms->originating_address),
		sms->originating_address.digits,
		mc->sms_sender_domain);
	sanprintf(text, sizeof(text), 
		"Subject: SMS message\r\n");
	sanprintf(text, sizeof(text), 
		"MIME-Version: 1.0\r\n");
	sanprintf(text, sizeof(text), 
		"Content-Type: text/plain; charset=\"UTF-8\"\r\n");
	sanprintf(text, sizeof(text), 
		"Content-Transfer-Encoding: base64\r\n");

        localtime_r(&sms->timestamp, &tm);
	strftime(tmpstr, sizeof(tmpstr), "%a, %d %b %Y %H:%M:%S %z", &tm);
	sanprintf(text, sizeof(text), 
		"Date: %s\r\n", tmpstr);

	sanprintf(text, sizeof(text), 
		"X-SMS-Message-Type: SMS-DELIVER\r\n");
	sanprintf(text, sizeof(text), 
		"X-SMS-Sender-NP: %s\r\n",
		vgsm_numbering_plan_to_text(sms->originating_address.np));
	sanprintf(text, sizeof(text), 
		"X-SMS-Sender-TON: %s\r\n",
		vgsm_type_of_number_to_text(sms->originating_address.ton));
	sanprintf(text, sizeof(text), 
		"X-SMS-Sender-Number: %s%s\r\n",
		vgsm_number_prefix(&sms->originating_address),
		sms->originating_address.digits);
	sanprintf(text, sizeof(text), 
		"X-SMS-SMCC-NP: %s\r\n",
		vgsm_numbering_plan_to_text(sms->smcc_address.np));
	sanprintf(text, sizeof(text), 
		"X-SMS-SMCC-TON: %s\r\n",
		vgsm_type_of_number_to_text(sms->smcc_address.ton));
	sanprintf(text, sizeof(text), 
		"X-SMS-SMCC-Number: %s%s\r\n",
		vgsm_number_prefix(&sms->smcc_address),
		sms->smcc_address.digits);

	if (sms->message_class != -1)
		sanprintf(text, sizeof(text), 
		"X-SMS-Class: %d\r\n", sms->message_class);

	sanprintf(text, sizeof(text), 
		"X-SMS-More-Messages-To-Send: %s\r\n",
		sms->more_messages_to_send ? "yes" : "no");
	sanprintf(text, sizeof(text), 
		"X-SMS-Reply-Path: %s\r\n", sms->reply_path ? "yes" : "no");
	sanprintf(text, sizeof(text), 
		"X-SMS-User-Data-Header-Indicator: %s\r\n",
		sms->user_data_header_indicator ? "yes" : "no");
	sanprintf(text, sizeof(text), 
		"X-SMS-Status-Report-Indication: %s\r\n",
		sms->status_report_indication ? "yes" : "no");

	if (sms->user_data_header_indicator &&
	    sms->udh_concatenate_maxmsg > 1) {
		sanprintf(text, sizeof(text), 
		"X-SMS-Concatenate-RefID: %d\r\n",
			sms->udh_concatenate_refnum);
		sanprintf(text, sizeof(text), 
		"X-SMS-Concatenate-Total-Messages: %d\r\n",
			sms->udh_concatenate_maxmsg);
		sanprintf(text, sizeof(text), 
		"X-SMS-Concatenate-Sequence-Number: %d\r\n",
			sms->udh_concatenate_seqnum);
	}

	if (sms->text) {
		char content[320];
		char *inbuf = (char *)sms->text;
		char *outbuf = content;
		size_t inbytes = (wcslen(sms->text) + 1) * sizeof(wchar_t);
		size_t outbytes_avail = sizeof(content);
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

		char content_base64[512];
		int outlen = sizeof(content_base64);
		base64_encode(content,
			strlen(content),
			content_base64, &outlen);

		sanprintf(text, sizeof(text),
			"Content: %s\r\n", content_base64);
	}

	manager_event(EVENT_FLAG_CALL, "SMSrx", "%s", text);

	setlocale(LC_CTYPE, loc);

	return 0;
}
