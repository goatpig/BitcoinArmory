////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016, goatpig                                               //            
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                   
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Blockchain.h"
#include "util.h"

#ifdef max
#undef max
#endif

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
// Start Blockchain methods
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

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

   topID_.store(0, memory_order_relaxed);
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
   map<unsigned, uint8_t> dupIDs;
   map<unsigned, bool> blockIDs;

   try
   {
      shared_ptr<BlockHeader> headerPtr;
      if (reorgState.prevTopStillValid_)
         headerPtr = reorgState.prevTop_;
      else
         headerPtr = reorgState.reorgBranchPoint_;

      if (!headerPtr->isInitialized())
         headerPtr = getGenesisBlock();

      while(headerPtr->getThisHash() != reorgState.newTop_->getNextHash())
      {
         dupIDs.insert(make_pair(
            headerPtr->getBlockHeight(), headerPtr->getDuplicateID()));
         blockIDs.insert(make_pair(
            headerPtr->getThisID(), headerPtr->isMainBranch()));
         
         headerPtr = getHeaderByHash(headerPtr->getNextHash());
      }
   }
   catch(exception&)
   { 
      LOGERR << "could not trace chain form prev top to new top";
   }

   if (!reorgState.prevTopStillValid_)
   {
      try
      {
         auto headerPtr = reorgState.prevTop_;
         while (headerPtr != reorgState.reorgBranchPoint_)
         {
            blockIDs.insert(make_pair(
               headerPtr->getThisID(), headerPtr->isMainBranch()));

            headerPtr = getHeaderByHash(headerPtr->getPrevHash());
         }
      }
      catch(exception&)
      {
         LOGERR << "could not trace chain form prev top to branch point";
      }
   }

   db->setValidDupIDForHeight(dupIDs);
   db->setBlockIDBranch(blockIDs);
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

const shared_ptr<BlockHeader> Blockchain::getHeaderByHeight(unsigned index) const
{
   auto headermap = headersByHeight_.get();

   auto headerIter = headermap->find(index);
   if (headerIter == headermap->end())
      throw std::range_error("Cannot get block at height " + to_string(index));

   return (headerIter->second);
}


bool Blockchain::hasHeaderByHeight(unsigned height) const
{
   if (height >= headersByHeight_.size())
      return false;

   return true;
}

const shared_ptr<BlockHeader> Blockchain::getHeaderByHash(HashString const & blkHash) const
{
   auto headermap = headerMap_.get();

   auto it = headermap->find(blkHash);
   if(it == headermap->end())
      throw std::range_error("Cannot find block with hash " + blkHash.copySwapEndian().toHexStr());

   return it->second;
}

shared_ptr<BlockHeader> Blockchain::getHeaderById(uint32_t id) const
{
   auto headermap = headersById_.get();

   auto headerIter = headermap->find(id);
   if (headerIter == headermap->end())
   {
      LOGERR << "cannot find block for id: " << id;
      throw std::range_error("Cannot find block by id");
   }

   return headerIter->second;
}

bool Blockchain::hasHeaderWithHash(BinaryData const & txHash) const
{
   auto headermap = headerMap_.get();
   auto it = headermap->find(txHash);
   if (it == headermap->end())
      return false;

   return true;
}

const shared_ptr<BlockHeader> Blockchain::getHeaderPtrForTxRef(const TxRef &txr) const
{
   if(txr.isNull())
      throw std::range_error("Null TxRef");

   uint32_t hgt = txr.getBlockHeight();
   uint8_t  dup = txr.getDuplicateID();
   auto bh = getHeaderByHeight(hgt);
   if(bh->getDuplicateID() != dup)
   {
      throw runtime_error("Requested txref not on main chain (BH dupID is diff)");
   }
   return bh;
}

////////////////////////////////////////////////////////////////////////////////
// Returns nullptr if the new top block is a direct follower of
// the previous top. Returns the branch point if we had to reorg
// TODO:  Figure out if there is an elegant way to deal with a forked 
//        blockchain containing two equal-length chains
shared_ptr<BlockHeader> Blockchain::organizeChain(bool forceRebuild, bool verbose)
{
   if (verbose)
   {
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

   if(forceRebuild)
   {
      map<HashString, shared_ptr<BlockHeader>>::iterator iter;
      for( iter  = headermap->begin();
           iter != headermap->end();
           iter++)
      {
         iter->second->difficultySum_  = -1;
         iter->second->blockHeight_ = 0;
         iter->second->isFinishedCalc_ = false;
         iter->second->nextHash_ = BtcUtils::EmptyHash();
         iter->second->isMainBranch_ = false;
      }
      topBlockPtr_ = NULL;
      topID_.store(0, memory_order_relaxed);
   }

   unsigned topID = topID_.load(memory_order_relaxed);

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
      if (topblock_iter != headermap->end())
      {
         atomic_store(&topBlockPtr_, topblock_iter->second);
      }
      else
      {
         atomic_store(&topBlockPtr_, genBlock);
      }
   }

   const auto prevTopBlock = top();
   auto newTopBlock = topBlockPtr_;
   
   // Iterate over all blocks, track the maximum difficulty-sum block
   double   maxDiffSum     = prevTopBlock->getDifficultySum();
   for( auto &header_pair : *headermap)
   {
      // *** Walk down the chain following prevHash fields, until
      //     you find a "solved" block.  Then walk back up and 
      //     fill in the difficulty-sum values (do not set next-
      //     hash ptrs, as we don't know if this is the main branch)
      //     Method returns instantly if block is already "solved"
      double thisDiffSum = traceChainDown(header_pair.second);

      if (header_pair.second->isOrphan_)
      {
         // disregard this block
      }
      // Determine if this is the top block.  If it's the same diffsum
      // as the prev top block, don't do anything
      else if(thisDiffSum > maxDiffSum)
      {
         maxDiffSum     = thisDiffSum;
         newTopBlock = header_pair.second;
      }
   }

   
   // Walk down the list one more time, set nextHash fields
   // Also set headersByHeight_;
   map<unsigned, shared_ptr<BlockHeader>> heightMap;
   bool prevChainStillValid = (newTopBlock == prevTopBlock);
   newTopBlock->nextHash_ = BtcUtils::EmptyHash();
   auto thisHeaderPtr = newTopBlock;

   while (!thisHeaderPtr->isFinishedCalc_)
   {
      thisHeaderPtr->isFinishedCalc_ = true;
      thisHeaderPtr->isMainBranch_   = true;
      thisHeaderPtr->isOrphan_       = false;
      heightMap[thisHeaderPtr->getBlockHeight()] = thisHeaderPtr;

      if (thisHeaderPtr->uniqueID_ > topID)
         topID = thisHeaderPtr->uniqueID_;

      auto prevHash = thisHeaderPtr->getPrevHashRef();
      auto childIter = headermap->find(prevHash);
      if (childIter == headermap->end())
      {
         LOGERR << "failed to get prev header by hash";
         throw runtime_error("failed to get prev header by hash");
      }

      childIter->second->nextHash_ = thisHeaderPtr->getThisHash();
      thisHeaderPtr = childIter->second;
      if (thisHeaderPtr == prevTopBlock)
         prevChainStillValid = true;
   }

   // Last header in the loop didn't get added (the genesis block on first run)
   thisHeaderPtr->isMainBranch_ = true;
   heightMap[thisHeaderPtr->getBlockHeight()] = thisHeaderPtr;
   headersByHeight_.update(heightMap);

   topID_.store(topID + 1, memory_order_relaxed);
   topBlockId_ = newTopBlock->getThisID();
   atomic_store(&topBlockPtr_, newTopBlock);

   // Force a full rebuild to make sure everything is marked properly
   // On a full rebuild, prevChainStillValid should ALWAYS be true
   if( !prevChainStillValid )
   {
      LOGWARN << "Reorg detected!";

      organizeChain(true); // force-rebuild blockchain (takes less than 1s)
      return thisHeaderPtr;
   }

   if (verbose)
   {
      TIMER_STOP("orgChain");
      auto duration = TIMER_READ_SEC("orgChain");
      LOGINFO << "Organized chain in " << duration << "s";
   }

   return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Start from a node, trace down to the highest solved block, accumulate
// difficulties and difficultySum values.  Return the difficultySum of 
// this block.
double Blockchain::traceChainDown(shared_ptr<BlockHeader> bhpStart)
{
   if(bhpStart->difficultySum_ > 0)
      return bhpStart->difficultySum_;

   // Prepare some data structures for walking down the chain
   vector<shared_ptr<BlockHeader>>   headerPtrStack(headerMap_.size());
   vector<double>         difficultyStack(headerMap_.size());
   uint32_t blkIdx = 0;

   // Walk down the chain of prevHash_ values, until we find a block
   // that has a definitive difficultySum value (i.e. >0). 
   auto headermap = headerMap_.get();

   auto thisPtr = bhpStart;
   while( thisPtr->difficultySum_ < 0)
   {
      double thisDiff         = thisPtr->difficultyDbl_;
      difficultyStack[blkIdx] = thisDiff;
      headerPtrStack[blkIdx]  = thisPtr;
      blkIdx++;

      auto iter = headermap->find(thisPtr->getPrevHash());
      if(iter != headermap->end())
      {
         thisPtr = iter->second;
      }
      else
      {
         thisPtr->isOrphan_ = true;
         // this block is an orphan, possibly caused by a HeadersFirst
         // blockchain. Nothing to do about that
         return numeric_limits<double>::max();
      }
   }


   // Now we have a stack of difficulties and pointers.  Walk back up
   // (by pointer) and accumulate the difficulty values 
   double   seedDiffSum = thisPtr->difficultySum_;
   uint32_t blkHeight   = thisPtr->blockHeight_;
   for(int32_t i=blkIdx-1; i>=0; i--)
   {
      seedDiffSum += difficultyStack[i];
      blkHeight++;
      thisPtr                 = headerPtrStack[i];
      thisPtr->difficultyDbl_ = difficultyStack[i];
      thisPtr->difficultySum_ = seedDiffSum;
      thisPtr->blockHeight_   = blkHeight;
      thisPtr->isOrphan_ = false;
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
   for (auto& block : *headermap)
   {
      StoredHeader sbh;
      sbh.createFromBlockHeader(*(block.second));
      uint8_t dup = db->putBareHeader(sbh, updateDupID);
      block.second->setDuplicateID(dup);  // make sure headerMap_ and DB agree
   }
}

/////////////////////////////////////////////////////////////////////////////
void Blockchain::putNewBareHeaders(LMDBBlockDatabase *db)
{
   unique_lock<mutex> lock(mu_);

   if (newlyParsedBlocks_.size() == 0)
      return;

   map<unsigned, uint8_t> dupIdMap;
   map<unsigned, bool> blockIdMap;

   //create transaction here to batch the write
   auto&& tx = db->beginTransaction(HEADERS, LMDB::ReadWrite);

   vector<shared_ptr<BlockHeader>> unputHeaders;
   for (auto& block : newlyParsedBlocks_)
   {
      if (block->blockHeight_ != UINT32_MAX)
      {
         StoredHeader sbh;
         sbh.createFromBlockHeader(*block);
         //don't update SDBI, we'll do it here once instead
         uint8_t dup = db->putBareHeader(sbh, true, false);
         block->setDuplicateID(dup);  // make sure headerMap_ and DB agree
         
         if(block->isMainBranch())
            dupIdMap.insert(make_pair(block->blockHeight_, dup));

         blockIdMap.insert(
            make_pair(block->getThisID(), block->isMainBranch()));
      }
      else
      {
         unputHeaders.push_back(block);
      }
   }

   //update SDBI, keep within the batch transaction
   auto&& sdbiH = db->getStoredDBInfo(HEADERS, 0);

   if (topBlockPtr_ == nullptr)
   {
      LOGINFO << "No known top block, didn't update SDBI";
      return;
   }

   if (topBlockPtr_->blockHeight_ >= sdbiH.topBlkHgt_)
   {
      sdbiH.topBlkHgt_ = topBlockPtr_->blockHeight_;
      sdbiH.topScannedBlkHash_ = topBlockPtr_->thisHash_;
      db->putStoredDBInfo(HEADERS, sdbiH, 0);
   }


   //once commited to the DB, they aren't considered new anymore, 
   //so clean up the container
   newlyParsedBlocks_ = unputHeaders;

   db->setValidDupIDForHeight(dupIdMap);
   db->setBlockIDBranch(blockIdMap);
}

/////////////////////////////////////////////////////////////////////////////
set<uint32_t> Blockchain::addBlocksInBulk(
   const map<BinaryData, shared_ptr<BlockHeader>>& bhMap, bool flag)
{
   if (bhMap.size() == 0)
      return set<uint32_t>();

   set<uint32_t> returnSet;
   unique_lock<mutex> lock(mu_);

   map<BinaryData, shared_ptr<BlockHeader>> toAddMap;
   map<unsigned, shared_ptr<BlockHeader>> idMap;
   
   {
      auto headermap = headerMap_.get();

      for (auto& header_pair : bhMap)
      {
         auto iter = headermap->find(header_pair.first);
         if (iter != headermap->end())
         {
            if (iter->second->dataCopy_.getSize() == HEADER_SIZE)
               continue;
         }

         toAddMap.insert(header_pair);
         idMap[header_pair.second->getThisID()] = header_pair.second;
         if (flag)
            newlyParsedBlocks_.push_back(header_pair.second);
         returnSet.insert(header_pair.second->getThisID());
      }
   }

   headerMap_.update(toAddMap);
   headersById_.update(idMap);
   
   {
      auto headeridmap = headersById_.get();
      auto localTop = headeridmap->rbegin()->first;
      if (topID_.load(memory_order_relaxed) < localTop)
         topID_.store(localTop + 1, memory_order_relaxed);
   }

   return returnSet;
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
