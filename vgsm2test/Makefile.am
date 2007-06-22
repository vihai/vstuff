#
# vstuff
#
# Copyright (C) 2007 Daniele Orlandi
#
# Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
#
# This program is free software and may be modified and distributed
# under the terms and conditions of the GNU General Public License.
#

bin_PROGRAMS = vgsm2test

vgsm2test_SOURCES = vgsm2test.c
vgsm2test_CPPFLAGS =
vgsm2test_LDADD = -lncurses -lform -lmenu

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall

if !inline
AM_CFLAGS += -fno-inline
endif
