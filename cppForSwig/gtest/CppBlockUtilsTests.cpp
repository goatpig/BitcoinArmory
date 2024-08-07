////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#include "TestUtils.h"
#include "hkdf.h"

using namespace std;
using namespace Armory::Signer;
using namespace Armory::Config;
using namespace Armory::Wallets;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BlockDir : public ::testing::Test
{
protected:
   const string blkdir_  = "./blkfiletest";
   const string homedir_ = "./fakehomedir";
   const string ldbdir_  = "./ldbtestdir";
   
   string blk0dat_;
   string wallet1id;

   /////////////////////////////////////////////////////////////////////////////
   void cleanUp()
   {
      DBUtils::removeDirectory(blkdir_);
      DBUtils::removeDirectory(homedir_);
      DBUtils::removeDirectory(ldbdir_);
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      LOGDISABLESTDOUT();
            
      cleanUp();

      mkdir(blkdir_ + "/blocks");
      mkdir(homedir_);
      mkdir(ldbdir_);

      DBSettings::setServiceType(SERVICE_UNITTEST);
      Armory::Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--public",
         "--db-type=DB_FULL",
         "--thread-count=3",
         "--public"},
         Armory::Config::ProcessType::DB);
      
      DBTestUtils::init();

      blk0dat_ = BtcUtils::getBlkFilename(blkdir_ + "/blocks", 0);
      wallet1id = "wallet1";
   }
   
   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      cleanUp();
      Armory::Config::reset();

      CLEANUP_ALL_TIMERS();
   }
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockDir, HeadersFirst)
{
   // Put the first 5 blocks out of order
   TestUtils::setBlocks({ "0", "1", "2", "4", "3", "5" }, blk0dat_);

   BlockDataManagerThread* BDMt = new BlockDataManagerThread();
   auto fakeshutdown = [](void)->void {};
   Clients *clients = new Clients(BDMt, fakeshutdown);

   BDMt->start(INIT_RESUME);
   
   const std::vector<BinaryData> scraddrs
   {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   auto&& bdvID = DBTestUtils::registerBDV(clients, BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients, bdvID, scraddrs, "wallet1");
   auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);

   DBTestUtils::goOnline(clients, bdvID);
   DBTestUtils::waitOnBDMReady(clients, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   
   const ScrAddrObj *scrobj;
   scrobj = wlt->getScrAddrObjByKey(scraddrs[0]);
   EXPECT_EQ(scrobj->getFullBalance(), 50*COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[1]);
   EXPECT_EQ(scrobj->getFullBalance(), 70*COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[2]);
   EXPECT_EQ(scrobj->getFullBalance(), 20*COIN);

   //cleanup
   bdvPtr.reset();
   wlt.reset();
   clients->exitRequestLoop();
   clients->shutdown();

   delete clients;
   delete BDMt;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockDir, HeadersFirstUpdate)
{
   // Put the first 5 blocks out of order
   TestUtils::setBlocks({ "0", "1", "2" }, blk0dat_);
   
   BlockDataManagerThread* BDMt = new BlockDataManagerThread();
   auto fakeshutdown = [](void)->void {};
   Clients *clients = new Clients(BDMt, fakeshutdown);

   BDMt->start(INIT_RESUME);

   const std::vector<BinaryData> scraddrs
   {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   auto&& bdvID = DBTestUtils::registerBDV(clients, BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients, bdvID, scraddrs, "wallet1");
   auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);

   DBTestUtils::goOnline(clients, bdvID);
   DBTestUtils::waitOnBDMReady(clients, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);

   TestUtils::appendBlocks({ "4", "3", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);
   DBTestUtils::waitOnNewBlockSignal(clients, bdvID);
   
   // we should get the same balance as we do for test 'Load5Blocks'
   const ScrAddrObj *scrobj;
   
   scrobj = wlt->getScrAddrObjByKey(scraddrs[0]);
   EXPECT_EQ(scrobj->getFullBalance(), 50*COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[1]);
   EXPECT_EQ(scrobj->getFullBalance(), 70*COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[2]);
   EXPECT_EQ(scrobj->getFullBalance(), 20*COIN);

   //cleanup
   bdvPtr.reset();
   wlt.reset();
   clients->exitRequestLoop();
   clients->shutdown();

   delete clients;
   delete BDMt;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockDir, HeadersFirstReorg)
{
   TestUtils::setBlocks({ "0", "1" }, blk0dat_);

   BlockDataManagerThread* BDMt = new BlockDataManagerThread();
   auto fakeshutdown = [](void)->void {};
   Clients *clients = new Clients(BDMt, fakeshutdown);

   BDMt->start(INIT_RESUME);

   const std::vector<BinaryData> scraddrs
   {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   auto&& bdvID = DBTestUtils::registerBDV(clients, BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients, bdvID, scraddrs, "wallet1");
   auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);

   DBTestUtils::goOnline(clients, bdvID);
   DBTestUtils::waitOnBDMReady(clients, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);

   TestUtils::appendBlocks({ "4A" }, blk0dat_);
   TestUtils::appendBlocks({ "3" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);

   TestUtils::appendBlocks({ "2" }, blk0dat_);
   TestUtils::appendBlocks({ "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);
   DBTestUtils::waitOnNewBlockSignal(clients, bdvID);

   TestUtils::appendBlocks({ "4" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);
   DBTestUtils::waitOnNewBlockSignal(clients, bdvID);

   const ScrAddrObj *scrobj;

   scrobj = wlt->getScrAddrObjByKey(scraddrs[0]);
   EXPECT_EQ(scrobj->getFullBalance(), 50 * COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[1]);
   EXPECT_EQ(scrobj->getFullBalance(), 70 * COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[2]);
   EXPECT_EQ(scrobj->getFullBalance(), 20 * COIN);

   TestUtils::appendBlocks({ "5A" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);
   DBTestUtils::waitOnNewBlockSignal(clients, bdvID);

   scrobj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrobj->getFullBalance(), 50 * COIN);
   scrobj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrobj->getFullBalance(), 30 * COIN);
   scrobj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrobj->getFullBalance(), 55 * COIN);

   //cleanup
   bdvPtr.reset();
   wlt.reset();
   clients->exitRequestLoop();
   clients->shutdown();

   delete clients;
   delete BDMt;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockDir, HeadersFirstUpdateTwice)
{
   TestUtils::setBlocks({ "0", "1", "2" }, blk0dat_);
   
   BlockDataManagerThread* BDMt = new BlockDataManagerThread();
   auto fakeshutdown = [](void)->void {};
   Clients *clients = new Clients(BDMt, fakeshutdown);

   BDMt->start(INIT_RESUME);

   const std::vector<BinaryData> scraddrs
   {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   auto&& bdvID = DBTestUtils::registerBDV(clients, BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients, bdvID, scraddrs, "wallet1");
   auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);

   DBTestUtils::goOnline(clients, bdvID);
   DBTestUtils::waitOnBDMReady(clients, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);

   TestUtils::appendBlocks({ "5" }, blk0dat_);
   TestUtils::appendBlocks({ "4" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);

   TestUtils::appendBlocks({ "3" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);
   DBTestUtils::waitOnNewBlockSignal(clients, bdvID);
   
   // we should get the same balance as we do for test 'Load5Blocks'
   const ScrAddrObj *scrobj;
   
   scrobj = wlt->getScrAddrObjByKey(scraddrs[0]);
   EXPECT_EQ(scrobj->getFullBalance(), 50*COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[1]);
   EXPECT_EQ(scrobj->getFullBalance(), 70*COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[2]);
   EXPECT_EQ(scrobj->getFullBalance(), 20*COIN);

   //cleanup
   bdvPtr.reset();
   wlt.reset();
   clients->exitRequestLoop();
   clients->shutdown();

   delete clients;
   delete BDMt;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockDir, BlockFileSplit)
{
   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
   
   std::string blk1dat = BtcUtils::getBlkFilename(blkdir_ + "/blocks", 1);
   TestUtils::setBlocks({ "2", "3", "4", "5" }, blk1dat);
   
   BlockDataManagerThread* BDMt = new BlockDataManagerThread();
   auto fakeshutdown = [](void)->void {};
   Clients *clients = new Clients(BDMt, fakeshutdown);

   BDMt->start(INIT_RESUME);

   const std::vector<BinaryData> scraddrs
   {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   auto&& bdvID = DBTestUtils::registerBDV(clients, BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients, bdvID, scraddrs, "wallet1");
   auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);

   DBTestUtils::goOnline(clients, bdvID);
   DBTestUtils::waitOnBDMReady(clients, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);

   const ScrAddrObj *scrobj;
   
   scrobj = wlt->getScrAddrObjByKey(scraddrs[0]);
   EXPECT_EQ(scrobj->getFullBalance(), 50*COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[1]);
   EXPECT_EQ(scrobj->getFullBalance(), 70*COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[2]);
   EXPECT_EQ(scrobj->getFullBalance(), 20*COIN);

   //cleanup
   bdvPtr.reset();
   wlt.reset();
   clients->exitRequestLoop();
   clients->shutdown();

   delete clients;
   delete BDMt;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockDir, BlockFileSplitUpdate)
{
   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
      
   BlockDataManagerThread* BDMt = new BlockDataManagerThread();
   auto fakeshutdown = [](void)->void {};
   Clients *clients = new Clients(BDMt, fakeshutdown);

   BDMt->start(INIT_RESUME);

   const std::vector<BinaryData> scraddrs
   {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   auto&& bdvID = DBTestUtils::registerBDV(clients, BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients, bdvID, scraddrs, "wallet1");
   auto bdvPtr = DBTestUtils::getBDV(clients, bdvID);

   DBTestUtils::goOnline(clients, bdvID);
   DBTestUtils::waitOnBDMReady(clients, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);

   std::string blk1dat = BtcUtils::getBlkFilename(blkdir_, 1);
   TestUtils::appendBlocks({ "2", "4", "3", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(BDMt);
   DBTestUtils::waitOnNewBlockSignal(clients, bdvID);

   const ScrAddrObj *scrobj;
   
   scrobj = wlt->getScrAddrObjByKey(scraddrs[0]);
   EXPECT_EQ(scrobj->getFullBalance(), 50*COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[1]);
   EXPECT_EQ(scrobj->getFullBalance(), 70*COIN);
   scrobj = wlt->getScrAddrObjByKey(scraddrs[2]);
   EXPECT_EQ(scrobj->getFullBalance(), 20*COIN);

   //cleanup
   bdvPtr.reset();
   wlt.reset();
   clients->exitRequestLoop();
   clients->shutdown();

   delete clients;
   delete BDMt;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BlockUtilsFull : public ::testing::Test
{
protected:
   BlockDataManagerThread *theBDMt_;
   Clients* clients_;

   void initBDM(void)
   {
      Armory::Config::reset();
      DBSettings::setServiceType(SERVICE_UNITTEST);
      Armory::Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--public",
         "--db-type=DB_FULL",
         "--thread-count=3",
         "--public"},
         Armory::Config::ProcessType::DB);

      DBTestUtils::init();
            
      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();

      auto nodePtr = dynamic_pointer_cast<NodeUnitTest>(
         NetworkSettings::bitcoinNodes().first);
      nodePtr->setBlockchain(theBDMt_->bdm()->blockchain());
      nodePtr->setBlockFiles(theBDMt_->bdm()->blockFiles());
      nodePtr->setIface(iface_);

      auto mockedShutdown = [](void)->void {};
      clients_ = new Clients(theBDMt_, mockedShutdown);
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      LOGDISABLESTDOUT();
      zeros_ = READHEX("00000000");

      blkdir_ = string("./blkfiletest");
      homedir_ = string("./fakehomedir");
      ldbdir_ = string("./ldbtestdir");

      DBUtils::removeDirectory(blkdir_);
      DBUtils::removeDirectory(homedir_);
      DBUtils::removeDirectory(ldbdir_);

      mkdir(blkdir_ + "/blocks");
      mkdir(homedir_);
      mkdir(ldbdir_);

      // Put the first 5 blocks into the blkdir
      blk0dat_ = BtcUtils::getBlkFilename(blkdir_ + "/blocks", 0);
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
      if (clients_ != nullptr)
      {
         clients_->exitRequestLoop();
         clients_->shutdown();
      }

      Armory::Config::reset();
      delete clients_;
      delete theBDMt_;

      theBDMt_ = nullptr;
      clients_ = nullptr;

      DBUtils::removeDirectory(blkdir_);
      DBUtils::removeDirectory(homedir_);
      DBUtils::removeDirectory("./ldbtestdir");
      
      mkdir("./ldbtestdir");

      Armory::Config::reset();

      LOGENABLESTDOUT();
      CLEANUP_ALL_TIMERS();
   }

   LMDBBlockDatabase* iface_;
   BinaryData zeros_;

   string blkdir_;
   string homedir_;
   string ldbdir_;
   string blk0dat_;

   string wallet1id;
   string wallet2id;
   string LB1ID;
   string LB2ID;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, Load5Blocks)
{
   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   
   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   scrAddrVec.push_back(TestChain::scrAddrD);
   scrAddrVec.push_back(TestChain::scrAddrE);
   scrAddrVec.push_back(TestChain::scrAddrF);

   const vector<BinaryData> lb1ScrAddrs
   {
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const vector<BinaryData> lb2ScrAddrs
   {
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");
   DBTestUtils::regLockbox(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
   DBTestUtils::regLockbox(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
   auto wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);


   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 70*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 20*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 65*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 30*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrF);
   EXPECT_EQ(scrObj->getFullBalance(),  5*COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 5*COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 25*COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 30*COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0*COIN);

   EXPECT_EQ(wlt->getFullBalance(), 240*COIN);
   EXPECT_EQ(wltLB1->getFullBalance(), 30*COIN);
   EXPECT_EQ(wltLB2->getFullBalance(), 30*COIN);

   //cleanup
   bdvPtr.reset();
   wlt.reset();
   wltLB1.reset();
   wltLB2.reset();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, Load5Blocks_DamagedBlkFile)
{
   // this test should be reworked to be in terms of createTestChain.py
   string path(TestUtils::dataDir + "/botched_block.dat");
   BtcUtils::copyFile(path.c_str(), blk0dat_);

   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);

   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 100*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(),   0*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(),  50*COIN);

   EXPECT_EQ(wlt->getFullBalance(), 150 * COIN);

   //cleanup
   bdvPtr.reset();
   wlt.reset();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, Load4Blocks_Plus2)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   scrAddrVec.push_back(TestChain::scrAddrD);
   scrAddrVec.push_back(TestChain::scrAddrE);
   scrAddrVec.push_back(TestChain::scrAddrF);

   const vector<BinaryData> lb1ScrAddrs
   {
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const vector<BinaryData> lb2ScrAddrs
   {
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");
   DBTestUtils::regLockbox(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
   DBTestUtils::regLockbox(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);


   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
   auto wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);

   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 3U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, HEADERS), TestChain::blkHash3);
   auto header = theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash3);
   EXPECT_TRUE(header->isMainBranch());

   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(),  5*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 30*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrF);
   EXPECT_EQ(scrObj->getFullBalance(),  5*COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 10*COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(),  0*COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 10*COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(),  5*COIN);

   // Load the remaining blocks.
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);
   
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 5U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, HEADERS), TestChain::blkHash5);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash5)->isMainBranch());

   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 70*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 20*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 65*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 30*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrF);
   EXPECT_EQ(scrObj->getFullBalance(),  5*COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 5*COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 25*COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 30*COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0*COIN);

   //cleanup
   bdvPtr.reset();
   wlt.reset();
   wltLB1.reset();
   wltLB2.reset();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, Load5Blocks_FullReorg)
{
   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");

   scrAddrVec.clear();
   scrAddrVec.push_back(TestChain::scrAddrD);
   scrAddrVec.push_back(TestChain::scrAddrE);
   scrAddrVec.push_back(TestChain::scrAddrF);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet2");

   const vector<BinaryData> lb1ScrAddrs
   {
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const vector<BinaryData> lb2ScrAddrs
   {
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   DBTestUtils::regLockbox(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
   DBTestUtils::regLockbox(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wlt2 = bdvPtr->getWalletOrLockbox(wallet2id);
   auto wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
   auto wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5", "4A" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);

   TestUtils::appendBlocks({ "5A" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55*COIN);

   scrObj = wlt2->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(),60*COIN);
   scrObj = wlt2->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(),30*COIN);
   scrObj = wlt2->getScrAddrObjByKey(TestChain::scrAddrF);
   EXPECT_EQ(scrObj->getFullBalance(),60*COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 5*COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0*COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 10*COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0*COIN);

   EXPECT_EQ(wlt->getFullBalance(), 135*COIN);
   EXPECT_EQ(wlt2->getFullBalance(), 150*COIN);
   EXPECT_EQ(wltLB1->getFullBalance(), 5*COIN);
   EXPECT_EQ(wltLB2->getFullBalance(), 10*COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, Load5Blocks_DoubleReorg)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat_);
   
   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");

   scrAddrVec.clear();
   scrAddrVec.push_back(TestChain::scrAddrD);
   scrAddrVec.push_back(TestChain::scrAddrE);
   scrAddrVec.push_back(TestChain::scrAddrF);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet2");

   const vector<BinaryData> lb1ScrAddrs
   {
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const vector<BinaryData> lb2ScrAddrs
   {
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   DBTestUtils::regLockbox(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
   DBTestUtils::regLockbox(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);


   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wlt2 = bdvPtr->getWalletOrLockbox(wallet2id);
   auto wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
   auto wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);

   //first reorg: up to 5
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 70 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
   
   scrObj = wlt2->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 65 * COIN);
   scrObj = wlt2->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt2->getScrAddrObjByKey(TestChain::scrAddrF);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 25 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   EXPECT_EQ(wlt->getFullBalance(), 140 * COIN);
   EXPECT_EQ(wlt2->getFullBalance(), 100 * COIN);
   EXPECT_EQ(wltLB1->getFullBalance(), 30 * COIN);
   EXPECT_EQ(wltLB2->getFullBalance(), 30 * COIN);

   //second reorg: up to 5A
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);

   scrObj = wlt2->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 60 * COIN);
   scrObj = wlt2->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt2->getScrAddrObjByKey(TestChain::scrAddrF);
   EXPECT_EQ(scrObj->getFullBalance(), 60 * COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   EXPECT_EQ(wlt->getFullBalance(), 135 * COIN);
   EXPECT_EQ(wlt2->getFullBalance(), 150 * COIN);
   EXPECT_EQ(wltLB1->getFullBalance(), 5 * COIN);
   EXPECT_EQ(wltLB2->getFullBalance(), 10 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, Load5Blocks_ReloadBDM_Reorg)
{
   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");

   vector<BinaryData> scrAddrVec2;
   scrAddrVec2.push_back(TestChain::scrAddrD);
   scrAddrVec2.push_back(TestChain::scrAddrE);
   scrAddrVec2.push_back(TestChain::scrAddrF);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec2, "wallet2");

   const vector<BinaryData> lb1ScrAddrs
   {
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const vector<BinaryData> lb2ScrAddrs
   {
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   DBTestUtils::regLockbox(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
   DBTestUtils::regLockbox(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   //shutdown bdm
   bdvPtr.reset();
   clients_->exitRequestLoop();
   clients_->shutdown();

   delete clients_;
   delete theBDMt_;

   //add the reorg blocks
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5", "4A", "5A" }, blk0dat_);

   //restart bdm
   initBDM();

   theBDMt_->start(DBSettings::initMode());
   bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec2, "wallet2");
   DBTestUtils::regLockbox(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
   DBTestUtils::regLockbox(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

   bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wlt2 = bdvPtr->getWalletOrLockbox(wallet2id);
   auto wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
   auto wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);

   EXPECT_EQ(theBDMt_->bdm()->blockchain()->top()->getBlockHeight(), 5U);

   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA); //unspent 50
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB); //spent 50, spent 50, spent 25, spent 5, unspent 30
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC); //unspent 50, unspent 5
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);

   scrObj = wlt2->getScrAddrObjByKey(TestChain::scrAddrD); //unspent 5, unspent 50, unspent 5
   EXPECT_EQ(scrObj->getFullBalance(), 60 * COIN);
   scrObj = wlt2->getScrAddrObjByKey(TestChain::scrAddrE); //unspent 5, unspent 25
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt2->getScrAddrObjByKey(TestChain::scrAddrF); //spent 20, spent 15, unspent 5, unspent 50, unspent 5
   EXPECT_EQ(scrObj->getFullBalance(), 60 * COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr); //spent 10, unspent 5
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH); //spent 15
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr); //spent 10, unspent 10
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH); //spent 5
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   EXPECT_EQ(wlt->getFullBalance(), 135 * COIN);
   EXPECT_EQ(wlt2->getFullBalance(), 150 * COIN);
   EXPECT_EQ(wltLB1->getFullBalance(), 5 * COIN);
   EXPECT_EQ(wltLB2->getFullBalance(), 10 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, CorruptedBlock)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);

   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");

   const vector<BinaryData> lb1ScrAddrs
   {
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const vector<BinaryData> lb2ScrAddrs
   {
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   DBTestUtils::regLockbox(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
   DBTestUtils::regLockbox(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
   auto wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);

   {
      TestUtils::appendBlocks({ "4A", "5", "5A" }, blk0dat_);
      const uint64_t srcsz = BtcUtils::GetFileSize(blk0dat_);
      BinaryData temp(srcsz);
      {
         ifstream is(blk0dat_.c_str(), ios::in  | ios::binary);
         is.read((char*)temp.getPtr(), srcsz);
      }

      const std::string dst = blk0dat_;

      ofstream os(dst.c_str(), ios::out | ios::binary);
      os.write((char*)temp.getPtr(), 100);
      os.write((char*)temp.getPtr()+120, srcsz-100-20); // erase 20 bytes
   }

   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 70*COIN);

   EXPECT_EQ(wlt->getFullBalance(), 140*COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, Load5Blocks_RescanOps)
{
   shared_ptr<BtcWallet> wlt;
   shared_ptr<BtcWallet> wltLB1;
   shared_ptr<BtcWallet> wltLB2;

   auto startbdm = [&wlt, &wltLB1, &wltLB2, this](BDM_INIT_MODE init)->void
   {
      theBDMt_->start(init);
      auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

      vector<BinaryData> scrAddrVec;
      scrAddrVec.push_back(TestChain::scrAddrA);
      scrAddrVec.push_back(TestChain::scrAddrB);
      scrAddrVec.push_back(TestChain::scrAddrC);
      scrAddrVec.push_back(TestChain::scrAddrD);
      scrAddrVec.push_back(TestChain::scrAddrE);
      scrAddrVec.push_back(TestChain::scrAddrF);

      const vector<BinaryData> lb1ScrAddrs
      {
         TestChain::lb1ScrAddr,
         TestChain::lb1ScrAddrP2SH
      };
      const vector<BinaryData> lb2ScrAddrs
      {
         TestChain::lb2ScrAddr,
         TestChain::lb2ScrAddrP2SH
      };

      DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");
      DBTestUtils::regLockbox(
         clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
      DBTestUtils::regLockbox(
         clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

      auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

      //wait on signals
      DBTestUtils::goOnline(clients_, bdvID);
      DBTestUtils::waitOnBDMReady(clients_, bdvID);
      wlt = bdvPtr->getWalletOrLockbox(wallet1id);
      wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
      wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);
   };

   auto checkBalance = [&wlt, &wltLB1, &wltLB2](void)->void
   {
      const ScrAddrObj* scrObj;
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
      EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
      EXPECT_EQ(scrObj->getFullBalance(), 70 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
      EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
      EXPECT_EQ(scrObj->getFullBalance(), 65 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
      EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrF);
      EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
      scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
      EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
      scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
      EXPECT_EQ(scrObj->getFullBalance(), 25 * COIN);
      scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
      EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
      scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   };

   auto resetbdm = [&wlt, &wltLB1, &wltLB2, this](void)->void
   {
      wlt.reset();
      wltLB1.reset();
      wltLB2.reset();

      clients_->exitRequestLoop();
      clients_->shutdown();

      delete clients_;
      delete theBDMt_;

      initBDM();
   };

   //regular start
   startbdm(INIT_RESUME);
   checkBalance();

   //rebuild
   resetbdm();
   startbdm(INIT_REBUILD);
   checkBalance();

   //regular start
   resetbdm();
   startbdm(INIT_RESUME);
   checkBalance();

   //rescan
   resetbdm();
   startbdm(INIT_RESCAN);
   checkBalance();

   //regular start
   resetbdm();
   startbdm(INIT_RESUME);
   checkBalance();

   //rescanSSH
   resetbdm();
   startbdm(INIT_SSH);
   checkBalance();

   //regular start
   resetbdm();
   startbdm(INIT_RESUME);
   checkBalance();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, Load5Blocks_RescanEmptyDB)
{
   shared_ptr<BtcWallet> wlt;
   shared_ptr<BtcWallet> wltLB1;
   shared_ptr<BtcWallet> wltLB2;

   auto startbdm = [&wlt, &wltLB1, &wltLB2, this](BDM_INIT_MODE init)->void
   {
      theBDMt_->start(init);
      auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

      vector<BinaryData> scrAddrVec;
      scrAddrVec.push_back(TestChain::scrAddrA);
      scrAddrVec.push_back(TestChain::scrAddrB);
      scrAddrVec.push_back(TestChain::scrAddrC);
      scrAddrVec.push_back(TestChain::scrAddrD);
      scrAddrVec.push_back(TestChain::scrAddrE);
      scrAddrVec.push_back(TestChain::scrAddrF);

      const vector<BinaryData> lb1ScrAddrs
      {
         TestChain::lb1ScrAddr,
         TestChain::lb1ScrAddrP2SH
      };
      const vector<BinaryData> lb2ScrAddrs
      {
         TestChain::lb2ScrAddr,
         TestChain::lb2ScrAddrP2SH
      };

      DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");
      DBTestUtils::regLockbox(
         clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
      DBTestUtils::regLockbox(
         clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

      auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

      //wait on signals
      DBTestUtils::goOnline(clients_, bdvID);
      DBTestUtils::waitOnBDMReady(clients_, bdvID);
      wlt = bdvPtr->getWalletOrLockbox(wallet1id);
      wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
      wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);
   };

   auto checkBalance = [&wlt, &wltLB1, &wltLB2](void)->void
   {
      const ScrAddrObj* scrObj;
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
      EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
      EXPECT_EQ(scrObj->getFullBalance(), 70 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
      EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
      EXPECT_EQ(scrObj->getFullBalance(), 65 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
      EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrF);
      EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
      scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
      EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
      scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
      EXPECT_EQ(scrObj->getFullBalance(), 25 * COIN);
      scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
      EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
      scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   };

   //start with rebuild atop an empty db
   startbdm(INIT_RESCAN);
   checkBalance();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, Load5Blocks_RebuildEmptyDB)
{
   shared_ptr<BtcWallet> wlt;
   shared_ptr<BtcWallet> wltLB1;
   shared_ptr<BtcWallet> wltLB2;

   auto startbdm = [&wlt, &wltLB1, &wltLB2, this](BDM_INIT_MODE init)->void
   {
      theBDMt_->start(init);
      auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

      vector<BinaryData> scrAddrVec;
      scrAddrVec.push_back(TestChain::scrAddrA);
      scrAddrVec.push_back(TestChain::scrAddrB);
      scrAddrVec.push_back(TestChain::scrAddrC);
      scrAddrVec.push_back(TestChain::scrAddrD);
      scrAddrVec.push_back(TestChain::scrAddrE);
      scrAddrVec.push_back(TestChain::scrAddrF);

      const vector<BinaryData> lb1ScrAddrs
      {
         TestChain::lb1ScrAddr,
         TestChain::lb1ScrAddrP2SH
      };
      const vector<BinaryData> lb2ScrAddrs
      {
         TestChain::lb2ScrAddr,
         TestChain::lb2ScrAddrP2SH
      };

      DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");
      DBTestUtils::regLockbox(
         clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
      DBTestUtils::regLockbox(
         clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

      auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

      //wait on signals
      DBTestUtils::goOnline(clients_, bdvID);
      DBTestUtils::waitOnBDMReady(clients_, bdvID);
      wlt = bdvPtr->getWalletOrLockbox(wallet1id);
      wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
      wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);
   };

   auto checkBalance = [&wlt, &wltLB1, &wltLB2](void)->void
   {
      const ScrAddrObj* scrObj;
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
      EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
      EXPECT_EQ(scrObj->getFullBalance(), 70 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
      EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
      EXPECT_EQ(scrObj->getFullBalance(), 65 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
      EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrF);
      EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
      scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
      EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
      scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
      EXPECT_EQ(scrObj->getFullBalance(), 25 * COIN);
      scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
      EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
      scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   };

   //start with rebuild atop an empty db
   startbdm(INIT_REBUILD);
   checkBalance();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, Load5Blocks_SideScan)
{
   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);

   const vector<BinaryData> lb1ScrAddrs
   {
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const vector<BinaryData> lb2ScrAddrs
   {
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");
   DBTestUtils::regLockbox(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
   DBTestUtils::regLockbox(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);


   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
   auto wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);

   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 70 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 25 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   EXPECT_EQ(wlt->getFullBalance(), 140 * COIN);

   //post-init address registration
   scrAddrVec.clear();
   scrAddrVec.push_back(TestChain::scrAddrD);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");

   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 70 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 65 * COIN);
   EXPECT_EQ(scrObj->getPageCount(), 1U);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 25 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   EXPECT_EQ(wlt->getFullBalance(), 205 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, Load5Blocks_GetUtxos)
{
   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   scrAddrVec.push_back(TestChain::scrAddrD);
   scrAddrVec.push_back(TestChain::scrAddrE);
   scrAddrVec.push_back(TestChain::scrAddrF);

   const vector<BinaryData> lb1ScrAddrs
   {
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const vector<BinaryData> lb2ScrAddrs
   {
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");
   DBTestUtils::regLockbox(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
   DBTestUtils::regLockbox(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
   auto wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);

   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 70 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 65 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrF);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 25 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   EXPECT_EQ(wlt->getFullBalance(), 240 * COIN);
   EXPECT_EQ(wltLB1->getFullBalance(), 30 * COIN);
   EXPECT_EQ(wltLB2->getFullBalance(), 30 * COIN);

   //get all utxos, ignore zc
   auto spendableBalance = wlt->getSpendableBalance(5);
   auto&& utxoVec = wlt->getSpendableTxOutListForValue();

   uint64_t totalUtxoVal = 0;
   for (auto& utxo : utxoVec)
      totalUtxoVal += utxo.getValue();

   EXPECT_EQ(spendableBalance, totalUtxoVal);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsFull, Load5Blocks_CheckWalletFilters)
{
   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   vector<BinaryData> scrAddrVec1, scrAddrVec2;
   scrAddrVec1.push_back(TestChain::scrAddrA);
   scrAddrVec1.push_back(TestChain::scrAddrB);
   scrAddrVec1.push_back(TestChain::scrAddrC);

   scrAddrVec2.push_back(TestChain::scrAddrD);
   scrAddrVec2.push_back(TestChain::scrAddrE);
   scrAddrVec2.push_back(TestChain::scrAddrF);

   const vector<BinaryData> lb1ScrAddrs
   {
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const vector<BinaryData> lb2ScrAddrs
   {
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec1, "wallet1");
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec2, "wallet2");

   DBTestUtils::regLockbox(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
   DBTestUtils::regLockbox(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt1 = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wlt2 = bdvPtr->getWalletOrLockbox(wallet2id);
   auto wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
   auto wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);
   auto delegateID = DBTestUtils::getLedgerDelegate(clients_, bdvID);


   const ScrAddrObj* scrObj;
   scrObj = wlt1->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt1->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 70 * COIN);
   scrObj = wlt1->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
   scrObj = wlt2->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 65 * COIN);
   scrObj = wlt2->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt2->getScrAddrObjByKey(TestChain::scrAddrF);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 25 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   EXPECT_EQ(wlt1->getFullBalance(), 140 * COIN);
   EXPECT_EQ(wlt2->getFullBalance(), 100 * COIN);
   EXPECT_EQ(wltLB1->getFullBalance(), 30 * COIN);
   EXPECT_EQ(wltLB2->getFullBalance(), 30 * COIN);


   //grab delegate ledger
   auto&& delegateLedger1 = DBTestUtils::getHistoryPage(clients_, bdvID, delegateID, 0);

   unsigned wlt1_count = 0, wlt2_count = 0;
   for (auto& ledger : delegateLedger1)
   {
      if (ledger.getID() == "wallet1")
         ++wlt1_count;
      else if (ledger.getID() == "wallet2")
         ++wlt2_count;
   }

   EXPECT_EQ(wlt1_count, 11U);
   EXPECT_EQ(wlt2_count, 9U);

   vector<string> idVec;
   idVec.push_back(wallet1id);
   DBTestUtils::updateWalletsLedgerFilter(clients_, bdvID, idVec);
   BinaryData emptyBD;
   DBTestUtils::waitOnWalletRefresh(clients_, bdvID, emptyBD);

   auto&& delegateLedger2 = DBTestUtils::getHistoryPage(clients_, bdvID, delegateID, 0);

   wlt1_count = 0;
   wlt2_count = 0;
   for (auto& ledger : delegateLedger2)
   {
      if (ledger.getID() == "wallet1")
         ++wlt1_count;
      else if (ledger.getID() == "wallet2")
         ++wlt2_count;
   }

   EXPECT_EQ(wlt1_count, 11U);
   EXPECT_EQ(wlt2_count, 0U);
}

////////////////////////////////////////////////////////////////////////////////
class WebSocketTests_1Way : public ::testing::Test
{
protected:
   BlockDataManagerThread *theBDMt_;
   Clients* clients_;
   PassphraseLambda authPeersPassLbd_;

   void initBDM(void)
   {
      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();

      auto mockedShutdown = [](void)->void {};
      clients_ = new Clients(theBDMt_, mockedShutdown);
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      LOGDISABLESTDOUT();
      zeros_ = READHEX("00000000");

      blkdir_ = string("./blkfiletest");
      homedir_ = string("./fakehomedir");
      ldbdir_ = string("./ldbtestdir");

      DBUtils::removeDirectory(blkdir_);
      DBUtils::removeDirectory(homedir_);
      DBUtils::removeDirectory(ldbdir_);

      mkdir(blkdir_ + "/blocks");
      mkdir(homedir_);
      mkdir(ldbdir_);

      DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);

      // Put the first 5 blocks into the blkdir
      blk0dat_ = BtcUtils::getBlkFilename(blkdir_ + "/blocks", 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);

      Armory::Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_FULL",
         "--thread-count=3",
         "--public"},
         Armory::Config::ProcessType::DB);

      wallet1id = "wallet1";
      wallet2id = "wallet2";
      LB1ID = TestChain::lb1B58ID;
      LB2ID = TestChain::lb2B58ID;

      startupBIP151CTX();
      startupBIP150CTX(4);

      //setup auth peers for server and client
      authPeersPassLbd_ = [](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData();
      };

      AuthorizedPeers serverPeers(
         homedir_, SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_);
      AuthorizedPeers clientPeers(
         homedir_, CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_);

      //share public keys between client and server
      auto& serverPubkey = serverPeers.getOwnPublicKey();

      stringstream serverAddr;
      serverAddr << "127.0.0.1:" << NetworkSettings::listenPort();
      clientPeers.addPeer(serverPubkey, serverAddr.str());
      
      serverPubkey_ = BinaryData(serverPubkey.pubkey, 33);
      serverAddr_ = serverAddr.str();

      initBDM();

      auto nodePtr = dynamic_pointer_cast<NodeUnitTest>(
         NetworkSettings::bitcoinNodes().first);
      nodePtr->setIface(theBDMt_->bdm()->getIFace());
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      if (clients_ != nullptr)
      {
         clients_->exitRequestLoop();
         clients_->shutdown();
      }

      delete clients_;
      delete theBDMt_;

      theBDMt_ = nullptr;
      clients_ = nullptr;

      DBUtils::removeDirectory(blkdir_);
      DBUtils::removeDirectory(homedir_);
      DBUtils::removeDirectory("./ldbtestdir");

      Armory::Config::reset();

      LOGENABLESTDOUT();
      CLEANUP_ALL_TIMERS();
   }

   LMDBBlockDatabase* iface_;
   BinaryData zeros_;

   string blkdir_;
   string homedir_;
   string ldbdir_;
   string blk0dat_;

   string wallet1id;
   string wallet2id;
   string LB1ID;
   string LB2ID;
   BinaryData serverPubkey_;
   string serverAddr_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(WebSocketTests_1Way, WebSocketStack)
{   
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_, true);
   theBDMt_->start(DBSettings::initMode());

   auto pCallback = make_shared<DBTestUtils::UTCallback>();
   auto&& bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::listenPort(), 
      Armory::Config::getDataDir(),
      authPeersPassLbd_, 
      NetworkSettings::ephemeralPeers(), true, //public server
      pCallback);
   bdvObj->connectToRemote();
   bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());

   auto createNAddresses = [](unsigned count)->vector<BinaryData>
   {
      vector<BinaryData> result;

      for (unsigned i = 0; i < count; i++)
      {
         BinaryWriter bw;
         bw.put_uint8_t(SCRIPT_PREFIX_HASH160);

         auto&& addrData = CryptoPRNG::generateRandom(20);
         bw.put_BinaryData(addrData);

         result.push_back(bw.getData());
      }

      return result;
   };

   auto&& scrAddrVec = createNAddresses(2000);
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   scrAddrVec.push_back(TestChain::scrAddrE);

   const vector<BinaryData> lb1ScrAddrs
   {
      TestChain::lb1ScrAddr,
      TestChain::lb1ScrAddrP2SH
   };
   const vector<BinaryData> lb2ScrAddrs
   {
      TestChain::lb2ScrAddr,
      TestChain::lb2ScrAddrP2SH
   };

   vector<string> walletRegIDs;

   auto&& wallet1 = bdvObj->instantiateWallet("wallet1");
   walletRegIDs.push_back(
      wallet1.registerAddresses(scrAddrVec, false));

   auto&& lb1 = bdvObj->instantiateLockbox("lb1");
   walletRegIDs.push_back(
      lb1.registerAddresses(lb1ScrAddrs, false));

   auto&& lb2 = bdvObj->instantiateLockbox("lb2");
   walletRegIDs.push_back(
      lb2.registerAddresses(lb2ScrAddrs, false));

   //wait on registration ack
   pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

   //go online
   bdvObj->goOnline();
   pCallback->waitOnSignal(BDMAction_Ready);

   auto w1AddrBalances = DBTestUtils::getAddrBalancesFromDB(wallet1);
   vector<uint64_t> balanceVec;
   balanceVec = w1AddrBalances[TestChain::scrAddrA];
   EXPECT_EQ(balanceVec[0], 50 * COIN);
   balanceVec = w1AddrBalances[TestChain::scrAddrB];
   EXPECT_EQ(balanceVec[0], 30 * COIN);
   balanceVec = w1AddrBalances[TestChain::scrAddrC];
   EXPECT_EQ(balanceVec[0], 55 * COIN);

   auto w1Balances = DBTestUtils::getBalancesAndCount(wallet1, 4);
   uint64_t fullBalance = w1Balances[0];
   uint64_t spendableBalance = w1Balances[1];
   uint64_t unconfirmedBalance = w1Balances[2];
   EXPECT_EQ(fullBalance, 165 * COIN);
   EXPECT_EQ(spendableBalance, 65 * COIN);
   EXPECT_EQ(unconfirmedBalance, 165 * COIN);

   auto lb1AddrBalances = DBTestUtils::getAddrBalancesFromDB(lb1);
   balanceVec = lb1AddrBalances[TestChain::lb1ScrAddr];
   EXPECT_EQ(balanceVec[0], 10 * COIN);
   balanceVec = lb1AddrBalances[TestChain::lb1ScrAddrP2SH];
   EXPECT_EQ(balanceVec.size(), 0ULL);

   auto lb2AddrBalances = DBTestUtils::getAddrBalancesFromDB(lb2);
   balanceVec = lb2AddrBalances[TestChain::lb2ScrAddr];
   EXPECT_EQ(balanceVec[0], 10 * COIN);
   balanceVec = lb2AddrBalances[TestChain::lb2ScrAddrP2SH];
   EXPECT_EQ(balanceVec[0], 5 * COIN);

   auto lb1Balances = DBTestUtils::getBalancesAndCount(lb1, 4);
   EXPECT_EQ(lb1Balances[0], 10 * COIN);

   auto lb2Balances = DBTestUtils::getBalancesAndCount(lb2, 4);
   EXPECT_EQ(lb2Balances[0], 15 * COIN);

   //add ZC
   string zcPath(TestUtils::dataDir + "/ZCtx.tx");
   BinaryData rawZC(TestChain::zcTxSize);
   FILE *ff = fopen(zcPath.c_str(), "rb");
   fread(rawZC.getPtr(), TestChain::zcTxSize, 1, ff);
   fclose(ff);

   string lbPath(TestUtils::dataDir + "/LBZC.tx");
   BinaryData rawLBZC(TestChain::lbZCTxSize);
   FILE *flb = fopen(lbPath.c_str(), "rb");
   fread(rawLBZC.getPtr(), TestChain::lbZCTxSize, 1, flb);
   fclose(flb);

   DBTestUtils::ZcVector zcVec;
   zcVec.push_back(rawZC, 14000000);
   zcVec.push_back(rawLBZC, 14100000);

   vector<string> hashVec;
   auto hash1 = BtcUtils::getHash256(rawZC);
   auto hash2 = BtcUtils::getHash256(rawLBZC);
   hashVec.push_back(string(hash1.getCharPtr(), hash1.getSize()));
   hashVec.push_back(string(hash2.getCharPtr(), hash2.getSize()));

   DBTestUtils::pushNewZc(theBDMt_, zcVec);
   pCallback->waitOnManySignals(BDMAction_ZC, hashVec);

   w1AddrBalances = DBTestUtils::getAddrBalancesFromDB(wallet1);
   balanceVec = w1AddrBalances[TestChain::scrAddrA];
   //value didn't change, shouldnt be getting a balance vector for this address
   EXPECT_EQ(balanceVec.size(), 0ULL);
   balanceVec = w1AddrBalances[TestChain::scrAddrB];
   EXPECT_EQ(balanceVec[0], 20 * COIN);
   balanceVec = w1AddrBalances[TestChain::scrAddrC];
   EXPECT_EQ(balanceVec[0], 65 * COIN);

   w1Balances = DBTestUtils::getBalancesAndCount(wallet1, 4);
   fullBalance = w1Balances[0];
   spendableBalance = w1Balances[1];
   unconfirmedBalance = w1Balances[2];
   EXPECT_EQ(fullBalance, 165 * COIN);
   EXPECT_EQ(spendableBalance, 35 * COIN);
   EXPECT_EQ(unconfirmedBalance, 165 * COIN);

   lb1AddrBalances = DBTestUtils::getAddrBalancesFromDB(lb1);
   balanceVec = lb1AddrBalances[TestChain::lb1ScrAddr];
   EXPECT_EQ(balanceVec[0], 5 * COIN);
   balanceVec = lb1AddrBalances[TestChain::lb1ScrAddrP2SH];
   EXPECT_EQ(balanceVec.size(), 0ULL);

   lb2AddrBalances = DBTestUtils::getAddrBalancesFromDB(lb2);
   balanceVec = lb2AddrBalances[TestChain::lb2ScrAddr];
   EXPECT_EQ(balanceVec.size(), 0ULL);
   balanceVec = lb2AddrBalances[TestChain::lb2ScrAddrP2SH];
   EXPECT_EQ(balanceVec.size(), 0ULL);

   lb1Balances = DBTestUtils::getBalancesAndCount(lb1, 4);
   EXPECT_EQ(lb1Balances[0], 5 * COIN);

   lb2Balances = DBTestUtils::getBalancesAndCount(lb2, 4);
   EXPECT_EQ(lb2Balances[0], 15 * COIN);

   //
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   pCallback->waitOnSignal(BDMAction_NewBlock);

   w1AddrBalances = DBTestUtils::getAddrBalancesFromDB(wallet1);
   balanceVec = w1AddrBalances[TestChain::scrAddrA];
   //value didn't change, shouldnt be getting a balance vector for this address
   EXPECT_EQ(balanceVec.size(), 0ULL);
   balanceVec = w1AddrBalances[TestChain::scrAddrB];
   EXPECT_EQ(balanceVec[0], 70 * COIN);
   balanceVec = w1AddrBalances[TestChain::scrAddrC];
   EXPECT_EQ(balanceVec[0], 20 * COIN);

   w1Balances = DBTestUtils::getBalancesAndCount(wallet1, 5);
   fullBalance = w1Balances[0];
   spendableBalance = w1Balances[1];
   unconfirmedBalance = w1Balances[2];
   EXPECT_EQ(fullBalance, 170 * COIN);
   EXPECT_EQ(spendableBalance, 70 * COIN);
   EXPECT_EQ(unconfirmedBalance, 170 * COIN);

   lb1AddrBalances = DBTestUtils::getAddrBalancesFromDB(lb1);
   balanceVec = lb1AddrBalances[TestChain::lb1ScrAddr];
   EXPECT_EQ(balanceVec[0], 5 * COIN);
   balanceVec = lb1AddrBalances[TestChain::lb1ScrAddrP2SH];
   EXPECT_EQ(balanceVec[0], 25 * COIN);

   lb2AddrBalances = DBTestUtils::getAddrBalancesFromDB(lb2);
   balanceVec = lb2AddrBalances[TestChain::lb2ScrAddr];
   EXPECT_EQ(balanceVec[0], 30 * COIN);
   balanceVec = lb2AddrBalances[TestChain::lb2ScrAddrP2SH];
   EXPECT_EQ(balanceVec[0], 0 * COIN);

   lb1Balances = DBTestUtils::getBalancesAndCount(lb1, 5);
   EXPECT_EQ(lb1Balances[0], 30 * COIN);

   lb2Balances = DBTestUtils::getBalancesAndCount(lb2, 5);
   EXPECT_EQ(lb2Balances[0], 30 * COIN);

   //set wallet unconfirmed balance target to 2 blocks
   auto&& confId = wallet1.setUnconfirmedTarget(2);
   vector<string> confIdVec;
   confIdVec.push_back(confId);
   pCallback->waitOnManySignals(BDMAction_Refresh, confIdVec);

   //check new wallet balances
   w1Balances = DBTestUtils::getBalancesAndCount(wallet1, 5);
   fullBalance = w1Balances[0];
   spendableBalance = w1Balances[1];
   unconfirmedBalance = w1Balances[2];
   EXPECT_EQ(fullBalance, 170 * COIN);
   EXPECT_EQ(spendableBalance, 70 * COIN);
   EXPECT_EQ(unconfirmedBalance, 130 * COIN);


   //check rekey count
   auto rekeyCount = bdvObj->getRekeyCount();

   EXPECT_EQ(rekeyCount.first, 2U);
   EXPECT_EQ(rekeyCount.second, 1U);

   //cleanup
   bdvObj->shutdown(NetworkSettings::cookie());

   WebSocketServer::waitOnShutdown();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WebSocketTests_1Way, WebSocketStack_Reconnect)
{
   //
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   auto&& firstHash = READHEX("b6b6f145742a9072fd85f96772e63a00eb4101709aa34ec5dd59e8fc904191a7");
   theBDMt_ = new BlockDataManagerThread();
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_, true);


   auto pubkeyPrompt = [this](const BinaryData& pubkey, const string& name)->bool
   {
      if (pubkey != serverPubkey_ || name != serverAddr_)
         return false;

      return true;
   };

   auto createNAddresses = [](unsigned count)->vector<BinaryData>
   {
      vector<BinaryData> result;

      for (unsigned i = 0; i < count; i++)
      {
         BinaryWriter bw;
         bw.put_uint8_t(SCRIPT_PREFIX_HASH160);

         auto&& addrData = CryptoPRNG::generateRandom(20);
         bw.put_BinaryData(addrData);

         result.push_back(bw.getData());
      }

      return result;
   };

   auto&& scrAddrVec = createNAddresses(2000);
   theBDMt_->start(DBSettings::initMode());

   {
      auto pCallback = make_shared<DBTestUtils::UTCallback>();
      auto&& bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::listenPort(), 
         Armory::Config::getDataDir(),
         authPeersPassLbd_, 
         true, true, //public server
         pCallback);
      bdvObj->setCheckServerKeyPromptLambda(pubkeyPrompt);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());

      scrAddrVec.push_back(TestChain::scrAddrA);
      scrAddrVec.push_back(TestChain::scrAddrB);
      scrAddrVec.push_back(TestChain::scrAddrC);
      scrAddrVec.push_back(TestChain::scrAddrE);

      const vector<BinaryData> lb1ScrAddrs
      {
         TestChain::lb1ScrAddr,
         TestChain::lb1ScrAddrP2SH
      };
      const vector<BinaryData> lb2ScrAddrs
      {
         TestChain::lb2ScrAddr,
         TestChain::lb2ScrAddrP2SH
      };

      vector<string> walletRegIDs;
      auto&& wallet1 = bdvObj->instantiateWallet("wallet1");
      walletRegIDs.push_back(
         wallet1.registerAddresses(scrAddrVec, false));

      auto&& lb1 = bdvObj->instantiateLockbox("lb1");
      walletRegIDs.push_back(
         lb1.registerAddresses(lb1ScrAddrs, false));

      auto&& lb2 = bdvObj->instantiateLockbox("lb2");
      walletRegIDs.push_back(
         lb2.registerAddresses(lb2ScrAddrs, false));

      //wait on registration ack
      pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      auto w1AddrBalances = DBTestUtils::getAddrBalancesFromDB(wallet1);
      vector<uint64_t> balanceVec;
      balanceVec = w1AddrBalances[TestChain::scrAddrA];
      EXPECT_EQ(balanceVec[0], 50 * COIN);
      balanceVec = w1AddrBalances[TestChain::scrAddrB];
      EXPECT_EQ(balanceVec[0], 30 * COIN);
      balanceVec = w1AddrBalances[TestChain::scrAddrC];
      EXPECT_EQ(balanceVec[0], 55 * COIN);

      auto w1Balances = DBTestUtils::getBalancesAndCount(wallet1, 4);
      uint64_t fullBalance = w1Balances[0];
      uint64_t spendableBalance = w1Balances[1];
      uint64_t unconfirmedBalance = w1Balances[2];
      EXPECT_EQ(fullBalance, 165 * COIN);
      EXPECT_EQ(spendableBalance, 65 * COIN);
      EXPECT_EQ(unconfirmedBalance, 165 * COIN);

      auto lb1AddrBalances = DBTestUtils::getAddrBalancesFromDB(lb1);
      balanceVec = lb1AddrBalances[TestChain::lb1ScrAddr];
      EXPECT_EQ(balanceVec[0], 10 * COIN);
      balanceVec = lb1AddrBalances[TestChain::lb1ScrAddrP2SH];
      EXPECT_EQ(balanceVec.size(), 0ULL);

      auto lb2AddrBalances = DBTestUtils::getAddrBalancesFromDB(lb2);
      balanceVec = lb2AddrBalances[TestChain::lb2ScrAddr];
      EXPECT_EQ(balanceVec[0], 10 * COIN);
      balanceVec = lb2AddrBalances[TestChain::lb2ScrAddrP2SH];
      EXPECT_EQ(balanceVec[0], 5 * COIN);

      auto lb1Balances = DBTestUtils::getBalancesAndCount(lb1, 4);
      EXPECT_EQ(lb1Balances[0], 10 * COIN);

      auto lb2Balances = DBTestUtils::getBalancesAndCount(lb2, 4);
      EXPECT_EQ(lb2Balances[0], 15 * COIN);

      //
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
      DBTestUtils::triggerNewBlockNotification(theBDMt_);
      pCallback->waitOnSignal(BDMAction_NewBlock);

      w1AddrBalances = DBTestUtils::getAddrBalancesFromDB(wallet1);
      balanceVec = w1AddrBalances[TestChain::scrAddrA];
      //value didn't change, shouldnt be getting a balance vector for this address
      EXPECT_EQ(balanceVec.size(), 0ULL);
      balanceVec = w1AddrBalances[TestChain::scrAddrB];
      EXPECT_EQ(balanceVec[0], 70 * COIN);
      balanceVec = w1AddrBalances[TestChain::scrAddrC];
      EXPECT_EQ(balanceVec[0], 20 * COIN);

      w1Balances = DBTestUtils::getBalancesAndCount(wallet1, 5);
      fullBalance = w1Balances[0];
      spendableBalance = w1Balances[1];
      unconfirmedBalance = w1Balances[2];
      EXPECT_EQ(fullBalance, 170 * COIN);
      EXPECT_EQ(spendableBalance, 70 * COIN);
      EXPECT_EQ(unconfirmedBalance, 170 * COIN);

      lb1AddrBalances = DBTestUtils::getAddrBalancesFromDB(lb1);
      balanceVec = lb1AddrBalances[TestChain::lb1ScrAddr];
      EXPECT_EQ(balanceVec[0], 5 * COIN);
      balanceVec = lb1AddrBalances[TestChain::lb1ScrAddrP2SH];
      EXPECT_EQ(balanceVec[0], 25 * COIN);

      lb2AddrBalances = DBTestUtils::getAddrBalancesFromDB(lb2);
      balanceVec = lb2AddrBalances[TestChain::lb2ScrAddr];
      EXPECT_EQ(balanceVec[0], 30 * COIN);
      balanceVec = lb2AddrBalances[TestChain::lb2ScrAddrP2SH];
      EXPECT_EQ(balanceVec[0], 0 * COIN);

      lb1Balances = DBTestUtils::getBalancesAndCount(lb1, 5);
      EXPECT_EQ(lb1Balances[0], 30 * COIN);

      lb2Balances = DBTestUtils::getBalancesAndCount(lb2, 5);
      EXPECT_EQ(lb2Balances[0], 30 * COIN);

      bdvObj->unregisterFromDB();
   }

   for (int i = 0; i < 10; i++)
   {
      cout << ".iter " << i << endl;

      auto pCallback = make_shared<DBTestUtils::UTCallback>();
      auto&& bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::listenPort(), 
         Armory::Config::getDataDir(),
         authPeersPassLbd_, 
         true, true, //public server
         pCallback);
      bdvObj->setCheckServerKeyPromptLambda(pubkeyPrompt);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());

      const vector<BinaryData> lb1ScrAddrs
      {
         TestChain::lb1ScrAddr,
         TestChain::lb1ScrAddrP2SH
      };
      const vector<BinaryData> lb2ScrAddrs
      {
         TestChain::lb2ScrAddr,
         TestChain::lb2ScrAddrP2SH
      };

      vector<string> walletRegIDs;

      auto&& wallet1 = bdvObj->instantiateWallet("wallet1");
      walletRegIDs.push_back(
         wallet1.registerAddresses(scrAddrVec, false));

      auto&& lb1 = bdvObj->instantiateLockbox("lb1");
      walletRegIDs.push_back(
         lb1.registerAddresses(lb1ScrAddrs, false));

      auto&& lb2 = bdvObj->instantiateLockbox("lb2");
      walletRegIDs.push_back(
         lb2.registerAddresses(lb2ScrAddrs, false));

      //wait on registration ack
      pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      auto w1AddrBalances = DBTestUtils::getAddrBalancesFromDB(wallet1);
      auto balanceVec = w1AddrBalances[TestChain::scrAddrA];
      EXPECT_EQ(balanceVec[0], 50 * COIN);
      balanceVec = w1AddrBalances[TestChain::scrAddrB];
      EXPECT_EQ(balanceVec[0], 70 * COIN);
      balanceVec = w1AddrBalances[TestChain::scrAddrC];
      EXPECT_EQ(balanceVec[0], 20 * COIN);

      auto w1Balances = DBTestUtils::getBalancesAndCount(wallet1, 5);
      auto fullBalance = w1Balances[0];
      auto spendableBalance = w1Balances[1];
      auto unconfirmedBalance = w1Balances[2];
      EXPECT_EQ(fullBalance, 170 * COIN);
      EXPECT_EQ(spendableBalance, 70 * COIN);
      EXPECT_EQ(unconfirmedBalance, 170 * COIN);

      auto lb1AddrBalances = DBTestUtils::getAddrBalancesFromDB(lb1);
      balanceVec = lb1AddrBalances[TestChain::lb1ScrAddr];
      EXPECT_EQ(balanceVec[0], 5 * COIN);
      balanceVec = lb1AddrBalances[TestChain::lb1ScrAddrP2SH];
      EXPECT_EQ(balanceVec[0], 25 * COIN);

      auto lb2AddrBalances = DBTestUtils::getAddrBalancesFromDB(lb2);
      balanceVec = lb2AddrBalances[TestChain::lb2ScrAddr];
      EXPECT_EQ(balanceVec[0], 30 * COIN);
      balanceVec = lb2AddrBalances[TestChain::lb2ScrAddrP2SH];
      EXPECT_EQ(balanceVec.size(), 0ULL);

      auto lb1Balances = DBTestUtils::getBalancesAndCount(lb1, 5);
      EXPECT_EQ(lb1Balances[0], 30 * COIN);

      auto lb2Balances = DBTestUtils::getBalancesAndCount(lb2, 5);
      EXPECT_EQ(lb2Balances[0], 30 * COIN);

      //grab main ledgers
      auto&& delegate = DBTestUtils::getLedgerDelegate(bdvObj);
      auto&& ledgers = DBTestUtils::getHistoryPage(delegate, 0);
      auto& firstEntry = ledgers[0];
      auto txHash = firstEntry.getTxHash();
      EXPECT_EQ(firstHash, txHash);

      auto&& tx = DBTestUtils::getTxByHash(bdvObj, firstHash);
      EXPECT_EQ(tx->getThisHash(), firstHash);

      bdvObj->unregisterFromDB();
   }

   auto&& bdvObj2 = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::listenPort(), Armory::Config::getDataDir(),
     authPeersPassLbd_, true, true, nullptr);
   bdvObj2->setCheckServerKeyPromptLambda(pubkeyPrompt);
   bdvObj2->connectToRemote();

   bdvObj2->shutdown(NetworkSettings::cookie());
   WebSocketServer::waitOnShutdown();
}

////////////////////////////////////////////////////////////////////////////////
class WebSocketTests_2Way : public ::testing::Test
{
protected:
   BlockDataManagerThread *theBDMt_;
   Clients* clients_;
   PassphraseLambda authPeersPassLbd_;

   void initBDM(void)
   {
      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();

      auto mockedShutdown = [](void)->void {};
      clients_ = new Clients(theBDMt_, mockedShutdown);
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      LOGDISABLESTDOUT();
      zeros_ = READHEX("00000000");

      blkdir_ = string("./blkfiletest");
      homedir_ = string("./fakehomedir");
      ldbdir_ = string("./ldbtestdir");

      DBUtils::removeDirectory(blkdir_);
      DBUtils::removeDirectory(homedir_);
      DBUtils::removeDirectory(ldbdir_);

      mkdir(blkdir_ + "/blocks");
      mkdir(homedir_);
      mkdir(ldbdir_);

      DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);

      // Put the first 5 blocks into the blkdir
      blk0dat_ = BtcUtils::getBlkFilename(blkdir_ + "/blocks", 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);

      Armory::Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_FULL",
         "--thread-count=3"},
         Armory::Config::ProcessType::DB);

      wallet1id = "wallet1";
      wallet2id = "wallet2";
      LB1ID = TestChain::lb1B58ID;
      LB2ID = TestChain::lb2B58ID;

      startupBIP151CTX();
      startupBIP150CTX(4);

      //setup auth peers for server and client
      authPeersPassLbd_ = [](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData();
      };

      AuthorizedPeers serverPeers(
         homedir_, SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_);
      AuthorizedPeers clientPeers(
         homedir_, CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_);

      //share public keys between client and server
      auto& serverPubkey = serverPeers.getOwnPublicKey();
      auto& clientPubkey = clientPeers.getOwnPublicKey();

      stringstream serverAddr;
      serverAddr << "127.0.0.1:" << NetworkSettings::listenPort();
      clientPeers.addPeer(serverPubkey, serverAddr.str());
      serverPeers.addPeer(clientPubkey, "127.0.0.1");
      
      serverPubkey_ = BinaryData(serverPubkey.pubkey, 33);
      serverAddr_ = serverAddr.str();

      initBDM();

      auto nodePtr = dynamic_pointer_cast<NodeUnitTest>(
         NetworkSettings::bitcoinNodes().first);
      nodePtr->setIface(theBDMt_->bdm()->getIFace());
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      if (clients_ != nullptr)
      {
         clients_->exitRequestLoop();
         clients_->shutdown();
      }

      delete clients_;
      delete theBDMt_;

      theBDMt_ = nullptr;
      clients_ = nullptr;

      DBUtils::removeDirectory(blkdir_);
      DBUtils::removeDirectory(homedir_);
      DBUtils::removeDirectory("./ldbtestdir");

      Armory::Config::reset();

      LOGENABLESTDOUT();
      CLEANUP_ALL_TIMERS();
   }

   LMDBBlockDatabase* iface_;
   BinaryData zeros_;

   string blkdir_;
   string homedir_;
   string ldbdir_;
   string blk0dat_;

   string wallet1id;
   string wallet2id;
   string LB1ID;
   string LB2ID;
   BinaryData serverPubkey_;
   string serverAddr_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(WebSocketTests_2Way, GrabAddrLedger_PostReg)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   theBDMt_ = new BlockDataManagerThread();
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_, true);
   theBDMt_->start(DBSettings::initMode());

   auto pCallback = make_shared<DBTestUtils::UTCallback>();
   auto&& bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::listenPort(), 
      Armory::Config::getDataDir(),
      authPeersPassLbd_, 
      NetworkSettings::ephemeralPeers(), false, //private server
      pCallback);
   bdvObj->connectToRemote();
   bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);

   //wait on signals
   bdvObj->goOnline();
   pCallback->waitOnSignal(BDMAction_Ready);

   const auto &walletId = CryptoPRNG::generateRandom(8).toHexStr();
   auto&& wallet = bdvObj->instantiateWallet(walletId);
   auto&& registrationId = wallet.registerAddresses(scrAddrVec, false);
   pCallback->waitOnSignal(BDMAction_Refresh, registrationId);

   auto w1AddrBalances = DBTestUtils::getAddrBalancesFromDB(wallet);
   ASSERT_NE(w1AddrBalances.size(), 0ULL);
   vector<uint64_t> balanceVec;
   balanceVec = w1AddrBalances[TestChain::scrAddrA];	// crashes here, too
   EXPECT_EQ(balanceVec[0], 50 * COIN);
   balanceVec = w1AddrBalances[TestChain::scrAddrB];
   EXPECT_EQ(balanceVec[0], 30 * COIN);
   balanceVec = w1AddrBalances[TestChain::scrAddrC];
   EXPECT_EQ(balanceVec[0], 55 * COIN);

   auto ledgerDelegate = DBTestUtils::getLedgerDelegateForScrAddr(
      bdvObj, walletId, TestChain::scrAddrA);
   EXPECT_FALSE(DBTestUtils::getHistoryPage(ledgerDelegate, 0).empty());

   //cleanup
   bdvObj->shutdown(NetworkSettings::cookie());
   WebSocketServer::waitOnShutdown();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WebSocketTests_2Way, WebSocketStack_ManyZC)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   theBDMt_ = new BlockDataManagerThread();
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_, true);

   theBDMt_->start(DBSettings::initMode());

   auto pCallback = make_shared<DBTestUtils::UTCallback>();
   auto&& bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::listenPort(), 
      Armory::Config::getDataDir(),
      authPeersPassLbd_, 
      NetworkSettings::ephemeralPeers(), false, //private server
      pCallback);
   bdvObj->connectToRemote();
   bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   scrAddrVec.push_back(TestChain::scrAddrE);

   vector<string> walletRegIDs;
   auto&& wallet1 = bdvObj->instantiateWallet("wallet1");
   walletRegIDs.push_back(
      wallet1.registerAddresses(scrAddrVec, false));

   //wait on registration ack
   pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

   //go online
   bdvObj->goOnline();
   pCallback->waitOnSignal(BDMAction_Ready);

   auto w1AddrBalances = DBTestUtils::getAddrBalancesFromDB(wallet1);
   vector<uint64_t> balanceVec;
   balanceVec = w1AddrBalances[TestChain::scrAddrA];
   EXPECT_EQ(balanceVec[0], 50 * COIN);
   balanceVec = w1AddrBalances[TestChain::scrAddrB];
   EXPECT_EQ(balanceVec[0], 30 * COIN);
   balanceVec = w1AddrBalances[TestChain::scrAddrC];
   EXPECT_EQ(balanceVec[0], 55 * COIN);

   auto w1Balances = DBTestUtils::getBalancesAndCount(wallet1, 4);
   uint64_t fullBalance = w1Balances[0];
   uint64_t spendableBalance = w1Balances[1];
   uint64_t unconfirmedBalance = w1Balances[2];
   EXPECT_EQ(fullBalance, 165 * COIN);
   EXPECT_EQ(spendableBalance, 65 * COIN);
   EXPECT_EQ(unconfirmedBalance, 165 * COIN);

   //signer feed
   auto feed = make_shared<ResolverUtils::TestResolverFeed>();
   feed->addPrivKey(TestChain::privKeyAddrB);
   feed->addPrivKey(TestChain::privKeyAddrC);
   feed->addPrivKey(TestChain::privKeyAddrE);

   //create spender lambda
   auto getSpenderPtr = [](const UTXO& utxo)->shared_ptr<ScriptSpender>
   {
      auto spender = make_shared<ScriptSpender>(utxo);
      spender->setSequence(UINT32_MAX - 2);

      return spender;
   };

   //add 100 ZC
   vector<BinaryData> allZcHash;
   for (int i = 0; i < 100; i++)
   {
      size_t spendVal = 1000000;
      Signer signer;

      //get utxo list for spend value
      auto&& unspentVec = DBTestUtils::getSpendableTxOutListForValue(wallet1, spendVal);
      auto&& zcOutputsVec = DBTestUtils::getSpendableZCList(wallet1);

      unspentVec.insert(unspentVec.end(),
         zcOutputsVec.begin(), zcOutputsVec.end());

      vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end())
      {
         tval += utxoIter->getValue();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal)
            break;

         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec)
      {
         total += utxo.getValue();
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spendVal to scrAddrD
      auto recipientD = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrE.getSliceCopy(1, 20), spendVal);
      signer.addRecipient(recipientD);

      //change to scrAddrE, no fee
      if (total > spendVal)
      {
         //deal with change, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      auto rawTx = signer.serializeSignedTx();
      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(rawTx, 14000000);

      auto&& ZCHash = BtcUtils::getHash256(rawTx);
      allZcHash.push_back(ZCHash);
      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      pCallback->waitOnSignal(BDMAction_ZC, string(ZCHash.toCharPtr(), ZCHash.getSize()));
   }

   //grab ledger, check all zc hash are in there
   auto&& ledgerDelegate = DBTestUtils::getLedgerDelegate(bdvObj);
   auto count = DBTestUtils::getPageCount(ledgerDelegate);
   EXPECT_EQ(count, 1U);

   auto&& history = DBTestUtils::getHistoryPage(ledgerDelegate, 0);
   set<BinaryData> ledgerHashes;
   for (auto& le : history)
      ledgerHashes.insert(le.getTxHash());

   for (auto& zcHash : allZcHash)
   {
      auto iter = ledgerHashes.find(zcHash);
      EXPECT_TRUE(iter != ledgerHashes.end());
   }

   //cleanup
   bdvObj->shutdown(NetworkSettings::cookie());
   WebSocketServer::waitOnShutdown();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Now actually execute all the tests
////////////////////////////////////////////////////////////////////////////////
GTEST_API_ int main(int argc, char **argv) 
{
   #ifdef _MSC_VER
      _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
   
      WSADATA wsaData;
      WORD wVersion = MAKEWORD(2, 0);
      WSAStartup(wVersion, &wsaData);
   #endif

   GOOGLE_PROTOBUF_VERIFY_VERSION;
   srand(time(0));
   std::cout << "Running main() from gtest_main.cc\n";

   // Required by libbtc.
   CryptoECDSA::setupContext();

   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();

   // Required by libbtc.
   CryptoECDSA::shutdown();

   FLUSHLOG();
   CLEANUPLOG();

   return exitCode;
}

//TODO: add test to merge new addresses on reorg
