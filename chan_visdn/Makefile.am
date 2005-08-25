
astmod_LTLIBRARIES = chan_visdn.la
chan_visdn_la_SOURCES = chan_visdn.c

INCLUDES =				\
	-I$(top_builddir)		\
	-I$(top_builddir)/visdn/	\
	-I$(top_builddir)/lapd/		\
	-I$(top_builddir)/visdn-core/	\
	-I$(top_builddir)/softport/	\
	-I$(astincdir)

chan_visdn_la_LIBADD = ../libq931/libq931.la
chan_visdn_la_LDFLAGS = -module

AM_CFLAGS = -D_REENTRANT -D_GNU_SOURCE
