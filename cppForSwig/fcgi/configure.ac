#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([libfcgi],[v1.21],[toshic@github])
AC_CONFIG_SRCDIR([fcgi_config.h.in])
AM_INIT_AUTOMAKE([1.11 subdir-objects foreign -Wall -Werror])
AC_CONFIG_HEADERS([fcgi_config.h])

AM_PROG_AR
LT_INIT
AC_CANONICAL_HOST
AC_CONFIG_MACRO_DIRS([build-aux/m4])

#reset these flags if they are the default -g -02
AS_IF([test "$CFLAGS" = "-g -O2"] ,[AC_SUBST(CFLAGS, [ ])])
AS_IF([test "$CXXFLAGS" = "-g -O2"] ,[AC_SUBST(CXXFLAGS, [ ])])

#debug flag
AC_ARG_ENABLE([debug],
              AS_HELP_STRING([--enable-debug],
              [compile with debug symbols @<:@default=no@:>@]),
              [want_debug="$enableval"], [want_debug=no])

# Debug flags picked up from cppForSwig folder. No need to overwrite them.
if test "x$want_debug" = "xyes" -a $ac_cv_c_compiler_gnu != no; then
  AC_DEFINE([DEBUG], 1, [Define for debugging])
fi

# Checks for programs.
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AM_PROG_CC_C_O

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h memory.h netdb.h netinet/in.h stdlib.h string.h sys/param.h sys/socket.h sys/time.h sys/timeb.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_PID_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_FORK
#AC_FUNC_MALLOC
#AC_FUNC_REALLOC
AC_CHECK_FUNCS([atexit dup2 ftime gethostbyname inet_ntoa memchr memset putenv select socket strchr strerror strrchr strspn strstr strtol])

AC_SUBST(LIBFCGIXX)
AC_SUBST(ECHO_CPP)

AC_LANG([C])

AC_CHECK_LIB([nsl],       [gethostbyname])
AC_CHECK_LIB([socket],    [socket]) 

AX_PTHREAD([THREADED=threaded${EXEEXT}])
AC_SUBST([THREADED])

SYSTEM=unix
AC_SUBST([SYSTEM])

AC_REPLACE_FUNCS([strerror])

AC_C_INLINE

# Make the compilation flags quiet unless V=1 is used.
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_FILES([Makefile
                 examples/Makefile
                 include/Makefile
                 libfcgi/Makefile])

AC_OUTPUT

echo "  libfcgi"
echo "  CC              = $CC"
echo "  CFLAGS          = $CFLAGS"
echo "  CPP             = $CPP"
echo "  CPPFLAGS        = $CPPFLAGS"
echo "  CXX             = $CXX"
echo "  CXXFLAGS        = $CXXFLAGS"
echo "  LDFLAGS         = $LDFLAGS"
echo "  LD              = $LD"
echo "  debug symbols   = $want_debug"
