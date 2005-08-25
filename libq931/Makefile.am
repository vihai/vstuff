
lib_LTLIBRARIES = libq931.la

libq931_la_LDFLAGS = -version-info 0:0:0

libq931_la_SOURCES =		\
	q931.c			\
	call.c			\
	ces.c			\
	intf.c			\
	msgtype.c		\
	ie.c			\
	ies.c			\
	timer.c			\
	channel.c		\
	out.c			\
	global.c		\
	chanset.c		\
	callref.c		\
	ie_bearercap.c		\
	ie_cdpn.c		\
	ie_cgpn.c		\
	ie_chanid.c		\
	ie_progind.c		\
	ie_cause.c		\
	ie_call_state.c		\
	ie_hlc.c		\
	ie_restind.c		\
	ie_sending_complete.c

INCLUDES = -I$(srcdir)/.. -I$(srcdir)/../lapd/

libq931_ladir = libq931

libq931_la_HEADERS =		\
	call.h			\
	call_inline.h		\
	callref.h		\
	ces.h			\
	ces_inline.h		\
	channel.h		\
	chanset.h		\
	dlc.h			\
	global.h		\
	ie.h			\
	ie_bearercap.h		\
	ie_call_state.h		\
	ie_cause.h		\
	ie_cdpn.h		\
	ie_cgpn.h		\
	ie_chanid.h		\
	ie_hlc.h		\
	ie_progind.h		\
	ie_restind.h		\
	ie_sending_complete.h	\
	ies.h			\
	intf.h			\
	lib.h			\
	list.h			\
	logging.h		\
	message.h		\
	msgtype.h		\
	out.h			\
	proto.h			\
	q931.h			\
	timer.h			\
	util.h

AM_CFLAGS=-Wall -fno-inline

