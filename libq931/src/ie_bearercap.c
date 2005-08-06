#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include "lib.h"
#include "ie_bearercap.h"

static const struct q931_ie_type *ie_type;

void q931_ie_bearer_capability_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

struct q931_ie_bearer_capability *q931_ie_bearer_capability_alloc(void)
{
	struct q931_ie_bearer_capability *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	ie->ie.refcnt = 1;
	ie->ie.type = ie_type;

	return ie;
}

struct q931_ie *q931_ie_bearer_capability_alloc_abstract(void)
{
	return &q931_ie_bearer_capability_alloc()->ie;
}

int q931_ie_bearer_capability_read_from_buf(
	struct q931_ie *abstract_ie,
	const struct q931_message *msg,
	int pos,
	int len)
{
	assert(abstract_ie->type == ie_type);

	struct q931_ie_bearer_capability *ie =
		container_of(abstract_ie,
			struct q931_ie_bearer_capability, ie);

	int nextoct = 0;

	if (len < 1) {
		report_msg(msg, LOG_WARNING, "IE size < 2\n");
		return FALSE;
	}

	struct q931_ie_bearer_capability_onwire_3 *oct_3 =
		(struct q931_ie_bearer_capability_onwire_3 *)
		(msg->rawies + pos + (nextoct++));

	if (!oct_3->ext) {
		report_msg(msg, LOG_WARNING, "IE oct 3 ext != 0\n");
		return FALSE;
	}

	ie->coding_standard =
		oct_3->coding_standard;
	ie->information_transfer_capability =
		oct_3->information_transfer_capability;

	struct q931_ie_bearer_capability_onwire_4 *oct_4 =
		(struct q931_ie_bearer_capability_onwire_4 *)
		(msg->rawies + pos + (nextoct++));

	ie->transfer_mode =
		oct_4->transfer_mode;
	ie->information_transfer_rate =
		oct_4->information_transfer_rate;

	if (oct_4->ext) {
		struct q931_ie_bearer_capability_onwire_4a *oct_4a =
			(struct q931_ie_bearer_capability_onwire_4a *)
			(msg->rawies + pos + (nextoct++));

		if (oct_4a->ext) {
			struct q931_ie_bearer_capability_onwire_4b *oct_4b =
				(struct q931_ie_bearer_capability_onwire_4b *)
				(msg->rawies + pos + (nextoct++));

			if (oct_4b->ext) {
				report_msg(msg, LOG_WARNING, "IE oct 4b ext != 0\n");
				return FALSE;
			}
		}
	}

	if (nextoct >= len)
		return TRUE;

	struct q931_ie_bearer_capability_onwire_5 *oct_5 =
		(struct q931_ie_bearer_capability_onwire_5 *)
		(msg->rawies + pos + (nextoct++));

	ie->user_information_layer_1_protocol =
		oct_5->user_information_layer_1_protocol;

	if (oct_5->ext) {
		struct q931_ie_bearer_capability_onwire_5a *oct_5a =
			(struct q931_ie_bearer_capability_onwire_5a *)
			(msg->rawies + pos + (nextoct++));

		if (oct_5a->ext) {
			int oct_5b_ext;

			if (oct_5->user_information_layer_1_protocol ==
				Q931_IE_BC_UIL1P_V110) {

				struct q931_ie_bearer_capability_onwire_5b1 *oct_5b1 =
					(struct q931_ie_bearer_capability_onwire_5b1 *)
					(msg->rawies + pos + (nextoct++));

				oct_5b_ext = oct_5b1->ext;
			} else if (oct_5->user_information_layer_1_protocol ==
				Q931_IE_BC_UIL1P_V120) {

				struct q931_ie_bearer_capability_onwire_5b2 *oct_5b2 =
					(struct q931_ie_bearer_capability_onwire_5b2 *)
					(msg->rawies + pos + (nextoct++));

				oct_5b_ext = oct_5b2->ext;
			} else {
				report_msg(msg, LOG_WARNING,
					"IE oct 5b ext != 0 and l1 != v110 or v120\n");
				return FALSE;
			}

			if (oct_5b_ext) {
				struct q931_ie_bearer_capability_onwire_5c *oct_5c =
					(struct q931_ie_bearer_capability_onwire_5c *)
					(msg->rawies + pos + (nextoct++));
				if (oct_5c->ext) {
					struct q931_ie_bearer_capability_onwire_5d *oct_5d =
						(struct q931_ie_bearer_capability_onwire_5d *)
						(msg->rawies + pos + (nextoct++));

					if (oct_5d->ext) {
						report_msg(msg, LOG_WARNING,
							"IE oct 5d ext != 0\n");
						return FALSE;
					}
				}
			}
		}
	}

	if (nextoct >= len)
		return TRUE;

	struct q931_ie_bearer_capability_onwire_6 *oct_6 =
		(struct q931_ie_bearer_capability_onwire_6 *)
		(msg->rawies + pos + (nextoct++));

	if (oct_6->ext) {
		report_msg(msg, LOG_WARNING, "IE oct 6 ext != 0\n");
		return FALSE;
	}
	
	if (nextoct >= len)
		return TRUE;

	struct q931_ie_bearer_capability_onwire_7 *oct_7 =
		(struct q931_ie_bearer_capability_onwire_7 *)
		(msg->rawies + pos + (nextoct++));

	if (oct_7->ext) {
		report_msg(msg, LOG_WARNING, "IE oct 7 ext != 0\n");
		return FALSE;
	}

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
	struct q931_ie_bearer_capability_onwire_3 *oct_3 =
	  (struct q931_ie_bearer_capability_onwire_3 *)(&ieow->data[ieow->len]);
	oct_3->ext = 1;
	oct_3->coding_standard = ie->coding_standard;
	oct_3->information_transfer_capability =
		ie->information_transfer_capability;
	ieow->len += 1;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_bearer_capability_onwire_4 *oct_4 =
	  (struct q931_ie_bearer_capability_onwire_4 *)(&ieow->data[ieow->len]);
	oct_4->ext = 1;
	oct_4->transfer_mode = ie->transfer_mode;
	oct_4->information_transfer_rate = ie->information_transfer_rate;
	ieow->len += 1;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_bearer_capability_onwire_5 *oct_5 =
	  (struct q931_ie_bearer_capability_onwire_5 *)(&ieow->data[ieow->len]);
	oct_5->ext = 1;
	oct_5->layer_1_ident = Q931_IE_BC_LAYER_1_IDENT;
	oct_5->user_information_layer_1_protocol =
		ie->user_information_layer_1_protocol;
	ieow->len += 1;

	return ieow->len + sizeof(struct q931_ie_onwire);
}
