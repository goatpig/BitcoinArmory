////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
   #include <winsock2.h>
   #include <windows.h>
#endif

#include <cstring>

#include "TestUtils.h"
#include <reorgTest/blkdata.h>

#include <Utils/ArmoryConfig.h>
#include <Utils/FileUtils.h>
#include <Utils/UniversalTimer.h>
#include <Wallets/IOHeader.h>
#include <Wallets/AuthorizedPeers.h>
#include <Signer/ScriptSpender.h>
#include <Network/WebSocketClient.h>
#include <Network/WebSocketMessage.h>
#include <Utils/BIP15x_Handshake.h>
#include <Network/SocketWritePayload.h>
#include <BlockchainDatabase/BlockchainData.h>
#include <ZeroConf/Parser.h>
#include <ZeroConf/Utils.h>
#include <ZeroConf/Notifications.h>
#include <BlockchainDatabase/ScrAddrFilter.h>

#include "BDM_mainthread.h"
#include "Server.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/BDV.capnp.h"

using namespace std::string_view_literals;
using namespace std::chrono_literals;
using namespace Armory;

namespace {
   std::pair<Types::BlockId, uint16_t> getTxKeyForHash(
      const Types::TxHash& txHash,
      LMDBBlockDatabase* db)
   {
      uint8_t hashTableIndex = txHash.getPtr()[8];
      auto tx = db->beginHashTableTx(
         DB_SELECT::TXHINTS, hashTableIndex, LMDB::Mode::ReadOnly);
      auto ldbIter = tx->getIterator();
      if (!ldbIter.seekToStartsWith(txHash.getSliceRef(0, 4))) {
         return { Types::INVALID_BLOCK_ID, UINT16_MAX };
      }
      auto keyRef = ldbIter.getKeyRef();
      uint64_t txHintKey;
      std::memcpy(&txHintKey, keyRef.getPtr(), 8);
      uint32_t blockID = txHintKey >> 32;

      auto valueReader = ldbIter.getValueReader();
      if (valueReader.getSizeRemaining() > 2) {
         return { Types::INVALID_BLOCK_ID, UINT16_MAX };
      }

      //blockIDs start at 1
      return { blockID - 1, valueReader.get_uint16_t() };
   }

   ////////
   /*
   hash42, tx #2 of block 4, is also tx #2 of block 5A and pays 5 btc to
   addrD. Going through 4A, then 4-5, then 4A-5A, it is mined on both
   branches, and in DB_BARE as in DB_FULL its KNOWNHASHES entry carries 2
   tx keys. Mine a block on 5A that spends that output.
   */
   void spendTxMinedOnBothBranches(BlockDataManagerThread* bdmt,
      Clients* clients, const std::filesystem::path& blk0dat)
   {
      TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat);

      clients->init();
      auto bdvID = DBTestUtils::registerBDV(
         clients, Config::BitcoinSettings::getMagicBytes());
      auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);

      DBTestUtils::registerWallet(clients, bdvID, {
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF},
         "wallet2",
         false);

      bdmt->start(Config::DBSettings::initMode());
      ASSERT_EQ(bdmt->bdm()->waitUntilReady(5s), BDMState::Ready);
      DBTestUtils::goOnline(clients, bdvID);
      DBTestUtils::waitOnBDVReady(clients, bdvID);

      auto bdm = bdmt->bdm();
      auto iface = bdm->getIFace();
      EXPECT_EQ(DBTestUtils::getTopBlockHash(iface, DB_SELECT::SCRADDR), TestChain::blkHash4A);

      //first reorg: up to 5
      TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat);
      DBTestUtils::triggerNewBlockNotification(bdmt);
      DBTestUtils::waitOnNewBlockSignal(clients, bdvID);
      EXPECT_EQ(DBTestUtils::getTopBlockHash(iface, DB_SELECT::SCRADDR), TestChain::blkHash5);

      //second reorg: up to 5A
      TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat);
      DBTestUtils::triggerNewBlockNotification(bdmt);
      DBTestUtils::waitOnNewBlockSignal(clients, bdvID);
      EXPECT_EQ(DBTestUtils::getTopBlockHash(iface, DB_SELECT::SCRADDR), TestChain::blkHash5A);
      EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrD, bdm), 60 * COIN);

      {
         //one key per branch
         auto tx = iface->beginTransaction(
            DB_SELECT::KNOWNHASHES, LMDB::Mode::ReadOnly);
         auto val = tx->get(LMDB::DataRef{
            TestChain::hash42.getSize(), TestChain::hash42.getCharPtr()});
         ASSERT_EQ(val.len, 16);
      }

      //spend it to an address unknown to our wallets
      BinaryData spenderHash;
      {
         UTXO utxo(5 * COIN, 5, 2, 0, TestChain::hash42,
            BtcUtils::getP2PKHScript(TestChain::addrD));

         Signing::Signer signer;
         auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
         feed->addPrivKey(TestChain::privKeyAddrD.getRef());
         signer.addSpender(std::make_shared<Signing::ScriptSpender>(utxo));
         signer.addRecipient(std::make_shared<Signing::Recipient_P2PKH>(
            Cryptography::PRNG::fortuna.generateRandom(20), 4 * COIN));
         signer.setFeed(feed);
         signer.sign();
         ASSERT_TRUE(signer.verify());

         auto rawTx = signer.serializeSignedTx();
         spenderHash = Tx{rawTx}.getThisHash();

         //staged: goes in the next block without going through the mempool
         DBTestUtils::ZcVector zcVec;
         zcVec.push_back(rawTx, 14000000);
         DBTestUtils::pushNewZc(bdmt, zcVec, true);
      }

      DBTestUtils::mineNewBlock(bdmt, TestChain::addrA, 1);
      DBTestUtils::waitOnNewBlockSignal(clients, bdvID);

      auto mined = DBTestUtils::getMinedBlocks(bdmt);
      ASSERT_EQ(mined.size(), 1);
      EXPECT_EQ(DBTestUtils::getTopBlockHash(iface, DB_SELECT::SCRADDR), mined.back().headerHash);

      //the spent output is the 5A one
      EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrD, bdm), 55 * COIN);

      {
         auto tx = iface->beginTransaction(
            DB_SELECT::KNOWNHASHES, LMDB::Mode::ReadOnly);
         auto val = tx->get(LMDB::DataRef{spenderHash.getSize(), spenderHash.getCharPtr()});
         EXPECT_EQ(val.len, 8);
      }

      bdvPtr.reset();
   }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BlockDir : public ::testing::Test
{
protected:
   const std::filesystem::path blkdir_  = "./blkfiletest";
   const std::filesystem::path homedir_ = "./fakehomedir";
   const std::filesystem::path ldbdir_  = "./ldbtestdir";
   std::filesystem::path blk0dat_;
   std::string wallet1id;
   std::vector<std::string> args;

   /////////////////////////////////////////////////////////////////////////////
   void cleanUp()
   {
      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);
   }

   /////////////////////////////////////////////////////////////////////////////
   void SetUp() override
   {
      cleanUp();

      FileUtils::createDirectory(blkdir_ / "blocks");
      FileUtils::createDirectory(homedir_);
      FileUtils::createDirectory(ldbdir_);

      Config::DBSettings::setServiceType(SERVICE_UNITTEST);

      args = {
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--public",
         "--db-type=DB_BARE",
         "--thread-count=3",
         "--rewind-blocks=0",
         "--public",
         "--disable-zc"};
      Config::parseArgs(args, Config::ProcessType::DB);
      DBTestUtils::init();

      blk0dat_ = FileUtils::getBlkFilename(blkdir_ / "blocks", 0);
      wallet1id = "wallet1";
   }

   /////////////////////////////////////////////////////////////////////////////
   void TearDown() override
   {
      cleanUp();
      Config::reset();

      CLEANUP_ALL_TIMERS();
   }
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockDir, HeadersFirst)
{
   const std::vector<BinaryData> scraddrs{
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   // Put the first 5 blocks out of order
   TestUtils::setBlocks({ "0", "1", "2", "4", "3", "5" }, blk0dat_);

   BlockDataManagerThread* BDMt = new BlockDataManagerThread();
   auto clients = new Clients(BDMt->bdm());
   clients->init();

   auto bdvID = DBTestUtils::registerBDV(clients, Config::BitcoinSettings::getMagicBytes());
   auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);
   DBTestUtils::registerWallet(clients, bdvID, scraddrs, "wallet1",
      false);

   BDMt->start(BdmInitMode::RESUME);
   ASSERT_EQ(BDMt->bdm()->waitUntilReady(5s), BDMState::Ready);
   DBTestUtils::goOnline(clients, bdvID);
   DBTestUtils::waitOnBDVReady(clients, bdvID);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, BDMt->bdm()), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, BDMt->bdm()), 70 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, BDMt->bdm()), 20 * COIN);

   //cleanup
   bdvPtr.reset();
   clients->shutdown();
   BDMt->shutdown();

   delete clients;
   delete BDMt;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockDir, HeadersFirstUpdate)
{
   // Put the first 5 blocks out of order
   TestUtils::setBlocks({ "0", "1", "2" }, blk0dat_);

   BlockDataManagerThread* BDMt = new BlockDataManagerThread();
   auto clients = new Clients(BDMt->bdm());
   clients->init();

   BDMt->start(BdmInitMode::RESUME);
   ASSERT_EQ(BDMt->bdm()->waitUntilReady(5s), BDMState::Ready);
   const std::vector<BinaryData> scraddrs {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   auto bdvID = DBTestUtils::registerBDV(clients, Config::BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients, bdvID, scraddrs, "wallet1",
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);

   DBTestUtils::goOnline(clients, bdvID);
   DBTestUtils::waitOnBDVReady(clients, bdvID);

   TestUtils::appendBlocks({ "4", "3", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);
   DBTestUtils::waitOnNewBlockSignal(clients, bdvID);

   // check balance from SSH
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, BDMt->bdm()), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, BDMt->bdm()), 70 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, BDMt->bdm()), 20 * COIN);

   //cleanup
   bdvPtr.reset();
   clients->shutdown();
   BDMt->shutdown();

   delete clients;
   delete BDMt;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockDir, HeadersFirstForwardUpdate)
{
   // Put the first 5 blocks out of order
   TestUtils::setBlocks({ "0", "1", "2", "4", "5", "4A" }, blk0dat_);

   BlockDataManagerThread* BDMt = new BlockDataManagerThread();
   auto clients = new Clients(BDMt->bdm());
   clients->init();

   BDMt->start(BdmInitMode::RESUME);
   ASSERT_EQ(BDMt->bdm()->waitUntilReady(5s), BDMState::Ready);
   const std::vector<BinaryData> scraddrs {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   auto bdvID = DBTestUtils::registerBDV(clients, Config::BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients, bdvID, scraddrs, "wallet1",
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);

   DBTestUtils::goOnline(clients, bdvID);
   DBTestUtils::waitOnBDVReady(clients, bdvID);

   TestUtils::appendBlocks({ "3" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);
   DBTestUtils::waitOnNewBlockSignal(clients, bdvID);

   // check balance from SSH
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, BDMt->bdm()), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, BDMt->bdm()), 70 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, BDMt->bdm()), 20 * COIN);

   //cleanup
   bdvPtr.reset();
   clients->shutdown();
   BDMt->shutdown();

   delete clients;
   delete BDMt;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockDir, HeadersFirstReorg)
{
   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
   BlockDataManagerThread* BDMt = new BlockDataManagerThread();
   auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
      Config::NetworkSettings::bitcoinNodes().first);
   nodePtr->setBDM(BDMt->bdm());
   auto clients = new Clients(BDMt->bdm());
   clients->init();

   BDMt->start(BdmInitMode::RESUME);
   ASSERT_EQ(BDMt->bdm()->waitUntilReady(5s), BDMState::Ready);
   const std::vector<BinaryData> scraddrs {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   auto bdvID = DBTestUtils::registerBDV(clients, Config::BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients, bdvID, scraddrs, "wallet1",
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);

   DBTestUtils::goOnline(clients, bdvID);
   DBTestUtils::waitOnBDVReady(clients, bdvID);

   TestUtils::appendBlocks({ "4A" }, blk0dat_);
   TestUtils::appendBlocks({ "3" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);

   std::this_thread::sleep_for(1s);
   auto blockchain = BDMt->bdm()->blockchain();
   auto top = blockchain->top();
   ASSERT_EQ(top->getBlockHeight(), 1);
   ASSERT_EQ(top->getThisHash(), TestChain::blkHash1);

   //3 and 4A should be invalid
   {
      auto invalidBlockIds = blockchain->getInvalidBlockIds();
      ASSERT_EQ(invalidBlockIds.size(), 2);
      auto blockIdIter = invalidBlockIds.begin();

      auto header4A = blockchain->getHeaderById(*blockIdIter++);
      EXPECT_EQ(header4A->getThisHash(), TestChain::blkHash4A);
      EXPECT_FALSE(header4A->isMainBranch());

      auto header3 = blockchain->getHeaderById(*blockIdIter);
      EXPECT_EQ(header3->getThisHash(), TestChain::blkHash3);
      EXPECT_FALSE(header3->isMainBranch());
   }

   TestUtils::appendBlocks({ "2" }, blk0dat_);
   TestUtils::appendBlocks({ "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);
   DBTestUtils::waitOnNewBlockSignal(clients, bdvID);
   top = blockchain->top();
   ASSERT_EQ(top->getBlockHeight(), 4);
   ASSERT_EQ(top->getThisHash(), TestChain::blkHash4A);

   //5 should be invalid
   {
      auto invalidBlockIds = blockchain->getInvalidBlockIds();
      ASSERT_EQ(invalidBlockIds.size(), 1);
      auto blockIdIter = invalidBlockIds.begin();

      auto header5 = blockchain->getHeaderById(*blockIdIter);
      EXPECT_EQ(header5->getThisHash(), TestChain::blkHash5);
      EXPECT_FALSE(header5->isMainBranch());
   }

   TestUtils::appendBlocks({ "4" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);
   DBTestUtils::waitOnNewBlockSignal(clients, bdvID);
   top = blockchain->top();
   ASSERT_EQ(top->getBlockHeight(), 5);
   ASSERT_EQ(top->getThisHash(), TestChain::blkHash5);

   //4A should be invalid
   {
      auto invalidBlockIds = blockchain->getInvalidBlockIds();
      ASSERT_EQ(invalidBlockIds.size(), 1);
      auto blockIdIter = invalidBlockIds.begin();

      auto header4A = blockchain->getHeaderById(*blockIdIter);
      EXPECT_EQ(header4A->getThisHash(), TestChain::blkHash4A);
      EXPECT_FALSE(header4A->isMainBranch());
   }

   // check balance from SSH
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, BDMt->bdm()), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, BDMt->bdm()), 70 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, BDMt->bdm()), 20 * COIN);

   TestUtils::appendBlocks({ "5A" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);
   DBTestUtils::waitOnNewBlockSignal(clients, bdvID);
   top = blockchain->top();
   ASSERT_EQ(top->getBlockHeight(), 5);
   ASSERT_EQ(top->getThisHash(), TestChain::blkHash5A);

   //4 and 5 should be invalid
   {
      auto invalidBlockIds = blockchain->getInvalidBlockIds();
      ASSERT_EQ(invalidBlockIds.size(), 2);
      auto blockIdIter = invalidBlockIds.begin();

      auto header5 = blockchain->getHeaderById(*blockIdIter++);
      EXPECT_EQ(header5->getThisHash(), TestChain::blkHash5);
      EXPECT_FALSE(header5->isMainBranch());

      auto header4 = blockchain->getHeaderById(*blockIdIter);
      EXPECT_EQ(header4->getThisHash(), TestChain::blkHash4);
      EXPECT_FALSE(header4->isMainBranch());
   }

   // check balance from SSH
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, BDMt->bdm()), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, BDMt->bdm()), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, BDMt->bdm()), 55 * COIN);

   // mine new block
   DBTestUtils::mineNewBlock(BDMt, TestChain::addrA, 1);
   DBTestUtils::waitOnNewBlockSignal(clients, bdvID);
   top = blockchain->top();
   ASSERT_EQ(top->getBlockHeight(), 6);
   ASSERT_EQ(top->getPrevHash(), TestChain::blkHash5A);
   auto blkHash6A = top->getThisHash();

   //4 and 5 should still be invalid
   {
      auto invalidBlockIds = blockchain->getInvalidBlockIds();
      ASSERT_EQ(invalidBlockIds.size(), 2);
      auto blockIdIter = invalidBlockIds.begin();

      auto header5 = blockchain->getHeaderById(*blockIdIter++);
      EXPECT_EQ(header5->getThisHash(), TestChain::blkHash5);
      EXPECT_FALSE(header5->isMainBranch());

      auto header4 = blockchain->getHeaderById(*blockIdIter);
      EXPECT_EQ(header4->getThisHash(), TestChain::blkHash4);
      EXPECT_FALSE(header4->isMainBranch());
   }

   // check balance from SSH
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, BDMt->bdm()), 100 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, BDMt->bdm()), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, BDMt->bdm()), 55 * COIN);

   //reorg back to original test chain
   auto header5 = blockchain->getHeaderByHash(TestChain::blkHash5);
   nodePtr->setReorgBranchPoint(header5);
   nodePtr->mineNewBlock(1, TestChain::addrF, 100.0);
   DBTestUtils::waitOnNewBlockSignal(clients, bdvID);
   top = blockchain->top();
   ASSERT_EQ(top->getBlockHeight(), 6);
   ASSERT_NE(top->getThisHash(), blkHash6A);
   ASSERT_EQ(top->getPrevHash(), TestChain::blkHash5);

   //4A, 5A and 6A should be invalid
   {
      auto invalidBlockIds = blockchain->getInvalidBlockIds();
      ASSERT_EQ(invalidBlockIds.size(), 3);
      auto blockIdIter = invalidBlockIds.begin();

      auto header4A = blockchain->getHeaderById(*blockIdIter++);
      EXPECT_EQ(header4A->getThisHash(), TestChain::blkHash4A);
      EXPECT_FALSE(header4A->isMainBranch());

      auto header5A = blockchain->getHeaderById(*blockIdIter++);
      EXPECT_EQ(header5A->getThisHash(), TestChain::blkHash5A);
      EXPECT_FALSE(header5A->isMainBranch());

      auto header6A = blockchain->getHeaderById(*blockIdIter);
      EXPECT_EQ(header6A->getThisHash(), blkHash6A);
      EXPECT_FALSE(header6A->isMainBranch());
   }

   //cleanup
   bdvPtr.reset();
   clients->shutdown();
   BDMt->shutdown();

   delete clients;
   delete BDMt;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockDir, HeadersFirstUpdateTwice)
{
   TestUtils::setBlocks({ "0", "1", "2" }, blk0dat_);
   BlockDataManagerThread* BDMt = new BlockDataManagerThread();
   auto clients = new Clients(BDMt->bdm());
   clients->init();

   BDMt->start(BdmInitMode::RESUME);
   ASSERT_EQ(BDMt->bdm()->waitUntilReady(5s), BDMState::Ready);
   const std::vector<BinaryData> scraddrs{
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   auto bdvID = DBTestUtils::registerBDV(clients, Config::BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients, bdvID, scraddrs, "wallet1",
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);

   DBTestUtils::goOnline(clients, bdvID);
   DBTestUtils::waitOnBDVReady(clients, bdvID);

   TestUtils::appendBlocks({ "5" }, blk0dat_);
   TestUtils::appendBlocks({ "4" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);

   TestUtils::appendBlocks({ "3" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);
   DBTestUtils::waitOnNewBlockSignal(clients, bdvID);

   // we should get the same balance as we do for test 'Load5Blocks'
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, BDMt->bdm()), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, BDMt->bdm()), 70 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, BDMt->bdm()), 20 * COIN);

   //cleanup
   bdvPtr.reset();
   clients->shutdown();
   BDMt->shutdown();

   delete clients;
   delete BDMt;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockDir, BlockFileSplit)
{
   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
   auto blk1dat = FileUtils::getBlkFilename(blkdir_ / "blocks", 1);
   TestUtils::setBlocks({ "2", "3", "4", "5" }, blk1dat);

   BlockDataManagerThread* BDMt = new BlockDataManagerThread();
   auto clients = new Clients(BDMt->bdm());
   clients->init();

   BDMt->start(BdmInitMode::RESUME);
   ASSERT_EQ(BDMt->bdm()->waitUntilReady(5s), BDMState::Ready);
   const std::vector<BinaryData> scraddrs{
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   auto bdvID = DBTestUtils::registerBDV(clients, Config::BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients, bdvID, scraddrs, "wallet1",
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);

   DBTestUtils::goOnline(clients, bdvID);
   DBTestUtils::waitOnBDVReady(clients, bdvID);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, BDMt->bdm()), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, BDMt->bdm()), 70 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, BDMt->bdm()), 20 * COIN);

   //cleanup
   bdvPtr.reset();
   clients->shutdown();
   BDMt->shutdown();

   delete clients;
   delete BDMt;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockDir, BlockFileSplitUpdate)
{
   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
   BlockDataManagerThread* BDMt = new BlockDataManagerThread();
   auto clients = new Clients(BDMt->bdm());
   clients->init();

   BDMt->start(BdmInitMode::RESUME);
   ASSERT_EQ(BDMt->bdm()->waitUntilReady(5s), BDMState::Ready);
   const std::vector<BinaryData> scraddrs{
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   auto bdvID = DBTestUtils::registerBDV(clients, Config::BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients, bdvID, scraddrs, "wallet1",
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);

   DBTestUtils::goOnline(clients, bdvID);
   DBTestUtils::waitOnBDVReady(clients, bdvID);

   auto blk1dat = FileUtils::getBlkFilename(blkdir_, 1);
   TestUtils::appendBlocks({ "2", "4", "3", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);
   DBTestUtils::waitOnNewBlockSignal(clients, bdvID);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, BDMt->bdm()), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, BDMt->bdm()), 70 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, BDMt->bdm()), 20 * COIN);

   //cleanup
   bdvPtr.reset();
   clients->shutdown();
   BDMt->shutdown();

   delete clients;
   delete BDMt;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockDir, DISABLED_FixBlockDataOffsets)
{
   #if 0
   /* 1. setup regular test, check balances */
   TestUtils::setBlocks({ "0", "1", "2", "4", "3", "5" }, blk0dat_);

   //setup BDM
   BlockDataManagerThread* BDMt = new BlockDataManagerThread();
   auto clients = new Clients(BDMt->bdm());
   clients->init();

   BDMt->start(BdmInitMode::RESUME);
   ASSERT_EQ(BDMt->bdm()->waitUntilReady(5s), BDMState::Ready);
   std::vector<BinaryData> scraddrs{
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   auto bdvID = DBTestUtils::registerBDV(clients, Config::BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients, bdvID, scraddrs, "wallet1",
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);

   DBTestUtils::goOnline(clients, bdvID);
   DBTestUtils::waitOnBDVReady(clients, bdvID);

   //check balances
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   const ScrAddrObj *scrobj;
   ASSERT_NE(wlt, nullptr);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[0]);
   EXPECT_EQ(scrobj->getFullBalance(), 50*COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[1]);
   EXPECT_EQ(scrobj->getFullBalance(), 70*COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[2]);
   EXPECT_EQ(scrobj->getFullBalance(), 20*COIN);

   //grab offset for block 3, we will mangle it in next phase of the test
   size_t block3Offset = SIZE_MAX;
   {
      auto bcPtr = BDMt->bdm()->blockchain();
      auto block3 = bcPtr->getHeaderByHeight(3);
      block3Offset = block3->getOffset();
   }
   ASSERT_NE(block3Offset, SIZE_MAX);

   //cleanup
   bdvPtr.reset();
   wlt.reset();
   BDMt->shutdown();
   clients->shutdown();
   delete clients;
   delete BDMt;
   Config::reset();

   /* 2. mangle chain data, append mangled block at the end of the file */
   {
      std::fstream fileStream{blk0dat_,
         std::ios::in | std::ios::out | std::ios::binary};
      fileStream.seekg(block3Offset + 120);
      fileStream.write("mangling the block", 18);
   }

   //setup BDM
   Config::DBSettings::setServiceType(SERVICE_UNITTEST);
   Config::parseArgs(args, Config::ProcessType::DB);
   DBTestUtils::init();
   BDMt = new BlockDataManagerThread();
   clients = new Clients(BDMt->bdm());
   clients->init();
   BDMt->start(BdmInitMode::RESUME);

   //register new address, will trigger scan and detect bad block data
   scraddrs.emplace_back(TestChain::scrAddrD);
   auto bdvID2 = DBTestUtils::registerBDV(clients, Config::BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients, bdvID2, scraddrs, "wallet2",
      false);

   auto bdvPtr2 = DBTestUtils::getBDV(clients, bdvID2);
   DBTestUtils::goOnline(clients, bdvID2);

   //BDM should warn user and shutdown gracefully
   BDMt->join();

   //cleanup
   bdvPtr2.reset();
   clients->shutdown();
   delete clients;
   delete BDMt;
   Config::reset();

   //append the correct 3rd block
   TestUtils::appendBlocks({"3"}, blk0dat_);

   /* 3. restart BDM, should fix mangled data and get through scan */
   Config::DBSettings::setServiceType(SERVICE_UNITTEST);
   Config::parseArgs(args, Config::ProcessType::DB);
   DBTestUtils::init();
   BDMt = new BlockDataManagerThread();
   clients = new Clients(BDMt->bdm());
   clients->init();
   BDMt->start(BdmInitMode::RESUME);

   scraddrs.emplace_back(TestChain::scrAddrD);
   auto bdvID3 = DBTestUtils::registerBDV(clients, Config::BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients, bdvID3, scraddrs, "wallet3",
      false);

   auto bdvPtr3 = DBTestUtils::getBDV(clients, bdvID3);
   DBTestUtils::goOnline(clients, bdvID3);
   DBTestUtils::waitOnBDVReady(clients, bdvID3);

   //check balances
   wlt = bdvPtr3->getWalletOrLockbox("wallet3");
   ASSERT_NE(wlt, nullptr);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[0]);
   EXPECT_EQ(scrobj->getFullBalance(), 50*COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[1]);
   EXPECT_EQ(scrobj->getFullBalance(), 70*COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[2]);
   EXPECT_EQ(scrobj->getFullBalance(), 20*COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[3]);
   EXPECT_EQ(scrobj->getFullBalance(), 65*COIN);

   //cleanup
   bdvPtr3.reset();
   wlt.reset();
   clients->shutdown();
   BDMt->shutdown();

   delete clients;
   delete BDMt;
   #endif
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockDir, StartAtBlkFile1)
{
   const std::vector<BinaryData> scraddrs{
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   // Put the first 5 blocks out of order
   auto blk1dat = FileUtils::getBlkFilename(blkdir_ / "blocks", 1);
   TestUtils::setBlocks({ "0", "1", "2", "4", "3", "5" }, blk1dat);

   BlockDataManagerThread* BDMt = new BlockDataManagerThread();
   auto clients = new Clients(BDMt->bdm());
   clients->init();

   auto bdvID = DBTestUtils::registerBDV(clients, Config::BitcoinSettings::getMagicBytes());
   auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);
   DBTestUtils::registerWallet(clients, bdvID, scraddrs, "wallet1",
      false);

   BDMt->start(BdmInitMode::RESUME);
   ASSERT_EQ(BDMt->bdm()->waitUntilReady(5s), BDMState::Ready);
   DBTestUtils::goOnline(clients, bdvID);
   DBTestUtils::waitOnBDVReady(clients, bdvID);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, BDMt->bdm()), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, BDMt->bdm()), 70 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, BDMt->bdm()), 20 * COIN);

   //cleanup
   bdvPtr.reset();
   clients->shutdown();
   BDMt->shutdown();

   delete clients;
   delete BDMt;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BlockUtilsBare : public ::testing::Test
{
protected:
   void initBDM()
   {
      Config::reset();
      Config::DBSettings::setServiceType(SERVICE_UNITTEST);
      Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_BARE",
         "--thread-count=3",
         "--public",
         "--disable-zc"},
         Config::ProcessType::DB);

      DBTestUtils::init();
      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();

      auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);
      nodePtr->setBDM(theBDMt_->bdm());
      clients_ = new Clients(theBDMt_->bdm());
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

      // Put the first 5 blocks into the blkdir
      blk0dat_ = FileUtils::getBlkFilename(blkdir_ / "blocks", 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);

      wallet1id = "wallet1";
      wallet2id = "wallet2";
      LB1ID = TestChain::lb1B58ID;
      LB2ID = TestChain::lb2B58ID;

      initBDM();
   }

   /////////////////////////////////////////////////////////////////////////////
   void TearDown() override
   {
      auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);

      if (clients_ != nullptr) {
         clients_->shutdown();
      }
      theBDMt_->shutdown();

      delete clients_;
      delete theBDMt_;
      clients_ = nullptr;
      theBDMt_ = nullptr;

      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);
      Config::reset();
      CLEANUP_ALL_TIMERS();
   }

   BlockDataManagerThread *theBDMt_;
   Clients* clients_;
   LMDBBlockDatabase* iface_;

   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;

   std::string wallet1id;
   std::string wallet2id;
   std::string LB1ID;
   std::string LB2ID;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsBare, Load5Blocks)
{
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   clients_->init();
   auto bdvID = DBTestUtils::registerBDV(
      clients_, Config::BitcoinSettings::getMagicBytes());
   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE,
      TestChain::scrAddrF
   };

   const std::vector<BinaryData> lb1ScrAddrs{
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const std::vector<BinaryData> lb2ScrAddrs{
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID,
      false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID,
      false);

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   auto bdm = theBDMt_->bdm();
   auto getBal = [bdm](const BinaryData& scrAddr)->uint64_t
   { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

   EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrB), 70 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 20 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrD), 65 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrE), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrF), 5 * COIN);

   EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 5 * COIN);
   EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 25 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 0 * COIN);

   //cleanup
   bdvPtr.reset();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsBare, Load5Blocks_DamagedBlkFile)
{
   // this test should be reworked to be in terms of createTestChain.py
   std::filesystem::path path(TestUtils::dataDir / "botched_block.dat");
   FileUtils::copy(path, blk0dat_);

   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   auto bdvID = DBTestUtils::registerBDV(
      clients_, Config::BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   auto bdm = theBDMt_->bdm();
   auto getBal = [bdm](const BinaryData& scrAddr)->uint64_t
   { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

   EXPECT_EQ(getBal(TestChain::scrAddrA), 100 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrB),   0 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrC),  50 * COIN);

   //cleanup
   bdvPtr.reset();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsBare, Load4Blocks_Plus2)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   clients_->init();
   auto bdvID = DBTestUtils::registerBDV(
      clients_, Config::BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE,
      TestChain::scrAddrF
   };

   const std::vector<BinaryData> lb1ScrAddrs
   {
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const std::vector<BinaryData> lb2ScrAddrs
   {
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID,
      false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID,
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::SCRADDR), 3U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::SCRADDR), TestChain::blkHash3);
   auto header = theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash3);
   EXPECT_TRUE(header->isMainBranch());

   auto bdm = theBDMt_->bdm();
   auto getBal = [bdm](const BinaryData& scrAddr)->uint64_t
   { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

   EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrB), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 55 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrD),  5 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrE), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrF),  5 * COIN);

   EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 10 * COIN);
   EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 0 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 10 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 5 * COIN);

   // Load the remaining blocks.
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::SCRADDR), 5U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::SCRADDR), TestChain::blkHash5);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash5)->isMainBranch());
   auto lastScannedRange = bdm->getLastScannedRange();
   EXPECT_EQ(lastScannedRange.first, TestChain::blkHash4);
   EXPECT_EQ(lastScannedRange.second, TestChain::blkHash5);

   EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrB), 70 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 20 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrD), 65 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrE), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrF), 5 * COIN);

   EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 5 * COIN);
   EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 25 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 0 * COIN);

   //cleanup
   bdvPtr.reset();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsBare, Load5Blocks_FullReorg)
{
   clients_->init();
   auto bdvID = DBTestUtils::registerBDV(
      clients_, Config::BitcoinSettings::getMagicBytes());
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   DBTestUtils::registerWallet(clients_, bdvID, {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
      }, "wallet1",
      false);

   DBTestUtils::registerWallet(clients_, bdvID, {
      TestChain::scrAddrD,
      TestChain::scrAddrE,
      TestChain::scrAddrF
      }, "wallet2",
      false);

   DBTestUtils::registerWallet(
      clients_, bdvID, {
         TestChain::lb1ScrAddr,
         TestChain::lb1ScrAddrP2SH
      }, TestChain::lb1B58ID,
      false);
   DBTestUtils::registerWallet(
      clients_, bdvID, {
         TestChain::lb2ScrAddr,
         TestChain::lb2ScrAddrP2SH
      }, TestChain::lb2B58ID,
      false);

   DBTestUtils::waitOnWalletRefresh(clients_, bdvID, "wallet1");
   DBTestUtils::waitOnWalletRefresh(clients_, bdvID, "wallet2");
   DBTestUtils::waitOnWalletRefresh(clients_, bdvID, TestChain::lb1B58ID);
   DBTestUtils::waitOnWalletRefresh(clients_, bdvID, TestChain::lb2B58ID);

   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::SCRADDR), 5U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::SCRADDR), TestChain::blkHash5);
   auto header5 = theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash5);
   EXPECT_TRUE(header5->isMainBranch());

   auto bdm = theBDMt_->bdm();
   auto getBal = [bdm](const BinaryData& scrAddr)->uint64_t
   { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

   EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrB), 70 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 20 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrD), 65 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrE), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrF), 5 * COIN);

   EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 5 * COIN);
   EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 25 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 0 * COIN);

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5", "4A" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);

   TestUtils::appendBlocks({ "5A" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::SCRADDR), 5U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::SCRADDR), TestChain::blkHash5A);
   auto header5A = theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash5A);
   EXPECT_TRUE(header5A->isMainBranch());
   EXPECT_FALSE(header5->isMainBranch());

   auto lastScannedRange = bdm->getLastScannedRange();
   EXPECT_EQ(lastScannedRange.first, TestChain::blkHash4A);
   EXPECT_EQ(lastScannedRange.second, TestChain::blkHash5A);

   EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrB), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 55 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrD), 60 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrE), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrF), 60 * COIN);

   EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 5 * COIN);
   EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 0 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 10 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 0 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsBare, Load5Blocks_DoubleReorg)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat_);

   clients_->init();
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   DBTestUtils::registerWallet(clients_, bdvID, {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC},
      "wallet1",
      false);

   DBTestUtils::registerWallet(clients_, bdvID, {
      TestChain::scrAddrD,
      TestChain::scrAddrE,
      TestChain::scrAddrF},
      "wallet2",
      false);

   DBTestUtils::registerWallet(clients_, bdvID, {
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH},
      TestChain::lb1B58ID,
      false);
   DBTestUtils::registerWallet(clients_, bdvID, {
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH},
      TestChain::lb2B58ID,
      false);

   //wait on signals
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::SCRADDR), TestChain::blkHash4A);

   auto bdm = theBDMt_->bdm();

   //first reorg: up to 5
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::SCRADDR), TestChain::blkHash5);

   auto lastScannedRange = bdm->getLastScannedRange();
   EXPECT_EQ(lastScannedRange.first, TestChain::blkHash4);
   EXPECT_EQ(lastScannedRange.second, TestChain::blkHash5);

   auto getBal = [bdm](const BinaryData& scrAddr)->uint64_t
   { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

   EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrB), 70 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 20 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrD), 65 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrE), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrF),  5 * COIN);

   EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 5 * COIN);
   EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 25 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 0 * COIN);

   //second reorg: up to 5A
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::SCRADDR), TestChain::blkHash5A);

   lastScannedRange = bdm->getLastScannedRange();
   EXPECT_EQ(lastScannedRange.first, TestChain::blkHash4A);
   EXPECT_EQ(lastScannedRange.second, TestChain::blkHash5A);

   EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrB), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 55 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrD), 60 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrE), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrF), 60 * COIN);

   EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 5 * COIN);
   EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 0 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 10 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 0 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsBare, DoubleReorg_SpendTxMinedOnBothBranches)
{
   spendTxMinedOnBothBranches(theBDMt_, clients_, blk0dat_);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsBare, Load5Blocks_ReloadBDM_Reorg)
{
   clients_->init();
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   DBTestUtils::registerWallet(clients_, bdvID, {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC},
      "wallet1",
      false);

   DBTestUtils::registerWallet(clients_, bdvID, {
      TestChain::scrAddrD,
      TestChain::scrAddrE,
      TestChain::scrAddrF},
      "wallet2",
      false);

   DBTestUtils::registerWallet(clients_, bdvID, {
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH},
      TestChain::lb1B58ID,
      false);
   DBTestUtils::registerWallet(clients_, bdvID, {
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH},
      TestChain::lb2B58ID,
      false);

   //wait on signals
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::SCRADDR), TestChain::blkHash5);

   //shutdown bdm
   bdvPtr.reset();
   clients_->shutdown();
   theBDMt_->shutdown();

   delete clients_;
   delete theBDMt_;

   //add the reorg blocks
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5", "4A", "5A" }, blk0dat_);

   //restart bdm
   initBDM();

   clients_->init();
   bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());
   bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   DBTestUtils::registerWallet(clients_, bdvID, {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC},
      "wallet1",
      false);

   DBTestUtils::registerWallet(clients_, bdvID, {
      TestChain::scrAddrD,
      TestChain::scrAddrE,
      TestChain::scrAddrF},
      "wallet2",
      false);

   DBTestUtils::registerWallet(clients_, bdvID, {
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH},
      TestChain::lb1B58ID,
      false);
   DBTestUtils::registerWallet(clients_, bdvID, {
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH},
      TestChain::lb2B58ID,
      false);

   //wait on signals
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::SCRADDR), TestChain::blkHash5A);

   {
      auto bdm = theBDMt_->bdm();
      auto lastScannedRange = bdm->getLastScannedRange();
      EXPECT_EQ(lastScannedRange.first, TestChain::blkHash4A);
      EXPECT_EQ(lastScannedRange.second, TestChain::blkHash5A);
   }

   auto bdm = theBDMt_->bdm();
   auto getBal = [bdm](const BinaryData& scrAddr)->uint64_t
   { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

   EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrB), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 55 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrD), 60 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrE), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrF), 60 * COIN);

   EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 5 * COIN);
   EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 0 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 10 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 0 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsBare, DISABLED_CorruptedBlock)
{
   #if 0
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);

   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);

   const std::vector<BinaryData> lb1ScrAddrs{
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const std::vector<BinaryData> lb2ScrAddrs{
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   DBTestUtils::registerWallet(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID,
      false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID,
      false);

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
   auto wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);

   {
      TestUtils::appendBlocks({ "4A", "5", "5A" }, blk0dat_);
      const uint64_t srcsz = FileUtils::getFileSize(blk0dat_);
      BinaryData temp(srcsz); {
         std::ifstream is(blk0dat_.c_str(), std::ios::in | std::ios::binary);
         is.read((char*)temp.getPtr(), srcsz);
      }

      const std::filesystem::path dst = blk0dat_;
      std::ofstream os(dst, std::ios::out | std::ios::binary);
      os.write((char*)temp.getPtr(), 100);
      os.write((char*)temp.getPtr()+120, srcsz-100-20); // erase 20 bytes
   }

   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   auto scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 70*COIN);
   EXPECT_EQ(wlt->getFullBalance(), 140*COIN);
   #endif
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsBare, Load5Blocks_RescanOps)
{
   auto startbdm = [this](BdmInitMode init)->void
   {
      clients_->init();
      auto bdvID = DBTestUtils::registerBDV(
         clients_, Config::BitcoinSettings::getMagicBytes());

      DBTestUtils::registerWallet(clients_, bdvID, {
            TestChain::scrAddrA,
            TestChain::scrAddrB,
            TestChain::scrAddrC,
            TestChain::scrAddrD,
            TestChain::scrAddrE,
            TestChain::scrAddrF},
         "wallet1",
         false);
      DBTestUtils::registerWallet(clients_, bdvID, {
            TestChain::lb1ScrAddr,
            TestChain::lb1ScrAddrP2SH},
         TestChain::lb1B58ID,
         false);
      DBTestUtils::registerWallet(clients_, bdvID, {
            TestChain::lb2ScrAddr,
            TestChain::lb2ScrAddrP2SH},
         TestChain::lb2B58ID,
         false);

      auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

      //wait on signals
      theBDMt_->start(init);
      ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
      DBTestUtils::goOnline(clients_, bdvID);
      DBTestUtils::waitOnBDVReady(clients_, bdvID);
   };

   auto checkBalance = [](std::shared_ptr<BlockDataManager> bdm)
   {
      EXPECT_EQ(bdm->blockchain()->top()->getThisHash(), TestChain::blkHash5);

      auto getBal = [bdm](const BinaryData& scrAddr)->uint64_t
      { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

      EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrB), 70 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrC), 20 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrD), 65 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrE), 30 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrF),  5 * COIN);

      EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 5 * COIN);
      EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 25 * COIN);
      EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 30 * COIN);
      EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 0 * COIN);
   };

   auto resetbdm = [this](void)->void
   {
      clients_->shutdown();
      theBDMt_->shutdown();

      delete clients_;
      delete theBDMt_;
      std::this_thread::sleep_for(1s);

      initBDM();
   };

   //regular start
   startbdm(BdmInitMode::RESUME);
   checkBalance(theBDMt_->bdm());
   auto lastScannedRange = theBDMt_->bdm()->getLastScannedRange();

   //rebuild
   resetbdm();
   startbdm(BdmInitMode::REBUILD);
   checkBalance(theBDMt_->bdm());
   lastScannedRange = theBDMt_->bdm()->getLastScannedRange();

   //regular start
   resetbdm();
   startbdm(BdmInitMode::RESUME);
   checkBalance(theBDMt_->bdm());
   lastScannedRange = theBDMt_->bdm()->getLastScannedRange();
   EXPECT_EQ(lastScannedRange.first, TestChain::blkHash5);
   EXPECT_EQ(lastScannedRange.second, TestChain::blkHash5);

   //rescan
   resetbdm();
   startbdm(BdmInitMode::RESCAN);
   checkBalance(theBDMt_->bdm());
   lastScannedRange = theBDMt_->bdm()->getLastScannedRange();
   EXPECT_EQ(lastScannedRange.first, TestChain::blkHash0);
   EXPECT_EQ(lastScannedRange.second, TestChain::blkHash5);

   //regular start
   resetbdm();
   startbdm(BdmInitMode::RESUME);
   checkBalance(theBDMt_->bdm());
   lastScannedRange = theBDMt_->bdm()->getLastScannedRange();
   EXPECT_EQ(lastScannedRange.first, TestChain::blkHash5);
   EXPECT_EQ(lastScannedRange.second, TestChain::blkHash5);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsBare, Load5Blocks_RescanEmptyDB)
{
   auto startbdm = [this](BdmInitMode init)->void
   {
      clients_->init();
      auto bdvID = DBTestUtils::registerBDV(
         clients_, Config::BitcoinSettings::getMagicBytes());

      std::vector<BinaryData> scrAddrVec {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      const std::vector<BinaryData> lb1ScrAddrs {
         TestChain::lb1ScrAddr,
         TestChain::lb1ScrAddrP2SH
      };
      const std::vector<BinaryData> lb2ScrAddrs {
         TestChain::lb2ScrAddr,
         TestChain::lb2ScrAddrP2SH
      };

      DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
         false);
      DBTestUtils::registerWallet(
         clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID,
         false);
      DBTestUtils::registerWallet(
         clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID,
         false);

      //wait on signals
      theBDMt_->start(init);
      ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
      DBTestUtils::goOnline(clients_, bdvID);
      DBTestUtils::waitOnBDVReady(clients_, bdvID);
   };

   auto checkBalance = [](std::shared_ptr<BlockDataManager> bdm)
   {
      EXPECT_EQ(bdm->blockchain()->top()->getThisHash(), TestChain::blkHash5);

      auto getBal = [bdm](const BinaryData& scrAddr)->uint64_t
      { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

      EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrB), 70 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrC), 20 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrD), 65 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrE), 30 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrF),  5 * COIN);

      EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 5 * COIN);
      EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 25 * COIN);
      EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 30 * COIN);
      EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 0 * COIN);
   };

   //start with rescan atop an empty db
   startbdm(BdmInitMode::RESCAN);
   checkBalance(theBDMt_->bdm());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsBare, Load5Blocks_RebuildEmptyDB)
{
   auto startbdm = [this](BdmInitMode init)->void
   {
      auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

      std::vector<BinaryData> scrAddrVec {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      const std::vector<BinaryData> lb1ScrAddrs {
         TestChain::lb1ScrAddr,
         TestChain::lb1ScrAddrP2SH
      };
      const std::vector<BinaryData> lb2ScrAddrs {
         TestChain::lb2ScrAddr,
         TestChain::lb2ScrAddrP2SH
      };

      DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
         false);
      DBTestUtils::registerWallet(
         clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID,
         false);
      DBTestUtils::registerWallet(
         clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID,
         false);

      auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

      //wait on signals
      theBDMt_->start(init);
      ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
      DBTestUtils::goOnline(clients_, bdvID);
      DBTestUtils::waitOnBDVReady(clients_, bdvID);
   };

   auto checkBalance = [](std::shared_ptr<BlockDataManager> bdm)
   {
      EXPECT_EQ(bdm->blockchain()->top()->getThisHash(), TestChain::blkHash5);

      auto getBal = [bdm](const BinaryData& scrAddr)->uint64_t
      { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

      EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrB), 70 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrC), 20 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrD), 65 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrE), 30 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrF),  5 * COIN);

      EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 5 * COIN);
      EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 25 * COIN);
      EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 30 * COIN);
      EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 0 * COIN);
   };

   //start with rebuild atop an empty db
   startbdm(BdmInitMode::REBUILD);
   checkBalance(theBDMt_->bdm());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsBare, Load5Blocks_SideScan)
{
   clients_->init();
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   const std::vector<BinaryData> lb1ScrAddrs {
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const std::vector<BinaryData> lb2ScrAddrs
   {
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID,
      false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID,
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   auto bdm = theBDMt_->bdm();
   auto getBal = [bdm](const BinaryData& scrAddr)->uint64_t
   { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

   EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrB), 70 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 20 * COIN);

   EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 5 * COIN);
   EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 25 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 0 * COIN);

   //post-init address registration
   scrAddrVec.clear();
   scrAddrVec.push_back(TestChain::scrAddrD);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      true);

   EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrB), 70 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 20 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrD), 65 * COIN);

   EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 5 * COIN);
   EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 25 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 0 * COIN);
}

TEST_F(BlockUtilsBare, BlockXor)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   uint64_t xorKey = UINT64_MAX;
   try {
      xorKey = TestUtils::xorTheBlockFolder(blkdir_);
      ASSERT_NE(xorKey, UINT64_MAX);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //add a file prior to both blocks and the xor file,
   //it shouldn't trip the parser
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

   //run the db
   clients_->init();
   auto bdvID = DBTestUtils::registerBDV(
      clients_, Config::BitcoinSettings::getMagicBytes());
   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE,
      TestChain::scrAddrF
   };

   const std::vector<BinaryData> lb1ScrAddrs{
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const std::vector<BinaryData> lb2ScrAddrs{
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID,
      false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID,
      false);

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::SCRADDR), 3U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::SCRADDR), TestChain::blkHash3);
   auto header = theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash3);
   EXPECT_TRUE(header->isMainBranch());

   auto bdm = theBDMt_->bdm();
   auto getBal = [bdm](const BinaryData& scrAddr)->uint64_t
   { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

   //check balances
   EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrB), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 55 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrD),  5 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrE), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrF),  5 * COIN);

   EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 10 * COIN);
   EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 0 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 10 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 5 * COIN);

   //add last 2 blocks
   FileUtils::removeDirectory(blkdir_);
   FileUtils::createDirectory(blkdir_ / "blocks");
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   try {
      auto returnedKey = TestUtils::xorTheBlockFolder(blkdir_, xorKey);
      ASSERT_EQ(returnedKey, xorKey);
   } catch (const std::exception& e) {
      ASSERT_TRUE(false) << e.what();
   }

   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::SCRADDR), 5U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::SCRADDR), TestChain::blkHash5);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash5)->isMainBranch());
   auto lastScannedRange = bdm->getLastScannedRange();
   EXPECT_EQ(lastScannedRange.first, TestChain::blkHash4);
   EXPECT_EQ(lastScannedRange.second, TestChain::blkHash5);

   //check balances
   EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrB), 70 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 20 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrD), 65 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrE), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrF), 5 * COIN);

   EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 5 * COIN);
   EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 25 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 0 * COIN);

   //pull a tx
   auto tx = DBTestUtils::getTx(TestChain::hash43, bdm);
   ASSERT_EQ(tx.getThisHash(), TestChain::hash43);

   //cleanup
   bdvPtr.reset();
}

////////
TEST_F(BlockUtilsBare, ShutdownDuringNodeSync)
{
   auto nodeRPC = std::dynamic_pointer_cast<NodeRPC_UnitTest>(
      Config::NetworkSettings::rpcNode());

   //set mocked RPC into a perpetual chain sync loop
   nodeRPC->loopOnSync = std::make_shared<std::promise<void>>();
   auto fut = nodeRPC->loopOnSync->get_future();
   theBDMt_->start(Config::DBSettings::initMode());

   //wait for mocked RPC to enter its sync loop
   fut.get();
   std::this_thread::sleep_for(2s);

   //mocked RPC will never let bdm get ready, try to shutdown anyways
   auto now = std::chrono::system_clock::now();
   theBDMt_->shutdown();
   auto diff = std::chrono::system_clock::now() - now;
   ASSERT_LE(diff, 1s);
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::InitializationFailed);
}

/* this is meant to spit out test chain data for debug purposes */
TEST_F(BlockUtilsBare, DISABLED_PPrintTestChain)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "4A", "5", "5A" }, blk0dat_);
   std::vector<std::pair<uint32_t, uint8_t>> blockIds {
      { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 1 }, { 5, 1 }
   };

   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   auto bdvID = DBTestUtils::registerBDV(
      clients_, Config::BitcoinSettings::getMagicBytes());
   auto blockchain = theBDMt_->bdm()->blockchain();

   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);
   auto bd = theBDMt_->bdm()->blockchainData();

   std::map<BinaryData, std::string> knownAddrs{
      { TestChain::scrAddrA, "scrAddrA" },
      { TestChain::scrAddrB, "scrAddrB" },
      { TestChain::scrAddrC, "scrAddrC" },
      { TestChain::scrAddrD, "scrAddrD" },
      { TestChain::scrAddrE, "scrAddrE" },
      { TestChain::scrAddrF, "scrAddrF" },
      { TestChain::lb1ScrAddr, "lb1" },
      { TestChain::lb1ScrAddrP2SH, "lb1P2SH" },
      { TestChain::lb2ScrAddr, "lb2" },
      { TestChain::lb2ScrAddrP2SH, "lb2P2SH" },

   };

   struct IdAndAmounts
   {
      std::string id;
      std::vector<std::pair<uint64_t, std::string>> amounts;
   };
   std::map<Types::TxHash, IdAndAmounts> knownTxHashes;
   for (const auto& blockId : blockIds) {
      auto headerPtr = blockchain->getHeaderByHeight(blockId.first);
      StoredHeader block;
      ASSERT_TRUE(bd->getStoredHeader(block, headerPtr, true));

      //header
      auto header = block.getBlockHeaderCopy();
      std::string hgtx = std::to_string(blockId.first) + "|" + std::to_string(blockId.second);
      std::cout << "Block #" << hgtx << ", " << header.getThisHash().toHexStr() << std::endl;
      std::cout << "   Prev: " << header.getPrevHash().toHexStr() << std::endl;
      std::cout << "   Txs: " << header.getNumTx() << std::endl;
      std::cout << "   Timestamp: " << header.getTimestamp() << std::endl << std::endl;

      //transactions
      for (unsigned y = 0; y < block.getNumTx(); y++) {
         auto tx = block.getTxCopy(y);
         std::string txId = hgtx + ":" + std::to_string(y);
         std::cout << "   * Tx [" << txId << "], " <<
            tx.getThisHash().toHexStr() << std::endl;
         std::cout << "      inputs: " << tx.getNumTxIn() <<
            ", outputs: " << tx.getNumTxOut() << std::endl << std::endl;

         //inputs
         if (y == 0) {
            std::cout << "      + Coinbase" << std::endl;
            std::cout << "         amount: 50" << std::endl;
         } else {
            for (unsigned z = 0; z < tx.getNumTxIn(); z++) {
               auto txIn = tx.getTxInCopy(z);
               std::cout << "      + TxIn #" << z << std::endl;

               auto outpoint = txIn.getOutPoint();
               auto opHash = outpoint.getTxHash();
               std::cout << "         Outpoint: ";
               try {
                  auto idAndAmounts = knownTxHashes.at(opHash);
                  auto index = outpoint.getTxOutIndex();
                  std::cout << "[" << idAndAmounts.id << "-" << index << "]" << std::endl;
                  std::cout << "         amount: " << idAndAmounts.amounts[index].first << std::endl;
                  std::cout << "         addr: " << idAndAmounts.amounts[index].second << std::endl;
               } catch (const std::out_of_range&) {
                  std::cout << opHash.toHexStr() << ", index: " << outpoint.getTxOutIndex() << std::endl;
                  std::cout << "         amount: N/A" << std::endl;
                  std::cout << "         addr: N/A" << std::endl;
               }
            }
         }

         //outputs
         std::cout << std::endl;
         std::vector<std::pair<Types::Amount, std::string>> txAmounts;
         for (unsigned z = 0; z < tx.getNumTxOut(); z++) {
            auto txOut = tx.getTxOutCopy(z);
            std::string txOutId = txId + "-" + std::to_string(z);
            std::cout << "      - TxOut [" << txOutId << "]" << std::endl;

            auto scrAddr = txOut.getScrAddress();
            std::string addrStr;
            try {
               addrStr = knownAddrs.at(scrAddr);
            } catch (const std::out_of_range&) {
               addrStr = BtcUtils::scrAddrToBase58(scrAddr);
            }

            std::cout << "         dest: " << addrStr << std::endl;
            auto amount = txOut.getAmount() / COIN;
            std::cout << "         amount: " << amount << std::endl;
            txAmounts.emplace_back(std::make_pair(amount, addrStr));
         }

         std::cout << std::endl;
         knownTxHashes.emplace(tx.getThisHash(),
            IdAndAmounts{txId, txAmounts});
      }

      std::cout << std::endl;
   }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BlockUtilsBareWithZC : public ::testing::Test
{
protected:
   void initBDM()
   {
      Config::reset();
      Config::DBSettings::setServiceType(SERVICE_UNITTEST);
      Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_BARE",
         "--thread-count=3",
         "--public"},
         Config::ProcessType::DB);

      DBTestUtils::init();
      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();

      auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);
      nodePtr->setBDM(theBDMt_->bdm());
      clients_ = new Clients(theBDMt_->bdm());
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

      // Put the first 5 blocks into the blkdir
      blk0dat_ = FileUtils::getBlkFilename(blkdir_ / "blocks", 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
      initBDM();
   }

   /////////////////////////////////////////////////////////////////////////////
   void TearDown() override
   {
      if (clients_ != nullptr) {
         clients_->shutdown();
      }
      theBDMt_->shutdown();

      delete clients_;
      delete theBDMt_;
      clients_ = nullptr;
      theBDMt_ = nullptr;

      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);
      Config::reset();
      CLEANUP_ALL_TIMERS();
   }

   BlockDataManagerThread *theBDMt_;
   Clients* clients_;
   LMDBBlockDatabase* iface_;

   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;
};

////////
TEST_F(BlockUtilsBareWithZC, ReloadMempool_ChainedZcIdAbove255)
{
   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE,
      TestChain::scrAddrF
   };

   auto startBDM = [this, &scrAddrVec](void)->Types::BdvId
   {
      clients_->init();
      auto bdvID = DBTestUtils::registerBDV(
         clients_, Config::BitcoinSettings::getMagicBytes());
      DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
         false);
      theBDMt_->start(Config::DBSettings::initMode());
      if (theBDMt_->bdm()->waitUntilReady(5s) != BDMState::Ready) {
         return UINT64_MAX;
      }
      DBTestUtils::goOnline(clients_, bdvID);
      DBTestUtils::waitOnBDVReady(clients_, bdvID);
      return bdvID;
   };

   auto bdvID = startBDM();
   auto bdm = theBDMt_->bdm();
   auto getBal = [&bdm](const BinaryData& scrAddr)->uint64_t
   { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

   EXPECT_EQ(getBal(TestChain::scrAddrC), 20 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrD), 65 * COIN);

   auto utxosC = DBTestUtils::getUTXOsForScrAddrs(bdm, { TestChain::scrAddrC });
   ASSERT_EQ(utxosC.size(), 2U);

   //parent: C to D
   BinaryData rawParent;
   {
      Signing::Signer signer;
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());

      signer.addSpender(std::make_shared<Signing::ScriptSpender>(utxosC[0]));
      signer.addRecipient(std::make_shared<Signing::Recipient_P2PKH>(
         TestChain::scrAddrD.getSliceCopy(1, 20), utxosC[0].amount - 10000));
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());
      rawParent = signer.serializeSignedTx();
   }
   Tx parentTx{rawParent};

   //child: spends the parent's output from D, to an address unknown to us
   BinaryData rawChild;
   {
      auto txOut = parentTx.getTxOutCopy(0);
      UTXO utxo(txOut.getAmount(), UINT32_MAX, 0, 0,
         parentTx.getThisHash(), txOut.getScript());

      Signing::Signer signer;
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());

      signer.addSpender(std::make_shared<Signing::ScriptSpender>(utxo));
      signer.addRecipient(std::make_shared<Signing::Recipient_P2PKH>(
         Cryptography::PRNG::fortuna.generateRandom(20), utxo.amount - 10000));
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());
      rawChild = signer.serializeSignedTx();
   }
   auto parentHash = parentTx.getThisHash();
   auto childHash = Tx{rawChild}.getThisHash();

   /*
   Persist the parent with id 255 and the child with id 256. On
   little-endian hosts the child's key sorts first as an integer: the id
   is stored big-endian inside the key.
   */
   auto persistZc = [this](const BinaryData& rawTx, Types::ZcId id)
   {
      StoredTx stx;
      stx.createFromTx(Tx{rawTx}, true, true);
      iface_->putStoredZC(stx, Types::constructZCKey(id));
   };
   persistZc(rawParent, 255);
   persistZc(rawChild, 256);

   //reload the mempool from the db
   clients_->shutdown();
   theBDMt_->shutdown();
   delete clients_;
   delete theBDMt_;
   clients_ = nullptr;
   theBDMt_ = nullptr;

   initBDM();
   bdvID = startBDM();
   bdm = theBDMt_->bdm();

   //C spent 10 btc to D, which spent it to an unknown address
   EXPECT_EQ(getBal(TestChain::scrAddrC), 10 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrD), 65 * COIN);

   //both zcs are back, the child spends from the parent
   {
      auto ss = bdm->zeroConfCont()->getSnapshot();
      ASSERT_NE(ss, nullptr);
      EXPECT_EQ(ss->getTxCount(), 2U);
      EXPECT_EQ(ss->getKeyForHash(parentHash), Types::constructZCKey(255));
      EXPECT_EQ(ss->getKeyForHash(childHash), Types::constructZCKey(256));

      auto childPtr = ss->getTxByHash(childHash);
      ASSERT_NE(childPtr, nullptr);
      ASSERT_EQ(childPtr->inputs.size(), 1U);
      EXPECT_EQ(childPtr->inputs[0].opRef.getDbKey(),
         Types::constructTxIOKeyFromTxKey(Types::constructZCKey(255), 0));
   }
}

////////
TEST_F(BlockUtilsBareWithZC, SpenderKnownHashes)
{
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   clients_->init();
   auto bdvID = DBTestUtils::registerBDV(
      clients_, Config::BitcoinSettings::getMagicBytes());
   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE,
      TestChain::scrAddrF
   };

   const std::vector<BinaryData> lb1ScrAddrs{
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const std::vector<BinaryData> lb2ScrAddrs{
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID,
      false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID,
      false);

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   auto bdm = theBDMt_->bdm();
   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(bdm.get(), DB_SELECT::HEADERS), 5U);

   auto getBal = [bdm](const BinaryData& scrAddr)->uint64_t
   { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

   EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrB), 70 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 20 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrD), 65 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrE), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrF), 5 * COIN);

   EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 5 * COIN);
   EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 25 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 30 * COIN);
   EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 0 * COIN);

   //send from C to a fresh address
   BinaryData theHash;
   {
      auto spendVal = 2 * COIN;
      Signing::Signer signer;

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());

      //get utxo list for spend value
      auto unspentVec = DBTestUtils::getUTXOsForScrAddrs(
         bdm, { TestChain::scrAddrC });

      //utxo from C
      ASSERT_EQ(unspentVec.size(), 2);
      ASSERT_EQ(unspentVec[0].amount, 10 * COIN);
      signer.addSpender(std::make_shared<Signing::ScriptSpender>(
         unspentVec[0]));

      //all to address unknown to our wallets
      auto recipientUnk = std::make_shared<Signing::Recipient_P2PKH>(
         Cryptography::PRNG::fortuna.generateRandom(20), spendVal);
      signer.addRecipient(recipientUnk);

      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      auto serializedTx = signer.serializeSignedTx();
      theHash = Tx{serializedTx}.getThisHash();
      zcVec.push_back(serializedTx, 14000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   Types::TxKey zcKey = Types::INVALID_TX_KEY;
   {
      //check the zc in in the mempool
      auto snapshot = bdm->zeroConfCont()->getSnapshot();
      ASSERT_NE(snapshot, nullptr);
      zcKey = snapshot->getKeyForHash(theHash);
      ASSERT_TRUE(Types::isTxKeyValid(zcKey));
      ASSERT_TRUE(Types::isThisAZCKey(zcKey));
   }

   //check the effect
   EXPECT_EQ(getBal(TestChain::scrAddrC), 10 * COIN);

   {
      //hash should not be in known hashes yet
      auto tx = iface_->beginTransaction(
         DB_SELECT::KNOWNHASHES, LMDB::Mode::ReadOnly);
      auto val = tx->get(LMDB::DataRef{theHash.getSize(), theHash.getCharPtr()});
      ASSERT_EQ(val.len, 0);
   }

   //mine the tx
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);
   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(bdm.get(), DB_SELECT::HEADERS), 6U);

   {
      //check the zc is out of the mempool
      auto snapshot = bdm->zeroConfCont()->getSnapshot();
      ASSERT_NE(snapshot, nullptr);
      zcKey = snapshot->getKeyForHash(theHash);
      ASSERT_FALSE(Types::isTxKeyValid(zcKey));
   }

   //check the effect
   EXPECT_EQ(getBal(TestChain::scrAddrC), 10 * COIN);

   {
      //hash should now be in known hashes
      auto tx = iface_->beginTransaction(
         DB_SELECT::KNOWNHASHES, LMDB::Mode::ReadOnly);
      auto val = tx->get(LMDB::DataRef{theHash.getSize(), theHash.getCharPtr()});
      ASSERT_EQ(val.len, 8);
   }

   //cleanup
   bdvPtr.reset();
}

////////
TEST_F(BlockUtilsBareWithZC, ReloadMempool_ZcIdAbove255)
{
   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE,
      TestChain::scrAddrF
   };

   auto startBDM = [this, &scrAddrVec](void)->Types::BdvId
   {
      clients_->init();
      auto bdvID = DBTestUtils::registerBDV(
         clients_, Config::BitcoinSettings::getMagicBytes());
      DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
         false);
      theBDMt_->start(Config::DBSettings::initMode());
      if (theBDMt_->bdm()->waitUntilReady(5s) != BDMState::Ready) {
         return UINT64_MAX;
      }
      DBTestUtils::goOnline(clients_, bdvID);
      DBTestUtils::waitOnBDVReady(clients_, bdvID);
      return bdvID;
   };

   auto restartBDM = [this, &startBDM](void)->Types::BdvId
   {
      clients_->shutdown();
      theBDMt_->shutdown();
      delete clients_;
      delete theBDMt_;
      clients_ = nullptr;
      theBDMt_ = nullptr;

      initBDM();
      return startBDM();
   };

   //spend a utxo to an address unknown to our wallets
   auto makeZc = [](const UTXO& utxo, const BinaryData& privKey)->BinaryData
   {
      Signing::Signer signer;
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(privKey.getRef());

      signer.addSpender(std::make_shared<Signing::ScriptSpender>(utxo));
      signer.addRecipient(std::make_shared<Signing::Recipient_P2PKH>(
         Cryptography::PRNG::fortuna.generateRandom(20), utxo.amount - 10000));
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());
      return signer.serializeSignedTx();
   };

   auto bdvID = startBDM();
   ASSERT_NE(bdvID, UINT64_MAX);
   auto bdm = theBDMt_->bdm();
   auto getBal = [&bdm](const BinaryData& scrAddr)->uint64_t
   { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

   EXPECT_EQ(getBal(TestChain::scrAddrB), 70 * COIN);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 20 * COIN);

   auto utxosC = DBTestUtils::getUTXOsForScrAddrs(bdm, { TestChain::scrAddrC });
   ASSERT_EQ(utxosC.size(), 2U);
   auto utxosB = DBTestUtils::getUTXOsForScrAddrs(bdm, { TestChain::scrAddrB });
   ASSERT_FALSE(utxosB.empty());

   auto rawZc1 = makeZc(utxosC[0], TestChain::privKeyAddrC);
   auto rawZc2 = makeZc(utxosC[1], TestChain::privKeyAddrC);
   auto rawZc3 = makeZc(utxosB[0], TestChain::privKeyAddrB);
   auto hash1 = Tx{rawZc1}.getThisHash();
   auto hash2 = Tx{rawZc2}.getThisHash();
   auto hash3 = Tx{rawZc3}.getThisHash();

   /*
   Persist 2 zcs with ids 255 and 256, as a db that has been running
   for a while would have them. On little-endian hosts their keys sort
   the other way around as integers: the id is stored big-endian inside
   the key.
   */
   auto persistZc = [this](const BinaryData& rawTx, Types::ZcId id)
   {
      StoredTx stx;
      stx.createFromTx(Tx{rawTx}, true, true);
      iface_->putStoredZC(stx, Types::constructZCKey(id));
   };
   persistZc(rawZc1, 255);
   persistZc(rawZc2, 256);

   //reload the mempool from the db
   bdvID = restartBDM();
   ASSERT_NE(bdvID, UINT64_MAX);
   bdm = theBDMt_->bdm();
   {
      auto zcCont = bdm->zeroConfCont();
      auto ss = zcCont->getSnapshot();
      ASSERT_NE(ss, nullptr);
      EXPECT_EQ(ss->getTxCount(), 2U);
      EXPECT_EQ(ss->getKeyForHash(hash1), Types::constructZCKey(255));
      EXPECT_EQ(ss->getKeyForHash(hash2), Types::constructZCKey(256));

      //next id has to be above the highest id in use
      EXPECT_EQ(zcCont->getTopZcID(), 257U);
   }
   EXPECT_EQ(getBal(TestChain::scrAddrC), 0U);

   //push a new zc, it should not take the key of a live one
   {
      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(rawZc3, 14000000);
      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }
   {
      auto ss = bdm->zeroConfCont()->getSnapshot();
      ASSERT_NE(ss, nullptr);
      EXPECT_EQ(ss->getTxCount(), 3U);
      EXPECT_EQ(ss->getKeyForHash(hash3), Types::constructZCKey(257));

      auto tx2 = ss->getTxByHash(hash2);
      ASSERT_NE(tx2, nullptr);
      EXPECT_EQ(tx2->getTxHash(), hash2);
      EXPECT_NE(tx2->getTxHash(), hash3);
   }
   EXPECT_EQ(getBal(TestChain::scrAddrB), 70 * COIN - utxosB[0].amount);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 0U);

   /*
   The zc is written to the db on its own thread (updateZCinDB), and the
   new zc notification doesn't wait on that write. Wait for it to land
   before restarting: shutdown drops the writes that are still queued.
   */
   {
      auto key3 = bdm->zeroConfCont()->getSnapshot()->getKeyForHash(hash3);
      bool inDb = false;
      for (unsigned i = 0; i < 500 && !inDb; i++) {
         StoredTx stx;
         inDb = iface_->getStoredZC(stx, key3) &&
            stx.getSerializedTx() == rawZc3;
         if (!inDb) {
            std::this_thread::sleep_for(10ms);
         }
      }
      ASSERT_TRUE(inDb);
   }

   //all 3 zcs should survive another restart
   bdvID = restartBDM();
   ASSERT_NE(bdvID, UINT64_MAX);
   bdm = theBDMt_->bdm();
   {
      auto zcCont = bdm->zeroConfCont();
      auto ss = zcCont->getSnapshot();
      ASSERT_NE(ss, nullptr);
      EXPECT_EQ(ss->getTxCount(), 3U);
      EXPECT_TRUE(ss->hasHash(hash1));
      EXPECT_TRUE(ss->hasHash(hash2));
      EXPECT_TRUE(ss->hasHash(hash3));
      EXPECT_EQ(zcCont->getTopZcID(), 258U);
   }
   EXPECT_EQ(getBal(TestChain::scrAddrB), 70 * COIN - utxosB[0].amount);
   EXPECT_EQ(getBal(TestChain::scrAddrC), 0U);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BlockUtilsBareWithServer : public ::testing::Test
{
protected:
   void initBDM()
   {
      theBDMt_ = new BlockDataManagerThread();
      auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);
      nodePtr->setBDM(theBDMt_->bdm());
   }

   void startWSServer(std::shared_ptr<Clients> clientsPtr)
   {
      wsServerThr_ = std::thread([bdm=theBDMt_->bdm()]
         (std::shared_ptr<Clients> clientsPtr){
            if (clientsPtr == nullptr) {
               clientsPtr = std::make_shared<Clients>(bdm);
            }
            WebSocketServer::start(clientsPtr);
         }, clientsPtr
      );

      //arbitrary wait to give time to WS server to startup
      std::this_thread::sleep_for(100ms);
   }

   /////////////////////////////////////////////////////////////////////////////
   void SetUp() override
   {
      startupBIP151CTX();
      startupBIP150CTX(4);

      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);

      FileUtils::createDirectory(blkdir_ / "blocks");
      FileUtils::createDirectory(homedir_);
      FileUtils::createDirectory(ldbdir_);

      Config::reset();
      Config::DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);
      Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_BARE",
         "--thread-count=3",
         "--public",
         "--disable-zc"},
         Config::ProcessType::DB);
      DBTestUtils::init();

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

      std::string serverPubkey;
      {
         auto serverPeersWlt = NetworkPeers::PeerStore::bootstrapWallet({
            homedir_ / SERVER_AUTH_PEER_FILENAME, {createWltLbd}});
         NetworkPeers::ServerStore serverPeers(serverPeersWlt);
         NetworkPeers::PeerKey servPK{
            serverPeers.getOwnPublicKey(),
            NetworkPeers::PeerType::ServerOneWay};
         serverPubkey = servPK.toHumanReadable();
      }

      {
         auto clientPeersWlt = NetworkPeers::PeerStore::bootstrapWallet({
            homedir_ / CLIENT_AUTH_PEER_FILENAME, {createWltLbd}});
         NetworkPeers::ClientStore clientPeers(clientPeersWlt);
         clientPeers.addPeer(
            NetworkPeers::PeerKey::fromHumanReadable(serverPubkey), {
               std::format("127.0.0.1:{}", Config::NetworkSettings::dbPort())
            }, {});
      }

      // Put the first 5 blocks into the blkdir
      blk0dat_ = FileUtils::getBlkFilename(blkdir_ / "blocks", 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
      WebSocketServer::init();
      initBDM();
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
      CLEANUP_ALL_TIMERS();
   }

   /////////////////////////////////////////////////////////////////////////////
   Passphrase::UnlockFunc authPeersPassLbd_;
   BlockDataManagerThread *theBDMt_;
   std::thread wsServerThr_;

   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;
};

TEST_F(BlockUtilsBareWithServer, ShutdownWithPendingClient)
{
   /*
   Clients waiting on bdm ready should not block the server
   shutdown sequence
   */
   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer(nullptr);

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", Config::NetworkSettings::dbPort(),
      std::make_shared<NetworkPeers::ClientStore>(Wallets::IO::ReadOnlyFileParams{
         homedir_ / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
      true, //public server
      pCallback);
   bdvObj->connectToRemote();
   bdvObj->registerWithDB(Config::BitcoinSettings::getMagicBytes().toHexStr());
   AsyncClient::BtcWallet walletObj{*bdvObj, "wallet1"};
   walletObj.registerAddresses(scrAddrVec, false);

   //go online without starting the bdm
   bdvObj->goOnline();

   //shutdown the server, it shouldn't hang
   std::this_thread::sleep_for(1s);
   WebSocketServer::shutdown();
   if (wsServerThr_.joinable()) {
      wsServerThr_.join();
   }
}

TEST_F(BlockUtilsBareWithServer, WSClientCleanup)
{
   /*
   Demonstrate a WebSocketClient can release its pending
   requests on disconnect
   */

   //start the WS server
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   auto clients = std::make_shared<Clients>(theBDMt_->bdm());
   startWSServer(clients);

   //start bdm
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);

   //connect to server
   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", Config::NetworkSettings::dbPort(),
      std::make_shared<NetworkPeers::ClientStore>(Wallets::IO::ReadOnlyFileParams{
         homedir_ / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
      true, //public server
      pCallback);

   bdvObj->connectToRemote();
   bdvObj->registerWithDB(Config::BitcoinSettings::getMagicBytes().toHexStr());
   bdvObj->goOnline();
   auto readyNotif = pCallback->waitOnNotification(
      DBTestUtils::UTCallback::BDMAction_Ready);
   ASSERT_NE(readyNotif, nullptr);
   ASSERT_EQ(readyNotif->action, DBTestUtils::UTCallback::BDMAction_Ready);
   ASSERT_EQ(readyNotif->newTop.getHeight(), 5);

   //grab id for our bdv
   Types::BdvId bdvId;
   {
      auto bdvMap = clients->getBdvIds();
      ASSERT_EQ(bdvMap.size(), 1);
      bdvId = *bdvMap.begin();
   }

   DBTestUtils::BdvPayloadNinja ninja{clients, bdvId};
   ninja.beginPayloadCapture();

   /*
   The payload ninja diverts all payloads for our BDV to its own queue.
   We elect to process none of these payload, leaving them hanging forever
   */
   using TimePoint = std::chrono::time_point<std::chrono::system_clock>;
   std::promise<std::pair<bool, TimePoint>> prom1, prom2, prom3, prom4;
   auto fut1 = prom1.get_future();
   auto fut2 = prom2.get_future();
   auto fut3 = prom3.get_future();
   auto fut4 = prom4.get_future();

   const auto& socketName = bdvObj->getSocketName();
   bdvObj->getTxsByHash({READHEX("abcd"), READHEX("ef01")},
      [&prom1, socketName](ReturnMessage<AsyncClient::TxBatchResult> result){
         try {
            result.get();
            prom1.set_value({true, std::chrono::system_clock::now()});
         } catch (const ClientMessageError& e) {
            EXPECT_EQ(e.what(), std::format("{} was disconnected", socketName));
            prom1.set_value({false, std::chrono::system_clock::now()});
         }
      });

   bdvObj->getTxsByKey({0, 1, 2},
      [&prom2, socketName](ReturnMessage<std::vector<Tx>> result){
         try {
            result.get();
            prom2.set_value({true, std::chrono::system_clock::now()});
         } catch (const ClientMessageError& e) {
            EXPECT_EQ(e.what(), std::format("{} was disconnected", socketName));
            prom2.set_value({false, std::chrono::system_clock::now()});
         }
      });

   std::mutex cvMutex;
   std::condition_variable cv;
   bdvObj->getTxios(0,
      [&prom3, &prom4, &cvMutex, &cv, socketName](ReturnMessage<std::vector<TxIOPair>> result){
         std::unique_lock<std::mutex> lock{cvMutex};
         try {
            result.get();
            prom3.set_value({true, std::chrono::system_clock::now()});
         } catch (const ClientMessageError& e) {
            EXPECT_EQ(e.what(), std::format("{} was disconnected", socketName));
            prom3.set_value({false, std::chrono::system_clock::now()});
         }

         //we deliberately block this callback by waiting on the condvar
         auto cvStatus = cv.wait_for(lock, 5s);
         prom4.set_value({
            cvStatus == std::cv_status::no_timeout,
            std::chrono::system_clock::now()});
      });

   //none of the requests should be fulfilled yet
   ASSERT_EQ(fut1.wait_for(0s), std::future_status::timeout);
   ASSERT_EQ(fut2.wait_for(0s), std::future_status::timeout);
   ASSERT_EQ(fut3.wait_for(0s), std::future_status::timeout);

   //squeeze in a wait for the good measure
   std::this_thread::sleep_for(3s);

   //still no request should be fulfilled yet
   ASSERT_EQ(fut1.wait_for(0s), std::future_status::timeout);
   ASSERT_EQ(fut2.wait_for(0s), std::future_status::timeout);
   ASSERT_EQ(fut3.wait_for(0s), std::future_status::timeout);

   //cleanup the server, the calls should release
   auto now = std::chrono::system_clock::now();
   WebSocketServer::shutdown();

   ASSERT_EQ(fut1.wait_for(3s), std::future_status::ready);
   auto result1 = fut1.get();
   //request should have failed
   EXPECT_EQ(result1.first, false);
   //and that should have happened after the server shutdown
   EXPECT_GT(result1.second, now);

   ASSERT_EQ(fut2.wait_for(3s), std::future_status::ready);
   auto result2 = fut2.get();
   EXPECT_EQ(result2.first, false);
   EXPECT_GT(result2.second, now);

   ASSERT_EQ(fut3.wait_for(3s), std::future_status::ready);
   auto result3 = fut3.get();
   EXPECT_EQ(result3.first, false);
   EXPECT_GT(result3.second, now);

   //when fut3 has a result, we know the 3rd request is waiting on the cv
   //we want to use this window to push a new request to the client, which
   //should correctly fail to queue a callback
   auto afterCVnow = std::chrono::system_clock::now();
   try {
      bdvObj->getTxios(0, nullptr);
      ASSERT_TRUE(false);
   } catch (const LWS_Error& e) {
      EXPECT_EQ(e.what(), std::string{"lws client down"});
   }

   //release the cv and consume the last future
   cv.notify_all();
   ASSERT_EQ(fut4.wait_for(3s), std::future_status::ready);
   auto result4 = fut4.get();
   EXPECT_EQ(result4.first, true);
   EXPECT_GT(result4.second, afterCVnow);

   //join on the server cleanup before exiting
   ninja.stopPayloadCapture();
   if (wsServerThr_.joinable()) {
      wsServerThr_.join();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsBareWithServer, WSClientCleanup_AnsweredRequest)
{
   /*
   A request that got its reply should not be released again
   when the socket drops while its callback runs
   */

   //the first call blocks in the callback until released, a second call
   //flags itself
   std::atomic<unsigned> callCount{0};
   std::promise<void> firstCallProm, secondCallProm;
   auto firstCallFut = firstCallProm.get_future();
   auto secondCallFut = secondCallProm.get_future();
   std::mutex cvMutex;
   std::condition_variable cv;
   bool release = false;

   //start the WS server
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   startWSServer(nullptr);

   //start bdm
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);

   //connect to server
   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", Config::NetworkSettings::dbPort(),
      std::make_shared<NetworkPeers::ClientStore>(Wallets::IO::ReadOnlyFileParams{
         homedir_ / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
      true, //public server
      pCallback);

   bdvObj->connectToRemote();
   bdvObj->registerWithDB(Config::BitcoinSettings::getMagicBytes().toHexStr());
   bdvObj->goOnline();
   pCallback->waitOnNotification(DBTestUtils::UTCallback::BDMAction_Ready);

   bdvObj->getTxios(0,
      [&](ReturnMessage<std::vector<TxIOPair>>) {
         auto count = callCount.fetch_add(1) + 1;
         if (count == 1) {
            firstCallProm.set_value();
            std::unique_lock<std::mutex> lock{cvMutex};
            cv.wait_for(lock, 10s, [&release]{ return release; });
         } else if (count == 2) {
            secondCallProm.set_value();
         }
      });

   //the reply is in and its callback is running
   ASSERT_EQ(firstCallFut.wait_for(5s), std::future_status::ready);

   //drop the socket while the callback runs
   WebSocketServer::shutdown();

   //the request was answered, the disconnect should not release it again
   EXPECT_EQ(secondCallFut.wait_for(3s), std::future_status::timeout);

   //release the callback
   {
      std::unique_lock<std::mutex> lock{cvMutex};
      release = true;
   }
   cv.notify_all();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
namespace {
   BinaryData zeroedBytes(size_t size)
   {
      return BinaryData(std::vector<uint8_t>(size, 0));
   }

   /*
   Bare websocket client: completes the websocket handshake with the server
   and writes a single frame of our choosing, without going through the AEAD
   layer the regular client enforces. With keepWriting, it writes that frame
   again every 10ms for as long as the connection is open.
   */
   class RawWsClient
   {
   private:
      struct lws_context* context_ = nullptr;
      std::thread serviceThr_;
      std::atomic<bool> run_{true};

      BinaryData frame_;
      bool written_ = false;
      std::promise<bool> writtenProm_;

      const bool keepWriting_;
      struct lws* wsi_ = nullptr;
      std::atomic<bool> closed_{false};

   private:
      static int callback(struct lws* wsi, enum lws_callback_reasons reason,
         void* user, void*, size_t)
      {
         auto instance = (RawWsClient*)user;
         if (instance == nullptr) {
            return 0;
         }

         switch (reason)
         {
            case LWS_CALLBACK_CLIENT_ESTABLISHED:
               instance->wsi_ = wsi;
               lws_callback_on_writable(wsi);
               break;

            case LWS_CALLBACK_CLIENT_WRITEABLE:
            {
               if (instance->written_ && !instance->keepWriting_) {
                  break;
               }
               auto size = instance->frame_.getSize() - LWS_PRE;
               auto result = lws_write(wsi,
                  instance->frame_.getPtr() + LWS_PRE, size, LWS_WRITE_BINARY);
               if (!instance->written_) {
                  instance->written_ = true;
                  instance->writtenProm_.set_value(result == (int)size);
               }
               break;
            }

            case LWS_CALLBACK_CLIENT_CLOSED:
               instance->wsi_ = nullptr;
               instance->closed_.store(true);
               break;

            case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            {
               if (!instance->written_) {
                  instance->written_ = true;
                  instance->writtenProm_.set_value(false);
               }
               break;
            }

            default:
               break;
         }
         return 0;
      }

   public:
      RawWsClient(const BinaryData& frame, bool keepWriting = false) :
         keepWriting_(keepWriting)
      {
         frame_.resize(LWS_PRE);
         frame_.append(frame);
      }

      ~RawWsClient()
      {
         run_.store(false);
         if (context_ != nullptr) {
            lws_cancel_service(context_);
         }
         if (serviceThr_.joinable()) {
            serviceThr_.join();
         }
         if (context_ != nullptr) {
            lws_context_destroy(context_);
         }
      }

      static lws_log_cx_t* logContext()
      {
         /*
         lws counts the contexts and connections bound to a logging
         context, without a lock unless it is built with LWS_MAX_SMP > 1.
         The server uses the default one from its own thread: give the raw
         clients theirs, so the two threads never update the same count.
         The raw clients run one at a time.
         */
         static lws_log_cx_t cx = []() {
            lws_log_cx_t result{};
            result.u.emit = lwsl_emit_stderr;
            result.lll_flags = LLL_ERR | LLL_WARN | LLL_NOTICE;
            return result;
         }();
         return &cx;
      }

      bool write(uint16_t port, std::chrono::milliseconds timeout)
      {
         static struct lws_protocols protocols[] = {
            { "armory-bdm-protocol", RawWsClient::callback,
               sizeof(void*), 8000, 0, nullptr, 0 },
            { nullptr, nullptr, 0, 0, 0, nullptr, 0 }
         };

         struct lws_context_creation_info info;
         memset(&info, 0, sizeof(info));
         info.port = CONTEXT_PORT_NO_LISTEN;
         info.protocols = protocols;
         info.gid = -1;
         info.uid = -1;
         info.log_cx = logContext();
         context_ = lws_create_context(&info);
         if (context_ == nullptr) {
            return false;
         }

         struct lws_client_connect_info connInfo;
         memset(&connInfo, 0, sizeof(connInfo));
         connInfo.context = context_;
         connInfo.address = "127.0.0.1";
         connInfo.port = port;
         connInfo.path = "/";
         connInfo.host = connInfo.address;
         connInfo.origin = connInfo.address;
         connInfo.ietf_version_or_minus_one = -1;
         connInfo.protocol = protocols[0].name;
         connInfo.userdata = this;
         if (lws_client_connect_via_info(&connInfo) == nullptr) {
            return false;
         }

         auto fut = writtenProm_.get_future();
         serviceThr_ = std::thread([this]{
            while (run_.load()) {
               if (keepWriting_ && written_ && wsi_ != nullptr) {
                  std::this_thread::sleep_for(10ms);
                  lws_callback_on_writable(wsi_);
               }
               lws_service(context_, 50);
            }
         });

         if (fut.wait_for(timeout) != std::future_status::ready) {
            return false;
         }
         return fut.get();
      }

      //true if the server closed the connection within the timeout
      bool waitClosed(std::chrono::milliseconds timeout)
      {
         auto deadline = std::chrono::steady_clock::now() + timeout;
         while (!closed_.load()) {
            if (std::chrono::steady_clock::now() >= deadline) {
               return false;
            }
            std::this_thread::sleep_for(10ms);
         }
         return true;
      }
   };

   //collects error notifications so tests can wait on them with a timeout
   class ErrorCatcher : public DBTestUtils::UTCallback
   {
   private:
      mutable Threading::TimedQueue<std::shared_ptr<BdvError>> errors_;

   public:
      void pushError(const BdvError& error) const override
      {
         errors_.push_back(std::make_shared<BdvError>(error));
         DBTestUtils::UTCallback::pushError(error);
      }

      std::shared_ptr<BdvError> nextError(std::chrono::milliseconds timeout)
      {
         try {
            return errors_.pop_front(timeout);
         } catch (const Threading::StackTimedOutException&) {
            return nullptr;
         }
      }
   };
}

////////////////////////////////////////////////////////////////////////////////
class BlockUtilsBareWithServerDeathTest : public BlockUtilsBareWithServer
{
   /*
   Scenarios that take the whole process down when they fail: they run in a
   death test child, which sets the fixture up and tears it down on its own,
   and has to exit cleanly to pass.
   */

protected:
   void SetUp() override
   {
      GTEST_FLAG_SET(death_test_style, "threadsafe");
   }

   void TearDown() override
   {}

   void runScenario(const std::function<void(void)>& scenario)
   {
      //a hung scenario fails instead of blocking the suite
      std::thread watchdog([]{
         std::this_thread::sleep_for(30s);
         std::_Exit(3);
      });
      watchdog.detach();

      BlockUtilsBareWithServer::SetUp();
      scenario();
      BlockUtilsBareWithServer::TearDown();
      std::_Exit(::testing::Test::HasFailure() ? 1 : 0);
   }

   std::shared_ptr<AsyncClient::BlockDataViewer> connectClient(
      std::shared_ptr<DBTestUtils::UTCallback> callback)
   {
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<NetworkPeers::ClientStore>(
            Wallets::IO::ReadOnlyFileParams{
               homedir_ / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, //public server
         callback);
      bdvObj->connectToRemote();
      return bdvObj;
   }

   //true if the node status request got a valid reply in time
   bool nodeStatusReplies(AsyncClient::BlockDataViewer& bdvObj)
   {
      auto prom = std::make_shared<std::promise<bool>>();
      auto fut = prom->get_future();
      bdvObj.getNodeStatus([prom](ReturnMessage<Node::Status> result){
         try {
            result.get();
            prom->set_value(true);
         } catch (const std::exception&) {
            prom->set_value(false);
         }
      });

      if (fut.wait_for(5s) != std::future_status::ready) {
         return false;
      }
      return fut.get();
   }

   struct RawReply
   {
      bool success = false;
      std::string error;
      uint32_t value = UINT32_MAX;
   };

   /*
   Sends a request payload as is and hands back the reply, inspect pulls a
   value out of it
   */
   std::future<RawReply> sendRawRequest(
      AsyncClient::BlockDataViewer& bdvObj, std::vector<uint8_t> data,
      std::function<uint32_t(Codec::BDV::Reply::Reader)> inspect = nullptr)
   {
      class ReplyCallback : public Network::CallbackReturn_WebSocket
      {
      private:
         std::shared_ptr<std::promise<RawReply>> prom_;
         std::function<uint32_t(Codec::BDV::Reply::Reader)> inspect_;

      public:
         ReplyCallback(std::shared_ptr<std::promise<RawReply>> prom,
            std::function<uint32_t(Codec::BDV::Reply::Reader)> inspect) :
            prom_(prom), inspect_(inspect)
         {}

         void callback(const Network::WebSocketMessagePartial& msg) override
         {
            RawReply result;
            try {
               auto msgReader = msg.getReader();
               auto capnReader = msgReader->getReader();
               auto reply = capnReader->getRoot<Codec::BDV::Reply>();
               result.success = reply.getSuccess();
               result.error = std::string(reply.getError());
               if (inspect_) {
                  result.value = inspect_(reply);
               }
            } catch (const std::exception& e) {
               result.success = false;
               result.error = std::string{"unreadable reply: "} + e.what();
            }
            prom_->set_value(std::move(result));
         }
      };

      auto prom = std::make_shared<std::promise<RawReply>>();
      auto readPayload = std::make_shared<Network::Socket_ReadPayload>();
      readPayload->callbackReturn_ =
         std::make_unique<ReplyCallback>(prom, inspect);
      bdvObj.getSocketObject()->pushPayload(
         std::make_unique<Network::WritePayload_Raw>(data), readPayload);
      return prom->get_future();
   }

   static std::vector<uint8_t> toBytes(capnp::MallocMessageBuilder& message)
   {
      auto flat = capnp::messageToFlatArray(message);
      auto bytes = flat.asBytes();
      return std::vector<uint8_t>(bytes.begin(), bytes.end());
   }

   /*
   Not a capnp message: the segment table claims more segments than
   there is data for
   */
   static std::vector<uint8_t> malformedRequest()
   {
      std::vector<uint8_t> data(16, 0);
      data[0] = 0xFF;
      data[1] = 0xFF;
      data[2] = 0xFF;
      data[3] = 0x7F;
      return data;
   }
};

////
TEST_F(BlockUtilsBareWithServerDeathTest, ShortFrameBeforeHandshake)
{
   /*
   A frame too short to carry a packet type, sent before the AEAD
   handshake, should get its connection dropped without taking the
   server down
   */
   EXPECT_EXIT(runScenario([this]{
      WebSocketServer::initPeerStore({
         homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
      startWSServer(nullptr);

      for (unsigned size=1; size<5; size++) {
         RawWsClient rawClient(zeroedBytes(size));
         ASSERT_TRUE(rawClient.write(Config::NetworkSettings::dbPort(), 5s));

         //give the server time to process the frame before disconnecting
         std::this_thread::sleep_for(200ms);
      }

      //the server still serves regular clients
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = connectClient(pCallback);
      bdvObj->registerWithDB(
         Config::BitcoinSettings::getMagicBytes().toHexStr());
      EXPECT_TRUE(nodeStatusReplies(*bdvObj));
   }), ::testing::ExitedWithCode(0), "");
}

////
TEST_F(BlockUtilsBareWithServerDeathTest, DroppedConnection_SocketClosed)
{
   /*
   A connection the server drops gets its socket closed by the server: a
   short frame, a handshake packet of a type the server does not take and
   a request sent ahead of the encryption handshake each end the connection
   */
   EXPECT_EXIT(runScenario([this]{
      WebSocketServer::initPeerStore({
         homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
      startWSServer(nullptr);

      //serialized packets start with LWS_PRE bytes of room for lws
      auto toWire = [](const BinaryData& packet)->BinaryData
      {
         return packet.getSliceCopy(LWS_PRE, packet.getSize() - LWS_PRE);
      };

      std::vector<BinaryData> frames {
         zeroedBytes(1),
         toWire(Network::WebSocketMessageCodec::serializePacketWithoutId(
            zeroedBytes(33).getRef(), nullptr,
            ArmoryAEAD::BIP151_PayloadType::PresentPubKey)[0]),
         toWire(Network::WebSocketMessageCodec::serialize(
            zeroedBytes(33).getRef(), nullptr,
            ArmoryAEAD::BIP151_PayloadType::SinglePacket, 1)[0])
      };

      for (unsigned i=0; i<frames.size(); i++) {
         RawWsClient rawClient(frames[i]);
         ASSERT_TRUE(rawClient.write(
            Config::NetworkSettings::dbPort(), 5s)) << "frame #" << i;
         EXPECT_TRUE(rawClient.waitClosed(5s)) << "frame #" << i;
      }

      //the server still serves regular clients
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = connectClient(pCallback);
      bdvObj->registerWithDB(
         Config::BitcoinSettings::getMagicBytes().toHexStr());
      EXPECT_TRUE(nodeStatusReplies(*bdvObj));
   }), ::testing::ExitedWithCode(0), "");
}

////
TEST_F(BlockUtilsBareWithServerDeathTest, DroppedConnection_KeepsSending)
{
   /*
   A dropped connection that keeps sending frames gets its socket closed
   all the same, so nothing it sends afterwards is queued. A client
   connected next to it is served all along.
   */
   EXPECT_EXIT(runScenario([this]{
      WebSocketServer::initPeerStore({
         homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
      startWSServer(nullptr);

      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = connectClient(pCallback);
      bdvObj->registerWithDB(
         Config::BitcoinSettings::getMagicBytes().toHexStr());

      RawWsClient rawClient(zeroedBytes(1), true);
      ASSERT_TRUE(rawClient.write(Config::NetworkSettings::dbPort(), 5s));
      EXPECT_TRUE(nodeStatusReplies(*bdvObj));
      EXPECT_TRUE(rawClient.waitClosed(5s));
      EXPECT_TRUE(nodeStatusReplies(*bdvObj));
   }), ::testing::ExitedWithCode(0), "");
}

////
TEST_F(BlockUtilsBareWithServerDeathTest, DroppedConnection_ClientLeaves)
{
   /*
   Raw clients that send a frame that gets them dropped and leave right
   away: the connection may be gone by the time the server gets to close
   it, the server skips it and keeps serving
   */
   EXPECT_EXIT(runScenario([this]{
      WebSocketServer::initPeerStore({
         homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
      startWSServer(nullptr);

      for (unsigned i=0; i<300; i++) {
         RawWsClient rawClient(zeroedBytes(1), i % 2 == 1);
         ASSERT_TRUE(rawClient.write(
            Config::NetworkSettings::dbPort(), 5s)) << "iter #" << i;
         if (i % 3 == 0) {
            std::this_thread::sleep_for(
               std::chrono::microseconds(200 * (i % 7)));
         }
      }

      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = connectClient(pCallback);
      bdvObj->registerWithDB(
         Config::BitcoinSettings::getMagicBytes().toHexStr());
      EXPECT_TRUE(nodeStatusReplies(*bdvObj));
   }), ::testing::ExitedWithCode(0), "");
}

////
TEST_F(BlockUtilsBareWithServerDeathTest, MalformedRequest_RegisteredBdv)
{
   /*
   A request from a registered client that fails to parse gets an error
   reply, and the client's next request is processed
   */
   EXPECT_EXIT(runScenario([this]{
      WebSocketServer::initPeerStore({
         homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
      startWSServer(nullptr);

      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = connectClient(pCallback);
      bdvObj->registerWithDB(
         Config::BitcoinSettings::getMagicBytes().toHexStr());

      auto fut = sendRawRequest(*bdvObj, malformedRequest());
      ASSERT_EQ(fut.wait_for(5s), std::future_status::ready);
      auto reply = fut.get();
      EXPECT_FALSE(reply.success);
      EXPECT_FALSE(reply.error.empty());

      EXPECT_TRUE(nodeStatusReplies(*bdvObj));
   }), ::testing::ExitedWithCode(0), "");
}

////
TEST_F(BlockUtilsBareWithServerDeathTest, MalformedRequest_UnregisteredClient)
{
   /*
   Same as above, from a client that has yet to register its bdv
   */
   EXPECT_EXIT(runScenario([this]{
      WebSocketServer::initPeerStore({
         homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
      startWSServer(nullptr);

      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = connectClient(pCallback);

      auto fut = sendRawRequest(*bdvObj, malformedRequest());
      ASSERT_EQ(fut.wait_for(5s), std::future_status::ready);
      auto reply = fut.get();
      EXPECT_FALSE(reply.success);
      EXPECT_FALSE(reply.error.empty());

      bdvObj->registerWithDB(
         Config::BitcoinSettings::getMagicBytes().toHexStr());
      EXPECT_TRUE(nodeStatusReplies(*bdvObj));
   }), ::testing::ExitedWithCode(0), "");
}

////
TEST_F(BlockUtilsBareWithServerDeathTest, RpcBroadcast_InvalidTx)
{
   /*
   A tx that fails to deserialize in the rpc broadcast thread is reported
   to its bdv, the thread moves on to the next broadcast
   */
   EXPECT_EXIT(runScenario([this]{
      WebSocketServer::initPeerStore({
         homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
      startWSServer(nullptr);
      theBDMt_->start(Config::DBSettings::initMode());
      ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);

      auto pCallback = std::make_shared<ErrorCatcher>();
      auto bdvObj = connectClient(pCallback);
      bdvObj->registerWithDB(
         Config::BitcoinSettings::getMagicBytes().toHexStr());
      bdvObj->goOnline();
      pCallback->waitOnNotification(DBTestUtils::UTCallback::BDMAction_Ready);

      for (unsigned i=0; i<2; i++) {
         std::vector<uint8_t> invalidTx{1, 2, 3};
         capnp::MallocMessageBuilder message;
         auto request = message.initRoot<Codec::BDV::Request>();
         auto staticRequest = request.initStatic();
         staticRequest.setRpcBroadcast(
            capnp::Data::Reader(invalidTx.data(), invalidTx.size()));

         auto fut = sendRawRequest(*bdvObj, toBytes(message));
         ASSERT_EQ(fut.wait_for(5s), std::future_status::ready);
         EXPECT_TRUE(fut.get().success);

         auto error = pCallback->nextError(5s);
         ASSERT_NE(error, nullptr) << "broadcast #" << i;
         EXPECT_EQ(error->errorCode, (int)ArmoryErrorCodes::ZcBroadcast_Error);
      }
   }), ::testing::ExitedWithCode(0), "");
}

////
TEST_F(BlockUtilsBareWithServer, ZcError_BdvGone)
{
   /*
   A zc error for a bdv that is gone should not stop the zc notification
   thread: errors for other bdvs still go through
   */
   WebSocketServer::initPeerStore({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   auto clients = std::make_shared<Clients>(theBDMt_->bdm());
   startWSServer(clients);
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);

   auto pCallback = std::make_shared<ErrorCatcher>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", Config::NetworkSettings::dbPort(),
      std::make_shared<NetworkPeers::ClientStore>(Wallets::IO::ReadOnlyFileParams{
         homedir_ / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
      true, //public server
      pCallback);
   bdvObj->connectToRemote();
   bdvObj->registerWithDB(Config::BitcoinSettings::getMagicBytes().toHexStr());
   bdvObj->goOnline();
   pCallback->waitOnNotification(DBTestUtils::UTCallback::BDMAction_Ready);

   auto bdvIds = clients->getBdvIds();
   ASSERT_EQ(bdvIds.size(), 1U);
   auto bdvId = *bdvIds.begin();

   auto goneHash = READHEX("0000000000000000000000000000000000000000000000000000000000000001");
   auto ourHash = READHEX("0000000000000000000000000000000000000000000000000000000000000002");
   {
      ZeroConf::ZeroConfCallbacks_BDV zcCallbacks(clients.get());
      zcCallbacks.pushZcError(bdvId + 1, goneHash,
         ArmoryErrorCodes::ZcBroadcast_Error, "bdv is gone");
      zcCallbacks.pushZcError(bdvId, ourHash,
         ArmoryErrorCodes::ZcBroadcast_Error, "bdv is here");

      auto error = pCallback->nextError(5s);
      ASSERT_NE(error, nullptr);
      EXPECT_EQ(error->errorData, ourHash);
      EXPECT_EQ(error->errorCode, (int)ArmoryErrorCodes::ZcBroadcast_Error);
   }
}

////////////////////////////////////////////////////////////////////////////////
namespace {
   /*
   Well formed tx spending an outpoint that does not exist: the mocked node
   ignores it on the p2p layer, so its broadcast times out and falls back
   to the rpc broadcast, which rejects it
   */
   BinaryData unknownOutpointTx()
   {
      BinaryWriter bw;
      bw.put_uint32_t(1); //version
      bw.put_var_int(1); //txin count
      bw.put_BinaryData(BinaryData(std::vector<uint8_t>(32, 0xAB)));
      bw.put_uint32_t(0); //outpoint index
      bw.put_var_int(0); //empty sigscript
      bw.put_uint32_t(UINT32_MAX); //sequence
      bw.put_var_int(1); //txout count
      bw.put_uint64_t(COIN);
      bw.put_var_int(25); //p2pkh script size
      bw.put_uint8_t(0x76); //OP_DUP
      bw.put_uint8_t(0xA9); //OP_HASH160
      bw.put_uint8_t(20);
      bw.put_BinaryData(TestChain::scrAddrA.getSliceCopy(1, 20));
      bw.put_uint8_t(0x88); //OP_EQUALVERIFY
      bw.put_uint8_t(0xAC); //OP_CHECKSIG
      bw.put_uint32_t(0); //locktime
      return bw.getData();
   }
}

////
TEST_F(BlockUtilsBareWithServerDeathTest, P2PBroadcastFallback_BdvGone)
{
   /*
   The bdv that pushed a zc can be gone by the time its p2p broadcast times
   out: the rpc fallback goes on without it, and still reports to the other
   bdvs that pushed the same zc
   */
   EXPECT_EXIT(runScenario([this]{
      WebSocketServer::initPeerStore({
         homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
      auto clients = std::make_shared<Clients>(theBDMt_->bdm());
      startWSServer(clients);
      theBDMt_->start(Config::DBSettings::initMode());
      ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
      theBDMt_->bdm()->enableZeroConf();

      const auto magic = Config::BitcoinSettings::getMagicBytes().toHexStr();
      auto callbackA = std::make_shared<ErrorCatcher>();
      auto bdvA = connectClient(callbackA);
      bdvA->registerWithDB(magic);
      auto callbackB = std::make_shared<ErrorCatcher>();
      auto bdvB = connectClient(callbackB);
      bdvB->registerWithDB(magic);
      ASSERT_EQ(clients->getBdvIds().size(), 2U);

      //a bdv only gets error notifications once it is ready
      bdvB->goOnline();
      callbackB->waitOnNotification(DBTestUtils::UTCallback::BDMAction_Ready);

      //A pushes the zc, B pushes it after A and gets tied to A's broadcast,
      //a reply to the next request means the broadcast was processed
      auto rawTx = unknownOutpointTx();
      bdvA->broadcastZC({rawTx});
      ASSERT_TRUE(nodeStatusReplies(*bdvA));
      bdvB->broadcastZC({rawTx});
      ASSERT_TRUE(nodeStatusReplies(*bdvB));

      //A leaves before the 5s p2p timeout
      bdvA->unregisterFromDB();
      for (unsigned i=0; i<50 && clients->getBdvIds().size() != 1; i++) {
         std::this_thread::sleep_for(100ms);
      }
      ASSERT_EQ(clients->getBdvIds().size(), 1U);

      //B gets the rpc fallback's error
      auto error = callbackB->nextError(10s);
      ASSERT_NE(error, nullptr);
      EXPECT_EQ(error->errorData, Tx(rawTx).getThisHash());
      EXPECT_EQ(error->errorCode, (int)ArmoryErrorCodes::ZcBroadcast_Error);
   }), ::testing::ExitedWithCode(0), "");
}

////
TEST_F(BlockUtilsBareWithServerDeathTest, P2PBroadcastFallback_UnregisteredClient)
{
   /*
   Same as above, for a zc pushed by a client that never registered a bdv
   */
   EXPECT_EXIT(runScenario([this]{
      WebSocketServer::initPeerStore({
         homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
      startWSServer(nullptr);
      theBDMt_->start(Config::DBSettings::initMode());
      ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
      theBDMt_->bdm()->enableZeroConf();

      auto unregistered = connectClient(
         std::make_shared<DBTestUtils::UTCallback>());
      auto pCallback = std::make_shared<ErrorCatcher>();
      auto bdvObj = connectClient(pCallback);
      bdvObj->registerWithDB(
         Config::BitcoinSettings::getMagicBytes().toHexStr());
      bdvObj->goOnline();
      pCallback->waitOnNotification(DBTestUtils::UTCallback::BDMAction_Ready);

      unregistered->broadcastZC({unknownOutpointTx()});

      //wait out the 5s p2p timeout, the fallback is queued by then
      std::this_thread::sleep_for(7s);

      //the rpc broadcast thread still serves the registered bdv
      std::vector<uint8_t> invalidTx{1, 2, 3};
      capnp::MallocMessageBuilder message;
      auto request = message.initRoot<Codec::BDV::Request>();
      auto staticRequest = request.initStatic();
      staticRequest.setRpcBroadcast(
         capnp::Data::Reader(invalidTx.data(), invalidTx.size()));
      auto fut = sendRawRequest(*bdvObj, toBytes(message));
      ASSERT_EQ(fut.wait_for(5s), std::future_status::ready);
      EXPECT_TRUE(fut.get().success);

      auto error = pCallback->nextError(5s);
      ASSERT_NE(error, nullptr);
      EXPECT_EQ(error->errorCode, (int)ArmoryErrorCodes::ZcBroadcast_Error);
   }), ::testing::ExitedWithCode(0), "");
}

////////////////////////////////////////////////////////////////////////////////
namespace {
   //scrAddr filter running against a bdm state of our choosing
   class ScrAddrFilter_ControlledBDM : public ScrAddrFilter
   {
   private:
      const std::shared_ptr<Blockchain> blockchain_;

   public:
      std::atomic<BDMState> state{BDMState::Initializing};

      ScrAddrFilter_ControlledBDM(LMDBBlockDatabase* db, uint16_t sdbiId,
         std::shared_ptr<Blockchain> bc) :
         ScrAddrFilter(db, sdbiId), blockchain_(bc)
      {}

      ~ScrAddrFilter_ControlledBDM() override
      {
         //the filter thread calls our overrides, stop it while they exist
         shutdown();
      }

   protected:
      bool bdmIsRunning() const override
      {
         return true;
      }

      BDMState waitOnBDMReady(std::chrono::milliseconds) const override
      {
         auto current = state.load();
         if (current == BDMState::Uninitialized ||
            current == BDMState::Initializing) {
            std::this_thread::sleep_for(10ms);
         }
         return current;
      }

      Hash32 applyBlockRangeToDB(
         uint32_t, const std::vector<std::string>&, bool) override
      {
         return Hash32{};
      }

      std::shared_ptr<Blockchain> blockchain() const override
      {
         return blockchain_;
      }

      std::shared_ptr<ScrAddrFilter> getNew(unsigned) override
      {
         return nullptr;
      }
   };

   //counts the calls to a registration batch callback
   class BatchCallbackTracker
   {
   private:
      std::promise<bool> firstCall_;

   public:
      std::atomic<unsigned> calls{0};
      std::future<bool> firstCall = firstCall_.get_future();

      RegistrationBatch::Callback get()
      {
         return [this](bool success) {
            if (calls.fetch_add(1) == 0) {
               firstCall_.set_value(success);
            }
         };
      }

      //empty address set: the batch completes as soon as the bdm is ready
      std::shared_ptr<RegistrationBatch> makeBatch(const std::string& id)
      {
         return std::make_shared<RegistrationBatch>(
            std::vector<std::string>{id}, std::vector<Types::ScrAddr>{},
            true, get());
      }
   };
}

////
TEST_F(BlockUtilsBare, ScrAddrFilter_BatchHaltedWhileWaitingOnBDM)
{
   /*
   A client disconnecting while its registration batch waits on the bdm
   halts the batch: the filter should drop it and keep processing the
   batches that come after it, and the halted batch callback should fire
   only once
   */
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);

   BatchCallbackTracker haltedTracker, nextTracker;
   auto haltedBatch = haltedTracker.makeBatch("halted");
   auto nextBatch = nextTracker.makeBatch("next");
   {
      ScrAddrFilter_ControlledBDM saf(
         iface_, 0x42, theBDMt_->bdm()->blockchain());
      saf.start();

      //let the filter pick up the batch and wait on the bdm, then halt it
      saf.pushAddressBatch(haltedBatch);
      std::this_thread::sleep_for(200ms);
      haltedBatch->halt();

      saf.pushAddressBatch(nextBatch);
      saf.state.store(BDMState::Ready);

      ASSERT_EQ(nextTracker.firstCall.wait_for(5s), std::future_status::ready);
      EXPECT_TRUE(nextTracker.firstCall.get());
   }

   //the filter thread is gone, all calls are in
   EXPECT_EQ(haltedTracker.calls.load(), 1U);
   EXPECT_EQ(nextTracker.calls.load(), 1U);
}

////
TEST_F(BlockUtilsBare, ScrAddrFilter_BDMInitFailed)
{
   /*
   Batches waiting on a bdm that failed to initialize fail, once each,
   and the filter moves on to the next batch
   */
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);

   BatchCallbackTracker tracker1, tracker2;
   auto batch1 = tracker1.makeBatch("batch1");
   auto batch2 = tracker2.makeBatch("batch2");
   {
      ScrAddrFilter_ControlledBDM saf(
         iface_, 0x42, theBDMt_->bdm()->blockchain());

      //halt the batches before the filter goes away, a filter thread
      //looping on a batch only exits once it is halted
      auto haltBatches = [&batch1, &batch2](void*) {
         batch1->halt();
         batch2->halt();
      };
      std::unique_ptr<void, decltype(haltBatches)> haltOnExit(
         &saf, haltBatches);

      saf.state.store(BDMState::InitializationFailed);
      saf.start();
      saf.pushAddressBatch(batch1);
      saf.pushAddressBatch(batch2);

      ASSERT_EQ(tracker1.firstCall.wait_for(5s), std::future_status::ready);
      EXPECT_FALSE(tracker1.firstCall.get());
      ASSERT_EQ(tracker2.firstCall.wait_for(5s), std::future_status::ready);
      EXPECT_FALSE(tracker2.firstCall.get());

      //give a looping filter thread time to show
      std::this_thread::sleep_for(200ms);
      EXPECT_EQ(tracker1.calls.load(), 1U);
      EXPECT_EQ(tracker2.calls.load(), 1U);
   }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class WebSocketMessageTests : public ::testing::Test
{
protected:
   void SetUp() override
   {
      startupBIP151CTX();
      startupBIP150CTX(4);
   }

   //serialized packets start with LWS_PRE bytes of room for lws, which
   //are not sent over the wire
   static BinaryData toWire(const BinaryData& packet)
   {
      return packet.getSliceCopy(LWS_PRE, packet.getSize() - LWS_PRE);
   }

   /*
   packet header for fragments and segments:
     uint32_t size, uint32_t msg id, uint32_t packet id or count,
     uint8_t type, 3 bytes of padding
   */
   static BinaryData makePacket(uint32_t msgId, uint32_t idOrCount,
      ArmoryAEAD::BIP151_PayloadType type, const BinaryDataRef& body)
   {
      BinaryWriter bw;
      bw.put_uint32_t(body.getSize() + 12);
      bw.put_uint32_t(msgId);
      bw.put_uint32_t(idOrCount);
      bw.put_uint8_t((uint8_t)type);
      bw.put_uint8_t(0);
      bw.put_uint16_t(0);
      bw.put_BinaryDataRef(body);
      return bw.getData();
   }
};

////
TEST_F(WebSocketMessageTests, ReadPacketType_ShortPacket)
{
   //a packet needs 4 bytes of size and 1 byte of type to carry a type
   for (unsigned size=0; size<5; size++) {
      BinaryData packet(std::vector<uint8_t>(size, 0x65));
      EXPECT_EQ(Network::WebSocketMessagePartial::readPacketType(packet.getRef()),
         ArmoryAEAD::BIP151_PayloadType::Undefined) << "size: " << size;
   }

   //an aead packet without an id
   auto packet = zeroedBytes(5);
   packet.getPtr()[4] = (uint8_t)ArmoryAEAD::BIP151_PayloadType::Start;
   EXPECT_EQ(Network::WebSocketMessagePartial::readPacketType(packet.getRef()),
      ArmoryAEAD::BIP151_PayloadType::Start);
}

////
TEST_F(WebSocketMessageTests, ParsePacket_ShortPacket)
{
   for (unsigned size=0; size<5; size++) {
      //zeroed packets: a 4 bytes packet carries a valid size field
      auto packet = zeroedBytes(size);

      Network::WebSocketMessagePartial msg;
      EXPECT_FALSE(msg.parsePacket(packet)) << "size: " << size;
      EXPECT_FALSE(msg.isReady());
   }

   //an aead packet without an id still parses
   auto packet = zeroedBytes(5);
   uint32_t packetSize = 1;
   memcpy(packet.getPtr(), &packetSize, 4);
   packet.getPtr()[4] = (uint8_t)ArmoryAEAD::BIP151_PayloadType::Start;

   Network::WebSocketMessagePartial msg;
   EXPECT_TRUE(msg.parsePacket(packet));
   EXPECT_TRUE(msg.isReady());
   EXPECT_EQ(msg.getType(), ArmoryAEAD::BIP151_PayloadType::Start);
}

////
TEST_F(WebSocketMessageTests, ParsePacket_TypeMismatch)
{
   //an aead packet then a single packet on the same message
   auto aeadPacket = toWire(Network::WebSocketMessageCodec::serialize(
      zeroedBytes(32).getRef(), nullptr,
      ArmoryAEAD::BIP151_PayloadType::EncInit, 0)[0]);
   auto singlePacket = toWire(Network::WebSocketMessageCodec::serialize(
      zeroedBytes(32).getRef(), nullptr,
      ArmoryAEAD::BIP151_PayloadType::SinglePacket, 1)[0]);

   Network::WebSocketMessagePartial msg;
   ASSERT_TRUE(msg.parsePacket(aeadPacket));
   EXPECT_FALSE(msg.parsePacket(singlePacket));
}

////
TEST_F(WebSocketMessageTests, SegmentedMessage)
{
   //a capnp message spread over several segments
   std::string errStr(1000, 'a');
   capnp::MallocMessageBuilder message(16, capnp::AllocationStrategy::FIXED_SIZE);
   auto reply = message.initRoot<Codec::BDV::Reply>();
   reply.setError(errStr);
   auto segments = message.getSegmentsForOutput();
   ASSERT_GT(segments.size(), 1U);

   std::vector<BinaryData> packets;
   for (uint32_t i=0; i<segments.size(); i++) {
      BinaryDataRef body(
         (const uint8_t*)segments[i].begin(),
         segments[i].size() * sizeof(capnp::word));
      if (i == 0) {
         packets.emplace_back(makePacket(10, segments.size(),
            ArmoryAEAD::BIP151_PayloadType::FirstSegment, body));
      } else {
         packets.emplace_back(makePacket(10, i,
            ArmoryAEAD::BIP151_PayloadType::Segment, body));
      }
   }

   //segments can come in any order
   Network::WebSocketMessagePartial msg;
   for (auto iter = packets.rbegin(); iter != packets.rend(); ++iter) {
      EXPECT_FALSE(msg.isReady());
      ASSERT_TRUE(msg.parsePacket(*iter));
   }
   ASSERT_TRUE(msg.isReady());

   auto msgReader = msg.getReader();
   ASSERT_NE(msgReader, nullptr);
   auto capnReader = msgReader->getReader();
   auto readReply = capnReader->getRoot<Codec::BDV::Reply>();
   EXPECT_EQ(std::string(readReply.getError()), errStr);
}

////
TEST_F(WebSocketMessageTests, SegmentedMessage_IdOutOfRange)
{
   /*
   A message announcing 2 segments, carrying segment ids 0 and 5: the
   segment count matches the announced count, but the segmented reader
   would write segment 5 past the end of its 2 entries
   */
   auto body = zeroedBytes(8);
   Network::WebSocketMessagePartial msg;
   auto packet1 = makePacket(10, 2,
      ArmoryAEAD::BIP151_PayloadType::FirstSegment, body.getRef());
   auto packet2 = makePacket(10, 5,
      ArmoryAEAD::BIP151_PayloadType::Segment, body.getRef());
   ASSERT_TRUE(msg.parsePacket(packet1));
   ASSERT_TRUE(msg.parsePacket(packet2));

   ASSERT_FALSE(msg.isReady());
   EXPECT_EQ(msg.getReader(), nullptr);
}

////
TEST_F(WebSocketMessageTests, FragmentedMessage_IdOutOfRange)
{
   //same with fragments: 2 announced, ids 0 and 3
   auto body = zeroedBytes(8);
   Network::WebSocketMessagePartial msg;
   auto packet1 = makePacket(10, 2,
      ArmoryAEAD::BIP151_PayloadType::FragmentHeader, body.getRef());
   auto packet2 = makePacket(10, 3,
      ArmoryAEAD::BIP151_PayloadType::FragmentPacket, body.getRef());
   ASSERT_TRUE(msg.parsePacket(packet1));
   ASSERT_TRUE(msg.parsePacket(packet2));

   ASSERT_FALSE(msg.isReady());
   EXPECT_EQ(msg.getReader(), nullptr);
}

////
TEST_F(WebSocketMessageTests, ClientConnection_ShortPacket)
{
   /*
   Before the AEAD handshake, packets reach processReadQueue straight off
   the socket. One too short to carry a type closes the connection.
   */
   auto peers = std::make_shared<NetworkPeers::ServerStore>();
   for (unsigned size=1; size<5; size++) {
      ClientConnection conn(nullptr, size, peers->getView(), true);
      conn.readQueue_->push_back(zeroedBytes(size));

      EXPECT_NO_THROW(conn.processReadQueue(nullptr, nullptr)) <<
         "size: " << size;
      EXPECT_EQ(conn.run_->load(), -1) << "size: " << size;
   }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BlockUtilsFull : public ::testing::Test
{
protected:
   void initBDM()
   {
      Config::reset();
      Config::DBSettings::setServiceType(SERVICE_UNITTEST);
      Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_FULL",
         "--thread-count=3",
         "--public"},
         Config::ProcessType::DB);

      DBTestUtils::init();
      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();

      auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);
      nodePtr->setBDM(theBDMt_->bdm());
      clients_ = new Clients(theBDMt_->bdm());
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

      // Put the first 5 blocks into the blkdir
      blk0dat_ = FileUtils::getBlkFilename(blkdir_ / "blocks", 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);

      initBDM();
   }

   /////////////////////////////////////////////////////////////////////////////
   void TearDown() override
   {
      if (clients_ != nullptr) {
         clients_->shutdown();
      }
      theBDMt_->shutdown();

      delete clients_;
      delete theBDMt_;
      clients_ = nullptr;
      theBDMt_ = nullptr;

      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);
      Config::reset();
      CLEANUP_ALL_TIMERS();
   }

   BlockDataManagerThread *theBDMt_;
   Clients* clients_;
   LMDBBlockDatabase* iface_;

   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, DoubleReorg_SpendTxMinedOnBothBranches)
{
   spendTxMinedOnBothBranches(theBDMt_, clients_, blk0dat_);
   if (HasFatalFailure()) {
      return;
   }

   //fullnode also has a tx hint per block: the lookup by hash gives both
   //keys, and the collision resolves to the 5A copy
   auto bdm = theBDMt_->bdm();
   try {
      bdm->getIFace()->getDBKeyForHash(TestChain::hash42);
      ASSERT_TRUE(false);
   } catch (const TxHintCollision& collision) {
      ASSERT_EQ(collision.getCandidates().size(), 2);
      auto solution = bdm->blockchainData()->resolveTxHintCollision(collision);
      auto header = bdm->blockchain()->getHeaderById(
         Types::getBlockIDFromTxKey(solution.txKey));
      EXPECT_EQ(header->getThisHash(), TestChain::blkHash5A);
   }
}

TEST_F(BlockUtilsFull, TxHints)
{
   clients_->init();
   auto bdvID = DBTestUtils::registerBDV(
      clients_, Config::BitcoinSettings::getMagicBytes());
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   auto bdm = theBDMt_->bdm();
   auto db = bdm->getIFace();

   //in fullnode, we should have txhints for every tx in the test chain

   //block 0
   auto keyPair = getTxKeyForHash(TestChain::hash00, db);
   EXPECT_EQ(keyPair.first, 0); EXPECT_EQ(keyPair.second, 0);

   //block 1
   keyPair = getTxKeyForHash(TestChain::hash10, db);
   EXPECT_EQ(keyPair.first, 1); EXPECT_EQ(keyPair.second, 0);

   //block 2
   keyPair = getTxKeyForHash(TestChain::hash20, db);
   EXPECT_EQ(keyPair.first, 2); EXPECT_EQ(keyPair.second, 0);
   keyPair = getTxKeyForHash(TestChain::hash21, db);
   EXPECT_EQ(keyPair.first, 2); EXPECT_EQ(keyPair.second, 1);
   keyPair = getTxKeyForHash(TestChain::hash22, db);
   EXPECT_EQ(keyPair.first, 2); EXPECT_EQ(keyPair.second, 2);

   //block 3
   keyPair = getTxKeyForHash(TestChain::hash30, db);
   EXPECT_EQ(keyPair.first, 3); EXPECT_EQ(keyPair.second, 0);
   keyPair = getTxKeyForHash(TestChain::hash31, db);
   EXPECT_EQ(keyPair.first, 3); EXPECT_EQ(keyPair.second, 1);
   keyPair = getTxKeyForHash(TestChain::hash32, db);
   EXPECT_EQ(keyPair.first, 3); EXPECT_EQ(keyPair.second, 2);
   keyPair = getTxKeyForHash(TestChain::hash33, db);
   EXPECT_EQ(keyPair.first, 3); EXPECT_EQ(keyPair.second, 3);
   keyPair = getTxKeyForHash(TestChain::hash34, db);
   EXPECT_EQ(keyPair.first, 3); EXPECT_EQ(keyPair.second, 4);
   keyPair = getTxKeyForHash(TestChain::hash35, db);
   EXPECT_EQ(keyPair.first, 3); EXPECT_EQ(keyPair.second, 5);

   //block 4
   keyPair = getTxKeyForHash(TestChain::hash40, db);
   EXPECT_EQ(keyPair.first, 4); EXPECT_EQ(keyPair.second, 0);
   keyPair = getTxKeyForHash(TestChain::hash41, db);
   EXPECT_EQ(keyPair.first, 4); EXPECT_EQ(keyPair.second, 1);
   keyPair = getTxKeyForHash(TestChain::hash42, db);
   EXPECT_EQ(keyPair.first, 4); EXPECT_EQ(keyPair.second, 2);
   keyPair = getTxKeyForHash(TestChain::hash43, db);
   EXPECT_EQ(keyPair.first, 4); EXPECT_EQ(keyPair.second, 3);

   //block 5
   keyPair = getTxKeyForHash(TestChain::hash50, db);
   EXPECT_EQ(keyPair.first, 5); EXPECT_EQ(keyPair.second, 0);
   keyPair = getTxKeyForHash(TestChain::hash51, db);
   EXPECT_EQ(keyPair.first, 5); EXPECT_EQ(keyPair.second, 1);
   keyPair = getTxKeyForHash(TestChain::hash52, db);
   EXPECT_EQ(keyPair.first, 5); EXPECT_EQ(keyPair.second, 2);

   //check db returns hint for hash35
   auto txkey = db->getDBKeyForHash(TestChain::hash35);
   ASSERT_EQ(txkey, 0xffff050004000000);

   //inject fake hints for hash35
   {
      const uint64_t blockIDMask = 0x00000000FFFFFFFF;
      uint8_t hashTableIndex = TestChain::hash35.getPtr()[8];
      auto tx = db->beginHashTableTx(
         DB_SELECT::TXHINTS, hashTableIndex, LMDB::Mode::ReadWrite);

      //2.1, 2.4 (an non existant tx, to cover deser throws)
      uint64_t blockID2 = 3ul << 32;
      uint64_t txHintKey2;
      std::memcpy(&txHintKey2, TestChain::hash35.getPtr(), 8);
      txHintKey2 = (txHintKey2 & blockIDMask) | blockID2;
      std::vector<uint16_t> txids2{1, 4};
      tx->insert(
         LMDB::DataRef{
            sizeof(uint64_t),
            (const char*)&txHintKey2},
         LMDB::DataRef{
            sizeof(uint16_t) * txids2.size(),
            (const char*)&txids2[0]}
      );

      //4.2, 4.4
      uint64_t blockID4 = 5ul << 32;
      uint64_t txHintKey4;
      std::memcpy(&txHintKey4, TestChain::hash35.getPtr(), 8);
      txHintKey4 = (txHintKey4 & blockIDMask) | blockID4;
      std::vector<uint16_t> txids4{2, 4};
      tx->insert(
         LMDB::DataRef{
            sizeof(uint64_t),
            (const char*)&txHintKey4},
         LMDB::DataRef{
            sizeof(uint16_t) * txids4.size(),
            (const char*)&txids4[0]}
      );

      //7.3 (inexistant block, to cover throws)
      uint64_t blockID7 = 8ul << 32;
      uint64_t txHintKey7;
      std::memcpy(&txHintKey7, TestChain::hash35.getPtr(), 8);
      txHintKey7 = (txHintKey7 & blockIDMask) | blockID7;
      std::vector<uint16_t> txids7{3};
      tx->insert(
         LMDB::DataRef{
            sizeof(uint64_t),
            (const char*)&txHintKey7},
         LMDB::DataRef{
            sizeof(uint16_t) * txids7.size(),
            (const char*)&txids7[0]}
      );
   }

   try {
      db->getDBKeyForHash(TestChain::hash35);
      ASSERT_TRUE(false);
   } catch (const TxHintCollision& collision) {
      ASSERT_EQ(collision.getTxHash(), TestChain::hash35);

      auto candidates = collision.getCandidates();
      ASSERT_EQ(candidates.size(), 6);

      //2.1
      auto candidatesIter = candidates.begin();
      ASSERT_EQ(*candidatesIter, 0xffff010003000000);

      //4.2
      ++candidatesIter;
      ASSERT_EQ(*candidatesIter, 0xffff020005000000);

      //7.3
      ++candidatesIter;
      ASSERT_EQ(*candidatesIter, 0xffff030008000000);

      //2.4
      ++candidatesIter;
      ASSERT_EQ(*candidatesIter, 0xffff040003000000);

      //4.4
      ++candidatesIter;
      ASSERT_EQ(*candidatesIter, 0xffff040005000000);

      //3.5
      ++candidatesIter;
      ASSERT_EQ(*candidatesIter, 0xffff050004000000);

      try {
         auto solution = bdm->blockchainData()->resolveTxHintCollision(collision);
         ASSERT_EQ(solution.txKey, 0xffff050004000000);
      } catch (const NoSolutionException&) {
         ASSERT_TRUE(false);
      }
   }

   //a GET_TXS_BY_HASH for hash35 goes through the same collision and
   //returns the tx the collision resolves to
   auto txs = DBTestUtils::getTxsByHash(clients_, bdvID, {TestChain::hash35});
   ASSERT_EQ(txs.size(), 1);
   EXPECT_EQ(txs[0].getThisHash(), TestChain::hash35);
   EXPECT_EQ(txs[0].getDBKey(), 0xffff050004000000);

   //cleanup
   bdvPtr.reset();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, Load5Blocks_RescanOps)
{
   auto startbdm = [this](BdmInitMode init)->void
   {
      clients_->init();
      auto bdvID = DBTestUtils::registerBDV(
         clients_, Config::BitcoinSettings::getMagicBytes());

      DBTestUtils::registerWallet(clients_, bdvID, {
            TestChain::scrAddrA,
            TestChain::scrAddrB,
            TestChain::scrAddrC,
            TestChain::scrAddrD,
            TestChain::scrAddrE,
            TestChain::scrAddrF},
         "wallet1",
         false);
      DBTestUtils::registerWallet(clients_, bdvID, {
            TestChain::lb1ScrAddr,
            TestChain::lb1ScrAddrP2SH},
         TestChain::lb1B58ID,
         false);
      DBTestUtils::registerWallet(clients_, bdvID, {
            TestChain::lb2ScrAddr,
            TestChain::lb2ScrAddrP2SH},
         TestChain::lb2B58ID,
         false);

      auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

      //wait on signals
      theBDMt_->start(init);
      ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
      DBTestUtils::goOnline(clients_, bdvID);
      DBTestUtils::waitOnBDVReady(clients_, bdvID);
   };

   auto checkBalance = [](std::shared_ptr<BlockDataManager> bdm)
   {
      EXPECT_EQ(bdm->blockchain()->top()->getThisHash(), TestChain::blkHash5);

      auto getBal = [bdm](const BinaryData& scrAddr)->uint64_t
      { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

      EXPECT_EQ(getBal(TestChain::scrAddrA), 50 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrB), 70 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrC), 20 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrD), 65 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrE), 30 * COIN);
      EXPECT_EQ(getBal(TestChain::scrAddrF),  5 * COIN);

      EXPECT_EQ(getBal(TestChain::lb1ScrAddr), 5 * COIN);
      EXPECT_EQ(getBal(TestChain::lb1ScrAddrP2SH), 25 * COIN);
      EXPECT_EQ(getBal(TestChain::lb2ScrAddr), 30 * COIN);
      EXPECT_EQ(getBal(TestChain::lb2ScrAddrP2SH), 0 * COIN);
   };

   auto resetbdm = [this](void)->void
   {
      clients_->shutdown();
      theBDMt_->shutdown();

      delete clients_;
      delete theBDMt_;
      std::this_thread::sleep_for(1s);

      initBDM();
   };

   //regular start
   startbdm(BdmInitMode::RESUME);
   checkBalance(theBDMt_->bdm());
   auto lastScannedRange = theBDMt_->bdm()->getLastScannedRange();

   //rebuild
   resetbdm();
   startbdm(BdmInitMode::REBUILD);
   checkBalance(theBDMt_->bdm());
   lastScannedRange = theBDMt_->bdm()->getLastScannedRange();

   //regular start
   resetbdm();
   startbdm(BdmInitMode::RESUME);
   checkBalance(theBDMt_->bdm());
   lastScannedRange = theBDMt_->bdm()->getLastScannedRange();
   EXPECT_EQ(lastScannedRange.first, TestChain::blkHash5);
   EXPECT_EQ(lastScannedRange.second, TestChain::blkHash5);

   //rescan
   resetbdm();
   startbdm(BdmInitMode::RESCAN);
   checkBalance(theBDMt_->bdm());
   lastScannedRange = theBDMt_->bdm()->getLastScannedRange();
   EXPECT_EQ(lastScannedRange.first, TestChain::blkHash0);
   EXPECT_EQ(lastScannedRange.second, TestChain::blkHash5);

   //regular start
   resetbdm();
   startbdm(BdmInitMode::RESUME);
   checkBalance(theBDMt_->bdm());
   lastScannedRange = theBDMt_->bdm()->getLastScannedRange();
   EXPECT_EQ(lastScannedRange.first, TestChain::blkHash5);
   EXPECT_EQ(lastScannedRange.second, TestChain::blkHash5);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, GetTxsByHash_BadHashSize)
{
   clients_->init();
   auto bdvID = DBTestUtils::registerBDV(
      clients_, Config::BitcoinSettings::getMagicBytes());
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   theBDMt_->start(Config::DBSettings::initMode());
   ASSERT_EQ(theBDMt_->bdm()->waitUntilReady(5s), BDMState::Ready);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   //a hash that isn't 32 bytes long has no tx key
   auto db = theBDMt_->bdm()->getIFace();
   auto hash31 = TestChain::hash35.getSliceCopy(0, 31);
   auto hash33 = TestChain::hash35;
   hash33.append(uint8_t(0));
   const std::vector<Types::TxHash> badHashes{ {}, hash31, hash33 };
   for (const auto& hash : badHashes) {
      EXPECT_EQ(db->getDBKeyForHash(hash), Types::INVALID_TX_KEY);
   }

   //before any zc there is no mempool snapshot: the bad hashes and an
   //unknown hash are left out of the reply, hash35 is still returned
   auto unknownHash = READHEX(
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
   ASSERT_FALSE(Types::isTxKeyValid(db->getDBKeyForHash(unknownHash)));

   auto request = badHashes;
   request.emplace_back(unknownHash);
   request.emplace_back(TestChain::hash35);
   auto txs = DBTestUtils::getTxsByHash(clients_, bdvID, request);
   ASSERT_EQ(txs.size(), 1);
   EXPECT_EQ(txs[0].getThisHash(), TestChain::hash35);

   //cleanup
   bdvPtr.reset();
}

/*
TODO:
 - test fresh address registration
 - test fresh address registration on top of empty db
*/

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Now actually execute all the tests
////////////////////////////////////////////////////////////////////////////////
GTEST_API_ int main(int argc, char **argv)
{
   #ifdef _WIN32
      WSADATA wsaData;
      WORD wVersion = MAKEWORD(2, 0);
      WSAStartup(wVersion, &wsaData);
   #endif

   srand(time(0));
   std::cout << "Running main() from gtest_main.cc\n";

   // Required by libbtc.
   Cryptography::ECDSA::setupContext();
   //LOGENABLESTDOUT();
   LOGDISABLESTDOUT();

   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();

   // Required by libbtc.
   Cryptography::ECDSA::shutdown();

   FLUSHLOG();
   CLEANUPLOG();

   return exitCode;
}
