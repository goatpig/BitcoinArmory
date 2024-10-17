////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// This is used to attempt to keep keying material out of swap
// I am stealing this from bitcoin 0.4.0 src, serialize.h
#if defined(__MINGW32__) || defined(_MSC_VER)
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

#include "SecureBinaryData.h"
#include "EncryptionUtils.h"

/////////////////////////////////////////////////////////////////////////////
void SecureBinaryData::lockData()
{
   if (!empty()) {
      mlock(getPtr(), getSize());
   }
}

////
void SecureBinaryData::destroy()
{
   if (!empty())
   {
      fill(0x00);
      munlock(getPtr(), getSize());
   }
   resize(0);
}

/////////////////////////////////////////////////////////////////////////////
// We have to explicitly re-define some of these methods...
SecureBinaryData & SecureBinaryData::append(const SecureBinaryData & sbd2)
{
   if (sbd2.getSize() == 0)
      return (*this);

   if (getSize() == 0)
      BinaryData::copyFrom(sbd2.getPtr(), sbd2.getSize());
   else
      BinaryData::append(sbd2.getRawRef());

   lockData();
   return (*this);
}

/////////////////////////////////////////////////////////////////////////////
SecureBinaryData SecureBinaryData::operator+(const SecureBinaryData & sbd2) const
{
   SecureBinaryData out(getSize() + sbd2.getSize());
   memcpy(out.getPtr(), getPtr(), getSize());
   memcpy(out.getPtr() + getSize(), sbd2.getPtr(), sbd2.getSize());
   out.lockData();
   return out;
}

/////////////////////////////////////////////////////////////////////////////
SecureBinaryData & SecureBinaryData::operator=(SecureBinaryData const & sbd2)
{
   copyFrom(sbd2.getPtr(), sbd2.getSize());
   lockData();
   return (*this);
}

/////////////////////////////////////////////////////////////////////////////
bool SecureBinaryData::operator==(SecureBinaryData const & sbd2) const
{
   if (getSize() != sbd2.getSize())
      return false;
   for (unsigned int i = 0; i < getSize(); i++)
      if ((*this)[i] != sbd2[i])
         return false;
   return true;
}

/////////////////////////////////////////////////////////////////////////////
// Swap endianness of the bytes in the index range [pos1, pos2)
SecureBinaryData SecureBinaryData::copySwapEndian(size_t pos1, size_t pos2) const
{
   return SecureBinaryData(BinaryData::copySwapEndian(pos1, pos2));
}

/////////////////////////////////////////////////////////////////////////////
SecureBinaryData SecureBinaryData::getHash256(void) const
{ 
   SecureBinaryData digest(32);
   CryptoSHA2::getHash256(getRef(), digest.getPtr()); 
   return digest;
}

/////////////////////////////////////////////////////////////////////////////
SecureBinaryData SecureBinaryData::getHash160(void) const
{ 
   SecureBinaryData digest(20);
   CryptoHASH160::getHash160(getRef(), digest.getPtr()); 
   return digest;
}
