
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

sysconf_DATA =					\
	etc/udev/30-visdn.rules	

astconf_DATA =					\
	etc/asterisk/visdn.conf

EXTRA_DIST =					\
	bootstrap				\
	visdn.spec				\
	etc/ethereal_coloring_rules		\
	$(sysconf_DATA)				\
	$(astconf_DATA)

SUBDIRS =

if kernel_modules
SUBDIRS += visdn-core lapd visdn-timer-system visdn-streamport	\
	visdn-netdev visdn-ppp

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
SUBDIRS += app_visdn_ppp
endif

if pppd
SUBDIRS += pppd_plugin
endif

if tests
SUBDIRS += tests
endif

dist-rpm: dist-bzip2
	rpmbuild -ta @PACKAGE_TARNAME@-@PACKAGE_VERSION@.tar.bz2
