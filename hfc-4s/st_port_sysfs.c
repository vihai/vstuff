#include <linux/kernel.h>
#include <linux/spinlock.h>

#include "st_port.h"
#include "st_port_inline.h"
#include "st_port_sysfs.h"
#include "card.h"

static ssize_t hfc_show_role(
	struct device *device,
	char *buf)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_st_port *port = to_st_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%s\n",
		port->nt_mode? "NT" : "TE");
}

static ssize_t hfc_store_role(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_st_port *port = to_st_port(visdn_port);

	if (count < 2)
		return count;

	if (down_interruptible(&port->card->sem))
		return -ERESTARTSYS;

	hfc_st_port_select(port);

	if (!strncmp(buf, "NT", 2) && !port->nt_mode) {
		port->regs.st_ctrl_0 =
			hfc_A_ST_CTRL0_V_ST_MD_NT;
		hfc_outb(port->card, hfc_A_ST_CTRL0,
			port->regs.st_ctrl_0);

		port->clock_delay = 0x0C;
		port->sampling_comp = 0x6;

		hfc_outb(port->card, hfc_A_ST_CLK_DLY,
			hfc_A_ST_CLK_DLY_V_ST_CLK_DLY(port->clock_delay)|
			hfc_A_ST_CLK_DLY_V_ST_SMPL(port->sampling_comp));

		port->nt_mode = TRUE;
	} else if (!strncmp(buf, "TE", 2) && port->nt_mode) {
		port->regs.st_ctrl_0 =
			hfc_A_ST_CTRL0_V_ST_MD_TE;
		hfc_outb(port->card, hfc_A_ST_CTRL0,
			port->regs.st_ctrl_0);

		port->clock_delay = 0x0E;
		port->sampling_comp = 0x6;

		hfc_outb(port->card, hfc_A_ST_CLK_DLY,
			hfc_A_ST_CLK_DLY_V_ST_CLK_DLY(port->clock_delay)|
			hfc_A_ST_CLK_DLY_V_ST_SMPL(port->sampling_comp));

		port->nt_mode = FALSE;
	}

	up(&port->card->sem);

	return count;
}

static DEVICE_ATTR(role, S_IRUGO | S_IWUSR,
		hfc_show_role,
		hfc_store_role);

//----------------------------------------------------------------------------

static ssize_t hfc_show_l1_state(
	struct device *device,
	char *buf)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_st_port *port = to_st_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%c%d\n",
		port->nt_mode?'G':'F',
		port->l1_state);
}

static ssize_t hfc_store_l1_state(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_st_port *port = to_st_port(visdn_port);
	struct hfc_card *card = port->card;
	int err;

	if (down_interruptible(&port->card->sem))
		return -ERESTARTSYS;

	hfc_st_port_select(port);

	if (count >= 8 && !strncmp(buf, "activate", 8)) {
		hfc_outb(card, hfc_A_ST_WR_STA,
			hfc_A_ST_WR_STA_V_ST_ACT_ACTIVATION|
			hfc_A_ST_WR_STA_V_SET_G2_G3);
	} else if (count >= 10 && !strncmp(buf, "deactivate", 10)) {
		hfc_outb(card, hfc_A_ST_WR_STA,
			hfc_A_ST_WR_STA_V_ST_ACT_DEACTIVATION|
			hfc_A_ST_WR_STA_V_SET_G2_G3);
	} else {
		int state;
		if (sscanf(buf, "%d", &state) < 1) {
			err = -EINVAL;
			goto err_invalid_scanf;
		}

		if (state < 0 ||
		    (port->nt_mode && state > 7) ||
		    (!port->nt_mode && state > 3)) {
			err = -EINVAL;
			goto err_invalid_state;
		}

		hfc_outb(card, hfc_A_ST_WR_STA,
			hfc_A_ST_WR_STA_V_ST_SET_STA(state));
	}

	up(&card->sem);

	return count;

err_invalid_scanf:
err_invalid_state:

	up(&card->sem);

	return err;
}

static DEVICE_ATTR(l1_state, S_IRUGO | S_IWUSR,
		hfc_show_l1_state,
		hfc_store_l1_state);

//----------------------------------------------------------------------------

static ssize_t hfc_show_st_clock_delay(
	struct device *device,
	char *buf)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_st_port *port = to_st_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%02x\n", port->clock_delay);
}

static void hfc_update_st_clk_dly(struct hfc_st_port *port)
{
	u8 st_clk_dly;

	WARN_ON(atomic_read(&port->card->sem.count) > 0);

	st_clk_dly = hfc_A_ST_CLK_DLY_V_ST_CLK_DLY(port->clock_delay) |
		     hfc_A_ST_CLK_DLY_V_ST_SMPL(port->sampling_comp);

	hfc_outb(port->card, hfc_A_ST_CLK_DLY, st_clk_dly);
}

static ssize_t hfc_store_st_clock_delay(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_st_port *port = to_st_port(visdn_port);
	struct hfc_card *card = port->card;

	unsigned int value;
	if (sscanf(buf, "%02x", &value) < 1)
		return -EINVAL;

	if (value > 0x0f)
		return -EINVAL;

	if (down_interruptible(&port->card->sem))
		return -ERESTARTSYS;
	port->clock_delay = value;
	hfc_update_st_clk_dly(port);
	up(&card->sem);

	return count;
}

static DEVICE_ATTR(st_clock_delay, S_IRUGO | S_IWUSR,
		hfc_show_st_clock_delay,
		hfc_store_st_clock_delay);

//----------------------------------------------------------------------------
static ssize_t hfc_show_st_sampling_comp(
	struct device *device,
	char *buf)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_st_port *port = to_st_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%02x\n", port->sampling_comp);
}

static ssize_t hfc_store_st_sampling_comp(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct visdn_port *visdn_port = to_visdn_port(device);
	struct hfc_st_port *port = to_st_port(visdn_port);
	struct hfc_card *card = port->card;

	unsigned int value;
	if (sscanf(buf, "%u", &value) < 1)
		return -EINVAL;

	if (value > 0x7)
		return -EINVAL;

	if (down_interruptible(&port->card->sem))
		return -ERESTARTSYS;
	port->sampling_comp = value;
	hfc_update_st_clk_dly(port);
	up(&card->sem);

	return count;
}

static DEVICE_ATTR(st_sampling_comp, S_IRUGO | S_IWUSR,
		hfc_show_st_sampling_comp,
		hfc_store_st_sampling_comp);

int hfc_st_port_sysfs_create_files(
        struct hfc_st_port *port)
{
	int err;

	err = device_create_file(
		&port->visdn_port.device,
		&dev_attr_role);
	if (err < 0)
		goto err_device_create_file_role;

	err = device_create_file(
		&port->visdn_port.device,
		&dev_attr_l1_state);
	if (err < 0)
		goto err_device_create_file_l1_state;

	err = device_create_file(
		&port->visdn_port.device,
		&dev_attr_st_clock_delay);
	if (err < 0)
		goto err_device_create_file_st_clock_delay;

	err = device_create_file(
		&port->visdn_port.device,
		&dev_attr_st_sampling_comp);
	if (err < 0)
		goto err_device_create_file_st_sampling_comp;

	return 0;

	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_role);
err_device_create_file_role:
	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_st_sampling_comp);
err_device_create_file_st_sampling_comp:
	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_st_clock_delay);
err_device_create_file_st_clock_delay:
	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_l1_state);
err_device_create_file_l1_state:

	return err;
}

void hfc_st_port_sysfs_delete_files(
        struct hfc_st_port *port)
{
	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_role);
	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_st_sampling_comp);
	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_st_clock_delay);
	device_remove_file(
		&port->visdn_port.device,
		&dev_attr_l1_state);
}
