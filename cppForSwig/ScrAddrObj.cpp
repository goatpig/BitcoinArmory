////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ScrAddrObj.h"
#include <Utils/BitcoinSettings.h>
#include <Utils/BtcUtils.h>
#include <Utils/DBUtils.h>
#include <BlockchainDatabase/lmdb_wrapper.h>
#include <BlockchainDatabase/Blockchain.h>
#include <BlockchainDatabase/txio.h>
#include <BlockchainDatabase/BlockObj.h>
#include <BlockchainDatabase/StoredBlockObj.h>
#include <ZeroConf/Utils.h>
#include <ZeroConf/Parser.h>
#include <Ledgers/LedgerEntry.h>
#include <Ledgers/Context.h>
#include "BitcoinP2P.h"

using namespace Armory;

////////////////////////////////////////////////////////////////////////////////
// ScrAddrObj Methods
ScrAddrObj::ScrAddrObj(LMDBBlockDatabase *db, const Blockchain *bc,
   ZeroConf::ZeroConfContainer* zc, BinaryDataRef addr) :
   db_(db), bc_(bc), zc_(zc), scrAddr_(addr), utxos_(this)
{}

////////////////////////////////////////////////////////////////////////////////
uint64_t ScrAddrObj::getSpendableBalance(uint32_t currBlk) const
{
   //TODO: this call is way too expensive, improve it
   uint64_t balance = getFullBalance();
   auto txios = getTxios(0, UINT32_MAX);
   for (const auto& txio : txios) {
      if (!txio.second.hasTxIn() && !txio.second.isSpendable(currBlk)) {
         balance -= txio.second.getValue();
      }
   }
   return balance;
}

////////////////////////////////////////////////////////////////////////////////
uint64_t ScrAddrObj::getUnconfirmedBalance(
   uint32_t currBlk, unsigned confTarget) const
{
   //TODO: this call is way too expensive, improve it
   uint64_t balance = 0;
   auto txios = getTxios(0, UINT32_MAX);
   for (const auto& txio : txios) {
      if (txio.second.hasTxIn()) {
         continue;
      } else if (txio.second.isUnconfirmed(currBlk, confTarget)) {
         balance += txio.second.getValue();
      }
   }
   return balance;
}

////////////////////////////////////////////////////////////////////////////////
uint64_t ScrAddrObj::getFullBalance(unsigned updateID) const
{
   //grab mined balance
   StoredScriptHistory ssh;
   db_->getStoredScriptHistorySummary(ssh, scrAddr_);
   uint64_t balance = ssh.getScriptBalance(false);

   //grab zc balances
   auto zcTxios = getTxios(UINT32_MAX, UINT32_MAX);
   for (const auto& txio : zcTxios) {
      if (txio.second.hasTxOutZC()) {
         balance += txio.second.getValue();
      }
      if (txio.second.hasTxInZC()) {
         balance -= txio.second.getValue();
      }
   }

   if (balance != internalBalance_) {
      internalBalance_ = balance;
      if (updateID != UINT32_MAX) {
         updateID_ = updateID;
      }
   }
   return balance;
}

////////////////////////////////////////////////////////////////////////////////
void ScrAddrObj::clearBlkData(void)
{
   hist_.reset();
   totalTxioCount_ = 0;
}

////////////////////////////////////////////////////////////////////////////////
std::map<BinaryData, TxIOPair> ScrAddrObj::scanZC(
   const ScanAddressStruct& scanInfo,
   std::function<bool(const BinaryDataRef)> isZcFromWallet,
   int32_t updateID)
{
   //Dont use a reference for this loop. We check and set the isFromSelf flag
   //in this operation, which is based on the wallet this scrAddr belongs to.
   //The txio comes straight from the ZC container object, which only deals 
   //with scrAddr. Since several wallets may reference the same scrAddr, we 
   //can't modify original txio, so we use a copy.
   std::set<BinaryData> invalidatedInputs;
   std::set<BinaryData> invalidatedOutputs;

   if (scanInfo.invalidatedZcKeys_ != nullptr &&
      !scanInfo.invalidatedZcKeys_->empty()) {
      //check zc inputs that affect this scrAddrObj
      for (const auto& inputKey : zcInputKeys_) {
         auto zcIter = scanInfo.invalidatedZcKeys_->find(
            inputKey.first.getSliceRef(0, 6));
         if (zcIter != scanInfo.invalidatedZcKeys_->end()) {
            invalidatedInputs.emplace(inputKey.first);
         }
      }

      //as well as outputs (txios are keyed by outputs)
      for (const auto& txioPair : zcTxios_) {
         auto zcIter = scanInfo.invalidatedZcKeys_->find(
            txioPair.first.getSliceRef(0, 6));
         if (zcIter != scanInfo.invalidatedZcKeys_->end()) {
            invalidatedOutputs.emplace(txioPair.first);
         }
      }
   }

   //purge if necessary
   if (!invalidatedInputs.empty() || !invalidatedOutputs.empty()) {
      if (purgeZC(invalidatedInputs, invalidatedOutputs)) {
         updateID_ = updateID;
      }
   }

   auto haveIter = scanInfo.scrAddrToTxioKeys_.find(scrAddr_);
   if (haveIter == scanInfo.scrAddrToTxioKeys_.end()) {
      return {};
   } else if (haveIter->second.empty()) {
      LOGWARN << "empty zc notification txio map";
      return {};
   }

   //look for new keys
   std::map<BinaryData, TxIOPair> newZCs;
   const auto& txioKeys = haveIter->second;
   for (const auto& txiokey : txioKeys) {
      auto newtxio = scanInfo.zcState_->getTxioByKey(txiokey);
      if (newtxio == nullptr) {
         continue;
      }
      auto emplaceResult = newZCs.emplace(txiokey, *newtxio);
      if (newtxio->hasTxInZC()) {
         zcInputKeys_[newtxio->getDBKeyOfInput()] = txiokey;
         if (!emplaceResult.second) {
            emplaceResult.first->second.merge(*newtxio);
         }
      }
   }

   //nothing to do if we didn't find new ZC
   if (newZCs.empty()) {
      return {};
   }
   updateID_ = updateID;

   for (auto& txioPair : newZCs) {
      if (txioPair.second.hasTxOutZC() &&
         isZcFromWallet(txioPair.second.getDBKeyOfOutput().getSliceRef(0, 6))) {
         txioPair.second.setTxOutFromSelf(true);
      }
      auto emplaceResult = zcTxios_.emplace(txioPair);
      if (!emplaceResult.second && txioPair.second.hasTxIn()) {
         emplaceResult.first->second.merge(txioPair.second);
      }
   }
   return newZCs;
}

////////////////////////////////////////////////////////////////////////////////
bool ScrAddrObj::purgeZC(
   const std::set<BinaryData>& invalidatedInputs,
   const std::set<BinaryData>& invalidatedOutputs)
{
   bool purged = false;
   for (const auto& outputKey : invalidatedOutputs) {
      //purge from zcTxios_
      auto txioIter = zcTxios_.find(outputKey);
      if (txioIter != zcTxios_.end()) {
         purged = true;
         zcTxios_.erase(txioIter);
      }
   }

   for (const auto& inputKey : invalidatedInputs) {
      auto inputIter = zcInputKeys_.find(inputKey);
      if (inputIter == zcInputKeys_.end()) {
         continue;
      }

      auto outputIter = zcTxios_.find(inputIter->second);
      if (outputIter != zcTxios_.end()) {
         auto& txio = outputIter->second;
         if (txio.getDBKeyOfInput() != inputIter->first) {
            continue;
         }

         if (!txio.hasTxOutZC()) {
            zcTxios_.erase(outputIter);
         } else {
            txio.setTxIn({});
         }
      }
      zcInputKeys_.erase(inputIter);
      purged = true;
   }
   return purged;
}

////////////////////////////////////////////////////////////////////////////////
std::map<BinaryData, Ledgers::Entry> ScrAddrObj::updateLedgers(
   const std::map<BinaryData, TxIOPair>& txioMap,
   uint32_t startBlock, uint32_t endBlock) const
{
   auto ctx = Ledgers::prepareContext(txioMap, *bc_, db_, zc_->getSnapshot());
   return Ledgers::computeLedgerMap(txioMap,
      startBlock, endBlock, {}, ctx);
}

////////////////////////////////////////////////////////////////////////////////
uint64_t ScrAddrObj::getTxioCountFromSSH(bool withZc) const
{
   StoredScriptHistory ssh;
   db_->getStoredScriptHistorySummary(ssh, scrAddr_);
   uint32_t count = ssh.totalTxioCount;

   if (withZc) {
      auto zcTxios = getTxios(UINT32_MAX, UINT32_MAX);
      for (const auto& txio : zcTxios) {
         if (txio.second.hasTxOutZC() || txio.second.hasTxInZC()) {
            ++count;
         }
      }
   }
   return count;
}

///////////////////////////////////////////////////////////////////////////////
std::map<BinaryData, TxIOPair> ScrAddrObj::getTxios(
   uint32_t startBlock, uint32_t endBlock, bool withMultisig) const
{
   std::map<BinaryData, TxIOPair> outMap;

   //grab txio range from ssh
   StoredScriptHistory ssh;
   auto start = startBlock;
   db_->getStoredScriptHistory(ssh, scrAddr_, start, endBlock);

   //update scrAddrObj containers
   totalTxioCount_ = ssh.totalTxioCount;

   if (endBlock != UINT32_MAX) {
      lastSeenBlock_ = endBlock;
   } else if (lastSeenBlock_ == 0) {
      lastSeenBlock_ = bc_->top()->getBlockHeight();
   }

   if (ssh.isInitialized()) {
      //Serve content as a map. Do not overwrite existing TxIOs to avoid wiping ZC
      //data. Since the data isn't overwritten, iterate the map from its end to make
      //sure newer txio aren't ignored due to older ones being inserted first.
      auto subSSHiter = ssh.subHistMap.rbegin();
      while (subSSHiter != ssh.subHistMap.rend()) {
         StoredSubHistory& subssh = subSSHiter->second;
         for (auto &txioPair : subssh.txioMap) {
            if (withMultisig || !txioPair.second.isMultisig()) {
               outMap.emplace(txioPair);
            }
         }
         ++subSSHiter;
      }
   }

   if (endBlock == UINT32_MAX) {
      for (const auto& zcTxio : zcTxios_) {
         auto iter = outMap.find(zcTxio.first);
         if (iter == outMap.end()) {
            outMap.emplace(zcTxio);
            continue;
         }
         iter->second.merge(zcTxio.second);
      }
   }
   return outMap;
}

////////////////////////////////////////////////////////////////////////////////
std::vector<Ledgers::Entry> ScrAddrObj::getHistoryPageById(uint32_t id)
{
   if (id > hist_.getPageCount()) {
      throw std::range_error("pageId out of range");
   }
   auto getTxio = [this](uint32_t start, uint32_t end)->
   std::map<BinaryData, TxIOPair>
   {
      return this->getTxios(start, end);
   };

   auto buildLedgers = [this](
      const std::map<BinaryData, TxIOPair>& txioMap,
      uint32_t start, uint32_t end)->
   std::map<BinaryData, Ledgers::Entry>
   {
      return this->updateLedgers(txioMap, start, end);
   };

   auto leMap = hist_.getPageLedgerMap(getTxio, buildLedgers, id, updateID_);
   return getTxLedgerAsVector(leMap.get());
}

////////////////////////////////////////////////////////////////////////////////
void ScrAddrObj::mapHistory()
{
   //create history map
   hist_.mapHistory(
      [this]()->std::map<uint32_t, uint32_t>
      { return db_->getSSHSummary(this->getScrAddr()); }
   );
}

////////////////////////////////////////////////////////////////////////////////
ScrAddrObj& ScrAddrObj::operator=(const ScrAddrObj& rhs)
{
   if (&rhs == this) {
      return *this;
   }

   this->db_ = rhs.db_;
   this->bc_ = rhs.bc_;
   this->scrAddr_ = rhs.scrAddr_;

   this->totalTxioCount_ = rhs.totalTxioCount_;
   this->lastSeenBlock_ = rhs.lastSeenBlock_;

   //prebuild history indexes for quick fetch from ssh
   this->hist_ = rhs.hist_;
   this->utxos_.reset();
   this->utxos_.scrAddrObj = this;
   return *this;
}

////////////////////////////////////////////////////////////////////////////////
std::vector<Ledgers::Entry> ScrAddrObj::getTxLedgerAsVector(
   const std::map<BinaryData, Ledgers::Entry>* leMap) const
{
   std::vector<Ledgers::Entry>le;
   if (leMap == nullptr) {
      return le;
   }
   for (auto& lePair : *leMap) {
      le.emplace_back(lePair.second);
   }
   return le;
}

////////////////////////////////////////////////////////////////////////////////
bool ScrAddrObj::getMoreUTXOs(std::function<bool(const BinaryData&)> spentByZC)
{
   return getMoreUTXOs(utxos_, spentByZC);
}

////////////////////////////////////////////////////////////////////////////////
bool ScrAddrObj::getMoreUTXOs(PagedUTXOs& utxos,
   std::function<bool(const BinaryData&)> spentByZC) const
{
   return utxos.fetchMoreUTXO(spentByZC);
}

////////////////////////////////////////////////////////////////////////////////
std::vector<UnspentTxOut> ScrAddrObj::getAllUTXOs(
   std::function<bool(const BinaryData&)> hasTxOutInZC) const
{
   PagedUTXOs utxos(this);
   while (getMoreUTXOs(utxos, hasTxOutInZC)) {}

   //start a RO txn to grab the txouts from DB
   auto&& tx = db_->beginTransaction(DB_SELECT::STXO, LMDB::Mode::ReadOnly);

   std::vector<UnspentTxOut> utxoList;
   uint32_t blk = bc_->top()->getBlockHeight();

   for (const auto& txioPair : utxos.utxoList) {
      if (!txioPair.second.isSpendable(blk)) {
         continue;
      }
      auto txout_key = txioPair.second.getDBKeyOfOutput();
      StoredTxOut stxo;
      db_->getStoredTxOut(stxo, txout_key);
      auto hash = db_->getTxHashForLdbKey(txout_key.getSliceRef(0, 6));

      BinaryData script(stxo.getScriptRef());
      utxoList.emplace_back(UnspentTxOut{
         hash, txioPair.second.getIndexOfOutput(), stxo.getHeight(),
         stxo.getValue(), script}
      );
   }
   return utxoList;
}

////////////////////////////////////////////////////////////////////////////////
std::vector<UnspentTxOut> ScrAddrObj::getFullTxOutList(uint32_t currBlk,
   bool ignoreZc) const
{
   if (currBlk == 0) {
      currBlk = UINT32_MAX;
   }
   if (currBlk != UINT32_MAX) {
      ignoreZc = true;
   }
   auto utxoVec = getSpendableTxOutList(ignoreZc);

   auto utxoIter = utxoVec.rbegin();
   uint32_t cutOff = UINT32_MAX;

   while (utxoIter != utxoVec.rend()) {
      if (utxoIter->getTxHeight() <= currBlk) {
         cutOff = utxoVec.size() - (utxoIter - utxoVec.rbegin());
         break;
      }
   }

   utxoVec.erase(utxoVec.begin() + cutOff, utxoVec.end());
   return utxoVec;
}

////////////////////////////////////////////////////////////////////////////////
std::vector<UnspentTxOut> ScrAddrObj::getSpendableTxOutList(
   bool ignoreZc) const
{
   StoredScriptHistory ssh;
   std::map<BinaryData, UnspentTxOut> utxoMap;
   db_->getStoredScriptHistory(ssh, scrAddr_);
   db_->getFullUTXOMapForSSH(ssh, utxoMap, false);

   auto txios = getTxios(0, UINT32_MAX);
   std::vector<UnspentTxOut> utxoVec;
   for (auto& utxo : utxoMap) {
      auto txioIter = txios.find(utxo.first);
      if (txioIter != txios.end()) {
         if (txioIter->second.hasTxInZC()) {
            continue;
         }
      }
      utxoVec.emplace_back(utxo.second);
   }

   if (ignoreZc) {
      return utxoVec;
   }

   auto tx = db_->beginTransaction(DB_SELECT::STXO, LMDB::Mode::ReadOnly);
   for (const auto& txio : txios) {
      if (!txio.second.hasTxOutZC()) {
         continue;
      }
      if (txio.second.hasTxInZC()) {
         continue;
      }

      auto txout_key = txio.second.getDBKeyOfOutput();
      StoredTxOut stxo;
      db_->getStoredTxOut(stxo, txout_key);
      auto hash = db_->getTxHashForLdbKey(txout_key.getSliceRef(0, 6));

      BinaryData script{stxo.getScriptRef()};
      utxoVec.emplace_back(UnspentTxOut{
         hash, txio.second.getIndexOfOutput(), stxo.getHeight(),
         stxo.getValue(), script});
   }
   return utxoVec;
}

////////
uint32_t ScrAddrObj::getBlockInVicinity(uint32_t blk) const
{
   //expect history has been computed, it will throw otherwise
   return hist_.getBlockInVicinity(blk);
}

uint32_t ScrAddrObj::getPageIdForBlockHeight(uint32_t blk) const
{
   //same as above
   return hist_.getPageIdForBlockHeight(blk);
}

uint32_t ScrAddrObj::getTxioCountForLedgers()
{
   //return UINT32_MAX unless count has changed since last call
   //(or it's the first call)
   auto count = getTxioCountFromSSH(false);
   if (count == txioCountForLedgers_) {
      return UINT32_MAX;
   }
   txioCountForLedgers_ = count;
   return count;
}

////////////////////////////////////////////////////////////////////////////////
// PagedUTXOs
ScrAddrObj::PagedUTXOs::PagedUTXOs(const ScrAddrObj* scrAddrObj) :
   scrAddrObj(scrAddrObj)
{}

const std::map<BinaryData, TxIOPair>& ScrAddrObj::PagedUTXOs::getUTXOs() const
{
   return utxoList;
}

bool ScrAddrObj::PagedUTXOs::fetchMoreUTXO(
   const std::function<bool(const BinaryData&)>& spentByZC)
{
   //return true if more UTXO were found, false otherwise
   if (topBlock < scrAddrObj->bc_->top()->getBlockHeight()) {
      uint32_t rangeTop;
      uint32_t count = 0;
      do {
         rangeTop = scrAddrObj->hist_.getRangeForHeightAndCount(
            topBlock, UTXOperFetch);
         count += fetchMoreUTXO(topBlock, rangeTop, spentByZC);
      } while (count < UTXOperFetch && rangeTop != UINT32_MAX);

      if (count > 0) {
         return true;
      }
   }
   return false;
}

uint32_t ScrAddrObj::PagedUTXOs::fetchMoreUTXO(uint32_t start, uint32_t end,
   const std::function<bool(const BinaryData&)>& spentByZC)
{
   uint32_t nutxo = 0;
   uint64_t val = 0;

   StoredScriptHistory ssh;
   scrAddrObj->db_->getStoredScriptHistory(
      ssh, scrAddrObj->scrAddr_, start, end);

   for (const auto& subsshPair : ssh.subHistMap) {
      for (const auto& txioPair : subsshPair.second.txioMap) {
         if (txioPair.second.isUTXO()) {
            //isMultisig only signifies this scrAddr was used in the
            //composition of a funded multisig transaction. This is purely
            //meta-data and shouldn't be returned as a spendable txout
            if (txioPair.second.isMultisig()) {
               continue;
            }

            if (spentByZC(txioPair.second.getDBKeyOfOutput())) {
               continue;
            }

            if (utxoList.emplace(txioPair).second) {
               val += txioPair.second.getValue();
               nutxo++;
            }
         }
      }
   }

   topBlock = end;
   value += val;
   count += nutxo;
   return nutxo;
}

uint64_t ScrAddrObj::PagedUTXOs::getValue() const
{
   return value;
}

uint32_t ScrAddrObj::PagedUTXOs::getCount() const
{
   return count;
}

void ScrAddrObj::PagedUTXOs::reset()
{
   topBlock = 0;
   value = 0;
   count = 0;
   utxoList.clear();
}

void ScrAddrObj::PagedUTXOs::addZcUTXOs(
   const std::map<BinaryData, TxIOPair>& txioMap)
{
   for (const auto& txio : txioMap) {
      if (!txio.first.startsWith(DBUtils::ZCPrefix)) {
         continue;
      }
      if (txio.second.hasTxIn()) {
         continue;
      }
      utxoList.emplace(txio);
   }
}
