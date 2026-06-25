////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2026, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "TxIOCache.h"

#include <Utils/DBUtils.h>
#include <Utils/BtcUtils.h>
#include <BlockchainDatabase/txio.h>
#include <Ledgers/Context.h>
#include <TxClasses.h>
#include <AsyncClient.h>
#include "Notifications.h"

using namespace Armory;
using namespace Armory::Bridge;

namespace
{
   constexpr Types::TxKey firstZCKey = 0xFFFF000000000000;

   uint32_t getConfCount(Types::TxKey txKey, uint32_t top,
      std::shared_ptr<Ledgers::DBCache> cache)
   {
      if (Types::isThisAZCKey(txKey)) {
         return 0;
      }

      auto blockId = Types::getBlockIDFromTxKey(txKey);

      auto header = cache->headers.at(blockId);
      return top - header->blockHeight + 1;
   }

   bool isSpendable(Types::TxKey txKey, uint32_t top,
      std::shared_ptr<Ledgers::DBCache> cache)
   {
      // spendable TxOuts are ones with at least 1 confirmation
      auto txId = Types::getTxIndexFromTxKey(txKey);
      auto nConf = getConfCount(txKey, top, cache);
      if (txId == 0 && nConf < COINBASE_MATURITY) {
         return false;
      } else if (nConf > 0) {
         return true;
      } else {
         return false;
      }
   }

   bool isUnconfirmed(Types::TxKey txKey, uint32_t top,
      std::shared_ptr<Ledgers::DBCache> cache)
   {
      auto txId = Types::getTxIndexFromTxKey(txKey);
      auto nConf = getConfCount(txKey, top, cache);
      if (txId == 0) {
         return nConf < COINBASE_MATURITY;
      } else {
         return nConf < MIN_CONFIRMATIONS;
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
// TxIOCache
TxIOCache::TxIOCache() :
   dbCache_{std::make_shared<Ledgers::DBCache>()}
{}

std::shared_ptr<const Ledgers::DBCache> TxIOCache::getDBCache() const
{
   return std::const_pointer_cast<const Ledgers::DBCache>(dbCache_);
}

void TxIOCache::purge()
{
   lastKnownBlock_ = UINT32_MAX;
   unspentTxios_.clear();
   spentTxios_.clear();
   zcTxios_.clear();
   dbCache_->txMap.clear();
   dbCache_->headers.clear();
}

////////
CacheResolveResult TxIOCache::resolve(
   const AddressFilter& filter, uint32_t fromHeight) const
{
   //run through unspent txios first
   CacheResolveResult result{lastKnownBlock_, false, dbCache_};
   for (const auto& txio : unspentTxios_) {
      const auto& txKey = txio.second.getTxKeyOfOutput();

      //skip txios that are not on the main chain
      if (!txKeyIsValid(txKey)) {
         continue;
      }

      //does this txout trigger our filter
      const auto& scrAddr = txio.second.getScrAddr();
      if (filter(scrAddr)) {
         result.addTxio(txio.first, txio.second, scrAddr);
      }
   }

   //check spent txios now
   for (const auto& txio : spentTxios_) {
      //is output on mainchain?
      const auto& txKeyOutput = txio.second.getTxKeyOfOutput();
      if (!txKeyIsValid(txKeyOutput)) {
         continue;
      }

      //is input on mainchain?
      const auto& txKeyInput = txio.second.getTxKeyOfInput();
      if (!txKeyIsValid(txKeyInput)) {
         continue;
      }

      //do we have an unspent txio in the result map?
      auto txioKey = txio.second.getTxIOKeyOfOutput();
      auto iter = result.txioMap.find(txioKey);
      if (iter != result.txioMap.end()) {
         //we do, merge in the txin
         iter->second.merge(txio.second);
         continue;
      }

      //we don't, is the txout relevant then?
      const auto& scrAddr = txio.second.getScrAddr();
      if (filter(scrAddr)) {
         result.addTxio(txioKey, txio.second, scrAddr);
      }
   }
   return result;
}

CacheResolveResult TxIOCache::resolveZC(const AddressFilter& filter) const
{
   CacheResolveResult result{lastKnownBlock_, true, dbCache_};
   for (const auto& txio : zcTxios_) {
      //do we have an unspent txio in the result map?
      auto txioKey = txio.second.getTxIOKeyOfOutput();
      auto iter = result.txioMap.find(txioKey);
      if (iter != result.txioMap.end()) {
         //we do, merge in the txin
         iter->second.merge(txio.second);
         continue;
      }

      //we don't, is the txout relevant then?
      const auto& scrAddr = txio.second.getScrAddr();
      if (filter(scrAddr)) {
         result.addTxio(txioKey, txio.second, scrAddr);
      }
   }
   return result;
}

////////
void TxIOCache::updateBlockBranching(const NewBlockNotif& reorgNotif)
{
   if (!reorgNotif.isReorg()) {
      return;
   }

   for (auto blockId : reorgNotif.invalidatedBlockIds()) {
      auto iter = dbCache_->headers.find(blockId);
      if (iter != dbCache_->headers.end()) {
         iter->second->isMainBranch = false;
      }
   }

   for (auto blockId : reorgNotif.newMainBranchBlockIds()) {
      auto iter = dbCache_->headers.find(blockId);
      if (iter != dbCache_->headers.end()) {
         iter->second->isMainBranch = true;
      }
   }
}

bool TxIOCache::txKeyIsValid(const Types::TxKey& txKey) const
{
   auto blockId = Types::getBlockIDFromTxKey(txKey);
   auto iter = dbCache_->headers.find(blockId);
   if (iter == dbCache_->headers.end()) {
      return false;
   }
   return iter->second->isMainBranch;
}

////////
uint32_t TxIOCache::update(
   std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr,
   std::shared_ptr<NotifStruct> notif)
{
   if (notif->type == NotifType::ZC) {
      auto zcPtr = std::dynamic_pointer_cast<NotifStruct_ZC>(notif);
      updateZC(bdvPtr, zcPtr->txios, zcPtr->invalidatedZCHashes, true);
      return UINT32_MAX;
   }

   NewBlockNotif blockNotif{UINT32_MAX, UINT32_MAX, {}, {}};
   if (notif->type == NotifType::NEWBLOCK) {
      auto blockPtr =
         std::dynamic_pointer_cast<NotifStruct_NewBlock>(notif);
      blockNotif = blockPtr->blockNotif;
   }
   uint32_t fromHeight = 0;
   if (blockNotif.isReorg()) {
      fromHeight = blockNotif.getBranchHeight() + 1;
      updateBlockBranching(blockNotif);
   } else if (notif->type != NotifType::REFRESH) {
      fromHeight = lastKnownBlock_ + 1;
   }

   //1. txios
   auto promTxios = std::make_shared<std::promise<std::vector<TxIOPair>>>();
   auto futTxios = promTxios->get_future();

   bdvPtr->getTxios(fromHeight, [prom = promTxios]
      (ReturnMessage<std::vector<TxIOPair>> result) {
         prom->set_value(result.get());
   });
   auto txios = futTxios.get();

   auto fetchedHeight = notif->type == NotifType::REFRESH ?
      UINT32_MAX : blockNotif.getHeight();
   auto missingStuff = addTxios(txios, fetchedHeight);
   auto missingTxKeys = std::move(missingStuff.first);
   auto missingBlockIds = std::move(missingStuff.second);

   //2. missing txs
   auto promTxs = std::make_shared<std::promise<std::vector<Tx>>>();
   auto futTxs = promTxs->get_future();
   bdvPtr->getTxsByKey(missingTxKeys, [prom = promTxs]
      (ReturnMessage<std::vector<Tx>> result) {
         prom->set_value(result.get());
   });

   //3. missing headers
   auto promHeader = std::make_shared<
      std::promise<std::vector<std::shared_ptr<DBClientClasses::BlockHeader>>>>();
   auto futHeaders = promHeader->get_future();
   AsyncClient::Blockchain bc{*bdvPtr};
   bc.getHeadersById(missingBlockIds, [prom = promHeader]
      (ReturnMessage<std::vector<std::shared_ptr<DBClientClasses::BlockHeader>>> result) {
         prom->set_value(result.get());
      }
   );

   //4. commit it all to the cache
   auto txs = futTxs.get();
   auto headers = futHeaders.get();
   ReentrantLock lock(this);

   //5. prune mined tx from dbCache
   std::map<Types::TxHash, Types::TxKey> zcHashes;
   {
      auto txIter = dbCache_->txMap.lower_bound(firstZCKey);
      while (txIter != dbCache_->txMap.end()) {
         zcHashes.emplace(txIter->second.getThisHash(), txIter->first);
         ++txIter;
      }
   }

   for (auto& tx : txs) {
      //prune mined tx from dbCache
      auto zcIter = zcHashes.find(tx.getThisHash());
      if (zcIter != zcHashes.end()) {
         dbCache_->txMap.erase(zcIter->second);
         dbCache_->txHashToKey.erase(tx.getThisHash());
      }

      //add fresh tx
      dbCache_->txHashToKey.emplace(tx.getThisHash(), tx.getDBKey());
      dbCache_->txMap.emplace(tx.getDBKey(), std::move(tx));
   }
   dbCache_->addHeaders(headers);
   return fromHeight;
}

std::set<Types::TxKey> TxIOCache::updateZC(
   std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr,
   const std::vector<TxIOPair>& zcTxios,
   const std::set<BinaryData>& invalidatedZC, bool append)
{
   ReentrantLock lock(this);
   auto& txMap = dbCache_->txMap;
   if (!append) {
      //append is false, clear up the zc map and apply
      //fresh set of zc txios
      zcTxios_.clear();
   }
   if (!invalidatedZC.empty()) {
      //we have invalidated zc txs, let's purge them
      std::set<Types::TxKey> invalidatedKeys;
      auto txIter = txMap.lower_bound(firstZCKey);
      while (txIter != txMap.end()) {
         auto& tx = txIter->second;
         if (invalidatedZC.find(tx.getThisHash()) != invalidatedZC.end()) {
            //also gather the invalidated keys
            invalidatedKeys.emplace(txIter->first);
            txMap.erase(txIter++);
         } else {
            ++txIter;
         }
      }

      //now purge the txio map
      auto txioIter = zcTxios_.begin();
      while (txioIter != zcTxios_.end()) {
         auto& zcTxio = txioIter->second;
         const auto& outKey = zcTxio.getTxKeyOfOutput();
         if (invalidatedKeys.find(outKey) != invalidatedKeys.end()) {
            //output is invalidated, get rid of the txio
            zcTxios_.erase(txioIter++);
            continue;
         }

         if (zcTxio.hasTxIn()) {
            const auto& inKey = zcTxio.getTxKeyOfInput();
            if (invalidatedKeys.find(inKey) != invalidatedKeys.end()) {
               //input is invalidated, reset it
               zcTxio.setTxIn({});
            }
         }
         ++txioIter;
      }
   }

   std::set<Types::TxKey> missingTxKeys;
   for (const auto& zcTxio : zcTxios) {
      auto zcOutKey = zcTxio.getTxKeyOfOutput();
      if (txMap.find(zcOutKey) == txMap.end()) {
         missingTxKeys.emplace(zcOutKey);
      }

      if (zcTxio.hasTxIn()) {
         auto zcInKey = zcTxio.getTxKeyOfInput();
         if (txMap.find(zcInKey) == txMap.end()) {
            missingTxKeys.emplace(zcInKey);
         }
      }

      auto insertResult = zcTxios_.emplace(zcTxio.getTxIOKeyOfOutput(), zcTxio);
      if (!insertResult.second) {
         insertResult.first->second.merge(zcTxio);
      }
      if (insertResult.first->second.hasTxOutZC()) {
         insertResult.first->second.setChained(true);
      }
   }

   if (append) {
      auto promTxs = std::make_shared<std::promise<std::vector<Tx>>>();
      auto futTxs = promTxs->get_future();
      bdvPtr->getTxsByKey(missingTxKeys, [prom = promTxs]
         (ReturnMessage<std::vector<Tx>> result) {
            prom->set_value(result.get());
      });
      auto txs = futTxs.get();

      for (auto& tx : txs) {
         txMap.emplace(tx.getDBKey(), std::move(tx));
      }
      return {};
   } else {
      return missingTxKeys;
   }
}

////////
std::pair<std::set<Types::TxKey>, std::set<Types::BlockId>>
TxIOCache::addTxios(
   std::vector<TxIOPair>& txios, uint32_t fetchedHeight)
{
   std::set<Types::TxKey> missingTxKeys;
   std::set<Types::BlockId> missingBlocks;

   auto addKey = [&missingTxKeys, &missingBlocks, this](Types::TxKey key)
   {
      auto blockId = Types::getBlockIDFromTxKey(key);
      if (dbCache_->headers.find(blockId) == dbCache_->headers.end()) {
         missingBlocks.emplace(blockId);
      }
      if (dbCache_->txMap.find(key) == dbCache_->txMap.end()) {
         missingTxKeys.emplace(key);
      }
   };

   std::vector<TxIOPair> zcTxios;
   zcTxios.reserve(5);
   ReentrantLock lock(this);
   for (auto& txio : txios) {
      if (!txio.hasTxOutZC()) {
         addKey(txio.getTxKeyOfOutput());
      } else {
         zcTxios.emplace_back(txio);
      }

      if (txio.hasTxIn()) {
         if (txio.hasTxInZC()) {
            zcTxios.emplace_back(txio);
            continue;
         }
         addKey(txio.getTxKeyOfInput());
         spentTxios_.emplace(txio.getTxIOKeyOfInput(), std::move(txio));
      } else if (!txio.hasTxOutZC()) {
         unspentTxios_.emplace(txio.getTxIOKeyOfOutput(), std::move(txio));
      }
   }

   auto zcMissingKeys = updateZC(nullptr, zcTxios, {}, false);
   missingTxKeys.insert(zcMissingKeys.begin(), zcMissingKeys.end());

   if (fetchedHeight != UINT32_MAX) {
      lastKnownBlock_ = fetchedHeight;
   }
   return {std::move(missingTxKeys), std::move(missingBlocks)};
}

////////
std::map<Types::ScrAddr, std::set<Types::TxHash>> TxIOCache::getAddressBook(
   const AddressFilter& filter) const
{
   std::map<Types::ScrAddr, std::set<Types::TxHash>> result;
   for (const auto& txio : unspentTxios_) {
      const auto& txKey = txio.second.getTxKeyOfOutput();

      //skip txios that are not on the main chain
      if (!txKeyIsValid(txKey)) {
         continue;
      }

      //track this <addr, hash>
      const auto& scrAddr = txio.second.getScrAddr();
      if (filter(scrAddr)) {
         auto iter = result.find(scrAddr);
         if (iter == result.end()) {
            iter = result.emplace(scrAddr, std::set<Types::TxHash>{}).first;
         }
         const auto& tx = dbCache_->txMap.at(txKey);
         iter->second.emplace(tx.getThisHash());
      }
   }

   for (const auto& txio : spentTxios_) {
      //is output on mainchain?
      const auto& txKeyOutput = txio.second.getTxKeyOfOutput();
      if (!txKeyIsValid(txKeyOutput)) {
         continue;
      }

      //is input on mainchain?
      const auto& txKeyInput = txio.second.getTxKeyOfInput();
      if (!txKeyIsValid(txKeyInput)) {
         continue;
      }

      //track this <addr, hash>
      const auto& scrAddr = txio.second.getScrAddr();
      if (filter(scrAddr)) {
         auto iter = result.find(scrAddr);
         if (iter == result.end()) {
            iter = result.emplace(scrAddr, std::set<Types::TxHash>{}).first;
         }
         const auto& tx = dbCache_->txMap.at(txKeyOutput);
         iter->second.emplace(tx.getThisHash());
      }
   }
   return result;
}

std::vector<UTXO> TxIOCache::getZcUTXOs(bool rbf,
   const AddressFilter& filter) const
{
   std::vector<UTXO> result;
   auto addTxio = [this, &result, filter](const TxIOPair& txio, const Tx& tx)
   {
      const auto& scrAddr = txio.getScrAddr();
      if (!filter(scrAddr)) {
         return;
      }

      auto txOut = tx.getTxOutCopy(txio.getIndexOfOutput());

      uint32_t height = UINT32_MAX;
      if (!Types::isThisAZCKey(tx.getDBKey())) {
         auto header = dbCache_->headers.at(tx.getBlockId());
         height = header->blockHeight;
      }
      result.emplace_back(UTXO{txio.getAmount(),
         height, tx.getTxIndex(), txio.getIndexOfOutput(),
         tx.getThisHash(), txOut.getScript()
      });
   };

   for (const auto& txio : zcTxios_) {
      const auto& txKey = txio.second.getTxKeyOfOutput();

      if (rbf) {
         //for rbf outputs, we consider all outputs that are RBF flagged
         if (txio.second.isRBF()) {
            const auto& tx = dbCache_->txMap.at(txKey);
            addTxio(txio.second, tx);
         }
         continue;
      }

      if (txio.second.hasTxIn()) {
         //for zc utxos, we ignore spent ones
         continue;
      }

      if (!txio.second.hasTxOutZC()) {
         //and the output has to be zc as well
         continue;
      }

      const auto& tx = dbCache_->txMap.at(txKey);
      addTxio(txio.second, tx);
   }
   return result;
}

std::vector<UTXO> TxIOCache::getUTXOs(
   uint64_t value, bool zc, bool rbf,
   const AddressFilter& filter) const
{
   if (zc || rbf) {
      return getZcUTXOs(rbf, filter);
   }

   uint64_t total = 0;
   std::set<Types::TxKey> spentKeys;
   for (const auto& txio : spentTxios_) {
      auto outputKey = txio.second.getTxIOKeyOfOutput();
      if (!txKeyIsValid(outputKey)) {
         continue;
      }
      spentKeys.emplace(outputKey);
   }

   std::vector<UTXO> result;
   result.reserve(unspentTxios_.size());
   for (const auto& txio : unspentTxios_) {
      auto iter = spentKeys.find(txio.first);
      if (iter != spentKeys.end()) {
         continue;
      }

      const auto& txKey = txio.second.getTxKeyOfOutput();
      if (!txKeyIsValid(txKey)) {
         continue;
      }
      const auto& tx = dbCache_->txMap.at(txKey);
      auto header = dbCache_->headers.at(tx.getBlockId());
      if (tx.getTxIndex() == 0 &&
         header->blockHeight + COINBASE_MATURITY > lastKnownBlock_) {
         continue;
      }
      if (!filter(txio.second.getScrAddr())) {
         continue;
      }

      auto txOut = tx.getTxOutCopy(txio.second.getIndexOfOutput());
      result.emplace_back(UTXO{txio.second.getAmount(),
         header->blockHeight, tx.getTxIndex(), txio.second.getIndexOfOutput(),
         tx.getThisHash(), txOut.getScript()
      });
      total += txio.second.getAmount();
      if (total >= value) {
         break;
      }
   }
   return result;
}

////////
std::map<Types::TxIOKey, TxIOPair> TxIOCache::getZcTxios(
   const AddressFilter& filter) const
{
   ReentrantLock lock(this);
   std::map<Types::TxIOKey, TxIOPair> result;
   for (const auto& txioPair : zcTxios_) {
      const auto& txio = txioPair.second;
      if (filter(txio.getScrAddr())) {
         result.emplace(txio.getTxIOKeyOfOutput(), txio);
      }
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
// CacheResolveResult
CacheResolveResult::CacheResolveResult(uint32_t height, bool iszc,
   std::shared_ptr<Ledgers::DBCache> cache) :
   topBlock(height), isZC(iszc), dbCache(cache)
{}

void CacheResolveResult::addTxio(const Types::TxIOKey& key,
   const TxIOPair& txio,
   const Types::ScrAddr& addr)
{
   auto emplaceResult = txioMap.emplace(key, txio);
   if (!emplaceResult.second) {
      emplaceResult.first->second.merge(txio);
   }
   auto addrIter = addrTxioMap.find(addr);
   if (addrIter == addrTxioMap.end()) {
      addrIter = addrTxioMap.emplace(
         addr, std::vector<TxIOPair*>{}).first;
   }
   addrIter->second.emplace_back(&emplaceResult.first->second);
}

////////////////////////////////////////////////////////////////////////////////
// ChainData
ChainData::ChainData(CacheResolveResult& data) :
   txioMap(std::move(data.txioMap))
{
   std::set<Types::TxKey> allTxKeys;
   for (const auto& addr : data.addrTxioMap) {
      //tally address balance
      Types::Value total = 0;
      Types::Value spendable = 0;
      Types::Value unconfirmed = 0;
      std::set<Types::TxKey> addrTxKeys;

      for (auto txioPtr : addr.second) {
         const auto& txio = *txioPtr;
         Types::Value val = static_cast<Types::Value>(txio.getAmount());
         auto txKeyOfOutput = txio.getTxKeyOfOutput();
         if (!data.isZC || txio.hasTxOutZC()) {
            addrTxKeys.emplace(txKeyOfOutput);
         }
         if (txio.hasTxIn()) {
            addrTxKeys.emplace(txio.getTxKeyOfInput());
            if (txio.hasTxInZC() && !txio.hasTxOutZC()) {
               total -= val;
               spendable -= val;
               if (isUnconfirmed(txKeyOfOutput, data.topBlock, data.dbCache)) {
                  unconfirmed -= val;
               }
            }
            continue;
         }

         //total tallies all unspent outputs indiscriminately
         total += val;

         //spendable only tracks mature outputs (cf mining reward maturity)
         if (!data.isZC &&
            isSpendable(txKeyOfOutput, data.topBlock, data.dbCache)) {
            spendable += val;
         }

         //unconfirmed adds up immature and unconfirmed outputs
         if (isUnconfirmed(txKeyOfOutput, data.topBlock, data.dbCache)) {
            unconfirmed += val;
         }
      }

      //set address data
      valueMap.emplace(addr.first, Values{
         total, spendable, unconfirmed,
         addrTxKeys.size()
      });

      //update wallet aggregate
      totalBalance        += total;
      spendableBalance    += spendable;
      unconfirmedBalance  += unconfirmed;
      allTxKeys.insert(addrTxKeys.begin(), addrTxKeys.end());
   }
   txCount = allTxKeys.size();
}
