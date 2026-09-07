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

// This is used to attempt to keep keying material out of swap
// I am stealing this from bitcoin 0.4.0 src, serialize.h
#ifdef _WIN32
   // Note that VirtualLock does not provide this as a guarantee on Windows,
   // but, in practice, memory that has been VirtualLock'd almost never gets written to
   // the pagefile except in rare circumstances where memory is extremely low.
   #include <windows.h>
   #define mlock(p, n) VirtualLock((p), (n));
   #define munlock(p, n) VirtualUnlock((p), (n));
#else
   #include <sys/mman.h>
   #include <limits.h>
   /* This comes from limits.h if it's not defined there set a sane default */
   #ifndef PAGESIZE
   #include <unistd.h>
   #define PAGESIZE sysconf(_SC_PAGESIZE)
   #endif
   #define mlock(a,b) \
      mlock(((void *)(((size_t)(a)) & (~((PAGESIZE)-1)))),\
      (((((size_t)(a)) + (b) - 1) | ((PAGESIZE) - 1)) + 1) - (((size_t)(a)) & (~((PAGESIZE) - 1))))
   #define munlock(a,b) \
      munlock(((void *)(((size_t)(a)) & (~((PAGESIZE)-1)))),\
      (((((size_t)(a)) + (b) - 1) | ((PAGESIZE) - 1)) + 1) - (((size_t)(a)) & (~((PAGESIZE) - 1))))
#endif

#include <cstring>
#include "SecureBinaryData.h"

/////////////////////////////////////////////////////////////////////////////
// SecureBinaryData
SecureBinaryData::SecureBinaryData() :
   BinaryData{}
{}

SecureBinaryData::SecureBinaryData(size_t sz) :
   BinaryData{}
{
   resize(sz);
   lockData();
}

SecureBinaryData::SecureBinaryData(const uint8_t* inData, size_t sz) :
   BinaryData{inData, sz}
{
   lockData();
}

SecureBinaryData::SecureBinaryData(const uint8_t* d0, const uint8_t* d1) :
   BinaryData{d0, d1}
{
   lockData();
}

SecureBinaryData::SecureBinaryData(BinaryDataRef bdRef) :
   BinaryData{bdRef}
{
   lockData();
}

SecureBinaryData::SecureBinaryData(SecureBinaryData&& mv) :
   BinaryData{}
{
   data_ = std::move(mv.data_);
}

SecureBinaryData::SecureBinaryData(BinaryData&& mv) :
   BinaryData{}
{
   data_ = std::move(mv.release());
   lockData();
}

SecureBinaryData::SecureBinaryData(const SecureBinaryData& sbd2) :
   BinaryData{sbd2.getPtr(), sbd2.getSize()}
{
   lockData();
}

SecureBinaryData::~SecureBinaryData()
{
   destroy();
}

////////
void SecureBinaryData::lockData()
{
   if (!empty()) {
      mlock(getPtr(), getSize());
   }
}


void SecureBinaryData::destroy()
{
   if (!empty()) {
      fill(0x00);
      munlock(getPtr(), getSize());
   }
   resize(0);
}

void SecureBinaryData::resize(size_t sz)
{
   BinaryData::resize(sz);
   lockData();
}

void SecureBinaryData::reserve(size_t sz)
{
   BinaryData::reserve(sz);
   lockData();
}

////////
SecureBinaryData SecureBinaryData::copy() const
{
   return SecureBinaryData{getPtr(), getSize()};
}

SecureBinaryData SecureBinaryData::getSliceCopy(
   size_t start, size_t nchar) const
{
   if (start + nchar > getSize()) {
      throw std::runtime_error("sbd slicecopy overflow");
   }
   return SecureBinaryData{getPtr() + start, nchar};
}

BinaryData SecureBinaryData::getRawCopy() const
{
   return BinaryData{getPtr(), getSize()};
}

SecureBinaryData& SecureBinaryData::append(BinaryDataRef ref)
{
   if (ref.empty()) {
      return (*this);
   }

   if (empty()) {
      BinaryData::copyFrom(ref.getPtr(), ref.getSize());
   } else {
      BinaryData::append(ref);
   }

   lockData();
   return *this;
}

SecureBinaryData& SecureBinaryData::append(uint8_t c)
{
   BinaryData::append(c);
   lockData();
   return *this;
}

////////
SecureBinaryData SecureBinaryData::operator+(const SecureBinaryData& sbd2) const
{
   SecureBinaryData out(getSize() + sbd2.getSize());
   memcpy(out.getPtr(), getPtr(), getSize());
   memcpy(out.getPtr() + getSize(), sbd2.getPtr(), sbd2.getSize());
   out.lockData();
   return out;
}

SecureBinaryData& SecureBinaryData::operator=(const SecureBinaryData& sbd2)
{
   copyFrom(sbd2.getPtr(), sbd2.getSize());
   lockData();
   return *this;
}

SecureBinaryData& SecureBinaryData::operator=(const BinaryDataRef& ref)
{
   copyFrom(ref.getPtr(), ref.getSize());
   lockData();
   return *this;
}

bool SecureBinaryData::operator==(const SecureBinaryData& sbd2) const
{
   if (getSize() != sbd2.getSize()) {
      return false;
   }
   return std::memcmp(getPtr(), sbd2.getPtr(), getSize()) == 0;
}

bool SecureBinaryData::operator==(const BinaryData& bd2) const
{
   if (getSize() != bd2.getSize()) {
      return false;
   }
   return std::memcmp(getPtr(), bd2.getPtr(), getSize()) == 0;
}

bool SecureBinaryData::operator==(const BinaryDataRef& bdr) const
{
   if (getSize() != bdr.getSize()) {
      return false;
   }
   return std::memcmp(getPtr(), bdr.getPtr(), getSize()) == 0;
}

/////////////////////////////////////////////////////////////////////////////
// Swap endianness of the bytes in the index range [pos1, pos2)
SecureBinaryData SecureBinaryData::copySwapEndian(size_t pos1, size_t pos2) const
{
   return SecureBinaryData(BinaryData::copySwapEndian(pos1, pos2));
}

void SecureBinaryData::XOR(const BinaryDataRef& rhs)
{
   if (getSize() > rhs.getSize()) {
      throw std::runtime_error("invalid rhs length");
   }
   for (unsigned i = 0; i < getSize(); i++) {
       auto val = getPtr() + i;
      *val ^= *(rhs.getPtr() + i);
   }
}

////////
SecureBinaryData SecureBinaryData::fromString(const std::string& str)
{
   if (str.empty()) {
      return {};
   }
   SecureBinaryData sbd(str.size());
   memcpy(sbd.getPtr(), str.data(), str.size());
   return sbd;
}

SecureBinaryData SecureBinaryData::fromStringView(const std::string_view& strv)
{
   if (strv.empty()) {
      return {};
   }
   SecureBinaryData sbd(strv.size());
   memcpy(sbd.getPtr(), strv.data(), strv.size());
   return sbd;
}
