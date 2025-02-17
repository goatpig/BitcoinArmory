////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BlockchainScanner_Super.h"
#include "EncryptionUtils.h"
#include "TxOutScrRef.h"

using namespace std;
using namespace Armory::Threading;

////////////////////////////////////////////////////////////////////////////////
BlockchainScanner_Super::BlockchainScanner_Super(
   std::shared_ptr<Blockchain> bc, LMDBBlockDatabase* db,
   std::shared_ptr<BlockFiles> bf, bool init,
   unsigned threadcount, unsigned queue_depth,
   ProgressCallback prg, bool reportProgress) :
   init_(init), blockchain_(bc), db_(db),
   blockFiles_(bf),
   totalThreadCount_(threadcount), writeQueueDepth_(queue_depth),
   totalBlockFileCount_(bf->fileCount()),
   progress_(prg), reportProgress_(reportProgress)
{}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::scan()
{
   TIMER_RESTART("scan");
   unsigned scanFrom = 0;
   auto&& subsshSdbi = db_->getStoredDBInfo(SUBSSH, 0);

   try {
      auto topScannedBlock =
         blockchain_->getHeaderByHash(subsshSdbi.topScannedBlkHash_);

      while (!topScannedBlock->isMainBranch())
      {
         topScannedBlock = blockchain_->getHeaderByHash(
            topScannedBlock->getPrevHash());
      }

      scanFrom = topScannedBlock->getBlockHeight() + 1;
   } catch (const range_error&) {}

   auto topBlock = blockchain_->top();

   if (scanFrom > topBlock->getBlockHeight()) {
      topScannedBlockHash_ = topBlock->getThisHash();
      return;
   }

   startAt_ = scanFrom;
   LOGINFO << "scanning new blocks from #" << startAt_ << " to #" << 
      topBlock->getBlockHeight();

   heightAndDupMap_ = move(blockchain_->getHeightAndDupMap());
   vector<shared_future<bool>> completedFutures;
   unsigned _count = 0;

   {
      //setup batch counter
      auto meta_tx = db_->beginTransaction(SUBSSH_META, LMDB::ReadOnly);

      //look for last entry in subssh_meta db
      BinaryWriter lastKey(8);
      lastKey.put_uint32_t(0xFFFFFFFF);
      lastKey.put_uint32_t(0);

      auto dbIter = db_->getIterator(SUBSSH_META);
      if (dbIter->seekToBefore(lastKey.getDataRef()) != false &&
         dbIter->getKeyRef().getSize() == 8) {
         auto&& keyReader = dbIter->getKeyReader();
         batch_counter_ = keyReader.get_uint32_t(BE) + 1;
      }
   }

   //lambdas
   auto commitLambda = [this](void)
   { commitSshBatch(); };

   //start threads
   auto commit_tID = thread(commitLambda);

   auto startHeight = scanFrom;
   unsigned endHeight = 0;
   completedBatches_.store(0, memory_order_relaxed);

   //loop until there are no more blocks available
   try {
      while (startHeight <= topBlock->getBlockHeight()) {
         //figure out how many blocks to pull for this batch
         //batches try to grab up nBlockFilesPerBatch_ worth of block data
         unsigned targetHeight = startHeight;
         size_t targetSize = BATCH_SIZE_SUPER;
         size_t tallySize;
         std::set<unsigned> blockFileIDs;
         try {
            shared_ptr<BlockHeader> currentHeader =
               blockchain_->getHeaderByHeight(startHeight, 0xFF);
            blockFileIDs.insert(currentHeader->getBlockFileNum());
            tallySize = currentHeader->getBlockSize();

            while (tallySize < targetSize) {
               currentHeader = blockchain_->getHeaderByHeight(++targetHeight, 0xFF);
               tallySize += currentHeader->getBlockSize();
               blockFileIDs.insert(currentHeader->getBlockFileNum());
            }
         } catch (const std::range_error& e) {
            //if getHeaderByHeight throws before targetHeight is topBlock's height,
            //something went wrong. Otherwise we just hit the end of the chain.

            if (targetHeight < topBlock->getBlockHeight()) {
               LOGERR << e.what();
               throw e;
            } else {
               targetHeight = topBlock->getBlockHeight();
               blockFileIDs.insert(topBlock->getBlockFileNum());

               if (_count == 0) {
                  withUpdateSshHints_ = true;
               }
            }
         }
         endHeight = targetHeight;

         //create batch
         auto blockDataBatch = make_unique<BlockDataBatch>(
            startHeight, endHeight, blockFileIDs,
            BD_ORDER_INCREMENT,
            blockFiles_, blockchain_);
         auto batch = make_unique<ParserBatch_Ssh>(move(blockDataBatch));

         shared_future<bool> batch_fut = batch->completedPromise_.get_future();
         completedFutures.push_back(batch_fut);
         batch->count_ = _count;

         //post for txout parsing
         processOutputs(batch.get());
         processInputs(batch.get());
         serializeSubSsh(move(batch));

         if (_count >
            completedBatches_.load(memory_order_relaxed) + writeQueueDepth_) {
            try {
               auto futIter = completedFutures.begin() +
                  (_count - writeQueueDepth_);
               futIter->get();
            } catch (const std::future_error &e) {
               LOGERR << "future error";
               throw e;
            }
         }

         ++_count;
         startHeight = endHeight + 1;
      }
   } catch (const std::range_error&) {
      LOGERR << "failed to grab block data starting height: " << startHeight;
      if (startHeight == scanFrom) {
         LOGERR << "no block data was scanned";
      }
   } catch (...) {
      LOGWARN << "scanning halted unexpectedly";
      //let the scan terminate
   }

   //mark all queues complete
   commitQueue_.completed();
   if (commit_tID.joinable()) {
      commit_tID.join();
   }

   TIMER_STOP("scan");
   if (topBlock->getBlockHeight() - scanFrom > 100) {
      auto timeSpent = TIMER_READ_SEC("scan");
      LOGINFO << "scanned transaction history in " << timeSpent << "s";
   }
   db_->updateHeightToIdMap(heightToId_);
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::processOutputs(ParserBatch_Ssh* batch)
{
   auto process_thread = [this](ParserBatch_Ssh* batch, unsigned id)->void
   {
      this->processOutputsThread(batch, id);
   };

   //populate the next batch's file map while the first
   //batch is being processed
   batch->bdb_->populateFileMap();

   batch->processStart_ = chrono::system_clock::now();
   batch->parseTxOutStart_ = chrono::system_clock::now();
   batch->txOutSshResults_.resize(totalThreadCount_);

   //start processing threads
   vector<thread> thr_vec;
   for (int i = 1; i < (int)totalThreadCount_ - 2; i++)
      thr_vec.push_back(thread(process_thread, batch, i));
   process_thread(batch, 0);

   //wait on threads
   for (auto& thr : thr_vec)
   {
      if (thr.joinable())
         thr.join();
   }

   batch->parseTxOutEnd_ = chrono::system_clock::now();
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::processInputs(ParserBatch_Ssh* batch)
{
   auto process_thread = [this](ParserBatch_Ssh* batch, unsigned id)->void
   {
      this->processInputsThread(batch, id);
   };

   //reset counter
   batch->resetCounter();

   //alloc result vectors
   batch->txInSshResults_.resize(totalThreadCount_);
   batch->parseTxInStart_ = chrono::system_clock::now();
   batch->bdb_->resetCounter();

   //start processing threads
   vector<thread> thr_vec;
   for (int i = 1; i < (int)totalThreadCount_ - 2; i++)
      thr_vec.push_back(thread(process_thread, batch, i));
   process_thread(batch, 0);

   //wait on threads
   for (auto& thr : thr_vec)
   {
      if (thr.joinable())
         thr.join();
   }

   //get spent offset
   batch->spent_offset_ = UINT32_MAX;
   for (auto& result : batch->txInSshResults_)
   {
      batch->spent_offset_ = min(
         batch->spent_offset_,
         result.spent_offset_);
   }

   //clear helper map
   batch->hashToDbKey_.clear();
   batch->parseTxInEnd_ = chrono::system_clock::now();
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::serializeSubSsh(
   unique_ptr<ParserBatch_Ssh> batch)
{
   auto process_thread = [this](ParserBatch_Ssh* batch)->void
   {
      this->serializeSubSshThread(batch);
   };

   auto serialize_start = chrono::system_clock::now();

   //prepare batch
   set<BinaryDataRef> sshKeys;
   for (auto& threadResult : batch->txOutSshResults_)
   {
      for (auto& result_pair : threadResult.subSshMap_)
         sshKeys.insert(result_pair.first.getRef());
   }

   for (auto& threadResult : batch->txInSshResults_)
   {
      for (auto& result_pair : threadResult.subSshMap_)
         sshKeys.insert(result_pair.first.getRef());
   }
   
   batch->batch_id_ = batch_counter_++;

   //prealloc key ref objects
   batch->keyRefs_.reserve(sshKeys.size());
   for (auto& key : sshKeys)
   {
      batch->serializedSubSsh_.insert(move(make_pair(
            key, pair<BinaryWriter, BinaryWriter>())));
      batch->keyRefs_.push_back(key);
   }

   batch->sshKeyCounter_.store(0, memory_order_relaxed);

   //start processing threads
   deque<thread> thr_vec;
   for (int i = 1; i < (int)totalThreadCount_ - 2; i++)
      thr_vec.push_back(thread(process_thread, batch.get()));
   process_thread(batch.get());

   //wait on threads
   for (auto& thr : thr_vec)
   {
      if (thr.joinable())
         thr.join();
   }

   //push for commit
   batch->serializeSsh_ = chrono::system_clock::now() - serialize_start;
   batch->insertToCommitQueue_ = chrono::system_clock::now();
   commitQueue_.push_back(move(batch));
}

////////////////////////////////////////////////////////////////////////////////
bool BlockchainScanner_Super::getTxKeyForHash(
   const BinaryDataRef& hash, BinaryData& key)
{
   //TODO: StoredTxHints could use zero copy references

   StoredTxHints sths;
   if (!db_->getStoredTxHints(sths, hash.getSliceRef(0, 4)))
   {
      LOGERR << "missing hints for hash";
      throw runtime_error("missing hints for hash");
   }

   if (sths.dbKeyList_.size() == 1)
   {
      key = move(*sths.dbKeyList_.begin());
      return true;
   }

   for (auto& hintkey : sths.dbKeyList_)
   {
      unsigned block_id;
      uint8_t fakedup;
      uint16_t txid;

      BinaryRefReader brr(hintkey);
      DBUtils::readBlkDataKeyNoPrefix(brr, block_id, fakedup, txid);

      auto hd_iter = heightAndDupMap_.find(block_id);
      if (hd_iter == heightAndDupMap_.end())
         continue;
      if (!hd_iter->second.isMain_)
         continue;

      //check hinted tx matches requested hash
      auto data = db_->getValueNoCopy(STXO, hintkey);
      if (data.getSize() < 32)
         continue;

      auto hashRef = data.getSliceRef(0, 32);
      if (hashRef == hash)
      {
         key = move(hintkey);
         return true;
      }
   }

   return false;
}

////////////////////////////////////////////////////////////////////////////////
StxoRef BlockchainScanner_Super::getStxoByHash(
   const BinaryDataRef& hash, uint16_t txoId,
   ParserBatch_Ssh* batch)
{
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
      if (!getTxKeyForHash(hash, txoKey))
      {
         stringstream ss;
         ss << "could not resolve key for hash " << hash.toHexStr();
         LOGERR << ss.str();
         throw runtime_error(ss.str());
      }
   }

   /*#2: create stxo*/
   BinaryRefReader brr(txoKey);
   DBUtils::readBlkDataKeyNoPrefix(brr, block_id, fakedup, txid);
   
   //sanity check on key
   auto hd_iter = heightAndDupMap_.find(block_id);
   if (hd_iter == heightAndDupMap_.end())
   {
      LOGERR << "invalid block id: " << block_id;
      LOGERR << "heightAndDupMap has " << heightAndDupMap_.size() << " entries";
      throw runtime_error("invalid block id");
   }

   //create stxo key
   BinaryWriter bw_key(8);
   bw_key.put_BinaryData(txoKey);
   bw_key.put_uint16_t(txoId, BE);

   auto data = db_->getValueNoCopy(STXO, bw_key.getDataRef());
   if (data.getSize() == 0)
   {
      LOGERR << "failed to grab stxo by key";
      LOGERR << "key is: " << bw_key.toHex();
      throw runtime_error("failed to grab stxo by key");
   }

   StxoRef stxo;
   stxo.unserializeDBValue(data);
   stxo.height_ = hd_iter->second.height_;
   stxo.dup_ = hd_iter->second.dup_;
   stxo.txIndex_ = txid;
   stxo.txOutIndex_ = txoId;

   return stxo;
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::processOutputsThread(
   ParserBatch_Ssh* batch, unsigned thisId)
{
   map<BinaryData, BinaryData> hashToKey;
   ThreadSubSshResult tsr;
   auto& sshMap = tsr.subSshMap_;

   auto getBlock = chrono::duration<double>::zero();
   auto parseBlock = chrono::duration<double>::zero();
   auto getHashCtr = chrono::duration<double>::zero();
   auto getScrAddr = chrono::duration<double>::zero();
   auto updateSsh = chrono::duration<double>::zero();

   while (1)
   {
      auto getblock_start = chrono::system_clock::now();
      auto currentBlock = batch->bdb_->getNext();
      if (currentBlock == nullptr)
         break;
         
      //TODO: flag isMultisig
      const auto header = currentBlock->header();

      //update processed height
      auto&& hgtx = DBUtils::heightAndDupToHgtx(
         header->getBlockHeight(), header->getDuplicateID());

      getBlock += chrono::system_clock::now() - getblock_start;
      auto parseblock_start = chrono::system_clock::now();

      auto& txns = currentBlock->getTxns();
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
               subsshPtr->height_ = header->getBlockHeight();
            }
            else
            {
               auto& ssh = sshMap[scrAddr];
               auto sub_iter = ssh.find(hgtx);
               if (sub_iter == ssh.end())
               {
                  auto&& subssh_pair = make_pair(hgtx, StoredSubHistory());
                  sub_iter = ssh.insert(move(subssh_pair)).first;
                  sub_iter->second.height_ = header->getBlockHeight();
               }

               subsshPtr = &sub_iter->second;
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

   batch->txOutSshResults_[thisId] = move(tsr);

   //grab batch mutex and merge processed data in
   unique_lock<mutex> lock(batch->mergeMutex_);
   batch->hashToDbKey_.insert(hashToKey.begin(), hashToKey.end());
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::processInputsThread(
   ParserBatch_Ssh* batch, unsigned thisId)
{
   ThreadSubSshResult tsr;
   auto& sshMap = tsr.subSshMap_;

   auto&& stxo_tx = db_->beginTransaction(STXO, LMDB::ReadOnly);
   auto&& hints_tx = db_->beginTransaction(TXHINTS, LMDB::ReadOnly);

   unsigned spent_offset = UINT32_MAX;
   while (1)
   {
      auto currentBlock =
         batch->bdb_->getNext();

      if (currentBlock == nullptr)
         break;

      const auto header = currentBlock->header();
      auto&& hgtx = DBUtils::getBlkDataKeyNoPrefix(
         header->getBlockHeight(), header->getDuplicateID());

      auto& txns = currentBlock->getTxns();
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
               outHash, txOutId, batch);

            auto&& txinkey = DBUtils::getBlkDataKeyNoPrefix(
               header->getBlockHeight(), header->getDuplicateID(),
               i, y);

            //add to ssh_
            auto&& scrAddrCopy = stxo.getScrAddressCopy();
            auto iter = sshMap.find(scrAddrCopy);
            if (iter == sshMap.end())
            {
               auto&& ssh_pair = make_pair(
                  move(scrAddrCopy),
                  map<BinaryData, StoredSubHistory>());
               iter = sshMap.insert(move(ssh_pair)).first;
            }

            auto& ssh = iter->second;
            auto& subssh = ssh[hgtx];
            subssh.height_ = header->getBlockHeight();

            //deal with txio count in subssh at serialization
            TxIOPair txio;
            auto&& txoutkey = stxo.getDBKey();
            txio.setTxOut(txoutkey);
            txio.setTxIn(txinkey);
            txio.setValue(stxo.getValue());
            subssh.txioMap_[txoutkey] = move(txio);

            spent_offset = min(spent_offset, stxo.height_);
         }
      }
   }

   tsr.spent_offset_ = spent_offset;
   batch->txInSshResults_[thisId] = move(tsr);
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::serializeSubSshThread(ParserBatch_Ssh* batch)
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
         auto map_iter = txoutSubssh.subSshMap_.find(sshKey);
         if (map_iter == txoutSubssh.subSshMap_.end())
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
      if (id >= batch->keyRefs_.size())
         break;

      //get sshkey to tally
      auto& sshKey = *(batch->keyRefs_.begin() + id);

      //reference all relevant subssh, merge hgtx collisions
      map<BinaryDataRef, StoredSubHistory*> subsshMap;
      parseResults(sshKey, subsshMap, batch->txOutSshResults_);
      parseResults(sshKey, subsshMap, batch->txInSshResults_);

      //serialize
      auto& bw_pair = batch->serializedSubSsh_[sshKey];
      StoredSubHistory::compressMany(subsshMap, 
         batch->bdb_->start_, batch->spent_offset_, 
         bw_pair.second);
      bw_pair.first.reserve(4 + sshKey.getSize());
      bw_pair.first.put_uint32_t(batch->batch_id_, BE);
      bw_pair.first.put_BinaryDataRef(sshKey);
   }
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::writeSubSsh(ParserBatch_Ssh* batch)
{
   batch->writeSshStart_ = chrono::system_clock::now();
   auto ctr = batch->batch_id_;
   auto&& tx = db_->beginTransaction(SUBSSH, LMDB::ReadWrite);
   auto&& meta_tx = db_->beginTransaction(SUBSSH_META, LMDB::ReadWrite);

   {
      //put height offset
      BinaryWriter meta_key(8), meta_data(8);
      meta_key.put_uint32_t(ctr, BE);
      meta_key.put_uint32_t(0);

      meta_data.put_uint32_t(batch->bdb_->start_);
      meta_data.put_uint32_t(batch->spent_offset_);

      db_->putValue(
         SUBSSH_META,
         meta_key.getDataRef(), meta_data.getDataRef());
   }

   for (auto& ssh_pair : batch->serializedSubSsh_)
   {
      db_->putValue(SUBSSH,
         ssh_pair.second.first.getDataRef(),
         ssh_pair.second.second.getDataRef());
   }

   //sdbi
   auto topheader = batch->bdb_->blockMap_.rbegin()->second->getHeaderPtr();
   auto&& subssh_sdbi = db_->getStoredDBInfo(SUBSSH, 0);
   subssh_sdbi.topBlkHgt_ = topheader->getBlockHeight();
   subssh_sdbi.topScannedBlkHash_ = topheader->getThisHash();
   subssh_sdbi.metaInt_ = ctr;

   db_->putStoredDBInfo(SUBSSH, subssh_sdbi, 0);

   //keep track of height range per batch id
   heightToId_.insert(make_pair(batch->bdb_->start_, ctr));
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::commitSshBatch()
{
   auto getGlobalOffsetForBlock = [&](unsigned height)->size_t
   {
      auto header = blockchain_->getHeaderByHeight(height, 0xFF);
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

   while (1)
   {
      unique_ptr<ParserBatch_Ssh> batch;
      try
      {
         batch = move(commitQueue_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      //sanity check
      if (batch->bdb_->blockMap_.size() == 0)
         continue;

      auto got_batch = chrono::system_clock::now();

      auto topheader = batch->bdb_->blockMap_.rbegin()->second->getHeaderPtr();
      if (topheader == nullptr)
      {
         LOGERR << "empty top block header ptr, aborting scan";
         throw runtime_error("nullptr header");
      }

      {
         //subssh
         writeSubSsh(batch.get());
         batch->writeSshEnd_ = chrono::system_clock::now();
      }

      if (batch->bdb_->start_ != batch->bdb_->end_)
      {
         LOGINFO << "scanned to height #" << batch->bdb_->end_;
      }
      else
      {
         LOGINFO << "scanned block #" << batch->bdb_->start_;
      }

      if(init_)
      {
         chrono::duration<double> total =
            chrono::system_clock::now() - batch->processStart_;
         LOGINFO << " batch lifetime: " << total.count() << "s";

         total =
            batch->parseTxOutEnd_ - batch->parseTxOutStart_;
         LOGINFO << "   parsed TxOuts in " << total.count() << "s";

         total =
            batch->parseTxInEnd_ - batch->parseTxInStart_;
         LOGINFO << "   parsed TxIns in " << total.count() << "s";

         total = batch->serializeSsh_;
         LOGINFO << "   serialized ssh in " << total.count() << "s";

         total =
            batch->writeSshEnd_ - batch->writeSshStart_;
         LOGINFO << "   put subssh in " << total.count() << "s";

         total = got_batch - batch->insertToCommitQueue_;
         LOGINFO << "   waited on batch for " << total.count() << "s";
      }

      size_t progVal = getGlobalOffsetForBlock(batch->bdb_->end_);
      calc.advance(progVal);
      if (reportProgress_)
         progress_(BDMPhase_Rescan,
         calc.fractionCompleted(), calc.remainingSeconds(),
         progVal);

      topScannedBlockHash_ = topheader->getThisHash();
      completedBatches_.fetch_add(1, memory_order_relaxed);
      batch->completedPromise_.set_value(true);
   }
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::scanSpentness()
{
   LOGINFO << "scanning spentness";
   TIMER_RESTART("spentness");

   heightAndDupMap_ = move(blockchain_->getHeightAndDupMap());

   completedBatches_.store(0, memory_order_relaxed);
   unsigned _count = 0;

   vector<shared_future<bool>> batchFutures;

   //start writer thread
   auto write_lbd = [this](void)->void
   {
      writeSpentness();
   };
   thread write_thread(write_lbd);

   //grab spentness db header to initialize scan state
   StoredDBInfo sdbi;
   {
      //get sdbi
      auto sdbitx = db_->beginTransaction(SPENTNESS, LMDB::ReadOnly);
      try
      {
         sdbi = move(db_->getStoredDBInfo(SPENTNESS, UINT32_MAX));
      }
      catch(exception&)
      { 
         sdbi.magic_ = Armory::Config::BitcoinSettings::getMagicBytes();
      }
   }

   //spentness db should carry last scanned height for spentness
   int end = 0;
   if (sdbi.metaInt_ != UINT64_MAX)
      end = sdbi.metaInt_ + 1;

   int start = blockchain_->top()->getBlockHeight();

   //run from current top to last commited
   while (start >= end)
   {
      //figure out batch range
      set<unsigned> blockFileIDs;
      shared_ptr<BlockHeader> currentHeader = blockchain_->getHeaderByHeight(start, 0xFF);
      blockFileIDs.insert(currentHeader->getBlockFileNum());

      size_t tallySize = currentHeader->getBlockSize();
      int nextHeight = (int)currentHeader->getBlockHeight();
      while (tallySize < BATCH_SIZE_SUPER)
      {
         if (nextHeight <= end || nextHeight == 0)
            break;

         currentHeader = blockchain_->getHeaderByHeight(--nextHeight, 0xFF);
         tallySize += currentHeader->getBlockSize();
         blockFileIDs.insert(currentHeader->getBlockFileNum());
      }

      //create batch
      auto blockDataBatch = make_unique<BlockDataBatch>(
         start, currentHeader->getBlockHeight(), blockFileIDs,
         BD_ORDER_DECREMENT,
         blockFiles_, blockchain_);
      auto batch = make_unique<ParserBatch_Spentness>(move(blockDataBatch));
      batchFutures.push_back(batch->prom_.get_future());

      //process batch
      parseSpentness(batch.get());

      //queue for write
      spentnessQueue_.push_back(move(batch));

      //check queue length, wait on commit thread if necessary
      if (_count >
         completedBatches_.load(memory_order_relaxed) + writeQueueDepth_)
      {
         try
         {
            auto futIter = batchFutures.begin() +
               (_count - writeQueueDepth_);
            futIter->get();
         }
         catch (future_error &e)
         {
            LOGERR << "future error";
            throw e;
         }
      }

      ++_count;
      start = (int)currentHeader->getBlockHeight() - 1;
   }

   spentnessQueue_.completed();
   if (write_thread.joinable())
      write_thread.join();

   //update top batch id
   {
      auto sdbitx = db_->beginTransaction(SPENTNESS, LMDB::ReadWrite);
      sdbi.metaInt_ = blockchain_->top()->getBlockHeight();;
      db_->putStoredDBInfo(SPENTNESS, sdbi, UINT32_MAX);
   }

   TIMER_STOP("spentness");
   auto timeSpent = TIMER_READ_SEC("spentness");
   LOGINFO << "parsed spentness in " << timeSpent << "s";
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::parseSpentness(ParserBatch_Spentness* batch)
{
   auto parse_lbd = [this, batch](void)->void
   {
      parseSpentnessThread(batch);
   };

   batch->bdb_->populateFileMap();
   vector<thread> threads(totalThreadCount_);
   auto threadCount = totalThreadCount_;
   if (threadCount > 2)
      threadCount -= 2;
   else
      threadCount = 0;

   for (unsigned i = 0; i < threadCount; i++)
      threads.push_back(thread(parse_lbd));
   parse_lbd();

   for (auto& thr : threads)
   {
      if (thr.joinable())
         thr.join();
   }
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::parseSpentnessThread(ParserBatch_Spentness* batch)
{
   map<BinaryData, BinaryData> keysToCommit;
   map<BinaryData, BinaryData> keysToCommitLater;

   auto hint_tx = db_->beginTransaction(TXHINTS, LMDB::ReadOnly);
   auto stxo_tx = db_->beginTransaction(STXO, LMDB::ReadOnly);

   while (true)
   {
      auto block = batch->bdb_->getNext();
      if (block == nullptr)
         break;

      auto height = block->getHeaderPtr()->getBlockHeight();
      auto dup = block->getHeaderPtr()->getDuplicateID();
      auto&& hgtx = DBUtils::getBlkDataKeyNoPrefix(height, dup);

      BinaryWriter bw(8);
      bw.put_BinaryData(hgtx);
      bw.put_uint32_t(0);
      auto txid_ptr = (uint8_t*)bw.getDataRef().getPtr() + 4;
      auto txinid_ptr = (uint8_t*)bw.getDataRef().getPtr() + 6;

      auto& txns = block->getTxns();

      //sanity check
      if (txns.size() >= UINT16_MAX)
         throw runtime_error("txn count overflow");

      for (uint16_t i = 0; i < txns.size(); i++)
      {
         //prefil txin key with txid
         auto i_ptr = (uint8_t*)&i;
         txid_ptr[0] = i_ptr[1];
         txid_ptr[1] = i_ptr[0];

         auto& txn = txns[i];
         for (unsigned y = 0; y < txn->txins_.size(); y++)
         {
            //get outpoint key
            auto& txin = txn->txins_[y];

            BinaryDataRef outHash(
               txn->data_ + txin.first, 32);

            if (outHash == BtcUtils::EmptyHash_)
               continue;

            unsigned txOutId = READ_UINT32_LE(
               txn->data_ + txin.first + 32);

            BinaryData txkey;
            if (!getTxKeyForHash(outHash, txkey))
            {
               stringstream ss;
               ss << "failed to grab key for hash";
               LOGERR << ss.str();
               throw runtime_error(ss.str());
            }

            //complete txin key with input id
            auto y_ptr = (uint8_t*)&y;
            txinid_ptr[0] = y_ptr[1];
            txinid_ptr[1] = y_ptr[0];

            //convert blockid to hgtx
            unsigned blockid;
            uint8_t dupid;
            uint16_t txid;

            BinaryRefReader brr_key(txkey);
            DBUtils::readBlkDataKeyNoPrefix(brr_key, blockid, dupid, txid);
            
            auto height_iter = heightAndDupMap_.find(blockid);
            if (height_iter == heightAndDupMap_.end())
            {
               LOGWARN << "missing height for blockid!";
               throw runtime_error("missing height for blockid!");
            }

            //create txout key
            unsigned converted_height =
               UINT32_MAX - height_iter->second.height_;
            auto&& txoutkey = DBUtils::getBlkDataKeyNoPrefix(
               converted_height, height_iter->second.dup_,
               txid, txOutId);

            auto spentness_pair = make_pair(move(txoutkey), bw.getData());

            //figure out which bucket this key goes in
            if (height_iter->second.height_ >= (uint32_t)batch->bdb_->end_)
            {
               //output belongs to tx within our batch range, we can
               //commit the spentness data right away
               keysToCommit.insert(move(spentness_pair));
            }
            else
            {
               //output belongs to a tx outside of our batch range, store
               //for later writing
               keysToCommitLater.insert(move(spentness_pair));
            }
         }
      }
   }

   //merge result into batch
   unique_lock<mutex> lock(batch->mergeMutex_);
   batch->keysToCommit_.insert(keysToCommit.begin(), keysToCommit.end());
   batch->keysToCommitLater_.insert(
      keysToCommitLater.begin(), keysToCommitLater.end());
}

////////////////////////////////////////////////////////////////////////////////
void BlockchainScanner_Super::writeSpentness()
{
   map<BinaryData, BinaryData> spentnessLeftOver;

   auto dbPtr = db_;
   auto commit = [dbPtr](
      map<BinaryData, BinaryData>::iterator begin, 
      map<BinaryData, BinaryData>::iterator end)
   {
      while (begin != end)
      {
         dbPtr->putValue(SPENTNESS, 
            begin->first, begin->second);
         ++begin;
      }
   };

   while (true)
   {
      unique_ptr<ParserBatch_Spentness> batch;
      try
      {
         batch = move(spentnessQueue_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      //check spentness data leftover against current batch bounds
      auto&& bw_cutoff = DBUtils::getBlkDataKeyNoPrefix(
         UINT32_MAX - batch->bdb_->end_, 0, 0, 0);

      auto dbtx = db_->beginTransaction(SPENTNESS, LMDB::ReadWrite);
      commit(batch->keysToCommit_.begin(), batch->keysToCommit_.end());

      //tally leftover size, commit if it breaches threshold
      if (spentnessLeftOver.size() > LEFTOVER_THRESHOLD)
      {
         commit(spentnessLeftOver.begin(), spentnessLeftOver.end());
         spentnessLeftOver.clear();
      }

      //check leftovers for eligible spentness to commit
      auto eligible_spentness = spentnessLeftOver.lower_bound(bw_cutoff);
      if (eligible_spentness != spentnessLeftOver.begin())
      {
         //grab valid range, remove from leftovers
         commit(spentnessLeftOver.begin(), eligible_spentness);
         spentnessLeftOver.erase(spentnessLeftOver.begin(), eligible_spentness);
      }

      //merge in new leftovers from current batch
      for (auto& keyVal : batch->keysToCommitLater_)
         spentnessLeftOver.emplace(keyVal);

      batch->prom_.set_value(true);
      completedBatches_.fetch_add(1, memory_order_relaxed);

      LOGINFO << "updated spentness for blocks " << 
         batch->bdb_->start_ << " to " << batch->bdb_->end_;
   }

   //commit leftovers
   if (spentnessLeftOver.size())
   {
      auto dbtx = db_->beginTransaction(SPENTNESS, LMDB::ReadWrite);
      commit(spentnessLeftOver.begin(), spentnessLeftOver.end());
   }
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

   ShardedSshParser sshParser(db_, scanFrom, totalThreadCount_, init_);
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
      reorgState.reorgBranchPoint_->getBlockHeight()) {
      std::stringstream ss;
      ss << "Invalid reorg state:" << std::endl << std::endl <<
         "  previous top height: " <<
         reorgState.prevTop_->getBlockHeight() <<
         "  branch point height: " <<
         reorgState.reorgBranchPoint_->getBlockHeight() << std::endl;

      auto&& errStr = ss.str();
      LOGERR << errStr;
      throw std::runtime_error(errStr);
   }

   auto blockPtr = reorgState.prevTop_;
   std::map<uint32_t, std::shared_ptr<FileUtils::FileMap>> fileMaps_;
   std::set<BinaryData> undoSpentness;
   std::set<unsigned> undoneHeights;

   while (blockPtr != reorgState.reorgBranchPoint_) {
      int currentHeight = blockPtr->getBlockHeight();
      auto hintsTx = db_->beginTransaction(TXHINTS, LMDB::ReadOnly);

      //grab blocks from previous top until branch point
      if (blockPtr == nullptr) {
         throw std::runtime_error(
            "reorg failed while tracing back to "
            "branch point"
         );
      }

      auto filenum = blockPtr->getBlockFileNum();
      auto fileIter = fileMaps_.find(filenum);
      if (fileIter == fileMaps_.end()) {
         auto filePath = blockFiles_->getFilePathForID(filenum);
         fileIter = fileMaps_.emplace(filenum,
            std::make_shared<FileUtils::FileMap>(filePath)).first;
      }

      auto filemap = fileIter->second;
      auto getID = [blockPtr](const BinaryData&)->uint32_t
      { return blockPtr->getThisID(); };

      auto bdata = BlockData::deserialize(
         filemap->ptr() + blockPtr->getOffset(),
         blockPtr->getBlockSize(), blockPtr, getID,
         BlockData::CheckHashes::NoChecks
      );

      //undo spends from this block
      const auto& txns = bdata->getTxns();
      for (const auto& txn : txns) {
         for (unsigned y = 0; y < txn->txins_.size(); y++) {
            const auto& txin = txn->txins_[y];

            BinaryDataRef outHash(txn->data_ + txin.first, 32);
            if (outHash == BtcUtils::EmptyHash_) {
               continue;
            }

            uint16_t txOutId = (uint16_t)READ_UINT32_LE(
               txn->data_ + txin.first + 32);

            StoredTxOut stxo;
            if (!db_->getStoredTxOut(stxo, outHash, txOutId)) {
               LOGERR << "failed to grab stxo";
               throw std::runtime_error("failed to grab stxo");
            }

            //mark spentness entry for deletion
            undoSpentness.insert(move(stxo.getSpentnessKey()));
         }
      }

      //set blockPtr to prev block
      undoneHeights.insert(currentHeight);
      blockPtr = blockchain_->getHeaderByHash(blockPtr->getPrevHashRef());
   }

   int branchPointHeight = reorgState.reorgBranchPoint_->getBlockHeight();
   {
      //spentness
      auto spentness_tx = db_->beginTransaction(SPENTNESS, LMDB::ReadWrite);
      for (const auto& spentness_key : undoSpentness) {
         db_->deleteValue(SPENTNESS, spentness_key);
      }

      auto sdbi = move(db_->getStoredDBInfo(SPENTNESS, UINT32_MAX));
      sdbi.metaInt_ = branchPointHeight;
      db_->putStoredDBInfo(SPENTNESS, sdbi, UINT32_MAX);
   }

   {
      //update SSH sdbi
      auto tx = db_->beginTransaction(SSH, LMDB::ReadWrite);
      auto sdbi = db_->getStoredDBInfo(SSH, 0);
      sdbi.topScannedBlkHash_ = reorgState.reorgBranchPoint_->getThisHash();
      sdbi.topBlkHgt_ = branchPointHeight;
      db_->putStoredDBInfo(SSH, sdbi, 0);
   }

   ShardedSshParser sshParser(db_, *undoneHeights.begin(),
      totalThreadCount_, false);
   sshParser.undo();
}

////////////////////////////////////////////////////////////////////////////////
//
// StxoRef
//
////////////////////////////////////////////////////////////////////////////////
void StxoRef::unserializeDBValue(const BinaryDataRef& bdr)
{
   theData_ = bdr;

   BinaryRefReader brr(bdr.getPtr() + 10, bdr.getSize() - 8);
   auto len = brr.get_var_int();
   scriptRef_ = brr.get_BinaryDataRef(len);
}

////////////////////////////////////////////////////////////////////////////////
uint64_t StxoRef::getValue() const
{
   //Compiler will optimize this away via TBAA & inlining. This change
   //removes the potential issues from undefined behavior.
   uint64_t value;
   memcpy(&value, theData_.getPtr() + 2, sizeof(uint64_t));
   return value;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData StxoRef::getScrAddressCopy() const
{
   auto&& ref = BtcUtils::getTxOutScrAddrNoCopy(scriptRef_);
   return ref.getScrAddr();
}

////////////////////////////////////////////////////////////////////////////////
BinaryData StxoRef::getDBKey() const
{
   return DBUtils::getBlkDataKeyNoPrefix(height_, dup_, txIndex_, txOutIndex_);
}

////////////////////////////////////////////////////////////////////////////////
//
// BlockDataBatch
//
////////////////////////////////////////////////////////////////////////////////
void BlockDataBatch::populateFileMap()
{
   resetCounter();
   if (blockDataFileIDs_.empty()) {
      return;
   }

   for (const auto& id : blockDataFileIDs_) {
      auto path = blockFiles_->getFilePathForID(id);
      fileMaps_.emplace(id, std::make_shared<FileUtils::FileMap>(path));
   }

   auto begin = min(start_, end_);
   auto end = max(start_, end_);
   for (int i = begin; i <= end; i++) {
      blockMap_.insert(make_pair((unsigned)i, nullptr));
   }
};

////////////////////////////////////////////////////////////////////////////////
void BlockDataBatch::resetCounter()
{
   blockCounter_.store(start_, memory_order_relaxed);
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<BlockData> BlockDataBatch::getBlockData(unsigned height)
{
   //grab block file map
   auto blockIter = blockMap_.find(height);
   if (blockIter == blockMap_.end())
   {
      stringstream ss; 
      ss << "missing block data entry for height " << height;
      LOGERR << ss.str();
      throw runtime_error(ss.str());
   }

   if (blockIter->second != nullptr)
      return blockIter->second;

   auto blockheader = blockchain_->getHeaderByHeight(height, 0xFF);
   auto filenum = blockheader->getBlockFileNum();
   auto mapIter = fileMaps_.find(filenum);
   if (mapIter == fileMaps_.end())
   {
      LOGERR << "Missing file map for output scan, this is unexpected";

      LOGERR << "Has the following block files:";
      for (auto& file_pair : fileMaps_)
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

   auto bdata = BlockData::deserialize(
      filemap->ptr() + blockheader->getOffset(),
      blockheader->getBlockSize(),
      blockheader, getID, BlockData::CheckHashes::NoChecks);

   if (!bdata->isInitialized())
   {
      stringstream ss;
      ss << "failed to grab block data for height " << height;
      LOGERR << ss.str();
      throw runtime_error(ss.str());
   }

   blockIter->second = bdata;
   return bdata;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<BlockData> BlockDataBatch::getNext()
{
   int height;

   if (order_ == BD_ORDER_INCREMENT)
   {
      height = blockCounter_.fetch_add(1, memory_order_relaxed);
      if (height > end_)
         return nullptr;
   }
   else
   {
      height = blockCounter_.fetch_sub(1, memory_order_relaxed);
      if (height < end_)
         return nullptr;
   }

   return getBlockData(height);
}
