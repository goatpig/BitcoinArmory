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
#include "../Wallets/Seeds/Seeds.h"
using namespace std;
using namespace Armory::Signer;
using namespace Armory::Config;
using namespace Armory::Assets;
using namespace Armory::Accounts;
using namespace Armory::Wallets;

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
      DBTestUtils::init();

      Armory::Config::reset();
      DBSettings::setServiceType(SERVICE_UNITTEST);
      Armory::Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_SUPER",
         "--thread-count=3"},
         Armory::Config::ProcessType::DB);

      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();

      auto nodePtr = dynamic_pointer_cast<NodeUnitTest>(
         NetworkSettings::bitcoinNodes().first);
      
      nodePtr->setBlockchain(theBDMt_->bdm()->blockchain());
      nodePtr->setBlockFiles(theBDMt_->bdm()->blockFiles());

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
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load5Blocks)
{
   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   StoredScriptHistory ssh;

   BinaryData scrA(TestChain::scrAddrA);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load5Blocks_ReloadBDM)
{
   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   StoredScriptHistory ssh;

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   //restart bdm
   clients_->exitRequestLoop();
   clients_->shutdown();

   delete clients_;
   delete theBDMt_;

   initBDM();

   auto&& subssh_sdbi = iface_->getStoredDBInfo(SUBSSH, 0);
   EXPECT_EQ(subssh_sdbi.topBlkHgt_, 5ULL);

   auto&& ssh_sdbi = iface_->getStoredDBInfo(SSH, 0);
   EXPECT_EQ(ssh_sdbi.topBlkHgt_, 5ULL);

   theBDMt_->start(DBSettings::initMode());
   bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load5Blocks_Reload_Rescan)
{
   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   StoredScriptHistory ssh;

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   //restart bdm
   clients_->exitRequestLoop();
   clients_->shutdown();

   delete clients_;
   delete theBDMt_;

   initBDM();

   auto&& subssh_sdbi = iface_->getStoredDBInfo(SUBSSH, 0);
   EXPECT_EQ(subssh_sdbi.topBlkHgt_, 5U);

   auto&& ssh_sdbi = iface_->getStoredDBInfo(SSH, 0);
   EXPECT_EQ(ssh_sdbi.topBlkHgt_, 5U);

   theBDMt_->start(INIT_RESCAN);
   bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load5Blocks_RescanSSH)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);


   StoredScriptHistory ssh;

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 160 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 9U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 55 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 55 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 5U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 10 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 20 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   //restart bdm
   clients_->exitRequestLoop();
   clients_->shutdown();

   delete clients_;
   delete theBDMt_;

   initBDM();

   auto&& subssh_sdbi = iface_->getStoredDBInfo(SUBSSH, 0);
   EXPECT_EQ(subssh_sdbi.topBlkHgt_, 3U);

   auto&& ssh_sdbi = iface_->getStoredDBInfo(SSH, 0);
   EXPECT_EQ(ssh_sdbi.topBlkHgt_, 3U);

   theBDMt_->start(INIT_SSH);
   bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 160 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 9U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 55 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 55 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 5U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 10 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 20 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   //restart bdm
   clients_->exitRequestLoop();
   clients_->shutdown();

   delete clients_;
   delete theBDMt_;


   initBDM();

   subssh_sdbi = iface_->getStoredDBInfo(SUBSSH, 0);
   EXPECT_EQ(subssh_sdbi.topBlkHgt_, 3U);

   ssh_sdbi = iface_->getStoredDBInfo(SSH, 0);
   EXPECT_EQ(ssh_sdbi.topBlkHgt_, 3U);

   //add next block
   TestUtils::appendBlocks({ "4" }, blk0dat_);

   theBDMt_->start(INIT_SSH);
   bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   subssh_sdbi = iface_->getStoredDBInfo(SUBSSH, 0);
   EXPECT_EQ(subssh_sdbi.topBlkHgt_, 4U);

   ssh_sdbi = iface_->getStoredDBInfo(SSH, 0);
   EXPECT_EQ(ssh_sdbi.topBlkHgt_, 4U);
   
   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 160 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 9U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 5U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 60 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   //add last block
   TestUtils::appendBlocks({ "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load3BlocksPlus3)
{
   // Copy only the first four blocks.  Will copy the full file next to test
   // readBlkFileUpdate method on non-reorg blocks.
   TestUtils::setBlocks({ "0", "1", "2" }, blk0dat_);

   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 2U);
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

   theBDMt_->start(DBSettings::initMode());
   bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   TestUtils::appendBlocks({ "4" }, blk0dat_);

   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 5U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, HEADERS), TestChain::blkHash5);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->
      getHeaderByHash(TestChain::blkHash5)->isMainBranch());

   StoredScriptHistory ssh;

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

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
   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5", "4A", "5A" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   StoredScriptHistory ssh;

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 160 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 9U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 55 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 55 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 60 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 95 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 20 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load5Blocks_ReloadBDM_Reorg)
{
   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   //reload BDM
   clients_->exitRequestLoop();
   clients_->shutdown();

   delete theBDMt_;
   delete clients_;

   initBDM();

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5", "4A", "5A" }, blk0dat_);

   theBDMt_->start(DBSettings::initMode());
   bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   EXPECT_EQ(theBDMt_->bdm()->blockchain()->top()->getBlockHeight(), 5U);

   StoredScriptHistory ssh;

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 160 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 9U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 55 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 55 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 60 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 95 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 20 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load5Blocks_DoubleReorg)
{
   StoredScriptHistory ssh;

   TestUtils::setBlocks({ "0", "1", "2", "3", "4A" }, blk0dat_);

   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   //first reorg: up to 5
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 25 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 40 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   //second reorg: up to 5A
   TestUtils::setBlocks({ "0", "1", "2", "3", "4A", "4", "5", "5A" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 160 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 9U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 55 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 55 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 60 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 60 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 95 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 15 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddr);
   EXPECT_EQ(ssh.getScriptBalance(), 10 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 20 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 3U);

   iface_->getStoredScriptHistory(ssh, TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 5 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsSuper, Load5Blocks_DynamicReorg_GrabSTXO)
{
   StoredScriptHistory ssh;
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);

   //grab utxos at height 3
   auto utxosB = DBTestUtils::getUtxoForAddress(
      clients_, bdvID, TestChain::scrAddrB, false);

   auto utxosC = DBTestUtils::getUtxoForAddress(
      clients_, bdvID, TestChain::scrAddrC, false);

   //mine till block 5
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 50 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 50 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 1U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 70 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 230 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 12U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 20 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 75 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 65 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 30 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 30 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 2U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 45 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7U);

   //reorg from block 3
   {
      auto headerPtr = theBDMt_->bdm()->blockchain()->getHeaderByHeight(3, 0xFF);
      DBTestUtils::setReorgBranchingPoint(theBDMt_, headerPtr->getThisHash());
   }
  
   //instantiate resolver feed overloaded object
   auto feed = make_shared<ResolverUtils::TestResolverFeed>();
   feed->addPrivKey(TestChain::privKeyAddrB);
   feed->addPrivKey(TestChain::privKeyAddrC);
   feed->addPrivKey(TestChain::privKeyAddrD);
   feed->addPrivKey(TestChain::privKeyAddrE);
   feed->addPrivKey(TestChain::privKeyAddrF);

   /*create the transactions*/

   //grab utxo from raw tx lambda
   auto getUtxoFromRawTx = [](BinaryData& rawTx, unsigned id)->UTXO
   {
      Tx tx(rawTx);
      if (id > tx.getNumTxOut())
         throw runtime_error("invalid txout count");

      auto&& txOut = tx.getTxOutCopy(id);

      UTXO utxo;
      utxo.unserializeRaw(txOut.serialize());
      utxo.txOutIndex_ = id;
      utxo.txHash_ = tx.getThisHash();

      return utxo;
   };

   //2x 2 outputs
   BinaryData rawTx1, rawTx2;

   {
      //50 from B, 5 to A, change to D
      Signer signer;

      auto spender = make_shared<ScriptSpender>(utxosB[0]);
      signer.addSpender(spender);

      auto recA = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
      signer.addRecipient(recA);

      auto recChange = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrD.getSliceCopy(1, 20), 
         spender->getValue() - recA->getValue());
      signer.addRecipient(recChange);

      signer.setFeed(feed);
      signer.sign();
      rawTx1 = signer.serializeSignedTx();
   }

   {
      //50 from C, 10 to E, change to F
      Signer signer;

      auto spender = make_shared<ScriptSpender>(utxosC[0]);
      signer.addSpender(spender);

      auto recE = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
      signer.addRecipient(recE);

      auto recChange = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrF.getSliceCopy(1, 20), 
         spender->getValue() - recE->getValue());
      signer.addRecipient(recChange);

      signer.setFeed(feed);
      signer.sign();
      rawTx2 = signer.serializeSignedTx();
   }

   //4 outputs
   BinaryData rawTx3;
   {
      //45 from D, 40 from F, 6 to A, 7 to E, 8 to D, change to C
      auto zcUtxo1 = getUtxoFromRawTx(rawTx1, 1);
      auto zcUtxo2 = getUtxoFromRawTx(rawTx2, 1);

      Signer signer;
      
      auto spender1 = make_shared<ScriptSpender>(zcUtxo1);
      auto spender2 = make_shared<ScriptSpender>(zcUtxo2);
      signer.addSpender(spender1);
      signer.addSpender(spender2);

      auto recA = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrA.getSliceCopy(1, 20), 6 * COIN);
      signer.addRecipient(recA);

      auto recE = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrE.getSliceCopy(1, 20), 7 * COIN);
      signer.addRecipient(recE);
      
      auto recD = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrD.getSliceCopy(1, 20), 8 * COIN);
      signer.addRecipient(recD);

      auto recChange = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrC.getSliceCopy(1, 20),
         spender1->getValue() + spender2->getValue() - 
         recA->getValue() - recE->getValue() - recD->getValue());
      signer.addRecipient(recChange);

      signer.setFeed(feed);
      signer.sign();
      rawTx3 = signer.serializeSignedTx();
   }

   /*stage the transactions*/

   //2 tx with 2 outputs each in block 4
   DBTestUtils::ZcVector zcVec4;
   zcVec4.push_back(rawTx1, 10000000, 0);
   zcVec4.push_back(rawTx2, 11000000, 0);
   DBTestUtils::pushNewZc(theBDMt_, zcVec4, true);

   //no tx in block 5

   //1 tx with 4 outputs in block 6, cover the roundabout zc delay setter
   DBTestUtils::setNextZcPushDelay(2);
   DBTestUtils::ZcVector zcVec6;
   zcVec6.push_back(rawTx3, 20000000, 0);
   DBTestUtils::pushNewZc(theBDMt_, zcVec6, true);

   /*reorg*/
   DBTestUtils::mineNewBlock(theBDMt_, 
      TestChain::scrAddrA.getSliceCopy(1, 20), 3);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   /*check balances*/
   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrA);
   EXPECT_EQ(ssh.getScriptBalance(), 211 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 211 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 6U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrB);
   EXPECT_EQ(ssh.getScriptBalance(), 0 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 160 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 10U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrC);
   EXPECT_EQ(ssh.getScriptBalance(), 49 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 99 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 13 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 38 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrE);
   EXPECT_EQ(ssh.getScriptBalance(), 47 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 47 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 4U);

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   EXPECT_EQ(ssh.getScriptReceived(), 80 * COIN);
   EXPECT_EQ(ssh.totalTxioCount_, 7U);

   /*grab STXOs*/

   //block 4
   StoredTxOut stxo1, stxo2;
   auto&& key4_0_0_0 = DBUtils::getBlkDataKeyNoPrefix(4, 0, 0, 0);
   EXPECT_TRUE(iface_->getStoredTxOut(stxo1, key4_0_0_0));

   auto&& key4_1_0_0 = DBUtils::getBlkDataKeyNoPrefix(4, 1, 0, 0);
   EXPECT_TRUE(iface_->getStoredTxOut(stxo2, key4_1_0_0));
   EXPECT_NE(stxo1.dataCopy_, stxo2.dataCopy_);

   //block 5
   StoredTxOut stxo3, stxo4, stxo5;
   auto&& key5_0_0_0 = DBUtils::getBlkDataKeyNoPrefix(5, 0, 0, 0);
   EXPECT_TRUE(iface_->getStoredTxOut(stxo3, key5_0_0_0));

   auto&& key5_1_0_0 = DBUtils::getBlkDataKeyNoPrefix(5, 1, 0, 0);
   EXPECT_TRUE(iface_->getStoredTxOut(stxo4, key5_1_0_0));
   EXPECT_NE(stxo3.dataCopy_, stxo4.dataCopy_);

   auto&& key5_0_1_0 = DBUtils::getBlkDataKeyNoPrefix(5, 0, 1, 0);
   EXPECT_TRUE(iface_->getStoredTxOut(stxo5, key5_0_1_0));

   //block 6
   StoredTxOut stxo6, stxo7;
   auto&& key6_0_1_0 = DBUtils::getBlkDataKeyNoPrefix(6, 0, 1, 0);
   EXPECT_TRUE(iface_->getStoredTxOut(stxo6, key6_0_1_0));

   auto&& key6_1_1_0 = DBUtils::getBlkDataKeyNoPrefix(6, 1, 1, 0);
   EXPECT_FALSE(iface_->getStoredTxOut(stxo7, key6_1_1_0));
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
      DBTestUtils::init();

      Armory::Config::reset();
      DBSettings::setServiceType(SERVICE_UNITTEST);
      Armory::Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_SUPER",
         "--thread-count=3"},
         Armory::Config::ProcessType::DB);

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

      initBDM();

      wallet1id = "wallet1";
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
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockUtilsWithWalletTest, Test_WithWallet)
{
   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);

   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

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

   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

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
TEST_F(BlockUtilsWithWalletTest, MultipleSigners_2of3_NativeP2WSH)
{
   //create spender lamba
   auto getSpenderPtr = [](const UTXO& utxo)->shared_ptr<ScriptSpender>
   {
      return make_shared<ScriptSpender>(utxo);
   };

   //
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   scrAddrVec.push_back(TestChain::scrAddrD);
   scrAddrVec.push_back(TestChain::scrAddrE);

   //// create 3 assetWlt ////
   WalletCreationParams params{{}, {}, homedir_, 3, 1, 1};

   //create a root private key
   unique_ptr<Armory::Seeds::ClearTextSeed> seed1(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_1 = AssetWallet_Single::createFromSeed(
      move(seed1), params);

   unique_ptr<Armory::Seeds::ClearTextSeed> seed2(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_2 = AssetWallet_Single::createFromSeed(
      move(seed2), params);

   unique_ptr<Armory::Seeds::ClearTextSeed> seed3(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_3 = AssetWallet_Single::createFromSeed(
      move(seed3), params);

          //create 2-of-3 multisig asset entry from 3 different wallets
   map<BinaryData, shared_ptr<AssetEntry>> asset_single_map;
   auto asset1 = TestUtils::getMainAccountAssetForIndex(assetWlt_1, 0);
   auto wltid1_bd = assetWlt_1->getID();
   asset_single_map.insert(make_pair(BinaryData::fromString(wltid1_bd), asset1));

   auto asset2 = TestUtils::getMainAccountAssetForIndex(assetWlt_2, 0);
   auto wltid2_bd = assetWlt_2->getID();
   asset_single_map.insert(make_pair(BinaryData::fromString(wltid2_bd), asset2));

   auto asset4_singlesig = assetWlt_2->getNewAddress();

   auto asset3 = TestUtils::getMainAccountAssetForIndex(assetWlt_3, 0);
   auto wltid3_bd = assetWlt_3->getID();
   asset_single_map.insert(make_pair(BinaryData::fromString(wltid3_bd), asset3));

   auto ae_ms = make_shared<AssetEntry_Multisig>(
      AssetId(0, 0, 0),
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
   auto ms_wlt = bdvPtr->getWalletOrLockbox("ms_entry");
   auto wlt_singleSig = bdvPtr->getWalletOrLockbox(assetWlt_2->getID());


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
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);

      //get utxo list for spend value
      auto&& unspentVec = wlt->getSpendableTxOutListForValue(spendVal);

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
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());
      auto&& zcHash = signer.getTxId();

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

      //grab ZC from DB and verify it again
      auto&& zc_from_db = DBTestUtils::getTxByHash(clients_, bdvID, zcHash);
      auto&& raw_tx = zc_from_db.serialize();
      auto bctx = BCTX::parse(raw_tx);
      Armory::Signer::TransactionVerifier tx_verifier(*bctx, utxoVec);

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
   auto feed_ms = make_shared<Armory::Signer::ResolverFeed_AssetWalletSingle_ForMultisig>(assetWlt_1);
   auto assetFeed = make_shared<ResolverUtils::CustomFeed>(addr_p2wsh, feed_ms);

   //create spenders
   uint64_t total = 0;
   for (auto& utxo : unspentVec)
   {
      total += utxo.getValue();
      signer2.addSpender(getSpenderPtr(utxo));
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
   signer2.setFeed(assetFeed);
   signer2.resolvePublicData();
   auto&& signerState = signer2.evaluateSignedState();

   {
      ASSERT_EQ(signerState.getEvalMapSize(), 2U);

      const auto& txinEval = signerState.getSignedStateForInput(0);
      const auto& pubkeyMap = txinEval.getPubKeyMap();
      EXPECT_EQ(pubkeyMap.size(), 3U);
      for (const auto& pubkeyState : pubkeyMap)
         EXPECT_FALSE(pubkeyState.second);

      const auto& txinEval2 = signerState.getSignedStateForInput(1);
      const auto& pubkeyMap_2 = txinEval2.getPubKeyMap();
      EXPECT_EQ(pubkeyMap_2.size(), 0U);
   }

   {
      auto lock = assetWlt_1->lockDecryptedContainer();
      signer2.sign();
   }

   EXPECT_FALSE(signer2.verify());

   {
      //signer state with 1 sig
      EXPECT_FALSE(signer2.isSigned());
      signerState = signer2.evaluateSignedState();

      EXPECT_EQ(signerState.getEvalMapSize(), 2U);

      const auto& txinEval = signerState.getSignedStateForInput(0);
      EXPECT_EQ(txinEval.getSigCount(), 1U);

      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset1);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));
   }

   Signer signer3;
   //create feed from asset wallet 2
   auto feed_ms3 = make_shared<Armory::Signer::ResolverFeed_AssetWalletSingle_ForMultisig>(assetWlt_2);
   auto assetFeed3 = make_shared<ResolverUtils::CustomFeed>(addr_p2wsh, feed_ms3);
   signer3.deserializeState(signer2.serializeState());

   {
      //make sure sig was properly carried over with state
      EXPECT_FALSE(signer3.isSigned());
      signerState = signer3.evaluateSignedState();

      EXPECT_EQ(signerState.getEvalMapSize(), 2U);
      const auto& txinEval = signerState.getSignedStateForInput(0);
      EXPECT_EQ(txinEval.getSigCount(), 1U);

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
      auto assetFeed4 = make_shared<Armory::Signer::ResolverFeed_AssetWalletSingle>(assetWlt_2);
      signer3.resetFeed();
      signer3.setFeed(assetFeed4);
      auto lock = assetWlt_2->lockDecryptedContainer();
      signer3.sign();
   }


   ASSERT_TRUE(signer3.isSigned());
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
      EXPECT_TRUE(signer3.isSigned());
      signerState = signer3.evaluateSignedState();

      EXPECT_EQ(signerState.getEvalMapSize(), 2U);
      auto&& txinEval = signerState.getSignedStateForInput(0);
      EXPECT_EQ(txinEval.getSigCount(), 2U);

      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset1);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));

      asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset2);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));
   }

   auto&& tx1 = signer3.serializeSignedTx();
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
   Armory::Signer::TransactionVerifier tx_verifier(*bctx, unspentVec);

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
class WebSocketTests : public ::testing::Test
{
protected:
   BlockDataManagerThread *theBDMt_;
   PassphraseLambda authPeersPassLbd_;

   void initBDM(void)
   {
      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();

      nodePtr_ = dynamic_pointer_cast<NodeUnitTest>(
         NetworkSettings::bitcoinNodes().first);

      rpcNode_ = dynamic_pointer_cast<NodeRPC_UnitTest>(
         NetworkSettings::rpcNode());

      nodePtr_->setIface(iface_);
      nodePtr_->setBlockchain(theBDMt_->bdm()->blockchain());
      nodePtr_->setBlockFiles(theBDMt_->bdm()->blockFiles());
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

      startupBIP151CTX();
      startupBIP150CTX(4);

      DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);
      Armory::Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_SUPER",
         "--thread-count=3",
         "--public",
         "--cookie"},
         Armory::Config::ProcessType::DB);

      //setup auth peers for server and client
      authPeersPassLbd_ = [](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData::fromString("authpeerpass");
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

      wallet1id = "wallet1";

      initBDM();
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      shutdownBIP151CTX();
      
      delete theBDMt_;
      theBDMt_ = nullptr;

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

   shared_ptr<NodeUnitTest> nodePtr_;
   shared_ptr<NodeRPC_UnitTest> rpcNode_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(WebSocketTests, DISABLED_WebSocketStack_ParallelAsync)
{
   //
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   auto&& firstHash = READHEX("b6b6f145742a9072fd85f96772e63a00eb4101709aa34ec5dd59e8fc904191a7");

   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_, true);
   auto&& serverPubkey = WebSocketServer::getPublicKey();

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

   auto&& _scrAddrVec = createNAddresses(2000);
   _scrAddrVec.push_back(TestChain::scrAddrA);
   _scrAddrVec.push_back(TestChain::scrAddrB);
   _scrAddrVec.push_back(TestChain::scrAddrC);
   _scrAddrVec.push_back(TestChain::scrAddrE);

   theBDMt_->start(DBSettings::initMode());

   {
      auto pCallback = make_shared<DBTestUtils::UTCallback>();
      auto&& bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::listenPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_, 
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());
      
      auto&& wallet1 = bdvObj->instantiateWallet("wallet1");
      vector<string> walletRegIDs;
      walletRegIDs.push_back(
         wallet1.registerAddresses(_scrAddrVec, false));

      //wait on registration ack
      pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      auto delegate = move(DBTestUtils::getLedgerDelegate(bdvObj));
      auto ledgers = move(DBTestUtils::getHistoryPage(delegate, 0));

      bdvObj->unregisterFromDB();
   }

   unsigned nThreads = 50;
   vector<shared_ptr<atomic<unsigned>>> times(nThreads);
   for (unsigned z=0; z<nThreads; z++)
      times[z] = make_shared<atomic<unsigned>>();
   atomic<unsigned> counter = {0};
   auto request_lambda = [&](void)->void
   {
      auto this_id = counter.fetch_add(1, memory_order_relaxed);
      auto rightnow = chrono::system_clock::now();
      auto&& scrAddrVec = createNAddresses(6);
      scrAddrVec.push_back(TestChain::scrAddrA);
      scrAddrVec.push_back(TestChain::scrAddrB);
      scrAddrVec.push_back(TestChain::scrAddrC);
      scrAddrVec.push_back(TestChain::scrAddrE);

      auto pCallback = make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::listenPort(), 
         Armory::Config::getDataDir(),
         authPeersPassLbd_, 
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

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

      scrAddrVec.push_back(TestChain::scrAddrD);
      auto&& wallet2 = bdvObj->instantiateWallet("wallet2");
      walletRegIDs.push_back(
         wallet2.registerAddresses(scrAddrVec, false));

      auto&& lb1 = bdvObj->instantiateLockbox("lb1");
      walletRegIDs.push_back(
         lb1.registerAddresses(lb1ScrAddrs, false));

      auto&& lb2 = bdvObj->instantiateLockbox("lb2");
      walletRegIDs.push_back(
         lb2.registerAddresses(lb2ScrAddrs, false));

      //wait on registration ack
      pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);


      //get wallets delegate
      auto del1_prom = make_shared<promise<AsyncClient::LedgerDelegate>>();
      auto del1_fut = del1_prom->get_future();
      auto del1_get = [del1_prom](ReturnMessage<AsyncClient::LedgerDelegate> delegate)->void
      {
         del1_prom->set_value(move(delegate.get()));
      };
      bdvObj->getLedgerDelegateForWallets(del1_get);

      vector<AsyncClient::LedgerDelegate> delV(21);

      auto getAddrDelegate = [bdvObj](const BinaryData& scrAddr, 
         string walletId, AsyncClient::LedgerDelegate* delPtr)->void
      {
         //get scrAddr delegates
         auto del_prom = make_shared<promise<AsyncClient::LedgerDelegate>>();
         auto del_fut = del_prom->get_future();
         auto del_get = [del_prom](ReturnMessage<AsyncClient::LedgerDelegate> delegate)->void
         {
            del_prom->set_value(move(delegate.get()));
         };
         bdvObj->getLedgerDelegateForScrAddr(
            walletId, scrAddr, del_get);
         *delPtr = move(del_fut.get());
      };
      
      auto delegate = move(del1_fut.get());

      deque<thread> delThr;
      for (unsigned i = 0; i < 10; i++)
      {
         delThr.push_back(
            thread(getAddrDelegate, scrAddrVec[i], "wallet1", &delV[i]));
      }

      for (unsigned i = 10; i < 21; i++)
      {
         delThr.push_back(
            thread(getAddrDelegate, scrAddrVec[i - 10], "wallet2", &delV[i]));
      }

      for (auto& thr : delThr)
      {
         if (thr.joinable())
            thr.join();
      }

      //get ledgers
      auto ledger_prom = 
         make_shared<promise<vector<DBClientClasses::LedgerEntry>>>();
      auto ledger_fut = ledger_prom->get_future();
      auto ledger_get = 
         [ledger_prom](ReturnMessage<vector<DBClientClasses::LedgerEntry>> ledgerV)->void
      {
         ledger_prom->set_value(move(ledgerV.get()));
      };
      delegate.getHistoryPage(0, ledger_get);

      //get addr ledgers
      deque<vector<DBClientClasses::LedgerEntry>> addrLedgerV(21);
      auto getAddrLedger = [bdvObj](
         AsyncClient::LedgerDelegate delegate, 
         vector<DBClientClasses::LedgerEntry>* addrLedger)->void
      {
         auto ledger_prom = 
            make_shared<promise<vector<DBClientClasses::LedgerEntry>>>();
         auto ledger_fut = ledger_prom->get_future();
         auto ledger_get = 
            [ledger_prom](ReturnMessage<vector<DBClientClasses::LedgerEntry>> ledgerV)->void
         {
            ledger_prom->set_value(move(ledgerV.get()));
         };

         delegate.getHistoryPage(0, ledger_get);
         *addrLedger = move(ledger_fut.get());
      };

      delThr.clear();

      for (unsigned i = 0; i < 21; i++)
         delThr.push_back(thread(getAddrLedger, delV[i], &addrLedgerV[i]));

      //
      auto w1AddrBal_prom = make_shared<promise<map<BinaryData, vector<uint64_t>>>>();
      auto w1AddrBal_fut = w1AddrBal_prom->get_future();
      auto w1_getAddrBalancesLBD = 
         [w1AddrBal_prom](ReturnMessage<map<BinaryData, vector<uint64_t>>> balances)->void
      {
         w1AddrBal_prom->set_value(move(balances.get()));
      };
      wallet1.getAddrBalancesFromDB(w1_getAddrBalancesLBD);
      
      //
      auto w1Bal_prom = make_shared<promise<vector<uint64_t>>>();
      auto w1Bal_fut = w1Bal_prom->get_future();
      auto w1_getBalanceAndCountLBD = 
         [w1Bal_prom](ReturnMessage<vector<uint64_t>> balances)->void
      {
         w1Bal_prom->set_value(move(balances.get()));
      };
      wallet1.getBalancesAndCount(5, w1_getBalanceAndCountLBD);

      //
      auto lb1AddrBal_prom = make_shared<promise<map<BinaryData, vector<uint64_t>>>>();
      auto lb1AddrBal_fut = lb1AddrBal_prom->get_future();
      auto lb1_getAddrBalancesLBD = 
         [lb1AddrBal_prom](ReturnMessage<map<BinaryData, vector<uint64_t>>> balances)->void
      {
         lb1AddrBal_prom->set_value(move(balances.get()));
      };
      lb1.getAddrBalancesFromDB(lb1_getAddrBalancesLBD);

      //
      auto lb2AddrBal_prom = make_shared<promise<map<BinaryData, vector<uint64_t>>>>();
      auto lb2AddrBal_fut = lb2AddrBal_prom->get_future();
      auto lb2_getAddrBalancesLBD = 
         [lb2AddrBal_prom](ReturnMessage<map<BinaryData, vector<uint64_t>>> balances)->void
      {
         lb2AddrBal_prom->set_value(move(balances.get()));
      };
      lb2.getAddrBalancesFromDB(lb2_getAddrBalancesLBD);

      //
      auto lb1Bal_prom = make_shared<promise<vector<uint64_t>>>();
      auto lb1Bal_fut = lb1Bal_prom->get_future();
      auto lb1_getBalanceAndCountLBD = 
         [lb1Bal_prom](ReturnMessage<vector<uint64_t>> balances)->void
      {
         lb1Bal_prom->set_value(move(balances.get()));
      };
      lb1.getBalancesAndCount(5, lb1_getBalanceAndCountLBD);

      //
      auto lb2Bal_prom = make_shared<promise<vector<uint64_t>>>();
      auto lb2Bal_fut = lb2Bal_prom->get_future();
      auto lb2_getBalanceAndCountLBD = 
         [lb2Bal_prom](ReturnMessage<vector<uint64_t>> balances)->void
      {
         lb2Bal_prom->set_value(move(balances.get()));
      };
      lb2.getBalancesAndCount(5, lb2_getBalanceAndCountLBD);

      //get tx
      auto tx_prom = make_shared<promise<AsyncClient::TxResult>>();
      auto tx_fut = tx_prom->get_future();
      auto tx_get = [tx_prom](ReturnMessage<AsyncClient::TxResult> tx)->void
      {
         tx_prom->set_value(move(tx.get()));
      };
      bdvObj->getTxByHash(firstHash, tx_get);

      //get utxos
      auto utxo_prom = make_shared<promise<vector<UTXO>>>();
      auto utxo_fut = utxo_prom->get_future();
      auto utxo_get = [utxo_prom](ReturnMessage<vector<UTXO>> utxoV)->void
      {
         utxo_prom->set_value(move(utxoV.get()));
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
      EXPECT_EQ(balanceVec.size(), 0ULL);

      //lb1 balances
      EXPECT_EQ(lb1Balances[0], 30 * COIN);

      //lb2 balances
      EXPECT_EQ(lb2Balances[0], 30 * COIN);

      //grab main ledgers
      auto& firstEntry = ledgers[1];
      auto txHash = firstEntry.getTxHash();
      EXPECT_EQ(firstHash, txHash);

      //check first tx
      EXPECT_EQ(tx->getThisHash(), firstHash);

      //check utxos
      EXPECT_EQ(utxos.size(), 5ULL);

      //grab all tx for each utxo
      map<BinaryData, shared_future<AsyncClient::TxResult>> futMap;
      for(auto& utxo : utxos)
      {
         auto& hash = utxo.getTxHash();
         if (futMap.find(hash) != futMap.end())
            continue;

         auto utxoProm = make_shared<promise<AsyncClient::TxResult>>();
         futMap.insert(make_pair(hash, utxoProm->get_future()));
         auto utxoLBD = [utxoProm](ReturnMessage<AsyncClient::TxResult> tx)->void
         {
            utxoProm->set_value(move(tx.get()));
         };
         bdvObj->getTxByHash(hash, utxoLBD);
      }

      for(auto& fut_pair : futMap)
      {
         auto txobj = move(fut_pair.second.get());
         EXPECT_EQ(txobj->getThisHash(), fut_pair.first);
      }

      for (auto& thr : delThr)
      {
         if (thr.joinable())
            thr.join();
      }

      for (unsigned i = 0; i < 6; i++)
         EXPECT_EQ(addrLedgerV[i].size(), 0ULL);
      EXPECT_EQ(addrLedgerV[6].size(), 1ULL);
      EXPECT_EQ(addrLedgerV[7].size(), 7ULL);
      EXPECT_EQ(addrLedgerV[8].size(), 4ULL);
      EXPECT_EQ(addrLedgerV[9].size(), 2ULL);
      EXPECT_EQ(addrLedgerV[20].size(), 4ULL);

      for (unsigned i = 0; i < 10; i++)
      {
         auto& v1 = addrLedgerV[i];
         auto& v2 = addrLedgerV[i + 10];

         if (v1.size() != v2.size())
         {
            EXPECT_TRUE(false);
         }

         for (unsigned y = 0; y < v1.size(); y++)
         {
            if(!(v1[y] == v2[y]))
            {
               EXPECT_TRUE(false);
            }
         }
      }

      auto rekeyCount = bdvObj->getRekeyCount();
      EXPECT_EQ(rekeyCount.first, 2U);
      EXPECT_TRUE(rekeyCount.second > 7U);
      bdvObj->unregisterFromDB();

      auto time_ms = chrono::duration_cast<chrono::milliseconds>(
         chrono::system_clock::now() - rightnow);
      times[this_id]->store(time_ms.count(), memory_order_relaxed);
   };

   vector<thread> thrV;
   for(unsigned ct=0; ct<nThreads; ct++)
      thrV.push_back(thread(request_lambda));

   for(auto& thr : thrV)
   {
      if(thr.joinable())
         thr.join();
   }

   {
      struct comparator
      {
         inline bool operator()(const shared_ptr<atomic<unsigned>>& lhs, const shared_ptr<atomic<unsigned>>& rhs)
         {
            return lhs->load(memory_order_relaxed) < rhs->load(memory_order_relaxed);
         }
      };

      sort(times.begin(), times.end(), comparator());
      unsigned total = 0;
      for (auto& tp : times)
         total += tp->load(memory_order_relaxed);
      
      cout << "completion average: " << total / nThreads << endl;
      cout << "top 5:" << endl;
      for (unsigned i=nThreads -1 ; i>nThreads - 6; i--)
         cout << "  " << *times[i] << endl;

      cout << "bottom 5:" << endl;
      for (unsigned i=0; i<5; i++)
         cout << "  " << *times[i] << endl;
   }

   auto&& bdvObj2 = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::listenPort(), Armory::Config::getDataDir(),
      authPeersPassLbd_, NetworkSettings::ephemeralPeers(), true, nullptr);
   bdvObj2->addPublicKey(serverPubkey);
   bdvObj2->connectToRemote();

   bdvObj2->shutdown(NetworkSettings::cookie());
   WebSocketServer::waitOnShutdown();

   EXPECT_EQ(theBDMt_->bdm()->zeroConfCont()->getMatcherMapSize(), 0U);

   delete theBDMt_;
   theBDMt_ = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WebSocketTests, DISABLED_WebSocketStack_ParallelAsync_ShutdownClients)
{
   /***
   Create a lot of client connections in parallel and slam the db with requests,
   then shutdown some of the clients before the requests are met.
   ***/

   //
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   auto&& firstHash = READHEX("b6b6f145742a9072fd85f96772e63a00eb4101709aa34ec5dd59e8fc904191a7");

   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_, true);
   auto&& serverPubkey = WebSocketServer::getPublicKey();

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

   auto&& _scrAddrVec = createNAddresses(2000);
   _scrAddrVec.push_back(TestChain::scrAddrA);
   _scrAddrVec.push_back(TestChain::scrAddrB);
   _scrAddrVec.push_back(TestChain::scrAddrC);
   _scrAddrVec.push_back(TestChain::scrAddrE);

   theBDMt_->start(DBSettings::initMode());

   {
      auto pCallback = make_shared<DBTestUtils::UTCallback>();
      auto&& bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::listenPort(), 
         Armory::Config::getDataDir(),
         authPeersPassLbd_, 
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());
      
      auto&& wallet1 = bdvObj->instantiateWallet("wallet1");
      vector<string> walletRegIDs;
      walletRegIDs.push_back(
         wallet1.registerAddresses(_scrAddrVec, false));

      //wait on registration ack
      pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      auto delegate = move(DBTestUtils::getLedgerDelegate(bdvObj));
      auto ledgers = move(DBTestUtils::getHistoryPage(delegate, 0));

      bdvObj->unregisterFromDB();
   }

   unsigned nThreads = 3;
   atomic<unsigned> counter = {0};
   auto request_lambda = [&](void)->void
   {
      unsigned killCount = 0;
      auto this_id = counter.fetch_add(1, memory_order_relaxed);
      auto checkForTermination = [this_id, &killCount](void)->bool
      {
         if (this_id % 3 != 0)
            return false;

         auto rndVal = CryptoPRNG::generateRandom(1);
         ++killCount;
         return rndVal.getPtr()[0] % 3 || killCount == 3;
      };

      /*
      kill 1/3rd of threads at different spots
      */ 
      auto&& scrAddrVec = createNAddresses(6);
      scrAddrVec.push_back(TestChain::scrAddrA);
      scrAddrVec.push_back(TestChain::scrAddrB);
      scrAddrVec.push_back(TestChain::scrAddrC);
      scrAddrVec.push_back(TestChain::scrAddrE);

      auto pCallback = make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::listenPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_, 
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

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

      scrAddrVec.push_back(TestChain::scrAddrD);
      auto&& wallet2 = bdvObj->instantiateWallet("wallet2");
      walletRegIDs.push_back(
         wallet2.registerAddresses(scrAddrVec, false));

      auto&& lb1 = bdvObj->instantiateLockbox("lb1");
      walletRegIDs.push_back(
         lb1.registerAddresses(lb1ScrAddrs, false));

      auto&& lb2 = bdvObj->instantiateLockbox("lb2");
      walletRegIDs.push_back(
         lb2.registerAddresses(lb2ScrAddrs, false));

      //wait on registration ack
      pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

      //get wallets delegate
      auto del1_prom = make_shared<promise<AsyncClient::LedgerDelegate>>();
      auto del1_fut = del1_prom->get_future();
      auto del1_get = [del1_prom](ReturnMessage<AsyncClient::LedgerDelegate> delegate)->void
      {
         del1_prom->set_value(move(delegate.get()));
      };
      bdvObj->getLedgerDelegateForWallets(del1_get);

      vector<shared_ptr<AsyncClient::LedgerDelegate>> delV(2);
      for (auto& delPtr : delV)
         delPtr = make_shared<AsyncClient::LedgerDelegate>();

      auto getAddrDelegate = [bdvObj](const BinaryData& scrAddr, 
         string walletId, shared_ptr<AsyncClient::LedgerDelegate> delPtr)->void
      {
         //get scrAddr delegates
         auto del_prom = make_shared<promise<AsyncClient::LedgerDelegate>>();
         auto del_fut = del_prom->get_future();
         auto del_get = [del_prom](ReturnMessage<AsyncClient::LedgerDelegate> delegate)->void
         {
            del_prom->set_value(move(delegate.get()));
         };
         bdvObj->getLedgerDelegateForScrAddr(
            walletId, scrAddr, del_get);
         *delPtr = del_fut.get();
      };
      
      auto delegate = move(del1_fut.get());

      deque<thread> delThr;
      for (unsigned i = 0; i < 1; i++)
      {
         delThr.push_back(
            thread(getAddrDelegate, scrAddrVec[i], "wallet1", delV[i]));
      }

      for (unsigned i = 1; i < 2; i++)
      {
         delThr.push_back(
            thread(getAddrDelegate, scrAddrVec[i], "wallet2", delV[i]));
      }

      //first termination spot
      if (checkForTermination())
      {
         cout << "out at first spot" << endl;
         return;
      }

      for (auto& thr : delThr)
      {
         if (thr.joinable())
            thr.join();
      }

      /*
      //get ledgers
      auto ledger_prom = 
         make_shared<promise<vector<::ClientClasses::LedgerEntry>>>();
      auto ledger_fut = ledger_prom->get_future();
      auto ledger_get = 
         [ledger_prom](ReturnMessage<vector<::ClientClasses::LedgerEntry>> ledgerV)->void
      {
         ledger_prom->set_value(move(ledgerV.get()));
      };
      delegate.getHistoryPage(0, ledger_get);

      //get addr ledgers
      deque<shared_ptr<vector<::ClientClasses::LedgerEntry>>> addrLedgerV(21);
      for (auto& addrLedgers : addrLedgerV)
         addrLedgers = make_shared<vector<::ClientClasses::LedgerEntry>>();

      auto getAddrLedger = [bdvObj](
         shared_ptr<AsyncClient::LedgerDelegate> delegate, 
         shared_ptr<vector<::ClientClasses::LedgerEntry>> addrLedger)->void
      {
         auto ledger_prom = 
            make_shared<promise<vector<::ClientClasses::LedgerEntry>>>();
         auto ledger_fut = ledger_prom->get_future();
         auto ledger_get = 
            [ledger_prom](ReturnMessage<vector<::ClientClasses::LedgerEntry>> ledgerV)->void
         {
            ledger_prom->set_value(move(ledgerV.get()));
         };

         delegate->getHistoryPage(0, ledger_get);
         *addrLedger = move(ledger_fut.get());
      };

      delThr.clear();

      for (unsigned i = 0; i < 21; i++)
         delThr.push_back(thread(getAddrLedger, delV[i], addrLedgerV[i]));


      //second termination spot
      if (checkForTermination())
      {
         cout << "out at second spot" << endl;
         return;
      }

      //
      auto w1AddrBal_prom = make_shared<promise<map<BinaryData, vector<uint64_t>>>>();
      auto w1AddrBal_fut = w1AddrBal_prom->get_future();
      auto w1_getAddrBalancesLBD = 
         [w1AddrBal_prom](ReturnMessage<map<BinaryData, vector<uint64_t>>> balances)->void
      {
         w1AddrBal_prom->set_value(move(balances.get()));
      };
      wallet1.getAddrBalancesFromDB(w1_getAddrBalancesLBD);
      
      //
      auto w1Bal_prom = make_shared<promise<vector<uint64_t>>>();
      auto w1Bal_fut = w1Bal_prom->get_future();
      auto w1_getBalanceAndCountLBD = 
         [w1Bal_prom](ReturnMessage<vector<uint64_t>> balances)->void
      {
         w1Bal_prom->set_value(move(balances.get()));
      };
      wallet1.getBalancesAndCount(5, w1_getBalanceAndCountLBD);

      //
      auto lb1AddrBal_prom = make_shared<promise<map<BinaryData, vector<uint64_t>>>>();
      auto lb1AddrBal_fut = lb1AddrBal_prom->get_future();
      auto lb1_getAddrBalancesLBD = 
         [lb1AddrBal_prom](ReturnMessage<map<BinaryData, vector<uint64_t>>> balances)->void
      {
         lb1AddrBal_prom->set_value(move(balances.get()));
      };
      lb1.getAddrBalancesFromDB(lb1_getAddrBalancesLBD);

      //
      auto lb2AddrBal_prom = make_shared<promise<map<BinaryData, vector<uint64_t>>>>();
      auto lb2AddrBal_fut = lb2AddrBal_prom->get_future();
      auto lb2_getAddrBalancesLBD = 
         [lb2AddrBal_prom](ReturnMessage<map<BinaryData, vector<uint64_t>>> balances)->void
      {
         lb2AddrBal_prom->set_value(move(balances.get()));
      };
      lb2.getAddrBalancesFromDB(lb2_getAddrBalancesLBD);

      //
      auto lb1Bal_prom = make_shared<promise<vector<uint64_t>>>();
      auto lb1Bal_fut = lb1Bal_prom->get_future();
      auto lb1_getBalanceAndCountLBD = 
         [lb1Bal_prom](ReturnMessage<vector<uint64_t>> balances)->void
      {
         lb1Bal_prom->set_value(move(balances.get()));
      };
      lb1.getBalancesAndCount(5, lb1_getBalanceAndCountLBD);

      //
      auto lb2Bal_prom = make_shared<promise<vector<uint64_t>>>();
      auto lb2Bal_fut = lb2Bal_prom->get_future();
      auto lb2_getBalanceAndCountLBD = 
         [lb2Bal_prom](ReturnMessage<vector<uint64_t>> balances)->void
      {
         lb2Bal_prom->set_value(move(balances.get()));
      };
      lb2.getBalancesAndCount(5, lb2_getBalanceAndCountLBD);

      //get tx
      auto tx_prom = make_shared<promise<Tx>>();
      auto tx_fut = tx_prom->get_future();
      auto tx_get = [tx_prom](ReturnMessage<Tx> tx)->void
      {
         tx_prom->set_value(move(tx.get()));
      };
      bdvObj->getTxByHash(firstHash, tx_get);

      //get utxos
      auto utxo_prom = make_shared<promise<vector<UTXO>>>();
      auto utxo_fut = utxo_prom->get_future();
      auto utxo_get = [utxo_prom](ReturnMessage<vector<UTXO>> utxoV)->void
      {
         utxo_prom->set_value(move(utxoV.get()));
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
      auto& firstEntry = ledgers[1];
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
         if (futMap.find(hash) != futMap.end())
            continue;

         auto utxoProm = make_shared<promise<Tx>>();
         futMap.insert(make_pair(hash, utxoProm->get_future()));
         auto utxoLBD = [utxoProm](ReturnMessage<Tx> tx)->void
         {
            utxoProm->set_value(move(tx.get()));
         };
         bdvObj->getTxByHash(hash, utxoLBD);
      }

      //third termination spot
      if (checkForTermination())
      {
         cout << "out at third spot" << endl;
         return;
      }

      for(auto& fut_pair : futMap)
      {
         auto txobj = move(fut_pair.second.get());
         EXPECT_EQ(txobj.getThisHash(), fut_pair.first);
      }

      for (auto& thr : delThr)
      {
         if (thr.joinable())
            thr.join();
      }

      for (unsigned i = 0; i < 6; i++)
         EXPECT_EQ(addrLedgerV[i]->size(), 0);
      EXPECT_EQ(addrLedgerV[6]->size(), 1);
      EXPECT_EQ(addrLedgerV[7]->size(), 7);
      EXPECT_EQ(addrLedgerV[8]->size(), 4);
      EXPECT_EQ(addrLedgerV[9]->size(), 2);
      EXPECT_EQ(addrLedgerV[20]->size(), 4);

      for (unsigned i = 0; i < 10; i++)
      {
         auto& v1 = addrLedgerV[i];         
         auto& v2 = addrLedgerV[i + 10];

         if (v1->size() != v2->size())
            EXPECT_TRUE(false);

         for (unsigned y = 0; y < v1->size(); y++)
         {
            if(!((*v1)[y] == (*v2)[y]))
               EXPECT_TRUE(false);
         }
      }

      auto rekeyCount = bdvObj->getRekeyCount();
      EXPECT_EQ(rekeyCount.first, 2);
      EXPECT_TRUE(rekeyCount.second > 7);
      */
      bdvObj->unregisterFromDB();
   };

   vector<thread> thrV;
   for(unsigned ct=0; ct<nThreads; ct++)
      thrV.push_back(thread(request_lambda));

   for(auto& thr : thrV)
   {
      if(thr.joinable())
         thr.join();
   }

   auto&& bdvObj2 = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::listenPort(), Armory::Config::getDataDir(),
      authPeersPassLbd_, NetworkSettings::ephemeralPeers(), true, nullptr);
   bdvObj2->addPublicKey(serverPubkey);
   bdvObj2->connectToRemote();

   bdvObj2->shutdown(NetworkSettings::cookie());
   WebSocketServer::waitOnShutdown();

   EXPECT_EQ(theBDMt_->bdm()->zeroConfCont()->getMatcherMapSize(), 0U);

   delete theBDMt_;
   theBDMt_ = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WebSocketTests, WebSocketStack_ManyLargeWallets)
{
   //
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_, true);
   auto&& serverPubkey = WebSocketServer::getPublicKey();

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

   auto&& _scrAddrVec1 = createNAddresses(2000);
   _scrAddrVec1.push_back(TestChain::scrAddrA);

   auto&& _scrAddrVec2 = createNAddresses(3);

   auto&& _scrAddrVec3 = createNAddresses(1500);
   _scrAddrVec3.push_back(TestChain::scrAddrB);

   auto&& _scrAddrVec4 = createNAddresses(4);

   auto&& _scrAddrVec5 = createNAddresses(4000);
   _scrAddrVec5.push_back(TestChain::scrAddrC);

   auto&& _scrAddrVec6 = createNAddresses(2);

   auto&& _scrAddrVec7 = createNAddresses(4000);
   _scrAddrVec7.push_back(TestChain::scrAddrE);

   auto&& _scrAddrVec8 = createNAddresses(2);

   theBDMt_->start(DBSettings::initMode());

   {
      auto pCallback = make_shared<DBTestUtils::UTCallback>();
      auto&& bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::listenPort(), 
         Armory::Config::getDataDir(),
         authPeersPassLbd_, 
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());

      auto&& wallet1 = bdvObj->instantiateWallet("wallet1");
      vector<string> walletRegIDs;
      walletRegIDs.push_back(
         wallet1.registerAddresses(_scrAddrVec1, false));

      auto&& wallet2 = bdvObj->instantiateWallet("wallet2");
      walletRegIDs.push_back(
         wallet2.registerAddresses(_scrAddrVec2, false));

      auto&& wallet3 = bdvObj->instantiateWallet("wallet3");
      walletRegIDs.push_back(
         wallet3.registerAddresses(_scrAddrVec3, false));

      auto&& wallet4 = bdvObj->instantiateWallet("wallet4");
      walletRegIDs.push_back(
         wallet4.registerAddresses(_scrAddrVec4, false));

      auto&& wallet5 = bdvObj->instantiateWallet("wallet5");
      walletRegIDs.push_back(
         wallet5.registerAddresses(_scrAddrVec5, false));

      auto&& wallet6 = bdvObj->instantiateWallet("wallet6");
      walletRegIDs.push_back(
         wallet6.registerAddresses(_scrAddrVec6, false));

      auto&& wallet7 = bdvObj->instantiateWallet("wallet7");
      walletRegIDs.push_back(
         wallet7.registerAddresses(_scrAddrVec7, false));

      auto&& wallet8 = bdvObj->instantiateWallet("wallet8");
      walletRegIDs.push_back(
         wallet8.registerAddresses(_scrAddrVec8, false));

      //wait on registration ack
      pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);
      bdvObj->unregisterFromDB();
   }

   //cleanup
   auto&& bdvObj2 = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::listenPort(), Armory::Config::getDataDir(),
      authPeersPassLbd_, NetworkSettings::ephemeralPeers(), true, nullptr);
   bdvObj2->addPublicKey(serverPubkey);
   bdvObj2->connectToRemote();

   bdvObj2->shutdown(NetworkSettings::cookie());
   WebSocketServer::waitOnShutdown();

   EXPECT_EQ(theBDMt_->bdm()->zeroConfCont()->getMatcherMapSize(), 0U);

   delete theBDMt_;
   theBDMt_ = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WebSocketTests, WebSocketStack_AddrOpLoop)
{
   auto feed = make_shared<ResolverUtils::TestResolverFeed>();
   feed->addPrivKey(TestChain::privKeyAddrB);

   //
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_, true);
   auto&& serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

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

   auto&& _scrAddrVec1 = createNAddresses(20);
   _scrAddrVec1.push_back(TestChain::scrAddrA);
   _scrAddrVec1.push_back(TestChain::scrAddrB);
   _scrAddrVec1.push_back(TestChain::scrAddrC);
   _scrAddrVec1.push_back(TestChain::scrAddrD);
   _scrAddrVec1.push_back(TestChain::scrAddrE);
   _scrAddrVec1.push_back(TestChain::scrAddrF);

   set<BinaryData> scrAddrSet;
   scrAddrSet.insert(_scrAddrVec1.begin(), _scrAddrVec1.end());

   {
      auto pCallback = make_shared<DBTestUtils::UTCallback>();
      auto&& bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::listenPort(), 
         Armory::Config::getDataDir(),
         authPeersPassLbd_, 
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());

      auto&& wallet1 = bdvObj->instantiateWallet("wallet1");
      vector<string> walletRegIDs;
      walletRegIDs.push_back(
         wallet1.registerAddresses(_scrAddrVec1, false));

      //wait on registration ack
      pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      //mine
      DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrB, 1000);
      pCallback->waitOnSignal(BDMAction_NewBlock);

      //get utxos
      auto utxo_prom = make_shared<promise<vector<UTXO>>>();
      auto utxo_fut = utxo_prom->get_future();
      auto utxo_get = [utxo_prom](ReturnMessage<vector<UTXO>> utxoV)->void
      {
         utxo_prom->set_value(move(utxoV.get()));
      };
      wallet1.getSpendableTxOutListForValue(UINT64_MAX, utxo_get);
      auto&& utxos = utxo_fut.get();

      DBTestUtils::ZcVector zcVec;

      //get utxo
      unsigned loopCount = 10;
      unsigned stagger = 0;
      for (auto& utxo : utxos)
      {
         if (utxo.getRecipientScrAddr() != TestChain::scrAddrB ||
            utxo.getScript().getSize() != 25 ||
            utxo.getValue() != 50 * COIN)
            continue;

         //sign
         {
            auto spenderA = make_shared<ScriptSpender>(utxo);
            Signer signer;
            signer.addSpender(spenderA);

            auto id = stagger % _scrAddrVec1.size();

            auto recipient = std::make_shared<Recipient_P2PKH>(
               _scrAddrVec1[id].getSliceCopy(1, 20), utxo.getValue());
            signer.addRecipient(recipient);

            signer.setFeed(feed);
            signer.sign();
            signer.serializeSignedTx();
            zcVec.push_back(signer.serializeSignedTx(), 130000000, stagger++);
         }

         if (stagger < loopCount)
            continue;

         break;
      }

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      pCallback->waitOnSignal(BDMAction_ZC);

      auto getAddrOp = [bdvObj, &scrAddrSet](
         unsigned heightOffset, unsigned zcOffset)->OutpointBatch
      {
         auto promPtr = make_shared<promise<OutpointBatch>>();
         auto fut = promPtr->get_future();
         auto addrOpLbd = [promPtr](ReturnMessage<OutpointBatch> batch)->void
         {
            promPtr->set_value(batch.get());
         };

         bdvObj->getOutpointsForAddresses(scrAddrSet, heightOffset, zcOffset, addrOpLbd);
         return fut.get();
      };

      auto computeBalance = [](const vector<OutpointData>& data)->uint64_t
      {
         uint64_t total = 0;
         for(auto& op : data)
         { 
            if (op.isSpent_)
               continue;

            total += op.value_;
         }

         return total;
      };

      //check current mined output state
      unsigned heightOffset = 0;
      auto&& addrOp = getAddrOp(heightOffset, UINT32_MAX);
      heightOffset = addrOp.heightCutoff_ + 1;
      ASSERT_EQ(addrOp.outpoints_.size(), 6ULL);
      
      auto iterAddrA = addrOp.outpoints_.find(TestChain::scrAddrA);
      EXPECT_NE(iterAddrA, addrOp.outpoints_.end());
      EXPECT_EQ(iterAddrA->second.size(), 1ULL);
      EXPECT_EQ(computeBalance(iterAddrA->second), 50 * COIN);

      auto iterAddrB = addrOp.outpoints_.find(TestChain::scrAddrB);
      EXPECT_NE(iterAddrB, addrOp.outpoints_.end());
      EXPECT_EQ(iterAddrB->second.size(), 1007ULL);
      EXPECT_EQ(computeBalance(iterAddrB->second), 50070 * COIN);

      auto iterAddrC = addrOp.outpoints_.find(TestChain::scrAddrC);
      EXPECT_NE(iterAddrC, addrOp.outpoints_.end());
      EXPECT_EQ(iterAddrC->second.size(), 4ULL);
      EXPECT_EQ(computeBalance(iterAddrC->second), 20 * COIN);

      auto iterAddrD = addrOp.outpoints_.find(TestChain::scrAddrD);
      EXPECT_NE(iterAddrD, addrOp.outpoints_.end());
      EXPECT_EQ(iterAddrD->second.size(), 4ULL);
      EXPECT_EQ(computeBalance(iterAddrD->second), 65 * COIN);

      auto iterAddrE = addrOp.outpoints_.find(TestChain::scrAddrE);
      EXPECT_NE(iterAddrE, addrOp.outpoints_.end());
      EXPECT_EQ(iterAddrE->second.size(), 2ULL);
      EXPECT_EQ(computeBalance(iterAddrE->second), 30 * COIN);

      auto iterAddrF = addrOp.outpoints_.find(TestChain::scrAddrF);
      EXPECT_NE(iterAddrF, addrOp.outpoints_.end());
      EXPECT_EQ(iterAddrF->second.size(), 4ULL);
      EXPECT_EQ(computeBalance(iterAddrF->second), 5 * COIN);

      //check zc outputs
      auto zcAddrOp = getAddrOp(UINT32_MAX, 0);
      ASSERT_EQ(zcAddrOp.outpoints_.size(), loopCount + 1);

      auto iterZcB = zcAddrOp.outpoints_.find(TestChain::scrAddrB);
      ASSERT_NE(iterZcB, zcAddrOp.outpoints_.end());
      EXPECT_EQ(iterZcB->second.size(), 10ULL);

      for (auto& opB : iterZcB->second)
      {
         EXPECT_EQ(opB.value_, 50 * COIN);
         EXPECT_EQ(opB.txIndex_, 0U);
         EXPECT_TRUE(opB.isSpent_);
      }

      for (unsigned z = 0; z < loopCount; z++)
      {
         auto id = z % _scrAddrVec1.size();
         auto& addr = _scrAddrVec1[id];
         
         auto addrIter = zcAddrOp.outpoints_.find(addr);
         ASSERT_NE(addrIter, zcAddrOp.outpoints_.end());
         EXPECT_EQ(addrIter->second.size(), 1ULL);

         auto& op = addrIter->second[0];
         EXPECT_EQ(op.value_, 50 * COIN);
         EXPECT_EQ(op.txHeight_, UINT32_MAX);
      }

      //mine the zc
      for (unsigned z = 0; z < loopCount; z++)
      {
         //mine
         DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
         pCallback->waitOnSignal(BDMAction_NewBlock);

         //grab addrop
         auto&& addr_op = getAddrOp(heightOffset, UINT32_MAX);
         EXPECT_EQ(addr_op.outpoints_.size(), 3ULL);

         //new coinbase to A
         auto iterA = addr_op.outpoints_.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, addr_op.outpoints_.end());
         EXPECT_EQ(iterA->second.size(), 1ULL);

         auto& opA = *iterA->second.begin();
         EXPECT_EQ(opA.txIndex_, 0U);
         EXPECT_EQ(opA.txOutIndex_, 0U);
         EXPECT_EQ(opA.value_, 50 * COIN);
         EXPECT_FALSE(opA.isSpent_);

         //B coinbase input
         auto iterB = addr_op.outpoints_.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, addr_op.outpoints_.end());
         EXPECT_EQ(iterB->second.size(), 1ULL);

         auto& opB = *iterB->second.begin();
         EXPECT_EQ(opB.txIndex_, 0U);
         EXPECT_EQ(opB.txOutIndex_, 0U);
         EXPECT_EQ(opB.value_, 50 * COIN);
         EXPECT_TRUE(opB.isSpent_);
         
         //to recipient
         auto id = z % _scrAddrVec1.size();
         auto& recAddr = _scrAddrVec1[id];
         auto iterR = addr_op.outpoints_.find(recAddr);
         ASSERT_NE(iterR, addr_op.outpoints_.end());
         EXPECT_EQ(iterR->second.size(), 1ULL);

         auto& opR = *iterR->second.begin();
         EXPECT_EQ(opR.txIndex_, 1U);
         EXPECT_EQ(opR.value_, 50 * COIN);

         //update cutoff
         heightOffset = addr_op.heightCutoff_ + 1;
      }
   }

   //cleanup
   auto&& bdvObj2 = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::listenPort(), Armory::Config::getDataDir(),
      authPeersPassLbd_, NetworkSettings::ephemeralPeers(), true, nullptr);
   bdvObj2->addPublicKey(serverPubkey);
   bdvObj2->connectToRemote();

   bdvObj2->shutdown(NetworkSettings::cookie());
   WebSocketServer::waitOnShutdown();

   EXPECT_EQ(theBDMt_->bdm()->zeroConfCont()->getMatcherMapSize(), 0U);

   delete theBDMt_;
   theBDMt_ = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WebSocketTests, WebSocketStack_CombinedCalls)
{
   //
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_, true);
   auto&& serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

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

   auto&& _scrAddrVec1 = createNAddresses(20);
   _scrAddrVec1.push_back(TestChain::scrAddrA);

   auto&& _scrAddrVec2 = createNAddresses(15);
   _scrAddrVec2.push_back(TestChain::scrAddrB);

   {
      auto pCallback = make_shared<DBTestUtils::UTCallback>();
      auto&& bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::listenPort(), 
         Armory::Config::getDataDir(),
         authPeersPassLbd_, 
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());

      auto&& wallet1 = bdvObj->instantiateWallet("wallet1");
      vector<string> walletRegIDs;
      walletRegIDs.push_back(
         wallet1.registerAddresses(_scrAddrVec1, false));

      auto&& wallet2 = bdvObj->instantiateWallet("wallet2");
      walletRegIDs.push_back(
         wallet2.registerAddresses(_scrAddrVec2, false));

      //wait on registration ack
      pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      //balances
      vector<string> walletIDs;
      walletIDs.push_back(wallet1.walletID());
      walletIDs.push_back(wallet2.walletID());

      auto promPtr = make_shared<promise<map<string, CombinedBalances>>>();
      auto fut = promPtr->get_future();
      auto balLbd = [promPtr](
         ReturnMessage<map<string, CombinedBalances>> combBal)->void
      {
         promPtr->set_value(combBal.get());
      };

      bdvObj->getCombinedBalances(walletIDs, balLbd);
      auto&& balMap = fut.get();
      ASSERT_EQ(balMap.size(), 2ULL);

      //wallet1
      auto iter1 = balMap.find(walletIDs[0]);
      ASSERT_NE(iter1, balMap.end());

      //sizes
      ASSERT_EQ(iter1->second.walletBalanceAndCount_.size(), 4ULL);
      ASSERT_EQ(iter1->second.addressBalances_.size(), 1ULL);

      //wallet balance
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[0], 50 * COIN);
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[1], 0ULL);
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[2], 50 * COIN);
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[3], 1ULL);

      //scrAddrA balance
      auto addrIter1 = iter1->second.addressBalances_.find(TestChain::scrAddrA);
      ASSERT_NE(addrIter1, iter1->second.addressBalances_.end());
      ASSERT_EQ(addrIter1->second.size(), 3ULL);
      EXPECT_EQ(addrIter1->second[0], 50 * COIN);
      EXPECT_EQ(addrIter1->second[1], 0U);
      EXPECT_EQ(addrIter1->second[2], 50 * COIN);

      //wallet2
      auto iter2 = balMap.find(walletIDs[1]);
      ASSERT_NE(iter2, balMap.end());

      //sizes
      ASSERT_EQ(iter2->second.walletBalanceAndCount_.size(), 4ULL);
      ASSERT_EQ(iter2->second.addressBalances_.size(), 1ULL);

      //wallet balance
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[0], 70 * COIN);
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[1], 20 * COIN);
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[2], 70 * COIN);
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[3], 12ULL);

      //scrAddrB balance
      auto addrIter2 = iter2->second.addressBalances_.find(TestChain::scrAddrB);
      ASSERT_NE(addrIter2, iter2->second.addressBalances_.end());
      ASSERT_EQ(addrIter2->second.size(), 3ULL);
      EXPECT_EQ(addrIter2->second[0], 70 * COIN);
      EXPECT_EQ(addrIter2->second[1], 20 * COIN);
      EXPECT_EQ(addrIter2->second[2], 70 * COIN);

      //addr txn counts
      auto promPtr2 = make_shared<promise<map<string, CombinedCounts>>>();
      auto fut2 = promPtr2->get_future();
      auto countLbd = [promPtr2](
         ReturnMessage<map<string, CombinedCounts>> combCount)->void
      {
         promPtr2->set_value(combCount.get());
      };

      bdvObj->getCombinedAddrTxnCounts(walletIDs, countLbd);
      auto&& countMap = fut2.get();
      ASSERT_EQ(countMap.size(), 2ULL);

      //wallet1
      auto iter3 = countMap.find(walletIDs[0]);
      ASSERT_NE(iter3, countMap.end());
      ASSERT_EQ(iter3->second.addressTxnCounts_.size(), 1ULL);

      auto addrIter3 = iter3->second.addressTxnCounts_.find(TestChain::scrAddrA);
      ASSERT_NE(addrIter3, iter3->second.addressTxnCounts_.end());
      EXPECT_EQ(addrIter3->second, 1U);

      //wallet2
      auto iter4 = countMap.find(walletIDs[1]);
      ASSERT_NE(iter4, countMap.end());
      ASSERT_EQ(iter4->second.addressTxnCounts_.size(), 1ULL);

      auto addrIter4 = iter4->second.addressTxnCounts_.find(TestChain::scrAddrB);
      ASSERT_NE(addrIter4, iter4->second.addressTxnCounts_.end());
      EXPECT_EQ(addrIter4->second, 12U);

      //utxos
      auto promPtr3 = make_shared<promise<vector<UTXO>>>();
      auto fut3 = promPtr3->get_future();
      auto utxoLbd = [promPtr3](ReturnMessage<vector<UTXO>> combUtxo)->void
      {
         promPtr3->set_value(combUtxo.get());
      };

      bdvObj->getCombinedSpendableTxOutListForValue(
         walletIDs, UINT64_MAX, utxoLbd);
      auto&& utxoVec = fut3.get();
      ASSERT_EQ(utxoVec.size(), 1ULL);

      auto& utxo1 = utxoVec[0];
      EXPECT_EQ(utxo1.getValue(), 20 * COIN);
      EXPECT_EQ(utxo1.getRecipientScrAddr(), TestChain::scrAddrB);

      //done
      bdvObj->unregisterFromDB();
   }

   //cleanup
   auto&& bdvObj2 = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::listenPort(), Armory::Config::getDataDir(),
      authPeersPassLbd_, NetworkSettings::ephemeralPeers(), true, nullptr);
   bdvObj2->addPublicKey(serverPubkey);
   bdvObj2->connectToRemote();

   bdvObj2->shutdown(NetworkSettings::cookie());
   WebSocketServer::waitOnShutdown();

   EXPECT_EQ(theBDMt_->bdm()->zeroConfCont()->getMatcherMapSize(), 0ULL);

   delete theBDMt_;
   theBDMt_ = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WebSocketTests, WebSocketStack_UnregisterAddresses)
{
   //
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_, true);
   auto&& serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

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

   auto&& _scrAddrVec1 = createNAddresses(20);
   _scrAddrVec1.push_back(TestChain::scrAddrA);

   auto&& _scrAddrVec2 = createNAddresses(15);
   _scrAddrVec2.push_back(TestChain::scrAddrB);

   {
      auto pCallback = make_shared<DBTestUtils::UTCallback>();
      auto&& bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::listenPort(), 
         Armory::Config::getDataDir(),
         authPeersPassLbd_, 
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());

      auto&& wallet1 = bdvObj->instantiateWallet("wallet1");
      vector<string> walletRegIDs;
      walletRegIDs.push_back(
         wallet1.registerAddresses(_scrAddrVec1, false));

      auto&& wallet2 = bdvObj->instantiateWallet("wallet2");
      walletRegIDs.push_back(
         wallet2.registerAddresses(_scrAddrVec2, false));

      //wait on registration ack
      pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      //balances
      vector<string> walletIDs;
      walletIDs.push_back(wallet1.walletID());
      walletIDs.push_back(wallet2.walletID());

      auto getCombinedBalances = [bdvObj](vector<string> walletIDs)->map<string, CombinedBalances>
      {
         auto promPtr = make_shared<promise<map<string, CombinedBalances>>>();
         auto fut = promPtr->get_future();
         auto balLbd = [promPtr](
            ReturnMessage<map<string, CombinedBalances>> combBal)->void
         {
            promPtr->set_value(combBal.get());
         };

         bdvObj->getCombinedBalances(walletIDs, balLbd);
         return fut.get();
      };

      auto&& balMap = getCombinedBalances(walletIDs);
      ASSERT_EQ(balMap.size(), 2ULL);

      //wallet1
      auto iter1 = balMap.find(walletIDs[0]);
      ASSERT_NE(iter1, balMap.end());

      //sizes
      ASSERT_EQ(iter1->second.walletBalanceAndCount_.size(), 4ULL);
      ASSERT_EQ(iter1->second.addressBalances_.size(), 1ULL);

      //wallet balance
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[0], 50 * COIN);
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[1], 0U);
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[2], 50 * COIN);
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[3], 1ULL);

      //scrAddrA balance
      auto addrIter1 = iter1->second.addressBalances_.find(TestChain::scrAddrA);
      ASSERT_NE(addrIter1, iter1->second.addressBalances_.end());
      ASSERT_EQ(addrIter1->second.size(), 3ULL);
      EXPECT_EQ(addrIter1->second[0], 50 * COIN);
      EXPECT_EQ(addrIter1->second[1], 0U);
      EXPECT_EQ(addrIter1->second[2], 50 * COIN);

      //wallet2
      auto iter2 = balMap.find(walletIDs[1]);
      ASSERT_NE(iter2, balMap.end());

      //sizes
      ASSERT_EQ(iter2->second.walletBalanceAndCount_.size(), 4ULL);
      ASSERT_EQ(iter2->second.addressBalances_.size(), 1ULL);

      //wallet balance
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[0], 70 * COIN);
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[1], 20 * COIN);
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[2], 70 * COIN);
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[3], 12ULL);

      //scrAddrB balance
      auto addrIter2 = iter2->second.addressBalances_.find(TestChain::scrAddrB);
      ASSERT_NE(addrIter2, iter2->second.addressBalances_.end());
      ASSERT_EQ(addrIter2->second.size(), 3ULL);
      EXPECT_EQ(addrIter2->second[0], 70 * COIN);
      EXPECT_EQ(addrIter2->second[1], 20 * COIN);
      EXPECT_EQ(addrIter2->second[2], 70 * COIN);

      //addr txn counts
      auto promPtr2 = make_shared<promise<map<string, CombinedCounts>>>();
      auto fut2 = promPtr2->get_future();
      auto countLbd = [promPtr2](
         ReturnMessage<map<string, CombinedCounts>> combCount)->void
      {
         promPtr2->set_value(combCount.get());
      };

      bdvObj->getCombinedAddrTxnCounts(walletIDs, countLbd);
      auto&& countMap = fut2.get();
      ASSERT_EQ(countMap.size(), 2ULL);

      //wallet1
      auto iter3 = countMap.find(walletIDs[0]);
      ASSERT_NE(iter3, countMap.end());
      ASSERT_EQ(iter3->second.addressTxnCounts_.size(), 1ULL);

      auto addrIter3 = iter3->second.addressTxnCounts_.find(TestChain::scrAddrA);
      ASSERT_NE(addrIter3, iter3->second.addressTxnCounts_.end());
      EXPECT_EQ(addrIter3->second, 1U);

      //wallet2
      auto iter4 = countMap.find(walletIDs[1]);
      ASSERT_NE(iter4, countMap.end());
      ASSERT_EQ(iter4->second.addressTxnCounts_.size(), 1ULL);

      auto addrIter4 = iter4->second.addressTxnCounts_.find(TestChain::scrAddrB);
      ASSERT_NE(addrIter4, iter4->second.addressTxnCounts_.end());
      EXPECT_EQ(addrIter4->second, 12U);

      //utxos
      auto promPtr3 = make_shared<promise<vector<UTXO>>>();
      auto fut3 = promPtr3->get_future();
      auto utxoLbd = [promPtr3](ReturnMessage<vector<UTXO>> combUtxo)->void
      {
         promPtr3->set_value(combUtxo.get());
      };

      bdvObj->getCombinedSpendableTxOutListForValue(
         walletIDs, UINT64_MAX, utxoLbd);
      auto&& utxoVec = fut3.get();
      ASSERT_EQ(utxoVec.size(), 1ULL);

      auto& utxo1 = utxoVec[0];
      EXPECT_EQ(utxo1.getValue(), 20 * COIN);
      EXPECT_EQ(utxo1.getRecipientScrAddr(), TestChain::scrAddrB);

      //mine a couple blocks on the new addresses
      DBTestUtils::mineNewBlock(theBDMt_, _scrAddrVec1[0].getSliceCopy(1, 20), 1);
      pCallback->waitOnSignal(BDMAction_NewBlock);
      
      DBTestUtils::mineNewBlock(theBDMt_, _scrAddrVec1[1].getSliceCopy(1, 20), 1);
      pCallback->waitOnSignal(BDMAction_NewBlock);
      
      DBTestUtils::mineNewBlock(theBDMt_, _scrAddrVec2[0].getSliceCopy(1, 20), 1);
      pCallback->waitOnSignal(BDMAction_NewBlock);
      
      DBTestUtils::mineNewBlock(theBDMt_, _scrAddrVec2[1].getSliceCopy(1, 20), 1);
      pCallback->waitOnSignal(BDMAction_NewBlock);

      //grab balances
      balMap = getCombinedBalances(walletIDs);
      ASSERT_EQ(balMap.size(), 2ULL);

      //wallet1
      iter1 = balMap.find(walletIDs[0]);
      ASSERT_NE(iter1, balMap.end());

      //sizes
      ASSERT_EQ(iter1->second.walletBalanceAndCount_.size(), 4ULL);
      ASSERT_EQ(iter1->second.addressBalances_.size(), 2ULL);

      //wallet balance
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[0], 150 * COIN);
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[1], 0ULL);
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[2], 150 * COIN);
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[3], 3ULL);

      //_scrAddrVec1[0] balance
      addrIter1 = iter1->second.addressBalances_.find(_scrAddrVec1[0]);
      ASSERT_NE(addrIter1, iter1->second.addressBalances_.end());
      ASSERT_EQ(addrIter1->second.size(), 3ULL);
      EXPECT_EQ(addrIter1->second[0], 50 * COIN);
      EXPECT_EQ(addrIter1->second[1], 0ULL);
      EXPECT_EQ(addrIter1->second[2], 50 * COIN);

      //_scrAddrVec1[1] balance
      addrIter1 = iter1->second.addressBalances_.find(_scrAddrVec1[1]);
      ASSERT_NE(addrIter1, iter1->second.addressBalances_.end());
      ASSERT_EQ(addrIter1->second.size(), 3ULL);
      EXPECT_EQ(addrIter1->second[0], 50 * COIN);
      EXPECT_EQ(addrIter1->second[1], 0ULL);
      EXPECT_EQ(addrIter1->second[2], 50 * COIN);

      //wallet2
      iter2 = balMap.find(walletIDs[1]);
      ASSERT_NE(iter2, balMap.end());

      //sizes
      ASSERT_EQ(iter2->second.walletBalanceAndCount_.size(), 4ULL);
      ASSERT_EQ(iter2->second.addressBalances_.size(), 2ULL);

      //wallet balance
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[0], 170 * COIN);
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[1], 20 * COIN);
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[2], 170 * COIN);
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[3], 14ULL);

      //_scrAddrVec2[0] balance
      addrIter2 = iter2->second.addressBalances_.find(_scrAddrVec2[0]);
      ASSERT_NE(addrIter2, iter2->second.addressBalances_.end());
      ASSERT_EQ(addrIter2->second.size(), 3ULL);
      EXPECT_EQ(addrIter2->second[0], 50 * COIN);
      EXPECT_EQ(addrIter2->second[1], 0ULL);
      EXPECT_EQ(addrIter2->second[2], 50 * COIN);

      //_scrAddrVec2[1] balance
      addrIter2 = iter2->second.addressBalances_.find(_scrAddrVec2[1]);
      ASSERT_NE(addrIter2, iter2->second.addressBalances_.end());
      ASSERT_EQ(addrIter2->second.size(), 3ULL);
      EXPECT_EQ(addrIter2->second[0], 50 * COIN);
      EXPECT_EQ(addrIter2->second[1], 0ULL);
      EXPECT_EQ(addrIter2->second[2], 50 * COIN);

      //unregister some addresses
      walletRegIDs.clear();
      walletRegIDs.push_back(
         wallet1.unregisterAddresses({ _scrAddrVec1[0], _scrAddrVec1[5]}));
      walletRegIDs.push_back(
         wallet2.unregisterAddresses({ _scrAddrVec2[1], _scrAddrVec2[6]}));

      pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

      //grab balances again
      balMap = getCombinedBalances(walletIDs);
      ASSERT_EQ(balMap.size(), 2ULL);

      //wallet1
      iter1 = balMap.find(walletIDs[0]);
      ASSERT_NE(iter1, balMap.end());

      //sizes
      ASSERT_EQ(iter1->second.walletBalanceAndCount_.size(), 4ULL);
      ASSERT_EQ(iter1->second.addressBalances_.size(), 0ULL);

      //wallet balance
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[0], 100 * COIN);
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[1], 0ULL);
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[2], 100 * COIN);
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[3], 2ULL);

      //wallet2
      iter2 = balMap.find(walletIDs[1]);
      ASSERT_NE(iter2, balMap.end());

      //sizes
      ASSERT_EQ(iter2->second.walletBalanceAndCount_.size(), 4ULL);
      ASSERT_EQ(iter2->second.addressBalances_.size(), 0ULL);

      //wallet balance
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[0], 120 * COIN);
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[1], 20 * COIN);
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[2], 120 * COIN);
      EXPECT_EQ(iter2->second.walletBalanceAndCount_[3], 13ULL);

      //unregister a wallet
      walletRegIDs.clear();
      walletRegIDs.push_back(wallet2.unregister());
      pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

      //grab balances again
      balMap = getCombinedBalances(walletIDs);
      ASSERT_EQ(balMap.size(), 0ULL); //should be 0, as one wallet in walletIDs is invalid

      //grab balances again
      balMap = getCombinedBalances({wallet1id});
      ASSERT_EQ(balMap.size(), 1ULL);

      //mine a block
      DBTestUtils::mineNewBlock(theBDMt_, _scrAddrVec1[2].getSliceCopy(1, 20), 1);
      pCallback->waitOnSignal(BDMAction_NewBlock);

      //grab balances again
      balMap = getCombinedBalances({wallet1id});
      ASSERT_EQ(balMap.size(), 1ULL);
      
      //wallet1
      iter1 = balMap.find(walletIDs[0]);
      ASSERT_NE(iter1, balMap.end());

      //sizes
      ASSERT_EQ(iter1->second.walletBalanceAndCount_.size(), 4ULL);
      ASSERT_EQ(iter1->second.addressBalances_.size(), 1ULL);

      //wallet balance
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[0], 150 * COIN);
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[1], 0ULL);
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[2], 150 * COIN);
      EXPECT_EQ(iter1->second.walletBalanceAndCount_[3], 3ULL);

      addrIter1 = iter1->second.addressBalances_.find(_scrAddrVec1[2]);
      ASSERT_NE(addrIter1, iter1->second.addressBalances_.end());
      ASSERT_EQ(addrIter1->second.size(), 3ULL);
      EXPECT_EQ(addrIter1->second[0], 50 * COIN);
      EXPECT_EQ(addrIter1->second[1], 0ULL);
      EXPECT_EQ(addrIter1->second[2], 50 * COIN);

      //done
      bdvObj->unregisterFromDB();
   }

   //cleanup
   auto&& bdvObj2 = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::listenPort(), Armory::Config::getDataDir(),
      authPeersPassLbd_, NetworkSettings::ephemeralPeers(), true, nullptr);
   bdvObj2->addPublicKey(serverPubkey);
   bdvObj2->connectToRemote();

   bdvObj2->shutdown(NetworkSettings::cookie());
   WebSocketServer::waitOnShutdown();

   EXPECT_EQ(theBDMt_->bdm()->zeroConfCont()->getMatcherMapSize(), 0ULL);

   delete theBDMt_;
   theBDMt_ = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WebSocketTests, WebSocketStack_DynamicReorg)
{
   //instantiate resolver feed overloaded object
   auto feed = make_shared<ResolverUtils::TestResolverFeed>();
   feed->addPrivKey(TestChain::privKeyAddrB);
   feed->addPrivKey(TestChain::privKeyAddrC);
   feed->addPrivKey(TestChain::privKeyAddrD);
   feed->addPrivKey(TestChain::privKeyAddrE);
   feed->addPrivKey(TestChain::privKeyAddrF);

   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_, true);
   auto&& serverPubkey = WebSocketServer::getPublicKey();

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   scrAddrVec.push_back(TestChain::scrAddrD);
   scrAddrVec.push_back(TestChain::scrAddrE);
   scrAddrVec.push_back(TestChain::scrAddrF);

   theBDMt_->start(DBSettings::initMode());

   auto pCallback = make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::listenPort(), 
      Armory::Config::getDataDir(),
      authPeersPassLbd_, 
      NetworkSettings::ephemeralPeers(), true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey);
   bdvObj->connectToRemote();
   bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());

   auto&& wallet1 = bdvObj->instantiateWallet("wallet1");
   vector<string> walletRegIDs;
   walletRegIDs.push_back(
      wallet1.registerAddresses(scrAddrVec, false));

   //wait on registration ack
   pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

   //go online
   bdvObj->goOnline();
   pCallback->waitOnSignal(BDMAction_Ready);

   //create tx from utxo lambda
   auto makeTxFromUtxo = [feed](const UTXO& utxo, const BinaryData& recipient)->BinaryData
   {
      auto spender = make_shared<ScriptSpender>(utxo);
      auto recPtr = make_shared<Recipient_P2PKH>(recipient.getSliceCopy(1, 20), utxo.getValue());

      Signer signer;
      signer.setFeed(feed);
      signer.addSpender(spender);
      signer.addRecipient(recPtr);

      signer.sign();
      return signer.serializeSignedTx();
   };

   //grab utxo from db
   auto getUtxo = [bdvObj](const BinaryData& addr)->vector<UTXO>
   {
      auto promPtr = make_shared<promise<vector<UTXO>>>();
      auto fut = promPtr->get_future();
      auto getUtxoLbd = [promPtr](ReturnMessage<vector<UTXO>> batch)->void
      {
         promPtr->set_value(batch.get());
      };

      bdvObj->getUTXOsForAddress(addr, false, getUtxoLbd);
      return fut.get();
   };

   //create tx from spender address lambda
   auto makeTx = [makeTxFromUtxo, getUtxo, bdvObj](
      const BinaryData& payer, const BinaryData& recipient)->BinaryData
   {
      auto utxoVec = getUtxo(payer);
      if (utxoVec.size() == 0)
         throw runtime_error("unexpected utxo vec size");

      auto& utxo = utxoVec[0];
      return makeTxFromUtxo(utxo, recipient);
   };

   //grab utxo from raw tx lambda
   auto getUtxoFromRawTx = [](BinaryData& rawTx, unsigned id)->UTXO
   {
      Tx tx(rawTx);
      if (id > tx.getNumTxOut())
         throw runtime_error("invalid txout count");

      auto&& txOut = tx.getTxOutCopy(id);
      
      UTXO utxo;
      utxo.unserializeRaw(txOut.serialize());
      utxo.txOutIndex_ = id;
      utxo.txHash_ = tx.getThisHash();

      return utxo;
   };

   vector<string> walletIDs;
   walletIDs.push_back(wallet1.walletID());

   //grab combined balances lambda
   auto getBalances = [bdvObj, walletIDs](void)->CombinedBalances
   {
      auto promPtr = make_shared<promise<map<string, CombinedBalances>>>();
      auto fut = promPtr->get_future();
      auto balLbd = [promPtr](
         ReturnMessage<map<string, CombinedBalances>> combBal)->void
      {
         promPtr->set_value(combBal.get());
      };

      bdvObj->getCombinedBalances(walletIDs, balLbd);
      auto&& balMap = fut.get();

      if (balMap.size() != 1)
         throw runtime_error("unexpected balance map size");

      return balMap.begin()->second;
   };

   //check original balances
   {
      auto&& combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances_.size(), 6ULL);

      auto iterA = combineBalances.addressBalances_.find(TestChain::scrAddrA);
      ASSERT_NE(iterA, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterA->second.size(), 3ULL);
      EXPECT_EQ(iterA->second[0], 50 * COIN);

      auto iterB = combineBalances.addressBalances_.find(TestChain::scrAddrB);
      ASSERT_NE(iterB, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterB->second.size(), 3ULL);
      EXPECT_EQ(iterB->second[0], 70 * COIN);

      auto iterC = combineBalances.addressBalances_.find(TestChain::scrAddrC);
      ASSERT_NE(iterC, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterC->second.size(), 3ULL);
      EXPECT_EQ(iterC->second[0], 20 * COIN);

      auto iterD = combineBalances.addressBalances_.find(TestChain::scrAddrD);
      ASSERT_NE(iterD, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterD->second.size(), 3ULL);
      EXPECT_EQ(iterD->second[0], 65 * COIN);

      auto iterE = combineBalances.addressBalances_.find(TestChain::scrAddrE);
      ASSERT_NE(iterE, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterE->second.size(), 3ULL);
      EXPECT_EQ(iterE->second[0], 30 * COIN);

      auto iterF = combineBalances.addressBalances_.find(TestChain::scrAddrF);
      ASSERT_NE(iterF, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterF->second.size(), 3ULL);
      EXPECT_EQ(iterF->second[0], 5 * COIN);
   }

   BinaryData branchPointBlockHash, mainBranchBlockHash;
   {
      auto top = theBDMt_->bdm()->blockchain()->top();
      branchPointBlockHash = top->getThisHash();
   }

   //main branch
   BinaryData bd_BtoC;
   UTXO utxoF;
   {
      //tx from B to C
      bd_BtoC = makeTx(TestChain::scrAddrB, TestChain::scrAddrC);

      //tx from F to A
      auto&& utxoVec = getUtxo(TestChain::scrAddrF);
      ASSERT_EQ(utxoVec.size(), 1ULL);
      utxoF = utxoVec[0];
      auto bd_FtoD = makeTxFromUtxo(utxoF, TestChain::scrAddrA);

      //broadcast
      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(bd_BtoC, 1300000000);
      zcVec.push_back(bd_FtoD, 1300000001);
      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      pCallback->waitOnSignal(BDMAction_ZC);

      //mine
      DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
      pCallback->waitOnSignal(BDMAction_NewBlock);

      //zc C to E
      auto&& utxo = getUtxoFromRawTx(bd_BtoC, 0);
      auto bd_CtoE = makeTxFromUtxo(utxo, TestChain::scrAddrE);
      
      //broadcast
      zcVec.zcVec_.clear();
      zcVec.push_back(bd_CtoE, 1300000002);
      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      pCallback->waitOnSignal(BDMAction_ZC);

      //mine
      DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
      pCallback->waitOnSignal(BDMAction_NewBlock);

      //check balances
      auto&& combineBalances = getBalances();

      /*
      D doesn't change so there should only be 5 balance entries
      C value does not change but the address sees a ZC in and a
      ZC out so the internal value change tracker counter was 
      incremented, resulting in an entry.
      */
      EXPECT_EQ(combineBalances.addressBalances_.size(), 5ULL);

      auto iterA = combineBalances.addressBalances_.find(TestChain::scrAddrA);
      ASSERT_NE(iterA, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterA->second.size(), 3ULL);
      EXPECT_EQ(iterA->second[0], 155 * COIN);

      auto iterB = combineBalances.addressBalances_.find(TestChain::scrAddrB);
      ASSERT_NE(iterB, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterB->second.size(), 3ULL);
      EXPECT_EQ(iterB->second[0], 20 * COIN);

      auto iterC = combineBalances.addressBalances_.find(TestChain::scrAddrC);
      ASSERT_NE(iterC, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterC->second.size(), 3ULL);
      EXPECT_EQ(iterC->second[0], 20 * COIN);

      auto iterE = combineBalances.addressBalances_.find(TestChain::scrAddrE);
      ASSERT_NE(iterE, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterE->second.size(), 3ULL);
      EXPECT_EQ(iterE->second[0], 80 * COIN);

      auto iterF = combineBalances.addressBalances_.find(TestChain::scrAddrF);
      ASSERT_NE(iterF, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterF->second.size(), 3ULL);
      EXPECT_EQ(iterF->second[0], 0 * COIN);

      {
         auto top = theBDMt_->bdm()->blockchain()->top();
         mainBranchBlockHash = top->getThisHash();
      }
   }

   //reorg
   {
      //set branching point
      DBTestUtils::setReorgBranchingPoint(theBDMt_, branchPointBlockHash);

      //tx from F to D
      auto bd_FtoD = makeTxFromUtxo(utxoF, TestChain::scrAddrD);

      //broadcast
      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(bd_BtoC, 1300000000, 2); //repeat B to C
      zcVec.push_back(bd_FtoD, 1300000001, 2);

      //zc D to E
      auto&& utxo = getUtxoFromRawTx(bd_FtoD, 0);
      auto bd_DtoE = makeTxFromUtxo(utxo, TestChain::scrAddrE);

      //broadcast
      zcVec.push_back(bd_DtoE, 1300000002, 3);

      /*
      Pass true to stage the zc. Cant broadcast that stuff until 
      the fork is live.
      */
      DBTestUtils::pushNewZc(theBDMt_, zcVec, true);

      //mine 3 blocks to outpace original chain
      DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrB, 3);
      EXPECT_EQ(pCallback->waitOnReorg(), 5U);

      //wait on ZC now, as the staged transactions have been pushed
      pCallback->waitOnSignal(BDMAction_ZC);

      //check balances
      auto&& combineBalances = getBalances();

      /*
      This triggers a reorg.
      F does not receive any effective change in balance from the
      previous top.

      D value does not change but this is due to a ZC spending the coins
      out, so the internal id is updated.
      */
      EXPECT_EQ(combineBalances.addressBalances_.size(), 5ULL);

      auto iterA = combineBalances.addressBalances_.find(TestChain::scrAddrA);
      ASSERT_NE(iterA, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterA->second.size(), 3ULL);
      EXPECT_EQ(iterA->second[0], 50 * COIN);

      auto iterB = combineBalances.addressBalances_.find(TestChain::scrAddrB);
      ASSERT_NE(iterB, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterB->second.size(), 3ULL);
      EXPECT_EQ(iterB->second[0], 170 * COIN);

      auto iterC = combineBalances.addressBalances_.find(TestChain::scrAddrC);
      ASSERT_NE(iterC, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterC->second.size(), 3ULL);
      EXPECT_EQ(iterC->second[0], 70 * COIN);

      auto iterD = combineBalances.addressBalances_.find(TestChain::scrAddrD);
      ASSERT_NE(iterD, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterD->second.size(), 3ULL);
      EXPECT_EQ(iterD->second[0], 65 * COIN);

      auto iterE = combineBalances.addressBalances_.find(TestChain::scrAddrE);
      ASSERT_NE(iterE, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterE->second.size(), 3ULL);
      EXPECT_EQ(iterE->second[0], 35 * COIN);
   }

   //back to main chain
   {
      //set branching point
      DBTestUtils::setReorgBranchingPoint(theBDMt_, mainBranchBlockHash);

      //mine 2 blocks to outpace forked chain
      DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrF, 2);
      EXPECT_EQ(pCallback->waitOnReorg(), 5U);

      //check balances
      auto&& combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances_.size(), 6ULL);

      auto iterA = combineBalances.addressBalances_.find(TestChain::scrAddrA);
      ASSERT_NE(iterA, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterA->second.size(), 3ULL);
      EXPECT_EQ(iterA->second[0], 155 * COIN);

      auto iterB = combineBalances.addressBalances_.find(TestChain::scrAddrB);
      ASSERT_NE(iterB, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterB->second.size(), 3ULL);
      EXPECT_EQ(iterB->second[0], 20 * COIN);

      auto iterC = combineBalances.addressBalances_.find(TestChain::scrAddrC);
      ASSERT_NE(iterC, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterC->second.size(), 3ULL);
      EXPECT_EQ(iterC->second[0], 20 * COIN);

      auto iterD = combineBalances.addressBalances_.find(TestChain::scrAddrD);
      ASSERT_NE(iterD, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterD->second.size(), 3ULL);
      EXPECT_EQ(iterD->second[0], 65 * COIN);

      auto iterE = combineBalances.addressBalances_.find(TestChain::scrAddrE);
      ASSERT_NE(iterE, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterE->second.size(), 3ULL);
      EXPECT_EQ(iterE->second[0], 80 * COIN);

      auto iterF = combineBalances.addressBalances_.find(TestChain::scrAddrF);
      ASSERT_NE(iterF, combineBalances.addressBalances_.end());
      ASSERT_EQ(iterF->second.size(), 3ULL);
      EXPECT_EQ(iterF->second[0], 100 * COIN);
   }

   //disconnect
   bdvObj->unregisterFromDB();

   //cleanup
   auto&& bdvObj2 = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::listenPort(), Armory::Config::getDataDir(),
      authPeersPassLbd_, NetworkSettings::ephemeralPeers(), true, nullptr);
   bdvObj2->addPublicKey(serverPubkey);
   bdvObj2->connectToRemote();

   bdvObj2->shutdown(NetworkSettings::cookie());
   WebSocketServer::waitOnShutdown();

   EXPECT_EQ(theBDMt_->bdm()->zeroConfCont()->getMatcherMapSize(), 0U);

   delete theBDMt_;
   theBDMt_ = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WebSocketTests, WebSocketStack_GetTxByHash)
{
   //instantiate resolver feed overloaded object
   auto feed = make_shared<ResolverUtils::TestResolverFeed>();
   feed->addPrivKey(TestChain::privKeyAddrB);
   feed->addPrivKey(TestChain::privKeyAddrC);
   feed->addPrivKey(TestChain::privKeyAddrD);
   feed->addPrivKey(TestChain::privKeyAddrE);
   feed->addPrivKey(TestChain::privKeyAddrF);

   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_, true);
   auto&& serverPubkey = WebSocketServer::getPublicKey();

   vector<BinaryData> scrAddrVec;
   scrAddrVec.push_back(TestChain::scrAddrA);
   scrAddrVec.push_back(TestChain::scrAddrB);
   scrAddrVec.push_back(TestChain::scrAddrC);
   scrAddrVec.push_back(TestChain::scrAddrD);
   scrAddrVec.push_back(TestChain::scrAddrE);
   scrAddrVec.push_back(TestChain::scrAddrF);

   theBDMt_->start(DBSettings::initMode());

   auto pCallback = make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::listenPort(), 
      Armory::Config::getDataDir(),
      authPeersPassLbd_, 
      NetworkSettings::ephemeralPeers(), true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey);
   bdvObj->connectToRemote();
   bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());

   auto&& wallet1 = bdvObj->instantiateWallet("wallet1");
   vector<string> walletRegIDs;
   walletRegIDs.push_back(
      wallet1.registerAddresses(scrAddrVec, false));

   //wait on registration ack
   pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

   //go online
   bdvObj->goOnline();
   pCallback->waitOnSignal(BDMAction_Ready);

   //grab mined tx
   auto&& ZC1 = TestUtils::getTx(5, 2); //block 5, tx 2
   auto&& hash1 = BtcUtils::getHash256(ZC1);
   
   auto getTxLbd = [bdvObj](const BinaryData& hash)->ReturnMessage<AsyncClient::TxResult>
   {
      auto promPtr = make_shared<promise<ReturnMessage<AsyncClient::TxResult>>>();
      auto fut = promPtr->get_future();
      auto lbd = [promPtr](ReturnMessage<AsyncClient::TxResult> txObj)
      {
         promPtr->set_value(txObj);
      };

      bdvObj->getTxByHash(hash, lbd);
      return fut.get();
   };

   //fetch mined tx
   auto&& txObj1 = getTxLbd(hash1);

   //fetch invalid tx
   auto&& txObj2 = getTxLbd(READHEX("000102030405060708090A0B0C0D0E0F000102030405060708090A0B0C0D0E0F"));

   try
   {
      auto&& tx = txObj1.get();
      auto hash = BtcUtils::getHash256(tx->serialize());
      EXPECT_EQ(hash, hash1);
   }
   catch (exception&)
   {
      ASSERT_FALSE(true);
   }  

   try
   {
      auto&& tx = txObj2.get();
      auto hash = BtcUtils::getHash256(tx->serialize());
      ASSERT_FALSE(true);
   }
   catch (ClientMessageError& e)
   {
      EXPECT_EQ(string(e.what()), 
         string("Error processing command: 80\n   errMsg: \"failed to grab tx by hash\""));
   }
   catch (...)
   {
      ASSERT_FALSE(true);
   }

   //test cache hit
   auto&& txObj3 = getTxLbd(hash1);
   try
   {
      auto&& tx = txObj3.get();
      auto hash = BtcUtils::getHash256(tx->serialize());
      EXPECT_EQ(hash, hash1);
   }
   catch (exception&)
   {
      ASSERT_FALSE(true);
   }  

   //grab a couple utxos
   auto promUtxo = make_shared<promise<vector<UTXO>>>();
   auto futUtxo = promUtxo->get_future();
   auto lbdUtxo = [promUtxo](ReturnMessage<vector<UTXO>> msg)->void
   {
      promUtxo->set_value(msg.get());
   };

   wallet1.getSpendableTxOutListForValue(UINT64_MAX, lbdUtxo);
   auto&& utxoVec = futUtxo.get();

   //create 2 zc
   BinaryData rawTx1;
   {
      //5 from E, 3 to A, change to C
      Signer signer;

      auto spender = make_shared<ScriptSpender>(utxoVec[0]);
      signer.addSpender(spender);

      auto recA = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrA.getSliceCopy(1, 20), 3 * COIN);
      signer.addRecipient(recA);

      auto recChange = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrC.getSliceCopy(1, 20), 
         spender->getValue() - recA->getValue());
      signer.addRecipient(recChange);

      signer.setFeed(feed);
      signer.sign();
      rawTx1 = signer.serializeSignedTx();
   }
   
   BinaryData rawTx2;
   {
      //20 from B, 5 to C, change to E
      Signer signer;

      auto spender = make_shared<ScriptSpender>(utxoVec.back());
      signer.addSpender(spender);

      auto recC = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrC.getSliceCopy(1, 20), 5 * COIN);
      signer.addRecipient(recC);

      auto recChange = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrE.getSliceCopy(1, 20), 
         spender->getValue() - recC->getValue());
      signer.addRecipient(recChange);

      signer.setFeed(feed);
      signer.sign();
      rawTx2 = signer.serializeSignedTx();
   }

   //push the 2 zc through the node
   DBTestUtils::ZcVector zcVec;
   zcVec.push_back(rawTx1, 1000000000);
   zcVec.push_back(rawTx2, 1000000001);
   DBTestUtils::pushNewZc(theBDMt_, zcVec);

   //wait on them
   Tx tx1(rawTx1);
   Tx tx2(rawTx2);
   set<BinaryData> zcHashes = {tx1.getThisHash(), tx2.getThisHash()};

   set<BinaryData> zcAddresses;
   zcAddresses.insert(TestChain::scrAddrA);
   zcAddresses.insert(TestChain::scrAddrB);
   zcAddresses.insert(TestChain::scrAddrC);
   zcAddresses.insert(TestChain::scrAddrE);

   pCallback->waitOnZc(zcHashes, zcAddresses, "");

   //grab them
   auto&& txObj4 = getTxLbd(tx1.getThisHash()).get();
   auto&& txObj5 = getTxLbd(tx2.getThisHash()).get();
 
   ASSERT_NE(txObj4, nullptr);
   ASSERT_NE(txObj5, nullptr);

   EXPECT_EQ(txObj4->getThisHash(), tx1.getThisHash());
   EXPECT_EQ(txObj4->getTxHeight(), UINT32_MAX);
   EXPECT_EQ(txObj4->getTxIndex(), 0U);

   EXPECT_EQ(txObj5->getThisHash(), tx2.getThisHash());
   EXPECT_EQ(txObj5->getTxHeight(), UINT32_MAX);
   EXPECT_EQ(txObj5->getTxIndex(), 1U);

   //create 2 more zc
   BinaryData rawTx3;
   {
      //25 from E, 5 to A, change to C
      Signer signer;

      auto spender = make_shared<ScriptSpender>(utxoVec[1]);
      signer.addSpender(spender);

      auto recA = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
      signer.addRecipient(recA);

      auto recChange = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrC.getSliceCopy(1, 20), 
         spender->getValue() - recA->getValue());
      signer.addRecipient(recChange);

      signer.setFeed(feed);
      signer.sign();
      rawTx3 = signer.serializeSignedTx();
   }
   
   BinaryData rawTx4;
   {
      //5 from D, 4 to C, change to E
      Signer signer;

      auto spender = make_shared<ScriptSpender>(utxoVec[2]);
      signer.addSpender(spender);

      auto recC = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrC.getSliceCopy(1, 20), 4 * COIN);
      signer.addRecipient(recC);

      auto recChange = make_shared<Recipient_P2PKH>(
         TestChain::scrAddrE.getSliceCopy(1, 20), 
         spender->getValue() - recC->getValue());
      signer.addRecipient(recChange);

      signer.setFeed(feed);
      signer.sign();
      rawTx4 = signer.serializeSignedTx();
   }

   //push the 2 zc through the rpc
   auto broadcastId1 = bdvObj->broadcastThroughRPC(rawTx3);
   auto broadcastId2 = bdvObj->broadcastThroughRPC(rawTx4);

   //wait on them
   Tx tx3(rawTx3);
   Tx tx4(rawTx4);
   zcHashes.clear();

   set<BinaryData> zcAddresses1;
   zcAddresses1.insert(TestChain::scrAddrA);
   zcAddresses1.insert(TestChain::scrAddrC);
   zcAddresses1.insert(TestChain::scrAddrE);

   set<BinaryData> zcAddresses2;
   zcAddresses2.insert(TestChain::scrAddrD);
   zcAddresses2.insert(TestChain::scrAddrC);
   zcAddresses2.insert(TestChain::scrAddrE);

   pCallback->waitOnZc({tx3.getThisHash()}, zcAddresses1, broadcastId1);
   pCallback->waitOnZc({tx4.getThisHash()}, zcAddresses2, broadcastId2);

   //grab them
   auto&& txObj6 = getTxLbd(tx3.getThisHash()).get();
   auto&& txObj7 = getTxLbd(tx4.getThisHash()).get();
 
   ASSERT_NE(txObj6, nullptr);
   ASSERT_NE(txObj7, nullptr);

   EXPECT_EQ(txObj6->getThisHash(), tx3.getThisHash());
   EXPECT_EQ(txObj6->getTxHeight(), UINT32_MAX);
   EXPECT_EQ(txObj6->getTxIndex(), 2U);

   EXPECT_EQ(txObj7->getThisHash(), tx4.getThisHash());
   EXPECT_EQ(txObj7->getTxHeight(), UINT32_MAX);
   EXPECT_EQ(txObj7->getTxIndex(), 3U);

   {
      //try to grab from another bdvobj
      auto pCallback2 = make_shared<DBTestUtils::UTCallback>();
      auto bdvObj2 = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::listenPort(), 
         Armory::Config::getDataDir(),
         authPeersPassLbd_, 
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback2);
      bdvObj2->addPublicKey(serverPubkey);
      bdvObj2->connectToRemote();
      bdvObj2->registerWithDB(BitcoinSettings::getMagicBytes());

      bdvObj2->goOnline();
      pCallback2->waitOnSignal(BDMAction_Ready);

      //
      auto getTxLbd2 = [bdvObj2](const BinaryData& hash)->ReturnMessage<AsyncClient::TxResult>
      {
         auto promPtr = make_shared<promise<ReturnMessage<AsyncClient::TxResult>>>();
         auto fut = promPtr->get_future();
         auto lbd = [promPtr](ReturnMessage<AsyncClient::TxResult> txObj)
         {
            promPtr->set_value(txObj);
         };

         bdvObj2->getTxByHash(hash, lbd);
         return fut.get();
      };

      //grab the zc
      auto&& txObj8 = getTxLbd2(tx1.getThisHash()).get();
      auto&& txObj10 = getTxLbd2(tx3.getThisHash()).get();
   
      ASSERT_NE(txObj8, nullptr);
      ASSERT_NE(txObj10, nullptr);

      EXPECT_EQ(txObj8->getThisHash(), tx1.getThisHash());
      EXPECT_EQ(txObj8->getTxHeight(), UINT32_MAX);
      EXPECT_EQ(txObj8->getTxIndex(), 0U);

      EXPECT_EQ(txObj10->getThisHash(), tx3.getThisHash());
      EXPECT_EQ(txObj10->getTxHeight(), UINT32_MAX);
      EXPECT_EQ(txObj10->getTxIndex(), 2U);

      //batch fetch partial cache hit
      auto getTxLbd3 = [bdvObj2](const set<BinaryData>& hashes)->
         ReturnMessage<AsyncClient::TxBatchResult>
      {
         auto promPtr = make_shared<promise<ReturnMessage<AsyncClient::TxBatchResult>>>();
         auto fut = promPtr->get_future();
         auto lbd = [promPtr](ReturnMessage<AsyncClient::TxBatchResult> txObj)
         {
            promPtr->set_value(txObj);
         };

         bdvObj2->getTxBatchByHash(hashes, lbd);
         return fut.get();
      };

      zcHashes.clear();
      zcHashes = 
      {
         tx1.getThisHash(), 
         tx2.getThisHash(), 
         tx3.getThisHash(), 
         tx4.getThisHash()
      };

      auto&& txMap = getTxLbd3(zcHashes).get();
      ASSERT_EQ(txMap.size(), 4ULL);

      auto iter = txMap.find(tx1.getThisHash());
      ASSERT_NE(iter, txMap.end());
      EXPECT_EQ(iter->second->getThisHash(), tx1.getThisHash());
      EXPECT_EQ(iter->second->getTxHeight(), UINT32_MAX);
      EXPECT_EQ(iter->second->getTxIndex(), 0U);

      iter = txMap.find(tx2.getThisHash());
      ASSERT_NE(iter, txMap.end());
      EXPECT_EQ(iter->second->getThisHash(), tx2.getThisHash());
      EXPECT_EQ(iter->second->getTxHeight(), UINT32_MAX);
      EXPECT_EQ(iter->second->getTxIndex(), 1U);

      iter = txMap.find(tx3.getThisHash());
      ASSERT_NE(iter, txMap.end());
      EXPECT_EQ(iter->second->getThisHash(), tx3.getThisHash());
      EXPECT_EQ(iter->second->getTxHeight(), UINT32_MAX);
      EXPECT_EQ(iter->second->getTxIndex(), 2U);

      iter = txMap.find(tx4.getThisHash());
      ASSERT_NE(iter, txMap.end());
      EXPECT_EQ(iter->second->getThisHash(), tx4.getThisHash());
      EXPECT_EQ(iter->second->getTxHeight(), UINT32_MAX);
      EXPECT_EQ(iter->second->getTxIndex(), 3U);

      //batch fetch full cache hit
      txMap = getTxLbd3(zcHashes).get();
      ASSERT_EQ(txMap.size(), 4ULL);

      iter = txMap.find(tx1.getThisHash());
      ASSERT_NE(iter, txMap.end());
      EXPECT_EQ(iter->second->getThisHash(), tx1.getThisHash());
      EXPECT_EQ(iter->second->getTxHeight(), UINT32_MAX);
      EXPECT_EQ(iter->second->getTxIndex(), 0U);

      iter = txMap.find(tx2.getThisHash());
      ASSERT_NE(iter, txMap.end());
      EXPECT_EQ(iter->second->getThisHash(), tx2.getThisHash());
      EXPECT_EQ(iter->second->getTxHeight(), UINT32_MAX);
      EXPECT_EQ(iter->second->getTxIndex(), 1U);

      iter = txMap.find(tx3.getThisHash());
      ASSERT_NE(iter, txMap.end());
      EXPECT_EQ(iter->second->getThisHash(), tx3.getThisHash());
      EXPECT_EQ(iter->second->getTxHeight(), UINT32_MAX);
      EXPECT_EQ(iter->second->getTxIndex(), 2U);

      iter = txMap.find(tx4.getThisHash());
      ASSERT_NE(iter, txMap.end());
      EXPECT_EQ(iter->second->getThisHash(), tx4.getThisHash());
      EXPECT_EQ(iter->second->getTxHeight(), UINT32_MAX);
      EXPECT_EQ(iter->second->getTxIndex(), 3U);

      //batch fetch an empty hash
      {
         auto getTxByHash = [bdvObj2](const BinaryData& hash)->AsyncClient::TxResult
         {
            auto promPtr = make_shared<promise<AsyncClient::TxResult>>();
            auto fut = promPtr->get_future();
            auto lbd = [promPtr](ReturnMessage<AsyncClient::TxResult> txObj)
            {
               try
               {
                  promPtr->set_value(txObj.get());
               }
               catch (const exception& e)
               {
                  promPtr->set_exception(make_exception_ptr(e));
               }
            };

            bdvObj2->getTxByHash(hash, lbd);
            return fut.get();
         };

         auto getTxBatch = [bdvObj2](const set<BinaryData>& hashes)->AsyncClient::TxBatchResult
         {
            auto promPtr = make_shared<promise<ReturnMessage<AsyncClient::TxBatchResult>>>();
            auto fut = promPtr->get_future();
            auto lbd = [promPtr](ReturnMessage<AsyncClient::TxBatchResult> txObj)
            {
               promPtr->set_value(txObj);
            };

            bdvObj2->getTxBatchByHash(hashes, lbd);
            auto msg = fut.get();
            return msg.get();
         };

         set<BinaryData> hashesEmpty;
         hashesEmpty.insert(BtcUtils::EmptyHash());

         try
         {
            auto&& txResult = getTxByHash(BtcUtils::EmptyHash());
            ASSERT_TRUE(false);
         }
         catch (const exception& e)
         {}

         auto&& txBatch = getTxBatch(hashesEmpty);
         auto&& txBatch2 = getTxBatch(hashesEmpty);
      }
      
      //disconnect
      bdvObj2->unregisterFromDB();
   }

   //disconnect
   bdvObj->unregisterFromDB();

   //cleanup
   auto&& bdvObj2 = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::listenPort(), Armory::Config::getDataDir(),
      authPeersPassLbd_, NetworkSettings::ephemeralPeers(), true, nullptr);
   bdvObj2->addPublicKey(serverPubkey);
   bdvObj2->connectToRemote();

   bdvObj2->shutdown(NetworkSettings::cookie());
   WebSocketServer::waitOnShutdown();

   EXPECT_EQ(theBDMt_->bdm()->zeroConfCont()->getMatcherMapSize(), 0U);

   delete theBDMt_;
   theBDMt_ = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WebSocketTests, WebSocketStack_GetSpentness)
{
   auto feed = make_shared<ResolverUtils::TestResolverFeed>();
   feed->addPrivKey(TestChain::privKeyAddrB);

   //
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_, true);
   auto&& serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

   struct KeyPair
   {
      SecureBinaryData priv_;
      SecureBinaryData pub_;
      BinaryData scrHash_;
   };

   vector<KeyPair> keyPairs;
   auto createNAddresses = [&keyPairs](unsigned count)->vector<BinaryData>
   {
      vector<BinaryData> result;
      for (unsigned i = 0; i < count; i++)
      {
         KeyPair kp;
         kp.priv_ = CryptoPRNG::generateRandom(32);
         kp.pub_ = CryptoECDSA().ComputePublicKey(kp.priv_, true);
         kp.scrHash_ = BtcUtils::getHash160(kp.pub_);

         BinaryWriter bw;
         bw.put_uint8_t(SCRIPT_PREFIX_HASH160);
         bw.put_BinaryData(kp.scrHash_);

         result.push_back(bw.getData());
         keyPairs.emplace_back(move(kp));
      }

      return result;
   };

   auto&& _scrAddrVec1 = createNAddresses(20);
   _scrAddrVec1.push_back(TestChain::scrAddrA);
   _scrAddrVec1.push_back(TestChain::scrAddrB);
   _scrAddrVec1.push_back(TestChain::scrAddrC);
   _scrAddrVec1.push_back(TestChain::scrAddrD);
   _scrAddrVec1.push_back(TestChain::scrAddrE);
   _scrAddrVec1.push_back(TestChain::scrAddrF);

   set<BinaryData> scrAddrSet;
   scrAddrSet.insert(_scrAddrVec1.begin(), _scrAddrVec1.end());

   {
      auto pCallback = make_shared<DBTestUtils::UTCallback>();
      auto&& bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::listenPort(), 
         Armory::Config::getDataDir(),
         authPeersPassLbd_, 
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(BitcoinSettings::getMagicBytes());

      auto&& wallet1 = bdvObj->instantiateWallet("wallet1");
      vector<string> walletRegIDs;
      walletRegIDs.push_back(
         wallet1.registerAddresses(_scrAddrVec1, false));

      //wait on registration ack
      pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      //mine
      DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrB, 1000);
      pCallback->waitOnSignal(BDMAction_NewBlock);

      //get utxos
      auto utxo_prom = make_shared<promise<vector<UTXO>>>();
      auto utxo_fut = utxo_prom->get_future();
      auto utxo_get = [utxo_prom](ReturnMessage<vector<UTXO>> utxoV)->void
      {
         utxo_prom->set_value(move(utxoV.get()));
      };
      wallet1.getSpendableTxOutListForValue(UINT64_MAX, utxo_get);
      auto&& utxos = utxo_fut.get();

      DBTestUtils::ZcVector zcVec;

      //spend some
      unsigned loopCount = 10;
      unsigned stagger = 0;
      for (auto& utxo : utxos)
      {
         if (utxo.getRecipientScrAddr() != TestChain::scrAddrB ||
            utxo.getScript().getSize() != 25 ||
            utxo.getValue() != 50 * COIN)
            continue;

         //sign
         {
            auto spenderA = make_shared<ScriptSpender>(utxo);
            Signer signer;
            signer.addSpender(spenderA);

            auto id = stagger % _scrAddrVec1.size();

            auto recipient = std::make_shared<Recipient_P2PKH>(
               _scrAddrVec1[id].getSliceCopy(1, 20), utxo.getValue());
            signer.addRecipient(recipient);

            signer.setFeed(feed);
            signer.sign();
            zcVec.push_back(signer.serializeSignedTx(), 130000000, stagger++);
         }

         if (stagger < loopCount)
            continue;

         break;
      }

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      pCallback->waitOnSignal(BDMAction_ZC);

      auto getAddrOp = [bdvObj, &scrAddrSet](
         unsigned heightOffset, unsigned zcOffset)->OutpointBatch
      {
         auto promPtr = make_shared<promise<OutpointBatch>>();
         auto fut = promPtr->get_future();
         auto addrOpLbd = [promPtr](ReturnMessage<OutpointBatch> batch)->void
         {
            promPtr->set_value(batch.get());
         };

         bdvObj->getOutpointsForAddresses(scrAddrSet, heightOffset, zcOffset, addrOpLbd);
         return fut.get();
      };

      auto computeBalance = [](const vector<OutpointData>& data)->uint64_t
      {
         uint64_t total = 0;
         for (auto& op : data)
         {
            if (op.isSpent_)
               continue;

            total += op.value_;
         }

         return total;
      };

      //check current mined output state
      unsigned heightOffset = 0;
      auto&& addrOp = getAddrOp(heightOffset, UINT32_MAX);
      heightOffset = addrOp.heightCutoff_ + 1;
      ASSERT_EQ(addrOp.outpoints_.size(), 6ULL);

      auto iterAddrA = addrOp.outpoints_.find(TestChain::scrAddrA);
      EXPECT_NE(iterAddrA, addrOp.outpoints_.end());
      EXPECT_EQ(iterAddrA->second.size(), 1ULL);
      EXPECT_EQ(computeBalance(iterAddrA->second), 50 * COIN);

      auto iterAddrB = addrOp.outpoints_.find(TestChain::scrAddrB);
      EXPECT_NE(iterAddrB, addrOp.outpoints_.end());
      EXPECT_EQ(iterAddrB->second.size(), 1007ULL);
      EXPECT_EQ(computeBalance(iterAddrB->second), 50070 * COIN);

      auto iterAddrC = addrOp.outpoints_.find(TestChain::scrAddrC);
      EXPECT_NE(iterAddrC, addrOp.outpoints_.end());
      EXPECT_EQ(iterAddrC->second.size(), 4ULL);
      EXPECT_EQ(computeBalance(iterAddrC->second), 20 * COIN);

      auto iterAddrD = addrOp.outpoints_.find(TestChain::scrAddrD);
      EXPECT_NE(iterAddrD, addrOp.outpoints_.end());
      EXPECT_EQ(iterAddrD->second.size(), 4ULL);
      EXPECT_EQ(computeBalance(iterAddrD->second), 65 * COIN);

      auto iterAddrE = addrOp.outpoints_.find(TestChain::scrAddrE);
      EXPECT_NE(iterAddrE, addrOp.outpoints_.end());
      EXPECT_EQ(iterAddrE->second.size(), 2ULL);
      EXPECT_EQ(computeBalance(iterAddrE->second), 30 * COIN);

      auto iterAddrF = addrOp.outpoints_.find(TestChain::scrAddrF);
      EXPECT_NE(iterAddrF, addrOp.outpoints_.end());
      EXPECT_EQ(iterAddrF->second.size(), 4ULL);
      EXPECT_EQ(computeBalance(iterAddrF->second), 5 * COIN);

      //check zc outputs
      auto zcAddrOp = getAddrOp(UINT32_MAX, 0U);
      ASSERT_EQ(zcAddrOp.outpoints_.size(), loopCount + 1ULL);

      auto iterZcB = zcAddrOp.outpoints_.find(TestChain::scrAddrB);
      ASSERT_NE(iterZcB, zcAddrOp.outpoints_.end());
      EXPECT_EQ(iterZcB->second.size(), 10ULL);

      map<BinaryData, set<unsigned>> zcSpentnessToGet;
      for (unsigned z = 0; z < loopCount; z++)
      {
         auto id = z % _scrAddrVec1.size();
         auto& addr = _scrAddrVec1[id];

         auto addrIter = zcAddrOp.outpoints_.find(addr);
         ASSERT_NE(addrIter, zcAddrOp.outpoints_.end());
         EXPECT_EQ(addrIter->second.size(), 1ULL);

         auto& op = addrIter->second[0];
         EXPECT_EQ(op.value_, 50 * COIN);
         EXPECT_EQ(op.txHeight_, UINT32_MAX);

         auto sptIter = zcSpentnessToGet.find(op.txHash_);
         if (sptIter == zcSpentnessToGet.end())
         {
            sptIter = zcSpentnessToGet.insert(
               make_pair(op.txHash_, set<unsigned>())).first;
         }

         sptIter->second.insert(op.txOutIndex_);
      }

      //grab spentness
      auto getSpentness = [bdvObj](map<BinaryData, set<unsigned>> query)->
         map<BinaryData, map<unsigned, SpentnessResult>>
      {
         auto prom =
            make_shared<promise<map<BinaryData, map<unsigned, SpentnessResult>>>>();
         auto fut = prom->get_future();
         auto lbd = [prom](ReturnMessage<std::map<BinaryData, std::map<unsigned,
            SpentnessResult>>> msg)
         {
            try
            {
               prom->set_value(msg.get());
            }
            catch (ClientMessageError&)
            {
               prom->set_exception(current_exception());
            }
         };

         bdvObj->getSpentnessForOutputs(query, lbd);
         return fut.get();
      };

      map<BinaryData, set<unsigned>> spentnessToGet;
      for (auto& opPair : addrOp.outpoints_)
      {
         for (auto& op : opPair.second)
         {
            auto iter = spentnessToGet.find(op.txHash_);
            if (iter == spentnessToGet.end())
            {
               iter = spentnessToGet.insert(
                  make_pair(op.txHash_, set<unsigned>())).first;
            }

            iter->second.insert(op.txOutIndex_);
         }
      }

      //add an invalid hash
      spentnessToGet.insert(make_pair(BtcUtils::EmptyHash(), set<unsigned>({0})));

      //grab the spentnees
      auto spentnessData = getSpentness(spentnessToGet);

      //check spentness data vs addr outpoint data
      for (auto& opPair : addrOp.outpoints_)
      {
         for (auto& op : opPair.second)
         {
            if (op.isSpent_)
            {
               auto iter = spentnessData.find(op.txHash_);
               ASSERT_NE(iter, spentnessData.end());

               auto idIter = iter->second.find(op.txOutIndex_);
               ASSERT_NE(idIter, iter->second.end());

               EXPECT_EQ(idIter->second.spender_, op.spenderHash_);
               EXPECT_EQ(idIter->second.state_, OutputSpentnessState::Spent);
            }
            else
            {
               auto iter = spentnessData.find(op.txHash_);
               ASSERT_NE(iter, spentnessData.end());

               auto idIter = iter->second.find(op.txOutIndex_);
               ASSERT_NE(idIter, iter->second.end());

               EXPECT_EQ(idIter->second.spender_.getSize(), 0ULL);
               EXPECT_EQ(idIter->second.height_, UINT32_MAX);
               EXPECT_EQ(idIter->second.state_, OutputSpentnessState::Unspent);
            }
         }
      }

      //check the invalid hash
      {
         auto iter = spentnessData.find(BtcUtils::EmptyHash());
         ASSERT_NE(iter, spentnessData.end());

         auto idIter = iter->second.find(0);
         ASSERT_NE(idIter, iter->second.end());

         EXPECT_EQ(idIter->second.spender_.getSize(), 0ULL);
         EXPECT_EQ(idIter->second.height_, UINT32_MAX);
         EXPECT_EQ(idIter->second.state_, OutputSpentnessState::Invalid);
      }

      //sneak in a bad sized hash, should throw
      spentnessToGet.insert(make_pair(READHEX("0011223344"), set<unsigned>()));

      try
      {
         auto spentnessData2 = getSpentness(spentnessToGet);
         ASSERT_FALSE(true);
      }
      catch (ClientMessageError& e)
      {
         EXPECT_EQ(e.what(), 
            string(R"(Error processing command: 84
   errMsg: "malformed output data")"));
      }

      //get zc utxos
      auto zcutxo_prom = make_shared<promise<vector<UTXO>>>();
      auto zcutxo_fut = zcutxo_prom->get_future();
      auto zcutxo_get = [zcutxo_prom](ReturnMessage<vector<UTXO>> utxoV)->void
      {
         zcutxo_prom->set_value(move(utxoV.get()));
      };
      wallet1.getSpendableZCList(zcutxo_get);
      auto&& zcUtxos = zcutxo_fut.get();
      ASSERT_EQ(zcUtxos.size(), loopCount);

      //resolver
      class ResolverUT : public ResolverFeed
      {
      private:
         map<BinaryData, SecureBinaryData> scriptToPub_;
         map<SecureBinaryData, SecureBinaryData> pubToPriv_;

      public:
         ResolverUT(const vector<KeyPair>& keyPairs) :
            ResolverFeed()
         {
            for (auto& keyPair : keyPairs)
            {
               scriptToPub_.insert(make_pair(keyPair.scrHash_, keyPair.pub_));
               pubToPriv_.insert(make_pair(keyPair.pub_, keyPair.priv_));
            }
         }

         BinaryData getByVal(const BinaryData& val) override
         {
            auto iter = scriptToPub_.find(val);
            if (iter == scriptToPub_.end())
               throw std::runtime_error("invalid value");
            return iter->second;
         }

         const SecureBinaryData& getPrivKeyForPubkey(const BinaryData& pubkey) override
         {
            auto iter = pubToPriv_.find(pubkey);
            if (iter == pubToPriv_.end())
               throw std::runtime_error("invalid value");
            return iter->second;
         }

         BIP32_AssetPath resolveBip32PathForPubkey(const BinaryData&) override
         {
            throw std::runtime_error("invalid pubkey");
         }

         void setBip32PathForPubkey(const BinaryData&, const BIP32_AssetPath&) override
         {}
      };

      auto zcFeed = make_shared<ResolverUT>(keyPairs);

      //spend some
      zcVec.clear();
      map<BinaryData, unsigned> newZcHashes;
      unsigned count = 0;
      for (unsigned i=0; i<loopCount; i+=2)
      {
         auto& utxo = zcUtxos[i];

         //sign
         {
            auto spenderA = make_shared<ScriptSpender>(utxo);
            Signer signer;
            signer.addSpender(spenderA);

            auto id = stagger % _scrAddrVec1.size();

            auto recipient = std::make_shared<Recipient_P2PKH>(
               _scrAddrVec1[id].getSliceCopy(1, 20), utxo.getValue());
            signer.addRecipient(recipient);

            signer.setFeed(zcFeed);
            signer.sign();
            auto rawTx = signer.serializeSignedTx();
            Tx tx(rawTx);
            newZcHashes.insert(make_pair(tx.getThisHash(), count++));
            zcVec.push_back(signer.serializeSignedTx(), 130000000, stagger++);
         }
      }

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      pCallback->waitOnSignal(BDMAction_ZC);  

      //grab new zc output data
      auto newZcAddrOp = getAddrOp(UINT32_MAX, zcAddrOp.zcIndexCutoff_ + 1);
      
      //we create 5 new zc that spend from 5 existing zc and create 5 new 
      //zc outputs, therefor we should have 10 outputs in the batch
      ASSERT_EQ(newZcAddrOp.outpoints_.size(), loopCount);

      //check the output batch
      for (auto& opPair : newZcAddrOp.outpoints_)
      {
         for (auto& op : opPair.second)
         {
            auto iter = newZcHashes.find(op.txHash_);
            if (iter == newZcHashes.end())
            {
               ASSERT_TRUE(op.isSpent_);
               ASSERT_EQ(op.spenderHash_.getSize(), 32ULL);

               auto spenderIter = newZcHashes.find(op.spenderHash_);
               ASSERT_TRUE(spenderIter != newZcHashes.end());
            }
            else
            {
               ASSERT_FALSE(op.isSpent_);
               ASSERT_EQ(op.spenderHash_.getSize(), 0ULL);
            }
         }
      }

      //add invalid hash to the new zc spentness to track
      zcSpentnessToGet.insert(make_pair(BtcUtils::EmptyHash(), set<unsigned>({0})));
      map<BinaryData, map<unsigned, SpentnessResult>> newZcSpentness;
      {
         auto prom =
            make_shared<promise<map<BinaryData, map<unsigned, SpentnessResult>>>>();
         auto fut = prom->get_future();
         auto lbd = [prom](ReturnMessage<map<BinaryData, map<unsigned,
            SpentnessResult>>> msg)
         {
            try
            {
               prom->set_value(msg.get());
            }
            catch (ClientMessageError&)
            {
               prom->set_exception(current_exception());
            }
         };

         bdvObj->getSpentnessForZcOutputs(zcSpentnessToGet, lbd);
         newZcSpentness = move(fut.get());
      }

      //check spentness data vs addr outpoint data
      unsigned spentCount = 0;
      unsigned unspentCount = 0;
      unsigned invalidCount = 0;
      for (auto& zcSpentness : zcSpentnessToGet)
      {
         auto iter = newZcSpentness.find(zcSpentness.first);
         ASSERT_NE(iter, newZcSpentness.end());

         for (auto& zcOp : zcSpentness.second)
         {
            auto idIter = iter->second.find(zcOp);
            ASSERT_NE(idIter, iter->second.end());

            switch (idIter->second.state_)
            {
               case OutputSpentnessState::Spent:
               {
                  auto spenderIter = newZcHashes.find(idIter->second.spender_);
                  EXPECT_NE(spenderIter, newZcHashes.end());
                  EXPECT_EQ(idIter->second.height_, spenderIter->second + loopCount);
                  ++spentCount;
                  break;
               }

               case OutputSpentnessState::Unspent:
               {
                  EXPECT_EQ(idIter->second.spender_.getSize(), 0ULL);

                  ++unspentCount;
                  break;
               }

               case OutputSpentnessState::Invalid:
               {
                  EXPECT_EQ(zcSpentness.first, BtcUtils::EmptyHash());

                  ++invalidCount;
                  break;
               }
            }
         }
      }

      EXPECT_EQ(spentCount, loopCount/2);
      EXPECT_EQ(unspentCount, loopCount/2);
      EXPECT_EQ(invalidCount, 1U);

      //check the invalid hash
      {
         auto iter = newZcSpentness.find(BtcUtils::EmptyHash());
         ASSERT_NE(iter, newZcSpentness.end());

         auto idIter = iter->second.find(0);
         ASSERT_NE(idIter, iter->second.end());

         EXPECT_EQ(idIter->second.spender_.getSize(), 0ULL);
         EXPECT_EQ(idIter->second.height_, UINT32_MAX);
         EXPECT_EQ(idIter->second.state_, OutputSpentnessState::Invalid);
      }
   }

   //cleanup
   auto&& bdvObj2 = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::listenPort(), Armory::Config::getDataDir(),
      authPeersPassLbd_, NetworkSettings::ephemeralPeers(), true, nullptr);
   bdvObj2->addPublicKey(serverPubkey);
   bdvObj2->connectToRemote();

   bdvObj2->shutdown(NetworkSettings::cookie());
   WebSocketServer::waitOnShutdown();

   EXPECT_EQ(theBDMt_->bdm()->zeroConfCont()->getMatcherMapSize(), 0U);

   delete theBDMt_;
   theBDMt_ = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/*
Zc failure tests:
   p2p node down
   p2p timeout into rpc down
   p2p timeout into rpc successful push but client d/c in between (dangling bdvPtr)
*/

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

   CryptoECDSA::setupContext();

   GOOGLE_PROTOBUF_VERIFY_VERSION;
   srand(time(0));
   std::cout << "Running main() from gtest_main.cc\n";

   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();

   FLUSHLOG();
   CLEANUPLOG();

   CryptoECDSA::shutdown();
   return exitCode;
}
