
astmod_LTLIBRARIES = chan_vgsm.la
chan_vgsm_la_SOURCES =\
	chan_vgsm.c	\
	module.c	\
	comm.c		\
	causes.c	\
	sms.c		\
	cbm.c		\
	operators.c	\
	pin.c		\
	number.c	\
	bcd.c		\
	gsm_charset.c	\
	7bit.c		\
	longtime.c	\
	util.c

noinst_HEADERS =\
	chan_vgsm.h	\
	module.h	\
	comm.h		\
	causes.h	\
	sms.h		\
	cbm.h		\
	operators.h	\
	pin.h		\
	number.h	\
	bcd.h		\
	gsm_charset.h	\
	7bit.h		\
	longtime.h	\
	util.h

INCLUDES =					\
	-I$(top_builddir)			\
	-I$(top_builddir)/include/		\
	-I$(top_builddir)/modules/include/	\
	-I$(astincdir)

chan_vgsm_la_LIBADD = 
chan_vgsm_la_LDFLAGS = -module -avoid-version -no-undefined

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall
#AM_CFLAGS += -fno-inline -include $(astincdir)/asterisk/astmm.h
