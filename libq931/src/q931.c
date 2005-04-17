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

#include "lib.h"
#include "list.h"
#include "logging.h"
#include "msgtype.h"
#include "ie.h"
#include "out.h"
#include "global.h"
#include "ces.h"
#include "call.h"
#include "intf.h"
#include "proto.h"

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
	q931_ie_infos_init();
	q931_message_types_init();

	INIT_LIST_HEAD(&lib->timers);
	INIT_LIST_HEAD(&lib->intfs);

	return lib;
}

void q931_leave(struct q931_lib *lib)
{
	free(lib);
}

void q931_dl_establish_indication(struct q931_dlc *dlc)
{
	dlc->status = DLC_CONNECTED;

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

		q931_call_dl_establish_indication(call);
	}
}

void q931_dl_establish_confirm(struct q931_dlc *dlc)
{
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

		q931_call_dl_establish_confirm(call);
	}
}

void q931_dl_release_indication(struct q931_dlc *dlc)
{
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

		q931_call_dl_release_indication(call);
	}
}

void q931_dl_release_confirmation(struct q931_dlc *dlc)
{
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

		q931_call_dl_release_confirm(call);
	}
}

int ie_has_content_errors(
	struct q931_message *msg,
	struct q931_ie *ie)
{
	assert(msg);
	assert(ie);
	assert(ie->info);

	if (ie->info->max_len >= 0 &&
	    ie->len > ie->info->max_len) {

		report_msg(msg, LOG_DEBUG,
			"IE bigger than maximum "
			" specified len (%d > %d)\n",
			ie->len,
			ie->info->max_len);

		if (0) { // Access IE??! 5.8.7.2
		} else if (0) { // Call identity???
		} else {
		}

	}

	if (ie->info->validity_check &&
	     !ie->info->validity_check(msg, ie)) {
		return TRUE;
	}

	return FALSE;
}

void q931_decode_information_elements(
	struct q931_call *call,
	struct q931_message *msg)
{
	int curpos= sizeof(struct q931_header) + msg->callref_len + 1;
	int codeset = 0;
	int codeset_locked = FALSE;
	int invalid_mandatory_ie_present = FALSE;
	int invalid_optional_ie_present = FALSE;
	int unrecognized_ie_present = FALSE;
	struct q931_ie *ie;

	int i=0;
	while(curpos < msg->rawlen) {
		ie = &msg->ies[msg->ies_cnt];

		__u8 first_octet = *(__u8 *)(msg->raw + curpos);
		curpos++;

		if (q931_is_so_ie(first_octet)) {
			// Single octet IE

			if (q931_get_so_ie_id(first_octet) == Q931_IE_SHIFT) {
				if (q931_get_so_ie_type2_value(first_octet) & 0x08) {
					// Locking shift

					report_dlc(msg->dlc, LOG_DEBUG,
						"Locked Switch from codeset %u to codeset %u",
						codeset,
						q931_get_so_ie_type2_value(first_octet) & 0x07);

					codeset = q931_get_so_ie_type2_value(first_octet) & 0x07;
					codeset_locked = FALSE;

					continue;
				} else {
					// Non-Locking shift

					report_dlc(msg->dlc, LOG_DEBUG,
						"Non-Locked Switch from codeset %u to codeset %u",
						codeset,
						q931_get_so_ie_type2_value(first_octet));

					codeset = q931_get_so_ie_type2_value(first_octet);
					codeset_locked = TRUE;

					continue;
				}
			}

			if (codeset == 0) {
				ie->info =
					q931_get_ie_info(first_octet);

				if (ie->info) {
					ie->len = 0;
					ie->data = NULL;
					msg->ies_cnt++;

					report_dlc(msg->dlc, LOG_DEBUG,
						"SO IE %d ===> %u (%s)\n", i,
						first_octet,
						ie->info->name);
				} else {
					report_dlc(msg->dlc, LOG_DEBUG,
						"SO IE %d ===> %u (unknown)\n", i,
						first_octet);
				}
			}
		} else {
			// Variable Length IE

			ie->data = msg->raw + curpos;
			ie->len = *(__u8 *)(msg->raw + curpos);
			curpos++;

			if (codeset == 0) {
				// Check out-of-sequence
				// Check duplicated IEs
				// Check missing mandatory IE

				ie->info =
					q931_get_ie_info(first_octet);

				const struct q931_ie_info_per_mt *ie_info2 =
					q931_get_ie_info_per_mt(
						msg->message_type, first_octet);

				if (!ie->info) {
					if (q931_ie_comprehension_required(first_octet)) {
						unrecognized_ie_present = TRUE;

						report_dlc(msg->dlc, LOG_DEBUG,
							"Unrecognized IE %d in message"
							" for which comprehension is required\n",
							first_octet);
					}

					goto skip_this_ie;
				}

				if (!ie_info2) {
					report_msg(msg, LOG_DEBUG,
						"Unexpected IE %d in message type %s\n",
						first_octet,
						q931_message_type_to_text(
							msg->message_type));

					goto skip_this_ie;
				}

				if (!ie_has_content_errors(msg, &msg->ies[msg->ies_cnt])) {
					report_msg(msg, LOG_DEBUG,
						"VS IE %d ===> %u (%s) -- length %u\n", i,
						first_octet,
						ie->info->name,
						ie->len);

					msg->ies_cnt++;
				} else {
					// If mandatory or comprension required
					if (0) {
						invalid_mandatory_ie_present = TRUE;
//						call->release_diagnostics[0] = first_octet; // FIXME
					} else {
						invalid_optional_ie_present = TRUE;
//						call->release_diagnostics[0] = first_octet;
					}
				}
			}

skip_this_ie:

			curpos += ie->len;

			if(curpos > msg->rawlen) {
				report_msg(msg, LOG_ERR, "MALFORMED FRAME\n");
				// FIXME
				break;
			}
		}

		if (!codeset_locked)
			codeset = 0;

		i++;
	}

	if (!call)
		return;

	if (invalid_mandatory_ie_present) {
		switch(msg->message_type) {
		case Q931_MT_SETUP:
		case Q931_MT_RELEASE:
			q931_send_release_complete_cause(call, msg->dlc,
				Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS);

			return;
		break;

		case Q931_MT_DISCONNECT:
			call->release_with_cause =
				Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS;
		break;

		case Q931_MT_RELEASE_COMPLETE:
			// Ignore content errors in RELEASE_COMPLETE
		break;

		default:
			q931_send_status(call, msg->dlc,
				call->state,
				Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS
				/* ADD diagnostics here FIXME */);

			return;
		break;
		}
	} else if (unrecognized_ie_present) {
		switch(msg->message_type) {
		case Q931_MT_DISCONNECT:
			call->release_with_cause =
				Q931_IE_C_CV_INFORMATION_ELEMENT_NON_EXISTENT;
		break;

		case Q931_MT_RELEASE:
			q931_send_release_complete_cause(call, msg->dlc,
				Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS);

			return;
		break;

		case Q931_MT_RELEASE_COMPLETE:
			// Ignore content errors in RELEASE_COMPLETE
		break;

		default:
			q931_send_status(call, msg->dlc,
				call->state,
				Q931_IE_C_CV_INFORMATION_ELEMENT_NON_EXISTENT
				/* ADD diagnostics here FIXME */);

			return;
		break;
		}
	} else if (invalid_optional_ie_present) {
		switch(msg->message_type) {
		case Q931_MT_DISCONNECT:
			call->release_with_cause =
				Q931_IE_C_CV_INFORMATION_ELEMENT_NON_EXISTENT;
		break;

		case Q931_MT_RELEASE:
			q931_send_release_complete_cause(call, msg->dlc,
				Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS);

			return;
		break;

		case Q931_MT_RELEASE_COMPLETE:
			// Ignore content errors in RELEASE_COMPLETE
		break;

		default:
			q931_send_status(call, msg->dlc,
				call->state,
				Q931_IE_C_CV_INFORMATION_ELEMENT_NON_EXISTENT
				/* ADD diagnostics here FIXME */);

			return;
		break;
		}
	}
}

void q931_receive(struct q931_dlc *dlc)
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
		if (errno == ECONNRESET)
			q931_dl_release_indication(dlc);
		else if (errno == EISCONN)
			q931_dl_establish_indication(dlc);
		else {
			report_dlc(dlc, LOG_ERR,
				"recvmsg: %s\n", strerror(errno));
		}

		return;
	}

	if (msg.rawlen < sizeof(struct q931_header)) {
		report_dlc(dlc, LOG_DEBUG,
			"Message to small (%d bytes), ignoring\n",
			msg.rawlen);

		return;
	}

	struct q931_header *hdr = (struct q931_header *)msg.raw;

	if (hdr->protocol_discriminator != Q931_PROTOCOL_DISCRIMINATOR_Q931) {
		report_dlc(dlc, LOG_DEBUG,
			"Protocol discriminator %u not supported,"
			" ignoring message\n",
			hdr->protocol_discriminator);

		return;
	}

	if (hdr->spare1 != 0) {
		report_dlc(dlc, LOG_DEBUG,
			"Call reference size invalid, ignoring frame\n");

		return;
	}

	if (hdr->call_reference_len > 4) {
		report_dlc(dlc, LOG_DEBUG,
			"Call reference length of %u bytes is too big"
			" and not supported (max 4), ignoring frame\n",
			hdr->call_reference_len);

		return;
	}

	if (hdr->call_reference_len == 1 &&
	    hdr->call_reference[0] == 0) {
		report_dlc(dlc, LOG_DEBUG,
			"The dummy call reference is not supported,"
			" ignoring frame\n");

		return;
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
	   
		// LITTLE ENDIAN
		msg.callref |= val << ((hdr->call_reference_len-i-1) * 8);
	}


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

	if (msg.callref == 0x00) { // FIXME CHECKME
		q931_decode_information_elements(NULL, &msg);
		q931_dispatch_global_message(&dlc->intf->global_call, &msg);

		return;
	}

	struct q931_call *call =
		q931_find_call_by_reference(
			dlc->intf,
			msg.callref_direction ==
				Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE
				? Q931_CALL_DIRECTION_INBOUND
				: Q931_CALL_DIRECTION_OUTBOUND,
			msg.callref);

	if (!call) {
		call = q931_alloc_call_in(
			dlc->intf, dlc,
			msg.callref,
			skmsg.msg_flags & MSG_OOB);
		if (!call) {
			report_dlc(dlc, LOG_ERR,
				"Error allocating call\n");

			return;
		}

		switch (msg.message_type) {
		case Q931_MT_RELEASE:
			report_call(call, LOG_DEBUG,
				"Received a RELEASE for an unknown callref\n");

			q931_send_release_complete_cause(call, dlc,
				Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE);

			return;
		break;

		case Q931_MT_RELEASE_COMPLETE:
			report_call(call, LOG_DEBUG,
				"Received a RELEASE COMPLETE for an unknown"
				" callref, ignoring frame\n");

			return;
		break;

		case Q931_MT_SETUP:
		case Q931_MT_RESUME:
			if (msg.callref_direction ==
			      Q931_CALLREF_FLAG_TO_ORIGINATING_SIDE) {

				report_call(call, LOG_DEBUG,
					"Received a SETUP/RESUME for an unknown"
					" outbound callref, ignoring frame\n");

				return;
			}
		break;

		case Q931_MT_STATUS:
			// Pass it to the handler
		break;

		default:
			q931_send_release_cause(call, dlc,
				Q931_IE_C_CV_INVALID_CALL_REFERENCE_VALUE);
			q931_call_start_timer(call, T308);

			if (call->state == N0_NULL_STATE)
				q931_call_set_state(call, U19_RELEASE_REQUEST);
			else
				q931_call_set_state(call, N19_RELEASE_REQUEST);

			return;
		break;
		}
	}

	q931_decode_information_elements(call, &msg);

	struct q931_ces *ces, *tces;
	list_for_each_entry_safe(ces, tces, &call->ces, node) {

		if (ces->dlc == dlc) {
			// selected_ces may change after "dispatch_message"
			if (ces == call->selected_ces) {
				q931_ces_dispatch_message(ces, &msg);

				break;
			} else {
				q931_ces_dispatch_message(ces, &msg);

				return;
			}
		}
	}

	q931_dispatch_message(call, &msg);

}
