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
// PassAndParams
PassAndParams::PassAndParams() :
   type(Type::Invalid), unlockMs(0ms), reuseKdf(false)
{}

PassAndParams::PassAndParams(std::chrono::milliseconds target, uint32_t mb,
   SecureBinaryData pass) :
   type(Type::SetNew), unlockMs(target), memTargetMB(mb),
   reuseKdf(false), passphrase{std::move(pass)}
{}

PassAndParams::PassAndParams(SecureBinaryData pass, bool reuseKdf) :
   type(reuseKdf ? Type::SetNew : Type::Unlock),
   unlockMs(0ms), reuseKdf(reuseKdf), passphrase{std::move(pass)}
{}

////////////////////////////////////////////////////////////////////////////////
//
//// SetNew
//
////////////////////////////////////////////////////////////////////////////////
SetNew::SetNew() :
   passAndparams_(std::make_unique<PassAndParams>()),
   setNewPassphrase_(nullptr)
{}

////
SetNew::SetNew(std::chrono::milliseconds kdfUnlockTime,
   uint32_t kdfMemTarget,
   SecureBinaryData passphrase) :
   passAndparams_(std::make_unique<PassAndParams>(
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
   if (rhs.passAndparams_ != nullptr &&
      rhs.passAndparams_->type != PassAndParams::Type::Invalid) {
      passAndparams_ = std::make_unique<PassAndParams>(
         rhs.passAndparams_->unlockMs,
         rhs.passAndparams_->memTargetMB,
         rhs.passAndparams_->passphrase);
   } else if (setNewPassphrase_ == nullptr) {
      passAndparams_ = std::make_unique<PassAndParams>();
   }
}

////////////////////////////////////////////////////////////////////////////////
const PassAndParams& SetNew::get() const
{
   if (passAndparams_ == nullptr) {
      if (setNewPassphrase_ == nullptr) {
         throw PassphraseException("cannot get passphrase");
      }

      passAndparams_ = std::move(setNewPassphrase_());
      if (passAndparams_ == nullptr ||
         passAndparams_->type != PassAndParams::Type::SetNew) {
         throw PassphraseException("passphrase request was rejected");
      }
   }
   return *passAndparams_;
}

////
SetNew SetNew::copy() const
{
   //allows for a copy when the object is setup with
   //a lambda that wasnt called yet
   if (passAndparams_ != nullptr) {
      if (passAndparams_->type == PassAndParams::Type::Invalid) {
         return {};
      }
      throw PassphraseException("this SetNew is not copyable");
   } else if (setNewPassphrase_ == nullptr) {
      throw PassphraseException("this SetNew is not copyable");
   }
   return SetNew(setNewPassphrase_);
}

////
std::unique_ptr<PassAndParams> SetNew::movePassAndParams()
{
   if (passAndparams_->passphrase.empty()) {
      throw PassphraseException("cannot get passphrase");
   }
   return std::move(passAndparams_);
}

////////////////////////////////////////////////////////////////////////////////
UnlockFunc SetNew::getUnlockFunc() const
{
   if (passAndparams_ == nullptr) {
      throw PassphraseException("call get first");
   } else if (passAndparams_->type == PassAndParams::Type::Invalid) {
      return [](const std::set<Wallets::EncryptionKeyId>&)->Result {
         /*
         Typical scenario for reaching this block is to create an encrypted
         container with no password provided, then trying to unlock it.
         In such setup, this lambda will never fire. Otherwise, throw.
         */
         throw std::runtime_error("invalid SetNew");
      };
   }

   return [&pass=passAndparams_->passphrase]
   (const std::set<Wallets::EncryptionKeyId>&)->Result {
      return {pass, true};
   };
}