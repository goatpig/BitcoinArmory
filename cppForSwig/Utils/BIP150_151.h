////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2026, goatpig                                          //
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

#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <secp256k1.h>
#include <btc/ecc_key.h>
extern "C" {
#include "chachapoly_aead.h"
}
#include "BinaryData.h"

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

namespace Armory
{
   namespace NetworkPeers
   {
      class PeerStoreView;
   }
}

// Match against BIP 151 spec, although "INVALID" is our addition.
enum class BIP151SymCiphers : uint8_t {
   CHACHA20POLY1305_OPENSSH = 0x00,
   INVALID
};

// Track BIP 150 message state.
enum class BIP150State : uint8_t {
   INACTIVE = 0x00,
   CHALLENGE1,
   REPLY1,
   PROPOSE,
   CHALLENGE2,
   REPLY2,
   SUCCESS,
   ERR_STATE
};

// Global functions needed to deal with a global libsecp256k1 context.
// libbtc doesn't export its libsecp256k1 context (which, by the way, is set up
// for extra stuff we currently don't need). We need a context because libbtc
// doesn't care about ECDH and forces us to go straight to libsecp256k1. We
// could alter the code but that would make it impossible to verify an upstream
// code match. The solution: Create our own global context, and use it only for
// ECDH stuff. (Also, try to upstream a libbtc patch so that we can piggyback
// off of their context.) Call these alongside any startup and shutdown code.
void startupBIP151CTX(void);
void shutdownBIP151CTX(void);

// Global function used to load up the key DBs. CALL AFTER BIP 151 IS INITIALIZED.
void startupBIP150CTX(uint32_t);

class BIP151Session
{
   friend class BIP150StateMachine;

private:
   const bool isOutgoing_;
   chachapolyaead_ctx sessionCTX_; // Session context
   std::array<uint8_t, BIP151PRVKEYSIZE> sessionID_{}; // Session ID
   std::array<uint8_t, BIP151PRVKEYSIZE*2> hkdfKeySet_{}; // K1=Payload, K2=Data size
   btc_key genSymECDHPrivKey_; // Prv key for ECDH deriv. Delete ASAP once used.
   uint32_t bytesOnCurKeys_ = 0; // Bytes ctr for when to switch
   BIP151SymCiphers cipherType_ = BIP151SymCiphers::INVALID;
   uint32_t seqNum_ = 0;
   bool encinit_ = false;
   bool encack_ = false;
   bool ecdhPubKeyGenerated_ = false;

private:
   void calcChaCha20Poly1305Keys(const btc_key&);
   void calcSessionID(const btc_key&);
   int verifyCipherType(void);
   int genSymKeys(const BinaryDataRef&);

   void chacha20Poly1305Rekey(uint8_t* keyToUpdate, size_t keySize,
      bool bip151Rekey,
      const BinaryDataRef&,
      const BinaryDataRef&,
      const BinaryDataRef&);

public:
   // Constructor setting the session direction.
   BIP151Session(bool);
   // Constructor manually setting the ECDH setup prv key. USE WITH CAUTION.
   BIP151Session(bool, btc_key*);

   // Set up the symmetric keys needed for the session.
   int symKeySetup(const BinaryDataRef&);
   void sessionRekey(bool,
      const BinaryDataRef&,
      const BinaryDataRef&,
      const BinaryDataRef&);

   // "Smart" ciphertype set. Checks to make sure it's valid.
   int setCipherType(BIP151SymCiphers);
   void setEncinitSeen() { encinit_ = true; }
   void setEncackSeen() { encack_ = true; }
   bool encinitSeen() const { return encinit_; }
   const uint8_t* getSessionID() const { return sessionID_.data(); }
   bool handshakeComplete() const {
      return (encinit_ == true && encack_ == true);
   }
   BIP151SymCiphers getCipherType() const { return cipherType_; }
   int inMsgIsRekey(const BinaryDataRef&) const;
   bool rekeyNeeded(size_t) const;

   int getEncinitData(BinaryData&, BIP151SymCiphers);
   int getEncackData(BinaryData&);
   bool isCipherValid(BIP151SymCiphers);
   int encPayload(uint8_t*, size_t, const BinaryDataRef&);
   int decPayload(const BinaryDataRef&, BinaryData&);
};

class BIP150StateMachine
{
   /***
   Design note: There will be only one pub/prv ID key for the system. Making
   global vars would be ideal. But, we don't want the private key exposed.
   Bite the bullet and give each 151 connection a copy via its 150 state
   machine. We won't have many connections open, so the I/O hit's minimal.
   ***/

private:
   BIP150State curState_;
   BIP151Session* inSess_;
   BIP151Session* outSess_;
   BinaryData chosenAuthPeerKey_;

   std::unique_ptr<Armory::NetworkPeers::PeerStoreView> peerView_;
   const bool oneWayAuth_;

private:
   int buildHashData(BinaryData&, const BinaryDataRef&, bool);
   inline void resetSM(void);

public:
   BIP150StateMachine(
      BIP151Session* incomingSes, BIP151Session* outgoingSes,
      std::unique_ptr<Armory::NetworkPeers::PeerStoreView>, bool oneWayAuth);

   int processAuthchallenge(const BinaryDataRef&, bool);
   int processAuthreply(const BinaryDataRef&, bool);
   int processAuthpropose(const BinaryDataRef&);

   int getAuthchallengeData(BinaryData&, const std::string&, bool);
   int getAuthreplyData(BinaryData&, bool);
   int getAuthproposeData(BinaryData&);

   BIP150State getBIP150State(void) const { return curState_; }
   int errorSM(const int&);
   void rekey(void);

   BinaryDataRef getOwnPubKey(void) const;
   bool havePublicKey(const BinaryDataRef&, const std::string&) const;
   const BinaryDataRef getChosenAuthPeerKey() const { return chosenAuthPeerKey_; }
   bool isOneWayAuth() const { return oneWayAuth_; }
};

class BIP151Connection
{
private:
   BIP151Session inSes_;
   BIP151Session outSes_;
   BIP150StateMachine bip150SM_;

   int getRekeyBuf(BinaryData&) const;
   bool goodPropose_ = false;

public:
   // Default constructor - Used when initiating contact with a peer.
   BIP151Connection(std::unique_ptr<Armory::NetworkPeers::PeerStoreView>, bool);

   //encryption methods
   int assemblePacket(const BinaryDataRef&, uint8_t*, size_t);
   int decryptPacket(const BinaryDataRef&, BinaryData&);

   //const getters
   bool rekeyNeeded(size_t sz) const { return outSes_.rekeyNeeded(sz); }
   bool connectionComplete() const {
      return (inSes_.handshakeComplete() == true &&
         outSes_.handshakeComplete() == true);
   }
   BIP150State getBIP150State() const { return bip150SM_.getBIP150State(); }
   bool isOneWayAuth() const { return bip150SM_.isOneWayAuth(); }
   BinaryDataRef getOwnPubKey(void) const;
   BinaryDataRef getChosenAuthPeerKey(void) const { return bip150SM_.getChosenAuthPeerKey(); }
   bool havePublicKey(const BinaryDataRef&, const std::string&) const;

   // BIP 150 handhsake methods
   int processAuthchallenge(const BinaryDataRef&, bool);
   int processAuthreply(const BinaryDataRef&, bool);
   int processAuthpropose(const BinaryDataRef&);
   int getAuthchallengeData(BinaryData&, const std::string&, bool);
   int getAuthreplyData(BinaryData&, bool);
   int getAuthproposeData(BinaryData&);
   void bip150HandshakeRekey(void);

   //BIP 151 handshake methods
   int processEncinit(const BinaryDataRef&, bool);
   int processEncack(const BinaryDataRef&, bool);
   int getEncinitData(BinaryData&, BIP151SymCiphers);
   int getEncackData(BinaryData&);
   void rekeyOuterSession() { outSes_.sessionRekey(true, {}, {}, {}); }

   /**** unit test methods ****/
   BIP151Connection(btc_key*, btc_key*,
      std::unique_ptr<Armory::NetworkPeers::PeerStoreView>, bool);
   const uint8_t* getSessionID(bool) const;
   int bip151RekeyConn(BinaryData&);
   std::string getBIP150Fingerprint(void) const;
};
