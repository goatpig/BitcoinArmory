////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2024-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
   #include <windows.h>
#else
   #include <sys/mman.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#include <string_view>
#include <cstring>

#include "FileUtils.h"
#include "log.h"

using namespace Armory;
using namespace Armory::FileUtils;

using namespace std::string_view_literals;

namespace {
   constexpr auto blkFilePrefix = "blk"sv;
   constexpr auto blkFileNumTemplace = "blk{:05}.dat"sv;
}

/////////////////////////////////////////////////////////////////////////////
// FileMap
FileMap::FileMap(const std::filesystem::path& path, bool write, size_t offset)
   : offset_(offset)
{
   int fd = 0;
   if (!pathExists(path, 2)) {
      //false positive warning, we often ask for block files that do not
      //exists as the way to check for exhaustion
      return;
   }

   try {
#ifdef _WIN32
      auto flag = _O_RDONLY | _O_BINARY;
      if (write) {
         flag = _O_RDWR | _O_BINARY;
      }

      fd = _wopen(path.c_str(), flag);
      if (fd == -1) {
         throw std::runtime_error("failed to open file");
      }

      auto size = _lseek(fd, 0, SEEK_END);
      if (size == 0) {
         throw std::runtime_error("empty file");
      }

      _lseek(fd, 0, SEEK_SET);
#else
      auto flag = O_RDONLY;
      if (write) {
         flag = O_RDWR;
      }
      fd = open(path.c_str(), flag);
      if (fd == -1) {
         throw std::runtime_error("failed to open");
      }

      auto size = lseek(fd, 0, SEEK_END);
      if (size == 0) {
         throw std::runtime_error("empty file");
      }

      lseek(fd, 0, SEEK_SET);
#endif
      size_ = size;
      if (offset_ > size_) {
         throw std::runtime_error("offset is too large");
      }

#ifdef _WIN32
      //create mmap
      auto fileHandle = (void*)_get_osfhandle(fd);
      uint32_t sizelo = size & 0xffffffff;
      uint32_t sizehi = size >> 16 >> 16;

      auto mmapflag = PAGE_READONLY;
      if (write) {
         mmapflag = PAGE_READWRITE;
      }
      auto mh = CreateFileMapping(fileHandle, NULL, mmapflag,
         sizehi, sizelo, NULL);
      if (!mh) {
         auto errorCode = GetLastError();
         std::stringstream errStr;
         errStr << errorCode << " (" << std::strerror(errorCode) << ")";
         throw std::runtime_error(errStr.str());
      }

      auto viewFlag = FILE_MAP_READ;
      if (write) {
         viewFlag = FILE_MAP_ALL_ACCESS;
      }
      ptr_ = (uint8_t*)MapViewOfFileEx(mh, viewFlag, 0, 0, size, NULL);
      if (ptr_ == nullptr) {
         auto errorCode = GetLastError();
         std::stringstream errStr;
         errStr << errorCode << " (" << std::strerror(errorCode) << ")";
         throw std::runtime_error(errStr.str());
      }

      CloseHandle(mh);
      _close(fd);
#else
      auto mapFlag = PROT_READ;
      if (write) {
         mapFlag |= PROT_WRITE;
      }
      ptr_ = (uint8_t*)mmap(0, size, mapFlag, MAP_SHARED, fd, 0);
      if (ptr_ == MAP_FAILED) {
         ptr_ = nullptr;
         std::stringstream errStr;
         errStr << errno << " (" << std::strerror(errno) << ")";
         throw std::runtime_error(errStr.str());
      }

      ::close(fd);
#endif
   } catch (const std::runtime_error &e) {
      if (fd != 0) {
#ifdef _WIN32
         _close(fd);
#else
         ::close(fd);
#endif
      }

      LOGERR << "FileMap error for path " << path.string() <<
         ", error: " << e.what();
   }
}

////
FileMap::~FileMap()
{
   close();
}

void FileMap::close()
{
   if (ptr_ != nullptr) {
#ifdef _WIN32
      if (!UnmapViewOfFile(ptr_)) {
         LOGERR << "failed to unmap file";
      }
#else
      if (munmap(ptr_, size_)) {
         LOGERR << "failed to unmap file";
      }
#endif
      ptr_ = nullptr;
   }
}

////
bool FileMap::isValid() const
{
   return ptr_ != nullptr;
}

////
size_t FileMap::size() const
{
   return size_ - offset_;
}

////
uint8_t* FileMap::ptr() const
{
   return ptr_ + offset_;
}

/////////////////////////////////////////////////////////////////////////////
// FileCopy
FileCopy::FileCopy(const std::filesystem::path& path, size_t offset)
   : offset_(offset)
{
   int fd = 0;
   try {
#ifdef _WIN32
      auto flag = _O_RDONLY | _O_BINARY;
      fd = _wopen(path.c_str(), flag);
      if (fd == -1) {
         throw std::runtime_error("failed to open file");
      }

      size_t size = _lseek(fd, 0, SEEK_END);
      if (size == 0) {
         throw std::runtime_error("empty file");
      }
      _lseek(fd, offset_, SEEK_SET);
#else
      auto flag = O_RDONLY;
      fd = open(path.c_str(), flag);
      if (fd == -1) {
         throw std::runtime_error("failed to open");
      }

      size_t size = lseek(fd, 0, SEEK_END);
      if (size == 0) {
         throw std::runtime_error("empty file");
      }
      lseek(fd, offset_, SEEK_SET);
#endif
      if (offset_ >= size) {
         throw std::runtime_error("offset is too large");
      }

      //8 align the buffer
      size_t sizeCount = (size - offset + 7) / 8;
      data_.resize(sizeCount * 8);

#ifdef _WIN32
      _read(fd, &data_[0], size-offset_);
      _close(fd);
#else
      read(fd, &data_[0], size-offset_);
      close(fd);
#endif

   } catch (const std::runtime_error &e) {
      if (fd != 0) {
#ifdef _WIN32
         _close(fd);
#else
         close(fd);
#endif
      }

      LOGERR << "FileCopy error for path: \"" << path.string() <<
         "\" - error: " << e.what();
   }
}

void FileCopy::clear()
{
   data_.clear();
}

////
bool FileCopy::isValid() const
{
   return !data_.empty();
}

////
size_t FileCopy::size() const
{
   return data_.size();
}

////
const uint8_t* FileCopy::ptr() const
{
   return data_.data();
}

void FileCopy::xorMe(uint64_t xorKey)
{
   if (data_.size() % 8 != 0) {
      throw std::length_error("xored block data is misaligned");
   }

   auto data64 = (uint64_t*)&data_[0];
   for (unsigned i = 0; i < data_.size() / 8; i++) {
      data64[i] ^= xorKey;
   }
}

/////////////////////////////////////////////////////////////////////////////
// FileUtils
bool FileUtils::pathExists(const std::filesystem::path& path, int mode)
{
   try {
      auto result = std::filesystem::status(path);
      if (result.type() == std::filesystem::file_type::not_found) {
         return false;
      }
      auto filePerms = result.permissions();

      //do we need read permission?
      if ((mode & 2) && (filePerms & std::filesystem::perms::owner_read) ==
         std::filesystem::perms::none) {
         return false;
      }

      //do we need write permission?
      if ((mode & 4) && (filePerms & std::filesystem::perms::owner_write) ==
         std::filesystem::perms::none) {
         return false;
      }

      //do we need exec permissions
      if ((mode & 8) && (filePerms & std::filesystem::perms::owner_exec) ==
         std::filesystem::perms::none) {
         return false;
      }
      return true;
   } catch (const std::filesystem::filesystem_error&) {
      //throw, invalid path/file doesnt exist
      return false;
   }
}

////
bool FileUtils::isFile(const std::filesystem::path& path)
{
   auto result = std::filesystem::status(path);
   return result.type() == std::filesystem::file_type::regular;
}

////
bool FileUtils::isDir(const std::filesystem::path& path, int mode)
{
   auto result = std::filesystem::status(path);
   if (result.type() != std::filesystem::file_type::directory) {
      return false;
   }
   auto filePerms = result.permissions();

   //do we need read permission?
   if ((mode & 2) && (filePerms & std::filesystem::perms::owner_read) ==
      std::filesystem::perms::none) {
      LOGERR << "lacking read permission for folder " << path.string();
      return false;
   }

   //do we need write permission?
   if ((mode & 4) && (filePerms & std::filesystem::perms::owner_write) ==
      std::filesystem::perms::none) {
      LOGERR << "lacking write permission for folder " << path.string();
      return false;
   }
   return true;
}

////
int FileUtils::removeDirectory(const std::filesystem::path& path)
{
   if (!isDir(path, 4)) {
      return -1;
   }

   std::error_code ec;
   if (std::filesystem::remove_all(path, ec) == UINTMAX_MAX) {
      return -1;
   }
   return 0;
}

////
void FileUtils::createDirectory(const std::filesystem::path& path)
{
   //recursively create directory, inherit parent rights where applicable
   if (path.empty()) {
      return;
   }

   auto result = std::filesystem::status(path);
   if (result.type() == std::filesystem::file_type::directory) {
      //directory exists, nothing to do
      return;
   } else if (result.type() != std::filesystem::file_type::not_found) {
      //something that isn't a directory exists under this path, throw
      throw std::runtime_error("path is not a directory: " + path.string());
   }

   //check parent exists
   auto parent = path.parent_path();
   createDirectory(parent);
   std::filesystem::create_directory(path, parent);
}

////////////////////////////////////////////////////////////////////////////////
// This got more complicated when Bitcoin Core 0.8 switched from
// blk0001.dat to blocks/blk00000.dat
std::filesystem::path FileUtils::getBlkFilename(
   const std::filesystem::path& path, uint32_t fblkNum)
{
   /// Update:  It's been enough time since the hardfork that just about
   //           everyone must've upgraded to 0.8+ by now... remove pre-0.8
   //           compatibility.
   return path / std::format(blkFileNumTemplace, fblkNum);
}

///
uint32_t FileUtils::blkPathToIntID(const std::filesystem::path& path)
{
   auto stem = path.stem().string();
   if (stem.size() < 8 ||
      strncmp(stem.c_str(), blkFilePrefix.data(), 3)) {
      throw std::runtime_error("invalid filename");
   }

   std::string substr{stem.c_str() + 3, 5};
   return std::stoi(substr);
}

////////////////////////////////////////////////////////////////////////////////
size_t FileUtils::getFileSize(const std::filesystem::path& path)
{
   try {
      return std::filesystem::file_size(path);
   } catch (const std::filesystem::filesystem_error&) {
      return SIZE_MAX;
   }
}

////////////////////
// Simple method for copying files (works in all OS, probably not efficient)
// This only used in tests so far
bool FileUtils::copy(const std::filesystem::path& src,
   const std::filesystem::path& dst, size_t nbytes)
{
   //TODO: force unbuffered read
   auto srcsz = getFileSize(src);
   if (srcsz == SIZE_MAX) {
      return false;
   }
   srcsz = std::min(srcsz, nbytes);
   std::vector<char> buffer(srcsz);

   std::ifstream is(src, std::ios::in  | std::ios::binary);
   is.read(buffer.data(), srcsz);

   std::ofstream os(dst, std::ios::out | std::ios::binary);
   os.write(buffer.data(), srcsz);
   os.flush();
   return true;
}

////
bool FileUtils::append(const std::filesystem::path& src,
   const std::filesystem::path& dst)
{
   if (!pathExists(dst, 2)) {
      return false;
   }

   auto srcsz = getFileSize(src);
   if (srcsz == SIZE_MAX) {
      return false;
   }
   std::vector<char> buffer(srcsz);

   std::ifstream is(src.c_str(), std::ios::in  | std::ios::binary);
   is.read(buffer.data(), srcsz);

   std::ofstream os(dst.c_str(), std::ios::app | std::ios::binary);
   os.write(buffer.data(), srcsz);
   os.flush();
   return true;
}

////
std::filesystem::path FileUtils::getUserHomePath()
{
#ifdef _WIN32
   return std::filesystem::path(std::getenv("APPDATA"));
#else
   return std::filesystem::path{std::getenv("HOME")};
#endif
}

std::filesystem::path FileUtils::appendTagToPath(
   const std::filesystem::path& orig, const std::string& tag)
{
   auto taggedName = std::filesystem::path{
      orig.stem().string() + tag + orig.extension().string()};
   auto origCopy = orig;
   return origCopy.replace_filename(taggedName);
}

/////////////////////////////////////////////////////////////////////////////
// BlockDataFileMap
BlockDataFileMap::BlockDataFileMap(
   const std::filesystem::path& path) :
   fileMap_(path)
{}

BlockDataFileMap::~BlockDataFileMap()
{}

const uint8_t* BlockDataFileMap::data() const
{
   return fileMap_.ptr();
}

size_t BlockDataFileMap::size() const
{
   return fileMap_.size();
}

bool BlockDataFileMap::valid() const
{
   return fileMap_.isValid();
}
