#ifndef _CHANSET_H
#define _CHANSET_H

#define Q931_CHANSET_MAX_SIZE	32

// This is not the best implementation (it sucks, actually).
struct q931_chanset
{
	int nchans;
	int chans[Q931_CHANSET_MAX_SIZE];
};

void q931_chanset_init(
	struct q931_chanset *chanset);

void q931_chanset_add(
	struct q931_chanset *chanset,
	int chan_id);

void q931_chanset_merge(
	struct q931_chanset *chanset,
	const struct q931_chanset *src_chanset);

int q931_chanset_contains(
	const struct q931_chanset *chanset,
	int channel_id);

int q931_chanset_equal(
	const struct q931_chanset *chanset,
	const struct q931_chanset *chanset2);

void q931_chanset_intersect(
	struct q931_chanset *chanset,
	const struct q931_chanset *chanset2);

void q931_chanset_copy(
	struct q931_chanset *chanset,
	const struct q931_chanset *src_chanset);

static inline int q931_chanset_count(
	const struct q931_chanset *chanset)
{
	return chanset->nchans;
}

#endif
