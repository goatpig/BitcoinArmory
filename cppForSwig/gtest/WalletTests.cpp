////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//                                                                            //
//  Copyright (C) 2017-2024, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <filesystem>

#include "TestUtils.h"
#include "../Wallets/PassphraseLambda.h"
#include "../Wallets/Seeds/Backups.h"
#include "../Wallets/Seeds/Seeds.h"
#include "../Wallets/WalletFileInterface.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/Bridge.capnp.h"

using namespace std;
using namespace Armory::Signing;
using namespace Armory::Config;
using namespace Armory::Assets;
using namespace Armory::Accounts;
using namespace Armory::Wallets;
using namespace Armory::Wallets::Encryption;
using namespace Armory::Seeds;

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
class AddressTests : public ::testing::Test
{
protected:
   virtual void SetUp(void)
   {
      homedir_ = std::filesystem::path("./fakehomedir");
      FileUtils::removeDirectory(homedir_);
      std::filesystem::create_directory(homedir_);

      Armory::Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Armory::Config::ProcessType::DB);
   }

   virtual void TearDown(void)
   {
      Armory::Config::reset();
      FileUtils::removeDirectory(homedir_);
   }

   std::filesystem::path homedir_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(AddressTests, base58_Tests)
{
   BinaryData h_160 = READHEX("00010966776006953d5567439e5e39f86a0d273bee");
   string addStr("16UwLL9Risc3QfPqBUvKofHmBQ7wMtjvM");

   auto&& encoded = BtcUtils::scrAddrToBase58(h_160);
   EXPECT_EQ(encoded, addStr);

   auto&& decoded = BtcUtils::base58toScrAddr(addStr);
   EXPECT_EQ(decoded, h_160);

   decoded = BtcUtils::base58toScrAddr(encoded);
   EXPECT_EQ(decoded, h_160);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(AddressTests, bech32_Tests)
{
   BinaryData pubkey =
      READHEX("0279BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798");
   string p2wpkhScrAddr("bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4");
   string p2wshAddr("bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3");
   string invalidSwAddr("bca0w508d6qejxtdg4y5r3zarvary0c5xw7kw508d6qejxtdg4y5r3zarvary0c5xw7kw5rljs90234567789035");

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
   EXPECT_EQ(pubkey_hash2.second, 0);
   EXPECT_EQ(pubkey_hash, pubkey_hash2.first);

   auto&& script_hash2 = BtcUtils::segWitAddressToScrAddr(scrAddr_p2wsh);
   EXPECT_EQ(script_hash2.second, 0);
   EXPECT_EQ(script_hash, script_hash2.first);

   //buffer overrun issue check
   try
   {
      auto&& script_hash3 = BtcUtils::segWitAddressToScrAddr(invalidSwAddr);
      ASSERT_TRUE(false);
   }
   catch (runtime_error& e)
   {
      EXPECT_EQ(e.what(), string("failed to decode sw address!"));
   }
}

////////////////////////////////////////////////////////////////////////////////
class WalletIdsTests : public ::testing::Test
{
   virtual void SetUp()
   {}

   virtual void TearDown()
   {}
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletIdsTests, AddressAccounts)
{
   int32_t accIdValue = 0x12345678;
   AddressAccountId accId(accIdValue);

   //check serialization as key
   {
      auto key = accId.getSerializedKey(22);
      EXPECT_EQ(key.getSize(), 5ull);

      BinaryRefReader brrKey(key);
      auto prefix = brrKey.get_uint8_t();
      EXPECT_EQ(prefix, 22);

      auto idkey = brrKey.get_int32_t(BE);
      EXPECT_EQ(idkey, accIdValue);

      try
      {
         auto aaidFromKey = AddressAccountId::deserializeKey(key, 22);
         EXPECT_EQ(aaidFromKey, accId);

         auto aaidKey = aaidFromKey.getAddressAccountKey();
         EXPECT_EQ(aaidKey, accIdValue);
      }
      catch (const exception&)
      {
         EXPECT_TRUE(false);
      }

      auto serKey = READHEX("1612345678");
      EXPECT_EQ(serKey, key);
   }

   //key with prefix > 0x7f
   {
      auto key = accId.getSerializedKey(231);
      EXPECT_EQ(key.getSize(), 5ull);

      BinaryRefReader brrKey(key);
      auto prefix = brrKey.get_uint8_t();
      EXPECT_EQ(prefix, 231);

      auto idkey = brrKey.get_int32_t(BE);
      EXPECT_EQ(idkey, accIdValue);

      try
      {
         auto aaidFromKey = AddressAccountId::deserializeKey(key, 231);
         EXPECT_EQ(aaidFromKey, accId);

         auto aaidKey = aaidFromKey.getAddressAccountKey();
         EXPECT_EQ(aaidKey, accIdValue);
      }
      catch (const exception&)
      {
         EXPECT_TRUE(false);
      }

      auto serKey = READHEX("E712345678");
      EXPECT_EQ(serKey, key);
   }

   //check serialization as value
   {
      BinaryWriter bw;
      accId.serializeValue(bw);

      auto value = bw.getData();
      EXPECT_EQ(value.getSize(), 5ull);
      BinaryRefReader brrValue(value);

      auto len = brrValue.get_var_int();
      EXPECT_EQ(len, 4ull);

      auto aaidKey = brrValue.get_int32_t(BE);
      EXPECT_EQ(aaidKey, accIdValue);

      try
      {
         auto aaidFromValue = AddressAccountId::deserializeValue(value);
         EXPECT_EQ(aaidFromValue, accId);

         auto aaidKey2 = aaidFromValue.getAddressAccountKey();
         EXPECT_EQ(aaidKey2, accIdValue);
      }
      catch (const exception&)
      {
         EXPECT_TRUE(false);
      }

      auto serValue = READHEX("0412345678");
      EXPECT_EQ(serValue, value);
   }

   //invalid deser
   try
   {
      auto serKey = READHEX("0001234567");
      AddressAccountId::deserializeKey(serKey, 1);
      EXPECT_TRUE(false);
   }
   catch (const IdException&)
   {}

   try
   {
      auto serKey = READHEX("00012345");
      AddressAccountId::deserializeKey(serKey, 0);
      EXPECT_TRUE(false);
   }
   catch (const runtime_error&)
   {}

   try
   {
      auto serKey = READHEX("000123456789AB");
      AddressAccountId::deserializeKey(serKey, 0);
      EXPECT_TRUE(false);
   }
   catch (const IdException&)
   {}

   try
   {
      auto serValue = READHEX("000123456789ABCDEF");
      AddressAccountId::deserializeValue(serValue);
      EXPECT_TRUE(false);
   }
   catch (const IdException&)
   {}

   try
   {
      auto serValue = READHEX("0A0123456789ABCDEF");
      AddressAccountId::deserializeValue(serValue);
      EXPECT_TRUE(false);
   }
   catch (const runtime_error&)
   {}

   try
   {
      auto serValue = READHEX("080123456789ABCD");
      AddressAccountId::deserializeValue(serValue);
      EXPECT_TRUE(false);
   }
   catch (const runtime_error&)
   {}

   try
   {
      auto serValue = READHEX("0A0123456789ABCDEFA0B1");
      AddressAccountId::deserializeValue(serValue);
      EXPECT_TRUE(false);
   }
   catch (const IdException&)
   {}
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletIdsTests, AssetAccounts)
{
   int32_t aaIdValue = 0x01234567;
   int32_t asIdValue = 0x89ABCDEF;
   AddressAccountId accId(aaIdValue);
   AssetAccountId assId1(aaIdValue, asIdValue);
   AssetAccountId assId2(accId, asIdValue);

   EXPECT_EQ(assId1, assId2);

   //check serialization as key
   {
      auto key = assId1.getSerializedKey(185);
      EXPECT_EQ(key.getSize(), 9ull);

      BinaryRefReader brrKey(key);
      auto prefix = brrKey.get_uint8_t();
      EXPECT_EQ(prefix, 185);

      auto aaidkey = brrKey.get_int32_t(BE);
      EXPECT_EQ(aaidkey, aaIdValue);

      auto asidkey = brrKey.get_int32_t(BE);
      EXPECT_EQ(asidkey, asIdValue);

      try
      {
         auto asidFromKey = AssetAccountId::deserializeKey(key, 185);
         EXPECT_EQ(asidFromKey, assId1);
         EXPECT_EQ(asidFromKey, assId2);

         auto aaidKey = asidFromKey.getAddressAccountKey();
         EXPECT_EQ(aaidKey, aaIdValue);

         auto asidKey = asidFromKey.getAssetAccountKey();
         EXPECT_EQ(asidKey, asIdValue);

         auto accid = asidFromKey.getAddressAccountId();
         EXPECT_EQ(accid, accId);
      }
      catch (const exception&)
      {
         EXPECT_TRUE(false);
      }

      auto serKey = READHEX("B90123456789ABCDEF");
      EXPECT_EQ(serKey, key);
   }

   //check serialization as value
   {
      BinaryWriter bw;
      assId2.serializeValue(bw);

      auto value = bw.getData();
      EXPECT_EQ(value.getSize(), 9ull);
      BinaryRefReader brrValue(value);

      auto len = brrValue.get_var_int();
      EXPECT_EQ(len, 8ull);

      auto aaidkey = brrValue.get_int32_t(BE);
      EXPECT_EQ(aaidkey, aaIdValue);

      auto asidkey = brrValue.get_int32_t(BE);
      EXPECT_EQ(asidkey, asIdValue);

      try
      {
         BinaryRefReader brrValue2(value);
         auto asidFromValue = AssetAccountId::deserializeValue(brrValue2);
         EXPECT_EQ(asidFromValue, assId1);
         EXPECT_EQ(asidFromValue, assId2);

         auto aaidKey = asidFromValue.getAddressAccountKey();
         EXPECT_EQ(aaidKey, aaIdValue);

         auto asidKey = asidFromValue.getAssetAccountKey();
         EXPECT_EQ(asidKey, asIdValue);

         auto accid = asidFromValue.getAddressAccountId();
         EXPECT_EQ(accid, accId);
      }
      catch (const exception&)
      {
         EXPECT_TRUE(false);
      }

      auto serValue = READHEX("080123456789ABCDEF");
      EXPECT_EQ(serValue, value);
   }

   //deser old
   {
      BinaryWriter bw;
      bw.put_var_int(4);
      bw.put_int32_t(asIdValue, BE);
      BinaryRefReader brr(bw.getData());
      auto asidValue = AssetAccountId::deserializeValueOld(accId, brr);

      EXPECT_EQ(assId1, asidValue);
      EXPECT_EQ(assId2, asidValue);

      auto aaidKey = asidValue.getAddressAccountKey();
      EXPECT_EQ(aaidKey, aaIdValue);

      auto asidKey = asidValue.getAssetAccountKey();
      EXPECT_EQ(asidKey, asIdValue);

      auto accid = asidValue.getAddressAccountId();
      EXPECT_EQ(accid, accId);
   }

   //invalid deser
   try
   {
      auto serKey = READHEX("000123456798AABBCC");
      AssetAccountId::deserializeKey(serKey, 230);
      EXPECT_TRUE(false);
   }
   catch (const IdException&)
   {}

   try
   {
      auto serKey = READHEX("00012345");
      AssetAccountId::deserializeKey(serKey, 0);
      EXPECT_TRUE(false);
   }
   catch (const runtime_error&)
   {}

   try
   {
      auto serKey = READHEX("E60123456789ABCDEF00");
      AssetAccountId::deserializeKey(serKey, 230);
      EXPECT_TRUE(false);
   }
   catch (const IdException&)
   {}

   try
   {
      auto serValue = READHEX("000123456789ABCDEF");
      BinaryRefReader brr(serValue);
      AssetAccountId::deserializeValue(brr);
      EXPECT_TRUE(false);
   }
   catch (const IdException&)
   {}

   try
   {
      auto serValue = READHEX("0A0123456789ABCDEF");
      BinaryRefReader brr(serValue);
      AssetAccountId::deserializeValue(brr);
      EXPECT_TRUE(false);
   }
   catch (const runtime_error&)
   {}

   try
   {
      auto serValue = READHEX("080123456789ABCD");
      BinaryRefReader brr(serValue);
      AssetAccountId::deserializeValue(brr);
      EXPECT_TRUE(false);
   }
   catch (const runtime_error&)
   {}

   try
   {
      auto serValue = READHEX("0A0123456789ABCDEFA0B1");
      BinaryRefReader brr(serValue);
      AssetAccountId::deserializeValue(brr);
      EXPECT_TRUE(false);
   }
   catch (const IdException&)
   {}
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletIdsTests, Assets)
{
   int32_t aaIdValue = 0x01234567;
   int32_t asIdValue = 0x89ABCDEF;
   int32_t assetIdValue = 0xA0B1C2D3;
   AddressAccountId accId(aaIdValue);
   AssetAccountId acsId(aaIdValue, asIdValue);

   AssetId assetId1(aaIdValue, asIdValue, assetIdValue);
   AssetId assetId2(accId, asIdValue, assetIdValue);
   AssetId assetId3(acsId, assetIdValue);

   EXPECT_EQ(assetId1, assetId2);
   EXPECT_EQ(assetId1, assetId3);
   EXPECT_EQ(assetId2, assetId3);

   //check serialization as key
   {
      auto key = assetId1.getSerializedKey(214);
      EXPECT_EQ(key.getSize(), 13ull);

      BinaryRefReader brrKey(key);
      auto prefix = brrKey.get_uint8_t();
      EXPECT_EQ(prefix, 214);

      auto aaidkey = brrKey.get_int32_t(BE);
      EXPECT_EQ(aaidkey, aaIdValue);

      auto asidkey = brrKey.get_int32_t(BE);
      EXPECT_EQ(asidkey, asIdValue);

      auto assetkey = brrKey.get_int32_t(BE);
      EXPECT_EQ(assetkey, assetIdValue);

      try
      {
         auto assetidFromKey = AssetId::deserializeKey(key, 214);
         EXPECT_EQ(assetidFromKey, assetId1);
         EXPECT_EQ(assetidFromKey, assetId2);
         EXPECT_EQ(assetidFromKey, assetId3);

         auto aaidKey = assetidFromKey.getAddressAccountKey();
         EXPECT_EQ(aaidKey, aaIdValue);
         auto assetidKey = assetidFromKey.getAssetKey();
         EXPECT_EQ(assetIdValue, assetidKey);

         auto acsid = assetidFromKey.getAssetAccountId();
         EXPECT_EQ(acsid, acsId);
         auto asidKey = acsid.getAssetAccountKey();
         EXPECT_EQ(asidKey, asIdValue);

         auto accid = assetidFromKey.getAddressAccountId();
         EXPECT_EQ(accid, accId);
      }
      catch (const exception&)
      {
         EXPECT_TRUE(false);
      }

      auto serKey = READHEX("D60123456789ABCDEFA0B1C2D3");
      EXPECT_EQ(serKey, key);
   }

   //check serialization as value
   {
      BinaryWriter bw;
      assetId2.serializeValue(bw);

      auto value = bw.getData();
      EXPECT_EQ(value.getSize(), 13ull);
      BinaryRefReader brrValue(value);

      auto len = brrValue.get_var_int();
      EXPECT_EQ(len, 12ull);

      auto aaidkey = brrValue.get_int32_t(BE);
      EXPECT_EQ(aaidkey, aaIdValue);

      auto asidkey = brrValue.get_int32_t(BE);
      EXPECT_EQ(asidkey, asIdValue);

      auto assetkey = brrValue.get_int32_t(BE);
      EXPECT_EQ(assetkey, assetIdValue);

      try
      {
         BinaryRefReader brrValue2(value);
         auto assetidFromKey = AssetId::deserializeValue(brrValue2);
         EXPECT_EQ(assetidFromKey, assetId1);
         EXPECT_EQ(assetidFromKey, assetId2);
         EXPECT_EQ(assetidFromKey, assetId3);

         auto aaidKey = assetidFromKey.getAddressAccountKey();
         EXPECT_EQ(aaidKey, aaIdValue);
         auto assetidKey = assetidFromKey.getAssetKey();
         EXPECT_EQ(assetIdValue, assetidKey);

         auto acsid = assetidFromKey.getAssetAccountId();
         EXPECT_EQ(acsid, acsId);
         auto asidKey = acsid.getAssetAccountKey();
         EXPECT_EQ(asidKey, asIdValue);

         auto accid = assetidFromKey.getAddressAccountId();
         EXPECT_EQ(accid, accId);
      }
      catch (const exception&)
      {
         EXPECT_TRUE(false);
      }

      auto serValue = READHEX("0C0123456789ABCDEFA0B1C2D3");
      EXPECT_EQ(serValue, value);
   }

   //invalid deser
   try
   {
      auto serKey = READHEX("000123456798AABBCC");
      AssetId::deserializeKey(serKey, 230);
      EXPECT_TRUE(false);
   }
   catch (const IdException&)
   {}

   try
   {
      auto serKey = READHEX("00012345");
      AssetId::deserializeKey(serKey, 0);
      EXPECT_TRUE(false);
   }
   catch (const runtime_error&)
   {}

   try
   {
      auto serKey = READHEX("E60123456789ABCDEF00");
      AssetId::deserializeKey(serKey, 230);
      EXPECT_TRUE(false);
   }
   catch (const runtime_error&)
   {}

   try
   {
      auto serValue = READHEX("000123456789ABCDEF");
      BinaryRefReader brr(serValue);
      AssetId::deserializeValue(brr);
      EXPECT_TRUE(false);
   }
   catch (const IdException&)
   {}

   try
   {
      auto serValue = READHEX("0A0123456789ABCDEF");
      BinaryRefReader brr(serValue);
      AssetId::deserializeValue(brr);
      EXPECT_TRUE(false);
   }
   catch (const runtime_error&)
   {}

   try
   {
      auto serValue = READHEX("080123456789ABCD");
      BinaryRefReader brr(serValue);
      AssetId::deserializeValue(brr);
      EXPECT_TRUE(false);
   }
   catch (const runtime_error&)
   {}

   try
   {
      auto serValue = READHEX("0A0123456789ABCDEFA0B1");
      BinaryRefReader brr(serValue);
      AssetId::deserializeValue(brr);
      EXPECT_TRUE(false);
   }
   catch (const IdException&)
   {}
}

////////////////////////////////////////////////////////////////////////////////
class DerivationTests : public ::testing::Test
{
protected:
   SecureBinaryData seed_ = READHEX("000102030405060708090a0b0c0d0e0f");

protected:
   virtual void SetUp(void)
   {
      homedir_ = std::filesystem::path("./fakehomedir");
      FileUtils::removeDirectory(homedir_);
      std::filesystem::create_directory(homedir_);

      Armory::Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Armory::Config::ProcessType::DB);
   }

   virtual void TearDown(void)
   {
      Armory::Config::reset();
      FileUtils::removeDirectory(homedir_);
   }

   std::filesystem::path homedir_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(DerivationTests, BIP32_Tests)
{
   //m
   {
      //priv ser & deser
      {
         auto&& ext_prv = SecureBinaryData::fromString(
            "xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stbPy6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk33yuGBxrMPHi");

         //ser
         BIP32_Node serObj;
         serObj.initFromSeed(seed_);
         EXPECT_EQ(serObj.getBase58(), ext_prv);
   
         //deser
         BIP32_Node deserObj;
         deserObj.initFromBase58(ext_prv);
         EXPECT_EQ(deserObj.getDepth(), 0);
         EXPECT_EQ(deserObj.getLeafID(), 0ULL);

         EXPECT_EQ(deserObj.getChaincode().toHexStr(), "873dff81c02f525623fd1fe5167eac3a55a049de3d314bb42ee227ffed37d508");

         auto& privkey = deserObj.getPrivateKey();
         EXPECT_EQ(privkey.toHexStr(), "e8f32e723decf4051aefac8e2c93c9c5b214313817cdb01a1494b917c8436b35");
      }

      //pub ser & deser
      {
         auto&& ext_pub = SecureBinaryData::fromString(
            "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8");

         //deser
         BIP32_Node deserObj;
         deserObj.initFromBase58(ext_pub);
         EXPECT_EQ(deserObj.getDepth(), 0);
         EXPECT_EQ(deserObj.getLeafID(), 0ULL);

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
         auto&& ext_prv = SecureBinaryData::fromString(
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
         auto&& ext_pub = SecureBinaryData::fromString(
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
TEST_F(DerivationTests, DerivationTree)
{
   //seed fingerprint is 1234
   vector<uint32_t> path1 = {
      0x80000020,
      0x80005081,
      0x80001111,
   };
   DerivationTree tree(1234);

   //get root branch
   auto& branch = tree.getBranch(0);
   for (const auto& node : path1)
      branch.appendNode(node);

   //resolve paths 1: main branch
   auto resolvedPaths1 = tree.getPaths();
   ASSERT_EQ(resolvedPaths1.size(), 1ULL);
   auto resolvedPaths1_32 = DerivationTree::toPath32(resolvedPaths1[0]);
   EXPECT_EQ(resolvedPaths1_32, path1);

   //fork the tree
   auto& fork1 = tree.forkFromBranch(0);

   //add another node to the main branch
   auto path2 = path1;
   path1.push_back(0x00000781);
   branch.appendNode(path1.back());

   //resolve paths 2: main branch with uninitialized fork
   const auto& resolvedPaths2 = tree.getPaths();
   ASSERT_EQ(resolvedPaths2.size(), 1ULL);
   auto resolvedPaths2_32 = DerivationTree::toPath32(resolvedPaths2[0]);
   EXPECT_EQ(resolvedPaths2_32, path1);

   //add 2 nodes to the fork
   path2.push_back(0x00000084);
   fork1.appendNode(path2.back());
   path2.push_back(0x00065c11);
   fork1.appendNode(path2.back());

   //resolve paths 3: main branch with a fork
   const auto& resolvedPaths3 = tree.getPaths();
   ASSERT_EQ(resolvedPaths3.size(), 2ULL);
   auto resolvedPaths3_32_1 = DerivationTree::toPath32(resolvedPaths3[0]);
   EXPECT_EQ(resolvedPaths3_32_1, path1);
   auto resolvedPaths3_32_2 = DerivationTree::toPath32(resolvedPaths3[1]);
   EXPECT_EQ(resolvedPaths3_32_2, path2);

   //fork twice at the end of the the main branch
   auto& fork2 = tree.forkFromBranch(0);
   auto path3 = path1;
   path3.push_back(0);
   fork2.appendNode(path3.back());

   auto& fork3 = tree.forkFromBranch(0);
   auto path4 = path1;
   path4.push_back(1);
   fork3.appendNode(path4.back());
   path4.push_back(22);
   fork3.appendNode(path4.back());

   //resolve paths 4: 3 forks, 2 end the main branch, 2 fork from the same node
   const auto& resolvedPaths4 = tree.getPaths();
   ASSERT_EQ(resolvedPaths4.size(), 3ULL);
   auto resolvedPaths4_32_1 = DerivationTree::toPath32(resolvedPaths4[0]);
   EXPECT_EQ(resolvedPaths4_32_1, path2);
   auto resolvedPaths4_32_2 = DerivationTree::toPath32(resolvedPaths4[1]);
   EXPECT_EQ(resolvedPaths4_32_2, path3);
   auto resolvedPaths4_32_3 = DerivationTree::toPath32(resolvedPaths4[2]);
   EXPECT_EQ(resolvedPaths4_32_3, path4);

   //check branch id and depth
   auto checkBranchAndDepth = [](const DerivationBranch::Path& path,
      const vector<pair<uint16_t, uint16_t>>& pathBD)->bool
   {
      if (path.size() != pathBD.size())
         return false;

      auto pathIt = path.begin();
      for (unsigned i=0; i<path.size(); i++)
      {
         if (pathIt->branchId != pathBD[i].first)
            return false;

         if (pathIt->depth != pathBD[i].second)
            return false;

         ++pathIt;
      }
      return true;
   };

   vector<pair<uint16_t, uint16_t>> path2_bd = {
      { 0, 0 },
      { 0, 1 },
      { 0, 2 },
      { 1, 3 },
      { 1, 4 }
   };
   EXPECT_TRUE(checkBranchAndDepth(resolvedPaths4[0], path2_bd));

   vector<pair<uint16_t, uint16_t>> path3_bd = {
      { 0, 0 },
      { 0, 1 },
      { 0, 2 },
      { 0, 3 },
      { 2, 4 }
   };
   EXPECT_TRUE(checkBranchAndDepth(resolvedPaths4[1], path3_bd));

   vector<pair<uint16_t, uint16_t>> path4_bd = {
      { 0, 0 },
      { 0, 1 },
      { 0, 2 },
      { 0, 3 },
      { 3, 4 },
      { 3, 5 }
  };
   EXPECT_TRUE(checkBranchAndDepth(resolvedPaths4[2], path4_bd));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(DerivationTests, DerivationTree_FromSeed)
{
   vector<uint32_t> path = 
   {
      0x80001564,
      0x80001111,
      0x8AEE0003,
      0x81116000,
      5
   };

   vector<uint32_t> fork1 =
   {
      0x85550314,
      0x00000000,
      0x00000054
   };

   vector<uint32_t> fork2 =
   {
      0,
      1,
      1,
   };

   vector<uint32_t> fork3 =
   {
      0x80001000,
      0x8ab01000,
      5,
      4
   };

   auto seed = CryptoPRNG::generateRandom(32);
   BIP32_Node rootNode;
   rootNode.initFromSeed(seed);

   DerivationTree tree(rootNode.getThisFingerprint());
   DerivationBranch *f1, *f2, *f3;

   auto& origin = tree.getBranch(0);
   for (unsigned i=0; i<path.size(); i++)
   {
      const auto& node = path[i];
      origin.appendNode(node);

      if (i == 2)
         f1 = &tree.forkFromBranch(0);

      if (i==3)
      {
         f2 = &tree.forkFromBranch(0);
         f3 = &tree.forkFromBranch(0);
      }
   }

   for (const auto& node : fork1)
      f1->appendNode(node);

   for (const auto& node : fork2)
      f2->appendNode(node);

   for (const auto& node : fork3)
      f3->appendNode(node);

   tree.addB58Root(tree.getSeedNode(), rootNode.getBase58());
   auto roots = tree.resolveNodeRoots(nullptr, nullptr);
   ASSERT_EQ(roots.size(), 4ULL);

   auto checkRoot = [&rootNode](
      const vector<uint32_t>& path, const NodeRoot& rootData)->bool
   {
      auto rootNodeCopy = rootNode;
      for (const auto& node : path)
         rootNodeCopy.derivePrivate(node);
      auto b58 = rootNodeCopy.getBase58();

      string b58str(b58.toCharPtr(), b58.getSize());
      string rootStr(rootData.b58Root.toCharPtr(), rootData.b58Root.getSize());
      EXPECT_EQ(b58str, rootStr);

      return b58 == rootData.b58Root;
   };

   /*derive roots locally and compare*/

   //fork1
   vector<uint32_t> pathFork1;
   pathFork1.insert(pathFork1.end(), path.begin(), path.begin() + 3);
   pathFork1.insert(pathFork1.end(), fork1.begin(), fork1.end());

   //fork 2
   vector<uint32_t> pathFork2;
   pathFork2.insert(pathFork2.end(), path.begin(), path.begin() + 4);
   pathFork2.insert(pathFork2.end(), fork2.begin(), fork2.end());

   //fork 3
   vector<uint32_t> pathFork3;
   pathFork3.insert(pathFork3.end(), path.begin(), path.begin() + 4);
   pathFork3.insert(pathFork3.end(), fork3.begin(), fork3.end());

   vector<vector<uint32_t>> paths = 
   {
      path, pathFork1, pathFork2, pathFork3
   };

   //compare
   for (const auto& nodeRoot : roots)
   {
      auto p32 = DerivationTree::toPath32(nodeRoot.path);
      auto pathIt = paths.begin();
      while (pathIt != paths.end())
      {
         if (p32 == *pathIt)
         {
            EXPECT_TRUE(checkRoot(*pathIt, nodeRoot));
            paths.erase(pathIt);
            break;
         }

         ++pathIt;
      }
   }
   ASSERT_TRUE(paths.empty());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(DerivationTests, DerivationTree_FromRoots)
{
   vector<uint32_t> path = 
   {
      0x80001564,
      0x80001111,
      0x8AEE0003,
      0x81116000,
      5
   };

   vector<uint32_t> fork1 =
   {
      0x85550314,
      0x00000000,
      0x00000054
   };

   vector<uint32_t> fork2 =
   {
      0,
      1,
      1,
   };

   vector<uint32_t> fork3 =
   {
      0x80001000,
      0x8ab01000,
      5,
      4
   };

   auto seed = CryptoPRNG::generateRandom(32);
   BIP32_Node rootNode;
   rootNode.initFromSeed(seed);

   DerivationTree tree(rootNode.getThisFingerprint());
   DerivationBranch *f1, *f2, *f3;

   auto& origin = tree.getBranch(0);
   for (unsigned i=0; i<path.size(); i++)
   {
      const auto& node = path[i];
      origin.appendNode(node);

      if (i == 2)
         f1 = &tree.forkFromBranch(0);

      if (i==3)
      {
         f2 = &tree.forkFromBranch(0);
         f3 = &tree.forkFromBranch(0);
      }
   }

   for (const auto& node : fork1)
      f1->appendNode(node);

   for (const auto& node : fork2)
      f2->appendNode(node);

   for (const auto& node : fork3)
      f3->appendNode(node);

   auto checkRoot = [&rootNode](
      const vector<uint32_t>& path,
      const NodeRoot& rootData)->bool
   {
      auto rootNodeCopy = rootNode;
      for (const auto& node : path)
         rootNodeCopy.derivePrivate(node);
      auto b58 = rootNodeCopy.getBase58();

      string b58str(b58.toCharPtr(), b58.getSize());
      string rootStr(rootData.b58Root.toCharPtr(), rootData.b58Root.getSize());
      EXPECT_EQ(b58str, rootStr);

      return b58 == rootData.b58Root;
   };

   /*derive roots locally and compare*/

   //fork1
   vector<uint32_t> pathFork1;
   pathFork1.insert(pathFork1.end(), path.begin(), path.begin() + 3);
   pathFork1.insert(pathFork1.end(), fork1.begin(), fork1.end());

   //fork 2
   vector<uint32_t> pathFork2;
   pathFork2.insert(pathFork2.end(), path.begin(), path.begin() + 4);
   pathFork2.insert(pathFork2.end(), fork2.begin(), fork2.end());

   //fork 3
   vector<uint32_t> pathFork3;
   pathFork3.insert(pathFork3.end(), path.begin(), path.begin() + 4);
   pathFork3.insert(pathFork3.end(), fork3.begin(), fork3.end());

   vector<vector<uint32_t>> paths = 
   {
      path, pathFork1, pathFork2, pathFork3
   };

   auto rootNode3 = rootNode;
   for (unsigned i=0; i<3; i++)
      rootNode3.derivePrivate(path[i]);
   tree.addB58Root(origin.getNodeByRelativeDepth(2), rootNode3.getBase58());

   auto rootNode5 = rootNode;
   for (unsigned i=0; i<5; i++)
      rootNode5.derivePrivate(pathFork2[i]);
   tree.addB58Root(f2->getNodeByRelativeDepth(0), rootNode5.getBase58());

   auto roots = tree.resolveNodeRoots(nullptr, nullptr);
   ASSERT_EQ(roots.size(), 4ULL);

   //compare
   for (const auto& nodeRoot : roots)
   {
      auto p32 = DerivationTree::toPath32(nodeRoot.path);
      auto pathIt = paths.begin();
      while (pathIt != paths.end())
      {
         if (p32 == *pathIt)
         {
            EXPECT_TRUE(checkRoot(*pathIt, nodeRoot));
            paths.erase(pathIt);
            break;
         }

         ++pathIt;
      }
   }
   ASSERT_TRUE(paths.empty());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(DerivationTests, DerivationTree_FromPublicRoots)
{
   vector<uint32_t> path = 
   {
      0x80001564,
      0x80001111,
      0x8AEE0003,
      0x81116000,
      5
   };

   vector<uint32_t> fork1 =
   {
      0x85550314,
      0x00000000,
      0x00000054
   };

   vector<uint32_t> fork2 =
   {
      0,
      1,
      1,
   };

   vector<uint32_t> fork3 =
   {
      0x80001000,
      0x8ab01000,
      5,
      4
   };

   auto seed = CryptoPRNG::generateRandom(32);
   BIP32_Node rootNode;
   rootNode.initFromSeed(seed);

   DerivationTree tree(rootNode.getThisFingerprint());
   DerivationBranch *f1, *f2, *f3;

   auto& origin = tree.getBranch(0);
   for (unsigned i=0; i<path.size(); i++)
   {
      const auto& node = path[i];
      origin.appendNode(node);

      if (i == 2)
         f1 = &tree.forkFromBranch(0);

      if (i==3)
      {
         f2 = &tree.forkFromBranch(0);
         f3 = &tree.forkFromBranch(0);
      }
   }

   for (const auto& node : fork1)
      f1->appendNode(node);

   for (const auto& node : fork2)
      f2->appendNode(node);

   for (const auto& node : fork3)
      f3->appendNode(node);

   auto checkRoot = [&rootNode](
      const vector<uint32_t>& path,
      const NodeRoot& rootData)->bool
   {
      auto rootNodeCopy = rootNode;
      for (const auto& node : path)
         rootNodeCopy.derivePrivate(node);
      auto rootNodePub = rootNodeCopy.getPublicCopy();
      auto b58 = rootNodePub.getBase58();

      string b58str(b58.toCharPtr(), b58.getSize());
      string rootStr(rootData.b58Root.toCharPtr(), rootData.b58Root.getSize());
      EXPECT_EQ(b58str, rootStr);

      return b58 == rootData.b58Root;
   };

   /*derive roots locally and compare*/

   //fork1
   vector<uint32_t> pathFork1;
   pathFork1.insert(pathFork1.end(), path.begin(), path.begin() + 3);
   pathFork1.insert(pathFork1.end(), fork1.begin(), fork1.end());

   //fork 2
   vector<uint32_t> pathFork2;
   pathFork2.insert(pathFork2.end(), path.begin(), path.begin() + 4);
   pathFork2.insert(pathFork2.end(), fork2.begin(), fork2.end());

   //fork 3
   vector<uint32_t> pathFork3;
   pathFork3.insert(pathFork3.end(), path.begin(), path.begin() + 4);
   pathFork3.insert(pathFork3.end(), fork3.begin(), fork3.end());

   vector<vector<uint32_t>> paths = 
   {
      path, pathFork1, pathFork2, pathFork3
   };

   //this one should work for main path and fork2: grab the
   //root last hard derivation in main path (all derivations
   //in main and f2 are soft past this point)
   //will fail for f3&4
   auto rootNodePath = rootNode;
   for (unsigned i=0; i<4; i++)
      rootNodePath.derivePrivate(path[i]);
   auto rootNodePath_public = rootNodePath.getPublicCopy();
   ASSERT_TRUE(rootNodePath_public.isPublic());
   tree.addB58Root(origin.getNodeByRelativeDepth(3),
      rootNodePath_public.getBase58());

   //this one should work: grab root for first soft derivation
   auto rootNodeF1 = rootNode;
   for (unsigned i=0; i<5; i++)
      rootNodeF1.derivePrivate(pathFork1[i]);
   auto rootNodeF1_public = rootNodeF1.getPublicCopy();
   ASSERT_TRUE(rootNodeF1_public.isPublic());
   tree.addB58Root(f1->getNodeByRelativeDepth(1),
      rootNodeF1_public.getBase58());

   //this one should fail: grab root for next to last hard derivation
   auto rootNodeF3 = rootNode;
   for (unsigned i=0; i<5; i++)
      rootNodeF3.derivePrivate(pathFork3[i]);
   auto rootNodeF3_public = rootNodeF3.getPublicCopy();
   ASSERT_TRUE(rootNodeF3_public.isPublic());
   tree.addB58Root(f3->getNodeByRelativeDepth(0),
      rootNodeF3_public.getBase58());


   //resolve the roots
   auto roots = tree.resolveNodeRoots(nullptr, nullptr);
   ASSERT_EQ(roots.size(), 4ULL);

   //compare
   auto pathsCopy = paths;
   for (const auto& nodeRoot : roots)
   {
      auto p32 = DerivationTree::toPath32(nodeRoot.path);
      auto pathIt = pathsCopy.begin();
      while (pathIt != pathsCopy.end())
      {
         if (p32 == *pathIt)
         {
            if (pathIt == prev(pathsCopy.end()))
               EXPECT_FALSE(nodeRoot.isInitialized());
            else
               EXPECT_TRUE(checkRoot(*pathIt, nodeRoot));

            pathsCopy.erase(pathIt);
            break;
         }

         ++pathIt;
      }
   }
   ASSERT_TRUE(pathsCopy.empty());

   //add tail public root for f3 and compare again, all should match
   rootNodeF3 = rootNode;
   for (auto& node : pathFork3)
      rootNodeF3.derivePrivate(node);
   rootNodeF3_public = rootNodeF3.getPublicCopy();
   ASSERT_TRUE(rootNodeF3_public.isPublic());
   tree.addB58Root(f3->getNodeByRelativeDepth(3),
      rootNodeF3_public.getBase58());

   roots = tree.resolveNodeRoots(nullptr, nullptr);
   ASSERT_EQ(roots.size(), 4ULL);

   //compare
   for (const auto& nodeRoot : roots)
   {
      auto p32 = DerivationTree::toPath32(nodeRoot.path);
      auto pathIt = paths.begin();
      while (pathIt != paths.end())
      {
         if (p32 == *pathIt)
         {
            EXPECT_TRUE(checkRoot(*pathIt, nodeRoot));
            paths.erase(pathIt);
            break;
         }

         ++pathIt;
      }
   }
   ASSERT_TRUE(paths.empty());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(DerivationTests, DerivationTree_FromWalletRoot)
{
   vector<uint32_t> path = 
   {
      0x80001564,
      0x80001111,
      0x8AEE0003,
      0x81116000,
      5
   };

   vector<uint32_t> fork1 =
   {
      0x85550314,
      0x00000000,
      0x00000054
   };

   vector<uint32_t> fork2 =
   {
      0,
      1,
      1,
   };

   vector<uint32_t> fork3 =
   {
      0x80001000,
      0x8ab01000,
      5,
      4
   };

   auto seed = CryptoPRNG::generateRandom(32);
   BIP32_Node rootNode;
   rootNode.initFromSeed(seed);

   shared_ptr<AssetEntry_BIP32Root> rootPtr;
   shared_ptr<DecryptedDataContainer> decrData;

   {
      //generate bip32 encrypted root
      auto whs = make_shared<IO::WalletHeader_Single>(
         Armory::Config::BitcoinSettings::getMagicBytes());
      whs->walletID_ = "abc";
      auto mks = IO::WalletDBInterface::initWalletHeaderObject(whs, {}, 0);

      auto rootCipher = mks.cipher_->getCopy(
         whs->masterEncryptionKeyId_);
      auto encryptedRoot = rootCipher->encrypt(
         mks.decryptedMasterKey_.get(),
         rootCipher->getKdfId(),
         rootNode.getPrivateKey());

      auto cipherData =
         make_unique<CipherData>(encryptedRoot, move(rootCipher));
      auto rootAsset = make_shared<Asset_PrivateKey>(
         AssetId::getRootAssetId(), move(cipherData));

      auto pubkey = rootNode.getPublicKey();
      auto chaincode = rootNode.getChaincode();
      rootPtr = make_unique<AssetEntry_BIP32Root>(
         AssetId::getRootAssetId(),
         pubkey, rootAsset,
         chaincode, 0, 0, 0, rootNode.getThisFingerprint(), vector<uint32_t>());

      decrData = make_shared<DecryptedDataContainer>(
         nullptr, "",
         whs->defaultEncryptionKey_, whs->defaultEncryptionKeyId_,
         whs->defaultKdfId_, whs->masterEncryptionKeyId_);
      decrData->addKdf(mks.kdf_);
      decrData->addEncryptionKey(mks.masterKey_);
   }

   DerivationTree tree(rootNode.getThisFingerprint());
   DerivationBranch *f1, *f2, *f3;

   auto& origin = tree.getBranch(0);
   for (unsigned i=0; i<path.size(); i++)
   {
      const auto& node = path[i];
      origin.appendNode(node);

      if (i == 2)
         f1 = &tree.forkFromBranch(0);

      if (i==3)
      {
         f2 = &tree.forkFromBranch(0);
         f3 = &tree.forkFromBranch(0);
      }
   }

   for (const auto& node : fork1)
      f1->appendNode(node);

   for (const auto& node : fork2)
      f2->appendNode(node);

   for (const auto& node : fork3)
      f3->appendNode(node);

   vector<NodeRoot> roots;
   {
      ReentrantLock lock(decrData.get());
      roots = move(tree.resolveNodeRoots(decrData, rootPtr));
      ASSERT_EQ(roots.size(), 4ULL);
   }

   auto checkRoot = [&rootNode](
      const vector<uint32_t>& path,
      const NodeRoot& rootData)->bool
   {
      auto rootNodeCopy = rootNode;
      for (const auto& node : path)
         rootNodeCopy.derivePrivate(node);
      auto b58 = rootNodeCopy.getBase58();

      string b58str(b58.toCharPtr(), b58.getSize());
      string rootStr(rootData.b58Root.toCharPtr(), rootData.b58Root.getSize());
      EXPECT_EQ(b58str, rootStr);

      return b58 == rootData.b58Root;
   };

   /*derive roots locally and compare*/

   //fork1
   vector<uint32_t> pathFork1;
   pathFork1.insert(pathFork1.end(), path.begin(), path.begin() + 3);
   pathFork1.insert(pathFork1.end(), fork1.begin(), fork1.end());

   //fork 2
   vector<uint32_t> pathFork2;
   pathFork2.insert(pathFork2.end(), path.begin(), path.begin() + 4);
   pathFork2.insert(pathFork2.end(), fork2.begin(), fork2.end());

   //fork 3
   vector<uint32_t> pathFork3;
   pathFork3.insert(pathFork3.end(), path.begin(), path.begin() + 4);
   pathFork3.insert(pathFork3.end(), fork3.begin(), fork3.end());

   vector<vector<uint32_t>> paths = 
   {
      path, pathFork1, pathFork2, pathFork3
   };

   //compare
   for (const auto& nodeRoot : roots)
   {
      auto p32 = DerivationTree::toPath32(nodeRoot.path);
      auto pathIt = paths.begin();
      while (pathIt != paths.end())
      {
         if (p32 == *pathIt)
         {
            EXPECT_TRUE(checkRoot(*pathIt, nodeRoot));
            paths.erase(pathIt);
            break;
         }

         ++pathIt;
      }
   }
   ASSERT_TRUE(paths.empty());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(DerivationTests, DerivationTree_MergePaths)
{
   vector<uint32_t> path = 
   {
      0x80001564,
      0x80001111,
      0x8AEE0003,
      0x81116000,
      5
   };

   vector<uint32_t> fork1 =
   {
      0x85550314,
      0x00000000,
      0x00000054
   };

   vector<uint32_t> fork2 =
   {
      0,
      1,
      1,
   };

   vector<uint32_t> fork3 =
   {
      0x80001000,
      0x8ab01000,
      5,
      4
   };

   vector<uint32_t> p0 = path;

   vector<uint32_t> p1 = {
      path[0], path[1], path[2],
      fork1[0], fork1[1], fork1[2]
   };

   vector<uint32_t> p2 {
      path[0], path[1], path[2], path[3],
      fork2[0], fork2[1], fork2[2]
   };

   vector<uint32_t> p3 {
      path[0], path[1], path[2], path[3],
      fork3[0], fork3[1], fork3[2], fork3[3]
   };

   vector<vector<uint32_t>> pathVec = { p0, p1, p2, p3 };
   auto derTree = DerivationTree::fromDerivationPaths(1234, pathVec);
   auto treePaths = derTree.getPaths();

   for (auto& pathIt : treePaths)
   {
      bool collision = false;
      auto path32 = DerivationTree::toPath32(pathIt);

      auto pathVecIt = pathVec.begin();
      while (pathVecIt != pathVec.end())
      {
         if (path32 == *pathVecIt)
         {
            pathVec.erase(pathVecIt);
            collision = true;
            break;
         }
         ++pathVecIt;
      }

      ASSERT_TRUE(collision);
   }

   EXPECT_TRUE(pathVec.empty());
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class AddressEntryTest : public ::testing::Test
{
protected:
   virtual void SetUp(void)
   {
      homedir_ = std::filesystem::path("./fakehomedir");
      FileUtils::removeDirectory(homedir_);
      std::filesystem::create_directory(homedir_);

      Armory::Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Armory::Config::ProcessType::DB);
   }

   virtual void TearDown(void)
   {
      Armory::Config::reset();
      FileUtils::removeDirectory(homedir_);
   }

   std::filesystem::path homedir_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(AddressEntryTest, P2PKH)
{
   auto privKey = CryptoPRNG::generateRandom(32);
   auto pubKey = CryptoECDSA().ComputePublicKey(privKey, false);

   auto pubKeyCopy = pubKey; //assetentry ctor moves in crypto assets
   auto assetPtr = 
      make_shared<AssetEntry_Single>(AssetId(0, 0, 0), pubKeyCopy, nullptr);

   //uncompressed
   AddressEntry_P2PKH address(assetPtr, false);
   auto addrStr = address.getAddress();

   auto scrAddrUnc = BtcUtils::getHash160(pubKey);
   BinaryWriter bw;
   bw.put_uint8_t(BitcoinSettings::getPubkeyHashPrefix());
   bw.put_BinaryData(scrAddrUnc);
   auto addrB58 = BtcUtils::scrAddrToBase58(bw.getData());

   EXPECT_EQ(addrB58, addrStr);

   //compressed
   AddressEntry_P2PKH addressCmp(assetPtr, true);
   auto addrStrCmp = addressCmp.getAddress();
   auto pubKeyCmp = CryptoECDSA().CompressPoint(pubKey);

   auto scrAddrCmp = BtcUtils::getHash160(pubKeyCmp);
   BinaryWriter bwCmp;
   bwCmp.put_uint8_t(BitcoinSettings::getPubkeyHashPrefix());
   bwCmp.put_BinaryData(scrAddrCmp);
   auto addrB58Cmp = BtcUtils::scrAddrToBase58(bwCmp.getData());

   EXPECT_EQ(addrB58Cmp, addrStrCmp);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(AddressEntryTest, P2WPKH)
{
   auto privKey = CryptoPRNG::generateRandom(32);
   auto pubKey = CryptoECDSA().ComputePublicKey(privKey, true);

   auto pubKeyCopy = pubKey; //assetentry ctor moves in crypto assets
   auto assetPtr =
      make_shared<AssetEntry_Single>(AssetId(0, 0, 0), pubKeyCopy, nullptr);

   //sw enforces compressed pubkeys
   AddressEntry_P2WPKH address(assetPtr);
   auto addrStr = address.getAddress();

   auto scrAddr = BtcUtils::getHash160(pubKey);
   auto addrBech32 = BtcUtils::scrAddrToSegWitAddress(scrAddr);

   EXPECT_EQ(addrBech32, addrStr);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(AddressEntryTest, P2SH)
{
   auto privKey = CryptoPRNG::generateRandom(32);
   auto pubKey = CryptoECDSA().ComputePublicKey(privKey, true);

   auto pubKeyCopy = pubKey; //assetentry ctor moves in crypto assets
   auto assetPtr =
      make_shared<AssetEntry_Single>(AssetId(0, 0, 0), pubKeyCopy, nullptr);

   {
      //p2sh-p2pk
      auto address = make_shared<AddressEntry_P2PK>(assetPtr, true);
      AddressEntry_P2SH nested(address);
      auto addrStr = nested.getAddress();

      BinaryWriter bwScript;
      bwScript.put_uint8_t(33);
      bwScript.put_BinaryData(pubKey);
      bwScript.put_uint8_t(OP_CHECKSIG);

      auto scriptHash = BtcUtils::getHash160(bwScript.getData());
      
      BinaryWriter bw;
      bw.put_uint8_t(BitcoinSettings::getScriptHashPrefix());
      bw.put_BinaryData(scriptHash);
      auto addrB58 = BtcUtils::scrAddrToBase58(bw.getData());

      EXPECT_EQ(addrB58, addrStr);
   }

   //p2sh-p2wpkh
   {
      auto address = make_shared<AddressEntry_P2WPKH>(assetPtr);
      AddressEntry_P2SH nested(address);
      auto addrStr = nested.getAddress();

      auto pubkeyHash = BtcUtils::getHash160(pubKey);

      BinaryWriter bwScript;
      bwScript.put_uint8_t(0);
      bwScript.put_uint8_t(20);
      bwScript.put_BinaryData(pubkeyHash);
   
      auto scriptHash = BtcUtils::getHash160(bwScript.getData());

      BinaryWriter bw;
      bw.put_uint8_t(BitcoinSettings::getScriptHashPrefix());
      bw.put_BinaryData(scriptHash);
      auto addrB58 = BtcUtils::scrAddrToBase58(bw.getData());

      EXPECT_EQ(addrB58, addrStr);
   }

   //p2sh-p2wsh-ms
   {
      map<BinaryData, SecureBinaryData> pubKeys;

      for (unsigned i = 0; i < 3; i++)
      {
         auto privKey = CryptoPRNG::generateRandom(32);
         auto pubKey = CryptoECDSA().ComputePublicKey(privKey, true);

         stringstream ss;
         ss << "wallet" << i;

         auto dataPair = make_pair(BinaryData::fromString(ss.str()), move(pubKey));
         pubKeys.emplace(dataPair);
      }

      map<BinaryData, shared_ptr<AssetEntry>> assetMap;
      unsigned i = 0;
      for (auto pubKey : pubKeys)
      {
         auto asset = make_shared<AssetEntry_Single>(
            AssetId(0, 0, i++), pubKey.second, nullptr);

         assetMap.emplace(make_pair(pubKey.first, asset));
      }

      auto assetMs = make_shared<AssetEntry_Multisig>(
         AssetId(0, 0, 0), assetMap, 2, 3);
      auto addressMs = make_shared<AddressEntry_Multisig>(assetMs, true);
      auto nested = make_shared<AddressEntry_P2SH>(addressMs);
      auto addr = nested->getAddress();

      BinaryWriter bw;
      bw.put_uint8_t(OP_2);

      for (auto& pubKey : pubKeys)
      {
         bw.put_uint8_t(33);
         bw.put_BinaryData(pubKey.second);
      }

      bw.put_uint8_t(OP_3);
      bw.put_uint8_t(OP_CHECKMULTISIG);

      auto scriptHash = BtcUtils::getHash160(bw.getData());
      BinaryWriter bwScrAddr;
      bwScrAddr.put_uint8_t(BitcoinSettings::getScriptHashPrefix());
      bwScrAddr.put_BinaryData(scriptHash);
      auto addrB58 = BtcUtils::scrAddrToBase58(bwScrAddr.getData());

      EXPECT_EQ(addrB58, addr);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(AddressEntryTest, P2WSH)
{
   map<BinaryData, SecureBinaryData> pubKeys;

   for (unsigned i = 0; i < 3; i++)
   {
      auto privKey = CryptoPRNG::generateRandom(32);
      auto pubKey = CryptoECDSA().ComputePublicKey(privKey, true);

      stringstream ss;
      ss << "wallet" << i;

      auto dataPair = make_pair(BinaryData::fromString(ss.str()), move(pubKey));
      pubKeys.emplace(dataPair);
   }

   map<BinaryData, shared_ptr<AssetEntry>> assetMap;
   unsigned i = 0;
   for (auto pubKey : pubKeys)
   {
      auto asset = make_shared<AssetEntry_Single>(
         AssetId(0, 0, i++), pubKey.second, nullptr);

      assetMap.emplace(make_pair(pubKey.first, asset));
   }

   auto assetMs = make_shared<AssetEntry_Multisig>(
      AssetId(0, 0, 0), assetMap, 2, 3);
   auto addressMs = make_shared<AddressEntry_Multisig>(assetMs, true);
   auto nested = make_shared<AddressEntry_P2WSH>(addressMs);
   auto addr = nested->getAddress();

   BinaryWriter bw;
   bw.put_uint8_t(OP_2);

   for (auto& pubKey : pubKeys)
   {
      bw.put_uint8_t(33);
      bw.put_BinaryData(pubKey.second);
   }

   bw.put_uint8_t(OP_3);
   bw.put_uint8_t(OP_CHECKMULTISIG);

   auto scriptHash = BtcUtils::getSha256(bw.getData());
   auto addrBech32 = BtcUtils::scrAddrToSegWitAddress(scriptHash);

   EXPECT_EQ(addr, addrBech32);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class WalletInterfaceTest : public ::testing::Test
{
protected:
   std::filesystem::path homedir_;
   std::filesystem::path dbPath_;
   BinaryData allZeroes16_;

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      homedir_ = std::filesystem::path("./fakehomedir");
      FileUtils::removeDirectory(homedir_);
      std::filesystem::create_directory(homedir_);

      dbPath_ = homedir_ / "wallet_test.wallet";
      Armory::Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Armory::Config::ProcessType::DB);

      allZeroes16_ = READHEX("00000000000000000000000000000000");
      if(allZeroes16_.getSize() != 16)
         throw runtime_error("failed to setup proper zeroed benchmark value");
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      Armory::Config::reset();
      FileUtils::removeDirectory(homedir_);
   }

   /////////////////////////////////////////////////////////////////////////////
   map<BinaryData, BinaryData> getAllEntries(shared_ptr<LMDBEnv> dbEnv, LMDB& db)
   {
      map<BinaryData, BinaryData> keyValMap;
      
      auto tx = LMDBEnv::Transaction(dbEnv.get(), LMDB::ReadOnly);
      auto iter = db.begin();
      while(iter.isValid())
      {
         auto keyData = iter.key();
         auto valData = iter.value();

         BinaryData keyBd((uint8_t*)keyData.mv_data, keyData.mv_size);
         BinaryData valBd((uint8_t*)valData.mv_data, valData.mv_size);

         keyValMap.insert(make_pair(keyBd, valBd));
         iter.advance();
      }

      return keyValMap;
   }

   /////////////////////////////////////////////////////////////////////////////
   struct BadKeyException
   {};

   ////
   set<unsigned> tallyGaps(const map<BinaryData, BinaryData>& keyValMap)
   {
      set<unsigned> gaps;
      int prevKeyInt = -1;

      for(auto& keyVal : keyValMap)
      {
         if(keyVal.first.getSize() != 4)
            throw BadKeyException();

         int keyInt = READ_UINT32_BE(keyVal.first);
         if(keyInt - prevKeyInt != 1)
         {
            for(int i=prevKeyInt + 1; i<keyInt; i++)
               gaps.insert((unsigned)i);
         }

         prevKeyInt = keyInt;
      }

      return gaps;
   }

   /////////////////////////////////////////////////////////////////////////////
   struct IESPacket
   {
      SecureBinaryData pubKey_;
      SecureBinaryData iv_;
      SecureBinaryData cipherText_;

      BinaryData dbKey_;
   };

   ////
   IESPacket getIESData(const pair<BinaryData, BinaryData>& keyVal)
   {
      IESPacket result;

      BinaryRefReader brr(keyVal.second.getRef());
      result.pubKey_ = brr.get_SecureBinaryData(33);
      result.iv_ = brr.get_SecureBinaryData(16);
      result.cipherText_ = brr.get_SecureBinaryData(brr.getSizeRemaining());

      result.dbKey_ = keyVal.first;

      return result;
   }

   /////////////////////////////////////////////////////////////////////////////
   pair<SecureBinaryData, SecureBinaryData> generateKeyPair(
      const SecureBinaryData& saltedRoot, unsigned ctr)
   {
      SecureBinaryData hmacKey((uint8_t*)&ctr, 4);
      auto hmacVal = BtcUtils::getHMAC512(hmacKey, saltedRoot);

      //first half is the encryption key, second half is the hmac key
      BinaryRefReader brr(hmacVal.getRef());
      auto&& decrPrivKey = brr.get_SecureBinaryData(32);
      auto&& macKey = brr.get_SecureBinaryData(32);

      //decryption private key sanity check
      if (!CryptoECDSA::checkPrivKeyIsValid(decrPrivKey))
         throw IO::WalletInterfaceException("invalid decryption private key");

      return make_pair(move(decrPrivKey), move(macKey));
   }

   /////////////////////////////////////////////////////////////////////////////
   struct LooseEntryException
   {};

   struct HMACMismatchException
   {};

   ////
   BinaryData computeHmac(const BinaryData& dbKey, 
      const BinaryData& dataKey, const BinaryData& dataVal,
      const SecureBinaryData& macKey)
   {
      BinaryWriter bw;
      bw.put_var_int(dataKey.getSize());
      bw.put_BinaryData(dataKey);

      bw.put_var_int(dataVal.getSize());
      bw.put_BinaryData(dataVal);

      bw.put_BinaryData(dbKey);
      
      return BtcUtils::getHMAC256(macKey, bw.getData());
   }

   ////
   pair<BinaryData, BinaryData> decryptPair(const IESPacket& packet,
      const SecureBinaryData& privKey, const SecureBinaryData& macKey)
   {
      //generate decryption key
      auto ecdhPubKey = 
         CryptoECDSA::PubKeyScalarMultiply(packet.pubKey_, privKey);
      auto decrKey = BtcUtils::hash256(ecdhPubKey);

      //decrypt packet
      auto payload = CryptoAES::DecryptCBC(
         packet.cipherText_, decrKey, packet.iv_);

      //break down payload
      BinaryRefReader brr(payload.getRef());
      auto&& hmac = brr.get_SecureBinaryData(32);
      auto len = brr.get_var_int();
      auto&& dataKey = brr.get_BinaryData(len);
      len = brr.get_var_int();
      auto&& dataVal = brr.get_BinaryData(len);

      //sanity check
      if (brr.getSizeRemaining() > 0)
         throw LooseEntryException();

      //compute hmac
      auto&& computedHmac = computeHmac(
         packet.dbKey_, dataKey, dataVal, macKey);
      
      if (computedHmac != hmac)
         throw HMACMismatchException();

      return make_pair(dataKey, dataVal);
   }

   ////
   pair<BinaryData, BinaryData> decryptPair(const IESPacket& packet,
      const pair<SecureBinaryData, SecureBinaryData>& keyPair)
   {
      return decryptPair(packet, keyPair.first, keyPair.second);
   }


   /////////////////////////////////////////////////////////////////////////////
   BinaryData getErasurePacket(unsigned dbKeyInt)
   {
      BinaryWriter packet;
      packet.put_String("erased");
      packet.put_var_int(4);
      packet.put_uint32_t(dbKeyInt, BE);

      return packet.getData();
   }
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletInterfaceTest, WalletIfaceTransaction_Test)
{
   //utils
   auto checkVals = [](IO::WalletIfaceTransaction& tx, 
      map<BinaryData, BinaryData>& keyValMap)->bool
   {
      for (auto& keyVal : keyValMap)
      {
         auto val = tx.getDataRef(keyVal.first);
         if (val != keyVal.second)
            return false;
      }
      
      return true;
   };

   //setup db env
   auto dbEnv = make_shared<LMDBEnv>();
   dbEnv->open(dbPath_, 0);
   auto filename = dbEnv->getFilename();
   ASSERT_EQ(filename, dbPath_);

   auto&& controlSalt = CryptoPRNG::generateRandom(32);
   auto&& rawRoot = CryptoPRNG::generateRandom(32);
   string dbName("test");

   //setup db
   auto dbIface = make_shared<IO::DBInterface>(
      dbEnv.get(), dbName, controlSalt, ENCRYPTION_TOPLAYER_VERSION);
   dbIface->loadAllEntries(rawRoot); 

   //commit some values
   map<BinaryData, BinaryData> keyValMap;
   for (unsigned i=0; i<50; i++)
   {
      keyValMap.insert(make_pair(
         CryptoPRNG::generateRandom(20),
         CryptoPRNG::generateRandom(80)
      ));
   }

   {
      //add the values
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), true);
      auto mapToWrite = keyValMap;
      for (auto& keyVal : mapToWrite)
         tx.insert(keyVal.first, keyVal.second);
      
      //try to grab them from the live write tx
      EXPECT_TRUE(checkVals(tx, keyValMap));

      //try to create read tx, should fail
      try
      {
         IO::WalletIfaceTransaction readTx(nullptr, dbIface.get(), false);
         ASSERT_TRUE(false);
      }
      catch (IO::WalletInterfaceException& e)
      {
         EXPECT_EQ(e.what(), string("failed to create db tx"));
      }

      //check data map isn't affected
      EXPECT_TRUE(checkVals(tx, keyValMap));

      //create nested write tx, shouldn't affect anything
      {
         IO::WalletIfaceTransaction txInner(nullptr, dbIface.get(), true);

         //check data map isn't affected
         EXPECT_TRUE(checkVals(tx, keyValMap));

         //should be able to check modification map from this tx
         EXPECT_TRUE(checkVals(txInner, keyValMap));
      }

      //check closing inner tx has no effect on parent
      EXPECT_TRUE(checkVals(tx, keyValMap));
   }

   {
      //check data them from read tx
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), false);
      EXPECT_TRUE(checkVals(tx, keyValMap));

      //check them from nested read tx
      {
         IO::WalletIfaceTransaction tx2(nullptr, dbIface.get(), false);
         EXPECT_TRUE(checkVals(tx2, keyValMap));
         EXPECT_TRUE(checkVals(tx, keyValMap));
      }

      //closing nested tx shouldn't affect parent
      EXPECT_TRUE(checkVals(tx, keyValMap));

      //should fail to open write tx while read tx is live
      try
      {
         IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), true);
         ASSERT_TRUE(false);
      }
      catch (IO::WalletInterfaceException& e)
      {
         EXPECT_EQ(e.what(), string("failed to create db tx"));
      }

      //failed write tx shouldn't affect read tx
      EXPECT_TRUE(checkVals(tx, keyValMap));
   }

   {
      //modify db
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), true);

      {
         //nest tx
         IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), true);
         auto iter = keyValMap.begin();
         for (unsigned i=0; i<10; i++)
            ++iter;
         iter->second = CryptoPRNG::generateRandom(35);
         auto valToWrite = iter->second;
         tx.insert(iter->first, valToWrite);

         for (unsigned i=0; i<10; i++)
            ++iter;
         iter->second = CryptoPRNG::generateRandom(70);
         auto valToWrite2 = iter->second;
         tx.insert(iter->first, valToWrite2);
      }

      auto pair1 = make_pair(
         CryptoPRNG::generateRandom(40),
         CryptoPRNG::generateRandom(80));
      auto pair2 = make_pair(
         CryptoPRNG::generateRandom(20),
         CryptoPRNG::generateRandom(16));

      tx.insert(pair1.first, pair1.second);
      tx.insert(pair2.first, pair2.second);

      //check data
      EXPECT_TRUE(checkVals(tx, keyValMap));
   }

   //check data after commit
   IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), false);
   EXPECT_TRUE(checkVals(tx, keyValMap));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletInterfaceTest, WalletIfaceTransaction_Concurrency_Test)
{
   //setup env
   auto dbEnv = make_shared<LMDBEnv>(3);
   dbEnv->open(dbPath_, 0);
   auto filename = dbEnv->getFilename();
   ASSERT_EQ(filename, dbPath_);

   auto&& controlSalt = CryptoPRNG::generateRandom(32);
   auto&& rawRoot = CryptoPRNG::generateRandom(32);
   string dbName("test");

   auto dbIface = make_shared<IO::DBInterface>(
      dbEnv.get(), dbName, controlSalt, ENCRYPTION_TOPLAYER_VERSION);

   //sanity check
   ASSERT_EQ(dbIface->getEntryCount(), 0U);
   dbIface->loadAllEntries(rawRoot);
   ASSERT_EQ(dbIface->getEntryCount(), 0U);

   map<BinaryData, BinaryData> dataMap1;
   for (unsigned i=0; i<30; i++)
   {
      dataMap1.insert(make_pair(
         CryptoPRNG::generateRandom(20),
         CryptoPRNG::generateRandom(64)));
   }

   map<BinaryData, BinaryData> dataMap2;
   for (unsigned i=0; i<10; i++)
   {
      dataMap2.insert(make_pair(
         CryptoPRNG::generateRandom(25),
         CryptoPRNG::generateRandom(64)));
   }

   map<BinaryData, BinaryData> modifiedMap;
   {
      auto iter = dataMap1.begin();
      for (unsigned i=0; i<8; i++)
         ++iter;

      modifiedMap.insert(make_pair(
         iter->first, 
         CryptoPRNG::generateRandom(48)));

      ++iter; ++iter;
      modifiedMap.insert(make_pair(
         iter->first, 
         CryptoPRNG::generateRandom(60)));

      ++iter; ++iter; ++iter;
      modifiedMap.insert(make_pair(
         iter->first, 
         CryptoPRNG::generateRandom(87)));
   }

   dataMap2.insert(modifiedMap.begin(), modifiedMap.end());

   auto checkDbValues = [](IO::DBIfaceTransaction* tx, 
      map<BinaryData, BinaryData> dataMap)->unsigned
   {
      auto iter = dataMap.begin();
      while (iter != dataMap.end())
      {
         auto dbData = tx->getDataRef(iter->first);
         if (dbData == iter->second.getRef())
         {
            dataMap.erase(iter++);
            continue;
         }

         ++iter;
      }

      return dataMap.size();
   };

   auto finalMap = dataMap2;
   finalMap.insert(dataMap1.begin(), dataMap1.end());

   auto writeThread2 = [&](void)->void
   {
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), true);

      //check dataMap1 is in
      EXPECT_EQ(checkDbValues(&tx, dataMap1), 0U);

      for (auto& dataPair : dataMap2)
         tx.insert(dataPair.first, dataPair.second);

      EXPECT_EQ(checkDbValues(&tx, finalMap), 0U);
   };

   thread* writeThr;

   {
      //create write tx in main thread
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), true);

      //fire second thread with another write tx
      writeThr = new thread(writeThread2);

      //check db is empty
      EXPECT_EQ(checkDbValues(&tx, dataMap1), dataMap1.size());

      //modify db through main thread
      auto mapToWrite = dataMap1;
      for (auto& dataPair : mapToWrite)
         tx.insert(dataPair.first, dataPair.second);

      //check values
      EXPECT_EQ(checkDbValues(&tx, dataMap1), 0U);
   }

   //wait on 2nd thread
   writeThr->join();
   delete writeThr;

   {
      //check db is consistent with main thread -> 2nd thread modification order
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), false);
      EXPECT_EQ(checkDbValues(&tx, finalMap), 0U);
   }
   
   /***********/

   //check read tx consistency while write tx is live
   map<BinaryData, BinaryData> dataMap5;
   for (unsigned i=0; i<10; i++)
   {
      dataMap5.insert(make_pair(
         CryptoPRNG::generateRandom(25),
         CryptoPRNG::generateRandom(64)));
   }

   {
      auto iter = finalMap.begin();
      for (unsigned i=0; i<25; i++)
         ++iter;
      
      dataMap5.insert(make_pair(
         iter->first,
         CryptoPRNG::generateRandom(50)));

      ++iter; ++iter;
      dataMap5.insert(make_pair(
         iter->first,
         CryptoPRNG::generateRandom(65)));
   }

   auto finalMap2 = dataMap5;
   finalMap2.insert(finalMap.begin(), finalMap.end());

   auto writeThread4 = [&](void)->void
   {
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), true);
      EXPECT_EQ(checkDbValues(&tx, finalMap), 0U);

      for (auto& dataPair : dataMap5)
         tx.insert(dataPair.first, dataPair.second);

      EXPECT_EQ(checkDbValues(&tx, finalMap2), 0U);
   };

   //create read tx
   {
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), false);
      EXPECT_EQ(checkDbValues(&tx, finalMap), 0U);

      //create write thread
      thread writeThr4(writeThread4);
      EXPECT_EQ(checkDbValues(&tx, finalMap), 0U);

      writeThr4.join();

      //data for this read tx should be unchanged
      EXPECT_EQ(checkDbValues(&tx, finalMap), 0U);
   }

   //final check
   IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), false);
   EXPECT_EQ(checkDbValues(&tx, finalMap2), 0U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletInterfaceTest, EncryptionTest)
{
   auto dbEnv = make_shared<LMDBEnv>();
   dbEnv->open(dbPath_, 0);
   auto filename = dbEnv->getFilename();
   ASSERT_EQ(filename, dbPath_);

   auto&& controlSalt = CryptoPRNG::generateRandom(32);
   auto&& rawRoot = CryptoPRNG::generateRandom(32);
   string dbName("test");

   auto dbIface = make_shared<IO::DBInterface>(
      dbEnv.get(), dbName, controlSalt, ENCRYPTION_TOPLAYER_VERSION);

   //setup new db
   ASSERT_EQ(dbIface->getEntryCount(), 0U);
   dbIface->loadAllEntries(rawRoot);
   ASSERT_EQ(dbIface->getEntryCount(), 0U);

   //generate data
   auto&& key1 = CryptoPRNG::generateRandom(20);
   auto&& key2 = CryptoPRNG::generateRandom(15);
   auto&& key3 = CryptoPRNG::generateRandom(12);

   auto&& val1 = CryptoPRNG::generateRandom(64);
   auto&& val2 = CryptoPRNG::generateRandom(64);
   auto&& val3 = CryptoPRNG::generateRandom(240);
   auto&& val4 = CryptoPRNG::generateRandom(16);
   auto&& val5 = CryptoPRNG::generateRandom(120);

   //check file content
   {
      ASSERT_FALSE(TestUtils::searchFile(filename, key1));
      ASSERT_FALSE(TestUtils::searchFile(filename, key2));
      ASSERT_FALSE(TestUtils::searchFile(filename, key3));

      ASSERT_FALSE(TestUtils::searchFile(filename, val1));
      ASSERT_FALSE(TestUtils::searchFile(filename, val2));
      ASSERT_FALSE(TestUtils::searchFile(filename, val3));
      ASSERT_FALSE(TestUtils::searchFile(filename, val4));
      ASSERT_FALSE(TestUtils::searchFile(filename, val5));
   }

   {
      //write data
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), true);
      auto valToWrite = val1;
      tx.insert(key1, valToWrite);
      valToWrite = val2;
      tx.insert(key2, valToWrite);
      valToWrite = val3;
      tx.insert(key3, valToWrite);

      //replace key3 value within same tx
      valToWrite = val4;
      tx.insert(key3, valToWrite);
   }

   //check entry count
   ASSERT_EQ(dbIface->getEntryCount(), 3U);

   //check file content
   {
      ASSERT_FALSE(TestUtils::searchFile(filename, key1));
      ASSERT_FALSE(TestUtils::searchFile(filename, key2));
      ASSERT_FALSE(TestUtils::searchFile(filename, key3));

      ASSERT_FALSE(TestUtils::searchFile(filename, val1));
      ASSERT_FALSE(TestUtils::searchFile(filename, val2));
      ASSERT_FALSE(TestUtils::searchFile(filename, val3));
      ASSERT_FALSE(TestUtils::searchFile(filename, val4));
      ASSERT_FALSE(TestUtils::searchFile(filename, val5));
   }

   //close dbIface
   dbIface->close();
   dbIface.reset();

   //open LMDB object
   LMDB dbObj;
   {
      auto tx = LMDBEnv::Transaction(dbEnv.get(), LMDB::ReadWrite);
      dbObj.open(dbEnv.get(), dbName);
   }

   //grab all entries in db
   auto&& keyValMap = getAllEntries(dbEnv, dbObj);
   EXPECT_EQ(keyValMap.size(), 4ULL);

   //check gaps
   ASSERT_EQ(tallyGaps(keyValMap).size(), 0ULL);

   //convert to IES packets
   vector<IESPacket> packets;
   for(auto& keyVal : keyValMap)
   {
      auto&& iesPacket = getIESData(keyVal);
      packets.push_back(iesPacket);
   }

   //check cryptographic material
   for(unsigned i=0; i<packets.size(); i++)
   {
      auto& packet = packets[i];

      ASSERT_TRUE(CryptoECDSA().VerifyPublicKeyValid(packet.pubKey_));
      ASSERT_NE(packet.iv_, allZeroes16_);

      for(unsigned y=0; y<packets.size(); y++)
      {
         if (y==i)
            continue;

         auto packetY = packets[y];
         ASSERT_NE(packet.iv_, packetY.iv_);
         ASSERT_NE(packet.pubKey_, packetY.pubKey_);
      }
   }

   /* decryption leg */

   //generate seed
   auto&& saltedRoot = BtcUtils::getHMAC256(controlSalt, rawRoot);

   //generate first key pair
   auto&& firstKeyPair = generateKeyPair(saltedRoot, 0);

   pair<SecureBinaryData, SecureBinaryData> currentKeyPair;
   try
   {
      auto& packet = packets[0];

      //check cylce flag is first entry in db
      ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), 0U);

      //check first entry is a cycle flag
      auto&& dataPair = decryptPair(packet, firstKeyPair);
      ASSERT_EQ(dataPair.first.getSize(), 0ULL);
      ASSERT_EQ(dataPair.second, BinaryData::fromString("cycle"));

      //cycle key pair
      currentKeyPair = generateKeyPair(saltedRoot, 1);
   }
   catch(...)
   {
      ASSERT_FALSE(true);
   }

   //decrypt the other values with wrong key pair
   vector<pair<BinaryData, BinaryData>> decryptedPairs;
   for (unsigned i=1; i<packets.size(); i++)
   {
      auto packet = packets[i];
      ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), i);

      try
      {
         auto&& dataPair = decryptPair(packet, firstKeyPair);
         decryptedPairs.push_back(dataPair);
         ASSERT_FALSE(true);
      }
      catch(...)
      {
         continue;
      }
   }

   //decrypt the other values with proper key pair
   for (unsigned i=1; i<packets.size(); i++)
   {
      auto packet = packets[i];
      ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), i);

      try
      {
         auto&& dataPair = decryptPair(packet, currentKeyPair);
         decryptedPairs.push_back(dataPair);
      }
      catch(...)
      {
         ASSERT_FALSE(true);
      }
   }

   //check decrypted values
   EXPECT_EQ(decryptedPairs[0].first, key1);
   EXPECT_EQ(decryptedPairs[0].second, val1);

   EXPECT_EQ(decryptedPairs[1].first, key2);
   EXPECT_EQ(decryptedPairs[1].second, val2);

   EXPECT_EQ(decryptedPairs[2].first, key3);
   EXPECT_EQ(decryptedPairs[2].second, val4);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletInterfaceTest, EncryptionTest_AmendValues)
{
   auto dbEnv = make_shared<LMDBEnv>();
   dbEnv->open(dbPath_, 0);
   auto filename = dbEnv->getFilename();
   ASSERT_EQ(filename, dbPath_);

   auto&& controlSalt = CryptoPRNG::generateRandom(32);
   auto&& rawRoot = CryptoPRNG::generateRandom(32);
   string dbName("test");

   auto dbIface = make_shared<IO::DBInterface>(
      dbEnv.get(), dbName, controlSalt, ENCRYPTION_TOPLAYER_VERSION);

   //sanity check
   ASSERT_EQ(dbIface->getEntryCount(), 0U);
   dbIface->loadAllEntries(rawRoot);
   ASSERT_EQ(dbIface->getEntryCount(), 0U);

   //generate data
   auto&& key1 = CryptoPRNG::generateRandom(20);
   auto&& key2 = CryptoPRNG::generateRandom(15);
   auto&& key3 = CryptoPRNG::generateRandom(12);

   auto&& val1 = CryptoPRNG::generateRandom(64);
   auto&& val2 = CryptoPRNG::generateRandom(64);
   auto&& val3 = CryptoPRNG::generateRandom(32);
   auto&& val4 = CryptoPRNG::generateRandom(16);
   auto&& val5 = CryptoPRNG::generateRandom(120);

   //check file content
   {
      ASSERT_FALSE(TestUtils::searchFile(filename, key1));
      ASSERT_FALSE(TestUtils::searchFile(filename, key2));
      ASSERT_FALSE(TestUtils::searchFile(filename, key3));

      ASSERT_FALSE(TestUtils::searchFile(filename, val1));
      ASSERT_FALSE(TestUtils::searchFile(filename, val2));
      ASSERT_FALSE(TestUtils::searchFile(filename, val3));
      ASSERT_FALSE(TestUtils::searchFile(filename, val4));
      ASSERT_FALSE(TestUtils::searchFile(filename, val5));
   }

   {
      //write data
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), true);
      auto valToWrite = val1;
      tx.insert(key1, valToWrite);
      valToWrite = val2;
      tx.insert(key2, valToWrite);
      valToWrite = val3;
      tx.insert(key3, valToWrite);
   }

   //check entry count
   ASSERT_EQ(dbIface->getEntryCount(), 3U);

   //check file content
   {
      ASSERT_FALSE(TestUtils::searchFile(filename, key1));
      ASSERT_FALSE(TestUtils::searchFile(filename, key2));
      ASSERT_FALSE(TestUtils::searchFile(filename, key3));

      ASSERT_FALSE(TestUtils::searchFile(filename, val1));
      ASSERT_FALSE(TestUtils::searchFile(filename, val2));
      ASSERT_FALSE(TestUtils::searchFile(filename, val3));
      ASSERT_FALSE(TestUtils::searchFile(filename, val4));
      ASSERT_FALSE(TestUtils::searchFile(filename, val5));
   }

   {
      //amend db in new transaction
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), true);
      tx.erase(key2);

      tx.erase(key3);
      auto valToWrite = val4;
      tx.insert(key3, valToWrite);

      auto key2Data = tx.getDataRef(key2);
      EXPECT_EQ(key2Data.getSize(), 0ULL);

      auto key3Data = tx.getDataRef(key3);
      EXPECT_EQ(key3Data, val4);
   }

   //check file content
   {
      ASSERT_FALSE(TestUtils::searchFile(filename, key1));
      ASSERT_FALSE(TestUtils::searchFile(filename, key2));
      ASSERT_FALSE(TestUtils::searchFile(filename, key3));

      ASSERT_FALSE(TestUtils::searchFile(filename, val1));
      ASSERT_FALSE(TestUtils::searchFile(filename, val2));
      ASSERT_FALSE(TestUtils::searchFile(filename, val3));
      ASSERT_FALSE(TestUtils::searchFile(filename, val4));
      ASSERT_FALSE(TestUtils::searchFile(filename, val5));
   }

   //check entry count
   ASSERT_EQ(dbIface->getEntryCount(), 2U);

   //close dbIface
   dbIface->close();
   dbIface.reset();

   //open LMDB object
   LMDB dbObj;
   {
      auto tx = LMDBEnv::Transaction(dbEnv.get(), LMDB::ReadWrite);
      dbObj.open(dbEnv.get(), dbName);
   }

   //grab all entries in db
   auto&& keyValMap = getAllEntries(dbEnv, dbObj);
   EXPECT_EQ(keyValMap.size(), 5ULL);

   //check gaps
   {
      auto&& gaps = tallyGaps(keyValMap);
      ASSERT_EQ(gaps.size(), 2ULL);

      auto gapsIter = gaps.begin();
      EXPECT_EQ(*gapsIter, 2U);
      
      ++gapsIter;
      EXPECT_EQ(*gapsIter, 3U);

      ++gapsIter;
      EXPECT_EQ(gapsIter, gaps.end());
   }

   //convert to IES packets
   vector<IESPacket> packets;
   for(auto& keyVal : keyValMap)
   {
      auto&& iesPacket = getIESData(keyVal);
      packets.push_back(iesPacket);
   }

   //check cryptographic material
   for(unsigned i=0; i<packets.size(); i++)
   {
      auto& packet = packets[i];

      ASSERT_TRUE(CryptoECDSA().VerifyPublicKeyValid(packet.pubKey_));
      ASSERT_NE(packet.iv_, allZeroes16_);

      for(unsigned y=0; y<packets.size(); y++)
      {
         if (y==i)
            continue;

         auto packetY = packets[y];
         ASSERT_NE(packet.iv_, packetY.iv_);
         ASSERT_NE(packet.pubKey_, packetY.pubKey_);
      }
   }

   /* decryption leg */

   //generate seed
   auto&& saltedRoot = BtcUtils::getHMAC256(controlSalt, rawRoot);

   //generate first key pair
   auto&& firstKeyPair = generateKeyPair(saltedRoot, 0);

   pair<SecureBinaryData, SecureBinaryData> currentKeyPair;
   try
   {
      auto& packet = packets[0];

      //check cylce flag is first entry in db
      ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), 0U);

      //check first entry is a cycle flag
      auto&& dataPair = decryptPair(packet, firstKeyPair);
      ASSERT_EQ(dataPair.first.getSize(), 0ULL);
      ASSERT_EQ(dataPair.second, BinaryData::fromString("cycle"));

      //cycle key pair
      currentKeyPair = generateKeyPair(saltedRoot, 1);
   }
   catch(...)
   {
      ASSERT_FALSE(true);
   }

   //decrypt the other values with wrong key pair
   vector<pair<BinaryData, BinaryData>> decryptedPairs;
   for (unsigned i=1; i<packets.size(); i++)
   {
      auto packet = packets[i];

      try
      {
         auto&& dataPair = decryptPair(packet, firstKeyPair);
         decryptedPairs.push_back(dataPair);
         ASSERT_FALSE(true);
      }
      catch(...)
      {
         continue;
      }
   }

   //decrypt the other values with proper key pair
   for (unsigned i=1; i<packets.size(); i++)
   {
      auto packet = packets[i];

      try
      {
         auto&& dataPair = decryptPair(packet, currentKeyPair);
         decryptedPairs.push_back(dataPair);
      }
      catch(...)
      {
         ASSERT_FALSE(true);
      }
   }

   //check decrypted values
   EXPECT_EQ(decryptedPairs[0].first, key1);
   EXPECT_EQ(decryptedPairs[0].second, val1);

   EXPECT_EQ(decryptedPairs[1].first.getSize(), 0ULL);
   EXPECT_EQ(decryptedPairs[1].second, getErasurePacket(2));

   EXPECT_EQ(decryptedPairs[2].first.getSize(), 0ULL);
   EXPECT_EQ(decryptedPairs[2].second, getErasurePacket(3));

   EXPECT_EQ(decryptedPairs[3].first, key3);
   EXPECT_EQ(decryptedPairs[3].second, val4);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletInterfaceTest, EncryptionTest_OpenCloseAmend)
{
   auto dbEnv = make_shared<LMDBEnv>();
   dbEnv->open(dbPath_, 0);
   auto filename = dbEnv->getFilename();
   ASSERT_EQ(filename, dbPath_);

   auto&& controlSalt = CryptoPRNG::generateRandom(32);
   auto&& rawRoot = CryptoPRNG::generateRandom(32);
   string dbName("test");

   auto dbIface = make_shared<IO::DBInterface>(
      dbEnv.get(), dbName, controlSalt, ENCRYPTION_TOPLAYER_VERSION);

   //sanity check
   ASSERT_EQ(dbIface->getEntryCount(), 0U);
   dbIface->loadAllEntries(rawRoot);
   ASSERT_EQ(dbIface->getEntryCount(), 0U);

   //generate data
   auto&& key1 = CryptoPRNG::generateRandom(20);
   auto&& key2 = CryptoPRNG::generateRandom(15);
   auto&& key3 = CryptoPRNG::generateRandom(12);

   auto&& val1 = CryptoPRNG::generateRandom(64);
   auto&& val2 = CryptoPRNG::generateRandom(64);
   auto&& val3 = CryptoPRNG::generateRandom(32);
   auto&& val4 = CryptoPRNG::generateRandom(16);
   auto&& val5 = CryptoPRNG::generateRandom(120);

   //check file content
   {
      ASSERT_FALSE(TestUtils::searchFile(filename, key1));
      ASSERT_FALSE(TestUtils::searchFile(filename, key2));
      ASSERT_FALSE(TestUtils::searchFile(filename, key3));

      ASSERT_FALSE(TestUtils::searchFile(filename, val1));
      ASSERT_FALSE(TestUtils::searchFile(filename, val2));
      ASSERT_FALSE(TestUtils::searchFile(filename, val3));
      ASSERT_FALSE(TestUtils::searchFile(filename, val4));
      ASSERT_FALSE(TestUtils::searchFile(filename, val5));
   }

   {
      //write data
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), true);
      auto valToWrite = val1;
      tx.insert(key1, valToWrite);
      valToWrite = val2;
      tx.insert(key2, valToWrite);
      valToWrite = val3;
      tx.insert(key3, valToWrite);
   }

   //check entry count
   ASSERT_EQ(dbIface->getEntryCount(), 3U);

   //check file content
   {
      ASSERT_FALSE(TestUtils::searchFile(filename, key1));
      ASSERT_FALSE(TestUtils::searchFile(filename, key2));
      ASSERT_FALSE(TestUtils::searchFile(filename, key3));

      ASSERT_FALSE(TestUtils::searchFile(filename, val1));
      ASSERT_FALSE(TestUtils::searchFile(filename, val2));
      ASSERT_FALSE(TestUtils::searchFile(filename, val3));
      ASSERT_FALSE(TestUtils::searchFile(filename, val4));
      ASSERT_FALSE(TestUtils::searchFile(filename, val5));
   }

   {
      //amend db in new transaction
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), true);
      
      tx.erase(key3);
      auto valToWrite = val4;
      tx.insert(key3, valToWrite);
      tx.erase(key2);

      auto key2Data = tx.getDataRef(key2);
      EXPECT_EQ(key2Data.getSize(), 0ULL);

      auto key3Data = tx.getDataRef(key3);
      EXPECT_EQ(key3Data, val4);
   }

   //check file content
   {
      ASSERT_FALSE(TestUtils::searchFile(filename, key1));
      ASSERT_FALSE(TestUtils::searchFile(filename, key2));
      ASSERT_FALSE(TestUtils::searchFile(filename, key3));

      ASSERT_FALSE(TestUtils::searchFile(filename, val1));
      ASSERT_FALSE(TestUtils::searchFile(filename, val2));
      ASSERT_FALSE(TestUtils::searchFile(filename, val3));
      ASSERT_FALSE(TestUtils::searchFile(filename, val4));
      ASSERT_FALSE(TestUtils::searchFile(filename, val5));
   }

   //check entry count
   ASSERT_EQ(dbIface->getEntryCount(), 2U);

   //close dbIface
   dbIface->close();
   dbIface.reset();

   //open LMDB object
   LMDB dbObj;
   {
      auto tx = LMDBEnv::Transaction(dbEnv.get(), LMDB::ReadWrite);
      dbObj.open(dbEnv.get(), dbName);
   }

   //grab all entries in db
   auto&& keyValMap = getAllEntries(dbEnv, dbObj);
   EXPECT_EQ(keyValMap.size(), 5ULL);

   //check gaps
   {
      auto&& gaps = tallyGaps(keyValMap);
      ASSERT_EQ(gaps.size(), 2ULL);

      auto gapsIter = gaps.begin();
      EXPECT_EQ(*gapsIter, 2U);
      
      ++gapsIter;
      EXPECT_EQ(*gapsIter, 3U);

      ++gapsIter;
      EXPECT_EQ(gapsIter, gaps.end());
   }

   //convert to IES packets
   vector<IESPacket> packets;
   for(auto& keyVal : keyValMap)
   {
      auto&& iesPacket = getIESData(keyVal);
      packets.push_back(iesPacket);
   }

   //check cryptographic material
   for(unsigned i=0; i<packets.size(); i++)
   {
      auto& packet = packets[i];

      ASSERT_TRUE(CryptoECDSA().VerifyPublicKeyValid(packet.pubKey_));
      ASSERT_NE(packet.iv_, allZeroes16_);

      for(unsigned y=0; y<packets.size(); y++)
      {
         if (y==i)
            continue;

         auto packetY = packets[y];
         ASSERT_NE(packet.iv_, packetY.iv_);
         ASSERT_NE(packet.pubKey_, packetY.pubKey_);
      }
   }

   /* decryption leg */

   //generate seed
   auto&& saltedRoot = BtcUtils::getHMAC256(controlSalt, rawRoot);

   //generate first key pair
   auto&& firstKeyPair = generateKeyPair(saltedRoot, 0);

   pair<SecureBinaryData, SecureBinaryData> currentKeyPair;
   try
   {
      auto& packet = packets[0];

      //check cylce flag is first entry in db
      ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), 0U);

      //check first entry is a cycle flag
      auto&& dataPair = decryptPair(packet, firstKeyPair);
      ASSERT_EQ(dataPair.first.getSize(), 0ULL);
      ASSERT_EQ(dataPair.second, BinaryData::fromString("cycle"));

      //cycle key pair
      currentKeyPair = generateKeyPair(saltedRoot, 1);
   }
   catch(...)
   {
      ASSERT_FALSE(true);
   }

   //decrypt the other values with wrong key pair
   vector<pair<BinaryData, BinaryData>> decryptedPairs;
   for (unsigned i=1; i<packets.size(); i++)
   {
      auto packet = packets[i];

      try
      {
         auto&& dataPair = decryptPair(packet, firstKeyPair);
         decryptedPairs.push_back(dataPair);
         ASSERT_FALSE(true);
      }
      catch(...)
      {
         continue;
      }
   }

   //decrypt the other values with proper key pair
   for (unsigned i=1; i<packets.size(); i++)
   {
      auto packet = packets[i];

      try
      {
         auto&& dataPair = decryptPair(packet, currentKeyPair);
         decryptedPairs.push_back(dataPair);
      }
      catch(...)
      {
         ASSERT_FALSE(true);
      }
   }

   //check decrypted values
   EXPECT_EQ(decryptedPairs[0].first, key1);
   EXPECT_EQ(decryptedPairs[0].second, val1);

   EXPECT_EQ(decryptedPairs[1].first.getSize(), 0ULL);
   EXPECT_EQ(decryptedPairs[1].second, getErasurePacket(3));

   EXPECT_EQ(decryptedPairs[2].first, key3);
   EXPECT_EQ(decryptedPairs[2].second, val4);
   
   EXPECT_EQ(decryptedPairs[3].first.getSize(), 0ULL);
   EXPECT_EQ(decryptedPairs[3].second, getErasurePacket(2));

   //cycle dbEnv
   dbObj.close();
   dbEnv->close();
   dbEnv->open(filename, 0);

   //reopen db
   dbIface = make_shared<IO::DBInterface>(
      dbEnv.get(), dbName, controlSalt, ENCRYPTION_TOPLAYER_VERSION);

   //sanity check
   ASSERT_EQ(dbIface->getEntryCount(), 0U);
   dbIface->loadAllEntries(rawRoot);
   ASSERT_EQ(dbIface->getEntryCount(), 2U);

   {
      //read db values
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), false);

      auto key1Data = tx.getDataRef(key1);
      EXPECT_EQ(key1Data, val1);

      auto key2Data = tx.getDataRef(key2);
      EXPECT_EQ(key2Data.getSize(), 0ULL);

      auto key3Data = tx.getDataRef(key3);
      EXPECT_EQ(key3Data, val4);
   }

   auto key4 = CryptoPRNG::generateRandom(30);
   auto val6 = CryptoPRNG::generateRandom(154);

   {
      //amend db in new transaction
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), true);

      auto valToWrite = val5;
      tx.insert(key2, valToWrite);
      valToWrite = val3;
      tx.insert(key4, valToWrite);
      valToWrite = val6;
      tx.insert(key3, valToWrite);
      tx.erase(key1);

      auto key1Data = tx.getDataRef(key1);
      EXPECT_EQ(key1Data.getSize(), 0ULL);

      auto key2Data = tx.getDataRef(key2);
      EXPECT_EQ(key2Data, val5);

      auto key3Data = tx.getDataRef(key3);
      EXPECT_EQ(key3Data, val6);

      auto key4Data = tx.getDataRef(key4);
      EXPECT_EQ(key4Data, val3);
   }

   //close dbIface
   dbIface->close();
   dbIface.reset();

   //open LMDB object
   LMDB dbObj2;
   {
      auto tx = LMDBEnv::Transaction(dbEnv.get(), LMDB::ReadWrite);
      dbObj2.open(dbEnv.get(), dbName);
   }

   //grab all entries in db
   keyValMap = getAllEntries(dbEnv, dbObj2);
   EXPECT_EQ(keyValMap.size(), 9ULL);

   //check gaps
   {
      auto&& gaps = tallyGaps(keyValMap);
      ASSERT_EQ(gaps.size(), 4ULL);

      auto gapsIter = gaps.begin();
      EXPECT_EQ(*gapsIter, 1U);
      
      ++gapsIter;
      EXPECT_EQ(*gapsIter, 2U);

      ++gapsIter;
      EXPECT_EQ(*gapsIter, 3U);

      ++gapsIter;
      EXPECT_EQ(*gapsIter, 5U);

      ++gapsIter;
      EXPECT_EQ(gapsIter, gaps.end());
   }

   //convert to IES packets
   packets.clear();
   for(auto& keyVal : keyValMap)
   {
      auto&& iesPacket = getIESData(keyVal);
      packets.push_back(iesPacket);
   }

   //check cryptographic material
   for(unsigned i=0; i<packets.size(); i++)
   {
      auto& packet = packets[i];

      ASSERT_TRUE(CryptoECDSA().VerifyPublicKeyValid(packet.pubKey_));
      ASSERT_NE(packet.iv_, allZeroes16_);

      for(unsigned y=0; y<packets.size(); y++)
      {
         if (y==i)
            continue;

         auto packetY = packets[y];
         ASSERT_NE(packet.iv_, packetY.iv_);
         ASSERT_NE(packet.pubKey_, packetY.pubKey_);
      }
   }

   /* 2nd decryption leg */

   try
   {
      auto& packet = packets[0];

      //check cylce flag is first entry in db
      ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), 0U);

      //check first entry is a cycle flag
      auto&& dataPair = decryptPair(packet, firstKeyPair);
      ASSERT_EQ(dataPair.first.getSize(), 0ULL);
      ASSERT_EQ(dataPair.second, BinaryData::fromString("cycle"));
   }
   catch(...)
   {
      ASSERT_FALSE(true);
   }

   //decrypt the other values
   decryptedPairs.clear();
   for (unsigned i=1; i<4; i++)
   {
      auto packet = packets[i];

      try
      {
         auto&& dataPair = decryptPair(packet, currentKeyPair);
         decryptedPairs.push_back(dataPair);
      }
      catch(...)
      {
         ASSERT_FALSE(true);
      }
   }

   {
      //check packets[2] is a cycle flag
      ASSERT_EQ(decryptedPairs[2].first.getSize(), 0ULL);
      ASSERT_EQ(decryptedPairs[2].second, BinaryData::fromString("cycle"));

      //cycle key
      currentKeyPair = generateKeyPair(saltedRoot, 2);
   }

   //decrypt last set of values with cycled keys
   for (unsigned i=4; i<packets.size(); i++)
   {
      auto packet = packets[i];

      try
      {
         auto&& dataPair = decryptPair(packet, currentKeyPair);
         decryptedPairs.push_back(dataPair);
      }
      catch(...)
      {
         ASSERT_FALSE(true);
      }
   }

   //check decrypted values
   EXPECT_EQ(decryptedPairs[0].first.getSize(), 0ULL);
   EXPECT_EQ(decryptedPairs[0].second, getErasurePacket(3));
   
   EXPECT_EQ(decryptedPairs[1].first.getSize(), 0ULL);
   EXPECT_EQ(decryptedPairs[1].second, getErasurePacket(2));

   EXPECT_EQ(decryptedPairs[3].first, key2);
   EXPECT_EQ(decryptedPairs[3].second, val5);

   EXPECT_EQ(decryptedPairs[4].first, key4);
   EXPECT_EQ(decryptedPairs[4].second, val3);

   EXPECT_EQ(decryptedPairs[5].first.getSize(), 0ULL);
   EXPECT_EQ(decryptedPairs[5].second, getErasurePacket(5));

   EXPECT_EQ(decryptedPairs[6].first, key3);
   EXPECT_EQ(decryptedPairs[6].second, val6);

   EXPECT_EQ(decryptedPairs[7].first.getSize(), 0ULL);
   EXPECT_EQ(decryptedPairs[7].second, getErasurePacket(1));

   dbObj2.close();
   dbEnv->close();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletInterfaceTest, Passphrase_Test)
{
   //passphrase lambdas
   auto passLbd = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString("abcd");
   };

   auto passEmpty = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData();
   };

   {
      //create wallet iface
      IO::WalletDBInterface dbIface;
      dbIface.setupEnv(dbPath_, false, passLbd, 0);

      //close iface
      dbIface.shutdown();
   }

   {
      //try to open iface with wrong passphrase
      try {
         IO::WalletDBInterface dbIface;
         dbIface.setupEnv(dbPath_, true, passEmpty, 0);
         ASSERT_TRUE(false);
      } catch (const DecryptedDataContainerException& e) {
         EXPECT_EQ(e.what(), string("empty passphrase"));
      }

      //try to open iface with wrong file flag
      try {
         IO::WalletDBInterface dbIface;
         dbIface.setupEnv(dbPath_, false, passLbd, 0);
         ASSERT_TRUE(false);
      } catch (const IO::WalletInterfaceException& e) {
         EXPECT_EQ(e.what(), string("[openEnv] file flag mismatch"));
      }

      //open with proper passphrase
      try {
         IO::WalletDBInterface dbIface;
         dbIface.setupEnv(dbPath_, true, passLbd, 0);
         dbIface.shutdown();
      } catch(...) {
         ASSERT_FALSE(true);
      }
   }

   auto dbPath2 = homedir_ / "db2_test";

   {
      //create wallet iface with empty passphrase lambda
      IO::WalletDBInterface dbIface;
      dbIface.setupEnv(dbPath2, false, passEmpty, 0);

      //close iface
      dbIface.shutdown();
   }

   {
      auto passLbd2 = [](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         throw runtime_error("shouldn't get here");
      };

      //reopen iface, check it won't hit the passphrase lambda
      IO::WalletDBInterface dbIface;
      try {
         dbIface.setupEnv(dbPath2, true, passLbd2, 0);
         dbIface.shutdown();
      } catch (...) {
         ASSERT_TRUE(false);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletInterfaceTest, DbCount_Test)
{
   //lambdas
   auto passLbd = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString("abcd");
   };

   auto checkDbValues = [](IO::WalletDBInterface& iface, string dbName, 
      map<BinaryData, BinaryData> dataMap)->bool
   {
      auto tx = iface.beginReadTransaction(dbName);

      auto dbIter = tx->getIterator();
      while (dbIter->isValid())
      {
         auto key = dbIter->key();
         auto val = dbIter->value();

         auto dataIter = dataMap.find(key);
         if(dataIter != dataMap.end())
         {
            if(dataIter->second == val)
               dataMap.erase(dataIter);
         }

         dbIter->advance();
      }

      return dataMap.size() == 0;
   };

   //create wallet dbEnv
   IO::WalletDBInterface dbIface;
   dbIface.setupEnv(dbPath_, false, passLbd, 0);

   //add db
   {
      EXPECT_EQ(dbIface.getDbCount(), 0U);

      auto headerPtr = make_shared<IO::WalletHeader_Custom>();
      headerPtr->walletID_ = "db1";

      dbIface.lockControlContainer(passLbd);
      dbIface.addHeader(headerPtr);
      dbIface.unlockControlContainer();
      EXPECT_EQ(dbIface.getDbCount(), 1U);
   }

   {
      auto dbHeader = dbIface.getWalletHeader("db1");
      ASSERT_EQ(dbHeader->getDbName(), "db1");
      ASSERT_NE(
         dynamic_pointer_cast<IO::WalletHeader_Custom>(dbHeader), nullptr);
   }

   //set db1 values
   map<BinaryData, BinaryData> db1Values;
   for (unsigned i=0; i<10; i++)
   {
      db1Values.insert(make_pair(
         CryptoPRNG::generateRandom(10),
         CryptoPRNG::generateRandom(30)));
   }
   
   {
      auto tx = dbIface.beginWriteTransaction("db1");
      auto mapToWrite = db1Values;
      for (auto& keyVal : mapToWrite)
         tx->insert(keyVal.first, keyVal.second);
   }

   //check db1 values
   EXPECT_TRUE(checkDbValues(dbIface, "db1", db1Values));

   //increase db count to 2
   dbIface.setDbCount(2);

   //check values of first db are still valid
   EXPECT_TRUE(checkDbValues(dbIface, "db1", db1Values));

   //modify first db, check it works
   {
      auto tx = dbIface.beginWriteTransaction("db1");
      auto db1Iter = db1Values.begin();
      db1Iter++; db1Iter++;
      db1Iter->second = CryptoPRNG::generateRandom(18);
      auto valToWrite = db1Iter->second;
      tx->insert(db1Iter->first, valToWrite);
      
      db1Iter++; db1Iter++;
      db1Iter->second = CryptoPRNG::generateRandom(42);
      valToWrite = db1Iter->second;
      tx->insert(db1Iter->first, valToWrite);

      auto dataPair = make_pair(
         CryptoPRNG::generateRandom(14),
         CryptoPRNG::generateRandom(80));
      valToWrite = dataPair.second;
      tx->insert(dataPair.first, valToWrite);
      db1Values.insert(dataPair);
   }

   //check modifcations held
   EXPECT_TRUE(checkDbValues(dbIface, "db1", db1Values));
  
   //add new db
   {
      EXPECT_EQ(dbIface.getDbCount(), 1U);
      auto headerPtr = make_shared<IO::WalletHeader_Custom>();
      headerPtr->walletID_ = "db2";

      dbIface.lockControlContainer(passLbd);
      dbIface.addHeader(headerPtr);
      dbIface.unlockControlContainer();
      EXPECT_EQ(dbIface.getDbCount(), 2U);
   }

   //check db1 modifcations held
   EXPECT_TRUE(checkDbValues(dbIface, "db1", db1Values));

   //set db2 values
   map<BinaryData, BinaryData> db2Values;
   for (unsigned i=0; i<15; i++)
   {
      db2Values.insert(make_pair(
         CryptoPRNG::generateRandom(12),
         CryptoPRNG::generateRandom(38)));
   }

   {
      auto tx = dbIface.beginWriteTransaction("db2");
      auto mapToWrite = db2Values;
      for (auto& keyVal : mapToWrite)
         tx->insert(keyVal.first, keyVal.second);
   }

   //check values
   EXPECT_TRUE(checkDbValues(dbIface, "db1", db1Values));
   EXPECT_TRUE(checkDbValues(dbIface, "db2", db2Values));

   //try to add db, should fail
   try
   {
      EXPECT_EQ(dbIface.getDbCount(), 2U);
      auto headerPtr = make_shared<IO::WalletHeader_Custom>();
      headerPtr->walletID_ = "db3";

      dbIface.lockControlContainer(passLbd);
      dbIface.addHeader(headerPtr);
      ASSERT_TRUE(false);
   }
   catch (IO::WalletInterfaceException& e)
   {
      EXPECT_EQ(e.what(), string("dbCount is too low"));
      dbIface.unlockControlContainer();
      EXPECT_EQ(dbIface.getDbCount(), 2U);
   }

   //shutdown db env
   dbIface.shutdown();

   //check dbIface is dead
   try
   {
      auto tx = dbIface.beginReadTransaction(CONTROL_DB_NAME);
      ASSERT_TRUE(false);
   }
   catch (LMDBException& e)
   {
      EXPECT_EQ(e.what(), string("null LMDBEnv"));
   }

   try
   {
      auto tx = dbIface.beginReadTransaction("db1");
      ASSERT_TRUE(false);
   } 
   catch (IO::WalletInterfaceException& e)
   {
      EXPECT_EQ(e.what(), string("invalid db name"));
   }

   try
   {
      dbIface.lockControlContainer(passLbd);
      ASSERT_TRUE(false);
   }
   catch (LockableException& e)
   {      
      EXPECT_EQ(e.what(), string("null lockable ptr"));
   }

   //setup db env anew
   dbIface.setupEnv(dbPath_, true, passLbd, 0);

   try
   {
      //try to increase db count while a tx is live, should fail
      auto tx = dbIface.beginReadTransaction("db1");
      dbIface.setDbCount(5);
   }
   catch (IO::WalletInterfaceException& e)
   {
      EXPECT_EQ(e.what(), string("live transactions, cannot change dbCount"));
   }

   //increase db count
   dbIface.setDbCount(5);
   EXPECT_EQ(dbIface.getDbCount(), 2U);

   //check db1 values
   EXPECT_TRUE(checkDbValues(dbIface, "db1", db1Values));

   //check db2 values
   EXPECT_TRUE(checkDbValues(dbIface, "db2", db2Values));

   //add 3rd db
   {
      auto headerPtr = make_shared<IO::WalletHeader_Custom>();
      headerPtr->walletID_ = "db3";

      dbIface.lockControlContainer(passLbd);
      dbIface.addHeader(headerPtr);
      dbIface.unlockControlContainer();
      EXPECT_EQ(dbIface.getDbCount(), 3U);
   }

   //modify db2
   {
      auto tx = dbIface.beginWriteTransaction("db2");
      auto db2Iter = db2Values.begin();
      db2Iter++; db2Iter++; db2Iter++;
      db2Iter->second = CryptoPRNG::generateRandom(22);
      auto valToWrite = db2Iter->second;
      tx->insert(db2Iter->first, valToWrite);
      
      db2Iter++;
      db2Iter->second = CryptoPRNG::generateRandom(16);
      valToWrite = db2Iter->second;
      tx->insert(db2Iter->first, valToWrite);

      auto dataPair = make_pair(
         CryptoPRNG::generateRandom(36),
         CryptoPRNG::generateRandom(124));
      valToWrite = dataPair.second;
      tx->insert(dataPair.first, valToWrite);
      db2Values.insert(dataPair);
   }

   //set db3 values
   map<BinaryData, BinaryData> db3Values;
   for (unsigned i=0; i<20; i++)
   {
      db3Values.insert(make_pair(
         CryptoPRNG::generateRandom(24),
         CryptoPRNG::generateRandom(48)));
   }

   {
      auto tx = dbIface.beginWriteTransaction("db3");
      auto mapToWrite = db3Values;
      for (auto& keyVal : mapToWrite)
         tx->insert(keyVal.first, keyVal.second);
   }

   //check values
   EXPECT_TRUE(checkDbValues(dbIface, "db1", db1Values));
   EXPECT_TRUE(checkDbValues(dbIface, "db2", db2Values));
   EXPECT_TRUE(checkDbValues(dbIface, "db3", db3Values));

   //try to overwrite db3
   try
   {
      EXPECT_EQ(dbIface.getDbCount(), 3U);
      auto headerPtr = make_shared<IO::WalletHeader_Custom>();
      headerPtr->walletID_ = "db3";

      dbIface.lockControlContainer(passLbd);
      dbIface.addHeader(headerPtr);
      ASSERT_FALSE(true);
   }
   catch (IO::WalletInterfaceException& e)
   {
      dbIface.unlockControlContainer();
      EXPECT_EQ(e.what(), string("header already in map"));
   }

   //check values
   EXPECT_TRUE(checkDbValues(dbIface, "db1", db1Values));
   EXPECT_TRUE(checkDbValues(dbIface, "db2", db2Values));
   EXPECT_TRUE(checkDbValues(dbIface, "db3", db3Values));

   //try to shutdown env with live tx, should fail
   try
   {
      auto tx = dbIface.beginReadTransaction("db2");
      dbIface.shutdown();
      ASSERT_FALSE(true);
   }
   catch (IO::WalletInterfaceException& e)
   {
      EXPECT_EQ(e.what(), string("live transactions, cannot shutdown env"));
   }

   //shutdown env
   dbIface.shutdown();

   //setup db env anew
   dbIface.setupEnv(dbPath_, true, passLbd, 0);

   //check db values
   EXPECT_TRUE(checkDbValues(dbIface, "db1", db1Values));
   EXPECT_TRUE(checkDbValues(dbIface, "db2", db2Values));
   EXPECT_TRUE(checkDbValues(dbIface, "db3", db3Values));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletInterfaceTest, WipeEntries_Test)
{
   auto passLbd = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData();
   };

   auto iface = make_shared<IO::WalletDBInterface>();
   iface->setupEnv(dbPath_, false, passLbd, 0);

   string dbName("test");
   auto dbHeader = make_shared<IO::WalletHeader_Custom>();
   dbHeader->walletID_ = dbName;
   iface->lockControlContainer(passLbd);
   iface->addHeader(dbHeader);
   iface->unlockControlContainer();

   map<BinaryData, BinaryData> dataMap1;
   for (unsigned i=0; i<30; i++)
   {
      dataMap1.insert(make_pair(
         CryptoPRNG::generateRandom(20),
         CryptoPRNG::generateRandom(64)));
   }

   {
      //commit data
      auto tx = iface->beginWriteTransaction(dbName);
      auto mapToWrite = dataMap1;
      for (auto keyVal : mapToWrite)
         tx->insert(keyVal.first, keyVal.second);
   }

   //open raw db
   auto dbEnv = make_shared<LMDBEnv>(3);
   dbEnv->open(dbPath_, 0);
   auto filename = dbEnv->getFilename();
   ASSERT_EQ(filename, dbPath_);

   //grab control root
   SecureBinaryData controlRoot;
   SecureBinaryData controlSalt;
   {
      //open control db
      LMDB dbCtrl;
      auto tx = LMDBEnv::Transaction(dbEnv.get(), LMDB::ReadWrite);
      dbCtrl.open(dbEnv.get(), CONTROL_DB_NAME);

      //grab control header
      shared_ptr<IO::WalletHeader_Control> controlHeader;
      {
         BinaryWriter bw;
         bw.put_uint8_t(WALLETHEADER_PREFIX);
         bw.put_BinaryData(BinaryData::fromString(CONTROL_DB_NAME));
         CharacterArrayRef carKey(bw.getSize(), bw.getData().getPtr());
         auto rawVal = dbCtrl.get_NoCopy(carKey);
         
         BinaryDataRef refVal((const uint8_t*)rawVal.data, rawVal.len);
         BinaryRefReader brrVal(refVal);
         auto len = brrVal.get_var_int();
         auto headerVal = brrVal.get_BinaryDataRef(len);
         controlHeader = dynamic_pointer_cast<IO::WalletHeader_Control>(
            IO::WalletHeader::deserialize(bw.getData(), headerVal));

         controlSalt = controlHeader->controlSalt_;
      }

      //grab DecryptedDataContainer
      auto decryptedData = make_unique<DecryptedDataContainer>(
            nullptr, controlHeader->getDbName(),
            controlHeader->getDefaultEncryptionKey(),
            controlHeader->getDefaultEncryptionKeyId(),
            controlHeader->defaultKdfId_, controlHeader->masterEncryptionKeyId_);
      {
         auto txInner = 
            make_shared<IO::RawIfaceTransaction>(dbEnv.get(), &dbCtrl, true);
         decryptedData->readFromDisk(txInner);
      }

      //grab seed
      unique_ptr<EncryptedSeed> controlSeed;
      {
         BinaryWriter bw;
         bw.put_uint32_t(WALLET_SEED_KEY);
         CharacterArrayRef carKey(bw.getSize(), bw.getData().getPtr());
         auto rawVal = dbCtrl.get_NoCopy(carKey);
         
         BinaryDataRef refVal((const uint8_t*)rawVal.data, rawVal.len);
         BinaryRefReader brrVal(refVal);
         auto len = brrVal.get_var_int();
         auto seedVal = brrVal.get_BinaryDataRef(len);

         auto seedPtr = EncryptedSeed::deserialize(seedVal);
         auto ptrCast = dynamic_cast<EncryptedSeed*>(seedPtr.get());
         if (ptrCast == nullptr)
            throw WalletException("failed to deser wallet seed");
         controlSeed = unique_ptr<EncryptedSeed>(ptrCast);

         seedPtr.release();
      }

      {
         ReentrantLock lock(decryptedData.get());
         controlRoot = decryptedData->getClearTextAssetData(controlSeed.get());
      }
   }
   
   iface->shutdown();
   dbEnv->close();
   dbEnv->open(dbPath_, 0);

   //grab db salt
   SecureBinaryData dbSalt;
   {
      LMDB headerDb;
      {
         auto tx = LMDBEnv::Transaction(dbEnv.get(), LMDB::ReadWrite);
         headerDb.open(dbEnv.get(), WALLETHEADER_DBNAME);
      }

      auto&& keyValMap = getAllEntries(dbEnv, headerDb);
      
      vector<IESPacket> packets;
      for(auto& keyVal : keyValMap)
      {
         auto&& iesPacket = getIESData(keyVal);
         packets.push_back(iesPacket);
      }

      //generate seed
      auto&& saltedRoot = BtcUtils::getHMAC256(controlSalt, controlRoot);

      //generate first key pair
      auto&& currentKeyPair = generateKeyPair(saltedRoot, 1);

      //decrypt the other values with proper key pair
      map<BinaryData, BinaryData> decrKeyValMap;
      for (unsigned i=1; i<packets.size(); i++)
      {
         auto packet = packets[i];
         ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), i);

         try
         {
            auto&& dataPair = decryptPair(packet, currentKeyPair);
            decrKeyValMap.insert(dataPair);
         }
         catch(...)
         {
            ASSERT_FALSE(true);
         }
      }

      BinaryWriter bwKey;
      bwKey.put_uint8_t(WALLETHEADER_PREFIX);
      bwKey.put_String(dbName);

      auto iter = decrKeyValMap.find(bwKey.getData());
      BinaryRefReader brr(iter->second);
      auto len = brr.get_var_int();
      auto headerRef = brr.get_BinaryData(len);
      auto headerPtr = IO::WalletHeader::deserialize(iter->first, headerRef);

      dbSalt = headerPtr->controlSalt_;
   }

   //grab the entries
   map<BinaryData, IESPacket> dataKeyToCipherText;
   {
      LMDB headerDb;
      {
         auto tx = LMDBEnv::Transaction(dbEnv.get(), LMDB::ReadWrite);
         headerDb.open(dbEnv.get(), dbName);
      }

      auto&& keyValMap = getAllEntries(dbEnv, headerDb);
      
      vector<IESPacket> packets;
      for(auto& keyVal : keyValMap)
      {
         auto&& iesPacket = getIESData(keyVal);
         packets.push_back(iesPacket);
      }

      //generate seed
      auto&& saltedRoot = BtcUtils::getHMAC256(dbSalt, controlRoot);

      //generate first key pair
      auto&& currentKeyPair = generateKeyPair(saltedRoot, 1);

      //decrypt the other values with proper key pair
      for (unsigned i=1; i<packets.size(); i++)
      {
         auto packet = packets[i];
         ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), i);

         try
         {
            auto&& dataPair = decryptPair(packet, currentKeyPair);
            dataKeyToCipherText.insert(make_pair(dataPair.first, packet));

            //check decrypted data matches
            auto iter = dataMap1.find(dataPair.first);
            ASSERT_NE(iter, dataMap1.end());
            EXPECT_EQ(dataPair.second, iter->second);

         }
         catch(...)
         {
            ASSERT_FALSE(true);
         }
      }
   }

   //replacement map
   map<BinaryData, BinaryData> replaceMap;
   {
      auto iter = dataMap1.begin();
      for (unsigned i=0; i<10; i++)
         ++iter;

      replaceMap.insert(make_pair(iter->first, 
         CryptoPRNG::generateRandom(60)));
      
      ++iter;
      replaceMap.insert(make_pair(iter->first, 
         CryptoPRNG::generateRandom(70)));

      ++iter; ++iter; ++iter; ++iter;
      replaceMap.insert(make_pair(iter->first, 
         CryptoPRNG::generateRandom(80)));

      ++iter;
      replaceMap.insert(make_pair(iter->first, 
         CryptoPRNG::generateRandom(90)));

      ++iter;
      replaceMap.insert(make_pair(iter->first, 
         CryptoPRNG::generateRandom(100)));
   }

   //check packets are on disk
   for (auto& packetPair : dataKeyToCipherText)
   {
      EXPECT_TRUE(TestUtils::searchFile(
         filename, packetPair.second.cipherText_));
   }

   dbEnv->close();
   dbEnv.reset();

   //reopen db iface
   iface = make_shared<IO::WalletDBInterface>();
   iface->setupEnv(dbPath_, true, passLbd, 0);

   //replace a couple entries
   {
      //commit data
      auto tx = iface->beginWriteTransaction(dbName);
      auto mapToWrite = replaceMap;
      for (auto keyVal : mapToWrite)
         tx->insert(keyVal.first, keyVal.second);
   }

   //check final db state
   auto finalMap = replaceMap;
   finalMap.insert(dataMap1.begin(), dataMap1.end());
   {
      auto tx = iface->beginReadTransaction(dbName);
      auto iter = tx->getIterator();
      
      while(iter->isValid())
      {
         auto key = iter->key();
         auto mapIter = finalMap.find(key);
         ASSERT_NE(mapIter, finalMap.end());

         if (mapIter->second.getRef() == iter->value())
            finalMap.erase(mapIter);

         iter->advance();
      }

      EXPECT_EQ(finalMap.size(), 0ULL);
   }

   //shutdown db
   iface.reset();

   //check data on file
   for (auto& packetPair : dataKeyToCipherText)
   {
      auto iter = replaceMap.find(packetPair.first);
      if (iter == replaceMap.end())
      {
         continue;
         //untouched keys should have same ciphertext
         EXPECT_TRUE(TestUtils::searchFile(
            filename, packetPair.second.cipherText_));
      }
      else
      {
         //modified keys should have a different ciphertext
         EXPECT_FALSE(TestUtils::searchFile(
            filename, packetPair.second.cipherText_));
      }
   }
}

//TODO
//tampering tests

//entry padding length test

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class WalletsTest : public ::testing::Test
{
protected:
   std::filesystem::path homedir_;
   SecureBinaryData controlPass_;
   PassphraseLambda controlLbd_;

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      homedir_ = std::filesystem::path("./fakehomedir");
      FileUtils::removeDirectory(homedir_);
      std::filesystem::create_directory(homedir_);

      Armory::Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Armory::Config::ProcessType::DB);

      controlPass_ = SecureBinaryData::fromString("control");
      controlLbd_ = [this](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return controlPass_;
      };
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      Armory::Config::reset();
      FileUtils::removeDirectory(homedir_);
   }

   /////////////////////////////////////////////////////////////////////////////
   unsigned checkDb(IO::DBIfaceTransaction* tx, 
      const vector<SecureBinaryData>& data)
   {
      auto binaryParse = [](const BinaryDataRef& a, const BinaryDataRef& b)->bool
      {
         unsigned ctr = 0;
         while (ctr + a.getSize() <= b.getSize())
         {
            if (b.getPtr()[ctr] == a.getPtr()[0])
            {
               if (b.getSliceRef(ctr, a.getSize()) == a)
                  return true;
            }
            
            ++ctr;
         }

         return false;
      };

      auto parseDb = [tx, &binaryParse](const SecureBinaryData& val)->bool
      {
         auto iter = tx->getIterator();
         while (iter->isValid())
         {
            auto key = iter->key();
            if (key.getSize() >= val.getSize())
            {
               if (binaryParse(val.getRef(), key))
                  return true;
            }     

            auto value = iter->value();    
            if (value.getSize() >= val.getSize())
            {
               if (binaryParse(val.getRef(), value))
                  return true;
            }     

            iter->advance();
         }

         return false;
      };

      set<BinaryData> dataSet;
      for (auto& val : data)
         dataSet.insert(val);

      auto setIter = dataSet.begin();
      while (setIter != dataSet.end())
      {
         if (parseDb(*setIter))
         {
            dataSet.erase(setIter++);
            continue;
         }

         ++setIter;
      }

      return data.size() - dataSet.size();
   };
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, CreateCloseOpen_Test)
{
   map<string, vector<BinaryData>> addrMap;
   map<string, std::filesystem::path> filenames;

   WalletCreationParams params{
      SecureBinaryData::fromString("passphrase"),
      controlPass_,
      homedir_, 4, 1, 1};

   //create 3 wallets
   for (unsigned i = 0; i < 1; i++)
   {
      unique_ptr<Armory::Seeds::ClearTextSeed> seed(
         new Armory::Seeds::ClearTextSeed_Armory135());
      auto assetWlt = AssetWallet_Single::createFromSeed(
         move(seed), params);

      //get AddrVec
      auto&& hashSet = assetWlt->getAddrHashSet();

      auto id = assetWlt->getID();
      auto& vec = addrMap[id];

      vec.insert(vec.end(), hashSet.begin(), hashSet.end());

      //get filename
      filenames.emplace(id, assetWlt->getDbFilename());

      //close wallet 
      assetWlt.reset();
   }

   for (auto& addrVecPair : addrMap)
   {
      auto fnameIter = filenames.find(addrVecPair.first);
      ASSERT_NE(fnameIter, filenames.end());

      auto newWallet = 
         AssetWallet::loadMainWalletFromFile(fnameIter->second, controlLbd_);

      auto wltSingle =
         dynamic_pointer_cast<AssetWallet_Single>(newWallet);
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
   WalletCreationParams params {
      SecureBinaryData::fromString("passphrase"),
      SecureBinaryData::fromString("control"),
      homedir_,
      4, 1, 1
   };

   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);
   auto filename = assetWlt->getDbFilename();

   //get AddrVec
   auto hashSet = assetWlt->getAddrHashSet();
   auto mainAccId = assetWlt->getMainAccountID();
   {
      auto accPtr = assetWlt->getAccountForID(mainAccId);
      EXPECT_TRUE(accPtr->isLegacy());
   }

   //get pub root and chaincode
   auto pubRoot = assetWlt->getPublicRoot();
   auto chainCode = assetWlt->getArmory135Chaincode();

   //close wallet
   assetWlt.reset();

   auto woWallet = AssetWallet_Single::createFromPublicRoot_Armory135(
      homedir_,
      pubRoot,
      chainCode,
      SecureBinaryData::fromString("control"),
      4);
   EXPECT_EQ(mainAccId, woWallet->getMainAccountID());
   {
      auto accPtr = woWallet->getAccountForID(mainAccId);
      EXPECT_TRUE(accPtr->isLegacy());
   }

   //get AddrVec
   auto hashSetWO = woWallet->getAddrHashSet();

   ASSERT_EQ(hashSet, hashSetWO);
   auto woFilename = woWallet->getDbFilename();
   woWallet.reset();

   //reload the WO wallet
   auto passLbd = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString("control");
   };

   auto reloadWoWallet = AssetWallet_Single::loadMainWalletFromFile(woFilename, passLbd);
   auto hashSetWO2 = reloadWoWallet->getAddrHashSet();
   ASSERT_EQ(hashSet, hashSetWO);
   EXPECT_EQ(mainAccId, reloadWoWallet->getMainAccountID());
   {
      auto accPtr = reloadWoWallet->getAccountForID(mainAccId);
      EXPECT_TRUE(accPtr->isLegacy());
   }

   //delete the underlying file so we can fork anew
   reloadWoWallet.reset();
   std::filesystem::remove(woFilename.c_str());

   //fork WO from full wallet
   auto forkFilename = AssetWallet_Single::forkWatchingOnly(filename, passLbd);

   auto woFork = AssetWallet::loadMainWalletFromFile(forkFilename, passLbd);
   auto hashSetFork = woFork->getAddrHashSet();
   ASSERT_EQ(hashSet, hashSetFork);
   EXPECT_EQ(mainAccId, woFork->getMainAccountID());
   {
      auto accPtr = woFork->getAccountForID(mainAccId);
      EXPECT_TRUE(accPtr->isLegacy());
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, IDs)
{
   auto rawEntropy = CryptoPRNG::generateRandom(32);
   auto rawPubkey = CryptoECDSA().ComputePublicKey(rawEntropy);
   string id;
   {
      auto chaincode = BtcUtils::computeChainCode_Armory135(rawEntropy);
      id = generateWalletId(rawPubkey, chaincode, SeedType::Armory135);
   }
   ASSERT_FALSE(id.empty());

   WalletCreationParams params{
      SecureBinaryData::fromString("passphrase"),
      SecureBinaryData::fromString("control"),
      homedir_, 4, 1, 1};

   //legacy wallet
   {
      unique_ptr<Armory::Seeds::ClearTextSeed> seed(
         new Armory::Seeds::ClearTextSeed_Armory135(rawEntropy));
      auto wlt = AssetWallet_Single::createFromSeed(
         move(seed), params);

      EXPECT_EQ(wlt->getID(), id);
   }

   //bip32 wallet
   {
      unique_ptr<Armory::Seeds::ClearTextSeed> seed(
         new Armory::Seeds::ClearTextSeed_BIP32(
            rawEntropy,
            Armory::Seeds::SeedType::BIP32_Structured));
      auto wlt = AssetWallet_Single::createFromSeed(
         move(seed), params);

      //wallet id
      BIP32_Node node;
      node.initFromSeed(rawEntropy);

      auto idBip32 = generateWalletId(node.getPublicKey(), node.getChaincode(),
         SeedType::BIP32_Structured);
      EXPECT_EQ(wlt->getID(), idBip32);

      //account ids
      auto seedFingerprint = node.getThisFingerprint();
      auto generateAddrAccountId = [&seedFingerprint](
         const vector<uint32_t>& nodes,
         const set<AddressEntryType>& addrTypeSet,
         bool isMain)->int32_t
      {
         BinaryWriter bw;
         bw.put_uint32_t(seedFingerprint);
         for (const auto& node : nodes)
            bw.put_uint32_t(node, BE);
         bw.put_uint32_t(0);

         for (const auto& node : nodes)
            bw.put_uint32_t(node, BE);
         bw.put_uint32_t(1, BE);

         for (auto& addrType : addrTypeSet)
            bw.put_uint32_t(addrType, BE);
         bw.put_uint32_t(*addrTypeSet.begin());
         bw.put_uint8_t(isMain);

         auto pub_hash160 = BtcUtils::getHash160(bw.getData());
         BinaryRefReader brr(pub_hash160.getRef());
         return brr.get_int32_t(BE);
      };

      AddressAccountId acc44(generateAddrAccountId(
         {0x8000002C, 0x80000000, 0x80000000},
         { AddressEntryType(
            AddressEntryType_P2PKH | AddressEntryType_Uncompressed),
         AddressEntryType_P2PKH },
         true));
      AssetAccountId outer44(acc44, 0);
      AssetAccountId inner44(acc44, 1);

      AddressAccountId acc49(generateAddrAccountId(
         {0x80000031, 0x80000000, 0x80000000},
         {AddressEntryType(AddressEntryType_P2SH | AddressEntryType_P2WPKH)},
         false));
      AssetAccountId outer49(acc49, 0);
      AssetAccountId inner49(acc49, 1);

      AddressAccountId acc84(generateAddrAccountId(
         {0x80000054, 0x80000000, 0x80000000},
         {AddressEntryType_P2WPKH},
         false));
      AssetAccountId outer84(acc84, 0);
      AssetAccountId inner84(acc84, 1);

      auto accIdSet = wlt->getAccountIDs();
      auto checkId = [&accIdSet, wlt](const AddressAccountId& accId,
         const AssetAccountId& outId, const AssetAccountId& innId)->bool
      {
         auto iter = accIdSet.find(accId);
         if (iter == accIdSet.end())
            return false;
         accIdSet.erase(iter);

         auto accPtr = wlt->getAccountForID(accId);
         if (accPtr->getOuterAccountID() != outId)
            return false;

         if (accPtr->getInnerAccountID() != innId)
            return false;

         return true;
      };

      EXPECT_TRUE(checkId(acc44, outer44, inner44));
      EXPECT_TRUE(checkId(acc49, outer49, inner49));
      EXPECT_TRUE(checkId(acc84, outer84, inner84));
      EXPECT_TRUE(accIdSet.empty());
   }

   //legacy with chaincode
   auto chaincode = CryptoPRNG::generateRandom(32);
   auto idcc = generateWalletId(rawPubkey, chaincode,
      SeedType::Armory135);
   ASSERT_NE(id, idcc);

   {
      unique_ptr<Armory::Seeds::ClearTextSeed> seed(
         new Armory::Seeds::ClearTextSeed_Armory135(
            rawEntropy, chaincode));
      auto wlt = AssetWallet_Single::createFromSeed(
         move(seed), params);

      EXPECT_EQ(wlt->getID(), idcc);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, ID_fromSeeds)
{
   auto rawSBD = SecureBinaryData::CreateFromHex(
      "0102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F20");

   //Armory135
   {
      auto seed135 = make_unique<ClearTextSeed_Armory135>(
         rawSBD, ClearTextSeed_Armory135::LegacyType::Armory135);
      EXPECT_EQ(seed135->getWalletId(), "2vrVAxyHR");
      EXPECT_EQ(seed135->getMasterId(), "LZxsEgeT");
   }

   //Armory200a
   {
      auto seed135 = make_unique<ClearTextSeed_Armory135>(
         rawSBD, ClearTextSeed_Armory135::LegacyType::Armory200);
      EXPECT_EQ(seed135->getWalletId(), "2vrVAxyHR");
      EXPECT_EQ(seed135->getMasterId(), "LZxsEgeT");
   }

   //BIP32 structured
   {
      auto bip32 = make_unique<ClearTextSeed_BIP32>(
         rawSBD, SeedType::BIP32_Structured);
      EXPECT_EQ(bip32->getWalletId(), "2BuhCGwV9");
      EXPECT_EQ(bip32->getMasterId(), "2d9H95rzK");
   }

   //BIP32 virgin
   {
      auto bip32 = make_unique<ClearTextSeed_BIP32>(
         rawSBD, SeedType::BIP32_Virgin);
      EXPECT_EQ(bip32->getWalletId(), "22bd31PB5");
      EXPECT_EQ(bip32->getMasterId(), "2d9H95rzK");
   }

   //xpriv
   {
      BIP32_Node node;
      node.initFromSeed(rawSBD);
      auto xpriv = node.getBase58();

      auto base58 = ClearTextSeed_BIP32::fromBase58(xpriv);
      EXPECT_EQ(base58->getWalletId(), "33qBfTB51");
      EXPECT_EQ(base58->getMasterId(), "2d9H95rzK");
   }


   //BIP39
   {
      auto bip32 = make_unique<ClearTextSeed_BIP39>(rawSBD,
         ClearTextSeed_BIP39::Dictionnary::English_Trezor);
      EXPECT_EQ(bip32->getWalletId(), "vUXT83m9");
      EXPECT_EQ(bip32->getMasterId(), "WLKZBhnX");
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, Encryption_Test)
{
   //#1: check deriving from an encrypted root yield correct chain
   //create 1 wallet from priv key
   WalletCreationParams params{
      SecureBinaryData::fromString("passphrase"),
      SecureBinaryData::fromString("control"),
      homedir_, 4, 1, 1};

   auto rawEntropy = CryptoPRNG::generateRandom(32);
   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135(rawEntropy));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   //derive private chain from root
   auto&& chaincode = BtcUtils::computeChainCode_Armory135(rawEntropy);

   vector<SecureBinaryData> privateKeys;
   auto currentPrivKey = &rawEntropy;

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
   auto accountPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
   auto outerAcc = accountPtr->getOuterAccount();
   for (int i = 0; i < 4; i++)
   {
      //grab indexes from 0 to 3
      auto assetptr = outerAcc->getAssetForKey(i);
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

   //open db env for wallet
   auto passLbd = []
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return SecureBinaryData::fromString("control");
   };

   IO::WalletDBInterface dbIface;
   dbIface.setupEnv(filename, true, passLbd, 0);
   string dbName;
   
   {
      auto tx = dbIface.beginReadTransaction(WALLETHEADER_DBNAME);
      BinaryWriter bwKey;
      bwKey.put_uint32_t(MAINWALLET_KEY);
      auto mainIdRef = tx->getDataRef(bwKey.getData());
      
      BinaryRefReader brr(mainIdRef);
      auto len = brr.get_var_int();
      auto mainIdBd = brr.get_BinaryData(len);
      dbName = string(mainIdBd.getCharPtr(), mainIdBd.getSize());
   }

   auto tx = dbIface.beginReadTransaction(dbName);

   ASSERT_EQ(checkDb(tx.get(), privateKeys), 0U);
   ASSERT_EQ(checkDb(tx.get(), publicKeys), 4U);

   /*
   Parse file for the presence of keys, neither should be visible as 
   the whole thing is encrypted
   */
   for (auto& privkey : privateKeys)
   {
      ASSERT_FALSE(TestUtils::searchFile(filename, privkey));
   }

   for (auto& pubkey : publicKeys)
   {
      ASSERT_FALSE(TestUtils::searchFile(filename, pubkey));
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, SeedEncryption)
{
   //create wallet
   auto passphrase = SecureBinaryData::fromString("password");
   WalletCreationParams params{
      passphrase,
      SecureBinaryData::fromString("control"),
      homedir_, 10, 1, 1};

   //create regular wallet
   auto rawEntropy = CryptoPRNG::generateRandom(32);

   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         rawEntropy, Armory::Seeds::SeedType::BIP32_Structured));
   auto wlt = AssetWallet_Single::createFromSeed(
      move(seed), params);
   //check clear text seed does not exist on disk
   auto filename = wlt->getDbFilename();
   ASSERT_FALSE(TestUtils::searchFile(filename, rawEntropy));

   //grab without passphrase lbd, should fail
   try
   {
      auto lock = wlt->lockDecryptedContainer();
      auto decryptedSeed = wlt->getDecryptedValue(wlt->getEncryptedSeed());
      EXPECT_EQ(decryptedSeed, rawEntropy);
      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException&)
   {}

   //set passphrase lambda
   auto passLbd = [&passphrase]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return passphrase;
   };
   wlt->setPassphrasePromptLambda(passLbd);

   //grab without locking, should fail
   try
   {
      auto decryptedSeed = wlt->getDecryptedValue(wlt->getEncryptedSeed());
      EXPECT_EQ(decryptedSeed, rawEntropy);
      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException&)
   {}

   //lock, grab and check
   try
   {
      auto lock = wlt->lockDecryptedContainer();
      auto clearTextSeed = ClearTextSeed::deserialize(
         wlt->getDecryptedValue(wlt->getEncryptedSeed()));
      auto seedBip32 = dynamic_cast<ClearTextSeed_BIP32*>(clearTextSeed.get());
      EXPECT_EQ(seedBip32->getRawEntropy(), rawEntropy);
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
      EXPECT_EQ(decryptedSeed, rawEntropy);
      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException&)
   {}

   //shutdown wallet
   wlt.reset();

   //create WO
   auto woFilename = AssetWallet::forkWatchingOnly(filename, controlLbd_);

   //check it has no seed
   auto wo = AssetWallet::loadMainWalletFromFile(woFilename, controlLbd_);
   auto woWlt = dynamic_pointer_cast<AssetWallet_Single>(wo);

   ASSERT_NE(woWlt, nullptr);
   EXPECT_EQ(woWlt->getEncryptedSeed(), nullptr);

   //reload wallet
   ASSERT_EQ(wlt, nullptr);
   auto wltReload = AssetWallet::loadMainWalletFromFile(filename, controlLbd_);
   wlt = dynamic_pointer_cast<AssetWallet_Single>(wltReload);
   ASSERT_NE(wlt, nullptr);

   //check seed again
   wlt->setPassphrasePromptLambda(passLbd);
   try
   {
      auto lock = wlt->lockDecryptedContainer();
      auto clearTextSeed = ClearTextSeed::deserialize(
         wlt->getDecryptedValue(wlt->getEncryptedSeed()));
      auto seedBip32 = dynamic_cast<ClearTextSeed_BIP32*>(clearTextSeed.get());
      EXPECT_EQ(seedBip32->getRawEntropy(), rawEntropy);
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
   WalletCreationParams params{
      SecureBinaryData::fromString("passphrase"),
      controlPass_,
      homedir_, 4, 1, 1};

   auto rawEntropy = CryptoPRNG::generateRandom(32);
   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135(rawEntropy));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   auto passLbd = []
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return SecureBinaryData::fromString("passphrase");
   };
   assetWlt->setPassphrasePromptLambda(passLbd);

   //derive private chain from root
   auto&& chaincode = BtcUtils::computeChainCode_Armory135(rawEntropy);

   vector<SecureBinaryData> privateKeys;
   auto currentPrivKey = &rawEntropy;

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
      auto accPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
      auto outerAcc = accPtr->getOuterAccount();
      ASSERT_EQ(outerAcc->getAssetCount(), 10U);

      //none of the new assets should have private keys
      for (unsigned i = 4; i < 10; i++)
      {
         auto asseti = outerAcc->getAssetForKey(i);
         ASSERT_FALSE(asseti->hasPrivateKey());
      }

      //grab last asset with a priv key
      auto asset3 = outerAcc->getAssetForKey(3);
      auto asset3_single = dynamic_pointer_cast<AssetEntry_Single>(asset3);
      if (asset3_single == nullptr)
         throw runtime_error("unexpected asset entry type");
      auto& privkey3 = assetWlt->getDecryptedValue(asset3_single->getPrivKey());

      //check privkey
      ASSERT_EQ(privkey3, privateKeys[3]);

      //extend private chain to 10 entries
      assetWlt->extendPrivateChainToIndex(assetWlt->getMainAccountID(), 9);

      //there should still be 10 assets
      ASSERT_EQ(outerAcc->getAssetCount(), 10U);

      //try to grab 10th private key
      auto asset9 = outerAcc->getAssetForKey(9);
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
      auto accPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
      auto outerAcc = accPtr->getOuterAccount();
      ASSERT_EQ(outerAcc->getAssetCount(), 4U);

      //grab 4th privkey
      auto asset3 = outerAcc->getAssetForKey(3);
      auto asset3_single = dynamic_pointer_cast<AssetEntry_Single>(asset3);
      if (asset3_single == nullptr)
         throw runtime_error("unexpected asset entry type");
      auto& privkey3 = assetWlt->getDecryptedValue(asset3_single->getPrivKey());

      //check privkey
      ASSERT_EQ(privkey3, privateKeys[3]);

      int deriveCount = 0;
      auto deriveCallback = [&deriveCount](int count)->void
      {
         deriveCount += count;
      };

      //extend address chain to 10 entries
      assetWlt->extendPublicChainToIndex(
         assetWlt->getMainAccountID(), 9, deriveCallback);

      ASSERT_EQ(outerAcc->getAssetCount(), 10U);
      ASSERT_EQ(deriveCount, 21);

      //none of the new assets should have private keys
      for (unsigned i = 4; i < 10; i++)
      {
         auto asseti = outerAcc->getAssetForKey(i);
         ASSERT_FALSE(asseti->hasPrivateKey());
      }
   }

   if (t2.joinable())
      t2.join();

   //wallet should be unlocked now
   ASSERT_FALSE(assetWlt->isDecryptedContainerLocked());

   //delete wallet, reload and check private keys are on disk and valid
   auto wltID = assetWlt->getID();
   auto filename = assetWlt->getDbFilename();
   assetWlt.reset();

   auto newWallet = AssetWallet::loadMainWalletFromFile(filename, controlLbd_);
   auto wltSingle = dynamic_pointer_cast<AssetWallet_Single>(newWallet);

   ASSERT_NE(wltSingle, nullptr);
   ASSERT_FALSE(wltSingle->isDecryptedContainerLocked());
   wltSingle->setPassphrasePromptLambda(passLbd);

   auto accountPtr = wltSingle->getAccountForID(wltSingle->getMainAccountID());
   auto outerAcc = accountPtr->getOuterAccount();
   auto lastlock = wltSingle->lockDecryptedContainer();
   for (unsigned i = 0; i < 10; i++)
   {
      auto asseti = outerAcc->getAssetForKey(i);
      auto asseti_single = dynamic_pointer_cast<AssetEntry_Single>(asseti);
      ASSERT_NE(asseti_single, nullptr);

      auto& asseti_privkey = wltSingle->getDecryptedValue(
         asseti_single->getPrivKey());

      ASSERT_EQ(asseti_privkey, privateKeys[i]);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, ControlPassphrase_Test)
{
   auto goodPassLbd = []
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return SecureBinaryData::fromString("control");
   };

   auto noPassLbd = []
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return SecureBinaryData();
   };

   auto checkSubDbValues = [](
      shared_ptr<AssetWallet> wlt,
      const string& dbName,
      map<BinaryData, BinaryData> dataMap)->bool
   {
      auto tx = wlt->beginSubDBTransaction(dbName, false);
      auto iter = tx->getIterator();

      while (iter->isValid())
      {
         auto key = iter->key();
         auto mapIter = dataMap.find(key);
         if (mapIter != dataMap.end())
         {
            if (mapIter->second == iter->value())
               dataMap.erase(mapIter);
         }

         iter->advance();
      }

      return dataMap.size() == 0;
   };

   //create wallet with control passphrase
   map<BinaryData, BinaryData> subDbData;
   for (unsigned i=0; i<20; i++)
   {
      subDbData.insert(make_pair(
         CryptoPRNG::generateRandom(20),
         CryptoPRNG::generateRandom(124)));
   }

   std::filesystem::path filename;
   set<BinaryData> addrSet;
   {
      WalletCreationParams params{
         SecureBinaryData::fromString("test"),
         SecureBinaryData::fromString("control"),
         homedir_, 4, 1, 1};

      unique_ptr<Armory::Seeds::ClearTextSeed> seed(
         new Armory::Seeds::ClearTextSeed_Armory135());
      auto assetWlt = AssetWallet_Single::createFromSeed(
         move(seed), params);
      filename = assetWlt->getDbFilename();
      addrSet = assetWlt->getAddrHashSet();
      ASSERT_EQ(addrSet.size(), 12ULL);

      unsigned count = 0;
      auto badPassLbd = [&count]
         (const set<EncryptionKeyId>&)
         ->SecureBinaryData
      {
         while (count++ < 3)
            return CryptoPRNG::generateRandom(15);
         return SecureBinaryData();
      };

      //with bad pass
      try
      {
         assetWlt->addSubDB("test-subdb", badPassLbd);
         ASSERT_TRUE(false);
      }
      catch (exception& e)
      {
         EXPECT_EQ(e.what(), string("empty passphrase"));
      }

      //with good pass
      assetWlt->addSubDB("test-subdb", goodPassLbd);
      
      //set some subdb values
      {
         auto&& tx = assetWlt->beginSubDBTransaction("test-subdb", true);
         auto mapToWrite = subDbData;
         for (auto& keyVal : mapToWrite)
            tx->insert(keyVal.first, keyVal.second);
      }

      EXPECT_TRUE(checkSubDbValues(assetWlt, "test-subdb", subDbData));
   }

   {
      unsigned badPassCtr = 0;
      auto badPassLbd = [&badPassCtr]
         (const set<EncryptionKeyId>&)
         ->SecureBinaryData
      {
         if (badPassCtr++ > 3)
            return SecureBinaryData();
         return CryptoPRNG::generateRandom(20);
      };

      try
      {
         auto assetWlt = AssetWallet::loadMainWalletFromFile(
            filename, badPassLbd);
         ASSERT_TRUE(false);
      }
      catch(DecryptedDataContainerException& e)
      {
         EXPECT_EQ(e.what(), string("empty passphrase"));
      }

      try
      {
         auto assetWlt = AssetWallet::loadMainWalletFromFile(
            filename, noPassLbd);
         ASSERT_TRUE(false);
      }
      catch(DecryptedDataContainerException& e)
      {
         EXPECT_EQ(e.what(), string("empty passphrase"));
         EXPECT_EQ(badPassCtr, 5U);
      }

      auto assetWlt = AssetWallet::loadMainWalletFromFile(
         filename, goodPassLbd);
      auto loadedAddrSet = assetWlt->getAddrHashSet();

      //wallet values
      EXPECT_EQ(addrSet, loadedAddrSet);
      EXPECT_TRUE(checkSubDbValues(assetWlt, "test-subdb", subDbData));
   }

   //create WO copy with different passphrase
   {
      EncryptionKeyId wltPassID;
      try
      {
         //try with bad pass, should fail
         auto badPassLbd = [&wltPassID]
            (const set<EncryptionKeyId>& ids)
            ->SecureBinaryData
         {
            if (!wltPassID.isValid())
            {
               if (ids.size() != 1)
                  throw range_error("");
               wltPassID = *ids.begin();
               return CryptoPRNG::generateRandom(10);
            }

            return SecureBinaryData(0);
         };
         auto woFilename = AssetWallet::forkWatchingOnly(filename, badPassLbd);
         ASSERT_TRUE(false);
      }
      catch (DecryptedDataContainerException& e)
      {
         EXPECT_EQ(e.what(), string("empty passphrase"));
      }

      //set different pass for WO fork
      auto passShift = [&wltPassID]
         (const set<EncryptionKeyId>& ids)
         ->SecureBinaryData
      {
         if (ids.size() == 1 && *ids.begin() == wltPassID)
            return SecureBinaryData::fromString("control");
         return SecureBinaryData::fromString("newwopass");
      };
      auto woFilename = AssetWallet::forkWatchingOnly(filename, passShift); 

      //try to open WO with old pass, should fail
      try
      {
         unsigned ctr = 0;
         auto oldPassLbd = [&ctr]
            (const set<EncryptionKeyId>&)
            ->SecureBinaryData
         {
            while (ctr++ < 2)
               return CryptoPRNG::generateRandom(18);
            return SecureBinaryData();
         };
         auto woWlt = AssetWallet::loadMainWalletFromFile(woFilename, oldPassLbd);
      }
      catch (DecryptedDataContainerException& e)
      {
         EXPECT_EQ(e.what(), string("empty passphrase"));
      }

      auto newPassLbd = []
         (const set<EncryptionKeyId>&)
         ->SecureBinaryData
      {
         return SecureBinaryData::fromString("newwopass");
      };
      auto woWlt = AssetWallet::loadMainWalletFromFile(woFilename, newPassLbd);
      auto loadedAddrSet = woWlt->getAddrHashSet();
      EXPECT_EQ(addrSet, loadedAddrSet);
   }

   /***********/

   //create wallet with no passphrase
   auto emptyPassLbd = []
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      throw runtime_error("shouldn't get here");
   };

   std::filesystem::path filename2;
   {
      WalletCreationParams params{
         SecureBinaryData::fromString("test"), {},
         homedir_, 4, 1, 1};

      unique_ptr<Armory::Seeds::ClearTextSeed> seed(
         new Armory::Seeds::ClearTextSeed_BIP32(
            Armory::Seeds::SeedType::BIP32_Structured));
      auto assetWlt = AssetWallet_Single::createFromSeed(
         move(seed), params);
      filename2 = assetWlt->getDbFilename();
      addrSet = assetWlt->getAddrHashSet();
      ASSERT_EQ(addrSet.size(), 32ULL);

      //with good pass
      try
      {
         assetWlt->addSubDB("test-subdb", emptyPassLbd);
      }
      catch (runtime_error&)
      {
         ASSERT_FALSE(true);
      }

      //set some subdb values
      {
         auto&& tx = assetWlt->beginSubDBTransaction("test-subdb", true);
         auto mapToWrite = subDbData;
         for (auto& keyVal : mapToWrite)
            tx->insert(keyVal.first, keyVal.second);
      }

      EXPECT_TRUE(checkSubDbValues(assetWlt, "test-subdb", subDbData));
   }

   //try to load, check passphrase lambda is never hit
   {
      auto assetWlt = AssetWallet::loadMainWalletFromFile(
         filename2, emptyPassLbd);
      auto loadedAddrSet = assetWlt->getAddrHashSet();

      //wallet values
      EXPECT_EQ(addrSet, loadedAddrSet);
      EXPECT_TRUE(checkSubDbValues(assetWlt, "test-subdb", subDbData));
   }

   /***********/

   {
      //create WO copy (lambda that returns empty pass)
      auto woFilename = 
         AssetWallet_Single::forkWatchingOnly(filename2, noPassLbd);

      //check WO wallet has no passphrase
      auto wltWO = AssetWallet::loadMainWalletFromFile(
         woFilename, emptyPassLbd);
      auto loadedAddrSet = wltWO->getAddrHashSet();

      //wallet values
      EXPECT_EQ(addrSet, loadedAddrSet);

      //subdb won't be copied
      try
      {
         auto tx = wltWO->beginSubDBTransaction("test-subdb", false);
         ASSERT_FALSE(true);
      }
      catch (IO::WalletInterfaceException& e)
      {
         EXPECT_EQ(e.what(), string("invalid db name"));
      }

      //cleanup this WO
      wltWO.reset();
      std::filesystem::remove(woFilename);
   }

   /***********/
   
   {
      auto newPass = []
         (const set<EncryptionKeyId>&)
         ->SecureBinaryData
      {
         return SecureBinaryData::fromString("newpass");
      };

      //create WO with different pass
      auto woFilename = 
         AssetWallet_Single::forkWatchingOnly(filename2, newPass);

      unsigned count = 0;
      auto wrongPass = [&count]
         (const set<EncryptionKeyId>&)
         ->SecureBinaryData
      {
         while (count++ < 5)
            return CryptoPRNG::generateRandom(12);
         return SecureBinaryData();
      };

      try
      {
         auto wltWO = AssetWallet::loadMainWalletFromFile(
         woFilename, wrongPass);
         ASSERT_TRUE(false);
      }
      catch (DecryptedDataContainerException& e)
      {
         EXPECT_EQ(e.what(), string("empty passphrase"));
         EXPECT_EQ(count, 6U);
      }

      //check WO works with different pass
      auto wltWO = AssetWallet::loadMainWalletFromFile(
         woFilename, newPass);
      auto loadedAddrSet = wltWO->getAddrHashSet();

      //wallet values
      EXPECT_EQ(addrSet, loadedAddrSet);

      //subdb won't be copied
      try
      {
         auto tx = wltWO->beginSubDBTransaction("test-subdb", false);
         ASSERT_FALSE(true);
      }
      catch (IO::WalletInterfaceException& e)
      {
         EXPECT_EQ(e.what(), string("invalid db name"));
      }

      //delete control passphrase

      //with wrong pass
      try
      {
         count = 0;
         wltWO->eraseControlPassphrase(wrongPass);
         ASSERT_TRUE(false);
      }
      catch (DecryptedDataContainerException& e)
      {
         EXPECT_EQ(e.what(), string("empty passphrase"));
         EXPECT_EQ(count, 6U);
      }

      //with right pass
      wltWO->eraseControlPassphrase(newPass);

      //shutdown wallet
      wltWO.reset();

      //check pass is gone

      wltWO = AssetWallet::loadMainWalletFromFile(
         woFilename, emptyPassLbd);

      loadedAddrSet = wltWO->getAddrHashSet();

      //wallet values
      EXPECT_EQ(addrSet, loadedAddrSet);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, SignPassphrase_Test)
{
   //create wallet from priv key
   WalletCreationParams params{
      SecureBinaryData::fromString("test"),
      SecureBinaryData::fromString("control"),
      homedir_, 4, 1, 1};

   auto rawEntropy = CryptoPRNG::generateRandom(32);
   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135(rawEntropy));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   unsigned passphraseCount = 0;
   auto badPassphrase = [&passphraseCount]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      //pass wrong passphrase once then give up
      if (passphraseCount++ > 1)
         return SecureBinaryData();
      return SecureBinaryData::fromString("bad pass");
   };

   //set passphrase lambda
   assetWlt->setPassphrasePromptLambda(badPassphrase);

   //try to decrypt with wrong passphrase
   try
   {
      auto containerLock = assetWlt->lockDecryptedContainer();
      auto accountPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
      auto outerAcc = accountPtr->getOuterAccount();
      auto asset = outerAcc->getAssetForKey(0);
      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw runtime_error("unexpected asset entry type");

      assetWlt->getDecryptedValue(asset_single->getPrivKey());

      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException&)
   {
      EXPECT_EQ(passphraseCount, 3U);
   }

   passphraseCount = 0;
   auto goodPassphrase = [&passphraseCount]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      //pass wrong passphrase once then the right one
      if (passphraseCount++ > 1)
         return SecureBinaryData::fromString("test");
      return SecureBinaryData::fromString("another bad pass");
   };

   assetWlt->setPassphrasePromptLambda(goodPassphrase);

   //try to decrypt with wrong passphrase then right passphrase
   try
   {
      auto&& containerLock = assetWlt->lockDecryptedContainer();
      auto accountPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
      auto outerAcc = accountPtr->getOuterAccount();
      auto asset = outerAcc->getAssetForKey(0);
      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw runtime_error("unexpected asset entry type");

      auto& privkey = assetWlt->getDecryptedValue(asset_single->getPrivKey());

      //make sure decrypted privkey is valid
      auto&& chaincode = BtcUtils::computeChainCode_Armory135(rawEntropy);
      auto&& privkey_ex =
         CryptoECDSA().ComputeChainedPrivateKey(rawEntropy, chaincode);

      ASSERT_EQ(privkey, privkey_ex);
   }
   catch (DecryptedDataContainerException&)
   {
      ASSERT_TRUE(false);
   }

   EXPECT_EQ(passphraseCount, 3U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, WrongPassphrase_BIP32_Test)
{
   //create wallet from priv key
   WalletCreationParams params{
      SecureBinaryData::fromString("test"),
      SecureBinaryData::fromString("control"),
      homedir_, 4, 1, 1};

   auto rawEntropy = CryptoPRNG::generateRandom(32);
   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         rawEntropy, Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   unsigned passphraseCount = 0;
   auto badPassphrase = [&passphraseCount]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      //pass wrong passphrase once then give up
      if (passphraseCount++ > 1)
         return SecureBinaryData();
      return SecureBinaryData::fromString("bad pass");
   };

   //set passphrase lambda
   assetWlt->setPassphrasePromptLambda(badPassphrase);

   //try to decrypt with wrong passphrase
   try
   {
      auto containerLock = assetWlt->lockDecryptedContainer();
      auto accountPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
      auto outerAcc = accountPtr->getOuterAccount();
      auto asset = outerAcc->getAssetForKey(0);
      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw runtime_error("unexpected asset entry type");

      assetWlt->getDecryptedValue(asset_single->getPrivKey());

      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException&)
   {
      EXPECT_EQ(passphraseCount, 3U);
   }

   passphraseCount = 0;
   auto goodPassphrase = [&passphraseCount]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      //pass wrong passphrase once then the right one
      if (passphraseCount++ > 2)
         return SecureBinaryData::fromString("test");
      return SecureBinaryData::fromString("another bad pass");
   };


   //try to decrypt with wrong passphrase then the right one
   assetWlt->setPassphrasePromptLambda(goodPassphrase);
   try
   {
      auto&& containerLock = assetWlt->lockDecryptedContainer();
      auto accountPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
      auto outerAcc = accountPtr->getOuterAccount();
      auto asset = outerAcc->getAssetForKey(0);
      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw runtime_error("unexpected asset entry type");

      auto& privkey = assetWlt->getDecryptedValue(asset_single->getPrivKey());

      //make sure decrypted privkey is valid
      BIP32_Node node;
      node.initFromSeed(rawEntropy);

      node.derivePrivate(0x8000002C);
      node.derivePrivate(0x80000000);
      node.derivePrivate(0x80000000);
      node.derivePrivate(0);
      node.derivePrivate(0);

      ASSERT_EQ(privkey, node.getPrivateKey());
   }
   catch (DecryptedDataContainerException&)
   {
      ASSERT_TRUE(false);
   }

   EXPECT_EQ(passphraseCount, 4U);

   //add another account
   vector<unsigned> derPath2 =
   {
      0x800050aa,
      0x8000c103,
   };

   auto accTypePtr = assetWlt->makeNewBip32AccTypeObject(derPath2);
   accTypePtr->setAddressLookup(10);
   accTypePtr->setNodes({0, 1});
   accTypePtr->setOuterAccountID(0);

   auto newAccId = assetWlt->createBIP32Account(accTypePtr);
   auto accPtr = assetWlt->getAccountForID(newAccId);
   ASSERT_NE(accPtr, nullptr);

   //try and grab priv key with wrong passphrase
   passphraseCount = 0;
   assetWlt->setPassphrasePromptLambda(badPassphrase);

   try
   {
      auto containerLock = assetWlt->lockDecryptedContainer();
      auto outerAcc = accPtr->getOuterAccount();
      auto asset = outerAcc->getAssetForKey(5);
      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw runtime_error("unexpected asset entry type");

      assetWlt->getDecryptedValue(asset_single->getPrivKey());

      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException&)
   {
      EXPECT_EQ(passphraseCount, 3U);
   }

   //try to decrypt with wrong passphrase then the right one
   passphraseCount = 0;
   assetWlt->setPassphrasePromptLambda(goodPassphrase);
   try
   {
      auto&& containerLock = assetWlt->lockDecryptedContainer();
      auto outerAcc = accPtr->getOuterAccount();
      auto asset = outerAcc->getAssetForKey(5);
      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw runtime_error("unexpected asset entry type");

      auto& privkey = assetWlt->getDecryptedValue(asset_single->getPrivKey());

      //make sure decrypted privkey is valid
      BIP32_Node node;
      node.initFromSeed(rawEntropy);

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

   EXPECT_EQ(passphraseCount, 4U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, ChangePassphrase_Test)
{
   //create wallet from priv key
   WalletCreationParams params{
      SecureBinaryData::fromString("test"),
      controlPass_,
      homedir_, 4, 1, 1};

   auto rawEntropy = CryptoPRNG::generateRandom(32);
   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135(rawEntropy));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   auto&& chaincode = BtcUtils::computeChainCode_Armory135(rawEntropy);
   auto&& privkey_ex =
      CryptoECDSA().ComputeChainedPrivateKey(rawEntropy, chaincode);
   auto filename = assetWlt->getDbFilename();


   //grab all IVs and encrypted private keys
   vector<SecureBinaryData> ivVec;
   vector<SecureBinaryData> privateKeys;

   struct EncryptionKeyEx : public EncryptionKey
   {
      const std::map<EncryptionKeyId, std::unique_ptr<CipherData>>&
         getCipherDataMap(void) const
      {
         return cipherDataMap_;
      }
   };

   struct DecryptedDataContainerEx : private DecryptedDataContainer
   {
      vector<SecureBinaryData> getMasterKeyIVs(void) const
      {
         vector<SecureBinaryData> result;
         for (auto& keyPair : encryptedKeys_)
         {
            auto encrKeyPtr = (EncryptionKeyEx*)keyPair.second.get();
            auto& cipherMap = encrKeyPtr->getCipherDataMap();

            for (auto& cipherPair : cipherMap)
            {
               auto cipherData = cipherPair.second.get();
               result.push_back(cipherData->cipher_->getIV());
            }
         }

         return result;
      }

      vector<SecureBinaryData> getMasterEncryptionKeys(void) const
      {
         vector<SecureBinaryData> result;
         for (auto& keyPair : encryptedKeys_)
         {
            auto encrKeyPtr = (EncryptionKeyEx*)keyPair.second.get();
            auto& cipherMap = encrKeyPtr->getCipherDataMap();

            for (auto& cipherPair : cipherMap)
            {
               auto cipherData = cipherPair.second.get();
               result.push_back(cipherData->cipherText_);
            }
         }

         return result;
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

      auto&& ivs = decryptedDataEx->getMasterKeyIVs();
      ivVec.insert(ivVec.end(), ivs.begin(), ivs.end());

      auto&& keys = decryptedDataEx->getMasterEncryptionKeys();
      privateKeys.insert(privateKeys.end(), keys.begin(), keys.end());
   }

   for (unsigned i = 0; i < 4; i++)
   {
      auto asseti = TestUtils::getMainAccountAssetForIndex(assetWlt, i);
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
   auto&& newPassphrase = SecureBinaryData::fromString("new pass");

   unsigned counter = 0;
   auto passphrasePrompt = [&counter]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      if (counter++ == 0)
         return SecureBinaryData::fromString("test");
      else
         return SecureBinaryData();
   };

   auto newPassLbd = [&newPassphrase](void)->SecureBinaryData
   {
      return newPassphrase;
   };

   {
      //set passphrase prompt lambda
      assetWlt->setPassphrasePromptLambda(passphrasePrompt);

      //lock the wallet, passphrase change should fail
      auto lock = assetWlt->lockDecryptedContainer();

      try
      {
         //change passphrase
         assetWlt->changePrivateKeyPassphrase(newPassLbd);
         ASSERT_TRUE(false);
      }
      catch (AlreadyLocked&)
      {}
   }

   {
      //try again without locking, should work
      try
      {
         //change passphrase
         assetWlt->changePrivateKeyPassphrase(newPassLbd);
      }
      catch (AlreadyLocked&)
      {
         ASSERT_TRUE(false);
      }
   }

   //try to decrypt with new passphrase
   auto newPassphrasePrompt = [&newPassphrase]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return newPassphrase;
   };

   {
      assetWlt->setPassphrasePromptLambda(newPassphrasePrompt);
      auto lock = assetWlt->lockDecryptedContainer();

      auto asset0 = TestUtils::getMainAccountAssetForIndex(assetWlt, 0);
      auto asset0_single = dynamic_pointer_cast<AssetEntry_Single>(asset0);
      ASSERT_NE(asset0_single, nullptr);

      auto& decryptedKey =
         assetWlt->getDecryptedValue(asset0_single->getPrivKey());

      ASSERT_EQ(decryptedKey, privkey_ex);
   }

   //close wallet, reload
   auto walletID = assetWlt->getID();
   assetWlt.reset();

   auto newWallet = AssetWallet::loadMainWalletFromFile(filename, controlLbd_);

   auto wltSingle =
      dynamic_pointer_cast<AssetWallet_Single>(newWallet);
   ASSERT_NE(wltSingle, nullptr);
   ASSERT_FALSE(wltSingle->isDecryptedContainerLocked());

   //grab all IVs and private keys again
   vector<SecureBinaryData> newIVs;
   vector<SecureBinaryData> newPrivKeys;

   {
      auto wltSingleEx = (AssetWalletEx*)wltSingle.get();
      auto decryptedDataEx =
         (DecryptedDataContainerEx*)wltSingleEx->getDecryptedDataContainer().get();

      auto&& ivs = decryptedDataEx->getMasterKeyIVs();
      newIVs.insert(newIVs.end(), ivs.begin(), ivs.end());

      auto keys = decryptedDataEx->getMasterEncryptionKeys();
      newPrivKeys.insert(newPrivKeys.end(), keys.begin(), keys.end());
   }

   for (unsigned i = 0; i < 4; i++)
   {
      auto asseti = TestUtils::getMainAccountAssetForIndex(wltSingle, i);
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

      auto asset0 = TestUtils::getMainAccountAssetForIndex(wltSingle, 0);
      auto asset0_single = dynamic_pointer_cast<AssetEntry_Single>(asset0);
      ASSERT_NE(asset0_single, nullptr);

      try
      {
         wltSingle->getDecryptedValue(asset0_single->getPrivKey());
         ASSERT_FALSE(true);
      }
      catch (...)
      {}

      //try to decrypt with new passphrase instead
      wltSingle->setPassphrasePromptLambda(newPassphrasePrompt);
      auto& decryptedKey =
         wltSingle->getDecryptedValue(asset0_single->getPrivKey());

      ASSERT_EQ(decryptedKey, privkey_ex);
   }

   //check on file values
   auto passLbd = []
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return SecureBinaryData::fromString("control");
   };

   IO::WalletDBInterface dbIface;
   dbIface.setupEnv(filename, true, passLbd, 0);
   string dbName;

   {
      auto tx = dbIface.beginReadTransaction(WALLETHEADER_DBNAME);
      BinaryWriter bwKey;
      bwKey.put_uint32_t(MAINWALLET_KEY);
      auto mainIdRef = tx->getDataRef(bwKey.getData());
      
      BinaryRefReader brr(mainIdRef);
      auto len = brr.get_var_int();
      auto mainIdBd = brr.get_BinaryData(len);
      dbName = string(mainIdBd.getCharPtr(), mainIdBd.getSize());
   }

   auto tx = dbIface.beginReadTransaction(dbName);

   ASSERT_EQ(checkDb(tx.get(), { privateKeys[0] }), 0U);
   ASSERT_EQ(checkDb(tx.get(), privateKeys), 4U);
   ASSERT_EQ(checkDb(tx.get(), { ivVec[0] }), 0U);
   ASSERT_EQ(checkDb(tx.get(), ivVec), 4U);

   ASSERT_EQ(checkDb(tx.get(), { newPrivKeys[0] }), 1U);
   ASSERT_EQ(checkDb(tx.get(), newPrivKeys), 5U);
   ASSERT_EQ(checkDb(tx.get(), { newIVs[0] }), 1U);
   ASSERT_EQ(checkDb(tx.get(), newIVs), 5U);

   //check values aren't on file
   ASSERT_FALSE(TestUtils::searchFile(filename, ivVec[0]));
   ASSERT_FALSE(TestUtils::searchFile(filename, privateKeys[0]));

   ASSERT_FALSE(TestUtils::searchFile(filename, newIVs[0]));
   ASSERT_FALSE(TestUtils::searchFile(filename, newPrivKeys[0]));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, ChangePassphrase_FromUnencryptedWallet_Test)
{
   //create wallet from priv key
   WalletCreationParams params{
      {},
      SecureBinaryData::fromString("control"),
      homedir_, 4, 1, 1};

   auto rawEntropy = CryptoPRNG::generateRandom(32);
   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135(rawEntropy));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   auto&& chaincode = BtcUtils::computeChainCode_Armory135(rawEntropy);
   auto&& privkey_ex =
      CryptoECDSA().ComputeChainedPrivateKey(rawEntropy, chaincode);
   auto filename = assetWlt->getDbFilename();

   auto newPass = SecureBinaryData::fromString("newpass");

   auto asset0 = TestUtils::getMainAccountAssetForIndex(assetWlt, 0);
   auto asset0_single = dynamic_pointer_cast<AssetEntry_Single>(asset0);
   ASSERT_NE(asset0_single, nullptr);

   //check the wallet has no passphrase
   auto emptyPassLbd = []
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return SecureBinaryData();
   };

   assetWlt->setPassphrasePromptLambda(emptyPassLbd);
   {
      auto lock = assetWlt->lockDecryptedContainer();
      auto& decryptedKey =
         assetWlt->getDecryptedValue(asset0_single->getPrivKey());

      ASSERT_EQ(decryptedKey, privkey_ex);
   }

   //try to add passhrase to an unencrypted wallet, should fail
   auto changePassLbd = [&newPass](void)->SecureBinaryData
   {
      return newPass;
   };

   try
   {
      assetWlt->addPrivateKeyPassphrase(changePassLbd);
      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException& e)
   {
      EXPECT_EQ(e.what(), string("cannot add passphrase to unencrypted wallet"));
   }

   //encrypt with new pass
   assetWlt->changePrivateKeyPassphrase(changePassLbd);
  
   //check the wallet can't be decrypted without a passphrase anymore
   try
   {
      auto lock = assetWlt->lockDecryptedContainer();
      assetWlt->getDecryptedValue(asset0_single->getPrivKey());
      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException& e)
   {
      EXPECT_EQ(e.what(), string("empty passphrase"));
   }

   //check the new pass works
   auto newPassLbd = [&newPass]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return newPass;
   };

   assetWlt->setPassphrasePromptLambda(newPassLbd);
   {
      auto lock = assetWlt->lockDecryptedContainer();
      auto& decryptedKey =
         assetWlt->getDecryptedValue(asset0_single->getPrivKey());

      ASSERT_EQ(decryptedKey, privkey_ex);
   }

   //try to add the same passphrase
   try
   {
      assetWlt->addPrivateKeyPassphrase(changePassLbd);
      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException& e)
   {
      EXPECT_EQ(e.what(), string("cipher data already present in encryption key"));
   }

   //check pass still works
   {
      auto lock = assetWlt->lockDecryptedContainer();
      auto& decryptedKey =
         assetWlt->getDecryptedValue(asset0_single->getPrivKey());

      ASSERT_EQ(decryptedKey, privkey_ex);
   }

   //add another passphrase
   auto newPass2 = SecureBinaryData::fromString("another pass");
   auto changePass2Lbd = [&newPass2](void)->SecureBinaryData
   {
      return newPass2;
   };
   assetWlt->addPrivateKeyPassphrase(changePass2Lbd);

   //check old pass works
   {
      auto lock = assetWlt->lockDecryptedContainer();
      auto& decryptedKey =
         assetWlt->getDecryptedValue(asset0_single->getPrivKey());

      ASSERT_EQ(decryptedKey, privkey_ex);
   }

   //check new pass works
   auto newPass2Lbd = [&newPass2]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return newPass2;
   };

   assetWlt->setPassphrasePromptLambda(newPass2Lbd);
   {
      auto lock = assetWlt->lockDecryptedContainer();
      auto& decryptedKey =
         assetWlt->getDecryptedValue(asset0_single->getPrivKey());

      ASSERT_EQ(decryptedKey, privkey_ex);
   }

   //delete old pass
   assetWlt->setPassphrasePromptLambda(newPassLbd);
   assetWlt->erasePrivateKeyPassphrase();

   //check old pass fails
   unsigned counter = 0;
   auto newPassLbdFail = [&counter, &newPass]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      while (counter++ < 4)
         return newPass;
      return SecureBinaryData();
   };
   assetWlt->setPassphrasePromptLambda(newPassLbdFail);

   try
   {
      auto lock = assetWlt->lockDecryptedContainer();
      assetWlt->getDecryptedValue(asset0_single->getPrivKey());
      ASSERT_TRUE(false);
   }
   catch (DecryptedDataContainerException& e)
   {
      EXPECT_EQ(e.what(), string("empty passphrase"));
      EXPECT_EQ(counter, 5U);
   }

   //check new pass works
   assetWlt->setPassphrasePromptLambda(newPass2Lbd);
   {
      auto lock = assetWlt->lockDecryptedContainer();
      auto& decryptedKey =
         assetWlt->getDecryptedValue(asset0_single->getPrivKey());

      ASSERT_EQ(decryptedKey, privkey_ex);
   }

   //delete new pass
   assetWlt->setPassphrasePromptLambda(newPass2Lbd);
   assetWlt->erasePrivateKeyPassphrase();

   counter = 0;
   auto emptyPassLbd2 = [&counter](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      ++counter;
      return SecureBinaryData();
   };
   assetWlt->setPassphrasePromptLambda(emptyPassLbd2);

   //check wallet is unencrypted
   {
      auto lock = assetWlt->lockDecryptedContainer();
      auto& decryptedKey =
         assetWlt->getDecryptedValue(asset0_single->getPrivKey());

      ASSERT_EQ(decryptedKey, privkey_ex);
      EXPECT_EQ(counter, 0U);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, ChangeControlPassphrase_Test)
{

   auto newPass = SecureBinaryData::fromString("newpass");
   WalletCreationParams params{
      SecureBinaryData::fromString("test"),
      SecureBinaryData::fromString("control"),
      homedir_, 40, 1, 1};

   //create wallet
   std::filesystem::path filename;
   {
      unique_ptr<Armory::Seeds::ClearTextSeed> seed(
         new Armory::Seeds::ClearTextSeed_BIP32(
            Armory::Seeds::SeedType::BIP32_Structured));
      auto assetWlt = AssetWallet_Single::createFromSeed(
         move(seed), params);
      filename = assetWlt->getDbFilename();

      //change control pass
      auto passLbd = [](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData::fromString("control");
      };

      auto newPass1Lbd = [newPass](void)->SecureBinaryData
      {
         return newPass;
      };
      assetWlt->changeControlPassphrase(newPass1Lbd, passLbd);

      //close wallet by scoping out
   }

   //open with old pass, should fail
   unsigned oldCounter = 0;
   auto oldPassLbd = [&oldCounter]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      while (oldCounter++ < 10)
         return SecureBinaryData::fromString("control");
      return SecureBinaryData();
   };

   try
   {
      auto wlt = AssetWallet::loadMainWalletFromFile(filename, oldPassLbd);
      ASSERT_FALSE(true);
   }
   catch (DecryptedDataContainerException& e)
   {
      EXPECT_EQ(e.what(), string("empty passphrase"));
      EXPECT_EQ(oldCounter, 11U);
   }

   //open with any/empty pass, should fail
   unsigned counter = 0;
   auto anyPassLbd = [&counter]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      while (counter++ < 10)
         return BtcUtils::fortuna_.generateRandom(20);
      return SecureBinaryData();
   };

   try
   {
      auto wlt = AssetWallet::loadMainWalletFromFile(filename, anyPassLbd);
      ASSERT_FALSE(true);
   }
   catch (DecryptedDataContainerException& e)
   {
      EXPECT_EQ(e.what(), string("empty passphrase"));
      EXPECT_EQ(counter, 11U);
   }

   //open with new pass, should work
   auto newPassLbd = [&newPass]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return newPass;
   };

   try
   {
      auto wlt = AssetWallet::loadMainWalletFromFile(filename, newPassLbd);
      //change pass again from the loaded wallet
      auto&& newPass2 = SecureBinaryData::fromString("second-pass");
      auto newPass2Lbd = [newPass2](void)->SecureBinaryData
      {
         return newPass2;
      };

      wlt->changeControlPassphrase(newPass2Lbd, newPassLbd);
   }
   catch (DecryptedDataContainerException&)
   {
      ASSERT_FALSE(true);
   }

   //open with old pass, should fail
   oldCounter = 0;
   auto oldPassLbd2 = [&oldCounter, &newPass]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      while (oldCounter++ < 10)
         return newPass;
      return SecureBinaryData();
   };

   try
   {
      auto wlt = AssetWallet::loadMainWalletFromFile(filename, oldPassLbd2);
      ASSERT_FALSE(true);
   }
   catch (DecryptedDataContainerException& e)
   {
      EXPECT_EQ(e.what(), string("empty passphrase"));
      EXPECT_EQ(oldCounter, 11U);
   }

   //open with any/empty pass, should fail
   counter = 0;
   try
   {
      auto wlt = AssetWallet::loadMainWalletFromFile(filename, anyPassLbd);
      ASSERT_FALSE(true);
   }
   catch (DecryptedDataContainerException& e)
   {
      EXPECT_EQ(e.what(), string("empty passphrase"));
      EXPECT_EQ(counter, 11U);
   }

   //open with new pass, should work
   auto newPassLbd2 = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString("second-pass");
   };

   try
   {
      auto wlt = AssetWallet::loadMainWalletFromFile(filename, newPassLbd2);
   }
   catch (DecryptedDataContainerException&)
   {
      ASSERT_FALSE(true);
   }  
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, MultiplePassphrase_Test)
{
   //create wallet from priv key
   WalletCreationParams params{
      SecureBinaryData::fromString("test"), controlPass_,
      homedir_, 4, 1, 1};

   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   auto passLbd1 = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString("test");
   };

   auto passLbd2 = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString("abcdedfg");
   };

   auto newPassLbd = [](void)->SecureBinaryData
   {
      return SecureBinaryData::fromString("abcdedfg");
   };

   {
      //try to change passphrase by locking container first, should fail
      assetWlt->setPassphrasePromptLambda(passLbd1);
      auto lock = assetWlt->lockDecryptedContainer();

      try
      {
         assetWlt->addPrivateKeyPassphrase(newPassLbd);
         ASSERT_TRUE(false);
      }
      catch (AlreadyLocked&)
      {}
   }

   {
      //try without locking first, should work
      try
      {
         assetWlt->addPrivateKeyPassphrase(newPassLbd);
      }
      catch (AlreadyLocked&)
      {
         ASSERT_TRUE(false);
      }
   }

   SecureBinaryData key1, key2;
   {
      //try to decrypt with first passphrase, should work
      auto lock = assetWlt->lockDecryptedContainer();
      assetWlt->setPassphrasePromptLambda(passLbd1);

      auto asset0 = TestUtils::getMainAccountAssetForIndex(assetWlt, 0);
      auto asset0_single = dynamic_pointer_cast<AssetEntry_Single>(asset0);
      ASSERT_NE(asset0_single, nullptr);

      try
      {
         key1 =
            assetWlt->getDecryptedValue(asset0_single->getPrivKey());
      }
      catch (...)
      {
         ASSERT_FALSE(true);
      }
   }

   {
      //try to decrypt with second passphrase, should work
      auto lock = assetWlt->lockDecryptedContainer();
      assetWlt->setPassphrasePromptLambda(passLbd2);

      auto asset0 = TestUtils::getMainAccountAssetForIndex(assetWlt, 0);
      auto asset0_single = dynamic_pointer_cast<AssetEntry_Single>(asset0);
      ASSERT_NE(asset0_single, nullptr);

      try
      {
         key2 =
            assetWlt->getDecryptedValue(asset0_single->getPrivKey());
      }
      catch (...)
      {
         ASSERT_FALSE(true);
      }
   }

   EXPECT_EQ(key1, key2);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, BIP32_Chain)
{
   //BIP32 test 1 seed
   SecureBinaryData wltSeed = READHEX("000102030405060708090a0b0c0d0e0f");
   BIP32_Node seedNode;
   seedNode.initFromSeed(wltSeed);

   //0'/1/2'/2
   vector<unsigned> derivationPath = { 0x80000000, 1, 0x80000002 };
   auto account = AccountType_BIP32::makeFromDerPaths(
      seedNode.getThisFingerprint(), {derivationPath});
   account->setMain(true);
   account->setAddressLookup(4);

   WalletCreationParams params{
      SecureBinaryData::fromString("test"), controlPass_,
      homedir_, 0, 1, 1};

   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         wltSeed, Armory::Seeds::SeedType::BIP32_Virgin));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   {
      auto passphraseLbd = []
         (const set<EncryptionKeyId>&)
         ->SecureBinaryData
      {
         return SecureBinaryData::fromString("test");
      };
      assetWlt->setPassphrasePromptLambda(passphraseLbd);
      assetWlt->createBIP32Account(account);
   }

   auto passphrasePrompt = []
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return SecureBinaryData::fromString("test");
   };

   assetWlt->setPassphrasePromptLambda(passphrasePrompt);
   auto lock = assetWlt->lockDecryptedContainer();

   auto assetPtr = TestUtils::getMainAccountAssetForIndex(assetWlt, 2);
   auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(assetPtr);
   ASSERT_NE(assetSingle, nullptr);

   auto& decryptedKey =
      assetWlt->getDecryptedValue(assetSingle->getPrivKey());

   BIP32_Node privNode;
   auto&& priv_b58 =
      SecureBinaryData::fromString("xprvA2JDeKCSNNZky6uBCviVfJSKyQ1mDYahRjijr5idH2WwLsEd4Hsb2Tyh8RfQMuPh7f7RtyzTtdrbdqqsunu5Mm3wDvUAKRHSC34sJ7in334");
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
   auto seedFingerprint = seedNode.getThisFingerprint();
   for (auto& derId : derivationPath)
      seedNode.derivePrivate(derId);

   auto pubSeedNode = seedNode.getPublicCopy();

   //2
   vector<unsigned> derivationPath_Soft = { 2 };
   auto mainAccType =
      AccountType_BIP32::makeFromDerPaths(seedFingerprint, {derivationPath_Soft});
   mainAccType->setSeedRoot(pubSeedNode.getBase58());
   mainAccType->setMain(true);
   mainAccType->setAddressLookup(4);
   mainAccType->setDefaultAddressType(
      AddressEntryType(AddressEntryType_P2WPKH));
   mainAccType->addAddressType(AddressEntryType_P2WPKH);

   auto assetWlt = AssetWallet_Single::createBlank(
      homedir_,
      "a wallet",
      controlPass_); //set lookup computation to 4 entries

   assetWlt->createBIP32Account(mainAccType);

   auto accID = assetWlt->getMainAccountID();
   auto accPtr = assetWlt->getAccountForID(accID);
   auto outerAcc = accPtr->getOuterAccount();
   auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(outerAcc->getRoot());
   ASSERT_NE(assetSingle, nullptr);

   BIP32_Node pubNode;
   auto&& pub_b58 =
      SecureBinaryData::fromString("xpub6FHa3pjLCk84BayeJxFW2SP4XRrFd1JYnxeLeU8EqN3vDfZmbqBqaGJAyiLjTAwm6ZLRQUMv1ZACTj37sR62cfN7fe5JnJ7dh8zL4fiyLHV");
   pubNode.initFromBase58(pub_b58);

   EXPECT_EQ(assetSingle->getPubKey()->getCompressedKey(), pubNode.getPublicKey());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, BIP32_ArmoryDefault)
{
   vector<unsigned> derivationPath =
   {
      0x8000002C,
      0x80000000,
      0x80000000
   };

   auto rawEntropy = CryptoPRNG::generateRandom(32);

   //create empty wallet
   auto passphrase = SecureBinaryData::fromString("password");
   WalletCreationParams params{
      passphrase, controlPass_,
      homedir_, 5, 1, 1};

   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         rawEntropy, Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   auto mainAcc = assetWlt->getAccountForID(assetWlt->getMainAccountID());
   auto outerAcc = mainAcc->getOuterAccount();
   auto accRootPtr = dynamic_pointer_cast<AssetEntry_BIP32Root>(
      outerAcc->getRoot());

   BIP32_Node node;
   node.initFromSeed(rawEntropy);
   for (auto id : derivationPath)
      node.derivePrivate(id);
   node.derivePrivate(0);

   EXPECT_EQ(accRootPtr->getPubKey()->getCompressedKey(), node.getPublicKey());

   auto accIDs = assetWlt->getAccountIDs();
   for (auto& id : accIDs)
   {
      if (id != mainAcc->getID())
      {
         auto accID = AssetId(id, 0, 0);
         auto addrPtr = assetWlt->getNewAddress(id);
         auto assetID = assetWlt->getAssetIDForScrAddr(
            addrPtr->getPrefixedHash());
         EXPECT_EQ(assetID.first, accID);
      }
   }
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
   auto rawEntropy = CryptoPRNG::generateRandom(32);

   //create empty wallet
   auto passphrase = SecureBinaryData::fromString("password");
   WalletCreationParams params{
      passphrase, controlPass_,
      homedir_, 0, 1, 1};

   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         rawEntropy, Armory::Seeds::SeedType::BIP32_Virgin));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   //this is a hard derivation scenario, the wallet needs to be able to 
   //decrypt its root's private key
   auto passphraseLbd = [&passphrase]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return passphrase;
   };
   assetWlt->setPassphrasePromptLambda(passphraseLbd);

   auto accountPtr = assetWlt->makeNewBip32AccTypeObject(derivationPath1);
   accountPtr->setMain(true);
   accountPtr->setNodes({0, 1});
   accountPtr->setOuterAccountID(0);
   accountPtr->setAddressLookup(10);

   //add bip32 account for derivationPath1
   auto accountID1 = assetWlt->createBIP32Account(accountPtr);

   //derive bip32 node
   BIP32_Node seedNode;
   seedNode.initFromSeed(rawEntropy);
   for (auto& derId : derivationPath1)
      seedNode.derivePrivate(derId);

   auto outerNode = seedNode;
   outerNode.derivePrivate(0);

   {
      //check vs wallet account root
      auto accountPtr = assetWlt->getAccountForID(accountID1);
      auto outerAcc = accountPtr->getOuterAccount();
      auto accountRoot_BIP32 =
         dynamic_pointer_cast<AssetEntry_BIP32Root>(outerAcc->getRoot());
      auto& pubkeyAcc = accountRoot_BIP32->getPubKey()->getCompressedKey();
      EXPECT_EQ(pubkeyAcc, outerNode.getPublicKey());

      {
         //check encryption for the added account works

         //try to fetch without locking wallet
         try
         {
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

   auto accountTypePtr = assetWlt->makeNewBip32AccTypeObject(derivationPath2);
   accountTypePtr->addAddressType(AddressEntryType_P2WPKH);
   accountTypePtr->addAddressType(AddressEntryType_P2PK);
   accountTypePtr->setDefaultAddressType(AddressEntryType_P2WPKH);
   accountTypePtr->setNodes({ 50, 60 });
   accountTypePtr->setOuterAccountID(50);
   accountTypePtr->setInnerAccountID(60);
   accountTypePtr->setAddressLookup(100);

   //add bip32 custom account for derivationPath2
   auto accountID2 = assetWlt->createBIP32Account(accountTypePtr);

   BIP32_Node seedNode2;
   seedNode2.initFromSeed(rawEntropy);
   for (auto& derId : derivationPath2)
      seedNode2.derivePrivate(derId);
   seedNode2.derivePrivate(50);

   {
      //check vs wallet account root
      auto accountPtr1 = assetWlt->getAccountForID(accountID2);
      auto outerAcc1 = accountPtr1->getOuterAccount();
      auto accountRoot_BIP32 =
         dynamic_pointer_cast<AssetEntry_BIP32Root>(outerAcc1->getRoot());
      auto& pubkey2 = accountRoot_BIP32->getPubKey()->getCompressedKey();
      EXPECT_EQ(pubkey2, seedNode2.getPublicKey());

      //grab address 32, check vs derivation
      auto accountPtr2 = assetWlt->getAccountForID(accountID2);
      auto outerAcc2 = accountPtr2->getOuterAccount();
      auto assetPtr = outerAcc2->getAssetForKey(32);

      auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(assetPtr);
      ASSERT_NE(assetSingle, nullptr);

      seedNode2.derivePrivate(32);
      EXPECT_EQ(assetSingle->getPubKey()->getCompressedKey(),
         seedNode2.getPublicKey());
   }

   //close wallet, reload it, check again
   auto filename = assetWlt->getDbFilename();
   assetWlt.reset();

   auto assetWlt2 = AssetWallet::loadMainWalletFromFile(filename, controlLbd_);
   auto wltSingle2 = dynamic_pointer_cast<AssetWallet_Single>(assetWlt2);
   ASSERT_NE(wltSingle2, nullptr);

   {
      //check first account
      auto accPtr = wltSingle2->getAccountForID(accountID1);
      auto outerAcc = accPtr->getOuterAccount();
      auto accountRoot_BIP32 =
         dynamic_pointer_cast<AssetEntry_BIP32Root>(outerAcc->getRoot());
      auto& pubkeyAcc = accountRoot_BIP32->getPubKey()->getCompressedKey();
      EXPECT_EQ(pubkeyAcc, outerNode.getPublicKey());
   }

   {
      //check 2nd account
      auto accountPtr = wltSingle2->getAccountForID(accountID2);
      auto outerAcc = accountPtr->getOuterAccount();
      auto assetPtr = outerAcc->getAssetForKey(32);

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
      auto accPtr1 = wltSingle2->getAccountForID(accountID1);
      auto outerAcc1 = accPtr1->getOuterAccount();
      auto accountRoot_BIP32 =
         dynamic_pointer_cast<AssetEntry_BIP32Root>(outerAcc1->getRoot());
      auto& privKey = wltSingle2->getDecryptedValue(accountRoot_BIP32->getPrivKey());
      EXPECT_EQ(privKey, outerNode.getPrivateKey());

      //check 2nd account
      auto accPtr2 = wltSingle2->getAccountForID(accountID2);
      auto outerAcc2 = accPtr2->getOuterAccount();
      auto assetPtr = outerAcc2->getAssetForKey(32);

      auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(assetPtr);
      ASSERT_NE(assetSingle, nullptr);
      auto& privKey2 = wltSingle2->getDecryptedValue(assetSingle->getPrivKey());
      EXPECT_EQ(privKey2, seedNode2.getPrivateKey());
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, BIP32_Fork_WatchingOnly)
{
   auto passphrase = SecureBinaryData::fromString("password");
   WalletCreationParams params{
      passphrase, controlPass_,
      homedir_, 10, 1, 1};

   //create regular wallet
   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto wlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   //create WO copy
   auto woCopyPath = AssetWallet::forkWatchingOnly(
      wlt->getDbFilename(), controlLbd_);
   auto woWlt = AssetWallet::loadMainWalletFromFile(
      woCopyPath, controlLbd_);
   auto woSingle = dynamic_pointer_cast<AssetWallet_Single>(woWlt);

   //check WO roots have no private keys
   {
      EXPECT_TRUE(woSingle->isWatchingOnly());

      auto mainAccountID = woSingle->getMainAccountID();
      auto mainAccount = woSingle->getAccountForID(mainAccountID);
      auto root = mainAccount->getOuterAssetRoot();
      auto rootSingle = dynamic_pointer_cast<AssetEntry_BIP32Root>(root);
      EXPECT_EQ(rootSingle->getPrivKey(), nullptr);
   }

   //compare keys
   for (unsigned i = 0; i < 10; i++)
   {
      auto assetFull = TestUtils::getMainAccountAssetForIndex(wlt, i);
      auto assetFullSingle = dynamic_pointer_cast<AssetEntry_Single>(assetFull);

      auto assetWo = TestUtils::getMainAccountAssetForIndex(woSingle, i);
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
      auto filename = wlt->getDbFilename();
      wlt.reset();
      wlt = dynamic_pointer_cast<AssetWallet_Single>(
         AssetWallet::loadMainWalletFromFile(filename, controlLbd_));

      auto passphraseLBD = [&passphrase]
         (const set<EncryptionKeyId>&)
         ->SecureBinaryData
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
      auto assetFull = TestUtils::getMainAccountAssetForIndex(wlt, i);
      auto assetFullSingle = dynamic_pointer_cast<AssetEntry_Single>(assetFull);

      auto assetWo = TestUtils::getMainAccountAssetForIndex(woSingle, i);
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
TEST_F(WalletsTest, BIP32_WatchingOnly_FromXPub)
{
   vector<unsigned> derPath = {
      0x80000054,
      0x80000000,
      0x80000000
   };

   auto passphrase = SecureBinaryData::fromString("password");
   WalletCreationParams params{
      passphrase, controlPass_,
      homedir_, 10, 1, 1};

   //create regular wallet
   auto rawEntropy = CryptoPRNG::generateRandom(32);
   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         rawEntropy, Armory::Seeds::SeedType::BIP32_Structured));
   auto wlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   //get xpub for main account
   BIP32_Node seedNode;
   seedNode.initFromSeed(rawEntropy);
   auto seedFingerprint = seedNode.getThisFingerprint();
   for (auto& derId : derPath)
      seedNode.derivePrivate(derId);

   auto pubNode = seedNode.getPublicCopy();
   auto xpub = pubNode.getBase58();

   /* WO wallet creation */

   //1: create wallet
   auto wltWO = AssetWallet_Single::createBlank(
      homedir_, "walletWO1", controlPass_);

   //2: create a custom bip32 account meta data object to setup the WO account
   //structure (nodes & address types)
   auto accountTypePtr = AccountType_BIP32::makeFromDerPaths(
      seedFingerprint, {derPath});

   //set nodes
   set<unsigned> nodes = {
      BIP32_OUTER_ACCOUNT_DERIVATIONID,
      BIP32_INNER_ACCOUNT_DERIVATIONID};
   accountTypePtr->setNodes(nodes);

   //set xpub
   vector<PathAndRoot> pathsAndRoots;
   pathsAndRoots.emplace_back(derPath, xpub);
   accountTypePtr->setRoots(pathsAndRoots);

   //populate address types, here native SegWit only
   accountTypePtr->addAddressType(AddressEntryType_P2WPKH);

   //set the default address type as well
   accountTypePtr->setDefaultAddressType(AddressEntryType_P2WPKH);

   //set address lookup
   accountTypePtr->setAddressLookup(10);

   //and finally internal accounts
   accountTypePtr->setOuterAccountID(*nodes.begin());
   accountTypePtr->setInnerAccountID(*nodes.rbegin());

   //set account as main, there has to be a main account and this is the
   //the first one in this wallet
   accountTypePtr->setMain(true);

   //3: feed it to the wallet
   wltWO->createBIP32Account(accountTypePtr);

   //4: check address chain matches with original wallet
   auto addressWO = wltWO->getNewAddress();
   auto addressOriginal = wlt->getNewAddress(AddressEntryType_P2WPKH);

   EXPECT_EQ(addressWO->getAddress(), addressOriginal->getAddress());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, AddressEntryTypes)
{
   //create wallet
   auto passphrase = SecureBinaryData::fromString("password");
   WalletCreationParams params{
      passphrase, controlPass_,
      homedir_, 10, 1, 1};

   //create regular wallet
   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto wlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   //grab a bunch of addresses of various types
   set<BinaryData> addrHashes;

   //5 default addresses
   for (unsigned i = 0; i < 5; i++)
   {
      auto addrPtr = wlt->getNewAddress();
      addrHashes.insert(addrPtr->getPrefixedHash());
   }

   //5 p2wpkh
   for (unsigned i = 0; i < 5; i++)
   {
      auto addrPtr = wlt->getNewAddress(AddressEntryType_P2WPKH);
      addrHashes.insert(addrPtr->getPrefixedHash());
   }

   //5 nested p2wpkh change addresses
   for (unsigned i = 0; i < 5; i++)
   {
      auto addrPtr = wlt->getNewChangeAddress(AddressEntryType(
         AddressEntryType_P2SH | AddressEntryType_P2WPKH));
      addrHashes.insert(addrPtr->getPrefixedHash());
   }

   //shutdown wallet
   auto filename = wlt->getDbFilename();
   wlt.reset();

   //load from file
   auto loaded = AssetWallet::loadMainWalletFromFile(
      filename, controlLbd_);

   //check used address list from loaded wallet matches grabbed addresses
   {
      auto usedAddressMap = loaded->getUsedAddressMap();
      set<BinaryData> usedAddrHashes;
      for (auto& addrPair : usedAddressMap)
         usedAddrHashes.insert(addrPair.second->getPrefixedHash());

      EXPECT_EQ(addrHashes, usedAddrHashes);
   }

   //shutdown wallet
   loaded.reset();

   //create WO copy
   auto woFilename = AssetWallet::forkWatchingOnly(
      filename, controlLbd_);
   auto woLoaded = AssetWallet::loadMainWalletFromFile(
      woFilename, controlLbd_);

   {
      auto usedAddressMap = woLoaded->getUsedAddressMap();
      set<BinaryData> usedAddrHashes;
      for (auto& addrPair : usedAddressMap)
         usedAddrHashes.insert(addrPair.second->getPrefixedHash());

      EXPECT_EQ(addrHashes, usedAddrHashes);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, LegacyUncompressedAddressTypes)
{
   //create wallet
   vector<unsigned> derPath = {
      0x80000050,
      0x80005421,
      0x80000024,
      785
   };

   auto passphrase = SecureBinaryData::fromString("password");
   WalletCreationParams params{
      passphrase, controlPass_,
      homedir_, 0, 1, 1};

   //create regular wallet
   auto rawEntropy = CryptoPRNG::generateRandom(32);
   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         rawEntropy, Armory::Seeds::SeedType::BIP32_Virgin));
   auto wlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   //create account with all common uncompressed address types & their 
   //compressed counterparts
   auto accountTypePtr = wlt->makeNewBip32AccTypeObject(derPath);
   
   set<unsigned> nodes = {0, 1};
   accountTypePtr->setNodes(nodes);
   accountTypePtr->setOuterAccountID(*nodes.begin());
   accountTypePtr->setInnerAccountID(*nodes.rbegin());

   accountTypePtr->setDefaultAddressType(AddressEntryType_P2PKH);
   accountTypePtr->addAddressType(AddressEntryType_P2PKH);
   accountTypePtr->addAddressType(AddressEntryType(
      AddressEntryType_P2PKH | AddressEntryType_Uncompressed));
   accountTypePtr->addAddressType(AddressEntryType(
      AddressEntryType_P2PK | AddressEntryType_P2SH));

   accountTypePtr->setAddressLookup(20);
   accountTypePtr->setMain(true);

   auto passphraseLbd = [&passphrase]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return passphrase;
   };
   wlt->setPassphrasePromptLambda(passphraseLbd);
   wlt->createBIP32Account(accountTypePtr);
   wlt->resetPassphrasePromptLambda();

   //grab addresses for each type, check vs manual instantiation
   auto addr1 = wlt->getNewAddress(AddressEntryType_P2PKH);
   auto addr2 = wlt->getNewAddress(
      AddressEntryType(AddressEntryType_P2PKH | AddressEntryType_Uncompressed));
   auto addr3 = wlt->getNewAddress(
      AddressEntryType(AddressEntryType_P2PK | AddressEntryType_P2SH));

   //derive the keys locally and reproduce the addresses
   BIP32_Node bip32Node;
   bip32Node.initFromSeed(rawEntropy);
   for (auto& der : derPath)
      bip32Node.derivePrivate(der);
   bip32Node.derivePublic(0); //spender leaf

   {
      //addr1
      auto nodeCopy = bip32Node.getPublicCopy();
      nodeCopy.derivePublic(0); //asset #0

      auto pubkey = nodeCopy.getPublicKey();
      auto hash160 = BtcUtils::getHash160(pubkey);
      BinaryWriter bw;
      bw.put_uint8_t(BitcoinSettings::getPubkeyHashPrefix());
      bw.put_BinaryData(hash160);

      EXPECT_EQ(addr1->getPrefixedHash(), bw.getData());
   }

   {
      //addr2
      auto nodeCopy = bip32Node.getPublicCopy();
      nodeCopy.derivePublic(1); //asset #1

      auto pubkey = nodeCopy.getPublicKey();
      auto pubkey2 = CryptoECDSA().UncompressPoint(pubkey);
      auto hash160 = BtcUtils::getHash160(pubkey2);
      BinaryWriter bw;
      bw.put_uint8_t(BitcoinSettings::getPubkeyHashPrefix());
      bw.put_BinaryData(hash160);

      EXPECT_EQ(addr2->getPrefixedHash(), bw.getData());
   }

   {
      //addr3
      auto nodeCopy = bip32Node.getPublicCopy();
      nodeCopy.derivePublic(2); //asset #2

      auto pubkey = nodeCopy.getPublicKey();
      BinaryWriter bw;
      bw.put_uint8_t(33);
      bw.put_BinaryData(pubkey);
      bw.put_uint8_t(OP_CHECKSIG);

      BinaryWriter p2shBw;
      p2shBw.put_uint8_t(BitcoinSettings::getScriptHashPrefix());
      p2shBw.put_BinaryData(BtcUtils::getHash160(bw.getData()));

      EXPECT_EQ(addr3->getPrefixedHash(), p2shBw.getData());
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

   auto rawEntropy = CryptoPRNG::generateRandom(32);
   auto salt1 = CryptoPRNG::generateRandom(32);
   auto salt2 = CryptoPRNG::generateRandom(32);

   std::filesystem::path filename;
   AddressAccountId accountID1;
   AddressAccountId accountID2;

   set<BinaryData> addrHashSet;

   {
      //create empty wallet
      unique_ptr<Armory::Seeds::ClearTextSeed> seed(
         new Armory::Seeds::ClearTextSeed_BIP32(
            rawEntropy, Armory::Seeds::SeedType::BIP32_Virgin));
      auto passphrase = SecureBinaryData::fromString("password");
      WalletCreationParams params{
         passphrase, controlPass_,
         homedir_, 0, 1, 1};

      auto assetWlt = AssetWallet_Single::createFromSeed(
         move(seed), params);

      auto rootbip32 = dynamic_pointer_cast<AssetEntry_BIP32Root>(
         assetWlt->getRoot());
      ASSERT_NE(rootbip32, nullptr);

      auto passphraseLbd = [&passphrase]
         (const set<EncryptionKeyId>&)
         ->SecureBinaryData
      {
         return passphrase;
      };
      assetWlt->setPassphrasePromptLambda(passphraseLbd);

      //create accounts
      auto saltedAccType1 = AccountType_BIP32_Salted::makeFromDerPaths(
            rootbip32->getSeedFingerprint(true), {derivationPath1}, salt1);
      saltedAccType1->setAddressLookup(40);
      saltedAccType1->setDefaultAddressType(
         AddressEntryType_P2WPKH);
      saltedAccType1->addAddressType(AddressEntryType_P2WPKH);

      auto saltedAccType2 = AccountType_BIP32_Salted::makeFromDerPaths(
            rootbip32->getSeedFingerprint(true), {derivationPath2}, salt2);
      saltedAccType2->setAddressLookup(40);
      saltedAccType2->setDefaultAddressType(
         AddressEntryType_P2WPKH);
      saltedAccType2->addAddressType(AddressEntryType_P2WPKH);

      //add bip32 account for derivationPath1
      accountID1 = assetWlt->createBIP32Account(saltedAccType1);

      //add bip32 account for derivationPath2
      accountID2 = assetWlt->createBIP32Account(saltedAccType2);

      //grab 10 addresses
      vector<shared_ptr<AddressEntry>> addrVec1, addrVec2;
      for (unsigned i = 0; i < 10; i++)
      {
         addrVec1.push_back(assetWlt->getNewAddress(accountID1));
         addrVec2.push_back(assetWlt->getNewAddress(accountID2));
      }

      //derive from seed
      {
         BIP32_Node seedNode;
         seedNode.initFromSeed(rawEntropy);
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
         seedNode.initFromSeed(rawEntropy);
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
      ASSERT_EQ(addrHashSet.size(), 80ULL);

      //shut down the wallet
      filename = assetWlt->getDbFilename();
   }

   {
      auto assetWlt = AssetWallet::loadMainWalletFromFile(
         filename, controlLbd_);
      auto wltSingle = dynamic_pointer_cast<AssetWallet_Single>(assetWlt);

      //check current address map
      EXPECT_EQ(addrHashSet, assetWlt->getAddrHashSet());

      //grab more 10 addresses
      vector<shared_ptr<AddressEntry>> addrVec1, addrVec2;
      for (unsigned i = 0; i < 10; i++)
      {
         addrVec1.push_back(wltSingle->getNewAddress(accountID1));
         addrVec2.push_back(wltSingle->getNewAddress(accountID2));
      }

      //derive from seed
      {
         BIP32_Node seedNode;
         seedNode.initFromSeed(rawEntropy);
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
         seedNode.initFromSeed(rawEntropy);
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
      ASSERT_EQ(addrHashSet.size(), 80ULL);

      //create WO copy
      filename = AssetWallet_Single::forkWatchingOnly(
         filename, controlLbd_);
   }

   {
      auto assetWlt = AssetWallet::loadMainWalletFromFile(
         filename, controlLbd_);
      auto wltSingle = dynamic_pointer_cast<AssetWallet_Single>(assetWlt);

      ASSERT_TRUE(wltSingle->isWatchingOnly());
      EXPECT_EQ(addrHashSet, assetWlt->getAddrHashSet());

      auto accountSalted1 = wltSingle->getAccountForID(accountID1);
      auto accountSalted2 = wltSingle->getAccountForID(accountID2);

      //grab more 10 addresses
      vector<shared_ptr<AddressEntry>> addrVec1, addrVec2;
      for (unsigned i = 0; i < 10; i++)
      {
         addrVec1.push_back(wltSingle->getNewAddress(accountID1));
         addrVec2.push_back(wltSingle->getNewAddress(accountID2));
      }

      //derive from seed
      {
         BIP32_Node seedNode;
         seedNode.initFromSeed(rawEntropy);
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
         seedNode.initFromSeed(rawEntropy);
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
   std::filesystem::path filename, woFilename;

   auto&& privKey1 = READHEX(
      "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F");
   auto&& pubKey1 = CryptoECDSA().ComputePublicKey(privKey1, true);

   auto&& privKey2 = READHEX(
      "101112131415161718191A1B1C1D1E1F202122232425262728292A2B2C2D2E2F");
   auto&& pubKey2 = CryptoECDSA().ComputePublicKey(privKey2, true);


   auto&& passphrase = SecureBinaryData::fromString("password");

   map<unsigned, SecureBinaryData> saltMap1;
   map<unsigned, SecureBinaryData> saltMap2;

   AddressAccountId accID2;
   map<unsigned, BinaryData> addrMap1, addrMap2;

   {
      //create empty wallet
      WalletCreationParams params{
         passphrase, controlPass_,
         homedir_, 0, 1, 1};

      unique_ptr<Armory::Seeds::ClearTextSeed> seed(
         new Armory::Seeds::ClearTextSeed_BIP32(
            Armory::Seeds::SeedType::BIP32_Virgin));
      auto assetWlt = AssetWallet_Single::createFromSeed(
         move(seed), params);

      auto passphraseLbd = [&passphrase]
         (const set<EncryptionKeyId>&)
         ->SecureBinaryData
      {
         return passphrase;
      };
      assetWlt->setPassphrasePromptLambda(passphraseLbd);

      //create accounts
      auto ecdhAccType1 =
         make_shared<AccountType_ECDH>(privKey1, pubKey1);
      ecdhAccType1->setDefaultAddressType(
         AddressEntryType_P2WPKH);
      ecdhAccType1->addAddressType(AddressEntryType_P2WPKH);
      ecdhAccType1->setMain(true);

      auto ecdhAccType2 =
         make_shared<AccountType_ECDH>(privKey2, pubKey2);
      ecdhAccType2->setDefaultAddressType(
         AddressEntryType_P2WPKH);
      ecdhAccType2->addAddressType(AddressEntryType_P2WPKH);

      //add accounts
      auto accPtr1 = assetWlt->createAccount(ecdhAccType1);
      auto assAccPtr1 = accPtr1->getOuterAccount();
      auto accEcdh1 = dynamic_cast<AssetAccount_ECDH*>(assAccPtr1.get());
      if (accEcdh1 == nullptr)
         throw runtime_error("unexpected account type 1");

      auto accPtr2 = assetWlt->createAccount(ecdhAccType2);
      auto assAccPtr2 = accPtr2->getOuterAccount();
      auto accEcdh2 = dynamic_cast<AssetAccount_ECDH*>(assAccPtr2.get());
      if (accEcdh2 == nullptr)
         throw runtime_error("unexpected account type 2");
      accID2 = accPtr2->getID();

      {
         //add salts
         auto tx = assetWlt->beginSubDBTransaction(assetWlt->getID(), true);
         for (unsigned i = 0; i < 5; i++)
         {
            auto&& salt = CryptoPRNG::generateRandom(32);
            auto index = accEcdh1->addSalt(tx, salt);
            saltMap1.insert(make_pair(index, salt));

            salt = CryptoPRNG::generateRandom(32);
            index = accEcdh2->addSalt(tx, salt);
            saltMap2.insert(make_pair(index, salt));
         }
      }

      //grab addresses
      for (unsigned i = 0; i < 5; i++)
      {
         addrMap1.insert(make_pair(i,
            assetWlt->getNewAddress(accPtr1->getID())->getHash()));
         addrMap2.insert(make_pair(i,
            assetWlt->getNewAddress(accPtr2->getID())->getHash()));
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
      auto wlt = AssetWallet::loadMainWalletFromFile(
         filename, controlLbd_);
      auto assetWlt = dynamic_pointer_cast<AssetWallet_Single>(wlt);
      if (assetWlt == nullptr)
         throw runtime_error("unexpected wallet type");

      //check existing address set
      auto&& addrHashSet = assetWlt->getAddrHashSet();
      EXPECT_EQ(addrHashSet.size(), 10ULL);

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
      auto assAccPtr = accPtr->getOuterAccount();
      auto accEcdh = dynamic_cast<AssetAccount_ECDH*>(assAccPtr.get());
      if (accEcdh == nullptr)
         throw runtime_error("unexpected account type 3");

      {
         auto tx = assetWlt->beginSubDBTransaction(assetWlt->getID(), true);
         auto&& salt = CryptoPRNG::generateRandom(32);
         auto index = accEcdh->addSalt(tx, salt);
         saltMap1.insert(make_pair(index, salt));
      }

      {
         //grab another address & check it
         auto addr = assetWlt->getNewAddress()->getHash();
         auto saltedKey = CryptoECDSA::PubKeyScalarMultiply(pubKey1, saltMap1[5]);
         auto hash = BtcUtils::getHash160(saltedKey);

         EXPECT_EQ(addr, hash);
      }

      {
         //grab an existing address from its settlement id
         auto tx = assetWlt->beginSubDBTransaction(assetWlt->getID(), true);
         auto id = accEcdh->addSalt(tx, saltMap1[3]);
         EXPECT_EQ(id, 3);

         auto assetPtr = accEcdh->getAssetForKey(id);
         auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(assetPtr);
         auto hash = BtcUtils::getHash160(
            assetSingle->getPubKey()->getCompressedKey());

         EXPECT_EQ(addrMap1[3], hash);
      }

      auto accPtr2 = assetWlt->getAccountForID(accID2);

      {
         //same with account 2
         auto assAcc2 = accPtr2->getOuterAccount();
         auto accEcdhPtr = dynamic_cast<AssetAccount_ECDH*>(assAcc2.get());
         ASSERT_NE(accEcdhPtr, nullptr);

         auto tx = assetWlt->beginSubDBTransaction(assetWlt->getID(), true);
         auto id = accEcdhPtr->addSalt(tx, saltMap2[2]);
         EXPECT_EQ(id, 2);

         auto assetPtr = accEcdhPtr->getAssetForKey(id);
         auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(assetPtr);
         auto hash = BtcUtils::getHash160(
            assetSingle->getPubKey()->getCompressedKey());

         EXPECT_EQ(addrMap2[2], hash);
      }
   }

   woFilename = AssetWallet::forkWatchingOnly(
      filename, controlLbd_);

   //same with WO
   {
      //reload wallet
      auto wlt = AssetWallet::loadMainWalletFromFile(
         woFilename, controlLbd_);
      auto assetWlt = dynamic_pointer_cast<AssetWallet_Single>(wlt);
      if (assetWlt == nullptr)
         throw runtime_error("unexpected wallet type");

      ASSERT_TRUE(assetWlt->isWatchingOnly());

      //check existing address set
      auto&& addrHashSet = assetWlt->getAddrHashSet();
      EXPECT_EQ(addrHashSet.size(), 11ULL);

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
      auto assAccPtr = accPtr->getOuterAccount();
      auto accEcdh = dynamic_cast<AssetAccount_ECDH*>(assAccPtr.get());
      if (accEcdh == nullptr)
         throw runtime_error("unexpected account type 4");

      auto rootAsset = accEcdh->getRoot();
      auto rootSingle = dynamic_pointer_cast<AssetEntry_Single>(rootAsset);
      ASSERT_NE(rootSingle, nullptr);
      EXPECT_EQ(rootSingle->getPrivKey(), nullptr);

      {
         auto tx = assetWlt->beginSubDBTransaction(assetWlt->getID(), true);
         auto&& salt = CryptoPRNG::generateRandom(32);
         auto index = accEcdh->addSalt(tx, salt);
         saltMap1.insert(make_pair(index, salt));
      }

      {
         //grab another address & check it
         auto addr = assetWlt->getNewAddress()->getHash();
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
TEST_F(WalletsTest, AssetPathResolution)
{
   //seed shared across all wallet instances
   auto rawEntropy = CryptoPRNG::generateRandom(32);

   vector<uint32_t> derPath = {
      0x800012ab,
      0x8000ff13,
      0x80050000
   };

   BIP32_Node node;
   node.initFromSeed(rawEntropy);
   auto seedFingerprint = node.getThisFingerprint();

   for (auto& step : derPath)
      node.derivePrivate(step);

   auto pubNode = node.getPublicCopy();

   node.derivePublic(0);
   node.derivePublic(5);

   auto pubkey = node.getPublicKey();
   auto xpub = node.getBase58();
   string xpubStr(xpub.getCharPtr(), xpub.getSize());

   auto checkWlt = [&pubkey, &xpubStr, &derPath](shared_ptr<AssetWallet> wltPtr)->bool
   {
      auto fullPath = derPath;
      fullPath.push_back(0);
      fullPath.push_back(5);

      auto wlt_single = dynamic_pointer_cast<AssetWallet_Single>(wltPtr);
      auto resolver = make_shared<Armory::Signing::ResolverFeed_AssetWalletSingle>(wlt_single);
      auto assetPath = resolver->resolveBip32PathForPubkey(pubkey);
      auto pathFromSeed = assetPath.getDerivationPathFromSeed();

      if (fullPath.size() != pathFromSeed.size()) {
         return false;
      }

      for (unsigned i=0; i<pathFromSeed.size(); i++) {
         if (pathFromSeed[i] != fullPath[i])
            return false;
      }

      auto pubkeyHash = BtcUtils::getHash160(pubkey);
      auto assetPair = resolver->getAssetPairForKey(pubkeyHash);
      if (assetPair.first == nullptr) {
         return false;
      }
      auto assetXPub = wlt_single->getXpubForAssetID(assetPair.first->getID());
      if (assetXPub != xpubStr) {
         return false;
      }
      return true;
   };

   {
      //empty wallet + custom account
      WalletCreationParams params{{}, {}, homedir_, 0, 1, 1};

      unique_ptr<Armory::Seeds::ClearTextSeed> seed(
         new Armory::Seeds::ClearTextSeed_BIP32(
            rawEntropy, Armory::Seeds::SeedType::BIP32_Virgin));
      auto wlt = AssetWallet_Single::createFromSeed(
         move(seed), params);

      auto account = wlt->makeNewBip32AccTypeObject(derPath);
      account->setMain(true);
      account->setNodes({0});
      account->setDefaultAddressType(
         AddressEntryType(AddressEntryType_P2WPKH));
      account->addAddressType(AddressEntryType_P2WPKH);
      account->setAddressLookup(10);

      wlt->createBIP32Account(account);
      EXPECT_TRUE(checkWlt(wlt));

      //create a WO copy
      auto filename = wlt->getDbFilename();
      auto woFilename = AssetWallet_Single::forkWatchingOnly(filename);

      //cleanup original wallet
      wlt.reset();
      std::filesystem::remove(filename);

      //check WO wallet
      auto wltWO = AssetWallet_Single::loadMainWalletFromFile(
         woFilename,
         [](const set<EncryptionKeyId>&)
            ->SecureBinaryData
            {return SecureBinaryData();});
      EXPECT_TRUE(checkWlt(wltWO));

      //cleanup WO
      wltWO.reset();
      std::filesystem::remove(woFilename);
   }

   {
      //empty WO wallet
      auto wltWO = AssetWallet_Single::createBlank(homedir_, "walletWO1", {});

      //add account
      auto mainAccType = AccountType_BIP32::makeFromDerPaths(
         seedFingerprint, {derPath});
      mainAccType->setMain(true);
      mainAccType->setAddressLookup(10);
      mainAccType->setNodes({0});
      mainAccType->setDefaultAddressType(
         AddressEntryType(AddressEntryType_P2WPKH));
      mainAccType->addAddressType(AddressEntryType_P2WPKH);

      auto b58sbd = pubNode.getBase58();
      string xpub(b58sbd.toCharPtr(), b58sbd.getSize());
      mainAccType->setRoots({{derPath, xpub}});

      wltWO->createBIP32Account(mainAccType);
      EXPECT_TRUE(checkWlt(wltWO));
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, isAssetIdInUse)
{
   //create wallet
   auto passphrase = SecureBinaryData::fromString("password");
   WalletCreationParams params{
      passphrase, controlPass_, homedir_, 10, 1, 1};

   //create regular wallet
   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto wlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   //grab a bunch of addresses of various types
   map<AssetId, BinaryData> addrHashesInUse;

   //5 default addresses
   for (unsigned i = 0; i < 5; i++)
   {
      auto addrPtr = wlt->getNewAddress();
      addrHashesInUse.emplace(addrPtr->getID(), addrPtr->getPrefixedHash());
   }

   //5 p2wpkh
   for (unsigned i = 0; i < 5; i++)
   {
      auto addrPtr = wlt->getNewAddress(AddressEntryType_P2WPKH);
      addrHashesInUse.emplace(addrPtr->getID(), addrPtr->getPrefixedHash());
   }

   //grab all address hashes for wallet
   auto addrHashes = wlt->getAddrHashSet();

   ASSERT_EQ(addrHashesInUse.size(), 10ULL);
   ASSERT_EQ(addrHashes.size(), 80ULL);

   set<AssetId> detectedIds;
   for (const auto& addrIt : addrHashes)
   {
      const auto& idAndType = wlt->getAssetIDForScrAddr(addrIt);
      const auto& id = idAndType.first;

      //is this one of our grabbed addresses?
      auto inUseIt = addrHashesInUse.find(id);
      if (inUseIt == addrHashesInUse.end())
      {
         //it isn't, should be seen as unused
         EXPECT_FALSE(wlt->isAssetUsed(id));
         continue;
      }

      EXPECT_TRUE(wlt->isAssetUsed(id));
      detectedIds.emplace(id);
   }

   //make sure we've seen every address
   for (const auto& id : detectedIds)
      addrHashesInUse.erase(id);
   ASSERT_TRUE(addrHashesInUse.empty());
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class WalletMetaDataTest : public ::testing::Test
{
protected:
   std::filesystem::path homedir_;

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      homedir_ = std::filesystem::path("./fakehomedir");
      FileUtils::removeDirectory(homedir_);
      std::filesystem::create_directory(homedir_);

      Armory::Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Armory::Config::ProcessType::DB);
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      Armory::Config::reset();

      FileUtils::removeDirectory(homedir_);
   }
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletMetaDataTest, AuthPeers)
{
   auto peerPassLbd = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString("authpeerpass");
   };
   auto authPeers = make_unique<AuthorizedPeers>(
      homedir_, "test.peers", peerPassLbd);

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
   authPeers = make_unique<AuthorizedPeers>(homedir_, "test.peers", peerPassLbd);

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
   btc_pubkey btckey4_cmp = CryptoECDSA::CompressPoint(btckey4);

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
   authPeers = make_unique<AuthorizedPeers>(homedir_, "test.peers", peerPassLbd);

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
   btc_pubkey btckey4_cmp = CryptoECDSA::CompressPoint(btckey4);

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
TEST_F(WalletMetaDataTest, Comments)
{
   auto passphrase = SecureBinaryData::fromString("password");
   auto controlPass = SecureBinaryData::fromString("control");

   auto controlLbd = [controlPass]
      (const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return controlPass;
   };

   //comments
   map<BinaryData, string> commentMap;
   commentMap.insert(make_pair(READHEX("aabbccdd"), "comment1"));
   commentMap.insert(make_pair(READHEX("eeff0011"), "comment2"));
   commentMap.insert(make_pair(READHEX("22334455"), "comment3"));

   //create regular wallet
   std::filesystem::path filename;
   {
      WalletCreationParams params{
         passphrase, controlPass,
         homedir_, 10, 1, 1};

      unique_ptr<Armory::Seeds::ClearTextSeed> seed(
         new Armory::Seeds::ClearTextSeed_BIP32(
            Armory::Seeds::SeedType::BIP32_Structured));
      auto wlt = AssetWallet_Single::createFromSeed(
         move(seed), params);
      filename = wlt->getDbFilename();

      //set comments
      for (auto& commentPair : commentMap)
         wlt->setComment(commentPair.first, commentPair.second);

      //check comments
      for (auto& commentPair : commentMap)
         EXPECT_EQ(wlt->getComment(commentPair.first), commentPair.second);
   }

   {
      //shut down wallet and reload it
      auto wlt = AssetWallet::loadMainWalletFromFile(filename, controlLbd);
      auto wltSingle = dynamic_pointer_cast<AssetWallet_Single>(wlt);
      ASSERT_NE(wltSingle, nullptr);

      //check loaded comments
      auto grabMap = wltSingle->getCommentMap();
      EXPECT_EQ(grabMap, commentMap);

      //update a comment
      commentMap[READHEX("22334455")] = "comment4";
      wlt->setComment(READHEX("22334455"), "comment4");

      //delete a comment
      commentMap.erase(READHEX("eeff0011"));
      wlt->deleteComment(READHEX("eeff0011"));

      //add a comment
      commentMap.insert(make_pair(READHEX("66778899aa"), "comment5"));
      wlt->setComment(READHEX("66778899aa"), "comment5");

      //check
      grabMap = wltSingle->getCommentMap();
      EXPECT_EQ(grabMap, commentMap);
   }

   {
      //create WO copy
      auto woCopyPath = AssetWallet::forkWatchingOnly(
         filename, controlLbd);
      auto woWlt = AssetWallet::loadMainWalletFromFile(
         woCopyPath, controlLbd);
      auto woSingle = dynamic_pointer_cast<AssetWallet_Single>(woWlt);
      
      //check loaded comments
      auto grabMap = woSingle->getCommentMap();
      EXPECT_EQ(grabMap, commentMap);
   }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BackupTests : public ::testing::Test
{
public:
   std::filesystem::path homedir_;

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      homedir_ = std::filesystem::path("./fakehomedir");
      FileUtils::removeDirectory(homedir_);
      std::filesystem::create_directory(homedir_);

      Armory::Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir",
         "--testnet" },
         Armory::Config::ProcessType::DB);
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      Armory::Config::reset();

      FileUtils::removeDirectory(homedir_);
   }

   /////////////////////////////////////////////////////////////////////////////
   bool compareWalletWithBackup(
      std::shared_ptr<AssetWallet_Single> assetWlt,
      const std::filesystem::path& path,
      const std::string& pass, const std::string& control)
   {
      unsigned controlPassCount = 0;
      auto controlPassLbd = [&controlPassCount, &control](
         const set<EncryptionKeyId>&)->SecureBinaryData
      {
         ++controlPassCount;
         return SecureBinaryData::fromString(control);
      };

      //load it, newCtrl should work for the control passphrase
      auto loadedWlt = AssetWallet::loadMainWalletFromFile(path, controlPassLbd);
      METHOD_ASSERT_NE(loadedWlt, nullptr);
      METHOD_ASSERT_EQ(controlPassCount, 1U);

      //check wallet id
      EXPECT_EQ(assetWlt->getID(), loadedWlt->getID());

      //compare account types between original and restored
      auto accountIDs = assetWlt->getAccountIDs();
      auto loadedIDs = loadedWlt->getAccountIDs();
      EXPECT_EQ(accountIDs, loadedIDs);

      //
      auto oldPassLbd = [](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData::fromString("passphrase");
      };

      unsigned keyPassCount = 0;
      auto newPassLbd = [&pass, &keyPassCount](
         const set<EncryptionKeyId>&)->SecureBinaryData
      {
         ++keyPassCount;
         return SecureBinaryData::fromString(pass);
      };

      assetWlt->setPassphrasePromptLambda(oldPassLbd);
      loadedWlt->setPassphrasePromptLambda(newPassLbd);

      //compare some priv keys to test passphrase
      for (unsigned i=0; i<10; i++)
      {
         auto address = assetWlt->getNewAddress();
         auto assetID = assetWlt->getAssetIDForScrAddr(
            address->getPrefixedHash());
         auto asset = assetWlt->getAssetForID(assetID.first);
         auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(asset);

         auto lock = assetWlt->lockDecryptedContainer();
         auto privKey = assetWlt->getDecryptedPrivateKeyForAsset(assetSingle);

         //
         auto newAddr = loadedWlt->getNewAddress();
         auto newID = loadedWlt->getAssetIDForScrAddr(
            newAddr->getPrefixedHash());
         auto newAsset = loadedWlt->getAssetForID(newID.first);
         auto newAssetSingle = dynamic_pointer_cast<AssetEntry_Single>(newAsset);

         auto newLock = loadedWlt->lockDecryptedContainer();
         auto singleWlt = dynamic_pointer_cast<AssetWallet_Single>(loadedWlt);
         auto newKey = singleWlt->getDecryptedPrivateKeyForAsset(newAssetSingle);

         //
         EXPECT_EQ(address->getPrefixedHash(), newAddr->getPrefixedHash());
         EXPECT_EQ(privKey, newKey);
         EXPECT_EQ(assetID.first, newID.first);
      }

      METHOD_ASSERT_EQ(keyPassCount, 10U);
      return true;
   }
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, Easy16)
{
   for (const auto& index : Armory::Seeds::Easy16Codec::eligibleIndexes_) {
      auto root = CryptoPRNG::generateRandom(32);

      //encode the root
      auto encoded = Armory::Seeds::Easy16Codec::encode(root.getRef(), index);
      ASSERT_EQ(encoded.size(), 2ULL);


      auto decoded = Armory::Seeds::Easy16Codec::decode(encoded);
      ASSERT_EQ(decoded.checksumIndexes_.size(), 2ULL);
      EXPECT_EQ(decoded.checksumIndexes_[0], (uint8_t)index);
      EXPECT_EQ(decoded.checksumIndexes_[1], (uint8_t)index);

      EXPECT_EQ(decoded.data_, root);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, Easy16_Repair)
{
   /*NOTE: this test will lead to a lot of hashing*/
   auto corruptLine = [](vector<SecureBinaryData>& lines,
      uint8_t lineSelect, uint8_t wordSelect, uint8_t charSelect, uint8_t newVal)
   {
      auto& line = lines[lineSelect];
      
      auto wordPos = wordSelect * 5;
      if (wordSelect >= 4)
         ++wordPos;

      auto charPos = wordPos + charSelect;
      auto& val = line[charPos];
      char newChar;
      while (true)
      {
         newChar = Armory::Seeds::Easy16Codec::e16chars_[newVal % 16];
         if (newChar != val)
            break;

         ++newVal;
      }
      
      val = newChar;
   };

   PRNG_Fortuna prng;

   //1 error, auto repair
   unsigned succesfulRepairs = 0;
   for (unsigned i=0; i<64; i++)
   {
      auto root = prng.generateRandom(32);

      //encode the root
      auto encoded = Armory::Seeds::Easy16Codec::encode(root.getRef(),
         Armory::Seeds::BackupType::Armory135);
      ASSERT_EQ(encoded.size(), 2ULL);

      //corrupt one character in one line
      auto randomSelection = prng.generateRandom(4);

      auto lineSelect = (uint8_t)(randomSelection.getPtr()[0]) % encoded.size();
      auto wordSelect = (uint8_t)(randomSelection.getPtr()[1]) % 8;
      auto charSelect = (uint8_t)(randomSelection.getPtr()[2]) % 4;
      auto newVal = (uint8_t)(randomSelection.getPtr()[3]) % 15 + 1;

      auto corrupted = encoded;
      corruptLine(corrupted, lineSelect, wordSelect, charSelect, newVal);
      ASSERT_NE(encoded[lineSelect], corrupted[lineSelect]);

      //decode the corrupted data, should yield an incorrect value
      auto decoded = Armory::Seeds::Easy16Codec::decode(corrupted);
      ASSERT_EQ(decoded.checksumIndexes_.size(), 2ULL);
      if (lineSelect == 0)
      {
         EXPECT_NE(decoded.checksumIndexes_[0], 0);
         EXPECT_EQ(decoded.checksumIndexes_[1], 0);
      }
      else
      {
         EXPECT_EQ(decoded.checksumIndexes_[0], 0);
         EXPECT_NE(decoded.checksumIndexes_[1], 0);
      }

      EXPECT_NE(root, decoded.data_);

      //attempt to repair, may fail because of collisions (no unique solution)
      try
      {
         auto result = Armory::Seeds::Easy16Codec::repair(decoded);
         if (result)
         {
            ASSERT_EQ(decoded.repairedIndexes_.size(), 2ULL);
            EXPECT_EQ(decoded.repairedIndexes_[0], 0);
            EXPECT_EQ(decoded.repairedIndexes_[1], 0);
            EXPECT_EQ(root, decoded.data_);
            
            ++succesfulRepairs;
         }
      }
      catch (const Armory::Seeds::Easy16RepairError&)
      {}
   }

   EXPECT_GE(succesfulRepairs, 20U);

   //2 errors, fail
   for (unsigned i=0; i<64; i++)
   {
      auto root = prng.generateRandom(32);
      
      //encode the root
      auto encoded = Armory::Seeds::Easy16Codec::encode(root.getRef(),
         Armory::Seeds::BackupType::Armory135);
      ASSERT_EQ(encoded.size(), 2ULL);

      //corrupt 2 characters in one line
      auto randomSelection = prng.generateRandom(8);

      auto lineSelect = (uint8_t)(randomSelection.getPtr()[0]) % encoded.size();
      auto wordSelect1 = (uint8_t)(randomSelection.getPtr()[1]) % 8;
      auto charSelect1 = (uint8_t)(randomSelection.getPtr()[2]) % 4;
      auto newVal1 = (uint8_t)(randomSelection.getPtr()[3]) % 15 + 1;

      auto wordSelect2 = (uint8_t)(randomSelection.getPtr()[4]) % 8;
      if (wordSelect2 == wordSelect1)
         wordSelect2 = (wordSelect2 + 1) % 8;
      auto charSelect2 = (uint8_t)(randomSelection.getPtr()[5]) % 4;
      auto newVal2 = (uint8_t)(randomSelection.getPtr()[6]) % 15 + 1;

      auto corrupted = encoded;
      corruptLine(corrupted, lineSelect, wordSelect1, charSelect1, newVal1);
      corruptLine(corrupted, lineSelect, wordSelect2, charSelect2, newVal2);
      ASSERT_NE(encoded[lineSelect], corrupted[lineSelect]);

      //decode, should yield an incorrect value
      auto decoded = Armory::Seeds::Easy16Codec::decode(corrupted);
      ASSERT_EQ(decoded.checksumIndexes_.size(), 2ULL);
      if (lineSelect == 0)
      {
         EXPECT_EQ(decoded.checksumIndexes_[0], EASY16_INVALID_CHECKSUM_INDEX);
         EXPECT_EQ(decoded.checksumIndexes_[1], 0);
      }
      else
      {
         EXPECT_EQ(decoded.checksumIndexes_[0], 0);
         EXPECT_EQ(decoded.checksumIndexes_[1], EASY16_INVALID_CHECKSUM_INDEX);
      }

      EXPECT_NE(root, decoded.data_);

      //attempt to repair, should fail
      auto result = Armory::Seeds::Easy16Codec::repair(decoded);
      if (result)
      {
         EXPECT_NE(decoded.data_, root);
      }
   }

   //1 error per line, broad spectrum fix attempt
   succesfulRepairs = 0;
   for (unsigned i=0; i<64; i++)
   {
      auto root = prng.generateRandom(32);
      
      //encode the root
      auto encoded = Armory::Seeds::Easy16Codec::encode(root.getRef(),
         Armory::Seeds::BackupType::Armory135);
      ASSERT_EQ(encoded.size(), 2ULL);

      //corrupt 1 character per line
      auto randomSelection = prng.generateRandom(8);

      auto wordSelect1 = (uint8_t)(randomSelection.getPtr()[1]) % 8;
      auto charSelect1 = (uint8_t)(randomSelection.getPtr()[2]) % 4;
      auto newVal1 = (uint8_t)(randomSelection.getPtr()[3]) % 15 + 1;

      auto wordSelect2 = (uint8_t)(randomSelection.getPtr()[4]) % 8;
      auto charSelect2 = (uint8_t)(randomSelection.getPtr()[5]) % 4;
      auto newVal2 = (uint8_t)(randomSelection.getPtr()[6]) % 15 + 1;

      auto corrupted = encoded;
      corruptLine(corrupted, 0, wordSelect1, charSelect1, newVal1);
      corruptLine(corrupted, 1, wordSelect2, charSelect2, newVal2);

      //decode, should yield an incorrect value
      auto decoded = Armory::Seeds::Easy16Codec::decode(corrupted);
      ASSERT_EQ(decoded.checksumIndexes_.size(), 2ULL);
      EXPECT_NE(decoded.checksumIndexes_[0], 0);
      EXPECT_NE(decoded.checksumIndexes_[1], 0);

      //attempt to repair, may fail because of collisions (no evident solution)
      try
      {
         auto result = Armory::Seeds::Easy16Codec::repair(decoded);
         if (result)
         {
            ASSERT_EQ(decoded.repairedIndexes_.size(), 2ULL);
            if (decoded.repairedIndexes_[0] != decoded.repairedIndexes_[1] ||
               decoded.repairedIndexes_[0] != 0)
            {
               continue;
            }

            EXPECT_EQ(root, decoded.data_);
            ++succesfulRepairs;
         }
      }
      catch (const Armory::Seeds::Easy16RepairError&)
      {}
   }

   EXPECT_GE(succesfulRepairs, 5U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, SecurePrint)
{
   auto root = CryptoPRNG::generateRandom(32);
   
   //encrypt the root
   Armory::Seeds::SecurePrint spEncr;
   auto encryptedData = spEncr.encrypt(root, {});
   ASSERT_FALSE(spEncr.getPassphrase().empty());
   ASSERT_EQ(encryptedData.first.getSize(), 32ULL);
   ASSERT_EQ(encryptedData.second.getSize(), 0ULL);
   EXPECT_NE(encryptedData.first, root);

   Armory::Seeds::SecurePrint spDecr;
   auto decryptedData = 
      spDecr.decrypt(encryptedData.first, spEncr.getPassphrase());

   ASSERT_EQ(decryptedData.getSize(), 32ULL);
   EXPECT_EQ(decryptedData, root);

   //with chaincode
   auto chaincode = CryptoPRNG::generateRandom(32);

   Armory::Seeds::SecurePrint spWithCC;
   auto dataWithCC = spWithCC.encrypt(root, chaincode);

   ASSERT_FALSE(spWithCC.getPassphrase().empty());
   ASSERT_EQ(dataWithCC.first.getSize(), 32ULL);
   ASSERT_EQ(dataWithCC.second.getSize(), 32ULL);
   EXPECT_NE(dataWithCC.first, root);

   EXPECT_NE(spEncr.getPassphrase(), spWithCC.getPassphrase());
   EXPECT_NE(encryptedData.first, dataWithCC.first);

   Armory::Seeds::SecurePrint spDecrWithCC;
   auto decrRoot = spDecrWithCC.decrypt(
      dataWithCC.first, spWithCC.getPassphrase());
   
   ASSERT_EQ(decrRoot.getSize(), 32ULL);
   EXPECT_EQ(decrRoot, root);

   auto decrCC = spDecrWithCC.decrypt(
      dataWithCC.second, spWithCC.getPassphrase());

   ASSERT_EQ(decrCC.getSize(), 32ULL);
   EXPECT_EQ(decrCC, chaincode);

   //mangled passphrase
   try 
   {
      auto mangledPass = spWithCC.getPassphrase();
      ASSERT_GE(mangledPass.getSize(), 4ULL);
      mangledPass.getPtr()[3] ^= 0xFF;

      Armory::Seeds::SecurePrint spDecrWithCC;
      auto decrypted = spDecrWithCC.decrypt(
         dataWithCC.first, mangledPass);

      ASSERT_FALSE(true);
   }
   catch (const runtime_error& e)
   {
      EXPECT_EQ(e.what(), string("invalid SecurePrint passphrase"));
   }

   //jibberish passphrase
   try 
   {
      auto passphrase = CryptoPRNG::generateRandom(7);
      auto passhash = BtcUtils::getHash256(passphrase);
      passphrase.append(passhash.getPtr()[0] ^ 0xFF);

      auto passB58 = BinaryData::fromString(
         BtcUtils::base58_encode(passphrase));
      
      Armory::Seeds::SecurePrint spDecrWithCC;
      auto decrypted = spDecrWithCC.decrypt(
         dataWithCC.first, passB58);

      ASSERT_FALSE(true);
   }
   catch (const runtime_error& e)
   {
      EXPECT_EQ(e.what(), string("invalid SecurePrint passphrase"));
   }

   //mismatched pass
   {
      Armory::Seeds::SecurePrint spDecrWithCC;
      auto decrypted = spDecrWithCC.decrypt(
         dataWithCC.first, spEncr.getPassphrase());
      EXPECT_NE(decrypted, dataWithCC.first);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_Legacy)
{
   //create a legacy wallet
   WalletCreationParams params{
      SecureBinaryData::fromString("passphrase"),
      SecureBinaryData::fromString("control"),
      homedir_, 4, 1, 1};

   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135(
         Armory::Seeds::ClearTextSeed_Armory135::LegacyType::Armory135));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   auto passLbd = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString("passphrase");
   };
   assetWlt->setPassphrasePromptLambda(passLbd);
   auto backupData = Helpers::getWalletBackup(assetWlt);
   auto backupEasy16 = dynamic_cast<Backup_Easy16*>(backupData.get());

   auto newPass = CryptoPRNG::generateRandom(10).toHexStr();
   auto newCtrl = CryptoPRNG::generateRandom(10).toHexStr();
   auto callback = [&backupData, &newPass, &newCtrl](
      const RestorePrompt& prompt)->PromptReply
   {
      capnp::MallocMessageBuilder reply;
      switch (prompt.promptType)
      {
         case RestorePromptType::Passphrases:
            return PromptReply{true,
               SecureBinaryData::fromString(newPass),
               SecureBinaryData::fromString(newCtrl)
            };

         case RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.walletId, backupData->getWalletId());
            EXPECT_EQ(prompt.backupType, BackupType::Armory135);
            return PromptReply{true};
         }

         default:
            return PromptReply{false};
      }
   };

   std::filesystem::path newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   std::filesystem::path filename;
   {
      //restore wallet
      auto backupCopy = Backup_Easy16::fromLines({
         backupEasy16->getRoot(Backup_Easy16::LineIndex::One, false),
         backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, false),
      });
      auto newWltPtr = Armory::Seeds::Helpers::restoreFromBackup(
         move(backupCopy), callback, WalletCreationParams{
            SecureBinaryData::fromString(newPass),
            SecureBinaryData::fromString(newCtrl),
            newHomeDir, 10, 1, 1 });
      EXPECT_NE(newWltPtr, nullptr);

      auto passLbd2 = [&newPass](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData::fromString(newPass);
      };
      newWltPtr->setPassphrasePromptLambda(passLbd2);

      auto newWalletSingle = dynamic_pointer_cast<AssetWallet_Single>(newWltPtr);
      auto backupData2 = Armory::Seeds::Helpers::getWalletBackup(newWalletSingle);
      auto backupEasy16_2 = dynamic_cast<Backup_Easy16*>(backupData2.get());

      EXPECT_EQ(backupEasy16->getRoot(Backup_Easy16::LineIndex::One, false),
         backupEasy16_2->getRoot(Backup_Easy16::LineIndex::One, false));
      EXPECT_EQ(backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, false),
         backupEasy16_2->getRoot(Backup_Easy16::LineIndex::Two, false));

      EXPECT_EQ(backupEasy16->getWalletId(), backupEasy16_2->getWalletId());

      filename = newWltPtr->getDbFilename();
   }

   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, newPass, newCtrl));
   FileUtils::removeDirectory(newHomeDir);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_Legacy_Armory200a)
{
   //create a legacy wallet
   WalletCreationParams params{
      SecureBinaryData::fromString("passphrase"),
      SecureBinaryData::fromString("control"),
      homedir_, 4, 1, 1};

   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135());
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   auto passLbd = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString("passphrase");
   };
   assetWlt->setPassphrasePromptLambda(passLbd);
   auto backupData = Armory::Seeds::Helpers::getWalletBackup(assetWlt);
   auto backupEasy16 = dynamic_cast<Backup_Easy16*>(backupData.get());

   auto newPass = CryptoPRNG::generateRandom(10).toHexStr();
   auto newCtrl = CryptoPRNG::generateRandom(10).toHexStr();
   auto callback = [&backupData, &newPass, &newCtrl](
      const RestorePrompt& prompt)->PromptReply
   {
      switch (prompt.promptType)
      {
         case RestorePromptType::Passphrases:
            return PromptReply{true,
               SecureBinaryData::fromString(newPass),
               SecureBinaryData::fromString(newCtrl)
            };

         case RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.walletId, backupData->getWalletId());
            EXPECT_EQ(prompt.backupType, BackupType::Armory200a);
            return PromptReply{true};
         }

         default:
            return PromptReply{false};
      }
   };

   std::filesystem::path newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   std::filesystem::path filename;
   {
      //restore wallet
      auto backupCopy = Backup_Easy16::fromLines({
         backupEasy16->getRoot(Backup_Easy16::LineIndex::One, false),
         backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, false),
      });
      auto newWltPtr = Armory::Seeds::Helpers::restoreFromBackup(
         move(backupCopy), callback, WalletCreationParams{
            SecureBinaryData::fromString(newPass),
            SecureBinaryData::fromString(newCtrl),
            newHomeDir, 10, 1, 1});
      EXPECT_NE(newWltPtr, nullptr);

      auto passLbd2 = [&newPass](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData::fromString(newPass);
      };
      newWltPtr->setPassphrasePromptLambda(passLbd2);

      auto newWalletSingle = dynamic_pointer_cast<AssetWallet_Single>(newWltPtr);
      auto backupData2 = Armory::Seeds::Helpers::getWalletBackup(newWalletSingle);
      auto backupEasy16_2 = dynamic_cast<Backup_Easy16*>(backupData2.get());

      EXPECT_EQ(backupEasy16->getRoot(Backup_Easy16::LineIndex::One, false),
         backupEasy16_2->getRoot(Backup_Easy16::LineIndex::One, false));
      EXPECT_EQ(backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, false),
         backupEasy16_2->getRoot(Backup_Easy16::LineIndex::Two, false));

      EXPECT_EQ(backupEasy16->getWalletId(), backupEasy16_2->getWalletId());

      filename = newWltPtr->getDbFilename();
   }

   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, newPass, newCtrl));
   FileUtils::removeDirectory(newHomeDir);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_Legacy_SecurePrint)
{
   //create a legacy wallet
   WalletCreationParams params{
      SecureBinaryData::fromString("passphrase"),
      SecureBinaryData::fromString("control"),
      homedir_, 4, 1, 1};

   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135(
         Armory::Seeds::ClearTextSeed_Armory135::LegacyType::Armory135));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   auto passLbd = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString("passphrase");
   };
   assetWlt->setPassphrasePromptLambda(passLbd);

   auto backupData = Armory::Seeds::Helpers::getWalletBackup(assetWlt);
   auto backupEasy16 = dynamic_cast<Backup_Easy16*>(backupData.get());

   auto newPass = CryptoPRNG::generateRandom(10).toHexStr();
   auto newCtrl = CryptoPRNG::generateRandom(10).toHexStr();
   auto callback = [&backupData, &newPass, &newCtrl](
      const RestorePrompt& prompt)->PromptReply
   {
      switch (prompt.promptType)
      {
         case RestorePromptType::Passphrases:
            return PromptReply{true,
               SecureBinaryData::fromString(newPass),
               SecureBinaryData::fromString(newCtrl)
            };

         case RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.backupType, BackupType::Armory135);
            return PromptReply{prompt.walletId == backupData->getWalletId()};
         }

         default:
            return PromptReply{false};
      }
   };

   std::filesystem::path newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   std::filesystem::path filename;
   {
      //try without sp pass
      try
      {
         auto backupCopy = Backup_Easy16::fromLines({
            backupEasy16->getRoot(Backup_Easy16::LineIndex::One, true),
            backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, true),
         });
         Armory::Seeds::Helpers::restoreFromBackup(
            move(backupCopy), callback, WalletCreationParams{
            SecureBinaryData::fromString(newPass),
            SecureBinaryData::fromString(newCtrl),
            newHomeDir, 10, 1, 1});
         ASSERT_TRUE(false);
      } catch (const Armory::Seeds::RestoreUserException& e) {
         EXPECT_EQ(e.what(), string("user rejected id"));
      }

      //try with secure print now
      auto backupCopy = Backup_Easy16::fromLines({
         backupEasy16->getRoot(Backup_Easy16::LineIndex::One, true),
         backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, true)},
         backupEasy16->getSpPass());
      auto newWltPtr = Armory::Seeds::Helpers::restoreFromBackup(
         move(backupCopy), callback, WalletCreationParams{
            SecureBinaryData::fromString(newPass),
            SecureBinaryData::fromString(newCtrl),
            newHomeDir, 10, 1, 1});
      EXPECT_NE(newWltPtr, nullptr);

      auto passLbd2 = [&newPass](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData::fromString(newPass);
      };
      newWltPtr->setPassphrasePromptLambda(passLbd2);

      auto newWalletSingle = dynamic_pointer_cast<AssetWallet_Single>(newWltPtr);
      auto backupData2 = Armory::Seeds::Helpers::getWalletBackup(newWalletSingle);
      auto backupEasy16_2 = dynamic_cast<Backup_Easy16*>(backupData2.get());

      EXPECT_EQ(backupEasy16->getRoot(Backup_Easy16::LineIndex::One, true),
         backupEasy16_2->getRoot(Backup_Easy16::LineIndex::One, true));
      EXPECT_EQ(backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, true),
         backupEasy16_2->getRoot(Backup_Easy16::LineIndex::Two, true));
      EXPECT_EQ(backupEasy16->getWalletId(), backupEasy16_2->getWalletId());

      filename = newWltPtr->getDbFilename();
   }

   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, newPass, newCtrl));
   FileUtils::removeDirectory(newHomeDir);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, Easy16_AutoRepair)
{
   /*NOTE: this test will lead to a lot of hashing*/
   auto corruptLine = [](vector<SecureBinaryData>& lines,
      uint8_t lineSelect, uint8_t wordSelect, uint8_t charSelect, uint8_t newVal)
   {
      auto& line = lines[lineSelect];
      
      auto wordPos = wordSelect * 5;
      if (wordSelect >= 4)
         ++wordPos;

      auto charPos = wordPos + charSelect;
      auto& val = line[charPos];
      char newChar;
      while (true)
      {
         newChar = Armory::Seeds::Easy16Codec::e16chars_[newVal % 16];
         if (newChar != val)
            break;

         ++newVal;
      }
      
      val = newChar;
   };

   auto computeWalletID = [](const SecureBinaryData& root)->string
   {
      auto chaincode = BtcUtils::computeChainCode_Armory135(root);
      auto derScheme = make_shared<DerivationScheme_ArmoryLegacy>(chaincode);

      auto pubkey = CryptoECDSA().ComputePublicKey(root);
      auto asset_single = make_shared<AssetEntry_Single>(
         AssetId::getRootAssetId(), pubkey, nullptr);

      auto addrVec = derScheme->extendPublicChain(asset_single, 1, 1, nullptr);
      if (addrVec.size() != 1)
         throw runtime_error("unexpected chain derivation output");

      auto firstEntry = dynamic_pointer_cast<AssetEntry_Single>(addrVec[0]);
      if (firstEntry == nullptr)
         throw runtime_error("unexpected asset entry type");

      return BtcUtils::computeID(firstEntry->getPubKey()->getUncompressedKey());
   };

   PRNG_Fortuna prng;

   //1 error, auto repair
   unsigned succesfulRepairs = 0;
   for (unsigned i=0; i<64; i++)
   {
      auto root = prng.generateRandom(32);
      auto wltID = computeWalletID(root);

      //encode the root
      auto encoded = Easy16Codec::encode(root.getRef(), BackupType::Armory135);
      ASSERT_EQ(encoded.size(), 2ULL);

      //corrupt one character in one line
      auto randomSelection = prng.generateRandom(4);

      auto lineSelect = (uint8_t)(randomSelection.getPtr()[0]) % encoded.size();
      auto wordSelect = (uint8_t)(randomSelection.getPtr()[1]) % 8;
      auto charSelect = (uint8_t)(randomSelection.getPtr()[2]) % 4;
      auto newVal = (uint8_t)(randomSelection.getPtr()[3]) % 15 + 1;

      auto corrupted = encoded;
      corruptLine(corrupted, lineSelect, wordSelect, charSelect, newVal);
      ASSERT_NE(encoded[lineSelect], corrupted[lineSelect]);

      //decode the corrupted data, should yield an incorrect value
      auto decoded = Easy16Codec::decode(corrupted);
      ASSERT_EQ(decoded.checksumIndexes_.size(), 2ULL);
      if (lineSelect == 0)
      {
         EXPECT_NE(decoded.checksumIndexes_[0], 0);
         EXPECT_EQ(decoded.checksumIndexes_[1], 0);
      }
      else
      {
         EXPECT_EQ(decoded.checksumIndexes_[0], 0);
         EXPECT_NE(decoded.checksumIndexes_[1], 0);
      }

      EXPECT_NE(root, decoded.data_);

      //attempt to restore wallet from corrupted backup
      try
      {
         auto userPrompt = [&wltID, &decoded, &succesfulRepairs](
            const RestorePrompt& prompt)->PromptReply
         {
            switch (prompt.promptType)
            {
               case RestorePromptType::ChecksumError:
               {
                  EXPECT_EQ(prompt.checksumResult.at(0), decoded.checksumIndexes_[0]);
                  EXPECT_EQ(prompt.checksumResult.at(1), decoded.checksumIndexes_[1]);
                  return PromptReply{false};
               }

               case RestorePromptType::Id:
               {
                  EXPECT_EQ(prompt.backupType, BackupType::Armory135);
                  if (prompt.walletId == wltID) {
                     ++succesfulRepairs;
                  }
                  return PromptReply{false};
               }

               default:
                  return PromptReply{false};
            }
         };

         auto backup = Backup_Easy16::fromLines({
            string_view(corrupted[0].toCharPtr(), corrupted[0].getSize()),
            string_view(corrupted[1].toCharPtr(), corrupted[1].getSize())
         });
         Armory::Seeds::Helpers::restoreFromBackup(
            move(backup), userPrompt, WalletCreationParams{
               {}, {}, homedir_, 10, 1, 1});
      }
      catch (const exception&)
      {}
   }

   EXPECT_GE(succesfulRepairs, 20U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_LegacyWithChaincode)
{
   //create a legacy wallet
   WalletCreationParams params{
      SecureBinaryData::fromString("passphrase"),
      SecureBinaryData::fromString("control"),
      homedir_, 4, 1, 1};

   std::unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135(
         CryptoPRNG::generateRandom(32), CryptoPRNG::generateRandom(32)
   ));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   auto passLbd = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString("passphrase");
   };
   assetWlt->setPassphrasePromptLambda(passLbd);

   auto backupData = Armory::Seeds::Helpers::getWalletBackup(assetWlt);
   auto backupEasy16 = dynamic_cast<Backup_Easy16*>(backupData.get());

   auto corruptLine = [](std::string& line,
      uint8_t wordSelect, uint8_t charSelect, uint8_t newVal)
   {
      auto wordPos = wordSelect * 5;
      if (wordSelect >= 4) {
         ++wordPos;
      }

      auto charPos = wordPos + charSelect;
      auto& val = line[charPos];
      char newChar;
      while (true) {
         newChar = Armory::Seeds::Easy16Codec::e16chars_[newVal % 16];
         if (newChar != val) {
            break;
         }
         ++newVal;
      }
      val = newChar;
   };

   auto newPass = CryptoPRNG::generateRandom(10).toHexStr();
   auto newCtrl = CryptoPRNG::generateRandom(10).toHexStr();
   std::vector<std::set<uint8_t>> corruptions{
      { 1 }, //corrupt second root line
      { 0, 2 }, //corrupt first line of root and chaincode
      { 2, 3 }, //both chaincode lines
      { 3 }, //last chaincode line
   };
   int corruptionCounter = 0;

   auto callback = [&backupData, &newPass, &newCtrl,
      &corruptions, &corruptionCounter](
      const RestorePrompt& prompt)->PromptReply
   {
      switch (prompt.promptType)
      {
         case RestorePromptType::Passphrases:
            return PromptReply{true,
               SecureBinaryData::fromString(newPass),
               SecureBinaryData::fromString(newCtrl)
            };

         case RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.backupType, BackupType::Armory135);
            return PromptReply{prompt.walletId == backupData->getWalletId()};
         }

         case RestorePromptType::ChecksumError:
         {
            auto corruptedLines = corruptions[corruptionCounter++];
            auto iter = corruptedLines.begin();
            unsigned y=0;
            for (const auto& linePair : prompt.checksumResult) {
               if (linePair.first == *iter) {
                  EXPECT_EQ(linePair.second, 255);
                  ++iter;
               } else {
                  EXPECT_EQ(linePair.second, 0);
               }
            }
            return PromptReply{false};
         }

         default:
            return PromptReply{false};
      }
   };

   std::filesystem::path newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);
   PRNG_Fortuna prng;

   std::filesystem::path filename;
   {
      std::vector<std::string> backupLines {
         std::string{backupEasy16->getRoot(Backup_Easy16::LineIndex::One, false)},
         std::string{backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, false)},
         std::string{backupEasy16->getChaincode(Backup_Easy16::LineIndex::One, false)},
         std::string{backupEasy16->getChaincode(Backup_Easy16::LineIndex::Two, false)},
      };

      //attempts with corrupted lines
      for (const auto& corruption : corruptions) {
         //copy the backup
         auto backupCopy = backupLines;

         //corrupt each affected line in 3 places so as to fail auto repair
         for (const auto& line : corruption) {
            for (unsigned i=0; i<3; i++) {
               auto randomSelection = prng.generateRandom(4);
               auto wordSelect = (uint8_t)(randomSelection.getPtr()[1]) % 8;
               auto charSelect = (uint8_t)(randomSelection.getPtr()[2]) % 4;
               auto newVal = (uint8_t)(randomSelection.getPtr()[3]) % 15 + 1;

               corruptLine(backupCopy[line], wordSelect, charSelect, newVal);
            }
         }

         try {
            auto corruptedBackup = Backup_Easy16::fromLines({
               backupCopy[0], backupCopy[1], backupCopy[2], backupCopy[3]
            });
            Armory::Seeds::Helpers::restoreFromBackup(
               move(corruptedBackup), callback, WalletCreationParams{
                  SecureBinaryData::fromString(newPass),
                  SecureBinaryData::fromString(newCtrl),
                  newHomeDir, 10, 1, 1});
            ASSERT_TRUE(false);
         } catch (const Armory::Seeds::RestoreUserException& e) {
            EXPECT_EQ(e.what(), string("failed to create seed from backup"));
         }
      }

      //try with valid backup now
      auto validBackup = Backup_Easy16::fromLines({
         backupLines[0], backupLines[1], backupLines[2], backupLines[3]});
      auto newWltPtr = Armory::Seeds::Helpers::restoreFromBackup(
         move(validBackup), callback, WalletCreationParams{
            SecureBinaryData::fromString(newPass),
            SecureBinaryData::fromString(newCtrl),
            newHomeDir, 10, 1, 1});
      EXPECT_NE(newWltPtr, nullptr);

      auto passLbd2 = [&newPass](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData::fromString(newPass);
      };
      newWltPtr->setPassphrasePromptLambda(passLbd2);

      auto newWalletSingle = dynamic_pointer_cast<AssetWallet_Single>(newWltPtr);
      auto backupData2 = Armory::Seeds::Helpers::getWalletBackup(newWalletSingle);
      auto backupEasy16_2 = dynamic_cast<Backup_Easy16*>(backupData2.get());

      EXPECT_EQ(backupEasy16->getRoot(Backup_Easy16::LineIndex::One, false),
         backupEasy16_2->getRoot(Backup_Easy16::LineIndex::One, false));
      EXPECT_EQ(backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, false),
         backupEasy16_2->getRoot(Backup_Easy16::LineIndex::Two, false));

      EXPECT_EQ(backupEasy16->getChaincode(Backup_Easy16::LineIndex::One, false),
         backupEasy16_2->getChaincode(Backup_Easy16::LineIndex::One, false));
      EXPECT_EQ(backupEasy16->getChaincode(Backup_Easy16::LineIndex::Two, false),
         backupEasy16_2->getChaincode(Backup_Easy16::LineIndex::Two, false));

      EXPECT_EQ(backupEasy16->getWalletId(), backupEasy16_2->getWalletId());
      filename = newWltPtr->getDbFilename();
   }

   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, newPass, newCtrl));
   FileUtils::removeDirectory(newHomeDir);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_LegacyWithChaincode_SecurePrint)
{
   //create a legacy wallet
   WalletCreationParams params{
      SecureBinaryData::fromString("passphrase"),
      SecureBinaryData::fromString("control"),
      homedir_, 4, 1, 1};

   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_Armory135(
      CryptoPRNG::generateRandom(32), CryptoPRNG::generateRandom(32)));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   auto passLbd = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString("passphrase");
   };
   assetWlt->setPassphrasePromptLambda(passLbd);

   auto backupData = Armory::Seeds::Helpers::getWalletBackup(assetWlt);
   auto backupEasy16 = dynamic_cast<Backup_Easy16*>(backupData.get());

   auto newPass = CryptoPRNG::generateRandom(10).toHexStr();
   auto newCtrl = CryptoPRNG::generateRandom(10).toHexStr();
   auto callback = [&backupData, &newPass, &newCtrl](
      const RestorePrompt& prompt)->PromptReply
   {
      switch (prompt.promptType)
      {
         case RestorePromptType::Passphrases:
            return PromptReply{true,
               SecureBinaryData::fromString(newPass),
               SecureBinaryData::fromString(newCtrl)
            };

         case RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.backupType, BackupType::Armory135);
            return PromptReply{prompt.walletId == backupData->getWalletId()};
         }

         default:
            return PromptReply{false};
      }
   };

   std::filesystem::path newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   std::filesystem::path filename;
   {
      //try without sp pass
      try
      {
         auto backupCopy = Backup_Easy16::fromLines({
            backupEasy16->getRoot(Backup_Easy16::LineIndex::One, true),
            backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, true),
            backupEasy16->getChaincode(Backup_Easy16::LineIndex::One, true),
            backupEasy16->getChaincode(Backup_Easy16::LineIndex::Two, true),
         });
         Armory::Seeds::Helpers::restoreFromBackup(
            move(backupCopy), callback, WalletCreationParams{
               SecureBinaryData::fromString(newPass),
               SecureBinaryData::fromString(newCtrl),
               newHomeDir, 10, 1, 1});
         ASSERT_TRUE(false);
      }
      catch (const Armory::Seeds::RestoreUserException& e)
      {
         EXPECT_EQ(e.what(), string("user rejected id"));
      }

      //try with secure print now
      auto backupCopy = Backup_Easy16::fromLines({
         backupEasy16->getRoot(Backup_Easy16::LineIndex::One, true),
         backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, true),
         backupEasy16->getChaincode(Backup_Easy16::LineIndex::One, true),
         backupEasy16->getChaincode(Backup_Easy16::LineIndex::Two, true)},
         backupEasy16->getSpPass()
      );
      auto newWltPtr = Armory::Seeds::Helpers::restoreFromBackup(
         move(backupCopy), callback, WalletCreationParams{
            SecureBinaryData::fromString(newPass),
            SecureBinaryData::fromString(newCtrl),
            newHomeDir, 10, 1, 1});
      EXPECT_NE(newWltPtr, nullptr);

      auto passLbd2 = [&newPass](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData::fromString(newPass);
      };
      newWltPtr->setPassphrasePromptLambda(passLbd2);

      auto newWalletSingle = dynamic_pointer_cast<AssetWallet_Single>(newWltPtr);
      auto backupData2 = Armory::Seeds::Helpers::getWalletBackup(newWalletSingle);
      auto backupEasy16_2 = dynamic_cast<Backup_Easy16*>(backupData2.get());

      EXPECT_EQ(backupEasy16->getRoot(Backup_Easy16::LineIndex::One, true),
         backupEasy16_2->getRoot(Backup_Easy16::LineIndex::One, true));
      EXPECT_EQ(backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, true),
         backupEasy16_2->getRoot(Backup_Easy16::LineIndex::Two, true));

      EXPECT_EQ(backupEasy16->getChaincode(Backup_Easy16::LineIndex::One, true),
         backupEasy16_2->getChaincode(Backup_Easy16::LineIndex::One, true));
      EXPECT_EQ(backupEasy16->getChaincode(Backup_Easy16::LineIndex::Two, true),
         backupEasy16_2->getChaincode(Backup_Easy16::LineIndex::Two, true));

      EXPECT_EQ(backupEasy16->getWalletId(), backupEasy16_2->getWalletId());

      filename = newWltPtr->getDbFilename();
   }

   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, newPass, newCtrl));
   FileUtils::removeDirectory(newHomeDir);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_BIP32)
{
   //create a legacy wallet
   WalletCreationParams params{
      SecureBinaryData::fromString("passphrase"),
      SecureBinaryData::fromString("control"),
      homedir_, 4, 1, 1};
   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Structured));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   auto passLbd = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString("passphrase");
   };
   assetWlt->setPassphrasePromptLambda(passLbd);

   auto backupData = Armory::Seeds::Helpers::getWalletBackup(assetWlt);
   auto backupEasy16 = dynamic_cast<Backup_Easy16*>(backupData.get());

   auto newPass = CryptoPRNG::generateRandom(10).toHexStr();
   auto newCtrl = CryptoPRNG::generateRandom(10).toHexStr();
   auto callback = [&backupData, &newPass, &newCtrl](
      const RestorePrompt& prompt)->PromptReply
   {
      switch (prompt.promptType)
      {
         case RestorePromptType::Passphrases:
            return PromptReply{true,
               SecureBinaryData::fromString(newPass),
               SecureBinaryData::fromString(newCtrl)
            };

         case RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.walletId, backupData->getWalletId());
            EXPECT_EQ(prompt.backupType, BackupType::Armory200b);
            return PromptReply{true};
         }

         default:
            return PromptReply{false};
      }
   };

   std::filesystem::path newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   std::filesystem::path filename;
   {
      //restore wallet
      auto backupCopy = Backup_Easy16::fromLines({
         backupEasy16->getRoot(Backup_Easy16::LineIndex::One, false),
         backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, false),
      });
      auto newWltPtr = Armory::Seeds::Helpers::restoreFromBackup(
         move(backupCopy), callback, WalletCreationParams{
            SecureBinaryData::fromString(newPass),
            SecureBinaryData::fromString(newCtrl),
            newHomeDir, 10, 1, 1});
      ASSERT_NE(newWltPtr, nullptr);

      auto passLbd2 = [&newPass](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData::fromString(newPass);
      };
      newWltPtr->setPassphrasePromptLambda(passLbd2);

      auto newWalletSingle = dynamic_pointer_cast<AssetWallet_Single>(newWltPtr);
      auto backupData2 = Armory::Seeds::Helpers::getWalletBackup(newWalletSingle);
      auto backupEasy16_2 = dynamic_cast<Backup_Easy16*>(backupData2.get());

      EXPECT_EQ(backupEasy16->getRoot(Backup_Easy16::LineIndex::One, false),
         backupEasy16_2->getRoot(Backup_Easy16::LineIndex::One, false));
      EXPECT_EQ(backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, false),
         backupEasy16_2->getRoot(Backup_Easy16::LineIndex::Two, false));

      EXPECT_EQ(backupEasy16->getWalletId(), backupEasy16_2->getWalletId());

      filename = newWltPtr->getDbFilename();
   }

   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, newPass, newCtrl));
   FileUtils::removeDirectory(newHomeDir);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_BIP32_Virgin)
{
   //create a legacy wallet
   WalletCreationParams params{
      SecureBinaryData::fromString("passphrase"),
      SecureBinaryData::fromString("control"),
      homedir_, 4, 1, 1};
   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new Armory::Seeds::ClearTextSeed_BIP32(
         Armory::Seeds::SeedType::BIP32_Virgin
      ));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   auto passLbd = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString("passphrase");
   };
   assetWlt->setPassphrasePromptLambda(passLbd);

   auto backupData = Armory::Seeds::Helpers::getWalletBackup(assetWlt);
   auto backupEasy16 = dynamic_cast<Armory::Seeds::Backup_Easy16*>(
      backupData.get());

   auto newPass = CryptoPRNG::generateRandom(10).toHexStr();
   auto newCtrl = CryptoPRNG::generateRandom(10).toHexStr();
   auto callback = [&backupData, &newPass, &newCtrl](
      const RestorePrompt& prompt)->PromptReply
   {
      switch (prompt.promptType)
      {
         case RestorePromptType::Passphrases:
            return PromptReply{true,
               SecureBinaryData::fromString(newPass),
               SecureBinaryData::fromString(newCtrl)
            };

         case RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.walletId, backupData->getWalletId());
            EXPECT_EQ(prompt.backupType, BackupType::Armory200c);
            return PromptReply{true};
         }

         default:
            return PromptReply{false};
      }
   };

   string newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   //restore wallet
   auto backupCopy = Backup_Easy16::fromLines({
      backupEasy16->getRoot(Backup_Easy16::LineIndex::One, false),
      backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, false),
   });
   auto newWltPtr = Armory::Seeds::Helpers::restoreFromBackup(
      move(backupCopy), callback, WalletCreationParams{
         SecureBinaryData::fromString(newPass),
         SecureBinaryData::fromString(newCtrl),
         newHomeDir, 10, 1, 1});
   ASSERT_NE(newWltPtr, nullptr);

   //check wallet id
   EXPECT_EQ(assetWlt->getID(), newWltPtr->getID());

   //compare account types between original and restored
   auto loadedIDs = newWltPtr->getAccountIDs();
   EXPECT_EQ(loadedIDs.size(), 0ULL);

   auto passLbd2 = [&newPass](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString(newPass);
   };
   newWltPtr->setPassphrasePromptLambda(passLbd2);

   auto newWalletSingle = dynamic_pointer_cast<AssetWallet_Single>(newWltPtr);
   auto backupData2 = Armory::Seeds::Helpers::getWalletBackup(newWalletSingle);
   auto backupEasy16_2 = dynamic_cast<Armory::Seeds::Backup_Easy16*>(
      backupData2.get());

   EXPECT_EQ(backupEasy16->getRoot(Backup_Easy16::LineIndex::One, false),
      backupEasy16_2->getRoot(Backup_Easy16::LineIndex::One, false));
   EXPECT_EQ(backupEasy16->getRoot(Backup_Easy16::LineIndex::Two, false),
      backupEasy16_2->getRoot(Backup_Easy16::LineIndex::Two, false));

   EXPECT_EQ(backupEasy16->getWalletId(), backupEasy16_2->getWalletId());

   FileUtils::removeDirectory(newHomeDir);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_BIP32_FromBase58)
{
   auto b58seed = string_view{
      "tprv8ZgxMBicQKsPd9TeAdPADNnSyH9SSUUbTVeFszDE23Ki6TBB5nCefAdHkK8Fm3qMQR6sHwA56zqRmKmxnHk37JkiFzvncDqoKmPWubu7hDF"};

   auto newPass = CryptoPRNG::generateRandom(10).toHexStr();
   auto newCtrl = CryptoPRNG::generateRandom(10).toHexStr();
   auto callback = [&newPass, &newCtrl](
      const RestorePrompt& prompt)->PromptReply
   {
      switch (prompt.promptType)
      {
         case RestorePromptType::Passphrases:
            return PromptReply{true,
               SecureBinaryData::fromString(newPass),
               SecureBinaryData::fromString(newCtrl)
            };

         case RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.walletId, "poUtmfmp");
            EXPECT_EQ(prompt.backupType, BackupType::Base58);
            return PromptReply{true};
         }

         default:
            return PromptReply{false};
      }
   };

   //create bip32 wallet from xpriv, check it yields same xpriv
   std::filesystem::path filename;
   {
      auto backup = Backup_Base58::fromString(b58seed);
      auto wallet = Helpers::restoreFromBackup(
         move(backup), callback, WalletCreationParams{
            SecureBinaryData::fromString(newPass),
            SecureBinaryData::fromString(newCtrl),
            homedir_, 10, 1, 1});
      ASSERT_NE(wallet, nullptr);

      auto passLbd = [newPass](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData::fromString(newPass);
      };
      wallet->setPassphrasePromptLambda(passLbd);

      auto walletSingle = dynamic_pointer_cast<AssetWallet_Single>(wallet);
      auto backupData = Helpers::getWalletBackup(walletSingle);
      auto backupBase58 = dynamic_cast<Backup_Base58*>(backupData.get());
      EXPECT_EQ(backupBase58->getBase58String(), b58seed);
      filename = wallet->getDbFilename();
   }

   //load wallet from file and check xpriv again
   {
      auto controlPassLbd = [&newCtrl](
         const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData::fromString(newCtrl);
      };

      //load it, newCtrl should work for the control passphrase
      auto loadedWlt = AssetWallet::loadMainWalletFromFile(
         filename, controlPassLbd);
      ASSERT_NE(loadedWlt, nullptr);

      auto passLbd = [newPass](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData::fromString(newPass);
      };
      loadedWlt->setPassphrasePromptLambda(passLbd);

      auto walletSingle = dynamic_pointer_cast<AssetWallet_Single>(loadedWlt);
      auto backupData = Helpers::getWalletBackup(walletSingle);
      auto backupBase58 = dynamic_cast<Backup_Base58*>(backupData.get());
      EXPECT_EQ(backupBase58->getBase58String(), b58seed);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_BIP39)
{
   //create a legacy wallet
   std::filesystem::path filename;
   WalletCreationParams params{
      SecureBinaryData::fromString("passphrase"),
      SecureBinaryData::fromString("control"),
      homedir_, 4, 1, 1};
   unique_ptr<Armory::Seeds::ClearTextSeed> seed(
      new ClearTextSeed_BIP39(
         ClearTextSeed_BIP39::Dictionnary::English_Trezor));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      move(seed), params);

   auto passLbd = [](const set<EncryptionKeyId>&)->SecureBinaryData
   {
      return SecureBinaryData::fromString("passphrase");
   };
   auto walletId = assetWlt->getID();
   assetWlt->setPassphrasePromptLambda(passLbd);
   auto backupDataBIP39 = Armory::Seeds::Helpers::getWalletBackup(
      assetWlt, BackupType::BIP39);
   auto backupDataArmory200d = Armory::Seeds::Helpers::getWalletBackup(
      assetWlt, BackupType::Armory200d);

   EXPECT_EQ(walletId, backupDataBIP39->getWalletId());
   EXPECT_EQ(walletId, backupDataArmory200d->getWalletId());

   //restore Armory200d lambda
   auto newPass = CryptoPRNG::generateRandom(10).toHexStr();
   auto newCtrl = CryptoPRNG::generateRandom(10).toHexStr();
   auto callback = [&walletId, &newPass, &newCtrl](
      const RestorePrompt& prompt)->PromptReply
   {
      switch (prompt.promptType)
      {
         case RestorePromptType::Passphrases:
            return PromptReply{true,
               SecureBinaryData::fromString(newPass),
               SecureBinaryData::fromString(newCtrl)
            };

         case RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.walletId, walletId);
            EXPECT_EQ(prompt.backupType, BackupType::Armory200d);
            return PromptReply{true};
         }

         default:
            return PromptReply{false};
      }
   };

   string newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   {
      auto backupE16 = dynamic_cast<Backup_Easy16*>(backupDataArmory200d.get());
      ASSERT_NE(backupE16, nullptr);
      auto backupE16Copy = Backup_Easy16::fromLines({
         backupE16->getRoot(Backup_Easy16::LineIndex::One, false),
         backupE16->getRoot(Backup_Easy16::LineIndex::Two, false),
      });
      auto newWltPtr = Armory::Seeds::Helpers::restoreFromBackup(
         move(backupE16Copy), callback, WalletCreationParams{
            SecureBinaryData::fromString(newPass),
            SecureBinaryData::fromString(newCtrl),
            newHomeDir, 10, 1, 1});
      ASSERT_NE(newWltPtr, nullptr);

      auto passLbd2 = [&newPass](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData::fromString(newPass);
      };
      newWltPtr->setPassphrasePromptLambda(passLbd2);

      auto newWalletSingle = dynamic_pointer_cast<AssetWallet_Single>(newWltPtr);
      auto backupData2 = Armory::Seeds::Helpers::getWalletBackup(
         newWalletSingle, BackupType::Armory200d);
      auto backupE16_2 = dynamic_cast<Backup_Easy16*>(backupData2.get());

      EXPECT_EQ(backupE16->getRoot(Backup_Easy16::LineIndex::One, false),
         backupE16_2->getRoot(Backup_Easy16::LineIndex::One, false));
      EXPECT_EQ(backupE16->getRoot(Backup_Easy16::LineIndex::Two, false),
         backupE16_2->getRoot(Backup_Easy16::LineIndex::Two, false));
      EXPECT_EQ(backupE16->getWalletId(), backupE16_2->getWalletId());

      filename = newWltPtr->getDbFilename();
   }

   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, newPass, newCtrl));
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   //restore BIP39 lambda
   auto newPass2 = CryptoPRNG::generateRandom(10).toHexStr();
   auto newCtrl2 = CryptoPRNG::generateRandom(10).toHexStr();
   auto callbackBip39 = [&walletId, &newPass2, &newCtrl2](
      const RestorePrompt& prompt)->PromptReply
   {
      switch (prompt.promptType)
      {
         case RestorePromptType::Passphrases:
            return PromptReply{true,
               SecureBinaryData::fromString(newPass2),
               SecureBinaryData::fromString(newCtrl2)
            };

         case RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.walletId, walletId);
            EXPECT_EQ(prompt.backupType, BackupType::BIP39);
            return PromptReply{true};
         }

         default:
            return PromptReply{false};
      }
   };

   //restore from mnemonic string
   {
      //restore wallet
      auto backupBIP39 = dynamic_cast<Backup_BIP39*>(backupDataBIP39.get());
      ASSERT_NE(backupBIP39, nullptr);

      auto backupBIP39Copy = Backup_BIP39::fromMnemonicString(
         backupBIP39->getMnemonicString());
      auto newWltPtr = Armory::Seeds::Helpers::restoreFromBackup(
         move(backupBIP39Copy), callbackBip39, WalletCreationParams{
            SecureBinaryData::fromString(newPass2),
            SecureBinaryData::fromString(newCtrl2),
            newHomeDir, 10, 1, 1});
      ASSERT_NE(newWltPtr, nullptr);

      auto passLbd2 = [&newPass2](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData::fromString(newPass2);
      };
      newWltPtr->setPassphrasePromptLambda(passLbd2);

      auto newWalletSingle = dynamic_pointer_cast<AssetWallet_Single>(newWltPtr);
      auto backupData2 = Armory::Seeds::Helpers::getWalletBackup(newWalletSingle);
      auto backupBIP39_2 = dynamic_cast<Backup_BIP39*>(backupData2.get());

      EXPECT_EQ(backupBIP39->getMnemonicString(), backupBIP39_2->getMnemonicString());
      EXPECT_EQ(backupBIP39->getWalletId(), backupBIP39_2->getWalletId());

      filename = newWltPtr->getDbFilename();

      //grab 10 addresses from restored wallet to get in sync with original
      //otherwise the comparision will fail
      for (int i=0; i<10; i++)
         newWltPtr->getNewAddress();
   }

   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, newPass2, newCtrl2));
   FileUtils::removeDirectory(newHomeDir);
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

   CryptoECDSA::setupContext();

   srand(time(0));
   std::cout << "Running main() from gtest_main.cc\n";

   // Setup the log file 
   STARTLOGGING("cppTestsLog.txt", LogLvlDebug2);
   //LOGDISABLESTDOUT();

   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();

   FLUSHLOG();
   CLEANUPLOG();

   CryptoECDSA::shutdown();
   return exitCode;
}
