#ifndef _INTF_H
#define _INTF_H

#include <netinet/in.h>
#include <lapd.h>

#include "list.h"

#define report_if(intf, lvl, format, arg...)				\
	(intf)->lib->report((lvl), format, ## arg)

struct q931_call;
struct q931_interface
{
	struct list_head node;

	struct q931_lib *lib;

	char *name;

	// NT mode, socket is the master socket
	int nt_socket;
	int nt_poll_id;

	// TE mode, use DLC
	struct q931_dlc te_dlc;

	enum lapd_role role;

	q931_callref next_call_reference;
	int call_reference_size;

	int ncalls;
	// TODO: Use a HASH for improved scalability
	struct list_head calls;

	longtime_t T301;
	longtime_t T302;
	longtime_t T303;
	longtime_t T304;
	longtime_t T305;
	longtime_t T306;
	longtime_t T307;
	longtime_t T308;
	longtime_t T309;
	longtime_t T310;
	longtime_t T312;
	longtime_t T314;
	longtime_t T316;
	longtime_t T317;
	longtime_t T320;
	longtime_t T321;
	longtime_t T322;

	void (*alerting_indication)(struct q931_call *call);
	void (*connect_indication)(struct q931_call *call);
	void (*disconnect_indication)(struct q931_call *call);
	void (*error_indication)(struct q931_call *call); // TE
	void (*info_indication)(struct q931_call *call);
	void (*more_info_indication)(struct q931_call *call);
	void (*notify_indication)(struct q931_call *call);
	void (*proceeding_indication)(struct q931_call *call);
	void (*progress_indication)(struct q931_call *call);
	void (*reject_indication)(struct q931_call *call);
	void (*release_confirm)(struct q931_call *call);//TE
	void (*release_indication)(struct q931_call *call);
	void (*resume_confirm)(struct q931_call *call);//TE
	void (*resume_indication)(struct q931_call *call);
	void (*setup_complete_indication)(struct q931_call *call);//TE
	void (*setup_confirm)(struct q931_call *call);
	void (*setup_indication)(struct q931_call *call);
	void (*status_indication)(struct q931_call *call);
	void (*suspend_confirm)(struct q931_call *call);//TE
	void (*suspend_indication)(struct q931_call *call);
	void (*timeout_indication)(struct q931_call *call);
};

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
	list_del(&call->calls_node);
	
	call->interface->ncalls--;

}

struct q931_interface *q931_open_interface(
	struct q931_lib *lib,
	const char *name);
void q931_close_interface(struct q931_interface *interface);

q931_callref q931_alloc_call_reference(struct q931_interface *interface);

#endif
