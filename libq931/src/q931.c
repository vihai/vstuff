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

//			q931_send_release_cause(call, call->ces[i],
//				Q931_IE_C_CV_NON_SELECTED_USER_CLEARING);


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
	list_for_each_entry_safe(call, callt, &dlc->interface->calls, calls_node) {
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
	list_for_each_entry_safe(call, callt, &dlc->interface->calls, calls_node) {
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
	list_for_each_entry_safe(call, callt, &dlc->interface->calls, calls_node) {
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
	list_for_each_entry_safe(call, callt, &dlc->interface->calls, calls_node) {
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
		q931_message_type_to_text(message_type),
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
		call = q931_alloc_call_in(
			dlc->interface, dlc,
			callref,
			msg.msg_flags & MSG_OOB);
		if (!call) {
			report_dlc(dlc, LOG_ERR,
				"Error allocating call\n");

			return;
		}

		// Shortcut for "Other Messages"
		if (message_type != Q931_MT_SETUP &&
		    message_type != Q931_MT_RESUME &&
		    message_type != Q931_MT_RELEASE &&
		    message_type != Q931_MT_STATUS &&
		    message_type != Q931_MT_RELEASE_COMPLETE) {
			q931_send_release(call, call->dlc);
			q931_call_start_timer(call, T308);
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

	struct q931_ces *ces, *tces;
	list_for_each_entry_safe(ces, tces, &call->ces, node) {
		report_dlc(dlc, LOG_INFO, "=====================> %p == %p\n", dlc, ces->dlc);

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
