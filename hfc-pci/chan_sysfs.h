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

#ifndef _HFC_CHAN_SYSFS_H
#define _HFC_CHAN_SYSFS_H

int hfc_chan_sysfs_create_files_D(struct hfc_chan_duplex *chan);
int hfc_chan_sysfs_create_files_E( struct hfc_chan_duplex *chan);
int hfc_chan_sysfs_create_files_B(struct hfc_chan_duplex *chan);
int hfc_chan_sysfs_create_files_SQ(struct hfc_chan_duplex *chan);

void hfc_chan_sysfs_delete_files_D(struct hfc_chan_duplex *chan);
void hfc_chan_sysfs_delete_files_E(struct hfc_chan_duplex *chan);
void hfc_chan_sysfs_delete_files_B(struct hfc_chan_duplex *chan);
void hfc_chan_sysfs_delete_files_SQ(struct hfc_chan_duplex *chan);

#endif
