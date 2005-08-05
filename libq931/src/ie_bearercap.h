
#ifndef _LIBQ931_IE_BEARERCAP_H
#define _LIBQ931_IE_BEARERCAP_H

#include "ie.h"

enum q931_ie_bearer_capability_coding_standard
{
	Q931_IE_BC_CS_CCITT	= 0x0,
	Q931_IE_BC_CS_RESERVED	= 0x1,
	Q931_IE_BC_CS_NATIONAL	= 0x2,
	Q931_IE_BC_CS_SPECIFIC	= 0x3,
};

enum q931_ie_bearer_capability_information_transfer_capability
{
	Q931_IE_BC_ITC_SPEECH				= 0x00,
	Q931_IE_BC_ITC_UNRESTRICTED_DIGITAL		= 0x08,
	Q931_IE_BC_ITC_RESTRICTED_DIGITAL		= 0x09,
	Q931_IE_BC_ITC_3_1_KHZ_AUDIO			= 0x10,
	Q931_IE_BC_ITC_UNRESTRICTED_DIGITAL_WITH_TONES	= 0x10,
	Q931_IE_BC_ITC_VIDEO				= 0x18,
};

enum q931_ie_bearer_capability_transfer_mode
{
	Q931_IE_BC_TM_CIRCUIT	= 0x0,
	Q931_IE_BC_TM_PACKET	= 0x2,
};

enum q931_ie_bearer_capability_information_transfer_rate
{
	Q931_IE_BC_ITR_PACKET	= 0x00,
	Q931_IE_BC_ITR_64	= 0x10,
	Q931_IE_BC_ITR_64_X_2	= 0x11,
	Q931_IE_BC_ITR_384	= 0x13,
	Q931_IE_BC_ITR_1536	= 0x15,
	Q931_IE_BC_ITR_1920	= 0x17,
};

enum q931_ie_bearer_capability_user_information_layer_1_protocol
{
	Q931_IE_BC_UIL1P_V110		= 0x01,
	Q931_IE_BC_UIL1P_G711_ULAW	= 0x02,
	Q931_IE_BC_UIL1P_G711_ALAW	= 0x03,
	Q931_IE_BC_UIL1P_G721		= 0x04,
	Q931_IE_BC_UIL1P_G722		= 0x05,
	Q931_IE_BC_UIL1P_G7XX_VIDEO	= 0x06,
	Q931_IE_BC_UIL1P_NON_CCITT	= 0x07,
	Q931_IE_BC_UIL1P_V120		= 0x08,
	Q931_IE_BC_UIL1P_X31		= 0x09,
};

enum q931_ie_bearer_capability_user_information_layer_ident
{
	Q931_IE_BC_LAYER_1_IDENT	= 0x1,
	Q931_IE_BC_LAYER_2_IDENT	= 0x2,
	Q931_IE_BC_LAYER_3_IDENT	= 0x2,
};

struct q931_ie_bearer_capability
{
	struct q931_ie ie;

	enum q931_ie_bearer_capability_coding_standard
		coding_standard;
	enum q931_ie_bearer_capability_information_transfer_capability
		information_transfer_capability;
	enum q931_ie_bearer_capability_transfer_mode
		transfer_mode;
	enum q931_ie_bearer_capability_information_transfer_rate
		information_transfer_rate;
	enum q931_ie_bearer_capability_user_information_layer_1_protocol
		user_information_layer_1_protocol;
};

#ifdef Q931_PRIVATE

struct q931_ie_bearer_capability_onwire_3
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 coding_standard:2;
	__u8 information_transfer_capability:5;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 information_transfer_capability:5;
	__u8 coding_standard:2;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct q931_ie_bearer_capability_onwire_4
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 transfer_mode:2;
	__u8 information_transfer_rate:5;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 information_transfer_rate:5;
	__u8 transfer_mode:2;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct q931_ie_bearer_capability_onwire_4a
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 structure:3;
	__u8 configuration:2;
	__u8 establishment:2;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 establishment:2;
	__u8 configuration:2;
	__u8 structure:3;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct q931_ie_bearer_capability_onwire_4b
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 symmetry:2;
	__u8 information_transfer_rate_dst_to_orig:5;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 information_transfer_rate_dst_to_orig:5;
	__u8 symmetry:2;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct q931_ie_bearer_capability_onwire_5
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 layer_1_ident:2;
	__u8 user_information_layer_1_protocol:5;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 user_information_layer_1_protocol:5;
	__u8 layer_1_ident:2;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct q931_ie_bearer_capability_onwire_5a
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 sync_async:1;
	__u8 negotiation:1;
	__u8 user_rate:5;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 user_rate:5;
	__u8 negotiation:1;
	__u8 sync_async:1;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct q931_ie_bearer_capability_onwire_5b1
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 intermediate_rate:2;
	__u8 nic_on_tx:1;
	__u8 nic_on_rx:1;
	__u8 flowcontrol_on_tx:1;
	__u8 flowcontrol_on_rx:1;
	__u8 :1;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 :1;
	__u8 flowcontrol_on_rx:1;
	__u8 flowcontrol_on_tx:1;
	__u8 nic_on_rx:1;
	__u8 nic_on_tx:1;
	__u8 intermediate_rate:2;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct q931_ie_bearer_capability_onwire_5b2
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 hdr_nohdr:1;
	__u8 multiframe_support:1;
	__u8 mode:1;
	__u8 lli_negotiation:1;
	__u8 assignor_assignee:1;
	__u8 inband_outband_nego:1;
	__u8 :1;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 :1;
	__u8 inband_outband_nego:1;
	__u8 assignor_assignee:1;
	__u8 lli_negotiation:1;
	__u8 mode:1;
	__u8 multiframe_support:1;
	__u8 hdr_nohdr:1;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct q931_ie_bearer_capability_onwire_5c
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 number_of_stop_bits:2;
	__u8 number_of_data_bits:2;
	__u8 parity:3;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 parity:3;
	__u8 number_of_data_bits:2;
	__u8 number_of_stop_bits:2;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct q931_ie_bearer_capability_onwire_5d
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 duplex_mode:1;
	__u8 modem_type:6;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 modem_type:6;
	__u8 duplex_mode:1;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct q931_ie_bearer_capability_onwire_6
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 layer_2_ident:2;
	__u8 user_information_layer_2_protocol:5;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 user_information_layer_2_protocol:5;
	__u8 layer_2_ident:2;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

struct q931_ie_bearer_capability_onwire_7
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 ext:1;
	__u8 layer_3_ident:2;
	__u8 user_information_layer_3_protocol:5;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 user_information_layer_3_protocol:5;
	__u8 layer_3_ident:2;
	__u8 ext:1;
#else
#error Unsupported byte order
#endif
} __attribute__ ((__packed__));

void q931_ie_bearer_capability_register(
	const struct q931_ie_type *type);

int q931_ie_bearer_capability_check(
	const struct q931_ie *ie,
	const struct q931_message *msg);

int q931_ie_bearer_capability_write_to_buf(
	const struct q931_ie *ie,
	void *buf,
	int max_size);

#endif
#endif
