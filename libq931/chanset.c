#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/chanset.h>

void q931_chanset_init(
	struct q931_chanset *chanset)
{
	assert(chanset);

	chanset->nchans = 0;
}

void q931_chanset_copy(
	struct q931_chanset *chanset,
	const struct q931_chanset *src_chanset)
{
	assert(chanset);
	assert(src_chanset);

	memcpy(chanset, src_chanset, sizeof(*chanset));
}

void q931_chanset_add(
	struct q931_chanset *chanset,
	struct q931_channel *channel)
{
	assert(chanset);
	assert(chanset->nchans < Q931_CHANSET_MAX_SIZE);
	assert(channel);

	// Avoid inserting duplicated
	int i;
	for (i=0; i<chanset->nchans; i++) {
		if (chanset->chans[i] == channel)
			return;
	}

	chanset->chans[chanset->nchans] = channel;
	chanset->nchans++;
}

void q931_chanset_del(
	struct q931_chanset *chanset,
	const struct q931_channel *channel)
{
	assert(chanset);
	assert(chanset->nchans);
	assert(channel);

	int i;
	for (i=0; i<chanset->nchans; i++) {
		if (chanset->chans[i] == channel) {
			int j;
			for (j=i; j<chanset->nchans - 1; j++) {
				chanset->chans[j] = chanset->chans[j+1];
			}

			chanset->nchans--;

			break;
		}
	}
}

void q931_chanset_merge(
	struct q931_chanset *chanset,
	const struct q931_chanset *src_chanset)
{
	int i;
	for (i=0; i<src_chanset->nchans; i++) {
		int j;
		for (j=0; j<chanset->nchans; j++) {
			if (chanset->chans[j] == src_chanset->chans[i])
				goto chan_found;
		}

		q931_chanset_add(chanset, src_chanset->chans[i]);

chan_found:
	;
	}
}

int q931_chanset_contains(
	const struct q931_chanset *chanset,
	const struct q931_channel *channel)
{
	int i;
	for (i=0; i<chanset->nchans; i++) {
		if (chanset->chans[i] == channel)
			return TRUE;
	}

	return FALSE;
}

int q931_chanset_equal(
	const struct q931_chanset *chanset,
	const struct q931_chanset *chanset2)
{
	assert(chanset);
	assert(chanset2);

	return !memcmp(chanset, chanset2, sizeof(*chanset));
}

void q931_chanset_intersect(
	struct q931_chanset *chanset,
	const struct q931_chanset *chanset2)
{
	assert(chanset);
	assert(chanset2);

	int i;
	for (i=0; i<chanset->nchans; i++) {
		int j;
		for (j=0; j<chanset2->nchans; j++) {
			if (chanset2->chans[j] == chanset->chans[i])
				goto chan_found;
		}

		q931_chanset_del(chanset, chanset->chans[i]);

chan_found:
	;
	}
}
