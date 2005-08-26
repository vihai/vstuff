
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
	ie_bearercap.c			\
	ie_cdpn.c			\
	ie_cgpn.c			\
	ie_chanid.c			\
	ie_progind.c			\
	ie_cause.c			\
	ie_call_state.c			\
	ie_hlc.c			\
	ie_restind.c			\
	ie_sending_complete.c		\
	call_inline.h			\
	ces_inline.h

libq931_la_LDFLAGS = -version-info 0:0:0

libq931_ladir = libq931

nobase_include_HEADERS =		\
	libq931/call.h			\
	libq931/callref.h		\
	libq931/ces.h			\
	libq931/channel.h		\
	libq931/chanset.h		\
	libq931/dlc.h			\
	libq931/global.h		\
	libq931/ie.h			\
	libq931/ie_bearercap.h		\
	libq931/ie_call_state.h		\
	libq931/ie_cause.h		\
	libq931/ie_cdpn.h		\
	libq931/ie_cgpn.h		\
	libq931/ie_chanid.h		\
	libq931/ie_hlc.h		\
	libq931/ie_progind.h		\
	libq931/ie_restind.h		\
	libq931/ie_sending_complete.h	\
	libq931/ies.h			\
	libq931/intf.h			\
	libq931/lib.h			\
	libq931/list.h			\
	libq931/logging.h		\
	libq931/message.h		\
	libq931/msgtype.h		\
	libq931/out.h			\
	libq931/proto.h			\
	libq931/q931.h			\
	libq931/timer.h			\
	libq931/util.h
