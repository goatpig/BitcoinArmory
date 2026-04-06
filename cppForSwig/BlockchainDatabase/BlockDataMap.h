////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <memory>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <atomic>
#include <functional>
#include <filesystem>

#include <Utils/BinaryData.h>

struct BlockHashVector;
class BCTX;
class BlockHeader;

////////////////////////////////////////////////////////////////////////////////
class BlockData
{
private:
   uint32_t uniqueID_ = UINT32_MAX;
   std::shared_ptr<BlockHashVector> txFilter_;

   std::shared_ptr<BlockHeader> headerPtr_;
   const uint8_t* data_ = nullptr;
   size_t size_ = SIZE_MAX;

   std::vector<std::shared_ptr<BCTX>> txns_;

   unsigned fileID_ = UINT32_MAX;
   size_t offset_ = SIZE_MAX;

   BinaryData blockHash_;

public:
   enum class CheckHashes : int
   {
      NoChecks,
      MerkleOnly,
      TxFilters,
      FullHints
   };

public:
   BlockData(uint32_t);

   static std::shared_ptr<BlockData> deserialize(
      const uint8_t*, size_t,
      const std::shared_ptr<BlockHeader>,
      std::function<unsigned int(const BinaryData&)> getID,
      CheckHashes);

   bool isInitialized(void) const
   {
      return (data_ != nullptr);
   }

   const std::vector<std::shared_ptr<BCTX>>& getTxns(void) const
   {
      return txns_;
   }

   const std::shared_ptr<BlockHeader> header(void) const
   {
      return headerPtr_;
   }

   size_t size(void) const
   {
      return size_;
   }

   void setFileID(unsigned fileid) { fileID_ = fileid; }
   void setOffset(size_t offset) { offset_ = offset; }

   std::shared_ptr<BlockHeader> createBlockHeader(void) const;
   const BinaryData& getHash(void) const { return blockHash_; }

   void computeTxFilter(const std::vector<BinaryData>&);
   std::shared_ptr<BlockHashVector> getTxFilter(void) const;
   uint32_t uniqueID(void) const { return uniqueID_; }
   void setUniqueID(uint32_t);
   std::shared_ptr<BlockHeader> getHeaderPtr(void) const { return headerPtr_; }
};

/////////////////////////////////////////////////////////////////////////////
struct BlockOffset
{
   uint16_t fileID;
   size_t offset;

   BlockOffset(void);
   BlockOffset(uint16_t, size_t);
   BlockOffset(const BlockOffset&);

   bool operator>(const BlockOffset&) const;
   BlockOffset& operator=(const BlockOffset&);
};

/////////////////////////////////////////////////////////////////////////////
class BlockFiles
{
   friend class BlockDataLoader;

private:
   std::map<uint16_t, std::filesystem::path> paths_;
   const std::filesystem::path folderPath_;
   size_t totalBlockchainBytes_ = 0;

public:
   BlockFiles(const std::filesystem::path& folderPath) :
      folderPath_(folderPath)
   {}

   void detectAllBlockFiles(void);
   void detectNewBlockFiles(void);
   const std::filesystem::path& folderPath(void) const { return folderPath_; }
   unsigned fileCount(void) const { return paths_.size(); }
   const std::filesystem::path& getLastFilePath(void) const;
   const std::filesystem::path& getFilePathForID(uint16_t) const;
};

/////////////////////////////////////////////////////////////////////////////
namespace Armory
{
   namespace FileUtils
   {
      class FileCopy;
      class FileMap;
      class BlockDataFileMap;
   }
}

class BlockDataLoader
{
public:
   struct PathAndOffset
   {
      const std::filesystem::path path;
      const uint16_t fileID;
      const size_t offset;
   };

   struct BlockDataCopy
   {
      const uint16_t fileID = UINT16_MAX;
      const size_t offset = SIZE_MAX;
      const std::shared_ptr<Armory::FileUtils::FileCopy> data=nullptr;

      BlockDataCopy(const PathAndOffset&);
      BlockDataCopy(void);
      bool isValid(void) const { return fileID != UINT16_MAX; }
   };

private:
   std::atomic_uint64_t counter_;
   std::vector<PathAndOffset> paf_;

private:
   BlockDataLoader(const BlockDataLoader&) = delete; //no copies
   std::shared_ptr<Armory::FileUtils::BlockDataFileMap>
   getNewBlockDataMap(uint32_t);

public:
   BlockDataLoader(std::shared_ptr<BlockFiles>, const BlockOffset&);
   BlockDataLoader(std::shared_ptr<BlockFiles>, const std::set<uint32_t>&);

   std::shared_ptr<Armory::FileUtils::FileMap> getNextMap(void);
   BlockDataCopy getNextCopy(void);
   size_t size(void) const;
   bool isValid(void) const;
};
