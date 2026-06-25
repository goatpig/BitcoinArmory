////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2026, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "lmdbpp.h"
#include "lmdb.h"
#ifndef _WIN32
#  include <unistd.h>
#endif
#include <sstream>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <format>

using namespace LMDB;

namespace {
   std::string errorString(int rc)
   {
      return mdb_strerror(rc);
   }
}

////////////////////////////////////////////////////////////////////////////////
// exceptions
Exception::Exception(const std::string& what)
   : std::runtime_error(what)
{}

NoValue::NoValue(const std::string& what)
   : Exception(what)
{}

////////////////////////////////////////////////////////////////////////////////
// CharacterArrayRef
DataRef::DataRef(const size_t _len, const char *_data)
   : len(_len), data(_data)
{}

DataRef::DataRef(const size_t _len, const unsigned char *_data)
   : len(_len), data(reinterpret_cast<const char*>(_data))
{}

DataRef::DataRef(const std::string &_data)
   : len(_data.size()), data(&_data[0])
{}

DataRef::DataRef(const std::vector<char> &_data)
   : len(_data.size()), data(&_data.front())
{}

////////////////////////////////////////////////////////////////////////////////
// Transaction
Transaction::Transaction(Env *_env, DbIndex dbi, Mode mode)
   : env_(_env), dbi_(dbi), mode_(mode)
{
   if (env_ == nullptr) {
      throw Exception("null Env");
   }
   tid_ = std::this_thread::get_id();
   begin();
}

Transaction::Transaction(Transaction&& mv)
{
   tid_ = std::this_thread::get_id();
   if (tid_ != mv.tid_) {
      throw Exception("cannot move tx accross threads");
   }

   env_ = mv.env_;
   began_ = mv.began_;
   mode_ = mv.mode_;
   mdbTxn_ = mv.mdbTxn_;
   dbi_ = mv.dbi_;
   mv.began_ = false;
}

Transaction::~Transaction()
{
   commit();
}

////////
Transaction& Transaction::operator=(Transaction&& mv)
{
   if (this == &mv) {
      return *this;
   }

   tid_ = std::this_thread::get_id();
   if (tid_ != mv.tid_) {
      throw Exception("cannot move tx accross threads");
   }

   this->env_ = mv.env_;
   this->mode_ = mv.mode_;
   this->began_ = mv.began_;
   this->mdbTxn_ = mv.mdbTxn_;
   this->dbi_ = mv.dbi_;
   mv.began_ = false;
   return *this;
}

void Transaction::begin()
{
   if (began_) {
      return;
   }
   began_ = true;

   auto tID = std::this_thread::get_id();
   std::unique_lock<std::mutex> lock(env_->threadTxMutex_);
   ThreadTxInfo& thTx = env_->txForThreads_[tID];
   lock.unlock();

   if (thTx.transactionLevel != 0 && mode_ == Mode::ReadWrite &&
      thTx.mode == Mode::ReadOnly) {
      throw Exception("Cannot access ReadOnly Transaction in ReadWrite mode");
   }

   if (thTx.transactionLevel++ != 0) {
      mdbTxn_ = thTx.txn;
      return;
   }
   if (env_->mdbEnv_ == nullptr) {
      throw Exception("Cannot start transaction without db env");
   }

   int modef = MDB_RDONLY;
   thTx.mode = Mode::ReadOnly;
   if (mode_ == Mode::ReadWrite) {
      modef = 0;
      thTx.mode = Mode::ReadWrite;
   }

   mdbTxn_ = nullptr;
   int rc = mdb_txn_begin(env_->mdbEnv_, nullptr, modef, &thTx.txn);
   if (rc != MDB_SUCCESS) {
      lock.lock();
      env_->txForThreads_.erase(tID);
      lock.unlock();

      began_ = false;
      throw Exception("Failed to create transaction (" + errorString(rc) +")");
   }
   mdbTxn_ = thTx.txn;
}

void Transaction::open(Env *_env, Mode mode)
{
   if (env_) {
      commit();
   }
   this->env_ = _env;
   this->mode_ = mode;
   begin();
}

void Transaction::commit()
{
   if (!began_) {
      return;
   }
   began_ = false;

   //look for an existing transaction in this thread
   auto tID = std::this_thread::get_id();
   std::unique_lock<std::mutex> lock(env_->threadTxMutex_);
   auto txnIter = env_->txForThreads_.find(tID);

   if (txnIter == env_->txForThreads_.end()) {
      throw Exception("Transaction bound to unknown thread");
   }
   ThreadTxInfo& thTx = txnIter->second;
   lock.unlock();

   if (thTx.transactionLevel-- == 1) {
      int rc = mdb_txn_commit(thTx.txn);
      for (Iterator *i : thTx.iterators) {
         mdb_cursor_close(i->csr_);
         i->csr_ = nullptr;
      }
      if (rc != MDB_SUCCESS) {
         throw Exception("Failed to close env tx (" + errorString(rc) +")");
      }

      lock.lock();
      env_->txForThreads_.erase(txnIter);
   }
}

void Transaction::rollback()
{
   throw std::runtime_error("unimplemented");
}

////////
void Transaction::insert(
   const DataRef& key,
   const DataRef& value)
{
   MDB_val mkey{ key.len, const_cast<char*>(key.data) };
   MDB_val mval{ value.len, const_cast<char*>(value.data) };
   int rc = mdb_put(mdbTxn_, dbi_, &mkey, &mval, 0);
   if (rc == MDB_SUCCESS) {
      return;
   }

   std::cout << "failed to insert data, returned following error string: " <<
      errorString(rc) << std::endl;
   throw Exception("Failed to insert (" + errorString(rc) + ")");
}

void Transaction::erase(
   const DataRef& key)
{
   MDB_val mkey = { key.len, const_cast<char*>(key.data) };
   int rc = mdb_del(mdbTxn_, dbi_, &mkey, 0);
   if (rc != MDB_SUCCESS && rc != MDB_NOTFOUND) {
      std::cout << "failed to erase data, returned following error string: "
         << errorString(rc) << std::endl;
      throw Exception("Failed to erase (" + errorString(rc) + ")");
   }
}

DataRef Transaction::get(const DataRef& key) const
{
   MDB_val mkey{ key.len, const_cast<char*>(key.data) };
   MDB_val mdata{ 0, 0 };

   int rc = mdb_get(mdbTxn_, dbi_, &mkey, &mdata);
   if (rc == MDB_NOTFOUND) {
      return DataRef{0, (uint8_t*)nullptr};
   }
   DataRef ref(
      mdata.mv_size,
      static_cast<uint8_t*>(mdata.mv_data)
   );
   return ref;
}

Iterator Transaction::getIterator() const
{
   return Iterator{this};
}

////////////////////////////////////////////////////////////////////////////////
// LMDB::Iterator
Iterator::Iterator(const Transaction* tx)
   : txPtr_(tx), csr_(nullptr), has_(false)
{
   openCursor();
}

Iterator::Iterator(const Iterator &copy)
   : txPtr_(copy.txPtr_), csr_(nullptr), has_(copy.has_)
{
   if (copy.txPtr_ == nullptr) {
      throw Exception("Iterator must be created within Transaction");
   }
   operator=(copy);
}

Iterator::~Iterator()
{
   reset();
}

Iterator::Iterator(Iterator &&move)
{
   operator=(std::move(move));
}

////////
inline void Iterator::reset()
{
   if (csr_) {
      mdb_cursor_close(csr_);
   }
   csr_ = nullptr;
   has_ = false;
}

bool Iterator::isValid() const
{
   return has_;
}

////////
Iterator& Iterator::operator=(Iterator&& move)
{
   reset();

   txPtr_ = move.txPtr_;
   std::swap(csr_ , move.csr_);
   std::swap(has_ , move.has_);
   std::swap(key_ , move.key_);
   std::swap(val_ , move.val_);

   move.reset();
   return *this;
}

Iterator& Iterator::operator=(const Iterator& copy)
{
   if (&copy == this) {
      return *this;
   }
   reset();

   has_ = copy.has_;
   txPtr_ = copy.txPtr_;
   openCursor();

   if (copy.has_) {
      DataRef keydata{copy.key_.mv_size,
         (const char*)copy.key_.mv_data};
      seek(keydata);
      if (!has_) {
         throw Exception("Cursor could not be copied");
      }
   }
   return *this;
}

bool Iterator::operator==(const Iterator& other) const
{
   if (this == &other) {
      return true;
   }

   bool a = !isValid();
   bool b = !other.isValid();
   if (a && b) {
      return true;
   } else if (a || b) {
      return false;
   }

   //make sure this is a proper check
   return key().mv_data == other.key().mv_data &&
      key().mv_size == key().mv_size;
}

bool Iterator::operator!=(const Iterator& other) const
{
   return !operator==(other);
}

Iterator& Iterator::operator++()
{
   advance();
   return *this;
}

Iterator& Iterator::operator--()
{
   retreat();
   return *this;
}

////////
void Iterator::openCursor()
{
   if (txPtr_ == nullptr || txPtr_->dbi_ == 0) {
      throw Exception("iterator needs valid tx");
   }
   if (txPtr_->tid_ != std::this_thread::get_id()) {
      throw Exception("iterator has to exist within same thread as tx");
   }

   int rc = mdb_cursor_open(txPtr_->mdbTxn_, txPtr_->dbi_, &csr_);
   if (rc != MDB_SUCCESS) {
      csr_ = nullptr;
      throw Exception("Failed to open cursor (" + errorString(rc) + ")");
   }
}

void Iterator::advance()
{
   if (!has_) {
      throw Exception("invalid iterator, cannot advance");
   }
   MDB_val mkey;
   MDB_val mval;

   int rc = mdb_cursor_get(csr_, &mkey, &mval, MDB_NEXT);
   if (rc == MDB_NOTFOUND) {
      has_ = false;
   } else if (rc != MDB_SUCCESS) {
      throw NoValue("Failed to seek (" + errorString(rc) +")");
   } else {
      has_ = true;
      key_ = mkey;
      val_ = mval;
   }
}

void Iterator::retreat()
{
   if (!has_) {
      throw Exception("invalid iterator, cannot retreat");
   }
   MDB_val mkey;
   MDB_val mval;

   int rc = mdb_cursor_get(csr_, &mkey, &mval, MDB_PREV);
   if (rc == MDB_NOTFOUND) {
      has_ = false;
   } else if (rc != MDB_SUCCESS) {
      throw NoValue("Failed to seek (" + errorString(rc) +")");
   } else {
      has_ = true;
      key_ = mkey;
      val_ = mval;
   }
}

void Iterator::toFirst()
{
   MDB_val mkey;
   MDB_val mval;

   int rc = mdb_cursor_get(csr_, &mkey, &mval, MDB_FIRST);
   if (rc == MDB_NOTFOUND) {
      has_ = false;
   } else if (rc != MDB_SUCCESS) {
      throw NoValue("Failed to seek (" + errorString(rc) +")");
   } else {
      has_ = true;
      key_ = mkey;
      val_ = mval;
   }
}

void Iterator::toLast()
{
   MDB_val mkey;
   MDB_val mval;

   int rc = mdb_cursor_get(csr_, &mkey, &mval, MDB_LAST);
   if (rc == MDB_NOTFOUND) {
      has_ = false;
   } else if (rc != MDB_SUCCESS) {
      throw NoValue("Failed to seek (" + errorString(rc) + ")");
   } else {
      has_ = true;
      key_ = mkey;
      val_ = mval;
   }
}

void Iterator::seek(const DataRef &key, SeekBy e)
{
   MDB_val mkey = { key.len, const_cast<char*>(key.data) };
   MDB_val mval = { 0, 0 };

   MDB_cursor_op op=MDB_SET;
   if (e == SeekBy::GE) {
      op = MDB_SET_RANGE;
   } else if (e == SeekBy::LE) {
      op = MDB_SET_RANGE;
   }

   int rc = mdb_cursor_get(csr_, &mkey, &mval, op);
   if (e == SeekBy::LE) {
      if (rc == MDB_NOTFOUND) {
         rc = mdb_cursor_get(csr_, &mkey, &mval, MDB_LAST);
      }
      // now make sure mkey is less than key
      if (rc == MDB_NOTFOUND) {
         has_ = false;
         return;
      } else if (rc != MDB_SUCCESS) {
         throw NoValue("Failed to seek (" + errorString(rc) +")");
      }

      if (mkey.mv_size > key.len) {
         // mkey can't possibly be before key if it's longer than key
         has_ = false;
         return;
      }

      const int cmp = std::memcmp(mkey.mv_data, key.data, key.len);
      if (cmp <= 0) {
         // key is longer and the earlier bytes are the same,
         // therefor, mkey is before key
         has_ = true;
         key_ = mkey;
         val_ = mval;
         return;
      } else {
         has_ = false;
         return;
      }
   }
   
   if (rc == MDB_NOTFOUND) {
      has_ = false;
   } else if (rc != MDB_SUCCESS) {
      throw NoValue("Failed to seek (" + errorString(rc) +")");
   } else {
      has_ = true;
      key_ = mkey;
      val_ = mval;
   }
}

////////
const MDB_val& Iterator::key() const
{
   if (!has_) {
      throw Exception("invalid iterator, cannot get key");
   }
   return key_;
}

const MDB_val& Iterator::value() const
{
   if (!has_) {
      throw Exception("invalid iterator, cannot get value");
   }
   return val_;
}

////////////////////////////////////////////////////////////////////////////////
// Env
Env::Env()
{}

Env::Env(unsigned dbCount)
{
   dbCount_ = dbCount;
}

Env::~Env()
{
   close();
}

////////
bool Env::isOpen() const
{
   return mdbEnv_ != nullptr;
}

void Env::open(const std::filesystem::path &path, unsigned flags)
{
   if (isOpen()) {
      throw Exception("Database environment already open (close it first)");
   }
   txForThreads_.clear();

   int rc;
   rc = mdb_env_create(&mdbEnv_);
   if (rc != MDB_SUCCESS) {
      throw Exception("Failed to load mdb env (" + errorString(rc) + ")");
   }

   rc = mdb_env_set_maxdbs(mdbEnv_, dbCount_);
   if (rc != MDB_SUCCESS) {
      throw Exception("Failed to set max dbs (" + errorString(rc) + ")");
   }

   rc = mdb_env_open(mdbEnv_, path.string().c_str(), MDB_NOSUBDIR | flags, 0600);
   if (rc != MDB_SUCCESS) {
      throw Exception(std::format("Failed to open db \"{}\" with error: {}",
         path.filename().string(), errorString(rc)));
   }
   path_ = path;
}

void Env::close()
{
   if (mdbEnv_) {
      mdb_env_close(mdbEnv_);
      mdbEnv_ = nullptr;
   }
}

////////
DbIndex Env::openDb(const std::string_view& name)
{
   Transaction tx{this, 0, Mode::ReadWrite};
   DbIndex dbi = 0;
   int rc = mdb_open(tx.mdbTxn_, name.data(), MDB_CREATE, &dbi);
   if (rc != MDB_SUCCESS) {
      throw Exception("Failed to open dbi (" + errorString(rc) +")");
   }
   return dbi;
}

void Env::closeDb(DbIndex dbi)
{
   mdb_dbi_close(mdbEnv_, dbi);
}

////////
void Env::setMapSize(size_t sz)
{
   auto rc = mdb_env_set_mapsize(mdbEnv_, sz);
   if (rc != MDB_SUCCESS) {
      std::string errStr{
         "failed to insert set map size, returned following error string: " +
         errorString(rc)};
      std::cout << errStr << std::endl;
      throw Exception(errStr);
   }
}

void Env::compactCopy(const std::filesystem::path& fname)
{
   auto rc = mdb_env_copy2(mdbEnv_, fname.string().c_str(), MDB_CP_COMPACT);
   if (rc != MDB_SUCCESS) {
      std::string errStr{
         "failed to copy env, returned following error string: " +
         errorString(rc)};
      std::cout << errStr << std::endl;
      throw Exception(errStr);
   }
}

const std::filesystem::path& Env::getFilename() const
{
   return path_;
}

////////
bool Env::hasAnyTx() const
{
   std::unique_lock<std::mutex> lock(threadTxMutex_);
   return !txForThreads_.empty();
}

////////////////////////////////////////////////////////////////////////////////
// LMDB
DB::DB()
{}

DB::~DB()
{
   try {
      close();
   } catch (const std::exception &e) {
      std::cerr << "Error: " << e.what() << std::endl;
   }
}

////////
void DB::close()
{
   if (dbi_ != 0) {
      if (env_->hasAnyTx()) {
         throw Exception("trying to close database with open txes");
      }
      env_->closeDb(dbi_);
      dbi_ = 0;
      env_ = nullptr;
   }
}

bool DB::isOpen() const
{
   return env_ != nullptr;
}

void DB::open(Env* dbenv, const std::string_view& name)
{
   if (isOpen()) {
      throw Exception("DB already open");
   }
   if (dbenv == nullptr) {
      throw Exception("null LMDB env");
   }
   dbi_ = dbenv->openDb(name);
   env_ = dbenv;
}

////////
DbIndex DB::dbi() const
{
   return dbi_;
}

/*
NOTE: this breaks tx data isolation
void LMDB::wipe(const CharacterArrayRef& key)
{
   auto tID = std::this_thread::get_id();
   std::unique_lock<std::mutex> lock(env_->threadTxMutex_);
   auto txnIter = env_->txForThreads_.find(tID);

   if (txnIter == env_->txForThreads_.end()) {
      throw LMDBException("Failed to insert: need transaction");
   }
   lock.unlock();

   try {
      MDB_val mdb_data_obj;
      mdb_data_obj = value(key);
      if (mdb_data_obj.mv_data != nullptr) {
         memset(mdb_data_obj.mv_data, 0, mdb_data_obj.mv_size);
      }
   } catch (const NoValue&) {
      return;
   }

   MDB_val mkey{ key.len, const_cast<char*>(key.data) };
   int rc = mdb_del(txnIter->second.txn, dbi_, &mkey, 0); // , MDB_WIPE_DATA);
   if (rc != MDB_SUCCESS && rc != MDB_NOTFOUND) {
      std::cout << "failed to erase data, returned following error string: " << errorString(rc) << std::endl;
      throw LMDBException("Failed to erase (" + errorString(rc) + ")");
   }
}
*/
