#ifndef _LIBQ931_PROTO_H
#define _LIBQ931_PROTO_H

struct q931_header
{
	__u8 protocol_discriminator;

#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 spare1:4;
	__u8 call_reference_len:4;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 call_reference_len:4;
	__u8 spare1:4;
#else
#error Unsupported byte order
#endif

	__u8 call_reference[0];

} __attribute__ ((__packed__));

struct q931_message_header
{
#if __BYTE_ORDER == __BIG_ENDIAN
	__u8 f:1;
	__u8 msg:7;
#elif __BYTE_ORDER == __LITTLE_ENDIAN
	__u8 msg:7;
	__u8 f:1;
#else
#error Unsupported byte order
#endif
	__u8 data[0];
} __attribute__ ((__packed__));

enum q931_protocol_discriminators
{
 Q931_PROTOCOL_DISCRIMINATOR_Q931	= 0x08,
 Q931_PROTOCOL_DISCRIMINATOR_GR303	= 0x4f,
};

#endif
