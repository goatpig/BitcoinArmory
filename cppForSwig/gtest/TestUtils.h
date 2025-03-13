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

#include "../log.h"
#include "../BinaryData.h"
#include "../BtcUtils.h"
#include "../BlockchainDatabase/BlockObj.h"
#include "../BlockchainDatabase/lmdb_wrapper.h"
#include "../BlockchainDatabase/BlockUtils.h"
#include "../BlockchainDatabase/txio.h"
#include "../BlockchainDatabase/StoredBlockObj.h"
#include "../PartialMerkle.h"
#include "../EncryptionUtils.h"
#include "../ScrAddrObj.h"
#include "../BtcWallet.h"
#include "../BlockDataViewer.h"

#include "../ArmoryErrors.h"
#include "../Progress.h"
#include "../reorgTest/blkdata.h"
#include "../BDM_Server.h"
#include "../TxClasses.h"
#include "../bdmenums.h"
#include "../Signer/Script.h"
#include "../Signer/Signer.h"
#include "../Signer/ResolverFeed_Wallets.h"
#include "../Wallets/Wallets.h"
#include "../AsyncClient.h"
#include "../Wallets/BIP32_Node.h"
#include "../BitcoinP2p.h"
#include "btc/ecc.h"

#include "MockedNode.h"

#ifdef _MSC_VER
#ifdef mlock
#undef mlock
#undef munlock
#endif
#include "win32_posix.h"
#undef close

#ifdef _DEBUG
//#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#ifndef DBG_NEW
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#define new DBG_NEW
#endif
#endif
#endif

#define READHEX BinaryData::CreateFromHex

#if ! defined(_MSC_VER) && ! defined(__MINGW32__)
   void mkdir(std::string newdir);
#endif

namespace Armory
{
   namespace Assets
   {
      class AssetEntry;
   };
};

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

   std::string registerBDV(Clients* clients, const BinaryData& magic_word);
   void goOnline(Clients* clients, const std::string& id);
   const std::shared_ptr<BDV_Server_Object> getBDV(Clients* clients, const std::string& id);
   
   void registerWallet(Clients* clients, const std::string& bdvId,
      const std::vector<BinaryData>& scrAddrs, const std::string& wltName,
      bool isLockbox, bool waitOnReg);

   std::vector<uint64_t> getBalanceAndCount(Clients* clients,
      const std::string& bdvId, const std::string& walletId, unsigned blockheight);
   std::string getLedgerDelegate(Clients* clients, const std::string& bdvId);
   std::vector<DBClientClasses::LedgerEntry> getHistoryPage(
      Clients* clients, const std::string& bdvId,
      const std::string& delegateId, uint32_t pageId);

   std::tuple<BinaryData, unsigned> waitOnSignal(
      Clients* clients, const std::string& bdvId, int signal);
   void waitOnBDMSignal(std::shared_ptr<BlockDataManager>, BDV_Action);
   void waitOnBDMReady(Clients* clients, const std::string& bdvId);
   void waitOnBDMError(std::shared_ptr<BlockDataManager>);

   std::tuple<BinaryData, unsigned> waitOnNewBlockSignal(Clients* clients, const std::string& bdvId);
   std::pair<std::vector<DBClientClasses::LedgerEntry>, std::set<BinaryData>>
      waitOnNewZcSignal(Clients* clients, const std::string& bdvId);
   void waitOnWalletRefresh(Clients* clients, const std::string& bdvId,
      const std::string& wltId);
   void triggerNewBlockNotification(BlockDataManagerThread* bdmt);
   void mineNewBlock(BlockDataManagerThread* bdmt, const BinaryData& h160,
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

   void pushNewZc(BlockDataManagerThread* bdmt, const ZcVector& zcVec, bool stage = false);
   void setNextZcPushDelay(unsigned);
   std::pair<BinaryData, BinaryData> getAddrAndPubKeyFromPrivKey(
      BinaryData privKey, bool compressed = false);

   Tx getTxByHash(Clients* clients, const std::string bdvId,
      const BinaryData& txHash);
   std::vector<UTXO> getUtxoForAddress(Clients* clients, const std::string bdvId, 
      const BinaryData& addr, bool withZc);

   void addTxioToSsh(StoredScriptHistory&, 
      const std::map<BinaryDataRef, std::shared_ptr<const TxIOPair>>&);
   void prettyPrintSsh(StoredScriptHistory& ssh);
   LedgerEntry getLedgerEntryFromWallet(std::shared_ptr<BtcWallet>, const BinaryData&);
   LedgerEntry getLedgerEntryFromAddr(ScrAddrObj*, const BinaryData&);

   void updateWalletsLedgerFilter(
      Clients*, const std::string&, const std::vector<std::string> &);

   BinaryData processCommand(Clients*, const std::string&, BinaryData);

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
         std::set<BinaryData> addrSet;
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

      void run(BdmNotification bdmNotif)
      {
         auto notif = std::make_unique<BdmNotif>();
         notif->action = bdmNotif.action;
         notif->requestID = bdmNotif.requestID;

         if (bdmNotif.action == BDMAction_Refresh) {
            notif->idSet = bdmNotif.ids;
         } else if (bdmNotif.action == BDMAction_ZC) {
            for (auto& le : bdmNotif.ledgers) {
               notif->idSet.emplace(le->getTxHash().toHexStr());

               auto addrVec = le->getScrAddrList();
               for (auto& addrRef : addrVec) {
                  notif->addrSet.insert(addrRef);
               }
            }
         } else if (bdmNotif.action == BDMAction_NewBlock) {
            notif->reorgHeight = bdmNotif.branchHeight;
         } else if (bdmNotif.action == BDMAction_BDV_Error) {
            notif->error = bdmNotif.error;
         }

         actionStack_.push_back(move(notif));
      }

      void progress(BDMPhase, const std::vector<std::string> &,
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
         const std::set<BinaryData>& hashes,
         std::set<BinaryData> scrAddrSet)
      {
         std::set<std::string> strHashes;
         for (const auto& hash : hashes) {
            strHashes.emplace(hash.toHexStr());
         }
         auto hashesToSee = strHashes;
         std::set<BinaryData> addrSet;
         while (true) {
            auto action = waitOnNotification(BDMAction_ZC);

            bool hasHashes = true;
            for (const auto& txHash : action->idSet) {
               if (strHashes.find(txHash) == strHashes.end()) {
                  hasHashes = false;
                  break;
               } else {
                  hashesToSee.erase(txHash);
               }
            }
            if (!hasHashes) {
               continue;
            }

            addrSet.insert(action->addrSet.begin(), action->addrSet.end());
            if (addrSet == scrAddrSet && hashesToSee.empty()) {
               break;
            }
         }
      }

      void waitOnZc_OutOfOrder(const std::set<BinaryData>& hashes)
      {
         std::set<std::string> hashSet;
         std::set<std::string> strHashes;
         for (const auto& hash : hashes) {
            strHashes.emplace(hash.toHexStr());
         }

         for (auto& pastNotif : zcNotifVec_) {
            for (auto& txHash : pastNotif.idSet) {
               if (strHashes.find(txHash) != strHashes.end()) {
                  hashSet.insert(txHash);
               }
            }

            if (hashSet == strHashes) {
               return;
            }
         }

         while (true) {
            auto action = waitOnNotification(BDMAction_ZC);
            zcNotifVec_.push_back(*action);

            for (auto& txHash : action->idSet) {
               if (strHashes.find(txHash) != strHashes.end()) {
                  hashSet.insert(txHash);
               }
            }

            if (hashSet == strHashes) {
               break;
            }
         }
      }

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
