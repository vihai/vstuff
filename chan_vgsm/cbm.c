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

#include <asterisk/lock.h>
#include <asterisk/logger.h>

#include "chan_vgsm.h"
#include "util.h"
#include "cbm.h"

static const char *vgsm_cbm_serial_geoscope_to_text(
	enum vgsm_cbm_serial_geoscopes gs)
{
	switch(gs) {
	case VGSM_CBM_GS_IMMEDIATE_CELL:
		return "Immediate/Cell";
	case VGSM_CBM_GS_NORMAL_PLMN:
		return "Normal/PLMN";
	case VGSM_CBM_GS_NORMAL_LOCATION_AREA:
		return "Normal/Location Area";
	case VGSM_CBM_GS_NORMAL_CELL:
		return "Normal/Cell";
	default:
		return "*UNKNOWN*";
	}
}

struct vgsm_cbm *vgsm_cbm_alloc(void)
{
	struct vgsm_cbm *cbm;
	cbm = malloc(sizeof(*cbm));
	if (!cbm)
		return NULL;

	memset(cbm, 0, sizeof(*cbm));

	cbm->refcnt = 1;

	return cbm;
};

struct vgsm_cbm *vgsm_cbm_get(struct vgsm_cbm *cbm)
{
	assert(cbm->refcnt > 0);

	ast_mutex_lock(&vgsm.usecnt_lock);
	cbm->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return cbm;
}

void vgsm_cbm_put(struct vgsm_cbm *cbm)
{
	assert(cbm->refcnt > 0);

	ast_mutex_lock(&vgsm.usecnt_lock);
	cbm->refcnt--;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!cbm->refcnt) {
		if (cbm->text)
			free(cbm->text);

		if (cbm->pdu)
			free(cbm->pdu);

		free(cbm);
	}
}

struct vgsm_cbm *vgsm_decode_cbm_pdu(
	const char *text_pdu)
{
	struct vgsm_cbm *cbm;
	cbm = vgsm_cbm_alloc();
	if (!cbm)
		goto err_cbm_alloc;

	if (strlen(text_pdu) % 2) {
		ast_log(LOG_NOTICE, "PDU string invalid");
		goto err_invalid_pdu;
	}

	cbm->pdu_len = strlen(text_pdu) / 2;
	cbm->pdu = malloc(cbm->pdu_len);
	if (!cbm->pdu)
		goto err_malloc_pdu;

	__u8 *pdu = cbm->pdu;

	int i;
	for(i=0; i<cbm->pdu_len; i++) {
		pdu[i] = char_to_hexdigit(text_pdu[i * 2]) << 4 |
		         char_to_hexdigit(text_pdu[i * 2 + 1]);
	}

	int pos = 0;

	/* Serial */

	struct vgsm_cbm_serial_number *serial =
		(struct vgsm_cbm_serial_number *)(pdu + pos);
	pos += sizeof(struct vgsm_cbm_serial_number);
	cbm->geoscope = serial->geoscope;
	cbm->message_code = serial->message_code;
	cbm->update_number = serial->update_number;

	/* Message-id */

	cbm->message_id = ntohs(*((__u16 *)(pdu + pos)));
	pos += sizeof(__u16);

	/* Data coding scheme */

	struct vgsm_cbm_data_coding_scheme *dcs =
		(struct vgsm_cbm_data_coding_scheme *)(pdu + pos++);

	/* Page Parameter */

	struct vgsm_cbm_page_parameter *pp =
		(struct vgsm_cbm_page_parameter *)(pdu + pos++);

	if (cbm->page == 0 || cbm->pages == 0) {
		cbm->page = 1;
		cbm->pages = 1;
	} else {
		cbm->page = pp->page;
		cbm->pages = pp->pages;
	}

	/* Content */

	ast_verbose("Cell broadcast: GS=%s CODE=%d UPDATE=%d"
			" MSGID=%d PAGE=%d/%d\n",
		vgsm_cbm_serial_geoscope_to_text(cbm->geoscope),
		cbm->message_code,
		cbm->update_number,
		cbm->message_id,
		cbm->page, cbm->pages);

	wchar_t content[100];

	if (dcs->coding_group == 0x0) {
		struct vgsm_cbm_data_coding_scheme_0000 *dcs2 =
			(struct vgsm_cbm_data_coding_scheme_0000 *)dcs;


		ast_verbose("  DCS (group 0000) Language = %d\n",
			dcs2->language);

		cbm->text = malloc(sizeof(wchar_t) * (82 + 1));
		if (!cbm->text)
			goto err_cbm_text_alloc;

		vgsm_7bit_to_wc(pdu + pos, 82,
			cbm->text, 82 + 1);

	} else if (dcs->coding_group == 0x4 ||
	           dcs->coding_group == 0x5 ||
	           dcs->coding_group == 0x6 ||
	           dcs->coding_group == 0x7) {

		struct vgsm_cbm_data_coding_scheme_01xx *dcs2 =
			(struct vgsm_cbm_data_coding_scheme_01xx *)dcs;

		ast_verbose("  DCS (group 01xx) compression=%d",
			dcs2->compressed);

		if (dcs2->has_class)
			ast_verbose(" class=%d", dcs2->message_class);

		ast_verbose(" alphabet=%d\n", dcs2->message_alphabet);

		if (dcs2->compressed) {
			ast_verbose(
				"Compressed messages are not supported,"
				" ignoring\n");
		} else if (dcs2->message_alphabet ==
				VGSM_CBM_DCS_ALPHABET_DEFAULT) {

			cbm->text = malloc(sizeof(wchar_t) * (82 + 1));
			if (!cbm->text)
				goto err_cbm_text_alloc;

			vgsm_7bit_to_wc(pdu + pos, 82,
				cbm->text, 82 + 1);

		} else if (dcs2->message_alphabet ==
				VGSM_CBM_DCS_ALPHABET_8_BIT_DATA) {

			/* What shall we do with 8-bit encoded msgs? */

		} else if (dcs2->message_alphabet ==
				VGSM_CBM_DCS_ALPHABET_UCS2) {

			cbm->text = malloc(sizeof(wchar_t) * (82 + 1));
			if (!cbm->text)
				goto err_cbm_text_alloc;

			int i;
			for (i=0; i<82; i++) {
				content[i] = (*(pdu + pos + i * 2) << 8) |
						*(pdu + pos + i * 2 + 1);
			}

			content[82] = L'\0';
		}
	} else if (dcs->coding_group == 0xe) {

		ast_verbose("=====> DCS (group 1110) not supported, sorry\n");

		goto err_unsupported_coding;

	} else if (dcs->coding_group == 0xf) {

		struct vgsm_cbm_data_coding_scheme_1111 *dcs2 =
			(struct vgsm_cbm_data_coding_scheme_1111 *)dcs;

		ast_verbose("=====> DCS (group 1111) coding=%s class=%d\n",
			dcs2->message_coding ? "8-bit data" : "Default",
			dcs2->message_class);

		if (dcs2->message_coding) {

			/* What shall we do with 8-bit encoded msgs? */

		} else {
			cbm->text = malloc(sizeof(wchar_t) * (82 + 1));
			if (!cbm->text)
				goto err_cbm_text_alloc;

			vgsm_7bit_to_wc(pdu + pos, 82,
				cbm->text, 82 + 1);
		}
	} else {
		ast_verbose("Unsupported coding group %02x, ignoring message\n",
			dcs->coding_group);

		goto err_unsupported_coding;
	}

	if (cbm->text) {
		wchar_t tmpstr[170];
		w_unprintable_remove(tmpstr, cbm->text, ARRAY_SIZE(tmpstr));

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

	return cbm;

err_payload_conversion:
	if (cbm->text) {
		free(cbm->text);
		cbm->text = NULL;
	}
err_cbm_text_alloc:
err_unsupported_coding:
	free(cbm->pdu);
	cbm->pdu = NULL;
err_malloc_pdu:
err_invalid_pdu:
	vgsm_cbm_put(cbm);
err_cbm_alloc:

	return NULL;
}
