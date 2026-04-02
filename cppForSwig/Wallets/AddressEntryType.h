////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#define ADDRESS_TYPE_PREFIX   0xD8

////
enum AddressEntryType
{
   Default = 0,
   P2PKH = 1,
   P2PK = 2,
   P2WPKH = 3,
   Multisig = 4,
   ScriptHash = 5,
   RawScript = 6,
   Uncompressed = 0x10000000,
   P2SH = 0x40000000,
   P2WSH = 0x80000000
};

#define ADDRESS_NESTED_MASK      0xC0000000
#define ADDRESS_COMPRESSED_MASK  0x10000000
#define ADDRESS_TYPE_MASK        0x0FFFFFFF

#define WITH_COMPRESSED_FLAG(a, b) b ? a : \
   AddressEntryType(a | AddressEntryType::Uncompressed)
