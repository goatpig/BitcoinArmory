////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "WebSocketClient.h"

////////////////////////////////////////////////////////////////////////////////
static struct lws_protocols protocols[] = {
   /* first protocol must always be HTTP handler */

   {
      "armory-bdm-protocol",
      WebSocketClient::callback,
      sizeof(struct per_session_data__client),
      per_session_data__client::rcv_size,
   },

{ NULL, NULL, 0, 0 } /* terminator */
};

////////////////////////////////////////////////////////////////////////////////
void WebSocketClient::pushPayload(
   unique_ptr<Socket_WritePayload> write_payload,
   shared_ptr<Socket_ReadPayload> read_payload)
{
   unsigned id = requestID_.fetch_add(1, memory_order_relaxed);
   if (read_payload != nullptr)
   {
      //create response object
      auto response = make_shared<WriteAndReadPacket>(id, read_payload);

      //set response id
      readPackets_.insert(make_pair(id, move(response)));   
   }

   vector<uint8_t> data;
   write_payload->serialize(data);

   //push packets to write queue
   WebSocketMessage ws_msg;
   ws_msg.construct(id, data);

   writeQueue_.push_back(move(ws_msg));

   //trigger write callback
   auto wsiptr = (struct lws*)wsiPtr_.load(memory_order_relaxed);
   if (wsiptr == nullptr)
      throw LWS_Error("invalid lws instance");
   if (lws_callback_on_writable(wsiptr) < 1)
      throw LWS_Error("invalid lws instance");
}

////////////////////////////////////////////////////////////////////////////////
struct lws_context* WebSocketClient::init()
{
   run_.store(1, memory_order_relaxed);
   currentReadMessage_.reset();

   //setup context
   struct lws_context_creation_info info;
   memset(&info, 0, sizeof info);
   
   info.port = CONTEXT_PORT_NO_LISTEN;
   info.protocols = protocols;
   info.ws_ping_pong_interval = 0;
   info.gid = -1;
   info.uid = -1;

   auto contextptr = lws_create_context(&info);
   if (contextptr == NULL) 
      throw LWS_Error("failed to create LWS context");

   //connect to server
   struct lws_client_connect_info i;
   memset(&i, 0, sizeof(i));
   
   //i.address = ip.c_str();
   int port = stoi(port_);
   if (port == 0)
      port = WEBSOCKET_PORT;
   i.port = port;

   const char *prot, *p;
   char path[300];
   lws_parse_uri((char*)addr_.c_str(), &prot, &i.address, &i.port, &p);

   path[0] = '/';
   lws_strncpy(path + 1, p, sizeof(path) - 1);
   i.path = path;
   i.host = i.address;
   i.origin = i.address;
   i.ietf_version_or_minus_one = -1;

   i.context = contextptr;
   i.method = nullptr;
   i.protocol = protocols[PROTOCOL_ARMORY_CLIENT].name;  
   i.userdata = this;

   struct lws* wsiptr;
   //i.pwsi = &wsiptr;
   wsiptr = lws_client_connect_via_info(&i); 
   wsiPtr_.store(wsiptr, memory_order_release);

   return contextptr;
}

////////////////////////////////////////////////////////////////////////////////
bool WebSocketClient::connectToRemote()
{
   auto connectedFut = connectedProm_.get_future();

   auto serviceLBD = [this](void)->void
   {
      auto contextPtr = init();
      this->service(contextPtr);
   };

   serviceThr_ = thread(serviceLBD);

   auto readLBD = [this](void)->void
   {
      this->readService();
   };

   readThr_ = thread(readLBD);

   connectedFut.get();
   return true;
}

////////////////////////////////////////////////////////////////////////////////
void WebSocketClient::service(lws_context* contextPtr)
{
   int n = 0;
   while(run_.load(memory_order_relaxed) != 0 && n >= 0)
   {
      n = lws_service(contextPtr, 50);
   }

   lws_context_destroy(contextPtr);
   cleanUp();
}

////////////////////////////////////////////////////////////////////////////////
void WebSocketClient::shutdown()
{
   run_.store(0, memory_order_relaxed);
}

////////////////////////////////////////////////////////////////////////////////
void WebSocketClient::cleanUp()
{
   readQueue_.terminate();

   try
   {
      if(readThr_.joinable())
         readThr_.join();
   }
   catch(system_error& e)
   {
      throw e;
   }
   
   readPackets_.clear();
}

////////////////////////////////////////////////////////////////////////////////
int WebSocketClient::callback(struct lws *wsi, 
   enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
   auto instance = (WebSocketClient*)user;

   switch (reason)
   {

   case LWS_CALLBACK_CLIENT_ESTABLISHED:
   {
      //ws connection established with server
      instance->connected_.store(true, memory_order_release);

      if (instance != nullptr)
         instance->connectedProm_.set_value(true);
      break;
   }

   case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
   {
      LOGERR << "lws client connection error";
      if (len > 0)
      {
         auto errstr = (char*)in;
         LOGERR << "   error message: " << errstr;
      }
      else
      {
         LOGERR << "no error message was provided by lws";
      }
   }

   case LWS_CALLBACK_CLIENT_CLOSED:
   case LWS_CALLBACK_CLOSED:
   {
      try
      {
         instance->connected_.store(false, memory_order_release);
         if (instance->callbackPtr_ != nullptr)
            instance->callbackPtr_->disconnected();
         instance->shutdown();
      }
      catch(LWS_Error&)
      { }

      break;
   }

   case LWS_CALLBACK_CLIENT_RECEIVE:
   {
      BinaryData bdData;
      bdData.resize(len);
      memcpy(bdData.getPtr(), in, len);

      instance->readQueue_.push_back(move(bdData));
      break;
   }

   case LWS_CALLBACK_CLIENT_WRITEABLE:
   {
      if (instance->currentWriteMessage_.isDone())
      {
         try
         {
            instance->currentWriteMessage_ =
               move(instance->writeQueue_.pop_front());
         }
         catch (IsEmpty&)
         {
            break;
         }
      }

      auto& packet = instance->currentWriteMessage_.getNextPacket();
      auto body = (uint8_t*)packet.getPtr() + LWS_PRE;
      auto m = lws_write(wsi, 
         body, packet.getSize() - LWS_PRE,
         LWS_WRITE_BINARY);

      if (m != packet.getSize() - LWS_PRE)
      {
         LOGERR << "failed to send packet of size";
         LOGERR << "packet is " << packet.getSize() <<
            " bytes, sent " << m << " bytes";
      }

      if (instance->currentWriteMessage_.isDone())      
         instance->count_.fetch_add(1, memory_order_relaxed);

      /***
      In case several threads are trying to write to the same socket, it's
      possible their calls to callback_on_writeable may overlap, resulting 
      in a single write entry being consumed.

      To avoid this, we trigger the callback from within itself, which will 
      break out if there are no more items in the writeable stack.
      ***/
      lws_callback_on_writable(wsi);

      break;
   }

   default:
      break;
   }

   return 0;
}

////////////////////////////////////////////////////////////////////////////////
void WebSocketClient::readService()
{
   size_t packetid = 0;
   while (1)
   {
      BinaryData payload;
      try
      {
         payload = move(readQueue_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      //deser packet
      auto payloadRef = currentReadMessage_.insertDataAndGetRef(payload);
      auto result = 
         currentReadMessage_.message_.parsePacket(packetid++, payloadRef);
      if (!result)
      {
         currentReadMessage_.reset();
         continue;
      }

      if (!currentReadMessage_.message_.isReady())
         continue;


      //figure out request id, fulfill promise
      auto& msgid = currentReadMessage_.message_.getId();
      auto readMap = readPackets_.get();
      auto iter = readMap->find(msgid);
      if (iter != readMap->end())
      {
         auto& msgObjPtr = iter->second;
         auto callbackPtr = dynamic_cast<CallbackReturn_WebSocket*>(
            msgObjPtr->payload_->callbackReturn_.get());
         if (callbackPtr == nullptr)
            continue;

         callbackPtr->callback(currentReadMessage_.message_);
         readPackets_.erase(msgid);
         currentReadMessage_.reset();
      }
      else if (msgid == WEBSOCKET_CALLBACK_ID)
      {
         if (callbackPtr_ == nullptr)
         {
            currentReadMessage_.reset();
            continue;
         }

         auto msgptr = make_shared<::Codec_BDVCommand::BDVCallback>();
         if (!currentReadMessage_.message_.getMessage(msgptr.get()))
         {
            currentReadMessage_.reset();
            continue;
         }

         callbackPtr_->processNotifications(msgptr);
         currentReadMessage_.reset();
      }
      else
      {
         LOGWARN << "invalid msg id";
      }
   }
}

