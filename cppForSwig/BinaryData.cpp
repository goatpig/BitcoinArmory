////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BinaryData.h"
#include "BtcUtils.h"
#include "EncryptionUtils.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////
BinaryData::BinaryData(BinaryDataRef const & bdRef) 
{ 
   copyFrom(bdRef.getPtr(), bdRef.getSize());
}

////////////////////////////////////////////////////////////////////////////////
// copyFrom
////////////////////////////////////////////////////////////////////////////////
void BinaryData::copyFrom(uint8_t const * start, uint8_t const * end)
{
   // [start, end)
   copyFrom(start, (end-start));
}

void BinaryData::copyFrom(const std::string& str)
{
   copyFrom(str.c_str(), str.size());
}

void BinaryData::copyFrom(const BinaryDataRef& bdr)
{
   copyFrom(bdr.getPtr(), bdr.getSize());
}

void  BinaryData::copyFrom(const BinaryData& bd)
{
   copyFrom(bd.getPtr(), bd.getSize());
}

void  BinaryData::copyFrom(const char* inData, size_t sz)
{
   copyFrom((uint8_t*)inData, sz);
}

void BinaryData::copyFrom(const uint8_t* inData, size_t sz)
{
   if (inData==NULL || sz == 0) {
      alloc(0);
   } else {
      alloc(sz);
      memcpy(&data_[0], inData, sz);
   }
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef BinaryData::getRef(void) const
{
   return BinaryDataRef(getPtr(), getSize());
}

////////////////////////////////////////////////////////////////////////////////
BinaryData& BinaryData::append(BinaryData const & bd2)
{
   return this->append(BinaryDataRef{bd2});
}

////////////////////////////////////////////////////////////////////////////////
BinaryData & BinaryData::append(BinaryDataRef const & bd2)
{
   if (bd2.empty()) {
      return (*this);
   }
   if (empty()) {
      copyFrom(bd2.getPtr(), bd2.getSize());
   } else {
      auto originSize = data_.size();
      if (data_.capacity() < originSize + bd2.getSize()) {
         //we have to enlarge the recipient vector's capacity
         data_.reserve((originSize + bd2.getSize()) * 2);
      }

      data_.resize(originSize + bd2.getSize());
      memcpy(&data_[0] + originSize, bd2.getPtr(), bd2.getSize());
   }
   return (*this);
}

/////////////////////////////////////////////////////////////////////////////
BinaryData & BinaryData::append(uint8_t const * str, size_t sz)
{
   BinaryDataRef appStr(str, sz);
   return append(appStr);
}

////////////////////////////////////////////////////////////////////////////////
int32_t BinaryData::find(BinaryDataRef const & matchStr, uint32_t startPos)
{
   int32_t finalAnswer = -1;
   if(matchStr.getSize()==0)
      return startPos;

   for(int32_t i=startPos; i<=(int32_t)getSize()-(int32_t)matchStr.getSize(); i++)
   {
      if(matchStr[0] != data_[i])
         continue;

      for(uint32_t j=0; j<matchStr.getSize(); j++)
      {
         if(matchStr[j] != data_[i+j])
            break;

         // If we are at this instruction and is the last index, it's a match
         if(j==matchStr.getSize()-1)
            finalAnswer = i;
      }

      if(finalAnswer != -1)
         break;
   }

   return finalAnswer;
}

////////////////////////////////////////////////////////////////////////////////
int32_t BinaryData::find(BinaryData const & matchStr, uint32_t startPos)
{
   BinaryDataRef bdrmatch(matchStr);
   return find(bdrmatch, startPos);
}


////////////////////////////////////////////////////////////////////////////////
bool BinaryData::contains(BinaryData const & matchStr, uint32_t startPos)
{
   return (find(matchStr, startPos) != -1);
}

////////////////////////////////////////////////////////////////////////////////
bool BinaryData::contains(BinaryDataRef const & matchStr, uint32_t startPos)
{
   return (find(matchStr, startPos) != -1);
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryData::startsWith(BinaryDataRef const & matchStr) const
{
   if(matchStr.getSize() > getSize())
      return false;

   for(uint32_t i=0; i<matchStr.getSize(); i++)
      if(matchStr[i] != (*this)[i])
         return false;

   return true;
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryData::startsWith(BinaryData const & matchStr) const
{
   if(matchStr.getSize() > getSize())
      return false;

   for(uint32_t i=0; i<matchStr.getSize(); i++)
      if(matchStr[i] != (*this)[i])
         return false;

   return true;
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryData::endsWith(BinaryDataRef const & matchStr) const
{
   size_t sz = matchStr.getSize();
   if(sz > getSize())
      return false;
   
   for(uint32_t i=0; i<sz; i++)
      if(matchStr[sz-(i+1)] != (*this)[getSize()-(i+1)])
         return false;

   return true;
}
/////////////////////////////////////////////////////////////////////////////
bool BinaryData::endsWith(BinaryData const & matchStr) const
{
   size_t sz = matchStr.getSize();
   if(sz > getSize())
      return false;
   
   for(uint32_t i=0; i<sz; i++)
      if(matchStr[sz-(i+1)] != (*this)[getSize()-(i+1)])
         return false;

   return true;
}

/////////////////////////////////////////////////////////////////////////////
BinaryDataRef BinaryData::getSliceRef(ssize_t start_pos, size_t nChar) const
{
   if(start_pos < 0) 
      start_pos = getSize() + start_pos;

   if((size_t)start_pos + nChar > getSize())
   {
      cerr << "getSliceRef: Invalid BinaryData access" << endl;
      return BinaryDataRef();
   }
   return BinaryDataRef( getPtr()+start_pos, nChar);
}

/////////////////////////////////////////////////////////////////////////////
BinaryData BinaryData::getSliceCopy(ssize_t start_pos, size_t nChar) const
{
   if(start_pos < 0) 
      start_pos = getSize() + start_pos;

   if((size_t)start_pos + nChar > getSize())
   {
      cerr << "getSliceCopy: Invalid BinaryData access" << endl;
      return BinaryData();
   }
   return BinaryData(getPtr()+start_pos, nChar);
}

/////////////////////////////////////////////////////////////////////////////
BinaryData BinaryData::fromString(const std::string& str, size_t len)
{
   if (len == SIZE_MAX) {
      len = str.size();
   }
   BinaryData data;
   data.copyFrom(str.c_str(), len);
   return data;
}

/////////////////////////////////////////////////////////////////////////////
BinaryData BinaryData::fromString(const std::string_view& str, size_t len)
{
   if (len == SIZE_MAX) {
      len = str.size();
   }
   BinaryData data;
   data.copyFrom(str.data(), len);
   return data;
}

/////////////////////////////////////////////////////////////////////////////
void BinaryData::createFromHex(const string& str)
{
   BinaryDataRef bdr((uint8_t*)str.c_str(), str.size());
   createFromHex(bdr);
}

/////////////////////////////////////////////////////////////////////////////
void BinaryData::createFromHex(BinaryDataRef const & bdr)
{
   static const uint8_t binLookupTable[256] = {
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0, 0, 0, 0, 0, 0,
      0, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

   if (bdr.getSize() % 2 != 0)
   {
      LOGERR << "odd hexit count";
      throw runtime_error("odd hexit count");
   }
   size_t newLen = bdr.getSize() / 2;
   alloc(newLen);

   auto ptr = bdr.getPtr();
   for (size_t i = 0; i<newLen; i++)
   {
      uint8_t char1 = binLookupTable[*(ptr + 2 * i)];
      uint8_t char2 = binLookupTable[*(ptr + 2 * i + 1)];
      data_[i] = (char1 << 4) | char2;
   }
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryData::isZero() const
{
   bool isZero = true;
   auto ptr = getPtr();
   for (size_t i = 0; i < getSize(); i++)
   {
      if (ptr[i] != 0)
      {
         isZero = false;
         break;
      }
   }

   return isZero;
}

/////////////////////////////////////////////////////////////////////////////
uint64_t BinaryReader::get_var_int(uint8_t* nRead)
{
   uint32_t nBytes;
   uint64_t varInt = BtcUtils::readVarInt( 
      bdStr_.getPtr() + pos_, bdStr_.getSize() - pos_, &nBytes);
   if(nRead != NULL)
      *nRead = nBytes;
   pos_ += nBytes;
   return varInt;
}

/////////////////////////////////////////////////////////////////////////////
uint64_t BinaryRefReader::get_var_int(uint8_t* nRead)
{
   uint32_t nBytes;
   uint64_t varInt = BtcUtils::readVarInt( bdRef_.getPtr() + pos_, getSizeRemaining(), &nBytes);
   if(nRead != NULL)
      *nRead = nBytes;
   pos_.fetch_add(nBytes, memory_order_relaxed);
   return varInt;
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryData::operator==(BinaryDataRef const & bd2) const
{
   if (!empty())
   {
      if(getSize() != bd2.getSize())
         return false;

      return (memcmp(getPtr(), bd2.getPtr(), getSize()) == 0);
   }

   return bd2.empty();
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryData::operator<(BinaryDataRef const & bd2) const
{
   size_t minLen = std::min(getSize(), bd2.getSize());
   int result = 0;
   if (minLen != 0)
      result = memcmp(getPtr(), bd2.getPtr(), minLen);
   
   if (result != 0)
      return result < 0;
   return (getSize() < bd2.getSize());
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryData::operator<(BinaryData const & bd2) const
{
   size_t minLen = std::min(getSize(), bd2.getSize());
   int result = 0;
   if (minLen != 0)
      result = memcmp(getPtr(), bd2.getPtr(), minLen);
   
   if (result != 0)
      return result < 0;
   return (getSize() < bd2.getSize());
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryData::operator>(BinaryData const & bd2) const
{
   size_t minLen = std::min(getSize(), bd2.getSize());
   int result = 0;
   if (minLen != 0)
      result = memcmp(getPtr(), bd2.getPtr(), minLen);
   
   if (result != 0)
      return result > 0;
   return (getSize() > bd2.getSize());
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryData::operator==(BinaryData const & bd2) const
{
   if (!empty())
   {
      if (getSize() != bd2.getSize())
         return false;

      return (memcmp(getPtr(), bd2.getPtr(), getSize()) == 0);
   }

   return bd2.empty();
}

/////////////////////////////////////////////////////////////////////////////
std::size_t hash<BinaryData>::operator()(const BinaryData& key) const
{
   if (key.empty())
      return 0;

   std::size_t result;
   auto len = std::min(sizeof(std::size_t), key.getSize());
   memcpy(&result, key.getPtr(), len);
   return result;
}

/////////////////////////////////////////////////////////////////////////////
//
////BinaryDataRef
//
/////////////////////////////////////////////////////////////////////////////
BinaryDataRef& BinaryDataRef::operator=(const BinaryDataRef& rhs)
{
   setRef(rhs.ptr_, rhs.nBytes_);
   return *this;
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryDataRef::operator<(BinaryDataRef const & bd2) const
{
   size_t minLen = std::min(getSize(), bd2.getSize());
   int result = 0;
   if (minLen != 0)
      result = memcmp(getPtr(), bd2.getPtr(), minLen);
   
   if (result != 0)
      return result < 0;
   return (getSize() < bd2.getSize());
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryDataRef::operator>(BinaryDataRef const & bd2) const
{
   size_t minLen = std::min(getSize(), bd2.getSize());
   int result = 0;
   if (minLen != 0)
      result = memcmp(getPtr(), bd2.getPtr(), minLen);
   
   if (result != 0)
      return result > 0;
   return (getSize() > bd2.getSize());
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryDataRef::startsWith(BinaryDataRef const & matchStr) const
{
   if(matchStr.getSize() > nBytes_)
      return false;
   
   for(uint32_t i=0; i<matchStr.getSize(); i++)
      if(matchStr[i] != (*this)[i])
         return false;
   
   return true;
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryDataRef::startsWith(BinaryData const & matchStr) const
{
   if(matchStr.getSize() > nBytes_)
      return false;
   
   for(uint32_t i=0; i<matchStr.getSize(); i++)
      if(matchStr[i] != (*this)[i])
         return false;
   
   return true;
}

/////////////////////////////////////////////////////////////////////////////
std::size_t hash<BinaryDataRef>::operator()(const BinaryDataRef& key) const
{
   if (key.empty())
      return 0;

   std::size_t result;
   auto len = std::min(sizeof(std::size_t), key.getSize());
   memcpy(&result, key.getPtr(), len);
   return result;
}

/////////////////////////////////////////////////////////////////////////////
//
////BinaryReader
//
/////////////////////////////////////////////////////////////////////////////
void BinaryReader::advance(uint32_t nBytes)
{
   pos_ += nBytes;
   pos_ = min(pos_, getSize());
}

/////////////////////////////////////////////////////////////////////////////
void BinaryReader::rewind(size_t nBytes)
{
   pos_ -= nBytes;
   pos_ = max(pos_, (size_t)0);
}

/////////////////////////////////////////////////////////////////////////////
void BinaryReader::resize(size_t nBytes)
{
   bdStr_.resize(nBytes);
   pos_ = min(nBytes, pos_);
}

/////////////////////////////////////////////////////////////////////////////
//
////BinaryRefReader
//
/////////////////////////////////////////////////////////////////////////////
SecureBinaryData BinaryRefReader::get_SecureBinaryData(uint32_t nBytes)
{
   if (getSizeRemaining() < nBytes)
      throw runtime_error("[get_SecureBinaryData] buffer overflow");
   SecureBinaryData out(nBytes);
   bdRef_.copyTo(out.getPtr(), pos_, nBytes);
   pos_.fetch_add(nBytes, memory_order_relaxed);
   return out;
}

/////////////////////////////////////////////////////////////////////////////
void BinaryRefReader::advance(size_t nBytes)
{
   if (getSizeRemaining() < nBytes)
      throw runtime_error("[advance] buffer overflow");

   pos_.fetch_add(nBytes, memory_order_relaxed);
}

/////////////////////////////////////////////////////////////////////////////
uint8_t BinaryRefReader::get_uint8_t()
{
   if (getSizeRemaining() < 1)
   {
      LOGERR << "[get_uint8_t] buffer overflow";
      throw runtime_error("[get_uint8_t] buffer overflow");
   }
   uint8_t outVal = bdRef_[pos_];
   pos_.fetch_add(1, memory_order_relaxed);
   return outVal;
}

/////////////////////////////////////////////////////////////////////////////
uint16_t BinaryRefReader::get_uint16_t(ENDIAN e)
{
   if (getSizeRemaining() < 2)
   {
      LOGERR << "[get_uint16_t] buffer overflow";
      throw runtime_error("[get_uint16_t] buffer overflow");
   }
   uint16_t  outVal = (e==LE ?
      READ_UINT16_LE(bdRef_.getPtr() + pos_) :
      READ_UINT16_BE(bdRef_.getPtr() + pos_));

   pos_.fetch_add(2, std::memory_order_relaxed);
   return outVal;
}

/////////////////////////////////////////////////////////////////////////////
uint32_t BinaryRefReader::get_uint32_t(ENDIAN e)
{
   if (getSizeRemaining() < 4)
   {
      LOGERR << "[get_uint32_t] buffer overflow";
      throw runtime_error("[get_uint32_t] buffer overflow");
   }
   uint32_t  outVal = (e==LE ?
      READ_UINT32_LE(bdRef_.getPtr() + pos_) :
      READ_UINT32_BE(bdRef_.getPtr() + pos_));

   pos_.fetch_add(4, memory_order_relaxed);
   return outVal;
}

/////////////////////////////////////////////////////////////////////////////
int32_t BinaryRefReader::get_int32_t(ENDIAN e)
{
   if (getSizeRemaining() < 4)
   {
      LOGERR << "[get_int32_t] buffer overflow";
      throw runtime_error("[get_int32_t] buffer overflow");
   }
   int32_t outVal = (e == LE ?
      BinaryData::StrToIntLE<int32_t>(bdRef_.getPtr() + pos_) :
      BinaryData::StrToIntBE<int32_t>(bdRef_.getPtr() + pos_));

   pos_.fetch_add(4, memory_order_relaxed);
   return outVal;
}

/////////////////////////////////////////////////////////////////////////////
uint64_t BinaryRefReader::get_uint64_t(ENDIAN e)
{
   if (getSizeRemaining() < 8)
   {
      LOGERR << "[get_uint64_t] buffer overflow";
      throw runtime_error("[get_uint64_t] buffer overflow");
   }
   uint64_t outVal = (e==LE ?
      READ_UINT64_LE(bdRef_.getPtr() + pos_) :
      READ_UINT64_BE(bdRef_.getPtr() + pos_));

   pos_.fetch_add(8, memory_order_relaxed);
   return outVal;
}

/////////////////////////////////////////////////////////////////////////////
int64_t BinaryRefReader::get_int64_t(ENDIAN e)
{
   if (getSizeRemaining() < 8)
   {
      LOGERR << "[get_int64_t] buffer overflow";
      throw runtime_error("[get_int64_t] buffer overflow");
   }
   int64_t outVal = (e == LE ?
      BinaryData::StrToIntLE<int64_t>(bdRef_.getPtr() + pos_) :
      BinaryData::StrToIntBE<int64_t>(bdRef_.getPtr() + pos_));

   pos_.fetch_add(8, memory_order_relaxed);
   return outVal;
}

/////////////////////////////////////////////////////////////////////////////
double BinaryRefReader::get_double()
{
   if (getSizeRemaining() < 8)
   {
      LOGERR << "[get_double] buffer overflow";
      throw runtime_error("[get_double] buffer overflow");
   }

   auto doublePtr = (double*)(bdRef_.getPtr() + pos_);

   pos_.fetch_add(8, memory_order_relaxed);
   return *doublePtr;
}

/////////////////////////////////////////////////////////////////////////////
BinaryDataRef BinaryRefReader::get_BinaryDataRef(uint32_t nBytes)
{
   if (getSizeRemaining() < nBytes)
   {
      LOGERR << "[get_BinaryDataRef] buffer overflow";
      throw runtime_error("[get_BinaryDataRef] buffer overflow");
   }

   BinaryDataRef bdrefout(bdRef_.getPtr() + pos_, nBytes);
   pos_.fetch_add(nBytes, memory_order_relaxed);
   return bdrefout;
}

/////////////////////////////////////////////////////////////////////////////
BinaryRefReader BinaryRefReader::fork() const
{
   return BinaryRefReader(
      bdRef_.getPtr() + pos_.load(memory_order_relaxed), getSizeRemaining());
}

/////////////////////////////////////////////////////////////////////////////
void BinaryRefReader::get_BinaryData(BinaryData & bdTarget, uint32_t nBytes)
{
   if (getSizeRemaining() < nBytes)
   {
      LOGERR << "[get_BinaryData] buffer overflow";
      throw runtime_error("[get_BinaryData] buffer overflow");
   }

   bdTarget.copyFrom( bdRef_.getPtr() + pos_, nBytes);
   pos_.fetch_add(nBytes, memory_order_relaxed);
}

/////////////////////////////////////////////////////////////////////////////
BinaryData BinaryRefReader::get_BinaryData(uint32_t nBytes)
{
   if (getSizeRemaining() < nBytes)
   {
      LOGERR << "[get_BinaryData] buffer overflow!";
      LOGERR << "grabbing " << nBytes << 
         " out of " << getSizeRemaining() << " bytes";
      throw runtime_error("[get_BinaryData] buffer overflow");
   }

   BinaryData out;
   get_BinaryData(out, nBytes);
   return out;
}

/////////////////////////////////////////////////////////////////////////////
void BinaryRefReader::get_BinaryData(uint8_t* targPtr, uint32_t nBytes)
{
   if (getSizeRemaining() < nBytes)
   {
      LOGERR << "[get_BinaryData] buffer overflow";
      throw runtime_error("[get_BinaryData] buffer overflow");
   }

   bdRef_.copyTo(targPtr, pos_, nBytes);
   pos_.fetch_add(nBytes, memory_order_relaxed);
}

/////////////////////////////////////////////////////////////////////////////
string BinaryRefReader::get_String(uint32_t nBytes)
{
   string strOut(bdRef_.toCharPtr() + pos_, nBytes);
   pos_.fetch_add(nBytes, memory_order_relaxed);
   return strOut;
}

/////////////////////////////////////////////////////////////////////////////
void BinaryRefReader::resetPosition()
{
   pos_ = 0;
}

/////////////////////////////////////////////////////////////////////////////
size_t BinaryRefReader::getPosition() const
{
   return pos_;
}

/////////////////////////////////////////////////////////////////////////////
size_t BinaryRefReader::getSize() const
{
   return totalSize_;
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryRefReader::empty() const
{
   return totalSize_ == 0;
}

/////////////////////////////////////////////////////////////////////////////
size_t BinaryRefReader::getSizeRemaining() const
{
   return totalSize_ - pos_.load(memory_order_relaxed);
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryRefReader::isEndOfStream() const
{
   return pos_.load(memory_order_relaxed) >= totalSize_;
}

/////////////////////////////////////////////////////////////////////////////
uint8_t const* BinaryRefReader::exposeDataPtr()
{
   return bdRef_.getPtr();
}

/////////////////////////////////////////////////////////////////////////////
uint8_t const* BinaryRefReader::getCurrPtr()
{
   return bdRef_.getPtr() + pos_.load(memory_order_relaxed);
}

/////////////////////////////////////////////////////////////////////////////
BinaryDataRef BinaryRefReader::getRawRef()
{
   return bdRef_;
}