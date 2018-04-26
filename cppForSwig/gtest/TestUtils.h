////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-17, goatpig                                            //            
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
#include "gtest.h"

#include "../log.h"
#include "../BinaryData.h"
#include "../BtcUtils.h"
#include "../BlockObj.h"
#include "../StoredBlockObj.h"
#include "../PartialMerkle.h"
#include "../EncryptionUtils.h"
#include "../lmdb_wrapper.h"
#include "../BlockUtils.h"
#include "../ScrAddrObj.h"
#include "../BtcWallet.h"
#include "../BlockDataViewer.h"
#include "../cryptopp/DetSign.h"
#include "../cryptopp/integer.h"
#include "../Progress.h"
#include "../reorgTest/blkdata.h"
#include "../BDM_seder.h"
#include "../BDM_Server.h"
#include "../TxClasses.h"
#include "../txio.h"
#include "../bdmenums.h"
#include "../SwigClient.h"
#include "../Script.h"
#include "../Signer.h"
#include "../Wallets.h"
#include "../WalletManager.h"
#include "../BIP32_Serialization.h"
#include "../BitcoinP2p.h"


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
   void rmdir(string src);
   void mkdir(string newdir);
#endif

namespace TestUtils
{
   // This function assumes src to be a zero terminated sanitized string with
   // an even number of [0-9a-f] characters, and target to be sufficiently large
   void hex2bin(const char* src, unsigned char* target);

   int char2int(char input);

   bool searchFile(const string& filename, BinaryData& data);
   uint32_t getTopBlockHeightInDB(BlockDataManager &bdm, DB_SELECT db);
   uint64_t getDBBalanceForHash160(
      BlockDataManager &bdm, BinaryDataRef addr160);

   void concatFile(const string &from, const string &to);
   void appendBlocks(const std::vector<std::string> &files, const std::string &to);
   void setBlocks(const std::vector<std::string> &files, const std::string &to);
   void nullProgress(unsigned, double, unsigned, unsigned);
   BinaryData getTx(unsigned height, unsigned id);
}

namespace DBTestUtils
{
   unsigned getTopBlockHeight(LMDBBlockDatabase*, DB_SELECT);
   BinaryData getTopBlockHash(LMDBBlockDatabase*, DB_SELECT);

   string registerBDV(Clients* clients, const BinaryData& magic_word);
   void goOnline(Clients* clients, const string& id);
   const shared_ptr<BDV_Server_Object> getBDV(Clients* clients, const string& id);
   
   void regWallet(Clients* clients, const string& bdvId,
      const vector<BinaryData>& scrAddrs, const string& wltName);
   void regLockbox(Clients* clients, const string& bdvId,
      const vector<BinaryData>& scrAddrs, const string& wltName);

   vector<uint64_t> getBalanceAndCount(Clients* clients,
      const string& bdvId, const string& walletId, unsigned blockheight);
   string getLedgerDelegate(Clients* clients, const string& bdvId);
   vector<LedgerEntryData> getHistoryPage(Clients* clients, const string& bdvId,
      const string& delegateId, uint32_t pageId);

   vector<shared_ptr<DataMeta>> waitOnSignal(
      Clients* clients, const string& bdvId,
      string command, const string& signal);
   void waitOnBDMReady(Clients* clients, const string& bdvId);

   void waitOnNewBlockSignal(Clients* clients, const string& bdvId);
   vector<LedgerEntryData> waitOnNewZcSignal(Clients* clients, const string& bdvId);
   void waitOnWalletRefresh(Clients* clients, const string& bdvId, 
      const BinaryData& wltId);
   void triggerNewBlockNotification(BlockDataManagerThread* bdmt);

   struct ZcVector
   {
      vector<Tx> zcVec_;

      void push_back(BinaryData rawZc, unsigned zcTime)
      {
         Tx zctx(rawZc);
         zctx.setTxTime(zcTime);

         zcVec_.push_back(move(zctx));
      }
   };

   void pushNewZc(BlockDataManagerThread* bdmt, const ZcVector& zcVec);
   pair<BinaryData, BinaryData> getAddrAndPubKeyFromPrivKey(BinaryData privKey);

   BinaryData getTxByHash(Clients* clients, const string bdvId,
      const BinaryData& txHash);
   Tx getTxObjByHash(
      Clients* clients, const string& bdvId, const BinaryData& txHash);

   void addTxioToSsh(StoredScriptHistory&, const map<BinaryData, TxIOPair>&);
   void prettyPrintSsh(StoredScriptHistory& ssh);
   LedgerEntry getLedgerEntryFromWallet(shared_ptr<BtcWallet>, const BinaryData&);
   LedgerEntry getLedgerEntryFromAddr(ScrAddrObj*, const BinaryData&);

   void updateWalletsLedgerFilter(
      Clients*, const string&, const vector<BinaryData>&);
}

namespace ResolverUtils
{
   ////////////////////////////////////////////////////////////////////////////////
   struct TestResolverFeed : public ResolverFeed
   {
      map<BinaryData, BinaryData> h160ToPubKey_;
      map<BinaryData, SecureBinaryData> pubKeyToPrivKey_;

      BinaryData getByVal(const BinaryData& val)
      {
         auto iter = h160ToPubKey_.find(val);
         if (iter == h160ToPubKey_.end())
            throw runtime_error("invalid value");

         return iter->second;
      }

      const SecureBinaryData& getPrivKeyForPubkey(const BinaryData& pubkey)
      {
         auto iter = pubKeyToPrivKey_.find(pubkey);
         if (iter == pubKeyToPrivKey_.end())
            throw runtime_error("invalid pubkey");

         return iter->second;
      }
   };

   ////////////////////////////////////////////////////////////////////////////////
   class HybridFeed : public ResolverFeed
   {
   private:
      shared_ptr<ResolverFeed_AssetWalletSingle> feedPtr_;

   public:
      TestResolverFeed testFeed_;

   public:
      HybridFeed(shared_ptr<AssetWallet_Single> wltPtr)
      {
         feedPtr_ = make_shared<ResolverFeed_AssetWalletSingle>(wltPtr);
      }

      BinaryData getByVal(const BinaryData& val)
      {
         try
         {
            return testFeed_.getByVal(val);
         }
         catch (runtime_error&)
         {
         }

         return feedPtr_->getByVal(val);
      }

      const SecureBinaryData& getPrivKeyForPubkey(const BinaryData& pubkey)
      {
         try
         {
            return testFeed_.getPrivKeyForPubkey(pubkey);
         }
         catch (runtime_error&)
         {
         }

         return feedPtr_->getPrivKeyForPubkey(pubkey);
      }
   };

   ////////////////////////////////////////////////////////////////////////////////
   struct CustomFeed : public ResolverFeed
   {
      map<BinaryDataRef, BinaryDataRef> hash_to_preimage_;
      shared_ptr<ResolverFeed> wltFeed_;

   private:
      void addAddressEntry(shared_ptr<AddressEntry> addrPtr)
      {
         try
         {
            BinaryDataRef hash(addrPtr->getHash());
            BinaryDataRef preimage(addrPtr->getPreimage());
            hash_to_preimage_.insert(make_pair(hash, preimage));
         }
         catch (exception)
         {
            return;
         }

         auto addr_nested = dynamic_pointer_cast<AddressEntry_Nested>(addrPtr);
         if (addr_nested != nullptr)
            addAddressEntry(addr_nested->getPredecessor());
      }

   public:
      CustomFeed(shared_ptr<AddressEntry> addrPtr,
         shared_ptr<AssetWallet_Single> wlt) :
         wltFeed_(make_shared<ResolverFeed_AssetWalletSingle>(wlt))
      {
         addAddressEntry(addrPtr);
      }

      CustomFeed(shared_ptr<AddressEntry> addrPtr,
         shared_ptr<ResolverFeed> feed) :
         wltFeed_(feed)
      {
         addAddressEntry(addrPtr);
      }

      BinaryData getByVal(const BinaryData& key)
      {
         auto keyRef = BinaryDataRef(key);
         auto iter = hash_to_preimage_.find(keyRef);
         if (iter == hash_to_preimage_.end())
            throw runtime_error("invalid value");

         return iter->second;
      }

      const SecureBinaryData& getPrivKeyForPubkey(const BinaryData& pubkey)
      {
         return wltFeed_->getPrivKeyForPubkey(pubkey);
      }
   };
}

#endif
