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
#ifndef _WIN32
TEST(AutomationContextTests, AutomateNode_Regtest)
{
   //fake bitcoind: writes its arguments, one per line, and exits.
   //posix_spawn gets an empty environment, so only absolute paths
   auto tmpDir = std::filesystem::absolute("./automatenodetest");
   FileUtils::removeDirectory(tmpDir);
   std::filesystem::create_directories(tmpDir / "regtest" / "blocks");
   std::filesystem::create_directories(tmpDir / "bin");
   std::filesystem::create_directories(tmpDir / "home");
   auto argsPath = tmpDir / "args.txt";
   auto fakeBin = tmpDir / "bin" / "bitcoind";
   {
      std::ofstream script(fakeBin);
      script << "#!/bin/sh\n"
         << "printf '%s\\n' \"$@\" > \"" << argsPath.string() << ".tmp\"\n"
         << "/bin/mv \"" << argsPath.string() << ".tmp\" \""
         << argsPath.string() << "\"\n";
   }
   std::filesystem::permissions(fakeBin, std::filesystem::perms::owner_all);

   //runningDir() is tmpDir/bin, which has no ArmoryDB: run() spawns the
   //node, then automateDb() throws before spawning anything else
   auto own = (tmpDir / "bin" / "test").string();
   auto datadirArg = "--datadir=" + (tmpDir / "home").string();
   char* argv[] = { own.data(), datadirArg.data(), (char*)"--regtest" };
   Config::parseArgs(3, argv, Config::ProcessType::Bridge);

   Bridge::AutomationContext ctx(
      tmpDir / "regtest", fakeBin, tmpDir / "db", true, true);
   EXPECT_THROW(ctx.run([](Bridge::AutomationStep){}), std::runtime_error);

   for (unsigned i=0; i<50 && !FileUtils::pathExists(argsPath, 0); i++) {
      std::this_thread::sleep_for(100ms);
   }
   auto lines = Config::SettingsUtils::getLines(argsPath);
   auto has = [&lines](const std::string& arg) {
      return std::find(lines.begin(), lines.end(), arg) != lines.end();
   };
   EXPECT_TRUE(has("--regtest"));
   EXPECT_TRUE(has("--datadir=" + tmpDir.string()));

   Config::reset();
   FileUtils::removeDirectory(tmpDir);
}
#endif
