
astmod_LTLIBRARIES = chan_visdn.la
chan_visdn_la_SOURCES = chan_visdn.c intf.c huntgroup.c ton.c overlap.c
noinst_HEADERS = chan_visdn.h intf.h huntgroup.h ton.h overlap.h util.h

INCLUDES =					\
	-I$(top_builddir)			\
	-I$(top_builddir)/include/		\
	-I$(top_builddir)/modules/include/	\
	-I$(top_builddir)/libq931/		\
	-I$(astincdir)

chan_visdn_la_LIBADD = ../libq931/libq931.la
chan_visdn_la_LDFLAGS = -module -avoid-version -no-undefined

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -Wall
