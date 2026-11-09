////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdlib>
#include <future>
#include <queue>
#include <set>
#include <algorithm>

#include "TestUtils.h"
#include <reorgTest/blkdata.h>
#include <Utils/ArmoryConfig.h>
#include <Utils/FileUtils.h>
#include <Utils/BIP15x_Handshake.h>
#include <Ledgers/LedgerEntry.h>
#include <Ledgers/Context.h>
#include <BlockchainDatabase/BlockchainData.h>

#include <Wallets/Accounts/AddressAccounts.h>
#include <Wallets/Accounts/AccountTypes.h>
#include <Wallets/Addresses.h>
#include <Wallets/WalletFileInterface.h>
#include <Wallets/Seeds/Seeds.h>
#include <Wallets/AuthorizedPeers.h>

#include <BridgeAPI/CppBridge.h>
#include <BridgeAPI/BridgeSocket.h>
#include <BridgeAPI/ProtoCommandParser.h>
#include <BridgeAPI/Notifications.h>
#include <BridgeAPI/Wallets/Manager.h>
#include <BridgeAPI/Wallets/TxIOCache.h>
#include <BridgeAPI/DBSetup.h>
#include <Signer/Signer.h>
#include <Signer/ScriptSpender.h>

#include "BDM_mainthread.h"
#include "Server.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/Bridge.capnp.h"
#include "capnp/BDV.capnp.h"
#include "capnp/Types.capnp.h"

using namespace Armory;

using namespace std::chrono_literals;
using namespace std::string_view_literals;

using MsgPtr = std::unique_ptr<Bridge::WritePayload_Bridge>;

////////////////////////////////////////////////////////////////////////////////
namespace BridgeTestUtils {
   extern int CapnWalletState_Legacy;
   extern int CapnWalletState_Encrypted;
   extern int CapnWalletState_Ready;
   extern std::string legacyAccId;
   BinaryData serializeCapnp(capnp::MallocMessageBuilder& msg);

   struct WltListEntry
   {
      std::string walletId;
      std::vector<std::string> accountIds;
      int loadState;
      bool staged;
      bool isWO;
   };

   struct AddressData
   {
      struct Comparator
      {
         using is_transparent = void;
         bool operator()(const AddressData& lhs, const AddressData& rhs) const
         {
            return lhs.hash < rhs.hash;
         }
         bool operator()(const BinaryData& lhs, const AddressData rhs) const {
            return lhs < rhs.hash;
         }
         bool operator()(const AddressData& lhs, const BinaryData& rhs) const {
            return lhs.hash < rhs;
         }
      };

      const int32_t index;
      const BinaryData hash;
      const std::string addrStr;
      const bool isUsed;
      const uint32_t type;

      bool operator<(const AddressData& rhs) const
      {
         return hash < rhs.hash;
      }
   };

   struct WalletData
   {
      const std::string walletId;
      const std::string accountId;
      const std::string masterId;
      const std::string dbId;

      const std::string label;
      const std::string desc;

      const bool encrypted;
      const bool watchingOnly;
      const std::set<AddressData, AddressData::Comparator> addresses;
      const int64_t lookup;
      const int64_t useCount;

      const std::filesystem::path path;
      const uint32_t kdfMemReq;
   };

   extern std::filesystem::path fullBinPath;
   AddressData capnToAddressData(const Codec::Bridge::WalletData::AddressData::Reader& capnAddr);
   WalletData capnToWalletData(const Codec::Bridge::WalletData::Reader& capnWlt);

   std::vector<Ledgers::Entry> capnToLedgers(
      const Codec::Types::TxLedger::Reader& capnLedgers);

   std::vector<std::vector<Ledgers::Entry>> capnToLedgers(
      const capnp::List<Codec::Types::TxLedger, capnp::Kind::STRUCT>::Reader& capnLedgers);

   bool checkBDVRequestType(
      const Codec::BDV::Request::Reader& reader,
      Codec::BDV::BdvRequest::Which which);

   bool checkStaticRequestType(
      const Codec::BDV::Request::Reader& reader,
      Codec::BDV::StaticRequest::Which which);

   /////////////////////////////////////////////////////////////////////////////
   // bridge client side mocked comms layer
   extern std::mutex commsMutex;
   extern std::deque<MsgPtr> replyQueue;
   extern std::condition_variable commsCV;
   MsgPtr waitOnReply();
   MsgPtr waitOnReplyFor(std::chrono::milliseconds timeout);

   void pushRequest(std::shared_ptr<Bridge::CppBridge> bridge,
      const BinaryData& rawRequest);

   void replyToKeyPrompt(std::shared_ptr<Bridge::CppBridge> bridge,
      uint64_t refId, uint32_t counter, bool accept);

   struct TestBridgeSocket : public Network::PersistentSocket
   {
      TestBridgeSocket() :
         PersistentSocket("127.0.0.1", 50000, "test_bridge_socket")
      {}

      SocketType type() const override
      {
         return SocketType::CppBridge;
      }

      void pushPayload(
         std::unique_ptr<Network::Socket_WritePayload> payload,
         std::shared_ptr<Network::Socket_ReadPayload>) override
      {
         auto payloadPtr = dynamic_cast<Bridge::WritePayload_Bridge*>(payload.get());
         if (payloadPtr == nullptr) {
            throw std::runtime_error("could not read the push payload");
         }
         auto msgPtr = std::unique_ptr<Bridge::WritePayload_Bridge>(payloadPtr);
         payload.release();

         std::unique_lock<std::mutex> lock(commsMutex);
         replyQueue.emplace_back(std::move(msgPtr));
         commsCV.notify_all();
      }
   };

   //a TestBridgeSocket that can be muted, to keep a bridge quiet once a
   //second one takes over. muted is guarded by commsMutex, flip it with the
   //reply queue lock held
   struct MutableTestBridgeSocket : public TestBridgeSocket
   {
      bool muted = false;

      void pushPayload(
         std::unique_ptr<Network::Socket_WritePayload> payload,
         std::shared_ptr<Network::Socket_ReadPayload>) override
      {
         auto payloadPtr = dynamic_cast<Bridge::WritePayload_Bridge*>(payload.get());
         if (payloadPtr == nullptr) {
            throw std::runtime_error("could not read the push payload");
         }
         auto msgPtr = std::unique_ptr<Bridge::WritePayload_Bridge>(payloadPtr);
         payload.release();

         std::unique_lock<std::mutex> lock(commsMutex);
         if (muted) {
            return;
         }
         replyQueue.emplace_back(std::move(msgPtr));
         commsCV.notify_all();
      }
   };

   std::string createWOWallet(
      const std::filesystem::path& homedir,
      const std::vector<SecureBinaryData>& pubkeys,
      const std::vector<BinaryData>& scrAddrs = {});

   std::string createWallet(const std::filesystem::path& homedir);

   std::string createImportWallet(
      const std::filesystem::path& homedir,
      const std::vector<BinaryData>& privateKeys);

   WalletData progressWalletCreation(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& callbackId, const std::string& passphrase,
      std::chrono::milliseconds targetMs, uint32_t targetMB, int lookup,
      bool WO=false, bool isBIP32=false, bool isRestore=false);

   std::string createAWallet(std::shared_ptr<Bridge::CppBridge> bridge,
      std::chrono::milliseconds targetMs, uint32_t lookup, bool isBIP32);

   std::vector<std::string> getWalletBackup(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& walletId,
      const std::string& passphrase, Codec::Bridge::WalletBackup::Type backupType);

   WalletData restoreWallet(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::vector<std::string>& lines,
      const std::string& expectedWltId, Codec::Bridge::WalletBackup::Type backupType,
      const std::string& passphrase, std::chrono::milliseconds targetMs, uint32_t targetMB,
      bool merge, unsigned expectedLookup);

   std::map<std::string, WalletData> loadWallets(
      std::shared_ptr<Bridge::CppBridge> bridge);

   std::map<std::string, WltListEntry> listWallets(
      std::shared_ptr<Bridge::CppBridge> bridge);

   std::set<std::string> getWalletAccountIds(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId);

   WalletData getWalletData(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId, const std::string& accId);

   bool checkHasImports(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId);

   bool changeWalletPassphrase(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId,
      const std::string& currentPass, const std::string& newPass);


   //-----------------------------------------------------------------------
   // exportKeys helpers
   //-----------------------------------------------------------------------
   constexpr uint8_t PROTO_ASSETID_PREFIX = 0xAF;

   struct ExportedKey {
      BinaryData assetId;
      BinaryData privKey;
      BinaryData pubKey;
      std::string addressString;
      uint32_t addrType = 0;
      int32_t index = 0;
      std::string accountId;
      bool isUsed = false;
      std::string chainRole;
   };

   std::string chainRoleForAssetAccount(
      const std::shared_ptr<Accounts::AddressAccount>& accPtr,
      const Wallets::AssetAccountId& assetAccId);

   struct ExportResult {
      bool success = false;
      std::string error;
      std::vector<ExportedKey> keys;
   };

   ExportedKey parseCapnExportedKey(
      const Codec::Bridge::WalletReply::ExportedPrivateKey::Reader& k);

   void fillExpectedExportMetadata(ExportedKey& key,
      const std::shared_ptr<Accounts::AddressAccount>& accPtr,
      const Wallets::AssetId& assetID);

   std::pair<size_t, std::map<BinaryData, ExportedKey>> buildExpectedExportKeys(
      const Wallets::AssetWallet_Single& assetWlt);

   void fillExpectedPrivateKeys(
      Wallets::AssetWallet_Single& assetWlt,
      const SecureBinaryData& privPass,
      std::map<BinaryData, ExportedKey>& expectedKeys);

   void expectExportedKeyMatchesExpected(const ExportedKey& exported,
      const ExportedKey& expected);

   void verifyExportSetMatchesExpected(
      const std::vector<ExportedKey>& exported,
      const std::map<BinaryData, ExportedKey>& expected,
      bool requirePrivateKeys = false);

   ExportResult exportKeys(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId,
      bool publicOnly,
      const std::string& passphrase = {},
      bool provideCorrectPass = true,
      const std::string& accountId = {});

   ExportResult exportPrivateKeys(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId,
      const std::string& passphrase,
      bool provideCorrectPass = true,
      const std::string& accountId = {});

   ExportResult exportPublicKeys(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId,
      const std::string& accountId = {});

   WalletData extendAddressPool(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId, const std::string& accountId,
      const std::string& dbId, unsigned count, bool isNew);

   AddressData getAddress(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId,
      const std::string& accountId,
      uint32_t addressType=0);

   bool stageWallet(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId,
      bool stage);

   bool waitOnConnection(Bridge::MessageId refId);

   bool connectToIp(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& ip, Network::port_t port,
      const std::string& expectedPubkey);

   bool connectToPeer(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& peerKey);

   int automateDb(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::filesystem::path& satoshiDir,
      const std::filesystem::path& dbDir);

   bool registerWallets(std::shared_ptr<Bridge::CppBridge> bridge);

   bool registerWallet(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId, const std::string& accountId,
      const std::string& dbId);

   int goOnline(std::shared_ptr<Bridge::CppBridge> bridge, bool preReg = false);
   int waitOnNewBlock();

   std::vector<uint64_t> getWalletBalance(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& wltId, const std::string& accId);

   std::map<BinaryData, std::vector<uint64_t>> getAddrBalances(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& wltId, const std::string& accId);

   std::string getLedgerDelegateId(std::shared_ptr<Bridge::CppBridge> bridge);

   std::string getLedgerDelegateIdForWallet(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& wltId, const std::string& accId);

   std::string getLedgerDelegateIdForScrAddr(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& wltId, const std::string& accId,
      const BinaryData& scrAddr);

   size_t getLedgersPageCount(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& delegateId);

   std::vector<Ledgers::Entry> getLedgersPage(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& delegateId, uint32_t pageId);

   bool checkLedgers(const Ledgers::Entry& lhs, const TestChain::LedgerEntryValue& rhs);

   void checkBalances(
      const std::map<BinaryData, std::vector<uint64_t>>& balances,
      uint32_t height, bool reorg);

   void printLedgers(const std::vector<Ledgers::Entry>& vec);

   std::map<BinaryData, std::set<BinaryData>> getAddressBook(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId, const std::string& accountId);

   const std::vector<UTXO> getUTXOs(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId, const std::string& accountId,
      int mode);

   const std::string getNewCoinSelector(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId, const std::string& accountId,
      uint32_t height);

   bool cleanupCoinSelectionInstance(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& csId);

   bool setNewCsRecipient(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& csId, unsigned recId,
      const std::string& addr, uint64_t amount);

   bool selectUtxos(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& csId, uint32_t flags, float feeByte);

   bool processCustomUtxoList(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& csId, const std::vector<UTXO>& utxos,
      uint32_t flags, uint64_t flatFee);

   std::vector<UTXO> getUtxoSelection(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& csId);

   uint64_t getFlatFee(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& csId);

   std::string getNewSigner(std::shared_ptr<Bridge::CppBridge> bridge);

   bool setSignerVersion(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      uint32_t version);

   std::map<BinaryData, BinaryData> getTxsByHash(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::set<BinaryData>& hashSet);

   struct FetchedTx
   {
      BinaryData raw;
      uint32_t height;
      uint32_t txIndex;
      uint32_t timestamp;

      bool operator==(const FetchedTx&) const = default;
   };

   std::map<BinaryData, FetchedTx> getTxDataByHash(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::set<BinaryData>& hashSet, bool& success);

   bool loadSignerString(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& signerId, const std::string& str);

   bool addSpenderByOutpoint(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      const BinaryData& hash, uint32_t index, uint32_t sequence);

   bool populateUtxo(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      const BinaryData& hash, uint32_t index,
      uint64_t value, const BinaryData& script);

   bool addSupportingTx(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      const BinaryData& rawTx);

   bool addSignerRecipient(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      const BinaryData& script, uint64_t value);

   bool resolveSigner(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      const std::string& walletId);

   bool signTx(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      const std::string& walletId, const std::string& passphrase);

   struct InputState
   {
      const bool isValid = false;
      const uint32_t sigCount = UINT32_MAX;
   };

   InputState getInputState(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId,
      uint32_t inputId);

   BinaryData getSignedTx(
      std::shared_ptr<Bridge::CppBridge> bridge, const std::string& signerId);

   bool cleanupSigner(std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& signerId);

   BinaryData createAndSignTx(
      std::shared_ptr<Bridge::CppBridge> bridge,
      const std::string& walletId, const std::string& accountId,
      const std::vector<UTXO>& utxos,
      const std::map<std::string, uint64_t>& recipients,
      const BinaryData& changeScrAddr, uint64_t fee, bool rbf,
      const std::string& passphrase);

   void broadcastTx(std::shared_ptr<Bridge::CppBridge> bridge,
      const BinaryData& rawTx, bool viaRpc = false);

   std::vector<Ledgers::Entry> waitOnZc();
   std::vector<BinaryData> waitOnInvalidatedZCs();
}

using namespace BridgeTestUtils;
