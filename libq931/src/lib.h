#ifndef _LIBQ931_LIB_H
#define _LIBQ931_LIB_H

#include "dlc.h"
#include "list.h"
#include "logging.h"
#include "call.h"
#include "intf.h"

enum q931_mode
{
	UNKNOWN_MODE,
	CIRCUIT_MODE,
	PACKET_MODE
};

struct q931_lib
{
	struct list_head timers;
	struct list_head intfs;

	void *pvt;

	void (*report)(int level, const char *format, ...);
	void (*timer_update)(struct q931_lib *lib);

	void (*alerting_indication)(
		struct q931_call *call,
		const struct q931_ies *ies);
	void (*connect_indication)(
		struct q931_call *call,
		const struct q931_ies *ies);
	void (*disconnect_indication)(
		struct q931_call *call,
		const struct q931_ies *ies);
	void (*error_indication)(
		struct q931_call *call,
		const struct q931_ies *ies); // TE
	void (*info_indication)(
		struct q931_call *call,
		const struct q931_ies *ies);
	void (*more_info_indication)(
		struct q931_call *call,
		const struct q931_ies *ies);
	void (*notify_indication)(
		struct q931_call *call,
		const struct q931_ies *ies);
	void (*proceeding_indication)(
		struct q931_call *call,
		const struct q931_ies *ies);
	void (*progress_indication)(
		struct q931_call *call,
		const struct q931_ies *ies);
	void (*reject_indication)(
		struct q931_call *call,
		const struct q931_ies *ies);
	void (*release_confirm)(
		struct q931_call *call,
		const struct q931_ies *ies,
		enum q931_release_confirm_status status);//TE
	void (*release_indication)(
		struct q931_call *call,
		const struct q931_ies *ies);
	void (*resume_confirm)(
		struct q931_call *call,
		const struct q931_ies *ies,
		enum q931_resume_confirm_status status);//TE
	void (*resume_indication)(
		struct q931_call *call,
		const struct q931_ies *ies,
		__u8 *call_identity, int call_identity_len);
	void (*setup_complete_indication)(
		struct q931_call *call,
		const struct q931_ies *ies,
		enum q931_setup_complete_indication_status status);//TE
	void (*setup_confirm)(
		struct q931_call *call,
		const struct q931_ies *ies,
		enum q931_setup_confirm_status status);
	void (*setup_indication)(
		struct q931_call *call,
		const struct q931_ies *ies);
	void (*status_indication)(
		struct q931_call *call,
		const struct q931_ies *ies,
		enum q931_status_indication_status status);
	void (*suspend_confirm)(
		struct q931_call *call,
		const struct q931_ies *ies,
		enum q931_suspend_confirm_status status);//TE
	void (*suspend_indication)(
		struct q931_call *call,
		const struct q931_ies *ies,
		__u8 *call_identity, int call_identity_len);
	void (*timeout_indication)(
		struct q931_call *call,
		const struct q931_ies *ies);

	void (*connect_channel)(struct q931_channel *chan);
	void (*disconnect_channel)(struct q931_channel *chan);
	void (*start_tone)(struct q931_channel *chan,
		enum q931_tone_type tone);
	void (*stop_tone)(struct q931_channel *chan);

	void (*timeout_management_indication)(struct q931_global_call *gc);
	void (*status_management_indication)(struct q931_global_call *gc);
	void (*management_restart_confirm)(struct q931_global_call *gc,
		const struct q931_chanset *chanset);
};

static inline void q931_set_logger_func(
	struct q931_lib *lib,
	void (*report)(int level, const char *format, ...))
{
	lib->report = report;
}

struct q931_lib *q931_init();
void q931_leave(struct q931_lib *lib);
void q931_receive(struct q931_dlc *dlc);

#ifdef Q931_PRIVATE

typedef char BOOL;

void q931_dl_establish_confirm(struct q931_dlc *dlc);

#endif

#endif
