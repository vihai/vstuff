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

#include <linux/autoconf.h>
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
	.driver_name		= "vgsm2_sim",
	.dev_name		= "vgsm2_sim",
	.major			= 0,
	.minor			= 0,
	.nr			= VGSM_MAX_CARDS * VGSM_MAX_MES,
	.cons			= NULL,
};

void vgsm_sim_update_sim_setup(struct vgsm_sim *sim)
{
	struct vgsm_card *card = sim->card;
	u32 reg;
	int i;

	for(i=0; i<card->mes_number; i++) {
		if (card->mes[i] &&
		    card->mes[i]->route_to_sim == sim->id) {
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

	reg |=	VGSM_R_SIM_SETUP_V_VCC |
                VGSM_R_SIM_SETUP_V_3V;

	vgsm_outl(card, VGSM_R_SIM_SETUP(sim->id), reg);
}

static int vgsm_sim_ioctl_sim_get_id(
	struct vgsm_sim *sim,
	unsigned int cmd,
	unsigned long arg)
{
	return put_user(sim->id, (int __user *)arg);
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
	case VGSM_IOC_SIM_GET_ID:
		return vgsm_sim_ioctl_sim_get_id(sim, cmd, arg);
	case VGSM_IOC_SIM_GET_CLOCK:
		return vgsm_sim_ioctl_sim_get_clock(sim, cmd, arg);
	case VGSM_IOC_SIM_SET_CLOCK:
		return vgsm_sim_ioctl_sim_set_clock(sim, cmd, arg);
	}

	return -ENOIOCTLCMD;
}

/*---------------------------------------------------------------------------*/

struct vgsm_sim_attribute {
	struct attribute attr;

	ssize_t (*show)(
		struct vgsm_sim *node,
		struct vgsm_sim_attribute *attr,
		char *buf);

	ssize_t (*store)(
		struct vgsm_sim *node,
		struct vgsm_sim_attribute *attr,
		const char *buf,
		size_t count);
};

#define VGSM_SIM_ATTR(_name,_mode,_show,_store) \
	struct vgsm_sim_attribute vgsm_sim_attr_##_name = \
		__ATTR(_name,_mode,_show,_store)

static ssize_t vgsm_sim_identify_show(
	struct vgsm_sim *sim,
	struct vgsm_sim_attribute *attr,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
		test_bit(VGSM_ME_STATUS_IDENTIFY, &sim->status) ? 1 : 0);
}

static ssize_t vgsm_sim_identify_store(
	struct vgsm_sim *sim,
	struct vgsm_sim_attribute *attr,
	const char *buf,
	size_t count)
{
	unsigned int value;
	if (sscanf(buf, "%02x", &value) < 1)
		return -EINVAL;

	if (value)
		set_bit(VGSM_ME_STATUS_IDENTIFY, &sim->status);
	else
		clear_bit(VGSM_ME_STATUS_IDENTIFY, &sim->status);

	vgsm_led_update();

	return count;
}

static VGSM_SIM_ATTR(identify, S_IRUGO | S_IWUSR,
		vgsm_sim_identify_show,
		vgsm_sim_identify_store);

/*---------------------------------------------------------------------------*/

static ssize_t vgsm_sim_attr_show(
	struct kobject *kobj,
	struct attribute *attr,
	char *buf)
{
	struct vgsm_sim_attribute *vgsm_sim_attr =
			container_of(attr, struct vgsm_sim_attribute, attr);
	struct vgsm_sim *vgsm_sim =
			container_of(kobj, struct vgsm_sim, kobj);
	ssize_t err;

	if (vgsm_sim_attr->show)
		err = vgsm_sim_attr->show(vgsm_sim, vgsm_sim_attr, buf);
	else
		err = -EIO;

	return err;
}

static ssize_t vgsm_sim_attr_store(
	struct kobject *kobj,
	struct attribute *attr,
	const char *buf,
	size_t count)
{
	struct vgsm_sim_attribute *vgsm_sim_attr =
			container_of(attr, struct vgsm_sim_attribute, attr);
	struct vgsm_sim *vgsm_sim =
			container_of(kobj, struct vgsm_sim, kobj);
	ssize_t err;

	if (vgsm_sim_attr->store)
		err = vgsm_sim_attr->store(vgsm_sim, vgsm_sim_attr, buf, count);
	else
		err = -EIO;

	return err;
}

static struct sysfs_ops vgsm_sim_sysfs_ops = {
	.show   = vgsm_sim_attr_show,
	.store  = vgsm_sim_attr_store,
};

static void vgsm_sim_release(struct kobject *kobj)
{
}

static struct attribute *vgsm_sim_default_attrs[] =
{
	&vgsm_sim_attr_identify.attr,
	NULL,
};

static struct kobj_type vgsm_sim_ktype = {
	.release	= vgsm_sim_release,
	.sysfs_ops	= &vgsm_sim_sysfs_ops,
	.default_attrs	= vgsm_sim_default_attrs,
};

struct vgsm_sim *vgsm_sim_create(
	struct vgsm_sim *sim,
	struct vgsm_card *card,
	int id,
	u32 uart_base)
{
	BUG_ON(!sim); /* Dynamic allocation not implemented */

	memset(sim, 0, sizeof(*sim));

	kobject_init(&sim->kobj);
	kobject_set_name(&sim->kobj, "sim%d", id);

	sim->kobj.ktype = &vgsm_sim_ktype;
	sim->kobj.parent = &card->pci_dev->dev.kobj;

	sim->card = card;
	sim->id = id;

	vgsm_uart_create(&sim->uart,
		&vgsm_uart_driver_sim,
		sim->card->regs_bus_mem + uart_base,
		sim->card->regs_mem + uart_base,
		card->pci_dev->irq,
		&card->pci_dev->dev,
		card->id * 8 + sim->id,
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

	err = kobject_add(&sim->kobj);
	if (err < 0)
		goto err_kobject_add;

	return 0;

	kobject_del(&sim->kobj);
err_kobject_add:
	vgsm_uart_unregister(&sim->uart);
err_register_uart:

	return err;
}

void vgsm_sim_unregister(struct vgsm_sim *sim)
{
	kobject_del(&sim->kobj);

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
