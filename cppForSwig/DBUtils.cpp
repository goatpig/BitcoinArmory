////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2024, goatpig                                           //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <filesystem>
#include <fcntl.h>
#include "DBUtils.h"

#ifdef _WIN32
   #include <io.h>
#else
   #include <sys/mman.h>
#endif

////////////////////////////////////////////////////////////////////////////////
const BinaryData DBUtils::ZeroConfHeader_ = BinaryData::CreateFromHex("FFFF");

////////////////////////////////////////////////////////////////////////////////
BLKDATA_TYPE DBUtils::readBlkDataKey(BinaryRefReader& brr,
   uint32_t& height, uint8_t& dupID)
{
   uint16_t tempTxIdx;
   uint16_t tempTxOutIdx;
   return readBlkDataKey(brr, height, dupID, tempTxIdx, tempTxOutIdx);
}

////////////////////////////////////////////////////////////////////////////////
BLKDATA_TYPE DBUtils::readBlkDataKey(BinaryRefReader& brr,
   uint32_t& height, uint8_t& dupID, uint16_t& txIdx)
{
   uint16_t tempTxOutIdx;
   return readBlkDataKey(brr, height, dupID, txIdx, tempTxOutIdx);
}

////////////////////////////////////////////////////////////////////////////////
BLKDATA_TYPE DBUtils::readBlkDataKey(BinaryRefReader & brr,
   uint32_t& height, uint8_t& dupID, uint16_t& txIdx, uint16_t& txOutIdx)
{
   uint8_t prefix = brr.get_uint8_t();
   if (prefix != (uint8_t)DB_PREFIX_TXDATA) {
      height = 0xffffffff;
      dupID = 0xff;
      txIdx = 0xffff;
      txOutIdx = 0xffff;
      return NOT_BLKDATA;
   }

   return readBlkDataKeyNoPrefix(brr, height, dupID, txIdx, txOutIdx);
}

////////////////////////////////////////////////////////////////////////////////
BLKDATA_TYPE DBUtils::readBlkDataKeyNoPrefix(BinaryRefReader& brr,
   uint32_t& height, uint8_t& dupID)
{
   uint16_t tempTxIdx;
   uint16_t tempTxOutIdx;
   return readBlkDataKeyNoPrefix(brr, height, dupID, tempTxIdx, tempTxOutIdx);
}

////////////////////////////////////////////////////////////////////////////////
BLKDATA_TYPE DBUtils::readBlkDataKeyNoPrefix(
   BinaryRefReader& brr, uint32_t& height, uint8_t& dupID, uint16_t& txIdx)
{
   uint16_t tempTxOutIdx;
   return readBlkDataKeyNoPrefix(brr, height, dupID, txIdx, tempTxOutIdx);
}

////////////////////////////////////////////////////////////////////////////////
BLKDATA_TYPE DBUtils::readBlkDataKeyNoPrefix(BinaryRefReader & brr,
   uint32_t& height, uint8_t& dupID, uint16_t& txIdx, uint16_t& txOutIdx)
{
   BinaryData hgtx = brr.get_BinaryData(4);
   height = hgtxToHeight(hgtx);
   dupID = hgtxToDupID(hgtx);

   if (brr.getSizeRemaining() == 0) {
      txIdx = 0xffff;
      txOutIdx = 0xffff;
      return BLKDATA_HEADER;
   } else if (brr.getSizeRemaining() == 2) {
      txIdx = brr.get_uint16_t(BE);
      txOutIdx = 0xffff;
      return BLKDATA_TX;
   } else if (brr.getSizeRemaining() == 4) {
      txIdx = brr.get_uint16_t(BE);
      txOutIdx = brr.get_uint16_t(BE);
      return BLKDATA_TXOUT;
   } else {
      LOGERR << "Unexpected bytes remaining: " << brr.getSizeRemaining();
      return NOT_BLKDATA;
   }
}

////////////////////////////////////////////////////////////////////////////////
std::string DBUtils::getPrefixName(uint8_t prefixInt)
{
   return getPrefixName((DB_PREFIX)prefixInt);
}

////////////////////////////////////////////////////////////////////////////////
std::string DBUtils::getPrefixName(DB_PREFIX pref)
{
   switch (pref)
   {
      case DB_PREFIX_DBINFO:    return std::string("DBINFO");
      case DB_PREFIX_TXDATA:    return std::string("TXDATA");
      case DB_PREFIX_SCRIPT:    return std::string("SCRIPT");
      case DB_PREFIX_TXHINTS:   return std::string("TXHINTS");
      case DB_PREFIX_TRIENODES: return std::string("TRIENODES");
      case DB_PREFIX_HEADHASH:  return std::string("HEADHASH");
      case DB_PREFIX_HEADHGT:   return std::string("HEADHGT");
      case DB_PREFIX_UNDODATA:  return std::string("UNDODATA");
      default:                  return std::string("<unknown>");
   }
}

/////////////////////////////////////////////////////////////////////////////
bool DBUtils::checkPrefixByteWError(BinaryRefReader& brr,
   DB_PREFIX prefix, bool rewindWhenDone)
{
   uint8_t oneByte = brr.get_uint8_t();
   bool out;
   if (oneByte == (uint8_t)prefix) {
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

/////////////////////////////////////////////////////////////////////////////
bool DBUtils::checkPrefixByte(BinaryRefReader& brr,
   DB_PREFIX prefix, bool rewindWhenDone)
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
   bw.put_uint8_t(DB_PREFIX_TXDATA);
   bw.put_BinaryData(heightAndDupToHgtx(height, dup));
   return bw.getData();
}

/////////////////////////////////////////////////////////////////////////////
BinaryData DBUtils::getBlkDataKey(uint32_t height,
   uint8_t dup, uint16_t txIdx)
{
   BinaryWriter bw(7);
   bw.put_uint8_t(DB_PREFIX_TXDATA);
   bw.put_BinaryData(heightAndDupToHgtx(height, dup));
   bw.put_uint16_t(txIdx, BE);
   return bw.getData();
}

/////////////////////////////////////////////////////////////////////////////
BinaryData DBUtils::getBlkDataKey(uint32_t height,
   uint8_t dup, uint16_t txIdx, uint16_t txOutIdx)
{
   BinaryWriter bw(9);
   bw.put_uint8_t(DB_PREFIX_TXDATA);
   bw.put_BinaryData(heightAndDupToHgtx(height, dup));
   bw.put_uint16_t(txIdx, BE);
   bw.put_uint16_t(txOutIdx, BE);
   return bw.getData();
}

/////////////////////////////////////////////////////////////////////////////
BinaryData DBUtils::getBlkDataKeyNoPrefix(uint32_t height, uint8_t dup)
{
   return heightAndDupToHgtx(height, dup);
}

/////////////////////////////////////////////////////////////////////////////
BinaryData DBUtils::getBlkDataKeyNoPrefix(uint32_t height,
   uint8_t dup, uint16_t txIdx)
{
   BinaryWriter bw(6);
   bw.put_BinaryData(heightAndDupToHgtx(height, dup));
   bw.put_uint16_t(txIdx, BE);
   return bw.getData();
}

/////////////////////////////////////////////////////////////////////////////
BinaryData DBUtils::getBlkDataKeyNoPrefix(uint32_t height,
   uint8_t dup, uint16_t txIdx, uint16_t txOutIdx)
{
   BinaryWriter bw(8);
   bw.put_BinaryData(heightAndDupToHgtx(height, dup));
   bw.put_uint16_t(txIdx, BE);
   bw.put_uint16_t(txOutIdx, BE);
   return bw.getData();
}

/////////////////////////////////////////////////////////////////////////////
uint32_t DBUtils::hgtxToHeight(const BinaryData& hgtx)
{
   return (READ_UINT32_BE(hgtx) >> 8);
}

/////////////////////////////////////////////////////////////////////////////
uint8_t DBUtils::hgtxToDupID(const BinaryData& hgtx)
{
   return (READ_UINT32_BE(hgtx) & 0x7f);
}

/////////////////////////////////////////////////////////////////////////////
BinaryData DBUtils::heightAndDupToHgtx(uint32_t hgt, uint8_t dup)
{
   uint32_t hgtxInt = (hgt << 8) | (uint32_t)dup;
   return WRITE_UINT32_BE(hgtxInt);
}

/////////////////////////////////////////////////////////////////////////////
BinaryData DBUtils::getFilterPoolKey(uint32_t filenum)
{
   uint32_t bucketKey = (DB_PREFIX_POOL << 24) | (uint32_t)filenum;
   return WRITE_UINT32_BE(bucketKey);
}

/////////////////////////////////////////////////////////////////////////////
BinaryData DBUtils::getMissingHashesKey(uint32_t id)
{
   BinaryData bd;
   bd.resize(4);

   id &= 0x00FFFFFF; //24bit ids top
   id |= DB_PREFIX_MISSING_HASHES << 24;

   auto keyPtr = (uint32_t*)bd.getPtr();
   *keyPtr = id;
   return bd;
}

/////////////////////////////////////////////////////////////////////////////
bool DBUtils::fileExists(const std::string& path, int mode)
{
   using std::filesystem::perms;
   try {
      auto result = std::filesystem::status(path);
      if (result.type() == std::filesystem::file_type::not_found) {
         return false;
      }
      auto filePerms = result.permissions();

      //do we need read permission?
      if ((mode & 2) && (filePerms & perms::owner_read) == perms::none) {
         return false;
      }

      //do we need write permission?
      if ((mode & 4) && (filePerms & perms::owner_write) == perms::none) {
         return false;
      }

      return true;
   } catch (const std::filesystem::filesystem_error&) {
      //throw, invalid path/file doesnt exist
      return false;
   }
}

/////////////////////////////////////////////////////////////////////////////
void FileMap::unmap()
{
   if (filePtr_ != nullptr)
   {
#ifdef WIN32
      if (!UnmapViewOfFile(filePtr_))
         throw std::runtime_error("failed to unmap file");
#else
      if (munmap(filePtr_, size_))
         throw std::runtime_error("failed to unmap file");
#endif

      filePtr_ = nullptr;
   }
}

/////////////////////////////////////////////////////////////////////////////
FileMap DBUtils::getMmapOfFile(const std::string& path, bool write)
{
   int fd = 0;
   if (!DBUtils::fileExists(path, 2)) {
      throw std::runtime_error("file does not exist");
   }
   FileMap fMap;

   try {
#ifdef _WIN32
      auto flag = _O_RDONLY | _O_BINARY;
      if (write) {
         flag = _O_RDWR | _O_BINARY;
      }

      fd = _open(path.c_str(), flag);
      if (fd == -1) {
         throw std::runtime_error("failed to open file");
      }

      auto size = _lseek(fd, 0, SEEK_END);
      if (size == 0) {
         stringstream ss;
         ss << "empty block file under path: " << path;
         throw ss.str();
      }

      _lseek(fd, 0, SEEK_SET);
#else
      auto flag = O_RDONLY;
      if (write) {
         flag = O_RDWR;
      }
      fd = open(path.c_str(), flag);
      if (fd == -1) {
         throw std::runtime_error("failed to open file");
      }

      auto size = lseek(fd, 0, SEEK_END);
      if (size == 0) {
         std::stringstream ss;
         ss << "empty block file under path: " << path;
         throw ss.str();
      }

      lseek(fd, 0, SEEK_SET);
#endif
      fMap.size_ = size;

#ifdef _WIN32
      //create mmap
      auto fileHandle = (HANDLE)_get_osfhandle(fd);
      HANDLE mh;

      uint32_t sizelo = size & 0xffffffff;
      uint32_t sizehi = size >> 16 >> 16;

      auto mmapflag = PAGE_READONLY;
      if (write) {
         mmapflag = PAGE_READWRITE;
      }
      mh = CreateFileMapping(fileHandle, NULL, mmapflag,
         sizehi, sizelo, NULL);
      if (!mh) {
         auto errorCode = GetLastError();
         stringstream errStr;
         errStr << "Failed to create map of file. Error Code: " <<
            errorCode << " (" << std::strerror(errorCode) << ")";
         throw std::runtime_error(errStr.str());
      }

      auto viewFlag = FILE_MAP_READ;
      if (write) {
         viewFlag = FILE_MAP_ALL_ACCESS;
      }
      fMap.filePtr_ = (uint8_t*)MapViewOfFileEx(mh, viewFlag, 0, 0, size, NULL);
      if (fMap.filePtr_ == nullptr) {
         auto errorCode = GetLastError();
         std::stringstream errStr;
         errStr << "Failed to create map of file. Error Code: " <<
            errorCode << " (" << std::strerror(errorCode) << ")";
         throw std::runtime_error(errStr.str());
      }

      CloseHandle(mh);
      _close(fd);
#else
      auto mapFlag = PROT_READ;
      if (write) {
         mapFlag |= PROT_WRITE;
      }
      fMap.filePtr_ = (uint8_t*)mmap(0, size, mapFlag, MAP_SHARED,
         fd, 0);
      if (fMap.filePtr_ == MAP_FAILED) {
         fMap.filePtr_ = NULL;
         std::stringstream errStr;
         errStr << "Failed to create map of file. Error Code: " <<
            errno << " (" << std::strerror(errno) << ")";
         std::cout << errStr.str() << std::endl;
         throw std::runtime_error(errStr.str());
      }

      close(fd);
#endif
      fd = 0;
   }
   catch (const std::runtime_error &e)
   {
      if (fd != 0)
      {
#ifdef _WIN32
         _close(fd);
#else
         close(fd);
#endif
         fd = 0;
      }

      throw e;
   }

   return fMap;
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

////////////////////////////////////////////////////////////////////////////////
bool DBUtils::isFile(const std::string& path)
{
   auto result = std::filesystem::status(path);
   return result.type() == std::filesystem::file_type::regular;
}

////////////////////////////////////////////////////////////////////////////////
bool DBUtils::isDir(const std::string& path)
{
   auto result = std::filesystem::status(path);
   return result.type() == std::filesystem::file_type::directory;
}

////////////////////////////////////////////////////////////////////////////////
int DBUtils::removeDirectory(const std::string& path)
{
   if (!isDir(path)) {
      return -1;
   }

   std::error_code ec;
   if (std::filesystem::remove_all(path, ec) == UINTMAX_MAX) {
      return -1;
   }
   return 0;
}
