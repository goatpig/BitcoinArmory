////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2024, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _BLOCKDATAMAP_H
#define _BLOCKDATAMAP_H

#include <stdint.h>

#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <future>
#include <atomic>
#include <map>
#include <filesystem>

#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>


#include "BlockObj.h"
#include "BinaryData.h"

#define OffsetAndSize std::pair<size_t, size_t>
struct BlockHashVector;

////////////////////////////////////////////////////////////////////////////////
struct BCTX
{
private:
   mutable BinaryData txHash_;

public:
   const uint8_t* data_;
   const size_t size_;

   uint32_t version_;
   uint32_t lockTime_;

   bool usesWitness_ = false;

   std::vector<OffsetAndSize> txins_;
   std::vector<OffsetAndSize> txouts_;
   std::vector<OffsetAndSize> witnesses_;

   bool isCoinbase_ = false;

   BCTX(const uint8_t* data, size_t size) :
      data_(data), size_(size)
   {}

   BCTX(const BinaryDataRef& bdr) :
      data_(bdr.getPtr()), size_(bdr.getSize())
   {}

   const BinaryData& getHash(void) const
   {
      if(txHash_.getSize() == 0)
      {
         if (usesWitness_)
         {
            BinaryData noWitData;
            BinaryDataRef version(data_, 4);
           
            auto& lastTxOut = txouts_.back();
            auto witnessOffset = lastTxOut.first + lastTxOut.second;
            BinaryDataRef txinout(data_ + 6, witnessOffset - 6);
            BinaryDataRef locktime(data_ + size_ - 4, 4);
           
            noWitData.append(version);
            noWitData.append(txinout);
            noWitData.append(locktime);
            
            BtcUtils::getHash256(noWitData, txHash_);
         }
         else
         {
            BinaryDataRef hashdata(data_, size_);
            BtcUtils::getHash256(hashdata, txHash_);
         }
      }

      return txHash_;
   }

   BinaryData&& moveHash(void)
   {
      getHash();

      return std::move(txHash_);
   }

   BinaryDataRef getTxInRef(unsigned inputId) const
   {
      if (inputId >= txins_.size())
         throw std::range_error("txin index overflow");

      auto txinIter = txins_.cbegin() + inputId;

      return BinaryDataRef(data_ + (*txinIter).first,
         (*txinIter).second);
   }

   BinaryDataRef getTxOutRef(unsigned outputId) const
   {
      if (outputId >= txouts_.size())
         throw std::range_error("txout index overflow");

      auto txoutIter = txouts_.cbegin() + outputId;

      return BinaryDataRef(data_ + (*txoutIter).first,
         (*txoutIter).second);
   }

   static std::shared_ptr<BCTX> parse(
      BinaryRefReader brr, unsigned id = UINT32_MAX)
   {
      return parse(brr.getCurrPtr(), brr.getSizeRemaining(), id);
   }

   static std::shared_ptr<BCTX> parse(
      const uint8_t* data, size_t len, unsigned id=UINT32_MAX)
   {
      std::vector<size_t> offsetIns, offsetOuts, offsetsWitness;
      auto txlen = BtcUtils::TxCalcLength(
         data, len,
         &offsetIns, &offsetOuts, &offsetsWitness);

      auto txPtr = std::make_shared<BCTX>(data, txlen);

      //create BCTX object and fill it up
      txPtr->version_ = READ_UINT32_LE(data);

      // Check the marker and flag for witness transaction
      txPtr->usesWitness_ = BtcUtils::checkSwMarker(data + 4);

      //convert offsets to offset + size pairs
      for (unsigned int y = 0; y < offsetIns.size() - 1; y++)
         txPtr->txins_.push_back(
            std::make_pair(
         offsetIns[y],
         offsetIns[y + 1] - offsetIns[y]));

      for (unsigned int y = 0; y < offsetOuts.size() - 1; y++)
         txPtr->txouts_.push_back(
            std::make_pair(
         offsetOuts[y],
         offsetOuts[y + 1] - offsetOuts[y]));

      if (txPtr->usesWitness_)
      {
         for (unsigned int y = 0; y < offsetsWitness.size() - 1; y++)
            txPtr->witnesses_.push_back(
               std::make_pair(
            offsetsWitness[y],
            offsetsWitness[y + 1] - offsetsWitness[y]));
      }

      txPtr->lockTime_ = READ_UINT32_LE(data + offsetsWitness.back());

      if (id != UINT32_MAX)
      {
         txPtr->isCoinbase_ = (id == 0);
      }
      else if (txPtr->txins_.size() == 1)
      {
         auto txinref = txPtr->getTxInRef(0);
         auto bdr = txinref.getSliceRef(0, 32);
         if (bdr == BtcUtils::EmptyHash_)
            txPtr->isCoinbase_ = true;
      }

      return txPtr;
   }
};


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
   enum class CheckHashes
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
   std::shared_ptr<BlockHeader> getHeaderPtr(void) const { return headerPtr_; }
};

/////////////////////////////////////////////////////////////////////////////
struct BlockOffset
{
   uint16_t fileID_;
   size_t offset_;

   BlockOffset(uint16_t fileID, size_t offset) :
      fileID_(fileID), offset_(offset)
   {}

   BlockOffset(const BlockOffset& bo) :
      fileID_(bo.fileID_), offset_(bo.offset_)
   {}

   bool operator>(const BlockOffset& rhs)
   {
      if (fileID_ == rhs.fileID_)
         return offset_ > rhs.offset_;

      return fileID_ > rhs.fileID_;
   }

   BlockOffset& operator=(const BlockOffset& rhs)
   {
      if (this != &rhs)
      {
         this->fileID_ = rhs.fileID_;
         this->offset_ = rhs.offset_;
      }

      return *this;
   }
};

/////////////////////////////////////////////////////////////////////////////
class BlockFiles
{
private:
   std::map<uint32_t, std::filesystem::path> filePaths_;
   const std::filesystem::path folderPath_;
   size_t totalBlockchainBytes_ = 0;

public:
   BlockFiles(const std::filesystem::path& folderPath) :
      folderPath_(folderPath)
   {}

   void detectAllBlockFiles(void);
   const std::filesystem::path& folderPath(void) const { return folderPath_; }
   unsigned fileCount(void) const { return filePaths_.size(); }
   const std::filesystem::path& getLastFileName(void) const;
};

/////////////////////////////////////////////////////////////////////////////
namespace FileUtils {
   class FileMap;
};

class BlockDataFileMap
{
private:
   FileUtils::FileMap fileMap_;
   std::atomic<int> useCounter_;

public:
   BlockDataFileMap(const std::filesystem::path& filename);
   ~BlockDataFileMap(void);

   bool valid(void) const;
   const uint8_t* data(void) const;
   size_t size(void) const;
};

/////////////////////////////////////////////////////////////////////////////
class BlockDataLoader
{
private:
   const std::filesystem::path path_;
   const std::string prefix_;

private:

   BlockDataLoader(const BlockDataLoader&) = delete; //no copies

   uint32_t nameToIntID(const std::filesystem::path& filename);
   std::shared_ptr<BlockDataFileMap>
      getNewBlockDataMap(uint32_t fileid);

public:
   BlockDataLoader(const std::filesystem::path& path);

   ~BlockDataLoader(void)
   {}

   std::shared_ptr<BlockDataFileMap> get(const std::filesystem::path&);
   std::shared_ptr<BlockDataFileMap> get(uint32_t fileid);
};

#endif
