/*
 * VoiSmart vGSM-II board driver
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_UART_H
#define _VGSM_UART_H

#include <linux/serial_core.h>

struct vgsm_uart
{
	struct uart_port port;
	struct uart_driver *driver;

	int (*ioctl)(struct vgsm_uart *uart,
		unsigned int cmd, unsigned long arg);

	int sim_mode;

	u8 acr;
	u8 ier;
	u8 lcr;
	u8 mcr;
	u8 mcr_mask;	/* mask of user bits */
	u8 mcr_force;	/* mask of forced bits */
	u8 lsr_break_flag;
};

void vgsm_uart_interrupt(struct vgsm_uart *up);

struct vgsm_uart *vgsm_uart_create(
	struct vgsm_uart *uart,
	struct uart_driver *uart_driver,
	unsigned long mapbase,
	void *membase,
	int irq,
	struct device *dev,
	int line,
	int sim_mode,
	int (*ioctl)(struct vgsm_uart *uart,
		unsigned int cmd, unsigned long arg));

void vgsm_uart_destroy(struct vgsm_uart *uart);
int vgsm_uart_register(struct vgsm_uart *uart);
void vgsm_uart_unregister(struct vgsm_uart *uart);

int __init vgsm_uart_modinit(void);
void __exit vgsm_uart_modexit(void);

#endif
