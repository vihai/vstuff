/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU Lesser General Public License.
 *
 */

#ifndef _LIBQ931_IES_H
#define _LIBQ931_IES_H

#define Q931_IES_INIT { { }, 0 }

#define Q931_IES_NUM_IES 260

struct q931_ies
{
	struct q931_ie *ies[Q931_IES_NUM_IES]; // FIXME make this dynamic
	int count;
};

static inline void q931_ies_init(
	struct q931_ies *ies)
{
	ies->count = 0;
}

void q931_ies_add(
	struct q931_ies *ies,
	struct q931_ie *ie);

void q931_ies_add_put(
	struct q931_ies *ies,
	struct q931_ie *ie);

void q931_ies_del(
	struct q931_ies *ies,
	struct q931_ie *ie);

void q931_ies_merge(
	struct q931_ies *ies,
	const struct q931_ies *src_ies);

void q931_ies_copy(
	struct q931_ies *ies,
	const struct q931_ies *src_ies);

static inline int q931_ies_count(
	const struct q931_ies *ies)
{
	return ies->count;
}

void q931_ies_sort(
	struct q931_ies *ies);

#endif
