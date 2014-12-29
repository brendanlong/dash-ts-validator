AC_PREREQ([2.69])
AC_INIT([TS-Validator], [1.0], [b.long@cablelabs.com])
AC_CONFIG_SRCDIR([tslib/mpd.h])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.9 foreign subdir-objects])

# Checks for programs.
AC_PROG_CC
AC_PROG_RANLIB
LT_INIT

# Checks for libraries.
AC_CHECK_LIB([datastruct], [binheap_new])
AC_CHECK_LIB([h264bitstream], [h264_new])
AC_CHECK_LIB([isobmff], [validateIndexSegment])
AC_CHECK_LIB([logging], [skit_log_struct])
AC_CHECK_LIB([m], [cos])

PKG_CHECK_MODULES(GLIB, [glib-2.0])
PKG_CHECK_MODULES(GIO, [gio-2.0])
PKG_CHECK_MODULES(LIBXML2, [libxml-2.0])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h inttypes.h stddef.h stdint.h stdlib.h string.h strings.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([gettimeofday memmove memset strchr strdup strerror strrchr strtol])

AC_CONFIG_FILES([Makefile])
AC_CONFIG_SUBDIRS([h264bitstream])
AC_OUTPUT
