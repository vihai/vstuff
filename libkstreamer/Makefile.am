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

lib_LTLIBRARIES = libkstreamer.la

libkstreamer_la_SOURCES = \
	libkstreamer.c	\
	dynattr.c	\
	channel.c	\
	netlink.c	\
	xact.c		\
	req.c		\
	node.c		\
	pipeline.c	\
	conn.c		\
	router.c

libkstreamer_ladir = libkstreamer

nobase_include_HEADERS = \
	dynattr.h	\
	libkstreamer.h	\
	channel.h	\
	node.h		\
	pipeline.h	\
	conn.h		\
	xact.h		\
	req.h		\
	router.h	\
	util.h

INCLUDES =					\
	-I$(top_builddir)			\
	-I$(top_builddir)/include/		\
	-I$(top_builddir)/libskb/		\
	-I$(top_builddir)/modules/include/

libkstreamer_la_LDFLAGS = -module -avoid-version -no-undefined

AM_CFLAGS = -D_REENTRANT -DGNU_SOURCE -Wall

if !inline
AM_CFLAGS += -fno-inline
endif
