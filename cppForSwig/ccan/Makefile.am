# Makefile for Bitcoin Armory - C/C++ subdirectory Makefile
bin_PROGRAMS =
noinst_PROGRAMS =
lib_LTLIBRARIES =

LIBCCAN = libCCAN.la

INCLUDE_FILES = -I.

CCAN_SOURCE_FILES = ccan/crypto/hkdf_sha256/hkdf_sha256.c \
	ccan/crypto/hmac_sha256/hmac_sha256.c \
	ccan/crypto/sha256/sha256.c

# libCCAN - Contains all CCAN code.
lib_LTLIBRARIES += $(LIBCCAN)
libCCAN_la_SOURCES = $(CCAN_SOURCE_FILES)
libCCAN_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
libCCAN_la_CFLAGS = $(AM_CCFLAGS)
libCCAN_la_LIBADD = -lpthread
libCCAN_la_LDFLAGS = $(LDFLAGS) -shared

# While not strictly necessary, it may be necessary at some point to use install
# names (macOS equivlent of rpath) longer than the one used by default
# (/usr/local/lib). Let's plan for it. Don't use "-install_name" flag, as that
# only affects CppBlockUtils and not whatever it relies upon. Can fix that
# elsewhere with the "install_name_tool" binary.
if BUILD_DARWIN
libCCAN_la_LDFLAGS += -headerpad_max_install_names
endif

if BUILD_BENCH
BENCH =
include Makefile.bench.include
endif

if BUILD_TESTS
TESTS =
include Makefile.tests.include
endif
