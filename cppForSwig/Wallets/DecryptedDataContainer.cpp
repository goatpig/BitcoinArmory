////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "DecryptedDataContainer.h"
#include "EncryptedDB.h"
#include "AssetEncryption.h"

using namespace std;
using namespace Armory::Wallets;
using namespace Armory::Wallets::Encryption;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// DecryptedDataContainer
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DecryptedDataContainer::DecryptedDataContainer(
   const WriteTxFuncType& getWriteTx,
   const std::string dbName,
   const SecureBinaryData& defaultEncryptionKey,
   const EncryptionKeyId& defaultEncryptionKeyId,
   const SecureBinaryData& defaultKdfId,
   const EncryptionKeyId& masterKeyId) :
   getWriteTx_(getWriteTx), dbName_(dbName),
   defaultEncryptionKey_(defaultEncryptionKey),
   defaultEncryptionKeyId_(defaultEncryptionKeyId),
   defaultKdfId_(defaultKdfId),
   masterEncryptionKeyId_(masterKeyId)
{
   resetPassphraseLambda();

   //add passthrough kdf
   auto passthroughKdf = std::make_shared<KeyDerivationFunction_Passthrough>();
   addKdf(passthroughKdf);
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::initAfterLock()
{
   auto decryptedDataInstance = std::make_unique<DecryptedDataMaps>();

   //copy default encryption key
   auto defaultEncryptionKeyCopy = defaultEncryptionKey_.copy();

   auto defaultKey = std::make_unique<ClearTextEncryptionKey>(
      defaultEncryptionKeyCopy);
   decryptedDataInstance->encryptionKeys_.emplace(
      defaultEncryptionKeyId_, std::move(defaultKey));

   lockedDecryptedData_ = std::move(decryptedDataInstance);
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::cleanUpBeforeUnlock()
{
   otherLocks_.clear();
   lockedDecryptedData_.reset();
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::lockOther(
   shared_ptr<DecryptedDataContainer> other)
{
   if (!ownsLock()) {
      throw DecryptedDataContainerException(
         "[DecryptedDataContainer::lockOther] unlocked/does not own lock");
   }

   if (lockedDecryptedData_ == nullptr) {
      throw DecryptedDataContainerException(
         "nullptr lock! how did we get this far?");
   }
   otherLocks_.push_back(OtherLockedContainer(other));
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::addKdf(
   std::shared_ptr<KeyDerivationFunction> kdfPtr)
{
   if (kdfPtr==nullptr) {
      return;
   }
   kdfMap_.emplace(kdfPtr->getId(), kdfPtr);
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<KeyDerivationFunction> DecryptedDataContainer::getKdf(
   const SecureBinaryData& kdfId) const
{
   auto iter = kdfMap_.find(kdfId);
   if (iter == kdfMap_.end()) {
      return nullptr;
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<EncryptionKey> DecryptedDataContainer::getEncryptionKey(
   const EncryptionKeyId& keyId) const
{
   auto iter = encryptedKeys_.find(keyId);
   if (iter == encryptedKeys_.end()) {
      throw std::runtime_error("unknown encryption key id");
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::addEncryptionKey(
   std::shared_ptr<EncryptionKey> keyPtr)
{
   encryptedKeys_.emplace(keyPtr->getId(), keyPtr);
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<ClearTextEncryptionKey> DecryptedDataContainer::deriveEncryptionKey(
   unique_ptr<ClearTextEncryptionKey> decrKey,
   const BinaryData& kdfid) const
{
   //sanity check
   if (!ownsLock()) {
      throw DecryptedDataContainerException(
         "[DecryptedDataContainer::deriveEncryptionKey]"
         " unlocked/does not own lock");
   }

   if (lockedDecryptedData_ == nullptr) {
      throw DecryptedDataContainerException(
         "nullptr lock! how did we get this far?");
   }

   //does the decryption key have this derivation?
   auto derivationIter = decrKey->derivedKeys_.find(kdfid);
   if (derivationIter == decrKey->derivedKeys_.end()) {
      //look for the kdf
      auto kdfIter = kdfMap_.find(kdfid);
      if (kdfIter == kdfMap_.end() || kdfIter->second == nullptr) {
         throw DecryptedDataContainerException("can't find kdf params for id");
      }
      //derive the key, this will insert it into the container too
      decrKey->deriveKey(kdfIter->second);
   }
   return decrKey;
}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& DecryptedDataContainer::getClearTextAssetData(
   const std::shared_ptr<EncryptedAssetData>& dataPtr)
{
   return getClearTextAssetData(dataPtr.get());
}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& DecryptedDataContainer::getClearTextAssetData(
   const EncryptedAssetData* dataPtr)
{
   /*
   Decrypt data from asset, insert it in decrypted data locked container.
   Return reference from that. Return straight from container if decrypted
   data is already there.

   Data is keyed by its asset id.
   */

   //sanity check
   if (!ownsLock()) {
      throw DecryptedDataContainerException(
         "[DecryptedDataContainer::getClearTextAssetData]"
         " unlocked/does not own lock");
   }

   if (lockedDecryptedData_ == nullptr) {
      throw DecryptedDataContainerException(
      "nullptr lock! how did we get this far?");
   }

   if (dataPtr == nullptr) {
      throw DecryptedDataContainerException("null data");
   }

   auto insertDecryptedData = [this](
      std::unique_ptr<ClearTextAssetData> decrKey)
      ->const SecureBinaryData&
   {
      //if decrKey is empty, all casts failed, throw
      if (decrKey == nullptr) {
         throw DecryptedDataContainerException("unexpected dataPtr type");
      }

      //make sure insertion succeeds
      auto keyId = decrKey->getId();
      lockedDecryptedData_->assetData_.erase(keyId);
      auto insertionPair = lockedDecryptedData_->assetData_.emplace(
         keyId, std::move(decrKey));
      return insertionPair.first->second->getData();
   };

   //look for already decrypted data
   auto dataIter = lockedDecryptedData_->assetData_.find(dataPtr->getAssetId());
   if (dataIter != lockedDecryptedData_->assetData_.end()) {
      return dataIter->second->getData();
   }

   //no decrypted val entry, let's try to decrypt the data instead
   if (!dataPtr->hasData()) {
      //missing encrypted data in container (most likely uncomputed private key)
      //throw back to caller, this object only deals with ciphers
      throw EncryptedDataMissing();
   }

   //check cipher
   if (dataPtr->getCipherDataPtr()->cipher_ == nullptr) {
      //null cipher, data is not encrypted, create entry and return it
      auto dataCopy = dataPtr->getCipherText();
      auto decrData = std::make_unique<ClearTextAssetData>(
         dataPtr->getAssetId(), dataCopy);
      return insertDecryptedData(std::move(decrData));
   }

   //we have a valid cipher, grab the encryption key
   const auto* cipherPtr = dataPtr->getCipherDataPtr()->cipher_.get();
   const auto& encryptionKeyId = cipherPtr->getEncryptionKeyId();
   const auto& kdfId = cipherPtr->getKdfId();

   std::map<EncryptionKeyId, BinaryData> encrKeyMap;
   encrKeyMap.emplace(encryptionKeyId, kdfId);
   populateEncryptionKey(encrKeyMap);

   auto decrKeyIter =
      lockedDecryptedData_->encryptionKeys_.find(encryptionKeyId);
   if (decrKeyIter == lockedDecryptedData_->encryptionKeys_.end()) {
      throw DecryptedDataContainerException("could not get encryption key");
   }

   auto derivationKeyIter = decrKeyIter->second->derivedKeys_.find(kdfId);
   if (derivationKeyIter == decrKeyIter->second->derivedKeys_.end()) {
      throw DecryptedDataContainerException("could not get derived encryption key");
   }

   //decrypt data
   auto decryptedDataPtr = dataPtr->decrypt(derivationKeyIter->second);

   //sanity check
   if (decryptedDataPtr == nullptr) {
      throw DecryptedDataContainerException("failed to decrypt data");
   }

   //insert the newly decrypted data in the container and return
   return insertDecryptedData(std::move(decryptedDataPtr));
}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& DecryptedDataContainer::getClearTextAssetData(
   const AssetId& id) const
{
   /*
   Get decrypted data from locked container by key. Throw on failure.
   */

   if (lockedDecryptedData_ == nullptr) {
      throw DecryptedDataContainerException("container is not locked");
   }

   auto decrKeyIter = lockedDecryptedData_->assetData_.find(id);
   if (decrKeyIter == lockedDecryptedData_->assetData_.end()) {
      throw DecryptedDataContainerException("could not get clear text data");
   }
   return decrKeyIter->second->getData();
}

////////////////////////////////////////////////////////////////////////////////
const AssetId& DecryptedDataContainer::insertClearTextAssetData(
   const uint8_t* data, size_t len)
{
   /*
   Insert random clear text data in the locked decrypted container. Use a dummy
   asset id and return it so the caller can fetch that data later.
   */

   if (lockedDecryptedData_ == nullptr)
      throw DecryptedDataContainerException("container is not locked");

   //random id
   auto dummyId = AssetId::getNextDummyId();
   SecureBinaryData sbd(len);
   memcpy(sbd.getPtr(), data, len);

   auto decrDataPtr = make_unique<ClearTextAssetData>(dummyId, sbd);

   auto insertIter = lockedDecryptedData_->assetData_.emplace(
      move(dummyId), move(decrDataPtr));
   return insertIter.first->first;
}

////////////////////////////////////////////////////////////////////////////////
EncryptionKeyId DecryptedDataContainer::populateEncryptionKey(
   const std::map<EncryptionKeyId, BinaryData>& keyMap)
{
   /*
   This method looks for existing encryption keys in the container. It will
   return if the clear text encryption key is present, or populate the
   container until it cannot find precursors (an encryption key may be
   encrypted by another encryption key). At which point, it will prompt the
   user for a passphrase.

   keyMap: <keyId, kdfId> for all eligible {key, kdf} pairs. These are listed by
   the encrypted data object that you're looking to decrypt.

   Returns the id of the key from the keyMap used for decryption.
   */

   EncryptionKeyId keyId, decryptId;
   BinaryData kdfId;

   //sanity check
   if (!ownsLock()) {
      throw DecryptedDataContainerException(
         "[DecryptedDataContainer::populateEncryptionKey]"
         " unlocked/does not own lock");
   }

   if (lockedDecryptedData_ == nullptr) {
      throw DecryptedDataContainerException(
      "nullptr lock! how did we get this far?");
   }

   //lambda to insert keys back into the container
   auto insertDecryptedData = [this](
      const EncryptionKeyId& keyid,
     std::unique_ptr<ClearTextEncryptionKey> decrKey)->void
   {
      //if decrKey is empty, all casts failed, throw
      if (decrKey == nullptr) {
         throw DecryptedDataContainerException(
         "tried to insert empty decryption key");
      }

      //make sure insertion succeeds
      lockedDecryptedData_->encryptionKeys_.erase(keyid);
      lockedDecryptedData_->encryptionKeys_.emplace(keyid, std::move(decrKey));
   };

   //look for already decrypted data
   std::unique_ptr<ClearTextEncryptionKey> decryptedKey = nullptr;
   for (auto& keyPair : keyMap) {
      auto dataIter = lockedDecryptedData_->encryptionKeys_.find(keyPair.first);
      if (dataIter != lockedDecryptedData_->encryptionKeys_.end()) {
         decryptedKey = std::move(dataIter->second);
         keyId = keyPair.first;
         kdfId = keyPair.second;
         break;
      }
   }

   if (decryptedKey == nullptr) {
      //we don't have a decrypted key, let's look for it in the encrypted map
      for (auto& keyPair : keyMap) {
         auto encrKeyIter = encryptedKeys_.find(keyPair.first);
         if (encrKeyIter != encryptedKeys_.end()) {
            //found the encrypted key, need to decrypt it first
            std::map<EncryptionKeyId, BinaryData> parentKeyMap;
            for (auto& cipherPair : encrKeyIter->second->cipherDataMap_) {
               auto cipherDataPtr = cipherPair.second.get();
               if (cipherDataPtr == nullptr) {
                  continue;
               }
               parentKeyMap.emplace(
                  cipherDataPtr->cipher_->getEncryptionKeyId(),
                  cipherDataPtr->cipher_->getKdfId()
               );
            }
            decryptId = populateEncryptionKey(parentKeyMap);

            /*
            We found an encryption key that matches our requested key id,
            cycle through its known cipher objects, looking for the kdfId
            we asked for, and decrypt it.
            */
            bool done = false;
            for (auto& cipherPair : encrKeyIter->second->cipherDataMap_) {
               //sanity check
               auto cipherDataPtr = cipherPair.second.get();
               if (cipherDataPtr == nullptr) {
                  continue;
               }

               //grab the encryption key
               const auto& encrKeyId = cipherDataPtr->cipher_->getEncryptionKeyId();
               const auto& encrKdfId = cipherDataPtr->cipher_->getKdfId();
               auto decrKeyIter = lockedDecryptedData_->encryptionKeys_.find(
                  encrKeyId);
               if (decrKeyIter == lockedDecryptedData_->encryptionKeys_.end()) {
                  continue;
               }

               //derive encryption key
               auto decryptionKey = std::move(deriveEncryptionKey(
                  std::move(decrKeyIter->second), encrKdfId));

               //decrypt encrypted key
               auto rawDecryptedKey = cipherDataPtr->cipher_->decrypt(
                  decryptionKey->getDerivedKey(encrKdfId),
                  cipherDataPtr->cipherText_);

               decryptedKey = std::make_unique<ClearTextEncryptionKey>(
                  rawDecryptedKey);

               //move decryption key back to container
               insertDecryptedData(encrKeyId, std::move(decryptionKey));
               done = true;
               break;
            }

            if (!done) {
               throw DecryptedDataContainerException("failed to decrypt key");
            }
            keyId = keyPair.first;
            kdfId = keyPair.second;
            break;
         }
      }
   }

   if (decryptedKey == nullptr) {
      //still no key, prompt the user
      decryptedKey = promptPassphrase(keyMap);
      for (auto& keyPair : keyMap) {
         if (decryptedKey->getId(keyPair.second) == keyPair.first) {
            keyId = keyPair.first;
            kdfId = keyPair.second;
            break;
         }
      }
   }

   //apply kdf
   decryptedKey = std::move(deriveEncryptionKey(std::move(decryptedKey), kdfId));

   //insert into map
   insertDecryptedData(keyId, std::move(decryptedKey));
   if (decryptId.isValid()) {
      return decryptId;
   }
   return keyId;
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData DecryptedDataContainer::encryptData(
   Cipher* const cipher, const SecureBinaryData& data)
{
   //sanity check
   if (cipher == nullptr) {
      throw DecryptedDataContainerException("null cipher");
   }

   if (!ownsLock()) {
      throw DecryptedDataContainerException(
         "[DecryptedDataContainer::encryptData]"
         " unlocked/does not own lock");
   }

   if (lockedDecryptedData_ == nullptr) {
      throw DecryptedDataContainerException(
         "nullptr lock! how did we get this far?");
   }

   std::map<EncryptionKeyId, BinaryData> keyMap {
      {cipher->getEncryptionKeyId(), cipher->getKdfId()}
   };
   populateEncryptionKey(keyMap);

   auto keyIter = lockedDecryptedData_->encryptionKeys_.find(
      cipher->getEncryptionKeyId());
   keyIter->second->getDerivedKey(cipher->getKdfId());
   return cipher->encrypt(keyIter->second.get(), cipher->getKdfId(), data);
}

////////////////////////////////////////////////////////////////////////////////
std::unique_ptr<ClearTextEncryptionKey> DecryptedDataContainer::promptPassphrase(
   const std::map<EncryptionKeyId, BinaryData>& keyMap) const
{
   if (!getPassphraseLambda_) {
      throw DecryptedDataContainerException("empty passphrase lambda");
   }

   std::set<EncryptionKeyId> keySet;
   for (auto& keyPair : keyMap) {
      keySet.insert(keyPair.first);
   }

   while (true) {
      auto passphrase = getPassphraseLambda_(keySet);
      if (passphrase.empty()) {
         throw DecryptedDataContainerException("empty passphrase");
      }

      auto keyPtr = std::make_unique<ClearTextEncryptionKey>(passphrase);
      for (auto& keyPair : keyMap) {
         keyPtr = std::move(deriveEncryptionKey(std::move(keyPtr), keyPair.second));
         if (keyPair.first == keyPtr->getId(keyPair.second)) {
            return keyPtr;
         }
      }
   }
   return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::updateOnDisk(
   shared_ptr<IO::DBIfaceTransaction> tx,
   const EncryptionKeyId& key, shared_ptr<EncryptionKey> dataPtr)
{
   //serialize db key
   auto dbKey = key.getSerializedKey(ENCRYPTIONKEY_PREFIX);
   updateOnDiskRaw(tx, dbKey, dataPtr);
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::updateOnDiskRaw(
   shared_ptr<IO::DBIfaceTransaction> tx,
   const BinaryData& dbKey, shared_ptr<EncryptionKey> dataPtr)
{
   //check if data is on disk already
   auto&& dataRef = tx->getDataRef(dbKey);

   if (!dataRef.empty())
   {
      //already have this key, is it the same data?
      auto onDiskData = EncryptionKey::deserialize(dataRef);

      //data has not changed, no need to commit
      if (onDiskData->isSame(dataPtr.get()))
         return;

      //data has changed, wipe the existing data
      deleteFromDisk(tx, dbKey);
   }

   auto&& serializedData = dataPtr->serialize();
   tx->insert(dbKey, serializedData);
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::updateOnDisk()
{
   if (getWriteTx_ == nullptr)
      throw runtime_error("empty write tx lambda");

   auto tx = getWriteTx_(dbName_);
   updateOnDisk(move(tx));
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::updateOnDisk(std::unique_ptr<IO::DBIfaceTransaction> tx)
{
   //encryption keys
   std::shared_ptr<IO::DBIfaceTransaction> sharedTx(std::move(tx));
   for (auto& key : encryptedKeys_) {
      updateOnDisk(sharedTx, key.first, key.second);
   }

   BinaryDataRef passthroughId{
      (const uint8_t*)passthroughKdfId.data(),
      passthroughKdfId.size()
   };

   //kdf
   for (const auto& key : kdfMap_) {
      if (key.first == passthroughId) {
         //do not save passthrough kdf on disk
         continue;
      }

      //get db key
      auto dbKey = WRITE_UINT8_BE(KDF_PREFIX);
      dbKey.append(key.first);

      //fetch from db
      auto dataRef = sharedTx->getDataRef(dbKey);

      if (!dataRef.empty()) {
         //already have this key, is it the same data?
         auto onDiskData = KeyDerivationFunction::deserialize(dataRef);

         //data has not changed, not commiting to disk
         if (onDiskData->isSame(key.second.get())) {
            continue;
         }
         //data has changed, wipe the existing data
         deleteFromDisk(sharedTx, dbKey);
      }

      auto serializedData = key.second->serialize();
      sharedTx->insert(dbKey, serializedData);
   }
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::deleteFromDisk(
   shared_ptr<IO::DBIfaceTransaction> tx, const BinaryData& key)
{
   //sanity checks
   if (!ownsLock())
   {
      throw DecryptedDataContainerException(
         "[DecryptedDataContainer::deleteFromDisk]"
         " unlocked/does not own lock");
   }

   //erase key, db interface will wipe it from file
   tx->erase(key);
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::readFromDisk(shared_ptr<IO::DBIfaceTransaction> tx)
{
   //encryption key and kdf entries
   auto dbIter = tx->getIterator();

   BinaryWriter bwEncrKey;
   bwEncrKey.put_uint8_t(ENCRYPTIONKEY_PREFIX);
   dbIter->seek(bwEncrKey.getData());

   while (dbIter->isValid()) {
      auto iterkey = dbIter->key();
      auto itervalue = dbIter->value();

      if (iterkey.getSize() < 2) {
         throw runtime_error("empty db key");
      }
      if (itervalue.getSize() < 1) {
         throw runtime_error("empty value");
      }
      auto prefix = (uint8_t*)iterkey.getPtr();
      switch (*prefix)
      {
         case ENCRYPTIONKEY_PREFIX:
         {
            auto keyUPtr = EncryptionKey::deserialize(itervalue);
            std::shared_ptr<EncryptionKey> keyPtr(std::move(keyUPtr));
            addEncryptionKey(keyPtr);
            break;
         }

         case KDF_PREFIX:
         {
            auto kdfPtr = KeyDerivationFunction::deserialize(itervalue);
            if (iterkey.getSliceRef(1, iterkey.getSize() - 1) != kdfPtr->getId()) {
               throw std::runtime_error("kdf id mismatch");
            }

            addKdf(kdfPtr);
            break;
         }
      }
      dbIter->advance();
   }
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::encryptEncryptionKey(
   const EncryptionKeyId& keyID,
   const BinaryData& kdfIdIn, const BinaryData& kdfIdOut,
   const std::function<SecureBinaryData(void)>& newPassLbd, bool replace)
{
   /***
   Encrypts an encryption key with newPassphrase.

   Will swap old passphrase with new one if replace is true.
   Will add the passphrase to the designated key if replace is false.

   The code detects which passphrase was used to decrypt the key prior to
   adding the new passphrase. For this purpose it needs to control the lifespan
   of the encryption lock.

   Pre-existing locks may have the relevant key already decrypted, and the
   passphrase that was used to decrypt it with will be replaced, which may not
   reflect the user's intent.

   Therefor, this method tries to SingleLock itself, and will fail if a lock is
   held elsewhere, even within the same thread.
   ***/

   if (getWriteTx_ == nullptr) {
      throw std::runtime_error("empty write tx lambda");
   }
   SingleLock lock(this);

   //we have to own the lock on this container before proceeding
   if (!ownsLock()) {
      throw DecryptedDataContainerException(
         "[DecryptedDataContainer::encryptEncryptionKey]"
         " unlocked/does not own lock");
   }

   if (lockedDecryptedData_ == nullptr) {
      throw DecryptedDataContainerException(
         "nullptr lock! how did we get this far?");
   }

   //grab encryption key object
   auto keyIter = encryptedKeys_.find(keyID);
   if (keyIter == encryptedKeys_.end()) {
      throw DecryptedDataContainerException(
         "cannot change passphrase for unknown key");
   }
   auto encryptedKey = std::dynamic_pointer_cast<EncryptionKey>(keyIter->second);

   //decrypt master encryption key
   std::map<EncryptionKeyId, BinaryData> encrKeyMap{ {keyID, kdfIdIn} };
   auto decryptionKeyId = populateEncryptionKey(encrKeyMap);

   //grab decrypted key
   auto decryptedKeyIter = lockedDecryptedData_->encryptionKeys_.find(keyID);
   if (decryptedKeyIter == lockedDecryptedData_->encryptionKeys_.end()) {
      throw DecryptedDataContainerException("failed to decrypt target key");
   }
   auto& decryptedKey = decryptedKeyIter->second->getData();

   //grab kdf for key id computation
   auto kdfIter = kdfMap_.find(kdfIdOut);
   if (kdfIter == kdfMap_.end()) {
      throw DecryptedDataContainerException("failed to grab kdf");
   }

   //grab passphrase through the lambda
   auto newPassphrase = newPassLbd();
   if (newPassphrase.empty()) {
      throw DecryptedDataContainerException("cannot set an empty passphrase");
   }

   //kdf the new passphrase to get its id
   auto newEncryptionKey = std::make_unique<ClearTextEncryptionKey>(newPassphrase);
   newEncryptionKey->deriveKey(kdfIter->second);
   auto newKeyId = newEncryptionKey->getId(kdfIdOut);

   //create new cipher, pointing to the new key and kdf by id
   auto newCipher = std::make_unique<Cipher_AES>(kdfIdOut, newKeyId);

   //add new encryption key object to container
   lockedDecryptedData_->encryptionKeys_.emplace(
      newKeyId, std::move(newEncryptionKey));

   //encrypt master key
   auto newEncryptedKey = encryptData(newCipher.get(), decryptedKey);

   //create new encrypted container
   auto newCipherData = std::make_unique<CipherData>(newEncryptedKey, move(newCipher));

   if (replace) {
      //get cipher for the key used to decrypt the wallet
      auto cipherPtr = encryptedKey->getCipherPtrForId(decryptionKeyId);
      if (cipherPtr == nullptr) {
         throw DecryptedDataContainerException("failed to find encryption key");
      }

      //remove old cipher data from the encrypted key object
      if (!encryptedKey->removeCipherData(cipherPtr->getEncryptionKeyId())) {
         throw DecryptedDataContainerException("failed to erase old encryption key");
      }
   } else {
      //check we arent adding a passphrase to an unencrypted wallet
      if (decryptionKeyId == defaultEncryptionKeyId_) {
         throw DecryptedDataContainerException(
            "cannot add passphrase to unencrypted wallet");
      }
   }

   //add new cipher data to the encrypted key object
   if (!encryptedKey->addCipherData(std::move(newCipherData))) {
      throw DecryptedDataContainerException(
         "cipher data already present in encryption key");
   }

   //write new encrypted object with backup key and commit to disk
   //we dont want to risk losing the old object at swap time without
   //a backup to recover it
   auto temp_key = keyID.getSerializedKey(ENCRYPTIONKEY_PREFIX_TEMP);
   {
      auto tx = getWriteTx_(dbName_);
      std::shared_ptr<IO::DBIfaceTransaction> sharedTx(std::move(tx));
      updateOnDiskRaw(sharedTx, temp_key, encryptedKey);
   }

   //replace old encrypted object with the new one
   auto perm_key = keyID.getSerializedKey(ENCRYPTIONKEY_PREFIX);
   {
      auto tx = getWriteTx_(dbName_);
      shared_ptr<IO::DBIfaceTransaction> sharedTx(move(tx));

      //wipe old object from disk
      deleteFromDisk(sharedTx, perm_key);

      //write new object to disk
      updateOnDiskRaw(sharedTx, perm_key, encryptedKey);
   }

   //wipe the backup entry from disk
   {
      auto tx = getWriteTx_(dbName_);
      std::shared_ptr<IO::DBIfaceTransaction> sharedTx(std::move(tx));
      deleteFromDisk(sharedTx, temp_key);
   }
}

////////////////////////////////////////////////////////////////////////////////
void DecryptedDataContainer::eraseEncryptionKey(
   const EncryptionKeyId& keyID, const BinaryData& kdfID)
{
   /***
   Removes a passphrase from an encrypted key designated by keyID. 
   
   The passphrase used to decrypt the wallet will be erased. If it is the last 
   passphrase used to encrypt the key, the key will be encrypted with the 
   default passphrase in turn.

   Has same locking requirements as encryptEncryptionKey.
   ***/

   if (getWriteTx_ == nullptr) {
      throw std::runtime_error("empty write tx lambda");
   }

   //we have to own the lock on this container before proceeding
   SingleLock lock(this);
   if (!ownsLock()) {
      throw DecryptedDataContainerException(
         "[DecryptedDataContainer::eraseEncryptionKey]"
         " unlocked/does not own lock");
   }

   if (lockedDecryptedData_ == nullptr) {
      throw DecryptedDataContainerException(
         "nullptr lock! how did we get this far?");
   }

   //grab encryption key object
   auto keyIter = encryptedKeys_.find(keyID);
   if (keyIter == encryptedKeys_.end()) {
      throw DecryptedDataContainerException(
         "cannot change passphrase for unknown key");
   }
   auto encryptedKey = std::dynamic_pointer_cast<EncryptionKey>(
      keyIter->second);

   //decrypt master encryption key
   std::map<EncryptionKeyId, BinaryData> encrKeyMap {{keyID, kdfID}};
   auto decryptionKeyId = populateEncryptionKey(encrKeyMap);

   //check key was decrypted
   auto decryptedKeyIter = lockedDecryptedData_->encryptionKeys_.find(keyID);
   if (decryptedKeyIter == lockedDecryptedData_->encryptionKeys_.end()) {
      throw DecryptedDataContainerException("failed to decrypt the key");
   }

   //sanity check on kdfID
   auto kdfIter = kdfMap_.find(kdfID);
   if (kdfIter == kdfMap_.end()) {
      throw DecryptedDataContainerException("failed to grab kdf");
   }

   //get cipher for the key used to decrypt the wallet
   auto cipherPtr = encryptedKey->getCipherPtrForId(decryptionKeyId);
   if (cipherPtr == nullptr) {
      throw DecryptedDataContainerException("failed to find encryption key");
   }

   //if the key only has 1 cipher data object left, reencrypt with the default
   //passphrase
   if (encryptedKey->cipherDataMap_.size() == 1) {
      //create new cipher, pointing to the default encryption key
      //and passthrough kdf
      auto newCipher = std::make_unique<Cipher_AES>(
         BinaryData::fromString(passthroughKdfId),
         defaultEncryptionKeyId_
      );

      //encrypt master key
      auto& decryptedKey = decryptedKeyIter->second->getData();
      auto newEncryptedKey = encryptData(newCipher.get(), decryptedKey);

      //create new encrypted container
      auto newCipherData = std::make_unique<CipherData>(
         newEncryptedKey, std::move(newCipher));

      //add new cipher data to the encrypted key object
      if (!encryptedKey->addCipherData(std::move(newCipherData))) {
         throw DecryptedDataContainerException(
            "cipher data already present in encryption key");
      }
   }

   //remove cipher data from the encrypted key object
   if (!encryptedKey->removeCipherData(cipherPtr->getEncryptionKeyId())) {
      throw DecryptedDataContainerException("failed to erase old encryption key");
   }

   //update on disk
   auto temp_key = keyID.getSerializedKey(ENCRYPTIONKEY_PREFIX_TEMP);
   auto perm_key = keyID.getSerializedKey(ENCRYPTIONKEY_PREFIX);

   {
      //write new encrypted key as temp key within it's own transaction
      auto tx = getWriteTx_(dbName_);
      std::shared_ptr<IO::DBIfaceTransaction> sharedTx(std::move(tx));
      updateOnDiskRaw(sharedTx, temp_key, encryptedKey);
   }

   {
      auto tx = getWriteTx_(dbName_);
      std::shared_ptr<IO::DBIfaceTransaction> sharedTx(std::move(tx));

      //wipe old key from disk
      deleteFromDisk(sharedTx, perm_key);

      //write new key to disk
      updateOnDiskRaw(sharedTx, perm_key, encryptedKey);
   }

   {
      //wipe temp entry
      auto tx = getWriteTx_(dbName_);
      std::shared_ptr<IO::DBIfaceTransaction> sharedTx(std::move(tx));
      deleteFromDisk(sharedTx, temp_key);
   }
}
