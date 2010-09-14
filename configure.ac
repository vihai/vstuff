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

AC_PREREQ(2.59)
AC_INIT(vstuff, head, daniele@orlandi.com)
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE(-Wall)

AC_CONFIG_SRCDIR([chan_visdn/chan_visdn.c])
AC_CONFIG_HEADER([config.h])

## Checks for libraries.
## FIXME: Replace `main' with a function in `-lq931':
#AC_CHECK_LIB([q931], [main])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL
AC_PROG_AWK

AC_PROG_YACC
AC_PROG_LEX

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netinet/in.h stdint.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_SELECT_ARGTYPES
AC_FUNC_SETVBUF_REVERSED
AC_TYPE_SIGNAL
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([gettimeofday memset select socket strcasecmp strchr strdup strerror strncasecmp strrchr strstr])

visdnhwconfdir="$sysconfdir/visdn"
AC_SUBST(visdnhwconfdir)

#-------------------------------------------------------------------------

AC_ARG_WITH(zlib,
  AC_HELP_STRING( [--with-zlib@<:@=DIR@:>@],
                  [use zlib (located in directory DIR, if supplied).  @<:@default=yes, if available@:>@]),
[
	if test $withval = no
	then
		want_zlib=no
	elif test $withval = yes
	then
		want_zlib=yes
	else
		want_zlib=yes
		zlib_dir=$withval
	fi
],[
	want_zlib=yes
	zlib_dir=
])

AC_MSG_CHECKING([zlib])
AC_VSTUFF_ZLIB_CHECK

if test "x$want_zlib" = "xno" ; then
	AC_MSG_RESULT([failed!])
	AC_MSG_ERROR([zlib not found])
fi

#-----------------------------------------------------------------------------

AC_ARG_ENABLE(
	[kernel-modules],
	AC_HELP_STRING(
		[--enable-kernel-modules],
		[Enable kernel modules]),
	[enable_kernel_modules="$enableval"],
	[enable_kernel_modules="auto"])

if [[ "$enable_kernel_modules" != "no" ]]; then

AC_MSG_CHECKING([kernel build directory])

AC_ARG_WITH(
	[kernel-build],
	AC_HELP_STRING(
		[--with-kernel-build=DIR],
		[Specify kernel build environment for external modules \								compilation]),
	[kblddir=$withval],
	[
		if [[ -d "/lib/modules/`uname -r`/build" ]]; then
			kblddir="/lib/modules/`uname -r`/build"
		elif [[ -d "/usr/src/linux" ]]; then
			kblddir="/usr/src/linux"
		fi
	])

if [[ "$enable_kernel_modules" = "yes" -o \
      "$enable_kernel_modules" = "auto" ]]; then

	if [[ ! -d "$kblddir" ]]; then
		AC_MSG_RESULT([failed!])
		AC_MSG_ERROR([Cannot locate kernel sources])
	fi

	if [[ ! -f "$kblddir/.config" ]]; then
		AC_MSG_RESULT([failed!])
		AC_MSG_ERROR([Kernel build environment is missing .config \
				(missing .config)])
	fi

	if [[ ! -f "$kblddir/include/linux/kernel.h" ]]; then
		AC_MSG_RESULT([failed!])
		AC_MSG_ERROR([Kernel build environment is missing \
				include/linux/kernel.h])
	fi
fi

AC_SUBST(kblddir)
AC_MSG_RESULT([$kblddir])

fi

AM_CONDITIONAL(kernel_modules, test "$enable_kernel_modules" != "no")

#-----------------------------------------------------------------------------

AC_ARG_ENABLE(
	[asterisk-modules],
	AC_HELP_STRING(
		[--enable-asterisk-modules],
		[Enable asterisk modules]),
	[enable_asterisk_modules="$enableval"],
	[enable_asterisk_modules="auto"])

if [[ "$enable_asterisk_modules" != "no" ]]; then

AC_MSG_CHECKING([asterisk modules directory])

AC_ARG_WITH(
	[asterisk-modules],
	AC_HELP_STRING(
		[--with-asterisk-modules=DIR],
		[Specify asterisk modules directory]),
	[astmoddir=$withval],
	[
		if [[ -d "/usr/lib/asterisk/modules" ]]; then
			astmoddir="/usr/lib/asterisk/modules"
		else
			enable_asterisk_modules="no"
		fi
	])

if [[ "$enable_asterisk_modules" = "yes" ]]; then
	if [[ ! -d "$astmoddir" ]]; then
		AC_MSG_RESULT([failed!])
		AC_MSG_ERROR([Cannot locate asterisk modules directory])
	fi
fi

AC_SUBST(astmoddir)
AC_MSG_RESULT([$astmoddir])

#----------------

AC_MSG_CHECKING([asterisk includes])

AC_ARG_WITH(
	[asterisk-includes],
	AC_HELP_STRING(
		[--with-asterisk-includes=DIR],
		[Specify asterisk includes directory (the prefix before \
		asterisk/)]),
	[astincdir=$withval],
	[
		if [[ -d "/usr/include/asterisk" ]]; then
			astincdir="/usr/include/"
		else
			enable_asterisk_modules="no"
		fi
	])

if [[ "$enable_asterisk_modules" = "yes" ]]; then
	if [[ ! -d "$astincdir/asterisk" ]]; then
		AC_MSG_RESULT([failed!])
		AC_MSG_ERROR([Cannot locate asterisk includes directory])
	fi
fi

CPPFLAGS="$CPPFLAGS -I$astincdir"

AC_SUBST(astincdir)
AC_MSG_RESULT([$astincdir])

AC_CHECK_HEADERS(asterisk/version.h)

#---------------

AC_MSG_CHECKING([asterisk configuration directory])

AC_ARG_WITH(
	[asterisk-config],
	AC_HELP_STRING(
		[--with-asterisk-config=DIR],
		[Specify asterisk configuration directory]),
	[astconfdir=$withval],
	[
		if [[ -d "/etc/asterisk" ]]; then
			astconfdir="/etc/asterisk"
		else
			enable_asterisk_modules="no"
		fi
	])

if [[ "$enable_asterisk_modules" = "yes" ]]; then
	if [[ ! -d "$astconfdir" ]]; then
		AC_MSG_RESULT([failed!])
		AC_MSG_ERROR([Cannot locate asterisk config directory])
	fi
fi

AC_MSG_RESULT([$astconfdir])
AC_SUBST(astconfdir)

fi

AM_CONDITIONAL(asterisk_modules, test "$enable_asterisk_modules" != "no")

#-----------------------------------------------------------------------------

AC_ARG_ENABLE(
	[pppd-plugin],
	AC_HELP_STRING(
		[--enable-pppd-plugin],
		[Enable pppd plugin]),
	[enable_pppd="$enableval"],
	[enable_pppd="auto"])

if [[ "$enable_pppd" != "no" ]]; then

AC_MSG_CHECKING([pppd plugins directory])

AC_ARG_WITH(
	[pppd-plugins],
	AC_HELP_STRING(
		[--with-pppd-plugins=DIR],
		[Specify PPPD plugins directory]),
	[pppdpluginsdir="$withval"],
	[
		if [[ -d "/usr/lib/pppd/" -a \
		      -d "/usr/lib/pppd/`/bin/ls -Ar1 /usr/lib/pppd/ \
						| head -n 1`" ]]; then
			pppdpluginsdir="/usr/lib/pppd/`/bin/ls -Ar1 \
						/usr/lib/pppd/ | head -n 1`"
		else
			enable_pppd="no"
		fi
	])

if [[ "$enable_pppd" = "yes" ]]; then
	if [[ ! -d "$pppdpluginsdir" ]]; then
		AC_MSG_RESULT([failed!])
		AC_MSG_ERROR([Cannot locate pppd plugins directory])
	fi
fi

AC_SUBST(pppdpluginsdir)
AC_MSG_RESULT([$pppdpluginsdir])

#----------

AC_MSG_CHECKING([pppd includes directory])

AC_ARG_WITH(
	[pppd-includes],
	AC_HELP_STRING(
		[--with-pppd-includes],
		[Specify pppd includes directory]),
	[pppdincdir=$withval],
	[
		if [[ -d "/usr/include/pppd/" ]]; then
			pppdincdir="/usr/include/pppd/"

			if [[ ! -f "$pppdincdir/pppd.h" -o \
			      ! -f "$pppdincdir/fsm.h" -o \
			      ! -f "$pppdincdir/lcp.h" ]]; then
				enable_pppd="no"
			fi
		else
			enable_pppd="no"
		fi
	])

if [[ "$enable_pppd" = "yes" ]]; then
	if [[ ! -d "$pppdincdir" ]]; then
		AC_MSG_RESULT([failed!])
		AC_MSG_ERROR([Cannot locate pppd includes directory])
	fi

	if [[ ! -f "$pppdincdir/pppd.h" -o \
	      ! -f "$pppdincdir/fsm.h" -o \
	      ! -f "$pppdincdir/lcp.h" ]]; then
		AC_MSG_RESULT([failed!])
		AC_MSG_ERROR([Missing pppd headers])
	fi
fi

AC_MSG_RESULT([$pppdincdir])
AC_SUBST(pppdincdir)

#------------

AC_MSG_CHECKING([pppd config directory])

AC_ARG_WITH(
	[pppd-config],
	AC_HELP_STRING(
		[--with-pppd-config],
		[Specify pppd configuration directory]),
	[pppdconfdir=$withval],
	[
		if [[ -d "/etc/ppp/" ]]; then
			pppdconfdir="/etc/ppp/"
		else
			enable_pppd="no"
		fi
	]
	)

if [[ "$enable_pppd" = "yes" ]]; then
	if [[ ! -d $pppdconfdir ]]; then
		AC_MSG_RESULT([failed!])
		AC_MSG_ERROR([Cannot locate pppd configuration directory])
	fi
fi

AC_SUBST(pppdconfdir)
AC_MSG_RESULT([$pppdconfdir])

fi

AM_CONDITIONAL(pppd_plugin, test "$enable_pppd" != "no")

#-----------------------------------------------------------------------------

AC_ARG_ENABLE(
	[tests],
	AC_HELP_STRING(
		[--enable-tests],
		[Enable testing tools]),
	[enable_tests="$enableval"],
	[enable_tests="no"])

AM_CONDITIONAL(tests, test "$enable_tests" != "no")

#-----------------------------------------------------------------------------

enable_debug_code="yes"

AC_ARG_ENABLE(
	[debug-code],
	AC_HELP_STRING(
		[--enable-debug],
		[Debugging code]))
AC_SUBST(enable_debug_code)

if [[ "$enable_debug_code" = "yes" ]]; then
	AC_DEFINE(DEBUG_CODE, 1, [Debugging code])
fi

#-----------------------------------------------------------------------------

AC_ARG_ENABLE(
	[debug-defaults],
	AC_HELP_STRING(
		[--enable-debug-defaults],
		[Debugging defaults]))
AC_SUBST(enable_debug_defaults)

if [[ "$enable_debug_defaults" = "yes" ]]; then
	AC_DEFINE(DEBUG_DEFAULTS, 1, [Debugging defaults])
fi

#-----------------------------------------------------------------------------

AC_ARG_ENABLE(
	[inline],
	AC_HELP_STRING(
		[--disable-inline],
		[Disable code inlining where possible (for debugging)]))

AM_CONDITIONAL(inline, test "$enable_inline" != no)

#-----------------------------------------------------------------------------

AC_ARG_ENABLE(
	[astmm],
	AC_HELP_STRING(
		[--enable-astmm],
		[Enable Asterisk's Memory Debugger]))

AM_CONDITIONAL(astmm, test "$enable_astmm" = yes)

#-----------------------------------------------------------------------------

drivers="hfc-pci, hfc-4s, hfc-usb"

AC_ARG_ENABLE(
	[drivers],
	AC_HELP_STRING(
		[--enable-drivers=<list>],
		[Enable drivers (comma separated list of driver names), \
		currently supported: hfc-usb, hfc-pci, hfc-4s, hfc-e1, vgsm, vgsm2, vdsp]),
	drivers="$enableval")

AM_CONDITIONAL(driver_hfc_usb,
	test -n "`echo $drivers | grep hfc-usb`" || test "$drivers" = "all")
AM_CONDITIONAL(driver_hfc_pci,
	test -n "`echo $drivers | grep hfc-pci`" || test "$drivers" = "all")
AM_CONDITIONAL(driver_hfc_4s,
	test -n "`echo $drivers | grep hfc-4s`" || test "$drivers" = "all")
AM_CONDITIONAL(driver_hfc_e1,
	test -n "`echo $drivers | grep hfc-e1`" || test "$drivers" = "all")
AM_CONDITIONAL(driver_vgsm,
	test -n "`echo $drivers | grep vgsm`" || test "$drivers" = "all")
AM_CONDITIONAL(driver_vgsm2,
	test -n "`echo $drivers | grep vgsm2`" || test "$drivers" = "all")
AM_CONDITIONAL(driver_vdsp,
	test -n "`echo $drivers | grep vdsp`" || test "$drivers" = "all")

AC_CONFIG_FILES([Makefile
		tools/Makefile
		libskb/Makefile
		libkstreamer/Makefile
		libq931/Makefile
		res_kstreamer/Makefile
		chan_visdn/Makefile
		chan_vgsm/Makefile
		app_pipe/Makefile
		kstool/Makefile
		visdn_netdev_ctl/Makefile
		vgsmctl/Makefile
		vgsm2test/Makefile
		pppd_plugin/Makefile
		samples/Makefile
		unsupported/Makefile
		tests/Makefile
		modules/include/kernel_config.h
		modules/Makefile
		modules/kstreamer/Makefile
		modules/visdn/Makefile
		modules/lapd/Makefile
		modules/softswitch/Makefile
		modules/userport/Makefile
		modules/netdev/Makefile
		modules/ppp/Makefile
		modules/ec/Makefile
		modules/milliwatt/Makefile
		modules/hfc-4s/Makefile
		modules/hfc-e1/Makefile
		modules/hfc-pci/Makefile
		modules/hfc-usb/Makefile
		modules/vgsm/Makefile
		modules/vgsm2/Makefile
		modules/vdsp/Makefile
		scripts/Makefile
		scripts/visdn_configurator
		doxygen.conf
		rpm/vstuff-centos-kmod-el5.spec
		rpm/vstuff-centos.spec
		rpm/vstuff-suse.spec
		])

#		modules/hfc-e1/Makefile

AC_OUTPUT
