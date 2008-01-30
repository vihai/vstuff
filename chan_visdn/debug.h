/*
 * vISDN channel driver for Asterisk
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
#define visdn_debug(format, arg...)					\
	ast_verbose("visdn: "						\
		format,							\
		## arg)

#define FUNC_DEBUG(format, arg...)	\
	visdn_debug("%s " format "\n", __FUNCTION__, ## arg);

#else /* DEBUG_CODE */

#define visdn_debug(format, arg...) do {} while(0)
#define FUNC_DEBUG() do {} while(0)

#endif

#endif
