////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <functional>
#include <map>

#include <Utils/ReentrantLock.h>
#include <Utils/BinaryData.h>
#include "WalletIdTypes.h"
#include "GetPassphrase.h"
#include "AssetEncryption.h"

#define ENCRYPTIONKEY_PREFIX        0xC0
#define ENCRYPTIONKEY_PREFIX_TEMP   0xCC

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
         class KeyDerivationFunction;

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

         using WriteTxFuncType = std::function<
            std::unique_ptr<IO::DBIfaceTransaction>(const std::string&)>;

         ////
         class DecryptedDataContainer
         {
         private:
            struct DecryptedDataMaps : public Lockable
            {
               std::map<EncryptionKeyId,
                  std::unique_ptr<ClearTextEncryptionKey>> encryptionKeys;
               std::map<AssetId,
                  std::unique_ptr<ClearTextAssetData>> clearTextAssets;
               Passphrase::UnlockFunc getPassphraseFunc = nullptr;

               void initAfterLock(void) override;
               void cleanUpBeforeUnlock(void) override;
            };

         private:
            DecryptedDataMaps decryptedData_;

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
            std::map<KdfId, std::shared_ptr<KeyDerivationFunction>> kdfMap_;
            std::map<EncryptionKeyId,
               std::shared_ptr<EncryptionKey>> encryptedKeys_;

         private:
            std::unique_ptr<ClearTextEncryptionKey> deriveEncryptionKey(
               std::unique_ptr<ClearTextEncryptionKey>, const KdfId&) const;
            std::unique_ptr<ClearTextEncryptionKey> promptPassphrase(
               const std::map<EncryptionKeyId, KdfId>&) const;
            const KdfId& getKdfForSetNew(const EncryptionKeyId&,
               const Passphrase::SetNew&, const EncryptionKeyId&);
            std::pair<EncryptionKeyId, EncryptionKeyId> populateEncryptionKey(
               const std::map<EncryptionKeyId, KdfId>&);

         public:
            DecryptedDataContainer(
               const WriteTxFuncType&, const std::string,
               const SecureBinaryData&, const EncryptionKeyId&,
               const KdfId&, const EncryptionKeyId&);

            SingleLock lockContainer(const Passphrase::UnlockFunc&);

            const SecureBinaryData& getClearTextAssetData(
               const std::shared_ptr<EncryptedAssetData>& data);
            const SecureBinaryData& getClearTextAssetData(
               const EncryptedAssetData* data);
            const SecureBinaryData& getClearTextAssetData(
               const AssetId&) const;
            const AssetId& insertClearTextAssetData(
               const uint8_t*, size_t);
            SecureBinaryData encryptData(Cipher* const, const SecureBinaryData&);

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

            void encryptEncryptionKey(const EncryptionKeyId&,
               const Passphrase::UnlockFunc&, Passphrase::SetNew&, bool=true);
            void eraseEncryptionKey(const EncryptionKeyId&, const KdfId&,
               const Passphrase::UnlockFunc&);

            const KdfId& getDefaultKdfId(void) const;
            std::shared_ptr<KeyDerivationFunction> getMasterKdf(void) const;
            bool isMasterKeyEncrypted(void) const;
            const EncryptionKeyId& getMasterEncryptionKeyId(void) const;
            const EncryptionKeyId& getDefaultEncryptionKeyId(void) const;
         };
      } //namespace Encryption
   } //namespace Wallets
} //namespace Armory
