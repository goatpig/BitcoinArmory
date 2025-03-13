////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2024, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_DBUTILS
#define _H_DBUTILS

#include <string>
#include <filesystem>
#include "BinaryData.h"

enum BLKDATA_TYPE
{
   NOT_BLKDATA,
   BLKDATA_HEADER,
   BLKDATA_TX,
   BLKDATA_TXOUT
};

enum DB_PREFIX
{
   DB_PREFIX_DBINFO = 0,
   DB_PREFIX_HEADHASH,
   DB_PREFIX_HEADHGT,
   DB_PREFIX_TXDATA,
   DB_PREFIX_TXHINTS,
   DB_PREFIX_SCRIPT,
   DB_PREFIX_UNDODATA,
   DB_PREFIX_TRIENODES,
   DB_PREFIX_COUNT,
   DB_PREFIX_ZCDATA,
   DB_PREFIX_POOL,
   DB_PREFIX_MISSING_HASHES,
   DB_PREFIX_SUBSSH,
   DB_PREFIX_TEMPSCRIPT,
   DB_PREFIX_FLAGGED_BLOCKFILES
};

////////
class DBUtils
{
public:
   static const BinaryData ZeroConfHeader_;

public:

   static uint32_t   hgtxToHeight(const BinaryData& hgtx);
   static uint8_t    hgtxToDupID(const BinaryData& hgtx);
   static BinaryData heightAndDupToHgtx(uint32_t hgt, uint8_t dup);

   /////////////////////////////////////////////////////////////////////////////
   static BinaryData getBlkDataKey(uint32_t height,
      uint8_t  dup);

   /////////////////////////////////////////////////////////////////////////////
   static BinaryData getBlkDataKey(uint32_t height,
      uint8_t  dup,
      uint16_t txIdx);

   /////////////////////////////////////////////////////////////////////////////
   static BinaryData getBlkDataKey(uint32_t height,
      uint8_t  dup,
      uint16_t txIdx,
      uint16_t txOutIdx);

   /////////////////////////////////////////////////////////////////////////////
   static BinaryData getBlkDataKeyNoPrefix(uint32_t height,
      uint8_t  dup);

   /////////////////////////////////////////////////////////////////////////////
   static BinaryData getBlkDataKeyNoPrefix(uint32_t height,
      uint8_t  dup,
      uint16_t txIdx);

   /////////////////////////////////////////////////////////////////////////////
   static BinaryData getBlkDataKeyNoPrefix(uint32_t height,
      uint8_t  dup,
      uint16_t txIdx,
      uint16_t txOutIdx);

   /////////////////////////////////////////////////////////////////////////////
   static BLKDATA_TYPE readBlkDataKey(BinaryRefReader & brr,
      uint32_t & height,
      uint8_t  & dupID);

   /////////////////////////////////////////////////////////////////////////////
   static BLKDATA_TYPE readBlkDataKey(BinaryRefReader & brr,
      uint32_t & height,
      uint8_t  & dupID,
      uint16_t & txIdx);

   /////////////////////////////////////////////////////////////////////////////
   static BLKDATA_TYPE readBlkDataKey(BinaryRefReader & brr,
      uint32_t & height,
      uint8_t  & dupID,
      uint16_t & txIdx,
      uint16_t & txOutIdx);
   /////////////////////////////////////////////////////////////////////////////
   static BLKDATA_TYPE readBlkDataKeyNoPrefix(
      BinaryRefReader & brr,
      uint32_t & height,
      uint8_t  & dupID);

   /////////////////////////////////////////////////////////////////////////////
   static BLKDATA_TYPE readBlkDataKeyNoPrefix(
      BinaryRefReader & brr,
      uint32_t & height,
      uint8_t  & dupID,
      uint16_t & txIdx);

   /////////////////////////////////////////////////////////////////////////////
   static BLKDATA_TYPE readBlkDataKeyNoPrefix(
      BinaryRefReader & brr,
      uint32_t & height,
      uint8_t  & dupID,
      uint16_t & txIdx,
      uint16_t & txOutIdx);

   static std::string getPrefixName(uint8_t prefixInt);
   static std::string getPrefixName(DB_PREFIX pref);

   static bool checkPrefixByte(BinaryRefReader & brr,
      DB_PREFIX prefix,
      bool rewindWhenDone = false);
   static bool checkPrefixByteWError(BinaryRefReader & brr,
      DB_PREFIX prefix,
      bool rewindWhenDone = false);

   static BinaryData getFilterPoolKey(uint32_t filenum);
   static BinaryData getMissingHashesKey(uint32_t id);
   static BinaryDataRef getDataRefForPacket(const BinaryDataRef& packet);
};

namespace FileUtils
{
   //used for blk file parsing
   class FileMap
   {
   private:
      size_t offset_ = 0;
      uint8_t* ptr_ = nullptr;
      size_t size_ = 0;

   public:
      FileMap(const std::filesystem::path&, bool write=false, size_t offset=0);
      ~FileMap(void);

      size_t size(void) const;
      uint8_t* ptr(void) const;
      bool isValid(void) const;
   };

   class FileCopy
   {
   private:
      size_t offset_ = 0;
      std::vector<uint8_t> data_;

   public:
      FileCopy(const std::filesystem::path&, size_t offset=0);

      size_t size(void) const;
      const uint8_t* ptr(void) const;
      bool isValid(void) const;
   };

   ////
   bool fileExists(const std::filesystem::path&, int);
   bool isFile(const std::filesystem::path&);
   bool isDir(const std::filesystem::path&);
   size_t getFileSize(const std::filesystem::path&);

   //core blk file naming pattern
   std::filesystem::path getBlkFilename(
      const std::filesystem::path&, uint32_t);
   uint32_t blkPathToIntID(const std::filesystem::path&);

   //used in tests
   bool copy(const std::filesystem::path&,
      const std::filesystem::path&,
      size_t nBytes=SIZE_MAX);
   bool append(const std::filesystem::path&,
      const std::filesystem::path&);

   //folder stuff
   int removeDirectory(const std::filesystem::path&);
   void createDirectory(const std::filesystem::path&);
   std::filesystem::path getUserHomePath(void);

   //filename manipulation
   std::filesystem::path appendTagToPath(
      const std::filesystem::path&,
      const std::string&
   );
};
#endif