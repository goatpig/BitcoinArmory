////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig                                               //            
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                   
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#include "TestUtils.h"


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class DB1kIter : public ::testing::Test
{
protected:
   BlockDataManagerThread *theBDMt_;
   Clients* clients_;

   void initBDM(void)
   {
      theBDMt_ = new BlockDataManagerThread(config);
      iface_ = theBDMt_->bdm()->getIFace();

      auto mockedShutdown = [](void)->void {};
      clients_ = new Clients(theBDMt_, mockedShutdown);
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      LOGDISABLESTDOUT();
      magic_ = READHEX(MAINNET_MAGIC_BYTES);
      ghash_ = READHEX(MAINNET_GENESIS_HASH_HEX);
      gentx_ = READHEX(MAINNET_GENESIS_TX_HASH_HEX);
      zeros_ = READHEX("00000000");

      blkdir_ = std::string("./blkfiletest");
      homedir_ = std::string("./fakehomedir");
      ldbdir_ = std::string("./ldbtestdir");

      rmdir(blkdir_);
      rmdir(homedir_);
      rmdir(ldbdir_);

      mkdir(blkdir_);
      mkdir(homedir_);
      mkdir(ldbdir_);

      // Put the first 5 blocks into the blkdir
      blk0dat_ = BtcUtils::getBlkFilename(blkdir_, 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);

      BlockDataManagerConfig::setDbType(ARMORY_DB_BARE);
      config.blkFileLocation_ = blkdir_;
      config.dbDir_ = ldbdir_;
      config.threadCount_ = 3;
      config.nodeType_ = Node_UnitTest;

      wallet1id = BinaryData("wallet1");
      wallet2id = BinaryData("wallet2");
      LB1ID = BinaryData(TestChain::lb1B58ID);
      LB2ID = BinaryData(TestChain::lb2B58ID);

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

      delete clients_;
      delete theBDMt_;

      theBDMt_ = nullptr;
      clients_ = nullptr;

      rmdir(blkdir_);
      rmdir(homedir_);

#ifdef _MSC_VER
      rmdir("./ldbtestdir");
      mkdir("./ldbtestdir");
#else
      std::string delstr = ldbdir_ + "/*";
      rmdir(delstr);
#endif
      LOGENABLESTDOUT();
      CLEANUP_ALL_TIMERS();
   }

   BlockDataManagerConfig config;

   LMDBBlockDatabase* iface_;
   BinaryData magic_;
   BinaryData ghash_;
   BinaryData gentx_;
   BinaryData zeros_;

   std::string blkdir_;
   std::string homedir_;
   std::string ldbdir_;
   std::string blk0dat_;

   BinaryData wallet1id;
   BinaryData wallet2id;
   BinaryData LB1ID;
   BinaryData LB2ID;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(DB1kIter, DbInit1kIter)
{
   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);

   std::vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   scrAddrVec.push_back(TestChain::scrAddrD);
   scrAddrVec.push_back(TestChain::scrAddrE);
   scrAddrVec.push_back(TestChain::scrAddrF);

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

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");
   DBTestUtils::regLockbox(clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
   DBTestUtils::regLockbox(clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   clients_->exitRequestLoop();
   clients_->shutdown();

   delete clients_;
   delete theBDMt_;

   auto fakeprog = [](BDMPhase, double, unsigned, unsigned)->void
   {};

   for (unsigned i = 0; i<1000; i++)
   {
      std::cout << "iter: " << i << std::endl;
      initBDM();
      auto bdm = theBDMt_->bdm();
      bdm->doInitialSyncOnLoad_Rebuild(fakeprog);

      clients_->exitRequestLoop();
      clients_->shutdown();

      delete clients_;
      delete theBDMt_;
   }

   //one last init so that TearDown doesn't blow up
   initBDM();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(DB1kIter, DbInit1kIter_WithSignals)
{

   std::vector<BinaryData> scrAddrVec;

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

   for (unsigned i = 0; i < 1000; i++)
   {
      scrAddrVec.clear();

      scrAddrVec.push_back(TestChain::scrAddrA);
      scrAddrVec.push_back(TestChain::scrAddrB);
      scrAddrVec.push_back(TestChain::scrAddrC);
      scrAddrVec.push_back(TestChain::scrAddrE);

      TestUtils::setBlocks({ "0", "1", "2" }, blk0dat_);

      theBDMt_->start(DBSettings::initMode());
      auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);

      DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");
      DBTestUtils::regLockbox(clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
      DBTestUtils::regLockbox(clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

      auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

      //wait on signals
      DBTestUtils::goOnline(clients_, bdvID);
      DBTestUtils::waitOnBDMReady(clients_, bdvID);
      auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
      auto wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
      auto wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);

      EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 2);
      EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, HEADERS), TestChain::blkHash2);
      EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash2)->isMainBranch());

      const ScrAddrObj* scrObj;
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
      EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
      EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

      uint64_t fullBalance = wlt->getFullBalance();
      uint64_t spendableBalance = wlt->getSpendableBalance(3);
      uint64_t unconfirmedBalance = wlt->getUnconfirmedBalance(3);
      EXPECT_EQ(fullBalance, 105 * COIN);
      EXPECT_EQ(spendableBalance, 5 * COIN);
      EXPECT_EQ(unconfirmedBalance, 105 * COIN);

      //add ZC
      BinaryData rawZC(259);
      FILE *ff = fopen("../reorgTest/ZCtx.tx", "rb");
      fread(rawZC.getPtr(), 259, 1, ff);
      fclose(ff);
      DBTestUtils::ZcVector rawZcVec;
      rawZcVec.push_back(std::move(rawZC), 1300000000);

      BinaryData ZChash = READHEX(TestChain::zcTxHash256);

      DBTestUtils::pushNewZc(theBDMt_, rawZcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
      EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
      EXPECT_EQ(scrObj->getFullBalance(), 75 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
      EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);

      fullBalance = wlt->getFullBalance();
      spendableBalance = wlt->getSpendableBalance(4);
      unconfirmedBalance = wlt->getUnconfirmedBalance(4);
      EXPECT_EQ(fullBalance, 135 * COIN);
      EXPECT_EQ(spendableBalance, 5 * COIN);
      EXPECT_EQ(unconfirmedBalance, 135 * COIN);

      //check ledger for ZC
      /*LedgerEntry le = wlt->getLedgerEntryForTx(ZChash);
      EXPECT_EQ(le.getTxTime(), 1300000000);
      EXPECT_EQ(le.getValue(), 3000000000);
      EXPECT_EQ(le.getBlockNum(), UINT32_MAX);*/

      //pull ZC from DB, verify it's carrying the proper data
      auto&& dbtx = iface_->beginTransaction(ZERO_CONF, LMDB::ReadOnly);
      StoredTx zcStx;
      BinaryData zcKey = WRITE_UINT16_BE(0xFFFF);
      zcKey.append(WRITE_UINT32_LE(0));

      EXPECT_EQ(iface_->getStoredZcTx(zcStx, zcKey), true);
      EXPECT_EQ(zcStx.thisHash_, ZChash);
      EXPECT_EQ(zcStx.numBytes_, TestChain::zcTxSize);
      EXPECT_EQ(zcStx.fragBytes_, 190);
      EXPECT_EQ(zcStx.numTxOut_, 2);
      EXPECT_EQ(zcStx.stxoMap_.begin()->second.getValue(), 10 * COIN);

      //check ZChash in DB
      EXPECT_EQ(iface_->getTxHashForLdbKey(zcKey), ZChash);
      dbtx.reset();

      //restart bdm
      bdvPtr.reset();
      wlt.reset();
      wltLB1.reset();
      wltLB2.reset();

      clients_->exitRequestLoop();
      clients_->shutdown();

      delete clients_;
      delete theBDMt_;

      initBDM();

      theBDMt_->start(DBSettings::initMode());
      bdvID = DBTestUtils::registerBDV(clients_, magic_);

      scrAddrVec.pop_back();
      DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");
      DBTestUtils::regLockbox(clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID);
      DBTestUtils::regLockbox(clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID);

      bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

      //wait on signals
      DBTestUtils::goOnline(clients_, bdvID);
      DBTestUtils::waitOnBDMReady(clients_, bdvID);
      wlt = bdvPtr->getWalletOrLockbox(wallet1id);
      wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
      wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);

      //add 4th block
      TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
      DBTestUtils::triggerNewBlockNotification(theBDMt_);
      DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

      EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 3);
      EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, HEADERS), TestChain::blkHash3);
      EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash3)->isMainBranch());

      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
      EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
      EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
      EXPECT_EQ(scrObj->getFullBalance(), 65 * COIN);

      fullBalance = wlt->getFullBalance();
      spendableBalance = wlt->getSpendableBalance(5);
      unconfirmedBalance = wlt->getUnconfirmedBalance(5);
      EXPECT_EQ(fullBalance, 135 * COIN);
      EXPECT_EQ(spendableBalance, 5 * COIN);
      EXPECT_EQ(unconfirmedBalance, 105 * COIN);

      /*le = wlt->getLedgerEntryForTx(ZChash);
      EXPECT_EQ(le.getTxTime(), 1300000000);
      EXPECT_EQ(le.getValue(), 3000000000);
      EXPECT_EQ(le.getBlockNum(), UINT32_MAX);*/

      //The BDM was recycled, but the ZC is still live, and the mempool should 
      //have reloaded it. Pull from DB and verify
      dbtx = move(iface_->beginTransaction(ZERO_CONF, LMDB::ReadOnly));
      StoredTx zcStx2;

      EXPECT_EQ(iface_->getStoredZcTx(zcStx2, zcKey), true);
      EXPECT_EQ(zcStx2.thisHash_, ZChash);
      EXPECT_EQ(zcStx2.numBytes_, TestChain::zcTxSize);
      EXPECT_EQ(zcStx2.fragBytes_, 190);
      EXPECT_EQ(zcStx2.numTxOut_, 2);
      EXPECT_EQ(zcStx2.stxoMap_.begin()->second.getValue(), 10 * COIN);

      dbtx.reset();

      //add 5th block
      TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);
      DBTestUtils::triggerNewBlockNotification(theBDMt_);
      DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

      EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 4);
      EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, HEADERS), TestChain::blkHash4);
      EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash4)->isMainBranch());

      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
      EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
      EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
      EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);

      fullBalance = wlt->getFullBalance();
      spendableBalance = wlt->getSpendableBalance(5);
      unconfirmedBalance = wlt->getUnconfirmedBalance(5);
      EXPECT_EQ(fullBalance, 90 * COIN);
      EXPECT_EQ(spendableBalance, 10 * COIN);
      EXPECT_EQ(unconfirmedBalance, 60 * COIN);

      dbtx = move(iface_->beginTransaction(ZERO_CONF, LMDB::ReadOnly));
      StoredTx zcStx3;

      EXPECT_EQ(iface_->getStoredZcTx(zcStx3, zcKey), true);
      EXPECT_EQ(zcStx3.thisHash_, ZChash);
      EXPECT_EQ(zcStx3.numBytes_, TestChain::zcTxSize);
      EXPECT_EQ(zcStx3.fragBytes_, 190); // Not sure how Python can get this value
      EXPECT_EQ(zcStx3.numTxOut_, 2);
      EXPECT_EQ(zcStx3.stxoMap_.begin()->second.getValue(), 10 * COIN);

      dbtx.reset();

      //add 6th block
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
      DBTestUtils::triggerNewBlockNotification(theBDMt_);
      DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

      EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 5);
      EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, HEADERS), TestChain::blkHash5);
      EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash5)->isMainBranch());

      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
      EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
      EXPECT_EQ(scrObj->getFullBalance(), 70 * COIN);
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
      EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);

      fullBalance = wlt->getFullBalance();
      spendableBalance = wlt->getSpendableBalance(5);
      unconfirmedBalance = wlt->getUnconfirmedBalance(5);
      EXPECT_EQ(fullBalance, 140 * COIN);
      EXPECT_EQ(spendableBalance, 40 * COIN);
      EXPECT_EQ(unconfirmedBalance, 140 * COIN);

      /*le = wlt->getLedgerEntryForTx(ZChash);
      EXPECT_EQ(le.getTxTime(), 1231009513);
      EXPECT_EQ(le.getValue(), 3000000000);
      EXPECT_EQ(le.getBlockNum(), 5);*/

      //Tx is now in a block, ZC should be gone from DB
      dbtx = move(iface_->beginTransaction(ZERO_CONF, LMDB::ReadWrite));
      StoredTx zcStx4;

      EXPECT_EQ(iface_->getStoredZcTx(zcStx4, zcKey), false);

      dbtx.reset();

      bdvPtr.reset();
      wlt.reset();
      wltLB1.reset();
      wltLB2.reset();

      clients_->exitRequestLoop();
      clients_->shutdown();

      delete clients_;
      delete theBDMt_;

      std::cout << i << std::endl;

      rmdir(blkdir_);
      rmdir(homedir_);
      rmdir(ldbdir_);

      mkdir(blkdir_);
      mkdir(homedir_);
      mkdir(ldbdir_);

      initBDM();
   }
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

   std::cout << "Running main() from gtest_main.cc\n";

   LOGDISABLESTDOUT();

   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();

   FLUSHLOG();
   CLEANUPLOG();

   return exitCode;
}
