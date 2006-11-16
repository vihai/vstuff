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

lib_LTLIBRARIES = libskb.la

libskb_la_SOURCES = libskb.c

libskb_ladir = libskb

nobase_include_HEADERS = libskb.h

AM_CFLAGS = -D_REENTRANT -DGNU_SOURCE -Wall

if !inline
AM_CFLAGS += -fno-inline
endif
