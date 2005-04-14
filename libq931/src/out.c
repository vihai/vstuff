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

#include "q931.h"
#include "logging.h"
#include "msgtype.h"
#include "ie.h"
#include "out.h"
#include "call.h"
#include "intf.h"
#include "proto.h"

static void q931_make_callref(
	void *void_buf,
	int size,
	q931_callref callref,
	int direction)
{
	int i;
	__u8 *buf = void_buf;

	for (i=0; i<size; i++) {
		buf[i] = callref & (0xFF << ((size-i-1) * 8));

		if (i == 0 &&
		    direction == Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE)
			buf[i] |= 0x80;
	}
}

static int q931_prepare_header(const struct q931_call *call,
	__u8 *frame,
	__u8 message_type)
{
	int size = 0;

	// Header

	struct q931_header *hdr = (struct q931_header *)(call->intf->sendbuf + size);
	size += sizeof(struct q931_header);

	memset(hdr, 0x00, sizeof(*hdr));

	hdr->protocol_discriminator = Q931_PROTOCOL_DISCRIMINATOR_Q931;

	assert(call->intf);
	assert(call->intf->call_reference_size >=1 &&
	       call->intf->call_reference_size <= 4);

	hdr->call_reference_size =
		call->intf->call_reference_size;

	// Call reference
	assert(call->call_reference >= 0 &&
	       call->call_reference < (1 << ((hdr->call_reference_size * 8) - 1)));

	q931_make_callref(call->intf->sendbuf + size,
		hdr->call_reference_size,
		call->call_reference,
		call->direction == Q931_CALL_DIRECTION_INBOUND ?
		Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE :
		Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE);

	size += hdr->call_reference_size;
	
	__u8 *message_type_onwire = (__u8 *)(call->intf->sendbuf + size);
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

	report_dlc(dlc, LOG_DEBUG, "q931_send_uframe\n");

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

	report_call(call, LOG_DEBUG, "Sending ALERTING\n");

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

	report_call(call, LOG_DEBUG, "Sending CALL_PROCEEDING\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_CALL_PROCEEDING);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

int q931_send_call_proceeding_channel(
	struct q931_call *call,
	struct q931_dlc *dlc,
	struct q931_channel *channel)
{
	int size = 0;

	assert(call);
	assert(dlc);

	report_call(call, LOG_DEBUG, "Sending CALL_PROCEEDING\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_CALL_PROCEEDING);

	if (call->intf->type == Q931_INTF_TYPE_BRA_POINT_TO_POINT ||
	    call->intf->type == Q931_INTF_TYPE_BRA_MULTIPOINT) {
		if (channel->id == 0) {
			size += q931_append_ie_channel_identification_bra(
				call->intf->sendbuf + size,
				Q931_IE_CI_PE_EXCLUSIVE,
				Q931_IE_CI_ICS_BRA_B1);
		} else {
			size += q931_append_ie_channel_identification_bra(
				call->intf->sendbuf + size,
				Q931_IE_CI_PE_EXCLUSIVE,
				Q931_IE_CI_ICS_BRA_B2);
		}
	} else {
		size += q931_append_ie_channel_identification_pra(call->intf->sendbuf + size,
				Q931_IE_CI_ICS_PRA_INDICATED,
				Q931_IE_CI_PE_EXCLUSIVE,
				channel->id);
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

	report_call(call, LOG_DEBUG, "Sending CONNECT\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_CONNECT);

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

	report_call(call, LOG_DEBUG, "Sending CONNECT_ACKNOWLEDGE\n");

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
	enum q931_ie_cause_value cause)
{
	int size = 0;

	assert(call);
	assert(dlc);

	report_call(call, LOG_DEBUG, "Sending DISCONNECT\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_DISCONNECT);

	size += q931_append_ie_cause(call->intf->sendbuf + size,
			call->intf->role == LAPD_ROLE_TE ? // FIXME
				Q931_IE_C_L_USER :
				Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER,
			cause);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

int q931_send_disconnect_pi(
	struct q931_call *call,
	struct q931_dlc *dlc,
	enum q931_ie_cause_value cause)
{
	int size = 0;

	assert(call);
	assert(dlc);

	report_call(call, LOG_DEBUG, "Sending DISCONNECT\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_DISCONNECT);

	size += q931_append_ie_cause(call->intf->sendbuf + size,
			call->intf->role == LAPD_ROLE_TE ? // FIXME
				Q931_IE_C_L_USER :
				Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER,
			cause);

	size += q931_append_ie_progress_indicator(call->intf->sendbuf + size,
			call->intf->role == LAPD_ROLE_TE ? // FIXME
				Q931_IE_C_L_USER :
				Q931_IE_PI_LOCATION_PRIVATE_NET_SERVING_LOCAL_USER,
			Q931_IE_PI_PD_IN_BAND_INFORMATION_OR_APPROPRIATE_PATTERN_AVAILABLE);

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

	report_call(call, LOG_DEBUG, "Sending INFORMATION\n");

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

	report_call(call, LOG_DEBUG, "Sending NOTIFY\n");

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

	report_call(call, LOG_DEBUG, "Sending PROGRESS\n");

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
	struct q931_dlc *dlc)
{
	int size = 0;

	assert(call);
	assert(dlc);

	report_call(call, LOG_DEBUG, "Sending RELEASE\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_RELEASE);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

int q931_send_release_cause(
	struct q931_call *call,
	struct q931_dlc *dlc,
	enum q931_ie_cause_value cause_value)
{
	int size = 0;

	assert(call);
	assert(dlc);

	report_call(call, LOG_DEBUG, "Sending RELEASE\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_RELEASE);
	size += q931_append_ie_cause(call->intf->sendbuf + size,
			call->intf->role == LAPD_ROLE_TE ?
				Q931_IE_C_L_USER :
				Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER, // FIXME
			cause_value);

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
	struct q931_dlc *dlc)
{
	int size = 0;

	assert(call);
	assert(dlc);

	report_call(call, LOG_DEBUG, "Sending RELEASE_COMPLETE\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_RELEASE_COMPLETE);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

int q931_send_release_complete_cause(
	struct q931_call *call,
	struct q931_dlc *dlc,
	enum q931_ie_cause_value cause_value)
{
	int size = 0;

	assert(call);
	assert(dlc);

	report_call(call, LOG_DEBUG, "Sending RELEASE_COMPLETE\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_RELEASE_COMPLETE);

	size += q931_append_ie_cause(call->intf->sendbuf + size,
			call->intf->role == LAPD_ROLE_TE ?
				Q931_IE_C_L_USER :
				Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER, // FIXME
			cause_value);

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

	report_call(call, LOG_DEBUG, "Sending RESUME\n");

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

	report_call(call, LOG_DEBUG, "Sending RESUME_ACKNOWLEDGE\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_RESUME_ACKNOWLEDGE);

	if (call->intf->type == Q931_INTF_TYPE_BRA_POINT_TO_POINT ||
	    call->intf->type == Q931_INTF_TYPE_BRA_MULTIPOINT) {
		if (call->channel->id == 0) {
			size += q931_append_ie_channel_identification_bra(
				call->intf->sendbuf + size,
				Q931_IE_CI_PE_EXCLUSIVE,
				Q931_IE_CI_ICS_BRA_B1);
		} else {
			size += q931_append_ie_channel_identification_bra(
				call->intf->sendbuf + size,
				Q931_IE_CI_PE_EXCLUSIVE,
				Q931_IE_CI_ICS_BRA_B2);
		}
	} else {
		size += q931_append_ie_channel_identification_pra(call->intf->sendbuf + size,
				Q931_IE_CI_ICS_PRA_INDICATED,
				Q931_IE_CI_PE_EXCLUSIVE,
				call->channel->id);
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
	enum q931_ie_cause_value cause)
{
	int size = 0;

	assert(call);
	assert(dlc);

	report_call(call, LOG_DEBUG, "Sending RESUME_REJECT\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_RESUME_REJECT);
	size += q931_append_ie_cause(call->intf->sendbuf + size,
			call->intf->role == LAPD_ROLE_TE ? // FIXME
				Q931_IE_C_L_USER :
				Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER,
			Q931_IE_C_CV_NORMAL_CALL_CLEARING);

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

	report_call(call, LOG_DEBUG, "Sending SETUP\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_SETUP);
	size += q931_append_ie_bearer_capability_alaw(call->intf->sendbuf + size);
	size += q931_append_ie_called_party_number(
			call->intf->sendbuf + size,
			call->called_number);

	if (strlen(call->calling_number))
		size += q931_append_ie_calling_party_number(
				call->intf->sendbuf + size,
				call->calling_number);

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
	struct q931_channel *channel)
{
	int size = 0;

	assert(call);
	assert(dlc);
	assert(call->called_number);

	report_call(call, LOG_DEBUG, "Sending SETUP\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_SETUP);
	size += q931_append_ie_bearer_capability_alaw(call->intf->sendbuf + size);
	size += q931_append_ie_called_party_number(
			call->intf->sendbuf + size,
			call->called_number);

	if (strlen(call->calling_number))
		size += q931_append_ie_calling_party_number(
				call->intf->sendbuf + size,
				call->calling_number);

	if (call->intf->type == Q931_INTF_TYPE_BRA_POINT_TO_POINT ||
	    call->intf->type == Q931_INTF_TYPE_BRA_MULTIPOINT) {
		if (channel) {
			if (channel->id == 0) {
				size += q931_append_ie_channel_identification_bra(
						call->intf->sendbuf + size,
						Q931_IE_CI_PE_EXCLUSIVE,
						Q931_IE_CI_ICS_BRA_B1);
			} else {
				size += q931_append_ie_channel_identification_bra(
						call->intf->sendbuf + size,
						Q931_IE_CI_PE_EXCLUSIVE,
						Q931_IE_CI_ICS_BRA_B2);
			}
		} else {	
			size += q931_append_ie_channel_identification_bra(
					call->intf->sendbuf + size,
					Q931_IE_CI_ICS_BRA_NO_CHANNEL,
					Q931_IE_CI_PE_PREFERRED);
		}
	} else {
		if (channel) {
			size += q931_append_ie_channel_identification_pra(call->intf->sendbuf + size,
					Q931_IE_CI_ICS_PRA_INDICATED,
					Q931_IE_CI_PE_EXCLUSIVE,
					channel->id);
		} else {
			size += q931_append_ie_channel_identification_pra(
					call->intf->sendbuf + size,
					Q931_IE_CI_ICS_PRA_NO_CHANNEL,
					Q931_IE_CI_PE_PREFERRED,
					0);
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

	report_call(call, LOG_DEBUG, "Sending SETUP_ACKNOWLEDGE\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_SETUP_ACKNOWLEDGE);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}

int q931_send_setup_acknowledge_channel(
	struct q931_call *call,
	struct q931_dlc *dlc,
	struct q931_channel *channel)
{
	int size = 0;

	assert(call);
	assert(dlc);
	assert(channel);

	report_call(call, LOG_DEBUG, "Sending SETUP_ACKNOWLEDGE\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_SETUP_ACKNOWLEDGE);

	if (call->intf->type == Q931_INTF_TYPE_BRA_POINT_TO_POINT ||
	    call->intf->type == Q931_INTF_TYPE_BRA_MULTIPOINT) {
		if (channel->id == 0) {
			size += q931_append_ie_channel_identification_bra(
				call->intf->sendbuf + size,
				Q931_IE_CI_PE_EXCLUSIVE,
				Q931_IE_CI_ICS_BRA_B1);
		} else {
			size += q931_append_ie_channel_identification_bra(
				call->intf->sendbuf + size,
				Q931_IE_CI_PE_EXCLUSIVE,
				Q931_IE_CI_ICS_BRA_B2);
		}
	} else {
		size += q931_append_ie_channel_identification_pra(call->intf->sendbuf + size,
				Q931_IE_CI_ICS_PRA_INDICATED,
				Q931_IE_CI_PE_EXCLUSIVE,
				channel->id);
	}

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
	enum q931_ie_cause_value cause)
{
	int size = 0;

	assert(call);
	assert(dlc);

	report_call(call, LOG_DEBUG, "Sending STATUS\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_STATUS);

	size += q931_append_ie_cause(call->intf->sendbuf + size,
			call->intf->role == LAPD_ROLE_TE ? // FIXME
				Q931_IE_C_L_USER :
				Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER,
			Q931_IE_C_CV_NORMAL_CALL_CLEARING);

	size += q931_append_ie_call_state(call->intf->sendbuf + size, state);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
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

	report_call(call, LOG_DEBUG, "Sending STATUS_ENQUIRY\n");

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

	report_call(call, LOG_DEBUG, "Sending SUSPEND\n");

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

	report_call(call, LOG_DEBUG, "Sending SUSPEND_ACKNOWLEDGE\n");

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
	enum q931_ie_cause_value cause)
{
	int size = 0;

	assert(call);
	assert(dlc);

	report_call(call, LOG_DEBUG, "Sending SUSPEND_REJECT\n");

	size += q931_prepare_header(call, call->intf->sendbuf, Q931_MT_SUSPEND_REJECT);
	size += q931_append_ie_cause(call->intf->sendbuf + size,
			call->intf->role == LAPD_ROLE_TE ? // FIXME
				Q931_IE_C_L_USER :
				Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER,
			Q931_IE_C_CV_NORMAL_CALL_CLEARING);

	return q931_send_frame(dlc, call->intf->sendbuf, size);
}
