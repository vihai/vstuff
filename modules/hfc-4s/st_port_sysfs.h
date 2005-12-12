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

#ifndef _HFC_ST_PORT_SYSFS_H
#define _HFC_ST_PORT_SYSFS_H

int hfc_st_port_sysfs_create_files(
        struct hfc_st_port *port);
void hfc_st_port_sysfs_delete_files(
        struct hfc_st_port *port);

#endif
