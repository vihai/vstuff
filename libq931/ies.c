/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
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

#include <libq931/ie_cause.h>

void q931_ies_flush(struct q931_ies *ies)
{
	assert(ies);

	int i;
	for (i=0; i<ies->count; i++)
		q931_ie_put(ies->ies[i]);

	ies->count = 0;
}

void q931_ies_destroy(struct q931_ies *ies)
{
	assert(ies);

	q931_ies_flush(ies);
}

void q931_ies_add(
	struct q931_ies *ies,
	struct q931_ie *ie)
{
	assert(ies);
	assert(ies->count < Q931_IES_NUM_IES);
	assert(ie);

	ies->ies[ies->count] = q931_ie_get(ie);

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

		if (src_ies->ies[i]->cls->id == Q931_IE_CAUSE) {
			struct q931_ie_cause *src_cause =
				container_of(src_ies->ies[i],
				struct q931_ie_cause, ie);

			int j;
			for (j=0; j<ies->count; j++) {
				if (ies->ies[j]->cls->id == Q931_IE_CAUSE) {
					struct q931_ie_cause *cause =
						container_of(ies->ies[j],
						struct q931_ie_cause, ie);

					if (cause->value == src_cause->value)
						goto continue_to_next;
				}
			}
		}

		q931_ies_add(ies, src_ies->ies[i]);

continue_to_next:;
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

	q931_ies_flush(ies);

	int i;
	for (i=0; i<src_ies->count; i++)
		q931_ies_add(ies, src_ies->ies[i]);
}

static int q931_ies_compare(const void *a, const void *b)
{
	return q931_intcmp((*((struct q931_ie **)a))->cls->id,
	                   (*((struct q931_ie **)b))->cls->id);
}

void q931_ies_sort(
	struct q931_ies *ies)
{
	qsort(ies->ies,
	      ies->count,
	      sizeof(*ies->ies),
	      q931_ies_compare);
}
