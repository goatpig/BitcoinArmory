# Makefile for Bitcoin Armory - C/C++ subdirectory Makefile
bin_PROGRAMS =
noinst_PROGRAMS =
BENCH =
lib_LTLIBRARIES =
MAYBE_BUILD =

LIBBTC = libbtc/libbtc.la
LIBCHACHA20POLY1305 = chacha20poly1305/.libs/libchacha20poly1305.la
LIBCPPBLOCKUTILS = libCppBlockUtils.la
LIBCRYPTOPP = cryptopp/libcryptopp.la
LIBFCGI = fcgi/libfcgi/.libs/libfcgi.la
LIBLMDB = lmdb/.libs/liblmdb.la
LIBSECP256K1 = libbtc/src/secp256k1/libsecp256k1.la

DIST_SUBDIRS = lmdb fcgi cryptopp libbtc chacha20poly1305
SUBDIRS = lmdb fcgi cryptopp libbtc chacha20poly1305 $(MAYBE_BUILD)

SWIG_FLAGS = -c++ -python -threads
AM_CXXFLAGS = $(CXXFLAGS)

if HAVE_GCC
SWIG_FLAGS += -D__GNUC__
if HAVE_64BIT
SWIG_FLAGS += -D__LP64__
endif
endif

if HAVE_CLANG
SWIG_FLAGS += -D__CLANG__
endif

INCLUDE_FILES = -Ilmdb -Icryptopp -Ifcgi -Ifcgi/include -Ichacha20poly1305 \
	-Ilibbtc/src/secp256k1/include -Ibech32/ref/c++

ARMORYDB_SOURCE_FILES = main.cpp

ARMORYGUI_SOURCE_FILES = CoinSelection.cpp \
	SwigClient.cpp \
	TransactionBatch.cpp \
	WalletManager.cpp \
	Wallets.cpp
ARMORYCLI_SOURCE_FILES = Accounts.cpp \
	Addresses.cpp \
	AssetEncryption.cpp \
	Assets.cpp \
	BDM_mainthread.cpp \
	BDM_Server.cpp \
	BitcoinP2P.cpp \
	BlockchainScanner.cpp \
	BlockchainScanner_Super.cpp \
	BlockDataViewer.cpp \
	BlockUtils.cpp \
	BtcWallet.cpp \
	DatabaseBuilder.cpp \
	DataObject.cpp \
	DecryptedDataContainer.cpp \
	DerivationScheme.cpp \
	FcgiMessage.cpp \
	HistoryPager.cpp \
	LedgerEntry.cpp \
	lmdb_wrapper.cpp \
	log.cpp \
	nodeRPC.cpp \
	Progress.cpp \
	ReentrantLock.cpp \
	ScrAddrFilter.cpp \
	ScrAddrObj.cpp \
	SocketObject.cpp \
	SshParser.cpp \
	StringSockets.cpp \
	StoredBlockObj.cpp \
	txio.cpp \
	ZeroConf.cpp
ARMORYCOMMON_SOURCE_FILES = BDM_seder.cpp \
	BinaryData.cpp \
	Blockchain.cpp \
	BlockDataManagerConfig.cpp \
	BlockDataMap.cpp \
	BlockObj.cpp \
	BtcUtils.cpp \
	DBUtils.cpp \
	EncryptionUtils.cpp \
	JSON_codec.cpp \
	Script.cpp \
	Signer.cpp \
	Transactions.cpp \
	TxClasses.cpp \
	TxEvalState.cpp \
	txio.cpp \
	UniversalTimer.cpp \
	bech32/ref/c++/bech32.cpp \
	bech32/ref/c++/segwit_addr.cpp

#ArmoryDB
bin_PROGRAMS += ArmoryDB
ArmoryDB_SOURCES = $(ARMORYDB_SOURCE_FILES)
ArmoryDB_CXXFLAGS = $(AM_CXXFLAGS) -D__STDC_LIMIT_MACROS
ArmoryDB_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
ArmoryDB_LDADD = $(LIBLMDB) $(LIBCRYPTOPP) $(LIBFCGI) -lpthread
ArmoryDB_LDFLAGS = -static $(LDFLAGS)

# libArmoryCommon - Required by all Armory programs/libraries.
# May be able to modularize in the future so that it's not monolithic.
lib_LTLIBRARIES += libArmoryCommon.la
libArmoryCommon_la_SOURCES = $(ARMORYCOMMON_SOURCE_FILES)
libArmoryCommon_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
libArmoryCommon_la_CXXFLAGS = $(AM_CXXFLAGS) -D__STDC_LIMIT_MACROS
libArmoryCommon_la_LIBADD = lmdb/liblmdb.la \
				cryptopp/libcryptopp.la \
				-lpthread
libArmoryCommon_la_LDFLAGS = $(LDFLAGS) -shared

# Common command-line functionality library
lib_LTLIBRARIES += libArmoryCLI.la
libArmoryCLI_la_SOURCES = $(ARMORYCLI_SOURCE_FILES)
libArmoryCLI_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES) \
			$(PYTHON_CFLAGS)
libArmoryCLI_la_CXXFLAGS = $(AM_CXXFLAGS) -Ilmdb \
			-D__STDC_LIMIT_MACROS
libArmoryCLI_la_LIBADD = lmdb/liblmdb.la \
			 fcgi/libfcgi/libfcgi.la \
			 libArmoryCommon.la \
			-lpthread
libArmoryCLI_la_LDFLAGS = $(LDFLAGS) -shared

#ArmoryDB
bin_PROGRAMS += ArmoryDB
ArmoryDB_SOURCES = $(ARMORYDB_SOURCE_FILES)
ArmoryDB_CXXFLAGS = $(AM_CXXFLAGS) -D__STDC_LIMIT_MACROS
ArmoryDB_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
ArmoryDB_LDADD = fcgi/libfcgi/libfcgi.la \
		 libArmoryCommon.la \
		 libArmoryCLI.la \
		 -lpthread

if HAVE_GUI
# Common GUI functionality library
lib_LTLIBRARIES += $(LIBARMORYGUI)
libArmoryGUI_la_SOURCES = $(ARMORYGUI_SOURCE_FILES)
libArmoryGUI_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES) \
			$(PYTHON_CFLAGS)
libArmoryGUI_la_CXXFLAGS = $(AM_CXXFLAGS)
			-D__STDC_LIMIT_MACROS
libArmoryGUI_la_LIBADD = $(LIBARMORYCOMMON) -lpthread
libArmoryGUI_la_LDFLAGS = $(LDFLAGS) $(PYTHON_LDFLAGS) -shared

#libCppBlockUtils - SWIG library.
# "shared" LDFLAG due to SWIG requirements.
lib_LTLIBRARIES += $(LIBCPPBLOCKUTILS)
libCppBlockUtils_la_SOURCES = CppBlockUtils_wrap.cxx
libCppBlockUtils_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES) $(PYTHON_CFLAGS)
libCppBlockUtils_la_CXXFLAGS = $(AM_CXXFLAGS) -Ilmdb -Ifcgi/include \
				-D__STDC_LIMIT_MACROS
libCppBlockUtils_la_LIBADD = $(LIBARMORYGUI) \
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
TESTS =
include Makefile.tests.include
endif
