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

#include <cstring>
#include "BinaryData.h"
#include "varint.h"

using namespace Armory;

////////////////////////////////////////////////////////////////////////////////
// BinaryData
BinaryData::BinaryData(BinaryDataRef const & bdRef)
{
   copyFrom(bdRef.getPtr(), bdRef.getSize());
}

BinaryData::BinaryData()
   : data_(0)
{}

BinaryData::BinaryData(size_t sz)
{
   alloc(sz);
}

BinaryData::BinaryData(const uint8_t* inData, size_t sz)
{
   copyFrom(inData, sz);
}

BinaryData::BinaryData(const char* inData, size_t sz)
{
   copyFrom(inData, sz);
}

BinaryData::BinaryData(const uint8_t* dstart, const uint8_t* dend)
{
   copyFrom(dstart, dend);
}

BinaryData::BinaryData(const BinaryData& bd)
{
   copyFrom(bd);
}

BinaryData::BinaryData(BinaryData&& copy)
{
   data_ = move(copy.data_);
}

BinaryData::~BinaryData()
{
   data_.clear();
}

////////////////////////////////////////////////////////////////////////////////
void BinaryData::alloc(size_t sz)
{
   if (sz != getSize()) {
      data_.clear();
      data_.resize(sz);
   }
}

void BinaryData::resize(size_t sz)
{
   data_.resize(sz);
}

void BinaryData::reserve(size_t sz)
{
   data_.reserve(sz);
}

size_t BinaryData::getSize() const
{
   return data_.size();
}

bool BinaryData::empty() const
{
   return data_.empty();
}

////////////////////////////////////////////////////////////////////////////////
BinaryData& BinaryData::operator=(const BinaryData& o)
{
   data_ = o.data_;
   return *this;
}

BinaryData& BinaryData::operator=(BinaryData&& o)
{
   swap(data_, o.data_);
   return *this;
}

uint8_t& BinaryData::operator[](ssize_t i)
{
   return (i<0 ? data_[getSize()+i] : data_[i]);
}

uint8_t BinaryData::operator[](ssize_t i) const
{
   return (i<0 ? data_[getSize()+i] : data_[i]);
}

BinaryData BinaryData::operator+(const BinaryData& bd2) const
{
   if (bd2.empty()) {
      return *this;
   }

   BinaryData out{getSize() + bd2.getSize()};
   if (!empty()) {
      memcpy(out.getPtr(), getPtr(), getSize());
   }
   memcpy(out.getPtr()+getSize(), bd2.getPtr(), bd2.getSize());
   return out;
}

bool BinaryData::operator!=(const BinaryData& bd2) const
{
   return (!((*this)==bd2));
}

bool BinaryData::operator!=(const BinaryDataRef& bd2) const
{
   return (!((*this)==bd2));
}

bool BinaryData::operator>=(const BinaryData& bd2) const
{
   return (*this > bd2 || *this == bd2);
}

/////////////////////////////////////////////////////////////////////////////
const uint8_t* BinaryData::getPtr() const
{
   if (empty()) {
      return nullptr;
   } else {
      return &(data_[0]);
   }
}

uint8_t* BinaryData::getPtr()
{
   if (empty()) {
      return nullptr;
   } else {
      return &(data_[0]);
   }
}

const std::vector<uint8_t>& BinaryData::getDataVector() const
{
   return data_;
}

/////////////////////////////////////////////////////////////////////////////
const char* BinaryData::getCharPtr() const
{
   if (empty()) {
      LOGERR << "Tried to get pointer of empty BinaryData";
      throw std::runtime_error("Tried to get pointer of empty BinaryData");
   } else {
      return reinterpret_cast<const char*>(&data_[0]);
   }
}

char* BinaryData::getCharPtr()
{
   if (empty()) {
      LOGERR << "Tried to get pointer of empty BinaryData";
      throw std::runtime_error("Tried to get pointer of empty BinaryData");
   } else {
      return reinterpret_cast<char*>(&data_[0]);
   }
}

////////////////////////////////////////////////////////////////////////////////
// copyFrom
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
// copyTo
void BinaryData::copyTo(uint8_t* outData) const
{
   memcpy( outData, &(data_[0]), getSize());
}

void BinaryData::copyTo(uint8_t* outData, size_t sz) const
{
   memcpy( outData, &(data_[0]), (size_t)sz);
}

void BinaryData::copyTo(uint8_t* outData, size_t offset, size_t sz) const
{
   memcpy( outData, &(data_[offset]), (size_t)sz);
}

void BinaryData::copyTo(BinaryData& bd) const
{
   if (empty()) {
      return;
   }

   bd.resize(getSize());
   memcpy(bd.getPtr(), getPtr(), getSize());
}

void BinaryData::copyTo(std::string& str)
{
   if (empty()) {
      return;
   }
   str.assign((char const *)(&(data_[0])), getSize());
}

void BinaryData::fill(uint8_t ch)
{
   if (!empty()) {
      memset(getPtr(), ch, getSize());
   }
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef BinaryData::getRef() const
{
   return BinaryDataRef(getPtr(), getSize());
}

////////////////////////////////////////////////////////////////////////////////
BinaryData& BinaryData::append(const BinaryData& bd2)
{
   return this->append(BinaryDataRef{bd2});
}

BinaryData& BinaryData::append(const BinaryDataRef& bd2)
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
   return *this;
}

BinaryData& BinaryData::append(const uint8_t* str, size_t sz)
{
   BinaryDataRef appStr{str, sz};
   return append(appStr);
}

BinaryData& BinaryData::append(uint8_t byte)
{
   data_.emplace_back(byte);
   return(*this);
}

////////////////////////////////////////////////////////////////////////////////
int32_t BinaryData::find(BinaryDataRef const & matchStr, uint32_t startPos)
{
   int32_t finalAnswer = -1;
   if (matchStr.empty()) {
      return startPos;
   }

   for(int32_t i=startPos; i<=(int32_t)getSize()-(int32_t)matchStr.getSize(); i++) {
      if (matchStr[0] != data_[i]) {
         continue;
      }

      for(uint32_t j=0; j<matchStr.getSize(); j++) {
         if (matchStr[j] != data_[i+j]) {
            break;
         }

         // If we are at this instruction and is the last index, it's a match
         if (j==matchStr.getSize()-1) {
            finalAnswer = i;
         }
      }

      if (finalAnswer != -1) {
         break;
      }
   }
   return finalAnswer;
}

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
   if (matchStr.getSize() > getSize()) {
      return false;
   }

   for (uint32_t i=0; i<matchStr.getSize(); i++) {
      if (matchStr[i] != (*this)[i]) {
         return false;
      }
   }
   return true;
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryData::startsWith(BinaryData const & matchStr) const
{
   if (matchStr.getSize() > getSize()) {
      return false;
   }

   for (uint32_t i=0; i<matchStr.getSize(); i++) {
      if (matchStr[i] != (*this)[i]) {
         return false;
      }
   }
   return true;
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryData::endsWith(BinaryDataRef const & matchStr) const
{
   size_t sz = matchStr.getSize();
   if (sz > getSize()) {
      return false;
   }

   for (uint32_t i=0; i<sz; i++) {
      if (matchStr[sz-(i+1)] != (*this)[getSize()-(i+1)]) {
         return false;
      }
   }
   return true;
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryData::endsWith(BinaryData const & matchStr) const
{
   size_t sz = matchStr.getSize();
   if (sz > getSize()) {
      return false;
   }

   for (uint32_t i=0; i<sz; i++) {
      if (matchStr[sz-(i+1)] != (*this)[getSize()-(i+1)]) {
         return false;
      }
   }
   return true;
}

/////////////////////////////////////////////////////////////////////////////
BinaryDataRef BinaryData::getSliceRef(ssize_t start_pos, size_t nChar) const
{
   if (start_pos < 0) {
      start_pos = getSize() + start_pos;
   }

   if ((size_t)start_pos + nChar > getSize()) {
      std::cerr << "getSliceRef: Invalid BinaryData access" << std::endl;
      return {};
   }
   return BinaryDataRef{getPtr()+start_pos, nChar};
}

/////////////////////////////////////////////////////////////////////////////
BinaryData BinaryData::getSliceCopy(ssize_t start_pos, size_t nChar) const
{
   if (start_pos < 0) {
      start_pos = getSize() + start_pos;
   }

   if ((size_t)start_pos + nChar > getSize()) {
      std::cerr << "getSliceCopy: Invalid BinaryData access" << std::endl;
      return {};
   }
   return BinaryData{getPtr()+start_pos, nChar};
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
void BinaryData::createFromHex(const std::string& str)
{
   BinaryDataRef bdr((uint8_t*)str.c_str(), str.size());
   createFromHex(bdr);
}

BinaryData BinaryData::CreateFromHex(const std::string& str)
{
   BinaryData out;
   out.createFromHex(str);
   return out;
}

void BinaryData::createFromHex(const BinaryDataRef& bdr)
{
   if (bdr.getSize() % 2 != 0) {
      LOGERR << "odd hexit count";
      throw std::runtime_error("odd hexit count");
   }
   size_t newLen = bdr.getSize() / 2;
   alloc(newLen);

   auto ptr = bdr.getPtr();
   for (size_t i = 0; i<newLen; i++) {
      uint8_t char1 = binLookupTable[*(ptr + 2 * i)];
      uint8_t char2 = binLookupTable[*(ptr + 2 * i + 1)];
      data_[i] = (char1 << 4) | char2;
   }
}

std::string BinaryData::toHexStr(bool bigEndian) const
{
   if (empty()) {
      return {};
   }

   auto size = getSize();
   std::string outStr{};
   outStr.resize(2*size);

   if (!bigEndian) {
      for (size_t i=0; i<size; i++) {
         uint8_t nextByte = data_[i];
         outStr[2*i  ] = hexLookupTable[ (nextByte >> 4) & 0x0F ];
         outStr[2*i+1] = hexLookupTable[ (nextByte     ) & 0x0F ];
      }
   } else {
      for (size_t i=0; i<size; i++) {
         uint8_t nextByte = data_[size - 1 - i];
         outStr[2*i  ] = hexLookupTable[ (nextByte >> 4) & 0x0F ];
         outStr[2*i+1] = hexLookupTable[ (nextByte     ) & 0x0F ];
      }
   }
   return outStr;
}

std::string BinaryData::toBinStr(bool bigEndian) const
{
   if (empty()) {
      return {};
   }

   if (bigEndian) {
      BinaryData out = copySwapEndian();
      return std::string{out.getCharPtr(), getSize()};
   } else {
      return std::string{getCharPtr(), getSize()};
   }
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryData::isZero() const
{
   bool isZero = true;
   auto ptr = getPtr();
   for (size_t i = 0; i < getSize(); i++) {
      if (ptr[i] != 0) {
         isZero = false;
         break;
      }
   }
   return isZero;
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryData::operator==(const BinaryDataRef& bd2) const
{
   if (!empty()) {
      if (getSize() != bd2.getSize()) {
         return false;
      }
      return (memcmp(getPtr(), bd2.getPtr(), getSize()) == 0);
   }
   return bd2.empty();
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryData::operator<(const BinaryDataRef& bd2) const
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
bool BinaryData::operator<(const BinaryData& bd2) const
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
bool BinaryData::operator>(const BinaryData& bd2) const
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
bool BinaryData::operator==(const BinaryData& bd2) const
{
   if (!empty()) {
      if (getSize() != bd2.getSize()) {
         return false;
      }
      return (memcmp(getPtr(), bd2.getPtr(), getSize()) == 0);
   }
   return bd2.empty();
}

/////////////////////////////////////////////////////////////////////////////
std::size_t std::hash<BinaryData>::operator()(const BinaryData& key) const
{
   if (key.empty()) {
      return 0;
   }
   std::size_t result;
   auto len = std::min(sizeof(std::size_t), key.getSize());
   memcpy(&result, key.getPtr(), len);
   return result;
}

/////////////////////////////////////////////////////////////////////////////
void BinaryData::clear()
{
   data_.clear();
}

std::vector<uint8_t> BinaryData::release()
{
   auto vec = std::move(data_);
   clear();
   return vec;
}

const std::vector<uint8_t>& BinaryData::getVector() const
{
   return data_;
}

/////////////////////////////////////////////////////////////////////////////
BinaryData& BinaryData::swapEndian(size_t pos1, size_t pos2)
{
   if (empty()) {
      return *this;
   }

   if (pos2 <= pos1) {
      pos2 = getSize();
   }

   const size_t count = (pos2 - pos1) / 2;
   for (size_t i = 0; i < count; i++) {
      std::swap(data_[pos1 + i], data_[pos2 - (i + 1)]);
   }
   return *this;
}

BinaryData BinaryData::copySwapEndian(size_t pos1, size_t pos2) const
{
   BinaryData bdout{*this};
   bdout.swapEndian(pos1, pos2);
   return bdout;
}

/////////////////////////////////////////////////////////////////////////////
// BinaryDataRef
BinaryDataRef::BinaryDataRef() :
   ptr_(nullptr), nBytes_(0)
{}

BinaryDataRef::BinaryDataRef(const uint8_t* inData, size_t sz)
{
   setRef(inData, sz);
}

BinaryDataRef::BinaryDataRef(const uint8_t* dstart, const uint8_t* dend)
{
   setRef(dstart, dend);
}

BinaryDataRef::BinaryDataRef(const BinaryDataRef& bdr)
{
   ptr_ = bdr.ptr_;
   nBytes_ = bdr.nBytes_;
}

BinaryDataRef::BinaryDataRef(const BinaryData& bd)
{
   if (!bd.empty()) {
      ptr_ = bd.getPtr();
      nBytes_ = bd.getSize();
   } else {
      ptr_= nullptr;
      nBytes_ = 0;
   }
}

void BinaryDataRef::reset()
{
   ptr_ = nullptr;
   nBytes_ = 0;
}

/////////////////////////////////////////////////////////////////////////////
const uint8_t* BinaryDataRef::getPtr() const
{
   return ptr_;
}

size_t BinaryDataRef::getSize() const
{
   return nBytes_;
}

bool BinaryDataRef::empty() const
{
   return nBytes_ == 0;
}

bool BinaryDataRef::isValid() const
{
   return ptr_ != nullptr;
}

/////////////////////////////////////////////////////////////////////////////
void BinaryDataRef::setRef(const uint8_t* inData, size_t sz)
{
   ptr_ = inData;
   nBytes_ = sz;
}

void BinaryDataRef::setRef(const uint8_t* start, const uint8_t* end)
{
   // [start, end)
   setRef(start, (end-start));
}

void BinaryDataRef::setRef(const std::string& str)
{
   setRef((uint8_t*)str.c_str(), str.size());
}

void BinaryDataRef::setRef(const BinaryData& bd)
{
   setRef(bd.getPtr(), bd.getSize());
}

BinaryDataRef BinaryDataRef::fromString(const std::string& str, size_t len)
{
   if (len == SIZE_MAX) {
      len = str.size();
   }

   BinaryDataRef data;
   data.setRef((const uint8_t*)str.c_str(), len);
   return data;
}

BinaryDataRef BinaryDataRef::fromStringView(
   const std::string_view& sv, size_t len)
{
   if (len == SIZE_MAX) {
      len = sv.size();
   }

   BinaryDataRef data;
   data.setRef((const uint8_t*)sv.data(), len);
   return data;
}

/////////////////////////////////////////////////////////////////////////////
void BinaryDataRef::copyTo(uint8_t* outData) const
{
   memcpy(outData, ptr_, nBytes_);
}

void BinaryDataRef::copyTo(uint8_t* outData, size_t sz) const
{
   memcpy(outData, ptr_, sz);
}

void BinaryDataRef::copyTo(uint8_t* outData, size_t offset, size_t sz) const
{
   memcpy(outData, ptr_+offset, sz);
}

void BinaryDataRef::copyTo(BinaryData& bd) const
{
   if (empty()) {
      return;
   }

   bd.resize(nBytes_);
   memcpy(bd.getPtr(), ptr_, nBytes_);
}

void BinaryDataRef::copyTo(std::string& str)
{
   str.assign((char const *)ptr_, nBytes_);
}

BinaryData BinaryDataRef::copy() const
{
   BinaryData outData(nBytes_);
   copyTo(outData);
   return outData;
}

/////////////////////////////////////////////////////////////////////////////
BinaryDataRef& BinaryDataRef::operator=(const BinaryDataRef& rhs)
{
   setRef(rhs.ptr_, rhs.nBytes_);
   return *this;
}

bool BinaryDataRef::operator<(BinaryDataRef const & bd2) const
{
   size_t minLen = std::min(getSize(), bd2.getSize());
   int result = 0;
   if (minLen != 0) {
      result = memcmp(getPtr(), bd2.getPtr(), minLen);
   }
   if (result != 0) {
      return result < 0;
   }
   return (getSize() < bd2.getSize());
}

bool BinaryDataRef::operator>(BinaryDataRef const & bd2) const
{
   size_t minLen = std::min(getSize(), bd2.getSize());
   int result = 0;
   if (minLen != 0) {
      result = memcmp(getPtr(), bd2.getPtr(), minLen);
   }
   if (result != 0) {
      return result > 0;
   }
   return (getSize() > bd2.getSize());
}

std::size_t std::hash<BinaryDataRef>::operator()(const BinaryDataRef& key) const
{
   if (key.empty()) {
      return 0;
   }
   std::size_t result;
   auto len = std::min(sizeof(std::size_t), key.getSize());
   memcpy(&result, key.getPtr(), len);
   return result;
}

const uint8_t& BinaryDataRef::operator[](ssize_t i) const
{
   return (i<0 ? ptr_[nBytes_+i] : ptr_[i]);
}

bool BinaryDataRef::operator==(const BinaryDataRef& bd2) const
{
   if (nBytes_ != bd2.nBytes_) {
      return false;
   } else if (ptr_ == bd2.ptr_) {
      return true;
   }
   return (memcmp(getPtr(), bd2.getPtr(), getSize()) == 0);
}

bool BinaryDataRef::operator==(BinaryData const & bd2) const
{
   if (nBytes_ != bd2.getSize()) {
      return false;
   } else if (ptr_ == bd2.getPtr()) {
      return true;
   }
   return (memcmp(getPtr(), bd2.getPtr(), getSize()) == 0);
}

bool BinaryDataRef::operator!=(const BinaryDataRef& bd2) const
{
   return !((*this)==bd2);
}

bool BinaryDataRef::operator!=(const BinaryData& bd2) const
{
   return !((*this)==bd2);
}

/////////////////////////////////////////////////////////////////////////////
std::string BinaryDataRef::toBinStr(bool bigEndian) const
{
   if (empty()) {
      return {};
   }

   if (bigEndian) {
      BinaryData out = copy();
      return {out.swapEndian().getCharPtr(), nBytes_};
   } else {
      return std::string{(const char*)ptr_, nBytes_};
   }
}

const char* BinaryDataRef::toCharPtr() const
{
   return (char*)ptr_;
}

/////////////////////////////////////////////////////////////////////////////
int32_t BinaryDataRef::find(const BinaryDataRef& matchStr, uint32_t startPos)
{
   int32_t finalAnswer = -1;
   if (matchStr.empty()) {
      return startPos;
   }

   for (int32_t i=startPos; i<=(int32_t)nBytes_-(int32_t)matchStr.nBytes_; i++) {
      if (matchStr.ptr_[0] != ptr_[i]) {
         continue;
      }

      for(uint32_t j=0; j<matchStr.nBytes_; j++) {
         if (matchStr.ptr_[j] != ptr_[i+j]) {
            break;
         }

         // If we are at this instruction and is the last index, it's a match
         if (j==matchStr.nBytes_-1) {
            finalAnswer = i;
         }
      }

      if (finalAnswer != -1) {
         break;
      }
   }
   return finalAnswer;
}

int32_t BinaryDataRef::find(const BinaryData& matchStr, uint32_t startPos)
{
   BinaryDataRef bdr(matchStr);
   return find(bdr, startPos);
}

bool BinaryDataRef::contains(const BinaryDataRef& matchStr, uint32_t startPos)
{
   return (find(matchStr, startPos) != -1);
}

bool BinaryDataRef::contains(const BinaryData& matchStr, uint32_t startPos)
{
   BinaryDataRef bdr(matchStr);
   return (find(bdr, startPos) != -1);
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryDataRef::startsWith(const BinaryDataRef& matchStr) const
{
   if (matchStr.getSize() > nBytes_) {
      return false;
   }

   for (uint32_t i=0; i<matchStr.getSize(); i++) {
      if (matchStr[i] != (*this)[i]) {
         return false;
      }
   }
   return true;
}

bool BinaryDataRef::startsWith(const BinaryData& matchStr) const
{
   if (matchStr.getSize() > nBytes_) {
      return false;
   }
   for (uint32_t i=0; i<matchStr.getSize(); i++) {
      if (matchStr[i] != (*this)[i]) {
         return false;
      }
   }
   return true;
}

bool BinaryDataRef::endsWith(const BinaryDataRef& matchStr) const
{
   size_t sz = matchStr.getSize();
   if (sz > nBytes_) {
      return false;
   }

   for (size_t i=0; i<sz; i++) {
      if (matchStr[sz-(i+1)] != (*this)[nBytes_-(i+1)]) {
         return false;
      }
   }
   return true;
}

bool BinaryDataRef::endsWith(const BinaryData& matchStr) const
{
   size_t sz = matchStr.getSize();
   if (sz > nBytes_) {
      return false;
   }

   for (size_t i=0; i<sz; i++) {
      if(matchStr[sz-(i+1)] != (*this)[nBytes_-(i+1)]) {
         return false;
      }
   }
   return true;
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef BinaryDataRef::getSliceRef(ssize_t start_pos, size_t nChar) const
{
   if (start_pos < 0) {
      start_pos = nBytes_ + start_pos;
   }

   if (start_pos + nChar > nBytes_) {
      std::cerr << "getSliceRef: Invalid BinaryData access" << std::endl;
      return {};
   }
   return {getPtr()+start_pos, nChar};
}

BinaryData BinaryDataRef::getSliceCopy(ssize_t start_pos, size_t nChar) const
{
   if (start_pos < 0) {
      start_pos = nBytes_ + start_pos;
   }

   if (start_pos + nChar > nBytes_) {
      std::cerr << "getSliceCopy: Invalid BinaryData access" << std::endl;
      return {};
   }
   return {getPtr()+start_pos, nChar};
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryDataRef::isSameRefAs(const BinaryDataRef& bdRef2) const
{
   return (ptr_ == bdRef2.ptr_ && nBytes_ == bdRef2.nBytes_);
}

/////////////////////////////////////////////////////////////////////////////
std::string BinaryDataRef::toHexStr(bool bigEndian) const
{
   if (empty()) {
      return {};
   }

   std::string outStr{};
   outStr.resize(2*nBytes_);

   if (!bigEndian) {
      for (size_t i=0; i<nBytes_; i++) {
         uint8_t nextByte = *(ptr_ + i);
         outStr[2*i  ] = hexLookupTable[ (nextByte >> 4) & 0x0F ];
         outStr[2*i+1] = hexLookupTable[ (nextByte     ) & 0x0F ];
      }
   } else {
      for (size_t i=0; i<nBytes_; i++) {
         uint8_t nextByte = *(ptr_ + nBytes_ - 1 - i);
         outStr[2*i  ] = hexLookupTable[ (nextByte >> 4) & 0x0F ];
         outStr[2*i+1] = hexLookupTable[ (nextByte     ) & 0x0F ];
      }
   }
   return outStr;
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryDataRef::isZero() const
{
   for (unsigned i=0; i<nBytes_; i++) {
      if (ptr_[i] != 0) {
         return false;
      }
   }

   return true;
}

/////////////////////////////////////////////////////////////////////////////
// BinaryReader
BinaryReader::BinaryReader(int sz) :
   bdStr_(sz), pos_(0)
{}

BinaryReader::BinaryReader(const BinaryData& toRead)
{
   setNewData(toRead);
}

BinaryReader::BinaryReader(uint8_t* ptr, size_t nBytes)
{
   setNewData(ptr, nBytes);
}

void BinaryReader::setNewData(const BinaryData& toRead)
{
   bdStr_ = toRead;
   pos_ = 0;
}

void BinaryReader::setNewData(const uint8_t* ptr, size_t nBytes)
{
   bdStr_ = BinaryData(ptr, nBytes);
   pos_ = 0;
}

/////////////////////////////////////////////////////////////////////////////
uint8_t BinaryReader::get_uint8_t()
{
   uint8_t outVal = bdStr_[pos_];
   pos_ += 1;
   return outVal;
}

 uint16_t BinaryReader::get_uint16_t(ENDIAN e)
{
   uint16_t outVal = (
      e==LE ? READ_UINT16_LE(bdStr_.getPtr() + pos_) :
      READ_UINT16_BE(bdStr_.getPtr() + pos_)
   );
   pos_ += 2;
   return outVal;
}

uint32_t BinaryReader::get_uint32_t(ENDIAN e)
{
   uint32_t outVal = (
      e==LE ? READ_UINT32_LE(bdStr_.getPtr() + pos_) :
      READ_UINT32_BE(bdStr_.getPtr() + pos_)
   );
   pos_ += 4;
   return outVal;
}

int32_t BinaryReader::get_int32_t(ENDIAN e)
{
   int32_t outVal = (e == LE ?
      BinaryData::StrToIntLE<int32_t>(bdStr_.getPtr() + pos_) :
      BinaryData::StrToIntBE<int32_t>(bdStr_.getPtr() + pos_));
   pos_ += 4;
   return outVal;
}

uint64_t BinaryReader::get_uint64_t(ENDIAN e)
{
   uint64_t outVal = (
      e==LE ? READ_UINT64_LE(bdStr_.getPtr() + pos_) :
      READ_UINT64_BE(bdStr_.getPtr() + pos_)
   );
   pos_ += 8;
   return outVal;
}

void BinaryReader::get_BinaryData(BinaryData& bdTarget, size_t nBytes)
{
   bdTarget.copyFrom(bdStr_.getPtr() + pos_, nBytes);
   pos_ += nBytes;
}

void BinaryReader::get_BinaryData(uint8_t* targPtr, size_t nBytes)
{
   bdStr_.copyTo(targPtr, pos_, nBytes);
   pos_ += nBytes;
}

BinaryDataRef BinaryReader::get_BinaryDataRef(size_t nBytes)
{
   auto bdr = bdStr_.getSliceRef(pos_, nBytes);
   pos_ += nBytes;
   return bdr;
}

uint64_t BinaryReader::get_var_int(uint8_t* nRead)
{
   uint8_t nBytes;
   uint64_t varInt = BtcUtils::readVarInt(
      bdStr_.getPtr() + pos_, bdStr_.getSize() - pos_, nBytes);
   if (nRead != NULL) {
      *nRead = nBytes;
   }
   pos_ += nBytes;
   return varInt;
}

std::pair<uint8_t*, size_t> BinaryReader::rotateRemaining()
{
   size_t nRemain = getSizeRemaining();
   memmove(bdStr_.getPtr(), bdStr_.getPtr() + pos_, nRemain);
   pos_ = 0;
   return std::make_pair(bdStr_.getPtr() + nRemain, getSize() - nRemain);
}

/////////////////////////////////////////////////////////////////////////////
void BinaryReader::resetPosition()
{
   pos_ = 0;
}

size_t BinaryReader::getPosition() const
{
   return pos_;
}

size_t BinaryReader::getSize() const
{
   return bdStr_.getSize();
}

size_t BinaryReader::getSizeRemaining() const
{
   return getSize() - pos_;
}

bool BinaryReader::isEndOfStream() const
{
   return pos_ >= getSize();
}

/////////////////////////////////////////////////////////////////////////////
uint8_t* BinaryReader::exposeDataPtr()
{
   return bdStr_.getPtr();
}

const uint8_t* BinaryReader::getCurrPtr()
{
   return bdStr_.getPtr() + pos_;
}

void BinaryReader::advance(size_t nBytes)
{
   pos_ += nBytes;
   pos_ = std::min(pos_, getSize());
}

void BinaryReader::rewind(size_t nBytes)
{
   pos_ -= nBytes;
   pos_ = std::max(pos_, (size_t)0);
}

void BinaryReader::resize(size_t nBytes)
{
   bdStr_.resize(nBytes);
   pos_ = std::min(nBytes, pos_);
}

/////////////////////////////////////////////////////////////////////////////
// BinaryRefReader
BinaryRefReader::BinaryRefReader(size_t sz) :
   bdRef_(), totalSize_(sz)
{
   pos_.store(0, std::memory_order_relaxed);
}

BinaryRefReader::BinaryRefReader(const BinaryRefReader& brr)
{
   bdRef_ = brr.bdRef_;
   totalSize_ = brr.totalSize_;
   pos_.store(
      brr.pos_.load(std::memory_order_relaxed),
      std::memory_order_relaxed);
}

BinaryRefReader::BinaryRefReader(const BinaryData& toRead)
{
   setNewData(toRead);
}

BinaryRefReader::BinaryRefReader(const BinaryDataRef& toRead)
{
   setNewData(toRead);
}

BinaryRefReader::BinaryRefReader(const uint8_t* rawPtr, size_t nBytes)
{
   setNewData(rawPtr, nBytes);
}

BinaryRefReader& BinaryRefReader::operator=(const BinaryRefReader& brr)
{
   if (&brr == this) {
      return *this;
   }

   bdRef_ = brr.bdRef_;
   totalSize_ = brr.totalSize_;
   pos_.store(brr.pos_.load(std::memory_order_relaxed), std::memory_order_relaxed);
   return *this;
}

/////////////////////////////////////////////////////////////////////////////
void BinaryRefReader::setNewData(const BinaryData& toRead)
{
   setNewData(toRead.getPtr(), toRead.getSize());
}

void BinaryRefReader::setNewData(const BinaryDataRef& toRead)
{
   setNewData(toRead.getPtr(), toRead.getSize());
}

void BinaryRefReader::setNewData(const uint8_t* ptr, size_t nBytes)
{
   bdRef_ = BinaryDataRef{ptr, nBytes};
   totalSize_ = nBytes;
   pos_.store(0, std::memory_order_relaxed);
}

/////////////////////////////////////////////////////////////////////////////
uint64_t BinaryRefReader::get_var_int(uint8_t* nRead)
{
   uint8_t nBytes;
   uint64_t varInt = BtcUtils::readVarInt(
      bdRef_.getPtr() + pos_, bdRef_.getSize() - pos_, nBytes);
   if (nRead != NULL) {
      *nRead = nBytes;
   }
   pos_ += nBytes;
   return varInt;
}

/////////////////////////////////////////////////////////////////////////////
void BinaryRefReader::get_BinaryData(BinaryData& bdTarget, uint32_t nBytes)
{
   if (getSizeRemaining() < nBytes) {
      LOGERR << "[get_BinaryData] buffer overflow";
      throw std::runtime_error("[get_BinaryData] buffer overflow");
   }

   bdTarget.copyFrom(bdRef_.getPtr() + pos_, nBytes);
   pos_.fetch_add(nBytes, std::memory_order_relaxed);
}

BinaryData BinaryRefReader::get_BinaryData(uint32_t nBytes)
{
   if (getSizeRemaining() < nBytes) {
      LOGERR << "[get_BinaryData] buffer overflow!";
      LOGERR << "grabbing " << nBytes << 
         " out of " << getSizeRemaining() << " bytes";
      throw std::runtime_error("[get_BinaryData] buffer overflow");
   }

   BinaryData out;
   get_BinaryData(out, nBytes);
   return out;
}

void BinaryRefReader::get_BinaryData(uint8_t* targPtr, uint32_t nBytes)
{
   if (getSizeRemaining() < nBytes) {
      LOGERR << "[get_BinaryData] buffer overflow";
      throw std::runtime_error("[get_BinaryData] buffer overflow");
   }

   bdRef_.copyTo(targPtr, pos_, nBytes);
   pos_.fetch_add(nBytes, std::memory_order_relaxed);
}

BinaryDataRef BinaryRefReader::get_BinaryDataRef(uint32_t nBytes)
{
   if (getSizeRemaining() < nBytes) {
      LOGERR << "[get_BinaryDataRef] buffer overflow";
      throw std::runtime_error("[get_BinaryDataRef] buffer overflow");
   }

   BinaryDataRef bdrefout(bdRef_.getPtr() + pos_, nBytes);
   pos_.fetch_add(nBytes, std::memory_order_relaxed);
   return bdrefout;
}

std::string BinaryRefReader::get_String(uint32_t nBytes)
{
   std::string strOut{bdRef_.toCharPtr() + pos_, nBytes};
   pos_.fetch_add(nBytes, std::memory_order_relaxed);
   return strOut;
}

/////////////////////////////////////////////////////////////////////////////
uint8_t BinaryRefReader::get_uint8_t()
{
   if (getSizeRemaining() < 1) {
      LOGERR << "[get_uint8_t] buffer overflow";
      throw std::runtime_error("[get_uint8_t] buffer overflow");
   }
   uint8_t outVal = bdRef_[pos_];
   pos_.fetch_add(1, std::memory_order_relaxed);
   return outVal;
}

uint16_t BinaryRefReader::get_uint16_t(ENDIAN e)
{
   if (getSizeRemaining() < 2) {
      LOGERR << "[get_uint16_t] buffer overflow";
      throw std::runtime_error("[get_uint16_t] buffer overflow");
   }

   uint16_t  outVal = (e==LE ?
      READ_UINT16_LE(bdRef_.getPtr() + pos_) :
      READ_UINT16_BE(bdRef_.getPtr() + pos_));
   pos_.fetch_add(2, std::memory_order_relaxed);
   return outVal;
}

uint32_t BinaryRefReader::get_uint32_t(ENDIAN e)
{
   if (getSizeRemaining() < 4) {
      LOGERR << "[get_uint32_t] buffer overflow";
      throw std::runtime_error("[get_uint32_t] buffer overflow");
   }

   uint32_t outVal = (e==LE ?
      READ_UINT32_LE(bdRef_.getPtr() + pos_) :
      READ_UINT32_BE(bdRef_.getPtr() + pos_));
   pos_.fetch_add(4, std::memory_order_relaxed);
   return outVal;
}

int32_t BinaryRefReader::get_int32_t(ENDIAN e)
{
   if (getSizeRemaining() < 4) {
      LOGERR << "[get_int32_t] buffer overflow";
      throw std::runtime_error("[get_int32_t] buffer overflow");
   }

   int32_t outVal = (e == LE ?
      BinaryData::StrToIntLE<int32_t>(bdRef_.getPtr() + pos_) :
      BinaryData::StrToIntBE<int32_t>(bdRef_.getPtr() + pos_));
   pos_.fetch_add(4, std::memory_order_relaxed);
   return outVal;
}

uint64_t BinaryRefReader::get_uint64_t(ENDIAN e)
{
   if (getSizeRemaining() < 8) {
      LOGERR << "[get_uint64_t] buffer overflow";
      throw std::runtime_error("[get_uint64_t] buffer overflow");
   }

   uint64_t outVal = (e==LE ?
      READ_UINT64_LE(bdRef_.getPtr() + pos_) :
      READ_UINT64_BE(bdRef_.getPtr() + pos_));
   pos_.fetch_add(8, std::memory_order_relaxed);
   return outVal;
}

int64_t BinaryRefReader::get_int64_t(ENDIAN e)
{
   if (getSizeRemaining() < 8) {
      LOGERR << "[get_int64_t] buffer overflow";
      throw std::runtime_error("[get_int64_t] buffer overflow");
   }

   int64_t outVal = (e == LE ?
      BinaryData::StrToIntLE<int64_t>(bdRef_.getPtr() + pos_) :
      BinaryData::StrToIntBE<int64_t>(bdRef_.getPtr() + pos_));
   pos_.fetch_add(8, std::memory_order_relaxed);
   return outVal;
}

double BinaryRefReader::get_double()
{
   if (getSizeRemaining() < 8) {
      LOGERR << "[get_double] buffer overflow";
      throw std::runtime_error("[get_double] buffer overflow");
   }

   auto doublePtr = (double*)(bdRef_.getPtr() + pos_);
   pos_.fetch_add(8, std::memory_order_relaxed);
   return *doublePtr;
}

/////////////////////////////////////////////////////////////////////////////
BinaryRefReader BinaryRefReader::fork() const
{
   return BinaryRefReader{
      bdRef_.getPtr() + pos_.load(std::memory_order_relaxed),
      getSizeRemaining()};
}

/////////////////////////////////////////////////////////////////////////////
void BinaryRefReader::advance(size_t nBytes)
{
   if (getSizeRemaining() < nBytes) {
      throw std::runtime_error("[advance] buffer overflow");
   }
   pos_.fetch_add(nBytes, std::memory_order_relaxed);
}

void BinaryRefReader::rewind(uint32_t nBytes)
{
   size_t start = pos_.load(std::memory_order_relaxed);
   pos_.fetch_sub(nBytes, std::memory_order_relaxed);
   if (pos_.load(std::memory_order_relaxed) > start) {
      pos_.store(0, std::memory_order_relaxed);
   }
}

void BinaryRefReader::resetPosition()
{
   pos_ = 0;
}

size_t BinaryRefReader::getPosition() const
{
   return pos_;
}

size_t BinaryRefReader::getSize() const
{
   return totalSize_;
}

bool BinaryRefReader::empty() const
{
   return totalSize_ == 0;
}

size_t BinaryRefReader::getSizeRemaining() const
{
   return totalSize_ - pos_.load(std::memory_order_relaxed);
}

/////////////////////////////////////////////////////////////////////////////
bool BinaryRefReader::isEndOfStream() const
{
   return pos_.load(std::memory_order_relaxed) >= totalSize_;
}

/////////////////////////////////////////////////////////////////////////////
uint8_t const* BinaryRefReader::exposeDataPtr()
{
   return bdRef_.getPtr();
}

uint8_t const* BinaryRefReader::getCurrPtr()
{
   return bdRef_.getPtr() + pos_.load(std::memory_order_relaxed);
}

BinaryDataRef BinaryRefReader::getRawRef()
{
   return bdRef_;
}

/////////////////////////////////////////////////////////////////////////////
// BinaryWriter
BinaryWriter::BinaryWriter(size_t reserveSize) :
   theString_(0)
{
   if (reserveSize != 0) {
      theString_.reserve(reserveSize);
   }
}

void BinaryWriter::reserve(size_t sz)
{
   theString_.reserve(sz);
}

/////////////////////////////////////////////////////////////////////////////
void BinaryWriter::put_uint8_t(const uint8_t& val)
{
   theString_.append(val);
}

void BinaryWriter::put_uint16_t(const uint16_t& val, ENDIAN e)
{
   auto end = theString_.getSize();
   theString_.resize(end + 2);

   if (e == LE) {
      memcpy(theString_.getPtr() + end, &val, 2);
   } else {
      auto ptr = (const uint8_t*)&val;
      theString_[end] = ptr[1];
      theString_[end+1] = ptr[0];
   }
}

void BinaryWriter::put_uint32_t(const uint32_t& val, ENDIAN e)
{
   auto end = theString_.getSize();
   theString_.resize(end + 4);

   if (e == LE) {
      memcpy(theString_.getPtr() + end, &val, 4);
   } else {
      auto ptr = (const uint8_t*)&val;
      theString_[end]   = ptr[3];
      theString_[end+1] = ptr[2];
      theString_[end+2] = ptr[1];
      theString_[end+3] = ptr[0];
   }
}

void BinaryWriter::put_int32_t(const int32_t& val, ENDIAN e)
{
   auto end = theString_.getSize();
   theString_.resize(end + 4);

   if (e == LE) {
      memcpy(theString_.getPtr() + end, &val, 4);
   } else {
      int copyVal = val;
      theString_[end+3] = copyVal % 256;
      theString_[end+2] = (copyVal >>= 8) % 256;
      theString_[end+1] = (copyVal >>= 8) % 256;
      theString_[end]   = (copyVal >>= 8) % 256;
   }
}

void BinaryWriter::put_uint64_t(const uint64_t& val, ENDIAN e)
{
   auto end = theString_.getSize();
   theString_.resize(end + 8);

   if (e == LE) {
      memcpy(theString_.getPtr() + end, &val, 8);
   } else {
      auto ptr = (const uint8_t*)&val;
      theString_[end]   = ptr[7];
      theString_[end+1] = ptr[6];
      theString_[end+2] = ptr[5];
      theString_[end+3] = ptr[4];
      theString_[end+4] = ptr[3];
      theString_[end+5] = ptr[2];
      theString_[end+6] = ptr[1];
      theString_[end+7] = ptr[0];
   }
}

void BinaryWriter::put_double(const double& val)
{
   auto end = theString_.getSize();
   theString_.resize(end + 8);
   memcpy(theString_.getPtr() + end, &val, 8);
}

uint8_t BinaryWriter::put_var_int(const uint64_t& val)
{
   if (val < 0xfd) {
      put_uint8_t((uint8_t)val);
      return 1;
   } else if(val <= UINT16_MAX) {
      put_uint8_t(0xfd);
      put_uint16_t((uint16_t)val);
      return 3;
   }
   else if(val <= UINT32_MAX) {
      put_uint8_t(0xfe);
      put_uint32_t((uint32_t)val);
      return 5;
   } else {
      put_uint8_t(0xff);
      put_uint64_t(val);
      return 9;
   }
}

/////////////////////////////////////////////////////////////////////////////
void BinaryWriter::put_BinaryData(const BinaryData& str,
   size_t offset, uint32_t sz)
{
   put_BinaryDataRef(str.getRef(), offset, sz);
}

void BinaryWriter::put_BinaryDataRef(const BinaryDataRef& str,
   size_t offset, uint32_t sz)
{
   if (offset==0) {
      if (sz==0) {
         theString_.append(str);
      } else {
         theString_.append(str.getPtr(), sz);
      }
   } else {
      if (sz==0) {
         theString_.append(str.getPtr() + offset, str.getSize() - offset);
      } else {
         theString_.append(str.getPtr() + offset, sz);
      }
   }
}

void BinaryWriter::put_BinaryData(uint8_t const * targPtr, uint32_t nBytes)
{
   theString_.append(targPtr, nBytes);
}

void BinaryWriter::put_String(const std::string& str)
{
   theString_.append((const uint8_t*)str.c_str(), str.size());
}

void BinaryWriter::put_StringView(const std::string_view& str)
{
   theString_.append((const uint8_t*)str.data(), str.size());
}

/////////////////////////////////////////////////////////////////////////////
const BinaryData& BinaryWriter::getData() const
{
   return theString_;
}

size_t BinaryWriter::getSize() const
{
   return theString_.getSize();
}

BinaryDataRef BinaryWriter::getDataRef() const
{
   return theString_.getRef();
}

std::string BinaryWriter::toString() const
{
   return theString_.toBinStr();
}

std::string BinaryWriter::toHex() const
{
   return theString_.toHexStr();
}

void BinaryWriter::reset()
{
   theString_.resize(0);
}
