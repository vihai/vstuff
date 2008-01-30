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
#include "sms_submit.h"
#include "operators.h"
#include "bcd.h"
#include "7bit.h"
#include "gsm_charset.h"

struct vgsm_sms_submit *vgsm_sms_submit_alloc(void)
{
	struct vgsm_sms_submit *sms;
	sms = malloc(sizeof(*sms));
	if (!sms)
		return NULL;

	memset(sms, 0, sizeof(*sms));

	sms->refcnt = 1;

	sms->reject_duplicates = FALSE;
	sms->reply_path = FALSE;
	sms->user_data_header_indicator = FALSE;
	sms->status_report_request = FALSE;

	sms->message_class = 1;
	sms->validity_period = 4 * 86400;

	return sms;
};

struct vgsm_sms_submit *vgsm_sms_submit_get(struct vgsm_sms_submit *sms)
{
	assert(sms->refcnt > 0);
	assert(sms->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	sms->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return sms;
}

void _vgsm_sms_submit_put(struct vgsm_sms_submit *sms)
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

int vgsm_sms_submit_prepare(struct vgsm_sms_submit *sms)
{
	int err;

	assert(!sms->pdu);
	assert(sms->text);

	sms->alphabet = VGSM_SMS_DCS_ALPHABET_DEFAULT;
	wchar_t *c = sms->text;
	unsigned char c1, c2;

	while(*c != L'\0') {
		if (vgsm_wc_to_gsm(*c, &c1, &c2) == 0) {
			ast_log(LOG_NOTICE,
				"Cannot translate character 0x%08x to GSM "
				"alphabet, switching to UCS2\n", *(__u32 *)c);

			sms->alphabet = VGSM_SMS_DCS_ALPHABET_UCS2;
			break;
		}

		c++;
	}

	if (sms->udh_concatenate_maxmsg > 1)
		sms->user_data_header_indicator = TRUE;

	int max_len =
		(2 + (strlen(sms->smcc_address.digits) + 1) / 2) +
		160;

	__u8 *pdu = malloc(max_len);
	if (!pdu) {
		err = -ENOMEM;
		goto err_alloc_pdu;
	}

	memset(pdu, 0, max_len);

	int pos = 0;
	sms->pdu = pdu;

	/* SMCC address */

	*(pdu + pos++) = ((strlen(sms->smcc_address.digits) + 1) / 2) + 1;

	struct vgsm_type_of_address *smcc_toa =
		(struct vgsm_type_of_address *)(pdu + pos++);

	smcc_toa->ext = 1;
	smcc_toa->numbering_plan_identificator = sms->smcc_address.np;
	smcc_toa->type_of_number = sms->smcc_address.ton;

	if (vgsm_text_to_bcd(pdu + pos, sms->smcc_address.digits) < 0) {
		err = -EINVAL;
		goto err_invalid_smcc;
	}

	pos += (strlen(sms->smcc_address.digits) + 1) / 2;

	int pre_tp_len = pos;

	/* SMS-SUBMIT first octect */

	struct vgsm_sms_submit_pdu *ssp =
		(struct vgsm_sms_submit_pdu *)(pdu + pos++);

	ssp->tp_mti = VGSM_SMS_MT_SUBMIT;
	ssp->tp_rd = sms->reject_duplicates;
	ssp->tp_vpf = VGSM_VPF_RELATIVE_FORMAT;
	ssp->tp_rp = sms->reply_path;
	ssp->tp_udhi = sms->user_data_header_indicator;
	ssp->tp_srr = sms->status_report_request;

	/* TP-Message-Reference */
	*(pdu + pos++) = sms->message_reference;

	/* Destination address */
	*(pdu + pos++) = strlen(sms->dest.digits);

	struct vgsm_type_of_address *dest_toa =
		(struct vgsm_type_of_address *)(pdu + pos++);

	dest_toa->ext = 1;
	dest_toa->numbering_plan_identificator = sms->dest.np;
	dest_toa->type_of_number = sms->dest.ton;

	if (vgsm_text_to_bcd(pdu + pos, sms->dest.digits) < 0) {
		err = -EINVAL;
		goto err_invalid_dest;
	}

	pos += (strlen(sms->dest.digits) + 1) / 2;

	/* Protocol identifier */

	struct vgsm_sms_protocol_identifier_00 *pi =
		(struct vgsm_sms_protocol_identifier_00 *)(pdu + pos++);

	pi->protocol_type = VGSM_SMS_PT_INTERWORKING;
	pi->interworking = 0;

	/* Data coding scheme */

	struct vgsm_sms_data_coding_scheme_00xx *dcs =
		(struct vgsm_sms_data_coding_scheme_00xx *)(pdu + pos++);

	dcs->coding_group = 0;
	dcs->compressed = 0;
	dcs->has_class = 1;
	dcs->message_alphabet = sms->alphabet;
 	dcs->message_class = sms->message_class;

	/* Validity Period */
	if (sms->validity_period < 300)
		*(pdu + pos++) = 1;
	else if (sms->validity_period <= 43200)
		*(pdu + pos++) = (sms->validity_period / 300) - 1;
	else if (sms->validity_period <= 86400)
		*(pdu + pos++) = (sms->validity_period / 1800) - 24 + 144;
	else if (sms->validity_period <= (30 * 86400))
		*(pdu + pos++) = (sms->validity_period / 86400) - 1 + 168;
	else if (sms->validity_period <= (63 * 7 * 86400))
		*(pdu + pos++) = (sms->validity_period / (7 * 86400)) - 1 + 197;
	else
		*(pdu + pos++) = 0xff;

	/* User data length */

	__u8 *tp_udl_ptr = pdu + pos++;

	/* User data header */

	int udh_len = 0;
	int udh_len_septets = 0;

	if (sms->user_data_header_indicator) {
		int udh_pos = pos;

		__u8 *udhl_ptr = pdu + udh_pos++;

		*(pdu + udh_pos++) = VGSM_SMS_UDH_IEI_CONCATENATED_SMS;
		*(pdu + udh_pos++) = 3;
		*(pdu + udh_pos++) = sms->udh_concatenate_refnum;
		*(pdu + udh_pos++) = sms->udh_concatenate_maxmsg;
		*(pdu + udh_pos++) = sms->udh_concatenate_seqnum;

		udh_len = udh_pos - pos;
		udh_len_septets = vgsm_octets_to_septets(udh_len);
		*udhl_ptr = udh_len - 1;
	}

	if (sms->alphabet == VGSM_SMS_DCS_ALPHABET_UCS2) {

		char *inbuf = (char *)sms->text;
		char *outbuf = (char *)(pdu + pos + udh_len);
		size_t inbytes = wcslen(sms->text) * sizeof(wchar_t);
		size_t outbytes_avail = max_len - pos - udh_len - 1;
		size_t outbytes_left = outbytes_avail;

		iconv_t cd = iconv_open("UCS-2BE", "WCHAR_T");
		if (cd < 0) {
			ast_log(LOG_ERROR, "Cannot open iconv context: %s\n",
				strerror(errno));
			err = -EINVAL;
			goto err_iconv_open;
		}

		if (iconv(cd, &inbuf, &inbytes, &outbuf, &outbytes_left) < 0) {
			ast_log(LOG_ERROR, "Cannot iconv; %s\n",
				strerror(errno));
			iconv_close(cd);
			err = -EINVAL;
			goto err_iconv;
		}

		iconv_close(cd);

		*tp_udl_ptr = udh_len + outbytes_avail - outbytes_left;
		pos += outbytes_avail - outbytes_left + udh_len;
	} else {
		int used_septets = vgsm_wc_to_7bit(sms->text, wcslen(sms->text),
					pdu + pos,
					160 - udh_len_septets,
					udh_len_septets);
		if (used_septets < 0) {
			err = used_septets;
			goto err_to_7bit;
		}

		*tp_udl_ptr = udh_len_septets + used_septets;
		pos += vgsm_septets_to_octets(udh_len_septets + used_septets);
	}

	sms->pdu_tp_len = pos - pre_tp_len;

	assert(pos <= max_len);

	sms->pdu_len = pos;

	return 0;

err_to_7bit:
err_iconv:
err_iconv_open:
err_invalid_dest:
err_invalid_smcc:
	free(sms->pdu);
	sms->pdu = NULL;
err_alloc_pdu:

	return err;
}

void vgsm_sms_submit_dump(struct vgsm_sms_submit *sms)
{
	ast_verbose(
		"---- Short Message: ----------\n"
		"  To = %s/%s/%s%s\n"
		"  Service Center = %s/%s/%s%s\n"
		"  Reply Path: %s\n"
		"  User Data Header Indicator: %s\n"
		"  Status Report Request: %s\n"
		"  Data Coding Scheme Group = %01x\n"
		"  Compression = %s\n"
		"  Class = %d\n"
		"  Alphabet = %s\n",
		vgsm_numbering_plan_to_text(sms->smcc_address.np),
		vgsm_type_of_number_to_text(sms->smcc_address.ton),
		vgsm_number_prefix(&sms->smcc_address),
		sms->smcc_address.digits,
		vgsm_numbering_plan_to_text(sms->dest.np),
		vgsm_type_of_number_to_text(sms->dest.ton),
		vgsm_number_prefix(&sms->dest),
		sms->dest.digits,
		sms->reply_path ? "Yes" : "No",
		sms->user_data_header_indicator ? "Yes" : "No",
		sms->status_report_request ? "Yes" : "No",
		sms->coding_group,
		sms->compressed ? "Yes" : "No",
		sms->message_class,
		vgsm_sms_alphabet_to_text(sms->alphabet));

	if (sms->user_data_header_indicator &&
	    sms->udh_concatenate_maxmsg > 1) {
		ast_verbose(
			"  Concatenate-RefID: %d\n"
			"  Concatenate-Total-Messages: %d\n"
			"  Concatenate-Sequence-Number: %d\n",
			sms->udh_concatenate_refnum,
			sms->udh_concatenate_maxmsg,
			sms->udh_concatenate_seqnum);
	}

	if (sms->text) {
		wchar_t tmpstr[170];
		w_unprintable_remove(tmpstr, sms->text, sizeof(tmpstr));

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

