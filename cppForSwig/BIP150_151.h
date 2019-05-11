////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2019, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// A BIP 150/151 implementation for Armory. As of Aug. 2018, BIP 150/151 isn't
// in Bitcoin Core. The immediate purpose of this code is to implement secure
// data transfer between an Armory server and a remote Armory client (i.e., the
// server talking to Core in an unencrypted (for now?) manner and feeding the
// (encrypted) data to the client).
//
// NOTE: As of Aug. 2018, BIP 151 is set for rewriting, and possible replacement
// by another BIP. The code in Armory is based on the BIP 151 spec as of July
// 2018. The BIP 151 replacement may be coded later.
//
// NOTE: There is a very subtle implementation detail in BIP 151 that requires
// attention. BIP 151 explicitly states that it uses ChaCha20Poly1305 as used in
// OpenSSH. This is important. RFC 7539 is a formalized version of what's in
// OpenSSH, with tiny changes. For example, the OpenSSH version of Poly1305 uses
// 64-bit nonces, and RFC 7539 uses 96-bit nonces. Because of this, THE
// IMPLEMENTATIONS ARE INCOMPATIBLE WHEN VERIFYING THE OTHER VARIANT'S POLY1305
// TAGS. As of July 2018, there are no codebases that can generate mutually
// verifiable BIP 150/151 test data. (See https://tools.ietf.org/html/rfc7539
// and https://github.com/openssh/openssh-portable/blob/master/PROTOCOL.chacha20poly1305
// for more info.)

#ifndef BIP150_151_H
#define BIP150_151_H

#include <array>
#include <cstdint>
#include <string>
#include <map>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <functional>

#include "secp256k1.h"
#include "btc/ecc_key.h"
extern "C" {
#include "chachapoly_aead.h"
}
#include "BinaryData.h"
#include "EncryptionUtils.h"

// With ChaCha20Poly1305, 1 GB is the max 
#ifndef UNIT_TESTS
#define CHACHA20POLY1305MAXBYTESSENT 1000000000
#else 
#define CHACHA20POLY1305MAXBYTESSENT 1200
#endif
#define POLY1305MACLEN 16
#define AUTHASSOCDATAFIELDLEN 4
#define CHACHAPOLY1305_AEAD_ENC 1
#define CHACHAPOLY1305_AEAD_DEC 0
#define BIP151PRVKEYSIZE 32
#define BIP151PUBKEYSIZE 33
#define ENCINITMSGSIZE 34

extern bool publicRequester;

// Match against BIP 151 spec, although "INVALID" is our addition.
enum class BIP151SymCiphers : uint8_t {CHACHA20POLY1305_OPENSSH = 0x00,
                                       INVALID};

// Track BIP 150 message state.
enum class BIP150State : uint8_t {INACTIVE = 0x00,
                                  CHALLENGE1,
                                  REPLY1,
                                  PROPOSE,
                                  CHALLENGE2,
                                  REPLY2,
                                  SUCCESS,
                                  ERR_STATE};

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

// Global function used to load up the key DBs. Takes the IP version and a flag
// indicating if verification is done only by the client (false by default).
void startupBIP150CTX(const uint32_t& ipVer, const bool& pubReq = false);

struct AuthPeersLambdas
{
private:
   std::function<const std::map<std::string, btc_pubkey>&()> getPubKeyMapLambda_;
   std::function<const SecureBinaryData&(const BinaryDataRef&)> getPrivKeyLambda_;
   std::function<const std::set<SecureBinaryData>&()> getAuthKeySet_;

public:
   AuthPeersLambdas(
      std::function<const std::map<std::string, btc_pubkey>&()> pubkeymap,
      std::function<const SecureBinaryData&(const BinaryDataRef&)> privkey,
      std::function<const std::set<SecureBinaryData>&()> getauthset):
      getPubKeyMapLambda_(pubkeymap), getPrivKeyLambda_(privkey),
      getAuthKeySet_(getauthset)
   {}

   const btc_pubkey& getPubKey(const std::string&) const;
   const SecureBinaryData& getPrivKey(const BinaryDataRef&) const;
   const std::set<SecureBinaryData>& getAuthorizedKeySet(void) const;
};

class BIP151Session
{
   friend class BIP150StateMachine;

private:
   chachapolyaead_ctx sessionCTX_; // Session context
   std::array<uint8_t, BIP151PRVKEYSIZE> sessionID_{}; // Session ID
   std::array<uint8_t, BIP151PRVKEYSIZE*2> hkdfKeySet_{}; // K1=Payload, K2=Data size
   btc_key genSymECDHPrivKey_; // Prv key for ECDH deriv. Delete ASAP once used.
   uint32_t bytesOnCurKeys_ = 0; // Bytes ctr for when to switch
   BIP151SymCiphers cipherType_ = BIP151SymCiphers::INVALID;
   uint32_t seqNum_ = 0;
   bool encinit_ = false;
   bool encack_ = false;
   bool isOutgoing_ = false;
   bool ecdhPubKeyGenerated_ = false;

   void calcChaCha20Poly1305Keys(const btc_key& sesECDHKey);
   void calcSessionID(const btc_key& sesECDHKey);
   int verifyCipherType();
   void gettempECDHPubKey(btc_pubkey* tempECDHPubKey);
   int genSymKeys(const uint8_t* peerECDHPubKey);
   void chacha20Poly1305Rekey(
      uint8_t* keyToUpdate, const size_t& keySize,
      const bool& bip151Rekey,
      const uint8_t* bip150ReqIDKey, const size_t& bip150ReqIDKeySize,
      const uint8_t* bip150ResIDKey, const size_t& bip150ResIDKeySize,
      const uint8_t* oppositeChannelCipherKey, const size_t& oppositeChannelCipherKeySize);

public:
   // Constructor setting the session direction.
   BIP151Session(const bool& sessOut);
   // Constructor manually setting the ECDH setup prv key. USE WITH CAUTION.
   BIP151Session(btc_key* inSymECDHPrivKey, const bool& sessOut);
   // Set up the symmetric keys needed for the session.
   int symKeySetup(const uint8_t* peerPubKey, const size_t& peerKeyPubSize);
   void sessionRekey(const bool& bip151Rekey,
      const uint8_t* reqIDKey, const size_t& reqIDKeySize,
      const uint8_t* resIDKey, const size_t& resIDKeySize,
      const uint8_t* oppositeSessionKey, const size_t& oppositeSessionKeySize);
   // "Smart" ciphertype set. Checks to make sure it's valid.
   int setCipherType(const BIP151SymCiphers& inCipher);
   void setEncinitSeen() { encinit_ = true; }
   void setEncackSeen() { encack_ = true; }
   bool encinitSeen() const { return encinit_; }
   bool encackSeen() const { return encack_; }
   const uint8_t* getSessionID() const { return sessionID_.data(); }
   std::string getSessionIDHex() const;
   bool handshakeComplete() const {
      return (encinit_ == true && encack_ == true);
   }
   uint32_t getBytesOnCurKeys() const { return bytesOnCurKeys_; }
   void setOutgoing() { isOutgoing_ = true; }
   bool getOutgoing() const { return isOutgoing_; }
   bool getSeqNum() const { return seqNum_; }
   BIP151SymCiphers getCipherType() const { return cipherType_; }
   int inMsgIsRekey(const uint8_t* inMsg, const size_t& inMsgSize);
   bool rekeyNeeded(const size_t& sz) const;
   void addBytes(const uint32_t& sentBytes) { bytesOnCurKeys_ += sentBytes; }
   int getEncinitData(uint8_t* initBuffer, const size_t& initBufferSize,
      const BIP151SymCiphers& inCipher);
   int getEncackData(uint8_t* ackBuffer, const size_t& ackBufferSize);
   bool isCipherValid(const BIP151SymCiphers& inCipher);
   void incSeqNum() { ++seqNum_; };
   chachapolyaead_ctx* getSessionCtxPtr() { return &sessionCTX_; };
   int encPayload(uint8_t* cipherData, const size_t cipherSize,
      const uint8_t* plainData, const size_t plainSize);
   int decPayload(const uint8_t* cipherData, const size_t cipherSize,
      uint8_t* plainData, const size_t plainSize);
};

class BIP150StateMachine
{
private:
   int buildHashData(uint8_t* outHash, const uint8_t* pubKey,
      const bool& willSendHash);
   inline void resetSM();

   // Design note: There will be only one pub/prv ID key for the system. Making
   // global vars would be ideal. But, we don't want the private key exposed.
   // Bite the bullet and give each 151 connection a copy via its 150 state
   // machine. We won't have many connections open, so the I/O hit's minimal.
   BIP150State curState_;
   BIP151Session* inSes_;
   BIP151Session* outSes_;
   btc_pubkey chosenAuthPeerKey;
   btc_pubkey chosenChallengeKey;
   bool usePublicRequester_;

   AuthPeersLambdas authKeys_;

public:
   BIP150StateMachine(BIP151Session* incomingSes, BIP151Session* outgoingSes,
      AuthPeersLambdas& authkeys, const bool& publicRequester = false);

   int processAuthchallenge(const BinaryData& inData,
      const bool& requesterSent);
   int processAuthreply(BinaryData& inData, const bool& responderSent,
      const bool& goodChallenge);
   int processAuthpropose(const BinaryData& inData);
   int getAuthchallengeData(uint8_t* buf, const size_t& bufSize,
      const std::string& targetIPPort, const bool& requesterSent,
      const bool& goodPropose);
   int getAuthreplyData(uint8_t* buf, const size_t& bufSize,
      const bool& responderSent, const bool& goodChallenge);
   int getAuthproposeData(uint8_t* buf, const size_t& bufSize);
   std::string getBIP150Fingerprint();
   BIP150State getBIP150State() const { return curState_; }
   int errorSM(const int& outVal);
   void rekey(void);
//   const void clearErrorState() { curState_ = BIP150State::INACTIVE; }
   BinaryDataRef getOwnPubKey(void) const;
   bool havePublicKey(const BinaryDataRef&, const std::string&) const;
};

class BIP151Connection
{
private:
   BIP151Session inSes_;
   BIP151Session outSes_;
   BIP150StateMachine bip150SM_;

   int getRekeyBuf(uint8_t* encackBuf, const size_t& encackSize);
   bool goodPropose_ = false;

public:
   // Default constructor - Used when initiating contact with a peer.
   BIP151Connection(AuthPeersLambdas&, const bool& publicRequester = false);

   // Constructor manually setting the ECDH setup prv keys. USE WITH CAUTION.
   BIP151Connection(btc_key* inSymECDHPrivKeyIn, btc_key* inSymECDHPrivKeyOut,
      AuthPeersLambdas& authkeys, const bool& publicRequester = false);

   int assemblePacket(const uint8_t* plainData, const size_t& plainSize,
      uint8_t* cipherData, const size_t& cipherSize);
   int decryptPacket(const uint8_t* cipherData, const size_t& cipherSize,
      uint8_t* plainData, const size_t& plainSize);
   int processEncinit(const uint8_t* inMsg, const size_t& inMsgSize,
      const bool outDir);
   int processEncack(const uint8_t* inMsg, const size_t& inMsgSize,
      const bool outDir);
   int getEncinitData(uint8_t* encinitBuf, const size_t& encinitBufSize,
      const BIP151SymCiphers& inCipher);
   int getEncackData(uint8_t* encackBuf, const size_t& encBufSize);
   bool rekeyNeeded(const size_t& sz) { return outSes_.rekeyNeeded(sz); }
   int bip151RekeyConn(uint8_t* encackBuf, const size_t& encackSize);
   void rekeyOuterSession(void) { outSes_.sessionRekey(true, nullptr, 0, nullptr, 0, nullptr, 0); }
   const uint8_t* getSessionID(const bool& dirIsOut);
   bool connectionComplete() const {
      return(inSes_.handshakeComplete() == true &&
         outSes_.handshakeComplete() == true);
   }

   // BIP 150-related calls.
   int processAuthchallenge(const uint8_t* inMsg, const size_t& inMsgSize,
      const bool& requesterSent);
   int processAuthreply(const uint8_t* inMsg, const size_t& inMsgSize,
      const bool& requesterSent, const bool& goodChallenge);
   int processAuthpropose(const uint8_t* inMsg, const size_t& inMsgSize);
   int getAuthchallengeData(uint8_t* authchallengeBuf,
      const size_t& authchallengeBufSize, const std::string& targetIPPort,
      const bool& requesterSent, const bool& goodPropose);
   int getAuthreplyData(uint8_t* authreplyBuf, const size_t& authreplyBufSize,
      const bool& responderSent, const bool& goodChallenge);
   int getAuthproposeData(uint8_t* authproposeBuf,
      const size_t& authproposeBufSize);
   BIP150State getBIP150State() const { return bip150SM_.getBIP150State(); }
   std::string getBIP150Fingerprint() { return bip150SM_.getBIP150Fingerprint(); }

   void bip150HandshakeRekey(void);
   void setGoodPropose(void) { goodPropose_ = true; }
   bool getProposeFlag(void) const { return goodPropose_; }
   BinaryDataRef getOwnPubKey(void) const { return bip150SM_.getOwnPubKey(); }
   bool havePublicKey(const BinaryDataRef&, const std::string&) const;
};

// Class to use on BIP 151 encrypted messages. Contains the plaintext contents
// and can generate plaintext packet contents but not the Poly1305 tag.
class BIP151Message
{
private:
   BinaryData cmd_;
   BinaryData payload_;

public:
   BIP151Message();
   BIP151Message(uint8_t* plaintextData, uint32_t plaintextDataSize);
   BIP151Message(const uint8_t* inCmd, const size_t& inCmdSize,
      const uint8_t* inPayload, const size_t& inPayloadSize);
   void setEncStructData(const uint8_t* inCmd, const size_t& inCmdSize,
      const uint8_t* inPayload, const size_t& inPayloadSize);
   int setEncStruct(uint8_t* plaintextData, const uint32_t& plaintextDataSize);
   void getEncStructMsg(uint8_t* outStruct, const size_t& outStructSize,
      size_t& finalStructSize);
   void getCmd(uint8_t* cmdBuf, const size_t& cmdBufSize);
   size_t getCmdSize() const { return cmd_.getSize(); }
   const uint8_t* getCmdPtr() const { return cmd_.getPtr(); }
   void getPayload(uint8_t* payloadBuf, const size_t& payloadBufSize);
   size_t getPayloadSize() const { return payload_.getSize(); }
   const uint8_t* getPayloadPtr() const { return payload_.getPtr(); }
   size_t messageSizeHint();
};
#endif // BIP150_151_H
