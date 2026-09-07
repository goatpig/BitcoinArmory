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

#pragma once

#include "BinaryData.h"

////////////////////////////////////////////////////////////////////////////////
// Make sure that all crypto information is handled with page-locked data,
// and overwritten when it's destructor is called.  For simplicity, we will
// use this data type for all crypto data, even for data values that aren't
// really sensitive.  We can use the SecureBinaryData(bdObj) to convert our 
// regular strings/BinaryData objects to secure objects
//
class SecureBinaryData : public BinaryData
{
public:
   // We want regular BinaryData, but page-locked and secure destruction
   SecureBinaryData(void);
   SecureBinaryData(size_t);
   SecureBinaryData(BinaryDataRef);
   SecureBinaryData(const uint8_t*, size_t);
   SecureBinaryData(const uint8_t*, const uint8_t*);
   SecureBinaryData(SecureBinaryData&&);
   SecureBinaryData(BinaryData&&);
   SecureBinaryData(const SecureBinaryData&);
   ~SecureBinaryData(void);

   SecureBinaryData copy(void) const;
   SecureBinaryData getSliceCopy(size_t, size_t) const;
   BinaryData getRawCopy(void) const;
   SecureBinaryData copySwapEndian(size_t=0, size_t=0) const;

   void resize(size_t);
   void reserve(size_t);
   SecureBinaryData& append(BinaryDataRef);
   SecureBinaryData& append(uint8_t);
   SecureBinaryData& operator=(const SecureBinaryData&);
   SecureBinaryData& operator=(const BinaryDataRef&);
   SecureBinaryData  operator+(const SecureBinaryData&) const;
   bool operator==(const SecureBinaryData&) const;
   bool operator==(const BinaryData&) const;
   bool operator==(const BinaryDataRef&) const;

   void lockData(void);
   void destroy(void);

   void XOR(const BinaryDataRef&);
   static SecureBinaryData fromString(const std::string&);
   static SecureBinaryData fromStringView(const std::string_view&);
};
