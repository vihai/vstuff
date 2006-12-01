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

sbin_PROGRAMS = visdnctl

INCLUDES = \
	-I$(top_srcdir)/include/		\
	-I$(top_srcdir)/modules/include/

visdnctl_SOURCES = \
	visdnctl.c		\
	netdev.c

visdnctl_LDADD =

noinst_HEADERS = \
	visdnctl.h		\
	netdev.h

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall

if !inline
AM_CFLAGS += -fno-inline
endif
