
#AUX_DIST =					\
#	$(ac_aux_dir)/config.guess		\
#	$(ac_aux_dir)/config.sub		\
#	$(ac_aux_dir)/install-sh		\
#	$(ac_aux_dir)/ltconfig			\
#	$(ac_aux_dir)/ltmain.sh			\
#	$(ac_aux_dir)/mdate-sh			\
#	$(ac_aux_dir)/missing			\
#	$(ac_aux_dir)/mkinstalldirs		\
#
#EXTRA_DIST = bootstrap
#
#MAINTAINERCLEANFILES =				\
#	Makefile.in aclocal.m4			\
#	configure config-h.in stamp-h.in	\
#	$(AUX_DIST)

SUBDIRS =

if kernel_modules
SUBDIRS += visdn-core lapd timer-system softport
SUBDIRS += fake_isdn
SUBDIRS += hfc-pci
SUBDIRS += hfc-4s 
#SUBDIRS += hfc-e1
#SUBDIRS += hfc-usb
endif

SUBDIRS += libq931
SUBDIRS += chan_visdn

if tests
SUBDIRS += tests
endif
