////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2018, goatpig                                          //            
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                   
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ZeroConf.h"
#include "BlockDataMap.h"

///////////////////////////////////////////////////////////////////////////////
//ZeroConfContainer Methods
///////////////////////////////////////////////////////////////////////////////
map<BinaryData, TxIOPair> ZeroConfContainer::emptyTxioMap_;

BinaryData ZeroConfContainer::getNewZCkey()
{
   uint32_t newId = topId_.fetch_add(1, memory_order_relaxed);
   BinaryData newKey = READHEX("ffff");
   newKey.append(WRITE_UINT32_BE(newId));

   return move(newKey);
}

///////////////////////////////////////////////////////////////////////////////
Tx ZeroConfContainer::getTxByHash(const BinaryData& txHash) const
{
   auto txhashmap = txHashToDBKey_.get();
   const auto keyIter = txhashmap->find(txHash);

   if (keyIter == txhashmap->end())
      return Tx();

   auto txmap = txMap_.get();
   auto txiter = txmap->find(keyIter->second);

   if (txiter == txmap->end())
      return Tx();

   auto& theTx = txiter->second.tx_;
   theTx.setTxRef(TxRef(keyIter->second));

   return theTx;
}
///////////////////////////////////////////////////////////////////////////////
bool ZeroConfContainer::hasTxByHash(const BinaryData& txHash) const
{
   auto txhashmap = txHashToDBKey_.get();
   return (txhashmap->find(txHash) != txhashmap->end());
}

///////////////////////////////////////////////////////////////////////////////
map<BinaryData, BinaryData> ZeroConfContainer::purge(
   const Blockchain::ReorganizationState& reorgState,
   map<BinaryData, ParsedTx>& zcMap)
{
   if (db_ == nullptr || zcMap.size() == 0)
      return map<BinaryData, BinaryData>();

   vector<BinaryData> ktdVec;
   map<BinaryData, BinaryData> minedKeys;

   auto updateChildren = [&zcMap, &minedKeys, this](
      const BinaryData& txHash, const BinaryData& blockKey,
      map<BinaryData, unsigned> minedHashes)->void
   {
      auto spentIter = outPointsSpentByKey_.find(txHash);
      if (spentIter == outPointsSpentByKey_.end())
         return;

      //is this zc mined or just invalidated?
      auto minedIter = minedHashes.find(txHash);
      if (minedIter == minedHashes.end())
         return;
      auto txid = minedIter->second;

      //list children by key
      set<BinaryDataRef> keysToClear;
      for (auto& op_pair : spentIter->second)
         keysToClear.insert(op_pair.second.getRef());

      //run through children, replace key
      for (auto& zckey : keysToClear)
      {
         auto zcIter = zcMap.find(zckey);
         if (zcIter == zcMap.end())
            continue;

         for (auto& input : zcIter->second.inputs_)
         {
            if (input.opRef_.getTxHashRef() != txHash)
               continue;

            auto prevKey = input.opRef_.getDbKey();
            input.opRef_.reset();

            BinaryWriter bw_key;
            bw_key.put_BinaryData(blockKey);
            bw_key.put_uint16_t(txid, BE);
            bw_key.put_uint16_t(input.opRef_.getIndex(), BE);

            minedKeys.insert(make_pair(prevKey, bw_key.getData()));
            input.opRef_.getDbKey() = bw_key.getData();

            zcIter->second.isChainedZc_ = false;
         }
      }
   };

   //lambda to purge zc map per block
   auto purgeZcMap =
      [&zcMap, &ktdVec, &reorgState, &minedKeys, this, updateChildren](
         map<BinaryDataRef, set<unsigned>>& spentOutpoints,
         map<BinaryData, unsigned> minedHashes,
         const BinaryData& blockKey)->void
   {
      auto txhashmap = txHashToDBKey_.get();

      auto zcMapIter = zcMap.begin();
      while (zcMapIter != zcMap.end())
      {
         auto& zc = zcMapIter->second;
         bool invalidated = false;
         for (auto& input : zc.inputs_)
         {
            auto opIter = spentOutpoints.find(input.opRef_.getTxHashRef());
            if (opIter == spentOutpoints.end())
               continue;

            auto indexIter = opIter->second.find(input.opRef_.getIndex());
            if (indexIter == opIter->second.end())
               continue;

            //the outpoint for this zc is spent, invalidate the zc
            invalidated = true;
            break;
         }

         if (!invalidated)
         {
            ++zcMapIter;
            continue;
         }

         //mark for deletion
         ktdVec.push_back(zcMapIter->first);

         //delete zc from map
         auto zcMove = move(zcMapIter->second);
         zcMap.erase(zcMapIter++);

         //this zc is now invalid, check if it has children
         auto& zchash = zcMove.getTxHash();
         updateChildren(zchash, blockKey, minedHashes);
      }
   };

   if (!reorgState.prevTopStillValid_)
   {
      //reset resolved outpoints cause of reorg
      for (auto& zc_pair : zcMap)
         zc_pair.second.reset();
   }

   auto getIdSpoofLbd = [](const BinaryData&)->unsigned
   {
      return 0;
   };

   //get all txhashes for the new blocks
   auto bcPtr = db_->blockchain();
   try
   {
      auto currentHeader = reorgState.prevTop_;
      if (!reorgState.prevTopStillValid_)
         currentHeader = reorgState.reorgBranchPoint_;

      //get the next header
      currentHeader = bcPtr->getHeaderByHash(currentHeader->getNextHash());

      //loop over headers
      while (currentHeader != nullptr)
      {
         //grab block
         auto&& rawBlock = db_->getRawBlock(
            currentHeader->getBlockHeight(),
            currentHeader->getDuplicateID());

         BlockData block;
         block.deserialize(
            rawBlock.getPtr(), rawBlock.getSize(),
            currentHeader, getIdSpoofLbd,
            false, false);

         //build up hash set
         map<BinaryDataRef, set<unsigned>> spentOutpoints;
         map<BinaryData, unsigned> minedHashes;
         auto txns = block.getTxns();
         for (unsigned txid = 0; txid < txns.size(); txid++)
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

            minedHashes.insert(make_pair(txn->getHash(), txid));
         }

         purgeZcMap(spentOutpoints, minedHashes,
            currentHeader->getBlockDataKey());

         if (BlockDataManagerConfig::getDbType() != ARMORY_DB_SUPER)
         {
            //purge mined hashes
            for (auto& minedHash : minedHashes)
            {
               allZcTxHashes_.erase(minedHash.first);
               ktdVec.push_back(minedHash.first);
            }
         }

         //next block
         if (currentHeader->getThisHash() == reorgState.newTop_->getThisHash())
            break;

         auto& bhash = currentHeader->getNextHash();
         currentHeader = bcPtr->getHeaderByHash(bhash);
      }
   }
   catch (...)
   {
   }

   if (reorgState.prevTopStillValid_)
   {
      set<BinaryData> ktdSet;
      for (auto& key : ktdVec)
         ktdSet.insert(key);
      dropZC(ktdSet);
   }
   else
   {
      //reset containers and resolve outpoints anew after a reorg
      reset();
      //delete keys from DB
      auto deleteKeys = [&](void)->void
      {
         this->updateZCinDB(vector<BinaryData>(), ktdVec);
      };

      preprocessZcMap(zcMap);
      thread deleteKeyThread(deleteKeys);
      if (deleteKeyThread.joinable())
         deleteKeyThread.join();
   }

   return minedKeys;
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::preprocessZcMap(map<BinaryData, ParsedTx>& zcMap)
{
   //run threads to preprocess the zcMap
   auto counter = make_shared<atomic<unsigned>>();
   counter->store(0, memory_order_relaxed);

   vector<ParsedTx*> txVec;
   for (auto& txPair : zcMap)
      txVec.push_back(&txPair.second);

   auto parserLdb = [this, &txVec, counter](void)->void
   {
      while (1)
      {
         auto id = counter->fetch_add(1, memory_order_relaxed);
         if (id >= txVec.size())
            return;

         auto txIter = txVec.begin() + id;
         this->preprocessTx(*(*txIter));
      }
   };

   vector<thread> parserThreads;
   for (unsigned i = 1; i < thread::hardware_concurrency(); i++)
      parserThreads.push_back(thread(parserLdb));
   parserLdb();

   for (auto& thr : parserThreads)
   {
      if (thr.joinable())
         thr.join();
   }
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::reset()
{
   txHashToDBKey_.clear();
   txMap_.clear();
   txioMap_.clear();
   keyToSpentScrAddr_.clear();
   txOutsSpentByZC_.clear();
   outPointsSpentByKey_.clear();
   keyToFundedScrAddr_.clear();
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::dropZC(const set<BinaryData>& zcKeys)
{
   if (zcKeys.size() == 0)
      return;

   vector<BinaryData> keysToDelete;
   vector<BinaryData> txoutsToDelete;
   vector<BinaryData> hashesToDelete;

   map<BinaryData, shared_ptr<map<BinaryData, TxIOPair>>> updateMap;

   {
      auto txiomapPtr = txioMap_.get();

      auto dropTxios = [txiomapPtr, &updateMap](
         const BinaryData& zcKey,
         const BinaryData& scrAddr)->void
      {
         auto mapIter = updateMap.find(scrAddr);
         if (mapIter == updateMap.end())
         {
            auto mapIter2 = txiomapPtr->find(scrAddr);
            if (mapIter2 == txiomapPtr->end())
               return;

            auto newmap = make_shared<map<BinaryData, TxIOPair>>(
               *(mapIter2->second));
            mapIter = updateMap.insert(make_pair(scrAddr, newmap)).first;
         }

         auto txioIter = mapIter->second->begin();
         while (txioIter != mapIter->second->end())
         {
            if (txioIter->first.startsWith(zcKey))
            {
               mapIter->second->erase(txioIter++);
               continue;
            }

            if (!txioIter->second.hasTxIn() ||
               !txioIter->second.getDBKeyOfInput().startsWith(zcKey))
            {
               ++txioIter;
               continue;
            }

            if (!txioIter->second.hasTxOutZC())
            {
               mapIter->second->erase(txioIter++);
               continue;
            }

            txioIter->second.setTxIn(BinaryData());
            ++txioIter;
         }
      };

      auto keytospendsaPtr = keyToSpentScrAddr_.get();
      auto txmapPtr = txMap_.get();
      auto txhashmapPtr = txHashToDBKey_.get();
      auto txoutset = txOutsSpentByZC_.get();


      auto rKeyIter = zcKeys.rbegin();
      while (rKeyIter != zcKeys.rend())
      {
         auto& zcKey = *rKeyIter;
         ++rKeyIter;

         //resolve txHash
         auto zcIter = txmapPtr->find(zcKey);
         if (zcIter == txmapPtr->end())
            continue;

         auto& hash = zcIter->second.getTxHash();
         hashesToDelete.push_back(hash);
         outPointsSpentByKey_.erase(hash);

         //drop from keyToSpendScrAddr_
         auto saSetIter = keytospendsaPtr->find(zcKey);
         if (saSetIter != keytospendsaPtr->end())
         {
            for (auto& sa : saSetIter->second)
               dropTxios(zcKey, sa);
         }

         //drop from keyToFundedScrAddr_
         auto fundedIter = keyToFundedScrAddr_.find(zcKey);
         if (fundedIter != keyToFundedScrAddr_.end())
         {
            for (auto& sa : fundedIter->second)
               dropTxios(zcKey, sa);

            keyToFundedScrAddr_.erase(fundedIter);
         }

         //drop from txOutsSpentByZC_
         for (auto& input : zcIter->second.inputs_)
         {
            if (!input.isResolved())
               continue;
            txoutsToDelete.push_back(input.opRef_.getDbKey());
         }

         //mark for deletion
         keysToDelete.push_back(zcKey);
      }
   }

   //drop from containers
   txHashToDBKey_.erase(hashesToDelete);
   txMap_.erase(keysToDelete);
   keyToSpentScrAddr_.erase(keysToDelete);
   txOutsSpentByZC_.erase(txoutsToDelete);

   //gathers keys to delete
   vector<BinaryData> delKeys;
   auto updateMapIter = updateMap.begin();
   while (updateMapIter != updateMap.end())
   {
      if (updateMapIter->second->size() > 0)
      {
         ++updateMapIter;
         continue;
      }

      delKeys.push_back(updateMapIter->first);
      updateMap.erase(updateMapIter++);
   }

   txioMap_.erase(delKeys);
   txioMap_.update(updateMap);

   //delete keys from DB
   auto deleteKeys = [&](void)->void
   {
      this->updateZCinDB(vector<BinaryData>(), keysToDelete);
   };

   thread deleteKeyThread(deleteKeys);
   if (deleteKeyThread.joinable())
      deleteKeyThread.join();
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::parseNewZC(void)
{
   while (1)
   {
      ZcActionStruct zcAction;
      map<BinaryData, ParsedTx> zcMap;
      try
      {
         zcAction = move(newZcStack_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      bool notify = true;
      vector<BinaryData> previouslyValidKeys;
      map<BinaryData, BinaryData> minedKeys;

      switch (zcAction.action_)
      {
      case Zc_Purge:
      {
         //setup batch with all tracked zc
         if (zcAction.batch_ == nullptr)
            zcAction.batch_ = make_shared<ZeroConfBatch>();

         zcAction.batch_->txMap_ = *txMap_.get();
         zcAction.batch_->isReadyPromise_.set_value(true);

         //build set of currently valid keys
         auto txmap = txMap_.get();
         for (auto& txpair : *txmap)
            previouslyValidKeys.push_back(txpair.first);

         //purge mined zc
         minedKeys = move(purge(zcAction.reorgState_, zcAction.batch_->txMap_));
         notify = false;
      }

      case Zc_NewTx:
      {
         if (zcAction.batch_ == nullptr)
            continue;

         auto fut = zcAction.batch_->isReadyPromise_.get_future();
         fut.wait();
         zcMap = move(zcAction.batch_->txMap_);
         break;
      }

      case Zc_Shutdown:
         reset();
         return;

      default:
         continue;
      }

      parseNewZC(move(zcMap), true, notify);
      if (zcAction.resultPromise_ != nullptr)
      {
         auto purgePacket = make_shared<ZcPurgePacket>();
         purgePacket->minedTxioKeys_ = move(minedKeys);

         auto txmap = txMap_.get();
         for (auto wasValid : previouslyValidKeys)
         {
            auto keyIter = txmap->find(wasValid);
            if (keyIter != txmap->end())
               continue;

            purgePacket->invalidatedZcKeys_.insert(wasValid);
         }

         zcAction.resultPromise_->set_value(purgePacket);
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::parseNewZC(map<BinaryData, ParsedTx> zcMap,
   bool updateDB, bool notify)
{
   unique_lock<mutex> lock(parserMutex_);
   set<BinaryData> newZcByHash;
   vector<BinaryData> keysToWrite;

   auto iter = zcMap.begin();
   while (iter != zcMap.end())
   {
      if (iter->second.status() == Tx_Mined ||
         iter->second.status() == Tx_Invalid)
         zcMap.erase(iter++);
      else
         ++iter;
   }

   for (auto& newZCPair : zcMap)
   {
      auto txmap_ptr = txMap_.get();
      if (BlockDataManagerConfig::getDbType() != ARMORY_DB_SUPER)
      {
         auto& txHash = newZCPair.second.getTxHash();
         auto insertIter = allZcTxHashes_.insert(txHash);
         if (!insertIter.second)
            continue;
      }
      else
      {
         if (txmap_ptr->find(newZCPair.first) != txmap_ptr->end())
            continue;
      }

      keysToWrite.push_back(newZCPair.first);
   }

   map<BinaryData, BinaryData> txhashmap_update;
   map<BinaryData, ParsedTx> txmap_update;
   set<BinaryData> invalidatedKeys;

   bool hasChanges = false;

   map<string, pair<bool, ParsedZCData>> flaggedBDVs;

   {
      auto txhashmap_ptr = txHashToDBKey_.get();
      auto txmap_ptr = txMap_.get();

      //zckey fetch lambda
      auto getzckeyfortxhash = [txhashmap_ptr, &txhashmap_update]
      (const BinaryData& txhash, BinaryData& zckey_output)->bool
      {
         auto local_iter = txhashmap_update.find(txhash);
         if (local_iter != txhashmap_update.end())
         {
            zckey_output = local_iter->second;
            return true;
         }

         auto global_iter = txhashmap_ptr->find(txhash);
         if (global_iter == txhashmap_ptr->end())
            return false;

         zckey_output = global_iter->second;
         return true;
      };

      //zc tx fetch lambda
      auto getzctxforkey = [txmap_ptr, &txmap_update]
      (const BinaryData& zc_key)->const ParsedTx&
      {
         auto local_iter = txmap_update.find(zc_key);
      if (local_iter != txmap_update.end())
         return local_iter->second;

      auto global_iter = txmap_ptr->find(zc_key);
      if (global_iter == txmap_ptr->end())
         throw runtime_error("no zc tx for this key");

      return global_iter->second;
      };

      function<set<BinaryData>(const BinaryData&)> getTxChildren =
         [&](const BinaryData& zcKey)->
         set<BinaryData>
      {
         set<BinaryData> childKeys;
         BinaryDataRef txhash;

         try
         {
            auto& parsedTx = getzctxforkey(zcKey);
            txhash = parsedTx.getTxHash().getRef();
         }
         catch (exception&)
         {
            return childKeys;
         }

         auto spentOP_iter = outPointsSpentByKey_.find(txhash);
         if (spentOP_iter != outPointsSpentByKey_.end())
         {
            auto& keymap = spentOP_iter->second;

            for (auto& keypair : keymap)
            {
               auto&& childrenKeys = getTxChildren(keypair.second);
               childKeys.insert(move(keypair.second));
               for (auto& c_key : childrenKeys)
                  childKeys.insert(move(c_key));
            }
         }

         return childKeys;
      };

      for (auto& newZCPair : zcMap)
      {
         auto& txHash = newZCPair.second.getTxHash();
         if (txhashmap_ptr->find(txHash) != txhashmap_ptr->end())
            continue; //already have this ZC

         {
            auto&& bulkData = ZCisMineBulkFilter(
               newZCPair.second, newZCPair.first,
               getzckeyfortxhash, getzctxforkey);

            //check for replacement
            {
               //loop through all outpoints consumed by this ZC
               for (auto& idSet : bulkData.outPointsSpentByKey_)
               {
                  //compare them to the list of currently spent outpoints
                  auto hashIter = outPointsSpentByKey_.find(idSet.first);
                  if (hashIter == outPointsSpentByKey_.end())
                     continue;

                  for (auto opId : idSet.second)
                  {
                     auto idIter = hashIter->second.find(opId.first);
                     if (idIter != hashIter->second.end())
                     {
                        try
                        {
                           //gather replaced tx children
                           auto&& keySet = getTxChildren(idIter->second);
                           invalidatedKeys.insert(keySet.begin(), keySet.end());
                           invalidatedKeys.insert(idIter->second);
                           hasChanges = true;
                        }
                        catch (exception&)
                        {
                           continue;
                        }
                     }
                  }
               }
            }

            //add ZC if its relevant
            if (newZCPair.second.status() != Tx_Invalid &&
               newZCPair.second.status() != Tx_Uninitialized &&
               !bulkData.isEmpty())
            {
               hasChanges = true;

               //merge spent outpoints
               txOutsSpentByZC_.insert(bulkData.txOutsSpentByZC_);

               for (auto& idmap : bulkData.outPointsSpentByKey_)
               {
                  //cant use insert, have to replace values if they already exist
                  auto& thisIdMap = outPointsSpentByKey_[idmap.first];
                  for (auto& idpair : idmap.second)
                     thisIdMap[idpair.first] = idpair.second;
               }

               //merge scrAddr spent by key
               keyToSpentScrAddr_.update(move(bulkData.keyToSpentScrAddr_));

               //merge scrAddr funded by key
               typedef map<BinaryData, set<BinaryData>>::iterator mapbd_setbd_iter;
               keyToFundedScrAddr_.insert(
                  move_iterator<mapbd_setbd_iter>(bulkData.keyToFundedScrAddr_.begin()),
                  move_iterator<mapbd_setbd_iter>(bulkData.keyToFundedScrAddr_.end()));

               //merge new txios
               txhashmap_update[txHash] = newZCPair.first;
               txmap_update[newZCPair.first] = newZCPair.second;

               map<HashString, shared_ptr<map<BinaryData, TxIOPair>>> newtxiomap;
               auto txiomapPtr = txioMap_.get();

               for (const auto& saTxio : bulkData.scrAddrTxioMap_)
               {
                  auto saIter = txiomapPtr->find(saTxio.first);
                  if (saIter != txiomapPtr->end())
                     saTxio.second->insert(saIter->second->begin(), saIter->second->end());

                  newtxiomap.insert(move(make_pair(saTxio.first, saTxio.second)));
               }

               txioMap_.update(move(newtxiomap));
               newZcByHash.insert(txHash);

               //notify BDVs
               for (auto& bdvMap : bulkData.flaggedBDVs_)
               {
                  auto& parserResult = flaggedBDVs[bdvMap.first];
                  parserResult.second.mergeTxios(bdvMap.second);
                  parserResult.first = true;
               }
            }
         }
      }
   }

   set<BinaryData> newZcKeys;
   for (auto tx_pair : txmap_update)
      newZcKeys.insert(tx_pair.first);

   txHashToDBKey_.update(txhashmap_update);
   txMap_.update(txmap_update);

   if (updateDB && keysToWrite.size() > 0)
   {
      //write ZC in the new thread to guaranty we can get a RW tx
      auto writeNewZC = [&, this](void)->void
      { this->updateZCinDB(keysToWrite, vector<BinaryData>()); };

      thread writeNewZCthread(writeNewZC);
      writeNewZCthread.join();
   }

   //notify bdvs
   if (!hasChanges)
      return;

   //find BDVs affected by invalidated keys
   if (invalidatedKeys.size() > 0)
   {
      //TODO: multi thread this at some point

      auto txmap = txMap_.get();

      for (auto& invalidKey : invalidatedKeys)
      {
         //grab tx
         auto zcIter = txmap->find(invalidKey);
         if (zcIter == txmap->end())
            continue;

         //gather all scrAddr from invalidated tx
         set<BinaryDataRef> addrRefs;

         for (auto& input : zcIter->second.inputs_)
         {
            if (!input.isResolved())
               continue;

            addrRefs.insert(input.scrAddr_.getRef());
         }

         for (auto& output : zcIter->second.outputs_)
         {
            addrRefs.insert(output.scrAddr_.getRef());
         }

         //flag relevant BDVs
         for (auto& addrRef : addrRefs)
         {
            auto&& bdvid_set = bdvCallbacks_->hasScrAddr(addrRef);
            for (auto& bdvid : bdvid_set)
            {
               auto& bdv = flaggedBDVs[bdvid];
               bdv.second.invalidatedKeys_.insert(invalidKey);
               bdv.first = true;
            }
         }
      }
   }

   //drop the replaced zc if any
   if (invalidatedKeys.size() > 0)
      dropZC(invalidatedKeys);

   if (!notify)
      return;

   auto txiomapPtr = txioMap_.get();

   for (auto& bdvMap : flaggedBDVs)
   {
      if (!bdvMap.second.first)
         continue;

      NotificationPacket notificationPacket(bdvMap.first);
      for (auto& sa : bdvMap.second.second.txioKeys_)
      {
         auto saIter = txiomapPtr->find(sa);
         if (saIter == txiomapPtr->end())
            continue;

         notificationPacket.txioMap_.insert(*saIter);
      }

      if (bdvMap.second.second.invalidatedKeys_.size() != 0)
      {
         notificationPacket.purgePacket_ = make_shared<ZcPurgePacket>();
         notificationPacket.purgePacket_->invalidatedZcKeys_ =
            move(bdvMap.second.second.invalidatedKeys_);
      }

      notificationPacket.newZcKeys_ = newZcKeys;
      bdvCallbacks_->pushZcNotification(notificationPacket);
   }
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::preprocessTx(ParsedTx& tx) const
{
   auto& txHash = tx.getTxHash();
   auto&& txref = db_->getTxRef(txHash);

   if (txref.isInitialized())
   {
      tx.state_ = Tx_Mined;
      return;
   }

   uint8_t const * txStartPtr = tx.tx_.getPtr();
   unsigned len = tx.tx_.getSize();

   auto nTxIn = tx.tx_.getNumTxIn();
   auto nTxOut = tx.tx_.getNumTxOut();

   //try to resolve as many outpoints as we can. unresolved outpoints are 
   //either invalid or (most likely) children of unconfirmed transactions
   if (nTxIn != tx.inputs_.size())
   {
      tx.inputs_.clear();
      tx.inputs_.resize(nTxIn);
   }

   if (nTxOut != tx.outputs_.size())
   {
      tx.outputs_.clear();
      tx.outputs_.resize(nTxOut);
   }

   for (uint32_t iin = 0; iin < nTxIn; iin++)
   {

      auto& txIn = tx.inputs_[iin];
      if (txIn.isResolved())
         continue;

      auto& opRef = txIn.opRef_;

      if (!opRef.isInitialized())
      {
         auto offset = tx.tx_.getTxInOffset(iin);
         if (offset > len)
            throw runtime_error("invalid txin offset");
         BinaryDataRef inputDataRef(txStartPtr + offset, len - offset);
         opRef.unserialize(inputDataRef);
      }

      if (!opRef.isResolved())
      {
         //resolve outpoint to dbkey
         txIn.opRef_.resolveDbKey(db_);
         if (!opRef.isResolved())
            continue;
      }

      //grab txout
      StoredTxOut stxOut;
      if (!db_->getStoredTxOut(stxOut, opRef.getDbKey()))
         continue;

      if (db_->armoryDbType() == ARMORY_DB_SUPER)
         opRef.getDbKey() = stxOut.getDBKey(false);

      if (stxOut.isSpent())
      {
         tx.state_ = Tx_Invalid;
         return;
      }

      //set txin address and value
      txIn.scrAddr_ = stxOut.getScrAddress();
      txIn.value_ = stxOut.getValue();
   }

   for (uint32_t iout = 0; iout < nTxOut; iout++)
   {
      auto& txOut = tx.outputs_[iout];
      if (txOut.isInitialized())
         continue;

      auto offset = tx.tx_.getTxOutOffset(iout);
      auto len = tx.tx_.getTxOutOffset(iout + 1) - offset;

      BinaryRefReader brr(txStartPtr + offset, len);
      txOut.value_ = brr.get_uint64_t();

      auto scriptLen = brr.get_var_int();
      auto scriptRef = brr.get_BinaryDataRef(scriptLen);
      txOut.scrAddr_ = move(BtcUtils::getTxOutScrAddr(scriptRef));
   }

   tx.isRBF_ = tx.tx_.isRBF();


   bool txInResolved = true;
   for (auto& txin : tx.inputs_)
   {
      if (txin.isResolved())
         continue;

      txInResolved = false;
      break;
   }

   if (!txInResolved)
      tx.state_ = Tx_Unresolved;
   else
      tx.state_ = Tx_Resolved;
}

///////////////////////////////////////////////////////////////////////////////
ZeroConfContainer::BulkFilterData ZeroConfContainer::ZCisMineBulkFilter(
   ParsedTx & parsedTx, const BinaryData & ZCkey,
   function<bool(const BinaryData&, BinaryData&)> getzckeyfortxhash,
   function<const ParsedTx&(const BinaryData&)> getzctxforkey)
{
   BulkFilterData bulkData;
   if (parsedTx.status() == Tx_Mined || parsedTx.status() == Tx_Invalid)
      return bulkData;

   auto mainAddressSet = scrAddrMap_->get();

   auto filter = [mainAddressSet, this]
      (const BinaryData& addr)->pair<bool, set<string>>
   {
      pair<bool, set<string>> flaggedBDVs;
      flaggedBDVs.first = false;

      auto addrIter = mainAddressSet->find(addr.getRef());
      if (addrIter == mainAddressSet->end())
      {
         if (BlockDataManagerConfig::getDbType() == ARMORY_DB_SUPER)
            flaggedBDVs.first = true;

         return flaggedBDVs;
      }

      flaggedBDVs.first = true;
      flaggedBDVs.second = move(bdvCallbacks_->hasScrAddr(addr.getRef()));
      return flaggedBDVs;
   };

   auto insertNewZc = [&bulkData](BinaryData sa,
      BinaryData txiokey, TxIOPair txio,
      set<string> flaggedBDVs, bool consumesTxOut)->void
   {
      if (consumesTxOut)
         bulkData.txOutsSpentByZC_.insert(txiokey);

      auto& key_txioPair = bulkData.scrAddrTxioMap_[sa];

      if (key_txioPair == nullptr)
         key_txioPair = make_shared<map<BinaryData, TxIOPair>>();

      (*key_txioPair)[txiokey] = move(txio);

      for (auto& bdvId : flaggedBDVs)
         bulkData.flaggedBDVs_[bdvId].txioKeys_.insert(sa);
   };

   if (parsedTx.status() == Tx_Uninitialized ||
      parsedTx.status() == Tx_ResolveAgain)
      preprocessTx(parsedTx);

   auto& txHash = parsedTx.getTxHash();
   bool isRBF = parsedTx.isRBF_;
   bool isChained = parsedTx.isChainedZc_;

   //if parsedTx has unresolved outpoint, they are most likely ZC
   for (auto& input : parsedTx.inputs_)
   {
      if (input.isResolved())
      {
         //check resolved key is valid
         if (input.opRef_.isZc())
         {
            try
            {
               isChained = true;
               auto& chainedZC = getzctxforkey(input.opRef_.getDbTxKeyRef());
               if (chainedZC.status() == Tx_Invalid)
                  throw runtime_error("invalid parent zc");
            }
            catch (exception&)
            {
               parsedTx.state_ = Tx_Invalid;
               return bulkData;
            }
         }
         else
         {
            auto&& keyRef = input.opRef_.getDbKey().getSliceRef(0, 4);
            auto height = DBUtils::hgtxToHeight(keyRef);
            auto dupId = DBUtils::hgtxToDupID(keyRef);

            if (db_->getValidDupIDForHeight(height) != dupId)
            {
               parsedTx.state_ = Tx_Invalid;
               return bulkData;
            }
         }

         continue;
      }

      auto& opZcKey = input.opRef_.getDbKey();
      if (!getzckeyfortxhash(input.opRef_.getTxHashRef(), opZcKey))
      {
         if (BlockDataManagerConfig::getDbType() == ARMORY_DB_SUPER ||
            allZcTxHashes_.find(input.opRef_.getTxHashRef()) == allZcTxHashes_.end())
            continue;
      }

      isChained = true;

      try
      {
         auto& chainedZC = getzctxforkey(opZcKey);
         auto&& chainedTxOut = chainedZC.tx_.getTxOutCopy(input.opRef_.getIndex());

         input.value_ = chainedTxOut.getValue();
         input.scrAddr_ = chainedTxOut.getScrAddressStr();
         isRBF |= chainedZC.tx_.isRBF();
         input.opRef_.setTime(chainedZC.tx_.getTxTime());

         opZcKey.append(WRITE_UINT16_BE(input.opRef_.getIndex()));
      }
      catch (runtime_error&)
      {
         continue;
      }
   }

   parsedTx.isRBF_ = isRBF;
   parsedTx.isChainedZc_ = isChained;

   //spent txios
   unsigned iin = 0;
   for (auto& input : parsedTx.inputs_)
   {
      auto inputId = iin++;
      if (!input.isResolved())
      {
         if (db_->armoryDbType() == ARMORY_DB_SUPER)
         {
            parsedTx.state_ = Tx_Invalid;
            return bulkData;
         }
         else
         {
            parsedTx.state_ = Tx_ResolveAgain;
         }

         continue;
      }

      //keep track of all outputs this ZC consumes
      auto& id_map = bulkData.outPointsSpentByKey_[input.opRef_.getTxHashRef()];
      id_map.insert(make_pair(input.opRef_.getIndex(), ZCkey));

      auto&& flaggedBDVs = filter(input.scrAddr_);
      if (!isChained && !flaggedBDVs.first)
         continue;

      TxIOPair txio(
         TxRef(input.opRef_.getDbTxKeyRef()), input.opRef_.getIndex(),
         TxRef(ZCkey), inputId);

      txio.setTxHashOfOutput(input.opRef_.getTxHashRef());
      txio.setTxHashOfInput(txHash);
      txio.setValue(input.value_);
      auto tx_time = input.opRef_.getTime();
      if (tx_time == UINT64_MAX)
         tx_time = parsedTx.tx_.getTxTime();
      txio.setTxTime(tx_time);
      txio.setRBF(isRBF);
      txio.setChained(isChained);

      auto&& txioKey = txio.getDBKeyOfOutput();
      insertNewZc(input.scrAddr_, move(txioKey), move(txio),
         move(flaggedBDVs.second), true);

      auto& updateSet = bulkData.keyToSpentScrAddr_[ZCkey];
      updateSet.insert(input.scrAddr_);
   }

   //funded txios
   unsigned iout = 0;
   for (auto& output : parsedTx.outputs_)
   {
      auto outputId = iout++;

      auto&& flaggedBDVs = filter(output.scrAddr_);
      if (flaggedBDVs.first)
      {
         TxIOPair txio(TxRef(ZCkey), outputId);

         txio.setValue(output.value_);
         txio.setTxHashOfOutput(txHash);
         txio.setTxTime(parsedTx.tx_.getTxTime());
         txio.setUTXO(true);
         txio.setRBF(isRBF);
         txio.setChained(isChained);

         auto& fundedScrAddr = bulkData.keyToFundedScrAddr_[ZCkey];
         fundedScrAddr.insert(output.scrAddr_);

         auto&& txioKey = txio.getDBKeyOfOutput();
         insertNewZc(output.scrAddr_, move(txioKey),
            move(txio), move(flaggedBDVs.second), false);
      }
   }

   return bulkData;
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::clear()
{
   txHashToDBKey_.clear();
   txMap_.clear();
   txioMap_.clear();
}

///////////////////////////////////////////////////////////////////////////////
bool ZeroConfContainer::isTxOutSpentByZC(const BinaryData& dbkey)
const
{
   auto txoutset = txOutsSpentByZC_.get();
   if (txoutset->find(dbkey) != txoutset->end())
      return true;

   return false;
}

///////////////////////////////////////////////////////////////////////////////
map<BinaryData, TxIOPair> ZeroConfContainer::getUnspentZCforScrAddr(
   BinaryData scrAddr) const
{
   auto txiomapptr = txioMap_.get();
   auto saIter = txiomapptr->find(scrAddr);

   if (saIter != txiomapptr->end())
   {
      auto& zcMap = saIter->second;
      map<BinaryData, TxIOPair> returnMap;

      for (auto& zcPair : *zcMap)
      {
         if (zcPair.second.hasTxIn())
            continue;

         returnMap.insert(zcPair);
      }

      return returnMap;
   }

   return emptyTxioMap_;
}

///////////////////////////////////////////////////////////////////////////////
map<BinaryData, TxIOPair> ZeroConfContainer::getRBFTxIOsforScrAddr(
   BinaryData scrAddr) const
{
   auto txiomapptr = txioMap_.get();
   auto saIter = txiomapptr->find(scrAddr);

   if (saIter != txiomapptr->end())
   {
      auto& zcMap = saIter->second;
      map<BinaryData, TxIOPair> returnMap;

      for (auto& zcPair : *zcMap)
      {
         if (!zcPair.second.hasTxIn())
            continue;

         if (!zcPair.second.isRBF())
            continue;

         returnMap.insert(zcPair);
      }

      return returnMap;
   }

   return emptyTxioMap_;
}

///////////////////////////////////////////////////////////////////////////////
vector<TxOut> ZeroConfContainer::getZcTxOutsForKey(
   const set<BinaryData>& keys) const
{
   vector<TxOut> result;
   auto txmap = txMap_.get();

   for (auto& key : keys)
   {
      auto zcKey = key.getSliceRef(0, 6);

      auto txIter = txmap->find(zcKey);
      if (txIter == txmap->end())
         continue;

      auto& theTx = txIter->second;

      auto outIdRef = key.getSliceRef(6, 2);
      auto outId = READ_UINT16_BE(outIdRef);

      auto&& txout = theTx.tx_.getTxOutCopy(outId);
      txout.setParentTxRef(zcKey);

      result.push_back(move(txout));
   }

   return result;
}

///////////////////////////////////////////////////////////////////////////////
const set<BinaryData>& ZeroConfContainer::getSpentSAforZCKey(
   const BinaryData& zcKey) const
{
   auto keytospendsaPtr = keyToSpentScrAddr_.get();
   auto iter = keytospendsaPtr->find(zcKey);
   if (iter == keytospendsaPtr->end())
      return emptySetBinData_;

   return iter->second;
}

///////////////////////////////////////////////////////////////////////////////
const shared_ptr<map<BinaryData, set<BinaryData>>>
ZeroConfContainer::getKeyToSpentScrAddrMap() const
{
   return keyToSpentScrAddr_.get();
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::updateZCinDB(const vector<BinaryData>& keysToWrite,
   const vector<BinaryData>& keysToDelete)
{
   auto txmap = txMap_.get();
   auto&& tx = db_->beginTransaction(ZERO_CONF, LMDB::ReadWrite);

   for (auto& key : keysToWrite)
   {
      auto iter = txmap->find(key);
      if (iter != txmap->end())
      {
         StoredTx zcTx;
         zcTx.createFromTx(iter->second.tx_, true, true);
         db_->putStoredZC(zcTx, key);
      }
      else
      {
         //if the key is not to be found in the txMap_, this is a ZC txhash
         db_->putValue(ZERO_CONF, key, BinaryData());
      }
   }

   for (auto& key : keysToDelete)
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
         keyWithPrefix = key;

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

      for (auto Key : ktd)
         db_->deleteValue(ZERO_CONF, Key);
   }
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::loadZeroConfMempool(bool clearMempool)
{
   map<BinaryData, ParsedTx> zcMap;

   {
      auto dbs = ZERO_CONF;

      auto&& tx = db_->beginTransaction(dbs, LMDB::ReadOnly);
      auto dbIter = db_->getIterator(dbs);

      if (!dbIter->seekToStartsWith(DB_PREFIX_ZCDATA))
      {
         enabled_ = true;
         return;
      }

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

            ParsedTx parsedTx;
            parsedTx.tx_ = move(zctx);

            zcMap.insert(move(make_pair(
               move(zckey), move(parsedTx))));
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
      vector<BinaryData> keysToWrite, keysToDelete;

      for (const auto& zcTx : zcMap)
         keysToDelete.push_back(zcTx.first);

      updateZCinDB(keysToWrite, keysToDelete);
   }
   else if (zcMap.size())
   {
      preprocessZcMap(zcMap);

      //set highest used index
      auto lastEntry = zcMap.rbegin();
      auto& topZcKey = lastEntry->first;
      topId_.store(READ_UINT32_BE(topZcKey.getSliceCopy(2, 4)) + 1);

      //no need to update the db nor notify bdvs on init
      parseNewZC(move(zcMap), false, false);
   }

   enabled_ = true;
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::init(shared_ptr<ScrAddrFilter> saf, bool clearMempool)
{
   LOGINFO << "Enabling zero-conf tracking";

   scrAddrMap_ = saf->getScrAddrMapPtr();
   loadZeroConfMempool(clearMempool);

   auto processZcThread = [this](void)->void
   {
      parseNewZC();
   };

   parserThreads_.push_back(thread(processZcThread));
   increaseParserThreadPool(1);

   zcEnabled_.store(true, memory_order_relaxed);
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::processInvTxVec(
   vector<InvEntry> invVec, bool extend, unsigned timeout)
{
   if (!isEnabled())
      return;

   //skip this entirely if there are no addresses to scan the ZCs against
   if (scrAddrMap_->size() == 0 && extend &&
      BlockDataManagerConfig::getDbType() != ARMORY_DB_SUPER)
      return;

   if (extend &&
      parserThreadCount_ < invVec.size() &&
      parserThreadCount_ < maxZcThreadCount_)
      increaseParserThreadPool(invVec.size());

   //setup batch
   auto batch = make_shared<ZeroConfBatch>();
   batch->timeout_ = timeout;
   for (unsigned i = 0; i < invVec.size(); i++)
   {
      pair<BinaryData, ParsedTx> txPair;
      txPair.first = move(getNewZCkey());
      batch->txMap_.insert(move(txPair));
   }

   //pass individual packets to parser threads
   auto mapIter = batch->txMap_.begin();
   for (auto& entry : invVec)
   {
      ZeroConfInvPacket packet;
      packet.batchPtr_ = batch;
      packet.zcKey_ = mapIter->first;
      packet.invEntry_ = move(entry);

      newInvTxStack_.push_back(move(packet));
      ++mapIter;
   }

   //register batch with main zc processing thread
   ZcActionStruct zac;
   zac.batch_ = batch;
   zac.action_ = Zc_NewTx;
   newZcStack_.push_back(move(zac));
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::processInvTxThread(void)
{
   while (1)
   {
      ZeroConfInvPacket packet;
      try
      {
         packet = move(newInvTxStack_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      try
      {
         processInvTxThread(packet);
      }
      catch (BitcoinP2P_Exception&)
      {
         //ignore any p2p connection related exceptions
         packet.batchPtr_->incrementCounter();
         continue;
      }
      catch (runtime_error &e)
      {
         LOGERR << "zc parser error: " << e.what();
         packet.batchPtr_->incrementCounter();
      }
      catch (exception&)
      {
         LOGERR << "unhandled exception in ZcParser thread!";
         packet.batchPtr_->incrementCounter();
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::processInvTxThread(ZeroConfInvPacket& packet)
{
   packet.invEntry_.invtype_ = Inv_Msg_Witness_Tx;
   auto payload =
      networkNode_->getTx(packet.invEntry_, packet.batchPtr_->timeout_);

   auto txIter = packet.batchPtr_->txMap_.find(packet.zcKey_);
   if (txIter == packet.batchPtr_->txMap_.end())
      throw runtime_error("batch does not have zckey");

   auto& txObj = txIter->second;

   auto payloadtx = dynamic_pointer_cast<Payload_Tx>(payload);
   if (payloadtx == nullptr)
   {
      txObj.state_ = Tx_Invalid;
      packet.batchPtr_->incrementCounter();
      return;
   }

   //push raw tx with current time
   auto& rawTx = payloadtx->getRawTx();
   txObj.tx_.unserialize(&rawTx[0], rawTx.size());
   txObj.tx_.setTxTime(time(0));

   preprocessTx(txObj);
   packet.batchPtr_->incrementCounter();
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::pushZcToParser(const BinaryDataRef& rawTx)
{
   pair<BinaryData, ParsedTx> zcpair;
   zcpair.first = getNewZCkey();
   zcpair.second.tx_.unserialize(rawTx.getPtr(), rawTx.getSize());
   zcpair.second.tx_.setTxTime(time(0));

   ZcActionStruct actionstruct;
   actionstruct.batch_ = make_shared<ZeroConfBatch>();
   actionstruct.batch_->txMap_.insert(move(zcpair));
   actionstruct.action_ = Zc_NewTx;
   newZcStack_.push_back(move(actionstruct));
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::broadcastZC(const BinaryData& rawzc,
   const string& bdvId, uint32_t timeout_ms)
{
   Tx zcTx(rawzc);

   //get tx hash
   auto&& txHash = zcTx.getThisHash();
   auto&& txHashStr = txHash.toHexStr();

   if (!networkNode_->connected())
   {
      string errorMsg("node is offline, cannot broadcast");
      LOGWARN << errorMsg;
      bdvCallbacks_->errorCallback(bdvId, errorMsg, txHashStr);
      return;
   }

   //create inv payload
   InvEntry entry;
   entry.invtype_ = Inv_Msg_Tx;
   memcpy(entry.hash, txHash.getPtr(), 32);

   vector<InvEntry> invVec;
   invVec.push_back(entry);

   Payload_Inv payload_inv;
   payload_inv.setInvVector(invVec);

   //create getData payload packet
   auto&& payload = make_unique<Payload_Tx>();
   vector<uint8_t> rawtx;
   rawtx.resize(rawzc.getSize());
   memcpy(&rawtx[0], rawzc.getPtr(), rawzc.getSize());

   payload->setRawTx(move(rawtx));
   auto getDataProm = make_shared<promise<bool>>();
   auto getDataFut = getDataProm->get_future();

   BitcoinP2P::getDataPayload getDataPayload;
   getDataPayload.payload_ = move(payload);
   getDataPayload.promise_ = getDataProm;

   pair<BinaryData, BitcoinP2P::getDataPayload> getDataPair;
   getDataPair.first = txHash;
   getDataPair.second = move(getDataPayload);

   //Register tx hash for watching before broadcasting the inv. This guarantees we will
   //catch any reject packet before trying to fetch the tx back for confirmation.
   auto gds = make_shared<GetDataStatus>();
   networkNode_->registerGetTxCallback(txHash, gds);

   //register getData payload
   networkNode_->getDataPayloadMap_.insert(move(getDataPair));

   //send inv packet
   networkNode_->sendMessage(move(payload_inv));
   LOGINFO << "sent inv packet";

   //wait on getData future
   bool sent = false;
   if (timeout_ms == 0)
   {
      getDataFut.wait();
   }
   else
   {
      auto getDataFutStatus = getDataFut.wait_for(chrono::milliseconds(timeout_ms));
      if (getDataFutStatus != future_status::ready)
      {
         gds->setStatus(false);
         LOGERR << "tx broadcast timed out (send)";
         gds->setMessage("tx broadcast timed out (send)");
      }
      else
      {
         LOGINFO << "got getData packet";
         sent = true;
      }
   }

   networkNode_->getDataPayloadMap_.erase(txHash);

   if (!sent)
   {
      auto&& errorMsg = gds->getMessage();
      networkNode_->unregisterGetTxCallback(txHash);
      bdvCallbacks_->errorCallback(bdvId, errorMsg, txHashStr);
      return;
   }

   auto watchTxFuture = gds->getFuture();

   //try to fetch tx by hash from node
   if (PEER_USES_WITNESS)
      entry.invtype_ = Inv_Msg_Witness_Tx;

   auto grabtxlambda = [this, timeout_ms](InvEntry inventry)->void
   {
      vector<InvEntry> invVec;
      invVec.push_back(move(inventry));

      processInvTxVec(move(invVec), false, timeout_ms);
   };

   thread grabtxthread(grabtxlambda, move(entry));
   if (grabtxthread.joinable())
      grabtxthread.detach();

   LOGINFO << "grabbing tx from node";

   if (timeout_ms == 0)
   {
      watchTxFuture.wait();
   }
   else
   {
      auto status = watchTxFuture.wait_for(chrono::milliseconds(timeout_ms));
      if (status != future_status::ready)
      {
         gds->setStatus(false);
         LOGERR << "tx broadcast timed out (get)";
         gds->setMessage("tx broadcast timed out (get)");
      }
   }

   networkNode_->unregisterGetTxCallback(txHash);

   if (!gds->status())
   {
      auto&& errorMsg = gds->getMessage();
      bdvCallbacks_->errorCallback(bdvId, errorMsg, txHashStr);
   }
   else
   {
      LOGINFO << "tx broadcast successfully";
   }
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::shutdown()
{
   newInvTxStack_.terminate();
   newZcStack_.terminate();

   vector<thread::id> idVec;
   for (auto& parser : parserThreads_)
   {
      idVec.push_back(parser.get_id());
      if (parser.joinable())
         parser.join();
   }

   DatabaseContainer_Sharded::clearThreadShardTx(idVec);
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::increaseParserThreadPool(unsigned count)
{
   unique_lock<mutex> lock(parserThreadMutex_);

   //start Zc parser thread
   auto processZcThread = [this](void)->void
   {
      processInvTxThread();
   };

   for (unsigned i = parserThreadCount_; i < count; i++)
      parserThreads_.push_back(thread(processZcThread));

   parserThreadCount_ = parserThreads_.size();
   LOGINFO << "now running " << parserThreadCount_ << " zc parser threads";
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<map<BinaryData, TxIOPair>>
ZeroConfContainer::getTxioMapForScrAddr(const BinaryData& scrAddr) const
{
   auto txiomap = txioMap_.get();

   auto iter = txiomap->find(scrAddr);
   if (iter == txiomap->end())
      return nullptr;

   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// OutPointRef
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void OutPointRef::unserialize(uint8_t const * ptr, uint32_t remaining)
{
   if (remaining < 36)
      throw runtime_error("ptr is too short to be an outpoint");

   BinaryDataRef bdr(ptr, remaining);
   BinaryRefReader brr(bdr);

   txHash_ = brr.get_BinaryDataRef(32);
   txOutIndex_ = brr.get_uint32_t();
}

////////////////////////////////////////////////////////////////////////////////
void OutPointRef::unserialize(BinaryDataRef bdr)
{
   unserialize(bdr.getPtr(), bdr.getSize());
}

////////////////////////////////////////////////////////////////////////////////
void OutPointRef::resolveDbKey(LMDBBlockDatabase *dbPtr)
{
   if (txHash_.getSize() == 0 || txOutIndex_ == UINT16_MAX)
      throw runtime_error("empty outpoint hash");

   auto&& key = dbPtr->getDBKeyForHash(txHash_);
   if (key.getSize() != 6)
      return;

   BinaryWriter bw;
   bw.put_BinaryData(key);
   bw.put_uint16_t(txOutIndex_, BE);

   dbKey_ = bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef OutPointRef::getDbTxKeyRef() const
{
   if (!isResolved())
      throw runtime_error("unresolved outpoint key");

   return dbKey_.getSliceRef(0, 6);
}

////////////////////////////////////////////////////////////////////////////////
bool OutPointRef::isInitialized() const
{
   return txHash_.getSize() == 32 && txOutIndex_ != UINT16_MAX;
}

////////////////////////////////////////////////////////////////////////////////
bool OutPointRef::isZc() const
{
   if (!isResolved())
      return false;

   auto ptr = dbKey_.getPtr();
   auto val = (uint16_t*)ptr;
   return *val == 0xFFFF;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// ParsedTxIn
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool ParsedTxIn::isResolved() const
{
   if (!opRef_.isResolved())
      return false;

   if (scrAddr_.getSize() == 0 || value_ == UINT64_MAX)
      return false;

   return true;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// ParsedTx
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool ParsedTx::isResolved() const
{
   if (state_ == Tx_Uninitialized)
      return false;

   if (!tx_.isInitialized())
      return false;

   if (inputs_.size() != tx_.getNumTxIn() ||
      outputs_.size() != tx_.getNumTxOut())
      return false;

   for (auto& input : inputs_)
   {
      if (!input.isResolved())
         return false;
   }

   return true;
}

////////////////////////////////////////////////////////////////////////////////
void ParsedTx::reset()
{
   for (auto& input : inputs_)
      input.opRef_.reset();
   tx_.setChainedZC(false);

   state_ = Tx_Uninitialized;
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& ParsedTx::getTxHash(void) const
{
   if (txHash_.getSize() == 0)
      txHash_ = move(tx_.getThisHash());
   return txHash_;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// ZeroConfCallbacks
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
ZeroConfCallbacks::~ZeroConfCallbacks() 
{}
