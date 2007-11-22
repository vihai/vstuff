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

sbin_PROGRAMS = vgsmctl

vgsmctl_SOURCES = \
	vgsmctl.c	\
	codec.c		\
	power.c		\
	pad_timeout.c	\
	fw_version.c	\
	fw_upgrade.c	\
	get_serial.c	\
	sim_route.c

vgsmctl_LDADD = -lz

noinst_HEADERS = \
	vgsmctl.h	\
	codec.h		\
	power.h		\
	pad_timeout.h	\
	fw_version.h	\
	fw_upgrade.h	\
	get_serial.h	\
	sim_route.h

vgsmctl_CPPFLAGS = \
	-I$(top_srcdir)/include/		\
	-I$(top_srcdir)/modules/include/

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall

if !inline
AM_CFLAGS += -fno-inline
endif
