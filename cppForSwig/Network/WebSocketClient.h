////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <atomic>
#include <future>
#include <string>
#include <thread>
#include <filesystem>

#include "libwebsockets.h"
#include "Utils/ThreadSafeClasses.h"
#include "SocketObject.h"
#include "WebSocketMessage.h"

#define CLIENT_AUTH_PEER_FILENAME "client.peers"

class RemoteCallback;
class SecureBinaryData;

namespace Armory
{
   namespace Wallets
   {
      namespace IO
      {
         struct ReadOnlyFileParams;
      }
   }

   namespace NetworkPeers
   {
      class ClientStore;
   }

   namespace Network
   {
      struct WriteAndReadPacket
      {
         const unsigned id_;
         std::vector<BinaryData> packets_;
         std::unique_ptr<WebSocketMessagePartial> partialMessage_ = nullptr;
         std::shared_ptr<Socket_ReadPayload> payload_;

         WriteAndReadPacket(unsigned id, std::shared_ptr<Socket_ReadPayload> payload) :
            id_(id), payload_(payload)
         {}

         ~WriteAndReadPacket(void)
         {}
      };

      ////////
      enum client_protocols {
         PROTOCOL_ARMORY_CLIENT,

         /* always last */
         CLIENT_PROTOCOL_COUNT
      };

      struct per_session_data__client {
         static const unsigned rcv_size = 8000;
      };

      ////////
      class WSClientWriteQueue
      {
      private:
         struct lws_context* contextPtr_;
         Threading::Queue<SerializedMessage> writeQueue_;

      public:
         WSClientWriteQueue(struct lws_context* contextPtr) :
            contextPtr_(contextPtr)
         {}

         void push_back(SerializedMessage&);
         SerializedMessage pop_front(void);
         bool empty(void) const;
      };

      ////////
      class WebSocketClient : public SocketPrototype
      {
      private:
         std::atomic<void*> wsiPtr_;
         std::atomic<void*> contextPtr_;
         const std::string servName_;
         std::atomic<bool> connected_ = { false };

         std::unique_ptr<WSClientWriteQueue> writeQueue_;
         SerializedMessage currentWriteMessage_;

         //AEAD requires messages to be sent in order of encryption, since the 
         //sequence number is the IV. Push all messages to a queue for serialization,
         //to guarantee payloads are queued for writing in the order they were encrypted
         Threading::BlockingQueue<
            std::unique_ptr<Socket_WritePayload>> writeSerializationQueue_;

         std::atomic<unsigned> run_ = { 1 };
         std::thread serviceThr_, readThr_, writeThr_;

         Threading::BlockingQueue<BinaryData> readQueue_;
         Threading::TransactionalMap<
            uint64_t, std::shared_ptr<WriteAndReadPacket>> readPackets_;

         std::shared_ptr<RemoteCallback> callbackPtr_ = nullptr;
         
         WebSocketMessagePartial currentReadMessage_;
         std::promise<bool> connectionReadyProm_;

         std::shared_ptr<BIP151Connection> bip151Connection_;
         std::chrono::time_point<std::chrono::system_clock> outKeyTimePoint_;
         unsigned outerRekeyCount_ = 0;
         unsigned innerRekeyCount_ = 0;

         std::shared_ptr<NetworkPeers::ClientStore> peerStore_;
         BinaryData leftOverData_;

         std::shared_ptr<std::promise<bool>> serverPubkeyProm_;
         std::function<bool(const BinaryData&)> userPromptLambda_;

      public:
         std::atomic<int> count_;
         bool serverPubkeyAnnounce_ = false;

      private:
         struct lws_context* init();
         void readService(void);
         void writeService(void);
         void service(lws_context*);
         bool processAEADHandshake(const WebSocketMessagePartial&);
         void promptUser(const BinaryDataRef&, const std::string&);
         void cleanup(void);

      public:
         WebSocketClient(const std::string& addr, const std::string& port,
            std::shared_ptr<NetworkPeers::ClientStore>, bool,
            std::shared_ptr<RemoteCallback>);
         ~WebSocketClient(void);

         //locals
         void shutdown(void);
         bool running(void) const override;
         std::pair<unsigned, unsigned> getRekeyCount(void) const;
         void addPublicKey(const SecureBinaryData&, bool);
         void setPubkeyPromptLambda(const std::function<bool(const BinaryData&)>&);

         //virtuals
         SocketType type(void) const override;
         void pushPayload(
            std::unique_ptr<Socket_WritePayload>,
            std::shared_ptr<Socket_ReadPayload>) override;
         bool connectToRemote(void) override;

         static int lwsServiceHandler(
            struct lws*, enum lws_callback_reasons,
            void*, void*, size_t);
      };
   } //namespace Network
} //namespace Armory
