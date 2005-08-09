#include <linux/kernel.h>
#include <linux/spinlock.h>

#include "st_port.h"
#include "card.h"
#include "card_inline.h"

void hfc_st_port_check_l1_up(struct hfc_st_port *port)
{
	struct hfc_card *card = port->card;

	if (port->visdn_port.enabled &&
		((!port->visdn_port.nt_mode && port->l1_state != 7) ||
		(port->visdn_port.nt_mode && port->l1_state != 3))) {

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

void hfc_st_port__do_set_role(struct hfc_st_port *port, int nt_mode)
{
	if (nt_mode) {
		port->card->regs.sctrl =
			hfc_SCTRL_MODE_NT;
		hfc_outb(port->card, hfc_SCTRL,
			port->card->regs.sctrl);

		port->clock_delay = 0x0C;
		port->sampling_comp = 0x6;
		hfc_update_st_clk_dly(port);
	} else {
		port->card->regs.sctrl =
			hfc_SCTRL_MODE_TE;
		hfc_outb(port->card, hfc_SCTRL,
			port->card->regs.sctrl);

		port->clock_delay = 0x0E;
		port->sampling_comp = 0x6;
		hfc_update_st_clk_dly(port);
	}

	port->visdn_port.nt_mode = nt_mode;
}

static int hfc_st_port_set_role(
	struct visdn_port *visdn_port,
	int nt_mode)
{
	struct hfc_st_port *port = visdn_port->priv;

	hfc_st_port__do_set_role(port, nt_mode);

	return 0;
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
	.set_role	= hfc_st_port_set_role,
	.enable		= hfc_st_port_enable,
	.disable	= hfc_st_port_disable,
};
