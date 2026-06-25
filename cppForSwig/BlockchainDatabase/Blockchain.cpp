////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cstring>
#include <unordered_set>

#include "Blockchain.h"
#include <Utils/BtcUtils.h>
#include <Utils/UniversalTimer.h>
#include <Utils/DBUtils.h>

#include "StoredBlockObj.h"
#include "lmdb_wrapper.h"

using namespace Armory;

namespace {
   //helper containers used in Blockchain::traceDownChain
   using Hash32USet = std::unordered_set<Hash32, Hash32::Hasher, Hash32::IsEqual>;
   std::map<Hash32, Hash32USet> orphans;
}

////////////////////////////////////////////////////////////////////////////////
// Blockchain
Blockchain::Blockchain(const BinaryData &genesisHash)
   : genesisHash_(genesisHash), topBlockOffset_{0, 0}
{}

void Blockchain::clear()
{
   newlyParsedHeaders_.clear();
   headersByHeight_.clear();
   headersById_.clear();
   headerSet_.clear();

   Hash32 genHash{genesisHash_};
   Hash32 emptyPrev{};
   Hash32 emptyMerkle{};
   auto genesisBlock = std::shared_ptr<BlockHeader>(new BlockHeader(
      genHash, emptyPrev, emptyMerkle, 1, 0, 0));
   genesisBlock->setUniqueID(1);
   headerSet_.emplace(genesisBlock);

   //block ids start at 1, genesis block is assigned id 1
   highestBlockID_.store(2, std::memory_order_relaxed);
   invalidBlockIds_.clear();
}

////////
HeaderPtr Blockchain::top() const
{
   auto ptr = std::atomic_load(&topBlockPtr_);
   return ptr;
}

HeaderPtr Blockchain::getGenesisHeader() const
{
   //NOTE: caller is responsible for holding the lock
   auto iter = headerSet_.find(genesisHash_);
   if (iter == headerSet_.end()) {
      throw std::runtime_error("missing genesis block header");
   }
   return *iter;
}

////////
const Blockchain::HeaderSet& Blockchain::getHeaderSet() const
{
   std::unique_lock<std::mutex> lock(mu_);
   return headerSet_;
}

const HeaderPtr Blockchain::getHeaderByHeight(unsigned height) const
{
   /*
   Returns header for height.
   Passing a dupId above 0x7F will return the main chain header for this height.
   Passing a dupId for a forked block will throw.
   */

   std::unique_lock<std::mutex> lock(mu_);
   if (height >= headersByHeight_.size()) {
      throw std::range_error(
         "Cannot get block at height " + std::to_string(height)
      );
   }
   return headersByHeight_[height];
}

HeaderPtr Blockchain::getHeaderByHash(const BinaryData& blkHash) const
{
   std::unique_lock<std::mutex> lock(mu_);
   auto iter = headerSet_.find(blkHash);
   if (iter == headerSet_.end()) {
      throw std::range_error(
         "cannot find header with hash " + blkHash.toHexStr(true));
   }
   return *iter;
}

HeaderPtr Blockchain::getHeaderByHash(const Hash32& blkHash) const
{
   std::unique_lock<std::mutex> lock(mu_);
   auto iter = headerSet_.find(blkHash);
   if (iter == headerSet_.end()) {
      throw std::range_error(
         "cannot find header with hash " + blkHash.toHexStr(true));
   }
   return *iter;
}

HeaderPtr Blockchain::getHeaderById(Types::BlockId id) const
{
   std::unique_lock<std::mutex> lock(mu_);
   if (id > highestBlockID_.load(std::memory_order_relaxed)) {
      LOGERR << "block id " << id << " is too big";
      throw std::range_error("block id overflow");
   }
   auto header = headersById_[id];
   if (header == nullptr) {
      LOGERR << "cannot find block for id: " << id;
      throw std::range_error("Cannot find block by id");
   }
   return header;
}

////////
ReorganizationState Blockchain::organize(bool force, bool verbose)
{
   ReorganizationState st;
   st.prevTop = top();
   st.reorgBranchPoint = organizeChain(force, verbose);
   st.prevTopStillValid = (st.reorgBranchPoint == nullptr);
   st.hasNewTop = (st.prevTop != top());
   st.newTop = top();

   if (!st.prevTopStillValid) {
      std::unique_lock<std::mutex> lock(mu_);
      auto header = st.prevTop;
      while (header->getUniqueID() != st.reorgBranchPoint->getUniqueID()) {
         st.invalidatedBlockIds.emplace_back(header->getUniqueID());
         auto headerIter = headerSet_.find(header->prevHash_);
         if (headerIter == headerSet_.end()) {
            break;
         }
         header = *headerIter;
      }

      header = st.newTop;
      while (header->getUniqueID() != st.reorgBranchPoint->getUniqueID()) {
         st.newMainBranchIds.emplace_back(header->getUniqueID());
         auto headerIter = headerSet_.find(header->prevHash_);
         if (headerIter == headerSet_.end()) {
            break;
         }
         header = *headerIter;
      }
   }
   return st;
}

// Returns nullptr if the new top block is a direct follower of
// the previous top. Returns the branch point if we had to reorg
// TODO: Figure out if there is an elegant way to deal with a forked
//   blockchain containing two equal-length chains
HeaderPtr Blockchain::organizeChain(
   bool forceRebuild, bool verbose)
{
   std::unique_lock<std::mutex> lock(mu_);
   if (forceRebuildFlag_) {
      forceRebuild = true;
      forceRebuildFlag_ = false;
      LOGINFO << "chain was flagged for a forced rebuild";
   }

   if (verbose) {
      TIMER_START("orgChain");
      LOGINFO << "Organizing chain " << (forceRebuild ? "w/ rebuild" : "");
   }

   // If rebuild, we zero out any original organization data and do a
   // rebuild of the chain from scratch.  This will need to be done in
   // the event that our first call to organizeChain returns false, which
   // means part of blockchain that was previously valid, has become
   // invalid.  Rather than get fancy, just rebuild all which takes less
   // than a second, anyway.

   if (forceRebuild) {
      for (auto& header : headerSet_) {
         header->difficultySum_  = -1;
         header->blockHeight_ = UINT32_MAX;
         header->isFinishedCalc_ = false;
         header->nextHash_ = nullptr;
         header->isMainBranch_ = false;
      }
      topBlockPtr_.store(nullptr);
   }

   // If this is the first run, the topBlock is the genesis block
   if (topBlockPtr_.load() == nullptr) {
      auto genBlock = getGenesisHeader();
      if (!genBlock->getMerkleRoot().valid()) {
         return nullptr;
      }
      genBlock->blockHeight_ = 0;
      genBlock->difficultySum_ = 1.0;
      genBlock->isMainBranch_ = true;
      genBlock->isOrphan_ = false;
      genBlock->isFinishedCalc_ = true;
      topBlockPtr_.store(genBlock);
   }
   const auto prevTopBlock = top();
   auto newTopBlock = topBlockPtr_.load();
   double maxDiffSum = prevTopBlock->getDifficultySum();

   //prepare helper containers
   orphans.clear();

   // Iterate over all blocks, track the maximum difficulty-sum block
   for (auto& header : headerSet_) {
      // *** Walk down the chain following prevHash fields, until
      //     you find a "solved" block. Then walk back up and
      //     fill in the difficulty-sum values (do not set next-
      //     hash ptrs, as we don't know if this is the main branch)
      //     Method returns instantly if block is already "solved"
      if (header->difficultySum_ <= 0.0) {
         traceChainDown(header);
      }

      if (header->isOrphan_) {
         // disregard this block
         continue;
      } else if (header->difficultySum_ > maxDiffSum) {
         // Determine if this is the top block.  If it's the same diffsum
         // as the prev top block, don't do anything
         maxDiffSum  = header->difficultySum_;
         newTopBlock = header;
      }
   }

   //report long orphaned chains
   for (const auto& orphanChain : orphans) {
      if (orphanChain.second.size() >= 144) {
         auto headerIter = headerSet_.find(orphanChain.first);
         if (headerIter == headerSet_.end()) {
            LOGERR << "Could not find first orphan by hash! This is a fatal error!";
            throw std::runtime_error("could not find orphan");
         }

         auto headerPtr = *headerIter;
         LOGWARN << "Found a long orphan chain!";
         LOGWARN << "  file: " << headerPtr->getBlockFileNum();
         LOGWARN << "  first header hash  : " << headerPtr->getThisHash().toHexStr(true);
         LOGWARN << "  missing header hash: " << headerPtr->getPrevHash().toHexStr(true);
         LOGWARN << "  orphan chain length: " << orphanChain.second.size();
      }

      //reset finishedCalc flag on all orphans
      for (const auto& headerHash : orphanChain.second) {
         auto headerIter = headerSet_.find(headerHash);
         if (headerIter == headerSet_.end()) {
            LOGERR << "Could not find an orphan by hash! This is a fatal error!";
            throw std::runtime_error("could not find orphan");
         }
         (*headerIter)->isFinishedCalc_ = false;
      }
   }

   // Walk down the list one more time, set nextHash fields
   // Also set headersByHeight_;
   bool prevChainStillValid = (newTopBlock == prevTopBlock);
   newTopBlock->nextHash_ = nullptr;
   auto thisHeaderPtr = newTopBlock;
   if (headersByHeight_.size() <= newTopBlock->getBlockHeight()) {
      if (headersByHeight_.capacity() <= newTopBlock->getBlockHeight()) {
         headersByHeight_.reserve(newTopBlock->getBlockHeight() + 100);
      }
      headersByHeight_.resize(newTopBlock->getBlockHeight() + 1);
   }

   while (!thisHeaderPtr->isFinishedCalc_) {
      thisHeaderPtr->isFinishedCalc_ = true;
      thisHeaderPtr->isMainBranch_   = true;
      thisHeaderPtr->isOrphan_       = false;
      headersByHeight_[thisHeaderPtr->getBlockHeight()] = thisHeaderPtr;
      invalidBlockIds_.erase(thisHeaderPtr->getUniqueID());

      auto childIter = headerSet_.find(thisHeaderPtr->getPrevHash());
      if (childIter == headerSet_.end()) {
         LOGERR << "failed to get prev header by hash";
         throw std::runtime_error("failed to get prev header by hash");
      }

      (*childIter)->nextHash_ = &thisHeaderPtr->getThisHash();
      thisHeaderPtr = *childIter;
      if (thisHeaderPtr == prevTopBlock) {
         prevChainStillValid = true;
      }
   }

   // Last header in the loop didn't get added (the genesis block on first run)
   thisHeaderPtr->isMainBranch_ = true;
   headersByHeight_[thisHeaderPtr->getBlockHeight()] = thisHeaderPtr;
   std::atomic_store(&topBlockPtr_, newTopBlock);

   //cleanup helper containers
   orphans.clear();

   if (!prevChainStillValid) {
      LOGWARN << "Reorg detected!";

      //reset calculation flag on lesser chain
      auto prevHeadPtr = prevTopBlock;
      while (prevHeadPtr->thisHash_ != thisHeaderPtr->thisHash_) {
         prevHeadPtr->isFinishedCalc_ = false;
         prevHeadPtr->difficultySum_ = -1.0;
         prevHeadPtr->isMainBranch_ = false;
         prevHeadPtr->isOrphan_ = true;
         invalidBlockIds_.emplace(prevHeadPtr->getUniqueID());
         prevHeadPtr = *headerSet_.find(prevHeadPtr->prevHash_);
      }
      return thisHeaderPtr;
   }

   if (verbose) {
      TIMER_STOP("orgChain");
      auto duration = TIMER_READ_SEC("orgChain");
      LOGINFO << "Organized chain in " << duration << "s";
   }
   return nullptr;
}

/////////////////////////////////////////////////////////////////////////////
// Start from a node, trace down to the highest solved block, accumulate
// difficulties and difficultySum values. Return the difficultySum of
// this block.
void Blockchain::traceChainDown(std::shared_ptr<BlockHeader> bhpStart)
{
   /*
   NOTE: caller is responsible for locking the mutex
   TODO: check difficulty target matches for each block
   */

   // Walk down the chain of prevHash_ values, until we find a block
   // that has a definitive difficultySum value (i.e. >0).
   auto thisPtr = bhpStart;
   while (thisPtr->difficultySum_ < 0.0) {
      auto iter = headerSet_.find(thisPtr->getPrevHash());
      if (iter != headerSet_.end()) {
         auto hPtr = *iter;
         hPtr->nextPtr_ = thisPtr;
         thisPtr = hPtr;
      } else {
         // this block is an orphan, possibly caused by a HeadersFirst
         // blockchain. Nothing to do about that
         break;
      }
   }

   // Now we have a stack of difficulties and pointers. Walk back up
   // (by pointer) and accumulate the difficulty values
   if (!thisPtr->isOrphan_) {
      while (thisPtr->nextPtr_ != nullptr) {
         auto hPtr = thisPtr->nextPtr_;
         hPtr->blockHeight_ = thisPtr->blockHeight_ + 1;
         hPtr->difficultySum_ = thisPtr->difficultySum_ + hPtr->difficultyDbl_;
         if (hPtr->isOrphan_) {
            //if this block previously was orphaned, reset the finished calc flag
            hPtr->isFinishedCalc_ = false;
         }
         hPtr->isOrphan_ = false;
         thisPtr = hPtr;
      }
   } else {
      //look for an orphan chain this new chain connects to
      auto orphanIter = orphans.begin();
      while (orphanIter != orphans.end()) {
         auto parent = orphanIter->second.find(thisPtr->getPrevHash());
         if (parent != orphanIter->second.end()) {
            break;
         }
         ++orphanIter;
      }

      //there was no chain for this orphan, start a new one
      if (orphanIter == orphans.end()) {
         orphanIter = orphans.emplace(
            thisPtr->getThisHash(), Hash32USet{}).first;
      }

      //mark all blocks in that chain as orphans and track them
      while (true) {
         thisPtr->isOrphan_ = true;
         thisPtr->isFinishedCalc_ = true;
         invalidBlockIds_.emplace(thisPtr->getUniqueID());
         orphanIter->second.emplace(thisPtr->getThisHash());

         if (thisPtr->nextPtr_ == nullptr) {
            break;
         }
         thisPtr = thisPtr->nextPtr_;
      }
   }
}

void Blockchain::putNewHeaders(LMDBBlockDatabase *db)
{
   std::unique_lock<std::mutex> lock(mu_);
   if (newlyParsedHeaders_.empty()) {
      return;
   }

   //create transaction here to batch the write
   auto tx = db->beginTransaction(DB_SELECT::HEADERS, LMDB::Mode::ReadWrite);
   for (const auto& header : newlyParsedHeaders_) {
      StoredHeader sbh;
      sbh.createFromBlockHeader(*header);
      //assume the header is valid for now
      sbh.merkleValid = true;
      db->putBareHeader(sbh);
   }

   //update SDBI, keep within the batch transaction
   auto sdbiH = db->getStoredDBInfo(DB_SELECT::HEADERS, 0xFFFF);
   auto topBlock = topBlockPtr_.load();
   if (topBlock == nullptr) {
      LOGINFO << "No known top block, didn't update SDBI";
      return;
   }

   if (topBlock->thisHash_ != sdbiH.topScannedBlkHash) {
      sdbiH.topScannedBlkHash = topBlock->thisHash_;
      db->putStoredDBInfo(DB_SELECT::HEADERS, sdbiH, 0xFFFF);
   }

   //once commited to the DB, they aren't considered new anymore,
   //so clean up the container
   newlyParsedHeaders_.clear();
}

Types::BlockId Blockchain::getNewUniqueID()
{
   return highestBlockID_.fetch_add(1, std::memory_order_relaxed);
}

////////
void Blockchain::loadHeadersFromDB(
   LMDBBlockDatabase* db, const std::function<void(size_t)>& prog)
{
   std::unique_lock<std::mutex> lock(mu_);
   if (!headerSet_.empty()) {
      throw std::runtime_error("blockchain object is already initialized");
   }

   Types::BlockId highestBlockID = 1;
   size_t count;
   auto callback = [this, &prog, &highestBlockID, &count](HeaderPtr hPtr)
   {
      if (!hPtr->isMerkleValid()) {
         return;
      }

      if (headerSet_.bucket_count() < headerSet_.size() + 1) {
         headerSet_.reserve(headerSet_.size() + 10000);
      }
      if (headersById_.size() < hPtr->getUniqueID() + 1) {
         headersById_.resize(hPtr->getUniqueID() + 10000);
      }

      auto iter = headerSet_.find(hPtr);
      if (iter != headerSet_.end()) {
         throw std::runtime_error("header hash collision");
      }
      headerSet_.emplace(hPtr);

      auto thisHeader = headersById_[hPtr->getUniqueID()];
      if (thisHeader != nullptr && thisHeader->isMerkleValid()) {
         throw std::runtime_error("header ID collision");
      }
      headersById_[hPtr->getUniqueID()] = hPtr;
      highestBlockID = std::max(highestBlockID, hPtr->getUniqueID());
      BlockOffset bo{hPtr->getBlockFileNum(), hPtr->getOffset() + hPtr->getBlockSize()};
      topBlockOffset_ = std::max(topBlockOffset_, bo);

      if (prog && count++ % 50000 == 0) {
         prog(count);
      }
   };

   LOGINFO << "Reading headers from db";
   db->readAllHeaders(callback);
   LOGINFO << "found " << headerSet_.size() << " headers in db";

   if (headerSet_.empty()) {
      clear();
   } else {
      try {
         getGenesisHeader();
      } catch (const std::exception&) {
         throw std::runtime_error("missing genesis header in db");
      }
      highestBlockID_.store(highestBlockID + 1);
   }
}

uint32_t Blockchain::stageNewHeaders(
   const std::vector<std::shared_ptr<BlockHeader>>& newHeaders)
{
   std::unique_lock<std::mutex> lock(mu_);
   uint32_t count = 0;
   if (headersById_.size() < headersById_.size() + newHeaders.size()) {
      headersById_.resize(headersById_.size() + newHeaders.size() + 150);
   }
   for (auto newHeader : newHeaders) {
      auto iter = headerSet_.find(newHeader);
      if (iter != headerSet_.end()) {
         auto thisHeader = *iter;
         if (thisHeader->getBlockFileNum() == newHeader->getBlockFileNum() &&
            thisHeader->getOffset() == newHeader->getOffset()) {
            continue;
         }

         //header will be replaced, carry the uniqueID over
         newHeader->setUniqueID(thisHeader->getUniqueID());
         headerSet_.erase(iter);
      }

      //assign uniqueID if necessary
      if (!Types::isBlockIdValid(newHeader->getUniqueID())) {
         newHeader->setUniqueID(getNewUniqueID());
      }
      headerSet_.emplace(newHeader);
      headersById_[newHeader->getUniqueID()] = newHeader;
      newlyParsedHeaders_.emplace_back(newHeader);

      BlockOffset bo{
         newHeader->getBlockFileNum(),
         newHeader->getOffset() + newHeader->getBlockSize()};
      topBlockOffset_ = std::max(topBlockOffset_, bo);
      ++count;
   }
   return count;
}

////////
std::map<Types::FileId, std::set<Types::BlockId>> Blockchain::mapIDsPerBlockFile() const
{
   std::unique_lock<std::mutex> lock(mu_);
   std::map<Types::FileId, std::set<Types::BlockId>> resultMap;
   for (const auto& header : headersById_) {
      if (header == nullptr) {
         continue;
      }
      auto& result_set = resultMap[header->blkFileNum_];
      result_set.emplace(header->uniqueID_);
   }
   return resultMap;
}

////////
void Blockchain::flagBlockHeader(std::shared_ptr<BlockHeader> header,
   LMDBBlockDatabase *db)
{
   if (db->getOrSetFlaggedBlockFile(header->getBlockFileNum())) {
      LOGINFO << "flagging block file " << header->getBlockFileNum() <<
         " for reparsing";
   }
}

void Blockchain::flagInvalidBlocks(LMDBBlockDatabase* db,
   const std::set<Types::BlockId>& invalidIDs)
{
   if (invalidIDs.empty()) {
      return;
   }
   std::unique_lock<std::mutex> lock(mu_);

   auto tx = db->beginTransaction(DB_SELECT::HEADERS, LMDB::Mode::ReadWrite);
   for (const auto& blockID : invalidIDs) {
      //sanity checks
      if (blockID >= headersById_.size()) {
         throw std::runtime_error("blockID overflow");
      }
      auto header = headersById_[blockID];
      if (header == nullptr) {
         throw std::runtime_error("this ID does not have a header");
      } else if (header->isMerkleValid()) {
         throw std::runtime_error("this header has not failed a merkle check");
      }

      //grab header from db, we need the raw data
      auto ldbIter = tx->getIterator();
      BinaryWriter bwKey(4);
      bwKey.put_uint32_t(blockID, BE);
      if (!ldbIter.seekToExact(bwKey.getDataRef())) {
         throw std::runtime_error("no db entry for this header");
      }
      header->setRawData(
         ldbIter.getValueReader().get_BinaryData(HEADER_SIZE));

      StoredHeader sbh;
      sbh.createFromBlockHeader(*header);
      db->putBareHeader(sbh);
   }
}

////////
BlockOffset Blockchain::getTopBlockOffset() const
{
   return topBlockOffset_;
}

const std::vector<HeaderPtr>& Blockchain::headersById() const
{
   std::unique_lock<std::mutex> lock(mu_);
   return headersById_;
}

const std::set<Types::BlockId>& Blockchain::getInvalidBlockIds() const
{
   std::unique_lock<std::mutex> lock(mu_);
   return invalidBlockIds_;
}
