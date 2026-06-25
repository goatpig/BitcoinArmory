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

#include <map>
#include <vector>
#include <set>
#include <cmath>
#include <cstring>

#include "lmdb_wrapper.h"
#include <Utils/BtcUtils.h>
#include <Utils/varint.h>
#include <Utils/DBUtils.h>
#include <Utils/FileUtils.h>
#include <Utils/ArmoryConfig.h>
#include <Utils/ArmoryErrors.h>
#include <Utils/BCTX.h>
#include <Utils/UniversalTimer.h>
#include <TxClasses.h>

#include "BlockObj.h"
#include "StoredBlockObj.h"
#include "txio.h"
#include "BlockDataMap.h"
#include "Blockchain.h"
#include "TxHashFilters.h"

using namespace Armory;

extern const std::vector<DB_SELECT> SUPERNODEDBS{
   DB_SELECT::HEADERS, DB_SELECT::SCRADDR,
   DB_SELECT::ZERO_CONF
};
extern const std::vector<DB_SELECT> SUPERNODEHASHTABLES{
   DB_SELECT::TXOUTS, DB_SELECT::TXINS,
   DB_SELECT::TXHINTS
};

extern const std::vector<DB_SELECT> FULLNODEDBS{
   DB_SELECT::HEADERS, DB_SELECT::SCRADDR,
   DB_SELECT::TXOUTS, DB_SELECT::TXINS,
   DB_SELECT::KNOWNHASHES, DB_SELECT::ZERO_CONF
};
extern const std::vector<DB_SELECT> FULLNODEHASHTABLES{
   DB_SELECT::TXHINTS
};

extern const std::vector<DB_SELECT> BARENODEDBS{
   DB_SELECT::HEADERS, DB_SELECT::SCRADDR,
   DB_SELECT::TXOUTS, DB_SELECT::TXINS,
   DB_SELECT::KNOWNHASHES, DB_SELECT::TXHINTS,
   DB_SELECT::ZERO_CONF
};

extern const std::map<DB_SELECT, size_t> MAPSIZES{
   { DB_SELECT::HEADERS,         10 * 1024 * 1024 * 1024ULL },
   { DB_SELECT::TXHINTS,          5 * 1024 * 1024 * 1024ULL },
   { DB_SELECT::ZERO_CONF,       10 * 1024 * 1024 * 1024ULL },
   { DB_SELECT::TXFILTERS,      100 * 1024 * 1024 * 1024ULL },
   { DB_SELECT::SCRADDR,        500 * 1024 * 1024 * 1024ULL },
   { DB_SELECT::TXOUTS,        2000 * 1024 * 1024 * 1024ULL },
   { DB_SELECT::TXINS,         2000 * 1024 * 1024 * 1024ULL },
   { DB_SELECT::KNOWNHASHES,      1 * 1024 * 1024 * 1024ULL }
};

////////////////////////////////////////////////////////////////////////////////
// exceptions
FilterException::FilterException(const std::string& err) :
   std::runtime_error(err)
{}

LmdbWrapperException::LmdbWrapperException(const std::string& err) :
   std::runtime_error(err)
{}

////////////////////////////////////////////////////////////////////////////////
// DBTransaction
DBTransaction::DBTransaction(LMDB::Transaction&& dbtx) :
   dbtx_(std::move(dbtx))
{}

DBTransaction::~DBTransaction()
{}

void DBTransaction::insert(const LMDB::DataRef& key, const LMDB::DataRef& val)
{
   dbtx_.insert(key, val);
}

void DBTransaction::erase(const LMDB::DataRef& key)
{
   dbtx_.erase(key);
}

LMDB::DataRef DBTransaction::get(const LMDB::DataRef& key) const
{
   return dbtx_.get(key);
}

DBIterator DBTransaction::getIterator() const
{
   return DBIterator{LMDB::Iterator{&dbtx_}};
}

////////////////////////////////////////////////////////////////////////////////
// DBIterator
DBIterator::DBIterator(LMDB::Iterator&& iter) :
   iter_(std::move(iter)), isDirty_(true)
{}

DBIterator::~DBIterator()
{}

void DBIterator::resetReaders()
{
   currKeyReader_.resetPosition();
   currValueReader_.resetPosition();
}

////////
bool DBIterator::isNull() const
{
   return !iter_.isValid();
}

bool DBIterator::isValid() const
{
   return iter_.isValid();
}

bool DBIterator::isValid(DbPrefix dbpref)
{
   if (!isValid()) {
      return false;
   }
   readIterData();
   if (currKey_.empty()) {
      return false;
   }
   return currKey_.getPtr()[0] == (uint8_t)dbpref;
}

////////
bool DBIterator::advance(DbPrefix prefix)
{
   advance();
   return isValid(prefix);
}

bool DBIterator::advanceAndRead()
{
   if (!advance()) {
      return false;
   }
   return readIterData();
}

bool DBIterator::advanceAndRead(DbPrefix prefix)
{
   if (!advance(prefix)) {
      return false;
   }
   return readIterData();
}

////////
BinaryData DBIterator::getKey() const
{
   if(isDirty_) {
      LOGERR << "Returning dirty key ref";
      return {};
   }
   return currKey_;
}

BinaryData DBIterator::getValue() const
{
   if(isDirty_) {
      LOGERR << "Returning dirty value ref";
      return {};
   }
   return currValue_;
}

BinaryDataRef DBIterator::getKeyRef() const
{
   if(isDirty_) {
      LOGERR << "Returning dirty key ref";
      return {};
   }
   return currKeyReader_.getRawRef();
}

BinaryDataRef DBIterator::getValueRef() const
{
   if(isDirty_) {
      LOGERR << "Returning dirty value ref";
      return {};
   }
   return currValueReader_.getRawRef();
}

BinaryRefReader& DBIterator::getKeyReader() const
{
   if(isDirty_) {
      LOGERR << "Returning dirty key reader";
   }
   return currKeyReader_;
}

BinaryRefReader& DBIterator::getValueReader() const
{
   if(isDirty_)
      LOGERR << "Returning dirty value reader";
   return currValueReader_;
}

////////
bool DBIterator::seekTo(DbPrefix pref, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)pref);
   bw.put_BinaryData(key);
   return seekTo(bw.getDataRef());
}

bool DBIterator::seekToExact(DbPrefix pref, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)pref);
   bw.put_BinaryData(key);
   return seekToExact(bw.getDataRef());
}

bool DBIterator::seekToStartsWith(BinaryDataRef key)
{
   if (!seekTo(key)) {
      return false;
   }
   return checkKeyStartsWith(key);
}

bool DBIterator::seekToStartsWith(DbPrefix prefix)
{
   BinaryWriter bw(1);
   bw.put_uint8_t((uint8_t)prefix);
   if (!seekTo(bw.getDataRef())) {
      return false;
   }
   return checkKeyStartsWith(bw.getDataRef());
}

bool DBIterator::seekToStartsWith(DbPrefix pref, BinaryDataRef key)
{
   if (!seekTo(pref, key)) {
      return false;
   }
   return checkKeyStartsWith(pref, key);
}

bool DBIterator::seekToBefore(DbPrefix prefix)
{
   BinaryWriter bw(1);
   bw.put_uint8_t((uint8_t)prefix);
   return seekToBefore(bw.getDataRef());
}

bool DBIterator::seekToBefore(DbPrefix pref, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)pref);
   bw.put_BinaryData(key);
   return seekToBefore(bw.getDataRef());
}

////////
bool DBIterator::checkKeyExact(BinaryDataRef key)
{
   if (isDirty_ && !readIterData()) {
      return false;
   }
   return (key==currKeyReader_.getRawRef());
}

bool DBIterator::checkKeyExact(DbPrefix prefix, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key);
   if (isDirty_ && !readIterData()) {
      return false;
   }
   return bw.getDataRef() == currKeyReader_.getRawRef();
}

bool DBIterator::checkKeyStartsWith(BinaryDataRef key)
{
   if (isDirty_ && !readIterData()) {
      return false;
   }
   return currKeyReader_.getRawRef().startsWith(key);
}

bool DBIterator::verifyPrefix(DbPrefix prefix, bool advanceReader)
{
   if (isDirty_ && !readIterData()) {
      return false;
   }
   if (currKeyReader_.getSizeRemaining() < 1) {
      return false;
   }
   if (advanceReader) {
      return currKeyReader_.get_uint8_t() == (uint8_t)prefix;
   } else {
      return currKeyReader_.getRawRef()[0] == (uint8_t)prefix;
   }
}

bool DBIterator::checkKeyStartsWith(DbPrefix prefix, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key);
   return checkKeyStartsWith(bw.getDataRef());
}

////////
bool DBIterator::seekTo(BinaryDataRef key)
{
   iter_.seek(
      LMDB::DataRef{key.getSize(), key.getPtr()},
      LMDB::Iterator::SeekBy::GE);
   return readIterData();
}

bool DBIterator::seekToExact(BinaryDataRef key)
{
   if (!seekTo(key)) {
      return false;
   }
   return checkKeyExact(key);
}

bool DBIterator::seekToBefore(BinaryDataRef key)
{
   iter_.seek(
      LMDB::DataRef{key.getSize(), key.getPtr()},
      LMDB::Iterator::SeekBy::LE);
   return readIterData();
}

////////
bool DBIterator::advance()
{
   ++iter_;
   isDirty_ = true;
   return isValid();
}

bool DBIterator::retreat()
{
   --iter_;
   isDirty_ = true;
   return isValid();
}

////////
bool DBIterator::readIterData()
{
   if (!isValid()){
      isDirty_ = true;
      return false;
   }

   currKey_ = BinaryDataRef{
      (uint8_t*)iter_.key().mv_data,
      iter_.key().mv_size};
   currValue_ = BinaryDataRef{
      (uint8_t*)iter_.value().mv_data,
      iter_.value().mv_size};

   currKeyReader_.setNewData(currKey_);
   currValueReader_.setNewData(currValue_);
   isDirty_ = false;
   return true;
}

////////
bool DBIterator::seekToFirst()
{
   iter_.toFirst();
   return readIterData();
}

bool DBIterator::seekToLast()
{
   iter_.toLast();
   return readIterData();
}

////////////////////////////////////////////////////////////////////////////////
// LMDBBlockDatabase
LMDBBlockDatabase::LMDBBlockDatabase(const std::filesystem::path& dbdir) :
   dbDir_{dbdir}
{}

LMDBBlockDatabase::~LMDBBlockDatabase()
{
   closeDatabases();
}

////////
std::shared_ptr<DatabaseContainer> LMDBBlockDatabase::getDbPtr(
   DB_SELECT db) const
{
   auto iter = dbMap_.find(db);
   if (iter == dbMap_.end()) {
      throw LmdbWrapperException("unexpected db");
   }
   return iter->second;
}

std::shared_ptr<DatabaseContainer> LMDBBlockDatabase::getHashTablePtr(
   DB_SELECT db, uint8_t id) const
{
   auto iter = dbHashTables_.find(db);
   if (iter == dbHashTables_.end()) {
      throw LmdbWrapperException("unexpected hash table");
   }
   return iter->second[id];
}

////////
std::unique_ptr<DBTransaction> LMDBBlockDatabase::beginTransaction(
   DB_SELECT db, LMDB::Mode mode) const
{
   auto dbObj = getDbPtr(db);
   return dbObj->beginTransaction(mode);
}

std::unique_ptr<DBTransaction> LMDBBlockDatabase::beginHashTableTx(
   DB_SELECT db, uint8_t id, LMDB::Mode mode) const
{
   auto dbObj = getHashTablePtr(db, id);
   return dbObj->beginTransaction(mode);
}

/////////////////////////////////////////////////////////////////////////////
// The dbType and pruneType inputs are left blank if you are just going to
// take whatever is the current state of database.  You can choose to
// manually specify them, if you want to throw an error if it's not what you
// were expecting
void LMDBBlockDatabase::openDatabases()
{
   LOGINFO << "Opening databases...";
   LOGINFO << "dbmode: " << Config::DBSettings::getDbModeStr();

   if (!Config::BitcoinSettings::isInitialized()) {
      LOGERR << " must set magic bytes and genesis block";
      LOGERR << "           before opening databases.";
      throw LmdbWrapperException("magic bytes not set");
   }

   // Just in case this isn't the first time we tried to open it.
   closeDatabases();

   dbMap_.clear();
   std::vector<DB_SELECT> dbSet;
   std::vector<DB_SELECT> hashTables;
   switch (Config::DBSettings::getDbType())
   {
      case ARMORY_DB_TYPE::Bare:
         dbSet = BARENODEDBS;
         break;

      case ARMORY_DB_TYPE::Full:
         dbSet = FULLNODEDBS;
         hashTables = FULLNODEHASHTABLES;
         break;

      case ARMORY_DB_TYPE::Super:
         dbSet = SUPERNODEDBS;
         hashTables = SUPERNODEHASHTABLES;
         break;
   }

   //regular dbs
   for (const auto& currDb : dbSet) {
      const auto& dbName = DatabaseContainer::getDbName(currDb);
      dbMap_.emplace(currDb,
         std::make_shared<DatabaseContainer>(
            dbDir_, dbName, MAPSIZES.at(currDb)));
      openDB(currDb);
   }

   //hashtables
   for (const auto& currDb : hashTables) {
      auto size = MAPSIZES.at(currDb);
      const auto& tableName = DatabaseContainer::getDbName(currDb);
      std::filesystem::path hashDir = dbDir_ / tableName;
      if (!FileUtils::pathExists(hashDir, 0)) {
         std::filesystem::create_directory(hashDir);
      }

      auto emplaceResult = dbHashTables_.emplace(currDb,
         std::vector<std::shared_ptr<DatabaseContainer>>{256});
      for (uint32_t index = 0; index < 256; index++) {
         auto name = std::format("{}_{:x}", tableName, index);
         auto db = std::make_shared<DatabaseContainer>(
            hashDir, name, size);
         db->open();
         emplaceResult.first->second[index] = db;
      }
   }

   //check/seed headers SDBI
   try {
      auto sdbi = getStoredDBInfo(DB_SELECT::HEADERS, 0xFFFF);
      if (Config::BitcoinSettings::getMagicBytes() != sdbi.magicBytes) {
         LOGERR << "magic bytes mismatch, aborting";
         exit(-2);
      } else if (Config::DBSettings::getDbType() != sdbi.armoryType) {
         LOGERR << "db type mismatch, aborting";
         exit(-3);
      }
   } catch (const LmdbWrapperException&) {
      //fresh db, seed headers sdbi
      StoredDBInfo sdbi;
      sdbi.armoryType = Config::DBSettings::getDbType();
      sdbi.magicBytes = Config::BitcoinSettings::getMagicBytes();
      auto tx = beginTransaction(DB_SELECT::HEADERS, LMDB::Mode::ReadWrite);
      putStoredDBInfo(DB_SELECT::HEADERS, sdbi, 0xFFFF);
   }
   dbIsOpen_ = true;
}

bool LMDBBlockDatabase::databasesAreOpen() const
{
   return dbIsOpen_;
}

////////
void LMDBBlockDatabase::closeDatabases()
{
   for (auto& dbPair : dbMap_) {
      dbPair.second->close();
   }
   dbMap_.clear();
   for (auto& table : dbHashTables_) {
      for (auto& db : table.second) {
         db->close();
      }
   }
   dbHashTables_.clear();
   dbIsOpen_ = false;
}

void LMDBBlockDatabase::cycleDatabase(DB_SELECT db)
{
   auto dbPtr = getDbPtr(db);
   dbPtr->close();
   dbPtr->open();
}

void LMDBBlockDatabase::resetHistoryDatabases()
{
   if (Config::DBSettings::getDbType() != ARMORY_DB_TYPE::Super) {
      auto dbTxouts = getDbPtr(DB_SELECT::TXOUTS);
      auto dbTxins = getDbPtr(DB_SELECT::TXINS);
      auto dbHints = getDbPtr(DB_SELECT::TXHINTS);
      auto dbHashes = getDbPtr(DB_SELECT::KNOWNHASHES);
      closeDatabases();

      dbTxouts->eraseOnDisk();
      dbTxins->eraseOnDisk();
      dbHints->eraseOnDisk();
      dbHashes->eraseOnDisk();
   }
   openDatabases();
}

void LMDBBlockDatabase::destroyAndResetDatabases()
{
   // We want to make sure the database is restarted with the same parameters
   // it was called with originally
   {
      //save dbMap because closeDatabases clears it
      auto dbMap = dbMap_;
      auto hashTables = dbHashTables_;
      closeDatabases();
      for (auto& dbPair : dbMap) {
         dbPair.second->eraseOnDisk();
      }
      for (auto& table : hashTables) {
         for (auto& db : table.second) {
            db->eraseOnDisk();
         }
      }
   }

   // Reopen the databases with the exact same parameters as before
   // The close & destroy operations shouldn't have changed any of that.
   openDatabases();
}

/////////////////////////////////////////////////////////////////////////////
Types::TxKey LMDBBlockDatabase::getDBKeyForHash(
   const Types::TxHash& txHash) const
{
   if (txHash.getSize() != 32) {
      LOGWARN << "invalid tx hash size:" << txHash.getSize();
      return {};
   }

   auto dbType = Config::DBSettings::getDbType();
   if (dbType != ARMORY_DB_TYPE::Super) {
      //we track known hashes in full and barenode, check that first
      auto tx = beginTransaction(DB_SELECT::KNOWNHASHES, LMDB::Mode::ReadOnly);
      auto val = tx->get(LMDB::DataRef{txHash.getSize(), txHash.getCharPtr()});
      if (val.len == sizeof(Types::TxKey)) {
         Types::TxKey txKey;
         std::memcpy(&txKey, val.data, val.len);
         return txKey;
      }

      //no known hash, in barenode we're done
      if (dbType == ARMORY_DB_TYPE::Bare) {
         return Types::INVALID_TX_KEY;
      }
   }

   //time to check tx hashes
   auto hashTableIndex = txHash.getPtr()[8];
   auto tx = beginHashTableTx(DB_SELECT::TXHINTS,
      hashTableIndex, LMDB::Mode::ReadOnly);
   auto dbIter = tx->getIterator();
   if (!dbIter.seekToStartsWith(txHash.getSliceRef(0, 4))) {
      //no hint starts with this a priori valid tx hash, this is odd
      //LOGWARN << "no hint for this tx hash: " << txHash.toHexStr();
      return Types::INVALID_TX_KEY;
   }

   //gather all db keys for valid hints
   uint64_t hintKey;
   std::set<Types::TxKey> result;
   do {
      //grab hint key
      auto keyRef = dbIter.getKeyRef();
      if (keyRef.getSize() !=  sizeof(uint64_t)) {
         LOGWARN << "invalid txhint key: " << keyRef.toHexStr();
         continue;
      }

      //check it starts with our hash
      std::memcpy(&hintKey, keyRef.getPtr(), sizeof(uint64_t));
      if (std::memcmp(&hintKey, txHash.getPtr(), 4) != 0) {
         break;
      }

      //extract blockID from key
      Types::BlockId blockID = hintKey >> 32;

      //grab txids from value
      auto valueReader = dbIter.getValueReader();
      while (valueReader.getSizeRemaining() >= 2) {
         result.emplace(Types::constructTxKey(
            blockID, valueReader.get_uint16_t()));
      }
   } while (dbIter.advanceAndRead());

   if (result.empty()) {
      return Types::INVALID_TX_KEY;
   } else if (result.size() == 1) {
      //TODO: migrate to uint64_t txkeys
      return *result.begin();
   } else {
      //NOTE: db wrapper shouldnt have to pick the correct key,
      //caller should deal with it
      throw std::runtime_error("implement me");
   }
}

/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::readAllHeaders(
   const std::function<void(std::shared_ptr<BlockHeader>)>& callback)
{
   auto tx = beginTransaction(DB_SELECT::HEADERS, LMDB::Mode::ReadOnly);
   auto ldbIter = tx->getIterator();
   if (!ldbIter.seekToFirst()) {
      return;
   }

   do {
      ldbIter.resetReaders();
      auto keyRef = ldbIter.getKeyRef();
      if (keyRef.getSize() != 4) {
         LOGERR << "How did we get a header key that is not uint32?"
            << " (" << keyRef.getSize() << ")";
         continue;
      }

      //key is uniqueID in BE
      Types::BlockKey blockKey;
      std::memcpy(&blockKey, keyRef.getPtr(), sizeof(Types::BlockKey));
      if (blockKey == 0xFFFFFFFF) {
         //we've hit the SDBI entry, we are done
         return;
      }
      Types::BlockId uniqueID = Types::getBlockIdFromKey(blockKey);

      //header data
      auto brrVal = ldbIter.getValueReader();
      auto regHead = std::make_shared<BlockHeader>(
         brrVal.get_BinaryDataRef(HEADER_SIZE));

      //metadata
      regHead->setBlockSize(brrVal.get_uint32_t());
      regHead->setNumTx(brrVal.get_uint32_t());
      regHead->setBlockFileOffset(brrVal.get_uint64_t());
      regHead->setBlockFileNum(brrVal.get_uint16_t());
      regHead->setUniqueID(uniqueID);
      regHead->setMerkleValid((bool)brrVal.get_uint8_t());

      callback(regHead);
   } while (ldbIter.advanceAndRead());
}

////////////////////////////////////////////////////////////////////////////////
// Puts bare header into HEADERS DB.  Use "putStoredHeader" to add to both
// (which actually calls this method as the first step)
//
// Returns the duplicateID of the header just inserted
void LMDBBlockDatabase::putBareHeader(const StoredHeader& sbh)
{
   if (!sbh.isInitialized()) {
      LOGERR << "Attempting to put uninitialized bare header into DB";
      return;
   }

   if (sbh.uniqueID == UINT32_MAX) {
      throw LmdbWrapperException("Attempted to put a header with no ID");
   }

   Types::BlockKey blockKey = Types::getBlockKeyFromId(sbh.uniqueID);
   LMDB::DataRef keyRef{4, (const char*)&blockKey};

   BinaryWriter bwData;
   sbh.serializeDBValue(bwData);
   LMDB::DataRef valRef{bwData.getSize(), bwData.getDataRef().getPtr()};

   auto tx = beginTransaction(DB_SELECT::HEADERS, LMDB::Mode::ReadWrite);
   tx->insert(keyRef, valRef);
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putStoredZC(StoredTx& stx, const Types::TxKey& zcKey)
{
   BinaryWriter bwKey{9};
   bwKey.put_uint8_t((uint8_t)DbPrefix::ZCDATA);
   bwKey.put_uint64_t(zcKey);
   LMDB::DataRef keyRef{bwKey.getSize(), bwKey.getDataRef().getPtr()};

   // Now add the base Tx entry in the BLKDATA DB.
   BinaryWriter bwVal;
   stx.serializeDBValue(bwVal, Config::DBSettings::getDbType());
   bwVal.put_uint32_t(stx.unixTime);
   LMDB::DataRef valRef{bwVal.getSize(), bwVal.getDataRef().getPtr()};

   auto tx = beginTransaction(DB_SELECT::ZERO_CONF, LMDB::Mode::ReadWrite);
   tx->insert(keyRef, valRef);

   // Make sure all the parameters of the TxOut are set right
   for (auto& stxoPair : stx.stxoMap) {
      stxoPair.second.txVersion = READ_UINT32_LE(stx.dataCopy.getPtr());
      stxoPair.second.txIndex = stx.txIndex;
      stxoPair.second.txOutIndex = stxoPair.first;
      auto zcStxoKey = Types::constructTxIOKeyFromTxKey(
         zcKey, stxoPair.second.txOutIndex);
      putStoredZcTxOut(stxoPair.second, zcStxoKey);
   }
}

////////
bool LMDBBlockDatabase::getStoredHeader(
   StoredHeader& sbh, std::shared_ptr<BlockHeader> bh, bool withTx) const
{
   try {
      //open block file
      auto path = FileUtils::getBlkFilename(
         Config::Pathing::blkFilePath(), bh->getBlockFileNum());
      auto fileMap = FileUtils::FileMap(path, false);
      BinaryRefReader brr(fileMap.ptr() + bh->getOffset(), bh->getBlockSize());

      if (withTx) {
         sbh.unserializeFullBlock(brr, false, false);
      } else {
         sbh.unserializeSimple(brr);
      }
   } catch (...) {
      return false;
   }
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredZC(StoredTx& stx, const Types::TxKey& zcKey) const
{
   //only by zcKey
   BinaryData zcDbKey;
   zcDbKey.resize(7);
   uint8_t* ptr = zcDbKey.getPtr();
   ptr[0] = (uint8_t)DbPrefix::ZCDATA;
   memcpy(ptr + 1, &zcKey, 6);

   auto tx = beginTransaction(DB_SELECT::ZERO_CONF, LMDB::Mode::ReadOnly);
   auto ldbIter = tx->getIterator();
   if (!ldbIter.seekToStartsWith(zcDbKey)) {
      return false;
   }

   size_t nbytes = 0;
   do {
      auto keyRef = ldbIter.getKeyRef();
      if (keyRef.getSize() != 9) {
         LOGERR << "Unexpected ZERO_CONF entry while iterating";
         return false;
      }
      Types::TxKey txKey;
      std::memcpy(&txKey, ldbIter.getKeyRef().getPtr() + 1, sizeof(Types::TxKey));

      // Stop if txio keys mismatch zc key
      if (Types::getTxKeyFromTxIOKey(txKey) != zcKey) {
         break;
      }

      // Now actually process the iter value
      if (!Types::isThisATxIOKey(txKey)) {
         // Get everything else from the iter value
         stx.unserializeDBValue(ldbIter.getValueRef());
         nbytes += stx.dataCopy.getSize();
      } else {
         auto txOutIdx = Types::getTxIOIndexFromTxIOKey(txKey);
         auto& stxo = stx.stxoMap[txOutIdx];
         stxo.unserializeDBValue(ldbIter.getValueRef());
         stxo.parentHash = stx.thisHash;
         stxo.txVersion  = stx.version;
         stxo.txOutIndex = txOutIdx;
         nbytes += stxo.dataCopy.getSize();
      }
   } while (ldbIter.advanceAndRead(DbPrefix::ZCDATA));

   stx.numBytes = stx.haveAllTxOut() ? nbytes : UINT32_MAX;
   return true;
}

void LMDBBlockDatabase::putStoredZcTxOut(const StoredTxOut& stxo,
   const Types::TxIOKey& zcKey)
{
   BinaryWriter bwKey{sizeof(Types::TxIOKey) + 1};
   bwKey.put_uint8_t((uint8_t)DbPrefix::ZCDATA);
   bwKey.put_uint64_t(zcKey);
   LMDB::DataRef keyRef{bwKey.getSize(), bwKey.getDataRef().getPtr()};

   BinaryWriter bwVal;
   stxo.serializeDBValue(bwVal);
   LMDB::DataRef valRef{bwVal.getSize(), bwVal.getDataRef().getPtr()};

   auto tx = beginTransaction(DB_SELECT::ZERO_CONF, LMDB::Mode::ReadWrite);
   tx->insert(keyRef, valRef);
}

////////////////////////////////////////////////////////////////////////////////
// This is used only for debugging and testing with small database sizes.
// For intance, the reorg unit test only has a couple blocks, a couple 
// addresses and a dozen transactions.  We can easily predict and construct
// the output of this function or analyze the output by eye.
KVLIST LMDBBlockDatabase::getAllDatabaseEntries(DB_SELECT db)
{
   if (!databasesAreOpen()) {
      return KVLIST();
   }
   KVLIST outList;
   outList.reserve(100);

   auto tx = beginTransaction(db, LMDB::Mode::ReadOnly);
   auto ldbIter = tx->getIterator();
   ldbIter.seekToFirst();
   for (ldbIter.seekToFirst(); ldbIter.isValid(); ldbIter.advanceAndRead()) {
      size_t last = outList.size();
      outList.push_back(std::pair<BinaryData, BinaryData>());
      outList[last].first  = ldbIter.getKey();
      outList[last].second = ldbIter.getValue();
   }
   return outList;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::printAllDatabaseEntries(DB_SELECT db)
{
   std::cout << "Printing DB entries... (DB=" << (int)db << ")" << std::endl;
   KVLIST dbList = getAllDatabaseEntries(db);
   if (dbList.empty()) {
      std::cout << "   <no entries in db>" << std::endl;
      return;
   }

   for (uint32_t i = 0; i < dbList.size(); i++) {
      std::cout << "   \"" << dbList[i].first.toHexStr() << "\"  ";
      std::cout << "   \"" << dbList[i].second.toHexStr() << "\"  " << std::endl;
   }
}

/////////////////////////////////////////////////////////////////////////////
TxFilterPoolWriter LMDBBlockDatabase::getFilterPoolWriter(
   uint32_t fileNum) const
{
   auto key = DBUtils::getFilterPoolKey(fileNum);
   LMDB::DataRef keyRef{key.getSize(), key.getPtr()};
   auto tx = beginTransaction(DB_SELECT::TXFILTERS, LMDB::Mode::ReadOnly);
   auto val = tx->get(keyRef);
   try {
      BinaryDataRef valBdr{(const uint8_t*)val.data, val.len};
      return {valBdr};
   } catch (const TxFilterException&) {
      //return empty pool if data is missing
      return {};
   }
}

/////////////////////////////////////////////////////////////////////////////
BinaryDataRef LMDBBlockDatabase::getFilterPoolDataRef(uint32_t fileNum) const
{
   auto key = DBUtils::getFilterPoolKey(fileNum);
   LMDB::DataRef keyRef{key.getSize(), key.getPtr()};
   auto tx = beginTransaction(DB_SELECT::TXFILTERS, LMDB::Mode::ReadOnly);
   auto val = tx->get(keyRef);
   return {(const uint8_t*)val.data, val.len};
}

/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putFilterPoolForFileNum(
   uint32_t fileNum, const TxFilterPoolWriter& pool)
{
   auto key = DBUtils::getFilterPoolKey(fileNum);
   LMDB::DataRef keyRef{key.getSize(), key.getPtr()};


   BinaryWriter bw;
   pool.serialize(bw);
   LMDB::DataRef valRef{bw.getSize(), bw.getDataRef().getPtr()};

   //update on disk
   auto tx = beginTransaction(DB_SELECT::TXFILTERS, LMDB::Mode::ReadWrite);
   tx->insert(keyRef, valRef);
}

/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putMissingHashes(
   const std::set<BinaryData>& hashSet, uint32_t id)
{
   auto missingHashesKey = DBUtils::getMissingHashesKey(id);
   LMDB::DataRef keyRef{
      missingHashesKey.getSize(), missingHashesKey.getPtr()};

   BinaryWriter bw;
   bw.put_uint32_t(hashSet.size());
   for (const auto& hash : hashSet) {
      bw.put_BinaryData(hash);
   }
   LMDB::DataRef valRef{bw.getSize(), bw.getDataRef().getPtr()};

   auto tx = beginTransaction(DB_SELECT::TXFILTERS, LMDB::Mode::ReadWrite);
   tx->insert(keyRef, valRef);
}

std::set<BinaryData> LMDBBlockDatabase::getMissingHashes(uint32_t id) const
{
   auto missingHashesKey = DBUtils::getMissingHashesKey(id);
   LMDB::DataRef keyRef{
      missingHashesKey.getSize(), missingHashesKey.getPtr()};
   auto tx = beginTransaction(DB_SELECT::TXFILTERS, LMDB::Mode::ReadOnly);
   auto value = tx->get(keyRef);

   BinaryRefReader brr((const uint8_t*)value.data, value.len);
   if (brr.getSizeRemaining() < 4) {
      throw LmdbWrapperException("invalid missing hashes entry");
   }
   std::set<BinaryData> missingHashesSet;

   auto len = brr.get_uint32_t();
   if (value.len != len * 32 + 4) {
      throw LmdbWrapperException("missing hashes entry size mismatch");
   }
   for (uint32_t i = 0; i < len; i++) {
      missingHashesSet.emplace(std::move(brr.get_BinaryData(32)));
   }
   return missingHashesSet;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putStoredDBInfo(DB_SELECT db,
   const StoredDBInfo& sdbi, uint16_t id)
{
   auto dbPtr = getDbPtr(db);
   dbPtr->putStoredDBInfo(sdbi, id);
}

StoredDBInfo LMDBBlockDatabase::getStoredDBInfo(DB_SELECT db, uint16_t id)
{
   auto dbPtr = getDbPtr(db);
   return dbPtr->getStoredDBInfo(id);
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::openDB(DB_SELECT db)
{
   auto dbPtr = getDbPtr(db);
   dbPtr->open();
}

void LMDBBlockDatabase::closeDB(DB_SELECT db)
{
   auto dbPtr = getDbPtr(db);
   dbPtr->close();
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getOrSetFlaggedBlockFile(uint32_t fileNum)
{
   BinaryWriter bw_key(5);
   bw_key.put_uint8_t((uint8_t)DbPrefix::FLAGGED_BLOCKFILES);
   bw_key.put_uint32_t(fileNum, BE);

   auto tx = beginTransaction(DB_SELECT::HEADERS, LMDB::Mode::ReadWrite);
   auto dbIter = tx->getIterator();
   if (!dbIter.seekToExact(bw_key.getDataRef())) {
      //missing this file num, add it
      LMDB::DataRef keyRef{bw_key.getSize(), bw_key.getDataRef().getPtr()};
      tx->insert(keyRef, LMDB::DataRef{0, (const char*)nullptr});
      return true;
   } else {
      //nothing to set, return false
      return false;
   }
}

////////
std::vector<uint32_t> LMDBBlockDatabase::getFlaggedFileNums() const
{
   auto tx = beginTransaction(DB_SELECT::HEADERS, LMDB::Mode::ReadOnly);
   auto dbIter = tx->getIterator();
   if (!dbIter.seekToStartsWith(DbPrefix::FLAGGED_BLOCKFILES)) {
      return {};
   }

   std::vector<uint32_t> result;
   do {
      if (!dbIter.verifyPrefix(DbPrefix::FLAGGED_BLOCKFILES)) {
         break;
      }

      auto keyReader = dbIter.getKeyReader();
      result.emplace_back(keyReader.get_uint32_t(BE));
   } while (dbIter.advanceAndRead());
   return result;
}

void LMDBBlockDatabase::clearFlaggedFileNums()
{
   auto tx = beginTransaction(DB_SELECT::HEADERS, LMDB::Mode::ReadWrite);
   auto dbIter = tx->getIterator();
   if (!dbIter.seekToStartsWith(DbPrefix::FLAGGED_BLOCKFILES)) {
      return;
   }

   std::set<BinaryData> keysToDelete;
   do {
      if (!dbIter.verifyPrefix(DbPrefix::FLAGGED_BLOCKFILES)) {
         break;
      }
      keysToDelete.emplace(dbIter.getKey());
   } while (dbIter.advanceAndRead());

   for (const auto& key : keysToDelete) {
      tx->erase(LMDB::DataRef{key.getSize(), key.getPtr()});
   }
}

////////
std::map<Types::TxIOKey, TxOutData>
LMDBBlockDatabase::getTxOutHistoryForScrAddrKey(
   uint32_t scrAddrId, Types::BlockId start, Types::BlockId end) const
{
   auto tx = beginTransaction(DB_SELECT::TXOUTS, LMDB::Mode::ReadWrite);
   auto dbIter = tx->getIterator();
   auto firstKey = Types::constructScrAddrKey(scrAddrId, start);
   BinaryDataRef firstKeyRef{
      (const uint8_t*)&firstKey, sizeof(Types::ScrAddrKey)};
   if (!dbIter.seekTo(firstKeyRef)) {
      return {};
   }

   Types::ScrAddrKey saKey;
   std::map<Types::TxIOKey, TxOutData> result;
   do {
      auto keyRef = dbIter.getKeyRef();
      if (keyRef.getSize() != sizeof(Types::ScrAddrKey)) {
         continue;
      }
      std::memcpy(&saKey, keyRef.getPtr(), sizeof(Types::ScrAddrKey));
      auto saId = Types::getScrAddrIdFromScrAddrKey(saKey);
      if (saId != scrAddrId) {
         break;
      }

      //get blockID
      auto blockID = Types::getBlockIDFromScrAddrKey(saKey);
      if (blockID > end) {
         break;
      }

      //deser txoutdata bodies
      auto valReader = dbIter.getValueReader();
      while (valReader.getSizeRemaining() >= 12) {
         Types::Amount amount = valReader.get_uint64_t();
         Types::TxId txId = valReader.get_uint16_t();
         Types::TxIOId txOutId = valReader.get_uint16_t();
         auto txOutKey = Types::constructTxIOKey(blockID, txId, txOutId);

         result.emplace(txOutKey, TxOutData{
            amount, blockID, txId, txOutId});
      }
   } while (dbIter.advanceAndRead());
   return result;
}

std::map<Types::TxIOKey, Types::TxIOKey>
LMDBBlockDatabase::getTxInHistoryForTxOutHistory(
   const std::vector<Types::TxIOKey>& txOutKeys) const
{
   std::map<Types::TxIOKey, Types::TxIOKey> result;
   auto tx = beginTransaction(DB_SELECT::TXINS, LMDB::Mode::ReadWrite);

   for (const auto& key : txOutKeys) {
      auto val = tx->get(LMDB::DataRef{
         sizeof(Types::TxIOKey), (const char*)&key});
      if (val.len != sizeof(Types::TxIOKey)) {
         continue;
      }
      auto emplaceResult = result.emplace(key, Types::INVALID_TXIO_KEY).first;
      std::memcpy(&emplaceResult->second, val.data, sizeof(Types::TxIOKey));
   }
   return result;
}

Types::TxIOKey LMDBBlockDatabase::getTxInHistoryForTxOutKey(
   Types::TxIOKey txOutKey) const
{
   auto tx = beginTransaction(DB_SELECT::TXINS, LMDB::Mode::ReadWrite);
   auto val = tx->get(LMDB::DataRef{
      sizeof(Types::TxIOKey), (const char*)&txOutKey});
   if (val.len != sizeof(Types::TxIOKey)) {
      return UINT64_MAX;
   }

   Types::TxIOKey result;
   std::memcpy(&result, val.data, sizeof(Types::TxIOKey));
   return result;
}

////////////////////////////////////////////////////////////////////////////////
// DatabaseContainer
DatabaseContainer::DatabaseContainer(
   const std::filesystem::path& basedir,
   const std::string& name, size_t mapSize) :
   baseDir_{basedir}, name_{name}, db_(mapSize)
{}

DatabaseContainer::~DatabaseContainer()
{
   close();
}

////////
std::string DatabaseContainer::getDbName(DB_SELECT db)
{
   switch (db)
   {
   case DB_SELECT::HEADERS:
      return "headers";

   case DB_SELECT::SCRADDR:
      return "scraddr";

   case DB_SELECT::TXOUTS:
      return "txouts";

   case DB_SELECT::TXINS:
      return "txins";

   case DB_SELECT::KNOWNHASHES:
      return "known_hashes";

   case DB_SELECT::TXHINTS:
      return "txhints";

   case DB_SELECT::TXFILTERS:
      return "txfilters";

   case DB_SELECT::ZERO_CONF:
      return "zeroconf";

   default:
      throw LmdbWrapperException("unknown db");
   }
}

void DatabaseContainer::close()
{
   db_.close();
}

void DatabaseContainer::open()
{
   db_.open(baseDir_ / name_, name_);
}

////////
void DatabaseContainer::eraseOnDisk()
{
   close();
   std::filesystem::path dbPath = baseDir_ / name_;
   std::filesystem::remove(dbPath);

   dbPath.append("-lock");
   std::filesystem::remove(dbPath);
}

void DatabaseContainer::putStoredDBInfo(
   const StoredDBInfo& sdbi, uint16_t id)
{
   if (!sdbi.isInitialized()) {
      throw LmdbWrapperException("tried to write uninitiliazed sdbi");
   }
   auto key = StoredDBInfo::getDBKey(id);
   LMDB::DataRef keyRef{key.getSize(), key.getPtr()};

   BinaryWriter bw;
   sdbi.serializeDBValue(bw);
   LMDB::DataRef valRef{bw.getSize(), bw.getDataRef().getPtr()};

   auto tx = db_.beginTransaction(LMDB::Mode::ReadWrite);
   tx.insert(keyRef, valRef);
}

StoredDBInfo DatabaseContainer::getStoredDBInfo(uint16_t id)
{
   auto tx = db_.beginTransaction(LMDB::Mode::ReadOnly);
   auto key = StoredDBInfo::getDBKey(id);
   auto val = tx.get(LMDB::DataRef{key.getSize(), key.getPtr()});
   BinaryRefReader brr((const uint8_t*)val.data, val.len);

   if (brr.empty()) {
      throw LmdbWrapperException("no sdbi at this key");
   }
   StoredDBInfo sdbi;
   sdbi.unserializeDBValue(brr);
   return sdbi;
}

////////
std::unique_ptr<DBTransaction> DatabaseContainer::beginTransaction(
   LMDB::Mode mode) const
{
   return std::make_unique<DBTransaction>(
      std::move(db_.beginTransaction(mode)));
}

////////////////////////////////////////////////////////////////////////////////
// DBPair
DBPair::DBPair(size_t mapsize) :
   mapSize_(mapsize)
{}

////////
void DBPair::open(const std::filesystem::path& path, const std::string& dbName)
{
   if (isOpen()) {
      return;
   }
   unsigned flags = MDB_NOSYNC | MDB_NOTLS;

   env_.open(path, flags);
   env_.setMapSize(mapSize_);
   db_.open(&env_, dbName);
}

void DBPair::close()
{
   if (!isOpen()) {
      return;
   }
   db_.close();
   env_.close();
}

bool DBPair::isOpen() const
{
   return env_.isOpen() && db_.isOpen();
}

////////
LMDB::Transaction DBPair::beginTransaction(LMDB::Mode mode)
{
   return LMDB::Transaction{&env_, db_.dbi(), mode};
}
