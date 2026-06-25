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
      LOGWARN << "no block for id " << blockId;
      return UINT32_MAX;
   }
}

uint32_t Context::getHeightForBlockId(Types::BlockId blockId) const
{
   try {
      auto header = headers_.at(blockId);
      return header->blockHeight;
   } catch (const std::out_of_range&) {
      LOGWARN << "no block for id " << blockId;
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
   return txMap_.at(key);
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
void DBCache::addHeaders(const std::vector<HeaderPtr>& headerVec)
{
   for (auto& header : headerVec) {
      auto iter = headers.find(header->blockId);
      if (iter == headers.end()) {
         iter = headers.emplace(header->blockId, header).first;
      }
   }
}

HeaderPtr DBCache::getHeaderForHeight(uint32_t bheight) const
{
   for (const auto& hPair : headers) {
      if (bheight == hPair.second->blockHeight) {
         if (hPair.second->isMainBranch) {
            return hPair.second;
         }
      }
   }
   return nullptr;
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
      txMap.emplace(txKey, dbCache->txMap.at(txKey));
   }

   return Context{dbCache->headers, std::move(txMap), std::move(scrAddrSet)};
}
