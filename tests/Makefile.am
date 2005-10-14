
M_CFLAGS = -D_REENTRANT -Wall

bin_PROGRAMS = connect jitter sniffer traffic demoswitch

INCLUDES = \
	 -I$(top_srcdir)/visdn-core/	\
	 -I$(top_srcdir)/lapd/		\
	 -I$(top_srcdir)/libq931/

connect_SOURCES = connect.c

jitter_SOURCES = jitter.c
jitter_LDADD = -lm

sniffer_SOURCES = sniffer.c
sniffer_LDADD = -lasound

traffic_SOURCES = traffic.c

demoswitch_SOURCES = demoswitch.c
demoswitch_LDADD = $(top_srcdir)/libq931/libq931.la
