////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BridgeTestUtils.h"

////////////////////////////////////////////////////////////////////////////////
// WalletManagerTests
class WalletManagerTests : public ::testing::Test
{
protected:
   void SetUp() override
   {
      homedir_ = std::filesystem::path("./fakehomedir");
      FileUtils::removeDirectory(homedir_);
      std::filesystem::create_directory(homedir_);

      Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Config::ProcessType::DB);
   }

   void TearDown() override
   {
      Config::reset();
      FileUtils::removeDirectory(homedir_);
   }

   std::filesystem::path homedir_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletManagerTests, ListStageLoad)
{
   /*
   covers the following scenarios:
      1: wallet without control passphrase
      2: with control passphrase, keep encrypted
      3: with control passphrase, decrypted
      4: with control passphrase, decrypted but unstaged
   */
   std::vector<std::filesystem::path> walletFiles;

   //wallet 1
   {
      Wallets::IO::CreateWalletParams params{
         homedir_,
         {1ms, 0, SecureBinaryData::fromString("privpass1")},
         {},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletFiles.emplace_back(assetWlt->getDbFilename());
   }

   //wallet 2, 3, 4
   for (unsigned i=2; i<5; i++) {
      Wallets::IO::CreateWalletParams params{
         homedir_,
         {1ms, 0, SecureBinaryData::fromString("privpass" + std::to_string(i))},
         {1ms, 0, SecureBinaryData::fromString("controlpass" + std::to_string(i))},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      walletFiles.emplace_back(assetWlt->getDbFilename());
   }

   //list wallets
   Bridge::WalletManager mgr{homedir_, nullptr};
   auto theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 4);
   for (const auto& path : walletFiles) {
      ASSERT_NE(theList.find(path.filename().string()), theList.end());
   }

   auto checkState = [&walletFiles](
      const std::map<std::string, std::shared_ptr<Bridge::WalletFileInfo>>& theList,
      unsigned intId, Bridge::WalletLoadState expLoadState, bool expectedStaged)->bool
   {
      auto listEntry = theList.at(walletFiles[intId].filename().string());
      if (listEntry->state() != expLoadState) {
         return false;
      }
      if (listEntry->state() == Bridge::WalletLoadState::Ready) {
         if (!listEntry->hasAccountIds()) {
            return false;
         } else {
            auto accIds = listEntry->getAccountIds();
            if (accIds.begin()->toHexStr() != legacyAccId) {
               return false;
            }
         }
         if (listEntry->walletId().empty()) {
            return false;
         }
      } else {
         if (listEntry->hasAccountIds()) {
            return false;
         }
      }
      return listEntry->staged() == expectedStaged;
   };

   EXPECT_TRUE(checkState(theList, 0, Bridge::WalletLoadState::Ready, true));
   EXPECT_TRUE(checkState(theList, 1, Bridge::WalletLoadState::Encrypted, false));
   EXPECT_TRUE(checkState(theList, 2, Bridge::WalletLoadState::Encrypted, false));
   EXPECT_TRUE(checkState(theList, 3, Bridge::WalletLoadState::Encrypted, false));

   //unlock wallets 3 & 4, fail for wtl2
   {
      try {
         mgr.unlockControlHeader(walletFiles[2].filename().string(), [](
            const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result {
               return { SecureBinaryData::fromString("controlpass3"), true };
            }
         );

         mgr.unlockControlHeader(walletFiles[3].filename().string(), [](
            const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result {
               return { SecureBinaryData::fromString("controlpass4"), true };
            }
         );
      } catch (const std::exception& e) {
         ASSERT_TRUE(false) << e.what();
      }

      unsigned count = 0;
      auto failUnlockLbd = [&count](
         const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result {
         if (count++ < 2) {
            return { Cryptography::PRNG::fortuna.generateRandom(10), true };
         }
         //give up after 2 tries
         return { {}, false };
      };
      try {
         mgr.unlockControlHeader(walletFiles[1].filename().string(), failUnlockLbd);
         ASSERT_TRUE(false);
      } catch (const Wallets::Encryption::DecryptedDataContainerException& e) {
         ASSERT_EQ(e.what(), std::string{"unlock request rejected"}) << e.what();
      }
   }

   //recheck the list
   theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 4);
   for (const auto& path : walletFiles) {
      ASSERT_NE(theList.find(path.filename().string()), theList.end());
   }
   EXPECT_TRUE(checkState(theList, 0, Bridge::WalletLoadState::Ready, true));
   EXPECT_TRUE(checkState(theList, 1, Bridge::WalletLoadState::Encrypted, false));
   EXPECT_TRUE(checkState(theList, 2, Bridge::WalletLoadState::Ready, true));
   EXPECT_TRUE(checkState(theList, 3, Bridge::WalletLoadState::Ready, true));

   //unstage wlt4
   {
      auto wlt4Info = theList.at(walletFiles[3].filename().string());
      ASSERT_TRUE(mgr.stageWallet(wlt4Info->walletId(), false));
   }

   //recheck the list
   theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 4);
   for (const auto& path : walletFiles) {
      ASSERT_NE(theList.find(path.filename().string()), theList.end());
   }
   EXPECT_TRUE(checkState(theList, 0, Bridge::WalletLoadState::Ready, true));
   EXPECT_TRUE(checkState(theList, 1, Bridge::WalletLoadState::Encrypted, false));
   EXPECT_TRUE(checkState(theList, 2, Bridge::WalletLoadState::Ready, true));
   EXPECT_TRUE(checkState(theList, 3, Bridge::WalletLoadState::Ready, false));

   //load wallets
   mgr.loadWallets();

   //check loaded wallets
   auto checkHasWallet = [&theList, &walletFiles, &mgr](unsigned intId)->bool
   {
      auto entry = theList.at(walletFiles[intId].filename().string());
      try {
         return mgr.hasWallet(entry->walletId());
      } catch (const std::exception&) {
         return false;
      }
      
   };

   EXPECT_TRUE(checkHasWallet(0));
   EXPECT_FALSE(checkHasWallet(1));
   EXPECT_TRUE(checkHasWallet(2));
   EXPECT_FALSE(checkHasWallet(3));

   //check list only has wlt1
   theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 2);

   ASSERT_EQ(theList.find(walletFiles[0].filename().string()), theList.end());
   ASSERT_NE(theList.find(walletFiles[1].filename().string()), theList.end());
   ASSERT_EQ(theList.find(walletFiles[2].filename().string()), theList.end());
   ASSERT_NE(theList.find(walletFiles[3].filename().string()), theList.end());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletManagerTests, ListWO)
{
   std::vector<std::filesystem::path> wltPaths;

   //wallet 1
   {
      Wallets::IO::CreateWalletParams params{
         homedir_,
         {1ms, 0, SecureBinaryData::fromString("privpass1")},
         {1ms, 0, SecureBinaryData::fromString("ctrlPass")},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      wltPaths.emplace_back(assetWlt->getDbFilename());
   }

   {
      auto woWltPath = Wallets::AssetWallet::forkWatchingOnly({wltPaths[0],
         [](const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result
         {
            return { SecureBinaryData::fromString("ctrlPass"), true };
         }},
         {1ms, 0, SecureBinaryData::fromString("woPass")}
      );
      ASSERT_FALSE(woWltPath.empty());
      ASSERT_TRUE(FileUtils::pathExists(woWltPath, 0));
      ASSERT_NE(woWltPath, wltPaths[0]);
      wltPaths.emplace_back(woWltPath);
   }

   //list wallets
   Bridge::WalletManager mgr{homedir_, nullptr};
   auto theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 2);
   for (const auto& path : wltPaths) {
      ASSERT_NE(theList.find(path.filename().string()), theList.end());
   }

   auto checkState = [&wltPaths](
      const std::map<std::string, std::shared_ptr<Bridge::WalletFileInfo>>& theList,
      unsigned intId, Bridge::WalletLoadState expLoadState,
      bool expectedStaged, bool isWO=false)->bool
   {
      auto listEntry = theList.at(wltPaths[intId].filename().string());
      if (listEntry->state() != expLoadState) {
         return false;
      }
      if (listEntry->state() == Bridge::WalletLoadState::Ready) {
         if (!listEntry->hasAccountIds()) {
            return false;
         } else {
            auto accIds = listEntry->getAccountIds();
            if (accIds.begin()->toHexStr() != legacyAccId) {
               return false;
            }
         }
         if (listEntry->walletId().empty()) {
            return false;
         }
      } else {
         if (listEntry->hasAccountIds()) {
            return false;
         }
      }
      if (isWO != listEntry->isWatchingOnly()) {
         return false;
      }
      return listEntry->staged() == expectedStaged;
   };

   EXPECT_TRUE(checkState(theList, 0,
      Bridge::WalletLoadState::Encrypted, false));
   EXPECT_TRUE(checkState(theList, 1,
      Bridge::WalletLoadState::Encrypted, false));

   //unlock the wallets
   {
      try {
         mgr.unlockControlHeader(wltPaths[0].filename().string(), [](
            const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result {
               return { SecureBinaryData::fromString("ctrlPass"), true };
            }
         );

         mgr.unlockControlHeader(wltPaths[1].filename().string(), [](
            const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result {
               return { SecureBinaryData::fromString("woPass"), true };
            }
         );
      } catch (const std::exception& e) {
         ASSERT_TRUE(false) << e.what();
      }
   }

   //recheck the list
   theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 2);
   for (const auto& path : wltPaths) {
      ASSERT_NE(theList.find(path.filename().string()), theList.end());
   }

   std::vector<std::string> wltIds;
   for (const auto& entry : theList) {
      wltIds.emplace_back(entry.second->walletId());
   }
   ASSERT_EQ(wltIds.size(), 2);
   ASSERT_EQ(wltIds[0], wltIds[1]);

   EXPECT_TRUE(checkState(theList, 0,
      Bridge::WalletLoadState::Ready, true, false));
   EXPECT_TRUE(checkState(theList, 1,
      Bridge::WalletLoadState::Ready, true, true));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletManagerTests, Migrate_Legacy)
{
   //copy test legacy wallet to datadir
   const auto& wltId = "28m472Xbm"sv;
   std::string fileName = std::string{"armory_"sv} +
      std::string{wltId} +
      std::string{"_.wallet"sv};
   const std::filesystem::path wltPath{fileName};
   std::filesystem::copy(
      "input_files/legacy.wallet"sv,
      homedir_ / wltPath
   );

   //list wallets
   Bridge::WalletManager mgr{homedir_, nullptr};
   auto theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 1);

   //sanity check
   {
      auto iter = theList.find(wltPath.filename().string());
      ASSERT_NE(iter, theList.end());
      EXPECT_EQ(iter->second->state(), Bridge::WalletLoadState::Legacy);
      EXPECT_EQ(iter->second->walletId(), wltId);
      EXPECT_FALSE(iter->second->staged());

      //a legacy wallet cannot be staged
      EXPECT_FALSE(mgr.stageWallet(iter->second->walletId(), true));
      EXPECT_FALSE(iter->second->staged());
   }

   //migrate the wallet
   auto passFunc = [](const std::set<Wallets::EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { SecureBinaryData::fromString("testnet"), true };
   };
   mgr.migrateWallet(wltPath.filename().string(), passFunc,
      Wallets::IO::CreateWalletParams{
         homedir_,
         {1ms, 0, {}},
         {1ms, 0, {}},
         nullptr, 0
      });

   theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 2);

   //check legacy wallet
   {
      auto iter = theList.find(wltPath.filename().string());
      ASSERT_NE(iter, theList.end());
      EXPECT_EQ(iter->second->state(), Bridge::WalletLoadState::Legacy);
      EXPECT_EQ(iter->second->walletId(), wltId);
      EXPECT_FALSE(iter->second->staged());
   }

   //check migrated wallet
   {
      auto iter = theList.begin();
      while (iter != theList.end()) {
         if (iter->first != wltPath.filename().string()) {
            break;
         }
         ++iter;
      }
      ASSERT_NE(iter, theList.end());
      EXPECT_EQ(iter->second->state(), Bridge::WalletLoadState::Ready);
      EXPECT_EQ(iter->second->walletId(), wltId);
      EXPECT_TRUE(iter->second->staged());
   }

   //load wallets
   mgr.loadWallets();

   //check migrated wallet
   try {
      auto wltContainer = mgr.getContainer(wltId);
      ASSERT_NE(wltContainer, nullptr);
      EXPECT_EQ(wltContainer->getHighestUsedIndex(), 2);
      EXPECT_EQ(wltContainer->getAccountId().toHexStr(), "f6e10000");

      auto wltPtr = wltContainer->getWalletPtr();
      EXPECT_EQ(wltPtr->getLabel(), "legacy1");
      EXPECT_EQ(wltPtr->getDescription(), "migration test");

      auto addrAccPtr = wltContainer->getAddressAccount();
      auto addrMap = addrAccPtr->getAddressHashMap();
      EXPECT_EQ(addrMap.size(), 104 * 3);

      //TODO: check addresses
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //TODO: check backup string, with version (should be 1.35c)
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletManagerTests, Migrate_Reject)
{
   //copy test legacy wallet to datadir
   const auto& wltId = "28m472Xbm"sv;
   std::string fileName = std::string{"armory_"sv} +
      std::string{wltId} +
      std::string{"_.wallet"sv};
   const std::filesystem::path wltPath{fileName};
   std::filesystem::copy(
      "input_files/legacy.wallet"sv,
      homedir_ / wltPath
   );

   //list wallets
   Bridge::WalletManager mgr{homedir_, nullptr};
   auto theList = mgr.listWallets();
   ASSERT_EQ(theList.size(), 1);

   //sanity check
   {
      auto iter = theList.find(wltPath.filename().string());
      ASSERT_NE(iter, theList.end());
      EXPECT_EQ(iter->second->state(), Bridge::WalletLoadState::Legacy);
      EXPECT_EQ(iter->second->walletId(), wltId);
      EXPECT_FALSE(iter->second->staged());
   }

   //reject the unlock
   auto passFunc = [](const std::set<Wallets::EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { SecureBinaryData::fromString("testnet"), false };
   };

   try {
      mgr.migrateWallet(wltPath.filename().string(), passFunc,
         Wallets::IO::CreateWalletParams{
            homedir_,
            {1ms, 0, {}},
            {1ms, 0, {}},
            nullptr, 0
         });
      ASSERT_TRUE(false);
   } catch (const std::exception& e) {
      EXPECT_EQ(e.what(), std::string{"rejected migration"});
   }
}
