/*
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and
 * distributed under the terms of the GNU Public License.
 *
 */

#ifndef _FAKEDEV_H
#define _FAKEDEV_H

#define hfc_DRIVER_NAME "fake-isdn"
#define hfc_DRIVER_PREFIX hfc_DRIVER_NAME ": "
#define hfc_DRIVER_DESCR "Fake ISDN driver"

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

struct fake_card {
	int id;
	spinlock_t lock;

	int ticks;

	struct pci_dev *pcidev;

	struct proc_dir_entry *proc_dir;
	char proc_dir_name[32];

	struct proc_dir_entry *proc_info;
	struct proc_dir_entry *proc_fifos;

	enum hfc_chip_type chip_type;
	int num_ports;
	struct hfc_port ports[8];

	unsigned long io_bus_mem;
	void *io_mem;

	int sync_loss_reported;
	int late_irqs;

	int ignore_first_timer_interrupt;

	int open_ports;
};

#endif
