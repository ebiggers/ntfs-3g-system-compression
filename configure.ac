AC_INIT([ntfs-3g-system-compression], [0.4], [ebiggers3@gmail.com])

AC_CONFIG_SRCDIR([src/plugin.c])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([-Wall -Werror subdir-objects foreign])
AM_SILENT_RULES([yes])
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

AC_PROG_CC
AC_C_BIGENDIAN

LT_INIT([dlopen])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])

AC_CHECK_HEADERS([errno.h \
		  limits.h \
		  stdarg.h \
		  stddef.h \
		  stdlib.h \
		  string.h \
		  sys/types.h])

PKG_CHECK_MODULES([LIBNTFS_3G], [libntfs-3g >= 2017.3.23], [],
		  [AC_MSG_ERROR(["Unable to find libntfs-3g"])])
AC_OUTPUT
