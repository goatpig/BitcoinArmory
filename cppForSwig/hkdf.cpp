////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <vector>
#include <algorithm>
#include <cmath>
#include <cassert>

#include "hkdf.h"
#include "btc/sha2.h"
#include "BinaryData.h"


// HKDF (RFC 5869) code for SHA-256. Based on libbtc's HMAC SHA-256 code.
//
// IN:  resultSize - Size of the output buffer (max 8160 bytes).
//      salt - HKDF salt. Optional. Pass nullptr if not used.
//      ssize - HKDF salt size. Optional. Pass 0 if not used.
//      key - Initial HKDF key material. Mandatory.
//      ksize - Initial HKDF key material size. Mandatory.
//      info - HKDF context-specific info. Optional. Pass nullptr if not used.
//      isize - HKDF context-specific info size. Optional. Pass 0 if not used.
// OUT: result - The final HKDF keying material.
// RET: None
void hkdf_sha256(uint8_t *result, const size_t &resultSize,
                 const uint8_t *salt, const size_t &ssize,
                 const uint8_t *key, const size_t &ksize,
                 const uint8_t *info, const size_t &isize)
{
   // RFC 5869 only allows for up to 8160 bytes of output data.
   assert(resultSize <= (255 * SHA256_DIGEST_LENGTH));
   assert(resultSize > 0);
   assert(ksize > 0);

   // Write an explicit float value in order to force the ceil() call to round
   // up if necessary.
   float numStepsF = static_cast<float>(resultSize) / \
                     static_cast<float>(SHA256_DIGEST_LENGTH);
   unsigned int numSteps = ceil(numStepsF);
   unsigned int hashInputBytes = isize + 1;
   unsigned int totalHashBytes = numSteps * SHA256_DIGEST_LENGTH;
   unsigned int hashInputSize = SHA256_DIGEST_LENGTH + hashInputBytes;

   BinaryData prk(SHA256_DIGEST_LENGTH);
   BinaryData t(totalHashBytes);
   BinaryData hashInput(hashInputSize);

   // Step 1 (Sect. 2.2) - Extract a pseudorandom key from the salt & key.
   hmac_sha256(salt, ssize, key, ksize, prk.getPtr());

   // Step 2 (Sec. 2.3) - Expand
   std::copy(info, info + isize, hashInput.getPtr());
   hashInput[isize] = 0x01;
   hmac_sha256(prk.getPtr(), prk.getSize(), hashInput.getPtr(), hashInputBytes, t.getPtr());
   hashInputBytes += SHA256_DIGEST_LENGTH;

   // Loop as needed until you have enough output keying material.
   // NB: There appear to be subtle memory allocation gotchas in the HMAC code.
   // If you try to make a buffer serve double duty (output that writes over
   // input), Armory crashes due to C memory deallocation errors. Circumvent
   // with dedicated buffers.
   for(unsigned int i = 1; i < numSteps; ++i)
   {
      BinaryData tmpHashRes(SHA256_DIGEST_LENGTH);
      BinaryData tmpHashInput(hashInputBytes);
      unsigned int resBytes = (i * SHA256_DIGEST_LENGTH);

      std::copy(&t[(i-1)*SHA256_DIGEST_LENGTH],
                &t[i*SHA256_DIGEST_LENGTH],
                &tmpHashInput[0]);
      std::copy(info, info + isize, &tmpHashInput[SHA256_DIGEST_LENGTH]);
      tmpHashInput[-1] = static_cast<uint8_t>(i + 1);
      hmac_sha256(prk.getPtr(),
                  prk.getSize(),
                  tmpHashInput.getPtr(),
                  tmpHashInput.getSize(),
                  tmpHashRes.getPtr());
      std::copy(&tmpHashRes[0], &tmpHashRes[SHA256_DIGEST_LENGTH], &t[resBytes]);
   }

   // Write the final results and exit.
   std::copy(&t[0], &t[resultSize], result);
   return;
}
