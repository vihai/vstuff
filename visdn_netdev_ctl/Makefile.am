#
# vISDN Netdev controlling tool
#
# Copyright (C) 2004-2007 Daniele Orlandi
#
# Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
#
# This program is free software and may be modified and distributed
# under the terms and conditions of the GNU General Public License.
#

sbin_PROGRAMS = visdn_netdev_ctl

visdn_netdev_ctl_SOURCES = \
	visdn_netdev_ctl.c

noinst_HEADERS =

visdn_netdev_ctl_CPPFLAGS = \
	-I$(top_srcdir)/include/		\
	-I$(top_srcdir)/modules/include/

visdn_netdev_ctl_LDADD =

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall

if !inline
AM_CFLAGS += -fno-inline
endif
