
AM_CFLAGS = -D_REENTRANT -Wall

sbin_PROGRAMS = vgsmctl

INCLUDES = \
	-I$(top_srcdir)/include/		\
	-I$(top_srcdir)/modules/include/

vgsmctl_SOURCES = \
	vgsmctl.c	\
	codec.c		\
	power.c		\
	pad_timeout.c	\
	fw_version.c	\
	fw_upgrade.c

vgsmctl_LDADD = -lz

noinst_HEADERS = \
	vgsmctl.h	\
	codec.h		\
	power.h		\
	pad_timeout.h	\
	fw_version.h	\
	fw_upgrade.h
