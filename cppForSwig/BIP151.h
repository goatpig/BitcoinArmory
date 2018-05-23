////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// A BIP 151 implementation for Armory. As of May 2018, BIP 151 isn't in Core.
// The immediate purpose of this code is to implement secure data transfer
// between an Armory server and a remote Armory client, the server talking to
// Core and feeding the (encrypted) data to the client.

#ifndef BIP151_H
#define BIP151_H

#include <cstdint>

#include "secp256k1.h"
#include "btc/ecc_key.h"
extern "C" {
#include "chachapoly_aead.h"
}

// With ChaCha20Poly1305, 1 GB is the max 
#define CHACHA20POLY1305MAXBYTESSENT 10000000000
#define POLY1305MACLEN 16
#define AUTHASSOCDATAFIELDLEN 4
#define CHACHAPOLY1305_AEAD_ENC 1
#define CHACHAPOLY1305_AEAD_DEC 0
#define BIP151PUBKEYSIZE 33

// Match against BIP 151 spec, although "INVALID" is our choice.
enum class bip151SymCiphers : uint8_t {CHACHA20POLY1305 = 0, INVALID};

// Global functions needed to deal with a global libsecp256k1 context.
// libbtc doesn't export its libsecp256k1 context (which, by the way, is set up
// for extra stuff we currently don't need). We need a context because libbtc
// doesn't care about ECDH and forces us to go straight to libsecp256k1. We
// could alter the code but that would make it impossible to verify an upstream
// code match. The solution: Create our own global context, and use it only for
// ECDH stuff. (Also, try to upstream a libbtc patch so that we can piggyback
// off of their context.) Call these alongside any startup and shutdown code.
void startupBIP151CTX();
void shutdownBIP151CTX();

class bip151Session
{
private:
   chachapolyaead_ctx sessionCTX; // Session context
   std::array<uint8_t, 32> sessionID{}; // Session ID
   std::array<uint8_t, 64> hkdfKeySet{}; // 2 32-byte keys (K1=Payload, K2=Data size)
   btc_key genSymECDHPrivKey; // Prv key for ECDH deriv. Delete ASAP once used.
   uint32_t bytesOnCurKeys = 0; // Bytes ctr for when to switch
   bip151SymCiphers cipherType = bip151SymCiphers::INVALID;
   uint32_t seqNum = 0;
   bool encinit = false;
   bool encack = false;
   bool isOutgoing = false;

   void calcChaCha20Poly1305Keys(const btc_key& sesECDHKey);
   void calcSessionID(const btc_key& sesECDHKey);
   int verifyCipherType();
   void gettempECDHPubKey(btc_pubkey* tempECDHPubKey);
   int genSymKeys(const uint8_t* peerECDHPubKey);
   void chacha20Poly1305Rekey(uint8_t* keyToUpdate, const size_t& keySize);

public:
   // Default constructor - Used when initiating contact with a peer.
   bip151Session();
   // Constructor setting the session direction.
   bip151Session(const bool& sessOut);
   // Constructor manually setting the ECDH setup prv key. USE WITH CAUTION.
   bip151Session(btc_key* inSymECDHPrivKey, const bool& sessOut);
   // Set up the symmetric keys needed for the session.
   int symKeySetup(const uint8_t* peerPubKey, const size_t& peerKeyPubSize);
   void sessionRekey();
   // "Smart" ciphertype set. Checks to make sure it's valid.
   int setCipherType(const bip151SymCiphers& inCipher);
   void setEncinit() { encinit = true; }
   void setEncack() { encack = true; }
   bool getEncinit() const { return encinit; }
   bool getEncack() const { return encack; }
   const uint8_t* getSessionID() const { return sessionID.data(); }
   const std::string getSessionIDHex();
   bool handshakeComplete() const { return (encinit == true && encack == true); }
   bool getBytesOnCurKeys() const { return bytesOnCurKeys; }
   void setOutgoing() { isOutgoing = true; }
   bool getOutgoing() const { return isOutgoing; }
   bool getSeqNum() const { return seqNum; }
   int inMsgIsRekey(const uint8_t* inMsg, const size_t& inMsgSize);
   bool rekeyNeeded();
   void addBytes(const uint32_t& sentBytes) { bytesOnCurKeys += sentBytes; }
   int sendEncinit(const bip151SymCiphers& cipherType);
   int sendEncack(const bip151SymCiphers& cipherType);
   bool isCipherValid(const bip151SymCiphers& inCipher);
   void incSeqNum() { ++seqNum; };
   chachapolyaead_ctx* getSessionCtxPtr() { return &sessionCTX; };
};

class bip151Connection
{
private:
   bip151Session inSes;
   bip151Session outSes;

   int encPayload(uint8_t* cipherData, const size_t& cipherSize,
                  const uint8_t* plainData, const size_t plainSize,
                  const uint32_t& curSeqNum, chachapolyaead_ctx* sesCtx);
   int decPayload(const uint8_t* cipherData, const size_t& cipherSize,
                  uint8_t* plainData, const size_t plainSize,
                  const uint32_t& curSeqNum, chachapolyaead_ctx* sesCtx);

public:
   // Default constructor - Used when initiating contact with a peer.
   bip151Connection();
   int assemblePacket(const uint8_t* packetData, const size_t& packetDataSize,
                      const bool& outDir);
   int decryptPacket(const uint8_t* packetData, const size_t& packetDataSize,
                     const bool& outDir);
   int handleEncinit(const uint8_t* inMsg, const size_t& inMsgSize,
                     const bip151SymCiphers& cipherType, const bool outDir);
   int handleEncack(const uint8_t* inMsg, const size_t& inMsgSize,
                    const bool outDir);
   bool connectionComplete() const { return(inSes.handshakeComplete() == true &&
                                            outSes.handshakeComplete() == true); }
};

#endif // BIP151_H
