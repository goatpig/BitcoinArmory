# Makefile for Bitcoin Armory - C/C++ subdirectory Makefile
bin_PROGRAMS =
noinst_PROGRAMS =
lib_LTLIBRARIES =

DIST_SUBDIRS = lmdb fcgi cryptopp
SUBDIRS = lmdb fcgi cryptopp $(MAYBE_BUILD)

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

INCLUDE_FILES = -Ibech32/ref/c++ -Ifcgi

ARMORYDB_SOURCE_FILES = main.cpp
ARMORYGUI_SOURCE_FILES = CoinSelection.cpp \
	SwigClient.cpp \
	TransactionBatch.cpp \
	WalletManager.cpp \
	Wallets.cpp
ARMORYCLI_SOURCE_FILES = BDM_mainthread.cpp \
	BDM_Server.cpp \
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
	SocketObject.cpp \
	StoredBlockObj.cpp \
	StringSockets.cpp \
	Transactions.cpp \
	TxClasses.cpp \
	TxEvalState.cpp \
	txio.cpp \
	UniversalTimer.cpp \
	bech32/ref/c++/bech32.cpp \
	bech32/ref/c++/segwit_addr.cpp

# libArmoryCommon - Required by all Armory programs/libraries.
# Includes any files required by the common files.
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
libArmoryCLI_la_CXXFLAGS = $(AM_CXXFLAGS) -Ilmdb -D__STDC_LIMIT_MACROS
libArmoryCLI_la_LIBADD = fcgi/libfcgi/libfcgi.la \
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
ArmoryDB_LDFLAGS = -static $(LDFLAGS)

if HAVE_GUI
# Common GUI functionality library
lib_LTLIBRARIES += libArmoryGUI.la
libArmoryGUI_la_SOURCES = $(ARMORYGUI_SOURCE_FILES)
libArmoryGUI_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES) \
			$(PYTHON_CFLAGS)
libArmoryGUI_la_CXXFLAGS = $(AM_CXXFLAGS) -D__STDC_LIMIT_MACROS
libArmoryGUI_la_LIBADD = libArmoryCommon.la cryptopp/libcryptopp.la \
			-lpthread
libArmoryGUI_la_LDFLAGS = $(LDFLAGS) $(PYTHON_LDFLAGS) -shared

#libCppBlockUtils - SWIG library.
# "shared" LDFLAG due to SWIG requirements.
lib_LTLIBRARIES += libCppBlockUtils.la
libCppBlockUtils_la_SOURCES = CppBlockUtils_wrap.cxx
libCppBlockUtils_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES) \
				$(PYTHON_CFLAGS)
libCppBlockUtils_la_CXXFLAGS = $(AM_CXXFLAGS) -Ilmdb \
				-Ifcgi/include \
				-D__STDC_LIMIT_MACROS
libCppBlockUtils_la_LIBADD = libArmoryCommon.la libArmoryGUI.la \
				cryptopp/libcryptopp.la -lpthread
libCppBlockUtils_la_LDFLAGS = $(LDFLAGS) $(PYTHON_LDFLAGS) -shared

#custom rules
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
