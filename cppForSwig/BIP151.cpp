////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cassert>
#include <iomanip>

extern "C" {
#include "secp256k1_ecdh.h"
#include "btc/ecc.h"
#include "btc/hash.h"
#include "btc/sha2.h"
#include "ccan/crypto/hkdf_sha256/hkdf_sha256.h"
}
#include "log.h"
#include "BIP151.h"

// Because libbtc doesn't export its libsecp256k1 context, and we need one for
// direct access to the ECDH code, just create one w/o signing or verification.
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
      secp256k1_ecdh_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
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

// Default constructor for a BIP 151 session.
// 
// IN:  None
// OUT: None
// RET: N/A
bip151Session::bip151Session()
{
   // Generate the ECDH key off the bat.
   btc_privkey_init(&genSymECDHPrivKey);
   btc_privkey_gen(&genSymECDHPrivKey);
   
}

// Overridden constructor for a BIP 151 session. Sets whether or not the session
// is outgoing.
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

// Function that generates the symmetric keys required by the BIP 151
// ciphersuite and performs any related setup.
// 
// IN:  peerPubKey  (The peer's public key - Assume the key is validated)
// OUT: N/A
// RET: -1 if not successful, 0 if successful.
int bip151Session::genSymKeys(const uint8_t* peerPubKey)
{
   int retVal = -1;
   switch(cipherType)
   {
   case bip151SymCiphers::CHACHA20POLY1305:
      // Confirm that the incoming pub key is valid and compressed.
      secp256k1_pubkey peerECDHPK;
      if(secp256k1_ec_pubkey_parse(secp256k1_ecdh_ctx, &peerECDHPK, peerPubKey,
                                   SECP256K1_EC_COMPRESSED) != 1)
      {
         LOGERR << "BIP 151 - Peer public key for session " << getSessionIDHex()
            << " is invalid.";
         return retVal;
      }

      // libbtc doesn't care about ECDH. We have to go straight to libsecp256k1.
      // Generate the session's ECDH key and delete our private key source.
      btc_key sessionECDHKey;
      if(secp256k1_ecdh(secp256k1_ecdh_ctx, &sessionECDHKey.privkey[0],
                        &peerECDHPK, &genSymECDHPrivKey.privkey[0]) != 1)
      {
         LOGERR << "BIP 151 - ECDH key generation failed.";
         return -1;
      }
      btc_privkey_cleanse(&genSymECDHPrivKey);

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
bool bip151Session::rekeyNeeded()
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
// RET: N/A
int bip151Session::symKeySetup(const uint8_t* peerPubKey,
                               const size_t& peerPubKeySize)
{
   int retVal = -1;
   switch(cipherType)
   {
   case bip151SymCiphers::CHACHA20POLY1305:
      // Generate the keys only if the peer key is the correct size (and valid)..
      if((peerPubKeySize != 33) || (genSymKeys(peerPubKey) != 0))
      {
         return retVal;
      }
      else
      {
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
   array<unsigned char, 11> salt = {'b','i','t','c','o','i','n','e','c','d','h'};
   array<unsigned char, 33> ikm;
   copy(begin(sesECDHKey.privkey), end(sesECDHKey.privkey), begin(ikm));
   ikm[32] = static_cast<uint8_t>(bip151SymCiphers::CHACHA20POLY1305);
   array<unsigned char, 9> info1 = {'B','i','t','c','o','i','n','K','1'};
   array<unsigned char, 9> info2 = {'B','i','t','c','o','i','n','K','2'};

   // NB: The ChaCha20Poly1305 library reverses the expected key order.
   hkdf_sha256(&hkdfKeySet[0], 32, &salt[0], salt.size(), &ikm[0],
               ikm.size(), &info2[0], info2.size());
   hkdf_sha256(&hkdfKeySet[32], 32, &salt[0], salt.size(), &ikm[0],
               ikm.size(), &info1[0], info1.size());
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
   array<uint8_t, 11> salt = {'b','i','t','c','o','i','n','e','c','d','h'};
   array<uint8_t, 33> ikm;
   copy(begin(sesECDHKey.privkey), end(sesECDHKey.privkey), ikm.data());
   ikm[32] = static_cast<uint8_t>(cipherType);
   array<uint8_t, 16> info = {'B','i','t','c','o','i','n','S','e','s','s','i','o','n','I','D'};

   hkdf_sha256(sessionID.data(), sessionID.size(), salt.data(), salt.size(),
               ikm.data(), ikm.size(), info.data(), info.size());
}

// A helper function that returns a hex string of the session ID.
// 
// IN:  None
// OUT: None
// RET: A const string with the session ID hex string.
const string bip151Session::getSessionIDHex()
{
    ostringstream sid;
    sid << hex << setfill('0');
    for(uint8_t curChar : sessionID)
    {
        sid << setw(2) << curChar;
    };
    return sid.str();
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
   case bip151SymCiphers::CHACHA20POLY1305:
      // Process both symmetric keys at the same time.
      uint8_t* poly1305Key;
      uint8_t* chacha20Key;
      poly1305Key = &hkdfKeySet[0];
      chacha20Key = &hkdfKeySet[32];
      chacha20Poly1305Rekey(poly1305Key, 32);
      chacha20Poly1305Rekey(chacha20Key, 32);
      bytesOnCurKeys = 0;
      break;

      // FIX - Send rekey packet.

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
// RET: 0 if valid, a negative value (probably -1) if not valid.
int bip151Session::inMsgIsRekey(const uint8_t* inMsg, const size_t& inMsgSize)
{
   int retVal = -1;
   if(inMsgSize == 33)
   {
     array<uint8_t, 33> rekeyMsg{};
     retVal = memcmp(inMsg, rekeyMsg.data(), 33);
   }
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
   array<uint8_t, 64> hashData;
   copy(begin(sessionID), end(sessionID), &hashData[0]);
   copy(keyToUpdate, keyToUpdate + keySize, &hashData[32]);
   uint256 hashOut;
   btc_hash(hashData.data(), hashData.size(), hashOut);
   copy(begin(hashOut), end(hashOut), keyToUpdate);
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
      LOGERR << "BIP 151 - Invalid ciphersuite (" << static_cast<int>(cipherType) << ")";
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
   if(inCipher == bip151SymCiphers::CHACHA20POLY1305)
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

// Send an encinit msg.
// 
// IN:  cipherType - The cipher type to send.
// OUT: None
// RET: -1 if failure, 0 if success
int bip151Session::sendEncinit(const bip151SymCiphers& cipherType)
{
   int retVal = -1;
   if(setCipherType(cipherType) != 0)
   {
      return retVal;
   }

   btc_pubkey ourPubKey; // Get compressed pub key from our priv key.
   btc_pubkey_from_key(&genSymECDHPrivKey, &ourPubKey);
   array<uint8_t, 34> encInitData{};
   copy(begin(ourPubKey.pubkey), end(ourPubKey.pubkey), &encInitData[0]);
   encInitData[33] = static_cast<uint8_t>(cipherType);


   // FIX - SEND PACKET HERE

   encinit = true;
   return retVal;
}

// Send an encinit msg.
// 
// IN:  None
// OUT: None
// RET: -1 if failure, 0 if success
int bip151Session::sendEncack(const bip151SymCiphers& cipherType)
{
   int retVal = -1;
   if(setCipherType(cipherType) != 0)
   {
      return retVal;
   }

   btc_pubkey ourPubKey; // Get compressed pub key from our priv key.
   btc_pubkey_from_key(&genSymECDHPrivKey, &ourPubKey);
   array<uint8_t, 33> encInitData{};
   copy(begin(ourPubKey.pubkey), end(ourPubKey.pubkey), &encInitData[0]);

   // FIX - SEND PACKET HERE

   encack = true;
   return retVal;
}

// Default BIP 151 connection constructor.
// 
// IN:  None
// OUT: None
// RET: N/A
bip151Connection::bip151Connection()
{
   // The context must be set up before we can establish BIP 151 connections.
   assert(secp256k1_ecdh_ctx != nullptr);
}

// The function that handing incoming and outgoing "encinit" messages.
// 
// IN:  inMsg - Buffer with the encinit msg contents. nullptr if we're sending.
//      outgoing - Boolean indicating if the message is outgoing or incoming.
// OUT: None
// RET: -1 if unsuccessful, 0 if successful.
int bip151Connection::handleEncinit(const uint8_t* inMsg,
                                    const size_t& inMsgSize,
                                    const bip151SymCiphers& cipherType,
                                    const bool outDir)
{
   
   int retVal = -1;
   if(inMsgSize != 34)
   {
      LOGERR << "BIP 151 - encinit message size isn't 34 bytes. Will shut down "
         << "connection.";
      return retVal;
   }

   // If we've already seen an encinit msg, bail out.
   bip151Session* sesToHandle;
   if(outDir)
   {
      sesToHandle = &outSes;
   }
   else
   {
      sesToHandle = &inSes;
   }
   if(sesToHandle->getEncinit())
   {
      LOGERR << "BIP 151 - Have already seen encinit (session ID "
         << sesToHandle->getSessionIDHex() << ") - Will shut down connection.";
      return retVal;
   }

   // Set keys and ciphersuite type as needed. For now, assume that if we're
   // kicking things off, we're using ChaCha20Poly1305. Also, if we're getting an
   // encinit, reply with an encack packet.
   if(outDir)
   {
      outSes.sendEncinit(bip151SymCiphers::CHACHA20POLY1305);
   }
   else
   {
      array<uint8_t, 33> inECDHPubKey;
      copy(inMsg, inMsg + 33, inECDHPubKey.data());
      if(inSes.symKeySetup(inECDHPubKey.data(), inECDHPubKey.size()) != 0 ||
         inSes.isCipherValid(static_cast<bip151SymCiphers>(inMsg[33])) != 0)
      {
         return retVal;
      }
      else
      {
         inSes.sendEncack(static_cast<bip151SymCiphers>(inMsg[33]));
      }
   }

   // We've successfully handled the packet.
   retVal = 0;

   return retVal;
}

// The function that handing incoming and outgoing "encack" messages.
// 
// IN:  inMsg - Buffer with the encack msg contents.
//      outgoing - Boolean indicating if the message is outgoing or incoming.
// OUT: None
// RET: -1 if unsuccessful, 0 if successful.
int bip151Connection::handleEncack(const uint8_t* inMsg,
                                   const size_t& inMsgSize,
                                   const bool outDir)
{
   int retVal = -1;
   if(inMsgSize != 33)
   {
      LOGERR << "BIP 151 - encack message size isn't 33 bytes. Will shut down "
         << "connection.";
      return retVal;
   }

   bip151Session* sesToHandle;
   if(outDir)
   {
      sesToHandle = &outSes;
   }
   else
   {
      sesToHandle = &inSes;
   }
   // Valid only if we've already seen an encinit.
   if(!sesToHandle->getEncinit())
   {
      LOGERR << "BIP 151 - Received an encack message before an encinit ("
         << "session ID " << sesToHandle->getSessionIDHex() << "). Closing "
         << "connection";
      return retVal;
   }

   // If outgoing, get their pub key and generate the keys. Per the spec,
   // assume it's the same ciphersuite.
   if(outDir)
   {
      if(outSes.symKeySetup(inMsg, inMsgSize) != 0)
      {
         return retVal;
      }
      outSes.setEncack();
      retVal = 0;
   }
   // If incoming, we should see one if it's a rekey, otherwise it's an error.
   else
   {
      if(inSes.inMsgIsRekey(inMsg, inMsgSize))
      {
         inSes.sessionRekey();
         retVal = 0;
      }
      else
      {
         LOGERR << "BIP 151 - Received an encack message after establishing "
            << "session " << inSes.getSessionIDHex() << " that is not a "
            << "rekey. Closing the connection";
      }
   }

   return retVal;
}

// ENCRYPTED PACKET OUTLINE, PER BIP 151:
// - Encrypted size of payload  (4 bytes)
// - Encrypted payload
// --- Command  (VarStr - Variable bytes)
// --- Length of command payload  (4 bytes)
// --- Payload  (Variable bytes)
// - MAC for the encrypted payload  (16 bytes)
// - Whether or not encryption is successful, increment the seq ctr & # of bytes.
// - Check to see if a rekey is necessary. If so, make it happen.

// Function used to assemble an encrypted packet.
// 
// IN:  packetData - Plaintext data that will be encrypted.
//      outDir - Indicates if the session is outgoing or incoming.
// OUT: None
// RET: -1 if failure, 0 if success.
int bip151Connection::assemblePacket(const uint8_t* packetData,
                                     const size_t& packetDataSize,
                                     const bool& outDir)
{
   int retVal = -1;
   array<uint8_t, 2000> cipherData; // FIX - Adjust buffer size?

   bip151Session* sesToUse;
   if(outDir)
   {
      sesToUse = &outSes;
   }
   else
   {
      sesToUse = &inSes;
   }

   if(encPayload(cipherData.data(), cipherData.size(), packetData,
                 packetDataSize, sesToUse->getSeqNum(),
                 sesToUse->getSessionCtxPtr()) == -1)
   {
      LOGERR << "BIP 151 - Session ID " << sesToUse->getSessionIDHex()
         << " failed to encrypt packet for seq num " << sesToUse->getSeqNum();
      return retVal;
   }

   // FIX - SEND PACKET

   retVal = 0;
   sesToUse->incSeqNum();
   sesToUse->addBytes(packetDataSize);
   if(sesToUse->rekeyNeeded())
   {
      sesToUse->sessionRekey();
   }

   return retVal;
}

// Function used to decrypt a packet.
// 
// IN:  packetData - Ciphertext data that will be decrypted.
//      outDir - Indicates if the session is outgoing or incoming.
// OUT: None
// RET: -1 if failure, 0 if success.
int bip151Connection::decryptPacket(const uint8_t* packetData,
                                    const size_t& packetDataSize,
                                    const bool& outDir)
{
   int retVal = -1;
   array<uint8_t, 2000> plainData; // FIX - Adjust buffer size?
   uint32_t decryptedLen = 0;

   bip151Session* sesToUse;
   if(outDir)
   {
      sesToUse = &outSes;
   }
   else
   {
      sesToUse = &inSes;
   }

   chacha20poly1305_get_length(sesToUse->getSessionCtxPtr(), &decryptedLen,
                               sesToUse->getSeqNum(), packetData,
                               packetDataSize);
   if(decPayload(packetData, packetDataSize, plainData.data(), plainData.size(),
                 sesToUse->getSeqNum(), sesToUse->getSessionCtxPtr()) == -1)
   {
      LOGERR << "BIP 151 - Session ID " << sesToUse->getSessionIDHex()
         << " failed to decrypt packet for seq num " << sesToUse->getSeqNum();
      return retVal;
   }

   // FIX - SEND PACKET

   retVal = 0;
   sesToUse->incSeqNum();
   sesToUse->addBytes(decryptedLen);
   if(sesToUse->rekeyNeeded())
   {
      sesToUse->sessionRekey();
   }

   return retVal;
}

// A helper function that encrypts a payload.
// 
// IN:  plainData - Plaintext data to encrypt.
// OUT: cipherData - The encrypted plaintext data.
// RET: -1 if failure, 0 if success
int bip151Connection::encPayload(uint8_t* cipherData,
                                 const size_t& cipherSize,
                                 const uint8_t* plainData,
                                 const size_t plainSize,
                                 const uint32_t& curSeqNum,
                                 chachapolyaead_ctx* sesCtx)
{
   int retVal = -1;
//   assert(plainSize <= (cipherSize - POLY1305MACLEN));

   if(chacha20poly1305_crypt(sesCtx, curSeqNum, cipherData, plainData,
                             plainSize, AUTHASSOCDATAFIELDLEN,
                             CHACHAPOLY1305_AEAD_ENC) == 0)
   {
      retVal = 0;
   }

   return retVal;
}

// A helper function that decrypts a payload.
//
// IN:  cipherData - Ciphertext data to encrypt.
// OUT: plainData - The decrypted ciphertext data.
// RET: -1 if failure, 0 if success
int bip151Connection::decPayload(const uint8_t* cipherData,
                                 const size_t& cipherSize,
                                 uint8_t* plainData,
                                 const size_t plainSize,
                                 const uint32_t& curSeqNum,
                                 chachapolyaead_ctx* sesCtx)
{
   int retVal = -1;
//   assert(cipherSize <= (plainSize - POLY1305MACLEN));

   if(chacha20poly1305_crypt(sesCtx, curSeqNum, plainData, cipherData,
                             cipherSize, AUTHASSOCDATAFIELDLEN,
                             CHACHAPOLY1305_AEAD_DEC) == 0)
   {
      retVal = 0;
   }

   return retVal;
}
