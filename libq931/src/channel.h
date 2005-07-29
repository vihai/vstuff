#ifndef _CHANNEL_H
#define _CHANNEL_H

#define q931_channel_primitive(channel, primitive, arg...)			\
	do {									\
		if ((channel)->intf->lib->primitive)				\
			(channel)->intf->lib->primitive(channel, ## arg);	\
	} while(0);

enum q931_channel_state
{
	Q931_CHANSTATE_MAINTAINANCE,
	Q931_CHANSTATE_AVAILABLE,
	Q931_CHANSTATE_SELECTED,
	Q931_CHANSTATE_PROPOSED,
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

struct q931_channel *q931_channel_select(struct q931_call *call);
struct q931_channel *q931_channel_alloc(struct q931_call *call);
struct q931_channel *get_channel_by_id(
	struct q931_interface *intf,
	int chan_id);

const char *q931_channel_state_to_text(enum q931_channel_state state);

#endif
