#include <linux/kernel.h>

#include "st_port.h"
#include "card.h"
#include "card_inline.h"


void hfc_st_port_state_change_work(void *data)
{
	struct hfc_st_port *port = data;
	struct hfc_card *card = port->card;

	down(&card->sem);

	u8 new_state = hfc_inb(card, hfc_STATES) & hfc_STATES_STATE_MASK;

	hfc_debug_port(port, 1,
			"layer 1 state = %c%d\n",
			port->nt_mode?'G':'F',
			new_state);

	if (port->nt_mode) {
		// NT mode

		if (new_state == 2) {
			// Allows transition from G2 to G3
			hfc_outb(card, hfc_STATES,
				hfc_STATES_ACTIVATE |
				hfc_STATES_NT_G2_G3);
		} else if (new_state == 3) {
			// fix to G3 state (see specs)
			hfc_outb(card, hfc_STATES, hfc_STATES_LOAD_STATE | 3);
		}

		if (new_state == 3 && port->l1_state != 3) {
			//hfc_resume_fifo(card);
		}

		if (new_state != 3 && port->l1_state == 3) {
			//hfc_suspend_fifo(card);
		}
	} else {
		if (new_state == 3) {
		}

		if (new_state == 7 && port->l1_state != 7) {
			// TE is now active, schedule FIFO activation after
			// some time, otherwise the first frames are lost

			card->regs.ctmt |= hfc_CTMT_TIMER_50 | hfc_CTMT_TIMER_CLEAR;
			hfc_outb(card, hfc_CTMT, card->regs.ctmt);

			// Activating the timer firest an interrupt immediately, we
			// obviously need to ignore it
			card->ignore_first_timer_interrupt = TRUE;
		}

		if (new_state != 7 && port->l1_state == 7) {
			// TE has become inactive, disable FIFO
			//hfc_suspend_fifo(card);
		}
	}

	port->l1_state = new_state;

	up(&card->sem);
}

void hfc_st_port_check_l1_up(struct hfc_st_port *port)
{
	struct hfc_card *card = port->card;

	if (port->visdn_port.enabled &&
		((!port->nt_mode && port->l1_state != 7) ||
		(port->nt_mode && port->l1_state != 3))) {

		hfc_debug_port(port, 1,
			"L1 is down, bringing up L1.\n");

       		hfc_outb(card, hfc_STATES, hfc_STATES_ACTIVATE);
       	}
}

void hfc_update_st_clk_dly(struct hfc_st_port *port)
{
	u8 st_clk_dly;

	st_clk_dly =
		hfc_CLKDEL_ST_CLK_DLY(port->clock_delay) |
		hfc_CLKDEL_ST_SMPL(port->sampling_comp);

	hfc_outb(port->card, hfc_CLKDEL, st_clk_dly);
}

static int hfc_st_port_enable(
	struct visdn_port *visdn_port)
{
	struct hfc_st_port *port = to_st_port(visdn_port);

	hfc_outb(port->card, hfc_STATES,
		hfc_STATES_STATE(0));

	hfc_debug_port(port, 2, "enabled\n");

	return 0;
}

static int hfc_st_port_disable(
	struct visdn_port *visdn_port)
{
	struct hfc_st_port *port = to_st_port(visdn_port);

	hfc_outb(port->card, hfc_STATES,
		hfc_STATES_STATE(0) |
		hfc_STATES_LOAD_STATE);

	hfc_debug_port(port, 2, "disabled\n");

	return 0;
}

struct visdn_port_ops hfc_st_port_ops = {
	.enable		= hfc_st_port_enable,
	.disable	= hfc_st_port_disable,
};
