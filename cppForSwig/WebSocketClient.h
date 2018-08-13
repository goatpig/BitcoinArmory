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

using namespace std;

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
   atomic<void*> contextPtr_;
   unique_ptr<promise<bool>> ctorProm_ = nullptr;

   atomic<int> shutdownCount_;

   Queue<WebSocketMessage> writeQueue_;
   WebSocketMessage currentWriteMessage_;

   BlockingQueue<BinaryData> readQueue_;
   shared_ptr<atomic<unsigned>> run_;
   thread serviceThr_, readThr_;
   TransactionalMap<uint64_t, shared_ptr<WriteAndReadPacket>> readPackets_;
   RemoteCallback* callbackPtr_ = nullptr;
   
   static TransactionalMap<
      struct lws*, shared_ptr<WebSocketClient>> objectMap_; 

   ClientPartialMessage currentReadMessage_;

private:
   WebSocketClient(const string& addr, const string& port) :
      SocketPrototype(addr, port, false)
   {
      shutdownCount_.store(0, memory_order_relaxed); 

      wsiPtr_.store(nullptr, memory_order_relaxed);
      contextPtr_.store(nullptr, memory_order_relaxed);
      run_ = make_shared<atomic<unsigned>>();

      count_.store(0, memory_order_relaxed);
      init();
   }

   void init();
   void setIsReady(bool);
   void readService(void);
   static void service(
      shared_ptr<atomic<unsigned>>, struct lws*, struct lws_context*);

public:
   atomic<int> count_;

public:
   ~WebSocketClient()
   {
      auto lwsPtr = (struct lws*)wsiPtr_.load(memory_order_relaxed);
      if(lwsPtr != nullptr)
         destroyInstance(lwsPtr);
   }

   //locals
   void shutdown(void);   
   void setCallback(RemoteCallback*);

   //virtuals
   SocketType type(void) const { return SocketWS; }
   void pushPayload(
      unique_ptr<Socket_WritePayload>,
      shared_ptr<Socket_ReadPayload>);
   bool connectToRemote(void);

   //statics
   static shared_ptr<WebSocketClient> getNew(
      const string& addr, const string& port);

   static int callback(
      struct lws *wsi, enum lws_callback_reasons reason, 
      void *user, void *in, size_t len);

   static shared_ptr<WebSocketClient> getInstance(struct lws* ptr);
   static void destroyInstance(struct lws* ptr);
};

#endif
