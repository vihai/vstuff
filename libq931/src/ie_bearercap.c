#include <string.h>

#define Q931_PRIVATE

#include "ie_bearercap.h"

int q931_append_ie_bearer_capability_speech(void *buf, __u8 l1_proto)
{
	struct q931_ie_onwire *ie = (struct q931_ie_onwire *)buf;

	ie->id = Q931_IE_BEARER_CAPABILITY;
	ie->size = 0;

	ie->data[ie->size] = 0x00;
	struct q931_ie_bearer_capability_onwire_3 *ie_bc_3 =
	  (struct q931_ie_bearer_capability_onwire_3 *)(&ie->data[ie->size]);
	ie_bc_3->ext = 1;
	ie_bc_3->coding_standard = Q931_IE_BC_CS_CCITT;
	ie_bc_3->information_transfer_capability = Q931_IE_BC_ITC_SPEECH;
	ie->size += 1;

	ie->data[ie->size] = 0x00;
	struct q931_ie_bearer_capability_onwire_4 *ie_bc_4 =
	  (struct q931_ie_bearer_capability_onwire_4 *)(&ie->data[ie->size]);
	ie_bc_4->ext = 1;
	ie_bc_4->transfer_mode = Q931_IE_BC_TM_CIRCUIT;
	ie_bc_4->information_transfer_rate = Q931_IE_BC_ITR_64;
	ie->size += 1;

	ie->data[ie->size] = 0x00;
	struct q931_ie_bearer_capability_onwire_5 *ie_bc_5 =
	  (struct q931_ie_bearer_capability_onwire_5 *)(&ie->data[ie->size]);
	ie_bc_5->ext = 1;
	ie_bc_5->layer_1_ident = Q931_IE_BC_LAYER_1_IDENT;
	ie_bc_5->user_information_layer_1_protocol = l1_proto;
	ie->size += 1;

	return ie->size + sizeof(struct q931_ie_onwire);
}

int q931_append_ie_bearer_capability_alaw(void *curpos)
{
	return q931_append_ie_bearer_capability_speech(
	         curpos,
	         Q931_IE_BC_UIL1P_G711_ALAW);
}

int q931_append_ie_bearer_capability_ulaw(void *curpos)
{
	return q931_append_ie_bearer_capability_speech(
	         curpos,
	         Q931_IE_BC_UIL1P_G711_ULAW);
}

int q931_append_ie_bearer_capability_v110(void *curpos)
{
	return -1;
}

int q931_append_ie_bearer_capability_v120(__u8 **curpos)
{
	return -1;
}
