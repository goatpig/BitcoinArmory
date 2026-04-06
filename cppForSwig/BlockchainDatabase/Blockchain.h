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

#pragma once

#include <memory>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>

#include <Utils/BinaryData.h>

class BlockHeader;
class BlockData;
class LMDBBlockDatabase;

namespace Armory
{
   struct HeightAndDup
   {
      const unsigned height;
      const uint8_t dup;
      bool isMain;

      HeightAndDup(unsigned, uint8_t, bool);
   };

   struct ReorganizationState
   {
      bool prevTopStillValid = false;
      bool hasNewTop = false;
      std::shared_ptr<BlockHeader> prevTop;
      std::shared_ptr<BlockHeader> newTop;
      std::shared_ptr<BlockHeader> reorgBranchPoint;
   };

   struct Hash32
   {
      uint64_t data[4];
      explicit Hash32(const BinaryData&);
      explicit Hash32(const BinaryDataRef&);

      struct Hasher
      {
         using is_transparent = void;
         std::size_t operator()(const Hash32&) const;
         std::size_t operator()(const BinaryData&) const;
         std::size_t operator()(const BinaryDataRef&) const;
      };

      struct Comparator
      {
         using is_transparent = void;
         constexpr bool operator()(const Hash32&, const Hash32&) const;
         bool operator()(const Hash32&, const BinaryData&) const;
         bool operator()(const BinaryData&, const Hash32&) const;
         bool operator()(const Hash32&, const BinaryDataRef&) const;
         bool operator()(const BinaryDataRef&, const Hash32&) const;
      };
   };

   /////////////////////////////////////////////////////////////////////////////
   // Manages the blockchain, keeping track of all the block headers
   // and our longest cord
   class Blockchain
   {
      using HeaderPtr = std::shared_ptr<BlockHeader>;

   private:
      std::shared_ptr<BlockHeader> getGenesisBlock(void) const;
      std::shared_ptr<BlockHeader> organizeChain(bool = false, bool = false);
      /////////////////////////////////////////////////////////////////////////////
      // Update/organize the headers map (figure out longest chain, mark orphans)
      // Start from a node, trace down to the highest solved block, accumulate
      // difficulties and difficultySum values.  Return the difficultySum of 
      // this block.
      double traceChainDown(std::shared_ptr<BlockHeader>);

   public:
      Blockchain(const BinaryData&);
      void clear(void);

      /**
      * check/add blocks to the chain
      **/
      std::set<uint32_t> checkForNewBlocks(const std::vector<std::shared_ptr<BlockData>>&);
      void addBlocksInBulk(const std::deque<std::deque<HeaderPtr>>&, bool);
      void forceAddBlocksInBulk(std::map<BinaryData, HeaderPtr>&);

      /**
      * organize/reorganize chain
      **/
      ReorganizationState organize(bool);
      ReorganizationState forceOrganize();
      ReorganizationState findReorgPointFromBlock(const BinaryData&);

      void updateBranchingMaps(LMDBBlockDatabase*, ReorganizationState&);

      std::shared_ptr<BlockHeader> top(void) const;
      const std::shared_ptr<BlockHeader> getHeaderByHeight(
         unsigned, uint8_t) const;

      HeaderPtr getHeaderByHash(const BinaryData&) const;
      HeaderPtr getHeaderById(uint32_t) const;
      HeaderPtr getHeaderForTxKey(const BinaryData&) const;

      void putBareHeaders(LMDBBlockDatabase*, bool = true);
      void putNewBareHeaders(LMDBBlockDatabase*);

      uint32_t getNewUniqueID(void);
      uint32_t getTopId(void) const;
      uint32_t getTopIdFromDb(LMDBBlockDatabase*) const;
      void initTopBlockId(LMDBBlockDatabase*);
      void updateTopIdInDb(LMDBBlockDatabase*);

      std::map<unsigned, std::set<unsigned>> mapIDsPerBlockFile(void) const;
      std::map<unsigned, HeightAndDup> getHeightAndDupMap(void) const;
      void flagBlockHeader(std::shared_ptr<BlockHeader>, LMDBBlockDatabase*);

   private:
      const BinaryData genesisHash_;
      std::unordered_map<Hash32, HeaderPtr, Hash32::Hasher, Hash32::Comparator> headerMap_;
      std::unordered_map<unsigned, HeaderPtr> headersById_;
      std::vector<HeaderPtr> headersByHeight_;

      std::vector<HeaderPtr> newlyParsedBlocks_;
      HeaderPtr topBlockPtr_;
      unsigned topBlockId_ = 0;
      Blockchain(const Blockchain&); // not defined

      std::atomic<uint32_t> topID_;
      static const BinaryData topIdKey_;

      mutable std::mutex mu_;
      bool forceRebuildFlag_ = false;
   };
} //namespace Armory
