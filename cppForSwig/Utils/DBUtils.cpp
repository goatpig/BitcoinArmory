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

#if defined(__MINGW32__) || defined(_MSC_VER)
   #include <windows.h>
#else
   #include <sys/mman.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <string_view>
#include <cstring>

#include "DBUtils.h"


namespace fs = std::filesystem;
using namespace std::string_view_literals;
using namespace Armory;

namespace {
   auto blkFilePrefix = "blk"sv;
}

const BinaryData DBUtils::ZCPrefix = BinaryData::CreateFromHex("FFFF");

////////////////////////////////////////////////////////////////////////////////
// DBUtils
BLKDATA_TYPE DBUtils::readBlkDataKey(BinaryRefReader& brr,
   uint32_t& height, uint8_t& dupID)
{
   uint16_t tempTxIdx;
   uint16_t tempTxOutIdx;
   return readBlkDataKey(brr, height, dupID, tempTxIdx, tempTxOutIdx);
}

BLKDATA_TYPE DBUtils::readBlkDataKey(BinaryRefReader& brr,
   uint32_t& height, uint8_t& dupID, uint16_t& txIdx)
{
   uint16_t tempTxOutIdx;
   return readBlkDataKey(brr, height, dupID, txIdx, tempTxOutIdx);
}

BLKDATA_TYPE DBUtils::readBlkDataKey(BinaryRefReader & brr,
   uint32_t& height, uint8_t& dupID, uint16_t& txIdx, uint16_t& txOutIdx)
{
   uint8_t prefix = brr.get_uint8_t();
   if (prefix != (uint8_t)DbPrefix::TXDATA) {
      height = 0xffffffff;
      dupID = 0xff;
      txIdx = 0xffff;
      txOutIdx = 0xffff;
      return BLKDATA_TYPE::Invalid;
   }
   return readBlkDataKeyNoPrefix(brr, height, dupID, txIdx, txOutIdx);
}

BLKDATA_TYPE DBUtils::readBlkDataKeyNoPrefix(BinaryRefReader& brr,
   uint32_t& height, uint8_t& dupID)
{
   uint16_t tempTxIdx;
   uint16_t tempTxOutIdx;
   return readBlkDataKeyNoPrefix(brr, height, dupID, tempTxIdx, tempTxOutIdx);
}

BLKDATA_TYPE DBUtils::readBlkDataKeyNoPrefix(
   BinaryRefReader& brr, uint32_t& height, uint8_t& dupID, uint16_t& txIdx)
{
   uint16_t tempTxOutIdx;
   return readBlkDataKeyNoPrefix(brr, height, dupID, txIdx, tempTxOutIdx);
}

BLKDATA_TYPE DBUtils::readBlkDataKeyNoPrefix(BinaryRefReader & brr,
   uint32_t& height, uint8_t& dupID, uint16_t& txIdx, uint16_t& txOutIdx)
{
   BinaryData hgtx = brr.get_BinaryData(4);
   height = hgtxToHeight(hgtx);
   dupID = hgtxToDupID(hgtx);

   if (brr.getSizeRemaining() == 0) {
      txIdx = 0xffff;
      txOutIdx = 0xffff;
      return BLKDATA_TYPE::Header;
   } else if (brr.getSizeRemaining() == 2) {
      txIdx = brr.get_uint16_t(BE);
      txOutIdx = 0xffff;
      return BLKDATA_TYPE::Tx;
   } else if (brr.getSizeRemaining() == 4) {
      txIdx = brr.get_uint16_t(BE);
      txOutIdx = brr.get_uint16_t(BE);
      return BLKDATA_TYPE::TxOut;
   } else {
      LOGERR << "Unexpected bytes remaining: " << brr.getSizeRemaining();
      return BLKDATA_TYPE::Invalid;
   }
}

////////////////////////////////////////////////////////////////////////////////
std::string DBUtils::getPrefixName(DbPrefix pref)
{
   switch (pref)
   {
      case DbPrefix::DBINFO:    return {"DBINFO"};
      case DbPrefix::TXDATA:    return {"TXDATA"};
      case DbPrefix::SCRIPT:    return {"SCRIPT"};
      case DbPrefix::TXHINTS:   return {"TXHINTS"};
      case DbPrefix::TRIENODES: return {"TRIENODES"};
      case DbPrefix::HEADHASH:  return {"HEADHASH"};
      case DbPrefix::HEADHGT:   return {"HEADHGT"};
      case DbPrefix::UNDODATA:  return {"UNDODATA"};
      default:                  return {"<unknown>"};
   }
}

bool DBUtils::checkPrefixByteWError(BinaryRefReader& brr,
   DbPrefix prefix, bool rewindWhenDone)
{
   auto oneByte = (DbPrefix)brr.get_uint8_t();
   bool out;
   if (oneByte == prefix) {
      out = true;
   } else {
      LOGERR << "Unexpected prefix byte: "
         << "Expected: " << getPrefixName(prefix)
         << "Received: " << getPrefixName(oneByte);
      out = false;
   }

   if (rewindWhenDone) {
      brr.rewind(1);
   }
   return out;
}

bool DBUtils::checkPrefixByte(BinaryRefReader& brr,
   DbPrefix prefix, bool rewindWhenDone)
{
   uint8_t oneByte = brr.get_uint8_t();
   bool out = (oneByte == (uint8_t)prefix);

   if (rewindWhenDone) {
      brr.rewind(1);
   }
   return out;
}

/////////////////////////////////////////////////////////////////////////////
BinaryData DBUtils::getBlkDataKey(uint32_t height, uint8_t dup)
{
   BinaryWriter bw(5);
   bw.put_uint8_t((uint8_t)DbPrefix::TXDATA);
   bw.put_BinaryData(heightAndDupToHgtx(height, dup));
   return bw.getData();
}

BinaryData DBUtils::getBlkDataKey(uint32_t height,
   uint8_t dup, uint16_t txIdx)
{
   BinaryWriter bw(7);
   bw.put_uint8_t((uint8_t)DbPrefix::TXDATA);
   bw.put_BinaryData(heightAndDupToHgtx(height, dup));
   bw.put_uint16_t(txIdx, BE);
   return bw.getData();
}

BinaryData DBUtils::getBlkDataKey(uint32_t height,
   uint8_t dup, uint16_t txIdx, uint16_t txOutIdx)
{
   BinaryWriter bw(9);
   bw.put_uint8_t((uint8_t)DbPrefix::TXDATA);
   bw.put_BinaryData(heightAndDupToHgtx(height, dup));
   bw.put_uint16_t(txIdx, BE);
   bw.put_uint16_t(txOutIdx, BE);
   return bw.getData();
}

BinaryData DBUtils::getBlkDataKeyNoPrefix(uint32_t height, uint8_t dup)
{
   return heightAndDupToHgtx(height, dup);
}

BinaryData DBUtils::getBlkDataKeyNoPrefix(uint32_t height,
   uint8_t dup, uint16_t txIdx)
{
   BinaryWriter bw(6);
   bw.put_BinaryData(heightAndDupToHgtx(height, dup));
   bw.put_uint16_t(txIdx, BE);
   return bw.getData();
}

BinaryData DBUtils::getBlkDataKeyNoPrefix(uint32_t height,
   uint8_t dup, uint16_t txIdx, uint16_t txOutIdx)
{
   BinaryWriter bw(8);
   bw.put_BinaryData(heightAndDupToHgtx(height, dup));
   bw.put_uint16_t(txIdx, BE);
   bw.put_uint16_t(txOutIdx, BE);
   return bw.getData();
}

BinaryData DBUtils::getDBSuperSpentnessKey(uint32_t height,
   uint8_t dup, uint16_t txIdx, uint16_t txOutIdx)
{
   return DBUtils::getBlkDataKeyNoPrefix(
      UINT32_MAX - height, dup, txIdx, txOutIdx);
}

/////////////////////////////////////////////////////////////////////////////
uint32_t DBUtils::hgtxToHeight(const BinaryData& hgtx)
{
   return (READ_UINT32_BE(hgtx) >> 8);
}

uint8_t DBUtils::hgtxToDupID(const BinaryData& hgtx)
{
   return (READ_UINT32_BE(hgtx) & 0x7f);
}

BinaryData DBUtils::heightAndDupToHgtx(uint32_t hgt, uint8_t dup)
{
   uint32_t hgtxInt = (hgt << 8) | (uint32_t)dup;
   return WRITE_UINT32_BE(hgtxInt);
}

bool DBUtils::keyIsZC(BinaryDataRef key)
{
   if (key.getSize() < 4) {
      return false;
   }
   uint16_t* keyInt = (uint16_t*)key.getPtr();
   return *keyInt == 0xFFFF;
}

/////////////////////////////////////////////////////////////////////////////
BinaryData DBUtils::getFilterPoolKey(uint32_t filenum)
{
   uint32_t bucketKey = (uint32_t(DbPrefix::POOL) << 24) | (uint32_t)filenum;
   return WRITE_UINT32_BE(bucketKey);
}

BinaryData DBUtils::getMissingHashesKey(uint32_t id)
{
   BinaryData bd;
   bd.resize(4);

   id &= 0x00FFFFFF; //24bit ids top
   id |= uint32_t(DbPrefix::MISSING_HASHES) << 24;

   auto keyPtr = (uint32_t*)bd.getPtr();
   *keyPtr = id;
   return bd;
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef DBUtils::getDataRefForPacket(
   const BinaryDataRef& packet)
{
   BinaryRefReader brr(packet);
   auto len = brr.get_var_int();
   if (len != brr.getSizeRemaining()) {
      throw std::runtime_error("on disk data length mismatch");
   }
   return brr.get_BinaryDataRef(brr.getSizeRemaining());
}

/////////////////////////////////////////////////////////////////////////////
// FileMap
FileUtils::FileMap::FileMap(const fs::path& path, bool write, size_t offset)
   : offset_(offset)
{
   int fd = 0;
   if (!fileExists(path, 2)) {
      //false positive warning, we often ask for block files that do not
      //exists as way to check for exhaustion
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

      LOGERR << "FileMap error for path " << path.string() <<
         ", error: " << e.what();
   }
}

////
FileUtils::FileMap::~FileMap()
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
bool FileUtils::FileMap::isValid() const
{
   return ptr_ != nullptr;
}

////
size_t FileUtils::FileMap::size() const
{
   return size_ - offset_;
}

////
uint8_t* FileUtils::FileMap::ptr() const
{
   return ptr_ + offset_;
}

/////////////////////////////////////////////////////////////////////////////
// FileCopy
FileUtils::FileCopy::FileCopy(const fs::path& path, size_t offset)
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

      auto size = _lseek(fd, 0, SEEK_END);
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

      auto size = lseek(fd, 0, SEEK_END);
      if (size == 0) {
         throw std::runtime_error("empty file");
      }
      lseek(fd, offset_, SEEK_SET);
#endif
      if (offset_ >= size) {
         throw std::runtime_error("offset is too large");
      }

      data_.resize(size-offset);

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

////
bool FileUtils::FileCopy::isValid() const
{
   return !data_.empty();
}

////
size_t FileUtils::FileCopy::size() const
{
   return data_.size();
}

////
const uint8_t* FileUtils::FileCopy::ptr() const
{
   return data_.data();
}

/////////////////////////////////////////////////////////////////////////////
// FileUtils
bool FileUtils::fileExists(const fs::path& path, int mode)
{
   try {
      auto result = fs::status(path);
      if (result.type() == fs::file_type::not_found) {
         return false;
      }
      auto filePerms = result.permissions();

      //do we need read permission?
      if ((mode & 2) && (filePerms & fs::perms::owner_read) == fs::perms::none) {
         return false;
      }

      //do we need write permission?
      if ((mode & 4) && (filePerms & fs::perms::owner_write) == fs::perms::none) {
         return false;
      }

      return true;
   } catch (const fs::filesystem_error&) {
      //throw, invalid path/file doesnt exist
      return false;
   }
}

////
bool FileUtils::isFile(const fs::path& path)
{
   auto result = fs::status(path);
   return result.type() == fs::file_type::regular;
}

////
bool FileUtils::isDir(const fs::path& path)
{
   auto result = fs::status(path);
   return result.type() == fs::file_type::directory;
}

////
int FileUtils::removeDirectory(const fs::path& path)
{
   if (!isDir(path)) {
      return -1;
   }

   std::error_code ec;
   if (fs::remove_all(path, ec) == UINTMAX_MAX) {
      return -1;
   }
   return 0;
}

////
void FileUtils::createDirectory(const fs::path& path)
{
   //recursively create directory, inherit parent rights where applicable
   if (path.empty()) {
      return;
   }

   auto result = fs::status(path);
   if (result.type() == fs::file_type::directory) {
      //directory exists, nothing to do
      return;
   } else if (result.type() != fs::file_type::not_found) {
      //something that isn't a directory exists under this path, throw
      throw std::runtime_error("path is not a directory: " + path.string());
   }

   //check parent exists
   auto parent = path.parent_path();
   createDirectory(parent);
   fs::create_directory(path, parent);
}

////////////////////////////////////////////////////////////////////////////////
// This got more complicated when Bitcoin Core 0.8 switched from
// blk0001.dat to blocks/blk00000.dat
fs::path FileUtils::getBlkFilename(const fs::path& path, uint32_t fblkNum)
{
   /// Update:  It's been enough time since the hardfork that just about
   //           everyone must've upgraded to 0.8+ by now... remove pre-0.8
   //           compatibility.
   std::stringstream filename;
   filename << "blk" << std::setw(5) << std::setfill('0') << fblkNum << ".dat";
   return path / filename.str();
}

///
uint32_t FileUtils::blkPathToIntID(const fs::path& path)
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
size_t FileUtils::getFileSize(const fs::path& path)
{
   try {
      return fs::file_size(path);
   } catch (const fs::filesystem_error&) {
      return SIZE_MAX;
   }
}

////////////////////
// Simple method for copying files (works in all OS, probably not efficient)
// This only used in tests so far
bool FileUtils::copy(const fs::path& src, const fs::path& dst, size_t nbytes)
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
bool FileUtils::append(const fs::path& src, const fs::path& dst)
{
   if (!fileExists(dst, 2)) {
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
fs::path FileUtils::getUserHomePath()
{
#ifdef _WIN32
   return fs::path(std::getenv("APPDATA"));
#else
   return fs::path{std::getenv("HOME")};
#endif
}

fs::path FileUtils::appendTagToPath(const fs::path& orig, const std::string& tag)
{
   auto taggedName = fs::path{orig.stem().string() + tag + orig.extension().string()};
   auto origCopy = orig;
   return origCopy.replace_filename(taggedName);
}

/////////////////////////////////////////////////////////////////////////////
// BlockDataFileMap
FileUtils::BlockDataFileMap::BlockDataFileMap(
   const std::filesystem::path& path) :
   fileMap_(path)
{}

FileUtils::BlockDataFileMap::~BlockDataFileMap()
{}

const uint8_t* FileUtils::BlockDataFileMap::data() const
{
   return fileMap_.ptr();
}

size_t FileUtils::BlockDataFileMap::size() const
{
   return fileMap_.size();
}

bool FileUtils::BlockDataFileMap::valid() const
{
   return fileMap_.isValid();
}
