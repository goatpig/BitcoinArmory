////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BridgeTestUtils.h"

////////////////////////////////////////////////////////////////////////////////
// BridgeSocketTests
class BridgeSocketTests : public ::testing::Test
{
protected:
   void SetUp() override
   {
      std::srand(std::time({}));
      homedir = std::filesystem::path("./fakehomedir");
      FileUtils::removeDirectory(homedir);
      std::filesystem::create_directory(homedir);

      serverStore_ = std::make_unique<NetworkPeers::ServerStore>(
         Cryptography::ECDSA::createNewPrivateKey());
      auto serverKey = serverStore_->getOwnPublicKey();

      Config::reset();
      Config::parseArgs({
         std::format("--uiPubKey={}", serverKey.toHexStr()),
         "--offline",
         "--datadir=./fakehomedir" },
         Config::ProcessType::Bridge);
   }

   void TearDown() override
   {
      Config::reset();
   }

   ////////
   class BridgeTestServerSocket : public Network::PersistentSocket
   {
   public:
      std::shared_ptr<Bridge::CppBridgeSocket> bridgeClientSock;

   public:
      BridgeTestServerSocket(Network::AcceptStruct acceptStruct,
         std::shared_ptr<Bridge::CppBridgeSocket> sockPtr) :
         PersistentSocket{acceptStruct.sockfd_, "ye_olde_test_server"},
         bridgeClientSock{sockPtr}
      {}

      SocketType type() const override
      {
         return SocketType::CppBridge;
      }

      void pushPayload(
         std::unique_ptr<Network::Socket_WritePayload> writePayload,
         std::shared_ptr<Network::Socket_ReadPayload>) override
      {
         std::vector<uint8_t> data;
         writePayload->serialize(data);
         queuePayloadForWrite(data);
      }
   };

   class RawBridgePayload : public Network::Socket_WritePayload
   {
      std::vector<uint8_t> data;

   public:
      RawBridgePayload(BinaryData bd) :
         Network::Socket_WritePayload{},
         data{std::move(bd.release())}
      {}

      void serialize(std::vector<uint8_t>& output) override
      {
         output = std::move(data);
      }

      std::string serializeToText() override
      {
         throw std::runtime_error("cannot serialize to text");
      }

      size_t getSerializedSize() const override
      {
         return data.size();
      }
   };

   ////////
   void injectClientKey(Network::port_t port)
   {
      SecureBinaryData clientKey(33);
      std::fstream file(homedir / "client_cookie",
         std::ios::in | std::ios::binary);
      file.read(clientKey.getCharPtr(), clientKey.getSize());

      NetworkPeers::PeerKey clientPeer{
         clientKey.getRef(), NetworkPeers::PeerType::Client};
      serverStore_->addPeer(clientPeer, {std::format("127.0.0.1:{}", port)}, {});
   }

protected:
   std::unique_ptr<NetworkPeers::ServerStore> serverStore_;
   std::filesystem::path homedir;
};

TEST_F(BridgeSocketTests, EstablishConnection)
{
   //setup & start the listen server
   Network::port_t port = (uint16_t)rand() % 10000 + 50000;
   std::promise<Network::AcceptStruct> acceptProm;
   auto fut = acceptProm.get_future();
   auto listenLbd = [port](std::promise<Network::AcceptStruct> prom) {
      Network::SimpleSocket listenSocket{"127.0.0.1", port, "listen_socket"};
      listenSocket.listen(
         [&prom](Network::AcceptStruct accStruct)->bool {
            prom.set_value(std::move(accStruct));
            return false;
         });
   };
   auto listenThr = std::thread(listenLbd, std::move(acceptProm));

   //wait on listen thread to start then connect
   std::this_thread::sleep_for(1s);
   auto client = std::make_shared<Bridge::CppBridgeSocket>("127.0.0.1", port);
   ASSERT_TRUE(client->connectToRemote());
   auto acceptStruct = fut.get();
   listenThr.join();

   //client should have written out its pubkey now
   injectClientKey(port);

   //we have a socket for the server side, now run it
   ASSERT_NE(acceptStruct.sockfd_, SOCK_MAX);
   BridgeTestServerSocket server{acceptStruct, client};
   ASSERT_TRUE(server.connectToRemote());

   /*
   Run bridge socket getPayload in its own thread, it will block until after
   the AEAD handshake is done and the first actual payload is delivered.
   */
   std::promise<BinaryData> firstPacketProm;
   auto firstPacketFut = firstPacketProm.get_future();
   auto readUntilFirstPacketThr = std::thread(
      [&client](std::promise<BinaryData> prom)
      {
         auto firstPacket = client->getPayload();
         prom.set_value(std::move(firstPacket));
      }, std::move(firstPacketProm)
   );

   /* some helpers */

   //server side BIP151 session
   BIP151Connection serverBIP15xSession{serverStore_->getView(), false};

   //server side decryption helper
   auto decryptByServer = [&serverBIP15xSession]
   (const std::vector<uint8_t>& cipherText, size_t expectedSize,
      ArmoryAEAD::BIP151_PayloadType expectedPayloadType)->BinaryData
   {
      if (cipherText.size() !=
         expectedSize + POLY1305MACLEN + AUTHASSOCDATAFIELDLEN) {
         return {};
      }

      BinaryData clearText;
      auto decrLen = serverBIP15xSession.decryptPacket(
         BinaryDataRef::fromVector(cipherText, POLY1305MACLEN + AUTHASSOCDATAFIELDLEN),
         clearText);
      if (decrLen != expectedSize) {
         return {};
      }

      clearText.resize(decrLen + AUTHASSOCDATAFIELDLEN);
      if (serverBIP15xSession.decryptPacket(
         BinaryDataRef::fromVector(cipherText),
         clearText) != 0) {
         return {};
      }
      if (clearText[4] != (uint8_t)expectedPayloadType) {
         return {};
      }

      //return the cleartext stripped of the size header and the
      //packet type prefix
      return clearText.getSliceCopy(5, clearText.getSize() - 5);
   };

   //server side encryption helper
   auto encryptByServer = [&serverBIP15xSession]
   (const BinaryData& clearText,
      ArmoryAEAD::BIP151_PayloadType pType)->BinaryData
   {
      uint32_t packetLen = clearText.getSize() + 1;

      //prepare ciphertext target
      BinaryData target;
      target.resize(packetLen + POLY1305MACLEN + AUTHASSOCDATAFIELDLEN);

      //set packet size
      std::memcpy(target.getPtr(), &packetLen, sizeof(uint32_t));

      //payload type
      target[4] = (uint8_t)pType;

      //copy the data
      std::memcpy(target.getPtr() + 5, clearText.getPtr(), clearText.getSize());

      //encrypt
      serverBIP15xSession.assemblePacket(
         target.getSliceRef(0, packetLen + AUTHASSOCDATAFIELDLEN),
         target.getPtr(), target.getSize());
      return target;
   };

   //server write function
   auto serverWriteCb = [&server, &encryptByServer]
   (const BinaryData& data, ArmoryAEAD::BIP151_PayloadType pType, bool encrypt) {
      std::unique_ptr<RawBridgePayload> payload;
      if (encrypt) {
         auto cipherText = encryptByServer(data, pType);
         payload = std::make_unique<RawBridgePayload>(std::move(cipherText));
      } else {
         BinaryWriter bw(data.getSize() + 1);
         bw.put_uint8_t((uint8_t)pType);
         bw.put_BinaryData(data);
         payload = std::make_unique<RawBridgePayload>(bw.getData());
      }
      server.pushPayload(std::move(payload), nullptr);
   };

   //empty server write function, for those payload that shouldn't reply
   int timesServerShouldNotHaveReplied = 0;
   auto noReplyServerWriteCb = [&timesServerShouldNotHaveReplied]
   (const BinaryData&, ArmoryAEAD::BIP151_PayloadType, bool) {
      ++timesServerShouldNotHaveReplied;
      return;
   };

   /* drive the server side AEAD handshake */

   //1. server sends start
   {
      ASSERT_EQ(ArmoryAEAD::BIP15x_Handshake::serverSideHandshake(
         &serverBIP15xSession, ArmoryAEAD::BIP151_PayloadType::Start,
         {}, serverWriteCb),
         ArmoryAEAD::HandshakeState::StepSuccessful
      );
   }

   //2. expect encAck
   //NOTE: encAck and encInit are sent back to back from the same path,
   //chances are they show up as a single packet
   auto encAckReply = server.getPacket();
   {
      ASSERT_GE(encAckReply.size(), 34);
      ASSERT_EQ(encAckReply[0], (uint8_t)ArmoryAEAD::BIP151_PayloadType::EncAck);
      BinaryDataRef encAckBdr{encAckReply.data() + 1, 33};

      //server should not have to reply to this payload
      ASSERT_EQ(ArmoryAEAD::BIP15x_Handshake::serverSideHandshake(
         &serverBIP15xSession, ArmoryAEAD::BIP151_PayloadType::EncAck,
         encAckBdr, noReplyServerWriteCb),
         ArmoryAEAD::HandshakeState::StepSuccessful
      );
      ASSERT_EQ(timesServerShouldNotHaveReplied, 0);
   }

   //3. expect encInit
   {
      std::vector<uint8_t> encInitReply;
      if (encAckReply.size() > 34) {
         encInitReply = std::vector<uint8_t>{encAckReply.begin() + 34, encAckReply.end()};
      } else {
         encInitReply = server.getPacket();
      }
      ASSERT_EQ(encInitReply.size(), 35);
      ASSERT_EQ(encInitReply[0], (uint8_t)ArmoryAEAD::BIP151_PayloadType::EncInit);
      BinaryDataRef encInitBdr{encInitReply.data() + 1, encInitReply.size() - 1};
      ASSERT_EQ(ArmoryAEAD::BIP15x_Handshake::serverSideHandshake(
         &serverBIP15xSession, ArmoryAEAD::BIP151_PayloadType::EncInit,
         encInitBdr, serverWriteCb),
         ArmoryAEAD::HandshakeState::StepSuccessful
      );
   }

   /* channel should be encrytped from now on */
   ASSERT_TRUE(serverBIP15xSession.connectionComplete());

   //4. expect auth challenge
   {
      auto authChallengeCipher = server.getPacket();
      auto authChallengeClear = decryptByServer(authChallengeCipher,
         33, ArmoryAEAD::BIP151_PayloadType::Challenge);

      //returned cleartext will be stripped of the size header and
      //the payload type prefix
      ASSERT_EQ(authChallengeClear.getSize(), 32);
      ASSERT_EQ(ArmoryAEAD::BIP15x_Handshake::serverSideHandshake(
         &serverBIP15xSession, ArmoryAEAD::BIP151_PayloadType::Challenge,
         authChallengeClear, serverWriteCb),
         ArmoryAEAD::HandshakeState::StepSuccessful
      );
   }

   //5. expect propose
   {
      auto proposeCipher = server.getPacket();
      auto proposeClear = decryptByServer(proposeCipher,
         33, ArmoryAEAD::BIP151_PayloadType::Propose);

      //returned cleartext will be stripped of the size header and
      //the payload type prefix
      ASSERT_EQ(proposeClear.getSize(), 32);
      ASSERT_EQ(ArmoryAEAD::BIP15x_Handshake::serverSideHandshake(
         &serverBIP15xSession, ArmoryAEAD::BIP151_PayloadType::Propose,
         proposeClear, serverWriteCb),
         ArmoryAEAD::HandshakeState::StepSuccessful
      );
   }

   //6. expect reply
   {
      auto replyCipher = server.getPacket();
      auto replyClear = decryptByServer(replyCipher,
         65, ArmoryAEAD::BIP151_PayloadType::Reply);

      //returned cleartext will be stripped of the size header and
      //the payload type prefix
      ASSERT_EQ(replyClear.getSize(), 64);

      //server should not have to reply to this payload
      ASSERT_EQ(ArmoryAEAD::BIP15x_Handshake::serverSideHandshake(
         &serverBIP15xSession, ArmoryAEAD::BIP151_PayloadType::Reply,
         replyClear, noReplyServerWriteCb),
         ArmoryAEAD::HandshakeState::Completed
      );
      ASSERT_EQ(timesServerShouldNotHaveReplied, 0);
   }

   //send a packet from server to end the client's getPayload loop
   auto serverSidePacket = BinaryData::fromString("hello world, from the server"sv);
   serverWriteCb(serverSidePacket, ArmoryAEAD::BIP151_PayloadType::SinglePacket, true);

   //grab the first packet down the queue
   auto deliveredPacket = firstPacketFut.get();
   EXPECT_EQ(deliveredPacket, serverSidePacket);
   readUntilFirstPacketThr.join();

   //feed it an encrypted packet in 2 parts, first part is <20 bytes
   auto lorem1 = BinaryData::fromString("Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua"sv);
   {
      //encrypt the data
      auto target = encryptByServer(lorem1,
         ArmoryAEAD::BIP151_PayloadType::SinglePacket);

      //split it
      auto payload1 = std::make_unique<RawBridgePayload>(
         std::move(target.getSliceCopy(0, 16)));
      auto payload2 = std::make_unique<RawBridgePayload>(
         std::move(target.getSliceCopy(16, target.getSize() - 16)));

      server.pushPayload(std::move(payload1), nullptr);
      std::this_thread::sleep_for(1s);
      server.pushPayload(std::move(payload2), nullptr);
   }
   auto lorem1FromClient = client->getPayload();
   EXPECT_EQ(lorem1, lorem1FromClient);

   //feed it an encrypted packet in 3 parts
   auto lorem2 = BinaryData::fromString("Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat."sv);
   {
      //encrypt the data
      auto target = encryptByServer(lorem2,
         ArmoryAEAD::BIP151_PayloadType::SinglePacket);

      auto payload1 = std::make_unique<RawBridgePayload>(
         std::move(target.getSliceCopy(0, 30)));
      auto payload2 = std::make_unique<RawBridgePayload>(
         std::move(target.getSliceCopy(30, 20)));
      auto payload3 = std::make_unique<RawBridgePayload>(
         std::move(target.getSliceCopy(50, target.getSize() - 50)));

      server.pushPayload(std::move(payload1), nullptr);
      std::this_thread::sleep_for(1s);
      server.pushPayload(std::move(payload2), nullptr);
      std::this_thread::sleep_for(1s);
      server.pushPayload(std::move(payload3), nullptr);
   }
   auto lorem2FromClient = client->getPayload();
   EXPECT_EQ(lorem2, lorem2FromClient);

   //feed it encrypted packets in 2 parts, the second part only carries the
   //last few bytes: the first part is long enough to read the packet length
   //but the packet is not complete yet
   auto lorem3 = BinaryData::fromString("Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur."sv);
   for (size_t tail : {1, 10, 16, 17, 36}) {
      auto target = encryptByServer(lorem3,
         ArmoryAEAD::BIP151_PayloadType::SinglePacket);

      auto payload1 = std::make_unique<RawBridgePayload>(
         std::move(target.getSliceCopy(0, target.getSize() - tail)));
      auto payload2 = std::make_unique<RawBridgePayload>(
         std::move(target.getSliceCopy(target.getSize() - tail, tail)));

      server.pushPayload(std::move(payload1), nullptr);
      std::this_thread::sleep_for(1s);
      server.pushPayload(std::move(payload2), nullptr);

      auto lorem3FromClient = client->getPayload();
      EXPECT_EQ(lorem3, lorem3FromClient) << "last " << tail << " bytes sent apart";
   }

   //cleanup
   client->shutdown();
   server.shutdown();
}
