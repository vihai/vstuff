/*
 * vISDN low-level drivers infrastructure core
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VISDN_TIMER_H
#define _VISDN_TIMER_H

#ifdef __KERNEL__

#include <linux/delay.h>

struct visdn_timer;
struct visdn_timer_ops
{
	struct module *owner;

	void (*release)(struct visdn_timer *timer);
	int (*open)(struct visdn_timer *visdn_timer);
	int (*close)(struct visdn_timer *visdn_timer);
};

struct visdn_timer
{
	char name[64]; // FIXME

	void *priv;

	struct class_device class_dev;

	struct visdn_timer_ops *ops;

	wait_queue_head_t wait_queue;

	struct list_head users_list;
	spinlock_t users_list_lock;

	int natural_frequency;
	int main_divider;
	int poll_divider;
	int poll_count;

	int total_users;
	int users_left;
};

int visdn_timer_modinit(void);
void visdn_timer_modexit(void);

#define to_visdn_timer(class) container_of(class, struct visdn_timer, class_dev)

extern void visdn_timer_tick(struct visdn_timer *timer);

extern void visdn_timer_init(
	struct visdn_timer *visdn_timer);

extern int visdn_timer_register(
	struct visdn_timer *visdn_timer);

extern void visdn_timer_unregister(
	struct visdn_timer *visdn_timer);

#endif

#endif
