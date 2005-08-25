# Makefile.in: hybrid automake-generated and Kbuild makefile to compile
# kernel modules



subdir = softport
DIST_SOURCES = softport_main.c
DIST_HEADERS = softport.h
DIST_COMMON = $(srcdir)/Makefile.in

srcdir = .
top_srcdir = ..

pkgdatadir = $(datadir)/visdn
pkglibdir = $(libdir)/visdn
pkgincludedir = $(includedir)/visdn
top_builddir = ..
INSTALL = /usr/bin/install -c
install_sh_DATA = $(install_sh) -c -m 644
install_sh_PROGRAM = $(install_sh) -c
install_sh_SCRIPT = $(install_sh) -c
INSTALL_HEADER = $(INSTALL_DATA)
transform = $(program_transform_name)
build_triplet = i686-suse-linux
host_triplet = i686-suse-linux
mkinstalldirs = $(install_sh) -d
CONFIG_HEADER = $(top_builddir)/config.h
CONFIG_CLEAN_FILES =
DISTFILES = $(DIST_COMMON) $(DIST_SOURCES) $(DIST_HEADERS) $(TEXINFOS) $(EXTRA_DIST)
ACLOCAL = ${SHELL} /root/isdn--devel/config/missing --run aclocal-1.9
AR = ar
AUTOCONF = ${SHELL} /root/isdn--devel/config/missing --run autoconf
AUTOHEADER = ${SHELL} /root/isdn--devel/config/missing --run autoheader
AUTOMAKE = ${SHELL} /root/isdn--devel/config/missing --run automake-1.9
INSTALL_DATA = ${INSTALL} -m 644
INSTALL_PROGRAM = ${INSTALL}
INSTALL_SCRIPT = ${INSTALL}
INSTALL_STRIP_PROGRAM = ${SHELL} $(install_sh) -c -s
PACKAGE = visdn
PACKAGE_BUGREPORT = daniele@orlandi.com
PACKAGE_NAME = visdn
PACKAGE_STRING = visdn 0.0.1
PACKAGE_TARNAME = visdn
PACKAGE_VERSION = 0.0.1
PATH_SEPARATOR = :
SHELL = /bin/sh
STRIP = strip
VERSION = 0.0.1
bindir = ${exec_prefix}/bin
build = i686-suse-linux
build_alias = 
build_cpu = i686
build_os = linux
build_vendor = suse
datadir = ${prefix}/share
exec_prefix = ${prefix}
host = i686-suse-linux
host_alias = 
host_cpu = i686
host_os = linux
host_vendor = suse
includedir = ${prefix}/include
infodir = ${prefix}/info
install_sh = /root/isdn--devel/config/install-sh
libdir = ${exec_prefix}/lib
libexecdir = ${exec_prefix}/libexec
localstatedir = ${prefix}/var
mandir = ${prefix}/man
mkdir_p = mkdir -p --
oldincludedir = /usr/include
prefix = /usr/local
program_transform_name = s,x,x,
sbindir = ${exec_prefix}/sbin
sharedstatedir = ${prefix}/com
sysconfdir = ${prefix}/etc
target_alias = 

ksrcdir = /usr/src/linux

# Use $(src) when we are run inside Kbuild
ifdef src
	abssrcdir = $(src)
else
	abssrcdir = $(shell pwd)
endif

EXTRA_CFLAGS=-I $(abssrcdir)/../visdn-core/

########################## Targets #################################

# Kbuild:
obj-m	:= softport.o
softport-y	:= softport_main.o
# end Kbuild

all:
	$(MAKE) -C $(ksrcdir) modules M=${abssrcdir}
install:
	$(MAKE) -C $(ksrcdir) modules_install M=${abssrcdir}
clean:
	$(MAKE) -C $(ksrcdir) clean M=${abssrcdir}

tags:
ctags:

.PRECIOUS: Makefile
Makefile: $(srcdir)/Makefile.in $(top_builddir)/config.status
	@case '$?' in \
	  *config.status*) \
	    cd $(top_builddir) && $(MAKE) $(AM_MAKEFLAGS) am--refresh;; \
	  *) \
	    echo ' cd $(top_builddir) && $(SHELL) ./config.status $(subdir)/$@ $(am__depfiles_maybe)'; \
	    cd $(top_builddir) && $(SHELL) ./config.status $(subdir)/$@ $(am__depfiles_maybe);; \
	esac;

distdir: $(DISTFILES)
	@srcdirstrip=`echo "$(srcdir)" | sed 's|.|.|g'`; \
	topsrcdirstrip=`echo "$(top_srcdir)" | sed 's|.|.|g'`; \
	list='$(DISTFILES)'; for file in $$list; do \
	  case $$file in \
	    $(srcdir)/*) file=`echo "$$file" | sed "s|^$$srcdirstrip/||"`;; \
	    $(top_srcdir)/*) file=`echo "$$file" | sed "s|^$$topsrcdirstrip/|$(top_builddir)/|"`;; \
	  esac; \
	  if test -f $$file || test -d $$file; then d=.; else d=$(srcdir); fi; \
	  dir=`echo "$$file" | sed -e 's,/[^/]*$$,,'`; \
	  if test "$$dir" != "$$file" && test "$$dir" != "."; then \
	    dir="/$$dir"; \
	    $(mkdir_p) "$(distdir)$$dir"; \
	  else \
	    dir=''; \
	  fi; \
	  if test -d $$d/$$file; then \
	    if test -d $(srcdir)/$$file && test $$d != $(srcdir); then \
	      cp -pR $(srcdir)/$$file $(distdir)$$dir || exit 1; \
	    fi; \
	    cp -pR $$d/$$file $(distdir)$$dir || exit 1; \
	  else \
	    test -f $(distdir)/$$file \
	    || cp -p $$d/$$file $(distdir)/$$file \
	    || exit 1; \
	  fi; \
	done
check:
installdirs:

install-exec:
install-data:
uninstall:

installcheck:
install-strip:
mostlyclean-generic:

clean-generic:


distclean-generic:
	-test -z "$(CONFIG_CLEAN_FILES)" || rm -f $(CONFIG_CLEAN_FILES)

maintainer-clean-generic:
	@echo "This command is intended for maintainers to use"
	@echo "it deletes files that may require special tools to rebuild."

distclean:
	-rm -f Makefile

dvi:
html:
info: 
install-info:
install-man:
maintainer-clean:
	-rm -f Makefile
mostlyclean:
pdf:
ps:

.PHONY: all check clean clean-generic clean-libtool \
	distclean distclean-generic distclean-libtool distdir dvi \
	html info install \
	install-data install-exec \
	install-info install-man \
	install-strip installcheck installdirs \
	maintainer-clean maintainer-clean-generic mostlyclean \
	mostlyclean-generic mostlyclean-libtool pdf ps \
	uninstall
