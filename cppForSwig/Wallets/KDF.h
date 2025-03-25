////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string_view>
#include <chrono>
#include "SecureBinaryData.h"
#include "WalletIdTypes.h"

// We will look for a high memory value to use in the KDF
// But as a safety check, we should probably put a cap
// on how much memory the KDF can use -- 32 MB is good
// If a KDF uses 32 MB of memory, it is undeniably easier
// to compute on a CPU than a GPU.
#define DEFAULT_KDF_MAX_MEMORY 32*1024*1024

namespace Armory
{
   namespace Wallets
   {
      namespace Encryption
      {
         ///////////////////////////////////////////////////////////////////////
         // A memory-bound key-derivation function -- uses a variation of Colin
         // Percival's ROMix algorithm: http://www.tarsnap.com/scrypt/scrypt.pdf
         //
         // The computeKdfParams method takes in a target time T, for
         // computation on the computer executing the test. The final KDF should
         // take somewhere between T/2 and T seconds.
         class KdfRomix
         {
         private:
            uint32_t memoryReqtBytes_;
            uint32_t sequenceCount_;
            SecureBinaryData lookupTable_;

            // prob not necessary amidst numIter, memReqts
            // but I guess it can't hurt
            SecureBinaryData salt_;

            // We set the ROMIX params for a given memory
            // req't. Then run it numIter times to meet
            // the computation-time req't
            uint32_t numIterations_;

         public:
            KdfRomix(void);
            KdfRomix(uint32_t, uint32_t, const SecureBinaryData&);

            void computeKdfParams(const std::chrono::milliseconds&,
               uint32_t maxMemReqtsBytes=DEFAULT_KDF_MAX_MEMORY,
               bool verbose=false);
            void usePrecomputedKdfParams(uint32_t, uint32_t,
               const SecureBinaryData&);
            void printKdfParams(void);

            SecureBinaryData DeriveKey_OneIter(const SecureBinaryData&);
            SecureBinaryData DeriveKey(const SecureBinaryData&);

            uint32_t getMemoryReqtBytes(void) const;
            uint32_t getNumIterations(void) const;
            const SecureBinaryData& getSalt(void) const;
         };

         ///////////////////////////////////////////////////////////////////////
         using namespace std::string_view_literals;
         constexpr std::string_view passthroughKdfId = "PASSTHROUGH_SENTINEL"sv;

         ////////
         class KeyDerivationFunction
         {
         public:
            KeyDerivationFunction(void);
            virtual ~KeyDerivationFunction(void) = 0;

            virtual SecureBinaryData deriveKey(
               const SecureBinaryData& rawKey) const = 0;
            virtual bool isSame(const KeyDerivationFunction*) const = 0;
            bool operator<(const KeyDerivationFunction& rhs);

            virtual const KdfId& getId(void) const = 0;
            virtual BinaryData serialize(void) const = 0;
            static std::shared_ptr<KeyDerivationFunction>
               deserialize(const BinaryDataRef&);
         };

         ////////
         class KeyDerivationFunction_Romix : public KeyDerivationFunction
         {
         private:
            mutable KdfId id_;
            unsigned iterations_;
            unsigned memTarget_;

            //NOTE: consider cycling salt per kdf, even though this is likely unnecessary
            const BinaryData salt_;

         private:
            KdfId computeID(void) const;
            BinaryData initialize(const std::chrono::milliseconds&);

         public:
            KeyDerivationFunction_Romix(const std::chrono::milliseconds&);
            KeyDerivationFunction_Romix(unsigned, unsigned, SecureBinaryData);
            ~KeyDerivationFunction_Romix(void) override;

            //overrides
            SecureBinaryData deriveKey(const SecureBinaryData&) const override;
            bool isSame(const KeyDerivationFunction*) const override;
            BinaryData serialize(void) const override;
            const KdfId& getId(void) const override;

            //locals
            unsigned memTarget(void) const;
            unsigned iterations(void) const;
            void prettyPrint(void) const;
         };

         ////////
         class KeyDerivationFunction_Passthrough : public KeyDerivationFunction
         {
            const KdfId id_;

         public:
            KeyDerivationFunction_Passthrough(void);
            ~KeyDerivationFunction_Passthrough(void) override;

            //overrides
            SecureBinaryData deriveKey(const SecureBinaryData&) const override;
            bool isSame(const KeyDerivationFunction*) const override;
            BinaryData serialize(void) const override;
            const KdfId& getId(void) const override;
         };
      } //namespace Encryption
   } //namespace Wallets
} //namespace Armory
