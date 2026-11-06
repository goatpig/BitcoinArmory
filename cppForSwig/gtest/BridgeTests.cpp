////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BridgeTestUtils.h"

////////////////////////////////////////////////////////////////////////////////
// BridgeBlocksAutoDBTests
class BridgeBlocksAutoDBTests : public ::testing::Test
{
protected:

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

      #ifdef _WIN32
         auto buildPath = fullBinPath.string();
      #else
         auto buildPath = fullBinPath.parent_path().parent_path().string();
      #endif
      char* argv[] = {
         buildPath.data(),
         (char*)"--datadir=./fakehomedir"sv.data(),
         (char*)"--dbdir=./ldbtestdir"sv.data(),
         (char*)"--satoshi-datadir=./blkfiletest"sv.data(),
         (char*)"--db-type=DB_BARE"sv.data(),
         (char*)"--thread-count=3"sv.data()
      };
      Config::parseArgs(6, argv, Config::ProcessType::Bridge);
      prepareWallets();

      replyQueue.clear();
      bridge_ = std::make_shared<Bridge::CppBridge>(
         std::make_shared<TestBridgeSocket>());
   }

   void TearDown() override
   {
      bridge_.reset();
      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);
      Config::reset();
   }

   int disconnectFromDb()
   {
      //command db to shutdown
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initSetup();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
      request.setCleanupAutomationContext(callbackId);

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      bool cleanup = false, hasReply = false, disconnected = false;
      int step = 0;
      while (!cleanup || !hasReply || !disconnected) {
         auto reply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
            reply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);

         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         switch (fromBridge.which())
         {
            case Codec::Bridge::FromBridge::NOTIFICATION:
            {
               auto notif = fromBridge.getNotification();
               switch (notif.which()) {
                  case Codec::Bridge::Notification::DISCONNECTED:
                  {
                     if (disconnected) {
                        return -1;
                     }
                     disconnected = true;
                     break;
                  }

                  case Codec::Bridge::Notification::CLEANUP:
                  {
                     if (cleanup) {
                        return -2;
                     }
                     cleanup = true;
                     break;
                  }

                  case Codec::Bridge::Notification::AUTOMATION:
                  {
                     if (notif.getCallbackId() != callbackId) {
                        return -300;
                     }
                     auto automationNotif = notif.getAutomation();
                     switch (automationNotif.which()) {
                        case Codec::Bridge::Notification::AutomationContextNotif::SHUTDOWN_DB:
                        {
                           if (step != 0) {
                              return -3;
                           }
                           step = 1;
                           break;
                        }

                        case Codec::Bridge::Notification::AutomationContextNotif::DONE:
                        {
                           if (step != 1) {
                              return -4;
                           }
                           step = 2;
                           break;
                        }

                        default:
                           return -5;
                     }
                     break;
                  }
               }
               break;
            }

            case Codec::Bridge::FromBridge::REPLY:
            {
               if (hasReply) {
                  return -6;
               }
               auto replyCapnp = fromBridge.getReply();
               if (replyCapnp.getReferenceId() != refId ||
                  replyCapnp.getSuccess() == false) {
                  return -7;
               }
               hasReply = true;
               break;
            }

            default:
               return -8;
         }
      }
      if (step != 2) {
         return -9;
      }
      return 0;
   }

   std::filesystem::path validateSatoshiDir(
      const std::filesystem::path& satoshiDir)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initSetup();
      auto satoshiReq = request.initSatoshiHelper();
      satoshiReq.setValidateDir(satoshiDir.string());

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      auto reply = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
         reply->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);

      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
         throw std::runtime_error("not a reply");
      }

      auto replyCapnp = fromBridge.getReply();
      if (replyCapnp.getReferenceId() != refId ||
         replyCapnp.getSuccess() == false) {
         throw std::runtime_error(std::format(
            "validate error: {}", std::string{replyCapnp.getError()}));
      }
      if (replyCapnp.which() != Codec::Bridge::RpcReply::SETUP) {
         throw std::runtime_error("unexpected reply which");
      }
      auto setupReply = replyCapnp.getSetup();
      if (setupReply.which() !=
         Codec::Bridge::DbSetupReply::SATOSHI_HELPER) {
         throw std::runtime_error("unexpected setup which");
      }

      auto helperReply = setupReply.getSatoshiHelper();
      if (helperReply.which() !=
         Codec::Bridge::DbSetupReply::SatoshiReply::VALIDATE_DIR) {
         throw std::runtime_error("unexpected satoshi which");
      }

      auto validationReply = helperReply.getValidateDir();
      if (validationReply.getPruned() != false) {
         throw std::runtime_error(
            "test blk folder shouldn't be seen as pruned");
      }
      return std::filesystem::path{std::string{validationReply.getPath()}};
   }

protected:
   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;

   std::shared_ptr<Bridge::CppBridge> bridge_;
   std::string walletId_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeBlocksAutoDBTests, Connect)
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

   //setup connection to db
   std::filesystem::path validatedSatoshiDir;
   try {
      /*
      SatoshiDir is the path to the node home folder.
      If the validation call is passed a path with the `/blocks` suffix appended,
      it will be stripped in the reply (the validation call returns the "legal"
      path as part of its reply).
      We cover that behavior here.
      Also poison cover block data xoring and blk file discovery
      */

      //1. xor the blkfile folder
      auto xorKey = TestUtils::xorTheBlockFolder(blkdir_);
      ASSERT_NE(xorKey, UINT64_MAX);

      //2. poison the folder
      while (true) {
         //grab random data to fill the file, make sure it doesn't start with
         //the magic word, or it would be treated as a mangled block file
         auto randomData = Cryptography::PRNG::fortuna.generateRandom(128);
         if (randomData.getSliceRef(0, 4) ==
            Config::BitcoinSettings::getMagicBytes().getRef()) {
            continue;
         }
         std::fstream firstFile(blkdir_ / "blocks" / "afileaheadofothers.dat",
            std::ios::out | std::ios::binary);
         firstFile.write(randomData.getCharPtr(), randomData.getSize());
         break;
      }

      //3. validate it
      validatedSatoshiDir = validateSatoshiDir(blkdir_ / "blocks");
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(validatedSatoshiDir.empty());
   ASSERT_EQ(automateDb(bridge_, validatedSatoshiDir, ldbdir_), 0);
   ASSERT_TRUE(bridge_->isDbRunning());
   ASSERT_TRUE(registerWallets(bridge_));
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

   //cleanup
   ASSERT_EQ(disconnectFromDb(), 0);

   //confirm db is down
   while (bridge_->isDbRunning()) {
      std::this_thread::sleep_for(100ms);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeBlocksAutoDBTests, Connect_NoCleanup)
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

   //setup connection to db
   ASSERT_EQ(automateDb(bridge_, blkdir_, ldbdir_), 0);
   ASSERT_TRUE(bridge_->isDbRunning());
   ASSERT_TRUE(registerWallets(bridge_));
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

   /* ephemeral db should clean itself up after the client disconnects */
   bridge_->cleanupDbConnection();

   //confirm db is down
   while (bridge_->isDbRunning()) {
      std::this_thread::sleep_for(100ms);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeBlocksAutoDBTests, DontOverscan)
{
   /*
   An automated db should wait for the parent signal before parsing the chain.
   This is to ensure it doesn't scan twice on startup if the set of registered
   addresses deviates between starts.

   This can happen if the wallets are manipulated in offline mode between two
   online runs.
   */

   //run first instance of the db
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

   //setup connection to db
   ASSERT_EQ(automateDb(bridge_, blkdir_, ldbdir_), 0);
   ASSERT_TRUE(bridge_->isDbRunning());
   ASSERT_TRUE(registerWallets(bridge_));
   ASSERT_EQ(goOnline(bridge_, true), 5);

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

   //cleanup
   ASSERT_EQ(disconnectFromDb(), 0);

   //confirm db is down
   while (bridge_->isDbRunning()) {
      std::this_thread::sleep_for(100ms);
   }

   //add a wallet
   auto pubKeyF = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrF);
   auto walletId_AFLB = createWOWallet(homedir_,
      { pubKeyF }, {
         TestChain::scrAddrA,
         TestChain::lb1ScrAddr, TestChain::lb1ScrAddrP2SH,
         TestChain::lb2ScrAddr, TestChain::lb2ScrAddrP2SH
      }
   );

   wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   auto wltId2 = wltList.begin()->second.walletId;
   ASSERT_FALSE(wltId2.empty());
   ASSERT_NE(wltId2, walletId_);
   ASSERT_EQ(wltId2, walletId_AFLB);
   wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 2);
   std::string accountId_AFLB;
   for (const auto& wlt : wallets) {
      if (wlt.second.walletId == walletId_AFLB) {
         accountId_AFLB = wlt.second.accountId;
      }
   }
   ASSERT_FALSE(accountId_AFLB.empty());

   //start 2nd db instance
   ASSERT_EQ(automateDb(bridge_, blkdir_, ldbdir_), 0);
   ASSERT_TRUE(bridge_->isDbRunning());
   ASSERT_TRUE(registerWallets(bridge_));
   ASSERT_EQ(goOnline(bridge_, true), 5);

   //check balances
   auto balances1 = getAddrBalances(bridge_, wltId, accountId);
   ASSERT_EQ(balances.size(), 4);
   try {
      for (const auto& balPair : balances1) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   auto balances2 = getAddrBalances(bridge_, walletId_AFLB, accountId_AFLB);
   ASSERT_EQ(balances.size(), 4);
   try {
      for (const auto& balPair : balances2) {
         const auto& addrBal = TestChain::testAddrBalances[5].at(balPair.first);
         EXPECT_EQ(addrBal[0], balPair.second[0]);
         EXPECT_EQ(addrBal[1], balPair.second[1]);
         EXPECT_EQ(addrBal[2], balPair.second[2]);
      }
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //last cleanup
   ASSERT_EQ(disconnectFromDb(), 0);
   while (bridge_->isDbRunning()) {
      std::this_thread::sleep_for(100ms);
   }
}

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
            []()->std::unique_ptr<Passphrase::Params>
            { return std::make_unique<Passphrase::Params>
               (1ms, 0, SecureBinaryData{});
            }
         }
      });
      NetworkPeers::ServerStore serverPeers(serverPeersWlt);

      auto clientPeersWlt = NetworkPeers::PeerStore::bootstrapWallet({
         homedir_ / CLIENT_AUTH_PEER_FILENAME, {
            [pass=clientPeersDbPass_]()->std::unique_ptr<Passphrase::Params>
            { return std::make_unique<Passphrase::Params>
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

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BridgeDBCacheTests : public ::testing::Test
{
protected:
   //the 2 txs the inputs of the BIP174 test vector spend from
   const BinaryData rawTx1 = READHEX("0200000000010158e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd7501000000171600145f275f436b09a8cc9a2eb2a2f528485c68a56323feffffff02d8231f1b0100000017a914aed962d6654f9a2b36608eb9d64d2b260db4f1118700c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e88702483045022100a22edcc6e5bc511af4cc4ae0de0fcd75c7e04d8c1c3a8aa9d820ed4b967384ec02200642963597b9b1bc22c75e9f3e117284a962188bf5e8a74c895089046a20ad770121035509a48eb623e10aace8bfd0212fdb8a8e5af3c94b0b133b95e114cab89e4f7965000000");
   const BinaryData rawTx2 = READHEX("0200000001aad73931018bd25f84ae400b68848be09db706eac2ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d481c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b6393e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a270100000017a91429ca74f8a08f81999428185c97b5d852e4063f618765000000");
   const Types::TxKey minedKey = Types::constructTxKey(10, 3);
   const Types::TxKey zcKey = Types::constructZCKey(5);

   static void add(Ledgers::DBCache& cache,
      const BinaryData& raw, Types::TxKey key)
   {
      Tx tx(raw);
      tx.setTxKey(key);
      cache.addTx(tx);
   }

   static Types::TxKey keyForHash(const Ledgers::DBCache& cache,
      const BinaryData& raw)
   {
      return cache.getTxByHash(Tx(raw).getThisHash()).getDBKey();
   }
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, ZcThenMined)
{
   Ledgers::DBCache cache;
   add(cache, rawTx1, zcKey);
   EXPECT_EQ(keyForHash(cache, rawTx1), zcKey);

   //the mined copy takes the hash, the zc entry stays for the ledgers
   add(cache, rawTx1, minedKey);
   EXPECT_EQ(keyForHash(cache, rawTx1), minedKey);
   EXPECT_EQ(cache.getTx(zcKey).serialize(), rawTx1);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, MinedThenZc)
{
   Ledgers::DBCache cache;
   add(cache, rawTx1, minedKey);
   add(cache, rawTx1, zcKey);
   EXPECT_EQ(keyForHash(cache, rawTx1), minedKey);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, MinedThenRemined)
{
   //a reorg mines the tx again in another block, the new copy wins
   Ledgers::DBCache cache;
   const auto reminedKey = Types::constructTxKey(11, 0);
   add(cache, rawTx1, minedKey);
   add(cache, rawTx1, reminedKey);
   EXPECT_EQ(keyForHash(cache, rawTx1), reminedKey);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, EraseZcKeepsMined)
{
   Ledgers::DBCache cache;
   add(cache, rawTx1, zcKey);
   add(cache, rawTx1, minedKey);

   //dropping the zc copy leaves the mined one
   cache.eraseTx(zcKey);
   EXPECT_THROW(cache.getTx(zcKey), std::out_of_range);
   EXPECT_EQ(keyForHash(cache, rawTx1), minedKey);

   //erasing the key the hash points to still drops the hash
   cache.eraseTx(minedKey);
   EXPECT_THROW(cache.getTxByHash(Tx(rawTx1).getThisHash()),
      std::out_of_range);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, Candidates)
{
   //mined > pooled > unpooled, newest first within a kind
   auto unpooledKey = Types::constructUnpooledKey(7);
   auto minedKey2 = Types::constructTxKey(11, 0);

   Ledgers::TxKeyCandidates candidates;
   EXPECT_TRUE(candidates.empty());
   EXPECT_EQ(candidates.best(), Types::INVALID_TX_KEY);

   candidates.add(unpooledKey);
   EXPECT_EQ(candidates.best(), unpooledKey);
   candidates.add(zcKey);
   EXPECT_EQ(candidates.best(), zcKey);
   candidates.add(minedKey);
   EXPECT_EQ(candidates.best(), minedKey);
   candidates.add(minedKey2);
   EXPECT_EQ(candidates.best(), minedKey2);
   candidates.add(unpooledKey);
   EXPECT_EQ(candidates.keys(), (std::vector<Types::TxKey>{
      minedKey2, minedKey, zcKey, unpooledKey}));

   EXPECT_TRUE(candidates.remove(minedKey2));
   EXPECT_FALSE(candidates.remove(minedKey2));
   EXPECT_EQ(candidates.best(), minedKey);
   EXPECT_TRUE(candidates.remove(minedKey));
   EXPECT_EQ(candidates.best(), zcKey);
   EXPECT_TRUE(candidates.remove(zcKey));
   EXPECT_EQ(candidates.best(), unpooledKey);
   EXPECT_TRUE(candidates.remove(unpooledKey));
   EXPECT_TRUE(candidates.empty());

   //each key goes ahead of its kind and behind the better kinds: within a
   //kind the newest comes first, whatever its id
   auto minedKey0 = Types::constructTxKey(9, 0);
   auto zcKey2 = Types::constructZCKey(6);
   auto zcKey0 = Types::constructZCKey(4);
   auto unpooledKey2 = Types::constructUnpooledKey(8);
   auto unpooledKey0 = Types::constructUnpooledKey(6);
   for (auto txKey : {minedKey, zcKey, unpooledKey, zcKey2, minedKey2,
      unpooledKey2, zcKey0, minedKey0, unpooledKey0}) {
      candidates.add(txKey);
   }
   EXPECT_EQ(candidates.keys(), (std::vector<Types::TxKey>{
      minedKey0, minedKey2, minedKey, zcKey0, zcKey2, zcKey,
      unpooledKey0, unpooledKey2, unpooledKey}));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, Unpooled)
{
   Ledgers::DBCache cache;
   auto hash1 = Tx(rawTx1).getThisHash();

   //a raw tx gets the next unpooled key, once
   auto key1 = cache.addUnpooledTx(rawTx1);
   EXPECT_EQ(key1, Types::constructUnpooledKey(0));
   EXPECT_EQ(cache.getTx(key1).getBlockId(), Types::INVALID_BLOCK_ID);
   EXPECT_EQ(cache.getTx(key1).getTxIndex(), UINT16_MAX);
   EXPECT_EQ(cache.addUnpooledTx(rawTx1), key1);
   EXPECT_EQ(cache.addUnpooledTx(rawTx2), Types::constructUnpooledKey(1));
   EXPECT_EQ(cache.getTxByHash(hash1).serialize(), rawTx1);
   EXPECT_EQ(keyForHash(cache, rawTx1), key1);

   //the pooled and mined copies take over, the unpooled one stays below
   add(cache, rawTx1, zcKey);
   EXPECT_EQ(keyForHash(cache, rawTx1), zcKey);
   add(cache, rawTx1, minedKey);
   EXPECT_EQ(keyForHash(cache, rawTx1), minedKey);
   EXPECT_EQ(cache.addUnpooledTx(rawTx1), minedKey);

   //a purged hash keeps its mined copy only
   EXPECT_EQ(cache.purgeTxs({hash1}), (std::set<Types::TxKey>{zcKey, key1}));
   EXPECT_EQ(keyForHash(cache, rawTx1), minedKey);
   cache.eraseTx(minedKey);
   EXPECT_THROW(cache.getTxByHash(hash1), std::out_of_range);

   //an erased copy falls back to what's left
   auto zcKey2 = Types::constructZCKey(6);
   add(cache, rawTx2, zcKey2);
   EXPECT_EQ(keyForHash(cache, rawTx2), zcKey2);
   cache.eraseTx(zcKey2);
   EXPECT_EQ(keyForHash(cache, rawTx2), Types::constructUnpooledKey(1));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, EraseHashes)
{
   //every copy of the hash goes, other hashes stay
   Ledgers::DBCache cache;
   auto unpooledKey = cache.addUnpooledTx(rawTx1);
   add(cache, rawTx1, zcKey);
   add(cache, rawTx1, minedKey);
   auto zcKey2 = Types::constructZCKey(6);
   add(cache, rawTx2, zcKey2);

   cache.eraseHashes({Tx(rawTx1).getThisHash()});
   EXPECT_THROW(cache.getTxByHash(Tx(rawTx1).getThisHash()),
      std::out_of_range);
   for (auto txKey : {unpooledKey, zcKey, minedKey}) {
      EXPECT_THROW(cache.getTx(txKey), std::out_of_range);
   }
   EXPECT_EQ(keyForHash(cache, rawTx2), zcKey2);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, ZcIdReuse)
{
   //zc ids can be reused after ArmoryDB restarts: a key can come back for
   //another tx, the old tx goes
   Ledgers::DBCache cache;
   add(cache, rawTx1, zcKey);
   add(cache, rawTx2, zcKey);
   EXPECT_EQ(cache.getTx(zcKey).serialize(), rawTx2);
   EXPECT_EQ(keyForHash(cache, rawTx2), zcKey);
   EXPECT_THROW(cache.getTxByHash(Tx(rawTx1).getThisHash()),
      std::out_of_range);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, PurgeSkipsMined)
{
   Ledgers::DBCache cache;
   add(cache, rawTx1, minedKey);
   add(cache, rawTx2, zcKey);

   auto purged = cache.purgeTxs({
      Tx(rawTx1).getThisHash(), Tx(rawTx2).getThisHash()});
   EXPECT_EQ(purged, std::set<Types::TxKey>{zcKey});
   EXPECT_EQ(keyForHash(cache, rawTx1), minedKey);
   EXPECT_THROW(cache.getTxByHash(Tx(rawTx2).getThisHash()),
      std::out_of_range);
   EXPECT_THROW(cache.getTx(zcKey), std::out_of_range);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeDBCacheTests, InvalidKeys)
{
   //INVALID_TX_KEY and txio keys aren't tx keys, they are no candidates
   using Kind = Ledgers::TxKeyCandidates::Kind;
   const auto txioKey = Types::constructTxIOKey(10, 3, 1);
   const auto unpooledKey = Types::constructUnpooledKey(7);
   EXPECT_EQ(Ledgers::TxKeyCandidates::kindOf(Types::INVALID_TX_KEY),
      Kind::Invalid);
   EXPECT_EQ(Ledgers::TxKeyCandidates::kindOf(txioKey), Kind::Invalid);
   EXPECT_EQ(Ledgers::TxKeyCandidates::kindOf(minedKey), Kind::Mined);
   EXPECT_EQ(Ledgers::TxKeyCandidates::kindOf(zcKey), Kind::Pooled);
   EXPECT_EQ(Ledgers::TxKeyCandidates::kindOf(unpooledKey), Kind::Unpooled);

   Ledgers::TxKeyCandidates candidates;
   candidates.add(Types::INVALID_TX_KEY);
   candidates.add(txioKey);
   EXPECT_TRUE(candidates.empty());
   candidates.add(unpooledKey);
   candidates.add(Types::INVALID_TX_KEY);
   EXPECT_EQ(candidates.keys(), std::vector<Types::TxKey>{unpooledKey});

   //a tx starts with an invalid key, the cache doesn't take a tx without
   //a valid one
   Ledgers::DBCache cache;
   Tx unkeyed(rawTx2);
   EXPECT_EQ(unkeyed.getDBKey(), Types::INVALID_TX_KEY);
   cache.addTx(unkeyed);
   add(cache, rawTx1, Types::INVALID_TX_KEY);
   add(cache, rawTx1, txioKey);
   EXPECT_THROW(cache.getTxByHash(Tx(rawTx1).getThisHash()),
      std::out_of_range);
   EXPECT_THROW(cache.getTxByHash(Tx(rawTx2).getThisHash()),
      std::out_of_range);
   EXPECT_THROW(cache.getTx(Types::INVALID_TX_KEY), std::out_of_range);
   EXPECT_THROW(cache.getTx(txioKey), std::out_of_range);

   //a valid key still goes in
   add(cache, rawTx1, zcKey);
   EXPECT_EQ(keyForHash(cache, rawTx1), zcKey);
}

////////////////////////////////////////////////////////////////////////////////
GTEST_API_ int main(int argc, char **argv)
{
   Cryptography::ECDSA::setupContext();

   srand(time(0));
   std::cout << "Running main() from gtest_main.cc\n";
   startupBIP151CTX();
   startupBIP150CTX(4);

   SETLOGLEVEL(LogLvlDebug);
   //LOGENABLESTDOUT();
   LOGDISABLESTDOUT();

   fullBinPath = std::filesystem::absolute(std::filesystem::path{argv[0]});
   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();

   FLUSHLOG();
   CLEANUPLOG();

   Cryptography::ECDSA::shutdown();
   return exitCode;
}
