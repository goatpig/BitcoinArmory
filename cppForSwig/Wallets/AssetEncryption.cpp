////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Utils/Cryptography.h"
#include "Utils/DBUtils.h"
#include "Utils/BtcUtils.h"
#include "AssetEncryption.h"
#include "KDF.h"
#include "GetPassphrase.h"

#define CIPHER_VERSION     0x00000001

using namespace Armory;
using namespace Armory::Wallets::Encryption;

namespace
{
   Cryptography::PRNG::Fortuna fortuna;
}

CipherException::CipherException(const std::string& msg) :
   std::runtime_error(msg)
{}

////////////////////////////////////////////////////////////////////////////////
// Cipher
Cipher::Cipher(CipherType type,
   const KdfId& kdfId, const EncryptionKeyId& keyId) :
   type_(type), kdfId_(kdfId), encryptionKeyId_(keyId),
   iv_(generateIV())
{
   //General purpose ctor, IV is generated on the fly
}

Cipher::Cipher(CipherType type,
   const KdfId& kdfId, const EncryptionKeyId& keyId,
   SecureBinaryData& iv) :
   type_(type), kdfId_(kdfId), encryptionKeyId_(keyId),
   iv_(std::move(iv))
{
   //for setting up a cipher object from existing
   //data (i.e. load from disk)
   if (iv_.getSize() != getBlockSize()) {
      throw CipherException("invalid iv length");
   }
}

Cipher::~Cipher()
{}

////////
const Wallets::EncryptionKeyId& Cipher::getEncryptionKeyId() const
{
   return encryptionKeyId_;
}

unsigned Cipher::getBlockSize(CipherType type)
{
   unsigned blockSize;
   switch (type)
   {
      case CipherType_AES:
      {
         blockSize = Cryptography::Encryption::AES::BLOCK_SIZE;
         break;
      }

      default:
         throw std::runtime_error("cannot get block size for unexpected cipher type");
   }
   return blockSize;
}

SecureBinaryData Cipher::generateIV(void) const
{
   return fortuna.generateRandom(getBlockSize(type_));
}

CipherType Cipher::getType() const
{
   return type_;
}

const Wallets::KdfId& Cipher::getKdfId() const
{
   return kdfId_;
}

const SecureBinaryData& Cipher::getIV() const
{
   return iv_;
}

unsigned Cipher::getBlockSize() const
{
   return getBlockSize(getType());
}

////////
std::unique_ptr<Cipher> Cipher::deserialize(BinaryRefReader& brr)
{
   std::unique_ptr<Cipher> cipher;
   auto version = brr.get_uint32_t();

   switch (version)
   {
      case 0x00000001:
      {
         auto prefix = brr.get_uint8_t();
         if (prefix != CIPHER_BYTE) {
            throw std::runtime_error("invalid serialized cipher prefix");
         }
         auto type = brr.get_uint8_t();

         auto len = brr.get_var_int();
         auto kdfBd = brr.get_BinaryData(len);
         auto kdfId = KdfId::fromBinaryData(kdfBd);

         len = brr.get_var_int();
         auto encryptionKeyId = brr.get_BinaryData(len);

         len = brr.get_var_int();
         auto iv = SecureBinaryData(brr.get_BinaryDataRef(len));

         switch (type)
         {
            case CipherType_AES:
            {
               cipher = std::move(std::make_unique<Cipher_AES>(
                  kdfId, encryptionKeyId, iv));
               break;
            }

            default:
               throw CipherException("unexpected cipher type");
         }
         break;
      }

      default:
         throw CipherException("unknown cipher version");
   }
   return cipher;
}

////////////////////////////////////////////////////////////////////////////////
// Cipher_AES
Cipher_AES::Cipher_AES(const KdfId& kdfId,
   const EncryptionKeyId& encryptionKeyId) :
   Cipher(CipherType_AES, kdfId, encryptionKeyId)
{}

Cipher_AES::Cipher_AES(const KdfId& kdfId,
   const EncryptionKeyId& encryptionKeyId,
   SecureBinaryData& iv) :
   Cipher(CipherType_AES, kdfId, encryptionKeyId, iv)
{}

////////
BinaryData Cipher_AES::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(CIPHER_VERSION);

   bw.put_uint8_t(CIPHER_BYTE);
   bw.put_uint8_t(getType());

   bw.put_var_int(kdfId_.data().getSize());
   bw.put_BinaryData(kdfId_.data());

   encryptionKeyId_.serializeValue(bw);

   bw.put_var_int(iv_.getSize());
   bw.put_BinaryData(iv_);

   return bw.getData();
}

////////
std::unique_ptr<Cipher> Cipher_AES::getCopy() const
{
   return std::make_unique<Cipher_AES>(kdfId_, encryptionKeyId_);
}

std::unique_ptr<Cipher> Cipher_AES::getCopy(const EncryptionKeyId& keyId) const
{
   return std::make_unique<Cipher_AES>(kdfId_, keyId);
}

////////
SecureBinaryData Cipher_AES::encrypt(ClearTextEncryptionKey* const key,
   const KdfId& kdfId, const SecureBinaryData& data) const
{
   if (key == nullptr) {
      throw std::runtime_error("null key ptr");
   }
   const auto& encryptionKey = key->getDerivedKey(kdfId);
   return Cryptography::Encryption::AES::encryptCBC(data, encryptionKey, iv_);
}

SecureBinaryData Cipher_AES::encrypt(ClearTextEncryptionKey* const key,
   const KdfId& kdfId, ClearTextEncryptionKey* const data) const
{
   if (data == nullptr) {
      throw std::runtime_error("null data ptr");
   }
   return encrypt(key, kdfId, data->getData());
}

SecureBinaryData Cipher_AES::decrypt(const SecureBinaryData& key,
   const SecureBinaryData& data) const
{
   return Cryptography::Encryption::AES::decryptCBC(data, key, iv_);
}

////////
bool Cipher_AES::isSame(Cipher* const cipher) const
{
   auto cipher_aes = dynamic_cast<Cipher_AES*>(cipher);
   if (cipher_aes == nullptr) {
      return false;
   }

   return kdfId_ == cipher_aes->kdfId_ &&
      encryptionKeyId_ == cipher_aes->encryptionKeyId_ &&
      iv_ == cipher_aes->iv_;
}

////////
unsigned Cipher_AES::getBlockSize(void) const
{
   return Cipher::getBlockSize(getType());
}

////////////////////////////////////////////////////////////////////////////////
// CipherData
CipherData::CipherData(SecureBinaryData& pantoText,
   std::unique_ptr<Cipher> pantoliano) :
   cipherText(std::move(pantoText)), cipher(std::move(pantoliano))
{
   if (cipherText.empty()) {
      throw CipherException("empty cipher text");
   }

   if (cipher == nullptr) {
      throw CipherException("null cipher for privkey");
   }
}

////////
bool CipherData::isSame(CipherData* const rhs) const
{
   return cipherText == rhs->cipherText && cipher->isSame(rhs->cipher.get());
}

////////
BinaryData CipherData::serialize(void) const
{
   BinaryWriter bw;
   bw.put_uint32_t(CIPHER_DATA_VERSION);

   bw.put_var_int(cipherText.getSize());
   bw.put_BinaryData(cipherText);

   auto&& data = cipher->serialize();
   bw.put_var_int(data.getSize());
   bw.put_BinaryData(data);

   return bw.getData();
}

std::unique_ptr<CipherData> CipherData::deserialize(BinaryRefReader& brr)
{
   std::unique_ptr<CipherData> cipherDataPtr = nullptr;

   auto version = brr.get_uint32_t();
   switch (version)
   {
      case 0x00000001:
      {
         auto len = brr.get_var_int();
         if (len > brr.getSizeRemaining()) {
            throw CipherException("invalid ciphertext length");
         }

         SecureBinaryData cipherText{brr.get_BinaryDataRef(len)};
         len = brr.get_var_int();
         if (len > brr.getSizeRemaining()) {
            throw CipherException("invalid cipher length");
         }

         auto pantoliano = Cipher::deserialize(brr);
         cipherDataPtr = std::make_unique<CipherData>(
            cipherText, std::move(pantoliano));
         break;
      }

      default:
         throw CipherException("unsupported cipher data version");
   }

   if (cipherDataPtr == nullptr) {
      throw CipherException("failed to deser cipher data");
   }
   return cipherDataPtr;
}

////////////////////////////////////////////////////////////////////////////////
// EncryptionKey
EncryptionKey::EncryptionKey(EncryptionKeyId& id,
   SecureBinaryData& cipherText,
   std::unique_ptr<Cipher> cipher) :
   id_(std::move(id))
{
   auto cipherData = std::make_unique<CipherData>(
      cipherText, std::move(cipher));
   cipherDataMap_.emplace(
      cipherData->cipher->getEncryptionKeyId(),
      std::move(cipherData));
}

EncryptionKey::EncryptionKey(EncryptionKeyId& id,
   std::map<EncryptionKeyId, std::unique_ptr<CipherData>> cipherDataMap) :
   id_(std::move(id)),
   cipherDataMap_(std::move(cipherDataMap))
{}

////////
bool EncryptionKey::isSame(EncryptionKey* const keyPtr) const
{
   if (keyPtr == nullptr) {
      return false;
   }
   if (id_ != keyPtr->id_) {
      return false;
   }
   if (cipherDataMap_.size() != keyPtr->cipherDataMap_.size()) {
      return false;
   }
   for (auto& cipherData : cipherDataMap_) {
      auto cdIter = keyPtr->cipherDataMap_.find(cipherData.first);
      if (cdIter == keyPtr->cipherDataMap_.end()) {
         return false;
      }
      if (!cipherData.second->isSame(cdIter->second.get())) {
         return false;
      }
   }
   return true;
}

////////
Cipher* EncryptionKey::getCipherPtrForId(const EncryptionKeyId& id) const
{
   auto iter = cipherDataMap_.find(id);
   if (iter == cipherDataMap_.end()) {
      return nullptr;
   }
   return iter->second->cipher.get();
}

bool EncryptionKey::removeCipherData(const EncryptionKeyId& id)
{
   return cipherDataMap_.erase(id) == 1;
}

bool EncryptionKey::addCipherData(std::unique_ptr<CipherData> dataPtr)
{
   auto insertIter = cipherDataMap_.emplace(
      dataPtr->cipher->getEncryptionKeyId(), move(dataPtr));
   return insertIter.second;
}

////////
BinaryData EncryptionKey::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(ENCRYPTION_KEY_VERSION);
   bw.put_uint8_t(ENCRYPTIONKEY_BYTE);
   id_.serializeValue(bw);

   bw.put_var_int(cipherDataMap_.size());

   for (auto& dataPair : cipherDataMap_) {
      auto cipherData = dataPair.second->serialize();
      bw.put_var_int(cipherData.getSize());
      bw.put_BinaryData(cipherData);
   }

   BinaryWriter finalBw;
   finalBw.put_var_int(bw.getSize());
   finalBw.put_BinaryDataRef(bw.getDataRef());
   return finalBw.getData();
}

std::unique_ptr<EncryptionKey> EncryptionKey::deserialize(
   const BinaryDataRef& data)
{
   BinaryRefReader brr(DBUtils::getDataRefForPacket(data));
   std::unique_ptr<EncryptionKey> keyPtr;

   //version
   auto version = brr.get_uint32_t();

   //prefix
   auto prefix = brr.get_uint8_t();

   switch (prefix)
   {
      case ENCRYPTIONKEY_BYTE:
      {
         switch (version)
         {
            case 0x00000001:
            {
               //id
               auto id = EncryptionKeyId::deserializeValue(brr);

               //cipher data
               std::map<EncryptionKeyId, std::unique_ptr<CipherData>> cipherMap;
               auto count = brr.get_var_int();
               for (unsigned i = 0; i < count; i++) {
                  auto len = brr.get_var_int();
                  if (len > brr.getSizeRemaining()) {
                     throw std::runtime_error(
                        "invalid serialized encrypted data len");
                  }
                  auto cipherBdr = brr.get_BinaryDataRef(len);
                  BinaryRefReader cipherBrr(cipherBdr);

                  auto cipherData = CipherData::deserialize(cipherBrr);
                  cipherMap.emplace(
                     cipherData->cipher->getEncryptionKeyId(),
                     std::move(cipherData));
               }

               //ptr
               keyPtr = std::make_unique<EncryptionKey>(id, std::move(cipherMap));
               break;
            }

            default:
               throw std::runtime_error("unsupported encryption key version");
         }

         break;
      }

      default:
         throw std::runtime_error("unexpected encrypted key prefix");
   }

   if (keyPtr == nullptr) {
      throw std::runtime_error("failed to deserialize encrypted asset");
   }
   return keyPtr;
}

////////////////////////////////////////////////////////////////////////////////
// ClearTextEncryptionKey
ClearTextEncryptionKey::ClearTextEncryptionKey(SecureBinaryData& key) :
   rawKey_(std::move(key))
{}

ClearTextEncryptionKey::ClearTextEncryptionKey(Passphrase::SetNew& setNewObj) :
   rawKey_(std::move(setNewObj.moveParams()->passphrase))
{}

////////
std::unique_ptr<ClearTextEncryptionKey> ClearTextEncryptionKey::copy() const
{
   auto key_copy = rawKey_;
   auto copy_ptr = std::make_unique<ClearTextEncryptionKey>(key_copy);
   copy_ptr->derivedKeys_ = derivedKeys_;
   return copy_ptr;
}

////////
Wallets::EncryptionKeyId ClearTextEncryptionKey::getId(const KdfId& kdfId) const
{
   const auto keyIter = derivedKeys_.find(kdfId);
   if (keyIter == derivedKeys_.end()) {
      throw std::runtime_error("couldn't find derivation for kdfid");
   }
   return computeId(keyIter->second);
}

Wallets::EncryptionKeyId ClearTextEncryptionKey::computeId(
   const SecureBinaryData& key) const
{
   //treat value as scalar, get pubkey for it
   auto hashedKey = BtcUtils::getHash256(key);
   auto pubkey = Cryptography::ECDSA::computePublicKey(hashedKey.getRef());

   //HMAC the pubkey, get last 16 bytes as ID
   return EncryptionKeyId(
      BtcUtils::computeDataId(pubkey, HMAC_KEY_ENCRYPTIONKEYS));
}

////////
bool ClearTextEncryptionKey::hasData() const
{
   return !rawKey_.empty();
}

const SecureBinaryData& ClearTextEncryptionKey::getData() const
{
   return rawKey_;
}

////////
void ClearTextEncryptionKey::deriveKey(
   std::shared_ptr<Encryption::KeyDerivationFunction> kdf)
{
   if (derivedKeys_.find(kdf->getId()) != derivedKeys_.end()) {
      return;
   }
   auto derivedkey = kdf->deriveKey(rawKey_);
   derivedKeys_.emplace(kdf->getId(), std::move(derivedkey));
}

const SecureBinaryData& ClearTextEncryptionKey::getDerivedKey(
   const KdfId& id) const
{
   auto iter = derivedKeys_.find(id);
   if (iter == derivedKeys_.end()) {
      throw std::runtime_error("invalid key");
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
// ClearTextAssetData
ClearTextAssetData::ClearTextAssetData(const Wallets::AssetId& id,
   SecureBinaryData& clearText) :
   id_(id), clearText_(std::move(clearText))
{}

////////
bool ClearTextAssetData::hasData() const
{
   return !clearText_.empty();
}

const Wallets::AssetId& ClearTextAssetData::getId() const
{
   return id_;
}

const SecureBinaryData& ClearTextAssetData::getData() const
{
   return clearText_;
}

////////////////////////////////////////////////////////////////////////////////
// EncryptedAssetData
EncryptedAssetData::EncryptedAssetData(std::unique_ptr<CipherData> cipherData) :
   encryptionKeyId_(cipherData->cipher->getEncryptionKeyId()),
   cipherData_(std::move(cipherData))
{
   if (cipherData_ == nullptr) {
      throw std::runtime_error("nullptr cipher data");
   }
}

EncryptedAssetData::~EncryptedAssetData()
{}

////////
bool EncryptedAssetData::hasData() const
{
   return cipherData_ != nullptr;
}

const CipherData* EncryptedAssetData::getCipherDataPtr() const
{
   if (!hasData()) {
      throw std::runtime_error("no cypher data");
   }
   return cipherData_.get();
}

////////
std::unique_ptr<ClearTextAssetData> EncryptedAssetData::decrypt(
   const SecureBinaryData& key) const
{
   auto cipherDataPtr = getCipherDataPtr();
   auto decryptedData = cipherDataPtr->cipher->decrypt(
      key, cipherDataPtr->cipherText);
   auto decrPtr = std::make_unique<ClearTextAssetData>(getAssetId(), decryptedData);
   return decrPtr;
}

bool EncryptedAssetData::isSame(EncryptedAssetData* const asset) const
{
   if (asset == nullptr) {
      return false;
   }
   return cipherData_->isSame(asset->cipherData_.get());
}

const SecureBinaryData& EncryptedAssetData::getCipherText() const
{
   auto ptr = getCipherDataPtr();
   return ptr->cipherText;
}

const SecureBinaryData& EncryptedAssetData::getIV() const
{
   auto ptr = getCipherDataPtr();
   return ptr->cipher->getIV();
}

const Wallets::EncryptionKeyId& EncryptedAssetData::getEncryptionKeyId() const
{
   auto ptr = getCipherDataPtr();
   return ptr->cipher->getEncryptionKeyId();
}

const Wallets::KdfId& EncryptedAssetData::getKdfId() const
{
   auto ptr = getCipherDataPtr();
   return ptr->cipher->getKdfId();
}
