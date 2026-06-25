////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <vector>
#include <set>
#include <map>
#include <unordered_map>

#include <Utils/BinaryData.h>

struct BlockHashVector;
class BCTX;

namespace Armory
{
   namespace FileUtils
   {
      class FileCopy;
      class FileMap;
      class BlockDataFileMap;
   }
   class BlockHeader;
   struct Hash32;
   class BlockOffset;
}

struct BlockDataExhausted
{};

////////////////////////////////////////////////////////////////////////////////
class BlockData
{
private:
   const std::shared_ptr<Armory::BlockHeader> headerPtr_;
   const uint8_t* data_;
   const size_t size_;

   std::vector<std::shared_ptr<BCTX>> txns_;
   std::shared_ptr<BlockHashVector> txFilter_;

public:
   enum class CheckHashes : int
   {
      NoChecks,
      MerkleOnly,
      TxFilters,
      FullHints
   };
   std::vector<BinaryData> allTxHashes;

private:
   BlockData(const std::shared_ptr<Armory::BlockHeader>, const uint8_t*, size_t);
   void serializeTxHints(const std::vector<BinaryData>&);

public:
   static std::shared_ptr<BlockData> deserialize(
      const uint8_t*, size_t,
      const std::shared_ptr<Armory::BlockHeader>,
      CheckHashes);

   std::shared_ptr<Armory::BlockHeader> getHeaderPtr(void) const;
   uint32_t uniqueID(void) const;
   size_t size(void) const;
   const Armory::Hash32& getHash(void) const;

   const std::vector<std::shared_ptr<BCTX>>& getTxns(void) const;
   void computeTxFilter(const std::vector<BinaryData>&);
   std::shared_ptr<BlockHashVector> getTxFilter(void) const;
};

/////////////////////////////////////////////////////////////////////////////
class BlockFiles
{
   friend class BlockDataLoader;

public:
   struct FileStat
   {
      const std::filesystem::path path;
      const size_t size;
   };

private:
   std::map<uint16_t, FileStat> paths_;
   const std::filesystem::path folderPath_;
   size_t totalBlockchainBytes_ = 0;

public:
   BlockFiles(const std::filesystem::path&);

   void detectAllBlockFiles(void);
   void detectNewBlockFiles(void);
   uint16_t getFirstID(void) const;
   const std::filesystem::path& folderPath(void) const;
   unsigned fileCount(void) const;
   const std::filesystem::path& getLastFilePath(void) const;
   const std::filesystem::path& getFilePathForID(uint16_t) const;
};

/////////////////////////////////////////////////////////////////////////////
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
      const uint16_t fileID;
      const size_t offset;
      const std::shared_ptr<Armory::FileUtils::FileCopy> data;

      BlockDataCopy(void);
      BlockDataCopy(const PathAndOffset&);
      BlockDataCopy(const BlockDataCopy&);
      bool isValid(void) const;
   };

private:
   std::atomic_uint64_t counter_;
   std::vector<PathAndOffset> paf_;

private:
   BlockDataLoader(const BlockDataLoader&) = delete; //no copies

public:
   BlockDataLoader(std::shared_ptr<BlockFiles>,
      const Armory::BlockOffset&, size_t = SIZE_MAX);
   BlockDataLoader(std::shared_ptr<BlockFiles>, const std::set<uint16_t>&);

   std::shared_ptr<Armory::FileUtils::FileMap> getNextMap(void);
   BlockDataCopy getNextCopy(void);
   size_t size(void) const;
   bool isValid(void) const;
   uint16_t getFirstFileID(void) const;
};
