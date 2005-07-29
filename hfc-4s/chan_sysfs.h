#ifndef _HFC_CHAN_SYSFS_H
#define _HFC_CHAN_SYSFS_H

int hfc_chan_sysfs_create_files_D(struct hfc_chan_duplex *chan);
int hfc_chan_sysfs_create_files_B(struct hfc_chan_duplex *chan);
int hfc_chan_sysfs_create_files_SQ(struct hfc_chan_duplex *chan);

void hfc_chan_sysfs_delete_files_D(struct hfc_chan_duplex *chan);
void hfc_chan_sysfs_delete_files_B(struct hfc_chan_duplex *chan);
void hfc_chan_sysfs_delete_files_SQ(struct hfc_chan_duplex *chan);

#endif
