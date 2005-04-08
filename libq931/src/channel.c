#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include "list.h"
#include "q931.h"
#include "logging.h"
#include "channel.h"
#include "intf.h"

struct q931_channel *q931_channel_alloc(struct q931_call *call)
{
	assert(call);
	assert(call->intf);

	int i;
	for(i=0; i<call->intf->n_channels; i++) {
		if (call->intf->channels[i].state ==
		      Q931_CHANSTATE_AVAILABLE) {

			call->intf->channels[i].state =
				Q931_CHANSTATE_SELECTED;

			call->intf->channels[i].call = call;

			return &call->intf->channels[i];
		}
	}

	return NULL;
}

struct q931_channel *q931_channel_select(struct q931_call *call)
{
	assert(call);
	assert(call->intf);

	int i;
	for(i=0; i<call->intf->n_channels; i++) {
		if (call->intf->channels[i].state ==
		      Q931_CHANSTATE_AVAILABLE) {

			call->intf->channels[i].state =
				Q931_CHANSTATE_SELECTED;

			call->intf->channels[i].call = call;

			return &call->intf->channels[i];
		}
	}

	return NULL;
}

struct q931_channel *get_channel_by_id(
	struct q931_interface *intf,
	int chan_id)
{
	int i;
	for (i=0; i<intf->n_channels; i++) {
		if (intf->channels[i].id == chan_id)
			return &intf->channels[i];
	}

	return NULL;
}

