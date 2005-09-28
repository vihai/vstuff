#ifndef _LAPD_DEV_H
#define _LAPD_DEV_H

int lapd_device_event(struct notifier_block *this,
 		unsigned long event, void *ptr);

#endif
