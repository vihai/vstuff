
AM_CFLAGS = -D_REENTRANT -Wall

sbin_PROGRAMS = visdnctl

INCLUDES = \
	-I$(top_srcdir)/include/		\
	-I$(top_srcdir)/modules/include/

visdnctl_SOURCES = \
	visdnctl.c		\
	connect.c		\
	disconnect.c		\
	pipeline_open.c		\
	pipeline_close.c	\
	pipeline_start.c	\
	pipeline_stop.c		\
	netdev.c

visdnctl_LDADD =

noinst_HEADERS = \
	visdnctl.h		\
	connect.h		\
	disconnect.h		\
	pipeline_open.h		\
	pipeline_close.h	\
	pipeline_start.h	\
	pipeline_stop.h		\
	netdev.h
