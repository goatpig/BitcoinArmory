////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////


#include "PassphrasePrompt.h"
#include "Utils/log.h"
#include "Wallets/Seeds/Backups.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/Bridge.capnp.h"

using namespace Armory;
using namespace Armory::Bridge;
using namespace std::chrono_literals;

uint32_t BridgePassphrasePrompt::replyIdCounter_ = 1;

////////////////////////////////////////////////////////////////////////////////
////
////  BridgePassphrasePrompt
////
////////////////////////////////////////////////////////////////////////////////
BridgePassphrasePrompt::BridgePassphrasePrompt(const std::string& id,
   std::function<void(BinaryData, PromptReplyWrapper)> func) :
   promptId_(id), promptFunc_(move(func))
{}

////////////////////////////////////////////////////////////////////////////////
Seeds::PromptReply BridgePassphrasePrompt::processFeedRequest(
   const std::set<Wallets::EncryptionKeyId>& ids)
{
   if (ids.empty()) {
      //exit condition
      cleanup();
      return {false, false, {}};
   }

   //cycle the promise & future
   auto promPtr = std::make_shared<std::promise<Seeds::PromptReply>>();
   auto fut = promPtr->get_future();
   auto replyId = replyIdCounter_++;

   //create payload
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto notif = fromBridge.initNotification();
   notif.setCallbackId(promptId_);
   notif.setCounter(replyId);

   auto unlockRequest = notif.initUnlockRequest(ids.size());
   unsigned i=0;
   for (const auto& id : ids) {
      auto idHex = id.toHexStr();
      unlockRequest.set(i++, idHex);
   }

   //serialize it
   auto flat = capnp::messageToFlatArray(message);
   auto bytes = flat.asBytes();
   BinaryData serialized(bytes.begin(), bytes.end());

   //reply handler
   auto replyHandler = [promPtr](const Seeds::PromptReply& reply)->bool
   {
      if (!reply.success) {
         promPtr->set_exception(std::make_exception_ptr(
            std::runtime_error("unsuccessful reply")));
         return true;
      }
      promPtr->set_value(std::move(reply));
      return true;
   };

   //prompt client
   promptFunc_(
      std::move(serialized),
      PromptReplyWrapper{replyId, replyHandler}
   );

   //wait on future
   try {
      return fut.get();
   } catch (const std::exception&) {
      LOGINFO << "cancelled wallet unlock";
      return {false, false, {}};
   }
}

////////////////////////////////////////////////////////////////////////////////
void BridgePassphrasePrompt::cleanup()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto notif = fromBridge.initNotification();
   notif.setCallbackId(promptId_);
   notif.setCleanup();

   auto flat = capnp::messageToFlatArray(message);
   auto bytes = flat.asBytes();
   BinaryData serialized(bytes.begin(), bytes.end());
   promptFunc_(std::move(serialized), PromptReplyWrapper{0, nullptr});
}

////////////////////////////////////////////////////////////////////////////////
Passphrase::UnlockFunc BridgePassphrasePrompt::getLambda()
{
   return [this](const std::set<Wallets::EncryptionKeyId>& ids)
   ->Passphrase::Result
   {
      auto reply = processFeedRequest(ids);
      if (reply.passParams.type != Passphrase::Params::Type::Unlock) {
         return { {}, false };
      }
      return { std::move(reply.passParams.passphrase), reply.success };
   };
}
