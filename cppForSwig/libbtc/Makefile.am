ACLOCAL_AMFLAGS = -I build-aux/m4
.PHONY: gen
.INTERMEDIATE: $(GENBIN)

DIST_SUBDIRS = src/secp256k1

LIBSECP256K1=src/secp256k1/libsecp256k1.la

$(LIBSECP256K1): $(wildcard src/secp256k1/src/*) $(wildcard src/secp256k1/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

lib_LTLIBRARIES = libbtc.la
include_HEADERS = \
    include/btc/aes256_cbc.h \
    include/btc/base58.h \
    include/btc/bip32.h \
    include/btc/block.h \
    include/btc/btc.h \
    include/btc/buffer.h \
    include/btc/chainparams.h \
    include/btc/cstr.h \
    include/btc/ctaes.h \
    include/btc/ecc_key.h \
    include/btc/ecc.h \
    include/btc/hash.h \
    include/btc/portable_endian.h \
    include/btc/random.h \
    include/btc/ripemd160.h \
    include/btc/script.h \
    include/btc/segwit_addr.h \
    include/btc/serialize.h \
    include/btc/sha2.h \
    include/btc/tool.h \
    include/btc/tx.h \
    include/btc/utils.h \
    include/btc/vector.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libbtc.pc

libbtc_la_SOURCES = \
    src/aes256_cbc.c \
    src/base58.c \
    src/bip32.c \
    src/block.c \
    src/buffer.c \
    src/chainparams.c \
    src/commontools.c \
    src/cstr.c \
    src/ctaes.c \
    src/ecc_key.c \
    src/ecc_libsecp256k1.c \
    src/memory.c \
    src/memory.h \
    src/random.c \
    src/ripemd160.c \
    src/script.c \
    src/segwit_addr.c \
    src/serialize.c \
    src/sha2.c \
    src/tx.c \
    src/utils.c \
    src/vector.c

libbtc_la_CFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/src/logdb/include -I$(top_srcdir)/src/secp256k1/src
libbtc_la_LIBADD = $(LIBSECP256K1)

if USE_TESTS
noinst_PROGRAMS = tests
tests_LDADD = libbtc.la
tests_SOURCES = \
    test/aes_tests.c \
    test/base58check_tests.c \
    test/bip32_tests.c \
    test/block_tests.c \
    test/buffer_tests.c \
    test/cstr_tests.c \
    test/ecc_tests.c \
    test/eckey_tests.c \
    test/hash_tests.c \
    test/memory_tests.c \
    test/random_tests.c \
    test/serialize_tests.c \
    test/sha2_tests.c \
    test/utest.h \
    test/unittester.c \
    test/tx_tests.c \
    test/utils_tests.c \
    test/vector_tests.c

tests_CFLAGS = $(libbtc_la_CFLAGS)
tests_CPPFLAGS = -I$(top_srcdir)/src
tests_LDFLAGS = -static
TESTS = tests
endif

if WITH_WALLET

libbtc_la_SOURCES += \
    src/logdb/logdb_core.c \
    src/logdb/logdb_memdb_llist.c \
    src/logdb/logdb_memdb_rbtree.c \
    src/logdb/logdb_rec.c \
    src/logdb/red_black_tree.c

include_HEADERS += \
    src/logdb/include/logdb/logdb_base.h \
    src/logdb/include/logdb/logdb_core.h \
    src/logdb/include/logdb/logdb_memdb_llist.h \
    src/logdb/include/logdb/logdb_memdb_rbtree.h \
    src/logdb/include/logdb/logdb_rec.h \
    src/logdb/include/logdb/logdb.h \
    src/logdb/include/logdb/red_black_tree.h

include_HEADERS += \
    include/btc/wallet.h

libbtc_la_SOURCES += \
    src/wallet.c

if USE_TESTS
tests_SOURCES += \
    test/wallet_tests.c \
    src/logdb/test/logdb_tests.c \
    src/logdb/test/tests_red_black_tree.c
endif
endif

if WITH_NET
include_HEADERS += \
    include/btc/headersdb.h \
    include/btc/headersdb_file.h \
    include/btc/protocol.h \
    include/btc/net.h \
    include/btc/netspv.h

libbtc_la_SOURCES += \
    src/headersdb_file.c \
    src/net.c \
    src/netspv.c \
    src/protocol.c

libbtc_la_LIBADD += $(EVENT_PTHREADS_LIBS) $(EVENT_LIBS)
libbtc_la_CFLAGS += $(EVENT_CFLAGS) $(EVENT_PTHREADS_CFLAGS)

if USE_TESTS
tests_SOURCES += \
    test/net_tests.c \
    test/netspv_tests.c \
    test/protocol_tests.c
tests_LDADD += $(EVENT_PTHREADS_LIBS) $(EVENT_LIBS)
tests_LDFLAGS += -levent
endif
endif

if WITH_TOOLS
if USE_TESTS
tests_SOURCES += \
    test/tool_tests.c
endif
instdir=$(prefix)/bin
inst_PROGRAMS = bitcointool
bitcointool_LDADD = libbtc.la
bitcointool_SOURCES = \
    src/tools/bitcointool.c
bitcointool_CFLAGS = $(libbtc_la_CFLAGS)
bitcointool_CPPFLAGS = -I$(top_srcdir)/src
bitcointool_LDFLAGS = -static

if WITH_NET
inst_PROGRAMS += bitcoin-send-tx
bitcoin_send_tx_LDADD = libbtc.la $(EVENT_PTHREADS_LIBS) $(EVENT_LIBS)
bitcoin_send_tx_SOURCES = \
    src/tools/bitcoin-send-tx.c
bitcoin_send_tx_CFLAGS = $(libbtc_la_CFLAGS) $(EVENT_CFLAGS) $(EVENT_PTHREADS_CFLAGS)
bitcoin_send_tx_CPPFLAGS = -I$(top_srcdir)/src
bitcoin_send_tx_LDFLAGS = -static

inst_PROGRAMS += bitcoin-spv
bitcoin_spv_LDADD = libbtc.la
bitcoin_spv_SOURCES = \
    src/tools/bitcoin-spv.c
bitcoin_spv_CFLAGS = $(libbtc_la_CFLAGS)
bitcoin_spv_CPPFLAGS = -I$(top_srcdir)/src
bitcoin_spv_LDFLAGS = -static
endif

endif

clean-local:
	-$(MAKE) -C src/secp256k1 clean
