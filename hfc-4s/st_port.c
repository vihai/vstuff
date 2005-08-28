#include <linux/kernel.h>

#include "st_port.h"
#include "st_port_inline.h"
#include "card.h"

void hfc_st_port_update_st_ctrl_0(struct hfc_st_port *port)
{
	WARN_ON(atomic_read(&port->card->sem.count) > 0);

	u8 st_ctrl_0 = 0;

	if (port->nt_mode)
		st_ctrl_0 |= hfc_A_ST_CTRL0_V_ST_MD_NT;
	else
		st_ctrl_0 |= hfc_A_ST_CTRL0_V_ST_MD_TE;

	if (port->sq_enabled)
		st_ctrl_0 |= hfc_A_ST_CTRL0_V_SQ_EN;

	if (port->chans[B1].status != HFC_CHAN_STATUS_FREE)
		st_ctrl_0 |= hfc_A_ST_CTRL0_V_B1_EN;

	if (port->chans[B2].status != HFC_CHAN_STATUS_FREE)
		st_ctrl_0 |= hfc_A_ST_CTRL0_V_B2_EN;

	hfc_outb(port->card, hfc_A_ST_CTRL0, st_ctrl_0);
}

void hfc_st_port_update_st_ctrl_2(struct hfc_st_port *port)
{
	WARN_ON(atomic_read(&port->card->sem.count) > 0);

	u8 st_ctrl_2 = 0;

	if (port->chans[B1].status != HFC_CHAN_STATUS_FREE)
		st_ctrl_2 |= hfc_A_ST_CTRL2_V_B1_RX_EN;

	if (port->chans[B2].status != HFC_CHAN_STATUS_FREE)
		st_ctrl_2 |= hfc_A_ST_CTRL2_V_B2_RX_EN;

	hfc_outb(port->card, hfc_A_ST_CTRL2, st_ctrl_2);
}

void hfc_st_port_update_st_clk_dly(struct hfc_st_port *port)
{
	WARN_ON(atomic_read(&port->card->sem.count) > 0);

	hfc_outb(port->card, hfc_A_ST_CLK_DLY,
		hfc_A_ST_CLK_DLY_V_ST_CLK_DLY(port->clock_delay) |
		hfc_A_ST_CLK_DLY_V_ST_SMPL(port->sampling_comp));
}

static void hfc_st_port_state_change_work(void *data)
{
	struct hfc_st_port *port = data;
	struct hfc_card *card = port->card;

	hfc_card_lock(card);

	hfc_st_port_select(port);

	u8 new_state = hfc_A_ST_RD_STA_V_ST_STA(hfc_inb(card, hfc_A_ST_RD_STA));

	hfc_debug_port(port, 1,
		"layer 1 state = %c%d\n",
		port->nt_mode?'G':'F',
		new_state);

	if (port->nt_mode) {
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

	hfc_card_unlock(card);
}

void hfc_st_port_check_l1_up(struct hfc_st_port *port)
{
	struct hfc_card *card = port->card;

	if (port->visdn_port.enabled &&
		((!port->nt_mode && port->l1_state != 7) ||
		(port->nt_mode && port->l1_state != 3))) {

		hfc_debug_port(port, 1,
			"L1 is down, bringing up L1.\n");

		hfc_outb(card, hfc_A_ST_WR_STA,
			hfc_A_ST_WR_STA_V_ST_ACT_ACTIVATION|
			hfc_A_ST_WR_STA_V_SET_G2_G3);

       	}
}

static int hfc_st_port_enable(
	struct visdn_port *visdn_port)
{
	struct hfc_st_port *port = to_st_port(visdn_port);
	struct hfc_card *card = port->card;

	if (hfc_card_lock_interruptible(port->card))
		return -ERESTARTSYS;
	hfc_st_port_select(port);
	hfc_outb(port->card, hfc_A_ST_WR_STA, 0);
	hfc_card_unlock(card);

	hfc_debug_port(port, 2, "enabled\n");

	return 0;
}

static int hfc_st_port_disable(
	struct visdn_port *visdn_port)
{
	struct hfc_st_port *port = to_st_port(visdn_port);
	struct hfc_card *card = port->card;

	if (hfc_card_lock_interruptible(port->card))
		return -ERESTARTSYS;
	hfc_st_port_select(port);
	hfc_outb(port->card, hfc_A_ST_WR_STA,
		hfc_A_ST_WR_STA_V_ST_SET_STA(0)|
		hfc_A_ST_WR_STA_V_ST_LD_STA);
	hfc_card_unlock(card);

	hfc_debug_port(port, 2, "disabled\n");

	return 0;
}

struct visdn_port_ops hfc_st_port_ops = {
	.enable		= hfc_st_port_enable,
	.disable	= hfc_st_port_disable,
};

void hfc_st_port_init(
	struct hfc_st_port *port,
	struct hfc_card *card,
	int id)
{
	port->card = card;
	port->id = id;

	INIT_WORK(&port->state_change_work,
		hfc_st_port_state_change_work,
		port);

	visdn_port_init(&port->visdn_port, &hfc_st_port_ops);
	port->visdn_port.priv = port;

	port->nt_mode = FALSE;
	port->clock_delay = HFC_DEF_TE_CLK_DLY;
	port->sampling_comp = HFC_DEF_TE_SAMPL_COMP;

	hfc_chan_init(&port->chans[D], port, "D", D, hfc_D_CHAN_OFF + id*4,
		16000, VISDN_CHAN_ROLE_D, VISDN_CHAN_ROLE_D);
	hfc_chan_init(&port->chans[B1], port, "B1", B1, hfc_B1_CHAN_OFF + id*4,
		64000, VISDN_CHAN_ROLE_B, VISDN_CHAN_ROLE_B);
	hfc_chan_init(&port->chans[B2], port, "B2", B2, hfc_B2_CHAN_OFF + id*4,
		64000, VISDN_CHAN_ROLE_B, VISDN_CHAN_ROLE_B);
	hfc_chan_init(&port->chans[E], port, "E", E, hfc_E_CHAN_OFF + id*4,
		16000, VISDN_CHAN_ROLE_E, VISDN_CHAN_ROLE_E);
	hfc_chan_init(&port->chans[SQ], port, "SQ", SQ, 0,
		4000, VISDN_CHAN_ROLE_S, VISDN_CHAN_ROLE_S);
}

