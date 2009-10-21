#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(rigel, 0.99.3)
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE([dist-bzip2 -Wall])

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

MAKE_SUBDIRS="libs src utils"

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_RANLIB

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([fcntl.h stdint.h stdlib.h string.h sys/time.h termios.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_HEADER_TIME
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_SELECT_ARGTYPES
AC_TYPE_SIGNAL
AC_CHECK_FUNCS([memmove memset select strerror strncasecmp])

AC_CONFIG_FILES([Makefile libs/Makefile src/Makefile utils/Makefile])
AC_OUTPUT

AC_MSG_RESULT([

rigel-$PACKAGE_VERSION is now configured and ready to be built

   Build: $build
   Host:  $host
   Source Directory: $srcdir
   Install prefix:   $prefix
   C compiler:       $CC $CFLAGS
])
