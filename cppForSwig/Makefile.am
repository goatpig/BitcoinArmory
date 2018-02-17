# Makefile for Bitcoin Armory - C/C++ subdirectory Makefile
bin_PROGRAMS =
noinst_PROGRAMS =
TESTS =
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

if BUILD_TESTS
MAYBE_BUILD += gtest
endif

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

DB_SOURCE_FILES = UniversalTimer.cpp BinaryData.cpp lmdb_wrapper.cpp \
	BtcUtils.cpp DBUtils.cpp BlockObj.cpp BlockUtils.cpp EncryptionUtils.cpp \
	BtcWallet.cpp LedgerEntry.cpp ScrAddrObj.cpp Blockchain.cpp \
	BDM_mainthread.cpp BDM_supportClasses.cpp \
	BlockDataViewer.cpp HistoryPager.cpp Progress.cpp \
	txio.cpp TxClasses.cpp StoredBlockObj.cpp \
	DatabaseBuilder.cpp BlockchainScanner.cpp BlockchainScanner_Super.cpp BlockDataMap.cpp \
	DataObject.cpp BitcoinP2P.cpp BDM_Server.cpp BDM_seder.cpp SocketObject.cpp \
	FcgiMessage.cpp BlockDataManagerConfig.cpp \
	Transactions.cpp Script.cpp Signer.cpp nodeRPC.cpp JSON_codec.cpp \
	StringSockets.cpp main.cpp ReentrantLock.cpp log.cpp TxEvalState.cpp \
	bech32/ref/c++/bech32.cpp bech32/ref/c++/segwit_addr.cpp \
        SshParser.cpp

CPPBLOCKUTILS_SOURCE_FILES = UniversalTimer.cpp BinaryData.cpp \
	BtcUtils.cpp DBUtils.cpp EncryptionUtils.cpp \
	BDM_seder.cpp DataObject.cpp FcgiMessage.cpp \
	SocketObject.cpp SwigClient.cpp StringSockets.cpp \
	BlockDataManagerConfig.cpp TxClasses.cpp \
	WalletManager.cpp Wallets.cpp Script.cpp Signer.cpp \
	Transactions.cpp \
	CoinSelection.cpp ReentrantLock.cpp JSON_codec.cpp \
	TransactionBatch.cpp log.cpp TxEvalState.cpp \
	Accounts.cpp Addresses.cpp AssetEncryption.cpp Assets.cpp \
	DecryptedDataContainer.cpp DerivationScheme.cpp \
	bech32/ref/c++/bech32.cpp bech32/ref/c++/segwit_addr.cpp

#ArmoryDB
bin_PROGRAMS += ArmoryDB
ArmoryDB_SOURCES = $(DB_SOURCE_FILES)
ArmoryDB_CXXFLAGS = $(AM_CXXFLAGS) -D__STDC_LIMIT_MACROS
ArmoryDB_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
ArmoryDB_LDADD = $(LIBLMDB) $(LIBCRYPTOPP) $(LIBFCGI) -lpthread
ArmoryDB_LDFLAGS = -static $(LDFLAGS)

#libCppBlockUtils
if HAVE_GUI
lib_LTLIBRARIES += $(LIBCPPBLOCKUTILS)
libCppBlockUtils_la_SOURCES = $(CPPBLOCKUTILS_SOURCE_FILES) \
			      CppBlockUtils_wrap.cxx
libCppBlockUtils_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
libCppBlockUtils_la_CXXFLAGS = $(AM_CXXFLAGS) $(AX_SWIG_PYTHON_CPPFLAGS) \
			       $(EXTRA_PYTHON_INCLUDES) -D__STDC_LIMIT_MACROS
libCppBlockUtils_la_LIBADD = $(LIBLMDB) $(LIBCRYPTOPP) $(LIBSECP256K1) -lpthread
libCppBlockUtils_la_LDFLAGS = $(LDFLAGS) -shared

if BUILD_DARWIN
libCppBlockUtils_la_LDFLAGS += -Wl,-rpath,@executable_path/ -Wl,-rpath,@loader_path/
endif

#custom rules - always force SWIG to run and compile the resultant C++ file
CppBlockUtils_wrap.cxx: CppBlockUtils.i
	swig $(SWIG_FLAGS) CppBlockUtils.i 

.PHONY: CppBlockUtils_wrap.cxx

clean-local:
	rm -f CppBlockUtils.py
	rm -f CppBlockUtils_wrap.cxx
	rm -f CppBlockUtils_wrap.h
endif
