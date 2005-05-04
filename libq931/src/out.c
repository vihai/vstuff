#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <assert.h>
#include <stdarg.h>
#include <fcntl.h>

#include <lapd.h>

#define Q931_PRIVATE

#include "lib.h"
#include "logging.h"
#include "msgtype.h"
#include "ie.h"
#include "out.h"
#include "call.h"
#include "intf.h"
#include "proto.h"
#include "callref.h"

#include "ie_sending_complete.h"
#include "ie_bearercap.h"
#include "ie_cdpn.h"
#include "ie_cgpn.h"
#include "ie_chanid.h"
#include "ie_progind.h"
#include "ie_cause.h"
#include "ie_call_state.h"
#include "ie_hlc.h"
#include "ie_restind.h"

static int q931_prepare_header(
	const struct q931_call *call,
	__u8 *frame,
	__u8 message_type)
{
	int size = 0;

	report_call(call, LOG_DEBUG, "Sending %s\n",
		q931_message_type_to_text(message_type));

	struct q931_header *hdr = (struct q931_header *)(frame + size);
	size += sizeof(struct q931_header);

	memset(hdr, 0x00, sizeof(*hdr));

	hdr->protocol_discriminator = Q931_PROTOCOL_DISCRIMINATOR_Q931;

	assert(call->intf);
	assert(call->intf->call_reference_len >=1 &&
	       call->intf->call_reference_len <= 4);

	hdr->call_reference_len =
		call->intf->call_reference_len;

	// Call reference
	assert(call->call_reference >= 0 &&
	       call->call_reference < (1 << ((hdr->call_reference_len * 8) - 1)));

	q931_make_callref(frame + size,
		hdr->call_reference_len,
		call->call_reference,
		call->direction == Q931_CALL_DIRECTION_INBOUND ?
		Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE :
		Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE);

	size += hdr->call_reference_len;
	
	__u8 *message_type_onwire = (__u8 *)(frame + size);
	size++;
	*message_type_onwire = message_type;

	return size;
}

static int q931_global_prepare_header(
	const struct q931_global_call *gc,
	__u8 *frame,
	__u8 message_type)
{
	int size = 0;

	struct q931_header *hdr = (struct q931_header *)(frame + size);
	size += sizeof(struct q931_header);

	memset(hdr, 0x00, sizeof(*hdr));

	hdr->protocol_discriminator = Q931_PROTOCOL_DISCRIMINATOR_Q931;

	assert(gc->intf);
	assert(gc->intf->call_reference_len >=1 &&
	       gc->intf->call_reference_len <= 4);

	hdr->call_reference_len =
		gc->intf->call_reference_len;

	q931_make_callref(frame + size,
		hdr->call_reference_len,
		0,
		Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE);

	size += hdr->call_reference_len;
	
	__u8 *message_type_onwire = (__u8 *)(frame + size);
	size++;
	*message_type_onwire = message_type;

	return size;
}

static int q931_send_frame(struct q931_dlc *dlc, void *frame, int size)
{
	assert(dlc);
	assert(frame);
	assert(size > 0);

	struct msghdr msg;
	struct iovec iov;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	iov.iov_base = frame;
	iov.iov_len = size;

	if (dlc->status != DLC_CONNECTED) {
/*		int oldflags;

		if (fcntl(dlc->socket, F_GETFL, &oldflags) < 0) {
			report_dlc(dlc, LOG_ERR, "fcntl: %s\n", strerror(errno));
			return errno;
		}

		if (fcntl(dlc->socket, F_SETFL, oldflags | O_NONBLOCK) < 0) {
			report_dlc(dlc, LOG_ERR, "fcntl: %s\n", strerror(errno));
			return errno;
		}*/

		if (connect(dlc->socket, NULL, 0) < 0) {
			return errno;
		}

		q931_dl_establish_confirm(dlc);

/*		if (fcntl(dlc->socket, F_SETFL, oldflags) < 0) {
			report_dlc(dlc, LOG_ERR, "fcntl: %s\n", strerror(errno));
			return errno;
		}*/

	}

	if (sendmsg(dlc->socket, &msg, 0) < 0) {
		report_dlc(dlc, LOG_ERR, "sendmsg error: %s\n",strerror(errno));
		return errno;
	}

	return 0;
}

static int q931_send_uframe(struct q931_dlc *dlc, void *frame, int size)
{
	assert(dlc);
	assert(frame);
	assert(size > 0);

	struct msghdr msg;
	struct iovec iov;
	struct sockaddr_lapd sal;

	msg.msg_name = &sal;
	msg.msg_namelen = sizeof(sal);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	sal.sal_bcast = 0;

	iov.iov_base = frame;
	iov.iov_len = size;

	if (sendmsg(dlc->socket, &msg, MSG_OOB) < 0) {
		report_dlc(dlc, LOG_ERR, "sendmsg error: %s\n",strerror(errno));
		return errno;
	}

	return 0;
}

/**************** ALERTING
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Channel Identification	both	O
 * Facility			both	O
 * Progress Indicator		both	O
 * Display			n->u	O
 * User-User			both	O
 *
 */

int q931_send_alerting(
	struct q931_call *call,
	struct q931_dlc *dlc)
{
	int size = 0;

	assert(call);
	assert(dlc);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_ALERTING);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/*************** CALL PROCEEDING
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Channel Identification	both	O
 * Progress Indicator		both	O
 * Display			n->u	O
 *
 */

int q931_send_call_proceeding(
	struct q931_call *call,
	struct q931_dlc *dlc)
{
	int size = 0;

	assert(call);
	assert(dlc);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_CALL_PROCEEDING);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

int q931_send_call_proceeding_channel(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_channel *channel)
{
	int size = 0;

	assert(call);
	assert(dlc);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_CALL_PROCEEDING);

	struct q931_chanset cs;
	q931_chanset_init(&cs);
	q931_chanset_add(&cs, channel->id);

	if (call->intf->type == Q931_INTF_TYPE_PRA) {
		size += q931_append_ie_channel_identification_pra(
				call->intf->sendbuf + size,
				Q931_IE_CI_ICS_PRA_INDICATED,
				Q931_IE_CI_PE_EXCLUSIVE,
				&cs);
	} else {
		size += q931_append_ie_channel_identification_bra(
				call->intf->sendbuf + size,
				Q931_IE_CI_PE_EXCLUSIVE,
				&cs);
	}

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/*************** CONNECT
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Channel Identification	both	O
 * Facility			both	O
 * Progress Indicator		both	O
 * Display			n->u	O
 * Date/Time			n->u	O
 * Low Layer Compatibility	both	O
 * User-User			both	O
 *
 */

int q931_send_connect(
	struct q931_call *call,
	struct q931_dlc *dlc)
{
	int size = 0;

	assert(call);
	assert(dlc);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_CONNECT);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

int q931_send_connect_channel(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_channel *channel)
{
	int size = 0;

	assert(call);
	assert(dlc);
	assert(channel);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_CONNECT);

	struct q931_chanset cs;
	q931_chanset_init(&cs);
	q931_chanset_add(&cs, channel->id);

	if (call->intf->type == Q931_INTF_TYPE_PRA) {
		size += q931_append_ie_channel_identification_pra(
				call->intf->sendbuf + size,
				Q931_IE_CI_ICS_PRA_INDICATED,
				Q931_IE_CI_PE_EXCLUSIVE,
				&cs);
	} else {
		size += q931_append_ie_channel_identification_bra(
				call->intf->sendbuf + size,
				Q931_IE_CI_PE_EXCLUSIVE,
				&cs);
	}

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/*************** CONNECT ACKNOWLEDGE
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Channel Identification	n->u	O
 * Display			n->u	O
 *
 */

int q931_send_connect_acknowledge(
	struct q931_call *call,
	struct q931_dlc *dlc)
{
	int size = 0;

	assert(call);
	assert(dlc);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_CONNECT_ACKNOWLEDGE);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/**************** DISCONNECT
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Cause			both	M
 * Facility			both	O
 * Progress Indicator		both	O
 * Display			n->u	O
 * User-User			both	O
 *
 */

int q931_send_disconnect(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_causeset *causeset)
{
	int size = 0;

	assert(call);
	assert(dlc);
	assert(causeset);

	q931_causeset_copy(&call->disconnect_cause, causeset);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_DISCONNECT);

	size += q931_append_ie_causes(call->intf->sendbuf + size,
			call->intf->role == LAPD_ROLE_TE ? // FIXME
				Q931_IE_C_L_USER :
				Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER,
			causeset);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

int q931_send_disconnect_pi(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_causeset *causeset)
{
	int size = 0;

	assert(call);
	assert(dlc);
	assert(causeset);

	q931_causeset_copy(&call->disconnect_cause, causeset);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_DISCONNECT);

	size += q931_append_ie_causes(call->intf->sendbuf + size,
			call->intf->role == LAPD_ROLE_TE ? // FIXME
				Q931_IE_C_L_USER :
				Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER,
			causeset);

	size += q931_append_ie_progress_indicator(call->intf->sendbuf + size,
			call->intf->role == LAPD_ROLE_TE ? // FIXME
				Q931_IE_C_L_USER :
				Q931_IE_PI_LOCATION_PRIVATE_NET_SERVING_LOCAL_USER,
			Q931_IE_PI_PD_IN_BAND_INFORMATION);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/****************** INFORMATION
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Sending Complete		both	O
 * Cause			n->u	O
 * Display			n->u	O
 * Keypad Facility		u->n	O
 * Called Party Number		both	O
 *
 */

int q931_send_info(
	struct q931_call *call,
	struct q931_dlc *dlc)
{
	int size = 0;

	assert(call);
	assert(dlc);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_INFORMATION);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/******************* NOTIFY
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Notification indicator	both	M
 * Display			n->u	O
 *
 */

int q931_send_notify(
	struct q931_call *call,
	struct q931_dlc *dlc)
{
	int size = 0;

	assert(call);
	assert(dlc);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_INFORMATION);

	// FIXME add Notification indicator

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/*************** PROGRESS
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Cause			n->u	O
 * Progress Indicator		both	M
 * Display			n->u	O
 * User-User			both	O
 *
 */

int q931_send_progress(
	struct q931_call *call,
	struct q931_dlc *dlc)
{
	int size = 0;

	assert(call);
	assert(dlc);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_PROGRESS);

	// FIXME add Progress indicator

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/**************** RELEASE
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Cause			both	O
 * Facility			both	O
 * Display			n->u	O
 * User-User			both	O
 *
 */

int q931_send_release(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_causeset *causeset)
{
	int size = 0;

	assert(call);
	assert(dlc);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_RELEASE);

	if (causeset) {
		q931_causeset_copy(&call->release_cause, causeset);

		size += q931_append_ie_causes(call->intf->sendbuf + size,
				call->intf->role == LAPD_ROLE_TE ?
					Q931_IE_C_L_USER :
					Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER, // FIXME
				causeset);
	}

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/**************** RELEASE COMPLETE
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Cause			both	O
 * Facility			both	O
 * Display			n->u	O
 * User-User			u->n	O
 *
 */

int q931_send_release_complete(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_causeset *causeset)
{
	int size = 0;

	assert(call);
	assert(dlc);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_RELEASE_COMPLETE);

	if (causeset) {
		size += q931_append_ie_causes(call->intf->sendbuf + size,
				call->intf->role == LAPD_ROLE_TE ?
					Q931_IE_C_L_USER :
					Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER, // FIXME
				causeset);
	}

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/***************** RESUME
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Call Identity		u->n	O
 *
 */

int q931_send_resume(
	struct q931_call *call,
	struct q931_dlc *dlc)
{
	int size = 0;

	assert(call);
	assert(dlc);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_RESUME);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/***************** RESUME ACKNOWLEDGE
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Channel Identification	n->u	M
 * Display			n->u	O
 *
 */

int q931_send_resume_acknowledge(
	struct q931_call *call,
	struct q931_dlc *dlc)
{
	int size = 0;

	assert(call);
	assert(dlc);
	assert(call->channel);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_RESUME_ACKNOWLEDGE);

	struct q931_chanset cs;
	q931_chanset_init(&cs);
	q931_chanset_add(&cs, call->channel->id);

	if (call->intf->type == Q931_INTF_TYPE_PRA) {
		size += q931_append_ie_channel_identification_pra(
				call->intf->sendbuf + size,
				Q931_IE_CI_ICS_PRA_INDICATED,
				Q931_IE_CI_PE_EXCLUSIVE,
				&cs);
	} else {
		size += q931_append_ie_channel_identification_bra(
				call->intf->sendbuf + size,
				Q931_IE_CI_PE_EXCLUSIVE,
				&cs);
	}

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/******************* RESUME REJECT
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Cause			n->u	M
 * Display			n->u	O
 *
 */

int q931_send_resume_reject(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_causeset *causeset)
{
	int size = 0;

	assert(call);
	assert(dlc);
	assert(causeset);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_RESUME_REJECT);
	size += q931_append_ie_causes(call->intf->sendbuf + size,
			call->intf->role == LAPD_ROLE_TE ? // FIXME
				Q931_IE_C_L_USER :
				Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER,
			causeset);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/****************** SETUP
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Sending Complete		both	O
 * Bearer Capability		both	M
 * Channel Identification	both	O
 * Facility			both	O
 * Progress Indicator		both	O
 * Net. Spec. Facilities	both	O
 * Display			n->u	O
 * Keypad Facility		u->n	O
 * Calling Party Number		both	O
 * Calling Party Subaddress	both	O
 * Called Party Number		both	O
 * Called Party Subaddress	both	O
 * Transit Network Selection	u->n	O
 * Low Layer Compatibility	both	O
 * High Layer Compatibility	both	O
 * User-User			both	O
 *
 */

int q931_send_setup(
	struct q931_call *call,
	struct q931_dlc *dlc,
	enum q931_setup_mode setup_mode)
{
	int size = 0;

	assert(call);
	assert(dlc);
	assert(call->called_number);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_SETUP);
	size += q931_append_ie_bearer_capability_alaw(call->intf->sendbuf + size);
	size += q931_append_ie_called_party_number(
			call->intf->sendbuf + size,
			call->called_number);

/*	if (strlen(call->calling_number))
		size += q931_append_ie_calling_party_number(
				call->intf->sendbuf + size,
				call->calling_number);*/

	size += q931_append_ie_high_layer_compatibility_telephony(
			call->intf->sendbuf + size);

	if (call->sending_complete)
		size += q931_append_ie_sending_complete(call->intf->sendbuf + size);

	if (setup_mode == Q931_SETUP_POINT_TO_POINT)
		return q931_send_frame(dlc, call->intf->sendbuf, size);
	else if (setup_mode == Q931_SETUP_BROADCAST)
		return q931_send_uframe(&call->intf->bc_dlc, call->intf->sendbuf, size);
	else
		assert(0);
}

int q931_send_setup_channel(
	struct q931_call *call,
	struct q931_dlc *dlc,
	enum q931_setup_mode setup_mode,
	const struct q931_channel *channel)
{
	int size = 0;

	assert(call);
	assert(dlc);
	assert(call->called_number);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_SETUP);
	size += q931_append_ie_bearer_capability_alaw(call->intf->sendbuf + size);
	size += q931_append_ie_called_party_number(
			call->intf->sendbuf + size,
			call->called_number);

/*	if (strlen(call->calling_number))
		size += q931_append_ie_calling_party_number(
				call->intf->sendbuf + size,
				call->calling_number);*/

	if (channel) {
		struct q931_chanset cs;
		q931_chanset_init(&cs);
		q931_chanset_add(&cs, channel->id);

		if (call->intf->type == Q931_INTF_TYPE_PRA) {
			size += q931_append_ie_channel_identification_pra(
					call->intf->sendbuf + size,
					Q931_IE_CI_ICS_PRA_INDICATED,
					Q931_IE_CI_PE_EXCLUSIVE,
					&cs);
		} else {
			size += q931_append_ie_channel_identification_bra(
					call->intf->sendbuf + size,
					Q931_IE_CI_PE_EXCLUSIVE,
					&cs);
		}
	} else {
		if (call->intf->type == Q931_INTF_TYPE_PRA) {
			size += q931_append_ie_channel_identification_pra(
					call->intf->sendbuf + size,
					Q931_IE_CI_ICS_PRA_NO_CHANNEL,
					Q931_IE_CI_PE_PREFERRED,
					NULL);
		} else {
			size += q931_append_ie_channel_identification_bra(
					call->intf->sendbuf + size,
					Q931_IE_CI_PE_PREFERRED,
					NULL);
		}
	}

	size += q931_append_ie_high_layer_compatibility_telephony(call->intf->sendbuf + size);

	if (call->sending_complete)
		size += q931_append_ie_sending_complete(call->intf->sendbuf + size);

	if (setup_mode == Q931_SETUP_POINT_TO_POINT)
		return q931_send_frame(dlc, call->intf->sendbuf, size);
	else if (setup_mode == Q931_SETUP_BROADCAST)
		return q931_send_uframe(&call->intf->bc_dlc, call->intf->sendbuf, size);
	else
		assert(0);
}

/****************** SETUP ACKNOWLEDGE
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Channel Identification	both	O
 * Progress Indicator		both	O
 * Display			n->u	O
 *
 */

int q931_send_setup_acknowledge(
	struct q931_call *call,
	struct q931_dlc *dlc)
{
	int size = 0;

	assert(call);
	assert(dlc);

	size += q931_prepare_header(call, call->intf->sendbuf,
			Q931_MT_SETUP_ACKNOWLEDGE);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

int q931_send_setup_acknowledge_channel(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_channel *channel)
{
	int size = 0;

	assert(call);
	assert(dlc);
	assert(channel);

	size += q931_prepare_header(call, call->intf->sendbuf,
			Q931_MT_SETUP_ACKNOWLEDGE);

	struct q931_chanset cs;
	q931_chanset_init(&cs);
	q931_chanset_add(&cs, channel->id);

	if (call->intf->type == Q931_INTF_TYPE_PRA) {
		size += q931_append_ie_channel_identification_pra(
				call->intf->sendbuf + size,
				Q931_IE_CI_ICS_PRA_INDICATED,
				Q931_IE_CI_PE_EXCLUSIVE,
				&cs);
	} else {
		size += q931_append_ie_channel_identification_bra(
				call->intf->sendbuf + size,
				Q931_IE_CI_PE_EXCLUSIVE,
				&cs);
	}

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

int q931_send_setup_acknowledge_channel_progind(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_channel *channel,
	enum q931_ie_progress_indicator_progress_description progind_descr)
{
	int size = 0;

	assert(call);
	assert(dlc);
	assert(channel);

	size += q931_prepare_header(call, call->intf->sendbuf,
			Q931_MT_SETUP_ACKNOWLEDGE);

	struct q931_chanset cs;
	q931_chanset_init(&cs);
	q931_chanset_add(&cs, channel->id);

	if (call->intf->type == Q931_INTF_TYPE_PRA) {
		size += q931_append_ie_channel_identification_pra(
				call->intf->sendbuf + size,
				Q931_IE_CI_ICS_PRA_INDICATED,
				Q931_IE_CI_PE_EXCLUSIVE,
				&cs);
	} else {
		size += q931_append_ie_channel_identification_bra(
				call->intf->sendbuf + size,
				Q931_IE_CI_PE_EXCLUSIVE,
				&cs);
	}

	size += q931_append_ie_progress_indicator(
			call->intf->sendbuf + size,
			Q931_IE_PI_LOCATION_PRIVATE_NET_SERVING_LOCAL_USER,
			progind_descr);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/***************** STATUS
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Cause			both	M
 * Call State			both	M
 * Display			n->u	O
 *
 */

int q931_send_status(
	struct q931_call *call,
	struct q931_dlc *dlc,
	__u8 state,
	const struct q931_causeset *causeset)
{
	int size = 0;

	assert(call);
	assert(dlc);
	assert(causeset);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_STATUS);

	size += q931_append_ie_causes(call->intf->sendbuf + size,
			call->intf->role == LAPD_ROLE_TE ? // FIXME
				Q931_IE_C_L_USER :
				Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER,
			causeset);

	size += q931_append_ie_call_state(call->intf->sendbuf + size, state);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

int q931_global_send_status(
	struct q931_global_call *gc,
	struct q931_dlc *dlc,
	const struct q931_causeset *causeset)
{
	int size = 0;

	assert(gc);
	assert(dlc);

	size += q931_global_prepare_header(gc, gc->intf->sendbuf, Q931_MT_STATUS);

	if (causeset) {
		size += q931_append_ie_causes(dlc->intf->sendbuf + size,
				dlc->intf->role == LAPD_ROLE_TE ? // FIXME
					Q931_IE_C_L_USER :
					Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER,
				causeset);
	}

	size += q931_append_ie_call_state(dlc->intf->sendbuf + size, gc->state);

	return q931_send_frame(dlc, dlc->intf->sendbuf, size);
}

int q931_send_restart(
	struct q931_global_call *gc,
	struct q931_dlc *dlc,
	const struct q931_chanset *chanset)
{
	int size = 0;

	assert(gc);
	assert(dlc);

	size += q931_global_prepare_header(gc, gc->intf->sendbuf, Q931_MT_RESTART);
	
	if (chanset) {
		if (gc->intf->type == Q931_INTF_TYPE_PRA) {
			size += q931_append_ie_channel_identification_pra(
					dlc->intf->sendbuf + size,
					Q931_IE_CI_ICS_PRA_INDICATED,
					Q931_IE_CI_PE_EXCLUSIVE,
					chanset);
		} else {
			size += q931_append_ie_channel_identification_bra(
					dlc->intf->sendbuf + size,
					Q931_IE_CI_PE_EXCLUSIVE,
					chanset);
		}

		size += q931_append_ie_restart_indicator(dlc->intf->sendbuf + size,
				Q931_IE_RI_C_INDICATED);
	} else {
		size += q931_append_ie_restart_indicator(dlc->intf->sendbuf + size,
				Q931_IE_RI_C_SIGNLE_INTERFACE);
	}

	return q931_send_frame(dlc, dlc->intf->sendbuf, size);
}

int q931_send_restart_acknowledge(
	struct q931_global_call *gc,
	struct q931_dlc *dlc,
	const struct q931_chanset *chanset)
{
	int size = 0;

	assert(gc);
	assert(dlc);

	size += q931_global_prepare_header(gc, gc->intf->sendbuf,
			Q931_MT_RESTART_ACKNOWLEDGE);
	
	if (chanset) {
		if (gc->intf->type == Q931_INTF_TYPE_PRA) {
			size += q931_append_ie_channel_identification_pra(
					dlc->intf->sendbuf + size,
					Q931_IE_CI_ICS_PRA_INDICATED,
					Q931_IE_CI_PE_EXCLUSIVE,
					chanset);
		} else {
			size += q931_append_ie_channel_identification_bra(
					dlc->intf->sendbuf + size,
					Q931_IE_CI_PE_EXCLUSIVE,
					chanset);
		}

		size += q931_append_ie_restart_indicator(dlc->intf->sendbuf + size,
				Q931_IE_RI_C_INDICATED);
	} else {
		size += q931_append_ie_restart_indicator(dlc->intf->sendbuf + size,
				Q931_IE_RI_C_SIGNLE_INTERFACE);
	}

	return q931_send_frame(dlc, dlc->intf->sendbuf, size);
}

/*************** STATUS ENQUIRY
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Display			n->u	O
 *
 */

int q931_send_status_enquiry(
	struct q931_call *call,
	struct q931_dlc *dlc)
{
	int size = 0;

	assert(call);
	assert(dlc);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_STATUS_ENQUIRY);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/*********** SUSPEND
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Call Identity		u->n	O
 *
 */

int q931_send_suspend(
	struct q931_call *call,
	struct q931_dlc *dlc)
{
	int size = 0;

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_SUSPEND);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/************** SUSPEND ACKNOWLEDGE
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Display			n->u	O
 *
 */

int q931_send_suspend_acknowledge(
	struct q931_call *call,
	struct q931_dlc *dlc)
{
	int size = 0;

	assert(call);
	assert(dlc);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_SUSPEND_ACKNOWLEDGE);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

/**************** SUSPEND REJECT
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Cause			both	M
 * Display			n->u	O
 *
 */

int q931_send_suspend_reject(
	struct q931_call *call,
	struct q931_dlc *dlc,
	const struct q931_causeset *causeset)
{
	int size = 0;

	assert(call);
	assert(dlc);
	assert(causeset);

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_SUSPEND_REJECT);
	size += q931_append_ie_causes(call->intf->sendbuf + size,
			call->intf->role == LAPD_ROLE_TE ? // FIXME
				Q931_IE_C_L_USER :
				Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER,
			causeset);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}
