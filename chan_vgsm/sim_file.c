/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2007 Daniele Orlandi
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
#include "sim_file.h"

static const char *vgsm_sim_type_of_file_to_text(
		enum vgsm_sim_type_of_file value)
{
	switch(value) {
	case VGSM_SIM_TOF_RFU:
		return "RFU";
	case VGSM_SIM_TOF_MF:
		return "MF";
	case VGSM_SIM_TOF_DF:
		return "DF";
	case VGSM_SIM_TOF_EF:
		return "EF";
	}

	return "*INVALID*";
}

const char *vgsm_sim_language_to_text(enum vgsm_sim_dcs_language value)
{
	switch(value) {
	case VGSM_SIM_DCS_LANG_GERMAN:
		return "german";
	case VGSM_SIM_DCS_LANG_ENGLISH:
		return "english";
	case VGSM_SIM_DCS_LANG_ITALIAN:
		return "italian";
	case VGSM_SIM_DCS_LANG_FRENCH:
		return "french";
	case VGSM_SIM_DCS_LANG_SPANISH:
		return "spanish";
	case VGSM_SIM_DCS_LANG_DUTCH:
		return "dutch";
	case VGSM_SIM_DCS_LANG_SWEDISH:
		return "swedish";
	case VGSM_SIM_DCS_LANG_DANISH:
		return "danish";
	case VGSM_SIM_DCS_LANG_PORTUGUESE:
		return "portuguese";
	case VGSM_SIM_DCS_LANG_FINNISH:
		return "finnish";
	case VGSM_SIM_DCS_LANG_NORWEGIAN:
		return "norwegian";
	case VGSM_SIM_DCS_LANG_GREEK:
		return "greek";
	case VGSM_SIM_DCS_LANG_TURKISH:
		return "turkish";
	case VGSM_SIM_DCS_LANG_HUNGARIAN:
		return "hungarian";
	case VGSM_SIM_DCS_LANG_POLISH:
		return "polish";
	case VGSM_SIM_DCS_LANG_UNSPECIFIED:
		return "unspecified";
	}

	return "*INVALID*";
};

struct vgsm_sim_file *vgsm_sim_file_alloc(struct vgsm_sim *sim)
{
	struct vgsm_sim_file *sim_file;

	sim_file = malloc(sizeof(*sim_file));
	if (!sim_file)
		return NULL;

	memset(sim_file, 0, sizeof(*sim_file));

	sim_file->refcnt = 1;
	sim_file->sim = vgsm_sim_get(sim);

	return sim_file;
}

struct vgsm_sim_file *vgsm_sim_file_get(
	struct vgsm_sim_file *sim_file)
{
	assert(sim_file);
	assert(sim_file->refcnt > 0);
	assert(sim_file->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	sim_file->refcnt++;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	return sim_file;
}

void _vgsm_sim_file_put(struct vgsm_sim_file *sim_file)
{
	assert(sim_file);
	assert(sim_file->refcnt > 0);
	assert(sim_file->refcnt < 100000);

	ast_mutex_lock(&vgsm.usecnt_lock);
	int refcnt = --sim_file->refcnt;
	ast_mutex_unlock(&vgsm.usecnt_lock);

	if (!refcnt) {
		vgsm_sim_put(sim_file->sim);

		free(sim_file);
	}
}

int vgsm_sim_file_open(
	struct vgsm_comm *comm,
	struct vgsm_sim_file *sim_file,
	__u16 file_id)
{
	int err;

	struct vgsm_req *req;
	req = vgsm_req_make_wait(comm, 5 * SEC,
				"AT+CRSM=192,%d", file_id);
	if (vgsm_req_status(req) != VGSM_RESP_OK) {
		vgsm_req_put(req);
		err = -EIO;
		goto err_req_make;
	}

	sim_file->id = file_id;

	const char *line = vgsm_req_first_line(req)->text;
	const char *pars_ptr = line + strlen("+CRSM: ");
	char field[512];

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse CRSM sw1 '%s'\n", line);
		vgsm_req_put(req);
		err = -EINVAL;
		goto err_parse_response;
	}

	int sw1 = atoi(field);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse CRSM sw2 '%s'\n", line);
		vgsm_req_put(req);
		err = -EINVAL;
		goto err_parse_response;
	}

	//int sw2 = atoi(field);

	if (!get_token(&pars_ptr, field, sizeof(field))) {
		ast_log(LOG_ERROR, "Cannot parse CRSM response '%s'\n", line);
		vgsm_req_put(req);
		err = -EINVAL;
		goto err_parse_response;
	}

	if (sw1 != 0x90) {
		vgsm_req_put(req);
		err = -EIO;
		goto err_read_failed;
	}

	if (strlen(field) % 2) {
		ast_log(LOG_ERROR, "Invalid CRSM response '%s'\n", field);
		vgsm_req_put(req);
		err = -EIO;
		goto err_response_invalid;
	}

	char buf[sizeof(field)/2];
	int buf_len = strlen(field) / 2;

	int i;
	for(i=0; i<buf_len; i++) {
		buf[i] = char_to_hexdigit(field[i * 2]) << 4 |
		         char_to_hexdigit(field[i * 2 + 1]);
	}

#if 0
	ast_verbose("  Status = %d\n", sw1);
	ast_verbose("  Status2 = %d\n", sw2);
	ast_verbose("  Response = %s\n", field);
#endif

	struct vgsm_sim_file_stats *stats = (struct vgsm_sim_file_stats *)buf;

	if (sim_file->id != ntohs(stats->file_id))
		ast_log(LOG_WARNING, "SIM file ID differs from requested\n");

	sim_file->length = ntohs(stats->length);
	sim_file->type = stats->type_of_file;

	return 0;

err_response_invalid:
err_read_failed:
err_parse_response:
err_req_make:

	return err;
}

int vgsm_sim_file_read(
	struct vgsm_comm *comm,
	struct vgsm_sim_file *sim_file,
	void *data, __u16 offset, __u8 len)
{
	int err;

	struct vgsm_req *req;
	req = vgsm_req_make_wait(comm, 5 * SEC,
				"AT+CRSM=176,%d,%d,%d,%d",
				sim_file->id,
				offset & 0xff00 >> 8,
				offset & 0x00ff,
				len);
	if (vgsm_req_status(req) != VGSM_RESP_OK) {
		vgsm_req_put(req);
		err = -EIO;
		goto err_req_make;
	}

	const char *line = vgsm_req_first_line(req)->text;
	const char *pars_ptr = line + strlen("+CRSM: ");

	int field_len = max(32, 32 + len * 2);
	char *field = alloca(field_len);

	if (!get_token(&pars_ptr, field, field_len)) {
		ast_log(LOG_ERROR, "Cannot parse CRSM sw1 '%s'\n", line);
		vgsm_req_put(req);
		err = -EINVAL;
		goto err_parse_response;
	}

	int sw1 = atoi(field);

	if (!get_token(&pars_ptr, field, field_len)) {
		ast_log(LOG_ERROR, "Cannot parse CRSM sw2 '%s'\n", line);
		vgsm_req_put(req);
		err = -EINVAL;
		goto err_parse_response;
	}

	//int sw2 = atoi(field);

	if (!get_token(&pars_ptr, field, field_len)) {
		ast_log(LOG_ERROR, "Cannot parse CRSM response '%s'\n", line);
		vgsm_req_put(req);
		err = -EINVAL;
		goto err_parse_response;
	}

	if (strlen(field) % 2) {
		ast_log(LOG_ERROR, "CRSM response '%s' invalid\n", line);
		vgsm_req_put(req);
		err = -EINVAL;
		goto err_parse_response;
	}

	if (sw1 != 0x90) {
		vgsm_req_put(req);
		err = -EIO;
		goto err_read_failed;
	}

	int i;
	for(i=0; i<len; i++) {
		((__u8 *)data)[i] = char_to_hexdigit(field[i * 2]) << 4 |
		    		     char_to_hexdigit(field[i * 2 + 1]);
	}

	vgsm_req_put(req);

#if 0
	ast_verbose("  Status = %d\n", sw1);
	ast_verbose("  Status2 = %d\n", sw2);
	ast_verbose("  Response = %s\n", field);
#endif

	return 0;

err_read_failed:
err_parse_response:
err_req_make:

	return err;
}

void vgsm_sim_file_stats_dump(struct vgsm_sim_file *sim_file)
{
	ast_verbose(
		"File:\n"
		"  ID = %04x\n"
		"  Length = %d\n"
		"  Type = %s\n",
		sim_file->id,
		sim_file->length,
		vgsm_sim_type_of_file_to_text(sim_file->type));
}
