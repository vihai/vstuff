#include <string.h>

#define Q931_PRIVATE

#include "ie_progind.h"

int q931_append_ie_progress_indicator(void *buf,
	enum q931_ie_progress_indicator_location location,
	enum q931_ie_progress_indicator_progress_description description)
{
	struct q931_ie_onwire *ie = (struct q931_ie_onwire *)buf;

	ie->id = Q931_IE_PROGRESS_INDICATOR;
	ie->len = 0;

	ie->data[ie->len] = 0x00;
	struct q931_ie_progress_indicator_onwire_3_4 *ie_bc_3_4 =
	  (struct q931_ie_progress_indicator_onwire_3_4 *)(&ie->data[ie->len]);
	ie_bc_3_4->ext = 1;
	ie_bc_3_4->coding_standard = Q931_IE_PI_CS_CCITT;
	ie_bc_3_4->location = location;
	ie_bc_3_4->ext2 = 1;
	ie_bc_3_4->progress_description = description;
	ie->len += sizeof(struct q931_ie_progress_indicator_onwire_3_4);

	return ie->len + sizeof(struct q931_ie_onwire);
}
