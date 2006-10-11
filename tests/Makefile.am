
AM_CFLAGS = -D_REENTRANT -Wall

bin_PROGRAMS = vgsm_stress sniffer # jitter traffic listener

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

