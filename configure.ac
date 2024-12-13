AC_PREREQ([2.69])
AC_INIT([BitcoinArmory],[0.96.99],[moothecowlord@gmail.com])

AM_INIT_AUTOMAKE([1.11 subdir-objects foreign -Wall -Werror])

AM_PROG_AR

LT_INIT()

AC_CANONICAL_HOST

# Reset these flags if they are the system default "-g -02".
AS_IF([test "$CFLAGS" = "-g -O2"] ,[AC_SUBST(CFLAGS, [ ])])
AS_IF([test "$CXXFLAGS" = "-g -O2"] ,[AC_SUBST(CXXFLAGS, [ ])])

#build tests arg
AC_ARG_ENABLE([tests],
              AS_HELP_STRING([--enable-tests],
              [build with test suites @<:@default=no@:>@]),
              [want_tests="$enableval"], [want_tests=no])

#build benchmarks
AC_ARG_ENABLE([benchmark],
              AS_HELP_STRING([--enable-benchmark],
              [build with benchmark suites @<:@default=no@:>@]),
              [want_benchmark=$enableval], [want_benchmark=no])

#build debug arg
AC_ARG_ENABLE([debug],
              AS_HELP_STRING([--enable-debug],
              [compile with debug symbols @<:@default=no@:>@]),
              [want_debug="$enableval"], [want_debug=no])

#build client arg
AC_ARG_WITH([client],
              AS_HELP_STRING([--without-client],
              [do not build client @<:@default=no@:>@]),
              [with_client="$withval"], [with_client=yes])

# Custom libwebsockets path. This will be a mandatory flag, even if the user
# uses /usr/local. The problem is that Ubuntu has a pretty old version of LWS.
# We should just use a newer version and link it in.
AC_ARG_WITH([own-lws],
    AS_HELP_STRING([--with-own-lws],
    [pass path of your own lws binary and headers @<:@default=no@:>@]),
    [with_own_lws="$withval"], [with_own_lws=no])

# Custom libbtc path
AC_ARG_WITH([own-libbtc],
    AS_HELP_STRING([--with-own-libbtc],
    [pass path of your own libbtc binary and headers @<:@default=no@:>@]),
    [with_own_libbtc="$withval"], [with_own_libbtc=no])

if test "x$want_debug" = "xyes" -a $ac_cv_c_compiler_gnu != no; then
  CFLAGS="$CFLAGS -O0 -g3"
  CXXFLAGS="$CXXFLAGS -O0 -g3"
  AC_DEFINE([DEBUG], 1, [Define for debugging])
else
  CFLAGS="$CFLAGS -O2"
  CXXFLAGS="$CXXFLAGS -O2"
fi


AS_IF([test "x$with_client" = "xyes"], [build_client=yes],
  [build_client=no])
AM_CONDITIONAL([BUILD_CLIENT], [test x$build_client = xyes])

# Building the GUI requires the client.
AS_IF([test "x$with_client$with_gui" = "xyesyes"], [build_gui=yes],
  [build_gui=no])
AM_CONDITIONAL([BUILD_GUI], [test x$build_gui = xyes])

AC_PROG_CC
AC_PROG_CXX
AM_CONDITIONAL([HAVE_GCC], [test $CXX = g++])
AC_CHECK_SIZEOF([long unsigned int])
AM_CONDITIONAL([HAVE_64BIT], [test $ac_cv_sizeof_long_unsigned_int = 8])

AC_CONFIG_MACRO_DIR([build-aux/m4])
m4_include([build-aux/m4/ax_check_compile_flag.m4])
m4_include([build-aux/m4/ax_cxx_compile_stdcxx.m4])

#check for c++17
AX_CXX_COMPILE_STDCXX_17([noext], [mandatory])

# Make the compilation flags quiet unless V=1 is used.
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_SUBST([CXX_STANDARD], $CXX_STANDARD)

# Tinkering with LDFLAGS hopefully won't be needed, but just in case....
#LDFLAGS="${LDFLAGS} -static"

dnl Determine build OS and do other OS-specific things.
case $host in
  *darwin*)
    BUILD_OS=darwin

    # Handle Mac OS X SDK flags.
    AC_ARG_WITH(macosx-sdk,
      [AS_HELP_STRING([--with-macosx-sdk=DIR],
        [compile using the SDK in DIR])])
    if test "${with_macosx_sdk}" != "" ; then
        test ! -d "${with_macosx_sdk}" && AC_MSG_ERROR([SDK "${with_macosx_sdk}" not found])
        CFLAGS="${CFLAGS} -isysroot ${with_macosx_sdk}"
        CXXFLAGS="${CXXFLAGS} -isysroot ${with_macosx_sdk}"
        OBJC="${OBJC} -isysroot ${with_macosx_sdk}"
    fi
    AC_ARG_WITH(macosx-version-min,
      [AS_HELP_STRING([--with-macosx-version-min=VERSION],
        [compile for Mac OS X VERSION and above])])
    if test "${with_macosx_version_min}" != "" ; then
        CFLAGS="${CFLAGS} -mmacosx-version-min=${with_macosx_version_min}"
        CXXFLAGS="${CXXFLAGS} -mmacosx-version-min=${with_macosx_version_min}"
        OBJC="${OBJC} -mmacosx-version-min=${with_macosx_version_min}"
    fi
    AC_CHECK_PROG([BREW],brew, brew)
    if test x$BREW = xbrew; then
        if test x$with_own_lws = xno; then
            use_pkgconfig=yes
            libwebsockets_prefix=`$BREW --prefix libwebsockets 2>/dev/null`
            if test x$libwebsockets_prefix != x; then
                PKG_CONFIG_PATH="$libwebsockets_prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
                export PKG_CONFIG_PATH
            fi
        fi
        openssl_prefix=`$BREW --prefix openssl 2>/dev/null`
        if test x$openssl_prefix != x; then
            PKG_CONFIG_PATH="$openssl_prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
            export PKG_CONFIG_PATH
        fi
        libuv_prefix=`$BREW --prefix libuv 2>/dev/null`
        if test x$openssl_prefix != x; then
            PKG_CONFIG_PATH="$libuv_prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
            export PKG_CONFIG_PATH
        fi
        libevent_prefix=`$BREW --prefix libevent 2>/dev/null`
        if test x$libevent_prefix != x; then
            PKG_CONFIG_PATH="$libevent_prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
            export PKG_CONFIG_PATH
        fi
    fi

    # Just FYI, here are some linker flags, used and unused, that are noteworthy.
    # In the future, making sure install_name_tool usage is minimized would be
    # ideal. (Some usage may be necessary. See https://lists.gnu.org/archive/html/libtool-patches/2010-09/msg00308.html
    # for an example.)
    # -undefined dynamic_lookup: https://stackoverflow.com/a/30934307
    # -rpath @executable_path/ (or other paths to search): https://wincent.com/wiki/@executable_path,_@load_path_and_@rpath
    # -install_name @loader_path/libArmoryCommon.0.dylib: http://osiris.laya.com/coding/dylib_linking.html
    CXXFLAGS="${CXXFLAGS} -stdlib=libc++ -fPIC -pipe"
    LDFLAGS="${LDFLAGS} -stdlib=libc++ -lc++"

    # There really ought to be some sort of Qt check here. M4 files don't work
    # well with Armory. Revisit this later. Not required, just nice to have.
    ;;
  #*linux*)
  #  BUILD_OS=linux

  #  use_pkgconfig=yes

  #  if test "x$build_gui" = "xyes"; then
  #     # Check existence of Qt packages.
  #     if ! pkg-config --exists 'QtCore >= 4.8 QtCore <= 5 QtGui >= 4.8 QtGui <= 5'; then
  #         AC_MSG_ERROR([missing QtCore library, make sure libqtcore4 and libqt4-dev are installed])
  #     fi
  #  fi
  #  ;;
esac

#if test x$use_pkgconfig = xyes; then
#    m4_ifndef([PKG_PROG_PKG_CONFIG], [AC_MSG_ERROR(PKG_PROG_PKG_CONFIG macro not found. Please install pkg-config and re-run autogen.sh.)])
#    m4_ifdef([PKG_PROG_PKG_CONFIG], [
#    PKG_PROG_PKG_CONFIG
#    if test x"$PKG_CONFIG" = "x"; then
#        AC_MSG_ERROR(pkg-config not found.)
#    fi
#    ])
#fi

dnl Check for capnproto compiler
AC_PATH_PROG([CAPNPC], [capnpc], [])
AS_IF([test x"$CAPNPC" = x],
  [AC_MSG_ERROR([cannot find capnpc (the capnproto compiler)])])

if test x$use_pkgconfig$with_own_lws = xyesno; then
    PKG_CHECK_MODULES_STATIC([WEBSOCKETS], [libwebsockets_static >= 3.0.1],,[AC_MSG_ERROR(libwebsockets_static >= 3.0.1 not found.)])
    AC_MSG_CHECKING([libwebsockets lib path])
    PKG_CHECK_VAR([WEBSOCKETS_LIBDIR], [libwebsockets_static], [libdir])
    AS_IF([test "x$WEBSOCKETS_LIBDIR" = "x"], [
      AC_MSG_FAILURE([Unable to identify libwebsockets_static lib path.])
    ])
    AC_MSG_RESULT($WEBSOCKETS_LIBDIR)
    AC_SUBST([WEBSOCKETS_LIBDIR])
    CXXFLAGS="${CXXFLAGS} ${WEBSOCKETS_CFLAGS}"
else
    # Even if libraries don't directly use LWS, they may rely on libraries that do
    # rely on LWS. This causes the "child" libraries to rely on LWS too! Be sure to
    # use the rpath equivalent in LDFLAGS for all affected libraries, "children" and
    # "parent".
    AC_SUBST([WEBSOCKETS_LIBDIR], ["$with_own_lws/lib"])
    CXXFLAGS="${CXXFLAGS} -I$with_own_lws/include"
fi

if test x$with_own_libbtc = xno; then
    AC_MSG_FAILURE([No path to libbtc, check README section on libbtc])
else
    AC_SUBST([LIBBTC_LIBDIR], ["$with_own_libbtc"])
    CXXFLAGS="${CXXFLAGS} -I$with_own_libbtc/include"
fi

AC_MSG_CHECKING(for LWS_WITH_TLS)
AC_LANG_PUSH([C++])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#include "lws_config.h"
#ifndef LWS_WITH_TLS
# error macro not defined
#endif
]])],
  [ LWS_WITH_TLS_FOUND="yes" ],
  [ LWS_WITH_TLS_FOUND="no" ]
)
AC_LANG_POP([C++])
AC_MSG_RESULT($LWS_WITH_TLS_FOUND)

if test x$LWS_WITH_TLS_FOUND = xyes; then
    PKG_CHECK_MODULES_STATIC([SSL], [libssl],, [AC_MSG_ERROR(libssl not found.)])
    AC_MSG_CHECKING([libssl lib path])
    PKG_CHECK_VAR([SSL_LIBDIR], [libssl], [libdir])
    AS_IF([test "x$SSL_LIBDIR" = "x"], [
      AC_MSG_FAILURE([Unable to identify libssl lib path.])
    ])
    AC_MSG_RESULT($SSL_LIBDIR)
    AC_SUBST([SSL_LIBDIR])
    CXXFLAGS="${CXXFLAGS} ${SSL_CFLAGS}"

    PKG_CHECK_MODULES_STATIC([CRYPTO], [libcrypto],, [AC_MSG_ERROR(libcrypto not found.)])
    AC_MSG_CHECKING([libcrypto lib path])
    PKG_CHECK_VAR([CRYPTO_LIBDIR], [libcrypto], [libdir])
    AS_IF([test "x$CRYPTO_LIBDIR" = "x"], [
      AC_MSG_FAILURE([Unable to identify libcrypto lib path.])
    ])
    AC_MSG_RESULT($CRYPTO_LIBDIR)
    AC_SUBST([CRYPTO_LIBDIR])

    AC_MSG_CHECKING([ldl required])
    PKG_CHECK_VAR([SSL_LIBS], [libssl], [libs])
    AS_IF(echo "$SSL_LIBS" | grep '\-ldl' >/dev/null 2>&1, SSL_LDL_REQUIRED="yes", SSL_LDL_REQUIRED="no")
    PKG_CHECK_VAR([CRYPTO_LIBS], [libcrypto], [libs])
    AS_IF(echo "$CRYPTO_LIBS" | grep '\-ldl' >/dev/null 2>&1, CRYPTO_LDL_REQUIRED="yes", CRYPTO_LDL_REQUIRED="no")
    LDL_REQUIRED="no"
    AS_IF([test "x$SSL_LDL_REQUIRED$CRYPTO_LDL_REQUIRED" != "xnono"], [
      LDL_REQUIRED="yes"
    ])
    AC_MSG_RESULT($LDL_REQUIRED)
fi

AC_MSG_CHECKING(for LWS_WITH_LIBUV)
AC_LANG_PUSH([C++])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#include "lws_config.h"
#ifndef LWS_WITH_LIBUV
# error macro not defined
#endif
]])],
  [ LWS_WITH_LIBUV_FOUND="yes" ],
  [ LWS_WITH_LIBUV_FOUND="no" ]
)
AC_LANG_POP([C++])
AC_MSG_RESULT($LWS_WITH_LIBUV_FOUND)

if test x$LWS_WITH_LIBUV_FOUND = xyes; then
    PKG_CHECK_MODULES_STATIC([LIBUV], [libuv],, [AC_MSG_ERROR(libuv not found.)])
    AC_MSG_CHECKING([libuv lib path])
    PKG_CHECK_VAR([LIBUV_LIBDIR], [libuv], [libdir])
    AS_IF([test "x$LIBUV_LIBDIR" = "x"], [
      AC_MSG_FAILURE([Unable to identify libuv lib path.])
    ])
    AC_MSG_RESULT($LIBUV_LIBDIR)
    AC_SUBST([LIBUV_LIBDIR])
    CXXFLAGS="${CXXFLAGS} ${LIBUV_CFLAGS}"
fi

AC_MSG_CHECKING(for LWS_WITH_LIBEVENT)
AC_LANG_PUSH([C++])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#include "lws_config.h"
#ifndef LWS_WITH_LIBEVENT
# error macro not defined
#endif
]])],
  [ LWS_WITH_LIBEVENT_FOUND="yes" ],
  [ LWS_WITH_LIBEVENT_FOUND="no" ]
)
AC_LANG_POP([C++])
AC_MSG_RESULT($LWS_WITH_LIBEVENT_FOUND)

if test x$LWS_WITH_LIBEVENT_FOUND = xyes; then
    PKG_CHECK_MODULES_STATIC([LIBEVENT], [libevent],, [AC_MSG_ERROR(libevent not found.)])
    AC_MSG_CHECKING([libevent lib path])
    PKG_CHECK_VAR([LIBEVENT_LIBDIR], [libevent], [libdir])
    AS_IF([test "x$LIBEVENT_LIBDIR" = "x"], [
      AC_MSG_FAILURE([Unable to identify libevent lib path.])
    ])
    AC_MSG_RESULT($LIBEVENT_LIBDIR)
    AC_SUBST([LIBEVENT_LIBDIR])
    CXXFLAGS="${CXXFLAGS} ${LIBEVENT_CFLAGS}"
fi

AC_MSG_CHECKING(for LWS_HAVE_LIBCAP)
AC_LANG_PUSH([C++])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#include "lws_config.h"
#ifndef LWS_HAVE_LIBCAP
# error macro not defined
#endif
]])],
  [ LWS_HAVE_LIBCAP_FOUND="yes" ],
  [ LWS_HAVE_LIBCAP_FOUND="no" ]
)
AC_LANG_POP([C++])
AC_MSG_RESULT($LWS_HAVE_LIBCAP_FOUND)

if test x$LWS_HAVE_LIBCAP_FOUND = xyes; then
    AC_SUBST([LIBCAP_LIBS], "-lcap")
fi

AM_CONDITIONAL([BUILD_OPENSSL_SUPPORT], [test x$LWS_WITH_TLS_FOUND = xyes])
AM_CONDITIONAL([BUILD_LIBUV_SUPPORT], [test x$LWS_WITH_LIBUV_FOUND = xyes])
AM_CONDITIONAL([BUILD_LIBEVENT_SUPPORT], [test x$LWS_WITH_LIBEVENT_FOUND = xyes])
AM_CONDITIONAL([BUILD_LIBCAP_SUPPORT], [test x$LWS_HAVE_LIBCAP_FOUND = xyes])
AM_CONDITIONAL([BUILD_LDL_REQUIRED], [test x$LDL_REQUIRED = xyes])

dnl Clang detection is complicated since it comes up as GCC too.
AC_LANG_PUSH([C++])
AC_COMPILE_IFELSE(
[AC_LANG_PROGRAM([], [[
#ifndef __clang__
       not clang
#endif
]])],
[CLANG=yes], [CLANG=no])
AC_LANG_POP([C++])

AC_MSG_RESULT([is compiler clang: $CLANG])
AC_SUBST([CLANG])

# Declare CFLAGS "unprecious" so that it can be passed directly to subdirs.
AC_DEFUN([AX_UNPRECIOUS], [
    m4_define([_AC_PRECIOUS_VARS], m4_bpatsubst(_AC_PRECIOUS_VARS, [$1
], []))
])
AX_UNPRECIOUS([CFLAGS])

export BUILD_OS
export CFLAGS
export CXXFLAGS
export LDFLAGS

AM_CONDITIONAL([HAVE_CLANG], [test x$CLANG = xyes])
AM_CONDITIONAL([BUILD_DARWIN], [test x$BUILD_OS = xdarwin])

AC_CONFIG_FILES(Makefile
    cppForSwig/Makefile
    cppForSwig/chacha20poly1305/Makefile
    cppForSwig/hkdf/Makefile)

AM_CONDITIONAL([BUILD_BENCH], [test x$want_benchmark = xyes])
AM_CONDITIONAL([BUILD_TESTS], [test "x$want_tests" = "xyes"])

#symlink test files over to build folder
if test x$want_tests = xyes; then
    AC_CONFIG_LINKS(cppForSwig/reorgTest/blk_0.dat:cppForSwig/reorgTest/blk_0.dat)
    AC_CONFIG_LINKS(cppForSwig/reorgTest/blk_1.dat:cppForSwig/reorgTest/blk_1.dat)
    AC_CONFIG_LINKS(cppForSwig/reorgTest/blk_2.dat:cppForSwig/reorgTest/blk_2.dat)
    AC_CONFIG_LINKS(cppForSwig/reorgTest/blk_3.dat:cppForSwig/reorgTest/blk_3.dat)
    AC_CONFIG_LINKS(cppForSwig/reorgTest/blk_4.dat:cppForSwig/reorgTest/blk_4.dat)
    AC_CONFIG_LINKS(cppForSwig/reorgTest/blk_4A.dat:cppForSwig/reorgTest/blk_4A.dat)
    AC_CONFIG_LINKS(cppForSwig/reorgTest/blk_5.dat:cppForSwig/reorgTest/blk_5.dat)
    AC_CONFIG_LINKS(cppForSwig/reorgTest/blk_5A.dat:cppForSwig/reorgTest/blk_5A.dat)

    AC_CONFIG_LINKS(cppForSwig/reorgTest/botched_block.dat:cppForSwig/reorgTest/botched_block.dat)
    AC_CONFIG_LINKS(cppForSwig/reorgTest/LBZC.tx:cppForSwig/reorgTest/LBZC.tx)
    AC_CONFIG_LINKS(cppForSwig/reorgTest/ZCtx.tx:cppForSwig/reorgTest/ZCtx.tx)
fi

AC_OUTPUT

echo "  Armory Autotools flags"
echo "  CC                = $CC"
echo "  CFLAGS            = $CFLAGS"
echo "  CPP               = $CPP"
echo "  CPPFLAGS          = $CPPFLAGS"
echo "  CXX               = $CXX"
echo "  CXXFLAGS          = $CXXFLAGS"
echo "  LDFLAGS           = $LDFLAGS"
echo "  LD                = $LD"
echo "  LIBS              = $LIBS"
echo "  ac_configure_args = $ac_configure_args"
echo "  with tests        = $want_tests"
echo "  with benchmarks   = $want_benchmark"
echo "  debug symbols     = $want_debug"
echo "  with client       = $build_client"
echo "  custom lws path   = $with_own_lws"
echo "  custom libbtc     = $with_own_libbtc"
