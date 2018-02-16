# Makefile for Bitcoin Armory - C/C++ test subdirectory Makefile
SUBDIRS = ../lmdb ../fcgi ../cryptopp
TESTS =
AM_CXXFLAGS =
bin_PROGRAMS =

# "#include <tr1/tuple>" causes a build error on OSX without this flag.
if BUILD_DARWIN
AM_CXXFLAGS += -DGTEST_USE_OWN_TR1_TUPLE=1
endif

INCLUDE_FILES = -I.. -I../cryptopp -I../lmdb -I../fcgi -I../bech32/ref/c++

SOURCE_FILES = ../UniversalTimer.cpp ../BinaryData.cpp ../lmdb_wrapper.cpp \
	../BtcUtils.cpp ../DBUtils.cpp ../BlockObj.cpp ../BlockUtils.cpp ../EncryptionUtils.cpp \
	../BtcWallet.cpp ../LedgerEntry.cpp ../ScrAddrObj.cpp ../Blockchain.cpp \
	../BDM_mainthread.cpp ../BDM_supportClasses.cpp \
	../BlockDataViewer.cpp ../HistoryPager.cpp ../Progress.cpp \
	../txio.cpp ../TxClasses.cpp ../StoredBlockObj.cpp \
	../DatabaseBuilder.cpp ../BlockchainScanner.cpp ../BlockchainScanner_Super.cpp ../BlockDataMap.cpp \
	../DataObject.cpp ../BitcoinP2P.cpp ../BDM_Server.cpp ../BDM_seder.cpp ../SocketObject.cpp \
	../FcgiMessage.cpp ../BlockDataManagerConfig.cpp \
	../Transactions.cpp ../nodeRPC.cpp ../JSON_codec.cpp \
	../StringSockets.cpp ../ReentrantLock.cpp \
	../WalletManager.cpp ../Wallets.cpp ../Script.cpp ../Signer.cpp ../SwigClient.cpp \
	../CoinSelection.cpp ../log.cpp ../TxEvalState.cpp \
	../Accounts.cpp ../Addresses.cpp ../AssetEncryption.cpp ../Assets.cpp \
	../DecryptedDataContainer.cpp ../DerivationScheme.cpp ../SshParser.cpp \
	../bech32/ref/c++/bech32.cpp ../bech32/ref/c++/segwit_addr.cpp \
	../BIP32_Serialization.cpp \
	TestUtils.cpp gtest-all.cc

# Files to add later, once they compile

AM_CXXFLAGS += $(CXXFLAGS)
AM_LDFLAGS = -static $(LDFLAGS)

# CppBlockUtilsTests
bin_PROGRAMS += CppBlockUtilsTests
TESTS += CppBlockUtilsTests
CppBlockUtilsTests_SOURCES = $(SOURCE_FILES) CppBlockUtilsTests.cpp
CppBlockUtilsTests_CXXFLAGS = $(AM_CXXFLAGS) -D__STDC_LIMIT_MACROS
CppBlockUtilsTests_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
CppBlockUtilsTests_LDADD = -L../lmdb -llmdb \
	-L../cryptopp -lcryptopp \
	-L../fcgi/libfcgi/ -lfcgi \
	-lpthread $(LDFLAGS)

# DB1kIterTest
bin_PROGRAMS += DB1kIterTest
TESTS += DB1kIterTest
DB1kIterTest_SOURCES = $(SOURCE_FILES) DB1kIterTest.cpp
DB1kIterTest_CXXFLAGS = $(AM_CXXFLAGS) $(CXXFLAGS)
DB1kIterTest_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
DB1kIterTest_LDADD = -L../lmdb -llmdb \
	-L../cryptopp -lcryptopp \
	-L../fcgi/libfcgi/ -lfcgi \
	-lpthread $(LDFLAGS)

# ContainerTests
bin_PROGRAMS += ContainerTests
TESTS += ContainerTests
ContainerTests_SOURCES = ContainerTests.cpp gtest.h gtest-all.cc
ContainerTests_CXXFLAGS = $(AM_CXXFLAGS) $(CXXFLAGS)
ContainerTests_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
ContainerTests_LDADD = -lpthread $(LDFLAGS)

# SupernodeTests (NOT YET FULLY FUNCTIONAL)
#bin_PROGRAMS += SupernodeTests
#TESTS += SupernodeTests
#SupernodeTests_SOURCES = $(SOURCE_FILES) SupernodeTests.cpp
#SupernodeTests_CXXFLAGS = $(AM_CPPFLAGS) $(CXXFLAGS)
#SupernodeTests_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
#SupernodeTests_LDADD = -L../lmdb -llmdb \
#	-L../cryptopp -lcryptopp \
#	-L../fcgi/libfcgi/ -lfcgi \
#	-lpthread $(LDFLAGS)
