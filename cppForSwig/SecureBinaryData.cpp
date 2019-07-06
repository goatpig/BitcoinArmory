////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "EncryptionUtils.h"

using namespace std;

/////////////////////////////////////////////////////////////////////////////
// We have to explicitly re-define some of these methods...
SecureBinaryData & SecureBinaryData::append(SecureBinaryData & sbd2)
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
SecureBinaryData SecureBinaryData::operator+(SecureBinaryData & sbd2) const
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
// Get the 2xSHA256 value for a given SBD object.
SecureBinaryData SecureBinaryData::getHash256(void) const
{ 
   SecureBinaryData digest(32);
   CryptoSHA2::getHash256(getRef(), digest.getPtr()); 
   return digest;
}

/////////////////////////////////////////////////////////////////////////////
// Get the SHA256/RIPEMD-160 value for a given SBD object.
SecureBinaryData SecureBinaryData::getHash160(void) const
{
   SecureBinaryData tempDigest(32);
   SecureBinaryData finalDigest(20);
   CryptoSHA2::getSha256(getRef(), tempDigest.getPtr()); 
   CryptoHASH160::getHash160(tempDigest.getRef(), finalDigest.getPtr());
   return finalDigest;
}
