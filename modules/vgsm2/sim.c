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

#include <linux/kstreamer/channel.h>
#include <linux/kstreamer/node.h>
#include <linux/kstreamer/pipeline.h>
#include <linux/kstreamer/streamframe.h>
#include <linux/kstreamer/softswitch.h>

#include "vgsm.h"
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
		card->id * 4 + sim->id,
		FALSE,
		NULL);

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
