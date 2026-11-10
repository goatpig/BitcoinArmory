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

/*
Some client requests require the bridge to interact with the user. Such
requests come with a promptId attached.

When the bridge has to send a message to the client as part of an interactive
request, it is called a prompt. A prompt comes with its promptId attached, so
that the client can tie it to the originating request.

All prompts expect a reply, otherwise it is simply a notification.
A prompt comes with a replyId, which the client returns in the reply, for the
bridge to try the reply to its prompt.
*/

namespace Armory
{
   namespace Bridge
   {
      using PromptId = std::string;
      using PromptReplyHandler = std::function<bool(const Passphrase::PromptReply&)>;

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
         Passphrase::PromptReply processFeedRequest(
            const std::set<Wallets::EncryptionKeyId>&);

      public:
         BridgePassphrasePrompt(const PromptId&,
            std::function<void(BinaryData, PromptReplyWrapper)>);

         Passphrase::UnlockFunc getLambda(void);
         void cleanup(void);
      };
   }; //namespace Bridge
}; //namespace Armory

#endif