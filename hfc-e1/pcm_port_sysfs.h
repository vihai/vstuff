#ifndef _HFC_PCM_PORT_SYSFS_H
#define _HFC_PCM_PORT_SYSFS_H

int hfc_pcm_port_sysfs_create_files(
        struct hfc_pcm_port *port);
void hfc_pcm_port_sysfs_delete_files(
        struct hfc_pcm_port *port);

#endif
