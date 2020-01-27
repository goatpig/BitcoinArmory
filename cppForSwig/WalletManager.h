////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-19, goatpig                                            //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _WALLET_MANAGER_H
#define _WALLET_MANAGER_H

#include <mutex>
#include <memory>
#include <string>
#include <map>
#include <iostream>

#include "log.h"
#include "Wallets.h"
#include "SwigClient.h"
#include "Signer.h"
#include "BlockDataManagerConfig.h"
#include "CoinSelection.h"
#include "Script.h"

class WalletContainer;

////////////////////////////////////////////////////////////////////////////////
struct CoinSelectionInstance
{
private:
   CoinSelection cs_;

   std::map<unsigned, std::shared_ptr<ScriptRecipient> > recipients_;
   UtxoSelection selection_;
   std::shared_ptr<AssetWallet> const walletPtr_;

   std::vector<UTXO> state_utxoVec_;
   uint64_t spendableBalance_;

private:
   static void decorateUTXOs(std::shared_ptr<AssetWallet> const, std::vector<UTXO>&);
   static std::function<std::vector<UTXO>(uint64_t)> getFetchLambdaFromWalletContainer(
      std::shared_ptr<WalletContainer> const walletContainer);
   static std::function<std::vector<UTXO>(uint64_t)> getFetchLambdaFromWallet(
      std::shared_ptr<AssetWallet> const, std::function<std::vector<UTXO>(uint64_t)>);


   static std::function<std::vector<UTXO>(uint64_t)> getFetchLambdaFromLockbox(
      SwigClient::Lockbox* const, unsigned M, unsigned N);

   uint64_t getSpendVal(void) const;
   void checkSpendVal(uint64_t) const;
   void addRecipient(unsigned, const BinaryData&, uint64_t);
   void selectUTXOs(std::vector<UTXO>&, uint64_t fee, float fee_byte, unsigned flags);

public:
   CoinSelectionInstance(std::shared_ptr<WalletContainer> const walletContainer,
      const std::vector<AddressBookEntry>& addrBook, unsigned topHeight);
   CoinSelectionInstance(std::shared_ptr<AssetWallet>, 
      std::function<std::vector<UTXO>(uint64_t)>,
      const std::vector<AddressBookEntry>& addrBook, 
      uint64_t spendableBalance, unsigned topHeight);
   CoinSelectionInstance(SwigClient::Lockbox* const,
      unsigned M, unsigned N, uint64_t balance, unsigned topHeight);

   unsigned addRecipient(const BinaryData&, uint64_t);
   void updateRecipient(unsigned, const BinaryData&, uint64_t);
   void updateOpReturnRecipient(unsigned, const BinaryData&);
   void removeRecipient(unsigned);
   void resetRecipients(void);
   const std::map<unsigned, std::shared_ptr<ScriptRecipient> >& getRecipients(void) const {
      return recipients_;
   }

   bool selectUTXOs(uint64_t fee, float fee_byte, unsigned flags);
   void processCustomUtxoList(
      const std::vector<BinaryData>& serializedUtxos,
      uint64_t fee, float fee_byte,
      unsigned flags);

   void updateState(uint64_t fee, float fee_byte, unsigned flags);

   uint64_t getFeeForMaxValUtxoVector(const std::vector<BinaryData>& serializedUtxos, float fee_byte);
   uint64_t getFeeForMaxVal(float fee_byte);

   size_t getSizeEstimate(void) const { return selection_.size_; }
   std::vector<UTXO> getUtxoSelection(void) const { return selection_.utxoVec_; }
   uint64_t getFlatFee(void) const { return selection_.fee_; }
   float getFeeByte(void) const { return selection_.fee_byte_; }

   bool isSW(void) const { return selection_.witnessSize_ != 0; }
   void rethrow(void) { cs_.rethrow(); }

   static std::shared_ptr<ScriptRecipient> createRecipient(const BinaryData&, uint64_t);
};

////////////////////////////////////////////////////////////////////////////////
class WalletContainer
{
   friend class WalletManager;
   friend class PythonSigner;
   friend struct CoinSelectionInstance;

private:
   const std::string id_;
   std::shared_ptr<AssetWallet> wallet_;

   std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr_;
   std::shared_ptr<AsyncClient::BtcWallet> asyncWlt_;

   std::map<BinaryData, std::vector<uint64_t>> balanceMap_;
   std::map<BinaryData, uint64_t> countMap_;

   uint64_t totalBalance_ = 0;
   uint64_t spendableBalance_ = 0;
   uint64_t unconfirmedBalance_ = 0;
   uint64_t txioCount_ = 0;
   
   uint32_t highestUsedIndex_ = UINT32_MAX;
   std::mutex stateMutex_;

   std::map<BinaryData, std::shared_ptr<AddressEntry>> updatedAddressMap_;

private:
   WalletContainer(const std::string& id) :
      id_(id)
   {}

   void reset(void)
   {
      totalBalance_ = 0;
      spendableBalance_ = 0;
      unconfirmedBalance_ = 0;
      balanceMap_.clear();
      countMap_.clear();
   }

   void setBdvPtr( std::shared_ptr<AsyncClient::BlockDataViewer> bdv)
   {
      std::unique_lock<std::mutex> lock(stateMutex_);
      bdvPtr_ = bdv;
   }

   void setWalletPtr(std::shared_ptr<AssetWallet> wltPtr)
   {
      wallet_ = wltPtr;
      auto mainAcc = wallet_->getAccountForID(wallet_->getMainAccountID());
      auto&& addrMap = mainAcc->getUsedAddressMap();
      
      unsigned count = 0;
      for (auto& addrPair : addrMap)
      {
         auto id = addrPair.second->getID();
         auto idLast = id.getSliceRef(id.getSize() - 4, 4);
         auto idInt = READ_UINT32_BE(idLast);

         if (idInt > count)
            count = idInt;
      }

      highestUsedIndex_ = count;
   }

public:
   std::string registerWithBDV(bool isNew);

   virtual std::shared_ptr<AssetWallet> getWalletPtr(void) const
   {
      return wallet_;
   }

   void updateBalancesAndCount(uint32_t topBlockHeight)
   {
      auto lbd = [this](ReturnMessage<std::vector<uint64_t>> vec)
      {
         std::unique_lock<std::mutex> lock(stateMutex_);
         
         auto&& balVec = vec.get();
         totalBalance_ = balVec[0];
         spendableBalance_ = balVec[1];
         unconfirmedBalance_ = balVec[2];
      };
      asyncWlt_->getBalancesAndCount(topBlockHeight, lbd);
   }

   void updateAddrTxCount(void)
   {
      auto lbd = [this](
         ReturnMessage<std::map<BinaryData, uint32_t>> countMap)->void
      {
         std::unique_lock<std::mutex> lock(stateMutex_);

         auto&& cmap = countMap.get();
         for (auto& cpair : cmap)
            countMap_[cpair.first] = cpair.second;
      };
      asyncWlt_->getAddrTxnCountsFromDB(lbd);
   }
   
   void updateAddrBalancesFromDB(void)
   {
      auto lbd = [this](ReturnMessage<
         std::map<BinaryData, std::vector<uint64_t>>> result)->void
      {
         std::unique_lock<std::mutex> lock(stateMutex_);

         auto&& balancemap = result.get();
         for (auto& balVec : balancemap)
         {
            if (balVec.first.getSize() == 0)
               continue;

            balanceMap_[balVec.first] = balVec.second;
         }
      };

      asyncWlt_->getAddrBalancesFromDB(lbd);
   }

   void updateWalletBalanceState(const CombinedBalances&);
   void updateAddressCountState(const CombinedCounts&);

   void extendAddressChain(unsigned count)
   {
      wallet_->extendPublicChain(count);
   }

   void extendAddressChainToIndex(const BinaryData& id, unsigned count)
   {
      wallet_->extendPublicChainToIndex(id, count);
   }

   bool hasAddress(const BinaryData& addr)
   {
      return wallet_->hasScrAddr(addr);
   }

   bool hasAddress(const std::string& addr)
   {
      return wallet_->hasAddrStr(addr);
   }

   /*
   const std::pair<BinaryData, AddressEntryType>& getAssetIDForAddr(
      const std::string& addrStr) const
   {
      return wallet_->getAssetIDForAddrStr(addrStr);
   }

   const std::pair<BinaryData, AddressEntryType>& getAssetIDForAddr(
      const BinaryData& scrAddr) const
   {
      return wallet_->getAssetIDForScrAddr(scrAddr);
   }

   const BinaryData& getScriptHashPreimage(const BinaryData& hash)
   {
      auto& assetID = wallet_->getAssetIDForScrAddr(hash);
      auto addrPtr = wallet_->getAddressEntryForID(assetID.first);
      return addrPtr->getPreimage();
   }

   AddressEntryType getAddrTypeForID(const BinaryData& ID)
   {
      return wallet_->getAddrTypeForID(ID);
   }

   AsyncClient::ScrAddrObj getAddrObjByID(const BinaryData& ID)
   {
      auto addrPtr = wallet_->getAddressEntryForID(ID);

      uint64_t full = 0, spend = 0, unconf = 0;
      auto balanceIter = balanceMap_.find(addrPtr->getPrefixedHash());
      if (balanceIter != balanceMap_.end())
      {
         full = balanceIter->second[0];
         spend = balanceIter->second[1];
         unconf = balanceIter->second[2];
      }

      uint32_t count = 0;
      auto countIter = countMap_.find(addrPtr->getPrefixedHash());
      if (countIter != countMap_.end())
         count = countIter->second;

      BinaryRefReader brr(ID.getRef());
      brr.advance(ID.getSize() - 4);
      auto index = brr.get_uint32_t();

      if (asyncWlt_ != nullptr)
      {
         AsyncClient::ScrAddrObj saObj(
            asyncWlt_.get(), addrPtr->getPrefixedHash(), index,
            full, spend, unconf, count);

         return saObj;
      }
      else
      {
         AsyncClient::ScrAddrObj saObj(addrPtr->getPrefixedHash(), index);

         return saObj;
      }
   }
   */

   void createAddressBook(
      const std::function<void(ReturnMessage<std::vector<AddressBookEntry>>)>&);

   void getSpendableTxOutListForValue(uint64_t val, 
      const std::function<void(ReturnMessage<std::vector<UTXO>>)>& lbd)
   {
      asyncWlt_->getSpendableTxOutListForValue(val, lbd);
   }

   void getSpendableZcTxOutList(
      const std::function<void(ReturnMessage<std::vector<UTXO>>)>& lbd)
   {
      asyncWlt_->getSpendableZCList(lbd);
   }

   void getRBFTxOutList(
      const std::function<void(ReturnMessage<std::vector<UTXO>>)>& lbd)
   {
      asyncWlt_->getRBFTxOutList(lbd);
   }

   uint64_t getFullBalance(void) const { return totalBalance_; }
   uint64_t getSpendableBalance(void) const { return spendableBalance_; }
   uint64_t getUnconfirmedBalance(void) const { return unconfirmedBalance_; }
   uint64_t getTxIOCount(void) const { return txioCount_; }

   std::map<BinaryData, std::vector<uint64_t>> getAddrBalanceMap(void) const;
   uint32_t getHighestUsedIndex(void) const { return highestUsedIndex_; }
   std::map<BinaryData, std::shared_ptr<AddressEntry>> getUpdatedAddressMap();
};

////////////////////////////////////////////////////////////////////////////////

enum Armory135WalletEntriesEnum
{
   WLT_DATATYPE_KEYDATA     = 0,
   WLT_DATATYPE_ADDRCOMMENT,
   WLT_DATATYPE_TXCOMMENT,
   WLT_DATATYPE_OPEVAL,
   WLT_DATATYPE_DELETED
};

////////////////////////////////////////////////////////////////////////////////
struct Armory135Address
{
private:
   //public data
   BinaryData scrAddr_;
   SecureBinaryData pubKey_;
   SecureBinaryData chaincode_;
   
   //private data
   SecureBinaryData privKey_;
   SecureBinaryData decryptedPrivKey_;

   //encryption data
   SecureBinaryData iv_;

   //indexes
   int64_t chainIndex_;
   int64_t depth_;

   //flags
   bool hasPrivKey_ = false;
   bool hasPubKey_ = false;
   bool isEncrypted_ = false;

public:
   Armory135Address(void) {}

   void parseFromRef(const BinaryDataRef&);
   bool isEncrypted(void) const { return isEncrypted_; }
   bool hasPrivKey(void) const { return hasPrivKey_; }
   
   const SecureBinaryData& privKey(void) const { return privKey_; }
   const SecureBinaryData& pubKey(void) const { return pubKey_; }
   const SecureBinaryData& chaincode(void) const { return chaincode_; }
   const SecureBinaryData& iv(void) const { return iv_; }

   const BinaryData& scrAddr(void) const { return scrAddr_; }
   int64_t chainIndex(void) const { return chainIndex_; }
};

////////////////////////////////////////////////////////////////////////////////
struct Armory135Header
{
private:
   //file system
   const std::string path_;

   //meta data
   std::string walletID_;
   uint32_t version_ = UINT32_MAX;
   uint64_t timestamp_ = UINT32_MAX;

   std::string labelName_;
   std::string labelDescription_;
   
   int64_t highestUsedIndex_ = -1;

   //flags
   bool isEncrypted_ = false;
   bool watchingOnly_ = false;

   //encryption data
   uint64_t kdfMem_ = UINT64_MAX;
   uint32_t kdfIter_;
   SecureBinaryData kdfSalt_;

   //comments
   std::map<BinaryData, std::string> commentMap_;

   //address map
   std::map<BinaryData, Armory135Address> addrMap_;

private:
   void parseFile();


public:
   Armory135Header(const std::string path) :
      path_(path)
   {
      parseFile();
   }

   bool isInitialized(void) { return version_ != UINT32_MAX; }
   const std::string& getID(void) const { return walletID_; }
   std::shared_ptr<AssetWallet_Single> migrate(
      const PassphraseLambda&) const;

   //static
   static void verifyChecksum(
      const BinaryDataRef& val, const BinaryDataRef& chkSum);
};

////////////////////////////////////////////////////////////////////////////////
class WalletManager
{
private:
   mutable std::mutex mu_;

   const std::string path_;
   std::map<std::string, std::shared_ptr<WalletContainer>> wallets_;

   PassphraseLambda passphraseLbd_;

   std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr_; 

private:
   void loadWallets(
      const std::function<SecureBinaryData(const std::set<BinaryData>&)>&);

public:
   WalletManager(const std::string& path,
      const std::function<SecureBinaryData(const std::set<BinaryData>&)>& passLbd) :
      path_(path)
   {
      loadWallets(passLbd);
   }

   bool hasWallet(const std::string& id)
   {
      std::unique_lock<std::mutex> lock(mu_);
      auto wltIter = wallets_.find(id);
      
      return wltIter != wallets_.end();
   }

   const std::map<std::string, std::shared_ptr<WalletContainer>>& getMap(void) const
   {
      return wallets_;
   }

   void setBdvPtr(std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr)
   {
      bdvPtr_ = bdvPtr;
      for (auto& wlt : wallets_)
         wlt.second->setBdvPtr(bdvPtr);
   }

   std::set<std::string> registerWallets()
   {
      std::set<std::string> idSet;
      for (auto& wlt : wallets_)
         idSet.insert(wlt.second->registerWithBDV(false));

      return idSet;
   }

   void updateStateFromDB(const std::function<void(void)>&);
};

#endif
