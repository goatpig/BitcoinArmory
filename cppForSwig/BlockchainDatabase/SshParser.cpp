////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "SshParser.h"
#include <Utils/DBUtils.h>
#include "txio.h"
#include "StoredBlockObj.h"

using namespace Armory;

SubSshParserResult parseSubSsh(
   std::unique_ptr<LDBIter> sshIter, int32_t scanFrom, bool resolveHashes,
   std::function<uint8_t(unsigned)> getDupIDForHeight,
   std::shared_ptr<const AddrAndHashMap> scrAddrMapPtr)
{
   std::map<BinaryData, StoredScriptHistory> sshMap;
   std::set<BinaryData> txnsToResolve;
   StoredScriptHistory* sshPtr = nullptr;

   do {
      while (sshIter->isValid()) {
         if (sshPtr != nullptr &&
            sshIter->getKeyRef().contains(sshPtr->uniqueKey)) {
            break;
         }

         //new address
         auto subsshkey = sshIter->getKey();
         if (subsshkey.getSize() < 5) {
            LOGWARN << "invalid scrAddr in SUBSSH db";
            sshIter->advanceAndRead();
            continue;
         }

         auto sshKey = subsshkey.getSliceCopy(1, subsshkey.getSize() - 5);
         if (scrAddrMapPtr->find(sshKey.getRef()) == scrAddrMapPtr->end()) {
            sshPtr = nullptr;
            auto newKey = sshIter->getKey().getSliceCopy(
               0, subsshkey.getSize() - 4);
            auto newHgtx = DBUtils::heightAndDupToHgtx(UINT32_MAX, 0xFF);
            newKey.append(newHgtx);
            sshIter->seekTo(newKey);
            continue;
         }

         //get what's already in the db
         sshPtr = &sshMap[sshKey];
         sshPtr->uniqueKey = sshKey;

         if (scanFrom <= 0) {
            break;
         }

         //set iterator at unscanned height
         auto hgtx = sshIter->getKeyRef().getSliceRef(-4, 4);
         auto height = DBUtils::hgtxToHeight(hgtx);
         if ((uint32_t)scanFrom > height) {
            //this ssh has already been scanned beyond the height sshIter is at,
            //let's set the iterator to the correct height (or the next key)
            auto newKey = sshIter->getKey().getSliceCopy(
               0, subsshkey.getSize() - 4);
            auto newHgtx = DBUtils::heightAndDupToHgtx(scanFrom, 0);

            newKey.append(newHgtx);
            sshIter->seekTo(newKey);
         }
      }

      //sanity checks
      if (!sshIter->isValid()) {
         break;
      }

      //deser subssh
      StoredSubHistory subssh;
      subssh.unserializeDBKey(sshIter->getKeyRef());

      //check dupID
      if (getDupIDForHeight(subssh.height) != subssh.dupID) {
         continue;
      }
      subssh.unserializeDBValue(sshIter->getValueRef());

      std::set<BinaryData> txSet;
      size_t extraTxioCount = 0;
      for (auto& txioPair : subssh.txioMap) {
         auto keyOfOutput = txioPair.second.getDBKeyOfOutput();
         if (resolveHashes) {
            auto txKey = keyOfOutput.getSliceRef(0, 6);
            txnsToResolve.emplace(txKey);
         }

         if (!txioPair.second.isMultisig()) {
            //add up balance
            if (txioPair.second.hasTxIn()) {
               //check for same block fund&spend
               auto keyOfInput = txioPair.second.getDBKeyOfInput();
               if (keyOfOutput.startsWith(keyOfInput.getSliceRef(0, 4))) {
                  //both output and input are part of the same block, skip
                  ++extraTxioCount;
                  continue;
               }

               if (resolveHashes) {
                  //this is to resolve output references in transaction build from
                  //multiple wallets (i.e coinjoin)
                  txnsToResolve.emplace(keyOfInput.getSliceRef(0, 6));
               }
               sshPtr->totalUnspent -= txioPair.second.getValue();
            } else {
               sshPtr->totalUnspent += txioPair.second.getValue();
            }
         }
      }

      //txio count
      sshPtr->totalTxioCount += subssh.txioCount + extraTxioCount;

      //build subssh summary
      sshPtr->subsshSummary[subssh.height] = subssh.txioCount;
   } while (sshIter->advanceAndRead());

   return SubSshParserResult{
      std::move(txnsToResolve),
      std::move(sshMap)
   };
}

////////////////////////////////////////////////////////////////////////////////
// ShardedSshParser
void ShardedSshParser::updateSsh()
{
   LOGINFO << "updating SSH";
   auto now = std::chrono::system_clock::now();

   commitedBoundsCounter_.store(0, std::memory_order_relaxed);
   fetchBoundsCounter_.store(0, std::memory_order_relaxed);

   //initialize bounds vector
   firstShard_ = db_->getShardIdForHeight(firstHeight_);
   setupBounds();

   //parser lambda
   auto ssh_lambda = [this](void)->void
   {
      parseSshThread();
   };

   std::vector<std::thread> threads;
   unsigned count = threadCount_;
   if (threadCount_ > 1) {
      --count;
   }

   threads.reserve(count);
   for (unsigned i = 0; i < count; i++) {
      threads.emplace_back(std::thread(ssh_lambda));
   }
   putSSH();

   for (auto& thr : threads) {
      if (thr.joinable()) {
         thr.join();
      }
   }

   std::chrono::duration<double> length = std::chrono::system_clock::now() - now;
   LOGINFO << "Updated SSH in " << length.count() << "s";
}

////////////////////////////////////////////////////////////////////////////////
void ShardedSshParser::undo()
{
   commitedBoundsCounter_.store(0, std::memory_order_relaxed);
   fetchBoundsCounter_.store(0, std::memory_order_relaxed);

   //initialize
   firstShard_ = db_->getShardIdForHeight(firstHeight_);
   undo_ = true;
   setupBounds();

   //parser lambda
   auto ssh_lambda = [this](void)->void
   {
      parseSshThread();
   };

   std::vector<std::thread> threads;
   threads.reserve(threadCount_);
   for (unsigned i = 1; i < threadCount_; i++) {
      threads.emplace_back(std::thread(ssh_lambda));
   }
   putSSH();

   for (auto& thr : threads) {
      if (thr.joinable()) {
         thr.join();
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void ShardedSshParser::putSSH()
{
   auto len = boundsVector_.size();
   auto increment = len / 100;

   for (unsigned i = 0; i < len; i++) {
      auto batch = boundsVector_[i].get();
      batch->fut_.wait();

      if (!batch->serializedSsh_.empty()) {
         auto tx = db_->beginTransaction(DB_SELECT::SSH, LMDB::Mode::ReadWrite);
         for (auto& ssh_pair : batch->serializedSsh_) {
            if (ssh_pair.second.getSize() > 0) {
               db_->putValue(DB_SELECT::SSH,
                  ssh_pair.first.getRef(),
                  ssh_pair.second.getDataRef());
            } else {
               db_->deleteValue(DB_SELECT::SSH, ssh_pair.first.getRef());
            }
         }
      }

      commitedBoundsCounter_.fetch_add(1, std::memory_order_relaxed);
      writeThreadCV_.notify_all();

      //release bound ptr
      auto batch_mv = std::move(boundsVector_[i]);
      if (increment != 0 && i%increment == 0) {
         float progress = float(i) / float(len);
         LOGINFO << "ssh scan progress: " << progress * 100.0f << "%";
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void ShardedSshParser::setupBounds()
{
   auto addBounds = [this](const BinaryData& start, const BinaryData& end)->void
   {
      auto boundsPtr = std::make_unique<SshBounds>();
      boundsPtr->bounds_ = std::move(std::make_pair(start, end));
      boundsVector_.emplace_back(std::move(boundsPtr));
   };

   BinaryData startKey;
   uint64_t tally = 0;

   //recursive lambda woohoo!
   std::function<void(SshMapping& ssh_mapping, const BinaryData& parent)> mapToBounds =
   [&startKey, &tally, &mapToBounds, &addBounds]
   (SshMapping& ssh_mapping, const BinaryData& parent)->void
   {
      for (auto& mapping : ssh_mapping.map_) {
         //sanity checks
         if (mapping.second == nullptr || mapping.second->count_ == 0) {
            continue;
         }

         //create start key if this the begining of a fresh bound
         if (startKey.empty()) {
            BinaryWriter bw_start;
            if (!parent.empty()) {
               bw_start.put_BinaryData(parent);
            }
            bw_start.put_uint8_t(mapping.first);
            startKey = bw_start.getData();
         }

         if (mapping.second->count_ > SSH_BOUNDS_BATCH_SIZE * 2) {
            //too many subssh in this slice, we should break it down a layer

            //does it have another layer?
            if (!mapping.second->map_.empty()) {
               BinaryWriter bw_parent;
               if (!parent.empty()) {
                  bw_parent.put_BinaryData(parent);
               }
               bw_parent.put_uint8_t(mapping.first);
               mapToBounds(*mapping.second, bw_parent.getData());
               continue;
            }

            //else proceed as usual
            LOGWARN << "large slice with no further layer";
         }

         tally += mapping.second->count_;
         if (tally >= SSH_BOUNDS_BATCH_SIZE) {
            BinaryWriter bw_last;
            if (!parent.empty()) {
               bw_last.put_BinaryData(parent);
            }
            bw_last.put_uint8_t(mapping.first);
            bw_last.put_uint8_t(0xFF);

            //add to container
            addBounds(startKey, bw_last.getData());

            //reset for new bounds
            tally = 0;
            startKey.clear();
         }
      }
   };

   auto sshMapping = mapSubSshDB();
   mapToBounds(sshMapping, BinaryData());

   //add last entry
   if (!startKey.empty()) {
      BinaryWriter bw_last;
      bw_last.put_uint8_t(0xFF);

      //add to container
      addBounds(startKey, bw_last.getData());
   }
   LOGINFO << "scanning " << boundsVector_.size() << " ssh bounds";
}

////////////////////////////////////////////////////////////////////////////////
SshMapping ShardedSshParser::mapSubSshDB()
{
   //lambda
   auto processLbd = [this](unsigned index)->void
   {
      mapSubSshDBThread(index);
   };

   LOGINFO << "mapping subssh db";
   SshMapping sshMapping;
   auto subssh_sdbi = db_->getStoredDBInfo(DB_SELECT::SUBSSH, 0);

   //initialize
   mapCount_.store(firstShard_, std::memory_order_relaxed);
   mappingResults_.resize(threadCount_);
   std::vector<std::thread> threads;
   threads.reserve(threadCount_);

   //start processing threads
   for (unsigned i = 1; i < threadCount_; i++) {
      threads.push_back(std::thread(processLbd, i));
   }
   processLbd(0);

   //wait on completion
   for (auto& thr : threads) {
      if (thr.joinable()) {
         thr.join();
      }
   }

   //merge results
   for (auto& mapping : mappingResults_) {
      sshMapping.merge(mapping);
   }
   return sshMapping;
}

////////////////////////////////////////////////////////////////////////////////
void ShardedSshParser::mapSubSshDBThread(unsigned index)
{
   auto tx = db_->beginTransaction(DB_SELECT::SUBSSH, LMDB::Mode::ReadOnly);
   auto& sshMapping = mappingResults_[index];

   auto subssh_sdbi = db_->getStoredDBInfo(DB_SELECT::SUBSSH, 0);
   auto top_id = subssh_sdbi.metaInt;
   auto current_id = mapCount_.fetch_add(1, std::memory_order_relaxed);

   while (current_id <= top_id) {
      auto dbIter = db_->getIterator(DB_SELECT::SUBSSH);

      {
         //seek to id
         BinaryWriter firstShardKey(4);
         firstShardKey.put_uint32_t(current_id, BE);

         if (!dbIter->seekTo(firstShardKey.getDataRef()) ||
            dbIter->getKeyRef().getSize() < 4) {
            current_id = mapCount_.fetch_add(1, std::memory_order_relaxed);
            continue;
         }

         auto keyReader = dbIter->getKeyReader();
         auto keyId = keyReader.get_uint32_t(BE);
         if (keyId != current_id) {
            current_id = mapCount_.fetch_add(1, std::memory_order_relaxed);
            continue;
         }
      }

      do {
         auto keyReader = dbIter->getKeyReader();
         if (keyReader.getSize() < 5) {
            continue;
         }

         auto key_id = keyReader.get_uint32_t(BE);
         if (key_id != current_id) {
            break;
         }

         auto key = keyReader.get_BinaryDataRef(keyReader.getSizeRemaining());
         auto ptr = key.getPtr();

         ++sshMapping.count_;
         auto first_byte = *ptr;
         auto mappingPtr = sshMapping.getMappingForKey(first_byte);
         ++mappingPtr->count_;

         if (key.getSize() < 3) {
            continue;
         }

         auto second_byte = *(ptr + 1);
         auto mappingPtr2 = mappingPtr->getMappingForKey(second_byte);
         ++mappingPtr2->count_;

         auto third_byte = *(ptr + 2);
         auto mappingPtr3 = mappingPtr2->getMappingForKey(third_byte);
         ++mappingPtr3->count_;
      } while (dbIter->advanceAndRead());
      current_id = mapCount_.fetch_add(1, std::memory_order_relaxed);
   }
}

////////////////////////////////////////////////////////////////////////////////
SshBounds* ShardedSshParser::getNext()
{
   //if write queue is too long, wait on condvar, otherwise break
   while (fetchBoundsCounter_.load(std::memory_order_relaxed) -
      commitedBoundsCounter_.load(std::memory_order_relaxed) >
      threadCount_ * 2) {
      std::unique_lock<std::mutex> lock(cvMutex_);
      writeThreadCV_.wait(lock);
   }

   //increment counter, grab bound ptr from vector
   auto id = fetchBoundsCounter_.fetch_add(1, std::memory_order_relaxed);
   if (id >= boundsVector_.size()) {
      return nullptr;
   }
   auto boundsPtr = boundsVector_[id].get();
   return boundsPtr;
}

////////////////////////////////////////////////////////////////////////////////
void ShardedSshParser::parseSshThread()
{
   //get top batch id
   auto subssh_sdbi = db_->getStoredDBInfo(DB_SELECT::SUBSSH, 0);
   auto id_max = subssh_sdbi.metaInt;

   //seek lambda
   auto seekToBoundsStart = [](LDBIter* iterPtr,
      unsigned id, SshBounds* bounds,
      BinaryData& keyStart, BinaryData& keyEnd)->bool
   {
      BinaryWriter bw_start;
      bw_start.put_uint32_t(id, BE);
      bw_start.put_BinaryData(bounds->bounds_.first);

      if (!iterPtr->seekTo(bw_start.getDataRef())) {
         return false;
      }

      BinaryRefReader brr_key(iterPtr->getKeyRef());
      auto id_key = brr_key.get_uint32_t(BE);
      if (id_key != id) {
         return false;
      }

      BinaryWriter bw_end;
      bw_end.put_uint32_t(id, BE);
      bw_end.put_BinaryData(bounds->bounds_.second);

      keyStart = bw_start.getData();
      keyEnd = bw_end.getData();
      return true;
   };

   //key compare lambda
   auto compareKeyToBounds = [](BinaryDataRef key,
      const BinaryData& start, const BinaryData& end)->int
   {
      auto len = std::min(key.getSize(), start.getSize());
      auto keyRef = std::move(key.getSliceRef(0, len));
      if (start.getSliceRef(0, len) > keyRef) {
         return -1;
      }

      len = std::min(key.getSize(), end.getSize());
      keyRef = std::move(key.getSliceRef(0, len));
      if (keyRef > end.getSliceRef(0, len)) {
         return 1;
      } else {
         return 0;
      }
   };

   //dupId check
   auto dbPtr = db_;
   auto checkDupId = [dbPtr](unsigned height, uint8_t dupId)->bool
   {
      return dupId == dbPtr->getValidDupIDForHeight(height);
   };

   //fetch base height
   auto metaTx = db_->beginTransaction(DB_SELECT::SUBSSH_META, LMDB::Mode::ReadOnly);
   auto getHeightForId = [dbPtr](unsigned id)->unsigned
   {
      BinaryWriter bw(8);
      bw.put_uint32_t(id, BE);
      bw.put_uint32_t(0);

      auto result = dbPtr->getValueNoCopy(DB_SELECT::SUBSSH_META, bw.getDataRef());
      if (result.empty()) {
         LOGERR << "cant get height base for batch id " << id;
         throw std::runtime_error{""};
      }
      BinaryRefReader brr(result);
      return brr.get_uint32_t();
   };

   auto tx = db_->beginTransaction(DB_SELECT::SUBSSH, LMDB::Mode::ReadOnly);
   while (true) {
      //grab range to work on
      auto bounds = getNext();
      if (bounds == nullptr) {
         break;
      }

      auto now = std::chrono::system_clock::now();
      std::map<BinaryDataRef, StoredScriptHistory> sshMap;

      //initialize db iterator
      auto dbIter = db_->getIterator(DB_SELECT::SUBSSH);
      unsigned current_id = firstShard_;
      BinaryData bound_start;
      BinaryData bound_end;

      while (current_id <= id_max) {
         if (!seekToBoundsStart(dbIter.get(), current_id,
            bounds, bound_start, bound_end)) {
            ++current_id;
            continue;
         }

         //get base height for id
         auto base_height = getHeightForId(current_id);
         do {
            auto keyRef = dbIter->getKeyRef();

            //compare key to bounds
            if (compareKeyToBounds(keyRef, bound_start, bound_end) != 0) {
               break;
            }

            //parse entry
            auto brr_key = dbIter->getKeyReader();

            //get ssh from map
            brr_key.advance(4);
            auto scrAddrRef = brr_key.get_BinaryDataRef(
               brr_key.getSizeRemaining());
            auto ssh_iter = sshMap.find(scrAddrRef);
            if (ssh_iter == sshMap.end()) {
               StoredScriptHistory sshNew;
               sshNew.uniqueKey = scrAddrRef;
               ssh_iter = sshMap.emplace(
                  sshNew.uniqueKey.getRef(), std::move(sshNew)).first;
            }
            auto& ssh = ssh_iter->second;

            ++bounds->count_;
            size_t totalTxioCount = 0;

            //read through values
            auto brr_data = dbIter->getValueReader();
            auto subsshcount = brr_data.get_var_int();
            for (unsigned z = 0; z < subsshcount; z++) {
               uint64_t totalValue = 0;
               unsigned extraTxCount = 0;
               auto subssh_height = brr_data.get_var_int();
               auto subssh_dupid = brr_data.get_uint8_t();

               //grab txio count
               auto txio_count = brr_data.get_var_int();
               for (unsigned y = 0; y < txio_count; y++) {
                  //get value
                  auto value = brr_data.get_var_int();

                  //get spent flag
                  auto spent_flag = brr_data.get_uint8_t();
                  switch (spent_flag)
                  {
                     case 0:
                     {
                        //unspent, add value to ssh
                        totalValue += value;

                        //skip 2 varints
                        brr_data.get_var_int();
                        brr_data.get_var_int();
                        break;
                     }

                     case 1:
                     {
                        //funds and spends in same block, no effect
                        //on value, skip 4 varints
                        brr_data.get_var_int();
                        brr_data.get_var_int();
                        brr_data.get_var_int();
                        brr_data.get_var_int();

                        //add an extra txio since this entry covers 2 tx
                        ++extraTxCount;
                        break;
                     }

                     case 0xFF:
                     {
                        //spent, substract value from ssh
                        totalValue -= value;

                        //skip 5 varints and 1 byte
                        brr_data.get_var_int();
                        brr_data.get_uint8_t();
                        brr_data.get_var_int();
                        brr_data.get_var_int();
                        brr_data.get_var_int();
                        brr_data.get_var_int();
                        break;
                     }

                     default:
                        LOGERR << "unexpected spent flag";
                        throw std::runtime_error("unexpected spent flag");
                  }
               }

               auto subsshHeight = base_height + subssh_height;
               if (subsshHeight < firstHeight_) {
                  continue;
               }
               if (!checkDupId(subsshHeight, subssh_dupid) && !undo_) {
                  continue;
               }
               ssh.totalUnspent += totalValue;
               totalTxioCount += txio_count + extraTxCount;
            }

            //tally count
            if (totalTxioCount > 0) {
               ssh.totalTxioCount += totalTxioCount;
               ssh.subsshSummary[current_id] = totalTxioCount;
            }
         } while (dbIter->advanceAndRead());

         //increment batch id
         ++current_id;
      }

      if (!sshMap.empty() && (firstShard_ != 0 || undo_)) {
         //does the key exist in db already?
         auto sshtx = db_->beginTransaction(DB_SELECT::SSH, LMDB::Mode::ReadOnly);
         auto sshIter = db_->getIterator(DB_SELECT::SSH);

         std::map<BinaryDataRef, StoredScriptHistory> substractedMap;
         auto subIter = sshMap.begin();
         do {
            if (!sshIter->seekToExact(DbPrefix::SCRIPT, subIter->first)) {
               if (undo_) {
                  LOGWARN << "failed to find ssh to undo";
               }
               ++subIter;
               continue;
            }

            StoredScriptHistory dbSsh;
            dbSsh.unserializeDBKey(sshIter->getKeyRef());
            dbSsh.unserializeDBValue(sshIter->getValueRef());

            if (!undo_) {
               subIter->second.addSummary(dbSsh);
               ++subIter;
            } else {
               dbSsh.substractSummary(subIter->second);
               substractedMap.emplace(
                  dbSsh.uniqueKey.getRef(), std::move(dbSsh));
               sshMap.erase(subIter++);
            }
         } while (subIter != sshMap.end());

         for (auto& sub_pair : substractedMap) {
            sshMap.emplace(sub_pair.first, std::move(sub_pair.second));
         }
      }

      //serialize result
      bounds->serializeResult(sshMap);
      bounds->time_ = std::chrono::system_clock::now() - now;

      //flag as completed
      bounds->completed_->set_value(true);
   }
}

////////////////////////////////////////////////////////////////////////////////
void SshBounds::serializeResult(
   std::map<BinaryDataRef, StoredScriptHistory>& sshMap)
{
   for (auto& ssh_pair : sshMap) {
      BinaryWriter bw_key(1 + ssh_pair.first.getSize());
      bw_key.put_uint8_t((uint8_t)DbPrefix::SCRIPT);
      bw_key.put_BinaryDataRef(ssh_pair.first);

      auto& bw = serializedSsh_[bw_key.getData()];
      ssh_pair.second.serializeDBValue(bw, ARMORY_DB_TYPE::Super);
   }
   sshMap.clear();
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<SshMapping> SshMapping::getMappingForKey(uint8_t key)
{
   auto iter = map_.find(key);
   if (iter == map_.end()) {
      iter = map_.emplace(key, std::make_shared<SshMapping>()).first;
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
void SshMapping::prettyPrint(std::stringstream& ss, unsigned stepping)
{
   auto printSteps = [&ss, &stepping](void)->void
   {
      for (unsigned i = 0; i < stepping; i++) {
         ss << " ";
      }
   };

   printSteps();
   ss << " count: " << count_ << std::endl;
   if (count_ > SSH_BOUNDS_BATCH_SIZE * 5) {
      printSteps();
      ss << " breaking down map:" << std::endl;

      for (auto& submap : map_) {
         if (submap.second == nullptr) {
            continue;
         }

         printSteps();
         ss << "  key: " << (unsigned)submap.first <<std:: endl;
         submap.second->prettyPrint(ss, stepping + 2);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void SshMapping::merge(SshMapping& mapping)
{
   count_ += mapping.count_;

   if (mapping.map_.empty()) {
      return;
   }

   for (auto& entry : mapping.map_) {
      auto iter = map_.find(entry.first);
      if (iter == map_.end()) {
         map_.emplace(entry);
         continue;
      }

      if (iter->second == nullptr) {
         iter->second = entry.second;
         continue;
      }

      if (entry.second == nullptr) {
         continue;
      }
      iter->second->merge(*entry.second);
   }
}
