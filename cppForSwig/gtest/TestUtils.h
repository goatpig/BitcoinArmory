////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _TEST_UTILS_H
#define _TEST_UTILS_H

#include <limits.h>
#include <iostream>
#include <stdlib.h>
#include <stdint.h>
#include <thread>
#include <gtest/gtest.h>
#include <btc/ecc.h>

#include <Utils/log.h>
#include <Utils/ArmoryErrors.h>
#include <Utils/BinaryData.h>
#include <Utils/BtcUtils.h>
#include <Utils/Cryptography.h>
#include <Utils/BitcoinSettings.h>

#include <Signer/Script.h>
#include <Signer/Signer.h>
#include <Signer/ResolverFeed_Wallets.h>

#include <BlockchainDatabase/BlockObj.h>
#include <BlockchainDatabase/lmdb_wrapper.h>
#include <BlockchainDatabase/BlockUtils.h>
#include <BlockchainDatabase/txio.h>
#include <BlockchainDatabase/StoredBlockObj.h>
#include <AsyncClient.h>
#include <ScrAddrObj.h>
#include <BtcWallet.h>
#include <BlockDataViewer.h>
#include <BitcoinP2P.h>

#include <Progress.h>
#include <BDM_Server.h>
#include <TxClasses.h>
#include <bdmenums.h>
#include <Wallets/Wallets.h>
#include <Wallets/BIP32_Node.h>

#include "MockedNode.h"

#define HASH160PREFIX WRITE_UINT8_LE((uint8_t)Armory::ScriptPrefix::HASH160)

namespace Armory
{
   namespace Assets
   {
      class AssetEntry;
   };
};

class BlockDataManagerThread;

template<class T, typename ...Args>
static BinaryData serializeDBValue(const T &o, const Args &...a)
{
   BinaryWriter wr;
   o.serializeDBValue(wr, a...);
   return wr.getData();
}

namespace TestUtils
{
   const std::filesystem::path dataDir("../reorgTest");

   // This function assumes src to be a zero terminated sanitized string with
   // an even number of [0-9a-f] characters, and target to be sufficiently large
   void hex2bin(const char* src, unsigned char* target);

   int char2int(char input);

   bool searchFile(const std::filesystem::path& filename, BinaryData& data);
   uint32_t getTopBlockHeightInDB(BlockDataManager &bdm, DB_SELECT db);
   uint64_t getDBBalanceForHash160(
      BlockDataManager &bdm, BinaryDataRef addr160);

   void concatFile(const std::vector<std::filesystem::path> &from,
      const std::filesystem::path &to);
   void appendBlocks(const std::vector<std::string> &files,
      const std::filesystem::path &to);
   void setBlocks(const std::vector<std::string> &files,
      const std::filesystem::path &to);
   void nullProgress(unsigned, double, unsigned, unsigned);
   BinaryData getTx(unsigned height, unsigned id);

   std::shared_ptr<Armory::Assets::AssetEntry> getMainAccountAssetForIndex(
      std::shared_ptr<Armory::Wallets::AssetWallet>, Armory::Wallets::AssetKeyType);
   size_t getMainAccountAssetCount(std::shared_ptr<Armory::Wallets::AssetWallet>);
}

namespace DBTestUtils
{
   extern unsigned commandCtr_;
   extern std::deque<unsigned> zcDelays_;

   void init(void);

   unsigned getTopBlockHeight(LMDBBlockDatabase*, DB_SELECT);
   BinaryData getTopBlockHash(LMDBBlockDatabase*, DB_SELECT);

   BdvIdKey registerBDV(Clients*, const BinaryData& magic_word);
   void goOnline(Clients*, BdvIdKey);
   const std::shared_ptr<BDV_Server_Object> getBDV(Clients*, BdvIdKey);
   
   void registerWallet(Clients*, BdvIdKey,
      const std::vector<BinaryData>& scrAddrs, const std::string& wltName,
      bool isLockbox, bool waitOnReg);

   std::vector<uint64_t> getBalanceAndCount(Clients*,
      BdvIdKey, const std::string&, unsigned);
   std::string getLedgerDelegate(Clients*, BdvIdKey);
   std::vector<DBClientClasses::LedgerEntry> getHistoryPage(
      Clients*, BdvIdKey, const std::string&, uint32_t);

   std::tuple<BinaryData, unsigned> waitOnSignal(
      Clients*, BdvIdKey, int signal);
   void waitOnBDMSignal(std::shared_ptr<BlockDataManager>, BDV_Action);
   void waitOnBDMReady(Clients*, BdvIdKey);
   void waitOnBDMError(std::shared_ptr<BlockDataManager>);

   std::tuple<BinaryData, unsigned> waitOnNewBlockSignal(Clients*, BdvIdKey);
   std::pair<std::vector<TxIOPair>, std::set<BinaryData>>
      waitOnNewZcSignal(Clients*, BdvIdKey);
   void waitOnWalletRefresh(Clients*, BdvIdKey, const std::string&);
   void triggerNewBlockNotification(BlockDataManagerThread*);
   void mineNewBlock(BlockDataManagerThread*, const BinaryData& h160,
      unsigned count);

   struct ZcVector
   {
      std::vector<std::pair<Tx, unsigned>> zcVec_;

      void push_back(BinaryData rawZc, unsigned zcTime, unsigned blocksToMine = 0)
      {
         Tx zctx(rawZc);
         zctx.setTxTime(zcTime);

         zcVec_.push_back(std::move(std::make_pair(zctx, blocksToMine)));
      }

      void clear(void) { zcVec_.clear(); }
   };

   void pushNewZc(BlockDataManagerThread*, const ZcVector&, bool stage = false);
   void setNextZcPushDelay(unsigned);
   std::pair<BinaryData, BinaryData> getAddrAndPubKeyFromPrivKey(
      BinaryData privKey, bool compressed = false);

   Tx getTxByHash(Clients*, BdvIdKey, const BinaryData&);
   Tx getTxByKey(Clients*, BdvIdKey, const BinaryData&);
   std::vector<UTXO> getUtxoForAddress(Clients*, BdvIdKey, const BinaryData&, bool);

   void addTxioToSsh(StoredScriptHistory&,
      const std::map<BinaryDataRef, std::shared_ptr<const TxIOPair>>&);
   void prettyPrintSsh(StoredScriptHistory& ssh);
   Armory::Ledgers::Entry getLedgerEntryFromWallet(std::shared_ptr<BtcWallet>, const BinaryData&);
   Armory::Ledgers::Entry getLedgerEntryFromAddr(ScrAddrObj*, const BinaryData&);
   void updateWalletsLedgerFilter(
      Clients*, BdvIdKey, const std::vector<std::string> &);

   BinaryData processCommand(Clients*, BdvIdKey, BinaryData);

   /////////////////////////////////////////////////////////////////////////////
   AsyncClient::LedgerDelegate getLedgerDelegate(
      std::shared_ptr<AsyncClient::BlockDataViewer> bdv);
   AsyncClient::LedgerDelegate getLedgerDelegateForScrAddr(
      std::shared_ptr<AsyncClient::BlockDataViewer> bdv,
      const std::string& walletId, const BinaryData& scrAddr);
   
   std::vector<DBClientClasses::LedgerEntry> getHistoryPage(
      AsyncClient::LedgerDelegate& del, uint32_t id);
   uint64_t getPageCount(AsyncClient::LedgerDelegate& del);

   std::map<BinaryData, std::vector<uint64_t>> getAddrBalancesFromDB(
      std::shared_ptr<AsyncClient::BlockDataViewer>, const std::string&);

   std::vector<uint64_t> getBalancesAndCount(AsyncClient::BtcWallet& wlt,
      uint32_t blockheight);

   AsyncClient::TxResult getTxByHash(
      std::shared_ptr<AsyncClient::BlockDataViewer> bdv,
      const BinaryData& hash);

   std::vector<UTXO> getSpendableTxOutListForValue(
      AsyncClient::BtcWallet& wlt, uint64_t value);
   std::vector<UTXO> getSpendableZCList(AsyncClient::BtcWallet& wlt);

   /////////////////////////////////////////////////////////////////////////////
   std::vector<UnitTestBlock> getMinedBlocks(BlockDataManagerThread*);
   void setReorgBranchingPoint(BlockDataManagerThread*, const BinaryData&);

   /////////////////////////////////////////////////////////////////////////////
   class UTCallback : public RemoteCallback
   {
      struct BdmNotif
      {
         BDMAction action;
         std::set<std::string> idSet;
         std::vector<TxIOPair> txios;
         unsigned reorgHeight = UINT32_MAX;
         BDV_Error_Struct error;
         std::string requestID;
      };

   private:
      Armory::Threading::BlockingQueue<std::unique_ptr<BdmNotif>> actionStack_;
      std::deque<std::unique_ptr<BdmNotif>> actionDeque_;
      std::vector<BdmNotif> zcNotifVec_;

   public:
      UTCallback() : RemoteCallback()
      {}

      std::unique_ptr<BdmNotif> waitOnNotification(BDMAction actionType)
      {
         {
            auto iter = actionDeque_.begin();
            while (iter != actionDeque_.end()) {
               if ((*iter)->action == actionType) {
                  auto result = std::move(*iter);
                  actionDeque_.erase(iter);
                  return result;
               }
               ++iter;
            }
         }

         while (true) {
            auto action = std::move(actionStack_.pop_front());
            if (action->action == actionType) {
               return action;
            }
            actionDeque_.push_back(std::move(action));
         }
      }

      void run(BdmNotification);

      void progress(BDMPhase, const std::vector<std::string>&,
         float ,unsigned , unsigned)
      {}

      void disconnected()
      {}

      unsigned waitOnReorg(void)
      {
         while (1)
         {
            auto&& action = actionStack_.pop_front();
            if (action->action == BDMAction_NewBlock)
            {
               if (action->reorgHeight != UINT32_MAX)
                  return action->reorgHeight;
            }
         }
      }

      void waitOnSignal(BDMAction signal, std::string id = "")
      {
         while (true) {
            auto action = std::move(actionStack_.pop_front());
            if (action->action == signal) {
               if (!id.empty()) {
                  for (const auto& notifId : action->idSet) {
                     if (notifId == id) {
                        return;
                     }
                  }
               } else {
                  return;
               }
            }
         }
      }

      void waitOnManySignals(BDMAction signal, std::vector<std::string> ids)
      {
         std::set<std::string> idSet;
         for (auto& id : ids) {
            idSet.emplace(id);
         }
         unsigned count = 0;
         while (true) {
            if (count >= ids.size()) {
               break;
            }

            auto action = actionStack_.pop_front();
            if (action->action == signal) {
               for (auto& id : action->idSet) {
                  if (idSet.find(id) != idSet.end()) {
                     ++count;
                  }
               }
            }
         }
      }

      void waitOnZc(
         std::shared_ptr<Armory::ZeroConf::ZeroConfContainer>,
         const std::set<BinaryData>&);
      void waitOnZc_OutOfOrder(
         std::shared_ptr<Armory::ZeroConf::ZeroConfContainer>,
         const std::set<BinaryData>&);

      void waitOnError(const BinaryData& hash, ArmoryErrorCodes errorCode)
      {
         while (true) {
            auto action = waitOnNotification(BDMAction_BDV_Error);

            if (action->error.errData_ == hash &&
               action->error.errCode_ == (int)errorCode) {
               break;
            }
         }
      }

      void waitOnErrors(const std::map<BinaryData, ArmoryErrorCodes>& errorMap)
      {
         auto mapCopy = errorMap;
         while (true) {
            if (mapCopy.empty()) {
               return;
            }

            auto action = waitOnNotification(BDMAction_BDV_Error);
            auto iter = mapCopy.find(action->error.errData_);
            if (iter == mapCopy.end()) {
               continue;
            }
            if ((int)iter->second == action->error.errCode_) {
               mapCopy.erase(iter);
            }
         }
      }
   };
}

namespace ResolverUtils
{
   ////////////////////////////////////////////////////////////////////////////////
   struct TestResolverFeed : public Armory::Signing::ResolverFeed
   {
   private:
      std::map<BinaryData, BinaryData> hashToPreimage_;
      std::map<BinaryData, SecureBinaryData> pubKeyToPrivKey_;

      std::map<BinaryData, Armory::Signing::BIP32_AssetPath> bip32Paths_;

   public:
      BinaryData getByVal(const BinaryData& val) override
      {
         auto iter = hashToPreimage_.find(val);
         if (iter == hashToPreimage_.end())
            throw std::runtime_error("invalid value");

         return iter->second;
      }

      const SecureBinaryData& getPrivKeyForPubkey(const BinaryData& pubkey) override
      {
         auto iter = pubKeyToPrivKey_.find(pubkey);
         if (iter == pubKeyToPrivKey_.end())
            throw std::runtime_error("invalid pubkey");

         return iter->second;
      }

      void addPrivKey(const SecureBinaryData& key, bool compressed = false)
      {
         auto&& datapair = DBTestUtils::getAddrAndPubKeyFromPrivKey(key, compressed);
         hashToPreimage_.insert(datapair);
         pubKeyToPrivKey_[datapair.second] = key;
      }

      void addValPair(const BinaryData& key, const BinaryData& val)
      {
         hashToPreimage_.emplace(key, val);
      }

      Armory::Signing::BIP32_AssetPath resolveBip32PathForPubkey(
         const BinaryData& pubkey) override
      {
         auto iter = bip32Paths_.find(pubkey);
         if (iter == bip32Paths_.end())
            throw std::runtime_error("missing path");

         return iter->second;
      }

      void setBip32PathForPubkey(
         const BinaryData& pubkey, const Armory::Signing::BIP32_AssetPath& path)
      {
         bip32Paths_.emplace(pubkey, path);
      }
   };

   ////////////////////////////////////////////////////////////////////////////////
   class HybridFeed : public Armory::Signing::ResolverFeed
   {
   private:
      std::shared_ptr<Armory::Signing::ResolverFeed_AssetWalletSingle> feedPtr_;

   public:
      TestResolverFeed testFeed_;

   public:
      HybridFeed(std::shared_ptr<Armory::Wallets::AssetWallet_Single> wltPtr)
      {
         feedPtr_ = std::make_shared<
            Armory::Signing::ResolverFeed_AssetWalletSingle>(wltPtr);
      }

      BinaryData getByVal(const BinaryData& val) override
      {
         try
         {
            return testFeed_.getByVal(val);
         }
         catch (std::runtime_error&)
         {}

         return feedPtr_->getByVal(val);
      }

      const SecureBinaryData& getPrivKeyForPubkey(const BinaryData& pubkey) override
      {
         try
         {
            return testFeed_.getPrivKeyForPubkey(pubkey);
         }
         catch (std::runtime_error&)
         {}

         return feedPtr_->getPrivKeyForPubkey(pubkey);
      }

      Armory::Signing::BIP32_AssetPath resolveBip32PathForPubkey(const BinaryData&) override
      {
         throw std::runtime_error("invalid pubkey");
      }

      void setBip32PathForPubkey(const BinaryData&,
         const Armory::Signing::BIP32_AssetPath&) override
      {}
   };

   /////////////////////////////////////////////////////////////////////////////
   struct CustomFeed : public Armory::Signing::ResolverFeed
   {
      std::map<BinaryDataRef, BinaryDataRef> hash_to_preimage_;
      std::shared_ptr<ResolverFeed> wltFeed_;

   private:
      void addAddressEntry(std::shared_ptr<AddressEntry> addrPtr)
      {
         try
         {
            BinaryDataRef hash(addrPtr->getHash());
            BinaryDataRef preimage(addrPtr->getPreimage());
            hash_to_preimage_.insert(std::make_pair(hash, preimage));
         }
         catch (const std::exception&)
         {
            return;
         }

         auto addr_nested = std::dynamic_pointer_cast<AddressEntry_Nested>(addrPtr);
         if (addr_nested != nullptr)
            addAddressEntry(addr_nested->getPredecessor());
      }

   public:
      CustomFeed(std::shared_ptr<AddressEntry> addrPtr,
         std::shared_ptr<Armory::Wallets::AssetWallet_Single> wlt) :
         wltFeed_(std::make_shared<
            Armory::Signing::ResolverFeed_AssetWalletSingle>(wlt))
      {
         addAddressEntry(addrPtr);
      }

      CustomFeed(std::shared_ptr<AddressEntry> addrPtr,
         std::shared_ptr<Armory::Signing::ResolverFeed> feed) :
         wltFeed_(feed)
      {
         addAddressEntry(addrPtr);
      }

      BinaryData getByVal(const BinaryData& key) override
      {
         auto keyRef = BinaryDataRef(key);
         auto iter = hash_to_preimage_.find(keyRef);
         if (iter == hash_to_preimage_.end())
            throw std::runtime_error("invalid value");

         return iter->second;
      }

      const SecureBinaryData& getPrivKeyForPubkey(
         const BinaryData& pubkey) override
      {
         return wltFeed_->getPrivKeyForPubkey(pubkey);
      }

      Armory::Signing::BIP32_AssetPath resolveBip32PathForPubkey(
         const BinaryData&) override
      {
         throw std::runtime_error("invalid pubkey");
      }

      void setBip32PathForPubkey(
         const BinaryData&, const Armory::Signing::BIP32_AssetPath&) override
      {}
   };
}

#endif
