/*
 * Cologne Chip's HFC-S PCI A vISDN driver
 *
 * Copyright (C) 2004-2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>

#include "st_port.h"
#include "card.h"
#include "card_inline.h"

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

	hfc_debug_st_port(port, 1,
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

	u8 l1_state = hfc_inb(port->card, hfc_STATES) & hfc_STATES_STATE_MASK;

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
		hfc_outb(card, hfc_STATES, hfc_STATES_ACTIVATE);
	} else if (count >= 10 && !strncmp(buf, "deactivate", 10)) {
		hfc_outb(card, hfc_STATES, hfc_STATES_DEACTIVATE);
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

		hfc_outb(card, hfc_STATES,
			hfc_STATES_STATE(state) |
			hfc_STATES_LOAD_STATE);
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

static ssize_t hfc_show_timer_t1(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_st_port *port = to_st_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			port->timer_t1_value * 1000 / HZ);
}

static ssize_t hfc_store_timer_t1(
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

	hfc_card_lock(card);
	port->timer_t1_value = value * HZ / 1000;
	hfc_card_unlock(card);

	return count;
}

static VISDN_PORT_ATTR(timer_t1, S_IRUGO | S_IWUSR,
		hfc_show_timer_t1,
		hfc_store_timer_t1);

//----------------------------------------------------------------------------

static ssize_t hfc_show_timer_t3(
	struct visdn_port *visdn_port,
	struct visdn_port_attribute *attr,
	char *buf)
{
	struct hfc_st_port *port = to_st_port(visdn_port);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			port->timer_t3_value * 1000 / HZ);
}

static ssize_t hfc_store_timer_t3(
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

	hfc_card_lock(card);
	port->timer_t3_value = value * HZ / 1000;
	hfc_card_unlock(card);

	return count;
}

static VISDN_PORT_ATTR(timer_t3, S_IRUGO | S_IWUSR,
		hfc_show_timer_t3,
		hfc_store_timer_t3);

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

static struct visdn_port_attribute *hfc_st_port_attributes[] =
{
	&visdn_port_attr_role,
	&visdn_port_attr_l1_state,
	&visdn_port_attr_timer_t1,
	&visdn_port_attr_timer_t3,
	&visdn_port_attr_st_clock_delay,
	&visdn_port_attr_st_sampling_comp,
	NULL
};

void hfc_st_port_update_sctrl(struct hfc_st_port *port)
{
	u8 sctrl = 0;

	// Select the non-capacitive line mode for the S/T interface */
	sctrl = hfc_SCTRL_NONE_CAP;

	if (port->nt_mode)
		sctrl |= hfc_SCTRL_MODE_NT;
	else
		sctrl |= hfc_SCTRL_MODE_TE;

	if (port->sq_enabled)
		sctrl |= hfc_SCTRL_SQ_ENA;

	if (port->chans[B1].tx_fifo &&
	    port->chans[B1].tx_fifo->enabled)
		sctrl |= hfc_SCTRL_B1_ENA;

	if (port->chans[B2].tx_fifo &&
	    port->chans[B2].tx_fifo->enabled)
		sctrl |= hfc_SCTRL_B2_ENA;

	hfc_outb(port->card, hfc_SCTRL, sctrl);
}

void hfc_st_port_update_sctrl_r(struct hfc_st_port *port)
{
	u8 sctrl_r = 0;

	if (port->chans[B1].rx_fifo &&
	    port->chans[B1].rx_fifo->enabled)
		sctrl_r |= hfc_SCTRL_R_B1_ENA;

	if (port->chans[B2].rx_fifo &&
	    port->chans[B2].rx_fifo->enabled)
		sctrl_r |= hfc_SCTRL_R_B2_ENA;

	hfc_outb(port->card, hfc_SCTRL_R, sctrl_r);
}

void hfc_st_port_update_st_clk_dly(struct hfc_st_port *port)
{
	hfc_outb(port->card, hfc_CLKDEL,
		hfc_CLKDEL_ST_CLK_DLY(port->clock_delay) |
		hfc_CLKDEL_ST_SMPL(port->sampling_comp));
}

static void hfc_st_port_state_change_nt(
	struct hfc_st_port *port,
	u8 old_state, u8 new_state)
{
	struct hfc_card *card = port->card;

	hfc_debug_st_port(port, 1,
		"layer 1 state = G%d => G%d\n",
		old_state,
		new_state);

	switch (new_state) {
	case 0:
	case 1:
		/* Do nothing */
	break;

	case 2:
		/* Allow transition from G2 to G3 */
		hfc_outb(card, hfc_STATES, hfc_STATES_NT_G2_G3);

		if (old_state == 3) {
			visdn_port_error_indication(&port->visdn_port);
			visdn_port_deactivated(&port->visdn_port);

			hfc_card_fifo_update(card);
		}
	break;

	case 3:
		visdn_port_activated(&port->visdn_port);

		schedule_delayed_work(&port->fifo_activation_work,
						50 * HZ / 1000);
	break;

	case 4:
		visdn_port_deactivated(&port->visdn_port);
		hfc_card_fifo_update(card);
	break;
	}
}

static void hfc_st_port_state_change_te(
	struct hfc_st_port *port,
	u8 old_state, u8 new_state)
{
	struct hfc_card *card = port->card;

	hfc_debug_st_port(port, 1,
		"layer 1 state = F%d => F%d\n",
		old_state,
		new_state);

	switch (new_state) {
	case 0:
	case 1:
		visdn_port_deactivated(&port->visdn_port);

		hfc_card_fifo_update(card);

		if (old_state != 3)
			visdn_port_disconnected(&port->visdn_port);
	break;

	case 2:
	case 4:
	case 5:
		/* Do nothing */
	break;

	case 3:
		visdn_port_deactivated(&port->visdn_port);

		hfc_card_fifo_update(card);

		if (old_state == 8)
			visdn_port_error_indication(&port->visdn_port);
	break;

	case 6:
	case 8:
		visdn_port_error_indication(&port->visdn_port);
	break;

	case 7:
		visdn_port_activated(&port->visdn_port);

		schedule_delayed_work(&port->fifo_activation_work,
						50 * HZ / 1000);

		if (old_state == 6 || old_state == 8)
			visdn_port_error_indication(&port->visdn_port);
	break;
	}
}

static void hfc_st_port_timer_t1(unsigned long data)
{
	struct hfc_st_port *port = (struct hfc_st_port *)data;
	struct hfc_card *card = port->card;

	hfc_card_lock(card);
	hfc_outb(card, hfc_STATES,
		hfc_STATES_STATE(4) |
		hfc_STATES_LOAD_STATE);
	udelay(6);
	hfc_outb(card, hfc_STATES, 0);
	hfc_card_unlock(card);
}

static void hfc_st_port_timer_t3(unsigned long data)
{
	struct hfc_st_port *port = (struct hfc_st_port *)data;
	struct hfc_card *card = port->card;

	hfc_card_lock(card);
	hfc_outb(card, hfc_STATES,
		hfc_STATES_STATE(3) |
		hfc_STATES_LOAD_STATE);
	udelay(6);
	hfc_outb(card, hfc_STATES, 0);
	hfc_card_unlock(card);
}

/* TODO: use a tasklet here, or better, add a queue of state changes, otherwise
 * we may miss state changes.
 */
static void hfc_st_port_state_change_work(void *data)
{
	struct hfc_st_port *port = data;
	struct hfc_card *card = port->card;
	u8 states;
	u8 old_state;

	hfc_card_lock(card);

	old_state = port->l1_state;

	states = hfc_inb(card, hfc_STATES);
	port->l1_state = states & hfc_STATES_STATE_MASK;

	if (port->nt_mode) {
		hfc_st_port_state_change_nt(port, old_state, port->l1_state);
	} else {
		if (old_state == 7 && port->l1_state == 6 &&
		    states & hfc_STATES_INFO0 &&
		    !port->rechecking_f7_f6) {

			port->rechecking_f7_f6 = TRUE;

			schedule_delayed_work(&port->state_change_work,
							1 * HZ / 1000);
		} else {
			if (port->rechecking_f7_f6 && port->l1_state != 3)
				hfc_st_port_state_change_te(port, 6,
						old_state);

			port->rechecking_f7_f6 = FALSE;

			hfc_st_port_state_change_te(port, old_state,
							port->l1_state);
		}
	}

	hfc_card_unlock(card);
}

static void hfc_st_port_fifo_activation_work(void *data)
{
	struct hfc_st_port *port = data;
	struct hfc_card *card = port->card;

	hfc_card_lock(card);

	hfc_card_fifo_update(card);

	hfc_card_unlock(card);
}

void hfc_st_port_check_l1_up(struct hfc_st_port *port)
{
	struct hfc_card *card = port->card;

	if (port->visdn_port.enabled &&
		((!port->nt_mode && port->l1_state != 7) ||
		(port->nt_mode && port->l1_state != 3))) {

		hfc_debug_st_port(port, 1,
			"L1 is down, bringing up L1.\n");

		hfc_outb(card, hfc_STATES, hfc_STATES_ACTIVATE);
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

	hfc_outb(port->card, hfc_STATES,
		hfc_STATES_STATE(0));

	hfc_debug_st_port(port, 2, "enabled\n");

	return 0;
}

static int hfc_st_port_disable(
	struct visdn_port *visdn_port)
{
	struct hfc_st_port *port = to_st_port(visdn_port);

	hfc_outb(port->card, hfc_STATES,
		hfc_STATES_STATE(0) |
		hfc_STATES_LOAD_STATE);

	hfc_debug_st_port(port, 2, "disabled\n");

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
	memset(port, 0, sizeof(*port));

	port->card = card;

	INIT_WORK(&port->state_change_work,
		hfc_st_port_state_change_work,
		port);

	INIT_WORK(&port->fifo_activation_work,
		hfc_st_port_fifo_activation_work,
		port);

	init_timer(&port->timer_t1);
	port->timer_t1.function = hfc_st_port_timer_t1;
	port->timer_t1.data = (unsigned long)port;
	port->timer_t1_value = 5 * HZ;

	init_timer(&port->timer_t3);
	port->timer_t3.function = hfc_st_port_timer_t3;
	port->timer_t3.data = (unsigned long)port;
	port->timer_t3_value = 5 * HZ;

	port->nt_mode = FALSE;
	port->clock_delay = HFC_DEF_TE_CLK_DLY;
	port->sampling_comp = HFC_DEF_TE_SAMPL_COMP;

	visdn_port_init(&port->visdn_port);
	port->visdn_port.ops = &hfc_st_port_ops;
	port->visdn_port.driver_data = port;
	port->visdn_port.device = &card->pci_dev->dev;
	strncpy(port->visdn_port.name, name, sizeof(port->visdn_port.name));

	hfc_st_chan_init(&port->chans[D], port, "D", D);
	hfc_st_chan_init(&port->chans[B1], port, "B1", B1);
	hfc_st_chan_init(&port->chans[B2], port, "B2", B2);
	hfc_st_chan_init(&port->chans[E], port, "E", E);
	hfc_st_chan_init(&port->chans[SQ], port, "SQ", SQ);

	port->chans[D].visdn_chan.leg_b.mtu = card->fifos[hfc_FIFO_D][TX].size;
	port->chans[B1].visdn_chan.leg_b.mtu = card->fifos[hfc_FIFO_B1][TX].size;
	port->chans[B2].visdn_chan.leg_b.mtu = card->fifos[hfc_FIFO_B2][TX].size;
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
