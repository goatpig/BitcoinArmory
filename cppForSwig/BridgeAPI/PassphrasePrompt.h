////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _PASSPHRASE_PROMPT_H
#define _PASSPHRASE_PROMPT_H

#include <future>

#include "../Wallets/WalletIdTypes.h"
#include "../Wallets/PassphraseLambda.h"

namespace Armory
{
   namespace Bridge
   {
      using CallbackHandler = std::function<bool(bool, SecureBinaryData&)>;
 
      //////////////////////////////////////////////////////////////////////////
      struct ServerPushWrapper
      {
         const uint32_t referenceId;
         CallbackHandler handler = nullptr;
         BinaryData payload;
      };

      //////////////////////////////////////////////////////////////////////////
      class BridgePassphrasePrompt
      {
         static uint32_t referenceCounter_;

      private:
         const std::string promptId_;
         std::function<void(ServerPushWrapper)> writeFunc_;

      private:
         SecureBinaryData processFeedRequest(
            const std::set<Armory::Wallets::EncryptionKeyId>&);

      public:
         BridgePassphrasePrompt(const std::string&,
            std::function<void(ServerPushWrapper)>);

         PassphraseLambda getLambda();
         void cleanup(void);
      };
   }; //namespace Bridge
}; //namespace Armory

#endif