
#include <string.h>

#include "ie_cdpn.h"

int q931_append_ie_called_party_number(void *buf, const char *called_number)
{
 struct q931_ie_onwire *ie = (struct q931_ie_onwire *)buf;

 ie->id = Q931_IE_CALLED_PARTY_NUMBER;
 ie->size = 0;

 struct q931_ie_called_party_number_onwire_3 *oct_3 =
   (struct q931_ie_called_party_number_onwire_3 *)(&ie->data[ie->size]);

 oct_3->ext = 1;
 oct_3->type_of_number = Q931_IE_CDPN_TON_UNKNOWN;
 oct_3->numbering_plan_identificator = Q931_IE_CDPN_NP_UNKNOWN;
 ie->size += 1;

 memcpy(&ie->data[ie->size], called_number, strlen(called_number));
 ie->size += strlen(called_number);

 return ie->size + sizeof(struct q931_ie_onwire);
}
