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
#include <Utils/log.h>
#include <Network/WebSocketMessage.h>
#include "../Notifications.h"

using namespace Armory;
using namespace Armory::Bridge;
using namespace std::chrono_literals;

namespace
{
   uint32_t getConfCount(Types::TxKey txKey, uint32_t top,
      std::shared_ptr<Ledgers::DBCache> cache)
   {
      if (Types::isThisAZCKey(txKey)) {
         return 0;
      }
      auto blockId = Types::getBlockIDFromTxKey(txKey);
      auto header = cache->getHeader(blockId);
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
   ReentrantLock lock(this);
   lastKnownBlock_ = UINT32_MAX;
   unspentTxios_.clear();
   spentTxios_.clear();
   zcTxios_.clear();
   dbCache_->clear();
}

////////
CacheResolveResult TxIOCache::resolve(
   const AddressFilter& filter, uint32_t fromHeight) const
{
   ReentrantLock lock(this);
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
   ReentrantLock lock(this);
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

   ReentrantLock lock(this);

   for (auto blockId : reorgNotif.invalidatedBlockIds()) {
      try {
         auto header = dbCache_->getHeader(blockId);
         header->isMainBranch = false;
      } catch (const std::out_of_range&) {
         continue;
      }
   }

   for (auto blockId : reorgNotif.newMainBranchBlockIds()) {
      try {
         auto header = dbCache_->getHeader(blockId);
         header->isMainBranch = true;
      } catch (const std::out_of_range&) {
         continue;
      }
   }
}

bool TxIOCache::txKeyIsValid(const Types::TxKey& txKey) const
{
   auto blockId = Types::getBlockIDFromTxKey(txKey);
   try {
      auto header = dbCache_->getHeader(blockId);
      return header->isMainBranch;
   } catch (const std::out_of_range&) {
      return false;
   }
}

const Tx& TxIOCache::getTxByHash(const Types::TxHash& txHash) const
{
   //the best copy of this hash that is valid: a mined copy on the main
   //branch, else a zc or unpooled copy. The caller holds the lock for as
   //long as it uses the tx
   ReentrantLock lock(this);
   for (auto txKey : dbCache_->getKeysForHash(txHash)) {
      if (Ledgers::TxKeyCandidates::kindOf(txKey) !=
         Ledgers::TxKeyCandidates::Kind::Mined || txKeyIsValid(txKey)) {
         return dbCache_->getTx(txKey);
      }
   }
   throw std::out_of_range("no valid tx for hash " + txHash.toHexStr());
}

////////
uint32_t TxIOCache::update(
   std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr,
   std::shared_ptr<Notifications::Server::Notif> notif)
{
   using namespace Notifications::Server;

   if (notif->type == NotifType::ZC) {
      auto zcPtr = std::dynamic_pointer_cast<Notif_ZC>(notif);
      updateZC(bdvPtr, zcPtr->txios, zcPtr->invalidatedZCHashes, true);
      return UINT32_MAX;
   }

   NewBlockNotif blockNotif{UINT32_MAX, UINT32_MAX, {}, {}};
   if (notif->type == NotifType::NEWBLOCK) {
      auto blockPtr =
         std::dynamic_pointer_cast<Notif_NewBlock>(notif);
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

   //5. a fetched tx replaces the mined and zc copies of its hash
   std::set<Types::TxHash> fetchedHashes;
   for (const auto& tx : txs) {
      fetchedHashes.emplace(tx.getThisHash());
   }
   dbCache_->eraseHashes(fetchedHashes);

   for (auto& tx : txs) {
      //add fresh tx
      dbCache_->addTx(tx);
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
   if (!append) {
      //append is false, clear up the zc map and apply
      //fresh set of zc txios
      zcTxios_.clear();
   }
   if (!invalidatedZC.empty()) {
      //we have invalidated zc hashes, let's purge them
      auto invalidatedKeys = dbCache_->purgeTxs(invalidatedZC);

      //purge the txio map using the keys
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
      try {
         dbCache_->getTx(zcOutKey);
      } catch (const std::out_of_range&) {
         missingTxKeys.emplace(zcOutKey);
      }

      if (zcTxio.hasTxIn()) {
         auto zcInKey = zcTxio.getTxKeyOfInput();
         try {
            dbCache_->getTx(zcInKey);
         } catch (const std::out_of_range&) {
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
         dbCache_->addTx(tx);
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
      try {
         dbCache_->getHeader(blockId);
      } catch (const std::out_of_range&) {
         missingBlocks.emplace(blockId);
      }
      try {
         dbCache_->getTx(key);
      } catch (const std::out_of_range&) {
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
   ReentrantLock lock(this);
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
         const auto& tx = dbCache_->getTx(txKey);
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
         const auto& tx = dbCache_->getTx(txKeyOutput);
         iter->second.emplace(tx.getThisHash());
      }
   }
   return result;
}

std::vector<UTXO> TxIOCache::getZcUTXOs(bool rbf,
   const AddressFilter& filter) const
{
   ReentrantLock lock(this);
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
         auto blockId = tx.getBlockId();
         auto header = dbCache_->getHeader(blockId);
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
            const auto& tx = dbCache_->getTx(txKey);
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

      const auto& tx = dbCache_->getTx(txKey);
      addTxio(txio.second, tx);
   }
   return result;
}

std::vector<UTXO> TxIOCache::getUTXOs(
   uint64_t value, bool zc, bool rbf,
   const AddressFilter& filter) const
{
   ReentrantLock lock(this);
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
      const auto& tx = dbCache_->getTx(txKey);
      auto blockId = tx.getBlockId();
      const auto& header = dbCache_->getHeader(blockId);

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

////////
void TxIOCache::addUnpooledTxs(const std::vector<BinaryData>& rawTxs)
{
   //txs the bridge sees before any db has a key for them
   ReentrantLock lock(this);
   for (const auto& rawTx : rawTxs) {
      try {
         dbCache_->addUnpooledTx(rawTx);
      } catch (const std::exception& e) {
         LOGWARN << "not caching a tx that doesn't deserialize: " << e.what();
      }
   }
}

////////
namespace
{
   //waits on a db reply, gives up if the db connection goes away
   template<typename T>
   bool waitOnDb(std::future<T>& fut,
      const std::shared_ptr<AsyncClient::BlockDataViewer>& bdvPtr)
   {
      while (fut.wait_for(250ms) != std::future_status::ready) {
         if (!bdvPtr->isValid()) {
            return false;
         }
      }
      return true;
   }
}

void TxIOCache::fetchTxs(
   std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr,
   const std::set<Types::TxHash>& hashes)
{
   if (bdvPtr == nullptr) {
      //offline, the cache is all there is
      return;
   }

   //1. what the cache misses: hashes with no valid copy (unknown, or mined
   //off the main branch or with no header), unpooled copies and zc copies
   //no txio points to (they may have been pooled or mined since, and update
   //only replaces the txs of the wallets)
   std::set<Types::TxHash> missingHashes;
   std::set<Types::BlockId> missingBlockIds;
   {
      ReentrantLock lock(this);
      std::set<Types::TxKey> walletZcKeys;
      for (const auto& txioPair : zcTxios_) {
         const auto& txio = txioPair.second;
         if (txio.hasTxOutZC()) {
            walletZcKeys.emplace(txio.getTxKeyOfOutput());
         }
         if (txio.hasTxInZC()) {
            walletZcKeys.emplace(txio.getTxKeyOfInput());
         }
      }

      for (const auto& txHash : hashes) {
         if (txHash.getSize() != 32) {
            continue;
         }
         try {
            auto txKey = getTxByHash(txHash).getDBKey();
            if (Types::isThisAZCKey(txKey)) {
               if (walletZcKeys.find(txKey) == walletZcKeys.end()) {
                  missingHashes.emplace(txHash);
               }
               continue;
            }
            if (Types::isThisAnUnpooledKey(txKey)) {
               missingHashes.emplace(txHash);
            }
            //a valid mined copy has its header cached
         } catch (const std::out_of_range&) {
            missingHashes.emplace(txHash);
         }
      }
   }

   //2. ask the db, without holding the lock. A miss in DB_BARE doesn't
   //mean the tx doesn't exist, so misses aren't remembered
   std::vector<Tx> txs;
   if (!missingHashes.empty()) {
      auto promTxs = std::make_shared<
         std::promise<AsyncClient::TxBatchResult>>();
      auto futTxs = promTxs->get_future();
      try {
         bdvPtr->getTxsByHash(missingHashes, [promTxs]
            (ReturnMessage<AsyncClient::TxBatchResult> result) {
               try {
                  promTxs->set_value(result.get());
               } catch (const ClientMessageError&) {
                  promTxs->set_exception(std::current_exception());
               }
         });
         if (!waitOnDb(futTxs, bdvPtr)) {
            LOGWARN << "lost the db while fetching txs";
            return;
         }

         auto txBatch = futTxs.get();
         for (auto& txPair : txBatch) {
            if (missingHashes.find(txPair.first) == missingHashes.end()) {
               continue;
            }

            //the db only returns txs with a valid key
            auto txKey = txPair.second->getDBKey();
            if (!Types::isTxKeyValid(txKey)) {
               continue;
            }
            if (!Types::isThisAZCKey(txKey)) {
               missingBlockIds.emplace(Types::getBlockIDFromTxKey(txKey));
            }
            txs.emplace_back(*txPair.second);
         }
      } catch (const ClientMessageError& e) {
         LOGWARN << "failed to fetch txs from the db: " << e.what();
      } catch (const LWS_Error& e) {
         LOGWARN << "failed to fetch txs from the db: " << e.what();
         return;
      }
   }

   //3. headers for the mined txs
   {
      ReentrantLock lock(this);
      for (auto iter = missingBlockIds.begin();
         iter != missingBlockIds.end();) {
         try {
            dbCache_->getHeader(*iter);
            iter = missingBlockIds.erase(iter);
         } catch (const std::out_of_range&) {
            ++iter;
         }
      }
   }

   AsyncClient::HeaderVec headers;
   if (!missingBlockIds.empty()) {
      auto promHeaders = std::make_shared<
         std::promise<AsyncClient::HeaderVec>>();
      auto futHeaders = promHeaders->get_future();
      try {
         AsyncClient::Blockchain bc{*bdvPtr};
         bc.getHeadersById(missingBlockIds, [promHeaders]
            (ReturnMessage<AsyncClient::HeaderVec> result) {
               try {
                  promHeaders->set_value(result.get());
               } catch (const ClientMessageError&) {
                  promHeaders->set_exception(std::current_exception());
               }
         });
         if (waitOnDb(futHeaders, bdvPtr)) {
            headers = futHeaders.get();
         } else {
            LOGWARN << "lost the db while fetching headers";
         }
      } catch (const ClientMessageError& e) {
         //the txs are still cached, the next fetch asks again
         LOGWARN << "failed to fetch headers from the db: " << e.what();
      } catch (const LWS_Error& e) {
         LOGWARN << "failed to fetch headers from the db: " << e.what();
      }
   }

   //4. fill the cache
   ReentrantLock lock(this);
   dbCache_->addHeaders(headers);
   for (auto& tx : txs) {
      dbCache_->addTx(tx);
   }
}
