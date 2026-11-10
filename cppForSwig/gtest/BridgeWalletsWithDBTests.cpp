////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BridgeTestUtils.h"

////////////////////////////////////////////////////////////////////////////////
// BridgeWalletsWithDBTests
class BridgeWalletsWithDBTests : public ::testing::Test
{
protected:
   void initBDM()
   {
      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();
   }

   void prepareWallets()
   {
      auto pubKeyB = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrB);
      auto pubKeyC = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrC);
      auto pubKeyD = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrD);
      auto pubKeyE = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrE);

      walletId_ = createWOWallet(homedir_, {
         pubKeyB, pubKeyC, pubKeyD, pubKeyE}
      );
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
      prepareWallets();

      //setup auth peers for server and client
      authPeersPassLbd_ = [](const std::set<Wallets::EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { {}, true };
      };

      auto createWltLbd = []()
      {
         return std::make_unique<Passphrase::PassAndParams>(
            1ms, 0, SecureBinaryData{});
      };

      auto serverPeersWlt = NetworkPeers::PeerStore::bootstrapWallet({
         homedir_ / SERVER_AUTH_PEER_FILENAME, {createWltLbd}});
      NetworkPeers::ServerStore serverPeers(serverPeersWlt);

      //share public keys between client and server
      NetworkPeers::PeerKey servPK{
         serverPeers.getOwnPublicKey(),
         NetworkPeers::PeerType::ServerOneWay};
      serverPubkey_ = servPK.toHumanReadable();

      replyQueue.clear();
      bridge_ = std::make_shared<Bridge::CppBridge>(
         std::make_shared<TestBridgeSocket>());

      initBDM();
      auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);
      nodePtr->setBDM(theBDMt_->bdm());
      hexMagicBytes = Config::BitcoinSettings::getMagicBytes().toHexStr();
   }

   void TearDown() override
   {
      bridge_.reset();
      WebSocketServer::shutdown();
      if (wsServerThr_.joinable()) {
         wsServerThr_.join();
      }
      theBDMt_->shutdown();

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
         WebSocketServer::start(clients); }
      );

      //arbitrary wait to give time to WS server to startup
      std::this_thread::sleep_for(100ms);
   }

protected:
   BlockDataManagerThread *theBDMt_;
   Passphrase::UnlockFunc authPeersPassLbd_;
   LMDBBlockDatabase* iface_;

   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;

   std::string hexMagicBytes;
   std::shared_ptr<Bridge::CppBridge> bridge_;
   std::string walletId_;
   std::string serverPubkey_;
   std::thread wsServerThr_;
};

TEST_F(BridgeWalletsWithDBTests, Connect)
{
   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   auto wltId = wltList.begin()->second.walletId;
   ASSERT_FALSE(wltId.empty());
   ASSERT_EQ(wltId, walletId_);
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 1);

   ASSERT_EQ(wallets.begin()->second.walletId, wltId);
   auto accountId = wallets.begin()->second.accountId;
   ASSERT_FALSE(accountId.empty());

   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, wltId, accountId);
   ASSERT_EQ(balances.size(), 4);

   try {
      for (const auto& balPair : balances) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }
}

TEST_F(BridgeWalletsWithDBTests, CycleConnection)
{
   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   auto wltId = wltList.begin()->second.walletId;
   ASSERT_FALSE(wltId.empty());
   ASSERT_EQ(wltId, walletId_);
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 1);

   ASSERT_EQ(wallets.begin()->second.walletId, wltId);
   auto accountId = wallets.begin()->second.accountId;
   ASSERT_FALSE(accountId.empty());

   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_FALSE(connectToIp(bridge_, "127.0.0.1", 9001, {}));
   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, wltId, accountId);
   ASSERT_EQ(balances.size(), 4);

   try {
      for (const auto& balPair : balances) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //kill BDM
   WebSocketServer::shutdown();

   //wait on disconnected notif
   auto reply = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
      reply->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);

   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::NOTIFICATION);
   auto notif = fromBridge.getNotification();
   ASSERT_EQ(notif.which(), Codec::Bridge::Notification::DISCONNECTED);
   if (wsServerThr_.joinable()) {
      wsServerThr_.join();
   }

   //restart BDM
   theBDMt_->shutdown();
   delete theBDMt_;

   {
      WebSocketServer::init();
      Config::reset();
      Config::DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);
      Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_BARE",
         "--thread-count=3",
         "--public"},
         Config::ProcessType::DB);
      initBDM();
      auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);
      nodePtr->setBDM(theBDMt_->bdm());
   }

   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   balances = getAddrBalances(bridge_, wltId, accountId);
   ASSERT_EQ(balances.size(), 4);

   try {
      for (const auto& balPair : balances) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }
}

////////////////////////////////////////////////////////////////////////////////
namespace {
   //once armed, holds the bridge's server disconnected notification until
   //the gate is opened. This pauses the bridge's notification thread in the
   //db cleanup, which sends that notification last
   struct GatedBridgeSocket : public TestBridgeSocket
   {
      std::mutex gateMutex;
      std::condition_variable gateCV;
      bool armed = false;
      bool reached = false;
      bool open = false;

      void pushPayload(
         std::unique_ptr<Network::Socket_WritePayload> payload,
         std::shared_ptr<Network::Socket_ReadPayload> readPayload) override
      {
         auto payloadPtr = dynamic_cast<Bridge::WritePayload_Bridge*>(payload.get());
         if (payloadPtr != nullptr && payloadPtr->data ==
            Bridge::Notifications::Serialization::getServerDisconnectedPacket()) {
            std::unique_lock<std::mutex> lock(gateMutex);
            if (armed) {
               reached = true;
               gateCV.notify_all();

               //bounded, a failed test can't leave this thread stuck
               gateCV.wait_for(lock, 10s, [this]{ return open; });
            }
         }
         TestBridgeSocket::pushPayload(std::move(payload), std::move(readPayload));
      }

      void arm()
      {
         std::unique_lock<std::mutex> lock(gateMutex);
         armed = true;
      }

      bool waitOnGate()
      {
         std::unique_lock<std::mutex> lock(gateMutex);
         return gateCV.wait_for(lock, 10s, [this]{ return reached; });
      }

      void openGate()
      {
         {
            std::unique_lock<std::mutex> lock(gateMutex);
            open = true;
         }
         gateCV.notify_all();
      }
   };
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletsWithDBTests, DestroyDuringDisconnectCleanup)
{
   //replace the fixture's bridge with one on a gated socket
   auto gatedSocket = std::make_shared<GatedBridgeSocket>();
   bridge_ = std::make_shared<Bridge::CppBridge>(gatedSocket);

   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 1);

   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //kill BDM, the bridge's notification thread stops at the gate
   gatedSocket->arm();
   WebSocketServer::shutdown();
   if (!gatedSocket->waitOnGate()) {
      gatedSocket->openGate();
      FAIL() << "no disconnected notif from the bridge";
   }

   //destroy the bridge from another thread while its notification thread
   //is still in the db cleanup
   BridgeDestroyThread destroyThr(std::move(bridge_));

   //~CppBridge should wait on the notification thread, which waits on the gate
   auto status = destroyThr.waitFor(500ms);
   gatedSocket->openGate();
   EXPECT_EQ(status, std::future_status::timeout) <<
      "~CppBridge returned while its db cleanup was running";

   if (!destroyThr.join(10s)) {
      FAIL() << "~CppBridge did not return";
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletsWithDBTests, DestroyDuringKeyPrompt)
{
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   //connect, the bridge's notification thread waits on the reply to the
   //server key prompt. Queue the command from this thread, so that no
   //parser thread holds a copy of the bridge
   uint64_t refId = rand();
   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(4).toHexStr();
   bridge_->connectToIp("127.0.0.1", 9001, callbackId, refId);

   uint32_t promptCounter = 0;
   ASSERT_TRUE(waitOnKeyPrompt(10s, promptCounter)) << "no server key prompt";
   if (bridge_.use_count() != 1) {
      replyToKeyPrompt(bridge_, refId, promptCounter, false);
      FAIL() << "the bridge has another owner, resetting it won't destroy it";
   }

   //does not own the bridge. Only used to answer the prompt if ~CppBridge
   //doesn't return on its own: it is then still waiting on the notification
   //thread, so the bridge's members are alive
   std::shared_ptr<Bridge::CppBridge> bridgeInDtor(
      bridge_.get(), [](Bridge::CppBridge*){});

   //leave the prompt unanswered, destroy the bridge from another thread
   BridgeDestroyThread destroyThr(std::move(bridge_));

   if (destroyThr.waitFor(10s) != std::future_status::ready) {
      ADD_FAILURE() << "~CppBridge did not return within 10s while the "
         "server key prompt was pending";

      //reject the key: the connect fails, the notification thread exits and
      //~CppBridge can return, the rest of the run is unaffected
      replyToKeyPrompt(bridgeInDtor, refId, promptCounter, false);
   }
   if (!destroyThr.join(10s)) {
      FAIL() << "~CppBridge did not return after the key was rejected";
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletsWithDBTests, LateDisconnectAfterReconnect)
{
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 1);

   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);
   while (waitOnReplyFor(500ms) != nullptr) {}

   //drop the db connection and reconnect right away, both commands are
   //queued from this thread in this order. The db cleanup shuts down the
   //old connection, which then reports its own disconnection: that notif
   //is pushed while the cleanup runs, by which time the connect command
   //is normally already queued
   bridge_->cleanupDbConnection();
   uint64_t refId = rand();
   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(4).toHexStr();
   bridge_->connectToIp("127.0.0.1", 9001, callbackId, refId);

   //accept the key and wait on the connect reply and the setup done notif,
   //then give a late disconnected notif 1s to come through
   bool gotReply = false, replySuccess = false, gotSetupDone = false;
   unsigned disconnectsAfterReply = 0;
   std::string received;
   auto deadline = std::chrono::steady_clock::now() + 10s;
   bool settling = false;
   while (std::chrono::steady_clock::now() < deadline) {
      auto reply = waitOnReplyFor(100ms);
      if (reply == nullptr) {
         continue;
      }
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
         reply->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() == Codec::Bridge::FromBridge::REPLY) {
         auto response = fromBridge.getReply();
         if (response.getReferenceId() != refId) {
            received += "[other reply] ";
            continue;
         }
         gotReply = true;
         replySuccess = response.getSuccess();
         received += replySuccess ? "[connect ok] " : "[connect failed: " +
            std::string(response.getError()) + "] ";
      } else if (fromBridge.which() == Codec::Bridge::FromBridge::NOTIFICATION) {
         auto notif = fromBridge.getNotification();
         switch (notif.which()) {
            case Codec::Bridge::Notification::PRESENT_PUBKEY:
            {
               received += "[key prompt] ";
               std::string presentedKey(notif.getPresentPubkey());
               replyToKeyPrompt(bridge_, refId, notif.getCounter(),
                  presentedKey == serverPubkey_);
               break;
            }

            case Codec::Bridge::Notification::DISCONNECTED:
               received += "[disconnected] ";
               if (gotReply) {
                  ++disconnectsAfterReply;
               }
               break;

            case Codec::Bridge::Notification::SETUP_DONE:
               received += "[setup done] ";
               gotSetupDone = true;
               break;

            default:
               received += "[notif " + std::to_string((int)notif.which()) + "] ";
               break;
         }
      }

      if (gotReply && gotSetupDone && !settling) {
         settling = true;
         deadline = std::chrono::steady_clock::now() + 1s;
      }
   }

   ASSERT_TRUE(gotReply) << "no reply to the connect request, got: " << received;
   ASSERT_TRUE(replySuccess) << "got: " << received;
   EXPECT_TRUE(gotSetupDone) << "got: " << received;
   EXPECT_EQ(disconnectsAfterReply, 0) << "the old connection's disconnected "
      "notif tore down the new one, got: " << received;

   auto bdvPtr = bridge_->bdvPtr();
   ASSERT_NE(bdvPtr, nullptr) << "lost the new db connection";
   EXPECT_TRUE(bdvPtr->isValid());
}

////////////////////////////////////////////////////////////////////////////////
namespace {
   //TCP relay on an ephemeral port: accepts a connection and holds it until
   //release(), then connects to the upstream port and pumps bytes both ways.
   //A db connect through it has its TCP connection up, but the server can't
   //present its key until the relay is released
   class HeldTcpRelay
   {
   private:
      const Network::port_t upstreamPort_;
      Network::port_t port_ = 0;
      SOCKET listenFd_ = SOCK_MAX;
      std::atomic<bool> stop_{false};
      std::thread thr_;

      std::mutex mu_;
      std::condition_variable cv_;
      bool accepted_ = false;
      bool released_ = false;

   private:
      static int pollFor100ms(pollfd* fds, unsigned count)
      {
      #ifdef _WIN32
         return WSAPoll(fds, count, 100);
      #else
         return poll(fds, count, 100);
      #endif
      }

      static sockaddr_in loopback(Network::port_t port)
      {
         sockaddr_in addr{};
         addr.sin_family = AF_INET;
         addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
         addr.sin_port = htons(port);
         return addr;
      }

      void run()
      {
         SOCKET clientFd = SOCK_MAX;
         while (!stop_) {
            pollfd pfd{listenFd_, POLLIN, 0};
            if (pollFor100ms(&pfd, 1) > 0) {
               clientFd = ::accept(listenFd_, nullptr, nullptr);
               break;
            }
         }

         {
            std::unique_lock<std::mutex> lock(mu_);
            accepted_ = true;
            cv_.notify_all();
            cv_.wait(lock, [this]{ return released_; });
         }

         SOCKET upstreamFd = SOCK_MAX;
         if (!stop_) {
            upstreamFd = ::socket(AF_INET, SOCK_STREAM, 0);
            auto addr = loopback(upstreamPort_);
            if (::connect(upstreamFd, (sockaddr*)&addr, sizeof(addr)) != 0) {
               Network::SocketPrototype::closeSocket(upstreamFd);
            }
         }

         //pump until either side closes
         std::vector<char> buffer(65536);
         bool done = (upstreamFd == SOCK_MAX);
         while (!stop_ && !done) {
            pollfd fds[2] = {{clientFd, POLLIN, 0}, {upstreamFd, POLLIN, 0}};
            if (pollFor100ms(fds, 2) <= 0) {
               continue;
            }

            for (unsigned i=0; i<2 && !done; i++) {
               if (fds[i].revents == 0) {
                  continue;
               }
               auto readLen = ::recv(fds[i].fd, buffer.data(), buffer.size(), 0);
               auto destFd = (i == 0) ? upstreamFd : clientFd;
               if (readLen <= 0 ||
                  ::send(destFd, buffer.data(), readLen, 0) != readLen) {
                  done = true;
               }
            }
         }

         Network::SocketPrototype::closeSocket(clientFd);
         Network::SocketPrototype::closeSocket(upstreamFd);
      }

   public:
      HeldTcpRelay(Network::port_t upstreamPort) :
         upstreamPort_(upstreamPort)
      {
         listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
         auto addr = loopback(0);
         socklen_t addrLen = sizeof(addr);
         if (::bind(listenFd_, (sockaddr*)&addr, addrLen) != 0 ||
            ::listen(listenFd_, 1) != 0 ||
            ::getsockname(listenFd_, (sockaddr*)&addr, &addrLen) != 0) {
            Network::SocketPrototype::closeSocket(listenFd_);
            return;
         }
         port_ = ntohs(addr.sin_port);
         thr_ = std::thread([this]{ run(); });
      }

      ~HeldTcpRelay()
      {
         stop_.store(true);
         release();
         if (thr_.joinable()) {
            thr_.join();
         }
         Network::SocketPrototype::closeSocket(listenFd_);
      }

      Network::port_t port() const
      {
         return port_;
      }

      bool waitOnAccept(std::chrono::milliseconds timeout)
      {
         std::unique_lock<std::mutex> lock(mu_);
         return cv_.wait_for(lock, timeout, [this]{ return accepted_; });
      }

      void release()
      {
         {
            std::unique_lock<std::mutex> lock(mu_);
            released_ = true;
         }
         cv_.notify_all();
      }
   };
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletsWithDBTests, DestroyBeforeKeyPrompt)
{
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();
   HeldTcpRelay relay(9001);
   ASSERT_NE(relay.port(), 0);

   /* test starts here */
   //connect through the relay, the bridge's notification thread is then in
   //the connect with no key prompt yet. Queue the command from this thread,
   //so that no parser thread holds a copy of the bridge
   uint64_t refId = rand();
   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(4).toHexStr();
   bridge_->connectToIp("127.0.0.1", relay.port(), callbackId, refId);
   ASSERT_TRUE(relay.waitOnAccept(10s)) << "the connect never reached the relay";
   ASSERT_EQ(bridge_.use_count(), 1);

   //does not own the bridge. Only used to answer the prompt if ~CppBridge
   //doesn't return on its own, as in DestroyDuringKeyPrompt
   std::shared_ptr<Bridge::CppBridge> bridgeInDtor(
      bridge_.get(), [](Bridge::CppBridge*){});

   //destroy the bridge from another thread
   BridgeDestroyThread destroyThr(std::move(bridge_));

   //~CppBridge waits on the notification thread, which is still in the
   //connect. Let the connect through: the server presents its key and the
   //bridge registers the prompt while ~CppBridge is already running
   auto status = destroyThr.waitFor(500ms);
   relay.release();
   EXPECT_EQ(status, std::future_status::timeout) <<
      "~CppBridge returned while the connect was underway";

   bool promptPushed = false;
   if (destroyThr.waitFor(10s) != std::future_status::ready) {
      ADD_FAILURE() << "~CppBridge did not return within 10s, the server key "
         "prompt registered while it was running was left pending";

      //reject the key: the connect fails, the notification thread exits and
      //~CppBridge can return, the rest of the run is unaffected
      uint32_t promptCounter = 0;
      promptPushed = waitOnKeyPrompt(10s, promptCounter);
      if (promptPushed) {
         replyToKeyPrompt(bridgeInDtor, refId, promptCounter, false);
      }
   }
   if (!destroyThr.join(10s)) {
      FAIL() << "~CppBridge did not return after the key was rejected";
   }

   //the bridge pushed the key prompt and the connect failed
   auto replies = drainConnectReplies(refId);
   EXPECT_TRUE(promptPushed || replies.promptPushed) << "the bridge never "
      "pushed the server key prompt, got: " << replies.received;
   ASSERT_TRUE(replies.gotReply) << "no reply to the connect request, got: " <<
      replies.received;
   EXPECT_FALSE(replies.replySuccess) << "the connect succeeded, got: " <<
      replies.received;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletsWithDBTests, DeleteWallet)
{
   //create a fresh wallet
   auto wltId = createWallet(homedir_);

   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 2);
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 2);

   std::string accountId, importAccId, dbId;
   for (const auto& wltData : wallets) {
      if (wltData.first != wltId) {
         ASSERT_EQ(wltData.first, walletId_);
         importAccId = wltData.second.accountId;
         ASSERT_FALSE(importAccId.empty());
      } else {
         ASSERT_EQ(wltData.first, wltId);
         accountId = wltData.second.accountId;
         dbId = wltData.second.dbId;
         ASSERT_FALSE(accountId.empty());
      }
   }

   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, walletId_, importAccId);
   ASSERT_EQ(balances.size(), 4);
   try {
      for (const auto& balPair : balances) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   balances = getAddrBalances(bridge_, wltId, accountId);
   ASSERT_EQ(balances.size(), 0);

   //get the wallet path, check it
   std::filesystem::path wltPath;
   for (const auto& entry : wltList) {
      if (entry.second.walletId == wltId) {
         wltPath = homedir_ / entry.first;
         break;
      }
   }
   ASSERT_FALSE(wltPath.empty());
   ASSERT_TRUE(FileUtils::pathExists(wltPath, 0));

   //delete said wallet
   uint64_t refId = rand();
   capnp::MallocMessageBuilder message;
   auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
   toBridge.setReferenceId(refId);
   auto request = toBridge.initWalletManager();
   request.setDeleteWallet(wltId);
   auto rawReq = serializeCapnp(message);
   pushRequest(bridge_, rawReq);

   //validate reply
   int count = 0;
   while (count < 2) {
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      switch (fromBridge.which())
      {
         case Codec::Bridge::FromBridge::REPLY:
         {
            auto reply = fromBridge.getReply();
            ASSERT_TRUE(reply.getSuccess());
            ASSERT_EQ(reply.getReferenceId(), refId);
            ++count;
            break;
         }

         case Codec::Bridge::FromBridge::NOTIFICATION:
         {
            auto notif = fromBridge.getNotification();
            ASSERT_EQ(notif.which(), Codec::Bridge::Notification::REFRESH);
            ++count;
            break;
         }

         default:
            ASSERT_TRUE(false);
            break;
      }
   }
   ASSERT_FALSE(FileUtils::pathExists(wltPath, 0));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletsWithDBTests, ExtendAddressChain)
{
   //create a fresh wallet
   auto wltId = createWallet(homedir_);

   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 2);
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 2);

   std::string accountId, importAccId, dbId;
   for (const auto& wltData : wallets) {
      if (wltData.first != wltId) {
         ASSERT_EQ(wltData.first, walletId_);
         importAccId = wltData.second.accountId;
         ASSERT_FALSE(importAccId.empty());
      } else {
         ASSERT_EQ(wltData.first, wltId);
         accountId = wltData.second.accountId;
         dbId = wltData.second.dbId;
         ASSERT_FALSE(accountId.empty());
      }
   }

   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, walletId_, importAccId);
   ASSERT_EQ(balances.size(), 4);
   try {
      for (const auto& balPair : balances) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   balances = getAddrBalances(bridge_, wltId, accountId);
   ASSERT_EQ(balances.size(), 0);

   /* extend address chain */
   try {
      auto wltData = extendAddressPool(bridge_,
         wltId, accountId, dbId, 10000, false);
      ASSERT_EQ(wltData.walletId, wltId);
      ASSERT_EQ(wltData.accountId, accountId);
      EXPECT_EQ(wltData.useCount, -1);
      EXPECT_EQ(wltData.lookup, 10004);
   } catch (const std::runtime_error& e) {
      ASSERT_TRUE(false) << e.what();
   }

   /* grab wallet data explicitly, check chain again */
   auto walletData = getWalletData(bridge_, wltId, accountId);
   ASSERT_EQ(walletData.walletId, wltId);
   ASSERT_EQ(walletData.accountId, accountId);
   EXPECT_EQ(walletData.useCount, -1);
   EXPECT_EQ(walletData.lookup, 10004);
}

TEST_F(BridgeWalletsWithDBTests, AddNewAddress)
{
   /*
   This test covers the edge case where a wallet does not have enough
   computed addresses to serve a getAddress request, in which event it
   needs to trigger the address generation flow first
   */

   //create a fresh wallet
   auto wltId = createWallet(homedir_);

   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 2);
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 2);

   std::string accountId, importAccId, dbId;
   for (const auto& wltData : wallets) {
      if (wltData.first != wltId) {
         ASSERT_EQ(wltData.first, walletId_);
         importAccId = wltData.second.accountId;
         ASSERT_FALSE(importAccId.empty());
      } else {
         ASSERT_EQ(wltData.first, wltId);
         accountId = wltData.second.accountId;
         dbId = wltData.second.dbId;
         ASSERT_FALSE(accountId.empty());
      }
   }

   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   auto balances = getAddrBalances(bridge_, walletId_, importAccId);
   ASSERT_EQ(balances.size(), 4);
   try {
      for (const auto& balPair : balances) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   balances = getAddrBalances(bridge_, wltId, accountId);
   ASSERT_EQ(balances.size(), 0);

   /* get 4 addresses, wallet should have the data for that */
   {
      auto walletData = getWalletData(bridge_, wltId, accountId);
      ASSERT_EQ(walletData.walletId, wltId);
      ASSERT_EQ(walletData.accountId, accountId);
      EXPECT_EQ(walletData.useCount, -1);
      EXPECT_EQ(walletData.lookup, 4);
   }

   for (unsigned i=0; i<4; i++) {
      getAddress(bridge_, wltId, accountId);
   }

   {
      auto walletData = getWalletData(bridge_, wltId, accountId);
      ASSERT_EQ(walletData.walletId, wltId);
      ASSERT_EQ(walletData.accountId, accountId);
      EXPECT_EQ(walletData.useCount, 3);
      EXPECT_EQ(walletData.lookup, 4);
   }

   /* request a new address, it should trigger address creation */
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWallet();
      request.setWalletId(wltId);
      request.setAccountId(accountId);
      auto reqAddr = request.initGetAddress();
      reqAddr.setNew();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      bool refreshSeen = false;
      bool done = false;
      while (!done || !refreshSeen) {
         auto result = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(result->data.getPtr()),
            result->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();

         switch (fromBridge.which())
         {
            case Codec::Bridge::FromBridge::REPLY:
            {
               auto reply = fromBridge.getReply();
               ASSERT_TRUE(reply.getSuccess());
               ASSERT_EQ(reply.getReferenceId(), refId);
               ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::WALLET);

               auto walletReply = reply.getWallet();
               ASSERT_EQ(walletReply.which(), Codec::Bridge::WalletReply::GET_ADDRESS);
               done = true;
               break;
            }

            case Codec::Bridge::FromBridge::NOTIFICATION:
            {
               auto notif = fromBridge.getNotification();
               if (notif.which() == Codec::Bridge::Notification::REFRESH) {
                  ASSERT_FALSE(refreshSeen);
                  auto refreshNotif = notif.getRefresh();
                  ASSERT_EQ(refreshNotif.size(), 1);
                  ASSERT_EQ(refreshNotif[0], dbId);
                  refreshSeen = true;
               }
               break;
            }

            default:
               ASSERT_TRUE(false);
               break;
         }
      }
   }

   {
      //grab wallet data explicitly, check chain again
      auto walletData = getWalletData(bridge_, wltId, accountId);
      ASSERT_EQ(walletData.walletId, wltId);
      ASSERT_EQ(walletData.accountId, accountId);
      EXPECT_EQ(walletData.useCount, 4);
      EXPECT_EQ(walletData.lookup, 104);
   }
}
