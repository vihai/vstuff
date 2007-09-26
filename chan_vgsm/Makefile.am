#
# vstuff
#
# Copyright (C) 2004-2007 Daniele Orlandi
#
# Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
#
# This program is free software and may be modified and distributed
# under the terms and conditions of the GNU General Public License.
#

astmod_LTLIBRARIES = chan_vgsm.la
chan_vgsm_la_SOURCES =\
	chan_vgsm.c		\
	module.c		\
	sim.c			\
	sim_file.c		\
	mesim.c			\
	huntgroup.c		\
	comm.c			\
	causes.c		\
	sms.c			\
	sms_submit.c		\
	sms_deliver.c		\
	sms_status_report.c	\
	cbm.c			\
	operators.c		\
	pin.c			\
	number.c		\
	bcd.c			\
	gsm_charset.c		\
	7bit.c			\
	base64.c		\
	quotprint.c		\
	longtime.c		\
	timer.c			\
	util.c

noinst_HEADERS =\
	chan_vgsm.h		\
	module.h		\
	sim.h			\
	sim_file.h		\
	mesim.h			\
	huntgroup.h		\
	comm.h			\
	causes.h		\
	sms.h			\
	sms_submit.h		\
	sms_deliver.h		\
	sms_status_report.h	\
	cbm.h			\
	operators.h		\
	pin.h			\
	number.h		\
	bcd.h			\
	gsm_charset.h		\
	7bit.h			\
	base64.h		\
	quotprint.h		\
	longtime.h		\
	timer.h			\
	util.h

chan_vgsm_la_CPPFLAGS=\
	-DAST_MODULE=\"chan_vgsm\"		\
	-I$(top_builddir)			\
	-I$(top_builddir)/include/		\
	-I$(top_builddir)/modules/include/	\
	-I$(top_builddir)/libkstreamer/		\
	-I$(top_builddir)/libskb/		\
	-I$(top_builddir)/res_kstreamer/	\
	-I$(astincdir)

chan_vgsm_la_LIBADD = ../libkstreamer/libkstreamer.la ../libskb/libskb.la
chan_vgsm_la_LDFLAGS = -module -avoid-version -no-undefined

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall

if !inline
AM_CFLAGS += -fno-inline
endif

if astmm
AM_CFLAGS += -include $(astincdir)/asterisk/astmm.h
endif
