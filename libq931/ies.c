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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/ies.h>

void q931_ies_add(
	struct q931_ies *ies,
	struct q931_ie *ie)
{
	assert(ies);
	assert(ies->count < Q931_IES_NUM_IES);
	assert(ie);

	q931_ie_get(ie);

	ies->ies[ies->count] = ie;
	ies->count++;
}

void q931_ies_add_put(
	struct q931_ies *ies,
	struct q931_ie *ie)
{
	q931_ies_add(ies, ie);
	q931_ie_put(ie);
}

void q931_ies_del(
	struct q931_ies *ies,
	struct q931_ie *ie)
{
	assert(ies);
	assert(ie);

	int i;
	for (i=0; i<ies->count; i++) {
		if (ies->ies[i] == ie) {
			int j;
			for (j=i; j<ies->count-1; j++)
				ies->ies[i] = ies->ies[i+1];

			q931_ie_put(ie);

			ies->count--;

			break;
		}
	}
}

void q931_ies_merge(
	struct q931_ies *ies,
	const struct q931_ies *src_ies)
{
	if (!src_ies)
		return;

	assert(ies);
	assert(ies->count + src_ies->count < Q931_IES_NUM_IES);

	int i;
	for (i=0; i<src_ies->count; i++) {
		q931_ies_add(ies, src_ies->ies[i]);
	}
}

void q931_ies_copy(
	struct q931_ies *ies,
	const struct q931_ies *src_ies)
{
	if (!src_ies)
		return;

	assert(ies);
	assert(src_ies);

	int i;
	for (i=0; i<ies->count; i++) {
		q931_ies_del(ies, ies->ies[i]);
	}

	for (i=0; i<src_ies->count; i++) {
		q931_ies_add(ies, src_ies->ies[i]);
	}
}

static int q931_ies_compare(const void *a, const void *b)
{
	return q931_intcmp((*((struct q931_ie **)a))->type->id,
	                   (*((struct q931_ie **)b))->type->id);
}

void q931_ies_sort(
	struct q931_ies *ies)
{
	qsort(ies->ies,
	      ies->count,
	      sizeof(*ies->ies),
	      q931_ies_compare);
}
