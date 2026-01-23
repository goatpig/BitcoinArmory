////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <stdint.h>
#include <map>

class BinaryData;
class BinaryDataRef;

namespace Armory
{
   namespace Signing
   {
      enum class PubKeyType : int
      {
         Compressed = 1,
         Uncompressed,
         Mixed,
         Unkonwn
      };

      //////////////////////////////////////////////////////////////////////////
      class TxInEvalState
      {
         friend class StackInterpreter;

      private:
         mutable PubKeyType keyType_ = PubKeyType::Unkonwn;
         std::map<BinaryData, bool> pubKeyState_;
         bool validStack_ = false;

         /*
         Fail all sigs count by setting m_ to UINT32_MAX. This guarantees
         sig checks can fail prior to setting m_ and still evaluate as
         failures (otherwise, any sig count >= m_ when m_ is 0 if unset).
         */
         unsigned n_ = 0;
         unsigned m_ = UINT32_MAX;

      private:
         PubKeyType getType(void) const;

      public:
         bool isValid(void) const;
         unsigned getSigCount(void) const;
         bool isSignedForPubKey(BinaryDataRef) const;
         const std::map<BinaryData, bool>& getPubKeyMap(void) const;
         unsigned getM(void) const;
         unsigned getN(void) const;
      };

      //////////////////////////////////////////////////////////////////////////
      class TxEvalState
      {
      private:
         std::map<unsigned, TxInEvalState> evalMap_;

      public:
         size_t getEvalMapSize(void) const;
         void reset(void);
         void updateState(unsigned, TxInEvalState);
         bool isValid(void) const;
         const TxInEvalState& getSignedStateForInput(unsigned) const;
      };
   } //namespace Signing
} //namespace Armory
