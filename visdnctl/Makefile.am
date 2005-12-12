
AM_CFLAGS = -D_REENTRANT -Wall

sbin_PROGRAMS = visdnctl

INCLUDES = \
	-I$(top_srcdir)/include/		\
	-I$(top_srcdir)/modules/include/

visdnctl_SOURCES = \
	visdnctl.c	\
	connect.c	\
	disconnect.c	\
	enable_path.c	\
	disable_path.c	\
	netdev.c

visdnctl_LDADD =

noinst_HEADERS = \
	visdnctl.h	\
	connect.h	\
	disconnect.h	\
	enable_path.h	\
	disable_path.h	\
	netdev.h
