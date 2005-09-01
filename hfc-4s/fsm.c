/*
 * Cologne Chip's HFC-4S and HFC-8S vISDN driver
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com> 
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>

#include "fsm.h"
#include "card.h"
#include "card_inline.h"
#include "fifo.h"
#include "chan.h"

static inline void hfc_fsm_select_entry(
	struct hfc_card *card,
	int index)
{
	mb();
	hfc_outb(card, hfc_R_FSM_IDX, index);
	hfc_wait_busy(card); // NOT DOCUMENTED BUT MANDATORY!!!!!!!
	mb();
}

void hfc_upload_fsm_entry(
	struct hfc_card *card,
	struct hfc_fifo *fifo,
	struct hfc_chan_simplex *chan,
	struct hfc_fifo *next_fifo,
	int index)
{
	hfc_debug_card(card, 2,
		"Seq #%d fifo [%d,%s] <=> chan [%d,%s] (%d:%s) ",
		index,
		fifo->hw_index,
		fifo->direction == RX ? "RX" : "TX",
		chan->chan->hw_index,
		chan->direction == RX ? "RX" : "TX",
		chan->chan->port->id,
		chan->chan->name);

	hfc_fsm_select_entry(card, index);

	hfc_outb(card, hfc_A_CHANNEL,
		(chan->direction == RX ?
			hfc_A_CHANNEL_V_CH_FDIR_RX :
			hfc_A_CHANNEL_V_CH_FDIR_TX) |
		hfc_A_CHANNEL_V_CH_FNUM(
			chan->chan->hw_index));

	if (next_fifo) {
		hfc_outb(card, hfc_A_FIFO_SEQ,
			(next_fifo->direction == RX ?
				hfc_A_FIFO_SEQ_V_NEXT_FIFO_DIR_RX :
				hfc_A_FIFO_SEQ_V_NEXT_FIFO_DIR_TX) |
			hfc_A_FIFO_SEQ_V_NEXT_FIFO_NUM(
				next_fifo->hw_index));

		hfc_debug_cont(2, "=> fifo [%d,%s]\n",
			next_fifo->hw_index,
			next_fifo->direction == RX ? "RX" : "TX");

	} else {
		hfc_outb(card, hfc_A_FIFO_SEQ,
			hfc_A_FIFO_SEQ_V_SEQ_END);

		hfc_debug_cont(2, "END!\n");
	}
}

void hfc_upload_fsm(
	struct hfc_card *card)
{
	struct hfc_fifo *fifos[64];
	int nfifos = 0;

	int i;
	for (i=0; i<card->num_fifos; i++) {
		if (card->fifos[i][TX].connected_chan)
			fifos[nfifos++] = &card->fifos[i][TX];

		if (card->fifos[i][RX].connected_chan)
			fifos[nfifos++] = &card->fifos[i][RX];
	}

	// Nothing inserted, let's close the empty list :)
	if (!nfifos) {
		hfc_fsm_select_entry(card, 0);

		hfc_outb(card, hfc_A_FIFO_SEQ,
			hfc_A_FIFO_SEQ_V_SEQ_END);

		return;
	}

	hfc_outb(card, hfc_R_FIRST_FIFO,
		(fifos[0]->direction == RX ?
			hfc_R_FIRST_FIFO_V_FIRST_FIFO_DIR_RX :
			hfc_R_FIRST_FIFO_V_FIRST_FIFO_DIR_TX) |
		hfc_R_FIRST_FIFO_V_FIRST_FIFO_NUM(fifos[0]->hw_index));

	int index = 0;
	for (i=0; i<nfifos; i++) {
		if (i < nfifos-1)
			hfc_upload_fsm_entry(card, fifos[i],
				fifos[i]->connected_chan, fifos[i+1], index);
		else
			hfc_upload_fsm_entry(card, fifos[i],
				fifos[i]->connected_chan, NULL, index);

		index++;
	}
}

