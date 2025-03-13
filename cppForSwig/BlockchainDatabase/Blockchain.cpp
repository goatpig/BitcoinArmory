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

#include "Blockchain.h"
#include "util.h"
#include "BlockDataMap.h"
#include <unordered_set>

using namespace std;

const BinaryData Blockchain::topIdKey_ = READHEX("544f504944"); //TOPID in hex

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
// Start Blockchain methods
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

namespace {
   //helper containers used in Blockchain::traceDownChain
   std::map<BinaryDataRef, std::unordered_set<BinaryDataRef>> orphans;
}

Blockchain::Blockchain(const HashString &genesisHash)
   : genesisHash_(genesisHash)
{
   clear();
}

void Blockchain::clear()
{
   newlyParsedBlocks_.clear();
   headersByHeight_.clear();
   headersById_.clear();
   headerMap_.clear();

   pair<BinaryData, shared_ptr<BlockHeader>> genesisPair;
   genesisPair.first = genesisHash_;
   genesisPair.second = make_shared<BlockHeader>();
   atomic_store(&topBlockPtr_, genesisPair.second);
   headerMap_.insert(genesisPair);
   topBlockId_ = 0;

   topID_.store(1, memory_order_relaxed);
}

Blockchain::ReorganizationState Blockchain::organize(bool verbose)
{
   ReorganizationState st;
   st.prevTop_ = top();
   st.reorgBranchPoint_ = organizeChain(false, verbose);
   st.prevTopStillValid_ = (st.reorgBranchPoint_ == nullptr);
   st.hasNewTop_ = (st.prevTop_ != top());
   st.newTop_ = top();
   return st;
}

Blockchain::ReorganizationState Blockchain::forceOrganize()
{
   ReorganizationState st;
   st.prevTop_ = top();
   st.reorgBranchPoint_ = organizeChain(true);
   st.prevTopStillValid_ = (st.reorgBranchPoint_ == nullptr);
   st.hasNewTop_ = (st.prevTop_ != top());
   st.newTop_ = top();
   return st;
}

void Blockchain::updateBranchingMaps(
   LMDBBlockDatabase* db, ReorganizationState& reorgState)
{
   std::map<unsigned, uint8_t> dupIDs;
   std::map<unsigned, bool> blockIDs;

   try {
      HeaderPtr headerPtr;
      if (reorgState.prevTopStillValid_) {
         headerPtr = reorgState.prevTop_;
      } else {
         headerPtr = reorgState.reorgBranchPoint_;
      }
      if (!headerPtr->isInitialized()) {
         headerPtr = getGenesisBlock();
      }

      while (headerPtr->getThisHash() != reorgState.newTop_->getNextHash()) {
         dupIDs.emplace(
            headerPtr->getBlockHeight(), headerPtr->getDuplicateID());
         blockIDs.emplace(
            headerPtr->getThisID(), headerPtr->isMainBranch());
         if (headerPtr->getNextHash().empty()) {
            break;
         }
         headerPtr = getHeaderByHash(headerPtr->getNextHash());
      }
   } catch (const std::exception&) {
      LOGERR << "could not trace chain form prev top to new top";
   }

   if (!reorgState.prevTopStillValid_) {
      try {
         auto headerPtr = reorgState.prevTop_;
         while (headerPtr != reorgState.reorgBranchPoint_) {
            blockIDs.emplace(
               headerPtr->getThisID(), headerPtr->isMainBranch());
            headerPtr = getHeaderByHash(headerPtr->getPrevHash());
         }
      } catch (const std::exception&) {
         LOGERR << "could not trace chain form prev top to branch point";
      }
   }

   db->setValidDupIDForHeight(dupIDs);
   db->setBlockIDBranch(blockIDs);
   initTopBlockId(db);
}

Blockchain::ReorganizationState 
Blockchain::findReorgPointFromBlock(const BinaryData& blkHash)
{
   auto bh = getHeaderByHash(blkHash);
   
   ReorganizationState st;
   st.prevTop_ = bh;
   st.prevTopStillValid_ = true;
   st.hasNewTop_ = false;
   st.reorgBranchPoint_ = nullptr;

   while (!bh->isMainBranch())
   {
      BinaryData prevHash = bh->getPrevHash();
      bh = getHeaderByHash(prevHash);
   }

   if (bh != st.prevTop_)
   {
      st.reorgBranchPoint_ = bh;
      st.prevTopStillValid_ = false;
   }

   st.newTop_ = top();
   return st;
}

shared_ptr<BlockHeader> Blockchain::top() const
{
   auto ptr = atomic_load(&topBlockPtr_);
   return ptr;
}

shared_ptr<BlockHeader> Blockchain::getGenesisBlock() const
{
   auto headermap = headerMap_.get();

   auto iter = headermap->find(genesisHash_);
   if (iter == headermap->end())
      throw runtime_error("missing genesis block header");

   return iter->second;
}

const shared_ptr<BlockHeader> Blockchain::getHeaderByHeight(
   unsigned index, uint8_t dupId) const
{
   /*
   Returns header for height.
   Passing a dupId above 0x7F will return the main chain header for this height.
   Passing a dupId for a forked block will throw.
   */

   auto headermap = headersByHeight_.get();

   auto headerIter = headermap->find(index);
   if (headerIter == headermap->end())
      throw std::range_error("Cannot get block at height " + to_string(index));

   if (dupId > 0x7F || headerIter->second->getDuplicateID() == dupId)
      return (headerIter->second);

   //if we get this far, we're looking for a block that isn't on the main chain
   throw std::length_error("Cannot get block at height " + to_string(index) +
      " and dup " + to_string(dupId));
}

bool Blockchain::hasHeaderByHeight(unsigned height) const
{
   if (height >= headersByHeight_.size())
      return false;

   return true;
}

////////////////////////////////////////////////////////////////////////////////
Blockchain::HeaderPtr Blockchain::getHeaderByHash(const BinaryData& blkHash) const
{
   auto headermap = headerMap_.get();
   auto it = headermap->find(blkHash);
   if(it == headermap->end()) {
      throw std::range_error(
         "Cannot find block with hash " + blkHash.copySwapEndian().toHexStr());
   }
   return it->second;
}

////////////////////////////////////////////////////////////////////////////////
Blockchain::HeaderPtr Blockchain::getHeaderById(uint32_t id) const
{
   auto headermap = headersById_.get();
   auto headerIter = headermap->find(id);
   if (headerIter == headermap->end()) {
      LOGERR << "cannot find block for id: " << id;
      throw std::range_error("Cannot find block by id");
   }
   return headerIter->second;
}

////////////////////////////////////////////////////////////////////////////////
// Returns nullptr if the new top block is a direct follower of
// the previous top. Returns the branch point if we had to reorg
// TODO: Figure out if there is an elegant way to deal with a forked
//   blockchain containing two equal-length chains
shared_ptr<BlockHeader> Blockchain::organizeChain(bool forceRebuild, bool verbose)
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

   auto headermap = headerMap_.get();
   if (forceRebuild) {
      for (const auto& headerPair : *headermap) {
         headerPair.second->difficultySum_  = -1;
         headerPair.second->blockHeight_ = 0;
         headerPair.second->isFinishedCalc_ = false;
         headerPair.second->nextHash_.clear();
         headerPair.second->isMainBranch_ = false;
      }
      topBlockPtr_ = NULL;
   }

   // Set genesis block
   auto genBlock = getGenesisBlock();
   genBlock->blockHeight_ = 0;
   genBlock->difficultyDbl_ = 1.0;
   genBlock->difficultySum_ = 1.0;
   genBlock->isMainBranch_ = true;
   genBlock->isOrphan_ = false;
   genBlock->isFinishedCalc_ = true;
   genBlock->isInitialized_ = true;

   // If this is the first run, the topBlock is the genesis block
   {
      auto headermap = headersById_.get();
      auto topblock_iter = headermap->find(topBlockId_);
      if (topblock_iter != headermap->end()) {
         std::atomic_store(&topBlockPtr_, topblock_iter->second);
      } else {
         std::atomic_store(&topBlockPtr_, genBlock);
      }
   }

   const auto prevTopBlock = top();
   auto newTopBlock = topBlockPtr_;
   double maxDiffSum = prevTopBlock->getDifficultySum();

   //prepare helper containers
   orphans.clear();

   // Iterate over all blocks, track the maximum difficulty-sum block
   for (const auto& header_pair : *headermap) {
      // *** Walk down the chain following prevHash fields, until
      //     you find a "solved" block. Then walk back up and
      //     fill in the difficulty-sum values (do not set next-
      //     hash ptrs, as we don't know if this is the main branch)
      //     Method returns instantly if block is already "solved"
      double thisDiffSum = traceChainDown(header_pair.second);

      if (header_pair.second->isOrphan_) {
         // disregard this block
      } else if(thisDiffSum > maxDiffSum) {
         // Determine if this is the top block.  If it's the same diffsum
         // as the prev top block, don't do anything
         maxDiffSum     = thisDiffSum;
         newTopBlock = header_pair.second;
      }
   }

   //report long orphaned chains
   for (const auto& orphanChain : orphans) {
      if (orphanChain.second.size() >= 144) {
         auto headerIter = headermap->find(orphanChain.first);
         if (headerIter == headermap->end()) {
            LOGERR << "Could not find first orphan by hash! This is a fatal error!";
            throw std::runtime_error("could not find orphan");
         }

         auto headerPtr = headerIter->second;
         LOGWARN << "Found a long orphan chain!";
         LOGWARN << "  file: " << headerPtr->getBlockFileNum();
         LOGWARN << "  first header hash  : " << headerPtr->getThisHashRef().toHexStr(true);
         LOGWARN << "  missing header hash: " << headerPtr->getPrevHash().toHexStr(true);
         LOGWARN << "  orphan chain length: " << orphanChain.second.size();
      }

      //reset finishedCalc flag on all orphans
      for (const auto& headerHash : orphanChain.second) {
         auto headerIter = headermap->find(orphanChain.first);
         if (headerIter == headermap->end()) {
            LOGERR << "Could not find an orphan by hash! This is a fatal error!";
            throw std::runtime_error("could not find orphan");
         }
         headerIter->second->isFinishedCalc_ = false;
      }
   }

   // Walk down the list one more time, set nextHash fields
   // Also set headersByHeight_;
   std::map<unsigned, HeaderPtr> heightMap;
   bool prevChainStillValid = (newTopBlock == prevTopBlock);
   newTopBlock->nextHash_.clear();
   auto thisHeaderPtr = newTopBlock;

   while (!thisHeaderPtr->isFinishedCalc_) {
      thisHeaderPtr->isFinishedCalc_ = true;
      thisHeaderPtr->isMainBranch_   = true;
      thisHeaderPtr->isOrphan_       = false;
      heightMap[thisHeaderPtr->getBlockHeight()] = thisHeaderPtr;

      auto prevHash = thisHeaderPtr->getPrevHashRef();
      auto childIter = headermap->find(prevHash);
      if (childIter == headermap->end()) {
         LOGERR << "failed to get prev header by hash";
         throw std::runtime_error("failed to get prev header by hash");
      }

      childIter->second->nextHash_ = thisHeaderPtr->getThisHash();
      thisHeaderPtr = childIter->second;
      if (thisHeaderPtr == prevTopBlock) {
         prevChainStillValid = true;
      }
   }

   // Last header in the loop didn't get added (the genesis block on first run)
   thisHeaderPtr->isMainBranch_ = true;
   heightMap[thisHeaderPtr->getBlockHeight()] = thisHeaderPtr;
   headersByHeight_.update(heightMap);

   topBlockId_ = newTopBlock->getThisID();
   std::atomic_store(&topBlockPtr_, newTopBlock);

   //cleanup helper containers
   orphans.clear();

   // Force a full rebuild to make sure everything is marked properly
   // On a full rebuild, prevChainStillValid should ALWAYS be true
   if (!prevChainStillValid) {
      // force-rebuild blockchain (takes less than 1s)
      LOGWARN << "Reorg detected!";
      lock.unlock();
      organizeChain(true);
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
// difficulties and difficultySum values.  Return the difficultySum of 
// this block.
double Blockchain::traceChainDown(shared_ptr<BlockHeader> bhpStart)
{
   /*
   NOTE: THIS CALL IS NOT THREADSAFE (due to headerPtrStack optimization)
   TODO: check difficulty target matches for each block
   */

   if (bhpStart->difficultySum_ > 0.0d) {
      return bhpStart->difficultySum_;
   } else if (bhpStart->isFinishedCalc_) {
      return std::numeric_limits<double>::max();
   }

   // Walk down the chain of prevHash_ values, until we find a block
   // that has a definitive difficultySum value (i.e. >0).
   auto headermap = headerMap_.get();
   std::deque<HeaderPtr> headerList;

   auto thisPtr = bhpStart;
   while (thisPtr->difficultySum_ < 0.0d) {
      headerList.emplace_front(thisPtr);

      auto prevHash = thisPtr->getPrevHash();
      auto iter = headermap->find(prevHash);
      if (iter != headermap->end()) {
         thisPtr = iter->second;
      } else {
         // this block is an orphan, possibly caused by a HeadersFirst
         // blockchain. Nothing to do about that
         break;
      }
   }

   // Now we have a stack of difficulties and pointers. Walk back up
   // (by pointer) and accumulate the difficulty values
   if (!thisPtr->isOrphan_) {
      auto seedDiffSum = thisPtr->difficultySum_;
      auto blkHeight = thisPtr->blockHeight_;

      for (auto& headerPtr : headerList) {
         seedDiffSum += headerPtr->difficultyDbl_;

         headerPtr->difficultySum_ = seedDiffSum;
         headerPtr->blockHeight_   = ++blkHeight;
         headerPtr->isOrphan_      = false;
      }
   } else {
      //look for an orphan chain this new chain connects to
      auto iter = orphans.begin();
      auto prevHash = thisPtr->getPrevHash();
      while (iter != orphans.end()) {
         auto parent = iter->second.find(prevHash);
         if (parent != iter->second.end()) {
            break;
         }
         ++iter;
      }

      //there was no chain for this orphan, start a new one
      if (iter == orphans.end()) {
         iter = orphans.emplace(thisPtr->getThisHashRef(),
            std::unordered_set<BinaryDataRef>{}).first;
      }

      //mark all blocks in that chain as orphans and track them
      for (auto& headerPtr : headerList) {
         headerPtr->isOrphan_ = true;
         headerPtr->isFinishedCalc_ = true;
         iter->second.emplace(headerPtr->getThisHashRef());
      }
      return numeric_limits<double>::max();
   }

   // Finally, we have all the difficulty sums calculated, return this one
   return bhpStart->difficultySum_;
}

/////////////////////////////////////////////////////////////////////////////
void Blockchain::putBareHeaders(LMDBBlockDatabase *db, bool updateDupID)
{
   /***
   Duplicated block heights (forks and orphans) have to saved to the headers
   DB.

   The current code detects the next unkown block by comparing the block
   hashes in the last parsed block file to the list saved in the DB. If
   the DB doesn't keep a record of duplicated or orphaned blocks, it will
   consider the next dup to be the first unknown block in DB until a new
   block file is created by Core.
   ***/

   auto headermap = headerMap_.get();
   for (auto& block : *headermap) {
      StoredHeader sbh;
      sbh.createFromBlockHeader(*(block.second));
      uint8_t dup = db->putBareHeader(sbh, updateDupID);

      // make sure headerMap_ and DB agree
      block.second->setDuplicateID(dup);
   }
}

/////////////////////////////////////////////////////////////////////////////
void Blockchain::putNewBareHeaders(LMDBBlockDatabase *db)
{
   std::unique_lock<std::mutex> lock(mu_);
   if (newlyParsedBlocks_.empty()) {
      return;
   }

   std::map<unsigned, uint8_t> dupIdMap;
   std::map<unsigned, bool> blockIdMap;
   std::vector<std::shared_ptr<BlockHeader>> unputHeaders;

   //create transaction here to batch the write
   auto tx = db->beginTransaction(HEADERS, LMDB::ReadWrite);
   for (auto& block : newlyParsedBlocks_) {
      if (block->blockHeight_ != UINT32_MAX) {
         StoredHeader sbh;
         sbh.createFromBlockHeader(*block);
         uint8_t dup = db->putBareHeader(sbh, true, false);
         block->setDuplicateID(dup); // make sure headerMap_ and DB agree

         if (block->isMainBranch()) {
            dupIdMap.emplace(block->blockHeight_, dup);
         }
         blockIdMap.emplace(block->getThisID(), block->isMainBranch());
      } else {
         unputHeaders.emplace_back(block);
      }
   }

   //update SDBI, keep within the batch transaction
   auto sdbiH = db->getStoredDBInfo(HEADERS, 0);
   if (topBlockPtr_ == nullptr) {
      LOGINFO << "No known top block, didn't update SDBI";
      return;
   }

   if (topBlockPtr_->blockHeight_ >= sdbiH.topBlkHgt_) {
      sdbiH.topBlkHgt_ = topBlockPtr_->blockHeight_;
      sdbiH.topScannedBlkHash_ = topBlockPtr_->thisHash_;
      db->putStoredDBInfo(HEADERS, sdbiH, 0);
   }

   //once commited to the DB, they aren't considered new anymore,
   //so clean up the container
   newlyParsedBlocks_ = std::move(unputHeaders);

   /*
   We need to keep track of the highest assigned
   topID across runs so we manually update it instead of relying on
   headers in the db.
   */
   updateTopIdInDb(db);

   db->setValidDupIDForHeight(dupIdMap);
   db->setBlockIDBranch(blockIdMap);
}

/////////////////////////////////////////////////////////////////////////////
uint32_t Blockchain::getTopIdFromDb(LMDBBlockDatabase *db) const
{
   auto&& tx = db->beginTransaction(HEADERS, LMDB::ReadOnly);

   auto value = db->getValueNoCopy(HEADERS, topIdKey_);
   if (value.getSize() != 4)
      return 0;

   uint32_t topId;
   memcpy(&topId, value.getPtr(), sizeof(uint32_t));
   return topId;
}

/////////////////////////////////////////////////////////////////////////////
void Blockchain::initTopBlockId(LMDBBlockDatabase* db)
{
   auto grabLastStxoKey = [db](void)->uint32_t
   {
      //only works for supernode
      if (db->armoryDbType() != ARMORY_DB_SUPER)
         return 0;

      auto&& tx = db->beginTransaction(STXO, LMDB::ReadOnly);
      auto stxoIter = db->getIterator(STXO);

      if (!stxoIter->seekToLast())
         return 0;
      
      auto lastKey = stxoIter->getKey();
      if (lastKey.getSize() < 4)
         return 0;

      BinaryRefReader keyReader(lastKey.getRef());
      auto intKey = keyReader.get_uint32_t(BE);

      if ((intKey & 0x000000FF) != 0xFF)
         return 0;

      return intKey >> 8;
   };

   //grab top id from block headers sdbi
   auto topId = getTopIdFromDb(db);
   
   //also check the top block id used to record stxos
   auto stxoTopId = grabLastStxoKey();

   if (stxoTopId != 0 && stxoTopId >= topId)
   {
      LOGWARN << "top ID in stxo DB isn't less than top ID in headers DB";
      topId = stxoTopId + 1;
   }

   if (topId > topID_.load(memory_order_relaxed))
      topID_.store(topId, memory_order_relaxed);
}

/////////////////////////////////////////////////////////////////////////////
void Blockchain::updateTopIdInDb(LMDBBlockDatabase *db)
{
   auto inDbTopId = getTopIdFromDb(db);
   auto currentTopId = topID_.load(memory_order_relaxed);

   if (inDbTopId >= currentTopId)
      return;

   BinaryDataRef valRef((const uint8_t*)&currentTopId, 4);

   auto&& tx = db->beginTransaction(HEADERS, LMDB::ReadWrite);
   db->putValue(HEADERS, topIdKey_.getRef(), valRef);
}

/////////////////////////////////////////////////////////////////////////////
std::set<uint32_t> Blockchain::checkForNewBlocks(
   const std::vector<std::shared_ptr<BlockData>>& blocks)
{
   std::set<uint32_t> result;
   auto headermap = headerMap_.get();
   for (const auto block : blocks) {
      const auto& headerHash = block->getHash();
      auto iter = headermap->find(headerHash);
      if (iter != headermap->end()) {
         if (iter->second->dataCopy_.getSize() == HEADER_SIZE) {
            continue;
         }
      }

      if (block->uniqueID() == UINT32_MAX) {
         block->setUniqueID(getNewUniqueID());
      }
      result.emplace(block->uniqueID());
   }
   return result;
}

/////////////////////////////////////////////////////////////////////////////
void Blockchain::addBlocksInBulk(
   const std::deque<std::deque<HeaderPtr>>& headerLists, bool areNew)
{
   if (headerLists.empty()) {
      return;
   }

   std::unique_lock<std::mutex> lock(mu_);
   std::map<BinaryData, HeaderPtr> toAddMap;
   std::map<unsigned, HeaderPtr> idMap;

   {
      auto headermap = headerMap_.get();
      for (auto& headers : headerLists) {
         for (auto& header : headers) {
            bool commitHeader = false;
            const auto& headerHash = header->getThisHash();
            auto iter = headermap->find(headerHash);
            if (iter != headermap->end()) {
               if (iter->second->dataCopy_.getSize() == HEADER_SIZE) {
                  //we already have this block, has the file/offset changed?
                  if (iter->second->getBlockFileNum() == header->getBlockFileNum() &&
                     iter->second->getOffset() == header->getOffset()) {
                     continue;
                  }

                  //header will be replaced, carry the uniqueID over
                  LOGWARN << "header " << headerHash.toHexStr(true) <<
                     " file and offset were replaced!";
                  LOGWARN << "  . fileID - old: " << iter->second->getBlockFileNum() <<
                     ", new: " << header->getBlockFileNum();
                  LOGWARN << "  . offset - old: " << iter->second->getOffset() <<
                     ", new: " << header->getOffset();
                  header->setUniqueID(iter->second->getThisID());
                  commitHeader = true;
                  forceRebuildFlag_ = true;
               }
            }

            //assign uniqueID if necessary
            if (header->getThisID() == UINT32_MAX) {
               header->setUniqueID(getNewUniqueID());
               commitHeader = true;
            }

            toAddMap.emplace(headerHash, header);
            idMap.emplace(header->getThisID(), header);
            if (areNew || commitHeader) {
               newlyParsedBlocks_.emplace_back(header);
            }
         }
      }
   }

   if (!areNew) {
      /*
      Only set the top id when blocks are originally loaded,
      do not allow the process to backtrack the top id to a
      lower value (i.e. if the block insertion was rejected).

      It is crucial block IDs are not reused.
      */
      unsigned topID = topID_.load(memory_order_relaxed);
      for (const auto& headers : headerLists) {
         for (const auto& header : headers) {
            if (topID < header->getThisID()) {
               topID = header->getThisID();
            }
         }
      }
      topID_.store(topID, memory_order_relaxed);
   }

   headerMap_.update(toAddMap);
   headersById_.update(idMap);
}

/////////////////////////////////////////////////////////////////////////////
void Blockchain::forceAddBlocksInBulk(
   map<HashString, shared_ptr<BlockHeader>>& bhMap)
{
   unique_lock<mutex> lock(mu_);
   map<unsigned, shared_ptr<BlockHeader>> idMap;

   for (auto& headerPair : bhMap)
   {
      idMap[headerPair.second->getThisID()] = headerPair.second;
      newlyParsedBlocks_.push_back(headerPair.second);
   }

   headersById_.update(idMap);
   headerMap_.update(bhMap);
}

/////////////////////////////////////////////////////////////////////////////
map<unsigned, set<unsigned>> Blockchain::mapIDsPerBlockFile(void) const
{
   unique_lock<mutex> lock(mu_);

   auto headermap = headersById_.get();
   map<unsigned, set<unsigned>> resultMap;

   for (auto& header : *headermap)
   {
      auto& result_set = resultMap[header.second->blkFileNum_];
      result_set.insert(header.second->uniqueID_);
   }

   return resultMap;
}

/////////////////////////////////////////////////////////////////////////////
map<unsigned, HeightAndDup> Blockchain::getHeightAndDupMap(void) const
{
   auto headermap = headersById_.get();
   map<unsigned, HeightAndDup> hd_map;

   for (auto& block_pair : *headermap)
   {
      HeightAndDup hd(block_pair.second->getBlockHeight(), 
         block_pair.second->getDuplicateID(),
         block_pair.second->isMainBranch());

      hd_map.insert(make_pair(block_pair.first, hd));
   }

   return hd_map;
}

/////////////////////////////////////////////////////////////////////////////
void Blockchain::flagBlockHeader(std::shared_ptr<BlockHeader> header,
   LMDBBlockDatabase *db)
{
   if (db->getOrSetFlaggedBlockFile(header->getBlockFileNum())) {
      LOGINFO << "flagging block file " << header->getBlockFileNum() <<
         " for reparsing";
   }
}
