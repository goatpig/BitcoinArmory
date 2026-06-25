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

#pragma once

#include <memory>
#include <map>
#include <set>
#include <unordered_set>
#include <functional>

#include <Utils/BinaryData.h>
#include "BlockObj.h"

class BlockData;
class LMDBBlockDatabase;

namespace Armory
{
   struct ReorganizationState
   {
      bool prevTopStillValid = false;
      bool hasNewTop = false;
      std::shared_ptr<BlockHeader> prevTop;
      std::shared_ptr<BlockHeader> newTop;
      std::shared_ptr<BlockHeader> reorgBranchPoint;

      std::vector<Types::BlockId> invalidatedBlockIds;
      std::vector<Types::BlockId> newMainBranchIds;
   };

   /////////////////////////////////////////////////////////////////////////////
   // Manages the blockchain, keeping track of all the block headers
   // and our longest cord
   class Blockchain
   {
      using HeaderSet = std::unordered_set<HeaderPtr,
         BlockHeader::Hasher, BlockHeader::IsEqual>;
   private:
      void clear(void);
      std::shared_ptr<BlockHeader> organizeChain(bool = false, bool = false);
      /////////////////////////////////////////////////////////////////////////////
      // Update/organize the headers map (figure out longest chain, mark orphans)
      // Start from a node, trace down to the highest solved block, accumulate
      // difficulties and difficultySum values.  Return the difficultySum of 
      // this block.
      void traceChainDown(std::shared_ptr<BlockHeader>);
      Types::BlockId getNewUniqueID(void);

   public:
      Blockchain(const BinaryData&);

      /**
      * check/add blocks to the chain
      **/
      void loadHeadersFromDB(LMDBBlockDatabase*, const std::function<void(size_t)>&);
      uint32_t stageNewHeaders(const std::vector<std::shared_ptr<BlockHeader>>&);
      void putNewHeaders(LMDBBlockDatabase*);
      void flagInvalidBlocks(LMDBBlockDatabase*, const std::set<Types::BlockId>&);

      /**
      * organize/reorganize chain
      **/
      ReorganizationState organize(bool, bool);

      std::shared_ptr<BlockHeader> top(void) const;
      std::shared_ptr<BlockHeader> getGenesisHeader(void) const;

      const HeaderSet& getHeaderSet(void) const;
      const std::shared_ptr<BlockHeader> getHeaderByHeight(unsigned) const;
      HeaderPtr getHeaderByHash(const BinaryData&) const;
      HeaderPtr getHeaderByHash(BinaryDataRef) const;
      HeaderPtr getHeaderByHash(const Hash32&) const;
      HeaderPtr getHeaderById(Types::BlockId) const;
      BlockOffset getTopBlockOffset(void) const;

      std::map<Types::FileId, std::set<Types::BlockId>> mapIDsPerBlockFile(void) const;
      void flagBlockHeader(std::shared_ptr<BlockHeader>, LMDBBlockDatabase*);
      const std::vector<HeaderPtr>& headersById(void) const;
      const std::set<Types::BlockId>& getInvalidBlockIds(void) const;

   private:
      const Hash32 genesisHash_;
      HeaderSet headerSet_;
      std::vector<HeaderPtr> headersById_;
      std::vector<HeaderPtr> headersByHeight_;
      std::set<Types::BlockId> invalidBlockIds_;

      std::vector<HeaderPtr> newlyParsedHeaders_;
      std::atomic<HeaderPtr> topBlockPtr_;
      Types::BlockId idOfTopBlock_ = 0;

      std::atomic<Types::BlockId> highestBlockID_;
      BlockOffset topBlockOffset_;

      mutable std::mutex mu_;
      bool forceRebuildFlag_ = false;
   };
} //namespace Armory
