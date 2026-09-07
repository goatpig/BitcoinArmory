////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Cryptography.h"
#include "log.h"
#include <secp256k1.h>
#include <btc/ecc.h>
#include <btc/ecc_key.h>
#include <btc/sha2.h>
#include <btc/hash.h>
#include <btc/ripemd160.h>
#include <btc/ctaes.h>
#include <btc/hmac.h>
#include <btc/random.h>
#include <btc/aes256_cbc.h>

std::once_flag contexFlag;

using namespace Cryptography;
using namespace std::string_view_literals;

constexpr std::string_view ECDSA::bitcoinMessageMagic{
   "Bitcoin Signed Message:\n"sv
};
constexpr size_t Encryption::AES::BLOCK_SIZE = AES_BLOCK_SIZE;
secp256k1_context* ECDSA::crypto_ecdsa_ctx = nullptr;
const PRNG::Fortuna PRNG::fortuna;

/////////////////////////////////////////////////////////////////////////////
// PRNG
SecureBinaryData PRNG::generateRandomStrong(uint32_t numBytes,
   const SecureBinaryData& extraEntropy)
{
   SecureBinaryData sbd(numBytes);
   btc_random_init();
   if (!btc_random_bytes(sbd.getPtr(), numBytes, 0)) {
      throw std::runtime_error("failed to generate random value");
   }

   if (!extraEntropy.empty()) {
      sbd.XOR(extraEntropy);
   }
   return sbd;
}

// Fortuna
PRNG::Fortuna::Fortuna()
{
   reseed();
}

void PRNG::Fortuna::reseed() const
{
   nBytes_.store(0, std::memory_order_relaxed);
   auto rng = PRNG::generateRandomStrong(32);

   auto newKey = std::make_shared<SecureBinaryData>(32);
   unsigned char digest[32];
   sha256_Raw(rng.getPtr(), rng.getSize(), digest);
   sha256_Raw(digest, 32, newKey->getPtr());

   key_.store(newKey, std::memory_order_relaxed);
}

SecureBinaryData PRNG::Fortuna::generateRandom(uint32_t numBytes,
   const SecureBinaryData& extraEntropy) const
{
   size_t blockCount = numBytes / Encryption::AES::BLOCK_SIZE;
   size_t spill = numBytes % Encryption::AES::BLOCK_SIZE;
   SecureBinaryData result(numBytes);

   unsigned char plainText[Encryption::AES::BLOCK_SIZE];
   memset(&plainText, 0, Encryption::AES::BLOCK_SIZE);

   //setup AES object, seed with key_
   auto keyPtr = key_.load(std::memory_order_relaxed);
   AES256_ctx aes_ctx;
   AES256_init(&aes_ctx, keyPtr->getPtr());

   //main body
   for (unsigned i=0; i<blockCount; i++) {
      //set counters in plain text block
      for (unsigned y=0; y < 4; y++) {
         auto ptr = (uint32_t*)plainText;
         *ptr = counter_.fetch_add(1, std::memory_order_relaxed);
      }

      //encrypt counters with key
      auto resultPtr = result.getPtr() + (i * Encryption::AES::BLOCK_SIZE);
      AES256_encrypt(&aes_ctx, 1, resultPtr, plainText);

      //xor with extra entropy if available
      if (extraEntropy.getSize() >= (i + 1) * Encryption::AES::BLOCK_SIZE) {
         auto entropyPtr = extraEntropy.getPtr() +
            (i * Encryption::AES::BLOCK_SIZE);
         for (unsigned z=0; z<Encryption::AES::BLOCK_SIZE; z++) {
            resultPtr[z] ^= entropyPtr[z];
         }
      }
   }

   //return if we have no spill
   if (spill > 0) {
      //deal with spill block
      for (unsigned y=0; y < 4; y++) {
         auto ptr = (uint32_t*)plainText;
         *ptr = counter_.fetch_add(1, std::memory_order_relaxed);
      }

      unsigned char lastBlock[Encryption::AES::BLOCK_SIZE];
      AES256_encrypt(&aes_ctx, 1, lastBlock, plainText);
      if (extraEntropy.getSize() >= numBytes) {
         auto entropyPtr = extraEntropy.getPtr() +
            blockCount * Encryption::AES::BLOCK_SIZE;
         for (unsigned z=0; z < spill; z++) {
            lastBlock[z] ^= entropyPtr[z];
         }
      }
      memcpy(
         result.getPtr() + blockCount * Encryption::AES::BLOCK_SIZE,
         lastBlock,
         spill
      );
   }

   //update size counter
   nBytes_.fetch_add(numBytes, std::memory_order_relaxed);

   //reseed after 1MB
   if (nBytes_.load(std::memory_order_relaxed) >= 1048576) {
      reseed();
   }
   return result;
}

/////////////////////////////////////////////////////////////////////////////
// Encryption
SecureBinaryData Encryption::AES::encryptCFB(
   const SecureBinaryData& clearText,
   const SecureBinaryData& key,
   const SecureBinaryData& iv)
{
   //TODO: needs test coverage

   /*
   Not gonna bother with padding with CFB, this is only to decrypt Armory 
   v1.35 wallet root keys (always 32 bytes)
   */

   if(clearText.empty() || clearText.getSize() % BLOCK_SIZE) {
      throw std::runtime_error("invalid data size");
   }
   AES256_ctx aes_ctx;
   AES256_init(&aes_ctx, key.getPtr());

   SecureBinaryData cipherText(clearText.getSize());
   SecureBinaryData intermediaryCipherText(BLOCK_SIZE);
   const uint8_t* dataToEncrypt = iv.getPtr();

   auto blockCount = clearText.getSize() / BLOCK_SIZE;
   for (unsigned i=0; i < blockCount; i++) {
      AES256_encrypt(
         &aes_ctx, 1,
         intermediaryCipherText.getPtr(),
         dataToEncrypt);
      
      auto clearTextPtr = clearText.getPtr() + i * BLOCK_SIZE;
      auto cipherTextPtr = cipherText.getPtr() + i * BLOCK_SIZE;
      for (unsigned y=0; y < BLOCK_SIZE; y++) {
         cipherTextPtr[y] =
            clearTextPtr[y] ^ intermediaryCipherText.getPtr()[y];
      }
      dataToEncrypt = cipherTextPtr;
   }
   return cipherText;
}

/////////////////////////////////////////////////////////////////////////////
// Implement AES decryption using AES mode, CFB
SecureBinaryData Encryption::AES::decryptCFB(
   const SecureBinaryData& cipherText,
   const SecureBinaryData& key,
   const SecureBinaryData& iv)
{
   /*
   Not gonna bother with padding with CFB, this is only to decrypt Armory
   v1.35 wallet root keys (always 32 bytes)
   */

   if (cipherText.empty() || cipherText.getSize() % BLOCK_SIZE) {
      throw std::runtime_error("invalid data size");
   }

   SecureBinaryData clearText(cipherText.getSize());

   AES256_ctx aes_ctx;
   AES256_init(&aes_ctx, key.getPtr());

   auto blockCount = cipherText.getSize() / BLOCK_SIZE;
   const uint8_t* dataToDecrypt = iv.getPtr();
   SecureBinaryData intermediaryCipherText(BLOCK_SIZE);
   for (unsigned i=0; i < blockCount; i++) {
      AES256_encrypt(
         &aes_ctx, 1,
         intermediaryCipherText.getPtr(),
         dataToDecrypt);

      auto clearTextPtr = clearText.getPtr() + i * BLOCK_SIZE;
      auto cipherTextPtr = cipherText.getPtr() + i * BLOCK_SIZE;
      for (unsigned y=0; y < BLOCK_SIZE; y++) {
         clearTextPtr[y] =
            cipherTextPtr[y] ^ intermediaryCipherText.getPtr()[y];
      }
      dataToDecrypt = cipherTextPtr;
   }
   return clearText;
}

SecureBinaryData Encryption::AES::encryptCBC(
   const SecureBinaryData& data,
   const SecureBinaryData& key,
   const SecureBinaryData& iv)
{
   if (data.empty()) {
      return {};
   }
   size_t packetCount = data.getSize() / BLOCK_SIZE + 1;
   SecureBinaryData encrData(packetCount * BLOCK_SIZE);

   //sanity check
   if (iv.getSize() != BLOCK_SIZE) {
      throw std::runtime_error("invalid IV size!");
   }

   auto result = aes256_cbc_encrypt(
      key.getPtr(), iv.getPtr(),
      data.getPtr(), data.getSize(),
      1, //PKCS #5 padding
      encrData.getPtr()
   );

   if (result == 0) {
      LOGERR << "AES CBC encryption failed!";
      throw std::runtime_error("AES CBC encryption failed!");
   } else if (result != (ssize_t)encrData.getSize()) {
      LOGERR << "Encrypted data size mismatch!";
      throw std::runtime_error("Encrypted data size mismatch!");
   }
   return encrData;
}

SecureBinaryData Encryption::AES::decryptCBC(
   const SecureBinaryData& data,
   const SecureBinaryData& key,
   const SecureBinaryData& iv)
{
   if (data.empty()) {
      return {};
   }

   SecureBinaryData unencrData(data.getSize());
   auto size = aes256_cbc_decrypt(
      key.getPtr(), iv.getPtr(),
      data.getPtr(), data.getSize(),
      1, //PKCS #5 padding
      unencrData.getPtr()
   );

   if (size == 0) {
      throw std::runtime_error("failed to decrypt packet");
   }
   if (size < (ssize_t)unencrData.getSize()) {
      unencrData.resize(size);
   }
   return unencrData;
}

/////////////////////////////////////////////////////////////////////////////
// ECDSA
void ECDSA::setupContext()
{
   std::call_once(contexFlag, [](){
      btc_ecc_start();
      crypto_ecdsa_ctx = secp256k1_context_create(
         SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

      auto rando = PRNG::generateRandomStrong(32);
      if (!secp256k1_context_randomize(crypto_ecdsa_ctx, rando.getPtr())) {
         throw std::runtime_error("[CryptoECDSA::setupContext]");
      }
   });
}

void ECDSA::shutdown()
{
   btc_ecc_stop();
   if (crypto_ecdsa_ctx != nullptr) {
      secp256k1_context_destroy(crypto_ecdsa_ctx);
      crypto_ecdsa_ctx = nullptr;
   }
}

/////////////////////////////////////////////////////////////////////////////
// private keys
bool ECDSA::checkPrivKeyIsValid(const SecureBinaryData& privKey)
{
   if (privKey.getSize() != 32)
      return false;

   btc_key pkey;
   btc_privkey_init(&pkey);
   memcpy(pkey.privkey, privKey.getPtr(), 32);
   return btc_privkey_is_valid(&pkey);
}

SecureBinaryData ECDSA::createNewPrivateKey(
   const SecureBinaryData& extraEntropy)
{
   while (true) {
      auto privKey = PRNG::generateRandomStrong(32, extraEntropy);
      if (checkPrivKeyIsValid(privKey)) {
         return privKey;
      }
   }
}

bool ECDSA::checkPubPrivKeyMatch(const SecureBinaryData& cppPrivKey,
   const SecureBinaryData& cppPubKey)
{
   auto pubkey = ECDSA::computePublicKey(cppPrivKey);
   return pubkey == cppPubKey;
}

// Deterministically generate new private key using a chaincode
SecureBinaryData ECDSA::computeChainedPrivateKey(
   const SecureBinaryData& binPrivKey,
   const SecureBinaryData& chainCode)
{
   auto binPubKey = computePublicKey(binPrivKey);

   if (binPrivKey.getSize() != 32 || chainCode.getSize() != 32) {
      LOGERR << "***ERROR:  Invalid private key or chaincode (both must be 32B)";
      LOGERR << "BinPrivKey: " << binPrivKey.getSize();
      LOGERR << "BinPrivKey: (not logged for security)";
      LOGERR << "BinChain  : " << chainCode.getSize();
      LOGERR << "BinChain  : " << chainCode.toHexStr();
   }

   // Adding extra entropy to chaincode by xor'ing with hash256 of pubkey
   BinaryData chainMod;
   chainMod.resize(32);
   Hash::getHash256(binPubKey.getRef(), chainMod.getPtr());
   BinaryData chainOrig = chainCode.getRawCopy();
   BinaryData chainXor;
   chainXor.resize(32);

   for (uint8_t i=0; i<8; i++) {
      uint8_t offset = 4*i;
      *(uint32_t*)(chainXor.getPtr()+offset) =
         *(uint32_t*)(chainMod.getPtr()+offset) ^
         *(uint32_t*)(chainOrig.getPtr()+offset);
   }

   SecureBinaryData newPrivData(binPrivKey);
   if (!secp256k1_ec_seckey_tweak_mul(crypto_ecdsa_ctx,
      newPrivData.getPtr(), chainXor.getPtr())) {
      throw std::runtime_error(
         "[ComputeChainedPrivateKey] failed to multiply priv key");
   }
   return newPrivData;
}

SecureBinaryData ECDSA::privKeyScalarMultiply(
   const SecureBinaryData& privKey,
   const SecureBinaryData& scalar)
{
   SecureBinaryData newPrivData(privKey);
   if (!secp256k1_ec_seckey_tweak_mul(crypto_ecdsa_ctx,
      newPrivData.getPtr(), scalar.getPtr())) {
      throw std::runtime_error("failed to multiply priv key");
   }
   return newPrivData;
}

/////////////////////////////////////////////////////////////////////////////
// public keys
SecureBinaryData ECDSA::computePublicKey(
   BinaryDataRef cppPrivKey, bool compressed)
{
   if (cppPrivKey.getSize() != 32) {
      throw std::runtime_error("invalid priv key size");
   }

   btc_key pkey;
   btc_privkey_init(&pkey);
   memcpy(pkey.privkey, cppPrivKey.getPtr(), 32);
   if (!btc_privkey_is_valid(&pkey)) {
      throw std::runtime_error("invalid private key");
   }

   btc_pubkey pubkey;
   btc_pubkey_init(&pubkey);

   SecureBinaryData result;
   size_t len;

   if (!compressed) {
      len = BTC_ECKEY_UNCOMPRESSED_LENGTH;
      btc_ecc_get_pubkey(pkey.privkey, pubkey.pubkey, &len, false);
   } else {
      len = BTC_ECKEY_COMPRESSED_LENGTH;
      btc_ecc_get_pubkey(pkey.privkey, pubkey.pubkey, &len, true);
   }

   result.resize(len);
   memcpy(result.getPtr(), pubkey.pubkey, len);
   return result;
}

bool ECDSA::verifyPublicKeyValid(BinaryDataRef pubKey)
{
   btc_pubkey key;
   btc_pubkey_init(&key);
   memcpy(key.pubkey, pubKey.getPtr(), pubKey.getSize());
   key.compressed = pubKey.getSize() == 33 ? true : false;
   return verifyPublicKeyValid(key);
}

bool ECDSA::verifyPublicKeyValid(const btc_pubkey& pubKey)
{
   return btc_pubkey_is_valid(&pubKey);
}

// Deterministically generate new public key using a chaincode
SecureBinaryData ECDSA::computeChainedPublicKey(
   BinaryDataRef binPubKey, BinaryDataRef chainCode)
{
   secp256k1_pubkey pubkey;
   if (!secp256k1_ec_pubkey_parse(
      crypto_ecdsa_ctx, &pubkey, binPubKey.getPtr(), binPubKey.getSize())) {
      throw std::runtime_error("[ComputeChainedPublicKey] invalid pubkey");
   }

   if (CRYPTO_DEBUG) {
      std::cout << "ComputeChainedPUBLICKey:" << std::endl;
      std::cout << "   BinPub: " << binPubKey.toHexStr() << std::endl;
      std::cout << "   BinChn: " << chainCode.toHexStr() << std::endl;
   }

   // Added extra entropy to chaincode by xor'ing with hash256 of pubkey
   BinaryData chainMod;
   chainMod.resize(32);
   Hash::getHash256(binPubKey, chainMod.getPtr());
   BinaryData chainXor;
   chainXor.resize(32);

   for (uint8_t i=0; i<8; i++) {
      uint8_t offset = 4*i;
      *(uint32_t*)(chainXor.getPtr()+offset) =
         *(uint32_t*)( chainMod.getPtr()+offset) ^
         *(uint32_t*)(chainCode.getPtr()+offset);
   }

   if (!secp256k1_ec_pubkey_tweak_mul(
      crypto_ecdsa_ctx, &pubkey, chainXor.getPtr())) {
      throw std::runtime_error(
         "[ComputeChainedPublicKey] failed to multiply pubkey");
   }

   SecureBinaryData pubKeyResult(binPubKey.getSize());
   size_t outputLen = binPubKey.getSize();
   if (!secp256k1_ec_pubkey_serialize(
      crypto_ecdsa_ctx, pubKeyResult.getPtr(), &outputLen, &pubkey,
      binPubKey.getSize() == 65 ?
         SECP256K1_EC_UNCOMPRESSED : SECP256K1_EC_COMPRESSED)) {
      throw std::runtime_error(
         "[ComputeChainedPublicKey] failed to serialize pubkey");
   }
   return pubKeyResult;
}

bool ECDSA::verifyPoint(const BinaryData& x, const BinaryData& y)
{
   BinaryWriter bw;
   bw.put_uint8_t(4);
   bw.put_BinaryData(x);
   bw.put_BinaryData(y);
   auto ptr = bw.getDataRef().getPtr();
   return btc_ecc_verify_pubkey(ptr, false);
}

SecureBinaryData ECDSA::compressPoint(BinaryDataRef pubKey65)
{
   if (pubKey65.getSize() != 65) {
      if (pubKey65.getSize() == 33) {
         return pubKey65;
      }
      throw std::runtime_error("[CompressPoint] invalid key size");
   }

   secp256k1_pubkey pubkey;
   if (!secp256k1_ec_pubkey_parse(
      crypto_ecdsa_ctx, &pubkey, pubKey65.getPtr(), 65)) {
      throw std::runtime_error("[CompressPoint] invalid pubkey");
   }

   SecureBinaryData ptCompressed(33);
   size_t outputLen = 33;
   if (!secp256k1_ec_pubkey_serialize(
      crypto_ecdsa_ctx, ptCompressed.getPtr(),
      &outputLen, &pubkey, SECP256K1_EC_COMPRESSED) || outputLen != 33) {
      throw std::runtime_error("[CompressPoint] failed to compress pubkey");
   }
   return ptCompressed;
}

btc_pubkey ECDSA::compressPoint(const btc_pubkey& pubKey65)
{
   if (pubKey65.compressed) {
      return pubKey65;
   }

   secp256k1_pubkey pubkey;
   if (!secp256k1_ec_pubkey_parse(
      crypto_ecdsa_ctx, &pubkey, pubKey65.pubkey, 65)) {
      throw std::runtime_error("[CompressPoint] invalid pubkey");
   }

   btc_pubkey pbCompressed;
   btc_pubkey_init(&pbCompressed);
   size_t outputLen = 33;
   if (!secp256k1_ec_pubkey_serialize(
      crypto_ecdsa_ctx, pbCompressed.pubkey,
      &outputLen, &pubkey, SECP256K1_EC_COMPRESSED) || outputLen != 33) {
      throw std::runtime_error("[CompressPoint] failed to compress pubkey");
   }

   pbCompressed.compressed = true;
   return pbCompressed;
}

SecureBinaryData ECDSA::uncompressPoint(BinaryDataRef pubKey33)
{
   if (pubKey33.getSize() != 33) {
      if (pubKey33.getSize() == 65) {
         return pubKey33;
      }
      throw std::runtime_error("[UncompressPoint] invalid key size");
   }

   secp256k1_pubkey pubkey;
   if (!secp256k1_ec_pubkey_parse(
      crypto_ecdsa_ctx, &pubkey, pubKey33.getPtr(), 33)) {
      throw std::runtime_error("[UncompressPoint] invalid pubkey");
   }

   SecureBinaryData ptUncompressed(65);
   size_t outputLen = 65;
   if (!secp256k1_ec_pubkey_serialize(
      crypto_ecdsa_ctx, ptUncompressed.getPtr(),
      &outputLen, &pubkey, SECP256K1_EC_UNCOMPRESSED) || outputLen != 65) {
      throw std::runtime_error("[CompressPoint] failed to compress pubkey");
   }
   return ptUncompressed;
}

SecureBinaryData ECDSA::pubKeyScalarMultiply(
   BinaryDataRef pubKeyIn, BinaryDataRef scalar)
{
   if (scalar.getSize() != 32) {
      throw std::runtime_error("[PubKeyScalarMultiply]");
   }

   secp256k1_pubkey pubkey;
   if (!secp256k1_ec_pubkey_parse(
      crypto_ecdsa_ctx, &pubkey, pubKeyIn.getPtr(), pubKeyIn.getSize())) {
      throw std::runtime_error("[PubKeyScalarMultiply] invalid pubkey");
   }

   if (!secp256k1_ec_pubkey_tweak_mul(
      crypto_ecdsa_ctx, &pubkey, scalar.getPtr())) {
      throw std::runtime_error("[PubKeyScalarMultiply] failed to multiply pub key");
   }

   size_t outputLen = pubKeyIn.getSize();
   SecureBinaryData result(outputLen);
   if (!secp256k1_ec_pubkey_serialize(crypto_ecdsa_ctx, result.getPtr(),
      &outputLen, &pubkey, pubKeyIn.getSize() == 65 ?
      SECP256K1_EC_UNCOMPRESSED : SECP256K1_EC_COMPRESSED)) {
      throw std::runtime_error("failed to compress point");
   }
   return result;
}


/////////////////////////////////////////////////////////////////////////////
// tx signing
SecureBinaryData ECDSA::signData(const BinaryData& binToSign,
   const SecureBinaryData& cppPrivKey)
{
   //hash message
   BinaryData digest;
   digest.resize(32);
   sha256_Raw(binToSign.getPtr(), binToSign.getSize(), digest.getPtr());
   sha256_Raw(digest.getPtr(), 32, digest.getPtr());

   // Only use RFC 6979
   SecureBinaryData sig(74);
   size_t outlen = 74;

   btc_key pkey;
   btc_privkey_init(&pkey);
   memcpy(pkey.privkey, cppPrivKey.getPtr(), 32);

   btc_key_sign_hash(&pkey, digest.getPtr(), sig.getPtr(), &outlen);
   if (outlen != 74) {
      sig.resize(outlen);
   }
   return sig;
}

bool ECDSA::verifyData(const BinaryData& binMessage,
   const BinaryData& sig, const BinaryData& cppPubKey)
{
   /* pub keys are already validated by the script parser */

   // We execute the first SHA256 op, here. Next one is done by Verifier
   BinaryData digest1, digest2;
   digest1.resize(32);
   digest2.resize(32);
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

////////////////////////////////////////////////////////////////////////////////
// bitcoin message signing
BinaryData ECDSA::signBitcoinMessage(
   const BinaryDataRef& msg,
   const SecureBinaryData& privKey,
   bool compressedPubKey)
{
   //prepend message
   BinaryWriter msgToSign;
   msgToSign.put_var_int(bitcoinMessageMagic.size());
   msgToSign.put_StringView(bitcoinMessageMagic);
   msgToSign.put_var_int(msg.getSize());
   msgToSign.put_BinaryDataRef(msg);

   //hash it
   BinaryData digest;
   digest.resize(32);
   btc_hash(
      msgToSign.getDataRef().getPtr(),
      msgToSign.getSize(),
      digest.getPtr());

   //sign
   int rec = -1;
   BinaryData result;
   result.resize(65);
   size_t outlen = 0;

   if (!btc_ecc_sign_compact_recoverable(
      privKey.getPtr(), digest.getPtr(), result.getPtr() + 1, &outlen, &rec)) {
      throw std::runtime_error("failed to sign message");
   }

   *result.getPtr() = 27 + rec + (compressedPubKey ? 4 : 0);
   return result;
}

BinaryData ECDSA::verifyBitcoinMessage(
   const BinaryDataRef& msg,
   const BinaryDataRef& sig)
{
   //prepend message
   BinaryWriter msgToSign;
   msgToSign.put_var_int(bitcoinMessageMagic.size());
   msgToSign.put_StringView(bitcoinMessageMagic);
   msgToSign.put_var_int(msg.getSize());
   msgToSign.put_BinaryDataRef(msg);

   //hash it
   BinaryData digest;
   digest.resize(32);
   btc_hash(
      msgToSign.getDataRef().getPtr(),
      msgToSign.getSize(),
      digest.getPtr());

   //check sig and recover pubkey
   bool compressed = ((*sig.getPtr() - 27) & 4) != 0;
   int recid = (*sig.getPtr() - 27) & 3;

   size_t outlen;
   outlen = compressed ? 33 : 65;
   BinaryData pubkey;
   pubkey.resize(outlen);

   if (!btc_ecc_recover_pubkey(sig.getPtr() + 1, digest.getPtr(), recid,
      pubkey.getPtr(), &outlen)) {
      throw std::runtime_error("failed to verify message signature");
   }

   //return the pubkey, caller will check vs expected address
   return pubkey;
}

/////////////////////////////////////////////////////////////////////////////
// hashes
void Hash::getHash256(BinaryDataRef bdr, uint8_t* digest)
{
   sha256_Raw(bdr.getPtr(), bdr.getSize(), digest);
   sha256_Raw(digest, 32, digest);
}

void Hash::getSha256(BinaryDataRef bdr, uint8_t* digest)
{
   sha256_Raw(bdr.getPtr(), bdr.getSize(), digest);
}

void Hash::getHMAC256(BinaryDataRef data, BinaryDataRef msg, uint8_t* digest)
{
   hmac_sha256(
      data.getPtr(), data.getSize(),
      msg.getPtr(), msg.getSize(),
      digest);
}

void Hash::getSha512(BinaryDataRef bdr, uint8_t* digest)
{
   sha512_Raw(bdr.getPtr(), bdr.getSize(), digest);
}

void Hash::getHMAC512(BinaryDataRef data, BinaryDataRef msg, uint8_t* digest)
{
   hmac_sha512(
      data.getPtr(), data.getSize(),
      msg.getPtr(), msg.getSize(),
      digest);
}

void Hash::getHash160(BinaryDataRef bdr, uint8_t* digest)
{
   btc_ripemd160(bdr.getPtr(), bdr.getSize(), digest);
}
