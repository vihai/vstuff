
AM_CFLAGS = -D_REENTRANT -Wall

sbin_PROGRAMS = vgsmctl

INCLUDES = \
	-I$(top_srcdir)/include/		\
	-I$(top_srcdir)/modules/include/

vgsmctl_SOURCES = \
	vgsmctl.c	\
	codec.c

vgsmctl_LDADD =

noinst_HEADERS = \
	vgsmctl.h	\
	codec.h
