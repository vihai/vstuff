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
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <assert.h>
#include <stdarg.h>

#include <lapd.h>

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/list.h>
#include <libq931/logging.h>
#include <libq931/msgtype.h>
#include <libq931/ie.h>
#include <libq931/out.h>
#include <libq931/global.h>
#include <libq931/ces.h>
#include <libq931/call.h>
#include <libq931/intf.h>
#include <libq931/proto.h>

#include <libq931/ie_cause.h>

#include "call_inline.h"

void q931_default_report(int level, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
}

struct q931_lib *q931_init()
{
	struct q931_lib *lib;

	lib = malloc(sizeof(*lib));
	if (!lib)
		return NULL;

	lib->report = q931_default_report;

	// Non-reentrant, FIXME
	q931_ie_types_init();
	q931_message_types_init();

	INIT_LIST_HEAD(&lib->timers);
	INIT_LIST_HEAD(&lib->intfs);

	return lib;
}

void q931_leave(struct q931_lib *lib)
{
	free(lib);
}

void q931_dl_establish_confirm(struct q931_dlc *dlc)
{
	report_dlc(dlc, LOG_DEBUG, "DL-ESTABLISH-CONFIRM\n");

	dlc->status = DLC_CONNECTED;

	struct q931_call *call, *callt;
	list_for_each_entry_safe(call, callt, &dlc->intf->calls, calls_node) {
		struct q931_ces *ces, *cest;
		list_for_each_entry_safe(ces, cest, &call->ces, node) {

			if (ces->dlc == dlc) {
				if (ces == call->selected_ces) {
					q931_ces_dl_establish_confirm(ces);

					break;
				} else {
					q931_ces_dl_establish_confirm(ces);

					return;
				}
			}
		}

		if (call->dlc == dlc)
			q931_call_dl_establish_confirm(call);
	}
}

void q931_dl_establish_indication(struct q931_dlc *dlc)
{
	report_dlc(dlc, LOG_DEBUG, "DL-ESTABLISH-INDICATION\n");

	dlc->status = DLC_CONNECTED;

	struct q931_call *call, *callt;
	list_for_each_entry_safe(call, callt, &dlc->intf->calls, calls_node) {
		struct q931_ces *ces, *cest;
		list_for_each_entry_safe(ces, cest, &call->ces, node) {

			if (ces->dlc == dlc) {
				if (ces == call->selected_ces) {
					q931_ces_dl_establish_indication(ces);

					break;
				} else {
					q931_ces_dl_establish_indication(ces);

					return;
				}
			}
		}

		if (call->dlc == dlc)
			q931_call_dl_establish_indication(call);
	}
}

void q931_dl_release_confirm(struct q931_dlc *dlc)
{
	report_dlc(dlc, LOG_DEBUG, "DL-RELEASE-CONFIRM\n");

	dlc->status = DLC_DISCONNECTED;

	struct q931_call *call, *callt;
	list_for_each_entry_safe(call, callt, &dlc->intf->calls, calls_node) {
		struct q931_ces *ces, *cest;
		list_for_each_entry_safe(ces, cest, &call->ces, node) {

			if (ces->dlc == dlc) {
				if (ces == call->selected_ces) {
					q931_ces_dl_release_confirm(ces);

					break;
				} else {
					q931_ces_dl_release_confirm(ces);

					return;
				}
			}
		}

		if (call->dlc == dlc)
			q931_call_dl_release_confirm(call);
	}
}

void q931_dl_release_indication(struct q931_dlc *dlc)
{
	report_dlc(dlc, LOG_DEBUG, "DL-RELEASE-INDICATION\n");

	dlc->status = DLC_DISCONNECTED;

	struct q931_call *call, *callt;
	list_for_each_entry_safe(call, callt, &dlc->intf->calls, calls_node) {
		struct q931_ces *ces, *cest;
		list_for_each_entry_safe(ces, cest, &call->ces, node) {

			if (ces->dlc == dlc) {
				if (ces == call->selected_ces) {
					q931_ces_dl_release_indication(ces);

					break;
				} else {
					q931_ces_dl_release_indication(ces);

					return;
				}
			}
		}

		if (call->dlc == dlc)
			q931_call_dl_release_indication(call);
	}
}

static int ie_has_content_errors(
	const struct q931_message *msg,
	const struct q931_ie_type *ie_type,
	int ie_len)
{
	assert(msg);
	assert(ie_type);

	if (ie_type->max_len >= 0 &&
	    ie_len > ie_type->max_len) {

		report_msg(msg, LOG_DEBUG,
			"IE bigger than maximum "
			" specified len (%d > %d)\n",
			ie_len,
			ie_type->max_len);

		if (0) { // Access IE??! 5.8.7.2
		} else if (0) { // Call identity???
		} else {
		}

	}

	return FALSE;
}

struct q931_decode_status
{
	int rawies_curpos;
	int curie;

	int codeset;
	int codeset_locked;

	__u8 ie_id;

	int invalid_mand_ies_cnt;
	__u8 invalid_mand_ies[32];

	int invalid_opt_ies_cnt;
	__u8 invalid_opt_ies[32];

	int unrecognized_ies_cnt;
	__u8 unrecognized_ies[32];
};

void q931_decode_so_ie(
	const struct q931_call *call,
	struct q931_message *msg,
	struct q931_decode_status *ds)
{
	if (ds->codeset == 0) {
		const struct q931_ie_type *ie_type =
			q931_get_ie_type(ds->ie_id);

		if (ie_type) {
			if (ie_type->alloc) {
				struct q931_ie *ie;
				ie = ie_type->alloc();

				if (ie->type->read_from_buf(ie, msg,
						ds->rawies_curpos, 1))
					q931_ies_add(&msg->ies, ie);

				if (ie->type->dump)
					ie->type->dump(ie,
						call->intf->lib->report, "  ");

				report_msg(msg, LOG_DEBUG,
					"SO IE %d ===> %u (%s)\n",
					ds->curie,
					ds->ie_id,
					ie_type->name);

				q931_ie_put(ie);
			}
		} else {
			report_msg(msg, LOG_DEBUG,
				"SO IE %d ===> %u (unknown)\n",
				ds->curie,
				ds->ie_id);
		}
	}
}

void q931_decode_vl_ie(
	const struct q931_call *call,
	struct q931_message *msg,
	struct q931_decode_status *ds)
{
	int ie_len = *(__u8 *)(msg->rawies + ds->rawies_curpos);
	ds->rawies_curpos++;

	if (ds->codeset == 0) {
		// Check out-of-sequence
		// Check duplicated IEs
		// Check missing mandatory IE

		const struct q931_ie_type *ie_type =
			q931_get_ie_type(ds->ie_id);

		if (!ie_type) {
			if (q931_ie_comprehension_required(ds->ie_id)) {
				ds->unrecognized_ies[ds->unrecognized_ies_cnt++] =
					ds->ie_id;

				report_msg(msg, LOG_DEBUG,
					"Unrecognized IE %d in message"
					" for which comprehension is required\n",
					ds->ie_id);
			}

			goto skip_this_ie;
		}

		const struct q931_ie_type_per_mt *ie_type2 =
			q931_get_ie_type_per_mt(
				msg->message_type, ds->ie_id);

		if (!ie_type2) {
			report_msg(msg, LOG_DEBUG,
				"Unexpected IE %d in message type %s\n",
				ds->ie_id,
				q931_message_type_to_text(
					msg->message_type));

			goto skip_this_ie;
		}

		if (!ie_has_content_errors(msg, ie_type, ie_len)) {
			report_msg(msg, LOG_DEBUG,
				"VL IE %d ===> %u (%s) -- length %u\n",
				ds->curie,
				ds->ie_id,
				ie_type->name,
				ie_len);

			if (ie_type->alloc) {
				struct q931_ie *ie;
				ie = ie_type->alloc();

				if (ie->type->read_from_buf(ie, msg,
						ds->rawies_curpos, ie_len))
					q931_ies_add_put(&msg->ies, ie);

				if (ie->type->dump)
					ie->type->dump(ie,
						call->intf->lib->report, "  ");
			}
		} else {
			// If mandatory or comprension required
			if (q931_ie_comprehension_required(ds->ie_id)) {
				if (ds->invalid_mand_ies_cnt <
				    sizeof(ds->invalid_mand_ies)/
				    sizeof(*ds->invalid_mand_ies)) {
					ds->invalid_mand_ies[ds->invalid_mand_ies_cnt]
						= ds->ie_id;

					ds->invalid_mand_ies_cnt++;
				}
			} else {
				if (ds->invalid_opt_ies_cnt <
				    sizeof(ds->invalid_opt_ies)/
				    sizeof(*ds->invalid_opt_ies)) {
					ds->invalid_opt_ies[ds->invalid_opt_ies_cnt]
						= ds->ie_id;

					ds->invalid_opt_ies_cnt++;
				}
			}
		}
	}

skip_this_ie:

	ds->rawies_curpos += ie_len;

	if(ds->rawies_curpos > msg->rawies_len) {

		report_msg(msg, LOG_ERR, "MALFORMED FRAME\n");
		// FIXME
		return;
	}
}

void q931_decode_shift_ie(
	const struct q931_call *call,
	struct q931_message *msg,
	struct q931_decode_status *ds)
{
	if (q931_get_so_ie_type2_value(ds->ie_id) & 0x08) {
		// Locking shift

		report_dlc(msg->dlc, LOG_DEBUG,
			"Locked Switch from codeset %u to codeset %u\n",
			ds->codeset,
			q931_get_so_ie_type2_value(ds->ie_id) & 0x07);

		ds->codeset = q931_get_so_ie_type2_value(ds->ie_id) & 0x07;
		ds->codeset_locked = FALSE;
	} else {
		// Non-Locking shift

		report_dlc(msg->dlc, LOG_DEBUG,
			"Non-Locked Switch from codeset %u to codeset %u\n",
			ds->codeset,
			q931_get_so_ie_type2_value(ds->ie_id));

		ds->codeset = q931_get_so_ie_type2_value(ds->ie_id);
		ds->codeset_locked = TRUE;
	}
}

int q931_decode_information_elements(
	struct q931_call *call,
	struct q931_message *msg)
{
	struct q931_decode_status ds;
	memset(&ds, 0x00, sizeof(ds));

	ds.rawies_curpos = 0;
	ds.codeset = 0;
	ds.codeset_locked = FALSE;

	while(ds.rawies_curpos < msg->rawies_len) {
		ds.ie_id = *(__u8 *)(msg->rawies + ds.rawies_curpos);
		ds.rawies_curpos++;

		if (q931_is_so_ie(ds.ie_id)) {
			if (q931_get_so_ie_id(ds.ie_id) == Q931_IE_SHIFT) {
				q931_decode_shift_ie(call, msg, &ds);

				continue;
			}

			q931_decode_so_ie(call, msg, &ds);
		} else {
			q931_decode_vl_ie(call, msg, &ds);
		}

		if (!ds.codeset_locked)
			ds.codeset = 0;

		ds.curie++;
	}

	 // TODO: Uhm... we should do some validity check for the global call too
	if (!call)
		return TRUE;

//	if (mandatory ies missing)
	if (ds.invalid_mand_ies_cnt) {
		switch(msg->message_type) {
		case Q931_MT_SETUP:
		case Q931_MT_RELEASE: {
			struct q931_ies ies = Q931_IES_INIT;

			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = Q931_IE_C_L_PRIVATE_NETWORK_SERVING_REMOTE_USER;
			cause->value = Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS;
			memcpy(cause->diagnostics, ds.invalid_mand_ies,
				ds.invalid_mand_ies_cnt);
			cause->diagnostics_len = ds.invalid_mand_ies_cnt;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_release_complete(call, &ies);

			return FALSE;
		}
		break;

		case Q931_MT_DISCONNECT: {
			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = Q931_IE_C_L_PRIVATE_NETWORK_SERVING_REMOTE_USER;
			cause->value = Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS;
			memcpy(cause->diagnostics, ds.invalid_mand_ies,
				ds.invalid_mand_ies_cnt);
			cause->diagnostics_len = ds.invalid_mand_ies_cnt;
			// Reset release with cause?
			q931_ies_add_put(&call->release_with_cause, &cause->ie);
		}
		break;

		case Q931_MT_RELEASE_COMPLETE:
			// Ignore content errors in RELEASE_COMPLETE
		break;

		default: {
			struct q931_ies ies = Q931_IES_INIT;
			struct q931_ie_cause *cause;
			cause = q931_ie_cause_alloc();

			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = Q931_IE_C_L_PRIVATE_NETWORK_SERVING_REMOTE_USER;
			cause->value = Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS;
			memcpy(cause->diagnostics, ds.invalid_mand_ies,
				ds.invalid_mand_ies_cnt);
			cause->diagnostics_len = ds.invalid_mand_ies_cnt;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_status(call, &ies);

			return FALSE;
		}
		break;
		}
	} else if (ds.unrecognized_ies_cnt) {
		switch(msg->message_type) {
		case Q931_MT_DISCONNECT: {
			struct q931_ie_cause *cause;
			cause = q931_ie_cause_alloc();

			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = Q931_IE_C_L_PRIVATE_NETWORK_SERVING_REMOTE_USER;
			cause->value = Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS;
			memcpy(cause->diagnostics, ds.unrecognized_ies,
				ds.unrecognized_ies_cnt);
			cause->diagnostics_len = ds.unrecognized_ies_cnt;
			// Reset release with cause?
			q931_ies_add_put(&call->release_with_cause, &cause->ie);
		}
		break;

		case Q931_MT_RELEASE: {
			struct q931_ies ies = Q931_IES_INIT;
			struct q931_ie_cause *cause;
			cause = q931_ie_cause_alloc();

			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = Q931_IE_C_L_PRIVATE_NETWORK_SERVING_REMOTE_USER;
			cause->value = Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS;
			memcpy(cause->diagnostics, ds.unrecognized_ies,
				ds.unrecognized_ies_cnt);
			cause->diagnostics_len = ds.unrecognized_ies_cnt;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_release_complete(call, &ies);

			return FALSE;
		}
		break;

		case Q931_MT_RELEASE_COMPLETE:
			// Ignore content errors in RELEASE_COMPLETE
		break;

		default: {
			struct q931_ies ies = Q931_IES_INIT;
			struct q931_ie_cause *cause;
			cause = q931_ie_cause_alloc();

			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = Q931_IE_C_L_PRIVATE_NETWORK_SERVING_REMOTE_USER;
			cause->value = Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS;
			memcpy(cause->diagnostics, ds.unrecognized_ies,
				ds.unrecognized_ies_cnt);
			cause->diagnostics_len = ds.unrecognized_ies_cnt;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_status(call, &ies);

			return FALSE;
		}
		break;
		}
	} else if (ds.invalid_opt_ies_cnt) {
		switch(msg->message_type) {
		case Q931_MT_DISCONNECT: {
			struct q931_ie_cause *cause;
			cause = q931_ie_cause_alloc();

			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = Q931_IE_C_L_PRIVATE_NETWORK_SERVING_REMOTE_USER;
			cause->value = Q931_IE_C_CV_INFORMATION_ELEMENT_NON_EXISTENT;
			memcpy(cause->diagnostics, ds.unrecognized_ies,
				ds.unrecognized_ies_cnt);
			cause->diagnostics_len = ds.unrecognized_ies_cnt;
			// Reset release with cause?
			q931_ies_add_put(&call->release_with_cause, &cause->ie);
		}
		break;

		case Q931_MT_RELEASE: {

			return FALSE;
		}
		break;

		case Q931_MT_RELEASE_COMPLETE:
			// Ignore content errors in RELEASE_COMPLETE
		break;

		default: {
			struct q931_ies ies = Q931_IES_INIT;
			struct q931_ie_cause *cause;
			cause = q931_ie_cause_alloc();

			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = Q931_IE_C_L_PRIVATE_NETWORK_SERVING_REMOTE_USER;
			cause->value = Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS;
			memcpy(cause->diagnostics, ds.invalid_opt_ies,
				ds.invalid_opt_ies_cnt);
			cause->diagnostics_len = ds.invalid_opt_ies_cnt;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_status(call, &ies);

			return FALSE;
		}
		break;
		}
	}

	return TRUE;
}

struct q931_dlc *q931_accept(
	struct q931_interface *intf,
	int accept_socket)
{
	struct q931_dlc *dlc;
	dlc = malloc(sizeof(*dlc));
	if (!dlc)
		goto err_malloc;

	int socket = accept(accept_socket, NULL, 0);
	if (dlc->socket < 0)
		goto err_accept;

	q931_dlc_init(dlc, intf, socket);

	int optlen = sizeof(dlc->tei);
	if (getsockopt(dlc->socket, SOL_LAPD, LAPD_TEI,
		&dlc->tei, &optlen) < 0) {
		report_intf(intf, LOG_ERR,
			"getsockopt: %s\n", strerror(errno));
		goto err_getsockopt;
	}

	if (intf->flags & Q931_INTF_FLAGS_DEBUG) {
		int on = 1;

		if (setsockopt(dlc->socket, SOL_SOCKET, SO_DEBUG,
						&on, sizeof(on)) < 0)
			report_intf(intf, LOG_ERR,
				"setsockopt: %s\n", strerror(errno));
	}

	list_add_tail(&dlc->intf_node, &intf->dlcs);

	return dlc;

err_getsockopt:
	close(dlc->socket);
err_accept:
	free(dlc);
err_malloc:

	return NULL;
}

int q931_receive(struct q931_dlc *dlc)
{
	struct q931_message msg;
	memset(&msg, 0x00, sizeof(msg));

	struct msghdr skmsg;
	struct sockaddr_lapd sal;
	struct cmsghdr cmsg;
	struct iovec iov;

	iov.iov_base = msg.raw;
	iov.iov_len = sizeof(msg.raw);

	skmsg.msg_name = &sal;
	skmsg.msg_namelen = sizeof(sal);
	skmsg.msg_iov = &iov;
	skmsg.msg_iovlen = 1;
	skmsg.msg_control = &cmsg;
	skmsg.msg_controllen = sizeof(cmsg);
	skmsg.msg_flags = 0;

	msg.dlc = dlc;
	msg.rawlen = recvmsg(dlc->socket, &skmsg, 0);
	if(msg.rawlen < 0) {
		if (errno == ECONNRESET) {
			q931_dl_release_indication(dlc);
		} else if (errno == EALREADY) {
			q931_dl_establish_indication(dlc);
		} else if (errno == ENOTCONN) {
			q931_dl_release_confirm(dlc);
		} else if (errno == EISCONN) {
			q931_dl_establish_confirm(dlc);
		} else {
			report_dlc(dlc, LOG_ERR, "recvmsg error: %s\n",
				strerror(errno));

			list_del(&dlc->intf_node);

			return Q931_RECEIVE_REFRESH;
		}

		return Q931_RECEIVE_OK;
	}

	if (msg.rawlen < sizeof(struct q931_header)) {
		report_dlc(dlc, LOG_DEBUG,
			"Message to small (%d bytes), ignoring\n",
			msg.rawlen);

		return -EBADMSG;
	}

	struct q931_header *hdr = (struct q931_header *)msg.raw;

	if (hdr->protocol_discriminator != Q931_PROTOCOL_DISCRIMINATOR_Q931) {
		report_dlc(dlc, LOG_DEBUG,
			"Protocol discriminator %u not supported,"
			" ignoring message\n",
			hdr->protocol_discriminator);

		return -EBADMSG;
	}

	if (hdr->spare1 != 0) {
		report_dlc(dlc, LOG_DEBUG,
			"Call reference size invalid, ignoring frame\n");

		return -EBADMSG;
	}

	if (hdr->call_reference_len > 4) {
		report_dlc(dlc, LOG_DEBUG,
			"Call reference length of %u bytes is too big"
			" and not supported (max 4), ignoring frame\n",
			hdr->call_reference_len);

		return -EBADMSG;
	}

	if (hdr->call_reference_len == 1 &&
	    hdr->call_reference[0] == 0) {
		report_dlc(dlc, LOG_DEBUG,
			"The dummy call reference is not supported,"
			" ignoring frame\n");

		return -EBADMSG;
	}

	msg.callref = 0;
	msg.callref_direction = Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE;
	msg.callref_len = hdr->call_reference_len;

	int i;
	for(i=0; i<hdr->call_reference_len; i++) {

		__u8 val = hdr->call_reference[i];

		if (i==0 && val & 0x80) {
			val &= 0x7f;

			msg.callref_direction =
				Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE;
		}

#if __BYTE_ORDER == __LITTLE_ENDIAN
		msg.callref |= val << ((hdr->call_reference_len-i-1) * 8);
#else
		msg.callref |= val << (hdr->i * 8);
#endif
	}

	report_dlc(dlc, LOG_DEBUG, "Received message:\n");

	report_dlc(dlc, LOG_DEBUG,
		"  call reference = %u %lu %c\n",
		hdr->call_reference_len,
		msg.callref,
		msg.callref_direction?'O':'I');

	msg.message_type = *(__u8 *)(msg.raw + sizeof(struct q931_header) +
		hdr->call_reference_len);

	report_dlc(dlc, LOG_DEBUG, "  message_type = %s (%u)\n",
		q931_message_type_to_text(msg.message_type),
		msg.message_type);

	msg.rawies = msg.raw + sizeof(struct q931_header) + msg.callref_len + 1;
	msg.rawies_len = msg.rawlen - (sizeof(struct q931_header) + msg.callref_len + 1);

	if (msg.callref == 0x00) { // FIXME CHECKME
		if (q931_decode_information_elements(NULL, &msg))
			q931_dispatch_global_message(
				&dlc->intf->global_call, &msg);

		return -EBADMSG;
	}

	struct q931_call *call =
		q931_get_call_by_reference(
			dlc->intf,
			msg.callref_direction ==
				Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE
				? Q931_CALL_DIRECTION_INBOUND
				: Q931_CALL_DIRECTION_OUTBOUND,
			msg.callref);

	if (!call) {

		call = q931_call_alloc_in(
			dlc->intf, dlc,
			msg.callref,
			skmsg.msg_flags & MSG_OOB);
		if (!call) {
			report_dlc(dlc, LOG_ERR,
				"Error allocating call\n");

			return -EFAULT;
		}

		switch (msg.message_type) {
		case Q931_MT_RELEASE:
			report_call(call, LOG_DEBUG,
				"Received a RELEASE for an unknown callref\n");

			struct q931_ies ies = Q931_IES_INIT;
			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = Q931_IE_C_L_PRIVATE_NETWORK_SERVING_REMOTE_USER;
			cause->value = Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_release_complete(call, &ies);

			q931_call_release_reference(call);
			q931_call_put(call);

			return Q931_RECEIVE_OK;
		break;

		case Q931_MT_RELEASE_COMPLETE:
			report_call(call, LOG_DEBUG,
				"Received a RELEASE COMPLETE for an unknown"
				" callref, ignoring frame\n");

			q931_call_release_reference(call);
			q931_call_put(call);

			return Q931_RECEIVE_OK;
		break;

		case Q931_MT_SETUP:
		case Q931_MT_RESUME:
			if (msg.callref_direction ==
			      Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE) {

				report_call(call, LOG_DEBUG,
					"Received a SETUP/RESUME for an unknown"
					" outbound callref, ignoring frame\n");

				q931_call_release_reference(call);
				q931_call_put(call);

				return Q931_RECEIVE_OK;
			}
		break;

		case Q931_MT_STATUS:
			// Pass it to the handler
		break;

		default: {
			struct q931_ies ies = Q931_IES_INIT;
			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = Q931_IE_C_L_PRIVATE_NETWORK_SERVING_REMOTE_USER;
			cause->value = Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_release(call, &ies);

			q931_call_start_timer(call, T308);

			if (call->state == N0_NULL_STATE)
				q931_call_set_state(call, U19_RELEASE_REQUEST);
			else
				q931_call_set_state(call, N19_RELEASE_REQUEST);

			q931_call_release_reference(call);
			q931_call_put(call);

			return Q931_RECEIVE_OK;
		}
		break;
		}
	}

	if (q931_decode_information_elements(call, &msg)) {
		struct q931_ces *ces, *tces;
		list_for_each_entry_safe(ces, tces, &call->ces, node) {

			if (ces->dlc == dlc) {
				// selected_ces may change after "dispatch_message"
				if (ces == call->selected_ces) {
					q931_ces_dispatch_message(ces, &msg);

					break;
				} else {
					q931_ces_dispatch_message(ces, &msg);
					q931_call_put(call);

					return Q931_RECEIVE_OK;
				}
			}
		}

		q931_dispatch_message(call, &msg);
	}

	q931_call_put(call);

	return Q931_RECEIVE_OK;
}
