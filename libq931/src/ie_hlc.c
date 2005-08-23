#include <string.h>
#include <assert.h>

#define Q931_PRIVATE

#include "lib.h"
#include "ie_hlc.h"

static const struct q931_ie_type *ie_type;

void q931_ie_high_layer_compatibility_register(
	const struct q931_ie_type *type)
{
	ie_type = type;
}

struct q931_ie_high_layer_compatibility *q931_ie_high_layer_compatibility_alloc(void)
{
	struct q931_ie_high_layer_compatibility *ie;
	ie = malloc(sizeof(*ie));
	assert(ie);

	memset(ie, 0x00, sizeof(*ie));

	ie->ie.refcnt = 1;
	ie->ie.type = ie_type;

	return ie;
}

struct q931_ie *q931_ie_high_layer_compatibility_abstract(void)
{
	return &q931_ie_high_layer_compatibility_alloc()->ie;
}

int q931_ie_high_layer_compatibility_read_from_buf(
	struct q931_ie *abstract_ie,
	const struct q931_message *msg,
	int pos,
	int len)
{
	assert(abstract_ie->type == ie_type);

	struct q931_ie_high_layer_compatibility *ie =
		container_of(abstract_ie,
			struct q931_ie_high_layer_compatibility, ie);

	if (len < 2) {
		report_msg(msg, LOG_ERR, "IE size < 2\n");
		return FALSE;
	}

	int nextoct = 0;

	struct q931_ie_high_layer_compatibility_onwire_3 *oct_3 =
		(struct q931_ie_high_layer_compatibility_onwire_3 *)
		(msg->rawies + pos + (nextoct++));

	ie->coding_standard = oct_3->coding_standard;
	ie->interpretation = oct_3->interpretation;
	ie->presentation_method = oct_3->presentation_method;

	struct q931_ie_high_layer_compatibility_onwire_4 *oct_4 =
		(struct q931_ie_high_layer_compatibility_onwire_4 *)
		(msg->rawies + pos + (nextoct++));

	ie->characteristics_identification =
		oct_4->characteristics_identification;

	if (oct_4->ext) {
		if (len < 3) {
			report_msg(msg, LOG_ERR, "IE size < 3\n");
			return FALSE;
		}

		struct q931_ie_high_layer_compatibility_onwire_4a *oct_4a =
			(struct q931_ie_high_layer_compatibility_onwire_4a *)
			(msg->rawies + pos + (nextoct++));

		ie->extended_characteristics_identification =
			oct_4a->extended_characteristics_identification;

		if (oct_4a->ext) {
			report_msg(msg, LOG_ERR, "IE Oct 4a ext != 1\n");
			return FALSE;
		}
	}

	return TRUE;
}

int q931_ie_high_layer_compatibility_write_to_buf(
	const struct q931_ie *generic_ie,
	void *buf,
	int max_size)
{
	struct q931_ie_high_layer_compatibility *ie =
		container_of(generic_ie, struct q931_ie_high_layer_compatibility, ie);
	struct q931_ie_onwire *ieow = (struct q931_ie_onwire *)buf;

	ieow->id = Q931_IE_HIGH_LAYER_COMPATIBILITY;
	ieow->len = 0;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_high_layer_compatibility_onwire_3 *oct_3 =
	  (struct q931_ie_high_layer_compatibility_onwire_3 *)(&ieow->data[ieow->len]);
	oct_3->ext = 1;
	oct_3->coding_standard = ie->coding_standard;
	oct_3->interpretation = ie->interpretation;
	oct_3->presentation_method = ie->presentation_method;
	ieow->len++;

	ieow->data[ieow->len] = 0x00;
	struct q931_ie_high_layer_compatibility_onwire_4 *oct_4 =
	  (struct q931_ie_high_layer_compatibility_onwire_4 *)(&ieow->data[ieow->len]);
	oct_4->characteristics_identification =
		ie->characteristics_identification;

	if (ie->characteristics_identification ==
		Q931_IE_HLC_CI_RESERVED_FOR_MAINTENANCE ||
	   ie->characteristics_identification ==
		Q931_IE_HLC_CI_RESERVED_FOR_MANAGEMENT) {

		oct_4->ext = 0;
		ieow->len++;

		ieow->data[ieow->len] = 0x00;
		struct q931_ie_high_layer_compatibility_onwire_4a *oct_4a =
		  (struct q931_ie_high_layer_compatibility_onwire_4a *)(&ieow->data[ieow->len]);
		oct_4a->ext = 1;
		oct_4a->extended_characteristics_identification =
			ie->extended_characteristics_identification;
		ieow->len++;
	} else {
		oct_4->ext = 1;
		ieow->len++;
	}

	return ieow->len + sizeof(struct q931_ie_onwire);
}

static const char *q931_ie_high_layer_compatibility_coding_standard_to_text(
	enum q931_ie_high_layer_compatibility_coding_standard
		coding_standard)
{
        switch(coding_standard) {
	case Q931_IE_HLC_CS_CCITT:
		return "CCITT";
	case Q931_IE_HLC_CS_RESERVED:
		return "Reserved";
	case Q931_IE_HLC_CS_NATIONAL:
		return "National";
	case Q931_IE_HLC_CS_NETWORK_SPECIFIC:
		return "Network specific";
	default:
		return "*INVALID*";
	}
}

static const char *q931_ie_high_layer_compatibility_characteristics_identification_to_text(
	enum q931_ie_high_layer_compatibility_characteristics_identification
		characteristics_identification)
{
        switch(characteristics_identification) {
	case Q931_IE_HLC_CI_TELEPHONY:
		return "Telephony";
	case Q931_IE_HLC_CI_FACSIMILE_G2_G3:
		return "G2/G3 Facsimile";
	case Q931_IE_HLC_CI_FACSIMILE_G4_CALSS1:
		return "G4/Class 1 Facsimile";
	case Q931_IE_HLC_CI_TELETEX_F184_FACSIMILE_G4_CLASS2_3:
		return "G4/Class 2/3 Facsimile";
	case Q931_IE_HLC_CI_TELETEX_F220:
		return "F.220 Telex";
	case Q931_IE_HLC_CI_TELETEX_F200:
		return "F.200 Telex";
	case Q931_IE_HLC_CI_VIDEOTEX:
		return "Videotex";
	case Q931_IE_HLC_CI_TELEX:
		return "Telex";
	case Q931_IE_HLC_CI_X400:
		return "X.400";
	case Q931_IE_HLC_CI_X200:
		return "X.200";
	case Q931_IE_HLC_CI_RESERVED_FOR_MAINTENANCE:
		return "Reserved for maintenance";
	case Q931_IE_HLC_CI_RESERVED_FOR_MANAGEMENT:
		return "Reserved for management";
	case Q931_IE_HLC_CI_RESERVED:
		return "Reserved";
	default:
		return "*INVALID*";
	}
}


void q931_ie_high_layer_compatibility_dump(
	const struct q931_ie *generic_ie,
	const struct q931_message *msg,
	const char *prefix)
{
	struct q931_ie_high_layer_compatibility *ie =
		container_of(generic_ie, struct q931_ie_high_layer_compatibility, ie);

	report_msg(msg, LOG_DEBUG, "%sCoding standard = %s (%d)\n", prefix,
		q931_ie_high_layer_compatibility_coding_standard_to_text(
			ie->coding_standard),
		ie->coding_standard);

	report_msg(msg, LOG_DEBUG, "%sCharacteristics identification = %s (%d)\n", prefix,
		q931_ie_high_layer_compatibility_characteristics_identification_to_text(
			ie->characteristics_identification),
		ie->characteristics_identification);
}
