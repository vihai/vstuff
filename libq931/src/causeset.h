#ifndef _CAUSESET_H
#define _CAUSESET_H

#include "ie_cause.h"

#define Q931_CAUSESET_MAX_SIZE	32

#define Q931_CAUSESET_INIT { .ncauses = 0 }
#define Q931_CAUSESET_INITC(c) { .ncauses = 1, .causes = { { (c), 0, { } } } }

struct q931_causeset_cause
{
	enum q931_ie_cause_value cause;
	int diaglen;
	__u8 diag[32];
};

// This is not the best implementation (it sucks, actually).
struct q931_causeset
{
	int ncauses;
	struct q931_causeset_cause causes[Q931_CAUSESET_MAX_SIZE];
};

void q931_causeset_init(
	struct q931_causeset *causeset);

void q931_causeset_add(
	struct q931_causeset *causeset,
	enum q931_ie_cause_value cause);

void q931_causeset_add_diag(
	struct q931_causeset *causeset,
	enum q931_ie_cause_value cause,
	const void *diag,
	int diaglen);

void q931_causeset_merge(
	struct q931_causeset *causeset,
	const struct q931_causeset *src_causeset);

int q931_causeset_contains(
	const struct q931_causeset *causeset,
	enum q931_ie_cause_value cause);

int q931_causeset_equal(
	const struct q931_causeset *causeset,
	const struct q931_causeset *causeset2);

void q931_causeset_intersect(
	struct q931_causeset *causeset,
	const struct q931_causeset *causeset2);

void q931_causeset_copy(
	struct q931_causeset *causeset,
	const struct q931_causeset *src_causeset);

static inline int q931_causeset_count(
	const struct q931_causeset *causeset)
{
	return causeset->ncauses;
}

#endif
