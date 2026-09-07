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

#include <iostream>
#include <cstring>
#include <cassert>

#include "BlockObj.h"
#include <Utils/BtcUtils.h>
#include <Utils/varint.h>
#include <Utils/DBUtils.h>
#include <Utils/Cryptography.h>

using namespace Armory;

////////////////////////////////////////////////////////////////////////////////
// BlockOffset
BlockOffset::BlockOffset(uint16_t fid, size_t off) :
   fileID_(fid), offset_(off)
{}

BlockOffset::BlockOffset(const BlockOffset& bo) :
   fileID_(bo.fileID_), offset_(bo.offset_)
{}

bool BlockOffset::operator<(const BlockOffset& rhs) const
{
   if (fileID_ < rhs.fileID_) {
      return true;
   }
   return offset_ < rhs.offset_;
}

BlockOffset& BlockOffset::operator=(const BlockOffset& rhs)
{
   this->fileID_ = rhs.fileID_;
   this->offset_ = rhs.offset_;
   return *this;
}

bool BlockOffset::isValid() const
{
   return fileID_ != UINT16_MAX && offset_ != SIZE_MAX;
}

uint16_t BlockOffset::fileID() const
{
   return fileID_;
}

size_t BlockOffset::offset() const
{
   return offset_;
}

////////////////////////////////////////////////////////////////////////////////
// Hash32
Hash32::Hash32()
{
   std::memset(data, 0, 32);
}

Hash32::Hash32(const BinaryData& bd)
{
   if (bd.getSize() != 32) {
      throw std::length_error("only accepts 32 bytes of data");
   }
   std::memcpy(data, bd.getPtr(), 32);
}

Hash32::Hash32(const BinaryDataRef& bdr)
{
   if (bdr.getSize() != 32) {
      throw std::length_error("only accepts 32 bytes of data");
   }
   std::memcpy(data, bdr.getPtr(), 32);
}

////////
bool Hash32::operator==(const Hash32& rhs) const
{
   //compiler should optimize the hell out of this
   return std::memcmp(data, rhs.data, 32) == 0;
}

bool Hash32::operator==(const BinaryData& rhs) const
{
   if (rhs.getSize() != 32) {
      throw std::length_error("hash32 comparator requires 32 bytes bd");
   }
   return std::memcmp(data, rhs.getPtr(), 32) == 0;
}

bool Hash32::operator<(const Hash32& lhs) const
{
   return std::memcmp(data, lhs.data, 32) < 0;
}

////////
BinaryData Hash32::toBinaryData() const
{
   return BinaryData{(const uint8_t*)&data, 32};
}

BinaryDataRef Hash32::getRef() const
{
   return BinaryDataRef{(const uint8_t*)&data, 32};
}

std::string Hash32::toHexStr(bool swapEndian) const
{
   return getRef().toHexStr(swapEndian);
}

bool Hash32::valid() const
{
   return data[0] != 0 || data[1] != 0 || data[2] != 0 || data[3] != 0;
}

////////////////////////////////////////////////////////////////////////////////
// Hasher
std::size_t Hash32::Hasher::operator()(const Hash32& h32) const
{
   return h32.data[0];
}

std::size_t Hash32::Hasher::operator()(const BinaryData& bd) const
{
   if (bd.getSize() != 32) {
      throw std::length_error("hash32 hasher requires 32 bytes bd");
   }
   std::size_t result;
   std::memcpy(&result, bd.getPtr(), sizeof(std::size_t));
   return result;
}

std::size_t Hash32::Hasher::operator()(const BinaryDataRef& bdr) const
{
   if (bdr.getSize() != 32) {
      throw std::length_error("hash32 hasher requires 32 bytes bdr");
   }
   std::size_t result;
   std::memcpy(&result, bdr.getPtr(), sizeof(std::size_t));
   return result;
}

////////////////////////////////////////////////////////////////////////////////
// IsEqual
bool Hash32::IsEqual::operator()(
   const Hash32& lhs, const Hash32& rhs) const
{
   return lhs == rhs;
}

bool Hash32::IsEqual::operator()(
   const Hash32& lhs, const BinaryData& rhs) const
{
   return lhs == rhs;
}

bool Hash32::IsEqual::operator()(
   const BinaryData& lhs, const Hash32& rhs) const
{
   if (lhs.getSize() != 32) {
      throw std::length_error("hash32 comparator requires 32 bytes bd");
   }
   return std::memcmp(lhs.getPtr(), rhs.data, 32) == 0;
}

bool Hash32::IsEqual::operator()(
   const Hash32& lhs, const BinaryDataRef& rhs) const
{
   if (rhs.getSize() != 32) {
      throw std::length_error("hash32 comparator requires 32 bytes bdr");
   }
   return std::memcmp(lhs.data, rhs.getPtr(), 32) == 0;
}

bool Hash32::IsEqual::operator()(
   const BinaryDataRef& lhs, const Hash32& rhs) const
{
   if (lhs.getSize() != 32) {
      throw std::length_error("hash32 comparator requires 32 bytes bdr");
   }
   return std::memcmp(lhs.getPtr(), rhs.data, 32) == 0;
}

////////////////////////////////////////////////////////////////////////////////
// BlockHeader
BlockHeader::BlockHeader(Hash32& thisHash, Hash32& prevHash, Hash32& merkleRoot,
   double diff, uint32_t timestamp, uint32_t version) :
   thisHash_{std::move(thisHash)}, prevHash_{std::move(prevHash)},
   merkleRoot_{std::move(merkleRoot)}, difficultyDbl_(diff),
   timestamp_(timestamp), version_(version)
{}

BlockHeader::BlockHeader(const uint8_t* ptr, size_t size) :
   BlockHeader{unserialize(ptr, size)}
{}

BlockHeader::BlockHeader(BinaryDataRef str) :
   BlockHeader{unserialize(str.getPtr(), str.getSize())}
{}

////////
BlockHeader BlockHeader::unserialize(
   const uint8_t* ptr, size_t size)
{
   if (size < HEADER_SIZE) {
      throw BtcUtils::BlockDeserializingException();
   }

   //header hash
   BinaryDataRef data{ptr, HEADER_SIZE};
   Hash32 thisHash;
   if (true) {
      Cryptography::Hash::getHash256(data, (uint8_t*)&thisHash.data);
   }

   //version
   BinaryRefReader brr(data);
   auto version = brr.get_uint32_t();

   //prev hash
   Hash32 prevHash{brr.get_BinaryDataRef(32)};

   //merkle root
   Hash32 merkleRoot{brr.get_BinaryDataRef(32)};

   //timestamp
   auto timestamp = brr.get_uint32_t();

   //diff converted into double
   auto diff = BtcUtils::convertDiffBitsToDouble(brr.get_BinaryData(4));

   return BlockHeader{thisHash, prevHash, merkleRoot, diff, timestamp, version};
}

////////
void BlockHeader::setRawData(BinaryData data)
{
   if (data.getSize() != HEADER_SIZE) {
      throw std::runtime_error("invalid header raw data size");
   }
   rawData_ = std::move(data.release());
}

BinaryDataRef BlockHeader::getRawData() const
{
   return BinaryDataRef{&rawData_[0], rawData_.size()};
}

////////
const Hash32& BlockHeader::getThisHash() const
{
   return thisHash_;
}

const Hash32& BlockHeader::getPrevHash() const
{
   return prevHash_;
}

const Hash32* BlockHeader::getNextHash() const
{
   return nextHash_;
}

const Hash32& BlockHeader::getMerkleRoot() const
{
   return merkleRoot_;
}

////////
uint32_t BlockHeader::getVersion() const
{
   return version_;
}

uint32_t BlockHeader::getTimestamp() const
{
   return timestamp_;
}

////////
uint32_t BlockHeader::getBlockHeight() const
{
   return blockHeight_;
}

void BlockHeader::setBlockHeight(unsigned hgt)
{
   blockHeight_ = hgt;
}

////////
bool BlockHeader::isMainBranch() const
{
   return isMainBranch_;
}

bool BlockHeader::isOrphan() const
{
   return isOrphan_;
}

////////
double BlockHeader::getDifficulty() const
{
   return difficultyDbl_;
}

double BlockHeader::getDifficultySum() const
{
   return difficultySum_;
}

////////
size_t BlockHeader::getOffset() const
{
   return blkFileOffset_;
}

Types::FileId BlockHeader::getBlockFileId() const
{
   return blkFileId_;
}

void BlockHeader::setBlockFileId(Types::FileId fnum)
{
   blkFileId_ = fnum;
}

void BlockHeader::setBlockFileOffset(size_t offs)
{
   blkFileOffset_ = offs;
}

////////
uint32_t BlockHeader::getBlockSize() const
{
   return numBlockBytes_;
}

void BlockHeader::setBlockSize(uint32_t sz)
{
   numBlockBytes_ = sz;
}

void BlockHeader::setNumTx(uint32_t ntx)
{
   numTx_ = ntx;
}

uint32_t BlockHeader::getNumTx() const
{
   return numTx_;
}

////////
Types::BlockId BlockHeader::getUniqueID() const
{
   return uniqueID_;
}

void BlockHeader::setUniqueID(Types::BlockId ID)
{
   uniqueID_ = ID;
}

////////
void BlockHeader::checkMerkleRoot(const BinaryData& merkleRoot)
{
   if (merkleRoot_ == merkleRoot) {
      checkState_ = MerkleState::Valid;
   } else {
      checkState_ = MerkleState::Invalid;
   }
}

void BlockHeader::setMerkleValid(bool isValid)
{
   if (checkState_ != MerkleState::Unchecked) {
      throw std::runtime_error("merkle already checked");
   }
   checkState_ = isValid ? MerkleState::Valid : MerkleState::Invalid;
}

bool BlockHeader::parsedBlockData() const
{
   return checkState_ != MerkleState::Unchecked;
}

bool BlockHeader::isMerkleValid() const
{
   return checkState_ == MerkleState::Valid;
}

////////
void BlockHeader::pprintAlot(std::ostream &)
{
   std::cout << "Header:   " << getBlockHeight() << std::endl;
   std::cout << "Hash:     " << getThisHash().toHexStr(true)  << std::endl;
   std::cout << "Hash:     " << getThisHash().toHexStr(false) << std::endl;
   std::cout << "PrvHash:  " << getPrevHash().toHexStr(true)  << std::endl;
   std::cout << "PrvHash:  " << getPrevHash().toHexStr(false) << std::endl;
   std::cout << "this*:    " << this << std::endl;
   std::cout << "TotSize:  " << getBlockSize() << std::endl;
   std::cout << "Tx Count: " << numTx_ << std::endl;
}

////////////////////////////////////////////////////////////////////////////////
// Hasher
std::size_t BlockHeader::Hasher::operator()(const HeaderPtr& ptr) const
{
   if (ptr == nullptr) {
      throw std::runtime_error("empty header ptr");
   }
   return Hash32::Hasher()(ptr->getThisHash());
}

std::size_t BlockHeader::Hasher::operator()(const Hash32& h32) const
{
   return Hash32::Hasher()(h32);
}

std::size_t BlockHeader::Hasher::operator()(const BinaryData& bd) const
{
   return Hash32::Hasher()(bd);
}

std::size_t BlockHeader::Hasher::operator()(const BinaryDataRef& bdr) const
{
   return Hash32::Hasher()(bdr);
}

////////////////////////////////////////////////////////////////////////////////
// IsEqual

bool BlockHeader::IsEqual::operator()(
   const HeaderPtr& lhs, const HeaderPtr& rhs) const
{
   if (lhs == nullptr || rhs == nullptr) {
      return false;
   }
   return lhs->getThisHash() == rhs->getThisHash();
}

bool BlockHeader::IsEqual::operator()(
   const HeaderPtr& lhs, const Hash32& rhs) const
{
   if (lhs == nullptr) {
      return false;
   }
   return lhs->getThisHash() == rhs;
}

bool BlockHeader::IsEqual::operator()(
   const Hash32& lhs, const HeaderPtr& rhs) const
{
   if (rhs == nullptr) {
      return false;
   }
   return lhs == rhs->getThisHash();
}

bool BlockHeader::IsEqual::operator()(
   const HeaderPtr& lhs, const BinaryData& bd) const
{
   if (lhs == nullptr) {
      return false;
   }
   return Hash32::IsEqual()(lhs->getThisHash(), bd);
}

bool BlockHeader::IsEqual::operator()(
   const BinaryData& bd, const HeaderPtr& rhs) const
{
   if (rhs == nullptr) {
      return false;
   }
   return Hash32::IsEqual()(bd, rhs->getThisHash());
}

bool BlockHeader::IsEqual::operator()(
   const HeaderPtr& lhs, const BinaryDataRef& bdr) const
{
   if (lhs == nullptr) {
      return false;
   }
   return Hash32::IsEqual()(lhs->getThisHash(), bdr);
}

bool BlockHeader::IsEqual::operator()(
   const BinaryDataRef& bdr, const HeaderPtr& rhs) const
{
   if (rhs == nullptr) {
      return false;
   }
   return Hash32::IsEqual()(bdr, rhs->getThisHash());
}
