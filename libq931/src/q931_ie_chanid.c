#include <string.h>

#include "q931_ie_chanid.h"

int q931_append_ie_channel_identification_any(void *buf)
{
 struct q931_ie_onwire *ie = (struct q931_ie_onwire *)buf;

 ie->id = Q931_IE_CHANNEL_IDENTIFICATION;
 ie->size = 0;

 struct q931_ie_channel_identification_onwire_3 *oct_3 =
   (struct q931_ie_channel_identification_onwire_3 *)(&ie->data[ie->size]);
 oct_3->ext = 1;
 oct_3->interface_id_present = Q931_IE_CI_IIP_IMPLICIT;
 oct_3->interface_type = Q931_IE_CI_IT_BASIC;
 oct_3->preferred_exclusive = Q931_IE_CI_PE_PREFERRED;
 oct_3->d_channel_indicator = Q931_IE_CI_DCI_IS_NOT_D_CHAN;
 oct_3->info_channel_selection = Q931_IE_CI_ICS_BRI_ANY;
 ie->size += 1;

 return ie->size + sizeof(struct q931_ie_onwire);
}
