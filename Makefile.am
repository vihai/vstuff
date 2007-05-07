#
# vStuff main Makefile
#
# Copyright (C) 2004-2007 Daniele Orlandi
#
# Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
#
# This program is free software and may be modified and distributed
# under the terms and conditions of the GNU General Public License.
#

noinst_DATA =					\
	include/list.h

EXTRA_DIST =					\
	$(sysconf_DATA)				\
	$(astconf_DATA)				\
	$(noinst_DATA)				\
	vstuff.spec				\
	visdn-init				\
	visdn-sleep.diff			\
	visdn-sysconfig				\
	modules/linux_compat/linux/kfifo.h	\
	modules/include/kernel_config.h.in	\
	modules/include/linux/lapd.h		\
	modules/include/linux/vgsm.h		\
	modules/include/linux/vgsm2.h		\
	modules/include/linux/kstreamer/*.h	\
	modules/include/linux/visdn/*.h		\
	docs/*					\
	vgsm_firmware/*.img			\
	vgsm_firmware/README			\
	vgsm2_firmware/*.fw			\
	vgsm2_firmware/README

SUBDIRS = tools

if kernel_modules
SUBDIRS += modules
endif

SUBDIRS += libskb
SUBDIRS += libkstreamer
SUBDIRS += libq931
SUBDIRS += scripts samples kstool vgsmctl
SUBDIRS += visdn_netdev_ctl

if asterisk_modules
SUBDIRS += res_kstreamer
SUBDIRS += chan_visdn
SUBDIRS += chan_vgsm
SUBDIRS += app_pipe
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
	-gpg --sign -ab $(distdir).tar.gz
	-gpg --sign -ab $(distdir).tar.bz2
	scp $(distdir).tar.gz $(distdir).tar.gz.asc \
	    $(distdir).tar.bz2 $(distdir).tar.bz2.asc \
	    root@aj.uli.it:/www/www.visdn.org/htdocs/download/

doxy:
	doxygen doxygen.conf

