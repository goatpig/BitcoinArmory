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

#include <list>
#include <vector>
#include <functional>
#include <filesystem>

#include <Utils/BinaryData.h>
#include <Utils/Types.h>
#include "lmdbpp.h"

#define SHARD_FILTER_DBKEY          0xAC28337D

class TxFilterPoolWriter;
struct StoredDBInfo;
struct TxOutData;

enum class DB_SELECT : int;
enum class ARMORY_DB_TYPE : int;

////
struct FilterException : public std::runtime_error
{
   FilterException(const std::string&);
};

struct LmdbWrapperException : public std::runtime_error
{
   LmdbWrapperException(const std::string&);
};

////////////////////////////////////////////////////////////////////////////////
//
// Create & manage a bunch of different databases
//
////////////////////////////////////////////////////////////////////////////////

#define KVLIST std::vector<std::pair<BinaryData,BinaryData> > 

class Tx;
class TxIn;
class TxOut;
class TxRef;

struct StoredHeader;
struct StoredTx;
struct StoredTxOut;

enum class DbPrefix : uint8_t;

namespace Armory
{
   class BlockHeader;
}

////////////////////////////////////////////////////////////////////////////////
class DBIterator
{
private:
   LMDB::Iterator iter_;

public:
   DBIterator(LMDB::Iterator&&);
   ~DBIterator(void);

   bool isNull(void) const;
   bool isValid(void) const;
   bool isValid(DbPrefix);

   bool readIterData(void);
   bool retreat(void);
   bool advance(void);
   bool advance(DbPrefix);
   bool advanceAndRead(void);
   bool advanceAndRead(DbPrefix);

   BinaryData       getKey(void) const;
   BinaryData       getValue(void) const;
   BinaryDataRef    getKeyRef(void) const;
   BinaryDataRef    getValueRef(void) const;
   BinaryRefReader& getKeyReader(void) const;
   BinaryRefReader& getValueReader(void) const;

   // All the seekTo* methods do the exact same thing, the variant simply
   // determines the meaning of the return true/false value.
   bool seekTo(BinaryDataRef);
   bool seekTo(DbPrefix, BinaryDataRef);
   bool seekToExact(BinaryDataRef);
   bool seekToExact(DbPrefix, BinaryDataRef);
   bool seekToStartsWith(BinaryDataRef);
   bool seekToStartsWith(DbPrefix);
   bool seekToStartsWith(DbPrefix, BinaryDataRef);
   bool seekToBefore(BinaryDataRef);
   bool seekToBefore(DbPrefix);
   bool seekToBefore(DbPrefix, BinaryDataRef);
   bool seekToFirst(void);
   bool seekToLast(void) ;

   // Return true if the iterator is currently on valid data, with key match
   bool checkKeyExact(BinaryDataRef);
   bool checkKeyExact(DbPrefix, BinaryDataRef);
   bool checkKeyStartsWith(BinaryDataRef);
   bool checkKeyStartsWith(DbPrefix, BinaryDataRef);

   bool verifyPrefix(DbPrefix, bool=true);
   void resetReaders(void);

protected:
   mutable BinaryDataRef    currKey_;
   mutable BinaryDataRef    currValue_;
   mutable BinaryRefReader  currKeyReader_;
   mutable BinaryRefReader  currValueReader_;

   bool isDirty_;
};

////////////////////////////////////////////////////////////////////////////////
class DBTransaction
{
private:
   LMDB::Transaction dbtx_;

public:
   DBTransaction(LMDB::Transaction&&);
   ~DBTransaction(void);

   void insert(const LMDB::DataRef&, const LMDB::DataRef&);
   void erase(const LMDB::DataRef&);
   LMDB::DataRef get(const LMDB::DataRef&) const;
   DBIterator getIterator(void) const;
};

////////////////////////////////////////////////////////////////////////////////
class DBPair
{
private:
   LMDB::Env env_;
   LMDB::DB db_;
   size_t mapSize_;

public:
   DBPair(size_t);

   LMDB::Transaction beginTransaction(LMDB::Mode);
   void open(const std::filesystem::path&, const std::string&);
   void close(void);
   bool isOpen(void) const;
};

////////////////////////////////////////////////////////////////////////////////
class DatabaseContainer
{
private:
   const std::filesystem::path baseDir_;
   const std::string name_;
   mutable DBPair db_;

public:
   //tor
   DatabaseContainer(const std::filesystem::path&, const std::string&, size_t);
   ~DatabaseContainer(void);

   //static
   static std::string getDbName(DB_SELECT);

   //virtual
   void open(void);
   void close(void);
   void eraseOnDisk(void);

   std::unique_ptr<DBTransaction> beginTransaction(LMDB::Mode) const;

   StoredDBInfo getStoredDBInfo(uint16_t);
   void putStoredDBInfo(const StoredDBInfo&, uint16_t);
};

////////////////////////////////////////////////////////////////////////////////
class LMDBBlockDatabase
{
private:
   const std::filesystem::path dbDir_;

   std::shared_ptr<DatabaseContainer> getDbPtr(DB_SELECT) const;
   std::shared_ptr<DatabaseContainer> getHashTablePtr(DB_SELECT, uint8_t) const;

public:
   LMDBBlockDatabase(const std::filesystem::path&);
   ~LMDBBlockDatabase(void);

   /////////////////////////////////////////////////////////////////////////////
   void openDatabases(void);
   bool databasesAreOpen(void) const;

   void closeDatabases(void);
   void cycleDatabase(DB_SELECT);
   void destroyAndResetDatabases(void);
   void resetHistoryDatabases(void);

   /////////////////////////////////////////////////////////////////////////////
   std::unique_ptr<DBTransaction> beginTransaction(
      DB_SELECT, LMDB::Mode) const;
   std::unique_ptr<DBTransaction> beginHashTableTx(
      DB_SELECT, uint8_t, LMDB::Mode) const;

   /////////////////////////////////////////////////////////////////////////////
   uint64_t getDBKeyForHash(const Armory::Types::TxHash&) const;

   /////////////////////////////////////////////////////////////////////////////
   // Interface to translate Stored* objects to/from persistent DB storage
   /////////////////////////////////////////////////////////////////////////////
   StoredDBInfo getStoredDBInfo(DB_SELECT, uint16_t);
   void putStoredDBInfo(DB_SELECT, StoredDBInfo const&, uint16_t);

   /////////////////////////////////////////////////////////////////////////////
   // BareHeaders are those in the HEADERS DB with no blockdta associated
   void readAllHeaders(
      const std::function<void(std::shared_ptr<Armory::BlockHeader>)>&);
   void putBareHeader(const StoredHeader&);
   bool getStoredHeader(StoredHeader&,
      std::shared_ptr<Armory::BlockHeader>, bool=true) const;

   /////////////////////////////////////////////////////////////////////////////
   // StoredTx Accessors
   void putStoredZC(StoredTx&, const Armory::Types::TxKey&);
   bool getStoredZC(StoredTx&, const Armory::Types::TxKey&) const;
   void putStoredZcTxOut(const StoredTxOut&, const Armory::Types::TxIOKey&);

   /////////////////////////////////////////////////////////////////////////////
   // TxOut/In history stuff
   std::map<Armory::Types::TxIOKey, TxOutData>
   getTxOutHistoryForScrAddrKey(Armory::Types::ScrAddrId,
      Armory::Types::BlockId, Armory::Types::BlockId) const;
   std::map<Armory::Types::TxIOKey, Armory::Types::TxIOKey>
   getTxInHistoryForTxOutHistory(
      const std::vector<Armory::Types::TxIOKey>&) const;
   Armory::Types::TxIOKey getTxInHistoryForTxOutKey(
      Armory::Types::TxIOKey) const;

   /////////////////////////////////////////////////////////////////////////////
   KVLIST getAllDatabaseEntries(DB_SELECT);
   void   printAllDatabaseEntries(DB_SELECT);

   const std::filesystem::path& baseDir(void) const;

   void closeDB(DB_SELECT);
   void openDB(DB_SELECT);

   ////
   TxFilterPoolWriter getFilterPoolWriter(uint32_t) const;
   BinaryDataRef getFilterPoolDataRef(uint32_t) const;
   void putFilterPoolForFileNum(uint32_t, const TxFilterPoolWriter& pool);

   void putMissingHashes(const std::set<BinaryData>&, uint32_t);
   std::set<BinaryData> getMissingHashes(uint32_t) const;

   //// block files flagged for reparsing
   bool getOrSetFlaggedBlockFile(uint32_t);
   std::vector<uint32_t> getFlaggedFileNums(void) const;
   void clearFlaggedFileNums(void);

public:
   std::map<DB_SELECT, std::shared_ptr<DatabaseContainer>> dbMap_;
   std::map<DB_SELECT, std::vector<std::shared_ptr<DatabaseContainer>>> dbHashTables_;

private:
   bool     dbIsOpen_;
   uint32_t ldbBlockSize_;
};
