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

#include "TestUtils.h"
#include <reorgTest/blkdata.h>

#include <Utils/ArmoryConfig.h>
#include <Utils/FileUtils.h>
#include <Utils/UniversalTimer.h>
#include <Wallets/IOHeader.h>
#include <Wallets/Seeds/Seeds.h>
#include <Signer/ScriptSpender.h>
#include <ZeroConf/Parser.h>
#include <ZeroConf/Utils.h>
#include <ZeroConf/Notifications.h>
#include <Network/WebSocketClient.h>

#include "BDM_mainthread.h"
#include "Server.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/BDV.capnp.h"

using namespace Armory;
using namespace Armory::ZeroConf;

using namespace std::string_view_literals;
using namespace std::chrono_literals;

namespace {
   std::shared_ptr<Signing::ScriptSpender> getSpenderPtr(
      const UTXO& utxo, bool RBF = false)
   {
      auto spender = std::make_shared<Signing::ScriptSpender>(utxo);
      if (RBF) {
         spender->setSequence(UINT32_MAX -2);
      }
      return spender;
   }
}

////////////////////////////////////////////////////////////////////////////////
#define METHOD_ASSERT_EQ(a, b) \
   if (a != b) { EXPECT_EQ(a, b); return false; }

#define METHOD_ASSERT_NE(a, b) \
   if (a == b) { EXPECT_NE(a, b); return false; }

#define METHOD_ASSERT_TRUE(a) \
   if (!(a)) { EXPECT_TRUE(false); return false; }

#define METHOD_ASSERT_FALSE(a) \
   if (a) { EXPECT_FALSE(true); return false; }

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class ZeroConfTests_Mempool : public ::testing::Test
{
private:
   BinaryData getOutpoint(const BinaryData& hash, uint32_t id) const
   {
      BinaryWriter bw;
      bw.put_BinaryData(hash);
      bw.put_uint32_t(id);
      return bw.getData();
   }

   void addAddrToMap(const BinaryData& addr)
   {
      mainAddrMap_->emplace(addr, nullptr);
   }

   void createTx(unsigned txid,
      std::vector<unsigned> txins,
      std::vector<unsigned> txouts)
   {
      txs_.emplace_back(TxData());
      auto& txData = txs_.back();
      txData.id = txid;
      txData.txIns = txins;
      txData.txOuts = txouts;

      auto key = zcKeys_[txid];
      txData.txPtr = std::make_shared<ZeroConf::ParsedTx>(key);
      auto tx = txData.txPtr;

      tx->setTxHash(zcHashes_[txid]);
      for (auto& id : txins) {
         const auto& txindata = txIns_[id];
         ZeroConf::ParsedTxIn pTxIn;

         pTxIn.value = txindata.amount;
         pTxIn.scrAddr = txindata.scrAddr;
         pTxIn.opRef.unserialize(txindata.outpoint.serialized);
         pTxIn.opRef.setDbKey(txindata.outpoint.txKey);

         tx->inputs.push_back(pTxIn);
         addAddrToMap(pTxIn.scrAddr);
      }

      for (auto& id : txouts) {
         const auto& txoutdata = txOuts_[id];
         ZeroConf::ParsedTxOut pTxOut;

         pTxOut.scrAddr = txoutdata.scrAddr;
         pTxOut.value = txoutdata.amount;
         tx->outputs.push_back(pTxOut);
         addAddrToMap(pTxOut.scrAddr);
      }
      tx->state = ZeroConf::ParsedTxStatus::Resolved;
   }

   void createTx0()
   {
      zcKeys_.push_back(Types::constructZCKey(1));
      zcHashes_.push_back(READHEX(
         "000102030405060708090A0B0C0D0E0FF0F1F2F3F4F5F6F7F8F9FAFBFCFDFEFF"));

      //txin0
      txIns_.emplace_back(TxInData());
      auto& txIn0 = txIns_.back();
      txIn0.amount = 10*COIN;
      txIn0.scrAddr = READHEX("000102030405060708090A");

      //outpoint0
      OutpointData outpoint0;
      outpoint0.hash = READHEX(
         "0101010101010101010101010101010101010101010101010101010101010101");
      outpoint0.index = 4;
      outpoint0.txKey = Types::constructTxKey(0x54, 3); //READHEX("000054000003")
      outpoint0.serialized = getOutpoint(outpoint0.hash, outpoint0.index);
      txIn0.outpoint = outpoint0;

      //txin1
      txIns_.emplace_back(TxInData());
      auto& txIn1 = txIns_.back();
      txIn1.amount = 5*COIN;
      txIn1.scrAddr = READHEX("00A1A2A3A4A5A6A7A8A9AA");

      //outpoint1
      OutpointData outpoint1;
      outpoint1.hash = READHEX(
         "0202020202020202020202020202020202020202020202020202020202020202");
      outpoint1.index = 2;
      outpoint1.txKey = Types::constructTxKey(0x62, 0x0A); //READHEX("00006200000A");
      outpoint1.serialized = getOutpoint(outpoint1.hash, outpoint1.index);
      txIn1.outpoint = outpoint1;

      //txout0
      txOuts_.emplace_back(TxOutData());
      auto& txOut0 = txOuts_.back();
      txOut0.scrAddr = READHEX("00B1B2B3B4B5B6B7B8B9BA");
      txOut0.amount = 7*COIN;

      //txout1
      txOuts_.emplace_back(TxOutData());
      auto& txOut1 = txOuts_.back();
      txOut1.scrAddr = READHEX("00C1C2C3C4C5C6C7C8C9CA");
      txOut1.amount = 8*COIN;

      //create tx
      createTx(0, {0, 1}, {0, 1});
   }

   void createTx1()
   {
      zcKeys_.push_back(Types::constructZCKey(2));
      zcHashes_.push_back(READHEX(
         "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABBBBB"));

      //txin2
      txIns_.emplace_back(TxInData());
      auto& txIn2 = txIns_.back();
      txIn2.amount = 45*COIN;
      txIn2.scrAddr = READHEX("C1C2C3C4C5C6C7C8C9CACB");

      //txin3
      txIns_.emplace_back(TxInData());
      auto& txIn3 = txIns_.back();
      txIn3.amount = 35*COIN;
      txIn3.scrAddr = READHEX("D1D2D3D4D5D6D7D8D9DADB");

      //outpoint2
      OutpointData outpoint2;
      outpoint2.hash = READHEX(
         "0303030303303030303030303030303030303030303030303030303030303030");
      outpoint2.index = 34;
      outpoint2.txKey = Types::constructTxKey(0x87, 0x10); //READHEX("000087000010");
      outpoint2.serialized = getOutpoint(outpoint2.hash, outpoint2.index);
      txIn2.outpoint = outpoint2;

      //outpoint3
      OutpointData outpoint3;
      outpoint3.hash = READHEX(
         "0404040404040404040404040404040404040404040404040404040404040404");
      outpoint3.index = 0;
      outpoint3.txKey = Types::constructTxKey(0x11, 0x0203); //READHEX("000011000203");
      outpoint3.serialized = getOutpoint(outpoint3.hash, outpoint3.index);
      txIn3.outpoint = outpoint3;

      //txout2
      txOuts_.emplace_back(TxOutData());
      auto& txOut2 = txOuts_.back();
      txOut2.scrAddr = READHEX("001112131415161718191F");
      txOut2.amount = 70*COIN;

      //txout3
      txOuts_.emplace_back(TxOutData());
      auto& txOut3 = txOuts_.back();
      txOut3.scrAddr = READHEX("0022232425262728292A2B");
      txOut3.amount = 10*COIN;

      //create tx
      createTx(1, {2, 3}, {2, 3});
   }

   void createTx2()
   {
      //child of tx0 & tx1 (txouts 0 & 2)
      zcKeys_.push_back(Types::constructZCKey(3));
      zcHashes_.push_back(READHEX(
         "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACCCCCCCAAAAAAAAAAAAAAAAAABBBBB"));

      //txin4
      txIns_.emplace_back(TxInData());
      auto& txIn4 = txIns_.back();
      txIn4.amount = txOuts_[0].amount;
      txIn4.scrAddr = txOuts_[0].scrAddr;

      //txin5
      txIns_.emplace_back(TxInData());
      auto& txIn5 = txIns_.back();
      txIn5.amount = txOuts_[2].amount;
      txIn5.scrAddr = txOuts_[2].scrAddr;

      //outpoint4
      OutpointData outpoint4;
      outpoint4.hash = zcHashes_[0];
      outpoint4.index = 0;
      outpoint4.txKey = zcKeys_[0];
      outpoint4.serialized = getOutpoint(outpoint4.hash, outpoint4.index);
      txIn4.outpoint = outpoint4;

      //outpoint5
      OutpointData outpoint5;
      outpoint5.hash = zcHashes_[1];
      outpoint5.index = 0;
      outpoint5.txKey = zcKeys_[1];
      outpoint5.serialized = getOutpoint(outpoint5.hash, outpoint5.index);
      txIn5.outpoint = outpoint5;

      //txout4
      txOuts_.emplace_back(TxOutData());
      auto& txOut4 = txOuts_.back();
      txOut4.scrAddr = READHEX("AAAAAAAAAAA4359802FF34");
      txOut4.amount = 27*COIN;

      //txout5
      txOuts_.emplace_back(TxOutData());
      auto& txOut5 = txOuts_.back();
      txOut5.scrAddr = READHEX("BBBBBBB342564CCCF4536C");
      txOut5.amount = 50*COIN;

      //create tx
      createTx(2, {4, 5}, {4, 5});
   }

   void createTx3()
   {
      //child of tx1 (txout 3)
      zcKeys_.push_back(Types::constructZCKey(4));
      zcHashes_.push_back(READHEX(
         "AAAAAAAAAAAAAAAAAAAAAAAAAAFFFFFFFFFCCCCCCCAAAAAAAAAAAAAAAAABBBBB"));

      //txin6
      txIns_.emplace_back(TxInData());
      auto& txIn6 = txIns_.back();
      txIn6.amount = txOuts_[3].amount;
      txIn6.scrAddr = txOuts_[3].scrAddr;

      //outpoint6
      OutpointData outpoint6;
      outpoint6.hash = zcHashes_[1];
      outpoint6.index = 1;
      outpoint6.txKey = zcKeys_[1];
      outpoint6.serialized = getOutpoint(outpoint6.hash, outpoint6.index);
      txIn6.outpoint = outpoint6;

      //txout6
      txOuts_.emplace_back(TxOutData());
      auto& txOut6 = txOuts_.back();
      txOut6.scrAddr = READHEX("EEEEEEEEEEEEEEE4534622");
      txOut6.amount = 2*COIN;

      //txout7
      txOuts_.emplace_back(TxOutData());
      auto& txOut7 = txOuts_.back();
      txOut7.scrAddr = READHEX("EEEEEEEEEEEEEE98790234");
      txOut7.amount = 8*COIN;

      //create tx
      createTx(3, {6}, {6, 7});
   }

   void createTx4()
   {
      //child of tx2 (txout 4)
      zcKeys_.push_back(Types::constructZCKey(5));
      zcHashes_.push_back(READHEX(
         "AAAAAAAAABBBBBBBBBBBBBBBBBBBBBBBBB3CCCCCCCAAAAAAAAAAAAAAAAABBBBB"));

      //txin7
      txIns_.emplace_back(TxInData());
      auto& txIn7 = txIns_.back();
      txIn7.amount = txOuts_[4].amount;
      txIn7.scrAddr = txOuts_[4].scrAddr;

      //outpoint7
      OutpointData outpoint7;
      outpoint7.hash = zcHashes_[2];
      outpoint7.index = 0;
      outpoint7.txKey = zcKeys_[2];
      outpoint7.serialized = getOutpoint(outpoint7.hash, outpoint7.index);
      txIn7.outpoint = outpoint7;

      //txout8
      txOuts_.emplace_back(TxOutData());
      auto& txOut8 = txOuts_.back();
      txOut8.scrAddr = txOuts_[0].scrAddr;
      txOut8.amount = 17*COIN;

      //txout9
      txOuts_.emplace_back(TxOutData());
      auto& txOut9 = txOuts_.back();
      txOut9.scrAddr = READHEX("DDDDDDDDDDDDDD98790234");
      txOut9.amount = 10*COIN;

      //create tx
      createTx(4, {7}, {8, 9});
   }

protected:
   class ZeroConfCallbacks_Tests : public ZeroConf::ZeroConfCallbacks
   {
      std::set<Types::BdvId> hasScrAddr(const Types::ScrAddr&) const override
      {
         return {};
      }

      void pushZcNotification(
         std::shared_ptr<ZeroConf::MempoolSnapshot>,
         std::shared_ptr<ZeroConf::KeyAddrMap>,
         std::map<Types::BdvId, ZeroConf::ParsedZCData>,
         Types::BdvId,
         std::map<Types::TxHash, std::shared_ptr<ZeroConf::WatcherTxBody>>&) override
      {}

      void pushZcError(Types::BdvId, const Types::TxHash&,
         ArmoryErrorCodes, const std::string&) override
      {}
   };

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
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
         "--db-type=DB_SUPER",
         "--thread-count=3",
         "--public"},
         Config::ProcessType::DB);

      //addrMap
      mainAddrMap_ = std::make_shared<std::map<BinaryData, std::shared_ptr<AddrAndHash>>>();

      //create the transactions
      createTx0();
      createTx1();
      createTx2();
      createTx3();
      createTx4();
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);
   }

   /////////////////////////////////////////////////////////////////////////////
   bool checkTxIsStaged(
      const ZeroConf::MempoolSnapshot& snapshot,
      unsigned txid) const
   {
      if (txid >= txs_.size()) {
         return false;
      }
      const auto& txData = txs_[txid];

      //check it was added
      METHOD_ASSERT_TRUE(snapshot.hasHash(zcHashes_[txid]));

      auto zckey = snapshot.getKeyForHash(zcHashes_[txid]);
      METHOD_ASSERT_EQ(zckey, zcKeys_[txid]);

      auto zcPtr = snapshot.getTxByKey(zckey);
      EXPECT_NE(zcPtr, nullptr);

      //inputs
      for (unsigned i=0; i<txData.txIns.size(); i++)
      try {
         auto txInId = txData.txIns[i];

         auto txOutKey = Types::constructTxIOKeyFromTxKey(
            txIns_[txInId].outpoint.txKey, txIns_[txInId].outpoint.index);

         auto txioKeys = snapshot.getTxioKeysForScrAddr(txIns_[txInId].scrAddr);
         METHOD_ASSERT_FALSE(txioKeys.empty());

         bool foundTxio = false;
         for (const auto& key : txioKeys) {
            if (key != txOutKey) {
               continue;
            }
            foundTxio = true;
            auto txio = snapshot.getTxioByKey(key);
            METHOD_ASSERT_NE(txio, nullptr);
            EXPECT_EQ(txio->getTxIOKeyOfOutput(), txOutKey);
            EXPECT_EQ(txio->getIndexOfOutput(), txIns_[txInId].outpoint.index);

            EXPECT_EQ(txio->getTxKeyOfInput(), zcKeys_[txid]);
            EXPECT_EQ(txio->getIndexOfInput(), i);
            EXPECT_EQ(txio->getAmount(), txIns_[txInId].amount);

            EXPECT_TRUE(snapshot.isTxOutSpentByZC(txOutKey));
         }

         METHOD_ASSERT_TRUE(foundTxio);
      } catch (const std::range_error&) {
         return false;
      }

      //outputs
      for (unsigned i=0; i<txData.txOuts.size(); i++)
      try {
         auto txOutId = txData.txOuts[i];
         auto txOutKey = Types::constructTxIOKeyFromTxKey(zcKeys_[txid], i);

         auto txioKeys = snapshot.getTxioKeysForScrAddr(txOuts_[txOutId].scrAddr);
         METHOD_ASSERT_FALSE(txioKeys.empty());

         bool foundTxio = false;
         for (const auto& key : txioKeys) {
            if (Types::getTxKeyFromTxIOKey(key) != zcKeys_[txid]) {
               continue;
            }
            foundTxio = true;
            auto txio = snapshot.getTxioByKey(key);
            METHOD_ASSERT_NE(txio, nullptr);
            EXPECT_EQ(txio->getTxIOKeyOfOutput(), txOutKey);
            EXPECT_EQ(txio->getIndexOfOutput(), i);
         }
         METHOD_ASSERT_TRUE(foundTxio);
      } catch (const std::range_error&) {
         return false;
      }
      return true;
   }

   /////////////////////////////////////////////////////////////////////////////
   bool checkIsDropped(
      const ZeroConf::MempoolSnapshot& snapshot, Types::TxIOId txId) const
   {
      if (txId >= txs_.size()) {
         return false;
      }
      const auto& txData = txs_[txId];

      EXPECT_FALSE(snapshot.hasHash(zcHashes_[txId]));
      auto zckey = snapshot.getKeyForHash(zcHashes_[txId]);
      EXPECT_EQ(zckey, Types::INVALID_TX_KEY);

      auto zcPtr = snapshot.getTxByKey(zcKeys_[txId]);
      METHOD_ASSERT_EQ(zcPtr, nullptr);

      //inputs
      for (unsigned i=0; i<txData.txIns.size(); i++) {
         auto txInId = txData.txIns[i];
         auto txOutKey = Types::constructTxIOKeyFromTxKey(
            txIns_[txInId].outpoint.txKey, txIns_[txInId].outpoint.index);

         try {
            auto txioKeys = snapshot.getTxioKeysForScrAddr(
               txIns_[txInId].scrAddr);

            for (auto& key : txioKeys) {
               auto txio = snapshot.getTxioByKey(key);
               if (txio == nullptr) {
                  continue;
               }
               METHOD_ASSERT_NE(txio->getTxKeyOfOutput(), zcKeys_[txId]);

               if (!txio->hasTxIn()) {
                  continue;
               }
               METHOD_ASSERT_NE(txio->getTxKeyOfInput(), zcKeys_[txId]);
            }
         } catch (const std::range_error&) {}

         auto txio = snapshot.getTxioByKey(txOutKey);
         if (txio != nullptr) {
            METHOD_ASSERT_TRUE(txio->hasTxOutZC());
            if (txio->hasTxIn()) {
               METHOD_ASSERT_NE(txio->getTxKeyOfInput(), zcKeys_[txId]);
            }
         }

         EXPECT_FALSE(snapshot.isTxOutSpentByZC(txOutKey));
      }

      for (unsigned i=0; i<txData.txOuts.size(); i++) {
         auto txOutId = txData.txOuts[i];
         auto txOutKey = Types::constructTxIOKeyFromTxKey(zcKeys_[txId], i);

         try {
            auto txioKeys = snapshot.getTxioKeysForScrAddr(
               txOuts_[txOutId].scrAddr);
            METHOD_ASSERT_TRUE(false);
         } catch (const std::range_error&) {}

         auto txio = snapshot.getTxioByKey(txOutKey);
         METHOD_ASSERT_EQ(txio, nullptr);
      }
      return true;
   }

   /////////////////////////////////////////////////////////////////////////////
   Types::TxIOKey checkTxOutIsSpent(
      const ZeroConf::MempoolSnapshot& snapshot,
      unsigned txid, unsigned txoutid) const
   {
      auto txOutKey = Types::constructTxIOKeyFromTxKey(zcKeys_[txid], txoutid);
      auto txio = snapshot.getTxioByKey(txOutKey);
      if (txio == nullptr) {
         return Types::INVALID_TXIO_KEY;
      }
      if (!txio->hasTxIn()) {
         return Types::INVALID_TXIO_KEY;
      }
      return txio->getTxIOKeyOfInput();
   }

protected:
   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};

   /*****/
   std::vector<Types::TxKey> zcKeys_;
   std::vector<Types::TxHash> zcHashes_;

   struct OutpointData
   {
      Types::TxHash hash;
      uint32_t index;
      Types::TxKey txKey;
      BinaryData serialized;
   };

   struct TxInData
   {
      Types::Amount amount;
      Types::ScrAddr scrAddr;

      OutpointData outpoint;
   };

   struct TxOutData
   {
      Types::Amount amount;
      Types::ScrAddr scrAddr;
   };

   struct TxData
   {
      std::vector<unsigned> txIns;
      std::vector<unsigned> txOuts;
      unsigned id;
      std::shared_ptr<ZeroConf::ParsedTx> txPtr;
   };

   std::vector<TxInData> txIns_;
   std::vector<TxOutData> txOuts_;
   std::vector<TxData> txs_;

   /*****/

   //mainAddressMap
   std::shared_ptr<std::map<Types::ScrAddr, std::shared_ptr<AddrAndHash>>> mainAddrMap_;
   ZeroConfCallbacks_Tests zcCallbacks_;
};

//TODO: copy snapshot, force merge, check it matches original

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Mempool, Stage)
{
   ZeroConf::MempoolSnapshot snapshot(1, 2);
   EXPECT_EQ(snapshot.getTopZcID(), 0U);

   //filter the tx
   auto filterResult = filterParsedTx(txs_[0].txPtr,
      [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
      {
         return mainAddrMap->find(addr) == mainAddrMap->end();
      },
      &zcCallbacks_
   );

   //stage it
   snapshot.stageNewZC(txs_[0].txPtr, filterResult);

   //check it was added
   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_EQ(snapshot.getTopZcID(), 1U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Mempool, Commit)
{
   ZeroConf::MempoolSnapshot snapshot(2, 4);
   EXPECT_EQ(snapshot.getTopZcID(), 0U);

   //filter the tx
   auto filterResult = filterParsedTx(txs_[0].txPtr,
      [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
      {
         return mainAddrMap->find(addr) == mainAddrMap->end();
      },
      &zcCallbacks_
   );

   //stage it
   snapshot.stageNewZC(txs_[0].txPtr, filterResult);

   //check it was added
   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));

   //commit
   snapshot.commitNewZCs();

   //check the tx is still in there
   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_EQ(snapshot.getTopZcID(), 1U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Mempool, Drop)
{
   ZeroConf::MempoolSnapshot snapshot(2, 4);
   EXPECT_EQ(snapshot.getTopZcID(), 0U);

   //filter the tx
   auto filterResult = filterParsedTx(txs_[0].txPtr,
      [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
      {
         return mainAddrMap->find(addr) == mainAddrMap->end();
      },
      &zcCallbacks_
   );

   //stage it
   snapshot.stageNewZC(txs_[0].txPtr, filterResult);

   //check it was added
   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_EQ(snapshot.getTopZcID(), 1U);

   //drop the tx
   auto droppedZCs = snapshot.dropZc(zcKeys_[0]);
   ASSERT_EQ(droppedZCs.size(), 1ULL);
   EXPECT_EQ(droppedZCs.begin()->first, zcKeys_[0]);

   //check it was dropped from the snapshot
   EXPECT_TRUE(checkIsDropped(snapshot, 0));
   EXPECT_EQ(snapshot.getTopZcID(), 1U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Mempool, CommitAndDrop)
{
   ZeroConf::MempoolSnapshot snapshot(2, 4);
   EXPECT_EQ(snapshot.getTopZcID(), 0U);

   //filter the tx
   auto filterResult = filterParsedTx(txs_[0].txPtr,
      [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
      {
         return mainAddrMap->find(addr) == mainAddrMap->end();
      },
      &zcCallbacks_
   );

   //stage it
   snapshot.stageNewZC(txs_[0].txPtr, filterResult);

   //check it was added
   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_EQ(snapshot.getTopZcID(), 1U);

   //commit and check again
   snapshot.commitNewZCs();
   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));

   //drop the tx
   auto droppedZCs = snapshot.dropZc(zcKeys_[0]);
   ASSERT_EQ(droppedZCs.size(), 1ULL);
   EXPECT_EQ(droppedZCs.begin()->first, zcKeys_[0]);

   //check it was dropped from the snapshot
   EXPECT_TRUE(checkIsDropped(snapshot, 0));

   //commit and check
   snapshot.commitNewZCs();
   EXPECT_TRUE(checkIsDropped(snapshot, 0));
   EXPECT_EQ(snapshot.getTopZcID(), 1U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Mempool, Stage2_Drop1)
{
   ZeroConf::MempoolSnapshot snapshot(2, 4);
   EXPECT_EQ(snapshot.getTopZcID(), 0U);

   {
      //add tx0
      auto filterResult = filterParsedTx(txs_[0].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[0].txPtr, filterResult);

      //add tx1
      auto filterResult1 = filterParsedTx(txs_[1].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[1].txPtr, filterResult1);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   EXPECT_EQ(snapshot.getTopZcID(), 2U);

   //drop tx0
   auto droppedZCs = snapshot.dropZc(zcKeys_[0]);
   ASSERT_EQ(droppedZCs.size(), 1ULL);
   EXPECT_EQ(droppedZCs.begin()->first, zcKeys_[0]);

   //check it was dropped from the snapshot
   EXPECT_TRUE(checkIsDropped(snapshot, 0));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   EXPECT_EQ(snapshot.getTopZcID(), 2U);

   //drop tx1
   droppedZCs = snapshot.dropZc(zcKeys_[1]);
   ASSERT_EQ(droppedZCs.size(), 1ULL);
   EXPECT_EQ(droppedZCs.begin()->first, zcKeys_[1]);

   EXPECT_TRUE(checkIsDropped(snapshot, 0));
   EXPECT_TRUE(checkIsDropped(snapshot, 1));
   EXPECT_EQ(snapshot.getTopZcID(), 2U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Mempool, Stage2_Commit_Drop1)
{
   ZeroConf::MempoolSnapshot snapshot(1, 2);
   EXPECT_EQ(snapshot.getTopZcID(), 0U);

   {
      //add tx0
      auto filterResult = filterParsedTx(txs_[0].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[0].txPtr, filterResult);
      EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   }

   snapshot.commitNewZCs();
   EXPECT_EQ(snapshot.getTopZcID(), 1U);

   {
      //add tx1
      auto filterResult1 = filterParsedTx(txs_[1].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[1].txPtr, filterResult1);
      EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
      EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   }

   snapshot.commitNewZCs();
   EXPECT_EQ(snapshot.getTopZcID(), 2U);

   //drop tx0
   auto droppedZCs = snapshot.dropZc(zcKeys_[0]);
   ASSERT_EQ(droppedZCs.size(), 1ULL);
   EXPECT_EQ(droppedZCs.begin()->first, zcKeys_[0]);

   //check it was dropped from the snapshot
   EXPECT_TRUE(checkIsDropped(snapshot, 0));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   EXPECT_EQ(snapshot.getTopZcID(), 2U);

   snapshot.commitNewZCs();

   //check it is still dropped from the snapshot
   EXPECT_TRUE(checkIsDropped(snapshot, 0));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   EXPECT_EQ(snapshot.getTopZcID(), 2U);

   //drop last tx
   droppedZCs = snapshot.dropZc(zcKeys_[1]);
   ASSERT_EQ(droppedZCs.size(), 1ULL);
   EXPECT_EQ(droppedZCs.begin()->first, zcKeys_[1]);

   EXPECT_TRUE(checkIsDropped(snapshot, 0));
   EXPECT_TRUE(checkIsDropped(snapshot, 1));
   EXPECT_EQ(snapshot.getTopZcID(), 2U);

   snapshot.commitNewZCs();
   EXPECT_EQ(snapshot.getTopZcID(), 2U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Mempool, StageChildren)
{
   ZeroConf::MempoolSnapshot snapshot(2, 4);

   {
      //add tx0
      auto filterResult = filterParsedTx(txs_[0].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[0].txPtr, filterResult);

      //add tx1
      auto filterResult1 = filterParsedTx(txs_[1].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[1].txPtr, filterResult1);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 0), Types::INVALID_TXIO_KEY);
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 1), Types::INVALID_TXIO_KEY);

   EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 1, 0), Types::INVALID_TXIO_KEY);
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 1, 1), Types::INVALID_TXIO_KEY);

   {
      //add tx2
      auto filterResult2 = filterParsedTx(txs_[2].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[2].txPtr, filterResult2);

      //add tx3
      auto filterResult3 = filterParsedTx(txs_[3].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[3].txPtr, filterResult3);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

   {
      auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender0), zcKeys_[2]);
      EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 1), Types::INVALID_TXIO_KEY);

      auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender1), zcKeys_[2]);

      auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender2), zcKeys_[3]);

      EXPECT_EQ(checkTxOutIsSpent(snapshot, 2, 0), Types::INVALID_TXIO_KEY);
   }

   {
      //add tx4
      auto filterResult4 = filterParsedTx(txs_[4].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[4].txPtr, filterResult4);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 3));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 4));

   {
      auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender0), zcKeys_[2]);
      EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 1), Types::INVALID_TXIO_KEY);

      auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender1), zcKeys_[2]);

      auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender2), zcKeys_[3]);

      auto spender3 = checkTxOutIsSpent(snapshot, 2, 0);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender3), zcKeys_[4]);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Mempool, StageChildren_Commit)
{
   ZeroConf::MempoolSnapshot snapshot(1, 2);

   {
      //add tx0
      auto filterResult = filterParsedTx(txs_[0].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[0].txPtr, filterResult);

      //add tx1
      auto filterResult1 = filterParsedTx(txs_[1].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[1].txPtr, filterResult1);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 0), Types::INVALID_TXIO_KEY);
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 1), Types::INVALID_TXIO_KEY);
   
   EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 1, 0), Types::INVALID_TXIO_KEY);
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 1, 1), Types::INVALID_TXIO_KEY);

   snapshot.commitNewZCs();

   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 0), Types::INVALID_TXIO_KEY);
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 1), Types::INVALID_TXIO_KEY);
   
   EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 1, 0), Types::INVALID_TXIO_KEY);
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 1, 1), Types::INVALID_TXIO_KEY);

   {
      //add tx2
      auto filterResult2 = filterParsedTx(txs_[2].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[2].txPtr, filterResult2);

      //add tx3
      auto filterResult3 = filterParsedTx(txs_[3].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[3].txPtr, filterResult3);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

   {
      auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender0), zcKeys_[2]);
      EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 1), Types::INVALID_TXIO_KEY);

      auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender1), zcKeys_[2]);

      auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender2), zcKeys_[3]);

      EXPECT_EQ(checkTxOutIsSpent(snapshot, 2, 0), Types::INVALID_TXIO_KEY);
   }

   snapshot.commitNewZCs();

   EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

   {
      auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender0), zcKeys_[2]);
      EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 1), Types::INVALID_TXIO_KEY);

      auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender1), zcKeys_[2]);

      auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender2), zcKeys_[3]);

      EXPECT_EQ(checkTxOutIsSpent(snapshot, 2, 0), Types::INVALID_TXIO_KEY);
   }

   {
      //add tx4
      auto filterResult4 = filterParsedTx(txs_[4].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[4].txPtr, filterResult4);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 4));

   auto spender3 = checkTxOutIsSpent(snapshot, 2, 0);
   EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender3), zcKeys_[4]);

   snapshot.commitNewZCs();

   EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

   {
      auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender0), zcKeys_[2]);
      EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 1), Types::INVALID_TXIO_KEY);

      auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender1), zcKeys_[2]);

      auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender2), zcKeys_[3]);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 4));

   auto spender4 = checkTxOutIsSpent(snapshot, 2, 0);
   EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender4), zcKeys_[4]);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Mempool, DropParent)
{
   ZeroConf::MempoolSnapshot snapshot(2, 4);

   {
      //add tx0
      auto filterResult = filterParsedTx(txs_[0].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[0].txPtr, filterResult);

      //add tx1
      auto filterResult1 = filterParsedTx(txs_[1].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[1].txPtr, filterResult1);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 0), Types::INVALID_TXIO_KEY);
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 1), Types::INVALID_TXIO_KEY);
   
   EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 1, 0), Types::INVALID_TXIO_KEY);
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 1, 1), Types::INVALID_TXIO_KEY);

   {
      //add tx2
      auto filterResult2 = filterParsedTx(txs_[2].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[2].txPtr, filterResult2);

      //add tx3
      auto filterResult3 = filterParsedTx(txs_[3].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[3].txPtr, filterResult3);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

   auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
   EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender0), zcKeys_[2]);
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 1), Types::INVALID_TXIO_KEY);

   auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
   EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender1), zcKeys_[2]);

   auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
   EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender2), zcKeys_[3]);

   //drop tx0
   auto droppedZCs = snapshot.dropZc(zcKeys_[0]);
   ASSERT_EQ(droppedZCs.size(), 2ULL);

   auto iter = droppedZCs.begin();
   EXPECT_EQ(iter->first, zcKeys_[0]);

   ++iter;
   EXPECT_EQ(iter->first, zcKeys_[2]);

   EXPECT_TRUE(checkIsDropped(snapshot, 0));
   EXPECT_TRUE(checkIsDropped(snapshot, 2));

   //check tx1 & 3 are still here
   EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

   EXPECT_EQ(checkTxOutIsSpent(snapshot, 1, 0), Types::INVALID_TXIO_KEY);

   auto spender3 = checkTxOutIsSpent(snapshot, 1, 1);
   EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender3), zcKeys_[3]);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Mempool, DropParent_Commit)
{
   ZeroConf::MempoolSnapshot snapshot(1, 2);

   {
      //add tx0
      auto filterResult = filterParsedTx(txs_[0].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[0].txPtr, filterResult);

      //add tx1
      auto filterResult1 = filterParsedTx(txs_[1].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[1].txPtr, filterResult1);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 0), Types::INVALID_TXIO_KEY);
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 1), Types::INVALID_TXIO_KEY);
   
   EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 1, 0), Types::INVALID_TXIO_KEY);
   EXPECT_EQ(checkTxOutIsSpent(snapshot, 1, 1), Types::INVALID_TXIO_KEY);

   {
      //add tx2
      auto filterResult2 = filterParsedTx(txs_[2].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[2].txPtr, filterResult2);

      //add tx3
      auto filterResult3 = filterParsedTx(txs_[3].txPtr,
         [mainAddrMap=mainAddrMap_](const Types::ScrAddr& addr)->bool
         {
            return mainAddrMap->find(addr) == mainAddrMap->end();
         },
         &zcCallbacks_
      );
      snapshot.stageNewZC(txs_[3].txPtr, filterResult3);
   }

   {
      EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
      EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

      auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender0), zcKeys_[2]);
      EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 1), Types::INVALID_TXIO_KEY);

      auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender1), zcKeys_[2]);

      auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender2), zcKeys_[3]);
   }

   snapshot.commitNewZCs();

   {
      EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
      EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

      auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender0), zcKeys_[2]);
      EXPECT_EQ(checkTxOutIsSpent(snapshot, 0, 1), Types::INVALID_TXIO_KEY);

      auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender1), zcKeys_[2]);

      auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender2), zcKeys_[3]);
   }

   //drop tx0
   auto droppedZCs = snapshot.dropZc(zcKeys_[0]);
   ASSERT_EQ(droppedZCs.size(), 2ULL);

   auto iter = droppedZCs.begin();
   EXPECT_EQ(iter->first, zcKeys_[0]);

   ++iter;
   EXPECT_EQ(iter->first, zcKeys_[2]);

   {
      EXPECT_TRUE(checkIsDropped(snapshot, 0));
      EXPECT_TRUE(checkIsDropped(snapshot, 2));

      //check tx1 & 3 are still here
      EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
      EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

      EXPECT_EQ(checkTxOutIsSpent(snapshot, 1, 0), Types::INVALID_TXIO_KEY);

      auto spender3 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender3), zcKeys_[3]);
   }

   snapshot.commitNewZCs();

   {
      EXPECT_TRUE(checkIsDropped(snapshot, 0));
      EXPECT_TRUE(checkIsDropped(snapshot, 2));

      //check tx1 & 3 are still here
      EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
      EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

      EXPECT_EQ(checkTxOutIsSpent(snapshot, 1, 0), Types::INVALID_TXIO_KEY);

      auto spender3 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_EQ(Types::getTxKeyFromTxIOKey(spender3), zcKeys_[3]);
   }
}

////////////////////////////////////////////////////////////////////////////////
class ZeroConfTests_FullNode : public ::testing::Test
{
protected:
   void initBDM(void)
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
      zeros_ = READHEX("00000000");

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

      //first UTXO to hit scrAddrF
      firstUtxoScrAddrF_ = UTXO(500000000, 3, UINT16_MAX, 1,
         READHEX("9ec8177ca0a4f7aa21ec88a324f236a4d1dce6c610812a90e16febef4603a438"),
         READHEX("76a914d63b766cd342e6f0f7390dd454065e4bbea26b1b88ac"));
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      if (clients_ != nullptr) {
         clients_->shutdown();
      }
      theBDMt_->shutdown();

      Config::reset();
      delete clients_;
      delete theBDMt_;

      theBDMt_ = nullptr;
      clients_ = nullptr;

      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory("./ldbtestdir");

      std::filesystem::create_directory("./ldbtestdir");

      Config::reset();
      CLEANUP_ALL_TIMERS();
   }

   BlockDataManagerThread *theBDMt_;
   Clients* clients_;
   LMDBBlockDatabase* iface_;
   BinaryData zeros_;

   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;

   std::string wallet1id;
   std::string wallet2id;
   std::string LB1ID;
   std::string LB2ID;

   UTXO firstUtxoScrAddrF_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_FullNode, Load4Blocks_ReloadBDM_ZC_Plus2)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   clients_->init();
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrE
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
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::HEADERS), 3U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::HEADERS), TestChain::blkHash3);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash3)->isMainBranch());

   auto bdm = theBDMt_->bdm();
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 55 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb1ScrAddr, bdm), 10 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb1ScrAddrP2SH, bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb2ScrAddr, bdm), 10 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb2ScrAddrP2SH, bdm), 5 * COIN);

   //restart bdm
   bdvPtr.reset();
   clients_->shutdown();
   theBDMt_->shutdown();

   delete clients_;
   delete theBDMt_;

   initBDM();
   clients_->init();
   bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID,
      false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID,
      false);
   bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   bdm = theBDMt_->bdm();
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 55 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb1ScrAddr, bdm), 10 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb1ScrAddrP2SH, bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb2ScrAddr, bdm), 10 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb2ScrAddrP2SH, bdm), 5 * COIN);

   //add ZC
   std::filesystem::path zcPath(TestUtils::dataDir / "ZCtx.tx");
   BinaryData rawZC; rawZC.resize(TestChain::zcTxSize);
   std::ifstream zcStream(zcPath, std::ios::in | std::ios::binary);
   zcStream.read(rawZC.getCharPtr(), TestChain::zcTxSize);
   zcStream.close();
   DBTestUtils::ZcVector rawZcVec;
   rawZcVec.push_back(std::move(rawZC), 0);

   std::filesystem::path lbPath(TestUtils::dataDir / "LBZC.tx");
   BinaryData rawLBZC; rawLBZC.resize(TestChain::lbZCTxSize);
   std::ifstream lbStream(lbPath, std::ios::in | std::ios::binary);
   lbStream.read(rawLBZC.getCharPtr(), TestChain::lbZCTxSize);
   lbStream.close();
   DBTestUtils::ZcVector rawLBZcVec;
   rawLBZcVec.push_back(std::move(rawLBZC), 0);

   DBTestUtils::pushNewZc(theBDMt_, rawZcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   DBTestUtils::pushNewZc(theBDMt_, rawLBZcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 20 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 65 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb1ScrAddr, bdm), 5 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb1ScrAddrP2SH, bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb2ScrAddr, bdm), 10 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb2ScrAddrP2SH, bdm), 5 * COIN);

   //
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::HEADERS), 5U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::HEADERS), TestChain::blkHash5);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash5)->isMainBranch());

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 70 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 20 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb1ScrAddr, bdm), 5 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb1ScrAddrP2SH, bdm), 25 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb2ScrAddr, bdm), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::lb2ScrAddrP2SH, bdm), 0 * COIN);

   //cleanup
   bdvPtr.reset();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_FullNode, Load3Blocks_ZC_Plus3)
{
   //copy the first 3 blocks
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrE
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::HEADERS), 3U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::HEADERS), TestChain::blkHash3);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash3)->isMainBranch());

   auto bdm = theBDMt_->bdm();
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 55 * COIN);

   //add ZC
   auto ZC1 = TestUtils::getTx(5, 1); //block 5, tx 1
   auto ZChash1 = BtcUtils::getHash256(ZC1);

   DBTestUtils::ZcVector rawZcVec;
   rawZcVec.push_back(ZC1, 1300000000);

   DBTestUtils::pushNewZc(theBDMt_, rawZcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 20 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 65 * COIN);

   //pull ZC from DB, verify it's carrying the proper data
   std::this_thread::sleep_for(100ms);
   auto dbtx = iface_->beginTransaction(
      DB_SELECT::ZERO_CONF, LMDB::Mode::ReadOnly);
   StoredTx zcStx;
   auto zcKey = Types::constructZCKey(0);

   ASSERT_TRUE(iface_->getStoredZC(zcStx, zcKey));
   EXPECT_EQ(zcStx.thisHash, ZChash1);
   EXPECT_EQ(zcStx.numBytes , TestChain::zcTxSize);
   EXPECT_EQ(zcStx.fragBytes, 190U);
   EXPECT_EQ(zcStx.numTxOut, 2U);
   ASSERT_FALSE(zcStx.stxoMap.empty());
   EXPECT_EQ(zcStx.stxoMap.begin()->second.getValue(), 10 * COIN);

   //check ZChash in DB
   {
      auto ss = theBDMt_->bdm()->zeroConfCont()->getSnapshot();
      EXPECT_EQ(ss->getHashForKey(zcKey), ZChash1);
   }

   dbtx.reset();

   //restart bdm
   bdvPtr.reset();

   clients_->shutdown();
   theBDMt_->shutdown();

   delete clients_;
   delete theBDMt_;

   initBDM();
   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

   scrAddrVec.pop_back();
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);
   bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   //add 5th block
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::HEADERS), 4U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::HEADERS), TestChain::blkHash4);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash4)->isMainBranch());

   bdm = theBDMt_->bdm();
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 20 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 20 * COIN);

   dbtx = move(
      iface_->beginTransaction(DB_SELECT::ZERO_CONF, LMDB::Mode::ReadOnly));
   StoredTx zcStx3;

   EXPECT_EQ(iface_->getStoredZC(zcStx3, zcKey), true);
   EXPECT_EQ(zcStx3.thisHash, ZChash1);
   EXPECT_EQ(zcStx3.numBytes, TestChain::zcTxSize);
   EXPECT_EQ(zcStx3.fragBytes, 190U);
   EXPECT_EQ(zcStx3.numTxOut, 2U);
   EXPECT_EQ(zcStx3.stxoMap.begin()->second.getValue(), 10 * COIN);

   dbtx.reset();

   //add 6th block
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::HEADERS), 5U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::HEADERS), TestChain::blkHash5);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash5)->isMainBranch());

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 70 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 20 * COIN);

   //Tx is now in a block, ZC should be gone from DB
   dbtx = move(
      iface_->beginTransaction(DB_SELECT::ZERO_CONF, LMDB::Mode::ReadWrite));
   StoredTx zcStx4;

   EXPECT_EQ(iface_->getStoredZC(zcStx4, zcKey), false);
   dbtx.reset();

   EXPECT_GE(theBDMt_->bdm()->zeroConfCont()->getMergeCount(), 1U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_FullNode, Load3Blocks_ZCchain)
{
   //copy the first 3 blocks
   TestUtils::setBlocks({ "0", "1", "2" }, blk0dat_);

   //get ZCs
   auto ZC1 = TestUtils::getTx(3, 4); //block 3, tx 4
   auto ZC2 = TestUtils::getTx(5, 1); //block 5, tx 1

   auto ZChash1 = BtcUtils::getHash256(ZC1);
   auto ZChash2 = BtcUtils::getHash256(ZC2);

   DBTestUtils::ZcVector zc1Vec;
   DBTestUtils::ZcVector zc2Vec;
   zc1Vec.push_back(std::move(ZC1), 1400000000);
   zc2Vec.push_back(std::move(ZC2), 1500000000);
   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
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
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::HEADERS), 2U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::HEADERS), TestChain::blkHash2);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash2)->isMainBranch());

   auto bdm = theBDMt_->bdm();
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 55 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 0 * COIN);

   //add first ZC
   DBTestUtils::pushNewZc(theBDMt_, zc1Vec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 0 * COIN);

   //add second ZC
   DBTestUtils::pushNewZc(theBDMt_, zc2Vec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 20 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 10 * COIN);

   //add 4th block
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 20 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 65 * COIN);

   //add 5th and 6th block
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::HEADERS), 5U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::HEADERS), TestChain::blkHash5);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash5)->isMainBranch());

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 70 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 20 * COIN);

   EXPECT_GE(theBDMt_->bdm()->zeroConfCont()->getMergeCount(), 1U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_FullNode, Load3Blocks_RBF)
{
   //get ZCs
   auto ZC1 = TestUtils::getTx(5, 1); //block 5, tx 1
   auto ZChash1 = BtcUtils::getHash256(ZC1);

   Tx zcTx1(ZC1);
   Outpoint op0 = zcTx1.getTxInCopy(0).getOutPoint();

   BinaryData rawRBF, spendRBF;

   {
      //build RBF enabled mock ZC, spend first input of 5|1, to bogus address
      BinaryWriter bw;
      bw.put_uint32_t(1); //version number

      //input
      bw.put_var_int(1); //1 input, no need to complicate this
      bw.put_BinaryData(op0.getTxHash()); //hash of tx we are spending
      bw.put_uint32_t(op0.getTxOutIndex()); //output id
      bw.put_var_int(0); //empty script, not like we are checking sigs anyways
      bw.put_uint32_t(1); //flagged sequence number

      //spend script, classic P2PKH
      BinaryData fakeAddr = 
         READHEX("0101010101010101010101010101010101010101");
      BinaryWriter spendScript;
      spendScript.put_uint8_t(OP_DUP);
      spendScript.put_uint8_t(OP_HASH160);
      spendScript.put_var_int(fakeAddr.getSize());
      spendScript.put_BinaryData(fakeAddr); //bogus address
      spendScript.put_uint8_t(OP_EQUALVERIFY);
      spendScript.put_uint8_t(OP_CHECKSIG);

      auto& spendScriptbd = spendScript.getData();

      //output
      bw.put_var_int(1); //txout count
      bw.put_uint64_t(30 * COIN); //value
      bw.put_var_int(spendScriptbd.getSize()); //script length
      bw.put_BinaryData(spendScriptbd); //spend script

      //locktime
      bw.put_uint32_t(UINT32_MAX);

      rawRBF = bw.getData();
   }

   {
      //build bogus ZC spending RBF to self instead
      BinaryWriter bw;
      bw.put_uint32_t(1); //version number

      //input
      bw.put_var_int(1); 
      bw.put_BinaryData(op0.getTxHash());
      bw.put_uint32_t(op0.getTxOutIndex());
      bw.put_var_int(0);
      bw.put_uint32_t(1);

      //spend script, classic P2PKH
      BinaryWriter spendScript;
      spendScript.put_uint8_t(OP_DUP);
      spendScript.put_uint8_t(OP_HASH160);
      spendScript.put_var_int(TestChain::addrA.getSize());
      spendScript.put_BinaryData(TestChain::addrA); //spend back to self
      spendScript.put_uint8_t(OP_EQUALVERIFY);
      spendScript.put_uint8_t(OP_CHECKSIG);

      auto& spendScriptbd = spendScript.getData();

      //output
      bw.put_var_int(1);
      bw.put_uint64_t(30 * COIN); //value
      bw.put_var_int(spendScriptbd.getSize()); //script length
      bw.put_BinaryData(spendScriptbd); //spend script

      //locktime
      bw.put_uint32_t(UINT32_MAX);
      spendRBF = bw.getData();
   }

   auto RBFhash       = BtcUtils::getHash256(rawRBF);
   auto spendRBFhash  = BtcUtils::getHash256(spendRBF);

   DBTestUtils::ZcVector rawRBFVec;
   DBTestUtils::ZcVector spendRBFVec;
   rawRBFVec.push_back(std::move(rawRBF), 1400000000);
   spendRBFVec.push_back(std::move(spendRBF), 1500000000);

   //copy the first 4 blocks
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
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
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   auto bdm = theBDMt_->bdm();
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 55 * COIN);

   //add RBF ZC
   DBTestUtils::pushNewZc(theBDMt_, rawRBFVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 55 * COIN);

   //replace it
   DBTestUtils::pushNewZc(theBDMt_, spendRBFVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 80 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 55 * COIN);

   //add last blocks
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::HEADERS), 5U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, DB_SELECT::HEADERS), TestChain::blkHash5);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash5)->isMainBranch());

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 70 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 20 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_FullNode, Replace_ZC_Test)
{
   BinaryData ZCHash1, ZCHash2, ZCHash3, ZCHash4;

   //
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   Wallets::IO::CreateWalletParams params{homedir_,
      Passphrase::SetNew{1ms, 0, {}},
      Passphrase::SetNew{1ms, 0, {}},
      nullptr, 10
   };

   //create a root private key
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory());
   auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //register with db
   std::vector<BinaryData> addrVec;
   auto hashSet = assetWlt->getAddrHashSet();
   std::vector<BinaryData> hashVec;
   hashVec.insert(hashVec.begin(), hashSet.begin(), hashSet.end());

   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID(),
      false);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   //check balances
   auto bdm = theBDMt_->bdm();
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 55 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrD, bdm), 5  * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrE, bdm), 30 * COIN);

   {
      ////spend 27 from wlt to assetWlt's first 2 unused addresses
      ////send rest back to scrAddrA

      auto spendVal = 27 * COIN;
      Signing::Signer signer;

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());

      //get utxo list for spend value
      auto unspentVec = DBTestUtils::getUTXOsForScrAddrs(bdm, {
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF}
      );

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getAmount();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getAmount();
         signer.addSpender(getSpenderPtr(utxo, true));
      }

      //spend 12 to first address
      auto addr0 = assetWlt->getNewAddress();
      signer.addRecipient(addr0->getRecipient(12 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());

      //spend 15 to addr 1, use P2PKH
      auto addr1 = assetWlt->getNewAddress();
      signer.addRecipient(addr1->getRecipient(15 * COIN));
      addrVec.push_back(addr1->getPrefixedHash());

      if (total > spendVal) {
         //deal with change, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      auto rawTx = signer.serializeSignedTx();
      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(rawTx, 14000000);

      ZCHash1 = std::move(BtcUtils::getHash256(rawTx));
      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 55 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrD, bdm), 8  * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrE, bdm), 0  * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrD, bdm), 8  * COIN);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[0], bdm), 12 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[1], bdm), 15 * COIN);

   {
      //first zc should be valid
      auto ss = bdm->zeroConfCont()->getSnapshot();
      auto key1 = ss->getKeyForHash(ZCHash1);
      ASSERT_TRUE(Types::isTxKeyValid(key1));
   }

   {
      ////Double spend the 27
      auto spendVal = 27 * COIN;
      Signing::Signer signer2;

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());

      //get utxo list for spend value
      auto unspentVec = DBTestUtils::getRBFUTXOs(bdm, {
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE}
      );

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getAmount();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getAmount();
         signer2.addSpender(getSpenderPtr(utxo, true));
      }

      //spend 12 to first address
      auto addr0 = assetWlt->getNewAddress();
      signer2.addRecipient(addr0->getRecipient(12 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());

      //spend 14 to addr 1, use P2PKH
      auto addr1 = assetWlt->getNewAddress();
      signer2.addRecipient(addr1->getRecipient(14 * COIN));
      addrVec.push_back(addr1->getPrefixedHash());

      if (total > spendVal) {
         //deal with change, 1 btc fee
         auto changeVal = total - spendVal - 1 * COIN;
         auto recipientChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer2.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer2.setFeed(feed);
      signer2.sign();
      EXPECT_TRUE(signer2.verify());

      auto rawTx = signer2.serializeSignedTx();
      DBTestUtils::ZcVector zcVec2;
      zcVec2.push_back(rawTx, 15000000);

      ZCHash2 = std::move(BtcUtils::getHash256(rawTx));
      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 55 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrD, bdm), 7  * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrE, bdm), 0  * COIN);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[0], bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[1], bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[2], bdm), 12 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[3], bdm), 14 * COIN);

   //grab zc

   {
      //first zc should be replaced
      auto ss = bdm->zeroConfCont()->getSnapshot();
      auto key1 = ss->getKeyForHash(ZCHash1);
      ASSERT_FALSE(Types::isTxKeyValid(key1));

      //second zc should be valid
      auto key2 = ss->getKeyForHash(ZCHash2);
      ASSERT_TRUE(Types::isTxKeyValid(key2));
      ASSERT_TRUE(Types::isThisAZCKey(key2));
   }

   //cpfp the first rbf
   {
      ////CPFP the 26
      auto spendVal = 15 * COIN;
      Signing::Signer signer3;

      //instantiate resolver feed overloaded object
      auto assetFeed = std::make_shared<Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto unspentVec = DBTestUtils::getZCUTXOs(bdm, {
         addrVec[0],
         addrVec[1],
         addrVec[2],
         addrVec[3]}
      );

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getAmount();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getAmount();
         signer3.addSpender(getSpenderPtr(utxo, true));
      }

      //spend 4 to first address
      auto addr0 = assetWlt->getNewAddress();
      signer3.addRecipient(addr0->getRecipient(4 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());

      //spend 6 to addr 1, use P2PKH
      auto addr1 = assetWlt->getNewAddress();
      signer3.addRecipient(addr1->getRecipient(6 * COIN));
      addrVec.push_back(addr1->getPrefixedHash());

      if (total > spendVal) {
         //deal with change, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer3.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      {
         auto lock = assetWlt->lockDecryptedContainer({});
         signer3.setFeed(assetFeed);
         signer3.sign();
      }
      EXPECT_TRUE(signer3.verify());

      auto rawTx = signer3.serializeSignedTx();
      DBTestUtils::ZcVector zcVec3;
      zcVec3.push_back(rawTx, 16000000);

      ZCHash3 = std::move(BtcUtils::getHash256(rawTx));
      DBTestUtils::pushNewZc(theBDMt_, zcVec3);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 55 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrD, bdm), 18  * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrE, bdm), 0  * COIN);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[0], bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[1], bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[2], bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[3], bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[4], bdm), 4 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[5], bdm), 6 * COIN);

   {
      //first zc should be replaced
      auto ss = bdm->zeroConfCont()->getSnapshot();
      auto key1 = ss->getKeyForHash(ZCHash1);
      ASSERT_FALSE(Types::isTxKeyValid(key1));

      //second zc should be valid
      auto key2 = ss->getKeyForHash(ZCHash2);
      ASSERT_TRUE(Types::isTxKeyValid(key2));
      ASSERT_TRUE(Types::isThisAZCKey(key2));

      //third zc should be valid
      auto key3 = ss->getKeyForHash(ZCHash3);
      ASSERT_TRUE(Types::isTxKeyValid(key3));
      ASSERT_TRUE(Types::isThisAZCKey(key3));
      auto tx3 = ss->getTxByKey(key3);
      ASSERT_TRUE(tx3->isRBF);
      EXPECT_TRUE(tx3->isChainedZc);
   }

   //rbf the 2 zc chain dead
   {
      ////Double spend the 27
      auto spendVal = 22 * COIN;
      Signing::Signer signer2;

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());

      //get utxo list for spend value
      auto unspentVec = DBTestUtils::getRBFUTXOs(bdm, {
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE}
      );

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getAmount();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getAmount();
         signer2.addSpender(getSpenderPtr(utxo, true));
      }

      //spend 12 to first address
      auto addr0 = assetWlt->getNewAddress();
      signer2.addRecipient(addr0->getRecipient(10 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());

      //spend 14 to addr 1, use P2PKH
      auto addr1 = assetWlt->getNewAddress();
      signer2.addRecipient(addr1->getRecipient(12 * COIN));
      addrVec.push_back(addr1->getPrefixedHash());

      if (total > spendVal) {
         //deal with change, 1 btc fee
         auto changeVal = total - spendVal - 1 * COIN;
         auto recipientChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer2.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer2.setFeed(feed);
      signer2.sign();
      EXPECT_TRUE(signer2.verify());

      auto rawTx = signer2.serializeSignedTx();
      DBTestUtils::ZcVector zcVec2;
      zcVec2.push_back(rawTx, 17000000);

      ZCHash4 = std::move(BtcUtils::getHash256(rawTx));
      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 55 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrD, bdm), 12  * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrE, bdm), 0  * COIN);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[0], bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[1], bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[2], bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[3], bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[4], bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[5], bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[6], bdm), 10 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[7], bdm), 12 * COIN);

   {
      //first zc should be replaced
      auto ss = bdm->zeroConfCont()->getSnapshot();
      auto key1 = ss->getKeyForHash(ZCHash1);
      ASSERT_FALSE(Types::isTxKeyValid(key1));

      //second zc should be replaced
      auto key2 = ss->getKeyForHash(ZCHash2);
      ASSERT_FALSE(Types::isTxKeyValid(key2));

      //third zc should be replaced
      auto key3 = ss->getKeyForHash(ZCHash3);
      ASSERT_FALSE(Types::isTxKeyValid(key3));

      //fourth zc should be valid
      auto key4 = ss->getKeyForHash(ZCHash4);
      ASSERT_TRUE(Types::isTxKeyValid(key4));
      ASSERT_TRUE(Types::isThisAZCKey(key4));
      auto tx4 = ss->getTxByKey(key4);
      ASSERT_TRUE(tx4->isRBF);
      EXPECT_FALSE(tx4->isChainedZc);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_FullNode, ChainZC_RBFchild_Test)
{
   BinaryData ZCHash1, ZCHash2, ZCHash3;

   //
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   Wallets::IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, {}},
      Passphrase::SetNew{1ms, 0, {}},
      nullptr, 10
   };
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory());
   auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //register with db
   std::vector<BinaryData> addrVec;
   auto hashSet = assetWlt->getAddrHashSet();
   std::vector<BinaryData> hashVec;
   hashVec.insert(hashVec.begin(), hashSet.begin(), hashSet.end());

   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID(),
      false);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   //check balances
   auto bdm = theBDMt_->bdm();
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 55 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrD, bdm), 5  * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrE, bdm), 30 * COIN);

   //check new wallet balances
   for (auto& scripthash : hashSet) {
      EXPECT_EQ(DBTestUtils::getScrAddrBalance(scripthash, bdm), 0 * COIN);
   }

   {
      ////spend 27 from wlt to assetWlt's first 2 unused addresses
      ////send rest back to scrAddrA

      auto spendVal = 27 * COIN;
      Signing::Signer signer;

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());

      //get utxo list for spend value
      auto unspentVec = DBTestUtils::getUTXOsForScrAddrs(bdm, {
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF}
      );

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getAmount();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getAmount();
         signer.addSpender(getSpenderPtr(utxo, true));
      }

      //spend 12 to first address
      auto addr0 = assetWlt->getNewAddress();
      signer.addRecipient(addr0->getRecipient(12 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());

      //spend 15 to addr 1, use P2PKH
      auto addr1 = assetWlt->getNewAddress();
      signer.addRecipient(addr1->getRecipient(15 * COIN));
      addrVec.push_back(addr1->getPrefixedHash());

      if (total > spendVal) {
         //deal with change, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      auto rawTx = signer.serializeSignedTx();
      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(rawTx, 14000000);

      ZCHash1 = std::move(BtcUtils::getHash256(rawTx));
      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      auto txioVec = DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
      EXPECT_EQ(txioVec.first.size(), 5ULL);
      EXPECT_EQ(txioVec.second.size(), 0ULL);

      for (const auto& txio : txioVec.first) {
         if (txio.hasTxOutZC()) {
            auto txObj = DBTestUtils::getTxByKey(clients_, bdvID,
               txio.getTxKeyOfOutput());
            EXPECT_EQ(txObj.getThisHash(), ZCHash1);
         }
         if (txio.hasTxInZC()) {
            auto txObj = DBTestUtils::getTxByKey(clients_, bdvID,
               txio.getTxKeyOfInput());
            EXPECT_EQ(txObj.getThisHash(), ZCHash1);
         }
      }
   }

   //check balances
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 55 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrD, bdm), 8  * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrE, bdm), 0  * COIN);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[0], bdm), 12 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[1], bdm), 15 * COIN);

   {
      //first zc should be valid
      auto ss = bdm->zeroConfCont()->getSnapshot();
      auto key1 = ss->getKeyForHash(ZCHash1);
      ASSERT_TRUE(Types::isTxKeyValid(key1));
      ASSERT_TRUE(Types::isThisAZCKey(key1));
      auto tx1 = ss->getTxByKey(key1);
      EXPECT_TRUE(tx1->isRBF);
      EXPECT_FALSE(tx1->isChainedZc);
   }

   //cpfp the first zc
   {
      Signing::Signer signer3;

      //instantiate resolver feed overloaded object
      auto assetFeed = std::make_shared<Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto unspentVec = DBTestUtils::getZCUTXOs(bdm, {
         addrVec[0],
         addrVec[1]}
      );

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : unspentVec) {
         total += utxo.getAmount();
         signer3.addSpender(getSpenderPtr(utxo, true));
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
      auto recipientChange = std::make_shared<Signing::Recipient_P2PKH>(
         TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
      signer3.addRecipient(recipientChange);

      //sign, verify then broadcast
      {
         auto lock = assetWlt->lockDecryptedContainer({});
         signer3.setFeed(assetFeed);
         signer3.sign();
      }

      auto rawTx = signer3.serializeSignedTx();
      DBTestUtils::ZcVector zcVec3;
      zcVec3.push_back(rawTx, 15000000);

      ZCHash2 = std::move(BtcUtils::getHash256(rawTx));
      DBTestUtils::pushNewZc(theBDMt_, zcVec3);
      auto txioVec = DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
      EXPECT_EQ(txioVec.first.size(), 6ULL);
      EXPECT_EQ(txioVec.second.size(), 0ULL);

      std::set<Types::TxKey> txKeys;
      for (const auto& txio : txioVec.first) {
         if (txio.hasTxOutZC()) {
            txKeys.emplace(txio.getTxKeyOfOutput());
         }
         if (txio.hasTxInZC()) {
            txKeys.emplace(txio.getTxKeyOfInput());
         }
      }
      for (const auto& txKey : txKeys) {
         auto txObj = DBTestUtils::getTxByKey(clients_, bdvID, txKey);
         auto zcId = Types::getZcIdFromTxKey(txKey);
         switch (zcId)
         {
            case 0:
               EXPECT_EQ(txObj.getThisHash(), ZCHash1);
               break;

            case 1:
               EXPECT_EQ(txObj.getThisHash(), ZCHash2);
               break;

            default:
               EXPECT_TRUE(false);
         }
      }
   }

   //check balances
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 55 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrD, bdm), 25 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrE, bdm), 0  * COIN);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[0], bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[1], bdm), 0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[2], bdm), 4 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[3], bdm), 6 * COIN);

   {
      //first zc should still be valid
      auto ss = bdm->zeroConfCont()->getSnapshot();
      auto key1 = ss->getKeyForHash(ZCHash1);
      ASSERT_TRUE(Types::isTxKeyValid(key1));
      ASSERT_TRUE(Types::isThisAZCKey(key1));
      auto tx1 = ss->getTxByKey(key1);
      EXPECT_TRUE(tx1->isRBF);
      EXPECT_FALSE(tx1->isChainedZc);

      //second zc should be valid
      auto key2 = ss->getKeyForHash(ZCHash2);
      ASSERT_TRUE(Types::isTxKeyValid(key2));
      ASSERT_TRUE(Types::isThisAZCKey(key2));
      auto tx2 = ss->getTxByKey(key2);
      EXPECT_TRUE(tx2->isRBF);
      EXPECT_TRUE(tx2->isChainedZc);
   }

   //rbf the child
   {
      auto spendVal = 10 * COIN;
      Signing::Signer signer2;

      //instantiate resolver feed
      auto assetFeed =
         std::make_shared<Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto unspentVec = DBTestUtils::getRBFUTXOs(bdm, {addrVec[0]});

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getAmount();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getAmount();
         signer2.addSpender(getSpenderPtr(utxo, true));
      }

      //spend 5 to new address
      auto addr0 = assetWlt->getNewAddress();
      signer2.addRecipient(addr0->getRecipient(6 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());
      if (total > spendVal) {
         //change addrE, 1 btc fee
         auto changeVal = 5 * COIN;
         auto recipientChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), changeVal);
         signer2.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      {
         auto lock = assetWlt->lockDecryptedContainer({});
         signer2.setFeed(assetFeed);
         signer2.sign();
      }
      EXPECT_TRUE(signer2.verify());

      auto rawTx = signer2.serializeSignedTx();
      DBTestUtils::ZcVector zcVec2;
      zcVec2.push_back(rawTx, 17000000);

      ZCHash3 = std::move(BtcUtils::getHash256(rawTx));
      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      auto txioVec = DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
      EXPECT_EQ(txioVec.first.size(), 5ULL);
      EXPECT_EQ(txioVec.second.size(), 1ULL);

      //tally zc txkeys and run some sanity checks
      std::set<Types::TxKey> txKeys;
      for (const auto& txio : txioVec.first) {
         EXPECT_FALSE(txio.getScrAddr().empty());
         EXPECT_NE(txio.getTxTime(), 0);

         ASSERT_TRUE(Types::isThisATxIOKey(txio.getTxIOKeyOfOutput()));
         if (txio.hasTxOutZC()) {
            txKeys.emplace(txio.getTxKeyOfOutput());
         }

         if (txio.hasTxInZC()) {
            ASSERT_TRUE(Types::isThisATxIOKey(txio.getTxIOKeyOfInput()));
            txKeys.emplace(txio.getTxKeyOfInput());
         }
      }

      for (const auto& txKey : txKeys) {
         auto txObj = DBTestUtils::getTxByKey(clients_, bdvID, txKey);
         auto zcId = Types::getZcIdFromTxKey(txKey);
         switch (zcId)
         {
            case 0:
               EXPECT_EQ(txObj.getThisHash(), ZCHash1);
               break;

            case 2:
               EXPECT_EQ(txObj.getThisHash(), ZCHash3);
               break;

            default:
               EXPECT_TRUE(false);
         }
      }
      EXPECT_EQ(*txioVec.second.begin(), ZCHash2);
   }

   //check balances
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 30 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm), 55 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrD, bdm), 8 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrE, bdm), 5  * COIN);

   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[0], bdm),  0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[1], bdm), 15 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[2], bdm),  0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[3], bdm),  0 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(addrVec[4], bdm),  6 * COIN);

   {
      //first zc should still be valid
      auto ss = bdm->zeroConfCont()->getSnapshot();
      auto key1 = ss->getKeyForHash(ZCHash1);
      ASSERT_TRUE(Types::isTxKeyValid(key1));
      ASSERT_TRUE(Types::isThisAZCKey(key1));
      auto tx1 = ss->getTxByKey(key1);
      EXPECT_TRUE(tx1->isRBF);
      EXPECT_FALSE(tx1->isChainedZc);

      //second zc should be replaced
      auto key2 = ss->getKeyForHash(ZCHash2);
      ASSERT_FALSE(Types::isTxKeyValid(key2));

      //third zc should be valid
      auto key3 = ss->getKeyForHash(ZCHash3);
      ASSERT_TRUE(Types::isTxKeyValid(key3));
      ASSERT_TRUE(Types::isThisAZCKey(key3));
      auto tx3 = ss->getTxByKey(key3);
      EXPECT_TRUE(tx3->isRBF);
      EXPECT_TRUE(tx3->isChainedZc);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_FullNode, ZC_InOut_SameBlock)
{
   BinaryData ZCHash1, ZCHash2, ZCHash3;

   //
   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

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

   //check balances
   auto bdm = theBDMt_->bdm();
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm),  0 * COIN);

   //add the 2 zc
   auto ZC1 = TestUtils::getTx(2, 1); //block 2, tx 1
   auto ZChash1 = BtcUtils::getHash256(ZC1);

   auto ZC2 = TestUtils::getTx(2, 2); //block 2, tx 2
   auto ZChash2 = BtcUtils::getHash256(ZC2);

   DBTestUtils::ZcVector rawZcVec;
   rawZcVec.push_back(ZC1, 1300000000);
   rawZcVec.push_back(ZC2, 1310000000);

   DBTestUtils::pushNewZc(theBDMt_, rawZcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   //check balances
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm),  5 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm),  0 * COIN);

   //add last block
   TestUtils::appendBlocks({ "2" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   //check balances
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrA, bdm), 50 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrB, bdm), 55 * COIN);
   EXPECT_EQ(DBTestUtils::getScrAddrBalance(TestChain::scrAddrC, bdm),  0 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//NOTE: have to rework supernode scanner before getting to this
#if 0
class ZeroConfTests_Supernode : public ::testing::Test
{
protected:
   void initBDM(void)
   {
      DBTestUtils::init();

      Config::reset();
      Config::DBSettings::setServiceType(SERVICE_UNITTEST);
      Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_SUPER",
         "--thread-count=3"},
         Config::ProcessType::DB);

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
      zeros_ = READHEX("00000000");

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
      wallet1id = "wallet1";
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

      theBDMt_ = nullptr;
      clients_ = nullptr;

      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory("./ldbtestdir");

      std::filesystem::create_directory("./ldbtestdir");

      Config::reset();
      CLEANUP_ALL_TIMERS();
   }

   BlockDataManagerThread *theBDMt_;
   Clients* clients_;
   LMDBBlockDatabase* iface_;
   BinaryData zeros_;

   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;

   std::string wallet1id;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode, ZeroConfUpdate)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrE
   };

   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);

   BinaryData ZChash;
   {
      ////spend 27 from wlt to assetWlt's first 2 unused addresses
      ////send rest back to scrAddrA

      auto spendVal = 27 * COIN;
      Signing::Signer signer;
      signer.setLockTime(3);

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());

      //get utxo list for spend value
      auto unspentVec = wlt->getSpendableTxOutListForValue(spendVal);

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getAmount();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getAmount();
         signer.addSpender(getSpenderPtr(utxo, true));
      }

      //spendVal to addrE
      auto recipientChange = std::make_shared<Signing::Recipient_P2PKH>(
         TestChain::scrAddrD.getSliceCopy(1, 20), spendVal);
      signer.addRecipient(recipientChange);

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      Tx zctx(signer.serializeSignedTx());
      ZChash = zctx.getThisHash();

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 1300000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   EXPECT_EQ(wlt->getScrAddrObjByKey(TestChain::scrAddrA)->getFullBalance(), 50 * COIN);
   EXPECT_EQ(wlt->getScrAddrObjByKey(TestChain::scrAddrB)->getFullBalance(), 30 * COIN);
   EXPECT_EQ(wlt->getScrAddrObjByKey(TestChain::scrAddrC)->getFullBalance(), 55 * COIN);
   EXPECT_EQ(wlt->getScrAddrObjByKey(TestChain::scrAddrE)->getFullBalance(), 3 * COIN);

   //test ledger entry
   Ledgers::Entry le = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash);

   //EXPECT_EQ(le.getTxTime(), 1300000000);
   EXPECT_EQ(le.isSentToSelf(), false);
   EXPECT_EQ(le.getValue(), -27 * (int64_t)COIN);

   //check ZChash in DB
   {
      auto zcKey = Types::constructZCKey(0);
      auto ss = theBDMt_->bdm()->zeroConfCont()->getSnapshot();
      EXPECT_EQ(ss->getHashForKey(zcKey), ZChash);
   }

   //grab ZC by hash
   auto txobj = DBTestUtils::getTxByHash(clients_, bdvID, ZChash);
   EXPECT_EQ(txobj.getThisHash(), ZChash);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode, UnrelatedZC_CheckLedgers)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);
   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

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

   auto ZC1 = TestUtils::getTx(5, 2); //block 5, tx 2
   auto ZChash1 = BtcUtils::getHash256(ZC1);

   auto ZC2 = TestUtils::getTx(5, 1); //block 5, tx 1
   auto ZChash2 = BtcUtils::getHash256(ZC2);

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

   try {
      auto snapshot = theBDMt_->bdm()->zeroConfCont()->getSnapshot();
      auto zcTxios = snapshot->getTxioMapForScrAddr(TestChain::scrAddrD);
      EXPECT_EQ(zcTxios.size(), 1ULL);
      iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
      DBTestUtils::addTxioToSsh(ssh, zcTxios);
      EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   {
      auto snapshot = theBDMt_->bdm()->zeroConfCont()->getSnapshot();
      auto zcTxios = snapshot->getTxioMapForScrAddr(TestChain::scrAddrF);
      ASSERT_FALSE(zcTxios.empty());
      iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
      DBTestUtils::addTxioToSsh(ssh, zcTxios);
      EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   }

   //grab ledger for 1st ZC, should be empty
   try {
      DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash1);
      ASSERT_TRUE(false);
   } catch (const std::runtime_error& e) {
      EXPECT_EQ(e.what(), std::string{"no ledger for txhash"});
   }

   //grab ledger for 2nd ZC
   auto zcledger = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash2);
   EXPECT_EQ(zcledger.getValue(), 30 * (int64_t)COIN);
   EXPECT_EQ(zcledger.getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(zcledger.isOptInRBF());

   //grab delegate ledger
   auto delegateLedger =
      DBTestUtils::getHistoryPage(clients_, bdvID, delegateID, 0);

   unsigned zc2_count = 0;
   for (auto& ld : delegateLedger) {
      if (ld.getTxHash() == ZChash2) {
         zc2_count++;
      }
   }

   EXPECT_EQ(zc2_count, 1U);

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

   {
      auto snapshot = theBDMt_->bdm()->zeroConfCont()->getSnapshot();
      auto zcTxios = snapshot->getTxioMapForScrAddr(TestChain::scrAddrD);
      ASSERT_TRUE(zcTxios.empty());
   }
   
   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
   EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);

   {
      auto snapshot = theBDMt_->bdm()->zeroConfCont()->getSnapshot();
      auto zcTxios = snapshot->getTxioMapForScrAddr(TestChain::scrAddrF);
      EXPECT_TRUE(zcTxios.empty());
   }

   iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
   EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);

   //try to get ledgers, ZCs should be all gone
   try {
      DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash1);
      ASSERT_TRUE(false);
   } catch (const std::runtime_error& e) {
      EXPECT_EQ(e.what(), std::string{"no ledger for txhash"});
   }

   zcledger = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash2);
   EXPECT_EQ(zcledger.getTxTime(), 1231009513U);
   EXPECT_EQ(zcledger.getBlockNum(), 5U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode, RegisterAfterZC)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);
   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

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

   auto ZC1 = TestUtils::getTx(5, 2); //block 5, tx 2
   auto ZChash1 = BtcUtils::getHash256(ZC1);

   auto ZC2 = TestUtils::getTx(5, 1); //block 5, tx 1
   auto ZChash2 = BtcUtils::getHash256(ZC2);

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

   try {
      auto snapshot = theBDMt_->bdm()->zeroConfCont()->getSnapshot();
      auto zcTxios = snapshot->getTxioMapForScrAddr(TestChain::scrAddrD);
      iface_->getStoredScriptHistory(ssh, TestChain::scrAddrD);
      DBTestUtils::addTxioToSsh(ssh, zcTxios);
      EXPECT_EQ(ssh.getScriptBalance(), 65 * COIN);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   try {
      auto snapshot = theBDMt_->bdm()->zeroConfCont()->getSnapshot();
      auto zcTxios = snapshot->getTxioMapForScrAddr(TestChain::scrAddrF);
      iface_->getStoredScriptHistory(ssh, TestChain::scrAddrF);
      DBTestUtils::addTxioToSsh(ssh, zcTxios);
      EXPECT_EQ(ssh.getScriptBalance(), 5 * COIN);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }

   //Register scrAddrD with the wallet. It should have the ZC balance
   scrAddrVec.push_back(TestChain::scrAddrD);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      true);

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
TEST_F(ZeroConfTests_Supernode, ZC_Reorg)
{
   //
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

   Wallets::IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, {}},
      Passphrase::SetNew{1ms, 0, {}},
      nullptr, 3
   };
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory());
   auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
      std::move(seed), params);
   auto addr1_ptr = assetWlt->getNewAddress();
   auto addr2_ptr = assetWlt->getNewAddress();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };
   auto wltSet = assetWlt->getAddrHashSet();
   std::vector<BinaryData> wltVec;
   for (auto& addr : wltSet) {
      wltVec.push_back(addr);
   }

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);
   DBTestUtils::registerWallet(clients_, bdvID, wltVec, assetWlt->getID(),
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto assetWltDbObj = bdvPtr->getWalletOrLockbox(assetWlt->getID());
   auto delegateID = DBTestUtils::getLedgerDelegate(clients_, bdvID);

   //check balances
   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 70 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);

   BinaryData ZCHash1, ZCHash2;
   for (auto& sa : wltSet) {
      scrObj = assetWltDbObj->getScrAddrObjByKey(sa);
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   }

   {
      Signing::Signer signer;

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());
      feed->addPrivKey(TestChain::privKeyAddrF.getRef());

      //get utxo list for spend value
      auto&& unspentVec = wlt->getSpendableTxOutListForValue(UINT64_MAX);

      //consume 1st utxo, send 2 to scrAddrA, 3 to new wallet
      signer.addSpender(getSpenderPtr(unspentVec[0]));
      signer.addRecipient(addr1_ptr->getRecipient(3 * COIN));
      auto recipientChange = std::make_shared<Signing::Recipient_P2PKH>(
         TestChain::scrAddrA.getSliceCopy(1, 20), 2 * COIN);
      signer.addRecipient(recipientChange);
      signer.setFeed(feed);
      signer.sign();

      //2nd tx, 2nd utxo, 5 to scrAddrB, 5 new wallet
      Signing::Signer signer2;
      signer2.addSpender(getSpenderPtr(unspentVec[1]));
      signer2.addRecipient(addr2_ptr->getRecipient(5 * COIN));
      auto recipientChange2 = std::make_shared<Signing::Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), 5 * COIN);
      signer2.addRecipient(recipientChange2);
      signer2.setFeed(feed);
      signer2.sign();

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);
      ZCHash1 = zcVec.zcVec_.back().first.getThisHash();

      zcVec.push_back(signer2.serializeSignedTx(), 14100000);
      ZCHash2 = zcVec.zcVec_.back().first.getThisHash();

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 52 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 75 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   scrObj = assetWltDbObj->getScrAddrObjByKey(addr1_ptr->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 3 * COIN);
   scrObj = assetWltDbObj->getScrAddrObjByKey(addr2_ptr->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);

   //reorg the chain
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5", "4A", "5A" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   auto newBlockNotif = DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   //check new block callback carries an invalidated zc notif as well
   auto notifRaw = std::get<0>(newBlockNotif);
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(notifRaw.getPtr()),
      notifRaw.getSize() / sizeof(capnp::word)
   );
   capnp::FlatArrayMessageReader message(words);
   auto msgRoot = message.getRoot<Codec::BDV::Notifications>();
   auto capnNotifs = msgRoot.getNotifs();
   ASSERT_EQ(capnNotifs.size(), 2);

   auto notifIndex = std::get<1>(newBlockNotif);
   EXPECT_EQ(notifIndex, 0U);

   //grab the invalidated zc notif, it should carry the hash for both our ZC
   auto zcNotif = capnNotifs[1];
   EXPECT_EQ(zcNotif.which(), Codec::BDV::Notification::Which::INVALIDATED_ZC);

   auto ids = zcNotif.getInvalidatedZc();
   EXPECT_EQ(ids.size(), 2);

   //check zc hash 1
   auto capnId0 = ids[0];
   BinaryData id0(capnId0.begin(), capnId0.end());
   EXPECT_EQ(ZCHash1, id0);

   //check zc hash 2
   auto capnId1 = ids[1];
   BinaryData id1(capnId1.begin(), capnId1.end());
   EXPECT_EQ(ZCHash2, id1);

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);

   scrObj = assetWltDbObj->getScrAddrObjByKey(addr1_ptr->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = assetWltDbObj->getScrAddrObjByKey(addr2_ptr->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode, ChainZC_RBFchild_Test)
{
   BinaryData ZCHash1, ZCHash2, ZCHash3;

   //
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   Wallets::IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, {}},
      Passphrase::SetNew{1ms, 0, {}},
      nullptr, 10
   };
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory());
   auto assetWlt = Wallets::AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //register with db
   std::vector<BinaryData> addrVec;
   auto hashSet = assetWlt->getAddrHashSet();
   std::vector<BinaryData> hashVec;
   hashVec.insert(hashVec.begin(), hashSet.begin(), hashSet.end());

   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID(),
      false);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);
   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::HEADERS), 3U);

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
   for (auto& scripthash : hashSet) {
      scrObj = dbAssetWlt->getScrAddrObjByKey(scripthash);
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   }

   {
      ////spend 27 from wlt to assetWlt's first 2 unused addresses
      ////send change back to scrAddrD

      auto spendVal = 27 * COIN;
      Signing::Signer signer;

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());

      //get utxo list for spend value
      auto unspentVec = wlt->getSpendableTxOutListForValue(spendVal);

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getAmount();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getAmount();
         signer.addSpender(getSpenderPtr(utxo, true));
      }

      //spend 12 to first address
      auto addr0 = assetWlt->getNewAddress();
      signer.addRecipient(addr0->getRecipient(12 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());

      //spend 15 to addr 1, use P2PKH
      auto addr1 = assetWlt->getNewAddress();
      signer.addRecipient(addr1->getRecipient(15 * COIN));
      addrVec.push_back(addr1->getPrefixedHash());

      if (total > spendVal) {
         //deal with change, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      auto rawTx = signer.serializeSignedTx();
      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(rawTx, 14000000);

      ZCHash1 = std::move(BtcUtils::getHash256(rawTx));
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
   EXPECT_EQ(zcledger.getValue(), 27 * (int64_t)COIN);
   //EXPECT_EQ(zcledger.getTxTime(), 14000000);
   EXPECT_TRUE(zcledger.isOptInRBF());

   //cpfp the first zc
   {
      Signing::Signer signer3;

      //instantiate resolver feed overloaded object
      auto assetFeed = std::make_shared<Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListZC();

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : unspentVec) {
         total += utxo.getAmount();
         signer3.addSpender(getSpenderPtr(utxo, true));
      }

      //spend 4 to a new address
      auto addr0 = assetWlt->getNewAddress();
      signer3.addRecipient(addr0->getRecipient(4 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());

      //spend 6 to a new address
      auto addr1 = assetWlt->getNewAddress();
      signer3.addRecipient(addr1->getRecipient(6 * COIN));
      addrVec.push_back(addr1->getPrefixedHash());

      //deal with change, no fee
      auto changeVal = total - 10 * COIN;
      auto recipientChange = std::make_shared<Signing::Recipient_P2PKH>(
         TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
      signer3.addRecipient(recipientChange);

      //sign, verify then broadcast 
      {
         auto lock = assetWlt->lockDecryptedContainer({});
         signer3.setFeed(assetFeed);
         signer3.sign();
      }

      auto rawTx = signer3.serializeSignedTx();
      DBTestUtils::ZcVector zcVec3;
      zcVec3.push_back(rawTx, 15000000);

      ZCHash2 = std::move(BtcUtils::getHash256(rawTx));
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
   EXPECT_EQ(zcledger1.getValue(), 27 * (int64_t)COIN);
   //EXPECT_EQ(zcledger1.getTxTime(), 14000000);
   EXPECT_TRUE(zcledger1.isOptInRBF());

   //second zc should be valid
   auto zcledger2 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash2);
   EXPECT_EQ(zcledger2.getValue(), -17 * (int64_t)COIN);
   //EXPECT_EQ(zcledger2.getTxTime(), 15000000);
   EXPECT_TRUE(zcledger2.isOptInRBF());

   //rbf the child
   {
      auto spendVal = 10 * COIN;
      Signing::Signer signer2;

      //instantiate resolver feed
      auto assetFeed =
         std::make_shared<Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getRBFTxOutList();

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getAmount();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getAmount();
         signer2.addSpender(getSpenderPtr(utxo, true));
      }

      //spend 6 to a new address
      auto addr0 = assetWlt->getNewAddress();
      signer2.addRecipient(addr0->getRecipient(6 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());


      if (total > spendVal) {
         //change addrE, 1 btc fee
         auto changeVal = 5 * COIN;
         auto recipientChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), changeVal);
         signer2.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      {
         auto lock = assetWlt->lockDecryptedContainer({});
         signer2.setFeed(assetFeed);
         signer2.sign();
      }
      EXPECT_TRUE(signer2.verify());

      auto rawTx = signer2.serializeSignedTx();
      DBTestUtils::ZcVector zcVec2;
      zcVec2.push_back(rawTx, 17000000);

      ZCHash3 = std::move(BtcUtils::getHash256(rawTx));
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
   EXPECT_EQ(zcledger3.getValue(), 27 * (int64_t)COIN);
   EXPECT_EQ(zcledger3.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(zcledger3.isOptInRBF());

   //second zc should be replaced
   try {
      DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash2);
      ASSERT_TRUE(false);
   } catch (const std::runtime_error& e) {
      EXPECT_EQ(e.what(), std::string{"no ledger for txhash"});
   }

   //third zc should be valid
   auto zcledger9 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash3);
   EXPECT_EQ(zcledger9.getValue(), -6 * (int64_t)COIN);
   EXPECT_EQ(zcledger9.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(zcledger9.isOptInRBF());

   //mine a new block
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 3);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   //check chain is 3 blocks longer
   EXPECT_EQ(TestUtils::getTopBlockHeightInDB(theBDMt_->bdm().get(), DB_SELECT::HEADERS), 6U);

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 200 * COIN);
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

   //check all zc are mined with 1 conf
   zcledger3 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash1);
   EXPECT_EQ(zcledger3.getValue(), 27 * (int64_t)COIN);
   EXPECT_EQ(zcledger3.getBlockNum(), 4U);
   EXPECT_FALSE(zcledger3.isOptInRBF());

   zcledger9 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash3);
   EXPECT_EQ(zcledger9.getValue(), -6 * (int64_t)COIN);
   EXPECT_EQ(zcledger9.getBlockNum(), 4U);
   EXPECT_FALSE(zcledger9.isOptInRBF());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode, ZC_InOut_SameBlock)
{
   BinaryData ZCHash1, ZCHash2, ZCHash3;

   //
   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

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
   auto ZC1 = TestUtils::getTx(2, 1); //block 2, tx 1
   auto ZChash1 = BtcUtils::getHash256(ZC1);

   auto ZC2 = TestUtils::getTx(2, 2); //block 2, tx 2
   auto ZChash2 = BtcUtils::getHash256(ZC2);

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
TEST_F(ZeroConfTests_Supernode, ZC_MineAfter1Block)
{
   auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
   feed->addPrivKey(TestChain::privKeyAddrB.getRef());
   feed->addPrivKey(TestChain::privKeyAddrC.getRef());
   feed->addPrivKey(TestChain::privKeyAddrD.getRef());

   ////
   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD
   };

   clients_->init();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   auto bdvID = DBTestUtils::registerBDV(clients_, Config::BitcoinSettings::getMagicBytes());

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDVReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);

   uint64_t balanceWlt;
   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrA)->getFullBalance();
   EXPECT_EQ(balanceWlt, 50 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrB)->getFullBalance();
   EXPECT_EQ(balanceWlt, 70 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrC)->getFullBalance();
   EXPECT_EQ(balanceWlt, 20 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrD)->getFullBalance();
   EXPECT_EQ(balanceWlt, 65 * COIN);

   //spend from B to C
   auto utxoVec = wlt->getSpendableTxOutListForValue();
   UTXO utxoA, utxoB;
   for (auto& utxo : utxoVec) {
      if (utxo.getRecipientScrAddr() == TestChain::scrAddrD) {
         utxoA.value_ = utxo.value_;
         utxoA.script_ = utxo.script_;
         utxoA.txHeight_ = utxo.txHeight_;
         utxoA.txIndex_ = utxo.txIndex_;
         utxoA.txOutIndex_ = utxo.txOutIndex_;
         utxoA.txHash_ = utxo.txHash_;
      } else if (utxo.getRecipientScrAddr() == TestChain::scrAddrB) {
         utxoB.value_ = utxo.value_;
         utxoB.script_ = utxo.script_;
         utxoB.txHeight_ = utxo.txHeight_;
         utxoB.txIndex_ = utxo.txIndex_;
         utxoB.txOutIndex_ = utxo.txOutIndex_;
         utxoB.txHash_ = utxo.txHash_;
      }
   }

   auto spenderA = std::make_shared<Signing::ScriptSpender>(utxoA);
   auto spenderB = std::make_shared<Signing::ScriptSpender>(utxoB);
   DBTestUtils::ZcVector zcVec;

   //spend from D to C
   {
      Signing::Signer signer;
      signer.addSpender(spenderA);

      auto recipient = std::make_shared<Signing::Recipient_P2PKH>(
         TestChain::scrAddrC.getSliceCopy(1, 20), utxoA.getAmount());
      signer.addRecipient(recipient);

      signer.setFeed(feed);
      signer.sign();
      signer.serializeSignedTx();
      zcVec.push_back(signer.serializeSignedTx(), 130000000, 0);
   }

   //spend from B to C
   {
      Signing::Signer signer;
      signer.addSpender(spenderB);

      auto recipient = std::make_shared<Signing::Recipient_P2PKH>(
         TestChain::scrAddrC.getSliceCopy(1, 20), utxoB.getAmount());
      signer.addRecipient(recipient);

      signer.setFeed(feed);
      signer.sign();
      zcVec.push_back(signer.serializeSignedTx(), 131000000, 1);
   }

   auto hash1 = zcVec.zcVec_[0].first.getThisHash();
   auto hash2 = zcVec.zcVec_[1].first.getThisHash();

   //broadcast
   DBTestUtils::pushNewZc(theBDMt_, zcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   //check balances
   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrA)->getFullBalance();
   EXPECT_EQ(balanceWlt, 50 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrB)->getFullBalance();
   EXPECT_EQ(balanceWlt, 50 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrC)->getFullBalance();
   EXPECT_EQ(balanceWlt, 45 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrD)->getFullBalance();
   EXPECT_EQ(balanceWlt, 60 * COIN);

   auto zc1 = bdvPtr->getTxByHash(hash1);
   auto zc2 = bdvPtr->getTxByHash(hash2);

   EXPECT_EQ(zc1.getTxHeight(), UINT32_MAX);
   EXPECT_EQ(zc2.getTxHeight(), UINT32_MAX);

   //mine 1 block
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   //check balances
   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrA)->getFullBalance();
   EXPECT_EQ(balanceWlt, 100 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrB)->getFullBalance();
   EXPECT_EQ(balanceWlt, 50 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrC)->getFullBalance();
   EXPECT_EQ(balanceWlt, 45 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrD)->getFullBalance();
   EXPECT_EQ(balanceWlt, 60 * COIN);

   auto zc3 = bdvPtr->getTxByHash(hash1);
   auto zc4 = bdvPtr->getTxByHash(hash2);

   EXPECT_EQ(zc3.getTxHeight(), 6U);
   EXPECT_EQ(zc4.getTxHeight(), UINT32_MAX);

   //mine last block
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrB, 1);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   //check balances
   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrA)->getFullBalance();
   EXPECT_EQ(balanceWlt, 100 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrB)->getFullBalance();
   EXPECT_EQ(balanceWlt, 100 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrC)->getFullBalance();
   EXPECT_EQ(balanceWlt, 45 * COIN);

   balanceWlt = wlt->getScrAddrObjByKey(TestChain::scrAddrD)->getFullBalance();
   EXPECT_EQ(balanceWlt, 60 * COIN);

   auto zc5 = bdvPtr->getTxByHash(hash1);
   auto zc6 = bdvPtr->getTxByHash(hash2);

   EXPECT_EQ(zc5.getTxHeight(), 6U);
   EXPECT_EQ(zc6.getTxHeight(), 7U);
   EXPECT_GE(theBDMt_->bdm()->zeroConfCont()->getMergeCount(), 1U);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class ZeroConfTests_Supernode_WebSocket : public ::testing::Test
{
protected:
   void initBDM(void)
   {
      theBDMt_ = new BlockDataManagerThread();
      iface_ = theBDMt_->bdm()->getIFace();

      nodePtr_ = std::dynamic_pointer_cast<NodeUnitTest>(
         Config::NetworkSettings::bitcoinNodes().first);

      rpcNode_ = std::dynamic_pointer_cast<NodeRPC_UnitTest>(
         Config::NetworkSettings::rpcNode());
      nodePtr_->setBDM(theBDMt_->bdm());
      hexMagicBytes = Config::BitcoinSettings::getMagicBytes().toHexStr();
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      zeros_ = READHEX("00000000");

      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);

      FileUtils::createDirectory(blkdir_ / "blocks");
      FileUtils::createDirectory(homedir_);
      FileUtils::createDirectory(ldbdir_);

      // Put the first 5 blocks into the blkdir
      blk0dat_ = FileUtils::getBlkFilename(blkdir_ / "blocks", 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);

      startupBIP151CTX();
      startupBIP150CTX(4);

      WebSocketServer::init();
      Config::DBSettings::setServiceType(SERVICE_UNITTEST_WITHWS);
      Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--db-type=DB_SUPER",
         "--thread-count=3",
         "--public"},
         Config::ProcessType::DB);

      //setup auth peers for server and client
      authPeersPassLbd_ = [](const std::set<Wallets::EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("authpeerpass"), true };
      };

      auto createWltLbd = []()->std::unique_ptr<Passphrase::Params>
      {
         return std::make_unique<Passphrase::Params>(
            1ms, 0, SecureBinaryData{});
      };

      Wallets::AuthorizedPeers::createWallet({
         homedir_ / SERVER_AUTH_PEER_FILENAME, {createWltLbd}});
      Wallets::AuthorizedPeers serverPeers(
         {homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});

      Wallets::AuthorizedPeers::createWallet({
         homedir_ / CLIENT_AUTH_PEER_FILENAME, {createWltLbd}});
      Wallets::AuthorizedPeers clientPeers(
         {homedir_ / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_});

      //share public keys between client and server
      clientPeers.addPeer(
         serverPeers.getOwnPublicKey(),
         {std::string{"127.0.0.1:" + Config::NetworkSettings::dbPort()}},\
         {}, true);

      wallet1id = "wallet1";
      initBDM();
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      shutdownBIP151CTX();
      WebSocketServer::shutdown();
      WebSocketServer::waitOnShutdown();

      EXPECT_EQ(theBDMt_->bdm()->zeroConfCont()->getMatcherMapSize(), 0U);
      theBDMt_->shutdown();
      delete theBDMt_;
      theBDMt_ = nullptr;

      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);

      Config::reset();
      CLEANUP_ALL_TIMERS();
   }

   BlockDataManagerThread *theBDMt_;
   Passphrase::UnlockFunc authPeersPassLbd_;
   LMDBBlockDatabase* iface_;
   BinaryData zeros_;

   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};
   std::filesystem::path blk0dat_;

   std::string wallet1id;

   std::shared_ptr<NodeUnitTest> nodePtr_;
   std::shared_ptr<NodeRPC_UnitTest> rpcNode_;
   std::string hexMagicBytes;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, ZcUpdate)
{
   /*
   Some sigs in static test chain are borked. P2SH scripts are borked too. This
   test plucks transactions from the static chain to push as ZC. Skip sig checks
   on the unit test mock P2P node to avoid faililng the test.
   */
   nodePtr_->checkSigs(false);

   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", Config::NetworkSettings::dbPort(),
      std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
         Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
      true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey, true);
   bdvObj->connectToRemote();
   bdvObj->registerWithDB(hexMagicBytes);

   //go online
   bdvObj->goOnline();
   pCallback->waitOnSignal(BDMAction_Ready);

   std::vector<std::string> walletRegIDs {"wallet1"};

   auto wallet1 = bdvObj->getWalletObj("wallet1");
   wallet1.registerAddresses(scrAddrVec, false);

   //wait on registration ack
   pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

   //get wallets delegate
   auto del1_prom = std::make_shared<std::promise<AsyncClient::LedgerDelegate>>();
   auto del1_fut = del1_prom->get_future();
   auto del1_get = [del1_prom](
      ReturnMessage<AsyncClient::LedgerDelegate> delegate)->void
   {
      del1_prom->set_value(std::move(delegate.get()));
   };
   wallet1.getLedgerDelegate(del1_get);
   auto main_delegate = del1_fut.get();

   auto ledger_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger_fut = ledger_prom->get_future();
   auto ledger_get =
      [ledger_prom](
         ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger_prom->set_value(std::move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger_get);
   auto main_ledger = ledger_fut.get();

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage = main_ledger[0];
   EXPECT_EQ(historyPage.size(), 2ULL);

   EXPECT_EQ(historyPage[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage[0].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage[1].getTxOutIndex(), 0U);

   //add the 2 zc
   auto ZC1 = TestUtils::getTx(2, 1); //block 2, tx 1
   auto ZChash1 = BtcUtils::getHash256(ZC1);

   auto ZC2 = TestUtils::getTx(2, 2); //block 2, tx 2
   auto ZChash2 = BtcUtils::getHash256(ZC2);

   bdvObj->broadcastZC({ZC1, ZC2});

   {
      std::set<BinaryData> zcHashes{ ZChash1, ZChash2 };
      pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), zcHashes);
   }

   //get the new ledgers
   auto ledger2_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger2_fut = ledger2_prom->get_future();
   auto ledger2_get =
      [ledger2_prom](ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger2_prom->set_value(move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger2_get);
   auto main_ledger2 = move(ledger2_fut.get());

   //check ledgers
   EXPECT_EQ(main_ledger2.size(), 1ULL);
   const auto& historyPage2 = main_ledger2[0];
   EXPECT_EQ(historyPage2.size(), 4ULL);

   EXPECT_EQ(historyPage2[3].getValue(), -20 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[3].getBlockHeight(), UINT32_MAX);
   EXPECT_EQ(historyPage2[3].getTxOutIndex(), 1U);

   EXPECT_EQ(historyPage2[2].getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[2].getBlockHeight(), UINT32_MAX);
   EXPECT_EQ(historyPage2[2].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage2[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage2[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage2[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage2[0].getTxOutIndex(), 0U);

   //tx cache testing
   //grab ZC1 from async client
   auto zc_prom1 = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
   auto zc_fut1 = zc_prom1->get_future();
   auto zc_get1 =
      [zc_prom1](ReturnMessage<AsyncClient::TxBatchResult> txObj)->void
   {
      auto tx = txObj.get();
      zc_prom1->set_value(move(tx));
   };

   bdvObj->getTxsByHash({ZChash1}, zc_get1);
   auto zcs_obj1 = zc_fut1.get();
   ASSERT_EQ(zcs_obj1.size(), 1ULL);

   EXPECT_EQ(ZChash1, zcs_obj1.begin()->first);
   const auto& zc_obj1 = zcs_obj1.at(ZChash1);
   EXPECT_EQ(zc_obj1->getTxHeight(), UINT32_MAX);

   //grab both zc from async client
   auto zc_prom2 = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
   auto zc_fut2 = zc_prom2->get_future();
   auto zc_get2 =
      [zc_prom2](ReturnMessage<AsyncClient::TxBatchResult> txObj)->void
   {
      auto txVec = txObj.get();
      zc_prom2->set_value(move(txVec));
   };

   std::set<BinaryData> bothZC = { ZChash1, ZChash2 };
   bdvObj->getTxsByHash(bothZC, zc_get2);
   auto zc_obj2 = zc_fut2.get();

   ASSERT_EQ(zc_obj2.size(), 2ULL);

   auto iterZc1 = zc_obj2.find(ZChash1);
   ASSERT_NE(iterZc1, zc_obj2.end());
   ASSERT_NE(iterZc1->second, nullptr);
   EXPECT_EQ(ZChash1, iterZc1->second->getThisHash());
   EXPECT_EQ(iterZc1->second->getTxHeight(), UINT32_MAX);

   auto iterZc2 = zc_obj2.find(ZChash2);
   ASSERT_NE(iterZc2, zc_obj2.end());
   ASSERT_NE(iterZc2->second, nullptr);
   EXPECT_EQ(ZChash2, iterZc2->second->getThisHash());
   EXPECT_EQ(iterZc2->second->getTxHeight(), UINT32_MAX);

   //push an extra block
   TestUtils::appendBlocks({ "2" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   pCallback->waitOnSignal(BDMAction_NewBlock);

   //get the new ledgers
   auto ledger3_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger3_fut = ledger3_prom->get_future();
   auto ledger3_get =
      [ledger3_prom](ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger3_prom->set_value(move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger3_get);
   main_ledger = move(ledger3_fut.get());

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage3 = main_ledger[0];
   EXPECT_EQ(historyPage3.size(), 5ULL);

   EXPECT_EQ(historyPage3[4].getValue(), -20 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[4].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[4].getTxOutIndex(), 2U);

   EXPECT_EQ(historyPage3[3].getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[3].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[3].getTxOutIndex(), 1U);

   EXPECT_EQ(historyPage3[2].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[2].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[2].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage3[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage3[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage3[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage3[0].getTxOutIndex(), 0U);

   //grab both zc from async client
   auto zc_prom4 = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
   auto zc_fut4 = zc_prom4->get_future();
   auto zc_get4 =
      [zc_prom4](ReturnMessage<AsyncClient::TxBatchResult> txObj)->void
   {
      auto&& txVec = txObj.get();
      zc_prom4->set_value(move(txVec));
   };
   bdvObj->getTxsByHash(bothZC, zc_get4);
   auto zc_obj4 = zc_fut4.get();

   ASSERT_EQ(zc_obj4.size(), 2ULL);

   auto iterZc3 = zc_obj4.find(ZChash1);
   ASSERT_NE(iterZc3, zc_obj4.end());
   ASSERT_NE(iterZc3->second, nullptr);
   EXPECT_EQ(ZChash1, iterZc3->second->getThisHash());
   EXPECT_EQ(iterZc3->second->getTxHeight(), 2U);

   auto iterZc4 = zc_obj4.find(ZChash2);
   ASSERT_NE(iterZc4, zc_obj4.end());
   ASSERT_NE(iterZc4->second, nullptr);
   EXPECT_EQ(ZChash2, iterZc4->second->getThisHash());
   EXPECT_EQ(iterZc4->second->getTxHeight(), 2U);

   //disconnect
   bdvObj->unregisterFromDB();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, ZcUpdate_RPC)
{
   /*
   Some sigs in static test chain are borked. P2SH scripts are borked too. This
   test plucks transactions from the static chain to push as ZC. Skip sig checks
   on the unit test mock P2P node to avoid faililng the test.
   */
   nodePtr_->checkSigs(false);

   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", Config::NetworkSettings::dbPort(),
      std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
         Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
      true,
      pCallback);
   bdvObj->addPublicKey(serverPubkey, true);
   bdvObj->connectToRemote();
   bdvObj->registerWithDB(hexMagicBytes);

   //go online
   bdvObj->goOnline();
   pCallback->waitOnSignal(BDMAction_Ready);
   std::vector<std::string> walletRegIDs {"wallet1"};

   auto wallet1 = bdvObj->getWalletObj("wallet1");
   wallet1.registerAddresses(scrAddrVec, false);

   //wait on registration ack
   pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

   //get wallets delegate
   auto del1_prom = std::make_shared<std::promise<AsyncClient::LedgerDelegate>>();
   auto del1_fut = del1_prom->get_future();
   auto del1_get = [del1_prom](
      ReturnMessage<AsyncClient::LedgerDelegate> delegate)->void
   {
      del1_prom->set_value(std::move(delegate.get()));
   };
   wallet1.getLedgerDelegate(del1_get);
   auto main_delegate = del1_fut.get();

   auto ledger_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger_fut = ledger_prom->get_future();
   auto ledger_get =
      [ledger_prom](
         ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger_prom->set_value(std::move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger_get);
   auto main_ledger = ledger_fut.get();

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage = main_ledger[0];
   EXPECT_EQ(historyPage.size(), 2ULL);

   EXPECT_EQ(historyPage[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage[0].getTxOutIndex(), 0U);

   //add the 2 zc
   auto ZC1 = TestUtils::getTx(2, 1); //block 2, tx 1
   auto ZChash1 = BtcUtils::getHash256(ZC1);

   auto ZC2 = TestUtils::getTx(2, 2); //block 2, tx 2
   auto ZChash2 = BtcUtils::getHash256(ZC2);

   bdvObj->broadcastThroughRPC(ZC1);
   bdvObj->broadcastThroughRPC(ZC2);
   pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), {ZChash1});
   pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), {ZChash2});

   //get the new ledgers
   auto ledger2_prom =
   std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger2_fut = ledger2_prom->get_future();
   auto ledger2_get =
      [ledger2_prom](ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger2_prom->set_value(move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger2_get);
   main_ledger = move(ledger2_fut.get());

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage2 = main_ledger[0];
   EXPECT_EQ(historyPage2.size(), 4ULL);

   EXPECT_EQ(historyPage2[3].getValue(), -20 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[3].getBlockHeight(), UINT32_MAX);
   EXPECT_EQ(historyPage2[3].getTxOutIndex(), 1U);

   EXPECT_EQ(historyPage2[2].getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[2].getBlockHeight(), UINT32_MAX);
   EXPECT_EQ(historyPage2[2].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage2[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage2[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage2[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage2[0].getTxOutIndex(), 0U);

   /*tx cache coverage*/
   //grab both zc from async client
   auto zc_prom2 = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
   auto zc_fut2 = zc_prom2->get_future();
   auto zc_get2 =
      [zc_prom2](ReturnMessage<AsyncClient::TxBatchResult> txObj)->void
   {
      auto&& txVec = txObj.get();
      zc_prom2->set_value(move(txVec));
   };

   std::set<BinaryData> bothZC = { ZChash1, ZChash2 };
   bdvObj->getTxsByHash(bothZC, zc_get2);
   auto zc_obj2 = zc_fut2.get();
   ASSERT_EQ(zc_obj2.size(), 2ULL);

   auto iterZc1 = zc_obj2.find(ZChash1);
   ASSERT_NE(iterZc1, zc_obj2.end());
   ASSERT_NE(iterZc1->second, nullptr);
   EXPECT_EQ(ZChash1, iterZc1->second->getThisHash());
   EXPECT_EQ(iterZc1->second->getTxHeight(), UINT32_MAX);

   auto iterZc2 = zc_obj2.find(ZChash2);
   ASSERT_NE(iterZc2, zc_obj2.end());
   ASSERT_NE(iterZc2->second, nullptr);
   EXPECT_EQ(ZChash2, iterZc2->second->getThisHash());
   EXPECT_EQ(iterZc2->second->getTxHeight(), UINT32_MAX);

   //push an extra block
   TestUtils::appendBlocks({ "2" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   pCallback->waitOnSignal(BDMAction_NewBlock);

   //get the new ledgers
   auto ledger3_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger3_fut = ledger3_prom->get_future();
   auto ledger3_get =
      [ledger3_prom](ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger3_prom->set_value(move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger3_get);
   main_ledger = move(ledger3_fut.get());

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage3 = main_ledger[0];
   EXPECT_EQ(historyPage3.size(), 5ULL);

   EXPECT_EQ(historyPage3[4].getValue(), -20 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[4].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[4].getTxOutIndex(), 2U);

   EXPECT_EQ(historyPage3[3].getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[3].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[3].getTxOutIndex(), 1U);

   EXPECT_EQ(historyPage3[2].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[2].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[2].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage3[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage3[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage3[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage3[0].getTxOutIndex(), 0U);


   //grab both zc from async client
   auto zc_prom4 = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
   auto zc_fut4 = zc_prom4->get_future();
   auto zc_get4 =
      [zc_prom4](ReturnMessage<AsyncClient::TxBatchResult> txObj)->void
   {
      auto&& txVec = txObj.get();
      zc_prom4->set_value(move(txVec));
   };

   bdvObj->getTxsByHash(bothZC, zc_get4);
   auto zc_obj4 = zc_fut4.get();

   ASSERT_EQ(zc_obj4.size(), 2ULL);
   auto iterZc3 = zc_obj4.find(ZChash1);
   ASSERT_NE(iterZc3, zc_obj4.end());
   ASSERT_NE(iterZc3->second, nullptr);
   EXPECT_EQ(ZChash1, iterZc3->second->getThisHash());
   EXPECT_EQ(iterZc3->second->getTxHeight(), 2U);

   auto iterZc4 = zc_obj4.find(ZChash2);
   ASSERT_NE(iterZc4, zc_obj4.end());
   ASSERT_NE(iterZc4->second, nullptr);
   EXPECT_EQ(ZChash2, iterZc4->second->getThisHash());
   EXPECT_EQ(iterZc4->second->getTxHeight(), 2U);

   //disconnect
   bdvObj->unregisterFromDB();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, ZcUpdate_RPC_Fallback)
{
   /*
   Some sigs in static test chain are borked. P2SH scripts are borked too. This
   test plucks transactions from the static chain to push as ZC. Skip sig checks
   on the unit test mock P2P node to avoid faililng the test.
   */
   nodePtr_->checkSigs(false);

   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", Config::NetworkSettings::dbPort(),
      std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
         Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
      true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey, true);
   bdvObj->connectToRemote();
   bdvObj->registerWithDB(hexMagicBytes);

   //go online
   bdvObj->goOnline();
   pCallback->waitOnSignal(BDMAction_Ready);

   std::vector<std::string> walletRegIDs {"wallet1"};
   auto wallet1 = bdvObj->getWalletObj("wallet1");
   wallet1.registerAddresses(scrAddrVec, false);

   //wait on registration ack
   pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

   //get wallets delegate
   auto del1_prom = std::make_shared<std::promise<AsyncClient::LedgerDelegate>>();
   auto del1_fut = del1_prom->get_future();
   auto del1_get = [del1_prom](
      ReturnMessage<AsyncClient::LedgerDelegate> delegate)->void
   {
      del1_prom->set_value(std::move(delegate.get()));
   };
   wallet1.getLedgerDelegate(del1_get);
   auto main_delegate = del1_fut.get();

   auto ledger_prom =
   std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger_fut = ledger_prom->get_future();
   auto ledger_get =
      [ledger_prom](
         ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger_prom->set_value(std::move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger_get);
   auto main_ledger = ledger_fut.get();

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage = main_ledger[0];
   EXPECT_EQ(historyPage.size(), 2ULL);

   EXPECT_EQ(historyPage[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage[0].getTxOutIndex(), 0U);

   //add the 2 zc
   auto ZC1 = TestUtils::getTx(2, 1); //block 2, tx 1
   auto ZChash1 = BtcUtils::getHash256(ZC1);

   auto ZC2 = TestUtils::getTx(2, 2); //block 2, tx 2
   auto ZChash2 = BtcUtils::getHash256(ZC2);

   //both these zc will be skipped by the p2p broadcast interface,
   //should trigger a RPC broadcast
   nodePtr_->skipZc(2);
   bdvObj->broadcastZC({ZC1});
   bdvObj->broadcastZC({ZC2});
   pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), {ZChash1});
   pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), {ZChash2});

   //get the new ledgers
   auto ledger2_prom =
   std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger2_fut = ledger2_prom->get_future();
   auto ledger2_get =
      [ledger2_prom](ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger2_prom->set_value(std::move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger2_get);
   main_ledger = std::move(ledger2_fut.get());

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage2 = main_ledger[0];
   EXPECT_EQ(historyPage2.size(), 4ULL);

   EXPECT_EQ(historyPage2[3].getValue(), -20 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[3].getBlockHeight(), UINT32_MAX);
   EXPECT_EQ(historyPage2[3].getTxOutIndex(), 3U);

   EXPECT_EQ(historyPage2[2].getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[2].getBlockHeight(), UINT32_MAX);
   EXPECT_EQ(historyPage2[2].getTxOutIndex(), 2U);

   EXPECT_EQ(historyPage2[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage2[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage2[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage2[0].getTxOutIndex(), 0U);

   //grab both zc from async client
   auto zc_prom2 = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
   auto zc_fut2 = zc_prom2->get_future();
   auto zc_get2 =
      [zc_prom2](ReturnMessage<AsyncClient::TxBatchResult> txObj)->void
   {
      auto&& txVec = txObj.get();
      zc_prom2->set_value(std::move(txVec));
   };

   std::set<BinaryData> bothZC = { ZChash1, ZChash2 };
   bdvObj->getTxsByHash(bothZC, zc_get2);
   auto zc_obj2 = zc_fut2.get();

   ASSERT_EQ(zc_obj2.size(), 2ULL);

   auto iterZc1 = zc_obj2.find(ZChash1);
   ASSERT_NE(iterZc1, zc_obj2.end());
   ASSERT_NE(iterZc1->second, nullptr);
   EXPECT_EQ(ZChash1, iterZc1->second->getThisHash());
   EXPECT_EQ(iterZc1->second->getTxHeight(), UINT32_MAX);

   auto iterZc2 = zc_obj2.find(ZChash2);
   ASSERT_NE(iterZc2, zc_obj2.end());
   ASSERT_NE(iterZc2->second, nullptr);
   EXPECT_EQ(ZChash2, iterZc2->second->getThisHash());
   EXPECT_EQ(iterZc2->second->getTxHeight(), UINT32_MAX);

   //push an extra block
   TestUtils::appendBlocks({ "2" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   pCallback->waitOnSignal(BDMAction_NewBlock);

   //get the new ledgers
   auto ledger3_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger3_fut = ledger3_prom->get_future();
   auto ledger3_get =
      [ledger3_prom](ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger3_prom->set_value(std::move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger3_get);
   main_ledger = std::move(ledger3_fut.get());

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage3 = main_ledger[0];
   EXPECT_EQ(historyPage3.size(), 5ULL);

   EXPECT_EQ(historyPage3[4].getValue(), -20 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[4].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[4].getTxOutIndex(), 2U);

   EXPECT_EQ(historyPage3[3].getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[3].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[3].getTxOutIndex(), 1U);

   EXPECT_EQ(historyPage3[2].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[2].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[2].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage3[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage3[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage3[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage3[0].getTxOutIndex(), 0U);

   //grab both zc from async client
   auto zc_prom4 = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
   auto zc_fut4 = zc_prom4->get_future();
   auto zc_get4 =
      [zc_prom4](ReturnMessage<AsyncClient::TxBatchResult> txObj)->void
   {
      auto txVec = txObj.get();
      zc_prom4->set_value(move(txVec));
   };

   bdvObj->getTxsByHash(bothZC, zc_get4);
   auto zc_obj4 = zc_fut4.get();

   ASSERT_EQ(zc_obj4.size(), 2ULL);

   auto iterZc3 = zc_obj4.find(ZChash1);
   ASSERT_NE(iterZc3, zc_obj4.end());
   ASSERT_NE(iterZc3->second, nullptr);
   EXPECT_EQ(ZChash1, iterZc3->second->getThisHash());
   EXPECT_EQ(iterZc3->second->getTxHeight(), 2U);

   auto iterZc4 = zc_obj4.find(ZChash2);
   ASSERT_NE(iterZc4, zc_obj4.end());
   ASSERT_NE(iterZc4->second, nullptr);
   EXPECT_EQ(ZChash2, iterZc4->second->getThisHash());
   EXPECT_EQ(iterZc4->second->getTxHeight(), 2U);

   //disconnect
   bdvObj->unregisterFromDB();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, ZcUpdate_RPC_Fallback_SingleBatch)
{
   /*
   Some sigs in static test chain are borked. P2SH scripts are borked too. This
   test plucks transactions from the static chain to push as ZC. Skip sig checks
   on the unit test mock P2P node to avoid faililng the test.
   */
   nodePtr_->checkSigs(false);

   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", Config::NetworkSettings::dbPort(),
      std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
         Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
      true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey, true);
   bdvObj->connectToRemote();
   bdvObj->registerWithDB(hexMagicBytes);

   //go online
   bdvObj->goOnline();
   pCallback->waitOnSignal(BDMAction_Ready);
   std::vector<std::string> walletRegIDs {"wallet1"};

   auto wallet1 = bdvObj->getWalletObj("wallet1");
   wallet1.registerAddresses(scrAddrVec, false);

   //wait on registration ack
   pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

   //get wallets delegate
   auto del1_prom = std::make_shared<std::promise<AsyncClient::LedgerDelegate>>();
   auto del1_fut = del1_prom->get_future();
   auto del1_get = [del1_prom](
      ReturnMessage<AsyncClient::LedgerDelegate> delegate)->void
   {
      del1_prom->set_value(std::move(delegate.get()));
   };
   wallet1.getLedgerDelegate(del1_get);
   auto&& main_delegate = del1_fut.get();

   auto ledger_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger_fut = ledger_prom->get_future();
   auto ledger_get =
      [ledger_prom](
         ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger_prom->set_value(std::move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger_get);
   auto main_ledger = ledger_fut.get();

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage = main_ledger[0];
   EXPECT_EQ(historyPage.size(), 2ULL);

   EXPECT_EQ(historyPage[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage[0].getTxOutIndex(), 0U);

   //add the 2 zc
   auto ZC1 = TestUtils::getTx(2, 1); //block 2, tx 1
   auto ZChash1 = BtcUtils::getHash256(ZC1);

   auto ZC2 = TestUtils::getTx(2, 2); //block 2, tx 2
   auto ZChash2 = BtcUtils::getHash256(ZC2);

   //both these zc will be skipped by the p2p broadcast interface,
   //should trigger a RPC broadcast
   nodePtr_->skipZc(2);
   std::vector<BinaryData> zcVec = {ZC1, ZC2};
   bdvObj->broadcastZC(zcVec);
   pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), {ZChash1, ZChash2});

   //get the new ledgers
   auto ledger2_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger2_fut = ledger2_prom->get_future();
   auto ledger2_get =
      [ledger2_prom](ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger2_prom->set_value(std::move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger2_get);
   main_ledger = std::move(ledger2_fut.get());

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage2 = main_ledger[0];
   EXPECT_EQ(historyPage2.size(), 4ULL);

   EXPECT_EQ(historyPage2[3].getValue(), -20 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[3].getBlockHeight(), UINT32_MAX);
   EXPECT_EQ(historyPage2[3].getTxOutIndex(), 3U);

   EXPECT_EQ(historyPage2[2].getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[2].getBlockHeight(), UINT32_MAX);
   EXPECT_EQ(historyPage2[2].getTxOutIndex(), 2U);

   EXPECT_EQ(historyPage2[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage2[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage2[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage2[0].getTxOutIndex(), 0U);

   //tx cache testing
   //grab both zc from async client
   auto zc_prom2 = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
   auto zc_fut2 = zc_prom2->get_future();
   auto zc_get2 =
      [zc_prom2](ReturnMessage<AsyncClient::TxBatchResult> txObj)->void
   {
      auto&& txVec = txObj.get();
      zc_prom2->set_value(move(txVec));
   };

   std::set<BinaryData> bothZC = { ZChash1, ZChash2 };
   bdvObj->getTxsByHash(bothZC, zc_get2);
   auto zc_obj2 = zc_fut2.get();

   ASSERT_EQ(zc_obj2.size(), 2ULL);

   auto iterZc1 = zc_obj2.find(ZChash1);
   ASSERT_NE(iterZc1, zc_obj2.end());
   ASSERT_NE(iterZc1->second, nullptr);
   EXPECT_EQ(ZChash1, iterZc1->second->getThisHash());
   EXPECT_EQ(iterZc1->second->getTxHeight(), UINT32_MAX);

   auto iterZc2 = zc_obj2.find(ZChash2);
   ASSERT_NE(iterZc2, zc_obj2.end());
   ASSERT_NE(iterZc2->second, nullptr);
   EXPECT_EQ(ZChash2, iterZc2->second->getThisHash());
   EXPECT_EQ(iterZc2->second->getTxHeight(), UINT32_MAX);

   //push an extra block
   TestUtils::appendBlocks({ "2" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   pCallback->waitOnSignal(BDMAction_NewBlock);

   //get the new ledgers
   auto ledger3_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger3_fut = ledger3_prom->get_future();
   auto ledger3_get =
      [ledger3_prom](ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger3_prom->set_value(move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger3_get);
   main_ledger = move(ledger3_fut.get());

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage3 = main_ledger[0];
   EXPECT_EQ(historyPage3.size(), 5ULL);

   EXPECT_EQ(historyPage3[4].getValue(), -20 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[4].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[4].getTxOutIndex(), 2U);

   EXPECT_EQ(historyPage3[3].getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[3].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[3].getTxOutIndex(), 1U);

   EXPECT_EQ(historyPage3[2].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[2].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[2].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage3[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage3[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage3[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage3[0].getTxOutIndex(), 0U);


   //grab both zc from async client
   auto zc_prom4 = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
   auto zc_fut4 = zc_prom4->get_future();
   auto zc_get4 =
      [zc_prom4](ReturnMessage<AsyncClient::TxBatchResult> txObj)->void
   {
      auto txVec = txObj.get();
      zc_prom4->set_value(move(txVec));
   };

   bdvObj->getTxsByHash(bothZC, zc_get4);
   auto zc_obj4 = zc_fut4.get();

   ASSERT_EQ(zc_obj4.size(), 2ULL);

   auto iterZc3 = zc_obj4.find(ZChash1);
   ASSERT_NE(iterZc3, zc_obj4.end());
   ASSERT_NE(iterZc3->second, nullptr);
   EXPECT_EQ(ZChash1, iterZc3->second->getThisHash());
   EXPECT_EQ(iterZc3->second->getTxHeight(), 2U);

   auto iterZc4 = zc_obj4.find(ZChash2);
   ASSERT_NE(iterZc4, zc_obj4.end());
   ASSERT_NE(iterZc4->second, nullptr);
   EXPECT_EQ(ZChash2, iterZc4->second->getThisHash());
   EXPECT_EQ(iterZc4->second->getTxHeight(), 2U);

   //disconnect
   bdvObj->unregisterFromDB();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, ZcUpdate_AlreadyInMempool)
{
   /*
   Some sigs in static test chain are borked. P2SH scripts are borked too. This
   test plucks transactions from the static chain to push as ZC. Skip sig checks
   on the unit test mock P2P node to avoid faililng the test.
   */
   nodePtr_->checkSigs(false);

   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", Config::NetworkSettings::dbPort(),
      std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
         Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
      true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey, true);
   bdvObj->connectToRemote();
   bdvObj->registerWithDB(hexMagicBytes);

   //go online
   bdvObj->goOnline();
   pCallback->waitOnSignal(BDMAction_Ready);
   std::vector<std::string> walletRegIDs{"wallet1"};

   auto wallet1 = bdvObj->getWalletObj("wallet1");
   wallet1.registerAddresses(scrAddrVec, false);

   //wait on registration ack
   pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

   //get wallets delegate
   auto del1_prom = std::make_shared<std::promise<AsyncClient::LedgerDelegate>>();
   auto del1_fut = del1_prom->get_future();
   auto del1_get = [del1_prom](
      ReturnMessage<AsyncClient::LedgerDelegate> delegate)->void
   {
      del1_prom->set_value(std::move(delegate.get()));
   };
   wallet1.getLedgerDelegate(del1_get);
   auto main_delegate = del1_fut.get();

   auto ledger_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger_fut = ledger_prom->get_future();
   auto ledger_get =
      [ledger_prom](
         ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger_prom->set_value(std::move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger_get);
   auto main_ledger = ledger_fut.get();

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage = main_ledger[0];
   EXPECT_EQ(historyPage.size(), 2ULL);

   EXPECT_EQ(historyPage[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage[0].getTxOutIndex(), 0U);

   //add the 2 zc
   auto ZC1 = TestUtils::getTx(2, 1); //block 2, tx 1
   auto ZChash1 = BtcUtils::getHash256(ZC1);

   auto ZC2 = TestUtils::getTx(2, 2); //block 2, tx 2
   auto ZChash2 = BtcUtils::getHash256(ZC2);

   //pushZC
   bdvObj->broadcastZC({ZC1});
   bdvObj->broadcastZC({ZC2});
   pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), {ZChash1});
   pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), {ZChash2});

   //push them again, should get already in mempool error
   bdvObj->broadcastZC({ZC1});
   bdvObj->broadcastZC({ZC2});

   pCallback->waitOnError(
      ZChash1, ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);
   pCallback->waitOnError(
      ZChash2, ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);

   //get the new ledgers
   auto ledger2_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger2_fut = ledger2_prom->get_future();
   auto ledger2_get =
      [ledger2_prom](ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger2_prom->set_value(std::move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger2_get);
   main_ledger = std::move(ledger2_fut.get());

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage2 = main_ledger[0];
   EXPECT_EQ(historyPage2.size(), 4ULL);

   EXPECT_EQ(historyPage2[3].getValue(), -20 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[3].getBlockHeight(), UINT32_MAX);
   EXPECT_EQ(historyPage2[3].getTxOutIndex(), 1U);

   EXPECT_EQ(historyPage2[2].getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[2].getBlockHeight(), UINT32_MAX);
   EXPECT_EQ(historyPage2[2].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage2[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage2[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage2[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage2[0].getTxOutIndex(), 0U);

   //grab both zc from async client
   auto zc_prom2 = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
   auto zc_fut2 = zc_prom2->get_future();
   auto zc_get2 =
      [zc_prom2](ReturnMessage<AsyncClient::TxBatchResult> txObj)->void
   {
      auto txVec = txObj.get();
      zc_prom2->set_value(move(txVec));
   };

   std::set<BinaryData> bothZC = { ZChash1, ZChash2 };
   bdvObj->getTxsByHash(bothZC, zc_get2);
   auto zc_obj2 = zc_fut2.get();

   ASSERT_EQ(zc_obj2.size(), 2ULL);

   auto iterZc1 = zc_obj2.find(ZChash1);
   ASSERT_NE(iterZc1, zc_obj2.end());
   ASSERT_NE(iterZc1->second, nullptr);
   EXPECT_EQ(ZChash1, iterZc1->second->getThisHash());
   EXPECT_EQ(iterZc1->second->getTxHeight(), UINT32_MAX);

   auto iterZc2 = zc_obj2.find(ZChash2);
   ASSERT_NE(iterZc2, zc_obj2.end());
   ASSERT_NE(iterZc2->second, nullptr);
   EXPECT_EQ(ZChash2, iterZc2->second->getThisHash());
   EXPECT_EQ(iterZc2->second->getTxHeight(), UINT32_MAX);

   //push an extra block
   TestUtils::appendBlocks({ "2" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   pCallback->waitOnSignal(BDMAction_NewBlock);

   //get the new ledgers
   auto ledger3_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger3_fut = ledger3_prom->get_future();
   auto ledger3_get =
      [ledger3_prom](ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger3_prom->set_value(move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger3_get);
   main_ledger = std::move(ledger3_fut.get());

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage3 = main_ledger[0];
   EXPECT_EQ(historyPage3.size(), 5ULL);

   EXPECT_EQ(historyPage3[4].getValue(), -20 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[4].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[4].getTxOutIndex(), 2U);

   EXPECT_EQ(historyPage3[3].getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[3].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[3].getTxOutIndex(), 1U);

   EXPECT_EQ(historyPage3[2].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[2].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[2].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage3[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage3[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage3[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage3[0].getTxOutIndex(), 0U);

   //grab both zc from async client
   auto zc_prom4 = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
   auto zc_fut4 = zc_prom4->get_future();
   auto zc_get4 =
      [zc_prom4](ReturnMessage<AsyncClient::TxBatchResult> txObj)->void
   {
      auto txVec = txObj.get();
      zc_prom4->set_value(move(txVec));
   };

   bdvObj->getTxsByHash(bothZC, zc_get4);
   auto zc_obj4 = zc_fut4.get();

   ASSERT_EQ(zc_obj4.size(), 2ULL);

   auto iterZc3 = zc_obj4.find(ZChash1);
   ASSERT_NE(iterZc3, zc_obj4.end());
   ASSERT_NE(iterZc3->second, nullptr);
   EXPECT_EQ(ZChash1, iterZc3->second->getThisHash());
   EXPECT_EQ(iterZc3->second->getTxHeight(), 2U);

   auto iterZc4 = zc_obj4.find(ZChash2);
   ASSERT_NE(iterZc4, zc_obj4.end());
   ASSERT_NE(iterZc4->second, nullptr);
   EXPECT_EQ(ZChash2, iterZc4->second->getThisHash());
   EXPECT_EQ(iterZc4->second->getTxHeight(), 2U);

   //disconnect
   bdvObj->unregisterFromDB();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, ZcUpdate_AlreadyInMempool_Batched)
{
   /*
   Some sigs in static test chain are borked. P2SH scripts are borked too. This
   test plucks transactions from the static chain to push as ZC. Skip sig checks
   on the unit test mock P2P node to avoid faililng the test.
   */
   nodePtr_->checkSigs(false);

   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", Config::NetworkSettings::dbPort(),
      std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
         Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
      true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey, true);
   bdvObj->connectToRemote();
   bdvObj->registerWithDB(hexMagicBytes);

   //go online
   bdvObj->goOnline();
   pCallback->waitOnSignal(BDMAction_Ready);
   std::vector<std::string> walletRegIDs{"wallet1"};

   auto wallet1 = bdvObj->getWalletObj("wallet1");
   wallet1.registerAddresses(scrAddrVec, false);

   //wait on registration ack
   pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

   //get wallets delegate
   auto del1_prom = std::make_shared<std::promise<AsyncClient::LedgerDelegate>>();
   auto del1_fut = del1_prom->get_future();
   auto del1_get = [del1_prom](
      ReturnMessage<AsyncClient::LedgerDelegate> delegate)->void
   {
      del1_prom->set_value(std::move(delegate.get()));
   };
   wallet1.getLedgerDelegate(del1_get);
   auto main_delegate = del1_fut.get();

   auto ledger_prom =
   std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger_fut = ledger_prom->get_future();
   auto ledger_get =
      [ledger_prom](
         ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger_prom->set_value(std::move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger_get);
   auto main_ledger = ledger_fut.get();

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage = main_ledger[0];
   EXPECT_EQ(historyPage.size(), 2ULL);

   EXPECT_EQ(historyPage[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage[0].getTxOutIndex(), 0U);

   //add the 2 zc
   auto ZC1 = TestUtils::getTx(2, 1); //block 2, tx 1
   auto ZChash1 = BtcUtils::getHash256(ZC1);

   auto ZC2 = TestUtils::getTx(2, 2); //block 2, tx 2
   auto ZChash2 = BtcUtils::getHash256(ZC2);

   //push the first zc
   bdvObj->broadcastZC({ZC1});
   pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), {ZChash1});

   //push them again, should get already in mempool error for first zc, notif for 2nd
   bdvObj->broadcastZC( { ZC1, ZC2 } );
   pCallback->waitOnError(
      ZChash1, ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);
   pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), {ZChash2});

   //get the new ledgers
   auto ledger2_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger2_fut = ledger2_prom->get_future();
   auto ledger2_get =
      [ledger2_prom](ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger2_prom->set_value(std::move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger2_get);
   main_ledger = std::move(ledger2_fut.get());

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage2 = main_ledger[0];
   EXPECT_EQ(historyPage2.size(), 4ULL);

   EXPECT_EQ(historyPage2[3].getValue(), -20 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[3].getBlockHeight(), UINT32_MAX);
   //zc index is 2 since 0 and 1 were assigned to the first zc: 0 at
   //the solo broadcast, 1 at the batched broadcast, which had the first
   //zc fail as already-in-mempool
   EXPECT_EQ(historyPage2[3].getTxOutIndex(), 2U);

   EXPECT_EQ(historyPage2[2].getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[2].getBlockHeight(), UINT32_MAX);
   EXPECT_EQ(historyPage2[2].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage2[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage2[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage2[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage2[0].getTxOutIndex(), 0U);

   //tx cache testing
   //grab both zc from async client
   auto zc_prom2 = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
   auto zc_fut2 = zc_prom2->get_future();
   auto zc_get2 =
      [zc_prom2](ReturnMessage<AsyncClient::TxBatchResult> txObj)->void
   {
      auto&& txVec = txObj.get();
      zc_prom2->set_value(move(txVec));
   };

   std::set<BinaryData> bothZC = { ZChash1, ZChash2 };
   bdvObj->getTxsByHash(bothZC, zc_get2);
   auto zc_obj2 = zc_fut2.get();

   ASSERT_EQ(zc_obj2.size(), 2ULL);

   auto iterZc1 = zc_obj2.find(ZChash1);
   ASSERT_NE(iterZc1, zc_obj2.end());
   ASSERT_NE(iterZc1->second, nullptr);
   EXPECT_EQ(ZChash1, iterZc1->second->getThisHash());
   EXPECT_EQ(iterZc1->second->getTxHeight(), UINT32_MAX);

   auto iterZc2 = zc_obj2.find(ZChash2);
   ASSERT_NE(iterZc2, zc_obj2.end());
   ASSERT_NE(iterZc2->second, nullptr);
   EXPECT_EQ(ZChash2, iterZc2->second->getThisHash());
   EXPECT_EQ(iterZc2->second->getTxHeight(), UINT32_MAX);
   
   //push an extra block
   TestUtils::appendBlocks({ "2" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   pCallback->waitOnSignal(BDMAction_NewBlock);

   //get the new ledgers
   auto ledger3_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger3_fut = ledger3_prom->get_future();
   auto ledger3_get =
      [ledger3_prom](ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger3_prom->set_value(move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger3_get);
   main_ledger = move(ledger3_fut.get());

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage3 = main_ledger[0];
   EXPECT_EQ(historyPage3.size(), 5ULL);

   EXPECT_EQ(historyPage3[4].getValue(), -20 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[4].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[4].getTxOutIndex(), 2U);

   EXPECT_EQ(historyPage3[3].getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[3].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[3].getTxOutIndex(), 1U);

   EXPECT_EQ(historyPage3[2].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[2].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[2].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage3[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage3[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage3[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage3[0].getTxOutIndex(), 0U);


   //grab both zc from async client
   auto zc_prom4 = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
   auto zc_fut4 = zc_prom4->get_future();
   auto zc_get4 =
      [zc_prom4](ReturnMessage<AsyncClient::TxBatchResult> txObj)->void
   {
      auto&& txVec = txObj.get();
      zc_prom4->set_value(move(txVec));
   };

   bdvObj->getTxsByHash(bothZC, zc_get4);
   auto zc_obj4 = zc_fut4.get();

   ASSERT_EQ(zc_obj4.size(), 2ULL);

   auto iterZc3 = zc_obj4.find(ZChash1);
   ASSERT_NE(iterZc3, zc_obj4.end());
   ASSERT_NE(iterZc3->second, nullptr);
   EXPECT_EQ(ZChash1, iterZc3->second->getThisHash());
   EXPECT_EQ(iterZc3->second->getTxHeight(), 2U);

   auto iterZc4 = zc_obj4.find(ZChash2);
   ASSERT_NE(iterZc4, zc_obj4.end());
   ASSERT_NE(iterZc4->second, nullptr);
   EXPECT_EQ(ZChash2, iterZc4->second->getThisHash());
   EXPECT_EQ(iterZc4->second->getTxHeight(), 2U);

   //disconnect
   bdvObj->unregisterFromDB();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, ZcUpdate_AlreadyInNodeMempool)
{
   /*
   Some sigs in static test chain are borked. P2SH scripts are borked too. This
   test plucks transactions from the static chain to push as ZC. Skip sig checks
   on the unit test mock P2P node to avoid faililng the test.
   */
   nodePtr_->checkSigs(false);

   //grab the first zc
   auto ZC1 = TestUtils::getTx(2, 1); //block 2, tx 1
   auto ZChash1 = BtcUtils::getHash256(ZC1);

   {
      //feed to node mempool while the zc parser is down
      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(ZC1, 0);
      DBTestUtils::pushNewZc(theBDMt_, zcVec, 0);
   }

   startupBIP150CTX(4);

   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", Config::NetworkSettings::dbPort(),
      std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
         Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
      true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey, true);
   bdvObj->connectToRemote();
   bdvObj->registerWithDB(hexMagicBytes);

   //go online
   bdvObj->goOnline();
   pCallback->waitOnSignal(BDMAction_Ready);
   std::vector<std::string> walletRegIDs{"wallet1"};

   auto wallet1 = bdvObj->getWalletObj("wallet1");
   wallet1.registerAddresses(scrAddrVec, false);

   //wait on registration ack
   pCallback->waitOnManySignals(BDMAction_Refresh, walletRegIDs);

   //get wallets delegate
   auto del1_prom = std::make_shared<std::promise<AsyncClient::LedgerDelegate>>();
   auto del1_fut = del1_prom->get_future();
   auto del1_get = [del1_prom](
      ReturnMessage<AsyncClient::LedgerDelegate> delegate)->void
   {
      del1_prom->set_value(std::move(delegate.get()));
   };
   wallet1.getLedgerDelegate(del1_get);
   auto&& main_delegate = del1_fut.get();

   auto ledger_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger_fut = ledger_prom->get_future();
   auto ledger_get =
      [ledger_prom](
         ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger_prom->set_value(std::move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger_get);
   auto&& main_ledger = ledger_fut.get();

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage = main_ledger[0];
   EXPECT_EQ(historyPage.size(), 2ULL);

   EXPECT_EQ(historyPage[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage[0].getTxOutIndex(), 0U);

   //add the 2 zc
   auto ZC2 = TestUtils::getTx(2, 2); //block 2, tx 2
   auto ZChash2 = BtcUtils::getHash256(ZC2);

   std::vector<BinaryData> zcVec = {ZC1, ZC2};
   bdvObj->broadcastZC(zcVec);
   pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), {ZChash1, ZChash2});

   //get the new ledgers
   auto ledger2_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger2_fut = ledger2_prom->get_future();
   auto ledger2_get =
      [ledger2_prom](ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger2_prom->set_value(move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger2_get);
   main_ledger = move(ledger2_fut.get());

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage2 = main_ledger[0];
   EXPECT_EQ(historyPage2.size(), 4ULL);

   EXPECT_EQ(historyPage2[3].getValue(), -20 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[3].getBlockHeight(), UINT32_MAX);
   EXPECT_EQ(historyPage2[3].getTxOutIndex(), 3U);

   EXPECT_EQ(historyPage2[2].getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[2].getBlockHeight(), UINT32_MAX);
   EXPECT_EQ(historyPage2[2].getTxOutIndex(), 2U);

   EXPECT_EQ(historyPage2[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage2[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage2[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage2[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage2[0].getTxOutIndex(), 0U);

   //tx cache testing
   //grab both zc from async client
   auto zc_prom2 = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
   auto zc_fut2 = zc_prom2->get_future();
   auto zc_get2 =
      [zc_prom2](ReturnMessage<AsyncClient::TxBatchResult> txObj)->void
   {
      auto txVec = txObj.get();
      zc_prom2->set_value(std::move(txVec));
   };

   std::set<BinaryData> bothZC = { ZChash1, ZChash2 };
   bdvObj->getTxsByHash(bothZC, zc_get2);
   auto zc_obj2 = zc_fut2.get();

   ASSERT_EQ(zc_obj2.size(), 2ULL);

   auto iterZc1 = zc_obj2.find(ZChash1);
   ASSERT_NE(iterZc1, zc_obj2.end());
   ASSERT_NE(iterZc1->second, nullptr);
   EXPECT_EQ(ZChash1, iterZc1->second->getThisHash());
   EXPECT_EQ(iterZc1->second->getTxHeight(), UINT32_MAX);

   auto iterZc2 = zc_obj2.find(ZChash2);
   ASSERT_NE(iterZc2, zc_obj2.end());
   ASSERT_NE(iterZc2->second, nullptr);
   EXPECT_EQ(ZChash2, iterZc2->second->getThisHash());
   EXPECT_EQ(iterZc2->second->getTxHeight(), UINT32_MAX);

   //push an extra block
   TestUtils::appendBlocks({ "2" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   pCallback->waitOnSignal(BDMAction_NewBlock);

   //get the new ledgers
   auto ledger3_prom =
      std::make_shared<std::promise<std::vector<DBClientClasses::HistoryPage>>>();
   auto ledger3_fut = ledger3_prom->get_future();
   auto ledger3_get =
      [ledger3_prom](ReturnMessage<std::vector<DBClientClasses::HistoryPage>> ledgerV)->void
   {
      ledger3_prom->set_value(std::move(ledgerV.get()));
   };
   main_delegate.getHistoryPages(0, 0, ledger3_get);
   main_ledger = std::move(ledger3_fut.get());

   //check ledgers
   EXPECT_EQ(main_ledger.size(), 1ULL);
   const auto& historyPage3 = main_ledger[0];
   EXPECT_EQ(historyPage3.size(), 5ULL);

   EXPECT_EQ(historyPage3[4].getValue(), -20 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[4].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[4].getTxOutIndex(), 2U);

   EXPECT_EQ(historyPage3[3].getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[3].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[3].getTxOutIndex(), 1U);

   EXPECT_EQ(historyPage3[2].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[2].getBlockHeight(), 2U);
   EXPECT_EQ(historyPage3[2].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage3[1].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[1].getBlockHeight(), 1U);
   EXPECT_EQ(historyPage3[1].getTxOutIndex(), 0U);

   EXPECT_EQ(historyPage3[0].getValue(), 50 * (int64_t)COIN);
   EXPECT_EQ(historyPage3[0].getBlockHeight(), 0U);
   EXPECT_EQ(historyPage3[0].getTxOutIndex(), 0U);

   //grab both zc from async client
   auto zc_prom4 = std::make_shared<std::promise<AsyncClient::TxBatchResult>>();
   auto zc_fut4 = zc_prom4->get_future();
   auto zc_get4 =
      [zc_prom4](ReturnMessage<AsyncClient::TxBatchResult> txObj)->void
   {
      auto txVec = txObj.get();
      zc_prom4->set_value(std::move(txVec));
   };

   bdvObj->getTxsByHash(bothZC, zc_get4);
   auto zc_obj4 = zc_fut4.get();

   ASSERT_EQ(zc_obj4.size(), 2ULL);

   auto iterZc3 = zc_obj4.find(ZChash1);
   ASSERT_NE(iterZc3, zc_obj4.end());
   ASSERT_NE(iterZc3->second, nullptr);
   EXPECT_EQ(ZChash1, iterZc3->second->getThisHash());
   EXPECT_EQ(iterZc3->second->getTxHeight(), 2U);

   auto iterZc4 = zc_obj4.find(ZChash2);
   ASSERT_NE(iterZc4, zc_obj4.end());
   ASSERT_NE(iterZc4->second, nullptr);
   EXPECT_EQ(ZChash2, iterZc4->second->getThisHash());
   EXPECT_EQ(iterZc4->second->getTxHeight(), 2U);

   //disconnect
   bdvObj->unregisterFromDB();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, ZcUpdate_RBFLowFee)
{
   //instantiate resolver feed overloaded object
   auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
   feed->addPrivKey(TestChain::privKeyAddrB.getRef());
   feed->addPrivKey(TestChain::privKeyAddrC.getRef());
   feed->addPrivKey(TestChain::privKeyAddrD.getRef());
   feed->addPrivKey(TestChain::privKeyAddrE.getRef());
   feed->addPrivKey(TestChain::privKeyAddrF.getRef());

   startupBIP150CTX(4);

   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE,
      TestChain::scrAddrF
   };

   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();
   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", Config::NetworkSettings::dbPort(),
      std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
         Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
      true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey, true);
   bdvObj->connectToRemote();
   bdvObj->registerWithDB(hexMagicBytes);

   auto wallet1 = bdvObj->getWalletObj("wallet1");
   wallet1.registerAddresses(scrAddrVec, false);

   //go online
   bdvObj->goOnline();
   pCallback->waitOnSignal(BDMAction_Ready);

   //create tx from utxo lambda
   auto makeTxFromUtxo = [feed](const UTXO& utxo, const BinaryData& recipient)->BinaryData
   {
      auto spender = std::make_shared<Signing::ScriptSpender>(utxo);
      spender->setSequence(0xFFFFFFFF - 2); //flag rbf

      auto recPtr = std::make_shared<Signing::Recipient_P2PKH>(
         recipient.getSliceCopy(1, 20), utxo.getValue());

      Signing::Signer signer;
      signer.setFeed(feed);
      signer.addSpender(spender);
      signer.addRecipient(recPtr);

      signer.sign();
      return signer.serializeSignedTx();
   };

   //grab utxo from db
   auto getUtxo = [&wallet1](const BinaryData& addr)->std::vector<UTXO>
   {
      auto addrObj = wallet1.getScrAddrObj(addr, 0, 0, 0, 0);
      auto promPtr = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto fut = promPtr->get_future();
      auto getUtxoLbd = [promPtr](ReturnMessage<std::vector<UTXO>> batch)->void
      {
         promPtr->set_value(batch.get());
      };

      addrObj.getOutputs(UINT64_MAX, false, false, getUtxoLbd);
      return fut.get();
   };

   //create tx from spender address lambda
   auto makeTx = [makeTxFromUtxo, getUtxo, bdvObj](
      const BinaryData& payer, const BinaryData& recipient)->BinaryData
   {
      auto utxoVec = getUtxo(payer);
      if (utxoVec.empty()) {
         throw std::runtime_error("unexpected utxo vec size");
      }
      auto& utxo = utxoVec[0];
      return makeTxFromUtxo(utxo, recipient);
   };

   //grab utxo from raw tx lambda
   auto getUtxoFromRawTx = [](BinaryData& rawTx, unsigned id)->UTXO
   {
      Tx tx(rawTx);
      if (id > tx.getNumTxOut()) {
         throw std::runtime_error("invalid txout count");
      }
      auto txOut = tx.getTxOutCopy(id);
      
      UTXO utxo;
      utxo.unserializeRaw(txOut.serialize());
      utxo.txOutIndex_ = id;
      utxo.txHash_ = tx.getThisHash();
      return utxo;
   };

   //grab combined balances lambda
   auto getBalances = [bdvObj]()->AsyncClient::CombinedBalances
   {
      auto promPtr = std::make_shared<std::promise<std::map<std::string, AsyncClient::CombinedBalances>>>();
      auto fut = promPtr->get_future();
      auto balLbd = [promPtr](
         ReturnMessage<std::map<std::string, AsyncClient::CombinedBalances>> combBal)->void
      {
         promPtr->set_value(combBal.get());
      };

      bdvObj->getCombinedBalances(balLbd);
      auto balMap = fut.get();
      if (balMap.size() != 1) {
         throw std::runtime_error("unexpected balance map size");
      }
      return balMap.begin()->second;
   };

   //check original balances
   {
      auto combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
      ASSERT_NE(iterA, combineBalances.addressBalances.end());
      ASSERT_EQ(iterA->second.size(), 4ULL);
      EXPECT_EQ(iterA->second[0], 50 * COIN);

      auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
      ASSERT_NE(iterB, combineBalances.addressBalances.end());
      ASSERT_EQ(iterB->second.size(), 4ULL);
      EXPECT_EQ(iterB->second[0], 70 * COIN);

      auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
      ASSERT_NE(iterC, combineBalances.addressBalances.end());
      ASSERT_EQ(iterC->second.size(), 4ULL);
      EXPECT_EQ(iterC->second[0], 20 * COIN);

      auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
      ASSERT_NE(iterD, combineBalances.addressBalances.end());
      ASSERT_EQ(iterD->second.size(), 4ULL);
      EXPECT_EQ(iterD->second[0], 65 * COIN);

      auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
      ASSERT_NE(iterE, combineBalances.addressBalances.end());
      ASSERT_EQ(iterE->second.size(), 4ULL);
      EXPECT_EQ(iterE->second[0], 30 * COIN);

      auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
      ASSERT_NE(iterF, combineBalances.addressBalances.end());
      ASSERT_EQ(iterF->second.size(), 4ULL);
      EXPECT_EQ(iterF->second[0], 5 * COIN);
   }

   BinaryData branchPointBlockHash, mainBranchBlockHash;
   {
      auto top = theBDMt_->bdm()->blockchain()->top();
      branchPointBlockHash = top->getThisHash().toBinaryData();
   }

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
      bdvObj->broadcastZC({bd_BtoC});
      bdvObj->broadcastZC({bd_FtoD});

      {
         Tx tx1(bd_BtoC);
         Tx tx2(bd_FtoD);

         pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), {tx1.getThisHash()});
         pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), {tx2.getThisHash()});
      }

      //tx from B to A, should fail with RBF low fee
      auto bd_BtoA = makeTx(TestChain::scrAddrB, TestChain::scrAddrA);
      Tx tx(bd_BtoA);

      bdvObj->broadcastZC({bd_BtoA});
      pCallback->waitOnError(tx.getThisHash(),
         ArmoryErrorCodes::P2PReject_InsufficientFee);
 
      //mine
      DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
      pCallback->waitOnSignal(BDMAction_NewBlock);

      //zc C to E
      auto utxo = getUtxoFromRawTx(bd_BtoC, 0);
      auto bd_CtoE = makeTxFromUtxo(utxo, TestChain::scrAddrE);

      //broadcast
      bdvObj->broadcastZC({bd_CtoE});
      pCallback->waitOnSignal(BDMAction_ZC);

      //mine
      DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 1);
      pCallback->waitOnSignal(BDMAction_NewBlock);

      //check balances
      auto combineBalances = getBalances();

      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
      ASSERT_NE(iterA, combineBalances.addressBalances.end());
      ASSERT_EQ(iterA->second.size(), 4ULL);
      EXPECT_EQ(iterA->second[0], 155 * COIN);

      auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
      ASSERT_NE(iterB, combineBalances.addressBalances.end());
      ASSERT_EQ(iterB->second.size(), 4ULL);
      EXPECT_EQ(iterB->second[0], 20 * COIN);

      auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
      ASSERT_NE(iterC, combineBalances.addressBalances.end());
      ASSERT_EQ(iterC->second.size(), 4ULL);
      EXPECT_EQ(iterC->second[0], 20 * COIN);

      auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
      ASSERT_NE(iterD, combineBalances.addressBalances.end());
      ASSERT_EQ(iterD->second.size(), 4ULL);
      EXPECT_EQ(iterD->second[0], 65 * COIN);

      auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
      ASSERT_NE(iterE, combineBalances.addressBalances.end());
      ASSERT_EQ(iterE->second.size(), 4ULL);
      EXPECT_EQ(iterE->second[0], 80 * COIN);

      auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
      ASSERT_NE(iterF, combineBalances.addressBalances.end());
      ASSERT_EQ(iterF->second.size(), 4ULL);
      EXPECT_EQ(iterF->second[0], 0 * COIN);
   }

   EXPECT_EQ(theBDMt_->bdm()->zeroConfCont()->getMatcherMapSize(), 0U);
   EXPECT_GE(theBDMt_->bdm()->zeroConfCont()->getMergeCount(), 1U);

   //cleanup
   bdvObj->unregisterFromDB();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, BatchZcChain)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
            Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey, true);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(hexMagicBytes);

      auto wallet1 = bdvObj->getWalletObj("wallet1");
      std::vector<BinaryData> _scrAddrVec1 {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      wallet1.registerAddresses(_scrAddrVec1, false);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      //balance fetching routine
      auto getBalances = [bdvObj](void)->AsyncClient::CombinedBalances
      {
         auto promPtr = std::make_shared<std::promise<std::map<std::string, AsyncClient::CombinedBalances>>>();
         auto fut = promPtr->get_future();
         auto balLbd = [promPtr](
            ReturnMessage<std::map<std::string, AsyncClient::CombinedBalances>> combBal)->void
         {
            promPtr->set_value(combBal.get());
         };

         bdvObj->getCombinedBalances(balLbd);
         auto balMap = fut.get();

         if (balMap.size() != 1) {
            throw std::runtime_error("unexpected balance map size");
         }
         return balMap.begin()->second;
      };

      //check balances before pushing zc
      auto combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 50 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 70 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 20 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 65 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 30 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 5 * COIN);
      }
   
      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());
      feed->addPrivKey(TestChain::privKeyAddrF.getRef());

      //grab utxos for scrAddrB
      auto promUtxo = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto futUtxo = promUtxo->get_future();
      auto getUtxoLbd = [promUtxo](ReturnMessage<std::vector<UTXO>> msg)->void
      {
         promUtxo->set_value(msg.get());
      };

      wallet1.getUTXOs(UINT64_MAX, false, false, getUtxoLbd);
      std::vector<UTXO> utxosB;
      {
         auto utxoVec = futUtxo.get();
         for (auto& utxo : utxoVec) {
            if (utxo.getRecipientScrAddr() != TestChain::scrAddrB) {
               continue;
            }
            utxosB.push_back(utxo);
         }
      }

      ASSERT_FALSE(utxosB.empty());

      /*create the transactions*/

      //grab utxo from raw tx
      auto getUtxoFromRawTx = [](BinaryData& rawTx, unsigned id)->UTXO
      {
         Tx tx(rawTx);
         if (id > tx.getNumTxOut()) {
            throw std::runtime_error("invalid txout count");
         }
         auto txOut = tx.getTxOutCopy(id);

         UTXO utxo;
         utxo.unserializeRaw(txOut.serialize());
         utxo.txOutIndex_ = id;
         utxo.txHash_ = tx.getThisHash();
         return utxo;
      };

      BinaryData rawTx1, rawTx2;

      {
         //20 from B, 5 to A, change to D
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 
            spender->getValue() - recA->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1 = signer.serializeSignedTx();
      }

      {
         auto utxoD = getUtxoFromRawTx(rawTx1, 1);

         //15 from D, 10 to E, change to F
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20), 
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx2 = signer.serializeSignedTx();
      }

      BinaryData rawTx3;
      {
         //10 from E, 5 from F, 3 to A, 2 to E, 5 to D, change to C
         auto zcUtxo1 = getUtxoFromRawTx(rawTx2, 0);
         auto zcUtxo2 = getUtxoFromRawTx(rawTx2, 1);

         Signing::Signer signer;

         auto spender1 = std::make_shared<Signing::ScriptSpender>(zcUtxo1);
         auto spender2 = std::make_shared<Signing::ScriptSpender>(zcUtxo2);
         signer.addSpender(spender1);
         signer.addSpender(spender2);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 3 * COIN);
         signer.addRecipient(recA);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 2 * COIN);
         signer.addRecipient(recE);
         
         auto recD = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recD);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrC.getSliceCopy(1, 20),
            spender1->getValue() + spender2->getValue() -
            recA->getValue() - recE->getValue() - recD->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx3 = signer.serializeSignedTx();
      }

      //batch push tx
      bdvObj->broadcastZC({ rawTx1, rawTx2, rawTx3 });
      Tx tx1(rawTx1);
      Tx tx2(rawTx2);
      Tx tx3(rawTx3);

      std::set<BinaryData> txHashes;
      txHashes.insert(tx1.getThisHash());
      txHashes.insert(tx2.getThisHash());
      txHashes.insert(tx3.getThisHash());
      pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), txHashes);

      //check balances
      combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 58 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 50 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 25 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 70 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 32 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 5 * COIN);
      }

      //disconnect
      bdvObj->unregisterFromDB();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, BatchZcChain_AlreadyInMempool)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
            Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey, true);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(hexMagicBytes);

      auto wallet1 = bdvObj->getWalletObj("wallet1");
      std::vector<BinaryData> _scrAddrVec1 {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      wallet1.registerAddresses(_scrAddrVec1, false);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      //balance fetching routine
      auto getBalances = [bdvObj](void)->AsyncClient::CombinedBalances
      {
         auto promPtr = std::make_shared<std::promise<std::map<std::string, AsyncClient::CombinedBalances>>>();
         auto fut = promPtr->get_future();
         auto balLbd = [promPtr](
            ReturnMessage<std::map<std::string, AsyncClient::CombinedBalances>> combBal)->void
         {
            promPtr->set_value(combBal.get());
         };

         bdvObj->getCombinedBalances(balLbd);
         auto balMap = fut.get();
         if (balMap.size() != 1) {
            throw std::runtime_error("unexpected balance map size");
         }
         return balMap.begin()->second;
      };

      //check balances before pushing zc
      auto combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 50 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 70 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 20 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 65 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 30 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 5 * COIN);
      }

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());
      feed->addPrivKey(TestChain::privKeyAddrF.getRef());

      //grab utxos for scrAddrB
      auto promUtxo = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto futUtxo = promUtxo->get_future();
      auto getUtxoLbd = [promUtxo](ReturnMessage<std::vector<UTXO>> msg)->void
      {
         promUtxo->set_value(msg.get());
      };

      wallet1.getUTXOs(UINT64_MAX, false, false, getUtxoLbd);
      std::vector<UTXO> utxosB;
      {
         auto utxoVec = futUtxo.get();
         for (auto& utxo : utxoVec) {
            if (utxo.getRecipientScrAddr() != TestChain::scrAddrB) {
               continue;
            }
            utxosB.push_back(utxo);
         }
      }

      ASSERT_FALSE(utxosB.empty());

      /*create the transactions*/

      //grab utxo from raw tx
      auto getUtxoFromRawTx = [](BinaryData& rawTx, unsigned id)->UTXO
      {
         Tx tx(rawTx);
         if (id > tx.getNumTxOut()) {
            throw std::runtime_error("invalid txout count");
         }
         auto txOut = tx.getTxOutCopy(id);

         UTXO utxo;
         utxo.unserializeRaw(txOut.serialize());
         utxo.txOutIndex_ = id;
         utxo.txHash_ = tx.getThisHash();
         return utxo;
      };

      BinaryData rawTx1, rawTx2;

      {
         //20 from B, 5 to A, change to D
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 
            spender->getValue() - recA->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1 = signer.serializeSignedTx();
      }

      {
         auto utxoD = getUtxoFromRawTx(rawTx1, 1);

         //15 from D, 10 to E, change to F
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20), 
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx2 = signer.serializeSignedTx();
      }

      BinaryData rawTx3;
      {
         //10 from E, 5 from F, 3 to A, 2 to E, 5 to D, change to C
         auto zcUtxo1 = getUtxoFromRawTx(rawTx2, 0);
         auto zcUtxo2 = getUtxoFromRawTx(rawTx2, 1);

         Signing::Signer signer;

         auto spender1 = std::make_shared<Signing::ScriptSpender>(zcUtxo1);
         auto spender2 = std::make_shared<Signing::ScriptSpender>(zcUtxo2);
         signer.addSpender(spender1);
         signer.addSpender(spender2);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 3 * COIN);
         signer.addRecipient(recA);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 2 * COIN);
         signer.addRecipient(recE);
         
         auto recD = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recD);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrC.getSliceCopy(1, 20),
            spender1->getValue() + spender2->getValue() -
            recA->getValue() - recE->getValue() - recD->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx3 = signer.serializeSignedTx();
      }

      Tx tx1(rawTx1);
      Tx tx2(rawTx2);
      Tx tx3(rawTx3);

      //push first tx
      bdvObj->broadcastZC({rawTx1});

      std::set<BinaryData> txHashes;
      txHashes.insert(tx1.getThisHash());
      pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), txHashes);

      //batch push all tx
      bdvObj->broadcastZC({ rawTx1, rawTx2, rawTx3 });
      txHashes.clear();
      txHashes.insert(tx2.getThisHash());
      txHashes.insert(tx3.getThisHash());

      //wait on already in mempool error
      pCallback->waitOnError(tx1.getThisHash(),
         ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);

      //wait on zc notifs
      pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), txHashes);

      //check balances
      combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 58 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 50 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 25 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 70 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 32 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 5 * COIN);
      }

      //disconnect
      bdvObj->unregisterFromDB();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, BatchZcChain_AlreadyInNodeMempool)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
            Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey, true);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(hexMagicBytes);

      auto wallet1 = bdvObj->getWalletObj("wallet1");
      std::vector<BinaryData> _scrAddrVec1 {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      wallet1.registerAddresses(_scrAddrVec1, false);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      //balance fetching routine
      auto getBalances = [bdvObj](void)->AsyncClient::CombinedBalances
      {
         auto promPtr = std::make_shared<std::promise<std::map<std::string, AsyncClient::CombinedBalances>>>();
         auto fut = promPtr->get_future();
         auto balLbd = [promPtr](
            ReturnMessage<std::map<std::string, AsyncClient::CombinedBalances>> combBal)->void
         {
            promPtr->set_value(combBal.get());
         };

         bdvObj->getCombinedBalances(balLbd);
         auto balMap = fut.get();
         if (balMap.size() != 1) {
            throw std::runtime_error("unexpected balance map size");
         }
         return balMap.begin()->second;
      };

      //check balances before pushing zc
      auto combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 50 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 70 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 20 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 65 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 30 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 5 * COIN);
      }
   
      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());
      feed->addPrivKey(TestChain::privKeyAddrF.getRef());

      //grab utxos for scrAddrB & scrAddrC
      auto promUtxo = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto futUtxo = promUtxo->get_future();
      auto getUtxoLbd = [promUtxo](ReturnMessage<std::vector<UTXO>> msg)->void
      {
         promUtxo->set_value(msg.get());
      };

      wallet1.getUTXOs(UINT64_MAX, false, false, getUtxoLbd);
      std::vector<UTXO> utxosB, utxosC;
      {
         auto utxoVec = futUtxo.get();
         for (auto& utxo : utxoVec) {
            if (utxo.getRecipientScrAddr() == TestChain::scrAddrB) {
               utxosB.push_back(utxo);
            } else if (utxo.getRecipientScrAddr() == TestChain::scrAddrC) {
               utxosC.push_back(utxo);
            }
         }
      }

      ASSERT_FALSE(utxosB.empty());
      ASSERT_FALSE(utxosC.empty());

      /*create the transactions*/

      //grab utxo from raw tx
      auto getUtxoFromRawTx = [](BinaryData& rawTx, unsigned id)->UTXO
      {
         Tx tx(rawTx);
         if (id > tx.getNumTxOut()) {
            throw std::runtime_error("invalid txout count");
         }
         auto txOut = tx.getTxOutCopy(id);

         UTXO utxo;
         utxo.unserializeRaw(txOut.serialize());
         utxo.txOutIndex_ = id;
         utxo.txHash_ = tx.getThisHash();
         return utxo;
      };

      BinaryData rawTx1_B;
      {
         //20 from B, 5 to A, change to D
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20),
            spender->getValue() - recA->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1_B = signer.serializeSignedTx();
      }

      BinaryData rawTx1_C;
      {
         //20 from C, 5 to E, change to C
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrC.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1_C = signer.serializeSignedTx();
      }

      BinaryData rawTx2;
      {
         auto utxoD = getUtxoFromRawTx(rawTx1_B, 1);

         //15 from D, 10 to E, change to F
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx2 = signer.serializeSignedTx();
      }

      BinaryData rawTx3;
      {
         //10 from E, 5 from F, 3 to A, 2 to E, 5 to D, change to C
         auto zcUtxo1 = getUtxoFromRawTx(rawTx2, 0);
         auto zcUtxo2 = getUtxoFromRawTx(rawTx2, 1);

         Signing::Signer signer;
         auto spender1 = std::make_shared<Signing::ScriptSpender>(zcUtxo1);
         auto spender2 = std::make_shared<Signing::ScriptSpender>(zcUtxo2);
         signer.addSpender(spender1);
         signer.addSpender(spender2);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 3 * COIN);
         signer.addRecipient(recA);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 2 * COIN);
         signer.addRecipient(recE);

         auto recD = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recD);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrC.getSliceCopy(1, 20),
            spender1->getValue() + spender2->getValue() -
            recA->getValue() - recE->getValue() - recD->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx3 = signer.serializeSignedTx();
      }

      Tx tx1_B(rawTx1_B);
      Tx tx1_C(rawTx1_C);
      Tx tx2(rawTx2);
      Tx tx3(rawTx3);

      //push through the node
      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(rawTx1_B, 1000000000);
      DBTestUtils::pushNewZc(theBDMt_, zcVec, true);

      //batch push all tx
      bdvObj->broadcastZC({ rawTx1_B, rawTx1_C, rawTx2, rawTx3 });

      std::set<BinaryData> txHashes;
      txHashes.insert(tx1_B.getThisHash());
      txHashes.insert(tx1_C.getThisHash());
      txHashes.insert(tx2.getThisHash());
      txHashes.insert(tx3.getThisHash());

      //wait on zc notifs
      pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), txHashes);

      //check balances
      combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6U);

      auto printBal = [](const std::string addrStr, std::vector<uint64_t> bals)
      {
         std::cout << " - " << addrStr << " -" << std::endl;
         std::cout << "   bal          : " << bals[0] << std::endl;
         std::cout << "   spendable    : " << bals[1] << std::endl;
         std::cout << "   unconfirmed  : " << bals[2] << std::endl;
         std::cout << "   count        : " << bals[3] << std::endl;
      };

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 58 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 50 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 20 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         printBal("scrAddrD", iterD->second);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 70 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         printBal("scrAddrE", iterE->second);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 37 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         printBal("scrAddrF", iterF->second);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 5 * COIN);
      }

      //disconnect
      bdvObj->unregisterFromDB();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, BatchZcChain_AlreadyInChain)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
            Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey, true);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(hexMagicBytes);

      auto wallet1 = bdvObj->getWalletObj("wallet1");
      std::vector<BinaryData> _scrAddrVec1 {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      wallet1.registerAddresses(_scrAddrVec1, false);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      //balance fetching routine
      auto getBalances = [bdvObj](void)->AsyncClient::CombinedBalances
      {
         auto promPtr = std::make_shared<std::promise<std::map<std::string, AsyncClient::CombinedBalances>>>();
         auto fut = promPtr->get_future();
         auto balLbd = [promPtr](
            ReturnMessage<std::map<std::string, AsyncClient::CombinedBalances>> combBal)->void
         {
            promPtr->set_value(combBal.get());
         };

         bdvObj->getCombinedBalances(balLbd);
         auto balMap = fut.get();

         if (balMap.size() != 1) {
            throw std::runtime_error("unexpected balance map size");
         }
         return balMap.begin()->second;
      };

      //check balances before pushing zc
      auto combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 50 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 70 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 20 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 65 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 30 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 5 * COIN);
      }
   
      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());
      feed->addPrivKey(TestChain::privKeyAddrF.getRef());

      //grab utxos for scrAddrB & scrAddrC
      auto promUtxo = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto futUtxo = promUtxo->get_future();
      auto getUtxoLbd = [promUtxo](ReturnMessage<std::vector<UTXO>> msg)->void
      {
         promUtxo->set_value(msg.get());
      };

      wallet1.getUTXOs(UINT64_MAX, false, false, getUtxoLbd);
      std::vector<UTXO> utxosB, utxosC;
      {
         auto utxoVec = futUtxo.get();
         for (auto& utxo : utxoVec) {
            if (utxo.getRecipientScrAddr() == TestChain::scrAddrB) {
               utxosB.push_back(utxo);
            } else if (utxo.getRecipientScrAddr() == TestChain::scrAddrC) {
               utxosC.push_back(utxo);
            }
         }
      }

      ASSERT_FALSE(utxosB.empty());
      ASSERT_FALSE(utxosC.empty());

      /*create the transactions*/

      //grab utxo from raw tx
      auto getUtxoFromRawTx = [](BinaryData& rawTx, unsigned id)->UTXO
      {
         Tx tx(rawTx);
         if (id > tx.getNumTxOut()) {
            throw std::runtime_error("invalid txout count");
         }
         auto txOut = tx.getTxOutCopy(id);

         UTXO utxo;
         utxo.unserializeRaw(txOut.serialize());
         utxo.txOutIndex_ = id;
         utxo.txHash_ = tx.getThisHash();
         return utxo;
      };

      BinaryData rawTx1_B;
      {
         //20 from B, 5 to A, change to D
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20),
            spender->getValue() - recA->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1_B = signer.serializeSignedTx();
      }

      BinaryData rawTx1_C;
      {
         //20 from C, 5 to E, change to C
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrC.getSliceCopy(1, 20), 
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1_C = signer.serializeSignedTx();
      }

      BinaryData rawTx2;
      {
         auto utxoD = getUtxoFromRawTx(rawTx1_B, 1);

         //15 from D, 10 to E, change to F
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx2 = signer.serializeSignedTx();
      }

      BinaryData rawTx3;
      {
         //10 from E, 5 from F, 3 to A, 2 to E, 5 to D, change to C
         auto zcUtxo1 = getUtxoFromRawTx(rawTx2, 0);
         auto zcUtxo2 = getUtxoFromRawTx(rawTx2, 1);

         Signing::Signer signer;
         auto spender1 = std::make_shared<Signing::ScriptSpender>(zcUtxo1);
         auto spender2 = std::make_shared<Signing::ScriptSpender>(zcUtxo2);
         signer.addSpender(spender1);
         signer.addSpender(spender2);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 3 * COIN);
         signer.addRecipient(recA);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 2 * COIN);
         signer.addRecipient(recE);

         auto recD = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recD);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrC.getSliceCopy(1, 20),
            spender1->getValue() + spender2->getValue() -
            recA->getValue() - recE->getValue() - recD->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx3 = signer.serializeSignedTx();
      }
      
      Tx tx1_B(rawTx1_B);
      Tx tx1_C(rawTx1_C);
      Tx tx2(rawTx2);
      Tx tx3(rawTx3);

      //push through the node
      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(rawTx1_B, 1000000000);
      DBTestUtils::pushNewZc(theBDMt_, zcVec, true);

      //mine 1 block
      DBTestUtils::mineNewBlock(theBDMt_, Cryptography::PRNG::generateRandomStrong(20), 1);
      pCallback->waitOnSignal(BDMAction_NewBlock);

      //batch push all tx
      bdvObj->broadcastZC({ rawTx1_B, rawTx1_C, rawTx2, rawTx3 });
      std::set<BinaryData> txHashes;
      txHashes.insert(tx1_C.getThisHash());
      txHashes.insert(tx2.getThisHash());
      txHashes.insert(tx3.getThisHash());

      //wait on zc notifs
      pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), txHashes);

      //check balances
      combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 58 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 50 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 20 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 70 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 37 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 5 * COIN);
      }

      //disconnect
      bdvObj->unregisterFromDB();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, BatchZcChain_MissInv)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
            Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey, true);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(hexMagicBytes);

      auto wallet1 = bdvObj->getWalletObj("wallet1");
      std::vector<BinaryData> _scrAddrVec1 {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      wallet1.registerAddresses(_scrAddrVec1, false);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      //balance fetching routine
      auto getBalances = [bdvObj](void)->AsyncClient::CombinedBalances
      {
         auto promPtr = std::make_shared<std::promise<std::map<std::string, AsyncClient::CombinedBalances>>>();
         auto fut = promPtr->get_future();
         auto balLbd = [promPtr](
            ReturnMessage<std::map<std::string, AsyncClient::CombinedBalances>> combBal)->void
         {
            promPtr->set_value(combBal.get());
         };

         bdvObj->getCombinedBalances(balLbd);
         auto balMap = fut.get();
         if (balMap.size() != 1) {
            throw std::runtime_error("unexpected balance map size");
         }
         return balMap.begin()->second;
      };

      //check balances before pushing zc
      auto combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 50 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 70 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 20 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 65 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 30 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 5 * COIN);
      }
   
      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());
      feed->addPrivKey(TestChain::privKeyAddrF.getRef());

      //grab utxos for scrAddrB & scrAddrC
      auto promUtxo = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto futUtxo = promUtxo->get_future();
      auto getUtxoLbd = [promUtxo](ReturnMessage<std::vector<UTXO>> msg)->void
      {
         promUtxo->set_value(msg.get());
      };

      wallet1.getUTXOs(UINT64_MAX, false, false, getUtxoLbd);
      std::vector<UTXO> utxosB, utxosC;
      {
         auto utxoVec = futUtxo.get();
         for (auto& utxo : utxoVec) {
            if (utxo.getRecipientScrAddr() == TestChain::scrAddrB) {
               utxosB.push_back(utxo);
            } else if (utxo.getRecipientScrAddr() == TestChain::scrAddrC) {
               utxosC.push_back(utxo);
            }
         }
      }

      ASSERT_FALSE(utxosB.empty());
      ASSERT_FALSE(utxosC.empty());

      /*create the transactions*/

      //grab utxo from raw tx
      auto getUtxoFromRawTx = [](BinaryData& rawTx, unsigned id)->UTXO
      {
         Tx tx(rawTx);
         if (id > tx.getNumTxOut()) {
            throw std::runtime_error("invalid txout count");
         }
         auto txOut = tx.getTxOutCopy(id);

         UTXO utxo;
         utxo.unserializeRaw(txOut.serialize());
         utxo.txOutIndex_ = id;
         utxo.txHash_ = tx.getThisHash();
         return utxo;
      };

      BinaryData rawTx1_B;
      {
         //20 from B, 5 to A, change to D
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 
            spender->getValue() - recA->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1_B = signer.serializeSignedTx();
      }

      BinaryData rawTx1_C;
      {
         //20 from C, 5 to E, change to C
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrC.getSliceCopy(1, 20), 
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1_C = signer.serializeSignedTx();
      }

      BinaryData rawTx2;
      {
         auto utxoD = getUtxoFromRawTx(rawTx1_B, 1);

         //15 from D, 10 to E, change to F
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx2 = signer.serializeSignedTx();
      }

      BinaryData rawTx3;
      {
         //10 from E, 5 from F, 3 to A, 2 to E, 5 to D, change to C
         auto zcUtxo1 = getUtxoFromRawTx(rawTx2, 0);
         auto zcUtxo2 = getUtxoFromRawTx(rawTx2, 1);

         Signing::Signer signer;

         auto spender1 = std::make_shared<Signing::ScriptSpender>(zcUtxo1);
         auto spender2 = std::make_shared<Signing::ScriptSpender>(zcUtxo2);
         signer.addSpender(spender1);
         signer.addSpender(spender2);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 3 * COIN);
         signer.addRecipient(recA);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 2 * COIN);
         signer.addRecipient(recE);
         
         auto recD = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recD);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrC.getSliceCopy(1, 20),
            spender1->getValue() + spender2->getValue() -
            recA->getValue() - recE->getValue() - recD->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx3 = signer.serializeSignedTx();
      }
      
      Tx tx1_B(rawTx1_B);
      Tx tx1_C(rawTx1_C);
      Tx tx2(rawTx2);
      Tx tx3(rawTx3);

      //push through the node
      nodePtr_->presentZcHash(tx2.getThisHash());

      //batch push all tx
      bdvObj->broadcastZC({ rawTx1_B, rawTx1_C, rawTx2, rawTx3 });
      std::set<BinaryData> txHashes;
      txHashes.insert(tx1_B.getThisHash());
      txHashes.insert(tx1_C.getThisHash());
      txHashes.insert(tx2.getThisHash());
      txHashes.insert(tx3.getThisHash());

      //wait on zc notifs
      pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), txHashes);

      //check balances
      combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 58 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 50 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 20 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 70 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 37 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 5 * COIN);
      }

      //disconnect
      bdvObj->unregisterFromDB();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, BatchZcChain_ConflictingChildren)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto&& serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
            Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey, true);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(hexMagicBytes);

      auto wallet1 = bdvObj->getWalletObj("wallet1");
      std::vector<BinaryData> _scrAddrVec1 {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      wallet1.registerAddresses(_scrAddrVec1, false);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      //balance fetching routine
      auto getBalances = [bdvObj](void)->AsyncClient::CombinedBalances
      {
         auto promPtr = std::make_shared<std::promise<std::map<std::string, AsyncClient::CombinedBalances>>>();
         auto fut = promPtr->get_future();
         auto balLbd = [promPtr](
            ReturnMessage<std::map<std::string, AsyncClient::CombinedBalances>> combBal)->void
         {
            promPtr->set_value(combBal.get());
         };

         bdvObj->getCombinedBalances(balLbd);
         auto balMap = fut.get();
         if (balMap.size() != 1) {
            throw std::runtime_error("unexpected balance map size");
         }
         return balMap.begin()->second;
      };

      //check balances before pushing zc
      auto combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 50 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 70 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 20 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 65 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 30 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 5 * COIN);
      }
   
      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());
      feed->addPrivKey(TestChain::privKeyAddrF.getRef());

      //grab utxos for scrAddrB & scrAddrC
      auto promUtxo = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto futUtxo = promUtxo->get_future();
      auto getUtxoLbd = [promUtxo](ReturnMessage<std::vector<UTXO>> msg)->void
      {
         promUtxo->set_value(msg.get());
      };

      wallet1.getUTXOs(UINT64_MAX, false, false, getUtxoLbd);
      std::vector<UTXO> utxosB, utxosC;
      {
         auto utxoVec = futUtxo.get();
         for (auto& utxo : utxoVec) {
            if (utxo.getRecipientScrAddr() == TestChain::scrAddrB) {
               utxosB.push_back(utxo);
            } else if (utxo.getRecipientScrAddr() == TestChain::scrAddrC) {
               utxosC.push_back(utxo);
            }
         }
      }

      ASSERT_FALSE(utxosB.empty());
      ASSERT_FALSE(utxosC.empty());

      /*create the transactions*/

      //grab utxo from raw tx
      auto getUtxoFromRawTx = [](BinaryData& rawTx, unsigned id)->UTXO
      {
         Tx tx(rawTx);
         if (id > tx.getNumTxOut()) {
            throw std::runtime_error("invalid txout count");
         }
         auto txOut = tx.getTxOutCopy(id);

         UTXO utxo;
         utxo.unserializeRaw(txOut.serialize());
         utxo.txOutIndex_ = id;
         utxo.txHash_ = tx.getThisHash();
         return utxo;
      };

      BinaryData rawTx1_B;
      {
         //20 from B, 5 to A, change to D
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20),
            spender->getValue() - recA->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1_B = signer.serializeSignedTx();
      }

      BinaryData rawTx1_C;
      {
         //20 from C, 5 to E, change to C
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrC.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1_C = signer.serializeSignedTx();
      }

      BinaryData rawTx2;
      {
         auto utxoD = getUtxoFromRawTx(rawTx1_B, 1);

         //15 from D, 10 to E, change to F
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx2 = signer.serializeSignedTx();
      }

      BinaryData rawTx3;
      {
         auto utxoD = getUtxoFromRawTx(rawTx1_B, 1);

         //15 from D, 10 to E, change to A
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx3 = signer.serializeSignedTx();
      }

      Tx tx1_B(rawTx1_B);
      Tx tx1_C(rawTx1_C);
      Tx tx2(rawTx2);
      Tx tx3(rawTx3);

      //batch push all tx
      bdvObj->broadcastZC({ rawTx1_B, rawTx1_C, rawTx2, rawTx3 });
      std::set<BinaryData> txHashes {
         tx1_B.getThisHash(),
         tx1_C.getThisHash(),
         tx2.getThisHash()
      };

      //wait on zc error for conflicting child
      pCallback->waitOnError(
         tx3.getThisHash(), ArmoryErrorCodes::ZcBroadcast_VerifyRejected);

      //wait on zc notifs
      pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), txHashes);

      //check balances
      combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 55 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 50 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 15 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 65 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 45 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 10 * COIN);
      }

      //disconnect
      bdvObj->unregisterFromDB();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, BatchZcChain_ConflictingChildren_AlreadyInChain1)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
            Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey, true);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(hexMagicBytes);

      auto wallet1 = bdvObj->getWalletObj("wallet1");
      std::vector<BinaryData> _scrAddrVec1 {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      wallet1.registerAddresses(_scrAddrVec1, false);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      //balance fetching routine
      auto getBalances = [bdvObj](void)->AsyncClient::CombinedBalances
      {
         auto promPtr = std::make_shared<std::promise<std::map<std::string, AsyncClient::CombinedBalances>>>();
         auto fut = promPtr->get_future();
         auto balLbd = [promPtr](
            ReturnMessage<std::map<std::string, AsyncClient::CombinedBalances>> combBal)->void
         {
            promPtr->set_value(combBal.get());
         };

         bdvObj->getCombinedBalances(balLbd);
         auto balMap = fut.get();

         if (balMap.size() != 1) {
            throw std::runtime_error("unexpected balance map size");
         }
         return balMap.begin()->second;
      };

      //check balances before pushing zc
      auto combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 50 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 70 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 20 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 65 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 30 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 5 * COIN);
      }
   
      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());
      feed->addPrivKey(TestChain::privKeyAddrF.getRef());

      //grab utxos for scrAddrB & scrAddrC
      auto promUtxo = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto futUtxo = promUtxo->get_future();
      auto getUtxoLbd = [promUtxo](ReturnMessage<std::vector<UTXO>> msg)->void
      {
         promUtxo->set_value(msg.get());
      };

      wallet1.getUTXOs(UINT64_MAX, false, false, getUtxoLbd);
      std::vector<UTXO> utxosB, utxosC;
      {
         auto utxoVec = futUtxo.get();
         for (auto& utxo : utxoVec) {
            if (utxo.getRecipientScrAddr() == TestChain::scrAddrB) {
               utxosB.push_back(utxo);
            } else if (utxo.getRecipientScrAddr() == TestChain::scrAddrC) {
               utxosC.push_back(utxo);
            }
         }
      }

      ASSERT_FALSE(utxosB.empty());
      ASSERT_FALSE(utxosC.empty());

      /*create the transactions*/

      //grab utxo from raw tx
      auto getUtxoFromRawTx = [](BinaryData& rawTx, unsigned id)->UTXO
      {
         Tx tx(rawTx);
         if (id > tx.getNumTxOut()) {
            throw std::runtime_error("invalid txout count");
         }
         auto txOut = tx.getTxOutCopy(id);

         UTXO utxo;
         utxo.unserializeRaw(txOut.serialize());
         utxo.txOutIndex_ = id;
         utxo.txHash_ = tx.getThisHash();
         return utxo;
      };

      BinaryData rawTx1_B;
      {
         //20 from B, 5 to A, change to D
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20),
            spender->getValue() - recA->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1_B = signer.serializeSignedTx();
      }

      BinaryData rawTx1_C;
      {
         //20 from C, 5 to E, change to C
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrC.getSliceCopy(1, 20), 
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1_C = signer.serializeSignedTx();
      }

      BinaryData rawTx2;
      {
         auto utxoD = getUtxoFromRawTx(rawTx1_B, 1);

         //15 from D, 10 to E, change to F
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx2 = signer.serializeSignedTx();
      }

      BinaryData rawTx3;
      {
         auto utxoD = getUtxoFromRawTx(rawTx1_B, 1);

         //15 from D, 10 to E, change to A
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx3 = signer.serializeSignedTx();
      }
      
      Tx tx1_B(rawTx1_B);
      Tx tx1_C(rawTx1_C);
      Tx tx2(rawTx2);
      Tx tx3(rawTx3);

      {
         //push the first zc
         bdvObj->broadcastZC({rawTx1_B});

         //wait on notification
         pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), {tx1_B.getThisHash()});
      }

      //batch push all tx
      bdvObj->broadcastZC({ rawTx1_B, rawTx1_C, rawTx2, rawTx3 });

      std::set<BinaryData> txHashes {
         tx1_C.getThisHash(),
         tx2.getThisHash()
      };

      //wait on zc error for conflicting child
      pCallback->waitOnError(
         tx3.getThisHash(), ArmoryErrorCodes::ZcBroadcast_VerifyRejected);

      //wait on zc notifs
      pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), txHashes);

      //check balances
      combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 55 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 50 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 15 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 65 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 45 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 10 * COIN);
      }

      //disconnect
      bdvObj->unregisterFromDB();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, BatchZcChain_ConflictingChildren_AlreadyInChain2)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
            Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey, true);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(hexMagicBytes);

      auto wallet1 = bdvObj->getWalletObj("wallet1");
      std::vector<BinaryData> _scrAddrVec1 {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      wallet1.registerAddresses(_scrAddrVec1, false);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      //balance fetching routine
      auto getBalances = [bdvObj](void)->AsyncClient::CombinedBalances
      {
         auto promPtr = std::make_shared<std::promise<std::map<std::string, AsyncClient::CombinedBalances>>>();
         auto fut = promPtr->get_future();
         auto balLbd = [promPtr](
            ReturnMessage<std::map<std::string, AsyncClient::CombinedBalances>> combBal)->void
         {
            promPtr->set_value(combBal.get());
         };

         bdvObj->getCombinedBalances(balLbd);
         auto balMap = fut.get();
         if (balMap.size() != 1) {
            throw std::runtime_error("unexpected balance map size");
         }
         return balMap.begin()->second;
      };

      //check balances before pushing zc
      auto combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 50 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 70 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 20 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 65 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 30 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 5 * COIN);
      }
   
      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());
      feed->addPrivKey(TestChain::privKeyAddrF.getRef());

      //grab utxos for scrAddrB & scrAddrC
      auto promUtxo = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto futUtxo = promUtxo->get_future();
      auto getUtxoLbd = [promUtxo](ReturnMessage<std::vector<UTXO>> msg)->void
      {
         promUtxo->set_value(msg.get());
      };

      wallet1.getUTXOs(UINT64_MAX, false, false, getUtxoLbd);
      std::vector<UTXO> utxosB, utxosC;
      {
         auto utxoVec = futUtxo.get();
         for (auto& utxo : utxoVec) {
            if (utxo.getRecipientScrAddr() == TestChain::scrAddrB) {
               utxosB.push_back(utxo);
            } else if (utxo.getRecipientScrAddr() == TestChain::scrAddrC) {
               utxosC.push_back(utxo);
            }
         }
      }

      ASSERT_FALSE(utxosB.empty());
      ASSERT_FALSE(utxosC.empty());

      /*create the transactions*/

      //grab utxo from raw tx
      auto getUtxoFromRawTx = [](BinaryData& rawTx, unsigned id)->UTXO
      {
         Tx tx(rawTx);
         if (id > tx.getNumTxOut()) {
            throw std::runtime_error("invalid txout count");
         }

         auto txOut = tx.getTxOutCopy(id);
         UTXO utxo;
         utxo.unserializeRaw(txOut.serialize());
         utxo.txOutIndex_ = id;
         utxo.txHash_ = tx.getThisHash();

         return utxo;
      };

      BinaryData rawTx1_B;
      {
         //20 from B, 5 to A, change to D
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20),
            spender->getValue() - recA->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1_B = signer.serializeSignedTx();
      }

      BinaryData rawTx1_C;
      {
         //20 from C, 5 to E, change to C
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrC.getSliceCopy(1, 20), 
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1_C = signer.serializeSignedTx();
      }

      BinaryData rawTx2;
      {
         auto utxoD = getUtxoFromRawTx(rawTx1_B, 1);

         //15 from D, 10 to E, change to F
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx2 = signer.serializeSignedTx();
      }

      BinaryData rawTx3;
      {
         auto utxoD = getUtxoFromRawTx(rawTx1_B, 1);

         //15 from D, 10 to E, change to A
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx3 = signer.serializeSignedTx();
      }
      
      Tx tx1_B(rawTx1_B);
      Tx tx1_C(rawTx1_C);
      Tx tx2(rawTx2);
      Tx tx3(rawTx3);

      {
         std::set<BinaryData> txHashes {
            tx1_B.getThisHash(),
            tx2.getThisHash()
         };

         //push the first zc and its child through the node
         nodePtr_->pushZC({ {rawTx1_B, 0}, {rawTx2, 0} }, false);

         //wait on notification
         pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), txHashes);
      }

      //batch push first zc (already in chain), C (unrelated) 
      //and tx3 (child of first, mempool conflict with tx2)
      bdvObj->broadcastZC({ rawTx1_B, rawTx1_C, rawTx3 });
      std::set<BinaryData> txHashes {tx1_C.getThisHash()};

      //wait on zc error for conflicting child
      pCallback->waitOnError(
         tx3.getThisHash(), ArmoryErrorCodes::ZcBroadcast_VerifyRejected);

      //wait on zc notifs
      pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), txHashes);

      //check balances
      combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 55 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 50 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 15 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 65 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 45 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 10 * COIN);
      }

      //disconnect
      bdvObj->unregisterFromDB();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, BatchZcChain_ConflictingChildren_AlreadyInChain3)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
            Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey, true);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(hexMagicBytes);

      auto wallet1 = bdvObj->getWalletObj("wallet1");
      std::vector<BinaryData> _scrAddrVec1 {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      wallet1.registerAddresses(_scrAddrVec1, false);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      //balance fetching routine
      auto getBalances = [bdvObj](void)->AsyncClient::CombinedBalances
      {
         auto promPtr = std::make_shared<std::promise<std::map<std::string, AsyncClient::CombinedBalances>>>();
         auto fut = promPtr->get_future();
         auto balLbd = [promPtr](
            ReturnMessage<std::map<std::string, AsyncClient::CombinedBalances>> combBal)->void
         {
            promPtr->set_value(combBal.get());
         };

         bdvObj->getCombinedBalances(balLbd);
         auto balMap = fut.get();
         if (balMap.size() != 1) {
            throw std::runtime_error("unexpected balance map size");
         }
         return balMap.begin()->second;
      };

      //check balances before pushing zc
      auto combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 50 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 70 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 20 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 65 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 30 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 5 * COIN);
      }

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());
      feed->addPrivKey(TestChain::privKeyAddrF.getRef());

      //grab utxos for scrAddrB & scrAddrC
      auto promUtxo = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto futUtxo = promUtxo->get_future();
      auto getUtxoLbd = [promUtxo](ReturnMessage<std::vector<UTXO>> msg)->void
      {
         promUtxo->set_value(msg.get());
      };

      wallet1.getUTXOs(UINT64_MAX, false, false, getUtxoLbd);
      std::vector<UTXO> utxosB, utxosC;
      {
         auto utxoVec = futUtxo.get();
         for (auto& utxo : utxoVec) {
            if (utxo.getRecipientScrAddr() == TestChain::scrAddrB) {
               utxosB.push_back(utxo);
            } else if (utxo.getRecipientScrAddr() == TestChain::scrAddrC) {
               utxosC.push_back(utxo);
            }
         }
      }

      ASSERT_FALSE(utxosB.empty());
      ASSERT_FALSE(utxosC.empty());

      /*create the transactions*/

      //grab utxo from raw tx
      auto getUtxoFromRawTx = [](BinaryData& rawTx, unsigned id)->UTXO
      {
         Tx tx(rawTx);
         if (id > tx.getNumTxOut()) {
            throw std::runtime_error("invalid txout count");
         }
         auto txOut = tx.getTxOutCopy(id);

         UTXO utxo;
         utxo.unserializeRaw(txOut.serialize());
         utxo.txOutIndex_ = id;
         utxo.txHash_ = tx.getThisHash();
         return utxo;
      };

      BinaryData rawTx1_B;
      {
         //20 from B, 5 to A, change to D
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 
            spender->getValue() - recA->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1_B = signer.serializeSignedTx();
      }

      BinaryData rawTx1_C;
      {
         //20 from C, 5 to E, change to C
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrC.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1_C = signer.serializeSignedTx();
      }

      BinaryData rawTx2;
      {
         auto utxoD = getUtxoFromRawTx(rawTx1_B, 1);

         //15 from D, 10 to E, change to F
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20), 
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx2 = signer.serializeSignedTx();
      }

      BinaryData rawTx3;
      {
         auto utxoD = getUtxoFromRawTx(rawTx1_B, 1);
         auto utxoE = getUtxoFromRawTx(rawTx1_C, 0);

         //15+5 from D & E, 10 to E, change to A
         Signing::Signer signer;

         auto spender1 = std::make_shared<Signing::ScriptSpender>(utxoD);
         auto spender2 = std::make_shared<Signing::ScriptSpender>(utxoE);
         signer.addSpender(spender1);
         signer.addSpender(spender2);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx3 = signer.serializeSignedTx();
      }
      
      Tx tx1_B(rawTx1_B);
      Tx tx1_C(rawTx1_C);
      Tx tx2(rawTx2);
      Tx tx3(rawTx3);

      {
         std::set<BinaryData> txHashes {
            tx1_B.getThisHash(),
            tx2.getThisHash()
         };

         //push the first zc and its child
         bdvObj->broadcastZC({ rawTx1_B, rawTx2 });

         //wait on notification
         pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), txHashes);
      }

      //batch push first zc (already in chain), C (unrelated) 
      //and tx3 (child of first & C, mempool conflict with tx2 on utxo from first)
      bdvObj->broadcastZC({ rawTx1_B, rawTx1_C, rawTx3 });
      std::set<BinaryData> txHashes {tx1_C.getThisHash()};

      //wait on zc error for conflicting child
      pCallback->waitOnError(
         tx3.getThisHash(), ArmoryErrorCodes::ZcBroadcast_VerifyRejected);

      //wait on zc notifs
      pCallback->waitOnZc(theBDMt_->bdm()->zeroConfCont(), txHashes);

      //check balances
      combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 55 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 50 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 15 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 65 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 45 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 10 * COIN);
      }

      //disconnect
      bdvObj->unregisterFromDB();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, BroadcastAlreadyMinedTx)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
            Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey, true);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(hexMagicBytes);

      auto wallet1 = bdvObj->getWalletObj("wallet1");
      std::vector<BinaryData> _scrAddrVec1 {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      wallet1.registerAddresses(_scrAddrVec1, false);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      //grab a mined tx with unspent outputs
      auto ZC1 = TestUtils::getTx(5, 2); //block 5, tx 2
      auto ZChash1 = BtcUtils::getHash256(ZC1);

      //and one with spent outputs
      auto ZC2 = TestUtils::getTx(2, 1); //block 5, tx 2
      auto ZChash2 = BtcUtils::getHash256(ZC2);

      //try and broadcast both
      bdvObj->broadcastZC({ZC1, ZC2});

      //wait on zc errors
      pCallback->waitOnError(ZChash1,
         ArmoryErrorCodes::ZcBroadcast_AlreadyInChain);

      pCallback->waitOnError(ZChash2,
         ArmoryErrorCodes::ZcBroadcast_AlreadyInChain);

      //disconnect
      bdvObj->unregisterFromDB();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, BroadcastSameZC_ManyThreads)
{
   struct WSClient
   {
      std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr_;
      AsyncClient::BtcWallet wlt_;
      std::shared_ptr<DBTestUtils::UTCallback> callbackPtr_;

      WSClient(
         std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr,
         AsyncClient::BtcWallet& wlt,
         std::shared_ptr<DBTestUtils::UTCallback> callbackPtr) :
         bdvPtr_(bdvPtr), wlt_(std::move(wlt)), callbackPtr_(callbackPtr)
      {}
   };

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   //create BDV lambda
   auto setupBDV = [this, &serverPubkey](void)->std::shared_ptr<WSClient>
   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
            Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey, true);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(hexMagicBytes);

      auto wallet1 = bdvObj->getWalletObj("wallet1");
      std::vector<BinaryData> _scrAddrVec1 {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      wallet1.registerAddresses(_scrAddrVec1, false);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      auto client = std::make_shared<WSClient>(bdvObj, wallet1, pCallback);
      return client;
   };

   //create main bdv instance
   auto mainInstance = setupBDV();

   /*
   create a batch of zc with chains:
      1-2-3
      1-4 (4 conflicts with 2)
      5-6
      7
   */

   std::vector<BinaryData> rawTxVec, zcHashes;
   std::map<BinaryData, std::map<unsigned, UTXO>> outputMap;
   {
      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());
      feed->addPrivKey(TestChain::privKeyAddrF.getRef());

      //utxo from raw tx lambda
      auto getUtxoFromRawTx = [&outputMap](BinaryData& rawTx, unsigned id)->UTXO
      {
         Tx tx(rawTx);
         if (id > tx.getNumTxOut()) {
            throw std::runtime_error("invalid txout count");
         }
         auto txOut = tx.getTxOutCopy(id);

         UTXO utxo;
         utxo.unserializeRaw(txOut.serialize());
         utxo.txOutIndex_ = id;
         utxo.txHash_ = tx.getThisHash();

         auto& idMap = outputMap[utxo.txHash_];
         idMap[id] = utxo;
         return utxo;
      };

      //grab utxos for scrAddrB, scrAddrC, scrAddrE
      auto promUtxo = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto futUtxo = promUtxo->get_future();
      auto getUtxoLbd = [promUtxo](ReturnMessage<std::vector<UTXO>> msg)->void
      {
         promUtxo->set_value(msg.get());
      };

      mainInstance->wlt_.getUTXOs(UINT64_MAX, false, false, getUtxoLbd);
      std::vector<UTXO> utxosB, utxosC, utxosE;
      {
         auto utxoVec = futUtxo.get();
         for (auto& utxo : utxoVec) {
            if (utxo.getRecipientScrAddr() == TestChain::scrAddrB) {
               utxosB.push_back(utxo);
            }
            else if (utxo.getRecipientScrAddr() == TestChain::scrAddrC) {
               utxosC.push_back(utxo);
            }
            else if (utxo.getRecipientScrAddr() == TestChain::scrAddrE) {
               utxosE.push_back(utxo);
            }

            auto& idMap = outputMap[utxo.txHash_];
            idMap[utxo.txOutIndex_] = utxo;
         }
      }

      ASSERT_FALSE(utxosB.empty());
      ASSERT_FALSE(utxosC.empty());
      ASSERT_FALSE(utxosE.empty());

      /*create the transactions*/

      //1
      {
         //20 from B, 5 to A, change to D
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 
            spender->getValue() - recA->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }

      //2
      {
         auto utxoD = getUtxoFromRawTx(rawTxVec[0], 1);

         //15 from D, 10 to E, change to F
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }
      
      //3
      {
         auto utxoF = getUtxoFromRawTx(rawTxVec[1], 1);

         //5 from F, 5 to B
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoF);
         signer.addSpender(spender);

         auto recB = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrB.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recB);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }

      //4
      {
         auto utxoA = getUtxoFromRawTx(rawTxVec[0], 1);

         //15 from D, 14 to C
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoA);
         signer.addSpender(spender);

         auto recC = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrC.getSliceCopy(1, 20), 14 * COIN);
         signer.addRecipient(recC);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }

      //5
      {
         //10 from C, 10 to D
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recD = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recD);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }

      //6
      {
         auto utxoD = getUtxoFromRawTx(rawTxVec[4], 0);

         //10 from D, 5 to F, change to A
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recF = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recF);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20),
            spender->getValue() - recF->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }

      //7
      {
         //20 from E, 10 to F, change to A
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosE[0]);
         signer.addSpender(spender);

         auto recF = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recF);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20),
            spender->getValue() - recF->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }
   }

   //3 case1, 3 case2, 1 case3, 3 case4, 3 case5
   unsigned N = 13;

   //create N side instances
   std::vector<std::shared_ptr<WSClient>> sideInstances;
   for (unsigned i=0; i<N; i++) {
      sideInstances.emplace_back(setupBDV());
   }

   //get addresses for tx lambda
   auto getAddressesForRawTx = [&outputMap](const Tx& tx)->std::set<BinaryData>
   {
      std::set<BinaryData> addrSet;
      for (unsigned i=0; i<tx.getNumTxIn(); i++) {
         auto txin = tx.getTxInCopy(i);
         auto op = txin.getOutPoint();

         auto hashIter = outputMap.find(op.getTxHash());
         EXPECT_TRUE(hashIter != outputMap.end());

         auto idIter = hashIter->second.find(op.getTxOutIndex());
         EXPECT_TRUE(idIter != hashIter->second.end());

         auto& utxo = idIter->second;
         addrSet.insert(utxo.getRecipientScrAddr());
      }

      for (unsigned i=0; i<tx.getNumTxOut(); i++) {
         auto txout = tx.getTxOutCopy(i);
         addrSet.insert(txout.getScrAddress());
      }
      return addrSet;
   };

   std::set<BinaryData> mainScrAddrSet;
   std::set<BinaryData> mainHashes;
   {
      std::vector<unsigned> zcIds = {1, 2, 3, 5, 6};
      for (auto& id : zcIds) {
         Tx tx(rawTxVec[id - 1]);
         mainHashes.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         mainScrAddrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }
   }

   //case 1
   auto case1 = [&](unsigned instanceId)->void
   {
      auto instance = sideInstances[instanceId];

      //push 1-2-3
      std::vector<unsigned> zcIds = {1, 2, 3};

      //ids for the zc we are not broadcasting but which addresses we watch
      std::vector<unsigned> zcIds_skipped = {5, 6};

      std::vector<BinaryData> zcs;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      instance->bdvPtr_->broadcastZC(zcs);
      instance->callbackPtr_->waitOnZc(theBDMt_->bdm()->zeroConfCont(), hashSet);

      //wait on broadcast errors
      std::map<BinaryData, ArmoryErrorCodes> errorMap;
      for (const auto& id : zcIds) {
         errorMap.emplace(zcHashes[id - 1], ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);
      }
      instance->callbackPtr_->waitOnErrors(errorMap);
   };

   //case 2
   auto case2 = [&](unsigned instanceId)->void
   {
      auto instance = sideInstances[instanceId];

      //push 5-6
      std::vector<unsigned> zcIds = {5, 6};
      std::vector<unsigned> zcIds_skipped = {1, 2, 3};

      std::vector<BinaryData> zcs;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
      }

      instance->bdvPtr_->broadcastZC(zcs);
      instance->callbackPtr_->waitOnZc(theBDMt_->bdm()->zeroConfCont(), hashSet);

      //wait on broadcast errors
      std::map<BinaryData, ArmoryErrorCodes> errorMap;
      for (const auto& id : zcIds) {
         errorMap.emplace(zcHashes[id - 1], ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);
      }
      instance->callbackPtr_->waitOnErrors(errorMap);
   };

   //case 3
   auto case3 = [&](unsigned instanceId)->void
   {
      auto instance = sideInstances[instanceId];

      //push 1, 4, 7
      instance->bdvPtr_->broadcastZC({
         rawTxVec[0], rawTxVec[3],
         rawTxVec[6]
      });

      //don't grab 4 as it can't broadcast
      std::vector<unsigned> zcIds = {1, 7};
      std::vector<unsigned> zcIds_skipped = {2, 3, 5, 6};

      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      //wait on zc
      instance->callbackPtr_->waitOnZc(theBDMt_->bdm()->zeroConfCont(), hashSet);

      //wait on broadcast errors
      instance->callbackPtr_->waitOnError(
         zcHashes[0], ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);

      instance->callbackPtr_->waitOnError(
         zcHashes[3], ArmoryErrorCodes::ZcBroadcast_VerifyRejected);
   };

   //case 4
   auto case4 = [&](unsigned instanceId)->void
   {
      auto instance = sideInstances[instanceId];

      //push 5-6 7
      std::vector<unsigned> zcIds = {5, 6, 7};
      std::vector<unsigned> zcIds_skipped = {1, 2, 3};

      std::vector<BinaryData> zcs;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      instance->bdvPtr_->broadcastZC(zcs);
      instance->callbackPtr_->waitOnZc(theBDMt_->bdm()->zeroConfCont(), hashSet);

      //wait on broadcast errors
      std::map<BinaryData, ArmoryErrorCodes> errorMap;
      for (const auto& id : zcIds) {
         errorMap.emplace(zcHashes[id - 1], ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);
      }
      instance->callbackPtr_->waitOnErrors(errorMap);
   };

   //case 5
   auto case5 = [&](unsigned instanceId)->void
   {
      auto instance = sideInstances[instanceId];

      //push 4 5-6
      instance->bdvPtr_->broadcastZC({
         rawTxVec[3],
         rawTxVec[4], rawTxVec[5]
      });

      //skip 4 as it can't broadcast
      std::vector<unsigned> zcIds = {5, 6};
      std::vector<unsigned> zcIds_skipped = {1, 2, 3};

      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      //wait on zc
      instance->callbackPtr_->waitOnZc(theBDMt_->bdm()->zeroConfCont(), hashSet);

      //wait on broadcast errors
      std::map<BinaryData, ArmoryErrorCodes> errorMap;
      for (const auto& id : zcIds) {
         errorMap.emplace(zcHashes[id - 1], ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);
      }
      instance->callbackPtr_->waitOnErrors(errorMap);
      instance->callbackPtr_->waitOnError(
         zcHashes[3], ArmoryErrorCodes::ZcBroadcast_VerifyRejected);
   };

   //main instance
   {
      //set zc inv delay, this will allow for batches in side jobs to
      //collide with the original one
      nodePtr_->stallNextZc(3); //in seconds

      //push 1-2-3 & 5-6
      std::vector<unsigned> zcIds = {1, 2, 3, 5, 6};

      std::vector<BinaryData> zcs;
      std::set<BinaryData> hashes;
      for (auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(zcs.back());
         hashes.insert(tx.getThisHash());
      }

      mainInstance->bdvPtr_->broadcastZC(zcs);
      /*
      delay for 1 second before starting side jobs to make sure the
      primary broadcast is first in line
      */
      std::this_thread::sleep_for(1s);

      //start the side jobs
      std::vector<std::thread> threads;
      for (unsigned i=0; i<3; i++) {
         threads.push_back(std::thread(case1, i));
      }

      for (unsigned i=3; i<6; i++) {
         threads.push_back(std::thread(case2, i));
      }

      //needs case3 to broadcast before case 4
      threads.push_back(std::thread(case3, 6));
      std::this_thread::sleep_for(500ms);

      for (unsigned i=7; i<10; i++) {
         threads.push_back(std::thread(case4, i));
      }

      for (unsigned i=10; i<13; i++) {
         threads.push_back(std::thread(case5, i));
      }

      //wait on zc
      mainInstance->callbackPtr_->waitOnZc(theBDMt_->bdm()->zeroConfCont(), hashes);

      //wait on side jobs
      for (auto& thr : threads) {
         if (thr.joinable()) {
            thr.join();
         }
      }

      //done
      mainInstance->bdvPtr_->unregisterFromDB();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, BroadcastSameZC_ManyThreads_RPCFallback)
{
   struct WSClient
   {
      std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr_;
      AsyncClient::BtcWallet wlt_;
      std::shared_ptr<DBTestUtils::UTCallback> callbackPtr_;

      WSClient(
         std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr,
         AsyncClient::BtcWallet& wlt,
         std::shared_ptr<DBTestUtils::UTCallback> callbackPtr) :
         bdvPtr_(bdvPtr), wlt_(std::move(wlt)), callbackPtr_(callbackPtr)
      {}
   };

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   //create BDV lambda
   auto setupBDV = [this, &serverPubkey](void)->std::shared_ptr<WSClient>
   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
            Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey, true);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(hexMagicBytes);

      auto wallet1 = bdvObj->getWalletObj("wallet1");
      std::vector<BinaryData> _scrAddrVec1 {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      wallet1.registerAddresses(_scrAddrVec1, false);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      auto client = std::make_shared<WSClient>(bdvObj, wallet1, pCallback);
      return client;
   };

   //create main bdv instance
   auto mainInstance = setupBDV();

   /*
   create a batch of zc with chains:
      1-2-3
      1-4 (4 conflicts with 2)
      5-6
      7
   */

   std::vector<BinaryData> rawTxVec, zcHashes;
   std::map<BinaryData, std::map<unsigned, UTXO>> outputMap;
   {
      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());
      feed->addPrivKey(TestChain::privKeyAddrF.getRef());

      //utxo from raw tx lambda
      auto getUtxoFromRawTx = [&outputMap](BinaryData& rawTx, unsigned id)->UTXO
      {
         Tx tx(rawTx);
         if (id > tx.getNumTxOut()) {
            throw std::runtime_error("invalid txout count");
         }
         auto txOut = tx.getTxOutCopy(id);

         UTXO utxo;
         utxo.unserializeRaw(txOut.serialize());
         utxo.txOutIndex_ = id;
         utxo.txHash_ = tx.getThisHash();

         auto& idMap = outputMap[utxo.txHash_];
         idMap[id] = utxo;
         return utxo;
      };

      //grab utxos for scrAddrB, scrAddrC, scrAddrE
      auto promUtxo = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto futUtxo = promUtxo->get_future();
      auto getUtxoLbd = [promUtxo](ReturnMessage<std::vector<UTXO>> msg)->void
      {
         promUtxo->set_value(msg.get());
      };

      mainInstance->wlt_.getUTXOs(UINT64_MAX, false, false, getUtxoLbd);
      std::vector<UTXO> utxosB, utxosC, utxosE;
      {
         auto&& utxoVec = futUtxo.get();
         for (auto& utxo : utxoVec) {
            if (utxo.getRecipientScrAddr() == TestChain::scrAddrB) {
               utxosB.push_back(utxo);
            } else if (utxo.getRecipientScrAddr() == TestChain::scrAddrC) {
               utxosC.push_back(utxo);
            } else if (utxo.getRecipientScrAddr() == TestChain::scrAddrE) {
               utxosE.push_back(utxo);
            }

            auto& idMap = outputMap[utxo.txHash_];
            idMap[utxo.txOutIndex_] = utxo;
         }
      }

      ASSERT_FALSE(utxosB.empty());
      ASSERT_FALSE(utxosC.empty());
      ASSERT_FALSE(utxosE.empty());

      /*create the transactions*/

      //1
      {
         //20 from B, 5 to A, change to D
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20),
            spender->getValue() - recA->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }

      //2
      {
         auto utxoD = getUtxoFromRawTx(rawTxVec[0], 1);

         //15 from D, 10 to E, change to F
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }

      //3
      {
         auto utxoF = getUtxoFromRawTx(rawTxVec[1], 1);

         //5 from F, 5 to B
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoF);
         signer.addSpender(spender);

         auto recB = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrB.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recB);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }

      //4
      {
         auto utxoA = getUtxoFromRawTx(rawTxVec[0], 1);

         //15 from D, 14 to C
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoA);
         signer.addSpender(spender);

         auto recC = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrC.getSliceCopy(1, 20), 14 * COIN);
         signer.addRecipient(recC);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }

      //5
      {
         //10 from C, 10 to D
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recD = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recD);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }

      //6
      {
         auto utxoD = getUtxoFromRawTx(rawTxVec[4], 0);

         //10 from D, 5 to F, change to A
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recF = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recF);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20),
            spender->getValue() - recF->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }

      //7
      {
         //20 from E, 10 to F, change to A
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosE[0]);
         signer.addSpender(spender);

         auto recF = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recF);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20),
            spender->getValue() - recF->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }
   }

   //3 case1, 3 case2, 1 case3, 3 case4, 3 case5
   unsigned N = 13;

   //create N side instances
   std::vector<std::shared_ptr<WSClient>> sideInstances;
   for (unsigned i=0; i<N; i++) {
      sideInstances.emplace_back(setupBDV());
   }

   //get addresses for tx lambda
   auto getAddressesForRawTx = [&outputMap](const Tx& tx)->std::set<BinaryData>
   {
      std::set<BinaryData> addrSet;
      for (unsigned i=0; i < tx.getNumTxIn(); i++) {
         auto txin = tx.getTxInCopy(i);
         auto op = txin.getOutPoint();

         auto hashIter = outputMap.find(op.getTxHash());
         EXPECT_TRUE(hashIter != outputMap.end());

         auto idIter = hashIter->second.find(op.getTxOutIndex());
         EXPECT_TRUE(idIter != hashIter->second.end());

         auto& utxo = idIter->second;
         addrSet.insert(utxo.getRecipientScrAddr());
      }

      for (unsigned i=0; i<tx.getNumTxOut(); i++) {
         auto txout = tx.getTxOutCopy(i);
         addrSet.insert(txout.getScrAddress());
      }
      return addrSet;
   };

   std::set<BinaryData> mainScrAddrSet;
   std::set<BinaryData> mainHashes;
   {
      std::vector<unsigned> zcIds = {1, 2, 3, 5, 6};
      for (auto& id : zcIds) {
         Tx tx(rawTxVec[id - 1]);
         mainHashes.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         mainScrAddrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }
   }

   //case 1
   auto case1 = [&](unsigned instanceId)->void
   {
      auto instance = sideInstances[instanceId];

      //push 1-2-3
      std::vector<unsigned> zcIds = {1, 2, 3};

      //ids for the zc we are not broadcasting but which addresses we watch
      std::vector<unsigned> zcIds_skipped = {5, 6};

      std::vector<BinaryData> zcs;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }
      instance->bdvPtr_->broadcastZC(zcs);

      //wait on zc
      instance->callbackPtr_->waitOnZc(theBDMt_->bdm()->zeroConfCont(), hashSet);

      //wait on broadcast errors
      std::map<BinaryData, ArmoryErrorCodes> errorMap;
      for (auto& id : zcIds) {
         errorMap.emplace(zcHashes[id - 1], ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);
      }
      instance->callbackPtr_->waitOnErrors(errorMap);
   };

   //case 2
   auto case2 = [&](unsigned instanceId)->void
   {
      auto instance = sideInstances[instanceId];

      //push 5-6
      std::vector<unsigned> zcIds = {5, 6};
      std::vector<unsigned> zcIds_skipped = {1, 2, 3};

      std::vector<BinaryData> zcs;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      instance->bdvPtr_->broadcastZC(zcs);

      //wait on zc
      instance->callbackPtr_->waitOnZc(theBDMt_->bdm()->zeroConfCont(), hashSet);

      //wait on broadcast errors
      std::map<BinaryData, ArmoryErrorCodes> errorMap;
      for (const auto& id : zcIds) {
         errorMap.emplace(zcHashes[id - 1], ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);
      }
      instance->callbackPtr_->waitOnErrors(errorMap);
   };

   //case 3
   auto case3 = [&](unsigned instanceId)->void
   {
      auto instance = sideInstances[instanceId];

      //push 1-4 7
      instance->bdvPtr_->broadcastZC({
         rawTxVec[0], rawTxVec[3],
         rawTxVec[6]
      });

      //don't grab 4 as it can't broadcast
      std::vector<unsigned> zcIds = {1, 7};
      std::vector<unsigned> zcIds_skipped = {2, 3, 5, 6};

      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      //wait on zc
      instance->callbackPtr_->waitOnZc_OutOfOrder(theBDMt_->bdm()->zeroConfCont(), hashSet);

      //wait on broadcast errors
      instance->callbackPtr_->waitOnError(
         zcHashes[0], ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);

      instance->callbackPtr_->waitOnError(
         zcHashes[3], ArmoryErrorCodes::ZcBroadcast_VerifyRejected);
   };

   //case 4
   auto case4 = [&](unsigned instanceId)->void
   {
      auto instance = sideInstances[instanceId];

      //push 5-6 7
      std::vector<unsigned> zcIds = {5, 6, 7};
      std::vector<unsigned> zcIds_skipped = {1, 2, 3};

      std::vector<BinaryData> zcs;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      instance->bdvPtr_->broadcastZC(zcs);
      instance->callbackPtr_->waitOnZc(theBDMt_->bdm()->zeroConfCont(), hashSet);

      //wait on broadcast errors
      std::map<BinaryData, ArmoryErrorCodes> errorMap;
      for (const auto& id : zcIds) {
         errorMap.emplace(zcHashes[id - 1], ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);
      }
      instance->callbackPtr_->waitOnErrors(errorMap);
   };

   //case 5
   auto case5 = [&](unsigned instanceId)->void
   {
      auto instance = sideInstances[instanceId];

      //push 4 5-6
      instance->bdvPtr_->broadcastZC({
         rawTxVec[3],
         rawTxVec[4], rawTxVec[5]
      });

      //skip 4 as it can't broadcast
      std::vector<unsigned> zcIds = {5, 6};
      std::vector<unsigned> zcIds_skipped = {1, 2, 3};

      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }

      instance->callbackPtr_->waitOnZc(theBDMt_->bdm()->zeroConfCont(), hashSet);

      //wait on broadcast errors
      std::map<BinaryData, ArmoryErrorCodes> errorMap;
      for (const auto& id : zcIds) {
         errorMap.emplace(zcHashes[id - 1], ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);
      }
      instance->callbackPtr_->waitOnErrors(errorMap);
      instance->callbackPtr_->waitOnError(
         zcHashes[3], ArmoryErrorCodes::ZcBroadcast_VerifyRejected);
   };

   //main instance
   {
      //skip all zc to force a RPC fallback
      nodePtr_->skipZc(100000);

      //push 1-2-3 & 5-6
      std::vector<unsigned> zcIds = {1, 2, 3, 5, 6};

      std::vector<BinaryData> zcs;
      std::set<BinaryData> hashes;
      for (auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(zcs.back());
         hashes.insert(tx.getThisHash());
      }
      mainInstance->bdvPtr_->broadcastZC(zcs);

      /*
      delay for 1 second before starting side jobs to make sure the 
      primary broadcast is first in line
      */
      std::this_thread::sleep_for(1s);

      //start the side jobs
      std::vector<std::thread> threads;
      for (unsigned i=0; i<3; i++) {
         threads.push_back(std::thread(case1, i));
      }

      for (unsigned i=3; i<6; i++) {
         threads.push_back(std::thread(case2, i));
      }

      //needs case3 to broadcast before case 4
      threads.push_back(std::thread(case3, 6));
      std::this_thread::sleep_for(500ms);

      for (unsigned i=7; i<10; i++) {
         threads.push_back(std::thread(case4, i));
      }

      for (unsigned i=10; i<13; i++) {
         threads.push_back(std::thread(case5, i));
      }

      //wait on zc
      mainInstance->callbackPtr_->waitOnZc(theBDMt_->bdm()->zeroConfCont(), hashes);

      //wait on side jobs
      for (auto& thr : threads) {
         if (thr.joinable()) {
            thr.join();
         }
      }

      //done
      mainInstance->bdvPtr_->unregisterFromDB();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, BroadcastSameZC_RPCThenP2P)
{
   struct WSClient
   {
      std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr_;
      AsyncClient::BtcWallet wlt_;
      std::shared_ptr<DBTestUtils::UTCallback> callbackPtr_;

      WSClient(
         std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr, 
         AsyncClient::BtcWallet& wlt,
         std::shared_ptr<DBTestUtils::UTCallback> callbackPtr) :
         bdvPtr_(bdvPtr), wlt_(std::move(wlt)), callbackPtr_(callbackPtr)
      {}
   };

   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   //create BDV lambda
   auto setupBDV = [this, &serverPubkey](void)->std::shared_ptr<WSClient>
   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
            Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey, true);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(hexMagicBytes);

      auto wallet1 = bdvObj->getWalletObj("wallet1");
      std::vector<BinaryData> _scrAddrVec1 {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      wallet1.registerAddresses(_scrAddrVec1, false);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      auto client = std::make_shared<WSClient>(bdvObj, wallet1, pCallback);
      return client;
   };

   //create main bdv instance
   auto mainInstance = setupBDV();

   /*
   create a batch of zc with chains:
      1-2
      3
   */

   std::vector<BinaryData> rawTxVec, zcHashes;
   std::map<BinaryData, std::map<unsigned, UTXO>> outputMap;
   {
      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());
      feed->addPrivKey(TestChain::privKeyAddrF.getRef());

      //utxo from raw tx lambda
      auto getUtxoFromRawTx = [&outputMap](BinaryData& rawTx, unsigned id)->UTXO
      {
         Tx tx(rawTx);
         if (id > tx.getNumTxOut()) {
            throw std::runtime_error("invalid txout count");
         }
         auto txOut = tx.getTxOutCopy(id);

         UTXO utxo;
         utxo.unserializeRaw(txOut.serialize());
         utxo.txOutIndex_ = id;
         utxo.txHash_ = tx.getThisHash();

         auto& idMap = outputMap[utxo.txHash_];
         idMap[id] = utxo;
         return utxo;
      };

      //grab utxos for scrAddrB, scrAddrC, scrAddrE
      auto promUtxo = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto futUtxo = promUtxo->get_future();
      auto getUtxoLbd = [promUtxo](ReturnMessage<std::vector<UTXO>> msg)->void
      {
         promUtxo->set_value(msg.get());
      };

      mainInstance->wlt_.getUTXOs(UINT64_MAX, false, false, getUtxoLbd);
      std::vector<UTXO> utxosB, utxosC, utxosE;
      {
         auto utxoVec = futUtxo.get();
         for (auto& utxo : utxoVec) {
            if (utxo.getRecipientScrAddr() == TestChain::scrAddrB) {
               utxosB.push_back(utxo);
            } else if (utxo.getRecipientScrAddr() == TestChain::scrAddrC) {
               utxosC.push_back(utxo);
            } else if (utxo.getRecipientScrAddr() == TestChain::scrAddrE) {
               utxosE.push_back(utxo);
            }

            auto& idMap = outputMap[utxo.txHash_];
            idMap[utxo.txOutIndex_] = utxo;
         }
      }

      ASSERT_FALSE(utxosB.empty());
      ASSERT_FALSE(utxosC.empty());
      ASSERT_FALSE(utxosE.empty());

      /*create the transactions*/

      //1
      {
         //20 from B, 5 to A, change to D
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20),
            spender->getValue() - recA->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }

      //2
      {
         auto utxoD = getUtxoFromRawTx(rawTxVec[0], 1);

         //15 from D, 10 to E, change to F
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }

      //3
      {
         //20 from E, 10 to F, change to A
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosE[0]);
         signer.addSpender(spender);

         auto recF = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recF);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20),
            spender->getValue() - recF->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTxVec.push_back(signer.serializeSignedTx());
         Tx tx(rawTxVec.back());
         zcHashes.push_back(tx.getThisHash());
      }
   }

   unsigned N = 1;

   //create N side instances
   std::vector<std::shared_ptr<WSClient>> sideInstances;
   for (unsigned i=0; i<N; i++) {
      sideInstances.emplace_back(setupBDV());
   }

   //get addresses for tx lambda
   auto getAddressesForRawTx = [&outputMap](const Tx& tx)->std::set<BinaryData>
   {
      std::set<BinaryData> addrSet;
      for (unsigned i=0; i<tx.getNumTxIn(); i++) {
         auto txin = tx.getTxInCopy(i);
         auto op = txin.getOutPoint();

         auto hashIter = outputMap.find(op.getTxHash());
         EXPECT_TRUE(hashIter != outputMap.end());

         auto idIter = hashIter->second.find(op.getTxOutIndex());
         EXPECT_TRUE(idIter != hashIter->second.end());

         auto& utxo = idIter->second;
         addrSet.insert(utxo.getRecipientScrAddr());
      }

      for (unsigned i=0; i<tx.getNumTxOut(); i++) {
         auto txout = tx.getTxOutCopy(i);
         addrSet.insert(txout.getScrAddress());
      }

      return addrSet;
   };

   std::set<BinaryData> mainScrAddrSet;
   std::set<BinaryData> mainHashes;
   {
      std::vector<unsigned> zcIds = {1, 2};
      for (auto& id : zcIds) {
         Tx tx(rawTxVec[id - 1]);
         mainHashes.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         mainScrAddrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }
   }

   //case 1
   auto case1 = [&](unsigned instanceId)->void
   {
      auto instance = sideInstances[instanceId];

      //push 1-2, 3
      std::vector<unsigned> zcIds = {1, 2, 3};

      std::vector<BinaryData> zcs;
      std::set<BinaryData> hashSet;
      for (auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
      }
      instance->bdvPtr_->broadcastZC(zcs);
      instance->callbackPtr_->waitOnZc(theBDMt_->bdm()->zeroConfCont(), hashSet);

      //wait on broadcast errors
      std::map<BinaryData, ArmoryErrorCodes> errorMap;
      errorMap.emplace(zcHashes[0], ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);
      instance->callbackPtr_->waitOnErrors(errorMap);
   };

   //main instance
   {
      //set RPC, this will allow for batches in side jobs to 
      //collide with the original one
      rpcNode_->stallNextZc(3); //in seconds

      //push 1-2

      BinaryData hash1, hash2;
      {
         Tx tx(rawTxVec[0]);
         hash1 = tx.getThisHash();
      }

      {
         Tx tx(rawTxVec[1]);
         hash2 = tx.getThisHash();
      }

      mainInstance->bdvPtr_->broadcastThroughRPC(rawTxVec[0]);
      mainInstance->bdvPtr_->broadcastThroughRPC(rawTxVec[1]);

      /*
      delay for 1 second before starting side jobs to make sure the 
      primary broadcast is first in line
      */
      std::this_thread::sleep_for(1s);

      //start the side jobs
      std::vector<std::thread> threads;
      threads.emplace_back(std::thread(case1, 0));

      //wait on zc
      mainInstance->callbackPtr_->waitOnZc(theBDMt_->bdm()->zeroConfCont(), {hash1});
      mainInstance->callbackPtr_->waitOnZc(theBDMt_->bdm()->zeroConfCont(), {hash2});

      //wait on side jobs
      for (auto& thr : threads) {
         if (thr.joinable()) {
            thr.join();
         }
      }

      //done
      mainInstance->bdvPtr_->unregisterFromDB();
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode_WebSocket, RebroadcastInvalidBatch)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   WebSocketServer::initAuthPeers({
      homedir_ / SERVER_AUTH_PEER_FILENAME, authPeersPassLbd_});
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(Config::DBSettings::initMode());
   theBDMt_->bdm()->blockUntilReady();

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", Config::NetworkSettings::dbPort(),
         std::make_shared<Wallets::AuthorizedPeers>(Wallets::IO::ReadOnlyFileParams{
            Config::getDataDir() / CLIENT_AUTH_PEER_FILENAME, authPeersPassLbd_}),
         true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey, true);
      bdvObj->connectToRemote();
      bdvObj->registerWithDB(hexMagicBytes);

      auto wallet1 = bdvObj->getWalletObj("wallet1");
      std::vector<BinaryData> _scrAddrVec1 {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };
      wallet1.registerAddresses(_scrAddrVec1, false);

      //go online
      bdvObj->goOnline();
      pCallback->waitOnSignal(BDMAction_Ready);

      //balance fetching routine
      auto getBalances = [bdvObj](void)->AsyncClient::CombinedBalances
      {
         auto promPtr = std::make_shared<std::promise<std::map<std::string, AsyncClient::CombinedBalances>>>();
         auto fut = promPtr->get_future();
         auto balLbd = [promPtr](
            ReturnMessage<std::map<std::string, AsyncClient::CombinedBalances>> combBal)->void
         {
            promPtr->set_value(combBal.get());
         };

         bdvObj->getCombinedBalances(balLbd);
         auto balMap = fut.get();

         if (balMap.size() != 1) {
            throw std::runtime_error("unexpected balance map size");
         }
         return balMap.begin()->second;
      };

      //check balances before pushing zc
      auto combineBalances = getBalances();
      EXPECT_EQ(combineBalances.addressBalances.size(), 6ULL);

      {
         auto iterA = combineBalances.addressBalances.find(TestChain::scrAddrA);
         ASSERT_NE(iterA, combineBalances.addressBalances.end());
         ASSERT_EQ(iterA->second.size(), 4ULL);
         EXPECT_EQ(iterA->second[0], 50 * COIN);

         auto iterB = combineBalances.addressBalances.find(TestChain::scrAddrB);
         ASSERT_NE(iterB, combineBalances.addressBalances.end());
         ASSERT_EQ(iterB->second.size(), 4ULL);
         EXPECT_EQ(iterB->second[0], 70 * COIN);

         auto iterC = combineBalances.addressBalances.find(TestChain::scrAddrC);
         ASSERT_NE(iterC, combineBalances.addressBalances.end());
         ASSERT_EQ(iterC->second.size(), 4ULL);
         EXPECT_EQ(iterC->second[0], 20 * COIN);

         auto iterD = combineBalances.addressBalances.find(TestChain::scrAddrD);
         ASSERT_NE(iterD, combineBalances.addressBalances.end());
         ASSERT_EQ(iterD->second.size(), 4ULL);
         EXPECT_EQ(iterD->second[0], 65 * COIN);

         auto iterE = combineBalances.addressBalances.find(TestChain::scrAddrE);
         ASSERT_NE(iterE, combineBalances.addressBalances.end());
         ASSERT_EQ(iterE->second.size(), 4ULL);
         EXPECT_EQ(iterE->second[0], 30 * COIN);

         auto iterF = combineBalances.addressBalances.find(TestChain::scrAddrF);
         ASSERT_NE(iterF, combineBalances.addressBalances.end());
         ASSERT_EQ(iterF->second.size(), 4ULL);
         EXPECT_EQ(iterF->second[0], 5 * COIN);
      }

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB.getRef());
      feed->addPrivKey(TestChain::privKeyAddrC.getRef());
      feed->addPrivKey(TestChain::privKeyAddrD.getRef());
      feed->addPrivKey(TestChain::privKeyAddrE.getRef());
      feed->addPrivKey(TestChain::privKeyAddrF.getRef());

      //grab utxos for scrAddrB
      auto promUtxo = std::make_shared<std::promise<std::vector<UTXO>>>();
      auto futUtxo = promUtxo->get_future();
      auto getUtxoLbd = [promUtxo](ReturnMessage<std::vector<UTXO>> msg)->void
      {
         promUtxo->set_value(msg.get());
      };

      wallet1.getUTXOs(UINT64_MAX, false, false, getUtxoLbd);
      std::vector<UTXO> utxosB;
      {
         auto utxoVec = futUtxo.get();
         for (auto& utxo : utxoVec) {
            if (utxo.getRecipientScrAddr() != TestChain::scrAddrB) {
               continue;
            }
            utxosB.push_back(utxo);
         }
      }

      ASSERT_FALSE(utxosB.empty());

      /*create the transactions*/

      //grab utxo from raw tx
      auto getUtxoFromRawTx = [](BinaryData& rawTx, unsigned id)->UTXO
      {
         Tx tx(rawTx);
         if (id > tx.getNumTxOut()) {
            throw std::runtime_error("invalid txout count");
         }
         auto txOut = tx.getTxOutCopy(id);

         UTXO utxo;
         utxo.unserializeRaw(txOut.serialize());
         utxo.txOutIndex_ = id;
         utxo.txHash_ = tx.getThisHash();
         return utxo;
      };

      BinaryData rawTx1, rawTx2;

      {
         //20 from B, 5 to A, change to D
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20),
            spender->getValue() - recA->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx1 = signer.serializeSignedTx();
      }

      {
         auto utxoD = getUtxoFromRawTx(rawTx1, 1);

         //15 from D, 10 to E, change to F
         Signing::Signer signer;

         auto spender = std::make_shared<Signing::ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20),
            spender->getValue() - recE->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx2 = signer.serializeSignedTx();
      }

      BinaryData rawTx3;
      {
         //10 from E, 5 from F, 3 to A, 2 to E, 5 to D, change to C
         auto zcUtxo1 = getUtxoFromRawTx(rawTx2, 0);
         auto zcUtxo2 = getUtxoFromRawTx(rawTx2, 1);

         Signing::Signer signer;

         auto spender1 = std::make_shared<Signing::ScriptSpender>(zcUtxo1);
         auto spender2 = std::make_shared<Signing::ScriptSpender>(zcUtxo2);
         signer.addSpender(spender1);
         signer.addSpender(spender2);

         auto recA = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 3 * COIN);
         signer.addRecipient(recA);

         auto recE = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 2 * COIN);
         signer.addRecipient(recE);
         
         auto recD = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recD);

         auto recChange = std::make_shared<Signing::Recipient_P2PKH>(
            TestChain::scrAddrC.getSliceCopy(1, 20),
            spender1->getValue() + spender2->getValue() -
            recA->getValue() - recE->getValue() - recD->getValue());
         signer.addRecipient(recChange);

         signer.setFeed(feed);
         signer.sign();
         rawTx3 = signer.serializeSignedTx();
      }

      //batch push tx
      bdvObj->broadcastZC({ rawTx2, rawTx3 });
      std::map<BinaryData, ArmoryErrorCodes> errMap;

      Tx tx1(rawTx2);
      Tx tx2(rawTx3);
      errMap.emplace(tx1.getThisHash(), ArmoryErrorCodes::ZcBroadcast_Error);
      errMap.emplace(tx2.getThisHash(), ArmoryErrorCodes::ZcBroadcast_Error);
      pCallback->waitOnErrors(errMap);

      //try again
      bdvObj->broadcastZC({ rawTx2, rawTx3 });
      pCallback->waitOnErrors(errMap);

      //done
      bdvObj->unregisterFromDB();
   }
}
#endif

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
   Cryptography::ECDSA::setupContext();
   //LOGENABLESTDOUT();
   LOGDISABLESTDOUT();

   std::cout << "Running main() from gtest_main.cc" << std::endl;
   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();

   FLUSHLOG();
   CLEANUPLOG();

   Cryptography::ECDSA::shutdown();
   return exitCode;
}
