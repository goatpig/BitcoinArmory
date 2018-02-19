////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "AssetEncryption.h"
#include "Assets.h"
#include "make_unique.h"

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// KeyDerivationFunction
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
KeyDerivationFunction::~KeyDerivationFunction()
{}

////////////////////////////////////////////////////////////////////////////////
BinaryData KeyDerivationFunction_Romix::computeID() const
{
   BinaryWriter bw;
   bw.put_BinaryData(salt_);
   bw.put_uint32_t(iterations_);
   bw.put_uint32_t(memTarget_);

   return BtcUtils::getHash256(bw.getData());
}

////////////////////////////////////////////////////////////////////////////////
BinaryData KeyDerivationFunction_Romix::initialize()
{
   KdfRomix kdf;
   kdf.computeKdfParams(0);
   iterations_ = kdf.getNumIterations();
   memTarget_ = kdf.getMemoryReqtBytes();
   return kdf.getSalt();
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData KeyDerivationFunction_Romix::deriveKey(
   const SecureBinaryData& rawKey) const
{
   KdfRomix kdfObj(memTarget_, iterations_, salt_);
   return move(kdfObj.DeriveKey(rawKey));
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

   //check prefix
   auto prefix = brr.get_uint16_t();

   switch (prefix)
   {
   case KDF_ROMIX_PREFIX:
   {
      //iterations
      auto iterations = brr.get_uint32_t();

      //memTarget
      auto memTarget = brr.get_uint32_t();

      //salt
      auto len = brr.get_var_int();
      SecureBinaryData salt(move(brr.get_BinaryData(len)));

      kdfPtr = make_shared<KeyDerivationFunction_Romix>(
         iterations, memTarget, salt);
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
const BinaryData& KeyDerivationFunction_Romix::getId(void) const
{
   if (id_.getSize() == 0)
      id_ = move(computeID());
   return id_;
}

////////////////////////////////////////////////////////////////////////////////
bool KeyDerivationFunction_Romix::isSame(KeyDerivationFunction* const kdf) const
{
   auto kdfromix = dynamic_cast<KeyDerivationFunction_Romix*>(kdf);
   if (kdfromix == nullptr)
      return false;

   return iterations_ == kdfromix->iterations_ &&
      memTarget_ == kdfromix->memTarget_ &&
      salt_ == kdfromix->salt_;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// Cypher
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
Cypher::~Cypher()
{}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<Cypher> Cypher::deserialize(BinaryRefReader& brr)
{
   unique_ptr<Cypher> cypher;
   auto prefix = brr.get_uint8_t();
   if (prefix != CYPHER_BYTE)
      throw runtime_error("invalid serialized cypher prefix");

   auto type = brr.get_uint8_t();

   auto len = brr.get_var_int();
   auto&& kdfId = brr.get_BinaryData(len);

   len = brr.get_var_int();
   auto&& encryptionKeyId = brr.get_BinaryData(len);

   len = brr.get_var_int();
   auto&& iv = SecureBinaryData(brr.get_BinaryDataRef(len));

   switch (type)
   {
   case CypherType_AES:
   {
      cypher = move(make_unique<Cypher_AES>(
         kdfId, encryptionKeyId, iv));

      break;
   }

   default:
      throw CypherException("unexpected cypher type");
   }

   return move(cypher);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Cypher_AES::serialize() const
{
   BinaryWriter bw;
   bw.put_uint8_t(CYPHER_BYTE);
   bw.put_uint8_t(getType());

   bw.put_var_int(kdfId_.getSize());
   bw.put_BinaryData(kdfId_);

   bw.put_var_int(encryptionKeyId_.getSize());
   bw.put_BinaryData(encryptionKeyId_);

   bw.put_var_int(iv_.getSize());
   bw.put_BinaryData(iv_);

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<Cypher> Cypher_AES::getCopy() const
{
   return make_unique<Cypher_AES>(kdfId_, encryptionKeyId_);
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<Cypher> Cypher_AES::getCopy(const BinaryData& keyId) const
{
   return make_unique<Cypher_AES>(kdfId_, keyId);
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData Cypher_AES::encrypt(const SecureBinaryData& key,
   const SecureBinaryData& data) const
{
   CryptoAES aes_cypher;
   return aes_cypher.EncryptCBC(data, key, iv_);
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData Cypher_AES::encrypt(DecryptedEncryptionKey* const key,
   const BinaryData& kdfId, const SecureBinaryData& data) const
{
   if (key == nullptr)
      throw runtime_error("empty ptr");

   auto& encryptionKey = key->getDerivedKey(kdfId);

   CryptoAES aes_cypher;
   return aes_cypher.EncryptCBC(data, encryptionKey, iv_);
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData Cypher_AES::decrypt(const SecureBinaryData& key,
   const SecureBinaryData& data) const
{
   CryptoAES aes_cypher;
   return aes_cypher.DecryptCBC(data, key, iv_);
}

////////////////////////////////////////////////////////////////////////////////
bool Cypher_AES::isSame(Cypher* const cypher) const
{
   auto cypher_aes = dynamic_cast<Cypher_AES*>(cypher);
   if (cypher_aes == nullptr)
      return false;

   return kdfId_ == cypher_aes->kdfId_ &&
      encryptionKeyId_ == cypher_aes->encryptionKeyId_ &&
      iv_ == cypher_aes->iv_;
}
