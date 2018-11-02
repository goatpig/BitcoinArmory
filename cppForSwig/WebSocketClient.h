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

////////////////////////////////////////////////////////////////////////////////
struct WriteAndReadPacket
{
   const unsigned id_;
   vector<BinaryData> packets_;
   unique_ptr<WebSocketMessagePartial> partialMessage_ = nullptr;
   shared_ptr<Socket_ReadPayload> payload_;

   WriteAndReadPacket(unsigned id, shared_ptr<Socket_ReadPayload> payload) :
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
   map<int, BinaryData> packets_;
   WebSocketMessagePartial message_;

   void reset(void) 
   {
      packets_.clear();
      message_.reset();
   }

   BinaryDataRef insertDataAndGetRef(BinaryData& data)
   {
      auto&& data_pair = make_pair(counter_++, move(data));
      auto iter = packets_.insert(move(data_pair));
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
   atomic<void*> wsiPtr_;

   atomic<unsigned> requestID_;
   atomic<bool> connected_ = { false };

   Queue<WebSocketMessage> writeQueue_;
   WebSocketMessage currentWriteMessage_;

   BlockingQueue<BinaryData> readQueue_;
   atomic<unsigned> run_ = { 1 };
   thread serviceThr_, readThr_;
   TransactionalMap<uint64_t, shared_ptr<WriteAndReadPacket>> readPackets_;
   shared_ptr<RemoteCallback> callbackPtr_ = nullptr;
   
   ClientPartialMessage currentReadMessage_;
   promise<bool> connectedProm_;

public:
   atomic<int> count_;

private:
   struct lws_context* init();
   void readService(void);
   void service(lws_context*);


public:
   WebSocketClient(const string& addr, const string& port,
      shared_ptr<RemoteCallback> cbPtr) :
      SocketPrototype(addr, port, false), callbackPtr_(cbPtr)
   {
      count_.store(0, memory_order_relaxed);
      requestID_.store(0, memory_order_relaxed);
   }

   ~WebSocketClient()
   {
      shutdown();

      if (serviceThr_.joinable())
         serviceThr_.join();
   }

   //locals
   void shutdown(void);   
   void cleanUp(void);

   //virtuals
   SocketType type(void) const { return SocketWS; }
   void pushPayload(
      unique_ptr<Socket_WritePayload>,
      shared_ptr<Socket_ReadPayload>);
   bool connectToRemote(void);

   static int callback(
      struct lws *wsi, enum lws_callback_reasons reason, 
      void *user, void *in, size_t len);
};

#endif
