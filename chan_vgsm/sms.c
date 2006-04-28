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

#include "chan_vgsm.h"
#include "util.h"
#include "sms.h"

static const char *vgsm_type_of_number_to_text(
	enum vgsm_type_of_number ton)
{
	switch(ton) {
	case VGSM_TON_UNKNOWN:
		return "Unknown";
	case VGSM_TON_INTERNATIONAL:
		return "International";
	case VGSM_TON_NATIONAL:
		return "National";
	case VGSM_TON_NETWORK_SPECIFIC:
		return "Network specific";
	case VGSM_TON_SUBSCRIBER:
		return "Subscriber";
	case VGSM_TON_ALPHANUMERIC:
		return "Alphanumeric";
	case VGSM_TON_ABBREVIATED:
		return "Abbreviated";
	case VGSM_TON_RESERVED:
		return "Reserved";
	default:
		return "*UNKNOWN*";
	}
}

static const char *vgsm_numbering_plan_to_text(
	enum vgsm_numbering_plan np)
{
	switch(np) {
	case VGSM_NP_UNKNOWN:
		return "Unknown";
	case VGSM_NP_ISDN:
		return "ISDN telephony";
	case VGSM_NP_DATA:
		return "Data";
	case VGSM_NP_TELEX:
		return "Telex";
	case VGSM_NP_NATIONAL:
		return "National";
	case VGSM_NP_PRIVATE:
		return "Private";
	case VGSM_NP_ERMES:
		return "ERMES";
	case VGSM_NP_RESERVED:
		return "Reserved";
	default:
		return "*UNKNOWN*";
	}
}

struct vgsm_sms *vgsm_sms_alloc(void)
{
	struct vgsm_sms *sms;
	sms = malloc(sizeof(*sms));
	if (!sms)
		return NULL;

	memset(sms, 0, sizeof(*sms));

	sms->refcnt = 1;

	return sms;
};

struct vgsm_sms *vgsm_sms_get(struct vgsm_sms *sms)
{
	assert(sms->refcnt > 0);

	ast_mutex_lock(&vgsm.usecnt_lock);
	sms->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return sms;
}

void vgsm_sms_put(struct vgsm_sms *sms)
{
	assert(sms->refcnt > 0);

	ast_mutex_lock(&vgsm.usecnt_lock);
	sms->refcnt--;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!sms->refcnt) {
		if (sms->text)
			free(sms->text);

		if (sms->pdu)
			free(sms->pdu);

		free(sms);
	}
}

struct vgsm_sms *vgsm_decode_sms_pdu(
	const char *text_pdu)
{
	struct vgsm_sms *sms;
	sms = vgsm_sms_alloc();
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

	if (smcc_len > sizeof(sms->smcc) - 1) {
		ast_log(LOG_ERROR,
			"Invalid message PDU: smcc_len too big (%d)\n",
			smcc_len);
		goto err_invalid_smcc;
	}

	struct vgsm_type_of_address *smcc_toa =
		(struct vgsm_type_of_address *)(pdu + pos++);

	sms->sender_ton = smcc_toa->type_of_number;
	sms->sender_np = smcc_toa->numbering_plan_identificator;

	if (vgsm_bcd_to_text(pdu + pos, (smcc_len - 1) * 2,
					sms->smcc, sizeof(sms->smcc)) < 0)
		goto err_invalid_smcc;

	pos += smcc_len - 1;

	/* Delivery PDU */

	struct vgsm_sms_deliver_pdu *sdp =
		(struct vgsm_sms_deliver_pdu *)(pdu + pos++);

	/* Sender Address */

	int sender_len = *(pdu + pos++);

	struct vgsm_type_of_address *sender_toa =
		(struct vgsm_type_of_address *)(pdu + pos++);

	sms->sender_ton = sender_toa->type_of_number;
	sms->sender_np = sender_toa->numbering_plan_identificator;

	if (vgsm_bcd_to_text(pdu + pos, sender_len,
				sms->sender, sizeof(sms->sender)) < 0)
		goto err_invalid_sender;

	pos += ((sender_len + 1) / 2);

	/* Protocol identifier */

	struct vgsm_protocol_identifier *pi =
		(struct vgsm_protocol_identifier *)(pdu + pos++);
	pi = pi; //XXX

	/* Data coding scheme */

	struct vgsm_sms_data_coding_scheme *dcs =
		(struct vgsm_sms_data_coding_scheme *)(pdu + pos++);

	/* Timestamp */

	struct tm tm;
	tm.tm_year = nibbles_to_decimal(*(pdu + pos++)) + 100;
	tm.tm_mon = nibbles_to_decimal(*(pdu + pos++)) - 1;
	tm.tm_mday = nibbles_to_decimal(*(pdu + pos++));
	tm.tm_hour = nibbles_to_decimal(*(pdu + pos++));
	tm.tm_min = nibbles_to_decimal(*(pdu + pos++));
	tm.tm_sec = nibbles_to_decimal(*(pdu + pos++));

	sms->timestamp = mktime(&tm);
	if (sms->timestamp == -1) {
		ast_log(LOG_NOTICE, "SMS timestamp is invalid\n");
		goto err_timestamp_invalid;
	}

	if (*(pdu + pos)  & 0x80)
		sms->timezone = nibbles_to_decimal(*(pdu + pos) & 0x7f);
	else
		sms->timezone = -nibbles_to_decimal(*(pdu + pos) & 0x7f);
	pos++;

	sms->timestamp -= sms->timezone;

	int tp_udl = *(pdu + pos++);

	ast_verbose(
		"New SMS: "
		"SMCC=%s/%s/%s "
		"TP-RP=%d TP-UHDI=%d TP-SRI=%d TP-MMS=%d TP-MTI=%d "
		"FROM=%s/%s/%s "
		"TIMESTAMP=%4d-%02d-%02d %02d:%02d:%02d%+03d%02d\n",
		vgsm_numbering_plan_to_text(sms->smcc_np),
		vgsm_type_of_number_to_text(sms->smcc_ton),
		sms->smcc,
		sdp->tp_rp,
		sdp->tp_udhi,
		sdp->tp_sri,
		sdp->tp_mms,
		sdp->tp_mti,
		vgsm_numbering_plan_to_text(sms->sender_np),
		vgsm_type_of_number_to_text(sms->sender_ton),
		sms->sender,
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec,
		sms->timezone / 3600, (sms->timezone / 60) % 60);

	if (dcs->coding_group == 0x0 ||
	    dcs->coding_group == 0x1 ||
	    dcs->coding_group == 0x2 ||
	    dcs->coding_group == 0x3) {
		struct vgsm_sms_data_coding_scheme_00xx *dcs2 =
			(struct vgsm_sms_data_coding_scheme_00xx *)dcs;

		ast_verbose(
			"======> DCS (group 01xx) compression=%d has_class=%d"
			" class=%d alphabet=%d\n",
			dcs2->compressed,
			dcs2->has_class,
			dcs2->message_class,
			dcs2->message_alphabet);

		if (dcs2->compressed) {
			ast_verbose(
				"Compressed messages are not supported,"
				" ignoring\n");
		} else if (dcs2->message_alphabet ==
				VGSM_SMS_DCS_ALPHABET_DEFAULT) {

			sms->text = malloc(sizeof(wchar_t) * (tp_udl + 1));
			if (!sms->text)
				goto err_sms_text_alloc;

			vgsm_7bit_to_wc(pdu + pos, tp_udl,
				sms->text, tp_udl + 1);

		} else if (dcs2->message_alphabet ==
				VGSM_SMS_DCS_ALPHABET_8_BIT_DATA) {

			/* What shall we do with 8-bit encoded msgs? */

		} else if (dcs2->message_alphabet ==
				VGSM_SMS_DCS_ALPHABET_UCS2) {

			// What if tp_udl + header != msg_len ?
			// What if tp_udl % 2  ?
			
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

			vgsm_7bit_to_wc(pdu + pos, tp_udl,
				sms->text, tp_udl + 1);
		}

	} else {
		ast_verbose("Unsupported coding group %02x, ignoring message\n",
			dcs->coding_group);

		goto err_unsupported_coding;
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
			goto err_payload_conversion;
		}

		tmpstr_p = tmpstr;
		char *mbs = malloc(len + 1);

		wcsrtombs(mbs, &tmpstr_p, len + 1, &ps);

		ast_verbose("CONTENT = '%s'\n", mbs);

		free(mbs);
	}

	return sms;

err_payload_conversion:
	if (sms->text) {
		free(sms->text);
		sms->text = NULL;
	}
err_sms_text_alloc:
err_unsupported_coding:
err_timestamp_invalid:
err_invalid_sender:
err_invalid_smcc:
	free(sms->pdu);
	sms->pdu = NULL;
err_malloc_pdu:
err_invalid_pdu:
	vgsm_sms_put(sms);
err_sms_alloc:

	return NULL;
}

void vgsm_sms_spool(struct vgsm_sms *sms)
{
	struct vgsm_interface *intf = sms->intf;
	char spooler[PATH_MAX];

	snprintf(spooler, sizeof(spooler), "%s %s",
			vgsm.sms_spooler,
			vgsm.sms_spooler_pars);

	FILE *f;
	f = popen(spooler, "w");
	if (!f) {
		ast_log(LOG_ERROR, "Cannot spawn spooler: %s\n",
			strerror(errno));
		return;
	}

	char *loc = setlocale(LC_CTYPE, "C");
	if (!loc) {
		ast_log(LOG_ERROR, "Cannot set locale: %s\n",
			strerror(errno));
		return;
	}

	struct tm tm;
	time_t tim = time(NULL);
        localtime_r(&tim, &tm);
	char tmpstr[40];
	strftime(tmpstr, sizeof(tmpstr), "%a, %d %b %Y %H:%M:%S %z", &tm);
	fprintf(f, "Received: from GSM module %s; %s\n", intf->name, tmpstr);

	fprintf(f, "From: %s@sms.orlandi.com\n", sms->sender);
	fprintf(f, "Subject: SMS message\n");
	fprintf(f, "MIME-Version: 1.0\n");
	fprintf(f, "Content-Type: text/plain\n\tcharset=\"UTF-8\"\n");
	fprintf(f, "To: <daniele@orlandi.com>\n");

        localtime_r(&sms->timestamp, &tm);
	strftime(tmpstr, sizeof(tmpstr), "%a, %d %b %Y %H:%M:%S %z", &tm);
	fprintf(f, "Date: %s\n", tmpstr);

	fprintf(f, "\n");
	
	char outbuffer[1000];
	char *inbuf = (char *)sms->text;
	char *outbuf = outbuffer;
	size_t inbytes = wcslen(sms->text) * sizeof(wchar_t);
	size_t outbytes = sizeof(outbuffer);

	iconv_t cd = iconv_open("UTF-8", "WCHAR_T");
	if (cd < 0) {
		ast_log(LOG_ERROR, "Cannot open iconv context; %s\n",
			strerror(errno));
		return;
	}

	if (iconv(cd, &inbuf, &inbytes, &outbuf, &outbytes) < 0) {
		ast_log(LOG_ERROR, "Cannot iconv; %s\n",
			strerror(errno));
		return;
	}

	iconv_close(cd);
	
	fprintf(f, "%s\n", outbuffer);

	setlocale(LC_CTYPE, loc);

	fclose(f);
}

int vgsm_sms_prepare(struct vgsm_sms *sms)
{
	if (sms->pdu)
		return -1;

	if (!sms->text)
		return -1;

	int max_len =
		(2 + (strlen(sms->smcc) + 1) / 2) +	// SMCC address
		1 +					// SMS-SUBMIT
		(2 + (strlen(sms->dest) + 1) / 2) +	// DEST address
		1 +					// Protocol Identifier
		1 +					// Data Coding Scheme
		1 +					// Validity
		1 + wcslen(sms->text) * 2;		// User data

	__u8 *pdu = malloc(max_len);
	if (!pdu)
		goto err_alloc_pdu;

	memset(pdu, 0, max_len);

	int len = 0;
	sms->pdu = pdu;

	/* SMCC address */

	*(pdu + len++) = ((strlen(sms->smcc) + 1) / 2) + 1;

	struct vgsm_type_of_address *smcc_toa =
		(struct vgsm_type_of_address *)(pdu + len++);

	smcc_toa->ext = 1;
	smcc_toa->numbering_plan_identificator = sms->smcc_np;
	smcc_toa->type_of_number = sms->smcc_ton;

	if (vgsm_text_to_bcd(pdu + len, sms->smcc) < 0)
		goto err_invalid_smcc;

	len += (strlen(sms->smcc) + 1) / 2;

	int pre_tp_len = len;

	/* SMS-SUBMIT first octect */

	struct vgsm_sms_submit_pdu *ssp =
		(struct vgsm_sms_submit_pdu *)(pdu + len++);

	ssp->tp_rp = 1;
	ssp->tp_udhi = 0;
	ssp->tp_srr = 0;
	ssp->tp_vpf = VGSM_VPF_RELATIVE_FORMAT;
	ssp->tp_rd = 0;
	ssp->tp_mti = 0x1;

	/* TP-Message-Reference */
	*(pdu + len++) = 0;

	/* Destination address */
	*(pdu + len++) = strlen(sms->dest);

	struct vgsm_type_of_address *dest_toa =
		(struct vgsm_type_of_address *)(pdu + len++);

	dest_toa->ext = 1;
	dest_toa->numbering_plan_identificator = sms->dest_np;
	dest_toa->type_of_number = sms->dest_ton;

	if (vgsm_text_to_bcd(pdu + len, sms->dest) < 0)
		goto err_invalid_dest;

	len += (strlen(sms->dest) + 1) / 2;

	/* Protocol identifier */

	struct vgsm_sms_protocol_identifier_01 *pi =
		(struct vgsm_sms_protocol_identifier_01 *)(pdu + len++);

	pi->protocol_type = VGSM_SMS_PT_MESSAGE_TYPE;
	pi->message_type = VGSM_SMS_MT_SHORT_TYPE_0;

	/* Data coding scheme */

	struct vgsm_sms_data_coding_scheme_00xx *dcs =
		(struct vgsm_sms_data_coding_scheme_00xx *)(pdu + len++);

	dcs->coding_group = 0;
	dcs->compressed = 0;
	dcs->has_class = 1;
	dcs->message_alphabet = VGSM_SMS_DCS_ALPHABET_DEFAULT;
	dcs->message_class = 0;

	/* Validity Period */
	*(pdu + len++) = 0xaa;

	/* User data */
	*(pdu + len++) = wcslen(sms->text);

	len += vgsm_wc_to_7bit(sms->text,
				wcslen(sms->text), pdu + len);
	sms->pdu_tp_len = len - pre_tp_len;

	assert(len <= max_len);

	sms->pdu_len = len;
	
	return 0;

err_invalid_dest:
err_invalid_smcc:
	free(sms->pdu);
	sms->pdu = NULL;
err_alloc_pdu:

	return -1;
}
