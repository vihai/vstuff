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

#include "list.h"

#include "q931.h"
#include "q931_log.h"
#include "q931_mt.h"
#include "q931_ie.h"
#include "q931_out.h"

static enum q931_ie_call_state_call_state
	q931_net_state_to_ie_state(
		enum q931_state state)
{
	switch (state) {
	case N0_NULL_STATE:
		return Q931_IE_CS_N0_NULL_STATE;
	case N1_CALL_INITIATED:
		return Q931_IE_CS_N1_CALL_INITIATED;
	case N2_OVERLAP_SENDING:
		return Q931_IE_CS_N2_OVERLAP_SENDING;
	case N3_OUTGOING_CALL_PROCEEDING:
		return Q931_IE_CS_N3_OUTGOING_CALL_PROCEEDING;
	case N4_CALL_DELIVERED:
		return Q931_IE_CS_N4_CALL_DELIVERED;
	case N6_CALL_PRESENT:
		return Q931_IE_CS_N6_CALL_PRESENT;
	case N7_CALL_RECEIVED:
		return Q931_IE_CS_N7_CALL_RECEIVED;
	case N8_CONNECT_REQUEST:
		return Q931_IE_CS_N8_CONNECT_REQUEST;
	case N9_INCOMING_CALL_PROCEEDING:
		return Q931_IE_CS_N9_INCOMING_CALL_PROCEEDING;
	case N10_ACTIVE:
		return Q931_IE_CS_N10_ACTIVE;
	case N11_DISCONNECT_REQUEST:
		return Q931_IE_CS_N11_DISCONNECT_REQUEST;
	case N12_DISCONNECT_INDICATION:
		return Q931_IE_CS_N12_DISCONNECT_INDICATION;
	case N15_SUSPEND_REQUEST:
		return Q931_IE_CS_N15_SUSPEND_REQUEST;
	case N17_RESUME_REQUEST:
		return Q931_IE_CS_N17_RESUME_REQUEST;
	case N19_RELEASE_REQUEST:
		return Q931_IE_CS_N19_RELEASE_REQUEST;
	case N22_CALL_ABORT:
		return Q931_IE_CS_N22_CALL_ABORT;
	case N25_OVERLAP_RECEIVING:
		return Q931_IE_CS_N25_OVERLAP_RECEIVING;
	case U0_NULL_STATE:
		return Q931_IE_CS_U0_NULL_STATE;
	case U1_CALL_INITIATED:
		return Q931_IE_CS_U1_CALL_INITIATED;
	case U2_OVERLAP_SENDING:
		return Q931_IE_CS_U2_OVERLAP_SENDING;
	case U3_OUTGOING_CALL_PROCEEDING:
		return Q931_IE_CS_U3_OUTGOING_CALL_PROCEEDING;
	case U4_CALL_DELIVERED:
		return Q931_IE_CS_U4_CALL_DELIVERED;
	case U6_CALL_PRESENT:
		return Q931_IE_CS_U6_CALL_PRESENT;
	case U7_CALL_RECEIVED:
		return Q931_IE_CS_U7_CALL_RECEIVED;
	case U8_CONNECT_REQUEST:
		return Q931_IE_CS_U8_CONNECT_REQUEST;
	case U9_INCOMING_CALL_PROCEEDING:
		return Q931_IE_CS_U9_INCOMING_CALL_PROCEEDING;
	case U10_ACTIVE:
		return Q931_IE_CS_U10_ACTIVE;
	case U11_DISCONNECT_REQUEST:
		return Q931_IE_CS_U11_DISCONNECT_REQUEST;
	case U12_DISCONNECT_INDICATION:
		return Q931_IE_CS_U12_DISCONNECT_INDICATION;
	case U15_SUSPEND_REQUEST:
		return Q931_IE_CS_U15_SUSPEND_REQUEST;
	case U17_RESUME_REQUEST:
		return Q931_IE_CS_U17_RESUME_REQUEST;
	case U19_RELEASE_REQUEST:
		return Q931_IE_CS_U19_RELEASE_REQUEST;
	case U25_OVERLAP_RECEIVING:
		return Q931_IE_CS_U25_OVERLAP_RECEIVING;
	}
}

static int q931_send_frame(const struct q931_dlc *dlc, void *frame, int size)
{
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

	report_dlc(dlc, LOG_DEBUG, "q931_send_frame\n");

	if (dlc->status != DLC_CONNECTED) {
		int oldflags;

		if (fcntl(dlc->socket, F_GETFL, &oldflags) < 0) {
			report_dlc(dlc, LOG_ERR, "fcntl: %s\n", strerror(errno));
			return errno;
		}

		if (fcntl(dlc->socket, F_SETFL, oldflags | O_NONBLOCK) < 0) {
			report_dlc(dlc, LOG_ERR, "fcntl: %s\n", strerror(errno));
			return errno;
		}

		if (connect(dlc->socket, NULL, 0) < 0) {
			return errno;
		}

		q931_dl_establish_confirm(call);

		if (fcntl(dlc->socket, F_SETFL, oldflags) < 0) {
			report_dlc(dlc, LOG_ERR, "fcntl: %s\n", strerror(errno));
			return errno;
		}

	}

	if (sendmsg(dlc->socket, &msg, 0) < 0) {
		report_dlc(dlc, LOG_ERR, "sendmsg error: %s\n",strerror(errno));
		return errno;
	}

	return 0;
}

// NOTE: Just the network can send broadcasts
static int q931_send_bc_uframe(struct q931_interface *interface, void *frame, int size)
{
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

	sal.sal_bcast = 1;

	iov.iov_base = frame;
	iov.iov_len = size;

	report_if(interface, LOG_DEBUG, "q931_send_uframe\n");

	if (sendmsg(interface->nt_socket, &msg, MSG_OOB) < 0) {
		report_if(interface, LOG_ERR,
			"sendmsg error: %s\n",strerror(errno));
		return errno;
	}

	return 0;
}

/*
static int q931_send_uframe(const struct q931_dlc *dlc, void *frame, int size)
{
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
*/

int q931_send_connect_acknowledge(struct q931_call *call)
{
	int size = 0;
	__u8 frame[260]; // FIXME

	size += q931_prepare_header(call, frame, Q931_MT_CONNECT_ACKNOWLEDGE);

	/* IEs:
	 *
	 * Information Element		Dir.	Type
	 * Channel Identification	n->u	O
	 * Display			n->u	O
	 *
	 */

	assert(call->selected_dlc);

	return q931_send_frame(call->selected_dlc, frame, size);
}

int q931_send_disconnect(struct q931_call *call)
{
	int size = 0;
	__u8 frame[260]; // FIXME

	size += q931_prepare_header(call, frame, Q931_MT_DISCONNECT);

	/* IEs:
	 *
	 * Information Element		Dir.	Type
	 * Cause			both	M
	 * Facility			both	O
	 * Progress Indicator		both	O
	 * Display			n->u	O
	 * User-User			both	O
	 *
	 */

	size += q931_append_ie_cause(frame + size,
		Q931_IE_C_L_USER, // FIXME
		Q931_IE_C_CV_NORMAL_CALL_CLEARING);

	assert(call->selected_dlc);

	return q931_send_frame(call->selected_dlc, frame, size);
}

int q931_send_release(
	struct q931_call *call,
	const struct q931_dlc *dlc)
{
	int size = 0;
	__u8 frame[260]; // FIXME

	size += q931_prepare_header(call, frame, Q931_MT_RELEASE);

	/* IEs:
	 *
	 * Information Element		Dir.	Type
	 * Cause			both	O
	 * Facility			both	O
	 * Display			n->u	O
	 * User-User			both	O
	 *
	 */

	return q931_send_frame(dlc, frame, size);
}

q931_send_release_cause(
	struct q931_call *call,
	const struct q931_dlc *dlc,
	enum q931_ie_cause_value cause_value)
{
	int size = 0;
	__u8 frame[260]; // FIXME

	size += q931_prepare_header(call, frame, Q931_MT_RELEASE);

	/* IEs:
	 *
	 * Information Element		Dir.	Type
	 * Cause			both	O
	 * Facility			both	O
	 * Display			n->u	O
	 * User-User			both	O
	 *
	 */

	size += q931_append_ie_cause(frame + size,
			call->interface->role == LAPD_ROLE_TE ?
				Q931_IE_C_L_USER :
				Q931_IE_C_L_PRIVATE_NET_SERVING_LOCAL_USER, // FIXME
			cause_value);

	return q931_send_frame(dlc, frame, size);
}

int q931_send_release_complete(
	struct q931_call *call)
{
	int size = 0;
	__u8 frame[260]; // FIXME

	size += q931_prepare_header(call, frame, Q931_MT_RELEASE_COMPLETE);

	/* IEs:
	 *
	 * Information Element		Dir.	Type
	 * Cause			both	O
	 * Facility			both	O
	 * Display			n->u	O
	 * User-User			u->n	O
	 *
	 */

	return q931_send_frame(call->selected_dlc, frame, size);
}

int q931_send_setup(struct q931_call *call, enum q931_setup_mode setup_mode)
{
	int size = 0;
	__u8 frame[260]; // FIXME

	assert(call->called_number);

	size += q931_prepare_header(call, frame, Q931_MT_SETUP);

	/* IEs:
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
	 * Calling Party Number	both	O
	 * Calling Party Subaddress	both	O
	 * Called Party Number		both	O
	 * Called Party Subaddress	both	O
	 * Transit Network Selection	u->n	O
	 * Low Layer Compatibility	both	O
	 * High Layer Compatibility	both	O
	 * User-User			both	O
	 *
	 */

	size += q931_append_ie_bearer_capability_alaw(frame + size);
// size += q931_append_ie_channel_identification_any(frame + size);

	if (!call->selected_dlc && call->interface->role == LAPD_ROLE_NT) {
		size += q931_append_ie_channel_identification(
			frame + size, Q931_IE_CI_ICS_BRI_B1);
	}

	if (strlen(call->calling_number))
		size += q931_append_ie_calling_party_number(frame + size, call->calling_number);

	size += q931_append_ie_called_party_number(frame + size, call->called_number);
	size += q931_append_ie_sending_complete(frame + size);

	if (setup_mode == Q931_SETUP_POINT_TO_POINT)
		return q931_send_frame(call->selected_dlc, frame, size);
	else if (setup_mode == Q931_SETUP_BROADCAST)
		return q931_send_bc_uframe(call->interface, frame, size);
	else
		assert(0);
}

int q931_send_setup_acknowledge(struct q931_call *call)
{
	int size = 0;
	__u8 frame[260]; // FIXME

	size += q931_prepare_header(call, frame, Q931_MT_SETUP_ACKNOWLEDGE);

	/* IEs:
	 *
	 * Information Element		Dir.	Type
	 * Channel Identification	both	O
	 * Progress Indicator		both	O
	 * Display			n->u	O
	 *
	 */

	size += q931_append_ie_channel_identification(
		frame + size, Q931_IE_CI_ICS_BRI_B1);

	assert(call->selected_dlc);

	return q931_send_frame(call->selected_dlc, frame, size);
}

int q931_send_status(struct q931_call *call)
{
	int size = 0;
	__u8 frame[260]; // FIXME

	size += q931_prepare_header(call, frame, Q931_MT_STATUS);

	/* IEs:
	 *
	 * Information Element		Dir.	Type
	 * Cause			both	M
	 * Call State			both	M
	 * Display			n->u	O
	 *
	 */

	size += q931_append_ie_cause(frame + size,
			Q931_IE_C_L_USER,
			Q931_IE_C_CV_NORMAL_UNSPECIFIED); // FIXME

	size += q931_append_ie_call_state(frame + size,
		q931_state_to_ie_state(call->user_state));

	assert(call->selected_dlc);

	return q931_send_frame(call->selected_dlc, frame, size);
}

int q931_send_alerting(struct q931_call *call)
{
	int size = 0;
	__u8 frame[260]; // FIXME

	size += q931_prepare_header(call, frame, Q931_MT_ALERTING);

	/* IEs:
	 *
	 * Information Element		Dir.	Type
	 * Channel Identification	both	O
	 * Facility			both	O
	 * Progress Indicator		both	O
	 * Display			n->u	O
	 * User-User			both	O
	 *
	 */

// size += q931_append_ie_channel_identification_any(frame + size);

	assert(call->selected_dlc);

	return q931_send_frame(call->selected_dlc, frame, size);
}

int q931_send_connect(struct q931_call *call)
{
	int size = 0;
	__u8 frame[260]; // FIXME

	size += q931_prepare_header(call, frame, Q931_MT_CONNECT);

	/* IEs:
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

// size += q931_append_ie_channel_identification_any(frame + size);

	assert(call->selected_dlc);

	return q931_send_frame(call->selected_dlc, frame, size);
}

int q931_send_call_proceeding(struct q931_call *call)
{
	int size = 0;
	__u8 frame[260];

	size += q931_prepare_header(call, frame, Q931_MT_CALL_PROCEEDING);

	/* IEs:
	 *
	 * Information Element		Dir.	Type
	 * Channel Identification	both	O
	 * Progress Indicator		both	O
	 * Display			n->u	O
	 *
	 */

// size += q931_append_ie_channel_identification_any(frame + size);

	assert(call->selected_dlc);

	return q931_send_frame(call->selected_dlc, frame, size);
}
