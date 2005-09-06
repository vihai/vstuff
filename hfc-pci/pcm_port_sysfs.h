/*
 * Cologne Chip's HFC-S PCI A vISDN driver
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#ifndef _HFC_PCM_PORT_SYSFS_H
#define _HFC_PCM_PORT_SYSFS_H

int hfc_pcm_port_sysfs_create_files(
        struct hfc_pcm_port *port);
void hfc_pcm_port_sysfs_delete_files(
        struct hfc_pcm_port *port);

#endif
