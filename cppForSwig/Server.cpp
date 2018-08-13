////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-18, goatpig.                                           //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////


#include "Server.h"
#include "BlockDataManagerConfig.h"
#include "BDM_Server.h"
#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

using namespace ::google::protobuf;

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//// FCGI_Server
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
FCGI_Server::FCGI_Server(
   BlockDataManagerThread* bdmT, string port, bool listen_all) :
   ip_(listen_all ? "" : "127.0.0.1"), port_(port)
{
   clients_ = make_unique<Clients>(bdmT, getShutdownCallback());

   LOGINFO << "Listening on port " << port;
   if (listen_all)
      LOGWARN << "Listening to all incoming connections";

   liveThreads_.store(0, memory_order_relaxed);
}

///////////////////////////////////////////////////////////////////////////////
void FCGI_Server::init()
{
   run_.store(true, memory_order_relaxed);

   stringstream ss;
#ifdef _WIN32
   if (ip_ == "127.0.0.1" || ip_ == "localhost")
      ss << "localhost:" << port_;
   else
      ss << ip_ << ":" << port_;
#else
   ss << ip_ << ":" << port_;
#endif

   auto socketStr = ss.str();
   sockfd_ = FCGX_OpenSocket(socketStr.c_str(), 10);
   if (sockfd_ == -1)
      throw runtime_error("failed to create FCGI listen socket");

   keepAliveService_.startService();
}

///////////////////////////////////////////////////////////////////////////////
void FCGI_Server::haltFcgiLoop()
{
   /*** to exit the FCGI loop we need to shutdown the FCGI lib as a whole
   (otherwise accept will keep on blocking until a new fcgi request is
   received. Shutting down the lib calls WSACleanUp in Windows, which will
   terminate all networking capacity for the process.

   This means the node P2P connection will crash if it isn't cleaned up first.
   ***/

   //shutdown loop
   run_.store(false, memory_order_relaxed);

   //connect to own listen to trigger thread exit
   SimpleSocket sock("127.0.0.1", port_);
   if(!sock.connectToRemote())
      return;

   BinaryData bd;
   auto&& fcgiMsg = FcgiMessage::makePacket(bd.getRef());
   auto serdata = fcgiMsg.serialize();

   auto payload = make_unique<WritePayload_Raw>();
   payload->data_ = move(serdata);
   sock.pushPayload(move(payload), nullptr);
   sock.shutdown();
}

///////////////////////////////////////////////////////////////////////////////
void FCGI_Server::enterLoop()
{
   while (run_.load(memory_order_relaxed))
   {
      auto request = make_shared<FCGX_Request>();
      FCGX_InitRequest(request.get(), sockfd_, 0);
      int rc = FCGX_Accept_r(request.get());

      if (rc != 0)
      {
#ifdef _WIN32
         auto err_i = WSAGetLastError();
#else
         auto err_i = errno;
#endif
         LOGERR << "Accept failed with error number: " << err_i;
         LOGERR << "error message is: " << strerror(err_i);
         throw runtime_error("accept error");
      }

      auto processRequestLambda = [this](shared_ptr<FCGX_Request> req)->void
      {
         this->processRequest(req);
      };

      liveThreads_.fetch_add(1, memory_order_relaxed);
      thread thr(processRequestLambda, request);
      if (thr.joinable())
         thr.detach();

      //TODO: implement thread recycling
   }
}

///////////////////////////////////////////////////////////////////////////////
void FCGI_Server::processRequest(shared_ptr<FCGX_Request> req)
{
   //extract the string command from the fgci request
  /* stringstream ss;
   stringstream retStream;
   char* content = nullptr;

   //pass to clients_
   char* content_length = FCGX_GetParam("CONTENT_LENGTH", req->envp);
   if (content_length != nullptr)
   {
      auto a = atoi(content_length);
      if (a == 0)
      {
         FCGX_Finish_r(req.get());
         return;
      }

      content = new char[a + 1];
      FCGX_GetStr(content, a, req->in);
      content[a] = 0;

      string contentStr(content + 4);
      io::ArrayInputStream ais(content + 4, a - 4);
      auto message = make_shared<::Codec_BDVCommand::BDVCommand>();
      TextFormat::Parse(&ais, message.get());

      //print HTML header
      ss << "HTTP/1.1 200 OK\r\n";
      ss << "Content-Type: text/html; charset=UTF-8\r\n";

      try
      {
         auto&& retVal = clients_->runCommand_FCGI(message);
         content[4] = 0;
         if (retVal != nullptr)
         {
            retStream << content;
            string ser_str;
            TextFormat::PrintToString(*retVal.get(), &ser_str);
            retStream << ser_str;
         }
      }
      catch (exception& e)
      {
         ::Codec_NodeStatus::BDV_Error errorMsg;
         errorMsg.set_error(e.what());
         string err_str;
         TextFormat::PrintToString(errorMsg, &err_str);
         retStream << err_str;
      }
      catch (DbErrorMsg &e)
      {
         ::Codec_NodeStatus::BDV_Error errorMsg;
         errorMsg.set_error(e.what());
         string err_str;
         TextFormat::PrintToString(errorMsg, &err_str);
         retStream << err_str;
      }
      catch (...)
      {
         ::Codec_NodeStatus::BDV_Error errorMsg;
         errorMsg.set_error("unknown error processing message");
         string err_str;
         TextFormat::PrintToString(errorMsg, &err_str);
         retStream << err_str;
      }

      //complete HTML header
      ss << "Content-Length: " << retStream.str().size();
      ss << "\r\n\r\n";
   }
   else
   {
      LOGERR << "empty content_length";
      FCGX_Finish_r(req.get());

      liveThreads_.fetch_sub(1, memory_order_relaxed);
      return;
   }

   delete[] content;

   if (retStream.str().size() > 0)
   {
      //print serialized retVal
      ss << retStream.str();

      auto&& retStr = ss.str();
      vector<pair<size_t, size_t>> msgOffsetVec;
      auto totalsize = retStr.size();
      //8192 (one memory page) - 8 (1 fcgi header), also a multiple of 8
      size_t delim = 8184;
      size_t start = 0;

      while (totalsize > 0)
      {
         auto chunk = delim;
         if (chunk > totalsize)
            chunk = totalsize;

         msgOffsetVec.push_back(make_pair(start, chunk));
         start += chunk;
         totalsize -= chunk;
      }

      //get non const ptr of the message string since we will set temp null bytes
      //for the purpose of breaking down the string into FCGI sized packets
      char* ptr = const_cast<char*>(retStr.c_str());

      //complete FCGI request
      for (auto& offsetPair : msgOffsetVec)
         FCGX_PutStr(ptr + offsetPair.first, offsetPair.second, req->out);
   }

   FCGX_Finish_r(req.get());

   if (req->ipcFd != -1)
      passToKeepAliveService(req);

   liveThreads_.fetch_sub(1, memory_order_relaxed);*/
}

///////////////////////////////////////////////////////////////////////////////
void FCGI_Server::shutdown()
{
   keepAliveService_.shutdown();
   clients_->shutdown();
}

///////////////////////////////////////////////////////////////////////////////
void FCGI_Server::passToKeepAliveService(shared_ptr<FCGX_Request> req)
{
   auto serviceRead = [this, req](void)->void
   {
      if (!run_.load(memory_order_relaxed))
         return;

      int rc = FCGX_Accept_r(req.get());

      if (rc != 0)
      {
#ifdef _WIN32
         auto err_i = WSAGetLastError();
#else
         auto err_i = errno;
#endif
         LOGERR << "Accept failed with error number: " << err_i;
         LOGERR << "error message is: " << strerror(err_i);
         throw runtime_error("accept error");
      }

      auto processRequestLambda = [this](shared_ptr<FCGX_Request> req)->void
      {
         this->processRequest(req);
      };

      liveThreads_.fetch_add(1, memory_order_relaxed);
      thread thr(processRequestLambda, req);
      if (thr.joinable())
         thr.detach();
   };

   SocketStruct keepAliveStruct;

   keepAliveStruct.serviceRead_ = serviceRead;
   keepAliveStruct.singleUse_ = true;

   SOCKET sockfd = req->ipcFd;
#ifdef _WIN32
   sockfd = Win32GetFDForDescriptor(req->ipcFd);
#endif
   keepAliveStruct.sockfd_ = sockfd;

   keepAliveService_.addSocket(keepAliveStruct);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//// WebSocketServer
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
atomic<WebSocketServer*> WebSocketServer::instance_;
mutex WebSocketServer::mu_;
promise<bool> WebSocketServer::shutdownPromise_;
shared_future<bool> WebSocketServer::shutdownFuture_;

///////////////////////////////////////////////////////////////////////////////
WebSocketServer::WebSocketServer()
{
   clients_ = make_unique<Clients>();
}

///////////////////////////////////////////////////////////////////////////////
static struct lws_protocols protocols[] = {
   /* first protocol must always be HTTP handler */

   {
      "http-only",		/* name */
      callback_http,		/* callback */
      sizeof(struct per_session_data__http),	/* per_session_data_size */
      0,			/* max frame size / rx buffer */
   },
   {
      "armory-bdm-protocol",
      WebSocketServer::callback,
      sizeof(struct per_session_data__bdv),
      per_session_data__bdv::rcv_size,
   },

{ NULL, NULL, 0, 0 } /* terminator */
};

///////////////////////////////////////////////////////////////////////////////
int callback_http(struct lws *wsi, enum lws_callback_reasons reason,
   void *user, void *in, size_t len)
{
   return 0;
}

///////////////////////////////////////////////////////////////////////////////
int WebSocketServer::callback(
   struct lws *wsi, enum lws_callback_reasons reason,
   void *user, void *in, size_t len)
{
   struct per_session_data__bdv *session_data =
      (struct per_session_data__bdv *)user;

   switch (reason)
   {

   case LWS_CALLBACK_PROTOCOL_INIT:
   {
      auto instance = WebSocketServer::getInstance();
      instance->setIsReady();
      break;
   }

   case LWS_CALLBACK_ESTABLISHED:
   {
      auto&& bdid = SecureBinaryData().GenerateRandom(8);
      session_data->id_ = *(uint64_t*)bdid.getPtr();

      auto instance = WebSocketServer::getInstance();
      instance->addId(session_data->id_, wsi);
      break;
   }

   case LWS_CALLBACK_CLOSED:
   {
      auto instance = WebSocketServer::getInstance();
      BinaryDataRef bdr((uint8_t*)&session_data->id_, 8);
      instance->clients_->unregisterBDV(bdr.toHexStr());
      instance->eraseId(session_data->id_);

      break;
   }

   case LWS_CALLBACK_RECEIVE:
   {
      auto packetPtr = make_shared<BDV_packet>(session_data->id_, wsi);
      packetPtr->data_.resize(len);
      memcpy(packetPtr->data_.getPtr(), (uint8_t*)in, len);

      auto wsPtr = WebSocketServer::getInstance();
      wsPtr->packetQueue_.push_back(move(packetPtr));
      break;
   }

   case LWS_CALLBACK_SERVER_WRITEABLE:
   {
      auto wsPtr = WebSocketServer::getInstance();
      auto writeMap = wsPtr->getWriteMap();
      auto iter = writeMap->find(session_data->id_);
      if (iter == writeMap->end())
         break;

      if (iter->second.currentMsg_.isDone())
      {
         try
         {
            iter->second.currentMsg_ = 
               move(iter->second.stack_->pop_front());
         }
         catch (IsEmpty&)
         {
            auto val = iter->second.count_->load(memory_order_relaxed);
            if(val != 0)
               LOGWARN << "!!!! out of server write loop with pending message: " << val;
            break;
         }
      }
      
      auto& ws_msg = iter->second.currentMsg_;

      auto& packet = ws_msg.getNextPacket();
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

      if (iter->second.currentMsg_.isDone())
         iter->second.count_->fetch_sub(1, memory_order_relaxed);
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

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::start(BlockDataManagerThread* bdmT, bool async)
{
   shutdownPromise_ = promise<bool>();
   shutdownFuture_ = shutdownPromise_.get_future();
   auto instance = getInstance();

   //init Clients object
   auto shutdownLbd = [](void)->void
   {
      WebSocketServer::shutdown();
   };

   instance->clients_->init(bdmT, shutdownLbd);

   //start command threads
   auto commandThr = [instance](void)->void
   {
      instance->commandThread();
   };

   instance->threads_.push_back(thread(commandThr));

   //run service thread
   if (async)
   {
      auto loopthr = [instance](void)->void
      {
         instance->webSocketService();
      };

      auto fut = instance->isReadyProm_.get_future();
      instance->threads_.push_back(thread(loopthr));

      fut.get();
      return;
   }

   instance->webSocketService();
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::shutdown()
{
   unique_lock<mutex> lock(mu_, defer_lock);
   if (!lock.try_lock())
      return;
   
   auto ptr = instance_.load(memory_order_relaxed);
   if (ptr == nullptr)
      return;

   auto instance = getInstance();
   if (instance->run_.load(memory_order_relaxed) == 0)
      return;

   instance->clients_->shutdown();
   instance->run_.store(0, memory_order_relaxed);
   instance->packetQueue_.terminate();

   vector<thread::id> idVec;
   for (auto& thr : instance->threads_)
   {
      idVec.push_back(thr.get_id());
      if (thr.joinable())
         thr.join();
   }

   instance->threads_.clear();
   DatabaseContainer_Sharded::clearThreadShardTx(idVec);

   instance_.store(nullptr, memory_order_relaxed);
   delete instance;
   
   try
   {
      shutdownPromise_.set_value(true);
   }
   catch (future_error& e)
   {}
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::setIsReady()
{
   try
   {
      isReadyProm_.set_value(true);
   }
   catch (future_error&)
   {}
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::webSocketService()
{
   struct lws_context_creation_info info;
   struct lws_vhost *vhost;
   const char *iface = nullptr;
   int uid = -1, gid = -1;
   int pp_secs = 0;
   int opts = 0;
   int n = 0;

   memset(&info, 0, sizeof info);
   info.port = WEBSOCKET_PORT;

   info.iface = iface;
   info.protocols = protocols;
   info.log_filepath = nullptr;
   info.ws_ping_pong_interval = pp_secs;
   info.gid = gid;
   info.uid = uid;
   info.max_http_header_pool = 256;
   info.options = opts | LWS_SERVER_OPTION_VALIDATE_UTF8 | LWS_SERVER_OPTION_EXPLICIT_VHOSTS;
   info.timeout_secs = 0;
   info.ip_limit_ah = 24; /* for testing */
   info.ip_limit_wsi = 105; /* for testing */

   auto context = lws_create_context(&info);
   if (context == nullptr) 
      throw LWS_Error("failed to create LWS context");

   vhost = lws_create_vhost(context, &info);
   if (vhost == nullptr)
      throw LWS_Error("failed to create vhost");

   run_.store(1, memory_order_relaxed);
   try
   {
   while (run_.load(memory_order_relaxed) != 0 && n >= 0)
   {
      n = lws_service(context, 50);
   }
   }
   catch(exception& e)
   {
      LOGERR << "server lws service choked: " << e.what();
   }

   LOGINFO << "cleaning up lws server";
   lws_vhost_destroy(vhost);
   lws_context_destroy(context);
}

///////////////////////////////////////////////////////////////////////////////
WebSocketServer* WebSocketServer::getInstance()
{
   while (1)
   {
      auto ptr = instance_.load(memory_order_relaxed);
      if (ptr == nullptr)
      {
         unique_lock<mutex> lock(mu_);
         ptr = instance_.load(memory_order_relaxed);
         if (ptr != nullptr)
            continue;

         ptr = new WebSocketServer();
         instance_.store(ptr, memory_order_relaxed);
      }

      return ptr;
   }
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::commandThread()
{
   while (1)
   {
      shared_ptr<BDV_packet> packetPtr;
      try
      {
         packetPtr = move(packetQueue_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         //end loop condition
         return;
      }

      if (packetPtr == nullptr)
      {
         LOGWARN << "empty command packet";
         continue;
      }

      //check wsi is valid
      if (packetPtr->wsiPtr_ == nullptr)
      {
         LOGWARN << "null wsi";
         continue;
      }

      BinaryDataRef bdr((uint8_t*)&packetPtr->bdvID_, 8);
      auto&& hexID = bdr.toHexStr();
      auto bdvPtr = clients_->get(hexID);

      if (bdvPtr != nullptr)
      {
         //create payload
         auto bdv_payload = make_shared<BDV_Payload>();
         bdv_payload->bdvPtr_ = bdvPtr;
         bdv_payload->packet_ = packetPtr;
         bdv_payload->packetID_ = bdvPtr->getNextPacketId(); 
  
         //queue for clients thread pool to process
         clients_->queuePayload(bdv_payload);
      }
      else
      {
         //unregistered command
         auto&& messageRef = 
            WebSocketMessageCodec::getSingleMessage(packetPtr->data_);
         if (messageRef.getSize() == 0)
            continue;

         //process command 
         auto message = make_shared<::Codec_BDVCommand::StaticCommand>();
         if (!message->ParseFromArray(messageRef.getPtr(), messageRef.getSize()))
            continue;

         auto&& reply = clients_->processUnregisteredCommand(
            packetPtr->bdvID_, message);

         //reply
         write(packetPtr->bdvID_, 
            WebSocketMessageCodec::getMessageId(packetPtr->data_), reply);
      }
   }

   DatabaseContainer_Sharded::clearThreadShardTx(this_thread::get_id());
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::write(const uint64_t& id, const uint32_t& msgid,
   shared_ptr<Message> message)
{
   if (message == nullptr)
      return;

   auto instance = getInstance();
   
   //serialize arg
   vector<uint8_t> serializedData;
   if (message->ByteSize() > 0)
   {
      serializedData.resize(message->ByteSize());
      auto result = message->SerializeToArray(
         &serializedData[0], serializedData.size());
      if (!result)
      {
         LOGWARN << "failed to serialize message";
         return;
      }
   }

   WebSocketMessage ws_msg; 
   ws_msg.construct(msgid, serializedData);

   //push to write map
   auto writemap = instance->writeMap_.get();

   auto wsi_iter = writemap->find(id);
   if (wsi_iter == writemap->end())
      return;

   wsi_iter->second.stack_->push_back(move(ws_msg));
   wsi_iter->second.count_->fetch_add(1, memory_order_relaxed);

   //call write callback
   lws_callback_on_writable(wsi_iter->second.wsiPtr_);
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::waitOnShutdown()
{
   try
   {
      shutdownFuture_.get();
   }
   catch(future_error&)
   { }
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<map<uint64_t, WriteStack>> WebSocketServer::getWriteMap(void)
{
   return writeMap_.get();
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::addId(const uint64_t& id, struct lws* ptr)
{
   auto&& write_pair = make_pair(id, WriteStack(ptr));
   writeMap_.insert(move(write_pair));
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::eraseId(const uint64_t& id)
{
   writeMap_.erase(id);
}
