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
   virtual void SetUp()
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
   virtual void TearDown(void)
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
   BDMt->bdm()->blockUntilReady();
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
   BDMt->bdm()->blockUntilReady();
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
   BDMt->bdm()->blockUntilReady();
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
   BDMt->bdm()->blockUntilReady();
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
   nodePtr->mineNewBlock(BDMt->bdm(), 1, TestChain::addrF, 100.0);
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
   BDMt->bdm()->blockUntilReady();
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
   BDMt->bdm()->blockUntilReady();
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
   BDMt->bdm()->blockUntilReady();
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
   BDMt->bdm()->blockUntilReady();
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
   BDMt->bdm()->blockUntilReady();
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
   virtual void SetUp()
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
   virtual void TearDown(void)
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

   std::string wallet1id;
   std::string wallet2id;
   std::string LB1ID;
   std::string LB2ID;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsBare, Load5Blocks)
{
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
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
   theBDMt_->bdm()->blockUntilReady();
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
   theBDMt_->bdm()->blockUntilReady();
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
   theBDMt_->bdm()->blockUntilReady();
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
   theBDMt_->bdm()->blockUntilReady();
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
   theBDMt_->bdm()->blockUntilReady();
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
   theBDMt_->bdm()->blockUntilReady();
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
      theBDMt_->bdm()->blockUntilReady();
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
      theBDMt_->bdm()->blockUntilReady();
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
      theBDMt_->bdm()->blockUntilReady();
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
   theBDMt_->bdm()->blockUntilReady();
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
   //generate a random xor key
   auto rando = Cryptography::PRNG::fortuna.generateRandom(8);
   uint64_t xorKey;
   memcpy(&xorKey, rando.getPtr(), 8);

   //get a mmap of the block data file
   auto fileMap = FileUtils::FileMap(blk0dat_, false);
   ASSERT_TRUE(fileMap.isValid());

   //create xored copy of the block file
   auto xoredFilePath = blkdir_ / "xoredfile.dat";
   {
      size_t offset = 0;
      std::fstream xoredFile;
      xoredFile.open(xoredFilePath, std::ios::out | std::ios::binary);
      while (offset <= fileMap.size()) {
         uint64_t chunk;
         memcpy(&chunk, fileMap.ptr() + offset,
            std::min(size_t{8}, fileMap.size() - offset));
         chunk ^= xorKey;
         xoredFile.write((const char*)&chunk, 8);
         offset += 8;
      }
   }

   //swap the files
   fileMap.close();
   std::filesystem::remove(blk0dat_);
   std::filesystem::rename(xoredFilePath, blk0dat_);

   //create xor file, this is where the xorkey sits
   {
      std::fstream xorFile;
      xorFile.open(blkdir_ / "blocks" / "xor.dat", std::ios::out | std::ios::binary);
      xorFile.write((const char*)&xorKey, 8);
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
   theBDMt_->bdm()->blockUntilReady();
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   auto bdm = theBDMt_->bdm();
   auto getBal = [bdm](const BinaryData& scrAddr)->uint64_t
   { return DBTestUtils::getScrAddrBalance(scrAddr, bdm); };

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

/* this is meant to spit out test chain data for debug purposes */
TEST_F(BlockUtilsBare, DISABLED_PPrintTestChain)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "4A", "5", "5A" }, blk0dat_);
   std::vector<std::pair<uint32_t, uint8_t>> blockIds {
      { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 1 }, { 5, 1 }
   };

   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   auto bdvID = DBTestUtils::registerBDV(
      clients_, Config::BitcoinSettings::getMagicBytes());
   auto blockchain = theBDMt_->bdm()->blockchain();

   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);
   auto db = bdvPtr->getDB();

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
      ASSERT_TRUE(db->getStoredHeader(block, headerPtr, true));

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
   virtual void SetUp()
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
   virtual void TearDown(void)
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
   theBDMt_->bdm()->blockUntilReady();
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

   //cleanup
   bdvPtr.reset();
}

/*
TODO:
 - test tx filters
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
