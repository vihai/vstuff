/*
 * vISDN LAPD/q.931 protocol implementation
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _FAKE_ISDN_H
#define _FAKE_ISDN_H

#define fake_DRIVER_NAME "fake-isdn"
#define fake_DRIVER_PREFIX fake_DRIVER_NAME ": "
#define fake_DRIVER_DESCR "Fake ISDN driver"

#ifndef intptr_t
#define intptr_t unsigned long
#endif

#ifndef uintptr_t
#define uintptr_t unsigned long
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

struct fake_card;

struct fake_chan
{
	struct fake_card *card;

	struct net_device *netdev;
	struct net_device_stats net_device_stats;
};

struct fake_card
{
	spinlock_t lock;

	struct fake_chan chans[2];
};

#endif
