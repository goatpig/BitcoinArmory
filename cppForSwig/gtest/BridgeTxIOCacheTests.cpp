////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BridgeTestUtils.h"

////////////////////////////////////////////////////////////////////////////////
// TxIOCacheTests
class TxIOCacheTests : public ::testing::Test
{
protected:
   void initBDM()
   {
      theBDMt_ = new BlockDataManagerThread();
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

      auto createWltLbd = []()->std::unique_ptr<Passphrase::Params>
      {
         return std::make_unique<Passphrase::Params>(
            1ms, 0, SecureBinaryData{});
      };

      auto serverPeersWlt = NetworkPeers::PeerStore::bootstrapWallet({
         homedir_ / SERVER_AUTH_PEER_FILENAME, {createWltLbd}});
      NetworkPeers::ServerStore serverPeers(serverPeersWlt);

      auto clientPeersWlt = NetworkPeers::PeerStore::bootstrapWallet({
         homedir_ / CLIENT_AUTH_PEER_FILENAME, {createWltLbd}
      });
      NetworkPeers::ClientStore clientPeers(clientPeersWlt);

      //share public keys between client and server
      NetworkPeers::PeerKey servPK{
         serverPeers.getOwnPublicKey(),
         NetworkPeers::PeerType::ServerOneWay};
      clientPeers.addPeer(
         servPK,
         {std::format("127.0.0.1:{}", Config::NetworkSettings::dbPort())},
         {});

      initBDM();
      auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);
      nodePtr->setBDM(theBDMt_->bdm());
      hexMagicBytes = Config::BitcoinSettings::getMagicBytes().toHexStr();
   }

   void TearDown() override
   {
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
   void startWSServer(std::shared_ptr<Clients> clients)
   {
      wsServerThr_ = std::thread(
         [](std::shared_ptr<Clients> clients){
            WebSocketServer::start(clients);
         }, clients
      );

      //arbitrary wait to give time to WS server to startup
      std::this_thread::sleep_for(100ms);
   }

   /////////////////////////////////////////////////////////////////////////////
   bool checkCache(const Bridge::TxIOCache& cache,
      const std::set<BinaryData>& txHashes,
      const std::set<BinaryData>& blockHashes)
   {
      /* first attempt a txio cache check, will need wider coverage */
      const auto& dbCache = cache.getDBCache();

      //check headers
      const auto& headerMap = dbCache->getHeaderMap();
      if (blockHashes.empty()) {
         if (!headerMap.empty()) {
            ADD_FAILURE() << "header map should be empty";
            return false;
         }
      } else {
         std::map<BinaryData, Ledgers::HeaderPtr> headerHashMap;
         for (const auto headerPair : headerMap) {
            headerHashMap.emplace(headerPair.second->thisHash, headerPair.second);
         }
         for (const auto& headerHash : blockHashes) {
            auto iter = headerHashMap.find(headerHash);
            if (iter == headerHashMap.end()) {
               ADD_FAILURE() << "missing header: " << headerHash.toHexStr();
               return false;
            }
            if (!iter->second->isMainBranch) {
               ADD_FAILURE() << "header isn't in main branch";
               return false;
            }
         }
      }

      //check txs
      for (const auto& txHash : txHashes) {
         try {
            auto tx = dbCache->getTxByHash(txHash);
            if (tx.getThisHash() != txHash) {
               ADD_FAILURE() << "tx in cache has different hash from expected";
               return false;
            }
            auto txKey = tx.getDBKey();
            if (!Types::isTxKeyValid(txKey)) {
               ADD_FAILURE() << "invalid txkey for hash " << txHash.toHexStr();
               return false;
            } else if (Types::isThisAZCKey(txKey)) {
               continue;
            }
            auto blockId = Types::getBlockIDFromTxKey(txKey);
            auto header = dbCache->getHeader(blockId);
            if (!header->isMainBranch) {
               ADD_FAILURE() << "header for tx is not in main branch";
               return false;
            }
            if (blockHashes.find(header->thisHash) == blockHashes.end()) {
               ADD_FAILURE() << "header for tx is not is unexpected";
               return false;
            }
         } catch (const std::out_of_range& e) {
            ADD_FAILURE() << e.what();
            return false;
         }
      }
      return true;
   }

   bool compareLedgers(const Bridge::TxIOCache& cache,
      const std::vector<Types::ScrAddr>& addresses,
      const std::vector<TestChain::LedgerEntryValue>& expected)
   {
      std::set<Types::ScrAddr> addrSet{addresses.begin(), addresses.end()};

      //resolve txios for our addresses
      auto resolveResult = cache.resolve(
         [&addrSet](const Types::ScrAddr& addr)->bool {
            return addrSet.find(addr) != addrSet.end();
         }, 0
      );
      auto zcResolveResult = cache.resolveZC(
         [&addrSet](const Types::ScrAddr& addr)->bool {
            return addrSet.find(addr) != addrSet.end();
         }
      );
      auto txios = resolveResult.txioMap;
      for (auto& zcTxioPair : zcResolveResult.txioMap) {
         auto iter = txios.emplace(zcTxioPair);
         if (!iter.second) {
            iter.first->second.merge(zcTxioPair.second);
         }
      }

      //build ledger context then ledgers
      auto context = Ledgers::prepareContext(txios, cache.getDBCache(), addrSet);
      auto ledgers = Ledgers::computeLedgerMap(txios, 0, UINT32_MAX, "", context);

      //sort ledgers in a vector
      std::vector<Ledgers::Entry> result;
      result.reserve(ledgers.size());
      for (auto& ledger : ledgers) {
         result.emplace_back(std::move(ledger.second));
      }
      std::sort(result.begin(), result.end(), Ledgers::DescendingOrder{});

      //compare with expected
      if (result.size() != expected.size()) {
         ADD_FAILURE() << "ledger count mismatch";
         return false;
      }
      for (unsigned i = 0; i < expected.size(); i++) {
         if (!checkLedgers(result[i], expected[i])) {
            ADD_FAILURE() << "ledger check failed at index " << i;
            return false;
         }
      }
      return true;
   }

protected:
   BlockDataManagerThread *theBDMt_;
   Passphrase::UnlockFunc authPeersPassLbd_;

   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;

   std::string hexMagicBytes;
   std::shared_ptr<Bridge::CppBridge> bridge_;
   std::string walletId_;
   std::thread wsServerThr_;

   std::vector<BinaryData> addressesBC{
      TestChain::scrAddrB,
      TestChain::scrAddrC,
   };

   std::set<BinaryData> txHashesAt5Blocks{
      TestChain::hash10,
      TestChain::hash20, TestChain::hash21, TestChain::hash22,
      TestChain::hash30, TestChain::hash33, TestChain::hash34,
      TestChain::hash43,
      TestChain::hash50, TestChain::hash51
   };

   std::set<BinaryData> blockHashesAt5{
      TestChain::blkHash1, TestChain::blkHash2, TestChain::blkHash3,
      TestChain::blkHash4, TestChain::blkHash5
   };
};

TEST_F(TxIOCacheTests, PurgeStates)
{
   /* a bdv request failure in TxIOCache::update should advertize a purge */

   //start WS server
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   auto clients = std::make_shared<Clients>(theBDMt_->bdm());
   startWSServer(clients);

   //connect a BDV
   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", Config::NetworkSettings::dbPort(),
      std::make_shared<NetworkPeers::ClientStore>(Wallets::IO::ReadOnlyFileParams{
         homedir_ / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
      true, pCallback
   );

   bdvObj->connectToRemote();
   bdvObj->registerWithDB(Config::BitcoinSettings::getMagicBytes().toHexStr());

   //register BC
   {
      auto asyncWlt = bdvObj->getWalletObj("wallet_bc");
      asyncWlt.registerAddresses(addressesBC, false);
   }

   //start BDM
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);

   bdvObj->goOnline();
   auto readyNotif = pCallback->waitOnNotification(
      DBTestUtils::UTCallback::BDMAction_Ready);
   ASSERT_NE(readyNotif, nullptr);
   ASSERT_EQ(readyNotif->action, DBTestUtils::UTCallback::BDMAction_Ready);
   ASSERT_EQ(readyNotif->newTop.getHeight(), 5);

   //update the cache via ready notif
   Bridge::TxIOCache cache;
   auto readyPush = std::make_shared<Bridge::Notifications::Push::Notif_NewBlock>(
      bdvObj->getSocketName(), NewBlockNotif{5, UINT32_MAX, {}, {}}, true);
   auto updateResult = cache.update(bdvObj, readyPush);
   EXPECT_EQ(updateResult.first, Bridge::TxIOCache::PurgeStatus::NoPurge);
   ASSERT_TRUE(checkCache(cache, txHashesAt5Blocks, blockHashesAt5));
   ASSERT_TRUE(compareLedgers(cache, addressesBC, TestChain::ledgersBC));

   //grab id for our bdv
   Types::BdvId bdvId;
   {
      auto bdvMap = clients->getBdvIds();
      ASSERT_EQ(bdvMap.size(), 1);
      bdvId = *bdvMap.begin();
   }

   //start hijacking bdv packets
   DBTestUtils::BdvPayloadNinja ninja{clients, bdvId};
   ninja.beginPayloadCapture();

   /** fail at getTxios **/
   std::promise<void> getTxiosProm;
   auto getTxiosFut = getTxiosProm.get_future();
   auto failGetTxiosThr = std::thread([&ninja, &getTxiosProm, bdvId](){
      uint32_t messageIdToFail;
      try {
         auto reader = ninja.getRequestReader();
         messageIdToFail = reader.first;
         if (reader.second == nullptr) {
            throw std::runtime_error("bad reader");
         }
         auto request = reader.second->getRoot<Codec::BDV::Request>();
         if (!checkBDVRequestType(request, Codec::BDV::BdvRequest::Which::GET_TXIOS)) {
            throw std::runtime_error("expected getTxios request");
         }
         getTxiosProm.set_value();
      } catch (const std::runtime_error&) {
         getTxiosProm.set_exception(std::current_exception());
      }

      //fail getTxios request
      ninja.failMessage(messageIdToFail);
   });
   auto newBlockPush = std::make_shared<Bridge::Notifications::Push::Notif_NewBlock>(
      bdvObj->getSocketName(), NewBlockNotif{6, UINT32_MAX, {}, {}}, false);
   updateResult = cache.update(bdvObj, newBlockPush);
   EXPECT_EQ(updateResult.first, Bridge::TxIOCache::PurgeStatus::PurgeAll);

   if (failGetTxiosThr.joinable()) {
      failGetTxiosThr.join();
   }
   try {
      getTxiosFut.get();
   } catch (const std::exception& e) {
      std::cout << e.what() << std::endl;
      ASSERT_TRUE(false);
   }

   /** fail at getTxs **/
   std::promise<void> getTxsProm;
   auto getTxsFut = getTxsProm.get_future();
   auto failGetTxsThr = std::thread([&ninja, &getTxsProm, bdvId](){
      uint32_t messageIdToFail;
      try {
         //grab intercepted getTxios request
         auto reader = ninja.getRequestReader();
         if (reader.second == nullptr) {
            throw std::runtime_error("bad reader");
         }
         auto request = reader.second->getRoot<Codec::BDV::Request>();
         if (!checkBDVRequestType(request, Codec::BDV::BdvRequest::Which::GET_TXIOS)) {
            throw std::runtime_error("expected getTxios request");
         }

         //let db reply to getTxios request
         ninja.passMessageToBDM(reader.first);

         //grab next 2 intercepted requests
         std::vector<std::pair<uint32_t, std::unique_ptr<capnp::MessageReader>>> readers;
         readers.emplace_back(ninja.getRequestReader());
         readers.emplace_back(ninja.getRequestReader());

         for (unsigned i=0; i<2; i++) {
            auto& reader = readers[i];
            if (reader.second == nullptr) {
               throw std::runtime_error("bad reader");
            }
            auto request = reader.second->getRoot<Codec::BDV::Request>();
            if (checkBDVRequestType(request, Codec::BDV::BdvRequest::Which::GET_TXS_BY_KEY)) {
               messageIdToFail = reader.first;
            } else if (checkStaticRequestType(request, Codec::BDV::StaticRequest::Which::GET_HEADERS_BY_ID)) {
               ninja.passMessageToBDM(reader.first);
            } else {
               throw std::runtime_error("expected getTxsByKey or getHeaders request");
            }
         }

         getTxsProm.set_value();
      } catch (const std::runtime_error& e) {
         std::cout << "blow up at getTxs: " << e.what() << std::endl;
         getTxsProm.set_exception(std::current_exception());
      }

      //fail getTxs request
      ninja.failMessage(messageIdToFail);
   });
   newBlockPush = std::make_shared<Bridge::Notifications::Push::Notif_NewBlock>(
      bdvObj->getSocketName(), NewBlockNotif{7, UINT32_MAX, {}, {}}, false);
   updateResult = cache.update(bdvObj, newBlockPush);
   EXPECT_EQ(updateResult.first, Bridge::TxIOCache::PurgeStatus::PurgeAll);

   if (failGetTxsThr.joinable()) {
      failGetTxsThr.join();
   }
   try {
      getTxsFut.get();
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   /**  fail at getHeaders **/
   std::promise<void> getHeadersProm;
   auto getHeadersFut = getHeadersProm.get_future();
   auto failGetHeadersThr = std::thread([&ninja, &getHeadersProm, bdvId](){
      uint32_t messageIdToFail;
      try {
         //grab intercepted getTxios request
         auto reader = ninja.getRequestReader();
         if (reader.second == nullptr) {
            throw std::runtime_error("bad reader");
         }
         auto request = reader.second->getRoot<Codec::BDV::Request>();
         if (!checkBDVRequestType(request, Codec::BDV::BdvRequest::Which::GET_TXIOS)) {
            throw std::runtime_error("expected getTxios request");
         }

         //let db reply to getTxios request
         ninja.passMessageToBDM(reader.first);

         //grab next 2 intercepted requests
         std::vector<std::pair<uint32_t, std::unique_ptr<capnp::MessageReader>>> readers;
         readers.emplace_back(ninja.getRequestReader());
         readers.emplace_back(ninja.getRequestReader());

         for (unsigned i=0; i<2; i++) {
            auto& reader = readers[i];
            if (reader.second == nullptr) {
               throw std::runtime_error("bad reader");
            }
            auto request = reader.second->getRoot<Codec::BDV::Request>();
            if (checkBDVRequestType(request, Codec::BDV::BdvRequest::Which::GET_TXS_BY_KEY)) {
               ninja.passMessageToBDM(reader.first);
            } else if (checkStaticRequestType(request, Codec::BDV::StaticRequest::Which::GET_HEADERS_BY_ID)) {
               messageIdToFail = reader.first;
            } else {
               throw std::runtime_error("expected getTxsByKey or getHeaders request");
            }
         }

         getHeadersProm.set_value();
      } catch (const std::runtime_error& e) {
         std::cout << "blow up at getHeaders: " << e.what() << std::endl;
         getHeadersProm.set_exception(std::current_exception());
      }

      //fail getHeaders request
      ninja.failMessage(messageIdToFail);
   });
   newBlockPush = std::make_shared<Bridge::Notifications::Push::Notif_NewBlock>(
      bdvObj->getSocketName(), NewBlockNotif{7, UINT32_MAX, {}, {}}, false);
   updateResult = cache.update(bdvObj, newBlockPush);
   EXPECT_EQ(updateResult.first, Bridge::TxIOCache::PurgeStatus::PurgeAll);

   if (failGetHeadersThr.joinable()) {
      failGetHeadersThr.join();
   }
   try {
      getHeadersFut.get();
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   /** fail at updateZC **/
   std::promise<void> updateZcProm;
   auto updateZcFut = updateZcProm.get_future();
   auto failUpdateZcThr = std::thread([&ninja, &updateZcProm, bdvId](){
      uint32_t messageIdToFail;
      try {
         auto reader = ninja.getRequestReader();
         messageIdToFail = reader.first;
         if (reader.second == nullptr) {
            throw std::runtime_error("bad reader");
         }
         auto request = reader.second->getRoot<Codec::BDV::Request>();
         if (!checkBDVRequestType(request, Codec::BDV::BdvRequest::Which::GET_TXS_BY_KEY)) {
            throw std::runtime_error("expected getTxsByKey request");
         }
         updateZcProm.set_value();
      } catch (const std::runtime_error& e) {
         std::cout << "blow up at updateZc: " << e.what() << std::endl;
         updateZcProm.set_exception(std::current_exception());
      }

      //fail getTxsByKey request
      ninja.failMessage(messageIdToFail);
   });
   auto zcPush = std::make_shared<Bridge::Notifications::Push::Notif_ZC>(
      bdvObj->getSocketName(), std::string{},
      std::vector<TxIOPair>{}, std::set<BinaryData>{});
   updateResult = cache.update(bdvObj, zcPush);
   EXPECT_EQ(updateResult.first, Bridge::TxIOCache::PurgeStatus::PurgeZC);

   if (failUpdateZcThr.joinable()) {
      failUpdateZcThr.join();
   }
   try {
      updateZcFut.get();
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //cleanup
   ninja.stopPayloadCapture();
   WebSocketServer::shutdown();
}

TEST_F(TxIOCacheTests, Purge)
{
   //start WS server
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   auto clients = std::make_shared<Clients>(theBDMt_->bdm());
   startWSServer(clients);

   //connect a BDV
   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", Config::NetworkSettings::dbPort(),
      std::make_shared<NetworkPeers::ClientStore>(Wallets::IO::ReadOnlyFileParams{
         homedir_ / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
      true, pCallback
   );

   bdvObj->connectToRemote();
   bdvObj->registerWithDB(Config::BitcoinSettings::getMagicBytes().toHexStr());

   //register BC
   {
      auto asyncWlt = bdvObj->getWalletObj("wallet_bc");
      asyncWlt.registerAddresses(addressesBC, false);
   }

   //start BDM
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);

   bdvObj->goOnline();
   auto readyNotif = pCallback->waitOnNotification(
      DBTestUtils::UTCallback::BDMAction_Ready);
   ASSERT_NE(readyNotif, nullptr);
   ASSERT_EQ(readyNotif->action, DBTestUtils::UTCallback::BDMAction_Ready);
   ASSERT_EQ(readyNotif->newTop.getHeight(), 5);

   //bootstrap cache via ready notif
   Bridge::TxIOCache cache;
   auto readyPush = std::make_shared<Bridge::Notifications::Push::Notif_NewBlock>(
      bdvObj->getSocketName(), NewBlockNotif{5, UINT32_MAX, {}, {}}, true);
   auto updateResult = cache.update(bdvObj, readyPush);
   EXPECT_EQ(updateResult.first, Bridge::TxIOCache::PurgeStatus::NoPurge);
   ASSERT_TRUE(checkCache(cache, txHashesAt5Blocks, blockHashesAt5));
   ASSERT_TRUE(compareLedgers(cache, addressesBC, TestChain::ledgersBC));

   //full purge the cache
   cache.purge(Bridge::TxIOCache::PurgeStatus::PurgeAll);
   ASSERT_TRUE(checkCache(cache, {}, {}));
   ASSERT_TRUE(compareLedgers(cache, addressesBC, {}));

   //refill the cache
   updateResult = cache.update(bdvObj, readyPush);
   EXPECT_EQ(updateResult.first, Bridge::TxIOCache::PurgeStatus::NoPurge);
   ASSERT_TRUE(checkCache(cache, txHashesAt5Blocks, blockHashesAt5));
   ASSERT_TRUE(compareLedgers(cache, addressesBC, TestChain::ledgersBC));

   //add a ZC
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr = BtcUtils::scrAddrToSegWitAddress(hash160);

   //get signed tx
   BinaryData signedTx;
   try {
      auto utxosC = DBTestUtils::getUTXOsForScrAddrs(
         theBDMt_->bdm(), { TestChain::scrAddrC });
      ASSERT_EQ(utxosC.size(), 2U);

      Signing::Signer signer;
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());

      signer.addSpender(std::make_shared<Signing::ScriptSpender>(utxosC[0]));
      signer.addRecipient(std::make_shared<Signing::Recipient_P2WPKH>(
         hash160, utxosC[0].amount - 10000));
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());
      signedTx = signer.serializeSignedTx();
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   auto now = std::chrono::system_clock::now();
   uint32_t broadcastTime = std::chrono::duration_cast<std::chrono::seconds>(
      now.time_since_epoch()).count();
   {
      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signedTx, 14000000);
      DBTestUtils::pushNewZc(theBDMt_, zcVec);
   }

   //wait on zc notif
   auto testNotifPtr = pCallback->waitOnNotification(
      DBTestUtils::UTCallback::BDMAction_ZC);
   ASSERT_NE(testNotifPtr, nullptr);
   ASSERT_EQ(testNotifPtr->txios.size(), 1);
   ASSERT_TRUE(testNotifPtr->invalidatedTxHashes.empty());

   //have the cache parse the zc
   auto zcPush = std::make_shared<Bridge::Notifications::Push::Notif_ZC>(
      std::string{}, std::string{},
      testNotifPtr->txios, testNotifPtr->invalidatedTxHashes);
   updateResult = cache.update(bdvObj, zcPush);
   EXPECT_EQ(updateResult.first, Bridge::TxIOCache::PurgeStatus::NoPurge);

   //check zc in cache
   Tx zcTx(signedTx);
   auto hashesAt5WithZc = txHashesAt5Blocks;
   hashesAt5WithZc.emplace(zcTx.getThisHash());
   ASSERT_TRUE(checkCache(cache, hashesAt5WithZc, blockHashesAt5));

   //check ledgers
   std::vector<TestChain::LedgerEntryValue> ledgersWithZc{
      TestChain::LedgerEntryValue{
         -10 * (int64_t)COIN,
         UINT32_MAX, 0, broadcastTime,
         zcTx.getThisHash(), false
      }
   };
   ledgersWithZc.insert(ledgersWithZc.end(),
      TestChain::ledgersBC.begin(), TestChain::ledgersBC.end());
   ASSERT_TRUE(compareLedgers(cache, addressesBC, ledgersWithZc));

   //purge the ZC then check the cache, the zc copy of the tx is gone
   cache.purge(Bridge::TxIOCache::PurgeStatus::PurgeZC);
   EXPECT_THROW(cache.getTxByHash(zcTx.getThisHash()), std::out_of_range);
   EXPECT_TRUE(cache.getDBCache()->getKeysForHash(zcTx.getThisHash()).empty());
   ASSERT_TRUE(checkCache(cache, txHashesAt5Blocks, blockHashesAt5));
   ASSERT_TRUE(compareLedgers(cache, addressesBC, TestChain::ledgersBC));
}

TEST_F(TxIOCacheTests, DoubleReorg)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat_);

   //start WS server
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   auto clients = std::make_shared<Clients>(theBDMt_->bdm());
   startWSServer(clients);

   //connect a BDV
   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", Config::NetworkSettings::dbPort(),
      std::make_shared<NetworkPeers::ClientStore>(Wallets::IO::ReadOnlyFileParams{
         homedir_ / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
      true, pCallback
   );

   bdvObj->connectToRemote();
   bdvObj->registerWithDB(Config::BitcoinSettings::getMagicBytes().toHexStr());

   std::vector<Types::ScrAddr> addrBCDE{
      TestChain::scrAddrB, TestChain::scrAddrC,
      TestChain::scrAddrD, TestChain::scrAddrE
   };

   //register addresses
   std::vector<Types::ScrAddr> addrAFLB{
      TestChain::scrAddrA, TestChain::scrAddrF,
      TestChain::lb1ScrAddr, TestChain::lb1ScrAddrP2SH,
      TestChain::lb2ScrAddr, TestChain::lb2ScrAddrP2SH
   };

   {
      auto wltBCDE = bdvObj->getWalletObj("wallet_bcde");
      wltBCDE.registerAddresses(addrBCDE, false);
      auto wltAFLB = bdvObj->getWalletObj("wallet_aflb");
      wltAFLB.registerAddresses(addrAFLB, false);
   }

   //start BDM
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);

   bdvObj->goOnline();
   auto readyNotif = pCallback->waitOnNotification(
      DBTestUtils::UTCallback::BDMAction_Ready);
   ASSERT_NE(readyNotif, nullptr);
   ASSERT_EQ(readyNotif->action, DBTestUtils::UTCallback::BDMAction_Ready);
   ASSERT_EQ(readyNotif->newTop.getHeight(), 4);

   //init cache
   Bridge::TxIOCache cache;
   auto readyPush = std::make_shared<Bridge::Notifications::Push::Notif_NewBlock>(
      bdvObj->getSocketName(), NewBlockNotif{4, UINT32_MAX, {}, {}}, true);
   auto updateResult = cache.update(bdvObj, readyPush);
   EXPECT_EQ(updateResult.first, Bridge::TxIOCache::PurgeStatus::NoPurge);

   //check cache
   ASSERT_TRUE(checkCache(cache, {
         TestChain::hash00,
         TestChain::hash10,
         TestChain::hash20, TestChain::hash21, TestChain::hash22,
         TestChain::hash30, TestChain::hash31, TestChain::hash32, TestChain::hash33, TestChain::hash34, TestChain::hash35,
         READHEX("45aa44ebb8bc87ad006abad80d0f246172289e92a863cca9be10470d05c6de4d") //tx 4A 0
      }, {
         TestChain::blkHash0, TestChain::blkHash1, TestChain::blkHash2, TestChain::blkHash3, TestChain::blkHash4A
      }
   ));
   ASSERT_TRUE(compareLedgers(cache, addrBCDE,
      { TestChain::ledgersBCDE_Reorg.begin() + 2, TestChain::ledgersBCDE_Reorg.end() }));
   ASSERT_TRUE(compareLedgers(cache, addrAFLB,
      { TestChain::ledgersAFLB_Reorg.begin() + 2, TestChain::ledgersAFLB_Reorg.end() }));

   /* first reorg */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   auto reorgBlock1 = pCallback->waitOnNotification(
      DBTestUtils::UTCallback::BDMAction_NewBlock);
   ASSERT_EQ(reorgBlock1->newTop.getHeight(), 5);
   ASSERT_EQ(reorgBlock1->newTop.getBranchHeight(), 3);

   //update cache with first reorg notif
   auto reorg1Push = std::make_shared<Bridge::Notifications::Push::Notif_NewBlock>(
      bdvObj->getSocketName(), reorgBlock1->newTop, false);
   updateResult = cache.update(bdvObj, reorg1Push);
   EXPECT_EQ(updateResult.first, Bridge::TxIOCache::PurgeStatus::NoPurge);

   //check cache
   ASSERT_TRUE(checkCache(cache, {
         TestChain::hash00,
         TestChain::hash10,
         TestChain::hash20, TestChain::hash21, TestChain::hash22,
         TestChain::hash30, TestChain::hash31, TestChain::hash32, TestChain::hash33, TestChain::hash34, TestChain::hash35,
         TestChain::hash40, TestChain::hash41, TestChain::hash42, TestChain::hash43,
         TestChain::hash50, TestChain::hash51, TestChain::hash52
      }, {
         TestChain::blkHash0, TestChain::blkHash1, TestChain::blkHash2, TestChain::blkHash3, TestChain::blkHash4, TestChain::blkHash5
      }
   ));
   ASSERT_TRUE(compareLedgers(cache, addrBCDE, TestChain::ledgersBCDE));
   ASSERT_TRUE(compareLedgers(cache, addrAFLB, TestChain::ledgersAFLB));

   /* second reorg */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   auto reorgBlock2 = pCallback->waitOnNotification(
      DBTestUtils::UTCallback::BDMAction_NewBlock);
   ASSERT_EQ(reorgBlock2->newTop.getHeight(), 5);
   ASSERT_EQ(reorgBlock2->newTop.getBranchHeight(), 3);

   //update cache with first reorg notif
   auto reorg2Push = std::make_shared<Bridge::Notifications::Push::Notif_NewBlock>(
      bdvObj->getSocketName(), reorgBlock2->newTop, false);
   updateResult = cache.update(bdvObj, reorg2Push);
   EXPECT_EQ(updateResult.first, Bridge::TxIOCache::PurgeStatus::NoPurge);

   //check cache
   ASSERT_TRUE(checkCache(cache, {
         TestChain::hash00,
         TestChain::hash10,
         TestChain::hash20, TestChain::hash21, TestChain::hash22,
         TestChain::hash30, TestChain::hash31, TestChain::hash32, TestChain::hash33, TestChain::hash34, TestChain::hash35,
         READHEX("45aa44ebb8bc87ad006abad80d0f246172289e92a863cca9be10470d05c6de4d"), //tx 4A 0
         READHEX("9bce56a46508c1ddfb4d8495c8c8eecc1735dcfe61631533ed85d3908bfa33f3"), //tx 5A 0
         TestChain::hash41, TestChain::hash42
      }, {
         TestChain::blkHash0, TestChain::blkHash1, TestChain::blkHash2, TestChain::blkHash3, TestChain::blkHash4A, TestChain::blkHash5A
      }
   ));
   ASSERT_TRUE(compareLedgers(cache, addrBCDE, TestChain::ledgersBCDE_Reorg));
   ASSERT_TRUE(compareLedgers(cache, addrAFLB, TestChain::ledgersAFLB_Reorg));
}

////////////////////////////////////////////////////////////////////////////////
// ChainDataTests
namespace
{
   //runs a function when it takes its lock for the nth time
   class HookedTxIOCache : public Bridge::TxIOCache
   {
   private:
      const unsigned hookAt_;
      const std::function<void(Bridge::TxIOCache&)> hook_;
      unsigned lockCount_ = 0;

      void initAfterLock(void) override
      {
         if (++lockCount_ == hookAt_) {
            hook_(*this);
         }
      }

   public:
      HookedTxIOCache(unsigned at,
         std::function<void(Bridge::TxIOCache&)> hook) :
         hookAt_(at), hook_(std::move(hook))
      {}
   };
}

class ChainDataTests : public TxIOCacheTests
{
protected:
   void TearDown() override
   {
      bdv_.reset();
      clients_.reset();
      callback_.reset();
      TxIOCacheTests::TearDown();
   }

   /////////////////////////////////////////////////////////////////////////////
   //starts the db with BCDE registered as a wallet and connects a bdv to it,
   //returns the top height once the bdv is online, UINT32_MAX on failure
   uint32_t goOnline()
   {
      WebSocketServer::initPeerStore({
         homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
      clients_ = std::make_shared<Clients>(theBDMt_->bdm());
      startWSServer(clients_);

      callback_ = std::make_shared<DBTestUtils::UTCallback>();
      bdv_ = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<NetworkPeers::ClientStore>(Wallets::IO::ReadOnlyFileParams{
            homedir_ / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, callback_
      );
      bdv_->connectToRemote();
      bdv_->registerWithDB(Config::BitcoinSettings::getMagicBytes().toHexStr());
      {
         auto wltBCDE = bdv_->getWalletObj("wallet_bcde");
         wltBCDE.registerAddresses(addrBCDE_, false);
      }

      theBDMt_->start(Config::DBSettings::initMode());
      if (theBDMt_->bdm()->waitUntilReady(5s) != BDMState::Ready) {
         ADD_FAILURE() << "bdm isn't ready";
         return UINT32_MAX;
      }
      bdv_->goOnline();
      auto readyNotif = callback_->waitOnNotification(
         DBTestUtils::UTCallback::BDMAction_Ready);
      if (readyNotif == nullptr) {
         ADD_FAILURE() << "no ready notification";
         return UINT32_MAX;
      }
      return readyNotif->newTop.getHeight();
   }

   /////////////////////////////////////////////////////////////////////////////
   //a signed tx spending all the utxos of the wallet address 'from': 'value'
   //goes to 'to', the rest less the fee to 'change' (P2PKH addresses). An rbf
   //tx flags its inputs as replaceable. Empty on failure
   BinaryData signTx(const Types::ScrAddr& from,
      const Types::ScrAddr& to, uint64_t value,
      const Types::ScrAddr& change, bool rbf)
   {
      try {
         auto utxos = DBTestUtils::getUTXOsForScrAddrs(
            theBDMt_->bdm(), { from });
         uint64_t total = 0;
         for (const auto& utxo : utxos) {
            total += utxo.amount;
         }
         if (total < value + fee_) {
            ADD_FAILURE() << "not enough coins to spend";
            return {};
         }

         Signing::Signer signer;
         for (const auto& utxo : utxos) {
            auto spender = std::make_shared<Signing::ScriptSpender>(utxo);
            if (rbf) {
               spender->setSequence(UINT32_MAX - 2);
            }
            signer.addSpender(spender);
         }
         signer.addRecipient(std::make_shared<Signing::Recipient_P2PKH>(
            to.getSliceCopy(1, 20), value));
         if (total > value + fee_) {
            signer.addRecipient(std::make_shared<Signing::Recipient_P2PKH>(
               change.getSliceCopy(1, 20), total - value - fee_));
         }

         auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
         feed->addPrivKey(TestChain::privKeyAddrB.getRef());
         feed->addPrivKey(TestChain::privKeyAddrC.getRef());
         feed->addPrivKey(TestChain::privKeyAddrD.getRef());
         feed->addPrivKey(TestChain::privKeyAddrE.getRef());
         signer.setFeed(feed);
         signer.sign();
         if (!signer.verify()) {
            ADD_FAILURE() << "unsigned tx";
            return {};
         }
         return signer.serializeSignedTx();
      } catch (const std::exception& e) {
         ADD_FAILURE() << e.what();
         return {};
      }
   }

   //pushes a zc to the db, returns its hash once the bdv was notified of it,
   //empty on failure
   Types::TxHash broadcastZc(const BinaryData& rawTx)
   {
      if (rawTx.empty()) {
         return {};
      }
      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(rawTx, 14000000);
      DBTestUtils::pushNewZc(theBDMt_, zcVec);

      auto zcNotif = callback_->waitOnNotification(
         DBTestUtils::UTCallback::BDMAction_ZC);
      if (zcNotif == nullptr || zcNotif->txios.empty()) {
         ADD_FAILURE() << "no zc notification";
         return {};
      }
      return Tx(rawTx).getThisHash();
   }

   //what the bridge pushes to have the cache resync with the db
   std::shared_ptr<Bridge::Notifications::Push::Payload> refreshPush() const
   {
      return std::make_shared<Bridge::Notifications::Push::Command_Refresh>(
         bdv_->getSocketName(), std::string{"refresh"},
         Bridge::Notifications::SyncState::ScanWallets);
   }

   Types::TxKey getDBKeyForHash(const Types::TxHash& txHash) const
   {
      return theBDMt_->bdm()->getIFace()->getDBKeyForHash(txHash);
   }

protected:
   std::shared_ptr<Clients> clients_;
   std::shared_ptr<DBTestUtils::UTCallback> callback_;
   std::shared_ptr<AsyncClient::BlockDataViewer> bdv_;

   const uint64_t fee_ = 10000;
   const Bridge::AddressFilter allAddresses_ =
      [](const Types::ScrAddr&)->bool { return true; };
   const std::vector<Types::ScrAddr> addrBCDE_{
      TestChain::scrAddrB, TestChain::scrAddrC,
      TestChain::scrAddrD, TestChain::scrAddrE
   };
};

TEST_F(ChainDataTests, GetZcUTXOs_TxNotCached)
{
   ASSERT_EQ(goOnline(), 5U);

   //an rbf zc from the wallet, its change goes back to the wallet
   auto zcHash = broadcastZc(signTx(TestChain::scrAddrC,
      TestChain::scrAddrA, 11 * COIN, TestChain::scrAddrC, true));
   ASSERT_FALSE(zcHash.empty());

   /* test starts here */
   std::vector<UTXO> zcUtxos, rbfUtxos;
   std::string error;
   HookedTxIOCache txioCache(2, [&](Bridge::TxIOCache& cache) {
      //an update takes the lock to set the txios (1), then to cache their
      //txs (2): in between, the zc txios have no tx in the cache. The zc
      //and rbf utxos are asked for there
      try {
         zcUtxos = cache.getUTXOs(UINT64_MAX, true, false, allAddresses_);
         rbfUtxos = cache.getUTXOs(UINT64_MAX, false, true, allAddresses_);
      } catch (const std::exception& e) {
         error = e.what();
      }
   });
   txioCache.update(bdv_, refreshPush());

   /* mid-update: no throw and no utxo */
   EXPECT_TRUE(error.empty()) << error;
   EXPECT_TRUE(zcUtxos.empty());
   EXPECT_TRUE(rbfUtxos.empty());

   //once they are cached, the zc's change is listed
   zcUtxos = txioCache.getUTXOs(UINT64_MAX, true, false, allAddresses_);
   ASSERT_EQ(zcUtxos.size(), 1U);
   EXPECT_EQ(zcUtxos[0].getTxHash().toHexStr(), zcHash.toHexStr());
   EXPECT_FALSE(txioCache.getUTXOs(
      UINT64_MAX, false, true, allAddresses_).empty());
}

TEST_F(ChainDataTests, FetchTxs_ZcAcrossPurge)
{
   ASSERT_EQ(goOnline(), 5U);

   //a zc from the wallet
   auto zcHash = broadcastZc(signTx(TestChain::scrAddrC,
      TestChain::scrAddrC, 11 * COIN, TestChain::scrAddrD, false));
   ASSERT_FALSE(zcHash.empty());

   //and a mined tx of the wallet, in its block
   const auto& mined = TestChain::ledgersBCDE.back();
   auto minedKey = getDBKeyForHash(mined.txHash);
   ASSERT_TRUE(Types::isTxKeyValid(minedKey));
   ASSERT_FALSE(Types::isThisAZCKey(minedKey));
   auto blockId = Types::getBlockIDFromTxKey(minedKey);

   /* test starts here */
   HookedTxIOCache txioCache(3, [](Bridge::TxIOCache& cache) {
      //fetchTxs takes the lock to see what the cache misses (1), then to see
      //which headers it misses (2), then to fill the cache (3). The zcs are
      //purged once the db has served the txs and the header, as on a reorg
      cache.purge(Bridge::TxIOCache::PurgeStatus::PurgeZC);
   });
   txioCache.fetchTxs(bdv_, {zcHash, mined.txHash});

   /* a purge ran mid-fetch: none of what the db served is cached */
   EXPECT_THROW(txioCache.getTxByHash(zcHash), std::out_of_range);
   EXPECT_THROW(txioCache.getTxByHash(mined.txHash), std::out_of_range);
   EXPECT_THROW(txioCache.getDBCache()->getHeader(blockId),
      std::out_of_range);

   //the next fetch caches them
   txioCache.fetchTxs(bdv_, {zcHash, mined.txHash});
   try {
      const auto& tx = txioCache.getTxByHash(zcHash);
      EXPECT_TRUE(Types::isThisAZCKey(tx.getDBKey()));
      EXPECT_NE(tx.getTxTime(), UINT32_MAX);
      EXPECT_EQ(txioCache.getTxByHash(mined.txHash).getDBKey(), minedKey);
      EXPECT_EQ(txioCache.getDBCache()->getHeader(blockId)->blockHeight,
         mined.height);
   } catch (const std::out_of_range& e) {
      ADD_FAILURE() << e.what();
   }
}
