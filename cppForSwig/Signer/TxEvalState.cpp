////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2024, goatpig                                           //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "TxEvalState.h"
#include "EncryptionUtils.h"

using namespace Armory;

////////////////////////////////////////////////////////////////////////////////
bool Signing::TxInEvalState::isValid() const
{
   if (!validStack_) {
      return false;
   }

   unsigned count = 0;
   for (const auto& state : pubKeyState_) {
      if (state.second)
         ++count;
   }

   return count >= m_;
}

////////////////////////////////////////////////////////////////////////////////
unsigned Signing::TxInEvalState::getSigCount() const
{
   unsigned count = 0;
   for (auto& state : pubKeyState_) {
      if (state.second) {
         ++count;
      }
   }

   return count;
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::TxInEvalState::isSignedForPubKey(const BinaryData& pubkey) const
{
   if (pubKeyState_.empty()) {
      return false;
   }

   auto type = getType();
   if (type == PubKeyType::Unkonwn) {
      throw std::runtime_error("can't establish pub key type");
   }

   if ((pubkey.getSize() == 65 && type == PubKeyType::Uncompressed) ||
      (pubkey.getSize() == 33 && type == PubKeyType::Compressed)) {
      auto iter = pubKeyState_.find(pubkey);
      if (iter == pubKeyState_.end())
         return false;

      return iter->second;
   } else if (type != PubKeyType::Mixed) {
      BinaryData modified_key;
      if (type == PubKeyType::Compressed)
         modified_key = CryptoECDSA().CompressPoint(pubkey);
      else if (type == PubKeyType::Uncompressed)
         modified_key = CryptoECDSA().UncompressPoint(pubkey);

      auto iter = pubKeyState_.find(modified_key);
      if (iter == pubKeyState_.end())
         return false;

      return iter->second;
   } else {
      BinaryData modified_key;
      if (type == PubKeyType::Compressed)
         modified_key = CryptoECDSA().CompressPoint(pubkey);
      else if (type == PubKeyType::Uncompressed)
         modified_key = CryptoECDSA().UncompressPoint(pubkey);

      auto iter = pubKeyState_.find(pubkey);
      if (iter == pubKeyState_.end())
      {
         auto iter2 = pubKeyState_.find(modified_key);
         if (iter2 == pubKeyState_.end())
            return false;

         return iter2->second;
      }

      return iter->second;
   }

   return false;
}

////////////////////////////////////////////////////////////////////////////////
Signing::PubKeyType Signing::TxInEvalState::getType() const
{
   if (keyType_ != PubKeyType::Unkonwn) {
      return keyType_;
   }

   bool isCompressed = false;
   bool isUncompressed = false;

   for (const auto& key : pubKeyState_) {
      if (key.first.getSize() == 65)
         isUncompressed = true;
      else if (key.first.getSize() == 33)
         isCompressed = true;
   }

   if (isCompressed && isUncompressed) {
      keyType_ = PubKeyType::Mixed;
   } else if (isCompressed) {
      keyType_ = PubKeyType::Compressed;
   } else if (isUncompressed) {
      keyType_ = PubKeyType::Uncompressed;
   }

   return keyType_;
}

////////////////////////////////////////////////////////////////////////////////
void Signing::TxEvalState::updateState(unsigned id, TxInEvalState state)
{
   evalMap_.insert(std::make_pair(id, state));
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::TxEvalState::isValid() const
{
   for (const auto& state : evalMap_) {
      if (!state.second.isValid())
         return false;
   }

   return true;
}

////////////////////////////////////////////////////////////////////////////////
const Signing::TxInEvalState& Signing::TxEvalState::getSignedStateForInput(
   unsigned i) const
{
   auto iter = evalMap_.find(i);
   if (iter == evalMap_.end()) {
      throw std::range_error("invalid input index");
   }

   return iter->second;
}



