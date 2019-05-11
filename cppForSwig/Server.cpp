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

using namespace std;
using namespace ::google::protobuf;

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
   clients_ = make_shared<Clients>();
}

///////////////////////////////////////////////////////////////////////////////
WebSocketServer::WebSocketServer(const bool& bip150AuthMode) :
   bip150AuthMode_(bip150AuthMode)
{
   clients_ = make_shared<Clients>();
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
      auto&& bdid = CryptoPRNG::generateRandom(8);
      session_data->id_ = *(uint64_t*)bdid.getPtr();

      auto instance = WebSocketServer::getInstance();
      instance->addId(session_data->id_, wsi);

      auto packetPtr = make_shared<BDV_packet>(session_data->id_);
      packetPtr->data_ = instance->encInitPacket_;
      instance->packetQueue_.push_back(move(packetPtr));

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
      auto packetPtr = make_shared<BDV_packet>(session_data->id_);
      packetPtr->data_.resize(len);
      memcpy(packetPtr->data_.getPtr(), (uint8_t*)in, len);

      auto wsPtr = WebSocketServer::getInstance();
      wsPtr->packetQueue_.push_back(move(packetPtr));
      break;
   }

   case LWS_CALLBACK_SERVER_WRITEABLE:
   {
      auto wsPtr = WebSocketServer::getInstance();
      auto stateMap = wsPtr->getConnectionStateMap();
      auto iter = stateMap->find(session_data->id_);
      if (iter == stateMap->end())
      {
         //no client object, kill this connection
         return -1;
      }

      if (iter->second.run_->load(memory_order_relaxed) == -1)
      {
         //connection flagged for closing
         return -1;
      }

      auto& stateObj = iter->second;
      if (stateObj.currentWriteMsg_.isDone())
      {
         try
         {
            stateObj.currentWriteMsg_ =
               move(stateObj.serializedStack_->pop_front());
         }
         catch (IsEmpty&)
         {
            auto val = iter->second.count_->load(memory_order_relaxed);
            if(val != 0)
               LOGWARN << "!!!! out of server write loop with pending message: " << val;
            break;
         }
      }
      
      auto& ws_msg = stateObj.currentWriteMsg_;

      auto& packet = ws_msg.getNextPacket();
      auto body = (uint8_t*)packet.getPtr() + LWS_PRE;

      auto m = lws_write(wsi, 
         body, packet.getSize() - LWS_PRE,
         LWS_WRITE_BINARY);

      if (m != (int)packet.getSize() - (int)LWS_PRE)
      {
         LOGERR << "failed to send packet of size";
         LOGERR << "packet is " << packet.getSize() <<
            " bytes, sent " << m << " bytes";
      }

      if (stateObj.currentWriteMsg_.isDone())
         stateObj.count_->fetch_sub(1, memory_order_relaxed);
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
void WebSocketServer::start(BlockDataManagerThread* bdmT,
   const string& datadir, const bool& ephemeralPeers, const bool& async)
{
   shutdownPromise_ = promise<bool>();
   shutdownFuture_ = shutdownPromise_.get_future();
   auto instance = getInstance();

   //init auth peer object
   if (!ephemeralPeers)
   {
      string peerFilename(SERVER_AUTH_PEER_FILENAME);
      instance->authorizedPeers_ = make_shared<AuthorizedPeers>(
         datadir, peerFilename);
   }
   else
   {
      instance->authorizedPeers_ = make_shared<AuthorizedPeers>();
   }

   //setup encinit and pubkey present packet
   instance->encInitPacket_ = READHEX("010000000B");

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

   //read & write threads
   auto writeProcessThread = [instance](void)->void
   {
      instance->prepareWriteThread();
   };

   auto readProcessThread = [instance](void)->void
   {
      instance->clientInterruptThread();
   };

   unsigned parserThreads = thread::hardware_concurrency() / 4;
   if (parserThreads == 0)
      parserThreads = 1;
   for (unsigned i = 0; i < parserThreads; i++)
   {
      instance->threads_.push_back(thread(writeProcessThread));
      instance->threads_.push_back(thread(readProcessThread));
   }

   auto port = stoi(bdmT->bdm()->config().listenPort_);
   if (port == 0)
      port = WEBSOCKET_PORT;

   //run service thread
   if (async)
   {
      auto loopthr = [instance, port](void)->void
      {
         instance->webSocketService(port);
      };

      auto fut = instance->isReadyProm_.get_future();
      instance->threads_.push_back(thread(loopthr));

      fut.get();
      return;
   }

   instance->webSocketService(port);
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

   instance->msgQueue_.terminate();
   instance->clientConnectionInterruptQueue_.terminate();
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
   catch (future_error)
   {}
}

///////////////////////////////////////////////////////////////////////////////
SecureBinaryData WebSocketServer::getPublicKey()
{
   auto instance = getInstance();
   auto& pubkey = instance->authorizedPeers_->getOwnPublicKey();
   SecureBinaryData keySbd(pubkey.pubkey, BIP151PUBKEYSIZE);
   return keySbd;
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
void WebSocketServer::webSocketService(int port)
{
   struct lws_context_creation_info info;
   struct lws_vhost *vhost;
   const char *iface = nullptr;
   int uid = -1, gid = -1;
   int pp_secs = 0;
   int opts = 0;
   int n = 0;

   memset(&info, 0, sizeof info);
   info.port = port;

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

      //get connection state object
      auto stateMap = getConnectionStateMap();
      auto iter = stateMap->find(packetPtr->bdvID_);
      if (iter == stateMap->end())
      {
         //missing state map, kill connection
         continue;
      }

      iter->second.readQueue_->push_back(move(packetPtr->data_));
      clientConnectionInterruptQueue_.push_back(move(packetPtr->bdvID_));
   }

   DatabaseContainer_Sharded::clearThreadShardTx(this_thread::get_id());
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::clientInterruptThread()
{
   while (true)
   {
      uint64_t clientId;
      try
      {
         clientId = clientConnectionInterruptQueue_.pop_front();
      }
      catch(StopBlockingLoop&)
      {
         break;
      }

      auto clientMap = clientStateMap_.get();
      auto iter = clientMap->find(clientId);
      if (iter == clientMap->end())
         continue;

      auto& ccs = iter->second;
      unsigned zero = 0;
      if (!ccs.readLock_->compare_exchange_weak(zero, 1))
      {
         clientConnectionInterruptQueue_.push_back(move(clientId));
         continue;
      }

      ccs.processReadQueue(clients_);
      ccs.readLock_->store(0);
   }
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::write(const uint64_t& id, const uint32_t& msgid,
   shared_ptr<Message> message)
{
   if (message == nullptr)
      return;

   auto msg = make_unique<PendingMessage>(id, msgid, message);
   auto instance = getInstance();
   instance->msgQueue_.push_back(move(msg));
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::prepareWriteThread()
{
   while (true)
   {
      unique_ptr<PendingMessage> msg;
      try
      {
         msg = msgQueue_.pop_front();
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      if (msg == nullptr)
         continue;

      auto statemap = getConnectionStateMap();
      auto stateIter = statemap->find(msg->id_);
      if (stateIter == statemap->end())
         continue;
      auto statePtr = &stateIter->second;

      //grab state object lock
      unsigned zero = 0;
      if(!statePtr->writeLock_->compare_exchange_weak(zero, 1))
      { 
         msgQueue_.push_back(move(msg));
         continue;
      }

      if (!statePtr->bip151Connection_->connectionComplete())
      {
         //aead session uninitialized, kill connection
         return;
      }

      //check for rekey
      {
         bool needs_rekey = false;
         auto rightnow = chrono::system_clock::now();

         if (statePtr->bip151Connection_->rekeyNeeded(msg->message_->ByteSize()))
         {
            needs_rekey = true;
         }
         else
         {
            auto time_sec = chrono::duration_cast<chrono::seconds>(
               rightnow - statePtr->outKeyTimePoint_);
            if (time_sec.count() >= AEAD_REKEY_INVERVAL_SECONDS)
               needs_rekey = true;
         }
         
         if (needs_rekey)
         {
            //create rekey packet
            BinaryData rekeyPacket(BIP151PUBKEYSIZE);
            memset(rekeyPacket.getPtr(), 0, BIP151PUBKEYSIZE);
            
            SerializedMessage ws_msg;
            ws_msg.construct(
               rekeyPacket.getDataVector(), 
               statePtr->bip151Connection_.get(),
               WS_MSGTYPE_AEAD_REKEY);

            //push to write map
            statePtr->serializedStack_->push_back(move(ws_msg));

            //rekey outer bip151 channel
            statePtr->bip151Connection_->rekeyOuterSession();

            //set outkey timepoint to rightnow
            statePtr->outKeyTimePoint_ = rightnow;
         }
      }

      //serialize arg
      vector<uint8_t> serializedData;
      if (msg->message_->ByteSize() > 0)
      {
         serializedData.resize(msg->message_->ByteSize());
         auto result = msg->message_->SerializeToArray(
            &serializedData[0], serializedData.size());
         if (!result)
         {
            LOGWARN << "failed to serialize message";
            return;
         }
      }

      SerializedMessage ws_msg;
      ws_msg.construct(
         serializedData, statePtr->bip151Connection_.get(), 
         WS_MSGTYPE_FRAGMENTEDPACKET_HEADER, msg->msgid_);

      //push to write map
      statePtr->serializedStack_->push_back(move(ws_msg));
      statePtr->count_->fetch_add(1, memory_order_relaxed);

      //reset lock
      statePtr->writeLock_->store(0);

      //call write callback
      lws_callback_on_writable(statePtr->wsiPtr_);
   }
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
shared_ptr<map<uint64_t, ClientConnection>> 
   WebSocketServer::getConnectionStateMap() const
{
   return clientStateMap_.get();
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::addId(const uint64_t& id, struct lws* ptr)
{
   auto&& lbds = getAuthPeerLambda();
   auto&& write_pair = make_pair(id, ClientConnection(ptr, id, lbds,
      bip150AuthMode_));
   clientStateMap_.insert(move(write_pair));
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::eraseId(const uint64_t& id)
{
   clientStateMap_.erase(id);
}

///////////////////////////////////////////////////////////////////////////////
AuthPeersLambdas WebSocketServer::getAuthPeerLambda(void) const
{
   auto authPeerPtr = authorizedPeers_;

   auto getMap = [authPeerPtr](void)->const map<string, btc_pubkey>&
   {
      return authPeerPtr->getPeerNameMap();
   };

   auto getPrivKey = [authPeerPtr](
      const BinaryDataRef& pubkey)->const SecureBinaryData&
   {
      return authPeerPtr->getPrivateKey(pubkey);
   };

   auto getAuthSet = [authPeerPtr](void)->const set<SecureBinaryData>&
   {
      return authPeerPtr->getPublicKeySet();
   };

   return AuthPeersLambdas(getMap, getPrivKey, getAuthSet);
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::closeClientConnection(uint64_t id)
{
   auto clientStateMap = getConnectionStateMap();
   auto iter = clientStateMap->find(id);
   if (iter == clientStateMap->end())
   {
      //invalid client id, return
      return;
   }

   iter->second.closeConnection();
}

///////////////////////////////////////////////////////////////////////////////
//
// ClientConnection
//
///////////////////////////////////////////////////////////////////////////////
void ClientConnection::processReadQueue(shared_ptr<Clients> clients)
{
   while (run_->load(memory_order_relaxed) != -1)
   {
      BinaryData packetData;
      try
      {
         packetData = move(readQueue_->pop_front());
      }
      catch (IsEmpty&)
      {
         //end loop condition
         return;
      }

      if (packetData.getSize() == 0)
      {
         LOGWARN << "empty command packet";
         continue;
      }

      if (readLeftOverData_.getSize() != 0)
      {
         readLeftOverData_.append(packetData);
         packetData = move(readLeftOverData_);
         readLeftOverData_.clear();
      }

      if (bip151Connection_->connectionComplete())
      {
         if(packetData.getSize() < POLY1305MACLEN + 4)
         { 
            //append to the leftover data until we have a packet that's at least
            //as large as the MAC length + the encrypted packet size
            readLeftOverData_ = move(packetData);
            continue;
         }

         //decrypt packet
         size_t plainTextSize = packetData.getSize() - POLY1305MACLEN;
         auto result = bip151Connection_->decryptPacket(
            packetData.getPtr(), packetData.getSize(),
            (uint8_t*)packetData.getPtr(), packetData.getSize());

         if (result != 0)
         {
            if (result <= WEBSOCKET_MESSAGE_PACKET_SIZE && result > -1)
            {
               /*
               lws receives packet in the order the counterpart sent them, but
               it may break down a packet into several payloads, dependent on the
               write buffer fillrate.

               The AEAD layer requires full packets to verify the attached MAC,
               meaning we cannot distinguish between packets with invalid encryption
               and partially transmitted packets with valid encryption until we have
               as many bytes as the advertized chacha20 size available to us.

               At same time we can reject packets that advertize a size superior to
               our expected maximum packet size (WEBSOCKET_MESSAGE_PACKET_SIZE),
               which is often the case when deciphering the length of an invalidly
               encrypted packet.

               Since lws does not spill packets onto one another, there is no risk
               that the data we receive carries the head of another packet at its tail.
               Reconstruction is therefor a simple case of appending the incoming data
               to the previous left over until we have enough data to decrypt for the
               advertized packet size.
               */
               readLeftOverData_ = move(packetData);
               continue;
            }

            //failed to decrypt, kill connection
            closeConnection();
            continue;
         }

         packetData.resize(plainTextSize);
      }

      uint8_t msgType =
         WebSocketMessagePartial::getPacketType(packetData.getRef());

      if (msgType > WS_MSGTYPE_AEAD_THESHOLD)
      {
         processAEADHandshake(move(packetData));
         continue;
      }

      if (bip151Connection_->getBIP150State() != BIP150State::SUCCESS)
      {
         //can't get this far without fully setup AEAD
         closeConnection();
         continue;
      }

      BinaryDataRef bdr((uint8_t*)&id_, 8);
      auto&& hexID = bdr.toHexStr();
      auto bdvPtr = clients->get(hexID);

      if (bdvPtr != nullptr)
      {
         //create payload
         auto bdv_payload = make_shared<BDV_Payload>();
         bdv_payload->bdvPtr_ = bdvPtr;
         bdv_payload->packetData_ = move(packetData);
         bdv_payload->bdvID_ = id_;

         //queue for clients thread pool to process
         clients->queuePayload(bdv_payload);
      }
      else
      {
         //unregistered command
         WebSocketMessagePartial msgObj;
         msgObj.parsePacket(packetData);
         if (msgObj.getType() != WS_MSGTYPE_SINGLEPACKET)
         {
            //invalid msg type, kill connection
            continue;
         }

         auto&& messageRef = msgObj.getSingleBinaryMessage();

         if (messageRef.getSize() == 0)
         {
            //invalid msg, kill connection
            continue;
         }

         //process command 
         auto message = make_shared<::Codec_BDVCommand::StaticCommand>();
         if (!message->ParseFromArray(messageRef.getPtr(), messageRef.getSize()))
         {
            //invalid msg, kill connection
            continue;
         }

         auto&& reply = clients->processUnregisteredCommand(
            id_, message);

         //reply
         WebSocketServer::write(id_, msgObj.getId(), reply);
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void ClientConnection::processAEADHandshake(BinaryData msg)
{
   auto writeToClient = [this](uint8_t type,
      const BinaryDataRef& msg, bool encrypt)->void
   {
      BIP151Connection* connPtr = nullptr;
      if (encrypt)
         connPtr = bip151Connection_.get();
      SerializedMessage aeadMsg;
      aeadMsg.construct(msg, connPtr, type);
      serializedStack_->push_back(move(aeadMsg));
      count_->fetch_add(1, memory_order_relaxed);

      //call write callback
      lws_callback_on_writable(wsiPtr_);
   };

   auto processHandshake = [this, &writeToClient](const BinaryData& msgdata)->bool
   {
      WebSocketMessagePartial wsMsg;

      if (!wsMsg.parsePacket(msgdata.getRef()) || !wsMsg.isReady())
      {
         //invalid packet
         return false;
      }

      auto dataBdr = wsMsg.getSingleBinaryMessage();
      switch (wsMsg.getType())
      {
      case WS_MSGTYPE_AEAD_SETUP:
      {
         //send pubkey message
         writeToClient(
            WS_MSGTYPE_AEAD_PRESENT_PUBKEY, 
            bip151Connection_->getOwnPubKey(), 
            false);

         //init bip151 handshake
         BinaryData encinitData(ENCINITMSGSIZE);
         if (bip151Connection_->getEncinitData(
            encinitData.getPtr(), ENCINITMSGSIZE,
            BIP151SymCiphers::CHACHA20POLY1305_OPENSSH) != 0)
         {
            //failed to init handshake, kill connection
            return false;
         }

         writeToClient(WS_MSGTYPE_AEAD_ENCINIT, encinitData.getRef(), false);
         break;
      }

      case WS_MSGTYPE_AEAD_ENCACK:
      {
         //process client encack
         if (bip151Connection_->processEncack(
            dataBdr.getPtr(), dataBdr.getSize(), true) != 0)
         {
            //failed to init handshake, kill connection
            return false;
         }

         break;
      }

      case WS_MSGTYPE_AEAD_REKEY:
      {
         if (bip151Connection_->getBIP150State() !=
            BIP150State::SUCCESS)
         {
            //can't rekey before auth, kill connection
            return false;
         }

         //process rekey
         if (bip151Connection_->processEncack(
            dataBdr.getPtr(), dataBdr.getSize(), false) != 0)
         {
            //failed to init handshake, kill connection
            LOGWARN << "failed to process rekey";
            return false;
         }

         break;
      }

      case WS_MSGTYPE_AEAD_ENCINIT:
      {
         //process client encinit
         if (bip151Connection_->processEncinit(
            dataBdr.getPtr(), dataBdr.getSize(), false) != 0)
         {
            //failed to init handshake, kill connection
            return false;
         }

         //return encack
         BinaryData encackData(BIP151PUBKEYSIZE);
         if (bip151Connection_->getEncackData(
            encackData.getPtr(), BIP151PUBKEYSIZE) != 0)
         {
            //failed to init handshake, kill connection
            return false;
         }

         writeToClient(WS_MSGTYPE_AEAD_ENCACK, encackData.getRef(), false);

         break;
      }

      case WS_MSGTYPE_AUTH_CHALLENGE:
      {
         bool goodChallenge = true;
         auto challengeResult =
            bip151Connection_->processAuthchallenge(
               dataBdr.getPtr(),
               dataBdr.getSize(),
               true); //true: step #1 of 6

         if (challengeResult == -1)
         {
            //auth fail, kill connection
            return false;
         }
         else if (challengeResult == 1)
         {
            goodChallenge = false;
         }

         BinaryData authreplyBuf(BIP151PRVKEYSIZE * 2);
         if (bip151Connection_->getAuthreplyData(
            authreplyBuf.getPtr(),
            authreplyBuf.getSize(),
            true, //true: step #2 of 6
            goodChallenge) == -1)
         {
            //auth setup failure, kill connection
            return false;
         }

         writeToClient(WS_MSGTYPE_AUTH_REPLY,
            authreplyBuf.getRef(), true);

         break;
      }

      case WS_MSGTYPE_AUTH_PROPOSE:
      {
         bool goodPropose = true;
         auto proposeResult = bip151Connection_->processAuthpropose(
            dataBdr.getPtr(),
            dataBdr.getSize());

         if (proposeResult == -1)
         {
            //auth setup failure, kill connection
            return false;
         }
         else if (proposeResult == 1)
         {
            goodPropose = false;
         }
         else
         {
            //keep track of the propose check state
            bip151Connection_->setGoodPropose();
         }

         BinaryData authchallengeBuf(BIP151PRVKEYSIZE);
         if (bip151Connection_->getAuthchallengeData(
            authchallengeBuf.getPtr(),
            authchallengeBuf.getSize(),
            "", //empty string, use chosen key from processing auth propose
            false, //false: step #4 of 6
            goodPropose) == -1)
         {
            //auth setup failure, kill connection
            return false;
         }

         writeToClient(WS_MSGTYPE_AUTH_CHALLENGE,
            authchallengeBuf.getRef(), true);

         break;
      }

      case WS_MSGTYPE_AUTH_REPLY:
      {
         if (bip151Connection_->processAuthreply(
            dataBdr.getPtr(),
            dataBdr.getSize(),
            false,
            bip151Connection_->getProposeFlag()) != 0)
         {
            //invalid auth setup, kill connection
            return false;
         }

         //rekey after succesful BIP150 handshake
         bip151Connection_->bip150HandshakeRekey();
         outKeyTimePoint_ = chrono::system_clock::now();

         break;
      }

      default:
         //unexpected msg id, kill connection
         return false;
      }

      return true;
   };

   if (!processHandshake(msg))
      closeConnection();
}

///////////////////////////////////////////////////////////////////////////////
void ClientConnection::closeConnection()
{
   run_->store(-1, memory_order_relaxed);
}
