////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-18, goatpig.                                           //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////


#include "Server.h"
#include "ArmoryConfig.h"
#include "BDM_Server.h"
#include "BIP15x_Handshake.h"

using namespace std;
using namespace google::protobuf;
using namespace Armory::Threading;
using namespace Armory::Wallets;

///////////////////////////////////////////////////////////////////////////////
PendingMessage::PendingMessage(uint64_t id, uint32_t msgid,
   std::unique_ptr<Socket_WritePayload> ptr) :
   id(id), msgid(msgid), payload(std::move(ptr))
{}

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
static struct lws_protocols protocols[] = {
   /* first protocol must always be HTTP handler */

   {
      "http-only",   /* name */
      callback_http, /* callback */
      sizeof(struct per_session_data__http), /* per_session_data_size */
      0,    /* max frame size / rx buffer */
      1,    /* id, custom value ignored by lws */
      NULL, /* user, custom value ignored by lws */
      0     /* rx_buffer_size, 0 for backwards compatibility */
   },
   {
      "armory-bdm-protocol",
      WebSocketServer::callback,
      sizeof(struct per_session_data__bdv),
      per_session_data__bdv::rcv_size,
      2,
      nullptr,
      0
   },

{ NULL, NULL, 0, 0, 0, NULL, 0 } /* terminator */
};

///////////////////////////////////////////////////////////////////////////////
int callback_http(struct lws *, enum lws_callback_reasons,
   void *, void *, size_t)
{
   return 0;
}

///////////////////////////////////////////////////////////////////////////////
int WebSocketServer::callback(struct lws *wsi,
   enum lws_callback_reasons reason,
   void *user, void *in, size_t len)
{
   auto* session_data = (struct per_session_data__bdv *)user;

   /***
   TODO: AEAD handshake takes place after WS handshake. Therefor, clients can
   connect and idle, holding a socket, without ever handshaking.

   Need to curate innactive sockets.
   ***/

   switch (reason)
   {
      case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
         break;

      case LWS_CALLBACK_PROTOCOL_INIT:
      {
         auto instance = WebSocketServer::getInstance();
         instance->setIsReady();
         break;
      }

      case LWS_CALLBACK_ESTABLISHED:
      {
         auto bdid = CryptoPRNG::generateRandom(8);
         session_data->id_ = *(uint64_t*)bdid.getPtr();

         auto instance = WebSocketServer::getInstance();
         instance->addId(session_data->id_, wsi);

         auto packetPtr = std::make_shared<BDV_packet>(session_data->id_);
         packetPtr->data_ = instance->encInitPacket_;
         instance->packetQueue_.push_back(std::move(packetPtr));
         break;
      }

      case LWS_CALLBACK_CLOSED:
      {
         auto instance = WebSocketServer::getInstance();
         BinaryDataRef bdr((uint8_t*)&session_data->id_, 8);
         instance->clients_->unregisterBDV(bdr.toHexStr());
         instance->eraseId(session_data->id_, wsi);

         if (instance->pendingWrites_.empty()) {
            break;
         }

         //pending write queue iterator is always set entering the 
         //lws callback unless the pending write set is empty
         if (instance->pendingWritesIter_ != instance->pendingWrites_.end() &&
            *instance->pendingWritesIter_ == wsi) {
            instance->pendingWritesIter_++;
         }

         instance->pendingWrites_.erase(wsi);
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
         if (wsPtr->pendingWrites_.empty() ||
            wsPtr->pendingWritesIter_ == wsPtr->pendingWrites_.end()) {
            break;
         }

         if (wsi != *wsPtr->pendingWritesIter_) {
            /*
            Sanity check: skip over lws pollin callbacks that are not
            for our expected wsi, as we have not requested those (lws
            ping/pong routines typically)
            */
            break;
         }

         auto iter = wsPtr->writeMap_.find(wsi);
         if (iter == wsPtr->writeMap_.end()) {
            wsPtr->pendingWrites_.erase(wsPtr->pendingWritesIter_++);
            LOGWARN << "incrementing over missing wsi write list";
            break;
         }

         if (iter->second.empty()) {
            wsPtr->pendingWrites_.erase(wsPtr->pendingWritesIter_++);
            LOGWARN << "incrementing over empty wsi write list";
            break;
         }

         auto& theList = iter->second.front();
         auto& packet = theList.front();
         auto body = (uint8_t*)packet.getPtr() + LWS_PRE;

         auto m = lws_write(wsi,
            body, packet.getSize() - LWS_PRE,
            LWS_WRITE_BINARY);

         if (m != (int)packet.getSize() - (int)LWS_PRE) {
            LOGERR << "failed to send packet of size";
            LOGERR << "packet is " << packet.getSize() <<
               " bytes, sent " << m << " bytes";
         }

         theList.pop_front();
         if (theList.empty()) {
            iter->second.pop_front();
            if (iter->second.empty())
            {
               wsPtr->pendingWrites_.erase(wsPtr->pendingWritesIter_++);
               break;
            }
         }

         ++wsPtr->pendingWritesIter_;
         break;
      }

      default:
         break;
   }

   return 0;
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::initAuthPeers(const PassphraseLambda& passLbd)
{
   //init auth peer object
   auto instance = getInstance();
   if (!Armory::Config::NetworkSettings::ephemeralPeers()) {
      string peerFilename(SERVER_AUTH_PEER_FILENAME);
      instance->authorizedPeers_ = make_shared<AuthorizedPeers>(
         Armory::Config::getDataDir(), peerFilename, passLbd);
   } else {
      instance->authorizedPeers_ = make_shared<AuthorizedPeers>();
   }
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::start(BlockDataManagerThread* bdmT, bool async)
{
   shutdownPromise_ = promise<bool>();
   shutdownFuture_ = shutdownPromise_.get_future();
   auto instance = getInstance();

   //setup encinit and pubkey present packet
   BinaryWriter encInitPacket;
   encInitPacket.put_uint32_t(1);
   encInitPacket.put_uint8_t((uint8_t)ArmoryAEAD::BIP151_PayloadType::Start);
   instance->encInitPacket_ = encInitPacket.getData();
   instance->oneWayAuth_ = Armory::Config::NetworkSettings::oneWayAuth();

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
   if (parserThreads == 0) {
      parserThreads = 1;
   }
   for (unsigned i = 0; i < parserThreads; i++) {
      instance->threads_.push_back(thread(writeProcessThread));
      instance->threads_.push_back(thread(readProcessThread));
   }

   auto port = stoi(Armory::Config::NetworkSettings::listenPort());
   if (port == 0) {
      port = WEBSOCKET_PORT;
   }

   //run service thread
   if (async) {
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
   if (!lock.try_lock()) {
      return;
   }

   auto ptr = instance_.load(memory_order_relaxed);
   if (ptr == nullptr) {
      return;
   }

   auto instance = getInstance();
   if (instance->run_.load(memory_order_relaxed) == 0) {
      return;
   }

   instance->msgQueue_.terminate();
   instance->clientConnectionInterruptQueue_.terminate();
   instance->clients_->shutdown();
   instance->run_.store(0, memory_order_relaxed);
   lws_cancel_service(instance->contextPtr_);
   instance->packetQueue_.terminate();

   vector<thread::id> idVec;
   for (auto& thr : instance->threads_) {
      idVec.push_back(thr.get_id());
      if (thr.joinable()) {
         thr.join();
      }
   }

   instance->threads_.clear();
   instance_.store(nullptr, memory_order_relaxed);
   delete instance;

   try {
      shutdownPromise_.set_value(true);
   }
   catch (const future_error&) {}
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
   try {
      isReadyProm_.set_value(true);
   } catch (const future_error&) {}
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::webSocketService(int port)
{
   struct lws_context_creation_info info;
   struct lws_vhost *vhost;
   const char *iface = nullptr;
   int uid = -1, gid = -1;
   int opts = 0;
   int n = 0;

   memset(&info, 0, sizeof info);
   info.port = port;

   info.iface = iface;
   info.protocols = protocols;
   info.log_filepath = nullptr;
   //info.ws_ping_pong_interval = pp_secs;
   info.gid = gid;
   info.uid = uid;
   info.max_http_header_pool = 256;
   info.options = opts | LWS_SERVER_OPTION_VALIDATE_UTF8 | LWS_SERVER_OPTION_EXPLICIT_VHOSTS;
   info.timeout_secs = 0;
   //info.ip_limit_ah = 24; /* for testing */
   //info.ip_limit_wsi = 105; /* for testing */

   contextPtr_ = lws_create_context(&info);
   if (contextPtr_ == nullptr) {
      throw LWS_Error("failed to create LWS context");
   }

   vhost = lws_create_vhost(contextPtr_, &info);
   if (vhost == nullptr) {
      throw LWS_Error("failed to create vhost");
   }

   pendingWritesIter_ = pendingWrites_.begin();
   run_.store(1, memory_order_relaxed);
   try {
      while (run_.load(memory_order_relaxed) != 0 && n >= 0) {
         n = lws_service(contextPtr_, 10000);
         updateWriteMap();
      }
   } catch(const std::exception& e) {
      LOGERR << "server lws service choked: " << e.what();
   }

   LOGINFO << "cleaning up lws server";
   lws_vhost_destroy(vhost);
   lws_context_destroy(contextPtr_);
}

///////////////////////////////////////////////////////////////////////////////
WebSocketServer* WebSocketServer::getInstance()
{
   while (true) {
      auto ptr = instance_.load(memory_order_relaxed);
      if (ptr == nullptr) {
         unique_lock<mutex> lock(mu_);
         ptr = instance_.load(memory_order_relaxed);
         if (ptr != nullptr) {
            continue;
         }
         ptr = new WebSocketServer();
         instance_.store(ptr, memory_order_relaxed);
      }

      return ptr;
   }
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::commandThread()
{
   while (true) {
      shared_ptr<BDV_packet> packetPtr;
      try {
         packetPtr = move(packetQueue_.pop_front());
      } catch (const StopBlockingLoop&) {
         //end loop condition
         return;
      }

      if (packetPtr == nullptr) {
         LOGWARN << "empty command packet";
         continue;
      }

      //get connection state object
      auto stateMap = getConnectionStateMap();
      auto iter = stateMap->find(packetPtr->bdvID_);
      if (iter == stateMap->end()) {
         //missing state map, kill connection
         continue;
      }

      iter->second.readQueue_->push_back(std::move(packetPtr->data_));
      clientConnectionInterruptQueue_.push_back(std::move(packetPtr->bdvID_));
   }
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::clientInterruptThread()
{
   while (true) {
      uint64_t clientId;
      try {
         clientId = clientConnectionInterruptQueue_.pop_front();
      } catch (const StopBlockingLoop&) {
         break;
      }

      auto clientMap = clientStateMap_.get();
      auto iter = clientMap->find(clientId);
      if (iter == clientMap->end()) {
         continue;
      }

      auto ccs = const_cast<ClientConnection*>(&iter->second);
      unsigned zero = 0;
      if (!ccs->readLock_->compare_exchange_weak(zero, 1)) {
         clientConnectionInterruptQueue_.push_back(move(clientId));
         continue;
      }

      ccs->processReadQueue(clients_);
      ccs->readLock_->store(0);
   }
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::write(const uint64_t& id, const uint32_t& msgid,
   std::unique_ptr<Socket_WritePayload> payload)
{
   if (payload == nullptr) {
      return;
   }

   auto msg = std::make_unique<PendingMessage>(id, msgid, std::move(payload));
   auto instance = getInstance();
   instance->msgQueue_.push_back(std::move(msg));
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::prepareWriteThread()
{
   while (true) {
      std::unique_ptr<PendingMessage> msg;
      try {
         msg = msgQueue_.pop_front();
      }
      catch (const StopBlockingLoop&) {
         break;
      }

      if (msg == nullptr) {
         continue;
      }

      auto statemap = getConnectionStateMap();
      auto stateIter = statemap->find(msg->id);
      if (stateIter == statemap->end()) {
         continue;
      }
      auto statePtr = const_cast<ClientConnection*>(&stateIter->second);

      //grab state object lock
      unsigned zero = 0;
      if (!statePtr->writeLock_->compare_exchange_weak(zero, 1)) { 
         msgQueue_.push_back(std::move(msg));
         continue;
      }

      if (!statePtr->bip151Connection_->connectionComplete()) {
         //aead session uninitialized, kill connection
         return;
      }

      //check for rekey
      {
         bool needs_rekey = false;
         auto rightnow = chrono::system_clock::now();

         if (statePtr->bip151Connection_->rekeyNeeded(
            msg->payload->getSerializedSize())) {
            needs_rekey = true;
         } else {
            auto time_sec = std::chrono::duration_cast<std::chrono::seconds>(
               rightnow - statePtr->outKeyTimePoint_);
            if (time_sec.count() >= AEAD_REKEY_INVERVAL_SECONDS) {
               needs_rekey = true;
            }
         }

         if (needs_rekey) {
            //create rekey packet
            BinaryData rekeyPacket(BIP151PUBKEYSIZE);
            memset(rekeyPacket.getPtr(), 0, BIP151PUBKEYSIZE);
            
            SerializedMessage ws_msg;
            ws_msg.construct(
               rekeyPacket.getDataVector(),
               statePtr->bip151Connection_.get(),
               ArmoryAEAD::BIP151_PayloadType::Rekey);

            //push to write map
            writeToSocket(statePtr->wsiPtr_, ws_msg);

            //rekey outer bip151 channel
            statePtr->bip151Connection_->rekeyOuterSession();

            //set outkey timepoint to rightnow
            statePtr->outKeyTimePoint_ = rightnow;
         }
      }

      SerializedMessage ws_msg;
      ws_msg.construct(std::move(msg->payload),
         statePtr->bip151Connection_.get(), msg->msgid);

      //push to write map
      writeToSocket(statePtr->wsiPtr_, ws_msg);

      //reset lock
      statePtr->writeLock_->store(0);
   }
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::waitOnShutdown()
{
   try {
      shutdownFuture_.get();
   }
   catch (const std::future_error&) {}
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<const map<uint64_t, ClientConnection>> 
   WebSocketServer::getConnectionStateMap() const
{
   return clientStateMap_.get();
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::addId(const uint64_t& id, struct lws* ptr)
{
   auto&& lbds = getAuthPeerLambda();
   auto&& write_pair = make_pair(
      id, ClientConnection(ptr, id, lbds, oneWayAuth_));
   clientStateMap_.insert(move(write_pair));
   writeMap_.emplace(ptr, list<list<BinaryData>>());
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::eraseId(const uint64_t& id, struct lws* ptr)
{
   clientStateMap_.erase(id);
   writeMap_.erase(ptr);
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
   if (iter == clientStateMap->end()) {
      //invalid client id, return
      return;
   }

   auto cc = const_cast<ClientConnection*>(&iter->second);
   cc->closeConnection();
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::writeToSocket(struct lws* ptr, SerializedMessage& msg)
{
   list<BinaryData> packetList;
   while (!msg.isDone()) {
      packetList.emplace_back(move(msg.consumeNextPacket()));
   }

   auto thePair = std::make_pair(ptr, std::move(packetList));
   writeQueue_.push_back(std::move(thePair));
   lws_cancel_service(contextPtr_);
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::updateWriteMap()
{
   try {
      while (true) {
         auto packetList = std::move(writeQueue_.pop_front());
         auto iter = writeMap_.find(packetList.first);
         if (iter == writeMap_.end()) {
            continue;
         }

         iter->second.emplace_back(move(packetList.second));
         pendingWrites_.insert(packetList.first);
         break;
      }
   }
   catch (const IsEmpty&) {}

   //round robin write activation
   if (pendingWrites_.empty()) {
      return;
   }

   if (pendingWritesIter_ == pendingWrites_.end()) {
      pendingWritesIter_ = pendingWrites_.begin();
   }
   lws_callback_on_writable(*pendingWritesIter_);
}

///////////////////////////////////////////////////////////////////////////////
//
// ClientConnection
//
///////////////////////////////////////////////////////////////////////////////
ClientConnection::ClientConnection(
   struct lws *wsi, uint64_t id, AuthPeersLambdas& lbds, bool isOneWayAuth) :
   wsiPtr_(wsi), id_(id)
{
   bip151Connection_ = std::make_shared<BIP151Connection>(lbds, isOneWayAuth);

   writeLock_ = std::make_shared<std::atomic<unsigned>>();
   writeLock_->store(0);

   readLock_ = std::make_shared<std::atomic<unsigned>>();
   readLock_->store(0);

   readQueue_ = std::make_shared<Queue<BinaryData>>();

   run_ = std::make_shared<std::atomic<int>>();
   run_->store(0, std::memory_order_relaxed);
}

///////////////////////////////////////////////////////////////////////////////
void ClientConnection::processReadQueue(std::shared_ptr<Clients> clients)
{
   while (run_->load(std::memory_order_relaxed) != -1) {
      BinaryData packetData;
      try {
         packetData = std::move(readQueue_->pop_front());
      } catch (const IsEmpty&) {
         //end loop condition
         return;
      }

      if (packetData.empty()) {
         LOGWARN << "empty command packet";
         continue;
      }

      if (!readLeftOverData_.empty()) {
         readLeftOverData_.append(packetData);
         packetData = std::move(readLeftOverData_);
         readLeftOverData_.clear();
      }

      if (bip151Connection_->connectionComplete()) {
         if (packetData.getSize() < POLY1305MACLEN + 4) {
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

         if (result != 0) {
            if (result <= 65536 && result > -1) {
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

      auto msgType = WebSocketMessagePartial::readPacketType(
         packetData.getRef());
      if (msgType > ArmoryAEAD::BIP151_PayloadType::Threshold_Begin) {
         processAEADHandshake(move(packetData));
         continue;
      }

      if (bip151Connection_->getBIP150State() != BIP150State::SUCCESS) {
         //can't get this far without fully setup AEAD
         closeConnection();
         continue;
      }

      BinaryDataRef bdr((uint8_t*)&id_, 8);
      auto hexID = bdr.toHexStr();
      auto bdvPtr = clients->get(hexID);

      //create payload
      auto bdv_payload = std::make_shared<BDV_Payload>();
      bdv_payload->bdvPtr_ = bdvPtr; //can be nullptr
      bdv_payload->packetData_ = std::move(packetData);
      bdv_payload->bdvID_ = id_;
      bdv_payload->hexID = hexID;

      //queue for clients thread pool to process
      clients->queuePayload(bdv_payload);
   }
}

///////////////////////////////////////////////////////////////////////////////
void ClientConnection::processAEADHandshake(BinaryData msg)
{
   auto writeToClient = [this](const BinaryDataRef& msg,
      ArmoryAEAD::BIP151_PayloadType type, bool encrypt)->void
   {
      BIP151Connection* connPtr = nullptr;
      if (encrypt) {
         connPtr = bip151Connection_.get();
      }
      SerializedMessage aeadMsg;
      aeadMsg.construct(msg, connPtr, type);

      auto instance = WebSocketServer::getInstance();
      instance->writeToSocket(wsiPtr_, aeadMsg);
   };

   auto processHandshake = [this, &writeToClient](BinaryData& msgdata)->bool
   {
      WebSocketMessagePartial wsMsg;
      if (!wsMsg.parsePacket(msgdata) || !wsMsg.isReady()) {
         //invalid packet
         return false;
      }

      auto dataBdr = wsMsg.getSingleBinaryMessage();
      switch (wsMsg.getType())
      {
         case ArmoryAEAD::BIP151_PayloadType::Start:
         {
            /*
            Announce server pubkey if it's public. This is done without encryption.
            Users should not accept unknown keys. It also reveals what server you
            are talking to, do not expect anonimity on the clearnet or over something
            like a Tor exit node.
            */
            if (bip151Connection_->isOneWayAuth()) {
               writeToClient(bip151Connection_->getOwnPubKey(),
                  ArmoryAEAD::BIP151_PayloadType::PresentPubKey,
                  false);
            }
            break;
         }

         default:
            break;
      }

      auto status = ArmoryAEAD::BIP15x_Handshake::serverSideHandshake(
         bip151Connection_.get(), wsMsg.getType(), dataBdr, writeToClient);
      switch (status)
      {
         case ArmoryAEAD::HandshakeState::StepSuccessful:
            return true;

         case ArmoryAEAD::HandshakeState::Completed:
         {
            outKeyTimePoint_ = chrono::system_clock::now();
            return true;
         }

         default:
            return false;
      }
   };

   if (!processHandshake(msg)) {
      closeConnection();
   }
}

///////////////////////////////////////////////////////////////////////////////
void ClientConnection::closeConnection()
{
   run_->store(-1, memory_order_relaxed);
}