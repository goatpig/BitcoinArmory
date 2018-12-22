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
//
// NOTE: There is a very subtle implementation detail in BIP 151 that requires
// attention. BIP 151 explicitly states that it uses ChaCha20Poly1305 as used in
// OpenSSH. This is important. RFC 7539 is a formalized version of what's in
// OpenSSH, with tiny changes. In particular, the OpenSSH version of Poly1305
// uses 64-bit nonces. RFC 7539 uses 96-bit nonces. Because of this, THE
// IMPLEMENTATIONS ARE INCOMPATIBLE WHEN VERIFYING THE OTHER VARIANT'S POLY1305
// TAGS. Bcrypto (the only library used in a public BIP 151 implementation as of
// June 2018) appears to use the RFC 7539 variant. So, as of June 2018, there
// are no Bitcoin-related codebases that can generate mutually verifiable
// Poly1305 data. (See https://tools.ietf.org/html/rfc7539#section-2.8 for more
// info.)
//
// Despite the OpenSSH and RFC differences, the two variants can generate the
// same encrypted results. However, this is another thing to note if Armory
// attempts to interoperate directly with other BIP 151 nodes in the future.

#ifndef BIP151_H
#define BIP151_H

#include <cstdint>

#include "secp256k1.h"
#include "btc/ecc_key.h"
extern "C" {
#include "chachapoly_aead.h"
}
#include "BinaryData.h"

// With ChaCha20Poly1305, 1 GB is the max 
#define CHACHA20POLY1305MAXBYTESSENT 10000000000
#define POLY1305MACLEN 16
#define AUTHASSOCDATAFIELDLEN 4
#define CHACHAPOLY1305_AEAD_ENC 1
#define CHACHAPOLY1305_AEAD_DEC 0
#define BIP151PUBKEYSIZE 33
#define ENCINITMSGSIZE 34

// Match against BIP 151 spec, although "INVALID" is our choice.
enum class bip151SymCiphers : uint8_t {CHACHA20POLY1305_OPENSSH = 0, INVALID};

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
   // Constructor setting the session direction.
   bip151Session(const bool& sessOut);
   // Constructor manually setting the ECDH setup prv key. USE WITH CAUTION.
   bip151Session(btc_key* inSymECDHPrivKey, const bool& sessOut);
   // Set up the symmetric keys needed for the session.
   int symKeySetup(const uint8_t* peerPubKey, const size_t& peerKeyPubSize);
   void sessionRekey();
   // "Smart" ciphertype set. Checks to make sure it's valid.
   int setCipherType(const bip151SymCiphers& inCipher);
   void setEncinitSeen() { encinit = true; }
   void setEncackSeen() { encack = true; }
   bool encinitSeen() const { return encinit; }
   bool encackSeen() const { return encack; }
   const uint8_t* getSessionID() const { return sessionID.data(); }
   const std::string getSessionIDHex() const;
   const bool handshakeComplete() const { return (encinit == true && encack == true); }
   const bool getBytesOnCurKeys() const { return bytesOnCurKeys; }
   void setOutgoing() { isOutgoing = true; }
   const bool getOutgoing() const { return isOutgoing; }
   bool getSeqNum() const { return seqNum; }
   const bip151SymCiphers getCipherType() const { return cipherType; }
   int inMsgIsRekey(const uint8_t* inMsg, const size_t& inMsgSize);
   const bool rekeyNeeded();
   void addBytes(const uint32_t& sentBytes) { bytesOnCurKeys += sentBytes; }
   int getEncinitData(uint8_t* initBuffer, const size_t& initBufferSize,
                      const bip151SymCiphers& cipherType);
   int getEncackData(uint8_t* ackBuffer, const size_t& ackBufferSize);
   bool isCipherValid(const bip151SymCiphers& inCipher);
   void incSeqNum() { ++seqNum; };
   chachapolyaead_ctx* getSessionCtxPtr() { return &sessionCTX; };
   int encPayload(uint8_t* cipherData, const size_t cipherSize,
                  const uint8_t* plainData, const size_t plainSize);
   int decPayload(const uint8_t* cipherData, const size_t cipherSize,
                  uint8_t* plainData, const size_t plainSize);
};

class bip151Connection
{
private:
   bip151Session inSes;
   bip151Session outSes;

public:
   // Default constructor - Used when initiating contact with a peer.
   bip151Connection();
   // Constructor manually setting the ECDH setup prv keys. USE WITH CAUTION.
   bip151Connection(btc_key* inSymECDHPrivKeyIn, btc_key* inSymECDHPrivKeyOut);
   int assemblePacket(const uint8_t* plainData, const size_t& plainSize,
                      uint8_t* cipherData, const size_t& cipherSize);
   int decryptPacket(const uint8_t* cipherData, const size_t& cipherSize,
                     uint8_t* plainData, const size_t& plainSize);
   int processEncinit(const uint8_t* inMsg, const size_t& inMsgSize,
                      const bool outDir);
   int processEncack(const uint8_t* inMsg, const size_t& inMsgSize,
                     const bool outDir);
   const int getEncinitData(uint8_t* encinitBuf, const size_t& encinitBufSize,
                            const bip151SymCiphers& cipherType);
   const int getEncackData(uint8_t* encackBuf, const size_t& encBufSize);
   const bool rekeyNeeded() { return outSes.rekeyNeeded(); }
   void getRekeyBuf(uint8_t* encackBuf, const size_t& encackSize);
   void rekeyConn(uint8_t* encackBuf, const size_t& encackSize);
   const uint8_t* getSessionID(const bool& dirIsOut);
   const bool connectionComplete() const { return(inSes.handshakeComplete() == true &&
                                            outSes.handshakeComplete() == true); }
};

// Class to use on BIP 151 encrypted messages. Contains the plaintext contents
// and can generate plaintext packet contents but not the Poly1305 tag.
class bip151Message
{
private:
   BinaryData cmd;
   BinaryData payload;

public:
   bip151Message();
   bip151Message(uint8_t* plaintextData, uint32_t plaintextDataSize);
   bip151Message(const uint8_t* inCmd, const size_t& inCmdSize,
                 const uint8_t* inPayload, const size_t& inPayloadSize);
   void setEncStructData(const uint8_t* inCmd, const size_t& inCmdSize,
                         const uint8_t* inPayload, const size_t& inPayloadSize);
   int setEncStruct(uint8_t* plaintextData, uint32_t& plaintextDataSize);
   void getEncStructMsg(uint8_t* outStruct, const size_t& outStructSize,
                        size_t& finalStructSize);
   void getCmd(uint8_t* cmdBuf, const size_t& cmdBufSize);
   const size_t getCmdSize() const { return cmd.getSize(); }
   const uint8_t* getCmdPtr() const { return cmd.getPtr(); }
   void getPayload(uint8_t* payloadBuf, const size_t& payloadBufSize);
   const size_t getPayloadSize() const { return payload.getSize(); }
   const uint8_t* getPayloadPtr() const { return payload.getPtr(); }
   const size_t messageSizeHint();
};

#endif // BIP151_H
