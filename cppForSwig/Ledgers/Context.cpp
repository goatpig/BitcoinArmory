////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Context.h"
#include <Utils/BinaryData.h>
#include <BlockchainDatabase/txio.h>
#include <TxClasses.h>
#include <DBClientClasses.h>

using namespace Armory;
using namespace Armory::Ledgers;

////////////////////////////////////////////////////////////////////////////////
// Context
Context::Context(
   const std::map<Types::BlockId, HeaderPtr>& headers,
   std::map<Types::TxKey, Tx> txMap,
   std::set<Types::ScrAddr> scrAddrSet) :
   headers_(headers),
   txMap_{std::move(txMap)},
   scrAddrSet_{std::move(scrAddrSet)}
{}

////////
uint32_t Context::getTimestampForBlockId(Types::BlockId blockId) const
{
   try {
      auto header = headers_.at(blockId);
      return header->timestamp;
   } catch (const std::out_of_range&) {
      LOGWARN << "[getTimestampForBlockId] no block for id " << blockId;
      return UINT32_MAX;
   }
}

uint32_t Context::getHeightForBlockId(Types::BlockId blockId) const
{
   try {
      auto header = headers_.at(blockId);
      return header->blockHeight;
   } catch (const std::out_of_range&) {
      LOGWARN << "[getHeightForBlockId] no block for id " << blockId;
      return UINT32_MAX;
   }
}

////////
const Types::TxHash& Context::getTxHash(Types::TxKey key) const
{
   const auto& tx = getTx(key);
   return tx.getThisHash();
}

////////
size_t Context::getTxOutCount(Types::TxKey key) const
{
   const auto& tx = getTx(key);
   return tx.getNumTxOut();
}

const Tx& Context::getTx(Types::TxKey key) const
{
   auto txIter = txMap_.find(key);
   if (txIter == txMap_.end()) {
      throw std::out_of_range(std::format(
         "missing tx for key {:x}", key));
   }
   return txIter->second;
}

bool Context::filterTxio(const TxIOPair& txio) const
{
   if (scrAddrSet_.empty()) {
      return true;
   }
   return scrAddrSet_.find(txio.getScrAddr()) != scrAddrSet_.end();
}

////////////////////////////////////////////////////////////////////////////////
// DBCache
DBCache::DBCache()
{}

void DBCache::clear()
{
   txMap_.clear();
   txHashToKey_.clear();
   headers_.clear();
}

////////
void DBCache::addHeaders(const std::vector<HeaderPtr>& headerVec)
{
   for (auto& header : headerVec) {
      auto iter = headers_.find(header->blockId);
      if (iter == headers_.end()) {
         iter = headers_.emplace(header->blockId, header).first;
      }
   }
}

HeaderPtr DBCache::getHeader(Types::BlockId blockId) const
{
   auto iter = headers_.find(blockId);
   if (iter == headers_.end()) {
      throw std::out_of_range(std::format("no block for id {}", blockId));
   }
   return iter->second;
}

HeaderPtr DBCache::getHeaderForHeight(uint32_t bheight) const
{
   for (const auto& hPair : headers_) {
      if (bheight == hPair.second->blockHeight) {
         if (hPair.second->isMainBranch) {
            return hPair.second;
         }
      }
   }
   return nullptr;
}

const std::map<Types::BlockId, HeaderPtr>& DBCache::getHeaderMap() const
{
   return headers_;
}

////////
void DBCache::addTx(Tx& tx)
{
   txHashToKey_.emplace(tx.getThisHash(), tx.getDBKey());
   txMap_.emplace(tx.getDBKey(), std::move(tx));
}

void DBCache::eraseTx(Types::TxKey txKey)
{
   auto txIter = txMap_.find(txKey);
   if (txIter == txMap_.end()) {
      return;
   }
   txHashToKey_.erase(txIter->second.getThisHash());
   txMap_.erase(txIter);
}

////////
const Tx& DBCache::getTx(Types::TxKey txKey) const
{
   auto iter = txMap_.find(txKey);
   if (iter == txMap_.end()) {
      throw std::out_of_range(std::format("no tx for key {:x}", txKey));
   }
   return iter->second;
}

const Tx& DBCache::getTxByHash(const Types::TxHash& txHash) const
{
   auto iter = txHashToKey_.find(txHash);
   if (iter == txHashToKey_.end()) {
      throw std::out_of_range(std::format(
         "no kex for txhash {}", txHash.toHexStr()));
   }
   return getTx(iter->second);
}

////////
std::map<Types::TxHash, Types::TxKey> DBCache::getHashesStartingKey(
   Types::TxKey txKey) const
{
   std::map<Types::TxHash, Types::TxKey> result;
   auto txIter = txMap_.lower_bound(txKey);
   while (txIter != txMap_.end()) {
      result.emplace(txIter->second.getThisHash(), txIter->first);
      ++txIter;
   }
   return result;
}

std::set<Types::TxKey> DBCache::purgeTxs(
   const std::set<Types::TxHash>& invalidHashes)
{
   std::set<Types::TxKey> result;
   for (const auto& txHash : invalidHashes) {
      auto keyIter = txHashToKey_.find(txHash);
      if (keyIter == txHashToKey_.end()) {
         continue;
      }
      txMap_.erase(keyIter->second);
      result.emplace(keyIter->second);
      txHashToKey_.erase(keyIter);
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
// namespace Ledgers functions
Context Ledgers::prepareContext(
   const std::map<Types::TxIOKey, TxIOPair>& txioMap,
   std::shared_ptr<const DBCache> dbCache,
   std::set<Types::ScrAddr> scrAddrSet)
{
   std::set<Types::TxKey> txKeys;

   /* 1. gather all relevant tx keys */
   for (const auto& txioPair : txioMap) {
      txKeys.emplace(txioPair.second.getTxKeyOfOutput());
      if (txioPair.second.hasTxIn()) {
         txKeys.emplace(txioPair.second.getTxKeyOfInput());
      }
   }

   /* 2. grab all txs for relevant tx keys */
   std::map<Types::TxKey, Tx> txMap;
   for (const auto& txKey : txKeys) {
      txMap.emplace(txKey, dbCache->getTx(txKey));
   }

   return Context{dbCache->getHeaderMap(), std::move(txMap), std::move(scrAddrSet)};
}
