#ifndef _Q931_OUT_H
#define _Q931_OUT_H

int q931_send_connect_acknowledge(struct q931_call *call);
int q931_send_disconnect(struct q931_call *call);
int q931_send_release(
	struct q931_call *call,
	const struct q931_dlc *dlc);
int q931_send_release_cause(
	struct q931_call *call,
	const struct q931_dlc *dlc,
	enum q931_ie_cause_value cause_value);
int q931_send_release_complete( struct q931_call *call);
int q931_send_setup(struct q931_call *call, enum q931_setup_mode setup_mode);
int q931_send_setup_acknowledge(struct q931_call *call);
int q931_send_status(struct q931_call *call);
int q931_send_alerting(struct q931_call *call);
int q931_send_connect(struct q931_call *call);
int q931_send_call_proceeding(struct q931_call *call);

#endif
