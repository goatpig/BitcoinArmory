AC_PREREQ([2.69])
AC_INIT([BitcoinArmory],[0.96.4],[moothecowlord@gmail.com])

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

#build gui arg
AC_ARG_WITH([gui],
              AS_HELP_STRING([--without-gui],
              [do not build GUI @<:@default=no@:>@]),
              [with_gui="$withval"], [with_gui=yes])

# Custom libwebsockets path. This will be a mandatory flag, even if the user
# uses /usr/local. The problem is that Ubuntu has a pretty old version of LWS.
# We should just use a newer version and link it in.
AC_ARG_WITH([own-lws],
		AS_HELP_STRING([--with-own-lws], 
		[pass path of your own lws binary and headers @<:@default=no@:>@]),
		[with_own_lws="$withval"], [with_own_lws=no])

# Crypto++ usage. Armory defaults to using libbtc for most crypto. However, a
# legacy transition period where Crypto++ can be used instead is in effect.
AC_ARG_WITH([cryptopp],
		AS_HELP_STRING([--with-cryptopp],
		[use Crypto++ instead of libbtc for crypto functions @<:@default=no@:>@]),
		[with_cryptopp="$withval"], [with_cryptopp=no])

# Force user to define which LWS they wish to use.
AS_IF([test $with_own_lws != no], [],
	[AC_MSG_ERROR([A custom version of libwebsockets must be defined. /usr/local is an acceptable location.])]) 

# Even if libraries don't directly use LWS, they may rely on libraries that do
# rely on LWS. This causes the "child" libraries to rely on LWS too! Be sure to
# use the rpath equivalent in LDFLAGS for all affected libraries, "children" and
# "parent".
AC_SUBST([LWS_CFLAGS], ["-I$with_own_lws/include"])
AC_SUBST([LWSLDFLAGS], ["-L$with_own_lws/lib -R $with_own_lws/lib"])
AC_SUBST([LWS_LIB_LOC], ["$with_own_lws/lib"])
LIBS="-lwebsockets $LIBS"

# Check for protobuf (protoc in particular is required)
PKG_CHECK_MODULES(PROTOBUF, protobuf >= 2.0.0)

if test "x$want_debug" = "xyes" -a $ac_cv_c_compiler_gnu != no; then
  CFLAGS="$CFLAGS -O0 -g3"
  CXXFLAGS="$CXXFLAGS -O0 -g3"
  AC_DEFINE([DEBUG], 1, [Define for debugging])
else
  CFLAGS="$CFLAGS -O2"
  CXXFLAGS="$CXXFLAGS -O2"
fi

# For now, the GUI is considered synonymous with the client. This may change.
AS_IF([test "x$with_gui" = "xyes"], [build_client=yes],
	[build_client=no])
AM_CONDITIONAL([BUILD_CLIENT], [test x$build_client = xyes])

AC_PROG_CC
AC_PROG_CXX
AM_CONDITIONAL([HAVE_GCC], [test $CXX = g++])
AC_CHECK_SIZEOF([long unsigned int])
AM_CONDITIONAL([HAVE_64BIT], [test $ac_cv_sizeof_long_unsigned_int = 8])

AC_CONFIG_MACRO_DIR([build-aux/m4])
m4_include([build-aux/m4/ax_check_compile_flag.m4])
m4_include([build-aux/m4/ax_swig_python.m4])
m4_include([build-aux/m4/ax_pkg_swig.m4])
m4_include([build-aux/m4/ax_python_devel.m4])
m4_include([build-aux/m4/ax_python_module.m4])
m4_include([build-aux/m4/ax_cxx_compile_stdcxx.m4])

#check for c++11
AX_CXX_COMPILE_STDCXX([11], [noext], [mandatory], [nodefault])

# Make the compilation flags quiet unless V=1 is used.
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

if test "x$build_client" = "xyes"; then
#check for  swig

AC_CHECK_PROG([HAVE_SWIG], [swig], [yes], [no])
AS_IF([test $HAVE_SWIG == yes], [],
	[AC_MSG_ERROR([missing swig!])]) 

if test "x$cross_compiling" != "xyes"; then
	AX_SWIG_PYTHON([no])
	AS_IF([test -z $AX_SWIG_PYTHON_CPPFLAGS], 
		[AC_MSG_ERROR([failed to procure swig include path, make sure swig is installed])])
	AC_SUBST([PYTHON_CFLAGS], [`$PYTHON-config --cflags`])
	AC_SUBST([PYTHON_LDFLAGS], [`$PYTHON-config --ldflags`])
	AX_PYTHON_MODULE([psutil], [fatal])
else
AC_SUBST([AX_SWIG_PYTHON_CPPFLAGS], [`$PYTHON-config --includes`])
AC_SUBST([PYTHON_CFLAGS], [`$PYTHON-config --cflags`])
AC_SUBST([PYTHON_LDFLAGS], [`$PYTHON-config --ldflags`])
fi

AC_MSG_RESULT([AX_SWIG_PYTHON_CPPFLAGS: $AX_SWIG_PYTHON_CPPFLAGS])
AC_MSG_RESULT([PYTHON_CFLAGS: $PYTHON_LDFLAGS])
AC_MSG_RESULT([PYTHON_LDFLAGS: $PYTHON_LDFLAGS])

if test "x$with_gui" = "xyes"; then
#check for  gui

AC_CHECK_PROG([HAVE_PYRCC4], [pyrcc4], [yes], [no])
AS_IF([test $HAVE_PYRCC4 == yes], [],
	[AC_MSG_ERROR([missing pyrcc4 in path, make sure pyqt4-dev-tools is installed])]) 

AC_CHECK_PROG([HAVE_LRELEASE], [lrelease], [yes], [no])
AS_IF([test $HAVE_LRELEASE == yes], [],
	[AC_MSG_ERROR([missing lrelease in path, make sure qt4-linguist-tools is installed])]) 

fi
fi

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

    # Just FYI, here are some linker flags, used and unused, that are noteworthy.
    # In the future, making sure install_name_tool usage is minimized would be
    # ideal. (Some usage may be necessary. See https://lists.gnu.org/archive/html/libtool-patches/2010-09/msg00308.html
    # for an example.)
    # -undefined dynamic_lookup: https://stackoverflow.com/a/30934307
    # -headerpad_max_install_names: https://software.intel.com/en-us/forums/intel-threading-building-blocks/topic/509396
    # -rpath @executable_path/ (or other paths to search): https://wincent.com/wiki/@executable_path,_@load_path_and_@rpath
    # -install_name @loader_path/libArmoryCommon.0.dylib: http://osiris.laya.com/coding/dylib_linking.html
    CXXFLAGS="${CXXFLAGS} -stdlib=libc++ -fPIC -pipe"
    LDFLAGS="${LDFLAGS} -stdlib=libc++ -lc++ -headerpad_max_install_names"

    # There really ought to be some sort of Qt check here. M4 files don't work
    # well with Armory. Revisit this later. Not required, just nice to have.
    ;;
  *linux*)
    BUILD_OS=linux

    if test "x$build_client$with_gui" = "xyesyes"; then
       # Check existence of Qt packages.
       if ! pkg-config --exists 'QtCore >= 4.8 QtCore <= 5 QtGui >= 4.8 QtGui <= 5'; then
       	   AC_MSG_ERROR([missing QtCore library, make sure libqtcore4 and libqt4-dev are installed])
       fi
    fi
    ;;
esac

dnl Check for protoc (Google Protocol Buffers).
AC_PATH_PROG([PROTOC], [protoc], [])
AS_IF([test x"$PROTOC" = x],
	[AC_MSG_ERROR([cannot find protoc (the Protocol Buffers compiler)])])

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
AM_CONDITIONAL([BUILD_LINUX], [test x$BUILD_OS = xlinux])

AC_CONFIG_FILES(Makefile
		cppForSwig/Makefile
		cppForSwig/chacha20poly1305/Makefile)

AM_CONDITIONAL([BUILD_BENCH], [test x$want_benchmark = xyes])
AM_CONDITIONAL([BUILD_TESTS], [test "x$want_tests" = "xyes"])
AM_CONDITIONAL([USE_CRYPTOPP], [test x$with_cryptopp = xyes])

dnl ac_configure_args can only be set once and affects all subdirs.
dnl Place all params here.
dnl Crypto++ - No customs
dnl libchacha20poly1305 - No customs
dnl libbtc - --disable-wallet --disable-net --disable-tools
dnl libsecp256k1 (libbtc) - --disable-shared --with-pic --with-bignum=no
dnl NB: For libbtc, wallet & net are basically intertwined. If disabling one,
dnl     both must be disabled, else weird compiler/linker errors appear.
ac_configure_args="${orig_config_args} --disable-wallet --disable-net --disable-tools --disable-shared --with-pic --with-bignum=no"
AC_CONFIG_SUBDIRS([cppForSwig/libbtc])
AM_COND_IF([USE_CRYPTOPP], [AC_CONFIG_SUBDIRS([cppForSwig/cryptopp])], [])

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
echo "  with GUI/client   = $build_client"
echo "  custom lws path   = $with_own_lws"
echo "  use Crypto++      = $with_cryptopp"
