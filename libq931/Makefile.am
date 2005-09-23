AM_CFLAGS = -D_REENTRANT -Wall

lib_LTLIBRARIES = libq931.la

INCLUDES = -I$(top_srcdir)/lapd/

libq931_la_SOURCES =			\
	q931.c				\
	call.c				\
	ces.c				\
	intf.c				\
	msgtype.c			\
	ie.c				\
	ies.c				\
	timer.c				\
	channel.c			\
	out.c				\
	global.c			\
	chanset.c			\
	callref.c			\
	ie_bearer_capability.c		\
	ie_call_identity.c		\
	ie_call_state.c			\
	ie_called_party_number.c	\
	ie_calling_party_number.c	\
	ie_cause.c			\
	ie_channel_identification.c	\
	ie_display.c			\
	ie_high_layer_compatibility.c	\
	ie_notification_indicator.c	\
	ie_progress_indicator.c		\
	ie_restart_indicator.c		\
	ie_sending_complete.c		\
	call_inline.h			\
	ces_inline.h

libq931_la_LDFLAGS = -version-info 0:0:0

libq931_ladir = libq931

nobase_include_HEADERS =			\
	libq931/call.h				\
	libq931/callref.h			\
	libq931/ces.h				\
	libq931/channel.h			\
	libq931/chanset.h			\
	libq931/dlc.h				\
	libq931/global.h			\
	libq931/ie.h				\
	libq931/ie_bearer_capability.h		\
	libq931/ie_call_identity.h		\
	libq931/ie_call_state.h			\
	libq931/ie_called_party_number.h	\
	libq931/ie_calling_party_number.h	\
	libq931/ie_cause.h			\
	libq931/ie_channel_identification.h	\
	libq931/ie_display.h			\
	libq931/ie_high_layer_compatibility.h	\
	libq931/ie_notification_indicator.h	\
	libq931/ie_progress_indicator.h		\
	libq931/ie_restart_indicator.h		\
	libq931/ie_sending_complete.h		\
	libq931/ies.h				\
	libq931/intf.h				\
	libq931/lib.h				\
	libq931/list.h				\
	libq931/logging.h			\
	libq931/message.h			\
	libq931/msgtype.h			\
	libq931/out.h				\
	libq931/proto.h				\
	libq931/q931.h				\
	libq931/timer.h				\
	libq931/util.h
