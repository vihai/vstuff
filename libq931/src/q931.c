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
#include "logging.h"
#include "msgtype.h"
#include "ie.h"
#include "out.h"
#include "ces.h"
#include "call.h"

//			q931_send_release_cause(call, call->ces[i],
//				Q931_IE_C_CV_NON_SELECTED_USER_CLEARING);


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
	list_for_each_entry(call, &interface->calls, calls_node) {
		if (call->direction == Q931_CALL_DIRECTION_OUTBOUND &&
		    call->call_reference == call_reference) {
			if (call_reference == interface->next_call_reference)
				return -1;
			else
				goto try_again;
		}
	}

	return call_reference;
}

struct q931_lib *q931_init()
{
	struct q931_lib *lib;

	lib = malloc(sizeof(*lib));
	if (!lib) {
		//FIXME
		exit(1);
	}

	lib->report = q931_default_report;

	// Non-reentrant, FIXME
	q931_ie_infos_init();
	q931_message_types_init();

	return lib;
}

void q931_leave(struct q931_lib *lib)
{
	free(lib);
}

struct q931_interface *q931_open_interface(
	struct q931_lib *lib,
	const char *name)
{
	struct q931_interface *interface;

	assert(name);

	interface = malloc(sizeof(*interface));
	if (!interface)
		abort();

	memset(interface, 0x00, sizeof(*interface));

	INIT_LIST_HEAD(&interface->calls);

	interface->lib = lib;
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

	interface->T301 = 180 * 1000000LL;
	interface->T302 =  12 * 1000000LL;
	interface->T303 =   4 * 1000000LL;
	interface->T304 =  20 * 1000000LL;
	interface->T305 =  30 * 1000000LL;
	interface->T306 =  30 * 1000000LL;
	interface->T307 = 180 * 1000000LL;
	interface->T308 =   4 * 1000000LL;
	interface->T309 =  90 * 1000000LL;
	interface->T310 =  35 * 1000000LL;
	interface->T312 = interface->T303 + 2 * 1000000LL;
	interface->T314 =   4 * 1000000LL;
	interface->T316 = 120 * 1000000LL;
	interface->T317 =  60 * 1000000LL;
	interface->T320 =  30 * 1000000LL;
	interface->T321 =  30 * 1000000LL;
	interface->T322 =   4 * 1000000LL;

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

	if (interface->role == LAPD_ROLE_TE) {
		shutdown(interface->te_dlc.socket, 0);
		close(interface->te_dlc.socket);
	} else {
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

	q931_init_timer(&call->T301);
	q931_init_timer(&call->T302);
	q931_init_timer(&call->T303);
	q931_init_timer(&call->T304);
	q931_init_timer(&call->T305);
	q931_init_timer(&call->T306);
	q931_init_timer(&call->T307);
	q931_init_timer(&call->T308);
	q931_init_timer(&call->T309);
	q931_init_timer(&call->T310);
	q931_init_timer(&call->T312);
	q931_init_timer(&call->T314);
	q931_init_timer(&call->T316);
	q931_init_timer(&call->T317);
	q931_init_timer(&call->T320);
	q931_init_timer(&call->T321);
	q931_init_timer(&call->T322);

	// Inherit callbacks from interface
	call->alerting_indication = interface->alerting_indication;
	call->disconnect_indication = interface->disconnect_indication;
	call->error_indication = interface->error_indication;
	call->info_indication = interface->info_indication;
	call->more_info_indication = interface->more_info_indication;
	call->notify_indication = interface->notify_indication;
	call->proceeding_indication = interface->proceeding_indication;
	call->progress_indication = interface->progress_indication;
	call->reject_indication = interface->reject_indication;
	call->release_confirm = interface->release_confirm;
	call->release_indication = interface->release_indication;
	call->resume_confirm = interface->resume_confirm;
	call->resume_indication = interface->resume_indication;
	call->setup_complete_indication = interface->setup_complete_indication;
	call->setup_confirm = interface->setup_confirm;
	call->setup_indication = interface->setup_indication;
	call->status_indication = interface->status_indication;
	call->suspend_confirm = interface->suspend_confirm;
	call->suspend_indication = interface->suspend_indication;
	call->timeout_indication = interface->timeout_indication;

	return call;
}

void q931_free_call(struct q931_call *call)
{
	assert(call);
	assert(call->calls_node.next == LIST_POISON1);
	assert(call->calls_node.prev == LIST_POISON2);

	free(call);
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
		call = q931_alloc_call(dlc->interface);
		if (!call) {
			report_dlc(dlc, LOG_ERR,
				"Error allocating call\n");

			return;
		}

		if (msg.msg_flags & MSG_OOB) {
			call->dlc = NULL;
			call->broadcasted_setup = TRUE;
		} else {
			call->dlc = dlc;
			call->broadcasted_setup = FALSE;
		}

		call->interface = dlc->interface;

		call->direction = Q931_CALL_DIRECTION_INBOUND;
		call->call_reference =
			q931_alloc_call_reference(call->interface);

		if (call->call_reference < 0) {
			report_call(call, LOG_ERR,
				"All call references are used!!!\n");
			return;
		}

		report_call(call, LOG_INFO,
			"Call reference allocated (%ld)\n",
			call->call_reference);

		q931_add_call(dlc->interface, call);

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

	struct q931_ces *ces;
	list_for_each_entry(ces, &call->ces, node) {
		if (ces->dlc == dlc) {

			q931_ces_dispatch_message(ces,
				message_type, ies, ies_cnt);

			if (ces == call->selected_ces)
				break;
			else
				return;
		}
	}

	q931_dispatch_message(call, dlc, message_type, ies, ies_cnt);
}
