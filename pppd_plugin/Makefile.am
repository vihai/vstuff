
AM_CFLAGS = -D_REENTRANT -Wall

pppdplugins_LTLIBRARIES = visdn.la
visdn_la_SOURCES = visdn.c

INCLUDES =					\
	-I$(top_builddir)			\
	-I$(top_builddir)/modules/include/	\
	-I$(pppdincdir)

visdn_la_LIBADD =
visdn_la_LDFLAGS = -module -avoid-version -no-undefined
