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

#pragma once

#include <cstdint>
#include <string>

enum class BLKDATA_TYPE : int
{
   Invalid,
   Header,
   Tx,
   TxOut
};

enum class DbPrefix : uint8_t
{
   HEADHASH = 1,
   HEADHGT,
   TXDATA,
   TXHINTS,
   SCRIPT,
   UNDODATA,
   TRIENODES,
   COUNT,
   ZCDATA,
   POOL,
   MISSING_HASHES,
   SUBSSH,
   TEMPSCRIPT,
   FLAGGED_BLOCKFILES,
   DBINFO = 0xFF,
};

class BinaryData;
class BinaryDataRef;
class BinaryRefReader;

namespace Armory
{
   namespace DBUtils
   {
      extern const BinaryData ZCPrefix;

      uint32_t   hgtxToHeight(const BinaryData&);
      uint8_t    hgtxToDupID(const BinaryData&);
      BinaryData heightAndDupToHgtx(uint32_t, uint8_t);
      bool       keyIsZC(BinaryDataRef);

      ////////
      BinaryData getBlkDataKey(uint32_t, uint8_t);
      BinaryData getBlkDataKey(uint32_t, uint8_t, uint16_t);
      BinaryData getBlkDataKey(uint32_t, uint8_t, uint16_t, uint16_t);
      BinaryData getBlkDataKeyNoPrefix(uint32_t, uint8_t);
      BinaryData getBlkDataKeyNoPrefix(uint32_t, uint8_t, uint16_t);
      BinaryData getBlkDataKeyNoPrefix(uint32_t, uint8_t, uint16_t, uint16_t);
      BinaryData getDBSuperSpentnessKey(uint32_t, uint8_t, uint16_t, uint16_t);

      ////////
      BLKDATA_TYPE readBlkDataKey(BinaryRefReader&, uint32_t&, uint8_t&);
      BLKDATA_TYPE readBlkDataKey(BinaryRefReader&, uint32_t&, uint8_t&, uint16_t&);
      BLKDATA_TYPE readBlkDataKey(BinaryRefReader&, uint32_t&, uint8_t&, uint16_t&,
         uint16_t&);
      BLKDATA_TYPE readBlkDataKeyNoPrefix(BinaryRefReader&, uint32_t&, uint8_t&);
      BLKDATA_TYPE readBlkDataKeyNoPrefix(BinaryRefReader&, uint32_t&, uint8_t&,
         uint16_t&);
      BLKDATA_TYPE readBlkDataKeyNoPrefix(BinaryRefReader&, uint32_t&, uint8_t&,
         uint16_t&, uint16_t&);

      ////////
      std::string getPrefixName(DbPrefix);
      bool checkPrefixByte(BinaryRefReader&, DbPrefix, bool=false);
      bool checkPrefixByteWError(BinaryRefReader&, DbPrefix, bool=false);

      ////////
      BinaryData getFilterPoolKey(uint32_t);
      BinaryData getMissingHashesKey(uint32_t);
      BinaryDataRef getDataRefForPacket(const BinaryDataRef&);
   }
}
