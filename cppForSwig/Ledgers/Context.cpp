////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Context.h"
#include <algorithm>
#include <Utils/BinaryData.h>
#include <BlockchainDatabase/txio.h>
#include <TxClasses.h>
#include <DBClientClasses.h>
#include <Utils/log.h>

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
// TxKeyCandidates
TxKeyCandidates::Kind TxKeyCandidates::kindOf(Types::TxKey txKey)
{
   if (!Types::isTxKeyValid(txKey) || Types::isThisATxIOKey(txKey)) {
      return Kind::Invalid;
   }
   if (Types::isThisAnUnpooledKey(txKey)) {
      return Kind::Unpooled;
   }
   if (Types::isThisAZCKey(txKey)) {
      return Kind::Pooled;
   }
   return Kind::Mined;
}

void TxKeyCandidates::add(Types::TxKey txKey)
{
   auto kind = kindOf(txKey);
   if (kind == Kind::Invalid ||
      std::find(keys_.begin(), keys_.end(), txKey) != keys_.end()) {
      return;
   }

   //ahead of the first key of the same kind or below
   auto iter = std::find_if(keys_.begin(), keys_.end(),
      [kind](Types::TxKey key)->bool { return kindOf(key) <= kind; });
   keys_.insert(iter, txKey);
}

bool TxKeyCandidates::remove(Types::TxKey txKey)
{
   auto iter = std::find(keys_.begin(), keys_.end(), txKey);
   if (iter == keys_.end()) {
      return false;
   }
   keys_.erase(iter);
   return true;
}

bool TxKeyCandidates::empty() const
{
   return keys_.empty();
}

Types::TxKey TxKeyCandidates::best() const
{
   if (keys_.empty()) {
      return Types::INVALID_TX_KEY;
   }
   return keys_.front();
}

const std::vector<Types::TxKey>& TxKeyCandidates::keys() const
{
   return keys_;
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
   nextUnpooledId_ = 0;
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
   auto txKey = tx.getDBKey();
   const auto& txHash = tx.getThisHash();
   if (TxKeyCandidates::kindOf(txKey) == TxKeyCandidates::Kind::Invalid) {
      //nothing could find it by key, and its hash would point nowhere
      LOGWARN << std::format("not caching tx {} with invalid key {:x}",
         txHash.toHexStr(), txKey);
      return;
   }

   //zc ids can be reused after ArmoryDB restarts, a key can come back for
   //another tx: drop the old tx along with its hash's candidate
   auto txIter = txMap_.find(txKey);
   if (txIter != txMap_.end() && txIter->second.getThisHash() != txHash) {
      eraseTx(txKey);
   }

   txHashToKey_[txHash].add(txKey);
   txMap_.emplace(txKey, std::move(tx));
}

Types::TxKey DBCache::addUnpooledTx(const BinaryData& rawTx)
{
   Tx tx(rawTx);
   auto hashIter = txHashToKey_.find(tx.getThisHash());
   if (hashIter != txHashToKey_.end()) {
      //the cache already has a copy of this tx
      return hashIter->second.best();
   }

   auto txKey = Types::constructUnpooledKey(nextUnpooledId_++);
   tx.setTxKey(txKey);
   addTx(tx);
   return txKey;
}

void DBCache::eraseTx(Types::TxKey txKey)
{
   auto txIter = txMap_.find(txKey);
   if (txIter == txMap_.end()) {
      return;
   }

   auto hashIter = txHashToKey_.find(txIter->second.getThisHash());
   if (hashIter != txHashToKey_.end()) {
      hashIter->second.remove(txKey);
      if (hashIter->second.empty()) {
         txHashToKey_.erase(hashIter);
      }
   }
   txMap_.erase(txIter);
}

void DBCache::eraseHashes(const std::set<Types::TxHash>& txHashes)
{
   //drop every copy of these hashes
   for (const auto& txHash : txHashes) {
      auto hashIter = txHashToKey_.find(txHash);
      if (hashIter == txHashToKey_.end()) {
         continue;
      }

      //eraseTx edits the candidates, iterate over a copy
      auto txKeys = hashIter->second.keys();
      for (auto txKey : txKeys) {
         eraseTx(txKey);
      }
   }
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
   return getTx(iter->second.best());
}

std::vector<Types::TxKey> DBCache::getKeysForHash(
   const Types::TxHash& txHash) const
{
   //best first, as TxKeyCandidates orders them
   auto iter = txHashToKey_.find(txHash);
   if (iter == txHashToKey_.end()) {
      return {};
   }
   return iter->second.keys();
}

////////
std::set<Types::TxKey> DBCache::purgeTxs(
   const std::set<Types::TxHash>& invalidHashes)
{
   //drop the copies no block holds: zc and unpooled, mined copies stay
   std::set<Types::TxKey> result;
   for (const auto& txHash : invalidHashes) {
      auto keyIter = txHashToKey_.find(txHash);
      if (keyIter == txHashToKey_.end()) {
         continue;
      }
      for (auto txKey : keyIter->second.keys()) {
         if (TxKeyCandidates::kindOf(txKey) !=
            TxKeyCandidates::Kind::Mined) {
            result.emplace(txKey);
         }
      }
   }
   for (auto txKey : result) {
      eraseTx(txKey);
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
