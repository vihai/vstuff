
sysconf_DATA =					\
	etc/udev/30-visdn.rules			\
	etc/visdn/*

astconf_DATA =					\
	etc/asterisk/visdn.conf

dist_sbin_SCRIPTS =				\
	scripts/visdn_configurator		\
	scripts/visdn_connect			\
	scripts/visdn_create_netdev

EXTRA_DIST =					\
	$(sysconf_DATA)				\
	$(astconf_DATA)				\
	visdn.spec				\
	samples/ethereal_coloring_rules		\
	samples/ppp_dialout			\
	kernel_config.h.in

SUBDIRS =

if kernel_modules
SUBDIRS += visdn-core lapd visdn-timer-system visdn-streamport	\
	visdn-netdev visdn-cxc-internal visdn-ppp visdn-ec-kb

SUBDIRS += kfifo

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

#release: dist-gzip dist-bzip2 dist-rpm
release: distdir
	tardir=$(distdir) && $(am__tar) | GZIP=$(GZIP_ENV) gzip -c >$(distdir).tar.gz
	tardir=$(distdir) && $(am__tar) | bzip2 -9 -c >$(distdir).tar.bz2
	$(am__remove_distdir)
	gpg --sign -ab $(distdir).tar.gz
	gpg --sign -ab $(distdir).tar.bz2
	scp $(distdir).tar.gz $(distdir).tar.gz.asc \
	    $(distdir).tar.bz2 $(distdir).tar.bz2.asc \
	    root@aj.uli.it:/www/www.orlandi.com/htdocs/visdn/download/
