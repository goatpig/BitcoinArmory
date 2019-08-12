////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "WebSocketClient.h"

using namespace std;

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
WebSocketClient::WebSocketClient(const string& addr, const string& port,
   const string& datadir, const bool& ephemeralPeers,
   const bool& overrideBIP150AuthMode, const bool& newBIP150AuthMode,
   shared_ptr<RemoteCallback> cbPtr) :
   SocketPrototype(addr, port, false), callbackPtr_(cbPtr)
{
   count_.store(0, std::memory_order_relaxed);
   requestID_.store(0, std::memory_order_relaxed);

   std::string filename(CLIENT_AUTH_PEER_FILENAME);
   if (!ephemeralPeers)
   {
      authPeers_ = make_shared<AuthorizedPeers>(
         datadir, filename);
   }
   else
   {
      authPeers_ = make_shared<AuthorizedPeers>();
   }

   // We may wish to override the global BIP 150 authentication mode setting.
   auto lbds = getAuthPeerLambda();
   bool bip150AuthMode = ::publicRequester;
   if (overrideBIP150AuthMode)
   {
      bip150AuthMode = newBIP150AuthMode;
   }
   bip151Connection_ = make_shared<BIP151Connection>(lbds, bip150AuthMode);
}


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

   write_payload->id_ = id;
   writeSerializationQueue_.push_back(move(write_payload));
}

////////////////////////////////////////////////////////////////////////////////
void WebSocketClient::writeService()
{
   while (true)
   {
      unique_ptr<Socket_WritePayload> message;
      try
      {
         message = move(writeSerializationQueue_.pop_front());
      }
      catch (StopBlockingLoop&)
      {
         break;
      }

      vector<uint8_t> data;
      message->serialize(data);

      //push packets to write queue
      if (!bip151Connection_->connectionComplete())
         throw LWS_Error("invalid aead state");

      //check for rekey
      {
         bool needs_rekey = false;
         auto rightnow = chrono::system_clock::now();

         if (bip151Connection_->rekeyNeeded(message->getSerializedSize()))
         {
            needs_rekey = true;
         }
         else
         {
            auto time_sec = chrono::duration_cast<chrono::seconds>(
               rightnow - outKeyTimePoint_);
            if (time_sec.count() >= AEAD_REKEY_INVERVAL_SECONDS)
               needs_rekey = true;
         }

         if (needs_rekey)
         {
            BinaryData rekeyPacket(BIP151PUBKEYSIZE);
            memset(rekeyPacket.getPtr(), 0, BIP151PUBKEYSIZE);

            SerializedMessage rekey_msg;
            rekey_msg.construct(
               rekeyPacket.getDataVector(),
               bip151Connection_.get(), WS_MSGTYPE_AEAD_REKEY);

            writeQueue_.push_back(move(rekey_msg));
            bip151Connection_->rekeyOuterSession();
            outKeyTimePoint_ = rightnow;
            ++outerRekeyCount_;
         }
      }

      SerializedMessage ws_msg;
      ws_msg.construct(
         data, bip151Connection_.get(), 
         WS_MSGTYPE_FRAGMENTEDPACKET_HEADER, message->id_);

      writeQueue_.push_back(move(ws_msg));

      //trigger write callback
      auto wsiptr = (struct lws*)wsiPtr_.load(memory_order_relaxed);
      if (wsiptr == nullptr)
         throw LWS_Error("invalid lws instance");
      if (lws_callback_on_writable(wsiptr) < 1)
         throw LWS_Error("invalid lws instance");
   }
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
   info.gid = -1;
   info.uid = -1;

   //1 min ping/pong
   info.ws_ping_pong_interval = 60;

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
   if(lws_parse_uri((char*)addr_.c_str(), &prot, &i.address, &i.port, &p) !=0)
   {
      LOGERR << "failed to parse server URI";
      throw LWS_Error("failed to parse server URI");
   }

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
   auto connectedFut = connectionReadyProm_.get_future();

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

   auto writeLBD = [this](void)->void
   {
      this->writeService();
   };

   writeThr_ = thread(writeLBD);

   return connectedFut.get();
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
   writeSerializationQueue_.terminate();
   readQueue_.terminate();

   try
   {
      if (writeThr_.joinable())
         writeThr_.join();

      if(readThr_.joinable())
         readThr_.join();
   }
   catch(system_error& e)
   {
      LOGERR << "failed to join on client threads with error:";
      LOGERR << e.what();

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
      if (instance != nullptr)
         instance->connected_.store(true, memory_order_release);

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
         {
            instance->callbackPtr_->disconnected();
            try
            {
               instance->connectionReadyProm_.set_value(false);
            }
            catch(future_error&)
            { }
         }

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

      if (m != (int)packet.getSize() - (int)LWS_PRE)
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

      if (leftOverData_.getSize() != 0)
      {
         leftOverData_.append(payload);
         payload = move(leftOverData_);
         leftOverData_.clear();
      }

      if (bip151Connection_->connectionComplete())
      {
         //decrypt packet
         auto result = bip151Connection_->decryptPacket(
            payload.getPtr(), payload.getSize(),
            payload.getPtr(), payload.getSize());

         if (result != 0)
         {
            //see WebSocketServer::commandThread for the explainantion
            if (result <= WEBSOCKET_MESSAGE_PACKET_SIZE && result > -1)
            {
               leftOverData_ = move(payload);
               continue;
            }

            shutdown();
            return;
         }

         payload.resize(payload.getSize() - POLY1305MACLEN);
      }

      //deser packet
      auto payloadRef = currentReadMessage_.insertDataAndGetRef(payload);
      auto result = 
         currentReadMessage_.message_.parsePacket(payloadRef);
      if (!result)
      {
         currentReadMessage_.reset();
         continue;
      }

      if (!currentReadMessage_.message_.isReady())
         continue;

      if (currentReadMessage_.message_.getType() > WS_MSGTYPE_AEAD_THESHOLD)
      {
         if (!processAEADHandshake(currentReadMessage_.message_))
         {
            //invalid AEAD message, kill connection
            shutdown();
            return;
         }

         currentReadMessage_.reset();
         continue;
      }

      if (bip151Connection_->getBIP150State() != BIP150State::SUCCESS)
      {
         LOGWARN << "encryption layer is uninitialized, aborting connection";
         shutdown();
         return;
      }

      //figure out request id, fulfill promise
      auto& msgid = currentReadMessage_.message_.getId();
      switch (msgid)
      {
      case WEBSOCKET_CALLBACK_ID:
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

         break;
      }

      default:
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
         else
         {
            LOGWARN << "invalid msg id";
            currentReadMessage_.reset();
         }
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
bool WebSocketClient::processAEADHandshake(const WebSocketMessagePartial& msgObj)
{
   auto writeData = [this](BinaryData& payload, uint8_t type, bool encrypt)
   {
      SerializedMessage msg;
      BIP151Connection* connPtr = nullptr;
      if (encrypt)
         connPtr = bip151Connection_.get();

      msg.construct(payload.getDataVector(), connPtr, type);
      writeQueue_.push_back(move(msg));

      //trigger write callback
      auto wsiptr = (struct lws*)wsiPtr_.load(memory_order_relaxed);
      if (wsiptr == nullptr)
         throw LWS_Error("invalid lws instance");
      if (lws_callback_on_writable(wsiptr) < 1)
         throw LWS_Error("invalid lws instance");
   };

   auto msgbdr = msgObj.getSingleBinaryMessage();
   switch (msgObj.getType())
   {
   case WS_MSGTYPE_AEAD_PRESENT_PUBKEY:
   {
      /*packet is server's pubkey, do we have it?*/
      
      //init server promise
      serverPubkeyProm_ = make_shared<promise<bool>>();

      //compute server name
      stringstream ss;
      ss << addr_ << ":" << port_;

      if (!bip151Connection_->havePublicKey(msgbdr, ss.str()))
      {
         //we don't have this key, call user prompt lambda
         promptUser(msgbdr, ss.str());
      }
      else
      {
         //set server key promise
         serverPubkeyProm_->set_value(true);
      }

      break;
   }

   case WS_MSGTYPE_AEAD_ENCINIT:
   {
      if (bip151Connection_->processEncinit(
         msgbdr.getPtr(), msgbdr.getSize(), false) != 0)
         return false;

      //valid encinit, send client side encack
      BinaryData encackPayload(BIP151PUBKEYSIZE);
      if (bip151Connection_->getEncackData(
         encackPayload.getPtr(), BIP151PUBKEYSIZE) != 0)
      {
         return false;
      }
      
      writeData(encackPayload, WS_MSGTYPE_AEAD_ENCACK, false);

      //start client side encinit
      BinaryData encinitPayload(ENCINITMSGSIZE);
      if (bip151Connection_->getEncinitData(
         encinitPayload.getPtr(), ENCINITMSGSIZE,
         BIP151SymCiphers::CHACHA20POLY1305_OPENSSH) != 0)
      {
         return false;
      }

      writeData(encinitPayload, WS_MSGTYPE_AEAD_ENCINIT, false);

      break;
   }
   case WS_MSGTYPE_AEAD_ENCACK:
   {
      if (bip151Connection_->processEncack(
         msgbdr.getPtr(), msgbdr.getSize(), true) == -1)
         return false;

      //have we seen the server's pubkey?
      if (serverPubkeyProm_ != nullptr)
      {
         //if so, wait on the promise
         auto serverProm = serverPubkeyProm_;
         auto fut = serverProm->get_future();
         fut.wait();
         serverPubkeyProm_.reset();
      }

      //bip151 handshake completed, time for bip150
      stringstream ss;
      ss << addr_ << ":" << port_;

      BinaryData authchallengeBuf(BIP151PRVKEYSIZE);
      if (bip151Connection_->getAuthchallengeData(
         authchallengeBuf.getPtr(),
         authchallengeBuf.getSize(),
         ss.str(),
         true, //true: auth challenge step #1 of 6
         false) != 0) //false: have not processed an auth propose yet
      {
         return false;
      }

      writeData(authchallengeBuf, WS_MSGTYPE_AUTH_CHALLENGE, true);

      break;
   }

   case WS_MSGTYPE_AEAD_REKEY:
   {
      //rekey requests before auth are invalid
      if (bip151Connection_->getBIP150State() != BIP150State::SUCCESS)
         return false;

      //if connection is already setup, we only accept rekey enack messages
      if (bip151Connection_->processEncack(
         msgbdr.getPtr(), msgbdr.getSize(), false) == -1)
         return false;

      ++innerRekeyCount_;
      break;
   }

   case WS_MSGTYPE_AUTH_REPLY:
   {
      if (bip151Connection_->processAuthreply(
         msgbdr.getPtr(),
         msgbdr.getSize(),
         true, //true: step #2 out of 6
         false) != 0) //false: haven't seen an auth challenge yet
      {
         return false;
      }

      BinaryData authproposeBuf(BIP151PRVKEYSIZE);
      if (bip151Connection_->getAuthproposeData(
         authproposeBuf.getPtr(),
         authproposeBuf.getSize()) != 0)
      {
         return false;
      }

      writeData(authproposeBuf, WS_MSGTYPE_AUTH_PROPOSE, true);

      break;
   }
   case WS_MSGTYPE_AUTH_CHALLENGE:
   {
      bool goodChallenge = true;
      auto challengeResult =
         bip151Connection_->processAuthchallenge(
            msgbdr.getPtr(),
            msgbdr.getSize(),
            false); //true: step #4 of 6

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
      auto validReply = bip151Connection_->getAuthreplyData(
         authreplyBuf.getPtr(),
         authreplyBuf.getSize(),
         false, //true: step #5 of 6
         goodChallenge);

      writeData(authreplyBuf, WS_MSGTYPE_AUTH_REPLY, true);

      if (validReply != 0)
      {
         //auth setup failure, kill connection
         return false;
      }

      //rekey
      bip151Connection_->bip150HandshakeRekey();
      outKeyTimePoint_ = chrono::system_clock::now();

      //flag connection as ready
      connectionReadyProm_.set_value(true);

      break;
   }

   default:
      return false;
   }

   return true;
}

///////////////////////////////////////////////////////////////////////////////
AuthPeersLambdas WebSocketClient::getAuthPeerLambda(void) const
{
   auto authPeerPtr = authPeers_;

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
void WebSocketClient::addPublicKey(const SecureBinaryData& pubkey)
{
   stringstream ss;
   ss << addr_ << ":" << port_;

   authPeers_->addPeer(pubkey, ss.str());
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketClient::setPubkeyPromptLambda(
   std::function<bool(const BinaryData&, const std::string&)> lbd)
{
   userPromptLambda_ = lbd;
}

///////////////////////////////////////////////////////////////////////////////
void WebSocketClient::promptUser(
   const BinaryDataRef& keyRef, const string& name)
{
   //is prompt lambda set?
   if (!userPromptLambda_)
   {
      serverPubkeyProm_->set_value(false);
      return;
   }

   BinaryData key_copy(keyRef);

   //create lambda to handle user prompt
   auto promptLbd = [this, key_copy, name](void)->void
   {
      if (this->userPromptLambda_(key_copy, name))
      {
         //the lambda returns true, the user accepted the key, add it to peers
         std::vector<std::string> nameVec;
         nameVec.push_back(name);
         this->authPeers_->addPeer(key_copy, nameVec);
         serverPubkeyProm_->set_value(true);
      }
      else
      {
         //otherwise, we still have to set the promise so that the auth 
         //challenge leg can progress
         serverPubkeyProm_->set_value(false);
      }
   };

   //run prompt in new thread
   thread thr(promptLbd);
   if (thr.joinable())
      thr.detach();
}
