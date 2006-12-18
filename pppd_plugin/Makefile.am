#
# vstuff
#
# Copyright (C) 2004-2006 Daniele Orlandi
#
# Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
#
# This program is free software and may be modified and distributed
# under the terms and conditions of the GNU General Public License.
#

pppdplugins_LTLIBRARIES = visdn.la
visdn_la_SOURCES = visdn.c

visdn_la_CPPFLAGS=\
	-I$(top_builddir)			\
	-I$(top_builddir)/modules/include/	\
	-I$(pppdincdir)

visdn_la_LIBADD =
visdn_la_LDFLAGS = -module -avoid-version -no-undefined

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall

if !inline
AM_CFLAGS += -fno-inline
endif
