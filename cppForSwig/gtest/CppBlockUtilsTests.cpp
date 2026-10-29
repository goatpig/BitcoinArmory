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
#include <BlockchainDatabase/BlockchainData.h>
#include <ZeroConf/Parser.h>
#include <ZeroConf/Utils.h>

#include "BDM_mainthread.h"
#include "Server.h"

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
      theBDMt_->bdm()->blockUntilReady();
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

   void startWSServer()
   {
      wsServerThr_ = std::thread([bdm=theBDMt_->bdm()](){
         WebSocketServer::start(bdm); }
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
   startWSServer();

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
