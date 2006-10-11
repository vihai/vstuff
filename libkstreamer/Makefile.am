AM_CFLAGS = -D_REENTRANT -Wall

lib_LTLIBRARIES = libkstreamer.la

libkstreamer_la_SOURCES = \
	libkstreamer.c	\
	dynattr.c	\
	link.c		\
	netlink.c	\
	request.c	\
	node.c		\
	pipeline.c	\
	conn.c		\
	router.c


libkstreamer_ladir = libkstreamer

nobase_include_HEADERS = \
	dynattr.h	\
	libkstreamer.h	\
	link.h		\
	node.h		\
	pipeline.h	\
	conn.h		\
	request.h	\
	router.h	\
	util.h

INCLUDES =					\
	-I$(top_builddir)			\
	-I$(top_builddir)/include/		\
	-I$(top_builddir)/libskb/		\
	-I$(top_builddir)/modules/include/

libkstreamer_la_LDFLAGS = -module -avoid-version -no-undefined
