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

#ifndef _LMDB_WRAPPER_
#define _LMDB_WRAPPER_

#include <list>
#include <vector>
#include <filesystem>

#include "log.h"
#include "BinaryData.h"
#include "BtcUtils.h"
#include "DbHeader.h"
#include "BlockObj.h"
#include "StoredBlockObj.h"

#include "lmdbpp.h"
#include "ThreadSafeClasses.h"
#include "ReentrantLock.h"

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

class Blockchain;
class TxFilterPoolWriter;

////
struct FilterException : public std::runtime_error
{
   FilterException(const std::string& err) : std::runtime_error(err)
   {}
};

////
struct DBIterException : public std::runtime_error
{
   DBIterException(const std::string& err) : std::runtime_error(err)
   {}
};

////
struct LmdbWrapperException : public std::runtime_error
{
   LmdbWrapperException(const std::string& err) : std::runtime_error(err)
   {}
};

////
struct SshAccessorException : public std::runtime_error
{
   SshAccessorException(const std::string& err) : std::runtime_error(err)
   {}
};

////
struct SpentnessAccessorException : public std::runtime_error
{
   SpentnessAccessorException(const std::string& err) : std::runtime_error(err)
   {}
};

////
struct DbTxException : public std::runtime_error
{
   DbTxException(const std::string& err) : std::runtime_error(err)
   {}
};


////////////////////////////////////////////////////////////////////////////////
//
// Create & manage a bunch of different databases
//
////////////////////////////////////////////////////////////////////////////////

#define STD_READ_OPTS       leveldb::ReadOptions()
#define STD_WRITE_OPTS      leveldb::WriteOptions()

#define KVLIST std::vector<std::pair<BinaryData,BinaryData> > 

#define DEFAULT_LDB_BLOCK_SIZE 32*1024

// Use this to create iterators that are intended for bulk scanning
// It's actually that the ReadOptions::fill_cache arg needs to be false
#define BULK_SCAN false

class BlockHeader;
class Tx;
class TxIn;
class TxOut;
class TxRef;
class TxIOPair;
class GlobalDBUtilities;

class StoredHeader;
class StoredTx;
struct StoredTxOut;
class StoredScriptHistory;

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

   virtual ~LDBIter(void) = 0;

   // fill_cache argument should be false for large bulk scans
   LDBIter(void) { isDirty_=true;}

   virtual bool isNull(void) const = 0;
   virtual bool isValid(void) const = 0;
   bool isValid(DB_PREFIX dbpref);

   virtual bool readIterData(void) = 0;
   
   virtual bool retreat(void) = 0;
   virtual bool advance(void) = 0;

   bool advance(DB_PREFIX prefix);
   bool advanceAndRead(void);
   bool advanceAndRead(DB_PREFIX prefix);

   BinaryData       getKey(void) const;
   BinaryData       getValue(void) const;
   BinaryDataRef    getKeyRef(void) const;
   BinaryDataRef    getValueRef(void) const;
   BinaryRefReader& getKeyReader(void) const;
   BinaryRefReader& getValueReader(void) const;

   // All the seekTo* methods do the exact same thing, the variant simply 
   // determines the meaning of the return true/false value.
   virtual bool seekTo(BinaryDataRef key) = 0;
   bool seekTo(DB_PREFIX pref, BinaryDataRef key);
   virtual bool seekToExact(BinaryDataRef key) = 0;
   bool seekToExact(DB_PREFIX pref, BinaryDataRef key);
   bool seekToStartsWith(BinaryDataRef key);
   bool seekToStartsWith(DB_PREFIX prefix);
   bool seekToStartsWith(DB_PREFIX pref, BinaryDataRef key);
   virtual bool seekToBefore(BinaryDataRef key) = 0;
   bool seekToBefore(DB_PREFIX prefix);
   bool seekToBefore(DB_PREFIX pref, BinaryDataRef key);
   virtual bool seekToFirst(void) = 0;
   virtual bool seekToLast(void) = 0;

   // Return true if the iterator is currently on valid data, with key match
   bool checkKeyExact(BinaryDataRef key);
   bool checkKeyExact(DB_PREFIX prefix, BinaryDataRef key);
   bool checkKeyStartsWith(BinaryDataRef key);
   bool checkKeyStartsWith(DB_PREFIX prefix, BinaryDataRef key);

   bool verifyPrefix(DB_PREFIX prefix, bool advanceReader=true);

   void resetReaders(void){currKeyReader_.resetPosition();currValueReader_.resetPosition();}

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
   LDBIter_Single(LMDB::Iterator&& iter) :
      iter_(std::move(iter))
   {}

   //virutals
   bool isNull(void) const override { return !iter_.isValid(); }
   bool isValid(void) const override { return iter_.isValid(); }

   bool seekTo(BinaryDataRef key) override;
   bool seekToExact(BinaryDataRef key) override;
   bool seekToBefore(BinaryDataRef key) override;
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
   DBPair(unsigned id) :
      id_(id)
   {}

   LMDBEnv::Transaction beginTransaction(LMDB::Mode mode);
   void open(const std::filesystem::path& path, const std::string& dbName);
   void close(void);

   BinaryDataRef getValue(BinaryDataRef keyWithPrefix) const;
   void putValue(BinaryDataRef key, BinaryDataRef value);
   void deleteValue(BinaryDataRef key);
   
   std::unique_ptr<LDBIter_Single> getIterator(void);
   unsigned getId(void) const { return id_; }

   bool isOpen(void) const;

   LMDBEnv* getEnv(void) { return &env_; }
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class DbTransaction
{
public:
   DbTransaction(void)
   {}

   virtual ~DbTransaction(void) = 0;
};

////////
class DbTransaction_Single : public DbTransaction
{
private:
   LMDBEnv::Transaction dbtx_;

public:
   DbTransaction_Single(LMDBEnv::Transaction&& dbtx) :
      dbtx_(std::move(dbtx))
   {}
};

////////////////////////////////////////////////////////////////////////////////
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
   DatabaseContainer(DB_SELECT dbSelect) :
      dbSelect_(dbSelect)
   {}

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
   
   virtual BinaryDataRef getValue(BinaryDataRef keyWithPrefix) const = 0;
   virtual void putValue(BinaryDataRef key, BinaryDataRef value) = 0;
   virtual void deleteValue(BinaryDataRef key) = 0;

   virtual StoredDBInfo getStoredDBInfo(uint32_t id) = 0;
   virtual void putStoredDBInfo(StoredDBInfo const & sdbi, uint32_t id) = 0;
};

////////////////////////////////////////////////////////////////////////////////
class DatabaseContainer_Single : public DatabaseContainer
{
private:
   mutable DBPair db_;

public:
   DatabaseContainer_Single(DB_SELECT dbSelect) :
      DatabaseContainer(dbSelect), db_(0)
   {}

   ~DatabaseContainer_Single(void)
   {
      close();
   }

   //virtuals
   StoredDBInfo open(void);
   void close(void);
   void eraseOnDisk(void);

   std::unique_ptr<DbTransaction> beginTransaction(LMDB::Mode) const;
   std::unique_ptr<LDBIter> getIterator(void);

   BinaryDataRef getValue(BinaryDataRef key) const;
   void putValue(BinaryDataRef key, BinaryDataRef value);
   void deleteValue(BinaryDataRef key);

   StoredDBInfo getStoredDBInfo(uint32_t id);
   void putStoredDBInfo(StoredDBInfo const & sdbi, uint32_t id);
};

////////////////////////////////////////////////////////////////////////////////
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

   ShardFilter_ScrAddr(unsigned step) : 
      step_(step)
   {
#ifndef UNIT_TESTS
      //x < -exp(step * 1.6 / 50k) / (1 - exp(step * 1.6 / 50k))
      auto eVal = expf(step * 1.6f / 50000.0f);
      thresholdId_ = unsigned(-eVal / (1.0f - eVal));

      //height = (ln(id) / 1.6 + 4) * 50k
      thresholdValue_ = unsigned((logf(thresholdId_) / 1.6f + 4.0f) * 50000.0f);
#else
      thresholdId_ = 0;
      thresholdValue_ = 0;
#endif
   }

   unsigned keyToId(BinaryDataRef) const;
   unsigned getHeightForId(unsigned) const;
   BinaryData serialize(void) const;

   static std::unique_ptr<ShardFilter> deserialize(BinaryDataRef);
};

////////
struct ShardFilter_Spentness : public ShardFilter
{
   const unsigned step_;
   unsigned thresholdId_;
   unsigned thresholdValue_;

   ShardFilter_Spentness(unsigned step) :
      step_(step)
   {      
#ifndef UNIT_TESTS
      //x < -exp(step / 50k) / (1 - exp(step / 50k))
      auto eVal = expf(step / 50000.0f);
      thresholdId_ = unsigned(-eVal / (1.0f - eVal));

      //height = (ln(id) + 4) * 50k
      thresholdValue_ = unsigned((logf(thresholdId_) + 4.0f) * 50000.0f);
#else 
      thresholdId_ = 0;
      thresholdValue_ = 0;
#endif
   }

   unsigned keyToId(BinaryDataRef) const;
   unsigned getHeightForId(unsigned) const;
   BinaryData serialize(void) const;

   static std::unique_ptr<ShardFilter> deserialize(BinaryDataRef);
};

////////////////////////////////////////////////////////////////////////////////
class LMDBBlockDatabase
{
   friend class ShardedSshParser;
   friend class BlockchainScanner_Super;

private:
   std::shared_ptr<DatabaseContainer> getDbPtr(DB_SELECT db) const
   {
      auto iter = dbMap_.find(db);
      if (iter == dbMap_.end())
         throw LMDBException("unexpected DB_SELECT");

      return iter->second;
   }
   
public:
   LMDBBlockDatabase(std::shared_ptr<Blockchain>, const std::filesystem::path&);
   ~LMDBBlockDatabase(void);

   /////////////////////////////////////////////////////////////////////////////
   void openDatabases(const std::filesystem::path &basedir);

   /////////////////////////////////////////////////////////////////////////////
   void closeDatabases();
   void replaceDatabases(DB_SELECT, const std::string&);
   void cycleDatabase(DB_SELECT);

   /////////////////////////////////////////////////////////////////////////////
   std::unique_ptr<DbTransaction> beginTransaction(DB_SELECT db, LMDB::Mode mode) const
   {
      auto dbObj = getDbPtr(db);
      return dbObj->beginTransaction(mode);
   }

   ARMORY_DB_TYPE getDbType(void) const
   { 
      return Armory::Config::DBSettings::getDbType(); 
   }

   /////////////////////////////////////////////////////////////////////////////
   // Sometimes, we just need to nuke everything and start over
   void destroyAndResetDatabases(void);
   void resetHistoryDatabases(void);

   /////////////////////////////////////////////////////////////////////////////
   bool databasesAreOpen(void) { return dbIsOpen_; }

   /////////////////////////////////////////////////////////////////////////////
   // Get latest block info
   BinaryData getTopBlockHash() const;


   /////////////////////////////////////////////////////////////////////////////
   std::unique_ptr<LDBIter> getIterator(DB_SELECT db) const
   {
      auto dbObj = getDbPtr(db);
      return dbObj->getIterator();
   }

   /////////////////////////////////////////////////////////////////////////////
   // Get value using BinaryData object.  If you have a string, you can use
   // BinaryData key(string(theStr));
   BinaryDataRef getValueNoCopy(DB_SELECT db, BinaryDataRef keyWithPrefix) const;

   /////////////////////////////////////////////////////////////////////////////
   // Get value using BinaryDataRef object.  The data from the get* call is 
   // actually stored in a member variable, and thus the refs are valid only 
   // until the next get* call.
   BinaryDataRef getValueRef(DB_SELECT db, DB_PREFIX prefix, BinaryDataRef key) const;

   /////////////////////////////////////////////////////////////////////////////
   // Same as the getValueRef, in that they are only valid until the next get*
   // call.  These are convenience methods which basically just save us 
   BinaryRefReader getValueReader(DB_SELECT db, BinaryDataRef keyWithPrefix) const;
   BinaryRefReader getValueReader(DB_SELECT db, DB_PREFIX prefix, BinaryDataRef key) const;

   BinaryData getDBKeyForHash(const BinaryData& txhash,
      uint8_t dupId = UINT8_MAX) const;
   BinaryData getHashForDBKey(BinaryData dbkey) const;
   BinaryData getHashForDBKey(uint32_t hgt,
      uint8_t  dup,
      uint16_t txi = UINT16_MAX,
      uint16_t txo = UINT16_MAX) const;

   unsigned getHeightForTxHash(const BinaryDataRef& hash) const;

   /////////////////////////////////////////////////////////////////////////////
   // Put value based on BinaryDataRefs key and value
   void putValue(DB_SELECT db, BinaryDataRef key, BinaryDataRef value);
   void putValue(DB_SELECT db, BinaryData const & key, BinaryData const & value);
   void putValue(DB_SELECT db, DB_PREFIX pref, BinaryDataRef key, BinaryDataRef value);

   /////////////////////////////////////////////////////////////////////////////
   // Put value based on BinaryData key.  If batch writing, pass in the batch
   void deleteValue(DB_SELECT db, BinaryDataRef key);
   void deleteValue(DB_SELECT db, DB_PREFIX pref, BinaryDataRef key);

   /////////////////////////////////////////////////////////////////////////////
   void readAllHeaders(
      const std::function<void(std::shared_ptr<BlockHeader>, uint32_t, uint8_t)> &callback
      );

   std::map<uint32_t, uint32_t> getSSHSummary(BinaryDataRef scrAddrStr);

   uint32_t getStxoCountForTx(const BinaryData & dbKey6) const;
   void resetHistoryForAddressVector(const std::vector<BinaryData>&);

public:

   uint8_t getValidDupIDForHeight(uint32_t blockHgt) const;
   uint8_t getValidDupIDForHeight_fromDB(uint32_t blockHgt);
   void setValidDupIDForHeight(
      uint32_t blockHgt, uint8_t dup, bool overwrite = true);
   void setValidDupIDForHeight(std::map<unsigned, uint8_t>&);
   
   bool isBlockIDOnMainBranch(unsigned) const;
   void setBlockIDBranch(std::map<unsigned, bool>&);

   /////////////////////////////////////////////////////////////////////////////
   // Interface to translate Stored* objects to/from persistent DB storage
   /////////////////////////////////////////////////////////////////////////////
   StoredDBInfo getStoredDBInfo(DB_SELECT db, uint32_t id);
   void putStoredDBInfo(DB_SELECT db, StoredDBInfo const & sdbi, uint32_t id);

   /////////////////////////////////////////////////////////////////////////////
   // BareHeaders are those int the HEADERS DB with no blockdta associated
   uint8_t putBareHeader(StoredHeader & sbh, bool updateDupID = true,
      bool updateSDBI = true);
   bool    getBareHeader(StoredHeader & sbh, uint32_t blkHgt, uint8_t dup) const;
   bool    getBareHeader(StoredHeader & sbh, uint32_t blkHgt) const;
   bool    getBareHeader(StoredHeader & sbh, BinaryDataRef headHash) const;

   /////////////////////////////////////////////////////////////////////////////
   // still using the old name even though no block data is stored anymore
   BinaryData getRawBlock(uint32_t height, uint8_t dupId) const;
   BinaryData getRawBlock(std::shared_ptr<BlockHeader>) const;
   bool getStoredHeader(StoredHeader&, uint32_t, uint8_t, bool withTx = true) const;
   bool getStoredHeader(StoredHeader&, std::shared_ptr<BlockHeader>, bool withTx = true) const;

   /////////////////////////////////////////////////////////////////////////////
   // StoredTx Accessors
   void updateStoredTx(StoredTx & st);

   void putStoredTx(StoredTx & st, bool withTxOut = true);
   void putStoredZC(StoredTx & stx, const BinaryData& zcKey);

   bool getStoredZcTx(StoredTx & stx,
      BinaryDataRef dbKey) const;

   bool getStoredTx(StoredTx & stx,
      BinaryData& txHashOrDBKey) const;

   bool getStoredTx_byDBKey(StoredTx & stx,
      BinaryDataRef dbKey) const;

   bool getStoredTx_byHash(const BinaryData& txHash,
      StoredTx* stx = nullptr) const;

   bool getStoredTx(StoredTx & st,
      uint32_t blkHgt,
      uint16_t txIndex,
      bool withTxOut = true) const;

   bool getStoredTx(StoredTx & st,
      uint32_t blkHgt,
      uint8_t  dupID,
      uint16_t txIndex,
      bool withTxOut = true) const;


   /////////////////////////////////////////////////////////////////////////////
   // StoredTxOut Accessors
   void putStoredTxOut(StoredTxOut const & sto);
   void putStoredZcTxOut(StoredTxOut const & stxo, const BinaryData& zcKey);

   bool getStoredTxOut(StoredTxOut & stxo,
      uint32_t blockHeight,
      uint8_t  dupID,
      uint16_t txIndex,
      uint16_t txOutIndex) const;

   bool getStoredTxOut(StoredTxOut & stxo,
      uint32_t blockHeight,
      uint16_t txIndex,
      uint16_t txOutIndex) const;

   bool getStoredTxOut(StoredTxOut & stxo,
      const BinaryData& DBkey) const;

   bool getStoredTxOut(
      StoredTxOut & stxo, const BinaryData& txHash, uint16_t txoutid) const;

   void getSpentness(StoredTxOut& stxo);

   void getUTXOflags(std::map<BinaryData, StoredSubHistory>&) const;
   void getUTXOflags(StoredSubHistory&) const;
   void getUTXOflags_Super(StoredSubHistory&) const;

   /////////////////////////////////////////////////////////////////////////////
   // StoredScriptHistory Accessors
   void putStoredScriptHistorySummary(StoredScriptHistory & ssh);

   bool getStoredScriptHistory(StoredScriptHistory & ssh,
      BinaryDataRef scrAddrStr,
      uint32_t startBlock = 0,
      uint32_t endBlock = UINT32_MAX) const;

   bool getStoredSubHistoryAtHgtX(StoredSubHistory& subssh,
      const BinaryDataRef scrAddrStr, const BinaryData& hgtX) const;
   
   bool getStoredSubHistoryAtHgtX(StoredSubHistory& subssh,
      const BinaryData& dbkey) const;

   bool getStoredScriptHistorySummary(StoredScriptHistory & ssh,
      BinaryDataRef scrAddrStr) const;

   void getStoredScriptHistoryByRawScript(
      StoredScriptHistory & ssh,
      BinaryDataRef rawScript) const;
   
   bool fillStoredSubHistory(StoredScriptHistory&, unsigned, unsigned) const;
   bool fillStoredSubHistory_Super(StoredScriptHistory&, unsigned, unsigned) const;

   // This method breaks from the convention I've used for getting/putting 
   // stored objects, because we never really handle Sub-ssh objects directly,
   // but we do need to harness them.  This method could be renamed to
   // "getPartialScriptHistory()" ... it reads the main 
   // sub-ssh from DB and adds it to the supplied regular-ssh.
   bool fetchStoredSubHistory(StoredScriptHistory & ssh,
      BinaryData hgtX,
      bool createIfDNE = false,
      bool forceReadAndMerge = false);

   // This could go in StoredBlockObj if it didn't need to lookup DB data
   bool     getFullUTXOMapForSSH(StoredScriptHistory & ssh,
      std::map<BinaryData, UnspentTxOut> & mapToFill,
      bool withMultisig = false);

   uint64_t getBalanceForScrAddr(BinaryDataRef scrAddr, bool withMulti = false);

   bool putStoredTxHints(StoredTxHints const & sths);
   bool getStoredTxHints(StoredTxHints & sths, BinaryDataRef hashPrefix) const;
   void updatePreferredTxHint(BinaryDataRef hashOrPrefix, BinaryData preferKey);

   bool putStoredHeadHgtList(StoredHeadHgtList const & hhl);
   bool getStoredHeadHgtList(StoredHeadHgtList & hhl, uint32_t height) const;

   // TxRefs are much simpler with LDB than the previous FileDataPtr construct
   TxRef getTxRef(BinaryDataRef txHash);
   TxRef getTxRef(BinaryData hgtx, uint16_t txIndex);
   TxRef getTxRef(uint32_t hgt, uint8_t dup, uint16_t txIndex);


   // Sometimes we already know where the Tx is, but we don't know its hash
   Tx    getFullTxCopy(BinaryData ldbKey6B) const;
   Tx    getFullTxCopy(uint32_t hgt, uint16_t txIndex) const;
   Tx    getFullTxCopy(uint32_t hgt, uint8_t dup, uint16_t txIndex) const;
   Tx    getFullTxCopy(uint16_t txIndex, std::shared_ptr<BlockHeader> bhPtr) const;
   TxOut getTxOutCopy(BinaryData ldbKey6B, uint16_t txOutIdx) const;
   TxIn  getTxInCopy(BinaryData ldbKey6B, uint16_t txInIdx) const;


   // Sometimes we already know where the Tx is, but we don't know its hash
   BinaryData getTxHashForLdbKey(BinaryDataRef ldbKey6B) const;

   ////////////////////////////////////////////////////////////////////////////
   bool markBlockHeaderValid(BinaryDataRef headHash);
   bool markBlockHeaderValid(uint32_t height, uint8_t dup);

   KVLIST getAllDatabaseEntries(DB_SELECT db);
   void   printAllDatabaseEntries(DB_SELECT db);

   ARMORY_DB_TYPE armoryDbType(void) const
   { return Armory::Config::DBSettings::getDbType(); }

   const std::filesystem::path& baseDir(void) const { return DatabaseContainer::baseDir_; }
   void setBlkFolder(const std::filesystem::path& path) { blkFolder_ = path; }

   void closeDB(DB_SELECT db);
   StoredDBInfo openDB(DB_SELECT);
   void resetSSHdb(void);
   void resetSSHdb_Super(void);

   const std::shared_ptr<Blockchain> blockchain(void) const { return blockchainPtr_; }

   ////
   TxFilterPoolWriter getFilterPoolWriter(uint32_t) const;
   BinaryDataRef getFilterPoolDataRef(uint32_t) const;
   void putFilterPoolForFileNum(uint32_t, const TxFilterPoolWriter& pool);

   void putMissingHashes(const std::set<BinaryData>&, uint32_t);
   std::set<BinaryData> getMissingHashes(uint32_t) const;

   ////
   void updateHeightToIdMap(std::map<unsigned, unsigned>& idmap)
   {
      heightToBatchId_.update(move(idmap));
   }

   void loadHeightToIdMap();
   unsigned getShardIdForHeight(unsigned) const;
   unsigned getNextShardIdForHeight(unsigned) const;

public:
   std::map<DB_SELECT, std::shared_ptr<DatabaseContainer>> dbMap_;
   const static std::map<std::string, size_t> mapSizes_;

private:
   bool                 dbIsOpen_;
   uint32_t             ldbBlockSize_;

   uint32_t             lowestScannedUpTo_;

   Armory::Threading::TransactionalMap<unsigned, uint8_t> validDupByHeight_;
   Armory::Threading::TransactionalMap<unsigned, bool> blockIDMainChainMap_;

   // In this case, a address is any TxOut script, which is usually
   // just a 25-byte script.  But this generically captures all types
   // of addresses including pubkey-only, P2SH, 
   std::map<BinaryData, StoredScriptHistory> registeredSSHs_;
   const std::shared_ptr<Blockchain> blockchainPtr_;
   std::filesystem::path blkFolder_;
   const static std::set<DB_SELECT> supernodeDBs_;

   Armory::Threading::TransactionalMap<unsigned, unsigned> heightToBatchId_;
};

#endif
// kate: indent-width 3; replace-tabs on;
