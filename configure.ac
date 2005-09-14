AC_PREREQ(2.59)
AC_INIT(visdn, 0.0.0, daniele@orlandi.com)
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE

AC_CONFIG_SRCDIR([chan_visdn/chan_visdn.c])
AC_CONFIG_HEADER([config.h])

## Checks for libraries.
## FIXME: Replace `main' with a function in `-lq931':
#AC_CHECK_LIB([q931], [main])

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL
AC_PROG_AWK

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

#-----------------------------------------------------------------------------

AC_ARG_WITH(
	[kernel-sources],
	AC_HELP_STRING(
		[--with-kernel-sources],
		[Specify kernel sourcetree to compile modules]),
	[ksrcdir=$withval],
	[ksrcdir="/usr/src/linux"])
AC_SUBST(ksrcdir)

AM_CONDITIONAL(kernel_modules, test "$ksrcdir" != no)

AC_ARG_WITH(
	[kernel-have-sk-prot],
	AC_HELP_STRING(
		[--with-kernel-have-sk-prot],
		[Fedora kernel have sk_prot before 2.6.12]),)
AC_SUBST(with_kernel_have_sk_prot)

#-----------------------------------------------------------------------------

with_asterisk=yes

AC_ARG_WITH(
	[asterisk],
	AC_HELP_STRING(
		[--with-asterisk],
		[Enable asterisk module compilation]),)

AM_CONDITIONAL(asterisk, test "$with_asterisk" = yes)

AC_ARG_WITH(
	[asterisk-modules],
	AC_HELP_STRING(
		[--with-asterisk-modules],
		[Specify asterisk modules directory]),
	[astmoddir=$withval],
	[astmoddir="/usr/lib/asterisk/modules/"])

test $prefix != "NONE" && astmoddir="$prefix/$astmoddir"

AC_SUBST(astmoddir)

AC_ARG_WITH(
	[asterisk-includes],
	AC_HELP_STRING(
		[--with-asterisk-includes],
		[Specify asterisk includes directory]),
	[astincdir=$withval],
	[astincdir="/usr/include/"])
AC_SUBST(astincdir)

AC_ARG_WITH(
	[asterisk-config],
	AC_HELP_STRING(
		[--with-asterisk-config],
		[Specify asterisk configuration directory]),
	[astconfdir=$withval],
	[astconfdir="/etc/asterisk/"])

test $prefix != "NONE" && astconfdir="$prefix/$astconfdir"

AC_SUBST(astconfdir)

#-----------------------------------------------------------------------------

with_pppd=yes

AC_ARG_WITH(
	[pppd],
	AC_HELP_STRING(
		[--with-pppd],
		[Enable PPPD plugin compilation]),)

AM_CONDITIONAL(pppd, test "$with_pppd" = yes)

AC_ARG_WITH(
	[pppd-plugins],
	AC_HELP_STRING(
		[--with-pppd-plugins],
		[Specify PPPD plugins directory]),
	[pppdpluginsdir=$withval],
	[pppdpluginsdir="/usr/lib/pppd/2.4.2"])

test $prefix != "NONE" && pppdpluginsdir="$prefix/$pppdpluginsdir"

AC_SUBST(pppdpluginsdir)

AC_ARG_WITH(
	[pppd-includes],
	AC_HELP_STRING(
		[--with-pppd-includes],
		[Specify pppd includes directory]),
	[pppdincdir=$withval],
	[pppdincdir="/usr/include/pppd"])
AC_SUBST(pppdincdir)

################ CHECK HEADERS EXISTANCE

AC_ARG_WITH(
	[pppd-config],
	AC_HELP_STRING(
		[--with-pppd-config],
		[Specify pppd configuration directory]),
	[pppdconfdir=$withval],
	[pppdconfdir="/etc/ppp"])

test $prefix != "NONE" && pppdconfdir="$prefix/$pppdconfdir"

AC_SUBST(pppdconfdir)

#-----------------------------------------------------------------------------

AC_ARG_ENABLE(
	[tests],
	AC_HELP_STRING(
		[--enable-tests],
		[Enable testing tools]))

AM_CONDITIONAL(tests, test "$enable_tests" = yes)

#-----------------------------------------------------------------------------

AC_ARG_ENABLE(
	[debug],
	AC_HELP_STRING(
		[--enable-debug],
		[Debugging defaults]))
AC_SUBST(enable_debug)
AC_DEFINE(DEBUG, 1, [Debugging defaults])

#-----------------------------------------------------------------------------
drivers="hfc-pci, hfc-4s"

AC_ARG_ENABLE(
	[drivers],
	AC_HELP_STRING(
		[--enable-drivers=<list>],
		[Enable drivers (comma separated list of driver names), currently supported: hfc-usb, hfc-pci, hfc-4s, hfc-e1, fake-isdn]),
	drivers="$enableval")

AM_CONDITIONAL(driver_hfc_usb,
	test -n "`echo $drivers | grep hfc-usb`" || test "$drivers" = "all")
AM_CONDITIONAL(driver_hfc_pci,
	test -n "`echo $drivers | grep hfc-pci`" || test "$drivers" = "all")
AM_CONDITIONAL(driver_hfc_4s,
	test -n "`echo $drivers | grep hfc-4s`" || test "$drivers" = "all")
AM_CONDITIONAL(driver_hfc_e1,
	test -n "`echo $drivers | grep hfc-e1`" || test "$drivers" = "all")
AM_CONDITIONAL(driver_fake_isdn,
	test -n "`echo $drivers | grep fake_isdn`" || test "$drivers" = "all")

AC_CONFIG_FILES([Makefile
                 libq931/Makefile
                 chan_visdn/Makefile
                 app_visdn_ppp/Makefile
                 pppd_plugin/Makefile
                 tests/Makefile
		 kernel_config.h
                 fake_isdn/Makefile
                 hfc-4s/Makefile
                 hfc-e1/Makefile
                 hfc-pci/Makefile
                 hfc-usb/Makefile
                 lapd/Makefile
                 visdn-cxc-internal/Makefile
                 visdn-core/Makefile
                 visdn-streamport/Makefile
                 visdn-timer-system/Makefile
                 visdn-netdev/Makefile
                 visdn-ppp/Makefile
                 visdn.spec])

AC_OUTPUT
