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

#include <string>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <filesystem>

#include "lmdb.h"

struct MDB_env;
struct MDB_txn;
struct MDB_cursor;

namespace LMDB
{
   // this exception is thrown for all errors from LMDB
   class Exception : public std::runtime_error
   {
   public:
      Exception(const std::string&);
   };

   class NoValue : public Exception
   {
   public:
      NoValue(const std::string&);
   };

   enum class Mode : int
   {
      ReadWrite=1,
      ReadOnly
   };

   // a struct that stores a pointer to a memory block
   struct DataRef
   {
      const size_t len;
      const char *data;

      DataRef(const size_t, const char*);
      DataRef(const size_t, const unsigned char*);
      DataRef(const std::string&);
      DataRef(const std::vector<char>&);
   };

   /////////////////////////////////////////////////////////////////////////////
   using DbIndex = unsigned;
   class Env;
   class Iterator;
   class Transaction
   {
      friend class Env;
      friend class Iterator;

   private:
      Env *env_;
      DbIndex dbi_;
      Mode mode_;
      MDB_txn *mdbTxn_ = nullptr;
      std::thread::id tid_;
      bool began_ = false;

   public:
      Transaction(Env*, DbIndex, Mode = Mode::ReadWrite);
      ~Transaction(void);

      Transaction(Transaction&&);
      Transaction& operator=(Transaction&&);

      // commit the current transaction, create a new one, and begin it
      void open(Env*, Mode = Mode::ReadWrite);

      // commit a transaction, if it exists, doing nothing otherwise.
      // after this function completes, no transaction exists
      void commit(void);

      // rollback the transaction, if it exists, doing nothing otherwise.
      // All modifications made since this transaction began are removed.
      // After this function completes, no transaction exists
      void rollback(void);

      // start a new transaction. If one already exists, do nothing
      void begin(void);

      // straight into tx db operations
      void insert(const DataRef&, const DataRef&);
      void erase(const DataRef&);
      DataRef get(const DataRef&) const;
      Iterator getIterator(void) const;

   private:
      Transaction(const Transaction&); // no copies
   };

   /////////////////////////////////////////////////////////////////////////////
   struct ThreadTxInfo;
   class Iterator
   {
      // this class can be used like a C++ iterator,
      // or you can just use isValid() to test for "last item"
      friend class Transaction;

   private:
      const Transaction* txPtr_;
      MDB_cursor* csr_ = nullptr;

      bool has_ = false;
      MDB_val key_, val_;

   private:
      void reset(void);
      void openCursor(void);

   public:
      Iterator(const Transaction*);
      ~Iterator(void);

      // copying permitted (encouraged!)
      Iterator(const Iterator&);
      Iterator(Iterator&&);
      Iterator& operator=(Iterator&&);
      Iterator& operator=(const Iterator&);

      // Returns true if the key pointed to is identical, or if both iterators
      // are invalid, and false otherwise.
      // returns true if the key pointed to is in different databases
      bool operator==(const Iterator&) const;
      // the inverse
      bool operator!=(const Iterator&) const;

      enum class SeekBy : int
      {
         EQ,
         GE,
         LE
      };

      // move this iterator such that, if the exact key is not found:
      // for e == Seek_EQ
      // The cursor is left as Invalid.
      // for e == Seek::GE
      // The cursor is left pointing to the smallest key in the database that is
      // larger than (key). If the database contains no keys larger than
      // (key), the cursor is left as Invalid.
      void seek(const DataRef&, SeekBy = SeekBy::EQ);

      // is the cursor pointing to a valid location?
      bool isValid(void) const;

      // advance the cursor
      // the postfix increment operator is not defined for performance reasons
      Iterator& operator++(void);
      void advance(void);

      Iterator& operator--(void);
      void retreat(void);

      // seek this iterator to the first sequence
      void toFirst(void);
      void toLast(void);

      // returns the key currently pointed to, if no key is being pointed to
      // Exception is thrown. You can avoid throws by checking isValid() first
      const MDB_val& key(void) const;

      // returns the value currently pointed to. Exceptions are thrown
      // under the same conditions as key()
      const MDB_val& value(void) const;
   };

   /////////////////////////////////////////////////////////////////////////////
   // one mother-txn per thread
   struct ThreadTxInfo
   {
      MDB_txn *txn = nullptr;
      std::vector<Iterator*> iterators;
      unsigned transactionLevel = 0;
      Mode mode;
   };

   class Env
   {
      friend class Transaction;

   private:
      MDB_env *mdbEnv_ = nullptr;
      unsigned dbCount_ = 1;
      std::filesystem::path path_;

      mutable std::mutex threadTxMutex_;
      std::unordered_map<std::thread::id, ThreadTxInfo> txForThreads_;

   public:
      Env(void);
      Env(unsigned);
      ~Env(void);

      // open a database by filename
      void open(const std::filesystem::path&, unsigned = 0);
      bool isOpen(void) const;
      void close();

      DbIndex openDb(const std::string_view&);
      void closeDb(DbIndex);

      const std::filesystem::path& getFilename(void) const;
      void setMapSize(size_t);
      void compactCopy(const std::filesystem::path&);
      bool hasAnyTx(void) const;

   private:
      Env(const Env&); // disallow copy
   };

   /////////////////////////////////////////////////////////////////////////////
   class DB
   {
   private:
      Env* env_ = nullptr;
      DbIndex dbi_ = 0;

   public:
      DB(void);
      ~DB(void);

      void open(Env*, const std::string_view& name={});
      bool isOpen(void) const;
      void close(void);
      DbIndex dbi(void) const;

   private:
      DB(const DB&);
      void resize(MDB_env*);
   };
} //namespace LMDB
