# Makefile for Bitcoin Armory - C/C++ subdirectory Makefile
bin_PROGRAMS =
noinst_PROGRAMS =
lib_LTLIBRARIES =
TESTS =
BENCH =

LIBBTC = libbtc/libbtc.la

DIST_SUBDIRS = cryptopp libbtc
SUBDIRS = cryptopp libbtc $(MAYBE_BUILD)

SWIG_FLAGS = -c++ -python -threads
AM_CXXFLAGS =

if HAVE_GCC
SWIG_FLAGS += -D__GNUC__
if HAVE_64BIT
SWIG_FLAGS += -D__LP64__
endif
endif

if HAVE_CLANG
SWIG_FLAGS += -D__CLANG__
endif

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
#	@test -f protoc
	protoc --cpp_out=protobuf --proto_path=protobuf $<

# NOTE: /usr/local/include reflects local installs of libwebsockets. This may
# be removed later.
INCLUDE_FILES = -Ibech32/ref/c++ -Ilmdb/libraries/liblmdb \
	-Ilibbtc/include -Ilibbtc/src/secp256k1/include \
	$(LWS_CFLAGS)

# Files should *not* be marked as "common" if at all possible. Also, a refactor
# might not hurt one of these days. Some of the "common" files could be GUI/CLI
# files but are "common" for the sake of unit testing. In an ideal world, the
# unit tests would be split into multiple files as needed. One day....
ARMORYDB_SOURCE_FILES = main.cpp
ARMORYGUI_SOURCE_FILES = TransactionBatch.cpp
ARMORYCLI_SOURCE_FILES = BDM_mainthread.cpp \
	BDM_Server.cpp \
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
	StoredBlockObj.cpp \
	StringSockets.cpp \
	txio.cpp \
	ZeroConf.cpp
ARMORYCOMMON_SOURCE_FILES = Accounts.cpp \
	Addresses.cpp \
	AssetEncryption.cpp \
	Assets.cpp \
	AsyncClient.cpp \
	BinaryData.cpp \
	BIP32_Node.cpp \
	BlockDataManagerConfig.cpp \
	BtcUtils.cpp \
	ClientClasses.cpp \
	CoinSelection.cpp \
	DecryptedDataContainer.cpp \
	DerivationScheme.cpp \
	EncryptionUtils.cpp \
	NetworkConfig.cpp \
	log.cpp \
	ReentrantLock.cpp \
	Script.cpp \
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

dist_noinst_DATA = $(PROTOBUF_PROTO)

# libArmoryCommon - Required by all Armory programs/libraries.
# Includes any files required by the common files.

lib_LTLIBRARIES += liblmdb.la
liblmdb_la_SOURCES = lmdb/libraries/liblmdb/lmdbpp.cpp \
		lmdb/libraries/liblmdb/mdb.c \
		lmdb/libraries/liblmdb/midl.c
liblmdb_la_CPPFLAGS = -Ilmdb/libraries/liblmdb -fPIC

# Common functionality across GUI and command line
lib_LTLIBRARIES += libArmoryCommon.la
libArmoryCommon_la_SOURCES = $(ARMORYCOMMON_SOURCE_FILES)
nodist_libArmoryCommon_la_SOURCES = $(PROTOBUF_CC) $(PROTOBUF_H)
libArmoryCommon_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
libArmoryCommon_la_CXXFLAGS = $(AM_CXXFLAGS) -D__STDC_LIMIT_MACROS
libArmoryCommon_la_LIBADD = liblmdb.la \
				cryptopp/libcryptopp.la \
				libbtc/libbtc.la libbtc/src/secp256k1/libsecp256k1.la \
				-lpthread -lprotobuf
libArmoryCommon_la_LDFLAGS = $(LDFLAGS) $(PROTOBUF_FLAGS)

# Command-line functionality library
lib_LTLIBRARIES += libArmoryCLI.la
libArmoryCLI_la_SOURCES = $(ARMORYCLI_SOURCE_FILES)
libArmoryCLI_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES) \
			$(PYTHON_CFLAGS)
libArmoryCLI_la_CXXFLAGS = $(AM_CXXFLAGS) -Ilmdb/libraries/liblmdb -D__STDC_LIMIT_MACROS
libArmoryCLI_la_LIBADD = $(LIBBTC) \
			 libArmoryCommon.la \
			 -lpthread
libArmoryCLI_la_LDFLAGS = $(LDFLAGS) $(LWS_LDFLAGS)

#ArmoryDB
bin_PROGRAMS += ArmoryDB
ArmoryDB_SOURCES = $(ARMORYDB_SOURCE_FILES)
ArmoryDB_CXXFLAGS = $(AM_CXXFLAGS) -D__STDC_LIMIT_MACROS
ArmoryDB_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
ArmoryDB_LDADD = libArmoryCommon.la \
		 libArmoryCLI.la \
		 -lpthread -lprotobuf
ArmoryDB_LDFLAGS = -static $(LWS_LDFLAGS) $(LDFLAGS)

if BUILD_CLIENT
# Common GUI functionality library
lib_LTLIBRARIES += libArmoryGUI.la
libArmoryGUI_la_SOURCES = $(ARMORYGUI_SOURCE_FILES)
libArmoryGUI_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES) \
			$(PYTHON_CFLAGS)
libArmoryGUI_la_CXXFLAGS = $(AM_CXXFLAGS) -D__STDC_LIMIT_MACROS
libArmoryGUI_la_LIBADD = libArmoryCommon.la cryptopp/libcryptopp.la \
			-lpthread
libArmoryGUI_la_LDFLAGS = $(LDFLAGS) $(PYTHON_LDFLAGS)

#libCppBlockUtils - SWIG library.
# "shared" LDFLAG due to SWIG requirements.
lib_LTLIBRARIES += libCppBlockUtils.la
libCppBlockUtils_la_SOURCES = CppBlockUtils_wrap.cxx
libCppBlockUtils_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES) \
				$(PYTHON_CFLAGS)
libCppBlockUtils_la_CXXFLAGS = $(AM_CXXFLAGS) -Ilmdb/libraries/liblmdb \
				-D__STDC_LIMIT_MACROS
libCppBlockUtils_la_LIBADD = libArmoryCommon.la libArmoryGUI.la \
				cryptopp/libcryptopp.la -lpthread
libCppBlockUtils_la_LDFLAGS = $(LDFLAGS) $(PYTHON_LDFLAGS) -shared

#custom rules
CppBlockUtils_wrap.cxx: CppBlockUtils.i
	swig $(SWIG_FLAGS) CppBlockUtils.i

.PHONY: CppBlockUtils_wrap.cxx $(PROTOBUF_H)
endif

MOSTLYCLEANFILES = $(PROTOBUF_CC) $(PROTOBUF_H)
BUILT_SOURCES = $(PROTOBUF_CC)

clean-local:
	rm -f CppBlockUtils.py
	rm -f CppBlockUtils_wrap.cxx
	rm -f CppBlockUtils_wrap.h

protobuf_clean:
	rm -f protobuf/*.pb.*

# .include file prevents gtest subdir from building a second copy of Armory.
# Also provides a convenient spot to place all test-related materials.
if BUILD_TESTS
include Makefile.tests.include
endif
