////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                                                                            //
//  Copyright (C) 2017, goatpig                                               //            
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                   
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "TestUtils.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////
class AddressTests : public ::testing::Test
{
protected:
   virtual void SetUp(void)
   {
      NetworkConfig::selectNetwork(NETWORK_MODE_MAINNET);
   }
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(AddressTests, base58_Tests)
{
   BinaryData h_160 = READHEX("00010966776006953d5567439e5e39f86a0d273bee");
   BinaryData scrAddr("16UwLL9Risc3QfPqBUvKofHmBQ7wMtjvM");

   auto&& encoded = BtcUtils::scrAddrToBase58(h_160);
   EXPECT_EQ(encoded, scrAddr);

   auto&& decoded = BtcUtils::base58toScrAddr(scrAddr);
   EXPECT_EQ(decoded, h_160);

   decoded = BtcUtils::base58toScrAddr(encoded);
   EXPECT_EQ(decoded, h_160);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(AddressTests, bech32_Tests)
{
   BinaryData pubkey =
      READHEX("0279BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798");
   BinaryData p2wpkhScrAddr("bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4");
   BinaryData p2wshAddr("bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3");

   auto pubkey_hash = BtcUtils::getHash160(pubkey);
   auto&& scrAddr_p2wpkh = BtcUtils::scrAddrToSegWitAddress(pubkey_hash);
   EXPECT_EQ(p2wpkhScrAddr, scrAddr_p2wpkh);

   BinaryWriter bw;
   bw.put_uint8_t(pubkey.getSize());
   bw.put_BinaryData(pubkey);
   bw.put_uint8_t(OP_CHECKSIG);

   auto&& script_hash = BtcUtils::getSha256(bw.getData());
   auto&& scrAddr_p2wsh = BtcUtils::scrAddrToSegWitAddress(script_hash);
   EXPECT_EQ(p2wshAddr, scrAddr_p2wsh);

   auto&& pubkey_hash2 = BtcUtils::segWitAddressToScrAddr(scrAddr_p2wpkh);
   EXPECT_EQ(pubkey_hash, pubkey_hash2);

   auto&& script_hash2 = BtcUtils::segWitAddressToScrAddr(scrAddr_p2wsh);
   EXPECT_EQ(script_hash, script_hash2);
}


////////////////////////////////////////////////////////////////////////////////
class DerivationTests : public ::testing::Test
{
protected:
   SecureBinaryData seed_ = READHEX("000102030405060708090a0b0c0d0e0f");

protected:
   virtual void SetUp(void)
   {
      NetworkConfig::selectNetwork(NETWORK_MODE_MAINNET);
   }
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(DerivationTests, BIP32_Tests)
{
   //m
   {
      //priv ser & deser
      {
         SecureBinaryData ext_prv(
            "xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stbPy6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk33yuGBxrMPHi");

         //ser
         BIP32_Node serObj;
         serObj.initFromSeed(seed_);
         EXPECT_EQ(serObj.getBase58(), ext_prv);
   
         //deser
         BIP32_Node deserObj;
         deserObj.initFromBase58(ext_prv);
         EXPECT_EQ(deserObj.getDepth(), 0);
         EXPECT_EQ(deserObj.getLeafID(), 0);

         EXPECT_EQ(deserObj.getChaincode().toHexStr(), "873dff81c02f525623fd1fe5167eac3a55a049de3d314bb42ee227ffed37d508");

         auto& privkey = deserObj.getPrivateKey();
         EXPECT_EQ(privkey.toHexStr(), "e8f32e723decf4051aefac8e2c93c9c5b214313817cdb01a1494b917c8436b35");
      }

      //pub ser & deser
      {
         SecureBinaryData ext_pub(
            "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8");

         //deser
         BIP32_Node deserObj;
         deserObj.initFromBase58(ext_pub);
         EXPECT_EQ(deserObj.getDepth(), 0);
         EXPECT_EQ(deserObj.getLeafID(), 0);

         EXPECT_EQ(deserObj.getChaincode().toHexStr(), "873dff81c02f525623fd1fe5167eac3a55a049de3d314bb42ee227ffed37d508");
         EXPECT_EQ(deserObj.getPublicKey().toHexStr(), "0339a36013301597daef41fbe593a02cc513d0b55527ec2df1050e2e8ff49c85c2");
      }
   }
   
   //m/0'
   {
      BIP32_Node serObj;
      serObj.initFromSeed(seed_);
      serObj.derivePrivate(0x80000000);

      //priv ser & deser
      {
         SecureBinaryData ext_prv(
            "xprv9uHRZZhk6KAJC1avXpDAp4MDc3sQKNxDiPvvkX8Br5ngLNv1TxvUxt4cV1rGL5hj6KCesnDYUhd7oWgT11eZG7XnxHrnYeSvkzY7d2bhkJ7");

         //ser
         EXPECT_EQ(serObj.getBase58(), ext_prv);

         //deser
         BIP32_Node deserObj;
         deserObj.initFromBase58(ext_prv);
         EXPECT_EQ(deserObj.getDepth(), 1);
         EXPECT_EQ(deserObj.getLeafID(), 0x80000000);

         EXPECT_EQ(deserObj.getChaincode(), serObj.getChaincode());
         EXPECT_EQ(deserObj.getPrivateKey(), serObj.getPrivateKey());
      }

      //pub ser & deser
      {
         SecureBinaryData ext_pub(
            "xpub68Gmy5EdvgibQVfPdqkBBCHxA5htiqg55crXYuXoQRKfDBFA1WEjWgP6LHhwBZeNK1VTsfTFUHCdrfp1bgwQ9xv5ski8PX9rL2dZXvgGDnw");

         BIP32_Node publicCopy = serObj.getPublicCopy();
         EXPECT_EQ(publicCopy.getBase58(), ext_pub);

         //deser
         BIP32_Node deserObj;
         deserObj.initFromBase58(ext_pub);
         EXPECT_EQ(deserObj.getDepth(), 1);
         EXPECT_EQ(deserObj.getLeafID(), 0x80000000);

         EXPECT_EQ(deserObj.getChaincode(), publicCopy.getChaincode());
         EXPECT_EQ(deserObj.getPublicKey(), publicCopy.getPublicKey());
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(DerivationTests, ArmoryChain_Tests)
{
   SecureBinaryData chaincode = READHEX(
      "0x31302928272625242322212019181716151413121110090807060504030201");
   SecureBinaryData privateKey = READHEX(
      "0x0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a");

   auto&& privkey1 = CryptoECDSA().ComputeChainedPrivateKey(
      privateKey, chaincode);
   auto&& privkey2 = CryptoECDSA().ComputeChainedPrivateKey(
      privkey1, chaincode);
   auto&& privkey3 = CryptoECDSA().ComputeChainedPrivateKey(
      privkey2, chaincode);
   auto&& privkey4 = CryptoECDSA().ComputeChainedPrivateKey(
      privkey3, chaincode);

   EXPECT_EQ(privkey1.toHexStr(), 
      "e2ffa33627c47f042e93425ded75942accaaca09d0a82d9bcf24af4fc6b5bb85");
   EXPECT_EQ(privkey2.toHexStr(), 
      "a2002f9fdfb531e68d1fd3383ec10195b30e77c58877ce4d82795133dfd8dd9e");
   EXPECT_EQ(privkey3.toHexStr(), 
      "03993b61f346be5a60a85bd465153b2c41abe92db4f6267a6577f590a85b8422");
   EXPECT_EQ(privkey4.toHexStr(), 
      "dd39a855e2528898fbb0e8c99c9237c70915c80d690741c0c87f1c6e74b9a8d4");

   auto&& publicKey = CryptoECDSA().ComputePublicKey(privateKey);

   auto&& pubkey1 = CryptoECDSA().ComputeChainedPublicKey(
      publicKey, chaincode);
   auto&& pubkey2 = CryptoECDSA().ComputeChainedPublicKey(
      pubkey1, chaincode);
   auto&& pubkey3 = CryptoECDSA().ComputeChainedPublicKey(
      pubkey2, chaincode);
   auto&& pubkey4 = CryptoECDSA().ComputeChainedPublicKey(
      pubkey3, chaincode);

   EXPECT_EQ(pubkey1.toHexStr(), 
      "045f22b6502501d833413073ace7ca34effcb455953559eb5d39914abcf2e8f64545fd54b4e1ca097d978c74c0bc1cab3d8c3c426dcba345d5d136b5494ae13d71");
   EXPECT_EQ(pubkey2.toHexStr(), 
      "04d0c5b147db60bfb59604871a89da13bc105066032e8d7667f5d631a1ebe04685d72894567aefdbcdac5abaa16f389d9da972882a703c58452c212e66e0e24671");
   EXPECT_EQ(pubkey3.toHexStr(), 
      "04b883039aa4d0c7903ce5ed26596f06af0698f91f804c19be027896fa67d1d14d45f85994cc38077a8bc8e980db41f736e0b1a8e41e34fd0e18dfd970fd7e681b");
   EXPECT_EQ(pubkey4.toHexStr(), 
      "0436e30c6b3295df86d8085d3171bfb11608943c4282a0bf98e841088a14e33cda8412dcf74fb6c8cb89dd00f208ca2c03a437b93730e8d92b45d6841e07ae4e6f");
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class WalletsTest : public ::testing::Test
{
protected:
   string homedir_;

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      LOGDISABLESTDOUT();
      homedir_ = string("./fakehomedir");
      DBUtils::removeDirectory(homedir_);
      mkdir(homedir_);
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      DBUtils::removeDirectory(homedir_);
   }
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, CreateCloseOpen_Test)
{
   map<string, vector<BinaryData>> addrMap;

   //create 3 wallets
   for (unsigned i = 0; i < 3; i++)
   {
      auto&& wltRoot = CryptoPRNG::generateRandom(32);
      auto assetWlt = AssetWallet_Single::createFromPrivateRoot_Armory135(
         homedir_,
         move(wltRoot), //root as a r value
         SecureBinaryData(), //empty passphrase, will use default key
         4); //set lookup computation to 4 entries

      //get AddrVec
      auto&& hashSet = assetWlt->getAddrHashSet();

      auto id = assetWlt->getID();
      auto& vec = addrMap[id];

      vec.insert(vec.end(), hashSet.begin(), hashSet.end());

      //close wallet 
      assetWlt.reset();
   }

   //load all wallets in homedir
   WalletManager wltMgr(homedir_);

   class WalletContainerEx : public WalletContainer
   {
   public:
      shared_ptr<AssetWallet> getWalletPtr(void) const
      {
         return WalletContainer::getWalletPtr();
      }
   };

   for (auto& addrVecPair : addrMap)
   {
      auto wltCtr = (WalletContainerEx*)&wltMgr.getCppWallet(addrVecPair.first);
      auto wltSingle =
         dynamic_pointer_cast<AssetWallet_Single>(wltCtr->getWalletPtr());
      ASSERT_NE(wltSingle, nullptr);

      auto&& hashSet = wltSingle->getAddrHashSet();

      vector<BinaryData> addrVec;
      addrVec.insert(addrVec.end(), hashSet.begin(), hashSet.end());

      ASSERT_EQ(addrVec, addrVecPair.second);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, CreateWOCopy_Test)
{
   //create 1 wallet from priv key
   auto&& wltRoot = CryptoPRNG::generateRandom(32);
   auto assetWlt = AssetWallet_Single::createFromPrivateRoot_Armory135(
      homedir_,
      move(wltRoot), //root as a r value
      SecureBinaryData(),
      4); //set lookup computation to 4 entries

   //get AddrVec
   auto&& hashSet = assetWlt->getAddrHashSet();

   //get pub root and chaincode
   auto pubRoot = assetWlt->getPublicRoot();
   auto chainCode = assetWlt->getArmory135Chaincode();

   //close wallet 
   assetWlt.reset();

   auto woWallet = AssetWallet_Single::createFromPublicRoot_Armory135(
      homedir_,
      pubRoot,
      chainCode,
      4);

   //get AddrVec
   auto&& hashSetWO = woWallet->getAddrHashSet();

   ASSERT_EQ(hashSet, hashSetWO);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, Encryption_Test)
{
   //#1: check deriving from an encrypted root yield correct chain
   //create 1 wallet from priv key
   auto&& wltRoot = CryptoPRNG::generateRandom(32);
   auto assetWlt = AssetWallet_Single::createFromPrivateRoot_Armory135(
      homedir_,
      wltRoot, //root as a r value
      SecureBinaryData(),
      4); //set lookup computation to 4 entries

   //derive private chain from root
   auto&& chaincode = BtcUtils::computeChainCode_Armory135(wltRoot);

   vector<SecureBinaryData> privateKeys;
   auto currentPrivKey = &wltRoot;

   for (int i = 0; i < 4; i++)
   {
      privateKeys.push_back(move(CryptoECDSA().ComputeChainedPrivateKey(
         *currentPrivKey, chaincode)));

      currentPrivKey = &privateKeys.back();
   }

   //compute public keys
   vector<SecureBinaryData> publicKeys;
   for (auto& privkey : privateKeys)
   {
      publicKeys.push_back(move(CryptoECDSA().ComputePublicKey(privkey)));
   }

   //compare with wallet's own
   for (int i = 0; i < 4; i++)
   {
      //grab indexes from 0 to 3
      auto assetptr = assetWlt->getMainAccountAssetForIndex(i);
      ASSERT_EQ(assetptr->getType(), AssetEntryType_Single);

      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(assetptr);
      if (asset_single == nullptr)
         throw runtime_error("unexpected assetptr type");

      auto pubkey_ptr = asset_single->getPubKey();
      ASSERT_EQ(pubkey_ptr->getUncompressedKey(), publicKeys[i]);
   }

   //#2: check no unencrypted private keys are on disk. Incidentally,
   //check public keys are, for sanity

   //close wallet object
   auto filename = assetWlt->getDbFilename();
   assetWlt.reset();

   //parse file for the presence of pubkeys and absence of priv keys
   for (auto& privkey : privateKeys)
   {
      ASSERT_FALSE(TestUtils::searchFile(filename, privkey));
   }

   for (auto& pubkey : publicKeys)
   {
      ASSERT_TRUE(TestUtils::searchFile(filename, pubkey));
   }
}


////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, SeedEncryption)
{
   //create wallet
   vector<unsigned> derPath = {
      0x80000050,
      0x80005421,
      0x80000024,
      785
   };

   SecureBinaryData passphrase("password");

   //create regular wallet
   auto&& seed = CryptoPRNG::generateRandom(32);
   auto wlt = AssetWallet_Single::createFromSeed_BIP32(
      homedir_, seed, derPath, passphrase, 10);

   //check clear text seed does not exist on disk
   auto filename = wlt->getDbFilename();
   ASSERT_FALSE(TestUtils::searchFile(filename, seed));

   //grab without passphrase lbd, should fail
   try
   {
      auto lock = wlt->lockDecryptedContainer();
      auto decryptedSeed = wlt->getDecryptedValue(wlt->getEncryptedSeed());
      EXPECT_EQ(decryptedSeed, seed);
      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException&)
   {
   }

   //set passphrase lambda
   auto passLbd = [&passphrase](const BinaryData&)->SecureBinaryData
   {
      return passphrase;
   };
   wlt->setPassphrasePromptLambda(passLbd);

   //grab without locking, should fail
   try
   {
      auto decryptedSeed = wlt->getDecryptedValue(wlt->getEncryptedSeed());
      EXPECT_EQ(decryptedSeed, seed);
      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException&)
   {
   }

   //lock, grab and check
   try
   {
      auto lock = wlt->lockDecryptedContainer();
      auto decryptedSeed = wlt->getDecryptedValue(wlt->getEncryptedSeed());
      EXPECT_EQ(decryptedSeed, seed);
   }
   catch (DecryptedDataContainerException&)
   {
      ASSERT_TRUE(false);
   }

   //reset passphrase lambda, grab, should fail
   wlt->resetPassphrasePromptLambda();
   try
   {
      auto lock = wlt->lockDecryptedContainer();
      auto decryptedSeed = wlt->getDecryptedValue(wlt->getEncryptedSeed());
      EXPECT_EQ(decryptedSeed, seed);
      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException&)
   {}

   //shutdown wallet
   wlt.reset();

   //create WO
   auto woFilename = AssetWallet::forkWathcingOnly(filename);

   //check it has no seed
   auto wo = AssetWallet::loadMainWalletFromFile(woFilename);
   auto woWlt = dynamic_pointer_cast<AssetWallet_Single>(wo);

   ASSERT_NE(woWlt, nullptr);
   EXPECT_EQ(woWlt->getEncryptedSeed(), nullptr);

   //reload wallet
   ASSERT_EQ(wlt, nullptr);
   auto wltReload = AssetWallet::loadMainWalletFromFile(filename);
   wlt = dynamic_pointer_cast<AssetWallet_Single>(wltReload);
   ASSERT_NE(wlt, nullptr);

   //check seed again
   wlt->setPassphrasePromptLambda(passLbd);
   try
   {
      auto lock = wlt->lockDecryptedContainer();
      auto decryptedSeed = wlt->getDecryptedValue(wlt->getEncryptedSeed());
      EXPECT_EQ(decryptedSeed, seed);
   }
   catch (DecryptedDataContainerException&)
   {
      ASSERT_TRUE(false);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, LockAndExtend_Test)
{
   //create wallet from priv key
   auto&& wltRoot = CryptoPRNG::generateRandom(32);
   auto assetWlt = AssetWallet_Single::createFromPrivateRoot_Armory135(
      homedir_,
      wltRoot, //root as a r value
      SecureBinaryData(), //set passphrase to "test"
      4); //set lookup computation to 4 entries


   //derive private chain from root
   auto&& chaincode = BtcUtils::computeChainCode_Armory135(wltRoot);

   vector<SecureBinaryData> privateKeys;
   auto currentPrivKey = &wltRoot;

   for (int i = 0; i < 10; i++)
   {
      privateKeys.push_back(move(CryptoECDSA().ComputeChainedPrivateKey(
         *currentPrivKey, chaincode)));

      currentPrivKey = &privateKeys.back();
   }

   auto secondthread = [assetWlt, &privateKeys](void)->void
   {
      //lock wallet
      auto secondlock = assetWlt->lockDecryptedContainer();

      //wallet should have 10 assets, last half with only pub keys
      ASSERT_TRUE(assetWlt->getMainAccountAssetCount() == 10);

      //none of the new assets should have private keys
      for (unsigned i = 4; i < 10; i++)
      {
         auto asseti = assetWlt->getMainAccountAssetForIndex(i);
         ASSERT_FALSE(asseti->hasPrivateKey());
      }

      //grab last asset with a priv key
      auto asset3 = assetWlt->getMainAccountAssetForIndex(3);
      auto asset3_single = dynamic_pointer_cast<AssetEntry_Single>(asset3);
      if (asset3_single == nullptr)
         throw runtime_error("unexpected asset entry type");
      auto& privkey3 = assetWlt->getDecryptedValue(asset3_single->getPrivKey());

      //check privkey
      ASSERT_EQ(privkey3, privateKeys[3]);

      //extend private chain to 10 entries
      assetWlt->extendPrivateChainToIndex(assetWlt->getMainAccountID(), 9);

      //there should still be 10 assets
      ASSERT_EQ(assetWlt->getMainAccountAssetCount(), 10);

      //try to grab 10th private key
      auto asset9 = assetWlt->getMainAccountAssetForIndex(9);
      auto asset9_single = dynamic_pointer_cast<AssetEntry_Single>(asset9);
      if (asset9_single == nullptr)
         throw runtime_error("unexpected asset entry type");

      auto& privkey9 = assetWlt->getDecryptedValue(asset9_single->getPrivKey());

      //check priv key
      ASSERT_EQ(privkey9, privateKeys[9]);
   };

   thread t2;

   {
      //grab lock
      auto firstlock = assetWlt->lockDecryptedContainer();

      //start second thread
      t2 = thread(secondthread);

      //sleep for a second
      this_thread::sleep_for(chrono::seconds(1));

      //make sure there are only 4 entries
      ASSERT_EQ(assetWlt->getMainAccountAssetCount(), 4);

      //grab 4th privkey 
      auto asset3 = assetWlt->getMainAccountAssetForIndex(3);
      auto asset3_single = dynamic_pointer_cast<AssetEntry_Single>(asset3);
      if (asset3_single == nullptr)
         throw runtime_error("unexpected asset entry type");
      auto& privkey3 = assetWlt->getDecryptedValue(asset3_single->getPrivKey());

      //check privkey
      ASSERT_EQ(privkey3, privateKeys[3]);

      //extend address chain to 10 entries
      assetWlt->extendPublicChainToIndex(
         assetWlt->getMainAccountID(), 9);

      ASSERT_EQ(assetWlt->getMainAccountAssetCount(), 10);

      //none of the new assets should have private keys
      for (unsigned i = 4; i < 10; i++)
      {
         auto asseti = assetWlt->getMainAccountAssetForIndex(i);
         ASSERT_FALSE(asseti->hasPrivateKey());
      }
   }

   if (t2.joinable())
      t2.join();

   //wallet should be unlocked now
   ASSERT_FALSE(assetWlt->isDecryptedContainerLocked());

   //delete wallet, reload and check private keys are on disk and valid
   auto wltID = assetWlt->getID();
   assetWlt.reset();

   WalletManager wltMgr(homedir_);

   class WalletContainerEx : public WalletContainer
   {
   public:
      shared_ptr<AssetWallet> getWalletPtr(void) const
      {
         return WalletContainer::getWalletPtr();
      }
   };

   auto wltCtr = (WalletContainerEx*)&wltMgr.getCppWallet(wltID);
   auto wltSingle =
      dynamic_pointer_cast<AssetWallet_Single>(wltCtr->getWalletPtr());
   ASSERT_NE(wltSingle, nullptr);
   ASSERT_FALSE(wltSingle->isDecryptedContainerLocked());

   auto lastlock = wltSingle->lockDecryptedContainer();
   for (unsigned i = 0; i < 10; i++)
   {
      auto asseti = wltSingle->getMainAccountAssetForIndex(i);
      auto asseti_single = dynamic_pointer_cast<AssetEntry_Single>(asseti);
      ASSERT_NE(asseti_single, nullptr);

      auto& asseti_privkey = wltSingle->getDecryptedValue(
         asseti_single->getPrivKey());

      ASSERT_EQ(asseti_privkey, privateKeys[i]);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, WrongPassphrase_Test)
{
   //create wallet from priv key
   auto&& wltRoot = CryptoPRNG::generateRandom(32);
   auto assetWlt = AssetWallet_Single::createFromPrivateRoot_Armory135(
      homedir_,
      wltRoot, //root as a r value
      SecureBinaryData("test"), //set passphrase to "test"
      4); //set lookup computation to 4 entries

   unsigned passphraseCount = 0;
   auto badPassphrase = [&passphraseCount](const BinaryData&)->SecureBinaryData
   {
      //pass wrong passphrase once then give up
      if (passphraseCount++ > 1)
         return SecureBinaryData();
      return SecureBinaryData("bad pass");
   };

   //set passphrase lambda
   assetWlt->setPassphrasePromptLambda(badPassphrase);

   //try to decrypt with wrong passphrase
   try
   {
      auto containerLock = assetWlt->lockDecryptedContainer();
      auto asset = assetWlt->getMainAccountAssetForIndex(0);
      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw runtime_error("unexpected asset entry type");

      assetWlt->getDecryptedValue(asset_single->getPrivKey());

      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException&)
   {
      EXPECT_EQ(passphraseCount, 3);
   }

   passphraseCount = 0;
   auto goodPassphrase = [&passphraseCount](const BinaryData&)->SecureBinaryData
   {
      //pass wrong passphrase once then the right one
      if (passphraseCount++ > 1)
         return SecureBinaryData("test");
      return SecureBinaryData("another bad pass");
   };

   assetWlt->setPassphrasePromptLambda(goodPassphrase);

   //try to decrypt with wrong passphrase then right passphrase
   try
   {
      auto&& containerLock = assetWlt->lockDecryptedContainer();
      auto asset = assetWlt->getMainAccountAssetForIndex(0);
      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw runtime_error("unexpected asset entry type");

      auto& privkey = assetWlt->getDecryptedValue(asset_single->getPrivKey());

      //make sure decrypted privkey is valid
      auto&& chaincode = BtcUtils::computeChainCode_Armory135(wltRoot);
      auto&& privkey_ex =
         CryptoECDSA().ComputeChainedPrivateKey(wltRoot, chaincode);

      ASSERT_EQ(privkey, privkey_ex);
   }
   catch (DecryptedDataContainerException&)
   {
      ASSERT_TRUE(false);
   }

   EXPECT_EQ(passphraseCount, 3);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, WrongPassphrase_BIP32_Test)
{
   //create wallet from priv key
   auto&& wltRoot = CryptoPRNG::generateRandom(32);

   vector<unsigned> derPath = 
   {
      0x80000012,
      0x8000a48c,
   };

   auto assetWlt = AssetWallet_Single::createFromSeed_BIP32(
      homedir_,
      wltRoot, //root as a r value
      derPath,
      SecureBinaryData("test"), //set passphrase to "test"
      4); //set lookup computation to 4 entries

   unsigned passphraseCount = 0;
   auto badPassphrase = [&passphraseCount](const BinaryData&)->SecureBinaryData
   {
      //pass wrong passphrase once then give up
      if (passphraseCount++ > 1)
         return SecureBinaryData();
      return SecureBinaryData("bad pass");
   };

   //set passphrase lambda
   assetWlt->setPassphrasePromptLambda(badPassphrase);

   //try to decrypt with wrong passphrase
   try
   {
      auto containerLock = assetWlt->lockDecryptedContainer();
      auto asset = assetWlt->getMainAccountAssetForIndex(0);
      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw runtime_error("unexpected asset entry type");

      assetWlt->getDecryptedValue(asset_single->getPrivKey());

      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException&)
   {
      EXPECT_EQ(passphraseCount, 3);
   }

   passphraseCount = 0;
   auto goodPassphrase = [&passphraseCount](const BinaryData&)->SecureBinaryData
   {
      //pass wrong passphrase once then the right one
      if (passphraseCount++ > 2)
         return SecureBinaryData("test");
      return SecureBinaryData("another bad pass");
   };


   //try to decrypt with wrong passphrase then the right one
   assetWlt->setPassphrasePromptLambda(goodPassphrase);
   try
   {
      auto&& containerLock = assetWlt->lockDecryptedContainer();
      auto asset = assetWlt->getMainAccountAssetForIndex(0);
      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw runtime_error("unexpected asset entry type");

      auto& privkey = assetWlt->getDecryptedValue(asset_single->getPrivKey());

      //make sure decrypted privkey is valid
      BIP32_Node node;
      node.initFromSeed(wltRoot);

      for (auto& der : derPath)
         node.derivePrivate(der);
      node.derivePrivate(0);
      node.derivePrivate(0);

      ASSERT_EQ(privkey, node.getPrivateKey());
   }
   catch (DecryptedDataContainerException&)
   {
      ASSERT_TRUE(false);
   }

   EXPECT_EQ(passphraseCount, 4);

   //add another account
   vector<unsigned> derPath2 =
   {
      0x800050aa,
      0x8000c103,
   };

   auto newAccId = assetWlt->createBIP32Account(nullptr, derPath2, false);
   auto accPtr = assetWlt->getAccountForID(newAccId);
   ASSERT_NE(accPtr, nullptr);

   //try and grab priv key with wrong passphrase
   passphraseCount = 0;
   assetWlt->setPassphrasePromptLambda(badPassphrase);

   try
   {
      auto containerLock = assetWlt->lockDecryptedContainer();
      auto asset = accPtr->getOutterAssetForIndex(5);
      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw runtime_error("unexpected asset entry type");

      assetWlt->getDecryptedValue(asset_single->getPrivKey());

      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException&)
   {
      EXPECT_EQ(passphraseCount, 3);
   }

   //try to decrypt with wrong passphrase then the right one
   passphraseCount = 0;
   assetWlt->setPassphrasePromptLambda(goodPassphrase);
   try
   {
      auto&& containerLock = assetWlt->lockDecryptedContainer();
      auto asset = accPtr->getOutterAssetForIndex(5);
      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw runtime_error("unexpected asset entry type");

      auto& privkey = assetWlt->getDecryptedValue(asset_single->getPrivKey());

      //make sure decrypted privkey is valid
      BIP32_Node node;
      node.initFromSeed(wltRoot);

      for (auto& der : derPath2)
         node.derivePrivate(der);
      node.derivePrivate(0);
      node.derivePrivate(5);

      ASSERT_EQ(privkey, node.getPrivateKey());
   }
   catch (DecryptedDataContainerException&)
   {
      ASSERT_TRUE(false);
   }

   EXPECT_EQ(passphraseCount, 4);
}


////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, ChangePassphrase_Test)
{
   //create wallet from priv key
   auto&& wltRoot = CryptoPRNG::generateRandom(32);
   auto assetWlt = AssetWallet_Single::createFromPrivateRoot_Armory135(
      homedir_,
      wltRoot, //root as a r value
      SecureBinaryData("test"), //set passphrase to "test"
      4); //set lookup computation to 4 entries

   auto&& chaincode = BtcUtils::computeChainCode_Armory135(wltRoot);
   auto&& privkey_ex =
      CryptoECDSA().ComputeChainedPrivateKey(wltRoot, chaincode);
   auto filename = assetWlt->getDbFilename();


   //grab all IVs and encrypted private keys
   vector<SecureBinaryData> ivVec;
   vector<SecureBinaryData> privateKeys;
   struct DecryptedDataContainerEx : public DecryptedDataContainer
   {
      const SecureBinaryData& getMasterKeyIV(void) const
      {
         auto keyIter = encryptionKeyMap_.begin();
         return keyIter->second->getIV();
      }

      const SecureBinaryData& getMasterEncryptionKey(void) const
      {
         auto keyIter = encryptionKeyMap_.begin();
         return keyIter->second->getCipherText();
      }
   };

   struct AssetWalletEx : public AssetWallet_Single
   {
      shared_ptr<DecryptedDataContainer> getDecryptedDataContainer(void) const
      {
         return decryptedData_;
      }
   };

   {
      auto assetWltEx = (AssetWalletEx*)assetWlt.get();
      auto decryptedDataEx =
         (DecryptedDataContainerEx*)assetWltEx->getDecryptedDataContainer().get();
      ivVec.push_back(decryptedDataEx->getMasterKeyIV());
      privateKeys.push_back(decryptedDataEx->getMasterEncryptionKey());
   }

   for (unsigned i = 0; i < 4; i++)
   {
      auto asseti = assetWlt->getMainAccountAssetForIndex(i);
      auto asseti_single = dynamic_pointer_cast<AssetEntry_Single>(asseti);
      ASSERT_NE(asseti_single, nullptr);

      ivVec.push_back(asseti_single->getPrivKey()->getIV());
      privateKeys.push_back(asseti_single->getPrivKey()->getCipherText());
   }

   //make sure the IVs are unique
   auto ivVecCopy = ivVec;

   while (ivVecCopy.size() > 0)
   {
      auto compare_iv = ivVecCopy.back();
      ivVecCopy.pop_back();

      for (auto& iv : ivVecCopy)
         ASSERT_NE(iv, compare_iv);
   }

   //change passphrase
   SecureBinaryData newPassphrase("new pass");

   unsigned counter = 0;
   auto passphrasePrompt = [&counter](const BinaryData&)->SecureBinaryData
   {
      if (counter++ == 0)
         return SecureBinaryData("test");
      else
         return SecureBinaryData();
   };

   {
      //set passphrase prompt lambda
      assetWlt->setPassphrasePromptLambda(passphrasePrompt);

      //change passphrase
      assetWlt->changeMasterPassphrase(newPassphrase);
   }

   //try to decrypt with new passphrase
   auto newPassphrasePrompt = [&newPassphrase](const BinaryData&)->SecureBinaryData
   {
      return newPassphrase;
   };

   {
      assetWlt->setPassphrasePromptLambda(newPassphrasePrompt);
      auto lock = assetWlt->lockDecryptedContainer();

      auto asset0 = assetWlt->getMainAccountAssetForIndex(0);
      auto asset0_single = dynamic_pointer_cast<AssetEntry_Single>(asset0);
      ASSERT_NE(asset0_single, nullptr);

      auto& decryptedKey =
         assetWlt->getDecryptedValue(asset0_single->getPrivKey());

      ASSERT_EQ(decryptedKey, privkey_ex);
   }

   //close wallet, reload
   auto walletID = assetWlt->getID();
   assetWlt.reset();

   WalletManager wltMgr(homedir_);

   class WalletContainerEx : public WalletContainer
   {
   public:
      shared_ptr<AssetWallet> getWalletPtr(void) const
      {
         return WalletContainer::getWalletPtr();
      }
   };

   auto wltCtr = (WalletContainerEx*)&wltMgr.getCppWallet(walletID);
   auto wltSingle =
      dynamic_pointer_cast<AssetWallet_Single>(wltCtr->getWalletPtr());
   ASSERT_NE(wltSingle, nullptr);
   ASSERT_FALSE(wltSingle->isDecryptedContainerLocked());

   //grab all IVs and private keys again
   vector<SecureBinaryData> newIVs;
   vector<SecureBinaryData> newPrivKeys;

   {
      auto wltSingleEx = (AssetWalletEx*)wltSingle.get();
      auto decryptedDataEx =
         (DecryptedDataContainerEx*)wltSingleEx->getDecryptedDataContainer().get();
      newIVs.push_back(decryptedDataEx->getMasterKeyIV());
      newPrivKeys.push_back(decryptedDataEx->getMasterEncryptionKey());
   }

   for (unsigned i = 0; i < 4; i++)
   {
      auto asseti = wltSingle->getMainAccountAssetForIndex(i);
      auto asseti_single = dynamic_pointer_cast<AssetEntry_Single>(asseti);
      ASSERT_NE(asseti_single, nullptr);

      newIVs.push_back(asseti_single->getPrivKey()->getIV());
      newPrivKeys.push_back(asseti_single->getPrivKey()->getCipherText());
   }

   //check only the master key and iv have changed, and that the new iv does 
   //not match existing ones
   ASSERT_NE(newIVs[0], ivVec[0]);
   ASSERT_NE(newPrivKeys[0], privateKeys[0]);

   for (unsigned i = 1; i < 4; i++)
   {
      ASSERT_EQ(newIVs[i], ivVec[i]);
      ASSERT_EQ(newPrivKeys[i], privateKeys[i]);

      ASSERT_NE(newIVs[0], ivVec[i]);
   }


   {
      //try to decrypt with old passphrase, should fail
      auto lock = wltSingle->lockDecryptedContainer();

      counter = 0;
      wltSingle->setPassphrasePromptLambda(passphrasePrompt);

      auto asset0 = wltSingle->getMainAccountAssetForIndex(0);
      auto asset0_single = dynamic_pointer_cast<AssetEntry_Single>(asset0);
      ASSERT_NE(asset0_single, nullptr);

      try
      {
         auto& decryptedKey =
            wltSingle->getDecryptedValue(asset0_single->getPrivKey());
         ASSERT_FALSE(true);
      }
      catch (...)
      {
      }

      //try to decrypt with new passphrase instead
      wltSingle->setPassphrasePromptLambda(newPassphrasePrompt);
      auto& decryptedKey =
         wltSingle->getDecryptedValue(asset0_single->getPrivKey());

      ASSERT_EQ(decryptedKey, privkey_ex);
   }

   //check old iv and key are not on disk anymore
   ASSERT_FALSE(TestUtils::searchFile(filename, ivVec[0]));
   ASSERT_FALSE(TestUtils::searchFile(filename, privateKeys[0]));

   ASSERT_TRUE(TestUtils::searchFile(filename, newIVs[0]));
   ASSERT_TRUE(TestUtils::searchFile(filename, newPrivKeys[0]));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, BIP32_Chain)
{
   //BIP32 test 1 seed
   SecureBinaryData wltSeed = READHEX("000102030405060708090a0b0c0d0e0f");
   BIP32_Node seedNode;
   seedNode.initFromSeed(wltSeed);
   auto b58 = seedNode.getBase58();

   //0'/1/2'/2
   vector<unsigned> derivationPath = { 0x80000000, 1, 0x80000002 };
   auto assetWlt = AssetWallet_Single::createFromBase58_BIP32(
      homedir_,
      b58, //root as a r value
      derivationPath,
      SecureBinaryData("test"), //set passphrase to "test"
      4); //set lookup computation to 4 entries

   auto passphrasePrompt = [](const BinaryData&)->SecureBinaryData
   {
      return SecureBinaryData("test");
   };

   assetWlt->setPassphrasePromptLambda(passphrasePrompt);
   auto lock = assetWlt->lockDecryptedContainer();

   auto assetPtr = assetWlt->getMainAccountAssetForIndex(2);
   auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(assetPtr);
   ASSERT_NE(assetSingle, nullptr);

   auto& decryptedKey =
      assetWlt->getDecryptedValue(assetSingle->getPrivKey());

   BIP32_Node privNode;
   SecureBinaryData priv_b58("xprvA2JDeKCSNNZky6uBCviVfJSKyQ1mDYahRjijr5idH2WwLsEd4Hsb2Tyh8RfQMuPh7f7RtyzTtdrbdqqsunu5Mm3wDvUAKRHSC34sJ7in334");
   privNode.initFromBase58(priv_b58);

   EXPECT_EQ(decryptedKey, privNode.getPrivateKey());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, BIP32_Public_Chain)
{
   //0'/1/2'
   vector<unsigned> derivationPath = { 0x80000000, 1, 0x80000002 };

   //BIP32 test 1 seed
   SecureBinaryData wltSeed = READHEX("000102030405060708090a0b0c0d0e0f");
   BIP32_Node seedNode;
   seedNode.initFromSeed(wltSeed);
   for (auto& derId : derivationPath)
      seedNode.derivePrivate(derId);

   auto pubSeedNode = seedNode.getPublicCopy();
   auto b58 = pubSeedNode.getBase58();

   //2
   vector<unsigned> derivationPath_Soft = { 2 };
   auto assetWlt = AssetWallet_Single::createFromBase58_BIP32(
      homedir_,
      b58, //root as a r value
      derivationPath_Soft,
      SecureBinaryData(), //set passphrase to "test"
      4); //set lookup computation to 4 entries

   auto accID = assetWlt->getMainAccountID();
   auto assetPtr = assetWlt->getAccountRoot(accID);
   auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(assetPtr);
   ASSERT_NE(assetSingle, nullptr);

   BIP32_Node pubNode;
   SecureBinaryData pub_b58("xpub6FHa3pjLCk84BayeJxFW2SP4XRrFd1JYnxeLeU8EqN3vDfZmbqBqaGJAyiLjTAwm6ZLRQUMv1ZACTj37sR62cfN7fe5JnJ7dh8zL4fiyLHV");
   pubNode.initFromBase58(pub_b58);

   EXPECT_EQ(assetSingle->getPubKey()->getCompressedKey(), pubNode.getPublicKey());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, BIP32_ArmoryDefault)
{
   vector<unsigned> derivationPath = {
   0x80000050,
   0x800005de,
   0x8000465a,
   501};

   auto&& seed = CryptoPRNG::generateRandom(32);

   //create empty wallet
   SecureBinaryData passphrase("password");
   auto assetWlt = AssetWallet_Single::createFromSeed_BIP32(
      homedir_, seed, derivationPath, passphrase, 5);

   auto rootAccId = assetWlt->getMainAccountID();
   auto accRoot = assetWlt->getAccountRoot(rootAccId);
   auto accRootPtr = dynamic_pointer_cast<AssetEntry_BIP32Root>(accRoot);

   BIP32_Node node;
   node.initFromSeed(seed);
   for (auto id : derivationPath)
      node.derivePrivate(id);
   node.derivePrivate(0);

   EXPECT_EQ(accRootPtr->getPubKey()->getCompressedKey(), node.getPublicKey());

   auto accIDs = assetWlt->getAccountIDs();
   BinaryData accID;
   for (auto& id : accIDs)
   {
      if (id != rootAccId)
      {
         accID = id;
         break;
      }
   }

   auto accPtr = assetWlt->getAccountForID(accID);
   auto addrPtr = accPtr->getNewAddress(
      AddressEntryType(AddressEntryType_P2SH | AddressEntryType_P2WPKH));
   auto assetID = assetWlt->getAssetIDForAddr(addrPtr->getPrefixedHash());
   accID.append(WRITE_UINT32_BE(0x10000000));
   accID.append(WRITE_UINT32_BE(0));
   EXPECT_EQ(assetID.first, accID);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, BIP32_Chain_AddAccount)
{
   vector<unsigned> derivationPath1 = {
      0x80000050,
      0x800005de,
      0x8000465a,
      501
   };

   //random seed
   auto&& seed = CryptoPRNG::generateRandom(32);

   //create empty wallet
   SecureBinaryData passphrase("password");
   auto assetWlt = AssetWallet_Single::createFromSeed_BIP32_Blank(
      homedir_, seed, passphrase);

   //this is a hard derivation scenario, the wallet needs to be able to 
   //decrypt its root's private key
   auto passphraseLbd = [&passphrase](const BinaryData&)->SecureBinaryData
   {
      return passphrase;
   };
   assetWlt->setPassphrasePromptLambda(passphraseLbd);

   //add bip32 account for derivationPath1
   auto accountID1 = assetWlt->createBIP32Account(nullptr, derivationPath1);

   //derive bip32 node
   BIP32_Node seedNode;
   seedNode.initFromSeed(seed);
   for (auto& derId : derivationPath1)
      seedNode.derivePrivate(derId);

   auto outerNode = seedNode;
   outerNode.derivePrivate(0);

   {
      //check vs wallet account root
      auto accountRoot = assetWlt->getAccountRoot(accountID1);
      auto accountRoot_BIP32 =
         dynamic_pointer_cast<AssetEntry_BIP32Root>(accountRoot);
      auto& pubkeyAcc = accountRoot_BIP32->getPubKey()->getCompressedKey();
      EXPECT_EQ(pubkeyAcc, outerNode.getPublicKey());

      {
         //check encryption for the added account works

         //try to fetch without locking wallet
         try
         {
            auto& accountPrivKey =
               assetWlt->getDecryptedValue(accountRoot_BIP32->getPrivKey());

            //should not get here
            ASSERT_TRUE(false);
         }
         catch (DecryptedDataContainerException&)
         {
         }

         //now with the lock
         try
         {
            auto lock = assetWlt->lockDecryptedContainer();
            auto& accountPrivKey =
               assetWlt->getDecryptedValue(accountRoot_BIP32->getPrivKey());

            EXPECT_EQ(accountPrivKey, outerNode.getPrivateKey());
         }
         catch (...)
         {
            //should not get here
            ASSERT_TRUE(false);
         }
      }
   }

   //second account
   vector<unsigned> derivationPath2 = {
      0x80000244,
      0x8000be7a,
      0x80002000,
      304
   };

   auto accountTypePtr =
      make_shared<AccountType_BIP32_Custom>();
   accountTypePtr->setAddressTypes({ AddressEntryType_P2WPKH, AddressEntryType_P2PK });
   accountTypePtr->setDefaultAddressType(AddressEntryType_P2WPKH);
   accountTypePtr->setNodes({ 50, 60 });
   accountTypePtr->setOuterAccountID(WRITE_UINT32_BE(50));
   accountTypePtr->setInnerAccountID(WRITE_UINT32_BE(60));
   accountTypePtr->setAddressLookup(100);

   //add bip32 custom account for derivationPath2
   auto accountID2 = assetWlt->createBIP32Account(
      nullptr, derivationPath2, accountTypePtr);

   BIP32_Node seedNode2;
   seedNode2.initFromSeed(seed);
   for (auto& derId : derivationPath2)
      seedNode2.derivePrivate(derId);
   seedNode2.derivePrivate(50);

   {
      //check vs wallet account root
      auto accountRoot = assetWlt->getAccountRoot(accountID2);
      auto accountRoot_BIP32 =
         dynamic_pointer_cast<AssetEntry_BIP32Root>(accountRoot);
      auto& pubkey2 = accountRoot_BIP32->getPubKey()->getCompressedKey();
      EXPECT_EQ(pubkey2, seedNode2.getPublicKey());

      //grab address 32, check vs derivation
      auto accountPtr = assetWlt->getAccountForID(accountID2);
      auto assetPtr = accountPtr->getAssetForID(32, true);

      auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(assetPtr);
      ASSERT_NE(assetSingle, nullptr);

      seedNode2.derivePrivate(32);
      EXPECT_EQ(assetSingle->getPubKey()->getCompressedKey(),
         seedNode2.getPublicKey());
   }

   //close wallet, reload it, check again
   auto filename = assetWlt->getDbFilename();
   assetWlt.reset();

   auto assetWlt2 = AssetWallet::loadMainWalletFromFile(filename);
   auto wltSingle2 = dynamic_pointer_cast<AssetWallet_Single>(assetWlt2);
   ASSERT_NE(wltSingle2, nullptr);

   {
      //check first account
      auto accountRoot = wltSingle2->getAccountRoot(accountID1);
      auto accountRoot_BIP32 =
         dynamic_pointer_cast<AssetEntry_BIP32Root>(accountRoot);
      auto& pubkeyAcc = accountRoot_BIP32->getPubKey()->getCompressedKey();
      EXPECT_EQ(pubkeyAcc, outerNode.getPublicKey());
   }

   {
      //check 2nd account
      auto accountPtr = wltSingle2->getAccountForID(accountID2);
      auto assetPtr = accountPtr->getAssetForID(32, true);

      auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(assetPtr);
      ASSERT_NE(assetSingle, nullptr);
      EXPECT_EQ(assetSingle->getPubKey()->getCompressedKey(),
         seedNode2.getPublicKey());
   }

   //check private keys in both accounts within same decryption lock
   wltSingle2->setPassphrasePromptLambda(passphraseLbd);

   {
      auto lock = wltSingle2->lockDecryptedContainer();

      //check first account
      auto accountRoot = wltSingle2->getAccountRoot(accountID1);
      auto accountRoot_BIP32 =
         dynamic_pointer_cast<AssetEntry_BIP32Root>(accountRoot);
      auto& privKey = wltSingle2->getDecryptedValue(accountRoot_BIP32->getPrivKey());
      EXPECT_EQ(privKey, outerNode.getPrivateKey());

      //check 2nd account
      auto accountPtr = wltSingle2->getAccountForID(accountID2);
      auto assetPtr = accountPtr->getAssetForID(32, true);

      auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(assetPtr);
      ASSERT_NE(assetSingle, nullptr);
      auto& privKey2 = wltSingle2->getDecryptedValue(assetSingle->getPrivKey());
      EXPECT_EQ(privKey2, seedNode2.getPrivateKey());
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, BIP32_Fork_WatchingOnly)
{
   vector<unsigned> derPath = {
      0x80000050,
      0x80005421,
      0x80000024,
      785
   };

   SecureBinaryData passphrase("password");

   //create regular wallet
   auto&& seed = CryptoPRNG::generateRandom(32);
   auto wlt = AssetWallet_Single::createFromSeed_BIP32(
      homedir_, seed, derPath, passphrase, 10);

   //create WO copy
   auto woCopyPath = AssetWallet::forkWathcingOnly(wlt->getDbFilename());
   auto woWlt = AssetWallet::loadMainWalletFromFile(woCopyPath);
   auto woSingle = dynamic_pointer_cast<AssetWallet_Single>(woWlt);

   //check WO roots have no private keys
   {
      EXPECT_TRUE(woSingle->isWatchingOnly());

      auto mainAccountID = woSingle->getMainAccountID();
      auto mainAccount = woSingle->getAccountForID(mainAccountID);
      auto root = mainAccount->getOutterAssetRoot();
      auto rootSingle = dynamic_pointer_cast<AssetEntry_BIP32Root>(root);
      EXPECT_EQ(rootSingle->getPrivKey(), nullptr);
   }

   //compare keys
   for (unsigned i = 0; i < 10; i++)
   {
      auto assetFull = wlt->getMainAccountAssetForIndex(i);
      auto assetFullSingle = dynamic_pointer_cast<AssetEntry_Single>(assetFull);

      auto assetWo = woSingle->getMainAccountAssetForIndex(i);
      auto assetWoSingle = dynamic_pointer_cast<AssetEntry_Single>(assetWo);
      
      //compare keys
      EXPECT_EQ(assetFullSingle->getPubKey()->getCompressedKey(),
         assetWoSingle->getPubKey()->getCompressedKey());

      //check wo wallet has no private key
      EXPECT_FALSE(assetWoSingle->hasPrivateKey());
      EXPECT_EQ(assetWoSingle->getPrivKey(), nullptr);
   }

   //extend chains, check new stuff derives properly
   {
      auto passphraseLBD = [&passphrase](const BinaryData&)->SecureBinaryData
      {
         return passphrase;
      };

      wlt->setPassphrasePromptLambda(passphraseLBD);
      auto lock = wlt->lockDecryptedContainer();
      wlt->extendPrivateChain(10);
   }

   woWlt->extendPublicChain(10);

   //compare keys
   for (unsigned i = 10; i < 20; i++)
   {
      auto assetFull = wlt->getMainAccountAssetForIndex(i);
      auto assetFullSingle = dynamic_pointer_cast<AssetEntry_Single>(assetFull);

      auto assetWo = woSingle->getMainAccountAssetForIndex(i);
      auto assetWoSingle = dynamic_pointer_cast<AssetEntry_Single>(assetWo);

      //compare keys
      EXPECT_EQ(assetFullSingle->getPubKey()->getCompressedKey(),
         assetWoSingle->getPubKey()->getCompressedKey());

      //check wo wallet has no private key
      EXPECT_FALSE(assetWoSingle->hasPrivateKey());
      EXPECT_EQ(assetWoSingle->getPrivKey(), nullptr);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, AddressEntryTypes)
{
   //create wallet
   vector<unsigned> derPath = {
      0x80000050,
      0x80005421,
      0x80000024,
      785
   };

   SecureBinaryData passphrase("password");

   //create regular wallet
   auto&& seed = CryptoPRNG::generateRandom(32);
   auto wlt = AssetWallet_Single::createFromSeed_BIP32(
      homedir_, seed, derPath, passphrase, 10);

   //grab a bunch of addresses of various types
   set<BinaryData> addrHashes;

   //5 default addresses
   for (unsigned i = 0; i < 5; i++)
   {
      auto addrPtr = wlt->getNewAddress();
      addrHashes.insert(addrPtr->getAddress());
   }

   //5 p2wpkh
   for (unsigned i = 0; i < 5; i++)
   {
      auto addrPtr = wlt->getNewAddress(AddressEntryType_P2WPKH);
      addrHashes.insert(addrPtr->getAddress());
   }

   //5 nested p2wpkh change addresses
   for (unsigned i = 0; i < 5; i++)
   {
      auto addrPtr = wlt->getNewChangeAddress(AddressEntryType(
         AddressEntryType_P2SH | AddressEntryType_P2WPKH));
      addrHashes.insert(addrPtr->getAddress());
   }

   //shutdown wallet
   auto filename = wlt->getDbFilename();
   wlt.reset();

   //load from file
   auto loaded = AssetWallet::loadMainWalletFromFile(filename);

   //check used address list from loaded wallet matches grabbed addresses
   {
      auto usedAddressMap = loaded->getUsedAddressMap();
      set<BinaryData> usedAddrHashes;
      for (auto& addrPair : usedAddressMap)
         usedAddrHashes.insert(addrPair.second->getAddress());

      EXPECT_EQ(addrHashes, usedAddrHashes);
   }

   //shutdown wallet
   loaded.reset();

   //create WO copy
   auto woFilename = AssetWallet::forkWathcingOnly(filename);
   auto woLoaded = AssetWallet::loadMainWalletFromFile(woFilename);

   {
      auto usedAddressMap = woLoaded->getUsedAddressMap();
      set<BinaryData> usedAddrHashes;
      for (auto& addrPair : usedAddressMap)
         usedAddrHashes.insert(addrPair.second->getAddress());

      EXPECT_EQ(addrHashes, usedAddrHashes);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, BIP32_SaltedAccount)
{
   vector<unsigned> derivationPath1 = {
      0x80000050,
      0x800005de,
      0x8000465a,
      501
   };

   vector<unsigned> derivationPath2 = {
   0x80000050,
   0x800005de,
   0x8000ee4f,
   327
   };

   auto&& seed = CryptoPRNG::generateRandom(32);
   auto&& salt1 = CryptoPRNG::generateRandom(32);
   auto&& salt2 = CryptoPRNG::generateRandom(32);

   string filename;
   BinaryData accountID1;
   BinaryData accountID2;

   set<BinaryData> addrHashSet;

   {
      //create empty wallet
      SecureBinaryData passphrase("password");
      auto assetWlt = AssetWallet_Single::createFromSeed_BIP32_Blank(
         homedir_, seed, passphrase);

      auto passphraseLbd = [&passphrase](const BinaryData&)->SecureBinaryData
      {
         return passphrase;
      };
      assetWlt->setPassphrasePromptLambda(passphraseLbd);

      //create accounts
      auto saltedAccType1 = 
         make_shared<AccountType_BIP32_Salted>(salt1);   
      saltedAccType1->setAddressLookup(40);
      saltedAccType1->setDefaultAddressType(
         AddressEntryType_P2WPKH);
      saltedAccType1->setAddressTypes(
         { AddressEntryType_P2WPKH });

      auto saltedAccType2 =
         make_shared<AccountType_BIP32_Salted>(salt2);
      saltedAccType2->setAddressLookup(40);
      saltedAccType2->setDefaultAddressType(
         AddressEntryType_P2WPKH);
      saltedAccType2->setAddressTypes(
         { AddressEntryType_P2WPKH });

      //add bip32 account for derivationPath1
      accountID1 = assetWlt->createBIP32Account(
         nullptr, derivationPath1, saltedAccType1);

      //add bip32 account for derivationPath2
      accountID2 = assetWlt->createBIP32Account(
         nullptr, derivationPath2, saltedAccType2);

      //grab the accounts
      auto accountSalted1 = assetWlt->getAccountForID(
         accountID1);
      auto accountSalted2 = assetWlt->getAccountForID(
         accountID2);

      //grab 10 addresses
      vector<shared_ptr<AddressEntry>> addrVec1, addrVec2;
      for (unsigned i = 0; i < 10; i++)
      {
         addrVec1.push_back(accountSalted1->getNewAddress());
         addrVec2.push_back(accountSalted2->getNewAddress());
      }

      //derive from seed
      {
         BIP32_Node seedNode;
         seedNode.initFromSeed(seed);
         for (auto& derId : derivationPath1)
            seedNode.derivePrivate(derId);

         for (unsigned i = 0; i < 10; i++)
         {
            auto nodeCopy = seedNode;
            nodeCopy.derivePrivate(i);
            auto pubkey = nodeCopy.getPublicKey();
            auto&& saltedKey =
               CryptoECDSA::PubKeyScalarMultiply(pubkey, salt1);
            EXPECT_EQ(saltedKey, addrVec1[i]->getPreimage());
         }
      }

      {
         BIP32_Node seedNode;
         seedNode.initFromSeed(seed);
         for (auto& derId : derivationPath2)
            seedNode.derivePrivate(derId);

         for (unsigned i = 0; i < 10; i++)
         {
            auto nodeCopy = seedNode;
            nodeCopy.derivePrivate(i);
            auto pubkey = nodeCopy.getPublicKey();
            auto&& saltedKey =
               CryptoECDSA::PubKeyScalarMultiply(pubkey, salt2);
            EXPECT_EQ(saltedKey, addrVec2[i]->getPreimage());
         }
      }

      addrHashSet = assetWlt->getAddrHashSet();
      ASSERT_EQ(addrHashSet.size(), 80);

      //shut down the wallet
      filename = assetWlt->getDbFilename();
   }

   {
      auto assetWlt = AssetWallet::loadMainWalletFromFile(filename);
      auto wltSingle = dynamic_pointer_cast<AssetWallet_Single>(assetWlt);
      
      auto accountSalted1 = wltSingle->getAccountForID(accountID1);
      auto accountSalted2 = wltSingle->getAccountForID(accountID2);

      //check current address map
      EXPECT_EQ(addrHashSet, assetWlt->getAddrHashSet());

      //grab more 10 addresses
      vector<shared_ptr<AddressEntry>> addrVec1, addrVec2;
      for (unsigned i = 0; i < 10; i++)
      {
         addrVec1.push_back(accountSalted1->getNewAddress());
         addrVec2.push_back(accountSalted2->getNewAddress());
      }

      //derive from seed
      {
         BIP32_Node seedNode;
         seedNode.initFromSeed(seed);
         for (auto& derId : derivationPath1)
            seedNode.derivePrivate(derId);

         for (unsigned i = 0; i < 10; i++)
         {
            auto nodeCopy = seedNode;
            nodeCopy.derivePrivate(i + 10);
            auto pubkey = nodeCopy.getPublicKey();
            auto&& saltedKey =
               CryptoECDSA::PubKeyScalarMultiply(pubkey, salt1);
            EXPECT_EQ(saltedKey, addrVec1[i]->getPreimage());
         }
      }

      {
         BIP32_Node seedNode;
         seedNode.initFromSeed(seed);
         for (auto& derId : derivationPath2)
            seedNode.derivePrivate(derId);

         for (unsigned i = 0; i < 10; i++)
         {
            auto nodeCopy = seedNode;
            nodeCopy.derivePrivate(i + 10);
            auto pubkey = nodeCopy.getPublicKey();
            auto&& saltedKey =
               CryptoECDSA::PubKeyScalarMultiply(pubkey, salt2);
            EXPECT_EQ(saltedKey, addrVec2[i]->getPreimage());
         }
      }

      addrHashSet = assetWlt->getAddrHashSet();
      ASSERT_EQ(addrHashSet.size(), 80);

      //create WO copy
      filename = AssetWallet_Single::forkWathcingOnly(filename);
   }

   {
      auto assetWlt = AssetWallet::loadMainWalletFromFile(filename);
      auto wltSingle = dynamic_pointer_cast<AssetWallet_Single>(assetWlt);

      ASSERT_TRUE(wltSingle->isWatchingOnly());
      EXPECT_EQ(addrHashSet, assetWlt->getAddrHashSet());

      auto accountSalted1 = wltSingle->getAccountForID(accountID1);
      auto accountSalted2 = wltSingle->getAccountForID(accountID2);

      //grab more 10 addresses
      vector<shared_ptr<AddressEntry>> addrVec1, addrVec2;
      for (unsigned i = 0; i < 10; i++)
      {
         addrVec1.push_back(accountSalted1->getNewAddress());
         addrVec2.push_back(accountSalted2->getNewAddress());
      }

      //derive from seed
      {
         BIP32_Node seedNode;
         seedNode.initFromSeed(seed);
         for (auto& derId : derivationPath1)
            seedNode.derivePrivate(derId);

         for (unsigned i = 0; i < 10; i++)
         {
            auto nodeCopy = seedNode;
            nodeCopy.derivePrivate(i + 20);
            auto pubkey = nodeCopy.getPublicKey();
            auto&& saltedKey =
               CryptoECDSA::PubKeyScalarMultiply(pubkey, salt1);
            EXPECT_EQ(saltedKey, addrVec1[i]->getPreimage());
         }
      }

      {
         BIP32_Node seedNode;
         seedNode.initFromSeed(seed);
         for (auto& derId : derivationPath2)
            seedNode.derivePrivate(derId);

         for (unsigned i = 0; i < 10; i++)
         {
            auto nodeCopy = seedNode;
            nodeCopy.derivePrivate(i + 20);
            auto pubkey = nodeCopy.getPublicKey();
            auto&& saltedKey =
               CryptoECDSA::PubKeyScalarMultiply(pubkey, salt2);
            EXPECT_EQ(saltedKey, addrVec2[i]->getPreimage());
         }
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, ECDH_Account)
{
   //create blank wallet
   string filename, woFilename;

   auto&& seed = CryptoPRNG::generateRandom(32);

   auto&& privKey1 = READHEX(
      "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F");
   auto&& pubKey1 = CryptoECDSA().ComputePublicKey(privKey1, true);

   auto&& privKey2 = READHEX(
      "101112131415161718191A1B1C1D1E1F202122232425262728292A2B2C2D2E2F");
   auto&& pubKey2 = CryptoECDSA().ComputePublicKey(privKey2, true);


   SecureBinaryData passphrase("password");

   map<unsigned, SecureBinaryData> saltMap1;
   map<unsigned, SecureBinaryData> saltMap2;

   BinaryData accID2;
   map<unsigned, BinaryData> addrMap1, addrMap2;

   {
      //create empty wallet
      auto assetWlt = AssetWallet_Single::createFromSeed_BIP32_Blank(
         homedir_, seed, passphrase);

      auto passphraseLbd = [&passphrase](const BinaryData&)->SecureBinaryData
      {
         return passphrase;
      };
      assetWlt->setPassphrasePromptLambda(passphraseLbd);

      //create accounts
      auto ecdhAccType1 =
         make_shared<AccountType_ECDH>(privKey1, pubKey1);
      ecdhAccType1->setDefaultAddressType(
         AddressEntryType_P2WPKH);
      ecdhAccType1->setAddressTypes(
         { AddressEntryType_P2WPKH });
      ecdhAccType1->setMain(true);

      auto ecdhAccType2 =
         make_shared<AccountType_ECDH>(privKey2, pubKey2);
      ecdhAccType2->setDefaultAddressType(
         AddressEntryType_P2WPKH);
      ecdhAccType2->setAddressTypes(
         { AddressEntryType_P2WPKH });

      //add accounts
      auto accPtr1 = assetWlt->createAccount(ecdhAccType1);
      auto accEcdh1 = dynamic_pointer_cast<AssetAccount_ECDH>(
         accPtr1->getOuterAccount());
      if (accEcdh1 == nullptr)
         throw runtime_error("unexpected account type");

      auto accPtr2 = assetWlt->createAccount(ecdhAccType2);
      auto accEcdh2 = dynamic_pointer_cast<AssetAccount_ECDH>(
         accPtr2->getOuterAccount());
      if (accEcdh2 == nullptr)
         throw runtime_error("unexpected account type");
      accID2 = accPtr2->getID();

      //add salts
      for (unsigned i = 0; i < 5; i++)
      {
         auto&& salt = CryptoPRNG::generateRandom(32);
         auto index = accEcdh1->addSalt(salt);
         saltMap1.insert(make_pair(index, salt));

         salt = CryptoPRNG::generateRandom(32);
         index = accEcdh2->addSalt(salt);
         saltMap2.insert(make_pair(index, salt));
      }

      //grab addresses
      for (unsigned i = 0; i < 5; i++)
      {
         addrMap1.insert(make_pair(i, accPtr1->getNewAddress()->getHash()));
         addrMap2.insert(make_pair(i, accPtr2->getNewAddress()->getHash()));
      }
   
      //derive locally, check addresses match
      for (unsigned i = 0; i < 5; i++)
      {
         auto saltedKey = CryptoECDSA::PubKeyScalarMultiply(pubKey1, saltMap1[i]);
         auto hash = BtcUtils::getHash160(saltedKey);
         EXPECT_EQ(addrMap1[i], hash);

         saltedKey = CryptoECDSA::PubKeyScalarMultiply(pubKey2, saltMap2[i]);
         hash = BtcUtils::getHash160(saltedKey);
         EXPECT_EQ(addrMap2[i], hash);
      }

      filename = assetWlt->getDbFilename();
   }

   {
      //reload wallet
      auto wlt = AssetWallet::loadMainWalletFromFile(filename);
      auto assetWlt = dynamic_pointer_cast<AssetWallet_Single>(wlt);
      if (assetWlt == nullptr)
         throw runtime_error("unexpected wallet type");

      //check existing address set
      auto&& addrHashSet = assetWlt->getAddrHashSet();
      EXPECT_EQ(addrHashSet.size(), 10);

      for (unsigned i = 0; i < 5; i++)
      {
         auto saltedKey = CryptoECDSA::PubKeyScalarMultiply(pubKey1, saltMap1[i]);
         auto hash = BtcUtils::getHash160(saltedKey);
         BinaryWriter bwAddr;
         bwAddr.put_uint8_t(SCRIPT_PREFIX_P2WPKH);
         bwAddr.put_BinaryData(hash);

         auto iter = addrHashSet.find(bwAddr.getData());
         EXPECT_NE(iter, addrHashSet.end());

         //
         saltedKey = CryptoECDSA::PubKeyScalarMultiply(pubKey2, saltMap2[i]);
         hash = BtcUtils::getHash160(saltedKey);
         BinaryWriter bwAddr2;
         bwAddr2.put_uint8_t(SCRIPT_PREFIX_P2WPKH);
         bwAddr2.put_BinaryData(hash);

         iter = addrHashSet.find(bwAddr2.getData());
         EXPECT_NE(iter, addrHashSet.end());
      }

      auto accID = assetWlt->getMainAccountID();
      auto accPtr = assetWlt->getAccountForID(accID);
      auto accEcdh = dynamic_pointer_cast<AssetAccount_ECDH>(
         accPtr->getOuterAccount());
      if (accEcdh == nullptr)
         throw runtime_error("unexpected account type");

      {
         auto&& salt = CryptoPRNG::generateRandom(32);
         auto index = accEcdh->addSalt(salt);
         saltMap1.insert(make_pair(index, salt));
      }

      {
         //grab another address & check it
         auto addr = accPtr->getNewAddress()->getHash();
         auto saltedKey = CryptoECDSA::PubKeyScalarMultiply(pubKey1, saltMap1[5]);
         auto hash = BtcUtils::getHash160(saltedKey);

         EXPECT_EQ(addr, hash);
      }

      {
         //grab an existing address from its settlement id
         auto id = accEcdh->addSalt(saltMap1[3]);
         EXPECT_EQ(id, 3);

         auto assetPtr = accEcdh->getAssetForIndex(id);
         auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(assetPtr);
         auto hash = BtcUtils::getHash160(
            assetSingle->getPubKey()->getCompressedKey());

         EXPECT_EQ(addrMap1[3], hash);
      }

      auto accPtr2 = assetWlt->getAccountForID(accID2);

      {
         //same with account 2
         auto accEcdhPtr = dynamic_pointer_cast<AssetAccount_ECDH>(
            accPtr2->getOuterAccount());
         ASSERT_NE(accEcdhPtr, nullptr);

         auto id = accEcdhPtr->addSalt(saltMap2[2]);
         EXPECT_EQ(id, 2);

         auto assetPtr = accEcdhPtr->getAssetForIndex(id);
         auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(assetPtr);
         auto hash = BtcUtils::getHash160(
            assetSingle->getPubKey()->getCompressedKey());

         EXPECT_EQ(addrMap2[2], hash);
      }
   }
      
   woFilename = AssetWallet::forkWathcingOnly(filename);

   //same with WO
   {
      //reload wallet
      auto wlt = AssetWallet::loadMainWalletFromFile(woFilename);
      auto assetWlt = dynamic_pointer_cast<AssetWallet_Single>(wlt);
      if (assetWlt == nullptr)
         throw runtime_error("unexpected wallet type");

      ASSERT_TRUE(assetWlt->isWatchingOnly());

      //check existing address set
      auto&& addrHashSet = assetWlt->getAddrHashSet();
      EXPECT_EQ(addrHashSet.size(), 11);

      for (unsigned i = 0; i < 6; i++)
      {
         auto saltedKey = CryptoECDSA::PubKeyScalarMultiply(pubKey1, saltMap1[i]);
         auto hash = BtcUtils::getHash160(saltedKey);
         BinaryWriter bwAddr;
         bwAddr.put_uint8_t(SCRIPT_PREFIX_P2WPKH);
         bwAddr.put_BinaryData(hash);

         auto iter = addrHashSet.find(bwAddr.getData());
         EXPECT_NE(iter, addrHashSet.end());
      }

      auto accID = assetWlt->getMainAccountID();
      auto accPtr = assetWlt->getAccountForID(accID);
      auto accEcdh = dynamic_pointer_cast<AssetAccount_ECDH>(
         accPtr->getOuterAccount());
      if (accEcdh == nullptr)
         throw runtime_error("unexpected account type");

      auto rootAsset = accEcdh->getRoot();
      auto rootSingle = dynamic_pointer_cast<AssetEntry_Single>(rootAsset);
      ASSERT_NE(rootSingle, nullptr);
      EXPECT_EQ(rootSingle->getPrivKey(), nullptr);

      {
         auto&& salt = CryptoPRNG::generateRandom(32);
         auto index = accEcdh->addSalt(salt);
         saltMap1.insert(make_pair(index, salt));
      }

      {
         //grab another address & check it
         auto addr = accPtr->getNewAddress()->getHash();
         auto saltedKey = CryptoECDSA::PubKeyScalarMultiply(pubKey1, saltMap1[6]);
         auto hash = BtcUtils::getHash160(saltedKey);

         EXPECT_EQ(addr, hash);
      }

      auto accID2 = assetWlt->getMainAccountID();
      auto accPtr2 = assetWlt->getAccountForID(accID2);

      for (unsigned i = 0; i < 5; i++)
      {
         auto saltedKey = CryptoECDSA::PubKeyScalarMultiply(pubKey2, saltMap2[i]);
         auto hash = BtcUtils::getHash160(saltedKey);
         BinaryWriter bwAddr;
         bwAddr.put_uint8_t(SCRIPT_PREFIX_P2WPKH);
         bwAddr.put_BinaryData(hash);

         auto iter = addrHashSet.find(bwAddr.getData());
         EXPECT_NE(iter, addrHashSet.end());
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class WalletMetaDataTest : public ::testing::Test
{
protected:
   string homedir_;
   BlockDataManagerConfig config_;

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      LOGDISABLESTDOUT();
      homedir_ = string("./fakehomedir");
      DBUtils::removeDirectory(homedir_);
      mkdir(homedir_);
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      DBUtils::removeDirectory(homedir_);
   }
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletMetaDataTest, AuthPeers)
{
   auto authPeers = make_unique<AuthorizedPeers>(homedir_, "test.peers");

   //auth meta account expects valid pubkeys
   auto&& privKey1 = CryptoPRNG::generateRandom(32);
   auto&& pubkey1 = CryptoECDSA().ComputePublicKey(privKey1);
   auto&& pubkey1_compressed = CryptoECDSA().CompressPoint(pubkey1);
   authPeers->addPeer(pubkey1, 
      "1.1.1.1", "0123::4567::89ab::cdef::", "test.com");

   auto&& privKey2 = CryptoPRNG::generateRandom(32);
   auto&& pubkey2 = CryptoECDSA().ComputePublicKey(privKey2);
   auto&& pubkey2_compressed = CryptoECDSA().CompressPoint(pubkey2);
   authPeers->addPeer(pubkey2_compressed, "2.2.2.2", "domain.com");

   auto&& privKey3 = CryptoPRNG::generateRandom(32);
   auto&& pubkey3 = CryptoECDSA().ComputePublicKey(privKey3);
   auto&& pubkey3_compressed = CryptoECDSA().CompressPoint(pubkey3);
   string domain_name("anotherdomain.com");
   authPeers->addPeer(pubkey3_compressed,
      "3.3.3.3", "test.com", domain_name);

   {
      //check peer object has expected values
      auto& peerMap = authPeers->getPeerNameMap();
      auto& pubkeySet = authPeers->getPublicKeySet();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, iter3->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey1_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey1_sbd, pubkey1_compressed);
         EXPECT_NE(pubkey1_sbd, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1_compressed) != pubkeySet.end());
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey2_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey2_sbd, pubkey2_compressed);
         EXPECT_NE(pubkey2_sbd, pubkey2);
         EXPECT_TRUE(pubkeySet.find(pubkey2_compressed) != pubkeySet.end());
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, iter3->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey3_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey3_sbd, pubkey3_compressed);
         EXPECT_NE(pubkey3_sbd, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3_compressed) != pubkeySet.end());
      }
   }

   //delete auth peer object, reload and test again
   authPeers.reset();
   authPeers = make_unique<AuthorizedPeers>(homedir_, "test.peers");

   {
      //check peer object has expected values
      auto& peerMap = authPeers->getPeerNameMap();
      auto& pubkeySet = authPeers->getPublicKeySet();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, iter3->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey1_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey1_sbd, pubkey1_compressed);
         EXPECT_NE(pubkey1_sbd, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1_compressed) != pubkeySet.end());
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey2_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey2_sbd, pubkey2_compressed);
         EXPECT_NE(pubkey2_sbd, pubkey2);
         EXPECT_TRUE(pubkeySet.find(pubkey2_compressed) != pubkeySet.end());
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, iter3->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey3_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey3_sbd, pubkey3_compressed);
         EXPECT_NE(pubkey3_sbd, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3_compressed) != pubkeySet.end());
      }
   }

   //add more keys
   auto&& privKey4 = CryptoPRNG::generateRandom(32);
   auto&& pubkey4 = CryptoECDSA().ComputePublicKey(privKey4);
   auto&& pubkey4_compressed = CryptoECDSA().CompressPoint(pubkey4);
   btc_pubkey btckey4;
   btc_pubkey_init(&btckey4);
   std::memcpy(btckey4.pubkey, pubkey4.getPtr(), 65);
   btc_pubkey btckey4_cmp;
   btc_pubkey_init(&btckey4_cmp);
   btc_ecc_public_key_compress(btckey4.pubkey, btckey4_cmp.pubkey);
   btckey4_cmp.compressed = true;

   authPeers->addPeer(btckey4,
      "4.4.4.4", "more.com");

   auto&& privKey5 = CryptoPRNG::generateRandom(32);
   auto&& pubkey5 = CryptoECDSA().ComputePublicKey(privKey5);
   auto&& pubkey5_compressed = CryptoECDSA().CompressPoint(pubkey5);
   btc_pubkey btckey5;
   btc_pubkey_init(&btckey5);
   std::memcpy(btckey5.pubkey, pubkey5_compressed.getPtr(), 33);
   btckey5.compressed = true;

   authPeers->addPeer(btckey5, "5.5.5.5", "newdomain.com");

   {
      //check peer object has expected values
      auto& peerMap = authPeers->getPeerNameMap();
      auto& pubkeySet = authPeers->getPublicKeySet();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, iter3->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey1_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey1_sbd, pubkey1_compressed);
         EXPECT_NE(pubkey1_sbd, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1_compressed) != pubkeySet.end());
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey2_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey2_sbd, pubkey2_compressed);
         EXPECT_NE(pubkey2_sbd, pubkey2);
         EXPECT_TRUE(pubkeySet.find(pubkey2_compressed) != pubkeySet.end());
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, iter3->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey3_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey3_sbd, pubkey3_compressed);
         EXPECT_NE(pubkey3_sbd, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3_compressed) != pubkeySet.end());
      }

      {
         //4th peer

         auto iter1 = peerMap.find("4.4.4.4");
         auto iter2 = peerMap.find("more.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         EXPECT_NE(memcmp(iter1->second.pubkey, btckey4.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, btckey4_cmp.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(pubkeySet.find(pubkey4_compressed) != pubkeySet.end());
      }

      {
         //5th peer

         auto iter1 = peerMap.find("5.5.5.5");
         auto iter2 = peerMap.find("newdomain.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         EXPECT_EQ(memcmp(iter1->second.pubkey, btckey5.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(pubkeySet.find(pubkey5_compressed) != pubkeySet.end());
      }
   }

   //remove entries, check again
   authPeers->eraseName(domain_name);
   authPeers->eraseKey(pubkey2);
   authPeers->eraseName("5.5.5.5");
   authPeers->eraseKey(btckey4);

   {
      //check peer object has expected values
      auto& peerMap = authPeers->getPeerNameMap();
      auto& pubkeySet = authPeers->getPublicKeySet();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, iter3->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey1_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey1_sbd, pubkey1_compressed);
         EXPECT_NE(pubkey1_sbd, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1_compressed) != pubkeySet.end());
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_TRUE(iter1 == peerMap.end());
         EXPECT_TRUE(iter2 == peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey2_compressed) == pubkeySet.end());
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(iter3 == peerMap.end());

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey3_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey3_sbd, pubkey3_compressed);
         EXPECT_NE(pubkey3_sbd, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3_compressed) != pubkeySet.end());
      }

      {
         //4th peer
         auto iter1 = peerMap.find("4.4.4.4");
         auto iter2 = peerMap.find("more.com");

         EXPECT_EQ(iter1, peerMap.end());
         EXPECT_EQ(iter2, peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey4_compressed) == pubkeySet.end());
      }

      {
         //5th peer
         auto iter1 = peerMap.find("5.5.5.5");
         auto iter2 = peerMap.find("newdomain.com");

         EXPECT_EQ(iter1, peerMap.end());

         EXPECT_EQ(memcmp(iter2->second.pubkey, btckey5.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(pubkeySet.find(pubkey5_compressed) != pubkeySet.end());
      }
   }

   //delete auth peer object, reload and test again
   authPeers.reset();
   authPeers = make_unique<AuthorizedPeers>(homedir_, "test.peers");

   {
      //check peer object has expected values
      auto& peerMap = authPeers->getPeerNameMap();
      auto& pubkeySet = authPeers->getPublicKeySet();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, iter3->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey1_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey1_sbd, pubkey1_compressed);
         EXPECT_NE(pubkey1_sbd, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1_compressed) != pubkeySet.end());
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_TRUE(iter1 == peerMap.end());
         EXPECT_TRUE(iter2 == peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey2_compressed) == pubkeySet.end());
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(iter3 == peerMap.end());

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey3_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey3_sbd, pubkey3_compressed);
         EXPECT_NE(pubkey3_sbd, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3_compressed) != pubkeySet.end());
      }

      {
         //4th peer
         auto iter1 = peerMap.find("4.4.4.4");
         auto iter2 = peerMap.find("more.com");

         EXPECT_EQ(iter1, peerMap.end());
         EXPECT_EQ(iter2, peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey4_compressed) == pubkeySet.end());
      }

      {
         //5th peer
         auto iter1 = peerMap.find("5.5.5.5");
         auto iter2 = peerMap.find("newdomain.com");

         EXPECT_EQ(iter1, peerMap.end());

         EXPECT_EQ(memcmp(iter2->second.pubkey, btckey5.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(pubkeySet.find(pubkey5_compressed) != pubkeySet.end());
      }
   }

   //remove last name of 5th peer, check keySet entry is gone too
   authPeers->eraseName("newdomain.com");

   {
      //check peer object has expected values
      auto& peerMap = authPeers->getPeerNameMap();
      auto& pubkeySet = authPeers->getPublicKeySet();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, iter3->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey1_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey1_sbd, pubkey1_compressed);
         EXPECT_NE(pubkey1_sbd, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1_sbd) != pubkeySet.end());
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_TRUE(iter1 == peerMap.end());
         EXPECT_TRUE(iter2 == peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey2_compressed) == pubkeySet.end());
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(iter3 == peerMap.end());

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey3_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey3_sbd, pubkey3_compressed);
         EXPECT_NE(pubkey3_sbd, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3_sbd) != pubkeySet.end());
      }

      {
         //4th peer
         auto iter1 = peerMap.find("4.4.4.4");
         auto iter2 = peerMap.find("more.com");

         EXPECT_EQ(iter1, peerMap.end());
         EXPECT_EQ(iter2, peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey4_compressed) == pubkeySet.end());
      }

      {
         //5th peer
         auto iter1 = peerMap.find("5.5.5.5");
         auto iter2 = peerMap.find("newdomain.com");

         EXPECT_EQ(iter1, peerMap.end());
         EXPECT_EQ(iter2, peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey5_compressed) == pubkeySet.end());
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletMetaDataTest, AuthPeers_Ephemeral)
{
   auto authPeers = make_unique<AuthorizedPeers>();

   //auth meta account expects valid pubkeys
   auto&& privKey1 = CryptoPRNG::generateRandom(32);
   auto&& pubkey1 = CryptoECDSA().ComputePublicKey(privKey1);
   auto&& pubkey1_compressed = CryptoECDSA().CompressPoint(pubkey1);
   authPeers->addPeer(pubkey1,
      "1.1.1.1", "0123::4567::89ab::cdef::", "test.com");

   auto&& privKey2 = CryptoPRNG::generateRandom(32);
   auto&& pubkey2 = CryptoECDSA().ComputePublicKey(privKey2);
   auto&& pubkey2_compressed = CryptoECDSA().CompressPoint(pubkey2);
   authPeers->addPeer(pubkey2_compressed, "2.2.2.2", "domain.com");

   auto&& privKey3 = CryptoPRNG::generateRandom(32);
   auto&& pubkey3 = CryptoECDSA().ComputePublicKey(privKey3);
   auto&& pubkey3_compressed = CryptoECDSA().CompressPoint(pubkey3);
   string domain_name("anotherdomain.com");
   authPeers->addPeer(pubkey3_compressed,
      "3.3.3.3", "test.com", domain_name);

   {
      //check peer object has expected values
      auto& peerMap = authPeers->getPeerNameMap();
      auto& pubkeySet = authPeers->getPublicKeySet();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, iter3->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey1_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey1_sbd, pubkey1_compressed);
         EXPECT_NE(pubkey1_sbd, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1_compressed) != pubkeySet.end());
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey2_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey2_sbd, pubkey2_compressed);
         EXPECT_NE(pubkey2_sbd, pubkey2);
         EXPECT_TRUE(pubkeySet.find(pubkey2_compressed) != pubkeySet.end());
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, iter3->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey3_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey3_sbd, pubkey3_compressed);
         EXPECT_NE(pubkey3_sbd, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3_compressed) != pubkeySet.end());
      }
   }

   //add more keys
   auto&& privKey4 = CryptoPRNG::generateRandom(32);
   auto&& pubkey4 = CryptoECDSA().ComputePublicKey(privKey4);
   auto&& pubkey4_compressed = CryptoECDSA().CompressPoint(pubkey4);
   btc_pubkey btckey4;
   btc_pubkey_init(&btckey4);
   std::memcpy(btckey4.pubkey, pubkey4.getPtr(), 65);
   btc_pubkey btckey4_cmp;
   btc_pubkey_init(&btckey4_cmp);
   btc_ecc_public_key_compress(btckey4.pubkey, btckey4_cmp.pubkey);
   btckey4_cmp.compressed = true;

   authPeers->addPeer(btckey4,
      "4.4.4.4", "more.com");

   auto&& privKey5 = CryptoPRNG::generateRandom(32);
   auto&& pubkey5 = CryptoECDSA().ComputePublicKey(privKey5);
   auto&& pubkey5_compressed = CryptoECDSA().CompressPoint(pubkey5);
   btc_pubkey btckey5;
   btc_pubkey_init(&btckey5);
   std::memcpy(btckey5.pubkey, pubkey5_compressed.getPtr(), 33);
   btckey5.compressed = true;

   authPeers->addPeer(btckey5, "5.5.5.5", "newdomain.com");

   {
      //check peer object has expected values
      auto& peerMap = authPeers->getPeerNameMap();
      auto& pubkeySet = authPeers->getPublicKeySet();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, iter3->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey1_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey1_sbd, pubkey1_compressed);
         EXPECT_NE(pubkey1_sbd, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1_compressed) != pubkeySet.end());
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey2_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey2_sbd, pubkey2_compressed);
         EXPECT_NE(pubkey2_sbd, pubkey2);
         EXPECT_TRUE(pubkeySet.find(pubkey2_compressed) != pubkeySet.end());
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, iter3->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey3_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey3_sbd, pubkey3_compressed);
         EXPECT_NE(pubkey3_sbd, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3_compressed) != pubkeySet.end());
      }

      {
         //4th peer

         auto iter1 = peerMap.find("4.4.4.4");
         auto iter2 = peerMap.find("more.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         EXPECT_NE(memcmp(iter1->second.pubkey, btckey4.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, btckey4_cmp.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(pubkeySet.find(pubkey4_compressed) != pubkeySet.end());
      }

      {
         //5th peer

         auto iter1 = peerMap.find("5.5.5.5");
         auto iter2 = peerMap.find("newdomain.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         EXPECT_EQ(memcmp(iter1->second.pubkey, btckey5.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(pubkeySet.find(pubkey5_compressed) != pubkeySet.end());
      }
   }

   //remove entries, check again
   authPeers->eraseName(domain_name);
   authPeers->eraseKey(pubkey2);
   authPeers->eraseName("5.5.5.5");
   authPeers->eraseKey(btckey4);

   {
      //check peer object has expected values
      auto& peerMap = authPeers->getPeerNameMap();
      auto& pubkeySet = authPeers->getPublicKeySet();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_EQ(memcmp(iter1->second.pubkey, iter3->second.pubkey, BIP151PUBKEYSIZE), 0);

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey1_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey1_sbd, pubkey1_compressed);
         EXPECT_NE(pubkey1_sbd, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1_compressed) != pubkeySet.end());
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_TRUE(iter1 == peerMap.end());
         EXPECT_TRUE(iter2 == peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey2_compressed) == pubkeySet.end());
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(memcmp(iter1->second.pubkey, iter2->second.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(iter3 == peerMap.end());

         //convert btc_pubkey to sbd
         SecureBinaryData pubkey3_sbd(iter1->second.pubkey, BIP151PUBKEYSIZE);
         EXPECT_EQ(pubkey3_sbd, pubkey3_compressed);
         EXPECT_NE(pubkey3_sbd, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3_compressed) != pubkeySet.end());
      }

      {
         //4th peer
         auto iter1 = peerMap.find("4.4.4.4");
         auto iter2 = peerMap.find("more.com");

         EXPECT_EQ(iter1, peerMap.end());
         EXPECT_EQ(iter2, peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey4_compressed) == pubkeySet.end());
      }

      {
         //5th peer
         auto iter1 = peerMap.find("5.5.5.5");
         auto iter2 = peerMap.find("newdomain.com");

         EXPECT_EQ(iter1, peerMap.end());

         EXPECT_EQ(memcmp(iter2->second.pubkey, btckey5.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(pubkeySet.find(pubkey5_compressed) != pubkeySet.end());
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

   btc_ecc_start();

   GOOGLE_PROTOBUF_VERIFY_VERSION;
   srand(time(0));
   std::cout << "Running main() from gtest_main.cc\n";

   // Setup the log file 
   STARTLOGGING("cppTestsLog.txt", LogLvlDebug2);
   //LOGDISABLESTDOUT();

   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();

   FLUSHLOG();
   CLEANUPLOG();
   google::protobuf::ShutdownProtobufLibrary();

   btc_ecc_stop();
   return exitCode;
}
