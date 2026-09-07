////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////


#include "Server.h"
#include <Utils/ArmoryConfig.h>
#include <Utils/Cryptography.h>
#include <Utils/BIP150_151.h>
#include <Utils/BIP15x_Handshake.h>
#include <Wallets/AuthorizedPeers.h>
#include <Ledgers/LedgerEntry.h>
#include <Network/WebSocketMessage.h>
#include "BDM_Server.h"

using namespace Armory;

///////////////////////////////////////////////////////////////////////////////
// PendingMessage
PendingMessage::PendingMessage(uint64_t id,
   std::unique_ptr<Network::Socket_WritePayload> ptr) :
   id(id), payload(std::move(ptr))
{}

///////////////////////////////////////////////////////////////////////////////
// WebSocketServer
std::atomic<WebSocketServer*> WebSocketServer::instance_;
std::mutex WebSocketServer::mu_;
std::promise<bool> WebSocketServer::shutdownPromise_;
std::shared_future<bool> WebSocketServer::shutdownFuture_;

WebSocketServer::WebSocketServer()
{}

static struct lws_protocols protocols[] = {
   /* first protocol for server must always be HTTP handler */
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
      WebSocketServer::lwsServiceHandler,
      sizeof(struct per_session_data__bdv),
      per_session_data__bdv::rcv_size,
      2,
      nullptr,
      0
   },
   /* terminator */
   { NULL, NULL, 0, 0, 0, NULL, 0 }
};

int callback_http(struct lws*, enum lws_callback_reasons,
   void*, void*, size_t)
{
   return 0;
}

///////
int WebSocketServer::lwsServiceHandler(struct lws* wsi,
   enum lws_callback_reasons reason,
   void* user, void* in, size_t len)
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
         auto bdid = Cryptography::PRNG::generateRandomStrong(8);
         LOGINFO << "new connection: " << bdid.toHexStr();
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
         if (instance == nullptr) {
            return 0;
         }
         LOGINFO << std::format("connection closed: {:x}", session_data->id_);
         instance->clients_->unregisterBDV(session_data->id_);
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
         auto packetPtr = std::make_shared<BDV_packet>(session_data->id_);
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
void WebSocketServer::initPeerStore(
   const Wallets::IO::ReadOnlyFileParams& params)
{
   //init auth peer object
   if (!Config::NetworkSettings::ephemeralPeers()) {
      initPeerStore(std::make_shared<NetworkPeers::ServerStore>(params));
   } else {
      if (Config::NetworkSettings::oneWayAuth()) {
         throw std::runtime_error(
            "--public and --ephemeral are mutually exclusive");
      }

      //setup server with an ephemeral key store
      auto instance = getInstance();
      instance->peerStore_ = std::make_shared<NetworkPeers::ServerStore>();

      //grab master pubkey
      auto keyPtr = std::getenv("MASTER_PUBKEY");
      if (keyPtr == nullptr) {
         throw std::runtime_error("caller key is not set");
      }
      auto masterKey = NetworkPeers::PeerKey::fromHumanReadable({keyPtr});

      //inject master pubkey in the store
      instance->peerStore_->addPeer(
         masterKey,
         {std::format("127.0.0.1:{}", Config::NetworkSettings::dbPort())}, 
         {}
      );

      //set master key
      if (!instance->peerStore_->setMasterKey(masterKey)) {
         throw std::runtime_error("ephemeral peers db setup snafu");
      }
   }
}

////
void WebSocketServer::initPeerStore(
   std::shared_ptr<NetworkPeers::ServerStore> peers)
{
   if (Config::NetworkSettings::ephemeralPeers()) {
      throw std::runtime_error("no peers store loading on ephemeral peers");
   }
   auto instance = getInstance();
   instance->peerStore_ = peers;
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::init()
{
   std::unique_lock<std::mutex> lock(mu_);
   auto ptr = instance_.load(std::memory_order_relaxed);
   if (ptr != nullptr) {
      return;
   }
   ptr = new WebSocketServer();
   instance_.store(ptr, std::memory_order_relaxed);
}

void WebSocketServer::start(std::shared_ptr<BlockDataManager> bdm, bool async)
{
   shutdownPromise_ = std::promise<bool>();
   shutdownFuture_ = shutdownPromise_.get_future();
   auto instance = getInstance();

   //setup encinit and pubkey present packet
   BinaryWriter encInitPacket;
   encInitPacket.put_uint32_t(1);
   encInitPacket.put_uint8_t((uint8_t)ArmoryAEAD::BIP151_PayloadType::Start);
   instance->encInitPacket_ = encInitPacket.getData();
   instance->oneWayAuth_ = Config::NetworkSettings::oneWayAuth();

   //init Clients object
   if (instance->clients_) {
      throw std::runtime_error("WS server is already started");
   }
   instance->clients_ = std::make_shared<Clients>(bdm);
   instance->clients_->init();

   //start command threads
   instance->threads_.push_back(std::thread(
      [instance]{ instance->commandThread(); }));

   //read & write threads
   unsigned parserThreads = std::thread::hardware_concurrency() / 4;
   if (parserThreads == 0) {
      parserThreads = 1;
   }
   for (unsigned i = 0; i < parserThreads; i++) {
      instance->threads_.push_back(std::thread(
         [instance]{ instance->prepareWriteThread(); }));
      instance->threads_.push_back(std::thread(
         [instance]{ instance->clientInterruptThread(); }));
   }

   auto port = stoi(Config::NetworkSettings::dbPort());
   if (port == 0) {
      port = WEBSOCKET_PORT;
   }

   //run service thread
   if (async) {
      auto loopthr = [instance, port](void)->void
      {
         instance->async_ = true;
         instance->webSocketService(port);
         instance_.store(nullptr, std::memory_order_relaxed);
      };
      auto fut = instance->isReadyProm_.get_future();
      instance->threads_.push_back(std::thread(loopthr));
      fut.get();
   } else {
      instance->webSocketService(port);
      instance_.store(nullptr, std::memory_order_relaxed);
      delete instance;
   }
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::shutdown()
{
   std::unique_lock<std::mutex> lock(mu_, std::defer_lock);
   if (!lock.try_lock()) {
      return;
   }

   auto instance = instance_.load(std::memory_order_relaxed);
   if (instance == nullptr) {
      return;
   }
   if (instance->run_.load(std::memory_order_relaxed) == 0) {
      return;
   }

   LOGINFO << "proceeding to WS server shutdown";
   instance->msgQueue_.terminate();
   instance->clientConnectionInterruptQueue_.terminate();
   instance->clients_->shutdown();
   instance->run_.store(0, std::memory_order_relaxed);
   lws_cancel_service(instance->contextPtr_);
   instance->packetQueue_.terminate();

   for (auto& thr : instance->threads_) {
      if (thr.joinable()) {
         thr.join();
      }
   }
   instance->threads_.clear();

   if (instance->async_) {
      while (instance_.load(std::memory_order_relaxed) != nullptr);
      delete instance;
   }

   try {
      shutdownPromise_.set_value(true);
   } catch (const std::future_error&) {}
   LOGINFO << "WS server shutdown sequence has completed";
}

///////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& WebSocketServer::getOwnPublicKey()
{
   auto instance = getInstance();
   if (instance == nullptr || instance->peerStore_ == nullptr) {
      throw std::runtime_error("null instance/store");
   }
   return instance->peerStore_->getOwnPublicKey();
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::setIsReady()
{
   try {
      isReadyProm_.set_value(true);
   } catch (const std::future_error&) {}
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
   run_.store(1, std::memory_order_relaxed);

   if (Config::NetworkSettings::ephemeralPeers()) {
      /*
      DB is automated by client, output pubkey to stdout to complete AEAD
      key share.
      We do this at this stage to make sure the server is ready and listening
      before the client tries to connect.
      */
      NetworkPeers::PeerKey myKey{
         peerStore_->getOwnPublicKey(),
         NetworkPeers::PeerType::ServerTwoWay
      };
      std::cout << myKey.toHumanReadable() << std::endl;

      //set stdout to nullptr to suppress any further cout
      std::cout.rdbuf(nullptr);
   }

   try {
      while (run_.load(std::memory_order_relaxed) != 0 && n >= 0) {
         n = lws_service(contextPtr_, 10000);
         updateWriteMap();
      }
   } catch (const std::exception& e) {
      LOGERR << "server lws service choked: " << e.what();
   }

   LOGINFO << "cleaning up lws server";
   lws_vhost_destroy(vhost);
   lws_context_destroy(contextPtr_);
}

///////////////////////////////////////////////////////////////////////////////
WebSocketServer* WebSocketServer::getInstance()
{
   return instance_.load(std::memory_order_relaxed);
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::commandThread()
{
   while (true) {
      std::shared_ptr<BDV_packet> packetPtr;
      try {
         packetPtr = std::move(packetQueue_.pop_front());
      } catch (const Threading::StopBlockingLoop&) {
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
      } catch (const Threading::StopBlockingLoop&) {
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
         clientConnectionInterruptQueue_.push_back(std::move(clientId));
         continue;
      }

      ccs->processReadQueue(clients_);
      ccs->readLock_->store(0);
   }
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::write(const uint64_t& id,
   std::unique_ptr<Network::Socket_WritePayload> payload)
{
   if (payload == nullptr) {
      return;
   }

   auto msg = std::make_unique<PendingMessage>(id, std::move(payload));
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
      catch (const Threading::StopBlockingLoop&) {
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
         auto rightnow = std::chrono::system_clock::now();

         if (statePtr->bip151Connection_->rekeyNeeded(
            msg->payload->getSerializedSize())) {
            needs_rekey = true;
         } else {
            auto time_sec = std::chrono::duration_cast<std::chrono::seconds>(
               rightnow - statePtr->outKeyTimePoint_);
            if (time_sec.count() >= AEAD_REKEY_INTVERVAL_SECONDS) {
               needs_rekey = true;
            }
         }

         if (needs_rekey) {
            //create rekey packet
            BinaryData rekeyPacket;
            rekeyPacket.resize(BIP151PUBKEYSIZE);
            memset(rekeyPacket.getPtr(), 0, BIP151PUBKEYSIZE);

            Network::SerializedMessage ws_msg;
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

      Network::SerializedMessage ws_msg;
      ws_msg.construct(std::move(msg->payload),
         statePtr->bip151Connection_.get());

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
std::shared_ptr<const std::map<uint64_t, ClientConnection>>
WebSocketServer::getConnectionStateMap() const
{
   return clientStateMap_.get();
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::addId(const uint64_t& id, struct lws* ptr)
{
   auto write_pair = std::make_pair(
      id, ClientConnection(ptr, id, peerStore_->getView(), oneWayAuth_));
   clientStateMap_.insert(std::move(write_pair));
   writeMap_.emplace(ptr, std::list<std::list<BinaryData>>());
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::eraseId(const uint64_t& id, struct lws* ptr)
{
   closeClientConnection(id);
   writeMap_.erase(ptr);
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
   if (cc->isMaster()) {
      clients_->setMasterIsConnected(false);
   }
   clientStateMap_.erase(id);
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketServer::writeToSocket(
   struct lws* ptr, Network::SerializedMessage& msg)
{
   std::list<BinaryData> packetList;
   while (!msg.isDone()) {
      packetList.emplace_back(std::move(msg.consumeNextPacket()));
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
         auto packetList = writeQueue_.pop_front();
         auto iter = writeMap_.find(packetList.first);
         if (iter == writeMap_.end()) {
            continue;
         }

         iter->second.emplace_back(move(packetList.second));
         pendingWrites_.insert(packetList.first);
         break;
      }
   } catch (const Threading::IsEmpty&) {}

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
bool WebSocketServer::isMasterKey(BinaryDataRef pubkey)
{
   auto instance = getInstance();
   if (instance == nullptr) {
      return false;
   }
   if (instance->peerStore_ == nullptr) {
      return false;
   }
   return instance->peerStore_->isMasterKey(pubkey);
}

///////////////////////////////////////////////////////////////////////////////
//
// ClientConnection
//
///////////////////////////////////////////////////////////////////////////////
ClientConnection::ClientConnection(struct lws *wsi, uint64_t id,
   std::unique_ptr<NetworkPeers::PeerStoreView> view, bool isOneWayAuth) :
   wsiPtr_(wsi), id_(id)
{
   bip151Connection_ = std::make_shared<BIP151Connection>(
      std::move(view), isOneWayAuth);

   writeLock_ = std::make_shared<std::atomic<unsigned>>();
   writeLock_->store(0);

   readLock_ = std::make_shared<std::atomic<unsigned>>();
   readLock_->store(0);

   readQueue_ = std::make_shared<Threading::Queue<BinaryData>>();

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
      } catch (const Threading::IsEmpty&) {
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
            readLeftOverData_ = std::move(packetData);
            continue;
         }

         //decrypt packet
         size_t plainTextSize = packetData.getSize() - POLY1305MACLEN;
         auto result = bip151Connection_->decryptPacket(
            packetData.getRef(), packetData);

         if (result != 0) {
            if (result <= 1048576 && result > -1) {
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
               readLeftOverData_ = std::move(packetData);
               continue;
            }

            //failed to decrypt, kill connection
            closeConnection();
            continue;
         }

         packetData.resize(plainTextSize);
      }

      auto msgType = Network::WebSocketMessagePartial::readPacketType(
         packetData.getRef());
      if (msgType > ArmoryAEAD::BIP151_PayloadType::Threshold_Begin) {
         processAEADHandshake(std::move(packetData));
         if (bip151Connection_->getBIP150State() == BIP150State::SUCCESS &&
            isMaster()) {
            clients->setMasterIsConnected(true);
         }
         continue;
      }

      if (bip151Connection_->getBIP150State() != BIP150State::SUCCESS) {
         //can't get this far without fully setup AEAD
         closeConnection();
         continue;
      }

      //create payload
      auto bdvPtr = clients->get(id_);
      auto payloadPtr = std::make_shared<BDV_Payload>(
         std::move(packetData),
         bdvPtr, //can be nullptr
         id_, bip151Connection_->getChosenAuthPeerKey()
      );

      //queue for clients thread pool to process
      clients->queuePayload(payloadPtr);
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
      Network::SerializedMessage aeadMsg;
      aeadMsg.construct(msg, connPtr, type);

      auto instance = WebSocketServer::getInstance();
      instance->writeToSocket(wsiPtr_, aeadMsg);
   };

   auto processHandshake = [this, &writeToClient](BinaryData& msgdata)->bool
   {
      Network::WebSocketMessagePartial wsMsg;
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
            outKeyTimePoint_ = std::chrono::system_clock::now();
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
   run_->store(-1, std::memory_order_relaxed);
}

bool ClientConnection::isMaster() const
{
   if (!bip151Connection_ || bip151Connection_->isOneWayAuth() ||
      bip151Connection_->getBIP150State() != BIP150State::SUCCESS) {
      return false;
   }
   return WebSocketServer::isMasterKey(bip151Connection_->getChosenAuthPeerKey());
}
