#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include "lib.h"
#include "ie_sending_complete.h"

static const struct q931_ie_type *ie_type;

void q931_ie_sending_complete_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

struct q931_ie_sending_complete *q931_ie_sending_complete_alloc(void)
{
	struct q931_ie_sending_complete *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	ie->ie.type = ie_type;
	ie->ie.refcnt = 1;

	return ie;
}

struct q931_ie *q931_ie_sending_complete_alloc_abstract(void)
{
	return &q931_ie_sending_complete_alloc()->ie;
}

int q931_ie_sending_complete_read_from_buf(
	struct q931_ie *abstract_ie,
	const struct q931_message *msg,
	int pos,
	int len)
{
	assert(abstract_ie->type == ie_type);

	return TRUE;
}

int q931_ie_sending_complete_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size)
{
	*(__u8 *)buf = Q931_IE_SENDING_COMPLETE;

	return 1;
}
