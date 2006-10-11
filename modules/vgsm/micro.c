/*
 * VoiSmart GSM board vISDN driver
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/kernel.h>
/*#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/bitops.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include "regs.h"
#include "module.h"
#include "codec.h"*/

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

