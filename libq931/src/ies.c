#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include "lib.h"
#include "ies.h"

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
	assert(ies);
	assert(src_ies);
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

