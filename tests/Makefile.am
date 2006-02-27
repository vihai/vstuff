
AM_CFLAGS = -D_REENTRANT -Wall

bin_PROGRAMS = jitter sniffer traffic

INCLUDES = \
	 -I$(top_srcdir)/modules/include/	\
	 -I$(top_srcdir)/libq931/

jitter_SOURCES = jitter.c
jitter_LDADD = -lm

sniffer_SOURCES = sniffer.c
sniffer_LDADD = -lasound

traffic_SOURCES = traffic.c
