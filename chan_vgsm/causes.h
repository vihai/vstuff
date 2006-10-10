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

#ifndef _VGSM_CAUSES_H
#define _VGSM_CAUSES_H

#define VGSM_CAUSE_LOCATION_LOCAL 1000

enum vgsm_cause_local_reasons
{
	VGSM_CAUSE_REASON_NORMAL_CALL_CLEARING,
	VGSM_CAUSE_REASON_NO_RESOURCES,
	VGSM_CAUSE_REASON_SLCC_ERROR,
	VGSM_CAUSE_REASON_MODULE_NOT_READY,
	VGSM_CAUSE_REASON_UNSUPPORTED_BEARER_TYPE,
	VGSM_CAUSE_REASON_USER_BUSY,
	VGSM_CAUSE_REASON_CONGESTION,
};

const char *vgsm_cause_location_to_text(int location);
const char *vgsm_cause_reason_to_text(int location, int cause);
int vgsm_cause_to_ast_cause(int location, int reason);

#endif
