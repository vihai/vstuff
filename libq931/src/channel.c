#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "list.h"
#include "q931.h"
#include "logging.h"
#include "channel.h"

struct q931_channel *q931_channel_alloc(struct q931_call *call)
{
	assert(call);
	assert(call->interface);

	int i;
	for(i=0; i<call->interface->n_channels; i++) {
		if (call->interface->channels[i].state ==
		      Q931_CHANSTATE_AVAILABLE) {

			call->interface->channels[i].state =
				Q931_CHANSTATE_SELECTED;

			call->interface->channels[i].call = call;

			return &call->interface->channels[i];
		}
	}

	return NULL;
}

static inline struct q931_channel *q931_select_channel_bra(
	struct q931_call *call,
	const struct q931_ies *ies)
{
	struct q931_ie_channel_identification_onwire_3 *oct_3 =
		(struct q931_ie_channel_identification_onwire_3 *)
		ies[i]->data[0];

	if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_B1) {
		struct q931_channel *channel;

		channel = get_channel_by_id(call->interface, 0);
		if (channel && channel->state == Q931_CHANSTATE_AVAILABLE)
			return channel;

		if (oct_3->preferred_exclusive == Q931_IE_CI_PE_EXCLUSIVE) {
			*cause = Q931_IE_C_CV_REQUESTED_CIRCUIT_CHANNEL_NOT_AVAILABLE;
			return NULL;
		}

		channel = get_channel_by_id(call->interface, 1);
		if (channel && channel->state == Q931_CHANSTATE_AVAILABLE) {
			// Should add chan_id to next reply to SETUP
			return channel;
		}

		*cause = Q931_IE_C_CV_NO_CIRCUIT_CANNEL_AVAILABLE;
		return NULL;

	} else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_B2) {

		struct q931_channel *channel;

		channel = get_channel_by_id(call->interface, 1);
		if (channel && channel->state == Q931_CHANSTATE_AVAILABLE)
			return channel;

		if (oct_3->preferred_exclusive == Q931_IE_CI_PE_EXCLUSIVE) {
			*cause = Q931_IE_C_CV_REQUESTED_CIRCUIT_CHANNEL_NOT_AVAILABLE;
			return NULL;
		}

		channel = get_channel_by_id(call->interface, 0);
		if (channel && channel->state == Q931_CHANSTATE_AVAILABLE) {
			// Should add chan_id to next reply to SETUP
			return channel;
		}

		*cause = Q931_IE_C_CV_NO_CIRCUIT_CANNEL_AVAILABLE;
		return NULL;

	} else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_ANY) {

		struct q931_channel *channel;
		channel = get_channel_by_id(call->interface, 0);
		if (channel && channel->state == Q931_CHANSTATE_AVAILABLE)
			return channel;

		channel = get_channel_by_id(call->interface, 1);
		if (channel && channel->state == Q931_CHANSTATE_AVAILABLE) {
			// Should add chan_id to next reply to SETUP
			return channel;
		}

	else if (oct_3->info_channel_selection == Q931_IE_CI_ICS_BRA_NO_CHANNEL) {

		// If we are able to free one channel (calling a callback?) we
		// can alloc the freed channel and continue, otherwise
		// respond with RELEASE COMPLETE with cause #34
		// FIXME TODO FIXME TODO
	}

	if (call->interface->role == LAPD_ROLE_NT) {
	}

}

static inline struct q931_channel *q931_select_channel_pra(
	struct q931_call *call,
	const struct q931_ies *ies)
{
	struct q931_ie_channel_identification_onwire_3c *oct_3c =
		(struct q931_ie_channel_identification_onwire_3c *)
		ies[i]->data[1];

	if (oct_3c->coding_standard != Q931_IE_CI_CS_CCITT) {
		report_call(call, LOG_ERR,
			"IE specifies unsupported coding type\n");

		goto invalid_ie;
	}

	struct q931_ie_channel_identification_onwire_3d *oct_3d;
	do {
		oct_3d = (struct q931_ie_channel_identification_onwire_3d *)
				ies[i]->data[2];

		oct_3d
	} while (!oct_3d->ext);
}

struct q931_channel *q931_channel_check(struct q931_call *call,
	const struct q931_ies *ies,
	int ies_cnt)
{
	assert(call);
	assert(call->interface);

	int i;
	for(i=0; i<ies_cnt; i++) {
		if (ies[i].info->id == Q931_IE_CHANNEL_IDENTIFICATION) {
			struct q931_ie_channel_identification_onwire_3 *oct_3 =
				(struct q931_ie_channel_identification_onwire_3 *)
				ies[i]->data[0];

			if (oct_3->interface_type == Q931_IE_CI_IT_BASIC)
				return q931_select_channel_bra(call, ies);
			else
				return q931_select_channel_pra(call, ies);
		}
	}

	return NULL;

invalid_ie:
	q931_send_status(call, call->dlc,
		Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS);

}

struct q931_channel *q931_channel_select(struct q931_call *call)
{
	assert(call);
	assert(call->interface);

	int i;
	for(i=0; i<call->interface->n_channels; i++) {
		if (call->interface->channels[i].state ==
		      Q931_CHANSTATE_AVAILABLE) {

			call->interface->channels[i].state =
				Q931_CHANSTATE_SELECTED;

			call->interface->channels[i].call = call;

			return &call->interface->channels[i];
		}
	}

	return NULL;
}


