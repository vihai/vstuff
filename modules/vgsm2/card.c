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

#include <linux/autoconf.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/bitops.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/serial.h>
#include <linux/termios.h>
#include <linux/serial_core.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>

#include "vgsm2.h"
#include "card.h"
#include "card_inline.h"
#include "regs.h"
#include "me.h"

struct list_head vgsm_cards_list = LIST_HEAD_INIT(vgsm_cards_list);
spinlock_t vgsm_cards_list_lock = SPIN_LOCK_UNLOCKED;

static dev_t vgsm_card_first_dev;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
static void vgsm_card_class_release(struct class_device *cd)
#else
static void vgsm_card_class_dev_release(struct device *cd)
#endif
{
}

static struct class vgsm_card_class = {
	.name = "vgsm_card",
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	.release = vgsm_card_class_release,
#else
	.dev_release = vgsm_card_class_dev_release,
#endif
};

static int vgsm_card_cdev_open(
	struct inode *inode,
	struct file *file)
{
	struct vgsm_card *card;

	file->private_data = NULL;

	spin_lock(&vgsm_cards_list_lock);
	list_for_each_entry(card, &vgsm_cards_list, cards_list_node) {
		if (card->id == inode->i_rdev - vgsm_card_first_dev)
			file->private_data = card;
	}
	spin_unlock(&vgsm_cards_list_lock);

	if (!file->private_data)
		return -ENOENT;

	return 0;
}

static int vgsm_card_cdev_release(
	struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t vgsm_card_cdev_read(
	struct file *file,
	char __user *buf,
	size_t count,
	loff_t *offp)
{
	return -ENOTSUPP;
}

static ssize_t vgsm_card_cdev_write(
	struct file *file,
	const char __user *buf,
	size_t count,
	loff_t *offp)
{
	return -ENOTSUPP;
}

static int vgsm_card_asmi_waitbusy(struct vgsm_card *card)
{
	int i;

	if (vgsm_inl(card, VGSM_R_ASMI_STA) & VGSM_R_ASMI_STA_V_RUNNING) {

		for(i=0; i<500 && (vgsm_inl(card, VGSM_R_ASMI_STA) &
				VGSM_R_ASMI_STA_V_RUNNING); i++)
			udelay(1);

		for(i=0; i<500 && (vgsm_inl(card, VGSM_R_ASMI_STA) &
				VGSM_R_ASMI_STA_V_RUNNING); i++)
			msleep(10);

		if (i == 500)
			return -ETIMEDOUT;
	}

	return 0;
}

static u8 vgsm_card_asmi_readb(struct vgsm_card *card, int pos)
{
	vgsm_outl(card, VGSM_R_ASMI_ADDR, pos);
	vgsm_outl(card, VGSM_R_ASMI_CTL,
		VGSM_R_ASMI_CTL_V_RDEN |
		VGSM_R_ASMI_CTL_V_READ |
		VGSM_R_ASMI_CTL_V_START);

	vgsm_card_asmi_waitbusy(card);

	return VGSM_R_ASMI_STA_V_DATAOUT(vgsm_inl(card, VGSM_R_ASMI_STA));
}

static int vgsm_card_ioctl_fw_version(
	struct vgsm_card *card,
	unsigned int cmd,
	unsigned long arg)
{
	if (copy_to_user((void __user *)arg, &card->fw_version,
						sizeof(card->fw_version)))
		return -EFAULT;

	return 0;
}

static int vgsm_card_ioctl_fw_flash_version(
	struct vgsm_card *card,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_fw_version fw_version;

	memset(&fw_version, 0, sizeof(fw_version));

	fw_version.maj = vgsm_card_asmi_readb(card, 0x5ffffd);
	fw_version.min = vgsm_card_asmi_readb(card, 0x5ffffe);
	fw_version.ser = vgsm_card_asmi_readb(card, 0x5fffff);

	if (copy_to_user((void __user *)arg, &fw_version, sizeof(fw_version)))
		return -EFAULT;

	return 0;
}

static int vgsm_card_ioctl_fw_upgrade(
	struct vgsm_card *card,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm2_fw_header fwh;
	int err;
	int i;
	u32 led_src_orig;
	u32 led = 0, prev_led = 0;

	if (!capable(CAP_SYS_ADMIN)) {
		err = -EPERM;
		goto err_no_capa;
	}

	if (copy_from_user(&fwh, (void *)arg, sizeof(fwh))) {
		err = -EFAULT;
		goto err_copy_from_user;
        }

	if (fwh.size > 0x60000) {
		err = -ENOMEM;
		goto err_too_big;
	}

	if (test_and_set_bit(VGSM_CARD_FLAGS_FLASH_ACCESS, &card->flags)) {
		err = -EBUSY;
		goto err_busy;
	}

	vgsm_msg_card(card, KERN_INFO,
		"Firmware programming started (%d bytes)...\n",
		fwh.size);

	led_src_orig = vgsm_inl(card, VGSM_R_LED_SRC);
	vgsm_outl(card, VGSM_R_LED_SRC, 0xffffffff);

	card->fw_upgrade_stat.state = VGSM_FW_UPGRADE_ERASE;
	card->fw_upgrade_stat.tot = 0x60000;

	for(i=0; i<0x60000; i+=0x10000) {
		vgsm_msg_card(card, KERN_INFO,
			"Erasing sector 0x%05x\n", i);

		vgsm_outl(card, VGSM_R_ASMI_ADDR, i);
		vgsm_outl(card, VGSM_R_ASMI_CTL,
				VGSM_R_ASMI_CTL_V_WREN |
				VGSM_R_ASMI_CTL_V_SECTOR_ERASE |
				VGSM_R_ASMI_CTL_V_START);

		card->fw_upgrade_stat.pos = i;

		led = ((i / 0x10000) % 2) ? 0x5555 : 0xaaaa;
		if (led != prev_led) {
			prev_led = led;
			vgsm_outl(card, VGSM_R_LED_USER, led);
		}

		err = vgsm_card_asmi_waitbusy(card);
		if (err < 0)
			goto err_timeout;
	}

	card->fw_upgrade_stat.state = VGSM_FW_UPGRADE_WRITE;
	card->fw_upgrade_stat.tot = fwh.size;

	for(i=0; i<fwh.size; i++) {
		u8 b;

		if (copy_from_user(&b, (void *)(arg + sizeof(fwh) + i),
				sizeof(b))) {
			err = -EFAULT;
			goto err_copy_from_user_payload;
		}

		vgsm_outl(card, VGSM_R_ASMI_ADDR, i);
		vgsm_outl(card, VGSM_R_ASMI_CTL,
				VGSM_R_ASMI_CTL_V_WREN |
				VGSM_R_ASMI_CTL_V_WRITE |
				VGSM_R_ASMI_CTL_V_START |
				VGSM_R_ASMI_CTL_V_DATAIN(b));

		card->fw_upgrade_stat.pos = i;

		led = 0x5555 >> ((7 - ((i * 8) / fwh.size)) * 2);
		if (led != prev_led) {
			prev_led = led;
			vgsm_outl(card, VGSM_R_LED_USER, led);
		}

		err = vgsm_card_asmi_waitbusy(card);
		if (err < 0)
			goto err_timeout;
	}

	vgsm_msg_card(card, KERN_INFO,
		"Firmware programming completed\n");

	set_bit(VGSM_CARD_FLAGS_RECONFIG_PENDING, &card->flags);

	card->fw_upgrade_stat.state = VGSM_FW_UPGRADE_OK;
	card->fw_upgrade_stat.pos = 0;

	clear_bit(VGSM_CARD_FLAGS_FLASH_ACCESS, &card->flags);
	vgsm_led_update();

	return 0; 

err_timeout:
err_copy_from_user_payload:
	card->fw_upgrade_stat.state = VGSM_FW_UPGRADE_KO;
	card->fw_upgrade_stat.pos = 0;
	clear_bit(VGSM_CARD_FLAGS_FLASH_ACCESS, &card->flags);
	vgsm_led_update();
err_busy:
err_too_big:
err_copy_from_user:
err_no_capa:

	return err;
}

static int vgsm_card_ioctl_fw_read(
	struct vgsm_card *card,
	unsigned int cmd,
	unsigned long arg)
{
	int err;
	int i;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (test_and_set_bit(VGSM_CARD_FLAGS_FLASH_ACCESS, &card->flags)) {
		err = -EBUSY;
		goto err_busy;
	}

	card->fw_upgrade_stat.state = VGSM_FW_UPGRADE_READ;
	card->fw_upgrade_stat.pos = 0;
	card->fw_upgrade_stat.tot = 0x60000;

	for(i=0; i<0x60000; i++) {
		u8 b;

		card->fw_upgrade_stat.pos = i;

		b = vgsm_card_asmi_readb(card, i);

		if (copy_to_user((void __user *)(arg + i),
				&b, sizeof(b))) {
			err = -EFAULT;
			goto err_copy_to_user_payload;
		}
	}

	clear_bit(VGSM_CARD_FLAGS_FLASH_ACCESS, &card->flags);

	return i; 

err_copy_to_user_payload:
	clear_bit(VGSM_CARD_FLAGS_FLASH_ACCESS, &card->flags);
err_busy:

	return err;
}

static int vgsm_card_ioctl_fw_upgrade_stat(
	struct vgsm_card *card,
	unsigned int cmd,
	unsigned long arg)
{
	if (copy_to_user((void __user *)arg, &card->fw_upgrade_stat,
						sizeof(card->fw_upgrade_stat)))
		return -EFAULT;

	return 0;
}

static int vgsm_card_ioctl_read_serial(
	struct vgsm_card *card,
	unsigned int cmd,
	unsigned long arg)
{
	return put_user(card->serial_number, (int __user *)arg);
}

static int vgsm_card_ioctl_get_id(
	struct vgsm_card *card,
	unsigned int cmd,
	unsigned long arg)
{
	return put_user(card->id, (int __user *)arg);
}

int vgsm_card_ioctl(
	struct vgsm_card *card,
	unsigned int cmd,
	unsigned long arg)
{
	switch(cmd) {
	case VGSM_IOC_FW_VERSION:
		return vgsm_card_ioctl_fw_version(card, cmd, arg);
	break;

	case VGSM_IOC_FW_FLASH_VERSION:
		return vgsm_card_ioctl_fw_flash_version(card, cmd, arg);
	break;

	case VGSM_IOC_FW_UPGRADE:
		return vgsm_card_ioctl_fw_upgrade(card, cmd, arg);
	break;

	case VGSM_IOC_FW_READ:
		return vgsm_card_ioctl_fw_read(card, cmd, arg);
	break;

	case VGSM_IOC_FW_UPGRADE_STAT:
		return vgsm_card_ioctl_fw_upgrade_stat(card, cmd, arg);
	break;

	case VGSM_IOC_READ_SERIAL:
		return vgsm_card_ioctl_read_serial(card, cmd, arg);
	break;

	case VGSM_IOC_CARD_GET_ID:
		return vgsm_card_ioctl_get_id(card, cmd, arg);
	break;
	}

	return -ENOIOCTLCMD;
}


static int vgsm_card_cdev_ioctl(
	struct inode *inode,
	struct file *file,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm_card *card = file->private_data;

	return vgsm_card_ioctl(card, cmd, arg);
}

struct file_operations vgsm_card_fops =
{
	.owner		= THIS_MODULE,
	.read		= vgsm_card_cdev_read,
	.write		= vgsm_card_cdev_write,
	.ioctl		= vgsm_card_cdev_ioctl,
	.open		= vgsm_card_cdev_open,
	.release	= vgsm_card_cdev_release,
	.llseek		= no_llseek,
};

#ifndef HAVE_CLASS_DEV_DEVT
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
static ssize_t show_dev(struct class_device *class_dev, char *buf)
{
	struct vgsm_card *card =
		container_of(class_dev, struct vgsm_card, class_device);

	return print_dev_t(buf, vgsm_card_first_dev + card->id);
}
#else
static ssize_t show_dev(struct device *class_dev, char *buf)
{
	struct vgsm_card *card =
		container_of(class_dev, struct vgsm_card, device);

	return print_dev_t(buf, vgsm_card_first_dev + card->id);
}
#endif

static CLASS_DEVICE_ATTR(dev, S_IRUGO, show_dev, NULL);
#endif


/* HW initialization */

static int vgsm_initialize_hw(struct vgsm_card *card)
{
	int i;
	u32 int_enable = 0;

	/* Reset all subsystems */
	vgsm_outl(card, VGSM_R_SERVICE, VGSM_R_SERVICE_V_RESET);
	msleep(10); // FIXME!!!

#ifdef __LITTLE_ENDIAN
	vgsm_outl(card, VGSM_R_SERVICE, 0);
#elif __BIG_ENDIAN
	vgsm_outl(card, VGSM_R_SERVICE,
		VGSM_R_SERVICE_V_BIG_ENDIAN);
#else
#error Unsupported endianness
#endif

	/* Set LEDs */
	vgsm_outl(card, VGSM_R_LED_SRC,
		VGSM_R_LED_SRC_V_STATUS_G |
		VGSM_R_LED_SRC_V_STATUS_R);

	vgsm_outl(card, VGSM_R_LED_USER,
		VGSM_R_LED_SRC_V_STATUS_G);

	/* Enable interrupts */

	for(i=0; i<card->mes_number; i++) {
		if (card->mes[i]) {
			vgsm_outl(card, VGSM_R_ME_INT_ENABLE(i),
/*				VGSM_R_ME_INT_ENABLE_V_VDD |
				VGSM_R_ME_INT_ENABLE_V_VDDLP |
				VGSM_R_ME_INT_ENABLE_V_CCVCC |*/
				VGSM_R_ME_INT_ENABLE_V_DAI_RX_INT |
				VGSM_R_ME_INT_ENABLE_V_DAI_RX_END |
				VGSM_R_ME_INT_ENABLE_V_DAI_TX_INT |
				VGSM_R_ME_INT_ENABLE_V_DAI_TX_END |
				VGSM_R_ME_INT_ENABLE_V_UART_ASC0 |
				VGSM_R_ME_INT_ENABLE_V_UART_ASC1 |
				VGSM_R_ME_INT_ENABLE_V_UART_MESIM);

			vgsm_outl(card, VGSM_R_ME_FIFO_SETUP(i),
				VGSM_R_ME_FIFO_SETUP_V_RX_LINEAR |
				VGSM_R_ME_FIFO_SETUP_V_TX_LINEAR);

			int_enable |= VGSM_R_INT_ENABLE_V_ME(i);
		}
	}

	for(i=0; i<card->sims_number; i++) {
		vgsm_outl(card, VGSM_R_SIM_INT_ENABLE(i),
			VGSM_R_SIM_INT_ENABLE_V_CCIN |
			VGSM_R_SIM_INT_ENABLE_V_UART);

		int_enable |= VGSM_R_INT_ENABLE_V_SIM(i);
	}

	vgsm_outl(card, VGSM_R_INT_ENABLE, int_enable);

	/* This takes care of setting SIM_ROUTE and SIM_SETUP(n) */
	vgsm_card_update_router(card);

	vgsm_msg(KERN_DEBUG, "vGSM-II card successfully initialized\n");

	return 0;
}

static void vgsm_me_interrupt(struct vgsm_card *card, int id)
{
	struct vgsm_me *me = card->mes[id];
	u32 me_int_status = vgsm_inl(card, VGSM_R_ME_INT_STATUS(id));
	u32 me_status = vgsm_inl(card, VGSM_R_ME_STATUS(id));

	if (!me)
		return;

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_UART_ASC0)
		vgsm_uart_interrupt(&me->asc0);

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_UART_ASC1)
		vgsm_uart_interrupt(&me->asc1);

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_UART_MESIM)
		vgsm_uart_interrupt(&me->mesim);

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_VDD)
		vgsm_debug_card(card, 1,
			"ME VDD changed from: %d to %d\n",
			!(me_status & VGSM_R_ME_STATUS_V_VDD),
			!!(me_status & VGSM_R_ME_STATUS_V_VDD));

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_VDDLP)
		vgsm_debug_card(card, 1,
			"ME VDDLP changed from: %d to %d\n",
			!(me_status & VGSM_R_ME_STATUS_V_VDDLP),
			!!(me_status & VGSM_R_ME_STATUS_V_VDDLP));

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_CCVCC)
		vgsm_debug_card(card, 1,
			"ME CCVCC changed from: %d to %d\n",
			!(me_status & VGSM_R_ME_STATUS_V_CCVCC),
			!!(me_status & VGSM_R_ME_STATUS_V_CCVCC));

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_DAI_RX_INT)
		vgsm_debug_card(card, 3, "DAI RX INT\n");

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_DAI_RX_END)
		vgsm_debug_card(card, 3, "DAI RX END\n");

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_DAI_TX_INT)
		vgsm_debug_card(card, 3, "DAI TX INT\n");

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_DAI_TX_END)
		vgsm_debug_card(card, 3, "DAI TX END\n");
}

static void vgsm_sim_interrupt(struct vgsm_sim *sim)
{
	struct vgsm_card *card = sim->card;

	u32 sim_int_status = vgsm_inl(card, VGSM_R_SIM_INT_STATUS(sim->id));
	u32 sim_status = vgsm_inl(card, VGSM_R_SIM_STATUS(sim->id));

	if (sim_int_status & VGSM_R_SIM_INT_STATUS_V_UART)
		vgsm_uart_interrupt(&sim->uart);

	if (sim_int_status & VGSM_R_SIM_INT_STATUS_V_CCIN)
		vgsm_debug_card(card, 1,
			"SIM CCIN changed from: %d to %d\n",
			!(sim_status & VGSM_R_SIM_STATUS_V_CCIN),
			!!(sim_status & VGSM_R_SIM_STATUS_V_CCIN));
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
static irqreturn_t vgsm_interrupt(int irq, void *dev_id, struct pt_regs *regs)
#else
static irqreturn_t vgsm_interrupt(int irq, void *dev_id)
#endif
{
	struct vgsm_card *card = dev_id;
	u32 int_status;
	int i;

	if (unlikely(!card)) {
		vgsm_msg(KERN_CRIT,
			"spurious interrupt (IRQ %d)\n",
			irq);
		return IRQ_NONE;
	}

	int_status = vgsm_inl(card, VGSM_R_INT_STATUS);
	if (!int_status)
		return IRQ_NONE;

	for(i=0; i<card->mes_number; i++) {
		if (int_status & VGSM_R_INT_STATUS_V_ME(i))
			vgsm_me_interrupt(card, i);
	}

	for(i=0; i<card->sims_number; i++) {
		if (int_status & VGSM_R_INT_STATUS_V_SIM(i))
			vgsm_sim_interrupt(&card->sims[i]);
	}

	return IRQ_HANDLED;
}

void vgsm_card_update_router(struct vgsm_card *card)
{
	u32 sim_router = 0;
	int i;
	int j;

	for(i=0; i<card->mes_number; i++) {
		if (card->mes[i])
			sim_router |= card->mes[i]->route_to_sim << (i*4);
		else
			sim_router |= i << (i*4);
	}

	vgsm_outl(card, VGSM_R_SIM_ROUTER, sim_router);

	for(j=0; j<card->sims_number; j++)
		vgsm_sim_update_sim_setup(&card->sims[j]);
}

static void vgsm_card_release(struct kref *kref)
{
	struct vgsm_card *card = container_of(kref, struct vgsm_card, kref);

	kfree(card);
}

struct vgsm_card *vgsm_card_get(struct vgsm_card *card)
{
	if (card)
		kref_get(&card->kref);

	return card;
}

void vgsm_card_put(struct vgsm_card *card)
{
	kref_put(&card->kref, vgsm_card_release);
}

/*----------------------------------------------------------------------------*/

static ssize_t vgsm_show_serial_number(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct vgsm_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%012d\n", card->serial_number);
}

static DEVICE_ATTR(serial_number, S_IRUGO,
		vgsm_show_serial_number,
		NULL);

/*----------------------------------------------------------------------------*/

static ssize_t vgsm_show_mes_number(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct vgsm_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", card->mes_number);
}

static DEVICE_ATTR(mes_number, S_IRUGO,
		vgsm_show_mes_number,
		NULL);

/*----------------------------------------------------------------------------*/

static ssize_t vgsm_show_sims_number(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct vgsm_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", card->sims_number);
}

static DEVICE_ATTR(sims_number, S_IRUGO,
		vgsm_show_sims_number,
		NULL);

/*----------------------------------------------------------------------------*/

static ssize_t vgsm_show_fw_version(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct vgsm_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE,
		"%d.%d.%d\n",
		card->fw_version.maj,
		card->fw_version.min,
		card->fw_version.ser);
}

static DEVICE_ATTR(fw_version, S_IRUGO,
		vgsm_show_fw_version,
		NULL);

/*----------------------------------------------------------------------------*/

static ssize_t vgsm_card_identify_attr_show(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct vgsm_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE,
		"%d\n",
		test_bit(VGSM_CARD_FLAGS_IDENTIFY, &card->flags) ? 1 : 0);
}

static ssize_t vgsm_card_identify_attr_store(
	struct device *device,
	DEVICE_ATTR_COMPAT
	const char *buf,
	size_t count)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct vgsm_card *card = pci_get_drvdata(pci_dev);

	unsigned int value;
	if (sscanf(buf, "%02x", &value) < 1)
		return -EINVAL;

	if (value)
		set_bit(VGSM_CARD_FLAGS_IDENTIFY, &card->flags);
	else
		clear_bit(VGSM_CARD_FLAGS_IDENTIFY, &card->flags);

	vgsm_led_update();

	return count;
}

static DEVICE_ATTR(identify, S_IRUGO | S_IWUSR,
		vgsm_card_identify_attr_show,
		vgsm_card_identify_attr_store);

/*----------------------------------------------------------------------------*/

static struct device_attribute *vgsm_card_attributes[] =
{
        &dev_attr_serial_number,
        &dev_attr_mes_number,
        &dev_attr_sims_number,
        &dev_attr_fw_version,
        &dev_attr_identify,
	NULL,
};

int vgsm_card_sysfs_create_files(
	struct vgsm_card *card)
{
	struct device_attribute **attr = vgsm_card_attributes;
	int err;

	while(*attr) {
		err = device_create_file(
			&card->pci_dev->dev,
			*attr);

		if (err < 0)
			goto err_create_file;

		attr++;
	}

	return 0;

err_create_file:

	while(*attr != *vgsm_card_attributes) {
		device_remove_file(
			&card->pci_dev->dev,
			*attr);

		attr--;
	}

	return err;
}

void vgsm_card_sysfs_delete_files(
	struct vgsm_card *card)
{
	struct device_attribute **attr = vgsm_card_attributes;

	while(*attr) {
		device_remove_file(
			&card->pci_dev->dev,
			*attr);

		attr++;
	}
}

struct vgsm_card *vgsm_card_create(
	struct vgsm_card *card,
	struct pci_dev *pci_dev,
	int id)
{
	BUG_ON(card); /* Static allocation not supported */

	if (!card) {
		card = kmalloc(sizeof(*card), GFP_KERNEL);
		if (!card)
			return NULL;
	}

	memset(card, 0, sizeof(*card));

	kref_init(&card->kref);

	card->pci_dev = pci_dev;
	card->id = id;

	spin_lock_init(&card->lock);

	return card;
}

void vgsm_card_destroy(struct vgsm_card *card)
{
	int i;

	for(i=card->mes_number-1; i>=0; i--) {
		if (card->mes[i])
			vgsm_me_destroy(card->mes[i]);
	}

	for(i=card->sims_number-1; i>=0; i--)
		vgsm_sim_destroy(&card->sims[i]);

	vgsm_card_put(card);
}


int vgsm_card_probe(struct vgsm_card *card)
{
	int err;
	int i;
	u32 r_info;

	/* From here on vgsm_msg_card may be used */

	err = pci_enable_device(card->pci_dev);
	if (err < 0) {
		vgsm_msg_card(card, KERN_CRIT,
			"Error enabling PCI device\n");
		goto err_pci_enable_device;
	}

	pci_write_config_word(card->pci_dev, PCI_COMMAND, PCI_COMMAND_MEMORY);

	err = pci_request_regions(card->pci_dev, vgsm_DRIVER_NAME);
	if (err < 0) {
		vgsm_msg_card(card, KERN_CRIT,
			"cannot request I/O memory region\n");
		goto err_pci_request_regions;
	}

	if (!card->pci_dev->irq) {
		vgsm_msg_card(card, KERN_CRIT,
			"No IRQ assigned to card!\n");
		err = -ENODEV;
		goto err_noirq;
	}

	card->regs_bus_mem = pci_resource_start(card->pci_dev, 0);
	if (!card->regs_bus_mem) {
		vgsm_msg_card(card, KERN_CRIT,
			"No IO memory assigned to card\n");
		err = -ENODEV;
		goto err_no_regs_base;
	}

	/* Note: rember to not directly access address returned by ioremap */
	card->regs_mem = ioremap(card->regs_bus_mem, 0x10000);

	if(!card->regs_mem) {
		vgsm_msg_card(card, KERN_CRIT,
			"Cannot ioremap I/O memory\n");
		err = -ENODEV;
		goto err_ioremap_regs;
	}

	card->fifo_bus_mem = pci_resource_start(card->pci_dev, 1);
	if (!card->fifo_bus_mem) {
		vgsm_msg_card(card, KERN_CRIT,
			"No FIFO memory assigned to card\n");
		err = -ENODEV;
		goto err_no_fifo_base;
	}

	/* Note: rember to not directly access address returned by ioremap */
	card->fifo_mem = ioremap(card->fifo_bus_mem, 0x10000);

	if(!card->fifo_mem) {
		vgsm_msg_card(card, KERN_CRIT,
			"Cannot ioremap FIFO memory\n");
		err = -ENODEV;
		goto err_ioremap_fifo;
	}

	/* Requesting IRQ */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
	err = request_irq(card->pci_dev->irq, &vgsm_interrupt,
			  SA_SHIRQ, vgsm_DRIVER_NAME, card);
#else
	err = request_irq(card->pci_dev->irq, &vgsm_interrupt, IRQF_SHARED,
			  vgsm_DRIVER_NAME, card);
#endif
	if (err < 0) {
		vgsm_msg_card(card, KERN_CRIT,
			"unable to register IRQ\n");
		goto err_request_irq;
	}

	r_info = vgsm_inl(card, VGSM_R_INFO);
	card->sims_number = (r_info & 0x000000f0) >> 4;
	card->mes_number = (r_info & 0x0000000f) >> 0;

	if (r_info == 0xffffffff) {
		vgsm_msg_card(card, KERN_CRIT, "Card not responding\n");
		err = -EINVAL;
		goto err_card_not_responding;
	}

	{
	__u32 fw_version_raw = vgsm_inl(card, VGSM_R_VERSION);
	card->fw_version.maj = (fw_version_raw & 0x00ff0000) >> 16;
	card->fw_version.min = (fw_version_raw & 0x0000ff00) >> 8;
	card->fw_version.ser = (fw_version_raw & 0x000000ff) >> 0;
	}

	if (card->sims_number > 8) {
		WARN_ON(1);
		err = -EINVAL;
		goto err_sims_number_invalid;
	}

	if (card->mes_number > 8) {
		WARN_ON(1);
		err = -EINVAL;
		goto err_mes_number_invalid;
	}

	vgsm_msg_card(card, KERN_INFO,
		"vGSM-II card found at %#0lx\n",
		card->regs_bus_mem);

	vgsm_msg_card(card, KERN_INFO,
		"FW version: %d.%d.%d\n",
		card->fw_version.maj,
		card->fw_version.min,
		card->fw_version.ser);

	for(i=0; i<ARRAY_SIZE(card->serial_octs);i++)
		card->serial_octs[i] = vgsm_card_asmi_readb(card, 0x70000 + i);

	if (card->serial_number)
		vgsm_msg_card(card, KERN_INFO,
			"Serial number: %012u\n", card->serial_number);
	else
		vgsm_msg_card(card, KERN_INFO,
			"Serial number: N/A\n");

	vgsm_msg_card(card, KERN_INFO,
		"GSM me sockets: %d\n",
		card->mes_number);

	vgsm_msg_card(card, KERN_INFO,
		"SIM sockets: %d\n",
		card->sims_number);

	for(i=0; i<card->mes_number; i++) {

		u32 me_status = vgsm_inl(card, VGSM_R_ME_STATUS(i));

		if (me_status & VGSM_R_ME_STATUS_V_VDDLP) {

			u32 fifo_size = vgsm_inl(card, VGSM_R_ME_FIFO_SIZE(i));

			char tmpstr[8];
			snprintf(tmpstr, sizeof(tmpstr), "gsm%d", i);

			card->mes[i] = vgsm_me_create(
						NULL, card, i, tmpstr,
						VGSM_FIFO_RX_BASE(i),
						VGSM_R_ME_FIFO_SIZE_V_RX_SIZE
								(fifo_size),
						VGSM_FIFO_TX_BASE(i),
						VGSM_R_ME_FIFO_SIZE_V_TX_SIZE
								(fifo_size),
						VGSM_ME_ASC0_BASE(i),
						VGSM_ME_ASC1_BASE(i),
						VGSM_ME_SIM_BASE(i));
			if (!card->mes[i]) {
				err = -ENOMEM;
				goto err_me_create;
			}

			{
			u32 me_status = vgsm_inl(card,
					VGSM_R_ME_STATUS(card->mes[i]->id));

			vgsm_msg_card(card, KERN_INFO,
				"ME %d is installed and powered %s\n", i,
				me_status & VGSM_R_ME_STATUS_V_VDD ?
							"ON" : "OFF");

			if (me_status & VGSM_R_ME_STATUS_V_VDD) {
				vgsm_msg_card(card, KERN_NOTICE,
					"ME %d is being shut down\n", i);

				vgsm_outl(card, VGSM_R_ME_SETUP(
							card->mes[i]->id),
						VGSM_R_ME_SETUP_V_EMERG_OFF);
			}
			}

		} else {
			vgsm_msg_card(card, KERN_INFO,
				"ME %d is not installed\n", i);
		}
	}

	for(i=0; i<card->sims_number; i++)
		vgsm_sim_create(&card->sims[i], card, i, VGSM_SIM_UART_BASE(i));

	vgsm_initialize_hw(card);

	set_bit(VGSM_CARD_FLAGS_READY, &card->flags);

	cdev_init(&card->cdev, &vgsm_card_fops);
	card->cdev.owner = THIS_MODULE;

	err = cdev_add(&card->cdev, vgsm_card_first_dev + card->id, 1);
	if (err < 0)
		goto err_cdev_add;

	card->device.class = &vgsm_card_class;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	card->device.dev = NULL;
	snprintf(card->device.class_id,
		sizeof(card->device.class_id),
		"vgsm2_card%d", card->id);
#else
	snprintf(card->device.bus_id,
		sizeof(card->device.bus_id),
		"vgsm2_card%d", card->id);
#endif

#ifdef HAVE_CLASS_DEV_DEVT
	card->device.devt = vgsm_card_first_dev + card->id;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	err = class_device_register(&card->device);
	if (err < 0)
		goto err_device_register;
#else
	err = device_register(&card->device);
	if (err < 0)
		goto err_device_register;
#endif

#ifndef HAVE_CLASS_DEV_DEVT
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	err = class_device_create_file(
		&card->device,
		&class_device_attr_dev);
	if (err < 0)
		goto err_device_create_file;
#else
	err = device_create_file(
		&card->device,
		&device_attr_dev);
	if (err < 0)
		goto err_device_create_file;
#endif
#endif

	return 0;

	cdev_del(&card->cdev);
err_cdev_add:
#ifndef HAVE_CLASS_DEV_DEVT
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	class_device_remove_file(
		&card->device,
		&class_device_attr_dev);
#else
	device_remove_file(
		&card->device,
		&device_attr_dev);
#endif
err_device_create_file:
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	class_device_unregister(&card->device);
#else
	device_unregister(&card->device);
#endif
err_device_register:
err_me_create:
	for(i=card->mes_number-1; i>=0; i--) {
		if (card->mes[i])
			vgsm_me_destroy(card->mes[i]);
	}
err_mes_number_invalid:
err_sims_number_invalid:
err_card_not_responding:
	free_irq(card->pci_dev->irq, card);
err_request_irq:
	iounmap(card->fifo_mem);
err_ioremap_fifo:
err_no_fifo_base:
	iounmap(card->regs_mem);
err_ioremap_regs:
err_no_regs_base:
err_noirq:
	pci_release_regions(card->pci_dev);
err_pci_request_regions:
err_pci_enable_device:

	return err;
}

void vgsm_card_remove(struct vgsm_card *card)
{
	int i;

	/* Clean up any allocated resources and stuff here */

	cdev_del(&card->cdev);

#ifndef HAVE_CLASS_DEV_DEVT
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	class_device_remove_file(
		&card->device,
		&class_device_attr_dev);
#else
	device_remove_file(
		&card->device,
		&device_attr_dev);
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	class_device_unregister(&card->device);
#else
	device_unregister(&card->device);
#endif

	vgsm_msg_card(card, KERN_INFO,
		"shutting down card at %p.\n", card->regs_mem);

	set_bit(VGSM_CARD_FLAGS_SHUTTING_DOWN, &card->flags);

	for(i=0; i<card->mes_number; i++) {
		if (card->mes[i]) {
			struct vgsm_me *me = card->mes[i];

			u32 me_status =
				vgsm_inl(card, VGSM_R_ME_STATUS(me->id));

			if (me_status & VGSM_R_ME_STATUS_V_VDD) {
				vgsm_msg_card(card, KERN_NOTICE,
					"ME %d has not been shut down,"
					" forcing emergency shutdown\n",
					me->id);

				vgsm_outl(card, VGSM_R_ME_SETUP(me->id),
						VGSM_R_ME_SETUP_V_EMERG_OFF);
			}
		}
	}

	clear_bit(VGSM_CARD_FLAGS_READY, &card->flags);

	/* Disable IRQs */
	vgsm_outl(card, VGSM_R_INT_ENABLE, 0);

	if (test_bit(VGSM_CARD_FLAGS_RECONFIG_PENDING, &card->flags)) {

		vgsm_msg_card(card, KERN_INFO,
			"Reconfiguring FPGA. PCI rescan or reboot required\n");

		vgsm_outl(card,
			VGSM_R_SERVICE,
			VGSM_R_SERVICE_V_RECONFIG);
	}

	pci_write_config_word(card->pci_dev, PCI_COMMAND, 0);

	/* Free IRQ */
	free_irq(card->pci_dev->irq, card);

	/* Unmap */
	iounmap(card->fifo_mem);
	iounmap(card->regs_mem);

	pci_release_regions(card->pci_dev);

	pci_disable_device(card->pci_dev);
}

int vgsm_card_register(struct vgsm_card *card)
{
	int err;
	int i;

	spin_lock(&vgsm_cards_list_lock);
	list_add_tail(&card->cards_list_node, &vgsm_cards_list);
	spin_unlock(&vgsm_cards_list_lock);

	for (i=0; i<card->sims_number; i++) {
		err = vgsm_sim_register(&card->sims[i]);
		if (err < 0) {
			vgsm_msg_card(card, KERN_ERR,
				"SIM registration failed: %d\n", err);
			goto err_register_sim;
		}
	}

	for (i=0; i<card->mes_number; i++) {
		if (card->mes[i]) {
			err = vgsm_me_register(card->mes[i]);
			if (err < 0) {
				vgsm_msg_card(card, KERN_ERR,
					"ME registration failed: %d\n", err);
				goto err_me_register;
			}
		}
	}

	err = vgsm_card_sysfs_create_files(card);
	if (err < 0) {
		vgsm_msg_card(card, KERN_ERR,
			"sysfs files creation failed: %d\n", err);
		goto err_card_sysfs_create_files;
	}

	return 0;

	vgsm_card_sysfs_delete_files(card);
err_card_sysfs_create_files:
err_register_sim:
	for(--i; i>=0; i--)
		vgsm_sim_unregister(&card->sims[i]);
err_me_register:
	for(--i; i>=0; i--) {
		if (card->mes[i])
			vgsm_me_unregister(card->mes[i]);
	}

	return err;
}

void vgsm_card_unregister(struct vgsm_card *card)
{
	int i;

	vgsm_card_sysfs_delete_files(card);

	spin_lock(&vgsm_cards_list_lock);
	list_del(&card->cards_list_node);
	spin_unlock(&vgsm_cards_list_lock);

	for(i=card->sims_number-1; i>=0; i--)
		vgsm_sim_unregister(&card->sims[i]);

	for(i=card->mes_number-1; i>=0; i--) {
		if (card->mes[i])
			vgsm_me_unregister(card->mes[i]);
	}

}

int __init vgsm_card_modinit(void)
{
	int err;

	err = class_register(&vgsm_card_class);
	if (err < 0)
		goto err_class_register;

	err = alloc_chrdev_region(&vgsm_card_first_dev, 0, VGSM_MAX_CARDS,
							vgsm_DRIVER_NAME);
	if (err < 0)
		goto err_register_chrdev;

	return 0;

	unregister_chrdev_region(vgsm_card_first_dev, VGSM_MAX_CARDS);
err_register_chrdev:
	class_unregister(&vgsm_card_class);
err_class_register:

	return err;
}

void __exit vgsm_card_modexit(void)
{
	unregister_chrdev_region(vgsm_card_first_dev, VGSM_MAX_CARDS);
	class_unregister(&vgsm_card_class);
}
