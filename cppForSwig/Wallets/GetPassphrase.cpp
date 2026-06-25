////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "GetPassphrase.h"
#include <chrono>

using namespace Armory::Passphrase;
using namespace std::chrono_literals;

PassphraseException::PassphraseException(const std::string& err)
   : std::runtime_error(err)
{}

////////////////////////////////////////////////////////////////////////////////
//
//// Params
//
////////////////////////////////////////////////////////////////////////////////
Params::Params() :
   type(Type::Invalid), unlockMs(0ms), reuseKdf(false)
{}

Params::Params(std::chrono::milliseconds target, uint32_t mb,
   SecureBinaryData pass) :
   type(Type::SetNew), unlockMs(target), memTargetMB(mb),
   reuseKdf(false), passphrase{std::move(pass)}
{}

Params::Params(SecureBinaryData pass, bool reuseKdf) :
   type(reuseKdf ? Type::SetNew : Type::Unlock),
   unlockMs(0ms), reuseKdf(reuseKdf), passphrase{std::move(pass)}
{}

////////////////////////////////////////////////////////////////////////////////
//
//// SetNew
//
////////////////////////////////////////////////////////////////////////////////
SetNew::SetNew() :
   params_(std::make_unique<Params>()),
   setNewPassphrase_(nullptr)
{}

////
SetNew::SetNew(std::chrono::milliseconds kdfUnlockTime,
   uint32_t kdfMemTarget,
   SecureBinaryData passphrase) :
   params_(std::make_unique<Params>(
      kdfUnlockTime, kdfMemTarget, std::move(passphrase))),
   setNewPassphrase_(nullptr)
{}

////
SetNew::SetNew(const SetNewPassFunc& func) :
   setNewPassphrase_(func)
{}

////
SetNew::SetNew(const SetNew& rhs) :
   setNewPassphrase_(rhs.setNewPassphrase_)
{
   if (rhs.params_ != nullptr && rhs.params_->type != Params::Type::Invalid) {
      params_ = std::make_unique<Params>(
         rhs.params_->unlockMs,
         rhs.params_->memTargetMB,
         rhs.params_->passphrase);
   } else if (setNewPassphrase_ == nullptr) {
      params_ = std::make_unique<Params>();
   }
}

////////////////////////////////////////////////////////////////////////////////
const Params& SetNew::get() const
{
   if (params_ == nullptr) {
      if (setNewPassphrase_ == nullptr) {
         throw PassphraseException("cannot get passphrase");
      }

      params_ = std::move(setNewPassphrase_());
      if (params_ == nullptr || params_->type != Params::Type::SetNew) {
         throw PassphraseException("passphrase request was rejected");
      }
   }
   return *params_;
}

////
SetNew SetNew::copy() const
{
   //allows for a copy when the object is setup with
   //a lambda that wasnt called yet
   if (params_ != nullptr) {
      if (params_->type == Params::Type::Invalid) {
         return {};
      }
      throw PassphraseException("this SetNew is not copyable");
   } else if (setNewPassphrase_ == nullptr) {
      throw PassphraseException("this SetNew is not copyable");
   }
   return SetNew(setNewPassphrase_);
}

////
std::unique_ptr<Params> SetNew::moveParams()
{
   if (params_->passphrase.empty()) {
      throw PassphraseException("cannot get passphrase");
   }
   return std::move(params_);
}

////////////////////////////////////////////////////////////////////////////////
UnlockFunc SetNew::getUnlockFunc() const
{
   if (params_ == nullptr) {
      throw PassphraseException("call get first");
   } else if (params_->type == Params::Type::Invalid) {
      return [](const std::set<Armory::Wallets::EncryptionKeyId>&)->Result {
         /*
         Typical scenario for reaching this block is to create an encrypted
         container with no password provided, then trying to unlock it.
         In such setup, this lambda will never fire. Otherwise, throw.
         */
         throw std::runtime_error("invalid SetNew");
      };
   }

   return [&pass=params_->passphrase]
   (const std::set<Armory::Wallets::EncryptionKeyId>&)->Result {
      return {pass, true};
   };
}