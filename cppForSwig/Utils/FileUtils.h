////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2024-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <string>
#include <filesystem>

namespace Armory
{
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

         void close(void);
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
         void xorMe(uint64_t);
         void clear(void);
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
      bool pathExists(const std::filesystem::path&, int);
      bool isFile(const std::filesystem::path&);
      bool isDir(const std::filesystem::path&, int);
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
} //namespace Armory
