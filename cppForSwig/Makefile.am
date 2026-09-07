# Makefile for Bitcoin Armory - C/C++ subdirectory Makefile
bin_PROGRAMS =
noinst_PROGRAMS =
lib_LTLIBRARIES =
noinst_LTLIBRARIES =
TESTS =
BUILT_SOURCES =

LIBBTC = $(LIBBTC_LIBDIR)/libbtc.la
LIBCHACHA20POLY1305 = chacha20poly1305/libchacha20poly1305.la
LIBHKDF = hkdf/libhkdf.la
LIBTREZORCRYPTO = libTrezorCrypto.la

LIBARMORYUTILS = libArmoryUtils.la
LIBARMORYWALLETS = libArmoryWallets.la
LIBARMORYSIGNER = libArmorySigner.la
LIBBLOCKCHAINDB = libBlockchainDb.la
LIBARMORYCOMMON = libArmoryCommon.la
LIBARMORYGUI = libArmoryGUI.la
LIBARMORYCLI = libArmoryCLI.la
LIBLMDBPP = liblmdbpp.la
LIBSECP256K1 = $(LIBBTC_LIBDIR)/src/secp256k1/libsecp256k1.la
LIBWEBSOCKETS_STATIC = $(WEBSOCKETS_LIBDIR)/libwebsockets.a
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

SUBDIRS = chacha20poly1305 hkdf $(MAYBE_BUILD)

# Unit test flags.
UNIT_TEST_CXXFLAGS=
if BUILD_TESTS
UNIT_TEST_CXXFLAGS += -DUNIT_TESTS -Wunused-parameter
endif

LIBBTC_FLAGS =

AM_CXXFLAGS = -Wfatal-errors

################################################################################
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

################################################################################
## capnp
capnp/%.capnp.c++ capnp/%.capnp.h: capnp/%.capnp
	capnp compile --src-prefix=$(srcdir)/capnp -oc++:capnp $<

INCLUDE_FILES = -I$(srcdir)/lmdbpp \
	-I$(srcdir)/chacha20poly1305 -I$(LIBBTC_LIBDIR)/src/secp256k1/include -I$(LIBBTC_LIBDIR)/src \
	-I$(LIBBTC_LIBDIR)/include $(LWS_CFLAGS) -I$(srcdir)/hkdf -I$(srcdir)/Wallets -I$(srcdir)/Signer

################################################################################
# Files should *not* be marked as "common" if at all possible. Also, a refactor
# might not hurt one of these days. Some of the "common" files could be GUI/CLI
# files but are "common" for the sake of unit testing. In an ideal world, the
# unit tests would be split into multiple files as needed. One day....
KEYMANAGER_SOURCE_FILES = KeyManager.cpp

CPPBRIDGE_SOURCE_FILES = BridgeAPI/CppBridge.cpp \
	BridgeAPI/BridgeSocket.cpp \
	BridgeAPI/ProtoCommandParser.cpp \
	BridgeAPI/PassphrasePrompt.cpp \
	BridgeAPI/Wallets/Container.cpp \
	BridgeAPI/Wallets/Loader.cpp \
	BridgeAPI/Wallets/Manager.cpp \
	BridgeAPI/Wallets/Notifications.cpp \
	BridgeAPI/Wallets/TxIOCache.cpp \
	BridgeAPI/DBSetup.cpp \
	Ledgers/Context.cpp Ledgers/LedgerEntry.cpp

LIBTREZORCRYPTO_SOURCE_FILES = $(LIBBTC_LIBDIR)/src/trezor-crypto/bip39.c \
	$(LIBBTC_LIBDIR)/src/trezor-crypto/pbkdf2.c \
	$(LIBBTC_LIBDIR)/src/trezor-crypto/memzero.c \
	$(LIBBTC_LIBDIR)/src/trezor-crypto/hmac.c \
	$(LIBBTC_LIBDIR)/src/trezor-crypto/sha2.c

ARMORYUTILS_SOURCE_FILES = Utils/ArmoryConfig.cpp \
	Utils/BinaryData.cpp \
	Utils/Types.cpp \
	Utils/BIP150_151.cpp \
	Utils/BIP15x_Handshake.cpp \
	Utils/BitcoinSettings.cpp \
	Utils/OpCodes.cpp \
	Utils/BCTX.cpp \
	Utils/varint.cpp \
	Utils/BtcUtils.cpp \
	Utils/TxOutScrRef.cpp \
	Utils/Cryptography.cpp \
	Utils/DBUtils.cpp \
	Utils/FileUtils.cpp \
	Utils/HttpMessage.cpp \
	Utils/JSON_codec.cpp \
	Utils/log.cpp \
	Utils/ReentrantLock.cpp \
	Utils/SecureBinaryData.cpp \
	Utils/UniversalTimer.cpp

ARMORYWALLETS_SOURCE_FILES = Wallets/BIP32_Node.cpp \
	capnp/Types.capnp.c++ \
	capnp/Bridge.capnp.c++ \
	Wallets/Addresses.cpp \
	Wallets/DecryptedDataContainer.cpp \
	Wallets/DerivationScheme.cpp \
	Wallets/Accounts/AccountTypes.cpp \
	Wallets/Accounts/AssetAccounts.cpp \
	Wallets/Accounts/AddressAccounts.cpp \
	Wallets/Accounts/MetaAccounts.cpp \
	Wallets/WalletIdTypes.cpp \
	Wallets/Assets.cpp \
	Wallets/WalletHeader.cpp \
	Wallets/EncryptedDB.cpp \
	Wallets/WalletFileInterface.cpp \
	Wallets/AssetEncryption.cpp \
	Wallets/Wallets.cpp \
	Wallets/AuthorizedPeers.cpp \
	Wallets/Progress.cpp \
	Wallets/Seeds/Backups.cpp \
	Wallets/Seeds/Seeds.cpp \
	Wallets/KDF.cpp \
	Wallets/GetPassphrase.cpp \
	Wallets/random_buffer.c

ARMORYSIGNER_SOURCE_FILES = Signer/Script.cpp \
	capnp/Types.capnp.c++ \
	capnp/Signer.capnp.c++ \
	Signer/ScriptRecipient.cpp \
	Signer/StackItems.cpp \
	Signer/ScriptSpender.cpp \
	Signer/Signer.cpp \
	Signer/PSBT.cpp \
	Signer/LegacySigner.cpp \
	Signer/ResolverFeed.cpp \
	Signer/ResolverFeed_Wallets.cpp \
	Signer/Transactions.cpp \
	Signer/TxEvalState.cpp \
	Signer/CoinSelection.cpp \
	capnp/Signer.capnp.c++ \
	capnp/Types.capnp.c++

BLOCKCHAINDB_SOURCE_FILES = BlockchainDatabase/Blockchain.cpp \
	BlockchainDatabase/BlockchainData.cpp \
	BlockchainDatabase/BlockchainScanner.cpp \
	BlockchainDatabase/BlockchainScanner_Super.cpp \
	BlockchainDatabase/BlockDataMap.cpp \
	BlockchainDatabase/BlockObj.cpp \
	BlockchainDatabase/BlockUtils.cpp \
	BlockchainDatabase/DatabaseBuilder.cpp \
	BlockchainDatabase/lmdb_wrapper.cpp \
	BlockchainDatabase/ScrAddrFilter.cpp \
	BlockchainDatabase/StoredBlockObj.cpp \
	BlockchainDatabase/TxHashFilters.cpp \
	BlockchainDatabase/txio.cpp

ARMORYCLI_SOURCE_FILES = BDM_mainthread.cpp \
	BDM_Server.cpp \
	BDV_Notification.cpp \
	Node/BitcoinP2P.cpp \
	Node/nodeRPC.cpp \
	BlockDataViewer.cpp \
	BtcWallet.cpp \
	Progress.cpp \
	ScrAddrObj.cpp \
	Server.cpp \
	Network/SocketService_unix.cpp \
	Network/StringSockets.cpp \
	ZeroConf/Utils.cpp \
	ZeroConf/Notifications.cpp \
	ZeroConf/Parser.cpp \
	TerminalPassphrasePrompt.cpp \
	gtest/MockedNode.cpp

ARMORYCOMMON_SOURCE_FILES = capnp/Types.capnp.c++ \
	capnp/BDV.capnp.c++ \
	AsyncClient.cpp \
	DBClientClasses.cpp \
	Network/SocketObject.cpp \
	TxClasses.cpp \
	Network/WebSocketClient.cpp \
	Network/WebSocketMessage.cpp

################################################################################
## LMDB (DB used by Armory). On macOS, don't use SYSV semaphores (the default),
## which cause crashes due to mutex lock/unlock issues. Use POSIX semaphores
## (same as Linux).
noinst_LTLIBRARIES += $(LIBLMDBPP)
liblmdbpp_la_SOURCES = lmdbpp/lmdbpp.cpp
liblmdbpp_la_CPPFLAGS = -Ilmdbpp -fPIC
liblmdbpp_la_LIBADD = -llmdb
liblmdbpp_la_LDFLAGS = -static

################################################################################
## libTrezorCrypto
noinst_LTLIBRARIES += $(LIBTREZORCRYPTO)
libTrezorCrypto_la_SOURCES = $(LIBTREZORCRYPTO_SOURCE_FILES)
libTrezorCrypto_la_CPPFLAGS = $(AM_CPPFLAGS)
libTrezorCrypto_la_CXXFLAGS = $(AM_CXXFLAGS) -fPIC
libTrezorCrypto_la_LDFLAGS = $(LDFLAGS) -static

################################################################################
## libArmoryUtils
noinst_LTLIBRARIES += $(LIBARMORYUTILS)
libArmoryUtils_la_SOURCES = $(ARMORYUTILS_SOURCE_FILES)
libArmoryUtils_la_CPPFLAGS = $(AM_CPPFLAGS) \
	$(INCLUDE_FILES)
libArmoryUtils_la_CXXFLAGS = $(AM_CXXFLAGS) \
	$(LIBBTC_FLAGS) \
	$(UNIT_TEST_CXXFLAGS) \
	-D__STDC_LIMIT_MACROS
libArmoryUtils_la_LIBADD = $(LIBBTC) \
	$(LIBSECP256K1) \
	$(LIBTREZORCRYPTO)
libArmoryUtils_la_LDFLAGS = $(LDFLAGS)

################################################################################
## libArmoryWallets
noinst_LTLIBRARIES += $(LIBARMORYWALLETS)
libArmoryWallets_la_SOURCES = $(ARMORYWALLETS_SOURCE_FILES)
libArmoryWallets_la_CPPFLAGS = $(AM_CPPFLAGS) \
	$(INCLUDE_FILES)
libArmoryWallets_la_CXXFLAGS = $(AM_CXXFLAGS) \
	$(LIBBTC_FLAGS) \
	$(UNIT_TEST_CXXFLAGS) \
	-D__STDC_LIMIT_MACROS
libArmoryWallets_la_LIBADD = $(LIBBTC) \
	$(LIBSECP256K1) \
	$(LIBTREZORCRYPTO)
libArmoryWallets_la_LDFLAGS = $(LDFLAGS)

################################################################################
## libArmorySigner
noinst_LTLIBRARIES += $(LIBARMORYSIGNER)
libArmorySigner_la_SOURCES = $(ARMORYSIGNER_SOURCE_FILES)
libArmorySigner_la_CPPFLAGS = $(AM_CPPFLAGS) \
	$(INCLUDE_FILES)
libArmorySigner_la_CXXFLAGS = $(AM_CXXFLAGS) \
	$(LIBBTC_FLAGS) \
	$(UNIT_TEST_CXXFLAGS) \
	-D__STDC_LIMIT_MACROS
libArmorySigner_la_LIBADD = $(LIBBTC) \
	$(LIBSECP256K1) \
	$(LIBTREZORCRYPTO) \
	-lcapnp \
	-lkj
libArmorySigner_la_LDFLAGS = $(LDFLAGS)

################################################################################
## libBlockchainDb
noinst_LTLIBRARIES += $(LIBBLOCKCHAINDB)
libBlockchainDb_la_SOURCES = $(BLOCKCHAINDB_SOURCE_FILES)
libBlockchainDb_la_CPPFLAGS = $(AM_CPPFLAGS) \
	$(INCLUDE_FILES)
libBlockchainDb_la_CXXFLAGS = $(AM_CXXFLAGS) \
	$(LIBBTC_FLAGS) \
	$(UNIT_TEST_CXXFLAGS) \
	-D__STDC_LIMIT_MACROS
libBlockchainDb_la_LIBADD = $(LIBBTC) \
	-llmdb \
	-lpthread
libBlockchainDb_la_LDFLAGS = $(LDFLAGS)

################################################################################
## libArmoryCommon - Required by all Armory programs/libraries.
## Common functionality across GUI and command line
noinst_LTLIBRARIES += $(LIBARMORYCOMMON)
libArmoryCommon_la_SOURCES = $(ARMORYCOMMON_SOURCE_FILES)
libArmoryCommon_la_CPPFLAGS = $(AM_CPPFLAGS) \
	$(INCLUDE_FILES)
libArmoryCommon_la_CXXFLAGS = $(AM_CXXFLAGS) \
	$(LIBBTC_FLAGS) \
	$(UNIT_TEST_CXXFLAGS) \
	-D__STDC_LIMIT_MACROS
libArmoryCommon_la_LIBADD = $(LIBHKDF) \
	$(LIBBTC) \
	$(LIBSECP256K1) \
	$(LIBTREZORCRYPTO) \
	$(LIBARMORYUTILS) \
	$(LIBARMORYWALLETS) \
	$(LIBARMORYSIGNER) \
	$(LIBBLOCKCHAINDB) \
	-lpthread
libArmoryCommon_la_LDFLAGS = $(LDFLAGS) \
	$(LWSLDFLAGS)

################################################################################
## Command-line functionality library
noinst_LTLIBRARIES += $(LIBARMORYCLI)
libArmoryCLI_la_SOURCES = $(ARMORYCLI_SOURCE_FILES)
libArmoryCLI_la_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES) \
	$(PYTHON_CFLAGS)
libArmoryCLI_la_CXXFLAGS = $(AM_CXXFLAGS) $(LIBBTC_FLAGS) \
	$(UNIT_TEST_CXXFLAGS) -D__STDC_LIMIT_MACROS
libArmoryCLI_la_LIBADD = $(LIBCHACHA20POLY1305) \
	$(LIBARMORYWALLETS) \
	$(LIBARMORYSIGNER) \
	$(LIBBLOCKCHAINDB) \
	$(LIBARMORYCOMMON) \
	$(LIBLMDBPP) \
	$(LIBHKDF) \
	$(LIBBTC) \
	$(LIBSECP256K1) \
	$(LIBTREZORCRYPTO) \
	-llmdb \
	-lcapnp \
	-lkj
libArmoryCLI_la_LDFLAGS = $(LDFLAGS) $(LWSLDFLAGS)

################################################################################
## ArmoryDB binary
bin_PROGRAMS += ArmoryDB
ArmoryDB_SOURCES = main.cpp
ArmoryDB_CXXFLAGS = $(AM_CXXFLAGS) $(LIBBTC_FLAGS) $(UNIT_TEST_CXXFLAGS) \
	-D__STDC_LIMIT_MACROS
ArmoryDB_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
ArmoryDB_LDADD = $(LIBARMORYWALLETS) \
	$(LIBARMORYSIGNER) \
	$(LIBBLOCKCHAINDB) \
	$(LIBARMORYCOMMON) \
	$(LIBARMORYCLI) \
	$(LIBHKDF) \
	$(LIBBTC) \
	$(LIBSECP256K1) \
	$(LIBTREZORCRYPTO) \
	$(LIBCHACHA20POLY1305) \
	$(LIBLMDBPP) \
	-llmdb \
	$(LIBWEBSOCKETS_STATIC) \
	-lcapnp \
	-lkj
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
if BUILD_LDL_REQUIRED
ArmoryDB_LDADD += -ldl
endif
ArmoryDB_LDFLAGS = $(LDFLAGS) -static

################################################################################
## KeyManager
bin_PROGRAMS += BIP150KeyManager
BIP150KeyManager_SOURCES = $(KEYMANAGER_SOURCE_FILES)
BIP150KeyManager_CXXFLAGS = $(AM_CXXFLAGS) \
	$(LIBBTC_FLAGS) \
	$(UNIT_TEST_CXXFLAGS) \
	-D__STDC_LIMIT_MACROS
BIP150KeyManager_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
BIP150KeyManager_LDADD = $(LIBARMORYCOMMON) \
	$(LIBARMORYCLI) \
	$(LIBHKDF) \
	$(LIBBTC) \
	$(LIBCHACHA20POLY1305) \
	$(LIBTREZORCRYPTO) \
	$(LIBLMDBPP) \
	-llmdb \
	$(LIBWEBSOCKETS_STATIC) \
	-lpthread \
	-lcapnp \
	-lkj
if BUILD_LIBCAP_SUPPORT
BIP150KeyManager_LDADD += $(LIBCAP_LIBS)
endif

BIP150KeyManager_LDFLAGS = $(LWSLDFLAGS) $(LDFLAGS) -static

################################################################################
## CppBridge
if BUILD_CLIENT
bin_PROGRAMS += CppBridge
CppBridge_SOURCES = BridgeAPI/BridgeMain.cpp \
	$(CPPBRIDGE_SOURCE_FILES)
CppBridge_CXXFLAGS = $(AM_CXXFLAGS) \
	$(LIBBTC_FLAGS) \
	$(UNIT_TEST_CXXFLAGS) \
	-D__STDC_LIMIT_MACROS
CppBridge_CPPFLAGS = $(AM_CPPFLAGS) $(INCLUDE_FILES)
CppBridge_LDADD = $(LIBARMORYCOMMON) \
	$(LIBARMORYCLI) \
	$(LIBHKDF) \
	$(LIBBTC) \
	$(LIBCHACHA20POLY1305) \
	$(LIBTREZORCRYPTO) \
	$(LIBLMDBPP) \
	-llmdb \
	$(LIBWEBSOCKETS_STATIC)
if BUILD_OPENSSL_SUPPORT
CppBridge_LDADD += $(LIBSSL_STATIC) \
	$(LIBCRYPTO_STATIC)
endif
if BUILD_LIBUV_SUPPORT
CppBridge_LDADD += $(LIBUV_STATIC)
endif
if BUILD_LIBEVENT_SUPPORT
CppBridge_LDADD += $(LIBEVENT_STATIC)
endif
if BUILD_LIBCAP_SUPPORT
CppBridge_LDADD += $(LIBCAP_LIBS)
endif
if BUILD_LDL_REQUIRED
CppBridge_LDADD += -ldl
endif
CppBridge_LDFLAGS = $(LWSLDFLAGS) $(LDFLAGS) -static
endif

clean-local:
	rm -f CppBridge
	rm -f BIP150KeyManager

# .include file prevents gtest subdir from building a second copy of Armory.
# Also provides a convenient spot to place all test-related materials.
if BUILD_TESTS
include Makefile.tests.include
endif