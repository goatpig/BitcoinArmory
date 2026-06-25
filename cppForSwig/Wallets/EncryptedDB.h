////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <mutex>
#include <functional>

#include "lmdbpp.h"
#include "Utils/BinaryData.h"
#include "Utils/SecureBinaryData.h"
#include "Utils/ReentrantLock.h"

namespace Armory
{
   namespace Wallets
   {
      namespace IO
      {
         ////
         class EncryptedDBException : public std::runtime_error
         {
         public:
            EncryptedDBException(const std::string&);
         };

         ////
         struct NoDataInDB
         {};

         ////
         class NoEntryInWalletException
         {};

         ///////////////////////////////////////////////////////////////////////
         struct BothBinaryDatas
         {
         public:
            BinaryData bd_;
            SecureBinaryData sbd_;

         public:
            BothBinaryDatas(void);
            BothBinaryDatas(BinaryData&);
            BothBinaryDatas(const BinaryData&);
            BothBinaryDatas(SecureBinaryData&);

            const BinaryDataRef getRef(void) const;
            size_t getSize(void) const;
         };

         ///////////////////////////////////////////////////////////////////////
         struct InsertData
         {
            BinaryData key_;
            BothBinaryDatas value_;
            bool write_ = true;
         };

         ///////////////////////////////////////////////////////////////////////
         class DBIfaceIterator;
         class WalletIfaceIterator;
         class WalletIfaceTransaction;

         ////
         struct IfaceDataMap
         {
            std::map<BinaryData, BothBinaryDatas> dataMap_;
            std::map<BinaryData, BinaryData> dataKeyToDbKey_;
            uint32_t dbKeyCounter_ = 0;

            void update(const std::vector<std::shared_ptr<InsertData>>&);
            bool resolveDataKey(const BinaryData&, BinaryData&);
            BinaryData getNewDbKey(void);
         };

         ///////////////////////////////////////////////////////////////////////
         class DBInterface
         {
            friend class WalletIfaceIterator;
            friend class WalletIfaceTransaction;

         private:
            LMDB::Env* dbEnv_;
            const std::string dbName_;
            const SecureBinaryData controlSalt_;
            const unsigned encrVersion_;

            LMDB::DB db_;
            std::atomic<std::shared_ptr<IfaceDataMap>> dataMapPtr_;

            SecureBinaryData encrPubKey_;
            SecureBinaryData macKey_;

            static const BinaryData erasurePlaceHolder_;
            static const BinaryData keyCycleFlag_;

         private:
            //serialization methods
            static BinaryData createDataPacket(const BinaryData&,
               const BinaryData&, const BothBinaryDatas&,
               const SecureBinaryData&, const SecureBinaryData&,
               unsigned);
            static std::pair<BinaryData, BothBinaryDatas> readDataPacket(
               const BinaryData&, const BinaryData&,
               const SecureBinaryData&, const SecureBinaryData&,
               unsigned);

         public:
            DBInterface(LMDB::Env*, const std::string&,
               const SecureBinaryData&, unsigned);
            ~DBInterface(void);

            ////
            void loadAllEntries(const SecureBinaryData&);
            void reset(LMDB::Env*);
            void close(void);

            ////
            const std::string& getName(void) const;
            unsigned getEntryCount(void) const;
         };

         ///////////////////////////////////////////////////////////////////////
         class DBIfaceTransaction
         {
         protected:
            struct ParentTx
            {
               unsigned counter_ = 1;
               bool commit_;
               std::unique_ptr<std::unique_lock<std::recursive_mutex>> writeLock_;

               std::function<void(const BinaryData&, BothBinaryDatas&)> insertLbd_;
               std::function<void(const BinaryData&)> eraseLbd_;
               std::function<const std::shared_ptr<InsertData>&(const BinaryData&)>
                  getDataLbd_;

               std::shared_ptr<IfaceDataMap> dataMapPtr_;
            };

            struct DbTxStruct
            {
               unsigned txCount_ = 0;
               std::map<std::thread::id, std::shared_ptr<ParentTx>> txMap_;
               unsigned txCount(void) const { return txCount_; }
            };

         public:
            static std::recursive_mutex writeMutex_;

         protected:
            //<dbName, DbTxStruct>
            static std::map<std::string, std::shared_ptr<DbTxStruct>> dbMap_;
            static std::mutex txMutex_;

         public:
            DBIfaceTransaction(void);
            virtual ~DBIfaceTransaction(void) noexcept(false) = 0;

            virtual void insert(const BinaryData&, BinaryData&) = 0;
            virtual void insert(const BinaryData&, const BinaryData&) = 0;
            virtual void insert(const BinaryData&, SecureBinaryData&) = 0;
            virtual void erase(const BinaryData&) = 0;

            virtual const BinaryDataRef getDataRef(const BinaryData&) const = 0;
            virtual std::shared_ptr<DBIfaceIterator> getIterator(void) const = 0;

            static bool hasTx(void);
         };

         ///////////////////////////////////////////////////////////////////////
         class RawIfaceTransaction : public DBIfaceTransaction
         {
         private:
            LMDB::DB* dbPtr_;
            std::unique_ptr<LMDB::Transaction> txPtr_;

         public:
            RawIfaceTransaction(LMDB::Env&, LMDB::DB&, bool);
            ~RawIfaceTransaction(void) noexcept(false);

            //write routines
            void insert(const BinaryData&, BinaryData&) override;
            void insert(const BinaryData&, const BinaryData&) override;
            void insert(const BinaryData&, SecureBinaryData&) override;
            void erase(const BinaryData&) override;

            //get routines
            const BinaryDataRef getDataRef(const BinaryData&) const override;
            std::shared_ptr<DBIfaceIterator> getIterator(void) const override;
         };

         ///////////////////////////////////////////////////////////////////////
         class DBIfaceIterator
         {
         public:
            DBIfaceIterator(void) {}
            virtual ~DBIfaceIterator(void) = 0;

            virtual bool isValid(void) const = 0;
            virtual void seek(const BinaryDataRef&) = 0;
            virtual void advance(void) = 0;

            virtual BinaryDataRef key(void) const = 0;
            virtual BinaryDataRef value(void) const = 0;
         };

         ///////////////////////////////////////////////////////////////////////
         class RawIfaceIterator : public DBIfaceIterator
         {
         private:
            LMDB::Iterator iterator_;

         public:
            RawIfaceIterator(LMDB::Transaction*);

            bool isValid(void) const override;
            void seek(const BinaryDataRef&) override;
            void advance(void) override;

            BinaryDataRef key(void) const override;
            BinaryDataRef value(void) const override;
         };
      }; //IO
   }; //Wallets
}; //Armory
