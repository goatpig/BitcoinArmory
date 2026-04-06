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
#include <Utils/ArmoryConfig.h>
#include <Utils/BitcoinSettings.h>
#include <Utils/ArmoryErrors.h>
#include <Utils/BCTX.h>
#include <Utils/UniversalTimer.h>

#include "BlockObj.h"
#include "StoredBlockObj.h"
#include "txio.h"
#include "BlockDataMap.h"
#include "Blockchain.h"
#include "TxHashFilters.h"

using namespace Armory;

const std::set<DB_SELECT> LMDBBlockDatabase::supernodeDBs_({});
const std::map<std::string, size_t> LMDBBlockDatabase::mapSizes_ = {
   {"headers", 50 * 1024 * 1024 * 1024ULL},
   {"blkdata", 50 * 1024 * 1024ULL},
   {"history", 50 * 1024 * 1024ULL},
   {"txhints", 50 * 1024 * 1024 * 1024ULL},
   {"ssh",   2000 * 1024 * 1024 * 1024ULL},
   {"subssh", 2000 * 1024 * 1024 * 1024ULL},
   {"subssh_meta", 1024 * 1024 * 1024ULL},
   {"stxo", 2000 * 1024 * 1024 * 1024ULL},
   {"zeroconf", 10 * 1024 * 1024 * 1024ULL},
   {"txfilters", 100 * 1024 * 1024 * 1024ULL},
   {"spentness", 2000 * 1024 * 1024 * 1024ULL},
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
// LDBIter
LDBIter::LDBIter() :
   isDirty_(true)
{}

LDBIter::~LDBIter()
{}

void LDBIter::resetReaders()
{
   currKeyReader_.resetPosition();
   currValueReader_.resetPosition();
}

////////
bool LDBIter::isValid(DbPrefix dbpref)
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
bool LDBIter::advance(DbPrefix prefix)
{
   advance();
   return isValid(prefix);
}

bool LDBIter::advanceAndRead()
{
   if (!advance()) {
      return false;
   }
   return readIterData();
}

bool LDBIter::advanceAndRead(DbPrefix prefix)
{
   if (!advance(prefix)) {
      return false;
   }
   return readIterData();
}


////////
BinaryData LDBIter::getKey() const
{
   if(isDirty_) {
      LOGERR << "Returning dirty key ref";
      return {};
   }
   return currKey_;
}

BinaryData LDBIter::getValue() const
{
   if(isDirty_) {
      LOGERR << "Returning dirty value ref";
      return {};
   }
   return currValue_;
}

BinaryDataRef LDBIter::getKeyRef() const
{
   if(isDirty_) {
      LOGERR << "Returning dirty key ref";
      return {};
   }
   return currKeyReader_.getRawRef();
}
   
BinaryDataRef LDBIter::getValueRef() const
{
   if(isDirty_) {
      LOGERR << "Returning dirty value ref";
      return {};
   }
   return currValueReader_.getRawRef();
}

BinaryRefReader& LDBIter::getKeyReader() const
{
   if(isDirty_) {
      LOGERR << "Returning dirty key reader";
   }
   return currKeyReader_;
}

BinaryRefReader& LDBIter::getValueReader() const
{
   if(isDirty_)
      LOGERR << "Returning dirty value reader";
   return currValueReader_;
}

////////
bool LDBIter::seekTo(DbPrefix pref, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)pref);
   bw.put_BinaryData(key);
   return seekTo(bw.getDataRef());
}

bool LDBIter::seekToExact(DbPrefix pref, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)pref);
   bw.put_BinaryData(key);
   return seekToExact(bw.getDataRef());
}

bool LDBIter::seekToStartsWith(BinaryDataRef key)
{
   if (!seekTo(key)) {
      return false;
   }
   return checkKeyStartsWith(key);

}

bool LDBIter::seekToStartsWith(DbPrefix prefix)
{
   BinaryWriter bw(1);
   bw.put_uint8_t((uint8_t)prefix);
   if (!seekTo(bw.getDataRef())) {
      return false;
   }
   return checkKeyStartsWith(bw.getDataRef());

}

bool LDBIter::seekToStartsWith(DbPrefix pref, BinaryDataRef key)
{
   if (!seekTo(pref, key)) {
      return false;
   }
   return checkKeyStartsWith(pref, key);
}

bool LDBIter::seekToBefore(DbPrefix prefix)
{
   BinaryWriter bw(1);
   bw.put_uint8_t((uint8_t)prefix);
   return seekToBefore(bw.getDataRef());
}

bool LDBIter::seekToBefore(DbPrefix pref, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)pref);
   bw.put_BinaryData(key);
   return seekToBefore(bw.getDataRef());
}

////////
bool LDBIter::checkKeyExact(BinaryDataRef key)
{
   if (isDirty_ && !readIterData()) {
      return false;
   }
   return (key==currKeyReader_.getRawRef());
}

bool LDBIter::checkKeyExact(DbPrefix prefix, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key);
   if (isDirty_ && !readIterData()) {
      return false;
   }
   return bw.getDataRef() == currKeyReader_.getRawRef();
}

bool LDBIter::checkKeyStartsWith(BinaryDataRef key)
{
   if (isDirty_ && !readIterData()) {
      return false;
   }
   return currKeyReader_.getRawRef().startsWith(key);
}

bool LDBIter::verifyPrefix(DbPrefix prefix, bool advanceReader)
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

bool LDBIter::checkKeyStartsWith(DbPrefix prefix, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key);
   return checkKeyStartsWith(bw.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
// LDBIter_Single
LDBIter_Single::LDBIter_Single(LMDB::Iterator&& iter) :
   iter_(std::move(iter))
{}

bool LDBIter_Single::isNull() const
{
   return !iter_.isValid();
}

bool LDBIter_Single::isValid() const
{
   return iter_.isValid();
}

////////
bool LDBIter_Single::seekTo(BinaryDataRef key)
{
   iter_.seek(CharacterArrayRef(
      key.getSize(), key.getPtr()),
      LMDB::Iterator::SeekBy::GE);
   return readIterData();
}

bool LDBIter_Single::seekToExact(BinaryDataRef key)
{
   if (!seekTo(key)) {
      return false;
   }
   return checkKeyExact(key);
}

bool LDBIter_Single::seekToBefore(BinaryDataRef key)
{
   iter_.seek(CharacterArrayRef(
      key.getSize(), key.getPtr()),
      LMDB::Iterator::SeekBy::LE);
   return readIterData();
}

////////
bool LDBIter_Single::advance()
{
   ++iter_;
   isDirty_ = true;
   return isValid();
}

bool LDBIter_Single::retreat()
{
   --iter_;
   isDirty_ = true;
   return isValid();
}

////////
bool LDBIter_Single::readIterData()
{
   if (!isValid()){
      isDirty_ = true;
      return false;
   }

   currKey_ = BinaryDataRef(
      (uint8_t*)iter_.key().mv_data,
      iter_.key().mv_size);
   currValue_ = BinaryDataRef(
      (uint8_t*)iter_.value().mv_data,
      iter_.value().mv_size);

   currKeyReader_.setNewData(currKey_);
   currValueReader_.setNewData(currValue_);
   isDirty_ = false;
   return true;
}

////////
bool LDBIter_Single::seekToFirst()
{
   iter_.toFirst();
   return readIterData();
}

bool LDBIter_Single::seekToLast()
{
   iter_.toLast();
   return readIterData();
}

////////////////////////////////////////////////////////////////////////////////
// LMDBBlockDatabase
LMDBBlockDatabase::LMDBBlockDatabase(const std::filesystem::path& blkFolder) :
   blkFolder_(blkFolder)
{}

LMDBBlockDatabase::~LMDBBlockDatabase(void)
{
   closeDatabases();
}

////////
bool LMDBBlockDatabase::databasesAreOpen() const
{
   return dbIsOpen_;
}

std::shared_ptr<DatabaseContainer> LMDBBlockDatabase::getDbPtr(
   DB_SELECT db) const
{
   auto iter = dbMap_.find(db);
   if (iter == dbMap_.end()) {
      throw LMDBException("unexpected DB_SELECT");
   }
   return iter->second;
}

std::unique_ptr<LDBIter> LMDBBlockDatabase::getIterator(DB_SELECT db) const
{
   auto dbObj = getDbPtr(db);
   return dbObj->getIterator();
}

std::unique_ptr<DbTransaction> LMDBBlockDatabase::beginTransaction(
   DB_SELECT db, LMDB::Mode mode) const
{
   auto dbObj = getDbPtr(db);
   return dbObj->beginTransaction(mode);
}

ARMORY_DB_TYPE LMDBBlockDatabase::getDbType() const
{
   return Config::DBSettings::getDbType();
}

const std::filesystem::path& LMDBBlockDatabase::baseDir() const
{
   return DatabaseContainer::baseDir_;
}

/////////////////////////////////////////////////////////////////////////////
// The dbType and pruneType inputs are left blank if you are just going to
// take whatever is the current state of database.  You can choose to
// manually specify them, if you want to throw an error if it's not what you
// were expecting
void LMDBBlockDatabase::openDatabases(const std::filesystem::path& basedir)
{
   LOGINFO << "Opening databases...";
   LOGINFO << "dbmode: " << Config::DBSettings::getDbModeStr();

   DatabaseContainer::baseDir_ = basedir;
   DatabaseContainer::magicBytes_ = Config::BitcoinSettings::getMagicBytes();

   if (!Config::BitcoinSettings::isInitialized()) {
      LOGERR << " must set magic bytes and genesis block";
      LOGERR << "           before opening databases.";
      throw LmdbWrapperException("magic bytes not set");
   }

   // Just in case this isn't the first time we tried to open it.
   closeDatabases();


   for (int i = 0; i < (int)DB_SELECT::COUNT; i++) {
      DB_SELECT CURRDB = DB_SELECT(i);
      auto iter = dbMap_.find(CURRDB);
      if (iter == dbMap_.end()) {
         if (getDbType() == ARMORY_DB_TYPE::Super) {
            if (supernodeDBs_.find(CURRDB) != supernodeDBs_.end()) {
               continue;
            }
         }

         dbMap_.emplace(CURRDB,
            std::make_shared<DatabaseContainer_Single>(CURRDB));
      }

      StoredDBInfo sdbi = openDB(CURRDB);

      // Check that the magic bytes are correct
      if (Config::BitcoinSettings::getMagicBytes() != sdbi.magic) {
         throw DbErrorMsg("Magic bytes mismatch!  Different blokchain?");
      }

      if (CURRDB == DB_SELECT::HEADERS) {
         if (getDbType() != sdbi.armoryType) {
            LOGERR << "db type mismatch, aborting";
            exit(-2);
         }
      }
   }

   if (getDbType() == ARMORY_DB_TYPE::Super) {
      loadHeightToIdMap();
   }
 
   //sanity check: try to open older SDBI version
   auto dbPtr = getDbPtr(DB_SELECT::HEADERS);
   auto tx = dbPtr->beginTransaction(LMDB::Mode::ReadOnly);
   BinaryData key;
   key.append((uint8_t)DbPrefix::DBINFO);
   auto valueRef = dbPtr->getValue(key.getRef());

   if (!valueRef.empty()) {
      //old style db, fail
      LOGERR << "DB version mismatch. Use another dbdir!";
      throw DbErrorMsg("DB version mismatch. Use another dbdir!");
   }
   dbIsOpen_ = true;
}

void LMDBBlockDatabase::closeDatabases()
{
   for (auto& dbPair : dbMap_) {
      dbPair.second->close();
   }
   dbMap_.clear();
   dbIsOpen_ = false;
}

////////
void LMDBBlockDatabase::replaceDatabases(
   DB_SELECT db, const std::string& swap_path)
{
   /*replace a db underlying file with file [swap_path]*/
   auto full_swap_path = DatabaseContainer::getDbPath(swap_path);
   closeDB(db);

   //delete underlying files
   auto db_name = DatabaseContainer::getDbPath(db);
   auto lock_name = db_name;
   lock_name.append("-lock");

   std::filesystem::remove(db_name);
   std::filesystem::remove(lock_name);

   //rename swap_path to db name
   std::filesystem::rename(full_swap_path, db_name);

   //rename lock file
   auto swap_lock = full_swap_path;
   swap_lock.append("-lock");
   std::filesystem::rename(swap_lock.c_str(), lock_name.c_str());

   //open db
   openDB(db);
}

void LMDBBlockDatabase::cycleDatabase(DB_SELECT db)
{
   auto dbPtr = getDbPtr(db);
   dbPtr->close();
   dbPtr->open();
}

void LMDBBlockDatabase::resetHistoryDatabases()
{
   if (getDbType() != ARMORY_DB_TYPE::Super) {
      resetSSHdb();

      auto db_subssh = getDbPtr(DB_SELECT::SUBSSH);
      auto db_hints = getDbPtr(DB_SELECT::TXHINTS);
      auto db_stxo = getDbPtr(DB_SELECT::STXO);
      closeDatabases();

      db_subssh->eraseOnDisk();
      db_hints->eraseOnDisk();
      db_stxo->eraseOnDisk();
   } else {
      auto db_subssh = getDbPtr(DB_SELECT::SUBSSH);
      auto db_subssh_meta = getDbPtr(DB_SELECT::SUBSSH_META);
      auto db_ssh = getDbPtr(DB_SELECT::SSH);
      auto db_spentness = getDbPtr(DB_SELECT::SPENTNESS);
      closeDatabases();

      db_subssh->eraseOnDisk();
      db_subssh_meta->eraseOnDisk();
      db_ssh->eraseOnDisk();
      db_spentness->eraseOnDisk();
   }
   openDatabases(DatabaseContainer::baseDir_);
}

void LMDBBlockDatabase::destroyAndResetDatabases()
{
   // We want to make sure the database is restarted with the same parameters
   // it was called with originally
   {
      auto dbMap{dbMap_}; //save dbMap because closeDatabases clears it
      closeDatabases();
      for (auto& dbPair : dbMap) {
         dbPair.second->eraseOnDisk();
      }
   }
   
   // Reopen the databases with the exact same parameters as before
   // The close & destroy operations shouldn't have changed any of that.
   openDatabases(DatabaseContainer::baseDir_);
}

/////////////////////////////////////////////////////////////////////////////
// Get value without resorting to a DB iterator
BinaryDataRef LMDBBlockDatabase::getValueNoCopy(DB_SELECT db,
   BinaryDataRef key) const
{
   auto dbPtr = getDbPtr(db);
   return dbPtr->getValue(key);
}

/////////////////////////////////////////////////////////////////////////////
// Get value using BinaryDataRef object.  The data from the get* call is 
// actually copied to a member variable, and thus the refs are valid only 
// until the next get* call.
BinaryDataRef LMDBBlockDatabase::getValueRef(
   DB_SELECT db, DbPrefix prefix, BinaryDataRef key) const
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key);
   return getValueNoCopy(db, bw.getDataRef());
}

/////////////////////////////////////////////////////////////////////////////
// Same as the getValueRef, in that they are only valid until the next get*
// call.  These are convenience methods which basically just save us 
BinaryRefReader LMDBBlockDatabase::getValueReader(
   DB_SELECT db, BinaryDataRef keyWithPrefix) const
{
   return BinaryRefReader(getValueNoCopy(db, keyWithPrefix));
}

/////////////////////////////////////////////////////////////////////////////
// Same as the getValueRef, in that they are only valid until the next get*
// call.  These are convenience methods which basically just save us 
BinaryRefReader LMDBBlockDatabase::getValueReader(
   DB_SELECT db, DbPrefix prefix, BinaryDataRef key) const
{
   return BinaryRefReader(getValueRef(db, prefix, key));
}

/////////////////////////////////////////////////////////////////////////////
BinaryData LMDBBlockDatabase::getDBKeyForHash(BinaryDataRef txhash,
   uint8_t expectedDupId) const
{
   if (txhash.getSize() < 4) {
      LOGWARN << "txhash is less than 4 bytes long";
      return {};
   }
   auto hash4 = txhash.getSliceRef(0, 4);

   auto txHints = beginTransaction(DB_SELECT::TXHINTS, LMDB::Mode::ReadOnly);
   BinaryRefReader brrHints = getValueRef(DB_SELECT::TXHINTS, DbPrefix::TXHINTS, hash4);

   uint32_t valSize = brrHints.getSize();
   if (valSize < 6) {
      return {};
   }
   uint32_t numHints = (uint32_t)brrHints.get_var_int();

   if (getDbType() != ARMORY_DB_TYPE::Super) {
      uint32_t height;
      uint8_t  dup;
      uint16_t txIdx;
      for (uint32_t i = 0; i < numHints; i++) {
         BinaryDataRef hint = brrHints.get_BinaryDataRef(6);
         BinaryRefReader brrHint(hint);
         DBUtils::readBlkDataKeyNoPrefix(brrHint, height, dup, txIdx);

         if (dup != expectedDupId) {
            if (dup != getValidDupIDForHeight(height) && numHints > 1) {
               continue;
            }
         }

         auto txKey = DBUtils::getBlkDataKey(height, dup, txIdx);
         auto dbVal = getValueNoCopy(DB_SELECT::TXHINTS, txKey.getRef());
         if (dbVal.getSize() < 36) {
            continue;
         }
         auto txHashRef = dbVal.getSliceRef(4, 32);

         if (txHashRef != txhash) {
            continue;
         }
         return txKey.getSliceCopy(1, 6);
      }
   } else {
      BinaryData forkedMatch;
      bool offChainHints = false;
      for (uint32_t i = 0; i < numHints; i++) {
         BinaryDataRef hint = brrHints.get_BinaryDataRef(6);

         //check this key is on the main branch
         auto hintRef = hint.getSliceRef(0, 4);
         auto blockId = DBUtils::hgtxToHeight(hintRef);

         if (!isBlockIDOnMainBranch(blockId)) {
            forkedMatch = hint;
            offChainHints = true;
            continue;
         }

         //check hash matches
         auto txhashfromdb = getTxHashForLdbKey(hint, nullptr);
         if (txhash != txhashfromdb) {
            continue;
         }
         return hint;
      }

      if (forkedMatch.empty()) {
         //LOGWARN << "failed to get valid key for txhash with " << numHints << " hints";

         if (brrHints.getSizeRemaining() != 0) {
            LOGWARN << " bytes remaining for this hint";
         }
         if (offChainHints) {
            LOGWARN << " had off chain hits";
         }
      }
      return forkedMatch;
   }
   return {};
}

/////////////////////////////////////////////////////////////////////////////
// Put value based on BinaryData key.  If batch writing, pass in the batch
void LMDBBlockDatabase::putValue(DB_SELECT db,
   BinaryDataRef key, BinaryDataRef value)
{
   auto dbPtr = getDbPtr(db);
   dbPtr->putValue(key, value);
}

/////////////////////////////////////////////////////////////////////////////
// Put value based on BinaryData key.  If batch writing, pass in the batch
void LMDBBlockDatabase::putValue(DB_SELECT db, DbPrefix prefix,
   BinaryDataRef key, BinaryDataRef value)
{
   BinaryWriter bw;
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key.getPtr(), key.getSize());
   putValue(db, bw.getDataRef(), value);
}

/////////////////////////////////////////////////////////////////////////////
// Delete value based on BinaryData key.  If batch writing, pass in the batch
void LMDBBlockDatabase::deleteValue(DB_SELECT db, BinaryDataRef key)
{
   auto dbPtr = getDbPtr(db);
   dbPtr->deleteValue(key);
}

/////////////////////////////////////////////////////////////////////////////
// Delete Put value based on BinaryData key.  If batch writing, pass in the batch
void LMDBBlockDatabase::deleteValue(DB_SELECT db, DbPrefix prefix,
   BinaryDataRef key)
{
   BinaryWriter bw;
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key);
   deleteValue(db, bw.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::fillStoredSubHistory(StoredScriptHistory& ssh,
   unsigned start, unsigned end) const
{
   if (Config::DBSettings::getDbType() == ARMORY_DB_TYPE::Super) {
      return fillStoredSubHistory_Super(ssh, start, end);
   } else {
      auto subsshtx = beginTransaction(DB_SELECT::SUBSSH, LMDB::Mode::ReadOnly);
      auto subsshIter = getIterator(DB_SELECT::SUBSSH);

      BinaryWriter dbkey_withHgtX;
      dbkey_withHgtX.put_uint8_t((uint8_t)DbPrefix::SCRIPT);
      dbkey_withHgtX.put_BinaryData(ssh.uniqueKey);

      if (start != 0) {
         dbkey_withHgtX.put_BinaryData(DBUtils::heightAndDupToHgtx(start, 0));
      }

      if (!subsshIter->seekTo(dbkey_withHgtX.getDataRef())) {
         return false;
      }

      // Now start iterating over the sub histories
      std::map<BinaryData, StoredSubHistory>::iterator iter;
      size_t numTxioRead = 0;
      do {
         size_t _sz = subsshIter->getKeyRef().getSize();
         BinaryDataRef keyNoPrefix = subsshIter->getKeyRef().getSliceRef(1, _sz - 1);
         if (!keyNoPrefix.startsWith(ssh.uniqueKey)) {
            break;
         }
         std::pair<BinaryData, StoredSubHistory> keyValPair;
         keyValPair.first = keyNoPrefix.getSliceCopy(_sz - 5, 4);
         keyValPair.second.unserializeDBKey(subsshIter->getKeyRef());

         //iter is at the right ssh, make sure hgtX <= endBlock
         if (keyValPair.second.height > end) {
            break;
         }
         //skip invalid dupIDs
         if (keyValPair.second.dupID !=
            getValidDupIDForHeight(keyValPair.second.height)) {
            continue;
         }
         keyValPair.second.unserializeDBValue(subsshIter->getValueReader());
         iter = ssh.subHistMap.emplace(keyValPair).first;
         numTxioRead += iter->second.txioMap.size();
      } while (subsshIter->advanceAndRead(DbPrefix::SCRIPT));
      return true;
   }
}

////////////////////////////////////////////////////////////////////////////////
unsigned LMDBBlockDatabase::getShardIdForHeight(unsigned height) const
{
   auto hiMap = heightToBatchId_.get();
   if (hiMap->empty()) {
      return UINT32_MAX;
   }
   auto height_iter = hiMap->lower_bound(height);
   if (height_iter == hiMap->end()) {
      return hiMap->rbegin()->second;
   }
   if (height_iter->first > height && height_iter != hiMap->begin()) {
      --height_iter;
   }
   return height_iter->second;
}

////////////////////////////////////////////////////////////////////////////////
unsigned LMDBBlockDatabase::getNextShardIdForHeight(unsigned height) const
{
   auto hiMap = heightToBatchId_.get();
   auto height_iter = hiMap->upper_bound(height);
   if (height_iter == hiMap->end()) {
      return UINT32_MAX;
   }
   return height_iter->second;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::fillStoredSubHistory_Super(
   StoredScriptHistory& ssh, unsigned start, unsigned end) const
{
   auto dupIdMap = validDupByHeight_.get();
   std::function<bool(unsigned, uint8_t)> isValidDupId = [dupIdMap]
   (unsigned height, uint8_t dupid)->bool
   {
      auto iter = dupIdMap->find(height);
      if (iter == dupIdMap->end()) {
         return false;
      }
      return dupid == iter->second;
   };

   auto meta_tx = beginTransaction(DB_SELECT::SUBSSH_META, LMDB::Mode::ReadOnly);

   //convert height range to batch id range
   auto start_id = getShardIdForHeight(start);
   if (start_id == UINT32_MAX) {
      return true;
   }
   auto end_id = getNextShardIdForHeight(end);

   //prepare for subssh db parsing
   BinaryWriter bwKey(4 + ssh.uniqueKey.getSize());
   bwKey.put_uint32_t(0);
   bwKey.put_BinaryData(ssh.uniqueKey);

   auto keyRef = bwKey.getDataRef();
   auto ptr = (uint8_t*)keyRef.getPtr();

   //get subssh summary iterator positioned at <= start_id
   auto ssh_lower_bound = ssh.subsshSummary.lower_bound(start_id);
   if (ssh_lower_bound == ssh.subsshSummary.end()) {
      return true;
   }
   if (ssh_lower_bound->first > start_id &&
      ssh_lower_bound != ssh.subsshSummary.begin()) {
      --ssh_lower_bound;
   }

   //grab db iterator
   auto subsshtx = beginTransaction(DB_SELECT::SUBSSH, LMDB::Mode::ReadOnly);
   auto dbIter = getIterator(DB_SELECT::SUBSSH);

   while (ssh_lower_bound != ssh.subsshSummary.end()) {
      //break if iterator is past end_id
      if (ssh_lower_bound->first > end_id) {
         break;
      }

      //grab meta entry for batch id
      BinaryWriter bw_meta(8);
      bw_meta.put_uint32_t(ssh_lower_bound->first, BE);
      bw_meta.put_uint32_t(0);
      auto meta_value = getValueNoCopy(DB_SELECT::SUBSSH_META, bw_meta.getDataRef());
      if (meta_value.empty()) {
         LOGWARN << "missing meta entry at batch id " << ssh_lower_bound->first;
         ++ssh_lower_bound;
         continue;
      }

      //grab height offsets
      BinaryRefReader meta_refreader(meta_value);
      auto height_offset = meta_refreader.get_uint32_t();
      auto spent_offset = meta_refreader.get_uint32_t();

      //set batch id in subssh key
      auto id_ptr = (uint8_t*)&ssh_lower_bound->first;
      ptr[0] = id_ptr[3];
      ptr[1] = id_ptr[2];
      ptr[2] = id_ptr[1];
      ptr[3] = id_ptr[0];

      //set iterator at subssh key
      if (!dbIter->seekToExact(keyRef)) {
         LOGWARN << "missing subssh expected batch id";
         ++ssh_lower_bound;
         continue;
      }

      ssh.decompressManySubssh(dbIter->getValueRef(),
         height_offset, spent_offset,
         start, end, isValidDupId);
      ++ssh_lower_bound;
   }
   return true;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putStoredScriptHistorySummary(StoredScriptHistory& ssh)
{
   SCOPED_TIMER("putStoredScriptHistory");
   if (!ssh.isInitialized()) {
      LOGERR << "Trying to put uninitialized ssh into DB";
      return;
   }
   BinaryWriter bw;
   ssh.serializeDBValue(bw, getDbType());
   putValue(DB_SELECT::SSH, ssh.getDBKey(), bw.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredScriptHistorySummary(StoredScriptHistory& ssh,
   BinaryDataRef scrAddr) const
{
   ssh.clear();
   auto tx = beginTransaction(DB_SELECT::SSH, LMDB::Mode::ReadOnly);
   auto ldbIter = getIterator(DB_SELECT::SSH);
   bool has = false;

   if (ldbIter->seekToExact(DbPrefix::SCRIPT, scrAddr)) {
      ssh.unserializeDBKey(ldbIter->getKeyRef());
      ssh.unserializeDBValue(ldbIter->getValueRef());
      has = true;
   }
   return has;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredScriptHistory(StoredScriptHistory& ssh,
   BinaryDataRef scrAddr, uint32_t startBlock, uint32_t endBlock) const
{
   if (!getStoredScriptHistorySummary(ssh, scrAddr)) {
      return false;
   }
   if (!fillStoredSubHistory(ssh, startBlock, endBlock)) {
      return false;
   }

   //grab UTXO flags
   getUTXOflags(ssh.subHistMap);
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredSubHistoryAtHgtX(StoredSubHistory& subssh,
   const BinaryDataRef scrAddrStr, const BinaryData& hgtX) const
{
   BinaryWriter bw(scrAddrStr.getSize() + hgtX.getSize());
   bw.put_BinaryData(scrAddrStr);
   bw.put_BinaryData(hgtX);
   return getStoredSubHistoryAtHgtX(subssh, bw.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredSubHistoryAtHgtX(StoredSubHistory& subssh,
   const BinaryData& dbkey) const
{
   if (getDbType() == ARMORY_DB_TYPE::Super) {
      LOGERR << "deprecated in supernode";
      throw std::runtime_error("deprecated in supernode");
   }

   auto tx = beginTransaction(DB_SELECT::SUBSSH, LMDB::Mode::ReadOnly);
   auto value = getValueNoCopy(DB_SELECT::SUBSSH, dbkey);
   if (value.empty()) {
      return false;
   }

   subssh.hgtX = dbkey.getSliceRef(-4, 4);
   subssh.unserializeDBValue(value);
   return true;
}


////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::getStoredScriptHistoryByRawScript(
   StoredScriptHistory& ssh, BinaryDataRef script) const
{
   BinaryData uniqueKey = BtcUtils::getTxOutScrAddr(script);
   getStoredScriptHistory(ssh, uniqueKey);
}

/////////////////////////////////////////////////////////////////////////////
// This doesn't actually return a SUBhistory, it grabs it and adds it to the
// regular-ssh object.  This does not affect balance or Txio count. It's
// simply filling in data that the ssh may be expected to have.
bool LMDBBlockDatabase::fetchStoredSubHistory(StoredScriptHistory& ssh,
   BinaryData hgtX, bool createIfDNE, bool forceReadDB)
{
   auto subIter = ssh.subHistMap.find(hgtX);
   if (!forceReadDB && subIter != ssh.subHistMap.end()) {
      return true;
   }

   BinaryData key = ssh.uniqueKey + hgtX;
   BinaryRefReader brr = getValueReader(DB_SELECT::BLKDATA, DbPrefix::SCRIPT, key);

   StoredSubHistory subssh;
   subssh.hgtX = hgtX;

   if (brr.getSize() > 0) {
      subssh.unserializeDBValue(brr);
   } else if (!createIfDNE) {
      return false;
   }
   ssh.mergeSubHistory(subssh);
   return true;
}

////////////////////////////////////////////////////////////////////////////////
uint64_t LMDBBlockDatabase::getBalanceForScrAddr(
   BinaryDataRef scrAddr, bool withMulti)
{
   StoredScriptHistory ssh;
   if (!withMulti) {
      getStoredScriptHistorySummary(ssh, scrAddr);
      return ssh.totalUnspent;
   } else {
      getStoredScriptHistory(ssh, scrAddr);
      uint64_t total = ssh.totalUnspent;
      std::map<BinaryData, UnspentTxOut> utxoList;
      getFullUTXOMapForSSH(ssh, utxoList);
      for (const auto& utxoPair : utxoList) {
         if (utxoPair.second.isMultisigRef()) {
            total += utxoPair.second.getValue();
         }
      }
      return total;
   }
}

////////////////////////////////////////////////////////////////////////////////
// We need the block hashes and scripts, which need to be retrieved from the
// DB, which is why this method can't be part of StoredBlockObj.h/.cpp
bool LMDBBlockDatabase::getFullUTXOMapForSSH(StoredScriptHistory& ssh,
   std::map<BinaryData, UnspentTxOut>& mapToFill)
{
   //TODO: deprecate. replace with paged version once new coin control is
   //implemented

   if (!ssh.haveFullHistoryLoaded()) {
      return false;
   }
   auto stxotx = beginTransaction(DB_SELECT::STXO, LMDB::Mode::ReadOnly);
   auto hinttx = beginTransaction(DB_SELECT::TXHINTS, LMDB::Mode::ReadOnly);

   for (const auto& ssPair : ssh.subHistMap) {
      const StoredSubHistory& subSSH = ssPair.second;
      for (const auto& txioPair : subSSH.txioMap) {
         const TxIOPair& txio = txioPair.second;
         if (!txio.isUTXO()) {
            continue;
         }

         BinaryData txoKey = txio.getDBKeyOfOutput();
         BinaryData txKey = txio.getTxRefOfOutput().getDBKey();
         uint16_t txoIdx = txio.getIndexOfOutput();

         StoredTxOut stxo;
         getStoredTxOut(stxo, txoKey);
         BinaryData txHash = getTxHashForLdbKey(txKey, nullptr);

         mapToFill[txoKey] = UnspentTxOut{
            txHash,
            txoIdx,
            stxo.blockHeight,
            txio.getValue(),
            stxo.getScriptRef()};
      }
   }
   return true;
}

/////////////////////////////////////////////////////////////////////////////
// TODO: We should also read the HeaderHgtList entries to get the blockchain
//       sorting that is saved in the DB.  But right now, I'm not sure what
//       that would get us since we are reading all the headers and doing
//       a fresh organize/sort anyway.
void LMDBBlockDatabase::readAllHeaders(
   const std::function<void(std::shared_ptr<BlockHeader>)>& callback)
{
   auto tx = beginTransaction(DB_SELECT::HEADERS, LMDB::Mode::ReadOnly);
   auto ldbIter = getIterator(DB_SELECT::HEADERS);
   if (!ldbIter->seekToStartsWith(DbPrefix::HEADHASH)) {
      LOGWARN << "No headers in DB yet!";
      return;
   }

   do {
      ldbIter->resetReaders();
      ldbIter->verifyPrefix(DbPrefix::HEADHASH);
      if (ldbIter->getKeyReader().getSizeRemaining() != 32) {
         LOGERR << "How did we get header hash not 32 bytes?";
         continue;
      }

      //key is the header's hash
      BinaryData headerHash;
      ldbIter->getKeyReader().get_BinaryData(headerHash, 32);

      //header data
      auto brrVal = ldbIter->getValueReader();
      auto regHead = std::make_shared<BlockHeader>(brrVal);

      //height & dup
      BinaryData hgtx = brrVal.get_BinaryData(4);
      regHead->setBlockHeight(DBUtils::hgtxToHeight(hgtx));
      regHead->setDuplicateID(DBUtils::hgtxToDupID(hgtx));

      //metadata
      regHead->setBlockSize(brrVal.get_uint32_t());
      regHead->setNumTx(brrVal.get_uint32_t());
      regHead->setBlockFileNum(brrVal.get_uint16_t());
      regHead->setBlockFileOffset(brrVal.get_uint64_t());
      regHead->setUniqueID(brrVal.get_uint32_t());

      //sanity check
      if (headerHash != regHead->getThisHash()) {
         LOGWARN << "Corruption detected: block header hash " <<
            headerHash.copySwapEndian().toHexStr() << " does not match "
            << regHead->getThisHash().copySwapEndian().toHexStr();
      }
      callback(regHead);
   } while (ldbIter->advanceAndRead(DbPrefix::HEADHASH));
}

////////////////////////////////////////////////////////////////////////////////
uint8_t LMDBBlockDatabase::getValidDupIDForHeight(uint32_t blockHgt) const
{
   auto dupmap = validDupByHeight_.get();
   auto iter = dupmap->find(blockHgt);
   if (iter == dupmap->end()) {
      LOGERR << "Block height exceeds DupID lookup table";
      return UINT8_MAX;
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::setValidDupIDForHeight(uint32_t blockHgt, uint8_t dup,
   bool overwrite)
{
   if (!overwrite) {
      auto dupmap = validDupByHeight_.get();
      auto iter = dupmap->find(blockHgt);
      if(iter != dupmap->end() && iter->second != UINT8_MAX) {
         return;
      }
   }

   std::map<unsigned, uint8_t> updateMap{{blockHgt, dup}};
   validDupByHeight_.update(updateMap);
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::setValidDupIDForHeight(
   std::map<unsigned, uint8_t>& dupMap)
{
   validDupByHeight_.update(dupMap);
}

////////////////////////////////////////////////////////////////////////////////
uint8_t LMDBBlockDatabase::getValidDupIDForHeight_fromDB(uint32_t blockHgt)
{
   BinaryData hgt4((uint8_t*)&blockHgt, 4);
   BinaryRefReader brrHgts = getValueReader(DB_SELECT::HEADERS, DbPrefix::HEADHGT, hgt4);

   if (brrHgts.empty()) {
      LOGERR << "Requested header does not exist in DB";
      return false;
   }

   uint8_t lenEntry = 33;
   uint8_t numDup = (uint8_t)(brrHgts.getSize() / lenEntry);
   for (uint8_t i=0; i < numDup; i++) {
      uint8_t dup8 = brrHgts.get_uint8_t(); 
      if ((dup8 & 0x80) > 0) {
         return (dup8 & 0x7f);
      }
   }

   LOGERR << "Requested a header-by-height but none were marked as main";
   return UINT8_MAX;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::isBlockIDOnMainBranch(unsigned blockId) const
{
   auto dupmap = blockIDMainChainMap_.get();
   auto iter = dupmap->find(blockId);
   if (iter == dupmap->end()) {
      //LOGERR << "no branching entry for blockID " << blockId;
      return false;
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::setBlockIDBranch(std::map<unsigned, bool>& idMap)
{
   blockIDMainChainMap_.update(idMap);
}

////////////////////////////////////////////////////////////////////////////////
// Puts bare header into HEADERS DB.  Use "putStoredHeader" to add to both
// (which actually calls this method as the first step)
//
// Returns the duplicateID of the header just inserted
uint8_t LMDBBlockDatabase::putBareHeader(StoredHeader& sbh,
   bool updateDupID, bool updateSDBI)
{
   SCOPED_TIMER("putBareHeader");

   if (!sbh.isInitialized()) {
      LOGERR << "Attempting to put uninitialized bare header into DB";
      return UINT8_MAX;
   }

   if (sbh.blockHeight == UINT32_MAX) {
      throw LmdbWrapperException("Attempted to put a header with no height");
   }

   // Batch the two operations to make sure they both hit the DB, or neither 
   auto tx = beginTransaction(DB_SELECT::HEADERS, LMDB::Mode::ReadWrite);
   auto sdbiH = getStoredDBInfo(DB_SELECT::HEADERS, 0);
   uint32_t height  = sbh.blockHeight;
   uint8_t sbhDupID = UINT8_MAX;

   // Check if it's already in the height-indexed DB - determine dupID if not
   StoredHeadHgtList hhl;
   getStoredHeadHgtList(hhl, height);

   bool alreadyInHgtDB = false;
   bool needToWriteHHL = false;
   if (hhl.dupAndHashList.empty()) {
      sbhDupID = 0;
      hhl.addDupAndHash(0, sbh.thisHash);
      if (sbh.isMainBranch) {
         hhl.preferredDup = 0;
      }
      needToWriteHHL = true;
   } else {
      int8_t maxDup = -1;
      for (const auto& dah : hhl.dupAndHashList) {
         maxDup = std::max(maxDup, (int8_t)dah.first);
         if (sbh.thisHash == dah.second) {
            alreadyInHgtDB = true;
            sbhDupID = dah.first;
            if (hhl.preferredDup != dah.first && sbh.isMainBranch && updateDupID) {
               // The header was in the head-hgt list, but not preferred
               hhl.preferredDup = dah.first;
               needToWriteHHL = true;
            }
            break;
         }
      }

      if (!alreadyInHgtDB) {
         needToWriteHHL = true;
         sbhDupID = maxDup+1;
         hhl.addDupAndHash(sbhDupID, sbh.thisHash);
         if (sbh.isMainBranch && updateDupID) {
            hhl.preferredDup = sbhDupID;
         }
      }
   }
   sbh.setKeyData(height, sbhDupID);
   if (needToWriteHHL) {
      putStoredHeadHgtList(hhl);
   }

   // Overwrite the existing hash-indexed entry, just in case the dupID was
   // not known when previously written.
   BinaryWriter bw;
   sbh.serializeDBValue(bw, DB_SELECT::HEADERS, getDbType());
   putValue(DB_SELECT::HEADERS, DbPrefix::HEADHASH, sbh.thisHash,
      bw.getDataRef());

   // If this block is valid, update quick lookup table, and store it in DBInfo
   if (sbh.isMainBranch) {
      if (updateSDBI) {
         sdbiH = std::move(getStoredDBInfo(DB_SELECT::HEADERS, 0));
         if (sbh.blockHeight >= sdbiH.topBlkHgt) {
            sdbiH.topBlkHgt = sbh.blockHeight;
            putStoredDBInfo(DB_SELECT::HEADERS, sdbiH, 0);
         }
      }
   }
   return sbhDupID;
}

////////
bool LMDBBlockDatabase::getBareHeader(StoredHeader& sbh,
   uint32_t blockHgt, uint8_t dup) const
{
   // Get the hash from the head-hgt list
   StoredHeadHgtList hhl;
   if (!getStoredHeadHgtList(hhl, blockHgt)) {
      LOGERR << "No headers at height " << blockHgt;
      return false;
   }

   for (const auto& dah : hhl.dupAndHashList) {
      if (dup == dah.first) {
         return getBareHeader(sbh, dah.second);
      }
   }
   return false;
}

bool LMDBBlockDatabase::getBareHeader(StoredHeader& sbh,
   uint32_t blockHgt) const
{
   uint8_t dupID = getValidDupIDForHeight(blockHgt);
   if (dupID == UINT8_MAX) {
      LOGERR << "Headers DB has no block at height: " << blockHgt; 
   }
   return getBareHeader(sbh, blockHgt, dupID);
}

bool LMDBBlockDatabase::getBareHeader(StoredHeader& sbh,
   BinaryDataRef headHash) const
{
   BinaryRefReader brr = getValueReader(
      DB_SELECT::HEADERS, DbPrefix::HEADHASH, headHash);
   if (brr.empty()) {
      LOGERR << "Header found in HHL but hash does not exist in DB";
      return false;
   }
   sbh.unserializeDBValue(DB_SELECT::HEADERS, brr);
   return true;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putStoredZC(StoredTx& stx, const BinaryData& zcKey)
{
   DB_SELECT dbs = DB_SELECT::ZERO_CONF;

   // Now add the base Tx entry in the BLKDATA DB.
   BinaryWriter bw;
   stx.serializeDBValue(bw, getDbType());
   bw.put_uint32_t(stx.unixTime);
   putValue(dbs, DbPrefix::ZCDATA, zcKey, bw.getDataRef());

   // Make sure all the parameters of the TxOut are set right 
   for (auto& stxoPair : stx.stxoMap) {
      stxoPair.second.txVersion = READ_UINT32_LE(stx.dataCopy.getPtr());
      stxoPair.second.txIndex = stx.txIndex;
      stxoPair.second.txOutIndex = stxoPair.first;
      BinaryData zcStxoKey(zcKey);
      zcStxoKey.append(WRITE_UINT16_BE(stxoPair.second.txOutIndex));
      putStoredZcTxOut(stxoPair.second, zcStxoKey);
   }
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::updatePreferredTxHint(BinaryDataRef hashOrPrefix,
   BinaryData preferDBKey)
{
   SCOPED_TIMER("updatePreferredTxHint");
   StoredTxHints sths;
   getStoredTxHints(sths, hashOrPrefix);

   if (sths.preferredDBKey == preferDBKey) {
      return;
   }

   // Check whether the hint already exists in the DB
   bool exists = false;
   for (const auto& dbKey : sths.dbKeyList) {
      if (dbKey == preferDBKey) {
         exists = true;
         break;
      }
   }

   if (!exists) {
      LOGERR << "Key not in hint list, something is wrong";
      return;
   }

   sths.preferredDBKey = preferDBKey;
   putStoredTxHints(sths);
}

////////
Tx LMDBBlockDatabase::getFullTxCopy(
   uint16_t txIndex, std::shared_ptr<BlockHeader> bhPtr) const
{
   if (bhPtr == nullptr) {
      throw LmdbWrapperException("null bhPtr");
   }

   if (txIndex >= bhPtr->getNumTx()) {
      throw std::range_error("txid > numTx");
   }

   if (blkFolder_.empty()) {
      throw LmdbWrapperException("invalid blkFolder");
   }

   //open block file
   auto path = FileUtils::getBlkFilename(blkFolder_, bhPtr->getBlockFileNum());
   auto fileMap = FileUtils::FileMap(path, false);
   auto getID = [bhPtr] (const BinaryData&)->uint32_t
   {
      return bhPtr->getThisID();
   };

   try {
      auto block = BlockData::deserialize(
         fileMap.ptr() + bhPtr->getOffset(),
         bhPtr->getBlockSize(), bhPtr, getID,
         BlockData::CheckHashes::NoChecks);

      const auto& bctx = block->getTxns()[txIndex];
      BinaryRefReader brr(bctx->data_, bctx->size_);
      Tx tx{brr};
      tx.setTxHeight(bhPtr->getBlockHeight());
      tx.setDupId(bhPtr->getDuplicateID());
      tx.setTxIndex(txIndex);
      return tx;
   } catch (const BtcUtils::BlockDeserializingException&) {
      throw LmdbWrapperException("failed to tx");
   }
}

TxOut LMDBBlockDatabase::getTxOutCopy(
   const BinaryData& ldbKey6B, uint16_t txOutIdx,
   std::shared_ptr<BlockHeader> bhPtr) const
{
   if (ldbKey6B.startsWith(DBUtils::ZCPrefix)) {
      throw std::runtime_error("this is a zc TxOut!");
   }

   BinaryRefReader brr;
   if (getDbType() == ARMORY_DB_TYPE::Super) {
      if (bhPtr == nullptr) {
         throw std::runtime_error("need a valid header ptr");
      }

      BinaryRefReader brr_key(ldbKey6B);
      unsigned block;
      uint8_t dup;
      uint16_t txid;
      DBUtils::readBlkDataKeyNoPrefix(brr_key, block, dup, txid);

      auto key_super = DBUtils::getBlkDataKeyNoPrefix(
         bhPtr->getThisID(), 0xFF, txid, txOutIdx);
      brr = getValueReader(DB_SELECT::STXO, key_super);
      if (brr.empty()) {
         throw std::runtime_error("TxOut key does not exist in STXO DB");
      }

      StoredTxOut stxo;
      stxo.unserializeDBValue(brr.getRawRef());
      auto txout_raw = stxo.getSerializedTxOut();
      return {txout_raw, txout_raw.getSize(), txOutIdx};
   } else {
      BinaryWriter bw(8);
      bw.put_BinaryData(ldbKey6B);
      bw.put_uint16_t(txOutIdx, BE);
      BinaryDataRef ldbKey8 = bw.getDataRef();
      brr = getValueReader(DB_SELECT::STXO, DbPrefix::TXDATA, ldbKey8);
      if (brr.empty()) {
         throw std::runtime_error("TxOut key does not exist in STXO DB");
      }

      brr.advance(2);
      return {brr.getCurrPtr(), brr.getSizeRemaining(), txOutIdx};
   }
}

////////////////////////////////////////////////////////////////////////////////
BinaryData LMDBBlockDatabase::getTxHashForLdbKey(
   BinaryDataRef ldbKey6B, std::shared_ptr<BlockHeader> bhPtr) const
{
   if (!ldbKey6B.startsWith(DBUtils::ZCPrefix)) {
      if (getDbType() != ARMORY_DB_TYPE::Super) {
         auto tx = beginTransaction(DB_SELECT::TXHINTS, LMDB::Mode::ReadOnly);
         BinaryData keyFull(ldbKey6B.getSize() + 1);
         keyFull[0] = (uint8_t)DbPrefix::TXDATA;
         ldbKey6B.copyTo(keyFull.getPtr() + 1, ldbKey6B.getSize());

         BinaryDataRef txData = getValueNoCopy(DB_SELECT::TXHINTS, keyFull);
         if (txData.getSize() >= 36) {
            return txData.getSliceRef(4, 32);
         }
      } else {
         //convert height to id
         unsigned height;
         uint8_t dupId;
         uint16_t txid;
         BinaryRefReader brr(ldbKey6B);

         DBUtils::readBlkDataKeyNoPrefix(brr, height, dupId, txid);
         auto blockId = height;
         if (dupId != 0x7F) {
            if (bhPtr == nullptr) {
               LOGWARN << "null header with block height key";
               return {};
            }
            blockId = bhPtr->getThisID();
         }
         auto id_key = DBUtils::getBlkDataKeyNoPrefix(blockId, 0xFF, txid);

         //get hash & height entry
         auto tx = beginTransaction(DB_SELECT::STXO, LMDB::Mode::ReadOnly);
         auto data = getValueNoCopy(DB_SELECT::STXO, id_key);
         if (data.getSize() <= 32) {
            LOGWARN << "no tx hash entry for this key: " << id_key.toHexStr();
            return {};
         }

         BinaryRefReader brr_result(data);
         return brr_result.get_BinaryData(32);
      }
   }
   return {};
}

////////
bool LMDBBlockDatabase::getStoredHeader(
   StoredHeader& sbh, std::shared_ptr<BlockHeader> bh, bool withTx) const
{
   try {
      if (blkFolder_.empty()) {
         throw LmdbWrapperException("invalid blkFolder");
      }
      //open block file
      auto path = FileUtils::getBlkFilename(blkFolder_, bh->getBlockFileNum());
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

BinaryData LMDBBlockDatabase::getRawBlock(std::shared_ptr<BlockHeader> bh) const
{
   //open block file
   if (blkFolder_.empty()) {
      throw LmdbWrapperException("invalid blkFolder");
   }
   auto path = FileUtils::getBlkFilename(blkFolder_, bh->getBlockFileNum());
   auto fileMap = FileUtils::FileMap(path, false);
   return BinaryData(fileMap.ptr() + bh->getOffset(), bh->getBlockSize());
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredZC(StoredTx& stx, BinaryDataRef zcKey) const
{
   auto dbs = DB_SELECT::ZERO_CONF;

   //only by zcKey
   BinaryData zcDbKey;

   if (zcKey.getSize() == 6) {
      zcDbKey = BinaryData(7);
      uint8_t* ptr = zcDbKey.getPtr();
      ptr[0] = (uint8_t)DbPrefix::ZCDATA;
      memcpy(ptr + 1, zcKey.getPtr(), 6);
   } else {
      zcDbKey = zcKey;
   }

   auto ldbIter = getIterator(dbs);
   if (!ldbIter->seekToExact(zcDbKey)) {
      LOGERR << "BLKDATA DB does not have the requested ZC tx";
      LOGERR << "(" << zcKey.toHexStr() << ")";
      return false;
   }

   size_t nbytes = 0;
   do {
      // Stop if key doesn't start with [PREFIX | ZCkey | TXIDX]
      if (!ldbIter->checkKeyStartsWith(zcDbKey)) {
         break;
      }

      // Read the prefix, height and dup 
      uint16_t txOutIdx;
      BinaryRefReader txKey = ldbIter->getKeyReader();

      // Now actually process the iter value
      if (txKey.getSize()==7) {
         // Get everything else from the iter value
         stx.unserializeDBValue(ldbIter->getValueRef());
         nbytes += stx.dataCopy.getSize();
      } else if(txKey.getSize() == 9) {
         txOutIdx = READ_UINT16_BE(ldbIter->getKeyRef().getSliceRef(7, 2));
         StoredTxOut & stxo = stx.stxoMap[txOutIdx];
         stxo.unserializeDBValue(ldbIter->getValueRef());
         stxo.parentHash = stx.thisHash;
         stxo.txVersion  = stx.version;
         stxo.txOutIndex = txOutIdx;
         nbytes += stxo.dataCopy.getSize();
      } else {
         LOGERR << "Unexpected BLKDATA entry while iterating";
         return false;
      }
   } while (ldbIter->advanceAndRead(DbPrefix::ZCDATA));

   stx.numBytes = stx.haveAllTxOut() ? nbytes : UINT32_MAX;
   return true;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putStoredTxOut(const StoredTxOut& stxo)
{
   BinaryData ldbKey = stxo.getDBKey(false);
   BinaryWriter bw;
   stxo.serializeDBValue(bw);
   putValue(DB_SELECT::STXO, DbPrefix::TXDATA, ldbKey, bw.getDataRef());
}

void LMDBBlockDatabase::putStoredZcTxOut(const StoredTxOut& stxo,
   const BinaryData& zcKey)
{
   BinaryWriter bw;
   stxo.serializeDBValue(bw);
   putValue(DB_SELECT::ZERO_CONF, DbPrefix::ZCDATA, zcKey, bw.getDataRef());
}

////////
bool LMDBBlockDatabase::getStoredTxOut(StoredTxOut& stxo,
   const BinaryData& txHash, uint16_t txoutid) const
{
   if (getDbType() != ARMORY_DB_TYPE::Super) {
      throw LmdbWrapperException("supernode only call");
   }

   auto txKey = getDBKeyForHash(txHash);
   if (txKey.empty()) {
      return false;
   }

   unsigned id;
   uint8_t dup;
   uint16_t txIdx;
   BinaryRefReader brrKey(txKey);
   DBUtils::readBlkDataKeyNoPrefix(brrKey, id, dup, txIdx);

   //grab header
   BinaryWriter bw;
   bw.put_BinaryData(txKey);
   bw.put_uint16_t(txoutid, BE);

   //grab tx
   auto stxo_tx = beginTransaction(DB_SELECT::STXO, LMDB::Mode::ReadOnly);
   auto data = getValueNoCopy(DB_SELECT::STXO, bw.getDataRef());
   if (data.empty()) {
      LOGWARN << "no txout for key: " << txKey.toHexStr();
      return false;
   }

   //convert to stxo
   stxo.unserializeDBValue(data);
   stxo.parentHash = txHash;
   stxo.txIndex = txIdx;
   stxo.txOutIndex = txoutid;
   stxo.isCoinbase = (txIdx == 0);
   return true;
}

bool LMDBBlockDatabase::getStoredTxOut(StoredTxOut& stxo,
   const BinaryData& dbKey) const
{
   if (dbKey.getSize() != 8) {
      LOGERR << "Tried to get StoredTxOut, but the provided key is not of the "
         "proper size. Expect size is 8, this key is: " << dbKey.getSize();
      return false;
   }

   unsigned id;
   uint8_t dup;
   uint16_t txIdx, txoutid;

   BinaryRefReader txout_key(dbKey);
   DBUtils::readBlkDataKeyNoPrefix(txout_key, id, dup, txIdx, txoutid);

   if (getDbType() != ARMORY_DB_TYPE::Super) {
      //Let's look in the db first. Stxos are fetched mostly to spend coins,
      //so there is a high chance we wont need to pull the stxo from the raw
      //block, since fullnode keeps track of all relevant stxos
      auto tx = beginTransaction(DB_SELECT::STXO, LMDB::Mode::ReadOnly);
      BinaryRefReader brr = getValueReader(DB_SELECT::STXO, DbPrefix::TXDATA, dbKey);

      if (!brr.empty()) {
         stxo.blockHeight = id;
         stxo.duplicateID = dup;
         stxo.txIndex = txIdx;
         stxo.txOutIndex = txoutid;
         stxo.unserializeDBValue(brr);
         return true;
      }
   } else {
      throw std::runtime_error("invalid call for supernode db");
   }
   return false;
}

bool LMDBBlockDatabase::getStoredTxOut(StoredTxOut& stxo,
   std::shared_ptr<BlockHeader> header, uint16_t txId, uint16_t txOutId) const
{
   if (getDbType() != ARMORY_DB_TYPE::Super) {
      throw std::runtime_error("supernode only");
   }

   auto txKey = DBUtils::getBlkDataKeyNoPrefix(
      header->getThisID(), 0xFF, txId, txOutId);

   auto stxo_tx = beginTransaction(DB_SELECT::STXO, LMDB::Mode::ReadOnly);
   auto data = getValueNoCopy(DB_SELECT::STXO, txKey);
   if (data.empty()) {
      LOGWARN << "no txout for key: " << txKey.toHexStr();
         return false;
      }

   stxo.unserializeDBValue(data);
   stxo.blockHeight = header->getBlockHeight();
   stxo.duplicateID = header->getDuplicateID();
   stxo.txIndex = txId;
   stxo.txOutIndex = txOutId;
   stxo.isCoinbase = txId == 0;

   //get spentness
   auto spentness_tx = beginTransaction(
      DB_SELECT::SPENTNESS, LMDB::Mode::ReadOnly);
   auto spentnessVal = getValueNoCopy(
      DB_SELECT::SPENTNESS, stxo.getSpentnessKey());
   if (!spentnessVal.empty()) {
      stxo.spentByTxInKey = spentnessVal;
      stxo.spentness = TXOUT_SPENT;
   } else {
      stxo.spentness = TXOUT_UNSPENT;
   }
   return true;
}

bool LMDBBlockDatabase::getStoredTxOut(StoredTxOut& stxo,
   uint32_t blockHeight, uint8_t dupID, uint16_t txIndex,
   uint16_t txOutIndex) const
{
   auto blkKey = DBUtils::getBlkDataKeyNoPrefix(
      blockHeight, dupID, txIndex, txOutIndex);
   return getStoredTxOut(stxo, blkKey);
}

bool LMDBBlockDatabase::getStoredTxOut(StoredTxOut& stxo,
   uint32_t blockHeight, uint16_t txIndex, uint16_t txOutIndex) const
{
   uint8_t dupID = getValidDupIDForHeight(blockHeight);
   if (dupID == UINT8_MAX) {
      LOGERR << "Headers DB has no block at height: " << blockHeight; 
   }
   return getStoredTxOut(stxo, blockHeight, dupID, txIndex, txOutIndex);
}

////////
void LMDBBlockDatabase::getSpentness(StoredTxOut& stxo)
{
   if (getDbType() != ARMORY_DB_TYPE::Super) {
      throw LmdbWrapperException("need to implement this for full node");
   }

   //get spentness
   auto spentness_tx = beginTransaction(DB_SELECT::SPENTNESS, LMDB::Mode::ReadOnly);
   auto spentnessVal = getValueNoCopy(DB_SELECT::SPENTNESS, stxo.getSpentnessKey());
   if (!spentnessVal.empty()) {
      stxo.spentByTxInKey = spentnessVal;
      stxo.spentness = TXOUT_SPENT;
   } else {
      stxo.spentness = TXOUT_UNSPENT;
   }
}

////////
void LMDBBlockDatabase::getUTXOflags(std::map<BinaryData, StoredSubHistory>&
   subSshMap) const
{
   if (getDbType() != ARMORY_DB_TYPE::Super) {
      auto tx = beginTransaction(DB_SELECT::STXO, LMDB::Mode::ReadOnly);
      for (auto& subssh : subSshMap) {
         getUTXOflags(subssh.second);
      }
   } else {
      auto tx = beginTransaction(DB_SELECT::SPENTNESS, LMDB::Mode::ReadOnly);
      for (auto& subssh : subSshMap) {
         getUTXOflags(subssh.second);
      }
   }
}

void LMDBBlockDatabase::getUTXOflags(StoredSubHistory& subssh) const
{
   if (getDbType() == ARMORY_DB_TYPE::Super) {
      getUTXOflags_Super(subssh);
      return;
   }

   for (auto& txioPair : subssh.txioMap) {
      auto& txio = txioPair.second;

      txio.setUTXO(false);
      if (txio.hasTxIn()) {
         continue;
      }

      StoredTxOut stxo;
      auto stxoKey = txio.getDBKeyOfOutput();
      if (!getStoredTxOut(stxo, stxoKey)) {
         continue;
      }
      if (stxo.spentness == TXOUT_UNSPENT) {
         txio.setUTXO(true);
      }
   }
}

void LMDBBlockDatabase::getUTXOflags_Super(StoredSubHistory& subSsh) const
{
   for (auto& txioPair : subSsh.txioMap) {
      auto& txio = txioPair.second;

      txio.setUTXO(false);
      if (txio.hasTxIn()) {
         continue;
      }

      unsigned height;
      uint8_t dupid;
      uint16_t txid, txoid;

      auto txRef = txio.getTxRefOfOutput();
      BinaryRefReader keyReader(txRef.getDBKeyRef());
      DBUtils::readBlkDataKeyNoPrefix(keyReader, height, dupid, txid);
      txoid = txio.getIndexOfOutput();

      auto stxoKey = DBUtils::getBlkDataKeyNoPrefix(
         UINT32_MAX - height, dupid, txid, txoid);
      auto value = getValueNoCopy(DB_SELECT::SPENTNESS, stxoKey.getRef());
      if (value.empty()) {
         txio.setUTXO(true);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::putStoredTxHints(const StoredTxHints& sths)
{
   if (sths.txHashPrefix.empty()) {
      LOGERR << "STHS does have a set prefix, so cannot be put into DB";
      return false;
   }
   putValue(DB_SELECT::TXHINTS, sths.getDBKey(), sths.serializeDBValue());
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTxHints(StoredTxHints& sths,
   BinaryDataRef hashPrefix) const
{
   if(hashPrefix.getSize() < 4) {
      LOGERR << "Cannot get hints without at least 4-byte prefix";
      return false;
   }
   BinaryDataRef prefix4 = hashPrefix.getSliceRef(0,4);
   sths.txHashPrefix = prefix4.copy();

   auto bdr = getValueRef(DB_SELECT::TXHINTS, DbPrefix::TXHINTS, prefix4);
   if (!bdr.empty()) {
      sths.unserializeDBValue(bdr);
      return true;
   } else {
      sths.dbKeyList.resize(0);
      sths.preferredDBKey.resize(0);
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::putStoredHeadHgtList(const StoredHeadHgtList& hhl)
{
   if (hhl.height == UINT32_MAX) {
      LOGERR << "HHL does not have a valid height to be put into DB";
      return false;
   }
   putValue(DB_SELECT::HEADERS, hhl.getDBKey(), hhl.serializeDBValue());
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredHeadHgtList(
   StoredHeadHgtList & hhl, uint32_t height) const
{
   BinaryData ldbKey = WRITE_UINT32_BE(height);
   BinaryDataRef bdr = getValueRef(DB_SELECT::HEADERS, DbPrefix::HEADHGT, ldbKey);

   hhl.height = height;
   if (!bdr.empty()) {
      hhl.unserializeDBValue(bdr);
      return true;
   } else {
      hhl.preferredDup = UINT8_MAX;
      hhl.dupAndHashList.resize(0);
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
TxRef LMDBBlockDatabase::getTxRef(BinaryDataRef txHash)
{
   auto key = getDBKeyForHash(txHash);
   if (key.empty()) {
      throw std::runtime_error("no tx for this hash");
   }
   return TxRef{key.getRef()};
}

////////////////////////////////////////////////////////////////////////////////
TxRef LMDBBlockDatabase::getTxRef(BinaryData hgtx, uint16_t txIndex)
{
   BinaryWriter bw;
   bw.put_BinaryData(hgtx);
   bw.put_uint16_t(txIndex, BE);
   return TxRef(bw.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
TxRef LMDBBlockDatabase::getTxRef(uint32_t hgt, uint8_t dup, uint16_t txIndex)
{
   BinaryWriter bw;
   bw.put_BinaryData(DBUtils::heightAndDupToHgtx(hgt, dup));
   bw.put_uint16_t(txIndex, BE);
   return TxRef(bw.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::markBlockHeaderValid(BinaryDataRef headHash)
{
   BinaryRefReader brr = getValueReader(DB_SELECT::HEADERS, DbPrefix::HEADHASH, headHash);
   if (brr.empty()) {
      LOGERR << "Invalid header hash: " << headHash.copy().copySwapEndian().toHexStr();
      return false;
   }
   brr.advance(HEADER_SIZE);
   BinaryData hgtx   = brr.get_BinaryData(4);
   uint32_t   height = DBUtils::hgtxToHeight(hgtx);
   uint8_t    dup    = DBUtils::hgtxToDupID(hgtx);
   return markBlockHeaderValid(height, dup);
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::markBlockHeaderValid(uint32_t height, uint8_t dup)
{
   StoredHeadHgtList hhl;
   getStoredHeadHgtList(hhl, height);
   if (hhl.preferredDup == dup) {
      return true;
   }

   bool hasEntry = false;
   for (const auto& dah : hhl.dupAndHashList) {
      if (dah.first == dup) {
         hasEntry = true;
      }
   }

   if (hasEntry) {
      hhl.setPreferredDupID(dup);
      putStoredHeadHgtList(hhl);
      setValidDupIDForHeight(height, dup);
      return true;
   } else {
      LOGERR << "Header was not found header-height list";
      return false;
   }
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

   auto tx = beginTransaction(db, LMDB::Mode::ReadOnly);

   KVLIST outList;
   outList.reserve(100);

   auto ldbIter = getIterator(db);
   ldbIter->seekToFirst();
   for (ldbIter->seekToFirst(); ldbIter->isValid(); ldbIter->advanceAndRead()) {
      size_t last = outList.size();
      outList.push_back(std::pair<BinaryData, BinaryData>());
      outList[last].first  = ldbIter->getKey();
      outList[last].second = ldbIter->getValue();
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

   for (uint32_t i=0; i<dbList.size(); i++) {
      std::cout << "   \"" << dbList[i].first.toHexStr() << "\"  ";
      std::cout << "   \"" << dbList[i].second.toHexStr() << "\"  " << std::endl;
   }
}

////////////////////////////////////////////////////////////////////////////////
std::map<uint32_t, uint32_t> LMDBBlockDatabase::getSSHSummary(
   BinaryDataRef scrAddrStr)
{
   std::map<uint32_t, uint32_t> SSHsummary;
   auto ldbIter = getIterator(DB_SELECT::SSH);
   if (!ldbIter->seekToExact(DbPrefix::SCRIPT, scrAddrStr)) {
      return SSHsummary;
   }

   StoredScriptHistory ssh;
   BinaryDataRef sshKey = ldbIter->getKeyRef();
   ssh.unserializeDBKey(sshKey, true);
   ssh.unserializeDBValue(ldbIter->getValueReader());
   return ssh.subsshSummary;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::resetHistoryForAddressVector(
   const std::vector<BinaryData>& addrVec)
{
   auto tx = beginTransaction(DB_SELECT::SSH, LMDB::Mode::ReadWrite);
   for (auto& addr : addrVec) {
      if (addr.empty()) {
         continue;
      }

      BinaryData addrWithPrefix;
      if (addr.getPtr()[0] == (uint8_t)DbPrefix::SCRIPT) {
         addrWithPrefix = addr;
      } else {
         addrWithPrefix = WRITE_UINT8_LE((uint8_t)DbPrefix::SCRIPT);
         addrWithPrefix.append(addr);
      }
      deleteValue(DB_SELECT::SSH, addrWithPrefix.getRef());
   }
}

/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::resetSSHdb()
{
   if (getDbType() == ARMORY_DB_TYPE::Super) {
      resetSSHdb_Super();
      return;
   }

   std::map<BinaryData, int> sshKeys;
   {
      //gather keys
      auto tx = beginTransaction(DB_SELECT::SSH, LMDB::Mode::ReadOnly);
      auto dbIter = getIterator(DB_SELECT::SSH);

      while (dbIter->advanceAndRead(DbPrefix::SCRIPT)) {
         StoredScriptHistory ssh;
         ssh.unserializeDBValue(dbIter->getValueRef());
         sshKeys[dbIter->getKeyRef()] = ssh.scanHeight;
      }
   }

   {
      //reset them
      auto tx = beginTransaction(DB_SELECT::SSH, LMDB::Mode::ReadWrite);
      for (auto& sshkey : sshKeys) {
         StoredScriptHistory ssh;
         BinaryWriter bw;
         ssh.scanHeight = sshkey.second;
         ssh.serializeDBValue(bw, ARMORY_DB_TYPE::Full);
         putValue(DB_SELECT::SSH, sshkey.first.getRef(), bw.getDataRef());
      }

      auto sdbi = getStoredDBInfo(DB_SELECT::SSH, 0);
      sdbi.topBlkHgt = 0;
      sdbi.topScannedBlkHash = BtcUtils::EmptyHash;
      putStoredDBInfo(DB_SELECT::SSH, sdbi, 0);
   }
}

/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::resetSSHdb_Super()
{
   //delete checkpoint and ssh db
   {
      auto db_ssh = getDbPtr(DB_SELECT::SSH);
      closeDatabases();
      db_ssh->eraseOnDisk();
   }
   openDatabases(DatabaseContainer::baseDir_);
}

/////////////////////////////////////////////////////////////////////////////
TxFilterPoolWriter LMDBBlockDatabase::getFilterPoolWriter(
   uint32_t fileNum) const
{
   auto key = DBUtils::getFilterPoolKey(fileNum);
   auto db = DB_SELECT::TXFILTERS;
   auto tx = beginTransaction(db, LMDB::Mode::ReadOnly);
   auto val = getValueNoCopy(DB_SELECT::TXFILTERS, key);
   try {
      return {val};
   } catch (const TxFilterException&) {
      //return empty pool if data is missing
      return {};
   }
}

/////////////////////////////////////////////////////////////////////////////
BinaryDataRef LMDBBlockDatabase::getFilterPoolDataRef(uint32_t fileNum) const
{
   auto key = DBUtils::getFilterPoolKey(fileNum);
   auto db = DB_SELECT::TXFILTERS;
   auto tx = beginTransaction(db, LMDB::Mode::ReadOnly);
   auto val = getValueNoCopy(DB_SELECT::TXFILTERS, key);
   if (val.empty()) {
      return {};
   }
   return val;
}

/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putFilterPoolForFileNum(
   uint32_t fileNum, const TxFilterPoolWriter& pool)
{
   BinaryWriter bw;
   pool.serialize(bw);
   const auto& data = bw.getData();
   auto key = DBUtils::getFilterPoolKey(fileNum);

   //update on disk
   auto db = DB_SELECT::TXFILTERS;
   auto tx = beginTransaction(db, LMDB::Mode::ReadWrite);
   putValue(DB_SELECT::TXFILTERS, key, data);
}

/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putMissingHashes(
   const std::set<BinaryData>& hashSet, uint32_t id)
{
   auto missingHashesKey = DBUtils::getMissingHashesKey(id);

   BinaryWriter bw;
   bw.put_uint32_t(hashSet.size());
   for (auto& hash : hashSet) {
      bw.put_BinaryData(hash);
   }
   putValue(DB_SELECT::TXFILTERS, missingHashesKey.getRef(), bw.getDataRef());
}

/////////////////////////////////////////////////////////////////////////////
std::set<BinaryData> LMDBBlockDatabase::getMissingHashes(uint32_t id) const
{
   auto missingHashesKey = DBUtils::getMissingHashesKey(id);
   auto tx = beginTransaction(DB_SELECT::TXFILTERS, LMDB::Mode::ReadOnly);
   auto rawMissingHashes = getValueNoCopy(DB_SELECT::TXFILTERS, missingHashesKey);

   BinaryRefReader brr(rawMissingHashes);
   if (brr.getSizeRemaining() < 4) {
      throw LmdbWrapperException("invalid missing hashes entry");
   }
   std::set<BinaryData> missingHashesSet;

   auto len = brr.get_uint32_t();
   if (rawMissingHashes.getSize() != len * 32 + 4) {
      throw LmdbWrapperException("missing hashes entry size mismatch");
   }
   for (uint32_t i = 0; i < len; i++) {
      missingHashesSet.emplace(std::move(brr.get_BinaryData(32)));
   }
   return missingHashesSet;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putStoredDBInfo(DB_SELECT db,
   const StoredDBInfo& sdbi, uint32_t id)
{
   auto dbPtr = getDbPtr(db);
   dbPtr->putStoredDBInfo(sdbi, id);
}

////////////////////////////////////////////////////////////////////////////////
StoredDBInfo LMDBBlockDatabase::getStoredDBInfo(DB_SELECT db, uint32_t id)
{
   auto dbPtr = getDbPtr(db);
   return dbPtr->getStoredDBInfo(id);
}

////////////////////////////////////////////////////////////////////////////////
StoredDBInfo LMDBBlockDatabase::openDB(DB_SELECT db)
{
   auto dbPtr = getDbPtr(db);
   return dbPtr->open();
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::closeDB(DB_SELECT db)
{
   auto dbPtr = getDbPtr(db);
   dbPtr->close();
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::loadHeightToIdMap()
{
   auto tx = beginTransaction(DB_SELECT::SUBSSH_META, LMDB::Mode::ReadOnly);
   auto dbIter = getIterator(DB_SELECT::SUBSSH_META);

   std::map<unsigned, unsigned> heightToIdMap;
   BinaryWriter bw_key(8);
   bw_key.put_uint64_t(0);
   if (!dbIter->seekToExact(bw_key.getDataRef())) {
      return;
   }

   do {
      auto brr_value = dbIter->getValueReader();
      auto height = brr_value.get_uint32_t();

      auto brr_key = dbIter->getKeyReader();
      auto ctr = brr_key.get_uint32_t(BE);

      heightToIdMap.emplace(height, ctr);
      ++ctr;
   } while (dbIter->advanceAndRead());
   heightToBatchId_.update(std::move(heightToIdMap));
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getOrSetFlaggedBlockFile(uint32_t fileNum)
{
   BinaryWriter bw_key(5);
   bw_key.put_uint8_t((uint8_t)DbPrefix::FLAGGED_BLOCKFILES);
   bw_key.put_uint32_t(fileNum, BE);

   auto tx = beginTransaction(DB_SELECT::HEADERS, LMDB::Mode::ReadWrite);
   auto dbIter = getIterator(DB_SELECT::HEADERS);
   if (!dbIter->seekToExact(bw_key.getDataRef())) {
      //missing this file num, add it
      putValue(DB_SELECT::HEADERS, bw_key.getDataRef(), {});
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
   auto dbIter = getIterator(DB_SELECT::HEADERS);
   if (!dbIter->seekToStartsWith(DbPrefix::FLAGGED_BLOCKFILES)) {
      return {};
   }

   std::vector<uint32_t> result;
   do {
      if (!dbIter->verifyPrefix(DbPrefix::FLAGGED_BLOCKFILES)) {
         break;
      }

      auto keyReader = dbIter->getKeyReader();
      result.emplace_back(keyReader.get_uint32_t(BE));
   } while (dbIter->advanceAndRead());
   return result;
}

void LMDBBlockDatabase::clearFlaggedFileNums()
{
   auto tx = beginTransaction(DB_SELECT::HEADERS, LMDB::Mode::ReadWrite);
   auto dbIter = getIterator(DB_SELECT::HEADERS);

   if (!dbIter->seekToStartsWith(DbPrefix::FLAGGED_BLOCKFILES)) {
      return;
   }

   std::set<BinaryData> keysToDelete;
   do {
      if (!dbIter->verifyPrefix(DbPrefix::FLAGGED_BLOCKFILES)) {
         break;
      }
      keysToDelete.emplace(dbIter->getKey());
   } while (dbIter->advanceAndRead());

   for (const auto& key : keysToDelete) {
      deleteValue(DB_SELECT::HEADERS, key);
   }
}

////////
void LMDBBlockDatabase::updateHeightToIdMap(std::map<unsigned, unsigned>& idmap)
{
   heightToBatchId_.update(std::move(idmap));
}

////////////////////////////////////////////////////////////////////////////////
// DatabaseContainer
std::filesystem::path DatabaseContainer::baseDir_;
BinaryData DatabaseContainer::magicBytes_;

DatabaseContainer::DatabaseContainer(DB_SELECT dbSelect) :
   dbSelect_(dbSelect)
{}

DatabaseContainer::~DatabaseContainer()
{}

////////
std::filesystem::path DatabaseContainer::getDbPath(DB_SELECT db)
{
   return getDbPath(getDbName(db));
}

std::filesystem::path DatabaseContainer::getDbPath(const std::string& dbName)
{
   return baseDir_ / dbName;
}

std::string DatabaseContainer::getDbName(DB_SELECT db)
{
   switch (db)
   {
   case DB_SELECT::HEADERS:
      return "headers";

   case DB_SELECT::BLKDATA:
      return "blkdata";

   case DB_SELECT::HISTORY:
      return "history";

   case DB_SELECT::TXHINTS:
      return "txhints";

   case DB_SELECT::SSH:
      return "ssh";

   case DB_SELECT::SUBSSH:
      return "subssh";

   case DB_SELECT::SUBSSH_META:
      return "subssh_meta";

   case DB_SELECT::STXO:
      return "stxo";

   case DB_SELECT::ZERO_CONF:
      return "zeroconf";

   case DB_SELECT::TXFILTERS:
      return "txfilters";

   case DB_SELECT::SPENTNESS:
      return "spentness";

   default:
      throw LmdbWrapperException("unknown db");
   }
}

////////////////////////////////////////////////////////////////////////////////
// DBPair
DBPair::DBPair(unsigned id) :
   id_(id)
{}

unsigned DBPair::getId() const
{
   return id_;
}

LMDBEnv* DBPair::getEnv()
{
   return &env_;
}

////////
void DBPair::open(const std::filesystem::path& path, const std::string& dbName)
{
   if (isOpen()) {
      return;
   }
   unsigned flags = MDB_NOSYNC | MDB_NOTLS;

   env_.open(path, flags);
   auto map_size = LMDBBlockDatabase::mapSizes_.at(dbName);
   env_.setMapSize(map_size);

   auto tx = beginTransaction(LMDB::Mode::ReadWrite);
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
LMDBEnv::Transaction DBPair::beginTransaction(LMDB::Mode mode)
{
   return LMDBEnv::Transaction(&env_, mode);
}

////////
BinaryDataRef DBPair::getValue(BinaryDataRef key) const
{
   CharacterArrayRef carKey(key.getSize(), key.getPtr());
   auto carData = db_.get_NoCopy(carKey);

   if (carData.len == 0) {
      return {};
   }
   BinaryDataRef data{(uint8_t*)carData.data, carData.len};
   return data;
}

void DBPair::putValue(BinaryDataRef key, BinaryDataRef value)
{
   db_.insert(
      CharacterArrayRef{key.getSize(), key.getPtr()},
      CharacterArrayRef{value.getSize(), value.getPtr()}
   );
}

void DBPair::deleteValue(BinaryDataRef key)
{
   db_.erase(CharacterArrayRef{key.getSize(), key.getPtr()});
}

////////
std::unique_ptr<LDBIter_Single> DBPair::getIterator()
{
   return std::make_unique<LDBIter_Single>(std::move(db_.begin()));
}

////////////////////////////////////////////////////////////////////////////////
// DatabaseContainer_Single
DatabaseContainer_Single::DatabaseContainer_Single(DB_SELECT dbSelect) :
   DatabaseContainer(dbSelect), db_(0)
{}

DatabaseContainer_Single::~DatabaseContainer_Single()
{
   close();
}

void DatabaseContainer_Single::close()
{
   db_.close();
}

StoredDBInfo DatabaseContainer_Single::open()
{
   db_.open(getDbPath(dbSelect_), getDbName(dbSelect_));

   StoredDBInfo sdbi;
   try {
      sdbi = std::move(getStoredDBInfo(0));
   } catch (const std::runtime_error&) {
      // If DB didn't exist yet (dbinfo key is empty), seed it
      auto tx = db_.beginTransaction(LMDB::Mode::ReadWrite);

      sdbi.magic = magicBytes_;
      sdbi.metaHash = BtcUtils::EmptyHash;
      sdbi.topBlkHgt = 0;
      sdbi.armoryType = Config::DBSettings::getDbType();
      putStoredDBInfo(sdbi, 0);
   }
   return sdbi;
}

////////
void DatabaseContainer_Single::eraseOnDisk()
{
   close();
   auto dbPath = getDbPath(dbSelect_);
   std::filesystem::remove(dbPath);

   dbPath.append("-lock");
   std::filesystem::remove(dbPath);
}

void DatabaseContainer_Single::putStoredDBInfo(
   const StoredDBInfo& sdbi, uint32_t id)
{
   SCOPED_TIMER("putStoredDBInfo");
   if (!sdbi.isInitialized()) {
      throw LmdbWrapperException("tried to write uninitiliazed sdbi");
   }
   BinaryWriter bw;
   sdbi.serializeDBValue(bw);
   putValue(StoredDBInfo::getDBKey(id), bw.getDataRef());
}

StoredDBInfo DatabaseContainer_Single::getStoredDBInfo(uint32_t id)
{
   SCOPED_TIMER("getStoredDBInfo");
   auto tx = db_.beginTransaction(LMDB::Mode::ReadOnly);
   auto key = StoredDBInfo::getDBKey(id);
   BinaryRefReader brr(getValue(key.getRef()));

   if (brr.empty()) {
      throw LmdbWrapperException("no sdbi at this key");
   }
   StoredDBInfo sdbi;
   sdbi.unserializeDBValue(brr);
   return sdbi;
}

////////
BinaryDataRef DatabaseContainer_Single::getValue(BinaryDataRef key) const
{
   return db_.getValue(key);
}

void DatabaseContainer_Single::putValue(
   BinaryDataRef key,
   BinaryDataRef value)
{
   db_.putValue(key, value);
}

void DatabaseContainer_Single::deleteValue(BinaryDataRef key)
{
   db_.deleteValue(key);
}

////////
std::unique_ptr<DbTransaction> DatabaseContainer_Single::beginTransaction(
   LMDB::Mode mode) const
{
   return std::make_unique<DbTransaction_Single>(
      std::move(db_.beginTransaction(mode)));
}

std::unique_ptr<LDBIter> DatabaseContainer_Single::getIterator()
{
   return db_.getIterator();
}

////////////////////////////////////////////////////////////////////////////////
// ShardFilter
ShardFilter::~ShardFilter()
{}

BinaryData ShardFilter::getDbKey()
{
   return WRITE_UINT32_BE(SHARD_FILTER_DBKEY);
}

std::unique_ptr<ShardFilter> ShardFilter::deserialize(BinaryDataRef dataRef)
{
   BinaryRefReader brr(dataRef);
   auto type = brr.get_uint8_t();
   switch (type)
   {
      case ShardFilterType_ScrAddr:
         return ShardFilter_ScrAddr::deserialize(dataRef);

      case ShardFilterType_Spentness:
         return ShardFilter_Spentness::deserialize(dataRef);

      default:
         throw FilterException("unexpected shard filter type");
   }
}

////////////////////////////////////////////////////////////////////////////////
// ShardFilter_ScrAddr
ShardFilter_ScrAddr::ShardFilter_ScrAddr(unsigned step) :
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

////////
BinaryData ShardFilter_ScrAddr::serialize() const
{
   BinaryWriter bw;
   bw.put_uint8_t(ShardFilterType_ScrAddr);
   bw.put_uint32_t(step_);

   return bw.getData();
}

std::unique_ptr<ShardFilter> ShardFilter_ScrAddr::deserialize(
   BinaryDataRef dataRef)
{
   BinaryRefReader brr(dataRef);
   auto type = brr.get_uint8_t();
   if (type != (uint8_t)ShardFilterType_ScrAddr) {
      throw FilterException("shard filter type mismatch");
   }
   auto step = brr.get_uint32_t();
   return std::make_unique<ShardFilter_ScrAddr>(step);
}

////////
unsigned ShardFilter_ScrAddr::keyToId(BinaryDataRef keyRef) const
{
   auto size = keyRef.getSize();
   if (size < 4) {
      throw FilterException("key is too short for scrAddr shard filter");
   }
   BinaryRefReader brr(keyRef);
   brr.advance(size - 4);
   auto height = DBUtils::hgtxToHeight(brr.get_BinaryDataRef(4));

   if (height >= thresholdValue_) {
      auto diff = height - thresholdValue_;
      return thresholdId_ + (diff / step_);
   } else {
      //id = exp((height/50k - 4)*1.6)
      auto val = (float(height) / 50000.0f - 4.0f) * 1.6f;
      return expl(val);
   }
}

unsigned ShardFilter_ScrAddr::getHeightForId(unsigned id) const
{
   if (id == 0) {
      return 0;
   } else if (id <= thresholdId_) {
      return unsigned((logf(id) / 1.6f + 4.0f) * 50000.0f);
   } else {
      return thresholdValue_ + (id - thresholdId_) * step_;
   }
}

////////////////////////////////////////////////////////////////////////////////
// ShardFilter_Spentness
ShardFilter_Spentness::ShardFilter_Spentness(unsigned step) :
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

////////
BinaryData ShardFilter_Spentness::serialize() const
{
   BinaryWriter bw;
   bw.put_uint8_t(ShardFilterType_Spentness);
   bw.put_uint32_t(step_);
   return bw.getData();
}

std::unique_ptr<ShardFilter> ShardFilter_Spentness::deserialize(
   BinaryDataRef dataRef)
{
   BinaryRefReader brr(dataRef);
   auto type = brr.get_uint8_t();
   if (type != (uint8_t)ShardFilterType_Spentness) {
      throw FilterException("shard filter type mismatch");
   }
   auto step = brr.get_uint32_t();
   return std::make_unique<ShardFilter_Spentness>(step);
}

////////
unsigned ShardFilter_Spentness::keyToId(BinaryDataRef keyRef) const
{
   auto size = keyRef.getSize();
   if (size < 4) {
      throw FilterException("key is too short for scrAddr shard filter");
   }
   BinaryRefReader brr(keyRef);
   auto height = DBUtils::hgtxToHeight(brr.get_BinaryDataRef(4));

   if (height >= thresholdValue_) {
      auto diff = height - thresholdValue_;
      return thresholdId_ + (diff / step_);
   } else {
      //id = exp((height/50k - 4))
      auto val = (float(height) / 50000.0f - 4.0f);
      return expl(val);
   }
}

unsigned ShardFilter_Spentness::getHeightForId(unsigned id) const
{
   if (id == 0) {
      return 0;
   } else if (id <= thresholdId_) {
      return unsigned((logf(id) + 4.0f) * 50000.0f);
   } else {
      return thresholdValue_ + (id - thresholdId_) * step_;
   }
}

////////////////////////////////////////////////////////////////////////////////
// DbTransaction
DbTransaction::DbTransaction()
{}

DbTransaction::~DbTransaction()
{}

DbTransaction_Single::DbTransaction_Single(LMDBEnv::Transaction&& dbtx) :
   dbtx_(std::move(dbtx))
{}
