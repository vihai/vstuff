/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU Lesser General Public License.
 *
 */

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

#include <libq931/lib.h>
#include <libq931/logging.h>
#include <libq931/msgtype.h>
#include <libq931/ie.h>
#include <libq931/out.h>
#include <libq931/call.h>
#include <libq931/intf.h>
#include <libq931/proto.h>
#include <libq931/callref.h>

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
		if (connect(dlc->socket, NULL, 0) < 0) {
			report_dlc(dlc, LOG_ERR, "connect: %s\n", strerror(errno));
			return errno;
		}

		q931_dl_establish_confirm(dlc);
	}

	if (sendmsg(dlc->socket, &msg, 0) < 0) {
		report_dlc(dlc, LOG_ERR, "sendmsg error: %s\n",strerror(errno));
		return errno;
	} else {
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

int q931_send_message(
	struct q931_call *call,
	struct q931_dlc *dlc,
	enum q931_message_type mt,
	const struct q931_ies *user_ies)
{
	assert(call);
	assert(dlc);

	__u8 buf[260];
	int size = 0;

	size += q931_prepare_header(call, buf, mt);

	if (user_ies) {
		struct q931_ies ies = Q931_IES_INIT;
		q931_ies_copy(&ies, user_ies);
		q931_ies_sort(&ies);

		int i;
		for (i=0; i<ies.count; i++) {
			assert(ies.ies[i]->type->write_to_buf);

			int ie_len = ies.ies[i]->type->write_to_buf(ies.ies[i],
					buf + size,
					sizeof(buf) - size);
			size += ie_len;

			report_call(call, LOG_DEBUG,
				"VS IE %d ===> %u (%s) -- length %u\n",
				i,
				ies.ies[i]->type->id,
				ies.ies[i]->type->name,
				ie_len);

			if (ies.ies[i]->type->dump)
				ies.ies[i]->type->dump(
					ies.ies[i],
					call->intf->lib->report, "  ");
		}
	}

	return q931_send_frame(dlc, buf, size);
}

int q931_send_message_bc(
	struct q931_call *call,
	struct q931_dlc *dlc,
	enum q931_message_type mt,
	const struct q931_ies *user_ies)
{
	assert(call);
	assert(dlc);

	__u8 buf[260];
	int size = 0;

	size += q931_prepare_header(call, buf, mt);

	if (user_ies) {
		struct q931_ies ies = Q931_IES_INIT;
		q931_ies_copy(&ies, user_ies);
		q931_ies_sort(&ies);

		int i;
		for (i=0; i<ies.count; i++) {
			assert(ies.ies[i]->type->write_to_buf);

			size += ies.ies[i]->type->write_to_buf(ies.ies[i],
						buf + size,
						sizeof(buf) - size);

			if (ies.ies[i]->type->dump)
				ies.ies[i]->type->dump(
					ies.ies[i],
					call->intf->lib->report, "  ");
		}
	}

	return q931_send_uframe(dlc, buf, size);
}

int q931_global_send_message(
	struct q931_global_call *gc,
	struct q931_dlc *dlc,
	enum q931_message_type mt,
	const struct q931_ies *user_ies)
{
	assert(gc);
	assert(dlc);

	__u8 buf[260];
	int size = 0;

	size += q931_global_prepare_header(gc, buf, Q931_MT_STATUS);

	if (user_ies) {
		struct q931_ies ies = Q931_IES_INIT;
		q931_ies_copy(&ies, user_ies);
		q931_ies_sort(&ies);

		int i;
		for (i=0; i<ies.count; i++) {
			assert(ies.ies[i]->type->write_to_buf);

			size += ies.ies[i]->type->write_to_buf(ies.ies[i],
					buf + size,
					sizeof(buf) - size);

			if (ies.ies[i]->type->dump)
				ies.ies[i]->type->dump(
					ies.ies[i],
					gc->intf->lib->report, "  ");
		}
	}

	return q931_send_frame(dlc, buf, size);
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

/*************** CONNECT ACKNOWLEDGE
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Channel Identification	n->u	O
 * Display			n->u	O
 *
 */

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

/******************* NOTIFY
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Notification indicator	both	M
 * Display			n->u	O
 *
 */

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

/***************** RESUME
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Call Identity		u->n	O
 *
 */

/***************** RESUME ACKNOWLEDGE
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Channel Identification	n->u	M
 * Display			n->u	O
 *
 */

/******************* RESUME REJECT
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Cause			n->u	M
 * Display			n->u	O
 *
 */

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

/*************** STATUS ENQUIRY
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Display			n->u	O
 *
 */

/*********** SUSPEND
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Call Identity		u->n	O
 *
 */

/************** SUSPEND ACKNOWLEDGE
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Display			n->u	O
 *
 */

/**************** SUSPEND REJECT
 *
 * IEs:
 *
 * Information Element		Dir.	Type
 * Cause			both	M
 * Display			n->u	O
 *
 */

