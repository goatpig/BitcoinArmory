////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BridgeTestUtils.h"
#include <ZeroConf/Parser.h>
#include <ZeroConf/Utils.h>

////////////////////////////////////////////////////////////////////////////////
// BridgeChainDataTests
class BridgeChainDataTests : public ::testing::Test
{
protected:
   void initBDM()
   {
      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();
   }

   void startWSServer(std::shared_ptr<Clients> clients=nullptr)
   {
      wsServerThr_ = std::thread([bdm=theBDMt_->bdm()]
         (std::shared_ptr<Clients> clients) {
            if (clients == nullptr) {
               clients = std::make_shared<Clients>(bdm);
            }
            WebSocketServer::start(clients);
         }, clients
      );

      //arbitrary wait to give time to WS server to startup
      std::this_thread::sleep_for(100ms);
   }

   ////////
   void prepareWallets()
   {
      auto pubKeyB = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrB);
      auto pubKeyC = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrC);
      auto pubKeyD = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrD);
      auto pubKeyE = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrE);
      auto pubKeyF = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrF);

      walletId_BCDE_ = createImportWallet(homedir_, {
         TestChain::privKeyAddrB,
         TestChain::privKeyAddrC,
         TestChain::privKeyAddrD,
         TestChain::privKeyAddrE
      });
      walletId_BC_ = createWOWallet(homedir_,
         { pubKeyB, pubKeyC }
      );
      walletId_DE_ = createWOWallet(homedir_,
         { pubKeyD, pubKeyE }
      );
      walletId_AFLB_ = createWOWallet(homedir_,
         { pubKeyF }, {
            TestChain::scrAddrA,
            TestChain::lb1ScrAddr, TestChain::lb1ScrAddrP2SH,
            TestChain::lb2ScrAddr, TestChain::lb2ScrAddrP2SH
         }
      );
   }

   void loadWallets(const std::set<std::string>& walletIds)
   {
      ASSERT_FALSE(walletId_BCDE_.empty());
      ASSERT_FALSE(walletId_BC_.empty());
      ASSERT_FALSE(walletId_DE_.empty());
      ASSERT_FALSE(walletId_AFLB_.empty());

      auto wltList = listWallets(bridge_);
      ASSERT_EQ(wltList.size(), 4);

      for (auto& wltData : wltList) {
         auto iter = walletIds.find(wltData.second.walletId);
         if (iter == walletIds.end()) {
            stageWallet(bridge_, wltData.second.walletId, false);
         }

         if (wltData.second.walletId == walletId_BCDE_) {
            accountId_BCDE_ = *wltData.second.accountIds.begin();
         } else if (wltData.second.walletId == walletId_BC_) {
            accountId_BC_ = *wltData.second.accountIds.begin();
         } else if (wltData.second.walletId == walletId_DE_) {
            accountId_DE_ = *wltData.second.accountIds.begin();
         } else if (wltData.second.walletId == walletId_AFLB_) {
            accountId_AFLB_ = *wltData.second.accountIds.begin();
         }
      }

      auto wallets = ::loadWallets(bridge_);
      auto wltIdsCopy = walletIds;
      for (const auto& wltPair : wallets) {
         auto iter = wltIdsCopy.find(wltPair.second.walletId);
         if (iter != wltIdsCopy.end()) {
            wltIdsCopy.erase(iter);
         }
      }
      ASSERT_TRUE(wltIdsCopy.empty());

      ASSERT_FALSE(accountId_BCDE_.empty());
      ASSERT_FALSE(accountId_BC_.empty());
      ASSERT_FALSE(accountId_DE_.empty());
      ASSERT_FALSE(accountId_AFLB_.empty());
   }

   std::map<std::string, std::string> loadWallet(const std::string& walletId)
   {
      auto wltList = listWallets(bridge_);
      for (auto& wltData : wltList) {
         if (walletId == wltData.second.walletId) {
            stageWallet(bridge_, wltData.second.walletId, true);
         }
      }

      std::map<std::string, std::string> result;
      auto wallets = ::loadWallets(bridge_);
      for (const auto& wltData : wallets) {
         if (wltData.second.walletId == walletId) {
            result.emplace(wltData.second.accountId, wltData.second.dbId);
         }
      }
      return result;
   }

   bool updateWalletFilters(
      const std::map<std::string, std::set<std::string>>& filters)
   {
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(rand());
      auto request = toBridge.initWalletManager();
      size_t count = 0;
      for (const auto& wltIdPair : filters) {
         count += wltIdPair.second.size();
      }
      auto filterReq = request.initUpdateMainLedgerFilter(count);

      count = 0;
      for (const auto& wltIdPair : filters) {
         for (const auto& accId : wltIdPair.second) {
            auto capnIds = filterReq[count++];
            capnIds.setWalletId(wltIdPair.first);
            capnIds.setAccountId(accId);
         }
      }

      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      auto result = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(result->data.getPtr()),
         result->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() != Codec::Bridge::FromBridge::NOTIFICATION) {
         return false;
      }
      auto notif = fromBridge.getNotification();
      if (notif.which() != Codec::Bridge::Notification::REFRESH) {
         return false;
      }
      auto notifIds = notif.getRefresh();
      if (notifIds.size() != 1 ||
         notifIds[0] != std::string{"wallet_filter_changed"}) {
         return false;
      }
      return true;
   }

   ////////
   void goOnlineAt(unsigned top)
   {
      //serves the blocks set so far, connects the bridge, registers the
      //wallets and waits on the bridge to go online at height top
      WebSocketServer::initPeerStore({
         homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
      startWSServer();

      ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
      ASSERT_TRUE(registerWallets(bridge_));
      theBDMt_->start(Config::DBSettings::initMode());
      ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
      ASSERT_EQ(goOnline(bridge_), top);
   }

   void parseDbArgs(bool clearMempool)
   {
      std::vector<std::string> args{
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_BARE",
         "--thread-count=3",
         "--public"};
      if (clearMempool) {
         args.emplace_back("--clear-mempool");
      }
      Config::parseArgs(args, Config::ProcessType::DB);
   }

   template<typename Func>
   auto readFromBridge(const MsgPtr& msg, Func func)
   {
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(msg->data.getPtr()),
         msg->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      return func(reader.getRoot<Codec::Bridge::FromBridge>());
   }

   void waitOnDisconnected()
   {
      //skips the notifications that come before it
      while (true) {
         auto msg = waitOnReplyFor(10s);
         ASSERT_NE(msg, nullptr) << "no DISCONNECTED notification";
         bool disconnected = readFromBridge(msg,
            [](Codec::Bridge::FromBridge::Reader fromBridge) {
               return fromBridge.which() ==
                     Codec::Bridge::FromBridge::NOTIFICATION &&
                  fromBridge.getNotification().which() ==
                     Codec::Bridge::Notification::DISCONNECTED;
            });
         if (disconnected) {
            return;
         }
      }
   }

   void restartDb(bool clearMempool)
   {
      //the db goes away, comes back on the same blocks, the bridge
      //reconnects to it
      WebSocketServer::shutdown();
      waitOnDisconnected();
      if (wsServerThr_.joinable()) {
         wsServerThr_.join();
      }
      theBDMt_->shutdown();
      delete theBDMt_;

      WebSocketServer::init();
      Config::reset();
      Config::DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);
      parseDbArgs(clearMempool);
      initBDM();
      nodePtr_ = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);
      nodePtr_->setBDM(theBDMt_->bdm());
      goOnlineAt(5);
   }

   ////////
   BinaryData signFromBCDE(const std::vector<UTXO>& utxos, uint64_t value)
   {
      //pays value to a new address from utxos (the wallet picks them if
      //empty), rbf, the change going to C
      auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
      auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
         recipientPrivKey, true);
      auto recipientAddr = BtcUtils::scrAddrToSegWitAddress(
         BtcUtils::getHash160(recipientPubKey));
      return createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         utxos, {{ recipientAddr, value }},
         TestChain::scrAddrC, 2, true,
         "privPass1"
      );
   }

   BinaryData broadcastZc(const BinaryData& signedTx, bool throughRpc)
   {
      //broadcasts, waits on the zc notif and returns the tx hash. Throws
      //if the notif isn't for that tx: gtest fails the test.
      //throughRpc: the node knows the hash already and the tx goes
      //through RPC, so it takes a single zc id wherever the test runs. A P2P
      //push takes one for its batch and another for the RPC fallback when
      //the mock watcher misses the tx (its inv queue is static, a node going
      //away stops it: after restartDb or an earlier test), and a node that
      //hasn't seen the hash pushes an inv of its own, another batch with
      //another id
      auto txHash = Tx(signedTx).getThisHash();
      if (throughRpc) {
         nodePtr_->presentZcHash(txHash);
      }
      broadcastTx(bridge_, signedTx, throughRpc);
      auto zcLedgers = waitOnZc();
      if (zcLedgers.empty() || zcLedgers[0].getTxHash() != txHash) {
         throw std::runtime_error("no zc notif for the tx");
      }
      if (throughRpc && zcLedgers.size() != 1) {
         throw std::runtime_error("expected 1 zc ledger");
      }
      return txHash;
   }

   BinaryData spendFromBCDE(
      uint32_t height, uint32_t txIndex, uint32_t txOutIndex)
   {
      //a zc spending the BCDE wallet's output at height/txIndex/txOutIndex.
      //Throws if the wallet has no such output
      auto utxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 0);
      auto iter = std::find_if(utxos.begin(), utxos.end(),
         [&](const UTXO& utxo)->bool {
            return utxo.getHeight() == height &&
               utxo.getTxIndex() == txIndex &&
               utxo.getTxOutIndex() == txOutIndex;
      });
      if (iter == utxos.end()) {
         throw std::runtime_error("no such utxo");
      }
      return broadcastZc(signFromBCDE({*iter}, 1 * COIN), false);
   }

   ////////
   auto dbZcKey(const BinaryData& txHash)
   {
      auto snapshot = theBDMt_->bdm()->zeroConfCont()->getSnapshot();
      if (snapshot == nullptr) {
         return Types::INVALID_TX_KEY;
      }
      return snapshot->getKeyForHash(txHash);
   }

   std::map<BinaryData, FetchedTx> getTxsByHash(
      const std::set<BinaryData>& hashes)
   {
      //the bridge serves every one of hashes
      bool success = false;
      auto served = getTxDataByHash(bridge_, hashes, success);
      EXPECT_TRUE(success);
      EXPECT_EQ(served.size(), hashes.size());
      return served;
   }

   std::set<std::string> zcLedgerHashes(const std::string& delegateId)
   {
      std::set<std::string> result;
      for (const auto& ledger : getLedgersPage(bridge_, delegateId, 0)) {
         if (ledger.getBlockNum() == UINT32_MAX) {
            result.emplace(ledger.getTxHash().toHexStr());
         }
      }
      return result;
   }

   std::set<std::pair<std::string, uint32_t>> minedLedgers(
      const std::string& delegateId)
   {
      //hash and height of the mined ledgers
      std::set<std::pair<std::string, uint32_t>> result;
      for (const auto& ledger : getLedgersPage(bridge_, delegateId, 0)) {
         if (ledger.getBlockNum() != UINT32_MAX) {
            result.emplace(ledger.getTxHash().toHexStr(), ledger.getBlockNum());
         }
      }
      return result;
   }

   void checkZcsAgainstDb(const std::string& tag,
      const std::string& delegateId, const std::set<BinaryData>& hashes,
      size_t inMempool)
   {
      //a zc the db's mempool has is served as a zc, with the time the
      //mempool got it, and the wallet's ledgers list it. One the mempool
      //doesn't have is neither: this bridge broadcast it, so all it serves
      //is its unpooled copy, with no time
      auto snapshot = theBDMt_->bdm()->zeroConfCont()->getSnapshot();
      ASSERT_NE(snapshot, nullptr) << tag;

      auto zcLedgers = zcLedgerHashes(delegateId);
      auto served = getTxsByHash(hashes);
      size_t count = 0;
      for (const auto& txHash : hashes) {
         auto hashStr = txHash.toHexStr(true);
         auto iter = served.find(txHash);
         if (iter == served.end()) {
            ADD_FAILURE() << tag << ": " << hashStr << " not served";
            continue;
         }
         EXPECT_EQ(iter->second.height, UINT32_MAX) << tag << ": " << hashStr;
         EXPECT_EQ(iter->second.txIndex, UINT32_MAX) << tag << ": " << hashStr;

         auto zcKey = snapshot->getKeyForHash(txHash);
         if (!Types::isTxKeyValid(zcKey)) {
            EXPECT_EQ(iter->second.timestamp, UINT32_MAX) << tag << ": " <<
               hashStr << " isn't in the db's mempool, served as a zc";
            EXPECT_EQ(zcLedgers.count(txHash.toHexStr()), 0U) << tag << ": " <<
               hashStr;
            continue;
         }
         ++count;
         auto zcTx = snapshot->getTxByKey(zcKey);
         ASSERT_NE(zcTx, nullptr);
         EXPECT_EQ(iter->second.timestamp, zcTx->getTxObj().getTxTime())
            << tag << ": " << hashStr;
         EXPECT_EQ(zcLedgers.count(txHash.toHexStr()), 1U) << tag << ": " <<
            hashStr;
      }
      EXPECT_EQ(count, inMempool) << tag;
   }

   ////////
   std::pair<bool, std::string> waitOnReplyStatus(uint64_t refId)
   {
      //success and error of the reply to refId, skipping notifications and
      //other replies
      while (true) {
         auto msg = waitOnReplyFor(10s);
         if (msg == nullptr) {
            return { false, "no reply" };
         }
         auto status = readFromBridge(msg,
            [refId](Codec::Bridge::FromBridge::Reader fromBridge)
               ->std::optional<std::pair<bool, std::string>> {
               if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
                  return std::nullopt;
               }
               auto reply = fromBridge.getReply();
               if (reply.getReferenceId() != refId) {
                  return std::nullopt;
               }
               return std::make_pair(
                  reply.getSuccess(), std::string(reply.getError().cStr()));
            });
         if (status.has_value()) {
            return *status;
         }
      }
   }

   std::pair<bool, std::string> getLedgerPageStatus(
      const std::string& delegateId, uint32_t page)
   {
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      uint64_t refId = rand();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initDelegate();
      request.setId(delegateId);
      auto pageReq = request.initGetPages();
      pageReq.setFirst(page);
      pageReq.setLast(page);
      pushRequest(bridge_, serializeCapnp(message));
      return waitOnReplyStatus(refId);
   }

   std::pair<bool, std::string> getFeeForMaxValStatus(
      const std::string& csId, float feeByte)
   {
      //the bridge processes the request on this thread: a throw from it
      //reaches the caller
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      uint64_t refId = rand();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initCoinSelection();
      request.setId(csId);
      auto feeReq = request.initGetFeeForMaxVal();
      feeReq.setFeeByte(feeByte);
      pushRequest(bridge_, serializeCapnp(message));
      return waitOnReplyStatus(refId);
   }

   ////////
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
      TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

      WebSocketServer::init();
      parseDbArgs(false);
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
      NetworkPeers::PeerKey servPK{serverPeers.getOwnPublicKey(), NetworkPeers::PeerType::ServerOneWay};
      serverPubkey_ = servPK.toHumanReadable();

      replyQueue.clear();
      bridge_ = std::make_shared<Bridge::CppBridge>(
         std::make_shared<TestBridgeSocket>());

      initBDM();
      nodePtr_ = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);
      nodePtr_->setBDM(theBDMt_->bdm());
      hexMagicBytes = Config::BitcoinSettings::getMagicBytes().toHexStr();
   }

   ////////
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

protected:
   BlockDataManagerThread *theBDMt_;
   Passphrase::UnlockFunc authPeersPassLbd_;
   LMDBBlockDatabase* iface_;

   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;

   std::string serverPubkey_;
   std::string hexMagicBytes;
   std::shared_ptr<Bridge::CppBridge> bridge_;
   std::shared_ptr<NodeUnitTest> nodePtr_;

   std::string walletId_BCDE_;
   std::string walletId_BC_;
   std::string walletId_DE_;
   std::string walletId_AFLB_;

   std::string accountId_BCDE_;
   std::string accountId_BC_;
   std::string accountId_DE_;
   std::string accountId_AFLB_;
   std::thread wsServerThr_;
};

////////////////////////////////////////////////////////////////////////////////
// ledgers
TEST_F(BridgeChainDataTests, Check5Blocks_BCDE)
{
   loadWallets({walletId_BCDE_});
   ASSERT_TRUE(checkHasImports(bridge_, walletId_BCDE_));

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check address balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }
}

TEST_F(BridgeChainDataTests, GetTxsByHash_DbFallback)
{
   loadWallets({walletId_BCDE_});
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //the wallet's txs, served from this bridge's cache
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());
   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   std::set<BinaryData> hashes;
   std::map<BinaryData, uint32_t> heights;
   for (const auto& ledger : ledgers) {
      hashes.emplace(ledger.getTxHash());
      heights.emplace(ledger.getTxHash(), ledger.getBlockNum());
   }

   bool success = false;
   auto fromCache = getTxDataByHash(bridge_, hashes, success);
   ASSERT_TRUE(success);
   ASSERT_EQ(fromCache.size(), hashes.size());
   for (const auto& txPair : fromCache) {
      //the block of each tx and its index in it, as the db has them
      auto txKey = iface_->getDBKeyForHash(txPair.first);
      ASSERT_TRUE(Types::isTxKeyValid(txKey));
      auto header = theBDMt_->bdm()->blockchain()->getHeaderById(
         Types::getBlockIDFromTxKey(txKey));
      ASSERT_NE(header, nullptr);
      EXPECT_EQ(txPair.second.height, heights[txPair.first]);
      EXPECT_EQ(txPair.second.height, header->getBlockHeight());
      EXPECT_EQ(txPair.second.txIndex, Types::getTxIndexFromTxKey(txKey));
      EXPECT_EQ(txPair.second.timestamp, header->getTimestamp());
   }

   //a fresh bridge with no wallet has nothing cached, the db still has
   //them (DB_BARE keeps the hashes of txs that pay or spend from a
   //registered address)
   bridge_.reset();
   replyQueue.clear();
   bridge_ = std::make_shared<Bridge::CppBridge>(
      std::make_shared<TestBridgeSocket>());

   loadWallets({});
   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   ASSERT_EQ(goOnline(bridge_), 5);

   //unknown and malformed hashes are left out, the rest is served
   auto unknownHash = READHEX(
      "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
   auto shortHash = READHEX("0011223344");
   auto request = hashes;
   request.emplace(unknownHash);
   request.emplace(shortHash);

   auto fromDb = getTxDataByHash(bridge_, request, success);
   ASSERT_TRUE(success);
   EXPECT_EQ(fromDb, fromCache);

   //finding nothing is an empty reply, not an error
   auto nothing = getTxDataByHash(bridge_, {unknownHash, shortHash}, success);
   EXPECT_TRUE(success);
   EXPECT_TRUE(nothing.empty());
}

TEST_F(BridgeChainDataTests, GetTxsByHash_PayloadThenDb)
{
   loadWallets({walletId_BCDE_});
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //a mined tx of the wallet, as its bridge serves it
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());
   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_FALSE(ledgers.empty());
   auto txHash = ledgers.back().getTxHash();

   bool success = false;
   auto fromWallet = getTxDataByHash(bridge_, {txHash}, success);
   ASSERT_TRUE(success);
   ASSERT_EQ(fromWallet.size(), 1U);
   ASSERT_NE(fromWallet[txHash].height, UINT32_MAX);

   //a fresh bridge with no wallet gets that tx as the supporting tx of a
   //USTX: unpooled while the bridge has no db, the db's mined copy once it
   //has one
   bridge_.reset();
   replyQueue.clear();
   bridge_ = std::make_shared<Bridge::CppBridge>(
      std::make_shared<TestBridgeSocket>());
   loadWallets({});

   const auto& rawTx = fromWallet[txHash].raw;
   Tx tx(rawTx);
   auto txOut = tx.getTxOutCopy(0);
   Signing::Signer ustx;
   ustx.addSpender(std::make_shared<Signing::ScriptSpender>(UTXO(
      txOut.getAmount(), UINT32_MAX, UINT16_MAX, 0,
      tx.getThisHash(), txOut.getScript())));
   ustx.addRecipient(std::make_shared<Signing::Recipient_P2WPKH>(
      BinaryData(std::vector<uint8_t>(20, 2)), txOut.getAmount() / 2));
   ustx.addSupportingTx(rawTx);

   auto signerId = getNewSigner(bridge_);
   ASSERT_FALSE(signerId.empty());
   ASSERT_TRUE(loadSignerString(bridge_, signerId,
      ustx.toString(Signing::SignerStringFormat::TxSigCollect_Modern)));

   auto unpooled = getTxDataByHash(bridge_, {txHash}, success);
   ASSERT_TRUE(success);
   ASSERT_EQ(unpooled.size(), 1U);
   EXPECT_EQ(unpooled[txHash].raw, rawTx);
   EXPECT_EQ(unpooled[txHash].height, UINT32_MAX);
   EXPECT_EQ(unpooled[txHash].txIndex, UINT32_MAX);
   EXPECT_EQ(unpooled[txHash].timestamp, UINT32_MAX);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   ASSERT_EQ(goOnline(bridge_), 5);

   auto fromDb = getTxDataByHash(bridge_, {txHash}, success);
   ASSERT_TRUE(success);
   EXPECT_EQ(fromDb, fromWallet);
}

TEST_F(BridgeChainDataTests, GetTxsByHash_ZcThenMined)
{
   //the wallet bridge gets muted once a second bridge takes over
   auto walletSocket = std::make_shared<MutableTestBridgeSocket>();
   bridge_ = std::make_shared<Bridge::CppBridge>(walletSocket);
   loadWallets({walletId_BCDE_});
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //the wallet sends a zc to itself. In DB_BARE the db only resolves
   //hashes of mined txs that pay or spend from a registered address
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ BtcUtils::scrAddrToBase58(TestChain::scrAddrC), 11 * COIN }},
         TestChain::scrAddrD, 2, false,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());
   auto beforeZc = (uint32_t)time(0);
   broadcastTx(bridge_, signedTx);
   auto zcLedgers = waitOnZc();
   auto afterZc = (uint32_t)time(0);
   ASSERT_EQ(zcLedgers.size(), 1);
   auto zcHash = Tx(signedTx).getThisHash();
   ASSERT_EQ(zcLedgers[0].getTxHash(), zcHash);

   //keep the wallet bridge registered but quiet, a second bridge with no
   //wallet asks for the zc
   auto walletBridge = bridge_;
   {
      std::unique_lock<std::mutex> lock(commsMutex);
      walletSocket->muted = true;
      replyQueue.clear();
   }

   bridge_ = std::make_shared<Bridge::CppBridge>(
      std::make_shared<TestBridgeSocket>());
   loadWallets({});
   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   ASSERT_EQ(goOnline(bridge_), 5);

   bool success = false;
   auto asZc = getTxDataByHash(bridge_, {zcHash}, success);
   ASSERT_TRUE(success);
   ASSERT_EQ(asZc.size(), 1);
   EXPECT_EQ(asZc.begin()->first, zcHash);
   EXPECT_EQ(asZc.begin()->second.height, UINT32_MAX);
   EXPECT_EQ(asZc.begin()->second.txIndex, UINT32_MAX);
   //the time the db's mempool got the zc
   EXPECT_GE(asZc.begin()->second.timestamp, beforeZc);
   EXPECT_LE(asZc.begin()->second.timestamp, afterZc);

   //once mined, the cached zc copy is replaced
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 6);

   auto asMined = getTxDataByHash(bridge_, {zcHash}, success);
   ASSERT_TRUE(success);
   ASSERT_EQ(asMined.size(), 1);
   EXPECT_EQ(asMined.begin()->first, zcHash);
   EXPECT_EQ(asMined.begin()->second.height, 6U);

   //its block and index in it, as the db has them
   auto minedKey = iface_->getDBKeyForHash(zcHash);
   ASSERT_TRUE(Types::isTxKeyValid(minedKey));
   ASSERT_FALSE(Types::isThisAZCKey(minedKey));
   auto minedHeader = theBDMt_->bdm()->blockchain()->getHeaderById(
      Types::getBlockIDFromTxKey(minedKey));
   ASSERT_NE(minedHeader, nullptr);
   EXPECT_EQ(minedHeader->getBlockHeight(), 6U);
   EXPECT_NE(Types::getTxIndexFromTxKey(minedKey), 0U); //not the coinbase
   EXPECT_EQ(asMined.begin()->second.txIndex,
      Types::getTxIndexFromTxKey(minedKey));
   EXPECT_EQ(asMined.begin()->second.timestamp, minedHeader->getTimestamp());

   bridge_.reset();
   walletBridge.reset();
}

TEST_F(BridgeChainDataTests, GetTxsByHash_DoubleReorg)
{
   loadWallets({walletId_BCDE_});
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 4);

   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   //every wallet tx seen so far, on any branch
   std::set<BinaryData> hashes;
   auto addLedgerHashes = [&]() {
      for (const auto& ledger : getLedgersPage(bridge_, delegateId, 0)) {
         hashes.emplace(ledger.getTxHash());
      }
   };

   //what the db's main chain has of these txs, as getTxsByHash gives it
   auto onMainChain = [this, &hashes]() {
      std::map<BinaryData, FetchedTx> result;
      auto blockchain = theBDMt_->bdm()->blockchain();
      auto blockData = theBDMt_->bdm()->blockchainData();
      auto top = blockchain->top()->getBlockHeight();
      for (unsigned height = 0; height <= top; height++) {
         auto header = blockchain->getHeaderByHeight(height);
         for (unsigned i = 0; i < header->getNumTx(); i++) {
            auto tx = blockData->getTx(header->getUniqueID(), i);
            if (hashes.find(tx.getThisHash()) == hashes.end()) {
               continue;
            }
            result.emplace(tx.getThisHash(), FetchedTx{
               BinaryData(tx.getPtr(), tx.getSize()),
               height, i, header->getTimestamp()});
         }
      }
      return result;
   };

   //the bridge serves a tx where the db's main chain has it, and doesn't
   //serve one the main chain doesn't have
   auto checkAgainstDb = [&](const std::string& tag, size_t offMain) {
      auto expected = onMainChain();
      EXPECT_EQ(hashes.size() - expected.size(), offMain) << tag;

      bool success = false;
      auto served = getTxDataByHash(bridge_, hashes, success);
      EXPECT_TRUE(success) << tag;
      for (const auto& txHash : hashes) {
         auto hashStr = txHash.toHexStr(true);
         auto expIter = expected.find(txHash);
         auto iter = served.find(txHash);
         if (expIter == expected.end()) {
            EXPECT_TRUE(iter == served.end()) << tag << ": " << hashStr <<
               " is off the main chain, served at height " <<
               iter->second.height;
            continue;
         }
         if (iter == served.end()) {
            ADD_FAILURE() << tag << ": " << hashStr << " not served";
            continue;
         }
         EXPECT_EQ(iter->second.height, expIter->second.height) << tag << ": " << hashStr;
         EXPECT_EQ(iter->second.txIndex, expIter->second.txIndex) << tag << ": " << hashStr;
         EXPECT_EQ(iter->second.timestamp, expIter->second.timestamp) << tag << ": " << hashStr;
         EXPECT_EQ(iter->second.raw, expIter->second.raw) << tag << ": " << hashStr;
      }
   };

   //0-3, 4A
   addLedgerHashes();
   checkAgainstDb("on 4A", 0);

   //reorg onto 4, 5: the wallet's 4A txs are in 4 and 5 too
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);
   addLedgerHashes();
   checkAgainstDb("on 4-5", 0);

   //reorg back onto 4A, 5A: 5 of the txs of 4 and 5 are off the main chain
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);
   addLedgerHashes();
   checkAgainstDb("on 4A-5A", 5);
}

TEST_F(BridgeChainDataTests, GetTxsByHash_ZcDoubleReorg)
{
   loadWallets({walletId_BCDE_});
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat_);
   ASSERT_NO_FATAL_FAILURE(goOnlineAt(4));
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   /* test starts here */

   //on 4A: one zc spends an output of block 3 all branches keep unspent,
   //the other one an output of block 3 that block 4 spends
   std::set<BinaryData> hashes;
   hashes.emplace(spendFromBCDE(3, 2, 0));
   hashes.emplace(spendFromBCDE(3, 3, 0));
   checkZcsAgainstDb("on 4A", delegateId, hashes, 2);

   //reorg onto 4, 5: the second zc is out. A third one spends an output
   //of block 5, which 5A doesn't have
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);
   checkZcsAgainstDb("on 4-5", delegateId, hashes, 1);
   hashes.emplace(spendFromBCDE(5, 1, 0));
   checkZcsAgainstDb("on 4-5, 3rd zc", delegateId, hashes, 2);

   //reorg back onto 4A, 5A: the third zc is out, the first one stays
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);
   checkZcsAgainstDb("on 4A-5A", delegateId, hashes, 1);
}

TEST_F(BridgeChainDataTests, GetTxsByHash_ZcReconnect)
{
   loadWallets({walletId_BCDE_});
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   ASSERT_NO_FATAL_FAILURE(goOnlineAt(5));
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   //the bridge gets the mined ledgers anew from each db it connects to
   auto minedAt5 = minedLedgers(delegateId);
   ASSERT_FALSE(minedAt5.empty());

   /* test starts here */

   auto zcHash1 = broadcastZc(signFromBCDE({}, 11 * COIN), true);
   auto zcKey1 = dbZcKey(zcHash1);
   ASSERT_EQ(zcKey1, Types::constructZCKey(0));

   auto served = getTxsByHash({zcHash1});
   auto zcTime1 = served[zcHash1].timestamp;
   EXPECT_NE(zcTime1, UINT32_MAX);

   //the db comes back with the mempool it saved, the zc has the same key:
   //the wallet lists it, the bridge serves it as a zc
   ASSERT_NO_FATAL_FAILURE(restartDb(false));
   EXPECT_EQ(minedLedgers(delegateId), minedAt5);
   EXPECT_EQ(dbZcKey(zcHash1), zcKey1);
   EXPECT_EQ(zcLedgerHashes(delegateId),
      std::set<std::string>{zcHash1.toHexStr()});
   served = getTxsByHash({zcHash1});
   EXPECT_EQ(served[zcHash1].height, UINT32_MAX);
   EXPECT_EQ(served[zcHash1].timestamp, zcTime1);

   //it comes back with its mempool cleared: the wallet lists no zc, and the
   //bridge serves the zc's unpooled copy only, with no time
   ASSERT_NO_FATAL_FAILURE(restartDb(true));
   EXPECT_EQ(minedLedgers(delegateId), minedAt5);
   EXPECT_TRUE(zcLedgerHashes(delegateId).empty());
   served = getTxsByHash({zcHash1});
   EXPECT_EQ(served[zcHash1].height, UINT32_MAX);
   EXPECT_EQ(served[zcHash1].timestamp, UINT32_MAX);

   /* a new zc gets the first zc id again, the key the first zc had */
   auto zcHash2 = broadcastZc(signFromBCDE({}, 5 * COIN), true);
   ASSERT_NE(zcHash2.toHexStr(), zcHash1.toHexStr());
   EXPECT_EQ(dbZcKey(zcHash2), zcKey1);

   //the wallet's ledgers and the bridge show the new tx
   EXPECT_EQ(zcLedgerHashes(delegateId),
      std::set<std::string>{zcHash2.toHexStr()});
   served = getTxsByHash({zcHash1, zcHash2});
   EXPECT_EQ(served[zcHash1].timestamp, UINT32_MAX);
   EXPECT_EQ(served[zcHash2].height, UINT32_MAX);
   EXPECT_NE(served[zcHash2].timestamp, UINT32_MAX);
}

////////////////////////////////////////////////////////////////////////////////
// cache purge
TEST_F(BridgeChainDataTests, TxIOCachePurge_Recover)
{
   loadWallets({walletId_BCDE_});
   ASSERT_TRUE(checkHasImports(bridge_, walletId_BCDE_));

   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   auto clients = std::make_shared<Clients>(theBDMt_->bdm());
   startWSServer(clients);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 4);

   //wlt balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[4][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[4][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[4][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[4][3]);

   //addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 4, false);

   //BCDE ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   auto ledgersAt4Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt4Blocks.size(), 12);

   for (unsigned i = 0; i < ledgersAt4Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt4Blocks[i],
            TestChain::ledgersBCDE[i + 3]));
   }

   //start capturing the bdv's requests on the server side
   Types::BdvId bdvId;
   {
      auto bdvMap = clients->getBdvIds();
      ASSERT_EQ(bdvMap.size(), 1);
      bdvId = *bdvMap.begin();
   }

   DBTestUtils::BdvPayloadNinja ninja{clients, bdvId};
   ninja.beginPayloadCapture();

   /* add block 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();

   //expecting a call for getTxios
   {
      auto reader = ninja.getRequestReader();
      ASSERT_NE(reader.second, nullptr);
      auto request = reader.second->getRoot<Codec::BDV::Request>();
      ASSERT_EQ(request.which(), Codec::BDV::Request::Which::BDV);
      auto bdvRequest = request.getBdv();
      ASSERT_EQ(bdvRequest.which(), Codec::BDV::BdvRequest::Which::GET_TXIOS);

      //expecting the txio request to be from height 5
      ASSERT_EQ(bdvRequest.getGetTxios(), 5);

      //fail the getTxios request
      ninja.failMessage(reader.first);
   }

   //this will trigger a purge, that will again lead to a request for txios,
   //this time from height 0
   {
      auto readerAfterPurge = ninja.getRequestReader();
      ASSERT_NE(readerAfterPurge.second, nullptr);
      auto request2 = readerAfterPurge.second->getRoot<Codec::BDV::Request>();
      ASSERT_EQ(request2.which(), Codec::BDV::Request::Which::BDV);
      auto bdvRequest2 = request2.getBdv();
      ASSERT_EQ(bdvRequest2.which(), Codec::BDV::BdvRequest::Which::GET_TXIOS);

      //expecting the txio request to be from height 5
      ASSERT_EQ(bdvRequest2.getGetTxios(), 0);

      //we can now stop capturing requests
      ninja.stopPayloadCapture();

      //and allow the latest getTxios request through
      ninja.passMessageToBDM(readerAfterPurge.first);
   }

   ASSERT_EQ(waitOnNewBlock(), 5);

   //wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //ledgers
   auto ledgersAt5Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt5Blocks.size(), 15);

   for (unsigned i = 0; i < ledgersAt5Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt5Blocks[i],
         TestChain::ledgersBCDE[i])) << i;
   }
}

TEST_F(BridgeChainDataTests, TxIOCachePurge_DropBDV)
{
   loadWallets({walletId_BCDE_});
   ASSERT_TRUE(checkHasImports(bridge_, walletId_BCDE_));

   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   auto clients = std::make_shared<Clients>(theBDMt_->bdm());
   startWSServer(clients);

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 4);

   //wlt balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[4][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[4][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[4][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[4][3]);

   //addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 4, false);

   //BCDE ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   auto ledgersAt4Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt4Blocks.size(), 12);

   for (unsigned i = 0; i < ledgersAt4Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt4Blocks[i],
            TestChain::ledgersBCDE[i + 3]));
   }

   //start capturing the bdv's requests on the server side
   Types::BdvId bdvId;
   {
      auto bdvMap = clients->getBdvIds();
      ASSERT_EQ(bdvMap.size(), 1);
      bdvId = *bdvMap.begin();
   }

   DBTestUtils::BdvPayloadNinja ninja{clients, bdvId};
   ninja.beginPayloadCapture();

   /* add block 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();

   //expecting a call for getTxios
   {
      auto reader = ninja.getRequestReader();
      ASSERT_NE(reader.first, UINT32_MAX);
      ASSERT_NE(reader.second, nullptr);
      auto request = reader.second->getRoot<Codec::BDV::Request>();
      ASSERT_EQ(request.which(), Codec::BDV::Request::Which::BDV);
      auto bdvRequest = request.getBdv();
      ASSERT_EQ(bdvRequest.which(), Codec::BDV::BdvRequest::Which::GET_TXIOS);

      //expecting the txio request to be from height 5
      ASSERT_EQ(bdvRequest.getGetTxios(), 5);

      //fail the getTxios request
      ninja.failMessage(reader.first);
   }

   //this will trigger a purge, that will again lead to a request for txios,
   //this time from height 0, twice
   unsigned count = 0;
   while (count++ < 2) {
      auto readerAfterPurge = ninja.getRequestReader();
      ASSERT_NE(readerAfterPurge.second, nullptr);
      auto request2 = readerAfterPurge.second->getRoot<Codec::BDV::Request>();
      ASSERT_EQ(request2.which(), Codec::BDV::Request::Which::BDV);
      auto bdvRequest2 = request2.getBdv();
      ASSERT_EQ(bdvRequest2.which(), Codec::BDV::BdvRequest::Which::GET_TXIOS);

      //expecting the txio request to be from height 5
      ASSERT_EQ(bdvRequest2.getGetTxios(), 0);

      //fail the getTxios request
      ninja.failMessage(readerAfterPurge.first);
   }
   ninja.stopPayloadCapture();

   //the bridge will drop the bdv connection in consequence
   ASSERT_NO_FATAL_FAILURE(waitOnDisconnected());

   /* check the cache was purged in the process */

   //wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_TRUE(wltBal.empty());

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_TRUE(balances.empty());

   //ledgers
   auto ledgersAt5Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_TRUE(ledgersAt5Blocks.empty());

   /*
   the wallet containers dropped their chain data with the bdv: the requests
   served offline don't run into the txs the purge dropped
   */

   //a ledger page is empty rather than an error
   auto pageStatus = getLedgerPageStatus(delegateId, 0);
   EXPECT_TRUE(pageStatus.first) << pageStatus.second;

   //a coin selection set up now has nothing to spend, and the fee to spend
   //it all doesn't ask the purged cache for utxos
   auto csId = getNewCoinSelector(bridge_, walletId_BCDE_, accountId_BCDE_, 5);
   ASSERT_FALSE(csId.empty());
   std::pair<bool, std::string> feeStatus;
   ASSERT_NO_THROW(feeStatus = getFeeForMaxValStatus(csId, 1.0f));
   EXPECT_TRUE(feeStatus.first) << feeStatus.second;
}

namespace
{
   //drops its db connection when it takes its lock for the nth time
   class DbDroppingTxIOCache : public Bridge::TxIOCache
   {
   private:
      std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr_;
      const unsigned dropAt_;
      unsigned lockCount_ = 0;

      void initAfterLock(void) override
      {
         if (++lockCount_ == dropAt_) {
            bdvPtr_->unregisterFromDB();
         }
      }

   public:
      DbDroppingTxIOCache(
         std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr, unsigned at) :
         bdvPtr_(bdvPtr), dropAt_(at)
      {}

      unsigned lockCount(void) const { return lockCount_; }
   };
}

TEST_F(BridgeChainDataTests, FetchTxs_NoHeaderAskAgain)
{
   loadWallets({walletId_BCDE_});
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());
   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   std::set<BinaryData> hashes;
   std::map<BinaryData, uint32_t> heights;
   for (const auto& ledger : ledgers) {
      hashes.emplace(ledger.getTxHash());
      heights.emplace(ledger.getTxHash(), ledger.getBlockNum());
   }
   auto bdvPtr = bridge_->bdvPtr();
   ASSERT_NE(bdvPtr, nullptr);

   //every hash has a mined copy with its header in the cache
   auto checkMined = [&](const Bridge::TxIOCache& txioCache) {
      auto dbCache = txioCache.getDBCache();
      for (const auto& txHash : hashes) {
         try {
            auto txKey = txioCache.getTxByHash(txHash).getDBKey();
            ASSERT_FALSE(Types::isThisAZCKey(txKey));
            auto header = dbCache->getHeader(
               Types::getBlockIDFromTxKey(txKey));
            EXPECT_EQ(header->blockHeight, heights[txHash]);
         } catch (const std::out_of_range& e) {
            ADD_FAILURE() << e.what();
         }
      }
   };

   //an empty cache gets the mined txs and their headers from the db
   {
      Bridge::TxIOCache txioCache;
      txioCache.fetchTxs(bdvPtr, hashes);
      checkMined(txioCache);
   }

   //the db goes away once it has served the txs, before the headers are
   //asked for: fetchTxs takes the lock to see what the cache misses (1),
   //then to see which headers it misses (2), then to fill the cache (3)
   DbDroppingTxIOCache txioCache(bdvPtr, 2);
   txioCache.fetchTxs(bdvPtr, hashes);
   EXPECT_FALSE(bdvPtr->isValid());
   EXPECT_EQ(txioCache.lockCount(), 3U);

   //the bridge lost its db along: wait on its disconnected notif, the
   //thread that sends it must be done before the bridge goes away
   while (true) {
      auto reply = waitOnReply();
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(reply->data.getPtr()),
         reply->data.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
      if (fromBridge.which() == Codec::Bridge::FromBridge::NOTIFICATION &&
         fromBridge.getNotification().which() ==
            Codec::Bridge::Notification::DISCONNECTED) {
         break;
      }
   }

   //a mined copy with no header is no valid copy
   for (const auto& txHash : hashes) {
      EXPECT_THROW(txioCache.getTxByHash(txHash), std::out_of_range);
   }

   //so the next fetch asks the db for these hashes again
   bridge_ = std::make_shared<Bridge::CppBridge>(
      std::make_shared<TestBridgeSocket>());
   loadWallets({});
   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   ASSERT_EQ(goOnline(bridge_), 5);
   bdvPtr = bridge_->bdvPtr();
   ASSERT_NE(bdvPtr, nullptr);

   txioCache.fetchTxs(bdvPtr, hashes);
   checkMined(txioCache);
}

TEST_F(BridgeChainDataTests, ChangeFilters_ALFB_BCDE)
{
   loadWallets({walletId_BCDE_, walletId_AFLB_});

   //changing filters while offline shouldn't blow up the manager
   ASSERT_TRUE(updateWalletFilters({
      {walletId_AFLB_, {accountId_AFLB_}}}));

   //revert fitler back to all wallets
   ASSERT_TRUE(updateWalletFilters({
      {walletId_AFLB_, {accountId_AFLB_}},
      {walletId_BCDE_, {accountId_BCDE_}},
   }));

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //check main ledger
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 26);

   std::vector<TestChain::LedgerEntryValue> combinedLedgers;
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE.begin(), TestChain::ledgersBCDE.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB.begin(), TestChain::ledgersAFLB.end());
   ASSERT_EQ(combinedLedgers.size(), 26);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgers[i],
            combinedLedgers[i])) << i;
   }

   //remove AFLB from filter
   ASSERT_TRUE(updateWalletFilters(
      {{walletId_BCDE_, {accountId_BCDE_}}}));

   //check main ledger again
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }
}

TEST_F(BridgeChainDataTests, BlocksOutOfOrder_BCDE)
{
   /*
   This implements the same test conditions as
   BlockDir.HeadersFirstForwardUpdate
   */

   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "4", "5", "4A" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 2);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[2][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[2][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[2][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[2][3]);

   //check address balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 1);
   checkBalances(balances, 2, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 4);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i + 11])) << i;
   }

   /* add block 3 */
   TestUtils::appendBlocks({ "3" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check address balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }
}

////////////////////////////////////////////////////////////////////////////////
// ledgers add block
TEST_F(BridgeChainDataTests, AddBlocks_BCDE)
{
   loadWallets({walletId_BCDE_});

   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   //connect to db
   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 3);

   /* block 3 */

   //wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[3][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[3][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[3][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[3][3]);

   //addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 3, false);

   //ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt3Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt3Blocks.size(), 9);

   for (unsigned i = 0; i < ledgersAt3Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt3Blocks[i],
         TestChain::ledgersBCDE[i + 6]));
   }

   /* block 4 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 4);

   //wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[4][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[4][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[4][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[4][3]);

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 4, false);

   //ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt4Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt4Blocks.size(), 12);

   for (unsigned i = 0; i < ledgersAt4Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt4Blocks[i],
         TestChain::ledgersBCDE[i + 3]));
   }

   /* block 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt5Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt5Blocks.size(), 15);

   for (unsigned i = 0; i < ledgersAt5Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt5Blocks[i],
         TestChain::ledgersBCDE[i])) << i;
   }
}

TEST_F(BridgeChainDataTests, AddBlocks_BC_DE)
{
   // load the 2 wallets
   loadWallets({walletId_BC_, walletId_DE_});

   //connect to db
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 3);

   /* block 3 */

   //balances
   auto balances1 = getAddrBalances(bridge_, walletId_BC_, accountId_BC_);
   ASSERT_EQ(balances1.size(), 2);
   checkBalances(balances1, 3, false);
   auto balances2 = getAddrBalances(bridge_, walletId_DE_, accountId_DE_);
   ASSERT_EQ(balances2.size(), 2);
   checkBalances(balances2, 3, false);

   //ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   auto delegateIdWlt1 = getLedgerDelegateIdForWallet(
      bridge_, walletId_BC_, accountId_BC_);
   auto delegateIdWlt2 = getLedgerDelegateIdForWallet(
      bridge_, walletId_DE_, accountId_DE_);
   ASSERT_FALSE(delegateIdWlt1.empty());
   ASSERT_FALSE(delegateIdWlt2.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt2);
   EXPECT_EQ(pageCount, 1);

   //wlt1 ledgers
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBC[i + 3]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersDE[i + 3]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 10);

   std::vector<TestChain::LedgerEntryValue> combinedLedgers;
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBC.begin() + 3, TestChain::ledgersBC.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersDE.begin() + 3, TestChain::ledgersDE.end());
   ASSERT_EQ(combinedLedgers.size(), 10);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
         combinedLedgers[i])) << i;
   }

   /* block 4 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 4);

   //balances
   balances1 = getAddrBalances(bridge_, walletId_BC_, accountId_BC_);
   ASSERT_EQ(balances1.size(), 2);
   checkBalances(balances1, 4, false);
   balances2 = getAddrBalances(bridge_, walletId_DE_, accountId_DE_);
   ASSERT_EQ(balances2.size(), 2);
   checkBalances(balances2, 4, false);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBC[i + 2]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersDE[i + 1]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 13);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBC.begin() + 2, TestChain::ledgersBC.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersDE.begin() + 1, TestChain::ledgersDE.end());
   ASSERT_EQ(combinedLedgers.size(), 13);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* block 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //balances
   balances1 = getAddrBalances(bridge_, walletId_BC_, accountId_BC_);
   ASSERT_EQ(balances1.size(), 2);
   checkBalances(balances1, 5, false);
   balances2 = getAddrBalances(bridge_, walletId_DE_, accountId_DE_);
   ASSERT_EQ(balances2.size(), 2);
   checkBalances(balances2, 5, false);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 10);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBC[i]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 6);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersDE[i]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 16);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBC.begin(), TestChain::ledgersBC.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersDE.begin(), TestChain::ledgersDE.end());
   ASSERT_EQ(combinedLedgers.size(), 16);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }
}

TEST_F(BridgeChainDataTests, AddBlocks_BCDE_AFLB)
{
   // load the 2 wallets
   loadWallets({walletId_BCDE_, walletId_AFLB_});

   //connect to db
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 3);

   /* block 3 */

   //wlt balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[3][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[3][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[3][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[3][3]);

   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB[3][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB[3][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB[3][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB[3][3]);

   //address balances
   auto balances1 = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances1.size(), 4);
   checkBalances(balances1, 3, false);
   auto balances2 = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances2.size(), 6);
   checkBalances(balances2, 3, false);

   //setup wallet ledger delegates
   auto delegateId = getLedgerDelegateId(bridge_);
   auto delegateIdWlt1 = getLedgerDelegateIdForWallet(
      bridge_, walletId_BCDE_, accountId_BCDE_);
   auto delegateIdWlt2 = getLedgerDelegateIdForWallet(
      bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_FALSE(delegateIdWlt1.empty());
   ASSERT_FALSE(delegateIdWlt2.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt2);
   EXPECT_EQ(pageCount, 1);

   //setup address ledger delegates
   auto delegateScrAddrA = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::scrAddrA);
   ASSERT_FALSE(delegateScrAddrA.empty());
   auto delegateScrAddrB = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrB);
   ASSERT_FALSE(delegateScrAddrB.empty());
   auto delegateScrAddrC = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrC);
   ASSERT_FALSE(delegateScrAddrC.empty());
   auto delegateScrAddrD = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrD);
   ASSERT_FALSE(delegateScrAddrD.empty());
   auto delegateScrAddrE = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrE);
   ASSERT_FALSE(delegateScrAddrE.empty());
   auto delegateScrAddrF = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::scrAddrF);
   ASSERT_FALSE(delegateScrAddrF.empty());
   auto delegateScrAddrLB1 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb1ScrAddr);
   ASSERT_FALSE(delegateScrAddrLB1.empty());
   auto delegateP2SHLB1 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb1ScrAddrP2SH);
   ASSERT_FALSE(delegateP2SHLB1.empty());
   auto delegateScrAddrLB2 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb2ScrAddr);
   ASSERT_FALSE(delegateScrAddrLB2.empty());
   auto delegateP2SHLB2 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb2ScrAddrP2SH);
   ASSERT_FALSE(delegateP2SHLB2.empty());

   pageCount = getLedgersPageCount(bridge_, delegateScrAddrA);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrB);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrC);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrD);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrE);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrF);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrLB1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateP2SHLB1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrLB2);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateP2SHLB2);
   EXPECT_EQ(pageCount, 1);

   //wlt1 ledgers
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 9);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBCDE[i + 6]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersAFLB[i + 4]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 16);

   std::vector<TestChain::LedgerEntryValue> combinedLedgers;
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE.begin() + 6, TestChain::ledgersBCDE.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB.begin() + 4, TestChain::ledgersAFLB.end());
   ASSERT_EQ(combinedLedgers.size(), 16);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
         combinedLedgers[i])) << i;
   }

   /* address ledgers */

   //A
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrA, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersA[i]));
   }

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i + 2]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i + 2]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i + 3]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //F
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrF, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersF[i + 2]));
   }

   //LB1
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1[i + 1]));
   }

   //LB1_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_P2SH[i + 1]));
   }

   //LB2
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2[i + 1])) << i;
   }

   //LB2_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_P2SH[i + 1]));
   }

   /* block 4 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 4);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[4][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[4][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[4][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[4][3]);

   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB[4][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB[4][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB[4][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB[4][3]);

   //addr balances
   balances1 = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances1.size(), 4);
   checkBalances(balances1, 4, false);
   balances2 = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances2.size(), 6);
   checkBalances(balances2, 4, false);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 12);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBCDE[i + 3]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 10);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersAFLB[i + 1]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 22);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE.begin() + 3, TestChain::ledgersBCDE.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB.begin() + 1, TestChain::ledgersAFLB.end());
   ASSERT_EQ(combinedLedgers.size(), 22);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* address ledgers */

   //A
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrA, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersA[i]));
   }

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i + 2]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i + 1]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i + 1]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //F
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrF, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersF[i + 1]));
   }

   //LB1
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1[i]));
   }

   //LB1_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_P2SH[i]));
   }

   //LB2
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2[i])) << i;
   }

   //LB2_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_P2SH[i]));
   }

   /* block 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB[5][3]);

   //addr balances
   balances1 = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances1.size(), 4);
   checkBalances(balances1, 5, false);
   balances2 = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances2.size(), 6);
   checkBalances(balances2, 5, false);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 15);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBCDE[i]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 11);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersAFLB[i]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 26);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE.begin(), TestChain::ledgersBCDE.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB.begin(), TestChain::ledgersAFLB.end());
   ASSERT_EQ(combinedLedgers.size(), 26);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* address ledgers */

   //A
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrA, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersA[i]));
   }

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //F
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrF, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersF[i]));
   }

   //LB1
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1[i]));
   }

   //LB1_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_P2SH[i]));
   }

   //LB2
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2[i]));
   }

   //LB2_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_P2SH[i]));
   }
}

////////////////////////////////////////////////////////////////////////////////
// ledgers reorg
TEST_F(BridgeChainDataTests, Reorg_BCDE)
{
   loadWallets({walletId_BCDE_});

   //connect to db
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 3);

   /* block 3 */

   //wlt balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[3][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[3][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[3][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[3][3]);

   //addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 3, false);

   //ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt3Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt3Blocks.size(), 9);

   for (unsigned i = 0; i < ledgersAt3Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt3Blocks[i],
         TestChain::ledgersBCDE[i + 6]));
   }

   /* block 4A */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 4);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE_Reorg[4][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE_Reorg[4][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE_Reorg[4][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE_Reorg[4][3]);

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 4, true);

   //ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt4Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt4Blocks.size(), 9);

   for (unsigned i = 0; i < ledgersAt4Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt4Blocks[i],
         TestChain::ledgersBCDE_Reorg[i + 2]));
   }

   /* block 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt5Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt5Blocks.size(), 15);

   for (unsigned i = 0; i < ledgersAt5Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt5Blocks[i],
         TestChain::ledgersBCDE[i]));
   }

   /* block 5A */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE_Reorg[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE_Reorg[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE_Reorg[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE_Reorg[5][3]);

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, true);

   //ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   ledgersAt5Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt5Blocks.size(), 11);

   for (unsigned i = 0; i < ledgersAt5Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt5Blocks[i],
         TestChain::ledgersBCDE_Reorg[i]));
   }
}

TEST_F(BridgeChainDataTests, Reorg_BCDE_DifferentOrder)
{
   loadWallets({walletId_BCDE_});

   //connect to db
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 3);

   /* block 3 */

   //wlt balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[3][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[3][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[3][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[3][3]);

   //addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 3, false);

   //ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt3Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt3Blocks.size(), 9);

   for (unsigned i = 0; i < ledgersAt3Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt3Blocks[i],
         TestChain::ledgersBCDE[i + 6]));
   }

   /* block 4, 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt5Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt5Blocks.size(), 15);

   for (unsigned i = 0; i < ledgersAt5Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt5Blocks[i],
         TestChain::ledgersBCDE[i]));
   }

   /* block 4A, 5A */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5", "4A", "5A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE_Reorg[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE_Reorg[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE_Reorg[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE_Reorg[5][3]);

   //addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, true);

   //ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   ledgersAt5Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt5Blocks.size(), 11);

   for (unsigned i = 0; i < ledgersAt5Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt5Blocks[i],
         TestChain::ledgersBCDE_Reorg[i]));
   }
}

TEST_F(BridgeChainDataTests, Reorg_BC_DE)
{
   loadWallets({walletId_BC_, walletId_DE_});

   //connect to db
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 3);

   /* block 3 */

   //balances
   auto balances1 = getAddrBalances(bridge_, walletId_BC_, accountId_BC_);
   ASSERT_EQ(balances1.size(), 2);
   checkBalances(balances1, 3, false);
   auto balances2 = getAddrBalances(bridge_, walletId_DE_, accountId_DE_);
   ASSERT_EQ(balances2.size(), 2);
   checkBalances(balances2, 3, false);

   //ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   auto delegateIdWlt1 = getLedgerDelegateIdForWallet(
      bridge_, walletId_BC_, accountId_BC_);
   auto delegateIdWlt2 = getLedgerDelegateIdForWallet(
      bridge_, walletId_DE_, accountId_DE_);
   ASSERT_FALSE(delegateIdWlt1.empty());
   ASSERT_FALSE(delegateIdWlt2.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt2);
   EXPECT_EQ(pageCount, 1);

   //wlt1 ledgers
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBC[i + 3]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersDE[i + 3]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 10);

   std::vector<TestChain::LedgerEntryValue> combinedLedgers;
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBC.begin() + 3, TestChain::ledgersBC.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersDE.begin() + 3, TestChain::ledgersDE.end());
   ASSERT_EQ(combinedLedgers.size(), 10);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
         combinedLedgers[i])) << i;
   }

   /* block 4A */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 4);

   //balances
   balances1 = getAddrBalances(bridge_, walletId_BC_, accountId_BC_);
   ASSERT_EQ(balances1.size(), 2);
   checkBalances(balances1, 4, true);
   balances2 = getAddrBalances(bridge_, walletId_DE_, accountId_DE_);
   ASSERT_EQ(balances2.size(), 2);
   checkBalances(balances2, 4, true);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBC_Reorg[i]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersDE_Reorg[i + 2]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 10);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBC_Reorg.begin(), TestChain::ledgersBC_Reorg.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersDE_Reorg.begin() + 2, TestChain::ledgersDE_Reorg.end());
   ASSERT_EQ(combinedLedgers.size(), 10);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* block 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //balances
   balances1 = getAddrBalances(bridge_, walletId_BC_, accountId_BC_);
   ASSERT_EQ(balances1.size(), 2);
   checkBalances(balances1, 5, false);
   balances2 = getAddrBalances(bridge_, walletId_DE_, accountId_DE_);
   ASSERT_EQ(balances2.size(), 2);
   checkBalances(balances2, 5, false);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 10);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBC[i]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 6);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersDE[i]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 16);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBC.begin(), TestChain::ledgersBC.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersDE.begin(), TestChain::ledgersDE.end());
   ASSERT_EQ(combinedLedgers.size(), 16);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* block 5A */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //balances
   balances1 = getAddrBalances(bridge_, walletId_BC_, accountId_BC_);
   ASSERT_EQ(balances1.size(), 2);
   checkBalances(balances1, 5, true);
   balances2 = getAddrBalances(bridge_, walletId_DE_, accountId_DE_);
   ASSERT_EQ(balances2.size(), 2);
   checkBalances(balances2, 5, true);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBC_Reorg[i]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersDE_Reorg[i])) << i;
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 12);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBC_Reorg.begin(), TestChain::ledgersBC_Reorg.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersDE_Reorg.begin(), TestChain::ledgersDE_Reorg.end());
   ASSERT_EQ(combinedLedgers.size(), 12);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }
}

TEST_F(BridgeChainDataTests, Reorg_BCDE_AFLB)
{
   loadWallets({walletId_BCDE_, walletId_AFLB_});

   //connect to db
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 3);

   /* block 3 */

   //wlt balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[3][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[3][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[3][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[3][3]);

   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB[3][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB[3][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB[3][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB[3][3]);

   //addr balances
   auto balances1 = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances1.size(), 4);
   checkBalances(balances1, 3, false);
   auto balances2 = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances2.size(), 6);
   checkBalances(balances2, 3, false);

   //setup wallet ledger delegates
   auto delegateId = getLedgerDelegateId(bridge_);
   auto delegateIdWlt1 = getLedgerDelegateIdForWallet(
      bridge_, walletId_BCDE_, accountId_BCDE_);
   auto delegateIdWlt2 = getLedgerDelegateIdForWallet(
      bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_FALSE(delegateIdWlt1.empty());
   ASSERT_FALSE(delegateIdWlt2.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateIdWlt2);
   EXPECT_EQ(pageCount, 1);

   //setup address ledger delegates
   auto delegateScrAddrA = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::scrAddrA);
   ASSERT_FALSE(delegateScrAddrA.empty());
   auto delegateScrAddrB = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrB);
   ASSERT_FALSE(delegateScrAddrB.empty());
   auto delegateScrAddrC = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrC);
   ASSERT_FALSE(delegateScrAddrC.empty());
   auto delegateScrAddrD = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrD);
   ASSERT_FALSE(delegateScrAddrD.empty());
   auto delegateScrAddrE = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrE);
   ASSERT_FALSE(delegateScrAddrE.empty());
   auto delegateScrAddrF = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::scrAddrF);
   ASSERT_FALSE(delegateScrAddrF.empty());
   auto delegateScrAddrLB1 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb1ScrAddr);
   ASSERT_FALSE(delegateScrAddrLB1.empty());
   auto delegateP2SHLB1 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb1ScrAddrP2SH);
   ASSERT_FALSE(delegateP2SHLB1.empty());
   auto delegateScrAddrLB2 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb2ScrAddr);
   ASSERT_FALSE(delegateScrAddrLB2.empty());
   auto delegateP2SHLB2 = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_AFLB_, accountId_AFLB_, TestChain::lb2ScrAddrP2SH);
   ASSERT_FALSE(delegateP2SHLB2.empty());

   pageCount = getLedgersPageCount(bridge_, delegateScrAddrA);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrB);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrC);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrD);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrE);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrF);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrLB1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateP2SHLB1);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrLB2);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateP2SHLB2);
   EXPECT_EQ(pageCount, 1);

   //wlt1 ledgers
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 9);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBCDE[i + 6]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersAFLB[i + 4]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 16);

   std::vector<TestChain::LedgerEntryValue> combinedLedgers;
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE.begin() + 6, TestChain::ledgersBCDE.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB.begin() + 4, TestChain::ledgersAFLB.end());
   ASSERT_EQ(combinedLedgers.size(), 16);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
         combinedLedgers[i])) << i;
   }

   /* address ledgers */

   //A
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrA, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersA[i]));
   }

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i + 2]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i + 2]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i + 3]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //F
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrF, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersF[i + 2]));
   }

   //LB1
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1[i + 1]));
   }

   //LB1_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_P2SH[i + 1]));
   }

   //LB2
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2[i + 1])) << i;
   }

   //LB2_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_P2SH[i + 1]));
   }

   /* block 4A */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 4);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE_Reorg[4][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE_Reorg[4][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE_Reorg[4][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE_Reorg[4][3]);

   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB_Reorg[4][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB_Reorg[4][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB_Reorg[4][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB_Reorg[4][3]);

   //addr balances
   balances1 = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances1.size(), 4);
   checkBalances(balances1, 4, true);
   balances2 = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances2.size(), 6);
   checkBalances(balances2, 4, true);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 9);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBCDE_Reorg[i + 2]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersAFLB_Reorg[i + 2]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 17);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE_Reorg.begin() + 2, TestChain::ledgersBCDE_Reorg.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB_Reorg.begin() + 2, TestChain::ledgersAFLB_Reorg.end());
   ASSERT_EQ(combinedLedgers.size(), 17);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* address ledgers */

   //A
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrA, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersA_Reorg[i]));
   }

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB_Reorg[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC_Reorg[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD_Reorg[i + 2]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE_Reorg[i]));
   }

   //F
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrF, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersF_Reorg[i + 1]));
   }

   //LB1
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_Reorg[i + 1]));
   }

   //LB1_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_P2SH_Reorg[i]));
   }

   //LB2
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_Reorg[i])) << i;
   }

   //LB2_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_P2SH_Reorg[i + 1]));
   }

   /* block 5 */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB[5][3]);

   //balances
   balances1 = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances1.size(), 4);
   checkBalances(balances1, 5, false);
   balances2 = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances2.size(), 6);
   checkBalances(balances2, 5, false);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 15);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBCDE[i]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 11);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersAFLB[i]));
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 26);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE.begin(), TestChain::ledgersBCDE.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB.begin(), TestChain::ledgersAFLB.end());
   ASSERT_EQ(combinedLedgers.size(), 26);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* address ledgers */

   //A
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrA, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersA[i]));
   }

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //F
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrF, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersF[i]));
   }

   //LB1
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1[i]));
   }

   //LB1_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_P2SH[i]));
   }

   //LB2
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2[i]));
   }

   //LB2_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_P2SH[i]));
   }

   /* block 5A */
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //wlt balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE_Reorg[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE_Reorg[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE_Reorg[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE_Reorg[5][3]);

   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB_Reorg[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB_Reorg[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB_Reorg[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB_Reorg[5][3]);

   //addr balances
   balances1 = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances1.size(), 4);
   checkBalances(balances1, 5, true);
   balances2 = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances2.size(), 6);
   checkBalances(balances2, 5, true);

   //wlt1 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 11);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersBCDE_Reorg[i]));
   }

   //wlt2 ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateIdWlt2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 10);

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersAFLB_Reorg[i])) << i;
   }

   //combined ledgers
   ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 21);

   combinedLedgers.clear();
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE_Reorg.begin(), TestChain::ledgersBCDE_Reorg.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB_Reorg.begin(), TestChain::ledgersAFLB_Reorg.end());
   ASSERT_EQ(combinedLedgers.size(), 21);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            combinedLedgers[i])) << i;
   }

   /* address ledgers */

   //A
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrA, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 1);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersA_Reorg[i]));
   }

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB_Reorg[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC_Reorg[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD_Reorg[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE_Reorg[i]));
   }

   //F
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrF, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersF_Reorg[i]));
   }

   //LB1
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_Reorg[i]));
   }

   //LB1_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB1, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB1_P2SH_Reorg[i]));
   }

   //LB2
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_Reorg[i])) << i;
   }

   //LB2_P2SH
   ledgersAtBlocks = getLedgersPage(bridge_, delegateP2SHLB2, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersLB2_P2SH_Reorg[i]));
   }
}

TEST_F(BridgeChainDataTests, Reorg_SpendBeforeBranchPoint)
{
   /*
   Amazingly enough, our test chain does not provide a case of a reorg
   where an output that lives before the branch point is spent across
   both branches. This test covers that.
   */

   /* start with classic 6 blocks */
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check address balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   /* create 2 txs that spends the same output */
   auto utxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 0);

   //look for output 4|2:0
   UTXO theUtxo;
   for (const auto& utxo : utxos) {
      if (utxo.txHash == TestChain::hash42) {
         theUtxo = utxo;
         break;
      }
   }
   ASSERT_TRUE(theUtxo.isInitialized());

   //txA
   auto recipientPrivKeyA = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKeyA = Cryptography::ECDSA::computePublicKey(
      recipientPrivKeyA, true);
   auto hash160A =  BtcUtils::getHash160(recipientPubKeyA);
   auto recipientAddrA = BtcUtils::scrAddrToSegWitAddress(hash160A);

   auto txA = createAndSignTx(
      bridge_, walletId_BCDE_, accountId_BCDE_,
      { theUtxo }, {{ recipientAddrA, 3 * COIN }},
      TestChain::scrAddrB, 2, false,
      "privPass1"
   );
   ASSERT_FALSE(txA.empty());
   Tx txAObj{txA};

   //txB
   auto recipientPrivKeyB = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKeyB = Cryptography::ECDSA::computePublicKey(
      recipientPrivKeyB, true);
   auto hash160B =  BtcUtils::getHash160(recipientPubKeyB);
   auto recipientAddrB = BtcUtils::scrAddrToSegWitAddress(hash160B);

   auto txB = createAndSignTx(
      bridge_, walletId_BCDE_, accountId_BCDE_,
      { theUtxo }, {{ recipientAddrB, 4 * COIN }},
      TestChain::scrAddrB, 2, false,
      "privPass1"
   );
   ASSERT_FALSE(txB.empty());
   Tx txBObj{txB};
   ASSERT_NE(txAObj.getThisHash(), txBObj.getThisHash());

   //broadcast txA
   broadcastTx(bridge_, txA);
   waitOnZc();

   /* validate effect of zc txA */

   //check wallet balance
   Types::Amount txAAmount = 3UL * COIN + 2UL;

   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - txAAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - 5 * COIN);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - txAAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check address balances
   try {
      auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
      auto scrAddrDBal = balances.at(TestChain::scrAddrD);
      auto testAddrDBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrD);
      EXPECT_EQ(scrAddrDBal[0], testAddrDBal[0] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[1], testAddrDBal[1] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[2], testAddrDBal[2] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[3], testAddrDBal[3] + 1);
   } catch (const std::out_of_range&) {
      ASSERT_TRUE(false);
   }

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);

   auto ledger0 = ledgers[0];
   ASSERT_EQ(ledger0.getTxHash(), txAObj.getThisHash());
   ASSERT_EQ(ledger0.getBlockNum(), UINT32_MAX);
   EXPECT_EQ(ledger0.getValue(), Types::Value(txAAmount) * -1L);

   for (unsigned i = 0; i < TestChain::ledgersBCDE.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i + 1], TestChain::ledgersBCDE[i])) << i;
   }

   //mine txA
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 6);

   /* validate effect of mined txA */

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - txAAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - txAAmount);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - txAAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check address balances
   try {
      auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
      auto scrAddrDBal = balances.at(TestChain::scrAddrD);
      auto testAddrDBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrD);
      EXPECT_EQ(scrAddrDBal[0], testAddrDBal[0] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[1], testAddrDBal[1] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[2], testAddrDBal[2] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[3], testAddrDBal[3] + 1);
   } catch (const std::out_of_range&) {
      ASSERT_TRUE(false);
   }

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);

   ledger0 = ledgers[0];
   ASSERT_EQ(ledger0.getTxHash(), txAObj.getThisHash());
   ASSERT_EQ(ledger0.getBlockNum(), 6);
   EXPECT_EQ(ledger0.getValue(), Types::Value(txAAmount) * -1L);

   for (unsigned i = 0; i < TestChain::ledgersBCDE.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i + 1], TestChain::ledgersBCDE[i])) << i;
   }

   /* reorg from block 5 */

   auto branchPoint = theBDMt_->bdm()->blockchain()->getHeaderByHeight(5);
   auto topA = theBDMt_->bdm()->blockchain()->top();
   ASSERT_EQ(branchPoint->getUniqueID(), 6);
   nodePtr_->setReorgBranchPoint(branchPoint);
   nodePtr_->mineNewBlock(1, TestChain::addrA, 100.0);
   ASSERT_EQ(waitOnNewBlock(), 6);

   auto topB = theBDMt_->bdm()->blockchain()->top();
   ASSERT_NE(topA->getThisHash(), topB->getThisHash());
   ASSERT_EQ(topA->getPrevHash(), branchPoint->getThisHash());
   ASSERT_EQ(topB->getPrevHash(), branchPoint->getThisHash());
   ASSERT_FALSE(topA->isMainBranch());

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   //broadcast txB
   Types::Amount txBAmount = 4UL * COIN + 2UL;
   broadcastTx(bridge_, txB);
   auto zcLedgers = waitOnZc();
   ASSERT_EQ(zcLedgers.size(), 1);
   ASSERT_EQ(zcLedgers[0].getTxHash(), txBObj.getThisHash());
   ASSERT_EQ(zcLedgers[0].getValue(), Types::Value(txBAmount) * -1L);

   /* validate effect of zc txB */

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - txBAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - 5 * COIN);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - txBAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check address balances
   try {
      auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
      auto scrAddrDBal = balances.at(TestChain::scrAddrD);
      auto testAddrDBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrD);
      EXPECT_EQ(scrAddrDBal[0], testAddrDBal[0] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[1], testAddrDBal[1] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[2], testAddrDBal[2] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[3], testAddrDBal[3] + 1);
   } catch (const std::out_of_range&) {
      ASSERT_TRUE(false);
   }

   //ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);

   ledger0 = ledgers[0];
   ASSERT_EQ(ledger0.getTxHash(), txBObj.getThisHash());
   ASSERT_EQ(ledger0.getBlockNum(), UINT32_MAX);
   EXPECT_EQ(ledger0.getValue(), Types::Value(txBAmount) * -1L);

   for (unsigned i = 0; i < TestChain::ledgersBCDE.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i + 1], TestChain::ledgersBCDE[i])) << i;
   }

   //mine txB
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 7);

   /* validate effect of mined txB */

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - txBAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - txBAmount);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - txBAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check address balances
   try {
      auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
      auto scrAddrDBal = balances.at(TestChain::scrAddrD);
      auto testAddrDBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrD);
      EXPECT_EQ(scrAddrDBal[0], testAddrDBal[0] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[1], testAddrDBal[1] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[2], testAddrDBal[2] - 5UL * COIN);
      EXPECT_EQ(scrAddrDBal[3], testAddrDBal[3] + 1);
   } catch (const std::out_of_range&) {
      ASSERT_TRUE(false);
   }

   //ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);

   ledger0 = ledgers[0];
   ASSERT_EQ(ledger0.getTxHash(), txBObj.getThisHash());
   ASSERT_EQ(ledger0.getBlockNum(), 7);
   EXPECT_EQ(ledger0.getValue(), Types::Value(txBAmount) * -1L);

   for (unsigned i = 0; i < TestChain::ledgersBCDE.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i + 1], TestChain::ledgersBCDE[i])) << i;
   }
}

////////////////////////////////////////////////////////////////////////////////
// helper data
TEST_F(BridgeChainDataTests, AddressBook)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
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
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   //grab address book
   auto addrBook = getAddressBook(bridge_, walletId_BCDE_, accountId_BCDE_);

   try {
      auto hashes = addrBook.at(TestChain::scrAddrB);
      ASSERT_EQ(hashes.size(), TestChain::ledgersB.size());
      for (const auto& entry : TestChain::ledgersB) {
         ASSERT_NE(hashes.find(entry.txHash), hashes.end());
      }
   } catch (const std::range_error&) {
      ASSERT_TRUE(false);
   }

   try {
      auto hashes = addrBook.at(TestChain::scrAddrC);
      ASSERT_EQ(hashes.size(), TestChain::ledgersC.size());
      for (const auto& entry : TestChain::ledgersC) {
         ASSERT_NE(hashes.find(entry.txHash), hashes.end());
      }
   } catch (const std::range_error&) {
      ASSERT_TRUE(false);
   }

   try {
      auto hashes = addrBook.at(TestChain::scrAddrD);
      ASSERT_EQ(hashes.size(), TestChain::ledgersD.size());
      for (const auto& entry : TestChain::ledgersD) {
         ASSERT_NE(hashes.find(entry.txHash), hashes.end());
      }
   } catch (const std::range_error&) {
      ASSERT_TRUE(false);
   }

   try {
      auto hashes = addrBook.at(TestChain::scrAddrE);
      ASSERT_EQ(hashes.size(), TestChain::ledgersE.size());
      for (const auto& entry : TestChain::ledgersE) {
         ASSERT_NE(hashes.find(entry.txHash), hashes.end());
      }
   } catch (const std::range_error&) {
      ASSERT_TRUE(false);
   }
}

TEST_F(BridgeChainDataTests, getUTXOs)
{
   loadWallets({walletId_BCDE_, walletId_AFLB_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //grab BCDE utxos
   auto utxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 0);
   ASSERT_EQ(utxos.size(), 8);

   //Block 4, tx 2, output 0, 5 COINS
   auto utxo = utxos[0];
   ASSERT_EQ(utxo.getHeight(), 4);
   ASSERT_EQ(utxo.getTxIndex(), 2);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 5 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash42);

   //Block 3, tx 4, output 0, 25 COINS
   utxo = utxos[1];
   ASSERT_EQ(utxo.getHeight(), 3);
   ASSERT_EQ(utxo.getTxIndex(), 4);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 25 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash34);

   //Block 3, tx 2, output 0, 5 COINS
   utxo = utxos[2];
   ASSERT_EQ(utxo.getHeight(), 3);
   ASSERT_EQ(utxo.getTxIndex(), 2);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 5 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash32);

   //Block 5, tx 2, output 0, 5 COINS
   utxo = utxos[3];
   ASSERT_EQ(utxo.getHeight(), 5);
   ASSERT_EQ(utxo.getTxIndex(), 2);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 5 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash52);

   //Block 4, tx 3, output 2, 10 COINS
   utxo = utxos[4];
   ASSERT_EQ(utxo.getHeight(), 4);
   ASSERT_EQ(utxo.getTxIndex(), 3);
   ASSERT_EQ(utxo.getTxOutIndex(), 2);
   ASSERT_EQ(utxo.getAmount(), 10 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash43);

   //Block 5, tx 1, output 0, 10 COINS
   utxo = utxos[5];
   ASSERT_EQ(utxo.getHeight(), 5);
   ASSERT_EQ(utxo.getTxIndex(), 1);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 10 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash51);

   //Block 5, tx 1, output 1, 20 COINS
   utxo = utxos[6];
   ASSERT_EQ(utxo.getHeight(), 5);
   ASSERT_EQ(utxo.getTxIndex(), 1);
   ASSERT_EQ(utxo.getTxOutIndex(), 1);
   ASSERT_EQ(utxo.getAmount(), 20 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash51);

   //Block 3, tx 1, output 0, 5 COINS
   utxo = utxos[7];
   ASSERT_EQ(utxo.getHeight(), 3);
   ASSERT_EQ(utxo.getTxIndex(), 1);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 5 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash31);

   //grab AFLB utxos
   utxos = getUTXOs(bridge_, walletId_AFLB_, accountId_AFLB_, 0);
   ASSERT_EQ(utxos.size(), 5);

   //Block 4, tx 1, output 1, 5 COINS
   utxo = utxos[0];
   ASSERT_EQ(utxo.getHeight(), 4);
   ASSERT_EQ(utxo.getTxIndex(), 1);
   ASSERT_EQ(utxo.getTxOutIndex(), 1);
   ASSERT_EQ(utxo.getAmount(), 5 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash41);

   //Block 4, tx 3, output 0, 25 COINS
   utxo = utxos[1];
   ASSERT_EQ(utxo.getHeight(), 4);
   ASSERT_EQ(utxo.getTxIndex(), 3);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 25 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash43);

   //Block 4, tx 3, output 1, 20 COINS
   utxo = utxos[2];
   ASSERT_EQ(utxo.getHeight(), 4);
   ASSERT_EQ(utxo.getTxIndex(), 3);
   ASSERT_EQ(utxo.getTxOutIndex(), 1);
   ASSERT_EQ(utxo.getAmount(), 20 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash43);

   //Block 3, tx 5, output 0, 10 COINS
   utxo = utxos[3];
   ASSERT_EQ(utxo.getHeight(), 3);
   ASSERT_EQ(utxo.getTxIndex(), 5);
   ASSERT_EQ(utxo.getTxOutIndex(), 0);
   ASSERT_EQ(utxo.getAmount(), 10 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash35);

   //Block 3, tx 5, output 1, 5 COINS
   utxo = utxos[4];
   ASSERT_EQ(utxo.getHeight(), 3);
   ASSERT_EQ(utxo.getTxIndex(), 5);
   ASSERT_EQ(utxo.getTxOutIndex(), 1);
   ASSERT_EQ(utxo.getAmount(), 5 * COIN);
   ASSERT_EQ(utxo.getTxHash(), TestChain::hash35);
}

////////////////////////////////////////////////////////////////////////////////
// tx signing & zc broadcast
TEST_F(BridgeChainDataTests, ZeroConf)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //setup wallet ledger delegate
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());
   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   //setup address ledger delegates
   auto delegateScrAddrB = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrB);
   ASSERT_FALSE(delegateScrAddrB.empty());
   auto delegateScrAddrC = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrC);
   ASSERT_FALSE(delegateScrAddrC.empty());
   auto delegateScrAddrD = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrD);
   ASSERT_FALSE(delegateScrAddrD.empty());
   auto delegateScrAddrE = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrE);
   ASSERT_FALSE(delegateScrAddrE.empty());

   pageCount = getLedgersPageCount(bridge_, delegateScrAddrB);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrC);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrD);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrE);
   EXPECT_EQ(pageCount, 1);

   //check wallet ledgers
   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   /* address ledgers */

   //B
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr = BtcUtils::scrAddrToSegWitAddress(hash160);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ recipientAddr, 11 * COIN }},
         TestChain::scrAddrA, 2, false,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //wait on zc notif
   Tx tx(signedTx);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      ASSERT_EQ(zcLedgers[0].getValue(), -20 * (int64_t)COIN);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN));
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN));
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* address ledgers */

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i+1],
            TestChain::ledgersB[i]));
   }
   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* mine the tx */
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 6);

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN));
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN));
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check addr balances again
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), 6);

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i+1],
            TestChain::ledgersB[i]));
   }
   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), 6);
}

TEST_F(BridgeChainDataTests, ZeroConf_TxTime)
{
   //zc ledgers carry the time the db saw the tx, mined ones the block time
   loadWallets({walletId_BCDE_});
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();
   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_, walletId_BCDE_, accountId_BCDE_,
         {}, {{ BtcUtils::scrAddrToBase58(TestChain::scrAddrC), 11 * COIN }},
         TestChain::scrAddrD, 2, false, "privPass1");
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());
   Tx tx(signedTx);

   auto before = (uint32_t)time(0);
   broadcastTx(bridge_, signedTx);
   auto zcLedgers = waitOnZc();
   auto after = (uint32_t)time(0);

   //zc notification
   ASSERT_EQ(zcLedgers.size(), 1);
   ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
   EXPECT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_GE(zcLedgers[0].getTxTime(), before);
   EXPECT_LE(zcLedgers[0].getTxTime(), after);

   //ledger page: the zc first, then the mined history
   auto delegateId = getLedgerDelegateId(bridge_);
   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_GE(ledgers.size(), 2);
   ASSERT_EQ(ledgers[0].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_GE(ledgers[0].getTxTime(), before);
   EXPECT_LE(ledgers[0].getTxTime(), after);
   for (unsigned i = 1; i < ledgers.size(); i++) {
      EXPECT_NE(ledgers[i].getBlockNum(), UINT32_MAX);
      EXPECT_LT(ledgers[i].getTxTime(), before);
   }
   auto zcTime = ledgers[0].getTxTime();

   //a new bridge gets the zc from the db's txios instead of a notification
   bridge_.reset();
   replyQueue.clear();
   bridge_ = std::make_shared<Bridge::CppBridge>(
      std::make_shared<TestBridgeSocket>());
   loadWallets({walletId_BCDE_});
   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   ASSERT_EQ(goOnline(bridge_), 5);

   delegateId = getLedgerDelegateId(bridge_);
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_GE(ledgers.size(), 2);
   ASSERT_EQ(ledgers[0].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_EQ(ledgers[0].getTxTime(), zcTime);
   for (unsigned i = 1; i < ledgers.size(); i++) {
      EXPECT_NE(ledgers[i].getBlockNum(), UINT32_MAX);
      EXPECT_LT(ledgers[i].getTxTime(), before);
   }
}

TEST_F(BridgeChainDataTests, ZeroConf_Replace)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
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
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr = BtcUtils::scrAddrToSegWitAddress(hash160);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ recipientAddr, 11 * COIN }},
         TestChain::scrAddrA, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //wait on zc notif
   Tx tx(signedTx);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -20 * (int64_t)COIN);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   //get rbf UTXOs
   auto rbfUtxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 2);
   ASSERT_EQ(rbfUtxos.size(), 1);

   /* RBF fee
      There is no actual network requirement for this value. The test suite's
      mocked node rejects RBFs with less than 100000000 satoshis in fees
      as convention.
      This has no bearing on mainnet operations and only serves as an easy
      trick for test purposes. Do not set actual RBF fee to such ridiculous
      amount on the mainnet!
   */
   uint64_t rbfFee = 110000000;
   BinaryData signedTxRbf;
   try {
      //respend the output
      signedTxRbf = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         rbfUtxos, {{ recipientAddr, 13 * COIN }},
         TestChain::scrAddrE, rbfFee, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTxRbf.empty());

   //broadcast
   broadcastTx(bridge_, signedTxRbf);

   //wait on zc notif
   int64_t changeVal = 7 * COIN - rbfFee;
   int64_t totalDiff = -20 * (int64_t)COIN + changeVal;
   Tx txRbf(signedTxRbf);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), txRbf.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), totalDiff);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);

      auto invalidatedHashes = waitOnInvalidatedZCs();
      ASSERT_EQ(invalidatedHashes.size(), 1);
      ASSERT_EQ(*invalidatedHashes.begin(), tx.getThisHash());
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] -20 * (int64_t)COIN);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] -20 * (int64_t)COIN);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] -20 * (int64_t)COIN);

   addrBBal = balances.at(TestChain::scrAddrE);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrE);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeVal);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeVal);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), txRbf.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), totalDiff);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* mine the rbf */
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 6);

   //check balances again
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrE);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrE);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeVal);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] + changeVal);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeVal);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), txRbf.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), totalDiff);
   ASSERT_EQ(lastEntry.getBlockNum(), 6);
}

TEST_F(BridgeChainDataTests, ZeroConf_Chain)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
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
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr1 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ recipientAddr1, 11 * COIN }},
         TestChain::scrAddrC, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //grab amount for change output
   Tx tx(signedTx);
   auto changeOutput = tx.getTxOutCopy(1);
   uint64_t changeAmount = changeOutput.getAmount();
   EXPECT_TRUE(changeAmount > 8 * COIN);
   EXPECT_TRUE(changeAmount < 9 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* second zc */

   //get ZC utxo
   auto zcUtxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 1);
   ASSERT_EQ(zcUtxos.size(), 1);

   //create second recipient address
   recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr2 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //spend the zc utxo
   BinaryData signedTx2;
   try {
      signedTx2 = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         zcUtxos, {{ recipientAddr2, 5 * COIN }},
         TestChain::scrAddrC, 1000, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx2.empty());

   Tx tx2(signedTx2);
   auto changeOutput2 = tx2.getTxOutCopy(1);
   uint64_t changeAmount2 = changeOutput2.getAmount();
   EXPECT_TRUE(changeAmount2 > 3 * COIN);
   EXPECT_TRUE(changeAmount2 < 4 * COIN);

   //broadcast & wait on zc notif
   broadcastTx(bridge_, signedTx2);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 2);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx2.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -changeAmount + changeAmount2);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[0].isChainedZC());

      ASSERT_EQ(zcLedgers[1].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[1].getBlockNum(), UINT32_MAX);
      EXPECT_FALSE(zcLedgers[1].isChainedZC());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(ledgers[0].isChainedZC());

   /* mine the 2 tx */
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 6);

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] + changeAmount2);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), 6);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), 6);
   EXPECT_FALSE(ledgers[0].isChainedZC());
}

TEST_F(BridgeChainDataTests, ZeroConf_StaggeredChain)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //grab wallet ledger delegates
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());
   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   //setup address ledger delegates
   auto delegateScrAddrB = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrB);
   ASSERT_FALSE(delegateScrAddrB.empty());
   auto delegateScrAddrC = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrC);
   ASSERT_FALSE(delegateScrAddrC.empty());
   auto delegateScrAddrD = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrD);
   ASSERT_FALSE(delegateScrAddrD.empty());
   auto delegateScrAddrE = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrE);
   ASSERT_FALSE(delegateScrAddrE.empty());

   pageCount = getLedgersPageCount(bridge_, delegateScrAddrB);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrC);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrD);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrE);
   EXPECT_EQ(pageCount, 1);

   //check wallet ledgers
   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   /* check address ledgers */

   //B
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr1 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ recipientAddr1, 11 * COIN }},
         TestChain::scrAddrC, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //grab amount for change output
   Tx tx(signedTx);
   auto changeOutput = tx.getTxOutCopy(1);
   uint64_t changeAmount = changeOutput.getAmount();
   EXPECT_TRUE(changeAmount > 8 * COIN);
   EXPECT_TRUE(changeAmount < 9 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* address ledgers */
   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersC[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(lastEntry.isChainedZC());

   /* second zc */

   //get ZC utxo
   auto zcUtxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 1);
   ASSERT_EQ(zcUtxos.size(), 1);

   //create second recipient address
   recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr2 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //spend the zc utxo
   BinaryData signedTx2;
   try {
      signedTx2 = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         zcUtxos, {{ recipientAddr2, 5 * COIN }},
         TestChain::scrAddrC, 1000, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx2.empty());

   Tx tx2(signedTx2);
   auto changeOutput2 = tx2.getTxOutCopy(1);
   uint64_t changeAmount2 = changeOutput2.getAmount();
   EXPECT_TRUE(changeAmount2 > 3 * COIN);
   EXPECT_TRUE(changeAmount2 < 4 * COIN);

   //broadcast & wait on zc notif
   //introduce a 1 block delay in mining so the 2 tx do not
   //mine in the same block
   nodePtr_->delayNextZc(1);
   broadcastTx(bridge_, signedTx2);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 2);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx2.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -changeAmount + changeAmount2);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[0].isChainedZC());

      ASSERT_EQ(zcLedgers[1].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[1].getBlockNum(), UINT32_MAX);
      EXPECT_FALSE(zcLedgers[1].isChainedZC());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 2);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 2);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(ledgers[0].isChainedZC());

   /* address ledgers */
   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 6);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 2],
            TestChain::ledgersC[i]));
   }

   lastEntry = ledgersAtBlocks[1];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(lastEntry.isChainedZC());

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx2.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -(int64_t)changeAmount + int64_t(changeAmount2));
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isChainedZC());

   /* mine first tx */
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 6);

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 2);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 2);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), 6);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(ledgers[0].isChainedZC());

   /* address ledgers */
   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), 6);
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 6);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 2],
            TestChain::ledgersC[i]));
   }

   lastEntry = ledgersAtBlocks[1];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), 6);
   EXPECT_FALSE(lastEntry.isChainedZC());

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx2.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -(int64_t)changeAmount + int64_t(changeAmount2));
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(lastEntry.isChainedZC());

   /* mine second tx */
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 7);

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 2);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] + changeAmount2);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 2);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), 6);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), 7);
   EXPECT_FALSE(ledgers[0].isChainedZC());

   /* address ledgers */
   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), 6);
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 6);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 2],
            TestChain::ledgersC[i]));
   }

   lastEntry = ledgersAtBlocks[1];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), 6);
   EXPECT_FALSE(lastEntry.isChainedZC());

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx2.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -(int64_t)changeAmount + int64_t(changeAmount2));
   ASSERT_EQ(lastEntry.getBlockNum(), 7);
   EXPECT_FALSE(lastEntry.isChainedZC());
}

TEST_F(BridgeChainDataTests, ZeroConf_ChainRBF)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //setup wallet ledger delegates
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());
   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   //setup address ledger delegates
   auto delegateScrAddrB = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrB);
   ASSERT_FALSE(delegateScrAddrB.empty());
   auto delegateScrAddrC = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrC);
   ASSERT_FALSE(delegateScrAddrC.empty());
   auto delegateScrAddrD = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrD);
   ASSERT_FALSE(delegateScrAddrD.empty());
   auto delegateScrAddrE = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrE);
   ASSERT_FALSE(delegateScrAddrE.empty());

   pageCount = getLedgersPageCount(bridge_, delegateScrAddrB);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrC);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrD);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrE);
   EXPECT_EQ(pageCount, 1);

   // check wallet ledgers
   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   /* check address ledgers */

   //B
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr1 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ recipientAddr1, 11 * COIN }},
         TestChain::scrAddrC, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //grab amount for change output
   Tx tx(signedTx);
   auto changeOutput = tx.getTxOutCopy(1);
   uint64_t changeAmount = changeOutput.getAmount();
   EXPECT_TRUE(changeAmount > 8 * COIN);
   EXPECT_TRUE(changeAmount < 9 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[0].isOptInRBF());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());

   /* address ledgers */

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersC[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());

   /* second zc */

   //get ZC utxo
   auto zcUtxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 1);
   ASSERT_EQ(zcUtxos.size(), 1);

   //create second recipient address
   recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr2 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //spend the zc utxo
   BinaryData signedTx2;
   try {
      signedTx2 = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         zcUtxos, {{ recipientAddr2, 5 * COIN }},
         TestChain::scrAddrC, 1000, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx2.empty());

   Tx tx2(signedTx2);
   auto changeOutput2 = tx2.getTxOutCopy(1);
   uint64_t changeAmount2 = changeOutput2.getAmount();
   EXPECT_TRUE(changeAmount2 > 3 * COIN);
   EXPECT_TRUE(changeAmount2 < 4 * COIN);

   //broadcast & wait on zc notif
   broadcastTx(bridge_, signedTx2);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 2);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx2.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -changeAmount + changeAmount2);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[0].isChainedZC());
      EXPECT_TRUE(zcLedgers[0].isOptInRBF());

      ASSERT_EQ(zcLedgers[1].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[1].getBlockNum(), UINT32_MAX);
      EXPECT_FALSE(zcLedgers[1].isChainedZC());
      EXPECT_TRUE(zcLedgers[1].isOptInRBF());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmount2);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 2);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 2);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(ledgers[1].isOptInRBF());
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(ledgers[0].isOptInRBF());
   EXPECT_TRUE(ledgers[0].isChainedZC());

   /* address ledgers */

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 6);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 2],
            TestChain::ledgersC[i]));
   }

   lastEntry = ledgersAtBlocks[1];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx2.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_TRUE(lastEntry.isChainedZC());

   //get rbf UTXOs
   auto rbfUtxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 2);
   ASSERT_EQ(rbfUtxos.size(), 3);

   //look for the mined output
   int utxoId = -1;
   for (unsigned i = 0; i < rbfUtxos.size(); i++) {
      if (rbfUtxos[i].getHeight() != UINT32_MAX) {
         utxoId = i;
         break;
      }
   }
   ASSERT_NE(utxoId, -1);

   std::vector<UTXO> rbfVec{ rbfUtxos[utxoId] };
   int64_t rbfFee = 110000000;
   BinaryData txRbf;
   try {
      txRbf = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         rbfVec, {{ recipientAddr1, 8 * COIN }},
         TestChain::scrAddrE, rbfFee, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(txRbf.empty());

   Tx tx3(txRbf);
   auto changeOutput3 = tx3.getTxOutCopy(1);
   uint64_t changeAmountRbf = changeOutput3.getAmount();
   EXPECT_EQ(changeAmountRbf, 12 * COIN - rbfFee);

   //broadcast it
   nodePtr_->delayNextZc(1);
   broadcastTx(bridge_, txRbf);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx3.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -8 * (int64_t)COIN -rbfFee);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_FALSE(zcLedgers[0].isChainedZC());

      auto invalidatedHashes = waitOnInvalidatedZCs();
      ASSERT_EQ(invalidatedHashes.size(), 2);
      std::set<BinaryData> ihSet{
         invalidatedHashes.begin(),
         invalidatedHashes.end()
      };

      ASSERT_NE(ihSet.find(tx.getThisHash()), ihSet.end());
      ASSERT_NE(ihSet.find(tx2.getThisHash()), ihSet.end());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmountRbf);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmountRbf);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0]);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2]);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3]);

   addrBBal = balances.at(TestChain::scrAddrE);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrE);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmountRbf);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmountRbf);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[0].getTxHash(), tx3.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -8 * (int64_t)COIN -rbfFee);
   ASSERT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(ledgers[0].isOptInRBF());
   EXPECT_FALSE(ledgers[0].isChainedZC());

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx3.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < TestChain::ledgersE.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersE[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx3.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmountRbf);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());
}

TEST_F(BridgeChainDataTests, ZeroConf_Reload)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
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
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr1 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ recipientAddr1, 11 * COIN }},
         TestChain::scrAddrC, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //grab amount for change output
   Tx tx(signedTx);
   auto changeOutput = tx.getTxOutCopy(1);
   uint64_t changeAmount = changeOutput.getAmount();
   EXPECT_TRUE(changeAmount > 8 * COIN);
   EXPECT_TRUE(changeAmount < 9 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* second zc */

   //get ZC utxo
   auto zcUtxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 1);
   ASSERT_EQ(zcUtxos.size(), 1);

   //create second recipient address
   recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr2 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //spend the zc utxo
   BinaryData signedTx2;
   try {
      signedTx2 = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         zcUtxos, {{ recipientAddr2, 5 * COIN }},
         TestChain::scrAddrC, 1000, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx2.empty());

   Tx tx2(signedTx2);
   auto changeOutput2 = tx2.getTxOutCopy(1);
   uint64_t changeAmount2 = changeOutput2.getAmount();
   EXPECT_TRUE(changeAmount2 > 3 * COIN);
   EXPECT_TRUE(changeAmount2 < 4 * COIN);

   //broadcast & wait on zc notif
   broadcastTx(bridge_, signedTx2);
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 2);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx2.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -changeAmount + changeAmount2);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[0].isChainedZC());

      ASSERT_EQ(zcLedgers[1].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[1].getBlockNum(), UINT32_MAX);
      EXPECT_FALSE(zcLedgers[1].isChainedZC());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(ledgers[0].isChainedZC());

   /* cycle bridge */
   bridge_.reset();
   replyQueue.clear();
   bridge_ = std::make_shared<Bridge::CppBridge>(
      std::make_shared<TestBridgeSocket>());

   loadWallets({walletId_BCDE_});
   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   ASSERT_EQ(goOnline(bridge_), 5);

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(ledgers[0].isChainedZC());

   /* mine the 2 tx */
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   ASSERT_EQ(waitOnNewBlock(), 6);

   //check balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount2);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] + changeAmount2);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount2);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   ASSERT_EQ(ledgers[1].getTxHash(), tx.getThisHash());
   EXPECT_EQ(ledgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(ledgers[1].getBlockNum(), 6);
   EXPECT_FALSE(ledgers[1].isChainedZC());

   ASSERT_EQ(ledgers[0].getTxHash(), tx2.getThisHash());
   EXPECT_EQ(ledgers[0].getValue(), -changeAmount + changeAmount2);
   ASSERT_EQ(ledgers[0].getBlockNum(), 6);
   EXPECT_FALSE(ledgers[0].isChainedZC());
}

TEST_F(BridgeChainDataTests, ZeroConf_Reorg)
{
   loadWallets({walletId_BCDE_});

   TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 4);

   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //setup wallet ledger delegate
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());
   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   //setup address ledger delegates
   auto delegateScrAddrB = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrB);
   ASSERT_FALSE(delegateScrAddrB.empty());
   auto delegateScrAddrC = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrC);
   ASSERT_FALSE(delegateScrAddrC.empty());
   auto delegateScrAddrD = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrD);
   ASSERT_FALSE(delegateScrAddrD.empty());
   auto delegateScrAddrE = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrE);
   ASSERT_FALSE(delegateScrAddrE.empty());

   pageCount = getLedgersPageCount(bridge_, delegateScrAddrB);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrC);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrD);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrE);
   EXPECT_EQ(pageCount, 1);

   //check wallet ledgers
   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   /* address ledgers */

   //B
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   /* create & push zc */

   //get UTXOs
   auto utxos = getUTXOs(bridge_, walletId_BCDE_, accountId_BCDE_, 0);
   ASSERT_EQ(utxos.size(), 8);

   //we want to spend output from block 5, tx2, index 0, since it
   //does not exist on our reorg chain
   unsigned utxoId = UINT32_MAX;
   for (unsigned i = 0; i < utxos.size(); i++) {
      const auto& utxo = utxos[i];
      if (utxo.getHeight() == 5 && utxo.getTxIndex() == 2 && utxo.getTxOutIndex() == 0) {
         utxoId = i;
         break;
      }
   }
   ASSERT_NE(utxoId, UINT32_MAX);

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr1 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {utxos[utxoId]}, {{ recipientAddr1, 2 * COIN }},
         TestChain::scrAddrC, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //grab amount for change output
   Tx tx(signedTx);
   auto changeOutput = tx.getTxOutCopy(1);
   uint64_t changeAmount = changeOutput.getAmount();
   EXPECT_TRUE(changeAmount > 2 * COIN);
   EXPECT_TRUE(changeAmount < 3 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -5 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] -(5 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] -(5 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] -(5 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrD);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrD);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (5 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (5 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (5 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -5 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* check address ledgers */

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersC[i]));
   }
   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < TestChain::ledgersD.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersD[i]));
   }
   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -5 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* reorg to block 5A*/
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat_);
   nodePtr_->notifyNewBlock();
   ASSERT_EQ(waitOnNewBlock(), 5);

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE_Reorg[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE_Reorg[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE_Reorg[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE_Reorg[5][3]);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, true);

   //wallet ledgers
   pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgersAt5Blocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAt5Blocks.size(), 11);

   for (unsigned i = 0; i < ledgersAt5Blocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAt5Blocks[i],
         TestChain::ledgersBCDE_Reorg[i]));
   }

   /* address ledgers */

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB_Reorg[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC_Reorg[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 3);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD_Reorg[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE_Reorg[i]));
   }
}

TEST_F(BridgeChainDataTests, DISABLED_ZeroConf_RegisterWallet)
{
   //NOTE: reenable once supernode scanner is redesigned
   /* this test only works with a supernode db */

   //shutdown bdm, clean up db folder
   theBDMt_->shutdown();
   delete theBDMt_;
   nodePtr_ = nullptr;
   FileUtils::removeDirectory(ldbdir_);
   FileUtils::createDirectory(ldbdir_);

   //set db mode to supernode
   Config::reset();
   Config::DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);
   Config::parseArgs({
      "--datadir=./fakehomedir",
      "--dbdir=./ldbtestdir",
      "--satoshi-datadir=./blkfiletest",
      "--db-type=DB_SUPER",
      "--thread-count=3",
      "--public"},
      Config::ProcessType::DB
   );

   //reset db
   initBDM();
   nodePtr_ = std::dynamic_pointer_cast<NodeUnitTest>(
      Config::NetworkSettings::bitcoinNodes().first);
   nodePtr_->setBDM(theBDMt_->bdm());

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   //init bridge
   loadWallets({walletId_BCDE_});
   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   //check addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //check ledgers
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());

   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ BtcUtils::scrAddrToBase58(TestChain::scrAddrA), 11 * COIN }},
         TestChain::scrAddrB, 2, false,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //wait on zc notif
   Tx tx(signedTx);
   int64_t spentAmount = -1100000516;
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      ASSERT_EQ(zcLedgers[0].getValue(), spentAmount);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] + spentAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] + spentAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + spentAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + spentAmount);

   //check ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 16);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+1], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), spentAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   /* load & register wallet AFLB */
   auto stagedIds = loadWallet(walletId_AFLB_);
   auto dbId = stagedIds.at(accountId_AFLB_);
   ASSERT_FALSE(dbId.empty());
   ASSERT_TRUE(registerWallet(bridge_, walletId_AFLB_, accountId_AFLB_, dbId));

   //recheck bcde balances
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] + spentAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] + spentAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   addrBBal = balances.at(TestChain::scrAddrB);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + spentAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + spentAmount);

   //check aflb balances
   wltBal = getWalletBalance(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_AFLB[5][0] + 11 * COIN);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_AFLB[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_AFLB[5][2] + 11 * COIN);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_AFLB[5][3] + 1);

   balances = getAddrBalances(bridge_, walletId_AFLB_, accountId_AFLB_);
   ASSERT_EQ(balances.size(), 6);
   addrBBal = balances.at(TestChain::scrAddrA);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrA);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + 11 * COIN);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + 11 * COIN);

   //check ledgers
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 28);

   std::vector<TestChain::LedgerEntryValue> combinedLedgers;
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersBCDE.begin(), TestChain::ledgersBCDE.end());
   combinedLedgers.insert(combinedLedgers.end(),
      TestChain::ledgersAFLB.begin(), TestChain::ledgersAFLB.end());
   ASSERT_EQ(combinedLedgers.size(), 26);
   std::sort(combinedLedgers.begin(), combinedLedgers.end());

   for (unsigned i = 0; i < combinedLedgers.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i+2],
            combinedLedgers[i])) << i;
   }

   lastEntry = ledgersAtBlocks[1];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), spentAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   ASSERT_EQ(lastEntry.getValue(), 11 * COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
}

TEST_F(BridgeChainDataTests, RestoreSynchronize)
{
   /*
   Checks that a restored wallet with history gets its use chain
   synchronized with on chain data; both length and address types.
   */

   //create BIP32 wallet, grab backup
   auto bip32WltId = createAWallet(bridge_, 50ms, 10, true);
   ASSERT_FALSE(bip32WltId.empty());
   auto bip32Backup = getWalletBackup(bridge_, bip32WltId, "pass1",
      Codec::Bridge::WalletBackup::Type::ARMORY200_B);
   ASSERT_FALSE(bip32Backup.empty());

   //create legacy wallet, grab backup
   auto legacyWltId = createAWallet(bridge_, 50ms, 10, false);
   ASSERT_FALSE(legacyWltId.empty());
   auto legacyBackup = getWalletBackup(bridge_, legacyWltId, "pass1",
      Codec::Bridge::WalletBackup::Type::ARMORY200_A);
   ASSERT_FALSE(legacyBackup.empty());

   //load BCDE, the new wallets are loaded as they're created
   loadWallets({walletId_BCDE_});

   //grab account ids for new wallets
   auto bip32AccIds = getWalletAccountIds(bridge_, bip32WltId);
   ASSERT_EQ(bip32AccIds.size(), 3);

   auto legacyAccIds = getWalletAccountIds(bridge_, legacyWltId);
   ASSERT_EQ(legacyAccIds.size(), 1);

   /* grab some addresses */
   auto getNewAddress = [](std::shared_ptr<Bridge::CppBridge> bridge,
      std::string wltId, std::string accId, unsigned count,
      uint32_t addrType = 0)->AddressData
   {
      for (unsigned i = 0; i < count - 1; i++) {
         getAddress(bridge, wltId, accId);
      }
      return getAddress(bridge, wltId, accId, addrType);
   };

   //advance the highest used index per each bip32 account
   auto bip32AccIdIter = bip32AccIds.begin();
   auto addrBip32Acc1 = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter, 5);
   ASSERT_EQ(addrBip32Acc1.index, 5);
   ASSERT_TRUE(addrBip32Acc1.isUsed);
   auto addrBip32Acc1Next = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter++, 1);

   auto addrBip32Acc2 = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter, 3);
   ASSERT_EQ(addrBip32Acc2.index, 3);
   ASSERT_TRUE(addrBip32Acc2.isUsed);
   auto addrBip32Acc2Next = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter++, 1);

   auto addrBip32Acc3 = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter, 7);
   ASSERT_EQ(addrBip32Acc3.index, 7);
   ASSERT_TRUE(addrBip32Acc3.isUsed);
   auto addrBip32Acc3Next = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter++, 1);

   //get a non default address from the legacy wallet
   auto addrLegacy = getNewAddress(bridge_, legacyWltId, *legacyAccIds.begin(), 4,
      uint32_t(AddressEntryType::P2PK | AddressEntryType::P2SH));
   ASSERT_EQ(addrLegacy.index, 4);
   ASSERT_EQ(addrLegacy.type, uint32_t(AddressEntryType::P2PK | AddressEntryType::P2SH));
   auto addrLegacyNext = getNewAddress(bridge_, legacyWltId, *legacyAccIds.begin(), 1);

   //register wallets, go online
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {
            { addrBip32Acc1.addrStr , 1 * COIN },
            { addrBip32Acc2.addrStr , 2 * COIN },
            { addrBip32Acc3.addrStr , 3 * COIN },
            { addrLegacy.addrStr    , 4 * COIN }
         }, TestChain::scrAddrC, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //grab amount for change output
   Tx tx(signedTx);
   auto changeOutput = tx.getTxOutCopy(4);
   uint64_t changeAmount = changeOutput.getAmount();
   EXPECT_TRUE(changeAmount > 9 * COIN);
   EXPECT_TRUE(changeAmount < 10 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 5);
      ASSERT_EQ(zcLedgers[4].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[4].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[4].getBlockNum(), UINT32_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check balances and highest used index
   std::filesystem::path wltBip32Path, wltLegacyPath;
   {
      bip32AccIdIter = bip32AccIds.begin();
      auto wltBal = getWalletBalance(bridge_, bip32WltId, *bip32AccIdIter);
      auto wltData1 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter++);
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 1 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 1 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData1.useCount, 6);
      EXPECT_EQ(wltData1.lookup, 10);
      wltBip32Path = wltData1.path;

      wltBal = getWalletBalance(bridge_, bip32WltId, *bip32AccIdIter);
      auto wltData2 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter++);
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 2 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 2 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData2.useCount, 4);
      EXPECT_EQ(wltData2.lookup, 10);

      wltBal = getWalletBalance(bridge_, bip32WltId, *bip32AccIdIter);
      auto wltData3 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter);
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 3 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 3 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData3.useCount, 8);
      EXPECT_EQ(wltData3.lookup, 10);

      wltBal = getWalletBalance(bridge_, legacyWltId, *legacyAccIds.begin());
      auto wltData4 = getWalletData(bridge_, legacyWltId, *legacyAccIds.begin());
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 4 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 4 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData4.useCount, 5);
      EXPECT_EQ(wltData4.lookup, 10);
      wltLegacyPath = wltData4.path;
   }

   /* delete the 2 new wallets */

   //check wallet path
   ASSERT_TRUE(FileUtils::pathExists(wltBip32Path, 0));
   ASSERT_TRUE(FileUtils::pathExists(wltLegacyPath, 0));

   //delete bip32 wlt
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWalletManager();
      request.setDeleteWallet(bip32WltId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      //validate reply
      while (true) {
         auto result = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(result->data.getPtr()),
            result->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
            continue;
         }
         auto reply = fromBridge.getReply();
         ASSERT_TRUE(reply.getSuccess());
         ASSERT_EQ(reply.getReferenceId(), refId);
         ASSERT_FALSE(FileUtils::pathExists(wltBip32Path, 0));
         break;
      }
   }

   //delete legacy wlt
   {
      uint64_t refId = rand();
      capnp::MallocMessageBuilder message;
      auto toBridge = message.initRoot<Codec::Bridge::ToBridge>();
      toBridge.setReferenceId(refId);
      auto request = toBridge.initWalletManager();
      request.setDeleteWallet(legacyWltId);
      auto rawReq = serializeCapnp(message);
      pushRequest(bridge_, rawReq);

      //validate reply
      while (true) {
         auto result = waitOnReply();
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(result->data.getPtr()),
            result->data.getSize() / sizeof(capnp::word));
         capnp::FlatArrayMessageReader reader(words);
         auto fromBridge = reader.getRoot<Codec::Bridge::FromBridge>();
         if (fromBridge.which() != Codec::Bridge::FromBridge::REPLY) {
            continue;
         }
         auto reply = fromBridge.getReply();
         ASSERT_TRUE(reply.getSuccess());
         ASSERT_EQ(reply.getReferenceId(), refId);
         ASSERT_FALSE(FileUtils::pathExists(wltLegacyPath, 0));
         break;
      }
   }

   /* cycle bridge */
   bridge_.reset();
   replyQueue.clear();
   bridge_ = std::make_shared<Bridge::CppBridge>(
      std::make_shared<TestBridgeSocket>());

   //restore bip32 wlt
   unsigned restoreLookup = 500;
   restoreWallet(bridge_, bip32Backup, bip32WltId,
      Codec::Bridge::WalletBackup::Type::ARMORY200_B,
      "pass2", 50ms, 0, false, restoreLookup);

   {
      bip32AccIdIter = bip32AccIds.begin();
      auto wltData1 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter++);
      EXPECT_EQ(wltData1.useCount, 0);
      EXPECT_EQ(wltData1.lookup, restoreLookup);

      auto wltData2 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter++);
      EXPECT_EQ(wltData2.useCount, -1);
      EXPECT_EQ(wltData2.lookup, restoreLookup);

      auto wltData3 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter);
      EXPECT_EQ(wltData3.useCount, -1);
      EXPECT_EQ(wltData3.lookup, restoreLookup);
   }

   //restore legacy wlt
   restoreWallet(bridge_, legacyBackup, legacyWltId,
      Codec::Bridge::WalletBackup::Type::ARMORY200_A,
      "pass3", 50ms, 0, false, restoreLookup);

   {
      auto wltData4 = getWalletData(bridge_, legacyWltId, *legacyAccIds.begin());
      EXPECT_EQ(wltData4.useCount, 0);
      EXPECT_EQ(wltData4.lookup, restoreLookup + 1);
   }

   //go online
   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));
   ASSERT_EQ(goOnline(bridge_), 5);

   /* check chain lengths */
   {
      bip32AccIdIter = bip32AccIds.begin();
      auto wltBal = getWalletBalance(bridge_, bip32WltId, *bip32AccIdIter);
      auto wltData1 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter);
      auto nextAddr1 = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter++, 1);
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 1 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 1 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData1.useCount, 5);
      EXPECT_EQ(wltData1.lookup, restoreLookup);
      EXPECT_EQ(nextAddr1.addrStr, addrBip32Acc1Next.addrStr);

      wltBal = getWalletBalance(bridge_, bip32WltId, *bip32AccIdIter);
      auto wltData2 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter);
      auto nextAddr2 = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter++, 1);
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 2 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 2 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData2.useCount, 3);
      EXPECT_EQ(wltData2.lookup, restoreLookup);
      EXPECT_EQ(nextAddr2.addrStr, addrBip32Acc2Next.addrStr);

      wltBal = getWalletBalance(bridge_, bip32WltId, *bip32AccIdIter);
      auto wltData3 = getWalletData(bridge_, bip32WltId, *bip32AccIdIter);
      auto nextAddr3 = getNewAddress(bridge_, bip32WltId, *bip32AccIdIter, 1);
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 3 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 3 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData3.useCount, 7);
      EXPECT_EQ(wltData3.lookup, restoreLookup);
      EXPECT_EQ(nextAddr3.addrStr, addrBip32Acc3Next.addrStr);

      wltBal = getWalletBalance(bridge_, legacyWltId, *legacyAccIds.begin());
      auto wltData4 = getWalletData(bridge_, legacyWltId, *legacyAccIds.begin());
      auto nextAddr4 = getNewAddress(bridge_, legacyWltId, *legacyAccIds.begin(), 1);
      ASSERT_EQ(wltBal.size(), 4);
      EXPECT_EQ(wltBal[0], 4 * COIN);
      EXPECT_EQ(wltBal[1], 0);
      EXPECT_EQ(wltBal[2], 4 * COIN);
      EXPECT_EQ(wltBal[3], 1);
      EXPECT_EQ(wltData4.useCount, 4);
      EXPECT_EQ(wltData4.lookup, restoreLookup + 1);
      EXPECT_EQ(nextAddr4.addrStr, addrLegacyNext.addrStr);

      //check legacy wallet address type
      auto iter = wltData4.addresses.find(addrLegacy.hash);
      ASSERT_NE(iter, wltData4.addresses.end());
      EXPECT_EQ(iter->addrStr, addrLegacy.addrStr);
      EXPECT_EQ(iter->type, addrLegacy.type);
   }
}

TEST_F(BridgeChainDataTests, ZeroConf_SpendNew)
{
   loadWallets({walletId_BCDE_});
   auto legacyWltId = createAWallet(bridge_, 50ms, 10, false);
   ASSERT_FALSE(legacyWltId.empty());

   auto legacyAccIds = getWalletAccountIds(bridge_, legacyWltId);
   ASSERT_EQ(legacyAccIds.size(), 1);
   auto legacyAccId = *legacyAccIds.begin();
   ASSERT_FALSE(legacyAccId.empty());

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer();

   ASSERT_TRUE(connectToIp(bridge_, "127.0.0.1", 9001, serverPubkey_));
   ASSERT_TRUE(registerWallets(bridge_));

   //start db, go online and wait on ready notif
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   ASSERT_EQ(goOnline(bridge_), 5);

   //check wallet balance
   auto wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0]);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1]);
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2]);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3]);

   wltBal = getWalletBalance(bridge_, legacyWltId, legacyAccId);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], 0);
   EXPECT_EQ(wltBal[1], 0);
   EXPECT_EQ(wltBal[2], 0);
   EXPECT_EQ(wltBal[3], 0);

   //check addr balances
   auto balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   checkBalances(balances, 5, false);

   //setup wallet ledger delegates
   auto delegateId = getLedgerDelegateId(bridge_);
   ASSERT_FALSE(delegateId.empty());
   auto pageCount = getLedgersPageCount(bridge_, delegateId);
   EXPECT_EQ(pageCount, 1);

   //setup address ledger delegates
   auto delegateScrAddrB = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrB);
   ASSERT_FALSE(delegateScrAddrB.empty());
   auto delegateScrAddrC = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrC);
   ASSERT_FALSE(delegateScrAddrC.empty());
   auto delegateScrAddrD = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrD);
   ASSERT_FALSE(delegateScrAddrD.empty());
   auto delegateScrAddrE = getLedgerDelegateIdForScrAddr(bridge_,
      walletId_BCDE_, accountId_BCDE_, TestChain::scrAddrE);
   ASSERT_FALSE(delegateScrAddrE.empty());

   pageCount = getLedgersPageCount(bridge_, delegateScrAddrB);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrC);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrD);
   EXPECT_EQ(pageCount, 1);
   pageCount = getLedgersPageCount(bridge_, delegateScrAddrE);
   EXPECT_EQ(pageCount, 1);

   // check wallet ledgers
   auto ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 15);

   for (unsigned i = 0; i < ledgers.size(); i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i], TestChain::ledgersBCDE[i])) << i;
   }

   /* check address ledgers */

   //B
   auto ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 7);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersB[i]));
   }

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersC[i]));
   }

   //D
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrD, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 4);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersD[i]));
   }

   //E
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrE, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 2);
   for (unsigned i = 0; i < ledgersAtBlocks.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i],
            TestChain::ledgersE[i]));
   }

   //create recipient address
   auto legacyAddr = getAddress(bridge_, legacyWltId, legacyAccId);

   //get signed tx
   BinaryData signedTx;
   try {
      signedTx = createAndSignTx(bridge_,
         walletId_BCDE_, accountId_BCDE_,
         {}, {{ legacyAddr.addrStr, 11 * COIN }},
         TestChain::scrAddrC, 2, true,
         "privPass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx.empty());

   //broadcast
   broadcastTx(bridge_, signedTx);

   //grab amount for change output
   Tx tx(signedTx);
   auto changeOutput = tx.getTxOutCopy(1);
   uint64_t changeAmount = changeOutput.getAmount();
   EXPECT_TRUE(changeAmount > 8 * COIN);
   EXPECT_TRUE(changeAmount < 9 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 2);
      ASSERT_EQ(zcLedgers[1].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[1].getValue(), -20 * (int64_t)COIN + changeAmount);
      ASSERT_EQ(zcLedgers[1].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[1].isOptInRBF());

      ASSERT_EQ(zcLedgers[0].getTxHash(), tx.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), 11 * COIN);
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[0].isOptInRBF());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN));
   EXPECT_EQ(wltBal[2], TestChain::wltBal_BCDE[5][2] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   wltBal = getWalletBalance(bridge_, legacyWltId, legacyAccId);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], 11 * COIN);
   EXPECT_EQ(wltBal[1], 0);
   EXPECT_EQ(wltBal[2], 11 * COIN);
   EXPECT_EQ(wltBal[3], 1);

   //check addr balances
   balances = getAddrBalances(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(balances.size(), 4);
   auto addrBBal = balances.at(TestChain::scrAddrB);
   auto testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrB);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] - (20 * COIN));
   EXPECT_EQ(addrBBal[1], testAddrBBal[1] - (20 * COIN));
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] - (20 * COIN));
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   addrBBal = balances.at(TestChain::scrAddrC);
   testAddrBBal = TestChain::testAddrBalances[5].at(TestChain::scrAddrC);
   EXPECT_EQ(addrBBal[0], testAddrBBal[0] + changeAmount);
   EXPECT_EQ(addrBBal[1], testAddrBBal[1]);
   EXPECT_EQ(addrBBal[2], testAddrBBal[2] + changeAmount);
   EXPECT_EQ(addrBBal[3], testAddrBBal[3] + 1);

   balances = getAddrBalances(bridge_, legacyWltId, legacyAccId);
   ASSERT_EQ(balances.size(), 1);
   addrBBal = balances.at(legacyAddr.hash);
   EXPECT_EQ(addrBBal[0], 11 * COIN);
   EXPECT_EQ(addrBBal[1], 0);
   EXPECT_EQ(addrBBal[2], 11 * COIN);
   EXPECT_EQ(addrBBal[3], 1);

   //check wallet ledgers
   ledgers = getLedgersPage(bridge_, delegateId, 0);
   ASSERT_EQ(ledgers.size(), 17);
   for (unsigned i = 0; i < 15; i++) {
      EXPECT_TRUE(checkLedgers(ledgers[i+2], TestChain::ledgersBCDE[i])) << i;
   }

   auto lastEntry = ledgers[1];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN + changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());

   lastEntry = ledgers[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), 11 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());

   /* address ledgers */

   //B
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrB, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 8);
   for (unsigned i = 0; i < TestChain::ledgersB.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersB[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), -20 * (int64_t)COIN);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());

   //C
   ledgersAtBlocks = getLedgersPage(bridge_, delegateScrAddrC, 0);
   ASSERT_EQ(ledgersAtBlocks.size(), 5);
   for (unsigned i = 0; i < TestChain::ledgersC.size(); i++) {
      EXPECT_TRUE(
         checkLedgers(ledgersAtBlocks[i + 1],
            TestChain::ledgersC[i]));
   }

   lastEntry = ledgersAtBlocks[0];
   ASSERT_EQ(lastEntry.getTxHash(), tx.getThisHash());
   EXPECT_EQ(lastEntry.getValue(), changeAmount);
   ASSERT_EQ(lastEntry.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(lastEntry.isOptInRBF());
   EXPECT_FALSE(lastEntry.isChainedZC());

   /* mine the tx */
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 10);
   ASSERT_EQ(waitOnNewBlock(), 15);

   //check wallet balance
   wltBal = getWalletBalance(bridge_, walletId_BCDE_, accountId_BCDE_);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], TestChain::wltBal_BCDE[5][0] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[1], TestChain::wltBal_BCDE[5][1] - (20 * COIN) + changeAmount);
   EXPECT_EQ(wltBal[2], 100 * COIN);
   EXPECT_EQ(wltBal[3], TestChain::wltBal_BCDE[5][3] + 1);

   wltBal = getWalletBalance(bridge_, legacyWltId, legacyAccId);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], 11 * COIN);
   EXPECT_EQ(wltBal[1], 11 * COIN);
   EXPECT_EQ(wltBal[2], 0ULL);
   EXPECT_EQ(wltBal[3], 1);

   /* spend from new wallet */

   //create recipient address
   auto recipientPrivKey = Cryptography::ECDSA::createNewPrivateKey();
   auto recipientPubKey = Cryptography::ECDSA::computePublicKey(
      recipientPrivKey, true);
   auto hash160 =  BtcUtils::getHash160(recipientPubKey);
   auto recipientAddr1 = BtcUtils::scrAddrToSegWitAddress(hash160);

   //change address
   auto legacyChangeAddr = getAddress(bridge_, legacyWltId, legacyAccId);

   //sign new tx
   BinaryData signedTx2;
   try {
      signedTx2 = createAndSignTx(bridge_,
         legacyWltId, legacyAccId,
         {}, {{ recipientAddr1, 5 * COIN }},
         legacyChangeAddr.hash, 2, true,
         "pass1"
      );
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }
   ASSERT_FALSE(signedTx2.empty());

   //broadcast
   broadcastTx(bridge_, signedTx2);

   //grab amount for change output
   Tx tx2(signedTx2);
   auto changeOutput2 = tx2.getTxOutCopy(1);
   uint64_t changeAmount2 = changeOutput2.getAmount();
   EXPECT_TRUE(changeAmount2 > 5 * COIN);
   EXPECT_TRUE(changeAmount2 < 6 * COIN);

   //wait on zc notif
   try {
      auto zcLedgers = waitOnZc();
      ASSERT_EQ(zcLedgers.size(), 1);
      ASSERT_EQ(zcLedgers[0].getTxHash(), tx2.getThisHash());
      EXPECT_EQ(zcLedgers[0].getValue(), -(11 * (int64_t)COIN - changeAmount2));
      ASSERT_EQ(zcLedgers[0].getBlockNum(), UINT32_MAX);
      EXPECT_TRUE(zcLedgers[0].isOptInRBF());
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //check wallet balance
   wltBal = getWalletBalance(bridge_, legacyWltId, legacyAccId);
   ASSERT_EQ(wltBal.size(), 4);
   EXPECT_EQ(wltBal[0], changeAmount2);
   EXPECT_EQ(wltBal[1], 0);
   EXPECT_EQ(wltBal[2], changeAmount2);
   EXPECT_EQ(wltBal[3], 2);
}
