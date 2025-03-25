////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "AssetEncryption.h"
#include "DBUtils.h"

#define CIPHER_VERSION     0x00000001
#define KDF_ROMIX_VERSION  0x00000001

using namespace std;
using namespace Armory::Wallets;
using namespace Armory::Wallets::Encryption;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// KeyDerivationFunction
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
KeyDerivationFunction::~KeyDerivationFunction()
{}

////////////////////////////////////////////////////////////////////////////////
// KeyDerivationFunction_Romix
////////////////////////////////////////////////////////////////////////////////
KeyDerivationFunction_Romix::KeyDerivationFunction_Romix(
   const std::chrono::milliseconds& unlockTime) :
   KeyDerivationFunction(), salt_((initialize(unlockTime)))
{}

////
KeyDerivationFunction_Romix::KeyDerivationFunction_Romix(
   unsigned iterations, unsigned memTarget, SecureBinaryData salt) :
   KeyDerivationFunction(),
   iterations_(iterations), memTarget_(memTarget), salt_(move(salt))
{}

////
KeyDerivationFunction_Romix::~KeyDerivationFunction_Romix()
{}

////////////////////////////////////////////////////////////////////////////////
KdfId KeyDerivationFunction_Romix::computeID() const
{
   BinaryWriter bw;
   bw.put_BinaryData(salt_);
   bw.put_uint32_t(iterations_);
   bw.put_uint32_t(memTarget_);

   BinaryData bd(32);
   CryptoSHA2::getHash256(bw.getData(), bd.getPtr());
   return KdfId::fromBinaryData(bd);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData KeyDerivationFunction_Romix::initialize(
   const std::chrono::milliseconds& unlockTime)
{
   KdfRomix kdf;
   kdf.computeKdfParams(unlockTime);
   iterations_ = kdf.getNumIterations();
   memTarget_ = kdf.getMemoryReqtBytes();
   return kdf.getSalt();
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData KeyDerivationFunction_Romix::deriveKey(
   const SecureBinaryData& rawKey) const
{
   KdfRomix kdfObj(memTarget_, iterations_, salt_);
   return kdfObj.DeriveKey(rawKey);
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<KeyDerivationFunction> KeyDerivationFunction::deserialize(
   const BinaryDataRef& data)
{
   BinaryRefReader brr(data);

   //check size
   auto totalLen = brr.get_var_int();
   if (totalLen != brr.getSizeRemaining())
      throw runtime_error("invalid serialized kdf size");

   //return ptr
   shared_ptr<KeyDerivationFunction> kdfPtr = nullptr;

   //version
   auto version = brr.get_uint32_t();

   //check prefix
   auto prefix = brr.get_uint16_t();

   switch (prefix)
   {
   case KDF_ROMIX_PREFIX:
   {
      switch (version)
      {
      case 0x00000001:
      {
         //iterations
         auto iterations = brr.get_uint32_t();

         //memTarget
         auto memTarget = brr.get_uint32_t();

         //salt
         auto len = brr.get_var_int();
         SecureBinaryData salt(move(brr.get_BinaryData(len)));

         kdfPtr = make_shared<KeyDerivationFunction_Romix>(
            iterations, memTarget, move(salt));
         break;
      }

      default:
         throw runtime_error("unsupported kdf version");
      }

      break;
   }

   default:
      throw runtime_error("unexpected kdf prefix");
   }

   return kdfPtr;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData KeyDerivationFunction_Romix::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(KDF_ROMIX_VERSION);
   bw.put_uint16_t(KDF_ROMIX_PREFIX);
   bw.put_uint32_t(iterations_);
   bw.put_uint32_t(memTarget_);
   bw.put_var_int(salt_.getSize());
   bw.put_BinaryData(salt_);

   BinaryWriter finalBw;
   finalBw.put_var_int(bw.getSize());
   finalBw.put_BinaryDataRef(bw.getDataRef());

   return finalBw.getData();
}

////////////////////////////////////////////////////////////////////////////////
const KdfId& KeyDerivationFunction_Romix::getId() const
{
   if (!id_.isValid()) {
      id_ = std::move(computeID());
   }
   return id_;
}

////////////////////////////////////////////////////////////////////////////////
bool KeyDerivationFunction_Romix::isSame(const KeyDerivationFunction* kdf) const
{
   auto kdfromix = dynamic_cast<const KeyDerivationFunction_Romix*>(kdf);
   if (kdfromix == nullptr) {
      return false;
   }

   return iterations_ == kdfromix->iterations_ &&
      memTarget_ == kdfromix->memTarget_ &&
      salt_ == kdfromix->salt_;
}

////////////////////////////////////////////////////////////////////////////////
unsigned KeyDerivationFunction_Romix::memTarget() const
{
   return memTarget_;
}

////////
unsigned KeyDerivationFunction_Romix::iterations() const
{
   return iterations_;
}

////////
void KeyDerivationFunction_Romix::prettyPrint() const
{
   cout << "KDF Parameters:" << endl;
   cout << "   HashFunction : " << "sha512" << endl;
   cout << "   Memory/thread: " << memTarget_ << " bytes" << endl;
   cout << "   NumIterations: " << iterations_ << endl;
   cout << "   Salt         : " << salt_.toHexStr() << endl;
}

////////////////////////////////////////////////////////////////////////////////
// KeyDerivationFunction_Passthrough
////////////////////////////////////////////////////////////////////////////////
KeyDerivationFunction_Passthrough::KeyDerivationFunction_Passthrough() :
   KeyDerivationFunction(), id_{passthroughKdfId}
{}

KeyDerivationFunction_Passthrough::~KeyDerivationFunction_Passthrough()
{}

const KdfId& KeyDerivationFunction_Passthrough::getId() const
{
   return id_;
}

SecureBinaryData KeyDerivationFunction_Passthrough::deriveKey(
   const SecureBinaryData& key) const
{
   return key;
}

bool KeyDerivationFunction_Passthrough::isSame(
   const KeyDerivationFunction* rhs) const
{
   return rhs->getId() == getId();
}

BinaryData KeyDerivationFunction_Passthrough::serialize() const
{
   throw std::runtime_error("passthrough kdf cannot be serialized");
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// Cipher
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
const PRNG_Fortuna Cipher::fortuna_;

////////////////////////////////////////////////////////////////////////////////
Cipher::~Cipher()
{}

////////////////////////////////////////////////////////////////////////////////
const EncryptionKeyId& Cipher::getEncryptionKeyId() const
{
   return encryptionKeyId_;
}

////////////////////////////////////////////////////////////////////////////////
unsigned Cipher::getBlockSize(CipherType type)
{
   unsigned blockSize;
   switch (type)
   {
      case CipherType_AES:
      {
         blockSize = AES_BLOCK_SIZE;
         break;
      }

      default:
         throw std::runtime_error("cannot get block size for unexpected cipher type");
   }
   return blockSize;
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData Cipher::generateIV(void) const
{
   return fortuna_.generateRandom(getBlockSize(type_));
}

////////////////////////////////////////////////////////////////////////////////
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

////////////////////////////////////////////////////////////////////////////////
std::unique_ptr<Cipher> Cipher_AES::getCopy() const
{
   return std::make_unique<Cipher_AES>(kdfId_, encryptionKeyId_);
}

////////////////////////////////////////////////////////////////////////////////
std::unique_ptr<Cipher> Cipher_AES::getCopy(const EncryptionKeyId& keyId) const
{
   return std::make_unique<Cipher_AES>(kdfId_, keyId);
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData Cipher_AES::encrypt(ClearTextEncryptionKey* const key,
   const KdfId& kdfId, const SecureBinaryData& data) const
{
   if (key == nullptr) {
      throw std::runtime_error("null key ptr");
   }
   auto& encryptionKey = key->getDerivedKey(kdfId);

   CryptoAES cipher;
   return cipher.EncryptCBC(data, encryptionKey, iv_);
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData Cipher_AES::encrypt(ClearTextEncryptionKey* const key,
   const KdfId& kdfId, ClearTextEncryptionKey* const data) const
{
   if (data == nullptr) {
      throw std::runtime_error("null data ptr");
   }
   return encrypt(key, kdfId, data->getData());
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData Cipher_AES::decrypt(const SecureBinaryData& key,
   const SecureBinaryData& data) const
{
   CryptoAES aes_cipher;
   return aes_cipher.DecryptCBC(data, key, iv_);
}

////////////////////////////////////////////////////////////////////////////////
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

////////////////////////////////////////////////////////////////////////////////
unsigned Cipher_AES::getBlockSize(void) const
{
   return Cipher::getBlockSize(getType());
}

////////////////////////////////////////////////////////////////////////////////
//
//// CipherData
//
////////////////////////////////////////////////////////////////////////////////
CipherData::CipherData(SecureBinaryData& cipherText,
   unique_ptr<Cipher> cipher) :
   cipherText_(move(cipherText)), cipher_(move(cipher))
{
   if (cipherText_.empty()) {
      throw CipherException("empty cipher text");
   }

   if (cipher_ == nullptr) {
      throw CipherException("null cipher for privkey");
   }
}

////////////////////////////////////////////////////////////////////////////////
bool CipherData::isSame(CipherData* const rhs) const
{
   return cipherText_ == rhs->cipherText_ &&
      cipher_->isSame(rhs->cipher_.get());
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CipherData::serialize(void) const
{
   BinaryWriter bw;
   bw.put_uint32_t(CIPHER_DATA_VERSION);

   bw.put_var_int(cipherText_.getSize());
   bw.put_BinaryData(cipherText_);

   auto&& data = cipher_->serialize();
   bw.put_var_int(data.getSize());
   bw.put_BinaryData(data);

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<CipherData> CipherData::deserialize(BinaryRefReader& brr)
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

         auto cipherText = brr.get_SecureBinaryData(len);
         len = brr.get_var_int();
         if (len > brr.getSizeRemaining()) {
            throw CipherException("invalid cipher length");
         }

         auto cipher = Cipher::deserialize(brr);
         cipherDataPtr = std::make_unique<CipherData>(
            cipherText, std::move(cipher));
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
//
//// EncryptionKey
//
////////////////////////////////////////////////////////////////////////////////
EncryptionKey::EncryptionKey(EncryptionKeyId& id,
   SecureBinaryData& cipherText,
   unique_ptr<Cipher> cipher) :
   id_(move(id))
{
   auto cipherData = make_unique<CipherData>(cipherText, move(cipher));
   cipherDataMap_.emplace(
      cipherData->cipher_->getEncryptionKeyId(), move(cipherData));
}

////////////////////////////////////////////////////////////////////////////////
EncryptionKey::EncryptionKey(EncryptionKeyId& id,
   map<EncryptionKeyId, unique_ptr<CipherData>> cipherDataMap) :
   id_(move(id)),
   cipherDataMap_(move(cipherDataMap))
{}

////////////////////////////////////////////////////////////////////////////////
bool EncryptionKey::isSame(EncryptionKey* const keyPtr) const
{
   if (keyPtr == nullptr)
      return false;

   if (id_ != keyPtr->id_)
      return false;

   if (cipherDataMap_.size() != keyPtr->cipherDataMap_.size())
      return false;

   for (auto& cipherData : cipherDataMap_)
   {
      auto cdIter = keyPtr->cipherDataMap_.find(cipherData.first);
      if (cdIter == keyPtr->cipherDataMap_.end())
         return false;

      if (!cipherData.second->isSame(cdIter->second.get()))
         return false;
   }

   return true;
}

////////////////////////////////////////////////////////////////////////////////
Cipher* EncryptionKey::getCipherPtrForId(const EncryptionKeyId& id) const
{
   auto iter = cipherDataMap_.find(id);
   if (iter == cipherDataMap_.end())
      return nullptr;

   return iter->second->cipher_.get();
}

////////////////////////////////////////////////////////////////////////////////
bool EncryptionKey::removeCipherData(const EncryptionKeyId& id)
{
   return cipherDataMap_.erase(id) == 1;
}

////////////////////////////////////////////////////////////////////////////////
bool EncryptionKey::addCipherData(std::unique_ptr<CipherData> dataPtr)
{
   auto insertIter = cipherDataMap_.emplace(
      dataPtr->cipher_->getEncryptionKeyId(), move(dataPtr));
   return insertIter.second;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData EncryptionKey::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(ENCRYPTION_KEY_VERSION);
   bw.put_uint8_t(ENCRYPTIONKEY_BYTE);
   id_.serializeValue(bw);

   bw.put_var_int(cipherDataMap_.size());

   for (auto& dataPair : cipherDataMap_)
   {
      auto&& cipherData = dataPair.second->serialize();
      bw.put_var_int(cipherData.getSize());
      bw.put_BinaryData(cipherData);
   }

   BinaryWriter finalBw;
   finalBw.put_var_int(bw.getSize());
   finalBw.put_BinaryDataRef(bw.getDataRef());
   return finalBw.getData();
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<EncryptionKey> EncryptionKey::deserialize(const BinaryDataRef& data)
{
   BinaryRefReader brr(DBUtils::getDataRefForPacket(data));
   unique_ptr<EncryptionKey> keyPtr;

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
         map<EncryptionKeyId, unique_ptr<CipherData>> cipherMap;
         auto count = brr.get_var_int();
         for (unsigned i = 0; i < count; i++)
         {
            auto len = brr.get_var_int();
            if (len > brr.getSizeRemaining())
               throw runtime_error("invalid serialized encrypted data len");

            auto cipherBdr = brr.get_BinaryDataRef(len);
            BinaryRefReader cipherBrr(cipherBdr);

            auto cipherData = CipherData::deserialize(cipherBrr);
            cipherMap.insert(make_pair(
               cipherData->cipher_->getEncryptionKeyId(), std::move(cipherData)));
         }

         //ptr
         keyPtr = make_unique<EncryptionKey>(id, move(cipherMap));
         break;
      }

      default:
         throw runtime_error("unsupported encryption key version");
      }

      break;
   }

   default:
      throw runtime_error("unexpected encrypted key prefix");
   }

   if (keyPtr == nullptr)
      throw runtime_error("failed to deserialize encrypted asset");

   return keyPtr;
}

////////////////////////////////////////////////////////////////////////////////
std::set<KdfId> EncryptionKey::getKdfIds() const
{
   std::set<KdfId> result;
   for (const auto& cipherData : cipherDataMap_) {
      result.emplace(cipherData.second->cipher_->getKdfId());
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
//
//// ClearTextEncryptionKey
//
////////////////////////////////////////////////////////////////////////////////
void ClearTextEncryptionKey::deriveKey(
   std::shared_ptr<Encryption::KeyDerivationFunction> kdf)
{
   if (derivedKeys_.find(kdf->getId()) != derivedKeys_.end()) {
      return;
   }
   auto derivedkey = kdf->deriveKey(rawKey_);
   derivedKeys_.emplace(kdf->getId(), std::move(derivedkey));
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<ClearTextEncryptionKey> ClearTextEncryptionKey::copy() const
{
   auto key_copy = rawKey_;
   auto copy_ptr = std::make_unique<ClearTextEncryptionKey>(key_copy);
   copy_ptr->derivedKeys_ = derivedKeys_;
   return copy_ptr;
}

////////////////////////////////////////////////////////////////////////////////
EncryptionKeyId ClearTextEncryptionKey::getId(const KdfId& kdfId) const
{
   const auto keyIter = derivedKeys_.find(kdfId);
   if (keyIter == derivedKeys_.end()) {
      throw std::runtime_error("couldn't find derivation for kdfid");
   }
   return computeId(keyIter->second);
}

////////////////////////////////////////////////////////////////////////////////
EncryptionKeyId ClearTextEncryptionKey::computeId(
   const SecureBinaryData& key) const
{
   //treat value as scalar, get pubkey for it
   auto hashedKey = BtcUtils::hash256(key);
   auto pubkey = CryptoECDSA().ComputePublicKey(hashedKey);

   //HMAC the pubkey, get last 16 bytes as ID
   return EncryptionKeyId(
      BtcUtils::computeDataId(pubkey, HMAC_KEY_ENCRYPTIONKEYS));
}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& ClearTextEncryptionKey::getDerivedKey(
   const KdfId& id) const
{
   auto iter = derivedKeys_.find(id);
   if (iter == derivedKeys_.end()) {
      throw runtime_error("invalid key");
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
//
//// EncryptedAssetData
//
////////////////////////////////////////////////////////////////////////////////
EncryptedAssetData::~EncryptedAssetData()
{}

////////////////////////////////////////////////////////////////////////////////
bool EncryptedAssetData::hasData() const
{
   return cipherData_ != nullptr;
}

////////////////////////////////////////////////////////////////////////////////
const CipherData* EncryptedAssetData::getCipherDataPtr() const
{
   if (!hasData())
      throw runtime_error("no cypher data");
   return cipherData_.get();
}

////////////////////////////////////////////////////////////////////////////////
std::unique_ptr<ClearTextAssetData> EncryptedAssetData::decrypt(
   const SecureBinaryData& key) const
{
   auto cipherDataPtr = getCipherDataPtr();
   auto decryptedData = cipherDataPtr->cipher_->decrypt(
      key, cipherDataPtr->cipherText_);
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

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& EncryptedAssetData::getCipherText() const
{
   auto ptr = getCipherDataPtr();
   return ptr->cipherText_;
}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& EncryptedAssetData::getIV() const
{
   auto ptr = getCipherDataPtr();
   return ptr->cipher_->getIV();
}

////////////////////////////////////////////////////////////////////////////////
const EncryptionKeyId& EncryptedAssetData::getEncryptionKeyId() const
{
   auto ptr = getCipherDataPtr();
   return ptr->cipher_->getEncryptionKeyId();
}

////////////////////////////////////////////////////////////////////////////////
const KdfId& EncryptedAssetData::getKdfId() const
{
   auto ptr = getCipherDataPtr();
   return ptr->cipher_->getKdfId();
}
