/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
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

#include <linux/lapd.h>

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/logging.h>
#include <libq931/msgtype.h>
#include <libq931/ie.h>
#include <libq931/output.h>
#include <libq931/input.h>
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

	struct q931_header *hdr = (struct q931_header *)(frame + size);
	size += sizeof(struct q931_header);

	memset(hdr, 0, sizeof(*hdr));

	hdr->protocol_discriminator = Q931_PROTOCOL_DISCRIMINATOR_Q931;

	assert(call->intf);
	assert(call->intf->call_reference_len >=1 &&
		call->intf->call_reference_len <= 4);

	hdr->call_reference_len =
		call->intf->call_reference_len;

	// Call reference
	assert(call->call_reference > 0 &&
		call->call_reference < 
			(1 << ((hdr->call_reference_len * 8) - 1)));

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

	memset(hdr, 0, sizeof(*hdr));

	hdr->protocol_discriminator = Q931_PROTOCOL_DISCRIMINATOR_Q931;

	assert(gc->intf);
	assert(gc->intf->call_reference_len >=1 &&
		gc->intf->call_reference_len <= 4);

	hdr->call_reference_len =
		gc->intf->call_reference_len;

	q931_make_callref(frame + size,
		hdr->call_reference_len,
		0,
		Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE);

	size += hdr->call_reference_len;

	__u8 *message_type_onwire = (__u8 *)(frame + size);
	size++;
	*message_type_onwire = message_type;

	return size;
}

int q931_send_frame(struct q931_dlc *dlc, void *frame, int size)
{
	assert(dlc);
	assert(frame);
	assert(size > 0);

	struct msghdr msghdr;
	struct iovec iov;
	msghdr.msg_name = NULL;
	msghdr.msg_namelen = 0;
	msghdr.msg_iov = &iov;
	msghdr.msg_iovlen = 1;
	msghdr.msg_control = NULL;
	msghdr.msg_controllen = 0;
	msghdr.msg_flags = 0;

	iov.iov_base = frame;
	iov.iov_len = size;

	if (sendmsg(dlc->socket, &msghdr, 0) < 0) {
		report_dlc(dlc, LOG_ERR, "sendmsg error: %s\n",
		strerror(errno));

		return errno;
	}

	return 0;
}

int q931_send_frame_bc(struct q931_dlc *dlc, void *frame, int size)
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

	if (sendmsg(dlc->socket, &msg, MSG_OOB) < 0) {
		report_dlc(dlc, LOG_ERR, "sendmsg error: %s\n",
			strerror(errno));

		return errno;
	}

	return 0;
}

void q931_flush_outgoing_queue(
	struct q931_dlc *dlc)
{
	struct q931_message *msg, *n;
	list_for_each_entry_safe(msg, n, &dlc->outgoing_queue,
					outgoing_queue_node) {

		q931_send_frame(dlc, msg->raw, msg->rawlen);

		list_del(&msg->outgoing_queue_node);

		q931_msg_put(msg);
	}
}

static void q931_write_ies(
	struct q931_message *msg,
	const struct q931_ies *user_ies)
{
	Q931_DECLARE_IES(ies);
	q931_ies_copy(&ies, user_ies);
	q931_ies_sort(&ies);

	int i;
	for (i=0; i<ies.count; i++) {
		assert(ies.ies[i]->cls->write_to_buf);

		if (ies.ies[i]->cls->type == Q931_IE_TYPE_VL) {
			/* Write container */
			*(__u8 *)(msg->raw + msg->rawlen) =
				ies.ies[i]->cls->id;
			msg->rawlen++;

			/* Write payload before len */
			int ie_len = ies.ies[i]->cls->write_to_buf(
					ies.ies[i],
					msg->raw + msg->rawlen + 1,
					sizeof(msg->raw) - msg->rawlen);
			assert(ie_len >= 0);

			/* Now write len */
			*(__u8 *)(msg->raw + msg->rawlen) = ie_len;
			msg->rawlen++;

			/* Finally jump len */
			msg->rawlen += ie_len;

			report_msg_cont(msg, LOG_DEBUG,
				"->  VL IE %d ===> %u (%s) --"
				" length %u\n",
				i,
				ies.ies[i]->cls->id,
				ies.ies[i]->cls->name,
				ie_len);
		} else {
			int res = ies.ies[i]->cls->write_to_buf(ies.ies[i],
				msg->raw + msg->rawlen,
				sizeof(msg->raw) - msg->rawlen);
			assert(res > 0);

			msg->rawlen++;

			report_msg_cont(msg, LOG_DEBUG,
				"->  SO IE %d ===> %u (%s)\n",
				i,
				ies.ies[i]->cls->id,
				ies.ies[i]->cls->name);
		}

		if (ies.ies[i]->cls->dump)
			ies.ies[i]->cls->dump(
				ies.ies[i],
				q931_report, "->    ");
	}

	Q931_UNDECLARE_IES(ies);
}

static int q931_send_message(
	struct q931_call *call,
	struct q931_global_call *gc,
	struct q931_dlc *dlc,
	enum q931_message_type mt,
	const struct q931_ies *user_ies)
{
	int err;

	assert(dlc);
	assert(call || gc);
	assert(!call || !gc);

	struct q931_message *msg;
	msg = q931_msg_alloc(dlc);
	if (!msg) {
		err = -EFAULT;
		goto err_msg_alloc;
	}

	if (call) {
		msg->rawlen += q931_prepare_header(call, msg->raw, mt);

		report_call(call, LOG_DEBUG, "Sending message:\n");
	} else {
		msg->rawlen += q931_global_prepare_header(gc, msg->raw, mt);

		report_gc(gc, LOG_DEBUG, "Sending message:\n");
	}

	report_msg_cont(msg, LOG_DEBUG, "->  message type: %s (%d)\n",
		q931_message_type_to_text(mt), mt);

	if (user_ies)
		q931_write_ies(msg, user_ies);

	report_msg_cont(msg, LOG_DEBUG, "\n");

	// AWAITING_DISCONNECTION ??

	if (dlc->status == Q931_DLC_STATUS_DISCONNECTED) {
		report_dlc(dlc, LOG_DEBUG,
			"DLC is disconnected, requesting connection\n");

		dlc->status = Q931_DLC_STATUS_AWAITING_CONNECTION;

		if (connect(dlc->socket, NULL, 0) < 0) {
			if (errno != EAGAIN) {
				report_dlc(dlc, LOG_ERR, "connect: %s\n",
					strerror(errno));

				err = errno;
				goto err_connect;
			}
		}
	}

	int res = 0;
	if (dlc->status == Q931_DLC_STATUS_AWAITING_CONNECTION) {
		report_dlc(dlc, LOG_DEBUG,
			"DLC is awaiting connection: message queued\n");

		list_add_tail(
			&q931_msg_get(msg)->outgoing_queue_node,
			&dlc->outgoing_queue);
	} else {
		res = q931_send_frame(dlc, msg->raw, msg->rawlen);
	}

	q931_msg_put(msg);

	return res;

err_connect:
	q931_msg_put(msg);
err_msg_alloc:

	return err;
}

int q931_call_send_message(
	struct q931_call *call,
	struct q931_dlc *dlc,
	enum q931_message_type mt,
	const struct q931_ies *user_ies)
{
	return q931_send_message(call, NULL, dlc, mt, user_ies);
}

int q931_global_send_message(
	struct q931_global_call *gc,
	struct q931_dlc *dlc,
	enum q931_message_type mt,
	const struct q931_ies *user_ies)
{
	return q931_send_message(NULL, gc, dlc, mt, user_ies);
}

int q931_call_send_message_bc(
	struct q931_call *call,
	struct q931_dlc *dlc,
	enum q931_message_type mt,
	const struct q931_ies *user_ies)
{
	assert(call);
	assert(dlc);

	struct q931_message *msg;
	msg = q931_msg_alloc(dlc);
	if (!msg)
		return -EFAULT;

	report_call(call, LOG_DEBUG, "Sending message:\n");
	report_call(call, LOG_DEBUG, "->  message type: %s (%d)\n",
		q931_message_type_to_text(mt), mt);

	msg->rawlen += q931_prepare_header(call, msg->raw, mt);

	if (user_ies)
		q931_write_ies(msg, user_ies);

	return q931_send_frame_bc(dlc, msg->raw, msg->rawlen);
}

