////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "TestUtils.h"
#include "../Wallets/Seeds/Seeds.h"
#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/BDV.capnp.h"

using namespace Armory::Signing;
using namespace Armory::Config;
using namespace Armory::Assets;
using namespace Armory::Accounts;
using namespace Armory::Wallets;

using namespace std::string_view_literals;
using namespace std::chrono_literals;

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<ScriptSpender> getSpenderPtr(const UTXO& utxo, bool RBF = false)
{
   auto spender = std::make_shared<ScriptSpender>(utxo);
   if (RBF) {
      spender->setSequence(UINT32_MAX -2);
   }
   return spender;
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
      txData.id_ = txid;
      txData.txIns_ = txins;
      txData.txOuts_ = txouts;

      auto key = zcKeys_[txid];
      txData.txPtr_ = std::make_shared<ParsedTx>(key);
      auto tx = txData.txPtr_;

      tx->setTxHash(zcHashes_[txid]);
      for (auto& id : txins) {
         const auto& txindata = txIns_[id];
         ParsedTxIn pTxIn;

         pTxIn.value_ = txindata.value_;
         pTxIn.scrAddr_ = txindata.scrAddr_;
         pTxIn.opRef_.unserialize(txindata.outpoint_.serialized_);
         pTxIn.opRef_.setDbKey(txindata.outpoint_.key_);

         tx->inputs_.push_back(pTxIn);
         addAddrToMap(pTxIn.scrAddr_);
      }

      for (auto& id : txouts) {
         const auto& txoutdata = txOuts_[id];
         ParsedTxOut pTxOut;

         pTxOut.scrAddr_ = txoutdata.scrAddr_;
         pTxOut.value_ = txoutdata.value_;
         tx->outputs_.push_back(pTxOut);
         addAddrToMap(pTxOut.scrAddr_);
      }
      tx->state_ = ParsedTxStatus::Resolved;
   }

   void createTx0(void)
   {
      zcKeys_.push_back(READHEX("FFFF00000001"));
      zcHashes_.push_back(READHEX(
         "000102030405060708090A0B0C0D0E0FF0F1F2F3F4F5F6F7F8F9FAFBFCFDFEFF"));

      //txin0
      txIns_.emplace_back(TxInData());
      auto& txIn0 = txIns_.back();
      txIn0.value_ = 10*COIN;
      txIn0.scrAddr_ = READHEX("000102030405060708090A");

      //outpoint0
      OutpointData outpoint0;
      outpoint0.hash_ = READHEX(
         "0101010101010101010101010101010101010101010101010101010101010101");
      outpoint0.index_ = 4;
      outpoint0.key_ = READHEX("000054000003");
      outpoint0.serialized_ = getOutpoint(outpoint0.hash_, outpoint0.index_);
      txIn0.outpoint_ = outpoint0;

      //txin1
      txIns_.emplace_back(TxInData());
      auto& txIn1 = txIns_.back();
      txIn1.value_ = 5*COIN;
      txIn1.scrAddr_ = READHEX("00A1A2A3A4A5A6A7A8A9AA");

      //outpoint1
      OutpointData outpoint1;
      outpoint1.hash_ = READHEX(
         "0202020202020202020202020202020202020202020202020202020202020202");
      outpoint1.index_ = 2;
      outpoint1.key_ = READHEX("00006200000A");
      outpoint1.serialized_ = getOutpoint(outpoint1.hash_, outpoint1.index_);
      txIn1.outpoint_ = outpoint1;

      //txout0
      txOuts_.emplace_back(TxOutData());
      auto& txOut0 = txOuts_.back();
      txOut0.scrAddr_ = READHEX("00B1B2B3B4B5B6B7B8B9BA");
      txOut0.value_ = 7*COIN;

      //txout1
      txOuts_.emplace_back(TxOutData());
      auto& txOut1 = txOuts_.back();
      txOut1.scrAddr_ = READHEX("00C1C2C3C4C5C6C7C8C9CA");
      txOut1.value_ = 8*COIN;

      //create tx
      createTx(0, {0, 1}, {0, 1});
   }

   void createTx1(void)
   {
      zcKeys_.push_back(READHEX("FFFF00000002"));
      zcHashes_.push_back(READHEX(
         "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABBBBB"));

      //txin2
      txIns_.emplace_back(TxInData());
      auto& txIn2 = txIns_.back();
      txIn2.value_ = 45*COIN;
      txIn2.scrAddr_ = READHEX("C1C2C3C4C5C6C7C8C9CACB");

      //txin3
      txIns_.emplace_back(TxInData());
      auto& txIn3 = txIns_.back();
      txIn3.value_ = 35*COIN;
      txIn3.scrAddr_ = READHEX("D1D2D3D4D5D6D7D8D9DADB");

      //outpoint2
      OutpointData outpoint2;
      outpoint2.hash_ = READHEX(
         "0303030303303030303030303030303030303030303030303030303030303030");
      outpoint2.index_ = 34;
      outpoint2.key_ = READHEX("000087000010");
      outpoint2.serialized_ = getOutpoint(outpoint2.hash_, outpoint2.index_);
      txIn2.outpoint_ = outpoint2;

      //outpoint3
      OutpointData outpoint3;
      outpoint3.hash_ = READHEX(
         "0404040404040404040404040404040404040404040404040404040404040404");
      outpoint3.index_ = 0;
      outpoint3.key_ = READHEX("000011000203");
      outpoint3.serialized_ = getOutpoint(outpoint3.hash_, outpoint3.index_);
      txIn3.outpoint_ = outpoint3;

      //txout2
      txOuts_.emplace_back(TxOutData());
      auto& txOut2 = txOuts_.back();
      txOut2.scrAddr_ = READHEX("001112131415161718191F");
      txOut2.value_ = 70*COIN;

      //txout3
      txOuts_.emplace_back(TxOutData());
      auto& txOut3 = txOuts_.back();
      txOut3.scrAddr_ = READHEX("0022232425262728292A2B");
      txOut3.value_ = 10*COIN;

      //create tx
      createTx(1, {2, 3}, {2, 3});
   }

   void createTx2(void)
   {
      //child of tx0 & tx1 (txouts 0 & 2)
      zcKeys_.push_back(READHEX("FFFF00000003"));
      zcHashes_.push_back(READHEX(
         "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACCCCCCCAAAAAAAAAAAAAAAAAABBBBB"));

      //txin4
      txIns_.emplace_back(TxInData());
      auto& txIn4 = txIns_.back();
      txIn4.value_ = txOuts_[0].value_;
      txIn4.scrAddr_ = txOuts_[0].scrAddr_;

      //txin5
      txIns_.emplace_back(TxInData());
      auto& txIn5 = txIns_.back();
      txIn5.value_ = txOuts_[2].value_;
      txIn5.scrAddr_ = txOuts_[2].scrAddr_;

      //outpoint4
      OutpointData outpoint4;
      outpoint4.hash_ = zcHashes_[0];
      outpoint4.index_ = 0;
      outpoint4.key_ = zcKeys_[0];
      outpoint4.serialized_ = getOutpoint(outpoint4.hash_, outpoint4.index_);
      txIn4.outpoint_ = outpoint4;

      //outpoint5
      OutpointData outpoint5;
      outpoint5.hash_ = zcHashes_[1];
      outpoint5.index_ = 0;
      outpoint5.key_ = zcKeys_[1];
      outpoint5.serialized_ = getOutpoint(outpoint5.hash_, outpoint5.index_);
      txIn5.outpoint_ = outpoint5;

      //txout4
      txOuts_.emplace_back(TxOutData());
      auto& txOut4 = txOuts_.back();
      txOut4.scrAddr_ = READHEX("AAAAAAAAAAA4359802FF34");
      txOut4.value_ = 27*COIN;

      //txout5
      txOuts_.emplace_back(TxOutData());
      auto& txOut5 = txOuts_.back();
      txOut5.scrAddr_ = READHEX("BBBBBBB342564CCCF4536C");
      txOut5.value_ = 50*COIN;

      //create tx
      createTx(2, {4, 5}, {4, 5});
   }

   void createTx3(void)
   {
      //child of tx1 (txout 3)
      zcKeys_.push_back(READHEX("FFFF00000004"));
      zcHashes_.push_back(READHEX(
         "AAAAAAAAAAAAAAAAAAAAAAAAAAFFFFFFFFFCCCCCCCAAAAAAAAAAAAAAAAABBBBB"));

      //txin6
      txIns_.emplace_back(TxInData());
      auto& txIn6 = txIns_.back();
      txIn6.value_ = txOuts_[3].value_;
      txIn6.scrAddr_ = txOuts_[3].scrAddr_;

      //outpoint6
      OutpointData outpoint6;
      outpoint6.hash_ = zcHashes_[1];
      outpoint6.index_ = 1;
      outpoint6.key_ = zcKeys_[1];
      outpoint6.serialized_ = getOutpoint(outpoint6.hash_, outpoint6.index_);
      txIn6.outpoint_ = outpoint6;

      //txout6
      txOuts_.emplace_back(TxOutData());
      auto& txOut6 = txOuts_.back();
      txOut6.scrAddr_ = READHEX("EEEEEEEEEEEEEEE4534622");
      txOut6.value_ = 2*COIN;

      //txout7
      txOuts_.emplace_back(TxOutData());
      auto& txOut7 = txOuts_.back();
      txOut7.scrAddr_ = READHEX("EEEEEEEEEEEEEE98790234");
      txOut7.value_ = 8*COIN;

      //create tx
      createTx(3, {6}, {6, 7});
   }

   void createTx4(void)
   {
      //child of tx2 (txout 4)
      zcKeys_.push_back(READHEX("FFFF00000005"));
      zcHashes_.push_back(READHEX(
         "AAAAAAAAABBBBBBBBBBBBBBBBBBBBBBBBB3CCCCCCCAAAAAAAAAAAAAAAAABBBBB"));

      //txin7
      txIns_.emplace_back(TxInData());
      auto& txIn7 = txIns_.back();
      txIn7.value_ = txOuts_[4].value_;
      txIn7.scrAddr_ = txOuts_[4].scrAddr_;

      //outpoint7
      OutpointData outpoint7;
      outpoint7.hash_ = zcHashes_[2];
      outpoint7.index_ = 0;
      outpoint7.key_ = zcKeys_[2];
      outpoint7.serialized_ = getOutpoint(outpoint7.hash_, outpoint7.index_);
      txIn7.outpoint_ = outpoint7;

      //txout8
      txOuts_.emplace_back(TxOutData());
      auto& txOut8 = txOuts_.back();
      txOut8.scrAddr_ = txOuts_[0].scrAddr_;
      txOut8.value_ = 17*COIN;

      //txout9
      txOuts_.emplace_back(TxOutData());
      auto& txOut9 = txOuts_.back();
      txOut9.scrAddr_ = READHEX("DDDDDDDDDDDDDD98790234");
      txOut9.value_ = 10*COIN;

      //create tx
      createTx(4, {7}, {8, 9});
   }


protected:
   class ZeroConfCallbacks_Tests : public ZeroConfCallbacks
   {
      std::set<std::string> hasScrAddr(const BinaryDataRef&) const override
      { return {}; }

      void pushZcNotification(
         std::shared_ptr<MempoolSnapshot>,
         std::shared_ptr<KeyAddrMap>,
         std::map<std::string, ParsedZCData>, //flaggedBDVs
         const std::string&, //bdvid
         std::map<BinaryData, std::shared_ptr<WatcherTxBody>>&) override
      {}

      void pushZcError(const std::string&, const BinaryData&,
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

      Armory::Config::reset();
      DBSettings::setServiceType(SERVICE_UNITTEST);
      Armory::Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--public",
         "--db-type=DB_SUPER",
         "--thread-count=3",
         "--public"},
         Armory::Config::ProcessType::DB);

      LOGDISABLESTDOUT();

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
      LOGENABLESTDOUT();
      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);
   }

   /////////////////////////////////////////////////////////////////////////////
   bool checkTxIsStaged(
      const MempoolSnapshot& snapshot, 
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
      for (unsigned i=0; i<txData.txIns_.size(); i++)
      try {
         auto txInId = txData.txIns_[i];

         BinaryWriter keyWriter;
         keyWriter.put_BinaryData(txIns_[txInId].outpoint_.key_);
         keyWriter.put_uint16_t(txIns_[txInId].outpoint_.index_, BE);
         auto txOutKey = keyWriter.getData();

         auto txioKeys = snapshot.getTxioKeysForScrAddr(txIns_[txInId].scrAddr_);
         METHOD_ASSERT_FALSE(txioKeys.empty());

         bool foundTxio = false;
         for (const auto& key : txioKeys) {
            if (key != txOutKey) {
               continue;
            }
            foundTxio = true;
            auto txio = snapshot.getTxioByKey(key);
            METHOD_ASSERT_NE(txio, nullptr);
            EXPECT_EQ(txio->getDBKeyOfOutput(), txOutKey);
            EXPECT_EQ(txio->getIndexOfOutput(), txIns_[txInId].outpoint_.index_);

            EXPECT_TRUE(txio->getDBKeyOfInput().startsWith(zcKeys_[txid]));
            EXPECT_EQ(txio->getIndexOfInput(), i);
            EXPECT_EQ(txio->getValue(), txIns_[txInId].value_);

            EXPECT_TRUE(snapshot.isTxOutSpentByZC(txOutKey));
         }

         METHOD_ASSERT_TRUE(foundTxio);
      } catch (const std::range_error&) {
         return false;
      }

      //outputs
      for (unsigned i=0; i<txData.txOuts_.size(); i++)
      try {
         auto txOutId = txData.txOuts_[i];

         BinaryWriter keyWriter;
         keyWriter.put_BinaryData(zcKeys_[txid]);
         keyWriter.put_uint16_t(i, BE);
         auto txOutKey = keyWriter.getData();

         auto txioKeys = snapshot.getTxioKeysForScrAddr(txOuts_[txOutId].scrAddr_);
         METHOD_ASSERT_FALSE(txioKeys.empty());

         bool foundTxio = false;
         for (const auto& key : txioKeys) {
            if (!key.startsWith(zcKeys_[txid])) {
               continue;
            }
            foundTxio = true;
            auto txio = snapshot.getTxioByKey(key);
            METHOD_ASSERT_NE(txio, nullptr);
            EXPECT_EQ(txio->getDBKeyOfOutput(), txOutKey);
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
      const MempoolSnapshot& snapshot,
      unsigned txid) const
   {
      if (txid >= txs_.size()) {
         return false;
      }
      const auto& txData = txs_[txid];

      EXPECT_FALSE(snapshot.hasHash(zcHashes_[txid]));

      auto zckey = snapshot.getKeyForHash(zcHashes_[txid]);
      EXPECT_TRUE(zckey.empty());

      auto zcPtr = snapshot.getTxByKey(zcKeys_[txid]);
      METHOD_ASSERT_EQ(zcPtr, nullptr);

      //inputs
      for (unsigned i=0; i<txData.txIns_.size(); i++) {
         auto txInId = txData.txIns_[i];

         BinaryWriter keyWriter;
         keyWriter.put_BinaryData(txIns_[txInId].outpoint_.key_);
         keyWriter.put_uint16_t(txIns_[txInId].outpoint_.index_, BE);
         auto txOutKey = keyWriter.getData();

         try {
            auto txioKeys = snapshot.getTxioKeysForScrAddr(
               txIns_[txInId].scrAddr_);

            for (auto& key : txioKeys) {
               auto txio = snapshot.getTxioByKey(key);
               if (txio == nullptr) {
                  continue;
               }
               METHOD_ASSERT_FALSE(
                  txio->getDBKeyOfOutput().startsWith(zcKeys_[txid]));

               if (!txio->hasTxIn()) {
                  continue;
               }
               METHOD_ASSERT_FALSE(
                  txio->getDBKeyOfInput().startsWith(zcKeys_[txid]));
            }
         } catch (const std::range_error&) {}

         auto txio = snapshot.getTxioByKey(txOutKey);
         if (txio != nullptr) {
            METHOD_ASSERT_TRUE(txio->hasTxOutZC());
            if (txio->hasTxIn()) {
               METHOD_ASSERT_FALSE(
                  txio->getDBKeyOfInput().startsWith(zcKeys_[txid]));
            }
         }

         EXPECT_FALSE(snapshot.isTxOutSpentByZC(txOutKey));
      }

      for (unsigned i=0; i<txData.txOuts_.size(); i++) {
         auto txOutId = txData.txOuts_[i];

         BinaryWriter keyWriter;
         keyWriter.put_BinaryData(zcKeys_[txid]);
         keyWriter.put_uint16_t(i, BE);
         auto txOutKey = keyWriter.getData();

         try {
            auto txioKeys = snapshot.getTxioKeysForScrAddr(
               txOuts_[txOutId].scrAddr_);
            METHOD_ASSERT_TRUE(false);
         }
         catch (const std::range_error&) {}

         auto txio = snapshot.getTxioByKey(txOutKey);
         METHOD_ASSERT_EQ(txio, nullptr);
      }

      return true;
   }

   /////////////////////////////////////////////////////////////////////////////
   BinaryData checkTxOutIsSpent(
      const MempoolSnapshot& snapshot,
      unsigned txid, unsigned txoutid) const
   {
      BinaryWriter keyWriter;
      keyWriter.put_BinaryData(zcKeys_[txid]);
      keyWriter.put_uint16_t(txoutid, BE);
      auto txOutKey = keyWriter.getData();

      auto txio = snapshot.getTxioByKey(txOutKey);
      if (txio == nullptr) {
         return {};
      }
      if (!txio->hasTxIn()) {
         return {};
      }
      return txio->getDBKeyOfInput();
   }

protected:
   std::filesystem::path blkdir_{"./blkfiletest"sv};
   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::filesystem::path ldbdir_{"./ldbtestdir"sv};

   /*****/
   std::vector<BinaryData> zcKeys_;
   std::vector<BinaryData> zcHashes_;

   struct OutpointData
   {
      BinaryData hash_;
      uint32_t index_;
      BinaryData key_;
      BinaryData serialized_;
   };

   struct TxInData
   {
      uint64_t value_;
      BinaryData scrAddr_;

      OutpointData outpoint_;
   };

   struct TxOutData
   {
      uint64_t value_;
      BinaryData scrAddr_;
   };

   struct TxData
   {
      std::vector<unsigned> txIns_;
      std::vector<unsigned> txOuts_;
      unsigned id_;
      std::shared_ptr<ParsedTx> txPtr_;
   };

   std::vector<TxInData> txIns_;
   std::vector<TxOutData> txOuts_;
   std::vector<TxData> txs_;

   /*****/

   //mainAddressMap
   std::shared_ptr<std::map<BinaryData, std::shared_ptr<AddrAndHash>>> mainAddrMap_;
   ZeroConfCallbacks_Tests zcCallbacks_;
};

//TODO: copy snapshot, force merge, check it matches original

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Mempool, Stage)
{
   MempoolSnapshot snapshot(1, 2);
   EXPECT_EQ(snapshot.getTopZcID(), 0U);

   //filter the tx
   auto filterResult = filterParsedTx(
      txs_[0].txPtr_, *mainAddrMap_, &zcCallbacks_);

   //stage it
   snapshot.stageNewZC(txs_[0].txPtr_, filterResult);

   //check it was added
   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_EQ(snapshot.getTopZcID(), 1U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Mempool, Commit)
{
   MempoolSnapshot snapshot(2, 4);
   EXPECT_EQ(snapshot.getTopZcID(), 0U);

   //filter the tx
   auto filterResult = filterParsedTx(
      txs_[0].txPtr_, *mainAddrMap_, &zcCallbacks_);

   //stage it
   snapshot.stageNewZC(txs_[0].txPtr_, filterResult);

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
   MempoolSnapshot snapshot(2, 4);
   EXPECT_EQ(snapshot.getTopZcID(), 0U);

   //filter the tx
   auto filterResult = filterParsedTx(
      txs_[0].txPtr_, *mainAddrMap_, &zcCallbacks_);

   //stage it
   snapshot.stageNewZC(txs_[0].txPtr_, filterResult);

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
   MempoolSnapshot snapshot(2, 4);
   EXPECT_EQ(snapshot.getTopZcID(), 0U);

   //filter the tx
   auto filterResult = filterParsedTx(
      txs_[0].txPtr_, *mainAddrMap_, &zcCallbacks_);

   //stage it
   snapshot.stageNewZC(txs_[0].txPtr_, filterResult);

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
   MempoolSnapshot snapshot(2, 4);
   EXPECT_EQ(snapshot.getTopZcID(), 0U);

   {
      //add tx0
      auto filterResult = filterParsedTx(
         txs_[0].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[0].txPtr_, filterResult);

      //add tx1
      auto filterResult1 = filterParsedTx(
         txs_[1].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[1].txPtr_, filterResult1);
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
   MempoolSnapshot snapshot(1, 2);
   EXPECT_EQ(snapshot.getTopZcID(), 0U);

   {
      //add tx0
      auto filterResult = filterParsedTx(
         txs_[0].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[0].txPtr_, filterResult);
      EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   }

   snapshot.commitNewZCs();
   EXPECT_EQ(snapshot.getTopZcID(), 1U);

   {
      //add tx1
      auto filterResult1 = filterParsedTx(
         txs_[1].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[1].txPtr_, filterResult1);
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
   MempoolSnapshot snapshot(2, 4);

   {
      //add tx0
      auto filterResult = filterParsedTx(
         txs_[0].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[0].txPtr_, filterResult);

      //add tx1
      auto filterResult1 = filterParsedTx(
         txs_[1].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[1].txPtr_, filterResult1);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 0).empty());
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 1).empty());
   
   EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 1, 0).empty());
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 1, 1).empty());

   {
      //add tx2
      auto filterResult2 = filterParsedTx(
         txs_[2].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[2].txPtr_, filterResult2);

      //add tx3
      auto filterResult3 = filterParsedTx(
         txs_[3].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[3].txPtr_, filterResult3);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

   {
      auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
      EXPECT_TRUE(spender0.startsWith(zcKeys_[2]));
      EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 1).empty());

      auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
      EXPECT_TRUE(spender1.startsWith(zcKeys_[2]));

      auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_TRUE(spender2.startsWith(zcKeys_[3]));

      EXPECT_TRUE(checkTxOutIsSpent(snapshot, 2, 0).empty());
   }

   {
      //add tx4
      auto filterResult4 = filterParsedTx(
         txs_[4].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[4].txPtr_, filterResult4);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 3));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 4));

   {
      auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
      EXPECT_TRUE(spender0.startsWith(zcKeys_[2]));
      EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 1).empty());

      auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
      EXPECT_TRUE(spender1.startsWith(zcKeys_[2]));

      auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_TRUE(spender2.startsWith(zcKeys_[3]));

      auto spender3 = checkTxOutIsSpent(snapshot, 2, 0);
      EXPECT_TRUE(spender3.startsWith(zcKeys_[4]));
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Mempool, StageChildren_Commit)
{
   MempoolSnapshot snapshot(1, 2);

   {
      //add tx0
      auto filterResult = filterParsedTx(
         txs_[0].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[0].txPtr_, filterResult);

      //add tx1
      auto filterResult1 = filterParsedTx(
         txs_[1].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[1].txPtr_, filterResult1);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 0).empty());
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 1).empty());
   
   EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 1, 0).empty());
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 1, 1).empty());

   snapshot.commitNewZCs();

   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 0).empty());
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 1).empty());
   
   EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 1, 0).empty());
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 1, 1).empty());

   {
      //add tx2
      auto filterResult2 = filterParsedTx(
         txs_[2].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[2].txPtr_, filterResult2);

      //add tx3
      auto filterResult3 = filterParsedTx(
         txs_[3].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[3].txPtr_, filterResult3);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

   {
      auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
      EXPECT_TRUE(spender0.startsWith(zcKeys_[2]));
      EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 1).empty());

      auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
      EXPECT_TRUE(spender1.startsWith(zcKeys_[2]));

      auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_TRUE(spender2.startsWith(zcKeys_[3]));

      EXPECT_TRUE(checkTxOutIsSpent(snapshot, 2, 0).empty());
   }

   snapshot.commitNewZCs();

   EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

   {
      auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
      EXPECT_TRUE(spender0.startsWith(zcKeys_[2]));
      EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 1).empty());

      auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
      EXPECT_TRUE(spender1.startsWith(zcKeys_[2]));

      auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_TRUE(spender2.startsWith(zcKeys_[3]));  

      EXPECT_TRUE(checkTxOutIsSpent(snapshot, 2, 0).empty());
   }

   {
      //add tx4
      auto filterResult4 = filterParsedTx(
         txs_[4].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[4].txPtr_, filterResult4);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 4));

   auto spender3 = checkTxOutIsSpent(snapshot, 2, 0);
   EXPECT_TRUE(spender3.startsWith(zcKeys_[4]));

   snapshot.commitNewZCs();

   EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

   {
      auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
      EXPECT_TRUE(spender0.startsWith(zcKeys_[2]));
      EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 1).empty());

      auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
      EXPECT_TRUE(spender1.startsWith(zcKeys_[2]));

      auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_TRUE(spender2.startsWith(zcKeys_[3]));  
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 4));

   auto spender4 = checkTxOutIsSpent(snapshot, 2, 0);
   EXPECT_TRUE(spender4.startsWith(zcKeys_[4]));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Mempool, DropParent)
{
   MempoolSnapshot snapshot(2, 4);

   {
      //add tx0
      auto filterResult = filterParsedTx(
         txs_[0].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[0].txPtr_, filterResult);

      //add tx1
      auto filterResult1 = filterParsedTx(
         txs_[1].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[1].txPtr_, filterResult1);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 0).empty());
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 1).empty());
   
   EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 1, 0).empty());
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 1, 1).empty());

   {
      //add tx2
      auto filterResult2 = filterParsedTx(
         txs_[2].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[2].txPtr_, filterResult2);

      //add tx3
      auto filterResult3 = filterParsedTx(
         txs_[3].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[3].txPtr_, filterResult3);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
   EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

   auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
   EXPECT_TRUE(spender0.startsWith(zcKeys_[2]));
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 1).empty());

   auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
   EXPECT_TRUE(spender1.startsWith(zcKeys_[2]));

   auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
   EXPECT_TRUE(spender2.startsWith(zcKeys_[3]));

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

   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 1, 0).empty());

   auto spender3 = checkTxOutIsSpent(snapshot, 1, 1);
   EXPECT_TRUE(spender3.startsWith(zcKeys_[3]));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Mempool, DropParent_Commit)
{
   MempoolSnapshot snapshot(1, 2);

   {
      //add tx0
      auto filterResult = filterParsedTx(
         txs_[0].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[0].txPtr_, filterResult);

      //add tx1
      auto filterResult1 = filterParsedTx(
         txs_[1].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[1].txPtr_, filterResult1);
   }

   EXPECT_TRUE(checkTxIsStaged(snapshot, 0));
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 0).empty());
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 1).empty());
   
   EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 1, 0).empty());
   EXPECT_TRUE(checkTxOutIsSpent(snapshot, 1, 1).empty());

   {
      //add tx2
      auto filterResult2 = filterParsedTx(
         txs_[2].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[2].txPtr_, filterResult2);

      //add tx3
      auto filterResult3 = filterParsedTx(
         txs_[3].txPtr_, *mainAddrMap_, &zcCallbacks_);
      snapshot.stageNewZC(txs_[3].txPtr_, filterResult3);
   }

   {
      EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
      EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

      auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
      EXPECT_TRUE(spender0.startsWith(zcKeys_[2]));
      EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 1).empty());

      auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
      EXPECT_TRUE(spender1.startsWith(zcKeys_[2]));

      auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_TRUE(spender2.startsWith(zcKeys_[3]));
   }

   snapshot.commitNewZCs();

   {
      EXPECT_TRUE(checkTxIsStaged(snapshot, 2));
      EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

      auto spender0 = checkTxOutIsSpent(snapshot, 0, 0);
      EXPECT_TRUE(spender0.startsWith(zcKeys_[2]));
      EXPECT_TRUE(checkTxOutIsSpent(snapshot, 0, 1).empty());

      auto spender1 = checkTxOutIsSpent(snapshot, 1, 0);
      EXPECT_TRUE(spender1.startsWith(zcKeys_[2]));

      auto spender2 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_TRUE(spender2.startsWith(zcKeys_[3]));
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

      EXPECT_TRUE(checkTxOutIsSpent(snapshot, 1, 0).empty());

      auto spender3 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_TRUE(spender3.startsWith(zcKeys_[3]));
   }

   snapshot.commitNewZCs();

   {
      EXPECT_TRUE(checkIsDropped(snapshot, 0));
      EXPECT_TRUE(checkIsDropped(snapshot, 2));

      //check tx1 & 3 are still here
      EXPECT_TRUE(checkTxIsStaged(snapshot, 1));
      EXPECT_TRUE(checkTxIsStaged(snapshot, 3));

      EXPECT_TRUE(checkTxOutIsSpent(snapshot, 1, 0).empty());

      auto spender3 = checkTxOutIsSpent(snapshot, 1, 1);
      EXPECT_TRUE(spender3.startsWith(zcKeys_[3]));
   }
}

////////////////////////////////////////////////////////////////////////////////
class ZeroConfTests_FullNode : public ::testing::Test
{
protected:
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

      auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
         NetworkSettings::bitcoinNodes().first);
      nodePtr->setBlockchain(theBDMt_->bdm()->blockchain());
      nodePtr->setBlockFiles(theBDMt_->bdm()->blockFiles());
      nodePtr->setIface(iface_);
      clients_ = new Clients(theBDMt_->bdm());
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      LOGDISABLESTDOUT();
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
      firstUtxoScrAddrF_ = UTXO(500000000, 3, UINT32_MAX, 1, 
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

      Armory::Config::reset();
      delete clients_;
      delete theBDMt_;

      theBDMt_ = nullptr;
      clients_ = nullptr;

      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory("./ldbtestdir");

      std::filesystem::create_directory("./ldbtestdir");

      Armory::Config::reset();

      LOGENABLESTDOUT();
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
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

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
      false, false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID,
      true, false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID,
      true, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
   auto wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);

   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 3U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, HEADERS), TestChain::blkHash3);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash3)->isMainBranch());

   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55*COIN);

   uint64_t fullBalance = wlt->getFullBalance();
   uint64_t spendableBalance = wlt->getSpendableBalance(4);
   uint64_t unconfirmedBalance = wlt->getUnconfirmedBalance(4);
   EXPECT_EQ(fullBalance, 165*COIN);
   EXPECT_EQ(spendableBalance, 65*COIN);
   EXPECT_EQ(unconfirmedBalance, 165*COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);

   EXPECT_EQ(wltLB1->getFullBalance(), 10 * COIN);
   EXPECT_EQ(wltLB2->getFullBalance(), 15 * COIN);

   //restart bdm
   bdvPtr.reset();
   wlt.reset();
   wltLB1.reset();
   wltLB2.reset();

   clients_->shutdown();
   theBDMt_->shutdown();

   delete clients_;
   delete theBDMt_;

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID,
      true, false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID,
      true, false);
   bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
   wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);

   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55*COIN);

   fullBalance = wlt->getFullBalance();
   spendableBalance = wlt->getSpendableBalance(4);
   unconfirmedBalance = wlt->getUnconfirmedBalance(4);
   EXPECT_EQ(fullBalance, 165 * COIN);
   EXPECT_EQ(spendableBalance, 65 * COIN);
   EXPECT_EQ(unconfirmedBalance, 165 * COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);

   EXPECT_EQ(wltLB1->getFullBalance(), 10 * COIN);
   EXPECT_EQ(wltLB2->getFullBalance(), 15 * COIN);

   //add ZC
   std::filesystem::path zcPath(TestUtils::dataDir / "ZCtx.tx");
   BinaryData rawZC(TestChain::zcTxSize);
   std::ifstream zcStream(zcPath, std::ios::in | std::ios::binary);
   zcStream.read(rawZC.getCharPtr(), TestChain::zcTxSize);
   zcStream.close();
   DBTestUtils::ZcVector rawZcVec;
   rawZcVec.push_back(std::move(rawZC), 0);

   std::filesystem::path lbPath(TestUtils::dataDir / "LBZC.tx");
   BinaryData rawLBZC(TestChain::lbZCTxSize);
   std::ifstream lbStream(lbPath, std::ios::in | std::ios::binary);
   lbStream.read(rawLBZC.getCharPtr(), TestChain::lbZCTxSize);
   lbStream.close();
   DBTestUtils::ZcVector rawLBZcVec;
   rawLBZcVec.push_back(std::move(rawLBZC), 0);

   DBTestUtils::pushNewZc(theBDMt_, rawZcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   DBTestUtils::pushNewZc(theBDMt_, rawLBZcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 20*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 65*COIN);

   fullBalance = wlt->getFullBalance();
   spendableBalance = wlt->getSpendableBalance(4);
   unconfirmedBalance = wlt->getUnconfirmedBalance(4);
   EXPECT_EQ(fullBalance, 165*COIN);
   EXPECT_EQ(spendableBalance, 35*COIN);
   EXPECT_EQ(unconfirmedBalance, 165*COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);

   EXPECT_EQ(wltLB1->getFullBalance(), 5 * COIN);
   EXPECT_EQ(wltLB2->getFullBalance(), 15 * COIN);

   //
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 5U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, HEADERS), TestChain::blkHash5);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash5)->isMainBranch());

   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(),  50*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(),  70*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(),  20*COIN);

   fullBalance = wlt->getFullBalance();
   spendableBalance = wlt->getSpendableBalance(5);
   unconfirmedBalance = wlt->getUnconfirmedBalance(5);
   EXPECT_EQ(fullBalance, 170*COIN);
   EXPECT_EQ(spendableBalance, 70*COIN);
   EXPECT_EQ(unconfirmedBalance, 170*COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 25 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   EXPECT_EQ(wltLB1->getFullBalance(), 30 * COIN);
   EXPECT_EQ(wltLB2->getFullBalance(), 30 * COIN);

   //cleanup
   bdvPtr.reset();
   wlt.reset();
   wltLB1.reset();
   wltLB2.reset();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_FullNode, Load3Blocks_ZC_Plus3_TestLedgers)
{
   //copy the first 3 blocks
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrE
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);

   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 3U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, HEADERS), TestChain::blkHash3);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash3)->isMainBranch());

   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55*COIN);

   uint64_t fullBalance = wlt->getFullBalance();
   uint64_t spendableBalance = wlt->getSpendableBalance(3);
   uint64_t unconfirmedBalance = wlt->getUnconfirmedBalance(3);
   EXPECT_EQ(fullBalance, 165 * COIN);
   EXPECT_EQ(spendableBalance, 65 * COIN);
   EXPECT_EQ(unconfirmedBalance, 165 * COIN);

   //add ZC
   auto ZC1 = TestUtils::getTx(5, 1); //block 5, tx 1
   auto ZChash1 = BtcUtils::getHash256(ZC1);

   DBTestUtils::ZcVector rawZcVec;
   rawZcVec.push_back(ZC1, 1300000000);

   DBTestUtils::pushNewZc(theBDMt_, rawZcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 65*COIN);

   {
      //check scrAddr ledger before checking balance to make sure fetching 
      //ledgers does not corrupt the scrAddrObj txio map
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
      EXPECT_EQ(scrObj->getFullBalance(), 20*COIN);
      auto&& leSA = DBTestUtils::getLedgerEntryFromAddr(
         (ScrAddrObj*)scrObj, ZChash1);
      //EXPECT_EQ(leSA.getTxTime(), 1300000000);
      EXPECT_EQ(leSA.getValue(), -1000000000);
      EXPECT_EQ(leSA.getBlockNum(), UINT32_MAX);
      EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
   }


   fullBalance = wlt->getFullBalance();
   spendableBalance = wlt->getSpendableBalance(4);
   unconfirmedBalance = wlt->getUnconfirmedBalance(4);
   EXPECT_EQ(fullBalance, 165 * COIN);
   EXPECT_EQ(spendableBalance, 35 * COIN);
   EXPECT_EQ(unconfirmedBalance, 165 * COIN);

   //check ledger for ZC
   auto&& le = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash1);
   //EXPECT_EQ(le.getTxTime(), 1300000000);
   EXPECT_EQ(le.getValue(),  3000000000);
   EXPECT_EQ(le.getBlockNum(), UINT32_MAX);

   //pull ZC from DB, verify it's carrying the proper data
   auto&& dbtx = 
      iface_->beginTransaction(ZERO_CONF, LMDB::ReadOnly);
   StoredTx zcStx;
   BinaryData zcKey = WRITE_UINT16_BE(0xFFFF);
   zcKey.append(WRITE_UINT32_LE(0));

   EXPECT_EQ(iface_->getStoredZcTx(zcStx, zcKey), true);
   EXPECT_EQ(zcStx.thisHash_, ZChash1);
   EXPECT_EQ(zcStx.numBytes_ , TestChain::zcTxSize);
   EXPECT_EQ(zcStx.fragBytes_, 190U);
   EXPECT_EQ(zcStx.numTxOut_, 2U);
   ASSERT_FALSE(zcStx.stxoMap_.empty());
   EXPECT_EQ(zcStx.stxoMap_.begin()->second.getValue(), 10 * COIN);

   //check ZChash in DB
   {
      auto ss = theBDMt_->bdm()->zeroConfCont()->getSnapshot();
      EXPECT_EQ(ss->getHashForKey(zcKey), ZChash1);
   }

   dbtx.reset();

   //restart bdm
   bdvPtr.reset();
   wlt.reset();

   clients_->shutdown();
   theBDMt_->shutdown();

   delete clients_;
   delete theBDMt_;

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   scrAddrVec.pop_back();
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   wlt = bdvPtr->getWalletOrLockbox(wallet1id);

   //add 5th block
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 4U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, HEADERS), TestChain::blkHash4);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash4)->isMainBranch());

   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 20*COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 20*COIN);

   fullBalance = wlt->getFullBalance();
   spendableBalance = wlt->getSpendableBalance(5);
   unconfirmedBalance = wlt->getUnconfirmedBalance(5);
   EXPECT_EQ(fullBalance, 90 * COIN);
   EXPECT_EQ(spendableBalance, 10 * COIN);
   EXPECT_EQ(unconfirmedBalance, 90 * COIN);

   dbtx = move(
      iface_->beginTransaction(ZERO_CONF, LMDB::ReadOnly));
   StoredTx zcStx3;

   EXPECT_EQ(iface_->getStoredZcTx(zcStx3, zcKey), true);
   EXPECT_EQ(zcStx3.thisHash_, ZChash1);
   EXPECT_EQ(zcStx3.numBytes_, TestChain::zcTxSize);
   EXPECT_EQ(zcStx3.fragBytes_, 190U); // Not sure how Python can get this value
   EXPECT_EQ(zcStx3.numTxOut_, 2U);
   EXPECT_EQ(zcStx3.stxoMap_.begin()->second.getValue(), 10 * COIN);

   dbtx.reset();

   //add 6th block
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 5U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, HEADERS), TestChain::blkHash5);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash5)->isMainBranch());

   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);

   {
      //check scrAddr ledger before checking balance to make sure fetching 
      //ledgers does not corrupt the scrAddrObj txio map
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
      auto&& leSA = DBTestUtils::getLedgerEntryFromAddr(
         (ScrAddrObj*)scrObj, ZChash1);
      EXPECT_EQ(leSA.getTxTime(), 1231009513U);
      EXPECT_EQ(leSA.getValue(), -1000000000);
      EXPECT_EQ(leSA.getBlockNum(), 5U);
      EXPECT_EQ(scrObj->getFullBalance(), 70 * COIN);
   }

   fullBalance = wlt->getFullBalance();
   spendableBalance = wlt->getSpendableBalance(5);
   unconfirmedBalance = wlt->getUnconfirmedBalance(5);
   EXPECT_EQ(fullBalance, 140 * COIN);
   EXPECT_EQ(spendableBalance, 40 * COIN);
   EXPECT_EQ(unconfirmedBalance, 140 * COIN);

   le = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash1);
   EXPECT_EQ(le.getTxTime(), 1231009513U);
   EXPECT_EQ(le.getValue(), 3000000000);
   EXPECT_EQ(le.getBlockNum(), 5U);

   //Tx is now in a block, ZC should be gone from DB
   dbtx = move(
      iface_->beginTransaction(ZERO_CONF, LMDB::ReadWrite));
   StoredTx zcStx4;

   EXPECT_EQ(iface_->getStoredZcTx(zcStx4, zcKey), false);
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
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

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
      false, false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID,
      true, false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID,
      true, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
   auto wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);

   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 2U);
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

   //add first ZC
   DBTestUtils::pushNewZc(theBDMt_, zc1Vec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   fullBalance = wlt->getFullBalance();
   spendableBalance = wlt->getSpendableBalance(3);
   unconfirmedBalance = wlt->getUnconfirmedBalance(3);
   EXPECT_EQ(fullBalance, 80 * COIN);
   EXPECT_EQ(spendableBalance, 0 * COIN);
   EXPECT_EQ(unconfirmedBalance, 80 * COIN);

   auto&& le = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash1);
   //EXPECT_EQ(le.getTxTime(), 1400000000);
   EXPECT_EQ(le.getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(le.getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(le.isChainedZC());

   //add second ZC
   DBTestUtils::pushNewZc(theBDMt_, zc2Vec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);

   fullBalance = wlt->getFullBalance();
   spendableBalance = wlt->getSpendableBalance(3);
   unconfirmedBalance = wlt->getUnconfirmedBalance(3);
   EXPECT_EQ(fullBalance, 80 * COIN);
   EXPECT_EQ(spendableBalance, 0 * COIN);
   EXPECT_EQ(unconfirmedBalance, 80 * COIN);

   le = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash1);
   //EXPECT_EQ(le.getTxTime(), 1400000000);
   EXPECT_EQ(le.getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(le.getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(le.isChainedZC());

   le = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash2);
   //EXPECT_EQ(le.getTxTime(), 1500000000);
   EXPECT_EQ(le.getValue(), 30 * (int64_t)COIN);
   EXPECT_EQ(le.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(le.isChainedZC());

   //add 4th block
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 65 * COIN);

   fullBalance = wlt->getFullBalance();
   spendableBalance = wlt->getSpendableBalance(3);
   unconfirmedBalance = wlt->getUnconfirmedBalance(3);
   EXPECT_EQ(fullBalance, 135 * COIN);
   EXPECT_EQ(spendableBalance, 5 * COIN);
   EXPECT_EQ(unconfirmedBalance, 135 * COIN);

   le = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash1);
   EXPECT_EQ(le.getTxTime(), 1231008309U);
   EXPECT_EQ(le.getValue(), -25 * (int64_t)COIN);
   EXPECT_EQ(le.getBlockNum(), 3U);
   EXPECT_FALSE(le.isChainedZC());

   le = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash2);
   //EXPECT_EQ(le.getTxTime(), 1500000000);
   EXPECT_EQ(le.getValue(), 30 * (int64_t)COIN);
   EXPECT_EQ(le.getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(le.isChainedZC());

   //add 5th and 6th block
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 5U);
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

   le = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash2);
   EXPECT_EQ(le.getTxTime(), 1231009513U);
   EXPECT_EQ(le.getValue(), 30 * (int64_t)COIN);
   EXPECT_EQ(le.getBlockNum(), 5U);
   EXPECT_FALSE(le.isChainedZC());

   EXPECT_GE(theBDMt_->bdm()->zeroConfCont()->getMergeCount(), 1U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_FullNode, Load3Blocks_RBF)
{
   //get ZCs
   auto ZC1 = TestUtils::getTx(5, 1); //block 5, tx 1
   auto ZChash1 = BtcUtils::getHash256(ZC1);

   Tx zcTx1(ZC1);
   OutPoint op0 = zcTx1.getTxInCopy(0).getOutPoint();

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
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

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
      false, false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID,
      true, false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID,
      true, false);
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
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);

   uint64_t fullBalance = wlt->getFullBalance();
   uint64_t spendableBalance = wlt->getSpendableBalance(3);
   uint64_t unconfirmedBalance = wlt->getUnconfirmedBalance(3);
   EXPECT_EQ(fullBalance, 135 * COIN);
   EXPECT_EQ(spendableBalance, 35 * COIN);
   EXPECT_EQ(unconfirmedBalance, 135 * COIN);

   //add RBF ZC
   DBTestUtils::pushNewZc(theBDMt_, rawRBFVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);

   fullBalance = wlt->getFullBalance();
   spendableBalance = wlt->getSpendableBalance(3);
   unconfirmedBalance = wlt->getUnconfirmedBalance(3);
   EXPECT_EQ(fullBalance, 105 * COIN);
   EXPECT_EQ(spendableBalance, 5 * COIN);
   EXPECT_EQ(unconfirmedBalance, 105 * COIN);

   //check ledger
   auto&& le = DBTestUtils::getLedgerEntryFromWallet(wlt, RBFhash);
   //EXPECT_EQ(le.getTxTime(), 1400000000);
   EXPECT_EQ(le.getValue(), -30 * (int64_t)COIN);
   EXPECT_EQ(le.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(le.isOptInRBF());

   //replace it
   DBTestUtils::pushNewZc(theBDMt_, spendRBFVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 80 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);

   fullBalance = wlt->getFullBalance();
   spendableBalance = wlt->getSpendableBalance(3);
   unconfirmedBalance = wlt->getUnconfirmedBalance(3);
   EXPECT_EQ(fullBalance, 135 * COIN);
   EXPECT_EQ(spendableBalance, 5 * COIN);
   EXPECT_EQ(unconfirmedBalance, 135 * COIN);

   //verify replacement in ledgers
   le = DBTestUtils::getLedgerEntryFromWallet(wlt, RBFhash);
   EXPECT_EQ(le.getTxHash(), BtcUtils::EmptyHash_);

   le = DBTestUtils::getLedgerEntryFromWallet(wlt, spendRBFhash);
   //EXPECT_EQ(le.getTxTime(), 1500000000);
   EXPECT_EQ(le.getValue(), 30 * (int64_t)COIN);
   EXPECT_EQ(le.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(le.isOptInRBF());

   //add last blocks
   TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);
   DBTestUtils::triggerNewBlockNotification(theBDMt_);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 5U);
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

   //verify replacement ZC is invalid now
   le = DBTestUtils::getLedgerEntryFromWallet(wlt, spendRBFhash);
   EXPECT_EQ(le.getTxHash(), BtcUtils::EmptyHash_);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_FullNode, Load4Blocks_ZC_GetUtxos)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

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
      false, false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb1ScrAddrs, TestChain::lb1B58ID,
      true, false);
   DBTestUtils::registerWallet(
      clients_, bdvID, lb2ScrAddrs, TestChain::lb2B58ID,
      true, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wltLB1 = bdvPtr->getWalletOrLockbox(LB1ID);
   auto wltLB2 = bdvPtr->getWalletOrLockbox(LB2ID);

   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 3U);
   EXPECT_EQ(DBTestUtils::getTopBlockHash(iface_, HEADERS), TestChain::blkHash3);
   EXPECT_TRUE(theBDMt_->bdm()->blockchain()->getHeaderByHash(TestChain::blkHash3)->isMainBranch());

   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);

   uint64_t fullBalance = wlt->getFullBalance();
   uint64_t spendableBalance = wlt->getSpendableBalance(4);
   uint64_t unconfirmedBalance = wlt->getUnconfirmedBalance(4);
   EXPECT_EQ(fullBalance, 165 * COIN);
   EXPECT_EQ(spendableBalance, 65 * COIN);
   EXPECT_EQ(unconfirmedBalance, 165 * COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);

   EXPECT_EQ(wltLB1->getFullBalance(), 10 * COIN);
   EXPECT_EQ(wltLB2->getFullBalance(), 15 * COIN);

   //add ZC
   std::filesystem::path zcPath(TestUtils::dataDir / "ZCtx.tx");
   BinaryData rawZC(TestChain::zcTxSize);
   std::ifstream zcStream(zcPath, std::ios::in | std::ios::binary);
   zcStream.read(rawZC.getCharPtr(), TestChain::zcTxSize);
   zcStream.close();

   std::filesystem::path lbPath(TestUtils::dataDir / "LBZC.tx");
   BinaryData rawLBZC(TestChain::lbZCTxSize);
   std::ifstream lbStream(lbPath, std::ios::in | std::ios::binary);
   lbStream.read(rawLBZC.getCharPtr(), TestChain::lbZCTxSize);
   lbStream.close();

   DBTestUtils::ZcVector zcVec;
   zcVec.push_back(std::move(rawZC), 0);
   zcVec.push_back(std::move(rawLBZC), 0);

   DBTestUtils::pushNewZc(theBDMt_, zcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 65 * COIN);

   fullBalance = wlt->getFullBalance();
   spendableBalance = wlt->getSpendableBalance(4);
   unconfirmedBalance = wlt->getUnconfirmedBalance(4);
   EXPECT_EQ(fullBalance, 165 * COIN);
   EXPECT_EQ(spendableBalance, 35 * COIN);
   EXPECT_EQ(unconfirmedBalance, 165 * COIN);

   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   scrObj = wltLB1->getScrAddrObjByKey(TestChain::lb1ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddr);
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);
   scrObj = wltLB2->getScrAddrObjByKey(TestChain::lb2ScrAddrP2SH);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);

   EXPECT_EQ(wltLB1->getFullBalance(), 5 * COIN);
   EXPECT_EQ(wltLB2->getFullBalance(), 15 * COIN);

   //get utxos with zc
   spendableBalance = wlt->getSpendableBalance(4);
   auto utxoVec = wlt->getSpendableTxOutListForValue(UINT64_MAX);

   uint64_t totalUtxoVal = 0;
   for (auto& utxo : utxoVec) {
      totalUtxoVal += utxo.getValue();
   }
   EXPECT_EQ(spendableBalance, totalUtxoVal);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_FullNode, Replace_ZC_Test)
{
   BinaryData ZCHash1, ZCHash2, ZCHash3, ZCHash4;

   //
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   IO::CreationParams params{homedir_, {}, 1ms, {}, 1ms, 10};

   //create a root private key
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //register with db
   std::vector<BinaryData> addrVec;
   auto hashSet = assetWlt->getAddrHashSet();
   std::vector<BinaryData> hashVec;
   hashVec.insert(hashVec.begin(), hashSet.begin(), hashSet.end());

   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID(),
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
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
   for (auto& scripthash : hashSet) {
      scrObj = dbAssetWlt->getScrAddrObjByKey(scripthash);
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   }

   {
      ////spend 27 from wlt to assetWlt's first 2 unused addresses
      ////send rest back to scrAddrA

      auto spendVal = 27 * COIN;
      Signer signer;

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);

      //get utxo list for spend value
      auto unspentVec = wlt->getSpendableTxOutListForValue(spendVal);

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getValue();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getValue();
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
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
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
   auto&& zcledger = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash1);
   EXPECT_EQ(zcledger.getValue(), 27 * (int64_t)COIN);
   //EXPECT_EQ(zcledger.getTxTime(), 14000000);
   EXPECT_TRUE(zcledger.isOptInRBF());

   {
      ////Double spend the 27
      auto spendVal = 27 * COIN;
      Signer signer2;

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);

      //get utxo list for spend value
      auto unspentVec = wlt->getRBFTxOutList();

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getValue();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getValue();
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
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
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
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 7 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //check new wallet balances
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]);
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[3]);
   EXPECT_EQ(scrObj->getFullBalance(), 14 * COIN);

   //grab ledgers

   //first zc should be replaced, hence the ledger should be empty
   auto zcledger2 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash1);
   EXPECT_EQ(zcledger2.getTxHash(), BtcUtils::EmptyHash_);

   //second zc should be valid
   //grab ledger
   auto zcledger3 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash2);
   EXPECT_EQ(zcledger3.getValue(), 26 * (int64_t)COIN);
   //EXPECT_EQ(zcledger3.getTxTime(), 15000000);
   EXPECT_TRUE(zcledger3.isOptInRBF());

   //cpfp the first rbf
   {
      ////CPFP the 26
      auto spendVal = 15 * COIN;
      Signer signer3;

      //instantiate resolver feed overloaded object
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListZC();

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getValue();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getValue();
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
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer3.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      {
         auto lock = assetWlt->lockDecryptedContainer();
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
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 18 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //check new wallet balances
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[3]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[4]);
   EXPECT_EQ(scrObj->getFullBalance(), 4 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[5]);
   EXPECT_EQ(scrObj->getFullBalance(), 6 * COIN);

   //grab ledgers

   //first zc should be replaced, hence the ledger should be empty
   auto zcledger4 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash1);
   EXPECT_EQ(zcledger4.getTxHash(), BtcUtils::EmptyHash_);

   //second zc should be valid
   //grab ledger
   auto zcledger5 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash2);
   EXPECT_EQ(zcledger5.getValue(), 26 * (int64_t)COIN);
   //EXPECT_EQ(zcledger5.getTxTime(), 15000000);
   EXPECT_TRUE(zcledger5.isOptInRBF());

   //third zc should be valid
   //grab ledger
   auto zcledger6 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash3);
   EXPECT_EQ(zcledger6.getValue(), -16 * (int64_t)COIN);
   //EXPECT_EQ(zcledger6.getTxTime(), 16000000);
   EXPECT_TRUE(zcledger6.isChainedZC());
   EXPECT_TRUE(zcledger6.isOptInRBF());

   //rbf the 2 zc chain dead

   {
      ////Double spend the 27
      auto spendVal = 22 * COIN;
      Signer signer2;

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);

      //get utxo list for spend value
      auto unspentVec = wlt->getRBFTxOutList();

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getValue();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getValue();
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
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
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
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //check new wallet balances
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[3]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[4]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[5]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[6]);
   EXPECT_EQ(scrObj->getFullBalance(), 10 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[7]);
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);

   //grab ledgers

   //first zc should be replaced, hence the ledger should be empty
   auto zcledger7 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash1);
   EXPECT_EQ(zcledger7.getTxHash(), BtcUtils::EmptyHash_);

   //second zc should be replaced
   auto zcledger8 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash2);
   EXPECT_EQ(zcledger8.getTxHash(), BtcUtils::EmptyHash_);

   //third zc should be replaced
   auto zcledger9 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash3);
   EXPECT_EQ(zcledger9.getTxHash(), BtcUtils::EmptyHash_);

   //fourth zc should be valid
   auto zcledger10 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash4);
   EXPECT_EQ(zcledger10.getValue(), 22 * (int64_t)COIN);
   //EXPECT_EQ(zcledger10.getTxTime(), 17000000);
   EXPECT_FALSE(zcledger10.isChainedZC());
   EXPECT_TRUE(zcledger10.isOptInRBF());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_FullNode, RegisterAddress_AfterZC)
{
   BinaryData ZCHash1, ZCHash2, ZCHash3, ZCHash4;

   //
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   IO::CreationParams params{homedir_, {}, 1ms, {}, 1ms, 3};
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //register with db
   std::vector<BinaryData> addrVec;
   auto hashSet = assetWlt->getAddrHashSet();
   std::vector<BinaryData> hashVec;
   hashVec.insert(hashVec.begin(), hashSet.begin(), hashSet.end());

   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID(),
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
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
   for (auto& scripthash : hashSet) {
      scrObj = dbAssetWlt->getScrAddrObjByKey(scripthash);
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   }

   {
      ////spend 27 from wlt to assetWlt's first 2 unused addresses
      ////send rest back to scrAddrA

      auto spendVal = 27 * COIN;
      Signer signer;
      signer.setLockTime(3);

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);

      //get utxo list for spend value
      auto unspentVec = wlt->getSpendableTxOutListForValue(spendVal);

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getValue();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getValue();
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
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
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

   auto wallet1_balanceCount =
      DBTestUtils::getBalanceAndCount(clients_, bdvID, "wallet1", 3);

   EXPECT_EQ(wallet1_balanceCount[0], 143 * COIN);
   EXPECT_EQ(wallet1_balanceCount[1], 40 * COIN);
   EXPECT_EQ(wallet1_balanceCount[2], 143 * COIN);

   //check new wallet balances
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);

   auto assetWlt_balanceCount =
      DBTestUtils::getBalanceAndCount(clients_, bdvID, assetWlt->getID(), 3);

   EXPECT_EQ(assetWlt_balanceCount[0], 27 * COIN);
   EXPECT_EQ(assetWlt_balanceCount[1], 0 * COIN);
   EXPECT_EQ(assetWlt_balanceCount[2], 27 * COIN);

   //grab ledger
   auto zcledger = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash1);
   EXPECT_EQ(zcledger.getValue(), 27 * (int64_t)COIN);
   //EXPECT_EQ(zcledger.getTxTime(), 14000000);
   EXPECT_TRUE(zcledger.isOptInRBF());

   //register new address
   assetWlt->extendPublicChain(1);
   hashSet = assetWlt->getAddrHashSet();
   hashVec.clear();
   hashVec.insert(hashVec.begin(), hashSet.begin(), hashSet.end());
   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID(),
      false, false);

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

   wallet1_balanceCount =
      DBTestUtils::getBalanceAndCount(clients_, bdvID, "wallet1", 3);

   EXPECT_EQ(wallet1_balanceCount[0], 143 * COIN);
   EXPECT_EQ(wallet1_balanceCount[1], 40 * COIN);
   EXPECT_EQ(wallet1_balanceCount[2], 143 * COIN);

   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);

   assetWlt_balanceCount =
      DBTestUtils::getBalanceAndCount(clients_, bdvID, assetWlt->getID(), 3);

   EXPECT_EQ(assetWlt_balanceCount[0], 27 * COIN);
   EXPECT_EQ(assetWlt_balanceCount[1], 0 * COIN);
   EXPECT_EQ(assetWlt_balanceCount[2], 27 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_FullNode, ChainZC_RBFchild_Test)
{
   BinaryData ZCHash1, ZCHash2, ZCHash3;

   //
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   IO::CreationParams params{homedir_, {}, 1ms, {}, 1ms, 10};
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //register with db
   std::vector<BinaryData> addrVec;
   auto hashSet = assetWlt->getAddrHashSet();
   std::vector<BinaryData> hashVec;
   hashVec.insert(hashVec.begin(), hashSet.begin(), hashSet.end());

   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID(),
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
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
   for (auto& scripthash : hashSet) {
      scrObj = dbAssetWlt->getScrAddrObjByKey(scripthash);
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   }

   {
      ////spend 27 from wlt to assetWlt's first 2 unused addresses
      ////send rest back to scrAddrA

      auto spendVal = 27 * COIN;
      Signer signer;

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);

      //get utxo list for spend value
      auto unspentVec = wlt->getSpendableTxOutListForValue(spendVal);

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getValue();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getValue();
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
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
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
      auto ledgerVec = DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
      EXPECT_EQ(ledgerVec.first.size(), 2ULL);
      EXPECT_EQ(ledgerVec.second.size(), 0ULL);

      for (auto& ledger : ledgerVec.first) {
         EXPECT_EQ(ledger.getTxHash(), ZCHash1);
      }
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

   {
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
      auto zcledger_sa = DBTestUtils::getLedgerEntryFromAddr(
         (ScrAddrObj*)scrObj, ZCHash1);
      EXPECT_EQ(zcledger_sa.getValue(), -30 * (int64_t)COIN);
      //EXPECT_EQ(zcledger_sa.getTxTime(), 14000000);
      EXPECT_TRUE(zcledger_sa.isOptInRBF());
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   }

   //check new wallet balances
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);

   //grab ledger
   auto&& zcledger = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash1);
   EXPECT_EQ(zcledger.getValue(), 27 * (int64_t)COIN);
   //EXPECT_EQ(zcledger.getTxTime(), 14000000);
   EXPECT_TRUE(zcledger.isOptInRBF());

   //cpfp the first zc
   {
      Signer signer3;

      //instantiate resolver feed overloaded object
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListZC();

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : unspentVec) {
         total += utxo.getValue();
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
      auto recipientChange = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
      signer3.addRecipient(recipientChange);

      //sign, verify then broadcast
      {
         auto lock = assetWlt->lockDecryptedContainer();
         signer3.setFeed(assetFeed);
         signer3.sign();
      }

      auto rawTx = signer3.serializeSignedTx();
      DBTestUtils::ZcVector zcVec3;
      zcVec3.push_back(rawTx, 15000000);

      ZCHash2 = std::move(BtcUtils::getHash256(rawTx));
      DBTestUtils::pushNewZc(theBDMt_, zcVec3);
      auto ledgerVec = DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
      EXPECT_EQ(ledgerVec.first.size(), 2ULL);
      EXPECT_EQ(ledgerVec.second.size(), 0ULL);

      for (auto& ledger : ledgerVec.first) {
         EXPECT_EQ(ledger.getTxHash(), ZCHash2);
      }
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
   {
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
      auto zcledger_sa = DBTestUtils::getLedgerEntryFromAddr(
         (ScrAddrObj*)scrObj, ZCHash1);
      EXPECT_EQ(zcledger_sa.getValue(), -30 * (int64_t)COIN);
      //EXPECT_EQ(zcledger_sa.getTxTime(), 14000000);
      EXPECT_TRUE(zcledger_sa.isOptInRBF());
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   }

   //check new wallet balances
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   {
      scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]);
      auto&& zcledger_sa = DBTestUtils::getLedgerEntryFromAddr(
         (ScrAddrObj*)scrObj, ZCHash2);
      EXPECT_EQ(zcledger_sa.getValue(), 4 * (int64_t)COIN);
      //EXPECT_EQ(zcledger_sa.getTxTime(), 15000000);
      EXPECT_TRUE(zcledger_sa.isOptInRBF());
      EXPECT_EQ(scrObj->getFullBalance(), 4 * COIN);
   }

   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[3]);
   EXPECT_EQ(scrObj->getFullBalance(), 6 * COIN);

   //first zc should still be valid
   auto&& zcledger1 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash1);
   EXPECT_EQ(zcledger1.getValue(), 27 * (int64_t)COIN);
   //EXPECT_EQ(zcledger1.getTxTime(), 14000000);
   EXPECT_TRUE(zcledger1.isOptInRBF());

   //second zc should be valid
   auto&& zcledger2 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash2);
   EXPECT_EQ(zcledger2.getValue(), -17 * (int64_t)COIN);
   //EXPECT_EQ(zcledger2.getTxTime(), 15000000);
   EXPECT_TRUE(zcledger2.isOptInRBF());

   //rbf the child
   {
      auto spendVal = 10 * COIN;
      Signer signer2;

      //instantiate resolver feed
      auto assetFeed =
         std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getRBFTxOutList();

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getValue();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo, true));
      }

      //spend 5 to new address
      auto addr0 = assetWlt->getNewAddress();
      signer2.addRecipient(addr0->getRecipient(6 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());
      if (total > spendVal) {
         //change addrE, 1 btc fee
         auto changeVal = 5 * COIN;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), changeVal);
         signer2.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      {
         auto lock = assetWlt->lockDecryptedContainer();
         signer2.setFeed(assetFeed);
         signer2.sign();
      }
      EXPECT_TRUE(signer2.verify());

      auto rawTx = signer2.serializeSignedTx();
      DBTestUtils::ZcVector zcVec2;
      zcVec2.push_back(rawTx, 17000000);

      ZCHash3 = std::move(BtcUtils::getHash256(rawTx));
      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      auto ledgerVec = DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
      EXPECT_EQ(ledgerVec.first.size(), 2ULL);
      EXPECT_EQ(ledgerVec.second.size(), 1ULL);

      for (auto& ledger : ledgerVec.first) {
         EXPECT_EQ(ledger.getTxHash(), ZCHash3);
      }
      EXPECT_EQ(*ledgerVec.second.begin(), ZCHash2);
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
   {
      scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
      auto&& zcledger_sa = DBTestUtils::getLedgerEntryFromAddr(
         (ScrAddrObj*)scrObj, ZCHash1);
      EXPECT_EQ(zcledger_sa.getValue(), -30 * (int64_t)COIN);
      //EXPECT_EQ(zcledger_sa.getTxTime(), 14000000);
      EXPECT_TRUE(zcledger_sa.isOptInRBF());
      EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   }

   //check new wallet balances
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);
   {
      scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]);
      auto zcledger_sa = DBTestUtils::getLedgerEntryFromAddr(
         (ScrAddrObj*)scrObj, ZCHash2);
      EXPECT_EQ(zcledger_sa.getTxHash(), BtcUtils::EmptyHash());
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   }
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[3]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[4]);
   EXPECT_EQ(scrObj->getFullBalance(), 6 * COIN);

   //grab ledgers

   //first zc should be valid
   auto zcledger3 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash1);
   EXPECT_EQ(zcledger3.getValue(), 27 * (int64_t)COIN);
   //EXPECT_EQ(zcledger3.getTxTime(), 14000000);
   EXPECT_TRUE(zcledger3.isOptInRBF());

   //second zc should be replaced
   auto zcledger8 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash2);
   EXPECT_EQ(zcledger8.getTxHash(), BtcUtils::EmptyHash_);

   //third zc should be valid
   auto zcledger9 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash3);
   EXPECT_EQ(zcledger9.getValue(), -6 * (int64_t)COIN);
   //EXPECT_EQ(zcledger9.getTxTime(), 17000000);
   EXPECT_TRUE(zcledger9.isOptInRBF());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_FullNode, ZC_InOut_SameBlock)
{
   BinaryData ZCHash1, ZCHash2, ZCHash3;

   //
   TestUtils::setBlocks({ "0", "1" }, blk0dat_);
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
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
TEST_F(ZeroConfTests_FullNode, TwoZC_CheckLedgers)
{
   BinaryData ZCHash1, ZCHash2, ZCHash3, ZCHash4;

   //
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   IO::CreationParams params{homedir_, {}, 1ms, {}, 1ms, 5};
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //register with db
   std::vector<BinaryData> addrVec;

   auto hashSet = assetWlt->getAddrHashSet();
   std::vector<BinaryData> hashVec;
   hashVec.insert(hashVec.begin(), hashSet.begin(), hashSet.end());

   //add existing address to asset wlt for zc test purposes
   hashVec.push_back(TestChain::scrAddrD);
   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID(),
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 3U);

   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto dbAssetWlt = bdvPtr->getWalletOrLockbox(assetWlt->getID());
   auto delegateID = DBTestUtils::getLedgerDelegate(clients_, bdvID);

   //check balances
   const ScrAddrObj* scrObj;
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);

   //check new wallet balances
   for (auto& scripthash : hashSet) {
      scrObj = dbAssetWlt->getScrAddrObjByKey(scripthash);
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   }

   {
      /*
      Create a tx to fund assetWlt from scrAddrF. This will appear as
      an external tx since scrAddrF isn't registered
      */
      Signer signer;

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrF);

      //create spender
      {
         auto spender = std::make_shared<ScriptSpender>(firstUtxoScrAddrF_);
         signer.addSpender(spender);
      }

      auto assetWlt_addr = assetWlt->getNewAddress(
         AddressEntryType(AddressEntryType_P2WPKH | AddressEntryType_P2SH));
      addrVec.push_back(assetWlt_addr->getPrefixedHash());
      signer.addRecipient(assetWlt_addr->getRecipient(firstUtxoScrAddrF_.value_));

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      auto rawTx = signer.serializeSignedTx();
      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(rawTx, 14000000);

      ZCHash1 = std::move(BtcUtils::getHash256(rawTx));
      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      auto ledgerVec = DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
      EXPECT_EQ(ledgerVec.first.size(), 1ULL);
      EXPECT_EQ(ledgerVec.second.size(), 0ULL);

      for (auto& ledger : ledgerVec.first) {
         EXPECT_EQ(ledger.getTxHash(), ZCHash1);
      }
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);

   //check new wallet balances
   {
      scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
      auto zcledgerSA = DBTestUtils::getLedgerEntryFromAddr(
         (ScrAddrObj*)scrObj, ZCHash1);
      EXPECT_EQ(zcledgerSA.getValue(), 5 * (int64_t)COIN);
      //EXPECT_EQ(zcledgerSA.getTxTime(), 14000000);
      EXPECT_FALSE(zcledgerSA.isOptInRBF());
      EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   }
   scrObj = dbAssetWlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);

   //grab wallet ledger
   auto zcledger = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash1);
   EXPECT_EQ(zcledger.getValue(), 5 * (int64_t)COIN);
   //EXPECT_EQ(zcledger.getTxTime(), 14000000);
   EXPECT_FALSE(zcledger.isOptInRBF());
   EXPECT_FALSE(zcledger.isSentToSelf());

   //grab delegate ledger
   auto&& delegateLedger = DBTestUtils::getHistoryPage(clients_, bdvID, delegateID, 0);

   unsigned zc1_count = 0;
   for (auto& ld : delegateLedger) {
      if (ld.getTxHash() == ZCHash1) {
         zc1_count++;
      }
   }

   EXPECT_EQ(zc1_count, 1U);

   {
      ////assetWlt send-to-self
      auto spendVal = 5 * COIN;
      Signer signer2;

      auto feed = std::make_shared<ResolverUtils::HybridFeed>(assetWlt);
      auto addToFeed = [feed](const BinaryData& key)->void
      {
         feed->testFeed_.addPrivKey(key);
      };
      addToFeed(TestChain::privKeyAddrD);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListForValue();

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getValue();
         utxoVec.push_back(*utxoIter);

         if (tval >= spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo, true));
      }

      auto addr2 = assetWlt->getNewAddress(
         AddressEntryType(AddressEntryType_P2WPKH | AddressEntryType_P2SH));
      signer2.addRecipient(addr2->getRecipient(spendVal));
      addrVec.push_back(addr2->getPrefixedHash());

      //sign, verify then broadcast
      signer2.setFeed(feed);
      signer2.sign();
      EXPECT_TRUE(signer2.verify());

      auto rawTx = signer2.serializeSignedTx();
      DBTestUtils::ZcVector zcVec2;
      zcVec2.push_back(rawTx, 15000000);

      ZCHash2 = std::move(BtcUtils::getHash256(rawTx));
      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      auto ledgerVec = DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
      for (auto& ledger : ledgerVec.first) {
         EXPECT_EQ(ledger.getTxHash(), ZCHash2);
      }
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);

   //check new wallet balances
   {
      scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
      auto zcledgerSA = DBTestUtils::getLedgerEntryFromAddr(
         (ScrAddrObj*)scrObj, ZCHash1);
      EXPECT_EQ(zcledgerSA.getValue(), 5 * (int64_t)COIN);
      //EXPECT_EQ(zcledgerSA.getTxTime(), 14000000);
      EXPECT_FALSE(zcledgerSA.isOptInRBF());
      EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   }

   scrObj = dbAssetWlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //grab wallet ledger
   auto zcledger2 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash1);
   EXPECT_EQ(zcledger2.getValue(), 5 * (int64_t)COIN);
   EXPECT_EQ(zcledger2.getBlockNum(), UINT32_MAX);
   EXPECT_FALSE(zcledger2.isSentToSelf());

   auto zcledger3 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash2);
   EXPECT_EQ(zcledger3.getValue(), 5 * (int64_t)COIN);
   EXPECT_EQ(zcledger3.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(zcledger3.isSentToSelf());

   //grab delegate ledger
   auto delegateLedger2 = DBTestUtils::getHistoryPage(clients_, bdvID, delegateID, 0);

   unsigned zc2_count = 0;
   unsigned zc3_count = 0;

   for (auto& ld : delegateLedger2) {
      if (ld.getTxHash() == ZCHash1) {
         zc2_count++;
      }
      if (ld.getTxHash() == ZCHash2) {
         zc3_count++;
      }
   }

   EXPECT_EQ(zc2_count, 1U);
   EXPECT_EQ(zc3_count, 1U);

   //mine a new block
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrB, 1);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   //check chain is 1 block longer
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 4U);

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 80 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);

   //check new wallet balances
   {
      scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
      auto zcledgerSA = DBTestUtils::getLedgerEntryFromAddr(
         (ScrAddrObj*)scrObj, ZCHash1);
      EXPECT_EQ(zcledgerSA.getValue(), 5 * (int64_t)COIN);
      //EXPECT_EQ(zcledgerSA.getTxTime(), 14000000);
      EXPECT_FALSE(zcledgerSA.isOptInRBF());
      EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   }

   scrObj = dbAssetWlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //grab wallet ledger
   zcledger2 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash1);
   EXPECT_EQ(zcledger2.getValue(), 5 * (int64_t)COIN);
   EXPECT_EQ(zcledger2.getBlockNum(), 4U);
   EXPECT_FALSE(zcledger2.isSentToSelf());

   zcledger3 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash2);
   EXPECT_EQ(zcledger3.getValue(), 5 * (int64_t)COIN);
   EXPECT_EQ(zcledger3.getBlockNum(), 4U);
   EXPECT_TRUE(zcledger3.isSentToSelf());

   //grab delegate ledger
   delegateLedger2 = DBTestUtils::getHistoryPage(clients_, bdvID, delegateID, 0);

   zc2_count = 0;
   zc3_count = 0;
   for (auto& ld : delegateLedger2) {
      if (ld.getTxHash() == ZCHash1) {
         zc2_count++;
      }
      if (ld.getTxHash() == ZCHash2) {
         zc3_count++;
      }
   }

   EXPECT_EQ(zc2_count, 1U);
   EXPECT_EQ(zc3_count, 1U);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class ZeroConfTests_Supernode : public ::testing::Test
{
protected:
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

      auto nodePtr = std::dynamic_pointer_cast<NodeUnitTest>(
         NetworkSettings::bitcoinNodes().first);

      nodePtr->setBlockchain(theBDMt_->bdm()->blockchain());
      nodePtr->setBlockFiles(theBDMt_->bdm()->blockFiles());
      nodePtr->setIface(iface_);
      clients_ = new Clients(theBDMt_->bdm());
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      LOGDISABLESTDOUT();
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

      Armory::Config::reset();
      LOGENABLESTDOUT();
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
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
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
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);

      //get utxo list for spend value
      auto unspentVec = wlt->getSpendableTxOutListForValue(spendVal);

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getValue();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getValue();
         signer.addSpender(getSpenderPtr(utxo, true));
      }

      //spendVal to addrE
      auto recipientChange = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrD.getSliceCopy(1, 20), spendVal);
      signer.addRecipient(recipientChange);

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
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
   LedgerEntry le = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash);

   //EXPECT_EQ(le.getTxTime(), 1300000000);
   EXPECT_EQ(le.isSentToSelf(), false);
   EXPECT_EQ(le.getValue(), -27 * (int64_t)COIN);

   //check ZChash in DB
   {
      BinaryData zcKey = WRITE_UINT16_BE(0xFFFF);
      zcKey.append(WRITE_UINT32_LE(0));
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
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
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
   auto zcledger = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash1);
   EXPECT_EQ(zcledger.getTxHash(), BtcUtils::EmptyHash());

   //grab ledger for 2nd ZC
   zcledger = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash2);
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
   zcledger = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash1);
   EXPECT_EQ(zcledger.getTxHash(), BtcUtils::EmptyHash());
   
   zcledger = DBTestUtils::getLedgerEntryFromWallet(wlt, ZChash2);
   EXPECT_EQ(zcledger.getTxTime(), 1231009513U);
   EXPECT_EQ(zcledger.getBlockNum(), 5U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ZeroConfTests_Supernode, RegisterAfterZC)
{
   TestUtils::setBlocks({ "0", "1", "2", "3", "4" }, blk0dat_);
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
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
      false, true);

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
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   IO::CreationParams params{homedir_, {}, 1ms, {}, 1ms, 3};
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt = AssetWallet_Single::createFromSeed(
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
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, wltVec, assetWlt->getID(),
      false, false);
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
   EXPECT_EQ(scrObj->getFullBalance(), 70 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 20 * COIN);

   BinaryData ZCHash1, ZCHash2;
   for (auto& sa : wltSet) {
      scrObj = assetWltDbObj->getScrAddrObjByKey(sa);
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   }

   {
      Signer signer;

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);
      feed->addPrivKey(TestChain::privKeyAddrF);

      //get utxo list for spend value
      auto&& unspentVec = wlt->getSpendableTxOutListForValue(UINT64_MAX);

      //consume 1st utxo, send 2 to scrAddrA, 3 to new wallet
      signer.addSpender(getSpenderPtr(unspentVec[0]));
      signer.addRecipient(addr1_ptr->getRecipient(3 * COIN));
      auto recipientChange = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrA.getSliceCopy(1, 20), 2 * COIN);
      signer.addRecipient(recipientChange);
      signer.setFeed(feed);
      signer.sign();

      //2nd tx, 2nd utxo, 5 to scrAddrB, 5 new wallet
      Signer signer2;
      signer2.addSpender(getSpenderPtr(unspentVec[1]));
      signer2.addRecipient(addr2_ptr->getRecipient(5 * COIN));
      auto recipientChange2 = std::make_shared<Recipient_P2PKH>(
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
   auto msgRoot = message.getRoot<Armory::Codec::BDV::Notifications>();
   auto capnNotifs = msgRoot.getNotifs();
   ASSERT_EQ(capnNotifs.size(), 2);

   auto notifIndex = std::get<1>(newBlockNotif);
   EXPECT_EQ(notifIndex, 0U);

   //grab the invalidated zc notif, it should carry the hash for both our ZC
   auto zcNotif = capnNotifs[1];
   EXPECT_EQ(zcNotif.which(), Armory::Codec::BDV::Notification::Which::INVALIDATED_ZC);

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
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   IO::CreationParams params{homedir_, {}, 1ms, {}, 1ms, 10};
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //register with db
   std::vector<BinaryData> addrVec;
   auto hashSet = assetWlt->getAddrHashSet();
   std::vector<BinaryData> hashVec;
   hashVec.insert(hashVec.begin(), hashSet.begin(), hashSet.end());

   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID(),
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 3U);

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
      Signer signer;

      //instantiate resolver feed overloaded object
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);

      //get utxo list for spend value
      auto unspentVec = wlt->getSpendableTxOutListForValue(spendVal);

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getValue();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getValue();
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
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
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
      Signer signer3;

      //instantiate resolver feed overloaded object
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListZC();

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : unspentVec) {
         total += utxo.getValue();
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
      auto recipientChange = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
      signer3.addRecipient(recipientChange);

      //sign, verify then broadcast 
      {
         auto lock = assetWlt->lockDecryptedContainer();
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
      Signer signer2;

      //instantiate resolver feed
      auto assetFeed =
         std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getRBFTxOutList();

      std::vector<UTXO> utxoVec;
      uint64_t tval = 0;
      auto utxoIter = unspentVec.begin();
      while (utxoIter != unspentVec.end()) {
         tval += utxoIter->getValue();
         utxoVec.push_back(*utxoIter);

         if (tval > spendVal) {
            break;
         }
         ++utxoIter;
      }

      //create script spender objects
      uint64_t total = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo, true));
      }

      //spend 6 to a new address
      auto addr0 = assetWlt->getNewAddress();
      signer2.addRecipient(addr0->getRecipient(6 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());


      if (total > spendVal) {
         //change addrE, 1 btc fee
         auto changeVal = 5 * COIN;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), changeVal);
         signer2.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      {
         auto lock = assetWlt->lockDecryptedContainer();
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
   auto zcledger8 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash2);
   EXPECT_EQ(zcledger8.getTxHash(), BtcUtils::EmptyHash_);

   //third zc should be valid
   auto zcledger9 = DBTestUtils::getLedgerEntryFromWallet(dbAssetWlt, ZCHash3);
   EXPECT_EQ(zcledger9.getValue(), -6 * (int64_t)COIN);
   EXPECT_EQ(zcledger9.getBlockNum(), UINT32_MAX);
   EXPECT_TRUE(zcledger9.isOptInRBF());

   //mine a new block
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrA, 3);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);

   //check chain is 3 block longer
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 6U);

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
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
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
   feed->addPrivKey(TestChain::privKeyAddrB);
   feed->addPrivKey(TestChain::privKeyAddrC);
   feed->addPrivKey(TestChain::privKeyAddrD);

   ////
   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD
   };

   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(clients_, BitcoinSettings::getMagicBytes());

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
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

   auto spenderA = std::make_shared<ScriptSpender>(utxoA);
   auto spenderB = std::make_shared<ScriptSpender>(utxoB);
   DBTestUtils::ZcVector zcVec;

   //spend from D to C
   {
      Signer signer;
      signer.addSpender(spenderA);

      auto recipient = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrC.getSliceCopy(1, 20), utxoA.getValue());
      signer.addRecipient(recipient);

      signer.setFeed(feed);
      signer.sign();
      signer.serializeSignedTx();
      zcVec.push_back(signer.serializeSignedTx(), 130000000, 0);
   }

   //spend from B to C
   {
      Signer signer;
      signer.addSpender(spenderB);

      auto recipient = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrC.getSliceCopy(1, 20), utxoB.getValue());
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

   zc1 = bdvPtr->getTxByHash(hash1);
   zc2 = bdvPtr->getTxByHash(hash2);

   EXPECT_EQ(zc1.getTxHeight(), 6U);
   EXPECT_EQ(zc2.getTxHeight(), UINT32_MAX);

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

   zc1 = bdvPtr->getTxByHash(hash1);
   zc2 = bdvPtr->getTxByHash(hash2);

   EXPECT_EQ(zc1.getTxHeight(), 6U);
   EXPECT_EQ(zc2.getTxHeight(), 7U);
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
         NetworkSettings::bitcoinNodes().first);

      rpcNode_ = std::dynamic_pointer_cast<NodeRPC_UnitTest>(
         NetworkSettings::rpcNode());

      nodePtr_->setIface(iface_);
      nodePtr_->setBlockchain(theBDMt_->bdm()->blockchain());
      nodePtr_->setBlockFiles(theBDMt_->bdm()->blockFiles());
      hexMagicBytes = BitcoinSettings::getMagicBytes().toHexStr();
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      LOGDISABLESTDOUT();
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
      authPeersPassLbd_ = [](const std::set<EncryptionKeyId>&)->SecureBinaryData
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

      std::stringstream serverAddr;
      serverAddr << "127.0.0.1:" << NetworkSettings::dbPort();
      clientPeers.addPeer(serverPubkey, serverAddr.str());
      serverPeers.addPeer(clientPubkey, "127.0.0.1");

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

      Armory::Config::reset();

      LOGENABLESTDOUT();
      CLEANUP_ALL_TIMERS();
   }

   BlockDataManagerThread *theBDMt_;
   PassphraseLambda authPeersPassLbd_;
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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };

   theBDMt_->start(DBSettings::initMode());
   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::dbPort(),
      Armory::Config::getDataDir(),
      authPeersPassLbd_,
      NetworkSettings::ephemeralPeers(), true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey);
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
      std::set<BinaryData> scrAddrSet{ TestChain::scrAddrB };
      pCallback->waitOnZc(zcHashes, scrAddrSet);
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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };
   theBDMt_->start(DBSettings::initMode());

   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::dbPort(),
      Armory::Config::getDataDir(),
      authPeersPassLbd_,
      NetworkSettings::ephemeralPeers(), true, 
      pCallback);
   bdvObj->addPublicKey(serverPubkey);
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
   pCallback->waitOnZc({ZChash1}, {TestChain::scrAddrB});
   pCallback->waitOnZc({ZChash2}, {TestChain::scrAddrB});

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };
   theBDMt_->start(DBSettings::initMode());

   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::dbPort(),
      Armory::Config::getDataDir(),
      authPeersPassLbd_,
      NetworkSettings::ephemeralPeers(), true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey);
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
   pCallback->waitOnZc({ZChash1}, {TestChain::scrAddrB});
   pCallback->waitOnZc({ZChash2}, {TestChain::scrAddrB});

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };
   theBDMt_->start(DBSettings::initMode());

   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::dbPort(),
      Armory::Config::getDataDir(),
      authPeersPassLbd_,
      NetworkSettings::ephemeralPeers(), true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey);
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
   pCallback->waitOnZc({ZChash1, ZChash2}, {TestChain::scrAddrB});

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };
   theBDMt_->start(DBSettings::initMode());

   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::dbPort(),
      Armory::Config::getDataDir(),
      authPeersPassLbd_,
      NetworkSettings::ephemeralPeers(), true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey);
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
   pCallback->waitOnZc({ZChash1}, {TestChain::scrAddrB});
   pCallback->waitOnZc({ZChash2}, {TestChain::scrAddrB});

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };
   theBDMt_->start(DBSettings::initMode());

   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::dbPort(),
      Armory::Config::getDataDir(),
      authPeersPassLbd_,
      NetworkSettings::ephemeralPeers(), true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey);
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
   pCallback->waitOnZc({ZChash1}, {TestChain::scrAddrB});

   //push them again, should get already in mempool error for first zc, notif for 2nd
   bdvObj->broadcastZC( { ZC1, ZC2 } );
   pCallback->waitOnError(
      ZChash1, ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);
   pCallback->waitOnZc({ZChash2}, {TestChain::scrAddrB});

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC
   };
   theBDMt_->start(DBSettings::initMode());

   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::dbPort(),
      Armory::Config::getDataDir(),
      authPeersPassLbd_,
      NetworkSettings::ephemeralPeers(), true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey);
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
   pCallback->waitOnZc({ZChash1, ZChash2}, {TestChain::scrAddrB});

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
   feed->addPrivKey(TestChain::privKeyAddrB);
   feed->addPrivKey(TestChain::privKeyAddrC);
   feed->addPrivKey(TestChain::privKeyAddrD);
   feed->addPrivKey(TestChain::privKeyAddrE);
   feed->addPrivKey(TestChain::privKeyAddrF);

   startupBIP150CTX(4);

   WebSocketServer::initAuthPeers(authPeersPassLbd_);
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

   theBDMt_->start(DBSettings::initMode());
   auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
   auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
      "127.0.0.1", NetworkSettings::dbPort(),
      Armory::Config::getDataDir(),
      authPeersPassLbd_,
      NetworkSettings::ephemeralPeers(), true, //public server
      pCallback);
   bdvObj->addPublicKey(serverPubkey);
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
      auto spender = std::make_shared<ScriptSpender>(utxo);
      spender->setSequence(0xFFFFFFFF - 2); //flag rbf

      auto recPtr = std::make_shared<Recipient_P2PKH>(
         recipient.getSliceCopy(1, 20), utxo.getValue());

      Signer signer;
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
      branchPointBlockHash = top->getThisHash();
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

      std::set<BinaryData> scrAddrSet1, scrAddrSet2;
      {
         Tx tx1(bd_BtoC);
         Tx tx2(bd_FtoD);

         scrAddrSet1.insert(TestChain::scrAddrB);
         scrAddrSet1.insert(TestChain::scrAddrC);

         scrAddrSet2.insert(TestChain::scrAddrF);
         scrAddrSet2.insert(TestChain::scrAddrA);

         pCallback->waitOnZc({tx1.getThisHash()}, scrAddrSet1);
         pCallback->waitOnZc({tx2.getThisHash()}, scrAddrSet2);
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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::dbPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_,
         NetworkSettings::ephemeralPeers(), true,  //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
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
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);
      feed->addPrivKey(TestChain::privKeyAddrF);

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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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

         Signer signer;
         
         auto spender1 = std::make_shared<ScriptSpender>(zcUtxo1);
         auto spender2 = std::make_shared<ScriptSpender>(zcUtxo2);
         signer.addSpender(spender1);
         signer.addSpender(spender2);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 3 * COIN);
         signer.addRecipient(recA);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 2 * COIN);
         signer.addRecipient(recE);
         
         auto recD = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recD);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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

      std::set<BinaryData> scrAddrSet;
      scrAddrSet.insert(TestChain::scrAddrA);
      scrAddrSet.insert(TestChain::scrAddrB);
      scrAddrSet.insert(TestChain::scrAddrC);
      scrAddrSet.insert(TestChain::scrAddrD);
      scrAddrSet.insert(TestChain::scrAddrE);
      scrAddrSet.insert(TestChain::scrAddrF);

      pCallback->waitOnZc(txHashes, scrAddrSet);

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto&& bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::dbPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_,
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
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
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);
      feed->addPrivKey(TestChain::privKeyAddrF);

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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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

         Signer signer;
         
         auto spender1 = std::make_shared<ScriptSpender>(zcUtxo1);
         auto spender2 = std::make_shared<ScriptSpender>(zcUtxo2);
         signer.addSpender(spender1);
         signer.addSpender(spender2);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 3 * COIN);
         signer.addRecipient(recA);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 2 * COIN);
         signer.addRecipient(recE);
         
         auto recD = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recD);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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

      std::set<BinaryData> scrAddrSet {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrD
      };
      pCallback->waitOnZc(txHashes, scrAddrSet);

      //batch push all tx
      bdvObj->broadcastZC({ rawTx1, rawTx2, rawTx3 });
      txHashes.clear();
      txHashes.insert(tx2.getThisHash());
      txHashes.insert(tx3.getThisHash());

      scrAddrSet.clear();
      scrAddrSet.insert(TestChain::scrAddrA);
      scrAddrSet.insert(TestChain::scrAddrC);
      scrAddrSet.insert(TestChain::scrAddrD);
      scrAddrSet.insert(TestChain::scrAddrE);
      scrAddrSet.insert(TestChain::scrAddrF);

      //wait on already in mempool error
      pCallback->waitOnError(tx1.getThisHash(),
         ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool);

      //wait on zc notifs
      pCallback->waitOnZc(txHashes, scrAddrSet);

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::dbPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_,
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
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
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);
      feed->addPrivKey(TestChain::privKeyAddrF);

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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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

         Signer signer;
         auto spender1 = std::make_shared<ScriptSpender>(zcUtxo1);
         auto spender2 = std::make_shared<ScriptSpender>(zcUtxo2);
         signer.addSpender(spender1);
         signer.addSpender(spender2);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 3 * COIN);
         signer.addRecipient(recA);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 2 * COIN);
         signer.addRecipient(recE);

         auto recD = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recD);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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

      std::set<BinaryData> scrAddrSet;
      scrAddrSet.insert(TestChain::scrAddrA);
      scrAddrSet.insert(TestChain::scrAddrB);
      scrAddrSet.insert(TestChain::scrAddrC);
      scrAddrSet.insert(TestChain::scrAddrD);
      scrAddrSet.insert(TestChain::scrAddrE);
      scrAddrSet.insert(TestChain::scrAddrF);

      //wait on zc notifs
      pCallback->waitOnZc(txHashes, scrAddrSet);

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::dbPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_,
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
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
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);
      feed->addPrivKey(TestChain::privKeyAddrF);

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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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

         Signer signer;
         auto spender1 = std::make_shared<ScriptSpender>(zcUtxo1);
         auto spender2 = std::make_shared<ScriptSpender>(zcUtxo2);
         signer.addSpender(spender1);
         signer.addSpender(spender2);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 3 * COIN);
         signer.addRecipient(recA);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 2 * COIN);
         signer.addRecipient(recE);

         auto recD = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recD);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
      DBTestUtils::mineNewBlock(theBDMt_, CryptoPRNG::generateRandom(20), 1);
      pCallback->waitOnSignal(BDMAction_NewBlock);

      //batch push all tx
      bdvObj->broadcastZC({ rawTx1_B, rawTx1_C, rawTx2, rawTx3 });
      std::set<BinaryData> txHashes;
      txHashes.insert(tx1_C.getThisHash());
      txHashes.insert(tx2.getThisHash());
      txHashes.insert(tx3.getThisHash());

      std::set<BinaryData> scrAddrSet {
         TestChain::scrAddrA,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };

      //wait on zc notifs
      pCallback->waitOnZc(txHashes, scrAddrSet);

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::dbPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_,
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
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
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);
      feed->addPrivKey(TestChain::privKeyAddrF);

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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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

         Signer signer;
         
         auto spender1 = std::make_shared<ScriptSpender>(zcUtxo1);
         auto spender2 = std::make_shared<ScriptSpender>(zcUtxo2);
         signer.addSpender(spender1);
         signer.addSpender(spender2);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 3 * COIN);
         signer.addRecipient(recA);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 2 * COIN);
         signer.addRecipient(recE);
         
         auto recD = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recD);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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

      std::set<BinaryData> scrAddrSet {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };

      //wait on zc notifs
      pCallback->waitOnZc(txHashes, scrAddrSet);

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto&& serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto&& bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::dbPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_,
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
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
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);
      feed->addPrivKey(TestChain::privKeyAddrF);

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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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

      std::set<BinaryData> scrAddrSet {
         TestChain::scrAddrA,
         TestChain::scrAddrB,
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };

      //wait on zc error for conflicting child
      pCallback->waitOnError(
         tx3.getThisHash(), ArmoryErrorCodes::ZcBroadcast_VerifyRejected);

      //wait on zc notifs
      pCallback->waitOnZc(txHashes, scrAddrSet);

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::dbPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_,
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
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
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);
      feed->addPrivKey(TestChain::privKeyAddrF);

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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         std::set<BinaryData> scrAddrSet{
            TestChain::scrAddrA,
            TestChain::scrAddrB,
            TestChain::scrAddrD
         };

         //push the first zc
         bdvObj->broadcastZC({rawTx1_B});

         //wait on notification
         pCallback->waitOnZc({tx1_B.getThisHash()}, scrAddrSet);
      }

      //batch push all tx
      bdvObj->broadcastZC({ rawTx1_B, rawTx1_C, rawTx2, rawTx3 });

      std::set<BinaryData> txHashes {
         tx1_C.getThisHash(),
         tx2.getThisHash()
      };

      std::set<BinaryData> scrAddrSet {
         TestChain::scrAddrC,
         TestChain::scrAddrD,
         TestChain::scrAddrE,
         TestChain::scrAddrF
      };

      //wait on zc error for conflicting child
      pCallback->waitOnError(
         tx3.getThisHash(), ArmoryErrorCodes::ZcBroadcast_VerifyRejected);

      //wait on zc notifs
      pCallback->waitOnZc(txHashes, scrAddrSet);

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::dbPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_,
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
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
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);
      feed->addPrivKey(TestChain::privKeyAddrF);

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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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

         std::set<BinaryData> scrAddrSet {
            TestChain::scrAddrA,
            TestChain::scrAddrB,
            TestChain::scrAddrD,
            TestChain::scrAddrE,
            TestChain::scrAddrF
         };

         //push the first zc and its child through the node
         nodePtr_->pushZC({ {rawTx1_B, 0}, {rawTx2, 0} }, false);

         //wait on notification
         pCallback->waitOnZc(txHashes, scrAddrSet);
      }

      //batch push first zc (already in chain), C (unrelated) 
      //and tx3 (child of first, mempool conflict with tx2)
      bdvObj->broadcastZC({ rawTx1_B, rawTx1_C, rawTx3 });
      std::set<BinaryData> txHashes {tx1_C.getThisHash()};

      std::set<BinaryData> scrAddrSet {
         TestChain::scrAddrC,
         TestChain::scrAddrE
      };

      //wait on zc error for conflicting child
      pCallback->waitOnError(
         tx3.getThisHash(), ArmoryErrorCodes::ZcBroadcast_VerifyRejected);

      //wait on zc notifs
      pCallback->waitOnZc(txHashes, scrAddrSet);

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::dbPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_,
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
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
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);
      feed->addPrivKey(TestChain::privKeyAddrF);

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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender1 = std::make_shared<ScriptSpender>(utxoD);
         auto spender2 = std::make_shared<ScriptSpender>(utxoE);
         signer.addSpender(spender1);
         signer.addSpender(spender2);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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

         std::set<BinaryData> scrAddrSet {
            TestChain::scrAddrA,
            TestChain::scrAddrB,
            TestChain::scrAddrD,
            TestChain::scrAddrE,
            TestChain::scrAddrF
         };

         //push the first zc and its child
         bdvObj->broadcastZC({ rawTx1_B, rawTx2 });

         //wait on notification
         pCallback->waitOnZc(txHashes, scrAddrSet);
      }

      //batch push first zc (already in chain), C (unrelated) 
      //and tx3 (child of first & C, mempool conflict with tx2 on utxo from first)
      bdvObj->broadcastZC({ rawTx1_B, rawTx1_C, rawTx3 });
      std::set<BinaryData> txHashes {tx1_C.getThisHash()};

      std::set<BinaryData> scrAddrSet {
         TestChain::scrAddrC,
         TestChain::scrAddrE
      };

      //wait on zc error for conflicting child
      pCallback->waitOnError(
         tx3.getThisHash(), ArmoryErrorCodes::ZcBroadcast_VerifyRejected);

      //wait on zc notifs
      pCallback->waitOnZc(txHashes, scrAddrSet);

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::dbPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_,
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

   //create BDV lambda
   auto setupBDV = [this, &serverPubkey](void)->std::shared_ptr<WSClient>
   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::dbPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_,
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
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
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);
      feed->addPrivKey(TestChain::privKeyAddrF);

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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoF);
         signer.addSpender(spender);

         auto recB = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoA);
         signer.addSpender(spender);

         auto recC = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recD = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recF = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recF);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosE[0]);
         signer.addSpender(spender);

         auto recF = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recF);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         addrSet.insert(txout.getScrAddressStr());
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
      std::set<BinaryData> addrSet;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      instance->bdvPtr_->broadcastZC(zcs);
      instance->callbackPtr_->waitOnZc(hashSet, addrSet);

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
      std::set<BinaryData> addrSet;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      instance->bdvPtr_->broadcastZC(zcs);
      instance->callbackPtr_->waitOnZc(hashSet, addrSet);

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

      std::set<BinaryData> addrSet;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      //wait on zc
      instance->callbackPtr_->waitOnZc(hashSet, addrSet);

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
      std::set<BinaryData> addrSet;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      instance->bdvPtr_->broadcastZC(zcs);
      instance->callbackPtr_->waitOnZc(hashSet, addrSet);

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

      std::set<BinaryData> addrSet;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      //wait on zc
      instance->callbackPtr_->waitOnZc(hashSet, addrSet);

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
      std::set<BinaryData> scrAddrSet;
      std::set<BinaryData> hashes;
      for (auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(zcs.back());
         hashes.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         scrAddrSet.insert(localAddrSet.begin(), localAddrSet.end());
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
      mainInstance->callbackPtr_->waitOnZc(hashes, scrAddrSet);

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

   //create BDV lambda
   auto setupBDV = [this, &serverPubkey](void)->std::shared_ptr<WSClient>
   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::dbPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_,
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
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
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);
      feed->addPrivKey(TestChain::privKeyAddrF);

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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoF);
         signer.addSpender(spender);

         auto recB = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoA);
         signer.addSpender(spender);

         auto recC = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosC[0]);
         signer.addSpender(spender);

         auto recD = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recF = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recF);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosE[0]);
         signer.addSpender(spender);

         auto recF = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recF);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         addrSet.insert(txout.getScrAddressStr());
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
      std::set<BinaryData> addrSet;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }
      instance->bdvPtr_->broadcastZC(zcs);

      //wait on zc
      instance->callbackPtr_->waitOnZc(hashSet, addrSet);

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
      std::set<BinaryData> addrSet;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      instance->bdvPtr_->broadcastZC(zcs);

      //wait on zc
      instance->callbackPtr_->waitOnZc(hashSet, addrSet);

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

      std::set<BinaryData> addrSet;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      //wait on zc
      instance->callbackPtr_->waitOnZc_OutOfOrder(hashSet);

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
      std::set<BinaryData> addrSet;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      instance->bdvPtr_->broadcastZC(zcs);
      instance->callbackPtr_->waitOnZc(hashSet, addrSet);

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

      std::set<BinaryData> addrSet;
      std::set<BinaryData> hashSet;
      for (const auto& id : zcIds) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      for (const auto& id : zcIds_skipped) {
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }

      instance->callbackPtr_->waitOnZc(hashSet, addrSet);

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
      std::set<BinaryData> scrAddrSet;
      std::set<BinaryData> hashes;
      for (auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(zcs.back());
         hashes.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         scrAddrSet.insert(localAddrSet.begin(), localAddrSet.end());
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
      mainInstance->callbackPtr_->waitOnZc(hashes, scrAddrSet);

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

   //create BDV lambda
   auto setupBDV = [this, &serverPubkey](void)->std::shared_ptr<WSClient>
   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::dbPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_,
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
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
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);
      feed->addPrivKey(TestChain::privKeyAddrF);

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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosE[0]);
         signer.addSpender(spender);

         auto recF = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrF.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recF);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         addrSet.insert(txout.getScrAddressStr());
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
      std::set<BinaryData> addrSet;
      std::set<BinaryData> hashSet;
      for (auto& id : zcIds) {
         zcs.push_back(rawTxVec[id - 1]);
         Tx tx(rawTxVec[id - 1]);
         hashSet.insert(tx.getThisHash());
         auto localAddrSet = getAddressesForRawTx(tx);
         addrSet.insert(localAddrSet.begin(), localAddrSet.end());
      }
      instance->bdvPtr_->broadcastZC(zcs);
      instance->callbackPtr_->waitOnZc(hashSet, addrSet);

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

      std::set<BinaryData> scrAddrSet1, scrAddrSet2;
      BinaryData hash1, hash2;
      {
         Tx tx(rawTxVec[0]);
         hash1 = tx.getThisHash();
         scrAddrSet1 = getAddressesForRawTx(tx);
      }

      {
         Tx tx(rawTxVec[1]);
         hash2 = tx.getThisHash();
         scrAddrSet2 = getAddressesForRawTx(tx);
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
      mainInstance->callbackPtr_->waitOnZc({hash1}, scrAddrSet1);
      mainInstance->callbackPtr_->waitOnZc({hash2}, scrAddrSet2);

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
   WebSocketServer::initAuthPeers(authPeersPassLbd_);
   WebSocketServer::start(theBDMt_->bdm(), true);
   auto serverPubkey = WebSocketServer::getPublicKey();
   theBDMt_->start(DBSettings::initMode());

   {
      auto pCallback = std::make_shared<DBTestUtils::UTCallback>();
      auto bdvObj = AsyncClient::BlockDataViewer::getNewBDV(
         "127.0.0.1", NetworkSettings::dbPort(),
         Armory::Config::getDataDir(),
         authPeersPassLbd_,
         NetworkSettings::ephemeralPeers(), true, //public server
         pCallback);
      bdvObj->addPublicKey(serverPubkey);
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
      feed->addPrivKey(TestChain::privKeyAddrB);
      feed->addPrivKey(TestChain::privKeyAddrC);
      feed->addPrivKey(TestChain::privKeyAddrD);
      feed->addPrivKey(TestChain::privKeyAddrE);
      feed->addPrivKey(TestChain::privKeyAddrF);

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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxosB[0]);
         signer.addSpender(spender);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recA);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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
         Signer signer;

         auto spender = std::make_shared<ScriptSpender>(utxoD);
         signer.addSpender(spender);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 10 * COIN);
         signer.addRecipient(recE);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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

         Signer signer;
         
         auto spender1 = std::make_shared<ScriptSpender>(zcUtxo1);
         auto spender2 = std::make_shared<ScriptSpender>(zcUtxo2);
         signer.addSpender(spender1);
         signer.addSpender(spender2);

         auto recA = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrA.getSliceCopy(1, 20), 3 * COIN);
         signer.addRecipient(recA);

         auto recE = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrE.getSliceCopy(1, 20), 2 * COIN);
         signer.addRecipient(recE);
         
         auto recD = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), 5 * COIN);
         signer.addRecipient(recD);

         auto recChange = std::make_shared<Recipient_P2PKH>(
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

   std::cout << "Running with following parameters:" << std::endl;
   std::cout << "   MEMPOOL_DEPTH: " << MEMPOOL_DEPTH << std::endl;
   std::cout << "   POOL_MERGE_THRESHOLD: " << POOL_MERGE_THRESHOLD << std::endl;
   std::cout << "   COINBASE_MATURITY: " << COINBASE_MATURITY << std::endl;

   CryptoECDSA::setupContext();

   srand(time(0));
   std::cout << "Running main() from gtest_main.cc" << std::endl;

   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();

   FLUSHLOG();
   CLEANUPLOG();

   CryptoECDSA::shutdown();
   return exitCode;
}