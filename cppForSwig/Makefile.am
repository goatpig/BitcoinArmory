# Makefile for Bitcoin Armory - C/C++ subdirectory Makefile
bin_PROGRAMS =
noinst_PROGRAMS =
lib_LTLIBRARIES =
noinst_LTLIBRARIES =
TESTS =
BUILT_SOURCES =

LIBBTC = libbtc/libbtc.la
LIBCHACHA20POLY1305 = chacha20poly1305/libchacha20poly1305.la
LIBARMORYCOMMON = libArmoryCommon.la
LIBARMORYCLIENT = libArmoryClient.la
LIBARMORYCLI = libArmoryCLI.la
LIBCRYPTOPP =
LIBLMDB = liblmdb.la
LIBSECP256K1 = libbtc/src/secp256k1/libsecp256k1.la
LIBWEBSOCKETS_STATIC = $(WEBSOCKETS_LIBDIR)/libwebsockets.a
LIBPROTOBUF_STATIC = $(PROTOBUF_LIBDIR)/libprotobuf.a
LIBPROTOBUF_SHARED = $(PROTOBUF_LIBDIR)/libprotobuf.so
if BUILD_OPENSSL_SUPPORT
LIBCRYPTO_STATIC = $(CRYPTO_LIBDIR)/libcrypto.a
LIBSSL_STATIC = $(SSL_LIBDIR)/libssl.a
endif
if BUILD_LIBUV_SUPPORT
LIBUV_STATIC = $(LIBUV_LIBDIR)/libuv.a
endif
if BUILD_LIBEVENT_SUPPORT
LIBEVENT_STATIC = $(LIBEVENT_LIBDIR)/libevent.a
endif

DIST_SUBDIRS = libbtc chacha20poly1305
SUBDIRS = libbtc chacha20poly1305 $(MAYBE_BUILD)

# Unit test flags.
UNIT_TEST_CXXFLAGS=
if BUILD_TESTS
UNIT_TEST_CXXFLAGS += -DUNIT_TESTS
endif

# Flag used to determine if the code will *only* use libbtc, and not Crypto++.
LIBBTC_FLAGS =

if USE_CRYPTOPP
DIST_SUBDIRS += cryptopp
SUBDIRS += cryptopp
LIBCRYPTOPP += cryptopp/libcryptopp.la
else
LIBBTC_FLAGS += -DLIBBTC_ONLY
endif

AM_CXXFLAGS =

# Revisit this line later. RPATH is needed for portability purposes. For now,
# install_name_tool works around portability issues on macOS but RPATH probably
# works better. Linux doesn't seem to need this for the most part.
# NOTE: '$$ORIGIN' is more linker special sauce insanity combined with quoting
# insanity combined with flag insanity ("-Wl,-rpath,stuff" vs. "-Wl,-rpath=stuff")
# combined with esoteric insanity (-Wl,-z,origin is here but might not be needed
# after all). Why is my TV suddenly talking to me, doc?
# Source 1: https://enchildfone.wordpress.com/2010/03/23/a-description-of-rpath-origin-ld_library_path-and-portable-linux-binaries/
# Source 2: https://longwei.github.io/rpath_origin/
# Source 3: https://stackoverflow.com/q/6324131
# Source 4: https://stackoverflow.com/a/33862145
#LDFLAGS += -Wl,-rpath,. -Wl,-rpath,'$$ORIGIN/../more/libs/here' -Wl,-z,origin

protobuf/%.pb.cc protobuf/%.pb.h: protobuf/%.proto
#	@test -f proto
	protoc --cpp_out=protobuf --proto_path=protobuf $<

INCLUDE_FILES = -Ibech32/ref/c++ -Ilmdb/libraries/liblmdb \
	-Ichacha20poly1305 -Ilibbtc/src/secp256k1/include \
	-Ilibbtc/include $(LWS_CFLAGS)

# Files should *not* be marked as "common" if at all possible. Also, a refactor
# might not hurt one of these days. Some of the "common" files could be GUI/CLI
# files but are "common" for the sake of unit testing. In an ideal world, the
# unit tests would be split into multiple files as needed. One day....
KEYMANAGER_SOURCE_FILES = KeyManager.cpp
ARMORYDB_SOURCE_FILES = main.cpp
ARMORYGUI_SOURCE_FILES = TransactionBatch.cpp
ARMORYCLI_SOURCE_FILES = BDM_mainthread.cpp \
	BDM_Server.cpp \
	BIP150_151.cpp \
	BitcoinP2P.cpp \
	Blockchain.cpp \
	BlockchainScanner.cpp \
	BlockchainScanner_Super.cpp \
	BlockDataMap.cpp \
	BlockDataViewer.cpp \
	BlockObj.cpp \
	BlockUtils.cpp \
	BtcWallet.cpp \
	DatabaseBuilder.cpp \
	DBUtils.cpp \
	HistoryPager.cpp \
	HttpMessage.cpp \
	JSON_codec.cpp \
	LedgerEntry.cpp \
	lmdb_wrapper.cpp \
	nodeRPC.cpp \
	Progress.cpp \
	ScrAddrFilter.cpp \
	ScrAddrObj.cpp \
	Server.cpp \
	SocketService_unix.cpp \
	SshParser.cpp \
	StringSockets.cpp \
	ZeroConf.cpp
ARMORYCOMMON_SOURCE_FILES = Accounts.cpp \
	Addresses.cpp \
	AssetEncryption.cpp \
	Assets.cpp \
	AsyncClient.cpp \
	AuthorizedPeers.cpp \
	BinaryData.cpp \
	BIP32_Node.cpp \
	BlockDataManagerConfig.cpp \
	BtcUtils.cpp \
	ClientClasses.cpp \
	CoinSelection.cpp \
	DecryptedDataContainer.cpp \
	DerivationScheme.cpp \
	hkdf.cpp \
	KDF.cpp \
	log.cpp \
	NetworkConfig.cpp \
	ReentrantLock.cpp \
	Script.cpp \
	SecureBinaryData.cpp \
	ScriptRecipient.cpp \
	Signer.cpp \
	SocketObject.cpp \
	StoredBlockObj.cpp \
	SwigClient.cpp \
	Transactions.cpp \
	TxClasses.cpp \
	TxEvalState.cpp \
	txio.cpp \
	UniversalTimer.cpp \
	WalletManager.cpp \
	Wallets.cpp \
	WebSocketClient.cpp \
	WebSocketMessage.cpp \
	bech32/ref/c++/bech32.cpp \
	bech32/ref/c++/segwit_addr.cpp

if ! USE_CRYPTOPP
ARMORYCOMMON_SOURCE_FILES += EncryptionUtils_libbtc.cpp
else
ARMORYCOMMON_SOURCE_FILES += EncryptionUtils.cpp
endif

PROTOBUF_PROTO = protobuf/AddressBook.proto \
	protobuf/AddressData.proto \
	protobuf/BDVCommand.proto \
	protobuf/CommonTypes.proto \
	protobuf/FeeEstimate.proto \
	protobuf/LedgerEntry.proto \
	protobuf/NodeStatus.proto \
	protobuf/Utxo.proto

PROTOBUF_CC = protobuf/AddressBook.pb.cc \
	protobuf/AddressData.pb.cc \
	protobuf/BDVCommand.pb.cc \
	protobuf/CommonTypes.pb.cc \
	protobuf/FeeEstimate.pb.cc \
	protobuf/LedgerEntry.pb.cc \
	protobuf/NodeStatus.pb.cc \
	protobuf/Utxo.pb.cc

PROTOBUF_H = protobuf/AddressBook.pb.h \
	protobuf/AddressData.pb.h \
	protobuf/BDVCommand.pb.h \
	protobuf/CommonTypes.pb.h \
	protobuf/FeeEstimate.pb.h \
	protobuf/LedgerEntry.pb.h \
	protobuf/NodeStatus.pb.h \
	protobuf/Utxo.pb.h

# Protobuf files required for distribution but not installation.
dist_noinst_DATA = $(PROTOBUF_PROTO)

# LMDB (DB used by Armory). On macOS, don't use SYSV semaphores (the default),
# which cause crashes due to mutex lock/unlock issues. Use POSIX semaphores
# (same as Linux).
noinst_LTLIBRARIES += $(LIBLMDB)
liblmdb_la_SOURCES = lmdb/libraries/liblmdb/lmdbpp.cpp \
		lmdb/libraries/liblmdb/mdb.c \
		lmdb/libraries/liblmdb/midl.c
liblmdb_la_CPPFLAGS = -Ilmdb/libraries/liblmdb -fPIC
if BUILD_DARWIN
liblmdb_la_CPPFLAGS += -DMDB_USE_POSIX_SEM=1
endif
liblmdb_la_LDFLAGS = -static

# libArmoryCommon - Required by all Armory programs/libraries.
# Common functionality across GUI and command line
noinst_LTLIBRARIES += $(LIBARMORYCOMMON)
libArmoryCommon_la_SOURCES = $(ARMORYCOMMON_SOURCE_FILES)
nodist_libArmoryCommon_la_SOURCES = $(PROTOBUF_CC) $(PROTOBUF_H)
libArmoryCommon_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
libArmoryCommon_la_CXXFLAGS = $(AM_CXXFLAGS) $(LIBBTC_FLAGS) \
			      $(UNIT_TEST_CXXFLAGS) -D__STDC_LIMIT_MACROS
libArmoryCommon_la_LIBADD = $(LIBCRYPTOPP) \
				$(LIBBTC) \
				$(LIBSECP256K1) \
				-lpthread
libArmoryCommon_la_LDFLAGS = $(LDFLAGS) $(LWSLDFLAGS) $(PROTOBUF_FLAGS)

# Command-line functionality library
noinst_LTLIBRARIES += $(LIBARMORYCLI)
libArmoryCLI_la_SOURCES = $(ARMORYCLI_SOURCE_FILES)
libArmoryCLI_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES) \
			$(PYTHON_CFLAGS)
libArmoryCLI_la_CXXFLAGS = $(AM_CXXFLAGS) $(LIBBTC_FLAGS) \
			   $(UNIT_TEST_CXXFLAGS) -Ilmdb/libraries/liblmdb -D__STDC_LIMIT_MACROS
libArmoryCLI_la_LIBADD = $(LIBCHACHA20POLY1305) \
			 $(LIBARMORYCOMMON) \
			 $(LIBCRYPTOPP) \
			 $(LIBLMDB) \
			 -lpthread
libArmoryCLI_la_LDFLAGS = $(LDFLAGS) $(LWSLDFLAGS)

# ArmoryDB binary
bin_PROGRAMS += ArmoryDB
ArmoryDB_SOURCES = $(ARMORYDB_SOURCE_FILES)
ArmoryDB_CXXFLAGS = $(AM_CXXFLAGS) $(LIBBTC_FLAGS) $(UNIT_TEST_CXXFLAGS) \
			-D__STDC_LIMIT_MACROS
ArmoryDB_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
ArmoryDB_LDADD = $(LIBARMORYCOMMON) \
		 $(LIBARMORYCLI) \
		 $(LIBCRYPTOPP) \
		 $(LIBBTC) \
		 $(LIBCHACHA20POLY1305) \
		 $(LIBLMDB) \
		 $(LIBPROTOBUF_STATIC) \
		 $(LIBWEBSOCKETS_STATIC)
if BUILD_OPENSSL_SUPPORT
ArmoryDB_LDADD += $(LIBSSL_STATIC) \
		 $(LIBCRYPTO_STATIC)
endif
if BUILD_LIBUV_SUPPORT
ArmoryDB_LDADD += $(LIBUV_STATIC)
endif
if BUILD_LIBEVENT_SUPPORT
ArmoryDB_LDADD += $(LIBEVENT_STATIC)
endif
if BUILD_LIBCAP_SUPPORT
ArmoryDB_LDADD += $(LIBCAP_LIBS)
endif
ArmoryDB_LDFLAGS = $(LDFLAGS) -static

# KeyManager binary
bin_PROGRAMS += BIP150KeyManager
BIP150KeyManager_SOURCES = $(KEYMANAGER_SOURCE_FILES)
BIP150KeyManager_CXXFLAGS = $(AM_CXXFLAGS) $(LIBBTC_FLAGS) $(UNIT_TEST_CXXFLAGS) \
			-D__STDC_LIMIT_MACROS
BIP150KeyManager_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
BIP150KeyManager_LDADD = $(LIBARMORYCOMMON) \
		 $(LIBARMORYCLI) \
		 $(LIBCRYPTOPP) \
		 $(LIBBTC) \
		 $(LIBCHACHA20POLY1305) \
		 $(LIBLMDB) \
		 $(LIBPROTOBUF_STATIC) \
		 -lpthread
BIP150KeyManager_LDFLAGS = $(LWSLDFLAGS) $(LDFLAGS) -static

if BUILD_CLIENT
# Client API shared lib
lib_LTLIBRARIES += $(LIBARMORYCLIENT)
libArmoryClient_la_SOURCES = $(ARMORYGUI_SOURCE_FILES)
libArmoryClient_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES) $(PYTHON_CFLAGS)
libArmoryClient_la_CXXFLAGS = $(AM_CXXFLAGS) $(UNIT_TEST_CXXFLAGS) -D__STDC_LIMIT_MACROS
libArmoryClient_la_LIBADD = $(LIBARMORYCLI) \
			$(LIBCRYPTOPP) $(LIBBTC) \
			$(LIBPROTOBUF_SHARED) $(LIBSECP256K1) \
			$(LIBWEBSOCKETS_STATIC) \
			-lpthread
libArmoryClient_la_LDFLAGS = $(LDFLAGS) $(LWSLDFLAGS) $(PYTHON_LDFLAGS) -shared
endif

MOSTLYCLEANFILES = $(PROTOBUF_CC) $(PROTOBUF_H)
BUILT_SOURCES += $(PROTOBUF_CC)

clean-local:
	rm -f BIP150KeyManager

protobuf_clean:
	rm -f protobuf/*.pb.*

# .include file prevents gtest subdir from building a second copy of Armory.
# Also provides a convenient spot to place all test-related materials.
if BUILD_TESTS
include Makefile.tests.include
endif
