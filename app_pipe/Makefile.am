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

astmod_LTLIBRARIES = app_pipe.la
app_pipe_la_SOURCES = app_pipe.c

INCLUDES =					\
	-I$(top_builddir)			\
	-I$(top_builddir)/include/		\
	-I$(top_builddir)/modules/include/	\
	-I$(top_builddir)/libq931/		\
	-I$(top_builddir)/chan_visdn/		\
	-I$(astincdir)

app_pipe_la_LIBADD =
app_pipe_la_LDFLAGS = -module -avoid-version -no-undefined

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall

if !inline
AM_CFLAGS += -fno-inline
endif

if astmm
AM_CFLAGS += -include $(astincdir)/asterisk/astmm.h
endif
