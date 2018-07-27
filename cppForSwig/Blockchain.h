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

////////////////////////////////////////////////////////////////////////////////
//
// Manages the blockchain, keeping track of all the block headers
// and our longest cord
//
class Blockchain
{
public:
   Blockchain(const HashString &genesisHash);
   void clear();
   
   struct ReorganizationState
   {
      bool prevTopStillValid_ = false;
      bool hasNewTop_ = false;
      shared_ptr<BlockHeader> prevTop_;
      shared_ptr<BlockHeader> newTop_;
      shared_ptr<BlockHeader> reorgBranchPoint_;
   };
   
   /**
    * Adds a block to the chain
    **/
   set<uint32_t> addBlocksInBulk(
      const map<HashString, shared_ptr<BlockHeader>>&, bool flag);
   void forceAddBlocksInBulk(map<HashString, shared_ptr<BlockHeader>>&);

   ReorganizationState organize(bool verbose);
   ReorganizationState forceOrganize();
   ReorganizationState findReorgPointFromBlock(const BinaryData& blkHash);

   void updateBranchingMaps(LMDBBlockDatabase*, ReorganizationState&);

   shared_ptr<BlockHeader> top() const;
   shared_ptr<BlockHeader> getGenesisBlock() const;
   const shared_ptr<BlockHeader> getHeaderByHeight(unsigned height) const;
   bool hasHeaderByHeight(unsigned height) const;
   
   const shared_ptr<BlockHeader> getHeaderByHash(HashString const & blkHash) const;
   shared_ptr<BlockHeader> getHeaderById(uint32_t id) const;

   bool hasHeaderWithHash(BinaryData const & txHash) const;
   const shared_ptr<BlockHeader> getHeaderPtrForTxRef(const TxRef &txr) const;
   const shared_ptr<BlockHeader> getHeaderPtrForTx(const Tx & txObj) const
   {
      if(txObj.getTxRef().isNull())
      {
         throw runtime_error("TxRef in Tx object is not set, cannot get header ptr");
      }
      
      return getHeaderPtrForTxRef(txObj.getTxRef());
   }
   
   shared_ptr<map<HashString, shared_ptr<BlockHeader>>> allHeaders(void) const
   {
      return headerMap_.get();
   }

   void putBareHeaders(LMDBBlockDatabase *db, bool updateDupID=true);
   void putNewBareHeaders(LMDBBlockDatabase *db);

   unsigned int getNewUniqueID(void) { return topID_.fetch_add(1, memory_order_relaxed); }

   map<unsigned, set<unsigned>> mapIDsPerBlockFile(void) const;
   map<unsigned, HeightAndDup> getHeightAndDupMap(void) const;

private:
   shared_ptr<BlockHeader> organizeChain(bool forceRebuild = false, bool verbose = false);
   /////////////////////////////////////////////////////////////////////////////
   // Update/organize the headers map (figure out longest chain, mark orphans)
   // Start from a node, trace down to the highest solved block, accumulate
   // difficulties and difficultySum values.  Return the difficultySum of 
   // this block.
   double traceChainDown(shared_ptr<BlockHeader> bhpStart);

private:
   //TODO: make this whole class thread safe

   const BinaryData genesisHash_;
   TransactionalMap<BinaryData, shared_ptr<BlockHeader>> headerMap_;
   TransactionalMap<unsigned, shared_ptr<BlockHeader>> headersById_;

   vector<shared_ptr<BlockHeader>> newlyParsedBlocks_;
   TransactionalMap<unsigned, shared_ptr<BlockHeader>> headersByHeight_;
   shared_ptr<BlockHeader> topBlockPtr_;
   unsigned topBlockId_ = 0;
   Blockchain(const Blockchain&); // not defined

   atomic<unsigned int> topID_;

   mutable mutex mu_;
};

#endif
