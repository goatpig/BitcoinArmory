////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-17, goatpig.                                           //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "SshParser.h"

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
subSshParserResult parseSubSsh(
   unique_ptr<LDBIter> sshIter, int32_t scanFrom, bool resolveHashes,
   function<uint8_t(unsigned)> getDupIDForHeight,
   shared_ptr<map<ScrAddrFilter::AddrAndHash, int>> scrAddrMapPtr,
   BinaryData upperBound)
{
   map<BinaryData, StoredScriptHistory> sshMap;
   set<BinaryData> txnsToResolve;

   StoredScriptHistory* sshPtr = nullptr;

   do
   {
      while (sshIter->isValid())
      {
         if (sshPtr != nullptr &&
            sshIter->getKeyRef().contains(sshPtr->uniqueKey_))
            break;

         //new address
         auto&& subsshkey = sshIter->getKey();
         if (subsshkey.getSize() < 5)
         {
            LOGWARN << "invalid scrAddr in SUBSSH db";
            sshIter->advanceAndRead();
            continue;
         }

         auto&& sshKey = subsshkey.getSliceCopy(1, subsshkey.getSize() - 5);

         if (scrAddrMapPtr != nullptr &&
            scrAddrMapPtr->find(sshKey) == scrAddrMapPtr->end())
         {
            sshPtr = nullptr;
            auto&& newKey =
               sshIter->getKey().getSliceCopy(0, subsshkey.getSize() - 4);
            auto&& newHgtx = DBUtils::heightAndDupToHgtx(
               UINT32_MAX, 0xFF);
            newKey.append(newHgtx);

            sshIter->seekTo(newKey);
            continue;
         }

         //get what's already in the db
         sshPtr = &sshMap[sshKey];
         sshPtr->uniqueKey_ = sshKey;

         //set iterator at unscanned height
         auto hgtx = sshIter->getKeyRef().getSliceRef(-4, 4);
         int height = DBUtils::hgtxToHeight(hgtx);
         if (scanFrom > height)
         {
            //this ssh has already been scanned beyond the height sshIter is at,
            //let's set the iterator to the correct height (or the next key)
            auto&& newKey =
               sshIter->getKey().getSliceCopy(0, subsshkey.getSize() - 4);
            auto&& newHgtx = DBUtils::heightAndDupToHgtx(
               scanFrom, 0);

            newKey.append(newHgtx);
            sshIter->seekTo(newKey);
            continue;
         }
         else
         {
            break;
         }
      }

      //sanity checks
      if (!sshIter->isValid())
      {
         break;
      }
      else
      {
         if (upperBound.getSize() > 0 &&
            upperBound.getSize() <= sshIter->getKeyRef().getSize())
         {
            if (sshIter->getKeyRef().getSliceRef(0, upperBound.getSize()) >
               upperBound)
            break;
         }
      }

      //deser subssh
      StoredSubHistory subssh;
      subssh.unserializeDBKey(sshIter->getKeyRef());

      //check dupID
      if (getDupIDForHeight(subssh.height_) != subssh.dupID_)
         continue;

      subssh.unserializeDBValue(sshIter->getValueRef());

      set<BinaryData> txSet;
      size_t extraTxioCount = 0;
      for (auto& txioPair : subssh.txioMap_)
      {
         auto&& keyOfOutput = txioPair.second.getDBKeyOfOutput();

         if (resolveHashes)
         {
            auto&& txKey = keyOfOutput.getSliceRef(0, 6);
            txnsToResolve.insert(txKey);
         }

         if (!txioPair.second.isMultisig())
         {
            //add up balance
            if (txioPair.second.hasTxIn())
            {
               //check for same block fund&spend
               auto&& keyOfInput = txioPair.second.getDBKeyOfInput();

               if (keyOfOutput.startsWith(keyOfInput.getSliceRef(0, 4)))
               {
                  //both output and input are part of the same block, skip
                  ++extraTxioCount;
                  continue;
               }

               if (resolveHashes)
               {
                  //this is to resolve output references in transaction build from
                  //multiple wallets (i.ei coinjoin)
                  txnsToResolve.insert(keyOfInput.getSliceRef(0, 6));
               }

               sshPtr->totalUnspent_ -= txioPair.second.getValue();
            }
            else
            {
               sshPtr->totalUnspent_ += txioPair.second.getValue();
            }
         }
      }

      //txio count
      sshPtr->totalTxioCount_ += subssh.txioCount_ + extraTxioCount;

      //build subssh summary
      sshPtr->subsshSummary_[subssh.height_] = subssh.txioCount_;
   } while (sshIter->advanceAndRead());

   subSshParserResult result;
   result.first = move(txnsToResolve);
   result.second = move(sshMap);

   return result;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// ShardedSshParser
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void ShardedSshParser::updateSsh()
{
   LOGINFO << "updating SSH";

   //parse subssh shards
   auto dbPtr = db_->getDbPtr(SUBSSH);
   auto dbSharded = dynamic_pointer_cast<DatabaseContainer_Sharded>(dbPtr);
   counter_.store(dbSharded->getShardIdForHeight(scanFrom_));
   
   compileCheckpoints();

   LOGINFO << "compiled checkpoints";

   //create ssh parse bounds
   auto&& bounds = getBounds(false, 0);
   for (auto& bound : bounds)
      sshBoundsQueue_.push_back(move(bound));
   sshBoundsQueue_.completed();

   //tally ssh
   auto ssh_lambda = [this](void)->void
   {
      tallySshThread();
   };
  
   //start writer thread
   auto writer_lambda = [this](void)->void
   {
      putSSH();
   };
   thread writer_thread(writer_lambda);

   vector<thread> threads;
   for (unsigned i = 1; i < threadCount_; i++)
      threads.push_back(thread(ssh_lambda));
   tallySshThread();

   for (auto& thr : threads)
      if (thr.joinable())
         thr.join();

   //kill writer thread
   serializedSshQueue_.completed();
   if (writer_thread.joinable())
      writer_thread.join();

   LOGINFO << "Updated SSH";
}

////////////////////////////////////////////////////////////////////////////////
void ShardedSshParser::parseShardsThread(
   const vector<pair<BinaryData, BinaryData>>& boundsVec)
{
   auto getDupForHeight = [this](unsigned height)->uint8_t
   {
      return this->db_->getValidDupIDForHeight(height);
   };

   auto dbPtr = db_->getDbPtr(SUBSSH);
   auto dbSharded = dynamic_pointer_cast<DatabaseContainer_Sharded>(dbPtr);
   if (dbSharded == nullptr)
      throw runtime_error("unexpected SUBSSH dbPtr type");

   auto topShardId = dbSharded->getTopShardId();
   auto metaShardPtr = dbSharded->getShard(META_SHARD_ID);

   auto&& topBlockHash = db_->getTopBlockHash();
   unsigned topBlockInChain = 0;
   {
      auto headerPtr = db_->blockchain()->getHeaderByHash(topBlockHash);
      topBlockInChain = headerPtr->getBlockHeight();
   }


   auto total = boundsVec.size();

   while (1)
   {
      //grab a shard id
      auto countId = counter_.fetch_add(1, memory_order_relaxed);
      unsigned shardId = countId / total;
      if (shardId > topShardId)
         break;

      auto boundId = countId % total;
      auto& bound = *(boundsVec.begin() + boundId);

      //get shardptr from db object
      
      auto semaIter = shardSemaphores_.find(shardId);
      if (semaIter == shardSemaphores_.end())
         throw runtime_error("missing shard semaphore");

      map<BinaryData, pair<StoredScriptHistory, bool>> sshMap;

      {
         BinaryData shardTopHash;
         {
            BinaryWriter topHashKey;
            topHashKey.put_uint32_t(SHARD_TOPHASH_ID, BE);
            topHashKey.put_uint16_t(shardId, BE);
            auto shardtx = metaShardPtr->beginTransaction(LMDB::ReadOnly);
            shardTopHash = metaShardPtr->getValue(topHashKey.getDataRef());
         }

         unsigned lastScannedHeight = 0;
         try
         {
            auto headerPtr = db_->blockchain()->getHeaderByHash(shardTopHash);
            if (headerPtr->isMainBranch())
            {
               lastScannedHeight = headerPtr->getBlockHeight() + 1;
            }
         }
         catch (exception&)
         {
         }

         //check top scanned height vs shard bounds
         auto shard_bounds = dbSharded->getShardBounds(shardId);
         auto this_bound = min(topBlockInChain, shard_bounds.second);
         if (this_bound < lastScannedHeight)
         {
            semaIter->second.fetch_sub(1, memory_order_relaxed);
            continue;
         }

         //grab all ssh in checkpoint
         if (lastScannedHeight > 0)
         {
            BinaryWriter boundKeyMin;
            boundKeyMin.put_uint16_t(shardId, BE);
            boundKeyMin.put_BinaryDataRef(
               bound.first.getSliceRef(1, bound.first.getSize() - 1));

            BinaryWriter boundKeyMax;
            boundKeyMax.put_uint16_t(shardId, BE);
            boundKeyMax.put_BinaryDataRef(
               bound.second.getSliceRef(1, bound.second.getSize() - 1));

            auto checkpointtx = db_->beginTransaction(CHECKPOINT, LMDB::ReadOnly);
            auto checkpoint_iter = db_->getIterator(CHECKPOINT);

            if (checkpoint_iter->seekToStartsWith(boundKeyMin.getDataRef()))
            {
               do
               {
                  auto keyRef = checkpoint_iter->getKeyRef();
                  if (keyRef.getSize() >= boundKeyMax.getSize() && 
                      keyRef.getSliceRef(0, boundKeyMax.getSize()) >
                     boundKeyMax.getDataRef())
                     break;

                  auto&& sshKey = keyRef.getSliceCopy(2, keyRef.getSize() - 2);

                  auto&& ssh_pair = make_pair(StoredScriptHistory(), false);
                  ssh_pair.first.unserializeDBValue(checkpoint_iter->getValueRef());

                  sshMap.insert(move(make_pair(
                     move(sshKey), move(ssh_pair))));
               } while (checkpoint_iter->advanceAndRead());
            }
         }

         //cycle over subssh tallying all values, txio counts and subssh summaries
         auto shardPtr = dbSharded->getShard(shardId, true);
         auto shardtx = shardPtr->beginTransaction(LMDB::ReadOnly);
         auto shard_iter = shardPtr->getIterator();
         if (!shard_iter->seekToStartsWith(bound.first))
         {
            semaIter->second.fetch_sub(1, memory_order_relaxed);
            continue;
         }

         auto&& result = parseSubSsh(
            move(shard_iter),
            lastScannedHeight, false,
            getDupForHeight,
            nullptr, bound.second);

         //tally results
         for (auto& ssh_pair : result.second)
         {
            //can't have values above the bound
            if (ssh_pair.second.totalTxioCount_ == 0)
               continue;

            auto ssh_iter = sshMap.find(ssh_pair.first);
            if (ssh_iter == sshMap.end())
            {
               auto&& new_pair = make_pair(move(ssh_pair.second), true);
               auto&& insert_pair =
                  make_pair(move(ssh_pair.first), move(new_pair));
               sshMap.insert(move(insert_pair));
            }
            else
            {
               ssh_iter->second.first.addUpSummary(ssh_pair.second);
               ssh_iter->second.second = true;
            }
         }
      }

      //serialize
      auto batch = make_unique<SshBatch>(shardId);

      for (auto& ssh_pair : sshMap)
      {
         //skip unflagged ssh
         if (!ssh_pair.second.second)
            continue;

         BinaryWriter bwKey;
         bwKey.put_uint16_t(shardId, BE);
         bwKey.put_BinaryData(ssh_pair.first);

         BinaryWriter bwData;
         ssh_pair.second.first.serializeDBValue(
            bwData, BlockDataManagerConfig::getDbType());

         auto&& data_pair = make_pair(bwKey.getData(), move(bwData));
         batch->serializedSsh_.insert(move(data_pair));
      }

      //push to writer
      if (batch->serializedSsh_.size() > 0)
      {
         future<bool> fut;
         bool waitOnWriter = false;
         if (checkpointQueue_.count() > total * 4)
         {
            batch->waitOnWriter_ = move(make_unique<promise<bool>>());
            fut = batch->waitOnWriter_->get_future();
            waitOnWriter = true;
         }

         checkpointQueue_.push_back(move(batch));

         if (waitOnWriter)
            fut.wait();
      }
      else
      {
         semaIter->second.fetch_sub(1, memory_order_relaxed);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void ShardedSshParser::putCheckpoint(unsigned boundsCount)
{
   TIMER_START("checkpoints");

   auto dbPtr = db_->getDbPtr(SUBSSH);
   auto dbSharded = dynamic_pointer_cast<DatabaseContainer_Sharded>(dbPtr);
   if (dbSharded == nullptr)
      throw runtime_error("unexpected subssh db type");

   auto metaShardPtr = dbSharded->getShard(META_SHARD_ID);
   auto currentShard = dbSharded->getShardIdForHeight(scanFrom_);
   auto topShardId = dbSharded->getTopShardId();

   auto topBlockHeight = db_->blockchain()->top()->getBlockHeight();

   auto closeShard = 
      [this, metaShardPtr, dbSharded, &topBlockHeight](unsigned shardId)->void
   {
      {
         auto bounds = dbSharded->getShardBounds(shardId);
         auto topHeight = min(bounds.second, topBlockHeight);

         auto blockPtr = db_->blockchain()->getHeaderByHeight(topHeight);
        
         //update top scanned block hash entry
         BinaryWriter topHashKey;
         topHashKey.put_uint32_t(SHARD_TOPHASH_ID, BE);
         topHashKey.put_uint16_t(shardId, BE);
         auto tx = metaShardPtr->beginTransaction(LMDB::ReadWrite);

         metaShardPtr->putValue(
            topHashKey.getDataRef(), blockPtr->getThisHashRef());
      }
      
      if (!init_)
         return;

      try
      {
         //close the shard, we won't need it again
         auto shardPtr = dbSharded->getShard(shardId);
         if (!shardPtr->isOpen())
            return;

         shardPtr->close();
         LOGINFO << "closed shard #" << shardId;
      }
      catch(...)
      { }
   };

   while (1)
   {
      unique_ptr<SshBatch> dataPtr;
      try
      {
         dataPtr = move(checkpointQueue_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      auto tx = db_->beginTransaction(CHECKPOINT, LMDB::ReadWrite);
      for (auto& data_pair : dataPtr->serializedSsh_)
      {
         db_->putValue(
            CHECKPOINT, 
            data_pair.first.getRef(), data_pair.second.getDataRef());
      }

      if (dataPtr->waitOnWriter_ != nullptr)
         dataPtr->waitOnWriter_->set_value(true);

      auto semaIter = shardSemaphores_.find(dataPtr->shardId_);
      if (semaIter != shardSemaphores_.end())
      {
         auto val = semaIter->second.fetch_sub(1, memory_order_relaxed);
         if (val == 1)
         {
            closeShard(semaIter->first);
            LOGINFO << "Commited SSH shard #" << semaIter->first;
         }
      }
   }

   LOGINFO << "closing left over shards";
   {
      auto tx = metaShardPtr->beginTransaction(LMDB::ReadWrite);
      unsigned missed = 0;

      for (auto& sema : shardSemaphores_)
      {
         auto val = sema.second.load(memory_order_relaxed);
         if (val != 0)
         {
            ++missed;
            continue;
         }

         BinaryWriter topHashKey;
         topHashKey.put_uint32_t(SHARD_TOPHASH_ID, BE);
         topHashKey.put_uint16_t(sema.first, BE);

         auto bounds = dbSharded->getShardBounds(sema.first);
         auto topHeight = min(bounds.second, topBlockHeight);
         auto tophash = metaShardPtr->getValue(topHashKey.getDataRef());

         try
         {
            auto headerPtr = db_->blockchain()->getHeaderByHash(tophash);
            if (headerPtr->getBlockHeight() >= topHeight)
               continue;
         }
         catch (...)
         {
         }
            
         closeShard(sema.first);
      }
      
      if (missed > 0)
      {
         LOGINFO << "missing " << missed << " shards!";
         throw runtime_error("missed shards");
      }
   }

   TIMER_STOP("checkpoints");
   LOGINFO << "compiled ssh checkpoints in " << TIMER_READ_SEC("checkpoints") << "s";
}

////////////////////////////////////////////////////////////////////////////////
void ShardedSshParser::compileCheckpoints(void)
{
   LOGINFO << "updating ssh checkpoints";
   
   auto dbPtr = db_->getDbPtr(SUBSSH);
   auto dbSharded = dynamic_pointer_cast<DatabaseContainer_Sharded>(dbPtr);
   if (dbSharded == nullptr)
      throw runtime_error("unexpected subssh db type");

   auto firstShardId = dbSharded->getShardIdForHeight(scanFrom_);

   //create bounds
   auto&& boundsVec = getBounds(true, DB_PREFIX_SUBSSH);
   auto counter = firstShardId * boundsVec.size();
   counter_.store(counter, memory_order_relaxed);

   for (unsigned i = firstShardId; i <= dbSharded->getTopShardId(); i++)
   {
      auto& sema = shardSemaphores_[i];
      sema.store(boundsVec.size(), memory_order_relaxed);
   }

   auto writerLbd = [this, &boundsVec](void)
   {
      putCheckpoint(boundsVec.size());
   };


   auto parserLbd = [this, &boundsVec](void)
   {
      parseShardsThread(boundsVec);
   };
   
   //start writer thread
   thread writerThread(writerLbd);

   vector<thread> threads;
   for (unsigned i = 1; i < threadCount_; i++)
   {
      threads.push_back(thread(parserLbd));
   }

   parserLbd();
   for (auto& thr : threads)
      if (thr.joinable())
         thr.join();

   //kill checkpoints queue
   checkpointQueue_.completed();

   if (writerThread.joinable())
      writerThread.join();

   LOGINFO << "updated ssh checkpoints";
}

////////////////////////////////////////////////////////////////////////////////
void ShardedSshParser::tallySshThread()
{
   auto parseShards = 
      [this](
      unsigned start, unsigned end, 
      const pair<BinaryData, BinaryData>& bounds)
      ->map<BinaryData, StoredScriptHistory>
   {
      map<BinaryData, StoredScriptHistory> sshMap;

      for (unsigned i = start; i < end; i++)
      {
         auto dbIter = db_->getIterator(CHECKPOINT);
         BinaryWriter shardMin;
         shardMin.put_uint16_t(i, BE);
         shardMin.put_BinaryData(bounds.first);

         BinaryWriter shardMax;
         shardMax.put_uint16_t(i, BE);
         shardMax.put_BinaryData(bounds.second);

         if (!dbIter->seekTo(shardMin.getDataRef()))
            continue;

         do
         {
            auto keyRef = dbIter->getKeyRef();
            if (keyRef.getSize() < shardMax.getSize())
               continue;

            auto compareRef = keyRef.getSliceRef(0, shardMax.getSize());
            if (compareRef > shardMax.getDataRef())
               break;

            StoredScriptHistory ssh;
            ssh.unserializeDBKey(keyRef.getSliceRef(1, keyRef.getSize() - 1));
            ssh.unserializeDBValue(dbIter->getValueRef());

            auto sshIter = sshMap.find(ssh.uniqueKey_);
            if (sshIter == sshMap.end())
            {
               sshMap.insert(make_pair(
                  ssh.uniqueKey_, move(ssh)));
            }
            else
            {
               sshIter->second.addUpSummary(ssh);
            }
         } 
         while (dbIter->advanceAndRead());
      }

      return sshMap;
   };

   auto serializeSsh = 
      [](map<BinaryData, BinaryWriter>& serializedSsh,
      const map<BinaryData, StoredScriptHistory>& sshMap,
      DB_PREFIX prefix)->void
   {
      for (auto& ssh_pair : sshMap)
      {
         BinaryWriter bw(1 + ssh_pair.first.getSize());
         bw.put_uint8_t(prefix);
         bw.put_BinaryData(ssh_pair.first);

         auto insert_pair = serializedSsh.insert(make_pair(
            bw.getData(), BinaryWriter()));

         ssh_pair.second.serializeDBValue(
            insert_pair.first->second,
            BlockDataManagerConfig::getDbType());
      }
   };

   auto subSshDbPtr = db_->getDbPtr(SUBSSH);
   auto sshDbPtr = db_->getDbPtr(SSH);
   auto dbSharded = 
      dynamic_pointer_cast<DatabaseContainer_Sharded>(subSshDbPtr);
   if (dbSharded == nullptr)
      throw runtime_error("unexpected SUBSSH dbPtr type");

   auto firstShardScanFrom = scanFrom_;
   if (scanFrom_ > 0)
      --firstShardScanFrom;

   auto firstShard = dbSharded->getShardIdForHeight(firstShardScanFrom);
   auto nextCheckpoint = dbSharded->getShardIdForHeight(scanTo_);

   size_t tallySize = 0;

   auto tx = db_->beginTransaction(CHECKPOINT, LMDB::ReadOnly);
   
   while (1)
   {
      //grab bounds
      pair<BinaryData, BinaryData> bounds;
      try
      {
         bounds = move(sshBoundsQueue_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      //first cover checkpoint ssh data
      auto batch = make_unique<SshBatch>(0);
      if (nextCheckpoint > firstShard)
      {
         auto&& sshMap = parseShards(
            firstShard, nextCheckpoint,
            bounds);

         //grab checkpoint data for each ssh to update
         auto sshTx = sshDbPtr->beginTransaction(LMDB::ReadOnly);
         for (auto& ssh_pair : sshMap)
         {
            BinaryWriter bw(1 + ssh_pair.first.getSize());
            bw.put_uint8_t(DB_PREFIX_SCRIPT);
            bw.put_BinaryData(ssh_pair.first);

            auto ssh_data = sshDbPtr->getValue(bw.getDataRef());
            if (ssh_data.getSize() != 0)
            {
               StoredScriptHistory ssh_obj;
               ssh_obj.unserializeDBValue(ssh_data);
               ssh_pair.second.addUpSummary(ssh_obj);
            }

            //mark temp entry for clean up if applicable
            auto keyPtr = const_cast<uint8_t*>(bw.getDataRef().getPtr());
            keyPtr[0] = DB_PREFIX_TEMPSCRIPT;
            ssh_data = sshDbPtr->getValue(bw.getDataRef());
            if (ssh_data.getSize() != 0)
            {
               batch->serializedSsh_.insert(
                  make_pair(bw.getData(), BinaryWriter()));
            }
         }

         if(sshMap.size() > 0)
            serializeSsh(batch->serializedSsh_, sshMap, DB_PREFIX_SCRIPT);
      }

      //then temp ssh
      /*auto&& sshMap2 = parseShards(
         nextCheckpoint, dbSharded->getTopShardId() + 1,
         bounds);
      serializeSsh(batch->serializedSsh_,
         sshMap2,
         DB_PREFIX_TEMPSCRIPT);*/

      //push to writer thread
      if (batch->serializedSsh_.size() == 0)
         continue;

      bool waitOnWriter = false;
      future<bool> fut;
      if (serializedSshQueue_.count() >= threadCount_ * 2)
      {
         batch->waitOnWriter_ = move(make_unique<promise<bool>>());
         fut = batch->waitOnWriter_->get_future();
         waitOnWriter = true;
      }

      serializedSshQueue_.push_back(move(batch));

      if (waitOnWriter)
         fut.wait();
   }
}

////////////////////////////////////////////////////////////////////////////////
void ShardedSshParser::putSSH()
{
   auto dbPtr = db_->getDbPtr(SSH);

   while (1)
   {
      unique_ptr<SshBatch> dataPtr;

      try
      {
         dataPtr = move(serializedSshQueue_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      if (dataPtr->serializedSsh_.size() == 0)
         continue;

      auto tx = dbPtr->beginTransaction(LMDB::ReadWrite);
      for (auto& ssh_pair : dataPtr->serializedSsh_)
      {
         if (ssh_pair.second.getSize() > 0)
         {
            dbPtr->putValue(
               ssh_pair.first.getRef(),
               ssh_pair.second.getDataRef());
         }
         else
         {
            dbPtr->deleteValue(ssh_pair.first.getRef());
         }
      }

      if (dataPtr->waitOnWriter_ != nullptr)
         dataPtr->waitOnWriter_->set_value(true);

      LOGINFO << "put one ssh batch of size " << dataPtr->serializedSsh_.size();
   }
}

////////////////////////////////////////////////////////////////////////////////
void ShardedSshParser::undoCheckpoint(unsigned shardId)
{
   //gather ssh from flagged checkpoint
   map<BinaryData, StoredScriptHistory> sshMap;
   auto&& keyPrefix = WRITE_UINT16_BE(shardId);

   {
      auto tx = db_->beginTransaction(CHECKPOINT, LMDB::ReadOnly);
      auto dbIter = db_->getIterator(CHECKPOINT);
      if (!dbIter->seekToStartsWith(keyPrefix))
         return;

      do
      {
         if (!dbIter->getKeyRef().startsWith(keyPrefix.getRef()))
            break;

         pair<BinaryData, StoredScriptHistory> sshPair;

         sshPair.second.unserializeDBKey(
            dbIter->getKeyRef().getSliceRef(1, dbIter->getKeyRef().getSize() - 1));
         sshPair.second.unserializeDBValue(dbIter->getValueRef());
         sshPair.first = sshPair.second.uniqueKey_;

         sshMap.insert(move(sshPair));
      } 
      while (dbIter->advanceAndRead());
   }

   auto sshDbPtr = db_->getDbPtr(SSH);
   map<BinaryData, BinaryWriter> serializeSsh;

   //grab ssh from sshdb perm entry, substract checkpoint from it
   {
      auto tx = sshDbPtr->beginTransaction(LMDB::ReadOnly);

      for (auto& sshPair : sshMap)
      {
         BinaryWriter bw(1 + sshPair.first.getSize());
         bw.put_uint8_t(DB_PREFIX_SCRIPT);
         bw.put_BinaryData(sshPair.first);

         auto val = sshDbPtr->getValue(bw.getDataRef());
         if (val.getSize() == 0)
            continue;

         StoredScriptHistory ssh_checkpoint;
         ssh_checkpoint.unserializeDBValue(val);
         ssh_checkpoint.substractSummary(sshPair.second);

         pair<BinaryData, BinaryWriter> bwPair;
         bwPair.first = bw.getData();
         ssh_checkpoint.serializeDBValue(bwPair.second, db_->getDbType());
         serializeSsh.insert(move(bwPair));
      }
   }

   if (serializeSsh.size() == 0)
      return;

   //write to disk
   {
      auto tx = sshDbPtr->beginTransaction(LMDB::ReadWrite);
      for (auto& bwPair : serializeSsh)
      {
         sshDbPtr->putValue(
            bwPair.first.getRef(), bwPair.second.getDataRef());
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void ShardedSshParser::resetCheckpoint(unsigned shardId)
{
   set<BinaryData> sshSet;
   auto&& keyPrefix = WRITE_UINT16_BE(shardId);

   //gather ssh from flagged checkpoint
   {
      auto tx = db_->beginTransaction(CHECKPOINT, LMDB::ReadOnly);
      auto dbIter = db_->getIterator(CHECKPOINT);
      if (!dbIter->seekToStartsWith(keyPrefix))
         return;

      do
      {
         if (!dbIter->getKeyRef().startsWith(keyPrefix.getRef()))
            break;

         sshSet.insert(dbIter->getKey());
      } 
      while (dbIter->advanceAndRead());
   }

   //delete all keys
   {
      auto tx = db_->beginTransaction(CHECKPOINT, LMDB::ReadWrite);

      for (auto& ssh : sshSet)
         db_->deleteValue(CHECKPOINT, ssh.getRef());
   }

   //reset top scanned hash in meta shard
   auto dbSubSsh = db_->getDbPtr(SUBSSH);
   auto dbSharded = dynamic_pointer_cast<DatabaseContainer_Sharded>(dbSubSsh);
   if (dbSharded == nullptr)
      throw runtime_error("unexpected SUBSSH dbPtr type");

   {
      auto metaShardPtr = dbSharded->getShard(META_SHARD_ID);
      auto tx = metaShardPtr->beginTransaction(LMDB::ReadWrite);
      
      BinaryWriter key;
      key.put_uint32_t(SHARD_TOPHASH_ID, BE);
      key.put_uint16_t(shardId, BE);

      metaShardPtr->deleteValue(key.getDataRef());
   }
}

////////////////////////////////////////////////////////////////////////////////
void ShardedSshParser::undoShards(const set<unsigned>& undoneHeights)
{
   auto dbSubSsh = db_->getDbPtr(SUBSSH);
   auto dbSharded = dynamic_pointer_cast<DatabaseContainer_Sharded>(dbSubSsh);
   if(dbSharded == nullptr)
      throw runtime_error("unexpected SUBSSH dbPtr type");

   //undo checkpoints from ssh db, skip last shard as it's not covered by the
   //ssh db
   set<unsigned> shardsToUndo;
   for (auto& height : undoneHeights)
   {
      auto shardId = dbSharded->getShardIdForHeight(height);
      if (shardId == dbSharded->getTopShardId())
         continue;

      shardsToUndo.insert(shardId);
   }

   for (auto& shardId : shardsToUndo)
      undoCheckpoint(shardId);

   //check all flagged shards, reset relevant checkpoints if 
   //they're off the main chain
   set<unsigned> shardsToReset;
   for (auto& height : undoneHeights)
   {
      auto shardId = dbSharded->getShardIdForHeight(height);
      shardsToReset.insert(shardId);
   }

   {
      auto metaShardPtr = dbSharded->getShard(META_SHARD_ID);
      auto tx = metaShardPtr->beginTransaction(LMDB::ReadOnly);

      auto iter = shardsToReset.begin();
      while (iter != shardsToReset.end())
      {
         BinaryWriter key;
         key.put_uint32_t(SHARD_TOPHASH_ID, BE);
         key.put_uint16_t(*iter, BE);

         auto hash = metaShardPtr->getValue(key.getDataRef());

         try
         {
            auto headerPtr = db_->blockchain()->getHeaderByHash(hash);
            if (headerPtr->isMainBranch())
            {
               shardsToReset.erase(iter++);
               continue;
            }
         }
         catch(...)
         { }

         ++iter;
      }
   }

   if (shardsToReset.size() == 0)
      return;

   for (auto& shardId : shardsToReset)
      resetCheckpoint(shardId);
}

////////////////////////////////////////////////////////////////////////////////
vector<pair<BinaryData, BinaryData>> ShardedSshParser::getBounds(
   bool withPrefix, uint8_t prefix)
{
   set<uint8_t> special_bytes;
   special_bytes.insert(BlockDataManagerConfig::getPubkeyHashPrefix());
   special_bytes.insert(BlockDataManagerConfig::getScriptHashPrefix());
   special_bytes.insert(SCRIPT_PREFIX_P2WPKH);
   special_bytes.insert(SCRIPT_PREFIX_P2WSH);

   vector<pair<BinaryData, BinaryData>> boundsVec;

   for (uint16_t i = 0; i < 256; i++)
   {
      auto byte_iter = special_bytes.find(i);
      if (byte_iter != special_bytes.end())
      {
         for (uint16_t y = 0; y < 256; y++)
         {
            BinaryWriter bw_first;
            if (withPrefix)
               bw_first.put_uint8_t(prefix);
            bw_first.put_uint8_t(i);
            bw_first.put_uint8_t(y);

            BinaryWriter bw_last;
            bw_last.put_BinaryData(bw_first.getData());
            bw_last.put_uint8_t(0xFF);

            auto&& bounds = make_pair(bw_first.getData(), bw_last.getData());
            boundsVec.push_back(move(bounds));
         }

         continue;
      }

      BinaryWriter bw_first;
      if (withPrefix)
         bw_first.put_uint8_t(prefix);
      bw_first.put_uint8_t(i);

      BinaryWriter bw_last;
      if (withPrefix)
         bw_last.put_uint8_t(prefix);
      bw_last.put_uint8_t(i);
      bw_last.put_uint8_t(0xFF);

      auto&& bounds = make_pair(bw_first.getData(), bw_last.getData());
      boundsVec.push_back(move(bounds));
   }

   return boundsVec;
}
