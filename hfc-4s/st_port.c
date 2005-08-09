#include <linux/kernel.h>
#include <linux/spinlock.h>

#include "st_port.h"
#include "st_port_inline.h"
#include "card.h"

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
	WARN_ON(!irqs_disabled() && !in_irq());

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

	unsigned long flags;
	spin_lock_irqsave(&port->card->lock, flags);

	hfc_st_port_select(port);
	hfc_st_port__do_set_role(port, nt_mode);

	spin_unlock_irqrestore(&port->card->lock, flags);

	return 0;
}

static int hfc_st_port_enable(
	struct visdn_port *visdn_port)
{
	struct hfc_st_port *port = to_st_port(visdn_port);

	unsigned long flags;
	spin_lock_irqsave(&port->card->lock, flags);

	hfc_st_port_select(port);
	hfc_outb(port->card, hfc_A_ST_WR_STA, 0);

	spin_unlock_irqrestore(&port->card->lock, flags);

	hfc_debug_port(port, 2, "enabled\n");

	return 0;
}

static int hfc_st_port_disable(
	struct visdn_port *visdn_port)
{
	struct hfc_st_port *port = to_st_port(visdn_port);

	unsigned long flags;
	spin_lock_irqsave(&port->card->lock, flags);

	hfc_st_port_select(port);
	hfc_outb(port->card, hfc_A_ST_WR_STA,
		hfc_A_ST_WR_STA_V_ST_SET_STA(0)|
		hfc_A_ST_WR_STA_V_ST_LD_STA);

	spin_unlock_irqrestore(&port->card->lock, flags);

	hfc_debug_port(port, 2, "disabled\n");

	return 0;
}

struct visdn_port_ops hfc_st_port_ops = {
	.set_role	= hfc_st_port_set_role,
	.enable		= hfc_st_port_enable,
	.disable	= hfc_st_port_disable,
};
