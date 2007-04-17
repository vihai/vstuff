#
# vstuff
#
# Copyright (C) 2006-2007 Daniele Orlandi
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
	netlink.h	\
	util.h		\
	logging.h	\
	pd_grammar.h	\
	pd_parser.h

libkstreamer_la_CPPFLAGS= \
	-I$(top_builddir)			\
	-I$(top_builddir)/include/		\
	-I$(top_builddir)/libskb/		\
	-I$(top_builddir)/modules/include/

CLEANFILES = pd_grammar.c pd_grammar.h pd_grammar.out libkstreamer_la-pd_grammar.lo
EXTRA_DIST = pd_grammar.lem

BUILT_SOURCES = pd_grammar.c

pd_grammar.c pd_grammar.h: pd_grammar.lem
	../tools/lemon pd_grammar.lem

libkstreamer_la-pd_grammar.lo: pd_grammar.c
	if $(LIBTOOL) --mode=compile --tag=CC $(CC) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(libkstreamer_la_CPPFLAGS) $(CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) -MT libkstreamer_la-pd_grammar.lo -MD -MP -MF "$(DEPDIR)/libkstreamer_la-pd_grammar.Tpo" -c -o libkstreamer_la-pd_grammar.lo `test -f 'pd_grammar.c' || echo '$(srcdir)/'`pd_grammar.c; \
	then mv -f "$(DEPDIR)/libkstreamer_la-pd_grammar.Tpo" "$(DEPDIR)/libkstreamer_la-pd_grammar.Plo"; else rm -f "$(DEPDIR)/libkstreamer_la-pd_grammar.Tpo"; exit 1; fi

libkstreamer_la_LIBADD = libkstreamer_la-pd_grammar.lo
libkstreamer_la_LDFLAGS = -module -avoid-version -no-undefined

AM_CFLAGS = -Wall -D_REENTRANT -D_GNU_SOURCE -D_LIBKSTREAMER_PRIVATE_

if !inline
AM_CFLAGS += -fno-inline
endif
