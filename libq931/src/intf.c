#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <assert.h>
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
#include "intf.h"

q931_callref q931_alloc_call_reference(struct q931_interface *interface)
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
		interface->master_socket = -1;

		q931_init_dlc(&interface->dlc, interface, s);
			
		interface->dlc.status = DLC_DISCONNECTED;

		interface->T301 = 180 * 1000000LL;
		interface->T302 =  15 * 1000000LL;
		interface->T303 =   4 * 1000000LL;
		interface->T304 =  30 * 1000000LL;
		interface->T305 =  30 * 1000000LL;
		interface->T308 =   4 * 1000000LL;
		interface->T309 =  90 * 1000000LL;
		interface->T310 =  45 * 1000000LL;
		interface->T314 =   4 * 1000000LL;
		interface->T316 = 120 * 1000000LL;
		interface->T317 =  60 * 1000000LL;
		interface->T318 =   4 * 1000000LL;
		interface->T319 =   4 * 1000000LL;
		interface->T321 =  30 * 1000000LL;
		interface->T322 =   4 * 1000000LL;
	} else {
		interface->master_socket = s;

		q931_init_dlc(&interface->bc_dlc, interface, s);
		q931_init_dlc(&interface->dlc, NULL, -1);

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
	}

	interface->type = Q931_INTF_TYPE_BRA_MULTIPOINT;

	switch (interface->type) {
	case Q931_INTF_TYPE_BRA_POINT_TO_POINT:
		interface->n_channels = 2;
	break;
	case Q931_INTF_TYPE_BRA_MULTIPOINT:
		interface->n_channels = 2;
	break;
	case Q931_INTF_TYPE_PRA:
		interface->n_channels = 30;
	break;
	}

	int i;
	for (i=0; i<interface->n_channels; i++) {
		interface->channels[i].id = i;
		interface->channels[i].intf = interface;
		interface->channels[i].state = Q931_CHANSTATE_AVAILABLE;
		interface->channels[i].call = NULL;
	}

	list_add(&interface->node, &lib->intfs);

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

	list_del(&interface->node);

	if (interface->role == LAPD_ROLE_TE) {
		shutdown(interface->dlc.socket, 0);
		close(interface->dlc.socket);
	} else {
// FIXME: for each dlc, shutdown and close?

		// Broadcast socket == master socket but only we know it :)

		close(interface->master_socket);
	}

	if (interface->name)
		free(interface->name);

	free(interface);
}
