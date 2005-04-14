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

#include "list.h"
#include "q931.h"
#include "logging.h"
#include "msgtype.h"
#include "ie.h"
#include "out.h"
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

int ie_has_content_errors(struct q931_call *call, struct q931_ie *ie)
{
	assert(call);
	assert(ie);

	if (!ie->info) {
		report_call(call, LOG_DEBUG,
			"Ignoring unrecognized IE"
			" (length %u)\n",
			ie->size);

		// What should we do here?

		return FALSE;
	}

	if (ie->info->max_size >= 0 &&
	    ie->size > ie->info->max_size) {

		report_call(call, LOG_DEBUG,
			"IE bigger than maximum "
			" specified size (%d > %d)\n",
			ie->size,
			ie->info->max_size);

		if (0) { // Access IE??! 5.8.7.2
		} else if (0) { // Call identity???
		} else {
		}

	}

	if (ie->info->validity_check &&
	     !ie->info->validity_check(call, ie)) {
		return FALSE;
	}

	return TRUE;
}


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
			q931_dl_release_indication(dlc);
		else if (errno == EISCONN)
			q931_dl_establish_indication(dlc);
		else {
			report_dlc(dlc, LOG_ERR,
				"recvmsg: %s\n", strerror(errno));
		}

		return;
	}

	if (len < sizeof(struct q931_header)) {
		report_dlc(dlc, LOG_DEBUG,
			"Message to small (%d bytes), ignoring\n",
			len);

		return;
	}

	struct q931_header *hdr = (struct q931_header *)frame;

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

	if (hdr->call_reference_size > 4) {
		report_dlc(dlc, LOG_DEBUG,
			"Call reference length of %u bytes is too big"
			" and not supported (max 4), ignoring frame\n",
			hdr->call_reference_size);

		return;
	}

	if (hdr->call_reference_size == 1 &&
	    hdr->call_reference[0] == 0) {
		report_dlc(dlc, LOG_DEBUG,
			"The dummy call reference is not supported,"
			" ignoring frame\n");

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
		"  call reference = %u %lu %c\n",
		hdr->call_reference_size,
		callref,
		callref_direction?'O':'I');

	__u8 message_type = *(__u8 *)(frame + sizeof(struct q931_header) +
		hdr->call_reference_size);

	report_dlc(dlc, LOG_INFO, "  message_type = %s (%u)\n",
		q931_message_type_to_text(message_type),
			message_type);

	struct q931_call *call =
		q931_find_call_by_reference(
			dlc->intf,
			callref_direction ==
				Q931_CALLREF_FLAG_FROM_ORIGINATING_SIDE
				? Q931_CALL_DIRECTION_INBOUND
				: Q931_CALL_DIRECTION_OUTBOUND,
			callref);

	if (!call) {
		call = q931_alloc_call_in(
			dlc->intf, dlc,
			callref,
			msg.msg_flags & MSG_OOB);
		if (!call) {
			report_dlc(dlc, LOG_ERR,
				"Error allocating call\n");

			return;
		}

		switch (message_type) {
		case Q931_MT_RELEASE:
			report_call(call, LOG_DEBUG,
				"Received a RELEASE for an unknown callref\n");

			q931_send_release_complete_cause(call, call->dlc,
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
			if (callref_direction ==
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
			q931_send_release_cause(call, call->dlc,
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

	// Decode information elemnts
	struct q931_ie ies[260];
	int ies_cnt = 0;

	int curpos= sizeof(struct q931_header) + hdr->call_reference_size + 1;
	int codeset = 0;
	int codeset_locked = FALSE;
	int invalid_mandatory_ie_present = FALSE;
	int invalid_optional_ie_present = FALSE;
	int unrecognized_ie_present = FALSE;

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
				// Check out-of-sequence
				// Check duplicated IEs
				// Check missing mandatory IE

				const struct q931_ie_info *ie_info =
					q931_get_ie_info(first_octet);

				const struct q931_ie_info_per_mt *ie_info2 =
					q931_get_ie_info_per_mt(message_type, first_octet);

				if (!ie_info) {
					if (q931_ie_comprehension_required(first_octet)) {
						unrecognized_ie_present = TRUE;

						report_call(call, LOG_DEBUG,
							"Unrecognized IE %d in message"
							" for which comprehension is required\n",
							first_octet);
					}

					goto skip_this_ie;
				}

				if (!ie_info2) {
					report_call(call, LOG_DEBUG,
						"Unexpected IE %d in message type %s\n",
						first_octet,
						q931_message_type_to_text(message_type));

					goto skip_this_ie;
				}

				ies[ies_cnt].info = ie_info;
				ies[ies_cnt].size = ie_len;
				ies[ies_cnt].data = frame + curpos;

				if (ie_has_content_errors(call, &ies[ies_cnt])) {
					ies_cnt++;

					report_dlc(dlc, LOG_DEBUG,
						"VS IE %d ===> %u (%s) -- length %u\n", i,
						first_octet,
						ie_info->name,
						ie_len);
				} else {
					// If mandatory or comprension required
					if (0) {
						invalid_mandatory_ie_present = TRUE;
						call->release_diagnostics[0] = first_octet; // FIXME
					} else {
						invalid_optional_ie_present = TRUE;
						call->release_diagnostics[0] = first_octet;
					}
				}
			}

skip_this_ie:

			curpos += ie_len;

			if(curpos > len) {
				report_dlc(dlc, LOG_ERR, "MALFORMED FRAME\n");
				// FIXME
				break;
			}
		}

		if (!codeset_locked) codeset = 0;

		i++;
	}

	if (invalid_mandatory_ie_present) {
		switch(message_type) {
		case Q931_MT_SETUP:
		case Q931_MT_RELEASE:
			q931_send_release_complete_cause(call, dlc,
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
			q931_send_status(call, dlc,
				call->state,
				Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS
				/* ADD diagnostics here FIXME */);

			return;
		break;
		}
	} else if (unrecognized_ie_present) {
		switch(message_type) {
		case Q931_MT_DISCONNECT:
			call->release_with_cause =
				Q931_IE_C_CV_INFORMATION_ELEMENT_NON_EXISTENT;
		break;

		case Q931_MT_RELEASE:
			q931_send_release_complete_cause(call, dlc,
				Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS);

			return;
		break;

		case Q931_MT_RELEASE_COMPLETE:
			// Ignore content errors in RELEASE_COMPLETE
		break;

		default:
			q931_send_status(call, dlc,
				call->state,
				Q931_IE_C_CV_INFORMATION_ELEMENT_NON_EXISTENT
				/* ADD diagnostics here FIXME */);

			return;
		break;
		}
	} else if (invalid_optional_ie_present) {
		switch(message_type) {
		case Q931_MT_DISCONNECT:
			call->release_with_cause =
				Q931_IE_C_CV_INFORMATION_ELEMENT_NON_EXISTENT;
		break;

		case Q931_MT_RELEASE:
			q931_send_release_complete_cause(call, dlc,
				Q931_IE_C_CV_INVALID_INFORMATION_ELEMENT_CONTENTS);

			return;
		break;

		case Q931_MT_RELEASE_COMPLETE:
			// Ignore content errors in RELEASE_COMPLETE
		break;

		default:
			q931_send_status(call, dlc,
				call->state,
				Q931_IE_C_CV_INFORMATION_ELEMENT_NON_EXISTENT
				/* ADD diagnostics here FIXME */);

			return;
		break;
		}
	}

	struct q931_ces *ces, *tces;
	list_for_each_entry_safe(ces, tces, &call->ces, node) {
		if (ces->dlc == dlc) {
			// selected_ces may change after "dispatch_message"
			if (ces == call->selected_ces) {
				q931_ces_dispatch_message(ces,
					message_type, ies, ies_cnt);

				break;
			} else {
				q931_ces_dispatch_message(ces,
					message_type, ies, ies_cnt);

				return;
			}
		}
	}

	q931_dispatch_message(call, dlc, message_type, ies, ies_cnt);
}
