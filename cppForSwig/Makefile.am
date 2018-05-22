# Makefile for Bitcoin Armory - C/C++ subdirectory Makefile
bin_PROGRAMS =
noinst_PROGRAMS =
lib_LTLIBRARIES =
TESTS =

LIBBTC = libbtc/libbtc.la
LIBCHACHA20POLY1305 = chacha20poly1305/.libs/libchacha20poly1305.la
LIBCPPBLOCKUTILS = libCppBlockUtils.la
LIBARMORYCOMMON = libArmoryCommon.la
LIBARMORYGUI = libArmoryGUI.la
LIBARMORYCLI = libArmoryCLI.la
LIBCRYPTOPP = cryptopp/libcryptopp.la
LIBFCGI = fcgi/libfcgi/.libs/libfcgi.la
LIBLMDB = lmdb/.libs/liblmdb.la
LIBSECP256K1 = libbtc/src/secp256k1/libsecp256k1.la

DIST_SUBDIRS = lmdb fcgi cryptopp libwebsockets/build libbtc chacha20poly1305
SUBDIRS = lmdb fcgi cryptopp libwebsockets/build libbtc chacha20poly1305 $(MAYBE_BUILD)

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
# works better. Linux doesn't seem to need this for the most part. libwebsockets
# integration is an exception. I assume it's because usage of CMake means no .la
# files, i.e., no auto-magical library linking in Autotools.
# NOTE: '$$ORIGIN' is more linker special sauce insanity combined with quoting
# insanity combined with flag insanity ("-Wl,-rpath,stuff" vs. "-Wl,-rpath=stuff")
# combined with esoteric insanity (-Wl,-z,origin is here but might not be needed
# after all). Why is my TV suddenly talking to me, doc?
# Source 1: https://enchildfone.wordpress.com/2010/03/23/a-description-of-rpath-origin-ld_library_path-and-portable-linux-binaries/
# Source 2: https://longwei.github.io/rpath_origin/
# Source 3: https://stackoverflow.com/q/6324131
# Source 4: https://stackoverflow.com/a/33862145
#LDFLAGS += -Wl,-rpath,. -Wl,-rpath,'$$ORIGIN/../more/libs/here' -Wl,-z,origin

INCLUDE_FILES = -Ilmdb -Icryptopp -Ifcgi -Ifcgi/include -Ichacha20poly1305 \
	-Ilibbtc/src/secp256k1/include -Ibech32/ref/c++ -Ilibbtc/include \
	-Ilibwebsockets/build/include
ARMORYDB_SOURCE_FILES = main.cpp
ARMORYGUI_SOURCE_FILES = CoinSelection.cpp \
	SwigClient.cpp \
	TransactionBatch.cpp \
	WalletManager.cpp \
	Wallets.cpp \
        WebSocketClient.cpp
ARMORYCLI_SOURCE_FILES = BDM_mainthread.cpp \
	BDM_Server.cpp \
	BIP151.cpp \
	BitcoinP2P.cpp \
	BlockchainScanner.cpp \
	BlockchainScanner_Super.cpp \
	BlockDataViewer.cpp \
	BlockUtils.cpp \
	BtcWallet.cpp \
	DatabaseBuilder.cpp \
	HistoryPager.cpp \
	LedgerEntry.cpp \
	nodeRPC.cpp \
	Progress.cpp \
	ScrAddrFilter.cpp \
	ScrAddrObj.cpp \
	Server.cpp \
	SocketObject.cpp \
	SshParser.cpp \
	txio.cpp \
	ZeroConf.cpp
ARMORYCOMMON_SOURCE_FILES = Accounts.cpp \
	Addresses.cpp \
	AssetEncryption.cpp \
	Assets.cpp \
	BDM_seder.cpp \
	BinaryData.cpp \
	Blockchain.cpp \
	BlockDataManagerConfig.cpp \
	BlockDataMap.cpp \
	BlockObj.cpp \
	BtcUtils.cpp \
	DataObject.cpp \
	DBUtils.cpp \
	DecryptedDataContainer.cpp \
	DerivationScheme.cpp \
	EncryptionUtils.cpp \
	FcgiMessage.cpp \
	JSON_codec.cpp \
	lmdb_wrapper.cpp \
	log.cpp \
	ReentrantLock.cpp \
	Script.cpp \
	Signer.cpp \
	StoredBlockObj.cpp \
	StringSockets.cpp \
	Transactions.cpp \
	TxClasses.cpp \
	TxEvalState.cpp \
	txio.cpp \
	UniversalTimer.cpp \
	WebSocketMessage.cpp \
	bech32/ref/c++/bech32.cpp \
	bech32/ref/c++/segwit_addr.cpp

# libArmoryCommon - Required by all Armory programs/libraries.
# Includes any files required by the common files.
lib_LTLIBRARIES += $(LIBARMORYCOMMON)
libArmoryCommon_la_SOURCES = $(ARMORYCOMMON_SOURCE_FILES)
libArmoryCommon_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
libArmoryCommon_la_CXXFLAGS = $(AM_CXXFLAGS) -D__STDC_LIMIT_MACROS
libArmoryCommon_la_LIBADD = $(LIBLMDB) $(LIBCRYPTOPP) -lpthread
libArmoryCommon_la_LDFLAGS = $(LDFLAGS) -shared

# Common command-line functionality library
# NB: libwebsockets isn't Autotools, so that library's added differently than
# Autotools-handled libraries. Ironically, LDADD for binaries doesn't care. Odd.
lib_LTLIBRARIES += $(LIBARMORYCLI)
libArmoryCLI_la_SOURCES = $(ARMORYCLI_SOURCE_FILES)
libArmoryCLI_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES) $(PYTHON_CFLAGS)
libArmoryCLI_la_CXXFLAGS = $(AM_CXXFLAGS) -Ilmdb -D__STDC_LIMIT_MACROS
libArmoryCLI_la_LIBADD = $(LIBBTC) $(LIBARMORYCOMMON) \
			 $(LIBCHACHA20POLY1305) $(LIBSECP256K1) $(LIBFCGI) \
			 -Llibwebsockets/build/lib/ -lwebsockets \
			 -lpthread
libArmoryCLI_la_LDFLAGS = $(LDFLAGS) -Wl,-rpath,'$$ORIGIN/../libwebsockets/build/lib' -shared

#ArmoryDB
bin_PROGRAMS += ArmoryDB
ArmoryDB_SOURCES = $(ARMORYDB_SOURCE_FILES)
ArmoryDB_CXXFLAGS = $(AM_CXXFLAGS) -D__STDC_LIMIT_MACROS
ArmoryDB_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
ArmoryDB_LDADD = $(LIBFCGI) $(LIBARMORYCOMMON) $(LIBARMORYCLI) \
		 libwebsockets/build/lib/libwebsockets.so \
		 -lpthread
ArmoryDB_LDFLAGS = -static $(LDFLAGS)

if BUILD_CLIENT
# Common GUI functionality library
lib_LTLIBRARIES += $(LIBARMORYGUI)
libArmoryGUI_la_SOURCES = $(ARMORYGUI_SOURCE_FILES)
libArmoryGUI_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES) $(PYTHON_CFLAGS)
libArmoryGUI_la_CXXFLAGS = $(AM_CXXFLAGS) -D__STDC_LIMIT_MACROS
libArmoryGUI_la_LIBADD = $(LIBARMORYCOMMON) $(LIBCRYPTOPP) -lpthread
libArmoryGUI_la_LDFLAGS = $(LDFLAGS) $(PYTHON_LDFLAGS) -shared

#libCppBlockUtils - SWIG library.
# "shared" LDFLAG due to SWIG requirements.
lib_LTLIBRARIES += $(LIBCPPBLOCKUTILS)
libCppBlockUtils_la_SOURCES = CppBlockUtils_wrap.cxx
libCppBlockUtils_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES) $(PYTHON_CFLAGS)
libCppBlockUtils_la_CXXFLAGS = $(AM_CXXFLAGS) -D__STDC_LIMIT_MACROS
libCppBlockUtils_la_LIBADD = $(LIBARMORYCOMMON) $(LIBARMORYGUI) $(LIBCRYPTOPP) \
			     -lpthread
libCppBlockUtils_la_LDFLAGS = $(LDFLAGS) $(PYTHON_LDFLAGS) -shared

# While not strictly necessary, it may be necessary at some point to use install
# names (macOS equivlent of rpath) longer than the one used by default
# (/usr/local/lib). Let's plan for it. Don't use "-install_name" flag, as that
# only affects CppBlockUtils and not whatever it relies upon. Can fix that
# elsewhere with the "install_name_tool" binary.
if BUILD_DARWIN
libCppBlockUtils_la_LDFLAGS += -headerpad_max_install_names
libArmoryCommon_la_LDFLAGS += -headerpad_max_install_names
libArmoryCLI_la_LDFLAGS += -headerpad_max_install_names
endif

#custom rules - always force SWIG to run and compile the resultant C++ file
CppBlockUtils_wrap.cxx: CppBlockUtils.i
	swig $(SWIG_FLAGS) CppBlockUtils.i

.PHONY: CppBlockUtils_wrap.cxx
endif

clean-local:
	rm -f CppBlockUtils.py
	rm -f CppBlockUtils_wrap.cxx
	rm -f CppBlockUtils_wrap.h

# .include file prevents gtest subdir from building a second copy of Armory.
# Also provides a convenient spot to place all test-related materials.
if BUILD_TESTS
include Makefile.tests.include
endif
