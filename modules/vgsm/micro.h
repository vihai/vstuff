/*
 * VoiSmart GSM board vISDN driver
 *
 * Copyright (C) 2006 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *          Massimo Mazzeo <mmazzeo@voismart.it>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _VGSM_MICRO_H
#define _VGSM_MICRO_H

struct vgsm_card;

struct vgsm_micro
{
	struct vgsm_card *card;
	int id;

	struct completion fw_upgrade_ready;
};

struct vgsm_micro_message
{
	union {
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 cmd:2;	/* Command */
	u8 cmd_dep:3;	/* cmd dependent */
	u8 numbytes:3;	/* data length */
	
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 numbytes:3;	/* data length */
	u8 cmd_dep:3;	/* cmd dependent */
	u8 cmd:2;	/* Command */
#endif
	u8 payload[7];
	};

	u8 raw[8];
	};

} __attribute__ ((__packed__));

void vgsm_micro_init(
	struct vgsm_micro *micro,
	struct vgsm_card *card,
	int id);

void vgsm_send_msg(
	struct vgsm_micro *micro,
	struct vgsm_micro_message *msg);
void vgsm_send_get_fw_ver(
	struct vgsm_micro *micro);
void vgsm_send_fw_upgrade(
	struct vgsm_micro *micro);

#endif
