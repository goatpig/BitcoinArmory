////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Context.h"
#include <Utils/BinaryData.h>
#include <Utils/DBUtils.h>
#include <BlockchainDatabase/BlockObj.h>
#include <BlockchainDatabase/Blockchain.h>
#include <BlockchainDatabase/txio.h>
#include <BlockchainDatabase/lmdb_wrapper.h>
#include <ZeroConf/Utils.h>
#include <DBClientClasses.h>

using namespace Armory;
using namespace Armory::Ledgers;

////////////////////////////////////////////////////////////////////////////////
// Context
Context::Context(std::map<uint32_t, uint32_t> timestamps,
   std::map<BinaryData, Tx>& txMap,
   std::map<BinaryData, std::map<uint32_t, BinaryData>>& txioKeyToScrAddr,
   std::set<BinaryData> scrAddrSet) :
   timestamps_(std::move(timestamps)),
   txMap_{std::move(txMap)},
   txioKeyToScrAddr_{std::move(txioKeyToScrAddr)},
   scrAddrSet_{std::move(scrAddrSet)}
{}

uint32_t Context::getTimestampForBlockHeight(uint32_t blockNum) const
{
   try {
      return timestamps_.at(blockNum);
   } catch (const std::out_of_range&) {
      LOGWARN << "no timestamp for block " << blockNum;
      return UINT32_MAX;
   }
}

const BinaryData& Context::getTxHash(BinaryDataRef key) const
{
   const auto& tx = getTx(key);
   return tx.getThisHash();
}

size_t Context::getTxOutCount(BinaryDataRef key) const
{
   const auto& tx = getTx(key);
   return tx.getNumTxOut();
}

const Tx& Context::getTx(BinaryDataRef key) const
{
   return txMap_.at(key);
}

const BinaryData& Context::getScrAddrForTxOut(const TxIOPair& txio) const
{
   return txioKeyToScrAddr_.at(txio.getTxRefOfOutput().getDBKey()).at(
      txio.getIndexOfOutput());
}

bool Context::filterTxio(const TxIOPair& txio) const
{
   if (scrAddrSet_.empty()) {
      return true;
   }
   const auto& scrAddr = getScrAddrForTxOut(txio);
   return scrAddrSet_.find(scrAddr) != scrAddrSet_.end();
}

////////////////////////////////////////////////////////////////////////////////
// DBCache
void DBCache::addBlocks(std::vector<DBClientClasses::BlockHeader>& blkVec)
{
   for (auto& blk : blkVec) {
      auto iter = blocks.find(blk.getBlockHeight());
      if (iter == blocks.end()) {
         iter = blocks.emplace(blk.getBlockHeight(), Blocks{}).first;
      }

      auto dupId = blk.getDupId();
      iter->second.mainChain = dupId;
      iter->second.blocks.emplace(dupId, std::move(blk));
   }
}

bool DBCache::isHeightValid(uint32_t height, uint8_t dupId) const
{
   auto iter = blocks.find(height);
   if (iter == blocks.end()) {
      return false;
   }
   return dupId == iter->second.mainChain;
}

////////////////////////////////////////////////////////////////////////////////
// namespace functions
Context Ledgers::prepareContext(
   const std::map<BinaryData, TxIOPair>& txioMap,
   const Blockchain& bc,
   LMDBBlockDatabase* db,
   std::shared_ptr<const ZeroConf::MempoolSnapshot> zcSs)
{
   std::map<uint32_t, uint32_t> timestamps;
   std::set<BinaryData> txKeys;

   /* 1. gather all tx keys, set timestamps */
   for (const auto& txioPair : txioMap) {
      /* output */
      const auto& txKeyOut = txioPair.second.getTxRefOfOutput().getDBKey();
      txKeys.emplace(txKeyOut);
      BinaryDataRef txInKeyRef;
      if (txioPair.second.hasTxIn()) {
         txInKeyRef = txioPair.second.getTxRefOfInput().getDBKeyRef();
         txKeys.emplace(BinaryData{txInKeyRef});
      }

      if (txKeyOut.startsWith(DBUtils::ZCPrefix)) {
         continue;
      }

      //block timestamp
      auto blockNum = DBUtils::hgtxToHeight(txKeyOut.getSliceRef(0, 4));
      if (timestamps.find(blockNum) == timestamps.end()) {
         try {
            auto headerPtr = bc.getHeaderByHeight(blockNum, 0xFF);
            timestamps.emplace(blockNum, headerPtr->getTimestamp());
         } catch (const std::range_error&) {
            LOGWARN << "no block for height " << blockNum;
            continue;
         }
      }

      /* input */
      if (txInKeyRef.empty()) {
         continue;
      }
      if (txInKeyRef.startsWith(DBUtils::ZCPrefix)) {
         continue;
      }

      //block timestamp
      blockNum = DBUtils::hgtxToHeight(txInKeyRef.getSliceRef(0, 4));
      if (timestamps.find(blockNum) == timestamps.end()) {
         try {
            auto headerPtr = bc.getHeaderByHeight(blockNum, 0xFF);
            timestamps.emplace(blockNum, headerPtr->getTimestamp());
         } catch (const std::range_error&) {
            LOGWARN << "no block for height " << blockNum;
            continue;
         }
      }
   }

   /* 2. grab all txs */
   std::map<BinaryData, Tx> txMap;
   for (const auto& txKey : txKeys) {
      if (!txKey.startsWith(DBUtils::ZCPrefix)) {
         //we don't use this anymore
         txMap.emplace(txKey, db->getFullTxCopy(0, nullptr));
      } else {
         auto ptx = zcSs->getTxByKey(txKey);
         txMap.emplace(txKey, ptx->getTxObj());
      }
   }

   /* 3. resolve output addresses */
   std::map<BinaryData, std::map<uint32_t, BinaryData>> txioKeyToScrAddr;
   for (const auto& txioPair : txioMap) {
      //output
      const auto& txKeyOut = txioPair.second.getTxRefOfOutput().getDBKey();
      const auto& outTx = txMap.at(txKeyOut);
      auto iterOut = txioKeyToScrAddr.find(txKeyOut);
      if (iterOut == txioKeyToScrAddr.end()) {
         iterOut = txioKeyToScrAddr.emplace(
            txKeyOut, std::map<uint32_t, BinaryData>{}).first;
      }
      auto indexOut = txioPair.second.getIndexOfOutput();
      iterOut->second.emplace(indexOut, outTx.getScrAddrForTxOut(indexOut));
   }
   return Context{timestamps, txMap, txioKeyToScrAddr, {}};
}

Context Ledgers::prepareContext(
   const std::map<BinaryData, TxIOPair>& txioMap,
   std::shared_ptr<const DBCache> dbCache,
   std::set<BinaryData> scrAddrSet)
{
   std::set<BinaryData> txKeys;

   /* 1. gather all tx keys */
   for (const auto& txioPair : txioMap) {
      const auto& txKeyOut = txioPair.second.getTxRefOfOutput().getDBKey();
      txKeys.emplace(txKeyOut);
      BinaryDataRef txInKeyRef;
      if (txioPair.second.hasTxIn()) {
         txInKeyRef = txioPair.second.getTxRefOfInput().getDBKeyRef();
         txKeys.emplace(BinaryData{txInKeyRef});
      }
   }

   /* 2. grab all txs */
   std::map<BinaryData, Tx> txMap;
   for (const auto& txKey : txKeys) {
      txMap.emplace(txKey, dbCache->txMap.at(txKey));
   }

   /* 3. resolve output addresses */
   std::map<BinaryData, std::map<uint32_t, BinaryData>> txioKeyToScrAddr;
   for (const auto& txioPair : txioMap) {
      //output
      const auto& txKeyOut = txioPair.second.getTxRefOfOutput().getDBKey();
      const auto& outTx = txMap.at(txKeyOut);
      auto iterOut = txioKeyToScrAddr.find(txKeyOut);
      if (iterOut == txioKeyToScrAddr.end()) {
         iterOut = txioKeyToScrAddr.emplace(
            txKeyOut, std::map<uint32_t, BinaryData>{}).first;
      }
      auto indexOut = txioPair.second.getIndexOfOutput();
      iterOut->second.emplace(indexOut, outTx.getScrAddrForTxOut(indexOut));
   }

   /* 4. timestamps */
   std::map<uint32_t, uint32_t> timestamps;
   for (const auto& blockPair : dbCache->blocks) {
      try {
         const auto& block = blockPair.second.blocks.at(blockPair.second.mainChain);
         timestamps.emplace(blockPair.first, block.getTimestamp());
      } catch (const std::out_of_range&) {
         LOGWARN << "missing block: " <<
            blockPair.first << "|" << blockPair.second.mainChain;
         continue;
      }
   }
   return Context{timestamps, txMap, txioKeyToScrAddr, std::move(scrAddrSet)};
}
