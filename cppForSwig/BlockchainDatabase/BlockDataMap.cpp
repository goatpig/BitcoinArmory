////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2024, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BlockDataMap.h"
#include "BtcUtils.h"
#include "TxHashFilters.h"
#include <string_view>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace {
   fs::path blkFileExt{".dat"sv};
   auto blkFilePrefix = "blk"sv;

   std::shared_ptr<FileUtils::FileCopy> getFileCopy(
      uint16_t id, const BlockDataLoader::PathAndOffset& path)
   {
      if (id == UINT16_MAX) {
         return nullptr;
      }
      return std::make_shared<FileUtils::FileCopy>(path.path, path.offset);
   }
}

////////////////////////////////////////////////////////////////////////////////
// BlockOffset
////////////////////////////////////////////////////////////////////////////////
BlockOffset::BlockOffset() :
   fileID(UINT16_MAX), offset(0)
{}

BlockOffset::BlockOffset(uint16_t fileID, size_t offset) :
   fileID(fileID), offset(offset)
{}

BlockOffset::BlockOffset(const BlockOffset& bo) :
   fileID(bo.fileID), offset(bo.offset)
{}

bool BlockOffset::operator>(const BlockOffset& rhs) const
{
   if (fileID == UINT16_MAX) {
      if (rhs.fileID == UINT16_MAX) {
         return false;
      }
      return true;
   } else if (fileID == rhs.fileID) {
      return offset > rhs.offset;
   }
   return fileID > rhs.fileID;
}

BlockOffset& BlockOffset::operator=(const BlockOffset& rhs)
{
   this->fileID = rhs.fileID;
   this->offset = rhs.offset;
   return *this;
}

////////////////////////////////////////////////////////////////////////////////
// BlockData
////////////////////////////////////////////////////////////////////////////////
BlockData::BlockData(uint32_t blockid)
   : uniqueID_(blockid)
{}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<BlockData> BlockData::deserialize(
   const uint8_t* data, size_t size,
   const std::shared_ptr<BlockHeader> blockHeader,
   std::function<unsigned int(const BinaryData&)> getID,
   BlockData::CheckHashes mode)
{
   //deser header from raw block and run a quick sanity check
   if (size < HEADER_SIZE) {
      throw BlockDeserializingException(
         "raw data is smaller than HEADER_SIZE");
   }

   BinaryDataRef bdr(data, HEADER_SIZE);
   BlockHeader bh(bdr);

   auto uniqueID = UINT32_MAX;
   if (getID) {
      uniqueID = getID(bh.getThisHash());
   }

   auto result = std::make_shared<BlockData>(uniqueID);
   result->headerPtr_ = blockHeader;
   result->blockHash_ = bh.thisHash_;

   BinaryRefReader brr(data + HEADER_SIZE, size - HEADER_SIZE);
   auto numTx = (unsigned)brr.get_var_int();

   if (blockHeader != nullptr) {
      if (bh.getThisHashRef() != blockHeader->getThisHashRef()) {
         throw BlockDeserializingException(
            "raw data does not match expected block hash");
      }

      if (numTx != blockHeader->getNumTx()) {
         throw BlockDeserializingException(
            "tx count mismatch in deser header");
      }
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

   result->data_ = data;
   result->size_ = size;

   std::vector<BinaryData> allHashes;
   switch (mode)
   {
      case CheckHashes::NoChecks:
         return result;

      case CheckHashes::MerkleOnly:
      case CheckHashes::TxFilters:
      {
         allHashes.reserve(result->txns_.size());
         for (auto& txn : result->txns_) {
            auto txhash = txn->moveHash();
            allHashes.emplace_back(std::move(txhash));
         }
         break;
      }

      case CheckHashes::FullHints:
      {
         allHashes.reserve(result->txns_.size());
         for (const auto& txn : result->txns_) {
            const auto& txhash = txn->getHash();
            allHashes.emplace_back(txhash);
         }
         break;
      }
   }

   //any form of later txhash filtering implies we check the merkle
   //root, otherwise we would have no guarantees the hashes are valid
   auto merkleroot = BtcUtils::calculateMerkleRoot(allHashes);
   if (merkleroot != bh.getMerkleRoot()) {
      LOGERR << "merkle root mismatch!";
      LOGERR << "   header has: " << bh.getMerkleRoot().toHexStr();
      LOGERR << "   block yields: " << merkleroot.toHexStr();
      throw BlockDeserializingException("invalid merkle root");
   }

   if (mode == CheckHashes::TxFilters) {
      result->computeTxFilter(allHashes);
   }
   return result;
}

/////////////////////////////////////////////////////////////////////////////
void BlockData::computeTxFilter(const std::vector<BinaryData>& allHashes)
{
   if (txFilter_ == nullptr) {
      txFilter_ = std::make_shared<BlockHashVector>(uniqueID_);
      txFilter_->isValid_ = true;
   }
   txFilter_->update(allHashes);
}

////
std::shared_ptr<BlockHashVector> BlockData::getTxFilter() const
{
   return txFilter_;
}

/////////////////////////////////////////////////////////////////////////////
std::shared_ptr<BlockHeader> BlockData::createBlockHeader() const
{
   if (headerPtr_ != nullptr) {
      return headerPtr_;
   }

   auto bhPtr = std::make_shared<BlockHeader>();
   auto& bh = *bhPtr;

   bh.dataCopy_ = std::move(BinaryData(data_, HEADER_SIZE));
   bh.difficultyDbl_ = BtcUtils::convertDiffBitsToDouble(
      BinaryDataRef(data_ + 72, 4));

   bh.isInitialized_ = true;
   bh.nextHash_ = BinaryData(0);
   bh.blockHeight_ = UINT32_MAX;
   bh.difficultySum_ = -1;
   bh.isMainBranch_ = false;
   bh.isOrphan_ = true;
   
   bh.numBlockBytes_ = size_;
   bh.numTx_ = txns_.size();

   bh.blkFileNum_ = fileID_;
   bh.blkFileOffset_ = offset_;
   bh.thisHash_ = blockHash_;
   bh.uniqueID_ = uniqueID_;

   return bhPtr;
}

////////////////////////////////////////////////////////////////////////////////
// BlockFiles
////////////////////////////////////////////////////////////////////////////////
void BlockFiles::detectAllBlockFiles()
{
   if (folderPath_.empty()) {
      throw std::runtime_error("empty block files folder path");
   }

   for (const auto& entry : fs::directory_iterator{folderPath_}) {
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

         paths_.emplace(fileId, filePath);
         totalBlockchainBytes_ += filesize;
      } catch (const std::exception&) {
         continue;
      }
   }
}

void BlockFiles::detectNewBlockFiles()
{
   //we expect consecutive new block files
   auto lastFilePath = getLastFilePath();
   auto fileID = FileUtils::blkPathToIntID(lastFilePath);
   while (++fileID < UINT16_MAX) {
      auto filePath = FileUtils::getBlkFilename(folderPath_, fileID);
      auto fileSize = FileUtils::getFileSize(filePath);
      if (fileSize == SIZE_MAX) {
         break;
      }

      paths_.emplace(fileID, filePath);
      totalBlockchainBytes_ += fileSize;
   }
}

/////////////////////////////////////////////////////////////////////////////
const fs::path& BlockFiles::getLastFilePath() const
{
   if (paths_.empty()) {
      throw std::runtime_error("empty path map");
   }
   return paths_.rbegin()->second;
}

/////////////////////////////////////////////////////////////////////////////
const fs::path& BlockFiles::getFilePathForID(uint16_t fileID) const
{
   auto iter = paths_.find(fileID);
   if (iter == paths_.end()) {
      LOGERR << "no file path for id " << fileID;
      throw std::range_error("unexpected fileID");
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
// BlockDataLoader
////////////////////////////////////////////////////////////////////////////////
BlockDataLoader::BlockDataLoader(
   std::shared_ptr<BlockFiles> files,
   const BlockOffset& startOffset)
{
   counter_.store(0, std::memory_order_relaxed);
   auto iter = files->paths_.begin();
   if (startOffset.fileID != UINT16_MAX) {
      iter = files->paths_.find(startOffset.fileID);
   }

   if (iter == files->paths_.end()) {
      throw std::runtime_error("could not find first file index!");
   }

   paf_.emplace_back(PathAndOffset{iter->second, startOffset.offset});
   while (++iter != files->paths_.end()) {
      paf_.emplace_back(PathAndOffset{iter->second, 0});
   }
}

/////////////////////////////////////////////////////////////////////////////
std::shared_ptr<FileUtils::FileMap> BlockDataLoader::getNextMap()
{
   auto id = counter_.fetch_add(1, std::memory_order_relaxed);
   if (id >= paf_.size()) {
      return nullptr;
   }

   const auto& file = paf_[id];
   return std::make_shared<FileUtils::FileMap>(file.path, file.offset);
}

/////////////////////////////////////////////////////////////////////////////
BlockDataLoader::BlockDataCopy BlockDataLoader::getNextCopy()
{
   uint16_t id = counter_.fetch_add(1, std::memory_order_relaxed);
   if (id >= paf_.size()) {
      return BlockDataCopy{UINT16_MAX, {}};
   }
   const auto& file = paf_[id];
   return { id, file };
}

/////////////////////////////////////////////////////////////////////////////
size_t BlockDataLoader::size() const
{
   return paf_.size();
}

bool BlockDataLoader::isValid() const
{
   auto counter = counter_.load(std::memory_order_relaxed);
   return counter < paf_.size();
}

/////////////////////////////////////////////////////////////////////////////
// BlockDataCopy
/////////////////////////////////////////////////////////////////////////////
BlockDataLoader::BlockDataCopy::BlockDataCopy(uint16_t id,
   const PathAndOffset& path) :
   fileID(id), offset(path.offset),
   data(getFileCopy(id, path))
{}

/////////////////////////////////////////////////////////////////////////////
// BlockDataFileMap
/////////////////////////////////////////////////////////////////////////////
BlockDataFileMap::BlockDataFileMap(const std::filesystem::path& path) :
   fileMap_(path)
{}

////
BlockDataFileMap::~BlockDataFileMap()
{}

////
const uint8_t* BlockDataFileMap::data() const
{
   return fileMap_.ptr();
}

////
size_t BlockDataFileMap::size() const
{
   return fileMap_.size();
}

////
bool BlockDataFileMap::valid() const
{
   return fileMap_.isValid();
}