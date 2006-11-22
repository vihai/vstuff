#
# vstuff
#
# Copyright (C) 2006 Daniele Orlandi
#
# Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
#
# This program is free software and may be modified and distributed
# under the terms and conditions of the GNU General Public License.
#

astmod_LTLIBRARIES = res_kstreamer.la
res_kstreamer_la_SOURCES = res_kstreamer.c

noinst_HEADERS =\
	res_kstreamer.h				\
	util.h

INCLUDES =					\
	-I$(top_builddir)			\
	-I$(top_builddir)/include/		\
	-I$(top_builddir)/modules/include/	\
	-I$(top_builddir)/libkstreamer/		\
	-I$(top_builddir)/libskb/		\
	-I$(astincdir)

res_kstreamer_la_LIBADD = ../libkstreamer/libkstreamer.la ../libskb/libskb.la
res_kstreamer_la_LDFLAGS = -module -avoid-version -no-undefined

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall

if !inline
AM_CFLAGS += -fno-inline
endif

if astmm
AM_CFLAGS += -include $(astincdir)/asterisk/astmm.h
endif
