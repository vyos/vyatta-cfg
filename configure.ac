# Process this file with autoconf to produce a configure script.
AC_PREREQ(2.59)

AC_CONFIG_MACRO_DIR([m4])

m4_define([VERSION_ID], [m4_esyscmd([
	if test -f .version ; then
	    head -n 1 .version | tr -d \\n
	else
	    echo -n 2.4
	fi])])
AC_INIT([vyatta-cfg], VERSION_ID, [maintainers@vyos.net])

test -n "$VYATTA_VERSION" || VYATTA_VERSION=$PACKAGE_VERSION

AC_CONFIG_AUX_DIR([config])
AM_INIT_AUTOMAKE([gnu no-dist-gzip dist-bzip2 subdir-objects])
AM_SILENT_RULES([yes])
AC_PREFIX_DEFAULT([/opt/vyatta])

AC_PROG_CC
AC_PROG_CXX
AM_PROG_AS
AM_PROG_CC_C_O
AC_PROG_LIBTOOL
AC_PROG_LEX
AC_PROG_YACC
AC_PROG_LN_S

AC_ARG_ENABLE([nostrip],
	AC_HELP_STRING([--enable-nostrip],
	[include -nostrip option during packaging]),
	[NOSTRIP=-nostrip], [NOSTRIP=])

AC_ARG_ENABLE([unionfsfuse],
	AC_HELP_STRING([--enable-unionfsfuse],
	[use unionfsfuse instead of unionfs (default is no)]),
	[enable_unionfsfuse=yes], [enable_unionfsfuse=no])
AM_CONDITIONAL([USE_UNIONFSFUSE], [test "$enable_unionfsfuse" != no])

AC_CONFIG_FILES(
	[Makefile]
	[perl_dmod/Makefile]
  [debian/vyatta-cfg.postinst])

AC_SUBST(NOSTRIP)
PKG_CHECK_MODULES(GOBJECT, [gobject-2.0])
AC_SUBST(GOBJECT_CFLAGS)
AC_SUBST(GOBJECT_LIBS)

AC_OUTPUT


