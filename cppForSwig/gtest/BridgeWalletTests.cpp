////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BridgeTestUtils.h"

////////////////////////////////////////////////////////////////////////////////
// BridgeWalletTests
class BridgeWalletTests : public ::testing::Test
{
protected:
   //setup
   void SetUp() override
   {
      std::srand(std::time({}));
      homedir = std::filesystem::path("./fakehomedir");
      FileUtils::removeDirectory(homedir);
      std::filesystem::create_directory(homedir);

      Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Config::ProcessType::Bridge);

      replyQueue.clear();
      bridge_ = std::make_shared<Bridge::CppBridge>(
         std::make_shared<TestBridgeSocket>());
   }

   void TearDown() override
   {
      bridge_.reset();
      Config::reset();
      FileUtils::removeDirectory(homedir);
   }

   //helpers
   bool unlockWallet(const std::string& path, const std::string& passphrase)
   {
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

      //request unlock
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initWalletManager();
         auto unlockReq = request.initUnlockControlHeader();

         unlockReq.setCallbackId(callbackId);
         unlockReq.setWalletPath(path);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);
      }

      //wait on passphrase prompt
      auto rawPrompt = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(rawPrompt->data.getPtr()),
         rawPrompt->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
         return false;
      }

      auto notif = fromBridge.getNotification();
      if (notif.getCallbackId() != callbackId) {
         return false;
      }
      if (notif.which() != Codec::Bridge::Notification::UNLOCK_REQUEST) {
         return false;
      }
      auto counter = notif.getCounter();

      //push passphrase
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         auto notifReply = toBridge.initNotification();
         notifReply.setSuccess(true);
         notifReply.setCounter(counter);
         notifReply.setUnlockRequest(passphrase);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);
      }

      //expect success reply
      bool success = false;
      {
         auto rawReply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawReply->data.getPtr()),
            rawReply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
            return false;
         }

         auto rpcReply = fromBridge.getReply();
         if (rpcReply.getReferenceId() != refId) {
            success = false;
         } else {
            success = rpcReply.getSuccess();
         }
      }

      //expect prompt cleanup
      {
         auto rawReply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawReply->data.getPtr()),
            rawReply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            return false;
         }
   
         auto notif = fromBridge.getNotification();
         if (notif.getCallbackId() != callbackId) {
            return false;
         }
         if (notif.which() != Codec::Bridge::Notification::CLEANUP) {
            return false;
         }
      }

      return success;
   }

   int failToUnlockWallet(const std::string& path, unsigned count)
   {
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

      //request unlock
      {
         capnp::MallocMessageBuilder message;
         auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
         toBridge.setReferenceId(refId);
         auto request = toBridge.initWalletManager();
         auto unlockReq = request.initUnlockControlHeader();

         unlockReq.setCallbackId(callbackId);
         unlockReq.setWalletPath(path);

         auto rawReq = serializeCapnp(message);
         pushRequest(bridge_, rawReq);
      }

      unsigned attempts = 0;
      while (attempts <= count) {
         //wait on passphrase prompt
         auto rawPrompt = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawPrompt->data.getPtr()),
            rawPrompt->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            return -1;
         }

         auto notif = fromBridge.getNotification();
         if (notif.getCallbackId() != callbackId) {
            return -2;
         }
         if (notif.which() != Codec::Bridge::Notification::UNLOCK_REQUEST) {
            return -3;
         }
         auto counter = notif.getCounter();

         //push passphrase
         {
            capnp::MallocMessageBuilder message;
            auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
            auto notifReply = toBridge.initNotification();
            notifReply.setCounter(counter);

            if (attempts != count) {
               notifReply.setSuccess(true);
               auto badPass = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
               notifReply.setUnlockRequest(badPass);
            } else {
               notifReply.setSuccess(false);
            }

            auto rawReq = serializeCapnp(message);
            pushRequest(bridge_, rawReq);
         }
         ++attempts;
      }

      //expect failure notif
      {
         auto rawReply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawReply->data.getPtr()),
            rawReply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
            return -4;
         }

         auto rpcReply = fromBridge.getReply();
         if (rpcReply.getReferenceId() != refId) {
            return -5;
         }
         if (rpcReply.getSuccess() != false) {
            return -6;
         }

         std::string errorStr = rpcReply.getError();
         if (errorStr != std::string{"unlock request rejected"}) {
            return -7;
         }
      }

      //expect prompt cleanup
      {
         auto rawReply = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(rawReply->data.getPtr()),
            rawReply->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
            return -8;
         }
   
         auto notif = fromBridge.getNotification();
         if (notif.getCallbackId() != callbackId) {
            return -9;
         }
         if (notif.which() != Codec::Bridge::Notification::CLEANUP) {
            return -10;
         }
      }
      return attempts;
   }

   std::chrono::milliseconds testKDFUnlock(const std::string& walletId)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWallet();
      request.setWalletId(walletId);
      request.setGetUnlockTime();

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
         return {};
      }
      return std::chrono::milliseconds(reply.getWallet().getGetUnlockTime());
   }

public:
   std::filesystem::path homedir;
   std::shared_ptr<Bridge::CppBridge> bridge_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, ListStageLoad)
{
   /*
   This test covers the same scenario as WalletManagerTests.ListStageLoad,
   but through CppBridge rather than WalletManager directly
   */
   std::vector<std::pair<std::filesystem::path, std::string>> walletFiles;

   //wallet 1
   {
      Wallets::IO::CreateWalletParams params{
         homedir,
         {1ms, 0, SecureBinaryData::fromString("privpass1")},
         {},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletFiles.emplace_back(
         std::make_pair(assetWlt->getDbFilename(), assetWlt->getID()));
   }

   //wallet 2, 3, 4
   for (unsigned i=2; i<5; i++) {
      Wallets::IO::CreateWalletParams params{
         homedir,
         {1ms, 0, SecureBinaryData::fromString("privpass" + std::to_string(i))},
         {1ms, 0, SecureBinaryData::fromString("controlpass" + std::to_string(i))},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletFiles.emplace_back(
         std::make_pair(assetWlt->getDbFilename(), assetWlt->getID()));
   }

   //list wallets
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 4);
      auto checkWltList = [&wltList, &walletFiles](
         unsigned intId, int expectedState, bool expectedStaged)->bool
      {
         auto entry = walletFiles[intId];
         auto path = entry.first.filename().string();
         auto capnEntry = wltList.at(path);

         if (capnEntry.loadState != expectedState) {
            return false;
         }

         if (expectedState == CapnWalletState_Ready) {
            if (capnEntry.walletId != entry.second) {
               return false;
            }
         }
         return capnEntry.staged == expectedStaged;
      };
      EXPECT_TRUE(checkWltList(0, CapnWalletState_Ready, true));
      EXPECT_TRUE(checkWltList(1, CapnWalletState_Encrypted, false));
      EXPECT_TRUE(checkWltList(2, CapnWalletState_Encrypted, false));
      EXPECT_TRUE(checkWltList(3, CapnWalletState_Encrypted, false));
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //unlock wallets 3 & 4
   ASSERT_TRUE(unlockWallet(walletFiles[2].first.filename().string(), "controlpass3"));
   ASSERT_TRUE(unlockWallet(walletFiles[3].first.filename().string(), "controlpass4"));

   //list wallets again, check for unlocks
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 4);
      auto checkWltList = [&wltList, &walletFiles](
         unsigned intId, int expectedState, bool expectedStaged)->bool
      {
         auto entry = walletFiles[intId];
         auto path = entry.first.filename().string();
         auto capnEntry = wltList.at(path);

         if (capnEntry.loadState != expectedState) {
            return false;
         }

         if (expectedState == CapnWalletState_Ready) {
            if (capnEntry.walletId != entry.second) {
               return false;
            }
         }
         return capnEntry.staged == expectedStaged;
      };
      EXPECT_TRUE(checkWltList(0, CapnWalletState_Ready, true));
      EXPECT_TRUE(checkWltList(1, CapnWalletState_Encrypted, false));
      EXPECT_TRUE(checkWltList(2, CapnWalletState_Ready, true));
      EXPECT_TRUE(checkWltList(3, CapnWalletState_Ready, true));
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //fail to unlock wallet 2
   ASSERT_EQ(failToUnlockWallet(walletFiles[1].first.filename().string(), 2), 3);

   //unstage wallet 4
   stageWallet(bridge_, walletFiles[3].second, false);

   //list wallets again, check for staging
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 4);
      auto checkWltList = [&wltList, &walletFiles](
         unsigned intId, int expectedState, bool expectedStaged)->bool
      {
         auto entry = walletFiles[intId];
         auto path = entry.first.filename().string();
         auto capnEntry = wltList.at(path);

         if (capnEntry.loadState != expectedState) {
            return false;
         }

         if (expectedState == CapnWalletState_Ready) {
            if (capnEntry.walletId != entry.second) {
               return false;
            }
         }
         return capnEntry.staged == expectedStaged;
      };
      EXPECT_TRUE(checkWltList(0, CapnWalletState_Ready, true));
      EXPECT_TRUE(checkWltList(1, CapnWalletState_Encrypted, false));
      EXPECT_TRUE(checkWltList(2, CapnWalletState_Ready, true));
      EXPECT_TRUE(checkWltList(3, CapnWalletState_Ready, false));
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //load wallets
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 2);
   EXPECT_NE(wallets.find(walletFiles[0].second), wallets.end());
   EXPECT_NE(wallets.find(walletFiles[2].second), wallets.end());

   //list wallets one last time
   //list wallets again, check for staging
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 2);
      auto checkWltList = [&wltList, &walletFiles](
         unsigned intId, int expectedState, bool expectedStaged)->bool
      {
         auto entry = walletFiles[intId];
         auto path = entry.first.filename().string();
         auto capnEntry = wltList.at(path);

         if (capnEntry.loadState != expectedState) {
            return false;
         }

         if (expectedState == CapnWalletState_Ready) {
            if (capnEntry.walletId != entry.second) {
               return false;
            }
         }
         return capnEntry.staged == expectedStaged;
      };
      EXPECT_TRUE(checkWltList(1, CapnWalletState_Encrypted, false));
      EXPECT_TRUE(checkWltList(3, CapnWalletState_Ready, false));
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
}

TEST_F(BridgeWalletTests, ListWO)
{
   std::vector<std::pair<std::filesystem::path, std::string>> wltPaths;
   std::string walletId;

   //wallet 1
   {
      Wallets::IO::CreateWalletParams params{
         homedir,
         {1ms, 0, SecureBinaryData::fromString("privpass1")},
         {1ms, 0, SecureBinaryData::fromString("ctrlPass")},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletId = assetWlt->getID();
      wltPaths.emplace_back(std::make_pair(
         assetWlt->getDbFilename(), walletId));
   }

   {
      const auto& fullWltPath = wltPaths.begin()->first;
      auto woWltPath = Wallets::AssetWallet::forkWatchingOnly({fullWltPath,
         [](const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result
         {
            return { SecureBinaryData::fromString("ctrlPass"), true };
         }},
         {1ms, 0, SecureBinaryData::fromString("woPass")}
      );
      ASSERT_FALSE(woWltPath.empty());
      ASSERT_TRUE(FileUtils::pathExists(woWltPath, 0));
      ASSERT_NE(woWltPath, fullWltPath);
      wltPaths.emplace_back(std::make_pair(
         woWltPath, walletId));
   }

   //list wallets
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 2);
      auto checkWltList = [&wltList, &wltPaths](
         unsigned intId, int expectedState, bool expectedStaged)->bool
      {
         auto entry = wltPaths[intId];
         auto path = entry.first.filename().string();
         auto capnEntry = wltList.at(path);

         if (capnEntry.loadState != expectedState) {
            return false;
         }

         if (expectedState == CapnWalletState_Ready) {
            if (capnEntry.walletId != entry.second) {
               return false;
            }
         }

         //encrypted wallets show up as not WO (can't be determined yet)
         if (capnEntry.isWO) {
            return false;
         }
         return capnEntry.staged == expectedStaged;
      };
      EXPECT_TRUE(checkWltList(0, CapnWalletState_Encrypted, false));
      EXPECT_TRUE(checkWltList(1, CapnWalletState_Encrypted, false));
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //unlock them
   ASSERT_TRUE(unlockWallet(wltPaths[0].first.filename().string(), "ctrlPass"));
   ASSERT_TRUE(unlockWallet(wltPaths[1].first.filename().string(), "woPass"));

   //list again
   try {
      std::vector<std::string> wltIds;
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 2);
      auto checkWltList = [&wltList, &wltPaths, &wltIds](
         unsigned intId, int expectedState,
         bool expectedStaged, bool isWO)->bool
      {
         auto entry = wltPaths[intId];
         auto path = entry.first.filename().string();
         auto capnEntry = wltList.at(path);

         if (capnEntry.loadState != expectedState) {
            return false;
         }

         if (expectedState == CapnWalletState_Ready) {
            if (capnEntry.walletId != entry.second) {
               return false;
            }
            wltIds.emplace_back(capnEntry.walletId);
         }

         if (isWO != capnEntry.isWO) {
            return false;
         }
         return capnEntry.staged == expectedStaged;
      };
      EXPECT_TRUE(checkWltList(0, CapnWalletState_Ready, true, false));
      EXPECT_TRUE(checkWltList(1, CapnWalletState_Ready, true, true));

      ASSERT_EQ(wltIds.size(), 2);
      ASSERT_EQ(wltIds[0], wltIds[1]);
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, CreateWallet)
{
   //create the wallet
   uint64_t refId = rand();
   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

   capnp::MallocMessageBuilder message;
   auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
   toBridge.setReferenceId(refId);
   auto request = toBridge.initUtils();
   auto createWltReq = request.initCreateWallet();

   createWltReq.setCallbackId(callbackId);
   createWltReq.setWalletType(Codec::Bridge::UtilsRequest::WalletType::LEGACY);
   createWltReq.setLookup(100);
   createWltReq.setLabel("labl");
   createWltReq.setDescription("desc");

   auto rawReq = serializeCapnp(message);
   pushRequest(bridge_, rawReq);

   //handle progress notifs
   std::string masterId;
   std::filesystem::path path;
   try {
      auto walletData = progressWalletCreation(
         bridge_, callbackId,
         "pass1", 500ms, 128, 100);
      masterId = walletData.masterId;
      path = walletData.path;
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //validate reply
   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   auto reply = fromBridge.getReply();
   ASSERT_TRUE(reply.getSuccess());
   ASSERT_EQ(reply.getReferenceId(), refId);

   ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::UTILS);
   auto utilsReply = reply.getUtils();
   ASSERT_EQ(utilsReply.which(), Codec::Bridge::UtilsReply::CREATE_WALLET);
   std::string wltId = utilsReply.getCreateWallet();
   ASSERT_FALSE(wltId.empty());

   //get the wallet data & validate it
   auto wltData = getWalletData(bridge_, wltId, {});
   EXPECT_EQ(wltData.walletId, wltId);
   EXPECT_FALSE(wltData.accountId.empty());
   EXPECT_EQ(wltData.path.filename().string(), path);
   EXPECT_EQ(wltData.masterId, masterId);

   EXPECT_EQ(wltData.label, "labl");
   EXPECT_EQ(wltData.desc, "desc");

   EXPECT_TRUE(wltData.encrypted);
   EXPECT_FALSE(wltData.watchingOnly);
   EXPECT_EQ(wltData.addresses.size(), 1);
   EXPECT_EQ(wltData.kdfMemReq, 128);

   //request KDF unlock time
   auto unlockTime = testKDFUnlock(wltId);
   EXPECT_GE(unlockTime, 500ms) << unlockTime.count();

   //grab backup string
   auto backup = getWalletBackup(bridge_, wltId, "pass1",
      Codec::Bridge::WalletBackup::Type::ARMORY200_A);
   ASSERT_EQ(backup.size(), 2);
   ASSERT_EQ(backup[0].size(), 46);
   ASSERT_EQ(backup[1].size(), 46);
}

TEST_F(BridgeWalletTests, CreateWallet_BIP32)
{
   //create the wallet
   uint64_t refId = rand();
   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

   capnp::MallocMessageBuilder message;
   auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
   toBridge.setReferenceId(refId);
   auto request = toBridge.initUtils();
   auto createWltReq = request.initCreateWallet();

   createWltReq.setCallbackId(callbackId);
   createWltReq.setWalletType(Codec::Bridge::UtilsRequest::WalletType::STRUCTURED_BIP32);
   createWltReq.setLookup(100);
   createWltReq.setLabel("labl");
   createWltReq.setDescription("desc");

   auto rawReq = serializeCapnp(message);
   pushRequest(bridge_, rawReq);

   //handle progress notifs
   std::string masterId;
   std::filesystem::path path;
   try {
      auto walletData = progressWalletCreation(
         bridge_, callbackId,
         "pass1", 500ms, 128, 100, false, true);
      masterId = walletData.masterId;
      path = walletData.path;
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //validate reply
   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   auto reply = fromBridge.getReply();
   ASSERT_TRUE(reply.getSuccess());
   ASSERT_EQ(reply.getReferenceId(), refId);

   ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::UTILS);
   auto utilsReply = reply.getUtils();
   ASSERT_EQ(utilsReply.which(), Codec::Bridge::UtilsReply::CREATE_WALLET);
   std::string wltId = utilsReply.getCreateWallet();
   ASSERT_FALSE(wltId.empty());

   //get the wallet data & validate it
   auto wltData = getWalletData(bridge_, wltId, {});
   EXPECT_EQ(wltData.walletId, wltId);
   EXPECT_FALSE(wltData.accountId.empty());
   EXPECT_EQ(wltData.path.filename().string(), path);
   EXPECT_EQ(wltData.masterId, masterId);

   EXPECT_EQ(wltData.label, "labl");
   EXPECT_EQ(wltData.desc, "desc");

   EXPECT_TRUE(wltData.encrypted);
   EXPECT_FALSE(wltData.watchingOnly);
   EXPECT_EQ(wltData.addresses.size(), 1);
   EXPECT_EQ(wltData.kdfMemReq, 128);

   //request KDF unlock time
   auto unlockTime = testKDFUnlock(wltId);
   EXPECT_GE(unlockTime, 500ms) << unlockTime.count();

   //grab backup string
   auto backup = getWalletBackup(bridge_, wltId, "pass1",
      Codec::Bridge::WalletBackup::Type::ARMORY200_B);
   ASSERT_EQ(backup.size(), 2);
   ASSERT_EQ(backup[0].size(), 46);
   ASSERT_EQ(backup[1].size(), 46);
}

TEST_F(BridgeWalletTests, DeleteWallet)
{
   std::string masterId;
   std::filesystem::path path;
   std::string wltId;

   {
      //create the wallet
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initUtils();
      auto createWltReq = request.initCreateWallet();

      createWltReq.setCallbackId(callbackId);
      createWltReq.setLookup(100);
      createWltReq.setLabel("labl");
      createWltReq.setDescription("desc");

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      //handle progress notifs
      try {
         auto walletData = progressWalletCreation(
            bridge_, callbackId,
            "pass1", 500ms, 0, 100);
         masterId = walletData.masterId;
         path = walletData.path;
      } catch (const std::exception& e) {
         ASSERT_TRUE(false) << e.what();
      }

      //validate reply
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();
      ASSERT_TRUE(reply.getSuccess());
      ASSERT_EQ(reply.getReferenceId(), refId);

      ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::UTILS);
      auto utilsReply = reply.getUtils();
      ASSERT_EQ(utilsReply.which(), Codec::Bridge::UtilsReply::CREATE_WALLET);
      wltId = utilsReply.getCreateWallet();
   }

   //get the wallet data & validate it
   ASSERT_FALSE(wltId.empty());
   auto wltData = getWalletData(bridge_, wltId, {});
   EXPECT_EQ(wltData.walletId, wltId);
   EXPECT_FALSE(wltData.accountId.empty());
   ASSERT_FALSE(path.empty());
   EXPECT_EQ(wltData.path.filename().string(), path);
   EXPECT_EQ(wltData.masterId, masterId);

   EXPECT_EQ(wltData.label, "labl");
   EXPECT_EQ(wltData.desc, "desc");

   EXPECT_TRUE(wltData.encrypted);
   EXPECT_FALSE(wltData.watchingOnly);
   EXPECT_EQ(wltData.addresses.size(), 1);
   EXPECT_GE(wltData.kdfMemReq, 8);

   //check wallet path
   auto fullWltPath = homedir / path;
   ASSERT_TRUE(FileUtils::pathExists(fullWltPath, 0));

   //delete said wallet
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWalletManager();
      request.setDeleteWallet(wltId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      //validate reply
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto reply = fromBridge.getReply();
      ASSERT_TRUE(reply.getSuccess());
      ASSERT_EQ(reply.getReferenceId(), refId);
      ASSERT_FALSE(FileUtils::pathExists(fullWltPath, 0));
   }
}

TEST_F(BridgeWalletTests, CreateWallet_Reject)
{
   //create the wallet
   uint64_t refId = rand();
   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

   capnp::MallocMessageBuilder message;
   auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
   toBridge.setReferenceId(refId);
   auto request = toBridge.initUtils();
   auto createWltReq = request.initCreateWallet();

   createWltReq.setCallbackId(callbackId);
   createWltReq.setLookup(100);
   createWltReq.setLabel("labl");
   createWltReq.setDescription("desc");

   auto rawReq = serializeCapnp(message);
   pushRequest(bridge_, rawReq);

   //handle progress notifs
   try {
      auto walletData = progressWalletCreation(
         bridge_, callbackId,
         std::string{}, 500ms, 128, 100);
      ASSERT_FALSE(walletData.masterId.empty());
      ASSERT_FALSE(walletData.path.empty());

      //check file is cleaned up
      EXPECT_FALSE(FileUtils::pathExists(
         std::filesystem::path{"./fakehomedir"} / walletData.path, 0));
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //validate reply
   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   auto reply = fromBridge.getReply();
   ASSERT_FALSE(reply.getSuccess());
   ASSERT_EQ(reply.getReferenceId(), refId);
   std::string errorStr = reply.getError();
   EXPECT_EQ(errorStr, std::string{"passphrase request was rejected"});
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, RestoreWallet_Legacy)
{
   const std::string walletId{"292AxMD9H"};
   const std::vector<std::string> lines {
      "oiow rfta wueg hewo  wuaj jawj rddi uufu  tusi",
      "idnt enrd sjgo tgfi  esni eutw ktna ustg  arfe",
      "jdtf fink jshs ewda  kkor daet kgtr eiha  ejgd",
      "uaew ggod ngjk ejuu  rugf kufg awnn ofas  rhtf"
   };
   const std::string passphrase{"privPassTest"};

   //restore the wallet
   auto restoreData = restoreWallet(bridge_, lines, walletId,
      Codec::Bridge::WalletBackup::Type::LEGACY135_A,
      passphrase, 300ms, 32, false, 500);

   //get the wallet data & validate it
   auto wltData = getWalletData(bridge_, walletId, {});
   EXPECT_EQ(wltData.walletId, walletId);
   EXPECT_FALSE(wltData.accountId.empty());
   EXPECT_EQ(wltData.path.filename(), restoreData.path);
   EXPECT_EQ(wltData.masterId, restoreData.masterId);

   EXPECT_TRUE(wltData.encrypted);
   EXPECT_FALSE(wltData.watchingOnly);
   EXPECT_EQ(wltData.addresses.size(), 1);
   EXPECT_EQ(wltData.lookup, 501);
   EXPECT_EQ(wltData.kdfMemReq, 32);

   //request KDF unlock time
   auto unlockTime = testKDFUnlock(walletId);
   EXPECT_GE(unlockTime, 300ms) << unlockTime.count();

   //grab backup strings via callback
   {
      uint64_t refId = rand();
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWallet();
      request.setWalletId(walletId);
      auto reqBackup = request.initCreateBackupString();
      reqBackup.setPrivate(callbackId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      //wait for unlock request
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      auto notif = fromBridge.getNotification();
      ASSERT_EQ(notif.getCallbackId(), callbackId);
      ASSERT_EQ(notif.which(), Codec::Bridge::Notification::UNLOCK_REQUEST);

      //reply with passphrase
      capnp::MallocMessageBuilder notifMsg;
      auto notifBridge = notifMsg.initRoot<Codec::Bridge::ToBridge>();
      auto notifReply = notifBridge.initNotification();
      notifReply.setSuccess(true);
      notifReply.setCounter(notif.getCounter());
      notifReply.setUnlockRequest(passphrase);

      auto rawNotif = serializeCapnp(notifMsg);
      pushRequest(bridge_, rawNotif);

      //cleanup notif
      result = waitOnReply();
      words = kj::ArrayPtr<const capnp::word>(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      reader = capnp::FlatArrayMessageReader(words);
      fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      notif = fromBridge.getNotification();
      ASSERT_EQ(notif.which(), Codec::Bridge::Notification::CLEANUP);

      //check the backup
      auto backup = waitOnReply();
      words = kj::ArrayPtr<const capnp::word>(
         reinterpret_cast<const capnp::word*>(backup->data.getPtr()),
         backup->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader replyReader(words);
      fromBridge = replyReader.getRoot<Codec::Bridge::FromBridge>();
      ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::REPLY);
      auto reply = fromBridge.getReply();
      ASSERT_TRUE(reply.getSuccess());
      ASSERT_EQ(reply.getReferenceId(), refId);

      ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::WALLET);
      auto walletReply = reply.getWallet();
      ASSERT_EQ(walletReply.which(), Codec::Bridge::WalletReply::CREATE_BACKUP_STRING);

      auto capnBackup = walletReply.getCreateBackupString();
      auto rootLines = capnBackup.getRootClear();
      ASSERT_EQ(rootLines.size(), 2);
      ASSERT_EQ(rootLines[0], lines[0]);
      ASSERT_EQ(rootLines[1], lines[1]);

      auto chainLines = capnBackup.getChainClear();
      ASSERT_EQ(chainLines.size(), 2);
      ASSERT_EQ(chainLines[0], lines[2]);
      ASSERT_EQ(chainLines[1], lines[3]);
   }

   //grab backup strings via passphrase
   try {
      auto capnLines = getWalletBackup(bridge_, walletId, passphrase,
         Codec::Bridge::WalletBackup::Type::LEGACY135_A);
      ASSERT_EQ(capnLines.size(), 4);

      for (unsigned i = 0; i < 4; i++) {
         ASSERT_EQ(lines[i], capnLines[i]);
      }
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //sanity check
   ASSERT_FALSE(checkHasImports(bridge_, walletId));
}

TEST_F(BridgeWalletTests, RestoreWallet_BIP32)
{
   const std::string walletId{"2tkj1x7cb"};
   const std::vector<std::string> lines {
      "reah edre tttd rekh  skhj ride anag weht  eeaw",
      "shdu ksuj fetd fguo  skwk twnw tsdr gkwg  gawh"
   };
   const std::string passphrase{"privPassTest"};

   //restore the wallet
   unsigned restoreLookup = 500;
   auto restoreData = restoreWallet(bridge_, lines, walletId,
      Codec::Bridge::WalletBackup::Type::ARMORY200_B,
      passphrase, 0ms, 32, false, restoreLookup);

   //get the wallet data & validate it
   auto wltData = getWalletData(bridge_, walletId, {});
   EXPECT_EQ(wltData.walletId, walletId);
   EXPECT_FALSE(wltData.accountId.empty());
   EXPECT_EQ(wltData.path.filename(), restoreData.path);
   EXPECT_EQ(wltData.masterId, restoreData.masterId);

   EXPECT_TRUE(wltData.encrypted);
   EXPECT_FALSE(wltData.watchingOnly);
   EXPECT_EQ(wltData.addresses.size(), 1);
   EXPECT_EQ(wltData.lookup, restoreLookup);
   EXPECT_EQ(wltData.kdfMemReq, 32);

   //open and inspect the wallet
   {
      auto restoredWlt = Wallets::AssetWallet::loadMainWalletFromFile(
         Wallets::IO::ReadOnlyFileParams{wltData.path, nullptr});
      ASSERT_NE(restoredWlt, nullptr);

      //iterate through address accounts
      for (const auto& addrAccId : restoredWlt->getAccountIDs()) {
         auto addrAccPtr = restoredWlt->getAccountForID(addrAccId);
         ASSERT_NE(addrAccPtr, nullptr);

         //sanity checks
         ASSERT_GE(addrAccPtr->getAccountIdSet().size(), 2);
         auto outerAccId = addrAccPtr->getOuterAccountID();
         auto innerAccId = addrAccPtr->getInnerAccountID();
         ASSERT_TRUE(outerAccId.isValid());
         ASSERT_TRUE(innerAccId.isValid());
         ASSERT_NE(outerAccId, innerAccId);

         auto outerAcc = addrAccPtr->getAccountForID(outerAccId);
         ASSERT_NE(outerAcc, nullptr);
         auto innerAcc = addrAccPtr->getAccountForID(innerAccId);
         ASSERT_NE(innerAcc, nullptr);

         //both main and change asset accounts should be extended on restore
         EXPECT_EQ(outerAcc->getAssetCount(), restoreLookup);
         EXPECT_EQ(innerAcc->getAssetCount(), restoreLookup);
         EXPECT_EQ(outerAcc->getLastComputedIndex(), restoreLookup - 1);
         EXPECT_EQ(innerAcc->getLastComputedIndex(), restoreLookup - 1);
      }
   }

   //grab backup strings via passphrase
   try {
      auto capnLines = getWalletBackup(bridge_, walletId, passphrase,
         Codec::Bridge::WalletBackup::Type::ARMORY200_B);
      ASSERT_EQ(capnLines.size(), 2);

      for (unsigned i = 0; i < 2; i++) {
         ASSERT_EQ(lines[i], capnLines[i]);
      }
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
}

TEST_F(BridgeWalletTests, RestoreWallet_LegacyWO)
{
   const std::string walletId{"292AxMD9H"};
   const std::vector<std::string> lines {
      "wswg egsh oghk aaaj eu",
      "kdrk ouid agee jttn tfaa ruun twsu jfgu nasj",
      "otrr egjh farw dfuo gddh ugki fhrt dgeh geth",
      "jdtf fink jshs ewda kkor daet kgtr eiha ejgd",
      "uaew ggod ngjk ejuu rugf kufg awnn ofas rhtf",
   };
   const std::string passphrase{"privPassTest"};

   //restore the wallet
   auto restoreData = restoreWallet(bridge_, lines, walletId,
      Codec::Bridge::WalletBackup::Type::LEGACY135_A,
      passphrase, 300ms, 32, false, 500);

   //get the wallet data & validate it
   auto wltData = getWalletData(bridge_, walletId, {});
   EXPECT_EQ(wltData.walletId, walletId);
   EXPECT_FALSE(wltData.accountId.empty());
   EXPECT_EQ(wltData.path.filename(), restoreData.path);
   EXPECT_EQ(wltData.masterId, restoreData.masterId);

   EXPECT_FALSE(wltData.encrypted);
   EXPECT_TRUE(wltData.watchingOnly);
   EXPECT_EQ(wltData.addresses.size(), 1);
   EXPECT_EQ(wltData.lookup, 501);
   EXPECT_EQ(wltData.kdfMemReq, 0);

   //grab backup strings
   {
      uint64_t refId = rand();

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWallet();
      request.setWalletId(walletId);
      auto reqBackup = request.initCreateBackupString();
      reqBackup.setPublic();
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      //check the backup
      auto backup = waitOnReply();
      auto words = kj::ArrayPtr<const capnp::word>(
         reinterpret_cast<const capnp::word*>(backup->data.getPtr()),
         backup->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader replyReader(words);
      auto fromBridge = replyReader.getRoot<Codec::Bridge::FromBridge>();
      ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::REPLY);
      auto reply = fromBridge.getReply();
      ASSERT_TRUE(reply.getSuccess());
      ASSERT_EQ(reply.getReferenceId(), refId);

      ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::WALLET);
      auto walletReply = reply.getWallet();
      ASSERT_EQ(walletReply.which(), Codec::Bridge::WalletReply::CREATE_BACKUP_STRING);

      auto capnBackup = walletReply.getCreateBackupString();
      auto rootLines = capnBackup.getRootClear();
      ASSERT_EQ(rootLines.size(), 2);
      ASSERT_EQ(rootLines[0], lines[1]);
      ASSERT_EQ(rootLines[1], lines[2]);

      auto chainLines = capnBackup.getChainClear();
      ASSERT_EQ(chainLines.size(), 2);
      ASSERT_EQ(chainLines[0], lines[3]);
      ASSERT_EQ(chainLines[1], lines[4]);

      //backupid
      auto backupId = capnBackup.getBackupId();
      ASSERT_EQ(backupId, lines[0]);
   }
}

TEST_F(BridgeWalletTests, RestoreMerge)
{
   //create the wallet
   uint64_t refId = rand();
   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
   std::string passphrase{"pass2"};
   std::string passphrase2{"pass3"};
   unsigned lookup = 46;

   capnp::MallocMessageBuilder message;
   auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
   toBridge.setReferenceId(refId);
   auto request = toBridge.initUtils();
   auto createWltReq = request.initCreateWallet();

   createWltReq.setCallbackId(callbackId);
   createWltReq.setLookup(lookup);
   createWltReq.setLabel("labl2");
   createWltReq.setDescription("desc2");

   auto rawReq = serializeCapnp(message);
   pushRequest(bridge_, rawReq);

   //handle progress notifs
   std::string masterId;
   std::filesystem::path path;
   try {
      auto walletData = progressWalletCreation(
         bridge_, callbackId,
         passphrase, 500ms, 128, lookup);
      masterId = walletData.masterId;
      path = walletData.path;
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //validate reply
   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   auto reply = fromBridge.getReply();
   ASSERT_TRUE(reply.getSuccess());
   ASSERT_EQ(reply.getReferenceId(), refId);

   ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::UTILS);
   auto utilsReply = reply.getUtils();
   ASSERT_EQ(utilsReply.which(), Codec::Bridge::UtilsReply::CREATE_WALLET);
   std::string wltId = utilsReply.getCreateWallet();
   ASSERT_FALSE(wltId.empty());

   //get the wallet data & validate it
   auto wltData = getWalletData(bridge_, wltId, {});
   EXPECT_EQ(wltData.walletId, wltId);
   EXPECT_FALSE(wltData.accountId.empty());
   EXPECT_EQ(wltData.path.filename().string(), path);
   EXPECT_EQ(wltData.masterId, masterId);

   EXPECT_EQ(wltData.label, "labl2");
   EXPECT_EQ(wltData.desc, "desc2");

   EXPECT_TRUE(wltData.encrypted);
   EXPECT_FALSE(wltData.watchingOnly);
   EXPECT_EQ(wltData.addresses.size(), 1);
   EXPECT_EQ(wltData.lookup, lookup);
   EXPECT_EQ(wltData.kdfMemReq, 128);

   //grab 3 addresses
   std::vector<AddressData> addresses;
   addresses.emplace_back(*wltData.addresses.begin());
   for (unsigned i=0; i<3; i++) {
      addresses.emplace_back(getAddress(bridge_, wltId, wltData.accountId));
   }

   for (unsigned i=0; i<4; i++) {
      const auto& addr = addresses[i];
      ASSERT_EQ(addr.index, i);
      ASSERT_FALSE(addr.hash.empty());
   }

   //grab the backup strings
   std::vector<std::string> lines;
   try {
      lines = getWalletBackup(bridge_, wltId, passphrase,
         Codec::Bridge::WalletBackup::Type::ARMORY200_A);
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //restore merge the wallet
   ASSERT_EQ(lines.size(), 2);
   for (const auto& line : lines) {
      ASSERT_FALSE(line.empty());
   }
   auto restoreData = restoreWallet(bridge_, lines, wltId,
      Codec::Bridge::WalletBackup::Type::ARMORY200_A,
      passphrase2, 1ms, 0, true,
      //the legacy armory account always starts with asset 0
      lookup - 1
   );
   ASSERT_EQ(restoreData.masterId, masterId);
   ASSERT_EQ(restoreData.path, path);

   //validate restored wallet state
   auto wltData2 = getWalletData(bridge_, wltId, {});
   EXPECT_EQ(wltData2.walletId, wltId);
   EXPECT_EQ(wltData2.accountId, wltData.accountId);
   EXPECT_EQ(wltData2.path.filename().string(), path);
   EXPECT_EQ(wltData2.masterId, masterId);

   EXPECT_EQ(wltData2.label, "labl2");
   EXPECT_EQ(wltData2.desc, "desc2");

   EXPECT_TRUE(wltData2.encrypted);
   EXPECT_FALSE(wltData2.watchingOnly);
   EXPECT_EQ(wltData2.addresses.size(), 4);
   EXPECT_EQ(wltData2.lookup, lookup);

   for (const auto& addr : wltData2.addresses) {
      ASSERT_LT(addr.index, 4);
      const auto& addrI = addresses[addr.index];
      ASSERT_EQ(addr.hash, addrI.hash);
   }

   //check it unlocks with passphrase2
   std::vector<std::string> lines2;
   try {
      lines2 = getWalletBackup(bridge_, wltId, passphrase2,
         Codec::Bridge::WalletBackup::Type::ARMORY200_A);
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   EXPECT_EQ(lines, lines2);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, Migrate_Legacy)
{
   //copy test legacy wallet to datadir
   const std::string wltId{"28m472Xbm"sv};
   const std::string fileName = std::string{"armory_"sv} +
      std::string{wltId} +
      std::string{"_.wallet"sv};
   const std::filesystem::path wltPath{fileName};
   std::filesystem::copy(
      "input_files/legacy.wallet"sv,
      homedir / wltPath
   );

   //list the wallet
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 1);

      auto wltInfoLegacy = wltList.at(fileName);
      ASSERT_EQ(wltInfoLegacy.walletId, wltId);
      ASSERT_FALSE(wltInfoLegacy.staged);
      ASSERT_EQ(wltInfoLegacy.loadState, 1);
      ASSERT_EQ(wltInfoLegacy.accountIds.size(), 0);
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //migrate it
   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
   uint64_t refId = rand();
   {
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto mgrRequest = toBridge.initWalletManager();
      auto migrationRequest = mgrRequest.initMigrateWallet();
      migrationRequest.setWalletPath(fileName);
      migrationRequest.setCallbackId(callbackId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);
   }

   /* handle migration callbacks */

   //unlock notif
   {
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::NOTIFICATION);

      auto notif = fromBridge.getNotification();
      ASSERT_EQ (notif.getCallbackId(), callbackId);
      auto counter = notif.getCounter();

      ASSERT_EQ(notif.which(), Codec::Bridge::Notification::UNLOCK_REQUEST);

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      auto notifReply = toBridge.initNotification();
      notifReply.setSuccess(true);
      notifReply.setCounter(counter);
      notifReply.setUnlockRequest("testnet");
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);
   }

   //progress new wallet creation
   auto wltData = progressWalletCreation(
      bridge_, callbackId, "newpass", 250ms, 32, 104);

   //validate success
   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::REPLY);
   auto reply = fromBridge.getReply();
   ASSERT_EQ(reply.getReferenceId(), refId);
   ASSERT_TRUE(reply.getSuccess());

   ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::WALLET_MANAGER);
   auto mgrReply = reply.getWalletManager();
   ASSERT_EQ(mgrReply.which(), Codec::Bridge::WalletManagerReply::MIGRATE_WALLET);
   std::string migratedId(mgrReply.getMigrateWallet());

   /* list wallets again, migrated wallet should be staged */
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 2);
      for (const auto& wltInfo : wltList) {
         ASSERT_EQ(wltInfo.second.walletId, wltId);
         if (wltInfo.first == fileName) {
            ASSERT_FALSE(wltInfo.second.staged);
            ASSERT_EQ(wltInfo.second.loadState, CapnWalletState_Legacy);
            ASSERT_EQ(wltInfo.second.accountIds.size(), 0);
         } else {
            ASSERT_TRUE(wltInfo.second.staged);
            ASSERT_EQ(wltInfo.second.loadState, CapnWalletState_Ready);
            ASSERT_EQ(wltInfo.second.accountIds.size(), 1);
            ASSERT_EQ(wltInfo.second.accountIds[0], legacyAccId);
         }
      }
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   {
      auto wallets = loadWallets(bridge_);
      ASSERT_EQ(wallets.size(), 1);
      auto iter = wallets.find(wltId);
      ASSERT_NE(iter, wallets.end());

      ASSERT_TRUE(iter->second.encrypted);
      ASSERT_FALSE(iter->second.watchingOnly);
      EXPECT_EQ(iter->second.kdfMemReq, 32);
      EXPECT_EQ(iter->second.lookup, 104);
      EXPECT_EQ(iter->second.addresses.size(), 3);
   }
}

TEST_F(BridgeWalletTests, ImportWallet_Legacy)
{
   std::filesystem::path legacyWalletFile{"input_files/legacy.wallet"sv};
   const std::string walletId{"28m472Xbm"sv};

   /* import the wallet file */
   uint64_t refId = rand();
   capnp::MallocMessageBuilder message;
   auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
   toBridge.setReferenceId(refId);
   auto request = toBridge.initUtils();
   request.setImportWallet(legacyWalletFile.string());
   auto rawReq = serializeCapnp(message);
   pushRequest(bridge_, rawReq);

   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   auto reply = fromBridge.getReply();
   ASSERT_TRUE(reply.getSuccess());
   ASSERT_EQ(reply.getReferenceId(), refId);

   /* validate the reply */
   auto utilsReply = reply.getUtils();
   auto wltReply = utilsReply.getImportWallet();

   //type
   EXPECT_EQ(wltReply.which(),
      Codec::Bridge::WalletImportPreview::LEGACY);

   //id
   std::string capnWltId(wltReply.getWalletId());
   EXPECT_EQ(capnWltId, walletId);

   //label
   std::string capnLabel(wltReply.getLabel());
   EXPECT_EQ(capnLabel, "legacy1");

   //description
   std::string capnDesc(wltReply.getDescription());
   EXPECT_EQ(capnDesc, "migration test");

   //address count & index
   EXPECT_EQ(wltReply.getHighestUsedIndex(), 2);
   EXPECT_EQ(wltReply.getAddressCount(), 104);

   //priv keys
   EXPECT_FALSE(wltReply.getWatchingOnly());
   EXPECT_TRUE(wltReply.getEncrypted());
   EXPECT_EQ(wltReply.getKdfMem(), 32 * 1024 * 1024);

   //version
   EXPECT_EQ(wltReply.getSeedVersion(), "1.35");

   /* check wallet was imported to the manager */
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 1);

      auto wltInfoLegacy = wltList.at(legacyWalletFile.filename().string());
      ASSERT_EQ(wltInfoLegacy.walletId, walletId);
      ASSERT_FALSE(wltInfoLegacy.staged);
      ASSERT_EQ(wltInfoLegacy.loadState, 1);
      ASSERT_EQ(wltInfoLegacy.accountIds.size(), 0);
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   /* migrate the wallet */
   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
   refId = rand();
   {
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto mgrRequest = toBridge.initWalletManager();
      auto migrationRequest = mgrRequest.initMigrateWallet();
      migrationRequest.setWalletPath(legacyWalletFile.filename().string());
      migrationRequest.setCallbackId(callbackId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);
   }

   /* handle migration callbacks */

   //unlock notif
   {
      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::NOTIFICATION);

      auto notif = fromBridge.getNotification();
      ASSERT_EQ (notif.getCallbackId(), callbackId);
      auto counter = notif.getCounter();

      ASSERT_EQ(notif.which(), Codec::Bridge::Notification::UNLOCK_REQUEST);

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      auto notifReply = toBridge.initNotification();
      notifReply.setSuccess(true);
      notifReply.setCounter(counter);
      notifReply.setUnlockRequest("testnet");
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);
   }

   //progress new wallet creation
   auto wltData = progressWalletCreation(
      bridge_, callbackId, "newpass", 250ms, 32, 104);

   //validate success
   result = waitOnReply();
   words = kj::ArrayPtr<const capnp::word>{
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word)};
   reader = capnp::FlatArrayMessageReader{words};
   fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::REPLY);
   reply = fromBridge.getReply();
   ASSERT_EQ(reply.getReferenceId(), refId);
   ASSERT_TRUE(reply.getSuccess());

   ASSERT_EQ(reply.which(), Codec::Bridge::RpcReply::WALLET_MANAGER);
   auto mgrReply = reply.getWalletManager();
   ASSERT_EQ(mgrReply.which(), Codec::Bridge::WalletManagerReply::MIGRATE_WALLET);
   std::string migratedId(mgrReply.getMigrateWallet());

   /* list wallets again, migrated wallet should be staged */
   try {
      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 2);
      for (const auto& wltInfo : wltList) {
         ASSERT_EQ(wltInfo.second.walletId, walletId);
         if (wltInfo.first == legacyWalletFile.filename()) {
            ASSERT_FALSE(wltInfo.second.staged);
            ASSERT_EQ(wltInfo.second.loadState, CapnWalletState_Legacy);
            ASSERT_EQ(wltInfo.second.accountIds.size(), 0);
         } else {
            ASSERT_TRUE(wltInfo.second.staged);
            ASSERT_EQ(wltInfo.second.loadState, CapnWalletState_Ready);
            ASSERT_EQ(wltInfo.second.accountIds.size(), 1);
            ASSERT_EQ(wltInfo.second.accountIds[0], legacyAccId);
         }
      }
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   {
      auto wallets = loadWallets(bridge_);
      ASSERT_EQ(wallets.size(), 1);
      auto iter = wallets.find(walletId);
      ASSERT_NE(iter, wallets.end());

      ASSERT_TRUE(iter->second.encrypted);
      ASSERT_FALSE(iter->second.watchingOnly);
      EXPECT_EQ(iter->second.kdfMemReq, 32);
      EXPECT_EQ(iter->second.lookup, 104);
      EXPECT_EQ(iter->second.addresses.size(), 3);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, ChangeWalletPassphrase)
{
   /* 1. create an encrypted wallet */
   std::filesystem::path walletPath;
   std::string walletId;

   std::string currentPass{"privPass1"};
   std::string newPass{"newPrivPass2"};

   //wallet 1
   {
      Wallets::IO::CreateWalletParams params{
         homedir,
         {500ms, 0, SecureBinaryData::fromString(currentPass)},
         {1ms, 0, {}},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletPath = assetWlt->getDbFilename();
      walletId = assetWlt->getID();
   }
   ASSERT_FALSE(walletId.empty());

   /* 2. load it into bridge */
   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   ASSERT_EQ(wltList.begin()->first, walletPath.filename().string());

   {
      const auto& wltEntry = wltList.begin()->second;
      ASSERT_EQ(wltEntry.loadState, (int)Bridge::WalletLoadState::Ready);
      ASSERT_TRUE(wltEntry.staged);
      EXPECT_EQ(wltEntry.walletId, walletId);
   }

   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 1);
   ASSERT_EQ(wallets.begin()->first, walletId);

   /* 3. check current passphrase, grab backup */
   auto now = std::chrono::system_clock::now();
   auto wltBackupLines = getWalletBackup(bridge_, walletId, currentPass,
      Codec::Bridge::WalletBackup::Type::ARMORY200_A);
   auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now() - now);
   EXPECT_GE(elapsed, 500ms);
   ASSERT_EQ(wltBackupLines.size(), 2);
   ASSERT_EQ(wltBackupLines[0].size(), 46);
   ASSERT_EQ(wltBackupLines[1].size(), 46);
   ASSERT_NE(wltBackupLines[0], wltBackupLines[1]);

   /* 4. change its passphrase */
   EXPECT_TRUE(changeWalletPassphrase(bridge_, walletId,
      currentPass, newPass));

   /* 5. check current passphrase fails to unlock wallet */
   try {
      auto newLines = getWalletBackup(bridge_, walletId, currentPass,
         Codec::Bridge::WalletBackup::Type::ARMORY200_A);
      ASSERT_TRUE(false);
   } catch (const std::exception& e) {
      EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
   }

   /* 6. check new passphrase unlocks wallet */
   try {
      auto now = std::chrono::system_clock::now();
      auto newLines = getWalletBackup(bridge_, walletId, newPass,
         Codec::Bridge::WalletBackup::Type::ARMORY200_A);
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
         std::chrono::system_clock::now() - now);
      EXPECT_GE(elapsed, 500ms);
      ASSERT_EQ(newLines.size(), 2);
      ASSERT_EQ(newLines[0], wltBackupLines[0]);
      ASSERT_EQ(newLines[1], wltBackupLines[1]);
   } catch (const std::exception& e) {
      ASSERT_TRUE(false);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, ExportPrivateKeysEncrypted)
{
   /* 1. create an encrypted wallet and count its full set of private keys. */
   std::string walletId;
   std::string privPass{"privPass1"};
   unsigned lookup = 4;
   size_t expectedKeyCount = 0;
   std::map<BinaryData, ExportedKey> expectedKeys;

   {
      Wallets::IO::CreateWalletParams params{
         homedir,
         {500ms, 0, SecureBinaryData::fromString(privPass)},
         {1ms, 0, {}},
         nullptr, lookup
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletId = assetWlt->getID();
      std::tie(expectedKeyCount, expectedKeys) = buildExpectedExportKeys(*assetWlt);
      fillExpectedPrivateKeys(*assetWlt,
         SecureBinaryData::fromString(privPass), expectedKeys);
   }
   ASSERT_FALSE(walletId.empty());
   ASSERT_EQ(expectedKeyCount, static_cast<size_t>(lookup));

   /* 2. load into bridge */
   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1ULL);

   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 1ULL);
   ASSERT_EQ(wallets.begin()->first, walletId);

   /* 3. export with correct passphrase: every private key must come back */
   std::map<BinaryData, BinaryData> originalKeys;
   {
      auto result = exportPrivateKeys(bridge_, walletId, privPass, true);
      ASSERT_TRUE(result.success) << "export failed: " << result.error;
      verifyExportSetMatchesExpected(result.keys, expectedKeys, true);

      for (const auto& exported : result.keys) {
         originalKeys.emplace(exported.assetId, exported.privKey);
      }
   }

   /* 4. export with wrong passphrase: unlock is rejected */
   {
      auto result = exportPrivateKeys(bridge_, walletId, "wrongPass", false);
      EXPECT_FALSE(result.success);
   }

   /* 5. restore the wallet from its backup, then export again and check the
      keys against the original export list. NOTE: restoring may recompute a
      different portion of the address chain than the original lookahead pool,
      which can legitimately surface fewer keys here. */
   std::vector<std::string> lines;
   try {
      lines = getWalletBackup(bridge_, walletId, privPass,
         Codec::Bridge::WalletBackup::Type::ARMORY200_A);
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_EQ(lines.size(), 2);

   restoreWallet(bridge_, lines, walletId,
      Codec::Bridge::WalletBackup::Type::ARMORY200_A,
      privPass, 1ms, 0, true,
      //the legacy armory account always starts with asset 0
      lookup - 1);

   {
      auto result = exportPrivateKeys(bridge_, walletId, privPass, true);
      ASSERT_TRUE(result.success) << "export failed: " << result.error;
      verifyExportSetMatchesExpected(result.keys, expectedKeys, true);

      std::map<BinaryData, BinaryData> restoredKeys;
      for (const auto& exported : result.keys) {
         restoredKeys.emplace(exported.assetId, exported.privKey);
      }

      //every key the restored wallet did recompute must match the original
      //export exactly (same asset id -> same private key)
      for (const auto& kp : restoredKeys) {
         auto it = originalKeys.find(kp.first);
         ASSERT_NE(it, originalKeys.end())
            << "restored an unexpected asset id: " << kp.first.toHexStr();
         EXPECT_EQ(kp.second.toHexStr(), it->second.toHexStr())
            << "private key mismatch for asset " << kp.first.toHexStr();
      }

      //the restored export must cover the full original set. Restoring may
      //recompute fewer keys than the original lookahead pool, which surfaces
      //the known "uncomputed private key" edge case rather than a defect in
      //the export path itself.
      EXPECT_EQ(restoredKeys.size(), originalKeys.size())
         << "restored " << restoredKeys.size() << " keys, expected "
         << originalKeys.size();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, ExportPublicKeysNoUnlock)
{
   std::string walletId;
   std::string privPass{"privPass1"};
   unsigned lookup = 4;
   size_t expectedKeyCount = 0;
   std::map<BinaryData, ExportedKey> expectedKeys;

   {
      Wallets::IO::CreateWalletParams params{
         homedir,
         {500ms, 0, SecureBinaryData::fromString(privPass)},
         {1ms, 0, {}},
         nullptr, lookup
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletId = assetWlt->getID();
      std::tie(expectedKeyCount, expectedKeys) = buildExpectedExportKeys(*assetWlt);
   }
   ASSERT_EQ(expectedKeyCount, static_cast<size_t>(lookup));

   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1ULL);
   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 1ULL);

   auto result = exportPublicKeys(bridge_, walletId);
   ASSERT_TRUE(result.success) << "export failed: " << result.error;
   verifyExportSetMatchesExpected(result.keys, expectedKeys, false);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, ExportPublicKeysLegacy)
{
   std::filesystem::path legacyWalletFile{"input_files/legacy.wallet"sv};
   const std::string walletId{"28m472Xbm"sv};
   std::string fileName = std::string{"armory_"} + walletId + "_.wallet";
   const std::filesystem::path wltPath{fileName};
   std::filesystem::copy(legacyWalletFile, homedir / wltPath);

   uint64_t refId = rand();
   {
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initUtils();
      request.setImportWallet((homedir / wltPath).string());
      pushRequest(bridge_, serializeCapnp(message));
   }

   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   auto reply = fromBridge.getReply();
   ASSERT_TRUE(reply.getSuccess());

   auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();
   refId = rand();
   {
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto mgrRequest = toBridge.initWalletManager();
      auto migrationRequest = mgrRequest.initMigrateWallet();
      migrationRequest.setWalletPath(wltPath.filename().string());
      migrationRequest.setCallbackId(callbackId);
      pushRequest(bridge_, serializeCapnp(message));
   }

   {
      auto unlockReply = waitOnReply();
      kj::ArrayPtr<const capnp::word> unlockWords(
         reinterpret_cast<const capnp::word*>(unlockReply->data.getPtr()),
         unlockReply->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader unlockReader(unlockWords);
      auto unlockBridge = unlockReader.getRoot<Codec::Bridge::FromBridge>();
      ASSERT_EQ(unlockBridge.which(), Codec::Bridge::FromBridge::NOTIFICATION);
      auto notif = unlockBridge.getNotification();
      ASSERT_EQ(notif.which(), Codec::Bridge::Notification::UNLOCK_REQUEST);

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      auto notifReply = toBridge.initNotification();
      notifReply.setSuccess(true);
      notifReply.setCounter(notif.getCounter());
      notifReply.setUnlockRequest("testnet");
      pushRequest(bridge_, serializeCapnp(message));
   }

   progressWalletCreation(bridge_, callbackId, "newpass", 250ms, 32, 104);

   {
      auto migResult = waitOnReply();
      kj::ArrayPtr<const capnp::word> migWords(
         reinterpret_cast<const capnp::word*>(migResult->data.getPtr()),
         migResult->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader migReader(migWords);
      auto migBridge = migReader.getRoot<Codec::Bridge::FromBridge>();
      ASSERT_EQ(migBridge.which(), Codec::Bridge::FromBridge::REPLY);
      auto migReply = migBridge.getReply();
      ASSERT_TRUE(migReply.getSuccess());
   }

   auto wallets = loadWallets(bridge_);
   ASSERT_EQ(wallets.size(), 1ULL);
   ASSERT_EQ(wallets.begin()->first, walletId);

   auto exportResult = exportPublicKeys(bridge_, walletId);
   ASSERT_TRUE(exportResult.success) << exportResult.error;
   ASSERT_GT(exportResult.keys.size(), 0ULL);

   const uint32_t legacyUncompressedP2PKH = uint32_t(
      AddressEntryType::P2PKH | AddressEntryType::Uncompressed);

   bool foundUncompressed = false;
   for (const auto& exported : exportResult.keys) {
      EXPECT_TRUE(exported.privKey.empty());
      EXPECT_NE(exported.addrType, 0u);
      if (!exported.pubKey.empty()) {
         EXPECT_EQ(exported.pubKey.getSize(), 65ULL);
         EXPECT_EQ(exported.addrType, legacyUncompressedP2PKH);
         foundUncompressed = true;
      }
      EXPECT_FALSE(exported.addressString.empty());
   }
   EXPECT_TRUE(foundUncompressed);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, ExportPublicKeysScopedToAccount)
{
   const std::string wltId = createAWallet(bridge_, 1ms, 4, true);
   ASSERT_FALSE(wltId.empty());

   auto wallets = loadWallets(bridge_);
   ASSERT_GE(wallets.size(), 1ULL);

   auto accIds = getWalletAccountIds(bridge_, wltId);
   ASSERT_GE(accIds.size(), 2u);

   auto allResult = exportPublicKeys(bridge_, wltId);
   ASSERT_TRUE(allResult.success) << allResult.error;
   ASSERT_GT(allResult.keys.size(), 0u);

   const std::string accId = *accIds.begin();
   auto scopedResult = exportPublicKeys(bridge_, wltId, accId);
   ASSERT_TRUE(scopedResult.success) << scopedResult.error;
   ASSERT_LT(scopedResult.keys.size(), allResult.keys.size());

   for (const auto& key : scopedResult.keys) {
      EXPECT_EQ(key.accountId, accId);
      EXPECT_TRUE(key.privKey.empty());
   }

   bool hasReceive = false;
   bool hasChange = false;
   for (const auto& key : scopedResult.keys) {
      if (key.chainRole == "Receive") {
         hasReceive = true;
      } else if (key.chainRole == "Change") {
         hasChange = true;
      }
   }
   EXPECT_TRUE(hasReceive);
   EXPECT_TRUE(hasChange);

   size_t scopedTotal = 0;
   for (const auto& id : accIds) {
      auto result = exportPublicKeys(bridge_, wltId, id);
      ASSERT_TRUE(result.success) << result.error;
      scopedTotal += result.keys.size();
      for (const auto& key : result.keys) {
         EXPECT_EQ(key.accountId, id);
      }
   }
   EXPECT_EQ(scopedTotal, allResult.keys.size());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, ExportPrivateKeysScopedToAccount)
{
   const std::string wltId = createAWallet(bridge_, 1ms, 4, true);
   ASSERT_FALSE(wltId.empty());

   auto accIds = getWalletAccountIds(bridge_, wltId);
   ASSERT_GE(accIds.size(), 2u);

   auto allResult = exportPrivateKeys(bridge_, wltId, "pass1");
   ASSERT_TRUE(allResult.success) << allResult.error;
   ASSERT_GT(allResult.keys.size(), 0u);

   const std::string accId = *accIds.begin();
   auto scopedResult = exportPrivateKeys(bridge_, wltId, "pass1", true, accId);
   ASSERT_TRUE(scopedResult.success) << scopedResult.error;
   ASSERT_LT(scopedResult.keys.size(), allResult.keys.size());

   for (const auto& key : scopedResult.keys) {
      EXPECT_EQ(key.accountId, accId);
      EXPECT_FALSE(key.privKey.empty());
   }

   size_t scopedTotal = 0;
   for (const auto& id : accIds) {
      auto result = exportPrivateKeys(bridge_, wltId, "pass1", true, id);
      ASSERT_TRUE(result.success) << result.error;
      scopedTotal += result.keys.size();
      for (const auto& key : result.keys) {
         EXPECT_EQ(key.accountId, id);
         EXPECT_FALSE(key.privKey.empty());
      }
   }
   EXPECT_EQ(scopedTotal, allResult.keys.size());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, ExtendAddressChain)
{
   /*
   NOTE: bridge is offline in this test. It covers the graceful handling of
   attempted wallet registration after generating a batch of fresh addresses
   */
   /* 1. create a wallet */
   std::filesystem::path walletPath;
   std::string walletId;
   std::string accountId;
   std::string dbId;

   //wallet 1
   {
      Wallets::IO::CreateWalletParams params{
         homedir,
         {1ms, 0, SecureBinaryData::fromString("pass1")},
         {1ms, 0, {}},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletPath = assetWlt->getDbFilename();
      walletId = assetWlt->getID();
   }
   ASSERT_FALSE(walletId.empty());

   /* 2. load it into bridge */
   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   ASSERT_EQ(wltList.begin()->first, walletPath.filename().string());

   {
      const auto& wltEntry = wltList.begin()->second;
      ASSERT_EQ(
         wltEntry.loadState,
         (int)Bridge::WalletLoadState::Ready
      );
      ASSERT_TRUE(wltEntry.staged);
      EXPECT_EQ(wltEntry.walletId, walletId);
   }

   {
      auto wallets = loadWallets(bridge_);
      ASSERT_EQ(wallets.size(), 1);
      auto wltIter = wallets.begin();
      ASSERT_EQ(wltIter->first, walletId);
      ASSERT_EQ(wltIter->second.lookup, 4);
      ASSERT_EQ(wltIter->second.useCount, -1);
      accountId = wltIter->second.accountId;
      dbId = wltIter->second.dbId;
   }

   /* 3. extend its address chain */
   try {
      auto wltData = extendAddressPool(bridge_,
         walletId, accountId, dbId, 10000, false);
      ASSERT_EQ(wltData.walletId, walletId);
      ASSERT_EQ(wltData.accountId, accountId);
      EXPECT_EQ(wltData.useCount, -1);
      EXPECT_EQ(wltData.lookup, 10004);
   } catch (const std::runtime_error& e) {
      ASSERT_TRUE(false) << e.what();
   }

   /* grab wallet data explicitly, check chain again */
   auto walletData = getWalletData(bridge_, walletId, accountId);
   ASSERT_EQ(walletData.walletId, walletId);
   ASSERT_EQ(walletData.accountId, accountId);
   EXPECT_EQ(walletData.useCount, -1);
   EXPECT_EQ(walletData.lookup, 10004);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, ForkWO)
{
   std::filesystem::path walletPath;
   std::string walletId;
   std::string accountId;
   std::string dbId;

   //wallet 1
   {
      Wallets::IO::CreateWalletParams params{
         homedir,
         {1ms, 0, SecureBinaryData::fromString("pass1")},
         {1ms, 0, {}},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletPath = assetWlt->getDbFilename();
      walletId = assetWlt->getID();
   }
   ASSERT_FALSE(walletId.empty());

   /* 2. load it into bridge */
   auto wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   ASSERT_EQ(wltList.begin()->first, walletPath.filename().string());

   {
      const auto& wltEntry = wltList.begin()->second;
      ASSERT_EQ(
         wltEntry.loadState,
         (int)Bridge::WalletLoadState::Ready
      );
      ASSERT_TRUE(wltEntry.staged);
      EXPECT_EQ(wltEntry.walletId, walletId);
   }

   {
      auto wallets = loadWallets(bridge_);
      ASSERT_EQ(wallets.size(), 1);
      auto wltIter = wallets.begin();
      ASSERT_EQ(wltIter->first, walletId);
      ASSERT_EQ(wltIter->second.lookup, 4);
      ASSERT_EQ(wltIter->second.useCount, -1);
      accountId = wltIter->second.accountId;
      dbId = wltIter->second.dbId;
   }

   /* 3. fork it */
   uint64_t refId = rand();
   {
      auto callbackId = Cryptography::PRNG::fortuna.generateRandom(10).toHexStr();

      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWallet();
      request.setWalletId(walletId);
      request.setForkWatchingOnly(callbackId);

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      //deal with notifications
      bool run = true;
      while (run) {
         auto result = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(result->data.getPtr()),
            result->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();

         ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::NOTIFICATION);
         auto notif = fromBridge.getNotification();
         ASSERT_EQ(notif.getCallbackId(), callbackId);
         switch (notif.which())
         {
            case Codec::Bridge::Notification::SET_PASSPHRASE:
            {
               auto counter = notif.getCounter();

               //push passphrase
               capnp::MallocMessageBuilder message;
               auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
               auto notifReply = toBridge.initNotification();
               notifReply.setSuccess(true);
               notifReply.setCounter(counter);
               auto capnSetPass = notifReply.initSetPassphrase();
               capnSetPass.setPassphrase("woCtrlPass");
               capnSetPass.setReuseKdf(true);

               auto rawReq = serializeCapnp(message);
               pushRequest(bridge_, rawReq);
               break;
            }

            case Codec::Bridge::Notification::CLEANUP:
            {
               run = false;
               break;
            }

            /*case Bridge::Notification::WALLET_PROGRESS:
            {
               auto prog = notif.getWalletProgress();
               switch (prog.which())
               {
               }
               break;
            }*/

            default:
               ASSERT_TRUE(false);
         }
      }
   }

   //handle reply
   auto result = waitOnReply();
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(result->data.getPtr()),
      result->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::REPLY);

   auto reply = fromBridge.getReply();
   ASSERT_EQ(reply.getReferenceId(), refId);
   ASSERT_TRUE(reply.getSuccess());
   std::filesystem::path woWltPath{
      std::string{reply.getWallet().getForkWatchingOnly()}};
   ASSERT_NE(woWltPath.filename(), walletPath.filename());

   /* unload full wallet */
   {
      uint64_t msgId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(msgId);
      auto request = toBridge.initWalletManager();
      request.setUnloadWallet(walletId);

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();

      auto reply = fromBridge.getReply();
      ASSERT_EQ(reply.getReferenceId(), msgId);
      ASSERT_TRUE(reply.getSuccess());
   }

   /* load WO wallet */
   wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   ASSERT_EQ(wltList.begin()->first, woWltPath.filename().string());
   ASSERT_EQ(wltList.begin()->second.loadState, CapnWalletState_Encrypted);

   ASSERT_TRUE(unlockWallet(woWltPath.filename().string(), "woCtrlPass"));
   wltList = listWallets(bridge_);
   ASSERT_EQ(wltList.size(), 1);
   ASSERT_EQ(wltList.begin()->first, woWltPath.filename().string());
   ASSERT_EQ(wltList.begin()->second.loadState, CapnWalletState_Ready);

   {
      auto wallets = loadWallets(bridge_);
      ASSERT_EQ(wallets.size(), 1);
      auto wltIter = wallets.begin();
      ASSERT_EQ(wltIter->first, walletId);
      ASSERT_EQ(wltIter->second.lookup, 4);
      ASSERT_EQ(wltIter->second.useCount, -1);
      ASSERT_EQ(wltIter->second.path, woWltPath);
      ASSERT_EQ(wltIter->second.accountId, accountId);
      ASSERT_NE(dbId, wltIter->second.dbId);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, Signer_FromTxSigCollect_Malformed)
{
   //a valid USTX: one p2pkh input, one output
   Signing::Signer ustx;
   ustx.addSpender(std::make_shared<Signing::ScriptSpender>(UTXO(
      COIN, 0, 0, 0, BinaryData(std::vector<uint8_t>(32, 0xab)),
      BtcUtils::getP2PKHScript(BinaryData(std::vector<uint8_t>(20, 1))))));
   ustx.addRecipient(std::make_shared<Signing::Recipient_P2WPKH>(
      BinaryData(std::vector<uint8_t>(20, 2)), COIN / 2));
   auto valid = ustx.toString(Signing::SignerStringFormat::TxSigCollect_Modern);

   auto signerId = getNewSigner(bridge_);
   ASSERT_FALSE(signerId.empty());

   //returns the reply's success flag, sets its error or USTX string
   auto signerRequest = [this, &signerId](std::string& result,
      const std::function<void(Codec::Bridge::SignerRequest::Builder&)>& set)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto req = toBridge.initSigner();
      req.setId(signerId);
      set(req);
      pushRequest(bridge_, serializeCapnp(message));

      auto resp = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto reply = reader.getRoot<Codec::Bridge::FromBridge>().getReply();
      EXPECT_EQ(reply.getReferenceId(), refId);
      if (!reply.getSuccess()) {
         result = reply.getError();
      } else if (reply.which() == Codec::Bridge::RpcReply::SIGNER) {
         result = reply.getSigner().getToTxSigCollect();
      }
      return reply.getSuccess();
   };
   auto fromTxSigCollect = [&](const std::string& str, std::string& error)
   {
      return signerRequest(error,
         [&str](Codec::Bridge::SignerRequest::Builder& req)
         { req.setFromTxSigCollect(str); });
   };

   //load the valid one first, malformed strings must leave it be
   std::string error;
   ASSERT_TRUE(fromTxSigCollect(valid, error)) << error;

   //valid header with: a junk body, a modern body whose capnp segment
   //table is junk (0x5a5a5a5a segments)
   auto header = valid.substr(0, valid.find('\n') + 1);
   auto footer = std::string(64, '=') + "\n";
   BinaryWriter capnBody;
   capnBody.put_uint32_t(3);
   capnBody.put_uint32_t(0);
   capnBody.put_BinaryData(BinaryData(std::vector<uint8_t>(32, 'Z')));
   auto capnStr = BtcUtils::base64_encode(std::string(
      capnBody.getData().getCharPtr(), capnBody.getSize()));

   std::vector<std::string> malformed = {
      "",
      valid.substr(0, valid.size() / 2),
      header + std::string(64, 'Z') + "\n" + footer,
      header + capnStr + "\n" + footer,
      BtcUtils::base64_encode("not a psbt either")
   };
   for (const auto& str : malformed) {
      std::string strError;
      EXPECT_FALSE(fromTxSigCollect(str, strError)) << str;
      EXPECT_FALSE(strError.empty()) << str;
   }

   //the bridge still serves, the signer kept the valid USTX
   std::string roundTrip;
   ASSERT_TRUE(signerRequest(roundTrip,
      [](Codec::Bridge::SignerRequest::Builder& req)
      {
         req.setToTxSigCollect(static_cast<uint32_t>(
            Signing::SignerStringFormat::TxSigCollect_Modern));
      }));
   EXPECT_EQ(roundTrip, valid);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, GetTxsByHash_OfflinePayload)
{
   //the 2 txs the inputs of the BIP174 test vector spend from
   auto rawTx1 = READHEX("0200000000010158e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd7501000000171600145f275f436b09a8cc9a2eb2a2f528485c68a56323feffffff02d8231f1b0100000017a914aed962d6654f9a2b36608eb9d64d2b260db4f1118700c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e88702483045022100a22edcc6e5bc511af4cc4ae0de0fcd75c7e04d8c1c3a8aa9d820ed4b967384ec02200642963597b9b1bc22c75e9f3e117284a962188bf5e8a74c895089046a20ad770121035509a48eb623e10aace8bfd0212fdb8a8e5af3c94b0b133b95e114cab89e4f7965000000");
   auto rawTx2 = READHEX("0200000001aad73931018bd25f84ae400b68848be09db706eac2ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d481c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b6393e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a270100000017a91429ca74f8a08f81999428185c97b5d852e4063f618765000000");
   Tx tx1(rawTx1);
   Tx tx2(rawTx2);
   auto getUtxo = [](const Tx& tx, unsigned index)->UTXO
   {
      auto txOut = tx.getTxOutCopy(index);
      return UTXO(txOut.getAmount(), UINT32_MAX, UINT16_MAX, index,
         tx.getThisHash(), txOut.getScript());
   };

   Signing::Signer ustx;
   ustx.setVersion(2);
   ustx.addSpender(std::make_shared<Signing::ScriptSpender>(getUtxo(tx2, 0)));
   ustx.addSpender(std::make_shared<Signing::ScriptSpender>(getUtxo(tx1, 1)));
   ustx.addRecipient(std::make_shared<Signing::Recipient_P2WPKH>(
      READHEX("d85c2b71d0060b09c9886aeb815e50991dda124d"), 149990000));
   ustx.addSupportingTx(rawTx1);
   ustx.addSupportingTx(rawTx2);

   auto unknownHash = READHEX(
      "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
   std::set<BinaryData> hashes{
      tx1.getThisHash(), tx2.getThisHash(), unknownHash};

   //nothing cached yet
   bool success = false;
   auto none = getTxDataByHash(bridge_, hashes, success);
   ASSERT_TRUE(success);
   EXPECT_TRUE(none.empty());

   //a malformed string is refused
   auto signerId = getNewSigner(bridge_);
   ASSERT_FALSE(signerId.empty());
   EXPECT_FALSE(loadSignerString(bridge_, signerId, "not a signer string"));
   EXPECT_TRUE(getTxDataByHash(bridge_, hashes, success).empty());

   //loading the USTX caches its supporting txs, served with no height. Each
   //format gets a fresh bridge, so its cache starts empty
   for (auto format : {Signing::SignerStringFormat::TxSigCollect_Modern,
      Signing::SignerStringFormat::PSBT}) {
      bridge_.reset();
      replyQueue.clear();
      bridge_ = std::make_shared<Bridge::CppBridge>(
         std::make_shared<TestBridgeSocket>());
      signerId = getNewSigner(bridge_);
      ASSERT_FALSE(signerId.empty());
      ASSERT_TRUE(getTxDataByHash(bridge_, hashes, success).empty());

      auto str = ustx.toString(format);
      ASSERT_TRUE(loadSignerString(bridge_, signerId, str));

      auto txs = getTxDataByHash(bridge_, hashes, success);
      ASSERT_TRUE(success);
      ASSERT_EQ(txs.size(), 2U);
      EXPECT_EQ(txs[tx1.getThisHash()].raw, rawTx1);
      EXPECT_EQ(txs[tx2.getThisHash()].raw, rawTx2);
      for (const auto& txPair : txs) {
         EXPECT_EQ(txPair.second.height, UINT32_MAX);
         EXPECT_EQ(txPair.second.txIndex, UINT32_MAX);
         EXPECT_EQ(txPair.second.timestamp, UINT32_MAX);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, GetTxsByHash_AddSupportingTx)
{
   auto rawTx1 = READHEX("0200000000010158e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd7501000000171600145f275f436b09a8cc9a2eb2a2f528485c68a56323feffffff02d8231f1b0100000017a914aed962d6654f9a2b36608eb9d64d2b260db4f1118700c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e88702483045022100a22edcc6e5bc511af4cc4ae0de0fcd75c7e04d8c1c3a8aa9d820ed4b967384ec02200642963597b9b1bc22c75e9f3e117284a962188bf5e8a74c895089046a20ad770121035509a48eb623e10aace8bfd0212fdb8a8e5af3c94b0b133b95e114cab89e4f7965000000");
   auto rawTx2 = READHEX("0200000001aad73931018bd25f84ae400b68848be09db706eac2ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d481c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b6393e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a270100000017a91429ca74f8a08f81999428185c97b5d852e4063f618765000000");
   auto hash1 = Tx(rawTx1).getThisHash();
   auto hash2 = Tx(rawTx2).getThisHash();

   auto signerId = getNewSigner(bridge_);
   ASSERT_FALSE(signerId.empty());

   //a truncated tx (no locktime) is skipped, the bridge stays up
   auto truncated = rawTx2.getSliceCopy(0, rawTx2.getSize() - 4);
   bool added = false;
   EXPECT_NO_THROW(added = addSupportingTx(bridge_, signerId, truncated));
   EXPECT_TRUE(added);

   bool success = false;
   EXPECT_TRUE(getTxDataByHash(bridge_, {hash1, hash2}, success).empty());
   ASSERT_TRUE(success);

   //a supporting tx added to a signer is cached as unpooled
   ASSERT_TRUE(addSupportingTx(bridge_, signerId, rawTx1));
   auto txs = getTxDataByHash(bridge_, {hash1, hash2}, success);
   ASSERT_TRUE(success);
   ASSERT_EQ(txs.size(), 1U);
   EXPECT_EQ(txs[hash1].raw, rawTx1);
   EXPECT_EQ(txs[hash1].height, UINT32_MAX);
   EXPECT_EQ(txs[hash1].txIndex, UINT32_MAX);
   EXPECT_EQ(txs[hash1].timestamp, UINT32_MAX);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, WalletManager_UnhandledRequest)
{
   //a WalletManager request with no field set (unset) isn't handled, it
   //gets an error reply and the bridge keeps serving
   uint64_t refId = rand();
   capnp::MallocMessageBuilder message;
   auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
   toBridge.setReferenceId(refId);
   auto request = toBridge.initWalletManager();
   ASSERT_EQ(request.which(), Codec::Bridge::WalletManagerRequest::UNSET);
   EXPECT_TRUE(Bridge::ProtoCommandParser::processData(
      bridge_, serializeCapnp(message)));

   auto resp = waitOnReplyFor(std::chrono::seconds(5));
   ASSERT_NE(resp, nullptr) << "no reply to an unhandled WalletManager request";
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
      resp->data.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
   ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::REPLY);
   auto reply = fromBridge.getReply();
   EXPECT_EQ(reply.getReferenceId(), refId);
   EXPECT_FALSE(reply.getSuccess());
   EXPECT_EQ(std::string(reply.getError()), "invalid WalletManager request");

   EXPECT_NO_THROW(listWallets(bridge_));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BridgeWalletTests, DbSetup_UnhandledHelperRequest)
{
   //peersHelper and satoshiHelper requests with no field set (unset)
   //aren't handled, they get an error reply and the bridge keeps serving
   auto unhandledRequest = [this](
      const std::function<void(Codec::Bridge::DbSetupRequest::Builder&)>& set,
      const std::string& expectedError)
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initSetup();
      set(request);
      EXPECT_TRUE(Bridge::ProtoCommandParser::processData(
         bridge_, serializeCapnp(message)));

      auto resp = waitOnReplyFor(std::chrono::seconds(5));
      ASSERT_NE(resp, nullptr) << "no reply, expected: " << expectedError;
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(resp->data.getPtr()),
         resp->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      ASSERT_EQ(fromBridge.which(), Codec::Bridge::FromBridge::REPLY);
      auto reply = fromBridge.getReply();
      EXPECT_EQ(reply.getReferenceId(), refId);
      EXPECT_FALSE(reply.getSuccess());
      EXPECT_EQ(std::string(reply.getError()), expectedError);
   };

   unhandledRequest([](Codec::Bridge::DbSetupRequest::Builder& req)
   {
      auto peers = req.initPeersHelper();
      ASSERT_EQ(peers.which(),
         Codec::Bridge::DbSetupRequest::PeersRequest::UNSET);
   }, "invalid peers helper request");

   unhandledRequest([](Codec::Bridge::DbSetupRequest::Builder& req)
   {
      auto satoshi = req.initSatoshiHelper();
      ASSERT_EQ(satoshi.which(),
         Codec::Bridge::DbSetupRequest::SatoshiRequest::UNSET);
   }, "invalid satoshi helper request");

   EXPECT_NO_THROW(listWallets(bridge_));
}
