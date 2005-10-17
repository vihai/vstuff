/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _LIBQ931_CHANSET_H
#define _LIBQ931_CHANSET_H

#define Q931_CHANSET_MAX_SIZE	32

// This is not the best implementation (it sucks, actually).
struct q931_chanset
{
	int nchans;
	struct q931_channel *chans[Q931_CHANSET_MAX_SIZE];
};

void q931_chanset_init(
	struct q931_chanset *chanset);

void q931_chanset_add(
	struct q931_chanset *chanset,
	struct q931_channel *channel);

void q931_chanset_merge(
	struct q931_chanset *chanset,
	const struct q931_chanset *src_chanset);

int q931_chanset_contains(
	const struct q931_chanset *chanset,
	const struct q931_channel *channel);

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
