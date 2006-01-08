/*
 * Cologne Chip's HFC-S USB vISDN driver
 *
 * Copyright (C) 2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>

#include "card_inline.h"
#include "st_port.h"
#include "st_chan.h"
#include "fifo_inline.h"

static void hfc_st_port_update_led(struct hfc_st_port *port)
{
	struct hfc_card *card = port->card;
	struct hfc_led *usb_led = &card->leds[HFC_LED_USB];
	struct hfc_led *isdn_led = &card->leds[HFC_LED_ISDN];

	if (port->visdn_port.enabled) {
		usb_led->color = HFC_LED_ON;
		usb_led->flashing_freq = 0;

		if ((port->nt_mode &&
		     port->l1_state == 3) ||
		    (!port->nt_mode &&
		     port->l1_state == 7)) {

			isdn_led->color = HFC_LED_ON;
			isdn_led->flashing_freq = 0;

		} else if ((port->nt_mode &&
		            port->l1_state == 1) ||
		           (!port->nt_mode &&
		            port->l1_state == 3)) {

			isdn_led->color = HFC_LED_OFF;
			isdn_led->flashing_freq = 0;
		} else {
			isdn_led->color = HFC_LED_ON;
			isdn_led->flashing_freq = HZ / 10;
			isdn_led->flashes = -1;
		}
	} else {
		usb_led->color = HFC_LED_OFF;
		usb_led->flashing_freq = 0;

		isdn_led->color = HFC_LED_OFF;
		isdn_led->flashing_freq = 0;
	}

	hfc_led_update(usb_led);
	hfc_led_update(isdn_led);
}

//----------------------------------------------------------------------------

static ssize_t hfc_show_role(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_st_port *port = to_st_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%s\n",
		port->nt_mode ? "NT" : "TE");
}

static ssize_t hfc_store_role(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_st_port *port = to_st_port(visdn_port);
	struct hfc_card *card = port->card;

	if (count < 2)
		return count;

	hfc_card_lock(card);

	if (!strncmp(buf, "NT", 2) && !port->nt_mode) {
		port->nt_mode = TRUE;
		port->clock_delay = HFC_DEF_NT_CLK_DLY;
		port->sampling_comp = HFC_DEF_NT_SAMPL_COMP;

		hfc_st_port_update_sctrl(port);
		hfc_st_port_update_st_clk_dly(port);

	} else if (!strncmp(buf, "TE", 2) && port->nt_mode) {
		port->nt_mode = FALSE;
		port->clock_delay = HFC_DEF_TE_CLK_DLY;
		port->sampling_comp = HFC_DEF_TE_SAMPL_COMP;

		hfc_st_port_update_sctrl(port);
		hfc_st_port_update_st_clk_dly(port);
	}

	hfc_card_unlock(card);

	hfc_debug_port(port, 1,
		"role set to %s\n",
		port->nt_mode ? "NT" : "TE");

	return count;
}

static VISDN_PORT_ATTR(role, S_IRUGO | S_IWUSR,
		hfc_show_role,
		hfc_store_role);

//----------------------------------------------------------------------------

static ssize_t hfc_show_l1_state(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_st_port *port = to_st_port(visdn_port);

	u8 l1_state = HFC_REG_STATES_STATE_READVAL(
			hfc_read(port->card, HFC_REG_STATES));

	return snprintf(buf, PAGE_SIZE, "%c%d\n",
		port->nt_mode ? 'G' : 'F',
		l1_state);
}

static ssize_t hfc_store_l1_state(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_st_port *port = to_st_port(visdn_port);
	struct hfc_card *card = port->card;
	int err;

	hfc_card_lock(card);

	if (count >= 8 && !strncmp(buf, "activate", 8)) {
		hfc_write(card, HFC_REG_STATES,
			HFC_REG_STATES_START_ACTIVATION|
			HFC_REG_STATES_NT_G2_G3);
	} else if (count >= 10 && !strncmp(buf, "deactivate", 10)) {
		hfc_write(card, HFC_REG_STATES,
			HFC_REG_STATES_START_DEACTIVATION);
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

		hfc_write(card, HFC_REG_STATES,
			HFC_REG_STATES_STATE_VAL(state) |
			HFC_REG_STATES_LOAD_STATE);
	}

	hfc_card_unlock(card);

	return count;

err_invalid_scanf:
err_invalid_state:

	hfc_card_unlock(card);

	return err;
}

static VISDN_PORT_ATTR(l1_state, S_IRUGO | S_IWUSR,
		hfc_show_l1_state,
		hfc_store_l1_state);

//----------------------------------------------------------------------------

static ssize_t hfc_show_st_clock_delay(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_st_port *port = to_st_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%02x\n", port->clock_delay);
}

static ssize_t hfc_store_st_clock_delay(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_st_port *port = to_st_port(visdn_port);
	struct hfc_card *card = port->card;

	unsigned int value;
	if (sscanf(buf, "%02x", &value) < 1)
		return -EINVAL;

	if (value > 0x0f)
		return -EINVAL;

	hfc_card_lock(card);
	port->clock_delay = value;
	hfc_st_port_update_st_clk_dly(port);
	hfc_card_unlock(card);

	return count;
}

static VISDN_PORT_ATTR(st_clock_delay, S_IRUGO | S_IWUSR,
		hfc_show_st_clock_delay,
		hfc_store_st_clock_delay);

//----------------------------------------------------------------------------
static ssize_t hfc_show_st_sampling_comp(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_st_port *port = to_st_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%02x\n", port->sampling_comp);
}

static ssize_t hfc_store_st_sampling_comp(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	const char *buf,
	size_t count)
{
	struct hfc_st_port *port = to_st_port(visdn_port);
	struct hfc_card *card = port->card;

	unsigned int value;
	if (sscanf(buf, "%u", &value) < 1)
		return -EINVAL;

	if (value > 0x7)
		return -EINVAL;

	hfc_card_lock(card);
	port->sampling_comp = value;
	hfc_st_port_update_st_clk_dly(port);
	hfc_card_unlock(card);

	return count;
}

static VISDN_PORT_ATTR(st_sampling_comp, S_IRUGO | S_IWUSR,
		hfc_show_st_sampling_comp,
		hfc_store_st_sampling_comp);

/*---------------------------------------------------------------------------*/

static ssize_t hfc_show_fifo_state(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_st_port *port = to_st_port(visdn_port);
	struct hfc_card *card = port->card;
	int len = 0;
	int i;


	len += snprintf(buf + len, PAGE_SIZE - len,
		"\n       Receive                      Transmit\n"
		"FIFO#  F1 F2 Z1 Z2 Used Mode    F1 F2 Z1 Z2"
		" Used Mode Connected\n");

	hfc_card_lock(card);

	for (i=0; i<ARRAY_SIZE(card->st_port.chans); i++) {

		u8 rx_f1, rx_f2, rx_z1, rx_z2;
		u8 tx_f1, tx_f2, tx_z1, tx_z2;

		if (!card->st_port.chans[i].has_real_fifo)
			continue;

		len += snprintf(buf + len, PAGE_SIZE - len,
			"%2d     ", i);

		hfc_fifo_select(&card->st_port.chans[i].rx_fifo);

		rx_f1 = hfc_read(card, HFC_REG_FIF_F1);
		rx_f2 = hfc_read(card, HFC_REG_FIF_F2);
		rx_z1 = hfc_read(card, HFC_REG_FIF_Z1);
		rx_z2 = hfc_read(card, HFC_REG_FIF_Z2);

		hfc_fifo_select(&card->st_port.chans[i].tx_fifo);

		tx_f1 = hfc_read(card, HFC_REG_FIF_F1);
		tx_f2 = hfc_read(card, HFC_REG_FIF_F2);
		tx_z1 = hfc_read(card, HFC_REG_FIF_Z1);
		tx_z2 = hfc_read(card, HFC_REG_FIF_Z2);

		len += snprintf(buf + len, PAGE_SIZE - len,
			"%02x %02x %02x %02x        "
			"%02x %02x %02x %02x\n",
			rx_f1, rx_f2, rx_z1, rx_z2,
			tx_f1, tx_f2, tx_z1, tx_z2);
	}

	hfc_card_unlock(card);

	return len;
}

static VISDN_PORT_ATTR(fifo_state, S_IRUGO,
	hfc_show_fifo_state,
	NULL);


static struct visdn_port_attribute *hfc_st_port_attributes[] =
{
	&visdn_port_attr_role,
	&visdn_port_attr_l1_state,
	&visdn_port_attr_st_clock_delay,
	&visdn_port_attr_st_sampling_comp,
	&visdn_port_attr_fifo_state,
	NULL
};

void hfc_st_port_update_sctrl(struct hfc_st_port *port)
{
	u8 sctrl = 0;

	/* Select the non-capacitive line mode for the S/T interface */ 
	sctrl = HFC_REG_SCTRL_NON_CAPACITIVE;

	if (port->nt_mode)
		sctrl |= HFC_REG_SCTRL_NT;
	else
		sctrl |= HFC_REG_SCTRL_TE;

	if (port->sq_enabled)
		sctrl |= HFC_REG_SCTRL_SQ_ENA;

	if (hfc_fifo_is_running(&port->chans[B1].tx_fifo))
		sctrl |= HFC_REG_SCTRL_B1_ENA;

	if (hfc_fifo_is_running(&port->chans[B2].tx_fifo))
		sctrl |= HFC_REG_SCTRL_B2_ENA;

	hfc_write(port->card, HFC_REG_SCTRL, sctrl);
}

void hfc_st_port_update_sctrl_r(struct hfc_st_port *port)
{
	u8 sctrl_r = 0;

	if (hfc_fifo_is_running(&port->chans[B1].rx_fifo))
		sctrl_r |= HFC_REG_SCTRL_R_B1_ENA;

	if (hfc_fifo_is_running(&port->chans[B2].rx_fifo))
		sctrl_r |= HFC_REG_SCTRL_R_B2_ENA;

	hfc_write(port->card, HFC_REG_SCTRL_R, sctrl_r);
}

void hfc_st_port_update_st_clk_dly(struct hfc_st_port *port)
{
	hfc_write(port->card, HFC_REG_CLKDEL,
		HFC_REG_CLKDEL_ST_CLK_DLY(port->clock_delay) |
		HFC_REG_CLKDEL_ST_SMPL(port->sampling_comp));
}

static void hfc_st_port_state_change_work(void *data)
{
	struct hfc_st_port *port = data;
	struct hfc_card *card = port->card;
	u8 new_state;
	int active;

	hfc_card_lock(card);

	new_state = HFC_REG_STATES_STATE_READVAL(hfc_read(card,
						HFC_REG_STATES));

	hfc_debug_port(port, 1,
			"layer 1 state = %c%d\n",
			port->nt_mode?'G':'F',
			new_state);

	if (port->nt_mode) {

		active = 3;

		if (new_state == 2)
			hfc_write(card, HFC_REG_STATES,
				HFC_REG_STATES_NT_G2_G3);
	} else {
		active = 7;
	}

	if (new_state == active && port->l1_state != active) {
		/* Layer 1 is now active, schedule FIFO activation after
		 * 50ms, otherwise the first frame gets corrupted. This is
		 * not documented on Cologne Chip's specs.
		 */

		schedule_delayed_work(&port->fifo_activation_work, 50 * HZ / 1000);

	} else if (new_state != active && port->l1_state == active) {

		schedule_work(&port->fifo_activation_work);
	}

	port->l1_state = new_state;

	hfc_st_port_update_led(port);

	hfc_card_unlock(card);
}

static void hfc_st_port_fifo_activation_work(void *data)
{
	struct hfc_st_port *port = data;
	struct hfc_card *card = port->card;
	int i;

	hfc_card_lock(card);

	for (i=0; i<ARRAY_SIZE(port->chans); i++) {
		if (port->chans[i].has_real_fifo) {
			hfc_fifo_select(&port->chans[i].rx_fifo);
			hfc_fifo_configure(&port->chans[i].rx_fifo);

			hfc_fifo_select(&port->chans[i].tx_fifo);
			hfc_fifo_configure(&port->chans[i].tx_fifo);
		}
	}

	hfc_card_unlock(card);
}

static void hfc_st_port_activate_request_work(void *data)
{
	struct hfc_st_port *port = data;

	hfc_write(port->card, HFC_REG_STATES,
		HFC_REG_STATES_START_ACTIVATION);
}

void hfc_st_port_check_l1_up(struct hfc_st_port *port)
{
	if (port->visdn_port.enabled &&
		((!port->nt_mode && port->l1_state != 7) ||
		(port->nt_mode && port->l1_state != 3))) {

		hfc_debug_port(port, 1,
			"L1 is down, bringing up L1.\n");

		schedule_work(&port->activate_request_work);
	}
}

static void hfc_st_port_release(
	struct visdn_port *port)
{
	printk(KERN_DEBUG "hfc_st_port_release()\n");

	// FIXME
}

static int hfc_st_port_enable(
	struct visdn_port *visdn_port)
{
	struct hfc_st_port *port = to_st_port(visdn_port);

	hfc_write(port->card, HFC_REG_STATES,
		HFC_REG_STATES_STATE_VAL(0));

	hfc_st_port_update_led(port);

	hfc_debug_port(port, 2, "enabled\n");

	return 0;
}

static int hfc_st_port_disable(
	struct visdn_port *visdn_port)
{
	struct hfc_st_port *port = to_st_port(visdn_port);

	hfc_write(port->card, HFC_REG_STATES,
		HFC_REG_STATES_STATE_VAL(0) |
		HFC_REG_STATES_LOAD_STATE);

	hfc_st_port_update_led(port);

	hfc_debug_port(port, 2, "disabled\n");

	return 0;
}

struct visdn_port_ops hfc_st_port_ops = {
	.owner		= THIS_MODULE,
	.release	= hfc_st_port_release,
	.enable		= hfc_st_port_enable,
	.disable	= hfc_st_port_disable,
};

void hfc_st_port_init(
	struct hfc_st_port *port,
	struct hfc_card *card,
	const char *name)
{
	port->card = card;

	INIT_WORK(&port->state_change_work,
		hfc_st_port_state_change_work,
		port);

	INIT_WORK(&port->fifo_activation_work,
		hfc_st_port_fifo_activation_work,
		port);

	INIT_WORK(&port->activate_request_work,
		hfc_st_port_activate_request_work,
		port);

	port->nt_mode = FALSE;
	port->clock_delay = HFC_DEF_TE_CLK_DLY;
	port->sampling_comp = HFC_DEF_TE_SAMPL_COMP;

	visdn_port_init(&port->visdn_port);
	port->visdn_port.ops = &hfc_st_port_ops;
	port->visdn_port.driver_data = port;
	port->visdn_port.device = &card->usb_dev->dev;
	strncpy(port->visdn_port.name, name, sizeof(port->visdn_port.name));

	hfc_st_chan_init(&port->chans[D], port, "D", D, 1,
			&card->leds[HFC_LED_ISDN], 2);
	hfc_st_chan_init(&port->chans[B1], port, "B1", B1, 1,
			&card->leds[HFC_LED_B1], 8);
	hfc_st_chan_init(&port->chans[B2], port, "B2", B2, 1,
			&card->leds[HFC_LED_B2], 8);
	hfc_st_chan_init(&port->chans[E], port, "E", E, 0,
			&card->leds[HFC_LED_ISDN], 2);
	hfc_st_chan_init(&port->chans[SQ], port, "SQ", SQ, 0,
			&card->leds[HFC_LED_ISDN], 0);
}

int hfc_st_port_register(struct hfc_st_port *port)
{
	int err;

	err = visdn_port_register(&port->visdn_port);
	if (err < 0)
		goto err_port_register;

	err = hfc_st_chan_register(&port->chans[D]);
	if (err < 0)
		goto err_chan_register_D;

	err = hfc_st_chan_register(&port->chans[B1]);
	if (err < 0)
		goto err_chan_register_B1;

	err = hfc_st_chan_register(&port->chans[B2]);
	if (err < 0)
		goto err_chan_register_B2;

	err = hfc_st_chan_register(&port->chans[E]);
	if (err < 0)
		goto err_chan_register_E;

	err = hfc_st_chan_register(&port->chans[SQ]);
	if (err < 0)
		goto err_chan_register_SQ;

	{
	struct visdn_port_attribute **attr = hfc_st_port_attributes;

	while(*attr) {
		visdn_port_create_file(
			&port->visdn_port,
			*attr);

		attr++;
	}
	}

	return 0;

	hfc_st_chan_unregister(&port->chans[SQ]);
err_chan_register_SQ:
	hfc_st_chan_unregister(&port->chans[E]);
err_chan_register_E:
	hfc_st_chan_unregister(&port->chans[B2]);
err_chan_register_B2:
	hfc_st_chan_unregister(&port->chans[B1]);
err_chan_register_B1:
	hfc_st_chan_unregister(&port->chans[D]);
err_chan_register_D:
	visdn_port_unregister(&port->visdn_port);
err_port_register:

	return err;
}

void hfc_st_port_unregister(struct hfc_st_port *port)
{
	struct visdn_port_attribute **attr = hfc_st_port_attributes;

	while(*attr) {
		visdn_port_remove_file(
			&port->visdn_port,
			*attr);

		attr++;
	}

	hfc_st_chan_unregister(&port->chans[SQ]);
	hfc_st_chan_unregister(&port->chans[E]);
	hfc_st_chan_unregister(&port->chans[B2]);
	hfc_st_chan_unregister(&port->chans[B1]);
	hfc_st_chan_unregister(&port->chans[D]);

	visdn_port_unregister(&port->visdn_port);
}
