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

void q931_default_report(int level, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
}

inline static void q931_add_call(
	struct q931_interface *interface,
	struct q931_call *call)
{
	list_add_tail(&call->calls_node, &interface->calls);
	interface->ncalls++;
}

inline static void q931_del_call(
	struct q931_call *call)
{
	q931_del_call(call);
	
	call->interface->ncalls--;

}

static q931_callref q931_alloc_call_reference(struct q931_interface *interface)
{
	assert(interface);

	q931_callref call_reference;
	q931_callref first_call_reference;

	first_call_reference = interface->next_call_reference;

try_again:

	call_reference = interface->next_call_reference;

	interface->next_call_reference++;

	if (interface->next_call_reference >=
	    (1 << ((interface->call_reference_size * 8) - 1)))
	  interface->next_call_reference = 1;

	struct q931_call *call;
	list_for_each_entry(call, &interface->calls, calls_node)
	 {
	  if (call->direction == Q931_CALL_DIRECTION_OUTBOUND &&
	      call->call_reference == call_reference)
	   {
	    if (call_reference == interface->next_call_reference)
	      return -1;
	    else
	      goto try_again;
	   }
	 }

	return call_reference;
}

static struct q931_call *q931_find_call_by_reference(
	struct q931_interface *interface,
	enum q931_call_direction direction,
	q931_callref call_reference)
{
	struct q931_call *call;
	list_for_each_entry(call, &interface->calls, calls_node)
	 {
report_intf(LOG_DEBUG, "Searching ==========> %d=%d %lu=%lu\n",call->direction,direction,call->call_reference,call_reference);

	  if (call->direction == direction &&
	      call->call_reference == call_reference)
	   {
	    return call;
	   }
	 }

	return NULL;
}

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

	struct q931_header *hdr = (struct q931_header *)(frame + size);
	size += sizeof(struct q931_header);

	memset(hdr, 0x00, sizeof(*hdr));

	hdr->protocol_discriminator = Q931_PROTOCOL_DISCRIMINATOR_Q931;

	assert(call->interface);
	assert(call->interface->call_reference_size >=1 &&
	       call->interface->call_reference_size <= 4);

	hdr->call_reference_size =
		call->interface->call_reference_size;

	// Call reference
	assert(call->call_reference >= 0 &&
	       call->call_reference < (1 << ((hdr->call_reference_size * 8) - 1)));

	q931_make_callref(frame + size,
		hdr->call_reference_size,
		call->call_reference,
		call->direction == Q931_CALL_DIRECTION_INBOUND ?
		Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE :
		Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE);

	size += hdr->call_reference_size;
	
	__u8 *message_type_onwire = (__u8 *)(frame + size);
	size++;
	*message_type_onwire = message_type;

	return size;
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
static int q931_send_connect_acknowledge(struct q931_call *call)
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

static int q931_send_disconnect(struct q931_call *call)
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

static int q931_send_release(
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

static int q931_send_release_cause(
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

static int q931_send_release_complete(
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

static int q931_send_setup(struct q931_call *call, enum q931_setup_mode setup_mode)
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

static int q931_send_setup_acknowledge(struct q931_call *call)
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

static int q931_send_status(struct q931_call *call)
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

static int q931_send_alerting(struct q931_call *call)
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

static int q931_send_connect(struct q931_call *call)
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

static int q931_send_call_proceeding(struct q931_call *call)
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

struct q931_libstate *q931_init()
{
	struct q931_libstate *libstate;

	libstate = malloc(sizeof(*libstate));
	if (!libstate) {
		//FIXME
		exit(1);
	}

	libstate->report = q931_default_report;

	// Non-reentrant, FIXME
	q931_ie_infos_init();
	q931_message_types_init();

	return libstate;
}

void q931_leave(struct q931_libstate *libstate)
{
	free(libstate);
}

struct q931_interface *q931_open_interface(
	struct q931_libstate *libstate,
	const char *name)
{
	struct q931_interface *interface;

	assert(name);

	interface = malloc(sizeof(*interface));
	if (!interface)
		abort();

	memset(interface, 0x00, sizeof(*interface));

	INIT_LIST_HEAD(&interface->calls);

	interface->libstate = libstate;
	interface->name = strdup(name);
	interface->next_call_reference = 1;
	interface->call_reference_size = 1; // FIXME should be 1 for BRI, 2 for PRI

	int s = socket(PF_LAPD, SOCK_SEQPACKET, 0);
	if (socket < 0)
		goto err_socket;

	if (setsockopt(s, SOL_LAPD, SO_BINDTODEVICE,
			name, strlen(name)+1) < 0)
		goto err_setsockopt;

	int optlen=sizeof(interface->role);
	if (getsockopt(s, SOL_LAPD, LAPD_ROLE,
		&interface->role, &optlen)<0)
		goto err_getsockopt;

	if (interface->role == LAPD_ROLE_TE) {
		interface->nt_socket = -1;

		q931_init_dlc(&interface->te_dlc,
			interface, s);
			
		interface->te_dlc.status = DLC_DISCONNECTED;
	} else {
		q931_init_dlc(&interface->te_dlc,
			NULL, -1);

		interface->nt_socket = s;
	}

	return interface;

err_getsockopt:
err_setsockopt:
	close(s);
err_socket:
	free(interface);

	return NULL;
}

void q931_close_interface(struct q931_interface *interface)
{
	assert(interface);

	if (interface->role == LAPD_ROLE_TE)
	 {
	  shutdown(interface->te_dlc.socket, 0);
	  close(interface->te_dlc.socket);
	 }
	else
	 {
// FIXME: for each dlc, shutdown and close?

	  close(interface->nt_socket);
	 }

	if (interface->name)
		free(interface->name);

	free(interface);
}

struct q931_call *q931_alloc_call(struct q931_interface *interface)
{
	struct q931_call *call;

	call = malloc(sizeof(*call));
	if (!call) abort();
	memset(call, 0x00, sizeof(*call));

	strcpy(call->calling_number, "");
	strcpy(call->called_number, "");

	call->interface = interface;

	alerting_indication = interface->alerting_indication;
	disconnect_indication = interface->disconnect_indication;
	error_indication = interface->error_indication;
	info_indication = interface->info_indication;
	more_info_indication = interface->more_info_indication;
	notify_indication = interface->notify_indication;
	proceeding_indication = interface->proceeding_indication;
	progress_indication = interface->progress_indication;
	reject_indication = interface->reject_indication;
	release_confirm = interface->release_confirm;
	release_indication = interface->release_indication;
	resume_confirm = interface->resume_confirm;
	resume_indication = interface->resume_indication;
	setup_complete_indication = interface->setup_complete_indication;
	setup_confirm = interface->setup_confirm;
	setup_indication = interface->setup_indication;
	status_indication = interface->status_indication;
	suspend_confirm = interface->suspend_confirm;
	suspend_indication = interface->suspend_indication;
	timeout_indication = interface->timeout_indication;

	return call;
}

void q931_free_call(struct q931_call *call)
{
	assert(call);
	assert(call->calls_node.next == LIST_POISON1);
	assert(call->calls_node.prev == LIST_POISON2);

	free(call);
}

void q931_call_connect(struct q931_call *call)
{
	q931_send_connect(call);
}

void q931_call_disconnect(struct q931_call *call)
{
	assert(call);

	if (!call->selected_dlc) {
		int i;
		for (i=0; i<call->nces; i++) {
			q931_send_release_cause(call, call->ces[i],
				Q931_IE_C_CV_NON_SELECTED_USER_CLEARING);
		}
	} else if (call->interface->role == LAPD_ROLE_TE) {
		if (call->user_state != U0_NULL_STATE &&
		    call->user_state != U11_DISCONNECT_REQUEST &&
		    call->user_state != U12_DISCONNECT_INDICATION)
			q931_send_disconnect(call);

		call->user_state = U11_DISCONNECT_REQUEST;
	} else {
		if (call->net_state != N0_NULL_STATE &&
		    call->net_state != N11_DISCONNECT_REQUEST &&
		    call->net_state != N12_DISCONNECT_INDICATION)
			q931_send_disconnect(call);

		call->net_state = N12_DISCONNECT_INDICATION;
	}

	// Start T305
	// Disconnect B channel
}

void q931_call_alerting(struct q931_call *call)
{
	q931_send_alerting(call);
}

void q931_call_proceeding(struct q931_call *call)
{
	q931_send_call_proceeding(call);
}

void q931_alerting_request(struct q931_call *call)
{
	switch (call->state) {
	case N2_OVERLAP_SENDING:
		// Stop T302

		q931_send_alerting(call);

		call->state = N4_CALL_DELIVERED;
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
		q931_send_alerting(call);

		call->state = N4_CALL_DELIVERED;
	break;

	case N4_CALL_DELIVERED:
	break;

	case N6_CALL_PRESENT:
	break;

	case N7_CALL_RECEIVED:
	break;

	case N8_CONNECT_REQUEST:
	break;

	case N9_INCOMING_CALL_PROCEEDING:
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_disconnect_request(struct q931_call *call)
{
	switch (call->state) {
	case N2_OVERLAP_SENDING:
		// Stop T302

		if (call->tones_option) {
			q931_send_disconnect(call); // PI??

			// Start tone

			// Start T306
		} else {
			// Disconnect B channel if connected

			q931_send_disconnect(call);

			// Start T305
		}

		call->state = N12_DISCONNECT_INDICATION;
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
		if (call->tones_option) {
			q931_send_disconnect(call); // PI??

			// Start tone

			// Start T306
		} else {
			// Disconnect B channel if connected

			q931_send_disconnect(call);

			// Start T305
		}

		call->state = N12_DISCONNECT_INDICATION;
	break;

	case N6_CALL_PRESENT:
		// Stop T303

		if (call->broadcasted_setup) {
			// Release B channel if not released

			if (call->release_indication)
				call->release_indication(call);

			call->state = N22_CALL_ABORT;
		} else {
			if (call->tones_option) {
				q931_send_disconnect(call); // PI??

				// Start tone

				// Start T306
			} else {
				// Disconnect B channel if connected

				q931_send_disconnect(call);

				// Start T305
			}

			call->state = N12_DISCONNECT_INDICATION;
		}
	break;

	case N7_CALL_RECEIVED:
		if (call->broadcasted_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces);
			}

			if (call->release_indication)
				call->release_indication(call);

			// Stop T301 if running

			// Release B channel

			// T312 Running?
			if (1)
				call->state = N22_CALL_ABORT;
			else
				call->state = N0_NULL_STATE;
		} else {
			q931_send_disconnect(call);

			// Stop T301 if running

			// Start T305

			call->state = N12_DISCONNECT_INDICATION;
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcasted_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces);
			}

			// Release B channel

			if (call->release_indication)
				call->release_indication(call);

			// T312 Running?
			if (1)
				call->state = N22_CALL_ABORT;
			else
				call->state = N0_NULL_STATE;
		} else {
			q931_send_disconnect(call);

			// Start T308

			call->state = N12_DISCONNECT_INDICATION;
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		// Stop T310 if running

		if (call->broadcasted_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces);
			}

			// Release B channel

			if (call->release_indication)
				call->release_indication(call);

			// T312 Running?
			if (1)
				call->state = N22_CALL_ABORT;
			else
				call->state = N0_NULL_STATE;
		} else {
			q931_send_disconnect(call);

			// Release B channel

			// Start T305

			call->state = N12_DISCONNECT_INDICATION;
		}
	break;

	case N10_ACTIVE:
		// Release B channel

		q931_send_disconnect(call);

		// Start T305

		call->state = N12_DISCONNECT_INDICATION;
	break;

	case N15_SUSPEND_REQUEST:
		if (call->tone_option) {
			q931_send_disconnect(call); // PI ?!?

			// Start Tone

			// Start T306
		} else {
			// Disconnect B channel

			q931_send_disconnect(call);

			// Start T305
		}

		call->state = N12_DISCONNECT_INDICATION;
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_info_request(struct q931_call *call)
{
	switch (call->state) {
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
		q931_send_info(call); // ???!?
	break;

	case N6_CALL_PRESENT:
		// Save event until compialtion of a transition???
	break;

	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcasted_setup) {
			// q931_ces_info_request(ces); TO ALL RESPONDING TERMS (!?!)
		} else {
			q931_send_info(call);
		}
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_more_info_request(struct q931_call *call)
{
	switch (call->state) {
	case N1_CALL_INITIATED:
		// Start T302

		q931_send_setup_ack(call); // With B channel?

		// Connect B channel

		if (strlen(call->called_number)) {
		} else {
			// Connect dialtone to B channel (optional)
		}

		call->state = N2_OVERLAP_SENDING;
	break;

	case N2_OVERLAP_SENDING:
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	break;

	case N4_CALL_DELIVERED:
	break;

	case N6_CALL_PRESENT:
	break;

	case N7_CALL_RECEIVED:
	break;

	case N8_CONNECT_REQUEST:
	break;

	case N9_INCOMING_CALL_PROCEEDING:
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
	
}

void q931_notify_request(struct q931_call *call)
{
	switch (call->state) {
	case N10_ACTIVE:
	case N15_SUSPEND_REQUEST:
		q931_send_notify(call);
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_proceeding_request(struct q931_call *call)
{
	switch (call->state) {
	case N1_CALL_INITIATED:
		q931_send_proceeding(call); // with B channel indication

		// Connect B channel

		call->state = N3_OUTGOING_CALL_PROCEEDING;
	break;

	case N2_OVERLAP_SENDING:
		// Stop T302

		q931_send_proceeding(call);

		call->state = N3_OUTGOING_CALL_PROCEEDING;
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	break;

	case N4_CALL_DELIVERED:
	break;

	case N6_CALL_PRESENT:
	break;

	case N7_CALL_RECEIVED:
	break;

	case N8_CONNECT_REQUEST:
	break;

	case N9_INCOMING_CALL_PROCEEDING:
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_progress_request(struct q931_call *call)
{
	switch (call->state) {
	case N2_OVERLAP_SENDING:
		q931_send_progress(call);
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
		q931_send_progress(call);
	break;

	case N4_CALL_DELIVERED:
		q931_send_progress(call);
	break;

	case N6_CALL_PRESENT:
	break;

	case N7_CALL_RECEIVED:
	break;

	case N8_CONNECT_REQUEST:
	break;

	case N9_INCOMING_CALL_PROCEEDING:
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_reject_request(struct q931_call *call)
{
	if (call->state == N1_CALL_INITIATED) {
		q931_send_release_complete(call);

		// Release B channel

		q931_del_call(call);

		call->state = N0_NULL;
	}
}

void q931_release_request(struct q931_call *call)
{
	switch (call->state) {
	case N2_OVERLAP_SENDING:
		// Stop T302

		q931_send_release(call);

		// Release B channel if not released

		// Start T308

		call->state = N19_RELEASE_REQUEST;
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
		q931_send_release(call);

		// Release B channel if not released

		// Start T308

		call->state = N19_RELEASE_REQUEST;
	break;

	case N6_CALL_PRESENT:
		// Stop T303

		if (call->broadcasted_setup) {
			// Release B channel if not released

			if (call->release_indication)
				call->release_indication(call);

			call->state = N22_CALL_ABORT;
		} else {
			q931_send_release(call);

			// Start T308

			call->state = N19_RELEASE_REQUEST;
		}
	break;

	case N7_CALL_RECEIVED:
		if (call->broadcasted_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces);
			}

			if (call->release_indication)
				call->release_indication(call);

			// Stop T301 if running

			// Release B channel

			// T312 Running?
			if (1)
				call->state = N22_CALL_ABORT;
			else
				call->state = N0_NULL_STATE;
		} else {
			q931_send_release(call);

			// Stop T301 if running

			// Start T308

			call->state = N19_RELEASE_REQUEST;
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcasted_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces);
			}

			// Release B channel

			if (call->release_indication)
				call->release_indication(call);

			// T312 Running?
			if (1)
				call->state = N22_CALL_ABORT;
			else
				call->state = N0_NULL_STATE;
		} else {
			q931_send_release(call);

			// Start T308

			call->state = N19_RELEASE_REQUEST;
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		// Stop T310 if running

		if (call->broadcasted_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces);
			}

			// Release B channel

			if (call->release_indication)
				call->release_indication(call);

			// T312 Running?
			if (1)
				call->state = N22_CALL_ABORT;
			else
				call->state = N0_NULL_STATE;
		} else {
			q931_send_release(call);

			// Start T308

			call->state = N19_RELEASE_REQUEST;
		}
	break;

	case N10_ACTIVE:
		q931_send_release(call);

		// Release B channel

		// Start T308

		call->state = N19_RELEASE_REQUEST;
	break;

	case N11_DISCONNECT_REQUEST:
		q931_send_release(call);

		// Start T308

		call->state = N19_RELEASE_REQUEST;
	break;

	case N15_SUSPEND_REQUEST:
		q931_send_release(call);

		// Release B channel if not released

		// Start T308

		call->state = N19_RELEASE_REQUEST;
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N12_DISCONNECT_INDICATION:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_resume_reject_request(struct q931_call *call)
{
	switch (call->state) {
	case N17_RESUME_REQUEST:
		q931_send_resume_reject(call);

		q931_del_call(call);

		call->state = N0_NULL_STATE;
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_resume_response(struct q931_call *call)
{
	switch (call->state) {
	case N17_RESUME_REQUEST:
		if (call->resume_repsonse)
			call->resume_response(call);

		q931_send_resume_acknowledge(call);

		call->state = N10_ACTIVE;
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case N15_SUSPEND_REQUEST:
	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_setup_complete_request(struct q931_call *call)
{
	switch (call->state) {
	case N8_CONNECT_REQUEST:
		// Connect B channel

		// Select CES

		q931_send_connect_acknowledge(call);

		// Kill selected CES
		// q931_ces_release_request(ces) OTHER CES

		call->state = N10_ACTIVE;
	break;

	case N9_INCOMING_CALL_PROCEEDING:
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_setup_request(struct q931_call *call)
{
	switch (call->state) {
	case N0_NULL_STATE:
		// Select B channel
		// Channel selection success?
		if (1) {
			call->direction = Q931_CALL_DIRECTION_OUTBOUND;
			call->call_reference = q931_alloc_call_reference(interface);

			if (call->call_reference < 0) {
				interface->libstate->report(LOG_ERR,
					"All call references are used!!!\n");
				return;
			}

			interface->libstate->report(LOG_INFO,
				"Call reference allocated (%ld)\n",
				call->call_reference);

			q931_add_call(interface, call);

			// Start T303

			// Is multipoint?
			if (1) {
				// Start T312

				q931_send_setup(call, Q931_SETUP_BROADCAST);
			} else {
				q931_send_setup(call, Q931_SETUP_POINT_TO_POINT);
			}

			call->state = N6_CALL_PRESENT;
		} else {
			if (call->release_indication)
				call->release_indication(call);
		}
	break;

	case N1_CALL_INITIATED:
	break;

	case N2_OVERLAP_SENDING:
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	break;

	case N4_CALL_DELIVERED:
	break;

	case N6_CALL_PRESENT:
	break;

	case N7_CALL_RECEIVED:
	break;

	case N8_CONNECT_REQUEST:
	break;

	case N9_INCOMING_CALL_PROCEEDING:
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_setup_response(struct q931_call *call)
{
	switch (call->state) {
	case N2_OVERLAP_SENDING:
		// Stop T302
		q931_send_connect(call);

		call->state = N10_ACTIVE;
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
		q931_send_connect(call);

		call->state = N10_ACTIVE;
	break;

	case N4_CALL_DELIVERED:
		q931_send_connect(call);

		call->state = N10_ACTIVE;
	break;

	case N6_CALL_PRESENT:
	break;

	case N7_CALL_RECEIVED:
	break;

	case N8_CONNECT_REQUEST:
	break;

	case N9_INCOMING_CALL_PROCEEDING:
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_status_enquiry_request(struct q931_call *call)
{
}

void q931_suspend_reject_request(struct q931_call *call)
{
	switch (call->state) {
	case N15_SUSPEND_REQUEST:
		q931_send_suspend_reject(call);

		call->state = N10_ACTIVE;
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_suspend_response(struct q931_call *call)
{
	switch (call->state) {
	case N15_SUSPEND_REQUEST:
		q931_send_suspend_acknowledge(call);

		// NOTE: Timer T307 is running in the call control block

		q931_del_call(call);

		call->state = N0_NULL_STATE;
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_suspend_request(struct q931_call *call)
{
}

void q931_int_alerting_indication(struct q931_call *call, struct q931_ces *ces)
{
	switch (call->state) {
	case N7_CALL_RECEIVED:
		// Do nothing
	break;

	case N8_CONNECT_REQUEST:
		// Do nothing
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		// Stop T310 if running

		if (call->alerting_indication)
			call->alerting_indication(call);

		// Start T301

		call->state = N7_CALL_RECEIVED;

		// NOTE 1
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_int_connect_indication(struct q931_call *call, struct q931_ces *ces)
{
	switch (call->state) {
	case N7_CALL_RECEIVED:
		call->selected_ces = ces;

		if (call->connect_indication)
			call->connect_indication(call);

		// Stop T301 if running

		call->state = N8_CONNECT_REQUEST;
	break;

	case N8_CONNECT_REQUEST:
		// Do nothing
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		// Stop T310 if running

		call->selected_ces = ces;

		if (call->connect_indication)
			call->connect_indication(call);

		call->state = N8_CONNECT_REQUEST;
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}

}

void q931_int_call_proceeding_indication(struct q931_call *call, struct q931_ces *ces)
{
	switch (call->state) {
	case N7_CALL_RECEIVED:
		// Do nothing
	break;

	case N8_CONNECT_REQUEST:
		// Do nothing
	break;

	case N9_INCOMING_CALL_PROCEEDING:
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_int_release_complete_indication(struct q931_call *call, struct q931_ces *ces)
{
	switch (call->state) {
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N10_ACTIVE:
		list_del(ces);

		q931_free_ces(ces);
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_int_info_indication(struct q931_call *call, struct q931_ces *ces)
{
	switch (call->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		if (call->info_indication)
			call->info_indication(call);
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_int_progress_indication(struct q931_call *call, struct q931_ces *ces)
{
	switch (call->state) {
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
		if (call->progress_indication)
			call->progress_indication(call);
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		// Stop T310 if running

		if (call->progress_indication)
			call->progress_indication(call);
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_int_release_indication(
	struct q931_call *call,
	struct q931_ces *ces)
{
	switch (call->state) {
	case N7_CALL_RECEIVED:
		// Are other CES able to proceed?
		if (1) {
			// Save cause
		} else {
			// Is T312 running (THIS MAY BE WRONG)
			if (1) {
				// Save cause
			} else {
				if (call->release_indication)
					call->release_indication(call); //With cause

				// Stop timer T301 if running

				// Release B channel

				q931_del_call(call);

				call->state = N0_NULL_STATE;
			}
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->selected_ces) {
			if (call->release_indication)
				call->release_indication(call);

			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces);
			}

			// Release B-channel

			// T312 Running
			if (1)
				call->state = N22_CALL_ABORT;
			else
				call->state = N0_NULL_STATE;
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		// Other CESs able to proceed? (!??!)
		if (1) {
			// Save cause
		} else {
			// Is T312 running (THIS MAY BE WRONG)
			if (1) {
				// Save cause
			} else {
				if (call->release_indication)
					call->release_indication(call); //With cause

				// Stop timer T310 if running

				// Release B channel

				q931_del_call(call);

				call->state = N0_NULL_STATE;
			}
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_timer_T301()
{
	if (call->timeout_indication)
		call->timeout_indication(call);
}

void q931_timer_T302()
{
	switch (call->state) {
	case N2_OVERLAP_SENDING:
		// Is call info definitely incomplete? (???)
		if (1) {
			// Tones?
			if (1) {
				q931_send_disconnect(call); // PI (???)

				// Start tone
				// Start T306
			} else {
				// Disconnect B channel
				q931_send_disconnect(call);

				// Start T305
			}

			call->state = N12_DISCONNECT_INDICATION;
		} else {
			if (call->timeout_indication)
				call->timeout_indication(call);

			q931_send_call_proceeding(call);

			call->state = N3_OUTGOING_CALL_PROCEEDING;
		}
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	break;

	case N4_CALL_DELIVERED:
	break;

	case N6_CALL_PRESENT:
	break;

	case N7_CALL_RECEIVED:
	break;

	case N8_CONNECT_REQUEST:
	break;

	case N9_INCOMING_CALL_PROCEEDING:
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_timer_T303()
{
	switch (call->state) {
	case N6_CALL_PRESENT:
		// First timeout?
		if (1) {
			if (call->broadcasted_setup) {
				 // REL COMP RCVD?
				if (1) {
					// Release B-channel if necessary
					if (call->release_indication)
						call->release_indication(call);

					call->state = N22_CALL_ABORT;
				} else {
					q931_send_setup(call, Q931_SETUP_BROADCAST);
					// Start T303

					// Restart T312
				}
			} else {
				q931_send_setup(call, Q931_SETUP_POINT_TO_POINT);
				// Start T303
			}
		} else {
			if (call->broadcasted_setup) {
				// Release B-channel if necessary
				if (call->release_indication)
					call->release_indication(call);

				call->state = N22_CALL_ABORT;
			} else {
				if (call->disconnect_indication)
					call->disconnect_indication(call);

				// Start T306

				call->state = N12_DISCONNECT_INDICATION;
			}
		}
	break;

	case N7_CALL_RECEIVED:
	break;

	case N8_CONNECT_REQUEST:
	break;

	case N9_INCOMING_CALL_PROCEEDING:
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_timer_T305()
{
	switch (call->state) {
	case N12_DISCONNECT_INDICATION:
		q931_send_release(call);
		// NOTE 1: Cause sent is same as in original disconnect message

		// Start T308

		call->state = N19_RELEASE_REQUEST;
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_timer_T306()
{
	switch (call->state) {
	case N12_DISCONNECT_INDICATION:
		// Stop tones/announcements if applicable

		q931_send_release(call);

		// NOTE 1: Cause sent is same as in original disconnect message

		// Start T308

		call->state = N19_RELEASE_REQUEST;
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_timer_T310()
{
	switch (call->state) {
	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcasted_setup) {
			struct q931_ces *ces;
			list_for_each_entry(ces, &call->ces, node) {
				q931_ces_release_request(ces);
			}

			// Release B channel

			if (call->release_indication)
				call->release_indication(call);

			// T312 Running?
			if (1)
				call->state = N22_CALL_ABORT;
			else
				call->state = N0_NULL_STATE;
		} else {
			q931_send_disconnect(call);

			// Release B channel

			// Start T305

			call->state = N12_DISCONNECT_INDICATION;
		}
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

void q931_timer_T312()
{
	switch (call->state) {
	case N7_CALL_RECEIVED:
		// Any CES able to proceed?
		if (1) {
		} else {
			if (call->release_indication)
				call->release_indication(call); // With cause

			// Stop T301 if running

			// Release B channel

			q931_del_call(call);

			call->state = N0_NULL_STATE;
		}
	break;

	case N8_CONNECT_REQUEST:
	break;

	case N9_INCOMING_CALL_PROCEEDING:
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}


inline static void q931_handle_alerting(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N6_CALL_PRESENT:
		if (call->broadcasted_setup) {
			// Channel OK?
			if (1) {
				// Stop T303

				if (call->alerting_indication)
					call->alerting_indication(call);

				struct q931_ces *ces;
				ces = q931_ces_alloc(call);
				q931_ces_alerting_request(ces);

				// Start T301

				call->state = N7_CALL_RECEIVED;
			} else {
				struct q931_ces *ces;
				ces = q931_ces_alloc(call);

				q931_ces_release_request(ces);
			}
		} else {
			// Stop T303

			// Channel OK?
			if (1) {
				if (call->alerting_indication)
					call->alerting_indication(call);

				// Start T301

				call->state = N7_CALL_RECEIVED;
			} else {
				q931_send_release(call);

				if (call->release_indication)
					call->release_indication(call);

				// Start T308

				call->state = N19_RELEASE_REQUEST;
			}
		}
	break;

	case N7_CALL_RECEIVED:
		if (call->broadcasted_setup) {
			// Channel OK?
			if (1) {
				struct q931_ces *ces;
				ces = q931_ces_alloc(call);

				q931_ces_alerting_request(ces);
			} else {
				struct q931_ces *ces;
				ces = q931_ces_alloc(call);

				q931_ces_release_request(ces);
			}
		} else {
			q931_send_status(call);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcasted_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call);

			q931_ces_release_request(ces);
		} else {
			q931_send_status(call);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcasted_setup) {
			// Channel OK?
			if (1) {
				// Stop T310 if running

				if (call->alerting_indication)
					call->alerting_indication(call);

				struct q931_ces *ces;
				ces = q931_ces_alloc(call);

				q931_ces_alerting_request(ces);

				// Start T301

				call->state = N7_CALL_RECEIVED;
			} else {
				struct q931_ces *ces;
				ces = q931_ces_alloc(call);

				q931_ces_release_request(ces);
			}
		} else {
			// Stop T310 if running

			if (call->alerting_indication)
				call->alerting_indication(call);

			// Start T301

			call->state = N7_CALL_RECEIVED;
		}
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_handle_call_proceeding(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N6_CALL_PRESENT:
		if (call->broadcasted_setup) {
			// Channel OK?
			if (1) {
				// Stop T303

				if (call->proceeding_indication)
					call->proceeding_indication(call);

				// Create CES
				// q931_ces_call_proceeding_request(ces);

				// Start T310
			} else {
				// Create CES
				// q931_ces_release_request(ces);
			}
		} else {
			// Stop T303

			// Channel OK?
			if (1) {
				if (call->proceeding_indication)
					call->proceeding_indication(call);

				// Start T310

				call->state = N9_INCOMING_CALL_PROCEEDING;
			} else {
				q931_send_release(call);

				if (call->release_indication)
					call->release_indication(call);

				// Start T308

				call->state = N19_RELEASE_REQUEST;
			}
		}
	break;

	case N7_CALL_RECEIVED:
	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcasted_setup) {
			// Channel OK?
			if (1) {
				struct q931_ces *ces;
				ces = q931_ces_alloc(call);

				q931_ces_call_proceeding_request(ces);
			} else {
				struct q931_ces *ces;
				ces = q931_ces_alloc(call);

				q931_ces_release_request(ces);
			}
		} else {
			q931_send_status(call);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcasted_setup) {
			struct q931_ces *ces;
			ces = q931_ces_alloc(call);

			q931_ces_release_request(ces);
		} else {
			q931_send_status(call);
		}
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_handle_connect(
	const struct q931_dlc *dlc,
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N6_CALL_PRESENT:
		if (call->broadcasted_setup) {
			// Channel OK?
			if (1) {
				// Stop T303

				if (call->setup_confirm)
					call->setup_confirm(call);

				// Preselect CES

				// Create CES
				// q931_ces_conect_request(ces);

				// Start T301

				call->state = N8_CONNECT_REQUEST;
			} else {
				// Create CES
				// q931_ces_release_request(ces);
			}
		} else {
			// Stop T303

			// Channel OK?
			if (1) {
				if (call->setup_confirm)
					call->setup_confirm(call);

				call->state = N8_CONNECT_REQUEST;
			} else {
				q931_send_release(call);

				if (call->release_indication)
					call->release_indication(call);

				// Start T308

				call->state = N19_RELEASE_REQUEST;
			}
		}
	break;

	case N7_CALL_RECEIVED:
		if (call->broadcasted_setup) {
			// Channel OK?
			if (1) {
				if (call->connect_indication)
					call->connect_indication(call);

				// Stop T301 if running

				// Preselect CES

				// Create CES

				// q931_ces_connect_request(ces);

				call->state = N8_CONNECT_REQUEST;
			} else {
				// Create CES

				// q931_ces_release_request(ces);
			}
		} else {
			if (call->connect_indication)
				call->connect_indication(call);

			// Stop T301 if running

			call->state = N8_CONNECT_REQUEST;
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcasted_setup) {
			// Create CES

			// q931_ces_release_request(ces);
		} else {
			q931_send_status(call);
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcasted_setup) {
			// Channel OK?
			if (1) {
				// Stop T310 if running

				if (call->connect_indication)
					call->connect_indication(call);

				struct q931_ces *ces;
				ces = q931_ces_alloc(call);

				call->selected_ces = ces;
				q931_ces_connect_request(ces);

				call->state = N8_CONNECT_REQUEST;
			} else {
				struct q931_ces *ces;
				ces = q931_ces_alloc(call);

				q931_ces_release_request(ces);
			}
		} else {
			// Stop T310 if running

			if (call->connect_indication)
				call->connect_indication(call);

			call->state = N8_CONNECT_REQUEST;
		}
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}











	if (call->selected_dlc && call->selected_dlc != dlc) {
		call->interface->libstate->report(LOG_WARNING,
			"Received CONNECT on an already selected call\n");

		return;
	}

	call->selected_dlc = dlc;

	int i;
	for (i=0; i<call->nces; i++) {
		if (call->ces[i] == call->selected_dlc) {
			q931_send_connect_acknowledge(call);
		} else {
			q931_send_release_cause(call, call->ces[i],
				Q931_IE_C_CV_NON_SELECTED_USER_CLEARING);
		}
	}

	if (call->connect_callback)
		call->connect_callback(call);
}

inline static void q931_handle_connect_acknowledge(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N10_ACTIVE:
		// Do nothing
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_handle_progress(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N7_CALL_RECEIVED:
		if (call->broadcasted_setup) {
			q931_send_status(call);
		} else {
			if (call->progress_indication)
				call->progress_indication(call);
		}
	break;

	case N8_CONNECT_REQUEST:
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcasted_setup) {
			q931_send_status(call);
		} else {
			// Stop T310 if running

			if (call->progress_indication)
				call->progress_indication(call);
		}
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_handle_setup(
	const struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N0_NULL_STATE:
		if (0) { // Can't select a channel
			q931_send_release_complete(call);

			q931_del_call(call);

			return;
		}

		if (call->interface->role == LAPD_ROLE_TE)
			call->state = U1_CALL_INITIATED;
		else
			call->state = N1_CALL_INITIATED;

		{
		int i;

		for(i=0; i<ies_cnt; i++) {
			if (ies[i].info->id == Q931_IE_SENDING_COMPLETE) {
				call->sending_complete = TRUE;
			} else if (ies[i].info->id == Q931_IE_CALLED_PARTY_NUMBER) {
				if (ies[i].size < 2) {
					report_dlc(dlc, LOG_ERR, "IE size < 2\n");
					// Send status with cause code
					return;
				}

				if (ies[i].size - 1 > Q931_MAX_DIGITS) {
					report_dlc(dlc, LOG_ERR, "IE size > Q931_MAX_DIGITS + 1\n");
					// Send status with cause code
					return;
				}

				char *number = ies[i].data + 1;

				if (number[ies[i].size - 1] == '#') {
					call->sending_complete = TRUE;
					strncat(call->called_number, number,
						ies[i].size - 1);
				} else {
					strncat(call->called_number, number,
						ies[i].size - 2);
				}
			}
		}
		}

		if (call->interface->setup_indication)
			call->interface->setup_indication(call);
	break;

	case N1_CALL_INITIATED:
	break;

	case N2_OVERLAP_SENDING:
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	break;

	case N4_CALL_DELIVERED:
	break;

	case N6_CALL_PRESENT:
	break;

	case N7_CALL_RECEIVED:
	break;

	case N8_CONNECT_REQUEST:
	break;

	case N9_INCOMING_CALL_PROCEEDING:
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}


/*
	if (call->sending_complete) {
		// Start T302
		q931_send_call_proceeding(call);
		q931_send_alerting(call);
		q931_send_connect(call);
	} else {
		q931_send_setup_acknowledge(call);
	}
*/

	call->interface->libstate->report(LOG_INFO,
		"Number: %s\n", call->called_number);
}

inline static void q931_handle_setup_acknowledge(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N6_CALL_PRESENT:
		if (call->broadcasted_setup) {
			// Channel OK?
			if (1) {
				// Stop T303

				if (call->more_info_indication)
					call->more_info_indication(call);

				// Create CES

				// q931_ces_setup_ack_request(ces);

				// Start T304

				call->state = N25_OVERLAP_RECEIVING;
			} else {
				// Create CES
				// q931_ces_release_request(ces);
			}
		} else {
			// Stop T303

			// Channel OK?
			if (1) {
				if (call->more_info_indication)
					call->more_info_indication(call);

				// Start T304
				call->state = N25_OVERLAP_RECEIVING;
			} else {
				q931_send_release(call);

				if (call->release_indication)
					call->release_indication(call);

				// Start T308

				call->state = N19_RELEASE_REQUEST;
			}
		}
	break;

	case N7_CALL_RECEIVED:
	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcasted_setup) {
			// Channel OK?
			if (1) {
				// Create CES

				q931_ces_setup_ack_request(ces);
			} else {
				// Create CES
				q931_ces_release_request(ces);
			}
		} else {
			q931_send_status(call);
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcasted_setup) {
			// Create CES

			// q931_ces_release_request(ces);
		} else {
			q931_send_status(call);
		}
	break;

	case N10_ACTIVE:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

// T305
// See 5.3.3

inline static void q931_handle_disconnect(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N1_CALL_INITIATED:
		// Disconnect B channel if connected

		if (call->disconnect_indication)
			call->disconnect_indication(call);

		call->state = N11_DISCONNECT_REQUEST;
	break;

	case N2_OVERLAP_SENDING:
		// Stop T302

		// Disconnect B channel if connected

		if (call->disconnect_indication)
			call->disconnect_indication(call);

		call->state = N11_DISCONNECT_REQUEST;
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
		// Disconnect B channel if connected

		if (call->disconnect_indication)
			call->disconnect_indication(call);

		call->state = N11_DISCONNECT_REQUEST;
	break;

	case N4_CALL_DELIVERED:
		// Disconnect B channel if connected

		if (call->disconnect_indication)
			call->disconnect_indication(call);

		call->state = N11_DISCONNECT_REQUEST;
	break;

	case N7_CALL_RECEIVED:
		if (call->broadcasted_setup) {
			q931_send_status(call);

			//IN THE CASE OF A BROADCAST SETUP,
			//THE CALL STATE RETURNED IN THE STATUS
			//MESSAGE SHOULD BE STATE 6.
		} else {
			// Stop T301 if running

			// Disconnect B channel if connected

			if (call->disconnect_indication)
				call->disconnect_indication(call);

			call->state = N11_DISCONNECT_REQUEST;
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcasted_setup) {
			q931_send_status(call);

			//IN THE CASE OF A BROADCAST SETUP,
			//THE CALL STATE RETURNED IN THE STATUS
			//MESSAGE SHOULD BE STATE 6.
		} else {
			// Disconnect B channel if connected

			if (call->disconnect_indication)
				call->disconnect_indication(call);

			call->state = N11_DISCONNECT_REQUEST;
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcasted_setup) {
			q931_send_status(call);

			// NOTE 2
		} else {
			// Stop T310 if running

			// Disconnect B channel if connected

			if (call->disconnect_indication)
				call->disconnect_indication(call);

			call->state = N11_DISCONNECT_REQUEST;
		}
	break;

	case N10_ACTIVE:
		// Disconnect B channel if connected

		if (call->disconnect_indication)
			call->disconnect_indication(call);
		
		call->state = N11_DISCONNECT_REQUEST;
	break;

	case N12_DISCONNECT_INDICATION:
		// Stop T305
		// Stop T306

		// Stop tones/announcements (if applicable)

		q931_send_release(call);

		// Start T308

		call->state = N19_RELEASE_REQUEST;
	break;

	case N17_RESUME_REQUEST:
		// Disconnect B channel if connected

		if (call->disconnect_indication)
			call->disconnect_indication(call);

		call->state = N11_DISCONNECT_REQUEST;
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N6_CALL_PRESENT:
	case N11_DISCONNECT_REQUEST:
	case N15_SUSPEND_REQUEST:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}















	// FIXME
	if (call->interface->role == LAPD_ROLE_TE)
		call->user_state = U11_DISCONNECT_REQUEST;
	else
		call->net_state = N11_DISCONNECT_REQUEST;

	int inband_info = FALSE;

	{
	int i;
	int ie_count = 0;

	for(i=0; i<ies_cnt; i++) {
		if (ies[i].info->id == Q931_IE_PROGRESS_INDICATOR) {
			struct q931_ie_progress_indicator_onwire_3_4 *ie =
				(struct q931_ie_progress_indicator_onwire_3_4 *)(ies[i].data);

			if (ie->coding_standard == Q931_IE_PI_CS_CCITT &&
			    ie->progress_description ==
				Q931_IE_PI_PD_IN_BAND_INFORMATION_OR_APPROPRIATE_PATTERN_AVAILABLE) {
				inband_info = TRUE;
				ie_count++;

				if (ie_count == 2)
					break;
			}
		}
	}
	}

	if (inband_info) {
		// Connect B channel if not yet connected

		if (call->interface->role == LAPD_ROLE_TE)
			call->user_state = U12_DISCONNECT_INDICATION;
		else
			call->net_state = N12_DISCONNECT_INDICATION;
	} else {
		// Disconnect B channel

		// Start T308
		if (call->interface->role == LAPD_ROLE_TE)
			call->user_state = U19_RELEASE_REQUEST;
		else
			call->net_state = N19_RELEASE_REQUEST;

		q931_send_release(call, call->selected_dlc);
	}

	if (call->disconnect_callback)
		call->disconnect_callback(call);
}

const char *q931_state_to_text(enum q931_state state)
{
	switch (call->state) {
	case U0_NULL_STATE: return "U0_NULL_STATE";
	case U1_CALL_INITIATED: return "U1_CALL_INITIATED";
	case U2_OVERLAP_SENDING: return "U2_OVERLAP_SENDING";
	case U3_OUTGOING_CALL_PROCEEDING: return "U3_OUTGOING_CALL_PROCEEDING";
	case U4_CALL_DELIVERED: return "U4_CALL_DELIVERED";
	case U6_CALL_PRESENT: return "U6_CALL_PRESENT";
	case U7_CALL_RECEIVED: return "U7_CALL_RECEIVED";
	case U8_CONNECT_REQUEST: return "U8_CONNECT_REQUEST";
	case U9_INCOMING_CALL_PROCEEDING: return "U9_INCOMING_CALL_PROCEEDING";
	case U10_ACTIVE: return "U10_ACTIVE";
	case U11_DISCONNECT_REQUEST: return "U11_DISCONNECT_REQUEST";
	case U12_DISCONNECT_INDICATION: return "U12_DISCONNECT_INDICATION";
	case U15_SUSPEND_REQUEST: return "U15_SUSPEND_REQUEST";
	case U17_RESUME_REQUEST: return "U17_RESUME_REQUEST";
	case U19_RELEASE_REQUEST: return "U19_RELEASE_REQUEST";
	case U25_OVERLAP_RECEIVING: return "U25_OVERLAP_RECEIVING";
	case N0_NULL_STATE: return "N0_NULL_STATE";
	case N1_CALL_INITIATED: return "N1_CALL_INITIATED";
	case N2_OVERLAP_SENDING: return "N2_OVERLAP_SENDING";
	case N3_OUTGOING_CALL_PROCEEDING: return "N3_OUTGOING_CALL_PROCEEDING";
	case N4_CALL_DELIVERED: return "N4_CALL_DELIVERED";
	case N6_CALL_PRESENT: return "N6_CALL_PRESENT";
	case N7_CALL_RECEIVED: return "N7_CALL_RECEIVED";
	case N8_CONNECT_REQUEST: return "N8_CONNECT_REQUEST";
	case N9_INCOMING_CALL_PROCEEDING: return "N9_INCOMING_CALL_PROCEEDING";
	case N10_ACTIVE: return "N10_ACTIVE";
	case N11_DISCONNECT_REQUEST: return "N11_DISCONNECT_REQUEST";
	case N12_DISCONNECT_INDICATION: return "N12_DISCONNECT_INDICATION";
	case N15_SUSPEND_REQUEST: return "N15_SUSPEND_REQUEST";
	case N17_RESUME_REQUEST: return "N17_RESUME_REQUEST";
	case N19_RELEASE_REQUEST: return "N19_RELEASE_REQUEST";
	case N22_CALL_ABORT: return "N22_CALL_ABORT";
	case N25_OVERLAP_RECEIVING: return "N25_OVERLAP_RECEIVING";
	default: return "*UNKNOWN*";
	}
}

inline static void q931_handle_release(
	const struct q931_dlc *dlc,
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{

	switch (call->state) {
	case N0_NULL_STATE:
		q931_send_release_complete(call);

		q931_del_call(call);
	break; 

	case N1_CALL_INITIATED:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
		if (call->release_indication)
			call->release_indication(call);

		// Release B channel if not released

		q931_send_release_complete(call);

		q931_del_call(call);
	
		call->state = N0_NULL_STATE;
	break;

	case N2_OVERLAP_SENDING:
		// Stop T302

		if (call->release_indication)
			call->release_indication(call);

		// Release B channel if not released

		q931_send_release_complete(call);

		q931_del_call(call);
	
		call->state = N0_NULL_STATE;
	break;

	case N6_CALL_PRESENT:
		if (call->broadcasted_setup) {
			// Save cause
		} else {
			// Stop T303

			if (call->release_indication)
				call->release_indication(call);

			// Release B channel if not released

			q931_del_call(call);

			call->state = N0_NULL_STATE;
		}
	break;

	case N7_CALL_RECEIVED:
		if (call->broadcasted_setup) {
			q931_send_release_complete(call);
		} else {
			if (call->release_indication)
				call->release_indication(call);

			q931_send_release_complete(call);

			// Stop T302 if running

			// Release B channel if not released

			q931_del_call(call);

			call->state = N0_NULL_STATE;
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcasted_setup) {
			q931_send_release_complete(call);
		} else {
			if (call->release_indication)
				call->release_indication(call);

			q931_send_release_complete(call);

			// Release B channel if not released

			q931_del_call(call);

			call->state = N0_NULL_STATE;
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcasted_setup) {
			q931_send_release_complete(call);
		} else {
			// Stop T310 if running

			if (call->release_indication)
				call->release_indication(call);

			// Release B channel if not released

			q931_send_release_complete(call);

			q931_del_call(call);

			call->state = N0_NULL_STATE;
		}
	break;

	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
		if (call->release_indication)
			call->release_indication(call);
		
		// Release B channel if not released

		q931_send_release_complete(call);

		q931_del_call(call);

		call->state = N0_NULL_STATE;
	break;

	case N12_DISCONNECT_INDICATION:
		if (call->release_indication)
			call->release_indication(call);

		// Stop T305
		// Stop T306

		// Stop Tones/Announcements (if applicable)

		// Release B channel if not released

		q931_send_release_complete(call);

		q931_del_call(call);

		call->state = N0_NULL_STATE;
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}

}

inline static void q931_handle_release_complete(
	const struct q931_dlc *dlc,
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N1_CALL_INITIATED:
		// Release B channel if no released

		if (call->release_indication)
			call->release_indication(call);

		q931_del_call(call);

		call->state = N0_NULL_STATE;
	break;

	case N2_OVERLAP_SENDING:
		// Stop T302
		// Release B channel if not released

		if (call->release_indication)
			call->release_indication(call);

		q931_del_call(call);

		call->state = N0_NULL_STATE;
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
		// Release B channel if not released

		if (call->release_indication)
			call->release_indication(call);

		q931_del_call(call);

		call->state = N0_NULL_STATE;
	break;

	case N6_CALL_PRESENT:

		if (call->broadcasted_setup) {
			// Save cause
		} else {
			// Stop T303
			if (call->release_indication)
				call->release_indication(call);

			// Release B channel if not released

			q931_del_call(call);

			call->state = N0_NULL_STATE;
		}
	break;

	case N7_CALL_RECEIVED:
		if (call->broadcasted_setup) {
			// Do nothing
		} else {
			// Stop T301 if running

			// Release B channel if not released

			if (call->release_indication)
				call->release_indication(call);

			q931_del_call(call);

			call->state = N0_NULL_STATE;
		}
	break;

	case N8_CONNECT_REQUEST:
		if (call->broadcasted_setup) {
			// Do nothing
		} else {
			// Release B channel if not released

			if (call->release_indication)
				call->release_indication(call);

			q931_del_call(call);

			call->state = N0_NULL_STATE;
		}
	break;

	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcasted_setup) {
			// NOTE 1

			// Save cause
		} else {
			// Stop T310 if running

			// Release B channel if not released

			if (call->release_indication)
				call->release_indication(call);

			q931_del_call(call);

			call->state = N0_NULL_STATE;
		}
	break;

	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
		// Release B-channel if not released

		if (call->release_indication)
			call->release_indication(call);

		q931_del_call(call);

		call->state = N0_NULL_STATE;
	break;

	case N12_DISCONNECT_INDICATION:
		// Stop T305
		// Stop T306

		// Stop Tones/Announcements (if applicable)

		// Release B-channel if not released

		if (call->release_indication)
			call->release_indication(call);

		q931_del_call(call);

		call->state = N0_NULL_STATE;
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}











	// FIXME
	if (dlc == call->selected_dlc) {
		// Stop T308
		// Release B channel

		q931_del_call(call);

		if (call->interface->role == LAPD_ROLE_TE)
			call->user_state = U0_NULL_STATE;
		else
			call->net_state = U0_NULL_STATE;

		if (call->release_callback)
			call->release_callback(call);
	} else {
		// Non-selected user clearing

		// Remove DLC from associated DLCs
		int i,j;
		for (i=0; i<call->nces; i++) {
			if (call->ces[i] == dlc) {
				for (j = i; j<call->nces-1; j++)
					call->ces[j] = call->ces[j+1];

				call->nces--;

				break;
			}
		}
	}
}

inline static void q931_handle_restart(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_restart_acknowledge(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_status(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_status_enquiry(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	q931_send_status(call);
}

inline static void q931_handle_user_information(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	
}

inline static void q931_handle_segment(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_congestion_control(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_info(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N2_OVERLAP_SENDING:
		// Stop T302

		// If it's the first INFO
		// Stop dialtone on B channel

		if (call->info_indication)
			call->info_indication(call);

		// Start T302
	break;

	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N10_ACTIVE:
	case N11_DISCONNECT_REQUEST:
	case N12_DISCONNECT_INDICATION:
		if (call->info_indication)
			call->info_indication(call);
	break;

	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
		if (call->broadcasted_setup) {
			q931_send_status(call); // NOTE 1
		} else {
			if (call->info_indication)
				call->info_indication(call);
		}
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case U0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N6_CALL_PRESENT:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}














	int sending_complete = FALSE;
	int i;

	for(i=0; i<ies_cnt; i++) {
		if (ies[i].info->id == Q931_IE_SENDING_COMPLETE) {
			sending_complete = TRUE;
		} else if (ies[i].info->id == Q931_IE_CALLED_PARTY_NUMBER) {
			if (ies[i].size < 2) {
				call->interface->libstate->report(LOG_ERR,
					"IE size < 2\n");
				// Send status with cause code
				return;
			}

			if (strlen(call->called_number) + ies[i].size - 1
			    > Q931_MAX_DIGITS) {
				call->interface->libstate->report(LOG_ERR,
					"Called number overflow\n");
				// Send status with cause code
				return;
			}

			char *number = ies[i].data + 1;

			if (number[ies[i].size - 1] == '#') {
				sending_complete = TRUE;
				strncat(call->called_number, number,
					ies[i].size - 2);
			} else {
				call->interface->libstate->report(LOG_INFO,
					"strncat(%s,%c,%d)\n",call->called_number,
					*number,ies[i].size - 1);

				strncat(call->called_number, number,
					ies[i].size - 1);
			}
		}
	}

	if (sending_complete) {
		// Start T302
		q931_send_alerting(call);
		q931_send_connect(call);
	}

	call->interface->libstate->report(LOG_INFO, "Number: %s\n", call->called_number);

	if (call->information_callback)
		call->information_callback(call);
}

inline static void q931_handle_facility(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_notify(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (call->state) {
	case N10_ACTIVE:
		if (call->notify_indication)
			call->notify_indication(call);
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_handle_hold(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_hold_acknowledge(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_hold_reject(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_retrieve(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_retrieve_acknowledge(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_retrieve_reject(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_resume(
	const struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	// Only on BRIs

	if (call->resume_indication)
		call->resume_indication(call);

	if (call->interface->role == LAPD_ROLE_TE)
		call->state = N17_RESUME_REQUEST;
	else
		call->state = U17_RESUME_REQUEST;
}

inline static void q931_handle_resume_acknowledge(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_resume_reject(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_suspend(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	// Only for BRI

	switch (call->state) {
	case N10_ACTIVE:
		if (call->suspend_indication)
			call->suspend_indication(call);

		call->status = N15_SUSPEND_REQUEST:
	break;

	case N11_DISCONNECT_REQUEST:
	break;

	case N12_DISCONNECT_INDICATION:
	break;

	case N15_SUSPEND_REQUEST:
	break;

	case N17_RESUME_REQUEST:
	break;

	case N19_RELEASE_REQUEST:
	break;

	case N22_CALL_ABORT:
	break;

	case N25_OVERLAP_RECEIVING:
	break;


	case U1_CALL_INITIATED:
	break;

	case U2_OVERLAP_SENDING:
	break;

	case U3_OUTGOING_CALL_PROCEEDING:
	break;

	case U4_CALL_DELIVERED:
	break;

	case U6_CALL_PRESENT:
	break;

	case U7_CALL_RECEIVED:
	break;

	case U8_CONNECT_REQUEST:
	break;

	case U9_INCOMING_CALL_PROCEEDING:
	break;

	case U10_ACTIVE:
	break;

	case U11_DISCONNECT_REQUEST:
	break;

	case U12_DISCONNECT_INDICATION:
	break;

	case U15_SUSPEND_REQUEST:
	break;

	case U17_RESUME_REQUEST:
	break;

	case U19_RELEASE_REQUEST:
	break;

	case U25_OVERLAP_RECEIVING:
	break;

	case N0_NULL_STATE:
	case N1_CALL_INITIATED:
	case N2_OVERLAP_SENDING:
	case N3_OUTGOING_CALL_PROCEEDING:
	case N4_CALL_DELIVERED:
	case N6_CALL_PRESENT:
	case N7_CALL_RECEIVED:
	case N8_CONNECT_REQUEST:
	case N9_INCOMING_CALL_PROCEEDING:
	case U0_NULL_STATE:
	default:
		call->interface->report(LOG_ERROR,
			"Unexpected  in state %s\n",
			q931_state_to_text(call->state));
	break;
	}
}

inline static void q931_handle_suspend_acknowledge(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_suspend_reject(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

void q931_mdl_establish_indication(struct q931_dlc *dlc)
{
/*
a) For calls in the Overlap Sending and Overlap Receiving states, the entity
   shall initiate clearing by sending a DISCONNECT message with cause #41
   "temporary failure", and following the procedures of §5.3.
b) For calls in the disestablishment phase (states N11, N12, N19, N22, U11,
   U12, and U19) no action shall be taken.
c) Calls in the establishment phase (states N1, N3, N4, N6, N7, N8, N9, U1,
   U3, U4, U6, U7, U8, and U9) and in the Active, Suspend Request and Resume
   Request states shall be maintained according to the procedures contained
   in other parts of §5.
*/	

	
	dlc->status = DLC_CONNECTED;
}

void q931_mdl_release_indication(struct q931_dlc *dlc)
{
	// Not fully implemented

	dlc->status = DLC_DISCONNECTED;
}

static void q931_dispatch_message(
	struct q931_call *call,
	__u8 message_type,
	const struct q931_ie *ies,
	int ies_cnt);

void q931_receive(struct q931_dlc *dlc)
{
	struct msghdr msg;
	struct sockaddr_lapd sal;
	struct cmsghdr cmsg;
	struct iovec iov;

	__u8 frame[512];

	iov.iov_base = frame;
	iov.iov_len = sizeof(frame);

	msg.msg_name = &sal;
	msg.msg_namelen = sizeof(sal);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &cmsg;
	msg.msg_controllen = sizeof(cmsg);
	msg.msg_flags = 0;

	int len;
	len = recvmsg(dlc->socket, &msg, 0);
	if(len < 0) {
		if (errno == ECONNRESET)
			q931_mdl_release_indication(dlc);
		else if (errno == EISCONN)
			q931_mdl_establish_indication(dlc);
		else {
			report_dlc(dlc, LOG_ERR,
				"recvmsg: %s\n", strerror(errno));
		}

		return;
	}

	struct q931_header *hdr = (struct q931_header *)frame;

	if (hdr->call_reference_size>3) {
		// TODO error
		report_dlc(dlc, LOG_ERR,
			"Call reference length > 3 ????\n");

		return;
	}

	q931_callref callref = 0;
	int callref_direction = Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE;

	int i;
	for(i=0; i<hdr->call_reference_size; i++) {

		__u8 val = hdr->call_reference[i];

		if (i==0 && val & 0x80) {
			val &= 0x7f;

			callref_direction =
				Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE;
		}
	   
		// LITTLE ENDIAN
		callref |= val << ((hdr->call_reference_size-i-1) * 8);
	}

	report_dlc(dlc, LOG_INFO,
		"  protocol descriptor = %u\n",
		hdr->protocol_discriminator);
	report_dlc(dlc, LOG_INFO,
		"  call reference = %u %lu %c\n",
		hdr->call_reference_size,
		callref,
		callref_direction?'O':'I');

	__u8 message_type = *(__u8 *)(frame + sizeof(struct q931_header) +
		hdr->call_reference_size);

	report_dlc(dlc, LOG_INFO, "  message_type = %s (%u)\n",
		q931_get_message_type_name(message_type),
			message_type);

	struct q931_call *call =
		q931_find_call_by_reference(
			dlc->interface,
			callref_direction ==
				Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE
				? Q931_CALL_DIRECTION_INBOUND
				: Q931_CALL_DIRECTION_OUTBOUND,
			callref);

	if (!call) {
		call = q931_alloc_call();
		if (!call) {
			report_dlc(dlc, LOG_ERR,
				"Error allocating call\n");
			break;
		}

		call->selected_ces = ces;
		call->interface = dlc->interface;

		call->broadcasted_setup = msg & MSG_OOB;
		call->direction = Q931_CALL_DIRECTION_INBOUND;
		call->call_reference = call_reference;

		q931_add_call(dlc->interface, call);

		// Shortcut for "Other Messages"
		if (message_type != Q931_MT_SETUP &&
		    message_type != Q931_MT_RESUME &&
		    message_type != Q931_MT_RELEASE &&
		    message_type != Q931_MT_STATUS &&
		    message_type != Q931_MT_RELEASE_COMPLETE) {
			q931_send_release(call);

			// Start T308
			call->state = U19_RELEASE_REQUEST;

			return;
		}
	}

	// Decode information elemnts
	struct q931_ie ies[260];
	int ies_cnt = 0;

	int curpos= sizeof(struct q931_header) + hdr->call_reference_size + 1;
	int codeset = 0;
	int codeset_locked = FALSE;

	i=0;
	while(curpos < len) {
		__u8 first_octet = *(__u8 *)(frame + curpos);
		curpos++;

		if (q931_is_so_ie(first_octet)) {
			// Single octet IE

			if (q931_get_so_ie_id(first_octet) == Q931_IE_SHIFT) {
				if (q931_get_so_ie_type2_value(first_octet) & 0x08) {
					// Locking shift

					report_dlc(dlc, LOG_INFO,
						"Locked Switch from codeset %u to codeset %u",
						codeset,
						q931_get_so_ie_type2_value(first_octet) & 0x07);

					codeset = q931_get_so_ie_type2_value(first_octet) & 0x07;
					codeset_locked = FALSE;

					continue;
				} else {
					// Non-Locking shift

					report_dlc(dlc, LOG_INFO,
						"Non-Locked Switch from codeset %u to codeset %u",
						codeset,
						q931_get_so_ie_type2_value(first_octet));

					codeset = q931_get_so_ie_type2_value(first_octet);
					codeset_locked = TRUE;

					continue;
				}
			}

			if (codeset == 0) {
				const struct q931_ie_info *ie_info =
					q931_get_ie_info(first_octet);

				if (ie_info) {
					report_dlc(dlc, LOG_INFO,
						"SO IE %d ===> %u (%s)\n", i,
						first_octet,
						ie_info->name);

					ies[ies_cnt].info = ie_info;
					ies[ies_cnt].size = 0;
					ies[ies_cnt].data = NULL;
					ies_cnt++;
				} else {
					report_dlc(dlc, LOG_INFO,
						"SO IE %d ===> %u (unknown)\n", i,
						first_octet);
				}
			}
		} else {
			// Variable Length IE

			__u8 ie_len = *(__u8 *)(frame + curpos);
			curpos++;

			if (codeset == 0) {
				const struct q931_ie_info *ie_info =
					q931_get_ie_info(first_octet);

				if (ie_info) {
					report_dlc(dlc, LOG_INFO,
						"VS IE %d ===> %u (%s) -- length %u\n", i,
						first_octet,
						ie_info->name,
						ie_len);

				} else {
					report_dlc(dlc, LOG_INFO,
						"VS IE %d ===> %u (unknown) -- length %u\n", i,
						first_octet,
						ie_len);
				}

				ies[ies_cnt].info = ie_info;
				ies[ies_cnt].size = ie_len;
				ies[ies_cnt].data = frame + curpos;
				ies_cnt++;
			}

			curpos += ie_len;

			if(curpos > len) {
				report_dlc(dlc, LOG_ERR, "MALFORMED FRAME\n");
				break;
			}
		}

		if (!codeset_locked) codeset = 0;

		i++;
	}

	struct q931_ces *ces;
	list_for_each_entry(ces, &call->ces, node) {
		if (ces->dlc == dlc) {

			q931_ces_dispatch_message(ces);

			if (ces == call->selected_ces)
				break;
			else
				return;
		}
	}



}

static void q931_dispatch_message(
	struct q931_call *call,
	__u8 message_type,
	const struct q931_ie *ies,
	int ies_cnt)
{
	switch (message_type) {
	case Q931_MT_ALERTING:
		q931_handle_alerting(call, ies, ies_cnt);
	break;

	case Q931_MT_CALL_PROCEEDING:
		q931_handle_call_proceeding(call, ies, ies_cnt);
	break;

	case Q931_MT_CONNECT:
		q931_handle_connect(dlc, call, ies, ies_cnt);
	break;

	case Q931_MT_CONNECT_ACKNOWLEDGE:
		q931_handle_connect_acknowledge(call, ies, ies_cnt);
	break;

	case Q931_MT_PROGRESS:
		q931_handle_progress(call, ies, ies_cnt);
	break;

	case Q931_MT_SETUP:
		q931_handle_setup(call, ies, ies_cnt);
	break;

	case Q931_MT_SETUP_ACKNOWLEDGE:
		q931_handle_setup_acknowledge(call, ies, ies_cnt);
	break;

	case Q931_MT_DISCONNECT:
		q931_handle_disconnect(call, ies, ies_cnt);
	break;

	case Q931_MT_RELEASE:
		q931_handle_release(call, ies, ies_cnt);
	break;

	case Q931_MT_RELEASE_COMPLETE:
		q931_handle_release_complete(call, ies, ies_cnt);
	break;

	case Q931_MT_RESTART:
		q931_handle_restart(call, ies, ies_cnt);
	break;

	case Q931_MT_RESTART_ACKNOWLEDGE:
		q931_handle_restart_acknowledge(call, ies, ies_cnt);
	break;


	case Q931_MT_STATUS:
		q931_handle_status(call, ies, ies_cnt);
	break;

	case Q931_MT_STATUS_ENQUIRY:
		q931_handle_status_enquiry(call, ies, ies_cnt);
	break;

	case Q931_MT_USER_INFORMATION:
		q931_handle_user_information(call, ies, ies_cnt);
	break;

	case Q931_MT_SEGMENT:
		q931_handle_segment(call, ies, ies_cnt);
	break;

	case Q931_MT_CONGESTION_CONTROL:
		q931_handle_congestion_control(call, ies, ies_cnt);
	break;

	case Q931_MT_INFORMATION:
		q931_handle_info(call, ies, ies_cnt);
	break;

	case Q931_MT_FACILITY:
		q931_handle_facility(call, ies, ies_cnt);
	break;

	case Q931_MT_NOTIFY:
		q931_handle_notify(call, ies, ies_cnt);
	break;


	case Q931_MT_HOLD:
		q931_handle_hold(call, ies, ies_cnt);
	break;

	case Q931_MT_HOLD_ACKNOWLEDGE:
		q931_handle_hold_acknowledge(call, ies, ies_cnt);
	break;

	case Q931_MT_HOLD_REJECT:
		q931_handle_hold_reject(call, ies, ies_cnt);
	break;

	case Q931_MT_RETRIEVE:
		q931_handle_retrieve(call, ies, ies_cnt);
	break;

	case Q931_MT_RETRIEVE_ACKNOWLEDGE:
		q931_handle_retrieve_acknowledge(call, ies, ies_cnt);
	break;

	case Q931_MT_RETRIEVE_REJECT:
		q931_handle_retrieve_reject(call, ies, ies_cnt);
	break;

	case Q931_MT_RESUME:
		q931_handle_resume(call, ies, ies_cnt);
	break;

	case Q931_MT_RESUME_ACKNOWLEDGE:
		q931_handle_resume_acknowledge(call, ies, ies_cnt);
	break;

	case Q931_MT_RESUME_REJECT:
		q931_handle_resume_reject(call, ies, ies_cnt);
	break;

	case Q931_MT_SUSPEND:
		q931_handle_suspend(call, ies, ies_cnt);
	break;

	case Q931_MT_SUSPEND_ACKNOWLEDGE:
		q931_handle_suspend_acknowledge(call, ies, ies_cnt);
	break;

	case Q931_MT_SUSPEND_REJECT:
		q931_handle_suspend_reject(call, ies, ies_cnt);
	break;

	default:
		report_dlc(dlc, LOG_WARNING,
			"Unkwnon/unhandled message type %d\n",
			message_type);
	break;
	}
}
