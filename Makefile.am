noinst_DATA =					\
	samples/visdn.conf			\
	samples/extensions.conf			\
	samples/ethereal_coloring_rules		\
	samples/ppp_dialout			\
	samples/30-visdn.rules-oldudev		\
	samples/device-pci-*

EXTRA_DIST =					\
	$(sysconf_DATA)				\
	$(astconf_DATA)				\
	$(noinst_DATA)				\
	visdn.spec				\
	linux_compat/linux/kfifo.h		\
	kernel_config.h.in

SUBDIRS =

SUBDIRS += etc scripts

if kernel_modules
SUBDIRS += visdn-core lapd visdn-cxc-internal visdn-timer-system \
	visdn-streamport visdn-netdev visdn-ppp visdn-ec-kb

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

if asterisk_modules
SUBDIRS += chan_visdn
SUBDIRS += app_visdn_ppp
endif

if pppd_plugin
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
