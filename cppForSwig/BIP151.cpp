////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cassert>
#include <iomanip>

#include "hkdf.h"
#include "btc/ecc.h"
#include "btc/hash.h"
#include "btc/sha2.h"
#include "BtcUtils.h"
#include "log.h"
#include "BIP151.h"

// Because libbtc doesn't export its libsecp256k1 context, and we need one for
// direct access to libsecp256k1 calls, just create one.
static secp256k1_context* secp256k1_ecdh_ctx = nullptr;

// FIX/NOTE: Just use btc_ecc_start() from btc/ecc.h when starting up Armory.
// Need to initialize things, and not just for BIP 151 once libbtc is used more.

// Startup code for BIP 151. Used for initialization of underlying libraries.
// 
// IN:  None
// OUT: None
// RET: N/A
void startupBIP151CTX()
{
   if(secp256k1_ecdh_ctx == nullptr)
   {
      // SIGN used to generate public keys from private keys. (Can be removed
      // once libbtc exports compressed public keys.)
      // VERIFY used to allow for EC multiplication, which won't work otherwise.
      secp256k1_ecdh_ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | \
                                                    SECP256K1_CONTEXT_VERIFY);
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
   if(secp256k1_ecdh_ctx != nullptr)
   {
      secp256k1_context_destroy(secp256k1_ecdh_ctx);
   }
}

// Overridden constructor for a BIP 151 session. Sets the session direction.
// 
// IN:  sessOut - Indicates session direction.
// OUT: None
// RET: N/A
bip151Session::bip151Session(const bool& sessOut) : isOutgoing(sessOut)
{
   // Generate the ECDH key off the bat.
   btc_privkey_init(&genSymECDHPrivKey);
   btc_privkey_gen(&genSymECDHPrivKey);
}

// Overridden constructor for a BIP 151 session. Sets the session direction and
// sets the private key used in ECDH. USE WITH EXTREME CAUTION!!! Unless there's
// a very specific need for a pre-determined key (e.g., test harness or key is
// HW-generated), using this will just get you into trouble.
// IN:  inSymECDHPrivKey - ECDH private key.
//      sessOut - Indicates session direction.
// OUT: None
// RET: N/A
bip151Session::bip151Session(btc_key* inSymECDHPrivKey, const bool& sessOut) :
isOutgoing(sessOut)
{
   // libbtc assumes it'll generate the private key. If you want to set it, you
   // have to go into the private key struct.
   btc_privkey_init(&genSymECDHPrivKey);
   std::copy(inSymECDHPrivKey->privkey,
             inSymECDHPrivKey->privkey + 32,
             genSymECDHPrivKey.privkey);
}

// Function that generates the symmetric keys required by the BIP 151
// ciphersuite and performs any related setup.
// 
// IN:  peerPubKey  (The peer's public key - Assume the key is validated)
// OUT: N/A
// RET: -1 if not successful, 0 if successful.
int bip151Session::genSymKeys(const uint8_t* peerPubKey)
{
   int retVal = -1;
   btc_key sessionECDHKey;
   secp256k1_pubkey peerECDHPK;
   std::array<uint8_t, BIP151PUBKEYSIZE> parseECDHMulRes{};
   size_t parseECDHMulResSize = parseECDHMulRes.size();
   switch(cipherType)
   {
   case bip151SymCiphers::CHACHA20POLY1305_OPENSSH:
      // Confirm that the incoming pub key is valid and compressed.
      if(secp256k1_ec_pubkey_parse(secp256k1_ecdh_ctx, &peerECDHPK, peerPubKey,
                                   BIP151PUBKEYSIZE) != 1)
      {
         LOGERR << "BIP 151 - Peer public key for session " << getSessionIDHex()
            << " is invalid.";
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
      if(secp256k1_ec_pubkey_tweak_mul(secp256k1_ecdh_ctx, &peerECDHPK,
                                       genSymECDHPrivKey.privkey) != 1)
      {
         LOGERR << "BIP 151 - ECDH failed.";
         return -1;
      }

      secp256k1_ec_pubkey_serialize(secp256k1_ecdh_ctx, parseECDHMulRes.data(),
                                    &parseECDHMulResSize, &peerECDHPK,
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
const bool bip151Session::rekeyNeeded()
{
   bool retVal = false;

   // In theory, there's a race condition if both sides decide at the same time
   // to rekey. In practice, they'll arrive at the same keys eventually.
   // FIX - Add a timer policy. Not currently coded.
   if(bytesOnCurKeys > CHACHA20POLY1305MAXBYTESSENT /*|| Timer policy check here */)
   {
      retVal = true;
   }
   return retVal;
}   

// Public function used to kick off symmetric key setup. Any setup directly
// related to symmetric keys should be handled here.
// 
// IN:  peerPubKey  (The peer's public key - Needs to be validated)
// OUT: None
// RET: -1 if failure, 0 if success.
int bip151Session::symKeySetup(const uint8_t* peerPubKey,
                               const size_t& peerPubKeySize)
{
   int retVal = -1;

   switch(cipherType)
   {
   case bip151SymCiphers::CHACHA20POLY1305_OPENSSH:
      // Generate the keys only if the peer key is the correct size (and valid).
      if((peerPubKeySize != BIP151PUBKEYSIZE) || (genSymKeys(peerPubKey) != 0))
      {
         return retVal;
      }
      else
      {
         // We're done with the ECDH key now. Nuke it.
         // **Applies only to outbound sessions.**
         if(isOutgoing)
         {
            btc_privkey_cleanse(&genSymECDHPrivKey);
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
void bip151Session::calcChaCha20Poly1305Keys(const btc_key& sesECDHKey)
{
   BinaryData salt("bitcoinecdh");
   std::array<uint8_t, 33> ikm;
   std::copy(sesECDHKey.privkey, sesECDHKey.privkey + 32,
             ikm.data());
   ikm[32] = static_cast<uint8_t>(bip151SymCiphers::CHACHA20POLY1305_OPENSSH);
   BinaryData info1("BitcoinK1");
   BinaryData info2("BitcoinK2");

   // NB: The ChaCha20Poly1305 library reverses the expected key order.
   hkdf_sha256(hkdfKeySet.data(), 32, salt.getPtr(), salt.getSize(), ikm.data(),
               ikm.size(), info2.getPtr(), info2.getSize());
   hkdf_sha256(hkdfKeySet.data() + 32, 32, salt.getPtr(), salt.getSize(),
               ikm.data(), ikm.size(), info1.getPtr(), info1.getSize());
   chacha20poly1305_init(&sessionCTX, hkdfKeySet.data(), hkdfKeySet.size());
}

// A helper function that calculates the session ID. See the "Symmetric
// Encryption Cipher Keys" section of the BIP 151 spec.
// 
// IN:  sesECDHKey (The session's ECDH key - libbtc formatting)
// OUT: None
// RET: None
void bip151Session::calcSessionID(const btc_key& sesECDHKey)
{
   BinaryData salt("bitcoinecdh");
   std::array<uint8_t, BIP151PUBKEYSIZE> ikm;
   std::copy(sesECDHKey.privkey, sesECDHKey.privkey + 32,
             ikm.data());
   ikm[32] = static_cast<uint8_t>(cipherType);
   BinaryData info("BitcoinSessionID");

   hkdf_sha256(sessionID.data(), sessionID.size(), salt.getPtr(),
               salt.getSize(), ikm.data(), ikm.size(), info.getPtr(),
               info.getSize());
}

// A helper function that can be used when it's time to rekey a session. It
// should be called when the other side wishes for a rekey or when we hit a
// policy limit (e.g., time or bytes sent by us). Rekey checks should be
// performed elsewhere.
// 
// IN:  None
// OUT: None
// RET: N/A
void bip151Session::sessionRekey()
{
   switch(cipherType)
   {
   case bip151SymCiphers::CHACHA20POLY1305_OPENSSH:
      // Process both symmetric keys at the same time. Reset the # of bytes on
      // the session but *not* the sequence number.
      uint8_t* poly1305Key;
      uint8_t* chacha20Key;
      poly1305Key = &hkdfKeySet[0];
      chacha20Key = &hkdfKeySet[32];
      chacha20Poly1305Rekey(poly1305Key, 32);
      chacha20Poly1305Rekey(chacha20Key, 32);
      bytesOnCurKeys = 0;
      break;

   default:
      // You should never get here.
      break;
   }
}

// A function that checks to see if an incoming encack message is requesting a
// rekey. See the "Re-Keying" section of the BIP 151 spec.
// 
// IN:  inMsg - Pointer to a message to check for a rekey request.
//      inMsgSize - incoming message size. Must be 33 bytes.
// OUT: None
// RET: 0 if rekey, any other value if not rekey.
int bip151Session::inMsgIsRekey(const uint8_t* inMsg, const size_t& inMsgSize)
{
   int retVal = -1;
   if(inMsgSize == BIP151PUBKEYSIZE)
   {
     std::array<uint8_t, BIP151PUBKEYSIZE> rekeyMsg{};
     retVal = memcmp(inMsg, rekeyMsg.data(), BIP151PUBKEYSIZE);
   }
   return retVal;
}

// A helper function that encrypts a payload. The code expects the BIP 151
// encrypted messages structure, minus the MAC (Poly1305) tag. The encrypted
// payload *will* include the MAC tag.
//
// IN:  plainData - Plaintext data to encrypt.
//      plainSize - The size of the plaintext buffer. The size *must* be the
//                   exact length of the actual plaintext buffer.
//      cipherSize - The size of the ciphertext buffer. The size *must* be at
//                   least 16 bytes larger than the plaintext buffer, as the
//                   cipher will include the Poly1305 tag.
// OUT: cipherData - The encrypted plaintext data and the Poly1305 tag.
// RET: -1 if failure, 0 if success
int bip151Session::encPayload(uint8_t* cipherData,
                              const size_t cipherSize,
                              const uint8_t* plainData,
                              const size_t plainSize)
{
   int retVal = -1;
   assert(cipherSize >= (plainSize + POLY1305MACLEN));

   if(chacha20poly1305_crypt(&sessionCTX,
                             seqNum,
                             cipherData,
                             plainData,
                             plainSize - AUTHASSOCDATAFIELDLEN,
                             AUTHASSOCDATAFIELDLEN,
                             CHACHAPOLY1305_AEAD_ENC) == -1)
   {
      LOGERR << "Encryption at sequence number " << seqNum << " failed.";
   }
   else
   {
      retVal = 0;
   }

   ++seqNum;
   bytesOnCurKeys += plainSize;
   return retVal;
}

// A helper function that decrypts a payload. The code expects the BIP 151
// encrypted messages structure, with the MAC (Poly1305) tag. The decrypted
// payload *will not* include the MAC tag but the tag will be authenticated
// before decryption occurs.
//
// IN:  cipherData - The buffer (w/ MAC tag) to decrypt. Must be at least 16
//                   bytes larger than the resulting plaintext buffer.
//      cipherSize - The size of the ciphertext buffer. The size *must* be the
//                   exact length of the actual ciphertext.
//      plainSize - The size of the plaintext buffer. The size can be up to 16
//                  bytes smaller than the cipher. This is due to the results
//                  not including the 16 byte Poly1305 tag.
// OUT: plainData - The decrypted ciphertext data but no Poly1305 tag.
// RET: -1 if failure, 0 if success
int bip151Session::decPayload(const uint8_t* cipherData,
                              const size_t cipherSize,
                              uint8_t* plainData,
                              const size_t plainSize)
{
   int retVal = -1;
   uint32_t decryptedLen = 0;
   assert(cipherSize <= plainSize);

   chacha20poly1305_get_length(&sessionCTX,
                               &decryptedLen,
                               seqNum,
                               cipherData,
                               cipherSize);
   if(chacha20poly1305_crypt(&sessionCTX,
                             seqNum,
                             plainData,
                             cipherData,
                             decryptedLen,
                             AUTHASSOCDATAFIELDLEN,
                             CHACHAPOLY1305_AEAD_DEC) == -1)
   {
      LOGERR << "Decryption at sequence number " << seqNum << " failed.";
   }
   else
   {
      retVal = 0;
   }

   ++seqNum;
   bytesOnCurKeys += plainSize;
   return retVal;
}

// Internal function that actually does the ChaCha20Poly1305 rekeying.
// 
// IN:  keySize - The size of the key to be updated.
// OUT: keyToUpdate - The updated key (ChaCha20 or Poly1305).
// RET: None
void bip151Session::chacha20Poly1305Rekey(uint8_t* keyToUpdate,
                                          const size_t& keySize)
{
   // Generate, via 2xSHA256, a new symmetric key.
   std::array<uint8_t, 64> hashData;
   std::copy(std::begin(sessionID), std::end(sessionID), &hashData[0]);
   std::copy(keyToUpdate, keyToUpdate + keySize, &hashData[32]);
   uint256 hashOut;
   btc_hash(hashData.data(), hashData.size(), hashOut);
   std::copy(std::begin(hashOut), std::end(hashOut), keyToUpdate);
}

// A helper function that confirms whether or not we have a valid ciphersuite,
// and sets an internal variable.
// 
// IN:  inCipher - The incoming cipher type.
// OUT: None
// RET: -1 if failure, 0 if success
int bip151Session::setCipherType(const bip151SymCiphers& inCipher)
{
   int retVal = -1;
   if(isCipherValid(inCipher))
   {
      cipherType = inCipher;
      retVal = 0;
   }
   else
   {
      LOGERR << "BIP 151 - Invalid ciphersuite type ("
         << static_cast<int>(cipherType) << ")";
   }
   return retVal;
}

// A helper function that confirms whether or not we have a valid ciphersuite,
// and sets an internal variable.
// 
// IN:  inCipher - The incoming cipher type.
// OUT: None
// RET: True if valid, false if not valid.
bool bip151Session::isCipherValid(const bip151SymCiphers& inCipher)
{
   // For now, this is simple. Just check for ChaChaPoly1305.
   bool retVal = false;
   if(inCipher == bip151SymCiphers::CHACHA20POLY1305_OPENSSH)
   {
      retVal = true;
   }
   return retVal;
}

// A helper function that returns the public key used to generate the ECDH key
// that will eventually generate the symmetric BIP 151 key set.
// 
// IN:  None
// OUT: tempECDHPubKey - A compressed public key to be used in ECDH.
// RET: None
void bip151Session::gettempECDHPubKey(btc_pubkey* tempECDHPubKey)
{
   btc_pubkey_from_key(&genSymECDHPrivKey, tempECDHPubKey);
}

// Function that gets the data sent alongside an encinit message. This can be
// used to get data for encrypted and unencrypted encinit messages.
//
// IN:  initBufferSize - The size of the output buffer.
//      cipherType - The cipher type to send.
// OUT: initBuffer - The buffer with the encinit data.
// RET: -1 if failure, 0 if success
int bip151Session::getEncinitData(uint8_t* initBuffer,
                                  const size_t& initBufferSize,
                                  const bip151SymCiphers& cipherType)
{
   int retVal = -1;
   if(setCipherType(cipherType) != 0 || initBufferSize > ENCINITMSGSIZE)
   {
      return retVal;
   }

   // Ideally, libbtc would be used here. Unfortunately, it doesn't output
   // compressed public keys (although it's aware of them). Go straight to
   // libsecp256k1 until this is fixed upstream.
   secp256k1_pubkey ourPubKey;
   size_t copyLen = BIP151PUBKEYSIZE;
   std::array<uint8_t, BIP151PUBKEYSIZE> ourCompPubKey{};
   if(!secp256k1_ec_pubkey_create(secp256k1_ecdh_ctx,
                                 &ourPubKey,
                                 genSymECDHPrivKey.privkey))
   {
      LOGERR << "BIP 151 - Invalid public key creation. Closing connection.";
      return retVal;
   }
   secp256k1_ec_pubkey_serialize(secp256k1_ecdh_ctx,
                                 initBuffer,
                                 &copyLen,
                                 &ourPubKey,
                                 SECP256K1_EC_COMPRESSED);
   initBuffer[33] = static_cast<uint8_t>(cipherType);


   retVal = 0;
   return retVal;
}

// Function that gets the data sent alongside an encack message. This can be
// used to get data for encrypted and unencrypted encack messages.
//
// IN:  ackBufferSize - The size of the output buffer.
// OUT: ackBuffer - The buffer with the encinit data.
// RET: -1 if failure, 0 if success
int bip151Session::getEncackData(uint8_t* ackBuffer,
                                 const size_t& ackBufferSize)
{
   int retVal = -1;

   if(setCipherType(cipherType) != 0 || ackBufferSize > BIP151PUBKEYSIZE)
   {
      return retVal;
   }

   // Ideally, libbtc would be used here. Unfortunately, it doesn't output
   // compressed public keys (although it's aware of them). Go straight to
   // libsecp256k1 until this is fixed upstream.
   secp256k1_pubkey ourPubKey;
   size_t copyLen = BIP151PUBKEYSIZE;
   std::array<uint8_t, BIP151PUBKEYSIZE> ourCompPubKey{};
   if(!secp256k1_ec_pubkey_create(secp256k1_ecdh_ctx,
                                 &ourPubKey,
                                 genSymECDHPrivKey.privkey))
   {
      LOGERR << "BIP 151 - Invalid public key creation. Closing connection.";
      return retVal;
   }
   secp256k1_ec_pubkey_serialize(secp256k1_ecdh_ctx,
                                 ackBuffer,
                                 &copyLen,
                                 &ourPubKey,
                                 SECP256K1_EC_COMPRESSED);

   // We're done with the ECDH key now. Nuke it. **Applies only to inbound sessions.**
   btc_privkey_cleanse(&genSymECDHPrivKey);
   retVal = 0;
   return retVal;
}

// A helper function that returns a hex string of the session ID.
// 
// IN:  None
// OUT: None
// RET: A const string with the session ID hex string.
const std::string bip151Session::getSessionIDHex() const
{
    BinaryData outID(getSessionID(), 32);
    return outID.toHexStr();
}

// Default BIP 151 connection constructor.
// 
// IN:  None
// OUT: None
// RET: N/A
bip151Connection::bip151Connection() : inSes(false), outSes(true)
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
bip151Connection::bip151Connection(btc_key* inSymECDHPrivKeyIn,
                                   btc_key* inSymECDHPrivKeyOut) :
                                   inSes(inSymECDHPrivKeyIn, false),
                                   outSes(inSymECDHPrivKeyOut, true)
{
   // The context must be set up before we can establish BIP 151 connections.
   assert(secp256k1_ecdh_ctx != nullptr);
}

// The function that handing incoming "encinit" messages.
// 
// IN:  inMsg - Buffer with the encinit msg contents. nullptr if we're sending.
//      inMsgSize - Size of the incomnig message.
//      outDir - Boolean indicating if the message is outgoing or incoming.
// OUT: None
// RET: -1 if unsuccessful, 0 if successful.
int bip151Connection::processEncinit(const uint8_t* inMsg,
                                     const size_t& inMsgSize,
                                     const bool outDir)
{
   
   int retVal = -1;
   if(inMsgSize != ENCINITMSGSIZE)
   {
      LOGERR << "BIP 151 - encinit message size isn't " << ENCINITMSGSIZE
         << " bytes. Will shut down connection.";
      return retVal;
   }

   // The BIP 151 spec states that traffic is handled via two unidirectional
   // sessions. We should only get an encinit on the incoming session.
   if(!outDir)
   {
      if(inSes.encinitSeen())
      {
         LOGERR << "BIP 151 - Have already seen encinit (session ID "
            << inSes.getSessionIDHex() << ") - Closing the connection.";
         return retVal;
      }

      // Set keys and ciphersuite type as needed. For now, assume that if we're
      // kicking things off, we're using ChaCha20Poly1305.
      std::array<uint8_t, BIP151PUBKEYSIZE> inECDHPubKey;
      std::copy(inMsg, inMsg + BIP151PUBKEYSIZE, inECDHPubKey.data());

      // Set up the session's symmetric keys and cipher type. If the functs fail,
      // they'll write log msgs.
      if(inSes.setCipherType(static_cast<bip151SymCiphers>(inMsg[33])) == 0 &&
         inSes.symKeySetup(inECDHPubKey.data(), inECDHPubKey.size()) == 0)
      {
         // We've successfully handled the packet.
         inSes.setEncinitSeen();
         retVal = 0;
      }
   }
   else
   {
      LOGERR << "BIP 151 - Received an encinit message on outgoing session "
         << outSes.getSessionIDHex() << ". This should not happen. Closing the "
         << "connection.";
   }

   return retVal;
}

// The function that handing incoming and outgoing "encack" messages.
// 
// IN:  inMsg - Buffer with the encack msg contents.
//      outgoing - Boolean indicating if the message is outgoing or incoming.
// OUT: None
// RET: -1 if unsuccessful, 0 if successful.
int bip151Connection::processEncack(const uint8_t* inMsg,
                                    const size_t& inMsgSize,
                                    const bool outDir)
{
   int retVal = -1;
   if(inMsgSize != BIP151PUBKEYSIZE)
   {
      LOGERR << "BIP 151 - encack message size isn't " << BIP151PUBKEYSIZE
         << " bytes. Will shut down connection.";
      return retVal;
   }

   // The BIP 151 spec states that traffic is handled via two unidirectional
   // sessions. We should only get an encack on the outgoing session.
   if(outDir)
   {
      // Valid only if we've already seen an encinit.
      if(!outSes.encinitSeen())
      {
         LOGERR << "BIP 151 - Received an encack message before an encinit ("
            << "outgoing session ID " << outSes.getSessionIDHex() << "). "
            << "Closing connection.";
         return retVal;
      }

      // We should never receive a rekey, just an initial keying.
      if(outSes.inMsgIsRekey(inMsg, inMsgSize) == 0)
      {
         LOGERR << "BIP 151 - Received a rekey message on outgoing session ID "
            << outSes.getSessionIDHex() << "). Closing connection.";
         return retVal;
      }

      if(outSes.symKeySetup(inMsg, inMsgSize) == 0)
      {
         outSes.setEncackSeen();
         retVal = 0;
      }
   }
   else
   {
      // Incoming sessions should only see rekeys.
      if(inSes.inMsgIsRekey(inMsg, inMsgSize) != 0)
      {
         LOGERR << "BIP 151 - Received a non-rekey encack message on incoming "
            << "session ID " << inSes.getSessionIDHex() << ". This should not "
            << "happen. Closing the connection.";
      }
      else
      {
         inSes.sessionRekey();
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
//      plainSize - Plaintext buffer size.
//      cipherSize - Ciphertext buffer size.
// OUT: cipherData - The encrypted buffer. Must be 16 bytes larger than the
//                   plaintext buffer.
// RET: -1 if failure, 0 if success.
int bip151Connection::assemblePacket(const uint8_t* plainData,
                                     const size_t& plainSize,
                                     uint8_t* cipherData,
                                     const size_t& cipherSize)
{
   int retVal = -1;

   if(outSes.encPayload(cipherData, cipherSize, plainData, plainSize) != 0)
   {
      LOGERR << "BIP 151 - Session ID " << outSes.getSessionIDHex()
         << " encryption failed (seq num " << outSes.getSeqNum() - 1 << ").";
      return retVal;
   }

   retVal = 0;
   return retVal;
}

// Function used to decrypt a packet.
//
// IN:  cipherData - Encrypted buffer that will be decrypted.
//      cipherSize - Encrypted buffer size.
//      plainSize - Decrypted buffer size.
// OUT: plainData - The decrypted packet. Must be no more than 16 bytes smaller
//                  than the plaintext buffer.
// RET: -1 if failure, 0 if success.
int bip151Connection::decryptPacket(const uint8_t* cipherData,
                                    const size_t& cipherSize,
                                    uint8_t* plainData,
                                    const size_t& plainSize)
{
   int retVal = -1;

   if(inSes.decPayload(cipherData, cipherSize, plainData, plainSize) != 0)
   {
      LOGERR << "BIP 151 - Session ID " << inSes.getSessionIDHex()
         << " decryption failed (seq num " << inSes.getSeqNum() - 1 << ").";
      return retVal;
   }

   retVal = 0;
   return retVal;
}

// Function that gets encinit data from the outbound session. Assume the session
// will do incoming data validation.
//
// IN:  encinitBufSize - encinit data buffer size.
//      cipherType - The cipher type to get.
// OUT: encinitBuf - The data to go into an encinit messsage.
// RET: -1 if not successful, 0 if successful.
const int bip151Connection::getEncinitData(uint8_t* encinitBuf,
                                           const size_t& encinitBufSize,
                                           const bip151SymCiphers& cipherType)
{
   outSes.setEncinitSeen();
   return outSes.getEncinitData(encinitBuf, encinitBufSize, cipherType);
}

// Function that gets encack data from the inbound session. Assume the session
// will do incoming data validation.
//
// IN:  encackBufSize - encack data buffer size. Must be >=33 bytes.
// OUT: encackBuf - The data to go into an encack messsage.
// RET: -1 if not successful, 0 if successful.
const int bip151Connection::getEncackData(uint8_t* encackBuf,
                                          const size_t& encackBufSize)
{
   inSes.setEncackSeen();
   int retVal = inSes.getEncackData(encackBuf, encackBufSize);

   return retVal;
}

// Get a rekey message. Will be in the BIP 151 "encrypted message" format.
//
// IN:  encackSize - The size of the buffer with the encack rekey message. Must
//                   be >=48 bytes.
// OUT: encackMsg - The data to go into the encack rekey messsage.
// RET: None
void bip151Connection::getRekeyBuf(uint8_t* encackBuf,
                                   const size_t& encackSize)
{
   BinaryData cmd("encack");
   std::array<uint8_t, BIP151PUBKEYSIZE> payload{};
   size_t finalMsgSize = 0;
   bip151Message encackMsg(cmd.getPtr(), cmd.getSize(),
                         payload.data(), payload.size());
   encackMsg.getEncStructMsg(encackBuf, encackSize, finalMsgSize);
}

// The function that kicks off a rekey for a connection's outbound session.
//
// IN:  encackSize - The size of the buffer with the encack rekey message. Must
//                   be >=48 bytes.
// OUT: encackMsg - The data to go into the encack rekey messsage.
// RET: None
void bip151Connection::rekeyConn(uint8_t* encackBuf,
                                 const size_t& encackSize)
{
   getRekeyBuf(encackBuf, encackSize);
   outSes.sessionRekey();
}

// Function that returns the connection's input or output session ID.
// 
// IN:  dirIsOut - Bool indicating if the direction is outbound.
// OUT: None
// RET: A pointer to a 32 byte array with the session ID.
const uint8_t* bip151Connection::getSessionID(const bool& dirIsOut)
{
   bip151Session* sesToUse;
   if(dirIsOut)
   {
      sesToUse = &outSes;
   }
   else
   {
      sesToUse = &inSes;
   }
   return sesToUse->getSessionID();
}


// Default BIP 151 "payload" constructor.
//
// IN:  None
// OUT: None
// RET: None
bip151Message::bip151Message() {}

// Overloaded BIP 151 message constructor. Sets up the contents based on a
// plaintext message in the BIP 151 "encrypted structure" format.
//
// IN:  inCmd - The command.
//      inCmdSize - The command size.
//      inPayload - The payload.
//      inPayloadSize - The payload size.
// OUT: None
// RET: None
bip151Message::bip151Message(uint8_t* plaintextData,
                             uint32_t plaintextDataSize)
{
   setEncStruct(plaintextData, plaintextDataSize);
}

// Overloaded BIP 151 message constructor. Sets up the contents based on a
// plaintext command and a binary payload.
//
// IN:  inCmd - The command.
//      inCmdSize - The command size.
//      inPayload - The payload.
//      inPayloadSize - The payload size.
// OUT: None
// RET: None
bip151Message::bip151Message(const uint8_t* inCmd,
                             const size_t& inCmdSize,
                             const uint8_t* inPayload,
                             const size_t& inPayloadSize)
{
   setEncStructData(inCmd, inCmdSize, inPayload, inPayloadSize);
}

// A function that sets up the plaintext contents via the individual command and
// payload pieces.
//
// IN:  inCmd - The command.
//      inCmdSize - The command size.
//      inPayload - The payload.
//      inPayloadSize - The payload size.
// OUT: None
// RET: None
void bip151Message::setEncStructData(const uint8_t* inCmd,
                                     const size_t& inCmdSize,
                                     const uint8_t* inPayload,
                                     const size_t& inPayloadSize)
{
   cmd.copyFrom(inCmd, inCmdSize);
   payload.copyFrom(inPayload, inPayloadSize);
}

// A function that sets up the plaintext contents for an encrypted BIP 151
// message. Use with a successfully decrypted payload.
//
// IN:  plaintextData - The payload from a decrypted message.
//      plaintextDataSize - The size of the decrypted message payload.
// OUT: None
// RET: -1 if failure, 0 if success
int bip151Message::setEncStruct(uint8_t* plaintextData,
                                uint32_t& plaintextDataSize)
{
   int retVal = -1;
   BinaryReader inData(plaintextData, plaintextDataSize);

   // Do some basic sanity checking before proceeding.
   uint32_t msgSize = inData.get_uint32_t();
   if(msgSize != inData.getSizeRemaining())
   {
      LOGERR << "BIP 151 - Incoming message size (" << msgSize << ") does not "
         << "match the data buffer size (" << inData.getSizeRemaining() << ").";
      return retVal;
   }

   // uint64_t -> uint32_t is safe in this case. The spec disallows >4GB msgs.
   uint8_t cmdSize = inData.get_uint8_t();
   inData.get_BinaryData(cmd, static_cast<uint32_t>(cmdSize));
   uint64_t payloadSize = inData.get_var_int();
   inData.get_BinaryData(payload, static_cast<uint32_t>(payloadSize));

   retVal = 0;
   return retVal;
}

// A function that gets an "encrypted structure" BIP 151 plaintext message.
//
// IN:  None
// OUT: outStruct - The struct for a to-be-encrypted BIP 151 message.
//      outStructSize - The size of the incoming struct.
//      finalStructSize - The final size of the written struct.
// RET: None
void bip151Message::getEncStructMsg(uint8_t* outStruct,
                                    const size_t& outStructSize,
                                    size_t& finalStructSize)
{
   assert(outStructSize >= messageSizeHint());

   size_t writerSize = messageSizeHint() - 4;
   BinaryWriter payloadWriter(writerSize);
   payloadWriter.put_var_int(cmd.getSize());
   payloadWriter.put_BinaryData(cmd);
   payloadWriter.put_uint32_t(payload.getSize());
   payloadWriter.put_BinaryData(payload);

   // Write a second, final buffer.
   finalStructSize = payloadWriter.getSize() + 4;
   BinaryWriter finalStruct(finalStructSize);
   finalStruct.put_uint32_t(payloadWriter.getSize());
   finalStruct.put_BinaryData(payloadWriter.getData());

   std::copy(finalStruct.getData().getPtr(),
             finalStruct.getData().getPtr() + finalStructSize,
             outStruct);
}

// A function that gets the command from a BIP 151 message structure.
//
// IN:  encackSize - The size of the buffer with the encack rekey message. Must
//                   be >=48 bytes.
// OUT: encackMsg - The data to go into the encack rekey messsage.
// RET: None
void bip151Message::getCmd(uint8_t* cmdBuf,
                           const size_t& cmdBufSize)
{
   assert(cmd.getSize() <= cmdBufSize);
   std::copy(cmd.getPtr(), cmd.getPtr() + cmd.getSize(), cmdBuf);
}

// A function that gets the payload from a BIP 151 message structure.
//
// IN:  encackSize - The size of the buffer with the encack rekey message. Must
//                   be >=48 bytes.
// OUT: encackMsg - The data to go into the encack rekey messsage.
// RET: None
void bip151Message::getPayload(uint8_t* payloadBuf,
                               const size_t& payloadBufSize)
{
   assert(payload.getSize() <= payloadBufSize);
   std::copy(payload.getPtr(), payload.getPtr() + payload.getSize(),
             payloadBuf);
}

// A function that can be used to determine the final struct output size. This
// will be the same size as the encrypted messages structure from the BIP 151
// spec, minus the MAC (Poly1305) tag (16 bytes).
//
// IN:  None
// OUT: None
// RET: The maximum possible size for the struct.
const size_t bip151Message::messageSizeHint()
{
   // Hint: Operand order is the same order as what's found in the struct.
   return 4 + BtcUtils::calcVarIntSize(cmd.getSize()) + cmd.getSize() + 4 + \
          payload.getSize();
}
