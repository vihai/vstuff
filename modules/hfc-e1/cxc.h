/*
 * Cologne Chip's HFC-E1 vISDN driver
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HFC_CXC_H
#define _HFC_CXC_H

struct hfc_cxc
{
	struct visdn_cxc visdn_cxc;
};

extern void hfc_cxc_init(struct hfc_cxc *cxc);

#endif
