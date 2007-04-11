/*
 * VoiSmart vGSM-II board driver
 *
 * Copyright (C) 2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/serial.h>
#include <linux/termios.h>
#include <linux/serial_core.h>

#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/node.h>
#include <linux/kstreamer/pipeline.h>
#include <linux/kstreamer/streamframe.h>
#include <linux/kstreamer/softswitch.h>

#include "vgsm2.h"
#include "card.h"
#include "card_inline.h"
#include "regs.h"
#include "sim.h"

#ifdef DEBUG_CODE
#define vgsm_debug_sim(sim, dbglevel, format, arg...)		\
	if (debug_level >= dbglevel)					\
		printk(KERN_DEBUG vgsm_DRIVER_PREFIX			\
			"%s-%s:"					\
			"sim[%d] "					\
			format,						\
			(sim)->card->pci_dev->dev.bus->name,		\
			(sim)->card->pci_dev->dev.bus_id,		\
			(sim)->id,					\
			## arg)

#else
#define vgsm_debug_sim(sim, dbglevel, format, arg...) do {} while (0)
#endif

#define vgsm_msg_sim(sim, level, format, arg...)			\
	printk(level vgsm_DRIVER_PREFIX					\
		"%s-%s:"						\
		"sim[%d] "						\
		format,							\
		(sim)->card->pci_dev->dev.bus->name,			\
		(sim)->card->pci_dev->dev.bus_id,			\
		(sim)->id,						\
		## arg)

static struct uart_driver vgsm_uart_driver_sim =
{
	.owner			= THIS_MODULE,
	.driver_name		= "vgsm_sim",
	.dev_name		= "vgsm_sim",
	.major			= 0,
	.minor			= 0,
	.nr			= 32,
	.cons			= NULL,
};

static void vgsm_sim_update_sim_setup(struct vgsm_sim *sim)
{
	struct vgsm_card *card = sim->card;
	u32 reg;
	int i;

	for(i=0; i<card->mes_number; i++) {
		if (card->modules[i]->route_to_sim == sim->id) {
			vgsm_outl(card, VGSM_R_SIM_SETUP(sim->id),
				VGSM_R_SIM_SETUP_V_CLOCK_ME);
			return;
		}
	}

	switch(sim->clock) {
	case 3571200: reg = VGSM_R_SIM_SETUP_V_CLOCK_3_5; break;
	case 4000000: reg = VGSM_R_SIM_SETUP_V_CLOCK_4; break;
	case 5000000: reg = VGSM_R_SIM_SETUP_V_CLOCK_5; break;
	case 7142400: reg = VGSM_R_SIM_SETUP_V_CLOCK_7_1; break;
	case 8000000: reg = VGSM_R_SIM_SETUP_V_CLOCK_8; break;
	case 10000000: reg = VGSM_R_SIM_SETUP_V_CLOCK_10; break;
	case 14284800: reg = VGSM_R_SIM_SETUP_V_CLOCK_14; break;
	case 16000000: reg = VGSM_R_SIM_SETUP_V_CLOCK_16; break;
	case 20000000: reg = VGSM_R_SIM_SETUP_V_CLOCK_20; break;
	default: reg = VGSM_R_SIM_SETUP_V_CLOCK_3_5; break;
	}

	vgsm_outl(card, VGSM_R_SIM_SETUP(sim->id), reg);
}

static int vgsm_sim_ioctl_sim_get_clock(
	struct vgsm_sim *sim,
	unsigned int cmd,
	unsigned long arg)
{
	return put_user(sim->clock, (int __user *)arg);
}

static int vgsm_sim_ioctl_sim_set_clock(
	struct vgsm_sim *sim,
	unsigned int cmd,
	unsigned long arg)
{
	if (arg <= 3571200)
		sim->clock = 3571200;
	else if (arg <= 4000000)
		sim->clock = 4000000;
	else if (arg <= 5000000)
		sim->clock = 5000000;
	else if (arg <= 7142400)
		sim->clock = 7142400;
	else if (arg <= 8000000)
		sim->clock = 8000000;
	else if (arg <= 10000000)
		sim->clock = 10000000;
	else if (arg <= 14284800)
		sim->clock = 14284800;
	else if (arg <= 16000000)
		sim->clock = 16000000;
	else /*if (arg <= 20000000)*/
		sim->clock = 20000000;

	vgsm_sim_update_sim_setup(sim);

	return 0;
}

static int vgsm_sim_ioctl(
	struct vgsm_uart *uart,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_sim *sim = container_of(uart, struct vgsm_sim, uart);

	switch(cmd) {
	case VGSM_IOC_SIM_GET_CLOCK:
		return vgsm_sim_ioctl_sim_get_clock(sim, cmd, arg);
	case VGSM_IOC_SIM_SET_CLOCK:
		return vgsm_sim_ioctl_sim_set_clock(sim, cmd, arg);
	break;
	}

	return -ENOIOCTLCMD;
}

struct vgsm_sim *vgsm_sim_create(
	struct vgsm_sim *sim,
	struct vgsm_card *card,
	int id,
	u32 uart_base)
{
	BUG_ON(!sim); /* Dynamic allocation not implemented */

	memset(sim, 0, sizeof(*sim));

	sim->card = card;
	sim->id = id;

	vgsm_uart_create(&sim->uart,
		&vgsm_uart_driver_sim,
		sim->card->regs_bus_mem + uart_base,
		sim->card->regs_mem + uart_base,
		card->pci_dev->irq,
		&card->pci_dev->dev,
		card->id * 8 + sim->id,
		FALSE,
		vgsm_sim_ioctl);

	return sim;
}


void vgsm_sim_destroy(struct vgsm_sim *sim)
{
	vgsm_uart_destroy(&sim->uart);
}

/*------------------------------------------------------------------------*/

int vgsm_sim_register(struct vgsm_sim *sim)
{
	int err;

	err = vgsm_uart_register(&sim->uart);
	if (err < 0)
		goto err_register_uart;

	return 0;

	vgsm_uart_unregister(&sim->uart);
err_register_uart:

	return err;
}

void vgsm_sim_unregister(struct vgsm_sim *sim)
{
	vgsm_uart_unregister(&sim->uart);
}

int __init vgsm_sim_modinit(void)
{
	int err;

	err = uart_register_driver(&vgsm_uart_driver_sim);
	if (err < 0)
		goto err_register_driver_sim;

	return 0;

	uart_unregister_driver(&vgsm_uart_driver_sim);
err_register_driver_sim:

	return err;
}

void __exit vgsm_sim_modexit(void)
{
	uart_unregister_driver(&vgsm_uart_driver_sim);
}
