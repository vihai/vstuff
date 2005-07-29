#ifndef _HFC_ST_PORT_SYSFS_H
#define _HFC_ST_PORT_SYSFS_H

int hfc_st_port_sysfs_create_files(
        struct hfc_st_port *port);
void hfc_st_port_sysfs_delete_files(
        struct hfc_st_port *port);

#endif
