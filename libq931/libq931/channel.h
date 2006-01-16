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

#ifndef _LIBQ931_CHANNEL_H
#define _LIBQ931_CHANNEL_H

enum q931_channel_state
{
	Q931_CHANSTATE_MAINTAINANCE,
	Q931_CHANSTATE_AVAILABLE,
	Q931_CHANSTATE_SELECTED,
	Q931_CHANSTATE_CONNECTED,
	Q931_CHANSTATE_DISCONNECTED,
};

enum q931_tone_type
{
	Q931_TONE_DIAL,
	Q931_TONE_BUSY,
	Q931_TONE_HANGUP,
	Q931_TONE_FAILURE,
};

struct q931_channel
{
	int id;
	enum q931_channel_state state;
	struct q931_call *call;
	struct q931_interface *intf;
	void *pvt;
};

struct q931_channel *q931_channel_alloc(struct q931_call *call);
struct q931_channel *get_channel_by_id(
	struct q931_interface *intf,
	int chan_id);

const char *q931_channel_state_to_text(enum q931_channel_state state);

void q931_channel_connect(struct q931_channel *channel);
void q931_channel_control(struct q931_channel *channel);
void q931_channel_disconnect(struct q931_channel *channel);
void q931_channel_release(struct q931_channel *channel);

#ifdef Q931_PRIVATE

#define report_chan(chan, lvl, format, arg...)		\
	q931_report((lvl),				\
		"%s[B%d]: "				\
		format,					\
		(chan)->intf->name,			\
		(chan)->id + 1,				\
		## arg)

void q931_channel_set_state(
	struct q931_channel *channel,
	enum q931_channel_state state);

void q931_channel_start_tone(
	struct q931_channel *channel,
	enum q931_tone_type tone);
void q931_channel_stop_tone(
	struct q931_channel *channel);

void q931_channel_init(
	struct q931_channel *channel,
	int id,
	struct q931_interface *intf);

#endif

#endif
