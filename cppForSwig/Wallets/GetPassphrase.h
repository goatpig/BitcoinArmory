////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2021-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <functional>
#include <set>
#include <chrono>

#include "Utils/SecureBinaryData.h"

////////////////////////////////////////////////////////////////////////////////
namespace Armory
{
   namespace Wallets
   {
      class EncryptionKeyId;

      namespace Encryption
      {
         struct ClearTextEncryptionKey;
      }
   }

   namespace Passphrase
   {
      class PassphraseException : public std::runtime_error
      {
      public:
         PassphraseException(const std::string&);
      };

      struct Result
      {
         SecureBinaryData passphrase;
         const bool success;
      };

      /* to unlock an encrypted container */
      using UnlockFunc = std::function<Result(
         const std::set<Wallets::EncryptionKeyId>&)>;

      /* to get passphrase and kdf params at encrypted container creation */
      struct PassAndParams
      {
         enum class Type : int
         {
            Invalid,
            Unlock,
            SetNew
         };

         const Type type;
         const std::chrono::milliseconds unlockMs;
         const uint32_t memTargetMB=0;
         const bool reuseKdf;
         SecureBinaryData passphrase{};

         PassAndParams(void);
         PassAndParams(std::chrono::milliseconds, uint32_t,
            SecureBinaryData);
         PassAndParams(SecureBinaryData, bool reuseKdf=false);
      };

      class SetNew
      {
         friend struct Armory::Wallets::Encryption::ClearTextEncryptionKey;

      private:
         using SetNewPassFunc = std::function<std::unique_ptr<PassAndParams>(void)>;

         mutable std::unique_ptr<PassAndParams> passAndparams_;
         const SetNewPassFunc setNewPassphrase_;

      private:
         std::unique_ptr<PassAndParams> movePassAndParams(void);

      public:
         SetNew(void);
         SetNew(std::chrono::milliseconds, uint32_t, SecureBinaryData);
         SetNew(const SetNewPassFunc&);
         SetNew(const SetNew&);

         const PassAndParams& get(void) const;
         SetNew copy(void) const;
         UnlockFunc getUnlockFunc(void) const;
      };

      struct PromptReply
      {
         const bool success;
         const bool merge;

         PassAndParams passAndParams;
      };
   }
}
