#include <string.h>

#include "ie_hlc.h"

int q931_append_ie_high_layer_compatibility_telephony(void *buf)
{
	struct q931_ie_onwire *ie = (struct q931_ie_onwire *)buf;

	ie->id = Q931_IE_HIGH_LAYER_COMPATIBILITY;
	ie->size = 0;

	ie->data[ie->size] = 0x00;
	struct q931_ie_high_layer_compatibility_onwire_3 *ie_bc_3 =
	  (struct q931_ie_high_layer_compatibility_onwire_3 *)(&ie->data[ie->size]);
	ie_bc_3->ext = 1;
	ie_bc_3->coding_standard = Q931_IE_HLC_CS_CCITT;
	ie_bc_3->interpretation = Q931_IE_HLC_P_FIRST;
	ie_bc_3->presentation_method = Q931_IE_HLC_PM_HIGH_LAYER_PROTOCOL_PROFILE;
	ie->size += 1;

	ie->data[ie->size] = 0x00;
	struct q931_ie_high_layer_compatibility_onwire_4 *ie_bc_4 =
	  (struct q931_ie_high_layer_compatibility_onwire_4 *)(&ie->data[ie->size]);
	ie_bc_4->ext = 1;
	ie_bc_4->characteristics_identification = Q931_IE_HLC_CI_TELEPHONY;
	ie->size += 1;

	return ie->size + sizeof(struct q931_ie_onwire);
}
