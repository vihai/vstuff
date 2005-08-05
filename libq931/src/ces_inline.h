#ifndef _LIBQ931_CES_INLINE_H
#define _LIBQ931_CES_INLINE_H

#ifdef Q931_PRIVATE

static inline int q931_ces_send_alerting(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_ALERTING, ies);
}

static inline int q931_ces_send_call_proceeding(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_CALL_PROCEEDING, ies);
}

static inline int q931_ces_send_connect(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_CONNECT, ies);
}

static inline int q931_ces_send_connect_acknowledge(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_CONNECT_ACKNOWLEDGE, ies);
}

static inline int q931_ces_send_disconnect(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_DISCONNECT, ies);
}

static inline int q931_ces_send_information(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_INFORMATION, ies);
}

static inline int q931_ces_send_notify(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_NOTIFY, ies);
}

static inline int q931_ces_send_progress(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_PROGRESS, ies);
}

static inline int q931_ces_send_release(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_RELEASE, ies);
}

static inline int q931_ces_send_release_complete(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_RELEASE_COMPLETE, ies);
}

static inline int q931_ces_send_resume(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_RESUME, ies);
}

static inline int q931_ces_send_resume_acknowledge(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_RESUME_ACKNOWLEDGE, ies);
}

static inline int q931_ces_send_resume_reject(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_RESUME_REJECT, ies);
}

static inline int q931_ces_send_setup(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_SETUP, ies);
}

static inline int q931_ces_send_setup_acknowledge(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_SETUP_ACKNOWLEDGE, ies);
}

static inline int q931_ces_send_status(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_STATUS, ies);
}

static inline int q931_ces_send_status_enquiry(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_STATUS_ENQUIRY, ies);
}

static inline int q931_ces_send_suspend(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_SUSPEND, ies);
}

static inline int q931_ces_send_suspend_acknowledge(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_SUSPEND_ACKNOWLEDGE, ies);
}

static inline int q931_ces_send_suspend_reject(
	struct q931_ces *ces,
	const struct q931_ies *ies)
{
	return q931_send_message(ces->call, ces->dlc, Q931_MT_SUSPEND_REJECT, ies);
}

#endif

#endif
