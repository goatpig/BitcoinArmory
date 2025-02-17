////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <sstream>
#include <map>
#include <list>
#include <vector>
#include <set>
#include "BinaryData.h"
#include "BtcUtils.h"
#include "BlockObj.h"
#include "StoredBlockObj.h"
#include "lmdb_wrapper.h"
#include "txio.h"
#include "BlockDataMap.h"
#include "Blockchain.h"
#include "TxHashFilters.h"

using namespace std;
using namespace Armory::Config;

const set<DB_SELECT> LMDBBlockDatabase::supernodeDBs_({});
const map<string, size_t> LMDBBlockDatabase::mapSizes_ = {
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
////////////////////////////////////////////////////////////////////////////////
////LDBIter
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
LDBIter::~LDBIter()
{}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::isValid(DB_PREFIX dbpref)
{
   if(!isValid())
      return false;

   readIterData();
   if (currKey_.getSize() == 0)
      return false;

   return currKey_.getPtr()[0] == (uint8_t)dbpref;
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::advance(DB_PREFIX prefix)
{
   advance();
   return isValid(prefix);
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::advanceAndRead(void)
{
   if(!advance())
      return false; 
   return readIterData(); 
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::advanceAndRead(DB_PREFIX prefix)
{
   if(!advance(prefix))
      return false; 
   return readIterData(); 
}


////////////////////////////////////////////////////////////////////////////////
BinaryData LDBIter::getKey(void) const
{ 
   if(isDirty_)
   {
      LOGERR << "Returning dirty key ref";
      return BinaryData(0);
   }
   return currKey_;
}
   
////////////////////////////////////////////////////////////////////////////////
BinaryData LDBIter::getValue(void) const
{ 
   if(isDirty_)
   {
      LOGERR << "Returning dirty value ref";
      return BinaryData(0);
   }
   return currValue_;
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef LDBIter::getKeyRef(void) const
{ 
   if(isDirty_)
   {
      LOGERR << "Returning dirty key ref";
      return BinaryDataRef();
   }
   return currKeyReader_.getRawRef();
}
   
////////////////////////////////////////////////////////////////////////////////
BinaryDataRef LDBIter::getValueRef(void) const
{ 
   if(isDirty_)
   {
      LOGERR << "Returning dirty value ref";
      return BinaryDataRef();
   }
   return currValueReader_.getRawRef();
}


////////////////////////////////////////////////////////////////////////////////
BinaryRefReader& LDBIter::getKeyReader(void) const
{ 
   if(isDirty_)
      LOGERR << "Returning dirty key reader";
   return currKeyReader_; 
}

////////////////////////////////////////////////////////////////////////////////
BinaryRefReader& LDBIter::getValueReader(void) const
{ 
   if(isDirty_)
      LOGERR << "Returning dirty value reader";
   return currValueReader_; 
}


////////////////////////////////////////////////////////////////////////////////
bool LDBIter::seekTo(DB_PREFIX pref, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)pref);
   bw.put_BinaryData(key);
   return seekTo(bw.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::seekToExact(DB_PREFIX pref, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)pref);
   bw.put_BinaryData(key);
   return seekToExact(bw.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::seekToStartsWith(BinaryDataRef key)
{
   if(!seekTo(key))
      return false;

   return checkKeyStartsWith(key);

}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::seekToStartsWith(DB_PREFIX prefix)
{
   BinaryWriter bw(1);
   bw.put_uint8_t((uint8_t)prefix);
   if(!seekTo(bw.getDataRef()))
      return false;

   return checkKeyStartsWith(bw.getDataRef());

}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::seekToStartsWith(DB_PREFIX pref, BinaryDataRef key)
{
   if(!seekTo(pref, key))
      return false;

   return checkKeyStartsWith(pref, key);
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::seekToBefore(DB_PREFIX prefix)
{
   BinaryWriter bw(1);
   bw.put_uint8_t((uint8_t)prefix);
   return seekToBefore(bw.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::seekToBefore(DB_PREFIX pref, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)pref);
   bw.put_BinaryData(key);
   return seekToBefore(bw.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::checkKeyExact(BinaryDataRef key)
{
   if(isDirty_ && !readIterData())
      return false;

   return (key==currKeyReader_.getRawRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::checkKeyExact(DB_PREFIX prefix, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key);
   if(isDirty_ && !readIterData())
      return false;

   return (bw.getDataRef()==currKeyReader_.getRawRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::checkKeyStartsWith(BinaryDataRef key)
{
   if(isDirty_ && !readIterData())
      return false;

   return (currKeyReader_.getRawRef().startsWith(key));
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::verifyPrefix(DB_PREFIX prefix, bool advanceReader)
{
   if(isDirty_ && !readIterData())
      return false;

   if(currKeyReader_.getSizeRemaining() < 1)
      return false;

   if(advanceReader)
      return (currKeyReader_.get_uint8_t() == (uint8_t)prefix);
   else
      return (currKeyReader_.getRawRef()[0] == (uint8_t)prefix);
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter::checkKeyStartsWith(DB_PREFIX prefix, BinaryDataRef key)
{
   BinaryWriter bw(key.getSize() + 1);
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key);
   return checkKeyStartsWith(bw.getDataRef());
}
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
/////LDBIter_Single
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool LDBIter_Single::seekTo(BinaryDataRef key)
{
   iter_.seek(CharacterArrayRef(
      key.getSize(), key.getPtr()), LMDB::Iterator::Seek_GE);
   return readIterData();
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter_Single::seekToExact(BinaryDataRef key)
{
   if (!seekTo(key))
      return false;

   return checkKeyExact(key);
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter_Single::seekToBefore(BinaryDataRef key)
{
   iter_.seek(CharacterArrayRef(key.getSize(), key.getPtr()), LMDB::Iterator::Seek_LE);
   return readIterData();
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter_Single::advance(void)
{
   ++iter_;
   isDirty_ = true;
   return isValid();
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter_Single::retreat(void)
{
   --iter_;
   isDirty_ = true;
   return isValid();
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter_Single::readIterData(void)
{
   if (!isValid())
   {
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

////////////////////////////////////////////////////////////////////////////////
bool LDBIter_Single::seekToFirst(void)
{
   iter_.toFirst();
   return readIterData();
}

////////////////////////////////////////////////////////////////////////////////
bool LDBIter_Single::seekToLast(void)
{
   iter_.toLast();
   return readIterData();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
/////LMDBBlockDatabase
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
LMDBBlockDatabase::LMDBBlockDatabase(
   shared_ptr<Blockchain> bcPtr, const std::filesystem::path& blkFolder) :
   blockchainPtr_(bcPtr), blkFolder_(blkFolder)
{}

/////////////////////////////////////////////////////////////////////////////
LMDBBlockDatabase::~LMDBBlockDatabase(void)
{
   closeDatabases();
}

/////////////////////////////////////////////////////////////////////////////
// The dbType and pruneType inputs are left blank if you are just going to
// take whatever is the current state of database.  You can choose to
// manually specify them, if you want to throw an error if it's not what you
// were expecting
void LMDBBlockDatabase::openDatabases(const std::filesystem::path& basedir)
{
   LOGINFO << "Opening databases...";
   LOGINFO << "dbmode: " << DBSettings::getDbModeStr();

   DatabaseContainer::baseDir_ = basedir;
   DatabaseContainer::magicBytes_ = BitcoinSettings::getMagicBytes();

   if (!BitcoinSettings::isInitialized())
   {
      LOGERR << " must set magic bytes and genesis block";
      LOGERR << "           before opening databases.";
      throw LmdbWrapperException("magic bytes not set");
   }

   // Just in case this isn't the first time we tried to open it.
   closeDatabases();


   for (int i = 0; i < COUNT; i++)
   {
      DB_SELECT CURRDB = DB_SELECT(i);

      auto iter = dbMap_.find(CURRDB);
      if (iter == dbMap_.end())
      {
         if (getDbType() == ARMORY_DB_SUPER)
         {
            if (supernodeDBs_.find(CURRDB) != supernodeDBs_.end())
               continue;
         }
            
         dbMap_.insert(make_pair(
            CURRDB, make_shared<DatabaseContainer_Single>(CURRDB)));
      }

      StoredDBInfo sdbi = openDB(CURRDB);

      // Check that the magic bytes are correct
      if (BitcoinSettings::getMagicBytes() != sdbi.magic_)
      {
         throw DbErrorMsg("Magic bytes mismatch!  Different blokchain?");
      }

      if (CURRDB == HEADERS)
      {
         if (getDbType() != sdbi.armoryType_)
         {
            LOGERR << "db type mismatch, aborting";
            exit(-2);
         }
      }
   }

   if (getDbType() == ARMORY_DB_SUPER)
   {
      loadHeightToIdMap();
   }
 
   {
      //sanity check: try to open older SDBI version
      auto dbPtr = getDbPtr(HEADERS);
      auto&& tx = dbPtr->beginTransaction(LMDB::ReadOnly);
      BinaryData key;
      key.append(DB_PREFIX_DBINFO);
      auto valueRef = dbPtr->getValue(key.getRef());

      if (valueRef.getSize() != 0)
      {
         //old style db, fail
         LOGERR << "DB version mismatch. Use another dbdir!";
         throw DbErrorMsg("DB version mismatch. Use another dbdir!");
      }
   }

   dbIsOpen_ = true;
}

/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::closeDatabases(void)
{
   for (auto& dbPair : dbMap_)
      dbPair.second->close();
   dbMap_.clear();
   dbIsOpen_ = false;
}

/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::replaceDatabases(
   DB_SELECT db, const string& swap_path)
{
   /*replace a db underlying file with file [swap_path]*/

   auto full_swap_path = DatabaseContainer::getDbPath(swap_path);

   //close db
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

/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::cycleDatabase(DB_SELECT db)
{
   auto dbPtr = getDbPtr(db);
   dbPtr->close();
   dbPtr->open();
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::resetHistoryDatabases(void)
{
   if (getDbType() != ARMORY_DB_SUPER)
   {
      resetSSHdb();

      auto db_subssh = getDbPtr(SUBSSH);
      auto db_hints = getDbPtr(TXHINTS);
      auto db_stxo = getDbPtr(STXO);
      closeDatabases();

      db_subssh->eraseOnDisk();
      db_hints->eraseOnDisk();
      db_stxo->eraseOnDisk();
   }
   else
   {
      auto db_subssh = getDbPtr(SUBSSH);
      auto db_subssh_meta = getDbPtr(SUBSSH_META);
      auto db_ssh = getDbPtr(SSH);
      auto db_spentness = getDbPtr(SPENTNESS);
      closeDatabases();

      db_subssh->eraseOnDisk();
      db_subssh_meta->eraseOnDisk();
      db_ssh->eraseOnDisk();
      db_spentness->eraseOnDisk();
   }
   
   openDatabases(DatabaseContainer::baseDir_);
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::destroyAndResetDatabases(void)
{
   // We want to make sure the database is restarted with the same parameters
   // it was called with originally
   {
      auto dbMap{dbMap_}; //save dbMap because closeDatabases clears it
      closeDatabases();
      for (auto& dbPair : dbMap)
         dbPair.second->eraseOnDisk();
   }
   
   // Reopen the databases with the exact same parameters as before
   // The close & destroy operations shouldn't have changed any of that.
   openDatabases(DatabaseContainer::baseDir_);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData LMDBBlockDatabase::getTopBlockHash() const
{
   return blockchainPtr_->top()->getThisHash();
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
BinaryDataRef LMDBBlockDatabase::getValueRef(DB_SELECT db, 
                                             DB_PREFIX prefix, 
                                             BinaryDataRef key) const
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
                                             DB_SELECT db, 
                                             BinaryDataRef keyWithPrefix) const
{
   return BinaryRefReader(getValueNoCopy(db, keyWithPrefix));
}

/////////////////////////////////////////////////////////////////////////////
// Same as the getValueRef, in that they are only valid until the next get*
// call.  These are convenience methods which basically just save us 
BinaryRefReader LMDBBlockDatabase::getValueReader(
                                             DB_SELECT db, 
                                             DB_PREFIX prefix, 
                                             BinaryDataRef key) const
{

   return BinaryRefReader(getValueRef(db, prefix, key));
}

/////////////////////////////////////////////////////////////////////////////
// Header Key:  returns header hash
// Tx Key:      returns tx hash
// TxOut Key:   returns serialized OutPoint
BinaryData LMDBBlockDatabase::getHashForDBKey(BinaryData dbkey) const
{
   uint32_t hgt;
   uint8_t  dup;
   uint16_t txi; 
   uint16_t txo; 

   size_t sz = dbkey.getSize();
   if(sz < 4 || sz > 9)
   {
      LOGERR << "Invalid DBKey size: " << sz << ", " << dbkey.toHexStr();
      return BinaryData(0);
   }
   
   BinaryRefReader brr(dbkey);
   if(dbkey.getSize() % 2 == 0)
      DBUtils::readBlkDataKeyNoPrefix(brr, hgt, dup, txi, txo);
   else
      DBUtils::readBlkDataKey(brr, hgt, dup, txi, txo);

   return getHashForDBKey(hgt, dup, txi, txo);
}


/////////////////////////////////////////////////////////////////////////////
// Header Key:  returns header hash
// Tx Key:      returns tx hash
// TxOut Key:   returns serialized OutPoint
BinaryData LMDBBlockDatabase::getHashForDBKey(uint32_t hgt,
                                           uint8_t  dup,
                                           uint16_t txi,
                                           uint16_t txo) const
{

   if(txi==UINT16_MAX)
   {
      StoredHeader sbh; 
      getBareHeader(sbh, hgt, dup);
      return sbh.thisHash_;
   }
   else if(txo==UINT16_MAX)
   {
      //this is slow, needs some help
      StoredTx stx;
      getStoredTx(stx, hgt, dup, txi, false);
      return stx.thisHash_;
   }
   else 
   {
      StoredTx stx;
      getStoredTx(stx, hgt, dup, txi, false);
      OutPoint op(stx.thisHash_, txo);
      return op.serialize();
   }
}

/////////////////////////////////////////////////////////////////////////////
BinaryData LMDBBlockDatabase::getDBKeyForHash(const BinaryData& txhash,
   uint8_t expectedDupId) const
{
   if (txhash.getSize() < 4)
   {
      LOGWARN << "txhash is less than 4 bytes long";
      return BinaryData();
   }

   auto hash4 = txhash.getSliceRef(0, 4);

   auto&& txHints = beginTransaction(TXHINTS, LMDB::ReadOnly);
   BinaryRefReader brrHints = getValueRef(TXHINTS, DB_PREFIX_TXHINTS, hash4);

   uint32_t valSize = brrHints.getSize();
   if (valSize < 6)
      return BinaryData();
   uint32_t numHints = (uint32_t)brrHints.get_var_int();

   if (getDbType() != ARMORY_DB_SUPER)
   {
      uint32_t height;
      uint8_t  dup;
      uint16_t txIdx;
      for (uint32_t i = 0; i < numHints; i++)
      {
         BinaryDataRef hint = brrHints.get_BinaryDataRef(6);
         BinaryRefReader brrHint(hint);
         DBUtils::readBlkDataKeyNoPrefix(
            brrHint, height, dup, txIdx);

         if (dup != expectedDupId)
         {
            if (dup != getValidDupIDForHeight(height) && numHints > 1)
               continue;
         }

         auto&& txKey = DBUtils::getBlkDataKey(height, dup, txIdx);
         auto dbVal = getValueNoCopy(TXHINTS, txKey.getRef());
         if (dbVal.getSize() < 36)
            continue;

         auto txHashRef = dbVal.getSliceRef(4, 32);

         if (txHashRef != txhash)
            continue;

         return txKey.getSliceCopy(1, 6);
      }
   }
   else
   {
      BinaryData forkedMatch;
      bool offChainHints = false;
      for (uint32_t i = 0; i < numHints; i++)
      {
         BinaryDataRef hint = brrHints.get_BinaryDataRef(6);

         //check this key is on the main branch
         auto hintRef = hint.getSliceRef(0, 4);
         auto blockId = DBUtils::hgtxToHeight(hintRef);

         if (!isBlockIDOnMainBranch(blockId))
         {
            forkedMatch = hint;
            offChainHints = true;
            continue;
         }

         //check hash matches
         auto&& txhashfromdb = getTxHashForLdbKey(hint);
         if (txhash != txhashfromdb)
            continue;

         return hint;
      }

      if (forkedMatch.getSize() == 0)
      {
         //LOGWARN << "failed to get valid key for txhash with " << numHints << " hints";

         if (brrHints.getSizeRemaining() != 0)
            LOGWARN << " bytes remaining for this hint";

         if (offChainHints)
            LOGWARN << " had off chain hits";
      }

      return forkedMatch;
   }

   return BinaryData();
}

/////////////////////////////////////////////////////////////////////////////
unsigned LMDBBlockDatabase::getHeightForTxHash(
   const BinaryDataRef& hash) const
{
   auto&& dbkey = getDBKeyForHash(hash);
   if (dbkey.empty())
      return UINT32_MAX;
      
   auto hgtx = dbkey.getSliceRef(0, 4);

   if (getDbType() == ARMORY_DB_SUPER)
   {
      auto block_id = DBUtils::hgtxToHeight(hgtx);
      auto header = blockchainPtr_->getHeaderById(block_id);
      return header->getBlockHeight();
   }

   return DBUtils::hgtxToHeight(hgtx);
}

/////////////////////////////////////////////////////////////////////////////
// Put value based on BinaryData key.  If batch writing, pass in the batch
void LMDBBlockDatabase::putValue(DB_SELECT db, 
                                  BinaryDataRef key, 
                                  BinaryDataRef value)
{
   auto dbPtr = getDbPtr(db);
   dbPtr->putValue(key, value);
}

/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putValue(DB_SELECT db, 
                              BinaryData const & key, 
                              BinaryData const & value)
{
   putValue(db, key.getRef(), value.getRef());
}

/////////////////////////////////////////////////////////////////////////////
// Put value based on BinaryData key.  If batch writing, pass in the batch
void LMDBBlockDatabase::putValue(DB_SELECT db, 
                                  DB_PREFIX prefix,
                                  BinaryDataRef key, 
                                  BinaryDataRef value)
{
   BinaryWriter bw;
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key.getPtr(), key.getSize());
   putValue(db, bw.getDataRef(), value);
}

/////////////////////////////////////////////////////////////////////////////
// Delete value based on BinaryData key.  If batch writing, pass in the batch
void LMDBBlockDatabase::deleteValue(DB_SELECT db, 
                                 BinaryDataRef key)                 
{
   auto dbPtr = getDbPtr(db);
   dbPtr->deleteValue(key);
}

/////////////////////////////////////////////////////////////////////////////
// Delete Put value based on BinaryData key.  If batch writing, pass in the batch
void LMDBBlockDatabase::deleteValue(DB_SELECT db, 
                                 DB_PREFIX prefix,
                                 BinaryDataRef key)
{
   BinaryWriter bw;
   bw.put_uint8_t((uint8_t)prefix);
   bw.put_BinaryData(key);
   deleteValue(db, bw.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::fillStoredSubHistory(
   StoredScriptHistory& ssh, unsigned start, unsigned end) const
{
   if (DBSettings::getDbType() == ARMORY_DB_SUPER)
   {
      return fillStoredSubHistory_Super(ssh, start, end);
   }
   else
   {
      auto subsshtx = beginTransaction(SUBSSH, LMDB::ReadOnly);
      auto subsshIter = getIterator(SUBSSH);

      BinaryWriter dbkey_withHgtX;
      dbkey_withHgtX.put_uint8_t(DB_PREFIX_SCRIPT);
      dbkey_withHgtX.put_BinaryData(ssh.uniqueKey_);

      if (start != 0)
      {
         dbkey_withHgtX.put_BinaryData(DBUtils::heightAndDupToHgtx(start, 0));
      }

      if (!subsshIter->seekTo(dbkey_withHgtX.getDataRef()))
         return false;
      // Now start iterating over the sub histories
      map<BinaryData, StoredSubHistory>::iterator iter;
      size_t numTxioRead = 0;
      do
      {
         size_t _sz = subsshIter->getKeyRef().getSize();
         BinaryDataRef keyNoPrefix = subsshIter->getKeyRef().getSliceRef(1, _sz - 1);
         if (!keyNoPrefix.startsWith(ssh.uniqueKey_))
            break;

         pair<BinaryData, StoredSubHistory> keyValPair;
         keyValPair.first = keyNoPrefix.getSliceCopy(_sz - 5, 4);
         keyValPair.second.unserializeDBKey(subsshIter->getKeyRef());

         //iter is at the right ssh, make sure hgtX <= endBlock
         if (keyValPair.second.height_ > end)
            break;

         //skip invalid dupIDs
         if (keyValPair.second.dupID_ !=
            getValidDupIDForHeight(keyValPair.second.height_))
            continue;

         keyValPair.second.unserializeDBValue(subsshIter->getValueReader());
         iter = ssh.subHistMap_.insert(keyValPair).first;
         numTxioRead += iter->second.txioMap_.size();
      } while (subsshIter->advanceAndRead(DB_PREFIX_SCRIPT));

      return true;
   }     
}

////////////////////////////////////////////////////////////////////////////////
unsigned LMDBBlockDatabase::getShardIdForHeight(unsigned height) const
{
   auto hiMap = heightToBatchId_.get();
   if (hiMap->size() == 0)
      return UINT32_MAX;

   auto height_iter = hiMap->lower_bound(height);
   if (height_iter == hiMap->end())
      return hiMap->rbegin()->second;

   if (height_iter->first > height && height_iter != hiMap->begin())
      --height_iter;
   return height_iter->second;
}

////////////////////////////////////////////////////////////////////////////////
unsigned LMDBBlockDatabase::getNextShardIdForHeight(unsigned height) const
{
   auto hiMap = heightToBatchId_.get();
   auto height_iter = hiMap->upper_bound(height);
   if (height_iter == hiMap->end())
      return UINT32_MAX;

   return height_iter->second;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::fillStoredSubHistory_Super(
   StoredScriptHistory& ssh, unsigned start, unsigned end) const
{
   auto dupIdMap = validDupByHeight_.get();
   function<bool(unsigned, uint8_t)> isValidDupId = 
      [dupIdMap](unsigned height, uint8_t dupid)->bool
   {
      auto iter = dupIdMap->find(height);
      if (iter == dupIdMap->end())
         return false;

      return dupid == iter->second;
   };

   auto meta_tx = beginTransaction(SUBSSH_META, LMDB::ReadOnly);

   //convert height range to batch id range
   auto start_id = getShardIdForHeight(start);
   if (start_id == UINT32_MAX)
      return true;
   auto end_id = getNextShardIdForHeight(end);
      
   //prepare for subssh db parsing
   BinaryWriter bwKey(4 + ssh.uniqueKey_.getSize());
   bwKey.put_uint32_t(0);
   bwKey.put_BinaryData(ssh.uniqueKey_);

   auto keyRef = bwKey.getDataRef();
   auto ptr = (uint8_t*)keyRef.getPtr();

   //get subssh summary iterator positioned at <= start_id
   auto ssh_lower_bound = ssh.subsshSummary_.lower_bound(start_id);
   if (ssh_lower_bound == ssh.subsshSummary_.end())
      return true;
   if (ssh_lower_bound->first > start_id &&
       ssh_lower_bound != ssh.subsshSummary_.begin())
      --ssh_lower_bound;

   //grab db iterator
   auto subsshtx = beginTransaction(SUBSSH, LMDB::ReadOnly);
   auto dbIter = getIterator(SUBSSH);

   while(ssh_lower_bound != ssh.subsshSummary_.end())
   {
      //break if iterator is past end_id
      if (ssh_lower_bound->first > end_id)
         break;

      //grab meta entry for batch id
      BinaryWriter bw_meta(8);
      bw_meta.put_uint32_t(ssh_lower_bound->first, BE);
      bw_meta.put_uint32_t(0);
      auto meta_value = getValueNoCopy(SUBSSH_META, bw_meta.getDataRef());
      if(meta_value.getSize() == 0)
      {
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
      if (!dbIter->seekToExact(keyRef))
      {
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
void LMDBBlockDatabase::putStoredScriptHistorySummary(StoredScriptHistory & ssh)
{
   SCOPED_TIMER("putStoredScriptHistory");
   if (!ssh.isInitialized())
   {
      LOGERR << "Trying to put uninitialized ssh into DB";
      return;
   }

   putValue(SSH, ssh.getDBKey(), serializeDBValue(ssh, getDbType()));
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredScriptHistorySummary(StoredScriptHistory & ssh,
   BinaryDataRef scrAddr) const
{
   ssh.clear();
   auto tx = beginTransaction(SSH, LMDB::ReadOnly);
   auto ldbIter = getIterator(SSH);
   bool has = false;

   if (ldbIter->seekToExact(DB_PREFIX_SCRIPT, scrAddr)) {
      ssh.unserializeDBKey(ldbIter->getKeyRef());
      ssh.unserializeDBValue(ldbIter->getValueRef());
      has = true;
   }
   return has;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredScriptHistory( StoredScriptHistory & ssh,
   BinaryDataRef scrAddr, uint32_t startBlock, uint32_t endBlock) const
{
   if (!getStoredScriptHistorySummary(ssh, scrAddr)) {
      return false;
   }

   if (!fillStoredSubHistory(ssh, startBlock, endBlock)) {
      return false;
   }

   //grab UTXO flags
   getUTXOflags(ssh.subHistMap_);
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
   if (armoryDbType() == ARMORY_DB_SUPER)
   {
      LOGERR << "deprecated in supernode";
      throw runtime_error("deprecated in supernode");
   }

   auto&& tx = beginTransaction(SUBSSH, LMDB::ReadOnly);
   auto value = getValueNoCopy(SUBSSH, dbkey);

   if (value.getSize() == 0)
      return false;

   subssh.hgtX_ = dbkey.getSliceRef(-4, 4);
   subssh.unserializeDBValue(value);
   return true;
}


////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::getStoredScriptHistoryByRawScript(
                                             StoredScriptHistory & ssh,
                                             BinaryDataRef script) const
{
   BinaryData uniqueKey = BtcUtils::getTxOutScrAddr(script);
   getStoredScriptHistory(ssh, uniqueKey);
}


/////////////////////////////////////////////////////////////////////////////
// This doesn't actually return a SUBhistory, it grabs it and adds it to the
// regular-ssh object.  This does not affect balance or Txio count.  It's 
// simply filling in data that the ssh may be expected to have.  
bool LMDBBlockDatabase::fetchStoredSubHistory( StoredScriptHistory & ssh,
                                            BinaryData hgtX,
                                            bool createIfDNE,
                                            bool forceReadDB)
{
   auto subIter = ssh.subHistMap_.find(hgtX);
   if (!forceReadDB && ITER_IN_MAP(subIter, ssh.subHistMap_))
   {
      return true;
   }

   BinaryData key = ssh.uniqueKey_ + hgtX; 
   BinaryRefReader brr = getValueReader(BLKDATA, DB_PREFIX_SCRIPT, key);

   StoredSubHistory subssh;
   subssh.uniqueKey_ = ssh.uniqueKey_;
   subssh.hgtX_      = hgtX;

   if(brr.getSize() > 0)
      subssh.unserializeDBValue(brr);
   else if(!createIfDNE)
      return false;

   ssh.mergeSubHistory(subssh);
   
   return true;
}


////////////////////////////////////////////////////////////////////////////////
uint64_t LMDBBlockDatabase::getBalanceForScrAddr(BinaryDataRef scrAddr, bool withMulti)
{
   StoredScriptHistory ssh;
   if(!withMulti)
   {
      getStoredScriptHistorySummary(ssh, scrAddr); 
      return ssh.totalUnspent_;
   }
   else
   {
      getStoredScriptHistory(ssh, scrAddr);
      uint64_t total = ssh.totalUnspent_;
      map<BinaryData, UnspentTxOut> utxoList;
      map<BinaryData, UnspentTxOut>::iterator iter;
      getFullUTXOMapForSSH(ssh, utxoList, true);
      for(iter = utxoList.begin(); iter != utxoList.end(); iter++)
         if(iter->second.isMultisigRef())
            total += iter->second.getValue();
      return total;
   }
}


////////////////////////////////////////////////////////////////////////////////
// We need the block hashes and scripts, which need to be retrieved from the
// DB, which is why this method can't be part of StoredBlockObj.h/.cpp
bool LMDBBlockDatabase::getFullUTXOMapForSSH( 
                                StoredScriptHistory & ssh,
                                map<BinaryData, UnspentTxOut> & mapToFill,
                                bool)
{
   //TODO: deprecate. replace with paged version once new coin control is
   //implemented

   if(!ssh.haveFullHistoryLoaded())
      return false;

   auto&& stxotx = beginTransaction(STXO, LMDB::ReadOnly);
   auto&& hinttx = beginTransaction(TXHINTS, LMDB::ReadOnly);

   {
      for (const auto& ssPair : ssh.subHistMap_)
      {
         const StoredSubHistory & subSSH = ssPair.second;
         for (const auto& txioPair : subSSH.txioMap_)
         {
            const TxIOPair & txio = txioPair.second;
            if (txio.isUTXO())
            {
               BinaryData txoKey = txio.getDBKeyOfOutput();
               BinaryData txKey = txio.getTxRefOfOutput().getDBKey();
               uint16_t txoIdx = txio.getIndexOfOutput();


               StoredTxOut stxo;
               getStoredTxOut(stxo, txoKey);
               BinaryData txHash = getTxHashForLdbKey(txKey);

               mapToFill[txoKey] = UnspentTxOut(
                  txHash,
                  txoIdx,
                  stxo.blockHeight_,
                  txio.getValue(),
                  stxo.getScriptRef());
            }
         }
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
   const function<void(shared_ptr<BlockHeader>, uint32_t, uint8_t)> &callback
)
{
   auto&& tx = beginTransaction(HEADERS, LMDB::ReadOnly);
   auto ldbIter = getIterator(HEADERS);

   if(!ldbIter->seekToStartsWith(DB_PREFIX_HEADHASH))
   {
      LOGWARN << "No headers in DB yet!";
      return;
   }
   
   do
   {
      ldbIter->resetReaders();
      ldbIter->verifyPrefix(DB_PREFIX_HEADHASH);
   
      if(ldbIter->getKeyReader().getSizeRemaining() != 32)
      {
         LOGERR << "How did we get header hash not 32 bytes?";
         continue;
      }

      StoredHeader sbh;
      ldbIter->getKeyReader().get_BinaryData(sbh.thisHash_, 32);

      sbh.unserializeDBValue(HEADERS, ldbIter->getValueRef());

      auto regHead = make_shared<BlockHeader>();

      regHead->unserialize(sbh.dataCopy_);
      regHead->setBlockSize(sbh.numBytes_);
      regHead->setNumTx(sbh.numTx_);

      regHead->setBlockFileNum(sbh.fileID_);
      regHead->setBlockFileOffset(sbh.offset_);
      regHead->setUniqueID(sbh.uniqueID_);

      if (sbh.thisHash_ != regHead->getThisHash())
      {
         LOGWARN << "Corruption detected: block header hash " <<
            sbh.thisHash_.copySwapEndian().toHexStr() << " does not match "
            << regHead->getThisHash().copySwapEndian().toHexStr();
      }
      callback(regHead, sbh.blockHeight_, sbh.duplicateID_);

   } while(ldbIter->advanceAndRead(DB_PREFIX_HEADHASH));
}

////////////////////////////////////////////////////////////////////////////////
uint8_t LMDBBlockDatabase::getValidDupIDForHeight(uint32_t blockHgt) const
{
   auto dupmap = validDupByHeight_.get();

   auto iter = dupmap->find(blockHgt);
   if(iter == dupmap->end())
   {
      LOGERR << "Block height exceeds DupID lookup table";
      return UINT8_MAX;
   }

   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::setValidDupIDForHeight(uint32_t blockHgt, uint8_t dup,
                                               bool overwrite)
{
   if (!overwrite)
   {
      auto dupmap = validDupByHeight_.get();
      auto iter = dupmap->find(blockHgt);

      if(iter != dupmap->end() && iter->second != UINT8_MAX)
         return;
   }

   map<unsigned, uint8_t> updateMap;
   updateMap[blockHgt] = dup;
   validDupByHeight_.update(updateMap);
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::setValidDupIDForHeight(map<unsigned, uint8_t>& dupMap)
{
   validDupByHeight_.update(dupMap);
}

////////////////////////////////////////////////////////////////////////////////
uint8_t LMDBBlockDatabase::getValidDupIDForHeight_fromDB(uint32_t blockHgt)
{

   BinaryData hgt4((uint8_t*)&blockHgt, 4);
   BinaryRefReader brrHgts = getValueReader(HEADERS, DB_PREFIX_HEADHGT, hgt4);

   if(brrHgts.getSize() == 0)
   {
      LOGERR << "Requested header does not exist in DB";
      return false;
   }

   uint8_t lenEntry = 33;
   uint8_t numDup = (uint8_t)(brrHgts.getSize() / lenEntry);
   for(uint8_t i=0; i<numDup; i++)
   {
      uint8_t dup8 = brrHgts.get_uint8_t(); 
      if((dup8 & 0x80) > 0)
         return (dup8 & 0x7f);
   }

   LOGERR << "Requested a header-by-height but none were marked as main";
   return UINT8_MAX;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::isBlockIDOnMainBranch(unsigned blockId) const
{
   auto dupmap = blockIDMainChainMap_.get();

   auto iter = dupmap->find(blockId);
   if (iter == dupmap->end())
   {
      //LOGERR << "no branching entry for blockID " << blockId;
      return false;
   }

   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::setBlockIDBranch(map<unsigned, bool>& idMap)
{
   blockIDMainChainMap_.update(idMap);
}

////////////////////////////////////////////////////////////////////////////////
// Puts bare header into HEADERS DB.  Use "putStoredHeader" to add to both
// (which actually calls this method as the first step)
//
// Returns the duplicateID of the header just inserted
uint8_t LMDBBlockDatabase::putBareHeader(StoredHeader & sbh, bool updateDupID,
   bool updateSDBI)
{
   SCOPED_TIMER("putBareHeader");

   if(!sbh.isInitialized())
   {
      LOGERR << "Attempting to put uninitialized bare header into DB";
      return UINT8_MAX;
   }
   
   if (sbh.blockHeight_ == UINT32_MAX)
   {
      throw LmdbWrapperException("Attempted to put a header with no height");
   }

   // Batch the two operations to make sure they both hit the DB, or neither 
   auto&& tx = beginTransaction(HEADERS, LMDB::ReadWrite);
   auto&& sdbiH = getStoredDBInfo(HEADERS, 0);


   uint32_t height  = sbh.blockHeight_;
   uint8_t sbhDupID = UINT8_MAX;

   // Check if it's already in the height-indexed DB - determine dupID if not
   StoredHeadHgtList hhl;
   getStoredHeadHgtList(hhl, height);

   bool alreadyInHgtDB = false;
   bool needToWriteHHL = false;
   if(hhl.dupAndHashList_.size() == 0)
   {
      sbhDupID = 0;
      hhl.addDupAndHash(0, sbh.thisHash_);
      if(sbh.isMainBranch_)
         hhl.preferredDup_ = 0;
      needToWriteHHL = true;
   }
   else
   {
      int8_t maxDup = -1;
      for(uint8_t i=0; i<hhl.dupAndHashList_.size(); i++)
      {
         uint8_t dup = hhl.dupAndHashList_[i].first;
         maxDup = max(maxDup, (int8_t)dup);
         if(sbh.thisHash_ == hhl.dupAndHashList_[i].second)
         {
            alreadyInHgtDB = true;
            sbhDupID = dup;
            if(hhl.preferredDup_ != dup && sbh.isMainBranch_ && updateDupID)
            {
               // The header was in the head-hgt list, but not preferred
               hhl.preferredDup_ = dup;
               needToWriteHHL = true;
            }
            break;
         }
      }

      if(!alreadyInHgtDB)
      {
         needToWriteHHL = true;
         sbhDupID = maxDup+1;
         hhl.addDupAndHash(sbhDupID, sbh.thisHash_);
         if(sbh.isMainBranch_ && updateDupID)
            hhl.preferredDup_ = sbhDupID;
      }
   }

   sbh.setKeyData(height, sbhDupID);
   

   if(needToWriteHHL)
      putStoredHeadHgtList(hhl);
      
   // Overwrite the existing hash-indexed entry, just in case the dupID was
   // not known when previously written.  
   putValue(HEADERS, DB_PREFIX_HEADHASH, sbh.thisHash_,
      serializeDBValue(sbh, HEADERS, getDbType()));

   // If this block is valid, update quick lookup table, and store it in DBInfo
   if(sbh.isMainBranch_)
   {
      if (updateSDBI)
      {
         sdbiH = move(getStoredDBInfo(HEADERS, 0));
         if (sbh.blockHeight_ >= sdbiH.topBlkHgt_)
         {
            sdbiH.topBlkHgt_ = sbh.blockHeight_;
            putStoredDBInfo(HEADERS, sdbiH, 0);
         }
      }
   }
   return sbhDupID;
}

////////////////////////////////////////////////////////////////////////////////
// "BareHeader" refers to 
bool LMDBBlockDatabase::getBareHeader(StoredHeader & sbh, 
                                   uint32_t blockHgt, 
                                   uint8_t dup) const
{
   SCOPED_TIMER("getBareHeader");

   // Get the hash from the head-hgt list
   StoredHeadHgtList hhl;
   if(!getStoredHeadHgtList(hhl, blockHgt))
   {
      LOGERR << "No headers at height " << blockHgt;
      return false;
   }

   for(uint32_t i=0; i<hhl.dupAndHashList_.size(); i++)
      if(dup==hhl.dupAndHashList_[i].first)
         return getBareHeader(sbh, hhl.dupAndHashList_[i].second);

   return false;

}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getBareHeader(
   StoredHeader & sbh, uint32_t blockHgt) const
{
   SCOPED_TIMER("getBareHeader(duplookup)");

   uint8_t dupID = getValidDupIDForHeight(blockHgt);
   if(dupID == UINT8_MAX)
      LOGERR << "Headers DB has no block at height: " << blockHgt; 

   return getBareHeader(sbh, blockHgt, dupID);
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getBareHeader(
   StoredHeader & sbh, BinaryDataRef headHash) const
{
   SCOPED_TIMER("getBareHeader(hashlookup)");

   BinaryRefReader brr = getValueReader(HEADERS, DB_PREFIX_HEADHASH, headHash);

   if(brr.getSize() == 0)
   {
      LOGERR << "Header found in HHL but hash does not exist in DB";
      return false;
   }
   sbh.unserializeDBValue(HEADERS, brr);
   return true;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::updateStoredTx(StoredTx & stx)
{
   // Add the individual TxOut entries if requested

   uint32_t version = READ_UINT32_LE(stx.dataCopy_.getPtr());

   for (auto& stxo : stx.stxoMap_)
   {      // Make sure all the parameters of the TxOut are set right 
      stxo.second.txVersion_ = version;
      stxo.second.blockHeight_ = stx.blockHeight_;
      stxo.second.duplicateID_ = stx.duplicateID_;
      stxo.second.txIndex_ = stx.txIndex_;
      stxo.second.txOutIndex_ = stxo.first;
      putStoredTxOut(stxo.second);
   }
}


////////////////////////////////////////////////////////////////////////////////
// This assumes that this new tx is "preferred" and will update the list as such
void LMDBBlockDatabase::putStoredTx( StoredTx & stx, bool withTxOut)
{
   if (getDbType() != ARMORY_DB_SUPER)
   {
      LOGERR << "putStoredTx is only meant for Supernode";
      throw LmdbWrapperException("mismatch dbType with putStoredTx");
   }

   SCOPED_TIMER("putStoredTx");
   BinaryData ldbKey = DBUtils::getBlkDataKeyNoPrefix(stx.blockHeight_, 
                                                      stx.duplicateID_, 
                                                      stx.txIndex_);


   // First, check if it's already in the hash-indexed DB
   StoredTxHints sths;
   getStoredTxHints(sths, stx.thisHash_);

   // Check whether the hint already exists in the DB
   bool needToAddTxToHints = true;
   bool needToUpdateHints = false;
   for(uint32_t i=0; i<sths.dbKeyList_.size(); i++)
   {
      if(sths.dbKeyList_[i] == ldbKey)
      {
         needToAddTxToHints = false;
         needToUpdateHints = (sths.preferredDBKey_!=ldbKey);
         sths.preferredDBKey_ = ldbKey;
         break;
      }
   }

   // Add it to the hint list if needed
   if(needToAddTxToHints)
   {
      sths.dbKeyList_.push_back(ldbKey);
      sths.preferredDBKey_ = ldbKey;
   }

   if(needToAddTxToHints || needToUpdateHints)
      putStoredTxHints(sths);

   // Now add the base Tx entry in the BLKDATA DB.
   BinaryWriter bw;
   stx.serializeDBValue(bw, getDbType());
   putValue(BLKDATA, DB_PREFIX_TXDATA, ldbKey, bw.getDataRef());


   // Add the individual TxOut entries if requested
   if(withTxOut)
   {
      map<uint16_t, StoredTxOut>::iterator iter;
      for(iter  = stx.stxoMap_.begin();
          iter != stx.stxoMap_.end();
          iter++)
      {
         // Make sure all the parameters of the TxOut are set right 
         iter->second.txVersion_   = READ_UINT32_LE(stx.dataCopy_.getPtr());
         iter->second.blockHeight_ = stx.blockHeight_;
         iter->second.duplicateID_ = stx.duplicateID_;
         iter->second.txIndex_     = stx.txIndex_;
         iter->second.txOutIndex_  = iter->first;
         putStoredTxOut(iter->second);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putStoredZC(StoredTx & stx, const BinaryData& zcKey)
{
   SCOPED_TIMER("putStoredTx");

   DB_SELECT dbs = ZERO_CONF;

   // Now add the base Tx entry in the BLKDATA DB.
   BinaryWriter bw;
   stx.serializeDBValue(bw, getDbType());
   bw.put_uint32_t(stx.unixTime_);
   putValue(dbs, DB_PREFIX_ZCDATA, zcKey, bw.getDataRef());


   // Add the individual TxOut entries
   {
      map<uint16_t, StoredTxOut>::iterator iter;
      for (iter = stx.stxoMap_.begin();
         iter != stx.stxoMap_.end();
         iter++)
      {
         // Make sure all the parameters of the TxOut are set right 
         iter->second.txVersion_ = READ_UINT32_LE(stx.dataCopy_.getPtr());
         //iter->second.blockHeight_ = stx.blockHeight_;
         //iter->second.duplicateID_ = stx.duplicateID_;
         iter->second.txIndex_ = stx.txIndex_;
         iter->second.txOutIndex_ = iter->first;
         BinaryData zcStxoKey(zcKey);
         zcStxoKey.append(WRITE_UINT16_BE(iter->second.txOutIndex_));
         putStoredZcTxOut(iter->second, zcStxoKey);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::updatePreferredTxHint( BinaryDataRef hashOrPrefix,
                                            BinaryData    preferDBKey)
{
   SCOPED_TIMER("updatePreferredTxHint");
   StoredTxHints sths;
   getStoredTxHints(sths, hashOrPrefix);

   if(sths.preferredDBKey_ == preferDBKey)
      return;

   // Check whether the hint already exists in the DB
   bool exists = false;
   for(uint32_t i=0; i<sths.dbKeyList_.size(); i++)
   {
      if(sths.dbKeyList_[i] == preferDBKey)
      {
         exists = true;
         break;
      }
   }

   if(!exists)
   {
      LOGERR << "Key not in hint list, something is wrong";
      return;
   }

   // sths.dbKeyList_.push_back(preferDBKey);

   sths.preferredDBKey_ = preferDBKey;
   putStoredTxHints(sths);

}

////////////////////////////////////////////////////////////////////////////////
Tx LMDBBlockDatabase::getFullTxCopy(BinaryData ldbKey6B) const
{
   unsigned height;
   uint8_t dup;
   uint16_t txid;

   if (ldbKey6B.getSize() == 6) {
      BinaryRefReader brr(ldbKey6B);
      DBUtils::readBlkDataKeyNoPrefix(brr, height, dup, txid);
   } else if (ldbKey6B.getSize() == 7) {
      BinaryRefReader brr(ldbKey6B);
      DBUtils::readBlkDataKey(brr, height, dup, txid);
   } else {
      LOGERR << "invalid key length";
      throw LmdbWrapperException("invalid key length");
   }

   std::shared_ptr<BlockHeader> header;
   if (getDbType() != ARMORY_DB_SUPER || dup != 0x7F) {
      header = blockchainPtr_->getHeaderByHeight(height, dup);
   } else {
      header = blockchainPtr_->getHeaderById(height);
   }
   return getFullTxCopy(txid, header);
}

////////////////////////////////////////////////////////////////////////////////
Tx LMDBBlockDatabase::getFullTxCopy( uint32_t hgt, uint16_t txIndex) const
{
   SCOPED_TIMER("getFullTxCopy");
   uint8_t dup = getValidDupIDForHeight(hgt);
   if(dup == UINT8_MAX)
      LOGERR << "Headers DB has no block at height: " << hgt;

   BinaryData ldbKey = DBUtils::getBlkDataKey(hgt, dup, txIndex);
   return getFullTxCopy(ldbKey);
}

////////////////////////////////////////////////////////////////////////////////
Tx LMDBBlockDatabase::getFullTxCopy(
   uint32_t hgt, uint8_t dup, uint16_t txIndex) const
{
   SCOPED_TIMER("getFullTxCopy");
   BinaryData ldbKey = DBUtils::getBlkDataKey(hgt, dup, txIndex);
   return getFullTxCopy(ldbKey);
}

////////////////////////////////////////////////////////////////////////////////
Tx LMDBBlockDatabase::getFullTxCopy(
   uint16_t txIndex, shared_ptr<BlockHeader> bhPtr) const
{
   if (bhPtr == nullptr) {
      throw LmdbWrapperException("null bhPtr");
   }

   if (txIndex >= bhPtr->getNumTx()) {
      throw range_error("txid > numTx");
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
      return Tx(brr);
   } catch (const BlockDeserializingException&) {
      throw LmdbWrapperException("failed to tx");
   }
}

////////////////////////////////////////////////////////////////////////////////
TxOut LMDBBlockDatabase::getTxOutCopy(
   BinaryData ldbKey6B, uint16_t txOutIdx) const
{
   SCOPED_TIMER("getTxOutCopy");
   BinaryWriter bw(8);
   bw.put_BinaryData(ldbKey6B);
   bw.put_uint16_t(txOutIdx, BE);
   BinaryDataRef ldbKey8 = bw.getDataRef();

   TxOut txoOut;

   BinaryRefReader brr;
   if (ldbKey6B.startsWith(DBUtils::ZeroConfHeader_)) {
      return {};
   }

   if (getDbType() == ARMORY_DB_SUPER) {
      BinaryRefReader brr_key(ldbKey6B);
      unsigned block;
      uint8_t dup;
      uint16_t txid;
      DBUtils::readBlkDataKeyNoPrefix(brr_key, block, dup, txid);

      auto header = blockchainPtr_->getHeaderByHeight(block, dup);
      auto key_super = DBUtils::getBlkDataKeyNoPrefix(
         header->getThisID(), 0xFF, txid, txOutIdx);
      brr = getValueReader(STXO, key_super);

      if (brr.empty()) {
         LOGERR << "TxOut key does not exist in BLKDATA DB";
         return TxOut();
      }

      StoredTxOut stxo;
      stxo.unserializeDBValue(brr.getRawRef());
      auto&& txout_raw = stxo.getSerializedTxOut();
      txoOut.unserialize(txout_raw, txout_raw.getSize(), txOutIdx);
      return txoOut;
   } else {
      brr = getValueReader(STXO, DB_PREFIX_TXDATA, ldbKey8);
   }

   if (brr.empty()) {
      LOGERR << "TxOut key does not exist in BLKDATA DB";
      return TxOut();
   }

   brr.advance(2);
   txoOut.unserialize_checked(
      brr.getCurrPtr(), brr.getSizeRemaining(), 0, (uint32_t)txOutIdx);
   return txoOut;
}


////////////////////////////////////////////////////////////////////////////////
TxIn LMDBBlockDatabase::getTxInCopy(
   BinaryData ldbKey6B, uint16_t txInIdx) const
{
   SCOPED_TIMER("getTxInCopy");

   if (getDbType() == ARMORY_DB_SUPER) {
      TxIn txiOut;
      BinaryRefReader brr = getValueReader(BLKDATA, DB_PREFIX_TXDATA, ldbKey6B);
      if (brr.empty()) {
         LOGERR << "TxOut key does not exist in BLKDATA DB";
         return TxIn();
      }

      BitUnpacker<uint16_t> bitunpack(brr); // flags
      uint16_t dbVer = bitunpack.getBits(4);
      (void)dbVer;
      uint16_t txVer = bitunpack.getBits(2);
      (void)txVer;
      uint16_t txSer = bitunpack.getBits(4);
      brr.advance(32);

      if (txSer != TX_SER_FULL && txSer != TX_SER_FRAGGED) {
         LOGERR << "Tx not available to retrieve TxIn";
         return TxIn();
      } else {
         bool isFragged = txSer == TX_SER_FRAGGED;
         std::vector<size_t> offsetsIn;
         BtcUtils::StoredTxCalcLength(brr.getCurrPtr(), 
            brr.getSize(), isFragged, &offsetsIn, nullptr, nullptr);
         if ((uint32_t)(offsetsIn.size() - 1) < (uint32_t)(txInIdx + 1)) {
            LOGERR << "Requested TxIn with index greater than numTxIn";
            return TxIn();
         }
         uint8_t const * txInStart = brr.exposeDataPtr() + 34 + offsetsIn[txInIdx];
         uint32_t txInLength = offsetsIn[txInIdx + 1] - offsetsIn[txInIdx];
         TxIn txin;
         txin.unserialize_checked(txInStart, brr.getSize() - 34 - offsetsIn[txInIdx], txInLength, txInIdx);
         return txin;
      }
   } else {
      Tx thisTx = getFullTxCopy(ldbKey6B);
      return thisTx.getTxInCopy(txInIdx);
   }
}

////////////////////////////////////////////////////////////////////////////////
BinaryData LMDBBlockDatabase::getTxHashForLdbKey(BinaryDataRef ldbKey6B) const
{
   if (!ldbKey6B.startsWith(DBUtils::ZeroConfHeader_))
   {
      if (getDbType() != ARMORY_DB_SUPER)
      {
         auto&& tx = beginTransaction(TXHINTS, LMDB::ReadOnly);
         BinaryData keyFull(ldbKey6B.getSize() + 1);
         keyFull[0] = (uint8_t)DB_PREFIX_TXDATA;
         ldbKey6B.copyTo(keyFull.getPtr() + 1, ldbKey6B.getSize());

         BinaryDataRef txData = getValueNoCopy(TXHINTS, keyFull);

         if (txData.getSize() >= 36)
         {
            return txData.getSliceRef(4, 32);
         }
      }
      else
      {
         //convert height to id
         unsigned height;
         uint8_t dup;
         uint16_t txid;
         BinaryRefReader brr(ldbKey6B);

         DBUtils::readBlkDataKeyNoPrefix(brr, height, dup, txid);
         
         unsigned block_id = height;
         if (dup != 0x7F)
         {
            try
            {
               auto header = blockchainPtr_->getHeaderByHeight(height, dup);
               block_id = header->getThisID();
            }
            catch (exception&)
            {
               LOGWARN << "failed to grab header while resolving txhash";
               return BinaryData();
            }
         }

         auto&& id_key = DBUtils::getBlkDataKeyNoPrefix(
            block_id, 0xFF, txid);

         //get hash & height entry
         auto&& tx = beginTransaction(STXO, LMDB::ReadOnly);
         auto data = getValueNoCopy(STXO, id_key);
         if (data.getSize() <= 32)
         {
            LOGWARN << "no tx hash entry for this key: " << id_key.toHexStr();
            return BinaryData();
         }

         BinaryRefReader brr_result(data);
         return brr_result.get_BinaryData(32);
      }
   }

   return BinaryData();
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredHeader(
   StoredHeader& sbh, uint32_t height, uint8_t dupId, bool withTx) const
{
   try
   {
      auto bh = blockchainPtr_->getHeaderByHeight(height, dupId);
      if (bh->getDuplicateID() != dupId)
         throw LmdbWrapperException("invalid dupId");

      return getStoredHeader(sbh, bh, withTx);
   }
   catch (...)
   {
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredHeader(
   StoredHeader& sbh, shared_ptr<BlockHeader> bh, bool withTx) const
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

////////////////////////////////////////////////////////////////////////////////
BinaryData LMDBBlockDatabase::getRawBlock(uint32_t height, uint8_t dupId) const
{
   auto bh = blockchainPtr_->getHeaderByHeight(height, dupId);
   if (bh->getDuplicateID() != dupId) {
      throw LmdbWrapperException("invalid dupId");
   }
   return getRawBlock(bh);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData LMDBBlockDatabase::getRawBlock(shared_ptr<BlockHeader> bh) const
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
bool LMDBBlockDatabase::getStoredTx( StoredTx & stx,
   BinaryData& txHashOrDBKey) const
{
   uint32_t sz = txHashOrDBKey.getSize();
   if(sz == 32) {
      return getStoredTx_byHash(txHashOrDBKey, &stx);
   } else if(sz == 6 || sz == 7) {
      return getStoredTx_byDBKey(stx, txHashOrDBKey);
   } else {
      LOGERR << "Unrecognized input string: " << txHashOrDBKey.toHexStr();
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTx_byDBKey( StoredTx & stx,
   BinaryDataRef dbKey) const
{
   uint32_t hgt;
   uint8_t  dup;
   uint16_t txi;

   BinaryRefReader brrKey(dbKey);

   if(dbKey.getSize() == 6) {
      DBUtils::readBlkDataKeyNoPrefix(brrKey, hgt, dup, txi);
   } else if(dbKey.getSize() == 7) {
      DBUtils::readBlkDataKey(brrKey, hgt, dup, txi);
   } else {
      LOGERR << "Unrecognized input string: " << dbKey.toHexStr();
      return false;
   }

   return getStoredTx(stx, hgt, dup, txi, true);
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredZcTx(StoredTx & stx,
   BinaryDataRef zcKey) const
{
   auto dbs = ZERO_CONF;
   
   //only by zcKey
   BinaryData zcDbKey;

   if (zcKey.getSize() == 6)
   {
      zcDbKey = BinaryData(7);
      uint8_t* ptr = zcDbKey.getPtr();
      ptr[0] = DB_PREFIX_ZCDATA;
      memcpy(ptr + 1, zcKey.getPtr(), 6);
   }
   else
      zcDbKey = zcKey;

   auto ldbIter = getIterator(dbs);
   if (!ldbIter->seekToExact(zcDbKey))
   {
      LOGERR << "BLKDATA DB does not have the requested ZC tx";
      LOGERR << "(" << zcKey.toHexStr() << ")";
      return false;
   }

   size_t nbytes = 0;
   do
   {
      // Stop if key doesn't start with [PREFIX | ZCkey | TXIDX]
      if(!ldbIter->checkKeyStartsWith(zcDbKey))
         break;


      // Read the prefix, height and dup 
      uint16_t txOutIdx;
      BinaryRefReader txKey = ldbIter->getKeyReader();

      // Now actually process the iter value
      if(txKey.getSize()==7)
      {
         // Get everything else from the iter value
         stx.unserializeDBValue(ldbIter->getValueRef());
         nbytes += stx.dataCopy_.getSize();
      }
      else if(txKey.getSize() == 9)
      {
         txOutIdx = READ_UINT16_BE(ldbIter->getKeyRef().getSliceRef(7, 2));
         StoredTxOut & stxo = stx.stxoMap_[txOutIdx];
         stxo.unserializeDBValue(ldbIter->getValueRef());
         stxo.parentHash_ = stx.thisHash_;
         stxo.txVersion_  = stx.version_;
         stxo.txOutIndex_ = txOutIdx;
         nbytes += stxo.dataCopy_.getSize();
      }
      else
      {
         LOGERR << "Unexpected BLKDATA entry while iterating";
         return false;
      }

   } while(ldbIter->advanceAndRead(DB_PREFIX_ZCDATA));


   stx.numBytes_ = stx.haveAllTxOut() ? nbytes : UINT32_MAX;

   return true;
}

////////////////////////////////////////////////////////////////////////////////
// We assume that the first TxHint that matches is correct.  This means that 
// when we mark a transaction/block valid, we need to make sure all the hints
// lists have the correct one in front.  Luckily, the TXHINTS entries are tiny 
// and the number of modifications to make for each reorg is small.
bool LMDBBlockDatabase::getStoredTx_byHash(const BinaryData& txHash,
   StoredTx* stx) const
{
   if (stx == nullptr) {
      return false;
   }

   auto dbKey = getDBKeyForHash(txHash);
   if (dbKey.getSize() < 6) {
      return false;
   }

   if (getDbType() == ARMORY_DB_SUPER) {
      auto hgtx = dbKey.getSliceRef(0, 4);
      if (DBUtils::hgtxToDupID(hgtx) == 0x7F) {
         auto block_id = DBUtils::hgtxToHeight(hgtx);
         auto header = blockchainPtr_->getHeaderById(block_id);

         BinaryWriter bw;
         bw.put_BinaryData(DBUtils::heightAndDupToHgtx(
            header->getBlockHeight(), header->getDuplicateID()));
         bw.put_BinaryDataRef(dbKey.getSliceRef(
            4, dbKey.getSize() - 4));

         dbKey = bw.getData();
      }
   }

   return getStoredTx_byDBKey(*stx, dbKey);
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTx( StoredTx & stx,
   uint32_t blockHeight, uint16_t txIndex, bool withTxOut) const
{
   uint8_t dupID = getValidDupIDForHeight(blockHeight);
   if(dupID == UINT8_MAX) {
      LOGERR << "Headers DB has no block at height: " << blockHeight; 
   }
   return getStoredTx(stx, blockHeight, dupID, txIndex, withTxOut);

}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTx( StoredTx & stx,
   uint32_t blockHeight, uint8_t  dupID, uint16_t txIndex, bool withTxOut) const
{
   SCOPED_TIMER("getStoredTx");

   BinaryData blkDataKey = DBUtils::getBlkDataKey(blockHeight, dupID, txIndex);
   stx.blockHeight_ = blockHeight;
   stx.duplicateID_ = dupID;
   stx.txIndex_     = txIndex;

   auto theTx = getFullTxCopy(blkDataKey);
   stx.createFromTx(theTx, false, withTxOut);
   return true;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putStoredTxOut( StoredTxOut const & stxo)
{
   SCOPED_TIMER("putStoredTx");

   BinaryData ldbKey = stxo.getDBKey(false);
   BinaryData bw = serializeDBValue(stxo);
   putValue(STXO, DB_PREFIX_TXDATA, ldbKey, bw);
}

void LMDBBlockDatabase::putStoredZcTxOut(StoredTxOut const & stxo, 
   const BinaryData& zcKey)
{
   SCOPED_TIMER("putStoredTx");

   BinaryData bw = serializeDBValue(stxo);
   putValue(ZERO_CONF, DB_PREFIX_ZCDATA, zcKey, bw);
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTxOut(
   StoredTxOut & stxo, const BinaryData& txHash, uint16_t txoutid) const
{
   if (getDbType() != ARMORY_DB_SUPER)
      throw LmdbWrapperException("supernode only call");

   auto&& txKey = getDBKeyForHash(txHash);
   if (txKey.getSize() == 0)
      return false;

   unsigned id;
   uint8_t dup;
   uint16_t txIdx;
   BinaryRefReader brrKey(txKey);

   DBUtils::readBlkDataKeyNoPrefix(
      brrKey, id, dup, txIdx);

   //grab header
   auto header = blockchainPtr_->getHeaderById(id);
   BinaryWriter bw;
   bw.put_BinaryData(txKey);
   bw.put_uint16_t(txoutid, BE);

   //grab tx
   auto&& stxo_tx = beginTransaction(STXO, LMDB::ReadOnly);
   auto data = getValueNoCopy(STXO, bw.getDataRef());
   if (data.getSize() == 0)
   {
      LOGWARN << "no txout for key: " << header->getBlockHeight() <<
         "|" << header->getDuplicateID() << "|" << txIdx << "|" << txoutid;
      return false;
   }

   //convert to stxo
   stxo.unserializeDBValue(data);
   stxo.parentHash_ = txHash;
   stxo.blockHeight_ = header->getBlockHeight();
   stxo.duplicateID_ = header->getDuplicateID();
   stxo.txIndex_ = txIdx;
   stxo.txOutIndex_ = txoutid;
   stxo.isCoinbase_ = (txIdx == 0);

   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTxOut(
   StoredTxOut & stxo, const BinaryData& DBkey) const
{
   if (DBkey.getSize() != 8)
   {
      LOGERR << "Tried to get StoredTxOut, but the provided key is not of the "
         "proper size. Expect size is 8, this key is: " << DBkey.getSize();
      return false;
   }

   if (getDbType() != ARMORY_DB_SUPER)
   {
      //Let's look in the db first. Stxos are fetched mostly to spend coins,
      //so there is a high chance we wont need to pull the stxo from the raw
      //block, since fullnode keeps track of all relevant stxos
      auto&& tx = beginTransaction(STXO, LMDB::ReadOnly);
      BinaryRefReader brr = getValueReader(STXO, DB_PREFIX_TXDATA, DBkey);

      if (brr.getSize() > 0)
      {
         stxo.blockHeight_ = DBUtils::hgtxToHeight(DBkey.getSliceRef(0, 4));
         stxo.duplicateID_ = DBUtils::hgtxToDupID(DBkey.getSliceRef(0, 4));
         stxo.txIndex_ = READ_UINT16_BE(DBkey.getSliceRef(4, 2));
         stxo.txOutIndex_ = READ_UINT16_BE(DBkey.getSliceRef(6, 2));

         stxo.unserializeDBValue(brr);
         return true;
      }
   }
   else
   {
      unsigned id;
      uint8_t dup;
      uint16_t txIdx, txoutid;

      BinaryRefReader txout_key(DBkey);
      DBUtils::readBlkDataKeyNoPrefix(txout_key, id, dup, txIdx, txoutid);
      shared_ptr<BlockHeader> header;
      
      BinaryDataRef bdr_key;
      BinaryData bd_tempkey;
      try
      {
         if (dup != 0x7F)
         {
            header = blockchainPtr_->getHeaderByHeight(id, dup);
            bd_tempkey = move(DBUtils::getBlkDataKeyNoPrefix(
               header->getThisID(), 0xFF, txIdx, txoutid));
            bdr_key.setRef(bd_tempkey);
         }
         else
         {
            header = blockchainPtr_->getHeaderById(id);
            bdr_key.setRef(DBkey);
         }
      }
      catch (range_error&)
      {
         LOGWARN << "no header for id " << id;
         return false;
      }
      catch (length_error&)
      {
         //can't get height for this dupId, find block the block hash instead
         auto&& hhTx = beginTransaction(HEADERS, LMDB::ReadWrite);
         
         StoredHeadHgtList hhl;
         if (!getStoredHeadHgtList(hhl, id))
         {
            LOGWARN << "failed to grab hhl list for height " << id;
            return false;
         }

         BinaryDataRef headerHashRef;
         for (auto& dupHashPair : hhl.dupAndHashList_)
         {
            if (dupHashPair.first == dup)
            {
               headerHashRef = dupHashPair.second.getRef();
               break;
            }
         }

         if(headerHashRef.getSize() == 0)
         {
            LOGWARN << "missing dup " << id << " in hhl";
            return false;
         }

         try
         {
            header = blockchainPtr_->getHeaderByHash(headerHashRef);
            bd_tempkey = move(DBUtils::getBlkDataKeyNoPrefix(
               header->getThisID(), 0xFF, txIdx, txoutid));
            bdr_key.setRef(bd_tempkey);
         }
         catch (...)
         {
            LOGWARN << "failed to grab header at " << id << "|" << dup;
            return false;
         }
      }

      auto&& stxo_tx = beginTransaction(STXO, LMDB::ReadOnly);
      auto data = getValueNoCopy(STXO, bdr_key);
      if (data.getSize() == 0)
      {
         LOGWARN << "no txout for key: " << header->getBlockHeight() <<
            "|" << header->getDuplicateID() << "|" << txIdx << "|" << txoutid;
         return false;
      }

      stxo.unserializeDBValue(data);
      stxo.blockHeight_ = header->getBlockHeight();
      stxo.duplicateID_ = header->getDuplicateID();
      stxo.txIndex_ = txIdx;
      stxo.txOutIndex_ = txoutid;
      stxo.isCoinbase_ = (txIdx == 0);

      //get spentness
      auto&& spentness_tx = beginTransaction(SPENTNESS, LMDB::ReadOnly);
      auto spentnessVal = getValueNoCopy(SPENTNESS, stxo.getSpentnessKey());
      if (spentnessVal.getSize() != 0)
      {
         stxo.spentByTxInKey_ = spentnessVal;
         stxo.spentness_ = TXOUT_SPENT;
      }
      else
      {
         stxo.spentness_ = TXOUT_UNSPENT;
      }

      return true;
   }

   return false;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTxOut(      
                              StoredTxOut & stxo,
                              uint32_t blockHeight,
                              uint8_t  dupID,
                              uint16_t txIndex,
                              uint16_t txOutIndex) const
{
   SCOPED_TIMER("getStoredTxOut");
   BinaryData blkKey = DBUtils::getBlkDataKeyNoPrefix(
      blockHeight, dupID, txIndex, txOutIndex);
   return getStoredTxOut(stxo, blkKey);
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTxOut(      
                              StoredTxOut & stxo,
                              uint32_t blockHeight,
                              uint16_t txIndex,
                              uint16_t txOutIndex) const
{
   uint8_t dupID = getValidDupIDForHeight(blockHeight);
   if(dupID == UINT8_MAX)
      LOGERR << "Headers DB has no block at height: " << blockHeight; 

   return getStoredTxOut(stxo, blockHeight, dupID, txIndex, txOutIndex);
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::getSpentness(StoredTxOut& stxo)
{
   if (getDbType() != ARMORY_DB_SUPER)
      throw runtime_error("need to implement this for full node");

   //get spentness
   auto&& spentness_tx = beginTransaction(SPENTNESS, LMDB::ReadOnly);
   auto spentnessVal = getValueNoCopy(SPENTNESS, stxo.getSpentnessKey());
   if (spentnessVal.getSize() != 0)
   {
      stxo.spentByTxInKey_ = spentnessVal;
      stxo.spentness_ = TXOUT_SPENT;
   }
   else
   {
      stxo.spentness_ = TXOUT_UNSPENT;
   }
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::getUTXOflags(map<BinaryData, StoredSubHistory>&
   subSshMap) const
{
   if (getDbType() != ARMORY_DB_SUPER)
   {
      auto tx = beginTransaction(STXO, LMDB::ReadOnly);
      for (auto& subssh : subSshMap)
         getUTXOflags(subssh.second);
   }
   else
   {
      auto tx = beginTransaction(SPENTNESS, LMDB::ReadOnly);
      for (auto& subssh : subSshMap)
         getUTXOflags(subssh.second);
   }
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::getUTXOflags(StoredSubHistory& subssh) const
{
   if (getDbType() == ARMORY_DB_SUPER)
   {
      getUTXOflags_Super(subssh);
      return;
   }

   for (auto& txioPair : subssh.txioMap_)
   {
      auto& txio = txioPair.second;

      txio.setUTXO(false);
      if (txio.hasTxIn())
         continue;

      auto&& stxoKey = txio.getDBKeyOfOutput();

      StoredTxOut stxo;
      if (!getStoredTxOut(stxo, stxoKey))
         continue;

      if (stxo.spentness_ == TXOUT_UNSPENT)
         txio.setUTXO(true);
   }
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::getUTXOflags_Super(StoredSubHistory& subSsh) const
{
   for (auto& txioPair : subSsh.txioMap_)
   {
      auto& txio = txioPair.second;

      txio.setUTXO(false);
      if (txio.hasTxIn())
         continue;

      unsigned height;
      uint8_t dupid;
      uint16_t txid, txoid;

      auto txRef = txio.getTxRefOfOutput();
      BinaryRefReader keyReader(txRef.getDBKeyRef());
      DBUtils::readBlkDataKeyNoPrefix(keyReader, height, dupid, txid);
      txoid = txio.getIndexOfOutput();

      auto&& stxoKey = DBUtils::getBlkDataKeyNoPrefix(
         UINT32_MAX - height, dupid, txid, txoid);
      auto value = getValueNoCopy(SPENTNESS, stxoKey.getRef());
      if (value.getSize() == 0)
         txio.setUTXO(true);
   }
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::putStoredTxHints(StoredTxHints const & sths)
{
   SCOPED_TIMER("putStoredTxHints");
   if(sths.txHashPrefix_.getSize()==0)
   {
      LOGERR << "STHS does have a set prefix, so cannot be put into DB";
      return false;
   }

   putValue(TXHINTS, sths.getDBKey(), sths.serializeDBValue());

   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredTxHints(StoredTxHints & sths, 
                                      BinaryDataRef hashPrefix) const
{
   if(hashPrefix.getSize() < 4)
   {
      LOGERR << "Cannot get hints without at least 4-byte prefix";
      return false;
   }
   BinaryDataRef prefix4 = hashPrefix.getSliceRef(0,4);
   sths.txHashPrefix_ = prefix4.copy();

   BinaryDataRef bdr;
   bdr = getValueRef(TXHINTS, DB_PREFIX_TXHINTS, prefix4);

   if(bdr.getSize() > 0)
   {
      sths.unserializeDBValue(bdr);
      return true;
   }
   else
   {
      sths.dbKeyList_.resize(0);
      sths.preferredDBKey_.resize(0);
      return false;
   }
}


////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::putStoredHeadHgtList(StoredHeadHgtList const & hhl)
{
   SCOPED_TIMER("putStoredHeadHgtList");

   if(hhl.height_ == UINT32_MAX)
   {
      LOGERR << "HHL does not have a valid height to be put into DB";
      return false;
   }

   putValue(HEADERS, hhl.getDBKey(), hhl.serializeDBValue());
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::getStoredHeadHgtList(
   StoredHeadHgtList & hhl, uint32_t height) const
{
   BinaryData ldbKey = WRITE_UINT32_BE(height);
   BinaryDataRef bdr = getValueRef(HEADERS, DB_PREFIX_HEADHGT, ldbKey);

   hhl.height_ = height;
   if(bdr.getSize() > 0)
   {
      hhl.unserializeDBValue(bdr);
      return true;
   }
   else
   {
      hhl.preferredDup_ = UINT8_MAX;
      hhl.dupAndHashList_.resize(0);
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
TxRef LMDBBlockDatabase::getTxRef(BinaryDataRef txHash)
{
   auto&& key = getDBKeyForHash(txHash);
   if (key.getSize() == 6)
      return TxRef(key);

   return TxRef();
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
TxRef LMDBBlockDatabase::getTxRef( uint32_t hgt, uint8_t  dup, uint16_t txIndex)
{
   BinaryWriter bw;
   bw.put_BinaryData(DBUtils::heightAndDupToHgtx(hgt,dup));
   bw.put_uint16_t(txIndex, BE);
   return TxRef(bw.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
bool LMDBBlockDatabase::markBlockHeaderValid(BinaryDataRef headHash)
{
   SCOPED_TIMER("markBlockHeaderValid");
   BinaryRefReader brr = getValueReader(HEADERS, DB_PREFIX_HEADHASH, headHash);
   if(brr.getSize()==0)
   {
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
   SCOPED_TIMER("markBlockHeaderValid");

   StoredHeadHgtList hhl;
   getStoredHeadHgtList(hhl, height);
   if(hhl.preferredDup_ == dup)
      return true;

   bool hasEntry = false;
   for(uint32_t i=0; i<hhl.dupAndHashList_.size(); i++)
      if(hhl.dupAndHashList_[i].first == dup)
         hasEntry = true;
   

   if(hasEntry)
   {
      hhl.setPreferredDupID(dup);
      putStoredHeadHgtList(hhl);
      setValidDupIDForHeight(height, dup);
      return true;
   }   
   else
   {
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
   SCOPED_TIMER("getAllDatabaseEntries");
   
   if(!databasesAreOpen())
      return KVLIST();

   auto&& tx = beginTransaction(db, LMDB::ReadOnly);

   KVLIST outList;
   outList.reserve(100);

   auto ldbIter = getIterator(db);
   ldbIter->seekToFirst();
   for(ldbIter->seekToFirst(); ldbIter->isValid(); ldbIter->advanceAndRead())
   {
      size_t last = outList.size();
      outList.push_back( pair<BinaryData, BinaryData>() );
      outList[last].first  = ldbIter->getKey();
      outList[last].second = ldbIter->getValue();
   }

   return outList;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::printAllDatabaseEntries(DB_SELECT db)
{
   SCOPED_TIMER("printAllDatabaseEntries");

   cout << "Printing DB entries... (DB=" << db << ")" << endl;
   KVLIST dbList = getAllDatabaseEntries(db);
   if(dbList.size() == 0)
   {
      cout << "   <no entries in db>" << endl;
      return;
   }

   for(uint32_t i=0; i<dbList.size(); i++)
   {
      cout << "   \"" << dbList[i].first.toHexStr() << "\"  ";
      cout << "   \"" << dbList[i].second.toHexStr() << "\"  " << endl;
   }
}

////////////////////////////////////////////////////////////////////////////////
map<uint32_t, uint32_t> LMDBBlockDatabase::getSSHSummary(
   BinaryDataRef scrAddrStr)
{
   map<uint32_t, uint32_t> SSHsummary;

   auto ldbIter = getIterator(SSH);

   if (!ldbIter->seekToExact(DB_PREFIX_SCRIPT, scrAddrStr))
      return SSHsummary;

   StoredScriptHistory ssh;
   BinaryDataRef sshKey = ldbIter->getKeyRef();
   ssh.unserializeDBKey(sshKey, true);
   ssh.unserializeDBValue(ldbIter->getValueReader());

   return ssh.subsshSummary_;
}

////////////////////////////////////////////////////////////////////////////////
uint32_t LMDBBlockDatabase::getStxoCountForTx(const BinaryData & dbKey6) const
{
   if (dbKey6.getSize() != 6)
   {
      LOGERR << "wrong key size";
      return UINT32_MAX;
   }

   if (!dbKey6.startsWith(DBUtils::ZeroConfHeader_))
   {
      if (getDbType() != ARMORY_DB_SUPER)
      {
         auto&& tx = beginTransaction(TXHINTS, LMDB::ReadOnly);

         BinaryRefReader brr = getValueRef(TXHINTS, DB_PREFIX_TXDATA, dbKey6);
         if (brr.getSize() == 0)
         {
            LOGERR << "no Tx data at key";
            return UINT32_MAX;
         }

         return brr.get_uint32_t();
      }
      else
      {
         auto&& tx = beginTransaction(STXO, LMDB::ReadOnly);

         //convert height to id
         unsigned height;
         uint8_t dup;
         uint16_t txid;

         BinaryRefReader brr(dbKey6.getRef());
         DBUtils::readBlkDataKeyNoPrefix(brr, height, dup, txid);

         auto header = blockchainPtr_->getHeaderByHeight(height, dup);
         auto id = header->getThisID();

         auto&& id_key = DBUtils::getBlkDataKeyNoPrefix(id, 0xFF, txid);
         auto data = getValueNoCopy(STXO, id_key);

         BinaryRefReader data_brr(data);
         data_brr.advance(32);
         return data_brr.get_var_int();
      }
   }

   return UINT32_MAX;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::resetHistoryForAddressVector(
   const vector<BinaryData>& addrVec)
{
   auto&& tx = beginTransaction(SSH, LMDB::ReadWrite);

   for (auto& addr : addrVec)
   {
      if (addr.getSize() == 0)
         continue;

      BinaryData addrWithPrefix;
      if (addr.getPtr()[0] == DB_PREFIX_SCRIPT)
      {
         addrWithPrefix = addr;
      }
      else
      {
         addrWithPrefix = WRITE_UINT8_LE(DB_PREFIX_SCRIPT);
         addrWithPrefix.append(addr);
      }

      deleteValue(SSH, addrWithPrefix.getRef());
   }
}

/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::resetSSHdb()
{
   if (getDbType() == ARMORY_DB_SUPER)
   {
      resetSSHdb_Super();
      return;
   }

   map<BinaryData, int> sshKeys;

   {
      //gather keys
      auto&& tx = beginTransaction(SSH, LMDB::ReadOnly);

      auto dbIter = getIterator(SSH);

      while (dbIter->advanceAndRead(DB_PREFIX_SCRIPT))
      {
         StoredScriptHistory ssh;
         ssh.unserializeDBValue(dbIter->getValueRef());

         sshKeys[dbIter->getKeyRef()] = ssh.scanHeight_;
      }
   }

   {
      //reset them

      auto&& tx = beginTransaction(SSH, LMDB::ReadWrite);

      for (auto& sshkey : sshKeys)
      {
         StoredScriptHistory ssh;
         BinaryWriter bw;
         
         ssh.scanHeight_ = sshkey.second;
         ssh.serializeDBValue(bw, ARMORY_DB_FULL);

         putValue(SSH, sshkey.first.getRef(), bw.getDataRef());
      }

      auto&& sdbi = getStoredDBInfo(SSH, 0);
      sdbi.topBlkHgt_ = 0;
      sdbi.topScannedBlkHash_ = BtcUtils::EmptyHash_;
      putStoredDBInfo(SSH, sdbi, 0);
   }
}

/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::resetSSHdb_Super()
{
   //delete checkpoint and ssh db
   {
      auto db_ssh = getDbPtr(SSH);
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

   auto db = TXFILTERS;
   auto tx = beginTransaction(db, LMDB::ReadOnly);

   auto val = getValueNoCopy(TXFILTERS, key);
   try
   {
      return TxFilterPoolWriter(val);
   }
   catch (const TxFilterException&)
   {
      //return empty pool if data is missing
      return TxFilterPoolWriter();
   }
}

/////////////////////////////////////////////////////////////////////////////
BinaryDataRef LMDBBlockDatabase::getFilterPoolDataRef(
   uint32_t fileNum) const
{
   auto key = DBUtils::getFilterPoolKey(fileNum);

   auto db = TXFILTERS;
   auto tx = beginTransaction(db, LMDB::ReadOnly);

   auto val = getValueNoCopy(TXFILTERS, key);
   if (val.empty())
      throw TxFilterException("invalid txfilter key");

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
   auto db = TXFILTERS;
   auto tx = beginTransaction(db, LMDB::ReadWrite);
   putValue(TXFILTERS, key, data);
}

/////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putMissingHashes(
   const set<BinaryData>& hashSet, uint32_t id)
{
   auto&& missingHashesKey = DBUtils::getMissingHashesKey(id);

   BinaryWriter bw;
   bw.put_uint32_t(hashSet.size());
   for (auto& hash : hashSet)
      bw.put_BinaryData(hash);

   putValue(TXFILTERS, missingHashesKey.getRef(), bw.getDataRef());
}

/////////////////////////////////////////////////////////////////////////////
set<BinaryData> LMDBBlockDatabase::getMissingHashes(uint32_t id) const
{
   auto&& missingHashesKey = DBUtils::getMissingHashesKey(id);

   auto&& tx = beginTransaction(TXFILTERS, LMDB::ReadOnly);

   auto&& rawMissingHashes = getValueNoCopy(TXFILTERS, missingHashesKey);

   BinaryRefReader brr(rawMissingHashes);
   if (brr.getSizeRemaining() < 4)
      throw LmdbWrapperException("invalid missing hashes entry");

   set<BinaryData> missingHashesSet;

   auto&& len = brr.get_uint32_t();
   if (rawMissingHashes.getSize() != len * 32 + 4)
      throw LmdbWrapperException("missing hashes entry size mismatch");

   for (uint32_t i = 0; i < len; i++)
      missingHashesSet.insert(move(brr.get_BinaryData(32)));

   return missingHashesSet;
}

////////////////////////////////////////////////////////////////////////////////
void LMDBBlockDatabase::putStoredDBInfo(
   DB_SELECT db, StoredDBInfo const & sdbi, uint32_t id)
{
   auto dbPtr = getDbPtr(db);
   dbPtr->putStoredDBInfo(sdbi, id);
}

////////////////////////////////////////////////////////////////////////////////
StoredDBInfo LMDBBlockDatabase::getStoredDBInfo(
   DB_SELECT db, uint32_t id)
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
   auto tx = beginTransaction(SUBSSH_META, LMDB::ReadOnly);
   auto dbIter = getIterator(SUBSSH_META);

   map<unsigned, unsigned> heightToIdMap;
   
   BinaryWriter bw_key(8);
   bw_key.put_uint32_t(0);
   bw_key.put_uint32_t(0);
      
   if (!dbIter->seekToExact(bw_key.getDataRef()))
      return;

   do
   {
      auto brr_value = dbIter->getValueReader();
      auto height = brr_value.get_uint32_t();

      auto brr_key = dbIter->getKeyReader();
      auto ctr = brr_key.get_uint32_t(BE);

      heightToIdMap.insert(make_pair(height, ctr));
      ++ctr;
   } 
   while (dbIter->advanceAndRead());

   heightToBatchId_.update(move(heightToIdMap));
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// DatabaseContainer
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
std::filesystem::path DatabaseContainer::baseDir_;
BinaryData DatabaseContainer::magicBytes_;

DatabaseContainer::~DatabaseContainer()
{}

////////////////////////////////////////////////////////////////////////////////
std::filesystem::path DatabaseContainer::getDbPath(DB_SELECT db)
{
   return getDbPath(getDbName(db));
}

////////////////////////////////////////////////////////////////////////////////
std::filesystem::path DatabaseContainer::getDbPath(const std::string& dbName)
{
   return baseDir_ / dbName;
}

////////////////////////////////////////////////////////////////////////////////
string DatabaseContainer::getDbName(DB_SELECT db)
{
   switch (db)
   {
   case HEADERS:
      return "headers";

   case BLKDATA:
      return "blkdata";

   case HISTORY:
      return "history";

   case TXHINTS:
      return "txhints";

   case SSH:
      return "ssh";

   case SUBSSH:
      return "subssh";

   case SUBSSH_META:
      return "subssh_meta";

   case STXO:
      return "stxo";

   case ZERO_CONF:
      return "zeroconf";

   case TXFILTERS:
      return "txfilters";

   case SPENTNESS:
      return "spentness";

   default:
      throw LmdbWrapperException("unknown db");
   }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// DBPair
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
LMDBEnv::Transaction DBPair::beginTransaction(LMDB::Mode mode)
{
   return LMDBEnv::Transaction(&env_, mode);
}

////////////////////////////////////////////////////////////////////////////////
void DBPair::open(const std::filesystem::path& path, const string& dbName)
{
   if (isOpen()) {
      return;
   }
   unsigned flags = MDB_NOSYNC | MDB_NOTLS;

   env_.open(path, flags);
   auto map_size = LMDBBlockDatabase::mapSizes_.at(dbName);
   env_.setMapSize(map_size);

   auto&& tx = beginTransaction(LMDB::ReadWrite);
   db_.open(&env_, dbName);
}

////////////////////////////////////////////////////////////////////////////////
void DBPair::close()
{
   if (!isOpen())
      return;

   db_.close();
   env_.close();
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef DBPair::getValue(BinaryDataRef key) const
{
   CharacterArrayRef carKey(key.getSize(), key.getPtr());
   auto carData = db_.get_NoCopy(carKey);

   if (carData.len == 0)
      return BinaryDataRef();

   BinaryDataRef data((uint8_t*)carData.data, carData.len);
   return data;
}

////////////////////////////////////////////////////////////////////////////////
void DBPair::putValue(BinaryDataRef key,BinaryDataRef value)
{
   db_.insert(
      CharacterArrayRef(key.getSize(), key.getPtr()),
      CharacterArrayRef(value.getSize(), value.getPtr()));
}

////////////////////////////////////////////////////////////////////////////////
void DBPair::deleteValue(BinaryDataRef key)
{
   db_.erase(CharacterArrayRef(key.getSize(), key.getPtr()));
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<LDBIter_Single> DBPair::getIterator()
{
   return make_unique<LDBIter_Single>(move(db_.begin()));
}

////////////////////////////////////////////////////////////////////////////////
bool DBPair::isOpen() const
{
   return env_.isOpen() && db_.isOpen();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// DatabaseContainer_Single
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DatabaseContainer_Single::close()
{
   db_.close();
}

////////////////////////////////////////////////////////////////////////////////
void DatabaseContainer_Single::eraseOnDisk()
{
   close();
   auto dbPath = getDbPath(dbSelect_);
   std::filesystem::remove(dbPath);

   dbPath.append("-lock");
   std::filesystem::remove(dbPath);
}

////////////////////////////////////////////////////////////////////////////////
StoredDBInfo DatabaseContainer_Single::open()
{
   db_.open(getDbPath(dbSelect_), getDbName(dbSelect_));

   StoredDBInfo sdbi;
   try {
      sdbi = std::move(getStoredDBInfo(0));
   } catch (const std::runtime_error&) {
      // If DB didn't exist yet (dbinfo key is empty), seed it
      auto tx = db_.beginTransaction(LMDB::ReadWrite);

      sdbi.magic_ = magicBytes_;
      sdbi.metaHash_ = BtcUtils::EmptyHash_;
      sdbi.topBlkHgt_ = 0;
      sdbi.armoryType_ = DBSettings::getDbType();
      putStoredDBInfo(sdbi, 0);
   }
   return sdbi;
}

////////////////////////////////////////////////////////////////////////////////
void DatabaseContainer_Single::putStoredDBInfo(
   StoredDBInfo const & sdbi, uint32_t id)
{
   SCOPED_TIMER("putStoredDBInfo");
   if (!sdbi.isInitialized())
      throw LmdbWrapperException("tried to write uninitiliazed sdbi");

   putValue(StoredDBInfo::getDBKey(id), serializeDBValue(sdbi));
}

////////////////////////////////////////////////////////////////////////////////
StoredDBInfo DatabaseContainer_Single::getStoredDBInfo(uint32_t id)
{
   SCOPED_TIMER("getStoredDBInfo");
   auto&& tx = db_.beginTransaction(LMDB::ReadOnly);

   auto&& key = StoredDBInfo::getDBKey(id);
   BinaryRefReader brr(getValue(key.getRef()));

   if (brr.getSize() == 0)
      throw LmdbWrapperException("no sdbi at this key");

   StoredDBInfo sdbi;
   sdbi.unserializeDBValue(brr);
   return sdbi;
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef DatabaseContainer_Single::getValue(BinaryDataRef key) const
{
   return db_.getValue(key);
}

////////////////////////////////////////////////////////////////////////////////
void DatabaseContainer_Single::putValue(
   BinaryDataRef key,
   BinaryDataRef value)
{
   db_.putValue(key, value);
}

////////////////////////////////////////////////////////////////////////////////
void DatabaseContainer_Single::deleteValue(BinaryDataRef key)
{
   db_.deleteValue(key);
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<DbTransaction> DatabaseContainer_Single::beginTransaction(
   LMDB::Mode mode) const
{
   return make_unique<DbTransaction_Single>(move(db_.beginTransaction(mode)));
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<LDBIter> DatabaseContainer_Single::getIterator()
{
   return db_.getIterator();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// ShardFilter
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
ShardFilter::~ShardFilter()
{}

////////////////////////////////////////////////////////////////////////////////
BinaryData ShardFilter::getDbKey()
{
   return WRITE_UINT32_BE(SHARD_FILTER_DBKEY);
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<ShardFilter> ShardFilter::deserialize(BinaryDataRef dataRef)
{
   BinaryRefReader brr(dataRef);
   
   auto type = brr.get_uint8_t();
   switch (type)
   {
   case ShardFilterType_ScrAddr:
   {
      return ShardFilter_ScrAddr::deserialize(dataRef);
   }

   case ShardFilterType_Spentness:
   {
      return ShardFilter_Spentness::deserialize(dataRef);
   }

   default:
      throw FilterException("unexpected shard filter type");
   }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// ShardFilter_ScrAddr
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
BinaryData ShardFilter_ScrAddr::serialize() const
{
   BinaryWriter bw;
   bw.put_uint8_t(ShardFilterType_ScrAddr);
   bw.put_uint32_t(step_);

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<ShardFilter> ShardFilter_ScrAddr::deserialize(BinaryDataRef dataRef)
{
   BinaryRefReader brr(dataRef);
   
   auto type = brr.get_uint8_t();
   if (type != (uint8_t)ShardFilterType_ScrAddr)
      throw FilterException("shard filter type mismatch");

   auto step = brr.get_uint32_t();
   return make_unique<ShardFilter_ScrAddr>(step);
}

////////////////////////////////////////////////////////////////////////////////
unsigned ShardFilter_ScrAddr::keyToId(BinaryDataRef keyRef) const
{
   auto size = keyRef.getSize();
   if (size < 4)
      throw FilterException("key is too short for scrAddr shard filter");

   BinaryRefReader brr(keyRef);
   brr.advance(size - 4);
   auto height = DBUtils::hgtxToHeight(brr.get_BinaryDataRef(4));

   if (height >= thresholdValue_)
   {
      auto diff = height - thresholdValue_;
      return thresholdId_ + (diff / step_);
   }
   else
   {
      //id = exp((height/50k - 4)*1.6)
      auto val = (float(height) / 50000.0f - 4.0f) * 1.6f;
      return expl(val);
   }
}

////////////////////////////////////////////////////////////////////////////////
unsigned ShardFilter_ScrAddr::getHeightForId(unsigned id) const
{
   if (id == 0)
      return 0;
   else if (id <= thresholdId_)
      return unsigned((logf(id) / 1.6f + 4.0f) * 50000.0f);
   else
      return thresholdValue_ + (id - thresholdId_) * step_;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// ShardFilter_Spentness
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
BinaryData ShardFilter_Spentness::serialize() const
{
   BinaryWriter bw;
   bw.put_uint8_t(ShardFilterType_Spentness);
   bw.put_uint32_t(step_);

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<ShardFilter> ShardFilter_Spentness::deserialize(BinaryDataRef dataRef)
{
   BinaryRefReader brr(dataRef);

   auto type = brr.get_uint8_t();
   if (type != (uint8_t)ShardFilterType_Spentness)
      throw FilterException("shard filter type mismatch");

   auto step = brr.get_uint32_t();
   return make_unique<ShardFilter_Spentness>(step);
}

////////////////////////////////////////////////////////////////////////////////
unsigned ShardFilter_Spentness::keyToId(BinaryDataRef keyRef) const
{
   auto size = keyRef.getSize();
   if (size < 4)
      throw FilterException("key is too short for scrAddr shard filter");

   BinaryRefReader brr(keyRef);
   auto height = DBUtils::hgtxToHeight(brr.get_BinaryDataRef(4));

   if (height >= thresholdValue_)
   {
      auto diff = height - thresholdValue_;
      return thresholdId_ + (diff / step_);
   }
   else
   {
      //id = exp((height/50k - 4))
      auto val = (float(height) / 50000.0f - 4.0f);
      return expl(val);
   }
}

////////////////////////////////////////////////////////////////////////////////
unsigned ShardFilter_Spentness::getHeightForId(unsigned id) const
{
   if (id == 0)
      return 0;
   else if (id <= thresholdId_)
      return unsigned((logf(id) + 4.0f) * 50000.0f);
   else
      return thresholdValue_ + (id - thresholdId_) * step_;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// DbTransaction
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DbTransaction::~DbTransaction()
{}
