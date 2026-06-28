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

////////////////////////////////////////////////////////////////////////////////
//
// ECDSA, SHA, AES and PRNG from libbtc
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

#pragma once

#include <memory>
#include <vector>
#include <set>

#include "SecureBinaryData.h"

#define CRYPTO_DEBUG false

class BinaryDataRef;
class SecureBinaryData;

struct secp256k1_context_struct;
struct btc_pubkey_;

namespace Cryptography
{
   namespace Hash
   {
      void getHash256(BinaryDataRef, uint8_t*);
      void getSha256(BinaryDataRef, uint8_t*);
      void getHMAC256(BinaryDataRef, BinaryDataRef, uint8_t*);

      void getSha512(BinaryDataRef, uint8_t*);
      void getHMAC512(BinaryDataRef, BinaryDataRef, uint8_t*);

      void getHash160(BinaryDataRef, uint8_t*);
   }

   namespace PRNG
   {
      //cryptographic strength PRNG
      SecureBinaryData generateRandomStrong(uint32_t,
         const SecureBinaryData& = {});

      class Fortuna
      {
         /*
         Not gonna bother with the entropy pooling, the crypto lib
         already offers a PRNG.

         This is an extra layer for applications that need a lot of rng
         but aren't critical for safety. It's also useful for RNG pulls that
         are presented to the outside world, like session IDs, as it won't
         leak bytes directly from our entropy source (the crypto lib PRNG).

         Use the crypto lib's PRNG directly to generate wallet seeds instead.
         */
      private:
         mutable std::shared_ptr<SecureBinaryData> key_;
         mutable std::atomic<unsigned> counter_;
         mutable std::atomic<unsigned> nBytes_;

      private:
         Fortuna(const Fortuna&) = delete; // no copies
         Fortuna(Fortuna&&) = delete;
         void reseed(void) const;

      public:
         Fortuna(void);

         SecureBinaryData generateRandom(uint32_t,
            const SecureBinaryData& = {}) const;
      };

      extern const Fortuna fortuna;
   }


   namespace Encryption
   {
      namespace AES
      {
         extern const size_t BLOCK_SIZE;

         SecureBinaryData encryptCFB(
            const SecureBinaryData&, //data
            const SecureBinaryData&, //key
            const SecureBinaryData&  //iv
         );

         SecureBinaryData decryptCFB(
            const SecureBinaryData&,
            const SecureBinaryData&,
            const SecureBinaryData&
         );

         SecureBinaryData encryptCBC(
            const SecureBinaryData&,
            const SecureBinaryData&,
            const SecureBinaryData&
         );

         SecureBinaryData decryptCBC(
            const SecureBinaryData&,
            const SecureBinaryData&,
            const SecureBinaryData&
         );
      };
   }

   namespace ECDSA
   {
      extern const std::string_view bitcoinMessageMagic;
      extern secp256k1_context_struct* crypto_ecdsa_ctx;

      void setupContext(void);
      void shutdown(void);

      //////////////////////////////////////////////////////////////////////////
      bool checkPrivKeyIsValid(const SecureBinaryData&);
      SecureBinaryData createNewPrivateKey(
         const SecureBinaryData& = {});
      bool checkPubPrivKeyMatch(const SecureBinaryData&,
         const SecureBinaryData&);
      SecureBinaryData computeChainedPrivateKey(
         const SecureBinaryData&, const SecureBinaryData&);
      SecureBinaryData privKeyScalarMultiply(
         const SecureBinaryData&, const SecureBinaryData&);

      //////////////////////////////////////////////////////////////////////////
      SecureBinaryData computePublicKey(BinaryDataRef, bool=false);
      bool verifyPublicKeyValid(BinaryDataRef);
      SecureBinaryData computeChainedPublicKey(BinaryDataRef, BinaryDataRef);

      bool verifyPoint(const BinaryData&, const BinaryData&);
      SecureBinaryData compressPoint(BinaryDataRef);
      btc_pubkey_ compressPoint(const btc_pubkey_&);
      SecureBinaryData uncompressPoint(BinaryDataRef);
      SecureBinaryData pubKeyScalarMultiply(BinaryDataRef, BinaryDataRef);

      //////////////////////////////////////////////////////////////////////////
      // For signing and verification, pass in original, UN-HASHED binary string.
      // Always uses deterministic k-value (RFC 6979).
      SecureBinaryData signData(const BinaryData&, const SecureBinaryData&);
      bool verifyData(const BinaryData&, const BinaryData&, const BinaryData&);

      //////////////////////////////////////////////////////////////////////////
      // takes unhashed, unprefixed message
      BinaryData signBitcoinMessage(const BinaryDataRef&,
         const SecureBinaryData&, bool);
      BinaryData verifyBitcoinMessage(const BinaryDataRef&,
         const BinaryDataRef&);
   }
}
