/*
 * fifo.c
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 *
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/netdevice.h>

#include "hfc-usb.h"
#include "fifo.h"

