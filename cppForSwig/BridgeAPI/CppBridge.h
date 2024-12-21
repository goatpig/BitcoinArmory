////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _CPPBRIDGE_H
#define _CPPBRIDGE_H

#include "../ArmoryConfig.h"
#include "WalletManager.h"
#include "btc/ecc.h"

namespace AsyncClient
{
   class BlockDataViewer;
   class LedgerDelegate;
}

namespace Armory
{
   namespace Seeds
   {
      struct PromptReply;
   }

   namespace Bridge
   {
      struct ServerPushWrapper;
      struct WritePayload_Bridge;

      //////////////////////////////////////////////////////////////////////////
      typedef std::function<void(BinaryData&)> notifLbd;
      using MessageId = uint64_t;

      ////
      class BridgeCallback : public RemoteCallback
      {
      private:
         std::shared_ptr<WalletManager> wltManager_;

         //to push packets to the gui
         notifLbd pushNotifLbd_;

         //id members
         Armory::Threading::BlockingQueue<std::string> idQueue_;
         std::set<std::string> validIds_;
         std::mutex idMutex_;

      public:
         BridgeCallback(
            std::shared_ptr<WalletManager> mgr, const notifLbd& lbd) :
            RemoteCallback(), wltManager_(mgr), pushNotifLbd_(lbd)
         {}

         //virtuals
         void run(BdmNotification) override;

         void progress(
            BDMPhase phase,
            const std::vector<std::string> &walletIdVec,
            float progress, unsigned secondsRem,
            unsigned progressNumeric
         ) override;

         void disconnected(void) override;

         //local notifications
         void notify_SetupDone(void);
         void notify_RegistrationDone(const std::set<std::string>&);
         void notify_SetupRegistrationDone(const std::set<std::string>&);
         void notify_NewBlock(unsigned);
         void notify_Ready(unsigned);

         //
         void waitOnId(const std::string&);
      };

      //////////////////////////////////////////////////////////////////////////
      using WalletPtr = std::shared_ptr<Armory::Wallets::AssetWallet>;
      class CppBridgeSignerStruct
      {
      private:
         std::unique_ptr<Armory::Signing::TxEvalState> signState_{};
         const std::function<WalletPtr(const std::string&)> getWalletFunc_;
         const std::function<void(ServerPushWrapper)> writeFunc_;

      public:
         Armory::Signing::Signer signer_{};

      public:
         CppBridgeSignerStruct(std::function<WalletPtr(const std::string&)>,
            std::function<void(ServerPushWrapper)>);

         void signTx(const std::string&, const std::string&, MessageId);
         bool resolve(const std::string&);
         BinaryData getSignedStateForInput(unsigned, MessageId);
      };

      //////////////////////////////////////////////////////////////////////////
      using CallbackHandler = std::function<bool(const Seeds::PromptReply&)>;

      class CppBridge
      {
      private:
         const std::filesystem::path path_;

         const std::string dbAddr_;
         const std::string dbPort_;

         std::shared_ptr<WalletManager> wltManager_;
         std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr_;

         std::shared_ptr<BridgeCallback> callbackPtr_;

         std::map<std::string, AsyncClient::LedgerDelegate> delegateMap_;
         std::map<std::string,
            std::shared_ptr<CoinSelection::CoinSelectionInstance>> csMap_;
         std::map<std::string,
            std::shared_ptr<CppBridgeSignerStruct>> signerMap_;

         PRNG_Fortuna fortuna_;

         std::function<void(std::unique_ptr<WritePayload_Bridge>)> writeLambda_;

         const bool dbOneWayAuth_;
         const bool dbOffline_;

         std::mutex callbackHandlerMu_;
         std::map<uint32_t, CallbackHandler> callbackHandlers_;

      public:
         std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr(void) const;
         void reset(void);

      public:
         //wallet setup
         void loadWallets(const std::string&, MessageId);
         BinaryData createWalletsPacket(MessageId);
         bool deleteWallet(const std::string&);
         BinaryData getWalletPacket(const std::string&, MessageId) const;

         //AsyncClient::BlockDataViewer setup
         void setupDB(void);
         void registerWallets(void);
         void registerWallet(const std::string&, bool isNew);

         BinaryData getNodeStatus(MessageId);

         //balance and counts
         BinaryData getBalanceAndCount(const std::string&, MessageId);
         BinaryData getAddrCombinedList(const std::string&, MessageId);
         BinaryData getHighestUsedIndex(const std::string&, MessageId);

         //wallet & addresses
         void extendAddressPool(const std::string&, unsigned,
            const std::string&, MessageId);
         BinaryData getAddress(const std::string&, uint32_t, uint32_t, MessageId);
         WalletAccountIdentifier createWallet(uint32_t,
            const std::string&, const std::string&,
            const SecureBinaryData&, const SecureBinaryData&,
            const SecureBinaryData&);
         void createBackupStringForWallet(const std::string&,
            const std::string&, MessageId);
         void restoreWallet(
            const std::vector<std::string_view>&,
            const std::string_view&,
            const std::string_view&);

         //ledgers
         const std::string& getLedgerDelegateId(void);
         const std::string& getLedgerDelegateIdForWallet(const std::string&);
         const std::string& getLedgerDelegateIdForScrAddr(
            const std::string&, const BinaryDataRef&);
         void getHistoryPageForDelegate(const std::string&,
            unsigned, unsigned, MessageId);
         void createAddressBook(const std::string&, MessageId);
         void setComment(const std::string&,
            const std::string&, const std::string&);
         void setWalletLabels(const std::string&,
            const std::string&, const std::string&);

         //txs & headers
         void getTxsByHash(const std::set<BinaryData>&, MessageId);
         void getHeadersByHeight(const std::vector<unsigned>&, MessageId);

         //utxos
         void getUTXOs(const std::string&, uint64_t, bool, bool, MessageId);

         //coin selection
         void setupNewCoinSelectionInstance(
            const std::string&, unsigned, MessageId);
         void destroyCoinSelectionInstance(const std::string&);
         std::shared_ptr<CoinSelection::CoinSelectionInstance>
            coinSelectionInstance(const std::string&) const;

         //signer
         BinaryData initNewSigner(MessageId);
         void destroySigner(const std::string&);
         std::shared_ptr<CppBridgeSignerStruct> signerInstance(
            const std::string&) const;
         WalletPtr getWalletPtr(const std::string&) const;

         //script utils
         BinaryData getTxInScriptType(
            const BinaryData&, const BinaryData&, MessageId) const;
         BinaryData getTxOutScriptType(const BinaryData&, MessageId) const;
         BinaryData getScrAddrForScript(const BinaryData&, MessageId) const;
         BinaryData getScrAddrForAddrStr(const std::string&, MessageId) const;
         BinaryData getLastPushDataInScript(const BinaryData&, MessageId) const;

         //utils
         BinaryData getHash160(const BinaryDataRef&, MessageId) const;
         void broadcastTx(const std::vector<BinaryData>&);
         BinaryData getTxOutScriptForScrAddr(const BinaryData&, MessageId) const;
         BinaryData getAddrStrForScrAddr(const BinaryData&, MessageId) const;
         std::string getNameForAddrType(int) const;
         BinaryData setAddressTypeFor(const std::string&, const BinaryDataRef&,
            uint32_t, MessageId) const;
         void getBlockTimeByHeight(uint32_t, MessageId) const;
         void getFeeSchedule(const std::string&, MessageId) const;

         //custom callback handlers
         void callbackWriter(ServerPushWrapper&);
         void setCallbackHandler(ServerPushWrapper&);
         CallbackHandler getCallbackHandler(uint32_t);

         SecureBinaryData generateRandom(size_t) const;

      public:
         CppBridge(const std::filesystem::path&, const std::string&,
            const std::string&, bool, bool);

         void writeToClient(BinaryData&) const;
         void setWriteLambda(std::function<void(std::unique_ptr<WritePayload_Bridge>)> lbd)
         {
            writeLambda_ = lbd;
         }
      };
   }; //namespace Bridge
}; //namespace Armory

#endif