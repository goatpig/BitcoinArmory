////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BridgeTestUtils.h"

////////////////////////////////////////////////////////////////////////////////
// BridgePeersManagement
////////////////////////////////////////////////////////////////////////////////
class BridgePeersManagement : public ::testing::Test
{
protected:
   struct PeerData
   {
      std::string key;
      std::set<std::string> names;
      std::string label;
   };

   void initBDM()
   {
      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();
   }

   bool loadPeersDb(bool succeed)
   {
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(4).toHexStr();
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initSetup();
         auto peersRequest = request.initPeersHelper();
         peersRequest.setLoadPeersDb(callbackId);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);
      }

      //deal with unlock request
      {
         auto reply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
            reply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);

         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            return false;
         }

         auto notif = fromBridge.getNotification();
         if (notif.which() != Codec::Bridge::Notification::UNLOCK_REQUEST) {
            return false;
         }

         //reply to unlock request
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto notifReply = toBridge.initNotification();
         notifReply.setCounter(notif.getCounter());
         if (succeed) {
            notifReply.setUnlockRequest(clientPeersDbPass_);
            notifReply.setSuccess(true);
         } else {
            notifReply.setSuccess(false);
         }

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);
      }

      //wait on success reply
      auto reply = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
         reply->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return false;
      }

      auto capnReply = fromBridge.getReply();
      return capnReply.getSuccess() && capnReply.getReferenceId() == refId;
   }

   bool createPeersDb(const std::string& passphrase)
   {
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(4).toHexStr();
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initSetup();
         auto peersRequest = request.initPeersHelper();
         peersRequest.setLoadPeersDb(callbackId);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);
      }

      //deal with setpass request
      {
         auto reply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
            reply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);

         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            return false;
         }

         auto notif = fromBridge.getNotification();
         if (notif.which() != Codec::Bridge::Notification::SET_PASSPHRASE) {
            return false;
         }

         auto passReq = notif.getSetPassphrase();
         if (passReq.which() != Codec::Bridge::Notification::SetPassphraseRequest::CONTROL_PASS) {
            return false;
         }

         //reply to setpass request
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto notifReply = toBridge.initNotification();
         notifReply.setCounter(notif.getCounter());
         notifReply.setSuccess(true);

         auto passReply = notifReply.initSetPassphrase();
         passReply.setPassphrase(passphrase);
         passReply.setKdfTargetMs(1);
         passReply.setKdfTargetMB(0);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);
      }

      //wait on success reply
      auto reply = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
         reply->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         return false;
      }

      auto capnReply = fromBridge.getReply();
      return capnReply.getSuccess() && capnReply.getReferenceId() == refId;
   }

   std::vector<PeerData> listPeers()
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initSetup();
      auto peersRequest = request.initPeersHelper();
      peersRequest.setListPeers();

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();
      if (!reply.getSuccess() || reply.getReferenceId() != refId) {
         throw std::runtime_error({});
      }

      auto replyMgr = reply.getSetup();
      auto capnPeers = replyMgr.getListPeers();

      std::vector<PeerData> peerVec;
      for (auto capnPeerData : capnPeers) {
         auto capnPeer = capnPeerData.getPeer();
         std::set<std::string> names;
         for (auto name : capnPeer.getNames()) {
            names.emplace(std::string{name});
         }
         std::string label = capnPeer.hasLabel() ? std::string(capnPeer.getLabel()) : "";
         peerVec.emplace_back(PeerData{
            std::string{capnPeer.getKey()},
            std::move(names),
            label
         });
      }
      return peerVec;
   }

   void addPeer(const BinaryData& key,
      const std::vector<std::string>& names,
      const std::string& label)
   {
      NetworkPeers::PeerKey peer{key.getRef(), NetworkPeers::PeerType::ServerOneWay};

      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initSetup();
      auto peersRequest = request.initPeersHelper();
      auto peerCapnp = peersRequest.initAddPeer();
      peerCapnp.setKey(peer.toHumanReadable());
      auto namesCapnp = peerCapnp.initNames(names.size());
      for (unsigned i = 0; i < names.size(); i++) {
         namesCapnp.set(i, names[i]);
      }
      peerCapnp.setLabel(label);

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();

      ASSERT_EQ(reply.getReferenceId(), refId);
      if (reply.getSuccess() == false) {
         std::cout << std::string(reply.getError()) << std::endl;
      }
      ASSERT_TRUE(reply.getSuccess());
   }

   void removePeer(const std::string& key)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initSetup();
      auto peersRequest = request.initPeersHelper();
      peersRequest.setRemovePeer(key);

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();

      ASSERT_EQ(reply.getReferenceId(), refId);
      if (reply.getSuccess() == false) {
         std::cout << std::string(reply.getError()) << std::endl;
      }
      ASSERT_TRUE(reply.getSuccess());
   }

   void setLabel(const std::string& key, const std::string& label)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initSetup();
      auto peersRequest = request.initPeersHelper();
      auto labelReq = peersRequest.initSetLabel();
      labelReq.setKey(key);
      labelReq.setLabel(label);

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();

      ASSERT_EQ(reply.getReferenceId(), refId);
      if (reply.getSuccess() == false) {
         std::cout << std::string(reply.getError()) << std::endl;
      }
      ASSERT_TRUE(reply.getSuccess());
   }

   /////////////////////////////////////////////////////////////////////////////
   void SetUp() override
   {
      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);

      FileUtils::createDirectory(blkdir_ / "blocks");
      FileUtils::createDirectory(homedir_);
      FileUtils::createDirectory(ldbdir_);

      Config::DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);

      // Put the first 5 blocks into the blkdir
      blk0dat_ = FileUtils::getBlkFilename(blkdir_ / "blocks", 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);

      WebSocketServer::init();
      Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_BARE",
         "--thread-count=3",
         "--public"},
         Config::ProcessType::DB);

      startupBIP151CTX();
      startupBIP150CTX(4);

      //setup auth peers for server and client
      serverPeersPassLbd_ = [](const std::set<Wallets::EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { {}, true };
      };

      auto serverPeersWlt = NetworkPeers::PeerStore::bootstrapWallet({
         homedir_ / SERVER_AUTH_PEER_FILENAME, {
            []() {
               return std::make_unique<Passphrase::PassAndParams>
               (1ms, 0, SecureBinaryData{});
            }
         }
      });
      NetworkPeers::ServerStore serverPeers(serverPeersWlt);

      auto clientPeersWlt = NetworkPeers::PeerStore::bootstrapWallet({
         homedir_ / CLIENT_AUTH_PEER_FILENAME, {
            [pass=clientPeersDbPass_]()
            { return std::make_unique<Passphrase::PassAndParams>
               (1ms, 0, SecureBinaryData::fromString(pass));
            }
         }
      });
      NetworkPeers::ClientStore clientPeers(clientPeersWlt);

      //grab public keys for client and server
      serverPubkey_ = serverPeers.getOwnPublicKey();
      clientPubKey_ = clientPeers.getOwnPublicKey();

      replyQueue.clear();
      bridge_ = std::make_shared<Bridge::CppBridge>(
         std::make_shared<TestBridgeSocket>());

      initBDM();
      auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);
      nodePtr->setBDM(theBDMt_->bdm());
   }

   void TearDown() override
   {
      bridge_.reset();
      WebSocketServer::shutdown();
      theBDMt_->shutdown();
      if (wsServerThr_.joinable()) {
         wsServerThr_.join();
      }

      delete theBDMt_;
      theBDMt_ = nullptr;

      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);
      Config::reset();
   }

   /////////////////////////////////////////////////////////////////////////////
   void startWSServer()
   {
      wsServerThr_ = std::thread([bdm=theBDMt_->bdm()](){
         auto clients = std::make_shared<Clients>(bdm);
         WebSocketServer::start(clients);
      });

      //arbitrary wait to give time to WS server to startup
      std::this_thread::sleep_for(100ms);
   }

protected:
   BlockDataManagerThread *theBDMt_;
   Passphrase::UnlockFunc serverPeersPassLbd_;
   const std::string clientPeersDbPass_{"client_peers_pass"};
   LMDBBlockDatabase* iface_;

   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;

   std::shared_ptr<Bridge::CppBridge> bridge_;
   BinaryData serverPubkey_;
   BinaryData clientPubKey_;
   std::thread wsServerThr_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgePeersManagement, ListAddConnect)
{
   WebSocketServer::initPeerStore({homedir_ / SERVER_AUTH_PEER_FILENAME, {}});
   startWSServer();
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);

   NetworkPeers::PeerKey clientPeer{clientPubKey_.getRef(), NetworkPeers::PeerType::Client};
   NetworkPeers::PeerKey serverPeer{serverPubkey_.getRef(), NetworkPeers::PeerType::ServerOneWay};
   auto clientKey = clientPeer.toHumanReadable();
   auto serverKey = serverPeer.toHumanReadable();

   //load peers db
   ASSERT_TRUE(loadPeersDb(true));

   //peer list should be empty at first, but for our own key
   auto peerList = listPeers();
   ASSERT_EQ(peerList.size(), 1);
   const auto& firstPeer = *peerList.begin();
   EXPECT_EQ(firstPeer.key, clientKey);
   ASSERT_EQ(firstPeer.names.size(), 1);
   EXPECT_EQ(*firstPeer.names.begin(), "own");
   EXPECT_EQ(firstPeer.label, "N/A");

   //try to connect to an invalid peer
   ASSERT_FALSE(connectToPeer(bridge_, serverKey));

   //add the server to peers store
   auto serverAddress = std::format("127.0.0.1:{}", Config::NetworkSettings::dbPort());
   addPeer(serverPubkey_, { serverAddress }, "the server key");

   //list again, server should appear
   peerList = listPeers();
   ASSERT_EQ(peerList.size(), 2);
   for (const auto& keyEntry : peerList) {
      if (keyEntry.key == clientKey) {
         continue;
      }
      EXPECT_EQ(keyEntry.key, serverKey);
      ASSERT_EQ(keyEntry.names.size(), 1);
      EXPECT_EQ(*keyEntry.names.begin(), serverAddress);
      EXPECT_EQ(keyEntry.label, "the server key");
   }

   //connect to db
   ASSERT_TRUE(connectToPeer(bridge_, serverKey));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgePeersManagement, LoadDeleteCreate)
{
   WebSocketServer::initPeerStore({homedir_ / SERVER_AUTH_PEER_FILENAME, {}});
   startWSServer();
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);

   NetworkPeers::PeerKey serverPeer{serverPubkey_.getRef(), NetworkPeers::PeerType::ServerOneWay};
   auto serverKey = serverPeer.toHumanReadable();

   //reject loading of peers db then delete it
   ASSERT_FALSE(loadPeersDb(false));
   ASSERT_TRUE(std::filesystem::remove(homedir_ / CLIENT_AUTH_PEER_FILENAME));

   //create from new call to loadPeersDb
   const std::string newPass{"new_client_peers_pass"};
   ASSERT_TRUE(createPeersDb(newPass));

   //peer list should be empty at first, but for our own key
   auto peerList = listPeers();
   ASSERT_EQ(peerList.size(), 1);
   const auto& firstPeer = *peerList.begin();
   ASSERT_EQ(firstPeer.names.size(), 1);
   ASSERT_EQ(*firstPeer.names.begin(), "own");
   auto clientKey = firstPeer.key;
   ASSERT_EQ(clientKey.size(), 48);

   //add the server to peers store
   auto serverAddress = std::format("127.0.0.1:{}", Config::NetworkSettings::dbPort());
   addPeer(serverPubkey_, { serverAddress }, "my serv key");

   //list again, server should appear
   peerList = listPeers();
   ASSERT_EQ(peerList.size(), 2);
   for (const auto& keyEntry : peerList) {
      if (keyEntry.key == clientKey) {
         continue;
      }
      EXPECT_EQ(keyEntry.key, serverKey);
      ASSERT_EQ(keyEntry.names.size(), 1);
      EXPECT_EQ(*keyEntry.names.begin(), serverAddress);
      EXPECT_EQ(keyEntry.label, "my serv key");
   }

   //connect to db
   ASSERT_TRUE(connectToPeer(bridge_, serverKey));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgePeersManagement, Remove)
{
   WebSocketServer::initPeerStore({homedir_ / SERVER_AUTH_PEER_FILENAME, {}});
   startWSServer();
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);

   NetworkPeers::PeerKey clientPeer{clientPubKey_.getRef(), NetworkPeers::PeerType::Client};
   NetworkPeers::PeerKey serverPeer{serverPubkey_.getRef(), NetworkPeers::PeerType::ServerOneWay};
   auto clientKey = clientPeer.toHumanReadable();
   auto serverKey = serverPeer.toHumanReadable();

   //load peers db
   ASSERT_TRUE(loadPeersDb(true));

   //peer list should be empty at first, but for our own key
   auto peerList = listPeers();
   ASSERT_EQ(peerList.size(), 1);
   const auto& firstPeer = *peerList.begin();
   EXPECT_EQ(firstPeer.key, clientKey);
   ASSERT_EQ(firstPeer.names.size(), 1);
   EXPECT_EQ(*firstPeer.names.begin(), "own");

   //try to connect to an invalid peer
   ASSERT_FALSE(connectToPeer(bridge_, "abcd"));

   //add the server to peers store
   auto serverAddress = std::format("127.0.0.1:{}", Config::NetworkSettings::dbPort());
   addPeer(serverPubkey_, { serverAddress }, "serv key");

   //also add a random key
   auto newKey = Cryptography::ECDSA::createNewPrivateKey();
   auto pubkey = Cryptography::ECDSA::computePublicKey(newKey, true);
   addPeer(pubkey, {"1.1.1.1"}, "rando key");
   NetworkPeers::PeerKey newPeer{pubkey.getRef(), NetworkPeers::PeerType::ServerOneWay};
   auto newPeerKey = newPeer.toHumanReadable();

   //list again, both keys should appear
   peerList = listPeers();
   ASSERT_EQ(peerList.size(), 3);
   int count = 0;
   for (const auto& keyEntry : peerList) {
      if (keyEntry.key == clientKey) {
         count++;
         continue;
      }
      else if (keyEntry.key == newPeerKey) {
         ASSERT_EQ(keyEntry.names.size(), 1);
         EXPECT_EQ(*keyEntry.names.begin(), "1.1.1.1");
         EXPECT_EQ(keyEntry.label, "rando key");
         count++;
      } else if (keyEntry.key == serverKey) {
         ASSERT_EQ(keyEntry.names.size(), 1);
         EXPECT_EQ(*keyEntry.names.begin(), serverAddress);
         EXPECT_EQ(keyEntry.label, "serv key");
         count++;
      } else {
         ASSERT_TRUE(false);
      }
   }
   ASSERT_EQ(count, 3);

   //delete the new key
   removePeer(newPeerKey);
   setLabel(serverKey, "updated serv key label");

   //list again, server should appear
   peerList = listPeers();
   ASSERT_EQ(peerList.size(), 2);
   for (const auto& keyEntry : peerList) {
      if (keyEntry.key == clientKey) {
         continue;
      }
      EXPECT_EQ(keyEntry.key, serverKey);
      ASSERT_EQ(keyEntry.names.size(), 1);
      EXPECT_EQ(*keyEntry.names.begin(), serverAddress);
      EXPECT_EQ(keyEntry.label, "updated serv key label");
   }

   //connect to db
   ASSERT_TRUE(connectToPeer(bridge_, serverKey));
}
