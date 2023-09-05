////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_WALLETFILEINTERFACE_
#define _H_WALLETFILEINTERFACE_

#include <memory>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <mutex>
#include <functional>

#include "lmdbpp.h"
#include "BinaryData.h"
#include "SecureBinaryData.h"
#include "EncryptedDB.h"
#include "PassphraseLambda.h"

#define CONTROL_DB_NAME "control_db"

////////////////////////////////////////////////////////////////////////////////
class PRNG_Fortuna;

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
            WalletIfaceTransaction(WalletDBInterface*,
               DBInterface* dbPtr, bool mode);
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
            WalletIfaceIterator(const WalletIfaceTransaction* tx) :
               txPtr_(tx)
            {
               if (tx == nullptr)
                  throw WalletInterfaceException("null tx");

               iterator_ = tx->dataMapPtr_->dataMap_.begin();
            }

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

            std::unique_ptr<LMDBEnv> dbEnv_ = nullptr;
            std::map<std::string, std::unique_ptr<DBInterface>> dbMap_;

            //encryption objects
            std::unique_ptr<LMDB> controlDb_;

            //wallet structure
            std::map<std::string, std::shared_ptr<WalletHeader>> headerMap_;

            std::string path_;
            unsigned dbCount_ = 0;

            std::unique_ptr<Encryption::DecryptedDataContainer> decryptedData_;
            std::unique_ptr<ReentrantLock> controlLock_;
            std::unique_ptr<Armory::Seeds::EncryptedSeed> controlSeed_;

            unsigned encryptionVersion_ = UINT32_MAX;
            std::unique_ptr<PRNG_Fortuna> fortuna_;

         private:
            //control objects loading
            std::shared_ptr<WalletHeader> loadControlHeader();
            void loadDataContainer(std::shared_ptr<WalletHeader>);
            void loadSeed(std::shared_ptr<WalletHeader>);
            void loadHeaders(void);

            //utils
            BinaryDataRef getDataRefForKey(
               DBIfaceTransaction* tx, const BinaryData& key);
            void setDbCount(unsigned, bool);
            void openDB(std::shared_ptr<WalletHeader>,
               const SecureBinaryData&, unsigned encrVersion);

            //header methods
            void openControlDb(void);
            std::shared_ptr<WalletHeader_Control> setupControlDB(
               const PassphraseLambda&, uint32_t);
            void putHeader(std::shared_ptr<WalletHeader>);

            void openDbEnv(bool);
            void openEnv(void);
            void closeEnv(void);

            void compactFile();
            static void wipeAndDeleteFile(const std::string&);

         public:
            //tors
            WalletDBInterface(void);
            ~WalletDBInterface(void);

            //setup
            void setupEnv(const std::string&, bool,
               const PassphraseLambda&, uint32_t);
            void shutdown(void);
            void eraseFromDisk(void);

            const std::string& getFilename(void) const;

            //headers
            static MasterKeyStruct initWalletHeaderObject(
               std::shared_ptr<WalletHeader>,
               const SecureBinaryData&,
               uint32_t);
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
               const std::string&);
            std::unique_ptr<DBIfaceTransaction> beginReadTransaction(
               const std::string&);

            //utils
            void lockControlContainer(const PassphraseLambda&);
            void unlockControlContainer(void);

            void changeControlPassphrase(
               const std::function<SecureBinaryData(void)>& newPassLbd,
               const PassphraseLambda& passLbd);
            void eraseControlPassphrase(const PassphraseLambda& passLbd);
         };
      }; //namespace IO
   }; //namespace Wallets
}; //namespace Armory

#endif