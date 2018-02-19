////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BlockDataMap.h"
#include "BtcUtils.h"

#ifndef _WIN32
#include <sys/mman.h>
#endif

////////////////////////////////////////////////////////////////////////////////
void BlockData::deserialize(const uint8_t* data, size_t size,
   const shared_ptr<BlockHeader> blockHeader,
   function<unsigned int(const BinaryData&)> getID, 
   bool checkMerkle, bool keepHashes)
{
   headerPtr_ = blockHeader;

   //deser header from raw block and run a quick sanity check
   if (size < HEADER_SIZE)
      throw BlockDeserializingException(
      "raw data is smaller than HEADER_SIZE");

   BinaryDataRef bdr(data, HEADER_SIZE);
   BlockHeader bh(bdr);

   blockHash_ = bh.thisHash_;

   BinaryRefReader brr(data + HEADER_SIZE, size - HEADER_SIZE);
   auto numTx = (unsigned)brr.get_var_int();

   if (blockHeader != nullptr)
   {
      if (bh.getThisHashRef() != blockHeader->getThisHashRef())
         throw BlockDeserializingException(
         "raw data does not match expected block hash");

      if (numTx != blockHeader->getNumTx())
         throw BlockDeserializingException(
         "tx count mismatch in deser header");
   }

   for (unsigned i = 0; i < numTx; i++)
   {
      //light tx deserialization, just figure out the offset and size of
      //txins and txouts
      auto tx = BCTX::parse(brr);
      brr.advance(tx->size_);

      //move it to BlockData object vector
      txns_.push_back(move(tx));
   }

   data_ = data;
   size_ = size;

   if (!checkMerkle)
      return;

   //let's check the merkle root
   vector<BinaryData> allhashes;
   for (auto& txn : txns_)
   {
      if (!keepHashes)
      {
         auto txhash = txn->moveHash();
         allhashes.push_back(move(txhash));
      }
      else
      {
         txn->getHash();
         allhashes.push_back(txn->txHash_);
      }
   }

   auto&& merkleroot = BtcUtils::calculateMerkleRoot(allhashes);
   if (merkleroot != bh.getMerkleRoot())
   {
      LOGERR << "merkle root mismatch!";
      LOGERR << "   header has: " << bh.getMerkleRoot().toHexStr();
      LOGERR << "   block yields: " << merkleroot.toHexStr();
      throw BlockDeserializingException("invalid merkle root");
   }

   uniqueID_ = getID(bh.getThisHash());

   txFilter_ = move(computeTxFilter(allhashes));
}

/////////////////////////////////////////////////////////////////////////////
TxFilter<TxFilterType> 
BlockData::computeTxFilter(const vector<BinaryData>& allHashes) const
{
   TxFilter<TxFilterType> txFilter(uniqueID_, allHashes.size());
   txFilter.update(allHashes);

   return move(txFilter);
}

/////////////////////////////////////////////////////////////////////////////
shared_ptr<BlockHeader> BlockData::createBlockHeader(void) const
{
   if (headerPtr_ != nullptr)
      return headerPtr_;

   auto bhPtr = make_shared<BlockHeader>();
   auto& bh = *bhPtr;

   bh.dataCopy_ = move(BinaryData(data_, HEADER_SIZE));

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
   if (folderPath_.size() == 0)
      throw runtime_error("empty block files folder path");

   unsigned numBlkFiles = filePaths_.size();

   while (numBlkFiles < UINT16_MAX)
   {
      string path = BtcUtils::getBlkFilename(folderPath_, numBlkFiles);
      uint64_t filesize = BtcUtils::GetFileSize(path);
      if (filesize == FILE_DOES_NOT_EXIST)
         break;

      filePaths_.insert(make_pair(numBlkFiles, path));

      totalBlockchainBytes_ += filesize;
      numBlkFiles++;
   }
}

/////////////////////////////////////////////////////////////////////////////
BlockDataLoader::BlockDataLoader(const string& path) :
   path_(path), prefix_("blk")
{}

/////////////////////////////////////////////////////////////////////////////
shared_ptr<BlockDataFileMap> BlockDataLoader::get(const string& filename)
{
   //convert to int ID
   auto intID = nameToIntID(filename);

   //get with int ID
   return get(intID);
}

/////////////////////////////////////////////////////////////////////////////
shared_ptr<BlockDataFileMap> BlockDataLoader::get(uint32_t fileid)
{
   //don't have this fileid yet, create it
   return getNewBlockDataMap(fileid);
}

/////////////////////////////////////////////////////////////////////////////
uint32_t BlockDataLoader::nameToIntID(const string& filename)
{
   if (filename.size() < 3 ||
      strncmp(prefix_.c_str(), filename.c_str(), 3))
      throw runtime_error("invalid filename");

   auto&& substr = filename.substr(3);
   return stoi(substr);
}

/////////////////////////////////////////////////////////////////////////////
string BlockDataLoader::intIDToName(uint32_t fileid)
{
   stringstream filename;

   filename << path_ << "/blk";
   filename << setw(5) << setfill('0') << fileid;
   filename << ".dat";

   return filename.str();
}

/////////////////////////////////////////////////////////////////////////////
shared_ptr<BlockDataFileMap>
   BlockDataLoader::getNewBlockDataMap(uint32_t fileid)
{
   string filename = move(intIDToName(fileid));

   return make_shared<BlockDataFileMap>(filename);
}

/////////////////////////////////////////////////////////////////////////////
BlockDataFileMap::BlockDataFileMap(const string& filename)
{
   //relaxed memory order for loads and stores, we only care about 
   //atomicity in these operations
   useCounter_.store(0, memory_order_relaxed);

   try
   {
      auto filemap = DBUtils::getMmapOfFile(filename);
      fileMap_ = filemap.filePtr_;
      size_ = filemap.size_;
   }
   catch (exception&)
   {
      //LOGERR << "Failed to create BlockDataMap with error: " << e.what();
   }
}

/////////////////////////////////////////////////////////////////////////////
BlockDataFileMap::~BlockDataFileMap()
{
   //close file mmap
   if (fileMap_ != nullptr)
   {
#ifdef _WIN32
      UnmapViewOfFile(fileMap_);
#else
      munmap(fileMap_, size_);
#endif
      fileMap_ = nullptr;
   }
}
