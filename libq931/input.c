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
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <assert.h>
#include <stdarg.h>

#include <linux/lapd.h>

#define Q931_PRIVATE

#include <libq931/lib.h>
#include <libq931/list.h>
#include <libq931/logging.h>
#include <libq931/msgtype.h>
#include <libq931/ie.h>
#include <libq931/output.h>
#include <libq931/input.h>
#include <libq931/global.h>
#include <libq931/ces.h>
#include <libq931/call.h>
#include <libq931/intf.h>
#include <libq931/proto.h>

#include <libq931/ie_cause.h>
#include <libq931/ie_channel_identification.h>

#include "call_inline.h"

void q931_dl_establish_confirm(struct q931_dlc *dlc)
{
	report_dlc(dlc, LOG_DEBUG, "DL-ESTABLISH-CONFIRM\n");

	dlc->status = Q931_DLC_STATUS_CONNECTED;

	struct q931_call *call, *callt;
	list_for_each_entry_safe(call, callt, &dlc->intf->calls, calls_node) {

		struct q931_ces *ces, *cest;
		list_for_each_entry_safe(ces, cest, &call->ces, node) {

			if (ces->dlc == dlc)
				q931_ces_dl_establish_confirm(ces);
		}

		if (call->dlc == dlc)
			q931_call_dl_establish_confirm(call);
	}

	q931_flush_outgoing_queue(dlc);

	/* Force autorelease timer to start */
	q931_dlc_get(dlc);
	q931_dlc_put(dlc);
}

void q931_dl_establish_indication(struct q931_dlc *dlc)
{
	report_dlc(dlc, LOG_DEBUG, "DL-ESTABLISH-INDICATION\n");

	dlc->status = Q931_DLC_STATUS_CONNECTED;

	/* Force autorelease timer to start */
	q931_dlc_get(dlc);
	q931_dlc_put(dlc);

	struct q931_call *call, *callt;
	list_for_each_entry_safe(call, callt, &dlc->intf->calls, calls_node) {

		struct q931_ces *ces, *cest;
		list_for_each_entry_safe(ces, cest, &call->ces, node) {

			if (ces->dlc == dlc)
				q931_ces_dl_establish_indication(ces);
		}

		if (call->dlc == dlc)
			q931_call_dl_establish_indication(call);
	}

	q931_flush_outgoing_queue(dlc);
}

void q931_dl_release_confirm(struct q931_dlc *dlc)
{
	report_dlc(dlc, LOG_DEBUG, "DL-RELEASE-CONFIRM\n");

	dlc->status = Q931_DLC_STATUS_DISCONNECTED;

	struct q931_call *call, *callt;
	list_for_each_entry_safe(call, callt, &dlc->intf->calls, calls_node) {

		struct q931_ces *ces, *cest;
		list_for_each_entry_safe(ces, cest, &call->ces, node) {

			if (ces->dlc == dlc)
				q931_ces_dl_release_confirm(ces);
		}

		if (call->dlc == dlc)
			q931_call_dl_release_confirm(call);
	}
}

void q931_dl_release_indication(struct q931_dlc *dlc)
{
	report_dlc(dlc, LOG_DEBUG, "DL-RELEASE-INDICATION\n");

	dlc->status = Q931_DLC_STATUS_DISCONNECTED;

	struct q931_call *call, *callt;
	list_for_each_entry_safe(call, callt, &dlc->intf->calls, calls_node) {

		struct q931_ces *ces, *cest;
		list_for_each_entry_safe(ces, cest, &call->ces, node) {

			if (ces->dlc == dlc)
				q931_ces_dl_release_indication(ces);
		}

		if (call->dlc == dlc)
			q931_call_dl_release_indication(call);
	}
}

static int q931_check_ie_size(
	const struct q931_message *msg,
	const struct q931_ie_class *ie_class,
	int ie_len)
{
	assert(msg);
	assert(ie_class);

	if (ie_class->max_len != INT_MAX &&
	    ie_len > ie_class->max_len) {

		report_msg_cont(msg, LOG_DEBUG,
			"IE bigger than maximum specified len (%d > %d)\n",
			ie_len,
			ie_class->max_len);

		return -1;
	}

	return 0;
}

struct q931_ieid_entry
{
	struct list_head node;

	__u8 codeset;
	__u8 id;
};

static void q931_ieid_add_to_list(
	const struct q931_message *msg,
	struct list_head *list,
	__u8 codeset,
	__u8 id)
{
	struct q931_ieid_entry *entry = malloc(sizeof(*entry));

	entry->codeset = codeset;
	entry->id = id;

	list_add_tail(&entry->node, list);
}

static void q931_ieid_flush_list(struct list_head *list)
{
	struct q931_ieid_entry *entry, *tpos;
	list_for_each_entry_safe(entry, tpos, list, node) {
		list_del(&entry->node);
		free(entry);
	}
}

static void q931_ieid_fill_diagnostic_from_list(
	__u8 *diagnostics,
	int *diagnostics_len,
       struct list_head *list)
{
	int max_len = *diagnostics_len;

	*diagnostics_len = 0;

	struct q931_ieid_entry *entry;
	list_for_each_entry(entry, list, node) {

		if (*diagnostics_len >= max_len)
			return;
		
		if (entry->codeset != 0) {
			/* Do a non-locking shift */
			
			diagnostics[*diagnostics_len] =
			       Q931_IE_SHIFT | 0x08 | entry->codeset;
			*diagnostics_len++;
		}

		if (*diagnostics_len >= max_len)
			return;

		diagnostics[*diagnostics_len] = entry->id;
		*diagnostics_len++;
	}
}

struct q931_decoder_status
{
	int curie;

	__u8 active_codeset;
	int previous_ie_id[8];

	struct list_head mandatory_ies;

	struct list_head missing_mand_ies;
	struct list_head invalid_mand_ies;
	struct list_head invalid_opt_ies;
	struct list_head invalid_access_ies;
	struct list_head unrecognized_ies;
};

static void q931_ie_has_content_errors(
	const struct q931_message *msg,
	struct q931_decoder_status *ds,
	const struct q931_ie_class *ie_class,
	const struct q931_ie_usage *ie_usage)
{
	if (q931_ie_comprehension_required(ie_class->id) ||
	    ie_usage->presence == Q931_IE_MANDATORY) {

		q931_ieid_add_to_list(msg, &ds->invalid_mand_ies,
			ie_class->codeset, ie_class->id);
	} else {

		q931_ieid_add_to_list(msg, &ds->invalid_opt_ies,
			ie_class->codeset, ie_class->id);
	}
}

static void q931_decode_ie(
	const struct q931_call *call,
	struct q931_message *msg,
	struct q931_decoder_status *ds,
	__u8 codeset,
	__u8 ie_id,
	__u8 *buf,
	__u8 len,
	BOOL is_vl)
{
	const struct q931_ie_class *ie_class;

	ie_class = q931_get_ie_class(codeset, ie_id);
	if (!ie_class) {
		if (q931_ie_comprehension_required(ie_id)) {

			q931_ieid_add_to_list(msg, &ds->unrecognized_ies,
				ie_class->codeset, ie_class->id);

			report_msg(msg, LOG_DEBUG,
				"!! Unrecognized IE %d:%d in message"
				" for which comprehension is required\n",
				codeset, ie_id);
		}

		return;
	}

	report_msg_cont(msg, LOG_DEBUG,
		"<-  %s IE %d ===> %u (%s) length=(%d)\n",
		is_vl ? "VL" : "SO",
		ds->curie,
		ie_class->id,
		ie_class->name,
		len);

	const struct q931_ie_usage *ie_usage;
	ie_usage = q931_get_ie_usage(msg->message_type, codeset, ie_id);

	if (!ie_usage) {
		report_msg(msg, LOG_NOTICE,
			"!! Unexpected IE %s (%d) in message type %s\n",
			ie_class->name,
			ie_class->id,
			q931_message_type_to_text(
				msg->message_type));

		return;
	}

	// Ignore IEs appearing more than "man_occur" times
	if (ie_class->max_occur > 0 && ie_class->max_occur != INT_MAX) {
		int i;
		int count = 0;
		for (i=0; i<msg->ies.count; i++) {
			if (msg->ies.ies[i]->cls->id == ie_class->id &&
			    msg->ies.ies[i]->cls->codeset == ie_class->codeset)
				count++;
		}

		if (count > ie_class->max_occur)
			return;
	}

	if (!ie_class->alloc ||
	    !ie_class->read_from_buf) {
		if (q931_ie_comprehension_required(ie_id)) {

			q931_ieid_add_to_list(msg, &ds->unrecognized_ies,
				ie_class->codeset, ie_class->id);

			report_msg(msg, LOG_DEBUG,
				"!! Unhandled IE %d:%d in message"
				" for which comprehension is required\n",
				codeset, ie_id);
		}
	
       		return;
	}

	if (q931_check_ie_size(msg, ie_class, len) < 0) {

		if (ie_class->id == Q931_IE_USER_USER ||
		    ie_class->id == Q931_IE_LOW_LAYER_COMPATIBILITY ||
		    ie_class->id == Q931_IE_HIGH_LAYER_COMPATIBILITY ||
		    ie_class->id == Q931_IE_CALLED_PARTY_SUBADDRESS) {

			q931_ieid_add_to_list(msg, &ds->invalid_access_ies,
				ie_class->codeset, ie_class->id);

			return;
		} else if (ie_class->id == Q931_IE_CALL_IDENTITY) {
			/* Truncate it */
			len = ie_class->max_len;
		} else {
			q931_ie_has_content_errors(msg, ds, ie_class, ie_usage);
			return;
		}
	}
	
	struct q931_ie *ie;
       
	ie = ie_class->alloc();

	if (!ie->cls->read_from_buf(ie, buf, len,
			call->intf->lib->report,
			call->intf)) {

		q931_ie_has_content_errors(msg, ds, ie_class, ie_usage);

		int i;
		char hexdump[512];

		for (i=0; i<len; i++)
			sprintf(hexdump + i * 2, "%02x",
				*(buf + i));

		report_msg_cont(msg, LOG_WARNING,
			"IE has content errors (%s)\n",
			hexdump);

		q931_ie_put(ie);

		return;
	}

	if (is_vl) {
		if (ie_class->id < ds->previous_ie_id[ie_class->codeset]) {
			/* This implementation processes out of sequence IEs but
			 * complains nonetheless
			 */

			report_msg_cont(msg,  LOG_INFO,
				"<-  Information element is out of sequence, "
				"processing anyway\n");
		}


		if (ie->cls->dump)
			ie->cls->dump(ie, call->intf->lib->report, "<-    ");


		ds->previous_ie_id[ie_class->codeset] = ie_class->id;
	}

	q931_ies_add_put(&msg->ies, ie);

	return;
}

__u8 q931_decode_shift_ie(
	const struct q931_call *call,
	struct q931_message *msg,
	struct q931_decoder_status *ds,
	__u8 ie_id)
{
	__u8 codeset;

	if (q931_get_so_ie_type2_value(ie_id) & 0x08) {
		// Locking shift

		codeset = q931_get_so_ie_type2_value(ie_id) & 0x07;

		report_msg_cont(msg, LOG_DEBUG,
			"<-  Locked Switch from codeset %u to codeset %u\n",
			ds->active_codeset,
			codeset);

		ds->active_codeset = codeset;
	} else {
		// Non-Locking shift

		codeset = q931_get_so_ie_type2_value(ie_id);

		report_msg_cont(msg, LOG_DEBUG,
			"<-  Non-Locked Switch from codeset %u to codeset %u\n",
			ds->active_codeset);
	}

	assert(codeset < ARRAY_SIZE(ds->previous_ie_id));

	return codeset;
}

static void q931_decoder_status_init(struct q931_decoder_status *ds)
{
	memset(ds, 0, sizeof(*ds));
	
	INIT_LIST_HEAD(&ds->mandatory_ies);

	INIT_LIST_HEAD(&ds->invalid_mand_ies);
	INIT_LIST_HEAD(&ds->invalid_opt_ies);
	INIT_LIST_HEAD(&ds->invalid_access_ies);
	INIT_LIST_HEAD(&ds->unrecognized_ies);
}

static void q931_decoder_status_destroy(struct q931_decoder_status *ds)
{
	q931_ieid_flush_list(&ds->mandatory_ies);

	q931_ieid_flush_list(&ds->invalid_mand_ies);
	q931_ieid_flush_list(&ds->invalid_opt_ies);
	q931_ieid_flush_list(&ds->invalid_access_ies);
	q931_ieid_flush_list(&ds->unrecognized_ies);
}

int q931_decode_information_elements(
	struct q931_call *call,
	struct q931_message *msg)
{
	struct q931_decoder_status ds;
	q931_decoder_status_init(&ds);

	/* Fill the list of mandatory IEs for this message type */
	int i;
	for (i=0; i<q931_ie_usages_cnt; i++) {
		if (q931_ie_usages[i].message_type == msg->message_type) {

			if (q931_ie_usages[i].presence != Q931_IE_MANDATORY)
				continue;
			
			q931_ieid_add_to_list(msg,
				&ds.mandatory_ies,
				q931_ie_usages[i].codeset,
				q931_ie_usages[i].ie_id);
		}
	}

	int rawies_curpos = 0;

	/* Go through the IEs buffer and decode each of them */
	__u8 codeset = ds.active_codeset;
	while(rawies_curpos < msg->rawies_len) {

		__u8 ie_id = *(__u8 *)(msg->rawies + rawies_curpos);

		ds.curie++;

		if (q931_is_so_ie(ie_id)) {
			if (q931_get_so_ie_id(ie_id) == Q931_IE_SHIFT) {
				codeset = q931_decode_shift_ie(
						call, msg, &ds, ie_id);

				rawies_curpos++;

				continue;
			}

			q931_decode_ie(call, msg, &ds, codeset,
				q931_get_so_ie_id(ie_id),
				msg->rawies + rawies_curpos, 1, FALSE);

			rawies_curpos++;
		} else {
			__u8 ie_len = 
				*(__u8 *)(msg->rawies + rawies_curpos + 1);
			
			q931_decode_ie(call, msg, &ds, codeset, ie_id,
				msg->rawies + rawies_curpos + 2,
				ie_len,
				TRUE);

			rawies_curpos += ie_len + 2;

			if(rawies_curpos > msg->rawies_len) {

				report_msg(msg, LOG_ERR,
					"<-  MALFORMED FRAME\n");

				break;
			}
		}

		/* Remove IE from mandatory IEs */
		struct q931_ieid_entry *entry, *tpos;
		list_for_each_entry_safe(entry, tpos, &ds.mandatory_ies, node) {
			if (entry->codeset == codeset && entry->id == ie_id) {
				list_del(&entry->node);
				free(entry);

				break;
			}
		}

		codeset = ds.active_codeset;
	}

	 // TODO: Uhm... we should do some validity check for the global
	 // call too

	if (!call)
		goto process_message;

	if (!list_empty(&ds.mandatory_ies)) {

		struct q931_ieid_entry *entry;
		list_for_each_entry(entry, &ds.mandatory_ies, node) {

			const struct q931_ie_class *ie_class;
			ie_class = q931_get_ie_class(entry->codeset, entry->id);

			assert(ie_class);
			
			report_msg_cont(msg, LOG_INFO,
				"Mandatory IE %d:%d (%s) is missing\n",
				entry->codeset,
				entry->id,
				ie_class->name);
		}
		
		struct q931_ie_cause *cause;
		cause = q931_ie_cause_alloc();

		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value =
			Q931_IE_C_CV_MANDATORY_INFORMATION_ELEMENT_IS_MISSING;

		cause->diagnostics_len = sizeof(cause->diagnostics);
		
		q931_ieid_fill_diagnostic_from_list(
			cause->diagnostics, &cause->diagnostics_len,
			&ds.mandatory_ies);

		struct q931_ies ies = Q931_IES_INIT;
		q931_ies_add_put(&ies, &cause->ie);

		switch(msg->message_type) {
		case Q931_MT_SETUP:
		case Q931_MT_RELEASE:
			q931_call_send_release_complete(call, &ies);

			goto do_not_process_message;
		break;

		case Q931_MT_DISCONNECT:
			q931_ies_add_put(&call->release_with_cause, &cause->ie);
		break;

		case Q931_MT_RELEASE_COMPLETE:
			/* Do nothing and let message pass assuming it was ok */
		break;

		default:
			q931_call_send_status(call, &ies);

			goto do_not_process_message;
		break;
		}
	}
       
	if (!list_empty(&ds.invalid_mand_ies)) {

		struct q931_ie_cause *cause;
		cause = q931_ie_cause_alloc();

		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value =
			Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS;

		cause->diagnostics_len = sizeof(cause->diagnostics);
		
		q931_ieid_fill_diagnostic_from_list(
			cause->diagnostics, &cause->diagnostics_len,
			&ds.invalid_mand_ies);

		struct q931_ies ies = Q931_IES_INIT;
		q931_ies_add_put(&ies, &cause->ie);

		switch(msg->message_type) {
		case Q931_MT_SETUP:
		case Q931_MT_RELEASE:
			q931_call_send_release_complete(call, &ies);

			goto do_not_process_message;
		break;

		case Q931_MT_DISCONNECT:
			q931_ies_add_put(&call->release_with_cause, &cause->ie);
		break;

		case Q931_MT_RELEASE_COMPLETE:
			/* Do nothing and let message pass assuming it was ok */
		break;

		default:
			q931_call_send_status(call, &ies);

			goto do_not_process_message;
		break;
		}
	}
       
	if (!list_empty(&ds.invalid_access_ies)) {
		struct q931_ies ies = Q931_IES_INIT;
		struct q931_ie_cause *cause;
		cause = q931_ie_cause_alloc();

		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value = Q931_IE_C_CV_ACCESS_INFORMATION_DISCARDED;

		cause->diagnostics_len = sizeof(cause->diagnostics);
		
		q931_ieid_fill_diagnostic_from_list(
			cause->diagnostics, &cause->diagnostics_len,
			&ds.invalid_access_ies);

		q931_ies_add_put(&ies, &cause->ie);

		q931_call_send_status(call, &ies);
	}

	if (!list_empty(&ds.invalid_opt_ies)) {

		struct q931_ies ies = Q931_IES_INIT;
		struct q931_ie_cause *cause;
		cause = q931_ie_cause_alloc();

		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value =
			Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS;

		cause->diagnostics_len = sizeof(cause->diagnostics);
		
		q931_ieid_fill_diagnostic_from_list(
			cause->diagnostics, &cause->diagnostics_len,
			&ds.invalid_opt_ies);

		q931_ies_add_put(&ies, &cause->ie);

		q931_call_send_status(call, &ies);
	}

	if (!list_empty(&ds.unrecognized_ies)) {

		struct q931_ie_cause *cause;
		cause = q931_ie_cause_alloc();

		cause->coding_standard = Q931_IE_C_CS_CCITT;
		cause->location = q931_ie_cause_location_call(call);
		cause->value = Q931_IE_C_CV_INFORMATION_ELEMENT_NON_EXISTENT;

		cause->diagnostics_len = sizeof(cause->diagnostics);
		
		q931_ieid_fill_diagnostic_from_list(
			cause->diagnostics, &cause->diagnostics_len,
			&ds.unrecognized_ies);

		struct q931_ies ies = Q931_IES_INIT;
		q931_ies_add_put(&ies, &cause->ie);

		switch(msg->message_type) {
		case Q931_MT_DISCONNECT: 
			q931_ies_add_put(&call->release_with_cause, &cause->ie);
		break;

		case Q931_MT_RELEASE: {
			q931_call_send_release_complete(call, &ies);

			goto do_not_process_message;
		}
		break;

		case Q931_MT_RELEASE_COMPLETE:
			// Ignore content errors in RELEASE_COMPLETE
		break;

		default:
			q931_call_send_status(call, &ies);

			goto do_not_process_message;
		break;
		}
	}

process_message:

	q931_decoder_status_destroy(&ds);

	return 0;

do_not_process_message:

	q931_decoder_status_destroy(&ds);

	return -1;
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
	if (socket < 0)
		goto err_accept;

	q931_dlc_init(dlc, intf, socket);

	dlc->status = Q931_DLC_STATUS_DISCONNECTED;

	socklen_t optlen = sizeof(dlc->tei);
	if (getsockopt(socket, SOL_LAPD, LAPD_TEI,
		&dlc->tei, &optlen) < 0) {
		report_intf(intf, LOG_ERR,
			"getsockopt: %s\n", strerror(errno));
		goto err_getsockopt;
	}

	if (intf->flags & Q931_INTF_FLAGS_DEBUG) {
		int on = 1;

		if (setsockopt(socket, SOL_SOCKET, SO_DEBUG,
						&on, sizeof(on)) < 0)
			report_intf(intf, LOG_ERR,
				"setsockopt: %s\n", strerror(errno));
	}

	list_add_tail(&dlc->intf_node, &intf->dlcs);

	return dlc;

err_getsockopt:
	close(socket);
err_accept:
	free(dlc);
err_malloc:

	return NULL;
}

int q931_receive(struct q931_dlc *dlc)
{
	struct q931_message *msg;
	msg = malloc(sizeof(*msg));
	if (!msg)
		return -EFAULT;

	q931_message_init(msg, dlc);

	struct msghdr skmsg;
	struct sockaddr_lapd sal;
	struct cmsghdr cmsg;
	struct iovec iov;

	iov.iov_base = msg->raw;
	iov.iov_len = sizeof(msg->raw);

	skmsg.msg_name = &sal;
	skmsg.msg_namelen = sizeof(sal);
	skmsg.msg_iov = &iov;
	skmsg.msg_iovlen = 1;
	skmsg.msg_control = &cmsg;
	skmsg.msg_controllen = sizeof(cmsg);
	skmsg.msg_flags = 0;

	msg->rawlen = recvmsg(dlc->socket, &skmsg, 0);
	if(msg->rawlen < 0) {
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

	if (msg->rawlen < sizeof(struct q931_header)) {
		report_msg(msg, LOG_DEBUG,
			"Message too short (%d bytes), ignoring\n",
			msg->rawlen);

		return -EBADMSG;
	}

	struct q931_header *hdr = (struct q931_header *)msg->raw;

	if (hdr->protocol_discriminator != Q931_PROTOCOL_DISCRIMINATOR_Q931) {
		report_msg(msg, LOG_DEBUG,
			"Protocol discriminator %u not supported,"
			" ignoring message\n",
			hdr->protocol_discriminator);

		return -EBADMSG;
	}

	if (hdr->spare1 != 0) {
		report_msg(msg, LOG_DEBUG,
			"Call reference size invalid, ignoring frame\n");

		return -EBADMSG;
	}

	if (hdr->call_reference_len > 4) {
		report_msg(msg, LOG_DEBUG,
			"Call reference length of %u bytes is too big"
			" and not supported (max 4), ignoring frame\n",
			hdr->call_reference_len);

		return -EBADMSG;
	}

	/* Decode the call reference. If the call reference length is zero
	 * msg->callref will remain zero, indicating the dummy call reference
	 */

	msg->callref = 0;
	msg->callref_direction = Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE;
	msg->callref_len = hdr->call_reference_len;

	int i;
	for(i=0; i<hdr->call_reference_len; i++) {

		__u8 val = hdr->call_reference[i];

		if (i==0 && val & 0x80) {
			val &= 0x7f;

			msg->callref_direction =
				Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE;
		}

#if __BYTE_ORDER == __LITTLE_ENDIAN
		msg->callref |= val << ((hdr->call_reference_len-i-1) * 8);
#else
		msg->callref |= val << (hdr->i * 8);
#endif
	}

	msg->raw_message_type =
		*(__u8 *)(msg->raw + sizeof(struct q931_header) +
		hdr->call_reference_len);

	// Shortcut :^) The enum happens to have the same integer value
	msg->message_type = msg->raw_message_type;

	report_msg(msg, LOG_DEBUG, "Received message:\n");

	report_msg_cont(msg, LOG_DEBUG,
		"<-  call reference = %lu.%c (len %d)\n",
		msg->callref,
		msg->callref_direction ? 'O' : 'I',
		msg->callref_len);

	report_msg_cont(msg, LOG_DEBUG, "<-  message_type = %s (%u)\n",
		q931_message_type_to_text(msg->message_type),
		msg->message_type);

	msg->rawies = msg->raw + sizeof(struct q931_header) +
			msg->callref_len + 1;
	msg->rawies_len = msg->rawlen - (sizeof(struct q931_header) +
			msg->callref_len + 1);

	if (msg->callref == 0) {
		q931_dispatch_global_message(
			&dlc->intf->global_call, msg);

		return Q931_RECEIVE_OK;
	}

	struct q931_call *call =
		q931_get_call_by_reference(
			dlc->intf,
			msg->callref_direction ==
				Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE
				? Q931_CALL_DIRECTION_INBOUND
				: Q931_CALL_DIRECTION_OUTBOUND,
			msg->callref);

	if (!call) {

		call = q931_call_alloc_in(
			dlc->intf, dlc,
			msg->callref,
			skmsg.msg_flags & MSG_OOB);
		if (!call) {
			report_msg(msg, LOG_ERR,
				"Error allocating call\n");

			return -EFAULT;
		}

		switch (msg->message_type) {
		case Q931_MT_RELEASE:
			report_call(call, LOG_DEBUG,
				"Received a RELEASE for an unknown callref\n");

			struct q931_ies ies = Q931_IES_INIT;
			struct q931_ie_cause *cause = q931_ie_cause_alloc();
			cause->coding_standard = Q931_IE_C_CS_CCITT;
			cause->location = q931_ie_cause_location_call(call);
			cause->value =
				Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE;
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
			if (msg->callref_direction ==
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
			cause->location = q931_ie_cause_location_call(call);
			cause->value =
				Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE;
			q931_ies_add_put(&ies, &cause->ie);

			q931_call_send_release(call, &ies);

			q931_call_start_timer(call, T308);

			if (call->state == N0_NULL_STATE)
				q931_call_set_state(call, N19_RELEASE_REQUEST);
			else
				q931_call_set_state(call, U19_RELEASE_REQUEST);

			q931_call_release_reference(call);
			q931_call_put(call);

			return Q931_RECEIVE_OK;
		}
		break;
		}
	}

	struct q931_ces *ces, *tces;
	list_for_each_entry_safe(ces, tces, &call->ces, node) {

		if (ces->dlc == dlc) {
			// selected_ces may change after
			// "dispatch_message"
			if (ces == call->selected_ces) {
				q931_ces_dispatch_message(ces, msg);

				break;
			} else {
				q931_ces_dispatch_message(ces, msg);
				q931_call_put(call);

				return Q931_RECEIVE_OK;
			}
		}
	}

	q931_dispatch_message(call, msg);

	report_msg_cont(msg, LOG_DEBUG, "\n");

	q931_message_put(msg);
	q931_call_put(call);

	return Q931_RECEIVE_OK;
}
