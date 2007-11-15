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

#ifndef _OPERATORS_H
#define _OPERATORS_H

#include <list.h>

struct vgsm_operator_country
{
	struct list_head node;

	__u16 mcc;

	char *name;
};

struct vgsm_operator_info
{
	struct list_head node;

	__u16 mcc;
	__u16 mnc;

	char *name;
	char *name_short;
	char *date;
	char *bands;

	struct vgsm_operator_country *country;
};

struct vgsm_operator_info *vgsm_operators_search(__u16 mcc, __u16 mnc);

void vgsm_operators_init(void);

#endif
