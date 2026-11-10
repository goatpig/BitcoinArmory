////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _PASSPHRASE_PROMPT_H
#define _PASSPHRASE_PROMPT_H

#include <future>

#include "../Wallets/WalletIdTypes.h"
#include "../Wallets/GetPassphrase.h"

namespace Armory
{
   namespace Seeds
   {
      struct PromptReply;
   }

   namespace Bridge
   {
      using PromptId = std::string;
      using PromptReplyHandler = std::function<bool(const Seeds::PromptReply&)>;

      //////////////////////////////////////////////////////////////////////////
      struct PromptReplyWrapper
      {
         const uint32_t replyId;
         PromptReplyHandler handler = nullptr;
      };

      //////////////////////////////////////////////////////////////////////////
      class BridgePassphrasePrompt
      {
         static uint32_t replyIdCounter_;

      private:
         const std::string promptId_;
         std::function<void(BinaryData, PromptReplyWrapper)> promptFunc_;

      private:
         Seeds::PromptReply processFeedRequest(
            const std::set<Armory::Wallets::EncryptionKeyId>&);

      public:
         BridgePassphrasePrompt(const PromptId&,
            std::function<void(BinaryData, PromptReplyWrapper)>);

         Passphrase::UnlockFunc getLambda(void);
         void cleanup(void);
      };
   }; //namespace Bridge
}; //namespace Armory

#endif