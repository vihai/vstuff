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

#ifndef _VGSM_BASE64_H
#define _VGSM_BASE64_H

void base64_decode(const char *src, __u8 *dest, int dest_size);
int base64_encode(const __u8 *in,  int inlen,
                        char *out, int *outlen);

#endif
