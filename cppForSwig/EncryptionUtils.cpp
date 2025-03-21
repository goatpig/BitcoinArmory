////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#include "EncryptionUtils.h"
#include "log.h"
#include <btc/ecc.h>
#include <btc/sha2.h>
#include <btc/hash.h>
#include <btc/ripemd160.h>
#include <btc/ctaes.h>
#include <btc/hmac.h>
#include <secp256k1.h>

using namespace std;

#define CRYPTO_DEBUG false

const string CryptoECDSA::bitcoinMessageMagic_("Bitcoin Signed Message:\n");
static secp256k1_context* crypto_ecdsa_ctx = nullptr;

/////////////////////////////////////////////////////////////////////////////
//// CryptoPRNG
/////////////////////////////////////////////////////////////////////////////
SecureBinaryData CryptoPRNG::generateRandom(uint32_t numBytes,
   const SecureBinaryData& extraEntropy)
{
   SecureBinaryData sbd(numBytes);
   btc_random_init();
   if (!btc_random_bytes(sbd.getPtr(), numBytes, 0))
      throw runtime_error("failed to generate random value");

   if (extraEntropy.getSize() != 0)
      sbd.XOR(extraEntropy);

   return sbd;
}

/////////////////////////////////////////////////////////////////////////////
//// PRNG_Fortuna
/////////////////////////////////////////////////////////////////////////////
PRNG_Fortuna::PRNG_Fortuna()
{
   reseed();
}

/////////////////////////////////////////////////////////////////////////////
void PRNG_Fortuna::reseed() const
{
   nBytes_.store(0, memory_order_relaxed);
   auto rng = CryptoPRNG::generateRandom(32);

   auto newKey = make_shared<SecureBinaryData>(32);
   unsigned char digest[32];
   sha256_Raw(rng.getPtr(), rng.getSize(), digest);
   sha256_Raw(digest, 32, newKey->getPtr());

   atomic_store_explicit(&key_, newKey, memory_order_relaxed);
}

/////////////////////////////////////////////////////////////////////////////
SecureBinaryData PRNG_Fortuna::generateRandom(uint32_t numBytes,
   const SecureBinaryData& extraEntropy) const
{
   size_t blockCount = numBytes / AES_BLOCK_SIZE;
   size_t spill = numBytes % AES_BLOCK_SIZE;
   SecureBinaryData result(numBytes);

   unsigned char plainText[AES_BLOCK_SIZE];
   memset(&plainText, 0, AES_BLOCK_SIZE);

   //setup AES object, seed with key_
   auto keyPtr = atomic_load_explicit(&key_, memory_order_relaxed);
   AES256_ctx aes_ctx;
   AES256_init(&aes_ctx, keyPtr->getPtr());

   //main body
   for (unsigned i=0; i<blockCount; i++)
   {
      //set counters in plain text block
      for (unsigned y=0; y<4; y++)
      {
         auto ptr = (uint32_t*)plainText;
         *ptr = counter_.fetch_add(1, memory_order_relaxed);
      }

      //encrypt counters with key
      auto resultPtr = result.getPtr() + (i * AES_BLOCK_SIZE);
      AES256_encrypt(&aes_ctx, 1, resultPtr, plainText);

      //xor with extra entropy if available
      if (extraEntropy.getSize() >= (i + 1) * AES_BLOCK_SIZE)
      {
         auto entropyPtr = extraEntropy.getPtr() + (i * AES_BLOCK_SIZE);
         for (unsigned z=0; z<AES_BLOCK_SIZE; z++)
            resultPtr[z] ^= entropyPtr[z];
      }
   }

   //return if we have no spill
   if (spill > 0)
   {
      //deal with spill block
      for (unsigned y=0; y<4; y++)
      {
         auto ptr = (uint32_t*)plainText;
         *ptr = counter_.fetch_add(1, memory_order_relaxed);
      }

      unsigned char lastBlock[AES_BLOCK_SIZE];
      AES256_encrypt(&aes_ctx, 1, lastBlock, plainText); 
      
      if (extraEntropy.getSize() >= numBytes)
      {
         auto entropyPtr = 
            extraEntropy.getPtr() + blockCount * AES_BLOCK_SIZE;

         for (unsigned z=0; z<spill; z++)
            lastBlock[z] ^= entropyPtr[z];
      }

      memcpy(result.getPtr() + blockCount * AES_BLOCK_SIZE,
         lastBlock, spill);
   }

   //update size counter
   nBytes_.fetch_add(numBytes, memory_order_relaxed);

   //reseed after 1MB
   if (nBytes_.load(memory_order_relaxed) >= 1048576)
      reseed();

   return result;
}

/////////////////////////////////////////////////////////////////////////////
//// CryptoAES
/////////////////////////////////////////////////////////////////////////////
// Implement AES encryption using AES mode, CFB
SecureBinaryData CryptoAES::EncryptCFB(const SecureBinaryData & clearText, 
                                       const SecureBinaryData & key,
                                       const SecureBinaryData & iv)
{
   //TODO: needs test coverage
   
   /*
   Not gonna bother with padding with CFB, this is only to decrypt Armory 
   v1.35 wallet root keys (always 32 bytes)
   */

   if(clearText.getSize() == 0 || clearText.getSize() % AES_BLOCK_SIZE)
      throw std::runtime_error("invalid data size");
   AES256_ctx aes_ctx;
   AES256_init(&aes_ctx, key.getPtr());

   SecureBinaryData cipherText(clearText.getSize());
   SecureBinaryData intermediaryCipherText(AES_BLOCK_SIZE);
   const uint8_t* dataToEncrypt = iv.getPtr();
   
   auto blockCount = clearText.getSize() / AES_BLOCK_SIZE;
   for (unsigned i=0; i<blockCount; i++)
   {
      AES256_encrypt(
         &aes_ctx, 1, 
         intermediaryCipherText.getPtr(),
         dataToEncrypt);
      
      auto clearTextPtr = clearText.getPtr() + i * AES_BLOCK_SIZE;
      auto cipherTextPtr = cipherText.getPtr() + i * AES_BLOCK_SIZE;
      for (unsigned y=0; y<AES_BLOCK_SIZE; y++)
      {
         cipherTextPtr[y] = 
            clearTextPtr[y] ^ intermediaryCipherText.getPtr()[y];
      }

      dataToEncrypt = cipherTextPtr;
   }

   return cipherText;
}

/////////////////////////////////////////////////////////////////////////////
// Implement AES decryption using AES mode, CFB
SecureBinaryData CryptoAES::DecryptCFB(const SecureBinaryData & cipherText, 
                                       const SecureBinaryData & key,
                                       const SecureBinaryData & iv  )
{
   /*
   Not gonna bother with padding with CFB, this is only to decrypt Armory 
   v1.35 wallet root keys (always 32 bytes)
   */

   if(cipherText.getSize() == 0 || cipherText.getSize() % AES_BLOCK_SIZE)
      throw std::runtime_error("invalid data size");

   SecureBinaryData clearText(cipherText.getSize());

   AES256_ctx aes_ctx;
   AES256_init(&aes_ctx, key.getPtr());

   auto blockCount = cipherText.getSize() / AES_BLOCK_SIZE;
   const uint8_t* dataToDecrypt = iv.getPtr();
   SecureBinaryData intermediaryCipherText(AES_BLOCK_SIZE);
   for (unsigned i=0; i<blockCount; i++)
   {
      AES256_encrypt(
         &aes_ctx, 1, 
         intermediaryCipherText.getPtr(),
         dataToDecrypt);
      
      auto clearTextPtr = clearText.getPtr() + i * AES_BLOCK_SIZE;
      auto cipherTextPtr = cipherText.getPtr() + i * AES_BLOCK_SIZE;
      for (unsigned y=0; y<AES_BLOCK_SIZE; y++)
      {
         clearTextPtr[y] = 
            cipherTextPtr[y] ^ intermediaryCipherText.getPtr()[y];
      }

      dataToDecrypt = cipherTextPtr;
   }

   return clearText;
}

/////////////////////////////////////////////////////////////////////////////
// Same as above, but only changing the AES mode of operation (CBC, not CFB)
SecureBinaryData CryptoAES::EncryptCBC(const SecureBinaryData & data, 
                                       const SecureBinaryData & key,
                                       const SecureBinaryData & iv)
{
   if(data.getSize() == 0)
      return SecureBinaryData(0);

   size_t packet_count = data.getSize() / AES_BLOCK_SIZE + 1;

   SecureBinaryData encrData(packet_count * AES_BLOCK_SIZE);

   //sanity check
   if (iv.getSize() != AES_BLOCK_SIZE)
      throw std::runtime_error("invalid IV size!");

   auto result = aes256_cbc_encrypt(
      key.getPtr(), iv.getPtr(),
      data.getPtr(), data.getSize(),
      1, //PKCS #5 padding
      encrData.getPtr());
      
   if (result == 0)
   {
      LOGERR << "AES CBC encryption failed!";
      throw std::runtime_error("AES CBC encryption failed!");
   }
   else if (result != (ssize_t)encrData.getSize())
   {
      LOGERR << "Encrypted data size mismatch!";
      throw std::runtime_error("Encrypted data size mismatch!");
   }

   return encrData;
}

/////////////////////////////////////////////////////////////////////////////
// Same as above, but only changing the AES mode of operation (CBC, not CFB)
SecureBinaryData CryptoAES::DecryptCBC(const SecureBinaryData& data,
   const SecureBinaryData& key, const SecureBinaryData& iv)
{
   if(data.empty()) {
      return {};
   }

   SecureBinaryData unencrData(data.getSize());
   auto size = aes256_cbc_decrypt(
      key.getPtr(), iv.getPtr(),
      data.getPtr(), data.getSize(),
      1, //PKCS #5 padding
      unencrData.getPtr());

   if (size == 0) {
      throw std::runtime_error("failed to decrypt packet");
   }
   if (size < (ssize_t)unencrData.getSize()) {
      unencrData.resize(size);
   }
   return unencrData;
}

/////////////////////////////////////////////////////////////////////////////
//// CryptoECDSA
/////////////////////////////////////////////////////////////////////////////
void CryptoECDSA::setupContext()
{
   btc_ecc_start();
   crypto_ecdsa_ctx = secp256k1_context_create(
      SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

   auto rando = CryptoPRNG::generateRandom(32);
   if (!secp256k1_context_randomize(crypto_ecdsa_ctx, rando.getPtr()))
      throw runtime_error("[CryptoECDSA::setupContext]");
}

/////////////////////////////////////////////////////////////////////////////
void CryptoECDSA::shutdown()
{
   btc_ecc_stop();
   if (crypto_ecdsa_ctx != nullptr)
   {
      secp256k1_context_destroy(crypto_ecdsa_ctx);
      crypto_ecdsa_ctx = nullptr;
   }
}

/////////////////////////////////////////////////////////////////////////////
bool CryptoECDSA::checkPrivKeyIsValid(const SecureBinaryData& privKey)
{
   if (privKey.getSize() != 32)
      return false;

   btc_key pkey;
   btc_privkey_init(&pkey);
   memcpy(pkey.privkey, privKey.getPtr(), 32);
   return btc_privkey_is_valid(&pkey);
}

/////////////////////////////////////////////////////////////////////////////
SecureBinaryData CryptoECDSA::ComputePublicKey(
   SecureBinaryData const & cppPrivKey, bool compressed) const
{
   if (cppPrivKey.getSize() != 32)
      throw runtime_error("invalid priv key size");

   btc_key pkey;
   btc_privkey_init(&pkey);
   memcpy(pkey.privkey, cppPrivKey.getPtr(), 32);
   if (!btc_privkey_is_valid(&pkey))
      throw runtime_error("invalid private key");

   btc_pubkey pubkey;
   btc_pubkey_init(&pubkey);

   SecureBinaryData result;
   size_t len;

   if (!compressed)
   {
      len = BTC_ECKEY_UNCOMPRESSED_LENGTH;
      btc_ecc_get_pubkey(pkey.privkey, pubkey.pubkey, &len, false);
   }
   else
   {
      len = BTC_ECKEY_COMPRESSED_LENGTH;
      btc_ecc_get_pubkey(pkey.privkey, pubkey.pubkey, &len, true);
   }

   result.resize(len);
   memcpy(result.getPtr(), pubkey.pubkey, len);
   return result;
}

////////////////////////////////////////////////////////////////////////////////
bool CryptoECDSA::VerifyPublicKeyValid(SecureBinaryData const & pubKey)
{
   if(CRYPTO_DEBUG)
   {
      cout << "BinPub: " << pubKey.toHexStr() << endl;
   }

   btc_pubkey key;
   btc_pubkey_init(&key);
   memcpy(key.pubkey, pubKey.getPtr(), pubKey.getSize());
   key.compressed = pubKey.getSize() == 33 ? true : false;
   return btc_pubkey_is_valid(&key);
}

/////////////////////////////////////////////////////////////////////////////
// Use the secp256k1 curve to sign data of an arbitrary length.
// Input:  Data to sign  (const SecureBinaryData&)
//         The private key used to sign the data  (const BTC_PRIVKEY&)
//         A flag indicating if deterministic signing is used  (const bool&)
// Output: None
// Return: The signature of the data  (SecureBinaryData)
SecureBinaryData CryptoECDSA::SignData(BinaryData const & binToSign, 
   SecureBinaryData const & cppPrivKey, const bool&)
{
   //hash message
   BinaryData digest(32);
   sha256_Raw(binToSign.getPtr(), binToSign.getSize(), digest.getPtr());
   sha256_Raw(digest.getPtr(), 32, digest.getPtr());

   // Only use RFC 6979
   SecureBinaryData sig(74);
   size_t outlen = 74;

   btc_key pkey;
   btc_privkey_init(&pkey);
   memcpy(pkey.privkey, cppPrivKey.getPtr(), 32);

   btc_key_sign_hash(&pkey, digest.getPtr(), sig.getPtr(), &outlen);
   if(outlen != 74)
      sig.resize(outlen);

   return sig;
}

/////////////////////////////////////////////////////////////////////////////
bool CryptoECDSA::VerifyData(BinaryData const & binMessage,
   const BinaryData& sig,
   BinaryData const & cppPubKey) const
{
   //pub keys are already validated by the script parser

   // We execute the first SHA256 op, here.  Next one is done by Verifier
   BinaryData digest1(32), digest2(32);
   sha256_Raw(binMessage.getPtr(), binMessage.getSize(), digest1.getPtr());
   sha256_Raw(digest1.getPtr(), 32, digest2.getPtr());

   //setup pubkey
   btc_pubkey key;
   btc_pubkey_init(&key);
   memcpy(key.pubkey, cppPubKey.getPtr(), cppPubKey.getSize());
   key.compressed = cppPubKey.getSize() == 33 ? true : false;

   // Verifying message 
   return btc_pubkey_verify_sig(
      &key, digest2.getPtr(),
      (unsigned char*)sig.getCharPtr(), sig.getSize());
}

/////////////////////////////////////////////////////////////////////////////
// Deterministically generate new private key using a chaincode
// Changed:  added using the hash of the public key to the mix
//           b/c multiplying by the chaincode alone is too "linear"
//           (there's no reason to believe it's insecure, but it doesn't
//           hurt to add some extra entropy/non-linearity to the chain
//           generation process)
SecureBinaryData CryptoECDSA::ComputeChainedPrivateKey(
                                 SecureBinaryData const & binPrivKey,
                                 SecureBinaryData const & chainCode,
                                 SecureBinaryData* multiplierOut)
{
   auto&& binPubKey = ComputePublicKey(binPrivKey);

   if( binPrivKey.getSize() != 32 || chainCode.getSize() != 32)
   {
      LOGERR << "***ERROR:  Invalid private key or chaincode (both must be 32B)";
      LOGERR << "BinPrivKey: " << binPrivKey.getSize();
      LOGERR << "BinPrivKey: (not logged for security)";
      LOGERR << "BinChain  : " << chainCode.getSize();
      LOGERR << "BinChain  : " << chainCode.toHexStr();
   }

   // Adding extra entropy to chaincode by xor'ing with hash256 of pubkey
   BinaryData chainMod  = binPubKey.getHash256();
   BinaryData chainOrig = chainCode.getRawCopy();
   BinaryData chainXor(32);
      
   for(uint8_t i=0; i<8; i++)
   {
      uint8_t offset = 4*i;
      *(uint32_t*)(chainXor.getPtr()+offset) =
                           *(uint32_t*)( chainMod.getPtr()+offset) ^ 
                           *(uint32_t*)(chainOrig.getPtr()+offset);
   }

   SecureBinaryData newPrivData(binPrivKey);
   if (!secp256k1_ec_seckey_tweak_mul(crypto_ecdsa_ctx,
      newPrivData.getPtr(), chainXor.getPtr()))
   {
      throw runtime_error(
         "[ComputeChainedPrivateKey] failed to multiply priv key");
   }

   if(multiplierOut != NULL)
      (*multiplierOut) = SecureBinaryData(chainXor);

   return newPrivData;
}

/////////////////////////////////////////////////////////////////////////////
// Deterministically generate new public key using a chaincode
SecureBinaryData CryptoECDSA::ComputeChainedPublicKey(
   SecureBinaryData const & binPubKey, SecureBinaryData const & chainCode,
   SecureBinaryData* multiplierOut)
{
   secp256k1_pubkey pubkey;
   if (!secp256k1_ec_pubkey_parse(
      crypto_ecdsa_ctx, &pubkey, binPubKey.getPtr(), binPubKey.getSize()))
   {
      throw runtime_error("[ComputeChainedPublicKey] invalid pubkey");
   }

   if(CRYPTO_DEBUG)
   {
      cout << "ComputeChainedPUBLICKey:" << endl;
      cout << "   BinPub: " << binPubKey.toHexStr() << endl;
      cout << "   BinChn: " << chainCode.toHexStr() << endl;
   }

   // Added extra entropy to chaincode by xor'ing with hash256 of pubkey
   BinaryData chainMod  = binPubKey.getHash256();
   BinaryData chainOrig = chainCode.getRawCopy();
   BinaryData chainXor(32);

   for(uint8_t i=0; i<8; i++)
   {
      uint8_t offset = 4*i;
      *(uint32_t*)(chainXor.getPtr()+offset) =
         *(uint32_t*)( chainMod.getPtr()+offset) ^
         *(uint32_t*)(chainOrig.getPtr()+offset);
   }

   if (!secp256k1_ec_pubkey_tweak_mul(
      crypto_ecdsa_ctx, &pubkey, chainXor.getPtr()))
   {
      throw runtime_error(
         "[ComputeChainedPublicKey] failed to multiply pubkey");
   }

   if(multiplierOut != NULL)
      (*multiplierOut) = SecureBinaryData(chainXor);

   SecureBinaryData pubKeyResult(binPubKey.getSize());
   size_t outputLen = binPubKey.getSize();
   if (!secp256k1_ec_pubkey_serialize(
      crypto_ecdsa_ctx, pubKeyResult.getPtr(), &outputLen, &pubkey,
      binPubKey.getSize() == 65 ?
         SECP256K1_EC_UNCOMPRESSED : SECP256K1_EC_COMPRESSED))
   {
      throw runtime_error(
         "[ComputeChainedPublicKey] failed to serialize pubkey");
   }
   return pubKeyResult;
}

////////////////////////////////////////////////////////////////////////////////
bool CryptoECDSA::ECVerifyPoint(BinaryData const & x, BinaryData const & y)
{
   BinaryWriter bw;
   bw.put_uint8_t(4);
   bw.put_BinaryData(x);
   bw.put_BinaryData(y);
   auto ptr = bw.getDataRef().getPtr();

   return btc_ecc_verify_pubkey(ptr, false);
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData CryptoECDSA::CompressPoint(SecureBinaryData const & pubKey65)
{
   if (pubKey65.getSize() != 65)
   {
      if (pubKey65.getSize() == 33)
         return pubKey65;

      throw runtime_error("[CompressPoint] invalid key size");
   }

   secp256k1_pubkey pubkey;
   if (!secp256k1_ec_pubkey_parse(
      crypto_ecdsa_ctx, &pubkey, pubKey65.getPtr(), 65))
   {
      throw runtime_error("[CompressPoint] invalid pubkey");
   }

   SecureBinaryData ptCompressed(33);
   size_t outputLen = 33;
   if (!secp256k1_ec_pubkey_serialize(
      crypto_ecdsa_ctx, ptCompressed.getPtr(),
      &outputLen, &pubkey, SECP256K1_EC_COMPRESSED) || outputLen != 33)
   {
      throw runtime_error("[CompressPoint] failed to compress pubkey");
   }

   return ptCompressed; 
}

////////////////////////////////////////////////////////////////////////////////
btc_pubkey CryptoECDSA::CompressPoint(btc_pubkey const & pubKey65)
{
   if (pubKey65.compressed)
      return pubKey65;

   secp256k1_pubkey pubkey;
   if (!secp256k1_ec_pubkey_parse(
      crypto_ecdsa_ctx, &pubkey, pubKey65.pubkey, 65))
   {
      throw runtime_error("[CompressPoint] invalid pubkey");
   }

   btc_pubkey pbCompressed;
   btc_pubkey_init(&pbCompressed);
   size_t outputLen = 33;
   if (!secp256k1_ec_pubkey_serialize(
      crypto_ecdsa_ctx, pbCompressed.pubkey,
      &outputLen, &pubkey, SECP256K1_EC_COMPRESSED) || outputLen != 33)
   {
      throw runtime_error("[CompressPoint] failed to compress pubkey");
   }

   pbCompressed.compressed = true;
   return pbCompressed; 
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData CryptoECDSA::UncompressPoint(SecureBinaryData const & pubKey33)
{
   if (pubKey33.getSize() != 33)
   {
      if (pubKey33.getSize() == 65)
         return pubKey33;

      throw runtime_error("[UncompressPoint] invalid key size");
   }

   secp256k1_pubkey pubkey;
   if (!secp256k1_ec_pubkey_parse(
      crypto_ecdsa_ctx, &pubkey, pubKey33.getPtr(), 33))
   {
      throw runtime_error("[UncompressPoint] invalid pubkey");
   }

   SecureBinaryData ptUncompressed(65);
   size_t outputLen = 65;
   if (!secp256k1_ec_pubkey_serialize(
      crypto_ecdsa_ctx, ptUncompressed.getPtr(),
      &outputLen, &pubkey, SECP256K1_EC_UNCOMPRESSED) || outputLen != 65)
   {
      throw runtime_error("[CompressPoint] failed to compress pubkey");
   }

   return ptUncompressed; 
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData CryptoECDSA::PrivKeyScalarMultiply(
   const SecureBinaryData& privKey,
   const SecureBinaryData& scalar)
{
   SecureBinaryData newPrivData(privKey);
   if (!secp256k1_ec_seckey_tweak_mul(crypto_ecdsa_ctx,
      newPrivData.getPtr(), scalar.getPtr()))
   {
      throw runtime_error("failed to multiply priv key");
   }

   return newPrivData;
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData CryptoECDSA::PubKeyScalarMultiply(
   const SecureBinaryData& pubKeyIn,
   const SecureBinaryData& scalar)
{
   if (scalar.getSize() != 32)
      throw runtime_error("[PubKeyScalarMultiply]");

   secp256k1_pubkey pubkey;
   if (!secp256k1_ec_pubkey_parse(
      crypto_ecdsa_ctx, &pubkey, pubKeyIn.getPtr(), pubKeyIn.getSize()))
   {
      throw runtime_error("[PubKeyScalarMultiply] invalid pubkey");
   }

   if (!secp256k1_ec_pubkey_tweak_mul(
      crypto_ecdsa_ctx, &pubkey, scalar.getPtr()))
   {
      throw runtime_error("[PubKeyScalarMultiply] failed to multiply pub key");
   }

   size_t outputLen = pubKeyIn.getSize();
   SecureBinaryData result(outputLen);
   if (!secp256k1_ec_pubkey_serialize(crypto_ecdsa_ctx, result.getPtr(),
      &outputLen, &pubkey, pubKeyIn.getSize() == 65 ?
      SECP256K1_EC_UNCOMPRESSED : SECP256K1_EC_COMPRESSED))
   {
      throw runtime_error("failed to compress point");
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CryptoECDSA::SignBitcoinMessage(
   const BinaryDataRef& msg, 
   const SecureBinaryData& privKey, 
   bool compressedPubKey)
{
   //prepend message
   BinaryWriter msgToSign;
   msgToSign.put_var_int(bitcoinMessageMagic_.size());
   msgToSign.put_String(bitcoinMessageMagic_);
   msgToSign.put_var_int(msg.getSize());
   msgToSign.put_BinaryDataRef(msg);

   //hash it
   BinaryData digest(32);
   btc_hash(msgToSign.getDataRef().getPtr(), msgToSign.getSize(), digest.getPtr());

   //sign
   int rec = -1;
   BinaryData result(65);
   size_t outlen = 0;

   if (!btc_ecc_sign_compact_recoverable(
      privKey.getPtr(), digest.getPtr(), result.getPtr() + 1, &outlen, &rec))
   {
      throw runtime_error("failed to sign message");
   }

   *result.getPtr() = 27 + rec + (compressedPubKey ? 4 : 0);
   return result;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CryptoECDSA::VerifyBitcoinMessage(
   const BinaryDataRef& msg, const BinaryDataRef& sig)
{
   //prepend message
   BinaryWriter msgToSign;
   msgToSign.put_var_int(bitcoinMessageMagic_.size());
   msgToSign.put_String(bitcoinMessageMagic_);
   msgToSign.put_var_int(msg.getSize());
   msgToSign.put_BinaryDataRef(msg);

   //hash it
   BinaryData digest(32);
   btc_hash(msgToSign.getDataRef().getPtr(), msgToSign.getSize(), digest.getPtr());

   //check sig and recover pubkey
   bool compressed = ((*sig.getPtr() - 27) & 4) != 0;
   int recid = (*sig.getPtr() - 27) & 3;
   
   size_t outlen; 
   outlen = compressed ? 33 : 65;
   BinaryData pubkey(outlen);

   if (!btc_ecc_recover_pubkey(sig.getPtr() + 1, digest.getPtr(), recid, 
      pubkey.getPtr(), &outlen))
   {
      throw runtime_error("failed to verify message signature");
   }

   //return the pubkey, caller will check vs expected address
   return pubkey;
}

/////////////////////////////////////////////////////////////////////////////
//// CryptoSHA2
////////////////////////////////////////////////////////////////////////////////
void CryptoSHA2::getHash256(BinaryDataRef bdr, uint8_t* digest)
{
   sha256_Raw(bdr.getPtr(), bdr.getSize(), digest);
   sha256_Raw(digest, 32, digest);
}

////////////////////////////////////////////////////////////////////////////////
void CryptoSHA2::getSha256(BinaryDataRef bdr, uint8_t* digest)
{
   sha256_Raw(bdr.getPtr(), bdr.getSize(), digest);
}

////////////////////////////////////////////////////////////////////////////////
void CryptoSHA2::getHMAC256(BinaryDataRef data, BinaryDataRef msg, 
   uint8_t* digest)
{
   hmac_sha256(data.getPtr(), data.getSize(), msg.getPtr(), msg.getSize(), 
      digest);
}

////////////////////////////////////////////////////////////////////////////////
void CryptoSHA2::getSha512(BinaryDataRef bdr, uint8_t* digest)
{
   sha512_Raw(bdr.getPtr(), bdr.getSize(), digest);
}

////////////////////////////////////////////////////////////////////////////////
void CryptoSHA2::getHMAC512(BinaryDataRef data, BinaryDataRef msg,
   uint8_t* digest)
{
   hmac_sha512(data.getPtr(), data.getSize(), msg.getPtr(), msg.getSize(),
      digest);
}

/////////////////////////////////////////////////////////////////////////////
//// CryptoHASH160
////////////////////////////////////////////////////////////////////////////////
void CryptoHASH160::getHash160(BinaryDataRef bdr, uint8_t* digest)
{
   btc_ripemd160(bdr.getPtr(), bdr.getSize(), digest);
}