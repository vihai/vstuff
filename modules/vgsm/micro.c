/*
 * VoiSmart vGSM-I card driver
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>

#include "vgsm.h"
#include "card.h"
#include "card_inline.h"

void vgsm_micro_init(
	struct vgsm_micro *micro,
	struct vgsm_card *card,
	int id)
{
	memset(micro, 0, sizeof(*micro));

	micro->card = card;
	micro->id = id;

	init_completion(&micro->fw_upgrade_ready);
}

void vgsm_send_msg(
	struct vgsm_micro *micro,
	struct vgsm_micro_message *msg)
{
	struct vgsm_card *card = micro->card;

	vgsm_wait_e0(card);
	vgsm_write_msg(card, msg);

	if (micro->id == 0)
		vgsm_interrupt_micro(card, 0x08);
	else if (micro->id == 1)
		vgsm_interrupt_micro(card, 0x10);
	else
		BUG();
}

void vgsm_send_get_fw_ver(struct vgsm_micro *micro)
{
	struct vgsm_micro_message msg = { };
	
	msg.cmd = VGSM_CMD_MAINT;
	msg.cmd_dep = VGSM_CMD_MAINT_GET_FW_VER;
	msg.numbytes = 0;
	
	vgsm_send_msg(micro, &msg);
}

void vgsm_send_fw_upgrade(struct vgsm_micro *micro)
{
	struct vgsm_micro_message msg = { };
	
	msg.cmd = VGSM_CMD_FW_UPGRADE;
	msg.cmd_dep = 0;
	msg.numbytes = 0;
	
	vgsm_send_msg(micro, &msg);
}

