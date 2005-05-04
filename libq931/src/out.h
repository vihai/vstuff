#ifndef _OUT_H
#define _OUT_H

#include "call.h"
#include "ie_progind.h"

int q931_send_alerting(
	struct q931_call *call,
	struct q931_dlc *dlc);

int q931_send_call_proceeding(
	struct q931_call *call,
	struct q931_dlc *dlc);
int q931_send_call_proceeding_channel(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_channel *channel);

int q931_send_connect(
	struct q931_call *call,
	struct q931_dlc *dlc);
int q931_send_connect_channel(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_channel *channel);

int q931_send_connect_acknowledge(
	struct q931_call *call,
	struct q931_dlc *dlc);

int q931_send_disconnect(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_causeset *causeset);
int q931_send_disconnect_pi(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_causeset *causeset);

int q931_send_info(
	struct q931_call *call,
	struct q931_dlc *dlc);

int q931_send_notify(
	struct q931_call *call,
	struct q931_dlc *dlc);

int q931_send_progress(
	struct q931_call *call,
	struct q931_dlc *dlc);

int q931_send_release(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_causeset *causeset);

int q931_send_release_complete(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_causeset *causeset);

int q931_send_resume(
	struct q931_call *call,
	struct q931_dlc *dlc);

int q931_send_resume_acknowledge(
	struct q931_call *call,
	struct q931_dlc *dlc);

int q931_send_resume_reject(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_causeset *causeset);

int q931_send_setup(
	struct q931_call *call,
	struct q931_dlc *dlc,
	enum q931_setup_mode setup_mode);
int q931_send_setup_channel(
	struct q931_call *call,
	struct q931_dlc *dlc,
	enum q931_setup_mode setup_mode,
	const struct q931_channel *channel);

int q931_send_setup_acknowledge(
	struct q931_call *call,
	struct q931_dlc *dlc);
int q931_send_setup_acknowledge_channel(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_channel *channel);
int q931_send_setup_acknowledge_channel_progind(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_channel *channel,
	enum q931_ie_progress_indicator_progress_description progind_descr);

int q931_send_status(
	struct q931_call *call,
	struct q931_dlc *dlc,
	__u8 state,
	const struct q931_causeset *causeset);
int q931_global_send_status(
	struct q931_global_call *gc,
	struct q931_dlc *dlc,
	const struct q931_causeset *causeset);

int q931_send_status_enquiry(
	struct q931_call *call,
	struct q931_dlc *dlc);

int q931_send_suspend(
	struct q931_call *call,
	struct q931_dlc *dlc);

int q931_send_suspend_acknowledge(
	struct q931_call *call,
	struct q931_dlc *dlc);

int q931_send_suspend_reject(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_causeset *causeset);

int q931_send_restart(
	struct q931_global_call *gc,
	struct q931_dlc *dlc,
	const struct q931_chanset *chanset);
int q931_send_restart_acknowledge(
	struct q931_global_call *gc,
	struct q931_dlc *dlc,
	const struct q931_chanset *chanset);


#endif
