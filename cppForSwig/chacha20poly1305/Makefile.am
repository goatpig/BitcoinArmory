# Makefile for Jonas Schnelli's ChaCha20Poly1305 library
lib_LTLIBRARIES = libchacha20poly1305.la
noinst_PROGRAMS =
TESTS =
BENCH =

CHACHA20POLY1305_SOURCE_FILES = poly1305.c chacha.c chachapoly_aead.c

# ChaCha20Poly1305 library
libchacha20poly1305_la_SOURCES = $(CHACHA20POLY1305_SOURCE_FILES)
libchacha20poly1305_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
libchacha20poly1305_la_CFLAGS = $(AM_CFLAGS)
libchacha20poly1305_la_LIBADD = $(LIBCHACHAPOLY1305)
libchacha20poly1305_la_LDFLAGS = $(LDFLAGS) -static

if BUILD_TESTS
noinst_PROGRAMS += chacha20poly1305tests
TESTS += chacha20poly1305tests
chacha20poly1305tests_SOURCES = $(CHACHA20POLY1305_SOURCE_FILES) tests.c
chacha20poly1305tests_CFLAGS = $(AM_CFLAGS)
chacha20poly1305tests_CPPFLAGS = $(AM_CPPFLAGS)
chacha20poly1305tests_LDADD = $(LIBCHACHA20POLY1305)
chacha20poly1305tests_LDFLAGS = $(LDFLAGS) -static
endif

if BUILD_BENCH
noinst_PROGRAMS += chacha20poly1305bench
BENCH += chacha20poly1305bench
chacha20poly1305bench_SOURCES = $(CHACHA20POLY1305_SOURCE_FILES) bench.c
chacha20poly1305bench_CFLAGS = $(AM_CFLAGS)
chacha20poly1305bench_CPPFLAGS = $(AM_CPPFLAGS)
chacha20poly1305bench_LDADD = $(LIBCHACHA20POLY1305)
chacha20poly1305bench_LDFLAGS = $(LDFLAGS) -static
endif
