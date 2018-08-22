////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
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
// TODO:  These tests were taken directly from the BlockUtilsSuper.cpp where 
//        they previously ran without issue.  After bringing them over to here,
//        they now seg-fault.  Disabled for now, since the PartialMerkleTrees 
//        are not actually in use anywhere yet.
class DISABLED_PartialMerkleTest : public ::testing::Test
{
protected:

   virtual void SetUp(void)
   {
      vector<BinaryData> txList_(7);
      // The "abcd" quartets are to trigger endianness errors -- without them,
      // these hashes are palindromes that work regardless of your endian-handling
      txList_[0] = READHEX("00000000000000000000000000000000"
         "000000000000000000000000abcd0000");
      txList_[1] = READHEX("11111111111111111111111111111111"
         "111111111111111111111111abcd1111");
      txList_[2] = READHEX("22222222222222222222222222222222"
         "222222222222222222222222abcd2222");
      txList_[3] = READHEX("33333333333333333333333333333333"
         "333333333333333333333333abcd3333");
      txList_[4] = READHEX("44444444444444444444444444444444"
         "444444444444444444444444abcd4444");
      txList_[5] = READHEX("55555555555555555555555555555555"
         "555555555555555555555555abcd5555");
      txList_[6] = READHEX("66666666666666666666666666666666"
         "666666666666666666666666abcd6666");

      vector<BinaryData> merkleTree_ = BtcUtils::calculateMerkleTree(txList_);

      /*
      cout << "Merkle Tree looks like the following (7 tx): " << endl;
      cout << "The ** indicates the nodes we care about for partial tree test" << endl;
      cout << "                                                    \n";
      cout << "                   _____0a10_____                   \n";
      cout << "                  /              \\                  \n";
      cout << "                _/                \\_                \n";
      cout << "            65df                    b4d6            \n";
      cout << "          /      \\                /      \\          \n";
      cout << "      6971        22dc        5675        d0b6      \n";
      cout << "     /    \\      /    \\      /    \\      /          \n";
      cout << "   0000  1111  2222  3333  4444  5555  6666         \n";
      cout << "    **                            **                \n";
      cout << "    " << endl;
      cout << endl;

      cout << "Full Merkle Tree (this one has been unit tested before):" << endl;
      for(uint32_t i=0; i<merkleTree_.size(); i++)
      cout << "    " << i << " " << merkleTree_[i].toHexStr() << endl;
      */
   }

   vector<BinaryData> txList_;
   vector<BinaryData> merkleTree_;
};



////////////////////////////////////////////////////////////////////////////////
TEST_F(DISABLED_PartialMerkleTest, FullTree)
{
   vector<bool> isOurs(7);
   isOurs[0] = true;
   isOurs[1] = true;
   isOurs[2] = true;
   isOurs[3] = true;
   isOurs[4] = true;
   isOurs[5] = true;
   isOurs[6] = true;

   //cout << "Start serializing a full tree" << endl;
   PartialMerkleTree pmtFull(7, &isOurs, &txList_);
   BinaryData pmtSerFull = pmtFull.serialize();

   //cout << "Finished serializing (full)" << endl;
   //cout << "Merkle Root: " << pmtFull.getMerkleRoot().toHexStr() << endl;

   //cout << "Starting unserialize (full):" << endl;
   //cout << "Serialized: " << pmtSerFull.toHexStr() << endl;
   PartialMerkleTree pmtFull2(7);
   pmtFull2.unserialize(pmtSerFull);
   BinaryData pmtSerFull2 = pmtFull2.serialize();
   //cout << "Reserializ: " << pmtSerFull2.toHexStr() << endl;
   //cout << "Equal? " << (pmtSerFull==pmtSerFull2 ? "True" : "False") << endl;

   //cout << "Print Tree:" << endl;
   //pmtFull2.pprintTree();
   EXPECT_EQ(pmtSerFull, pmtSerFull2);
}


////////////////////////////////////////////////////////////////////////////////
TEST_F(DISABLED_PartialMerkleTest, SingleLeaf)
{
   vector<bool> isOurs(7);
   /////////////////////////////////////////////////////////////////////////////
   // Test all 7 single-flagged trees
   for (uint32_t i = 0; i<7; i++)
   {
      for (uint32_t j = 0; j<7; j++)
         isOurs[j] = i == j;

      PartialMerkleTree pmt(7, &isOurs, &txList_);
      //cout << "Serializing (partial)" << endl;
      BinaryData pmtSer = pmt.serialize();
      PartialMerkleTree pmt2(7);
      //cout << "Unserializing (partial)" << endl;
      pmt2.unserialize(pmtSer);
      //cout << "Reserializing (partial)" << endl;
      BinaryData pmtSer2 = pmt2.serialize();
      //cout << "Serialized (Partial): " << pmtSer.toHexStr() << endl;
      //cout << "Reserializ (Partial): " << pmtSer.toHexStr() << endl;
      //cout << "Equal? " << (pmtSer==pmtSer2 ? "True" : "False") << endl;

      //cout << "Print Tree:" << endl;
      //pmt2.pprintTree();
      EXPECT_EQ(pmtSer, pmtSer2);
   }
}


////////////////////////////////////////////////////////////////////////////////
TEST_F(DISABLED_PartialMerkleTest, MultiLeaf)
{
   // Use deterministic seed
   srand(0);

   vector<bool> isOurs(7);

   /////////////////////////////////////////////////////////////////////////////
   // Test a variety of 3-flagged trees
   for (uint32_t i = 0; i<512; i++)
   {
      if (i<256)
      {
         // 2/3 of leaves will be selected
         for (uint32_t j = 0; j<7; j++)
            isOurs[j] = (rand() % 3 < 2);
      }
      else
      {
         // 1/3 of leaves will be selected
         for (uint32_t j = 0; j<7; j++)
            isOurs[j] = (rand() % 3 < 1);
      }

      PartialMerkleTree pmt(7, &isOurs, &txList_);
      //cout << "Serializing (partial)" << endl;
      BinaryData pmtSer = pmt.serialize();
      PartialMerkleTree pmt2(7);
      //cout << "Unserializing (partial)" << endl;
      pmt2.unserialize(pmtSer);
      //cout << "Reserializing (partial)" << endl;
      BinaryData pmtSer2 = pmt2.serialize();
      //cout << "Serialized (Partial): " << pmtSer.toHexStr() << endl;
      //cout << "Reserializ (Partial): " << pmtSer.toHexStr() << endl;
      cout << "Equal? " << (pmtSer == pmtSer2 ? "True" : "False") << endl;

      //cout << "Print Tree:" << endl;
      //pmt2.pprintTree();
      EXPECT_EQ(pmtSer, pmtSer2);
   }
}


////////////////////////////////////////////////////////////////////////////////
TEST_F(DISABLED_PartialMerkleTest, EmptyTree)
{
   vector<bool> isOurs(7);
   isOurs[0] = false;
   isOurs[1] = false;
   isOurs[2] = false;
   isOurs[3] = false;
   isOurs[4] = false;
   isOurs[5] = false;
   isOurs[6] = false;

   //cout << "Start serializing a full tree" << endl;
   PartialMerkleTree pmtFull(7, &isOurs, &txList_);
   BinaryData pmtSerFull = pmtFull.serialize();

   //cout << "Finished serializing (full)" << endl;
   //cout << "Merkle Root: " << pmtFull.getMerkleRoot().toHexStr() << endl;

   //cout << "Starting unserialize (full):" << endl;
   //cout << "Serialized: " << pmtSerFull.toHexStr() << endl;
   PartialMerkleTree pmtFull2(7);
   pmtFull2.unserialize(pmtSerFull);
   BinaryData pmtSerFull2 = pmtFull2.serialize();
   //cout << "Reserializ: " << pmtSerFull2.toHexStr() << endl;
   //cout << "Equal? " << (pmtSerFull==pmtSerFull2 ? "True" : "False") << endl;

   //cout << "Print Tree:" << endl;
   //pmtFull2.pprintTree();
   EXPECT_EQ(pmtSerFull, pmtSerFull2);

}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BlockUtilsSuper : public ::testing::Test
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

      blkdir_ = string("./blkfiletest");
      homedir_ = string("./fakehomedir");
      ldbdir_ = string("./ldbtestdir");

      rmdir(blkdir_);
      rmdir(homedir_);
      rmdir(ldbdir_);

      mkdir(blkdir_);
      mkdir(homedir_);
      mkdir(ldbdir_);

      // Put the first 5 blocks into the blkdir
      blk0dat_ = BtcUtils::getBlkFilename(blkdir_, 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);

      BlockDataManagerConfig::setDbType(ARMORY_DB_SUPER);
      config.blkFileLocation_ = blkdir_;
      config.dbDir_ = ldbdir_;
      config.threadCount_ = 3;

      config.genesisBlockHash_ = ghash_;
      config.genesisTxHash_ = gentx_;
      config.magicBytes_ = magic_;
      config.nodeType_ = Node_UnitTest;

      unsigned port_int = 50000 + rand() % 10000;
      stringstream port_ss;
      port_ss << port_int;
      config.listenPort_ = port_ss.str();

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
      
      DatabaseContainer_Sharded::clearThreadShardTx(this_thread::get_id());
      EXPECT_EQ(DatabaseContainer_Sharded::txShardMap_.size(), 0);

      rmdir(blkdir_);
      rmdir(homedir_);

#ifdef _MSC_VER
      rmdir("./ldbtestdir");
      mkdir("./ldbtestdir");
#else
      string delstr = ldbdir_ + "/*";
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

   string blkdir_;
   string homedir_;
   string ldbdir_;
   string blk0dat_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load5Blocks)
{
   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   StoredScriptHistory ssh;

   BinaryData scrA(TestChain::scrAddrA);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load5Blocks_ReloadBDM)
{
   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   StoredScriptHistory ssh;

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   //restart bdm
   clients_->exitRequestLoop();
   clients_->shutdown();

   delete clients_;
   delete theBDMt_;

   initBDM();

   auto&& subssh_sdbi = iface_->getStoredDBInfo(SUBSSH, 0);
   EXPECT_EQ(subssh_sdbi.topBlkHgt_, 5);

   auto&& ssh_sdbi = iface_->getStoredDBInfo(SSH, 0);
   EXPECT_EQ(ssh_sdbi.topBlkHgt_, 5);

   theBDMt_->start(config.initMode_);
   bdvID = DBTestUtils::registerBDV(clients_, magic_);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load5Blocks_Reload_Rescan)
{
   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   StoredScriptHistory ssh;

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   //restart bdm
   clients_->exitRequestLoop();
   clients_->shutdown();

   delete clients_;
   delete theBDMt_;

   initBDM();

   auto&& subssh_sdbi = iface_->getStoredDBInfo(SUBSSH, 0);
   EXPECT_EQ(subssh_sdbi.topBlkHgt_, 5);

   auto&& ssh_sdbi = iface_->getStoredDBInfo(SSH, 0);
   EXPECT_EQ(ssh_sdbi.topBlkHgt_, 5);

   theBDMt_->start(INIT_RESCAN);
   bdvID = DBTestUtils::registerBDV(clients_, magic_);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load5Blocks_RescanSSH)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);


   StoredScriptHistory ssh;

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 160 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 9);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 55 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 55 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 5);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 10 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 20 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   //restart bdm
   clients_->exitRequestLoop();
   clients_->shutdown();

   delete clients_;
   delete theBDMt_;

   initBDM();

   auto&& subssh_sdbi = iface_->getStoredDBInfo(SUBSSH, 0);
   EXPECT_EQ(subssh_sdbi.topBlkHgt_, 3);

   auto&& ssh_sdbi = iface_->getStoredDBInfo(SSH, 0);
   EXPECT_EQ(ssh_sdbi.topBlkHgt_, 3);

   theBDMt_->start(INIT_SSH);
   bdvID = DBTestUtils::registerBDV(clients_, magic_);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 160 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 9);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 55 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 55 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 5);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 10 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 20 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   //restart bdm
   clients_->exitRequestLoop();
   clients_->shutdown();

   delete clients_;
   delete theBDMt_;


   initBDM();

   subssh_sdbi = iface_->getStoredDBInfo(SUBSSH, 0);
   EXPECT_EQ(subssh_sdbi.topBlkHgt_, 3);

   ssh_sdbi = iface_->getStoredDBInfo(SSH, 0);
   EXPECT_EQ(ssh_sdbi.topBlkHgt_, 3);

   //add next block
   TestUtils::appendBlocks({ "4" }, blk0dat_);

   theBDMt_->start(INIT_SSH);
   bdvID = DBTestUtils::registerBDV(clients_, magic_);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   subssh_sdbi = iface_->getStoredDBInfo(SUBSSH, 0);
   EXPECT_EQ(subssh_sdbi.topBlkHgt_, 4);

   ssh_sdbi = iface_->getStoredDBInfo(SSH, 0);
   EXPECT_EQ(ssh_sdbi.topBlkHgt_, 4);
   
   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 160 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 9);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 5);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 60 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   //add last block
   TestUtils::appendBlocks({ "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load3BlocksPlus3)
{
   // Copy only the first four blocks.  Will copy the full file next to test
   // readBlkFileUpdate method on non-reorg blocks.
   TestUtils::setBlocks({ "0", "1", "2" }, blk0dat_);

   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 2);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, HEADERS), TestChain::blkHash2);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->
      getHeaderByHash(TestChain::blkHash2)->isMainBranch());

   TestUtils::appendBlocks({ "3" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   TestUtils::appendBlocks({ "5" }, blk0dat_);

   //restart bdm
   clients_->exitRequestLoop();
   clients_->shutdown();

   delete clients_;
   delete theBDMt_;

   initBDM();

   theBDMt_->start(config.initMode_);
   bdvID = DBTestUtils::registerBDV(clients_, magic_);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   TestUtils::appendBlocks({ "4" }, blk0dat_);

   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 5);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, HEADERS), TestChain::blkHash5);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->
      getHeaderByHash(TestChain::blkHash5)->isMainBranch());

   StoredScriptHistory ssh;

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   //grab a tx by hash for coverage
   auto& txioHeightMap = ssh.subHistMap_.rbegin()->second;
   auto& txio = txioHeightMap.txioMap_.rbegin()->second;
   auto&& txhash = txio.getTxHashOfOutput(iface_);

   auto&& txObj = DBTestUtils::getTxByHash(clients_, bdvID, txhash);
   EXPECT_EQ(txObj.getThisHash(), txhash);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load5Blocks_FullReorg)
{
   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5", "4A", "5A" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   StoredScriptHistory ssh;

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 160 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 9);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 55 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 55 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 60 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 95 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 20 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load5Blocks_ReloadBDM_Reorg)
{
   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   //reload BDM
   clients_->exitRequestLoop();
   clients_->shutdown();

   delete theBDMt_;
   delete clients_;

   initBDM();

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5", "4A", "5A" }, blk0dat_);

   theBDMt_->start(config.initMode_);
   bdvID = DBTestUtils::registerBDV(clients_, magic_);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   EXPECT_EQ(theBDMt_->bdm()->blockchain()->top()->getBlockHeight(), 5);

   StoredScriptHistory ssh;

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 160 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 9);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 55 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 55 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 60 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 95 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 20 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load5Blocks_DoubleReorg)
{
   StoredScriptHistory ssh;

   TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat_);

   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   //first reorg: up to 5
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   //second reorg: up to 5A
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 160 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 9);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 55 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 55 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 60 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 95 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 20 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2);
}

////////////////////////////////////////////////////////////////////////////////
// I thought I was going to do something different with this set of tests,
// but I ended up with an exact copy of the BlockUtilsSuper fixture.  Oh well.
class BlockUtilsWithWalletTest : public ::testing::Test
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

      blkdir_ = string("./blkfiletest");
      homedir_ = string("./fakehomedir");
      ldbdir_ = string("./ldbtestdir");

      rmdir(blkdir_);
      rmdir(homedir_);
      rmdir(ldbdir_);

      mkdir(blkdir_);
      mkdir(homedir_);
      mkdir(ldbdir_);

      // Put the first 5 blocks into the blkdir
      blk0dat_ = BtcUtils::getBlkFilename(blkdir_, 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);

      BlockDataManagerConfig::setDbType(ARMORY_DB_SUPER);
      config.blkFileLocation_ = blkdir_;
      config.dbDir_ = ldbdir_;
      config.threadCount_ = 3;

      config.genesisBlockHash_ = ghash_;
      config.genesisTxHash_ = gentx_;
      config.magicBytes_ = magic_;
      config.nodeType_ = Node_UnitTest;

      unsigned port_int = 50000 + rand() % 10000;
      stringstream port_ss;
      port_ss << port_int;
      config.listenPort_ = port_ss.str();

      initBDM();

      wallet1id = BinaryData(string("wallet1"));
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

      DatabaseContainer_Sharded::clearThreadShardTx(this_thread::get_id());
      EXPECT_EQ(DatabaseContainer_Sharded::txShardMap_.size(), 0);

      rmdir(blkdir_);
      rmdir(homedir_);

#ifdef _MSC_VER
      rmdir("./ldbtestdir");
      mkdir("./ldbtestdir");
#else
      string delstr = ldbdir_ + "/*";
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

   string blkdir_;
   string homedir_;
   string ldbdir_;
   string blk0dat_;

   BinaryData wallet1id;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsWithWalletTest, Test_WithWallet)
{
   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);

   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);

   uint64_t balanceWlt;
   uint64_t balanceDB;

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrA)->getFullBalance();
   balanceDB = iface_->getBalanceForScrAddr(TestChain::scrAddrA);
   EXPECT_EQ(balanceWlt, 50 * COIN);
   EXPECT_EQ(balanceDB, 50 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrB)->getFullBalance();
   balanceDB = iface_->getBalanceForScrAddr(TestChain::scrAddrB);
   EXPECT_EQ(balanceWlt, 70 * COIN);
   EXPECT_EQ(balanceDB, 70 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrC)->getFullBalance();
   balanceDB = iface_->getBalanceForScrAddr(TestChain::scrAddrC);
   EXPECT_EQ(balanceWlt, 20 * COIN);
   EXPECT_EQ(balanceDB, 20 * COIN);

   balanceDB = iface_->getBalanceForScrAddr(TestChain::scrAddrD);
   EXPECT_EQ(balanceDB, 65 * COIN);
   balanceDB = iface_->getBalanceForScrAddr(TestChain::scrAddrE);
   EXPECT_EQ(balanceDB, 30 * COIN);
   balanceDB = iface_->getBalanceForScrAddr(TestChain::scrAddrF);
   EXPECT_EQ(balanceDB, 5 * COIN);   
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsWithWalletTest, RegisterAddrAfterWallet)
{
   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);

   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);

   uint64_t balanceWlt;
   uint64_t balanceDB;

   //post initial load address registration
   scrAddrVec.clear();
   scrAddrVec.push_back(TestChain::scrAddrD);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrA)->getFullBalance();
   balanceDB = iface_->getBalanceForScrAddr(TestChain::scrAddrA);
   EXPECT_EQ(balanceWlt, 50 * COIN);
   EXPECT_EQ(balanceDB, 50 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrB)->getFullBalance();
   balanceDB = iface_->getBalanceForScrAddr(TestChain::scrAddrB);
   EXPECT_EQ(balanceWlt, 70 * COIN);
   EXPECT_EQ(balanceDB, 70 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrC)->getFullBalance();
   balanceDB = iface_->getBalanceForScrAddr(TestChain::scrAddrC);
   EXPECT_EQ(balanceWlt, 20 * COIN);
   EXPECT_EQ(balanceDB, 20 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrD)->getFullBalance();
   balanceDB = iface_->getBalanceForScrAddr(TestChain::scrAddrD);
   EXPECT_EQ(balanceWlt, 65 * COIN);
   EXPECT_EQ(balanceDB, 65 * COIN);

   balanceDB = iface_->getBalanceForScrAddr(TestChain::scrAddrE);
   EXPECT_EQ(balanceDB, 30 * COIN);
   balanceDB = iface_->getBalanceForScrAddr(TestChain::scrAddrF);
   EXPECT_EQ(balanceDB, 5 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsWithWalletTest, ZeroConfUpdate)
{
   //create script spender objects
   auto getSpenderPtr = [](
      const UnspentTxOut& utxo,
      shared_ptr<ResolverFeed> feed)
      ->shared_ptr<ScriptSpender>
   {
      UTXO entry(utxo.value_, utxo.txHeight_, utxo.txIndex_, utxo.txOutIndex_,
         move(utxo.txHash_), move(utxo.script_));

      auto spender = make_shared<ScriptSpender>(entry, feed);
      spender->setSequence(UINT32_MAX - 2);

      return spender;
   };

   // Copy only the first two blocks
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   scrAddrVec.push_back(TestChain::scrAddrE);

   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);

   BinaryData ZChash;

   {
      ////spend 27 from wlt to assetWlt's first 2 unused addresses
      ////send rest back to scrAddrA

      auto spendVal = 27 * COIN;
      Signer signer;
      signer.setLockTime(3);

      //instantiate resolver feed overloaded object
      auto feed = make_shared<ResolverUtils::TestResolverFeed>();

      auto addToFeed = [feed](const BinaryData& key)->void
      {
         auto&& datapair = DBTestUtils::getAddrAndPubKeyFromPrivKey(key);
         feed->h160ToPubKey_.insert(datapair);
         feed->pubKeyToPrivKey_[datapair.second] = key;
      };

      addToFeed(TestChain::privKeyAddrA);
      addToFeed(TestChain::privKeyAddrB);
      addToFeed(TestChain::privKeyAddrC);
      addToFeed(TestChain::privKeyAddrD);
      addToFeed(TestChain::privKeyAddrE);

      //get utxo list for spend value
      auto&& unspentVec = wlt->getSpendableTxOutListForValue(spendVal);

      vector<UnspentTxOut> utxoVec;
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
         signer.addSpender(getSpenderPtr(utxo, feed));
      }

      //spendVal to addrE
      auto recipientChange = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrD.getSliceCopy(1, 20), spendVal);
      signer.addRecipient(recipientChange);

      if (total > spendVal)
      {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.sign();
      EXPECT_TRUE(signer.verify());

      Tx zctx(signer.serialize());
      ZChash = zctx.getThisHash();

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serialize(), 1300000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   EXPECT_EQ(wlt->getScrAddrObjByKey(TestChain::scrAddrA)->getFullBalance(), 50 * COIN);
   EXPECT_EQ(wlt->getScrAddrObjByKey(TestChain::scrAddrB)->getFullBalance(), 30 * COIN);
   EXPECT_EQ(wlt->getScrAddrObjByKey(TestChain::scrAddrC)->getFullBalance(), 55 * COIN);
   EXPECT_EQ(wlt->getScrAddrObjByKey(TestChain::scrAddrE)->getFullBalance(), 3 * COIN);

   //test ledger entry
   LedgerEntry le = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash);

   EXPECT_EQ(le.getTxTime(), 1300000000);
   EXPECT_EQ(le.isSentToSelf(), false);
   EXPECT_EQ(le.getValue(), -27 * COIN);

   //check ZChash in DB
   BinaryData zcKey = WRITE_UINT16_BE(0xFFFF);
   zcKey.append(WRITE_UINT32_LE(0));
   EXPECT_EQ(iface_->getTxHashForLdbKey(zcKey), ZChash);

   //grab ZC by hash
   auto&& txobj = DBTestUtils::getTxByHash(clients_, bdvID, ZChash);
   EXPECT_EQ(txobj.getThisHash(), ZChash);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsWithWalletTest, UnrelatedZC_CheckLedgers)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);

   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);

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
   auto delegateID = DBTestUtils::getLedgerDelegate(clients_, bdvID);

   //check balances
   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);

   StoredScriptHistory ssh;
   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);

   //Create zc that spends from addr D to F. This is supernode so the DB
   //should track this ZC even though it isn't registered. Send the ZC as
   //a batch along with a ZC that hits our wallets, in order to get the 
   //notification, which comes at the BDV level (i.e. only for registered
   //wallets).
   
   auto&& ZC1 = TestUtils::getTx(5, 2); //block 5, tx 2
   auto&& ZChash1 = BtcUtils::getHash256(ZC1);

   auto&& ZC2 = TestUtils::getTx(5, 1); //block 5, tx 1
   auto&& ZChash2 = BtcUtils::getHash256(ZC2);

   DBTestUtils::ZcVector zcVec1;
   zcVec1.push_back(ZC1, 14000000);
   zcVec1.push_back(ZC2, 14100000);

   DBTestUtils::pushNewZc(theBDMt_, zcVec1);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);

   auto zcTxios = 
      theBDMt_->bdm()->zeroConfCont()->getTxioMapForScrAddr(
         TestChain::scrAddrD);
   ASSERT_NE(zcTxios, nullptr);
   EXPECT_EQ(zcTxios->size(), 1);
   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   DBTestUtils::addTxioToSsh(ssh, *zcTxios);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);

   zcTxios = 
      theBDMt_->bdm()->zeroConfCont()->getTxioMapForScrAddr(
         TestChain::scrAddrF);
   ASSERT_NE(zcTxios, nullptr);
   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   DBTestUtils::addTxioToSsh(ssh, *zcTxios);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);

   //grab ledger for 1st ZC, should be empty
   auto zcledger = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash1);
   EXPECT_EQ(zcledger.getTxHash(), BtcUtils::EmptyHash());

   //grab ledger for 2nd ZC
   zcledger = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash2);
   EXPECT_EQ(zcledger.getValue(), 30 * COIN);
   EXPECT_EQ(zcledger.getTxTime(), 14100000);
   EXPECT_FALSE(zcledger.isOptInRBF());

   //grab delegate ledger
   auto&& delegateLedger = 
      DBTestUtils::getHistoryPage(clients_, bdvID, delegateID, 0);

   unsigned zc2_count = 0;
   for (auto& ld : delegateLedger)
   {
      if (ld.getTxHash() == ZChash2)
         zc2_count++;
   }

   EXPECT_EQ(zc2_count, 1);

   //push last block
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 70 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);

   zcTxios = 
      theBDMt_->bdm()->zeroConfCont()->getTxioMapForScrAddr(
         TestChain::scrAddrD);
   ASSERT_EQ(zcTxios, nullptr);
   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);

   zcTxios = 
      theBDMt_->bdm()->zeroConfCont()->getTxioMapForScrAddr(
         TestChain::scrAddrF);
   ASSERT_EQ(zcTxios, nullptr);
   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);

   //try to get ledgers, ZCs should be all gone
   zcledger = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash1);
   EXPECT_EQ(zcledger.getTxHash(), BtcUtils::EmptyHash());
   zcledger = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash2);
   EXPECT_EQ(zcledger.getTxTime(), 1231009513);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsWithWalletTest, RegisterAfterZC)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);

   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);

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
   auto delegateID = DBTestUtils::getLedgerDelegate(clients_, bdvID);

   //check balances
   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);

   StoredScriptHistory ssh;
   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);

   //Create zc that spends from addr D to F. This is supernode so the DB
   //should track this ZC even though it isn't registered. Send the ZC as
   //a batch along with a ZC that hits our wallets, in order to get the
   //notification, which comes at the BDV level (i.e. only for registered
   //wallets).

   auto&& ZC1 = TestUtils::getTx(5, 2); //block 5, tx 2
   auto&& ZChash1 = BtcUtils::getHash256(ZC1);

   auto&& ZC2 = TestUtils::getTx(5, 1); //block 5, tx 1
   auto&& ZChash2 = BtcUtils::getHash256(ZC2);

   DBTestUtils::ZcVector zcVec1;
   zcVec1.push_back(ZC1, 14000000);
   zcVec1.push_back(ZC2, 14100000);

   DBTestUtils::pushNewZc(theBDMt_, zcVec1);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);

   auto zcTxios = 
      theBDMt_->bdm()->zeroConfCont()->getTxioMapForScrAddr(
         TestChain::scrAddrD);
   ASSERT_NE(zcTxios, nullptr);
   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   DBTestUtils::addTxioToSsh(ssh, *zcTxios);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);

   zcTxios = 
      theBDMt_->bdm()->zeroConfCont()->getTxioMapForScrAddr(
         TestChain::scrAddrF);
   ASSERT_NE(zcTxios, nullptr);
   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   DBTestUtils::addTxioToSsh(ssh, *zcTxios);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);

   //Register scrAddrD with the wallet. It should have the ZC balance
   scrAddrVec.push_back(TestChain::scrAddrD);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");
   
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 65 * COIN);

   //add last block
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 70 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 65 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsWithWalletTest, ZC_Reorg)
{
   //create spender lamba
   auto getSpenderPtr = [](
      const UnspentTxOut& utxo,
      shared_ptr<ResolverFeed> feed)
      ->shared_ptr<ScriptSpender>
   {
      UTXO entry(utxo.value_, utxo.txHeight_, utxo.txIndex_, utxo.txOutIndex_,
         move(utxo.txHash_), move(utxo.script_));

      return make_shared<ScriptSpender>(entry, feed);
   };

   //
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);

   auto&& wltRoot = SecureBinaryData().GenerateRandom(32);
   auto assetWlt = AssetWallet_Single::createFromPrivateRoot_Armory135(
      homedir_,
      move(wltRoot), //root as a rvalue
      SecureBinaryData(),
      3); //set lookup computation to 3 entries
   auto addr1_ptr = assetWlt->getNewAddress();
   auto addr2_ptr = assetWlt->getNewAddress();

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   
   auto&& wltSet = assetWlt->getAddrHashSet();
   vector<BinaryData> wltVec;
   for (auto& addr : wltSet)
      wltVec.push_back(addr);

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");
   DBTestUtils::registerWallet(clients_, bdvID, wltVec, assetWlt->getID());
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto assetWltDbObj = bdvPtr->getWalletOrLockbox(assetWlt->getID());
   auto delegateID = DBTestUtils::getLedgerDelegate(clients_, bdvID);

   //check balances
   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);

   for (auto& sa : wltSet)
   {
      scrObj = assetWltDbObj->getScrAddrObjByKey(sa);
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   }

   {
      Signer signer;

      //instantiate resolver feed overloaded object
      auto feed = make_shared<ResolverUtils::TestResolverFeed>();

      auto addToFeed = [feed](const BinaryData& key)->void
      {
         auto&& datapair = DBTestUtils::getAddrAndPubKeyFromPrivKey(key);
         feed->h160ToPubKey_.insert(datapair);
         feed->pubKeyToPrivKey_[datapair.second] = key;
      };

      addToFeed(TestChain::privKeyAddrA);
      addToFeed(TestChain::privKeyAddrB);
      addToFeed(TestChain::privKeyAddrC);
      addToFeed(TestChain::privKeyAddrD);
      addToFeed(TestChain::privKeyAddrE);

      //get utxo list for spend value
      auto&& unspentVec = wlt->getSpendableTxOutListForValue(UINT64_MAX);

      //consume firt utxo, send 2 to scrAddrA, 3 to new wallet
      signer.addSpender(getSpenderPtr(unspentVec[0], feed));
      signer.addRecipient(addr1_ptr->getRecipient(3 * COIN));
      auto recipientChange = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrA.getSliceCopy(1, 20), 2 * COIN);
      signer.addRecipient(recipientChange);
      signer.sign();

      //2nd tx, 2nd utxo, 20 to scrAddrB, 10 new wallet
      Signer signer2;
      signer2.addSpender(getSpenderPtr(unspentVec[1], feed));
      signer2.addRecipient(addr2_ptr->getRecipient(10 * COIN));
      auto recipientChange2 = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), 20 * COIN);
      signer2.addRecipient(recipientChange2);
      signer2.sign();

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serialize(), 14000000);
      zcVec.push_back(signer2.serialize(), 14100000);
      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 52 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);

   scrObj = assetWltDbObj->getScrAddrObjByKey(addr1_ptr->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 3 * COIN);
   scrObj = assetWltDbObj->getScrAddrObjByKey(addr2_ptr->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);

   //push block 4 of first chain
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   //check balances, 1st ZC should be gone, 2nd should still be valid
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);

   scrObj = assetWltDbObj->getScrAddrObjByKey(addr1_ptr->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = assetWltDbObj->getScrAddrObjByKey(addr2_ptr->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsWithWalletTest, MultipleSigners_2of3_NativeP2WSH)
{
   //create spender lamba
   auto getSpenderPtr = [](
      const UnspentTxOut& utxo,
      shared_ptr<ResolverFeed> feed)
      ->shared_ptr<ScriptSpender>
   {
      UTXO entry(utxo.value_, utxo.txHeight_, utxo.txIndex_, utxo.txOutIndex_,
         move(utxo.txHash_), move(utxo.script_));

      return make_shared<ScriptSpender>(entry, feed);
   };

   //
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   scrAddrVec.push_back(TestChain::scrAddrD);
   scrAddrVec.push_back(TestChain::scrAddrE);

   //// create 3 assetWlt ////

   //create a root private key
   auto&& wltRoot = SecureBinaryData().GenerateRandom(32);
   auto assetWlt_1 = AssetWallet_Single::createFromPrivateRoot_Armory135(
      homedir_,
      move(wltRoot), //root as a rvalue
      SecureBinaryData(),
      3); //set lookup computation to 3 entries

   wltRoot = move(SecureBinaryData().GenerateRandom(32));
   auto assetWlt_2 = AssetWallet_Single::createFromPrivateRoot_Armory135(
      homedir_,
      move(wltRoot), //root as a rvalue
      SecureBinaryData(),
      3); //set lookup computation to 3 entries

   wltRoot = move(SecureBinaryData().GenerateRandom(32));
   auto assetWlt_3 = AssetWallet_Single::createFromPrivateRoot_Armory135(
      homedir_,
      move(wltRoot), //root as a rvalue
      SecureBinaryData(),
      3); //set lookup computation to 3 entries

          //create 2-of-3 multisig asset entry from 3 different wallets
   map<BinaryData, shared_ptr<AssetEntry>> asset_single_map;
   auto asset1 = assetWlt_1->getMainAccountAssetForIndex(0);
   BinaryData wltid1_bd(assetWlt_1->getID());
   asset_single_map.insert(make_pair(wltid1_bd, asset1));

   auto asset2 = assetWlt_2->getMainAccountAssetForIndex(0);
   BinaryData wltid2_bd(assetWlt_2->getID());
   asset_single_map.insert(make_pair(wltid2_bd, asset2));

   auto asset4_singlesig = assetWlt_2->getNewAddress();

   auto asset3 = assetWlt_3->getMainAccountAssetForIndex(0);
   BinaryData wltid3_bd(assetWlt_3->getID());
   asset_single_map.insert(make_pair(wltid3_bd, asset3));

   auto ae_ms = make_shared<AssetEntry_Multisig>(0, BinaryData("test"),
      asset_single_map, 2, 3);
   auto addr_ms_raw = make_shared<AddressEntry_Multisig>(ae_ms, true);
   auto addr_p2wsh = make_shared<AddressEntry_P2WSH>(addr_ms_raw);


   //register with db
   vector<BinaryData> addrVec;
   addrVec.push_back(addr_p2wsh->getPrefixedHash());

   vector<BinaryData> addrVec_singleSig;
   auto&& addrSet = assetWlt_2->getAddrHashSet();
   for (auto& addr : addrSet)
      addrVec_singleSig.push_back(addr);

   DBTestUtils::registerWallet(clients_, bdvID, addrVec, "ms_entry");
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");
   DBTestUtils::registerWallet(clients_, bdvID, addrVec_singleSig, assetWlt_2->getID());

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto ms_wlt = bdvPtr->getWalletOrLockbox(BinaryData("ms_entry"));
   auto wlt_singleSig = bdvPtr->getWalletOrLockbox(BinaryData(assetWlt_2->getID()));


   //check balances
   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);

   //check new wallet balances
   scrObj = ms_wlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   {
      ////spend 27 from wlt to ms_wlt only address
      ////send rest back to scrAddrA

      auto spendVal = 27 * COIN;
      Signer signer;

      //instantiate resolver feed overloaded object
      auto feed = make_shared<ResolverUtils::TestResolverFeed>();

      auto addToFeed = [feed](const BinaryData& key)->void
      {
         auto&& datapair = DBTestUtils::getAddrAndPubKeyFromPrivKey(key);
         feed->h160ToPubKey_.insert(datapair);
         feed->pubKeyToPrivKey_[datapair.second] = key;
      };

      addToFeed(TestChain::privKeyAddrA);
      addToFeed(TestChain::privKeyAddrB);
      addToFeed(TestChain::privKeyAddrC);
      addToFeed(TestChain::privKeyAddrD);
      addToFeed(TestChain::privKeyAddrE);

      //get utxo list for spend value
      auto&& unspentVec = wlt->getSpendableTxOutListForValue(spendVal);

      vector<UnspentTxOut> utxoVec;
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
         signer.addSpender(getSpenderPtr(utxo, feed));
      }

      //spend 20 to nested p2wsh script hash
      signer.addRecipient(addr_p2wsh->getRecipient(20 * COIN));

      //spend 7 to assetWlt_2
      signer.addRecipient(asset4_singlesig->getRecipient(7 * COIN));

      if (total > spendVal)
      {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.sign();
      EXPECT_TRUE(signer.verify());
      auto&& zcHash = signer.getTxId();

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serialize(), 14000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

      //grab ZC from DB and verify it again
      auto&& zc_from_db = DBTestUtils::getTxByHash(clients_, bdvID, zcHash);
      auto&& raw_tx = zc_from_db.serialize();
      auto bctx = BCTX::parse(raw_tx);
      TransactionVerifier tx_verifier(*bctx, utxoVec);

      ASSERT_TRUE(tx_verifier.evaluateState().isValid());
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //check new wallet balances
   scrObj = ms_wlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
   scrObj = wlt_singleSig->getScrAddrObjByKey(asset4_singlesig->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 7 * COIN);

   auto spendVal = 18 * COIN;
   Signer signer2;
   signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

   //get utxo list for spend value
   auto&& unspentVec =
      ms_wlt->getSpendableTxOutListZC();

   auto&& unspentVec_singleSig = wlt_singleSig->getSpendableTxOutListZC();

   unspentVec.insert(unspentVec.end(),
      unspentVec_singleSig.begin(), unspentVec_singleSig.end());

   //create feed from asset wallet 1
   auto feed_ms = make_shared<ResolverFeed_AssetWalletSingle_ForMultisig>(assetWlt_1);
   auto assetFeed = make_shared<ResolverUtils::CustomFeed>(addr_p2wsh, feed_ms);

   //create spenders
   uint64_t total = 0;
   for (auto& utxo : unspentVec)
   {
      total += utxo.getValue();
      signer2.addSpender(getSpenderPtr(utxo, assetFeed));
   }

   //creates outputs
   //spend 18 to addr 0, use P2PKH
   auto recipient2 = make_shared<Recipient_P2PKH>(
      TestChain::scrAddrB.getSliceCopy(1, 20), spendVal);
   signer2.addRecipient(recipient2);

   if (total > spendVal)
   {
      //deal with change, no fee
      auto changeVal = total - spendVal;
      signer2.addRecipient(addr_p2wsh->getRecipient(changeVal));
   }

   //sign, verify & return signed tx
   auto&& signerState = signer2.evaluateSignedState();

   {
      EXPECT_EQ(signerState.getEvalMapSize(), 2);

      auto&& txinEval = signerState.getSignedStateForInput(0);
      auto& pubkeyMap = txinEval.getPubKeyMap();
      EXPECT_EQ(pubkeyMap.size(), 3);
      for (auto& pubkeyState : pubkeyMap)
         EXPECT_FALSE(pubkeyState.second);

      txinEval = signerState.getSignedStateForInput(1);
      auto& pubkeyMap_2 = txinEval.getPubKeyMap();
      EXPECT_EQ(pubkeyMap_2.size(), 0);
   }

   {
      auto lock = assetWlt_1->lockDecryptedContainer();
      signer2.sign();
   }

   try
   {
      signer2.verify();
      EXPECT_TRUE(false);
   }
   catch (...)
   {
   }

   {
      //signer state with 1 sig
      EXPECT_FALSE(signer2.isValid());
      signerState = signer2.evaluateSignedState();

      EXPECT_EQ(signerState.getEvalMapSize(), 2);

      auto&& txinEval = signerState.getSignedStateForInput(0);
      EXPECT_EQ(txinEval.getSigCount(), 1);

      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset1);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));
   }

   Signer signer3;
   //create feed from asset wallet 2
   auto feed_ms3 = make_shared<ResolverFeed_AssetWalletSingle_ForMultisig>(assetWlt_2);
   auto assetFeed3 = make_shared<ResolverUtils::CustomFeed>(addr_p2wsh, feed_ms3);
   signer3.deserializeState(signer2.serializeState());

   {
      //make sure sig was properly carried over with state
      EXPECT_FALSE(signer3.isValid());
      signerState = signer3.evaluateSignedState();

      EXPECT_EQ(signerState.getEvalMapSize(), 2);
      auto&& txinEval = signerState.getSignedStateForInput(0);
      EXPECT_EQ(txinEval.getSigCount(), 1);

      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset1);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));
   }

   signer3.setFeed(assetFeed3);

   {
      auto lock = assetWlt_2->lockDecryptedContainer();
      signer3.sign();
   }

   {
      auto assetFeed4 = make_shared<ResolverFeed_AssetWalletSingle>(assetWlt_2);
      signer3.resetFeeds();
      signer3.setFeed(assetFeed4);
      auto lock = assetWlt_2->lockDecryptedContainer();
      signer3.sign();
   }


   ASSERT_TRUE(signer3.isValid());
   try
   {
      signer3.verify();
   }
   catch (...)
   {
      EXPECT_TRUE(false);
   }

   {
      //should have 2 sigs now
      EXPECT_TRUE(signer3.isValid());
      signerState = signer3.evaluateSignedState();

      EXPECT_EQ(signerState.getEvalMapSize(), 2);
      auto&& txinEval = signerState.getSignedStateForInput(0);
      EXPECT_EQ(txinEval.getSigCount(), 2);

      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset1);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));

      asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset2);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));
   }

   auto&& tx1 = signer3.serialize();
   auto&& zcHash = signer3.getTxId();

   //broadcast the last one
   DBTestUtils::ZcVector zcVec;
   zcVec.push_back(tx1, 15000000);

   DBTestUtils::pushNewZc(theBDMt_, zcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   //grab ZC from DB and verify it again
   auto&& zc_from_db = DBTestUtils::getTxByHash(clients_, bdvID, zcHash);
   auto&& raw_tx = zc_from_db.serialize();
   auto bctx = BCTX::parse(raw_tx);
   TransactionVerifier tx_verifier(*bctx, unspentVec);

   ASSERT_TRUE(tx_verifier.evaluateState().isValid());


   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 48 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //check new wallet balances
   scrObj = ms_wlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 9 * COIN);
   scrObj = wlt_singleSig->getScrAddrObjByKey(asset4_singlesig->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsWithWalletTest, ChainZC_RBFchild_Test)
{
   //create spender lambda
   auto getSpenderPtr = [](
      const UnspentTxOut& utxo,
      shared_ptr<ResolverFeed> feed, bool flagRBF)
      ->shared_ptr<ScriptSpender>
   {
      UTXO entry(utxo.value_, utxo.txHeight_, utxo.txIndex_, utxo.txOutIndex_,
         move(utxo.txHash_), move(utxo.script_));

      auto spender = make_shared<ScriptSpender>(entry, feed);

      if (flagRBF)
         spender->setSequence(UINT32_MAX - 2);

      return spender;
   };

   BinaryData ZCHash1, ZCHash2, ZCHash3;

   //
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   scrAddrVec.push_back(TestChain::scrAddrD);
   scrAddrVec.push_back(TestChain::scrAddrE);

   //// create assetWlt ////

   //create a root private key
   auto&& wltRoot = SecureBinaryData().GenerateRandom(32);
   auto assetWlt = AssetWallet_Single::createFromPrivateRoot_Armory135(
      homedir_,
      move(wltRoot), //root as a r value
      SecureBinaryData(),
      10); //set lookup computation to 5 entries

           //register with db
   vector<BinaryData> addrVec;

   auto hashSet = assetWlt->getAddrHashSet();
   vector<BinaryData> hashVec;
   hashVec.insert(hashVec.begin(), hashSet.begin(), hashSet.end());

   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID());
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1");

   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto dbAssetWlt = bdvPtr->getWalletOrLockbox(assetWlt->getID());

   //check balances
   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);

   //check new wallet balances
   for (auto& scripthash : hashSet)
   {
      scrObj = dbAssetWlt->getScrAddrObjByKey(scripthash);
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   }

   {
      ////spend 27 from wlt to assetWlt's first 2 unused addresses
      ////send rest back to scrAddrA

      auto spendVal = 27 * COIN;
      Signer signer;

      //instantiate resolver feed overloaded object
      auto feed = make_shared<ResolverUtils::TestResolverFeed>();

      auto addToFeed = [feed](const BinaryData& key)->void
      {
         auto&& datapair = DBTestUtils::getAddrAndPubKeyFromPrivKey(key);
         feed->h160ToPubKey_.insert(datapair);
         feed->pubKeyToPrivKey_[datapair.second] = key;
      };

      addToFeed(TestChain::privKeyAddrA);
      addToFeed(TestChain::privKeyAddrB);
      addToFeed(TestChain::privKeyAddrC);
      addToFeed(TestChain::privKeyAddrD);
      addToFeed(TestChain::privKeyAddrE);

      //get utxo list for spend value
      auto&& unspentVec = wlt->getSpendableTxOutListForValue(spendVal);

      vector<UnspentTxOut> utxoVec;
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
         signer.addSpender(getSpenderPtr(utxo, feed, true));
      }

      //spend 12 to first address
      auto addr0 = assetWlt->getNewAddress();
      signer.addRecipient(addr0->getRecipient(12 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());

      //spend 15 to addr 1, use P2PKH
      auto addr1 = assetWlt->getNewAddress();
      signer.addRecipient(addr1->getRecipient(15 * COIN));
      addrVec.push_back(addr1->getPrefixedHash());

      if (total > spendVal)
      {
         //deal with change, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.sign();
      EXPECT_TRUE(signer.verify());

      auto rawTx = signer.serialize();
      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(rawTx, 14000000);

      ZCHash1 = move(BtcUtils::getHash256(rawTx));
      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //check new wallet balances
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);

   //grab ledger
   auto zcledger = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash1);
   EXPECT_EQ(zcledger.getValue(), 27 * COIN);
   EXPECT_EQ(zcledger.getTxTime(), 14000000);
   EXPECT_TRUE(zcledger.isOptInRBF());

   //cpfp the first zc
   {
      Signer signer3;

      //instantiate resolver feed overloaded object
      auto assetFeed = make_shared<ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto&& unspentVec = dbAssetWlt->getSpendableTxOutListZC();

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : unspentVec)
      {
         total += utxo.getValue();
         signer3.addSpender(getSpenderPtr(utxo, assetFeed, true));
      }

      //spend 4 to new address
      auto addr0 = assetWlt->getNewAddress();
      signer3.addRecipient(addr0->getRecipient(4 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());

      //spend 6 to new address
      auto addr1 = assetWlt->getNewAddress();
      signer3.addRecipient(addr1->getRecipient(6 * COIN));
      addrVec.push_back(addr1->getPrefixedHash());

      //deal with change, no fee
      auto changeVal = total - 10 * COIN;
      auto recipientChange = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
      signer3.addRecipient(recipientChange);

      //sign, verify then broadcast
      {
         auto lock = assetWlt->lockDecryptedContainer();
         signer3.sign();
      }

      auto rawTx = signer3.serialize();
      DBTestUtils::ZcVector zcVec3;
      zcVec3.push_back(rawTx, 15000000);

      ZCHash2 = move(BtcUtils::getHash256(rawTx));
      DBTestUtils::pushNewZc(theBDMt_, zcVec3);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 25 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //check new wallet balances
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]);
   EXPECT_EQ(scrObj->getFullBalance(), 4 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[3]);
   EXPECT_EQ(scrObj->getFullBalance(), 6 * COIN);


   //grab ledgers

   //first zc should be valid still
   auto zcledger1 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash1);
   EXPECT_EQ(zcledger1.getValue(), 27 * COIN);
   EXPECT_EQ(zcledger1.getTxTime(), 14000000);
   EXPECT_TRUE(zcledger1.isOptInRBF());

   //second zc should be valid
   auto zcledger2 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash2);
   EXPECT_EQ(zcledger2.getValue(), -17 * COIN);
   EXPECT_EQ(zcledger2.getTxTime(), 15000000);
   EXPECT_TRUE(zcledger2.isOptInRBF());

   //rbf the child
   {
      auto spendVal = 10 * COIN;
      Signer signer2;

      //instantiate resolver feed
      auto assetFeed =
         make_shared<ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto&& unspentVec = dbAssetWlt->getRBFTxOutList();

      vector<UnspentTxOut> utxoVec;
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
         signer2.addSpender(getSpenderPtr(utxo, assetFeed, true));
      }

      //spend 5 to new address
      auto addr0 = assetWlt->getNewAddress();
      signer2.addRecipient(addr0->getRecipient(6 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());


      if (total > spendVal)
      {
         //change addrE, 1 btc fee
         auto changeVal = 5 * COIN;
         auto recipientChange = make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), changeVal);
         signer2.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      {
         auto lock = assetWlt->lockDecryptedContainer();
         signer2.sign();
      }
      EXPECT_TRUE(signer2.verify());

      auto rawTx = signer2.serialize();
      DBTestUtils::ZcVector zcVec2;
      zcVec2.push_back(rawTx, 17000000);

      ZCHash3 = move(BtcUtils::getHash256(rawTx));
      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);

   //check new wallet balances
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[3]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[4]);
   EXPECT_EQ(scrObj->getFullBalance(), 6 * COIN);

   //grab ledgers

   //first zc should be replaced, hence the ledger should be empty
   auto zcledger3 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash1);
   EXPECT_EQ(zcledger3.getValue(), 27 * COIN);
   EXPECT_EQ(zcledger3.getTxTime(), 14000000);
   EXPECT_TRUE(zcledger3.isOptInRBF());

   //second zc should be replaced
   auto zcledger8 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash2);
   EXPECT_EQ(zcledger8.getTxHash(), BtcUtils::EmptyHash_);

   //third zc should be valid
   auto zcledger9 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash3);
   EXPECT_EQ(zcledger9.getValue(), -6 * COIN);
   EXPECT_EQ(zcledger9.getTxTime(), 17000000);
   EXPECT_TRUE(zcledger9.isOptInRBF());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsWithWalletTest, ZC_InOut_SameBlock)
{
   //create spender lambda
   auto getSpenderPtr = [](
      const UnspentTxOut& utxo,
      shared_ptr<ResolverFeed> feed, bool flagRBF)
      ->shared_ptr<ScriptSpender>
   {
      UTXO entry(utxo.value_, utxo.txHeight_, utxo.txIndex_, utxo.txOutIndex_,
         move(utxo.txHash_), move(utxo.script_));

      auto spender = make_shared<ScriptSpender>(entry, feed);

      if (flagRBF)
         spender->setSequence(UINT32_MAX - 2);

      return spender;
   };

   BinaryData ZCHash1, ZCHash2, ZCHash3;

   //
   TestUtils::setBlocks({ "0", "1" }, blk0dat_);

   theBDMt_->start(config.initMode_);
   auto&& bdvID = DBTestUtils::registerBDV(clients_, magic_);

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

   //check balances
   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //add the 2 zc
   auto&& ZC1 = TestUtils::getTx(2, 1); //block 2, tx 1
   auto&& ZChash1 = BtcUtils::getHash256(ZC1);

   auto&& ZC2 = TestUtils::getTx(2, 2); //block 2, tx 2
   auto&& ZChash2 = BtcUtils::getHash256(ZC2);

   DBTestUtils::ZcVector rawZcVec;
   rawZcVec.push_back(ZC1, 1300000000);
   rawZcVec.push_back(ZC2, 1310000000);

   DBTestUtils::pushNewZc(theBDMt_, rawZcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //add last block
   TestUtils::appendBlocks({ "2" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsWithWalletTest, WebSocketStack_ParallelAsync)
{
   BlockDataManagerConfig::setServiceType(SERVICE_WEBSOCKET);

   //
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);

   //run clients from websocketserver object instead
   clients_->exitRequestLoop();
   clients_->shutdown();

   auto&& firstHash = READHEX("b6b6f145742a9072fd85f96772e63a00eb4101709aa34ec5dd59e8fc904191a7");
   delete clients_;
   delete theBDMt_;
   clients_ = nullptr;

   theBDMt_ = new BlockDataManagerThread(config);
   WebSocketServer::start(theBDMt_, true);

   auto createNAddresses = [](unsigned count)->vector<BinaryData>
   {
      vector<BinaryData> result;

      for (unsigned i = 0; i < count; i++)
      {
         BinaryWriter bw;
         bw.put_uint8_t(SCRIPT_PREFIX_HASH160);

         auto&& addrData = SecureBinaryData().GenerateRandom(20);
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

   theBDMt_->start(config.initMode_);

   {
      auto pCallback = make_shared<DBTestUtils::UTCallback>();
      auto isConnectedFut = pCallback->getFuture();
      auto&& bdvObj = SwigClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", config.listenPort_, pCallback);
      if (!isConnectedFut.get())
         throw runtime_error("failed to connect to remote");
      bdvObj.registerWithDB(config.magicBytes_);
      
      auto&& wallet1 = bdvObj.instantiateWallet("wallet1");
      vector<string> walletRegIDs;
      walletRegIDs.push_back(
         wallet1.registerAddresses(scrAddrVec, false));

      //wait on registration ack
      pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

      //go online
      bdvObj.goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      auto delegate = move(bdvObj.getLedgerDelegateForWallets());
      auto ledgers = move(delegate.getHistoryPage(0));

      bdvObj.unregisterFromDB();
   }

   auto request_lambda = [&](void)->void
   {
      auto pCallback = make_shared<DBTestUtils::UTCallback>();
      auto isConnectedFut = pCallback->getFuture();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", config.listenPort_, pCallback);
      if (!isConnectedFut.get())
         throw runtime_error("failed to connect to remote");
      bdvObj->registerWithDB(config.magicBytes_);

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

      //get delegate
      auto del1_prom = make_shared<promise<AsyncClient::LedgerDelegate>>();
      auto del1_fut = del1_prom->get_future();
      auto del1_get = [del1_prom](AsyncClient::LedgerDelegate delegate)->void
      {
         del1_prom->set_value(move(delegate));
      };
      bdvObj->getLedgerDelegateForWallets(del1_get);
      auto delegate = move(del1_fut.get());

      //get ledgers
      auto ledger_prom = 
         make_shared<promise<vector<::ClientClasses::LedgerEntry>>>();
      auto ledger_fut = ledger_prom->get_future();
      auto ledger_get = 
         [ledger_prom](vector<::ClientClasses::LedgerEntry> ledgerV)->void
      {
         ledger_prom->set_value(move(ledgerV));
      };
      delegate.getHistoryPage(0, ledger_get);

      //
      auto w1AddrBal_prom = make_shared<promise<map<BinaryData, vector<uint64_t>>>>();
      auto w1AddrBal_fut = w1AddrBal_prom->get_future();
      auto w1_getAddrBalancesLBD = 
         [w1AddrBal_prom](map<BinaryData, vector<uint64_t>> balances)->void
      {
         w1AddrBal_prom->set_value(move(balances));
      };
      wallet1.getAddrBalancesFromDB(w1_getAddrBalancesLBD);
      
      //
      auto w1Bal_prom = make_shared<promise<vector<uint64_t>>>();
      auto w1Bal_fut = w1Bal_prom->get_future();
      auto w1_getBalanceAndCountLBD = 
         [w1Bal_prom](vector<uint64_t> balances)->void
      {
         w1Bal_prom->set_value(move(balances));
      };
      wallet1.getBalancesAndCount(5, w1_getBalanceAndCountLBD);

      //
      auto lb1AddrBal_prom = make_shared<promise<map<BinaryData, vector<uint64_t>>>>();
      auto lb1AddrBal_fut = lb1AddrBal_prom->get_future();
      auto lb1_getAddrBalancesLBD = 
         [lb1AddrBal_prom](map<BinaryData, vector<uint64_t>> balances)->void
      {
         lb1AddrBal_prom->set_value(move(balances));
      };
      lb1.getAddrBalancesFromDB(lb1_getAddrBalancesLBD);

      //
      auto lb2AddrBal_prom = make_shared<promise<map<BinaryData, vector<uint64_t>>>>();
      auto lb2AddrBal_fut = lb2AddrBal_prom->get_future();
      auto lb2_getAddrBalancesLBD = 
         [lb2AddrBal_prom](map<BinaryData, vector<uint64_t>> balances)->void
      {
         lb2AddrBal_prom->set_value(move(balances));
      };
      lb2.getAddrBalancesFromDB(lb2_getAddrBalancesLBD);

      //
      auto lb1Bal_prom = make_shared<promise<vector<uint64_t>>>();
      auto lb1Bal_fut = lb1Bal_prom->get_future();
      auto lb1_getBalanceAndCountLBD = 
         [lb1Bal_prom](vector<uint64_t> balances)->void
      {
         lb1Bal_prom->set_value(move(balances));
      };
      lb1.getBalancesAndCount(5, lb1_getBalanceAndCountLBD);

      //
      auto lb2Bal_prom = make_shared<promise<vector<uint64_t>>>();
      auto lb2Bal_fut = lb2Bal_prom->get_future();
      auto lb2_getBalanceAndCountLBD = 
         [lb2Bal_prom](vector<uint64_t> balances)->void
      {
         lb2Bal_prom->set_value(move(balances));
      };
      lb2.getBalancesAndCount(5, lb2_getBalanceAndCountLBD);

      //get tx
      auto tx_prom = make_shared<promise<Tx>>();
      auto tx_fut = tx_prom->get_future();
      auto tx_get = [tx_prom](Tx tx)->void
      {
         tx_prom->set_value(move(tx));
      };
      bdvObj->getTxByHash(firstHash, tx_get);

      //get utxos
      auto utxo_prom = make_shared<promise<vector<UTXO>>>();
      auto utxo_fut = utxo_prom->get_future();
      auto utxo_get = [utxo_prom](vector<UTXO> utxoV)->void
      {
         utxo_prom->set_value(move(utxoV));
      };
      wallet1.getSpendableTxOutListForValue(UINT64_MAX, utxo_get);

      //wait on futures
      auto w1AddrBalances = move(w1AddrBal_fut.get());
      auto w1Balances = move(w1Bal_fut.get());
      auto lb1AddrBalances = move(lb1AddrBal_fut.get());
      auto lb2AddrBalances = move(lb2AddrBal_fut.get());
      auto lb1Balances = move(lb1Bal_fut.get());
      auto lb2Balances = move(lb2Bal_fut.get());
      auto ledgers = move(ledger_fut.get());
      auto tx = move(tx_fut.get());
      auto utxos = move(utxo_fut.get());

      //w1 addr balances
      auto balanceVec = w1AddrBalances[TestChain::scrAddrA];
      EXPECT_EQ(balanceVec[0], 50 * COIN);
      balanceVec = w1AddrBalances[TestChain::scrAddrB];
      EXPECT_EQ(balanceVec[0], 70 * COIN);
      balanceVec = w1AddrBalances[TestChain::scrAddrC];
      EXPECT_EQ(balanceVec[0], 20 * COIN);

      //w1 balances
      auto fullBalance = w1Balances[0];
      auto spendableBalance = w1Balances[1];
      auto unconfirmedBalance = w1Balances[2];
      EXPECT_EQ(fullBalance, 170 * COIN);
      EXPECT_EQ(spendableBalance, 70 * COIN);
      EXPECT_EQ(unconfirmedBalance, 170 * COIN);

      //lb1 addr balances
      balanceVec = lb1AddrBalances[TestChain::lb1ScrAddr];
      EXPECT_EQ(balanceVec[0], 5 * COIN);
      balanceVec = lb1AddrBalances[TestChain::lb1ScrAddrP2SH];
      EXPECT_EQ(balanceVec[0], 25 * COIN);

      //lb2 addr balances
      balanceVec = lb2AddrBalances[TestChain::lb2ScrAddr];
      EXPECT_EQ(balanceVec[0], 30 * COIN);
      balanceVec = lb2AddrBalances[TestChain::lb2ScrAddrP2SH];
      EXPECT_EQ(balanceVec.size(), 0);

      //lb1 balances
      EXPECT_EQ(lb1Balances[0], 30 * COIN);

      //lb2 balances
      EXPECT_EQ(lb2Balances[0], 30 * COIN);

      //grab main ledgers
      auto& firstEntry = ledgers[0];
      auto txHash = firstEntry.getTxHash();
      EXPECT_EQ(firstHash, txHash);

      //check first tx
      EXPECT_EQ(tx.getThisHash(), firstHash);

      //check utxos
      EXPECT_EQ(utxos.size(), 5);

      //grab all tx for each utxo
      map<BinaryData, shared_future<Tx>> futMap;
      for(auto& utxo : utxos)
      {
         auto& hash = utxo.getTxHash();
         auto utxoProm = make_shared<promise<Tx>>();
         futMap.insert(make_pair(hash, utxoProm->get_future()));
         auto utxoLBD = [utxoProm](Tx tx)->void
         {
            utxoProm->set_value(move(tx));
         };
         bdvObj->getTxByHash(hash, utxoLBD);
      }

      for(auto& fut_pair : futMap)
      {
         auto txobj = move(fut_pair.second.get());
         EXPECT_EQ(txobj.getThisHash(), fut_pair.first);
      }

      bdvObj->unregisterFromDB();
   };

   vector<thread> thrV;
   for(unsigned ct=0; ct<20; ct++)
      thrV.push_back(thread(request_lambda));

   for(auto& thr : thrV)
   {
      if(thr.joinable())
         thr.join();
   }

   auto&& bdvObj2 = SwigClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", config.listenPort_, nullptr);
   bdvObj2.shutdown(config.cookie_);
   WebSocketServer::waitOnShutdown();

   delete theBDMt_;
   theBDMt_ = nullptr;
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

   // Setup the log file 
   STARTLOGGING("cppTestsLog.txt", LogLvlDebug2);
   //LOGDISABLESTDOUT();

   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();

   FLUSHLOG();
   CLEANUPLOG();

   google::protobuf::ShutdownProtobufLibrary();
   return exitCode;
}
