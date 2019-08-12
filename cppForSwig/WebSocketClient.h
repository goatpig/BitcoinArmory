////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include <atomic>
#include <future>
#include <string>
#include <thread>

#include "libwebsockets.h"
#include "ThreadSafeClasses.h"
#include "BinaryData.h"
#include "SocketObject.h"
#include "WebSocketMessage.h"
#include "BlockDataManagerConfig.h"
#include "ClientClasses.h"
#include "AsyncClient.h"

#include "BIP150_151.h"
#include "AuthorizedPeers.h"

#define CLIENT_AUTH_PEER_FILENAME "client.peers"

////////////////////////////////////////////////////////////////////////////////
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

////////////////////////////////////////////////////////////////////////////////
enum client_protocols {
   PROTOCOL_ARMORY_CLIENT,

   /* always last */
   CLIENT_PROTOCOL_COUNT
};

struct per_session_data__client {
   static const unsigned rcv_size = 8000;
};

namespace SwigClient
{
   class PythonCallback;
}

////////////////////////////////////////////////////////////////////////////////
struct ClientPartialMessage
{
private:
   int counter_ = 0;

public:
   std::map<int, BinaryData> packets_;
   WebSocketMessagePartial message_;

   void reset(void) 
   {
      packets_.clear();
      message_.reset();
   }

   BinaryDataRef insertDataAndGetRef(BinaryData& data)
   {
      auto&& data_pair = std::make_pair(counter_++, std::move(data));
      auto iter = packets_.insert(std::move(data_pair));
      return iter.first->second.getRef();
   }

   void eraseLast(void)
   {
      if (counter_ == 0)
         return;

      packets_.erase(counter_--);
   }
};

////////////////////////////////////////////////////////////////////////////////
class WebSocketClient : public SocketPrototype
{
private:
   std::atomic<void*> wsiPtr_;

   std::atomic<unsigned> requestID_;
   std::atomic<bool> connected_ = { false };

   Queue<SerializedMessage> writeQueue_;
   SerializedMessage currentWriteMessage_;

   //AEAD requires messages to be sent in order of encryption, since the 
   //sequence number is the IV. Push all messages to a queue for serialization,
   //to guarantee payloads are queued for writing in the order they were encrypted
   BlockingQueue<std::unique_ptr<Socket_WritePayload>> writeSerializationQueue_;

   BlockingQueue<BinaryData> readQueue_;
   std::atomic<unsigned> run_ = { 1 };
   std::thread serviceThr_, readThr_, writeThr_;
   TransactionalMap<uint64_t, std::shared_ptr<WriteAndReadPacket>> readPackets_;
   std::shared_ptr<RemoteCallback> callbackPtr_ = nullptr;
   
   ClientPartialMessage currentReadMessage_;
   std::promise<bool> connectionReadyProm_;

   std::shared_ptr<BIP151Connection> bip151Connection_;
   std::chrono::time_point<std::chrono::system_clock> outKeyTimePoint_;
   unsigned outerRekeyCount_ = 0;
   unsigned innerRekeyCount_ = 0;

   std::shared_ptr<AuthorizedPeers> authPeers_;
   BinaryData leftOverData_;

   std::shared_ptr<std::promise<bool>> serverPubkeyProm_;
   std::function<bool(const BinaryData&, const std::string&)> userPromptLambda_;

public:
   std::atomic<int> count_;

private:
   struct lws_context* init();
   void readService(void);
   void writeService(void);
   void service(lws_context*);
   bool processAEADHandshake(const WebSocketMessagePartial&);
   AuthPeersLambdas getAuthPeerLambda(void) const;
   void promptUser(const BinaryDataRef&, const std::string&);

public:
   WebSocketClient(const std::string& addr, const std::string& port,
      const std::string& datadir, const bool& ephemeralPeers,
      const bool& overrideBIP150AuthMode, const bool& newBIP150AuthMode,
      std::shared_ptr<RemoteCallback> cbPtr);

   ~WebSocketClient()
   {
      shutdown();

      if (serviceThr_.joinable())
         serviceThr_.join();
   }

   //locals
   void shutdown(void);   
   void cleanUp(void);
   std::pair<unsigned, unsigned> 
      getRekeyCount(void) const { return std::make_pair(outerRekeyCount_, innerRekeyCount_); }
   void addPublicKey(const SecureBinaryData&);
   void setPubkeyPromptLambda(std::function<bool(const BinaryData&, const std::string&)>);

   //virtuals
   SocketType type(void) const { return SocketWS; }
   void pushPayload(
      std::unique_ptr<Socket_WritePayload>,
      std::shared_ptr<Socket_ReadPayload>);
   bool connectToRemote(void);

   static int callback(
      struct lws *wsi, enum lws_callback_reasons reason, 
      void *user, void *in, size_t len);
};

#endif
