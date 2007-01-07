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
	router.c	\
	pd_parser.c	\
	pd_grammar.lem

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
	util.h		\
	logging.h	\
	pd_grammar.h	\
	pd_parser.h

libkstreamer_la_CPPFLAGS= \
	-I$(top_builddir)			\
	-I$(top_builddir)/include/		\
	-I$(top_builddir)/libskb/		\
	-I$(top_builddir)/modules/include/

CLEANFILES = pd_grammar.c pd_grammar.h pd_grammar.out
EXTRA_DIST = pd_grammar.lem

BUILT_SOURCES = pd_grammar.c

pd_grammar.c pd_grammar.h: pd_grammar.lem
	../tools/lemon pd_grammar.lem

pd_grammar.$(OBJEXT): pd_grammar.c
	$(CC) -c pd_grammar.c -o $@ $(libkstreamer_la_CPPFLAGS)

libkstreamer_la_LIBADD = pd_grammar.o

libkstreamer_la_LDFLAGS = -module -avoid-version -no-undefined

AM_CFLAGS = -Wall -D_REENTRANT -D_GNU_SOURCE -D_LIBKSTREAMER_PRIVATE_

if !inline
AM_CFLAGS += -fno-inline
endif
