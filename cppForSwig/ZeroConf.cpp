////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ZeroConf.h"
#include "BlockchainDatabase/BlockDataMap.h"
#include "ArmoryErrors.h"

using namespace std;
using namespace Armory::Threading;
using namespace Armory::Config;

#define ZC_GETDATA_TIMEOUT_MS 60000

///////////////////////////////////////////////////////////////////////////////
ZcPreprocessPacket::~ZcPreprocessPacket()
{}

///////////////////////////////////////////////////////////////////////////////
ZcGetPacket::~ZcGetPacket()
{}

///////////////////////////////////////////////////////////////////////////////
//
//ZeroConfContainer Methods
//
///////////////////////////////////////////////////////////////////////////////
ZeroConfContainer::ZeroConfContainer(LMDBBlockDatabase* db,
   std::shared_ptr<BitcoinNodeInterface> node, unsigned maxZcThread) :
   db_(db), networkNode_(node), maxZcThreadCount_(maxZcThread)
{
   zcEnabled_.store(false, memory_order_relaxed);

   zcPreprocessQueue_ = make_shared<PreprocessQueue>();

   //register ZC callbacks
   auto processInvTx = [this](vector<InvEntry> entryVec)->void
   {
      if (!zcEnabled_.load(memory_order_relaxed))
         return;
            
      auto payload = make_shared<ZcInvPayload>(false);
      payload->invVec_ = move(entryVec);
      zcWatcherQueue_.push_back(move(payload));
   };

   networkNode_->registerInvTxLambda(processInvTx);

   auto getTx = [this](unique_ptr<Payload> payload)->void
   {
      this->processTxGetDataReply(move(payload));
   };
   networkNode_->registerGetTxCallback(getTx);
}

///////////////////////////////////////////////////////////////////////////////
bool ZeroConfContainer::isEnabled() const 
{ 
   return zcEnabled_.load(std::memory_order_relaxed);
}

///////////////////////////////////////////////////////////////////////////////
shared_future<shared_ptr<ZcPurgePacket>> 
ZeroConfContainer::pushNewBlockNotification(
   Blockchain::ReorganizationState reorgState)
{ 
   return actionQueue_->pushNewBlockNotification(reorgState); 
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::setZeroConfCallbacks(
   std::unique_ptr<ZeroConfCallbacks> ptr)
{
   bdvCallbacks_ = std::move(ptr);
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<MempoolSnapshot> ZeroConfContainer::getSnapshot() const
{
   auto ss = atomic_load_explicit(&snapshot_, memory_order_acquire);
   return ss;
}

///////////////////////////////////////////////////////////////////////////////
Tx ZeroConfContainer::getTxByHash(const BinaryData& txHash) const
{
   auto ss = getSnapshot();
   if (ss == nullptr)
      return Tx();

   auto parsedTxPtr = ss->getTxByHash(txHash);
   if (parsedTxPtr == nullptr)
      return Tx();

   //copy base tx, add txhash map
   auto txCopy = parsedTxPtr->tx_;
   
   //get zc outpoints id
   for (unsigned i=0; i<txCopy.getNumTxIn(); i++)
   {
      auto&& txin = txCopy.getTxInCopy(i);
      auto&& op = txin.getOutPoint();

      auto opKey = ss->getKeyForHash(op.getTxHashRef());
      if (opKey.empty())
      {
         txCopy.pushBackOpId(0);
         continue;
      }

      BinaryRefReader brr(opKey);
      brr.advance(2);
      txCopy.pushBackOpId(brr.get_uint32_t(BE));
   }

   return txCopy;
}

///////////////////////////////////////////////////////////////////////////////
bool ZeroConfContainer::hasTxByHash(const BinaryData& txHash) const
{
   auto ss = getSnapshot();
   return ss->hasHash(txHash);
}

///////////////////////////////////////////////////////////////////////////////
map<BinaryData, shared_ptr<ParsedTx>> ZeroConfContainer::purgeToBranchpoint(
   const Blockchain::ReorganizationState& reorgState, 
   shared_ptr<MempoolSnapshot> ss)
{
   /*
   Rewinds mempool to branchpoint
    * on reorgs:
      - evict all ZCs that spend from reorged blocks
      - evict their descendants too
      - reset input resolution for mined dbKeys on all evicted ZC
      - return all reorged ZC for reparsing
   */

   if (reorgState.prevTopStillValid_)
      return {};

   set<BinaryData> keysToDelete;
   auto bcPtr = db_->blockchain();
   auto currentHeader = reorgState.prevTop_;

   //loop over headers
   while (currentHeader != reorgState.reorgBranchPoint_)
   {
      //grab block
      auto&& rawBlock = db_->getRawBlock(currentHeader);

      auto block = BlockData::deserialize(
         rawBlock.getPtr(), rawBlock.getSize(),
         currentHeader, nullptr,
         BlockData::CheckHashes::NoChecks);
      const auto& txns = block->getTxns();

      for (unsigned txid = 0; txid < txns.size(); txid++)
      {
         const auto& txn = txns[txid];
         const auto& txHash = txn->getHash();

         //look for ZC spending from this tx hash
         auto hashIter = outPointsSpentByKey_.find(txHash);
         if (hashIter == outPointsSpentByKey_.end())
            continue;

         for (const auto& opid : hashIter->second)
            keysToDelete.emplace(opid.second.getSliceCopy(0, 6));
      }

      const auto& bhash = currentHeader->getPrevHash();
      currentHeader = bcPtr->getHeaderByHash(bhash);
   }

   //drop the ZC from the mempool
   auto droppedZC = dropZCs(ss, keysToDelete);

   //reset all mined input resolution in dropped zc and return
   for (auto& zcPtr : droppedZC)
      zcPtr.second->resetInputResolution(InputResolution::Mined);

   return droppedZC;
}

///////////////////////////////////////////////////////////////////////////////
map<BinaryData, shared_ptr<ParsedTx>> ZeroConfContainer::purge(
   const Blockchain::ReorganizationState& reorgState,
   shared_ptr<MempoolSnapshot> ss)
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

   map<BinaryData, shared_ptr<ParsedTx>> txsToReparse;

   if (db_ == nullptr || outPointsSpentByKey_.empty())
      return {};

   set<BinaryData> keysToDelete;

   //purge zc map per block
   auto resolveInvalidatedZCs =
      [&keysToDelete, &reorgState, this](
         map<BinaryDataRef, set<unsigned>>& spentOutpoints)->void
   {
      //find zc spender for these spent outpoints
      for (auto& opIdMap : spentOutpoints)
      {
         auto hashIter = outPointsSpentByKey_.find(opIdMap.first);
         if (hashIter == outPointsSpentByKey_.end())
            continue;

         for (auto& opid : opIdMap.second)
         {
            auto idIter = hashIter->second.find(opid);
            if (idIter == hashIter->second.end())
               continue;

            keysToDelete.emplace(idIter->second);
         }
      }
   };

   //handle reorgs
   if (!reorgState.prevTopStillValid_)
      txsToReparse = move(purgeToBranchpoint(reorgState, ss));

   //get all txhashes for the new blocks
   ZcUpdateBatch batch;
   auto bcPtr = db_->blockchain();

   auto currentHeader = reorgState.prevTop_;
   if (!reorgState.prevTopStillValid_)
      currentHeader = reorgState.reorgBranchPoint_;

   //get the next header
   currentHeader = bcPtr->getHeaderByHash(currentHeader->getNextHash());

   //loop over headers
   while (currentHeader != nullptr)
   {
      //grab block
      auto&& rawBlock = db_->getRawBlock(currentHeader);

      auto block = BlockData::deserialize(
         rawBlock.getPtr(), rawBlock.getSize(),
         currentHeader, nullptr,
         BlockData::CheckHashes::NoChecks);
      const auto& txns = block->getTxns();

      //gather all outpoints spent by this block
      map<BinaryDataRef, set<unsigned>> spentOutpoints;
      for (unsigned txid = 1; txid < txns.size(); txid++)
      {
         auto& txn = txns[txid];
         for (unsigned iin = 0; iin < txn->txins_.size(); iin++)
         {
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
      if (currentHeader->getThisHash() == reorgState.newTop_->getThisHash())
         break;

      const auto& bhash = currentHeader->getNextHash();
      currentHeader = bcPtr->getHeaderByHash(bhash);
   }

   //drop the invalidated ZCs
   auto invalidatedZCs = dropZCs(ss, keysToDelete);

   //reset direct descendants' unconfirmed input resolution
   for (auto& zcPtr : invalidatedZCs)
      zcPtr.second->resetInputResolution(InputResolution::Unconfirmed);

   //add to set of transactions to reparse (might have reorged ZCs)
   txsToReparse.insert(invalidatedZCs.begin(), invalidatedZCs.end());

   //preprocess the dropped ZCs
   preprocessZcMap(txsToReparse, db_);
   return txsToReparse;
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::reset()
{
   keyToSpentScrAddr_.clear();
   outPointsSpentByKey_.clear();
   keyToFundedScrAddr_.clear();
}

///////////////////////////////////////////////////////////////////////////////
map<BinaryData, shared_ptr<ParsedTx>> ZeroConfContainer::dropZC(
   shared_ptr<MempoolSnapshot> ss, const BinaryDataRef& key)
{
   /*
   ZeroConfSharedSnapshot will drop the tx and its children and return them.
   We need to clear our containers all dropped ZCs so we first drop from the
   snapshot and use the returned map to clear the requested ZC as well as all
   of its children.
   */
   auto droppedZCs = ss->dropZc(key);

   for (auto& zcPair : droppedZCs)
   {
      auto txPtr = zcPair.second;
      if (txPtr == nullptr)
         return {};

      //drop from outPointsSpentByKey_
      outPointsSpentByKey_.erase(txPtr->getTxHash());
      for (auto& input : txPtr->inputs_)
      {
         auto opIter = 
            outPointsSpentByKey_.find(input.opRef_.getTxHashRef());
         if (opIter == outPointsSpentByKey_.end())
            continue;

         //erase the index
         opIter->second.erase(input.opRef_.getIndex());

         //erase the txhash if the index map is empty
         if (opIter->second.size() == 0)
         {
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

///////////////////////////////////////////////////////////////////////////////
map<BinaryData, shared_ptr<ParsedTx>> ZeroConfContainer::dropZCs(
   shared_ptr<MempoolSnapshot> ss, const set<BinaryData>& zcKeys)
{
   if (zcKeys.size() == 0)
      return {};

   map<BinaryData, shared_ptr<ParsedTx>> droppedZCs;

   auto rIter = zcKeys.rbegin();
   while (rIter != zcKeys.rend())
   {
      auto dropped = dropZC(ss, *rIter++);
      droppedZCs.insert(dropped.begin(), dropped.end());
   }

   //TODO: drop invalidated zc and children from DB *after* reparsing

   ZcUpdateBatch batch;
   batch.keysToDelete_ = zcKeys;
   updateBatch_.push_back(move(batch));

   return droppedZCs;
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::finalizePurgePacket(
   ZcActionStruct zcAction,
   shared_ptr<MempoolSnapshot> ss) const
{
   auto purgePacket = make_shared<ZcPurgePacket>();
   purgePacket->ssPtr_ = ss;

   if (zcAction.batch_ == nullptr)
      zcAction.resultPromise_->set_value(purgePacket);

   for (const auto& zcPair : zcAction.batch_->zcMap_)
   {
      if (snapshot_->getTxByKey(zcPair.first) == nullptr)
      {
         //can't find zc for this key, flag as invalidated
         purgePacket->invalidatedZcKeys_.emplace(
            zcPair.first, zcPair.second->getTxHash());
      }
      else if (zcPair.second->status() == ParsedTxStatus::Resolved)
      {
         /*
         This zc persisted through the new blocks, we need to
         keep track of the txios it creates
         */

         auto& zcPtr = zcPair.second;

         //check txins
         for (const auto& parsedTxIn : zcPtr->inputs_)
         {
            const auto& txioKey = parsedTxIn.opRef_.getDbKey();
            auto& txioMap = 
               purgePacket->scrAddrToTxioKeys_[parsedTxIn.scrAddr_];
            txioMap.emplace(txioKey);
         }

         //txouts
         try
         {
            for (unsigned i=0; i < zcPtr->outputs_.size(); i++)
            {
               const auto& parsedTxOut = zcPtr->outputs_[i];
               auto& txioMap = 
                  purgePacket->scrAddrToTxioKeys_[parsedTxOut.scrAddr_];

               BinaryWriter bw;
               bw.put_BinaryData(zcPair.first);
               bw.put_uint16_t(i);
               txioMap.emplace(bw.getData());
            }
         }
         catch (range_error&)
         {}
      }
   }

   zcAction.resultPromise_->set_value(purgePacket);
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::parseNewZC(ZcActionStruct zcAction)
{
   bool notify = true;

   auto ss = MempoolSnapshot::copy(
      snapshot_, MEMPOOL_DEPTH, POOL_MERGE_THRESHOLD);

   map<BinaryData, shared_ptr<ParsedTx>> zcMap;
   map<BinaryData, shared_ptr<WatcherTxBody>> watcherMap;
   pair<string, string> requestor;

   switch (zcAction.action_)
   {
   case Zc_Purge:
   {
      //purge mined zc
      auto result = purge(zcAction.reorgState_, ss);
      notify = false;

      ss->commitNewZCs();

      //setup batch with all tracked zc
      if (zcAction.batch_ == nullptr)
         zcAction.batch_ = make_shared<ZeroConfBatch>(false);
      zcAction.batch_->zcMap_ = result;
      zcAction.batch_->isReadyPromise_->set_value(ArmoryErrorCodes::Success);

      [[fallthrough]];
   }

   case Zc_NewTx:
   {
      try
      {
         auto batchTxMap = move(getBatchTxMap(zcAction.batch_, ss));
         zcMap = move(batchTxMap.txMap_);
         watcherMap = move(batchTxMap.watcherMap_);
         requestor = move(batchTxMap.requestor_);
      }
      catch (ZcBatchError&)
      {
         return;
      }

      break;
   }

   case Zc_Shutdown:
   {
      reset();
      return;
   }

   default:
      return;
   }

   parseNewZC(move(zcMap), ss, true, notify, requestor, watcherMap);
   if (zcAction.resultPromise_ != nullptr)
      finalizePurgePacket(move(zcAction), ss);
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::parseNewZC(
   map<BinaryData, shared_ptr<ParsedTx>> zcMap,
   shared_ptr<MempoolSnapshot> ss,
   bool updateDB, bool notify,
   const pair<string, string>& requestor,
   std::map<BinaryData, shared_ptr<WatcherTxBody>>& watcherMap)
{
   unique_lock<mutex> lock(parserMutex_);
   ZcUpdateBatch batch;

   auto iter = zcMap.begin();
   while (iter != zcMap.end())
   {
      if (iter->second->status() == ParsedTxStatus::Mined ||
         iter->second->status() == ParsedTxStatus::Invalid || 
         iter->second->status() == ParsedTxStatus::Skip)
         zcMap.erase(iter++);
      else
         ++iter;
   }

   if (ss == nullptr)
      ss = make_shared<MempoolSnapshot>(MEMPOOL_DEPTH, POOL_MERGE_THRESHOLD);

   for (auto& newZCPair : zcMap)
   {
      if (DBSettings::getDbType() != ARMORY_DB_SUPER)
      {
         auto& txHash = newZCPair.second->getTxHash();
         auto insertIter = allZcTxHashes_.insert(txHash);
         if (!insertIter.second)
            continue;
      }
      else
      {
         if (ss->getTxByKey(newZCPair.first) != nullptr)
            continue;
      }

      batch.zcToWrite_.insert(newZCPair);
   }

   bool hasChanges = false;
   map<string, ParsedZCData> flaggedBDVs;
   map<BinaryData, shared_ptr<ParsedTx>> invalidatedTx;

   //zc logic
   set<BinaryDataRef> addedZcKeys;
   for (auto& newZCPair : zcMap)
   {
      auto&& txHash = newZCPair.second->getTxHash().getRef();
      if (!ss->getKeyForHash(txHash).empty())
         continue;

      //parse the zc
      auto&& filterResult = filterTransaction(newZCPair.second, ss);

      //check for replacement
      invalidatedTx = checkForCollisions(filterResult.outPointsSpentByKey_, ss);

      //add ZC if its relevant
      if (filterResult.isValid())
      {
         addedZcKeys.insert(newZCPair.first);
         hasChanges = true;

         for (auto& idmap : filterResult.outPointsSpentByKey_)
         {
            //is this owner hash already in the map?
            auto& opMap = outPointsSpentByKey_[idmap.first];
            opMap.insert(idmap.second.begin(), idmap.second.end());
         }

         //merge scrAddr spent by key
         for (auto& sa_pair : filterResult.keyToSpentScrAddr_)
         {
            auto insertResult = keyToSpentScrAddr_.insert(sa_pair);
            if (insertResult.second == false)
               insertResult.first->second = move(sa_pair.second);
         }

         //merge scrAddr funded by key
         typedef map<BinaryDataRef, set<BinaryDataRef>>::iterator mapbd_setbd_iter;
         keyToFundedScrAddr_.insert(
            move_iterator<mapbd_setbd_iter>(filterResult.keyToFundedScrAddr_.begin()),
            move_iterator<mapbd_setbd_iter>(filterResult.keyToFundedScrAddr_.end()));

         ss->stageNewZC(newZCPair.second, filterResult);

         //flag affected BDVs
         for (auto& bdvMap : filterResult.flaggedBDVs_)
         {
            auto& parserResult = flaggedBDVs[bdvMap.first];
            parserResult.mergeTxios(bdvMap.second);
         }
      }
   }

   if (updateDB && batch.hasData())
   {
      //post new zc for writing to db, no need to wait on it
      updateBatch_.push_back(move(batch));
   }

   //find BDVs affected by invalidated keys
   if (invalidatedTx.size() > 0)
   {
      //TODO: multi thread this at some point

      for (auto& tx_pair : invalidatedTx)
      {
         //gather all scrAddr from invalidated tx
         set<BinaryDataRef> addrRefs;

         for (auto& input : tx_pair.second->inputs_)
         {
            if (!input.isResolved())
               continue;

            addrRefs.insert(input.scrAddr_.getRef());
         }

         for (auto& output : tx_pair.second->outputs_)
            addrRefs.insert(output.scrAddr_.getRef());

         //flag relevant BDVs
         for (auto& addrRef : addrRefs)
         {
            auto&& bdvid_set = bdvCallbacks_->hasScrAddr(addrRef);
            for (auto& bdvid : bdvid_set)
            {
               auto& bdv = flaggedBDVs[bdvid];
               bdv.invalidatedKeys_.insert(
                  make_pair(tx_pair.first, tx_pair.second->getTxHash()));
               hasChanges = true;
            }
         }
      }
   }

   //swap in new state
   atomic_store_explicit(&snapshot_, ss, memory_order_release);

   //notify bdvs
   if (!hasChanges)
      return;

   if (!notify)
      return;

   //prepare notifications
   auto newZcKeys =
      make_shared<map<BinaryData, shared_ptr<set<BinaryDataRef>>>>();
   for (auto& newKey : addedZcKeys)
   {
      //fill key to spent scrAddr map
      shared_ptr<set<BinaryDataRef>> spentScrAddr = nullptr;
      auto iter = keyToSpentScrAddr_.find(newKey);
      if (iter != keyToSpentScrAddr_.end())
         spentScrAddr = iter->second;

      auto addr_pair = make_pair(newKey, move(spentScrAddr));
      newZcKeys->insert(move(addr_pair));
   }

   bdvCallbacks_->pushZcNotification(
      ss, newZcKeys,
      flaggedBDVs, 
      requestor.first, requestor.second, 
      watcherMap);
}

///////////////////////////////////////////////////////////////////////////////
FilteredZeroConfData ZeroConfContainer::filterTransaction(
   shared_ptr<ParsedTx> parsedTx,
   shared_ptr<MempoolSnapshot> ss) const
{
   if (parsedTx->status() == ParsedTxStatus::Mined || 
      parsedTx->status() == ParsedTxStatus::Invalid ||
      parsedTx->status() == ParsedTxStatus::Skip)
   {
      return {};
   }

   if (parsedTx->status() == ParsedTxStatus::Uninitialized ||
      parsedTx->status() == ParsedTxStatus::ResolveAgain)
   {
      preprocessTx(*parsedTx, db_);
   }

   //check tx resolution
   finalizeParsedTxResolution(
      parsedTx,
      db_, allZcTxHashes_,
      ss);

   //parse it
   auto addrMap = scrAddrMap_->get();
   return filterParsedTx(parsedTx, *addrMap, bdvCallbacks_.get());
}

///////////////////////////////////////////////////////////////////////////////
map<BinaryData, shared_ptr<ParsedTx>> ZeroConfContainer::checkForCollisions(
   const map<BinaryDataRef, map<unsigned, BinaryDataRef>>& spentOutpoints,
   shared_ptr<MempoolSnapshot> ss)
{
   map<BinaryData, shared_ptr<ParsedTx>> invalidatedZCs;

   //loop through outpoints
   for (auto& idSet : spentOutpoints)
   {
      //compare them to the list of currently spent outpoints
      auto hashIter = outPointsSpentByKey_.find(idSet.first);
      if (hashIter == outPointsSpentByKey_.end())
         continue;

      set<BinaryData> keysToDrop;
      for (auto opId : idSet.second)
      {
         auto idIter = hashIter->second.find(opId.first);
         if (idIter != hashIter->second.end())
            keysToDrop.emplace(idIter->second);
      }

      for (auto& zcKey : keysToDrop)
      {
         //drop the zc, get the map of invalidated zc in return
         auto droppedTxs = dropZC(ss, zcKey);
         if (droppedTxs.empty())
            continue;

         //we need to track those to figure out which bdv to notify
         invalidatedZCs.insert(
            droppedTxs.begin(), droppedTxs.end());
      }
   }

   return invalidatedZCs;
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::clear()
{
   snapshot_.reset();
}

///////////////////////////////////////////////////////////////////////////////
bool ZeroConfContainer::isTxOutSpentByZC(const BinaryData& dbKey) const
{
   auto ss = getSnapshot();
   if (ss == nullptr)
      return false;

   return ss->isTxOutSpentByZC(dbKey);
}

///////////////////////////////////////////////////////////////////////////////
map<BinaryData, shared_ptr<const TxIOPair>> 
ZeroConfContainer::getUnspentZCforScrAddr(BinaryData scrAddr) const
{
   auto ss = getSnapshot();
   if (ss == nullptr)
      return {};

   auto txioMap = ss->getTxioMapForScrAddr(scrAddr);
   map<BinaryData, shared_ptr<const TxIOPair>> returnMap;

   for (auto& zcPair : txioMap)
   {
      if (zcPair.second->hasTxIn())
         continue;

      returnMap.insert(zcPair);
   }

   return returnMap;
}

///////////////////////////////////////////////////////////////////////////////
map<BinaryData, shared_ptr<const TxIOPair>> 
ZeroConfContainer::getRBFTxIOsforScrAddr(BinaryData scrAddr) const
{
   auto ss = getSnapshot();
   if (ss == nullptr)
      return {};

   auto txioMap = ss->getTxioMapForScrAddr(scrAddr);
   map<BinaryData, shared_ptr<const TxIOPair>> returnMap;

   for (auto& zcPair : txioMap)
   {
      if (!zcPair.second->hasTxIn())
         continue;

      if (!zcPair.second->isRBF())
         continue;

      returnMap.insert(zcPair);
   }

   return returnMap;
}

///////////////////////////////////////////////////////////////////////////////
vector<TxOut> ZeroConfContainer::getZcTxOutsForKey(
   const set<BinaryData>& keys) const
{
   auto ss = getSnapshot();
   if (ss == nullptr)
      return {};

   vector<TxOut> result;
   for (auto& key : keys)
   {
      auto zcKey = key.getSliceRef(0, 6);
      auto theTx = ss->getTxByKey(zcKey);
      if (theTx == nullptr)
         continue;

      auto outIdRef = key.getSliceRef(6, 2);
      auto outId = READ_UINT16_BE(outIdRef);

      auto&& txout = theTx->tx_.getTxOutCopy(outId);
      result.push_back(move(txout));
   }

   return result;
}

///////////////////////////////////////////////////////////////////////////////
vector<UTXO> ZeroConfContainer::getZcUTXOsForKey(
   const set<BinaryData>& keys) const
{
   auto ss = getSnapshot();
   if (ss == nullptr)
      return {};

   vector<UTXO> result;
   for (auto& key : keys)
   {
      auto zcKey = key.getSliceRef(0, 6);
      auto theTx = ss->getTxByKey(zcKey);
      if (theTx == nullptr)
         continue;

      auto zcIdRef = key.getSliceRef(2, 4);
      auto zcId = READ_UINT32_BE(zcIdRef);

      auto outIdRef = key.getSliceRef(6, 2);
      auto outId = READ_UINT16_BE(outIdRef);

      auto&& txout = theTx->tx_.getTxOutCopy(outId);
      UTXO utxo(
         txout.getValue(), UINT32_MAX, 
         zcId, outId,
         theTx->getTxHash(), txout.getScript());

      result.push_back(move(utxo));
   }

   return result;
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::updateZCinDB()
{
   while (true)
   {
      ZcUpdateBatch batch;
      try
      {
         batch = move(updateBatch_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      if (!batch.hasData())
         continue;

      auto&& tx = db_->beginTransaction(ZERO_CONF, LMDB::ReadWrite);
      for (auto& zc_pair : batch.zcToWrite_)
      {
         /*TODO: speed this up*/
         StoredTx zcTx;
         zcTx.createFromTx(zc_pair.second->tx_, true, true);
         db_->putStoredZC(zcTx, zc_pair.first);
      }

      for (auto& txhash : batch.txHashes_)
      {
         //if the key is not to be found in the txMap_, this is a ZC txhash
         db_->putValue(ZERO_CONF, txhash, BinaryData());
      }

      for (auto& key : batch.keysToDelete_)
      {
         BinaryData keyWithPrefix;
         if (key.getSize() == 6)
         {
            keyWithPrefix.resize(7);
            uint8_t* keyptr = keyWithPrefix.getPtr();
            keyptr[0] = DB_PREFIX_ZCDATA;
            memcpy(keyptr + 1, key.getPtr(), 6);
         }
         else
         {
            keyWithPrefix = key;
         }

         auto dbIter = db_->getIterator(ZERO_CONF);

         if (!dbIter->seekTo(keyWithPrefix))
            continue;

         vector<BinaryData> ktd;
         ktd.push_back(keyWithPrefix);

         do
         {
            BinaryDataRef thisKey = dbIter->getKeyRef();
            if (!thisKey.startsWith(keyWithPrefix))
               break;

            ktd.push_back(thisKey);
         } while (dbIter->advanceAndRead(DB_PREFIX_ZCDATA));

         for (auto _key : ktd)
            db_->deleteValue(ZERO_CONF, _key);
      }

      for (auto& key : batch.txHashesToDelete_)
         db_->deleteValue(ZERO_CONF, key);

      batch.setCompleted(true);
   }
}

///////////////////////////////////////////////////////////////////////////////
unsigned ZeroConfContainer::loadZeroConfMempool(bool clearMempool)
{
   unsigned topId = 0;
   map<BinaryData, shared_ptr<ParsedTx>> zcMap;

   {
      auto&& tx = db_->beginTransaction(ZERO_CONF, LMDB::ReadOnly);
      auto dbIter = db_->getIterator(ZERO_CONF);

      if (!dbIter->seekToStartsWith(DB_PREFIX_ZCDATA))
         return topId;

      do
      {
         BinaryDataRef zcKey = dbIter->getKeyRef();

         if (zcKey.getSize() == 7)
         {
            //Tx, grab it from DB
            StoredTx zcStx;
            db_->getStoredZcTx(zcStx, zcKey);

            //add to newZCMap_
            auto&& zckey = zcKey.getSliceCopy(1, 6);
            Tx zctx(zcStx.getSerializedTx());
            zctx.setTxTime(zcStx.unixTime_);

            auto parsedTx = make_shared<ParsedTx>(zckey);
            parsedTx->tx_ = move(zctx);

            zcMap.insert(move(make_pair(
               parsedTx->getKeyRef(), move(parsedTx))));
         }
         else if (zcKey.getSize() == 9)
         {
            //TxOut, ignore it
            continue;
         }
         else if (zcKey.getSize() == 32)
         {
            //tx hash
            allZcTxHashes_.insert(zcKey);
         }
         else
         {
            //shouldn't hit this
            LOGERR << "Unknown key found in ZC mempool";
            break;
         }
      } while (dbIter->advanceAndRead(DB_PREFIX_ZCDATA));
   }

   if (clearMempool == true)
   {
      LOGWARN << "Mempool was flagged for deletion!";
      ZcUpdateBatch batch;
      auto fut = batch.getCompletedFuture();

      for (const auto& zcTx : zcMap)
         batch.keysToDelete_.insert(zcTx.first);

      updateBatch_.push_back(move(batch));
      fut.wait();
   }
   else if (zcMap.size())
   {
      preprocessZcMap(zcMap, db_);

      //set highest used index
      auto lastEntry = zcMap.rbegin();
      auto& topZcKey = lastEntry->first;
      topId = READ_UINT32_BE(topZcKey.getSliceCopy(2, 4)) + 1;

      //no need to update the db nor notify bdvs on init
      map<BinaryData, shared_ptr<WatcherTxBody>> emptyWatcherMap;
      parseNewZC(
         move(zcMap), nullptr, false, false, 
         make_pair(string(), string()), 
         emptyWatcherMap);

      snapshot_->commitNewZCs();
   }

   return topId;
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::init(shared_ptr<ScrAddrFilter> saf, bool clearMempool)
{
   LOGINFO << "Enabling zero-conf tracking";

   scrAddrMap_ = saf->getZcFilterMapPtr();
   auto topId = loadZeroConfMempool(clearMempool);

   auto newZcPacketLbd = [this](ZcActionStruct zas)->void
   {
      this->parseNewZC(move(zas));
   };
   actionQueue_ = make_unique<ZcActionQueue>(
      newZcPacketLbd, zcPreprocessQueue_, topId);

   auto updateZcThread = [this](void)->void
   {
      updateZCinDB();
   };

   auto invTxThread = [this](void)->void
   {
      handleInvTx();
   };

   parserThreads_.push_back(thread(updateZcThread));
   parserThreads_.push_back(thread(invTxThread));
   increaseParserThreadPool(1);

   zcEnabled_.store(true, memory_order_relaxed);
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::pushZcPreprocessVec(shared_ptr<RequestZcPacket> req)
{
   if (req->hashes_.size() == 0)
      return;

   //register batch with main zc processing thread
   actionQueue_->initiateZcBatch(
      req->hashes_, ZC_GETDATA_TIMEOUT_MS, {}, false);

   //queue up individual requests for parser threads to process
   zcPreprocessQueue_->push_back(move(req));
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::handleInvTx()
{
   shared_ptr<RequestZcPacket> request = nullptr;

   while (true)
   {
      shared_ptr<ZcPreprocessPacket> packet;
      ZcPreprocessPacketType packetType;
      try
      {
         //pop every seconds
         packet = move(zcWatcherQueue_.pop_front(
            std::chrono::milliseconds(1000)));
         packetType = packet->type();
      }
      catch (const StackTimedOutException&)
      {
         //progress with an empty packet
         packetType = ZcPreprocessPacketType_Inv;
      }
      catch (const StopBlockingLoop&)
      {
         break;
      }

      switch (packetType)
      {
      case ZcPreprocessPacketType_Inv:
      {
         //skip this entirely if there are no addresses to scan the ZCs against
         if (scrAddrMap_->size() == 0 &&
            DBSettings::getDbType() != ARMORY_DB_SUPER)
            continue;

         auto invPayload = dynamic_pointer_cast<ZcInvPayload>(packet);
         if (invPayload != nullptr && invPayload->watcher_)
         {
            /*
            This is an inv tx payload from the watcher node, check it against 
            our outstanding broadcasts
            */
            SingleLock lock(&watcherMapMutex_);
            for (auto& invEntry : invPayload->invVec_)
            {
               BinaryData bd(invEntry.hash, sizeof(invEntry.hash));
               auto iter = watcherMap_.find(bd);
               if (iter == watcherMap_.end() || 
                  iter->second->inved_ ||
                  iter->second->ignoreWatcherNodeInv_)
               {
                  continue;
               }

               //mark as fetched
               iter->second->inved_ = true;

               //set parsedTx tx body
               auto payloadTx = make_shared<ProcessPayloadTxPacket>(bd);
               payloadTx->rawTx_ = iter->second->rawTxPtr_;

               //push to preprocess threads
               actionQueue_->queueGetDataResponse(move(payloadTx));
            }
         }
         else
         {         
            /*
            inv tx from the process node, send a getdata request for these hashes
            */

            if (request == nullptr)
               request = make_shared<RequestZcPacket>();

            if (invPayload != nullptr)
            {
               auto& invVec = invPayload->invVec_;
               if (parserThreadCount_ < invVec.size() &&
                  parserThreadCount_ < maxZcThreadCount_)
                  increaseParserThreadPool(invVec.size());

               SingleLock lock(&watcherMapMutex_);

               for (auto& entry : invVec)
               {
                  BinaryDataRef hash(entry.hash, sizeof(entry.hash));

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
                  if (watcherMap_.find(hash) != watcherMap_.end())
                     continue;

                  request->hashes_.emplace_back(hash);
               }
            }
         
            if (!request->ready())
               break;

            pushZcPreprocessVec(request);
            request.reset();
         }

         //register batch with main zc processing thread
         break;
      }

      default: 
         throw runtime_error("invalid packet");
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::handleZcProcessingStructThread(void)
{
   while (1)
   {
      shared_ptr<ZcGetPacket> packet;
      try
      {
         packet = move(zcPreprocessQueue_->pop_front());
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      switch (packet->type_)
      {
      case ZcGetPacketType_Request:
      {
         auto request = dynamic_pointer_cast<RequestZcPacket>(packet);
         if (request != nullptr)
            requestTxFromNode(*request);

         break;
      }

      case ZcGetPacketType_Payload:
      {
         auto payloadTx = dynamic_pointer_cast<ProcessPayloadTxPacket>(packet);
         if (payloadTx == nullptr)
            throw runtime_error("unexpected payload type");

         processPayloadTx(payloadTx);
         break;
      }

      case ZcGetPacketType_Broadcast:
      {
         auto broadcastPacket = dynamic_pointer_cast<ZcBroadcastPacket>(packet);
         if (broadcastPacket == nullptr)
            break;
            
         pushZcPacketThroughP2P(*broadcastPacket);
         break;
      }

      default:
         break;
      } //switch
   } //while
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::processTxGetDataReply(unique_ptr<Payload> payload)
{
   switch (payload->type())
   {
   case Payload_tx:
   {
      shared_ptr<Payload> payload_sptr(move(payload));
      auto payloadtx = dynamic_pointer_cast<Payload_Tx>(payload_sptr);
      if (payloadtx == nullptr || payloadtx->getSize() == 0)
      {
         LOGERR << "invalid tx getdata payload";
         return;
      }

      //got a tx, post it to the zc preprocessing queue
      auto txData = make_shared<ProcessPayloadTxPacket>(payloadtx->getHash256());
      txData->rawTx_ = make_shared<BinaryData>(
         &payloadtx->getRawTx()[0], payloadtx->getRawTx().size());

      actionQueue_->queueGetDataResponse(move(txData));
      break;
   }

   case Payload_reject:
   {
      shared_ptr<Payload> payload_sptr(move(payload));
      auto payloadReject = dynamic_pointer_cast<Payload_Reject>(payload_sptr);
      if (payloadReject == nullptr)
      {
         LOGERR << "invalid reject payload";
         return;
      }

      if (payloadReject->rejectType() != Payload_tx)
      {
         //only handling payload_tx rejections
         return;
      }

      BinaryData hash(
         &payloadReject->getExtra()[0], 
         payloadReject->getExtra().size());

      auto rejectPacket = make_shared<RejectPacket>(hash, payloadReject->code());
      actionQueue_->queueGetDataResponse(move(rejectPacket));
      break;
   }

   default:
      break;
   }
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::requestTxFromNode(RequestZcPacket& packet)
{
   vector<InvEntry> invVec;
   for (auto& hash : packet.hashes_)
   {
      if (hash.getSize() != 32)
         throw runtime_error("invalid inv hash length");

      InvEntry inv;
      inv.invtype_ = Inv_Msg_Witness_Tx;
      memcpy(inv.hash, hash.getPtr(), 32);
      invVec.emplace_back(move(inv));
   }
   networkNode_->requestTx(move(invVec));
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::processPayloadTx(
   shared_ptr<ProcessPayloadTxPacket> payloadPtr)
{
   if (payloadPtr->rawTx_->getSize() == 0)
   {
      payloadPtr->pTx_->state_ = ParsedTxStatus::Invalid;
      payloadPtr->incrementCounter();
      return;
   }

   //set raw tx and current time
   payloadPtr->pTx_->tx_.unserialize(*payloadPtr->rawTx_);
   payloadPtr->pTx_->tx_.setTxTime(time(0));

   preprocessTx(*payloadPtr->pTx_, db_);
   payloadPtr->incrementCounter();
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::broadcastZC(
   const vector<BinaryDataRef>& rawZcVec, uint32_t timeout_ms,
   const ZcBroadcastCallback& cbk, 
   const string& bdvID, const string& requestID)
{
   auto zcPacket = make_shared<ZcBroadcastPacket>();
   zcPacket->hashes_.reserve(rawZcVec.size());
   zcPacket->zcVec_.reserve(rawZcVec.size());

   for (auto& rawZcRef : rawZcVec)
   {
      if (rawZcRef.getSize() == 0)
         continue;

      auto rawZcPtr = make_shared<BinaryData>(rawZcRef);
      Tx tx(*rawZcPtr);

      zcPacket->hashes_.push_back(tx.getThisHash());
      zcPacket->zcVec_.push_back(rawZcPtr);
   }

   if (zcPacket->zcVec_.empty())
      return;

   {
      //update the watcher map
      ReentrantLock lock(&watcherMapMutex_);
      for (unsigned i=0; i < zcPacket->hashes_.size(); i++)
      {
         auto& hash = zcPacket->hashes_[i];
         map<string, string> emptyMap;
         if (insertWatcherEntry(
            hash, zcPacket->zcVec_[i],
            bdvID, requestID, emptyMap))
         {
            continue;
         }

         //already have this zc in an earlier batch, drop the hash
         hash.clear();
      }
   }

   //sets up & queues the zc batch for us
   if (actionQueue_->initiateZcBatch(
      zcPacket->hashes_, timeout_ms, cbk, true, bdvID, requestID) == nullptr)
   {
      //return if no batch was created
      return;
   }

   //push each zc on the process queue
   zcPreprocessQueue_->push_back(zcPacket);
}

///////////////////////////////////////////////////////////////////////////////
bool ZeroConfContainer::insertWatcherEntry(
   const BinaryData& hash, shared_ptr<BinaryData> rawTxPtr, 
   const std::string& bdvID, const std::string& requestID,
   std::map<std::string, std::string>& extraRequestors,
   bool watchEntry)
{
   //lock 
   ReentrantLock lock(&watcherMapMutex_);

   auto iter = watcherMap_.find(hash);

   //try to insert
   if (iter == watcherMap_.end())
   {
      auto insertIter = watcherMap_.emplace(
         hash, 
         make_shared<WatcherTxBody>(rawTxPtr));

      //set the watcher node flag
      insertIter.first->second->ignoreWatcherNodeInv_ = !watchEntry;

      //set extra requestors
      if (!extraRequestors.empty())
         insertIter.first->second->extraRequestors_ = move(extraRequestors);

      //return true for successful insertion
      return true;
   }
   else
   {
      //already have this hash, do not change the watcher node flag

      //tie this request to the existing watcher entry
      iter->second->extraRequestors_.emplace(
         requestID, bdvID);
      
      //add the extra requestors if any
      if (!extraRequestors.empty())
      {
         iter->second->extraRequestors_.insert(
            extraRequestors.begin(), extraRequestors.end());
      }

      //return false for failed insertion
      return false;
   }
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<WatcherTxBody> ZeroConfContainer::eraseWatcherEntry(
   const BinaryData& hash)
{
   //lock 
   ReentrantLock lock(&watcherMapMutex_);

   auto iter = watcherMap_.find(hash);
   if (iter == watcherMap_.end())
      return nullptr;

   auto objPtr = move(iter->second);   
   watcherMap_.erase(iter);
   
   return objPtr;
}

///////////////////////////////////////////////////////////////////////////////
std::shared_ptr<ZeroConfBatch> ZeroConfContainer::initiateZcBatch(
   const vector<BinaryData>& zcHashes, unsigned timeout, 
   const ZcBroadcastCallback& cbk, bool hasWatcherEntries,
   const std::string& bdvId, const std::string& requestId)
{
   return actionQueue_->initiateZcBatch(
      zcHashes, timeout, 
      cbk, hasWatcherEntries, 
      bdvId, requestId);
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::pushZcPacketThroughP2P(ZcBroadcastPacket& packet)
{
   if (!networkNode_->connected())
   {
      LOGWARN << "node is offline, cannot broadcast";

      //TODO: report node down errors to batch
      /*packet.errorCallback_(
         move(*packet.rawZc_), ZCBroadcastStatus_P2P_NodeDown);*/
      return;
   }

   //create inv payload
   vector<InvEntry> invVec;
   map<BinaryData, shared_ptr<BitcoinP2P::getDataPayload>> getDataPair;

   for (unsigned i=0; i<packet.hashes_.size(); i++)
   {
      auto& hash = packet.hashes_[i];
      if (hash.empty())
         continue;
         
      auto& rawZc = packet.zcVec_[i];

      //create inv entry, this announces the zc by its hash to the node
      InvEntry entry;
      entry.invtype_ = Inv_Msg_Witness_Tx;
      memcpy(entry.hash, hash.getPtr(), 32);
      invVec.push_back(entry);

      //create getData payload packet, this is the zc body for the node to
      //grab once it knows of the hash
      auto&& payload = make_unique<Payload_Tx>();
      vector<uint8_t> rawtx;
      rawtx.resize(rawZc->getSize());
      memcpy(&rawtx[0], rawZc->getPtr(), rawZc->getSize());

      payload->setRawTx(move(rawtx));
      auto getDataPayload = make_shared<BitcoinP2P::getDataPayload>();
      getDataPayload->payload_ = move(payload);

      getDataPair.emplace(hash, getDataPayload);
   }

   //register getData payload
   networkNode_->getDataPayloadMap_.update(move(getDataPair));

   //send inv packet
   auto payload_inv = make_unique<Payload_Inv>();
   payload_inv->setInvVector(invVec);
   networkNode_->sendMessage(move(payload_inv));
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::shutdown()
{
   if (actionQueue_ != nullptr)
      actionQueue_->shutdown();

   zcWatcherQueue_.terminate();
   zcPreprocessQueue_->terminate();
   updateBatch_.terminate();

   vector<thread::id> idVec;
   for (auto& parser : parserThreads_)
   {
      idVec.push_back(parser.get_id());
      if (parser.joinable())
         parser.join();
   }
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::increaseParserThreadPool(unsigned count)
{
   unique_lock<mutex> lock(parserThreadMutex_);

   //start Zc parser thread
   auto processZcThread = [this](void)->void
   {
      handleZcProcessingStructThread();
   };

   for (unsigned i = parserThreadCount_; i < count; i++)
      parserThreads_.push_back(thread(processZcThread));

   parserThreadCount_ = parserThreads_.size();
   LOGINFO << "now running " << parserThreadCount_ << " zc parser threads";
}

////////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::setWatcherNode(
   shared_ptr<BitcoinNodeInterface> watcherNode)
{
   auto getTxLambda = [this](vector<InvEntry> invVec)->void
   {
      if (!zcEnabled_.load(std::memory_order_relaxed))
         return;

      //push inv vector as watcher inv packet on the preprocessing queue
      auto payload = make_shared<ZcInvPayload>(true);
      payload->invVec_ = move(invVec);
      zcWatcherQueue_.push_back(move(payload));
   };

   watcherNode->registerInvTxLambda(getTxLambda);
}

////////////////////////////////////////////////////////////////////////////////
BatchTxMap ZeroConfContainer::getBatchTxMap(shared_ptr<ZeroConfBatch> batch, 
   shared_ptr<MempoolSnapshot> ss)
{
   if (batch == nullptr)
      throw ZcBatchError();

   //wait on the batch for the duration of the 
   //timeout minus time elapsed since creation
   unsigned timeLeft = 0;
   auto delay = chrono::duration_cast<chrono::milliseconds>(
      chrono::system_clock::now() - batch->creationTime_);
   if (delay.count() < batch->timeout_)
      timeLeft = batch->timeout_ - delay.count();

   auto timeLeftMs = chrono::milliseconds(timeLeft);

   ArmoryErrorCodes batchResult;
   if (batch->timeout_ > 0 && 
      batch->isReadyFut_.wait_for(timeLeftMs) != future_status::ready)
   {
      batchResult = ArmoryErrorCodes::ZcBatch_Timeout;
   }
   else
   {
      batchResult = batch->isReadyFut_.get();
   }

   BatchTxMap result;
   result.requestor_ = batch->requestor_;

   //purge the watcher map of the hashes this batch registered
   if (batch->hasWatcherEntries_)
   {
      SingleLock lock(&watcherMapMutex_);
      for (auto& keyPair : batch->hashToKeyMap_)
      {
         auto iter = watcherMap_.find(keyPair.first);
         if (iter == watcherMap_.end())
         {
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

   if (batchResult != ArmoryErrorCodes::Success)
   {
      /*
      Failed to get transactions for batch, fire the error callback
      */
      
      //skip if this batch doesn't have a callback
      if (!batch->errorCallback_ || !batch->hasWatcherEntries_)
         throw ZcBatchError();

      unsigned invedZcCount = 0;
      vector<ZeroConfBatchFallbackStruct> txVec;
      set<BinaryDataRef> purgedHashes;
      txVec.reserve(batch->zcMap_.size());

      //purge the batch of missing tx and their children
      for (auto& txPair : batch->zcMap_)
      {
         bool purge = false;

         //does this tx depend on a purged predecessor?
         for (auto& txInObj : txPair.second->inputs_)
         {
            auto parentIter = purgedHashes.find(
               txInObj.opRef_.getTxHashRef());

            if (parentIter == purgedHashes.end())
               continue;

            //this zc depends on a purged zc, purge it too
            purge = true;
            break;
         }

         //was this tx inv'ed back to us?
         bool inved = true;
         auto iter = result.watcherMap_.find(txPair.second->getTxHash());

         //map consistency is assured in the watcherMap purge 
         //scope, this iterator is guaranteed valid
         if (!iter->second->inved_)
         {
            inved = false;
            purge = true;
         }
                        
         if (!purge)
         {
            //sanity check
            if (!inved)
            {
               LOGWARN << "keeping zc from timed out batch"
                  << " that wasn't inved";
            }

            //we're keeping this tx
            ++invedZcCount;
            continue;
         }

         //create the fallback struct
         ZeroConfBatchFallbackStruct fallbackStruct;
         fallbackStruct.txHash_ = iter->first;
         fallbackStruct.rawTxPtr_ = move(iter->second->rawTxPtr_);
         fallbackStruct.err_ = batchResult;
         fallbackStruct.extraRequestors_ = move(iter->second->extraRequestors_);

         //check snapshot for collisions
         if (ss->hasHash(iter->first.getRef()))
         {
            //already have this tx in our mempool, report to callback
            //but don't flag hash as purged (children need to be processed if any)
            fallbackStruct.err_ = ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool;
         }
         else
         {
            //keep track of purged zc hashes
            purgedHashes.insert(iter->first.getRef());
         }

         //flag tx to be skipped by parser
         txPair.second->state_ = ParsedTxStatus::Skip;

         //add to vector for error callback
         txVec.emplace_back(move(fallbackStruct));
      }

      batch->errorCallback_(move(txVec));

      //don't forward the batch if it has no zc ready to be parsed
      if (invedZcCount == 0)
         throw ZcBatchError();

      //we have some inv'ed zc to parse but the batch timed out, we need to
      //wait on the counter to match our local count of valid tx.
      while (batch->zcMap_.size() - batch->counter_->load(memory_order_acquire) < 
         invedZcCount)
      {
         LOGWARN << "timedout batch waiting on " << invedZcCount << " inved tx: ";
         LOGWARN << "batch size: " << batch->zcMap_.size() << ", counter: " << 
            batch->counter_->load(memory_order_acquire);
         this_thread::sleep_for(chrono::milliseconds(100));
      }
   }

   result.txMap_ = batch->zcMap_;
   return result;
}

///////////////////////////////////////////////////////////////////////////////
unsigned ZeroConfContainer::getMatcherMapSize(void) const 
{ 
   return actionQueue_->getMatcherMapSize(); 
}

///////////////////////////////////////////////////////////////////////////////
unsigned ZeroConfContainer::getMergeCount(void) const
{
   auto ss = getSnapshot();
   if (ss == nullptr)
      return 0;

   return ss->getMergeCount();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// ZcActionQueue
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void ZcActionQueue::start()
{
   auto processZcThread = [this](void)->void
   {
      processNewZcQueue();
   };

   auto matcherThread = [this](void)->void
   {
      getDataToBatchMatcherThread();
   };

   processThreads_.push_back(thread(processZcThread));
   processThreads_.push_back(thread(matcherThread));
}

////////////////////////////////////////////////////////////////////////////////
void ZcActionQueue::shutdown()
{
   newZcQueue_.terminate();
   getDataResponseQueue_.terminate();
   for (auto& thr : processThreads_)
   {
      if (thr.joinable())
         thr.join();
   }
}

////////////////////////////////////////////////////////////////////////////////
BinaryData ZcActionQueue::getNewZCkey()
{
   uint32_t newId = topId_.fetch_add(1, memory_order_relaxed);
   BinaryData newKey = READHEX("ffff");
   newKey.append(WRITE_UINT32_BE(newId));

   return newKey;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<ZeroConfBatch> ZcActionQueue::initiateZcBatch(
   const vector<BinaryData>& zcHashes, unsigned timeout, 
   const ZcBroadcastCallback& cbk, bool hasWatcherEntries,
   const std::string& bdvId, const std::string& requestId)
{
   auto batch = make_shared<ZeroConfBatch>(hasWatcherEntries);
   batch->requestor_ = make_pair(requestId, bdvId);

   for (auto& hash : zcHashes)
   {
      //skip if hash is empty
      if (hash.empty())
         continue;

      auto&& key = getNewZCkey();
      auto ptx = make_shared<ParsedTx>(key);
      ptx->setTxHash(hash);

      batch->hashToKeyMap_.emplace(
         make_pair(ptx->getTxHash().getRef(), ptx->getKeyRef()));
      batch->zcMap_.emplace(make_pair(ptx->getKeyRef(), ptx));
   }

   if (batch->zcMap_.empty())
   {
      //empty batch, skip
      return nullptr;
   }

   batch->counter_->store(batch->zcMap_.size(), memory_order_relaxed);
   batch->timeout_ = timeout; //in milliseconds
   batch->errorCallback_ = cbk;

   ZcActionStruct zac;
   zac.action_ = Zc_NewTx;
   zac.batch_ = batch;
   newZcQueue_.push_back(move(zac));

   auto batchCopy = batch;
   batchQueue_.push_back(move(batchCopy));

   return batch;
}

////////////////////////////////////////////////////////////////////////////////
void ZcActionQueue::processNewZcQueue()
{
   while (1)
   {
      ZcActionStruct zcAction;
      map<BinaryData, shared_ptr<ParsedTx>> zcMap;
      try
      {
         zcAction = move(newZcQueue_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      /*
      Populate local map with batch's zcMap_ so that we can cleanup the
      hashes from the request map after parsing.
      */
      if (zcAction.batch_ != nullptr)
      {
         /*
         We can't just grab the hash reference since the object referred to is
         held by a ParsedTx and that has no guarantee of surviving the parsing 
         function, hence copying the entire map.
         */
         zcMap = zcAction.batch_->zcMap_;
      }

      newZcFunction_(move(zcAction));

      if (zcMap.empty())
         continue;

      //cleanup request map
      set<BinaryData> hashSet;
      for (auto& zcPair : zcMap)
         hashSet.insert(zcPair.second->getTxHash());
      hashesToClear_.push_back(move(hashSet));
   }
}

////////////////////////////////////////////////////////////////////////////////
shared_future<shared_ptr<ZcPurgePacket>> 
ZcActionQueue::pushNewBlockNotification(
   Blockchain::ReorganizationState reorgState)
{
   ZcActionStruct zcaction;
   zcaction.action_ = Zc_Purge;
   zcaction.resultPromise_ =
      make_unique<promise<shared_ptr<ZcPurgePacket>>>();
   zcaction.reorgState_ = reorgState;

   auto fut = zcaction.resultPromise_->get_future();
   newZcQueue_.push_back(move(zcaction));
   
   return fut;
}

////////////////////////////////////////////////////////////////////////////////
void ZcActionQueue::queueGetDataResponse(std::shared_ptr<ZcGetPacket> payloadTx)
{
   getDataResponseQueue_.push_back(move(payloadTx));
}

////////////////////////////////////////////////////////////////////////////////
void ZcActionQueue::getDataToBatchMatcherThread()
{
   bool run = true;
   map<BinaryData, shared_ptr<ZeroConfBatch>> hashToBatchMap;
   while (run)
   {
      //queue of outstanding node getdata packets that need matched with 
      //their parent batch - blocking
      shared_ptr<ZcGetPacket> zcPacket;
      try
      {    
         zcPacket = getDataResponseQueue_.pop_front();
      }
      catch (const StopBlockingLoop&)
      {
         run = false;
      }

      //queue of new batches - non blocking
      while (true)
      {
         try
         {
            auto batch = batchQueue_.pop_front();

            //populate local map with hashes from this batch, do not
            //overwrite existing entries (older batches should get 
            //precedence over a shared tx hash)
            for (auto& hashPair : batch->hashToKeyMap_)
               hashToBatchMap.emplace(hashPair.first, batch);
         }
         catch (const IsEmpty&)
         {
            break;
         }
      }

      if (zcPacket != nullptr)
      {
         switch (zcPacket->type_)
         {
         case ZcGetPacketType_Payload:
         {
            auto payloadTx = dynamic_pointer_cast<ProcessPayloadTxPacket>(zcPacket);
            if (payloadTx == nullptr)
               break;

            //look for parent batch in local map
            auto iter = hashToBatchMap.find(payloadTx->txHash_);
            if (iter != hashToBatchMap.end())
            {
               //tie the tx to its batch
               payloadTx->batchCtr_ = iter->second->counter_;
               payloadTx->batchProm_ = iter->second->isReadyPromise_;
                  
               auto keyIter = iter->second->hashToKeyMap_.find(
                  payloadTx->txHash_.getRef());
               if (keyIter != iter->second->hashToKeyMap_.end())
               { 
                  auto txIter = iter->second->zcMap_.find(keyIter->second);
                  if (txIter != iter->second->zcMap_.end())
                  {
                     payloadTx->pTx_ = txIter->second;
                     zcPreprocessQueue_->push_back(payloadTx);
                  }
               }

               hashToBatchMap.erase(iter);
            }

            break;
         }

         case ZcGetPacketType_Reject:
         {
            auto rejectPacket = dynamic_pointer_cast<RejectPacket>(zcPacket);
            if (rejectPacket == nullptr)
               break;

            //grab the batch
            auto iter = hashToBatchMap.find(rejectPacket->txHash_);
            if (iter != hashToBatchMap.end())
            {
               iter->second->isReadyPromise_->set_value(
                  ArmoryErrorCodes(rejectPacket->code_));
               
               hashToBatchMap.erase(iter);
            }

            break;
         }

         default:
            break;
         }
      }

      //queue of hashes to purge from the local map
      while (true)
      {
         try
         {
            auto&& hashSet = hashesToClear_.pop_front();
            for (auto& hash : hashSet)
               hashToBatchMap.erase(hash);
         }
         catch (const IsEmpty&)
         {
            break;
         }
      }

      matcherMapSize_.store(hashToBatchMap.size(), memory_order_relaxed);
   }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// ZeroConfCallbacks
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
ZeroConfCallbacks::~ZeroConfCallbacks() 
{}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// ZcUpdateBatch
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
shared_future<bool> ZcUpdateBatch::getCompletedFuture()
{
   if (completed_ == nullptr)
      completed_ = make_unique<promise<bool>>();
   return completed_->get_future();
}

////////////////////////////////////////////////////////////////////////////////
void ZcUpdateBatch::setCompleted(bool val)
{
   if (completed_ == nullptr)
      return;

   completed_->set_value(val);
}

////////////////////////////////////////////////////////////////////////////////
bool ZcUpdateBatch::hasData() const
{
   if (zcToWrite_.size() > 0 ||
      txHashes_.size() > 0 ||
      keysToDelete_.size() > 0)
      return true;
   
   return false;
}