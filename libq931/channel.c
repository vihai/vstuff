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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include <libq931/list.h>
#include <libq931/logging.h>
#include <libq931/channel.h>
#include <libq931/intf.h>
#include <libq931/lib.h>

#define q931_channel_primitive(channel, primitive)	\
		q931_queue_primitive(NULL, primitive, NULL,	\
			(unsigned long)channel, 0)

#define q931_channel_primitive1(channel, primitive, par1)	\
		q931_queue_primitive(NULL, primitive, NULL,	\
			(unsigned long)channel, par1)

struct q931_channel *q931_channel_select(struct q931_call *call)
{
	assert(call);
	assert(call->intf);

	int i;
	for(i=0; i<call->intf->n_channels; i++) {
		if (call->intf->channels[i].state ==
		      Q931_CHANSTATE_AVAILABLE) {
			return &call->intf->channels[i];
		}
	}

	return NULL;
}

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

const char *q931_channel_state_to_text(enum q931_channel_state state)
{
	switch(state) {
	case Q931_CHANSTATE_MAINTAINANCE:
		return "MAINTAINANCE";
	case Q931_CHANSTATE_AVAILABLE:
		return "AVAILABLE";
	case Q931_CHANSTATE_SELECTED:
		return "SELECTED";
	case Q931_CHANSTATE_PROPOSED:
		return "PROPOSED";
	case Q931_CHANSTATE_CONNECTED:
		return "CONNECTED";
	case Q931_CHANSTATE_DISCONNECTED:
		return "DISCONNECTED";
	}

	assert(0);
}

void q931_channel_connect(
	struct q931_channel *channel)
{
	assert(channel);
	assert(channel->call);

	if (channel->state != Q931_CHANSTATE_CONNECTED) {
		channel->state = Q931_CHANSTATE_CONNECTED;

		q931_channel_primitive(channel, Q931_CCB_CONNECT_CHANNEL);
	}
}

void q931_channel_control(
	struct q931_channel *channel)
{
	assert(channel);
	assert(channel->call);

	if (channel->state != Q931_CHANSTATE_CONNECTED) {
		channel->state = Q931_CHANSTATE_CONNECTED;

		q931_channel_primitive(channel, Q931_CCB_CONNECT_CHANNEL);
	}
}

void q931_channel_disconnect(
	struct q931_channel *channel)
{
	// We may be called with channel == NULL when the channel has not
	// even been selected

	if (!channel)
		return;

	assert(channel->call);

	if (channel->state == Q931_CHANSTATE_CONNECTED) {
		channel->state = Q931_CHANSTATE_DISCONNECTED;

		q931_channel_primitive(channel, Q931_CCB_DISCONNECT_CHANNEL);
	}
}

void q931_channel_release(
	struct q931_channel *channel)
{
	// We may be called with channel == NULL when the channel has not
	// even been selected

	if (!channel)
		return;

	if (channel->state == Q931_CHANSTATE_CONNECTED) {
		// Is this an unexpected state?

		q931_channel_primitive(channel, Q931_CCB_DISCONNECT_CHANNEL);
	}

	channel->state = Q931_CHANSTATE_AVAILABLE;
}

void q931_channel_start_tone(
	struct q931_channel *channel,
	enum q931_tone_type tone)
{
	assert(channel);

	q931_channel_primitive1(channel, Q931_CCB_START_TONE, tone);
}

void q931_channel_stop_tone(
	struct q931_channel *channel)
{
	assert(channel);

	q931_channel_primitive(channel, Q931_CCB_STOP_TONE);
}

