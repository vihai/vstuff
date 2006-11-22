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

bin_PROGRAMS = vgsm_stress sniffer ksmonitor # jitter traffic listener

INCLUDES = \
	-I$(top_srcdir)/include/		\
	-I$(top_srcdir)/modules/include/	\
	-I$(top_srcdir)/libskb/			\
	-I$(top_srcdir)/libkstreamer/		\
	-I$(top_srcdir)/libq931/

#jitter_SOURCES = jitter.c
#jitter_LDADD = -lm

sniffer_SOURCES = sniffer.c
sniffer_LDADD = \
	-lpthread	\
	$(top_srcdir)/libskb/libskb.la	\
	$(top_srcdir)/libkstreamer/libkstreamer.la

ksmonitor_SOURCES = ksmonitor.c
ksmonitor_LDADD = \
	-lpthread	\
	$(top_srcdir)/libskb/libskb.la	\
	$(top_srcdir)/libkstreamer/libkstreamer.la

#framesniffer_SOURCES = framesniffer.c
#framesniffer_LDADD =
#
#listener_SOURCES = listener.c
#listener_LDADD = -lasound
#
#traffic_SOURCES = traffic.c

vgsm_stress_SOURCES = vgsm_stress.c

AM_CFLAGS = -D_REENTRANT -DGNU_SOURCE -Wall

if !inline
AM_CFLAGS += -fno-inline
endif
