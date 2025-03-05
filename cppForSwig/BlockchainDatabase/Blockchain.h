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

#ifndef _BLOCKCHAIN_H
#define _BLOCKCHAIN_H

#include "ThreadSafeClasses.h"
#include "BlockObj.h"
#include "lmdb_wrapper.h"

#include <memory>
#include <deque>
#include <map>

////////////////////////////////////////////////////////////////////////////////
struct HeightAndDup
{
   const unsigned height_;
   const uint8_t dup_;
   bool isMain_;

   HeightAndDup(unsigned height, uint8_t dup, bool isMain) :
      height_(height), dup_(dup), isMain_(isMain)
   {}
};

class BlockData;

////////////////////////////////////////////////////////////////////////////////
//
// Manages the blockchain, keeping track of all the block headers
// and our longest cord
//
class Blockchain
{
   using HeaderPtr = std::shared_ptr<BlockHeader>;

public:
   Blockchain(const HashString &genesisHash);
   void clear();

   struct ReorganizationState
   {
      bool prevTopStillValid_ = false;
      bool hasNewTop_ = false;
      std::shared_ptr<BlockHeader> prevTop_;
      std::shared_ptr<BlockHeader> newTop_;
      std::shared_ptr<BlockHeader> reorgBranchPoint_;
   };

   /**
    * check/add blocks to the chain
   **/
   std::set<uint32_t> checkForNewBlocks(const std::vector<std::shared_ptr<BlockData>>&);
   void addBlocksInBulk(const std::deque<std::deque<HeaderPtr>>&, bool flag);
   void forceAddBlocksInBulk(std::map<BinaryData, HeaderPtr>&);

   /**
    * organize/reorganize chain
   **/
   ReorganizationState organize(bool verbose);
   ReorganizationState forceOrganize();
   ReorganizationState findReorgPointFromBlock(const BinaryData& blkHash);

   void updateBranchingMaps(LMDBBlockDatabase*, ReorganizationState&);

   std::shared_ptr<BlockHeader> top() const;
   std::shared_ptr<BlockHeader> getGenesisBlock() const;
   const std::shared_ptr<BlockHeader> getHeaderByHeight(
      unsigned height, uint8_t dupId) const;
   bool hasHeaderByHeight(unsigned height) const;
   
   const std::shared_ptr<BlockHeader> getHeaderByHash(HashString const & blkHash) const;
   std::shared_ptr<BlockHeader> getHeaderById(uint32_t id) const;

   bool hasHeaderWithHash(BinaryData const & txHash) const;
   const std::shared_ptr<BlockHeader> getHeaderPtrForTxRef(const TxRef &txr) const;
   
   std::shared_ptr<const std::map<HashString, std::shared_ptr<BlockHeader>>> allHeaders(void) const
   {
      return headerMap_.get();
   }

   void putBareHeaders(LMDBBlockDatabase *db, bool updateDupID=true);
   void putNewBareHeaders(LMDBBlockDatabase *db);

   uint32_t getNewUniqueID(void) { return topID_.fetch_add(1, std::memory_order_relaxed); }
   uint32_t getTopId(void) { return topID_.load(std::memory_order_relaxed); }
   uint32_t getTopIdFromDb(LMDBBlockDatabase*) const;
   void initTopBlockId(LMDBBlockDatabase* db);
   void updateTopIdInDb(LMDBBlockDatabase*);

   std::map<unsigned, std::set<unsigned>> mapIDsPerBlockFile(void) const;
   std::map<unsigned, HeightAndDup> getHeightAndDupMap(void) const;
   void flagBlockHeader(std::shared_ptr<BlockHeader>, LMDBBlockDatabase*);

private:
   std::shared_ptr<BlockHeader> organizeChain(bool forceRebuild = false, bool verbose = false);
   /////////////////////////////////////////////////////////////////////////////
   // Update/organize the headers map (figure out longest chain, mark orphans)
   // Start from a node, trace down to the highest solved block, accumulate
   // difficulties and difficultySum values.  Return the difficultySum of 
   // this block.
   double traceChainDown(std::shared_ptr<BlockHeader> bhpStart);

private:
   //TODO: get rid of this shyte!
   //use std::unordered_map, manage access in class getters, not at container level
   //they lead to too many copies, it slows down header parsing to a crawl

   const BinaryData genesisHash_;
   Armory::Threading::TransactionalMap<BinaryData, HeaderPtr> headerMap_;
   Armory::Threading::TransactionalMap<unsigned, HeaderPtr> headersById_;
   Armory::Threading::TransactionalMap<unsigned, HeaderPtr> headersByHeight_;

   std::vector<HeaderPtr> newlyParsedBlocks_;
   HeaderPtr topBlockPtr_;
   unsigned topBlockId_ = 0;
   Blockchain(const Blockchain&); // not defined

   std::atomic<uint32_t> topID_;
   static const BinaryData topIdKey_;

   mutable std::mutex mu_;
};

#endif
