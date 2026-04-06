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
#include <filesystem>

#include <Utils/BinaryData.h>
#include <Utils/ThreadSafeClasses.h>
#include "lmdbpp.h"

#define META_SHARD_ID               0xFFFFFFFF
#define SHARD_COUNTER_KEY           0xA76B6C00
#define SHARD_TOPHASH_ID            0xFFAAAA

#define SHARD_FILTER_DBKEY          0xAC28337D

#ifndef UNIT_TESTS
#define SHARD_FILTER_SCRADDR_STEP   1500
#define SHARD_FILTER_SPENTNESS_STEP 5000
#else
#define SHARD_FILTER_SCRADDR_STEP   2
#define SHARD_FILTER_SPENTNESS_STEP 2
#endif

class TxFilterPoolWriter;
struct StoredDBInfo;
struct StoredSubHistory;
class UnspentTxOut;

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

class BlockHeader;
class Tx;
class TxIn;
class TxOut;
class TxRef;

struct StoredHeader;
struct StoredTx;
struct StoredTxOut;
struct StoredScriptHistory;
struct StoredTxHints;
struct StoredHeadHgtList;

enum class DbPrefix : uint8_t;

enum ShardFilterType
{
   ShardFilterType_ScrAddr = 0,
   ShardFilterType_Spentness
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class LDBIter
{
public:
   LDBIter(void);
   virtual ~LDBIter(void) = 0;

   virtual bool isNull(void) const = 0;
   virtual bool isValid(void) const = 0;
   bool isValid(DbPrefix);

   virtual bool readIterData(void) = 0;
   virtual bool retreat(void) = 0;
   virtual bool advance(void) = 0;

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
   virtual bool seekTo(BinaryDataRef) = 0;
   bool seekTo(DbPrefix, BinaryDataRef);
   virtual bool seekToExact(BinaryDataRef) = 0;
   bool seekToExact(DbPrefix, BinaryDataRef);
   bool seekToStartsWith(BinaryDataRef);
   bool seekToStartsWith(DbPrefix);
   bool seekToStartsWith(DbPrefix, BinaryDataRef);
   virtual bool seekToBefore(BinaryDataRef) = 0;
   bool seekToBefore(DbPrefix);
   bool seekToBefore(DbPrefix, BinaryDataRef);
   virtual bool seekToFirst(void) = 0;
   virtual bool seekToLast(void) = 0;

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
class LDBIter_Single : public LDBIter
{
private:
   LMDB::Iterator iter_;

public:
   LDBIter_Single(LMDB::Iterator&&);

   //virutals
   bool isNull(void) const override;
   bool isValid(void) const override;

   bool seekTo(BinaryDataRef) override;
   bool seekToExact(BinaryDataRef) override;
   bool seekToBefore(BinaryDataRef) override;
   bool seekToFirst(void) override;
   bool seekToLast(void) override;

   bool advance(void) override;
   bool retreat(void) override;
   bool readIterData(void) override;
};

////////////////////////////////////////////////////////////////////////////////
class DBPair
{
private:
   LMDBEnv env_;
   LMDB db_;
   const unsigned id_;

public:
   DBPair(unsigned);

   LMDBEnv::Transaction beginTransaction(LMDB::Mode);
   void open(const std::filesystem::path&, const std::string&);
   void close(void);

   BinaryDataRef getValue(BinaryDataRef) const;
   void putValue(BinaryDataRef, BinaryDataRef);
   void deleteValue(BinaryDataRef);
   
   std::unique_ptr<LDBIter_Single> getIterator(void);
   unsigned getId(void) const;
   bool isOpen(void) const;
   LMDBEnv* getEnv(void);
};

////////////////////////////////////////////////////////////////////////////////
class DbTransaction
{
public:
   DbTransaction(void);
   virtual ~DbTransaction(void) = 0;
};

////////
class DbTransaction_Single : public DbTransaction
{
private:
   LMDBEnv::Transaction dbtx_;

public:
   DbTransaction_Single(LMDBEnv::Transaction&&);
};

////////////////////////////////////////////////////////////////////////////////
class DatabaseContainer
{
protected:
   const DB_SELECT dbSelect_;

public:
   static std::filesystem::path baseDir_;
   static BinaryData magicBytes_;

public:
   //tor
   DatabaseContainer(DB_SELECT);
   virtual ~DatabaseContainer(void) = 0;

   //static
   static std::filesystem::path getDbPath(DB_SELECT);
   static std::filesystem::path getDbPath(const std::string&);
   static std::string getDbName(DB_SELECT);

   //virtual
   virtual StoredDBInfo open(void) = 0;
   virtual void close(void) = 0;
   virtual void eraseOnDisk(void) = 0;

   virtual std::unique_ptr<DbTransaction> beginTransaction(LMDB::Mode) const = 0;
   virtual std::unique_ptr<LDBIter> getIterator(void) = 0;
   
   virtual BinaryDataRef getValue(BinaryDataRef) const = 0;
   virtual void putValue(BinaryDataRef, BinaryDataRef) = 0;
   virtual void deleteValue(BinaryDataRef) = 0;

   virtual StoredDBInfo getStoredDBInfo(uint32_t) = 0;
   virtual void putStoredDBInfo(const StoredDBInfo&, uint32_t) = 0;
};

////////////////////////////////////////////////////////////////////////////////
class DatabaseContainer_Single : public DatabaseContainer
{
private:
   mutable DBPair db_;

public:
   DatabaseContainer_Single(DB_SELECT);
   ~DatabaseContainer_Single(void) override;

   //virtuals
   StoredDBInfo open(void) override;
   void close(void) override;
   void eraseOnDisk(void) override;

   std::unique_ptr<DbTransaction> beginTransaction(LMDB::Mode) const override;
   std::unique_ptr<LDBIter> getIterator(void) override;

   BinaryDataRef getValue(BinaryDataRef) const override;
   void putValue(BinaryDataRef, BinaryDataRef) override;
   void deleteValue(BinaryDataRef) override;

   StoredDBInfo getStoredDBInfo(uint32_t) override;
   void putStoredDBInfo(const StoredDBInfo&, uint32_t) override;
};

////////////////////////////////////////////////////////////////////////////////
struct ShardFilter
{
   virtual ~ShardFilter(void) = 0;
   virtual unsigned keyToId(BinaryDataRef) const = 0;
   virtual unsigned getHeightForId(unsigned) const = 0;
   virtual BinaryData serialize(void) const = 0;

   static std::unique_ptr<ShardFilter> deserialize(BinaryDataRef);
   static BinaryData getDbKey(void);
};

////////
struct ShardFilter_ScrAddr : public ShardFilter
{
   const unsigned step_;
   unsigned thresholdId_;
   unsigned thresholdValue_;

   ShardFilter_ScrAddr(unsigned);

   unsigned keyToId(BinaryDataRef) const override;
   unsigned getHeightForId(unsigned) const override;
   BinaryData serialize(void) const override;

   static std::unique_ptr<ShardFilter> deserialize(BinaryDataRef);
};

////////
struct ShardFilter_Spentness : public ShardFilter
{
   const unsigned step_;
   unsigned thresholdId_;
   unsigned thresholdValue_;

   ShardFilter_Spentness(unsigned);

   unsigned keyToId(BinaryDataRef) const override;
   unsigned getHeightForId(unsigned) const override;
   BinaryData serialize(void) const override;

   static std::unique_ptr<ShardFilter> deserialize(BinaryDataRef);
};

////////////////////////////////////////////////////////////////////////////////
class LMDBBlockDatabase
{
   friend class ShardedSshParser;
   friend class BlockchainScanner_Super;

private:
   std::shared_ptr<DatabaseContainer> getDbPtr(DB_SELECT) const;

public:
   LMDBBlockDatabase(const std::filesystem::path&);
   ~LMDBBlockDatabase(void);

   /////////////////////////////////////////////////////////////////////////////
   void openDatabases(const std::filesystem::path&);

   /////////////////////////////////////////////////////////////////////////////
   void closeDatabases();
   void replaceDatabases(DB_SELECT, const std::string&);
   void cycleDatabase(DB_SELECT);

   /////////////////////////////////////////////////////////////////////////////
   std::unique_ptr<DbTransaction> beginTransaction(DB_SELECT, LMDB::Mode) const;
   ARMORY_DB_TYPE getDbType(void) const;

   /////////////////////////////////////////////////////////////////////////////
   // Sometimes, we just need to nuke everything and start over
   void destroyAndResetDatabases(void);
   void resetHistoryDatabases(void);
   bool databasesAreOpen(void) const;

   /////////////////////////////////////////////////////////////////////////////
   std::unique_ptr<LDBIter> getIterator(DB_SELECT) const;

   /////////////////////////////////////////////////////////////////////////////
   // Get value using BinaryData object.  If you have a string, you can use
   // BinaryData key(string(theStr));
   BinaryDataRef getValueNoCopy(DB_SELECT, BinaryDataRef) const;

   /////////////////////////////////////////////////////////////////////////////
   // Get value using BinaryDataRef object.  The data from the get* call is 
   // actually stored in a member variable, and thus the refs are valid only 
   // until the next get* call.
   BinaryDataRef getValueRef(DB_SELECT, DbPrefix, BinaryDataRef) const;

   /////////////////////////////////////////////////////////////////////////////
   // Same as the getValueRef, in that they are only valid until the next get*
   // call.  These are convenience methods which basically just save us 
   BinaryRefReader getValueReader(DB_SELECT, BinaryDataRef) const;
   BinaryRefReader getValueReader(DB_SELECT, DbPrefix, BinaryDataRef) const;

   BinaryData getDBKeyForHash(BinaryDataRef, uint8_t = UINT8_MAX) const;

   /////////////////////////////////////////////////////////////////////////////
   // Put value based on BinaryDataRefs key and value
   void putValue(DB_SELECT, BinaryDataRef, BinaryDataRef);
   void putValue(DB_SELECT, DbPrefix, BinaryDataRef, BinaryDataRef);

   /////////////////////////////////////////////////////////////////////////////
   // Put value based on BinaryData key. If batch writing, pass in the batch
   void deleteValue(DB_SELECT, BinaryDataRef);
   void deleteValue(DB_SELECT, DbPrefix, BinaryDataRef);

   /////////////////////////////////////////////////////////////////////////////
   void readAllHeaders(
      const std::function<void(std::shared_ptr<BlockHeader>)>&
   );

   std::map<uint32_t, uint32_t> getSSHSummary(BinaryDataRef);
   void resetHistoryForAddressVector(const std::vector<BinaryData>&);

public:
   uint8_t getValidDupIDForHeight(uint32_t) const;
   uint8_t getValidDupIDForHeight_fromDB(uint32_t);
   void setValidDupIDForHeight(
      uint32_t, uint8_t, bool = true);
   void setValidDupIDForHeight(std::map<unsigned, uint8_t>&);
   
   bool isBlockIDOnMainBranch(unsigned) const;
   void setBlockIDBranch(std::map<unsigned, bool>&);

   /////////////////////////////////////////////////////////////////////////////
   // Interface to translate Stored* objects to/from persistent DB storage
   /////////////////////////////////////////////////////////////////////////////
   StoredDBInfo getStoredDBInfo(DB_SELECT, uint32_t);
   void putStoredDBInfo(DB_SELECT, StoredDBInfo const&, uint32_t);

   /////////////////////////////////////////////////////////////////////////////
   // BareHeaders are those int the HEADERS DB with no blockdta associated
   uint8_t putBareHeader(StoredHeader&, bool = true, bool = true);
   bool    getBareHeader(StoredHeader&, uint32_t, uint8_t) const;
   bool    getBareHeader(StoredHeader&, uint32_t) const;
   bool    getBareHeader(StoredHeader&, BinaryDataRef) const;

   /////////////////////////////////////////////////////////////////////////////
   // still using the old name even though no block data is stored anymore
   BinaryData getRawBlock(std::shared_ptr<BlockHeader>) const;
   bool getStoredHeader(StoredHeader&,
      std::shared_ptr<BlockHeader>, bool = true) const;

   /////////////////////////////////////////////////////////////////////////////
   // StoredTx Accessors
   void putStoredZC(StoredTx&, const BinaryData&);
   bool getStoredZC(StoredTx&, BinaryDataRef) const;

   /////////////////////////////////////////////////////////////////////////////
   // StoredTxOut Accessors
   void putStoredTxOut(const StoredTxOut&);
   void putStoredZcTxOut(const StoredTxOut&, const BinaryData&);

   bool getStoredTxOut(StoredTxOut&,
      uint32_t,
      uint8_t,
      uint16_t,
      uint16_t) const;

   bool getStoredTxOut(StoredTxOut&,
      uint32_t,
      uint16_t,
      uint16_t) const;

   bool getStoredTxOut(
      StoredTxOut&, const BinaryData&) const;
   bool getStoredTxOut(
      StoredTxOut&, const BinaryData&, uint16_t) const;
   bool getStoredTxOut(
      StoredTxOut&, const std::shared_ptr<BlockHeader>, uint16_t, uint16_t) const;
   void getSpentness(StoredTxOut&);

   void getUTXOflags(std::map<BinaryData, StoredSubHistory>&) const;
   void getUTXOflags(StoredSubHistory&) const;
   void getUTXOflags_Super(StoredSubHistory&) const;

   /////////////////////////////////////////////////////////////////////////////
   // StoredScriptHistory Accessors
   void putStoredScriptHistorySummary(StoredScriptHistory&);

   bool getStoredScriptHistory(StoredScriptHistory&,
      BinaryDataRef,
      uint32_t = 0,
      uint32_t = UINT32_MAX) const;

   bool getStoredSubHistoryAtHgtX(StoredSubHistory&,
      const BinaryDataRef, const BinaryData&) const;
   
   bool getStoredSubHistoryAtHgtX(StoredSubHistory&,
      const BinaryData&) const;

   bool getStoredScriptHistorySummary(StoredScriptHistory&,
      BinaryDataRef) const;

   void getStoredScriptHistoryByRawScript(
      StoredScriptHistory&,
      BinaryDataRef) const;
   
   bool fillStoredSubHistory(StoredScriptHistory&, unsigned, unsigned) const;
   bool fillStoredSubHistory_Super(StoredScriptHistory&, unsigned, unsigned) const;

   // This method breaks from the convention I've used for getting/putting 
   // stored objects, because we never really handle Sub-ssh objects directly,
   // but we do need to harness them.  This method could be renamed to
   // "getPartialScriptHistory()" ... it reads the main 
   // sub-ssh from DB and adds it to the supplied regular-ssh.
   bool fetchStoredSubHistory(StoredScriptHistory&,
      BinaryData,
      bool = false,
      bool = false);

   // This could go in StoredBlockObj if it didn't need to lookup DB data
   bool getFullUTXOMapForSSH(StoredScriptHistory&,
      std::map<BinaryData, UnspentTxOut>&);

   uint64_t getBalanceForScrAddr(BinaryDataRef, bool = false);

   bool putStoredTxHints(const StoredTxHints&);
   bool getStoredTxHints(StoredTxHints&, BinaryDataRef) const;
   void updatePreferredTxHint(BinaryDataRef, BinaryData);

   bool putStoredHeadHgtList(const StoredHeadHgtList&);
   bool getStoredHeadHgtList(StoredHeadHgtList&, uint32_t) const;

   // TxRefs are much simpler with LDB than the previous FileDataPtr construct
   TxRef getTxRef(BinaryDataRef);
   TxRef getTxRef(BinaryData, uint16_t);
   TxRef getTxRef(uint32_t, uint8_t, uint16_t);

   // Sometimes we already know where the Tx is, but we don't know its hash
   Tx getFullTxCopy(uint16_t, std::shared_ptr<BlockHeader>) const;
   TxOut getTxOutCopy(const BinaryData&, uint16_t,
      std::shared_ptr<BlockHeader>) const;

   // Sometimes we already know where the Tx is, but we don't know its hash
   BinaryData getTxHashForLdbKey(BinaryDataRef,
      std::shared_ptr<BlockHeader>) const;

   ////////////////////////////////////////////////////////////////////////////
   bool markBlockHeaderValid(BinaryDataRef);
   bool markBlockHeaderValid(uint32_t, uint8_t);

   KVLIST getAllDatabaseEntries(DB_SELECT);
   void   printAllDatabaseEntries(DB_SELECT);

   const std::filesystem::path& baseDir(void) const;

   void closeDB(DB_SELECT);
   StoredDBInfo openDB(DB_SELECT);
   void resetSSHdb(void);
   void resetSSHdb_Super(void);

   ////
   TxFilterPoolWriter getFilterPoolWriter(uint32_t) const;
   BinaryDataRef getFilterPoolDataRef(uint32_t) const;
   void putFilterPoolForFileNum(uint32_t, const TxFilterPoolWriter& pool);

   void putMissingHashes(const std::set<BinaryData>&, uint32_t);
   std::set<BinaryData> getMissingHashes(uint32_t) const;

   ////
   void updateHeightToIdMap(std::map<unsigned, unsigned>&);
   void loadHeightToIdMap();
   unsigned getShardIdForHeight(unsigned) const;
   unsigned getNextShardIdForHeight(unsigned) const;

   //// block files flagged for reparsing
   bool getOrSetFlaggedBlockFile(uint32_t);
   std::vector<uint32_t> getFlaggedFileNums(void) const;
   void clearFlaggedFileNums(void);

public:
   std::map<DB_SELECT, std::shared_ptr<DatabaseContainer>> dbMap_;
   const static std::map<std::string, size_t> mapSizes_;

private:
   bool     dbIsOpen_;
   uint32_t ldbBlockSize_;
   uint32_t lowestScannedUpTo_;

   Armory::Threading::TransactionalMap<unsigned, uint8_t> validDupByHeight_;
   Armory::Threading::TransactionalMap<unsigned, bool> blockIDMainChainMap_;

   // In this case, a address is any TxOut script, which is usually
   // just a 25-byte script.  But this generically captures all types
   // of addresses including pubkey-only, P2SH, 
   std::map<BinaryData, StoredScriptHistory> registeredSSHs_;
   std::filesystem::path blkFolder_;
   const static std::set<DB_SELECT> supernodeDBs_;

   Armory::Threading::TransactionalMap<unsigned, unsigned> heightToBatchId_;
};
