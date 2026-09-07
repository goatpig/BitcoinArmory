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

#include <vector>
#include <string>
#include <memory>
#include <Utils/Types.h>

class BinaryData;
class BinaryDataRef;

namespace Armory
{
   struct Hash32
   {
      uint64_t data[4];
      explicit Hash32(void);
      explicit Hash32(const BinaryData&);
      explicit Hash32(const BinaryDataRef&);

      bool operator==(const Hash32&) const;
      bool operator==(const BinaryData&) const;
      bool operator<(const Hash32&) const;

      BinaryData toBinaryData(void) const;
      BinaryDataRef getRef(void) const;
      std::string toHexStr(bool=false) const;
      bool valid(void) const;

      struct Hasher
      {
         using is_transparent = void;
         std::size_t operator()(const Hash32&) const;
         std::size_t operator()(const BinaryData&) const;
         std::size_t operator()(const BinaryDataRef&) const;
      };

      struct IsEqual
      {
         using is_transparent = void;
         bool operator()(const Hash32&, const Hash32&) const;
         bool operator()(const Hash32&, const BinaryData&) const;
         bool operator()(const BinaryData&, const Hash32&) const;
         bool operator()(const Hash32&, const BinaryDataRef&) const;
         bool operator()(const BinaryDataRef&, const Hash32&) const;
      };
   };

   ////////
   class BlockOffset
   {
   private:
      Types::FileId fileID_;
      size_t offset_;

   public:
      BlockOffset(Types::FileId, size_t);
      BlockOffset(const BlockOffset&);

      bool operator<(const BlockOffset&) const;
      BlockOffset& operator=(const BlockOffset&);
      bool isValid(void) const;

      Types::FileId fileID(void) const;
      size_t offset(void) const;
   };

   ////////
   class BlockHeader;
   using HeaderPtr = std::shared_ptr<BlockHeader>;

   ////////
   class BlockHeader
   {
      friend class Blockchain;

   private:
      BlockHeader(Hash32&, Hash32&, Hash32&, double, uint32_t, uint32_t);
      static BlockHeader unserialize(const uint8_t*, size_t);

   public:
      struct Hasher
      {
         using is_transparent = void;
         std::size_t operator()(const HeaderPtr&) const;
         std::size_t operator()(const Hash32&) const;
         std::size_t operator()(const BinaryData&) const;
         std::size_t operator()(const BinaryDataRef&) const;
      };

      struct IsEqual
      {
         using is_transparent = void;
         bool operator()(const HeaderPtr&, const HeaderPtr&) const;
         bool operator()(const HeaderPtr&, const Hash32&) const;
         bool operator()(const Hash32&, const HeaderPtr&) const;
         bool operator()(const HeaderPtr&, const BinaryData&) const;
         bool operator()(const BinaryData&, const HeaderPtr&) const;
         bool operator()(const HeaderPtr&, const BinaryDataRef&) const;
         bool operator()(const BinaryDataRef&, const HeaderPtr&) const;
      };

      enum class MerkleState : int
      {
         Unchecked = 0,
         Valid,
         Invalid
      };

   public:
      explicit BlockHeader(const uint8_t*, size_t);
      explicit BlockHeader(BinaryDataRef);

      //native header data getters
      uint32_t getVersion(void) const;
      const Hash32& getThisHash(void) const;
      const Hash32* getNextHash(void) const;
      const Hash32& getPrevHash(void) const;
      const Hash32& getMerkleRoot(void) const;

      uint32_t getTimestamp(void) const;
      bool isMainBranch(void) const;
      bool isOrphan(void) const;
      double getDifficulty(void) const;
      double getDifficultySum(void) const;

      //optional header data getters
      uint32_t getBlockHeight(void) const;
      uint32_t getNumTx(void) const;
      size_t getOffset(void) const;
      Types::FileId getBlockFileId(void) const;
      uint32_t getBlockSize(void) const;
      BinaryDataRef getRawData(void) const;
      Types::BlockId getUniqueID(void) const;

      //setters for optional data
      void setBlockHeight(unsigned);
      void setBlockSize(uint32_t);
      void setNumTx(uint32_t);
      void setBlockFileId(Types::FileId);
      void setBlockFileOffset(size_t);
      void setRawData(BinaryData);
      void setUniqueID(Types::BlockId);

      //merkle checks
      void checkMerkleRoot(const BinaryData&);
      void setMerkleValid(bool);
      bool parsedBlockData(void) const;
      bool isMerkleValid(void) const;

      void pprintAlot(std::ostream&);

   private:
      const Hash32      thisHash_;
      const Hash32      prevHash_;
      const Hash32      merkleRoot_;
      const double      difficultyDbl_;
      const uint32_t    timestamp_;
      const uint32_t    version_;

      // Specific to the DB storage
      double            difficultySum_ = -1.0;
      size_t            blkFileOffset_ = SIZE_MAX;
      uint32_t          blockHeight_ = UINT32_MAX;
      Types::BlockId    uniqueID_ = Types::INVALID_BLOCK_ID;
      uint32_t          numTx_ = UINT32_MAX;
      uint32_t          numBlockBytes_; // includes header + nTx + sum(Tx)
      Types::FileId     blkFileId_ = Types::INVALID_FILE_ID;
      MerkleState       checkState_ = MerkleState::Unchecked;

      //only useful to write header on disk the one time
      std::vector<uint8_t> rawData_;

      // Need to compute these later
      const Hash32*     nextHash_ = nullptr;
      std::shared_ptr<BlockHeader> nextPtr_ = nullptr;

      bool              isMainBranch_ = false;
      bool              isOrphan_ = true;
      bool              isFinishedCalc_ = false;
   };
}
