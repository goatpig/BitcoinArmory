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

#include "NodeUnitTest.h"

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
   const std::string dataDir("../reorgTest");

   // This function assumes src to be a zero terminated sanitized string with
   // an even number of [0-9a-f] characters, and target to be sufficiently large
   void hex2bin(const char* src, unsigned char* target);

   int char2int(char input);

   bool searchFile(const std::string& filename, BinaryData& data);
   uint32_t getTopBlockHeightInDB(BlockDataManager &bdm, DB_SELECT db);
   uint64_t getDBBalanceForHash160(
      BlockDataManager &bdm, BinaryDataRef addr160);

   void concatFile(const std::string &from, const std::string &to);
   void appendBlocks(const std::vector<std::string> &files, const std::string &to);
   void setBlocks(const std::vector<std::string> &files, const std::string &to);
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
   void regLockbox(Clients* clients, const std::string& bdvId,
      const std::vector<BinaryData>& scrAddrs, const std::string& wltName);

   std::vector<uint64_t> getBalanceAndCount(Clients* clients,
      const std::string& bdvId, const std::string& walletId, unsigned blockheight);
   std::string getLedgerDelegate(Clients* clients, const std::string& bdvId);
   std::vector<DBClientClasses::LedgerEntry> getHistoryPage(
      Clients* clients, const std::string& bdvId,
      const std::string& delegateId, uint32_t pageId);

   std::tuple<BinaryData, unsigned> waitOnSignal(Clients* clients, const std::string& bdvId, int signal);
   void waitOnBDMReady(Clients* clients, const std::string& bdvId);

   std::tuple<BinaryData, unsigned> waitOnNewBlockSignal(Clients* clients, const std::string& bdvId);
   std::pair<std::vector<DBClientClasses::LedgerEntry>, std::set<BinaryData>>
      waitOnNewZcSignal(Clients* clients, const std::string& bdvId);
   void waitOnWalletRefresh(Clients* clients, const std::string& bdvId,
      const BinaryData& wltId);
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
         BDMAction action_;
         std::vector<BinaryData> idVec_;
         std::set<BinaryData> addrSet_;
         unsigned reorgHeight_ = UINT32_MAX;
         BDV_Error_Struct error_;
         std::string requestID_;
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
            while (iter != actionDeque_.end())
            {
               if ((*iter)->action_ == actionType)
               {
                  auto result = move(*iter);
                  actionDeque_.erase(iter);
                  return result;
               }

               ++iter;
            }
         }

         while (true)
         {
            auto&& action = actionStack_.pop_front();
            if (action->action_ == actionType)
               return move(action);

            actionDeque_.push_back(move(action));
         }
      }

      void run(BdmNotification bdmNotif)
      {
         auto notif = std::make_unique<BdmNotif>();
         notif->action_ = bdmNotif.action_;
         notif->requestID_ = bdmNotif.requestID_;

         if (bdmNotif.action_ == BDMAction_Refresh)
         {
            notif->idVec_ = bdmNotif.ids_;
         }
         else if (bdmNotif.action_ == BDMAction_ZC)
         {
            for (auto& le : bdmNotif.ledgers_)
            {
               notif->idVec_.push_back(le->getTxHash());

               auto addrVec = le->getScrAddrList();
               for (auto& addrRef : addrVec)
                  notif->addrSet_.insert(addrRef);
            }
         }
         else if (bdmNotif.action_ == BDMAction_NewBlock)
         {
            notif->reorgHeight_ = bdmNotif.branchHeight_;
         }
         else if (bdmNotif.action_ == BDMAction_BDV_Error)
         {
            notif->error_ = bdmNotif.error_;
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
            if (action->action_ == BDMAction_NewBlock)
            {
               if (action->reorgHeight_ != UINT32_MAX)
                  return action->reorgHeight_;
            }
         }
      }

      void waitOnSignal(BDMAction signal, std::string id = "")
      {
         BinaryDataRef idRef; idRef.setRef(id);
         while (1)
         {
            auto&& action = actionStack_.pop_front();
            if (action->action_ == signal)
            {
               if (id.size() > 0)
               {
                  for (auto& id : action->idVec_)
                  {
                     if (id == idRef)
                        return;
                  }
               }
               else
               {
                  return;
               }
            }
         }
      }

      void waitOnManySignals(BDMAction signal, std::vector<std::string> ids)
      {
         unsigned count = 0;
         std::set<BinaryDataRef> bdrVec;
         for (auto& id : ids) {
            BinaryDataRef bdr; bdr.setRef(id);
            bdrVec.insert(bdr);
         }

         while (true) {
            if (count >= ids.size()) {
               break;
            }

            auto action = actionStack_.pop_front();
            if (action->action_ == signal) {
               for (auto& id : action->idVec_) {
                  if (bdrVec.find(id) != bdrVec.end()) {
                     ++count;
                  }
               }
            }
         }
      }

      void waitOnZc(
         const std::set<BinaryData>& hashes, 
         std::set<BinaryData> scrAddrSet,
         const std::string& broadcastID)
      {
         std::set<BinaryData> addrSet;
         while (1)
         {
            auto&& action = waitOnNotification(BDMAction_ZC);

            if (!broadcastID.empty())
            {
               if (action->requestID_ != broadcastID)
                  continue;
            }

            bool hasHashes = true;
            for (auto& txHash : action->idVec_)
            {
               if (hashes.find(txHash) == hashes.end())
               {
                  hasHashes = false;
                  break;
               }
            }

            if (!hasHashes)
               continue;

            addrSet.insert(action->addrSet_.begin(), action->addrSet_.end());
            if (addrSet == scrAddrSet)
               break;
         }
      }

      void waitOnZc_OutOfOrder(
         const std::set<BinaryData>& hashes, 
         const std::string& broadcastID)
      {
         std::set<BinaryData> hashSet;

         for (auto& pastNotif : zcNotifVec_)
         {
            for (auto& txHash : pastNotif.idVec_)
            {
               if (hashes.find(txHash) != hashes.end())
                  hashSet.insert(txHash);
            }

            if (hashSet == hashes)
               return;
         }

         while (1)
         {
            auto&& action = waitOnNotification(BDMAction_ZC);
            zcNotifVec_.push_back(*action);

            if (!broadcastID.empty())
            {
               if (action->requestID_ != broadcastID)
                  continue;
            }

            for (auto& txHash : action->idVec_)
            {
               if (hashes.find(txHash) != hashes.end())
                  hashSet.insert(txHash);
            }

            if (hashSet == hashes)
               break;
         }
      }

      void waitOnError(const BinaryData& hash, ArmoryErrorCodes errorCode,
         const std::string& requestID)
      {
         if (requestID.empty())
            throw std::runtime_error("empty request id");

         while (true)
         {
            auto&& action = waitOnNotification(BDMAction_BDV_Error);

            if (action->requestID_ != requestID)
               continue;

            if (action->error_.errData_ == hash && 
               action->error_.errCode_ == (int)errorCode)
               break;
         }         
      }

      void waitOnErrors(const std::map<BinaryData, ArmoryErrorCodes>& errorMap,
         const std::string& requestID)
      {
         if (requestID.empty())
            throw std::runtime_error("empty request id");

         auto mapCopy = errorMap;
         while (true)
         {
            if (mapCopy.empty())
               return;

            auto&& action = waitOnNotification(BDMAction_BDV_Error);
            if (action->requestID_ != requestID)
               continue;

            auto iter = mapCopy.find(action->error_.errData_);
            if (iter == mapCopy.end())
               continue;
            
            if ((int)iter->second == action->error_.errCode_)
               mapCopy.erase(iter);
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
