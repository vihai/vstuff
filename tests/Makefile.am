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

noinst_PROGRAMS = vgsm_stress sniffer traffic dsptest # jitter  listener

#jitter_SOURCES = jitter.c
#jitter_LDADD = -lm

noinst_HEADERS = longtime.h

sniffer_SOURCES = sniffer.c
sniffer_LDADD = \
	-lpthread	\
	$(top_srcdir)/libskb/libskb.la	\
	$(top_srcdir)/libkstreamer/libkstreamer.la
sniffer_CPPFLAGS=\
	-I$(top_srcdir)/include/		\
	-I$(top_srcdir)/modules/include/	\
	-I$(top_srcdir)/libskb/			\
	-I$(top_srcdir)/libkstreamer/

#framesniffer_SOURCES = framesniffer.c
#framesniffer_LDADD =
#
#listener_SOURCES = listener.c
#listener_LDADD = -lasound
#

traffic_SOURCES = traffic.c
traffic_CPPFLAGS=\
	-I$(top_srcdir)/include/		\
	-I$(top_srcdir)/modules/include/	\
	-I$(top_srcdir)/libskb/			\
	-I$(top_srcdir)/libkstreamer/

vgsm_stress_SOURCES = vgsm_stress.c
vgsm_stress_CPPFLAGS=\
	-I$(top_srcdir)/include/		\
	-I$(top_srcdir)/modules/include/

dsptest_SOURCES = dsptest.c
dsptest_CPPFLAGS=

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall

if !inline
AM_CFLAGS += -fno-inline
endif
