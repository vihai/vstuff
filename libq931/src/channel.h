#ifndef _CHANNEL_H
#define _CHANNEL_H

enum q931_channel_state
{
	Q931_CHANSTATE_AVAILABLE,
	Q931_CHANSTATE_SELECTED,
	Q931_CHANSTATE_CONNECTED,
	Q931_CHANSTATE_DISCONNECTED,
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

#endif
