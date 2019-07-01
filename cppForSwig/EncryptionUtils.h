////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// Implements canned routines in Crypto++ for AES encryption (for wallet
// security), ECDSA (which is already available in the python interface,
// but it is slow, so we might as well use the fast C++ method if avail),
// time- and memory-hard key derivation functions (resistent to brute
// force, and designed to be too difficult for a GPU to implement), and
// secure binary data handling (to make sure we don't leave sensitive 
// data floating around in application memory).
//
// 
// For the KDF:
//
// This technique is described in Colin Percival's paper on memory-hard 
// key-derivation functions, used to create "scrypt":
//
//       http://www.tarsnap.com/scrypt/scrypt.pdf
//
// The goal is to create a key-derivation function that can force a memory
// requirement on the thread applying the KDF.  By picking a sequence-length
// of 1,000,000, each thread will require 32 MB of memory to compute the keys,
// which completely disarms GPUs of their massive parallelization capabilities
// (for maximum parallelization, the kernel must use less than 1-2 MB/thread)
//
// Even with less than 1,000,000 hashes, as long as it requires more than 64
// kB of memory, a GPU will have to store the computed lookup tables in global
// memory, which is extremely slow for random lookup.  As a result, GPUs are 
// no better (and possibly much worse) than a CPU for brute-forcing the passwd
//
// This KDF is actually the ROMIX algorithm described on page 6 of Colin's
// paper.  This was chosen because it is the simplest technique that provably
// achieves the goal of being secure, and memory-hard.
//
// The computeKdfParams method well test the speed of the system it is running
// on, and try to pick the largest memory-size the system can compute in less
// than 0.25s (or specified target).  
//
//
// NOTE:  If you are getting an error about invalid argument types, from python,
//        it is usually because you passed in a BinaryData/Python-string instead
//        of a SecureBinaryData object
//
////////////////////////////////////////////////////////////////////////////////

#ifndef _ENCRYPTION_UTILS_
#define _ENCRYPTION_UTILS_

#include <iostream>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>

#include "BinaryData.h"
#include "UniversalTimer.h"
#include "SecureBinaryData.h"

//#define LIBBTC_ONLY

#ifdef LIBBTC_ONLY
#include "btc/random.h"
#include "btc/ctaes.h"
#include <btc/aes256_cbc.h>
#include <btc/ecc_key.h>
#endif

// We will look for a high memory value to use in the KDF
// But as a safety check, we should probably put a cap
// on how much memory the KDF can use -- 32 MB is good
// If a KDF uses 32 MB of memory, it is undeniably easier
// to compute on a CPU than a GPU.
#define DEFAULT_KDF_MAX_MEMORY 32*1024*1024

#define CRYPTO_DEBUG false

#ifndef LIBBTC_ONLY
#include "cryptopp/cryptlib.h"
#include "cryptopp/osrng.h"
#include "cryptopp/sha.h"
#include "cryptopp/aes.h"
#include "cryptopp/modes.h"
#include "cryptopp/eccrypto.h"
#include "cryptopp/filters.h"
#include "cryptopp/DetSign.h"

#define UNSIGNED    ((CryptoPP::Integer::Signedness)(0))
#define BTC_AES       CryptoPP::AES
#define BTC_CFB_MODE  CryptoPP::CFB_Mode
#define BTC_CBC_MODE  CryptoPP::CBC_Mode
#define BTC_PRNG      CryptoPP::AutoSeededX917RNG<CryptoPP::AES>

#define BTC_ECPOINT   CryptoPP::ECP::Point
#define BTC_PUBKEY    CryptoPP::ECDSA<CryptoPP::ECP, CryptoPP::SHA256>::PublicKey
#define BTC_PRIVKEY   CryptoPP::ECDSA<CryptoPP::ECP, CryptoPP::SHA256>::PrivateKey

#define BTC_ECDSA     CryptoPP::ECDSA<CryptoPP::ECP, CryptoPP::SHA256>
#define BTC_SIGNER    CryptoPP::ECDSA<CryptoPP::ECP, CryptoPP::SHA256>::Signer
#define BTC_DETSIGNER CryptoPP::ECDSA_DetSign<CryptoPP::ECP, CryptoPP::SHA256>::DetSigner
#define BTC_VERIFIER  CryptoPP::ECDSA<CryptoPP::ECP, CryptoPP::SHA256>::Verifier

// Make some libbtc-specific #defines and #includes as needed.
#else
// libbtc doesn't have some #defines AES bits, so we'll make them.
#define AES_MIN_KEY_LEN AES_BLOCK_SIZE
#define AES_MAX_KEY_LEN AES_BLOCK_SIZE*2
#endif


////////////////////////////////////////////////////////////////////////////////
class CryptoSHA2
{
public:
   static void getHash256(BinaryDataRef bdr, uint8_t* digest);
   static void getSha256(BinaryDataRef bdr, uint8_t* digest);
   static void getHMAC256(BinaryDataRef data, BinaryDataRef msg,
      uint8_t* digest);

   static void getSha512(BinaryDataRef bdr, uint8_t* digest);
   static void getHMAC512(BinaryDataRef data, BinaryDataRef msg,
      uint8_t* digest);
};

class CryptoHASH160
{
public:
   static void getHash160(BinaryDataRef bdr, uint8_t* digest);
};

////////////////////////////////////////////////////////////////////////////////
class CryptoPRNG
{
public:
   static SecureBinaryData generateRandom(uint32_t numBytes,
      SecureBinaryData extraEntropy = SecureBinaryData());
};

////////////////////////////////////////////////////////////////////////////////
// A memory-bound key-derivation function -- uses a variation of Colin 
// Percival's ROMix algorithm: http://www.tarsnap.com/scrypt/scrypt.pdf
//
// The computeKdfParams method takes in a target time, T, for computation
// on the computer executing the test.  The final KDF should take somewhere
// between T/2 and T seconds.
class KdfRomix
{
public:

   /////////////////////////////////////////////////////////////////////////////
   KdfRomix(void);

   /////////////////////////////////////////////////////////////////////////////
   KdfRomix(uint32_t memReqts, uint32_t numIter, SecureBinaryData salt);


   /////////////////////////////////////////////////////////////////////////////
   // Default max-memory reqt will 
   void computeKdfParams(double   targetComputeSec=0.25, 
                         uint32_t maxMemReqtsBytes=DEFAULT_KDF_MAX_MEMORY);

   /////////////////////////////////////////////////////////////////////////////
   void usePrecomputedKdfParams(uint32_t memReqts, 
                                uint32_t numIter, 
                                SecureBinaryData salt);

   /////////////////////////////////////////////////////////////////////////////
   void printKdfParams(void);

   /////////////////////////////////////////////////////////////////////////////
   SecureBinaryData DeriveKey_OneIter(SecureBinaryData const & password);

   /////////////////////////////////////////////////////////////////////////////
   SecureBinaryData DeriveKey(SecureBinaryData const & password);

   /////////////////////////////////////////////////////////////////////////////
   std::string       getHashFunctionName(void) const { return hashFunctionName_; }
   uint32_t     getMemoryReqtBytes(void) const  { return memoryReqtBytes_; }
   uint32_t     getNumIterations(void) const    { return numIterations_; }
   SecureBinaryData   getSalt(void) const       { return salt_; }
   
private:

   std::string   hashFunctionName_;  // name of hash function to use (only one)
   uint32_t hashOutputBytes_;
   uint32_t kdfOutputBytes_;    // size of final key data

   uint32_t memoryReqtBytes_;
   uint32_t sequenceCount_;
   SecureBinaryData lookupTable_;
   SecureBinaryData salt_;            // prob not necessary amidst numIter, memReqts
                                // but I guess it can't hurt

   uint32_t numIterations_;     // We set the ROMIX params for a given memory 
                                // req't. Then run it numIter times to meet
                                // the computation-time req't
};


////////////////////////////////////////////////////////////////////////////////
// Leverage CryptoPP library for AES encryption/decryption
class CryptoAES
{
public:
   /////////////////////////////////////////////////////////////////////////////
   static SecureBinaryData EncryptCFB(const SecureBinaryData & data, 
                               const SecureBinaryData & key,
                               const SecureBinaryData & iv);

   /////////////////////////////////////////////////////////////////////////////
   static SecureBinaryData DecryptCFB(const SecureBinaryData & data, 
                               const SecureBinaryData & key,
                               const SecureBinaryData& iv);

   /////////////////////////////////////////////////////////////////////////////
   static SecureBinaryData EncryptCBC(const SecureBinaryData & data, 
                               const SecureBinaryData & key,
                               const SecureBinaryData & iv);

   /////////////////////////////////////////////////////////////////////////////
   static SecureBinaryData DecryptCBC(const SecureBinaryData & data, 
                               const SecureBinaryData & key,
                               const SecureBinaryData & iv);
};





////////////////////////////////////////////////////////////////////////////////
// Create a C++ interface to the Crypto++ ECDSA ops:  should be more secure
// and much faster than the pure-python methods created by Lis
//
// These methods might as well just be static methods, but SWIG doesn't like
// static methods.  So we will invoke these via CryptoECDSA().Function()
class CryptoECDSA
{
public:
   CryptoECDSA(void) {}

   /////////////////////////////////////////////////////////////////////////////
   static SecureBinaryData CreateNewPrivateKey(
      SecureBinaryData extraEntropy = SecureBinaryData())
   {
      return CryptoPRNG::generateRandom(32, extraEntropy);
   }
   
   /////////////////////////////////////////////////////////////////////////////
   static bool CheckPubPrivKeyMatch(SecureBinaryData const & cppPrivKey,
                                    SecureBinaryData  const & cppPubKey)
   {
      auto&& pubkey = CryptoECDSA().ComputePublicKey(cppPrivKey);
      return pubkey == cppPubKey;
   }

   
   /////////////////////////////////////////////////////////////////////////////
   // For signing and verification, pass in original, UN-HASHED binary string.
   // For signing, k-value can use a PRNG or deterministic value (RFC 6979).
   static SecureBinaryData SignData(SecureBinaryData const & binToSign, 
      SecureBinaryData const & cppPrivKey, const bool& detSign = true);

   /////////////////////////////////////////////////////////////////////////////
   // We need to make sure that we have methods that take only secure strings
   // and return secure strings (I don't feel like figuring out how to get 
   // SWIG to take BTC_PUBKEY and BTC_PRIVKEY
   
   /////////////////////////////////////////////////////////////////////////////
   SecureBinaryData ComputePublicKey(
      SecureBinaryData const & cppPrivKey, bool compressed = false) const;

   /////////////////////////////////////////////////////////////////////////////
   bool VerifyPublicKeyValid(SecureBinaryData const & pubKey);

   /////////////////////////////////////////////////////////////////////////////
   // The version with no mlock'd memory. Reduces copies for increased speed,
   // meant for mined tx verification.
   bool VerifyData(BinaryData const & binMessage,
      const BinaryData& sig,
      BinaryData const & cppPubKey) const;


   /////////////////////////////////////////////////////////////////////////////
   // Deterministically generate new private key using a chaincode
   // Changed:  Added using the hash of the public key to the mix
   //           b/c multiplying by the chaincode alone is too "linear"
   //           (there's no reason to believe it's insecure, but it doesn't
   //           hurt to add some extra entropy/non-linearity to the chain
   //           generation process)
   SecureBinaryData ComputeChainedPrivateKey(
                           SecureBinaryData const & binPrivKey,
                           SecureBinaryData const & chainCode,
                           SecureBinaryData* computedMultiplier=NULL);
                               
   /////////////////////////////////////////////////////////////////////////////
   // Deterministically generate new private key using a chaincode
   SecureBinaryData ComputeChainedPublicKey(
                           SecureBinaryData const & binPubKey,
                           SecureBinaryData const & chainCode,
                           SecureBinaryData* multiplierOut=NULL);

   /////////////////////////////////////////////////////////////////////////////
   // We need some direct access to Crypto++ math functions
   SecureBinaryData InvMod(const SecureBinaryData& m);

   /////////////////////////////////////////////////////////////////////////////
   // Some standard ECC operations
   ////////////////////////////////////////////////////////////////////////////////
   bool ECVerifyPoint(BinaryData const & x,
                      BinaryData const & y);

   /////////////////////////////////////////////////////////////////////////////
   // For Point-compression
   static SecureBinaryData CompressPoint(SecureBinaryData const & pubKey65);
   static SecureBinaryData UncompressPoint(SecureBinaryData const & pubKey33);

   ////////////////////////////////////////////////////////////////////////////////
   // for ECDH
   static SecureBinaryData PrivKeyScalarMultiply(
      const SecureBinaryData& privKey,
      const SecureBinaryData& scalar);

   ////////////////////////////////////////////////////////////////////////////////
   static SecureBinaryData PubKeyScalarMultiply(
      const SecureBinaryData& pubKey,
      const SecureBinaryData& scalar);

#ifndef LIBBTC_ONLY
   /////////////////////////////////////////////////////////////////////////////
   static BTC_PRIVKEY ParsePrivateKey(SecureBinaryData const & privKeyData);
   static BTC_PUBKEY ComputePublicKey(BTC_PRIVKEY const & cppPrivKey);
   
   /////////////////////////////////////////////////////////////////////////////
   static BinaryData computeLowS(BinaryDataRef s);
#endif
};

#endif
