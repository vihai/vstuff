#
# vstuff
#
# Copyright (C) 2007 Daniele Orlandi
#
# Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
#
# This program is free software and may be modified and distributed
# under the terms and conditions of the GNU General Public License.
#

lib_LTLIBRARIES = libskb.la

libskb_la_SOURCES = libskb.c

libskb_ladir = libskb
libskb_la_LDFLAGS = -module -version-info 1:0:0 -no-undefined

nobase_include_HEADERS = libskb.h

libskb_la_CPPFLAGS= \
	-I$(top_builddir)			\
	-I$(top_builddir)/include/

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall

if !inline
AM_CFLAGS += -fno-inline
endif
