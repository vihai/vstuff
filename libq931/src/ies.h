#ifndef _LIBQ931_IES_H
#define _LIBQ931_IES_H

#define Q931_IES_INIT { { }, 0 }

struct q931_ies
{
	struct q931_ie ies[260]; // FIXME
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

#endif
