////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "TestUtils.h"
#include "CoinSelection.h"
#include "../Wallets/Seeds/Seeds.h"

using namespace Armory::Signing;
using namespace Armory::Config;
using namespace Armory::Assets;
using namespace Armory::Accounts;
using namespace Armory::Wallets;
using namespace Armory::CoinSelection;

using namespace std::string_view_literals;
using namespace std::chrono_literals;

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<ScriptSpender> getSpenderPtr(const UTXO& utxo, bool RBF = false)
{
   auto spender = std::make_shared<ScriptSpender>(utxo);
   if (RBF) {
      spender->setSequence(UINT32_MAX - 2);
   }
   return spender;
}

////////////////////////////////////////////////////////////////////////////////
class PRNGTest : public ::testing::Test
{
protected:
   virtual void SetUp(void)
   {}

   virtual void TearDown(void)
   {}
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(PRNGTest, FortunaTest)
{
   unsigned sampleSize = 1000000;

   auto checkPools = [](
      const std::set<SecureBinaryData>& p1,
      const std::set<SecureBinaryData>& p2,
      size_t sampleSize, size_t len)
      ->std::vector<unsigned>
   {
      unsigned collisionP1 = 0;
      unsigned collisionP2 = 0;
      unsigned collisions = 0;
      unsigned offSizes = 0;
      if (p1.size() != sampleSize) {
         collisionP1 = sampleSize - p1.size();
      }
      if (p2.size() != sampleSize) {
         collisionP2 = sampleSize - p2.size();
      }

      for (auto& data : p1) {
         if (data.getSize() != len) {
            ++offSizes;
         }
         auto iter = p2.find(data);
         if (iter != p2.end()) {
            ++collisions;
         }
      }

      for (auto& data : p2) {
         if (data.getSize() != len) {
            ++offSizes;
         }
      }

      return { collisionP1, collisionP2, collisions, offSizes };
   };

   PRNG_Fortuna prng1;
   PRNG_Fortuna prng2;

   //conscutive
   std::set<SecureBinaryData> pool1, pool2;
   for (unsigned i=0; i<sampleSize; i++) {
      pool1.insert(prng1.generateRandom(32));
   }
   for (unsigned i=0; i<sampleSize; i++) {
      pool2.insert(prng2.generateRandom(32));
   }
   auto check1 = checkPools(pool1, pool2, sampleSize, 32);
   EXPECT_EQ(check1[0], 0ULL);
   EXPECT_EQ(check1[1], 0ULL);
   EXPECT_EQ(check1[2], 0ULL);
   EXPECT_EQ(check1[3], 0ULL);

   //interlaced
   std::set<SecureBinaryData> pool3, pool4;
   auto thread2 = [&pool4, &prng2, &sampleSize]
   {
      for (unsigned i=0; i<sampleSize; i++) {
         pool4.insert(prng2.generateRandom(32));
      }
   };

   std::thread thr2(thread2);
   for (unsigned i=0; i<sampleSize; i++) {
      pool3.insert(prng1.generateRandom(32));
   }
   thr2.join();

   auto check2 = checkPools(pool3, pool4, sampleSize, 32);
   EXPECT_EQ(check2[0], 0ULL);
   EXPECT_EQ(check2[1], 0ULL);
   EXPECT_EQ(check2[2], 0ULL);
   EXPECT_EQ(check2[3], 0ULL);

   //cross checks
   auto check3 = checkPools(pool1, pool3, sampleSize, 32);
   EXPECT_EQ(check3[0], 0ULL);
   EXPECT_EQ(check3[1], 0ULL);
   EXPECT_EQ(check3[2], 0ULL);
   EXPECT_EQ(check3[3], 0ULL);

   auto check4 = checkPools(pool1, pool4, sampleSize, 32);
   EXPECT_EQ(check4[0], 0ULL);
   EXPECT_EQ(check4[1], 0ULL);
   EXPECT_EQ(check4[2], 0ULL);
   EXPECT_EQ(check4[3], 0ULL);

   auto check5 = checkPools(pool2, pool3, sampleSize, 32);
   EXPECT_EQ(check5[0], 0ULL);
   EXPECT_EQ(check5[1], 0ULL);
   EXPECT_EQ(check5[2], 0ULL);
   EXPECT_EQ(check5[3], 0ULL);

   auto check6 = checkPools(pool2, pool4, sampleSize, 32);
   EXPECT_EQ(check6[0], 0ULL);
   EXPECT_EQ(check6[1], 0ULL);
   EXPECT_EQ(check6[2], 0ULL);
   EXPECT_EQ(check6[3], 0ULL);

   //odd size pulls
   std::set<SecureBinaryData> pool5, pool6;
   for (unsigned i=0; i<100; i++) {
      pool5.insert(prng1.generateRandom(15));
   }
   for (unsigned i=0; i<100; i++) {
      pool6.insert(prng2.generateRandom(15));
   }

   auto check7 = checkPools(pool5, pool6, 100, 15);
   EXPECT_EQ(check7[0], 0ULL);
   EXPECT_EQ(check7[1], 0ULL);
   EXPECT_EQ(check7[2], 0ULL);
   EXPECT_EQ(check7[3], 0ULL);

   //
   std::set<SecureBinaryData> pool7, pool8;
   for (unsigned i=0; i<100; i++) {
      pool7.insert(prng1.generateRandom(70));
   }
   for (unsigned i=0; i<100; i++) {
      pool8.insert(prng2.generateRandom(70));
   }

   auto check8 = checkPools(pool7, pool8, 100, 70);
   EXPECT_EQ(check8[0], 0ULL);
   EXPECT_EQ(check8[1], 0ULL);
   EXPECT_EQ(check8[2], 0ULL);
   EXPECT_EQ(check8[3], 0ULL);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class SignerTest : public ::testing::Test
{
protected:
   void initBDM(void)
   {
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
      ghash_ = READHEX(MAINNET_GENESIS_HASH_HEX);
      gentx_ = READHEX(MAINNET_GENESIS_TX_HASH_HEX);
      zeros_ = READHEX("00000000");

      FileUtils::removeDirectory(blkdir_);
      FileUtils::removeDirectory(homedir_);
      FileUtils::removeDirectory(ldbdir_);

      FileUtils::createDirectory(blkdir_ / "blocks");
      FileUtils::createDirectory(homedir_);
      FileUtils::createDirectory(ldbdir_);

      DBSettings::setServiceType(SERVICE_UNITTEST);

      // Put the first 5 blocks into the blkdir
      blk0dat_ = FileUtils::getBlkFilename(blkdir_ / "blocks", 0);
      TestUtils::setBlocks({ "0", "1", "2", "3", "4", "5" }, blk0dat_);

      Armory::Config::parseArgs({
         "--datadir=./fakehomedir",
         "--dbdir=./ldbtestdir",
         "--satoshi-datadir=./blkfiletest",
         "--public",
         "--db-type=DB_FULL",
         "--thread-count=3",
         "--public" },
         Armory::Config::ProcessType::DB);

      wallet1id = "wallet1";
      wallet2id = "wallet2";
      LB1ID = TestChain::lb1B58ID;
      LB2ID = TestChain::lb2B58ID;
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
      FileUtils::removeDirectory(ldbdir_);

      Armory::Config::reset();
      CLEANUP_ALL_TIMERS();
   }

   BlockDataManagerThread *theBDMt_ = nullptr;
   Clients* clients_ = nullptr;
   LMDBBlockDatabase* iface_;
   BinaryData ghash_;
   BinaryData gentx_;
   BinaryData zeros_;

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
TEST_F(SignerTest, DISABLED_CheckChain_Test)
{
   //this test fails because the p2sh tx in our unit test chain are botched
   //(the input script has opcode when it should only be push data)

   BlockDataManager bdm(nullptr);
   try {
      bdm.doInitialSyncOnLoad(TestUtils::nullProgress);
   } catch (const std::exception&) {
      //signify the failure
      EXPECT_TRUE(false);
   }
   EXPECT_EQ(bdm.getCheckedTxCount(), 20U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, Signer_Test)
{
   //
   TestUtils::setBlocks({ "0", "1", "2" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);

   //// spend 2 from wlt to scrAddrF, rest back to scrAddrA ////
   auto spendVal = 2 * COIN;
   Signer signer;

   //instantiate resolver feed overloaded object
   auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
   feed->addPrivKey(TestChain::privKeyAddrB);
   feed->addPrivKey(TestChain::privKeyAddrC);
   feed->addPrivKey(TestChain::privKeyAddrD);
   feed->addPrivKey(TestChain::privKeyAddrE);

   //get utxo list for spend value
   auto&& unspentVec = wlt->getSpendableTxOutListForValue(spendVal);

   //create script spender objects
   uint64_t total = 0;
   for (auto& utxo : unspentVec) {
      total += utxo.getValue();
      signer.addSpender(getSpenderPtr(utxo));
   }

   //add spend to addr F, use P2PKH
   auto recipientF = std::make_shared<Recipient_P2PKH>(
      TestChain::scrAddrF.getSliceCopy(1, 20), spendVal);
   signer.addRecipient(recipientF);

   if (total > spendVal) {
      //deal with change, no fee
      auto changeVal = total - spendVal;
      auto recipientA = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrA.getSliceCopy(1, 20), changeVal);
      signer.addRecipient(recipientA);
   }

   signer.setFeed(feed);
   signer.sign();
   EXPECT_TRUE(signer.verify());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_SizeEstimates)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   IO::CreationParams params {
      homedir_,
      {}, 1ms,
      {}, 1ms,
      5
   };

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
         signer.addSpender(getSpenderPtr(utxo));
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
         auto addr2 = assetWlt->getNewChangeAddress();
         signer.addRecipient(addr2->getRecipient(changeVal));
         addrVec.push_back(addr2->getPrefixedHash());
      }

      //add op_return output for coverage
      auto opreturn_msg = BinaryData::fromString("testing op_return"sv);
      signer.addRecipient(std::make_shared<Recipient_OPRETURN>(opreturn_msg));

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //check new wallet balances
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]);
   EXPECT_EQ(scrObj->getFullBalance(), 3 * COIN);

   uint64_t feeVal = 0;
   {
      ////spend 18 back to scrAddrB, with change to addr[2]

      auto spendVal = 18 * COIN;
      Signer signer2;

      auto getUtxos = [dbAssetWlt](uint64_t)->std::vector<UTXO>
      {
         auto unspentVec = dbAssetWlt->getSpendableTxOutListZC();
         std::vector<UTXO> utxoVec;
         for (auto& unspentTxo : unspentVec) {
            UTXO entry(unspentTxo.value_, unspentTxo.txHeight_,
               unspentTxo.txIndex_, unspentTxo.txOutIndex_,
               std::move(unspentTxo.txHash_),
               std::move(unspentTxo.script_));
            utxoVec.emplace_back(entry);
         }
         return utxoVec;
      };

      auto addrBook = dbAssetWlt->createAddressBook();
      auto topBlock = theBDMt_->bdm()->blockchain()->top()->getBlockHeight();
      CoinSelectionInstance csi(assetWlt, getUtxos,
         addrBook, dbAssetWlt->getUnconfirmedBalance(topBlock), 
         topBlock);

      //spend 18 to addr B, use P2PKH
      csi.addRecipient(TestChain::scrAddrB, spendVal);

      float desiredFeeByte = 200.0f;
      csi.selectUTXOs(0, desiredFeeByte, 0);
      auto utxoSelect = csi.getUtxoSelection();

      //create feed from asset wallet
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //create spenders
      uint64_t total = 0;
      for (auto& utxo : utxoSelect) {
         total += utxo.getValue();
         signer2.addSpender(std::make_shared<ScriptSpender>(utxo));
      }

      //add recipients to signer
      auto& csRecipients = csi.getRecipients();
      for (const auto& group : csRecipients) {
         for (const auto& recipient : group.second) {
            signer2.addRecipient(recipient, group.first);
         }
      }

      if (total > spendVal) {
         //deal with change
         auto changeVal = total - spendVal - csi.getFlatFee();
         feeVal = csi.getFlatFee();
         auto addr3 = assetWlt->getNewChangeAddress(
            AddressEntryType(AddressEntryType_P2WPKH | AddressEntryType_P2SH));
         signer2.addRecipient(addr3->getRecipient(changeVal));
         addrVec.push_back(addr3->getPrefixedHash());
      }

      //sign, verify & broadcast
      {
         auto lock = assetWlt->lockDecryptedContainer();
         signer2.setFeed(assetFeed);
         signer2.sign();
      }

      EXPECT_TRUE(signer2.verify());

      DBTestUtils::ZcVector zcVec2;
      auto txref = signer2.serializeSignedTx();

      //size estimate should not deviate from the signed tx size by more than 4 bytes
      //per input (DER sig size variance)
      EXPECT_TRUE(csi.getSizeEstimate() < txref.getSize() + utxoSelect.size() * 2);
      EXPECT_TRUE(csi.getSizeEstimate() > txref.getSize() - utxoSelect.size() * 2);
      zcVec2.push_back(signer2.serializeSignedTx(), 15000000);

      //check fee/byte matches tx size
      auto totalFee = total - zcVec2.zcVec_[0].first.getSumOfOutputs();
      EXPECT_EQ(totalFee, csi.getFlatFee());
      float fee_byte = float(totalFee) / float(zcVec2.zcVec_[0].first.getTxWeight());
      auto fee_byte_diff = fee_byte - desiredFeeByte;

      EXPECT_TRUE(fee_byte_diff < 2.0f);
      EXPECT_TRUE(fee_byte_diff > -2.0f);

      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 48 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //check new wallet balances
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]);
   EXPECT_EQ(scrObj->getFullBalance(), 3 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[3]);
   EXPECT_EQ(scrObj->getFullBalance(), 9 * COIN - feeVal);

   uint64_t feeVal2;
   {
      ////spend 18 back to scrAddrB, with change to addr[2]

      Signer signer3;
      signer3.setFlags(SCRIPT_VERIFY_SEGWIT);

      auto getUtxos = [dbAssetWlt](uint64_t)->std::vector<UTXO>
      {
         auto unspentVec = dbAssetWlt->getSpendableTxOutListZC();

         std::vector<UTXO> utxoVec;
         for (auto& unspentTxo : unspentVec) {
            UTXO entry(unspentTxo.value_, unspentTxo.txHeight_,
               unspentTxo.txIndex_, unspentTxo.txOutIndex_,
               std::move(unspentTxo.txHash_),
               std::move(unspentTxo.script_));
            utxoVec.emplace_back(entry);
         }
         return utxoVec;
      };

      auto addrBook = dbAssetWlt->createAddressBook();
      auto topBlock = theBDMt_->bdm()->blockchain()->top()->getBlockHeight();
      CoinSelectionInstance csi(assetWlt, getUtxos,
         addrBook, dbAssetWlt->getUnconfirmedBalance(topBlock),
         topBlock);

      //have to add the recipient with 0 val for MAX fee estimate
      float desiredFeeByte = 200.0f;
      auto recipientID = csi.addRecipient(TestChain::scrAddrD, 0);
      feeVal2 = csi.getFeeForMaxVal(desiredFeeByte);
      auto spendVal = dbAssetWlt->getUnconfirmedBalance(topBlock);
      spendVal -= feeVal2;

      //spend 18 to addr D, use P2PKH
      csi.updateRecipient(recipientID, TestChain::scrAddrD, spendVal);

      csi.selectUTXOs(0, desiredFeeByte, 0);
      auto utxoSelect = csi.getUtxoSelection();

      //create feed from asset wallet
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //create spenders
      uint64_t total = 0;
      for (auto& utxo : utxoSelect) {
         total += utxo.getValue();
         signer3.addSpender(std::make_shared<ScriptSpender>(utxo));
      }

      //add recipients to signer
      auto& csRecipients = csi.getRecipients();
      for (const auto& group : csRecipients) {
         for (const auto& recipient : group.second) {
            signer3.addRecipient(recipient, group.first);
         }
      }
      EXPECT_EQ(total, spendVal + feeVal2);

      //sign, verify & broadcast
      {
         auto lock = assetWlt->lockDecryptedContainer();
         signer3.setFeed(assetFeed);
         signer3.sign();
      }
      EXPECT_TRUE(signer3.verify());

      DBTestUtils::ZcVector zcVec2;
      auto txref = signer3.serializeSignedTx();

      //size estimate should not deviate from the signed tx size by more than 4 bytes
      //per input (DER sig size variance)
      EXPECT_TRUE(csi.getSizeEstimate() < txref.getSize() + utxoSelect.size() * 2);
      EXPECT_TRUE(csi.getSizeEstimate() > txref.getSize() - utxoSelect.size() * 2);

      zcVec2.push_back(signer3.serializeSignedTx(), 15000000);

      //check fee/byte matches tx size
      auto totalFee = total - zcVec2.zcVec_[0].first.getSumOfOutputs();
      EXPECT_EQ(totalFee, csi.getFlatFee());
      float fee_byte = float(totalFee) / float(zcVec2.zcVec_[0].first.getTxWeight());
      auto fee_byte_diff = fee_byte - desiredFeeByte;

      EXPECT_TRUE(fee_byte_diff < 2.0f);
      EXPECT_TRUE(fee_byte_diff > -2.0f);

      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 48 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 55 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 17 * COIN - feeVal - feeVal2);
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
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_P2WPKH)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto&& bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   IO::CreationParams params {
      homedir_,
      {}, 1ms, {}, 1ms, 5
   };

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //register with db
   std::vector<std::shared_ptr<AddressEntry>> addrVec;
   addrVec.push_back(assetWlt->getNewAddress(AddressEntryType_P2WPKH));
   addrVec.push_back(assetWlt->getNewAddress(AddressEntryType_P2WPKH));
   addrVec.push_back(assetWlt->getNewAddress(AddressEntryType_P2WPKH));

   std::vector<BinaryData> hashVec;
   for (auto addrPtr : addrVec) {
      hashVec.push_back(addrPtr->getPrefixedHash());
   }

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
   for (auto& addrPtr : addrVec) {
      scrObj = dbAssetWlt->getScrAddrObjByKey(addrPtr->getPrefixedHash());
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 12 to addr0, use P2WPKH
      signer.addRecipient(addrVec[0]->getRecipient(12 * COIN));

      //spend 15 to addr1, use P2WPKH
      signer.addRecipient(addrVec[1]->getRecipient(15 * COIN));

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

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   {
      ////spend 18 back to scrAddrB, with change to addr2

      auto spendVal = 18 * COIN;
      Signer signer2;
      Signer signer_nofeed;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListZC();

      //create feed from asset wallet
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //create spenders
      uint64_t total = 0;
      for (auto& utxo : unspentVec) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo));
         signer_nofeed.addSpender(getSpenderPtr(utxo));
      }

      //creates outputs
      //spend 18 to scrAddrB, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), spendVal);
      signer2.addRecipient(recipient2);
      signer_nofeed.addRecipient(recipient2);

      if (total > spendVal) {
         //change to addr2, use P2WPKH
         auto changeVal = total - spendVal;
         signer2.addRecipient(addrVec[2]->getRecipient(changeVal));
         signer_nofeed.addRecipient(addrVec[2]->getRecipient(changeVal));
      }

      //grab the unsigned tx and get the tx hash from it
      BinaryData txHashUnsigned;
      {
         signer2.setFeed(assetFeed);
         auto unsignedTxRaw = signer2.serializeUnsignedTx();

         Tx unsignedTx(unsignedTxRaw);
         txHashUnsigned = unsignedTx.getThisHash();
      }

      auto hashFromSigner = signer2.getTxId();
      EXPECT_EQ(txHashUnsigned, hashFromSigner);

      auto hashFromUnresolvedSigner = signer_nofeed.getTxId();
      EXPECT_EQ(hashFromSigner, hashFromUnresolvedSigner);

      //sign, verify & broadcast
      {
         auto lock = assetWlt->lockDecryptedContainer();
         signer2.sign();
      }
      EXPECT_TRUE(signer2.verify());

      DBTestUtils::ZcVector zcVec2;
      auto signedTxRaw = signer2.serializeSignedTx();
      zcVec2.push_back(signedTxRaw, 15000000);

      Tx signedTx(signedTxRaw);
      EXPECT_EQ(signedTx.getThisHash(), txHashUnsigned);

      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

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
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 9 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_MixedInputTypes)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   IO::CreationParams params {
      homedir_, {}, 1ms, {}, 1ms, 5
   };

   auto rawEntropy = CryptoPRNG::generateRandom(32);
   BIP32_Node node;
   node.initFromSeed(rawEntropy);

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135(rawEntropy));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //add a bip32 account
   {
      auto lock = assetWlt->lockDecryptedContainer();
      auto accTypePtr = AccountType_BIP32::makeFromDerPaths(
         node.getThisFingerprint(), {{0x80000000, 0x80000000, 0, 1}});
      accTypePtr->setSeedRoot(node.getBase58());
      accTypePtr->setAddressLookup(5);
      accTypePtr->addAddressType(AddressEntryType_P2WPKH);
      accTypePtr->setDefaultAddressType(AddressEntryType_P2WPKH);
      assetWlt->createBIP32Account(accTypePtr);
   }

   //register with db
   std::vector<std::shared_ptr<AddressEntry>> addrVec;
   addrVec.push_back(assetWlt->getNewAddress(AddressEntryType(
         AddressEntryType_P2PKH | AddressEntryType_Uncompressed)));
   addrVec.push_back(assetWlt->getNewAddress(AddressEntryType_P2WPKH));
   addrVec.push_back(assetWlt->getNewAddress(AddressEntryType(
         AddressEntryType_P2PK | AddressEntryType_P2SH)));
   addrVec.push_back(assetWlt->getNewAddress(AddressEntryType(
         AddressEntryType_P2WPKH | AddressEntryType_P2SH)));
   addrVec.push_back(assetWlt->getNewAddress(AddressEntryType_P2WPKH));

   std::vector<BinaryData> hashVec;
   for (auto addrPtr : addrVec) {
      hashVec.push_back(addrPtr->getPrefixedHash());
   }
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
   for (auto& addrPtr : addrVec) {
      scrObj = dbAssetWlt->getScrAddrObjByKey(addrPtr->getPrefixedHash());
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 6 to addr0, uncompressed P2PKH
      signer.addRecipient(addrVec[0]->getRecipient(6* COIN));

      //spend 7 to addr1, P2WPKH
      signer.addRecipient(addrVec[1]->getRecipient(7 * COIN));

      //spend 2 to addr1, nested P2PK
      signer.addRecipient(addrVec[2]->getRecipient(2 * COIN));

      //spend 12 to addr1, nested P2WPKH
      signer.addRecipient(addrVec[3]->getRecipient(12 * COIN));

      if (total > spendVal)
      {
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

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 6 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 7 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 2 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[3]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[4]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   {
      ////spend 18 back to scrAddrB, with change to addr2

      auto spendVal = 18 * COIN;
      Signer signer2;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //get utxo list for spend value
      auto&& unspentVec = dbAssetWlt->getSpendableTxOutListZC();

      //create feed from asset wallet
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //create spenders
      uint64_t total = 0;
      for (auto& utxo : unspentVec) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo));
      }

      //creates outputs
      //spend 18 to scrAddrB, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), spendVal);
      signer2.addRecipient(recipient2);

      if (total > spendVal) {
         //change to addr2, use P2WPKH
         auto changeVal = total - spendVal;
         signer2.addRecipient(addrVec[4]->getRecipient(changeVal));
      }

      //sign, verify & broadcast
      {
         signer2.setFeed(assetFeed);
         auto lock = assetWlt->lockDecryptedContainer();
         signer2.sign();
      }
      EXPECT_TRUE(signer2.verify());
      EXPECT_EQ(signer2.getTxInCount(), 4U);

      DBTestUtils::ZcVector zcVec2;
      auto signedTxRaw = signer2.serializeSignedTx();
      zcVec2.push_back(signedTxRaw, 15000000);

      Tx signedTx(signedTxRaw);

      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

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
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[3]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[4]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 9 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_MultipleSigners_1of3)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   IO::CreationParams params {
      homedir_, {}, 1ms, {}, 1ms, 3
   };

   //// create 3 assetWlt ////
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed1(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt_1 = AssetWallet_Single::createFromSeed(
      std::move(seed1), params);

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed2(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt_2 = AssetWallet_Single::createFromSeed(
      std::move(seed2), params);

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed3(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt_3 = AssetWallet_Single::createFromSeed(
      std::move(seed3), params);

   //create 1-of-3 multisig asset entry from 3 different wallets
   std::map<BinaryData, std::shared_ptr<AssetEntry>> asset_single_map;

   auto accountPtr1 = assetWlt_1->getAccountForID(assetWlt_1->getMainAccountID());
   auto outerAcc1 = accountPtr1->getOuterAccount();
   auto asset1 = outerAcc1->getAssetForKey(0);
   auto wltid1_bd = assetWlt_1->getID();
   asset_single_map.emplace(BinaryData::fromString(wltid1_bd), asset1);

   auto accountPtr2 = assetWlt_2->getAccountForID(assetWlt_2->getMainAccountID());
   auto outerAcc2 = accountPtr2->getOuterAccount();
   auto asset2 = outerAcc2->getAssetForKey(0);
   auto wltid2_bd = assetWlt_2->getID();
   asset_single_map.emplace(BinaryData::fromString(wltid2_bd), asset2);

   auto accountPtr3 = assetWlt_3->getAccountForID(assetWlt_3->getMainAccountID());
   auto outerAcc3 = accountPtr3->getOuterAccount();
   auto asset3 = outerAcc3->getAssetForKey(0);
   auto wltid3_bd = assetWlt_3->getID();
   asset_single_map.emplace(BinaryData::fromString(wltid3_bd), asset3);

   auto ae_ms = std::make_shared<AssetEntry_Multisig>(
      AssetId(0, 0, 0),
      asset_single_map, 1, 3);
   auto addr_ms_raw = std::make_shared<AddressEntry_Multisig>(ae_ms, true);
   auto addr_p2wsh = std::make_shared<AddressEntry_P2WSH>(addr_ms_raw);
   auto addr_ms = std::make_shared<AddressEntry_P2SH>(addr_p2wsh);

   //register with db
   std::vector<BinaryData> addrVec;
   addrVec.push_back(addr_ms->getPrefixedHash());

   DBTestUtils::registerWallet(clients_, bdvID, addrVec, "ms_entry",
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto ms_wlt = bdvPtr->getWalletOrLockbox("ms_entry");

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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 27 nested p2wsh script hash
      signer.addRecipient(addr_ms->getRecipient(27 * COIN));

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //add op_return output for coverage
      auto opreturn_msg = BinaryData::fromString("testing op_return 0123"sv);
      signer.addRecipient(std::make_shared<Recipient_OPRETURN>(opreturn_msg));

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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
   scrObj = ms_wlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 27 * COIN);

   //lambda to sign with each wallet
   auto signPerWallet = [&](std::shared_ptr<AssetWallet_Single> wltPtr, 
      BinaryData& unsignedHash)->BinaryData
   {
      ////spend 18 back to scrAddrB, with change to self

      auto spendVal = 18 * COIN;
      Signer signer2;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //get utxo list for spend value
      auto unspentVec = ms_wlt->getSpendableTxOutListZC();

      //create feed from asset wallet
      auto feed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle_ForMultisig>(wltPtr);
      auto assetFeed = std::make_shared<ResolverUtils::CustomFeed>(addr_ms, feed);

      //create spenders
      uint64_t total = 0;
      for (auto& utxo : unspentVec) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo));
      }

      //creates outputs
      //spend 18 to addr 0, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), spendVal);
      signer2.addRecipient(recipient2);

      if (total > spendVal) {
         //deal with change, no fee
         auto changeVal = total - spendVal;
         signer2.addRecipient(addr_ms->getRecipient(changeVal));
      }

      //add op_return output for coverage
      auto opreturn_msg = BinaryData::fromString("testing op_return 0123"sv);
      signer2.addRecipient(std::make_shared<Recipient_OPRETURN>(opreturn_msg));

      {
         signer2.setFeed(assetFeed);
         auto hash = signer2.getTxId();
         auto unsignedTx = signer2.serializeUnsignedTx();
         Tx tx(unsignedTx);
         unsignedHash = tx.getThisHash();
         EXPECT_EQ(unsignedHash, hash);
      }

      //sign, verify & return signed tx
      {
         auto lock = wltPtr->lockDecryptedContainer();
         signer2.sign();
      }
      EXPECT_TRUE(signer2.verify());

      return signer2.serializeSignedTx();
   };

   //call lambda with each wallet
   BinaryData unsignedHash1, unsignedHash2, unsignedHash3;
   auto tx1 = signPerWallet(assetWlt_1, unsignedHash1);
   auto tx2 = signPerWallet(assetWlt_2, unsignedHash2);
   auto tx3 = signPerWallet(assetWlt_3, unsignedHash3);

   {
      Tx tx_1(tx1);
      EXPECT_EQ(tx_1.getThisHash(), unsignedHash1);
      
      Tx tx_2(tx2);
      EXPECT_EQ(tx_2.getThisHash(), unsignedHash2);
      
      Tx tx_3(tx3);
      EXPECT_EQ(tx_3.getThisHash(), unsignedHash3);
   }

   //broadcast the last one
   DBTestUtils::ZcVector zcVec;
   zcVec.push_back(tx3, 15000000);

   DBTestUtils::pushNewZc(theBDMt_, zcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

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
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_MultipleSigners_2of3_NativeP2WSH)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   IO::CreationParams params {
      homedir_, {}, 1ms, {}, 1ms, 3
   };

   //// create 3 assetWlt ////
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed1(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt_1 = AssetWallet_Single::createFromSeed(
      std::move(seed1), params);

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed2(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt_2 = AssetWallet_Single::createFromSeed(
      std::move(seed2), params);

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed3(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt_3 = AssetWallet_Single::createFromSeed(
      std::move(seed3), params);

   //create 2-of-3 multisig asset entry from 3 different wallets
   std::map<BinaryData, std::shared_ptr<AssetEntry>> asset_single_map;

   auto accountPtr1 = assetWlt_1->getAccountForID(assetWlt_1->getMainAccountID());
   auto outerAcc1 = accountPtr1->getOuterAccount();
   auto asset1 = outerAcc1->getAssetForKey(0);
   auto wltid1_bd = assetWlt_1->getID();
   asset_single_map.emplace(BinaryData::fromString(wltid1_bd), asset1);

   auto accountPtr2 = assetWlt_2->getAccountForID(assetWlt_2->getMainAccountID());
   auto outerAcc2 = accountPtr2->getOuterAccount();
   auto asset2 = outerAcc2->getAssetForKey(0);
   auto wltid2_bd = assetWlt_2->getID();
   asset_single_map.emplace(BinaryData::fromString(wltid2_bd), asset2);

   auto asset4_singlesig = assetWlt_2->getNewAddress();

   auto accountPtr3 = assetWlt_3->getAccountForID(assetWlt_3->getMainAccountID());
   auto outerAcc3 = accountPtr3->getOuterAccount();
   auto asset3 = outerAcc3->getAssetForKey(0);
   auto wltid3_bd = assetWlt_3->getID();
   asset_single_map.emplace(BinaryData::fromString(wltid3_bd), asset3);

   auto ae_ms = std::make_shared<AssetEntry_Multisig>(
      AssetId::getRootAssetId(),
      asset_single_map, 2, 3);
   auto addr_ms_raw = std::make_shared<AddressEntry_Multisig>(ae_ms, true);
   auto addr_p2wsh = std::make_shared<AddressEntry_P2WSH>(addr_ms_raw);


   //register with db
   std::vector<BinaryData> addrVec;
   addrVec.push_back(addr_p2wsh->getPrefixedHash());

   std::vector<BinaryData> addrVec_singleSig;
   auto addrSet = assetWlt_2->getAddrHashSet();
   for (auto& addr : addrSet) {
      addrVec_singleSig.push_back(addr);
   }
   DBTestUtils::registerWallet(clients_, bdvID, addrVec, "ms_entry",
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, addrVec_singleSig, assetWlt_2->getID(),
      false, false);
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 20 to nested p2wsh script hash
      signer.addRecipient(addr_p2wsh->getRecipient(20 * COIN));

      //spend 7 to assetWlt_2
      signer.addRecipient(asset4_singlesig->getRecipient(7 * COIN));

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());
      auto zcHash = signer.getTxId();

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

      //grab ZC from DB and verify it again
      auto zc_from_db = DBTestUtils::getTxByHash(clients_, bdvID, zcHash);
      auto raw_tx = zc_from_db.serialize();
      auto bctx = BCTX::parse(raw_tx);
      Armory::Signing::TransactionVerifier tx_verifier(*bctx, utxoVec);

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

   //get the zc utxo (ms script)
   auto unspentVec = ms_wlt->getSpendableTxOutListZC();
   ASSERT_EQ(unspentVec.size(), 1ULL);

   auto unspentVec_singleSig = wlt_singleSig->getSpendableTxOutListZC();
   ASSERT_EQ(unspentVec_singleSig.size(), 1ULL);

   unspentVec.insert(unspentVec.end(),
      unspentVec_singleSig.begin(), unspentVec_singleSig.end());

   //create feed from asset wallet 1
   auto feed_ms = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle_ForMultisig>(assetWlt_1);
   auto assetFeed = std::make_shared<ResolverUtils::CustomFeed>(addr_p2wsh, feed_ms);

   //create spenders
   uint64_t total = 0;
   for (auto& utxo : unspentVec) {
      total += utxo.getValue();
      signer2.addSpender(getSpenderPtr(utxo));
   }

   //creates outputs
   //spend 18 to addr 0, use P2PKH
   auto recipient2 = std::make_shared<Recipient_P2PKH>(
      TestChain::scrAddrB.getSliceCopy(1, 20), spendVal);
   signer2.addRecipient(recipient2);

   if (total > spendVal) {
      //deal with change, no fee
      auto changeVal = total - spendVal;
      signer2.addRecipient(addr_p2wsh->getRecipient(changeVal));
   }

   //sign, verify & return signed tx
   signer2.setFeed(assetFeed);
   signer2.resolvePublicData();
   auto&& signerState = signer2.evaluateSignedState();

   {
      EXPECT_EQ(signerState.getEvalMapSize(), 2U);

      const auto& txinEval = signerState.getSignedStateForInput(0);
      const auto& pubkeyMap = txinEval.getPubKeyMap();
      EXPECT_EQ(pubkeyMap.size(), 3ULL);
      for (auto& pubkeyState : pubkeyMap)
         EXPECT_FALSE(pubkeyState.second);

      const auto& txinEval2 = signerState.getSignedStateForInput(1);
      const auto& pubkeyMap_2 = txinEval2.getPubKeyMap();
      EXPECT_EQ(pubkeyMap_2.size(), 0ULL);
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

      auto&& txinEval = signerState.getSignedStateForInput(0);
      EXPECT_EQ(txinEval.getSigCount(), 1U);

      auto asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(asset1);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));
   }

   //create feed from asset wallet 2
   Signer signer3;
   auto feed_ms3 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle_ForMultisig>(assetWlt_2);
   auto assetFeed3 = std::make_shared<ResolverUtils::CustomFeed>(addr_p2wsh, feed_ms3);
   signer3.deserializeState(signer2.serializeState());

   {
      //make sure sig was properly carried over with state
      EXPECT_FALSE(signer3.isSigned());
      signerState = signer3.evaluateSignedState();

      EXPECT_EQ(signerState.getEvalMapSize(), 2U);
      auto&& txinEval = signerState.getSignedStateForInput(0);
      EXPECT_EQ(txinEval.getSigCount(), 1U);

      auto asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(asset1);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));
   }

   signer3.setFeed(assetFeed3);
   {
      auto lock = assetWlt_2->lockDecryptedContainer();
      signer3.sign();

      signerState = signer3.evaluateSignedState();
      EXPECT_EQ(signerState.getEvalMapSize(), 2U);
      auto&& txinEval = signerState.getSignedStateForInput(0);
      EXPECT_EQ(txinEval.getSigCount(), 2U);

      auto asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(asset2);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));
   }

   {
      auto assetFeed4 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_2);
      signer3.resetFeed();
      signer3.setFeed(assetFeed4);
      auto lock = assetWlt_2->lockDecryptedContainer();
      signer3.sign();
   }

   ASSERT_TRUE(signer3.isSigned());
   EXPECT_TRUE(signer3.verify());

   {
      //should have 2 sigs now
      EXPECT_TRUE(signer3.isSigned());
      signerState = signer3.evaluateSignedState();

      EXPECT_EQ(signerState.getEvalMapSize(), 2U);
      auto&& txinEval = signerState.getSignedStateForInput(0);
      EXPECT_EQ(txinEval.getSigCount(), 2U);

      auto asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(asset1);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));

      asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(asset2);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));
   }

   auto tx1 = signer3.serializeSignedTx();
   auto zcHash = signer3.getTxId();

   //broadcast the last one
   DBTestUtils::ZcVector zcVec;
   zcVec.push_back(tx1, 15000000);

   DBTestUtils::pushNewZc(theBDMt_, zcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   //grab ZC from DB and verify it again
   auto zc_from_db = DBTestUtils::getTxByHash(clients_, bdvID, zcHash);
   auto raw_tx = zc_from_db.serialize();
   auto bctx = BCTX::parse(raw_tx);
   Armory::Signing::TransactionVerifier tx_verifier(*bctx, unspentVec);

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
TEST_F(SignerTest, SpendTest_MultipleSigners_DifferentInputs)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };
   IO::CreationParams params {
      homedir_, {}, 1ms, {}, 1ms, 3
   };

   //// create 2 assetWlt ////
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed1(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_1 = AssetWallet_Single::createFromSeed(
      std::move(seed1), params);

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed2(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_2 = AssetWallet_Single::createFromSeed(
      std::move(seed2), params);

   //register with db
   std::vector<std::shared_ptr<AddressEntry>> addrVec_1;
   addrVec_1.push_back(assetWlt_1->getNewAddress());
   addrVec_1.push_back(assetWlt_1->getNewAddress());
   addrVec_1.push_back(assetWlt_1->getNewAddress());

   std::vector<BinaryData> hashVec_1;
   for (auto addrPtr : addrVec_1) {
      hashVec_1.push_back(addrPtr->getPrefixedHash());
   }

   std::vector<std::shared_ptr<AddressEntry>> addrVec_2;
   addrVec_2.push_back(assetWlt_2->getNewAddress());
   addrVec_2.push_back(assetWlt_2->getNewAddress());
   addrVec_2.push_back(assetWlt_2->getNewAddress());

   std::vector<BinaryData> hashVec_2;
   for (auto addrPtr : addrVec_2) {
      hashVec_2.push_back(addrPtr->getPrefixedHash());
   }

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, hashVec_1, assetWlt_1->getID(),
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, hashVec_2, assetWlt_2->getID(),
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wlt_1 = bdvPtr->getWalletOrLockbox(assetWlt_1->getID());
   auto wlt_2 = bdvPtr->getWalletOrLockbox(assetWlt_2->getID());

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   {
      ////spend 12 to wlt_1, 15 to wlt_2 from wlt
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 12 to p2pkh script hash
      signer.addRecipient(addrVec_1[0]->getRecipient(12 * COIN));

      //spend 15 to p2pkh script hash
      signer.addRecipient(addrVec_2[0]->getRecipient(15 * COIN));

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);

   //spend 18 back to wlt, split change among the 2

   //get utxo list for spend value
   auto unspentVec_1 = wlt_1->getSpendableTxOutListZC();
   auto unspentVec_2 = wlt_2->getSpendableTxOutListZC();

   BinaryData serializedSignerState;
   std::string stringState;
   auto assetFeed2 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_1);
   auto assetFeed3 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_2);

   {
      auto spendVal = 8 * COIN;
      Signer signer2;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //create feed from asset wallet 1

      //create wlt_1 spenders
      uint64_t total = 0;
      for (const auto& utxo : unspentVec_1) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo));
      }

      //spend 18 to addrB, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), 18 * COIN);
      signer2.addRecipient(recipient2);

      //change back to wlt_1
      if (total > spendVal) {
         //spend 4 to p2pkh script hash
         signer2.addRecipient(addrVec_1[1]->getRecipient(total - spendVal));
      }

      serializedSignerState = std::move(signer2.serializeState());
      stringState = signer2.toString(SignerStringFormat::TxSigCollect_Modern);
   }

   {
      //serialize signer 2, deser with signer3 and populate
      auto spendVal = 10 * COIN;
      Signer signer3;
      signer3.deserializeState(serializedSignerState);

      //add spender from wlt_2
      uint64_t total = 0;
      for (const auto& utxo : unspentVec_2) {
         total += utxo.getValue();
         signer3.addSpender(getSpenderPtr(utxo));
      }

      //set change
      if (total > spendVal) {
         //spend 4 to p2pkh script hash
         signer3.addRecipient(addrVec_2[1]->getRecipient(total - spendVal));
      }
      serializedSignerState = std::move(signer3.serializeState());
   }

   //sign, verify & return signed tx
   Signer signer4;
   signer4.deserializeState(serializedSignerState);
   signer4.setFeed(assetFeed2);

   {
      auto lock = assetWlt_1->lockDecryptedContainer();
      signer4.sign();
   }

   EXPECT_FALSE(signer4.verify());
   EXPECT_FALSE(signer4.isResolved());
   EXPECT_FALSE(signer4.isSigned());

   Signer signer5;
   signer5.deserializeState(signer4.serializeState());
   signer5.setFeed(assetFeed3);

   {
      auto lock = assetWlt_2->lockDecryptedContainer();
      signer5.sign();
   }

   ASSERT_TRUE(signer5.isSigned());
   EXPECT_TRUE(signer5.verify());
   auto tx1 = signer5.serializeSignedTx();

   //broadcast the last one
   DBTestUtils::ZcVector zcVec;
   zcVec.push_back(tx1, 15000000);

   DBTestUtils::pushNewZc(theBDMt_, zcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 4 * COIN);

   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_MultipleSigners_DifferentInputs_Strings)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };
   IO::CreationParams params {
      homedir_, {}, 1ms, {}, 1ms, 3
   };

   //// create 2 assetWlt ////
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed1(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_1 = AssetWallet_Single::createFromSeed(
      std::move(seed1), params);

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed2(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_2 = AssetWallet_Single::createFromSeed(
      std::move(seed2), params);

   //register with db
   std::vector<std::shared_ptr<AddressEntry>> addrVec_1;
   addrVec_1.push_back(assetWlt_1->getNewAddress());
   addrVec_1.push_back(assetWlt_1->getNewAddress());
   addrVec_1.push_back(assetWlt_1->getNewAddress());

   std::vector<BinaryData> hashVec_1;
   for (auto addrPtr : addrVec_1) {
      hashVec_1.push_back(addrPtr->getPrefixedHash());
   }

   std::vector<std::shared_ptr<AddressEntry>> addrVec_2;
   addrVec_2.push_back(assetWlt_2->getNewAddress());
   addrVec_2.push_back(assetWlt_2->getNewAddress());
   addrVec_2.push_back(assetWlt_2->getNewAddress());

   std::vector<BinaryData> hashVec_2;
   for (auto addrPtr : addrVec_2) {
      hashVec_2.push_back(addrPtr->getPrefixedHash());
   }

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, hashVec_1, assetWlt_1->getID(),
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, hashVec_2, assetWlt_2->getID(),
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wlt_1 = bdvPtr->getWalletOrLockbox(assetWlt_1->getID());
   auto wlt_2 = bdvPtr->getWalletOrLockbox(assetWlt_2->getID());

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   {
      ////spend 12 to wlt_1, 15 to wlt_2 from wlt
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 12 to p2pkh script hash
      signer.addRecipient(addrVec_1[0]->getRecipient(12 * COIN));

      //spend 15 to p2pkh script hash
      signer.addRecipient(addrVec_2[0]->getRecipient(15 * COIN));

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);

   //spend 18 back to wlt, split change among the 2

   //get utxo list for spend value
   auto unspentVec_1 = wlt_1->getSpendableTxOutListZC();
   auto unspentVec_2 = wlt_2->getSpendableTxOutListZC();

   std::string stringState;
   auto assetFeed2 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_1);
   auto assetFeed3 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_2);

   {
      auto spendVal = 8 * COIN;
      Signer signer2;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //create feed from asset wallet 1

      //create wlt_1 spenders
      uint64_t total = 0;
      for (const auto& utxo : unspentVec_1) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo));
      }

      //spend 18 to addrB, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), 18 * COIN);
      signer2.addRecipient(recipient2);

      //change back to wlt_1
      if (total > spendVal) {
         //spend 4 to p2pkh script hash
         signer2.addRecipient(addrVec_1[1]->getRecipient(total - spendVal));
      }

      stringState = signer2.toString(SignerStringFormat::TxSigCollect_Modern);
   }

   {
      //serialize signer 2, deser with signer3 and populate
      auto spendVal = 10 * COIN;
      auto signer3 = Signer::fromString(stringState);

      //add spender from wlt_2
      uint64_t total = 0;
      for (const auto& utxo : unspentVec_2) {
         total += utxo.getValue();
         signer3.addSpender(getSpenderPtr(utxo));
      }

      //set change
      if (total > spendVal) {
         //spend 4 to p2pkh script hash
         signer3.addRecipient(addrVec_2[1]->getRecipient(total - spendVal));
      }

      stringState = std::move(signer3.toString(
         SignerStringFormat::TxSigCollect_Modern));
   }


   //sign, verify & return signed tx
   auto signer4 = Signer::fromString(stringState);
   signer4.setFeed(assetFeed2);

   {
      auto lock = assetWlt_1->lockDecryptedContainer();
      signer4.sign();
   }

   EXPECT_FALSE(signer4.verify());
   EXPECT_FALSE(signer4.isResolved());
   EXPECT_FALSE(signer4.isSigned());

   auto signer5 = Signer::fromString(signer4.toString(
      SignerStringFormat::TxSigCollect_Modern));
   signer5.setFeed(assetFeed3);

   {
      auto lock = assetWlt_2->lockDecryptedContainer();
      signer5.sign();
   }

   ASSERT_TRUE(signer5.isSigned());
   EXPECT_TRUE(signer5.verify());
   auto tx1 = signer5.serializeSignedTx();

   //broadcast the last one
   DBTestUtils::ZcVector zcVec;
   zcVec.push_back(tx1, 15000000);

   DBTestUtils::pushNewZc(theBDMt_, zcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 4 * COIN);

   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_MultipleSigners_DifferentInputs_StringsLegacy)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };
   IO::CreationParams params {
      homedir_, {}, 1ms, {}, 1ms, 3
   };

   //// create 2 assetWlt ////
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed1(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_1 = AssetWallet_Single::createFromSeed(
      std::move(seed1), params);

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed2(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_2 = AssetWallet_Single::createFromSeed(
      std::move(seed2), params);

   //register with db
   std::vector<std::shared_ptr<AddressEntry>> addrVec_1;
   addrVec_1.push_back(assetWlt_1->getNewAddress());
   addrVec_1.push_back(assetWlt_1->getNewAddress());
   addrVec_1.push_back(assetWlt_1->getNewAddress());

   std::vector<BinaryData> hashVec_1;
   for (auto addrPtr : addrVec_1) {
      hashVec_1.push_back(addrPtr->getPrefixedHash());
   }

   std::vector<std::shared_ptr<AddressEntry>> addrVec_2;
   addrVec_2.push_back(assetWlt_2->getNewAddress());
   addrVec_2.push_back(assetWlt_2->getNewAddress());
   addrVec_2.push_back(assetWlt_2->getNewAddress());

   std::vector<BinaryData> hashVec_2;
   for (auto addrPtr : addrVec_2) {
      hashVec_2.push_back(addrPtr->getPrefixedHash());
   }

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, hashVec_1, assetWlt_1->getID(),
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, hashVec_2, assetWlt_2->getID(),
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wlt_1 = bdvPtr->getWalletOrLockbox(assetWlt_1->getID());
   auto wlt_2 = bdvPtr->getWalletOrLockbox(assetWlt_2->getID());

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   {
      ////spend 12 to wlt_1, 15 to wlt_2 from wlt
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 12 to p2pkh script hash
      signer.addRecipient(addrVec_1[0]->getRecipient(12 * COIN));

      //spend 15 to p2pkh script hash
      signer.addRecipient(addrVec_2[0]->getRecipient(15 * COIN));

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);

   //spend 18 back to wlt, split change among the 2

   //get utxo list for spend value
   auto unspentVec_1 = wlt_1->getSpendableTxOutListZC();
   auto unspentVec_2 = wlt_2->getSpendableTxOutListZC();

   std::string stringState;
   auto assetFeed2 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_1);
   auto assetFeed3 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_2);

   {
      auto spendVal = 8 * COIN;
      Signer signer2;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //create feed from asset wallet 1

      //create wlt_1 spenders
      uint64_t total = 0;
      std::set<BinaryData> spenderHashes;
      for (const auto& utxo : unspentVec_1) {
         total += utxo.getValue();
         spenderHashes.emplace(utxo.getTxHash());
         signer2.addSpender(getSpenderPtr(utxo));
      }

      //spend 18 to addrB, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), 18 * COIN);
      signer2.addRecipient(recipient2);

      //change back to wlt_1
      if (total > spendVal) {
         //spend 4 to p2pkh script hash
         signer2.addRecipient(addrVec_1[1]->getRecipient(total - spendVal));
      }

      //to serialize a signer to the legacy format, we need the supporting tx
      for (const auto& hash : spenderHashes) {
         auto supportingTx = DBTestUtils::getTxByHash(clients_, bdvID, hash);
         signer2.addSupportingTx(supportingTx.serialize());
      }
      ASSERT_TRUE(signer2.canLegacySerialize());
      stringState = signer2.toString(SignerStringFormat::TxSigCollect_Legacy);
   }

   {
      //serialize signer 2, deser with signer3 and populate
      auto spendVal = 10 * COIN;
      auto signer3 = Signer::fromString(stringState);

      //add spender from wlt_2
      uint64_t total = 0;
      std::set<BinaryData> spenderHashes;
      for (const auto& utxo : unspentVec_2) {
         total += utxo.getValue();
         spenderHashes.emplace(utxo.getTxHash());
         signer3.addSpender(getSpenderPtr(utxo));
      }

      //set change
      if (total > spendVal) {
         //spend 4 to p2pkh script hash
         signer3.addRecipient(addrVec_2[1]->getRecipient(total - spendVal));
      }

      //to serialize a signer to the legacy format, we need the supporting tx
      for (const auto& hash : spenderHashes) {
         auto supportingTx = DBTestUtils::getTxByHash(clients_, bdvID, hash);
         signer3.addSupportingTx(supportingTx.serialize());
      }

      ASSERT_TRUE(signer3.canLegacySerialize());
      stringState = move(signer3.toString(
         SignerStringFormat::TxSigCollect_Legacy));
   }


   //sign, verify & return signed tx
   auto signer4 = Signer::fromString(stringState);
   signer4.setFeed(assetFeed2);

   {
      auto lock = assetWlt_1->lockDecryptedContainer();
      signer4.sign();
   }

   EXPECT_FALSE(signer4.verify());
   EXPECT_FALSE(signer4.isResolved());
   EXPECT_FALSE(signer4.isSigned());

   auto signer5 = Signer::fromString(signer4.toString(
      SignerStringFormat::TxSigCollect_Legacy));
   signer5.setFeed(assetFeed3);

   {
      auto lock = assetWlt_2->lockDecryptedContainer();
      signer5.sign();
   }

   ASSERT_TRUE(signer5.isSigned());
   EXPECT_TRUE(signer5.verify());
   auto tx1 = signer5.serializeSignedTx();

   //broadcast the last one
   DBTestUtils::ZcVector zcVec;
   zcVec.push_back(tx1, 15000000);

   DBTestUtils::pushNewZc(theBDMt_, zcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 4 * COIN);

   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_MultipleSigners_ParallelSigning)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };
   IO::CreationParams params {
      homedir_, {}, 1ms, {}, 1ms, 3
   };

   //// create 2 assetWlt ////
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed1(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_1 = AssetWallet_Single::createFromSeed(
      std::move(seed1), params);

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed2(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_2 = AssetWallet_Single::createFromSeed(
      std::move(seed2), params);

   //register with db
   std::vector<std::shared_ptr<AddressEntry>> addrVec_1;
   addrVec_1.push_back(assetWlt_1->getNewAddress());
   addrVec_1.push_back(assetWlt_1->getNewAddress());
   addrVec_1.push_back(assetWlt_1->getNewAddress());

   std::vector<BinaryData> hashVec_1;
   for (auto addrPtr : addrVec_1) {
      hashVec_1.push_back(addrPtr->getPrefixedHash());
   }

   std::vector<std::shared_ptr<AddressEntry>> addrVec_2;
   addrVec_2.push_back(assetWlt_2->getNewAddress());
   addrVec_2.push_back(assetWlt_2->getNewAddress());
   addrVec_2.push_back(assetWlt_2->getNewAddress());

   std::vector<BinaryData> hashVec_2;
   for (auto addrPtr : addrVec_2) {
      hashVec_2.push_back(addrPtr->getPrefixedHash());
   }

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, hashVec_1, assetWlt_1->getID(),
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, hashVec_2, assetWlt_2->getID(),
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wlt_1 = bdvPtr->getWalletOrLockbox(assetWlt_1->getID());
   auto wlt_2 = bdvPtr->getWalletOrLockbox(assetWlt_2->getID());

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   {
      ////spend 12 to wlt_1, 15 to wlt_2 from wlt
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 12 to p2pkh script hash
      signer.addRecipient(addrVec_1[0]->getRecipient(12 * COIN));

      //spend 15 to p2pkh script hash
      signer.addRecipient(addrVec_2[0]->getRecipient(15 * COIN));

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);

   //spend 18 back to wlt, split change among the 2

   //get utxo list for spend value
   auto unspentVec_1 = wlt_1->getSpendableTxOutListZC();
   auto unspentVec_2 = wlt_2->getSpendableTxOutListZC();
   BinaryData serializedSignerState;

   {
      //create first signer, set outpoint from wlt_1 and change to wlt_1
      auto spendVal = 8 * COIN;
      Signer signer2;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //create feed from asset wallet 1

      //create wlt_1 spenders
      uint64_t total = 0;
      for (auto& utxo : unspentVec_1) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo));
      }

      //spend 18 to addrB, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), 18 * COIN);
      signer2.addRecipient(recipient2);

      //change back to wlt_1
      if (total > spendVal) {
         //spend 4 to p2pkh script hash
         signer2.addRecipient(addrVec_1[1]->getRecipient(total - spendVal));
      }

      serializedSignerState = std::move(signer2.serializeState());
   }

   {
      //serialize signer 2, deser with signer3 and populate with outpoint and 
      //change from wlt_2
      auto spendVal = 10 * COIN;
      Signer signer3;
      signer3.deserializeState(serializedSignerState);

      //add spender from wlt_2
      uint64_t total = 0;
      for (auto& utxo : unspentVec_2) {
         total += utxo.getValue();
         signer3.addSpender(getSpenderPtr(utxo));
      }

      //set change
      if (total > spendVal) {
         //spend 4 to p2pkh script hash
         signer3.addRecipient(addrVec_2[1]->getRecipient(total - spendVal));
      }

      serializedSignerState = std::move(signer3.serializeState());
   }

   auto assetFeed2 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_1);
   auto assetFeed3 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_2);

   //deser to new signer, this time populate with feed and utxo from wlt_1
   Signer signer4;
   signer4.setFeed(assetFeed2);
   for (auto& utxo : unspentVec_1) {
      signer4.addSpender(getSpenderPtr(utxo));
   }

   signer4.deserializeState(serializedSignerState);
   {
      auto lock = assetWlt_1->lockDecryptedContainer();
      signer4.sign();
   }

   EXPECT_FALSE(signer4.verify());
   EXPECT_FALSE(signer4.isResolved());
   EXPECT_FALSE(signer4.isSigned());

   //deser from same state into wlt_2 signer
   Signer signer5;

   //in this case, we can't set the utxos first then deser the state, as it would break
   //utxo ordering. we have to deser first, then populate utxos
   signer5.deserializeState(serializedSignerState);

   for (auto& utxo : unspentVec_2) {
      UTXO entry(utxo.value_, utxo.txHeight_, utxo.txIndex_, utxo.txOutIndex_,
         std::move(utxo.txHash_), std::move(utxo.script_));
      signer5.populateUtxo(entry);
   }

   //finally set the feed
   signer5.setFeed(assetFeed3);

   {
      auto lock = assetWlt_2->lockDecryptedContainer();
      signer5.sign();
   }

   EXPECT_FALSE(signer5.verify());

   //now serialize both signers into the final signer, verify and broadcast
   Signer signer6;
   signer6.deserializeState(signer4.serializeState());
   signer6.deserializeState(signer5.serializeState());

   ASSERT_TRUE(signer6.isSigned());
   EXPECT_TRUE(signer6.verify());

   //try again in the opposite order, that should not matter
   Signer signer7;
   signer7.deserializeState(signer5.serializeState());
   signer7.deserializeState(signer4.serializeState());

   ASSERT_TRUE(signer7.isSigned());
   EXPECT_TRUE(signer7.verify());

   auto tx1 = signer7.serializeSignedTx();

   //broadcast the last one
   DBTestUtils::ZcVector zcVec;
   zcVec.push_back(tx1, 15000000);

   DBTestUtils::pushNewZc(theBDMt_, zcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 4 * COIN);

   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_MultipleSigners_ParallelSigning_GetUnsignedTx)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };
   IO::CreationParams params {
      homedir_, {}, 1ms, {}, 1ms, 3
   };

   //// create 2 assetWlt ////
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed1(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt_1 = AssetWallet_Single::createFromSeed(
      std::move(seed1), params);

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed2(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt_2 = AssetWallet_Single::createFromSeed(
      std::move(seed2), params);

   //register with db
   std::vector<std::shared_ptr<AddressEntry>> addrVec_1;
   addrVec_1.push_back(assetWlt_1->getNewAddress(AddressEntryType_P2WPKH));
   addrVec_1.push_back(assetWlt_1->getNewAddress(AddressEntryType_P2WPKH));
   addrVec_1.push_back(assetWlt_1->getNewAddress(AddressEntryType_P2WPKH));

   std::vector<BinaryData> hashVec_1;
   for (auto addrPtr : addrVec_1) {
      hashVec_1.push_back(addrPtr->getPrefixedHash());
   }

   std::vector<std::shared_ptr<AddressEntry>> addrVec_2;
   addrVec_2.push_back(assetWlt_2->getNewAddress(AddressEntryType_P2WPKH));
   addrVec_2.push_back(assetWlt_2->getNewAddress(AddressEntryType_P2WPKH));
   addrVec_2.push_back(assetWlt_2->getNewAddress(AddressEntryType_P2WPKH));

   std::vector<BinaryData> hashVec_2;
   for (auto addrPtr : addrVec_2) {
      hashVec_2.push_back(addrPtr->getPrefixedHash());
   }

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, hashVec_1, assetWlt_1->getID(),
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, hashVec_2, assetWlt_2->getID(),
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wlt_1 = bdvPtr->getWalletOrLockbox(assetWlt_1->getID());
   auto wlt_2 = bdvPtr->getWalletOrLockbox(assetWlt_2->getID());

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   {
      ////spend 12 to wlt_1, 15 to wlt_2 from wlt
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 12 to p2pkh script hash
      signer.addRecipient(addrVec_1[0]->getRecipient(12 * COIN));

      //spend 15 to p2pkh script hash
      signer.addRecipient(addrVec_2[0]->getRecipient(15 * COIN));

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);

   //spend 18 back to wlt, split change among the 2

   //get utxo list for spend value
   auto unspentVec_1 = wlt_1->getSpendableTxOutListZC();
   auto unspentVec_2 = wlt_2->getSpendableTxOutListZC();

   BinaryData serializedSignerState;
   {
      //create first signer, set outpoint from wlt_1 and change to wlt_1
      auto spendVal = 8 * COIN;
      Signer signer2;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //create feed from asset wallet 1

      //create wlt_1 spenders
      auto _assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_1);
      uint64_t total = 0;
      for (auto& utxo : unspentVec_1) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo));
      }

      //spend 18 to addrB, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), 18 * COIN);
      signer2.addRecipient(recipient2);

      //change back to wlt_1
      if (total > spendVal) {
         //spend 4 to p2pkh script hash
         signer2.addRecipient(addrVec_1[1]->getRecipient(total - spendVal));
      }

      signer2.setFeed(_assetFeed);
      signer2.resolvePublicData();

      {
         auto assetID = assetWlt_1->getAssetIDForScrAddr(
            (*addrVec_1.begin())->getPrefixedHash());
         auto accountPtr = assetWlt_1->getAccountForID(
            assetID.first.getAddressAccountId());

         EXPECT_NE(signer2.getTxInCount(), 0ULL) ;
         for (unsigned i=0; i<signer2.getTxInCount(); i++) {
            auto spender = signer2.getSpender(i);
            auto bip32Paths = spender->getBip32Paths();
            EXPECT_FALSE(bip32Paths.empty());

            for (const auto& pathData : bip32Paths) {
               EXPECT_TRUE(accountPtr->hasBip32Path(pathData.second));
            }
         }
      }

      //spender resolved state should be seralized along
      serializedSignerState = std::move(signer2.serializeState());
   }

   BinaryData unsignedTxRaw, unsignedHash;
   {
      //serialize signer 2, deser with signer3 and populate with outpoint and
      //change from wlt_2
      auto spendVal = 10 * COIN;
      Signer signer3;
      signer3.deserializeState(serializedSignerState);

      //add spender from wlt_2
      uint64_t total = 0;
      for (auto& utxo : unspentVec_2) {
         total += utxo.getValue();
         auto spender = getSpenderPtr(utxo);
         spender->setSequence(UINT32_MAX - 2);
         signer3.addSpender(spender);
      }

      //set change
      if (total > spendVal) {
         //spend 4 to p2pkh script hash
         signer3.addRecipient(addrVec_2[1]->getRecipient(total - spendVal));
      }

      //get txid & unsigned tx, should be valid
      auto _assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_2);
      signer3.setFeed(_assetFeed);
      unsignedHash = signer3.getTxId();
      unsignedTxRaw = signer3.serializeUnsignedTx();

      //spender resolved state should be seralized along
      serializedSignerState = std::move(signer3.serializeState());

      EXPECT_TRUE(signer3.isResolved());
   }

   auto assetFeed2 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_1);
   auto assetFeed3 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_2);

   //deser to new signer, this time populate with feed and utxo from wlt_1
   Signer signer4;
   signer4.setFeed(assetFeed2);
   for (auto& utxo : unspentVec_1) {
      signer4.addSpender(getSpenderPtr(utxo));
   }

   signer4.deserializeState(serializedSignerState);
   {
      auto lock = assetWlt_1->lockDecryptedContainer();
      signer4.sign();
   }

   EXPECT_FALSE(signer4.verify());
   EXPECT_TRUE(signer4.isResolved());

   //deser from same state into wlt_2 signer
   Signer signer5;

   //in this case, we can't set the utxos first then deser the state, as it would break
   //utxo ordering. we have to deser first, then populate utxos
   signer5.deserializeState(serializedSignerState);

   for (auto& utxo : unspentVec_2) {
      UTXO entry(utxo.value_, utxo.txHeight_, utxo.txIndex_, utxo.txOutIndex_,
         std::move(utxo.txHash_), std::move(utxo.script_));
      signer5.populateUtxo(entry);
   }

   //finally set the feed
   signer5.setFeed(assetFeed3);
   {
      auto lock = assetWlt_2->lockDecryptedContainer();
      signer5.sign();
   }

   EXPECT_FALSE(signer5.verify());

   //now serialize both signers into the final signer, verify and broadcast
   Signer signer6;
   signer6.deserializeState(signer4.serializeState());
   signer6.deserializeState(signer5.serializeState());

   ASSERT_TRUE(signer6.isSigned());
   EXPECT_TRUE(signer6.verify());

   //try again in the opposite order, that should not matter
   Signer signer7;
   signer7.deserializeState(signer5.serializeState());
   signer7.deserializeState(signer4.serializeState());

   ASSERT_TRUE(signer7.isSigned());
   EXPECT_TRUE(signer7.verify());

   auto tx1 = signer7.serializeSignedTx();

   //broadcast the last one
   DBTestUtils::ZcVector zcVec;
   zcVec.push_back(tx1, 15000000);

   DBTestUtils::pushNewZc(theBDMt_, zcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 4 * COIN);

   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);

   //compare hashes with unsigned counterparts
   Tx unsignedTx(unsignedTxRaw);
   EXPECT_EQ(unsignedTx.getThisHash(), unsignedHash);
   EXPECT_EQ(unsignedTx.getThisHash(), signer7.getTxId());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_MultipleSigners_ParallelSigning_GetUnsignedTx_Nested)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };
   IO::CreationParams params{
      homedir_, {}, 1ms, {}, 1ms, 3
   };

   //// create 2 assetWlt ////
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed1(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt_1 = AssetWallet_Single::createFromSeed(
      std::move(seed1), params);

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed2(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_2 = AssetWallet_Single::createFromSeed(
      std::move(seed2), params);

   //register with db
   auto addr_type_nested_p2sh = AddressEntryType(AddressEntryType_P2WPKH | AddressEntryType_P2SH);
   std::vector<std::shared_ptr<AddressEntry>> addrVec_1;
   addrVec_1.push_back(assetWlt_1->getNewAddress(AddressEntryType_P2WPKH));
   addrVec_1.push_back(assetWlt_1->getNewAddress(AddressEntryType_P2WPKH));
   addrVec_1.push_back(assetWlt_1->getNewAddress(AddressEntryType_P2WPKH));

   std::vector<BinaryData> hashVec_1;
   for (auto addrPtr : addrVec_1) {
      hashVec_1.push_back(addrPtr->getPrefixedHash());
   }

   std::vector<std::shared_ptr<AddressEntry>> addrVec_2;
   addrVec_2.push_back(assetWlt_2->getNewAddress(addr_type_nested_p2sh));
   addrVec_2.push_back(assetWlt_2->getNewAddress(addr_type_nested_p2sh));
   addrVec_2.push_back(assetWlt_2->getNewAddress(addr_type_nested_p2sh));

   std::vector<BinaryData> hashVec_2;
   for (auto addrPtr : addrVec_2) {
      hashVec_2.push_back(addrPtr->getPrefixedHash());
   }

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, hashVec_1, assetWlt_1->getID(),
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, hashVec_2, assetWlt_2->getID(),
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wlt_1 = bdvPtr->getWalletOrLockbox(assetWlt_1->getID());
   auto wlt_2 = bdvPtr->getWalletOrLockbox(assetWlt_2->getID());

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   {
      ////spend 12 to wlt_1, 15 to wlt_2 from wlt
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 12 to p2pkh script hash
      signer.addRecipient(addrVec_1[0]->getRecipient(12 * COIN));

      //spend 15 to p2pkh script hash
      signer.addRecipient(addrVec_2[0]->getRecipient(15 * COIN));

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);

   //spend 18 back to wlt, split change among the 2

   //get utxo list for spend value
   auto unspentVec_1 = wlt_1->getSpendableTxOutListZC();
   auto unspentVec_2 = wlt_2->getSpendableTxOutListZC();

   BinaryData serializedSignerState;
   {
      //create first signer, set outpoint from wlt_1 and change to wlt_1
      auto spendVal = 8 * COIN;
      Signer signer2;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //create feed from asset wallet 1

      //create wlt_1 spenders
      auto _assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_1);
      uint64_t total = 0;
      for (auto& utxo : unspentVec_1) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo));
      }

      //spend 18 to addrB, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), 18 * COIN);
      signer2.addRecipient(recipient2);

      //change back to wlt_1
      if (total > spendVal) {
         //spend 4 to p2pkh script hash
         signer2.addRecipient(addrVec_1[1]->getRecipient(total - spendVal));
      }

      {
         EXPECT_NE(signer2.getTxInCount(), 0U);
         for (unsigned i=0; i<signer2.getTxInCount(); i++) {
            auto spender = signer2.getSpender(i);
            auto bip32Paths = spender->getBip32Paths();
            EXPECT_TRUE(bip32Paths.empty());
         }
      }

      signer2.setFeed(_assetFeed);
      signer2.resolvePublicData();
      EXPECT_TRUE(signer2.isResolved());

      {
         auto assetID = assetWlt_1->getAssetIDForScrAddr(
            (*addrVec_1.begin())->getPrefixedHash());
         auto accountPtr = assetWlt_1->getAccountForID(
            assetID.first.getAddressAccountId());

         EXPECT_NE(signer2.getTxInCount(), 0U);
         for (unsigned i=0; i<signer2.getTxInCount(); i++) {
            auto spender = signer2.getSpender(i);
            auto bip32Paths = spender->getBip32Paths();
            EXPECT_FALSE(bip32Paths.empty());

            for (const auto& pathData : bip32Paths) {
               EXPECT_TRUE(accountPtr->hasBip32Path(pathData.second));
            }
         }
      }
      //spender resolved state should be seralized along
      serializedSignerState = std::move(signer2.serializeState());
   }

   BinaryData unsignedTxRaw, unsignedHash;
   {
      //deser signer2 state in signer3 and populate with outpoint and
      //change from wlt_2
      auto spendVal = 10 * COIN;
      Signer signer3;
      signer3.setFlags(SCRIPT_VERIFY_SEGWIT);
      signer3.deserializeState(serializedSignerState);

      //add spender from wlt_2
      uint64_t total = 0;
      for (auto& utxo : unspentVec_2) {
         total += utxo.getValue();
         signer3.addSpender(getSpenderPtr(utxo));
      }

      //set change
      if (total > spendVal) {
         //spend 4 to p2pkh script hash
         signer3.addRecipient(addrVec_2[1]->getRecipient(total - spendVal));
      }

      //get txid & unsigned tx, should be valid now
      auto _assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_2);
      signer3.setFeed(_assetFeed);
      EXPECT_FALSE(signer3.isResolved());
      unsignedHash = signer3.getTxId();
      unsignedTxRaw = signer3.serializeUnsignedTx();
      EXPECT_TRUE(signer3.isResolved());

      //spender resolved state should be seralized along
      serializedSignerState = std::move(signer3.serializeState());
   }

   auto assetFeed2 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_1);
   auto assetFeed3 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_2);

   //deser to new signer, this time populate with feed and utxo from wlt_1
   Signer signer4;
   signer4.setFlags(SCRIPT_VERIFY_SEGWIT);
   signer4.setFeed(assetFeed2);

   for (auto& utxo : unspentVec_1) {
      signer4.addSpender(getSpenderPtr(utxo));
   }

   signer4.deserializeState(serializedSignerState);
   EXPECT_TRUE(signer4.isResolved());

   {
      signer4.setFeed(assetFeed2);
      auto lock = assetWlt_1->lockDecryptedContainer();
      signer4.sign();
   }

   EXPECT_FALSE(signer4.verify());
   EXPECT_TRUE(signer4.isResolved());
   EXPECT_FALSE(signer4.isSigned());

   auto checkInputsAreSigned = [](
      const Signer& signer,
      const std::vector<UTXO>& utxoVec)->bool
   {
      EXPECT_EQ(signer.getTxInCount(), 2U);
      std::set<unsigned> spenderIndexes;

      for (unsigned i=0; i<utxoVec.size(); i++) {
         const auto& utxo = utxoVec[i];
         for (unsigned y=0; y<signer.getTxInCount(); y++) {
            auto spender = signer.getSpender(y);
            if (spender->getOutputHash() == utxo.txHash_ &&
               spender->getOutputIndex() == utxo.txOutIndex_) {
               spenderIndexes.emplace(y);
               break;
            }
         }
      }

      if (spenderIndexes.size() != utxoVec.size()) {
         return false;
      }

      auto evalSignState = signer.evaluateSignedState();
      for (const auto& spenderId : spenderIndexes) {
         const auto& inputEvalState =
            evalSignState.getSignedStateForInput(spenderId);
         if (inputEvalState.getM() != 1 || !inputEvalState.isValid()) {
            return false;
         }
      }

      return true;
   };
   ASSERT_TRUE(checkInputsAreSigned(signer4, unspentVec_1));

   //deser from same state into wlt_2 signer
   Signer signer5;

   //in this case, we can't set the utxos first then deser the state, as it would break
   //utxo ordering. we have to deser first, then populate utxos
   signer5.deserializeState(serializedSignerState);

   auto unspentVec_2Copy = unspentVec_2;
   for (auto& utxo : unspentVec_2) {
      UTXO entry(utxo.value_, utxo.txHeight_, utxo.txIndex_, utxo.txOutIndex_,
         std::move(utxo.txHash_), std::move(utxo.script_));
      signer5.populateUtxo(entry);
   }

   //finally set the feed
   signer5.setFeed(assetFeed3);
   {
      auto lock = assetWlt_2->lockDecryptedContainer();
      signer5.sign();
   }

   EXPECT_FALSE(signer5.verify());
   EXPECT_TRUE(signer5.isResolved());
   ASSERT_TRUE(checkInputsAreSigned(signer5, unspentVec_2Copy));

   //now serialize both signers into the final signer, verify and broadcast
   Signer signer6;
   signer6.deserializeState(signer4.serializeState());
   signer6.deserializeState(signer5.serializeState());

   EXPECT_TRUE(signer6.isSigned());
   EXPECT_TRUE(signer6.verify());

   //try again in the opposite order, that should not matter
   Signer signer7;
   signer7.deserializeState(signer5.serializeState());
   signer7.deserializeState(signer4.serializeState());

   EXPECT_TRUE(signer7.isSigned());
   EXPECT_TRUE(signer7.verify());
   auto tx1 = signer7.serializeSignedTx();

   //broadcast the last one
   DBTestUtils::ZcVector zcVec;
   zcVec.push_back(tx1, 15000000);

   DBTestUtils::pushNewZc(theBDMt_, zcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 4 * COIN);

   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 5 * COIN);

   //compare hashes with unsigned counterparts
   Tx unsignedTx(unsignedTxRaw);
   EXPECT_EQ(unsignedTx.getThisHash(), unsignedHash);
   EXPECT_EQ(unsignedTx.getThisHash(), signer7.getTxId());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, GetUnsignedTxId)
{
   auto locktime = 1946132849;
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };
   IO::CreationParams params{
      homedir_, {}, 1ms, {}, 1ms, 3
   };

   //// create 2 assetWlt ////
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed1(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt_1 = AssetWallet_Single::createFromSeed(
      std::move(seed1), params);

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed2(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_2 = AssetWallet_Single::createFromSeed(
      std::move(seed2), params);

   //register with db
   std::vector<std::shared_ptr<AddressEntry>> addrVec_1;
   addrVec_1.push_back(assetWlt_1->getNewAddress());
   addrVec_1.push_back(assetWlt_1->getNewAddress());
   addrVec_1.push_back(assetWlt_1->getNewAddress());

   std::vector<BinaryData> hashVec_1;
   for (auto addrPtr : addrVec_1) {
      hashVec_1.push_back(addrPtr->getPrefixedHash());
   }

   std::vector<std::shared_ptr<AddressEntry>> addrVec_2;
   auto addr_type_nested_p2sh = AddressEntryType(AddressEntryType_P2WPKH | AddressEntryType_P2SH);
   addrVec_2.push_back(assetWlt_2->getNewAddress(addr_type_nested_p2sh));
   addrVec_2.push_back(assetWlt_2->getNewAddress(addr_type_nested_p2sh));
   addrVec_2.push_back(assetWlt_2->getNewAddress(addr_type_nested_p2sh));

   std::vector<BinaryData> hashVec_2;
   for (auto addrPtr : addrVec_2) {
      hashVec_2.push_back(addrPtr->getPrefixedHash());
   }

   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, hashVec_1, assetWlt_1->getID(),
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, hashVec_2, assetWlt_2->getID(),
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto wlt_1 = bdvPtr->getWalletOrLockbox(assetWlt_1->getID());
   auto wlt_2 = bdvPtr->getWalletOrLockbox(assetWlt_2->getID());

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   BinaryData supportingTx;
   {
      ////spend 12 to wlt_1, 15 to wlt_2 from wlt
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 12 to p2pkh script hash
      signer.addRecipient(addrVec_1[0]->getRecipient(12 * COIN));

      //spend 15 to p2pkh script hash
      signer.addRecipient(addrVec_2[0]->getRecipient(15 * COIN));

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      try {
         //shouldn't be able to get txid on legacy unsigned tx
         signer.setFeed(feed);
         signer.getTxId();
         EXPECT_TRUE(false);
      }
      catch (const std::exception&) {}

      try {
         //set a lock time, check it's encoded correctly
         signer.setLockTime(locktime);
         auto unsignedTx = signer.serializeUnsignedTx();

         Tx tx(unsignedTx);
         EXPECT_EQ(tx.getLockTime(), locktime);
      }
      catch (const std::exception&) {
         EXPECT_TRUE(false);
      }

      //sign, verify then broadcast
      signer.sign();
      EXPECT_TRUE(signer.verify());
      supportingTx = signer.serializeSignedTx();

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(supportingTx, 14000000);

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
   scrObj = wlt_1->getScrAddrObjByKey(hashVec_1[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = wlt_2->getScrAddrObjByKey(hashVec_2[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);

   auto unspentVec_1 = wlt_1->getSpendableTxOutListZC();
   auto unspentVec_2 = wlt_2->getSpendableTxOutListZC();

   BinaryData serializedSignerState;
   {
      //create first signer, set outpoint from wlt_1 and change to wlt_1
      auto spendVal = 18 * COIN;
      Signer signer2;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //create feed from asset wallet 1

      //create wlt_1 spenders
      uint64_t total = 0;
      for (auto& utxo : unspentVec_1) {
         total += utxo.getValue();
         signer2.addSpender(
            std::make_shared<ScriptSpender>(utxo.getTxHash(), utxo.getTxOutIndex()));
      }

      //spend 18 to addrB, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), 18 * COIN);
      signer2.addRecipient(recipient2);

      //change back to wlt_1
      if (total > spendVal) {
         //spend 4 to p2pkh script hash
         signer2.addRecipient(addrVec_1[1]->getRecipient(total - spendVal));
      }

      //locktime deser test
      {
         signer2.setLockTime(++locktime);
         auto unsignedTx = signer2.serializeUnsignedTx(true);

         Tx tx(unsignedTx);
         EXPECT_EQ(tx.getLockTime(), locktime);
      }
      serializedSignerState = std::move(signer2.serializeState());
   }

   {
      //serialize signer 2, deser with signer3 and populate with outpoint and 
      //change from wlt_2
      auto spendVal = 10 * COIN;
      Signer signer3;
      signer3.deserializeState(serializedSignerState);

      //add spender from wlt_2
      uint64_t total = 0;
      for (auto& utxo : unspentVec_2) {
         total += utxo.getValue();
         signer3.addSpender(
            std::make_shared<ScriptSpender>(utxo.getTxHash(), utxo.getTxOutIndex()));
      }

      //set change
      if (total > spendVal) {
         //spend 4 to p2pkh script hash
         signer3.addRecipient(addrVec_2[1]->getRecipient(total - spendVal));
      }

      //locktime deser test
      {
         auto unsignedTx = signer3.serializeUnsignedTx(true);

         Tx tx(unsignedTx);
         EXPECT_EQ(tx.getLockTime(), locktime);
      }
      serializedSignerState = std::move(signer3.serializeState());
   }

   auto assetFeed2 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_1);
   auto assetFeed3 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_2);

   //deser to new signer, this time populate with feed and utxo from wlt_1
   Signer signer4;
   signer4.deserializeState(serializedSignerState);
   signer4.addSupportingTx(supportingTx);
   signer4.setFeed(assetFeed2);

   {
      auto unsignedTx = signer4.serializeUnsignedTx(true);

      Tx tx(unsignedTx);
      EXPECT_EQ(tx.getLockTime(), locktime);

      auto lock = assetWlt_1->lockDecryptedContainer();
      signer4.sign();
   }

   {
      auto mainAccountID = assetWlt_1->getMainAccountID();
      auto mainAccount = assetWlt_1->getAccountForID(mainAccountID);

      EXPECT_NE(signer4.getTxInCount(), 0U);
      for (unsigned i=0; i<signer4.getTxInCount(); i++) {
         auto spender = signer4.getSpender(i);
         auto bip32Paths = spender->getBip32Paths();
         if (i < unspentVec_1.size()) {
            EXPECT_FALSE(bip32Paths.empty());
            for (const auto& pathData : bip32Paths) {
               EXPECT_TRUE(mainAccount->hasBip32Path(pathData.second));
            }
         } else {
            EXPECT_TRUE(bip32Paths.empty());
         }
      }
   }

   EXPECT_FALSE(signer4.verify());
   EXPECT_FALSE(signer4.isResolved());
   EXPECT_FALSE(signer4.isSigned());

   //should fail to get txid
   try {
      signer4.getTxId();
      EXPECT_TRUE(false);
   } catch (...) {}

   //deser from same state into wlt_2 signer
   Signer signer5;

   //in this case, we can't set the utxos first then deser the state, as it would break
   //utxo ordering. we have to deser first, then populate utxos
   signer5.deserializeState(signer4.serializeState());

   //should fail since second spender isn't resolved and we lack a feed
   try {
      signer5.getTxId();
      EXPECT_TRUE(false);
   } catch (...) {}

   //set the feed
   signer5.setFeed(assetFeed3);

   //tx should be unsigned
   EXPECT_FALSE(signer5.verify());

   //should produce valid txid without signing
   BinaryData txid;
   try {
      auto unsignedTx = signer5.serializeUnsignedTx();

      Tx tx(unsignedTx);
      EXPECT_EQ(tx.getLockTime(), locktime);

      txid = signer5.getTxId();
   } catch (...) {
      EXPECT_TRUE(false);
   }

   //producing a txid should not change the signer status from unsigned to signed
   EXPECT_FALSE(signer5.verify());

   {
      auto lock = assetWlt_2->lockDecryptedContainer();
      signer5.sign();
   }

   EXPECT_TRUE(signer5.verify());

   //check txid pre sig with txid post sig
   EXPECT_EQ(txid, signer5.getTxId());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, Wallet_SpendTest_Nested_P2WPKH)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   IO::CreationParams params{
      homedir_, {}, 1ms, {}, 1ms, 0
   };

   //create empty bip32 wallet
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Virgin));
   auto assetWlt = AssetWallet_Single::createFromSeed(move(seed), params);

   //add p2sh-p2wpkh account
   std::vector<unsigned> derPath = { 0x800061a5, 0x80000000 };

   auto mainAccType =
      assetWlt->makeNewBip32AccTypeObject(derPath);
   mainAccType->setMain(true);
   mainAccType->setAddressLookup(3);
   mainAccType->setDefaultAddressType(
      AddressEntryType(AddressEntryType_P2SH | AddressEntryType_P2WPKH));
   mainAccType->addAddressType(
      AddressEntryType(AddressEntryType_P2SH | AddressEntryType_P2WPKH));
   auto accountID = assetWlt->createBIP32Account(mainAccType);

   //// register with db ////
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 12 to addr0, nested P2WPKH
      auto addr0 = assetWlt->getNewAddress();
      signer.addRecipient(addr0->getRecipient(12 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());

      //spend 15 to addr1, nested P2WPKH
      auto addr1 = assetWlt->getNewAddress();
      signer.addRecipient(addr1->getRecipient(15 * COIN));
      addrVec.push_back(addr1->getPrefixedHash());

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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

   BinaryData signerState;
   {
      ////spend 18 back to scrAddrB, with change to addr[2]

      auto spendVal = 18 * COIN;
      Signer signer2;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListZC();

      //create feed from asset wallet
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //create spenders
      uint64_t total = 0;
      for (auto& utxo : unspentVec) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo));
      }

      //creates outputs
      //spend 18 to addr 0, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), spendVal);
      signer2.addRecipient(recipient2);

      if (total > spendVal) {
         //deal with change, no fee
         auto changeVal = total - spendVal;
         auto addr2 = assetWlt->getNewAddress();
         signer2.addRecipient(addr2->getRecipient(changeVal));
         addrVec.push_back(addr2->getPrefixedHash());
      }

      //sign, verify & broadcast
      {
         signer2.setFeed(assetFeed);
         signer2.resolvePublicData();
      }

      EXPECT_FALSE(signer2.verify());
      signerState = signer2.serializeState();
   }

   {
      Signer signer3;
      signer3.deserializeState(signerState);
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //sign, verify & broadcast
      {
         auto lock = assetWlt->lockDecryptedContainer();
         signer3.setFeed(assetFeed);
         signer3.sign();
      }

      EXPECT_TRUE(signer3.verify());

      DBTestUtils::ZcVector zcVec2;
      zcVec2.push_back(signer3.serializeSignedTx(), 15000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

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
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]);
   EXPECT_EQ(scrObj->getFullBalance(), 9 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, Wallet_SpendTest_Nested_P2WPKH_WOResolution_fromWOCopy)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////

   auto rawEntropy = CryptoPRNG::generateRandom(32);
   std::filesystem::path woPath, wltPath;

   IO::CreationParams params{
      homedir_, {}, 1ms, {}, 1ms, 0
   };

   Signer signer3;
   {
      //create bip32 wallet
      std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
         new Armory::Seeds::ClearTextSeed_BIP32(
            rawEntropy, Armory::Seeds::SeedType::BIP32_Virgin));
      auto assetWlt = AssetWallet_Single::createFromSeed(move(seed), params);

      //add p2sh-p2wpkh account
      std::vector<unsigned> derPath = { 0x800061a5, 0x80000000 };
      auto mainAccType =
         assetWlt->makeNewBip32AccTypeObject(derPath);
      mainAccType->setMain(true);
      mainAccType->setAddressLookup(3);
      mainAccType->setDefaultAddressType(
         AddressEntryType(AddressEntryType_P2SH | AddressEntryType_P2WPKH));
      mainAccType->addAddressType(
         AddressEntryType(AddressEntryType_P2SH | AddressEntryType_P2WPKH));

         std::set<unsigned> nodes = { 0, 1 };
      mainAccType->setNodes(nodes);
      mainAccType->setOuterAccountID(*nodes.begin());
      mainAccType->setInnerAccountID(*nodes.rbegin());

      auto accountID = assetWlt->createBIP32Account(mainAccType);

      //make a WO copy
      wltPath = assetWlt->getDbFilename();
      woPath = AssetWallet::forkWatchingOnly(
         IO::OpenFileParams{wltPath, nullptr});
   }
   std::filesystem::remove(wltPath);
   auto wltWO = std::dynamic_pointer_cast<AssetWallet_Single>(
      AssetWallet::loadMainWalletFromFile(
         IO::OpenFileParams{woPath, nullptr}));

   //recreate empty bip32 wallet
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         rawEntropy, Armory::Seeds::SeedType::BIP32_Virgin));
   auto emptyWlt = AssetWallet_Single::createFromSeed(move(seed), params);

   //// register with db ////
   std::vector<BinaryData> addrVec;

   auto hashSet = wltWO->getAddrHashSet();
   std::vector<BinaryData> hashVec;
   hashVec.insert(hashVec.begin(), hashSet.begin(), hashSet.end());

   DBTestUtils::registerWallet(clients_, bdvID, hashVec, wltWO->getID(),
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto dbAssetWlt = bdvPtr->getWalletOrLockbox(wltWO->getID());


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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 12 to addr0, nested P2WPKH
      auto addr0 = wltWO->getNewAddress();
      signer.addRecipient(addr0->getRecipient(12 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());

      //spend 15 to addr1, nested P2WPKH
      auto addr1 = wltWO->getNewAddress();
      signer.addRecipient(addr1->getRecipient(15 * COIN));
      addrVec.push_back(addr1->getPrefixedHash());

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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

   //-- resolve unsigned tx with WO wallet --//
   BinaryData signerState;
   {
      ////spend 18 back to scrAddrB, with change to addr[2]

      auto spendVal = 18 * COIN;
      Signer signer2;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListZC();

      //create feed from asset wallet
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(wltWO);

      //create spenders
      uint64_t total = 0;
      for (auto& utxo : unspentVec)
      {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo));
      }

      //creates outputs
      //spend 18 to addr 0, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), spendVal);
      signer2.addRecipient(recipient2);

      if (total > spendVal) {
         //deal with change, no fee
         auto changeVal = total - spendVal;
         auto addr2 = wltWO->getNewAddress();
         signer2.addRecipient(addr2->getRecipient(changeVal));
         addrVec.push_back(addr2->getPrefixedHash());
      }

      /*
      Merge state of signer2 into signer3. This is to check that
      resolved data merges in properly into existing spender objects
      */
      signer3.deserializeState(signer2.serializeState());

      //sign, verify & broadcast
      {
         signer2.setFeed(assetFeed);
         signer2.resolvePublicData();
      }

      EXPECT_FALSE(signer2.verify());
      signerState = signer2.serializeState();
   }

   //-- sign tx with empty wallet --//
   {
      signer3.deserializeState(signerState);
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(emptyWlt);

      //sign, verify & broadcast
      {
         auto lock = emptyWlt->lockDecryptedContainer();
         signer3.setFeed(assetFeed);
         signer3.sign();
      }

      EXPECT_TRUE(signer3.verify());

      DBTestUtils::ZcVector zcVec2;
      zcVec2.push_back(signer3.serializeSignedTx(), 15000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

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
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]);
   EXPECT_EQ(scrObj->getFullBalance(), 9 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, Wallet_SpendTest_Nested_P2WPKH_WOResolution_fromXPub)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };
   //// create assetWlt ////

   //create empty bip32 wallet
   IO::CreationParams params{
      homedir_, {}, 1ms, {}, 1ms, 0
   };

   auto rawEntropy = CryptoPRNG::generateRandom(32);
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         rawEntropy, Armory::Seeds::SeedType::BIP32_Virgin));
   auto emptyWlt = AssetWallet_Single::createFromSeed(move(seed), params);

   //create empty WO wallet
   auto wltWO = AssetWallet_Single::createBlank(
      "walletWO1", IO::CreationParams{ homedir_ }
   );

   //derive public root
   std::vector<unsigned> derPath = { 0x800061a5, 0x80000000 };
   BIP32_Node seedNode;
   seedNode.initFromSeed(rawEntropy);
   auto seedFingerprint = seedNode.getThisFingerprint();
   for (auto& derId : derPath) {
      seedNode.derivePrivate(derId);
   }
   auto pubNode = seedNode.getPublicCopy();
   auto xpub = pubNode.getBase58();

   //add p2sh-p2wpkh account
   auto mainAccType = AccountType_BIP32::makeFromDerPaths(
      seedFingerprint, {derPath});
   mainAccType->setRoots({{derPath, xpub}});
   mainAccType->setMain(true);
   mainAccType->setAddressLookup(3);
   mainAccType->setDefaultAddressType(
      AddressEntryType(AddressEntryType_P2SH | AddressEntryType_P2WPKH));
   mainAccType->addAddressType(
      AddressEntryType(AddressEntryType_P2SH | AddressEntryType_P2WPKH));
   auto accountID = wltWO->createBIP32Account(mainAccType);

   //// register with db ////
   std::vector<BinaryData> addrVec;
   auto hashSet = wltWO->getAddrHashSet();

   std::vector<BinaryData> hashVec;
   hashVec.insert(hashVec.begin(), hashSet.begin(), hashSet.end());

   DBTestUtils::registerWallet(clients_, bdvID, hashVec, wltWO->getID(),
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   auto dbAssetWlt = bdvPtr->getWalletOrLockbox(wltWO->getID());

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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 12 to addr0, nested P2WPKH
      auto addr0 = wltWO->getNewAddress();
      signer.addRecipient(addr0->getRecipient(12 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());

      //spend 15 to addr1, nested P2WPKH
      auto addr1 = wltWO->getNewAddress();
      signer.addRecipient(addr1->getRecipient(15 * COIN));
      addrVec.push_back(addr1->getPrefixedHash());

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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

   BinaryData signerState;
   {
      ////spend 18 back to scrAddrB, with change to addr[2]

      auto spendVal = 18 * COIN;
      Signer signer2;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListZC();

      //create feed from asset wallet
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(wltWO);

      //create spenders
      uint64_t total = 0;
      for (auto& utxo : unspentVec) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo));
      }

      //creates outputs
      //spend 18 to addr 0, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), spendVal);
      signer2.addRecipient(recipient2);

      if (total > spendVal)
      {
         //deal with change, no fee
         auto changeVal = total - spendVal;
         auto addr2 = wltWO->getNewAddress();
         signer2.addRecipient(addr2->getRecipient(changeVal));
         addrVec.push_back(addr2->getPrefixedHash());
      }

      //sign, verify & broadcast
      {
         signer2.setFeed(assetFeed);
         signer2.resolvePublicData();
      }

      EXPECT_FALSE(signer2.verify());
      signerState = signer2.serializeState();
   }

   {
      Signer signer3;
      signer3.deserializeState(signerState);
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(emptyWlt);

      //sign, verify & broadcast
      {
         auto lock = emptyWlt->lockDecryptedContainer();
         signer3.setFeed(assetFeed);
         signer3.sign();
      }

      EXPECT_TRUE(signer3.verify());

      DBTestUtils::ZcVector zcVec2;
      zcVec2.push_back(signer3.serializeSignedTx(), 15000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

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
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]);
   EXPECT_EQ(scrObj->getFullBalance(), 9 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, Wallet_SpendTest_Nested_P2PK)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   IO::CreationParams params{
      homedir_, {}, 1ms, {}, 1ms, 3
   };
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 12 to addr0, nested P2K
      auto addr0 = assetWlt->getNewAddress(
         AddressEntryType(AddressEntryType_P2PK | AddressEntryType_P2SH));
      signer.addRecipient(addr0->getRecipient(12 * COIN));
      addrVec.push_back(addr0->getPrefixedHash());

      //spend 15 to addr1, nested P2PK
      auto addr1 = assetWlt->getNewAddress(
         AddressEntryType(AddressEntryType_P2PK | AddressEntryType_P2SH));
      signer.addRecipient(addr1->getRecipient(15 * COIN));
      addrVec.push_back(addr1->getPrefixedHash());

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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

   {
      ////spend 18 back to scrAddrB, with change to addr[2]

      auto spendVal = 18 * COIN;
      Signer signer2;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListZC();

      //create feed from asset wallet
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //create spenders
      uint64_t total = 0;
      for (auto& utxo : unspentVec) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo));
      }

      //creates outputs
      //spend 18 to addr 0, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), spendVal);
      signer2.addRecipient(recipient2);

      if (total > spendVal) {
         //deal with change, no fee
         auto changeVal = total - spendVal;
         auto addr2 = assetWlt->getNewAddress(
            AddressEntryType(AddressEntryType_P2PK | AddressEntryType_P2SH));
         signer2.addRecipient(addr2->getRecipient(changeVal));
         addrVec.push_back(addr2->getPrefixedHash());
      }

      //add opreturn for coverage
      auto opreturn_msg = BinaryData::fromString("op_return message testing"sv);
      signer2.addRecipient(std::make_shared<Recipient_OPRETURN>(opreturn_msg));

      //sign, verify & broadcast
      {
         auto lock = assetWlt->lockDecryptedContainer();
         signer2.setFeed(assetFeed);
         signer2.sign();
      }
      EXPECT_TRUE(signer2.verify());

      DBTestUtils::ZcVector zcVec2;
      zcVec2.push_back(signer2.serializeSignedTx(), 15000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

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
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]);
   EXPECT_EQ(scrObj->getFullBalance(), 9 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_FromAccount_Reload)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   IO::CreationParams params{
      homedir_, {}, 1ms, {}, 1ms, 0
   };
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Virgin));
   auto assetWlt = AssetWallet_Single::createFromSeed(move(seed), params);

   //add a bip32 account
   {
      auto root = std::dynamic_pointer_cast<AssetEntry_BIP32Root>(
         assetWlt->getRoot());
      ASSERT_NE(root, nullptr);

      auto lock = assetWlt->lockDecryptedContainer();
      auto accTypePtr = AccountType_BIP32::makeFromDerPaths(
         root->getSeedFingerprint(true), {{0x80000000, 0x80000000, 0, 1}});
      accTypePtr->setAddressLookup(5);
      accTypePtr->addAddressType(AddressEntryType_P2WPKH);
      accTypePtr->addAddressType(AddressEntryType(
         AddressEntryType_P2SH | AddressEntryType_P2WPKH));
      accTypePtr->setDefaultAddressType(AddressEntryType_P2WPKH);
      accTypePtr->setMain(true);

      assetWlt->createBIP32Account(accTypePtr);
   }

   //register with db
   std::vector<std::shared_ptr<AddressEntry>> addrVec;
   auto accID = assetWlt->getMainAccountID();
   addrVec.push_back(assetWlt->getNewAddress(accID, AddressEntryType_P2WPKH));
   addrVec.push_back(assetWlt->getNewAddress(accID, AddressEntryType_P2WPKH));
   addrVec.push_back(assetWlt->getNewAddress(accID, AddressEntryType_P2WPKH));

   std::vector<BinaryData> hashVec;
   for (auto addrPtr : addrVec) {
      hashVec.push_back(addrPtr->getPrefixedHash());
   }

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
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 3U);

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
   for (auto& addrPtr : addrVec) {
      scrObj = dbAssetWlt->getScrAddrObjByKey(addrPtr->getPrefixedHash());
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
      auto&& unspentVec = wlt->getSpendableTxOutListForValue(spendVal);

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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 12 to addr0, use P2WPKH
      signer.addRecipient(addrVec[0]->getRecipient(12 * COIN));

      //spend 15 to addr1, use P2WPKH
      signer.addRecipient(addrVec[1]->getRecipient(15 * COIN));

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

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //destroy wallet object
   auto fName = assetWlt->getDbFilename();
   ASSERT_EQ(assetWlt.use_count(), 1);
   assetWlt.reset();

   //reload it
   auto controlPassLbd = []
      (const std::set<EncryptionKeyId>&)->SecureBinaryData
   {
      return {};
   };
   auto loadedWlt = AssetWallet::loadMainWalletFromFile(
      IO::OpenFileParams{fName, controlPassLbd});
   assetWlt = std::dynamic_pointer_cast<AssetWallet_Single>(loadedWlt);
   auto accIdReload = assetWlt->getMainAccountID();
   ASSERT_EQ(accIdReload, accID);

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
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   {
      ////spend 18 back to scrAddrB, with change to addr2

      auto spendVal = 18 * COIN;
      Signer signer2;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListZC();

      //create feed from asset wallet
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //create spenders
      uint64_t total = 0;
      for (auto& utxo : unspentVec) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo));
      }

      //creates outputs
      //spend 18 to scrAddrB, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), spendVal);
      signer2.addRecipient(recipient2);

      if (total > spendVal) {
         //change to new address, use P2SH-P2WPKH
         auto changeVal = total - spendVal;
         auto addr3 = assetWlt->getNewAddress(accID,
            AddressEntryType(AddressEntryType_P2SH | AddressEntryType_P2WPKH));
         signer2.addRecipient(addr3->getRecipient(changeVal));

         addrVec.push_back(addr3);
         hashVec.push_back(addr3->getPrefixedHash());
      }

      //sign, verify & broadcast
      {
         auto&& lock = assetWlt->lockDecryptedContainer();
         signer2.setFeed(assetFeed);
         signer2.sign();
      }
      EXPECT_TRUE(signer2.verify());

      DBTestUtils::ZcVector zcVec2;
      zcVec2.push_back(signer2.serializeSignedTx(), 15000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

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
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   try {
      scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[3]->getPrefixedHash());
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
      ASSERT_TRUE(false); //should never get here
   }
   catch (const std::exception&) {}

   //register new change address
   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID(),
      false, true);

   //check new wallet balance again, change value should appear
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //full node cannot track zc prior to address registration, balance will
   //show after the zc mines
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[3]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //mine 2 blocks
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrC, 2);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 5U);

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 48 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 155 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //check new wallet balances
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   //change balance will now show on post zc registered address
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[3]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 9 * COIN);

   {
      //check there are no zc utxos anymore
      auto unspentVec = dbAssetWlt->getSpendableTxOutListZC();
      ASSERT_EQ(unspentVec.size(), 0ULL);
   }

   {
      ////clean up change address

      auto spendVal = 9 * COIN;
      Signer signer3;
      signer3.setFlags(SCRIPT_VERIFY_SEGWIT);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListForValue();

      //create feed from asset wallet
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //create spenders
      uint64_t total = 0;
      for (auto& utxo : unspentVec) {
         total += utxo.getValue();
         signer3.addSpender(getSpenderPtr(utxo));
      }

      //creates outputs
      auto recipient3 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrE.getSliceCopy(1, 20), spendVal);
      signer3.addRecipient(recipient3);

      EXPECT_EQ(total, spendVal);

      //sign, verify & broadcast
      {
         auto&& lock = assetWlt->lockDecryptedContainer();
         signer3.setFeed(assetFeed);
         signer3.sign();
      }
      EXPECT_TRUE(signer3.verify());

      DBTestUtils::ZcVector zcVec3;
      zcVec3.push_back(signer3.serializeSignedTx(), 15000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec3);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 48 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 155 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 9 * COIN);

   //check new wallet balances
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[3]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_BIP32_Accounts)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////

   auto passphrase = SecureBinaryData::fromString("test");
   IO::CreationParams params{
      homedir_, passphrase, 1ms, {}, 1ms, 0
   };
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Virgin));
   auto assetWlt = AssetWallet_Single::createFromSeed(move(seed), params);

   auto rootBip32 = std::dynamic_pointer_cast<
      AssetEntry_BIP32Root>(assetWlt->getRoot());
   auto seedFingerprint = rootBip32->getSeedFingerprint(true);

   //salted account
   std::vector<unsigned> derPath = { 0x80000099, 0x80000001 };
   auto salt = CryptoPRNG::generateRandom(32);
   auto saltedAccType =
      AccountType_BIP32_Salted::makeFromDerPaths(seedFingerprint, {derPath}, salt);
   saltedAccType->setAddressLookup(5);
   saltedAccType->setDefaultAddressType(
      AddressEntryType(AddressEntryType_P2SH | AddressEntryType_P2WPKH));
   saltedAccType->addAddressType(
      AddressEntryType(AddressEntryType_P2SH | AddressEntryType_P2WPKH));

   auto passphraseLbd = [&passphrase]
      (const std::set<EncryptionKeyId>&)->SecureBinaryData
   {
      return passphrase;
   };
   assetWlt->setPassphrasePromptLambda(passphraseLbd);

   auto accountID1 = assetWlt->createBIP32Account(saltedAccType);

   //regular account
   std::vector<unsigned> derPath2 = { 0x80000099, 0x80000001 };
   auto mainAccType = assetWlt->makeNewBip32AccTypeObject(derPath2);
   mainAccType->setAddressLookup(5);
   mainAccType->setDefaultAddressType(
      AddressEntryType_P2WPKH);
   mainAccType->addAddressType(AddressEntryType_P2WPKH);
   auto accountID2 = assetWlt->createBIP32Account(mainAccType);
   assetWlt->resetPassphrasePromptLambda();

   //register with db
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 3U);

   auto newAddr1 = assetWlt->getNewAddress(accountID1);
   auto newAddr2 = assetWlt->getNewAddress(accountID2);
   auto newAddr3 = assetWlt->getNewAddress(accountID2);

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

   {
      ////spend 27 from wlt to acc1 & acc2
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend spendVal to newAddr
      signer.addRecipient(newAddr1->getRecipient(14 * COIN));
      signer.addRecipient(newAddr2->getRecipient(13 * COIN));

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

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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

   //register new wallet
   std::vector<BinaryData> hashVec;
   hashVec.push_back(newAddr1->getPrefixedHash());
   hashVec.push_back(newAddr2->getPrefixedHash());
   hashVec.push_back(newAddr3->getPrefixedHash());

   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID(),
      false, false);
   auto dbAssetWlt = bdvPtr->getWalletOrLockbox(assetWlt->getID());

   //mine some blocks
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrC, 2);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 5U);

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 155 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 14 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 13 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[2]);
   EXPECT_EQ(scrObj->getFullBalance(), 0ULL);

   //spend from the new addresses
   {

      auto spendVal = 27 * COIN;
      Signer signer;

      auto feed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListForValue(spendVal);

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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend spendVal to newAddr
      signer.addRecipient(newAddr3->getRecipient(spendVal));

      //sign, verify then broadcast
      {
         auto passlbd = [passphrase]
            (const std::set<EncryptionKeyId>&)
            ->SecureBinaryData
         {
            return passphrase;
         };

         assetWlt->setPassphrasePromptLambda(passlbd);
         auto lock = assetWlt->lockDecryptedContainer();
         signer.setFeed(feed);
         signer.sign();
      }

      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 155 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0ULL);
   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[2]);
   EXPECT_EQ(scrObj->getFullBalance(), 27 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_FromExtendedAddress_Armory135)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////

   auto passphrase = SecureBinaryData::fromString("test");
   IO::CreationParams params{
      homedir_,
      passphrase, 1ms,
      SecureBinaryData::fromString("control"), 1ms,
      5
   };
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   //register with db
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 3U);

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

   //grab enough addresses to trigger a lookup extention
   auto accountPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
   auto outerAcc = accountPtr->getOuterAccount();
   EXPECT_EQ(outerAcc->getAssetCount(), 5U);

   for (unsigned i = 0; i < 15; i++) {
      assetWlt->getNewAddress();
   }
   auto newAddr = assetWlt->getNewAddress();
   EXPECT_EQ(outerAcc->getAssetCount(), 105U);

   {
      ////spend 27 from wlt to newAddr
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend spendVal to newAddr
      signer.addRecipient(newAddr->getRecipient(spendVal));
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

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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

   //register new wallet
   std::vector<BinaryData> hashVec;
   hashVec.push_back(newAddr->getPrefixedHash());
   auto newAddr2 = assetWlt->getNewAddress();
   hashVec.push_back(newAddr2->getPrefixedHash());

   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID(),
      false, false);
   auto dbAssetWlt = bdvPtr->getWalletOrLockbox(assetWlt->getID());

   //mine some blocks
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrC, 2);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 5U);

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 155 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0ULL);

   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 27 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 0ULL);

   //spend from the new address
   {
      //spend from the new address

      auto spendVal = 27 * COIN;
      Signer signer;

      auto feed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListForValue(spendVal);

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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend spendVal to newAddr
      signer.addRecipient(newAddr2->getRecipient(spendVal));

      //sign, verify then broadcast
      {
         auto passlbd = [passphrase]
            (const std::set<EncryptionKeyId>&)->SecureBinaryData
         {
            return passphrase;
         };

         assetWlt->setPassphrasePromptLambda(passlbd);
         auto lock = assetWlt->lockDecryptedContainer();
         signer.setFeed(feed);
         signer.sign();
      }

      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 155 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0ULL);
   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 27 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_FromExtendedAddress_BIP32)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////

   auto passphrase = SecureBinaryData::fromString("test");
   IO::CreationParams params{
      homedir_,
      passphrase, 1ms,
      SecureBinaryData::fromString("control"), 1ms,
      5
   };

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt = AssetWallet_Single::createFromSeed(move(seed), params);

   //register with db
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 3U);

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

   //grab enough addresses to trigger a lookup extention
   auto accountPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
   auto outerAcc = accountPtr->getOuterAccount();
   EXPECT_EQ(outerAcc->getAssetCount(), 5U);

   for (unsigned i = 0; i < 10; i++) {
      assetWlt->getNewAddress();
   }
   auto newAddr = assetWlt->getNewAddress();

   EXPECT_EQ(outerAcc->getAssetCount(), 105U);

   {
      ////spend 27 from wlt to newAddr
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend spendVal to newAddr
      signer.addRecipient(newAddr->getRecipient(spendVal));

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

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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

   //register new wallet
   std::vector<BinaryData> hashVec;
   hashVec.push_back(newAddr->getPrefixedHash());
   auto newAddr2 = assetWlt->getNewAddress();
   hashVec.push_back(newAddr2->getPrefixedHash());

   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID(),
      false, false);
   auto dbAssetWlt = bdvPtr->getWalletOrLockbox(assetWlt->getID());

   //mine some blocks
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrC, 2);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 5U);

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 155 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 27 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 0ULL);

   //spend from the new address
   {
      //spend from the new address

      auto spendVal = 27 * COIN;
      Signer signer;

      auto feed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListForValue(spendVal);

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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend spendVal to newAddr
      signer.addRecipient(newAddr2->getRecipient(spendVal));

      //sign, verify then broadcast
      {
         auto passlbd = [passphrase]
            (const std::set<EncryptionKeyId>&)->SecureBinaryData
         {
            return passphrase;
         };

         assetWlt->setPassphrasePromptLambda(passlbd);
         auto lock = assetWlt->lockDecryptedContainer();
         signer.setFeed(feed);
         signer.sign();
      }

      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 155 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0ULL);
   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 27 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_FromExtendedAddress_Salted)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   auto passphrase = SecureBinaryData::fromString("test");
   IO::CreationParams params{
      homedir_,
      passphrase, 1ms,
      SecureBinaryData::fromString("control"), 1ms,
      0
   };

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Virgin));
   auto assetWlt = AssetWallet_Single::createFromSeed(move(seed), params);

   auto rootBip32 = std::dynamic_pointer_cast<
      AssetEntry_BIP32Root>(assetWlt->getRoot());
   auto seedFingerprint = rootBip32->getSeedFingerprint(true);

   std::vector<unsigned> derPath = {0x80000099, 0x80000001};
   auto salt = CryptoPRNG::generateRandom(32);
   auto saltedAccType =
      AccountType_BIP32_Salted::makeFromDerPaths(seedFingerprint, {derPath}, salt);
   saltedAccType->setAddressLookup(5);
   saltedAccType->setDefaultAddressType(
      AddressEntryType_P2WPKH);
   saltedAccType->addAddressType(AddressEntryType_P2WPKH);
   saltedAccType->setMain(true);

   auto passphraseLbd = [&passphrase]
      (const std::set<EncryptionKeyId>&)->SecureBinaryData
   {
      return passphrase;
   };
   assetWlt->setPassphrasePromptLambda(passphraseLbd);

   //add salted account
   auto accountID = assetWlt->createBIP32Account(saltedAccType);
   assetWlt->resetPassphrasePromptLambda();

   //register with db
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 3U);

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

   //grab enough addresses to trigger a lookup extention
   auto accountPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
   auto outerAcc = accountPtr->getOuterAccount();
   EXPECT_EQ(outerAcc->getAssetCount(), 5U);

   for (unsigned i = 0; i < 10; i++) {
      assetWlt->getNewAddress();
   }
   auto newAddr = assetWlt->getNewAddress();
   EXPECT_EQ(outerAcc->getAssetCount(), 105U);

   {
      ////spend 27 from wlt to newAddr
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend spendVal to newAddr
      signer.addRecipient(newAddr->getRecipient(spendVal));

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

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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

   //register new wallet
   std::vector<BinaryData> hashVec;
   hashVec.push_back(newAddr->getPrefixedHash());
   auto newAddr2 = assetWlt->getNewAddress();
   hashVec.push_back(newAddr2->getPrefixedHash());

   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID(),
      false, false);
   auto dbAssetWlt = bdvPtr->getWalletOrLockbox(assetWlt->getID());

   //mine some blocks
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrC, 2);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 5U);

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 155 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 27 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 0ULL);

   //spend from the new address
   {
      //spend from the new address

      auto spendVal = 27 * COIN;
      Signer signer;

      auto feed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListForValue(spendVal);

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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend spendVal to newAddr
      signer.addRecipient(newAddr2->getRecipient(spendVal));

      //sign, verify then broadcast
      {
         auto passlbd = [passphrase]
            (const std::set<EncryptionKeyId>&)
            ->SecureBinaryData
         {
            return passphrase;
         };

         assetWlt->setPassphrasePromptLambda(passlbd);
         auto lock = assetWlt->lockDecryptedContainer();
         signer.setFeed(feed);
         signer.sign();
      }

      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 155 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0ULL);
   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 27 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_FromExtendedAddress_ECDH)
{
   //ecdh account base key pair
   auto privKey = READHEX(
      "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F");
   auto pubKey = CryptoECDSA().ComputePublicKey(privKey, true);

   //setup bdm
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);
   initBDM();

   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create assetWlt ////
   auto passphrase = SecureBinaryData::fromString("test");
   IO::CreationParams params{
      homedir_,
      passphrase, 1ms,
      SecureBinaryData::fromString("control"), 1ms,
      0
   };

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Virgin));
   auto assetWlt = AssetWallet_Single::createFromSeed(move(seed), params);

   auto ecdhAccType = std::make_shared<AccountType_ECDH>(privKey, pubKey);
   ecdhAccType->setDefaultAddressType(
      AddressEntryType_P2WPKH);
   ecdhAccType->addAddressType(AddressEntryType_P2WPKH);
   ecdhAccType->setMain(true);

   auto passphraseLbd = [&passphrase]
      (const std::set<EncryptionKeyId>&)->SecureBinaryData
   {
      return passphrase;
   };

   //add salted account
   assetWlt->setPassphrasePromptLambda(passphraseLbd);
   auto addrAccountObj = assetWlt->createAccount(ecdhAccType, nullptr);
   assetWlt->resetPassphrasePromptLambda();

   //register with db
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   auto bdvPtr = DBTestUtils::getBDV(clients_, bdvID);

   //wait on signals
   DBTestUtils::goOnline(clients_, bdvID);
   DBTestUtils::waitOnBDMReady(clients_, bdvID);
   auto wlt = bdvPtr->getWalletOrLockbox(wallet1id);
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 3U);

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

   //generate some ECDH addresses
   EXPECT_EQ(TestUtils::getMainAccountAssetCount(assetWlt), 0U);

   auto assAccPtr = addrAccountObj->getOuterAccount();
   auto accPtr = dynamic_cast<AssetAccount_ECDH*>(assAccPtr.get());
   ASSERT_NE(accPtr, nullptr);

   for (unsigned i = 0; i < 5; i++) {
      auto tx = assetWlt->beginSubDBTransaction(assetWlt->getID(), true);
      auto salt = CryptoPRNG::generateRandom(32);
      accPtr->addSalt(tx, salt);
   }

   std::vector<std::shared_ptr<AddressEntry>> addrVec;
   for (unsigned i = 0; i < 5; i++) {
      addrVec.push_back(assetWlt->getNewAddress());
   }
   EXPECT_EQ(TestUtils::getMainAccountAssetCount(assetWlt), 5U);

   {
      ////spend 27 from wlt to newAddr
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend spendVal to newAddr
      signer.addRecipient(addrVec[0]->getRecipient(spendVal));

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

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

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

   //register new wallet
   std::vector<BinaryData> hashVec;
   hashVec.push_back(addrVec[0]->getPrefixedHash());
   hashVec.push_back(addrVec[1]->getPrefixedHash());

   DBTestUtils::registerWallet(clients_, bdvID, hashVec, assetWlt->getID(),
      false, false);
   auto dbAssetWlt = bdvPtr->getWalletOrLockbox(assetWlt->getID());

   //mine some blocks
   DBTestUtils::mineNewBlock(theBDMt_, TestChain::addrC, 2);
   DBTestUtils::waitOnNewBlockSignal(clients_, bdvID);
   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 5U);

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 155 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 27 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 0ULL);

   //spend from the new address
   {
      //spend from the new address

      auto spendVal = 27 * COIN;
      Signer signer;

      auto feed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListForValue(spendVal);

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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend spendVal to newAddr
      signer.addRecipient(addrVec[1]->getRecipient(spendVal));

      //sign, verify then broadcast
      {
         auto passlbd = [passphrase]
            (const std::set<EncryptionKeyId>&)->SecureBinaryData
         {
            return passphrase;
         };

         assetWlt->setPassphrasePromptLambda(passlbd);
         auto lock = assetWlt->lockDecryptedContainer();
         signer.setFeed(feed);
         signer.sign();
      }

      EXPECT_TRUE(signer.verify());

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

   //check balances
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrA);
   EXPECT_EQ(scrObj->getFullBalance(), 50 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrB);
   EXPECT_EQ(scrObj->getFullBalance(), 30 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrC);
   EXPECT_EQ(scrObj->getFullBalance(), 155 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrD);
   EXPECT_EQ(scrObj->getFullBalance(), 8 * COIN);
   scrObj = wlt->getScrAddrObjByKey(TestChain::scrAddrE);
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[0]);
   EXPECT_EQ(scrObj->getFullBalance(), 0ULL);
   scrObj = dbAssetWlt->getScrAddrObjByKey(hashVec[1]);
   EXPECT_EQ(scrObj->getFullBalance(), 27 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_InjectSignature)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   IO::CreationParams params{
      homedir_, {}, 1ms, {}, 1ms, 5
   };

   //// create assetWlt ////
   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //register with db
   std::vector<std::shared_ptr<AddressEntry>> addrVec;
   addrVec.push_back(assetWlt->getNewAddress(AddressEntryType_P2WPKH));
   addrVec.push_back(assetWlt->getNewAddress(AddressEntryType_P2WPKH));
   addrVec.push_back(assetWlt->getNewAddress(AddressEntryType_P2WPKH));

   std::vector<BinaryData> hashVec;
   for (auto addrPtr : addrVec) {
      hashVec.push_back(addrPtr->getPrefixedHash());
   }
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
   for (auto& addrPtr : addrVec) {
      scrObj = dbAssetWlt->getScrAddrObjByKey(addrPtr->getPrefixedHash());
      EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   }

   {
      ////spend 27 from wlt to assetWlt's first 2 unused addresses
      ////send rest back to scrAddrA

      auto spendVal = 27 * COIN;
      Signer signer;
      Signer signer_inject;

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
      unsigned sigCount = 0;
      for (auto& utxo : utxoVec) {
         total += utxo.getValue();
         signer.addSpender(getSpenderPtr(utxo));
         signer_inject.addSpender(getSpenderPtr(utxo));
         ++sigCount;
      }

      //spend 12 to addr0, use P2WPKH
      signer.addRecipient(addrVec[0]->getRecipient(12 * COIN));
      signer_inject.addRecipient(addrVec[0]->getRecipient(12 * COIN));

      //spend 15 to addr1, use P2WPKH
      signer.addRecipient(addrVec[1]->getRecipient(15 * COIN));
      signer_inject.addRecipient(addrVec[1]->getRecipient(15 * COIN));

      if (total > spendVal) {
         //deal with change, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
         signer_inject.addRecipient(recipientChange);
      }


      //sign & verify
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());

      //extract sigs from tx 
      auto signedTxRaw = signer.serializeSignedTx();
      std::vector<SecureBinaryData> sigs;
      {
         Tx signedTx(signedTxRaw);

         for (unsigned i=0; i<signedTx.getNumTxIn(); i++) {
            auto txInCopy = signedTx.getTxInCopy(i);
            auto script = txInCopy.getScript();

            auto scriptItems = BtcUtils::splitPushOnlyScriptRefs(script);
            for (auto& item : scriptItems) {
               if (item.getSize() > 68 &&
                  item.getPtr()[0] == 0x30 &&
                  item.getPtr()[2] == 0x02) {
                  sigs.push_back(item);
                  break;
               }
            }
         }

         ASSERT_EQ(sigs.size(), sigCount);
      }

      //try to inject into unresolved signer, should fail
      for (unsigned i=0; i<sigs.size(); i++) {
         try {
            signer_inject.injectSignature(i, sigs[i]);
            EXPECT_TRUE(false);
         } catch (const std::exception&) {}
      }

      //resolve signer
      signer_inject.setFeed(feed);
      signer_inject.resolvePublicData();
      EXPECT_FALSE(signer_inject.verify());
      EXPECT_FALSE(signer_inject.isSigned());

      //inject sigs
      for (unsigned i=0; i<sigs.size(); i++) {
         try {
            signer_inject.injectSignature(i, sigs[i]);
         } catch (const std::exception&) {
            EXPECT_TRUE(false);
         }
      }

      //verify sigs
      EXPECT_TRUE(signer_inject.isSigned());
      EXPECT_TRUE(signer_inject.verify());
      
      //finally, broadcast
      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signedTxRaw, 14000000);

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
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 12 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 15 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);

   {
      ////spend 18 back to scrAddrB, with change to addr2

      auto spendVal = 18 * COIN;
      Signer signer2;
      Signer signer_inject;
      signer2.setFlags(SCRIPT_VERIFY_SEGWIT);

      //get utxo list for spend value
      auto unspentVec = dbAssetWlt->getSpendableTxOutListZC();

      //create feed from asset wallet
      auto assetFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt);

      //create spenders
      uint64_t total = 0;
      for (auto& utxo : unspentVec) {
         total += utxo.getValue();
         signer2.addSpender(getSpenderPtr(utxo));
         signer_inject.addSpender(getSpenderPtr(utxo));
      }

      //creates outputs
      //spend 18 to scrAddrB, use P2PKH
      auto recipient2 = std::make_shared<Recipient_P2PKH>(
         TestChain::scrAddrB.getSliceCopy(1, 20), spendVal);
      signer2.addRecipient(recipient2);
      signer_inject.addRecipient(recipient2);

      if (total > spendVal) {
         //change to addr2, use P2WPKH
         auto changeVal = total - spendVal;
         auto addr2 = assetWlt->getNewAddress(AddressEntryType_P2WPKH);
         signer2.addRecipient(addrVec[2]->getRecipient(changeVal));
         signer_inject.addRecipient(addrVec[2]->getRecipient(changeVal));
      }

      //grab the unsigned tx and get the tx hash from it
      BinaryData txHashUnsigned;
      {
         auto unsignedTxRaw = signer2.serializeUnsignedTx();
         Tx unsignedTx(unsignedTxRaw);
         txHashUnsigned = unsignedTx.getThisHash();
      }

      auto hashFromSigner = signer2.getTxId();
      EXPECT_EQ(txHashUnsigned, hashFromSigner);

      //sign & verify
      {
         auto lock = assetWlt->lockDecryptedContainer();
         signer2.setFeed(assetFeed);
         signer2.sign();
      }
      EXPECT_TRUE(signer2.verify());

      DBTestUtils::ZcVector zcVec2;
      auto signedTxRaw = signer2.serializeSignedTx();

      //extract sigs from tx 
      std::vector<SecureBinaryData> sigs;
      {
         Tx signedTx(signedTxRaw);
         for (unsigned i=0; i<signedTx.getNumTxIn(); i++) {
            auto witnessStart = signedTx.getWitnessOffset(i);
            auto witnessEnd = signedTx.getWitnessOffset(i+1);

            BinaryDataRef witnessDataRef(
               signedTxRaw.getPtr() + witnessStart,
               witnessEnd - witnessStart);
            BinaryRefReader brrWit(witnessDataRef);

            auto count = brrWit.get_var_int();
            for (unsigned y=0; y<count; y++) {
               auto len = brrWit.get_var_int();
               auto data = brrWit.get_BinaryDataRef(len);

               if (data.getSize() > 68 &&
                  data.getPtr()[0] == 0x30 &&
                  data.getPtr()[2] == 0x02) {
                  sigs.push_back(data);
               }
            }
         }
      }
      ASSERT_EQ(sigs.size(), 2ULL);

      //try to inject into unresolved signer, should fail
      for (unsigned i=0; i<sigs.size(); i++) {
         try {
            signer_inject.injectSignature(i, sigs[i]);
            EXPECT_TRUE(false);
         } catch (const std::exception&) {}
      }

      //resolve signer
      signer_inject.setFeed(assetFeed);
      signer_inject.resolvePublicData();
      EXPECT_FALSE(signer_inject.verify());
      EXPECT_FALSE(signer_inject.isSigned());

      //inject sigs
      for (unsigned i=0; i<sigs.size(); i++) {
         try {
            signer_inject.injectSignature(i, sigs[i]);
         } catch (const std::exception&) {
            EXPECT_TRUE(false);
         }
      }

      //verify sigs
      EXPECT_TRUE(signer_inject.isSigned());
      EXPECT_TRUE(signer_inject.verify());

      //finally, broadcast
      zcVec2.push_back(signedTxRaw, 15000000);

      Tx signedTx(signedTxRaw);
      EXPECT_EQ(signedTx.getThisHash(), txHashUnsigned);

      DBTestUtils::pushNewZc(theBDMt_, zcVec2);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);
   }

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
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[0]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[1]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 0 * COIN);
   scrObj = dbAssetWlt->getScrAddrObjByKey(addrVec[2]->getPrefixedHash());
   EXPECT_EQ(scrObj->getFullBalance(), 9 * COIN);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(SignerTest, SpendTest_InjectSignature_Multisig)
{
   TestUtils::setBlocks({ "0", "1", "2", "3" }, blk0dat_);

   initBDM();
   clients_->init();
   theBDMt_->start(DBSettings::initMode());
   auto bdvID = DBTestUtils::registerBDV(
      clients_, BitcoinSettings::getMagicBytes());

   std::vector<BinaryData> scrAddrVec {
      TestChain::scrAddrA,
      TestChain::scrAddrB,
      TestChain::scrAddrC,
      TestChain::scrAddrD,
      TestChain::scrAddrE
   };

   //// create 3 assetWlt ////
   IO::CreationParams params{
      homedir_, {}, 1ms, {}, 1ms, 3
   };

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed1(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_1 = AssetWallet_Single::createFromSeed(
      move(seed1), params);

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed2(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_2 = AssetWallet_Single::createFromSeed(
      move(seed2), params);

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed3(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt_3 = AssetWallet_Single::createFromSeed(
      move(seed3), params);

   //create 2-of-3 multisig asset entry from 3 different wallets
   std::map<BinaryData, std::shared_ptr<AssetEntry>> asset_single_map;
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

   auto ae_ms = std::make_shared<AssetEntry_Multisig>(
      AssetId::getRootAssetId(),
      asset_single_map, 2, 3);
   auto addr_ms_raw = std::make_shared<AddressEntry_Multisig>(ae_ms, true);
   auto addr_p2wsh = std::make_shared<AddressEntry_P2WSH>(addr_ms_raw);

   //register with db
   std::vector<BinaryData> addrVec;
   addrVec.push_back(addr_p2wsh->getPrefixedHash());

   std::vector<BinaryData> addrVec_singleSig;
   auto addrSet = assetWlt_2->getAddrHashSet();
   for (auto& addr : addrSet) {
      addrVec_singleSig.push_back(addr);
   }
   DBTestUtils::registerWallet(clients_, bdvID, addrVec, "ms_entry",
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, scrAddrVec, "wallet1",
      false, false);
   DBTestUtils::registerWallet(clients_, bdvID, addrVec_singleSig, assetWlt_2->getID(),
      false, false);
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
         signer.addSpender(getSpenderPtr(utxo));
      }

      //spend 20 to nested p2wsh script hash
      signer.addRecipient(addr_p2wsh->getRecipient(20 * COIN));

      //spend 7 to assetWlt_2
      signer.addRecipient(asset4_singlesig->getRecipient(7 * COIN));

      if (total > spendVal) {
         //change to scrAddrD, no fee
         auto changeVal = total - spendVal;
         auto recipientChange = std::make_shared<Recipient_P2PKH>(
            TestChain::scrAddrD.getSliceCopy(1, 20), changeVal);
         signer.addRecipient(recipientChange);
      }

      //sign, verify then broadcast
      signer.setFeed(feed);
      signer.sign();
      EXPECT_TRUE(signer.verify());
      auto zcHash = signer.getTxId();

      DBTestUtils::ZcVector zcVec;
      zcVec.push_back(signer.serializeSignedTx(), 14000000);

      DBTestUtils::pushNewZc(theBDMt_, zcVec);
      DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

      //grab ZC from DB and verify it again
      auto zc_from_db = DBTestUtils::getTxByHash(clients_, bdvID, zcHash);
      auto raw_tx = zc_from_db.serialize();
      auto bctx = BCTX::parse(raw_tx);
      Armory::Signing::TransactionVerifier tx_verifier(*bctx, utxoVec);
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

   //get the zc utxo (ms script)
   auto unspentVec = ms_wlt->getSpendableTxOutListZC();
   ASSERT_EQ(unspentVec.size(), 1ULL);

   auto unspentVec_singleSig = wlt_singleSig->getSpendableTxOutListZC();
   ASSERT_EQ(unspentVec_singleSig.size(), 1ULL);

   unspentVec.insert(unspentVec.end(),
      unspentVec_singleSig.begin(), unspentVec_singleSig.end());

   //create feed from asset wallet 1
   auto feed_ms = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle_ForMultisig>(assetWlt_1);
   auto assetFeed = std::make_shared<ResolverUtils::CustomFeed>(addr_p2wsh, feed_ms);

   //create spenders
   uint64_t total = 0;
   for (auto& utxo : unspentVec) {
      total += utxo.getValue();
      signer2.addSpender(getSpenderPtr(utxo));
   }

   //creates outputs
   //spend 18 to addr 0, use P2PKH
   auto recipient2 = std::make_shared<Recipient_P2PKH>(
      TestChain::scrAddrB.getSliceCopy(1, 20), spendVal);
   signer2.addRecipient(recipient2);

   if (total > spendVal) {
      //deal with change, no fee
      auto changeVal = total - spendVal;
      signer2.addRecipient(addr_p2wsh->getRecipient(changeVal));
   }

   //sign, verify & return signed tx
   Signer signer_inject;
   signer_inject.deserializeState(signer2.serializeState());
   signer2.setFeed(assetFeed);
   signer2.resolvePublicData();
   auto signerState = signer2.evaluateSignedState();

   {
      EXPECT_EQ(signerState.getEvalMapSize(), 2ULL);

      const auto& txinEval = signerState.getSignedStateForInput(0);
      const auto& pubkeyMap = txinEval.getPubKeyMap();
      EXPECT_EQ(pubkeyMap.size(), 3ULL);
      for (auto& pubkeyState : pubkeyMap)
         EXPECT_FALSE(pubkeyState.second);

      const auto& txinEval2 = signerState.getSignedStateForInput(1);
      const auto& pubkeyMap_2 = txinEval2.getPubKeyMap();
      EXPECT_EQ(pubkeyMap_2.size(), 0ULL);
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

      auto asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(asset1);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));
   }

   Signer signer3;
   //create feed from asset wallet 2
   auto feed_ms3 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle_ForMultisig>(assetWlt_2);
   auto assetFeed3 = std::make_shared<ResolverUtils::CustomFeed>(addr_p2wsh, feed_ms3);
   signer3.deserializeState(signer2.serializeState());

   {
      //make sure sig was properly carried over with state
      EXPECT_FALSE(signer3.isSigned());
      signerState = signer3.evaluateSignedState();

      EXPECT_EQ(signerState.getEvalMapSize(), 2U);
      auto txinEval = signerState.getSignedStateForInput(0);
      EXPECT_EQ(txinEval.getSigCount(), 1U);

      auto asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(asset1);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));
   }

   signer3.setFeed(assetFeed3);

   {
      auto lock = assetWlt_2->lockDecryptedContainer();
      signer3.sign();

      signerState = signer3.evaluateSignedState();
      EXPECT_EQ(signerState.getEvalMapSize(), 2U);
      auto&& txinEval = signerState.getSignedStateForInput(0);
      EXPECT_EQ(txinEval.getSigCount(), 2U);

      auto asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(asset2);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));
   }

   {
      auto assetFeed4 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_2);
      signer3.resetFeed();
      signer3.setFeed(assetFeed4);
      auto lock = assetWlt_2->lockDecryptedContainer();
      signer3.sign();
   }

   ASSERT_TRUE(signer3.isSigned());
   EXPECT_TRUE(signer3.verify());

   {
      //should have 2 sigs now
      EXPECT_TRUE(signer3.isSigned());
      signerState = signer3.evaluateSignedState();

      EXPECT_EQ(signerState.getEvalMapSize(), 2U);
      auto txinEval = signerState.getSignedStateForInput(0);
      EXPECT_EQ(txinEval.getSigCount(), 2U);

      auto asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(asset1);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));

      asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(asset2);
      ASSERT_NE(asset_single, nullptr);
      ASSERT_TRUE(txinEval.isSignedForPubKey(asset_single->getPubKey()->getCompressedKey()));
   }

   //extract sigs from tx
   auto tx1 = signer3.serializeSignedTx();
   std::vector<SecureBinaryData> sigs;
   {
      Tx signedTx(tx1);
      for (unsigned i=0; i<signedTx.getNumTxIn(); i++) {
         auto witnessStart = signedTx.getWitnessOffset(i);
         auto witnessEnd = signedTx.getWitnessOffset(i+1);

         BinaryDataRef witnessDataRef(
            tx1.getPtr() + witnessStart,
            witnessEnd - witnessStart);
         BinaryRefReader brrWit(witnessDataRef);

         auto count = brrWit.get_var_int();
         for (unsigned y=0; y<count; y++) {
            auto len = brrWit.get_var_int();
            auto data = brrWit.get_BinaryDataRef(len);

            if (data.getSize() > 68 &&
               data.getPtr()[0] == 0x30 &&
               data.getPtr()[2] == 0x02) {
               sigs.push_back(data);
            }
         }
      }

      for (unsigned i=0; i<signedTx.getNumTxIn(); i++) {
         auto txInCopy = signedTx.getTxInCopy(i);
         auto script = txInCopy.getScript();
         auto scriptItems = BtcUtils::splitPushOnlyScriptRefs(script);
         for (auto& item : scriptItems) {
            if (item.getSize() > 68 &&
               item.getPtr()[0] == 0x30 &&
               item.getPtr()[2] == 0x02) {
               sigs.push_back(item);
               break;
            }
         }
      }
      ASSERT_EQ(sigs.size(), 3ULL);
   }

   //resolve spender
   {
      signer_inject.setFeed(assetFeed);
      signer_inject.resolvePublicData();
      EXPECT_FALSE(signer_inject.isResolved());
      EXPECT_FALSE(signer_inject.isSigned());
      EXPECT_FALSE(signer_inject.verify());

      signer_inject.resetFeed();
      auto assetFeed5 = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(assetWlt_2);
      signer_inject.setFeed(assetFeed5);
      signer_inject.resolvePublicData();
      EXPECT_TRUE(signer_inject.isResolved());
      EXPECT_FALSE(signer_inject.isSigned());
      EXPECT_FALSE(signer_inject.verify());
   }

   //inject sigs & verify
   {
      //ms sigs
      signer_inject.injectSignature(0, sigs[0], 0);
      signer_inject.injectSignature(0, sigs[1], 1);

      //single sig for second input
      signer_inject.injectSignature(1, sigs[2]);

      //verify
      EXPECT_TRUE(signer_inject.isResolved());
      EXPECT_TRUE(signer_inject.isSigned());
      EXPECT_TRUE(signer_inject.verify());
   }

   auto zcHash = signer3.getTxId();
   EXPECT_EQ(zcHash, signer_inject.getTxId());

   //broadcast the last one
   DBTestUtils::ZcVector zcVec;
   zcVec.push_back(tx1, 15000000);

   DBTestUtils::pushNewZc(theBDMt_, zcVec);
   DBTestUtils::waitOnNewZcSignal(clients_, bdvID);

   //grab ZC from DB and verify it again
   auto zc_from_db = DBTestUtils::getTxByHash(clients_, bdvID, zcHash);
   auto raw_tx = zc_from_db.serialize();
   auto bctx = BCTX::parse(raw_tx);
   Armory::Signing::TransactionVerifier tx_verifier(*bctx, unspentVec);
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
class ExtrasTest : public ::testing::Test
{
protected:

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      FileUtils::removeDirectory(homedir_);
      std::filesystem::create_directory(homedir_);

      DBSettings::setServiceType(SERVICE_UNITTEST);
      Armory::Config::parseArgs({
         "--offline",
         "--testnet",
         "--datadir=./fakehomedir",
         "--satoshi-datadir=./blkfiletest" },
         Armory::Config::ProcessType::DB);

      wallet1id = "wallet1";
      wallet2id = "wallet2";
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      FileUtils::removeDirectory(homedir_);
      Armory::Config::reset();
      CLEANUP_ALL_TIMERS();
   }

   std::filesystem::path homedir_{"./fakehomedir"sv};
   std::string wallet1id;
   std::string wallet2id;
};

#if 0
////////////////////////////////////////////////////////////////////////////////
TEST_F(ExtrasTest, Serialization)
{
   //resolver
   auto feed = make_shared<ResolverUtils::TestResolverFeed>();

   //create some private keys
   unsigned keyCount = 11;
   std::vector<SecureBinaryData> privKeys;
   for (unsigned i=0; i<keyCount; i++) {
      //generate the key
      privKeys.emplace_back(CryptoPRNG::generateRandom(32));

      //populate the feed
      feed->addPrivKey(privKeys.back(), true);
   }

   //compute the pubekys
   std::vector<SecureBinaryData> pubKeys;
   for (auto& privKey : privKeys) {
      pubKeys.emplace_back(CryptoECDSA().ComputePublicKey(privKey, true));
   }

   //create recipients
   std::vector<BinaryData> hashes;
   std::vector<std::shared_ptr<ScriptRecipient>> recipients;
   std::vector<UTXO> utxos;

   //P2WPKH
   for (unsigned i=0; i<6; i++) {
      const auto& pubKey = pubKeys[i];

      hashes.emplace_back(BtcUtils::getHash160(pubKey));
      recipients.emplace_back(make_shared<Recipient_P2WPKH>(hashes.back(), COIN));

      UTXO utxo;
      utxo.unserializeRaw(recipients.back()->getSerializedScript());
      utxo.txHash_ = CryptoPRNG::generateRandom(32);
      utxo.txOutIndex_ = 0;
      utxos.emplace_back(move(utxo));
   }

   //Nested P2WPKH
   {
      const auto& pubKey = pubKeys[6];

      hashes.emplace_back(BtcUtils::getHash160(pubKey));
      auto script = BtcUtils::getP2WPKHOutputScript(hashes.back());
      hashes.emplace_back(BtcUtils::getHash160(script));
      recipients.emplace_back(make_shared<Recipient_P2SH>(hashes.back(), 2 * COIN));

      feed->addValPair(hashes.back(), script);

      UTXO utxo;
      utxo.unserializeRaw(recipients.back()->getSerializedScript());
      utxo.txHash_ = CryptoPRNG::generateRandom(32);
      utxo.txOutIndex_ = 0;
      utxos.emplace_back(move(utxo));
   }

   //P2PKH
   {
      const auto& pubKey = pubKeys[7];

      hashes.emplace_back(BtcUtils::getHash160(pubKey));
      recipients.emplace_back(make_shared<Recipient_P2PKH>(hashes.back(), 3 * COIN));

      UTXO utxo;
      utxo.unserializeRaw(recipients.back()->getSerializedScript());
      utxo.txHash_ = CryptoPRNG::generateRandom(32);
      utxo.txOutIndex_ = 0;
      utxos.emplace_back(move(utxo));
   }

   //Nested P2PK
   {
      const auto& pubKey = pubKeys[8];
      auto script = BtcUtils::getP2PKScript(pubKey);
      hashes.emplace_back(BtcUtils::getHash160(script));
      recipients.emplace_back(make_shared<Recipient_P2SH>(hashes.back(), 10 * COIN));

      feed->addValPair(hashes.back(), script);

      UTXO utxo;
      utxo.unserializeRaw(recipients.back()->getSerializedScript());
      utxo.txHash_ = CryptoPRNG::generateRandom(32);
      utxo.txOutIndex_ = 0;
      utxos.emplace_back(move(utxo));
   }

   //P2WSH multisig
   {
      const auto& pubKey1 = pubKeys[9];
      const auto& pubKey2 = pubKeys[10];

      //create ms script
      BinaryWriter msWriter;
      msWriter.put_uint8_t(OP_1);
      
      msWriter.put_uint8_t(33);
      msWriter.put_BinaryData(pubKey1);

      msWriter.put_uint8_t(33);
      msWriter.put_BinaryData(pubKey2);

      msWriter.put_uint8_t(OP_2);
      msWriter.put_uint8_t(OP_CHECKMULTISIG);

      //hash it
      auto msScript = msWriter.getDataRef();
      auto msHash = BtcUtils::getSha256(msScript);

      hashes.emplace_back(msHash);
      recipients.emplace_back(make_shared<Recipient_P2WSH>(hashes.back(), 5 * COIN));

      UTXO utxo;
      utxo.unserializeRaw(recipients.back()->getSerializedScript());
      utxo.txHash_ = CryptoPRNG::generateRandom(32);
      utxo.txOutIndex_ = 0;
      utxos.emplace_back(move(utxo));
   }


   /*
   Demonstrate the good case, with spender resolution and state restore
   at deserialization time.

   Note: we're not attacking the protobuf serialization, that's 
   covered by protobuf itself. We're attacking the data carried by the 
   protobuf message directly, i.e. this is a valid SignerState message, 
   but it carries corrupt Signer data.
   */
   Signer signer1;
   signer1.setFeed(feed);

   for (unsigned i=0; i<3; i++) {
      signer1.addSpender(make_shared<ScriptSpender>(utxos[i]));
   }

   for (unsigned i=3; i<6; i++) {
      signer1.addRecipient(recipients[i]);
   }

   signer1.resolvePublicData();
   EXPECT_TRUE(signer1.isResolved());
   EXPECT_FALSE(signer1.isSigned());
   EXPECT_FALSE(signer1.verify());

   auto serState = signer1.serializeState();
   Signer signer2;
   signer2.deserializeState(serState);

   EXPECT_TRUE(signer2.isResolved());
   EXPECT_FALSE(signer2.isSigned());
   EXPECT_FALSE(signer2.verify());

   /*attack spender header*/

   //version
   {
      class BadSpender_Header_Version : public ScriptSpender
      {
      private:
         const unsigned int counter_;

      protected:
         void serializeStateHeader(const BinaryData& protoMsg) const override
         {
            if (counter_ == 0) {
               protoMsg.set_version_max(10);
            } else {
               protoMsg.set_version_max(SCRIPT_SPENDER_VERSION_MAX);
            }

            if (counter_ == 1) {
               protoMsg.set_version_min(20);
            } else {
               protoMsg.set_version_min(SCRIPT_SPENDER_VERSION_MIN);
            }

            protoMsg.set_legacy_status((uint8_t)SpenderStatus::Empty);
            protoMsg.set_segwit_status((uint8_t)SpenderStatus::Resolved);

            protoMsg.set_sighash_type((uint8_t)SIGHASH_ALL);
            protoMsg.set_sequence(UINT32_MAX);

            protoMsg.set_is_p2sh(false);
            protoMsg.set_is_csv(false);
            protoMsg.set_is_cltv(false);
         }

      public:
         BadSpender_Header_Version(const UTXO& utxo, unsigned counter) :
            ScriptSpender(utxo), counter_(counter)
         {}
      };

      //max version, first spender
      {
         Signer signer3;
         signer3.setFeed(feed);

         //this spender will serialize with a corrupt header
         signer3.addSpender(make_shared<BadSpender_Header_Version>(utxos[0], 0));

         //regular spenders
         for (unsigned i=1; i<3; i++) {
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));
         }

         //regular recipients
         for (unsigned i=3; i<6; i++) {
            signer3.addRecipient(recipients[i]);
         }

         signer3.resolvePublicData();
         auto serState2 = signer3.serializeState();

         try {
            Signer signer4(serState2);
            ASSERT_TRUE(false);
         } catch (const SignerDeserializationError& e) {
            EXPECT_EQ(e.what(), string("serialized spender version mismatch"));
         }
      }

      //min version, last spender
      {
         Signer signer3;
         signer3.setFeed(feed);

         //regular spenders
         for (unsigned i=0; i<2; i++) {
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));
         }

         //this spender will serialize with a corrupt header
         signer3.addSpender(make_shared<BadSpender_Header_Version>(utxos[2], 1));

         //regular recipients
         for (unsigned i=3; i<6; i++) {
            signer3.addRecipient(recipients[i]);
         }

         signer3.resolvePublicData();
         auto serState2 = signer3.serializeState();

         try {
            Signer signer4(serState2);
            ASSERT_TRUE(false);
         } catch (const SignerDeserializationError& e) {
            EXPECT_EQ(e.what(), string("serialized spender version mismatch"));
         }
      }
   }

   //resolved status
   {
      class BadSpender_Header_Status : public ScriptSpender
      {
      private:
         const unsigned counter_ = 0;

      protected:
         void serializeStateHeader(
            Codec_SignerState::ScriptSpenderState& protoMsg) const override
         {
            protoMsg.set_version_max(SCRIPT_SPENDER_VERSION_MAX);
            protoMsg.set_version_min(SCRIPT_SPENDER_VERSION_MIN);

            if (counter_ == 0)
               protoMsg.set_legacy_status((uint8_t)30);
            else 
               protoMsg.set_legacy_status((uint8_t)SpenderStatus::Empty);

            if (counter_ == 1)
               protoMsg.set_segwit_status((uint8_t)SpenderStatus::Signed);
            else
               protoMsg.set_segwit_status((uint8_t)SpenderStatus::Resolved);

            protoMsg.set_sighash_type((uint8_t)SIGHASH_ALL);
            protoMsg.set_sequence(UINT32_MAX);

            protoMsg.set_is_p2sh(false);
            protoMsg.set_is_csv(false);
            protoMsg.set_is_cltv(false);
         }

      public:
         BadSpender_Header_Status(const UTXO& utxo, unsigned counter) :
            ScriptSpender(utxo), counter_(counter)
         {}
      };

      //bogus legacy status, first spender
      {
         Signer signer3;
         signer3.setFeed(feed);

         //this spender will serialize with a corrupt header
         signer3.addSpender(make_shared<BadSpender_Header_Status>(utxos[0], 0));
         
         //regular spenders
         for (unsigned i=1; i<3; i++)
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));  

         //regular recipients
         for (unsigned i=3; i<6; i++)
            signer3.addRecipient(recipients[i]);

         signer3.resolvePublicData();
         auto serState2 = signer3.serializeState();

         try
         {
            Signer signer4(serState2);
            ASSERT_TRUE(false);
         }
         catch (const SignerDeserializationError& e)
         {
            EXPECT_EQ(e.what(), string("unserialized spender has inconsistent state"));
         }
      }

      //segwit status as signed, last spender
      {
         Signer signer3;
         signer3.setFeed(feed);

         //regular spenders
         for (unsigned i=0; i<2; i++)
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));  

         //this spender will serialize with a corrupt header
         signer3.addSpender(make_shared<BadSpender_Header_Status>(utxos[2], 1));
         
         //regular recipients
         for (unsigned i=3; i<6; i++)
            signer3.addRecipient(recipients[i]);

         signer3.resolvePublicData();
         auto serState2 = signer3.serializeState();

         try
         {
            Signer signer4(serState2);
            ASSERT_TRUE(false);
         }
         catch (const SignerDeserializationError& e)
         {
            EXPECT_EQ(e.what(), string("unserialized spender has inconsistent state"));
         }
      }
   }

   /*attack utxo/outpoint*/
   {
      class BadSpender_Utxo : public ScriptSpender
      {
      private:
         const unsigned counter_ = 0;

      protected:
         void serializeStateUtxo(
            Codec_SignerState::ScriptSpenderState& protoMsg) const override
         {
            switch (counter_)
            {
            case 0:
            {
               //utxo script size mismatch
               auto utxoProto = protoMsg.mutable_utxo();
               utxoProto->set_value(COIN);
               BinaryWriter bw;
               bw.put_var_int(50);
               bw.put_BinaryData(CryptoPRNG::generateRandom(27));
               auto script = bw.getDataRef();
               utxoProto->set_script(script.getPtr(), script.getSize());

               utxoProto->set_txheight(utxo_.txHeight_);
               utxoProto->set_txindex(utxo_.txIndex_);
               utxoProto->set_txoutindex(utxo_.txOutIndex_);
               utxoProto->set_txhash(utxo_.txHash_.getPtr(), utxo_.txHash_.getSize());

               break;
            }

            case 1:
            {
               //utxo script size mismatch, size as 3 bytes varint
               auto utxoProto = protoMsg.mutable_utxo();
               utxoProto->set_value(COIN);
               BinaryWriter bw;
               bw.put_var_int(10000);
               bw.put_BinaryData(CryptoPRNG::generateRandom(100));
               auto script = bw.getDataRef();
               utxoProto->set_script(script.getPtr(), script.getSize());

               utxoProto->set_txheight(utxo_.txHeight_);
               utxoProto->set_txindex(utxo_.txIndex_);
               utxoProto->set_txoutindex(utxo_.txOutIndex_);
               utxoProto->set_txhash(utxo_.txHash_.getPtr(), utxo_.txHash_.getSize());

               break;
            }

            case 2:
            {
               //utxo hash isn't 32 bytes
               auto utxoProto = protoMsg.mutable_utxo();
               utxoProto->set_value(COIN);
               utxoProto->set_script(utxo_.script_.getPtr(), utxo_.script_.getSize());

               utxoProto->set_txheight(utxo_.txHeight_);
               utxoProto->set_txindex(utxo_.txIndex_);
               utxoProto->set_txoutindex(utxo_.txOutIndex_);
               auto invalid_hash = CryptoPRNG::generateRandom(15);
               utxoProto->set_txhash(invalid_hash.getPtr(), invalid_hash.getSize());

               break;
            }
            
            case 3:
            {
               //repeat outpoint hash & id, different value
               auto utxoProto = protoMsg.mutable_utxo();
               auto fake_val = CryptoPRNG::generateRandom(8);
               auto val_int = (uint64_t*)fake_val.getPtr();
               utxoProto->set_value(*val_int);
               utxoProto->set_script(utxo_.script_.getPtr(), utxo_.script_.getSize());

               utxoProto->set_txheight(utxo_.txHeight_);
               utxoProto->set_txindex(utxo_.txIndex_);
               utxoProto->set_txoutindex(utxo_.txOutIndex_);
               utxoProto->set_txhash(utxo_.txHash_.getPtr(), utxo_.txHash_.getSize());

               break;
            }
            
            case 4:
            {
               //outpoint hash isn't 32 bytes
               auto invalid_hash = CryptoPRNG::generateRandom(18);
               auto outpointProto = protoMsg.mutable_outpoint();
               outpointProto->set_txhash(invalid_hash.getPtr(), invalid_hash.getSize());
               outpointProto->set_txoutindex(utxo_.txOutIndex_);
               outpointProto->set_value(COIN);
               outpointProto->set_isspent(false);
               
               break;
            }
            
            default:
               throw runtime_error("invalid counter");
            }
         }

      public:
         BadSpender_Utxo(const vector<UTXO>& utxos, unsigned counter) :
            ScriptSpender(utxos[counter]), counter_(counter)
         {}
      };

      //bogus utxo script, first spender
      {
         Signer signer3;
         signer3.setFeed(feed);

         //this spender will serialize with a corrupt utxo script
         signer3.addSpender(make_shared<BadSpender_Utxo>(utxos, 0));
         
         //regular spenders
         for (unsigned i=1; i<3; i++)
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));  

         //regular recipients
         for (unsigned i=3; i<6; i++)
            signer3.addRecipient(recipients[i]);

         signer3.resolvePublicData();
         auto serState2 = signer3.serializeState();

         try
         {
            Signer signer4(serState2);
            ASSERT_TRUE(false);
         }
         catch (const SignerDeserializationError& e)
         {
            EXPECT_EQ(e.what(), string("unserialized spender has inconsistent state"));
         }
      }

      //bogus utxo script, last spender
      {
         Signer signer3;
         signer3.setFeed(feed);

         //regular spenders
         for (unsigned i=0; i<3; i++)
         {
            if (i == 1)
               continue;
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));  
         }

         //this spender will serialize with a corrupt utxo script
         signer3.addSpender(make_shared<BadSpender_Utxo>(utxos, 1));
         
         //regular recipients
         for (unsigned i=3; i<6; i++)
            signer3.addRecipient(recipients[i]);

         signer3.resolvePublicData();
         auto serState2 = signer3.serializeState();

         try
         {
            Signer signer4(serState2);
            ASSERT_TRUE(false);
         }
         catch (const SignerDeserializationError& e)
         {
            EXPECT_EQ(e.what(), string("unserialized spender has inconsistent state"));
         }
      }

      //bogus utxo hash, first spender
      {
         Signer signer3;
         signer3.setFeed(feed);

         //this spender will serialize with a corrupt utxo hash
         signer3.addSpender(make_shared<BadSpender_Utxo>(utxos, 2));
         
         //regular spenders
         for (unsigned i=0; i<3; i++)
         {
            if (i==2)
               continue;
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));  
         }

         //regular recipients
         for (unsigned i=3; i<6; i++)
            signer3.addRecipient(recipients[i]);

         signer3.resolvePublicData();
         auto serState2 = signer3.serializeState();

         try
         {
            Signer signer4(serState2);
            ASSERT_TRUE(false);
         }
         catch (const runtime_error& e)
         {
            EXPECT_EQ(e.what(), string("invalid utxo hash size"));
         }
      }      

      //bogus utxo hash, first spender
      {
         Signer signer3;
         signer3.setFeed(feed);

         //this spender will serialize with an invalid outpoint
         signer3.addSpender(make_shared<BadSpender_Utxo>(utxos, 4));
         
         //regular spenders
         for (unsigned i=0; i<2; i++)
         {
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));  
         }

         //regular recipients
         for (unsigned i=3; i<6; i++)
            signer3.addRecipient(recipients[i]);

         signer3.resolvePublicData();
         auto serState2 = signer3.serializeState();

         try
         {
            Signer signer4(serState2);
            ASSERT_TRUE(false);
         }
         catch (const SignerDeserializationError& e)
         {
            EXPECT_EQ(e.what(), string("invalid outpoint hash"));
         }
      }
   }

   /*attack resolution stack*/

   //legacy script
   {
      class BadSpender_LegacyPubkey : public ScriptSpender
      {
         const unsigned counter_;
         BinaryData goodSigScript_;

      protected:
         void serializeLegacyState(
            Codec_SignerState::ScriptSpenderState& protoMsg) const override
         {          
            switch (counter_)
            {
            case 0:
            {
               //overshoot pubkey size header
               BinaryRefReader brr(goodSigScript_);

               //skip sig
               auto len = brr.get_var_int();
               brr.advance(len);
               auto pos = brr.getPosition();

               //corrupt the pubkey size header
               auto ptr = (uint8_t*)goodSigScript_.getPtr();
               if (ptr[pos] != 33)
                  throw runtime_error("invalid pubkey size in good sigscript");
               ptr[pos] = 51;

               protoMsg.set_sig_script(goodSigScript_.getPtr(), goodSigScript_.getSize());
               break;
            }

            case 1:
            {
               //undershoot pubkey size header
               BinaryRefReader brr(goodSigScript_);

               //skip sig
               auto len = brr.get_var_int();
               brr.advance(len);
               auto pos = brr.getPosition();

               //corrupt the pubkey size header
               auto ptr = (uint8_t*)goodSigScript_.getPtr();
               if (ptr[pos] != 33)
                  throw runtime_error("invalid pubkey size in good sigscript");
               ptr[pos] = 20;

               protoMsg.set_sig_script(goodSigScript_.getPtr(), goodSigScript_.getSize());
               break;
            }

            case 2:
            {
               //overshoot sig size header
               auto ptr = (uint8_t*)goodSigScript_.getPtr();
               ptr[0] = 91;

               protoMsg.set_sig_script(goodSigScript_.getPtr(), goodSigScript_.getSize());
               break;
            }

            case 3:
            {
               //undershoot sig size header
               auto ptr = (uint8_t*)goodSigScript_.getPtr();
               ptr[0] = 31;

               protoMsg.set_sig_script(goodSigScript_.getPtr(), goodSigScript_.getSize());
               break;
            }

            case 4:
            {
               //undershoot R size header
               auto ptr = (uint8_t*)goodSigScript_.getPtr();
               ptr[4] = 10;

               protoMsg.set_sig_script(goodSigScript_.getPtr(), goodSigScript_.getSize());
               break;
            }

            case 5:
            {
               //overshoot S size header
               auto ptr = (uint8_t*)goodSigScript_.getPtr();
               ptr[4] = 58;

               protoMsg.set_sig_script(goodSigScript_.getPtr(), goodSigScript_.getSize());
               break;
            }

            case 6:
            {
               //corrupt R int flag
               auto ptr = (uint8_t*)goodSigScript_.getPtr();
               ptr[3] = 60;

               protoMsg.set_sig_script(goodSigScript_.getPtr(), goodSigScript_.getSize());
               break;
            }

            case 7:
            {
               protoMsg.set_sig_script(goodSigScript_.getPtr(), goodSigScript_.getSize() - 10);
               break;
            }

            case 8:
            {
               //corrupt the p2pk preimage
               BinaryRefReader brr(goodSigScript_);

               //skip sig
               auto len = brr.get_var_int();
               brr.advance(len + 5);
               auto pos = brr.getPosition();

               //corrupt the p2pk preimage
               auto ptr = (uint8_t*)goodSigScript_.getPtr();
               ptr[pos] = 50;
               ptr[pos +1] = 50;
               ptr[pos +2] = 50;

               protoMsg.set_sig_script(goodSigScript_.getPtr(), goodSigScript_.getSize());
               break;
            }

            default:
               throw runtime_error("invalid counter");
            }
         }

      public:
         BadSpender_LegacyPubkey(const UTXO& utxo, unsigned counter) :
            ScriptSpender(utxo), counter_(counter)
         {}

         BinaryData& getGoodSigScriptReference(void)
         {
            return goodSigScript_;
         }
      };

      //p2pkh sigscript, pubkey size headar overshoot
      {
         Signer signer3;
         signer3.setFeed(feed);

         //this spender will serialize with a corrupt sigscript
         auto badSpender = make_shared<BadSpender_LegacyPubkey>(utxos[7], 0);
         auto& goodSigScript = badSpender->getGoodSigScriptReference();
         signer3.addSpender(badSpender);
         
         //regular spenders
         for (unsigned i=1; i<3; i++)
         {
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));  
         }

         //regular recipients
         for (unsigned i=3; i<6; i++)
            signer3.addRecipient(recipients[i]);

         signer3.sign();

         EXPECT_TRUE(signer3.isResolved());
         EXPECT_TRUE(signer3.isSigned());
         EXPECT_TRUE(signer3.verify());

         {
            //get good sig
            auto rawTx = signer3.serializeSignedTx();
            Tx tx(rawTx);
            auto txinCopy = tx.getTxInCopy(0);
            goodSigScript = txinCopy.getScript();
         }

         auto serState2 = signer3.serializeState();
         try
         {
            Signer signer4(serState2);
            ASSERT_TRUE(false);
         }
         catch (const runtime_error& e)
         {
            EXPECT_EQ(e.what(), string("unserialized spender has inconsistent state"));
         }
      }      

      //p2pkh sigscript, pubkey size header undershoot
      {
         Signer signer3;
         signer3.setFeed(feed);

         //this spender will serialize with a corrupt sigscript
         auto badSpender = make_shared<BadSpender_LegacyPubkey>(utxos[7], 1);
         auto& goodSigScript = badSpender->getGoodSigScriptReference();
         signer3.addSpender(badSpender);
         
         //regular spenders
         for (unsigned i=1; i<3; i++)
         {
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));  
         }

         //regular recipients
         for (unsigned i=3; i<6; i++)
            signer3.addRecipient(recipients[i]);

         signer3.sign();

         EXPECT_TRUE(signer3.isResolved());
         EXPECT_TRUE(signer3.isSigned());
         EXPECT_TRUE(signer3.verify());

         {
            //get good sig
            auto rawTx = signer3.serializeSignedTx();
            Tx tx(rawTx);
            auto txinCopy = tx.getTxInCopy(0);
            goodSigScript = txinCopy.getScript();
         }

         auto serState2 = signer3.serializeState();
         try
         {
            Signer signer4(serState2);
            ASSERT_TRUE(false);
         }
         catch (const runtime_error& e)
         {
            EXPECT_EQ(e.what(), string("unserialized spender has inconsistent state"));
         }
      }      

      //p2pkh sigscript, sig size header overshoot
      {
         Signer signer3;
         signer3.setFeed(feed);

         //this spender will serialize with a corrupt sigscript
         auto badSpender = make_shared<BadSpender_LegacyPubkey>(utxos[7], 2);
         auto& goodSigScript = badSpender->getGoodSigScriptReference();
         signer3.addSpender(badSpender);
         
         //regular spenders
         for (unsigned i=1; i<3; i++)
         {
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));  
         }

         //regular recipients
         for (unsigned i=3; i<6; i++)
            signer3.addRecipient(recipients[i]);

         signer3.sign();

         EXPECT_TRUE(signer3.isResolved());
         EXPECT_TRUE(signer3.isSigned());
         EXPECT_TRUE(signer3.verify());

         {
            //get good sig
            auto rawTx = signer3.serializeSignedTx();
            Tx tx(rawTx);
            auto txinCopy = tx.getTxInCopy(0);
            goodSigScript = txinCopy.getScript();
         }

         auto serState2 = signer3.serializeState();
         try
         {
            Signer signer4(serState2);
            ASSERT_TRUE(false);
         }
         catch (const runtime_error& e)
         {
            EXPECT_EQ(e.what(), string("unserialized spender has inconsistent state"));
         }
      }      

      //p2pkh sigscript, sig size header undershoot
      {
         Signer signer3;
         signer3.setFeed(feed);

         //this spender will serialize with a corrupt sigscript
         auto badSpender = make_shared<BadSpender_LegacyPubkey>(utxos[7], 3);
         auto& goodSigScript = badSpender->getGoodSigScriptReference();
         signer3.addSpender(badSpender);
         
         //regular spenders
         for (unsigned i=1; i<3; i++)
         {
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));  
         }

         //regular recipients
         for (unsigned i=3; i<6; i++)
            signer3.addRecipient(recipients[i]);

         signer3.sign();

         EXPECT_TRUE(signer3.isResolved());
         EXPECT_TRUE(signer3.isSigned());
         EXPECT_TRUE(signer3.verify());

         {
            //get good sig
            auto rawTx = signer3.serializeSignedTx();
            Tx tx(rawTx);
            auto txinCopy = tx.getTxInCopy(0);
            goodSigScript = txinCopy.getScript();
         }

         auto serState2 = signer3.serializeState();
         try
         {
            Signer signer4(serState2);
            ASSERT_TRUE(false);
         }
         catch (const runtime_error& e)
         {
            EXPECT_EQ(e.what(), string("unserialized spender has inconsistent state"));
         }
      }      

      //p2pkh sigscript, sig R size header undershoot
      {
         Signer signer3;
         signer3.setFeed(feed);

         //this spender will serialize with a corrupt sigscript
         auto badSpender = make_shared<BadSpender_LegacyPubkey>(utxos[7], 4);
         auto& goodSigScript = badSpender->getGoodSigScriptReference();
         signer3.addSpender(badSpender);
         
         //regular spenders
         for (unsigned i=1; i<3; i++)
         {
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));  
         }

         //regular recipients
         for (unsigned i=3; i<6; i++)
            signer3.addRecipient(recipients[i]);

         signer3.sign();

         EXPECT_TRUE(signer3.isResolved());
         EXPECT_TRUE(signer3.isSigned());
         EXPECT_TRUE(signer3.verify());

         {
            //get good sig
            auto rawTx = signer3.serializeSignedTx();
            Tx tx(rawTx);
            auto txinCopy = tx.getTxInCopy(0);
            goodSigScript = txinCopy.getScript();
         }

         auto serState2 = signer3.serializeState();
         try
         {
            Signer signer4(serState2);
            EXPECT_TRUE(signer4.isResolved());
            EXPECT_TRUE(signer4.isSigned());
            EXPECT_FALSE(signer4.verify());
         }
         catch (const runtime_error& e)
         {
            ASSERT_TRUE(false);
         }
      }      

      //p2pkh sigscript, sig S size header overshoot
      {
         Signer signer3;
         signer3.setFeed(feed);

         //this spender will serialize with a corrupt sigscript
         auto badSpender = make_shared<BadSpender_LegacyPubkey>(utxos[7], 5);
         auto& goodSigScript = badSpender->getGoodSigScriptReference();
         signer3.addSpender(badSpender);
         
         //regular spenders
         for (unsigned i=1; i<3; i++)
         {
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));  
         }

         //regular recipients
         for (unsigned i=3; i<6; i++)
            signer3.addRecipient(recipients[i]);

         signer3.sign();

         EXPECT_TRUE(signer3.isResolved());
         EXPECT_TRUE(signer3.isSigned());
         EXPECT_TRUE(signer3.verify());

         {
            //get good sig
            auto rawTx = signer3.serializeSignedTx();
            Tx tx(rawTx);
            auto txinCopy = tx.getTxInCopy(0);
            goodSigScript = txinCopy.getScript();
         }

         auto serState2 = signer3.serializeState();
         try
         {
            Signer signer4(serState2);
            EXPECT_TRUE(signer4.isResolved());
            EXPECT_TRUE(signer4.isSigned());
            EXPECT_FALSE(signer4.verify());
         }
         catch (const runtime_error& e)
         {
            ASSERT_TRUE(false);
         }
      }      

      //p2pkh sigscript, sig S size header overshoot
      {
         Signer signer3;
         signer3.setFeed(feed);

         //this spender will serialize with a corrupt sigscript
         auto badSpender = make_shared<BadSpender_LegacyPubkey>(utxos[7], 6);
         auto& goodSigScript = badSpender->getGoodSigScriptReference();
         signer3.addSpender(badSpender);
         
         //regular spenders
         for (unsigned i=1; i<3; i++)
         {
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));  
         }

         //regular recipients
         for (unsigned i=3; i<6; i++)
            signer3.addRecipient(recipients[i]);

         signer3.sign();

         EXPECT_TRUE(signer3.isResolved());
         EXPECT_TRUE(signer3.isSigned());
         EXPECT_TRUE(signer3.verify());

         {
            //get good sig
            auto rawTx = signer3.serializeSignedTx();
            Tx tx(rawTx);
            auto txinCopy = tx.getTxInCopy(0);
            goodSigScript = txinCopy.getScript();
         }

         auto serState2 = signer3.serializeState();
         try
         {
            Signer signer4(serState2);
            ASSERT_TRUE(false);
         }
         catch (const runtime_error& e)
         {
            EXPECT_EQ(e.what(), string("unserialized spender has inconsistent state"));
         }
      }  

      //nested p2pk, undershoot preimage
      {
         Signer signer3;
         signer3.setFeed(feed);

         //this spender will serialize with a corrupt sigscript
         auto badSpender = make_shared<BadSpender_LegacyPubkey>(utxos[8], 7);
         auto& goodSigScript = badSpender->getGoodSigScriptReference();
         signer3.addSpender(badSpender);
         
         //regular spenders
         for (unsigned i=1; i<3; i++)
         {
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));  
         }

         //regular recipients
         for (unsigned i=3; i<6; i++)
            signer3.addRecipient(recipients[i]);

         signer3.sign();

         EXPECT_TRUE(signer3.isResolved());
         EXPECT_TRUE(signer3.isSigned());
         EXPECT_TRUE(signer3.verify());

         {
            //get good sig
            auto rawTx = signer3.serializeSignedTx();
            Tx tx(rawTx);
            auto txinCopy = tx.getTxInCopy(0);
            goodSigScript = txinCopy.getScript();
         }

         auto serState2 = signer3.serializeState();
         try
         {
            Signer signer4(serState2);
            ASSERT_TRUE(false);
         }
         catch (const runtime_error& e)
         {
            EXPECT_EQ(e.what(), string("unserialized spender has inconsistent state"));
         }
      }

      //nested p2pk, corrupt preimage
      {
         Signer signer3;
         signer3.setFeed(feed);

         //this spender will serialize with a corrupt sigscript
         auto badSpender = make_shared<BadSpender_LegacyPubkey>(utxos[8], 8);
         auto& goodSigScript = badSpender->getGoodSigScriptReference();
         signer3.addSpender(badSpender);
         
         //regular spenders
         for (unsigned i=1; i<3; i++)
         {
            signer3.addSpender(make_shared<ScriptSpender>(utxos[i]));  
         }

         //regular recipients
         for (unsigned i=3; i<6; i++)
            signer3.addRecipient(recipients[i]);

         signer3.sign();

         EXPECT_TRUE(signer3.isResolved());
         EXPECT_TRUE(signer3.isSigned());
         EXPECT_TRUE(signer3.verify());

         {
            //get good sig
            auto rawTx = signer3.serializeSignedTx();
            Tx tx(rawTx);
            auto txinCopy = tx.getTxInCopy(0);
            goodSigScript = txinCopy.getScript();
         }

         auto serState2 = signer3.serializeState();
         try
         {
            Signer signer4(serState2);
            ASSERT_TRUE(false);
         }
         catch (const runtime_error& e)
         {
            EXPECT_EQ(e.what(), string("unserialized spender has inconsistent state"));
         }
      }
   }

   //legacy stack
   {

   }

   //witness data
   {
      //p2wsh multisig, attack 1 sig

      //nested p2wpkh, preimage
   }

   //witness stack
   {}

   /*recipients*/

   //recipient script size headers

   //recipient value mismatch

   //recipient count

   //recipient ordering
}
#endif

////////////////////////////////////////////////////////////////////////////////
TEST_F(ExtrasTest, PSBT)
{
   //
   auto getUtxoFromRawTx = [](const BinaryDataRef rawTx, unsigned index)->UTXO
   {
      Tx tx(rawTx);

      auto hash = tx.getThisHash();
      auto txOut = tx.getTxOutCopy(index);

      UTXO utxo(
         txOut.getValue(),
         UINT32_MAX, UINT32_MAX, index,
         hash, txOut.getScript());

      return utxo;
   };

   //
   auto createSigner = []()->Signer
   {
      Signer signer;
      signer.setVersion(2);

      {
         //read hash hexits
         auto&& hash = READHEX("75ddabb27b8845f5247975c8a5ba7c6f336c4570708ebe230caf6db5217ae858");

         //flip endianess
         BinaryData hashBE(32);
         auto hashPtr = hash.getPtr();
         auto hashBEPtr = (uint8_t*)hashBE.getPtr();
         for (unsigned i=0; i<32; i++)
            hashBEPtr[i] = hashPtr[31-i];

         //create spender
         signer.addSpender(std::make_shared<ScriptSpender>(hashBE, 0));
      }

      {
         //read hash hexits
         auto&& hash = READHEX("1dea7cd05979072a3578cab271c02244ea8a090bbb46aa680a65ecd027048d83");

         //flip endianess
         BinaryData hashBE(32);
         auto hashPtr = hash.getPtr();
         auto hashBEPtr = (uint8_t*)hashBE.getPtr();
         for (unsigned i=0; i<32; i++)
            hashBEPtr[i] = hashPtr[31-i];

         //create spender
         signer.addSpender(std::make_shared<ScriptSpender>(hashBE, 1));
      }

      {
         auto hash = READHEX("d85c2b71d0060b09c9886aeb815e50991dda124d");
         signer.addRecipient(std::make_shared<Recipient_P2WPKH>(hash, 149990000));
      }

      {
         auto hash = READHEX("00aea9a2e5f0f876a588df5546e8742d1d87008f");
         signer.addRecipient(std::make_shared<Recipient_P2WPKH>(hash, 100000000));
      }

      return signer;
   };

   //
   //BitcoinSettings::selectNetwork(NETWORK_MODE_TESTNET);
   auto b58seed = SecureBinaryData::fromString(
      "tprv8ZgxMBicQKsPd9TeAdPADNnSyH9SSUUbTVeFszDE23Ki6TBB5nCefAdHkK8Fm3qMQR6sHwA56zqRmKmxnHk37JkiFzvncDqoKmPWubu7hDF");

   //create a wallet from that seed to test bip32 on the fly derivation
   IO::CreationParams params{
      homedir_, {}, 1ms, {}, 1ms, 0
   };
   auto wallet = AssetWallet_Single::createFromSeed(
      Armory::Seeds::ClearTextSeed_BIP32::fromBase58(b58seed),
      params);

   //create node
   BIP32_Node node;
   node.initFromBase58(b58seed);
   auto masterFingerprint = node.getThisFingerprint();

   // 0'/0'
   node.derivePrivate(0x80000000);
   node.derivePrivate(0x80000000);

   //generate assets
   unsigned keyCount = 6;
   std::vector<SecureBinaryData> privKeys;
   std::vector<BinaryData> pubKeys;
   for (unsigned i=0; i<keyCount; i++)
   {
      auto nodeCopy = node;
      unsigned derStep = i ^ 0x80000000;
      nodeCopy.derivePrivate(derStep);
      privKeys.emplace_back(nodeCopy.movePrivateKey());
      pubKeys.emplace_back(
         CryptoECDSA().ComputePublicKey(privKeys.back(), true));
   }

   auto supportingTx1 = READHEX("0200000000010158e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd7501000000171600145f275f436b09a8cc9a2eb2a2f528485c68a56323feffffff02d8231f1b0100000017a914aed962d6654f9a2b36608eb9d64d2b260db4f1118700c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e88702483045022100a22edcc6e5bc511af4cc4ae0de0fcd75c7e04d8c1c3a8aa9d820ed4b967384ec02200642963597b9b1bc22c75e9f3e117284a962188bf5e8a74c895089046a20ad770121035509a48eb623e10aace8bfd0212fdb8a8e5af3c94b0b133b95e114cab89e4f7965000000");
   auto supportingTx2 = READHEX("0200000001aad73931018bd25f84ae400b68848be09db706eac2ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d481c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b6393e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a270100000017a91429ca74f8a08f81999428185c97b5d852e4063f618765000000");
   auto utxo1_1 = getUtxoFromRawTx(supportingTx1, 1);
   
   //setup
   {
      auto signer = createSigner();

      //convert to PSBT & check vs test values
      auto psbt = signer.toPSBT();
      auto psbtTestVal = READHEX(
         "70736274ff01009a020000000258e87a21b56daf0c23be8e7070456c336f7cba"
         "a5c8757924f545887bb2abdd750000000000ffffffff838d0427d0ec650a68aa"
         "46bb0b098aea4422c071b2ca78352a077959d07cea1d0100000000ffffffff02"
         "70aaf00800000000160014d85c2b71d0060b09c9886aeb815e50991dda124d00"
         "e1f5050000000016001400aea9a2e5f0f876a588df5546e8742d1d87008f0000"
         "00000000000000");
      EXPECT_EQ(psbt, psbtTestVal);

      auto signer2 = Signer::fromPSBT(psbtTestVal);
      EXPECT_EQ(psbtTestVal, signer2.toPSBT());

      Signer signer3;
      signer3.deserializeState(signer.serializeState());
      EXPECT_EQ(psbtTestVal, signer3.toPSBT());
   }

   //resolve scripts
   BinaryData resolvedPSBT;
   {
      auto signer = createSigner();
      resolvedPSBT = READHEX(
         "70736274ff01009a020000000258e87a21b56daf0c23be8e7070456c336f7cba"
         "a5c8757924f545887bb2abdd750000000000ffffffff838d0427d0ec650a68aa"
         "46bb0b098aea4422c071b2ca78352a077959d07cea1d0100000000ffffffff02"
         "70aaf00800000000160014d85c2b71d0060b09c9886aeb815e50991dda124d00"
         "e1f5050000000016001400aea9a2e5f0f876a588df5546e8742d1d87008f0000"
         "0000000100bb0200000001aad73931018bd25f84ae400b68848be09db706eac2"
         "ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d4"
         "81c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b63"
         "93e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa"
         "020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a"
         "270100000017a91429ca74f8a08f81999428185c97b5d852e4063f6187650000"
         "00010304010000000104475221029583bf39ae0a609747ad199addd634fa6108"
         "559d6c5cd39b4c2183f1ab96e07f2102dab61ff49a14db6a7d02b0cd1fbb78fc"
         "4b18312b5b4e54dae4dba2fbfef536d752ae2206029583bf39ae0a609747ad19"
         "9addd634fa6108559d6c5cd39b4c2183f1ab96e07f10d90c6a4f000000800000"
         "008000000080220602dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54"
         "dae4dba2fbfef536d710d90c6a4f0000008000000080010000800001012000c2"
         "eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e8870103"
         "040100000001042200208c2353173743b595dfb4a07b72ba8e42e3797da74e87"
         "fe7d9d7497e3b2028903010547522103089dc10c7ac6db54f91329af617333db"
         "388cead0c231f723379d1b99030b02dc21023add904f3d6dcf59ddb906b0dee2"
         "3529b7ffb9ed50e5e86151926860221f0e7352ae2206023add904f3d6dcf59dd"
         "b906b0dee23529b7ffb9ed50e5e86151926860221f0e7310d90c6a4f00000080"
         "0000008003000080220603089dc10c7ac6db54f91329af617333db388cead0c2"
         "31f723379d1b99030b02dc10d90c6a4f00000080000000800200008000220203"
         "a9a4c37f5996d3aa25dbac6b570af0650394492942460b354753ed9eeca58771"
         "10d90c6a4f000000800000008004000080002202027f6399757d2eff55a136ad"
         "02c684b1838b6556e5f1b6b34282a94b6b5005109610d90c6a4f000000800000"
         "00800500008000");

      //setup feed
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      for (unsigned i=0; i<pubKeys.size(); i++) {
         feed->setBip32PathForPubkey(pubKeys[i], BIP32_AssetPath(
            pubKeys[i],
            {0x80000000, 0x80000000, i ^ 0x80000000},
            masterFingerprint, nullptr));

         auto hash = BtcUtils::getHash160(pubKeys[i]);
         feed->addValPair(hash, pubKeys[i]);
      }
      
      {
         //p2sh multisig input
         auto msScript = READHEX("5221029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f2102dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d752ae");
         auto hash = BtcUtils::getHash160(msScript);
         feed->addValPair(hash, msScript);
      }

      {
         //p2sh-p2wsh multisig input
         auto msScript = READHEX("522103089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02dc21023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e7352ae");
         auto hash256 = BtcUtils::getSha256(msScript);
         feed->addValPair(hash256, msScript);

         auto p2wshScript = BtcUtils::getP2WSHOutputScript(hash256);
         auto hash160 = BtcUtils::getHash160(p2wshScript);
         feed->addValPair(hash160, p2wshScript);
      }

      //set supporting data
      signer.populateUtxo(utxo1_1);
      signer.addSupportingTx(supportingTx2);

      //resolve
      signer.setFeed(feed);
      signer.resolvePublicData();
      auto psbt = signer.toPSBT();
      EXPECT_EQ(psbt, resolvedPSBT);

      auto signer2 = Signer::fromPSBT(resolvedPSBT);
      EXPECT_EQ(resolvedPSBT, signer2.toPSBT());

      Signer signer3;
      signer3.deserializeState(signer.serializeState());
      EXPECT_EQ(resolvedPSBT, signer3.toPSBT());
   }

   //sign first half
   BinaryData psbtHalf1;
   {
      auto signer = createSigner();

      psbtHalf1 = READHEX(
         "70736274ff01009a020000000258e87a21b56daf0c23be8e7070456c336f7cba"
         "a5c8757924f545887bb2abdd750000000000ffffffff838d0427d0ec650a68aa"
         "46bb0b098aea4422c071b2ca78352a077959d07cea1d0100000000ffffffff02"
         "70aaf00800000000160014d85c2b71d0060b09c9886aeb815e50991dda124d00"
         "e1f5050000000016001400aea9a2e5f0f876a588df5546e8742d1d87008f0000"
         "0000000100bb0200000001aad73931018bd25f84ae400b68848be09db706eac2"
         "ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d4"
         "81c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b63"
         "93e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa"
         "020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a"
         "270100000017a91429ca74f8a08f81999428185c97b5d852e4063f6187650000"
         "002202029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1"
         "ab96e07f473044022074018ad4180097b873323c0015720b3684cc8123891048"
         "e7dbcd9b55ad679c99022073d369b740e3eb53dcefa33823c8070514ca55a7dd"
         "9544f157c167913261118c01010304010000000104475221029583bf39ae0a60"
         "9747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f2102dab61ff49a"
         "14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d752ae220602"
         "9583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f"
         "10d90c6a4f000000800000008000000080220602dab61ff49a14db6a7d02b0cd"
         "1fbb78fc4b18312b5b4e54dae4dba2fbfef536d710d90c6a4f00000080000000"
         "80010000800001012000c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db"
         "3535f2b72fa921e887220203089dc10c7ac6db54f91329af617333db388cead0"
         "c231f723379d1b99030b02dc473044022062eb7a556107a7c73f45ac4ab5a1dd"
         "df6f7075fb1275969a7f383efff784bcb202200c05dbb7470dbf2f08557dd356"
         "c7325c1ed30913e996cd3840945db12228da5f01010304010000000104220020"
         "8c2353173743b595dfb4a07b72ba8e42e3797da74e87fe7d9d7497e3b2028903"
         "010547522103089dc10c7ac6db54f91329af617333db388cead0c231f723379d"
         "1b99030b02dc21023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e861"
         "51926860221f0e7352ae2206023add904f3d6dcf59ddb906b0dee23529b7ffb9"
         "ed50e5e86151926860221f0e7310d90c6a4f0000008000000080030000802206"
         "03089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02"
         "dc10d90c6a4f00000080000000800200008000220203a9a4c37f5996d3aa25db"
         "ac6b570af0650394492942460b354753ed9eeca5877110d90c6a4f0000008000"
         "00008004000080002202027f6399757d2eff55a136ad02c684b1838b6556e5f1"
         "b6b34282a94b6b5005109610d90c6a4f00000080000000800500008000");

      //setup feed
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      for (unsigned i=0; i<pubKeys.size(); i++) {
         feed->setBip32PathForPubkey(pubKeys[i],
            BIP32_AssetPath(pubKeys[i],
               {0x80000000, 0x80000000, i ^ 0x80000000},
               masterFingerprint, nullptr));
         auto hash = BtcUtils::getHash160(pubKeys[i]);
         feed->addValPair(hash, pubKeys[i]);
      }

      feed->addPrivKey(privKeys[0], true);
      feed->addPrivKey(privKeys[2], true);

      {
         //p2sh multisig input
         auto msScript = READHEX("5221029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f2102dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d752ae");
         auto hash = BtcUtils::getHash160(msScript);
         feed->addValPair(hash, msScript);
      }

      {
         //p2sh-p2wsh multisig input
         auto msScript = READHEX("522103089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02dc21023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e7352ae");
         auto hash256 = BtcUtils::getSha256(msScript);
         feed->addValPair(hash256, msScript);

         auto p2wshScript = BtcUtils::getP2WSHOutputScript(hash256);
         auto hash160 = BtcUtils::getHash160(p2wshScript);
         feed->addValPair(hash160, p2wshScript);
      }

      signer.populateUtxo(utxo1_1);
      signer.addSupportingTx(supportingTx2);

      //sign
      signer.setFeed(feed);
      signer.sign();
      auto psbt = signer.toPSBT();
      EXPECT_EQ(psbt, psbtHalf1);

      auto signer2 = Signer::fromPSBT(psbtHalf1);
      EXPECT_EQ(psbtHalf1, signer2.toPSBT());

      Signer signer3;
      signer3.deserializeState(signer.serializeState());
      EXPECT_EQ(psbtHalf1, signer3.toPSBT());
   }

   //signer other half
   BinaryData psbtHalf2;
   {
      auto signer = createSigner();
      psbtHalf2 = READHEX("70736274ff01009a020000000258e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd750000000000ffffffff838d0427d0ec650a68aa46bb0b098aea4422c071b2ca78352a077959d07cea1d0100000000ffffffff0270aaf00800000000160014d85c2b71d0060b09c9886aeb815e50991dda124d00e1f5050000000016001400aea9a2e5f0f876a588df5546e8742d1d87008f00000000000100bb0200000001aad73931018bd25f84ae400b68848be09db706eac2ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d481c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b6393e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a270100000017a91429ca74f8a08f81999428185c97b5d852e4063f618765000000220202dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d7483045022100f61038b308dc1da865a34852746f015772934208c6d24454393cd99bdf2217770220056e675a675a6d0a02b85b14e5e29074d8a25a9b5760bea2816f661910a006ea01010304010000000104475221029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f2102dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d752ae2206029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f10d90c6a4f000000800000008000000080220602dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d710d90c6a4f0000008000000080010000800001012000c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e8872202023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e73473044022065f45ba5998b59a27ffe1a7bed016af1f1f90d54b3aa8f7450aa5f56a25103bd02207f724703ad1edb96680b284b56d4ffcb88f7fb759eabbe08aa30f29b851383d2010103040100000001042200208c2353173743b595dfb4a07b72ba8e42e3797da74e87fe7d9d7497e3b2028903010547522103089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02dc21023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e7352ae2206023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e7310d90c6a4f000000800000008003000080220603089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02dc10d90c6a4f00000080000000800200008000220203a9a4c37f5996d3aa25dbac6b570af0650394492942460b354753ed9eeca5877110d90c6a4f000000800000008004000080002202027f6399757d2eff55a136ad02c684b1838b6556e5f1b6b34282a94b6b5005109610d90c6a4f00000080000000800500008000");

      //setup feed
      auto feed = std::make_shared<ResolverUtils::TestResolverFeed>();
      for (unsigned i=0; i<pubKeys.size(); i++) {
         feed->setBip32PathForPubkey(pubKeys[i], BIP32_AssetPath(
            pubKeys[i],
            {0x80000000, 0x80000000, i ^ 0x80000000},
            masterFingerprint, nullptr));

         auto hash = BtcUtils::getHash160(pubKeys[i]);
         feed->addValPair(hash, pubKeys[i]);
      }

      feed->addPrivKey(privKeys[1], true);
      feed->addPrivKey(privKeys[3], true);

      {
         //p2sh multisig input
         auto msScript = READHEX("5221029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f2102dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d752ae");
         auto hash = BtcUtils::getHash160(msScript);
         feed->addValPair(hash, msScript);
      }

      {
         //p2sh-p2wsh multisig input
         auto msScript = READHEX("522103089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02dc21023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e7352ae");
         auto hash256 = BtcUtils::getSha256(msScript);
         feed->addValPair(hash256, msScript);

         auto p2wshScript = BtcUtils::getP2WSHOutputScript(hash256);
         auto hash160 = BtcUtils::getHash160(p2wshScript);
         feed->addValPair(hash160, p2wshScript);
      }

      signer.populateUtxo(utxo1_1);
      signer.addSupportingTx(supportingTx2);

      //sign
      signer.setFeed(feed);
      signer.sign();
      auto psbt = signer.toPSBT();
      EXPECT_EQ(psbt, psbtHalf2);

      auto signer2 = Signer::fromPSBT(psbtHalf2);
      EXPECT_EQ(psbtHalf2, signer2.toPSBT());

      Signer signer3;
      signer3.deserializeState(signer.serializeState());
      EXPECT_EQ(psbtHalf2, signer3.toPSBT());
   }

   //combine sigs & finalize inputs
   {
      auto psbtTestVal = READHEX("70736274ff01009a020000000258e87a21b56daf0c23be8e7070456c336f7cbaa5c8757924f545887bb2abdd750000000000ffffffff838d0427d0ec650a68aa46bb0b098aea4422c071b2ca78352a077959d07cea1d0100000000ffffffff0270aaf00800000000160014d85c2b71d0060b09c9886aeb815e50991dda124d00e1f5050000000016001400aea9a2e5f0f876a588df5546e8742d1d87008f00000000000100bb0200000001aad73931018bd25f84ae400b68848be09db706eac2ac18298babee71ab656f8b0000000048473044022058f6fc7c6a33e1b31548d481c826c015bd30135aad42cd67790dab66d2ad243b02204a1ced2604c6735b6393e5b41691dd78b00f0c5942fb9f751856faa938157dba01feffffff0280f0fa020000000017a9140fb9463421696b82c833af241c78c17ddbde493487d0f20a270100000017a91429ca74f8a08f81999428185c97b5d852e4063f6187650000000107da00473044022074018ad4180097b873323c0015720b3684cc8123891048e7dbcd9b55ad679c99022073d369b740e3eb53dcefa33823c8070514ca55a7dd9544f157c167913261118c01483045022100f61038b308dc1da865a34852746f015772934208c6d24454393cd99bdf2217770220056e675a675a6d0a02b85b14e5e29074d8a25a9b5760bea2816f661910a006ea01475221029583bf39ae0a609747ad199addd634fa6108559d6c5cd39b4c2183f1ab96e07f2102dab61ff49a14db6a7d02b0cd1fbb78fc4b18312b5b4e54dae4dba2fbfef536d752ae0001012000c2eb0b0000000017a914b7f5faf40e3d40a5a459b1db3535f2b72fa921e8870107232200208c2353173743b595dfb4a07b72ba8e42e3797da74e87fe7d9d7497e3b20289030108da0400473044022062eb7a556107a7c73f45ac4ab5a1dddf6f7075fb1275969a7f383efff784bcb202200c05dbb7470dbf2f08557dd356c7325c1ed30913e996cd3840945db12228da5f01473044022065f45ba5998b59a27ffe1a7bed016af1f1f90d54b3aa8f7450aa5f56a25103bd02207f724703ad1edb96680b284b56d4ffcb88f7fb759eabbe08aa30f29b851383d20147522103089dc10c7ac6db54f91329af617333db388cead0c231f723379d1b99030b02dc21023add904f3d6dcf59ddb906b0dee23529b7ffb9ed50e5e86151926860221f0e7352ae00220203a9a4c37f5996d3aa25dbac6b570af0650394492942460b354753ed9eeca5877110d90c6a4f000000800000008004000080002202027f6399757d2eff55a136ad02c684b1838b6556e5f1b6b34282a94b6b5005109610d90c6a4f00000080000000800500008000");
      auto signer = Signer::fromPSBT(psbtHalf1);
      auto signer2 = Signer::fromPSBT(psbtHalf2);

      signer.merge(signer2);
      auto psbt = signer.toPSBT();
      EXPECT_EQ(psbt, psbtTestVal);

      auto signer3 = Signer::fromPSBT(psbtTestVal);
      EXPECT_EQ(psbtTestVal, signer3.toPSBT());

      Signer signer4;
      signer4.deserializeState(signer.serializeState());
      EXPECT_EQ(psbtTestVal, signer4.toPSBT());

      //sign with wallet
      {
         auto signer5 = Signer::fromPSBT(resolvedPSBT);
         auto wltFeed = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(wallet);
         signer5.setFeed(wltFeed);

         auto lock = wallet->lockDecryptedContainer();
         signer5.sign();
         EXPECT_EQ(signer5.toPSBT(), psbtTestVal);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(ExtrasTest, BitcoinMessage)
{
   struct ResolverFeed_SignMessage : public ResolverFeed
   {
      std::map<BinaryData, BinaryData> addrToPubKey;
      std::map<BinaryData, SecureBinaryData> pubKeyToPrivKey;

      BinaryData getByVal(const BinaryData& val) override
      {
         return addrToPubKey[val];
      }

      const SecureBinaryData& getPrivKeyForPubkey(const BinaryData& key) override
      {
         return pubKeyToPrivKey[key];
      }

      void setBip32PathForPubkey(
         const BinaryData&, const BIP32_AssetPath&) override {}

      BIP32_AssetPath resolveBip32PathForPubkey(const BinaryData&) override 
      {
         throw std::runtime_error("nope");
      }
   };

   std::string message{"abcd"};

   //randomized run
   {
      auto privkey = CryptoPRNG::generateRandom(32);
      auto pubkey = CryptoECDSA().ComputePublicKey(privkey, true);
      auto pubkeyCopy = pubkey;

      auto assetPubKey = std::make_shared<Asset_PublicKey>(pubkeyCopy);
      auto assetSingle = std::make_shared<AssetEntry_Single>(
         AssetId(0, 0, -1), assetPubKey, nullptr);
      auto addr = std::make_shared<AddressEntry_P2WPKH>(assetSingle);

      auto resolver = std::make_shared<ResolverFeed_SignMessage>();
      resolver->addrToPubKey.emplace(addr->getHash(), pubkey);
      resolver->pubKeyToPrivKey.emplace(pubkey, privkey);

      auto msgBD = BinaryData::fromString(message);
      auto sig = Signer::signMessage(msgBD, addr->getPrefixedHash(), resolver);
      EXPECT_TRUE(Signer::verifyMessageSignature(
         msgBD, addr->getPrefixedHash(), sig));
   }

   //// check vs static sig
   {
      auto sig = std::string{"IFGmuRxItnOy/Dj26RhwJ1FrHo4gi2jB4JewKqIH0pRxIaiRVCKsyiML9nx34G5MCgfrRD6U21HmJguXBHgWNso="};
      auto privkey = READHEX("e805a7c5b46d4d8458c35a75edbed01b0ed9552761278053f56bf6afad07e1f0");
      auto privkeyB58 = std::string{"cVMiqxWqJpPL1bUnHafgr3XhuTkgZeTjWxmL1csYcaPdA8y1nxhB"};

      auto privKeyDecode = BtcUtils::decodePrivKeyBase58(privkeyB58);
      ASSERT_EQ(privKeyDecode, privkey);

      auto pubkey = CryptoECDSA().ComputePublicKey(privKeyDecode, true);
      auto pubkeyCopy = pubkey;

      auto assetPubKey = std::make_shared<Asset_PublicKey>(pubkeyCopy);
      auto assetSingle = std::make_shared<AssetEntry_Single>(
         AssetId::getRootAssetId(), assetPubKey, nullptr);
      auto addr = std::make_shared<AddressEntry_P2WPKH>(assetSingle);

      auto resolver = std::make_shared<ResolverFeed_SignMessage>();
      resolver->addrToPubKey.emplace(addr->getHash(), pubkey);
      resolver->pubKeyToPrivKey.emplace(pubkey, privKeyDecode);

      auto msgBD = BinaryData::fromString(message);
      auto sigCompute = Signer::signMessage(msgBD, addr->getPrefixedHash(), resolver);
      auto sigDecode = BtcUtils::base64_decode(sig);
      auto sigDecodeBD = BinaryData::fromString(BtcUtils::base64_decode(sig));

      EXPECT_EQ(sigCompute, sigDecodeBD);
      EXPECT_TRUE(Signer::verifyMessageSignature(
         msgBD, addr->getPrefixedHash(), sigDecodeBD));
   }
}

////////////////////////////////////////////////////////////////////////////////
class ExtrasTest_Mainnet : public ::testing::Test
{
protected:

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      FileUtils::removeDirectory(homedir_);
      std::filesystem::create_directory(homedir_);

      DBSettings::setServiceType(SERVICE_UNITTEST);
      Armory::Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir",
         "--satoshi-datadir=./blkfiletest" },
         Armory::Config::ProcessType::DB);

      wallet1id = "wallet1";
      wallet2id = "wallet2";
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      FileUtils::removeDirectory(homedir_);

      Armory::Config::reset();
      CLEANUP_ALL_TIMERS();
   }

   std::filesystem::path homedir_{"./fakehomedir"sv};

   std::string wallet1id;
   std::string wallet2id;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(ExtrasTest_Mainnet, Bip32PathDiscovery)
{
   auto rawEntropy = CryptoPRNG::generateRandom(32);

   BIP32_Node node;
   node.initFromSeed(rawEntropy);
   auto masterFingerprint = node.getThisFingerprint();

   std::vector<uint32_t> derPath = { 0x8000002C, 0x80000000, 0x80000000 };
   for (auto& step : derPath) {
      node.derivePrivate(step);
   }
   node.derivePublic(0);

   std::map<BinaryData, std::vector<uint32_t>> keyAndPath;
   for (unsigned i=0; i<10; i++) {
      auto nodeSoft = node;
      nodeSoft.derivePublic(i);

      std::vector<uint32_t> path = { masterFingerprint };
      path.insert(path.end(), derPath.begin(), derPath.end());
      path.push_back(0);
      path.push_back(i);

      keyAndPath.emplace(nodeSoft.getPublicKey(), path);
   }

   auto passLbd = [](const std::set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData();
   };

   std::filesystem::path wltPath;
   {
      IO::CreationParams params{homedir_, {}, 1ms, {}, 1ms, 10};
      std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
         new Armory::Seeds::ClearTextSeed_BIP32(
            rawEntropy, Armory::Seeds::SeedType::BIP32_Structured));
      auto wallet = AssetWallet_Single::createFromSeed(
         std::move(seed), params);

      wltPath = wallet->getDbFilename();
      auto woWalletPath = wallet->forkWatchingOnly(
         IO::OpenFileParams{wltPath, passLbd});
      auto woWallet = AssetWallet::loadMainWalletFromFile(
         IO::OpenFileParams{woWalletPath, passLbd});
      auto woWalletSingle = std::dynamic_pointer_cast<AssetWallet_Single>(woWallet);

      auto resolver = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(wallet);
      for (auto& keyPathPair : keyAndPath) {
         auto resolvedPath = resolver->resolveBip32PathForPubkey(keyPathPair.first);
         std::vector<unsigned> pathVec;
         pathVec.push_back(resolvedPath.getThisFingerprint());
         pathVec.insert(pathVec.end(),
            resolvedPath.getPath().begin(), resolvedPath.getPath().end());
         EXPECT_EQ(pathVec, keyPathPair.second);
      }

      auto resolverPublic = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(woWalletSingle);
      for (auto& keyPathPair : keyAndPath) {
         auto resolvedPath = resolver->resolveBip32PathForPubkey(keyPathPair.first);
         std::vector<unsigned> pathVec;
         pathVec.push_back(resolvedPath.getThisFingerprint());
         pathVec.insert(pathVec.end(),
            resolvedPath.getPath().begin(), resolvedPath.getPath().end());
         EXPECT_EQ(pathVec, keyPathPair.second);
      }
   }

   //reopen the wallet, check again
   {
      auto loadedWlt = AssetWallet::loadMainWalletFromFile(
         IO::OpenFileParams{wltPath, passLbd});
      auto loadedWltSingle = std::dynamic_pointer_cast<AssetWallet_Single>(loadedWlt);
      auto resolver = std::make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(loadedWltSingle);

      for (auto& keyPathPair : keyAndPath) {
         auto resolvedPath = resolver->resolveBip32PathForPubkey(keyPathPair.first);
         std::vector<unsigned> pathVec;
         pathVec.push_back(resolvedPath.getThisFingerprint());
         pathVec.insert(pathVec.end(),
            resolvedPath.getPath().begin(), resolvedPath.getPath().end());
         EXPECT_EQ(pathVec, keyPathPair.second);
      }
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

   srand(time(0));
   std::cout << "Running main() from gtest_main.cc\n";

   CryptoECDSA::setupContext();

   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();
   CryptoECDSA::shutdown();

   FLUSHLOG();
   CLEANUPLOG();

   return exitCode;
}
