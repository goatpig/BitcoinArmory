////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <set>
#include <map>
#include <string>
#include <mutex>
#include <functional>
#include <filesystem>

#include <Utils/Types.h>

class BinaryData;
class BinaryDataRef;
class SecureBinaryData;

namespace AsyncClient
{
   class BlockDataViewer;
}

namespace Armory
{
   namespace Seeds
   {
      struct PromptReply;
      enum class SeedType : int;
   };

   namespace Signing
   {
      class Signer;
      class TxEvalState;
   }

   namespace CoinSelection
   {
      class CoinSelectionInstance;
   }

   namespace Wallets
   {
      class WalletId;
      class AddressAccountId;
      class AssetWallet;

      namespace IO
      {
         struct CreateWalletParams;
      }
   }

   namespace NetworkPeers
   {
      class ClientStore;
   }

   namespace Bridge
   {
      struct ServerPushWrapper;
      struct WritePayload_Bridge;
      class WalletManager;
      class AutomationContext;

      ////
      using MessageId = uint64_t;
      using CallbackId = std::string;

      //////////////////////////////////////////////////////////////////////////
      using WalletPtr = std::shared_ptr<Armory::Wallets::AssetWallet>;
      class CppBridgeSignerStruct
      {
      private:
         const std::function<WalletPtr(const Wallets::WalletId&)> getWalletFunc_;
         const std::function<void(ServerPushWrapper)> writeFunc_;

      public:
         std::unique_ptr<Signing::Signer> signer;

      public:
         CppBridgeSignerStruct(std::function<WalletPtr(const Wallets::WalletId&)>,
            std::function<void(ServerPushWrapper)>);

         void signTx(const Wallets::WalletId&, const CallbackId&, MessageId);
         bool resolve(const Wallets::WalletId&);
         BinaryData getSignedStateForInput(unsigned, MessageId);
      };

      //////////////////////////////////////////////////////////////////////////
      using CallbackHandler = std::function<bool(const Seeds::PromptReply&)>;

      class CppBridge
      {
      private:
         //datadir
         const std::filesystem::path path_;

         //armorydb stuff
         const bool dbOffline_;
         std::shared_ptr<NetworkPeers::ClientStore> peersDb_;

         //to write to the bridge client
         std::function<void(std::unique_ptr<WritePayload_Bridge>)> writeLambda_;

         //these objects are the core of the bridge
         std::shared_ptr<WalletManager> wltManager_;
         std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr_;

         //various states cache
         std::map<std::string,
            std::shared_ptr<CoinSelection::CoinSelectionInstance>> csMap_;
         std::map<std::string,
            std::shared_ptr<CppBridgeSignerStruct>> signerMap_;

         //UI related ad hoc callbacks
         std::mutex callbackHandlerMu_;
         std::map<uint32_t, CallbackHandler> callbackHandlers_;

         //binary automation
         std::unique_ptr<AutomationContext> automationContext_;

      private:
         void reset(void);

      public:
         std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr(void) const;

         //wallet manager init methods
         BinaryData listWallets(MessageId);
         void unlockControlHeader(const std::string&, const CallbackId&,
            MessageId);
         bool stageWallet(const Wallets::WalletId&, bool);
         void migrateWallet(const std::filesystem::path&,
            const CallbackId&, MessageId);
         BinaryData loadWallets(MessageId);

         //wallets
         const std::filesystem::path& getDataDir(void) const;
         BinaryData createWalletsPacket(MessageId);
         bool unloadWallet(const Wallets::WalletId&);
         bool deleteWallet(const Wallets::WalletId&);
         BinaryData getAccountIds(const Wallets::WalletId&, MessageId) const;
         BinaryData getWalletPacket(const Wallets::WalletId&,
            Wallets::AddressAccountId, MessageId) const;

         //binary automation
         void setAutomationContext(std::unique_ptr<AutomationContext>);
         void runAutomationContext(CallbackId, MessageId);
         void cleanupAutomationContext(CallbackId, MessageId);

         //db setup
         void connectToIp(const std::string&, const std::string&,
            const CallbackId&, MessageId);
         void connectToPeer(const std::string&, MessageId);
         void beginDbSession(void);
         bool isDbRunning(void);

         //peers db
         void loadPeersDb(const CallbackId&, MessageId);
         void listPeers(MessageId);
         void addPeer(const std::string&,
            std::vector<std::string>&, const std::string&, MessageId);
         void removePeer(const std::string&, MessageId);
         void setPeerLabel(const std::string&, const std::string&, MessageId);

         //wallet registration
         void registerWallets(void);
         void registerWallet(const Wallets::WalletId&,
            const Wallets::AddressAccountId&, bool isNew);
         BinaryData getNodeStatus(MessageId);

         //balance and counts
         BinaryData getBalanceAndCount(const Wallets::WalletId&,
            const Wallets::AddressAccountId&, MessageId);
         BinaryData getAddrCombinedList(const Wallets::WalletId&,
            const Wallets::AddressAccountId&, MessageId);

         //create/generate wallet & addresses
         void extendAddressPool(const Wallets::WalletId&,
            const Wallets::AddressAccountId&, unsigned, bool,
            const CallbackId&, MessageId);
         void getAddress(const Wallets::WalletId&,
            const Wallets::AddressAccountId&, uint32_t,
            uint32_t, MessageId);
         void createWallet(
            Seeds::SeedType,
            SecureBinaryData, //extra entropy
            Wallets::IO::CreateWalletParams,
            const CallbackId&,
            MessageId);
         void createBackupStringForWallet(const Wallets::WalletId&,
            bool, const CallbackId&, MessageId);
         void changeWalletPassphrase(const Wallets::WalletId&,
            const CallbackId&, bool, MessageId);
         void restoreWallet(
            const std::vector<std::string_view>&,
            const std::string_view&,
            const CallbackId&, MessageId);
         void importWallet(const std::filesystem::path&, MessageId);
         void forkWatchingOnly(const Wallets::WalletId&,
            const CallbackId&, MessageId);
         void exportKeys(const Wallets::WalletId&,
            const Wallets::AddressAccountId&, bool includePrivateKeys,
            const CallbackId& callbackId, MessageId);

         //ledgers
         const std::string& getLedgerDelegateId(void);
         const std::string& getLedgerDelegateIdForWallet(
            const Wallets::WalletId&, const Wallets::AddressAccountId&);
         const std::string& getLedgerDelegateIdForScrAddr(
            const Wallets::WalletId&, const Wallets::AddressAccountId&,
            const BinaryDataRef&);
         void getPageCountForDelegate(const std::string&, MessageId);
         void getHistoryPageForDelegate(const std::string&,
            unsigned, unsigned, MessageId);
         void createAddressBook(const Wallets::WalletId&,
            const Wallets::AddressAccountId&, MessageId);
         void setComment(const Wallets::WalletId&,
            const std::string&, const std::string&);
         void setWalletLabels(const Wallets::WalletId&,
            const std::string&, const std::string&);
         void updateWalletsLedgerFilter(const std::map<Wallets::WalletId,
            std::set<Wallets::AddressAccountId>>&);

         //tx stuff
         void getTxsByHash(const std::set<Types::TxHash>&, MessageId);
         void getUTXOs(const Wallets::WalletId&,
            const Wallets::AddressAccountId&,
            uint64_t, bool, bool, MessageId);

         //coin selection
         void setupNewCoinSelectionInstance(const Wallets::WalletId&,
            const Wallets::AddressAccountId&, unsigned, MessageId);
         void destroyCoinSelectionInstance(const std::string&);
         std::shared_ptr<CoinSelection::CoinSelectionInstance>
            coinSelectionInstance(const std::string&) const;

         //signer
         BinaryData initNewSigner(MessageId);
         void destroySigner(const std::string&);
         std::shared_ptr<CppBridgeSignerStruct> signerInstance(
            const std::string&) const;
         WalletPtr getWalletPtr(const Wallets::WalletId&) const;

         //script utils
         BinaryData getTxInScriptType(
            const BinaryData&, const BinaryData&, MessageId) const;
         BinaryData getTxOutScriptType(const BinaryData&, MessageId) const;
         BinaryData getScrAddrForScript(const BinaryData&, MessageId) const;
         BinaryData getScrAddrForAddrStr(const std::string&, MessageId) const;
         BinaryData getLastPushDataInScript(const BinaryData&, MessageId) const;

         //utils
         BinaryData getHash160(const BinaryDataRef&, MessageId) const;
         void broadcastTxs(const std::vector<BinaryData>&, bool);
         BinaryData getTxOutScriptForScrAddr(const BinaryData&, MessageId) const;
         BinaryData getAddrStrForScrAddr(const BinaryData&, MessageId) const;
         std::string getNameForAddrType(int) const;
         BinaryData setAddressTypeFor(const Wallets::WalletId&,
            const Wallets::AddressAccountId&, const BinaryDataRef&,
            uint32_t, MessageId) const;
         void getBlockTimeByHeight(uint32_t, MessageId) const;
         void getFeeSchedule(const std::string&, MessageId) const;

         //custom callback handlers
         void callbackWriter(ServerPushWrapper&);
         void setCallbackHandler(ServerPushWrapper&);
         CallbackHandler getCallbackHandler(uint32_t);

         //sanity checks
         bool isOffline(void) const;

         //wallet misc
         void getUnlockTime(const Wallets::WalletId&, MessageId) const;
         bool doesWalletHaveImports(const Wallets::WalletId&) const;

      public:
         CppBridge(void);
         ~CppBridge(void);

         void disconnectFromDb(void);
         void writeToClient(BinaryData&) const;
         void setWriteLambda(
            const std::function<void(std::unique_ptr<WritePayload_Bridge>)>&);
      };
   } //namespace Bridge
} //namespace Armory
