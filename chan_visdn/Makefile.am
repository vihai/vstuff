#
# vISDN channel driver for Asterisk
#
# Copyright (C) 2004-2008 Daniele Orlandi
#
# Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
#
# This program is free software and may be modified and distributed
# under the terms and conditions of the GNU General Public License.
#

astmod_LTLIBRARIES = chan_visdn.la
chan_visdn_la_SOURCES = \
	chan_visdn.c	\
	intf.c		\
	huntgroup.c	\
	ton.c		\
	overlap.c	\
	disconnect.c	\
	app_visdn_ppp.c \
	numbers_list.c

noinst_HEADERS = \
	chan_visdn.h	\
	intf.h		\
	huntgroup.h	\
	ton.h		\
	overlap.h	\
	disconnect.h	\
	numbers_list.h	\
	app_visdn_ppp.h \
	util.h		\
	rwlock_compat.h	\
	debug.h

chan_visdn_la_CPPFLAGS=\
	-DAST_MODULE=\"chan_visdn\"		\
	-I$(top_builddir)			\
	-I$(top_builddir)/include/		\
	-I$(top_builddir)/modules/include/	\
	-I$(top_builddir)/libkstreamer/		\
	-I$(top_builddir)/libskb/		\
	-I$(top_builddir)/res_kstreamer/	\
	-I$(top_builddir)/libq931/		\
	-I$(astincdir)

chan_visdn_la_LIBADD = ../libq931/libq931.la
chan_visdn_la_LDFLAGS = -module -avoid-version -no-undefined

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall -DDEBUG_CODE

if !inline
AM_CFLAGS += -fno-inline
endif

if astmm
AM_CFLAGS += -include $(astincdir)/asterisk/astmm.h
endif
