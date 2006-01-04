/*
 * vISDN DSSS-1/q.931 signalling library
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
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
		Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE);

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
		if (errno == ECONNRESET) {
			q931_dl_release_indication(dlc);
		} else if (errno == EALREADY) {
			q931_dl_establish_indication(dlc);
		} else if (errno == ENOTCONN) {
			q931_dl_release_confirm(dlc);
		} else if (errno == EISCONN) {
			q931_dl_establish_confirm(dlc);
		} else {
			report_dlc(dlc, LOG_ERR, "sendmsg error: %s\n",
				strerror(errno));
		}

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

	struct q931_message *msg;
	msg = malloc(sizeof(*msg));
	if (!msg)
		return -EFAULT;

	q931_message_init(msg, dlc);

	report_call(call, LOG_DEBUG, "Sending message:\n");
	report_msg_cont(msg, LOG_DEBUG, "->  message type: %s (%d)\n",
		q931_message_type_to_text(mt), mt);

	msg->rawlen += q931_prepare_header(call, msg->raw, mt);

	if (user_ies) {
		struct q931_ies ies = Q931_IES_INIT;
		q931_ies_copy(&ies, user_ies);
		q931_ies_sort(&ies);

		int i;
		for (i=0; i<ies.count; i++) {
			assert(ies.ies[i]->type->write_to_buf);

			int ie_len = ies.ies[i]->type->write_to_buf(ies.ies[i],
					msg->raw + msg->rawlen,
					sizeof(msg->raw) - msg->rawlen);
			msg->rawlen += ie_len;

			report_msg_cont(msg, LOG_DEBUG,
				"->  IE %d ===> %u (%s) -- length %u\n",
				i,
				ies.ies[i]->type->id,
				ies.ies[i]->type->name,
				ie_len);

			if (ies.ies[i]->type->dump)
				ies.ies[i]->type->dump(
					ies.ies[i],
					call->intf->lib->report, "->    ");
		}
	}

	report_msg_cont(msg, LOG_DEBUG, "\n");

	// AWAITING_DISCONNECTION ??

	if (dlc->status == Q931_DLC_STATUS_DISCONNECTED) {
		if (connect(dlc->socket, NULL, 0) < 0) {
			if (errno != EAGAIN) {
				report_dlc(dlc, LOG_ERR, "connect: %s\n",
					strerror(errno));
				return errno;
			}
		}

		dlc->status = Q931_DLC_STATUS_AWAITING_CONNECTION;
	}

	int res = 0;
	if (dlc->status == Q931_DLC_STATUS_AWAITING_CONNECTION) {
		report_dlc(dlc, LOG_DEBUG,
			"DLC is awaiting connection: message queued\n");

		list_add_tail(
			&q931_message_get(msg)->outgoing_queue_node,
			&dlc->outgoing_queue);
	} else {
		res = q931_send_frame(dlc, msg->raw, msg->rawlen);
	}

	q931_message_put(msg);

	return res;
}

int q931_send_message_bc(
	struct q931_call *call,
	struct q931_broadcast_dlc *bc_dlc,
	enum q931_message_type mt,
	const struct q931_ies *user_ies)
{
	assert(call);
	assert(bc_dlc);

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
			
			report_lib(call->intf->lib, LOG_DEBUG,
				"->  IE %d ===> %u (%s) -- length %u\n",
				i,
				ies.ies[i]->type->id,
				ies.ies[i]->type->name,
				ie_len);

			if (ies.ies[i]->type->dump)
				ies.ies[i]->type->dump(
					ies.ies[i],
					call->intf->lib->report, "->    ");
		}
	}

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

	iov.iov_base = buf;
	iov.iov_len = size;

	if (sendmsg(bc_dlc->socket, &msg, MSG_OOB) < 0) {
		report_call(call, LOG_ERR, "sendmsg error: %s\n",
			strerror(errno));

		return errno;
	}

	return 0;
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

			int ie_len = ies.ies[i]->type->write_to_buf(ies.ies[i],
					buf + size,
					sizeof(buf) - size);

			size += ie_len;

			report_lib(gc->intf->lib, LOG_DEBUG,
				"->  IE %d ===> %u (%s) -- length %u\n",
				i,
				ies.ies[i]->type->id,
				ies.ies[i]->type->name,
				ie_len);

			if (ies.ies[i]->type->dump)
				ies.ies[i]->type->dump(
					ies.ies[i],
					gc->intf->lib->report, "->    ");
		}
	}

	return q931_send_frame(dlc, buf, size);
}
