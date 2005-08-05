#include <string.h>

#define Q931_PRIVATE

#include "ie_bearercap.h"

static const struct q931_ie_type *ie_type;

void q931_ie_bearer_capability_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

int q931_ie_bearer_capability_check(
	const struct q931_ie *ie,
	const struct q931_message *msg)
{
	// TODO

	return TRUE;
}

int q931_ie_bearer_capability_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size)
{
	struct q931_ie_bearer_capability *ie =
		container_of(generic_ie, struct q931_ie_bearer_capability, ie);
	struct q931_ie_onwire *ieow = (struct q931_ie_onwire *)buf;

	ieow->id = Q931_IE_BEARER_CAPABILITY;
	ieow->len = 0;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_bearer_capability_onwire_3 *ie_bc_3 =
	  (struct q931_ie_bearer_capability_onwire_3 *)(&ieow->data[ieow->len]);
	ie_bc_3->ext = 1;
	ie_bc_3->coding_standard = ie->coding_standard;
	ie_bc_3->information_transfer_capability =
		ie->information_transfer_capability;
	ieow->len += 1;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_bearer_capability_onwire_4 *ie_bc_4 =
	  (struct q931_ie_bearer_capability_onwire_4 *)(&ieow->data[ieow->len]);
	ie_bc_4->ext = 1;
	ie_bc_4->transfer_mode = ie->transfer_mode;
	ie_bc_4->information_transfer_rate = ie->information_transfer_rate;
	ieow->len += 1;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_bearer_capability_onwire_5 *ie_bc_5 =
	  (struct q931_ie_bearer_capability_onwire_5 *)(&ieow->data[ieow->len]);
	ie_bc_5->ext = 1;
	ie_bc_5->layer_1_ident = Q931_IE_BC_LAYER_1_IDENT;
	ie_bc_5->user_information_layer_1_protocol =
		ie->user_information_layer_1_protocol;
	ieow->len += 1;

	return ieow->len + sizeof(struct q931_ie_onwire);
}
