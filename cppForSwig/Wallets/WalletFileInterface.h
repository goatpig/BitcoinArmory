////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2025, goatpig                                          //
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
#include <filesystem>

#include "EncryptedDB.h"
#include "GetPassphrase.h"
#include "IOHeader.h"

#define CONTROL_DB_NAME "control_db"sv

////////////////////////////////////////////////////////////////////////////////
namespace LMDB
{
   class DB;
   class Env;
}

class BinaryData;

namespace Armory
{
   namespace Seeds
   {
      class EncryptedSeed;
   };

   namespace Wallets
   {
      namespace Encryption
      {
         class DecryptedDataContainer;
      };

      namespace IO
      {
         class WalletInterfaceException : public EncryptedDBException
         {
         public:
            WalletInterfaceException(const std::string& err) :
               EncryptedDBException(err)
            {}
         };

         class WalletDBInterface;

         class WalletIfaceTransaction : public DBIfaceTransaction
         {
            friend class WalletIfaceIterator;

         private:
            WalletDBInterface* ifacePtr_;
            DBInterface* dbPtr_;
            bool commit_ = false;

            /*
            insertVec tracks the order in which insertion and deletion take place
            keyToMapData keeps tracks of the final effect for each key

            using shared_ptr to reduce cost of copies when resizing the vector
            */
            std::vector<std::shared_ptr<InsertData>> insertVec_;
            std::map<BinaryData, unsigned> keyToDataMap_;

            std::function<void(const BinaryData&, BothBinaryDatas&)> insertLbd_;
            std::function<void(const BinaryData&)> eraseLbd_;
            std::function<const std::shared_ptr<InsertData>&(const BinaryData&)>
               getDataLbd_;
            std::shared_ptr<IfaceDataMap> dataMapPtr_;

         private:
            static bool insertTx(WalletIfaceTransaction*);
            static std::unique_ptr<std::unique_lock<std::recursive_mutex>>
            eraseTx(WalletIfaceTransaction*);

            void closeTx(void);
            const std::shared_ptr<InsertData>& getInsertDataForKey(
               const BinaryData&) const;

         public:
            WalletIfaceTransaction(WalletDBInterface*, DBInterface*, bool);
            ~WalletIfaceTransaction(void) noexcept(false);

            const std::string& getDbName(void) const;

            //write routines
            void insert(const BinaryData&, BinaryData&) override;
            void insert(const BinaryData&, const BinaryData&) override;
            void insert(const BinaryData&, SecureBinaryData&) override;
            void erase(const BinaryData&) override;

            //get routines
            const BinaryDataRef getDataRef(const BinaryData&) const override;
            std::shared_ptr<DBIfaceIterator> getIterator() const override;
         };

         //////////////////////////////////////////////////////////////////////////
         class WalletIfaceIterator : public DBIfaceIterator
         {
         private:
            const WalletIfaceTransaction* txPtr_;
            std::map<BinaryData, BothBinaryDatas>::const_iterator iterator_;

         public:
            WalletIfaceIterator(const WalletIfaceTransaction*);

            bool isValid(void) const override;
            void seek(const BinaryDataRef&) override;
            void advance(void) override;

            BinaryDataRef key(void) const override;
            BinaryDataRef value(void) const override;
         };

         struct WalletHeader;
         struct WalletHeader_Control;
         struct MasterKeyStruct;

         //////////////////////////////////////////////////////////////////////////
         class WalletDBInterface
         {
            friend class DBIfaceTransaction;
            friend class WalletIfaceTransaction;

         private:
            mutable std::mutex setupMutex_;

            std::unique_ptr<LMDB::Env> dbEnv_ = nullptr;
            std::map<std::string, std::unique_ptr<DBInterface>, std::less<>> dbMap_;

            //encryption objects
            std::unique_ptr<LMDB::DB> controlDb_;

            //wallet structure
            std::map<std::string, std::shared_ptr<WalletHeader>> headerMap_;

            std::filesystem::path path_;
            unsigned dbCount_ = 0;

            std::unique_ptr<Encryption::DecryptedDataContainer> decryptedData_;
            std::unique_ptr<ReentrantLock> controlLock_;
            std::unique_ptr<Armory::Seeds::EncryptedSeed> controlSeed_;

            unsigned encryptionVersion_ = UINT32_MAX;

         private:
            //control objects loading
            std::shared_ptr<WalletHeader> loadControlHeader();
            void loadDataContainer(std::shared_ptr<WalletHeader>);
            void loadSeed(std::shared_ptr<WalletHeader>);
            void loadHeaders(void);

            //utils
            BinaryDataRef getDataRefForKey(
               DBIfaceTransaction*, const BinaryData&);
            void setDbCount(unsigned, bool);
            void openDB(std::shared_ptr<WalletHeader>,
               const SecureBinaryData&, unsigned);

            //header methods
            void openControlDb(void);
            std::shared_ptr<WalletHeader_Control> setupControlDB(
               const CreateFileParams&);
            void putHeader(std::shared_ptr<WalletHeader>);

            void openDbEnv(bool);
            void openEnv(void);
            void closeEnv(void);

            void compactFile();
            static void wipeAndDeleteFile(const std::filesystem::path&);

         public:
            //tors
            WalletDBInterface(void);
            ~WalletDBInterface(void);

            //setup
            void createEnv(const CreateFileParams&);
            void setupEnv(const ReadOnlyFileParams&);
            void shutdown(void);
            void eraseFromDisk(void);

            const std::filesystem::path& getFilename(void) const;

            //headers
            static MasterKeyStruct initWalletHeaderObject(
               std::shared_ptr<WalletHeader>, Passphrase::Params&);
            void addHeader(std::shared_ptr<WalletHeader>);
            std::shared_ptr<WalletHeader> getWalletHeader(
               const std::string&) const;
            const std::map<std::string, std::shared_ptr<WalletHeader>>&
               getHeaderMap(void) const;

            //db count
            unsigned getDbCount(void) const;
            unsigned getFreeDbCount(void) const;
            void setDbCount(unsigned);

            //transactions
            std::unique_ptr<DBIfaceTransaction> beginWriteTransaction(
               const std::string_view&);
            std::unique_ptr<DBIfaceTransaction> beginReadTransaction(
               const std::string_view&);

            //utils
            void lockControlContainer(const Passphrase::UnlockFunc&);
            void unlockControlContainer(void);

            void changeControlPassphrase(
               Passphrase::SetNew&,
               const Passphrase::UnlockFunc&);
            void eraseControlPassphrase(const Passphrase::UnlockFunc&);
         };
      }; //namespace IO
   }; //namespace Wallets
}; //namespace Armory
