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
#include <cstring>

#include "Parser.h"
#include <Utils/ArmoryErrors.h>
#include <Utils/ArmoryConfig.h>
#include <Utils/DBUtils.h>
#include <Utils/BCTX.h>
#include <BlockchainDatabase/BlockObj.h>
#include <BlockchainDatabase/BlockDataMap.h>
#include <BlockchainDatabase/lmdb_wrapper.h>
#include <BlockchainDatabase/ScrAddrFilter.h>
#include <BlockchainDatabase/txio.h>
#include <BlockchainDatabase/StoredBlockObj.h>
#include <BitcoinP2P.h>

#include "Utils.h"
#include "Notifications.h"

using namespace Armory;
using namespace Armory::ZeroConf;
using namespace std::chrono_literals;

#define ZC_GETDATA_TIMEOUT_MS 60000

////////////////////////////////////////////////////////////////////////////////
// misc
ZeroConfBatch::ZeroConfBatch(bool hasWatcherEntries) :
   hasWatcherEntries(hasWatcherEntries)
{
   counter = std::make_shared<std::atomic<int>>();
   isReadyPromise = std::make_shared<std::promise<ArmoryErrorCodes>>();
   isReadyFut = isReadyPromise->get_future();
   creationTime = std::chrono::system_clock::now();
}

////////////////////////////////////////////////////////////////////////////////
// ZcPreprocessPacket
ZcPreprocessPacket::ZcPreprocessPacket(ZcPreprocessPacketType type) :
   type_(type)
{}

ZcPreprocessPacket::~ZcPreprocessPacket()
{}

ZcPreprocessPacketType ZcPreprocessPacket::type() const
{
   return type_;
}

ZcInvPayload::ZcInvPayload(bool watcher) :
   ZcPreprocessPacket(ZcPreprocessPacketType::Inv),
   watcher(watcher)
{}

////////////////////////////////////////////////////////////////////////////////
// ZcGetPacket
ZcGetPacket::ZcGetPacket(ZcGetPacketType t) :
   type{t}
{}

ZcGetPacket::~ZcGetPacket()
{}

ZcBroadcastPacket::ZcBroadcastPacket() :
   ZcGetPacket(ZcGetPacketType::Broadcast)
{}

RejectPacket::RejectPacket(const BinaryData& hash, char code) :
   ZcGetPacket(ZcGetPacketType::Reject),
   txHash(hash), code(code)
{}

////////////////////////////////////////////////////////////////////////////////
// RequestZcPacket
RequestZcPacket::RequestZcPacket() :
   ZcGetPacket(ZcGetPacketType::Request)
{
   timestamp = std::chrono::steady_clock::now();
}

bool RequestZcPacket::ready() const
{
   //skip if we have no hashes to request
   if (hashes.empty()) {
      return false;
   }

   /*
   Buffer zc from the network node until we have enough 
   to process or enough that has elapsed. This reduces
   the zc snapshot replacement frequency.
   */

   if (hashes.size() >= ZC_BUFFER_SIZE_THRESHOLD) {
      //buffer is ready if we have over ZC_BUFFER_SIZE_THRESHOLD hashes
      return true;
   }

   auto timediff = std::chrono::steady_clock::now() - timestamp;
   if (timediff >= std::chrono::seconds(ZC_BUFFER_LIFETIME_SEC)) {
      //or if the buffer is older than ZC_BUFFER_LIFETIME_SEC seconds
      return true;
   } else {
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
// ZeroConfContainer
ZeroConfContainer::ZeroConfContainer(LMDBBlockDatabase* db,
   std::shared_ptr<Blockchain> bcPtr,
   std::shared_ptr<Node::BitcoinNodeInterface> node, unsigned maxZcThread) :
   db_(db), bc_(bcPtr), networkNode_(node), maxZcThreadCount_(maxZcThread)
{
   zcEnabled_.store(false, std::memory_order_relaxed);
   zcPreprocessQueue_ = std::make_shared<PreprocessQueue>();

   //register ZC callbacks
   networkNode_->registerInvTxCallback(
      [this](std::vector<Node::InvEntry> entryVec)
      {
         if (!zcEnabled_.load(std::memory_order_relaxed)) {
            return;
         }
         auto payload = std::make_shared<ZcInvPayload>(false);
         payload->invVec = std::move(entryVec);
         zcWatcherQueue_.push_back(std::move(payload));
      }
   );

   networkNode_->registerGetTxCallback(
      [this](std::unique_ptr<Node::Payload> payload)
      {
         this->processTxGetDataReply(std::move(payload));
      }
   );
}

bool ZeroConfContainer::isEnabled() const
{
   return zcEnabled_.load(std::memory_order_relaxed);
}

std::shared_future<std::shared_ptr<ZcPurgePacket>>
ZeroConfContainer::pushNewBlockNotification(ReorganizationState reorgState)
{
   return actionQueue_->pushNewBlockNotification(reorgState);
}

void ZeroConfContainer::setZeroConfCallbacks(
   std::unique_ptr<ZeroConfCallbacks> ptr)
{
   bdvCallbacks_ = std::move(ptr);
}

std::shared_ptr<const MempoolSnapshot> ZeroConfContainer::getSnapshot() const
{
   auto ss = std::atomic_load_explicit(
      &snapshot_, std::memory_order_acquire);
   return std::const_pointer_cast<const MempoolSnapshot>(ss);
}

////////
Tx ZeroConfContainer::getTxByHash(const BinaryData& txHash) const
{
   auto ss = getSnapshot();
   if (ss == nullptr) {
      throw std::runtime_error("null snapshot");
   }

   auto parsedTxPtr = ss->getTxByHash(txHash);
   if (parsedTxPtr == nullptr) {
      throw std::runtime_error("empty ParsedTx");
   }

   //copy base tx, add txhash map
   return parsedTxPtr->getTxObj();
}

bool ZeroConfContainer::hasTxByHash(const BinaryData& txHash) const
{
   auto ss = getSnapshot();
   return ss->hasHash(txHash);
}

////////
std::map<BinaryData, std::shared_ptr<ParsedTx>>
ZeroConfContainer::purgeToBranchpoint(
   const ReorganizationState& reorgState,
   std::shared_ptr<MempoolSnapshot> ss)
{
   /*
   Rewinds mempool to branchpoint
    * on reorgs:
      - evict all ZCs that spend from reorged blocks
      - evict their descendants too
      - reset input resolution for mined dbKeys on all evicted ZC
      - return all reorged ZC for reparsing
   */

   if (reorgState.prevTopStillValid) {
      return {};
   }

   std::set<BinaryData> keysToDelete;
   auto currentHeader = reorgState.prevTop;

   //loop over headers
   while (currentHeader != reorgState.reorgBranchPoint) {
      //grab block
      auto rawBlock = db_->getRawBlock(currentHeader);
      auto block = BlockData::deserialize(
         rawBlock.getPtr(), rawBlock.getSize(),
         currentHeader, nullptr,
         BlockData::CheckHashes::NoChecks);
      const auto& txns = block->getTxns();

      for (unsigned txid = 0; txid < txns.size(); txid++) {
         const auto& txn = txns[txid];
         const auto& txHash = txn->getHash();

         //look for ZC spending from this tx hash
         auto hashIter = outPointsSpentByKey_.find(txHash);
         if (hashIter == outPointsSpentByKey_.end()) {
            continue;
         }

         for (const auto& opid : hashIter->second) {
            keysToDelete.emplace(opid.second.getSliceCopy(0, 6));
         }
      }

      const auto& bhash = currentHeader->getPrevHash();
      currentHeader = bc_->getHeaderByHash(bhash);
   }

   //drop the ZC from the mempool
   auto droppedZC = dropZCs(ss, keysToDelete);

   //reset all mined input resolution in dropped zc and return
   for (auto& zcPtr : droppedZC) {
      zcPtr.second->resetInputResolution(InputResolution::Mined);
   }
   return droppedZC;
}

std::map<BinaryData, std::shared_ptr<ParsedTx>> ZeroConfContainer::purge(
   const ReorganizationState& reorgState,
   std::shared_ptr<MempoolSnapshot> ss)
{
   /*
   Purges the mempool on new blocks:

    * on new blocks:
      - evict mined transactions from the mempool
      - evict invalidated transactions (ZCs in the mempool that
        are in conflict with the new blocks)

      - evict all the descendants of mined and invalidated ZCs
      - for descendants, reset all resolved spenders.
      - return any descendant that wasn't invalidated (for reparsing and 
        potential reentry in the mempool)

    * reorgs are first handled in purgeToBranchpoint
   */

   //sanity check
   if (db_ == nullptr || outPointsSpentByKey_.empty()) {
      return {};
   }

   std::map<BinaryData, std::shared_ptr<ParsedTx>> txsToReparse;
   std::set<BinaryData> keysToDelete;

   //purge zc map per block
   auto resolveInvalidatedZCs =
      [&keysToDelete, &reorgState, this](
         std::map<BinaryDataRef, std::set<unsigned>>& spentOutpoints)->void
   {
      //find zc spender for these spent outpoints
      for (const auto& opIdMap : spentOutpoints) {
         auto hashIter = outPointsSpentByKey_.find(opIdMap.first);
         if (hashIter == outPointsSpentByKey_.end()) {
            continue;
         }

         for (const auto& opid : opIdMap.second) {
            auto idIter = hashIter->second.find(opid);
            if (idIter == hashIter->second.end()) {
               continue;
            }
            keysToDelete.emplace(idIter->second);
         }
      }
   };

   //handle reorgs
   if (!reorgState.prevTopStillValid) {
      txsToReparse = purgeToBranchpoint(reorgState, ss);
   }

   //get all txhashes for the new blocks
   ZcUpdateBatch batch;
   auto currentHeader = reorgState.prevTop;
   if (!reorgState.prevTopStillValid) {
      currentHeader = reorgState.reorgBranchPoint;
   }

   //get the next header
   currentHeader = bc_->getHeaderByHash(currentHeader->getNextHash());

   //loop over headers
   while (currentHeader != nullptr) {
      //grab block
      auto rawBlock = db_->getRawBlock(currentHeader);
      auto block = BlockData::deserialize(
         rawBlock.getPtr(), rawBlock.getSize(),
         currentHeader, nullptr,
         BlockData::CheckHashes::NoChecks);
      const auto& txns = block->getTxns();

      //gather all outpoints spent by this block
      std::map<BinaryDataRef, std::set<unsigned>> spentOutpoints;
      for (unsigned txid = 1; txid < txns.size(); txid++) {
         const auto& txn = txns[txid];
         for (unsigned iin = 0; iin < txn->txins_.size(); iin++) {
            auto txInRef = txn->getTxInRef(iin);
            BinaryRefReader brr(txInRef);
            auto hash = brr.get_BinaryDataRef(32);
            auto index = brr.get_uint32_t();

            auto& indexSet = spentOutpoints[hash];
            indexSet.insert(index);
         }
      }

      //result for resolveInvalidatedZCs are set in keysToDelete
      resolveInvalidatedZCs(spentOutpoints);

      //next block
      if (currentHeader->getThisHash() == reorgState.newTop->getThisHash()) {
         break;
      }

      const auto& bhash = currentHeader->getNextHash();
      currentHeader = bc_->getHeaderByHash(bhash);
   }

   //drop the invalidated ZCs
   auto invalidatedZCs = dropZCs(ss, keysToDelete);

   //reset direct descendants' unconfirmed input resolution
   for (auto& zcPtr : invalidatedZCs) {
      zcPtr.second->resetInputResolution(InputResolution::Unconfirmed);
   }

   //add to set of transactions to reparse (might have reorged ZCs)
   txsToReparse.insert(invalidatedZCs.begin(), invalidatedZCs.end());

   //preprocess the dropped ZCs
   preprocessZcMap(txsToReparse, db_);
   return txsToReparse;
}

void ZeroConfContainer::reset()
{
   keyToSpentScrAddr_.clear();
   outPointsSpentByKey_.clear();
   keyToFundedScrAddr_.clear();
}

////////
std::map<BinaryData, std::shared_ptr<ParsedTx>> ZeroConfContainer::dropZC(
   std::shared_ptr<MempoolSnapshot> ss, const BinaryDataRef& key)
{
   /*
   ZeroConfSharedSnapshot will drop the tx and its children and return them.
   We need to clear our containers all dropped ZCs so we first drop from the
   snapshot and use the returned map to clear the requested ZC as well as all
   of its children.
   */
   auto droppedZCs = ss->dropZc(key);
   for (const auto& zcPair : droppedZCs) {
      auto txPtr = zcPair.second;
      if (txPtr == nullptr) {
         return {};
      }

      //drop from outPointsSpentByKey_
      outPointsSpentByKey_.erase(txPtr->getTxHash());
      for (const auto& input : txPtr->inputs) {
         auto opIter = outPointsSpentByKey_.find(input.opRef.getTxHashRef());
         if (opIter == outPointsSpentByKey_.end()) {
            continue;
         }

         //erase the index
         opIter->second.erase(input.opRef.getIndex());

         //erase the txhash if the index map is empty
         if (opIter->second.empty()) {
            minedTxHashes_.erase(opIter->first);
            outPointsSpentByKey_.erase(opIter);
         }
      }

      keyToSpentScrAddr_.erase(key);
      keyToFundedScrAddr_.erase(key);
      allZcTxHashes_.erase(txPtr->getTxHash());
   }
   return droppedZCs;
}

std::map<BinaryData, std::shared_ptr<ParsedTx>> ZeroConfContainer::dropZCs(
   std::shared_ptr<MempoolSnapshot> ss, const std::set<BinaryData>& zcKeys)
{
   if (zcKeys.empty()) {
      return {};
   }

   std::map<BinaryData, std::shared_ptr<ParsedTx>> droppedZCs;
   auto rIter = zcKeys.rbegin();
   while (rIter != zcKeys.rend()) {
      auto dropped = dropZC(ss, *rIter++);
      droppedZCs.insert(dropped.begin(), dropped.end());
   }

   //TODO: drop invalidated zc and children from DB *after* reparsing

   ZcUpdateBatch batch;
   batch.keysToDelete = zcKeys;
   updateBatch_.push_back(std::move(batch));
   return droppedZCs;
}

void ZeroConfContainer::finalizePurgePacket(
   ZcActionStruct zcAction,
   std::shared_ptr<MempoolSnapshot> ss) const
{
   auto purgePacket = std::make_shared<ZcPurgePacket>();
   purgePacket->ssPtr = ss;

   if (zcAction.batch == nullptr) {
      zcAction.resultPromise->set_value(purgePacket);
   }

   for (const auto& zcPair : zcAction.batch->zcMap) {
      if (snapshot_->getTxByKey(zcPair.first) == nullptr) {
         //can't find zc for this key, flag as invalidated
         purgePacket->invalidatedZcKeys.emplace(
            zcPair.first, zcPair.second->getTxHash());
      } else if (zcPair.second->state == ParsedTxStatus::Resolved) {
         /*
         This zc persisted through the new blocks, we need to
         keep track of the txios it creates
         */

         //check txins
         const auto& zcPtr = zcPair.second;
         for (const auto& parsedTxIn : zcPtr->inputs) {
            const auto& txioKey = parsedTxIn.opRef.getDbKey();
            auto& txioMap =
               purgePacket->scrAddrToTxioKeys[parsedTxIn.scrAddr];
            txioMap.emplace(txioKey);
         }

         //txouts
         try {
            for (unsigned i=0; i < zcPtr->outputs.size(); i++) {
               const auto& parsedTxOut = zcPtr->outputs[i];
               auto& txioMap =
                  purgePacket->scrAddrToTxioKeys[parsedTxOut.scrAddr];

               BinaryWriter bw;
               bw.put_BinaryData(zcPair.first);
               bw.put_uint16_t(i);
               txioMap.emplace(bw.getData());
            }
         } catch (const std::range_error&) {}
      }
   }
   zcAction.resultPromise->set_value(purgePacket);
}

void ZeroConfContainer::parseNewZC(ZcActionStruct zcAction)
{
   bool notify = true;
   auto ss = MempoolSnapshot::copy(
      snapshot_, MEMPOOL_DEPTH, POOL_MERGE_THRESHOLD);

   std::map<BinaryData, std::shared_ptr<ParsedTx>> zcMap;
   std::map<BinaryData, std::shared_ptr<WatcherTxBody>> watcherMap;
   BdvIdKey requestor;

   switch (zcAction.action)
   {
      case ZcAction::Purge:
      {
         //purge mined zc
         auto result = purge(zcAction.reorgState, ss);
         notify = false;

         ss->commitNewZCs();

         //setup batch with all tracked zc
         if (zcAction.batch == nullptr) {
            zcAction.batch = std::make_shared<ZeroConfBatch>(false);
         }
         zcAction.batch->zcMap = result;
         zcAction.batch->isReadyPromise->set_value(ArmoryErrorCodes::Success);

         [[fallthrough]];
      }

      case ZcAction::NewTx:
      {
         try {
            auto batchTxMap = std::move(getBatchTxMap(zcAction.batch, ss));
            zcMap = std::move(batchTxMap.txMap_);
            watcherMap = std::move(batchTxMap.watcherMap_);
            requestor = std::move(batchTxMap.requestor_);
         } catch (const ZcBatchError&) {
            return;
         }

         break;
      }

      case ZcAction::Shutdown:
      {
         reset();
         return;
      }

      default:
         return;
   }

   parseNewZC(std::move(zcMap), ss, true, notify, requestor, watcherMap);
   if (zcAction.resultPromise != nullptr) {
      finalizePurgePacket(std::move(zcAction), ss);
   }
}

void ZeroConfContainer::parseNewZC(
   std::map<BinaryData, std::shared_ptr<ParsedTx>> zcMap,
   std::shared_ptr<MempoolSnapshot> ss,
   bool updateDB, bool notify, BdvIdKey requestor,
   std::map<BinaryData, std::shared_ptr<WatcherTxBody>>& watcherMap)
{
   std::unique_lock<std::mutex> lock(parserMutex_);
   ZcUpdateBatch batch;

   auto iter = zcMap.begin();
   while (iter != zcMap.end()) {
      if (iter->second->state == ParsedTxStatus::Mined ||
         iter->second->state == ParsedTxStatus::Invalid ||
         iter->second->state == ParsedTxStatus::Skip) {
         zcMap.erase(iter++);
      } else {
         ++iter;
      }
   }

   if (ss == nullptr) {
      ss = std::make_shared<MempoolSnapshot>(
         MEMPOOL_DEPTH, POOL_MERGE_THRESHOLD);
   }

   for (const auto& newZCPair : zcMap) {
      if (Config::DBSettings::getDbType() != ARMORY_DB_TYPE::Super) {
         const auto& txHash = newZCPair.second->getTxHash();
         auto insertIter = allZcTxHashes_.emplace(txHash);
         if (!insertIter.second) {
            continue;
         }
      } else {
         if (ss->getTxByKey(newZCPair.first) != nullptr) {
            continue;
         }
      }
      batch.zcToWrite.emplace(newZCPair);
   }

   bool hasChanges = false;
   std::map<BdvIdKey, ParsedZCData> flaggedBDVs;
   std::map<BinaryData, std::shared_ptr<ParsedTx>> invalidatedTx;

   //zc logic
   std::set<BinaryDataRef> addedZcKeys;
   std::set<BinaryData> droppedZcKeys;
   for (const auto& newZCPair : zcMap) {
      auto txHash = newZCPair.second->getTxHash().getRef();
      if (!ss->getKeyForHash(txHash).empty()) {
         continue;
      }

      //add ZC if its relevant
      auto filterResult = filterTransaction(newZCPair.second, ss);
      if (filterResult.isValid()) {
         //check for collisions with other valid zcs
         auto invalidTxs = checkForCollisions(filterResult.outPointsSpentByKey, ss);
         for (auto& invalidTx : invalidTxs) {
            invalidatedTx.emplace(std::move(invalidTx));
         }

         //add this tx to known valid zcs
         addedZcKeys.emplace(newZCPair.first);
         hasChanges = true;

         for (auto& idmap : filterResult.outPointsSpentByKey) {
            //is this owner hash already in the map?
            auto& opMap = outPointsSpentByKey_[idmap.first];
            opMap.insert(idmap.second.begin(), idmap.second.end());
         }

         //merge scrAddr spent by key
         for (auto& sa_pair : filterResult.keyToSpentScrAddr) {
            auto insertResult = keyToSpentScrAddr_.emplace(sa_pair);
            if (insertResult.second == false) {
               insertResult.first->second = std::move(sa_pair.second);
            }
         }

         //merge scrAddr funded by key
         using mapbd_setbd_iter =
            std::map<BinaryDataRef, std::set<BinaryDataRef>>::iterator;
         keyToFundedScrAddr_.insert(
            std::move_iterator<mapbd_setbd_iter>(filterResult.keyToFundedScrAddr.begin()),
            std::move_iterator<mapbd_setbd_iter>(filterResult.keyToFundedScrAddr.end()));

         ss->stageNewZC(newZCPair.second, filterResult);

         //flag affected BDVs
         for (auto& bdvMap : filterResult.flaggedBDVs) {
            auto& parserResult = flaggedBDVs[bdvMap.first];
            parserResult.mergeTxios(bdvMap.second);
         }
      } else {
         if (Config::DBSettings::getDbType() == ARMORY_DB_TYPE::Super) {
            continue;
         }
         //in bare/full node, zcs that cannot be resolved do not affect
         //our list of addresses, drop them
         droppedZcKeys.emplace(newZCPair.first);
      }
   }

   //get rid of invalid zc, only applies to bare/full node
   dropZCs(ss, droppedZcKeys);

   if (updateDB && batch.hasData()) {
      //post new zc for writing to db, no need to wait on it
      updateBatch_.push_back(std::move(batch));
   }

   //find BDVs affected by invalidated keys
   if (!invalidatedTx.empty()) {
      //TODO: multi thread this at some point

      for (const auto& tx_pair : invalidatedTx) {
         //gather all scrAddr from invalidated tx
         std::set<BinaryDataRef> addrRefs;
         for (const auto& input : tx_pair.second->inputs) {
            if (!input.isResolved()) {
               continue;
            }
            addrRefs.emplace(input.scrAddr.getRef());
         }

         for (const auto& output : tx_pair.second->outputs) {
            addrRefs.emplace(output.scrAddr.getRef());
         }

         //flag relevant BDVs
         for (const auto& addrRef : addrRefs) {
            auto bdvid_set = bdvCallbacks_->hasScrAddr(addrRef);
            for (const auto& bdvid : bdvid_set) {
               auto& bdv = flaggedBDVs[bdvid];
               bdv.invalidatedKeys.emplace(
                  tx_pair.first, tx_pair.second->getTxHash());
               hasChanges = true;
            }
         }
      }
   }

   //swap in new state
   std::atomic_store_explicit(&snapshot_, ss, std::memory_order_release);

   //notify bdvs
   if (!hasChanges) {
      return;
   }

   if (!notify) {
      return;
   }

   //prepare notifications
   auto newZcKeys =
      std::make_shared<std::map<BinaryData, std::shared_ptr<std::set<BinaryDataRef>>>>();
   for (const auto& newKey : addedZcKeys) {
      //fill key to spent scrAddr map
      std::shared_ptr<std::set<BinaryDataRef>> spentScrAddr = nullptr;
      auto iter = keyToSpentScrAddr_.find(newKey);
      if (iter != keyToSpentScrAddr_.end()) {
         spentScrAddr = iter->second;
      }
      newZcKeys->emplace(newKey, std::move(spentScrAddr));
   }

   bdvCallbacks_->pushZcNotification(
      ss, newZcKeys,
      flaggedBDVs,
      requestor,
      watcherMap);
}

FilteredZeroConfData ZeroConfContainer::filterTransaction(
   std::shared_ptr<ParsedTx> parsedTx,
   std::shared_ptr<MempoolSnapshot> ss) const
{
   if (parsedTx->state == ParsedTxStatus::Mined ||
      parsedTx->state == ParsedTxStatus::Invalid ||
      parsedTx->state == ParsedTxStatus::Skip) {
      return {};
   }

   if (parsedTx->state == ParsedTxStatus::Uninitialized ||
      parsedTx->state == ParsedTxStatus::ResolveAgain) {
      preprocessTx(*parsedTx, db_);
   }

   //check tx resolution
   finalizeParsedTxResolution(
      parsedTx,
      db_, allZcTxHashes_,
      ss);

   //parse it
   return filterParsedTx(parsedTx,
      [addrMap=scrAddrMap_->get()](const BinaryData& addr)->bool
      {
         return addrMap->find(addr) != addrMap->end();
      }, bdvCallbacks_.get()
   );
}

std::map<BinaryData, std::shared_ptr<ParsedTx>>
ZeroConfContainer::checkForCollisions(
   const std::map<BinaryDataRef, std::map<unsigned, BinaryDataRef>>& spentOutpoints,
   std::shared_ptr<MempoolSnapshot> ss)
{
   //loop through outpoints
   std::map<BinaryData, std::shared_ptr<ParsedTx>> invalidatedZCs;
   for (const auto& idSet : spentOutpoints) {
      //compare them to the list of currently spent outpoints
      auto hashIter = outPointsSpentByKey_.find(idSet.first);
      if (hashIter == outPointsSpentByKey_.end()) {
         continue;
      }

      std::set<BinaryData> keysToDrop;
      for (const auto& opId : idSet.second) {
         auto idIter = hashIter->second.find(opId.first);
         if (idIter != hashIter->second.end()) {
            keysToDrop.emplace(idIter->second);
         }
      }

      for (const auto& zcKey : keysToDrop) {
         //drop the zc, get the map of invalidated zc in return
         auto droppedTxs = dropZC(ss, zcKey);
         if (droppedTxs.empty()) {
            continue;
         }

         //we need to track those to figure out which bdv to notify
         invalidatedZCs.insert(
            droppedTxs.begin(), droppedTxs.end());
      }
   }
   return invalidatedZCs;
}

void ZeroConfContainer::clear()
{
   snapshot_.reset();
}

bool ZeroConfContainer::isTxOutSpentByZC(const BinaryData& dbKey) const
{
   auto ss = getSnapshot();
   if (ss == nullptr) {
      return false;
   }
   return ss->isTxOutSpentByZC(dbKey);
}

std::map<BinaryData, std::shared_ptr<const TxIOPair>>
ZeroConfContainer::getUnspentZCforScrAddr(BinaryData scrAddr) const
{
   auto ss = getSnapshot();
   if (ss == nullptr) {
      return {};
   }

   auto txioMap = ss->getTxioMapForScrAddr(scrAddr);
   std::map<BinaryData, std::shared_ptr<const TxIOPair>> returnMap;
   for (const auto& zcPair : txioMap) {
      if (zcPair.second->hasTxIn()) {
         continue;
      }
      returnMap.emplace(zcPair);
   }
   return returnMap;
}

std::map<BinaryData, std::shared_ptr<const TxIOPair>>
ZeroConfContainer::getRBFTxIOsforScrAddr(BinaryData scrAddr) const
{
   auto ss = getSnapshot();
   if (ss == nullptr) {
      return {};
   }

   auto txioMap = ss->getTxioMapForScrAddr(scrAddr);
   std::map<BinaryData, std::shared_ptr<const TxIOPair>> returnMap;

   for (auto& zcPair : txioMap) {
      if (!zcPair.second->hasTxIn()) {
         continue;
      }
      if (!zcPair.second->isRBF()) {
         continue;
      }
      returnMap.emplace(zcPair);
   }
   return returnMap;
}

std::vector<TxOut> ZeroConfContainer::getZcTxOutsForKey(
   const std::set<BinaryData>& keys) const
{
   auto ss = getSnapshot();
   if (ss == nullptr) {
      return {};
   }

   std::vector<TxOut> result;
   for (const auto& key : keys) {
      auto zcKey = key.getSliceRef(0, 6);
      auto theTx = ss->getTxByKey(zcKey);
      if (theTx == nullptr) {
         continue;
      }
      auto outIdRef = key.getSliceRef(6, 2);
      auto outId = READ_UINT16_BE(outIdRef);
      result.emplace_back(theTx->getTxObj().getTxOutCopy(outId));
   }
   return result;
}

std::vector<UTXO> ZeroConfContainer::getZcUTXOsForKey(
   const std::set<BinaryData>& keys) const
{
   auto ss = getSnapshot();
   if (ss == nullptr) {
      return {};
   }

   std::vector<UTXO> result;
   result.reserve(keys.size());
   for (const auto& key : keys) {
      auto zcKey = key.getSliceRef(0, 6);
      auto theTx = ss->getTxByKey(zcKey);
      if (theTx == nullptr) {
         continue;
      }

      auto zcIdRef = key.getSliceRef(2, 4);
      auto zcId = READ_UINT32_BE(zcIdRef);

      auto outIdRef = key.getSliceRef(6, 2);
      auto outId = READ_UINT16_BE(outIdRef);

      auto txout = theTx->getTxObj().getTxOutCopy(outId);
      result.emplace_back(UTXO{
         txout.getValue(), UINT32_MAX,
         zcId, outId,
         theTx->getTxHash(), txout.getScript()
      });
   }
   return result;
}

////////
void ZeroConfContainer::updateZCinDB()
{
   while (true) {
      ZcUpdateBatch batch;
      try {
         batch = std::move(updateBatch_.pop_front());
      } catch (const Threading::StopBlockingLoop&) {
         break;
      }

      if (!batch.hasData()) {
         continue;
      }

      auto tx = db_->beginTransaction(
         DB_SELECT::ZERO_CONF, LMDB::Mode::ReadWrite);
      for (auto& zcPair : batch.zcToWrite) {
         /*TODO: speed this up*/
         StoredTx zcTx;
         auto tx = zcPair.second->getTxObj();
         zcTx.createFromTx(tx, true, true);
         db_->putStoredZC(zcTx, zcPair.first);
      }

      for (const auto& txhash : batch.txHashes) {
         //if the key is not to be found in the txMap_, this is a ZC txhash
         db_->putValue(DB_SELECT::ZERO_CONF, txhash, {});
      }

      for (auto& key : batch.keysToDelete) {
         BinaryData keyWithPrefix;
         if (key.getSize() == 6) {
            keyWithPrefix.resize(7);
            uint8_t* keyptr = keyWithPrefix.getPtr();
            keyptr[0] = (uint8_t)DbPrefix::ZCDATA;
            memcpy(keyptr + 1, key.getPtr(), 6);
         } else {
            keyWithPrefix = key;
         }

         auto dbIter = db_->getIterator(DB_SELECT::ZERO_CONF);
         if (!dbIter->seekTo(keyWithPrefix)) {
            continue;
         }

         std::vector<BinaryData> ktd;
         ktd.emplace_back(keyWithPrefix);

         do {
            BinaryDataRef thisKey = dbIter->getKeyRef();
            if (!thisKey.startsWith(keyWithPrefix)) {
               break;
            }
            ktd.emplace_back(thisKey);
         } while (dbIter->advanceAndRead(DbPrefix::ZCDATA));

         for (auto _key : ktd) {
            db_->deleteValue(DB_SELECT::ZERO_CONF, _key);
         }
      }

      for (auto& key : batch.txHashesToDelete) {
         db_->deleteValue(DB_SELECT::ZERO_CONF, key);
      }
      batch.setCompleted(true);
   }
}

unsigned ZeroConfContainer::loadZeroConfMempool(bool clearMempool)
{
   unsigned topId = 0;
   std::map<BinaryData, std::shared_ptr<ParsedTx>> zcMap;

   {
      auto tx = db_->beginTransaction(
         DB_SELECT::ZERO_CONF, LMDB::Mode::ReadOnly);
      auto dbIter = db_->getIterator(DB_SELECT::ZERO_CONF);
      if (!dbIter->seekToStartsWith(DbPrefix::ZCDATA)) {
         return topId;
      }

      do {
         BinaryDataRef zcKey = dbIter->getKeyRef();

         if (zcKey.getSize() == 7) {
            //Tx, grab it from DB
            StoredTx zcStx;
            db_->getStoredZC(zcStx, zcKey);

            //add to newZCMap_
            auto zckey = zcKey.getSliceCopy(1, 6);
            auto parsedTx = std::make_shared<ParsedTx>(zckey);
            parsedTx->setTx(zcStx.getSerializedTx(), zcStx.unixTime);
            zcMap.emplace(parsedTx->getKeyRef(), std::move(parsedTx));
         } else if (zcKey.getSize() == 9) {
            //TxOut, ignore it
            continue;
         } else if (zcKey.getSize() == 32) {
            //tx hash
            allZcTxHashes_.emplace(zcKey);
         } else {
            //shouldn't hit this
            LOGERR << "Unknown key found in ZC mempool";
            break;
         }
      } while (dbIter->advanceAndRead(DbPrefix::ZCDATA));
   }

   if (clearMempool == true) {
      LOGWARN << "Mempool was flagged for deletion!";
      ZcUpdateBatch batch;
      auto fut = batch.getCompletedFuture();

      for (const auto& zcTx : zcMap) {
         batch.keysToDelete.emplace(zcTx.first);
      }
      updateBatch_.push_back(std::move(batch));
      fut.wait();
   } else if (!zcMap.empty()) {
      LOGDEBUG << "parsing " << zcMap.size() << " txns from mempool";
      preprocessZcMap(zcMap, db_);

      //set highest used index
      auto lastEntry = zcMap.rbegin();
      auto& topZcKey = lastEntry->first;
      topId = READ_UINT32_BE(topZcKey.getSliceCopy(2, 4)) + 1;

      //no need to update the db nor notify bdvs on init
      std::map<BinaryData, std::shared_ptr<WatcherTxBody>> emptyWatcherMap;
      parseNewZC(
         std::move(zcMap), nullptr, false, false,
         UINT64_MAX,
         emptyWatcherMap);
      snapshot_->commitNewZCs();
   }
   return topId;
}

void ZeroConfContainer::init(std::shared_ptr<ScrAddrFilter> saf,
   bool clearMempool)
{
   LOGINFO << "Enabling zero-conf tracking";

   scrAddrMap_ = saf->getZcFilterMapPtr();
   auto topId = loadZeroConfMempool(clearMempool);

   auto newZcPacketLbd = [this](ZcActionStruct zas)->void
   {
      this->parseNewZC(std::move(zas));
   };
   actionQueue_ = std::make_unique<ZcActionQueue>(
      newZcPacketLbd, zcPreprocessQueue_, topId);

   auto updateZcThread = [this](void)->void
   {
      updateZCinDB();
   };

   auto invTxThread = [this](void)->void
   {
      handleInvTx();
   };

   parserThreads_.emplace_back(std::thread(updateZcThread));
   parserThreads_.emplace_back(std::thread(invTxThread));
   increaseParserThreadPool(1);

   zcEnabled_.store(true, std::memory_order_relaxed);
}

////////
void ZeroConfContainer::pushZcPreprocessVec(
   std::shared_ptr<RequestZcPacket> req)
{
   if (req->hashes.empty()) {
      return;
   }

   //register batch with main zc processing thread
   actionQueue_->initiateZcBatch(
      req->hashes, ZC_GETDATA_TIMEOUT_MS, {}, false, {});

   //queue up individual requests for parser threads to process
   zcPreprocessQueue_->push_back(std::move(req));
}

void ZeroConfContainer::handleInvTx()
{
   std::shared_ptr<RequestZcPacket> request = nullptr;

   while (true) {
      std::shared_ptr<ZcPreprocessPacket> packet;
      ZcPreprocessPacketType packetType;
      try {
         //pop every seconds
         packet = std::move(zcWatcherQueue_.pop_front(1s));
         packetType = packet->type();
      } catch (const Threading::StackTimedOutException&) {
         //progress with an empty packet
         packetType = ZcPreprocessPacketType::Inv;
      } catch (const Threading::StopBlockingLoop&) {
         break;
      }

      switch (packetType)
      {
         case ZcPreprocessPacketType::Inv:
         {
            //skip this entirely if there are no addresses to scan the ZCs against
            if (scrAddrMap_->empty() &&
               Config::DBSettings::getDbType() != ARMORY_DB_TYPE::Super) {
               continue;
            }

            auto invPayload = std::dynamic_pointer_cast<ZcInvPayload>(packet);
            if (invPayload != nullptr && invPayload->watcher) {
               /*
               This is an inv tx payload from the watcher node, check it against
               our outstanding broadcasts
               */
               SingleLock lock(&watcherMapMutex_);
               for (const auto& invEntry : invPayload->invVec) {
                  BinaryDataRef bdRef(invEntry.hash, sizeof(invEntry.hash));
                  auto iter = watcherMap_.find(bdRef);
                  if (iter == watcherMap_.end() ||
                     iter->second->inved ||
                     iter->second->ignoreWatcherNodeInv) {
                     continue;
                  }

                  //mark as fetched
                  iter->second->inved = true;

                  //set parsedTx tx body
                  auto payloadTx = std::make_shared<ProcessPayloadTxPacket>(bdRef);
                  payloadTx->rawTx = iter->second->rawTxPtr;

                  //push to preprocess threads
                  actionQueue_->queueGetDataResponse(std::move(payloadTx));
               }
            } else {
               /*
               inv tx from the process node, send a getdata request for these hashes
               */

               if (request == nullptr) {
                  request = std::make_shared<RequestZcPacket>();
               }

               if (invPayload != nullptr) {
                  const auto& invVec = invPayload->invVec;
                  if (parserThreadCount_ < invVec.size() &&
                     parserThreadCount_ < maxZcThreadCount_) {
                     increaseParserThreadPool(invVec.size());
                  }

                  SingleLock lock(&watcherMapMutex_);
                  for (const auto& entry : invVec) {
                     BinaryDataRef hash{entry.hash, sizeof(entry.hash)};

                     /*
                     Skip this hash if it's in our watcher map. Invs from the network
                     will never trigger this condition. Invs from the tx we broadcast
                     through the p2p interface neither, as we present the hash to
                     kickstart the chain of events (node won't inv back a hash it was
                     inv'ed to).

                     Only a native RPC broadcast can trigger this condition, as the
                     node will inv all peers it has not received this hash from. We do
                     not want to create an unnecessary batch for native RPC pushes, so
                     we skip those.
                     */
                     if (watcherMap_.find(hash) != watcherMap_.end()) {
                        continue;
                     }
                     request->hashes.emplace_back(hash);
                  }
               }

               if (!request->ready()) {
                  break;
               }
               pushZcPreprocessVec(request);
               request.reset();
            }

            //register batch with main zc processing thread
            break;
         }

         default:
            throw std::runtime_error("invalid packet");
      }
   }
}

void ZeroConfContainer::handleZcProcessingStructThread()
{
   while (true) {
      std::shared_ptr<ZcGetPacket> packet;
      try {
         packet = move(zcPreprocessQueue_->pop_front());
      } catch (const Threading::StopBlockingLoop&) {
         break;
      }

      switch (packet->type)
      {
         case ZcGetPacketType::Request:
         {
            auto request = std::dynamic_pointer_cast<RequestZcPacket>(packet);
            if (request != nullptr) {
               requestTxFromNode(*request);
            }
            break;
         }

         case ZcGetPacketType::Payload:
         {
            auto payloadTx = std::dynamic_pointer_cast<ProcessPayloadTxPacket>(
               packet);
            if (payloadTx == nullptr) {
               throw std::runtime_error("unexpected payload type");
            }
            processPayloadTx(payloadTx);
            break;
         }

         case ZcGetPacketType::Broadcast:
         {
            auto broadcastPacket = std::dynamic_pointer_cast<ZcBroadcastPacket>(
               packet);
            if (broadcastPacket == nullptr) {
               break;
            }
            pushZcPacketThroughP2P(*broadcastPacket);
            break;
         }

         default:
            break;
      }
   }
}

void ZeroConfContainer::processTxGetDataReply(
   std::unique_ptr<Node::Payload> payload)
{
   switch (payload->type())
   {
      case Node::PayloadType::Tx:
      {
         std::shared_ptr<Node::Payload> payload_sptr(std::move(payload));
         auto payloadtx = std::dynamic_pointer_cast<Node::Payload_Tx>(
            payload_sptr);
         if (payloadtx == nullptr || payloadtx->empty()) {
            LOGERR << "invalid tx getdata payload";
            return;
         }

         //got a tx, post it to the zc preprocessing queue
         auto txData = std::make_shared<ProcessPayloadTxPacket>(
            payloadtx->getHash256());
         txData->rawTx = std::make_shared<BinaryData>(
            &payloadtx->getRawTx()[0], payloadtx->getSize());
         actionQueue_->queueGetDataResponse(std::move(txData));
         break;
      }

      case Node::PayloadType::Reject:
      {
         std::shared_ptr<Node::Payload> payload_sptr(std::move(payload));
         auto payloadReject = std::dynamic_pointer_cast<Node::Payload_Reject>(
            payload_sptr);
         if (payloadReject == nullptr) {
            LOGERR << "invalid reject payload";
            return;
         }

         if (payloadReject->rejectType() != Node::PayloadType::Tx) {
            //only handling payload_tx rejections
            return;
         }

         BinaryData hash{
            &payloadReject->getExtra()[0],
            payloadReject->getExtra().size()
         };
         auto rejectPacket = std::make_shared<RejectPacket>(
            hash, payloadReject->code());
         actionQueue_->queueGetDataResponse(rejectPacket);
         break;
      }

      default:
         break;
   }
}

void ZeroConfContainer::requestTxFromNode(RequestZcPacket& packet)
{
   std::vector<Node::InvEntry> invVec;
   invVec.reserve(packet.hashes.size());
   for (const auto& hash : packet.hashes) {
      if (hash.getSize() != 32) {
         throw std::runtime_error("invalid inv hash length");
      }
      Node::InvEntry inv;
      inv.invtype = Node::Inv_Msg_Witness_Tx;
      memcpy(inv.hash, hash.getPtr(), 32);
      invVec.emplace_back(std::move(inv));
   }
   networkNode_->requestTx(std::move(invVec));
}

void ZeroConfContainer::processPayloadTx(
   std::shared_ptr<ProcessPayloadTxPacket> payloadPtr)
{
   if (payloadPtr->rawTx->empty()) {
      payloadPtr->pTx->state = ParsedTxStatus::Invalid;
      payloadPtr->incrementCounter();
      return;
   }

   //set raw tx and current time
   try {
      payloadPtr->pTx->setTx(*payloadPtr->rawTx, time(0));
   } catch (const std::exception&) {
      //tx already set, ignore
   }
   preprocessTx(*payloadPtr->pTx, db_);
   payloadPtr->incrementCounter();
}

////////
void ZeroConfContainer::broadcastZC(
   const std::vector<BinaryDataRef>& rawZcVec, uint32_t timeout_ms,
   const ZcBroadcastCallback& cbk, BdvIdKey bdvID)
{
   auto zcPacket = std::make_shared<ZcBroadcastPacket>();
   zcPacket->hashes.reserve(rawZcVec.size());
   zcPacket->zcVec.reserve(rawZcVec.size());

   for (const auto& rawZcRef : rawZcVec) {
      if (rawZcRef.empty()) {
         continue;
      }
      auto rawZcPtr = std::make_shared<BinaryData>(rawZcRef);
      Tx tx(*rawZcPtr);
      zcPacket->hashes.emplace_back(tx.getThisHash());
      zcPacket->zcVec.emplace_back(rawZcPtr);
   }

   if (zcPacket->zcVec.empty()) {
      return;
   }

   {
      //update the watcher map
      ReentrantLock lock(&watcherMapMutex_);
      for (unsigned i=0; i < zcPacket->hashes.size(); i++) {
         auto& hash = zcPacket->hashes[i];
         if (insertWatcherEntry(
            hash, zcPacket->zcVec[i],
            bdvID, std::set<BdvIdKey>{})) {
            continue;
         }

         //already have this zc in an earlier batch, drop the hash
         hash.clear();
      }
   }

   //sets up & queues the zc batch for us
   if (actionQueue_->initiateZcBatch(
      zcPacket->hashes, timeout_ms, cbk, true, bdvID) == nullptr) {
      //return if no batch was created
      return;
   }

   //push each zc on the process queue
   zcPreprocessQueue_->push_back(zcPacket);
}

bool ZeroConfContainer::insertWatcherEntry(
   const BinaryData& hash, std::shared_ptr<BinaryData> rawTxPtr,
   BdvIdKey bdvID, std::set<BdvIdKey> extraRequestors,
   bool watchEntry)
{
   //lock
   ReentrantLock lock(&watcherMapMutex_);

   //try to insert
   auto iter = watcherMap_.find(hash);
   if (iter == watcherMap_.end()) {
      auto insertIter = watcherMap_.emplace(
         hash, std::make_shared<WatcherTxBody>(rawTxPtr));

      //set the watcher node flag
      insertIter.first->second->ignoreWatcherNodeInv = !watchEntry;

      //set extra requestors
      if (!extraRequestors.empty()) {
         insertIter.first->second->extraRequestors = move(extraRequestors);
      }

      //return true for successful insertion
      return true;
   } else {
      //already have this hash, do not change the watcher node flag

      //tie this request to the existing watcher entry
      iter->second->extraRequestors.emplace(bdvID);

      //add the extra requestors if any
      if (!extraRequestors.empty()) {
         iter->second->extraRequestors.insert(
            extraRequestors.begin(), extraRequestors.end());
      }

      //return false for failed insertion
      return false;
   }
}

std::shared_ptr<WatcherTxBody> ZeroConfContainer::eraseWatcherEntry(
   const BinaryData& hash)
{
   ReentrantLock lock(&watcherMapMutex_);

   auto iter = watcherMap_.find(hash);
   if (iter == watcherMap_.end()) {
      return nullptr;
   }

   auto objPtr = std::move(iter->second);
   watcherMap_.erase(iter);
   return objPtr;
}

std::shared_ptr<ZeroConfBatch> ZeroConfContainer::initiateZcBatch(
   const std::vector<BinaryData>& zcHashes, unsigned timeout,
   const ZcBroadcastCallback& cbk, bool hasWatcherEntries,
   BdvIdKey bdvId)
{
   return actionQueue_->initiateZcBatch(
      zcHashes, timeout,
      cbk, hasWatcherEntries,
      bdvId);
}

////////
void ZeroConfContainer::pushZcPacketThroughP2P(ZcBroadcastPacket& packet)
{
   if (!networkNode_->connected()) {
      LOGWARN << "node is offline, cannot broadcast";

      //TODO: report node down errors to batch
      /*packet.errorCallback_(
         move(*packet.rawZc_), ZCBroadcastStatus_P2P_NodeDown);*/
      return;
   }

   //create inv payload
   std::vector<Node::InvEntry> invVec;
   std::map<BinaryData,
      std::shared_ptr<Node::BitcoinP2P::GetDataPayload>> getDataPair;

   for (unsigned i=0; i < packet.hashes.size(); i++) {
      const auto& hash = packet.hashes[i];
      if (hash.empty()) {
         continue;
      }
      const auto& rawZc = packet.zcVec[i];

      //create inv entry, this announces the zc by its hash to the node
      Node::InvEntry entry;
      entry.invtype = Node::Inv_Msg_Witness_Tx;
      memcpy(entry.hash, hash.getPtr(), 32);
      invVec.push_back(entry);

      //create getData payload packet, this is the zc body for the node to
      //grab once it knows of the hash
      auto payload = std::make_unique<Node::Payload_Tx>();
      std::vector<uint8_t> rawtx;
      rawtx.resize(rawZc->getSize());
      memcpy(&rawtx[0], rawZc->getPtr(), rawZc->getSize());

      payload->setRawTx(std::move(rawtx));
      auto getDataPayload =
         std::make_shared<Node::BitcoinP2P::GetDataPayload>();
      getDataPayload->payload_ = std::move(payload);
      getDataPair.emplace(hash, getDataPayload);
   }

   //register getData payload
   networkNode_->getDataPayloadMap_.update(move(getDataPair));

   //send inv packet
   auto payload_inv = std::make_unique<Node::Payload_Inv>();
   payload_inv->setInvVector(invVec);
   networkNode_->sendMessage(std::move(payload_inv));
}

void ZeroConfContainer::shutdown()
{
   if (actionQueue_ != nullptr) {
      actionQueue_->shutdown();
   }
   zcWatcherQueue_.terminate();
   zcPreprocessQueue_->terminate();
   updateBatch_.terminate();

   for (auto& parser : parserThreads_) {
      if (parser.joinable()) {
         parser.join();
      }
   }
}

////////
void ZeroConfContainer::increaseParserThreadPool(unsigned count)
{
   std::unique_lock<std::mutex> lock(parserThreadMutex_);

   //start Zc parser thread
   auto processZcThread = [this](void)->void
   {
      handleZcProcessingStructThread();
   };

   for (unsigned i = parserThreadCount_; i < count; i++) {
      parserThreads_.emplace_back(std::thread(processZcThread));
   }
   parserThreadCount_ = parserThreads_.size();
   LOGINFO << "now running " << parserThreadCount_ << " zc parser threads";
}

void ZeroConfContainer::setWatcherNode(
   std::shared_ptr<Node::BitcoinNodeInterface> watcherNode)
{
   auto getTxLambda = [this](std::vector<Node::InvEntry> invVec)->void
   {
      if (!zcEnabled_.load(std::memory_order_relaxed)) {
         return;
      }

      //push inv vector as watcher inv packet on the preprocessing queue
      auto payload = std::make_shared<ZcInvPayload>(true);
      payload->invVec = std::move(invVec);
      zcWatcherQueue_.push_back(std::move(payload));
   };
   watcherNode->registerInvTxCallback(getTxLambda);
}

////////
BatchTxMap ZeroConfContainer::getBatchTxMap(
   std::shared_ptr<ZeroConfBatch> batch,
   std::shared_ptr<MempoolSnapshot> ss)
{
   if (batch == nullptr) {
      throw ZcBatchError();
   }

   //wait on the batch for the duration of the
   //timeout minus time elapsed since creation
   unsigned timeLeft = 0;
   auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now() - batch->creationTime);
   if (delay.count() < batch->timeout_) {
      timeLeft = batch->timeout_ - delay.count();
   }
   auto timeLeftMs = std::chrono::milliseconds(timeLeft);

   ArmoryErrorCodes batchResult;
   if (batch->timeout_ > 0 &&
      batch->isReadyFut.wait_for(timeLeftMs) != std::future_status::ready) {
      batchResult = ArmoryErrorCodes::ZcBatch_Timeout;
   } else {
      batchResult = batch->isReadyFut.get();
   }

   BatchTxMap result;
   result.requestor_ = batch->requestor;

   //purge the watcher map of the hashes this batch registered
   if (batch->hasWatcherEntries) {
      SingleLock lock(&watcherMapMutex_);
      for (const auto& keyPair : batch->hashToKeyMap) {
         auto iter = watcherMap_.find(keyPair.first);
         if (iter == watcherMap_.end()) {
            LOGERR << "missing watcher entry, this should not happen!";
            LOGERR << "skipping this timed out batch, this needs reported to a dev";
            throw ZcBatchError();
         }

         //save watcher object in the batch, mostly to carry the extra
         //requestors over
         result.watcherMap_.emplace(iter->first, move(iter->second));

         /*
         Watcher map entries are only set by broadcast requests.
         These are currated to avoid collisions, therefor a batch will
         only carry the hashes for the watcher entries it created. Thus
         it is safe to erase all matched hashes from the map.
         */
         watcherMap_.erase(iter);
      }
   }

   if (batchResult != ArmoryErrorCodes::Success) {
      /*
      Failed to get transactions for batch, fire the error callback
      */

      //skip if this batch doesn't have a callback
      if (!batch->errorCallback || !batch->hasWatcherEntries) {
         throw ZcBatchError{};
      }

      unsigned invedZcCount = 0;
      std::vector<ZeroConfBatchFallbackStruct> txVec;
      std::set<BinaryDataRef> purgedHashes;
      txVec.reserve(batch->zcMap.size());

      //purge the batch of missing tx and their children
      for (auto& txPair : batch->zcMap) {
         bool purge = false;

         //does this tx depend on a purged predecessor?
         for (auto& txInObj : txPair.second->inputs) {
            auto parentIter = purgedHashes.find(txInObj.opRef.getTxHashRef());
            if (parentIter == purgedHashes.end()) {
               continue;
            }

            //this zc depends on a purged zc, purge it too
            purge = true;
            break;
         }

         //was this tx inv'ed back to us?
         bool inved = true;
         auto iter = result.watcherMap_.find(txPair.second->getTxHash());

         //map consistency is assured in the watcherMap purge 
         //scope, this iterator is guaranteed valid
         if (!iter->second->inved) {
            inved = false;
            purge = true;
         }

         if (!purge) {
            //sanity check
            if (!inved) {
               LOGWARN << "keeping zc from timed out batch that wasn't inved";
            }

            //we're keeping this tx
            ++invedZcCount;
            continue;
         }

         //create the fallback struct
         ZeroConfBatchFallbackStruct fallbackStruct;
         fallbackStruct.txHash = iter->first;
         fallbackStruct.rawTxPtr = std::move(iter->second->rawTxPtr);
         fallbackStruct.err = batchResult;
         fallbackStruct.extraRequestors =
            std::move(iter->second->extraRequestors);

         //check snapshot for collisions
         if (ss->hasHash(iter->first.getRef())) {
            //already have this tx in our mempool, report to callback
            //but don't flag hash as purged (children need to be processed if any)
            fallbackStruct.err = ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool;
         } else {
            //keep track of purged zc hashes
            purgedHashes.emplace(iter->first.getRef());
         }

         //flag tx to be skipped by parser
         txPair.second->state = ParsedTxStatus::Skip;

         //add to vector for error callback
         txVec.emplace_back(std::move(fallbackStruct));
      }
      batch->errorCallback(std::move(txVec));

      //don't forward the batch if it has no zc ready to be parsed
      if (invedZcCount == 0) {
         throw ZcBatchError{};
      }

      //we have some inv'ed zc to parse but the batch timed out, we need to
      //wait on the counter to match our local count of valid tx.
      while (batch->zcMap.size() -
         batch->counter->load(std::memory_order_acquire) <
         invedZcCount) {
         LOGWARN << "timedout batch waiting on " << invedZcCount << " inved tx: ";
         LOGWARN << "batch size: " << batch->zcMap.size() << ", counter: " << 
            batch->counter->load(std::memory_order_acquire);
         std::this_thread::sleep_for(100ms);
      }
   }

   result.txMap_ = batch->zcMap;
   return result;
}

unsigned ZeroConfContainer::getMatcherMapSize(void) const 
{
   return actionQueue_->getMatcherMapSize();
}

unsigned ZeroConfContainer::getMergeCount(void) const
{
   auto ss = getSnapshot();
   if (ss == nullptr) {
      return 0;
   }
   return ss->getMergeCount();
}

////////////////////////////////////////////////////////////////////////////////
// ZcActionQueue
ZcActionQueue::ZcActionQueue(
   const std::function<void(ZcActionStruct)>& func,
   std::shared_ptr<PreprocessQueue> zcPreprocessQueue,
   unsigned topId) :
   newZcFunction_(func), zcPreprocessQueue_(zcPreprocessQueue)
{
   topId_.store(topId, std::memory_order_relaxed);
   matcherMapSize_.store(0, std::memory_order_relaxed);
   start();
}

////////
void ZcActionQueue::start()
{
   auto processZcThread = [this]()->void
   {
      processNewZcQueue();
   };

   auto matcherThread = [this]()->void
   {
      getDataToBatchMatcherThread();
   };

   processThreads_.push_back(std::thread(processZcThread));
   processThreads_.push_back(std::thread(matcherThread));
}

void ZcActionQueue::shutdown()
{
   newZcQueue_.terminate();
   getDataResponseQueue_.terminate();
   for (auto& thr : processThreads_) {
      if (thr.joinable()) {
         thr.join();
      }
   }
}

////////
BinaryData ZcActionQueue::getNewZCkey()
{
   uint32_t newId = topId_.fetch_add(1, std::memory_order_relaxed);
   BinaryWriter bw;
   bw.reserve(6);
   bw.put_uint16_t(0xFFFF);
   bw.put_uint32_t(newId, BE);
   return bw.getData();
}

std::shared_ptr<ZeroConfBatch> ZcActionQueue::initiateZcBatch(
   const std::vector<BinaryData>& zcHashes, unsigned timeout,
   const ZcBroadcastCallback& cbk, bool hasWatcherEntries,
   BdvIdKey bdvId)
{
   auto batch = std::make_shared<ZeroConfBatch>(hasWatcherEntries);
   batch->requestor = bdvId;

   for (const auto& hash : zcHashes) {
      //skip if hash is empty
      if (hash.empty()) {
         continue;
      }

      auto key = getNewZCkey();
      auto ptx = std::make_shared<ParsedTx>(key);
      ptx->setTxHash(hash);

      batch->hashToKeyMap.emplace(ptx->getTxHash().getRef(), ptx->getKeyRef());
      batch->zcMap.emplace(ptx->getKeyRef(), ptx);
   }

   if (batch->zcMap.empty()) {
      //empty batch, skip
      return nullptr;
   }

   batch->counter->store(batch->zcMap.size(), std::memory_order_relaxed);
   batch->timeout_ = timeout; //in milliseconds
   batch->errorCallback = cbk;

   ZcActionStruct zac;
   zac.action = ZcAction::NewTx;
   zac.batch = batch;
   newZcQueue_.push_back(std::move(zac));

   auto batchCopy = batch;
   batchQueue_.push_back(std::move(batchCopy));
   return batch;
}

void ZcActionQueue::processNewZcQueue()
{
   while (true) {
      ZcActionStruct zcAction;
      std::map<BinaryData, std::shared_ptr<ParsedTx>> zcMap;
      try {
         zcAction = std::move(newZcQueue_.pop_front());
      } catch (const Threading::StopBlockingLoop&) {
         break;
      }

      /*
      Populate local map with batch's zcMap_ so that we can cleanup the
      hashes from the request map after parsing.
      */
      if (zcAction.batch != nullptr) {
         /*
         We can't just grab the hash reference since the object referred to is
         held by a ParsedTx and that has no guarantee of surviving the parsing
         function, hence copying the entire map.
         */
         zcMap = zcAction.batch->zcMap;
      }

      newZcFunction_(std::move(zcAction));
      if (zcMap.empty()) {
         continue;
      }

      //cleanup request map
      std::set<BinaryData> hashSet;
      for (const auto& zcPair : zcMap) {
         hashSet.emplace(zcPair.second->getTxHash());
      }
      hashesToClear_.push_back(std::move(hashSet));
   }
}

////////
std::shared_future<std::shared_ptr<ZcPurgePacket>>
ZcActionQueue::pushNewBlockNotification(ReorganizationState& reorgState)
{
   ZcActionStruct zcaction;
   zcaction.action = ZcAction::Purge;
   zcaction.resultPromise =
      std::make_unique<std::promise<std::shared_ptr<ZcPurgePacket>>>();
   zcaction.reorgState = std::move(reorgState);

   auto fut = zcaction.resultPromise->get_future();
   newZcQueue_.push_back(std::move(zcaction));
   return fut;
}

void ZcActionQueue::queueGetDataResponse(std::shared_ptr<ZcGetPacket> payloadTx)
{
   getDataResponseQueue_.push_back(std::move(payloadTx));
}

void ZcActionQueue::getDataToBatchMatcherThread()
{
   bool run = true;
   std::map<BinaryData, std::shared_ptr<ZeroConfBatch>> hashToBatchMap;
   while (run) {
      //queue of outstanding node getdata packets that need matched with
      //their parent batch - blocking
      std::shared_ptr<ZcGetPacket> zcPacket;
      try {
         zcPacket = getDataResponseQueue_.pop_front();
      } catch (const Threading::StopBlockingLoop&) {
         run = false;
      }

      //queue of new batches - non blocking
      while (true) {
         try {
            //populate local map with hashes from each batch, do not
            //overwrite existing entries (older batches should get
            //precedence over a shared tx hash)
            auto batch = batchQueue_.pop_front();
            for (auto& hashPair : batch->hashToKeyMap) {
               hashToBatchMap.emplace(hashPair.first, batch);
            }
         } catch (const Threading::IsEmpty&) {
            break;
         }
      }

      if (zcPacket != nullptr) {
         switch (zcPacket->type)
         {
            case ZcGetPacketType::Payload:
            {
               auto payloadTx = std::dynamic_pointer_cast<ProcessPayloadTxPacket>(zcPacket);
               if (payloadTx == nullptr) {
                  break;
               }

               //look for parent batch in local map
               auto iter = hashToBatchMap.find(payloadTx->txHash);
               if (iter != hashToBatchMap.end()) {
                  //tie the tx to its batch
                  payloadTx->batchCtr = iter->second->counter;
                  payloadTx->batchProm = iter->second->isReadyPromise;

                  auto keyIter = iter->second->hashToKeyMap.find(
                     payloadTx->txHash.getRef());
                  if (keyIter != iter->second->hashToKeyMap.end()) {
                     auto txIter = iter->second->zcMap.find(keyIter->second);
                     if (txIter != iter->second->zcMap.end()) {
                        payloadTx->pTx = txIter->second;
                        zcPreprocessQueue_->push_back(payloadTx);
                     }
                  }
                  hashToBatchMap.erase(iter);
               }
               break;
            }

            case ZcGetPacketType::Reject:
            {
               auto rejectPacket = std::dynamic_pointer_cast<RejectPacket>(zcPacket);
               if (rejectPacket == nullptr) {
                  break;
               }

               //grab the batch
               auto iter = hashToBatchMap.find(rejectPacket->txHash);
               if (iter != hashToBatchMap.end()) {
                  iter->second->isReadyPromise->set_value(
                     (ArmoryErrorCodes)rejectPacket->code);
                  hashToBatchMap.erase(iter);
               }
               break;
            }

            default:
               break;
         }
      }

      //queue of hashes to purge from the local map
      while (true) {
         try {
            auto hashSet = std::move(hashesToClear_.pop_front());
            for (const auto& hash : hashSet) {
               hashToBatchMap.erase(hash);
            }
         } catch (const Threading::IsEmpty&) {
            break;
         }
      }

      matcherMapSize_.store(hashToBatchMap.size(), std::memory_order_relaxed);
   }
}

unsigned ZcActionQueue::getMatcherMapSize() const
{
   return matcherMapSize_.load(std::memory_order_relaxed);
}

////////////////////////////////////////////////////////////////////////////////
// ZcUpdateBatch
std::shared_future<bool> ZcUpdateBatch::getCompletedFuture()
{
   if (completed_ == nullptr) {
      completed_ = std::make_unique<std::promise<bool>>();
   }
   return completed_->get_future();
}

void ZcUpdateBatch::setCompleted(bool val)
{
   if (completed_ == nullptr) {
      return;
   }
   completed_->set_value(val);
}

bool ZcUpdateBatch::hasData() const
{
   if (!zcToWrite.empty() ||
      !txHashes.empty() ||
      !keysToDelete.empty()) {
      return true;
   }
   return false;
}

////////////////////////////////////////////////////////////////////////////////
// ProcessPayloadTxPacket
ProcessPayloadTxPacket::ProcessPayloadTxPacket(const BinaryData& hash) :
   ZcGetPacket(ZcGetPacketType::Payload), txHash(hash)
{}

void ProcessPayloadTxPacket::incrementCounter(void)
{
   if (batchCtr == nullptr) {
      LOGERR << "null batch ptr";
      throw std::runtime_error("null batch ptr");
   }

   auto val = batchCtr->fetch_sub(1, std::memory_order_release);
   if (val == 1) {
      try {
         batchProm->set_value(ArmoryErrorCodes::Success);
      } catch (const std::future_error&) {
         LOGWARN << "batch promise already set";
      }
   }
}
