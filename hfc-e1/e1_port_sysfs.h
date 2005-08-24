#ifndef _HFC_E1_PORT_SYSFS_H
#define _HFC_E1_PORT_SYSFS_H

int hfc_e1_port_sysfs_create_files(
        struct hfc_e1_port *port);
void hfc_e1_port_sysfs_delete_files(
        struct hfc_e1_port *port);

#endif
