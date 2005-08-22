#include <linux/kernel.h>

#include "st_port.h"
#include "st_port_inline.h"
#include "card.h"

void hfc_st_port_state_change_work(void *data)
{
	struct hfc_st_port *port = data;
	struct hfc_card *card = port->card;

	down(&card->sem);

	hfc_st_port_select(port);

	u8 new_state = hfc_A_ST_RD_STA_V_ST_STA(hfc_inb(card, hfc_A_ST_RD_STA));

	hfc_debug_port(port, 1,
		"layer 1 state = %c%d\n",
		port->visdn_port.nt_mode?'G':'F',
		new_state);

	if (port->visdn_port.nt_mode) {
		// NT mode

		if (new_state == 2) {
			// Allows transition from G2 to G3
			hfc_outb(card, hfc_A_ST_WR_STA,
				hfc_A_ST_WR_STA_V_ST_ACT_ACTIVATION|
				hfc_A_ST_WR_STA_V_SET_G2_G3);
		} else if (new_state == 3) {
			// fix to G3 state (see specs) ? Why? TODO FIXME
			hfc_outb(card, hfc_A_ST_WR_STA,
				hfc_A_ST_WR_STA_V_ST_SET_STA(3)|
				hfc_A_ST_WR_STA_V_ST_LD_STA);
		}

		if (new_state == 3 && port->l1_state != 3) {
//			hfc_resume_fifo(card);
		}

		if (new_state != 3 && port->l1_state == 3) {
//			hfc_suspend_fifo(card);
		}
	} else {
		if (new_state == 3) {
//			if (force_l1_up) {
//				hfc_outb(card, hfc_STATES, hfc_STATES_DO_ACTION |
//						hfc_STATES_ACTIVATE);
//			}
		}

		if (new_state == 7 && port->l1_state != 7) {
			// TE is now active, schedule FIFO activation after
			// some time, otherwise the first frames are lost

//			card->regs.ctmt |= hfc_CTMT_TIMER_50 | hfc_CTMT_TIMER_CLEAR;
//			hfc_outb(card, hfc_CTMT, card->regs.ctmt);

			// Activating the timer firest an interrupt immediately, we
			// obviously need to ignore it
		}

		if (new_state != 7 && port->l1_state == 7) {
			// TE has become inactive, disable FIFO
//			hfc_suspend_fifo(card);
		}
	}

	port->l1_state = new_state;

	up(&card->sem);
}

void hfc_st_port_check_l1_up(struct hfc_st_port *port)
{
	struct hfc_card *card = port->card;

	if (port->visdn_port.enabled &&
		((!port->visdn_port.nt_mode && port->l1_state != 7) ||
		(port->visdn_port.nt_mode && port->l1_state != 3))) {

		hfc_debug_port(port, 1,
			"L1 is down, bringing up L1.\n");

		hfc_outb(card, hfc_A_ST_WR_STA,
			hfc_A_ST_WR_STA_V_ST_ACT_ACTIVATION|
			hfc_A_ST_WR_STA_V_SET_G2_G3);

       	}
}

void hfc_st_port__do_set_role(struct hfc_st_port *port, int nt_mode)
{
	WARN_ON(atomic_read(&port->card->sem.count) > 0);

	if (nt_mode) {
		port->regs.st_ctrl_0 =
			hfc_A_ST_CTRL0_V_ST_MD_NT;
		hfc_outb(port->card, hfc_A_ST_CTRL0,
			port->regs.st_ctrl_0);

		port->clock_delay = 0x0C;
		port->sampling_comp = 0x6;

		hfc_outb(port->card, hfc_A_ST_CLK_DLY,
			hfc_A_ST_CLK_DLY_V_ST_CLK_DLY(port->clock_delay)|
			hfc_A_ST_CLK_DLY_V_ST_SMPL(port->sampling_comp));
	} else {
		port->regs.st_ctrl_0 =
			hfc_A_ST_CTRL0_V_ST_MD_TE;
		hfc_outb(port->card, hfc_A_ST_CTRL0,
			port->regs.st_ctrl_0);

		port->clock_delay = 0x0E;
		port->sampling_comp = 0x6;

		hfc_outb(port->card, hfc_A_ST_CLK_DLY,
			hfc_A_ST_CLK_DLY_V_ST_CLK_DLY(port->clock_delay)|
			hfc_A_ST_CLK_DLY_V_ST_SMPL(port->sampling_comp));
	}

	port->visdn_port.nt_mode = nt_mode;
}

static int hfc_st_port_set_role(
	struct visdn_port *visdn_port,
	int nt_mode)
{
	struct hfc_st_port *port = visdn_port->priv;

	if (down_interruptible(&port->card->sem))
		return -ERESTARTSYS;
	hfc_st_port_select(port);
	hfc_st_port__do_set_role(port, nt_mode);
	up(&port->card->sem);

	return 0;
}

static int hfc_st_port_enable(
	struct visdn_port *visdn_port)
{
	struct hfc_st_port *port = to_st_port(visdn_port);

	if (down_interruptible(&port->card->sem))
		return -ERESTARTSYS;
	hfc_st_port_select(port);
	hfc_outb(port->card, hfc_A_ST_WR_STA, 0);
	up(&port->card->sem);

	hfc_debug_port(port, 2, "enabled\n");

	return 0;
}

static int hfc_st_port_disable(
	struct visdn_port *visdn_port)
{
	struct hfc_st_port *port = to_st_port(visdn_port);

	if (down_interruptible(&port->card->sem))
		return -ERESTARTSYS;
	hfc_st_port_select(port);
	hfc_outb(port->card, hfc_A_ST_WR_STA,
		hfc_A_ST_WR_STA_V_ST_SET_STA(0)|
		hfc_A_ST_WR_STA_V_ST_LD_STA);
	up(&port->card->sem);

	hfc_debug_port(port, 2, "disabled\n");

	return 0;
}

struct visdn_port_ops hfc_st_port_ops = {
	.set_role	= hfc_st_port_set_role,
	.enable		= hfc_st_port_enable,
	.disable	= hfc_st_port_disable,
};
