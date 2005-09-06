#ifndef _HFC_CARD_SYSFS_H
#define _HFC_CARD_SYSFS_H

int hfc_card_sysfs_create_files(struct hfc_card *card);
void hfc_card_sysfs_delete_files(struct hfc_card *card);

#endif
