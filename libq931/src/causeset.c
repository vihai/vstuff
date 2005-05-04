#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include "lib.h"
#include "causeset.h"

void q931_causeset_init(
	struct q931_causeset *causeset)
{
	assert(causeset);

	causeset->ncauses = 0;
}

void q931_causeset_copy(
	struct q931_causeset *causeset,
	const struct q931_causeset *src_causeset)
{
	assert(causeset);
	assert(src_causeset);

	memcpy(causeset, src_causeset, sizeof(*causeset));
}

void q931_causeset_add(
	struct q931_causeset *causeset,
	enum q931_ie_cause_value cause)
{
	assert(causeset);
	assert(causeset->ncauses < Q931_CHANSET_MAX_SIZE);

	// Avoid inserting duplicated
	int i;
	for (i=0; i<causeset->ncauses; i++) {
		if (causeset->causes[i].cause == cause)
			return;
	}

	causeset->causes[causeset->ncauses].cause = cause;
	causeset->causes[causeset->ncauses].diaglen = 0;
	causeset->ncauses++;
}

void q931_causeset_add_diag(
	struct q931_causeset *causeset,
	enum q931_ie_cause_value cause,
	const void *diag,
	int diaglen)
{
	assert(causeset);
	assert(causeset->ncauses < Q931_CHANSET_MAX_SIZE);
	assert(diaglen < 32);

	// Avoid inserting duplicated
	int i;
	for (i=0; i<causeset->ncauses; i++) {
		if (causeset->causes[i].cause == cause)
			return;
	}

	causeset->causes[causeset->ncauses].cause = cause;
	causeset->causes[causeset->ncauses].diaglen = diaglen;
	memcpy(causeset->causes[causeset->ncauses].diag,
		diag, diaglen);

	causeset->ncauses++;
}

void q931_causeset_del(
	struct q931_causeset *causeset,
	enum q931_ie_cause_value cause)
{
	assert(causeset);
	assert(causeset->ncauses);

	int i;
	for (i=0; i<causeset->ncauses; i++) {
		if (causeset->causes[i].cause == cause) {
			int j;
			for (j=i; j<causeset->ncauses - 1; j++) {
				memcpy(&causeset->causes[j],
					&causeset->causes[j+1],
					sizeof(*causeset));
			}

			causeset->ncauses--;

			break;
		}
	}
}

void q931_causeset_merge(
	struct q931_causeset *causeset,
	const struct q931_causeset *src_causeset)
{
	int i;
	for (i=0; i<src_causeset->ncauses; i++) {
		int j;
		for (j=0; j<causeset->ncauses; j++) {
			if (causeset->causes[j].cause ==
			    src_causeset->causes[i].cause)
				goto cause_found;
		}

		q931_causeset_add_diag(causeset,
			src_causeset->causes[i].cause,
			src_causeset->causes[i].diag,
			src_causeset->causes[i].diaglen);

cause_found:
	;
	}
}

int q931_causeset_contains(
	const struct q931_causeset *causeset,
	enum q931_ie_cause_value cause)
{
	int i;
	for (i=0; i<causeset->ncauses; i++) {
		if (causeset->causes[i].cause == cause)
			return TRUE;
	}

	return FALSE;
}

int q931_causeset_equal(
	const struct q931_causeset *causeset,
	const struct q931_causeset *causeset2)
{
	assert(causeset);
	assert(causeset2);

	return !memcmp(causeset, causeset2, sizeof(*causeset));
}

void q931_causeset_intersect(
	struct q931_causeset *causeset,
	const struct q931_causeset *causeset2)
{
	assert(causeset);
	assert(causeset2);

	int i;
	for (i=0; i<causeset->ncauses; i++) {
		int j;
		for (j=0; j<causeset2->ncauses; j++) {
			if (causeset2->causes[j].cause ==
			    causeset->causes[i].cause)
				goto cause_found;
		}

		q931_causeset_del(causeset, causeset->causes[i].cause);

cause_found:
	;
	}
}
