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

namespace fs = std::filesystem;

////////////////////////////////////////////////////////////////////////////////
BlockData::BlockData(uint32_t blockid)
   : uniqueID_(blockid)
{}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<BlockData> BlockData::deserialize(const uint8_t* data, size_t size,
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

   for (unsigned i = 0; i < numTx; i++) {
      //light tx deserialization, just figure out the offset and size of
      //txins and txouts
      auto tx = BCTX::parse(brr);
      brr.advance(tx->size_);

      //move it to BlockData object vector
      result->txns_.push_back(move(tx));
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

/////////////////////////////////////////////////////////////////////////////
void BlockFiles::detectAllBlockFiles()
{
   if (folderPath_.empty()) {
      throw std::runtime_error("empty block files folder path");
   }

   unsigned numBlkFiles = filePaths_.size();
   while (numBlkFiles < UINT16_MAX) {
      auto path = FileUtils::getBlkFilename(folderPath_, numBlkFiles);
      auto filesize = FileUtils::getFileSize(path);
      if (filesize == SIZE_MAX) {
         break;
      }

      filePaths_.emplace(numBlkFiles, path);
      totalBlockchainBytes_ += filesize;
      numBlkFiles++;
   }
}

/////////////////////////////////////////////////////////////////////////////
const fs::path& BlockFiles::getLastFileName(void) const
{
   if (filePaths_.empty()) {
      throw std::runtime_error("empty path map");
   }
   return filePaths_.rbegin()->second;
}

/////////////////////////////////////////////////////////////////////////////
BlockDataLoader::BlockDataLoader(const fs::path& path) :
   path_(path), prefix_("blk")
{}

/////////////////////////////////////////////////////////////////////////////
std::shared_ptr<BlockDataFileMap> BlockDataLoader::get(
   const fs::path& filename)
{
   //convert to int ID
   auto intID = nameToIntID(filename);

   //get with int ID
   return get(intID);
}

/////////////////////////////////////////////////////////////////////////////
std::shared_ptr<BlockDataFileMap> BlockDataLoader::get(uint32_t fileid)
{
   //don't have this fileid yet, create it
   return getNewBlockDataMap(fileid);
}

/////////////////////////////////////////////////////////////////////////////
uint32_t BlockDataLoader::nameToIntID(const fs::path& path)
{
   auto filename = path.filename().string();
   if (filename.size() < 3 ||
      strncmp(prefix_.c_str(), filename.c_str(), 3)) {
      throw std::runtime_error("invalid filename");
   }

   auto substr = filename.substr(3);
   return std::stoi(substr);
}

/////////////////////////////////////////////////////////////////////////////
std::shared_ptr<BlockDataFileMap>
   BlockDataLoader::getNewBlockDataMap(uint32_t fileid)
{
   auto filename = FileUtils::getBlkFilename(path_, fileid);
   return std::make_shared<BlockDataFileMap>(filename);
}

/////////////////////////////////////////////////////////////////////////////
// BlockDataFileMap
/////////////////////////////////////////////////////////////////////////////
BlockDataFileMap::BlockDataFileMap(const std::filesystem::path& path) :
   fileMap_(path)
{
   //relaxed memory order for loads and stores, we only care about 
   //atomicity in these operations
   useCounter_.store(0,std:: memory_order_relaxed);
}

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