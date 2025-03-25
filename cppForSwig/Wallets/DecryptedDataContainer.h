////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_DECRYPTED_DATA_CONTAINER
#define _H_DECRYPTED_DATA_CONTAINER

#include <functional>

#include "AssetEncryption.h"
#include "ReentrantLock.h"
#include "BinaryData.h"
#include "PassphraseLambda.h"

#define ENCRYPTIONKEY_PREFIX        0xC0
#define ENCRYPTIONKEY_PREFIX_TEMP   0xCC

////////////////////////////////////////////////////////////////////////////////
namespace Armory
{
   namespace Wallets
   {
      namespace IO
      {
         class DBIfaceTransaction;
      };

      class AssetUnavailableException
      {};

      namespace Encryption
      {
         class DecryptedDataContainerException : public std::runtime_error
         {
         public:
            DecryptedDataContainerException(const std::string& msg) :
               std::runtime_error(msg)
            {}
         };

         class EncryptedDataMissing : public std::runtime_error
         {
         public:
            EncryptedDataMissing() : std::runtime_error("")
            {}
         };

         using WriteTxFuncType = std::function<std::unique_ptr<
            IO::DBIfaceTransaction>(const std::string&)>;

         ////
         class DecryptedDataContainer : public Lockable
         {
         private:
            struct DecryptedDataMaps
            {
               std::map<EncryptionKeyId, std::unique_ptr<
                  ClearTextEncryptionKey>> encryptionKeys_;

               std::map<AssetId,
                  std::unique_ptr<ClearTextAssetData>> assetData_;
            };

         private:
            std::map<KdfId, std::shared_ptr<KeyDerivationFunction>> kdfMap_;
            std::unique_ptr<DecryptedDataMaps> lockedDecryptedData_ = nullptr;

            struct OtherLockedContainer
            {
               std::shared_ptr<DecryptedDataContainer> container_;
               std::shared_ptr<ReentrantLock> lock_;

               OtherLockedContainer(std::shared_ptr<DecryptedDataContainer> obj)
               {
                  if (obj == nullptr) {
                     throw std::runtime_error(
                        "emtpy DecryptedDataContainer ptr");
                  }
                  lock_ = std::make_unique<ReentrantLock>(obj.get());
               }
            };

            std::vector<OtherLockedContainer> otherLocks_ = {};

         public:
            const WriteTxFuncType getWriteTx_;
            const std::string dbName_;

         private:
            /*
            The default encryption key is used to encrypt the master encryption
            in case no passphrase was provided at wallet creation. This is to
            prevent the master key being written in plain text on disk. It
            is encryption but does not effectively result in the wallet being
            protected by encryption, since the default encryption key is written
            on disk in plain text.

            This is mostly to allow for all private keys to be encrypted without
            implementing large caveats to handle unencrypted use cases.
            */
            const SecureBinaryData defaultEncryptionKey_;
            const EncryptionKeyId defaultEncryptionKeyId_;

            const KdfId defaultKdfId_;
            const EncryptionKeyId masterEncryptionKeyId_;

         protected:
            std::map<EncryptionKeyId,
               std::shared_ptr<EncryptionKey>> encryptedKeys_;

         private:
            PassphraseLambda getPassphraseLambda_;

         private:
            std::unique_ptr<ClearTextEncryptionKey> deriveEncryptionKey(
               std::unique_ptr<ClearTextEncryptionKey>, const KdfId&) const;
            std::unique_ptr<ClearTextEncryptionKey> promptPassphrase(
               const std::map<EncryptionKeyId, KdfId>&) const;

            void initAfterLock(void);
            void cleanUpBeforeUnlock(void);

         public:
            const EncryptionKeyId& getDefaultEncryptionKeyId(void) const
            {
               return defaultEncryptionKeyId_;
            }

         public:
            DecryptedDataContainer(
               const WriteTxFuncType& getWriteTx,
               const std::string dbName,
               const SecureBinaryData& defaultEncryptionKey,
               const EncryptionKeyId& defaultEncryptionKeyId,
               const KdfId& defaultKdfId,
               const EncryptionKeyId& masterKeyId);

            const SecureBinaryData& getClearTextAssetData(
               const std::shared_ptr<EncryptedAssetData>& data);
            const SecureBinaryData& getClearTextAssetData(
               const EncryptedAssetData* data);
            const SecureBinaryData& getClearTextAssetData(
               const AssetId&) const;
            const AssetId& insertClearTextAssetData(
               const uint8_t*, size_t);

            SecureBinaryData encryptData(Cipher* const, const SecureBinaryData&);
            EncryptionKeyId populateEncryptionKey(
               const std::map<EncryptionKeyId, KdfId>&);

            void addKdf(std::shared_ptr<KeyDerivationFunction>);
            std::shared_ptr<KeyDerivationFunction> getKdf(
               const KdfId&) const;
            void addEncryptionKey(std::shared_ptr<EncryptionKey>);
            std::shared_ptr<EncryptionKey> getEncryptionKey(
               const EncryptionKeyId&) const;

            void updateOnDisk(void);
            void updateOnDisk(std::unique_ptr<IO::DBIfaceTransaction>);
            void readFromDisk(std::shared_ptr<IO::DBIfaceTransaction>);

            void updateOnDiskRaw(std::shared_ptr<IO::DBIfaceTransaction>,
               const BinaryData&, std::shared_ptr<EncryptionKey>);
            void updateOnDisk(std::shared_ptr<IO::DBIfaceTransaction>,
               const EncryptionKeyId&,
               std::shared_ptr<EncryptionKey>);
            void deleteFromDisk(std::shared_ptr<IO::DBIfaceTransaction>,
               const BinaryData&);

            void setPassphrasePromptLambda(const PassphraseLambda& lambda)
            {
               getPassphraseLambda_ = lambda;
            }

            void resetPassphraseLambda(void) { getPassphraseLambda_ = nullptr; }

            void encryptEncryptionKey(
               const EncryptionKeyId&,
               const KdfId&, const KdfId&, //kdf in, out
               const std::function<SecureBinaryData(void)>&, bool replace=true);
            void eraseEncryptionKey(const EncryptionKeyId&, const KdfId&);

            void lockOther(std::shared_ptr<DecryptedDataContainer> other);
            const KdfId& getDefaultKdfId(void) const { return defaultKdfId_; }
            const EncryptionKeyId& getMasterEncryptionKeyId(void) const
            {
               return masterEncryptionKeyId_;
            }
         };
      }; //namespace Encryption
   }; //namespace Wallets
}; //namespace Armory

#endif
