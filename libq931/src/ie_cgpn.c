
#include <string.h>

#include "ie_cgpn.h"

int q931_append_ie_calling_party_number(void *buf, const char *calling_number)
{
	struct q931_ie_onwire *ie = (struct q931_ie_onwire *)buf;

	ie->id = Q931_IE_CALLING_PARTY_NUMBER;
	ie->size = 0;

	ie->data[ie->size] = 0x00;
	struct q931_ie_calling_party_number_onwire_3_4 *oct_3_4 =
	  (struct q931_ie_calling_party_number_onwire_3_4 *)(&ie->data[ie->size]);

	oct_3_4->ext = 0;
	oct_3_4->type_of_number = Q931_IE_CGPN_TON_UNKNOWN;
	oct_3_4->numbering_plan_identificator = Q931_IE_CGPN_NP_ISDN_TELEPHONY;
	oct_3_4->ext2 = 1;
	oct_3_4->presentation_indicator = Q931_IE_CGPN_PI_PRESENTATION_ALLOWED;
	oct_3_4->screening_indicator = Q931_IE_CGPN_SI_USER_PROVIDED_VERIFIED_AND_PASSED;
	ie->size += 2;

	memcpy(&ie->data[ie->size], calling_number, strlen(calling_number));
	ie->size += strlen(calling_number);

	return ie->size + sizeof(struct q931_ie_onwire);
}
