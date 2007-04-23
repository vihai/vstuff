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

sbin_PROGRAMS = kstool

kstool_SOURCES = \
	kstool.c		\
	connect.c		\
	disconnect.c		\
	pipeline_open.c		\
	pipeline_close.c	\
	pipeline_start.c	\
	pipeline_stop.c		\
	monitor.c

kstool_LDADD = \
        -lpthread					\
        $(top_srcdir)/libskb/libskb.la			\
        $(top_srcdir)/libkstreamer/libkstreamer.la

noinst_HEADERS = \
	kstool.h		\
	connect.h		\
	disconnect.h		\
	pipeline_open.h		\
	pipeline_close.h	\
	pipeline_start.h	\
	pipeline_stop.h

kstool_CPPFLAGS=\
	-I$(top_srcdir)/include/		\
	-I$(top_srcdir)/modules/include/	\
	-I$(top_srcdir)/libskb/			\
	-I$(top_srcdir)/libkstreamer/

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall

if !inline
AM_CFLAGS += -fno-inline
endif
