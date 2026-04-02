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
#include <algorithm>
#include <cstring>

#include "LedgerEntry.h"
#include <Utils/DBUtils.h>
#include <Utils/BtcUtils.h>
#include <BlockchainDatabase/BlockObj.h>
#include <BlockchainDatabase/txio.h>
#include "Context.h"

using namespace Armory;
using namespace Armory::Ledgers;

namespace
{
   using TxDbKey = BinaryDataRef;
   struct TxData
   {
      uint32_t blockNum;
      uint32_t txTime;
      uint16_t txIndex;
      BinaryData txHash;
      std::map<BinaryData, const TxIOPair*> txios;
   };

   std::map<TxDbKey, TxData> sortByTx(
      const std::map<BinaryData, TxIOPair>& txioMap,
      const Context& ctx)
   {
      std::map<TxDbKey, TxData> txnTxIOMap;
      for (const auto& txio : txioMap) {
         if (!ctx.filterTxio(txio.second)) {
            continue;
         }

         //txout
         auto txOutDBKey = txio.second.getTxRefOfOutput().getDBKeyRef();
         auto txOutIter = txnTxIOMap.find(txOutDBKey);
         if (txOutIter == txnTxIOMap.end()) {
            txOutIter = txnTxIOMap.emplace(txOutDBKey, TxData{}).first;
         }
         txOutIter->second.txios.emplace(
            txio.second.getDBKeyOfOutput(), &txio.second);

         //txin
         if (!txio.second.hasTxIn()) {
            continue;
         }
         auto txInDBKey = txio.second.getTxRefOfInput().getDBKeyRef();
         auto txInIter = txnTxIOMap.find(txInDBKey);
         if (txInIter == txnTxIOMap.end()) {
            txInIter = txnTxIOMap.emplace(txInDBKey, TxData{}).first;
         }
         auto insertResult = txInIter->second.txios.emplace(
            txio.second.getDBKeyOfOutput(), &txio.second);
         if (!insertResult.second) {
            insertResult.first->second = &txio.second;
         }
      }
      return txnTxIOMap;
   }

   void resolveTxnData(std::map<TxDbKey, TxData>& txnMap, const Context& ctx)
   {
      //get txhash, block, txIndex and txtime
      for (auto& txPair : txnMap) {
         bool isZc = txPair.first.startsWith(DBUtils::ZCPrefix);
         if (!isZc) {
            txPair.second.blockNum = DBUtils::hgtxToHeight(
               txPair.first.getSliceRef(0, 4));
            txPair.second.txIndex = READ_UINT16_BE(
               txPair.first.getSliceRef(4, 2));
            txPair.second.txTime = ctx.getTimestampForBlockHeight(
               txPair.second.blockNum);
         } else {
            txPair.second.blockNum = UINT32_MAX;
            txPair.second.txIndex = READ_UINT32_BE(
               txPair.first.getSliceRef(2, 4));

            if (txPair.second.txios.empty()) {
               LOGWARN << "have a tx with no attached txios";
            } else {
               auto txioIter = txPair.second.txios.begin();
               txPair.second.txTime = txioIter->second->getTxTime();
            }
         }
         txPair.second.txHash = ctx.getTxHash(txPair.first);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
// LedgerEntry
Entry::Entry(const std::string& ID,
   int64_t val, uint32_t blkNum, const BinaryData& txHash,
   uint32_t idx, uint32_t txtime,
   std::set<BinaryData>& scrAddrSet,
   bool isCoinbase, bool isToSelf, bool isChange,
   bool isOptInRBF, bool usesWitness, bool isChainedZC) :
   ID_(ID), value_(val), blockNum_(blkNum),
   txHash_(txHash), index_(idx), txTime_(txtime),
   scrAddrSet_{std::move(scrAddrSet)},
   isCoinbase_(isCoinbase), isSentToSelf_(isToSelf), isChangeBack_(isChange),
   isOptInRBF_(isOptInRBF), usesWitness_(usesWitness), isChainedZC_(isChainedZC)
{}

////////
std::string Entry::getWalletID() const
{
   return ID_;
}

const std::set<BinaryData>& Entry::getScrAddrList() const
{
   return scrAddrSet_;
}

int64_t Entry::getValue() const
{
   return value_;
}

uint32_t Entry::getBlockNum() const
{
   return blockNum_;
}
const BinaryData& Entry::getTxHash() const
{
   return txHash_;
}

uint32_t Entry::getIndex() const
{
   return index_;
}

uint32_t Entry::getTxTime() const
{
   return txTime_;
}

bool Entry::isCoinbase() const
{
   return isCoinbase_;
}

bool Entry::isSentToSelf() const
{
   return isSentToSelf_;
}

bool Entry::isChangeBack() const
{
   return isChangeBack_;
}

bool Entry::isOptInRBF() const
{
   return isOptInRBF_;
}

bool Entry::usesWitness() const
{
   return usesWitness_;
}

bool Entry::isChainedZC() const
{
   return isChainedZC_;
}

////////
bool Entry::operator<(const Entry& le2) const
{
   if (blockNum_ < le2.blockNum_) {
      return true;
   } else if (blockNum_ == le2.blockNum_) {
      if (index_ < le2.index_) {
         return true;
      } else if (index_ == le2.index_) {
         return value_ < le2.value_;
      }
   }
   return false;
}

bool Entry::operator==(const Entry& le2) const
{
   return blockNum_ == le2.blockNum_ && index_ == le2.index_;
}

bool Entry::operator>(const Entry& le2) const
{
   if (blockNum_ > le2.blockNum_) {
      return true;
   } else if (blockNum_ == le2.blockNum_) {
      if (index_ > le2.index_) {
         return true;
      } else if (index_ == le2.index_) {
         return value_ > le2.value_;
      }
   }
   return false;
}

ScriptPrefix Entry::getScriptType() const
{
   return (ScriptPrefix)ID_[0];
}

//////////////////////////////////////////////////////////////////////////////
void Entry::pprint()
{
   std::cout << "LedgerEntry: " << std::endl;
   std::cout << "   ID      : " << getWalletID() << std::endl;
   std::cout << "   Value   : " << getValue()/1e8 << std::endl;
   std::cout << "   BlkNum  : " << getBlockNum() << std::endl;
   std::cout << "   TxHash  : " <<
      getTxHash().copySwapEndian().toHexStr() << std::endl;
   std::cout << "   TxIndex : " << getIndex() << std::endl;
   std::cout << "   Coinbase: " << (isCoinbase() ? 1 : 0) << std::endl;
   std::cout << "   sentSelf: " << (isSentToSelf() ? 1 : 0) << std::endl;
   std::cout << "   isChange: " << (isChangeBack() ? 1 : 0) << std::endl;
   std::cout << "   isOptInRBF: " << (isOptInRBF() ? 1 : 0) << std::endl;
   for (const auto& addr : scrAddrSet_) {
      std::cout << "   scrAddr: " << addr.toHexStr() << std::endl;
   }
   std::cout << std::endl;
}

void Entry::pprintOneLine() const
{
   printf("   Addr:%s Tx:%s:%02d   BTC:%0.3f   Blk:%06d\n",
      "   ",
      getTxHash().getSliceCopy(0,8).toHexStr().c_str(),
      getIndex(),
      getValue()/1e8,
      getBlockNum()
   );
}

//////////////////////////////////////////////////////////////////////////////
void Entry::purgeLedgerMapFromHeight(
   std::map<BinaryData, Entry>& leMap, uint32_t purgeFrom)
{
   //Remove all entries starting this height, included.
   BinaryData cutOffHeight(6);
   auto heightPtr = cutOffHeight.getPtr();

   uint8_t* purgeFromPtr = reinterpret_cast<uint8_t*>(&purgeFrom);
   memset(heightPtr, 0, 6);
   heightPtr[0] = purgeFromPtr[2];
   heightPtr[1] = purgeFromPtr[1];
   heightPtr[2] = purgeFromPtr[0];

   auto cutOffIterPair = leMap.equal_range(cutOffHeight);
   leMap.erase(cutOffIterPair.first, leMap.end());
}

void Entry::purgeLedgerVectorFromHeight(
  std::vector<Entry>& leVec, uint32_t purgeFrom)
{
   //Remove all entries starting this height, included.
   uint32_t i = 0;
   std::sort(leVec.begin(), leVec.end());
   for (const auto& le : leVec) {
      if (le.getBlockNum() >= purgeFrom) {
         break;
      }
      i++;
   }
   leVec.erase(leVec.begin() + i, leVec.end());
}

//////////////////////////////////////////////////////////////////////////////
std::map<BinaryData, Entry> Ledgers::computeLedgerMap(
   const std::map<BinaryData, TxIOPair>& txioMap,
   uint32_t startBlock, uint32_t endBlock, const std::string& id,
   const Context& ctx)
{
   auto txnTxIOMap = sortByTx(txioMap, ctx);
   resolveTxnData(txnTxIOMap, ctx);

   //convert TxIO to ledgers
   std::map<BinaryData, Entry> leMap;
   for (auto& txnPair : txnTxIOMap) {
      auto& txnData = txnPair.second;
      if (txnData.blockNum < startBlock || txnData.blockNum > endBlock) {
         continue;
      }

      bool isZc         = txnData.blockNum == UINT32_MAX ? true : false;
      bool isRBF        = false;
      bool usesWitness  = false;
      bool isChained    = false;
      bool isCoinbase   = false;

      int64_t value = 0;
      int64_t valIn = 0, valOut = 0;
      uint32_t nTxInAreOurs = 0, nTxOutAreOurs = 0;

      std::set<BinaryData> scrAddrSet;
      for (const auto& txioPair : txnData.txios) {
         const auto& txio = *txioPair.second;
         if (txnData.blockNum == UINT32_MAX) {
            if (txio.isRBF()) {
               isRBF = true;
            }
            if (txio.getTxTime() > txnData.txTime) {
               txnData.txTime = txio.getTxTime();
            }
         }

         if (txio.getTxRefOfOutput().getDBKey().startsWith(txnPair.first)) {
            isCoinbase |= txio.isFromCoinbase();
            valIn += txio.getValue();
            value += txio.getValue();
            nTxOutAreOurs++;
         }

         if (txio.hasTxIn() &&
            txio.getTxRefOfInput().getDBKey().startsWith(txnPair.first)) {
            valOut -= txio.getValue();
            value -= txio.getValue();
            nTxInAreOurs++;

            if (txio.isChainedZC()) {
               isChained = true;
            }
         }

         scrAddrSet.emplace(ctx.getScrAddrForTxOut(txio));
      }

      bool isSentToSelf = false;
      bool isChangeBack = false;
      if (nTxInAreOurs * nTxOutAreOurs > 0) {
         //if some of the txins AND some of the txouts are ours, this could be an STS
         //pull the txn and compare the txin and txout counts
         if (ctx.getTxOutCount(txnPair.first) == nTxOutAreOurs) {
            value = valIn;
            isSentToSelf = true;
         }
      } else if (nTxInAreOurs != 0 && (valIn + valOut) < 0) {
         isChangeBack = true;
      }

      leMap.emplace(txnPair.first, Entry{
         id, value,
         txnData.blockNum, txnData.txHash,
         txnData.txIndex, txnData.txTime,
         scrAddrSet,
         isCoinbase, isSentToSelf, isChangeBack, isRBF,
         usesWitness, isChained}
      );
   }
   return leMap;
}

////////////////////////////////////////////////////////////////////////////////
// comparator
bool DescendingOrder::operator()(
   const Entry& a, const Entry& b) const
{
   return a > b;
}

////////////////////////////////////////////////////////////////////////////////
// Delegate
Delegate::Delegate(
   std::function<std::vector<Entry>(uint32_t)> getHist,
   std::function<uint32_t(uint32_t)> getBlock,
   std::function<uint32_t(uint32_t)> getPageId,
   std::function<uint32_t(void)> getPageCount) :
   getHistoryPage_(getHist),
   getBlockInVicinity_(getBlock),
   getPageIdForBlockHeight_(getPageId),
   getPageCount_(getPageCount)
{}

////////
std::vector<Entry> Delegate::getHistoryPage(uint32_t id) const
{
   return getHistoryPage_(id);
}

uint32_t Delegate::getBlockInVicinity(uint32_t blk) const
{
   return getBlockInVicinity_(blk);
}

uint32_t Delegate::getPageIdForBlockHeight(uint32_t blk) const
{
   return getPageIdForBlockHeight_(blk);
}

uint32_t Delegate::getPageCount() const
{
   return getPageCount_();
}
