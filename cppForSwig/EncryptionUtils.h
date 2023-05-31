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
#include <set>
#include <map>
#include <cmath>
#include <algorithm>
#include <functional>

#include "BinaryData.h"
#include "UniversalTimer.h"
#include "SecureBinaryData.h"

#include <btc/random.h>
#include <btc/ctaes.h>
#include <btc/aes256_cbc.h>
#include <btc/ecc_key.h>

// We will look for a high memory value to use in the KDF
// But as a safety check, we should probably put a cap
// on how much memory the KDF can use -- 32 MB is good
// If a KDF uses 32 MB of memory, it is undeniably easier
// to compute on a CPU than a GPU.
#define DEFAULT_KDF_MAX_MEMORY 32*1024*1024

#define CRYPTO_DEBUG false

// libbtc doesn't have some #defines AES bits, so we'll make them.
#define AES_MIN_KEY_LEN AES_BLOCK_SIZE
#define AES_MAX_KEY_LEN AES_BLOCK_SIZE*2

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
      const SecureBinaryData& extraEntropy = SecureBinaryData());
};

////////////////////////////////////////////////////////////////////////////////
class PRNG_Fortuna
{
   /*
   Not gonna bother with the entropy pooling, the crypto lib already offers
   a PRNG.

   This is an extra layer for applications that need a lot of rng but aren't 
   critical to safety. It's also useful for RNG pulls that are presented to the
   outside world, like session IDs, as it won't leak bytes directly from our
   entropy source (the crypto lib PRNG).

   Use the crypto lib's PRNG directly to generate wallet seeds instead.
   */
private:
   mutable std::shared_ptr<SecureBinaryData> key_;
   mutable std::atomic<unsigned> counter_ = { 1 };
   mutable std::atomic<unsigned> nBytes_;

private:
   PRNG_Fortuna(const PRNG_Fortuna&) = delete; // no copies
   PRNG_Fortuna(PRNG_Fortuna&&) = delete;
   void reseed(void) const;

public:
   PRNG_Fortuna(void);

   SecureBinaryData generateRandom(uint32_t numBytes, 
      const SecureBinaryData& extraEntropy = SecureBinaryData()) const;
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
   void computeKdfParams(double targetComputeSec=0.25,
      uint32_t maxMemReqtsBytes=DEFAULT_KDF_MAX_MEMORY,
      bool verbose = false);

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


/*
Serialize a pubkey object into a serialized byte sequence.
 *
 *  Returns: 1 always.
 *  Args:   ctx:        a secp256k1 context object.
 *  Out:    output:     a pointer to a 65-byte (if compressed==0) or 33-byte (if
 *                      compressed==1) byte array to place the serialized key
 *                      in.
 *  In/Out: outputlen:  a pointer to an integer which is initially set to the
 *                      size of output, and is overwritten with the written
 *                      size.
 *  In:     pubkey:     a pointer to a secp256k1_pubkey containing an
 *                      initialized public key.
 *          flags:      SECP256K1_EC_COMPRESSED if serialization should be in
 *                      compressed format, otherwise SECP256K1_EC_UNCOMPRESSED.
 *
SECP256K1_API int secp256k1_ec_pubkey_serialize(
    const secp256k1_context* ctx,
    unsigned char *output,
    size_t *outputlen,
    const secp256k1_pubkey* pubkey,
    unsigned int flags
) SECP256K1_ARG_NONNULL(1) SECP256K1_ARG_NONNULL(2) SECP256K1_ARG_NONNULL(3) SECP256K1_ARG_NONNULL(4);
*/


////////////////////////////////////////////////////////////////////////////////
// Create a C++ interface to the Crypto++ ECDSA ops:  should be more secure
// and much faster than the pure-python methods created by Lis
//
// These methods might as well just be static methods, but SWIG doesn't like
// static methods.  So we will invoke these via CryptoECDSA().Function()
class CryptoECDSA
{
private:
   static const std::string bitcoinMessageMagic_;

public:
   CryptoECDSA(void) {}

   ////////////////////////////////////////////////////////////////////////////
   static void setupContext(void);
   static void shutdown(void);

   /////////////////////////////////////////////////////////////////////////////
   static bool checkPrivKeyIsValid(const SecureBinaryData& privKey);

   /////////////////////////////////////////////////////////////////////////////
   static SecureBinaryData createNewPrivateKey(
      SecureBinaryData extraEntropy = SecureBinaryData())
   {
      while(true)
      {
         auto&& privKey = CryptoPRNG::generateRandom(32, extraEntropy);
         if (checkPrivKeyIsValid(privKey))
            return privKey;
      }
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
   static SecureBinaryData SignData(BinaryData const & binToSign, 
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
   static btc_pubkey CompressPoint(btc_pubkey const & pubKey65);
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

   /////////////////////////////////////////////////////////////////////////////
   // takes unhashed, unprefixed message
   static BinaryData SignBitcoinMessage(
      const BinaryDataRef& msg, 
      const SecureBinaryData& privKey, 
      bool compressedPubKey);
   
   static BinaryData VerifyBitcoinMessage(
      const BinaryDataRef& msg,
      const BinaryDataRef& sig);
};

#endif
