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

sbin_PROGRAMS = vgsmctl

INCLUDES = \
	-I$(top_srcdir)/include/		\
	-I$(top_srcdir)/modules/include/

vgsmctl_SOURCES = \
	vgsmctl.c	\
	codec.c		\
	power.c		\
	pad_timeout.c	\
	fw_version.c	\
	fw_upgrade.c

vgsmctl_LDADD = -lz

noinst_HEADERS = \
	vgsmctl.h	\
	codec.h		\
	power.h		\
	pad_timeout.h	\
	fw_version.h	\
	fw_upgrade.h

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall

if !inline
AM_CFLAGS += -fno-inline
endif
