////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <thread>
#include <cassert>

#include <Utils/BinaryData.h>
#include <Utils/BtcUtils.h>
#include <Utils/varint.h>
#include <Utils/DBUtils.h>

#include "StoredBlockObj.h"
#include "BlockObj.h"
#include "lmdb_wrapper.h"

using namespace Armory;

////////////////////////////////////////////////////////////////////////////////
// BlockHeader
BlockHeader::BlockHeader() :
   isInitialized_(false),
   isMainBranch_(false),
   isOrphan_(false),
   isFinishedCalc_(false),
   duplicateID_(UINT8_MAX),
   numTx_(UINT32_MAX),
   numBlockBytes_(UINT32_MAX)
{}

BlockHeader::BlockHeader(const uint8_t* ptr, uint32_t size)
{
   unserialize(ptr, size);
}

BlockHeader::BlockHeader(BinaryRefReader& brr)
{
   unserialize(brr);
}

BlockHeader::BlockHeader(BinaryDataRef str)
{
   unserialize(str);
}

void BlockHeader::clearDataCopy()
{
   dataCopy_.resize(0);
}

////////
void BlockHeader::unserialize(const uint8_t* ptr, uint32_t size)
{
   if (size < HEADER_SIZE) {
      throw BtcUtils::BlockDeserializingException();
   }
   dataCopy_.copyFrom(ptr, HEADER_SIZE);
   BtcUtils::getHash256(dataCopy_.getPtr(), HEADER_SIZE, thisHash_);
   difficultyDbl_ = BtcUtils::convertDiffBitsToDouble(
      BinaryDataRef{dataCopy_.getPtr()+72, 4});
   isInitialized_ = true;
   nextHash_ = {};
   blockHeight_ = UINT32_MAX;
   difficultySum_ = -1;
   isMainBranch_ = false;
   isOrphan_ = true;
   numTx_ = UINT32_MAX;
}

void BlockHeader::unserialize(const BinaryDataRef& str)
{
   unserialize(str.getPtr(), str.getSize());
}

void BlockHeader::unserialize(BinaryRefReader& brr)
{
   unserialize(brr.get_BinaryDataRef(HEADER_SIZE));
}

BinaryData BlockHeader::getBlockDataKey() const
{
   return DBUtils::getBlkDataKeyNoPrefix(blockHeight_, duplicateID_);
}

////////
const BinaryData& BlockHeader::serialize() const
{
   return dataCopy_;
}

bool BlockHeader::hasFilePos() const
{
   return blkFileNum_ != UINT32_MAX;
}

////////
uint32_t BlockHeader::getVersion() const
{
   return READ_UINT32_LE(getPtr());
}

const BinaryData& BlockHeader::getThisHash() const
{
   return thisHash_;
}

BinaryData BlockHeader::getPrevHash() const
{
   return BinaryData(getPtr()+4 ,32);
}

const BinaryData& BlockHeader::getNextHash() const
{
   return nextHash_;
}

BinaryData BlockHeader::getMerkleRoot() const
{
   return BinaryData(getPtr()+36,32);
}

BinaryData BlockHeader::getDiffBits() const
{
   return BinaryData(getPtr()+72,4 );
}

uint32_t BlockHeader::getTimestamp() const
{
   return READ_UINT32_LE(getPtr()+68);
}

uint32_t BlockHeader::getNonce() const
{
   return READ_UINT32_LE(getPtr()+76);
}

uint32_t BlockHeader::getBlockHeight() const
{
   return blockHeight_;
}

void BlockHeader::setBlockHeight(unsigned hgt)
{
   blockHeight_ = hgt;
}

bool BlockHeader::isMainBranch() const
{
   return isMainBranch_;
}

bool BlockHeader::isOrphan() const
{
   return isOrphan_;
}

double BlockHeader::getDifficulty() const
{
   return difficultyDbl_;
}

double BlockHeader::getDifficultySum() const
{
   return difficultySum_;
}

////////
BinaryDataRef BlockHeader::getThisHashRef() const
{
   return thisHash_.getRef();
}

BinaryDataRef BlockHeader::getPrevHashRef() const
{
   return BinaryDataRef(getPtr()+4, 32);
}

BinaryDataRef BlockHeader::getNextHashRef() const
{
   return nextHash_.getRef();
}

BinaryDataRef BlockHeader::getMerkleRootRef() const
{
   return BinaryDataRef(getPtr()+36,32);
}

BinaryDataRef BlockHeader::getDiffBitsRef() const
{
   return BinaryDataRef(getPtr()+72,4 );
}

uint32_t BlockHeader::getNumTx() const
{
   return numTx_;
}

const std::string& BlockHeader::getFileName() const
{
   return blkFile_;
}

uint64_t BlockHeader::getOffset() const
{
   return blkFileOffset_;
}

uint32_t BlockHeader::getBlockFileNum() const
{
   return blkFileNum_;
}

////////
const uint8_t* BlockHeader::getPtr() const
{
   assert(isInitialized_);
   return dataCopy_.getPtr();
}

size_t BlockHeader::getSize() const
{
   assert(isInitialized_);
   return dataCopy_.getSize();
}

bool BlockHeader::isInitialized() const
{
   return isInitialized_;
}

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

////////
void BlockHeader::setBlockFile(std::string filename)
{
   blkFile_ = filename;
}

void BlockHeader::setBlockFileNum(uint32_t fnum)
{
   blkFileNum_ = fnum;
}

void BlockHeader::setBlockFileOffset(uint64_t offs)
{
   blkFileOffset_ = offs;
}

////////
uint8_t BlockHeader::getDuplicateID() const
{
   return duplicateID_;
}

void BlockHeader::setDuplicateID(uint8_t d)
{
   duplicateID_ = d;
}

unsigned int BlockHeader::getThisID() const
{
   return uniqueID_;
}

void BlockHeader::setUniqueID(unsigned int ID)
{
   uniqueID_ = ID;
}

////////
void BlockHeader::pprint(std::ostream& os, int nIndent, bool pBigendian) const
{
   std::string indent{""};
   for (int i=0; i < nIndent; i++) {
      indent += "   ";
   }

   std::string endstr = (pBigendian ? " (BE)" : " (LE)");
   os << indent << "Block Information: " << blockHeight_ << std::endl;
   os << indent << "   Hash:       " <<
      getThisHash().toHexStr(pBigendian).c_str() << endstr << std::endl;
   os << indent << "   Timestamp:  " << getTimestamp() << std::endl;
   os << indent << "   Prev Hash:  " <<
      getPrevHash().toHexStr(pBigendian).c_str() << endstr << std::endl;
   os << indent << "   MerkleRoot: " <<
      getMerkleRoot().toHexStr(pBigendian).c_str() << endstr << std::endl;
   os << indent << "   Difficulty: " << (difficultyDbl_) <<
      "    (" << getDiffBits().toHexStr().c_str() << ")" << std::endl;
   os << indent << "   CumulDiff:  " << (difficultySum_) << std::endl;
   os << indent << "   Nonce:      " << getNonce() << std::endl;
}

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
// DBOutPoint Methods
DBOutPoint::DBOutPoint(Outpoint op, LMDBBlockDatabase* db) :
   Outpoint(op), db_(db)
{}

BinaryDataRef DBOutPoint::getDBkey() const
{
   if (DBkey_.getSize() == 8) {
      return DBkey_;
   }

   if (db_ != nullptr) {
      DBkey_ = std::move(db_->getDBKeyForHash(txHash_));
      if (DBkey_.getSize() == 6) {
         DBkey_.append(WRITE_UINT16_BE((uint16_t)txOutIndex_));
         return DBkey_;
      }
   }
   return {};
}

////////////////////////////////////////////////////////////////////////////////
// UnspentTxOut Methods
UnspentTxOut::UnspentTxOut() :
   txHash_(BtcUtils::EmptyHash),
   txOutIndex_(0),
   txHeight_(0),
   value_(0),
   script_(BinaryData(0)),
   isMultisigRef_(false)
{}

UnspentTxOut::UnspentTxOut(const BinaryData& hash, uint32_t outIndex,
   uint32_t height, uint64_t val, const BinaryData& script) :
   txHash_(hash), txOutIndex_(outIndex), txHeight_(height),
   value_(val), script_(script)
{}

////////
BinaryData UnspentTxOut::getTxHash() const
{
   return txHash_;
}

uint32_t UnspentTxOut::getTxtIndex() const
{
   return txIndex_;
}

uint32_t UnspentTxOut::getTxOutIndex() const
{
   return txOutIndex_;
}

uint64_t UnspentTxOut::getValue() const
{
   return value_;
}

uint64_t UnspentTxOut::getTxHeight() const
{
   return txHeight_;
}

uint32_t UnspentTxOut::isMultisigRef() const
{
   return isMultisigRef_;
}

BinaryData UnspentTxOut::getRecipientScrAddr() const
{
   return BtcUtils::getTxOutScrAddr(getScript());
}

uint32_t UnspentTxOut::getNumConfirm(uint32_t currBlkNum) const
{
   if (txHeight_ == UINT32_MAX) {
      throw std::runtime_error("uninitiliazed UnspentTxOut");
   }
   return currBlkNum - txHeight_ + 1;
}

Outpoint UnspentTxOut::getOutPoint() const
{
   return Outpoint(txHash_, txOutIndex_);
}

const BinaryData& UnspentTxOut::getScript() const
{
   return script_;
}

////////
bool UnspentTxOut::CompareNaive(const UnspentTxOut& uto1,
   const UnspentTxOut& uto2)
{
   float val1 = (float)uto1.getValue();
   float val2 = (float)uto2.getValue();
   return (val1 * uto1.txHeight_ < val2 * uto2.txHeight_);
}

bool UnspentTxOut::CompareTech1(const UnspentTxOut& uto1,
   const UnspentTxOut& uto2)
{
   float val1 = pow((float)uto1.getValue(), 1.0f/3.0f);
   float val2 = pow((float)uto2.getValue(), 1.0f/3.0f);
   return (val1 * uto1.txHeight_ < val2 * uto2.txHeight_);
}

bool UnspentTxOut::CompareTech2(const UnspentTxOut& uto1,
   const UnspentTxOut& uto2)
{
   float val1 = pow(log10((float)uto1.getValue()) + 5, 5);
   float val2 = pow(log10((float)uto2.getValue()) + 5, 5);
   return (val1 * uto1.txHeight_ < val2 * uto2.txHeight_);

}

bool UnspentTxOut::CompareTech3(const UnspentTxOut& uto1,
   const UnspentTxOut& uto2)
{
   float val1 = pow(log10((float)uto1.getValue()) + 5, 4);
   float val2 = pow(log10((float)uto2.getValue()) + 5, 4);
   return (val1 * uto1.txHeight_ < val2 * uto2.txHeight_);
}

void UnspentTxOut::sortTxOutVect(
   std::vector<UnspentTxOut>& utovect, int sortType)
{
   switch (sortType)
   {
      case 0: sort(utovect.begin(), utovect.end(), CompareNaive); break;
      case 1: sort(utovect.begin(), utovect.end(), CompareTech1); break;
      case 2: sort(utovect.begin(), utovect.end(), CompareTech2); break;
      case 3: sort(utovect.begin(), utovect.end(), CompareTech3); break;
      default: break; // do nothing
   }
}

void UnspentTxOut::pprintOneLine(uint32_t currBlk)
{
   printf(" Tx:%s:%02d   BTC:%0.3f   nConf:%04d\n",
      txHash_.copySwapEndian().getSliceCopy(0,8).toHexStr().c_str(),
      txOutIndex_,
      value_/1e8,
      getNumConfirm(currBlk)
   );
}
