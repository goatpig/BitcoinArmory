////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "DatabaseBuilder.h"
#include "BlockUtils.h"
#include "BlockchainScanner.h"
#include "BlockchainScanner_Super.h"
#include "ScrAddrFilter.h"
#include "Transactions.h"
#include "TxHashFilters.h"

#define REWIND_COUNT 100

using namespace std;
using namespace Armory::Config;
using HeaderPtr = std::shared_ptr<BlockHeader>;

/////////////////////////////////////////////////////////////////////////////
void dumpBlock(
   LMDBBlockDatabase* db,
   shared_ptr<BlockHeader> bh)
{
   //grab the block
   StoredHeader sbh;
   db->getStoredHeader(sbh, bh, true);

   cout << "###############################################" << endl;

   //header
   cout << "# hash: " << bh->getThisHash().toHexStr() << endl;
   cout << "# prev: " << bh->getPrevHash().toHexStr() << endl;
   cout << "# height: " << bh->getBlockHeight() << endl;
   cout << "# diffsum: " << bh->getDifficultySum() << endl;
   cout << "# size: " << bh->getBlockSize() << endl;
   cout << "########" << endl;
   cout << "# tx count: " << sbh.getNumTx() << endl;

   /*for (unsigned i=0; i<sbh.getNumTx(); i++)
   {
      auto& tx = sbh.getTxByIndex(i);
      cout << "#  hash: " << tx.getThisHash().toHexStr() << endl;
   }*/

   cout << endl;
}

/////////////////////////////////////////////////////////////////////////////
void dumpBlock(
   shared_ptr<Blockchain> bcPtr,
   LMDBBlockDatabase* db,
   unsigned blockId)
{
   //grab the header by id
   auto bh = bcPtr->getHeaderById(blockId);

   dumpBlock(db, bh);
}

/////////////////////////////////////////////////////////////////////////////
DatabaseBuilder::DatabaseBuilder(std::shared_ptr<BlockFiles> blockFiles,
   BlockDataManager& bdm,
   const ProgressCallback &progress,
   bool forceRescanSSH)
   : blockFiles_(blockFiles), blockchain_(bdm.blockchain()),
   db_(bdm.getIFace()), scrAddrFilter_(bdm.getScrAddrFilter()),
   progress_(progress), topBlockOffset_(0, 0),
   forceRescanSSH_(forceRescanSSH)
{}

/////////////////////////////////////////////////////////////////////////////
void DatabaseBuilder::init()
{
   if (DBSettings::checkChain()) {
      verifyChain();
      return;
   }

   TIMER_START("initdb");

   //list all files in block data folder
   blockFiles_->detectAllBlockFiles();

   //read all blocks already in DB and populate blockchain
   loadBlockHeadersFromDB(progress_);
   if (DBSettings::reportProgress()) {
      progress_(BDMPhase_OrganizingChain, 0, UINT32_MAX, 0);
   }

   LOGINFO << "organizing chain";
   auto initialReorgState = blockchain_->forceOrganize();
   LOGINFO << "updating branches";
   blockchain_->updateBranchingMaps(db_, initialReorgState);

   try {
      //rewind the top block offset to catch on missed blocks for db init
      auto topBlock = blockchain_->top();
      auto rewindHeight = topBlock->getBlockHeight();
      if (rewindHeight > REWIND_COUNT) {
         rewindHeight -= REWIND_COUNT;
      } else {
         rewindHeight = 1;
      }

      auto rewindBlock = blockchain_->getHeaderByHeight(rewindHeight, 0xFF);
      topBlockOffset_.fileID = rewindBlock->getBlockFileNum();
      topBlockOffset_.offset = rewindBlock->getOffset() + rewindBlock->getBlockSize();

      LOGINFO << "Rewinding " << REWIND_COUNT << " blocks";
   } catch (const std::exception&) {}

   //update db
   TIMER_START("updateblocksindb");
   LOGINFO << "updating HEADERS db";
   auto reorgState = updateBlocksInDB(
      progress_, DBSettings::reportProgress(),
      DBSettings::getDbType() == ARMORY_DB_SUPER);
   TIMER_STOP("updateblocksindb");
   double updatetime = TIMER_READ_SEC("updateblocksindb");
   LOGINFO << "updated HEADERS db in " << updatetime << "s";

   if (DBSettings::checkTxHints()) {
      checkTxHintsIntegrity();
   }
   cycleDatabases();

   int scanFrom = -1;
   bool reset = false;

   if (DBSettings::getDbType() != ARMORY_DB_SUPER) {
      //verifyTxFilters();

      //blockchain object now has the longest chain, update address history
      //retrieve all tracked addresses from DB
      scrAddrFilter_->getAllScrAddrInDB();

      //don't scan without any registered addresses
      if (scrAddrFilter_->getScanFilterAddrMap()->empty()) {
         return;
      }

      //determine from which block to start scanning
      scrAddrFilter_->getScrAddrCurrentSyncState();
      scanFrom = scrAddrFilter_->scanFrom();

      //DatabaseBuilder objects always operate on sdbi index 0
      //BlockchainScanner object depend on the underlying ScrAddrFilter uniqueID
      auto subsshSdbi = db_->getStoredDBInfo(SUBSSH, 0);
      auto sshsdbi = db_->getStoredDBInfo(SSH, 0);

      //check merkle of registered addresses vs what's in the DB
      if (!scrAddrFilter_->hasNewAddresses()) {
         //no new addresses were registered in between runs.

         if (subsshSdbi.topBlkHgt_ > sshsdbi.topBlkHgt_) {
            //SUBSSH db has scanned ahead of SSH db, no point rescanning these
            //blocks
            scanFrom = subsshSdbi.topBlkHgt_;
         }
      } else {
         //we have newly registered addresses this run, force a full rescan
         resetHistory();
         scanFrom = -1;
         reset = true;
      }
   }

   if (!reorgState.prevTopStillValid_ && !reset) {
      //reorg
      undoHistory(reorgState);

      scanFrom = min(
         scanFrom, (int)reorgState.reorgBranchPoint_->getBlockHeight() + 1);
   }

   TIMER_START("scanning");
   while (true) {
      auto topScannedBlockHash = initTransactionHistory(scanFrom);
      cycleDatabases();

      if (topScannedBlockHash == blockchain_->top()->getThisHash()) {
         break;
      }

      //if we got this far the scan failed, diagnose the DB and repair it

      LOGWARN << "topScannedBlockHash does match the hash of the current top";
      LOGWARN << "current top is height #" << blockchain_->top()->getBlockHeight();

      try {
         auto topscannedblock = blockchain_->getHeaderByHash(topScannedBlockHash);
         LOGWARN << "topScannedBlockHash is height #" << topscannedblock->getBlockHeight();
      } catch (...) {
         LOGWARN << "topScannedBlockHash is invalid";
      }

      LOGINFO << "repairing DB";

      //grab top scanned height from SUBSSH DB
      auto sdbi = db_->getStoredDBInfo(SUBSSH, 0);

      //get fileID for height
      auto topHeader = blockchain_->getHeaderByHeight(sdbi.topBlkHgt_, 0xFF);
      auto fileID = topHeader->getBlockFileNum();

      //rewind 5 blk files for the good measure
      fileID -= 5;
      if (fileID < 0) {
         fileID = 0;
      }

      //reparse these blk files
      if (!reparseBlkFiles(fileID)) {
         LOGERR << "failed to repair DB, aborting";
         throw runtime_error("failed to repair DB");
      }
   }

   TIMER_STOP("scanning");
   double scanning = TIMER_READ_SEC("scanning");
   LOGINFO << "scanned new blocks in " << scanning << "s";

   TIMER_STOP("initdb");
   double timeSpent = TIMER_READ_SEC("initdb");
   LOGINFO << "init db in " << timeSpent << "s";
}

/////////////////////////////////////////////////////////////////////////////
void DatabaseBuilder::loadBlockHeadersFromDB(
   const ProgressCallback &progress)
{
   //TODO: preload the headers db file to speed process up

   LOGINFO << "Reading headers from db";
   blockchain_->clear();

   unsigned counter = 0;
   const unsigned howManyBlocks = [&]() -> unsigned
   {
      const time_t btcEpoch = 1230963300; // genesis block ts
      const time_t now = time(nullptr);

      // every ten minutes we get a block, how many blocks exist?
      const unsigned blocks = (now - btcEpoch) / 60 / 10;
      return blocks;
   }();

   ProgressCalculator calc(howManyBlocks);
   std::deque<HeaderPtr> headers;

   const auto callback = [&](HeaderPtr h, uint32_t height, uint8_t dup)
   {
      h->setBlockHeight(height);
      h->setDuplicateID(dup);
      headers.emplace_back(h);

      if ((counter++ % 50000) != 0) {
         return;
      }

      if (!DBSettings::reportProgress()) {
         return;
      }

      calc.advance(counter);
      progress(
         BDMPhase_DBHeaders,
         calc.fractionCompleted(),
         calc.remainingSeconds(),
         counter
      );
   };

   db_->readAllHeaders(callback);
   LOGINFO << "found " << headers.size() << " headers in db";
   blockchain_->addBlocksInBulk({std::move(headers)}, false);
}

/////////////////////////////////////////////////////////////////////////////
Blockchain::ReorganizationState DatabaseBuilder::updateBlocksInDB(
   const ProgressCallback &progress, bool verbose, bool fullHints)
{
   //this is a helper to load block data files in memory
   BlockDataLoader bdl(blockFiles_, topBlockOffset_);

   //do not run more threads than there are block files to read
   unsigned threadcount = min((size_t)DBSettings::threadCount(), bdl.size());

   std::mutex progressMutex;
   unsigned lastParsedFileID = topBlockOffset_.fileID;
   if (lastParsedFileID == UINT16_MAX) {
      lastParsedFileID = 0;
   }

   //init progress
   ProgressCalculator calc(blockFiles_->fileCount());
   if (verbose) {
      calc.init(lastParsedFileID);
      progress(BDMPhase_BlockData,
         calc.fractionCompleted(),
         UINT32_MAX,
         lastParsedFileID
      );
   }

   //parser threads will start with block fileID + 1
   std::deque<std::deque<HeaderPtr>> parsedHeaders;
   auto addblocks = [&](shared_ptr<BlockOffset> bo, bool _verbose)->void
   {
      std::deque<std::deque<HeaderPtr>> headersList;
      while (true) {
         auto fileCopy = bdl.getNextCopy();
         if (!fileCopy.isValid()) {
            break;
         }

         //track the parser result
         auto result = addBlocksToDB(fileCopy, bo, fullHints);
         headersList.emplace_back(std::move(result));

         //report to progress callback every ~100 block files
         if (_verbose && fileCopy.fileID >= lastParsedFileID + 100) {
            std::unique_lock<std::mutex> lock(progressMutex, defer_lock);
            if (lock.try_lock()) {
               LOGINFO << "parsed block file #" << fileCopy.fileID;
               calc.advance(fileCopy.fileID);
               progress(BDMPhase_BlockData,
                  calc.fractionCompleted(), calc.remainingSeconds(),
                  fileCopy.fileID);

               //bump last seen file, this doesn't need to be accurate
               lastParsedFileID = fileCopy.fileID;
            }
         }
      }

      //merge all found headers into the common map
      std::unique_lock<std::mutex> lock(progressMutex);
      for (auto& headers : headersList) {
         parsedHeaders.emplace_back(std::move(headers));
      }
   };

   std::vector<std::thread> tIDs;
   std::vector<std::shared_ptr<BlockOffset>> boVec;

   //start parser threads
   for (unsigned i = 1; i < threadcount; i++) {
      boVec.push_back(std::make_shared<BlockOffset>(topBlockOffset_));
      tIDs.push_back(std::thread(addblocks, boVec.back(), verbose));
   }

   //run one parser in current thread too
   boVec.push_back(make_shared<BlockOffset>(topBlockOffset_));
   addblocks(boVec.back(), verbose);

   //wait on parser threads to complete
   for (auto& tID : tIDs) {
      if (tID.joinable()) {
         tID.join();
      }
   }

   //update top block offset
   for (auto& blockoffset : boVec) {
      if (*blockoffset > topBlockOffset_) {
         topBlockOffset_ = *blockoffset;
      }
   }

   //done parsing new blocks, add headers to blockchain
   if (verbose) {
      progress_(BDMPhase_OrganizingChain, 0, UINT32_MAX, 0);
   }
   blockchain_->addBlocksInBulk(parsedHeaders, true);

   //reorg chain
   auto reorgState = blockchain_->organize(verbose);

   //commit new headers to db
   blockchain_->putNewBareHeaders(db_);
   LOGINFO << "top block height is " << reorgState.newTop_->getBlockHeight();
   return reorgState;
}

/////////////////////////////////////////////////////////////////////////////
std::deque<HeaderPtr> DatabaseBuilder::addBlocksToDB(
   const BlockDataLoader::BlockDataCopy& bdc,
   shared_ptr<BlockOffset> bo, bool fullHints)
{
   std::vector<std::shared_ptr<BlockData>> blocksVec;
   blocksVec.reserve(200);

   auto getID = [bcPtr = blockchain_](const BinaryData&)->uint32_t
   {
      return bcPtr->getNewUniqueID();
   };

   auto tallyBlocks = [&blocksVec, bo, getID, fullHints, &bdc]
      (const uint8_t* data, size_t size, size_t offset)->bool
   {
      //deser full block, check merkle
      std::shared_ptr<BlockData> bd;
      try {
         bd = BlockData::deserialize(
            data, size, nullptr, getID,
            fullHints ? BlockData::CheckHashes::FullHints :
               BlockData::CheckHashes::TxFilters
         );
      } catch (const BlockDeserializingException &e) {
         LOGERR << "block deser except: " << e.what();
         LOGERR << "block fileID: " << bdc.fileID;
         return false;
      } catch (const std::exception &e) {
         LOGERR << "block deser exception: " << e.what();
         return false;
      } catch (...) {
         //deser failed, ignore this block
         LOGERR << "block deser unknown exception";
         return false;
      }

      //block is valid, add to container
      bd->setFileID(bdc.fileID);
      bd->setOffset(bdc.offset + offset);

      BlockOffset blockoffset(bdc.fileID, offset + bd->size());
      if (blockoffset > *bo) {
         *bo = blockoffset;
      }

      blocksVec.emplace_back(bd);
      return true;
   };

   //parseBlockFile will crawl the whole file and callback tallyBlocks
   //for each new block; tallyBlocks will deser the blocks and append
   //them to blocksVec
   parseBlockFile(bdc, tallyBlocks);

   //we got the block data, let's grab all headers from them
   std::deque<HeaderPtr> headers;
   for (const auto& block : blocksVec) {
      headers.emplace_back(block->createBlockHeader());
   }

   //check which blocks have not been seen yet, we need to
   //know this to update tx hash hints/filters
   auto insertedBlocks = blockchain_->checkForNewBlocks(headers);

   if (!fullHints) {
      //process filters
      if (DBSettings::getDbType() == ARMORY_DB_FULL) {
         //pull existing file filter bucket from db (if any)
         auto pool = db_->getFilterPoolWriter(bdc.fileID);

         if (insertedBlocks.empty()) {
            if (pool.isValid()) {
               //this block has a filter pool and there is no data to append,
               //we can return
               return headers;
            }

            //if we got this far, this block file does not add any new blocks 
            //to the chain, but it still needs an empty filter pool for the 
            //resolver to fetch. we simply let it run on an empty block set
         }

         //tally all block filters
         std::map<uint32_t, std::shared_ptr<BlockHashVector>> allFilters;
         for (const auto& block : blocksVec) {
            auto bdId = block->uniqueID();
            auto iter = insertedBlocks.find(bdId);
            if (iter == insertedBlocks.end()) {
               continue;
            }
            allFilters.emplace(bdId, block->getTxFilter());
         }

         //update bucket
         pool.update(allFilters);

         //update db entry
         db_->putFilterPoolForFileNum(bdc.fileID, pool);
      }
   } else {
      commitAllTxHints(blocksVec, insertedBlocks);
      if (DBSettings::getDbType() == ARMORY_DB_SUPER) {
         commitAllStxos(blocksVec, insertedBlocks);
      }
   }

   return headers;
}

/////////////////////////////////////////////////////////////////////////////
void DatabaseBuilder::parseBlockFile(
   const BlockDataLoader::BlockDataCopy& bdc,
   const function<bool(const uint8_t* data, size_t size, size_t offset)>& callback)
{
   //check magic bytes at start of data
   auto filePtr = bdc.data;
   auto fileSize = filePtr->size();
   auto dataPtr = filePtr->ptr();

   const auto& magicBytes = BitcoinSettings::getMagicBytes();
   auto magicBytesSize = magicBytes.getSize();
   if (fileSize < magicBytesSize) {
      std::stringstream ss;
      ss << "Block data file size is " << fileSize << "bytes long";
      throw std::runtime_error(ss.str());
   }

   //parse the file
   size_t progress = 0;
   while (progress + magicBytesSize < fileSize) {
      size_t localProgress = magicBytesSize;
      BinaryDataRef magic(dataPtr, magicBytesSize);

      if (magic != magicBytes) {
         //no magic byte trailing the last valid file offset, let's look for one
         BinaryDataRef theFile(dataPtr + localProgress,
            fileSize - progress - localProgress);
         int32_t foundOffset = theFile.find(magicBytes);
         if (foundOffset == -1) {
            return;
         }
         LOGINFO << "Found next block after skipping " << foundOffset - 4 << "bytes";

         localProgress += foundOffset;
         magic.setRef(dataPtr + localProgress, magicBytesSize);
         if (magic != magicBytes) {
            LOGWARN << "Could not find magicword in file " << bdc.fileID;
            throw std::runtime_error("parsing for magic byte failed");
         }
         localProgress += 4;
      }

      if (progress + localProgress + 4 >= fileSize) {
         return;
      }

      BinaryDataRef blockSize(dataPtr + localProgress, 4);
      localProgress += 4;
      size_t thisBlkSize = READ_UINT32_LE(blockSize.getPtr());
      if (progress + localProgress + thisBlkSize > fileSize) {
         return;
      }

      dataPtr += localProgress;
      progress += localProgress;
      if (callback(dataPtr, thisBlkSize, progress)) {
         //only advance for the whole blockSize if callback returned true
         dataPtr += thisBlkSize;
         progress += thisBlkSize;
      }
   }
}

/////////////////////////////////////////////////////////////////////////////
BinaryData DatabaseBuilder::initTransactionHistory(int32_t startHeight)
{
   //Scan history
   auto topScannedBlockHash = scanHistory(
      startHeight, DBSettings::reportProgress(), true
   );

   //return the hash of the last scanned block
   return topScannedBlockHash;
}

/////////////////////////////////////////////////////////////////////////////
BinaryData DatabaseBuilder::scanHistory(int32_t startHeight,
   bool reportprogress, bool init)
{
   if (DBSettings::getDbType() != ARMORY_DB_SUPER) {
      LOGINFO << "scanning new blocks from #" << startHeight << " to #" <<
         blockchain_->top()->getBlockHeight();

      BlockchainScanner bcs(blockchain_,
         db_, scrAddrFilter_.get(),
         blockFiles_,
         DBSettings::threadCount(), DBSettings::ramUsage(),
         progress_, reportprogress);

      bcs.scan(startHeight);
      bcs.updateSSH(forceRescanSSH_, startHeight);

      unsigned count = 0;
      while (!bcs.resolveTxHashes()) {
         ++count;
         //verifyTxFilters();

         if (count > 5) {
            LOGERR << "failed to fix filters after 5 attempts";
            break;
         }
      }

      return bcs.getTopScannedBlockHash();
   } else {
      BlockchainScanner_Super bcs(
         blockchain_, db_,
         blockFiles_, init,
         DBSettings::threadCount(), DBSettings::ramUsage(),
         progress_, reportprogress);

      bcs.scan();
      bcs.scanSpentness();
      bcs.updateSSH(forceRescanSSH_ & init);

      return bcs.getTopScannedBlockHash();
   }
}

/////////////////////////////////////////////////////////////////////////////
Blockchain::ReorganizationState DatabaseBuilder::update(void)
{
   unique_lock<mutex> lock(scrAddrFilter_->mergeLock_);

   //list new files in block data folder
   blockFiles_->detectNewBlockFiles();

   //update db
   auto&& reorgState = updateBlocksInDB(progress_, false,
      DBSettings::getDbType() == ARMORY_DB_SUPER);

   if (!reorgState.hasNewTop_)
      return reorgState;

   uint32_t startHeight = reorgState.prevTop_->getBlockHeight() + 1;

   if (!reorgState.prevTopStillValid_)
   {
      //reorg, undo blocks up to branch point
      undoHistory(reorgState);
      startHeight = reorgState.reorgBranchPoint_->getBlockHeight() + 1;
   }

   //scan new blocks
   BinaryData&& topScannedHash = scanHistory(startHeight, false, false);
   if (topScannedHash != blockchain_->top()->getThisHash())
   {
      LOGERR << "scan failure during DatabaseBuilder::update";
      throw runtime_error("scan failure during DatabaseBuilder::update");
   }

   //TODO: recover from failed scan 

   return reorgState;
}

/////////////////////////////////////////////////////////////////////////////
void DatabaseBuilder::undoHistory(
   Blockchain::ReorganizationState& reorgState)
{
   if (DBSettings::getDbType() != ARMORY_DB_SUPER) {
      BlockchainScanner bcs(blockchain_, db_, scrAddrFilter_.get(),
         blockFiles_,
         DBSettings::threadCount(), DBSettings::ramUsage(),
         progress_, false);
      bcs.undo(reorgState);
   } else {
      BlockchainScanner_Super bcs(blockchain_, db_,
         blockFiles_, false,
         DBSettings::threadCount(), DBSettings::ramUsage(),
         progress_, false);
      bcs.undo(reorgState);
   }

   blockchain_->updateBranchingMaps(db_, reorgState);
}

/////////////////////////////////////////////////////////////////////////////
void DatabaseBuilder::resetHistory()
{
   //nuke SSH, SUBSSH, TXHINT and STXO DBs
   LOGINFO << "reseting history in DB";
   db_->resetHistoryDatabases();
}

/////////////////////////////////////////////////////////////////////////////
bool DatabaseBuilder::reparseBlkFiles(unsigned fromID)
{
   std::mutex mu;
   std::map<BinaryData, HeaderPtr> headerMap;
   BlockDataLoader bdl(
      blockFiles_,
      BlockOffset{fromID, 0}
   );

   auto assessLambda = [this, &bdl, &headerMap, &mu]()->void
   {
      while (bdl.isValid()) {
         auto hmap = assessBlkFile(bdl);
         if (hmap.empty()) {
            continue;
         }
         std::unique_lock<std::mutex> lock(mu);
         headerMap.insert(hmap.begin(), hmap.end());
      }
   };

   unsigned threadcount = min((size_t)DBSettings::threadCount(), bdl.size());
   std::vector<std::thread> tIDs;
   for (unsigned i = 1; i < threadcount; i++) {
      tIDs.push_back(thread(assessLambda));
   }
   assessLambda();

   for (auto& tID : tIDs) {
      if (tID.joinable()) {
         tID.join();
      }
   }

   //headerMap contains blocks that are either missing from our blockchain 
   //object or are recorded under invalid fileID/offset. Lets forcefully add
   //them to the blockchain object, then force a full reorg

   if (headerMap.empty()) {
      LOGWARN << "did not find any damaged and/or missings blocks";
      return false;
   }

   blockchain_->forceAddBlocksInBulk(headerMap);
   blockchain_->forceOrganize();
   blockchain_->putNewBareHeaders(db_);

   //TODO: edge case: all the new blocks found were orphans, nothing was added
   //to the db, will run into the same blocks next run
   return true;
}

/////////////////////////////////////////////////////////////////////////////
std::map<BinaryData, HeaderPtr> DatabaseBuilder::assessBlkFile(
   BlockDataLoader& bdl)
{

   //ptr is null if we're out of block files
   auto fileCopy = bdl.getNextCopy();
   if (!fileCopy.isValid()) {
      return {};
   }

   std::vector<std::shared_ptr<BlockData>> bdVec;
   auto tallyBlocks = [this, &bdVec, fileID=fileCopy.fileID](
      const uint8_t* data, size_t size, size_t offset)->bool
   {
      //deser full block, check merkle
      std::shared_ptr<BlockData> bd;
      BinaryRefReader brr(data, size);

      auto getID = [this](const BinaryData&)->uint32_t
      { return blockchain_->getNewUniqueID(); };

      try {
         bd = BlockData::deserialize(
            data, size, nullptr, getID,
            BlockData::CheckHashes::TxFilters);
      } catch (...) {
         //deser failed, ignore this block
         return false;
      }

      bd->setFileID(fileID);
      bd->setOffset(offset);

      //query blockchain object for block by hash
      BlockHeader* bhPtr = nullptr;
      try {
         blockchain_->getHeaderByHash(bd->getHash());
      } catch (const range_error&) {
         //catch and continue
      }

      //add the block either if we don't have it in our blockchain object,
      //or if the offsets and/or fileID mismatch
      if (bhPtr != nullptr) {
         if (bhPtr->getBlockFileNum() == fileID &&
            bhPtr->getOffset() == offset)
            return true;
      }

      bdVec.emplace_back(std::move(bd));
      return true;
   };
   parseBlockFile(fileCopy, tallyBlocks);

   //done parsing, return headers
   //convert BlockData vector to BlockHeader map first
   std::map<BinaryData, HeaderPtr> result;
   for (const auto& bd : bdVec) {
      auto bh = bd->createBlockHeader();
      result.emplace(bh->getThisHash(), std::move(bh));
   }
   return result;
}

/////////////////////////////////////////////////////////////////////////////
void DatabaseBuilder::verifyChain()
{
   /*
   builds db (no scanning) with full txhints, then verifies all tx 
   (consensus and sigs).
   */

   //list all files in block data folder
   blockFiles_->detectAllBlockFiles();

   //read all blocks already in DB and populate blockchain
   loadBlockHeadersFromDB(progress_);

   if (DBSettings::reportProgress()) {
      progress_(BDMPhase_OrganizingChain, 0, UINT32_MAX, 0);
   }
   auto initialReorgState = blockchain_->forceOrganize();
   blockchain_->updateBranchingMaps(db_, initialReorgState);

   //update db
   LOGINFO << "updating HEADERS db";
   auto reorgState = updateBlocksInDB(
      progress_, DBSettings::reportProgress(), true);
   LOGINFO << "updated HEADERS db";

   //verify transactions
   LOGWARN << "fix me =)";
   //verifyTransactions();
}

/////////////////////////////////////////////////////////////////////////////
void DatabaseBuilder::commitAllTxHints(
   const std::vector<std::shared_ptr<BlockData>>& blocks,
   const set<unsigned>& insertedBlocks)
{
   std::map<BinaryData, StoredTxHints> txHints;

   auto addTxHint = [&](StoredTxHints& stxh, const BinaryData& txkey)->void
   {
      //make sure key isn't already in there
      for (const auto& key : stxh.dbKeyList_) {
         if (key == txkey) {
            return;
         }
      }
      stxh.dbKeyList_.push_back(txkey);
   };

   //The readwrite db transactions makes sure only one thread is batching
   //txhints at a time. This is relevant, as hints are first pulled from
   //disk then updated. In case 2 different blocks commit to the same
   //hint, one will likely overwrite the other.
   auto hintdbtx = db_->beginTransaction(TXHINTS, LMDB::ReadWrite);

   {
      auto addTxHintMap =
         [&](const shared_ptr<BCTX>& txn, const BinaryData& txkey)->void
      {
         auto txHashPrefix = txn->getHash().getSliceCopy(0, 4);
         auto& stxh = txHints[txHashPrefix];

         //pull txHint from memory first, don't want to overwrite
         //existing hints
         if (stxh.isNull()) {
            db_->getStoredTxHints(stxh, txHashPrefix);
         }
         addTxHint(stxh, txkey);
         stxh.preferredDBKey_ = stxh.dbKeyList_.front();
      };

      for (const auto& block : blocks) {
         auto blockId = block->uniqueID();
         if (insertedBlocks.find(blockId) == insertedBlocks.end()) {
            continue;
         }

         const auto& txns = block->getTxns();
         for (unsigned i=0; i < txns.size(); i++) {
            const auto& txn = txns[i];
            auto txkey = DBUtils::getBlkDataKeyNoPrefix(blockId, 0xFF, i);
            addTxHintMap(txn, txkey);
         }
      }
   }

   std::map<BinaryData, BinaryWriter> serializedHints;

   //serialize
   for (const auto& txhint : txHints) {
      auto& bw = serializedHints[txhint.second.getDBKey()];
      txhint.second.serializeDBValue(bw);
   }

   //write
   for (const auto& txhint : serializedHints) {
      db_->putValue(TXHINTS,
         txhint.first.getRef(),
         txhint.second.getDataRef());
   }
}

/////////////////////////////////////////////////////////////////////////////
void DatabaseBuilder::commitAllStxos(
   const std::vector<shared_ptr<BlockData>>& blocks,
   const std::set<uint32_t>& insertedBlocks)
{
   if (DBSettings::getDbType() != ARMORY_DB_SUPER) {
      throw runtime_error("invalid db mode");
   }
   std::vector<std::pair<BinaryData, BinaryWriter>> serializedStxos;

   for (const auto& block : blocks) {
      auto blockID = block->uniqueID();
      if (insertedBlocks.find(blockID) == insertedBlocks.end()) {
         continue;
      }

      const auto& txns = block->getTxns();
      for (unsigned i = 0; i < txns.size(); i++) {
         const auto& hash = txns[i]->getHash();
         const auto& txouts = txns[i]->txouts_;
         bool isCoinbase = (i == 0);

         //commit stxo count
         std::pair<BinaryData, BinaryWriter> stxocount;
         stxocount.first = std::move(
            DBUtils::getBlkDataKeyNoPrefix(blockID, 0xFF, i));
         stxocount.second.put_BinaryData(hash);
         stxocount.second.put_var_int(txouts.size());
         serializedStxos.push_back(std::move(stxocount));

         for (unsigned y = 0; y < txouts.size(); y++) {
            std::pair<BinaryData, BinaryWriter> bwPair;

            bwPair.first = std::move(
               DBUtils::getBlkDataKeyNoPrefix(blockID, 0xFF, i, y));
            auto txoutDataRef = txns[i]->getTxOutRef(y);

            StoredTxOut::serializeDBValue(
               bwPair.second,
               0, isCoinbase,
               txoutDataRef,
               TXOUT_UNSPENT, {});
            serializedStxos.push_back(std::move(bwPair));
         }
      }
   }

   auto tx = db_->beginTransaction(STXO, LMDB::ReadWrite);
   for (auto& bwPair : serializedStxos) {
      if (bwPair.first.getSize() == 6) {
         auto key = db_->getValueNoCopy(STXO, bwPair.first.getRef());
         if (!key.empty()) {
            std::string errMsg{"trying to commit stxo key " +
               bwPair.first.toHexStr() +
               " which already exists, aborting!"
            };
            LOGERR << errMsg;
            throw std::runtime_error(errMsg);
         }
      }

      db_->putValue(
         STXO, bwPair.first.getRef(), bwPair.second.getDataRef());
   }
}

#if 0
/////////////////////////////////////////////////////////////////////////////
void DatabaseBuilder::verifyTransactions()
{
   struct ParserState
   {
      atomic<unsigned> blockHeight_;
      atomic<unsigned> unknownErrors_;
      atomic<unsigned> unsupportedSigHash_;
      atomic<unsigned> unresolvedHashes_;
      atomic<unsigned> parsedCount_;
      mutex mu_;

      ParserState()
      {
         blockHeight_.store(0);
         unknownErrors_.store(0);
         unsupportedSigHash_.store(0);
         unresolvedHashes_.store(0);
         parsedCount_.store(0);
      }
   };

   TIMER_START("10blocks");

   //dont preload, prefetch
   BlockDataLoader bdl(blockFiles_, BlockOffset());

   auto stateStruct = make_shared<ParserState>();

   auto verifyBlockTx = [&bdl, this, stateStruct](void)->void
   {
      auto getUtxoMap = [&bdl, stateStruct, this]
         (shared_ptr<BCTX> txn)->Armory::Signing::TransactionVerifier::utxoMap
      {
         Armory::Signing::TransactionVerifier::utxoMap utxomap;
         for (auto& txin : txn->txins_)
         {
            //get output hash
            BinaryDataRef hashref(txn->data_ + txin.first, 32);
            auto outputID = (uint32_t*)(txn->data_ + txin.first + 32);

            //resolve hash
            StoredTxHints sths;
            if (!db_->getStoredTxHints(sths, hashref.getSliceRef(0, 4)))
            {
               stateStruct->unresolvedHashes_.fetch_add(1, memory_order_relaxed);
               throw UnresolvedHashException();
            }

            bool foundtx = false;
            for (auto& outpointkey : sths.dbKeyList_)
            {
               if (outpointkey.getSize() == 0)
                  continue;

               //parse key
               auto blockkey = outpointkey.getSliceRef(0, 4);
               auto opDup = (uint8_t*)(outpointkey.getPtr() + 3);
               if (*opDup != 0xFF)
                  continue;

               auto blockID = DBUtils::hgtxToHeight(blockkey);
               shared_ptr<BlockHeader> bhPtr;
               try
               {
                  bhPtr = blockchain_->getHeaderById(blockID);
               }
               catch (exception&)
               {
                  continue;
               }

               //get tx index
               BinaryRefReader brr(outpointkey);
               brr.advance(4);
               auto txid = brr.get_uint16_t(BE);

               //get block data
               auto blockFileNum = bhPtr->getBlockFileNum();
               auto fileCopy = bdl.getNextCopy();

               auto getID = [bhPtr](const BinaryData&)->unsigned int
               {
                  return bhPtr->getThisID();
               };

               auto bdata = BlockData::deserialize(
                  fileMap->data() + bhPtr->getOffset(),
                  bhPtr->getBlockSize(),
                  bhPtr, getID, BlockData::CheckHashes::NoChecks);

               const auto& txns = bdata->getTxns();
               if (txid > txns.size())
                  continue;

               //check hash
               const auto& _txn = txns[txid];
               const auto& txhash = _txn->getHash();
               if (hashref != txhash.getRef())
                  continue;

               //grab output
               auto txoutcount = _txn->txouts_.size();
               if (*outputID > txoutcount)
                  break;

               BinaryDataRef output(_txn->data_ + _txn->txouts_[*outputID].first,
                  _txn->txouts_[*outputID].second);

               UTXO utxo;
               utxo.unserializeRaw(output);
               auto& idmap = utxomap[hashref];
               idmap[*outputID] = move(utxo);

               foundtx = true;
               break;
            }

            if (!foundtx)
               throw UnresolvedHashException();
         }

         return utxomap;
      };

      unsigned thisHeight = 0;
      unsigned failedVerifications = 0;

      auto&& hintdbtx = db_->beginTransaction(TXHINTS, LMDB::ReadOnly);

      while (thisHeight < blockchain_->top()->getBlockHeight())
      {
         //grab blockheight
         thisHeight = stateStruct->blockHeight_.fetch_add(1, memory_order_relaxed);
         auto blockheader = blockchain_->getHeaderByHeight(thisHeight, 0xFF);
         auto fileMap = getFileMap(blockheader->getBlockFileNum());

         auto getID = [blockheader](const BinaryData&)->unsigned int
         {
            return blockheader->getThisID();
         };

         auto bdata = BlockData::deserialize(
            fileMap->data() + blockheader->getOffset(),
            blockheader->getBlockSize(),
            blockheader, getID, BlockData::CheckHashes::NoChecks);

         const auto& txns = bdata->getTxns();
         for (unsigned i = 1; i < txns.size(); i++)
         {
            const auto& txn = txns[i];

            try
            {
               //gather utxos
               auto utxomap = getUtxoMap(txn);

               //verify tx
               Armory::Signing::TransactionVerifier txV(*txn, utxomap);
               auto flags = txV.getFlags();

               if (blockheader->getTimestamp() > P2SH_TIMESTAMP)
                  flags |= SCRIPT_VERIFY_P2SH;

               if (txn->usesWitness_)
                  flags |= SCRIPT_VERIFY_SEGWIT;

               txV.setFlags(flags);

               if (txV.verify())
                  stateStruct->parsedCount_.fetch_add(1, memory_order_relaxed);
               else
                  ++failedVerifications;
            }
            catch (Armory::Signing::UnsupportedSigHashTypeException&)
            {
               stateStruct->unsupportedSigHash_.fetch_add(1, memory_order_relaxed);
            }
            catch (UnresolvedHashException&)
            {
               stateStruct->unresolvedHashes_.fetch_add(1, memory_order_relaxed);
            }
            catch (const exception& e)
            {
               unique_lock<mutex> lock(stateStruct->mu_);
               LOGERR << "+++ error at #" << thisHeight << ":" << i;
               LOGERR << "+++ strerr: " << e.what();
               stateStruct->unknownErrors_.fetch_add(1, memory_order_relaxed);
            }
         }

         if (thisHeight % 1000 == 0)
         {
            unique_lock<mutex> lock(stateStruct->mu_);
            auto tE = TIMER_READ_SEC("10blocks");
            TIMER_RESTART("10blocks");

            LOGINFO << "=== time elapsed: " << tE << " ===";

            LOGINFO << "current block: " << thisHeight;
            LOGINFO << "--- verified " << 
               stateStruct->parsedCount_.load(memory_order_relaxed) << " transactions";

            LOGINFO << "--- *encountered " <<
               stateStruct->unsupportedSigHash_.load(memory_order_relaxed) <<
               " unknown sighashes";

            LOGINFO << "--- *encountered " <<
               stateStruct->unresolvedHashes_.load(memory_order_relaxed) <<
               " unresolved hashes";

            LOGINFO << "--- ***encountered " <<
               stateStruct->unknownErrors_.load(memory_order_relaxed) <<
               " unknown errors";
         }
      }
   };

   vector<thread> parserThrVec;
   for (unsigned i = 1; i < DBSettings::threadCount(); i++)
      parserThrVec.push_back(thread(verifyBlockTx));

   verifyBlockTx();

   checkedTransactions_ = stateStruct->parsedCount_.load(memory_order_relaxed);

   for (auto& thr : parserThrVec)
      if (thr.joinable())
         thr.join();

   if (stateStruct->unresolvedHashes_.load(memory_order_relaxed) > 0)
      throw runtime_error("checkChain failed with unresolved hash errors");

   if (stateStruct->unsupportedSigHash_.load(memory_order_relaxed) > 0)
      throw runtime_error("checkChain failed with unsupported sig hash errors");

   if (stateStruct->unknownErrors_.load(memory_order_relaxed) > 0)
      throw runtime_error("checkChain failed with unknown errors");

   LOGINFO << "Done checking chain";
}

/////////////////////////////////////////////////////////////////////////////
void DatabaseBuilder::verifyTxFilters()
{
   if (DBSettings::getDbType() != ARMORY_DB_FULL)
      return;

   LOGINFO << "verifying txfilters integrity";

   atomic<unsigned> fileCounter;
   fileCounter.store(0, memory_order_relaxed);

   mutex resultMutex;
   set<unsigned> damagedFilters;

   auto&& file_id_map = blockchain_->mapIDsPerBlockFile();

   auto checkThr = [&](void)->void
   {
      auto&& tx = db_->beginTransaction(TXFILTERS, LMDB::ReadOnly);

      set<unsigned> mismatchedFilters;

      while (1)
      {
         unsigned mismatchCount = 0;
         unsigned fileNum = fileCounter.fetch_add(1, memory_order_relaxed);
         auto file_id_iter = file_id_map.find(fileNum);
         if (file_id_iter == file_id_map.end())
         {
            if (fileNum < blockFiles_.fileCount())
            {
               LOGINFO << "no recorded block headers in file #" << fileNum;
               LOGINFO << "skipping";
               continue;
            }

            if (mismatchedFilters.size() > 0)
            {
               auto lock = unique_lock<mutex>(resultMutex);
               damagedFilters.insert(
                  mismatchedFilters.begin(), mismatchedFilters.end());
            }

            return;
         }

         auto& idset = file_id_iter->second;

         try
         {
            auto pool = db_->getFilterPoolRefForFileNum(fileNum);
            auto filters = pool.getFilterPoolPtr();

            auto match_count = 0;
            for (const auto& filter : filters)
            {
               //check filter blockid is for this block file
               auto id_iter = idset.find(filter.getBlockKey());
               if (id_iter != idset.end())
                  ++match_count;
            }

            mismatchCount = idset.size() - match_count;

            if (mismatchCount > 0)
            {
               mismatchedFilters.insert(fileNum);
               LOGWARN << mismatchCount << " mismatches in txfilter for file #" << fileNum;
            }
         }
         catch (const runtime_error&)
         {
            mismatchedFilters.insert(fileNum);
            LOGWARN << "couldnt get filter pool for file: " << fileNum;
         }
      }
   };

   vector<thread> thrs;
   for (unsigned i = 1; i < DBSettings::threadCount(); i++)
      thrs.push_back(thread(checkThr));
   checkThr();

   for (auto& thr : thrs)
      if (thr.joinable())
         thr.join();
   
   if (damagedFilters.size() == 0)
   {
      LOGINFO << "done checking txfilters";
      return;
   }

   LOGWARN << damagedFilters.size() << " damaged filters, repairing";
   repairTxFilters(damagedFilters);
}

/////////////////////////////////////////////////////////////////////////////
void DatabaseBuilder::repairTxFilters(const set<unsigned>& badFilters)
{
   {
      LOGINFO << "clearing damaged filters";

      auto&& tx = db_->beginTransaction(TXFILTERS, LMDB::ReadWrite);

      for (auto& filter : badFilters)
      {
         auto&& dbkey = DBUtils::getFilterPoolKey(filter);
         db_->deleteValue(TXFILTERS, dbkey);
      }
   }

   //no preload nor prefetch
   BlockDataLoader bdl(blockFiles_.folderPath());

   vector<unsigned> idVec;
   for (auto& id : badFilters)
      idVec.push_back(id);

   atomic<unsigned> counter;
   counter.store(0, memory_order_relaxed);

   auto fixFilterThr = [&](void)->void
   {
      while (counter.load(memory_order_relaxed) < badFilters.size())
      {
         auto counterID = counter.fetch_add(1, memory_order_relaxed);
         auto fileID = *(idVec.cbegin() + counterID);
         auto&& blockfilemapptr = bdl.get(fileID);

         this->reprocessTxFilter(blockfilemapptr, fileID);
      }
   };

   vector<thread> thrs;
   for (unsigned i = 1; i < DBSettings::threadCount(); i++)
      thrs.push_back(thread(fixFilterThr));
   fixFilterThr();

   for (auto& thr : thrs)
      if (thr.joinable())
         thr.join();
}

/////////////////////////////////////////////////////////////////////////////
void DatabaseBuilder::reprocessTxFilter(
   shared_ptr<BlockDataFileMap> blockfilemappointer, unsigned fileID)
{
   auto ptr = blockfilemappointer->getPtr();

   //ptr is null if we're out of block files
   if (ptr == nullptr)
      return;

   map<uint32_t, shared_ptr<BlockData>> bdMap;

   auto getID = [&](const BinaryData& heahder_hash)->uint32_t
   {
      try
      {
         auto header = blockchain_->getHeaderByHash(heahder_hash);
         return header->getThisID();
      }
      catch (...)
      {
         LOGERR << "no header in db matches this hash!";
         return UINT32_MAX;
      }
   };

   auto tallyBlocks =
      [&](const uint8_t* data, size_t size, size_t offset)->bool
   {
      //deser full block, check merkle
      shared_ptr<BlockData> bd;
      BinaryRefReader brr(data, size);

      try {
         bd = BlockData::deserialize(data, size, nullptr,
            getID, true, false);
      } catch (const BlockDeserializingException &e) {
         LOGERR << "block deser except: " << e.what();
         LOGERR << "block fileID: " << fileID;
         return false;
      } catch (const exception &e) {
         LOGERR << "exception: " << e.what();
         return false;
      } catch (...) {
         //deser failed, ignore this block
         LOGERR << "unknown block deser exception";
         return false;
      }

      //block is valid, add to container
      bd->setFileID(fileID);
      bd->setOffset(offset);
      bdMap.emplace(bd->uniqueID(), move(bd));
      return true;
   };
   parseBlockFile(bdl,0, tallyBlocks);

   {
      //delete existing txfilter
      auto tx = db_->beginTransaction(TXFILTERS, LMDB::ReadWrite);
      auto dbkey = DBUtils::getFilterPoolKey(fileID);
      db_->deleteValue(TXFILTERS, dbkey);

      //tally all block filters
      std::map<uint32_t, BlockHashVector> allFilters;
      for (const auto& bd_pair : bdMap) {
         allFilters.emplace(
            bd_pair.first, bd_pair.second->getTxFilter());
      }

      TxFilterPool pool(allFilters);

      //commit to db
      db_->putFilterPoolForFileNum(fileID, pool);
   }

   LOGINFO << "fixed txfilter for file #" << fileID;
}
#endif

/////////////////////////////////////////////////////////////////////////////
void DatabaseBuilder::cycleDatabases()
{
   db_->closeDatabases();
   db_->openDatabases(Pathing::dbDir());
}

/////////////////////////////////////////////////////////////////////////////
void DatabaseBuilder::checkTxHintsIntegrity()
{
   BlockDataLoader bdl(blockFiles_, BlockOffset{});
   unsigned threadcount = min(size_t(DBSettings::threadCount()), bdl.size());

   auto parserThread = [this, &bdl]()
   {
      while (true) {
         auto fileCopy = bdl.getNextCopy();
         if (!fileCopy.isValid()) {
            return;
         }

         if (fileCopy.fileID % 100 == 0) {
            LOGINFO << "checking txhints for file " << fileCopy.fileID;
         }

         //tally all blocks in file
         std::list<std::shared_ptr<BlockData>> bdList;
         parseBlockFile(fileCopy,
            [&bdList](const uint8_t* data, size_t size, size_t)->bool
            {
               try {
                  auto bd = BlockData::deserialize(
                     data, size, nullptr,
                     nullptr, BlockData::CheckHashes::FullHints);
                  bdList.emplace_back(bd);
                  return true;
               } catch (...) {
                  return false;
               }
            }
         );

         //check hashes can be resolved via tx hints db
         int missedCount = 0;
         auto dbtx = db_->beginTransaction(TXHINTS, LMDB::ReadOnly);
         for (const auto& blockData : bdList) {
            //skip blocks not in the main chain
            auto headerPtr = blockchain_->getHeaderByHash(blockData->getHash());
            if (headerPtr == nullptr || !headerPtr->isMainBranch()) {
               continue;
            }

            const auto& txns = blockData->getTxns();
            for (const auto& txn : txns) {
               auto hash4 = txn->getHash().getSliceRef(0, 4);
               BinaryRefReader brrHints = db_->getValueRef(
                  TXHINTS, DB_PREFIX_TXHINTS, hash4);

               uint32_t valSize = brrHints.getSize();
               if (valSize < 6) {
                  ++missedCount;
                  return;
               }

               bool hit = false;
               uint32_t numHints = (uint32_t)brrHints.get_var_int();
               for (uint32_t i = 0; i < numHints; i++) {
                  BinaryDataRef hint = brrHints.get_BinaryDataRef(6);

                  //check this key is on the main branch
                  auto hintRef = hint.getSliceRef(0, 4);
                  auto blockId = DBUtils::hgtxToHeight(hintRef);
                  if (blockId == headerPtr->getThisID()) {
                     hit = true;
                     break;
                  }
               }

               if (!hit) {
                  ++missedCount;
               }
            }
         }

         if (missedCount != 0) {
            LOGERR << "missed " << missedCount << " hashes" <<
               " in file " << fileCopy.fileID;
         }
      }
   };

   std::vector<std::thread> threads;
   LOGINFO << "checking txhints for " << bdl.size() <<
      " files on " << threadcount << " threads";
   threads.reserve(threadcount);
   for (unsigned i=1; i<threadcount; i++) {
      threads.emplace_back(thread(parserThread));
   }
   parserThread();

   for (auto& thr : threads) {
      if (thr.joinable()) {
         thr.join();
      }
   }

   LOGINFO << "done checking txhints";
}