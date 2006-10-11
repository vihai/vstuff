/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _KS_ROUTER_H
#define _KS_ROUTER_H

#include <kernel_config.h>

extern dev_t ks_first_dev;

/* See core.h for IOC allocation */
#define KS_IOC_CONNECT		_IOR(0xd0, 0x02, unsigned int)
#define KS_IOC_DISCONNECT		_IOR(0xd0, 0x03, unsigned int)
#define KS_IOC_DISCONNECT_ENDPOINT	_IOR(0xd0, 0x04, unsigned int)
#define KS_IOC_PIPELINE_OPEN		_IOR(0xd0, 0x05, unsigned int)
#define KS_IOC_PIPELINE_CLOSE	_IOR(0xd0, 0x06, unsigned int)
#define KS_IOC_PIPELINE_START	_IOR(0xd0, 0x07, unsigned int)
#define KS_IOC_PIPELINE_STOP		_IOR(0xd0, 0x08, unsigned int)

struct ks_connect
{
	int pipeline_id;

	char from_endpoint[80];
	char to_endpoint[80];

	int flags;
};

#define KS_CONNECT_FLAG_PERMANENT	(1 << 0)

#ifdef __KERNEL__

int ks_router_modinit(void);
void ks_router_modexit(void);

#endif

#endif
