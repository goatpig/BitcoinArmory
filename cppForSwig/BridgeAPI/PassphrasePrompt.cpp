////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2024, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/Bridge.capnp.h"

#include "log.h"
#include "PassphrasePrompt.h"


using namespace Armory;
using namespace Armory::Bridge;

uint32_t BridgePassphrasePrompt::referenceCounter_ = 1;

////////////////////////////////////////////////////////////////////////////////
////
////  BridgePassphrasePrompt
////
////////////////////////////////////////////////////////////////////////////////
BridgePassphrasePrompt::BridgePassphrasePrompt(const std::string& id,
   std::function<void(ServerPushWrapper)> func) :
   promptId_(id), writeFunc_(move(func))
{}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData BridgePassphrasePrompt::processFeedRequest(
   const std::set<Wallets::EncryptionKeyId>& ids)
{
   if (ids.empty()) {
      //exit condition
      cleanup();
      return {};
   }

   //cycle the promise & future
   auto promPtr = std::make_shared<std::promise<SecureBinaryData>>();
   auto fut = promPtr->get_future();
   auto refId = referenceCounter_++;

   //create payload
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto notif = fromBridge.initNotification();
   notif.setCallbackId(promptId_);
   notif.setCounter(refId);

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
   auto replyHandler = [promPtr](bool success, SecureBinaryData& passphrase)->bool
   {
      if (!success) {
         promPtr->set_exception(std::make_exception_ptr(
            std::runtime_error("unsuccessful reply")));
      }
      promPtr->set_value(std::move(passphrase));
      return true;
   };

   //push over socket
   ServerPushWrapper wrapper{ refId, replyHandler, std::move(serialized) };
   writeFunc_(std::move(wrapper));

   //wait on future
   try {
      return fut.get();
   }
   catch (const std::exception&) {
      LOGINFO << "cancelled wallet unlock";
      return {};
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
   writeFunc_(ServerPushWrapper{0, nullptr, std::move(serialized)});
}

////////////////////////////////////////////////////////////////////////////////
PassphraseLambda BridgePassphrasePrompt::getLambda()
{
   return [this](const std::set<Wallets::EncryptionKeyId>& ids)->SecureBinaryData
   {
      return processFeedRequest(ids);
   };
}
