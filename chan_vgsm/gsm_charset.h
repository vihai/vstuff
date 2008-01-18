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

#ifndef _GSM_CHARSET_H
#define _GSM_CHARSET_H

#include <wchar.h>
#include <linux/types.h>

wchar_t vgsm_gsm_to_wc(char gsm);
int vgsm_wc_to_gsm(wchar_t wc, __u8 *c, __u8 *c2);

#endif
