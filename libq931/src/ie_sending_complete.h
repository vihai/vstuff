#ifndef _IE_SENDING_COMPLETE_H
#define _IE_SENDING_COMPLETE_H

#include "ie.h"

static inline int q931_append_ie_sending_complete(void *buf)
{
 *(__u8 *)buf = Q931_IE_SENDING_COMPLETE;

 return 1;
}

#endif
