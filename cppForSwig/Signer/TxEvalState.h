////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_TXEVALSTATE
#define _H_TXEVALSTATE

#include <map>
#include "BinaryData.h"

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
         bool validStack_ = false;

         unsigned n_ = 0;

         /*
         Fail all sigs count by setting m_ to UINT32_MAX. This guarantees
         sig checks can fail prior to setting m_ and still evaluate as
         failures (otherwise, any sig count >= m_ when m_ is 0 if unset).
         */
         unsigned m_ = UINT32_MAX;

         std::map<BinaryData, bool> pubKeyState_;

         mutable PubKeyType keyType_ = PubKeyType::Unkonwn;

      private:
         PubKeyType getType(void) const;

      public:
         bool isValid(void) const;
         unsigned getSigCount(void) const;
         bool isSignedForPubKey(const BinaryData& pubkey) const;
         const std::map<BinaryData, bool>& getPubKeyMap(void) const
         { return pubKeyState_; }

         unsigned getM(void) const { return m_; }
         unsigned getN(void) const { return n_; }
      };

      //////////////////////////////////////////////////////////////////////////
      class TxEvalState
      {
      private:
         std::map<unsigned, TxInEvalState> evalMap_;

      public:
         size_t getEvalMapSize(void) const { return evalMap_.size(); }
         void reset(void) { evalMap_.clear(); }
         void updateState(unsigned id, TxInEvalState state);
         bool isValid(void) const;
         const TxInEvalState& getSignedStateForInput(unsigned i) const;
      };
   }; //namespace Signer
}; //namespace Armory

#endif