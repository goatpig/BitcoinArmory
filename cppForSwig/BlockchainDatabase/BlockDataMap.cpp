////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <string_view>
#include <cstring>

#include "BlockDataMap.h"
#include <Utils/BtcUtils.h>
#include <Utils/varint.h>
#include <Utils/BCTX.h>
#include <Utils/FileUtils.h>
#include <Utils/ArmoryConfig.h>
#include "TxHashFilters.h"
#include "BlockObj.h"

using namespace std::string_view_literals;
using namespace Armory;

namespace {
   std::filesystem::path blkFileExt{".dat"sv};
   auto blkFilePrefix = "blk"sv;

   std::shared_ptr<FileUtils::FileCopy> getFileCopy(
      const BlockDataLoader::PathAndOffset& path)
   {
      if (path.fileID == UINT16_MAX) {
         return nullptr;
      }
      return std::make_shared<FileUtils::FileCopy>(path.path, path.offset);
   }

   constexpr std::string_view xorFileName{"xor.dat"sv};
}

////////////////////////////////////////////////////////////////////////////////
// BlockData
BlockData::BlockData(const std::shared_ptr<Armory::BlockHeader> header,
   const uint8_t* ptr, size_t size)
   : headerPtr_(header), data_(ptr), size_(size)
{}

const std::vector<std::shared_ptr<BCTX>>& BlockData::getTxns() const
{
   return txns_;
}

size_t BlockData::size() const
{
   return size_;
}

const Hash32& BlockData::getHash() const
{
   return headerPtr_->getThisHash();
}

uint32_t BlockData::uniqueID() const
{
   return headerPtr_->getUniqueID();
}

std::shared_ptr<BlockHeader> BlockData::getHeaderPtr() const
{
   return headerPtr_;
}

////////
std::shared_ptr<BlockData> BlockData::deserialize(
   const uint8_t* data, size_t size,
   const std::shared_ptr<BlockHeader> blockHeader,
   BlockData::CheckHashes mode)
{
   //deser header from raw block and run a quick sanity check
   if (blockHeader == nullptr) {
      throw std::runtime_error("empty bhptr");
   }
   if (size < blockHeader->getBlockSize()) {
      throw BtcUtils::BlockDeserializingException(
         "block data is smaller than expected");
   }

   auto result = std::shared_ptr<BlockData>(new BlockData(
      blockHeader, data, blockHeader->getBlockSize()));

   BinaryRefReader brr(data + HEADER_SIZE, size - HEADER_SIZE);
   auto numTx = (unsigned)brr.get_var_int();
   if (numTx != blockHeader->getNumTx()) {
      throw BtcUtils::BlockDeserializingException(
         "tx count mismatch in deser header");
   }

   result->txns_.reserve(numTx);
   for (unsigned i = 0; i < numTx; i++) {
      //light tx deserialization, just figure out the offset and size of
      //txins and txouts
      auto tx = BCTX::parse(brr);
      brr.advance(tx->size_);

      //move it to BlockData object vector
      result->txns_.emplace_back(std::move(tx));
   }

   std::vector<BinaryData> allHashes;
   switch (mode)
   {
      case CheckHashes::NoChecks:
         return result;

      case CheckHashes::MerkleOnly:
      case CheckHashes::TxFilters:
      case CheckHashes::FullHints:
      {
         allHashes.reserve(result->txns_.size());
         for (auto& txn : result->txns_) {
            allHashes.emplace_back(txn->moveHash());
         }
         break;
      }
   }

   //any form of later txhash filtering implies we check the merkle
   //root, otherwise we would have no guarantees the hashes are valid
   auto merkleroot = BtcUtils::calculateMerkleRoot(allHashes);
   blockHeader->checkMerkleRoot(merkleroot);
   if (!blockHeader->isMerkleValid()) {
      LOGERR << "merkle root mismatch!";
      LOGERR << "   header has: " << blockHeader->getMerkleRoot().toHexStr();
      LOGERR << "   block yields: " << merkleroot.toHexStr();
      throw BtcUtils::BlockDeserializingException("invalid merkle root");
   }

   if (mode == CheckHashes::TxFilters) {
      result->computeTxFilter(allHashes);
   } else if (mode == CheckHashes::FullHints) {
      result->allTxHashes = std::move(allHashes);
   }
   return result;
}

////////
void BlockData::computeTxFilter(const std::vector<BinaryData>& allHashes)
{
   if (txFilter_ == nullptr) {
      txFilter_ = std::make_shared<BlockHashVector>(uniqueID());
      txFilter_->isValid_ = true;
   }
   txFilter_->update(allHashes);
}

std::shared_ptr<BlockHashVector> BlockData::getTxFilter() const
{
   return txFilter_;
}

////////////////////////////////////////////////////////////////////////////////
// BlockFiles
BlockFiles::BlockFiles(const std::filesystem::path& folderPath) :
   folderPath_(folderPath)
{}

const std::filesystem::path& BlockFiles::folderPath() const
{
   return folderPath_;
}

unsigned BlockFiles::fileCount() const
{
   return paths_.size();
}

////////
void BlockFiles::detectAllBlockFiles()
{
   if (folderPath_.empty()) {
      throw std::runtime_error("empty block files folder path");
   }

   for (const auto& entry : std::filesystem::directory_iterator{folderPath_}) {
      if (!entry.is_regular_file()) {
         continue;
      }

      const auto& filePath = entry.path();
      try {
         auto fileId = FileUtils::blkPathToIntID(filePath);
         auto filesize = FileUtils::getFileSize(filePath);
         if (filesize == SIZE_MAX) {
            continue;
         }

         paths_.emplace(fileId, FileStat{filePath, filesize});
         totalBlockchainBytes_ += filesize;
      } catch (const std::exception&) {
         if (filePath.filename().string() != xorFileName) {
            continue;
         }

         //this is the xor key, grab ita
         auto fileCopy = FileUtils::FileCopy(filePath);
         if (fileCopy.size() != 8) {
            LOGWARN << "Found a xor key but it's not 8 bytes";
            continue;
         }
         uint64_t xorkey;
         std::memcpy(&xorkey, fileCopy.ptr(), 8);
         if (xorkey != 0) {
            LOGINFO << "found a xor key";
            Config::DBSettings::setXorKey(xorkey);
         }
      }
   }
}

void BlockFiles::detectNewBlockFiles()
{
   //we expect consecutive new block files
   auto lastFilePath = getLastFilePath();
   auto fileID = FileUtils::blkPathToIntID(lastFilePath);

   //check if last known file has grown
   auto filePath = FileUtils::getBlkFilename(folderPath_, fileID);
   auto fileSize = FileUtils::getFileSize(filePath);
   auto iter = paths_.find(fileID);
   if (iter->second.size != fileSize) {
      totalBlockchainBytes_ -= iter->second.size;
      totalBlockchainBytes_ += fileSize;
      paths_.erase(iter);
      paths_.emplace(fileID, FileStat{filePath, fileSize});
   }

   while (++fileID < UINT16_MAX) {
      auto filePath = FileUtils::getBlkFilename(folderPath_, fileID);
      auto fileSize = FileUtils::getFileSize(filePath);
      if (fileSize == SIZE_MAX) {
         break;
      }

      paths_.emplace(fileID, FileStat{filePath, fileSize});
      totalBlockchainBytes_ += fileSize;
   }
}

////////
const std::filesystem::path& BlockFiles::getLastFilePath() const
{
   if (paths_.empty()) {
      throw std::runtime_error("empty path map");
   }
   return paths_.rbegin()->second.path;
}

const std::filesystem::path& BlockFiles::getFilePathForID(
   uint16_t fileID) const
{
   auto iter = paths_.find(fileID);
   if (iter == paths_.end()) {
      LOGERR << "no file path for id " << fileID;
      throw std::range_error("unexpected fileID");
   }
   return iter->second.path;
}

uint16_t BlockFiles::getFirstID() const
{
   if (paths_.empty()) {
      throw std::range_error("empty paths");
   }
   return paths_.begin()->first;
}

////////////////////////////////////////////////////////////////////////////////
// BlockDataLoader
BlockDataLoader::BlockDataLoader(
   std::shared_ptr<BlockFiles> files,
   const BlockOffset& startBO, size_t maxCount)
{
   counter_.store(0, std::memory_order_relaxed);
   auto startOffset = startBO.offset();
   auto iter = files->paths_.find(startBO.fileID());
   if (iter == files->paths_.end()) {
      throw std::runtime_error("could not find first file index!");
   }

   if (startBO.offset() >= iter->second.size) {
      ++iter;
      if (iter == files->paths_.end()) {
         throw BlockDataExhausted();
      }
      startOffset = 0;
   }

   paf_.emplace_back(PathAndOffset{iter->second.path,
      iter->first, startOffset});
   while (++iter != files->paths_.end() && paf_.size() < maxCount) {
      paf_.emplace_back(PathAndOffset{iter->second.path, iter->first, 0});
   }
}

BlockDataLoader::BlockDataLoader(std::shared_ptr<BlockFiles> files,
   const std::set<uint16_t>& fileIDs)
{
   counter_.store(0, std::memory_order_relaxed);
   for (const auto& fileID : fileIDs) {
      auto iter = files->paths_.find(fileID);
      if (iter == files->paths_.end()) {
         //this bdl ctor is permissive, simply ignore ids for which there
         //is no associated file
         continue;
      }
      paf_.emplace_back(PathAndOffset{iter->second.path, fileID, 0});
   }
}

////////
std::shared_ptr<FileUtils::FileMap> BlockDataLoader::getNextMap()
{
   auto id = counter_.fetch_add(1, std::memory_order_relaxed);
   if (id >= paf_.size()) {
      return nullptr;
   }

   const auto& file = paf_[id];
   return std::make_shared<FileUtils::FileMap>(file.path, file.offset);
}

BlockDataLoader::BlockDataCopy BlockDataLoader::getNextCopy()
{
   uint16_t id = counter_.fetch_add(1, std::memory_order_relaxed);
   if (id >= paf_.size()) {
      return {};
   }
   const auto& file = paf_[id];
   return {file};
}

////////
size_t BlockDataLoader::size() const
{
   return paf_.size();
}

bool BlockDataLoader::isValid() const
{
   auto counter = counter_.load(std::memory_order_relaxed);
   return counter < paf_.size();
}

uint16_t BlockDataLoader::getFirstFileID() const
{
   return paf_[0].fileID;
}

/////////////////////////////////////////////////////////////////////////////
// BlockDataCopy
BlockDataLoader::BlockDataCopy::BlockDataCopy() :
   fileID{UINT16_MAX}, offset{SIZE_MAX}, data{nullptr}
{}

BlockDataLoader::BlockDataCopy::BlockDataCopy(const PathAndOffset& path) :
   fileID(path.fileID), offset(path.offset),
   data(getFileCopy(path))
{
   if (Config::DBSettings::isXored()) {
      data->xorMe(Config::DBSettings::getXorKey());
   }
}

BlockDataLoader::BlockDataCopy::BlockDataCopy(const BlockDataCopy& bdc) :
   fileID(bdc.fileID), offset(bdc.offset), data(bdc.data)
{}

bool BlockDataLoader::BlockDataCopy::isValid() const
{
   return fileID != UINT16_MAX;
}
