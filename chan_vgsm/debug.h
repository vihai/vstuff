/*
 * vGSM channel driver for Asterisk
 *
 * Copyright (C) 2008 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _DEBUG_H
#define _DEBUG_H

#ifdef DEBUG_CODE
#define vgsm_debug(format, arg...)					\
	ast_verbose("vgsm: "						\
		format,							\
		## arg)

#endif

#endif
