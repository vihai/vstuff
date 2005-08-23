#ifndef _LIBQ931_DLC_H
#define _LIBQ931_DLC_H

#include "list.h"

#define report_dlc(dlc, lvl, format, arg...)				\
	(dlc)->intf->lib->report(					\
		(lvl),							\
		"TEI '%d': "						\
		format,							\
		(dlc)->tei,						\
		## arg)

enum q931_dlc_status
{
	DLC_DISCONNECTED,
	DLC_AWAITING_CONNECTION,
	DLC_AWAITING_DISCONNECTION,
	DLC_CONNECTED,
};

struct q931_interface;
struct q931_dlc
{
	struct q931_lib *lib;

	struct list_head intf_node;

	int socket;
	struct q931_interface *intf;
	enum q931_dlc_status status;
	int tei;
};

static inline void q931_init_dlc(
	struct q931_dlc *dlc,
	struct q931_interface *intf,
	int socket)
{
	dlc->socket = socket;
	dlc->intf = intf;
}

#endif
