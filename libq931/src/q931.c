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

#include <lapd_user.h>

#include "list.h"

#include "q931.h"
#include "q931_mt.h"
#include "q931_ie.h"

inline static void q931_add_call(
	struct q931_interface *interface,
	struct q931_call *call)
{
	list_add_tail(&call->node, &interface->calls);
	interface->ncalls++;
}

inline static void q931_del_call(
	struct q931_call *call)
{
	list_del(&call->node);
	
	if (call->dlc)
		call->dlc->interface->ncalls--;
	else
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
	list_for_each_entry(call, &interface->calls, node)
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
	list_for_each_entry(call, &interface->calls, node)
	 {
printf("Searching ==========> %d=%d %lu=%lu\n",call->direction,direction,call->call_reference,call_reference);

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

	printf("q931_send_frame\n");

	if (sendmsg(dlc->socket, &msg, 0) < 0)
	 {
	  printf("sendmsg error: %s\n",strerror(errno));
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

	printf("q931_send_uframe\n");

	if (sendmsg(interface->nt_socket, &msg, MSG_OOB) < 0)
	 {
	  printf("sendmsg error: %s\n",strerror(errno));
	  return errno;
	 }

	return 0;
}

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

	printf("q931_send_uframe\n");

	if (sendmsg(dlc->socket, &msg, MSG_OOB) < 0)
	 {
	  printf("sendmsg error: %s\n",strerror(errno));
	  return errno;
	 }

	return 0;
}

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

	return q931_send_frame(call->dlc, frame, size);
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

	return q931_send_frame(call->dlc, frame, size);
}

static int q931_send_release(struct q931_call *call)
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

	return q931_send_frame(call->dlc, frame, size);
}

static int q931_send_release_complete(struct q931_call *call)
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

	return q931_send_frame(call->dlc, frame, size);
}

static int q931_send_setup(struct q931_call *call)
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

	if (!call->dlc && call->interface->role == LAPD_ROLE_NT) {
		size += q931_append_ie_channel_identification(
			frame + size, Q931_IE_CI_ICS_BRI_B1);
	}

	if (strlen(call->calling_number))
		size += q931_append_ie_calling_party_number(frame + size, call->calling_number);

	size += q931_append_ie_called_party_number(frame + size, call->called_number);
	size += q931_append_ie_sending_complete(frame + size);

	if (call->dlc)
		return q931_send_frame(call->dlc, frame, size);
	else
		return q931_send_bc_uframe(call->interface, frame, size);
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

	return q931_send_frame(call->dlc, frame, size);
}

static enum q931_ie_call_state_call_state_net
	q931_net_state_to_ie_state(
		enum q931_network_state state)
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
	}
}

static enum q931_ie_call_state_call_state_user
	q931_user_state_to_ie_state(
		enum q931_user_state state)
{
	switch (state) {
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

	if (call->interface->role == LAPD_ROLE_TE) {
		size += q931_append_ie_call_state(frame + size,
			q931_user_state_to_ie_state(call->user_state));
	} else {
		size += q931_append_ie_call_state(frame + size,
			q931_net_state_to_ie_state(call->net_state));
	}

	return q931_send_frame(call->dlc, frame, size);
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

	return q931_send_frame(call->dlc, frame, size);
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

	return q931_send_frame(call->dlc, frame, size);
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

	return q931_send_frame(call->dlc, frame, size);
}

void q931_init()
{
	q931_ie_infos_init();
	q931_message_types_init();
}

struct q931_interface *q931_open_interface(const char *name)
{
	struct q931_interface *interface;

	assert(name);

	interface = malloc(sizeof(*interface));
	if (!interface) abort();
	memset(interface, 0x00, sizeof(*interface));

	INIT_LIST_HEAD(&interface->calls);

	interface->name = strdup(name);
	interface->next_call_reference = 1;
	interface->call_reference_size = 1; // FIXME should be 1 for BRI, 2 for PRI

	int s = socket(PF_LAPD, SOCK_DGRAM, 0);
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
	interface->te_dlc.interface = interface;
	interface->te_dlc.socket = s;

	printf("connecting...");

	if (connect(s, NULL, 0) < 0)
	   goto err_connect;

	printf("OK\n");
	} else {
	interface->te_dlc.interface = NULL;
	interface->te_dlc.socket = -1;

	interface->nt_socket = s;
	}

	return interface;

err_connect:
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

	if (interface->name) free(interface->name);
	free(interface);
}

struct q931_call *q931_alloc_call()
{
	struct q931_call *call;

	call = malloc(sizeof(*call));
	if (!call) abort();
	memset(call, 0x00, sizeof(*call));

	INIT_LIST_HEAD(&call->node);

	strcpy(call->calling_number, "");
	strcpy(call->called_number, "");

	return call;
}

void q931_hangup_call(struct q931_call *call)
{
	assert(call);

	if (call->interface->role == LAPD_ROLE_TE) {
		if (call->user_state != U0_NULL_STATE &&
		    call->user_state != U11_DISCONNECT_REQUEST &&
		    call->user_state != U12_DISCONNECT_INDICATION)
			q931_send_disconnect(call);

		call->user_state = U11_DISCONNECT_REQUEST;
	} else {
		if (call->user_state != N0_NULL_STATE &&
		    call->user_state != N11_DISCONNECT_REQUEST &&
		    call->user_state != N12_DISCONNECT_INDICATION)
			q931_send_disconnect(call);

		call->net_state = N12_DISCONNECT_INDICATION;
	}

	q931_send_disconnect(call);
	// Start T305
	// Disconnect B channel


}

void q931_free_call(struct q931_call *call)
{
	assert(call);
	assert(call->node.next == LIST_POISON1);
	assert(call->node.prev == LIST_POISON2);

	free(call);
}

int q931_make_call(struct q931_interface *interface, struct q931_call *call)
{
	assert(!call->interface);
	assert(!call->dlc);

	call->interface = interface;

	if (interface->role == LAPD_ROLE_TE)
		call->dlc = &interface->te_dlc;
	else
		call->dlc = NULL;

	call->direction = Q931_CALL_DIRECTION_OUTBOUND;

	call->call_reference = q931_alloc_call_reference(interface);

	if (call->call_reference < 0) {
		printf("All call references are used!!!\n");
		return -1;
	}

	printf("Call reference allocated (%ld)\n", call->call_reference);

	q931_add_call(interface, call);

	q931_send_setup(call);
	call->user_state = U1_CALL_INITIATED;

	// If we know all channels are busy we should not send SETUP (5.1)

	return 0;
}

inline static void q931_handle_alerting(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	// if user: Stop T304

	if (call->alerting_callback)
		call->alerting_callback(call);
}

inline static void q931_handle_call_proceeding(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	if (call->interface->role == LAPD_ROLE_TE)
	  call->user_state = U3_OUTGOING_CALL_PROCEEDING;
	  // Stop T302
	else
	  call->net_state = N3_OUTGOING_CALL_PROCEEDING;
}

inline static void q931_handle_connect(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	if (call->interface->role == LAPD_ROLE_TE)
	  call->user_state = U10_ACTIVE;
	  // Stop T304
	else
	  call->net_state = N10_ACTIVE;
	q931_send_connect_acknowledge(call);

	if (call->connect_callback)
		call->connect_callback(call);
}

inline static void q931_handle_connect_acknowledge(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_progress(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

inline static void q931_handle_setup(
	const struct q931_dlc *dlc,
	unsigned long call_reference,
	const struct q931_ie *ies,
	int ies_cnt)
{
	struct q931_call *call;
	int sending_complete = FALSE;

	call = q931_alloc_call();
	if (!call) {
		printf("Error allocating call\n");
		break;
	}

	call->dlc = dlc;
	call->interface = dlc->interface;

	call->direction = Q931_CALL_DIRECTION_INBOUND;
	call->call_reference = call_reference;

	if (call->interface->role == LAPD_ROLE_TE)
		call->user_state = U6_CALL_PRESENT;
	else
		call->net_state = N6_CALL_PRESENT;

	printf("New call (%lu) allocated\n", call->call_reference);

	q931_add_call(dlc->interface, call);

	{
	int i;

	for(i=0; i<ies_cnt; i++) {
		if (ies[i].info->id == Q931_IE_SENDING_COMPLETE) {
			sending_complete = TRUE;
		} else if (ies[i].info->id == Q931_IE_CALLED_PARTY_NUMBER) {
			if (ies[i].size < 2) {
				printf("IE size < 2\n");
				// Send status with cause code
				return;
			}

			if (ies[i].size - 1 > Q931_MAX_DIGITS) {
				printf("IE size > Q931_MAX_DIGITS + 1\n");
				// Send status with cause code
				return;
			}

			char *number = ies[i].data + 1;

			if (number[ies[i].size - 1] == '#') {
				sending_complete = TRUE;
				strncat(call->called_number, number,
					ies[i].size - 1);
			} else {
				strncat(call->called_number, number,
					ies[i].size - 2);
			}
		}
	}
	}

	if (sending_complete) {
		// Start T302
		q931_send_call_proceeding(call);
		q931_send_alerting(call);
		q931_send_connect(call);
	} else {
		q931_send_setup_acknowledge(call);
	}

	printf("Number: %s\n", call->called_number);
}

inline static void q931_handle_setup_acknowledge(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	if (call->dlc->interface->role == LAPD_ROLE_TE)
	  call->user_state = U2_OVERLAP_SENDING;
	else
	  call->net_state = N2_OVERLAP_SENDING;
}

// T305
// See 5.3.3

inline static void q931_handle_disconnect(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	if (call->dlc->interface->role == LAPD_ROLE_TE)
	  call->user_state = U11_DISCONNECT_REQUEST;
	else
	  call->net_state = N11_DISCONNECT_REQUEST;

	int inband_info = FALSE;

	{
	 int i;
	 int ie_count = 0;

	 for(i=0; i<ies_cnt; i++)
	  {
	   if (ies[i].info->id == Q931_IE_PROGRESS_INDICATOR) 
	    {
	     struct q931_ie_progress_indicator_onwire_3_4 *ie =
	       (struct q931_ie_progress_indicator_onwire_3_4 *)(ies[i].data);

	     if (ie->coding_standard == Q931_IE_PI_CS_CCITT &&
	         ie->progress_description ==
	           Q931_IE_PI_PD_IN_BAND_INFORMATION_OR_APPROPRIATE_PATTERN_AVAILABLE)
	      {
	       inband_info = TRUE;
	       ie_count++;

	       if (ie_count == 2) break;
	      }
	    }
	  }
	}

	if (inband_info)
	 {
	  // Connect B channel if not yet connected

	  if (call->dlc->interface->role == LAPD_ROLE_TE)
	    call->user_state = U12_DISCONNECT_INDICATION;
	  else
	    call->net_state = N12_DISCONNECT_INDICATION;
	 }
	else
	 {
	  // Disconnect B channel

	  // Start T308
	  if (call->dlc->interface->role == LAPD_ROLE_TE)
	    call->user_state = U19_RELEASE_REQUEST;
	  else
	    call->net_state = N19_RELEASE_REQUEST;

	  q931_send_release(call);
	 }
}

inline static void q931_handle_release(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	// Stop T305
	// Release B chans

	q931_del_call(call);

	q931_send_release_complete(call);

	if (call->dlc->interface->role == LAPD_ROLE_TE)
	  call->user_state = U0_NULL_STATE;
	else
	  call->net_state = U0_NULL_STATE;
}

inline static void q931_handle_release_complete(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	// Stop T308
	// Release B channel

	list_del(&call->node);

	if (call->dlc->interface->role == LAPD_ROLE_TE)
	  call->user_state = U0_NULL_STATE;
	else
	  call->net_state = U0_NULL_STATE;

	if (call->release_callback)
		call->release_callback(call);
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

inline static void q931_handle_information(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
	int sending_complete = FALSE;
	int i;

	for(i=0; i<ies_cnt; i++) {
		if (ies[i].info->id == Q931_IE_SENDING_COMPLETE) {
			sending_complete = TRUE;
		} else if (ies[i].info->id == Q931_IE_CALLED_PARTY_NUMBER) {
			if (ies[i].size < 2) {
				printf("IE size < 2\n");
				// Send status with cause code
				return;
			}

			if (strlen(call->called_number) + ies[i].size - 1
				 > Q931_MAX_DIGITS) {
				printf("Called number overflow\n");
				// Send status with cause code
				return;
			}

			char *number = ies[i].data + 1;

			if (number[ies[i].size - 1] == '#') {
				sending_complete = TRUE;
				strncat(call->called_number, number,
					ies[i].size - 2);
			} else {
				printf("strncat(%s,%c,%d)\n",call->called_number,*number,ies[i].size - 1);

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

	printf("Number: %s\n", call->called_number);
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
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
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

inline static void q931_handle_(
	struct q931_call *call,
	const struct q931_ie *ies,
	int ies_cnt)
{
}

void q931_receive(const struct q931_dlc *dlc)
{
	struct msghdr msg;
	struct sockaddr_lapd sal;
	struct cmsghdr cmsg;
	struct iovec iov;

	q931_init();

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
	if(len < 0)
	 {
	  printf("recvmsg: %d %s\n",errno,strerror(errno));
	  exit(1);
	 }

	printf("recv ok (len=%d): ", len);
	int i;
	for(i=0; i<len; i++)
	  printf("%02x",frame[i]);
	printf("\n");

	struct q931_header *hdr = (struct q931_header *)frame;

	if (hdr->call_reference_size>3)
	 {
	  // TODO error
	  printf("Call reference length > 3 ????\n");
	  return;
	 }

	q931_callref callref = 0;
	int callref_direction = Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE;

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

	printf("  protocol descriptor = %u\n", hdr->protocol_discriminator);
	printf("  call reference = %u %lu %c\n",
		hdr->call_reference_size,
		callref,
		callref_direction?'O':'I');

	__u8 message_type = *(__u8 *)(frame + sizeof(struct q931_header) +
		hdr->call_reference_size);

	printf("  message_type = %s (%u)\n",
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

	if (!call && message_type != Q931_MT_SETUP) {
		printf("Received message for an unknown callref %lu\n",
			callref);

		return;
	}

	if (call && !call->dlc) {
		// The user responded to a broadcast SETUP, the call still
		// doesn't have an associated DLC, let's assign one to it :)

		call->dlc = dlc;
	}

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

					printf("Locked Switch from codeset %u to codeset %u",
						codeset,
						q931_get_so_ie_type2_value(first_octet) & 0x07);

					codeset = q931_get_so_ie_type2_value(first_octet) & 0x07;
					codeset_locked = FALSE;

					continue;
				} else {
					// Non-Locking shift

					printf("Non-Locked Switch from codeset %u to codeset %u",
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
					printf("SO IE %d ===> %u (%s)\n", i,
						first_octet,
						ie_info->name);

					ies[ies_cnt].info = ie_info;
					ies[ies_cnt].size = 0;
					ies[ies_cnt].data = NULL;
					ies_cnt++;
				} else {
					printf("SO IE %d ===> %u (unknown)\n", i,
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
					printf("VS IE %d ===> %u (%s) -- length %u\n", i,
						first_octet,
						ie_info->name,
						ie_len);

				} else {
					printf("VS IE %d ===> %u (unknown) -- length %u\n", i,
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
				printf("MALFORMED FRAME\n");
				break;
			}
		}

		if (!codeset_locked) codeset = 0;

		i++;
	}

	switch (message_type) {
	case Q931_MT_ALERTING:
		q931_handle_alerting(call, ies, ies_cnt);
	break;

	case Q931_MT_CALL_PROCEEDING:
		q931_handle_call_proceeding(call, ies, ies_cnt);
	break;

	case Q931_MT_CONNECT:
		q931_handle_connect(call, ies, ies_cnt);
	break;

	case Q931_MT_CONNECT_ACKNOWLEDGE:
		q931_handle_connect_acknowledge(call, ies, ies_cnt);
	break;

	case Q931_MT_PROGRESS:
		q931_handle_progress(call, ies, ies_cnt);
	break;

	case Q931_MT_SETUP:
		if (call)
		 {
		  // FIXME
		  printf("Setup on an existent call?!? What should we do?\n");
		  break;
		 }

		q931_handle_setup(dlc, callref, ies, ies_cnt);
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
		q931_handle_information(call, ies, ies_cnt);
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
		printf("Unkwnon/unhandled message type %d\n", message_type);
	break;
	}
}
