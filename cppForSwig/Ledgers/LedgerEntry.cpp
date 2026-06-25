////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2026, goatpig                                          //
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
   struct TxData
   {
      Types::BlockId blockId;
      uint32_t height;
      uint32_t txTime;
      Types::TxId txIndex;
      Types::ZcId zcIndex = UINT32_MAX;
      Types::TxHash txHash;
      std::map<Types::TxIOKey, const TxIOPair*> txios;
   };

   std::map<Types::TxKey, TxData> sortByTx(
      const std::map<Types::TxIOKey, TxIOPair>& txioMap,
      const Context& ctx)
   {
      std::map<Types::TxKey, TxData> txnTxIOMap;
      for (const auto& txio : txioMap) {
         if (!ctx.filterTxio(txio.second)) {
            continue;
         }

         //txout
         auto txOutKey = txio.second.getTxKeyOfOutput();
         auto txOutIter = txnTxIOMap.find(txOutKey);
         if (txOutIter == txnTxIOMap.end()) {
            txOutIter = txnTxIOMap.emplace(txOutKey, TxData{}).first;
         }
         txOutIter->second.txios.emplace(
            txio.second.getTxIOKeyOfOutput(), &txio.second);

         //txin
         if (!txio.second.hasTxIn()) {
            continue;
         }
         auto txInKey = txio.second.getTxKeyOfInput();
         auto txInIter = txnTxIOMap.find(txInKey);
         if (txInIter == txnTxIOMap.end()) {
            txInIter = txnTxIOMap.emplace(txInKey, TxData{}).first;
         }
         auto insertResult = txInIter->second.txios.emplace(
            txio.second.getTxIOKeyOfOutput(), &txio.second);
         if (!insertResult.second) {
            insertResult.first->second = &txio.second;
         }
      }
      return txnTxIOMap;
   }

   void resolveTxnData(
      std::map<Types::TxKey, TxData>& txnMap,
      const Context& ctx)
   {
      //get txhash, block, txIndex and txtime
      for (auto& txPair : txnMap) {
         auto& txData = txPair.second;
         if (!Types::isThisAZCKey(txPair.first)) {
            txData.blockId = Types::getBlockIDFromTxKey(txPair.first);
            txData.height = ctx.getHeightForBlockId(txData.blockId);
            txData.txIndex = Types::getTxIndexFromTxKey(txPair.first);
            txData.txTime = ctx.getTimestampForBlockId(txData.blockId);
         } else {
            txData.blockId = Types::INVALID_BLOCK_ID;
            txData.height = UINT32_MAX;
            txData.txIndex = UINT16_MAX;
            txData.zcIndex = Types::getZcIdFromTxKey(txPair.first);

            if (txData.txios.empty()) {
               LOGWARN << "have a tx with no attached txios";
            } else {
               auto txioIter = txData.txios.begin();
               txData.txTime = txioIter->second->getTxTime();
            }
         }
         txData.txHash = ctx.getTxHash(txPair.first);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
// LedgerEntry
Entry::Entry(const std::string& ID,
   Types::Value val, uint32_t blkNum, const Types::TxHash& txHash,
   uint32_t idx, uint32_t txtime,
   std::set<Types::ScrAddr>& scrAddrSet,
   bool isCoinbase, bool isToSelf, bool isChange,
   bool isOptInRBF, bool isChainedZC) :
   ID_(ID), value_(val), blockNum_(blkNum),
   txHash_(txHash), index_(idx), txTime_(txtime),
   scrAddrSet_{std::move(scrAddrSet)},
   isCoinbase_(isCoinbase), isSentToSelf_(isToSelf), isChangeBack_(isChange),
   isOptInRBF_(isOptInRBF), isChainedZC_(isChainedZC)
{}

////////
const std::string& Entry::getWalletID() const
{
   return ID_;
}

const std::set<Types::ScrAddr>& Entry::getScrAddrList() const
{
   return scrAddrSet_;
}

Types::Value Entry::getValue() const
{
   return value_;
}

uint32_t Entry::getBlockNum() const
{
   return blockNum_;
}
const Types::TxHash& Entry::getTxHash() const
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
std::map<Types::TxKey, Entry> Ledgers::computeLedgerMap(
   const std::map<Types::TxIOKey, TxIOPair>& txioMap,
   uint32_t startBlock, uint32_t endBlock, const std::string& id,
   const Context& ctx)
{
   auto txnTxIOMap = sortByTx(txioMap, ctx);
   resolveTxnData(txnTxIOMap, ctx);

   //convert TxIO to ledgers
   std::map<Types::TxKey, Entry> leMap;
   for (auto& txnPair : txnTxIOMap) {
      auto& txnData = txnPair.second;
      if (txnData.height < startBlock || txnData.height > endBlock) {
         continue;
      }

      bool isZc         = txnData.zcIndex == UINT32_MAX ? false : true;
      bool isRBF        = false;
      bool isChained    = false;
      bool isCoinbase   = isZc ? false : txnData.txIndex == 0;

      Types::Value value = 0;
      Types::Value valIn = 0, valOut = 0;
      uint32_t nTxInAreOurs = 0, nTxOutAreOurs = 0;

      std::set<Types::ScrAddr> scrAddrSet;
      for (const auto& txioPair : txnData.txios) {
         const auto& txio = *txioPair.second;
         if (isZc) {
            if (txio.isRBF()) {
               isRBF = true;
            }
            if (txio.getTxTime() > txnData.txTime) {
               txnData.txTime = txio.getTxTime();
            }
         }

         if (txio.getTxKeyOfOutput() == txnPair.first) {
            valIn += txio.getAmount();
            value += txio.getAmount();
            nTxOutAreOurs++;
         }

         if (txio.hasTxIn() && txio.getTxKeyOfInput() == txnPair.first) {
            valOut -= txio.getAmount();
            value -= txio.getAmount();
            nTxInAreOurs++;

            if (txio.isChained()) {
               isChained = true;
            }
         }

         scrAddrSet.emplace(txio.getScrAddr());
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
         txnData.height, txnData.txHash,
         isZc ? txnData.zcIndex : txnData.txIndex, txnData.txTime,
         scrAddrSet,
         isCoinbase, isSentToSelf, isChangeBack, isRBF, isChained}
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
