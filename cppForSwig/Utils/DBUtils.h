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

#include <string>
#include <filesystem>
#include "BinaryData.h"

enum class BLKDATA_TYPE : int
{
   Invalid,
   Header,
   Tx,
   TxOut
};

enum class DbPrefix : uint8_t
{
   DBINFO = 0,
   HEADHASH,
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
   FLAGGED_BLOCKFILES
};

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

      std::string getPrefixName(DbPrefix);
      bool checkPrefixByte(BinaryRefReader&, DbPrefix, bool=false);
      bool checkPrefixByteWError(BinaryRefReader&, DbPrefix, bool=false);

      BinaryData getFilterPoolKey(uint32_t);
      BinaryData getMissingHashesKey(uint32_t);
      BinaryDataRef getDataRefForPacket(const BinaryDataRef&);
   }

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
         FileMap(const std::filesystem::path&, bool=false, size_t=0);
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
         FileCopy(const std::filesystem::path&, size_t=0);

         size_t size(void) const;
         const uint8_t* ptr(void) const;
         bool isValid(void) const;
      };

      class BlockDataFileMap
      {
      private:
         const FileMap fileMap_;

      public:
         BlockDataFileMap(const std::filesystem::path&);
         ~BlockDataFileMap(void);

         bool valid(void) const;
         const uint8_t* data(void) const;
         size_t size(void) const;
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
         size_t=SIZE_MAX);
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
   }
}
