////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BlockchainScanner_Super.h"
#include "EncryptionUtils.h"

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::scan()
{
   TIMER_RESTART("scan");

   unsigned scanFrom = 0;

   auto&& subsshSdbi = db_->getStoredDBInfo(SUBSSH, 0);

   try
   {
      auto topScannedBlock =
         blockchain_->getHeaderByHash(subsshSdbi.topScannedBlkHash_);

      while (!topScannedBlock->isMainBranch())
      {
         topScannedBlock = blockchain_->getHeaderByHash(
            topScannedBlock->getPrevHash());
      }

      scanFrom = topScannedBlock->getBlockHeight() + 1;
   }
   catch (range_error&)
   { }

   auto topBlock = blockchain_->top();

   if (scanFrom > topBlock->getBlockHeight())
   {
      topScannedBlockHash_ = topBlock->getThisHash();
      return;
   }

   startAt_ = scanFrom;
   LOGINFO << "scanning new blocks from #" << startAt_ << " to #" << 
      topBlock->getBlockHeight();

   heightAndDupMap_ = move(blockchain_->getHeightAndDupMap());
   vector<future<bool>> completedFutures;
   unsigned _count = 0;

   //lambdas
   auto commitLambda = [this](void)
   { writeBlockData(); };

   auto outputsLambda = [this](void)
   { processOutputs(); };

   auto inputsLambda = [this](void)
   { processInputs(); };

   auto serializeLambda = [this](void)
   { serializeSubSsh(); };


   //start threads
   auto commit_tID = thread(commitLambda);
   auto outputs_tID = thread(outputsLambda);
   auto inputs_tID = thread(inputsLambda);
   auto serialize_tID = thread(serializeLambda);

   auto startHeight = scanFrom;
   unsigned endHeight = 0;
   completedBatches_.store(0, memory_order_relaxed);

   //loop until there are no more blocks available
   try
   {
      unsigned firstBlockFileID = UINT32_MAX;
      unsigned targetBlockFileID = UINT32_MAX;

      while (startHeight <= topBlock->getBlockHeight())
      {
         //figure out how many blocks to pull for this batch
         //batches try to grab up nBlockFilesPerBatch_ worth of block data
         unsigned targetHeight = 0;
         size_t targetSize = BATCH_SIZE_SUPER;
         size_t tallySize;
         try
         {
            shared_ptr<BlockHeader> currentHeader =
               blockchain_->getHeaderByHeight(startHeight);
            firstBlockFileID = currentHeader->getBlockFileNum();

            targetBlockFileID = 0;
            targetHeight = startHeight;

            tallySize = currentHeader->getBlockSize();

            while (tallySize < targetSize)
            {
               currentHeader = blockchain_->getHeaderByHeight(++targetHeight);
               tallySize += currentHeader->getBlockSize();

               if (currentHeader->getBlockFileNum() < firstBlockFileID)
                  firstBlockFileID = currentHeader->getBlockFileNum();
            
               if (currentHeader->getBlockFileNum() > targetBlockFileID)
                  targetBlockFileID = currentHeader->getBlockFileNum();
            }

         }
         catch (range_error& e)
         {
            //if getHeaderByHeight throws before targetHeight is topBlock's height,
            //something went wrong. Otherwise we just hit the end of the chain.

            if (targetHeight < topBlock->getBlockHeight())
            {
               LOGERR << e.what();
               throw e;
            }
            else
            {
               targetHeight = topBlock->getBlockHeight();
               if (targetBlockFileID < topBlock->getBlockFileNum())
                  targetBlockFileID = topBlock->getBlockFileNum();

               if (_count == 0)
                  withUpdateSshHints_ = true;
            }
         }

         endHeight = targetHeight;

         //create batch
         auto&& batch = make_unique<ParserBatch_Super>(
            startHeight, endHeight,
            firstBlockFileID, targetBlockFileID);

         completedFutures.push_back(batch->completedPromise_.get_future());
         batch->count_ = _count;

         //post for txout parsing
         outputQueue_.push_back(move(batch));
         if (_count - completedBatches_.load(memory_order_relaxed) >= 
            writeQueueDepth_)
         {
            try
            {
               auto futIter = completedFutures.begin() + 
                  (_count - writeQueueDepth_);
               futIter->wait();
            }
            catch (future_error &e)
            {
               LOGERR << "future error";
               throw e;
            }
         }

         ++_count;
         startHeight = endHeight + 1;
      }
   }
   catch (range_error&)
   {
      LOGERR << "failed to grab block data starting height: " << startHeight;
      if (startHeight == scanFrom)
         LOGERR << "no block data was scanned";
   }
   catch (...)
   {
      LOGWARN << "scanning halted unexpectedly";
      //let the scan terminate
   }

   //mark all queues complete
   outputQueue_.completed();

   if (outputs_tID.joinable())
      outputs_tID.join();

   if (inputs_tID.joinable())
      inputs_tID.join();

   if (serialize_tID.joinable())
      serialize_tID.join();

   if (commit_tID.joinable())
      commit_tID.join();

   TIMER_STOP("scan");
   if (topBlock->getBlockHeight() - scanFrom > 100)
   {
      auto timeSpent = TIMER_READ_SEC("scan");
      LOGINFO << "scanned transaction history in " << timeSpent << "s";
   }
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::processOutputs()
{
   auto process_thread = [this](ParserBatch_Super* batch, unsigned id)->void
   {
      this->processOutputsThread(batch, id);
   };

   auto preloadBlockDataFiles = [this](ParserBatch_Super* batch)->void
   {
      if (batch == nullptr)
         return;

      auto file_id = batch->startBlockFileID_;
      while (file_id <= batch->targetBlockFileID_)
      {
         batch->fileMaps_.insert(
            make_pair(file_id, blockDataLoader_.get(file_id)));
         ++file_id;
      }
   };

   //init batch
   unique_ptr<ParserBatch_Super> batch;
   while (1)
   {
      try
      {
         batch = move(outputQueue_.pop_front());
         break;
      }
      catch (StopBlockingLoop&)
      {}
   }

   preloadBlockDataFiles(batch.get());

   while (1)
   {

      batch->txOutSshResults_.resize(totalThreadCount_);

      batch->parseTxOutStart_ = chrono::system_clock::now();
      batch->mergeTxoutSsh_ = chrono::duration<double>::zero();

      //start processing threads
      vector<thread> thr_vec;
      for (unsigned i = 0; i < totalThreadCount_; i++)
         thr_vec.push_back(thread(process_thread, batch.get(), i));

      unique_ptr<ParserBatch_Super> nextBatch;
      try
      {
         auto waitOnBatch = chrono::system_clock::now();
         nextBatch = move(outputQueue_.pop_front());
         batch->waitOnBatch_ = chrono::system_clock::now() - waitOnBatch;
      }
      catch (StopBlockingLoop&)
      {
      }

      //populate the next batch's file map while the first
      //batch is being processed
      auto preloadBlockFiles = chrono::system_clock::now();
      preloadBlockDataFiles(nextBatch.get());
      batch->preloadBlockFiles_ = 
         chrono::system_clock::now() - preloadBlockFiles;

      //wait on threads
      for (auto& thr : thr_vec)
      {
         if (thr.joinable())
            thr.join();
      }

      batch->parseTxOutEnd_ = chrono::system_clock::now();

      //push first batch for input processing
      inputQueue_.push_back(move(batch));

      //exit loop condition
      if (nextBatch == nullptr)
         break;

      //set batch for next iteration
      batch = move(nextBatch);
   }

   //done with processing ouputs, there won't be anymore batches to push 
   //to the input queue, we can mark it complete
   inputQueue_.completed();
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::processInputs()
{
   auto process_thread = [this](ParserBatch_Super* batch, unsigned id)->void
   {
      this->processInputsThread(batch, id);
   };

   while (1)
   {
      unique_ptr<ParserBatch_Super> batch;
      try
      {
         auto waitForBatch = chrono::system_clock::now();
         batch = move(inputQueue_.pop_front());
         batch->waitOnTxInBatch_ = chrono::system_clock::now() - waitForBatch;
      }
      catch (StopBlockingLoop&)
      {
         //end condition
         break;
      }

      //reset counter
      batch->blockCounter_.store(batch->start_, memory_order_relaxed);

      //alloc result vectors
      batch->txInSshResults_.resize(totalThreadCount_);
      batch->spentnessResults_.resize(totalThreadCount_);

      batch->parseTxInStart_ = chrono::system_clock::now();

      //start processing threads
      vector<thread> thr_vec;
      for (unsigned i = 1; i < totalThreadCount_; i++)
         thr_vec.push_back(thread(process_thread, batch.get(), i));
      process_thread(batch.get(), 0);

      //wait on threads
      for (auto& thr : thr_vec)
      {
         if (thr.joinable())
            thr.join();
      }

      //clear helper map
      batch->hashToDbKey_.clear();
      batch->parseTxInEnd_ = chrono::system_clock::now();

      //push for commit
      serializeQueue_.push_back(move(batch));
   }

   serializeQueue_.completed();
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::serializeSubSsh()
{
   auto process_thread = [this](ParserBatch_Super* batch)->void
   {
      this->serializeSubSshThread(batch);
   };

   while (1)
   {
      unique_ptr<ParserBatch_Super> batch;
      try
      {
         batch = move(serializeQueue_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         //end condition
         break;
      }
      
      auto serialize_start = chrono::system_clock::now();

      //prepare batch
      set<BinaryDataRef> sshKeys;
      for (auto& threadResult : batch->txOutSshResults_)
      {
         for (auto& result_pair : threadResult)
            sshKeys.insert(result_pair.first.getRef());
      }
      
      for (auto& threadResult : batch->txInSshResults_)
      {
         for (auto& result_pair : threadResult)
            sshKeys.insert(result_pair.first.getRef());
      }

      batch->subsshKeys_.reserve(sshKeys.size());
      for (auto& key : sshKeys)
      {
         batch->subsshKeys_.push_back(key);
         batch->serializedSubSsh_.insert(
            move(make_pair(key, map<BinaryDataRef, BinaryWriter>())));
      }

      batch->sshKeyCounter_.store(0, memory_order_relaxed);
      
      //start processing threads
      vector<thread> thr_vec;
      for (unsigned i = 1; i < totalThreadCount_; i++)
         thr_vec.push_back(thread(process_thread, batch.get()));
      process_thread(batch.get());

      //wait on threads
      for (auto& thr : thr_vec)
      {
         if (thr.joinable())
            thr.join();
      }

      batch->serializeSsh_ = chrono::system_clock::now() - serialize_start;

      //push for commit
      commitQueue_.push_back(move(batch));
   }

   //done with serializing batches, there won't be anymore batches to push 
   //to the commit queue, we can mark it complete
   commitQueue_.completed();
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<BlockData> BlockchainScanner_Super::getBlockData(
   ParserBatch_Super* batch, unsigned height)
{
   //grab block file map
   auto blockheader = blockchain_->getHeaderByHeight(height);
   auto filenum = blockheader->getBlockFileNum();
   auto mapIter = batch->fileMaps_.find(filenum);
   if (mapIter == batch->fileMaps_.end())
   {
      LOGERR << "Missing file map for output scan, this is unexpected";

      LOGERR << "Has the following block files:";
      for (auto& file_pair : batch->fileMaps_)
         LOGERR << " --- #" << file_pair.first;

      LOGERR << "Was looking for id #" << filenum;

      throw runtime_error("missing file map");
   }

   auto filemap = mapIter->second.get();

   //find block and deserialize it
   auto getID = [blockheader](const BinaryData&)->unsigned int
   {
      return blockheader->getThisID();
   };

   auto bdata = make_shared<BlockData>();
   bdata->deserialize(
      filemap->getPtr() + blockheader->getOffset(),
      blockheader->getBlockSize(),
      blockheader, getID, false, false);

   return bdata;
}

////////////////////////////////////////////////////////////////////////////////
StoredTxOut BlockchainScanner_Super::getStxoByHash(
   BinaryDataRef& hash, uint16_t txoId,
   ParserBatch_Super* batch,
   map<unsigned, shared_ptr<BlockDataFileMap>>& filemap)
{
   StoredTxOut stxo;

   /*#1: resolve dbkey*/
   BinaryData txoKey;
   uint32_t block_id;
   uint8_t fakedup;
   uint16_t txid;


   //check batch map first
   auto hash_iter = batch->hashToDbKey_.find(hash);
   if (hash_iter != batch->hashToDbKey_.end())
      txoKey = hash_iter->second;

   if (txoKey.getSize() == 0)
   {
      //next, fetch and resolve hints
      StoredTxHints sths;
      if (!db_->getStoredTxHints(sths, hash.getSliceRef(0, 4)))
      {
         LOGERR << "missing hints for hash";
         throw runtime_error("missing hints for hash");
      }

      for (auto& hintkey : sths.dbKeyList_)
      {
         BinaryWriter bw_key;
         bw_key.put_BinaryData(hintkey);
         bw_key.put_uint16_t(txoId, BE);

         BinaryRefReader brr(hintkey);
         DBUtils::readBlkDataKeyNoPrefix(brr, block_id, fakedup, txid);

         auto hd_iter = heightAndDupMap_.find(block_id);
         if (hd_iter == heightAndDupMap_.end())
            continue;
         if (!hd_iter->second.isMain_)
            continue;

         auto data = db_->getValueNoCopy(STXO, bw_key.getDataRef());
         if (data.getSize() == 0)
            continue;

         stxo.unserializeDBValue(data);
         if (stxo.parentHash_ == hash)
         {
            txoKey = hintkey;
            break;
         }

         stxo.dataCopy_.clear();
      }
   }

   /*#2: create stxo*/

   if (!stxo.isInitialized())
   {
      if (txoKey.getSize() == 0)
      {
         LOGERR << "could not get stxo by hash";
         throw runtime_error("could not get stxo by hash");
      }

      BinaryRefReader brr(txoKey);
      DBUtils::readBlkDataKeyNoPrefix(brr, block_id, fakedup, txid);

      //create stxo key
      BinaryWriter bw_key;
      bw_key.put_BinaryData(txoKey);
      bw_key.put_uint16_t(txoId, BE);

      auto data = db_->getValueNoCopy(STXO, bw_key.getDataRef());
      if (data.getSize() == 0)
      {
         LOGERR << "failed to grab stxo by key";
         LOGERR << "key is: " << bw_key.toHex();
         throw runtime_error("failed to grab stxo by key");
      }

      stxo.unserializeDBValue(data);
   }

   //fill in key
   auto hd_iter = heightAndDupMap_.find(block_id);
   if (hd_iter == heightAndDupMap_.end())
   {
      LOGERR << "invalid block id: " << block_id;
      LOGERR << "heightAndDupMap has " << heightAndDupMap_.size() << " entries";
      throw runtime_error("invalid block id");
   }

   stxo.blockHeight_ = hd_iter->second.height_;
   stxo.duplicateID_ = hd_iter->second.dup_;
   stxo.txIndex_     = txid;
   stxo.txOutIndex_  = txoId;

   return stxo;
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::processOutputsThread(
   ParserBatch_Super* batch, unsigned thisId)
{
   map<unsigned, shared_ptr<BlockData>> blockMap;
   map<BinaryData, BinaryData> hashToKey;
   map<BinaryData, map<BinaryData, StoredSubHistory>> sshMap;

   auto getBlock = chrono::duration<double>::zero();
   auto parseBlock = chrono::duration<double>::zero();
   auto getHashCtr = chrono::duration<double>::zero();
   auto getScrAddr = chrono::duration<double>::zero();
   auto updateSsh = chrono::duration<double>::zero();

   while (1)
   {
      auto getblock_start = chrono::system_clock::now();
      auto currentBlock =
         batch->blockCounter_.fetch_add(1, memory_order_relaxed);

      if (currentBlock > batch->end_)
         break;

      auto blockdata = getBlockData(batch, currentBlock);
      if (!blockdata->isInitialized())
      {
         LOGERR << "Could not get block data for height #" << currentBlock;
         return;
      }

      blockMap.insert(make_pair(currentBlock, blockdata));

      //TODO: flag isMultisig
      const auto header = blockdata->header();

      //update processed height
      auto topHeight = header->getBlockHeight();
      auto&& hgtx = DBUtils::heightAndDupToHgtx(
         header->getBlockHeight(), header->getDuplicateID());

      getBlock += chrono::system_clock::now() - getblock_start;
      auto parseblock_start = chrono::system_clock::now();

      auto& txns = blockdata->getTxns();
      for (unsigned i = 0; i < txns.size(); i++)
      {
         auto gethash = chrono::system_clock::now();
         const BCTX& txn = *(txns[i].get());
         auto& txHash = txn.getHash();

         auto&& txkey = 
            DBUtils::getBlkDataKeyNoPrefix(header->getThisID(), 0xFF, i);
         hashToKey.insert(make_pair(txHash, move(txkey)));

         getHashCtr += chrono::system_clock::now() - gethash;

         for (unsigned y = 0; y < txn.txouts_.size(); y++)
         {
            auto getscraddr = chrono::system_clock::now();
            auto& txout = txn.txouts_[y];

            BinaryRefReader brr(
               txn.data_ + txout.first, txout.second);
            auto value = brr.get_uint64_t();
            unsigned scriptSize = (unsigned)brr.get_var_int();
            auto&& scrRef = BtcUtils::getTxOutScrAddrNoCopy(
               brr.get_BinaryDataRef(scriptSize));

            getScrAddr += chrono::system_clock::now() - getscraddr;
            
            auto updatessh = chrono::system_clock::now();

            auto&& scrAddr = scrRef.getScrAddr();
            auto&& txioKey = DBUtils::getBlkDataKeyNoPrefix(
               header->getBlockHeight(), header->getDuplicateID(),
               i, y);

            //update ssh_
            StoredSubHistory* subsshPtr;

            auto ssh_iter = sshMap.find(scrAddr);
            if (ssh_iter == sshMap.end())
            {
               auto&& ssh_pair = make_pair(move(scrAddr),
                  map<BinaryData, StoredSubHistory>());
               auto insertIter = sshMap.insert(move(ssh_pair));
               subsshPtr = &(insertIter.first->second[hgtx]);
            }
            else
            {
               auto& ssh = sshMap[scrAddr];
               subsshPtr = &ssh[hgtx];
            }

            //deal with txio count in subssh at serialization
            TxIOPair txio;
            txio.setValue(value);
            txio.setTxOut(txioKey);
            txio.setFromCoinbase(txn.isCoinbase_);
            subsshPtr->txioMap_.insert(
               move(make_pair(move(txioKey), move(txio))));

            updateSsh += chrono::system_clock::now() - updatessh;
         }
      }

      parseBlock += chrono::system_clock::now() - parseblock_start;
   }

   batch->txOutSshResults_[thisId] = move(sshMap);

   //grab batch mutex and merge processed data in
   unique_lock<mutex> lock(batch->mergeMutex_);

   auto merge_start = chrono::system_clock::now();

   batch->blockMap_.insert(blockMap.begin(), blockMap.end());
   batch->hashToDbKey_.insert(hashToKey.begin(), hashToKey.end());

   batch->mergeTxoutSsh_ = chrono::system_clock::now() - merge_start;
   batch->getBlock_ = getBlock;
   batch->parseBlock_ = parseBlock;
   batch->getHashCtr_ = getHashCtr;
   batch->getScrAddr_ = getScrAddr;
   batch->updateSsh_ = updateSsh;
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::processInputsThread(
   ParserBatch_Super* batch, unsigned thisId)
{
   map<BinaryData, map<BinaryData, StoredSubHistory>> sshMap;
   map<BinaryData, BinaryData> spentness;

   map<unsigned, shared_ptr<BlockDataFileMap>> temp_filemap;

   auto&& stxo_tx = db_->beginTransaction(STXO, LMDB::ReadOnly);
   auto&& hints_tx = db_->beginTransaction(TXHINTS, LMDB::ReadOnly);

   while (1)
   {
      auto currentBlock =
         batch->blockCounter_.fetch_add(1, memory_order_relaxed);

      if (currentBlock > batch->end_)
         break;

      auto blockdata_iter = batch->blockMap_.find(currentBlock);
      if (blockdata_iter == batch->blockMap_.end())
      {
         LOGERR << "can't find block #" << currentBlock << " in batch";
         throw runtime_error("missing block");
      }

      auto blockdata = blockdata_iter->second;

      const auto header = blockdata->header();
      auto&& hgtx = DBUtils::getBlkDataKeyNoPrefix(
         header->getBlockHeight(), header->getDuplicateID());

      auto& txns = blockdata->getTxns();
      for (unsigned i = 0; i < txns.size(); i++)
      {
         const BCTX& txn = *(txns[i].get());

         for (unsigned y = 0; y < txn.txins_.size(); y++)
         {
            auto& txin = txn.txins_[y];
            BinaryDataRef outHash(
               txn.data_ + txin.first, 32);
            
            if (outHash == BtcUtils::EmptyHash_)
               continue;

            unsigned txOutId = READ_UINT32_LE(
               txn.data_ + txin.first + 32);

            auto&& stxo = getStxoByHash(
               outHash, txOutId,
               batch, temp_filemap);

            auto&& txinkey = DBUtils::getBlkDataKeyNoPrefix(
               header->getBlockHeight(), header->getDuplicateID(),
               i, y);

            //add to ssh_
            auto& ssh = sshMap[stxo.getScrAddress()];
            auto& subssh = ssh[hgtx];

            //deal with txio count in subssh at serialization
            TxIOPair txio;
            auto&& txoutkey = stxo.getDBKey(false);
            txio.setTxOut(txoutkey);
            txio.setTxIn(txinkey);
            txio.setValue(stxo.getValue());
            subssh.txioMap_[txoutkey] = move(txio);

            //add to spentTxOuts_
            spentness[txoutkey] = txinkey;
         }
      }
   }

   batch->txInSshResults_[thisId] = move(sshMap);
   batch->spentnessResults_[thisId] = move(spentness);
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::serializeSubSshThread(ParserBatch_Super* batch)
{
   auto mergeTxioMap = [](
      StoredSubHistory* destPtr, StoredSubHistory& orig)->void
   {
      for (auto& txio_pair : orig.txioMap_)
         destPtr->txioMap_[txio_pair.first] = move(txio_pair.second);
   };

   auto parseResults = [mergeTxioMap](
      BinaryDataRef sshKey,
      map<BinaryDataRef, StoredSubHistory*>& subsshMap,
      vector<ThreadSubSshResult>& vecResults)->void
   {
      for (auto& txoutSubssh : vecResults)
      {
         auto map_iter = txoutSubssh.find(sshKey);
         if (map_iter == txoutSubssh.end())
            continue;

         for (auto& subssh : map_iter->second)
         {
            auto iter = subsshMap.find(subssh.first);
            if (iter == subsshMap.end())
            {
               subsshMap[subssh.first.getRef()] = &subssh.second;
            }
            else
            {
               auto subsshPtr = iter->second;
               mergeTxioMap(subsshPtr, subssh.second);
            }
         }
      }
   };

   while (1)
   {
      //grab id from counter
      auto id = batch->sshKeyCounter_.fetch_add(1, memory_order_relaxed);
      if (id >= batch->subsshKeys_.size())
         break;

      //get sshkey to tally
      auto& sshKey = *(batch->subsshKeys_.begin() + id);

      //reference all relevant subssh, merge hgtx collisions
      map<BinaryDataRef, StoredSubHistory*> subsshMap;

      parseResults(sshKey, subsshMap, batch->txOutSshResults_);
      parseResults(sshKey, subsshMap, batch->txInSshResults_);

      //serialize
      auto& hgtx_map = batch->serializedSubSsh_[sshKey];
      for (auto subssh_pair : subsshMap)
      {
         auto& bw = hgtx_map[subssh_pair.first];
         subssh_pair.second->serializeDBValue(
            bw, BlockDataManagerConfig::getDbType());
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::writeSubSsh(ParserBatch_Super* batch)
{
   batch->writeSshStart_ = chrono::system_clock::now();
   auto&& tx = db_->beginTransaction(SUBSSH, LMDB::ReadWrite);

   for (auto& subssh_map : batch->serializedSubSsh_)
   {
      BinaryWriter bw(subssh_map.first.getSize() + 5);
      bw.put_uint8_t(DB_PREFIX_SUBSSH);
      bw.put_BinaryDataRef(subssh_map.first);
      bw.put_uint32_t(0);

      auto hgtx_ptr = 
         (uint32_t*)(bw.getData().getPtr() + subssh_map.first.getSize() + 1);

      for (auto& bw_pair : subssh_map.second)
      {
         auto hgtx = (uint32_t*)bw_pair.first.getPtr();
         *hgtx_ptr = *hgtx;

         db_->putValue(
            SUBSSH,
            bw.getDataRef(),
            bw_pair.second.getDataRef());
      }
   }

   //sdbi
   auto topheader = batch->blockMap_.rbegin()->second->getHeaderPtr();
   auto&& subssh_sdbi = db_->getStoredDBInfo(SUBSSH, 0);
   subssh_sdbi.topBlkHgt_ = topheader->getBlockHeight();
   subssh_sdbi.topScannedBlkHash_ = topheader->getThisHash();
   db_->putStoredDBInfo(SUBSSH, subssh_sdbi, 0);

   batch->writeSshEnd_ = chrono::system_clock::now();
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::writeBlockData()
{
   auto getGlobalOffsetForBlock = [&](unsigned height)->size_t
   {
      auto header = blockchain_->getHeaderByHeight(height);
      size_t val = header->getBlockFileNum();
      val *= 128 * 1024 * 1024;
      val += header->getOffset();
      return val;
   };

   ProgressCalculator calc(getGlobalOffsetForBlock(
      blockchain_->top()->getBlockHeight()));
   auto initVal = getGlobalOffsetForBlock(startAt_);
   calc.init(initVal);
   if (reportProgress_)
      progress_(BDMPhase_Rescan,
      calc.fractionCompleted(), UINT32_MAX,
      initVal);

   auto putSpentnessLbd = [this](ParserBatch_Super* batchPtr)->void
   {
      putSpentness(batchPtr);
   };

   while (1)
   {
      unique_ptr<ParserBatch_Super> batch;
      try
      {
         batch = move(commitQueue_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      //sanity check
      if (batch->blockMap_.size() == 0)
         continue;

      thread spentnessThr(putSpentnessLbd, batch.get());

      auto topheader = batch->blockMap_.rbegin()->second->getHeaderPtr();
      if (topheader == nullptr)
      {
         LOGERR << "empty top block header ptr, aborting scan";
         throw runtime_error("nullptr header");
      }

      {
         //subssh
         writeSubSsh(batch.get());
      }

      closeShardsByHeight(SUBSSH, topheader->getBlockHeight(), 0);

      if (spentnessThr.joinable())
         spentnessThr.join();

      if (batch->start_ != batch->end_)
      {
         LOGINFO << "scanned to height #" << batch->end_;
      }
      else
      {
         LOGINFO << "scanned block #" << batch->start_;
      }

      if(init_)
      {
         chrono::duration<double> total =
            batch->parseTxOutEnd_ - batch->parseTxOutStart_;
         //LOGINFO << "   parsed TxOuts in " << total.count() << "s";
         //LOGINFO << "     waited on batch for " << batch->waitOnBatch_.count() << "s";
         //LOGINFO << "     got block files in " << batch->preloadBlockFiles_.count() << "s";         
         //LOGINFO << "     merged txout ssh in " << batch->mergeTxoutSsh_.count() << "s";
         //LOGINFO << "     parsed block files in " << batch->parseBlock_.count() << "s";
         //LOGINFO << "     computed hashes in " << batch->getHashCtr_.count() << "s";
         //LOGINFO << "      grabbed scrAddr in " << batch->getScrAddr_.count() << "s";
         //LOGINFO << "     updated subssh in " << batch->updateSsh_.count() << "s";

         total =
            batch->parseTxInEnd_ - batch->parseTxInStart_;
         //LOGINFO << "   parsed TxIns in " << total.count() << "s";
         //LOGINFO << "     waited on batch for " << batch->waitOnTxInBatch_.count() << "s";
         //LOGINFO << "     merged txin ssh in " << batch->mergeTxInSsh_.count() << "s";
         //LOGINFO << "   serialized ssh in " << batch->serializeSsh_.count() << "s";

         total =
            batch->writeSshEnd_ - batch->writeSshStart_;
         //LOGINFO << "   put subssh in " << total.count() << "s";

         total =
            batch->writeSpentnessEnd_ - batch->writeSpentnessStart_;
         //LOGINFO << "   put spentness in " << total.count() << "s";
         
         map<unsigned, unsigned> distributionMap;
         for (auto& distPair : batch->spentnessDistribution_)
            distributionMap.insert(make_pair(distPair.second, distPair.first));

         unsigned count = 0;
         auto rIter = distributionMap.rbegin();
         while (rIter != distributionMap.rend())
         {
            if (count >= 5)
               break;

            //LOGINFO << "     shardId: " << rIter->second << ", count: " << rIter->first;

            ++count;
            ++rIter;
         }
      }

      size_t progVal = getGlobalOffsetForBlock(batch->end_);
      calc.advance(progVal);
      if (reportProgress_)
         progress_(BDMPhase_Rescan,
         calc.fractionCompleted(), calc.remainingSeconds(),
         progVal);

      topScannedBlockHash_ = topheader->getThisHash();
      batch->completedPromise_.set_value(true);
   }
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::putSpentnessThread(ParserBatch_Super* batch)
{
   auto makeKey = [](unsigned height)->BinaryData
   {
      uint64_t hgtx = uint64_t(height) << 40;
      BinaryWriter bw;
      bw.put_uint64_t(hgtx, BE);

      return bw.getData();
   };

   auto getHeight = [](const BinaryDataRef& key)->unsigned
   {
      if (key.getSize() < 4)
         throw runtime_error("invalid spentness key");

      return DBUtils::hgtxToHeight(key.getSliceRef(0, 4));
   };

   auto dbPtr = db_->getDbPtr(SPENTNESS);
   auto dbSharded = dynamic_pointer_cast<DatabaseContainer_Sharded>(dbPtr);
   if (dbSharded == nullptr)
      throw runtime_error("unexpected spentness db type");

   while (1)
   {
      auto shardId = 
         batch->spentnessKeyCounter_.fetch_add(1, memory_order_relaxed);
      if (shardId > batch->topSpentnessShard_)
         return;

      auto bounds = dbSharded->getShardBounds(shardId);

      //get shard, lock it
      auto shardPtr = dbSharded->getShard(shardId, true);
      auto tx = shardPtr->beginTransaction(LMDB::ReadWrite);

      auto&& bottomKey = makeKey(bounds.first);
      auto topKey = makeKey(bounds.second + 1);

      unsigned count = 0;

      //iterate till we're in shard bounds
      for (auto& resultMap : batch->spentnessResults_)
      {
         auto iter = resultMap.lower_bound(bottomKey);

         while (iter != resultMap.end())
         {
            if (iter->first >= topKey)
               break;

            shardPtr->putValue(iter->first.getRef(), iter->second.getRef());
            ++count;
            ++iter;
         }
      }

      auto distIter = batch->spentnessDistribution_.find(shardId);
      distIter->second = count;
   }
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::putSpentness(ParserBatch_Super* batch)
{
   /***
   TODO: get rid of this dataset, resolve spentness on the fly as:
   1) resolve txhash to blockid|txid
   2) grab stxo, get scrAddr
   3) fetch subssh for scrAddr|txoutid
   4) check is subssh carries txin
   ***/

   auto dbPtr = db_->getDbPtr(SPENTNESS);
   auto dbSharded = dynamic_pointer_cast<DatabaseContainer_Sharded>(dbPtr);
   if (dbSharded == nullptr)
      throw runtime_error("unexpected spentness db type");

   batch->writeSpentnessStart_ = chrono::system_clock::now();

   //figure out top spentness shard
   BinaryDataRef topKey;
   for (auto& spentnessMap : batch->spentnessResults_)
   {
      auto rIter = spentnessMap.rbegin();
      if (rIter != spentnessMap.rend())
      {
         if (rIter->first > topKey)
            topKey = rIter->first.getRef();
      }
   }

   unsigned topShardId = 0;
   if (topKey.getSize() != 0)
      topShardId = dbSharded->getShardIdForKey(topKey);
   batch->topSpentnessShard_ = topShardId;

   for (unsigned i = 0; i <= topShardId; i++)
   {
      batch->spentnessDistribution_.insert(
         make_pair(i, 0));
   }

   auto putSpentnessLbd = [this, batch](void)->void
   {
      this->putSpentnessThread(batch);
   };

   batch->spentnessKeyCounter_.store(0, memory_order_relaxed);
   vector<thread> threads;
   for (unsigned i = 1; i < totalThreadCount_; i++)
      threads.push_back(thread(putSpentnessLbd));
   putSpentnessThread(batch);

   for (auto& thr : threads)
      if (thr.joinable())
         thr.join();

   closeShardsById(SPENTNESS, topShardId, 5);
   batch->writeSpentnessEnd_ = chrono::system_clock::now();
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::updateSSH(bool force)
{
   //loop over all subssh entiers in SUBSSH db, 
   //compile balance, txio count and summary map for each address
   unsigned scanFrom = 0;

   auto&& sshSdbi = db_->getStoredDBInfo(SSH, 0);

   try
   {
      auto topScannedBlock =
         blockchain_->getHeaderByHash(sshSdbi.topScannedBlkHash_);

      while (!topScannedBlock->isMainBranch())
      {
         topScannedBlock = blockchain_->getHeaderByHash(
            topScannedBlock->getPrevHash());
      }

      scanFrom = topScannedBlock->getBlockHeight() + 1;
   }
   catch (range_error&)
   {
   }

   auto topBlock = blockchain_->top();

   if (force)
      scanFrom = 0;

   if (scanFrom > topBlock->getBlockHeight())
      return;
   
   TIMER_RESTART("updateSSH");

   ShardedSshParser sshParser(
      db_, scanFrom, topBlock->getBlockHeight(), totalThreadCount_, init_);
   sshParser.updateSsh();

   {
      //update sdbi
      auto topheader = blockchain_->getHeaderByHash(topScannedBlockHash_);
      auto topheight = topheader->getBlockHeight();

      //update sdbi
      sshSdbi.topScannedBlkHash_ = topBlock->getThisHash();
      sshSdbi.topBlkHgt_ = topheight;

      auto ssh_tx = db_->beginTransaction(SSH, LMDB::ReadWrite);
      db_->putStoredDBInfo(SSH, sshSdbi, 0);
   }

   TIMER_STOP("updateSSH");
   auto timeSpent = TIMER_READ_SEC("updateSSH");
   if (timeSpent >= 5)
      LOGINFO << "updated SSH in " << timeSpent << "s";
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::undo(Blockchain::ReorganizationState& reorgState)
{
   //TODO: sanity checks on header ptrs from reorgState
   if (reorgState.prevTop_->getBlockHeight() <=
      reorgState.reorgBranchPoint_->getBlockHeight())
      throw runtime_error("invalid reorg state");

   auto blockPtr = reorgState.prevTop_;
   map<uint32_t, shared_ptr<BlockDataFileMap>> fileMaps_;
   set<BinaryData> undoSpentness;

   set<unsigned> undoneHeights;

   while (blockPtr != reorgState.reorgBranchPoint_)
   {
      int currentHeight = blockPtr->getBlockHeight();
      auto currentDupId = blockPtr->getDuplicateID();

      auto&& hintsTx = db_->beginTransaction(TXHINTS, LMDB::ReadOnly);

      //grab blocks from previous top until branch point
      if (blockPtr == nullptr)
         throw runtime_error("reorg failed while tracing back to "
         "branch point");

      auto filenum = blockPtr->getBlockFileNum();
      auto fileIter = fileMaps_.find(filenum);
      if (fileIter == fileMaps_.end())
      {
         fileIter = fileMaps_.insert(make_pair(
            filenum, blockDataLoader_.get(filenum))).first;
      }

      auto filemap = fileIter->second;

      auto getID = [blockPtr]
         (const BinaryData&)->uint32_t {return blockPtr->getThisID(); };

      BlockData bdata;
      bdata.deserialize(filemap.get()->getPtr() + blockPtr->getOffset(),
         blockPtr->getBlockSize(), blockPtr, getID, false, false);

      auto& txns = bdata.getTxns();
      for (unsigned i = 0; i < txns.size(); i++)
      {
         auto& txn = txns[i];

         //undo spends from this block
         for (unsigned y = 0; y < txn->txins_.size(); y++)
         {
            auto& txin = txn->txins_[y];

            BinaryDataRef outHash(
               txn->data_ + txin.first, 32);

            if (outHash == BtcUtils::EmptyHash_)
               continue;

            uint16_t txOutId = (uint16_t)READ_UINT32_LE(
               txn->data_ + txin.first + 32);

            StoredTxOut stxo;
            if (!db_->getStoredTxOut(stxo, outHash, txOutId))
            {
               LOGERR << "failed to grab stxo";
               throw runtime_error("failed to grab stxo");
            }
            
            //mark spentness entry for deletion
            undoSpentness.insert(move(stxo.getDBKey(false)));
         }
      }

      //set blockPtr to prev block
      undoneHeights.insert(currentHeight);
      blockPtr = blockchain_->getHeaderByHash(blockPtr->getPrevHashRef());
   }

   undoneHeights.insert(blockPtr->getBlockHeight());

   int branchPointHeight =
      reorgState.reorgBranchPoint_->getBlockHeight();

   //spentness
   {
      auto&& spentness_tx = db_->beginTransaction(SPENTNESS, LMDB::ReadWrite);
      for (auto& spentness_key : undoSpentness)
         db_->deleteValue(SPENTNESS, spentness_key);
   }

   {
      //update SSH sdbi      
      auto&& tx = db_->beginTransaction(SSH, LMDB::ReadWrite);

      auto&& sdbi = db_->getStoredDBInfo(SSH, 0);
      sdbi.topScannedBlkHash_ = reorgState.reorgBranchPoint_->getThisHash();
      sdbi.topBlkHgt_ = branchPointHeight;
      db_->putStoredDBInfo(SSH, sdbi, 0);
   }

   ShardedSshParser sshParser(db_, 0, 0, 0, false);
   sshParser.undoShards(undoneHeights);
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::closeShardsByHeight(
   DB_SELECT db, unsigned height, unsigned lookup)
{
   if (!init_)
      return;

   auto dbSubSsh = db_->dbMap_.find(db)->second;
   auto dbSharded = dynamic_pointer_cast<DatabaseContainer_Sharded>(dbSubSsh);
   if (dbSharded == nullptr)
      return;

   auto shardId = dbSharded->getShardIdForHeight(height);
   closeShardsById(db, shardId, lookup);
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::closeShardsById(
   DB_SELECT db, unsigned shardId, unsigned lookup)
{
   if (!init_)
      return;
   
   if (lookup > shardId)
      return;
   shardId -= lookup;

   auto dbSubSsh = db_->dbMap_.find(db)->second;
   auto dbSharded = dynamic_pointer_cast<DatabaseContainer_Sharded>(dbSubSsh);
   if (dbSharded == nullptr)
      return;

   dbSharded->closeShardsById(shardId);
}
