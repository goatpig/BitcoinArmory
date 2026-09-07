////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cassert>
#include <cstring>

#include "BIP150_151.h"
#include <hkdf.h>
#include <btc/ecc.h>
#include <btc/hash.h>
#include <btc/sha2.h>
#include <btc/ripemd160.h>
#include <btc/base58.h>
#include "log.h"

#include <Wallets/AuthorizedPeers.h>
#include <Utils/Cryptography.h>

using namespace Armory;
using namespace std::string_view_literals;

namespace {
   std::string toHexStr(const uint8_t* ptr, size_t len)
   {
      BinaryDataRef bdr{ptr, len};
      return bdr.toHexStr();
   }

   std::string getKeyFingerprint(const BinaryDataRef ownKey)
   {
      // Hash the ID pub key.
      uint256 hashStep1;
      std::array<uint8_t, 20> hashStep2;
      btc_hash_sngl_sha256(ownKey.getPtr(), ownKey.getSize(), hashStep1);
      btc_ripemd160(hashStep1, sizeof(hashStep1), hashStep2.data());

      // Build the Base58 data but don't add the SHA-256 tag. libbtc handles it.
      std::array<uint8_t, 23> addrData{};
      std::array<char, 50> b58IDAddr; // 38 is safe but leave a safety buffer.
      addrData[0] = 0x0f;
      addrData[1] = 0xff;
      addrData[2] = 0x01;
      std::copy(std::begin(hashStep2), std::end(hashStep2), &addrData[3]);
      int outLen = btc_base58_encode_check(
         addrData.data(), addrData.size(),
         b58IDAddr.data(), b58IDAddr.size()
      );
      return { b58IDAddr.data(), size_t(outLen - 1) };
   }
}

// Because libbtc doesn't export its libsecp256k1 context, and we need one for
// direct access to libsecp256k1 calls, just create one.
static secp256k1_context* secp256k1_ecdh_ctx = nullptr;
uint32_t ipType_ = 0;
uint8_t oneWayAuthClientPubKey[33];
uint8_t rekeyMsg[33];

// FIX/NOTE: Just use btc_ecc_start() from btc/ecc.h when starting up Armory.
// Need to initialize things, and not just for BIP 151 once libbtc is used more.

// Startup code for BIP 151. Used for initialization of underlying libraries.
// 
// IN:  None
// OUT: None
// RET: N/A
void startupBIP151CTX()
{
   if (secp256k1_ecdh_ctx == nullptr) {
      // SIGN used to generate public keys from private keys. (Can be removed
      // once libbtc exports compressed public keys.)
      // VERIFY used to allow for EC multiplication, which won't work otherwise.
      secp256k1_ecdh_ctx = secp256k1_context_create(
         SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

      auto rando = Cryptography::PRNG::generateRandomStrong(32);
      if (!secp256k1_context_randomize(secp256k1_ecdh_ctx, rando.getPtr())) {
         throw std::runtime_error("[startupBIP151CTX");
      }
   }
   assert(secp256k1_ecdh_ctx != nullptr);
}

// Startup code for BIP 151. Used for shutdown of underlying libraries.
// 
// IN:  None
// OUT: None
// RET: N/A
void shutdownBIP151CTX()
{
   secp256k1_context* ctx = secp256k1_ecdh_ctx;
   secp256k1_ecdh_ctx = nullptr;
   if (ctx != nullptr) {
      secp256k1_context_destroy(ctx);
   }
}

// Overridden constructor for a BIP 151 session. Sets the session direction.
// 
// IN:  sessOut - Indicates session direction.
// OUT: None
// RET: N/A
BIP151Session::BIP151Session(bool sessOut) :
   isOutgoing_(sessOut)
{
   // Generate the ECDH key off the bat.
   btc_privkey_init(&genSymECDHPrivKey_);
   btc_privkey_gen(&genSymECDHPrivKey_);
}

// Overridden constructor for a BIP 151 session. Sets the session direction and
// sets the private key used in ECDH. USE WITH EXTREME CAUTION!!! Unless there's
// a very specific need for a pre-determined key (e.g., test harness or key is
// HW-generated), using this will just get you into trouble.
// IN:  inSymECDHPrivKey - ECDH private key.
//      sessOut - Indicates session direction.
// OUT: None
// RET: N/A
BIP151Session::BIP151Session(bool sessOut, btc_key* inSymECDHPrivKey) :
   isOutgoing_(sessOut)
{
   // libbtc assumes it'll generate the private key. If you want to set it, you
   // have to go into the private key struct.
   btc_privkey_init(&genSymECDHPrivKey_);
   std::copy(inSymECDHPrivKey->privkey,
      inSymECDHPrivKey->privkey + BIP151PRVKEYSIZE,
      genSymECDHPrivKey_.privkey);
}

// Function that generates the symmetric keys required by the BIP 151
// ciphersuite and performs any related setup.
// 
// IN:  peerPubKey  (The peer's public key - Assume the key is validated)
// OUT: N/A
// RET: -1 if not successful, 0 if successful.
int BIP151Session::genSymKeys(const BinaryDataRef& peerPubKey)
{
   int retVal = -1;
   if (peerPubKey.getSize() != BIP151PUBKEYSIZE) {
      LOGERR << "BIP151 - invalid peer public key";
      return retVal;
   }

   btc_key sessionECDHKey;
   secp256k1_pubkey peerECDHPK;
   std::array<uint8_t, BIP151PUBKEYSIZE> parseECDHMulRes{};
   size_t parseECDHMulResSize = parseECDHMulRes.size();
   switch (cipherType_)
   {
      case BIP151SymCiphers::CHACHA20POLY1305_OPENSSH:
         // Confirm that the incoming pub key is valid and compressed.
         if (secp256k1_ec_pubkey_parse(secp256k1_ecdh_ctx,
               &peerECDHPK, peerPubKey.getPtr(), BIP151PUBKEYSIZE) != 1) {
            LOGERR << "BIP 151 - Peer public key for session " <<
               toHexStr(getSessionID(), BIP151PRVKEYSIZE) << " is invalid.";
            return retVal;
         }

         // Perform ECDH here. Use direct calculations via libsecp256k1. The libbtc
         // API doesn't offer ECDH or calls that allow for ECDH functionality. So,
         // just multiply our priv key by their pub key and cut off the first byte.
         //
         // Do NOT use the libsecp256k1 ECDH module. On top of having to create a
         // libsecp256k1 context or use libbtc's context, it has undocumented
         // behavior. Instead of returning the X-coordinate, it returns a SHA-256
         // hash of the compressed pub key in order to preserve secrecy. See
         // https://github.com/bitcoin-core/secp256k1/pull/252#issuecomment-118129035
         // for more info. This is NOT standard ECDH behavior. It will kill
         // BIP 151 interopability.
         if (secp256k1_ec_pubkey_tweak_mul(secp256k1_ecdh_ctx,
            &peerECDHPK, genSymECDHPrivKey_.privkey) != 1) {
            LOGERR << "BIP 151 - ECDH failed.";
            return -1;
         }

         secp256k1_ec_pubkey_serialize(secp256k1_ecdh_ctx,
            parseECDHMulRes.data(), &parseECDHMulResSize, &peerECDHPK,
            SECP256K1_EC_COMPRESSED);
         std::copy(parseECDHMulRes.data() + 1, parseECDHMulRes.data() + 33,
            sessionECDHKey.privkey);

         // Generate the ChaCha20Poly1305 key set and the session ID.
         calcChaCha20Poly1305Keys(sessionECDHKey);
         calcSessionID(sessionECDHKey);
         retVal = 0;
         break;

      default:
         // You should never get here.
         break;
   }
   return retVal;
}

// Function checking to see if we need to perform a rekey. Will occur if too
// many bytes have been sent using the current ciphersuite (mandatory in the
// spec) or if enough time has lapsed (optional in the spec).
// 
// IN:  None
// OUT: None
// RET: True if a rekey is required, false if not.
bool BIP151Session::rekeyNeeded(size_t sz) const
{
   // In theory, there's a race condition if both sides decide at the same time
   // to rekey. In practice, they'll arrive at the same keys eventually.
   // NOTE - user implements rekey timer policy atm
   return (bytesOnCurKeys_ + sz) >= CHACHA20POLY1305MAXBYTESSENT;
}

// Public function used to kick off symmetric key setup. Any setup directly
// related to symmetric keys should be handled here.
// 
// IN:  peerPubKey  (The peer's public key - Needs to be validated)
// OUT: None
// RET: -1 if failure, 0 if success.
int BIP151Session::symKeySetup(const BinaryDataRef& peerPubKey)
{
   int retVal = -1;
   switch (cipherType_)
   {
      case BIP151SymCiphers::CHACHA20POLY1305_OPENSSH:
         // Generate the keys only if the peer key is the correct size (and valid).
         if ((peerPubKey.getSize() != BIP151PUBKEYSIZE) || (genSymKeys(peerPubKey) != 0)) {
            return retVal;
         } else {
            // We're done with the ECDH key now. Nuke it.
            // **Applies only to outbound sessions.**
            if (isOutgoing_) {
               btc_privkey_cleanse(&genSymECDHPrivKey_);
            }
            retVal = 0;
         }
         break;

      default:
         // You should never get here.
         break;
   }

   // If we've made it this far, assume the session is set up, and it's okay to
   // communicate with the outside world.
   return retVal;
}

// A helper function that calculates the ChaCha20Poly1305 keys based on the BIP
// 151 spec.
// 
// IN:  sesECDHKey (The session's ECDH key - libbtc formatting)
// OUT: None
// RET: None
void BIP151Session::calcChaCha20Poly1305Keys(const btc_key& sesECDHKey)
{
   std::array<uint8_t, 33> ikm;
   std::copy(sesECDHKey.privkey, sesECDHKey.privkey + BIP151PRVKEYSIZE,
             ikm.data());
   ikm[BIP151PRVKEYSIZE] = static_cast<uint8_t>(BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   static const auto salt = "bitcoinecdh"sv;
   static const auto info1 = "BitcoinK1"sv;
   static const auto info2 = "BitcoinK2"sv;

   // NB: The ChaCha20Poly1305 library reverses the expected key order.
   hkdf_sha256(hkdfKeySet_.data(), BIP151PRVKEYSIZE,
      (const uint8_t*)salt.data(), salt.size(),
      ikm.data(), ikm.size(),
      (const uint8_t*)info2.data(), info2.size()
   );
   hkdf_sha256(hkdfKeySet_.data() + BIP151PRVKEYSIZE, BIP151PRVKEYSIZE,
      (const uint8_t*)salt.data(), salt.size(),
      ikm.data(), ikm.size(),
      (const uint8_t*)info1.data(), info1.size()
   );
   chacha20poly1305_init(&sessionCTX_, hkdfKeySet_.data(), hkdfKeySet_.size());
}

// A helper function that calculates the session ID. See the "Symmetric
// Encryption Cipher Keys" section of the BIP 151 spec.
// 
// IN:  sesECDHKey (The session's ECDH key - libbtc formatting)
// OUT: None
// RET: None
void BIP151Session::calcSessionID(const btc_key& sesECDHKey)
{
   std::array<uint8_t, BIP151PUBKEYSIZE> ikm;
   std::copy(sesECDHKey.privkey, sesECDHKey.privkey + BIP151PRVKEYSIZE,
      ikm.data());
   ikm[BIP151PRVKEYSIZE] = static_cast<uint8_t>(cipherType_);
   static const auto salt = "bitcoinecdh"sv;
   static const auto info = "BitcoinSessionID"sv;

   hkdf_sha256(sessionID_.data(), sessionID_.size(),
      (const uint8_t*)salt.data(), salt.size(),
      ikm.data(), ikm.size(),
      (const uint8_t*)info.data(), info.size()
   );
}

// FIX DESCRIPTION
// helper function that can be used when it's time to rekey a session. It
// should be called when the other side wishes for a rekey or when we hit a
// policy limit (e.g., time or bytes sent by us). Rekey checks should be
// performed elsewhere.
// 
// IN:  None
// OUT: None
// RET: N/A
void BIP151Session::sessionRekey(bool bip151Rekey,
   const BinaryDataRef& reqIDKey,
   const BinaryDataRef& resIDKey,
   const BinaryDataRef& oppositeSessionKey)
{
   switch (cipherType_)
   {
      case BIP151SymCiphers::CHACHA20POLY1305_OPENSSH:
         // Process both symmetric keys at the same time. Reset the # of bytes on
         // the session but *not* the sequence number.
         uint8_t* mainKey;
         uint8_t* aadKey;
         mainKey = &hkdfKeySet_[0];
         aadKey = &hkdfKeySet_[BIP151PRVKEYSIZE];

         if (bip151Rekey == true) {
            chacha20Poly1305Rekey(mainKey, BIP151PRVKEYSIZE,
               true, {}, {}, {});
            chacha20Poly1305Rekey(aadKey, BIP151PRVKEYSIZE,
               true, {}, {}, {});
         } else {
            assert(oppositeSessionKey.getSize() == BIP151PRVKEYSIZE * 2);
            auto oppositeMainKey = oppositeSessionKey.getSliceRef(
               0, BIP151PRVKEYSIZE);
            auto oppositeAadKey = oppositeSessionKey.getSliceRef(
               BIP151PRVKEYSIZE, BIP151PRVKEYSIZE);

            chacha20Poly1305Rekey(mainKey, BIP151PRVKEYSIZE,
               false, reqIDKey, resIDKey, oppositeMainKey);
            chacha20Poly1305Rekey(aadKey, BIP151PRVKEYSIZE,
               false, reqIDKey, resIDKey, oppositeAadKey);
         }

         //upload new keys to chacha session
         chacha20poly1305_init(&sessionCTX_, hkdfKeySet_.data(), hkdfKeySet_.size());

         //reset session usage counter
         bytesOnCurKeys_ = 0;
         break;

      default:
         // You should never get here. It's fine to fail silently,
         // channel will collapse
         break;
   }
}

// A function that checks to see if an incoming encack message is requesting a
// rekey. See the "Re-Keying" section of the BIP 151 spec.
// 
// IN:  inMsg - Pointer to a message to check for a rekey request. Must be 33 bytes.
// OUT: None
// RET: 0 if rekey, any other value if not rekey.
int BIP151Session::inMsgIsRekey(const BinaryDataRef& inMsg) const
{
   int retVal = -1;
   if (inMsg.getSize() == BIP151PUBKEYSIZE) {
      retVal = std::memcmp(inMsg.getPtr(), rekeyMsg, BIP151PUBKEYSIZE);
   }
   return retVal;
}

// A helper function that encrypts a payload. The code expects the BIP 151
// encrypted messages structure, minus the MAC (Poly1305) tag. The encrypted
// payload *will* include the MAC tag.
//
// IN:  plainData - Plaintext data to encrypt.
//      cipherSize - The size of the ciphertext buffer. The size *must* be at
//                   least 16 bytes larger than the plaintext buffer, as the
//                   cipher will include the Poly1305 tag.
// OUT: cipherData - The encrypted plaintext data and the Poly1305 tag.
// RET: -1 if failure, 0 if success
int BIP151Session::encPayload(uint8_t* cipherData, size_t cipherSize,
   const BinaryDataRef& plainData)
{
   int retVal = -1;
   assert(cipherSize >= (plainData.getSize() + POLY1305MACLEN));

   if (chacha20poly1305_crypt(&sessionCTX_,
      seqNum_, cipherData, plainData.getPtr(),
      plainData.getSize() - AUTHASSOCDATAFIELDLEN, AUTHASSOCDATAFIELDLEN,
      CHACHAPOLY1305_AEAD_ENC) == -1) {
      LOGERR << "Encryption at sequence number " << seqNum_ << " failed.";
   } else {
      retVal = 0;
   }

   ++seqNum_;
   bytesOnCurKeys_ += plainData.getSize();
   return retVal;
}

// A helper function that decrypts a payload. The code expects the BIP 151
// encrypted messages structure, with the MAC (Poly1305) tag. The decrypted
// payload *will not* include the MAC tag but the tag will be authenticated
// before decryption occurs.
//
// IN:  cipherData - The buffer (w/ MAC tag) to decrypt. Must be at least 16
//                   bytes larger than the resulting plaintext buffer.
// OUT: plainData  - The decrypted ciphertext data, without the no Poly1305 tag.
// RET: -1 if failure, 0 if success.
//      If the decrypted length is bigger than the potential max clear text
//      size, return the decrypted length instead
int BIP151Session::decPayload(const BinaryDataRef& cipherData,
   BinaryData& plainData)
{
   int retVal = -1;
   if (cipherData.getSize() < (POLY1305MACLEN + AUTHASSOCDATAFIELDLEN)) {
      return retVal;
   }

   //decrypt clear text length
   uint32_t decryptedLen = 0;
   chacha20poly1305_get_length(&sessionCTX_,
      &decryptedLen, seqNum_,
      cipherData.getPtr(), cipherData.getSize());

   //sanity checks
   if (decryptedLen + POLY1305MACLEN + AUTHASSOCDATAFIELDLEN > cipherData.getSize()) {
      return decryptedLen;
   }
   if (decryptedLen + AUTHASSOCDATAFIELDLEN > plainData.getSize()) {
      return retVal;
   }

   //decrypt message body
   if (chacha20poly1305_crypt(&sessionCTX_,
      seqNum_, plainData.getPtr(), cipherData.getPtr(),
      decryptedLen, AUTHASSOCDATAFIELDLEN,
      CHACHAPOLY1305_AEAD_DEC) == -1) {
      LOGERR << "Decryption at sequence number " << seqNum_ << " failed.";
   } else {
      retVal = 0;
   }

   ++seqNum_;
   bytesOnCurKeys_ += plainData.getSize();
   return retVal;
}

// IN:  bip150ReqIDKey - pubkey of the requester, relative to channel direction
// IN:  bip150ResIDKey - pubkey of the responder, relative to channel direction
// IN:  oppositeChannelCipherKey - opposite session hdkf keys
// OUT: keyToUpdate - The updated key (ChaCha20 or Poly1305).
// RET: None
void BIP151Session::chacha20Poly1305Rekey(uint8_t* keyToUpdate,
   size_t keySize, bool bip151Rekey,
   const BinaryDataRef& bip150ReqIDKey, const BinaryDataRef& bip150ResIDKey,
   const BinaryDataRef& oppositeChannelCipherKey)
{
   assert(keySize == BIP151PRVKEYSIZE);

   if (bip151Rekey == true) {
      // Generate, via 2xSHA256, a new symmetric key.
      std::array<uint8_t, 64> hashData1;
      std::copy(std::begin(sessionID_), std::end(sessionID_), &hashData1[0]);
      std::copy(keyToUpdate, keyToUpdate + keySize, &hashData1[BIP151PRVKEYSIZE]);
      btc_hash(hashData1.data(), hashData1.size(), keyToUpdate);
   } else {
      assert(bip150ReqIDKey.getSize() == BIP151PUBKEYSIZE);
      assert(bip150ResIDKey.getSize() == BIP151PUBKEYSIZE);
      assert(oppositeChannelCipherKey.getSize() == BIP151PRVKEYSIZE);

      // Generate, via 2xSHA256, a new symmetric key.
      std::array<uint8_t, 162> hashData2;
      std::copy(std::begin(sessionID_), std::end(sessionID_), &hashData2[0]);
      std::copy(keyToUpdate, keyToUpdate + keySize, &hashData2[BIP151PRVKEYSIZE]);
      std::copy(oppositeChannelCipherKey.getPtr(),
         oppositeChannelCipherKey.getPtr() + oppositeChannelCipherKey.getSize(),
         &hashData2[BIP151PRVKEYSIZE + keySize]);

      std::copy(bip150ReqIDKey.getPtr(),
         bip150ReqIDKey.getPtr() + bip150ReqIDKey.getSize(),
         &hashData2[BIP151PRVKEYSIZE + keySize +
            oppositeChannelCipherKey.getSize()]);
      std::copy(bip150ResIDKey.getPtr(),
         bip150ResIDKey.getPtr() + bip150ResIDKey.getSize(),
         &hashData2[BIP151PRVKEYSIZE + keySize +
            oppositeChannelCipherKey.getSize() + bip150ReqIDKey.getSize()]);
      btc_hash(hashData2.data(), hashData2.size(), keyToUpdate);
   }
}

// A helper function that confirms whether or not we have a valid ciphersuite,
// and sets an internal variable.
// 
// IN:  inCipher - The incoming cipher type.
// OUT: None
// RET: -1 if failure, 0 if success
int BIP151Session::setCipherType(BIP151SymCiphers inCipher)
{
   int retVal = -1;
   if (cipherType_ != BIP151SymCiphers::INVALID) {
      LOGERR << "BIP 151 - ciphersuite already set";
      return retVal;
   }

   if (isCipherValid(inCipher) == true) {
      cipherType_ = inCipher;
      retVal = 0;
   } else {
      LOGERR << "BIP 151 - Invalid ciphersuite type ("
         << static_cast<int>(inCipher) << ")";
   }
   return retVal;
}

// A helper function that confirms whether or not we have a valid ciphersuite,
// and sets an internal variable.
// 
// IN:  inCipher - The incoming cipher type.
// OUT: None
// RET: True if valid, false if not valid.
bool BIP151Session::isCipherValid(BIP151SymCiphers inCipher)
{
   // For now, this is simple. Just check for ChaChaPoly1305.
   bool retVal = false;
   if (inCipher == BIP151SymCiphers::CHACHA20POLY1305_OPENSSH) {
      retVal = true;
   }
   return retVal;
}

// Function that gets the data sent alongside an encinit message. This can be
// used to get data for encrypted and unencrypted encinit messages.
//
// IN:  inCipher - The cipher type to send.
// OUT: initBuffer - The buffer with the encinit data.
// RET: -1 if failure, 0 if success
int BIP151Session::getEncinitData(BinaryData& initBuffer, BIP151SymCiphers inCipher)
{
   int retVal = -1;
   if (setCipherType(inCipher) != 0) {
      return retVal;
   }
   if (initBuffer.getSize() != ENCINITMSGSIZE) {
      LOGERR << "BIP 151 - encinit data buffer is not " << ENCINITMSGSIZE
         << " bytes.";
      return retVal;
   }

   // Ideally, libbtc would be used here. Unfortunately, it doesn't output
   // compressed public keys (although it's aware of them). Go straight to
   // libsecp256k1 until this is fixed upstream.
   secp256k1_pubkey ourPubKey;
   size_t copyLen = BIP151PUBKEYSIZE;
   if (!secp256k1_ec_pubkey_create(secp256k1_ecdh_ctx,
      &ourPubKey, genSymECDHPrivKey_.privkey)) {
      LOGERR << "BIP 151 - Invalid public key creation. Closing connection.";
      return retVal;
   }
   secp256k1_ec_pubkey_serialize(secp256k1_ecdh_ctx,
      initBuffer.getPtr(),
      &copyLen, &ourPubKey,
      SECP256K1_EC_COMPRESSED);
   initBuffer[33] = static_cast<uint8_t>(cipherType_);

   retVal = 0;
   return retVal;
}

// Function that gets the data sent alongside an encack message. This can be
// used to get data for encrypted and unencrypted encack messages.
//
// IN:  N/A
// OUT: ackBuffer - The buffer with the encinit data.
// RET: -1 if failure, 0 if success
int BIP151Session::getEncackData(BinaryData& ackBuffer)
{
   int retVal = -1;

   if (!encinit_) {
      LOGERR << "BIP 151 - Getting encack data before an encinit has arrived.";
      return retVal;
   } if (ackBuffer.getSize() != BIP151PUBKEYSIZE) {
      LOGERR << "BIP 151 - encack data buffer is not " << BIP151PUBKEYSIZE
         << " bytes.";
      return retVal;
   }

   // Ideally, libbtc would be used here. Unfortunately, it doesn't output
   // compressed public keys (although it's aware of them). Go straight to
   // libsecp256k1 until this is fixed upstream.
   secp256k1_pubkey ourPubKey;
   size_t copyLen = BIP151PUBKEYSIZE;
   if (!secp256k1_ec_pubkey_create(secp256k1_ecdh_ctx,
      &ourPubKey, genSymECDHPrivKey_.privkey)) {
      LOGERR << "BIP 151 - Invalid encack public key creation.";
      return retVal;
   }
   secp256k1_ec_pubkey_serialize(secp256k1_ecdh_ctx,
      ackBuffer.getPtr(),
      &copyLen, &ourPubKey,
      SECP256K1_EC_COMPRESSED);

   // We're done with the ECDH key now. Nuke it. **Applies only to inbound sessions.**
   btc_privkey_cleanse(&genSymECDHPrivKey_);
   retVal = 0;
   return retVal;
}

// Default BIP 151 connection constructor.
// 
// IN:  None
// OUT: None
// RET: N/A
BIP151Connection::BIP151Connection(
   std::unique_ptr<NetworkPeers::PeerStoreView> peerView, bool oneWayAuth) :
   inSes_{false}, outSes_{true},
   bip150SM_{&inSes_, &outSes_, std::move(peerView), oneWayAuth}
{
   // The context must be set up before we can establish BIP 151 connections.
   assert(secp256k1_ecdh_ctx != nullptr);
}

// Overridden constructor for a BIP 151 connection. Sets out ECDH private keys
// used in the input and output sessions. USE WITH EXTREME CAUTION!!! Unless
// there's a very specific need for a pre-determined key (e.g., test harness or
// keys are HW-generated), using this will just get you into trouble.
// IN:  inSymECDHPrivKeyIn - ECDH private key for the inbound channel.
//      inSymECDHPrivKeyOut - ECDH private key for the outbound channel.
// OUT: None
// RET: N/A
BIP151Connection::BIP151Connection(
   btc_key* inSymECDHPrivKeyIn, btc_key* inSymECDHPrivKeyOut,
   std::unique_ptr<NetworkPeers::PeerStoreView> peerView, bool oneWayAuth) :
   inSes_(false, inSymECDHPrivKeyIn), outSes_(true, inSymECDHPrivKeyOut),
   bip150SM_(&inSes_, &outSes_, std::move(peerView), oneWayAuth)
{
   // The context must be set up before we can establish BIP 151 connections.
   assert(secp256k1_ecdh_ctx != nullptr);
}

// The function that handles incoming "encinit" messages.
// 
// IN:  inMsg - Buffer with the encinit msg contents. Emtpy if we're sending.
//      outDir - Boolean indicating if the message is outgoing or incoming.
// OUT: None
// RET: -1 if unsuccessful, 0 if successful.
int BIP151Connection::processEncinit(const BinaryDataRef& inMsg, bool outDir)
{
   int retVal = -1;
   if (inMsg.getSize() != ENCINITMSGSIZE) {
      LOGERR << "BIP 151 - encinit message size isn't " << ENCINITMSGSIZE
         << " bytes. Will shut down connection.";
      return retVal;
   }

   // The BIP 151 spec states that traffic is handled via two unidirectional
   // sessions. We should only get an encinit on the incoming session.
   if (!outDir) {
      if (inSes_.encinitSeen()) {
         LOGERR << "BIP 151 - Have already seen encinit (session ID " <<
            toHexStr(inSes_.getSessionID(), BIP151PRVKEYSIZE) <<
            ") - Closing the connection.";
         return retVal;
      }

      // Set keys and ciphersuite type as needed. For now, assume that if we're
      // kicking things off, we're using ChaCha20Poly1305.
      // Set up the session's symmetric keys and cipher type. If the functs fail,
      // they'll write log msgs.
      if (inSes_.setCipherType(static_cast<BIP151SymCiphers>(inMsg[33])) == 0 &&
         inSes_.symKeySetup(inMsg.getSliceRef(0, BIP151PUBKEYSIZE)) == 0) {
         // We've successfully handled the packet.
         inSes_.setEncinitSeen();
         retVal = 0;
      }
   } else {
      LOGERR << "BIP 151 - Received an encinit message on outgoing session " <<
         toHexStr(outSes_.getSessionID(), BIP151PRVKEYSIZE) <<
         ". This should not happen. Closing the connection.";
   }
   return retVal;
}

// The function that handles incoming and outgoing "encack" payloads.
// 
// IN:  inMsg - Buffer with the encack msg contents. Must be 33 bytes.
//      outDir - Boolean indicating if the message is outgoing or incoming.
// OUT: None
// RET: -1 if unsuccessful, 0 if successful.
int BIP151Connection::processEncack(const BinaryDataRef& inMsg, const bool outDir)
{
   int retVal = -1;
   if (inMsg.getSize() != BIP151PUBKEYSIZE) {
      LOGERR << "BIP 151 - encack message size isn't " << BIP151PUBKEYSIZE
         << " bytes. Will shut down connection.";
      return retVal;
   }

   // The BIP 151 spec states that traffic is handled via two unidirectional
   // sessions. We should only get an encack on the outgoing session.
   if (outDir) {
      // Valid only if we've already seen an encinit.
      if (!outSes_.encinitSeen()) {
         LOGERR << "BIP 151 - Received an encack message before an encinit. "
            << "Closing connection.";
         return retVal;
      }

      // We should never receive a rekey, just an initial keying.
      if (outSes_.inMsgIsRekey(inMsg) == 0) {
         LOGERR << "BIP 151 - Received a rekey message on outgoing session ID " <<
            toHexStr(outSes_.getSessionID(), BIP151PRVKEYSIZE) <<
            "). Closing connection.";
         return retVal;
      }

      if (outSes_.symKeySetup(inMsg) == 0) {
         outSes_.setEncackSeen();
         retVal = 0;
      }
   } else {
      // Incoming sessions should only see rekeys.
      if (inSes_.inMsgIsRekey(inMsg) != 0) {
         LOGERR << "BIP 151 - Received a non-rekey encack message on incoming " <<
            "session ID " << toHexStr(inSes_.getSessionID(), BIP151PRVKEYSIZE) <<
            ". This should not happen. Closing the connection.";
      } else {
         inSes_.sessionRekey(true, {}, {}, {});
         retVal = 0;
      }
   }
   return retVal;
}

////////////////////////////////////////////////////////////////////////////////
// ENCRYPTED PACKET OUTLINE, PER BIP 151:
// - Encrypted size of payload  (4 bytes)  (Uses the K1/AAD key for ChaCha20)
// - Encrypted payload  (Uses the "K1" key)
// --- Command length  (VarStr)
// --- Command  ("Command length" bytes)
// --- Length of command payload  (4 bytes)
// --- Payload  (Variable bytes)
// - MAC for the encrypted payload  (16 bytes)  (Uses the K2 key for Poly1305)
// - Whether or not encryption is successful, increment the seq ctr & # of bytes.
// - Check to see if a rekey's needed for the outgoing session. If so, do it.
////////////////////////////////////////////////////////////////////////////////

// Function used to assemble an encrypted packet.
//
// IN:  plainData - Plaintext buffer that will be encrypted.
//      cipherSize - Ciphertext buffer size.
// OUT: cipherData - The encrypted buffer. Must be 16 bytes larger than the
//                   plaintext buffer.
// RET: -1 if failure, 0 if success.
int BIP151Connection::assemblePacket(const BinaryDataRef& plainData,
   uint8_t* cipherData, size_t cipherSize)
{
   if (outSes_.encPayload(cipherData, cipherSize, plainData) != 0) {
      LOGERR << "BIP 151 - Session ID " <<
         toHexStr(outSes_.getSessionID(), BIP151PRVKEYSIZE) <<
         " encryption failed.";
      return -1;
   }
   return 0;
}

// Function used to decrypt a packet.
//
// IN:  cipherData - Encrypted buffer that will be decrypted.
//      cipherSize - Encrypted buffer size.
//      plainSize - Decrypted buffer size.
// OUT: plainData - The decrypted packet. Must be no more than 16 bytes smaller
//                  than the ciphertext buffer.
// RET: -1 if failure, 0 if success. If the decrypted length is bigger than
// than the potential max clear text size, return the decrypted length instead
int BIP151Connection::decryptPacket(const BinaryDataRef& cipherData,
   BinaryData& plainData)
{
   return inSes_.decPayload(cipherData, plainData);
}

// Function that gets encinit data from the outbound session. Assume the session
// will do incoming data validation.
//
// IN:  inCipher - The cipher type to get.
// OUT: encinitBuf - The data to go into an encinit messsage.
// RET: -1 if not successful, 0 if successful.
int BIP151Connection::getEncinitData(BinaryData& encinitBuf,
   BIP151SymCiphers inCipher)
{
   outSes_.setEncinitSeen();
   return outSes_.getEncinitData(encinitBuf, inCipher);
}

// Function that gets encack data from the inbound session. Assume the session
// will do incoming data validation.
//
// IN:  N/A
// OUT: encackBuf - The data to go into an encack messsage. Must be >=33 bytes.
// RET: -1 if not successful, 0 if successful.
int BIP151Connection::getEncackData(BinaryData& encackBuf)
{
   inSes_.setEncackSeen();
   int retVal = inSes_.getEncackData(encackBuf);
   return retVal;
}

// The function that kicks off a rekey for a connection's outbound session.
//
// IN:  N/A
// OUT: encackBuf - The data to go into the encack rekey messsage. Must
//                  be >=64 bytes.
// RET: -1 if failure, 0 if successful.
int BIP151Connection::bip151RekeyConn(BinaryData& encackBuf)
{
   assert(encackBuf.getSize() >= 64);

   BinaryData clrRekeyBuf;
   clrRekeyBuf.resize(48);
   if (getRekeyBuf(clrRekeyBuf) == -1) {
      return -1;
   }

   if (assemblePacket(clrRekeyBuf.getRef(),
      encackBuf.getPtr(), encackBuf.getSize()) == -1) {
      return -1;
   }

   outSes_.sessionRekey(true, {}, {}, {});
   return 0;
}

// Function that returns the connection's input or output session ID.
// 
// IN:  dirIsOut - Bool indicating if the direction is outbound.
// OUT: None
// RET: A pointer to a 32 byte array with the session ID.
const uint8_t* BIP151Connection::getSessionID(bool dirIsOut) const
{
   return dirIsOut ? outSes_.getSessionID() : inSes_.getSessionID();
}

// The function that handles incoming and outgoing "authchallenge" payloads.
//
// IN:  inMsg - Buffer with the authchallenge msg contents, must be 32 bytes.
//      requesterSent - Indicates whether or not the requester sent the msg.
// OUT: None
// RET: -1 if unsuccessful (code setup), 0 if successful, 1 if unsuccessful
//      (bad hash).
int BIP151Connection::processAuthchallenge(const BinaryDataRef& inMsg, bool requesterSent)
{
   return bip150SM_.processAuthchallenge(inMsg, requesterSent);
}

// The function that handles incoming and outgoing "authreply" payloads.
//
// IN:  inMsg - Buffer with the authreply msg contents. Must be 64 bytes.
//      requesterSent - Indicates whether or not the requester sent the msg.
// OUT: None
// RET: -1 if unsuccessful, 0 if successful.
// RET: -1 if unsuccessful (code setup), 0 if successful, 1 if unsuccessful
//      (bad signature).
int BIP151Connection::processAuthreply(const BinaryDataRef& inMsg, bool requesterSent)
{
   return bip150SM_.processAuthreply(inMsg, requesterSent);
}

// The function that handles incoming and outgoing "authpropose" payloads.
//
// IN:  inMsg - Buffer with the authreply msg contents.
// OUT: None
// RET: -1 if unsuccessful (code setup), 0 if successful, 1 if unsuccessful
//      (bad hash).
int BIP151Connection::processAuthpropose(const BinaryDataRef& inMsg)
{
   return bip150SM_.processAuthpropose(inMsg);
}

// Function that gets the data sent alongside an authchallenge message.
//
// IN:  targetIPPort - The IP:Port/Name of the target. This name is used to
//                     to find the relevant public key, needed to generate the 
//                     challenge hash (step 1). This argument is ignored in
//                     step 4 (requesterSent == false).
//      requesterSent - Indicates if the requester wants the data (true - step
//                      1) or the responder (false - step 4). In step 4, the 
//                      challenge key is set to the key from selected by the
//                      AuthPropose process.
// OUT: authchallengeBuf - The buffer with the authchallenge data.
// RET: -1 if failure, 0 if success, 1 if AUTHPROPOSE validation was a failure.
int BIP151Connection::getAuthchallengeData(BinaryData& authchallengeBuf,
   const std::string& targetIPPort, bool requesterSent)
{
   return bip150SM_.getAuthchallengeData(
      authchallengeBuf, targetIPPort, requesterSent);
}

// Function that gets the data sent alongside an authreply message.
//
// IN: responderSent - Indicates if the responder wants the data (true - step
//                   2) or the requester (false - step 5).
// OUT: authReply    - The buffer with the authreply data.
// RET: -1 if failure, 0 if success, 1 if AUTHREPLY validation was a failure.
int BIP151Connection::getAuthreplyData(
   BinaryData& authReply, bool responderSent)
{
   return bip150SM_.getAuthreplyData(authReply, responderSent);
}

// Function that gets the data sent alongside an authpropose message.
//
// OUT: authproposeBuf - The buffer with the authpropose data.
// RET: -1 if failure, 0 if success
int BIP151Connection::getAuthproposeData(BinaryData& authproposeBuf)
{
   return bip150SM_.getAuthproposeData(authproposeBuf);
}

// Get a rekey message. Will be in the BIP 151 "encrypted message" format.
//
// OUT: encackMsg - The data to go into the encack rekey messsage. Must
//                  be >=48 bytes.
// RET: -1 if failure, 0 if successful.
int BIP151Connection::getRekeyBuf(BinaryData& encackMsg) const
{
   // If the connection isn't complete yet, the function fails.
   if (connectionComplete() == false) {
      LOGERR << "BIP 151 - Attempting a rekey before connection is completed.";
      return -1;
   }

   // 5: 4 bytes for flat size prefix
   //    1 for cmd.size + BIP151PUBKEYSIZE in varint
   static const auto cmd = "encack"sv;
   static const uint32_t totalLen = 5 + cmd.size() + BIP151PUBKEYSIZE;
   static const uint32_t pubKeySize = BIP151PUBKEYSIZE;

   if (encackMsg.getSize() < totalLen) {
      throw std::runtime_error("invalid rekey buffer size");
   }
   auto ptr = encackMsg.getPtr();
   memcpy(ptr, &totalLen, 4);
   memset(ptr + 4, 6, 1);
   memcpy(ptr + 5, cmd.data(), cmd.size());
   memcpy(ptr + 11, &pubKeySize, 4);
   memset(ptr + 15, 0, 33);
   return 0;
}

// Rekey bip151 channels after a succesful bip150 handshake
//
// IN:  None
// OUT: None
// RET: -1 if failure, 0 if successful
void BIP151Connection::bip150HandshakeRekey()
{
   bip150SM_.rekey();
}

// Check if this peer name exists and that it matches this public key
//
// IN:  pubkey - the public key
//      name - the ip:port or domain name
// OUT: None
// RET: true if the key/name pair matches, otherwise false
bool BIP151Connection::havePublicKey(const BinaryDataRef& pubkey,
   const std::string& name) const
{
   return bip150SM_.havePublicKey(pubkey, name);
}

BinaryDataRef BIP151Connection::getOwnPubKey() const
{
   return bip150SM_.getOwnPubKey();
}

std::string BIP151Connection::getBIP150Fingerprint() const
{
   return getKeyFingerprint(getOwnPubKey());
}

////////////////////////////////////////////////////////////////////////////////
// Startup code for BIP 150. Used for basic initialization of relevant global
// DBs and to let users know 150 is ready. Call alongside BIP 151 startup. It is
// safe to call this function if switching to a new IP version mid-stream,
// although it's not recommended except for test purposes.
// 
// IN:  ipVer - The IP version to be used. Valid values are 4, 6, and 20 (20
//              indicates that Armory will use Tor).
//      publicRequester - false: auth both sides
//                        true: auth responder (server), allow anonymous requester (client)
// OUT: None
// RET: N/A
void startupBIP150CTX(uint32_t ipVer)
{
   ::ipType_ = ipVer;
   memset(::oneWayAuthClientPubKey, 0xFF, 33);
   memset(::rekeyMsg, 0, 33);
}

// Overridden constructor for a BIP 150 state machine session. Sets the internal
// variables. Must be used instead of the default constructor.
// 
// IN:  incomingSes - 151 connection's incoming session.
//      outgoingSes - 151 connection's outgoing session.
// OUT: None
// RET: N/A
BIP150StateMachine::BIP150StateMachine(
   BIP151Session* incomingSes, BIP151Session* outgoingSes,
   std::unique_ptr<NetworkPeers::PeerStoreView> peerView, bool oneWayAuth) :
   curState_(BIP150State::INACTIVE),
   inSess_(incomingSes), outSess_(outgoingSes),
   peerView_(std::move(peerView)), oneWayAuth_(oneWayAuth)
{}

// Function that gets AUTHCHALLENGE data for the state machine. Works for
// steps 1 or 4 of the 150 handshake.
//
// IN:  targetIPPort - The IP:Port/Name of the target. This name is used to
//                     to find the relevant public key, needed to generate the 
//                     challenge hash (step 1). This argument is ignored in
//                     step 4 (requesterSent == false).
//      requesterSent - Indicates if the requester wants the data (true - step
//                      1) or the responder (false - step 4). In step 4, the 
//                      challenge key is set to the key from selected by the
//                      AuthPropose process.
// OUT: output - The data to go into an AUTHCHALLENGE messsage. Must be ==32 bytes.
// RET: -1 if not successful, 0 if successful, 1 if AUTHPROPOSE validation was
//      a failure.
int BIP150StateMachine::getAuthchallengeData(BinaryData& output,
   const std::string& targetIPPort, bool requesterSent)
{
   int retVal = -1;
   BIP151Session* checkSes = nullptr;

   if (requesterSent == true) {
      resetSM();
      curState_ = BIP150State::CHALLENGE1;
      checkSes = outSess_;
   } else {
      // Make sure the current state is acceptable before proceeding.
      if (curState_ != BIP150State::PROPOSE) {
         LOGERR << "BIP 150 - Attempting to process AUTHCHALLENGE (2) message "
            << "when state is not correct. Setting BIP 150 to error state.";
         return errorSM(retVal);
      }
      curState_ = BIP150State::CHALLENGE2;
      checkSes = inSess_;
   }

   if (checkSes->handshakeComplete() == false) {
      LOGERR << "BIP 150 - Cannot get AUTHCHALLENGE data before BIP 151 "
         << "handshake is complete.";
      return errorSM(retVal);
   }
   if (output.getSize() != BIP151PRVKEYSIZE) {
      LOGERR << "BIP 150 - AUTHCHALLENGE data buffer is not " << BIP151PRVKEYSIZE
         << " bytes.";
      return errorSM(retVal);
   }

   // Check the known-peers DB and generate a key if the target IP/Port is found.
   try {
      if (requesterSent == true) {
         chosenAuthPeerKey_ = peerView_->getPubKeyRef(targetIPPort);
      }
   } catch (const std::exception&) {
      LOGERR << "BIP 150 - Unable to find IP:Port " << targetIPPort
         << " in known-peers list.";
      return errorSM(retVal);
   }

   // What's hashed depends on if AUTHPROPOSE was verified.
   if (requesterSent == true) {// AUTHCHALLENGE 1
      //client side auth challenge, hash server's expected pubkey
      retVal = buildHashData(output, chosenAuthPeerKey_.getRef(), true);
   } else {
      //server side challenge
      if (oneWayAuth_) { //AC 2 GOOD
         //1-way: hash stand-in value (0xFF * 33)
         BinaryDataRef oneWayStandInKey{oneWayAuthClientPubKey, 33};
         retVal = buildHashData(output, oneWayStandInKey, true);
      } else {
         //2-way: hash client's expected pubkey
         retVal = buildHashData(output, chosenAuthPeerKey_.getRef(), true);
      }
   }

   if (retVal != 0) {
      return errorSM(retVal);
   }
   return retVal;
}

// Function that gets AUTHREPLY data for the state machine. Works for
// steps 2 or 5 of the 150 handshake.
//
// IN:  bufSize - AUTHREPLY data buffer size. Must be >=64 bytes.
//      responderSent - Indicates if the responder wants the data (true - step
//                      2) or the requester (false - step 5).
// OUT: buf - The data to go into an AUTHREPLY messsage.
// RET: -1 if not successful, 0 if successful, 1 if AUTHCHALLENGE validation
//      was a failure.
int BIP150StateMachine::getAuthreplyData(BinaryData& output, bool responderSent)
{
   int retVal = -1;
   if (responderSent == true) {
      // Make sure the current state is acceptable before proceeding.
      if (curState_ != BIP150State::CHALLENGE1) {
         LOGERR << "BIP 150 - Attempting to process AUTHREPLY (1) message "
            << "when state is not correct. Setting BIP 150 to error state.";
         return errorSM(retVal);
      }
      curState_ = BIP150State::REPLY1;
   } else {
      // Make sure the current state is acceptable before proceeding.
      if (curState_ != BIP150State::CHALLENGE2) {
         LOGERR << "BIP 150 - Attempting to process AUTHREPLY (2) message "
            << "when state is not correct. Setting BIP 150 to error state.";
         return errorSM(retVal);
      }
      curState_ = BIP150State::REPLY2;
   }

   if (outSess_->handshakeComplete() == false) {
      LOGERR << "BIP 150 - Cannot get AUTHREPLY data before BIP 151 handshake "
         << "is complete.";
      return errorSM(retVal);
   }

   if (output.getSize() != BIP151PRVKEYSIZE * 2) {
      LOGERR << "BIP 150 - AUTHPROPOSE data buffer is not "
         << BIP151PRVKEYSIZE * 2 << " bytes.";
      return errorSM(retVal);
   }

   // Sign the session ID. libbtc assumes data to sign will be 32 bytes.
   // Thankfully, the session ID is 32 bytes.
   // NB: Behind-the-scenes, libsecp256k1 assumes RFC 6979 nonces.
   if (!responderSent && oneWayAuth_) {
      /*
      Client side 1-way auth: return own pubkey
      */
      auto ownPubKey = peerView_->getPubKeyRef("own");
      LOGERR << "BIP 150 - own public key is not 33 bytes long";
      if (ownPubKey.getSize() != BIP151PUBKEYSIZE) {
         return errorSM(retVal);
      }
      std::memcpy(output.getPtr(), ownPubKey.getPtr(), BIP151PUBKEYSIZE);
      std::memset(output.getPtr() + BIP151PUBKEYSIZE, 0,
         output.getSize() - BIP151PUBKEYSIZE);
      retVal = 0;
   } else {
      if (peerView_->signChallenge(outSess_->getSessionID(), output) == false) {
         LOGERR << "BIP 150 - Unable to sign AUTHREPLY data.";
         return errorSM(retVal);
      }
      retVal = 0;
   }
   return retVal;
}

// Function that gets AUTHPROPOSE data for the state machine. Works for
// step 3 of the BIP150 handshake (client side only).
//
// OUT: output - The data to go into an AUTHPROPOSE messsage.
// RET: -1 if not successful, 0 if successful.
int BIP150StateMachine::getAuthproposeData(BinaryData& output)
{
   int retVal = -1;

   // Make sure the current state is acceptable before proceeding.
   if (curState_ != BIP150State::REPLY1) {
      LOGERR << "BIP 150 - Attempting to process AUTHREPLY message when "
         << "state is not correct. Setting BIP 150 to error state.";
      return errorSM(retVal);
   }
   curState_ = BIP150State::PROPOSE;

   if (outSess_->handshakeComplete() == false) {
      LOGERR << "BIP 150 - Cannot get AUTHPROPOSE data before BIP 151 "
         << "handshake is complete.";
      return errorSM(retVal);
   }

   if (output.getSize() != BIP151PRVKEYSIZE) {
      LOGERR << "BIP 150 - AUTHPROPOSE data buffer is not " << BIP151PRVKEYSIZE
         << " bytes.";
      return errorSM(retVal);
   }

   // Build the data hash to be returned.
   BinaryDataRef pubKeyRef;
   if (oneWayAuth_) {
      /*
      Send expected pubkey for 1-way auth (server does not auth client).
      This is to fail 2-way clients talking to 1-way server (so that a 
      2-way client can only talk to a private server).
      */
      pubKeyRef.setRef(oneWayAuthClientPubKey, BIP151PUBKEYSIZE);
   } else {
      //for 2-way auth, send own pubkey
      pubKeyRef = peerView_->getPubKeyRef("own");
   }

   retVal = buildHashData(output, pubKeyRef, true);
   if (retVal != 0) {
      return errorSM(retVal);
   }
   return retVal;
}

// The function that handles incoming AUTHCHALLENGE messages.
// 
// IN:  inData - The incoming hash.
//      requesterSent - Indicates whether the responder (step 1) or requester
//                      (step 4) is processing the data.
// OUT: None
// RET: -1 if unsuccessful (bad code setup), 0 if successful, 1 if unsuccessful
//      (unable to verify hash).
int BIP150StateMachine::processAuthchallenge(const BinaryDataRef& inData,
   bool requesterSent)
{
   int retVal = -1;
   if (requesterSent == true) {
      resetSM();
      curState_ = BIP150State::CHALLENGE1;
   } else {
      // Make sure the current state is acceptable before proceeding.
      if (curState_ != BIP150State::PROPOSE) {
         LOGERR << "BIP 150 - Attempting to process AUTHCHALLENGE (2) message "
            << "when state is not correct. Setting BIP 150 to error state.";
         return errorSM(retVal);
      }
      curState_ = BIP150State::CHALLENGE2;
   }

   if (inData.getSize() != BIP151PRVKEYSIZE) {
      LOGERR << "BIP 150 - wrong auth challenge size";
      return errorSM(retVal);
   }

   // Build a hash and compare.
   BinaryData challengeHash;
   challengeHash.resize(BIP151PRVKEYSIZE);
   auto pubKeyRef = peerView_->getPubKeyRef("own");
   if (!requesterSent && oneWayAuth_) {
      /*
      Client side processAuthChallenge, use stand-in value if
      we're doing a 1-way auth.
      */
      pubKeyRef.reset();
      pubKeyRef.setRef(oneWayAuthClientPubKey, BIP151PUBKEYSIZE);
   }

   if (buildHashData(challengeHash, pubKeyRef, false) == -1) {
      LOGERR << "BIP 150 - Unable to process AUTHCHALLENGE message.";
      return errorSM(retVal);
   }

   if (inData != challengeHash) {
      LOGERR << "BIP 150 - AUTHCHALLENGE message cannot be verified.";
      return errorSM(retVal);
   }

   retVal = 0;
   return retVal;
}

// The function that handles incoming AUTHCHALLENGE messages.
//
// IN:  inData - The incoming signature.
//      requesterSent - Indicates whether the requester (step 2) or responder
//                      (step 5) is processing the data.
// OUT: None
// RET: -1 if unsuccessful (bad code setup), 0 if successful, 1 if unsuccessful
//      (unable to verify signature).
int BIP150StateMachine::processAuthreply(
   const BinaryDataRef& inData, bool requesterSent)
{
   int retVal = -1;
   if (requesterSent == true) {
      // Make sure the current state is acceptable before proceeding.
      if (curState_ != BIP150State::CHALLENGE1) {
         LOGERR << "BIP 150 - Attempting to process AUTHREPLY (1) message "
            << "when state is not correct. Setting BIP 150 to error state.";
         return errorSM(retVal);
      }
      curState_ = BIP150State::REPLY1;
   } else {
      // Make sure the current state is acceptable before proceeding.
      if (curState_ != BIP150State::CHALLENGE2) {
         LOGERR << "BIP 150 - Attempting to process AUTHREPLY (2) message "
            << "when state is not correct. Setting BIP 150 to error state.";
         return errorSM(retVal);
      }
      curState_ = BIP150State::REPLY2;
   }

   if (inData.getSize() != BIP151PRVKEYSIZE * 2) {
      LOGERR << "BIP 150 - wrong auth reply size";
      return errorSM(retVal);
   }

   if (oneWayAuth_ && !requesterSent) {
      /*
      1-way auth server. Auth reply from client carries its pubkey. We don't
      know that key since a 1-way server does not check for known peers. Set
      the peer auth key in order to rekey successfully.
      */
      chosenAuthPeerKey_ = inData.getSliceRef(0, BIP151PUBKEYSIZE);
      return 0;
   }

   // Verify the incoming sig. Note that libbtc has a quirk. It only verifies
   // DER-encoded sigs. We must convert our compact sig to DER and then verify
   // the sig (and maybe upstream a patch to do it all in one pass).
   // NB: A DER sig is 72 bytes at most, so plan for that with the buffer.
   std::array<uint8_t, 72> derSig{};
   size_t derSigSize = derSig.size(); // In/Out for libsecp256k1
   if (btc_ecc_compact_to_der_normalized(
      (uint8_t*)inData.getPtr(), derSig.data(), &derSigSize) == false) {
      LOGERR << "BIP 150 - AUTHREPLY unable to convert signature to DER.";
      retVal = 1;
      return errorSM(retVal);
   }

   //2-way auth: check client signed session id
   const btc_pubkey* hashKey;
   if (btc_ecc_verify_sig(chosenAuthPeerKey_.getPtr(), true,
      inSess_->getSessionID(), &derSig[0], derSigSize) == true) {
      retVal = 0;
   } else {
      LOGERR << "BIP 150 - AUTHREPLY signature cannot be verified.";
      retVal = 1;
      return errorSM(retVal);
   }
   return retVal;
}

// The function that handles incoming AUTHPROPOSE messages.
// 
// IN:  inData - The incoming hash.
// OUT: None
// RET: -1 if unsuccessful (bad code setup), 0 if successful, 1 if unsuccessful
//      (unable to verify signature).
int BIP150StateMachine::processAuthpropose(const BinaryDataRef& inData)
{
   int retVal = -1;

   // Make sure the current state is acceptable before proceeding.
   if (curState_ != BIP150State::REPLY1) {
      LOGERR << "BIP 150 - Attempting to process AUTHPROPOSE message when "
         << "state is not correct. Setting BIP 150 to error state.";
      return errorSM(retVal);
   }
   curState_ = BIP150State::PROPOSE;

   if (inData.getSize() != BIP151PRVKEYSIZE) {
      LOGERR << "BIP 150 - wrong auth propose size";
      return errorSM(retVal);
   }

   BinaryData proposeHash;
   proposeHash.resize(BIP151PRVKEYSIZE);
   if (oneWayAuth_) {
      BinaryDataRef oneWayStandInKey{oneWayAuthClientPubKey, BIP151PUBKEYSIZE};
      if (buildHashData(proposeHash, oneWayStandInKey, false) == -1) {
         LOGERR << "BIP 150 - Unable to verify AUTHPROPOSE message.";
         return errorSM(retVal);
      }

      // Compare hashes. If they match, we're happy!
      if (inData != proposeHash) {
         LOGERR << "BIP 150 - Unable to verify AUTHPROPOSE message.";
         return errorSM(retVal);
      }

      //this is all we need for 1-way auth, return 1 to specify that
      return 1;
   }

   /*
   Iterate through the authorized-users DB and attempt to replicate the
   incoming hash. This is an expensive search, only performed for 2-way
   auth (where the set of known peers is expected to be small).
   */
   const SecureBinaryData* validKey = nullptr;
   const auto& peerKeys = peerView_->getPublicKeyMap();
   for (const auto& peerKey : peerKeys) {
      if (buildHashData(proposeHash, peerKey.first.getRef(), false) == -1) {
         continue;
      }

      // Compare hashes. If they match, we're happy!
      if (inData == proposeHash) {
         validKey = &peerKey.first;
         break;
      }
   }

   // If we found a valid key, save it for later processing purposes.
   if (validKey == nullptr) {
      LOGERR << "BIP 150 - Unable to verify AUTHPROPOSE message.";
      return errorSM(retVal);
   } else {
      chosenAuthPeerKey_ = *validKey;
      retVal = 0;
   }
   return retVal;
}

// Internal function that builds hashes related to AUTHCHALLENGE and AUTHPROPOSE
// messages. Note that the code explicitly assumes that the buffers are the
// appropriate size.
//
// IN:  pubKey - The compressed public key to be hashed. Must be >=33 bytes.
//      willSendHash - Indicates if the related session is the sender (true) or
//                     receiver (false).
// OUT: outHash - The resultant hash. Must be >= 32 bytes.
// RET: -1 if not successful, 0 if successful.
int BIP150StateMachine::buildHashData(BinaryData& outHash,
   const BinaryDataRef& pubKey, bool willSendHash)
{
   if (outHash.getSize() != 32 || pubKey.getSize() != BIP151PUBKEYSIZE) {
      throw std::runtime_error("invalid arg sizes");
   }

   // Get the session pointer. Assume it's 32 bytes long.
   const uint8_t* sessionID = willSendHash ?
      outSess_->getSessionID() :
      inSess_->getSessionID();

   // Assemble the data to hash.
   std::array<uint8_t, 66> hashData{};
   std::copy(sessionID, sessionID + BIP151PRVKEYSIZE, &hashData[0]);
   switch (curState_)
   {
      case BIP150State::CHALLENGE1:
         hashData[32] = 'i';
         break;
      case BIP150State::PROPOSE:
         hashData[32] = 'p';
         break;
      case BIP150State::CHALLENGE2:
         hashData[32] = 'r';
         break;
      default:
         LOGERR << "BIP 150 - Wrong state when trying to deal with an "
            << "AUTHCHALLENGE or AUTHPROPOSE message's hash.";
         return -1;
   }
   std::copy(pubKey.getPtr(), pubKey.getPtr() + BIP151PUBKEYSIZE, &hashData[33]);

   // 2xSHA-256 and return the result.
   btc_hash(hashData.data(), hashData.size(), outHash.getPtr());
   return 0;
}

// Function that resets the BIP 150 state machine. Can be called by the user but
// is primarily intended for internal use.
//
// IN:  None
// OUT: None
// RET: None
void BIP150StateMachine::resetSM()
{
   curState_ = BIP150State::INACTIVE;
}

// Function that sets the error state for the state machine. Must be called
// whenever an error state occurs. 
//
// IN:  None
// OUT: outVal - The error state value to return.
// RET: None
int BIP150StateMachine::errorSM(const int& outVal)
{
   curState_ = BIP150State::ERR_STATE;
   chosenAuthPeerKey_.clear();
   return outVal;
}

// Rekey bip151 channels after a succesful bip150 handshake
//
// IN:  None
// OUT: None
// RET: -1 if failure, 0 if successful
void BIP150StateMachine::rekey()
{
   auto ownPubKey = peerView_->getPubKeyRef("own");
   auto outSesOldKey = outSess_->hkdfKeySet_;

   outSess_->sessionRekey(false,
      ownPubKey,
      chosenAuthPeerKey_.getRef(),
      BinaryDataRef{inSess_->hkdfKeySet_.data(), 64});
   inSess_->sessionRekey(false,
      chosenAuthPeerKey_.getRef(),
      ownPubKey,
      BinaryDataRef{outSesOldKey.data(), 64});

   curState_ = BIP150State::SUCCESS;
}

// Get own BIP150 public key
//
// IN : None
// OUT: None
// RET: BinaryDataRef to compressed secp256k1 public key
BinaryDataRef BIP150StateMachine::getOwnPubKey() const
{
   return peerView_->getPubKeyRef("own");
}

// Check if this peer name exists and that it matches this public key
//
// IN:  pubkey - the public key
//      name - the ip:port or domain name
// OUT: None
// RET: true if the key/name pair matches, otherwise false
bool BIP150StateMachine::havePublicKey(
   const BinaryDataRef& pubkey, const std::string& name) const
{
   try {
      auto peerKey = peerView_->getPubKeyRef(name);
      return peerKey == pubkey;
   } catch (const std::exception&) {
      return false;
   }
}
