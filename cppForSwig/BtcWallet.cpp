////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#include "BtcWallet.h"
#include "BlockchainDatabase/BlockUtils.h"
#include "BlockchainDatabase/txio.h"
#include "BlockDataViewer.h"
#include "TxOutScrRef.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
// BtcWallet Methods
//
////////////////////////////////////////////////////////////////////////////////
void BtcWallet::removeAddressBulk(vector<BinaryDataRef> const & scrAddrBulk)
{
   scrAddrMap_.erase(scrAddrBulk);
   needsRefresh(true);
}

/////////////////////////////////////////////////////////////////////////////
bool BtcWallet::hasScrAddress(const BinaryDataRef& scrAddr) const
{
   auto addrMap = scrAddrMap_.get();
   return (addrMap->find(scrAddr) != addrMap->end());
}

/////////////////////////////////////////////////////////////////////////////
std::set<BinaryDataRef> BtcWallet::getAddrSet() const
{
   auto addrMap = scrAddrMap_.get();
   std::set<BinaryDataRef> addrSet;

   for (auto& addrPair : *addrMap) {
      addrSet.emplace(addrPair.first);
   }
   return addrSet;
}

////////////////////////////////////////////////////////////////////////////////
void BtcWallet::clearBlkData(void)
{
   auto addrMap = scrAddrMap_.get();

   for (auto saPair : *addrMap)
   { saPair.second->clearBlkData(); }

   histPages_.reset();
}

////////////////////////////////////////////////////////////////////////////////
uint64_t BtcWallet::getSpendableBalance(uint32_t currBlk) const
{
   auto addrMap = scrAddrMap_.get();

   uint64_t balance = 0;
   for(const auto& scrAddr : *addrMap)
      balance += scrAddr.second->getSpendableBalance(currBlk);

   return balance;
}

////////////////////////////////////////////////////////////////////////////////
uint64_t BtcWallet::getUnconfirmedBalance(uint32_t currBlk) const
{
   auto addrMap = scrAddrMap_.get();

   uint64_t balance = 0;
   for (const auto& scrAddr : *addrMap)
      balance += scrAddr.second->getUnconfirmedBalance(currBlk, confTarget_);
   
   return balance;
}

////////////////////////////////////////////////////////////////////////////////
uint64_t BtcWallet::getFullBalance() const
{
   return balance_;
}

////////////////////////////////////////////////////////////////////////////////
uint64_t BtcWallet::getFullBalanceFromDB(unsigned updateID) const
{
   uint64_t balance = 0;

   auto addrMap = scrAddrMap_.get();

   for (auto& scrAddr : *addrMap)
      balance += scrAddr.second->getFullBalance(updateID);

   return balance;
}

////////////////////////////////////////////////////////////////////////////////
map<BinaryData, uint32_t> BtcWallet::getAddrTxnCounts(int32_t updateID) const
{
   map<BinaryData, uint32_t> countMap;

   auto addrMap = scrAddrMap_.get();
   for (const auto& sa : *addrMap)
   {
      if (sa.second->updateID_ <= lastPulledCountsID_)
         continue;

      auto count = sa.second->getTxioCount();
      if (count == 0 || count == UINT32_MAX)
         continue;

      countMap[sa.first] = count;
   }

   lastPulledCountsID_ = updateID;

   return countMap;
}

////////////////////////////////////////////////////////////////////////////////
map<BinaryData, tuple<uint64_t, uint64_t, uint64_t>> 
   BtcWallet::getAddrBalances(int32_t updateID, unsigned blockHeight) const
{
   map<BinaryData, tuple<uint64_t, uint64_t, uint64_t>> balanceMap;

   auto addrMap = scrAddrMap_.get();
   for (auto& sa : *addrMap)
   {
      if (sa.second->updateID_ <= lastPulledBalancesID_)
         continue;

      uint64_t full, spendable, unconf;

      #ifdef DEBUG
      try
      {
      #endif
            
         full = sa.second->getFullBalance(UINT32_MAX);
         spendable = sa.second->getSpendableBalance(blockHeight);
         unconf = sa.second->getUnconfirmedBalance(blockHeight, confTarget_);

      #ifdef DEBUG
      }
      catch (...)
      {
         LOGERR << "BtcWallet::getAddrBalances: " <<
            " failed to get balance for address: " <<
            sa.first.toHexStr();
         throw runtime_error(sa.first.toHexStr());
      }
      #endif

      if (lastPulledBalancesID_ <= 0)
      {
         if (full == 0 && spendable == 0 && unconf == 0)
            continue;
      }

      balanceMap[sa.first] = move(make_tuple(full, spendable, unconf));
   }
   
   lastPulledBalancesID_ = updateID;

   return balanceMap;
}

////////////////////////////////////////////////////////////////////////////////
void BtcWallet::prepareTxOutHistory(uint64_t val)
{
   uint64_t value;
   uint32_t count;
      
   auto addrMap = scrAddrMap_.get();

   auto spentByZC = [this](const BinaryData& dbkey)->bool
   { return this->bdvPtr_->isTxOutSpentByZC(dbkey); };

   while (1)
   {
      value = 0;
      count = 0;


      for (const auto& scrAddr : *addrMap)
      {
         value += scrAddr.second->getLoadedTxOutsValue();
         count += scrAddr.second->getLoadedTxOutsCount();
      }

      //grab at least MIN_UTXO_PER_TXN and cover for twice the requested value
      if (value * 2 < val || count < MIN_UTXO_PER_TXN)
      {
         /***getMoreUTXOs returns true if it found more. As long as one
         ScrAddrObj has more, reassess the utxo state, otherwise get out of 
         the loop
         ***/

         bool hasMore = false;
         for (auto& scrAddr : *addrMap)
            hasMore |= scrAddr.second->getMoreUTXOs(spentByZC);

         if (!hasMore)
            break;
      }
      else 
         break;
   } 
}

////////////////////////////////////////////////////////////////////////////////
void BtcWallet::prepareFullTxOutHistory()
{
   auto spentByZC = [this](BinaryData dbkey)->bool
   { return this->bdvPtr_->isTxOutSpentByZC(dbkey); };

   auto addrMap = scrAddrMap_.get();

   while (1)
   {
      bool hasMore = false;
      for (auto& scrAddr : *addrMap)
         hasMore |= scrAddr.second->getMoreUTXOs(spentByZC);

      if (hasMore == false)
         return;
   }
}

////////////////////////////////////////////////////////////////////////////////
void BtcWallet::resetTxOutHistory()
{
   auto addrMap = scrAddrMap_.get();

   for (auto& scrAddr : *addrMap)
      scrAddr.second->resetTxOutHistory();
}

////////////////////////////////////////////////////////////////////////////////
void BtcWallet::resetCounters()
{
   lastPulledCountsID_ = -1;
   lastPulledBalancesID_ = -1;
}

////////////////////////////////////////////////////////////////////////////////
vector<UTXO> BtcWallet::getSpendableTxOutListForValue(uint64_t val)
{
   /***
   Only works with DB so it naturally ignores ZC 
   Use getSpendableTxOutListZC get unconfirmed outputs

   Only the TxIOPairs (DB keys) are saved in RAM. The full TxOuts are pulled only
   on demand since there is a high probability that at least a few of them will 
   be consumed.

   Grabs at least 100 UTXOs with enough spendable balance to cover 2x val (if 
   available of course), otherwise returns the full UTXO list for the wallet.

   val defaults to UINT64_MAX, so not passing val will result in 
   grabbing all UTXOs in the wallet
   ***/

   prepareTxOutHistory(val);
   LMDBBlockDatabase *db = bdvPtr_->getDB();

   //start a RO txn to grab the txouts from DB
   auto&& tx = db->beginTransaction(STXO, LMDB::ReadOnly);

   vector<UTXO> utxoList;
   uint32_t blk = bdvPtr_->getTopBlockHeight();

   auto addrMap = scrAddrMap_.get();

   for (const auto& scrAddr : *addrMap)
   {
      const auto& txioMap = scrAddr.second->getPreparedTxOutList();

      for (const auto& txioPair : txioMap)
      {
         if (!txioPair.second.isSpendable(db, blk))
            continue;

         auto&& txout_key = txioPair.second.getDBKeyOfOutput();
         StoredTxOut stxo;
         db->getStoredTxOut(stxo, txout_key);
         auto hash = db->getTxHashForLdbKey(txout_key.getSliceRef(0, 6));

         UTXO utxo(
            stxo.getValue(), stxo.getHeight(), 
            stxo.txIndex_, stxo.txOutIndex_, 
            hash, stxo.getScriptRef());
         utxoList.emplace_back(move(utxo));
      }
   }

   //Shipped a list of TxOuts, time to reset the entire TxOut history, since 
   //we dont know if any TxOut will be spent

   resetTxOutHistory();
   return utxoList;
}

////////////////////////////////////////////////////////////////////////////////
vector<UTXO> BtcWallet::getSpendableTxOutListZC()
{
   set<BinaryData> txioKeys;

   {
      auto addrMap = scrAddrMap_.get();
      for (auto& scrAddr : *addrMap)
      {
         auto&& zcTxioMap = bdvPtr_->getUnspentZCForScrAddr(
            scrAddr.second->getScrAddr());

         for (auto& zcTxio : zcTxioMap)
            txioKeys.insert(zcTxio.first);
      }
   }

   return bdvPtr_->getZcUTXOsForKeys(txioKeys);
}

////////////////////////////////////////////////////////////////////////////////
vector<UTXO> BtcWallet::getRBFTxOutList()
{
   set<BinaryData> zcKeys;
   set<BinaryData> txoutKeys;

   {
      auto addrMap = scrAddrMap_.get();
      for (auto& scrAddr : *addrMap)
      {
         auto&& zcTxioMap = bdvPtr_->getRBFTxIOsforScrAddr(
            scrAddr.second->getScrAddr());

         for (auto& zcTxio : zcTxioMap)
         {
            if (zcTxio.second->hasTxOutZC())
               zcKeys.insert(zcTxio.second->getDBKeyOfOutput());
            else
               txoutKeys.insert(zcTxio.second->getDBKeyOfOutput());
         }
      }
   }

   auto&& utxoVec = bdvPtr_->getZcUTXOsForKeys(zcKeys);

   BinaryDataRef prevTxKey;
   BinaryDataRef prevTxHash;
   for (auto& txoutkey : txoutKeys)
   {
      auto&& stxo = bdvPtr_->getStoredTxOut(txoutkey);
      UTXO utxo(
         stxo.getValue(), stxo.getHeight(), 
         stxo.txIndex_, stxo.txOutIndex_, 
         stxo.parentHash_, stxo.getScriptRef());

      utxoVec.emplace_back(move(utxo));
   }

   return utxoVec;
}

////////////////////////////////////////////////////////////////////////////////
// Return a list of addresses this wallet has ever sent to (w/o change addr)
// Does not include zero-conf tx
//
// TODO: make this scalable!
//
vector<AddressBookEntry> BtcWallet::createAddressBook(void)
{
   // Collect all data into a map -- later converted to vector and sort it
   map<BinaryData, set<BinaryData>> sentToMap;
   map<BinaryData, BinaryData> keyToHash;

   auto scrAddrMap = scrAddrMap_.get();

   for (auto& saPair : *scrAddrMap)
   {
      auto&& txioMap = saPair.second->getTxios();

      for (auto& txioPair : txioMap)
      {
         //skip unspent and zc spends
         if (!txioPair.second.hasTxIn() || txioPair.second.hasTxInZC())
            continue;

         //skip already processed tx
         auto&& dbKey = txioPair.second.getDBKeyOfInput();
         auto& txHash = keyToHash[dbKey];
         if (txHash.getSize() == 32)
            continue;

         //grab tx
         auto&& fullTx = bdvPtr_->getDB()->getFullTxCopy(dbKey.getSliceRef(0, 6));
         txHash = fullTx.getThisHash();

         auto nOut = fullTx.getNumTxOut();
         auto txPtr = fullTx.getPtr();

         for (unsigned i = 0; i < nOut; i++)
         {
            unsigned offset = fullTx.getTxOutOffset(i);
            unsigned outputSize = fullTx.getTxOutOffset(i + 1) - offset;
            BinaryDataRef outputRef(txPtr + offset + 8, outputSize);
            
            BinaryRefReader brr(outputRef);
            auto scriptSize = brr.get_var_int();

            auto&& scrRef = 
               BtcUtils::getTxOutScrAddrNoCopy(brr.get_BinaryDataRef(scriptSize));
            auto&& scrAddr = scrRef.getScrAddr();

            if (hasScrAddress(scrRef.getScrAddr()))
               continue;

            auto&& hashSet = sentToMap[scrAddr];
            hashSet.insert(txHash);
         }
      }
   }

   vector<AddressBookEntry> outputVect;
   for (const auto &entry : sentToMap)
   {
      AddressBookEntry abe(entry.first);
      for (auto& hash : entry.second)
         abe.addTxHash(move(hash));

      outputVect.push_back(move(abe));
   }

   sort(outputVect.begin(), outputVect.end());
   return outputVect;
}

////////////////////////////////////////////////////////////////////////////////
map<BinaryData, TxIOPair> BtcWallet::scanWalletZeroConf(
   const ScanWalletStruct& scanInfo, int32_t updateID)
{
   /***
   Scanning ZC will update the scrAddr ledger with the ZC txio. Ledgers require
   a block height, which should be the current top block.
   ***/
   auto isZcFromWallet = [&scanInfo, this](const BinaryDataRef zcKey)->bool
   {
      if (scanInfo.saStruct_.newKeysAndScrAddr_ == nullptr)
         return false;

      auto iter = scanInfo.saStruct_.newKeysAndScrAddr_->find(zcKey);
      if (iter == scanInfo.saStruct_.newKeysAndScrAddr_->end() ||
         iter->second == nullptr)
         return false;

      for (const auto& spentSA : *iter->second)
      {
         if (this->hasScrAddress(spentSA))
            return true;
      }

      return false;
   };

   map<BinaryData, TxIOPair> result;
   auto addrMap = scrAddrMap_.get();

   for (auto& saPair : *addrMap)
   {
      auto&& saResult = saPair.second->scanZC(
         scanInfo.saStruct_, isZcFromWallet, updateID);
      result.insert(saResult.begin(), saResult.end());
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
bool BtcWallet::scanWallet(ScanWalletStruct& scanInfo, int32_t updateID)
{
   if (scanInfo.action_ != BDV_ZC)
   {
      //new top block         
      auto&& tx = bdvPtr_->getDB()->beginTransaction(SSH, LMDB::ReadOnly);
      balance_ = getFullBalanceFromDB(updateID);
   }
  
   if (scanInfo.saStruct_.scrAddrToTxioKeys_.size() != 0 ||
      (scanInfo.saStruct_.invalidatedZcKeys_ != nullptr && 
       scanInfo.saStruct_.invalidatedZcKeys_->size() != 0))
   {
      //top block didnt change, only have to check for new ZC
      if (bdvPtr_->isZcEnabled())
      {
         auto&& zcTxios = scanWalletZeroConf(scanInfo, updateID);
         if (scanInfo.saStruct_.newKeysAndScrAddr_ != nullptr &&
            zcTxios.size() > 0)
         {
            auto&& ledgerMap = updateWalletLedgersFromTxio(
               zcTxios, scanInfo.endBlock_ + 1, UINT32_MAX);

            for (auto& zckey : *scanInfo.saStruct_.newKeysAndScrAddr_)
            {
               auto iter = ledgerMap.find(zckey.first);
               if (iter == ledgerMap.end())
                  continue;

               auto& walletZcLedgers =
                  scanInfo.saStruct_.zcLedgers_[walletID()];
               walletZcLedgers.insert(*iter);
            }
         }

         balance_ = getFullBalanceFromDB(updateID);
         updateID_ = updateID;

         //return false because no new block was parsed
         return false;
      }
   }

   updateID_ = updateID;
   return true;
}

////////////////////////////////////////////////////////////////////////////////
void BtcWallet::reset()
{
   clearBlkData();
}

////////////////////////////////////////////////////////////////////////////////
map<uint32_t, uint32_t> BtcWallet::computeScrAddrMapHistSummary()
{
   if (Armory::Config::DBSettings::getDbType() == ARMORY_DB_SUPER)
      return computeScrAddrMapHistSummary_Super();

   struct PreHistory
   {
      uint32_t txioCount_;
      vector<BinaryDataRef> scrAddrs_;

      PreHistory(void) : txioCount_(0) {}
   };

   map<uint32_t, PreHistory> preHistSummary;

   auto addrMap = scrAddrMap_.get();

   auto&& sshtx = bdvPtr_->getDB()->beginTransaction(SSH, LMDB::ReadOnly);
   auto&& subtx = bdvPtr_->getDB()->beginTransaction(SUBSSH, LMDB::ReadOnly);

   
   for (auto& scrAddrPair : *addrMap)
   {
      scrAddrPair.second->mapHistory();
      const map<uint32_t, uint32_t>& txioSum =
         scrAddrPair.second->getHistSSHsummary();

      //keep count of txios at each height with a vector of all related scrAddr
      for (const auto& histPair : txioSum)
      {
         auto& preHistAtHeight = preHistSummary[histPair.first];

         preHistAtHeight.txioCount_ += histPair.second;
         preHistAtHeight.scrAddrs_.push_back(scrAddrPair.first);
      }
   }

   map<uint32_t, uint32_t> histSummary;
   for (auto& preHistAtHeight : preHistSummary)
   {
      if (preHistAtHeight.second.scrAddrs_.size() > 1)
      {
         //get hgtX for height
         uint8_t dupID = bdvPtr_->getDB()->getValidDupIDForHeight(preHistAtHeight.first);
         auto&& hgtX = DBUtils::heightAndDupToHgtx(preHistAtHeight.first, dupID);

         set<BinaryData> txKeys;

         //this height has several txio for several scrAddr, let's look at the
         //txios in detail to reduce the total count for repeating txns.
         for (auto scrAddr : preHistAtHeight.second.scrAddrs_)
         {
            StoredSubHistory subssh;
            if (bdvPtr_->getDB()->getStoredSubHistoryAtHgtX(subssh, scrAddr, hgtX))
            {
               for (auto& txioPair : subssh.txioMap_)
               {
                  if (txioPair.second.hasTxIn())
                     txKeys.insert(txioPair.second.getTxRefOfInput().getDBKey());
                  else
                     txKeys.insert(txioPair.second.getTxRefOfOutput().getDBKey());
               }
            }
         }

         preHistAtHeight.second.txioCount_ = txKeys.size();
      }
   
      histSummary[preHistAtHeight.first] = preHistAtHeight.second.txioCount_;
   }

   return histSummary;
}

////////////////////////////////////////////////////////////////////////////////
map<uint32_t, uint32_t> BtcWallet::computeScrAddrMapHistSummary_Super()
{
   auto addrMap = scrAddrMap_.get();
   auto&& sshtx = bdvPtr_->getDB()->beginTransaction(SSH, LMDB::ReadOnly);

   map<uint32_t, uint32_t> result;

   for (auto& scrAddrPair : *addrMap)
   {
      scrAddrPair.second->mapHistory();
      const map<uint32_t, uint32_t>& txioSum =
         scrAddrPair.second->getHistSSHsummary();

      for (auto& sum : txioSum)
      {
         auto iter = result.find(sum.first);
         if (iter != result.end())
            iter->second += sum.second;
         else
            result.insert(sum);
      }
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
void BtcWallet::mapPages()
{
    /***mapPages seems rather fast (0.6~0.3sec to map the history of wallet
   with 1VayNert, 1Exodus and 100k empty addresses.

   My original plan was to grab the first 100 txn of a wallet to have the first
   page of its history ready for rendering, and parse the rest in a side 
   thread, as I was expecting that process to be long.

   Since my original assumption understimated LMDB's speed, I can instead map 
   the history entirely, then create the first page, as it results in a more 
   consistent txn distribution per page.

   Also taken in consideration is the code in updateLedgers. Ledgers are built
   by ScrAddrObj. The particular call, updateLedgers, expects to parse
   txioPairs in ascending order (lowest to highest height). 

   By gradually parsing history from the top block downward, updateLedgers is
   fed both ascending and descending sets of txioPairs, which would require
   certain in depth amendments to its code to satisfy a behavior that takes 
   place only once per wallet per load.
   ***/
   auto computeSSHsummary = [this](void)->map<uint32_t, uint32_t>
      {return this->computeScrAddrMapHistSummary(); };

   histPages_.mapHistory(computeSSHsummary);
}

////////////////////////////////////////////////////////////////////////////////
bool BtcWallet::isPaged() const
{
   //get address map
   auto addrMap = scrAddrMap_.get();

   for (auto& saPair : *addrMap)
   {
      if (!saPair.second->hist_.isInitiliazed())
         return false;
   }

   return true;
}

////////////////////////////////////////////////////////////////////////////////
map<BinaryData, TxIOPair> BtcWallet::getTxioForRange(
   uint32_t start, uint32_t end) const
{
   map<BinaryData, TxIOPair> outMap;
   auto addrMap = scrAddrMap_.get();

   for (const auto& scrAddrPair : *addrMap)
   {
      auto&& saTxioMap = 
         scrAddrPair.second->getHistoryForScrAddr(start, end, false);
      outMap.insert(saTxioMap.begin(), saTxioMap.end());
   }

   return outMap;
}

////////////////////////////////////////////////////////////////////////////////
map<BinaryData, LedgerEntry> BtcWallet::updateWalletLedgersFromTxio(
   const map<BinaryData, TxIOPair>& txioMap,
   uint32_t startBlock, uint32_t endBlock) const
{
   return LedgerEntry::computeLedgerMap(txioMap, startBlock, endBlock, 
      walletID_, bdvPtr_->getDB(), &bdvPtr_->blockchain(), bdvPtr_->zcContainer());
}

////////////////////////////////////////////////////////////////////////////////
const ScrAddrObj* BtcWallet::getScrAddrObjByKey(const BinaryData& key) const
{
   auto addrMap = scrAddrMap_.get();
   auto saIter = addrMap->find(key);
   if (saIter == addrMap->end()) {
      LOGWARN << "unknown address in btcwallet";
      throw std::runtime_error("unknown address in btcwallet");
   }
   return saIter->second.get();
}

////////////////////////////////////////////////////////////////////////////////
ScrAddrObj& BtcWallet::getScrAddrObjRef(const BinaryData& key)
{
   auto addrMap = scrAddrMap_.get();

   auto saIter = addrMap->find(key);
   if (saIter != addrMap->end())
   {
      return *saIter->second;
   }

   std::ostringstream ss;
   ss << "no ScrAddr matches key " << key.toHexStr() << 
      " in Wallet " << walletID_;
   LOGERR << ss.str();
   throw std::runtime_error(ss.str());
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<const map<BinaryData, LedgerEntry>> BtcWallet::getHistoryPage(
   uint32_t pageId)
{
   if (!bdvPtr_->isBDMRunning())
      return nullptr;

   if (pageId >= getHistoryPageCount())
      throw std::range_error("pageID is out of range");

   auto getTxio = 
      [this](uint32_t start, uint32_t end)->map<BinaryData, TxIOPair>
   { return this->getTxioForRange(start, end); };

   auto computeLedgers = [this](
      const map<BinaryData, TxIOPair>& txioMap, uint32_t start, uint32_t end)->
      map<BinaryData, LedgerEntry>
   { return this->updateWalletLedgersFromTxio(txioMap, start, end); };

   return histPages_.getPageLedgerMap(getTxio, computeLedgers, pageId, updateID_);
}

////////////////////////////////////////////////////////////////////////////////
vector<LedgerEntry> BtcWallet::getHistoryPageAsVector(uint32_t pageId)
{
   auto ledgerMap = getHistoryPage(pageId);

   vector<LedgerEntry> ledgerVec;
   if (ledgerMap == nullptr)
      return ledgerVec;

   for (const auto& ledgerPair : *ledgerMap)
      ledgerVec.push_back(ledgerPair.second);

   return ledgerVec;
}

const HistoryPager& BtcWallet::historyPager() const
{
   return histPages_;
}

////////////////////////////////////////////////////////////////////////////////
void BtcWallet::needsRefresh(bool refresh)
{ 
   //notify BDV
   if (refresh && isRegistered_)
   {
      bdvPtr_->flagRefresh(
         BDV_refreshAndRescan, BinaryData::fromString(walletID_), nullptr);
   }

   //call custom callback
   doneRegisteringCallback_();
   doneRegisteringCallback_ = [](void)->void{};
}

////////////////////////////////////////////////////////////////////////////////
uint64_t BtcWallet::getWltTotalTxnCount(void) const
{
   uint64_t ntxn = 0;

   auto addrMap = scrAddrMap_.get();

   for (const auto& scrAddrPair : *addrMap)
      ntxn += scrAddrPair.second->getTxioCountFromSSH(true);

   return ntxn;
}

////////////////////////////////////////////////////////////////////////////////
void BtcWallet::setConfTarget(unsigned confTarget, const string& hash)
{
   if(confTarget != confTarget_)
      confTarget_ = confTarget;

   if (hash.size() != 0)
   {
      auto&& hashBd = BinaryData::fromString(hash);
      bdvPtr_->flagRefresh(BDV_refreshSkipRescan, hashBd, nullptr);
   }
}

////////////////////////////////////////////////////////////////////////////////
void BtcWallet::unregisterAddresses(const std::set<BinaryDataRef>& addrSet)
{
   vector<BinaryDataRef> bdRefVec(addrSet.size());
   bdRefVec.insert(bdRefVec.end(), addrSet.begin(), addrSet.end());

   scrAddrMap_.erase(bdRefVec);
   histPages_.reset();
}
