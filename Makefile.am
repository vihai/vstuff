
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

#
#MAINTAINERCLEANFILES =				\
#	Makefile.in aclocal.m4			\
#	configure config-h.in stamp-h.in	\
#	$(AUX_DIST)

EXTRA_DIST =
	bootstrap				\
	etc/ethereal_coloring_rules

sysconf_DATA =
	etc/ethereal_coloring_rules		\
	etc/udev/30-visdn.rules			\
	etc/sysconfig/scripts/ifup-visdn	\
	etc/sysconfig/network/ifcfg-visdn0

astconf_DATA =
	etc/asterisk/visdn.conf


SUBDIRS =

if kernel_modules
SUBDIRS += visdn-core lapd timer-system softport

if driver_hfc_usb
SUBDIRS += hfc-usb
endif

if driver_hfc_pci
SUBDIRS += hfc-pci
endif

if driver_hfc_4s
SUBDIRS += hfc-4s 
endif

if driver_hfc_e1
SUBDIRS += hfc-e1
endif

if driver_fake_isdn
SUBDIRS += fake_isdn
endif

endif

SUBDIRS += libq931

if asterisk
SUBDIRS += chan_visdn
endif

if tests
SUBDIRS += tests
endif
