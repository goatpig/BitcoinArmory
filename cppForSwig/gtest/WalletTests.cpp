////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
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
#include <Utils/FileUtils.h>
#include <Utils/ArmoryConfig.h>
#include <Utils/BitcoinSettings.h>
#include <Ledgers/LedgerEntry.h>

#include <Wallets/GetPassphrase.h>
#include <Wallets/AuthorizedPeers.h>
#include <Wallets/Accounts/AccountTypes.h>
#include <Wallets/Accounts/AddressAccounts.h>
#include <Wallets/Seeds/Backups.h>
#include <Wallets/Seeds/Seeds.h>
#include <Wallets/WalletFileInterface.h>
#include <Wallets/KDF.h>
#include <Wallets/Addresses.h>

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/Bridge.capnp.h"

using namespace Armory;
using namespace Armory::Wallets;
using namespace std::chrono_literals;
using namespace std::string_view_literals;

namespace
{
   struct EncryptionKeyEx : public Encryption::EncryptionKey
   {
      const std::map<EncryptionKeyId, std::unique_ptr<Encryption::CipherData>>&
         getCipherDataMap() const
      {
         return cipherDataMap_;
      }
   };

   struct DecryptedDataContainerEx : private Encryption::DecryptedDataContainer
   {
      std::vector<SecureBinaryData> getMasterKeyIVs() const
      {
         std::vector<SecureBinaryData> result;
         for (auto& keyPair : encryptedKeys_) {
            auto encrKeyPtr = (EncryptionKeyEx*)keyPair.second.get();
            auto& cipherMap = encrKeyPtr->getCipherDataMap();

            for (auto& cipherPair : cipherMap) {
               auto cipherData = cipherPair.second.get();
               result.push_back(cipherData->cipher->getIV());
            }
         }
         return result;
      }

      std::vector<SecureBinaryData> getMasterEncryptionKeys() const
      {
         std::vector<SecureBinaryData> result;
         for (auto& keyPair : encryptedKeys_) {
            auto encrKeyPtr = (EncryptionKeyEx*)keyPair.second.get();
            auto& cipherMap = encrKeyPtr->getCipherDataMap();

            for (auto& cipherPair : cipherMap) {
               auto cipherData = cipherPair.second.get();
               result.push_back(cipherData->cipherText);
            }
         }
         return result;
      }

      const std::map<KdfId, std::shared_ptr<Encryption::KeyDerivationFunction>>&
      getKdfMap() const
      {
         return kdfMap_;
      }
   };

   struct AssetWalletEx : public AssetWallet_Single
   {
      std::shared_ptr<Encryption::DecryptedDataContainer>
      getDecryptedDataContainer() const
      {
         return decryptedData_;
      }
   };
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
class PassphraseTests : public ::testing::Test
{};

TEST_F(PassphraseTests, InvalidParams)
{
   using namespace Passphrase;

   Params invalidParams{};
   ASSERT_EQ(invalidParams.type, Params::Type::Invalid);

   SetNew invalidObj{};
   const auto& params = invalidObj.get();
   ASSERT_EQ(params.type, Params::Type::Invalid);

   try {
      auto unlockFunc = invalidObj.getUnlockFunc();
      unlockFunc({});
      ASSERT_TRUE(false);
   } catch (const std::runtime_error& e) {
      ASSERT_EQ(e.what(), std::string{"invalid SetNew"});
   }

   IO::CreateWalletParams wltParams{
      {}, {}, {}, nullptr, 0};
   ASSERT_EQ(wltParams.setPrivPassObj.get().type, Params::Type::Invalid);
   ASSERT_EQ(wltParams.setCtrlPassObj.get().type, Params::Type::Invalid);
}

////////////////////////////////////////////////////////////////////////////////
class AddressTests : public ::testing::Test
{
protected:
   virtual void SetUp(void)
   {
      homedir_ = std::filesystem::path("./fakehomedir");
      FileUtils::removeDirectory(homedir_);
      std::filesystem::create_directory(homedir_);

      Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Config::ProcessType::DB);
   }

   virtual void TearDown(void)
   {
      Config::reset();
      FileUtils::removeDirectory(homedir_);
   }

   std::filesystem::path homedir_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(AddressTests, base58_Tests)
{
   BinaryData h_160 = READHEX("00010966776006953d5567439e5e39f86a0d273bee");
   std::string addStr("16UwLL9Risc3QfPqBUvKofHmBQ7wMtjvM");

   auto encoded = BtcUtils::scrAddrToBase58(h_160);
   EXPECT_EQ(encoded, addStr);

   auto decoded = BtcUtils::base58toScrAddr(addStr);
   EXPECT_EQ(decoded, h_160);

   decoded = BtcUtils::base58toScrAddr(encoded);
   EXPECT_EQ(decoded, h_160);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(AddressTests, bech32_Tests)
{
   BinaryData pubkey =
      READHEX("0279BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798");
   std::string p2wpkhScrAddr("bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4");
   std::string p2wshAddr("bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3");
   std::string invalidSwAddr("bca0w508d6qejxtdg4y5r3zarvary0c5xw7kw508d6qejxtdg4y5r3zarvary0c5xw7kw5rljs90234567789035");

   auto pubkey_hash = BtcUtils::getHash160(pubkey);
   auto scrAddr_p2wpkh = BtcUtils::scrAddrToSegWitAddress(pubkey_hash);
   EXPECT_EQ(p2wpkhScrAddr, scrAddr_p2wpkh);

   BinaryWriter bw;
   bw.put_uint8_t(pubkey.getSize());
   bw.put_BinaryData(pubkey);
   bw.put_uint8_t(OP_CHECKSIG);

   auto script_hash = BtcUtils::getSha256(bw.getData());
   auto scrAddr_p2wsh = BtcUtils::scrAddrToSegWitAddress(script_hash);
   EXPECT_EQ(p2wshAddr, scrAddr_p2wsh);

   auto pubkey_hash2 = BtcUtils::segWitAddressToScrAddr(scrAddr_p2wpkh);
   EXPECT_EQ(pubkey_hash2.second, 0);
   EXPECT_EQ(pubkey_hash, pubkey_hash2.first);

   auto script_hash2 = BtcUtils::segWitAddressToScrAddr(scrAddr_p2wsh);
   EXPECT_EQ(script_hash2.second, 0);
   EXPECT_EQ(script_hash, script_hash2.first);

   //buffer overrun issue check
   try {
      auto script_hash3 = BtcUtils::segWitAddressToScrAddr(invalidSwAddr);
      ASSERT_TRUE(false);
   } catch (const std::runtime_error& e) {
      EXPECT_EQ(e.what(), std::string("failed to decode sw address!"));
   }
}

TEST_F(AddressTests, OddAddressTypes)
{
   //raw script
   SecureBinaryData lb1SBD{TestChain::lb1ScrAddr};
   auto asset1 = std::make_shared<Assets::AssetEntry_RawScript>(
      Wallets::AssetId{}, lb1SBD);
   auto address1 = AddressEntry::instantiate(asset1, AddressEntryType::RawScript);
   EXPECT_EQ(address1->getScript(), TestChain::lb1ScrAddr);

   SecureBinaryData lbd2SBD{TestChain::lb2ScrAddr};
   auto asset2 = std::make_shared<Assets::AssetEntry_RawScript>(
      Wallets::AssetId{},  lbd2SBD);
   auto address2 = AddressEntry::instantiate(asset2, AddressEntryType::RawScript);
   EXPECT_EQ(address2->getScript(), TestChain::lb2ScrAddr);

   //script hash
   SecureBinaryData lb1PS2H{TestChain::lb1ScrAddrP2SH.getSliceRef(1, 20)};
   auto asset1sh = std::make_shared<Assets::AssetEntry_ScriptHash>(
      Wallets::AssetId{}, lb1PS2H);
   auto address1_p2sh = AddressEntry::instantiate(asset1sh, AddressEntryType(
      AddressEntryType::P2SH | AddressEntryType::ScriptHash));
   EXPECT_EQ(address1_p2sh->getPrefixedHash(), TestChain::lb1ScrAddrP2SH);

   SecureBinaryData lb2PS2H{TestChain::lb2ScrAddrP2SH.getSliceRef(1, 20)};
   auto asset2sh = std::make_shared<Assets::AssetEntry_ScriptHash>(
      Wallets::AssetId{}, lb2PS2H);
   auto address2_p2sh = AddressEntry::instantiate(asset2sh, AddressEntryType(
      AddressEntryType::P2SH | AddressEntryType::ScriptHash));
   EXPECT_EQ(address2_p2sh->getPrefixedHash(), TestChain::lb2ScrAddrP2SH);
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

      try {
         auto aaidFromKey = AddressAccountId::deserializeKey(key, 22);
         EXPECT_EQ(aaidFromKey, accId);

         auto aaidKey = aaidFromKey.getAddressAccountKey();
         EXPECT_EQ(aaidKey, accIdValue);
      } catch (const std::exception&) {
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

      try {
         auto aaidFromKey = AddressAccountId::deserializeKey(key, 231);
         EXPECT_EQ(aaidFromKey, accId);

         auto aaidKey = aaidFromKey.getAddressAccountKey();
         EXPECT_EQ(aaidKey, accIdValue);
      } catch (const std::exception&) {
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

      try {
         auto aaidFromValue = AddressAccountId::deserializeValue(value);
         EXPECT_EQ(aaidFromValue, accId);

         auto aaidKey2 = aaidFromValue.getAddressAccountKey();
         EXPECT_EQ(aaidKey2, accIdValue);
      } catch (const std::exception&) {
         EXPECT_TRUE(false);
      }

      auto serValue = READHEX("0412345678");
      EXPECT_EQ(serValue, value);
   }

   //invalid deser
   try {
      auto serKey = READHEX("0001234567");
      AddressAccountId::deserializeKey(serKey, 1);
      EXPECT_TRUE(false);
   } catch (const IdException&) {}

   try {
      auto serKey = READHEX("00012345");
      AddressAccountId::deserializeKey(serKey, 0);
      EXPECT_TRUE(false);
   } catch (const std::runtime_error&) {}

   try {
      auto serKey = READHEX("000123456789AB");
      AddressAccountId::deserializeKey(serKey, 0);
      EXPECT_TRUE(false);
   } catch (const IdException&) {}

   try {
      auto serValue = READHEX("000123456789ABCDEF");
      AddressAccountId::deserializeValue(serValue);
      EXPECT_TRUE(false);
   } catch (const IdException&) {}

   try {
      auto serValue = READHEX("0A0123456789ABCDEF");
      AddressAccountId::deserializeValue(serValue);
      EXPECT_TRUE(false);
   } catch (const std::runtime_error&) {}

   try {
      auto serValue = READHEX("080123456789ABCD");
      AddressAccountId::deserializeValue(serValue);
      EXPECT_TRUE(false);
   } catch (const std::runtime_error&) {}

   try {
      auto serValue = READHEX("0A0123456789ABCDEFA0B1");
      AddressAccountId::deserializeValue(serValue);
      EXPECT_TRUE(false);
   } catch (const IdException&) {}
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

      try {
         auto asidFromKey = AssetAccountId::deserializeKey(key, 185);
         EXPECT_EQ(asidFromKey, assId1);
         EXPECT_EQ(asidFromKey, assId2);

         auto aaidKey = asidFromKey.getAddressAccountKey();
         EXPECT_EQ(aaidKey, aaIdValue);

         auto asidKey = asidFromKey.getAssetAccountKey();
         EXPECT_EQ(asidKey, asIdValue);

         auto accid = asidFromKey.getAddressAccountId();
         EXPECT_EQ(accid, accId);
      } catch (const std::exception&) {
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

      try {
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
      } catch (const std::exception&) {
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
   try {
      auto serKey = READHEX("000123456798AABBCC");
      AssetAccountId::deserializeKey(serKey, 230);
      EXPECT_TRUE(false);
   } catch (const IdException&) {}

   try {
      auto serKey = READHEX("00012345");
      AssetAccountId::deserializeKey(serKey, 0);
      EXPECT_TRUE(false);
   } catch (const std::runtime_error&) {}

   try {
      auto serKey = READHEX("E60123456789ABCDEF00");
      AssetAccountId::deserializeKey(serKey, 230);
      EXPECT_TRUE(false);
   } catch (const IdException&) {}

   try {
      auto serValue = READHEX("000123456789ABCDEF");
      BinaryRefReader brr(serValue);
      AssetAccountId::deserializeValue(brr);
      EXPECT_TRUE(false);
   } catch (const IdException&) {}

   try {
      auto serValue = READHEX("0A0123456789ABCDEF");
      BinaryRefReader brr(serValue);
      AssetAccountId::deserializeValue(brr);
      EXPECT_TRUE(false);
   } catch (const std::runtime_error&) {}

   try {
      auto serValue = READHEX("080123456789ABCD");
      BinaryRefReader brr(serValue);
      AssetAccountId::deserializeValue(brr);
      EXPECT_TRUE(false);
   } catch (const std::runtime_error&) {}

   try {
      auto serValue = READHEX("0A0123456789ABCDEFA0B1");
      BinaryRefReader brr(serValue);
      AssetAccountId::deserializeValue(brr);
      EXPECT_TRUE(false);
   } catch (const IdException&) {}
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

      try {
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
      } catch (const std::exception&) {
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

      try {
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
      } catch (const std::exception&) {
         EXPECT_TRUE(false);
      }

      auto serValue = READHEX("0C0123456789ABCDEFA0B1C2D3");
      EXPECT_EQ(serValue, value);
   }

   //invalid deser
   try {
      auto serKey = READHEX("000123456798AABBCC");
      AssetId::deserializeKey(serKey, 230);
      EXPECT_TRUE(false);
   } catch (const IdException&) {}

   try {
      auto serKey = READHEX("00012345");
      AssetId::deserializeKey(serKey, 0);
      EXPECT_TRUE(false);
   } catch (const std::runtime_error&) {}

   try {
      auto serKey = READHEX("E60123456789ABCDEF00");
      AssetId::deserializeKey(serKey, 230);
      EXPECT_TRUE(false);
   } catch (const std::runtime_error&) {}

   try {
      auto serValue = READHEX("000123456789ABCDEF");
      BinaryRefReader brr(serValue);
      AssetId::deserializeValue(brr);
      EXPECT_TRUE(false);
   } catch (const IdException&) {}

   try {
      auto serValue = READHEX("0A0123456789ABCDEF");
      BinaryRefReader brr(serValue);
      AssetId::deserializeValue(brr);
      EXPECT_TRUE(false);
   } catch (const std::runtime_error&) {}

   try {
      auto serValue = READHEX("080123456789ABCD");
      BinaryRefReader brr(serValue);
      AssetId::deserializeValue(brr);
      EXPECT_TRUE(false);
   } catch (const std::runtime_error&) {}

   try {
      auto serValue = READHEX("0A0123456789ABCDEFA0B1");
      BinaryRefReader brr(serValue);
      AssetId::deserializeValue(brr);
      EXPECT_TRUE(false);
   } catch (const IdException&) {}
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

      Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Config::ProcessType::DB);
   }

   virtual void TearDown(void)
   {
      Config::reset();
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
         auto ext_prv = SecureBinaryData::fromString(
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
         auto ext_pub = SecureBinaryData::fromString(
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
         auto ext_prv = SecureBinaryData::fromString(
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
         auto ext_pub = SecureBinaryData::fromString(
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

   auto privkey1 = Cryptography::ECDSA::computeChainedPrivateKey(
      privateKey, chaincode);
   auto privkey2 = Cryptography::ECDSA::computeChainedPrivateKey(
      privkey1, chaincode);
   auto privkey3 = Cryptography::ECDSA::computeChainedPrivateKey(
      privkey2, chaincode);
   auto privkey4 = Cryptography::ECDSA::computeChainedPrivateKey(
      privkey3, chaincode);

   EXPECT_EQ(privkey1.toHexStr(),
      "e2ffa33627c47f042e93425ded75942accaaca09d0a82d9bcf24af4fc6b5bb85");
   EXPECT_EQ(privkey2.toHexStr(),
      "a2002f9fdfb531e68d1fd3383ec10195b30e77c58877ce4d82795133dfd8dd9e");
   EXPECT_EQ(privkey3.toHexStr(),
      "03993b61f346be5a60a85bd465153b2c41abe92db4f6267a6577f590a85b8422");
   EXPECT_EQ(privkey4.toHexStr(),
      "dd39a855e2528898fbb0e8c99c9237c70915c80d690741c0c87f1c6e74b9a8d4");

   auto publicKey = Cryptography::ECDSA::computePublicKey(privateKey);

   auto pubkey1 = Cryptography::ECDSA::computeChainedPublicKey(
      publicKey, chaincode);
   auto pubkey2 = Cryptography::ECDSA::computeChainedPublicKey(
      pubkey1, chaincode);
   auto pubkey3 = Cryptography::ECDSA::computeChainedPublicKey(
      pubkey2, chaincode);
   auto pubkey4 = Cryptography::ECDSA::computeChainedPublicKey(
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
   std::vector<uint32_t> path1 = {
      0x80000020,
      0x80005081,
      0x80001111,
   };
   Accounts::DerivationTree tree(1234);

   //get root branch
   auto& branch = tree.getBranch(0);
   for (const auto& node : path1) {
      branch.appendNode(node);
   }

   //resolve paths 1: main branch
   auto resolvedPaths1 = tree.getPaths();
   ASSERT_EQ(resolvedPaths1.size(), 1ULL);
   auto resolvedPaths1_32 = Accounts::DerivationTree::toPath32(resolvedPaths1[0]);
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
   auto resolvedPaths2_32 = Accounts::DerivationTree::toPath32(resolvedPaths2[0]);
   EXPECT_EQ(resolvedPaths2_32, path1);

   //add 2 nodes to the fork
   path2.push_back(0x00000084);
   fork1.appendNode(path2.back());
   path2.push_back(0x00065c11);
   fork1.appendNode(path2.back());

   //resolve paths 3: main branch with a fork
   const auto& resolvedPaths3 = tree.getPaths();
   ASSERT_EQ(resolvedPaths3.size(), 2ULL);
   auto resolvedPaths3_32_1 = Accounts::DerivationTree::toPath32(resolvedPaths3[0]);
   EXPECT_EQ(resolvedPaths3_32_1, path1);
   auto resolvedPaths3_32_2 = Accounts::DerivationTree::toPath32(resolvedPaths3[1]);
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
   auto resolvedPaths4_32_1 = Accounts::DerivationTree::toPath32(resolvedPaths4[0]);
   EXPECT_EQ(resolvedPaths4_32_1, path2);
   auto resolvedPaths4_32_2 = Accounts::DerivationTree::toPath32(resolvedPaths4[1]);
   EXPECT_EQ(resolvedPaths4_32_2, path3);
   auto resolvedPaths4_32_3 = Accounts::DerivationTree::toPath32(resolvedPaths4[2]);
   EXPECT_EQ(resolvedPaths4_32_3, path4);

   //check branch id and depth
   auto checkBranchAndDepth = [](const Accounts::DerivationBranch::Path& path,
      const std::vector<std::pair<uint16_t, uint16_t>>& pathBD)->bool
   {
      if (path.size() != pathBD.size()) {
         return false;
      }

      auto pathIt = path.begin();
      for (unsigned i=0; i<path.size(); i++) {
         if (pathIt->branchId != pathBD[i].first) {
            return false;
         }
         if (pathIt->depth != pathBD[i].second) {
            return false;
         }
         ++pathIt;
      }
      return true;
   };

   std::vector<std::pair<uint16_t, uint16_t>> path2_bd {
      { 0, 0 },
      { 0, 1 },
      { 0, 2 },
      { 1, 3 },
      { 1, 4 }
   };
   EXPECT_TRUE(checkBranchAndDepth(resolvedPaths4[0], path2_bd));

   std::vector<std::pair<uint16_t, uint16_t>> path3_bd {
      { 0, 0 },
      { 0, 1 },
      { 0, 2 },
      { 0, 3 },
      { 2, 4 }
   };
   EXPECT_TRUE(checkBranchAndDepth(resolvedPaths4[1], path3_bd));

   std::vector<std::pair<uint16_t, uint16_t>> path4_bd {
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
   std::vector<uint32_t> path {
      0x80001564,
      0x80001111,
      0x8AEE0003,
      0x81116000,
      5
   };

   std::vector<uint32_t> fork1 {
      0x85550314,
      0x00000000,
      0x00000054
   };

   std::vector<uint32_t> fork2 {
      0,
      1,
      1,
   };

   std::vector<uint32_t> fork3 {
      0x80001000,
      0x8ab01000,
      5,
      4
   };

   auto seed = Cryptography::PRNG::generateRandomStrong(32);
   BIP32_Node rootNode;
   rootNode.initFromSeed(seed);

   Accounts::DerivationTree tree(rootNode.getThisFingerprint());
   Accounts::DerivationBranch *f1, *f2, *f3;

   auto& origin = tree.getBranch(0);
   for (unsigned i=0; i<path.size(); i++) {
      const auto& node = path[i];
      origin.appendNode(node);

      if (i == 2) {
         f1 = &tree.forkFromBranch(0);
      }
      if (i==3) {
         f2 = &tree.forkFromBranch(0);
         f3 = &tree.forkFromBranch(0);
      }
   }

   for (const auto& node : fork1) {
      f1->appendNode(node);
   }
   for (const auto& node : fork2) {
      f2->appendNode(node);
   }
   for (const auto& node : fork3) {
      f3->appendNode(node);
   }
   tree.addB58Root(tree.getSeedNode(), rootNode.getBase58());
   auto roots = tree.resolveNodeRoots(nullptr, nullptr);
   ASSERT_EQ(roots.size(), 4ULL);

   auto checkRoot = [&rootNode](
      const std::vector<uint32_t>& path, const Accounts::NodeRoot& rootData)->bool
   {
      auto rootNodeCopy = rootNode;
      for (const auto& node : path) {
         rootNodeCopy.derivePrivate(node);
      }
      auto b58 = rootNodeCopy.getBase58();

      std::string b58str(b58.getCharPtr(), b58.getSize());
      std::string rootStr(rootData.b58Root.getCharPtr(), rootData.b58Root.getSize());
      EXPECT_EQ(b58str, rootStr);
      return b58 == rootData.b58Root;
   };

   /*derive roots locally and compare*/

   //fork1
   std::vector<uint32_t> pathFork1;
   pathFork1.insert(pathFork1.end(), path.begin(), path.begin() + 3);
   pathFork1.insert(pathFork1.end(), fork1.begin(), fork1.end());

   //fork 2
   std::vector<uint32_t> pathFork2;
   pathFork2.insert(pathFork2.end(), path.begin(), path.begin() + 4);
   pathFork2.insert(pathFork2.end(), fork2.begin(), fork2.end());

   //fork 3
   std::vector<uint32_t> pathFork3;
   pathFork3.insert(pathFork3.end(), path.begin(), path.begin() + 4);
   pathFork3.insert(pathFork3.end(), fork3.begin(), fork3.end());

   std::vector<std::vector<uint32_t>> paths {
      path, pathFork1, pathFork2, pathFork3
   };

   //compare
   for (const auto& nodeRoot : roots) {
      auto p32 = Accounts::DerivationTree::toPath32(nodeRoot.path);
      auto pathIt = paths.begin();
      while (pathIt != paths.end()) {
         if (p32 == *pathIt) {
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
   std::vector<uint32_t> path {
      0x80001564,
      0x80001111,
      0x8AEE0003,
      0x81116000,
      5
   };

   std::vector<uint32_t> fork1 {
      0x85550314,
      0x00000000,
      0x00000054
   };

   std::vector<uint32_t> fork2 {
      0,
      1,
      1,
   };

   std::vector<uint32_t> fork3 {
      0x80001000,
      0x8ab01000,
      5,
      4
   };

   auto seed = Cryptography::PRNG::generateRandomStrong(32);
   BIP32_Node rootNode;
   rootNode.initFromSeed(seed);

   Accounts::DerivationTree tree(rootNode.getThisFingerprint());
   Accounts::DerivationBranch *f1, *f2, *f3;

   auto& origin = tree.getBranch(0);
   for (unsigned i=0; i<path.size(); i++) {
      const auto& node = path[i];
      origin.appendNode(node);

      if (i == 2) {
         f1 = &tree.forkFromBranch(0);
      }
      if (i==3) {
         f2 = &tree.forkFromBranch(0);
         f3 = &tree.forkFromBranch(0);
      }
   }

   for (const auto& node : fork1) {
      f1->appendNode(node);
   }
   for (const auto& node : fork2) {
      f2->appendNode(node);
   }
   for (const auto& node : fork3) {
      f3->appendNode(node);
   }
   auto checkRoot = [&rootNode](
      const std::vector<uint32_t>& path,
      const Accounts::NodeRoot& rootData)->bool
   {
      auto rootNodeCopy = rootNode;
      for (const auto& node : path) {
         rootNodeCopy.derivePrivate(node);
      }
      auto b58 = rootNodeCopy.getBase58();

      std::string b58str(b58.getCharPtr(), b58.getSize());
      std::string rootStr(rootData.b58Root.getCharPtr(), rootData.b58Root.getSize());
      EXPECT_EQ(b58str, rootStr);

      return b58 == rootData.b58Root;
   };

   /*derive roots locally and compare*/

   //fork1
   std::vector<uint32_t> pathFork1;
   pathFork1.insert(pathFork1.end(), path.begin(), path.begin() + 3);
   pathFork1.insert(pathFork1.end(), fork1.begin(), fork1.end());

   //fork 2
   std::vector<uint32_t> pathFork2;
   pathFork2.insert(pathFork2.end(), path.begin(), path.begin() + 4);
   pathFork2.insert(pathFork2.end(), fork2.begin(), fork2.end());

   //fork 3
   std::vector<uint32_t> pathFork3;
   pathFork3.insert(pathFork3.end(), path.begin(), path.begin() + 4);
   pathFork3.insert(pathFork3.end(), fork3.begin(), fork3.end());

   std::vector<std::vector<uint32_t>> paths {
      path, pathFork1, pathFork2, pathFork3
   };

   auto rootNode3 = rootNode;
   for (unsigned i=0; i<3; i++) {
      rootNode3.derivePrivate(path[i]);
   }
   tree.addB58Root(origin.getNodeByRelativeDepth(2), rootNode3.getBase58());

   auto rootNode5 = rootNode;
   for (unsigned i=0; i<5; i++) {
      rootNode5.derivePrivate(pathFork2[i]);
   }
   tree.addB58Root(f2->getNodeByRelativeDepth(0), rootNode5.getBase58());

   auto roots = tree.resolveNodeRoots(nullptr, nullptr);
   ASSERT_EQ(roots.size(), 4ULL);

   //compare
   for (const auto& nodeRoot : roots) {
      auto p32 = Accounts::DerivationTree::toPath32(nodeRoot.path);
      auto pathIt = paths.begin();
      while (pathIt != paths.end()) {
         if (p32 == *pathIt) {
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
   std::vector<uint32_t> path {
      0x80001564,
      0x80001111,
      0x8AEE0003,
      0x81116000,
      5
   };

   std::vector<uint32_t> fork1 {
      0x85550314,
      0x00000000,
      0x00000054
   };

   std::vector<uint32_t> fork2 {
      0,
      1,
      1,
   };

   std::vector<uint32_t> fork3 {
      0x80001000,
      0x8ab01000,
      5,
      4
   };

   auto seed = Cryptography::PRNG::generateRandomStrong(32);
   BIP32_Node rootNode;
   rootNode.initFromSeed(seed);

   Accounts::DerivationTree tree(rootNode.getThisFingerprint());
   Accounts::DerivationBranch *f1, *f2, *f3;

   auto& origin = tree.getBranch(0);
   for (unsigned i=0; i<path.size(); i++) {
      const auto& node = path[i];
      origin.appendNode(node);

      if (i == 2) {
         f1 = &tree.forkFromBranch(0);
      }
      if (i==3) {
         f2 = &tree.forkFromBranch(0);
         f3 = &tree.forkFromBranch(0);
      }
   }

   for (const auto& node : fork1) {
      f1->appendNode(node);
   }
   for (const auto& node : fork2) {
      f2->appendNode(node);
   }
   for (const auto& node : fork3) {
      f3->appendNode(node);
   }
   auto checkRoot = [&rootNode](
      const std::vector<uint32_t>& path,
      const Accounts::NodeRoot& rootData)->bool
   {
      auto rootNodeCopy = rootNode;
      for (const auto& node : path) {
         rootNodeCopy.derivePrivate(node);
      }
      auto rootNodePub = rootNodeCopy.getPublicCopy();
      auto b58 = rootNodePub.getBase58();

      std::string b58str(b58.getCharPtr(), b58.getSize());
      std::string rootStr(rootData.b58Root.getCharPtr(), rootData.b58Root.getSize());
      EXPECT_EQ(b58str, rootStr);

      return b58 == rootData.b58Root;
   };

   /*derive roots locally and compare*/

   //fork1
   std::vector<uint32_t> pathFork1;
   pathFork1.insert(pathFork1.end(), path.begin(), path.begin() + 3);
   pathFork1.insert(pathFork1.end(), fork1.begin(), fork1.end());

   //fork 2
   std::vector<uint32_t> pathFork2;
   pathFork2.insert(pathFork2.end(), path.begin(), path.begin() + 4);
   pathFork2.insert(pathFork2.end(), fork2.begin(), fork2.end());

   //fork 3
   std::vector<uint32_t> pathFork3;
   pathFork3.insert(pathFork3.end(), path.begin(), path.begin() + 4);
   pathFork3.insert(pathFork3.end(), fork3.begin(), fork3.end());

   std::vector<std::vector<uint32_t>> paths {
      path, pathFork1, pathFork2, pathFork3
   };

   //this one should work for main path and fork2: grab the
   //root last hard derivation in main path (all derivations
   //in main and f2 are soft past this point)
   //will fail for f3&4
   auto rootNodePath = rootNode;
   for (unsigned i=0; i<4; i++) {
      rootNodePath.derivePrivate(path[i]);
   }
   auto rootNodePath_public = rootNodePath.getPublicCopy();
   ASSERT_TRUE(rootNodePath_public.isPublic());
   tree.addB58Root(origin.getNodeByRelativeDepth(3),
      rootNodePath_public.getBase58());

   //this one should work: grab root for first soft derivation
   auto rootNodeF1 = rootNode;
   for (unsigned i=0; i<5; i++) {
      rootNodeF1.derivePrivate(pathFork1[i]);
   }
   auto rootNodeF1_public = rootNodeF1.getPublicCopy();
   ASSERT_TRUE(rootNodeF1_public.isPublic());
   tree.addB58Root(f1->getNodeByRelativeDepth(1),
      rootNodeF1_public.getBase58());

   //this one should fail: grab root for next to last hard derivation
   auto rootNodeF3 = rootNode;
   for (unsigned i=0; i<5; i++) {
      rootNodeF3.derivePrivate(pathFork3[i]);
   }
   auto rootNodeF3_public = rootNodeF3.getPublicCopy();
   ASSERT_TRUE(rootNodeF3_public.isPublic());
   tree.addB58Root(f3->getNodeByRelativeDepth(0),
      rootNodeF3_public.getBase58());

   //resolve the roots
   auto roots = tree.resolveNodeRoots(nullptr, nullptr);
   ASSERT_EQ(roots.size(), 4ULL);

   //compare
   auto pathsCopy = paths;
   for (const auto& nodeRoot : roots) {
      auto p32 = Accounts::DerivationTree::toPath32(nodeRoot.path);
      auto pathIt = pathsCopy.begin();
      while (pathIt != pathsCopy.end()) {
         if (p32 == *pathIt) {
            if (pathIt == prev(pathsCopy.end())) {
               EXPECT_FALSE(nodeRoot.isInitialized());
            } else {
               EXPECT_TRUE(checkRoot(*pathIt, nodeRoot));
            }
            pathsCopy.erase(pathIt);
            break;
         }
         ++pathIt;
      }
   }
   ASSERT_TRUE(pathsCopy.empty());

   //add tail public root for f3 and compare again, all should match
   rootNodeF3 = rootNode;
   for (auto& node : pathFork3) {
      rootNodeF3.derivePrivate(node);
   }
   rootNodeF3_public = rootNodeF3.getPublicCopy();
   ASSERT_TRUE(rootNodeF3_public.isPublic());
   tree.addB58Root(f3->getNodeByRelativeDepth(3),
      rootNodeF3_public.getBase58());

   roots = tree.resolveNodeRoots(nullptr, nullptr);
   ASSERT_EQ(roots.size(), 4ULL);

   //compare
   for (const auto& nodeRoot : roots) {
      auto p32 = Accounts::DerivationTree::toPath32(nodeRoot.path);
      auto pathIt = paths.begin();
      while (pathIt != paths.end()) {
         if (p32 == *pathIt) {
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
   std::vector<uint32_t> path {
      0x80001564,
      0x80001111,
      0x8AEE0003,
      0x81116000,
      5
   };

   std::vector<uint32_t> fork1 {
      0x85550314,
      0x00000000,
      0x00000054
   };

   std::vector<uint32_t> fork2 {
      0,
      1,
      1,
   };

   std::vector<uint32_t> fork3 {
      0x80001000,
      0x8ab01000,
      5,
      4
   };

   auto seed = Cryptography::PRNG::generateRandomStrong(32);
   BIP32_Node rootNode;
   rootNode.initFromSeed(seed);

   std::shared_ptr<Assets::AssetEntry_BIP32Root> rootPtr;
   std::shared_ptr<Encryption::DecryptedDataContainer> decrData;

   {
      //generate bip32 encrypted root
      auto whs = std::make_shared<IO::WalletHeader_Single>(
         Config::BitcoinSettings::getMagicBytes());
      whs->walletID_ = "abc"sv;
      Passphrase::Params params{1ms, 0, {}};
      auto mks = IO::WalletDBInterface::initWalletHeaderObject(whs, params);

      auto rootCipher = mks.cipher_->getCopy(
         whs->masterEncryptionKeyId_);
      auto encryptedRoot = rootCipher->encrypt(
         mks.decryptedMasterKey_.get(),
         rootCipher->getKdfId(),
         rootNode.getPrivateKey());

      auto cipherData = std::make_unique<Encryption::CipherData>(
         encryptedRoot, move(rootCipher));
      auto rootAsset = std::make_shared<Assets::Asset_PrivateKey>(
         AssetId::getRootAssetId(), move(cipherData));

      auto pubkey = rootNode.getPublicKey();
      auto chaincode = rootNode.getChaincode();
      rootPtr = std::make_unique<Assets::AssetEntry_BIP32Root>(
         AssetId::getRootAssetId(),
         pubkey, rootAsset,
         chaincode, 0, 0, 0, rootNode.getThisFingerprint(),
         std::vector<uint32_t>{}
      );
      decrData = std::make_shared<Encryption::DecryptedDataContainer>(
         nullptr, "",
         whs->defaultEncryptionKey_, whs->defaultEncryptionKeyId_,
         whs->defaultKdfId_, whs->masterEncryptionKeyId_);
      decrData->addKdf(mks.kdf_);
      decrData->addEncryptionKey(mks.masterKey_);
   }

   Accounts::DerivationTree tree(rootNode.getThisFingerprint());
   Accounts::DerivationBranch *f1, *f2, *f3;

   auto& origin = tree.getBranch(0);
   for (unsigned i=0; i<path.size(); i++) {
      const auto& node = path[i];
      origin.appendNode(node);

      if (i == 2) {
         f1 = &tree.forkFromBranch(0);
      }
      if (i==3) {
         f2 = &tree.forkFromBranch(0);
         f3 = &tree.forkFromBranch(0);
      }
   }

   for (const auto& node : fork1) {
      f1->appendNode(node);
   }
   for (const auto& node : fork2) {
      f2->appendNode(node);
   }
   for (const auto& node : fork3) {
      f3->appendNode(node);
   }

   std::vector<Accounts::NodeRoot> roots;
   {
      auto lock = decrData->lockContainer({});
      roots = std::move(tree.resolveNodeRoots(decrData, rootPtr));
      ASSERT_EQ(roots.size(), 4ULL);
   }

   auto checkRoot = [&rootNode](
      const std::vector<uint32_t>& path,
      const Accounts::NodeRoot& rootData)->bool
   {
      auto rootNodeCopy = rootNode;
      for (const auto& node : path) {
         rootNodeCopy.derivePrivate(node);
      }
      auto b58 = rootNodeCopy.getBase58();

      std::string b58str(b58.getCharPtr(), b58.getSize());
      std::string rootStr(rootData.b58Root.getCharPtr(), rootData.b58Root.getSize());
      EXPECT_EQ(b58str, rootStr);
      return b58 == rootData.b58Root;
   };

   /*derive roots locally and compare*/

   //fork1
   std::vector<uint32_t> pathFork1;
   pathFork1.insert(pathFork1.end(), path.begin(), path.begin() + 3);
   pathFork1.insert(pathFork1.end(), fork1.begin(), fork1.end());

   //fork 2
   std::vector<uint32_t> pathFork2;
   pathFork2.insert(pathFork2.end(), path.begin(), path.begin() + 4);
   pathFork2.insert(pathFork2.end(), fork2.begin(), fork2.end());

   //fork 3
   std::vector<uint32_t> pathFork3;
   pathFork3.insert(pathFork3.end(), path.begin(), path.begin() + 4);
   pathFork3.insert(pathFork3.end(), fork3.begin(), fork3.end());

   std::vector<std::vector<uint32_t>> paths {
      path, pathFork1, pathFork2, pathFork3
   };

   //compare
   for (const auto& nodeRoot : roots) {
      auto p32 = Accounts::DerivationTree::toPath32(nodeRoot.path);
      auto pathIt = paths.begin();
      while (pathIt != paths.end()) {
         if (p32 == *pathIt) {
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
   std::vector<uint32_t> path = {
      0x80001564,
      0x80001111,
      0x8AEE0003,
      0x81116000,
      5
   };

   std::vector<uint32_t> fork1 {
      0x85550314,
      0x00000000,
      0x00000054
   };

   std::vector<uint32_t> fork2 {
      0,
      1,
      1,
   };

   std::vector<uint32_t> fork3 {
      0x80001000,
      0x8ab01000,
      5,
      4
   };

   std::vector<uint32_t> p0 = path;
   std::vector<uint32_t> p1 {
      path[0], path[1], path[2],
      fork1[0], fork1[1], fork1[2]
   };

   std::vector<uint32_t> p2 {
      path[0], path[1], path[2], path[3],
      fork2[0], fork2[1], fork2[2]
   };

   std::vector<uint32_t> p3 {
      path[0], path[1], path[2], path[3],
      fork3[0], fork3[1], fork3[2], fork3[3]
   };

   std::vector<std::vector<uint32_t>> pathVec { p0, p1, p2, p3 };
   auto derTree = Accounts::DerivationTree::fromDerivationPaths(1234, pathVec);
   auto treePaths = derTree.getPaths();

   for (auto& pathIt : treePaths) {
      bool collision = false;
      auto path32 = Accounts::DerivationTree::toPath32(pathIt);

      auto pathVecIt = pathVec.begin();
      while (pathVecIt != pathVec.end()) {
         if (path32 == *pathVecIt) {
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

      Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Config::ProcessType::DB);
   }

   virtual void TearDown(void)
   {
      Config::reset();
      FileUtils::removeDirectory(homedir_);
   }

   std::filesystem::path homedir_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(AddressEntryTest, P2PKH)
{
   auto privKey = Cryptography::PRNG::generateRandomStrong(32);
   auto pubKey = Cryptography::ECDSA::computePublicKey(privKey, false);

   auto pubKeyCopy = pubKey; //assetentry ctor moves in crypto assets
   auto assetPtr = std::make_shared<Assets::AssetEntry_Single>(
      AssetId{0, 0, 0}, pubKeyCopy, nullptr);

   //uncompressed
   AddressEntry_P2PKH address(assetPtr, false);
   auto addrStr = address.getAddress();

   auto scrAddrUnc = BtcUtils::getHash160(pubKey);
   BinaryWriter bw;
   bw.put_uint8_t(Config::BitcoinSettings::getPubkeyHashPrefix());
   bw.put_BinaryData(scrAddrUnc);
   auto addrB58 = BtcUtils::scrAddrToBase58(bw.getData());

   EXPECT_EQ(addrB58, addrStr);

   //compressed
   AddressEntry_P2PKH addressCmp(assetPtr, true);
   auto addrStrCmp = addressCmp.getAddress();
   auto pubKeyCmp = Cryptography::ECDSA::compressPoint(pubKey);

   auto scrAddrCmp = BtcUtils::getHash160(pubKeyCmp);
   BinaryWriter bwCmp;
   bwCmp.put_uint8_t(Config::BitcoinSettings::getPubkeyHashPrefix());
   bwCmp.put_BinaryData(scrAddrCmp);
   auto addrB58Cmp = BtcUtils::scrAddrToBase58(bwCmp.getData());

   EXPECT_EQ(addrB58Cmp, addrStrCmp);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(AddressEntryTest, P2WPKH)
{
   auto privKey = Cryptography::PRNG::generateRandomStrong(32);
   auto pubKey = Cryptography::ECDSA::computePublicKey(privKey, true);

   auto pubKeyCopy = pubKey; //assetentry ctor moves in crypto assets
   auto assetPtr = std::make_shared<Assets::AssetEntry_Single>(
      AssetId{0, 0, 0}, pubKeyCopy, nullptr);

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
   auto privKey = Cryptography::PRNG::generateRandomStrong(32);
   auto pubKey = Cryptography::ECDSA::computePublicKey(privKey, true);

   auto pubKeyCopy = pubKey; //assetentry ctor moves in crypto assets
   auto assetPtr = std::make_shared<Assets::AssetEntry_Single>(
      AssetId{0, 0, 0}, pubKeyCopy, nullptr);

   {
      //p2sh-p2pk
      auto address = std::make_shared<AddressEntry_P2PK>(assetPtr, true);
      AddressEntry_P2SH nested(address);
      auto addrStr = nested.getAddress();

      BinaryWriter bwScript;
      bwScript.put_uint8_t(33);
      bwScript.put_BinaryData(pubKey);
      bwScript.put_uint8_t(OP_CHECKSIG);

      auto scriptHash = BtcUtils::getHash160(bwScript.getData());
      BinaryWriter bw;
      bw.put_uint8_t(Config::BitcoinSettings::getScriptHashPrefix());
      bw.put_BinaryData(scriptHash);
      auto addrB58 = BtcUtils::scrAddrToBase58(bw.getData());

      EXPECT_EQ(addrB58, addrStr);
   }

   //p2sh-p2wpkh
   {
      auto address = std::make_shared<AddressEntry_P2WPKH>(assetPtr);
      AddressEntry_P2SH nested(address);
      auto addrStr = nested.getAddress();

      auto pubkeyHash = BtcUtils::getHash160(pubKey);

      BinaryWriter bwScript;
      bwScript.put_uint8_t(0);
      bwScript.put_uint8_t(20);
      bwScript.put_BinaryData(pubkeyHash);
   
      auto scriptHash = BtcUtils::getHash160(bwScript.getData());

      BinaryWriter bw;
      bw.put_uint8_t(Config::BitcoinSettings::getScriptHashPrefix());
      bw.put_BinaryData(scriptHash);
      auto addrB58 = BtcUtils::scrAddrToBase58(bw.getData());

      EXPECT_EQ(addrB58, addrStr);
   }

   //p2sh-p2wsh-ms
   {
      std::map<BinaryData, SecureBinaryData> pubKeys;
      for (unsigned i = 0; i < 3; i++) {
         auto privKey = Cryptography::PRNG::generateRandomStrong(32);
         auto pubKey = Cryptography::ECDSA::computePublicKey(privKey, true);

         std::stringstream ss;
         ss << "wallet" << i;

         auto dataPair = std::make_pair(BinaryData::fromString(ss.str()), std::move(pubKey));
         pubKeys.emplace(dataPair);
      }

      std::map<BinaryData, std::shared_ptr<Assets::AssetEntry>> assetMap;
      int i = 0;
      for (auto pubKey : pubKeys) {
         auto asset = std::make_shared<Assets::AssetEntry_Single>(
            AssetId{0, 0, i++}, pubKey.second, nullptr);
         assetMap.emplace(pubKey.first, asset);
      }

      auto assetMs = std::make_shared<Assets::AssetEntry_Multisig>(
         AssetId{0, 0, 0}, assetMap, 2, 3);
      auto addressMs = std::make_shared<AddressEntry_Multisig>(assetMs, true);
      auto nested = std::make_shared<AddressEntry_P2SH>(addressMs);
      auto addr = nested->getAddress();

      BinaryWriter bw;
      bw.put_uint8_t(OP_2);
      for (auto& pubKey : pubKeys) {
         bw.put_uint8_t(33);
         bw.put_BinaryData(pubKey.second);
      }

      bw.put_uint8_t(OP_3);
      bw.put_uint8_t(OP_CHECKMULTISIG);

      auto scriptHash = BtcUtils::getHash160(bw.getData());
      BinaryWriter bwScrAddr;
      bwScrAddr.put_uint8_t(Config::BitcoinSettings::getScriptHashPrefix());
      bwScrAddr.put_BinaryData(scriptHash);
      auto addrB58 = BtcUtils::scrAddrToBase58(bwScrAddr.getData());

      EXPECT_EQ(addrB58, addr);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(AddressEntryTest, P2WSH)
{
   std::map<BinaryData, SecureBinaryData> pubKeys;
   for (unsigned i = 0; i < 3; i++) {
      auto privKey = Cryptography::PRNG::generateRandomStrong(32);
      auto pubKey = Cryptography::ECDSA::computePublicKey(privKey, true);

      std::stringstream ss;
      ss << "wallet" << i;

      auto dataPair = std::make_pair(BinaryData::fromString(ss.str()), std::move(pubKey));
      pubKeys.emplace(dataPair);
   }

   std::map<BinaryData, std::shared_ptr<Assets::AssetEntry>> assetMap;
   int i = 0;
   for (auto pubKey : pubKeys) {
      auto asset = std::make_shared<Assets::AssetEntry_Single>(
         AssetId{0, 0, i++}, pubKey.second, nullptr);
      assetMap.emplace(pubKey.first, asset);
   }

   auto assetMs = std::make_shared<Assets::AssetEntry_Multisig>(
      AssetId{0, 0, 0}, assetMap, 2, 3);
   auto addressMs = std::make_shared<AddressEntry_Multisig>(assetMs, true);
   auto nested = std::make_shared<AddressEntry_P2WSH>(addressMs);
   auto addr = nested->getAddress();

   BinaryWriter bw;
   bw.put_uint8_t(OP_2);
   for (auto& pubKey : pubKeys) {
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
      Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Config::ProcessType::DB);

      allZeroes16_ = READHEX("00000000000000000000000000000000");
      if(allZeroes16_.getSize() != 16) {
         throw std::runtime_error("failed to setup proper zeroed benchmark value");
      }
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      Config::reset();
      FileUtils::removeDirectory(homedir_);
   }

   /////////////////////////////////////////////////////////////////////////////
   std::map<BinaryData, BinaryData> getAllEntries(
      std::shared_ptr<LMDB::Env> dbEnv, LMDB::DB& db)
   {
      std::map<BinaryData, BinaryData> keyValMap;

      auto tx = LMDB::Transaction(dbEnv.get(), db.dbi(), LMDB::Mode::ReadOnly);
      auto iter = tx.getIterator();
      iter.toFirst();
      while (iter.isValid()) {
         auto keyData = iter.key();
         auto valData = iter.value();

         BinaryData keyBd((uint8_t*)keyData.mv_data, keyData.mv_size);
         BinaryData valBd((uint8_t*)valData.mv_data, valData.mv_size);

         keyValMap.emplace(keyBd, valBd);
         iter.advance();
      }
      return keyValMap;
   }

   /////////////////////////////////////////////////////////////////////////////
   struct BadKeyException
   {};

   ////
   std::set<unsigned> tallyGaps(const std::map<BinaryData, BinaryData>& keyValMap)
   {
      std::set<unsigned> gaps;
      int prevKeyInt = -1;

      for (auto& keyVal : keyValMap) {
         if(keyVal.first.getSize() != 4) {
            throw BadKeyException();
         }
         int keyInt = READ_UINT32_BE(keyVal.first);
         if(keyInt - prevKeyInt != 1) {
            for(int i=prevKeyInt + 1; i<keyInt; i++) {
               gaps.insert((unsigned)i);
            }
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
   IESPacket getIESData(const std::pair<BinaryData, BinaryData>& keyVal)
   {
      IESPacket result;

      BinaryRefReader brr(keyVal.second.getRef());
      result.pubKey_ = SecureBinaryData{brr.get_BinaryDataRef(33)};
      result.iv_ =  SecureBinaryData{brr.get_BinaryDataRef(16)};
      result.cipherText_ = SecureBinaryData{
         brr.get_BinaryDataRef(brr.getSizeRemaining())};

      result.dbKey_ = keyVal.first;
      return result;
   }

   /////////////////////////////////////////////////////////////////////////////
   std::pair<SecureBinaryData, SecureBinaryData> generateKeyPair(
      const SecureBinaryData& saltedRoot, unsigned ctr)
   {
      SecureBinaryData hmacKey((uint8_t*)&ctr, 4);
      auto hmacVal = BtcUtils::getHMAC512(hmacKey, saltedRoot.getRef());

      //first half is the encryption key, second half is the hmac key
      BinaryRefReader brr(hmacVal.getRef());
      auto decrPrivKey = SecureBinaryData{brr.get_BinaryDataRef(32)};
      auto macKey = SecureBinaryData{brr.get_BinaryDataRef(32)};

      //decryption private key sanity check
      if (!Cryptography::ECDSA::checkPrivKeyIsValid(decrPrivKey)) {
         throw IO::WalletInterfaceException("invalid decryption private key");
      }
      return std::make_pair(std::move(decrPrivKey), std::move(macKey));
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
   std::pair<BinaryData, BinaryData> decryptPair(const IESPacket& packet,
      const SecureBinaryData& privKey, const SecureBinaryData& macKey)
   {
      //generate decryption key
      auto ecdhPubKey = Cryptography::ECDSA::pubKeyScalarMultiply(
         packet.pubKey_, privKey);
      auto decrKey = BtcUtils::getHash256(ecdhPubKey);

      //decrypt packet
      auto payload = Cryptography::Encryption::AES::decryptCBC(
         packet.cipherText_, decrKey.getRef(), packet.iv_);

      //break down payload
      BinaryRefReader brr(payload.getRef());
      auto hmac = brr.get_BinaryDataRef(32);
      auto len = brr.get_var_int();
      auto dataKey = brr.get_BinaryData(len);
      len = brr.get_var_int();
      auto dataVal = brr.get_BinaryData(len);

      //sanity check
      if (brr.getSizeRemaining() > 0) {
         throw LooseEntryException();
      }

      //compute hmac
      auto computedHmac = computeHmac( packet.dbKey_, dataKey, dataVal, macKey);
      if (computedHmac != hmac) {
         throw HMACMismatchException();
      }
      return std::make_pair(dataKey, dataVal);
   }

   ////
   std::pair<BinaryData, BinaryData> decryptPair(const IESPacket& packet,
      const std::pair<SecureBinaryData, SecureBinaryData>& keyPair)
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
      std::map<BinaryData, BinaryData>& keyValMap)->bool
   {
      for (auto& keyVal : keyValMap) {
         auto val = tx.getDataRef(keyVal.first);
         if (val != keyVal.second) {
            return false;
         }
      }
      return true;
   };

   //setup db env
   auto dbEnv = std::make_shared<LMDB::Env>();
   dbEnv->open(dbPath_, 0);
   auto filename = dbEnv->getFilename();
   ASSERT_EQ(filename, dbPath_);

   auto controlSalt = Cryptography::PRNG::generateRandomStrong(32);
   auto rawRoot = Cryptography::PRNG::generateRandomStrong(32);
   std::string dbName{"test"};

   //setup db
   auto dbIface = std::make_shared<IO::DBInterface>(
      dbEnv.get(), dbName, controlSalt, ENCRYPTION_TOPLAYER_VERSION);
   dbIface->loadAllEntries(rawRoot);

   //commit some values
   std::map<BinaryData, BinaryData> keyValMap;
   for (unsigned i=0; i<50; i++) {
      keyValMap.emplace(
         Cryptography::PRNG::generateRandomStrong(20),
         Cryptography::PRNG::generateRandomStrong(80)
      );
   }

   {
      //add the values
      IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), true);
      auto mapToWrite = keyValMap;
      for (auto& keyVal : mapToWrite) {
         tx.insert(keyVal.first, keyVal.second);
      }
      //try to grab them from the live write tx
      EXPECT_TRUE(checkVals(tx, keyValMap));

      //try to create read tx, should fail
      try {
         IO::WalletIfaceTransaction readTx(nullptr, dbIface.get(), false);
         ASSERT_TRUE(false);
      } catch (const IO::WalletInterfaceException& e) {
         EXPECT_EQ(e.what(), std::string{"failed to create db tx"});
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
      IO::WalletIfaceTransaction tx{nullptr, dbIface.get(), false};
      EXPECT_TRUE(checkVals(tx, keyValMap));

      //check them from nested read tx
      {
         IO::WalletIfaceTransaction tx2{nullptr, dbIface.get(), false};
         EXPECT_TRUE(checkVals(tx2, keyValMap));
         EXPECT_TRUE(checkVals(tx, keyValMap));
      }

      //closing nested tx shouldn't affect parent
      EXPECT_TRUE(checkVals(tx, keyValMap));

      //should fail to open write tx while read tx is live
      try {
         IO::WalletIfaceTransaction tx(nullptr, dbIface.get(), true);
         ASSERT_TRUE(false);
      } catch (const IO::WalletInterfaceException& e)
      {
         EXPECT_EQ(e.what(), std::string{"failed to create db tx"});
      }

      //failed write tx shouldn't affect read tx
      EXPECT_TRUE(checkVals(tx, keyValMap));
   }

   {
      //modify db
      IO::WalletIfaceTransaction tx{nullptr, dbIface.get(), true};

      {
         //nest tx
         IO::WalletIfaceTransaction tx{nullptr, dbIface.get(), true};
         auto iter = keyValMap.begin();
         for (unsigned i=0; i<10; i++) {
            ++iter;
         }
         iter->second = Cryptography::PRNG::generateRandomStrong(35);
         auto valToWrite = iter->second;
         tx.insert(iter->first, valToWrite);

         for (unsigned i=0; i<10; i++) {
            ++iter;
         }
         iter->second = Cryptography::PRNG::generateRandomStrong(70);
         auto valToWrite2 = iter->second;
         tx.insert(iter->first, valToWrite2);
      }

      auto pair1 = std::make_pair(
         Cryptography::PRNG::generateRandomStrong(40),
         Cryptography::PRNG::generateRandomStrong(80));
      auto pair2 = std::make_pair(
         Cryptography::PRNG::generateRandomStrong(20),
         Cryptography::PRNG::generateRandomStrong(16));

      tx.insert(pair1.first, pair1.second);
      tx.insert(pair2.first, pair2.second);

      //check data
      EXPECT_TRUE(checkVals(tx, keyValMap));
   }

   //check data after commit
   IO::WalletIfaceTransaction tx{nullptr, dbIface.get(), false};
   EXPECT_TRUE(checkVals(tx, keyValMap));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletInterfaceTest, WalletIfaceTransaction_Concurrency_Test)
{
   //setup env
   auto dbEnv = std::make_shared<LMDB::Env>(3);
   dbEnv->open(dbPath_, 0);
   auto filename = dbEnv->getFilename();
   ASSERT_EQ(filename, dbPath_);

   auto controlSalt = Cryptography::PRNG::generateRandomStrong(32);
   auto rawRoot = Cryptography::PRNG::generateRandomStrong(32);
   std::string dbName{"test"};

   auto dbIface = std::make_shared<IO::DBInterface>(
      dbEnv.get(), dbName, controlSalt, ENCRYPTION_TOPLAYER_VERSION);

   //sanity check
   ASSERT_EQ(dbIface->getEntryCount(), 0U);
   dbIface->loadAllEntries(rawRoot);
   ASSERT_EQ(dbIface->getEntryCount(), 0U);

   std::map<BinaryData, BinaryData> dataMap1;
   for (unsigned i=0; i<30; i++) {
      dataMap1.emplace(
         Cryptography::PRNG::generateRandomStrong(20),
         Cryptography::PRNG::generateRandomStrong(64)
      );
   }

   std::map<BinaryData, BinaryData> dataMap2;
   for (unsigned i=0; i<10; i++) {
      dataMap2.emplace(
         Cryptography::PRNG::generateRandomStrong(25),
         Cryptography::PRNG::generateRandomStrong(64)
      );
   }

   std::map<BinaryData, BinaryData> modifiedMap;
   {
      auto iter = dataMap1.begin();
      for (unsigned i=0; i<8; i++) {
         ++iter;
      }
      modifiedMap.emplace(iter->first, Cryptography::PRNG::generateRandomStrong(48));

      ++iter; ++iter;
      modifiedMap.emplace(iter->first, Cryptography::PRNG::generateRandomStrong(60));

      ++iter; ++iter; ++iter;
      modifiedMap.emplace(iter->first, Cryptography::PRNG::generateRandomStrong(87));
   }
   dataMap2.insert(modifiedMap.begin(), modifiedMap.end());

   auto checkDbValues = [](IO::DBIfaceTransaction* tx,
      std::map<BinaryData, BinaryData> dataMap)->unsigned
   {
      auto iter = dataMap.begin();
      while (iter != dataMap.end()) {
         auto dbData = tx->getDataRef(iter->first);
         if (dbData == iter->second.getRef()) {
            dataMap.erase(iter++);
            continue;
         }
         ++iter;
      }
      return dataMap.size();
   };

   auto finalMap = dataMap2;
   finalMap.insert(dataMap1.begin(), dataMap1.end());

   auto writeThread2 = [&](void)->void {
      IO::WalletIfaceTransaction tx{nullptr, dbIface.get(), true};

      //check dataMap1 is in
      EXPECT_EQ(checkDbValues(&tx, dataMap1), 0U);

      for (auto& dataPair : dataMap2) {
         tx.insert(dataPair.first, dataPair.second);
      }
      EXPECT_EQ(checkDbValues(&tx, finalMap), 0U);
   };

   std::thread* writeThr;
   {
      //create write tx in main thread
      IO::WalletIfaceTransaction tx{nullptr, dbIface.get(), true};

      //fire second thread with another write tx
      writeThr = new std::thread(writeThread2);

      //check db is empty
      EXPECT_EQ(checkDbValues(&tx, dataMap1), dataMap1.size());

      //modify db through main thread
      auto mapToWrite = dataMap1;
      for (auto& dataPair : mapToWrite) {
         tx.insert(dataPair.first, dataPair.second);
      }
      //check values
      EXPECT_EQ(checkDbValues(&tx, dataMap1), 0U);
   }

   //wait on 2nd thread
   writeThr->join();
   delete writeThr;

   {
      //check db is consistent with main thread -> 2nd thread modification order
      IO::WalletIfaceTransaction tx{nullptr, dbIface.get(), false};
      EXPECT_EQ(checkDbValues(&tx, finalMap), 0U);
   }

   /***********/

   //check read tx consistency while write tx is live
   std::map<BinaryData, BinaryData> dataMap5;
   for (unsigned i=0; i<10; i++) {
      dataMap5.emplace(
         Cryptography::PRNG::generateRandomStrong(25),
         Cryptography::PRNG::generateRandomStrong(64)
      );
   }

   {
      auto iter = finalMap.begin();
      for (unsigned i=0; i<25; i++) {
         ++iter;
      }
      dataMap5.emplace(iter->first, Cryptography::PRNG::generateRandomStrong(50));

      ++iter; ++iter;
      dataMap5.emplace(iter->first, Cryptography::PRNG::generateRandomStrong(65));
   }

   auto finalMap2 = dataMap5;
   finalMap2.insert(finalMap.begin(), finalMap.end());

   auto writeThread4 = [&](void)->void
   {
      IO::WalletIfaceTransaction tx{nullptr, dbIface.get(), true};
      EXPECT_EQ(checkDbValues(&tx, finalMap), 0U);

      for (auto& dataPair : dataMap5) {
         tx.insert(dataPair.first, dataPair.second);
      }
      EXPECT_EQ(checkDbValues(&tx, finalMap2), 0U);
   };

   //create read tx
   {
      IO::WalletIfaceTransaction tx{nullptr, dbIface.get(), false};
      EXPECT_EQ(checkDbValues(&tx, finalMap), 0U);

      //create write thread
      std::thread writeThr4(writeThread4);
      EXPECT_EQ(checkDbValues(&tx, finalMap), 0U);

      writeThr4.join();

      //data for this read tx should be unchanged
      EXPECT_EQ(checkDbValues(&tx, finalMap), 0U);
   }

   //final check
   IO::WalletIfaceTransaction tx{nullptr, dbIface.get(), false};
   EXPECT_EQ(checkDbValues(&tx, finalMap2), 0U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletInterfaceTest, EncryptionTest)
{
   auto dbEnv = std::make_shared<LMDB::Env>();
   dbEnv->open(dbPath_, 0);
   auto filename = dbEnv->getFilename();
   ASSERT_EQ(filename, dbPath_);

   auto controlSalt = Cryptography::PRNG::generateRandomStrong(32);
   auto rawRoot = Cryptography::PRNG::generateRandomStrong(32);
   std::string dbName{"test"};

   auto dbIface = std::make_shared<IO::DBInterface>(
      dbEnv.get(), dbName, controlSalt, ENCRYPTION_TOPLAYER_VERSION);

   //setup new db
   ASSERT_EQ(dbIface->getEntryCount(), 0U);
   dbIface->loadAllEntries(rawRoot);
   ASSERT_EQ(dbIface->getEntryCount(), 0U);

   //generate data
   auto key1 = Cryptography::PRNG::generateRandomStrong(20);
   auto key2 = Cryptography::PRNG::generateRandomStrong(15);
   auto key3 = Cryptography::PRNG::generateRandomStrong(12);

   auto val1 = Cryptography::PRNG::generateRandomStrong(64);
   auto val2 = Cryptography::PRNG::generateRandomStrong(64);
   auto val3 = Cryptography::PRNG::generateRandomStrong(240);
   auto val4 = Cryptography::PRNG::generateRandomStrong(16);
   auto val5 = Cryptography::PRNG::generateRandomStrong(120);

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
   LMDB::DB dbObj;
   dbObj.open(dbEnv.get(), dbName);

   //grab all entries in db
   auto keyValMap = getAllEntries(dbEnv, dbObj);
   EXPECT_EQ(keyValMap.size(), 4ULL);

   //check gaps
   ASSERT_EQ(tallyGaps(keyValMap).size(), 0ULL);

   //convert to IES packets
   std::vector<IESPacket> packets;
   for (auto& keyVal : keyValMap) {
      auto iesPacket = getIESData(keyVal);
      packets.emplace_back(iesPacket);
   }

   //check cryptographic material
   for (unsigned i = 0; i < packets.size(); i++) {
      auto& packet = packets[i];

      ASSERT_TRUE(Cryptography::ECDSA::verifyPublicKeyValid(packet.pubKey_));
      ASSERT_NE(packet.iv_, allZeroes16_);

      for (unsigned y=0; y<packets.size(); y++) {
         if (y==i) {
            continue;
         }
         auto packetY = packets[y];
         ASSERT_NE(packet.iv_, packetY.iv_);
         ASSERT_NE(packet.pubKey_, packetY.pubKey_);
      }
   }

   /* decryption leg */

   //generate seed
   auto saltedRoot = BtcUtils::getHMAC256(controlSalt, rawRoot);

   //generate first key pair
   auto firstKeyPair = generateKeyPair(saltedRoot.getRef(), 0);

   std::pair<SecureBinaryData, SecureBinaryData> currentKeyPair;
   try {
      auto& packet = packets[0];

      //check cylce flag is first entry in db
      ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), 0U);

      //check first entry is a cycle flag
      auto dataPair = decryptPair(packet, firstKeyPair);
      ASSERT_EQ(dataPair.first.getSize(), 0ULL);
      ASSERT_EQ(dataPair.second, BinaryData::fromString("cycle"sv));

      //cycle key pair
      currentKeyPair = generateKeyPair(saltedRoot.getRef(), 1);
   } catch (...) {
      ASSERT_FALSE(true);
   }

   //decrypt the other values with wrong key pair
   std::vector<std::pair<BinaryData, BinaryData>> decryptedPairs;
   for (unsigned i=1; i<packets.size(); i++) {
      auto packet = packets[i];
      ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), i);

      try {
         auto dataPair = decryptPair(packet, firstKeyPair);
         decryptedPairs.push_back(dataPair);
         ASSERT_FALSE(true);
      } catch(...) {
         continue;
      }
   }

   //decrypt the other values with proper key pair
   for (unsigned i=1; i<packets.size(); i++) {
      auto packet = packets[i];
      ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), i);

      try {
         auto dataPair = decryptPair(packet, currentKeyPair);
         decryptedPairs.push_back(dataPair);
      } catch(...) {
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
   auto dbEnv = std::make_shared<LMDB::Env>();
   dbEnv->open(dbPath_, 0);
   auto filename = dbEnv->getFilename();
   ASSERT_EQ(filename, dbPath_);

   auto controlSalt = Cryptography::PRNG::generateRandomStrong(32);
   auto rawRoot = Cryptography::PRNG::generateRandomStrong(32);
   std::string dbName("test");

   auto dbIface = std::make_shared<IO::DBInterface>(
      dbEnv.get(), dbName, controlSalt, ENCRYPTION_TOPLAYER_VERSION);

   //sanity check
   ASSERT_EQ(dbIface->getEntryCount(), 0U);
   dbIface->loadAllEntries(rawRoot);
   ASSERT_EQ(dbIface->getEntryCount(), 0U);

   //generate data
   auto key1 = Cryptography::PRNG::generateRandomStrong(20);
   auto key2 = Cryptography::PRNG::generateRandomStrong(15);
   auto key3 = Cryptography::PRNG::generateRandomStrong(12);

   auto val1 = Cryptography::PRNG::generateRandomStrong(64);
   auto val2 = Cryptography::PRNG::generateRandomStrong(64);
   auto val3 = Cryptography::PRNG::generateRandomStrong(32);
   auto val4 = Cryptography::PRNG::generateRandomStrong(16);
   auto val5 = Cryptography::PRNG::generateRandomStrong(120);

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
   LMDB::DB dbObj;
   dbObj.open(dbEnv.get(), dbName);

   //grab all entries in db
   auto keyValMap = getAllEntries(dbEnv, dbObj);
   EXPECT_EQ(keyValMap.size(), 5ULL);

   //check gaps
   {
      auto gaps = tallyGaps(keyValMap);
      ASSERT_EQ(gaps.size(), 2ULL);

      auto gapsIter = gaps.begin();
      EXPECT_EQ(*gapsIter, 2U);
      
      ++gapsIter;
      EXPECT_EQ(*gapsIter, 3U);

      ++gapsIter;
      EXPECT_EQ(gapsIter, gaps.end());
   }

   //convert to IES packets
   std::vector<IESPacket> packets;
   for(auto& keyVal : keyValMap) {
      auto iesPacket = getIESData(keyVal);
      packets.push_back(iesPacket);
   }

   //check cryptographic material
   for(unsigned i=0; i<packets.size(); i++) {
      auto& packet = packets[i];

      ASSERT_TRUE(Cryptography::ECDSA::verifyPublicKeyValid(packet.pubKey_));
      ASSERT_NE(packet.iv_, allZeroes16_);

      for(unsigned y=0; y<packets.size(); y++) {
         if (y==i) {
            continue;
         }

         auto packetY = packets[y];
         ASSERT_NE(packet.iv_, packetY.iv_);
         ASSERT_NE(packet.pubKey_, packetY.pubKey_);
      }
   }

   /* decryption leg */

   //generate seed
   auto saltedRoot = BtcUtils::getHMAC256(controlSalt, rawRoot);

   //generate first key pair
   auto firstKeyPair = generateKeyPair(saltedRoot.getRef(), 0);

   std::pair<SecureBinaryData, SecureBinaryData> currentKeyPair;
   try {
      auto& packet = packets[0];

      //check cylce flag is first entry in db
      ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), 0U);

      //check first entry is a cycle flag
      auto dataPair = decryptPair(packet, firstKeyPair);
      ASSERT_EQ(dataPair.first.getSize(), 0ULL);
      ASSERT_EQ(dataPair.second, BinaryData::fromString("cycle"sv));

      //cycle key pair
      currentKeyPair = generateKeyPair(saltedRoot.getRef(), 1);
   } catch (...) {
      ASSERT_FALSE(true);
   }

   //decrypt the other values with wrong key pair
   std::vector<std::pair<BinaryData, BinaryData>> decryptedPairs;
   for (unsigned i=1; i<packets.size(); i++) {
      auto packet = packets[i];
      try {
         auto dataPair = decryptPair(packet, firstKeyPair);
         decryptedPairs.push_back(dataPair);
         ASSERT_FALSE(true);
      } catch(...) {
         continue;
      }
   }

   //decrypt the other values with proper key pair
   for (unsigned i=1; i<packets.size(); i++) {
      auto packet = packets[i];

      try {
         auto dataPair = decryptPair(packet, currentKeyPair);
         decryptedPairs.push_back(dataPair);
      } catch(...) {
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
   auto dbEnv = std::make_shared<LMDB::Env>();
   dbEnv->open(dbPath_, 0);
   auto filename = dbEnv->getFilename();
   ASSERT_EQ(filename, dbPath_);

   auto controlSalt = Cryptography::PRNG::generateRandomStrong(32);
   auto rawRoot = Cryptography::PRNG::generateRandomStrong(32);
   std::string dbName("test");

   auto dbIface = std::make_shared<IO::DBInterface>(
      dbEnv.get(), dbName, controlSalt, ENCRYPTION_TOPLAYER_VERSION);

   //sanity check
   ASSERT_EQ(dbIface->getEntryCount(), 0U);
   dbIface->loadAllEntries(rawRoot);
   ASSERT_EQ(dbIface->getEntryCount(), 0U);

   //generate data
   auto key1 = Cryptography::PRNG::generateRandomStrong(20);
   auto key2 = Cryptography::PRNG::generateRandomStrong(15);
   auto key3 = Cryptography::PRNG::generateRandomStrong(12);

   auto val1 = Cryptography::PRNG::generateRandomStrong(64);
   auto val2 = Cryptography::PRNG::generateRandomStrong(64);
   auto val3 = Cryptography::PRNG::generateRandomStrong(32);
   auto val4 = Cryptography::PRNG::generateRandomStrong(16);
   auto val5 = Cryptography::PRNG::generateRandomStrong(120);

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
   LMDB::DB dbObj;
   dbObj.open(dbEnv.get(), dbName);

   //grab all entries in db
   auto keyValMap = getAllEntries(dbEnv, dbObj);
   EXPECT_EQ(keyValMap.size(), 5ULL);

   //check gaps
   {
      auto gaps = tallyGaps(keyValMap);
      ASSERT_EQ(gaps.size(), 2ULL);

      auto gapsIter = gaps.begin();
      EXPECT_EQ(*gapsIter, 2U);

      ++gapsIter;
      EXPECT_EQ(*gapsIter, 3U);

      ++gapsIter;
      EXPECT_EQ(gapsIter, gaps.end());
   }

   //convert to IES packets
   std::vector<IESPacket> packets;
   for(auto& keyVal : keyValMap) {
      auto&& iesPacket = getIESData(keyVal);
      packets.push_back(iesPacket);
   }

   //check cryptographic material
   for (unsigned i=0; i<packets.size(); i++) {
      auto& packet = packets[i];

      ASSERT_TRUE(Cryptography::ECDSA::verifyPublicKeyValid(packet.pubKey_));
      ASSERT_NE(packet.iv_, allZeroes16_);

      for (unsigned y=0; y<packets.size(); y++) {
         if (y==i) {
            continue;
         }
         auto packetY = packets[y];
         ASSERT_NE(packet.iv_, packetY.iv_);
         ASSERT_NE(packet.pubKey_, packetY.pubKey_);
      }
   }

   /* decryption leg */

   //generate seed
   auto saltedRoot = BtcUtils::getHMAC256(controlSalt, rawRoot);

   //generate first key pair
   auto firstKeyPair = generateKeyPair(saltedRoot.getRef(), 0);

   std::pair<SecureBinaryData, SecureBinaryData> currentKeyPair;
   try {
      auto& packet = packets[0];

      //check cylce flag is first entry in db
      ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), 0U);

      //check first entry is a cycle flag
      auto&& dataPair = decryptPair(packet, firstKeyPair);
      ASSERT_EQ(dataPair.first.getSize(), 0ULL);
      ASSERT_EQ(dataPair.second, BinaryData::fromString("cycle"sv));

      //cycle key pair
      currentKeyPair = generateKeyPair(saltedRoot.getRef(), 1);
   } catch(...) {
      ASSERT_FALSE(true);
   }

   //decrypt the other values with wrong key pair
   std::vector<std::pair<BinaryData, BinaryData>> decryptedPairs;
   for (unsigned i=1; i<packets.size(); i++) {
      auto packet = packets[i];

      try {
         auto dataPair = decryptPair(packet, firstKeyPair);
         decryptedPairs.push_back(dataPair);
         ASSERT_FALSE(true);
      } catch(...) {
         continue;
      }
   }

   //decrypt the other values with proper key pair
   for (unsigned i=1; i<packets.size(); i++) {
      auto packet = packets[i];

      try {
         auto dataPair = decryptPair(packet, currentKeyPair);
         decryptedPairs.push_back(dataPair);
      } catch(...) {
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
   dbIface = std::make_shared<IO::DBInterface>(
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

   auto key4 = Cryptography::PRNG::generateRandomStrong(30);
   auto val6 = Cryptography::PRNG::generateRandomStrong(154);

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
   LMDB::DB dbObj2;
   dbObj2.open(dbEnv.get(), dbName);

   //grab all entries in db
   keyValMap = getAllEntries(dbEnv, dbObj2);
   EXPECT_EQ(keyValMap.size(), 9ULL);

   //check gaps
   {
      auto gaps = tallyGaps(keyValMap);
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
   for (auto& keyVal : keyValMap) {
      auto iesPacket = getIESData(keyVal);
      packets.push_back(iesPacket);
   }

   //check cryptographic material
   for (unsigned i=0; i<packets.size(); i++) {
      auto& packet = packets[i];

      ASSERT_TRUE(Cryptography::ECDSA::verifyPublicKeyValid(packet.pubKey_));
      ASSERT_NE(packet.iv_, allZeroes16_);

      for(unsigned y=0; y<packets.size(); y++) {
         if (y==i) {
            continue;
         }
         auto packetY = packets[y];
         ASSERT_NE(packet.iv_, packetY.iv_);
         ASSERT_NE(packet.pubKey_, packetY.pubKey_);
      }
   }

   /* 2nd decryption leg */

   try {
      auto& packet = packets[0];

      //check cylce flag is first entry in db
      ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), 0U);

      //check first entry is a cycle flag
      auto dataPair = decryptPair(packet, firstKeyPair);
      ASSERT_EQ(dataPair.first.getSize(), 0ULL);
      ASSERT_EQ(dataPair.second, BinaryData::fromString("cycle"sv));
   } catch(...) {
      ASSERT_FALSE(true);
   }

   //decrypt the other values
   decryptedPairs.clear();
   for (unsigned i=1; i<4; i++) {
      auto packet = packets[i];

      try {
         auto dataPair = decryptPair(packet, currentKeyPair);
         decryptedPairs.push_back(dataPair);
      } catch(...) {
         ASSERT_FALSE(true);
      }
   }

   {
      //check packets[2] is a cycle flag
      ASSERT_EQ(decryptedPairs[2].first.getSize(), 0ULL);
      ASSERT_EQ(decryptedPairs[2].second, BinaryData::fromString("cycle"sv));

      //cycle key
      currentKeyPair = generateKeyPair(saltedRoot.getRef(), 2);
   }

   //decrypt last set of values with cycled keys
   for (unsigned i=4; i<packets.size(); i++) {
      auto packet = packets[i];

      try {
         auto dataPair = decryptPair(packet, currentKeyPair);
         decryptedPairs.push_back(dataPair);
      } catch(...) {
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
   /***
   NOTE:
      Wallet creation does necessarely take as the target unlock time.
      This is because KDF memory usage is tuned to 1/4th of target unlock time.
   ***/

   //passphrase lambdas
   auto thePassphrase = SecureBinaryData::fromString("abcd");
   auto unlockLbd = [&thePassphrase](
      const std::set<EncryptionKeyId>&)->Passphrase::Result
   {
      return { thePassphrase, true };
   };
   auto setPassLbd = [&thePassphrase]()->std::unique_ptr<Passphrase::Params>
   {
      return std::make_unique<Passphrase::Params>(400ms, 0, thePassphrase);
   };

   int count=0;
   auto passBad = [&count](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      if (count++ == 0) {
         return { SecureBinaryData::fromString("efghij"), true };
      }
      return { {}, false };
   };

   {
      //create wallet iface
      IO::WalletDBInterface dbIface;
      IO::CreateFileParams params{
         dbPath_, Passphrase::SetNew{setPassLbd}};
      dbIface.createEnv(params);
      dbIface.setupEnv(params.getOpenFileParams());

      //close iface
      dbIface.shutdown();
   }

   {
      //try to open iface with empty passphrase
      auto start = std::chrono::system_clock::now();
      try {
         IO::WalletDBInterface dbIface;
         dbIface.setupEnv(IO::ReadOnlyFileParams{dbPath_, {}});
         ASSERT_TRUE(false);
      } catch (const Encryption::DecryptedDataContainerException& e) {
         EXPECT_EQ(e.what(), std::string("empty passphrase lambda"));
      }
      auto end = std::chrono::system_clock::now();
      auto timeTaken = end-start;
      EXPECT_LE(timeTaken, 10ms);

      //try to open iface with bad passphrase
      start = std::chrono::system_clock::now();
      try {
         IO::WalletDBInterface dbIface;
         dbIface.setupEnv(IO::ReadOnlyFileParams{dbPath_, passBad});
         ASSERT_TRUE(false);
      } catch (const Encryption::DecryptedDataContainerException& e) {
         EXPECT_EQ(e.what(), std::string("unlock request rejected"));
      }
      end = std::chrono::system_clock::now();
      timeTaken = end-start;
      EXPECT_GE(timeTaken, 400ms);
      EXPECT_LE(timeTaken, 600ms);

      //open with proper passphrase
      try {
         start = std::chrono::system_clock::now();
         IO::WalletDBInterface dbIface;
         dbIface.setupEnv(IO::ReadOnlyFileParams{dbPath_, unlockLbd});
         end = std::chrono::system_clock::now();
         timeTaken = end-start;
         EXPECT_GE(timeTaken, 400ms);
         EXPECT_LE(timeTaken, 600ms);

         dbIface.shutdown();
      } catch (...) {
         ASSERT_FALSE(true);
      }
   }

   auto dbPath2 = homedir_ / "db2_test";
   {
      //create wallet iface with empty passphrase lambda
      auto setPassLbd2 = []()->std::unique_ptr<Passphrase::Params>
      {
         return std::make_unique<Passphrase::Params>(600ms, 0, SecureBinaryData{});
      };
   
      IO::WalletDBInterface dbIface;
      dbIface.createEnv(IO::CreateFileParams{
         dbPath2, Passphrase::SetNew{setPassLbd2}
      });
      dbIface.setupEnv(IO::ReadOnlyFileParams{dbPath2, {}});

      //close iface
      dbIface.shutdown();
   }

   {
      auto unlockFunc2 = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         throw std::runtime_error("shouldn't get here");
      };

      //reopen iface, check it won't hit the passphrase lambda
      IO::WalletDBInterface dbIface;
      try {
         auto start = std::chrono::system_clock::now();
         dbIface.setupEnv(IO::ReadOnlyFileParams{dbPath2, unlockFunc2});
         auto end = std::chrono::system_clock::now();

         //it should be almost instant, since the 600ms kdf will not be used,
         //as no passphrase was set
         EXPECT_LE(end-start, 50ms);

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
   auto thePassphrase = SecureBinaryData::fromString("abcd");
   auto unlockLbd = [&thePassphrase](
      const std::set<EncryptionKeyId>&)->Passphrase::Result
   {
      return { thePassphrase, true };
   };
   auto setPassLbd = [&thePassphrase]()->std::unique_ptr<Passphrase::Params>
   {
      return std::make_unique<Passphrase::Params>(1ms, 0, thePassphrase);
   };

   auto checkDbValues = [](IO::WalletDBInterface& iface, std::string dbName,
      std::map<BinaryData, BinaryData> dataMap)->bool
   {
      auto tx = iface.beginReadTransaction(dbName);
      auto dbIter = tx->getIterator();
      while (dbIter->isValid()) {
         auto key = dbIter->key();
         auto val = dbIter->value();

         auto dataIter = dataMap.find(key);
         if (dataIter != dataMap.end()) {
            if (dataIter->second == val)
               dataMap.erase(dataIter);
         }
         dbIter->advance();
      }
      return dataMap.empty();
   };

   //create wallet dbEnv
   IO::WalletDBInterface dbIface;
   dbIface.createEnv(IO::CreateFileParams{dbPath_,
      Passphrase::SetNew{setPassLbd}});
   dbIface.setupEnv(IO::ReadOnlyFileParams{dbPath_, unlockLbd});

   //add db
   {
      EXPECT_EQ(dbIface.getDbCount(), 0U);

      auto headerPtr = std::make_shared<IO::WalletHeader_Custom>();
      headerPtr->walletID_ = "db1"sv;

      auto lock = dbIface.lockControlContainer(unlockLbd);
      dbIface.addHeader(headerPtr);
      EXPECT_EQ(dbIface.getDbCount(), 1U);
   }

   {
      auto dbHeader = dbIface.getWalletHeader("db1");
      ASSERT_EQ(dbHeader->getDbName(), "db1");
      ASSERT_NE(std::dynamic_pointer_cast<IO::WalletHeader_Custom>(dbHeader),
         nullptr);
   }

   //set db1 values
   std::map<BinaryData, BinaryData> db1Values;
   for (unsigned i=0; i<10; i++) {
      db1Values.emplace(
         Cryptography::PRNG::generateRandomStrong(10),
         Cryptography::PRNG::generateRandomStrong(30)
      );
   }
   
   {
      auto tx = dbIface.beginWriteTransaction("db1");
      auto mapToWrite = db1Values;
      for (auto& keyVal : mapToWrite) {
         tx->insert(keyVal.first, keyVal.second);
      }
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
      db1Iter->second = Cryptography::PRNG::generateRandomStrong(18);
      auto valToWrite = db1Iter->second;
      tx->insert(db1Iter->first, valToWrite);

      db1Iter++; db1Iter++;
      db1Iter->second = Cryptography::PRNG::generateRandomStrong(42);
      valToWrite = db1Iter->second;
      tx->insert(db1Iter->first, valToWrite);

      auto dataPair = std::make_pair(
         Cryptography::PRNG::generateRandomStrong(14),
         Cryptography::PRNG::generateRandomStrong(80));
      valToWrite = dataPair.second;
      tx->insert(dataPair.first, valToWrite);
      db1Values.insert(dataPair);
   }

   //check modifcations held
   EXPECT_TRUE(checkDbValues(dbIface, "db1", db1Values));

   //add new db
   {
      EXPECT_EQ(dbIface.getDbCount(), 1U);
      auto headerPtr = std::make_shared<IO::WalletHeader_Custom>();
      headerPtr->walletID_ = "db2"sv;

      auto lock = dbIface.lockControlContainer(unlockLbd);
      dbIface.addHeader(headerPtr);
      EXPECT_EQ(dbIface.getDbCount(), 2U);
   }

   //check db1 modifcations held
   EXPECT_TRUE(checkDbValues(dbIface, "db1", db1Values));

   //set db2 values
   std::map<BinaryData, BinaryData> db2Values;
   for (unsigned i=0; i<15; i++) {
      db2Values.emplace(
         Cryptography::PRNG::generateRandomStrong(12),
         Cryptography::PRNG::generateRandomStrong(38)
      );
   }

   {
      auto tx = dbIface.beginWriteTransaction("db2");
      auto mapToWrite = db2Values;
      for (auto& keyVal : mapToWrite) {
         tx->insert(keyVal.first, keyVal.second);
      }
   }

   //check values
   EXPECT_TRUE(checkDbValues(dbIface, "db1", db1Values));
   EXPECT_TRUE(checkDbValues(dbIface, "db2", db2Values));

   //try to add db, should fail
   try {
      EXPECT_EQ(dbIface.getDbCount(), 2U);
      auto headerPtr = std::make_shared<IO::WalletHeader_Custom>();
      headerPtr->walletID_ = "db3"sv;

      auto lock = dbIface.lockControlContainer(unlockLbd);
      dbIface.addHeader(headerPtr);
      ASSERT_TRUE(false);
   } catch (const IO::WalletInterfaceException& e) {
      EXPECT_EQ(e.what(), std::string("dbCount is too low"));
      EXPECT_EQ(dbIface.getDbCount(), 2U);
   }

   //shutdown db env
   dbIface.shutdown();

   //check dbIface is dead
   try {
      auto tx = dbIface.beginReadTransaction(CONTROL_DB_NAME.data());
      ASSERT_TRUE(false);
   } catch (const IO::WalletInterfaceException& e) {
      EXPECT_EQ(e.what(), std::string("null env"));
   }

   try {
      auto tx = dbIface.beginReadTransaction("db1");
      ASSERT_TRUE(false);
   } catch (const IO::WalletInterfaceException& e) {
      EXPECT_EQ(e.what(), std::string("null env"));
   }

   try {
      dbIface.lockControlContainer(unlockLbd);
      ASSERT_TRUE(false);
   } catch (const LockableException& e) {
      EXPECT_EQ(e.what(), std::string("null lockable ptr"));
   }

   //setup db env anew
   dbIface.setupEnv(IO::ReadOnlyFileParams{dbPath_, unlockLbd});

   try {
      //try to increase db count while a tx is live, should fail
      auto tx = dbIface.beginReadTransaction("db1");
      dbIface.setDbCount(5);
   } catch (const IO::WalletInterfaceException& e) {
      EXPECT_EQ(e.what(), std::string("live transactions, cannot change dbCount"));
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
      auto headerPtr = std::make_shared<IO::WalletHeader_Custom>();
      headerPtr->walletID_ = "db3"sv;

      auto lock = dbIface.lockControlContainer(unlockLbd);
      dbIface.addHeader(headerPtr);
      EXPECT_EQ(dbIface.getDbCount(), 3U);
   }

   //modify db2
   {
      auto tx = dbIface.beginWriteTransaction("db2");
      auto db2Iter = db2Values.begin();
      db2Iter++; db2Iter++; db2Iter++;
      db2Iter->second = Cryptography::PRNG::generateRandomStrong(22);
      auto valToWrite = db2Iter->second;
      tx->insert(db2Iter->first, valToWrite);

      db2Iter++;
      db2Iter->second = Cryptography::PRNG::generateRandomStrong(16);
      valToWrite = db2Iter->second;
      tx->insert(db2Iter->first, valToWrite);

      auto dataPair = std::make_pair(
         Cryptography::PRNG::generateRandomStrong(36),
         Cryptography::PRNG::generateRandomStrong(124));
      valToWrite = dataPair.second;
      tx->insert(dataPair.first, valToWrite);
      db2Values.insert(dataPair);
   }

   //set db3 values
   std::map<BinaryData, BinaryData> db3Values;
   for (unsigned i=0; i<20; i++) {
      db3Values.emplace(
         Cryptography::PRNG::generateRandomStrong(24),
         Cryptography::PRNG::generateRandomStrong(48)
      );
   }

   {
      auto tx = dbIface.beginWriteTransaction("db3");
      auto mapToWrite = db3Values;
      for (auto& keyVal : mapToWrite) {
         tx->insert(keyVal.first, keyVal.second);
      }
   }

   //check values
   EXPECT_TRUE(checkDbValues(dbIface, "db1", db1Values));
   EXPECT_TRUE(checkDbValues(dbIface, "db2", db2Values));
   EXPECT_TRUE(checkDbValues(dbIface, "db3", db3Values));

   //try to overwrite db3
   try {
      EXPECT_EQ(dbIface.getDbCount(), 3U);
      auto headerPtr = std::make_shared<IO::WalletHeader_Custom>();
      headerPtr->walletID_ = "db3"sv;

      auto lock = dbIface.lockControlContainer(unlockLbd);
      dbIface.addHeader(headerPtr);
      ASSERT_FALSE(true);
   } catch (const IO::WalletInterfaceException& e) {
      EXPECT_EQ(e.what(), std::string("header already in map"));
   }

   //check values
   EXPECT_TRUE(checkDbValues(dbIface, "db1", db1Values));
   EXPECT_TRUE(checkDbValues(dbIface, "db2", db2Values));
   EXPECT_TRUE(checkDbValues(dbIface, "db3", db3Values));

   //try to shutdown env with live tx, should fail
   try {
      auto tx = dbIface.beginReadTransaction("db2");
      dbIface.shutdown();
      ASSERT_FALSE(true);
   } catch (const IO::WalletInterfaceException& e) {
      EXPECT_EQ(e.what(), std::string("live transactions, cannot shutdown env"));
   }

   //shutdown env
   dbIface.shutdown();

   //setup db env anew
   dbIface.setupEnv(IO::ReadOnlyFileParams{dbPath_, unlockLbd});

   //check db values
   EXPECT_TRUE(checkDbValues(dbIface, "db1", db1Values));
   EXPECT_TRUE(checkDbValues(dbIface, "db2", db2Values));
   EXPECT_TRUE(checkDbValues(dbIface, "db3", db3Values));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletInterfaceTest, WipeEntries_Test)
{
   auto iface = std::make_shared<IO::WalletDBInterface>();
   iface->createEnv(IO::CreateFileParams{dbPath_,
      Passphrase::SetNew{1ms, 0, {}}});
   iface->setupEnv(IO::ReadOnlyFileParams{dbPath_, {}});

   auto dbName = "test"sv;
   auto dbHeader = std::make_shared<IO::WalletHeader_Custom>();
   dbHeader->walletID_ = dbName;
   {
      auto lock = iface->lockControlContainer({});
      iface->addHeader(dbHeader);
   }

   std::map<BinaryData, BinaryData> dataMap1;
   for (unsigned i=0; i<30; i++) {
      dataMap1.emplace(
         Cryptography::PRNG::generateRandomStrong(20),
         Cryptography::PRNG::generateRandomStrong(64)
      );
   }

   {
      //commit data
      auto tx = iface->beginWriteTransaction(dbName);
      auto mapToWrite = dataMap1;
      for (auto keyVal : mapToWrite) {
         tx->insert(keyVal.first, keyVal.second);
      }
   }

   //open raw db
   auto dbEnv = std::make_shared<LMDB::Env>(3);
   dbEnv->open(dbPath_, 0);
   auto filename = dbEnv->getFilename();
   ASSERT_EQ(filename, dbPath_);

   //grab control root
   SecureBinaryData controlRoot;
   SecureBinaryData controlSalt;
   {
      //open control db
      LMDB::DB dbCtrl;
      dbCtrl.open(dbEnv.get(), CONTROL_DB_NAME.data());

      //grab control header
      std::shared_ptr<IO::WalletHeader_Control> controlHeader;
      {
         auto tx = LMDB::Transaction(dbEnv.get(), dbCtrl.dbi(), LMDB::Mode::ReadOnly);
         BinaryWriter bw;
         bw.put_uint8_t(WALLETHEADER_PREFIX);
         bw.put_BinaryData(BinaryData::fromString(CONTROL_DB_NAME));
         LMDB::DataRef carKey(bw.getSize(), bw.getData().getPtr());
         auto rawVal = tx.get(carKey);

         BinaryDataRef refVal((const uint8_t*)rawVal.data, rawVal.len);
         BinaryRefReader brrVal(refVal);
         auto len = brrVal.get_var_int();
         auto headerVal = brrVal.get_BinaryDataRef(len);
         controlHeader = std::dynamic_pointer_cast<IO::WalletHeader_Control>(
            IO::WalletHeader::deserialize(bw.getData(), headerVal));
         controlSalt = controlHeader->controlSalt_;
      }

      //grab DecryptedDataContainer
      auto decryptedData = std::make_unique<Encryption::DecryptedDataContainer>(
         nullptr, controlHeader->getDbName(),
         controlHeader->getDefaultEncryptionKey(),
         controlHeader->getDefaultEncryptionKeyId(),
         controlHeader->defaultKdfId_, controlHeader->masterEncryptionKeyId_);
      {
         auto txInner = std::make_shared<IO::RawIfaceTransaction>(
            *dbEnv, dbCtrl, true);
         decryptedData->readFromDisk(txInner);
      }

      //grab seed
      std::unique_ptr<Seeds::EncryptedSeed> controlSeed;
      {
         auto tx = LMDB::Transaction(dbEnv.get(), dbCtrl.dbi(), LMDB::Mode::ReadOnly);
         BinaryWriter bw;
         bw.put_uint32_t(WALLET_SEED_KEY);
         LMDB::DataRef carKey(bw.getSize(), bw.getData().getPtr());
         auto rawVal = tx.get(carKey);

         BinaryDataRef refVal((const uint8_t*)rawVal.data, rawVal.len);
         BinaryRefReader brrVal(refVal);
         auto len = brrVal.get_var_int();
         auto seedVal = brrVal.get_BinaryDataRef(len);

         auto seedPtr = Seeds::EncryptedSeed::deserialize(seedVal);
         auto ptrCast = dynamic_cast<Seeds::EncryptedSeed*>(seedPtr.get());
         if (ptrCast == nullptr) {
            throw WalletException("failed to deser wallet seed");
         }
         controlSeed = std::unique_ptr<Seeds::EncryptedSeed>(ptrCast);
         seedPtr.release();
      }

      {
         auto lock = decryptedData->lockContainer({});
         controlRoot = decryptedData->getClearTextAssetData(controlSeed.get());
      }
   }

   iface->shutdown();
   dbEnv->close();
   dbEnv->open(dbPath_, 0);

   //grab db salt
   SecureBinaryData dbSalt;
   {
      LMDB::DB headerDb;
      headerDb.open(dbEnv.get(), std::string{WALLETHEADER_DBNAME});
      auto keyValMap = getAllEntries(dbEnv, headerDb);

      std::vector<IESPacket> packets;
      for (auto& keyVal : keyValMap) {
         auto iesPacket = getIESData(keyVal);
         packets.push_back(iesPacket);
      }

      //generate seed
      auto saltedRoot = BtcUtils::getHMAC256(controlSalt, controlRoot);

      //generate first key pair
      auto currentKeyPair = generateKeyPair(saltedRoot.getRef(), 1);

      //decrypt the other values with proper key pair
      std::map<BinaryData, BinaryData> decrKeyValMap;
      for (unsigned i=1; i<packets.size(); i++) {
         auto packet = packets[i];
         ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), i);
         try {
            auto dataPair = decryptPair(packet, currentKeyPair);
            decrKeyValMap.insert(dataPair);
         } catch (...) {
            ASSERT_FALSE(true);
         }
      }

      BinaryWriter bwKey;
      bwKey.put_uint8_t(WALLETHEADER_PREFIX);
      bwKey.put_StringView(dbName);

      auto iter = decrKeyValMap.find(bwKey.getData());
      BinaryRefReader brr(iter->second);
      auto len = brr.get_var_int();
      auto headerRef = brr.get_BinaryData(len);
      auto headerPtr = IO::WalletHeader::deserialize(iter->first, headerRef);
      dbSalt = headerPtr->controlSalt_;
   }

   //grab the entries
   std::map<BinaryData, IESPacket> dataKeyToCipherText;
   {
      LMDB::DB headerDb;
      headerDb.open(dbEnv.get(), dbName);
      auto keyValMap = getAllEntries(dbEnv, headerDb);

      std::vector<IESPacket> packets;
      for (auto& keyVal : keyValMap) {
         auto iesPacket = getIESData(keyVal);
         packets.push_back(iesPacket);
      }

      //generate seed
      auto saltedRoot = BtcUtils::getHMAC256(dbSalt, controlRoot);

      //generate first key pair
      auto currentKeyPair = generateKeyPair(saltedRoot.getRef(), 1);

      //decrypt the other values with proper key pair
      for (unsigned i=1; i<packets.size(); i++) {
         auto packet = packets[i];
         ASSERT_EQ(READ_UINT32_BE(packet.dbKey_), i);

         try {
            auto dataPair = decryptPair(packet, currentKeyPair);
            dataKeyToCipherText.emplace(dataPair.first, packet);

            //check decrypted data matches
            auto iter = dataMap1.find(dataPair.first);
            ASSERT_NE(iter, dataMap1.end());
            EXPECT_EQ(dataPair.second, iter->second);

         } catch(...) {
            ASSERT_FALSE(true);
         }
      }
   }

   //replacement map
   std::map<BinaryData, BinaryData> replaceMap;
   {
      auto iter = dataMap1.begin();
      for (unsigned i=0; i<10; i++) {
         ++iter;
      }
      replaceMap.emplace(iter->first, Cryptography::PRNG::generateRandomStrong(60));

      ++iter;
      replaceMap.emplace(iter->first, Cryptography::PRNG::generateRandomStrong(70));

      ++iter; ++iter; ++iter; ++iter;
      replaceMap.emplace(iter->first, Cryptography::PRNG::generateRandomStrong(80));

      ++iter;
      replaceMap.emplace(iter->first, Cryptography::PRNG::generateRandomStrong(90));

      ++iter;
      replaceMap.emplace(iter->first, Cryptography::PRNG::generateRandomStrong(100));
   }

   //check packets are on disk
   for (auto& packetPair : dataKeyToCipherText) {
      EXPECT_TRUE(TestUtils::searchFile(
         filename, packetPair.second.cipherText_));
   }

   dbEnv->close();
   dbEnv.reset();

   //reopen db iface
   iface = std::make_shared<IO::WalletDBInterface>();
   iface->setupEnv(IO::ReadOnlyFileParams{dbPath_, {}});

   //replace a couple entries
   {
      //commit data
      auto tx = iface->beginWriteTransaction(dbName);
      auto mapToWrite = replaceMap;
      for (auto keyVal : mapToWrite) {
         tx->insert(keyVal.first, keyVal.second);
      }
   }

   //check final db state
   auto finalMap = replaceMap;
   finalMap.insert(dataMap1.begin(), dataMap1.end());
   {
      auto tx = iface->beginReadTransaction(dbName);
      auto iter = tx->getIterator();
      while (iter->isValid()) {
         auto key = iter->key();
         auto mapIter = finalMap.find(key);
         ASSERT_NE(mapIter, finalMap.end());

         if (mapIter->second.getRef() == iter->value()) {
            finalMap.erase(mapIter);
         }
         iter->advance();
      }

      EXPECT_EQ(finalMap.size(), 0ULL);
   }

   //shutdown db
   iface.reset();

   //check data on file
   for (auto& packetPair : dataKeyToCipherText) {
      auto iter = replaceMap.find(packetPair.first);
      if (iter == replaceMap.end()) {
         continue;
         //untouched keys should have same ciphertext
         EXPECT_TRUE(TestUtils::searchFile(
            filename, packetPair.second.cipherText_));
      } else {
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
   Passphrase::UnlockFunc controlLbd_;

   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp()
   {
      homedir_ = std::filesystem::path("./fakehomedir");
      FileUtils::removeDirectory(homedir_);
      std::filesystem::create_directory(homedir_);

      Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Config::ProcessType::DB);

      controlPass_ = SecureBinaryData::fromString("control");
      controlLbd_ = [this](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { controlPass_, true };
      };
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      Config::reset();
      FileUtils::removeDirectory(homedir_);
   }

   /////////////////////////////////////////////////////////////////////////////
   unsigned checkDb(IO::DBIfaceTransaction* tx,
      const std::vector<SecureBinaryData>& data)
   {
      auto binaryParse = [](const BinaryDataRef& a, const BinaryDataRef& b)->bool
      {
         unsigned ctr = 0;
         while (ctr + a.getSize() <= b.getSize()) {
            if (b.getPtr()[ctr] == a.getPtr()[0]) {
               if (b.getSliceRef(ctr, a.getSize()) == a)
                  return true;
            }
            ++ctr;
         }
         return false;
      };

      auto parseDb = [tx, &binaryParse](const BinaryDataRef& val)->bool
      {
         auto iter = tx->getIterator();
         while (iter->isValid()) {
            auto key = iter->key();
            if (key.getSize() >= val.getSize()) {
               if (binaryParse(val, key)) {
                  return true;
               }
            }

            auto value = iter->value();
            if (value.getSize() >= val.getSize()) {
               if (binaryParse(val, value))
                  return true;
            }
            iter->advance();
         }
         return false;
      };

      std::set<BinaryData> dataSet;
      for (auto& val : data) {
         dataSet.insert(val);
      }

      auto setIter = dataSet.begin();
      while (setIter != dataSet.end()) {
         if (parseDb(setIter->getRef())) {
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
   std::map<std::string, std::vector<BinaryData>> addrMap;
   std::map<std::string, std::filesystem::path> filenames;

   std::shared_ptr<Progress::State> progState;
   unsigned count=0;
   auto progressFunc = [&progState, &count, this](std::unique_ptr<Progress::State> notif)
   {
      ++count;
      switch (notif->type())
      {
         case Progress::StateEnum::CreateWalletFile:
         {
            //there should be no prev state
            ASSERT_EQ(progState, nullptr);

            //save notif
            progState = std::shared_ptr<Progress::State>(notif.release());
            auto cfState = std::dynamic_pointer_cast<Progress::CreateWalletFile>(progState);
            ASSERT_NE(cfState, nullptr);

            //validate wallet path
            ASSERT_EQ(homedir_, cfState->path().parent_path());
            break;
         }

         case Progress::StateEnum::InitWalletFile:
         {
            //prev state should be CreateFile
            ASSERT_NE(progState, nullptr);
            ASSERT_EQ(progState->type(), Progress::StateEnum::CreateWalletFile);

            //save notif
            progState = std::shared_ptr<Progress::State>(notif.release());
            auto initState = std::dynamic_pointer_cast<Progress::InitWalletFile>(progState);
            ASSERT_NE(initState, nullptr);

            //validate master id
            ASSERT_FALSE(initState->masterId().empty());
            break;
         }

         case Progress::StateEnum::ReadWalletFile:
         {
            //prev state should be InitFile
            ASSERT_NE(progState, nullptr);
            ASSERT_EQ(progState->type(), Progress::StateEnum::InitWalletFile);

            //save notif
            progState = std::shared_ptr<Progress::State>(notif.release());
            auto readState = std::dynamic_pointer_cast<Progress::ReadWalletFile>(progState);
            ASSERT_NE(readState, nullptr);

            //validate master id
            ASSERT_FALSE(readState->masterId().empty());
            break;
         }

         case Progress::StateEnum::CreateAccount:
         {
            //prev state should be ReadFile
            ASSERT_NE(progState, nullptr);
            ASSERT_EQ(progState->type(), Progress::StateEnum::ReadWalletFile);

            //save notif
            progState = std::shared_ptr<Progress::State>(notif.release());
            auto createState = std::dynamic_pointer_cast<Progress::CreateAccount>(progState);
            ASSERT_NE(createState, nullptr);

            //validate account metadata
            ASSERT_NE(createState->accPtr(), nullptr);
            break;
         }

         case Progress::StateEnum::ExtendChain:
         {
            //prev state should be CreateAccount
            ASSERT_NE(progState, nullptr);
            ASSERT_EQ(progState->type(), Progress::StateEnum::CreateAccount);

            //save notif
            progState = std::shared_ptr<Progress::State>(notif.release());
            auto extendState = std::dynamic_pointer_cast<Progress::ExtendChain>(progState);
            ASSERT_NE(extendState, nullptr);

            //validate account metadata
            ASSERT_EQ(extendState->lookup(), 4);

            //clear progState cuse that's the last state
            progState.reset();
            break;
         }

         default:
            ASSERT_TRUE(false);
      }
   };

   //create 3 wallets
   for (unsigned i = 0; i < 3; i++) {
      IO::CreateWalletParams params{
         homedir_,
         Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("passphrase")},
         Passphrase::SetNew{1ms, 0, controlPass_},
         //misc stuff starts here
         progressFunc, 4,
         "lbl", "dsc"
      };
   
      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = AssetWallet_Single::createFromSeed(
         std::move(seed), params);

      ASSERT_EQ(assetWlt->getLabel(), "lbl");
      ASSERT_EQ(assetWlt->getDescription(), "dsc");

      //get AddrVec
      auto hashSet = assetWlt->getAddrHashSet();
      auto id = assetWlt->getID();
      auto& vec = addrMap[id];
      vec.insert(vec.end(), hashSet.begin(), hashSet.end());

      //get filename
      filenames.emplace(id, assetWlt->getDbFilename());

      //close wallet
      assetWlt.reset();
   }
   EXPECT_EQ(count, 15);

   for (auto& addrVecPair : addrMap) {
      auto fnameIter = filenames.find(addrVecPair.first);
      ASSERT_NE(fnameIter, filenames.end());

      auto newWallet = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{fnameIter->second, controlLbd_});

      auto wltSingle =
         std::dynamic_pointer_cast<AssetWallet_Single>(newWallet);
      ASSERT_NE(wltSingle, nullptr);

      auto hashSet = wltSingle->getAddrHashSet();
      std::vector<BinaryData> addrVec;
      addrVec.insert(addrVec.end(), hashSet.begin(), hashSet.end());
      ASSERT_EQ(addrVec, addrVecPair.second);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, RejectCreationAtPassphrase)
{
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory());
   auto filename = homedir_ / std::filesystem::path{
      "armory_" + seed->getMasterId() + "_wallet.lmdb"};

   bool fileExists = false;
   auto passFunc = [&fileExists, filename]()->std::unique_ptr<Passphrase::Params>
   {
      //check the file exists
      if (FileUtils::pathExists(filename, 0)) {
         fileExists = true;
      }
      return std::make_unique<Passphrase::Params>();
   };

   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{passFunc},
      Passphrase::SetNew{1ms, 0, controlPass_},
      //misc stuff starts here
      nullptr, 4,
      "lbl", "dsc"
   };

   try {
      auto assetWlt = AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      ASSERT_TRUE(false);
   } catch (const std::exception& e) {
      EXPECT_EQ(e.what(), std::string{"passphrase request was rejected"});
   }

   //check file is deleted
   EXPECT_TRUE(fileExists);
   EXPECT_FALSE(FileUtils::pathExists(filename, 0));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, CreateWOCopy_Test)
{
   //create 1 wallet from priv key
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("passphrase")},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4
   };

   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory());
   Seeds::LegacyType legacyType;
   {
      auto legacySeed = dynamic_cast<Seeds::ClearTextSeed_Armory*>(seed.get());
      legacyType = legacySeed->getLegacyType();
   }
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);
   auto filename = assetWlt->getDbFilename();

   //check wlt has private root
   {
      auto seed = assetWlt->getEncryptedSeed();
      ASSERT_NE(seed, nullptr);

      auto root = std::dynamic_pointer_cast<Assets::AssetEntry_ArmoryLegacyRoot>(
         assetWlt->getRoot());
      ASSERT_NE(root, nullptr);
      ASSERT_NE(root->getPrivKey(), nullptr);
   }

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

   auto publicSeed = std::make_unique<Seeds::ClearTextSeed_ArmoryPublic>(
      pubRoot, chainCode, legacyType
   );
   auto woWallet = AssetWallet_Single::createFromSeed(
      std::move(publicSeed),
      IO::CreateWalletParams{
         homedir_,
         Passphrase::SetNew{1ms, 0, {}},
         Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
         nullptr, 4
      });

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
   unsigned count = 0;
   auto passLbd = [&count](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      ++count;
      return { SecureBinaryData::fromString("control"), true };
   };

   auto reloadWoWallet = AssetWallet_Single::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{woFilename, passLbd});
   auto hashSetWO2 = reloadWoWallet->getAddrHashSet();
   ASSERT_EQ(hashSet, hashSetWO);
   EXPECT_EQ(mainAccId, reloadWoWallet->getMainAccountID());
   {
      auto accPtr = reloadWoWallet->getAccountForID(mainAccId);
      EXPECT_TRUE(accPtr->isLegacy());
   }
   EXPECT_EQ(count, 1);

   //delete the underlying file so we can fork anew
   reloadWoWallet.reset();
   std::filesystem::remove(woFilename.c_str());

   //fork WO from full wallet
   auto forkFilename = AssetWallet::forkWatchingOnly(
      IO::ReadOnlyFileParams{filename, passLbd},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")}
   );
   EXPECT_EQ(count, 2);

   auto woFork = AssetWallet::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{forkFilename, passLbd});
   auto hashSetFork = woFork->getAddrHashSet();
   ASSERT_EQ(hashSet, hashSetFork);
   EXPECT_EQ(mainAccId, woFork->getMainAccountID());
   {
      auto accPtr = woFork->getAccountForID(mainAccId);
      EXPECT_TRUE(accPtr->isLegacy());
   }
   EXPECT_EQ(count, 3);

   //check WO wlt has no private root
   {
      auto woSingle = std::dynamic_pointer_cast<AssetWallet_Single>(woFork);
      ASSERT_NE(woSingle, nullptr);
      auto seed = woSingle->getEncryptedSeed();
      ASSERT_EQ(seed, nullptr);

      auto rootWO = std::dynamic_pointer_cast<Assets::AssetEntry_ArmoryLegacyRoot>(
         woFork->getRoot());
      ASSERT_NE(rootWO, nullptr);
      ASSERT_EQ(rootWO->getPrivKey(), nullptr);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, IDs)
{
   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);
   auto rawPubkey = Cryptography::ECDSA::computePublicKey(rawEntropy);
   std::string id;
   {
      auto chaincode = BtcUtils::computeChainCode_ArmoryLegacy(rawEntropy);
      id = generateWalletId(rawPubkey, chaincode, Seeds::SeedType::ArmoryLegacy);
   }
   ASSERT_FALSE(id.empty());

   IO::CreateWalletParams params {
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("passphrase")},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4
   };

   //legacy wallet
   {
      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory(
            rawEntropy, {}, Seeds::LegacyType::Armory200));
      auto wlt = AssetWallet_Single::createFromSeed(
         move(seed), params);

      EXPECT_EQ(wlt->getID(), id);
   }

   //bip32 wallet
   {
      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_BIP32(
            rawEntropy,
            Seeds::SeedType::BIP32_Structured));
      auto wlt = AssetWallet_Single::createFromSeed(
         std::move(seed), params);

      //wallet id
      BIP32_Node node;
      node.initFromSeed(rawEntropy);

      auto idBip32 = generateWalletId(node.getPublicKey(), node.getChaincode(),
         Seeds::SeedType::BIP32_Structured);
      EXPECT_EQ(wlt->getID(), idBip32);

      //account ids
      auto seedFingerprint = node.getThisFingerprint();
      auto generateAddrAccountId = [&seedFingerprint](
         const std::vector<uint32_t>& nodes,
         const std::set<AddressEntryType>& addrTypeSet,
         bool isMain)->int32_t
      {
         BinaryWriter bw;
         bw.put_uint32_t(seedFingerprint);
         for (const auto& node : nodes) {
            bw.put_uint32_t(node, BE);
         }
         bw.put_uint32_t(0);

         for (const auto& node : nodes) {
            bw.put_uint32_t(node, BE);
         }
         bw.put_uint32_t(1, BE);

         for (auto& addrType : addrTypeSet) {
            bw.put_uint32_t(addrType, BE);
         }
         bw.put_uint32_t(*addrTypeSet.begin());
         bw.put_uint8_t(isMain);

         auto pub_hash160 = BtcUtils::getHash160(bw.getData());
         BinaryRefReader brr(pub_hash160.getRef());
         return brr.get_int32_t(BE);
      };

      AddressAccountId acc44(generateAddrAccountId(
         {0x8000002C, 0x80000000, 0x80000000},
         { AddressEntryType(
            AddressEntryType::P2PKH | AddressEntryType::Uncompressed),
         AddressEntryType::P2PKH },
         true));
      AssetAccountId outer44(acc44, 0);
      AssetAccountId inner44(acc44, 1);

      AddressAccountId acc49(generateAddrAccountId(
         {0x80000031, 0x80000000, 0x80000000},
         {AddressEntryType(AddressEntryType::P2SH | AddressEntryType::P2WPKH)},
         false));
      AssetAccountId outer49(acc49, 0);
      AssetAccountId inner49(acc49, 1);

      AddressAccountId acc84(generateAddrAccountId(
         {0x80000054, 0x80000000, 0x80000000},
         {AddressEntryType::P2WPKH},
         false));
      AssetAccountId outer84(acc84, 0);
      AssetAccountId inner84(acc84, 1);

      auto accIdSet = wlt->getAccountIDs();
      auto checkId = [&accIdSet, wlt](const AddressAccountId& accId,
         const AssetAccountId& outId, const AssetAccountId& innId)->bool
      {
         auto iter = accIdSet.find(accId);
         if (iter == accIdSet.end()) {
            return false;
         }
         accIdSet.erase(iter);

         auto accPtr = wlt->getAccountForID(accId);
         if (accPtr->getOuterAccountID() != outId) {
            return false;
         }
         if (accPtr->getInnerAccountID() != innId) {
            return false;
         }
         return true;
      };

      EXPECT_TRUE(checkId(acc44, outer44, inner44));
      EXPECT_TRUE(checkId(acc49, outer49, inner49));
      EXPECT_TRUE(checkId(acc84, outer84, inner84));
      EXPECT_TRUE(accIdSet.empty());
   }

   //legacy with chaincode
   auto chaincode = Cryptography::PRNG::generateRandomStrong(32);
   auto idcc = generateWalletId(rawPubkey, chaincode,
      Seeds::SeedType::ArmoryLegacy);
   ASSERT_NE(id, idcc);

   {
      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory(
            rawEntropy, chaincode, Seeds::LegacyType::Armory200));
      auto wlt = AssetWallet_Single::createFromSeed(
         std::move(seed), params);
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
      auto seed135 = std::make_unique<Seeds::ClearTextSeed_Armory>(
         rawSBD.getRef(), SecureBinaryData{}, Seeds::LegacyType::Armory135);
      EXPECT_EQ(seed135->getWalletId(), "2vrVAxyHR");
      EXPECT_EQ(seed135->getMasterId(), "LZxsEgeT");
   }

   //Armory200a
   {
      auto seed200 = std::make_unique<Seeds::ClearTextSeed_Armory>(
         rawSBD.getRef(), SecureBinaryData{}, Seeds::LegacyType::Armory200);
      EXPECT_EQ(seed200->getWalletId(), "2vrVAxyHR");
      EXPECT_EQ(seed200->getMasterId(), "LZxsEgeT");
   }

   //BIP32 structured
   {
      auto bip32 = std::make_unique<Seeds::ClearTextSeed_BIP32>(
         rawSBD.getRef(), Seeds::SeedType::BIP32_Structured);
      EXPECT_EQ(bip32->getWalletId(), "2BuhCGwV9");
      EXPECT_EQ(bip32->getMasterId(), "2d9H95rzK");
   }

   //BIP32 virgin
   {
      auto bip32 = std::make_unique<Seeds::ClearTextSeed_BIP32>(
         rawSBD.getRef(), Seeds::SeedType::BIP32_Virgin);
      EXPECT_EQ(bip32->getWalletId(), "22bd31PB5");
      EXPECT_EQ(bip32->getMasterId(), "2d9H95rzK");
   }

   //xpriv
   {
      BIP32_Node node;
      node.initFromSeed(rawSBD.getRef());
      auto xpriv = node.getBase58();

      auto base58 = Seeds::ClearTextSeed_BIP32::fromBase58(xpriv);
      EXPECT_EQ(base58->getWalletId(), "33qBfTB51");
      EXPECT_EQ(base58->getMasterId(), "2d9H95rzK");
   }


   //BIP39
   {
      auto bip32 = std::make_unique<Seeds::ClearTextSeed_BIP39>(rawSBD.getRef(),
         Seeds::ClearTextSeed_BIP39::Dictionnary::English_Trezor);
      EXPECT_EQ(bip32->getWalletId(), "vUXT83m9");
      EXPECT_EQ(bip32->getMasterId(), "WLKZBhnX");
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, Encryption_Test)
{
   //#1: check deriving from an encrypted root yield correct chain
   //create 1 wallet from priv key
   IO::CreateWalletParams params {
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("passphrase")},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4
   };

   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory(rawEntropy, {},
         Seeds::LegacyType::Armory200));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //derive private chain from root
   auto chaincode = BtcUtils::computeChainCode_ArmoryLegacy(rawEntropy);

   std::vector<SecureBinaryData> privateKeys;
   auto currentPrivKey = &rawEntropy;

   for (int i = 0; i < 4; i++) {
      privateKeys.emplace_back(Cryptography::ECDSA::computeChainedPrivateKey(
         *currentPrivKey, chaincode));
      currentPrivKey = &privateKeys.back();
   }

   //compute public keys
   std::vector<SecureBinaryData> publicKeys;
   for (auto& privkey : privateKeys) {
      publicKeys.emplace_back(Cryptography::ECDSA::computePublicKey(privkey));
   }

   //compare with wallet's own
   auto accountPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
   auto outerAcc = accountPtr->getOuterAccount();
   for (int i = 0; i < 4; i++) {
      //grab indexes from 0 to 3
      auto assetptr = outerAcc->getAssetForKey(i);
      ASSERT_EQ(assetptr->getType(), Assets::AssetEntryType::Single);

      auto asset_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(assetptr);
      if (asset_single == nullptr) {
         throw std::runtime_error("unexpected assetptr type");
      }
      auto pubkey_ptr = asset_single->getPubKey();
      ASSERT_EQ(pubkey_ptr->getUncompressedKey(), publicKeys[i]);
   }

   //#2: check no unencrypted private keys are on disk. Incidentally,
   //check public keys are, for sanity

   //close wallet object
   auto filename = assetWlt->getDbFilename();
   assetWlt.reset();

   //open db env for wallet
   auto passLbd = [](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { SecureBinaryData::fromString("control"), true };
   };

   IO::WalletDBInterface dbIface;
   dbIface.setupEnv(IO::ReadOnlyFileParams{filename, passLbd});
   std::string dbName;

   {
      auto tx = dbIface.beginReadTransaction(WALLETHEADER_DBNAME);
      BinaryWriter bwKey;
      bwKey.put_uint32_t(MAINWALLET_KEY);
      auto mainIdRef = tx->getDataRef(bwKey.getData());

      BinaryRefReader brr(mainIdRef);
      auto len = brr.get_var_int();
      auto mainIdBd = brr.get_BinaryData(len);
      dbName = std::string(mainIdBd.getCharPtr(), mainIdBd.getSize());
   }

   auto tx = dbIface.beginReadTransaction(dbName);
   ASSERT_EQ(checkDb(tx.get(), privateKeys), 0U);
   ASSERT_EQ(checkDb(tx.get(), publicKeys), 4U);

   /*
   Parse file for the presence of keys, neither should be visible as
   the whole thing is encrypted
   */
   for (auto& privkey : privateKeys) {
      ASSERT_FALSE(TestUtils::searchFile(filename, privkey));
   }

   for (auto& pubkey : publicKeys) {
      ASSERT_FALSE(TestUtils::searchFile(filename, pubkey));
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, SeedEncryption)
{
   //create wallet
   auto passphrase = SecureBinaryData::fromString("password");
   IO::CreateWalletParams params {
      homedir_,
      Passphrase::SetNew{350ms, 0, passphrase},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 10
   };

   //create regular wallet
   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);

   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP32(
         rawEntropy, Seeds::SeedType::BIP32_Structured));
   auto wlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);
   //check clear text seed does not exist on disk
   auto filename = wlt->getDbFilename();
   ASSERT_FALSE(TestUtils::searchFile(filename, rawEntropy));

   {
      auto kdfPtr = wlt->getPrimaryKdf();
      auto start = std::chrono::system_clock::now();
      kdfPtr->deriveKey(SecureBinaryData::fromString("test key"));
      auto end = std::chrono::system_clock::now();
      ASSERT_GE(end-start, 350ms);
      ASSERT_LE(end-start, 500ms);
   }

   //grab without passphrase lbd, should fail
   try {
      auto lock = wlt->lockDecryptedContainer({});
      auto decryptedSeed = wlt->getDecryptedValue(wlt->getEncryptedSeed());
      EXPECT_EQ(decryptedSeed, rawEntropy);
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException&) {}

   //set passphrase lambda
   auto passLbd = [&passphrase](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { passphrase, true };
   };

   //grab without locking, should fail
   try {
      auto decryptedSeed = wlt->getDecryptedValue(wlt->getEncryptedSeed());
      EXPECT_EQ(decryptedSeed, rawEntropy);
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException&) {}

   //lock, grab and check
   auto start = std::chrono::system_clock::now();
   try {
      auto lock = wlt->lockDecryptedContainer(passLbd);
      auto clearTextSeed = Seeds::ClearTextSeed::deserialize(
         wlt->getDecryptedValue(wlt->getEncryptedSeed()));
      auto seedBip32 = dynamic_cast<Seeds::ClearTextSeed_BIP32*>(
         clearTextSeed.get());
      EXPECT_EQ(seedBip32->getRawEntropy(), rawEntropy);
   } catch (const Encryption::DecryptedDataContainerException&) {
      ASSERT_TRUE(false);
   }
   auto end = std::chrono::system_clock::now();
   auto timeTaken = end-start;
   EXPECT_GE(timeTaken, 350ms);
   EXPECT_LE(timeTaken, 500ms);

   //grab without passphrase lambda again, should fail
   try {
      auto lock = wlt->lockDecryptedContainer({});
      auto decryptedSeed = wlt->getDecryptedValue(wlt->getEncryptedSeed());
      EXPECT_EQ(decryptedSeed, rawEntropy);
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException&) {}

   //shutdown wallet
   wlt.reset();

   //create WO
   Passphrase::SetNew passObj{1ms, 0, controlPass_};
   auto woFilename = AssetWallet::forkWatchingOnly(
      IO::ReadOnlyFileParams{filename, passObj.getUnlockFunc()}, passObj);

   //check it has no seed
   auto wo = AssetWallet::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{woFilename, controlLbd_});
   auto woWlt = std::dynamic_pointer_cast<AssetWallet_Single>(wo);

   ASSERT_NE(woWlt, nullptr);
   EXPECT_EQ(woWlt->getEncryptedSeed(), nullptr);

   //reload wallet
   ASSERT_EQ(wlt, nullptr);
   auto wltReload = AssetWallet::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{filename, controlLbd_});
   wlt = std::dynamic_pointer_cast<AssetWallet_Single>(wltReload);
   ASSERT_NE(wlt, nullptr);

   //check seed again
   try {
      auto lock = wlt->lockDecryptedContainer(passLbd);
      auto clearTextSeed = Seeds::ClearTextSeed::deserialize(
         wlt->getDecryptedValue(wlt->getEncryptedSeed()));
      auto seedBip32 = dynamic_cast<Seeds::ClearTextSeed_BIP32*>(clearTextSeed.get());
      EXPECT_EQ(seedBip32->getRawEntropy(), rawEntropy);
   } catch (const Encryption::DecryptedDataContainerException&) {
      ASSERT_TRUE(false);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, LockAndExtend_Legacy)
{
   //create wallet from priv key
   IO::CreateWalletParams params {
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("passphrase")},
      Passphrase::SetNew{1ms, 0, controlPass_},
      nullptr, 4
   };

   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory(rawEntropy,
         {}, Seeds::LegacyType::Armory200));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   auto passLbd = [](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { SecureBinaryData::fromString("passphrase"), true };
   };

   //derive private chain from root
   auto chaincode = BtcUtils::computeChainCode_ArmoryLegacy(rawEntropy);

   std::vector<std::pair<SecureBinaryData, SecureBinaryData>> pkeys;
   auto currentPrivKey = &rawEntropy;
   for (int i = 0; i < 10; i++) {
      auto privKey = Cryptography::ECDSA::computeChainedPrivateKey(
         *currentPrivKey, chaincode);
      auto pubkey = Cryptography::ECDSA::computePublicKey(privKey, true);
      pkeys.emplace_back(pubkey, privKey);
      currentPrivKey = &pkeys.back().second;
   }

   auto secondthread = [assetWlt, &pkeys, passLbd](void)->void
   {
      //lock wallet
      auto secondlock = assetWlt->lockDecryptedContainer(passLbd);

      //wallet should have 10 assets, last half with only pub keys
      auto accPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
      auto outerAcc = accPtr->getOuterAccount();
      ASSERT_EQ(outerAcc->getAssetCount(), 10U);

      //none of the new assets should have private keys
      for (unsigned i = 4; i < 10; i++) {
         auto asseti = outerAcc->getAssetForKey(i);
         ASSERT_FALSE(asseti->hasPrivateKey());
      }

      //grab last asset with a priv key
      auto asset3 = outerAcc->getAssetForKey(3);
      auto asset3_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset3);
      ASSERT_NE(asset3_single, nullptr);
      auto& privkey3 = assetWlt->getDecryptedValue(asset3_single->getPrivKey());

      //check privkey
      ASSERT_EQ(privkey3, pkeys[3].second);

      //check pubkey
      auto pubkey = asset3_single->getPubKey();
      ASSERT_EQ(pubkey->getCompressedKey(), pkeys[3].first);

      //extend private chain to 10 entries
      assetWlt->extendPrivateChainToIndex(assetWlt->getMainAccountID(), 9);

      //there should still be 10 assets
      ASSERT_EQ(outerAcc->getAssetCount(), 10U);

      //try to grab 10th private key
      auto asset9 = outerAcc->getAssetForKey(9);
      auto asset9_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset9);
      if (asset9_single == nullptr) {
         throw std::runtime_error("unexpected asset entry type");
      }
      auto& privkey9 = assetWlt->getDecryptedValue(asset9_single->getPrivKey());

      //check priv key
      ASSERT_EQ(privkey9, pkeys[9].second);
   };

   std::thread t2;

   {
      //grab lock
      auto firstlock = assetWlt->lockDecryptedContainer(passLbd);

      //start second thread
      t2 = std::thread(secondthread);

      //sleep for a second
      std::this_thread::sleep_for(1s);

      //make sure there are only 4 entries
      auto accPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
      auto outerAcc = accPtr->getOuterAccount();
      ASSERT_EQ(outerAcc->getAssetCount(), 4U);

      //grab 4th privkey
      auto asset3 = outerAcc->getAssetForKey(3);
      auto asset3_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset3);
      ASSERT_NE(asset3_single, nullptr);
      auto& privkey3 = assetWlt->getDecryptedValue(asset3_single->getPrivKey());

      //check privkey
      ASSERT_EQ(privkey3, pkeys[3].second);

      //check pubkey
      auto pubkey = asset3_single->getPubKey();
      ASSERT_EQ(pubkey->getCompressedKey(), pkeys[3].first);

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
      for (unsigned i = 4; i < 10; i++) {
         auto asseti = outerAcc->getAssetForKey(i);
         ASSERT_FALSE(asseti->hasPrivateKey());

         auto asseti_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asseti);
         ASSERT_NE(asseti_single, nullptr);
         auto pubkeyi = asseti_single->getPubKey();
         ASSERT_EQ(pubkeyi->getCompressedKey(), pkeys[i].first);
      }
   }

   if (t2.joinable()) {
      t2.join();
   }

   //delete wallet, reload and check private keys are on disk and valid
   auto wltID = assetWlt->getID();
   auto filename = assetWlt->getDbFilename();
   assetWlt.reset();

   auto newWallet = AssetWallet::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{filename, controlLbd_});
   auto wltSingle = std::dynamic_pointer_cast<AssetWallet_Single>(newWallet);

   ASSERT_NE(wltSingle, nullptr);

   auto accountPtr = wltSingle->getAccountForID(wltSingle->getMainAccountID());
   auto outerAcc = accountPtr->getOuterAccount();
   auto lastlock = wltSingle->lockDecryptedContainer(passLbd);
   std::set<BinaryData> ivs;
   for (unsigned i = 0; i < 10; i++) {
      auto asseti = outerAcc->getAssetForKey(i);
      auto asseti_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asseti);
      ASSERT_NE(asseti_single, nullptr);

      auto privKey = asseti_single->getPrivKey();
      auto iv = privKey->getIV();
      auto& asseti_privkey = wltSingle->getDecryptedValue(privKey);
      auto pubkey = asseti_single->getPubKey();

      ASSERT_EQ(asseti_privkey, pkeys[i].second);
      ASSERT_EQ(pubkey->getCompressedKey(), pkeys[i].first);
      ASSERT_FALSE(iv.empty());
      ASSERT_EQ(ivs.find(iv), ivs.end());
      ivs.emplace(iv);
   }
   ASSERT_EQ(ivs.size(), 10);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, LockAndExtend_BIP32)
{
   //create wallet from priv key
   IO::CreateWalletParams params {
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("passphrase")},
      Passphrase::SetNew{1ms, 0, controlPass_},
      nullptr, 4
   };

   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP32(rawEntropy,
         Seeds::SeedType::BIP32_Structured));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   auto passLbd = [](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { SecureBinaryData::fromString("passphrase"), true };
   };

   //derive private chain from root
   BIP32_Node seedNode;
   seedNode.initFromSeed(rawEntropy);
   seedNode.derivePrivate(0x8000002C);
   seedNode.derivePrivate(0x80000000);
   seedNode.derivePrivate(0x80000000);
   seedNode.derivePrivate(0);

   std::vector<std::pair<SecureBinaryData, SecureBinaryData>> pkeys;
   for (int i = 0; i < 10; i++) {
      auto node = seedNode;
      node.derivePrivate(i);
      pkeys.emplace_back(node.getPublicKey(), node.getPrivateKey());
   }

   auto secondthread = [assetWlt, &pkeys, passLbd](void)->void
   {
      //lock wallet
      auto secondlock = assetWlt->lockDecryptedContainer(passLbd);

      //wallet should have 10 assets, last half with only pub keys
      auto accPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
      auto outerAcc = accPtr->getOuterAccount();
      ASSERT_EQ(outerAcc->getAssetCount(), 10U);

      //none of the new assets should have private keys
      for (unsigned i = 4; i < 10; i++) {
         auto asseti = outerAcc->getAssetForKey(i);
         ASSERT_FALSE(asseti->hasPrivateKey());
      }

      //grab last asset with a priv key
      auto asset3 = outerAcc->getAssetForKey(3);
      auto asset3_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset3);
      ASSERT_NE(asset3_single, nullptr);
      auto& privkey3 = assetWlt->getDecryptedValue(asset3_single->getPrivKey());
      ASSERT_EQ(privkey3, pkeys[3].second);

      //check pubkey
      auto pubkey = asset3_single->getPubKey();
      ASSERT_EQ(pubkey->getCompressedKey(), pkeys[3].first);

      //extend private chain to 10 entries
      assetWlt->extendPrivateChainToIndex(assetWlt->getMainAccountID(), 9);

      //there should still be 10 assets
      ASSERT_EQ(outerAcc->getAssetCount(), 10U);

      //try to grab 10th private key
      auto asset9 = outerAcc->getAssetForKey(9);
      auto asset9_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset9);
      if (asset9_single == nullptr) {
         throw std::runtime_error("unexpected asset entry type");
      }
      auto& privkey9 = assetWlt->getDecryptedValue(asset9_single->getPrivKey());

      //check priv key
      ASSERT_EQ(privkey9, pkeys[9].second);
   };

   std::thread t2;

   {
      //grab lock
      auto firstlock = assetWlt->lockDecryptedContainer(passLbd);

      //start second thread
      t2 = std::thread(secondthread);

      //sleep for a second
      std::this_thread::sleep_for(1s);

      //make sure there are only 4 entries
      auto accPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
      auto outerAcc = accPtr->getOuterAccount();
      ASSERT_EQ(outerAcc->getAssetCount(), 4U);

      //grab 4th privkey
      auto asset3 = outerAcc->getAssetForKey(3);
      auto asset3_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset3);
      ASSERT_NE(asset3_single, nullptr);
      auto& privkey3 = assetWlt->getDecryptedValue(asset3_single->getPrivKey());
      ASSERT_EQ(privkey3, pkeys[3].second);

      //check pubkey
      auto pubkey = asset3_single->getPubKey();
      ASSERT_EQ(pubkey->getCompressedKey(), pkeys[3].first);

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
      for (unsigned i = 4; i < 10; i++) {
         auto asseti = outerAcc->getAssetForKey(i);
         ASSERT_FALSE(asseti->hasPrivateKey());

         auto asseti_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asseti);
         ASSERT_NE(asseti_single, nullptr);
         auto pubkeyi = asseti_single->getPubKey();
         ASSERT_EQ(pubkeyi->getCompressedKey(), pkeys[i].first);
      }
   }

   if (t2.joinable()) {
      t2.join();
   }

   //delete wallet, reload and check private keys are on disk and valid
   auto wltID = assetWlt->getID();
   auto filename = assetWlt->getDbFilename();
   assetWlt.reset();

   auto newWallet = AssetWallet::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{filename, controlLbd_});
   auto wltSingle = std::dynamic_pointer_cast<AssetWallet_Single>(newWallet);

   ASSERT_NE(wltSingle, nullptr);

   auto accountPtr = wltSingle->getAccountForID(wltSingle->getMainAccountID());
   auto outerAcc = accountPtr->getOuterAccount();
   auto lastlock = wltSingle->lockDecryptedContainer(passLbd);
   std::set<BinaryData> ivs;
   for (unsigned i = 0; i < 10; i++) {
      auto asseti = outerAcc->getAssetForKey(i);
      auto asseti_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asseti);
      ASSERT_NE(asseti_single, nullptr);

      auto privKey = asseti_single->getPrivKey();
      auto iv = privKey->getIV();
      auto& asseti_privkey = wltSingle->getDecryptedValue(privKey);
      auto pubkey = asseti_single->getPubKey();

      ASSERT_EQ(asseti_privkey, pkeys[i].second);
      ASSERT_EQ(pubkey->getCompressedKey(), pkeys[i].first);
      ASSERT_FALSE(iv.empty());
      ASSERT_EQ(ivs.find(iv), ivs.end());
      ivs.emplace(iv);
   }
   ASSERT_EQ(ivs.size(), 10);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, ControlPassphrase_Test)
{
   auto goodPassLbd = [](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { SecureBinaryData::fromString("control"), true };
   };

   auto checkSubDbValues = [](
      std::shared_ptr<AssetWallet> wlt,
      const std::string& dbName,
      std::map<BinaryData, BinaryData> dataMap)->bool
   {
      auto tx = wlt->beginSubDBTransaction(dbName, false);
      auto iter = tx->getIterator();
      while (iter->isValid()) {
         auto key = iter->key();
         auto mapIter = dataMap.find(key);
         if (mapIter != dataMap.end()) {
            if (mapIter->second == iter->value()) {
               dataMap.erase(mapIter);
            }
         }
         iter->advance();
      }
      return dataMap.empty();
   };

   //create wallet with control passphrase
   std::map<BinaryData, BinaryData> subDbData;
   for (unsigned i=0; i<20; i++) {
      subDbData.emplace(
         Cryptography::PRNG::generateRandomStrong(20),
         Cryptography::PRNG::generateRandomStrong(124)
      );
   }

   std::filesystem::path filename;
   std::set<BinaryData> addrSet;
   {
      IO::CreateWalletParams params {
         homedir_,
         Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("test")},
         Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory());
      auto assetWlt = AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      filename = assetWlt->getDbFilename();
      addrSet = assetWlt->getAddrHashSet();
      ASSERT_EQ(addrSet.size(), 12ULL);

      unsigned count = 0;
      auto badPassLbd = [&count](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         while (count++ < 3) {
            return { Cryptography::PRNG::generateRandomStrong(15), true };
         }
         return { {}, false };
      };

      //with bad pass
      try {
         assetWlt->addSubDB("test-subdb", badPassLbd);
         ASSERT_TRUE(false);
      } catch (const std::exception& e) {
         EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
      }

      //with good pass
      assetWlt->addSubDB("test-subdb", goodPassLbd);

      //set some subdb values
      {
         auto tx = assetWlt->beginSubDBTransaction("test-subdb", true);
         auto mapToWrite = subDbData;
         for (auto& keyVal : mapToWrite) {
            tx->insert(keyVal.first, keyVal.second);
         }
      }
      EXPECT_TRUE(checkSubDbValues(assetWlt, "test-subdb", subDbData));
   }

   {
      unsigned badPassCtr = 0;
      auto badPassLbd = [&badPassCtr](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         if (badPassCtr++ > 3) {
            return { {}, false };
         }
         return { Cryptography::PRNG::generateRandomStrong(20), true };
      };

      try {
         auto assetWlt = AssetWallet::loadMainWalletFromFile(
            IO::ReadOnlyFileParams{filename, badPassLbd});
         ASSERT_TRUE(false);
      } catch (const Encryption::DecryptedDataContainerException& e) {
         EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
      }

      try {
         auto assetWlt = AssetWallet::loadMainWalletFromFile(
            IO::ReadOnlyFileParams{filename, nullptr});
         ASSERT_TRUE(false);
      } catch (const Encryption::DecryptedDataContainerException& e) {
         EXPECT_EQ(e.what(), std::string{"empty passphrase lambda"});
         EXPECT_EQ(badPassCtr, 5U);
      }

      auto assetWlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{filename, goodPassLbd});
      auto loadedAddrSet = assetWlt->getAddrHashSet();

      //wallet values
      EXPECT_EQ(addrSet, loadedAddrSet);
      EXPECT_TRUE(checkSubDbValues(assetWlt, "test-subdb", subDbData));
   }

   //create WO copy with different passphrase
   {
      EncryptionKeyId wltPassID;
      try {
         //try with bad pass, should fail
         auto badPassLbd = [&wltPassID](const std::set<EncryptionKeyId>& ids)
         ->Passphrase::Result
         {
            if (!wltPassID.isValid()) {
               if (ids.size() != 1) {
                  throw std::range_error("");
               }
               wltPassID = *ids.begin();
               return { Cryptography::PRNG::generateRandomStrong(10), true };
            }
            return { {}, false };
         };
         auto woFilename = AssetWallet::forkWatchingOnly(
            IO::ReadOnlyFileParams{filename, badPassLbd},
            Passphrase::SetNew{nullptr});
         ASSERT_TRUE(false);
      } catch (const Encryption::DecryptedDataContainerException& e) {
         EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
      }

      //set different pass for WO fork
      auto passShift = [&wltPassID](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("control"), true };
      };
      auto woFilename = AssetWallet::forkWatchingOnly(
         IO::ReadOnlyFileParams{filename, passShift},
         Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("newwopass")}
      );

      //try to open WO with old pass, should fail
      try {
         unsigned ctr = 0;
         auto oldPassLbd = [&ctr](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         {
            while (ctr++ < 2) {
               return { Cryptography::PRNG::generateRandomStrong(18), true };
            }
            return { {}, false };
         };
         auto woWlt = AssetWallet::loadMainWalletFromFile(
            IO::ReadOnlyFileParams{woFilename, oldPassLbd});
      } catch (const Encryption::DecryptedDataContainerException& e) {
         EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
      }

      auto newPassLbd = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("newwopass"), true };
      };
      auto woWlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{woFilename, newPassLbd});
      auto loadedAddrSet = woWlt->getAddrHashSet();
      EXPECT_EQ(addrSet, loadedAddrSet);
   }

   /***********/

   //create wallet with no passphrase
   auto emptyPassLbd = [](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      throw std::runtime_error("shouldn't get here");
   };

   std::filesystem::path filename2;
   {
      IO::CreateWalletParams params{
         homedir_,
         Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("test")},
         Passphrase::SetNew{1ms, 0, {}},
         nullptr, 4
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_BIP32(
            Seeds::SeedType::BIP32_Structured));
      auto assetWlt = AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      filename2 = assetWlt->getDbFilename();
      addrSet = assetWlt->getAddrHashSet();
      ASSERT_EQ(addrSet.size(), 32ULL);

      //with good pass
      try {
         assetWlt->addSubDB("test-subdb", emptyPassLbd);
      } catch (const std::runtime_error&) {
         ASSERT_FALSE(true);
      }

      //set some subdb values
      {
         auto tx = assetWlt->beginSubDBTransaction("test-subdb", true);
         auto mapToWrite = subDbData;
         for (auto& keyVal : mapToWrite) {
            tx->insert(keyVal.first, keyVal.second);
         }
      }
      EXPECT_TRUE(checkSubDbValues(assetWlt, "test-subdb", subDbData));
   }

   //try to load, check passphrase lambda is never hit
   {
      auto assetWlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{filename2, emptyPassLbd});
      auto loadedAddrSet = assetWlt->getAddrHashSet();

      //wallet values
      EXPECT_EQ(addrSet, loadedAddrSet);
      EXPECT_TRUE(checkSubDbValues(assetWlt, "test-subdb", subDbData));
   }

   /***********/

   {
      //create WO copy (lambda that returns empty pass)
      auto woFilename = AssetWallet::forkWatchingOnly(
         IO::ReadOnlyFileParams{filename2, nullptr},
         Passphrase::SetNew{1ms, 0, {}}
      );

      //check WO wallet has no passphrase
      auto wltWO = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{woFilename, nullptr});
      auto loadedAddrSet = wltWO->getAddrHashSet();

      //wallet values
      EXPECT_EQ(addrSet, loadedAddrSet);

      //subdb won't be copied
      try {
         auto tx = wltWO->beginSubDBTransaction("test-subdb", false);
         ASSERT_FALSE(true);
      } catch (const IO::WalletInterfaceException& e) {
         EXPECT_EQ(e.what(), std::string{"invalid db name"});
      }

      //cleanup this WO
      wltWO.reset();
      std::filesystem::remove(woFilename);
   }

   /***********/

   {
      //create WO with different pass
      auto woFilename = AssetWallet::forkWatchingOnly(
         IO::ReadOnlyFileParams{filename2, nullptr},
         Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("newpass")}
      );

      unsigned count = 0;
      auto wrongPass = [&count](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         while (count++ < 5) {
            return { Cryptography::PRNG::generateRandomStrong(12), true };
         }
         return { {}, false };
      };

      try {
         auto wltWO = AssetWallet::loadMainWalletFromFile(
            IO::ReadOnlyFileParams{woFilename, wrongPass});
         ASSERT_TRUE(false);
      } catch (const Encryption::DecryptedDataContainerException& e) {
         EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
         EXPECT_EQ(count, 6U);
      }

      //check WO works with different pass
      auto newPass = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("newpass"), true };
      };
      auto wltWO = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{woFilename, newPass});
      auto loadedAddrSet = wltWO->getAddrHashSet();

      //wallet values
      EXPECT_EQ(addrSet, loadedAddrSet);

      //subdb won't be copied
      try {
         auto tx = wltWO->beginSubDBTransaction("test-subdb", false);
         ASSERT_FALSE(true);
      } catch (const IO::WalletInterfaceException& e) {
         EXPECT_EQ(e.what(), std::string{"invalid db name"});
      }

      //delete control passphrase

      //with wrong pass
      try {
         count = 0;
         wltWO->eraseControlPassphrase(wrongPass);
         ASSERT_TRUE(false);
      } catch (const Encryption::DecryptedDataContainerException& e) {
         EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
         EXPECT_EQ(count, 6U);
      }

      //with right pass
      wltWO->eraseControlPassphrase(newPass);

      //shutdown wallet
      wltWO.reset();

      //check pass is gone
      wltWO = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{woFilename, nullptr});
      loadedAddrSet = wltWO->getAddrHashSet();

      //wallet values
      EXPECT_EQ(addrSet, loadedAddrSet);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, SignPassphrase)
{
   //create wallet from priv key
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("test")},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4
   };

   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory(rawEntropy,
         {}, Seeds::LegacyType::Armory200));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   unsigned passphraseCount = 0;
   auto badPassphrase = [&passphraseCount](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      //pass wrong passphrase once then give up
      if (passphraseCount++ > 1) {
         return { {}, false };
      }
      return { SecureBinaryData::fromString("bad pass"), true };
   };

   //try to decrypt with wrong passphrase
   try {
      auto containerLock = assetWlt->lockDecryptedContainer(badPassphrase);
      auto accountPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
      auto outerAcc = accountPtr->getOuterAccount();
      auto asset = outerAcc->getAssetForKey(0);
      auto asset_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset);
      if (asset_single == nullptr) {
         throw std::runtime_error("unexpected asset entry type");
      }
      assetWlt->getDecryptedValue(asset_single->getPrivKey());
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException&) {
      EXPECT_EQ(passphraseCount, 3U);
   }

   passphraseCount = 0;
   auto goodPassphrase = [&passphraseCount](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      //pass wrong passphrase once then the right one
      if (passphraseCount++ > 1) {
         return { SecureBinaryData::fromString("test"), true };
      }
      return { SecureBinaryData::fromString("another bad pass"), true };
   };

   //try to decrypt with wrong passphrase then right passphrase
   try {
      auto containerLock = assetWlt->lockDecryptedContainer(goodPassphrase);
      auto accountPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
      auto outerAcc = accountPtr->getOuterAccount();
      auto asset = outerAcc->getAssetForKey(0);
      auto asset_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset);
      if (asset_single == nullptr) {
         throw std::runtime_error("unexpected asset entry type");
      }
      auto& privkey = assetWlt->getDecryptedValue(asset_single->getPrivKey());

      //make sure decrypted privkey is valid
      auto chaincode = BtcUtils::computeChainCode_ArmoryLegacy(rawEntropy);
      auto privkey_ex = Cryptography::ECDSA::computeChainedPrivateKey(
         rawEntropy, chaincode);
      ASSERT_EQ(privkey, privkey_ex);
   } catch (const Encryption::DecryptedDataContainerException&) {
      ASSERT_TRUE(false);
   }
   EXPECT_EQ(passphraseCount, 3U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, WrongPassphrase_BIP32)
{
   //create wallet from priv key
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("test")},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4
   };

   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP32(
         rawEntropy, Seeds::SeedType::BIP32_Structured));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   unsigned passphraseCount = 0;
   auto badPassphrase = [&passphraseCount](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      //pass wrong passphrase once then give up
      if (passphraseCount++ > 1) {
         return { {}, false };
      }
      return { SecureBinaryData::fromString("bad pass"), true };
   };

   //try to decrypt with wrong passphrase
   try {
      auto containerLock = assetWlt->lockDecryptedContainer(badPassphrase);
      auto accountPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
      auto outerAcc = accountPtr->getOuterAccount();
      auto asset = outerAcc->getAssetForKey(0);
      auto asset_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset);
      if (asset_single == nullptr) {
         throw std::runtime_error("unexpected asset entry type");
      }
      assetWlt->getDecryptedValue(asset_single->getPrivKey());
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException&) {
      EXPECT_EQ(passphraseCount, 3U);
   }

   passphraseCount = 0;
   auto goodPassphrase = [&passphraseCount](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      //pass wrong passphrase once then the right one
      if (passphraseCount++ > 2) {
         return { SecureBinaryData::fromString("test"), true };
      }
      return { SecureBinaryData::fromString("another bad pass"), true };
   };


   //try to decrypt with wrong passphrase then the right one
   try {
      auto containerLock = assetWlt->lockDecryptedContainer(goodPassphrase);
      auto accountPtr = assetWlt->getAccountForID(assetWlt->getMainAccountID());
      auto outerAcc = accountPtr->getOuterAccount();
      auto asset = outerAcc->getAssetForKey(0);
      auto asset_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset);
      if (asset_single == nullptr) {
         throw std::runtime_error("unexpected asset entry type");
      }
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
   } catch (const Encryption::DecryptedDataContainerException&) {
      ASSERT_TRUE(false);
   }
   EXPECT_EQ(passphraseCount, 4U);

   //add another account
   std::vector<unsigned> derPath2 {
      0x800050aa,
      0x8000c103,
   };

   Wallets::AddressAccountId newAccId;
   {
      auto lock = assetWlt->lockDecryptedContainer(
         [](const std::set<EncryptionKeyId>&)->Passphrase::Result
         { return {SecureBinaryData::fromString("test"), true}; }
      );
      auto accTypePtr = assetWlt->makeNewBip32AccTypeObject(derPath2);
      accTypePtr->setAddressLookup(10);
      accTypePtr->setNodes({0, 1});
      accTypePtr->setOuterAccountID(0);
      newAccId = assetWlt->createBIP32Account(accTypePtr);
   }
   auto accPtr = assetWlt->getAccountForID(newAccId);
   ASSERT_NE(accPtr, nullptr);

   //try and grab priv key with wrong passphrase
   passphraseCount = 0;
   try {
      auto containerLock = assetWlt->lockDecryptedContainer(badPassphrase);
      auto outerAcc = accPtr->getOuterAccount();
      auto asset = outerAcc->getAssetForKey(5);
      auto asset_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset);
      if (asset_single == nullptr) {
         throw std::runtime_error("unexpected asset entry type");
      }
      assetWlt->getDecryptedValue(asset_single->getPrivKey());
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException&) {
      EXPECT_EQ(passphraseCount, 3U);
   }

   //try to decrypt with wrong passphrase then the right one
   passphraseCount = 0;
   try {
      auto containerLock = assetWlt->lockDecryptedContainer(goodPassphrase);
      auto outerAcc = accPtr->getOuterAccount();
      auto asset = outerAcc->getAssetForKey(5);
      auto asset_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset);
      if (asset_single == nullptr) {
         throw std::runtime_error("unexpected asset entry type");
      }
      auto& privkey = assetWlt->getDecryptedValue(asset_single->getPrivKey());

      //make sure decrypted privkey is valid
      BIP32_Node node;
      node.initFromSeed(rawEntropy);
      for (auto& der : derPath2) {
         node.derivePrivate(der);
      }
      node.derivePrivate(0);
      node.derivePrivate(5);

      ASSERT_EQ(privkey, node.getPrivateKey());
   } catch (const Encryption::DecryptedDataContainerException&) {
      ASSERT_TRUE(false);
   }
   EXPECT_EQ(passphraseCount, 4U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, ChangePassphrase)
{
   //create wallet from priv key
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("test")},
      Passphrase::SetNew{1ms, 0, controlPass_},
      nullptr, 4
   };

   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory(rawEntropy,
         {}, Seeds::LegacyType::Armory200));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   auto chaincode = BtcUtils::computeChainCode_ArmoryLegacy(rawEntropy);
   auto privkey_ex = Cryptography::ECDSA::computeChainedPrivateKey(
      rawEntropy, chaincode);
   auto filename = assetWlt->getDbFilename();

   //grab all IVs and encrypted private keys
   std::vector<SecureBinaryData> ivVec;
   std::vector<SecureBinaryData> privateKeys;

   {
      auto assetWltEx = (AssetWalletEx*)assetWlt.get();
      auto decryptedDataEx =
         (DecryptedDataContainerEx*)assetWltEx->getDecryptedDataContainer().get();
      auto ivs = decryptedDataEx->getMasterKeyIVs();
      ivVec.insert(ivVec.end(), ivs.begin(), ivs.end());

      auto keys = decryptedDataEx->getMasterEncryptionKeys();
      privateKeys.insert(privateKeys.end(), keys.begin(), keys.end());
   }

   for (unsigned i = 0; i < 4; i++) {
      auto asseti = TestUtils::getMainAccountAssetForIndex(assetWlt, i);
      auto asseti_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asseti);
      ASSERT_NE(asseti_single, nullptr);

      ivVec.push_back(asseti_single->getPrivKey()->getIV());
      privateKeys.push_back(asseti_single->getPrivKey()->getCipherText());
   }

   //make sure the IVs are unique
   auto ivVecCopy = ivVec;
   while (ivVecCopy.size() > 0) {
      auto compare_iv = ivVecCopy.back();
      ivVecCopy.pop_back();

      for (auto& iv : ivVecCopy) {
         ASSERT_NE(iv, compare_iv);
      }
   }

   //change passphrase
   auto newPassphrase = SecureBinaryData::fromString("new pass");
   unsigned counter = 0;
   auto passphrasePrompt = [&counter](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      if (counter++ == 0) {
         return { SecureBinaryData::fromString("test"), true };
      } else {
         return { {}, false };
      }
   };

   auto newPassLbd = [&newPassphrase](void)->std::unique_ptr<Passphrase::Params>
   {
      return std::make_unique<Passphrase::Params>(1ms, 0, newPassphrase);
   };

   {
      //lock the wallet, passphrase change should fail
      auto lock = assetWlt->lockDecryptedContainer(passphrasePrompt);
      try {
         //change passphrase
         Passphrase::SetNew newPassObj{newPassLbd};
         assetWlt->changePrivateKeyPassphrase(passphrasePrompt, newPassObj);
         ASSERT_TRUE(false);
      } catch (const AlreadyLocked&) {}
   }

   {
      //try again without locking, should work
      try {
         //change passphrase
         Passphrase::SetNew newPassObj{newPassLbd};
         assetWlt->changePrivateKeyPassphrase(passphrasePrompt, newPassObj);
      } catch (const AlreadyLocked&) {
         ASSERT_TRUE(false);
      }
   }

   //try to decrypt with new passphrase
   auto newPassphrasePrompt = [&newPassphrase](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { newPassphrase, true };
   };

   {
      auto lock = assetWlt->lockDecryptedContainer(newPassphrasePrompt);

      auto asset0 = TestUtils::getMainAccountAssetForIndex(assetWlt, 0);
      auto asset0_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset0);
      ASSERT_NE(asset0_single, nullptr);

      auto& decryptedKey = assetWlt->getDecryptedValue(asset0_single->getPrivKey());
      ASSERT_EQ(decryptedKey, privkey_ex);
   }

   //close wallet, reload
   auto walletID = assetWlt->getID();
   assetWlt.reset();

   auto newWallet = AssetWallet::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{filename, controlLbd_});
   auto wltSingle = std::dynamic_pointer_cast<AssetWallet_Single>(newWallet);
   ASSERT_NE(wltSingle, nullptr);

   //grab all IVs and private keys again
   std::vector<SecureBinaryData> newIVs;
   std::vector<SecureBinaryData> newPrivKeys;

   {
      auto wltSingleEx = (AssetWalletEx*)wltSingle.get();
      auto decryptedDataEx =
         (DecryptedDataContainerEx*)wltSingleEx->getDecryptedDataContainer().get();
      auto ivs = decryptedDataEx->getMasterKeyIVs();
      newIVs.insert(newIVs.end(), ivs.begin(), ivs.end());

      auto keys = decryptedDataEx->getMasterEncryptionKeys();
      newPrivKeys.insert(newPrivKeys.end(), keys.begin(), keys.end());
   }

   for (unsigned i = 0; i < 4; i++) {
      auto asseti = TestUtils::getMainAccountAssetForIndex(wltSingle, i);
      auto asseti_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asseti);
      ASSERT_NE(asseti_single, nullptr);

      newIVs.push_back(asseti_single->getPrivKey()->getIV());
      newPrivKeys.push_back(asseti_single->getPrivKey()->getCipherText());
   }

   //check only the master key and iv have changed, and that the new iv does 
   //not match existing ones
   ASSERT_NE(newIVs[0], ivVec[0]);
   ASSERT_NE(newPrivKeys[0], privateKeys[0]);
   for (unsigned i = 1; i < 4; i++) {
      ASSERT_EQ(newIVs[i], ivVec[i]);
      ASSERT_EQ(newPrivKeys[i], privateKeys[i]);
      ASSERT_NE(newIVs[0], ivVec[i]);
   }

   auto asset0 = TestUtils::getMainAccountAssetForIndex(wltSingle, 0);
   auto asset0_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset0);
   ASSERT_NE(asset0_single, nullptr);

   {
      //try to decrypt with old passphrase, should fail
      auto lock = wltSingle->lockDecryptedContainer(passphrasePrompt);
      counter = 0;

      try {
         wltSingle->getDecryptedValue(asset0_single->getPrivKey());
         ASSERT_FALSE(true);
      } catch (...) {}
   }

   {
      //try to decrypt with new passphrase instead
      auto lock = wltSingle->lockDecryptedContainer(newPassphrasePrompt);
      auto& decryptedKey = wltSingle->getDecryptedValue(asset0_single->getPrivKey());
      ASSERT_EQ(decryptedKey, privkey_ex);
   }

   //check on file values
   auto passLbd = [](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { SecureBinaryData::fromString("control"), true };
   };

   IO::WalletDBInterface dbIface;
   dbIface.setupEnv(IO::ReadOnlyFileParams{filename, passLbd});
   std::string dbName;

   {
      auto tx = dbIface.beginReadTransaction(WALLETHEADER_DBNAME);
      BinaryWriter bwKey;
      bwKey.put_uint32_t(MAINWALLET_KEY);
      auto mainIdRef = tx->getDataRef(bwKey.getData());

      BinaryRefReader brr(mainIdRef);
      auto len = brr.get_var_int();
      auto mainIdBd = brr.get_BinaryData(len);
      dbName = std::string(mainIdBd.getCharPtr(), mainIdBd.getSize());
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
TEST_F(WalletsTest, ChangePassphrase_ChangeKDF)
{
   //create wallet
   auto passphrase = SecureBinaryData::fromString("test");
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, passphrase},
      Passphrase::SetNew{},
      nullptr, 4
   };

   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory(rawEntropy,
         {}, Seeds::LegacyType::Armory200));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);
   auto chaincode = BtcUtils::computeChainCode_ArmoryLegacy(rawEntropy);
   auto privkey_ex = Cryptography::ECDSA::computeChainedPrivateKey(
      rawEntropy, chaincode);

   auto asset0 = TestUtils::getMainAccountAssetForIndex(assetWlt, 0);
   auto asset0_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset0);
   ASSERT_NE(asset0_single, nullptr);
   asset0.reset();

   auto timeUnlock = [&privkey_ex](
      std::shared_ptr<AssetWallet_Single> wltPtr,
      std::shared_ptr<Assets::AssetEntry_Single> assetPtr,
      const SecureBinaryData& pass)->std::chrono::milliseconds
   {
      auto lock = wltPtr->lockDecryptedContainer(
         [&pass](const std::set<EncryptionKeyId>&)->Passphrase::Result
         { return { pass, true }; }
      );
      auto start = std::chrono::system_clock::now();
      auto& decryptedKey = wltPtr->getDecryptedValue(assetPtr->getPrivKey());
      auto end = std::chrono::system_clock::now();
      if (decryptedKey != privkey_ex) {
         return 0ms;
      }
      return std::chrono::duration_cast<std::chrono::milliseconds>(end-start);
   };

   auto failUnlock = [](
      std::shared_ptr<AssetWallet_Single> wltPtr,
      const SecureBinaryData& pass)->bool
   {
      auto root = wltPtr->getRoot();
      auto rootSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(root);

      int count = 0;
      auto lock = wltPtr->lockDecryptedContainer(
         [&pass, &count](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         {
            if (count++ > 0) {
               return { {}, false };
            }
            return { pass, true };
         }
      );
      try {
         wltPtr->getDecryptedValue(rootSingle->getPrivKey());
         return false;
      } catch (const Encryption::DecryptedDataContainerException& e) {
         return e.what() == std::string{"unlock request rejected"};
      }
   };

   //test wallet unlock time, should be <10ms
   auto elapsed = timeUnlock(assetWlt, asset0_single, passphrase);
   EXPECT_LE(elapsed, 10ms);

   //check wallet has 1 KDF on record
   std::shared_ptr<Encryption::KeyDerivationFunction_Romix> firstKdf;
   {
      auto assetWltEx = (AssetWalletEx*)assetWlt.get();
      auto decryptedDataEx = (DecryptedDataContainerEx*)
         assetWltEx->getDecryptedDataContainer().get();
      auto kdfs = decryptedDataEx->getKdfMap();
      EXPECT_EQ(kdfs.size(), 2);

      for (const auto& kdf : kdfs) {
         if (kdf.first == Encryption::passthroughKdfId) {
            continue;
         }

         auto kdfRomix = std::dynamic_pointer_cast<Encryption::KeyDerivationFunction_Romix>(
            kdf.second);
         ASSERT_NE(kdfRomix, nullptr);
         ASSERT_GE(kdfRomix->iterations(), 1);
         ASSERT_LE(kdfRomix->memTarget(), 65536);
         firstKdf = kdfRomix;
      }
   }

   //encrypt the wallet
   auto newPassphrase = SecureBinaryData::fromString("new pass");
   {
      Passphrase::SetNew newPass{500ms, 0, newPassphrase};
      assetWlt->changePrivateKeyPassphrase(
         [&passphrase](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         { return { passphrase, true }; },
         newPass
      );
   }

   //try to decrypt with first passphrase, should fail
   ASSERT_TRUE(failUnlock(assetWlt, passphrase));

   //test unlock with new passphrase and stronger kdf, should be >500ms
   elapsed = timeUnlock(assetWlt, asset0_single, newPassphrase);
   EXPECT_GE(elapsed, 500ms) << elapsed.count();

   //check wallet has 2 KDFs on record now
   std::shared_ptr<Encryption::KeyDerivationFunction_Romix> secondKdf;
   {
      auto assetWltEx = (AssetWalletEx*)assetWlt.get();
      auto decryptedDataEx = (DecryptedDataContainerEx*)
         assetWltEx->getDecryptedDataContainer().get();
      auto kdfs = decryptedDataEx->getKdfMap();
      EXPECT_EQ(kdfs.size(), 3);

      for (const auto& kdf : kdfs) {
         if (kdf.first == Encryption::passthroughKdfId) {
            continue;
         }

         auto kdfRomix = std::dynamic_pointer_cast<Encryption::KeyDerivationFunction_Romix>(
            kdf.second);
         ASSERT_NE(kdfRomix, nullptr);
         if (kdfRomix->isSame(firstKdf.get())) {
            continue;
         }

         //new kdf should be bigger
         ASSERT_NE(kdfRomix, nullptr);
         ASSERT_GE(kdfRomix->iterations(), 1);
         ASSERT_GE(kdfRomix->memTarget(), 8388608);
         secondKdf = kdfRomix;
      }
   }

   //shutdown wallet, reopen, check new kdf is there
   auto walletId = assetWlt->getID();
   auto filename = assetWlt->getDbFilename();
   asset0_single.reset();
   assetWlt.reset();

   auto reloadWlt = AssetWallet::loadMainWalletFromFile({filename, {}});
   ASSERT_EQ(walletId, reloadWlt->getID());
   auto reloadAssetWlt = std::dynamic_pointer_cast<AssetWallet_Single>(reloadWlt);
   ASSERT_NE(reloadAssetWlt, nullptr);

   asset0 = TestUtils::getMainAccountAssetForIndex(reloadAssetWlt, 0);
   auto asset0_reloaded = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset0);
   ASSERT_NE(asset0_reloaded, nullptr);
   asset0.reset();

   //try to decrypt with first passphrase, should fail
   ASSERT_TRUE(failUnlock(reloadAssetWlt, passphrase));

   //test unlock with new passphrase and stronger kdf, should be >500ms
   elapsed = timeUnlock(reloadAssetWlt, asset0_reloaded, newPassphrase);
   EXPECT_GE(elapsed, 500ms) << elapsed.count();

   //add another passphrase, reuse kdf
   auto thirdPass = SecureBinaryData::fromString("thrid passphrase");
   {
      Passphrase::SetNew pass3{
         [&thirdPass](void)->std::unique_ptr<Passphrase::Params>
         { return std::make_unique<Passphrase::Params>(thirdPass, true); }
      };
      auto start = std::chrono::system_clock::now();
      reloadAssetWlt->addPrivateKeyPassphrase(
         [&newPassphrase](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result{
            std::string passStr{newPassphrase.getCharPtr(), newPassphrase.getSize()};
            return { newPassphrase, true };
         }, pass3
      );
      auto end = std::chrono::system_clock::now();
      auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end-start);

      //one pass to unlock, one pass to encrypt
      EXPECT_GE(dur, 1000ms) << dur.count();
      EXPECT_LE(dur, 1500ms) << dur.count();
   }

   //unlock with 3rd pass
   elapsed = timeUnlock(reloadAssetWlt, asset0_reloaded, thirdPass);
   EXPECT_GE(elapsed, 500ms) << elapsed.count();
   EXPECT_LE(elapsed, 750ms) << elapsed.count();

   //add a 4th passphrase, same unlock target as current kdf
   auto fourthPass = SecureBinaryData::fromString("4th passphrase");
   {
      Passphrase::SetNew pass4{
         [&fourthPass](void)->std::unique_ptr<Passphrase::Params>
         { return std::make_unique<Passphrase::Params>(500ms, 0, fourthPass); }
      };
      auto start = std::chrono::system_clock::now();
      reloadAssetWlt->addPrivateKeyPassphrase(
         [&newPassphrase](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         { return { newPassphrase, true }; },
         pass4
      );
      auto end = std::chrono::system_clock::now();
      auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end-start);

      //one pass to unlock, one pass to compare kdfs, one pass to encrypt
      EXPECT_GE(dur, 1500ms) << dur.count();
      EXPECT_LE(dur, 1950ms) << dur.count();
   }

   //check wallet still only has 2 KDFs on record
   {
      auto assetWltEx = (AssetWalletEx*)reloadAssetWlt.get();
      auto decryptedDataEx = (DecryptedDataContainerEx*)
         assetWltEx->getDecryptedDataContainer().get();
      auto kdfs = decryptedDataEx->getKdfMap();
      EXPECT_EQ(kdfs.size(), 3);

      for (const auto& kdf : kdfs) {
         if (kdf.first == Encryption::passthroughKdfId) {
            continue;
         }

         if (kdf.second->isSame(firstKdf.get()) ||
            kdf.second->isSame(secondKdf.get())) {
            continue;
         }
         ASSERT_TRUE(false);
      }
   }

   //add 5th pass, with new KDF
   auto fifthPass = SecureBinaryData::fromString("passphrase 5");
   {
      Passphrase::SetNew pass5{
         [&fifthPass](void)->std::unique_ptr<Passphrase::Params>
         { return std::make_unique<Passphrase::Params>(1500ms, 0, fifthPass); }
      };
      auto start = std::chrono::system_clock::now();
      reloadAssetWlt->addPrivateKeyPassphrase(
         [&fourthPass](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         { return { fourthPass, true }; },
         pass5
      );
      auto end = std::chrono::system_clock::now();
      auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end-start);

      //one pass to unlock, one pass to compare kdfs, one pass to encrypt
      EXPECT_GE(dur, 3500ms) << dur.count();
      EXPECT_LE(dur, 4800ms) << dur.count();
   }

   //check wallet has 3 KDFs now
   std::shared_ptr<Encryption::KeyDerivationFunction_Romix> thirdKdf;
   {
      auto assetWltEx = (AssetWalletEx*)reloadAssetWlt.get();
      auto decryptedDataEx = (DecryptedDataContainerEx*)
         assetWltEx->getDecryptedDataContainer().get();
      auto kdfs = decryptedDataEx->getKdfMap();
      EXPECT_EQ(kdfs.size(), 4);

      for (const auto& kdf : kdfs) {
         if (kdf.first == Encryption::passthroughKdfId) {
            continue;
         }

         if (kdf.second->isSame(firstKdf.get()) ||
            kdf.second->isSame(secondKdf.get())) {
            continue;
         }

         auto kdfRomix = std::dynamic_pointer_cast<Encryption::KeyDerivationFunction_Romix>(
            kdf.second);
         ASSERT_NE(kdfRomix, nullptr);

         //new kdf should be bigger
         ASSERT_NE(kdfRomix, nullptr);
         ASSERT_GE(kdfRomix->iterations(), 3);
         ASSERT_GE(kdfRomix->memTarget(), 33554432);
         thirdKdf = kdfRomix;
      }
   }
   auto baseline = 0ms;
   if (thirdKdf->getId() < secondKdf->getId()) {
      auto now = std::chrono::system_clock::now();
      thirdKdf->deriveKey(SecureBinaryData::fromString("test key"));
      baseline = std::chrono::duration_cast<std::chrono::milliseconds>(
         std::chrono::system_clock::now() - now);
   }

   //unlock with 5th pass
   elapsed = timeUnlock(reloadAssetWlt, asset0_reloaded, fifthPass);
   EXPECT_GE(elapsed, 1500ms) << elapsed.count();

   //change pass 2, reuse kdf
   auto newPass2 = SecureBinaryData::fromString("new pass2");
   {
      Passphrase::SetNew nPass2{
         [&newPass2](void)->std::unique_ptr<Passphrase::Params>
         { return std::make_unique<Passphrase::Params>(newPass2, true); }
      };
      auto start = std::chrono::system_clock::now();
      reloadAssetWlt->changePrivateKeyPassphrase(
         [&newPassphrase](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         { return { newPassphrase, true }; },
         nPass2
      );
      auto end = std::chrono::system_clock::now();
      auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end-start);
      EXPECT_GE(dur, baseline+1000ms) << dur.count();
      EXPECT_LE(dur, baseline+1300ms) << dur.count();
   }

   //fail unlock with old pass 2
   ASSERT_TRUE(failUnlock(reloadAssetWlt, newPassphrase));

   //unlock with new pass 2
   elapsed = timeUnlock(reloadAssetWlt, asset0_reloaded, newPass2);
   EXPECT_GE(elapsed, baseline+500ms) << elapsed.count();
   EXPECT_LE(elapsed, baseline+800ms) << elapsed.count();

   //change pass 3, change kdf
   auto newPass3 = SecureBinaryData::fromString("new pass3");
   {
      Passphrase::SetNew nPass3{
         [&newPass3](void)->std::unique_ptr<Passphrase::Params>
         { return std::make_unique<Passphrase::Params>(1500ms, 0, newPass3); }
      };
      auto start = std::chrono::system_clock::now();
      reloadAssetWlt->changePrivateKeyPassphrase(
         [&thirdPass](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         { return { thirdPass, true }; },
         nPass3
      );
      auto end = std::chrono::system_clock::now();
      auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end-start);
      EXPECT_GE(dur, 3500ms+baseline) << dur.count();
      EXPECT_LE(dur, 5000ms+baseline) << dur.count();
   }

   //fail unlock with old pass 3
   ASSERT_TRUE(failUnlock(reloadAssetWlt, thirdPass));

   //unlock with new pass 3
   auto extraBaseline = (baseline == 0ms) ? 600ms : 0ms;
   elapsed = timeUnlock(reloadAssetWlt, asset0_reloaded, newPass3);
   EXPECT_GE(elapsed, extraBaseline+1500ms) << elapsed.count();
   EXPECT_LE(elapsed, extraBaseline+2000ms) << elapsed.count();

   //check wallet has 3 KDFs still
   {
      auto assetWltEx = (AssetWalletEx*)reloadAssetWlt.get();
      auto decryptedDataEx = (DecryptedDataContainerEx*)
         assetWltEx->getDecryptedDataContainer().get();
      auto kdfs = decryptedDataEx->getKdfMap();
      EXPECT_EQ(kdfs.size(), 4);

      for (const auto& kdf : kdfs) {
         if (kdf.first == Encryption::passthroughKdfId) {
            continue;
         }

         if (kdf.second->isSame(firstKdf.get()) ||
            kdf.second->isSame(secondKdf.get()) ||
            kdf.second->isSame(thirdKdf.get())) {
            continue;
         }

         ASSERT_TRUE(false);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, ChangePassphrase_FromUnencryptedWallet)
{
   //create wallet from priv key
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{},
      Passphrase::SetNew{100ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4
   };

   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory(rawEntropy,
         {}, Seeds::LegacyType::Armory200));
   auto seed135 = dynamic_cast<Seeds::ClearTextSeed_Armory*>(seed.get());
   auto clearRoot = seed135->getRoot();
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   auto chaincode = BtcUtils::computeChainCode_ArmoryLegacy(rawEntropy);
   auto privkey_ex = Cryptography::ECDSA::computeChainedPrivateKey(
      rawEntropy, chaincode);
   auto filename = assetWlt->getDbFilename();
   auto newPass = SecureBinaryData::fromString("newpass");

   auto asset0 = TestUtils::getMainAccountAssetForIndex(assetWlt, 0);
   auto asset0_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset0);
   ASSERT_NE(asset0_single, nullptr);
   asset0.reset();

   auto root = assetWlt->getRoot();
   auto root_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(root);
   ASSERT_NE(root_single, nullptr);
   root.reset();

   auto dontTriggerLbd = [](const std::set<EncryptionKeyId>&)->Passphrase::Result
   {
      throw std::runtime_error("should not get this far");
   };

   //check the wallet has no passphrase
   {
      //decrypt the root, should not hit the lambda
      auto lock = assetWlt->lockDecryptedContainer(dontTriggerLbd);
      auto start = std::chrono::system_clock::now();
      auto& decryptedKey = assetWlt->getDecryptedValue(root_single->getPrivKey());
      auto end = std::chrono::system_clock::now();
      ASSERT_EQ(decryptedKey, clearRoot);
      EXPECT_LE(end-start, 50ms);
   }

   {
      //decrypt an asset, should not hit the lambda
      auto lock = assetWlt->lockDecryptedContainer(dontTriggerLbd);
      auto start = std::chrono::system_clock::now();
      auto& decryptedKey = assetWlt->getDecryptedValue(asset0_single->getPrivKey());
      auto end = std::chrono::system_clock::now();
      ASSERT_EQ(decryptedKey, privkey_ex);
      EXPECT_LE(end-start, 50ms);
   }

   //try to set new passphrase with reused kdf
   //should fail since no encryption is applied yet
   auto reuseKdfLbd = [&newPass](void)->std::unique_ptr<Passphrase::Params>
   {
      return std::make_unique<Passphrase::Params>(newPass, true);
   };

   try {
      Passphrase::SetNew newPassObj{reuseKdfLbd};
      assetWlt->changePrivateKeyPassphrase({}, newPassObj);
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException& e) {
      EXPECT_EQ(e.what(), std::string{"target key has no kdf"});
   }


   //try to add passhrase to an unencrypted wallet, should fail
   auto changePassLbd = [&newPass](void)->std::unique_ptr<Passphrase::Params>
   {
      return std::make_unique<Passphrase::Params>(1200ms, 0, newPass);
   };
   auto reusePassLbd = [&newPass](void)->std::unique_ptr<Passphrase::Params>
   {
      return std::make_unique<Passphrase::Params>(newPass, true);
   };

   try {
      Passphrase::SetNew newPassObj{changePassLbd};
      assetWlt->addPrivateKeyPassphrase({}, newPassObj);
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException& e) {
      EXPECT_EQ(e.what(), std::string{"cannot add passphrase to unencrypted wallet"});
   }

   //encrypt with new pass
   {
      auto start = std::chrono::system_clock::now();
      Passphrase::SetNew newPassObj{changePassLbd};
      assetWlt->changePrivateKeyPassphrase({}, newPassObj);
      auto end = std::chrono::system_clock::now();
      auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

      //should compute the kdf twice:
      // once to test params for comparison purposes (~75% of target unlock)
      // once to apply it to the key
      EXPECT_GE(diff, 2100ms) << diff.count();
      EXPECT_LE(diff, 2600ms) << diff.count();
   }

   //check the wallet can't be decrypted without a passphrase anymore
   auto failUnlockLbd = [](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result{ return { {}, false }; };

   try {
      auto lock = assetWlt->lockDecryptedContainer(failUnlockLbd);
      assetWlt->getDecryptedValue(asset0_single->getPrivKey());
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException& e) {
      EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
   }

   try {
      auto lock = assetWlt->lockDecryptedContainer(failUnlockLbd);
      assetWlt->getDecryptedValue(root_single->getPrivKey());
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException& e) {
      EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
   }

   //check the new pass works
   auto newPassLbd = [&newPass](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { newPass, true };
   };

   std::chrono::milliseconds baseline;
   {
      auto start = std::chrono::system_clock::now();
      auto lock = assetWlt->lockDecryptedContainer(newPassLbd);
      auto& decryptedKey = assetWlt->getDecryptedValue(asset0_single->getPrivKey());
      auto end = std::chrono::system_clock::now();
      ASSERT_EQ(decryptedKey, privkey_ex);
      baseline = std::chrono::duration_cast<std::chrono::milliseconds>(end - start) - 20ms;

      EXPECT_GE(baseline, 1160ms) << baseline.count();
      EXPECT_LE(baseline, 1600ms) << baseline.count();
      std::cout << "baseline is: " << baseline.count() << "ms" << std::endl;
   }

   {
      auto start = std::chrono::system_clock::now();
      auto lock = assetWlt->lockDecryptedContainer(newPassLbd);
      auto& decryptedKey = assetWlt->getDecryptedValue(root_single->getPrivKey());
      auto end = std::chrono::system_clock::now();
      ASSERT_EQ(decryptedKey, clearRoot);
      auto diff = end - start;
      EXPECT_GE(diff, baseline) << diff.count();
      EXPECT_LE(diff, baseline*1.2) << diff.count();
   }

   //try to add the same passphrase
   try {
      Passphrase::SetNew newPassObj{reusePassLbd};
      assetWlt->addPrivateKeyPassphrase(newPassLbd, newPassObj);
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException& e) {
      EXPECT_EQ(e.what(), std::string{"cipher data already present in encryption key"});
   }

   //check pass still works
   {
      auto lock = assetWlt->lockDecryptedContainer(newPassLbd);
      auto& decryptedKey = assetWlt->getDecryptedValue(asset0_single->getPrivKey());
      ASSERT_EQ(decryptedKey, privkey_ex);
   }

   //add another passphrase
   auto newPass2 = SecureBinaryData::fromString("another pass");
   auto changePass2Lbd = [&newPass2](void)->std::unique_ptr<Passphrase::Params>
   {
      //set to reuse existing kdf
      return std::make_unique<Passphrase::Params>(newPass2, true);
   };
   {
      auto start = std::chrono::system_clock::now();
      Passphrase::SetNew newPassObj{changePass2Lbd};
      assetWlt->addPrivateKeyPassphrase(newPassLbd, newPassObj);
      auto end = std::chrono::system_clock::now();
      auto diff = end - start;

      //2 KDF extensions: one for current key, one for new key
      EXPECT_GE(diff, baseline*2) << diff.count();
      EXPECT_LE(diff, baseline*2.4) << diff.count();
   }

   //check old pass works
   {
      auto start = std::chrono::system_clock::now();
      auto lock = assetWlt->lockDecryptedContainer(newPassLbd);
      auto decryptedKey = assetWlt->getDecryptedValue(asset0_single->getPrivKey());
      ASSERT_EQ(decryptedKey, privkey_ex);
      decryptedKey = assetWlt->getDecryptedValue(root_single->getPrivKey());
      ASSERT_EQ(decryptedKey, clearRoot);
      auto end = std::chrono::system_clock::now();
      auto diff = end - start;

      //only need to extend passprhase once for both decryption
      EXPECT_GE(diff, baseline) << diff.count();
      EXPECT_LE(diff, baseline*1.2) << diff.count();
   }

   //check new pass works
   auto newPass2Lbd = [&newPass2](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { newPass2, true };
   };

   {
      auto start = std::chrono::system_clock::now();
      auto lock = assetWlt->lockDecryptedContainer(newPass2Lbd);
      auto decryptedKey = assetWlt->getDecryptedValue(asset0_single->getPrivKey());
      ASSERT_EQ(decryptedKey, privkey_ex);
      decryptedKey = assetWlt->getDecryptedValue(root_single->getPrivKey());
      ASSERT_EQ(decryptedKey, clearRoot);
      auto end = std::chrono::system_clock::now();
      auto diff = end - start;
      EXPECT_GE(diff, baseline) << diff.count();
      EXPECT_LE(diff, baseline*1.2) << diff.count();
   }

   //reload the wallet
   assetWlt.reset();
   asset0_single.reset();
   root_single.reset();
   auto wlt2 = AssetWallet::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{filename, [](
         const std::set<EncryptionKeyId>&)->Passphrase::Result {
            return { SecureBinaryData::fromString("control"), true };
         }
   });
   auto assetWlt2 = std::dynamic_pointer_cast<AssetWallet_Single>(wlt2);
   ASSERT_NE(assetWlt2, nullptr);
   wlt2.reset();

   auto asset1 = TestUtils::getMainAccountAssetForIndex(assetWlt2, 0);
   auto asset1_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset1);
   ASSERT_NE(asset1_single, nullptr);
   asset1.reset();

   auto root1 = assetWlt2->getRoot();
   auto root1_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(root1);
   ASSERT_NE(root1_single, nullptr);
   root1.reset();

   //check old pass works
   {
      auto start = std::chrono::system_clock::now();
      auto lock = assetWlt2->lockDecryptedContainer(newPassLbd);
      auto decryptedKey = assetWlt2->getDecryptedValue(asset1_single->getPrivKey());
      ASSERT_EQ(decryptedKey, privkey_ex);
      decryptedKey = assetWlt2->getDecryptedValue(root1_single->getPrivKey());
      ASSERT_EQ(decryptedKey, clearRoot);
      auto end = std::chrono::system_clock::now();
      auto diff = end - start;
      EXPECT_GE(diff, baseline) << diff.count();
      EXPECT_LE(diff, baseline*1.2) << diff.count();
   }

   {
      auto start = std::chrono::system_clock::now();
      auto lock = assetWlt2->lockDecryptedContainer(newPass2Lbd);
      auto decryptedKey = assetWlt2->getDecryptedValue(asset1_single->getPrivKey());
      ASSERT_EQ(decryptedKey, privkey_ex);
      decryptedKey = assetWlt2->getDecryptedValue(root1_single->getPrivKey());
      ASSERT_EQ(decryptedKey, clearRoot);
      auto end = std::chrono::system_clock::now();
      auto diff = end - start;
      EXPECT_GE(diff, baseline) << diff.count();
      EXPECT_LE(diff, baseline*1.2) << diff.count();
   }

   //delete old pass
   assetWlt2->erasePrivateKeyPassphrase(newPassLbd);

   //check old pass fails
   unsigned counter = 0;
   auto newPassLbdFail = [&counter, &newPass](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      while (counter++ < 4) {
         return { newPass, true };
      }
      return { {}, false };
   };

   try {
      auto lock = assetWlt2->lockDecryptedContainer(newPassLbdFail);
      assetWlt2->getDecryptedValue(asset1_single->getPrivKey());
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException& e) {
      EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
      EXPECT_EQ(counter, 5U);
   }

   counter=0;
   try {
      auto lock = assetWlt2->lockDecryptedContainer(newPassLbdFail);
      assetWlt2->getDecryptedValue(root1_single->getPrivKey());
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException& e) {
      EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
      EXPECT_EQ(counter, 5U);
   }

   //check new pass works
   {
      auto start = std::chrono::system_clock::now();
      auto lock = assetWlt2->lockDecryptedContainer(newPass2Lbd);
      auto& decryptedKey = assetWlt2->getDecryptedValue(asset1_single->getPrivKey());
      auto end = std::chrono::system_clock::now();
      ASSERT_EQ(decryptedKey, privkey_ex);
      auto diff = end - start;
      EXPECT_GE(diff, baseline) << diff.count();
      EXPECT_LE(diff, baseline*1.2) << diff.count();
   }

   {
      auto start = std::chrono::system_clock::now();
      auto lock = assetWlt2->lockDecryptedContainer(newPass2Lbd);
      auto& decryptedKey = assetWlt2->getDecryptedValue(root1_single->getPrivKey());
      auto end = std::chrono::system_clock::now();
      ASSERT_EQ(decryptedKey, clearRoot);
      auto diff = end - start;
      EXPECT_GE(diff, baseline) << diff.count();
      EXPECT_LE(diff, baseline*1.2) << diff.count();
   }

   //delete new pass
   assetWlt2->erasePrivateKeyPassphrase(newPass2Lbd);

   counter = 0;
   auto emptyPassLbd2 = [&counter](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      ++counter;
      return { {}, true };
   };

   //check wallet is unencrypted
   {
      auto start = std::chrono::system_clock::now();
      auto lock = assetWlt2->lockDecryptedContainer(emptyPassLbd2);
      auto& decryptedKey = assetWlt2->getDecryptedValue(asset1_single->getPrivKey());
      auto end = std::chrono::system_clock::now();

      ASSERT_EQ(decryptedKey, privkey_ex);
      EXPECT_EQ(counter, 0U);
      EXPECT_LE(end-start, 50ms);
   }

   {
      auto start = std::chrono::system_clock::now();
      auto lock = assetWlt2->lockDecryptedContainer(emptyPassLbd2);
      auto& decryptedKey = assetWlt2->getDecryptedValue(root1_single->getPrivKey());
      auto end = std::chrono::system_clock::now();

      ASSERT_EQ(decryptedKey, clearRoot);
      EXPECT_EQ(counter, 0U);
      EXPECT_LE(end-start, 50ms);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, ChangePassphrase_SeedBIP32)
{
   //create wallet from priv key
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1200ms, 0, {}},
      Passphrase::SetNew{100ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4
   };

   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP32(rawEntropy,
         Seeds::SeedType::BIP32_Virgin));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);
   auto newPass = SecureBinaryData::fromString("newpass");
   auto filename = assetWlt->getDbFilename();

   //check the wallet has no passphrase
   auto failPassLbd = [](const std::set<EncryptionKeyId>&)->
   Passphrase::Result{ throw std::runtime_error("should not get this far"); };

   {
      //decrypt the root, should not hit the lambda
      auto lock = assetWlt->lockDecryptedContainer(failPassLbd);
      auto start = std::chrono::system_clock::now();
      auto clearTextSeed = Seeds::ClearTextSeed::deserialize(
         assetWlt->getDecryptedValue(assetWlt->getEncryptedSeed()));
      auto seedBip32 = dynamic_cast<Seeds::ClearTextSeed_BIP32*>(clearTextSeed.get());
      EXPECT_EQ(seedBip32->getRawEntropy(), rawEntropy);
      auto end = std::chrono::system_clock::now();
      EXPECT_LE(end-start, 50ms);
   }

   //try to add passhrase to an unencrypted wallet, should fail
   auto changePassLbd = [&newPass](void)->std::unique_ptr<Passphrase::Params>
   {
      return std::make_unique<Passphrase::Params>(1200ms, 0, newPass);
   };

   try {
      Passphrase::SetNew newPassObj{changePassLbd};
      assetWlt->addPrivateKeyPassphrase({}, newPassObj);
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException& e) {
      EXPECT_EQ(e.what(), std::string{"cannot add passphrase to unencrypted wallet"});
   }

   //encrypt with new pass
   {
      auto start = std::chrono::system_clock::now();
      Passphrase::SetNew newPassObj{changePassLbd};
      assetWlt->changePrivateKeyPassphrase({}, newPassObj);
      auto end = std::chrono::system_clock::now();
      auto diff = end - start;
      EXPECT_GE(diff, 2100ms) << diff.count();
      EXPECT_LE(diff, 2600ms) << diff.count();
   }

   //check the wallet can't be decrypted without a passphrase anymore
   auto rejectPassLbd = [](const std::set<EncryptionKeyId>&)->
   Passphrase::Result{ return { {}, false }; };

   try {
      auto lock = assetWlt->lockDecryptedContainer(rejectPassLbd);
      assetWlt->getDecryptedValue(assetWlt->getEncryptedSeed());
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException& e) {
      EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
   }

   //check the new pass works
   auto newPassLbd = [&newPass]
      (const std::set<EncryptionKeyId>&)->Passphrase::Result
   {
      return { newPass, true };
   };

   {
      auto start = std::chrono::system_clock::now();
      auto lock = assetWlt->lockDecryptedContainer(newPassLbd);
      auto clearTextSeed = Seeds::ClearTextSeed::deserialize(
         assetWlt->getDecryptedValue(assetWlt->getEncryptedSeed()));
      auto seedBip32 = dynamic_cast<Seeds::ClearTextSeed_BIP32*>(clearTextSeed.get());
      EXPECT_EQ(seedBip32->getRawEntropy(), rawEntropy);
      auto end = std::chrono::system_clock::now();
      auto diff = end - start;
      EXPECT_GE(diff, 1200ms) << diff.count();
      EXPECT_LE(diff, 1320ms) << diff.count();
   }

   //reload the wallet
   assetWlt.reset();
   auto wlt = AssetWallet::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{filename, [](
         const std::set<EncryptionKeyId>&)->Passphrase::Result {
            return { SecureBinaryData::fromString("control"), true };
         }
   });
   auto assetWlt2 = std::dynamic_pointer_cast<AssetWallet_Single>(wlt);
   ASSERT_NE(assetWlt2, nullptr);
   wlt.reset();

   //check the wallet can't be decrypted without a passphrase
   try {
      auto lock = assetWlt2->lockDecryptedContainer(rejectPassLbd);
      assetWlt2->getDecryptedValue(assetWlt2->getEncryptedSeed());
      ASSERT_TRUE(false);
   } catch (const Encryption::DecryptedDataContainerException& e) {
      EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
   }

   //check the new pass works
   {
      auto start = std::chrono::system_clock::now();
      auto lock = assetWlt2->lockDecryptedContainer(newPassLbd);
      auto clearTextSeed = Seeds::ClearTextSeed::deserialize(
         assetWlt2->getDecryptedValue(assetWlt2->getEncryptedSeed()));
      auto seedBip32 = dynamic_cast<Seeds::ClearTextSeed_BIP32*>(clearTextSeed.get());
      EXPECT_EQ(seedBip32->getRawEntropy(), rawEntropy);
      auto end = std::chrono::system_clock::now();
      auto diff = end - start;
      EXPECT_GE(diff, 1200ms) << diff.count();
      EXPECT_LE(diff, 1320ms) << diff.count();
   }

   //delete the new pass
   assetWlt2->erasePrivateKeyPassphrase(newPassLbd);

   //check wallet is unencrypted
   unsigned counter = 0;
   auto emptyPassLbd = [&counter](const std::set<EncryptionKeyId>&)\
   ->Passphrase::Result
   {
      ++counter;
      return { {}, true };
   };

   {
      auto start = std::chrono::system_clock::now();
      auto lock = assetWlt2->lockDecryptedContainer(emptyPassLbd);
      auto clearTextSeed = Seeds::ClearTextSeed::deserialize(
         assetWlt2->getDecryptedValue(assetWlt2->getEncryptedSeed()));
      auto seedBip32 = dynamic_cast<Seeds::ClearTextSeed_BIP32*>(clearTextSeed.get());
      EXPECT_EQ(seedBip32->getRawEntropy(), rawEntropy);
      auto end = std::chrono::system_clock::now();
      EXPECT_EQ(counter, 0U);
      EXPECT_LE(end-start, 50ms);
   }

   //reload wallet one last time
   assetWlt2.reset();
   auto wlt2 = AssetWallet::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{filename, [](
         const std::set<EncryptionKeyId>&)->Passphrase::Result {
            return { SecureBinaryData::fromString("control"), true };
         }
   });
   auto assetWlt3 = std::dynamic_pointer_cast<AssetWallet_Single>(wlt2);
   ASSERT_NE(assetWlt3, nullptr);
   wlt2.reset();

   //check wallet is unencrypted
   {
      auto start = std::chrono::system_clock::now();
      auto lock = assetWlt3->lockDecryptedContainer(emptyPassLbd);
      auto clearTextSeed = Seeds::ClearTextSeed::deserialize(
         assetWlt3->getDecryptedValue(assetWlt3->getEncryptedSeed()));
      auto seedBip32 = dynamic_cast<Seeds::ClearTextSeed_BIP32*>(clearTextSeed.get());
      EXPECT_EQ(seedBip32->getRawEntropy(), rawEntropy);
      auto end = std::chrono::system_clock::now();
      EXPECT_EQ(counter, 0U);
      EXPECT_LE(end-start, 50ms);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, ChangeControlPassphrase)
{

   auto newPass = SecureBinaryData::fromString("newpass");
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("test")},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 40,
   };

   //create wallet
   std::filesystem::path filename;
   {
      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_BIP32(
            Seeds::SeedType::BIP32_Structured));
      auto assetWlt = AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      filename = assetWlt->getDbFilename();

      //change control pass
      Passphrase::SetNew newCtrlPassObj{
         Passphrase::SetNew{1ms, 0, newPass}};
      assetWlt->changeControlPassphrase(
         newCtrlPassObj,
         params.setCtrlPassObj.getUnlockFunc());

      //close wallet by scoping out
   }

   //open with old pass, should fail
   unsigned oldCounter = 0;
   auto oldPassLbd = [&oldCounter](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      while (oldCounter++ < 10) {
         return { SecureBinaryData::fromString("control"), true };
      }
      return { {}, false };
   };

   try {
      auto wlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{filename, oldPassLbd});
      ASSERT_FALSE(true);
   } catch (const Encryption::DecryptedDataContainerException& e) {
      EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
      EXPECT_EQ(oldCounter, 11U);
   }

   //open with any/empty pass, should fail
   unsigned counter = 0;
   auto anyPassLbd = [&counter]
      (const std::set<EncryptionKeyId>&)->Passphrase::Result
   {
      while (counter++ < 10) {
         return { Cryptography::PRNG::fortuna.generateRandom(20), true };
      }
      return { {}, false };
   };

   try {
      auto wlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{filename, anyPassLbd});
      ASSERT_FALSE(true);
   } catch (const Encryption::DecryptedDataContainerException& e)
   {
      EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
      EXPECT_EQ(counter, 11U);
   }

   //open with new pass, should work
   auto newPassLbd = [&newPass](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { newPass, true };
   };

   try {
      auto wlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{filename, newPassLbd});
      //change pass again from the loaded wallet
      auto newPass2 = SecureBinaryData::fromString("second-pass");
      Passphrase::SetNew newCtrlPassObj{1ms, 0, newPass2};
      wlt->changeControlPassphrase(newCtrlPassObj, newPassLbd);
   } catch (const Encryption::DecryptedDataContainerException&) {
      ASSERT_FALSE(true);
   }

   //open with old pass, should fail
   oldCounter = 0;
   auto oldPassLbd2 = [&oldCounter, &newPass](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      while (oldCounter++ < 10) {
         return { newPass, true };
      }
      return { {}, false };
   };

   try {
      auto wlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{filename, oldPassLbd2});
      ASSERT_FALSE(true);
   } catch (const Encryption::DecryptedDataContainerException& e) {
      EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
      EXPECT_EQ(oldCounter, 11U);
   }

   //open with any/empty pass, should fail
   counter = 0;
   try {
      auto wlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{filename, anyPassLbd});
      ASSERT_FALSE(true);
   } catch (const Encryption::DecryptedDataContainerException& e)
   {
      EXPECT_EQ(e.what(), std::string{"unlock request rejected"});
      EXPECT_EQ(counter, 11U);
   }

   //open with new pass, should work
   auto newPassLbd2 = [](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { SecureBinaryData::fromString("second-pass"), true };
   };

   try {
      auto wlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{filename, newPassLbd2});
   } catch (const Encryption::DecryptedDataContainerException&) {
      ASSERT_FALSE(true);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, MultiplePassphrase)
{
   //create wallet from priv key
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("test")},
      Passphrase::SetNew{1ms, 0, controlPass_},
      nullptr, 4
   };

   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory());
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   auto passLbd1 = [](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { SecureBinaryData::fromString("test"), true };
   };

   auto passLbd2 = [](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { SecureBinaryData::fromString("abcdedfg"), true };
   };

   auto newPassLbd = [](void)->std::unique_ptr<Passphrase::Params>
   {
      return std::make_unique<Passphrase::Params>(
         1ms, 0, SecureBinaryData::fromString("abcdedfg"));
   };

   {
      //try to change passphrase by locking container first, should fail
      auto lock = assetWlt->lockDecryptedContainer(passLbd1);

      try {
         Passphrase::SetNew setnew{newPassLbd};
         assetWlt->addPrivateKeyPassphrase(passLbd1, setnew);
         ASSERT_TRUE(false);
      } catch (const AlreadyLocked&) {}
   }

   {
      //try without locking first, should work
      try {
         Passphrase::SetNew setnew{newPassLbd};
         assetWlt->addPrivateKeyPassphrase(passLbd1, setnew);
      } catch (const AlreadyLocked&) {
         ASSERT_TRUE(false);
      }
   }

   SecureBinaryData key1, key2;
   {
      //try to decrypt with first passphrase, should work
      auto lock = assetWlt->lockDecryptedContainer(passLbd1);

      auto asset0 = TestUtils::getMainAccountAssetForIndex(assetWlt, 0);
      auto asset0_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset0);
      ASSERT_NE(asset0_single, nullptr);

      try {
         key1 = assetWlt->getDecryptedValue(asset0_single->getPrivKey());
      } catch (...) {
         ASSERT_FALSE(true);
      }
   }

   {
      //try to decrypt with second passphrase, should work
      auto lock = assetWlt->lockDecryptedContainer(passLbd2);

      auto asset0 = TestUtils::getMainAccountAssetForIndex(assetWlt, 0);
      auto asset0_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset0);
      ASSERT_NE(asset0_single, nullptr);

      try {
         key2 = assetWlt->getDecryptedValue(asset0_single->getPrivKey());
      } catch (...) {
         ASSERT_FALSE(true);
      }
   }
   EXPECT_EQ(key1, key2);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, BIP32Name_HardenedAccountIndex)
{
   using namespace Armory::Accounts;
   const auto coinType = Armory::Config::BitcoinSettings::getCoinType();

   {
      std::vector<unsigned> path = { 0x8000002C, coinType, 0x80000000 };
      auto account = AccountType_BIP32::makeFromDerPaths(0x12345678, {path});
      EXPECT_EQ(account->name(), "BIP44");
   }

   {
      std::vector<unsigned> path = { 0x8000002C, coinType, 0x80000001 };
      auto account = AccountType_BIP32::makeFromDerPaths(0x12345678, {path});
      EXPECT_EQ(account->name(), "BIP44");
   }

   {
      std::vector<unsigned> path = { 0x80000031, coinType, 0x80000001 };
      auto account = AccountType_BIP32::makeFromDerPaths(0x12345678, {path});
      EXPECT_EQ(account->name(), "BIP49");
   }

   {
      std::vector<unsigned> path = { 0x80000054, coinType, 0x80000002 };
      auto account = AccountType_BIP32::makeFromDerPaths(0x12345678, {path});
      EXPECT_EQ(account->name(), "BIP84");
   }

   {
      std::vector<unsigned> path = { 0x8000002C, coinType, 1 };
      auto account = AccountType_BIP32::makeFromDerPaths(0x12345678, {path});
      EXPECT_EQ(account->name(), "BIP32");
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, WalletDisplayNames)
{
   using namespace Armory::Accounts;
   const auto coinType = Armory::Config::BitcoinSettings::getCoinType();

   EXPECT_EQ(Assets::bip32PurposeDisplayName(
      {0x8000002C, coinType, 0x80000001}), "BIP44");
   EXPECT_EQ(Assets::formatBip32DerivationPath(
      {0x8000002C, 0x80000000, 0x80000000, 0}),
      "m/44'/0'/0'/0");

   auto legacyDir = homedir_ / "display_legacy";
   std::filesystem::create_directories(legacyDir);
   IO::CreateWalletParams legacyParams{
      legacyDir,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("test")},
      Passphrase::SetNew{1ms, 0, controlPass_},
      nullptr, 4
   };

   std::unique_ptr<Seeds::ClearTextSeed> legacySeed(
      new Seeds::ClearTextSeed_Armory());
   auto legacyWlt = AssetWallet_Single::createFromSeed(
      std::move(legacySeed), legacyParams);
   auto legacyAcc = legacyWlt->getAccountForID(legacyWlt->getMainAccountID());
   EXPECT_EQ(legacyAcc->getDisplayName(), "Armory Legacy");
   EXPECT_EQ(legacyAcc->getDerivationSchemeDisplay(), "Armory Legacy");
   auto legacyRoot = std::dynamic_pointer_cast<Assets::AssetEntry_ArmoryLegacyRoot>(
      legacyWlt->getRoot());
   ASSERT_NE(legacyRoot, nullptr);
   EXPECT_EQ(legacyRoot->getDisplayName(), "Armory Legacy (2.00)");
   EXPECT_EQ(legacyWlt->getSeedTypeDisplayName(), "Armory Legacy (2.00)");

   auto importAccId = legacyWlt->setupImportAccount();
   auto importAcc = legacyWlt->getAccountForID(importAccId);
   ASSERT_NE(importAcc, nullptr);
   EXPECT_EQ(importAcc->getDerivationSchemeDisplay(), "N/A");

   SecureBinaryData bip32Seed = READHEX("000102030405060708090a0b0c0d0e0f");
   auto bip32Dir = homedir_ / "display_bip32";
   std::filesystem::create_directories(bip32Dir);
   IO::CreateWalletParams bip32Params{
      bip32Dir,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("test")},
      Passphrase::SetNew{1ms, 0, controlPass_},
      nullptr, 4
   };
   std::unique_ptr<Seeds::ClearTextSeed> bip32SeedObj(
      new Seeds::ClearTextSeed_BIP32(
         bip32Seed, Seeds::SeedType::BIP32_Structured));
   auto bip32Wlt = AssetWallet_Single::createFromSeed(
      std::move(bip32SeedObj), bip32Params);

   std::set<std::string> accountNames;
   for (const auto& accId : bip32Wlt->getAccountIDs()) {
      auto accPtr = bip32Wlt->getAccountForID(accId);
      if (accPtr->isLegacy()) {
         continue;
      }
      accountNames.insert(accPtr->getDisplayName());
      EXPECT_FALSE(accPtr->getDerivationSchemeDisplay().empty());
   }
   EXPECT_EQ(accountNames, (std::set<std::string>{"BIP44", "BIP49", "BIP84"}));
   EXPECT_EQ(bip32Wlt->getSeedTypeDisplayName(), "BIP32");

   auto passthroughKdf =
      std::make_shared<Encryption::KeyDerivationFunction_Passthrough>();
   EXPECT_EQ(passthroughKdf->getDisplayName(), "Passthrough");
   auto romixKdf = std::make_shared<Encryption::KeyDerivationFunction_Romix>(
      1ms, 0);
   EXPECT_EQ(romixKdf->getDisplayName(), "ROMIX");
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, BIP32_Chain)
{
   //BIP32 test 1 seed
   SecureBinaryData wltSeed = READHEX("000102030405060708090a0b0c0d0e0f");
   BIP32_Node seedNode;
   seedNode.initFromSeed(wltSeed);

   //0'/1/2'/2
   std::vector<unsigned> derivationPath = { 0x80000000, 1, 0x80000002 };
   auto account = Accounts::AccountType_BIP32::makeFromDerPaths(
      seedNode.getThisFingerprint(), {derivationPath});
   account->setMain(true);
   account->setAddressLookup(4);

   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("test")},
      Passphrase::SetNew{1ms, 0, controlPass_},
      nullptr, 0
   };

   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP32(
         wltSeed, Seeds::SeedType::BIP32_Virgin));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   {
      auto passphraseLbd = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("test"), true };
      };
      auto lock = assetWlt->lockDecryptedContainer(passphraseLbd);
      assetWlt->createBIP32Account(account);
   }

   auto passphrasePrompt = [](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { SecureBinaryData::fromString("test"), true };
   };

   auto lock = assetWlt->lockDecryptedContainer(passphrasePrompt);

   auto assetPtr = TestUtils::getMainAccountAssetForIndex(assetWlt, 2);
   auto assetSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(assetPtr);
   ASSERT_NE(assetSingle, nullptr);

   auto& decryptedKey =
      assetWlt->getDecryptedValue(assetSingle->getPrivKey());

   BIP32_Node privNode;
   auto priv_b58 =
      SecureBinaryData::fromString("xprvA2JDeKCSNNZky6uBCviVfJSKyQ1mDYahRjijr5idH2WwLsEd4Hsb2Tyh8RfQMuPh7f7RtyzTtdrbdqqsunu5Mm3wDvUAKRHSC34sJ7in334");
   privNode.initFromBase58(priv_b58);
   EXPECT_EQ(decryptedKey, privNode.getPrivateKey());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, BIP32_Public_Chain)
{
   //0'/1/2'
   std::vector<unsigned> derivationPath = { 0x80000000, 1, 0x80000002 };

   //BIP32 test 1 seed
   SecureBinaryData wltSeed = READHEX("000102030405060708090a0b0c0d0e0f");
   BIP32_Node seedNode;
   seedNode.initFromSeed(wltSeed);
   auto seedFingerprint = seedNode.getThisFingerprint();
   for (auto& derId : derivationPath) {
      seedNode.derivePrivate(derId);
   }
   auto pubSeedNode = seedNode.getPublicCopy();

   //2
   std::vector<unsigned> derivationPath_Soft = { 2 };
   auto mainAccType = Accounts::AccountType_BIP32::makeFromDerPaths(
      seedFingerprint, {derivationPath_Soft});
   mainAccType->setSeedRoot(pubSeedNode.getBase58());
   mainAccType->setMain(true);
   mainAccType->setAddressLookup(4);
   mainAccType->setDefaultAddressType(
      AddressEntryType(AddressEntryType::P2WPKH));
   mainAccType->addAddressType(AddressEntryType::P2WPKH);

   auto assetWlt = AssetWallet_Single::createBlank(
      "a wallet"sv, IO::CreateWalletParams{
         homedir_,
         Passphrase::SetNew{1ms, 0, {}},
         Passphrase::SetNew{1ms, 0, controlPass_},
         nullptr, 0, {}, {}
   });
   assetWlt->createBIP32Account(mainAccType);

   auto accID = assetWlt->getMainAccountID();
   auto accPtr = assetWlt->getAccountForID(accID);
   auto outerAcc = accPtr->getOuterAccount();
   auto assetSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(
      outerAcc->getRoot());
   ASSERT_NE(assetSingle, nullptr);

   BIP32_Node pubNode;
   auto pub_b58 = SecureBinaryData::fromString(
      "xpub6FHa3pjLCk84BayeJxFW2SP4XRrFd1JYnxeLeU8EqN3vDfZmbqBqaGJAyiLjTAwm6ZLRQUMv1ZACTj37sR62cfN7fe5JnJ7dh8zL4fiyLHV");
   pubNode.initFromBase58(pub_b58);
   EXPECT_EQ(assetSingle->getPubKey()->getCompressedKey(), pubNode.getPublicKey());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, BIP32_ArmoryDefault)
{
   std::vector<unsigned> derivationPath {
      0x8000002C,
      0x80000000,
      0x80000000
   };

   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);

   //create empty wallet
   auto passphrase = SecureBinaryData::fromString("password");
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, passphrase},
      Passphrase::SetNew{1ms, 0, controlPass_},
      nullptr, 5
   };

   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP32(
         rawEntropy, Seeds::SeedType::BIP32_Structured));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   auto mainAcc = assetWlt->getAccountForID(assetWlt->getMainAccountID());
   auto outerAcc = mainAcc->getOuterAccount();
   auto accRootPtr = std::dynamic_pointer_cast<Assets::AssetEntry_BIP32Root>(
      outerAcc->getRoot());

   BIP32_Node node;
   node.initFromSeed(rawEntropy);
   for (auto id : derivationPath) {
      node.derivePrivate(id);
   }
   node.derivePrivate(0);

   EXPECT_EQ(accRootPtr->getPubKey()->getCompressedKey(), node.getPublicKey());
   auto accIDs = assetWlt->getAccountIDs();
   for (auto& id : accIDs) {
      if (id != mainAcc->getID()) {
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
   std::vector<unsigned> derivationPath1 {
      0x80000050,
      0x800005de,
      0x8000465a,
      501
   };

   //random seed
   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);

   //create empty wallet
   auto passphrase = SecureBinaryData::fromString("password");
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, passphrase},
      Passphrase::SetNew{1ms, 0, controlPass_},
      nullptr, 0
   };

   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP32(
         rawEntropy, Seeds::SeedType::BIP32_Virgin));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   auto passphraseLbd = [&passphrase](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { passphrase, true };
   };

   //this is a hard derivation scenario, the wallet needs to be able to
   //decrypt its root's private key
   Wallets::AddressAccountId accountID1;
   {
      auto lock = assetWlt->lockDecryptedContainer(passphraseLbd);

      auto accountPtr = assetWlt->makeNewBip32AccTypeObject(derivationPath1);
      accountPtr->setMain(true);
      accountPtr->setNodes({0, 1});
      accountPtr->setOuterAccountID(0);
      accountPtr->setAddressLookup(10);

      //add bip32 account for derivationPath1
      accountID1 = assetWlt->createBIP32Account(accountPtr);
   }

   //derive bip32 node
   BIP32_Node seedNode;
   seedNode.initFromSeed(rawEntropy);
   for (auto& derId : derivationPath1) {
      seedNode.derivePrivate(derId);
   }
   auto outerNode = seedNode;
   outerNode.derivePrivate(0);

   {
      //check vs wallet account root
      auto accountPtr = assetWlt->getAccountForID(accountID1);
      auto outerAcc = accountPtr->getOuterAccount();
      auto accountRoot_BIP32 =
         std::dynamic_pointer_cast<Assets::AssetEntry_BIP32Root>(outerAcc->getRoot());
      auto& pubkeyAcc = accountRoot_BIP32->getPubKey()->getCompressedKey();
      EXPECT_EQ(pubkeyAcc, outerNode.getPublicKey());

      {
         //check encryption for the added account works

         //try to fetch without locking wallet
         try {
            assetWlt->getDecryptedValue(accountRoot_BIP32->getPrivKey());

            //should not get here
            ASSERT_TRUE(false);
         } catch (const Encryption::DecryptedDataContainerException&) {}

         //now with the lock
         try {
            auto lock = assetWlt->lockDecryptedContainer(passphraseLbd);
            auto& accountPrivKey =
               assetWlt->getDecryptedValue(accountRoot_BIP32->getPrivKey());
            EXPECT_EQ(accountPrivKey, outerNode.getPrivateKey());
         } catch (...) {
            //should not get here
            ASSERT_TRUE(false);
         }
      }
   }

   //second account
   Wallets::AddressAccountId accountID2;
   std::vector<unsigned> derivationPath2 {
      0x80000244,
      0x8000be7a,
      0x80002000,
      304
   };

   {
      auto accountTypePtr = assetWlt->makeNewBip32AccTypeObject(derivationPath2);
      accountTypePtr->addAddressType(AddressEntryType::P2WPKH);
      accountTypePtr->addAddressType(AddressEntryType::P2PK);
      accountTypePtr->setDefaultAddressType(AddressEntryType::P2WPKH);
      accountTypePtr->setNodes({ 50, 60 });
      accountTypePtr->setOuterAccountID(50);
      accountTypePtr->setInnerAccountID(60);
      accountTypePtr->setAddressLookup(100);

      //add bip32 custom account for derivationPath2
      auto lock = assetWlt->lockDecryptedContainer(passphraseLbd);
      accountID2 = assetWlt->createBIP32Account(accountTypePtr);
   }

   BIP32_Node seedNode2;
   seedNode2.initFromSeed(rawEntropy);
   for (auto& derId : derivationPath2) {
      seedNode2.derivePrivate(derId);
   }
   seedNode2.derivePrivate(50);

   {
      //check vs wallet account root
      auto accountPtr1 = assetWlt->getAccountForID(accountID2);
      auto outerAcc1 = accountPtr1->getOuterAccount();
      auto accountRoot_BIP32 =
      std::dynamic_pointer_cast<Assets::AssetEntry_BIP32Root>(outerAcc1->getRoot());
      auto& pubkey2 = accountRoot_BIP32->getPubKey()->getCompressedKey();
      EXPECT_EQ(pubkey2, seedNode2.getPublicKey());

      //grab address 32, check vs derivation
      auto accountPtr2 = assetWlt->getAccountForID(accountID2);
      auto outerAcc2 = accountPtr2->getOuterAccount();
      auto assetPtr = outerAcc2->getAssetForKey(32);

      auto assetSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(assetPtr);
      ASSERT_NE(assetSingle, nullptr);

      seedNode2.derivePrivate(32);
      EXPECT_EQ(assetSingle->getPubKey()->getCompressedKey(),
         seedNode2.getPublicKey());
   }

   //close wallet, reload it, check again
   auto filename = assetWlt->getDbFilename();
   assetWlt.reset();

   auto assetWlt2 = AssetWallet::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{filename, controlLbd_});
   auto wltSingle2 = std::dynamic_pointer_cast<AssetWallet_Single>(assetWlt2);
   ASSERT_NE(wltSingle2, nullptr);

   {
      //check first account
      auto accPtr = wltSingle2->getAccountForID(accountID1);
      auto outerAcc = accPtr->getOuterAccount();
      auto accountRoot_BIP32 =
         std::dynamic_pointer_cast<Assets::AssetEntry_BIP32Root>(outerAcc->getRoot());
      auto& pubkeyAcc = accountRoot_BIP32->getPubKey()->getCompressedKey();
      EXPECT_EQ(pubkeyAcc, outerNode.getPublicKey());
   }

   {
      //check 2nd account
      auto accountPtr = wltSingle2->getAccountForID(accountID2);
      auto outerAcc = accountPtr->getOuterAccount();
      auto assetPtr = outerAcc->getAssetForKey(32);

      auto assetSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(assetPtr);
      ASSERT_NE(assetSingle, nullptr);
      EXPECT_EQ(assetSingle->getPubKey()->getCompressedKey(),
         seedNode2.getPublicKey());
   }

   //check private keys in both accounts within same decryption lock
   {
      auto lock = wltSingle2->lockDecryptedContainer(passphraseLbd);

      //check first account
      auto accPtr1 = wltSingle2->getAccountForID(accountID1);
      auto outerAcc1 = accPtr1->getOuterAccount();
      auto accountRoot_BIP32 =
         std::dynamic_pointer_cast<Assets::AssetEntry_BIP32Root>(outerAcc1->getRoot());
      auto& privKey = wltSingle2->getDecryptedValue(accountRoot_BIP32->getPrivKey());
      EXPECT_EQ(privKey, outerNode.getPrivateKey());

      //check 2nd account
      auto accPtr2 = wltSingle2->getAccountForID(accountID2);
      auto outerAcc2 = accPtr2->getOuterAccount();
      auto assetPtr = outerAcc2->getAssetForKey(32);

      auto assetSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(assetPtr);
      ASSERT_NE(assetSingle, nullptr);
      auto& privKey2 = wltSingle2->getDecryptedValue(assetSingle->getPrivKey());
      EXPECT_EQ(privKey2, seedNode2.getPrivateKey());
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, BIP32_Fork_WatchingOnly)
{
   auto passphrase = SecureBinaryData::fromString("password");
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, passphrase},
      Passphrase::SetNew{1ms, 0, controlPass_},
      nullptr, 10
   };

   //create regular wallet
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP32(
         Seeds::SeedType::BIP32_Structured));
   auto wlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //create WO copy
   auto woCopyPath = AssetWallet::forkWatchingOnly(
      IO::ReadOnlyFileParams{wlt->getDbFilename(), controlLbd_},
      Passphrase::SetNew{1ms, 0, controlPass_});
   auto woWlt = AssetWallet::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{woCopyPath, controlLbd_});
   auto woSingle = std::dynamic_pointer_cast<AssetWallet_Single>(woWlt);

   //check WO root has no private keys
   {
      EXPECT_TRUE(woSingle->isWatchingOnly());

      auto mainAccountID = woSingle->getMainAccountID();
      auto mainAccount = woSingle->getAccountForID(mainAccountID);
      auto root = mainAccount->getOuterAssetRoot();
      auto rootSingle = std::dynamic_pointer_cast<Assets::AssetEntry_BIP32Root>(root);
      EXPECT_EQ(rootSingle->getPrivKey(), nullptr);
   }

   //compare keys
   for (unsigned i = 0; i < 10; i++) {
      auto assetFull = TestUtils::getMainAccountAssetForIndex(wlt, i);
      auto assetFullSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(assetFull);

      auto assetWo = TestUtils::getMainAccountAssetForIndex(woSingle, i);
      auto assetWoSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(assetWo);

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
      wlt = std::dynamic_pointer_cast<AssetWallet_Single>(
         AssetWallet::loadMainWalletFromFile(
            IO::ReadOnlyFileParams{filename, controlLbd_}));

      auto passphraseLBD = [&passphrase](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { passphrase, true };
      };

      auto lock = wlt->lockDecryptedContainer(passphraseLBD);
      wlt->extendPrivateChain(10);
   }

   woWlt->extendPublicChain(10);

   //compare keys
   for (unsigned i = 10; i < 20; i++) {
      auto assetFull = TestUtils::getMainAccountAssetForIndex(wlt, i);
      auto assetFullSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(assetFull);

      auto assetWo = TestUtils::getMainAccountAssetForIndex(woSingle, i);
      auto assetWoSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(assetWo);

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
   std::vector<unsigned> derPath = {
      0x80000054,
      0x80000000,
      0x80000000
   };

   auto passphrase = SecureBinaryData::fromString("password");
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, passphrase},
      Passphrase::SetNew{1ms, 0, controlPass_},
      nullptr, 10
   };

   //create regular wallet
   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP32(
         rawEntropy, Seeds::SeedType::BIP32_Structured));
   auto wlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //get xpub for main account
   BIP32_Node seedNode;
   seedNode.initFromSeed(rawEntropy);
   auto seedFingerprint = seedNode.getThisFingerprint();
   for (auto& derId : derPath) {
      seedNode.derivePrivate(derId);
   }
   auto pubNode = seedNode.getPublicCopy();
   auto xpub = pubNode.getBase58();

   /* WO wallet creation */

   //1: create wallet
   auto wltWO = AssetWallet_Single::createBlank(
      "walletWO1"sv, IO::CreateWalletParams{
         homedir_,
         Passphrase::SetNew{1ms, 0, {}},
         Passphrase::SetNew{1ms, 0, controlPass_},
         nullptr, 0, {}, {}
      });

   //2: create a custom bip32 account meta data object to setup the WO account
   //structure (nodes & address types)
   auto accountTypePtr = Accounts::AccountType_BIP32::makeFromDerPaths(
      seedFingerprint, {derPath});

   //set nodes
   std::set<unsigned> nodes = {
      BIP32_OUTER_ACCOUNT_DERIVATIONID,
      BIP32_INNER_ACCOUNT_DERIVATIONID};
   accountTypePtr->setNodes(nodes);

   //set xpub
   std::vector<Accounts::PathAndRoot> pathsAndRoots;
   pathsAndRoots.emplace_back(derPath, xpub);
   accountTypePtr->setRoots(pathsAndRoots);

   //populate address types, here native SegWit only
   accountTypePtr->addAddressType(AddressEntryType::P2WPKH);

   //set the default address type as well
   accountTypePtr->setDefaultAddressType(AddressEntryType::P2WPKH);

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
   auto addressOriginal = wlt->getNewAddress(AddressEntryType::P2WPKH);

   EXPECT_EQ(addressWO->getAddress(), addressOriginal->getAddress());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, AddressEntryTypes)
{
   //create wallet
   auto passphrase = SecureBinaryData::fromString("password");
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, passphrase},
      Passphrase::SetNew{1ms, 0, controlPass_},
      nullptr, 10
   };

   //create regular wallet
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP32(
         Seeds::SeedType::BIP32_Structured));
   auto wlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //grab a bunch of addresses of various types
   std::set<BinaryData> addrHashes;

   //5 default addresses
   for (unsigned i = 0; i < 5; i++) {
      auto addrPtr = wlt->getNewAddress();
      addrHashes.insert(addrPtr->getPrefixedHash());
   }

   //5 p2wpkh
   for (unsigned i = 0; i < 5; i++) {
      auto addrPtr = wlt->getNewAddress(AddressEntryType::P2WPKH);
      addrHashes.insert(addrPtr->getPrefixedHash());
   }

   //5 nested p2wpkh change addresses
   for (unsigned i = 0; i < 5; i++) {
      auto addrPtr = wlt->getNewChangeAddress(AddressEntryType(
         AddressEntryType::P2SH | AddressEntryType::P2WPKH));
      addrHashes.insert(addrPtr->getPrefixedHash());
   }

   //shutdown wallet
   auto filename = wlt->getDbFilename();
   wlt.reset();

   //load from file
   auto loaded = AssetWallet::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{filename, controlLbd_});

   //check used address list from loaded wallet matches grabbed addresses
   {
      auto usedAddressMap = loaded->getUsedAddressMap();
      std::set<BinaryData> usedAddrHashes;
      for (auto& addrPair : usedAddressMap) {
         usedAddrHashes.insert(addrPair.second->getPrefixedHash());
      }
      EXPECT_EQ(addrHashes, usedAddrHashes);
   }

   //shutdown wallet
   loaded.reset();

   //create WO copy
   auto woFilename = AssetWallet::forkWatchingOnly(
      IO::ReadOnlyFileParams{filename, controlLbd_},
      Passphrase::SetNew{1ms, 0, controlPass_});
   auto woLoaded = AssetWallet::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{woFilename, controlLbd_});

   {
      auto usedAddressMap = woLoaded->getUsedAddressMap();
      std::set<BinaryData> usedAddrHashes;
      for (auto& addrPair : usedAddressMap) {
         usedAddrHashes.insert(addrPair.second->getPrefixedHash());
      }
      EXPECT_EQ(addrHashes, usedAddrHashes);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, LegacyUncompressedAddressTypes)
{
   //create wallet
   std::vector<unsigned> derPath {
      0x80000050,
      0x80005421,
      0x80000024,
      785
   };

   auto passphrase = SecureBinaryData::fromString("password");
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, passphrase},
      Passphrase::SetNew{1ms, 0, controlPass_},
      nullptr, 0
   };

   //create regular wallet
   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP32(
         rawEntropy, Seeds::SeedType::BIP32_Virgin));
   auto wlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //create account with all common uncompressed address types & their 
   //compressed counterparts
   auto accountTypePtr = wlt->makeNewBip32AccTypeObject(derPath);

   std::set<unsigned> nodes = {0, 1};
   accountTypePtr->setNodes(nodes);
   accountTypePtr->setOuterAccountID(*nodes.begin());
   accountTypePtr->setInnerAccountID(*nodes.rbegin());

   accountTypePtr->setDefaultAddressType(AddressEntryType::P2PKH);
   accountTypePtr->addAddressType(AddressEntryType::P2PKH);
   accountTypePtr->addAddressType(AddressEntryType(
      AddressEntryType::P2PKH | AddressEntryType::Uncompressed));
   accountTypePtr->addAddressType(AddressEntryType(
      AddressEntryType::P2PK | AddressEntryType::P2SH));

   accountTypePtr->setAddressLookup(20);
   accountTypePtr->setMain(true);

   auto passphraseLbd = [&passphrase](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { passphrase, true };
   };
   {
      auto lock = wlt->lockDecryptedContainer(passphraseLbd);
      wlt->createBIP32Account(accountTypePtr);
   }

   //grab addresses for each type, check vs manual instantiation
   auto addr1 = wlt->getNewAddress(AddressEntryType::P2PKH);
   auto addr2 = wlt->getNewAddress(
      AddressEntryType(AddressEntryType::P2PKH | AddressEntryType::Uncompressed));
   auto addr3 = wlt->getNewAddress(
      AddressEntryType(AddressEntryType::P2PK | AddressEntryType::P2SH));

   //derive the keys locally and reproduce the addresses
   BIP32_Node bip32Node;
   bip32Node.initFromSeed(rawEntropy);
   for (auto& der : derPath) {
      bip32Node.derivePrivate(der);
   }
   bip32Node.derivePublic(0); //spender leaf

   {
      //addr1
      auto nodeCopy = bip32Node.getPublicCopy();
      nodeCopy.derivePublic(0); //asset #0

      auto pubkey = nodeCopy.getPublicKey();
      auto hash160 = BtcUtils::getHash160(pubkey);
      BinaryWriter bw;
      bw.put_uint8_t(Config::BitcoinSettings::getPubkeyHashPrefix());
      bw.put_BinaryData(hash160);

      EXPECT_EQ(addr1->getPrefixedHash(), bw.getData());
   }

   {
      //addr2
      auto nodeCopy = bip32Node.getPublicCopy();
      nodeCopy.derivePublic(1); //asset #1

      auto pubkey = nodeCopy.getPublicKey();
      auto pubkey2 = Cryptography::ECDSA::uncompressPoint(pubkey);
      auto hash160 = BtcUtils::getHash160(pubkey2);
      BinaryWriter bw;
      bw.put_uint8_t(Config::BitcoinSettings::getPubkeyHashPrefix());
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
      p2shBw.put_uint8_t(Config::BitcoinSettings::getScriptHashPrefix());
      p2shBw.put_BinaryData(BtcUtils::getHash160(bw.getData()));

      EXPECT_EQ(addr3->getPrefixedHash(), p2shBw.getData());
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, BIP32_SaltedAccount)
{
   std::vector<unsigned> derivationPath1 {
      0x80000050,
      0x800005de,
      0x8000465a,
      501
   };

   std::vector<unsigned> derivationPath2 {
      0x80000050,
      0x800005de,
      0x8000ee4f,
      327
   };

   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);
   auto salt1 = Cryptography::PRNG::generateRandomStrong(32);
   auto salt2 = Cryptography::PRNG::generateRandomStrong(32);

   std::filesystem::path filename;
   AddressAccountId accountID1;
   AddressAccountId accountID2;
   std::set<BinaryData> addrHashSet;

   {
      //create empty wallet
      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_BIP32(
            rawEntropy, Seeds::SeedType::BIP32_Virgin));
      auto passphrase = SecureBinaryData::fromString("password");
      IO::CreateWalletParams params{
         homedir_,
         Passphrase::SetNew{1ms, 0, passphrase},
         Passphrase::SetNew{1ms, 0, controlPass_},
         nullptr, 0
      };

      auto assetWlt = AssetWallet_Single::createFromSeed(
         std::move(seed), params);

      auto rootbip32 = std::dynamic_pointer_cast<Assets::AssetEntry_BIP32Root>(
         assetWlt->getRoot());
      ASSERT_NE(rootbip32, nullptr);

      auto passphraseLbd = [&passphrase](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { passphrase, true };
      };
      auto lock = assetWlt->lockDecryptedContainer(passphraseLbd);

      //create accounts
      auto saltedAccType1 = Accounts::AccountType_BIP32_Salted::makeFromDerPaths(
         rootbip32->getSeedFingerprint(true), {derivationPath1}, salt1);
      saltedAccType1->setAddressLookup(40);
      saltedAccType1->setDefaultAddressType(
         AddressEntryType::P2WPKH);
      saltedAccType1->addAddressType(AddressEntryType::P2WPKH);

      auto saltedAccType2 = Accounts::AccountType_BIP32_Salted::makeFromDerPaths(
         rootbip32->getSeedFingerprint(true), {derivationPath2}, salt2);
      saltedAccType2->setAddressLookup(40);
      saltedAccType2->setDefaultAddressType(
         AddressEntryType::P2WPKH);
      saltedAccType2->addAddressType(AddressEntryType::P2WPKH);

      //add bip32 account for derivationPath1
      accountID1 = assetWlt->createBIP32Account(saltedAccType1);

      //add bip32 account for derivationPath2
      accountID2 = assetWlt->createBIP32Account(saltedAccType2);

      //grab 10 addresses
      std::vector<std::shared_ptr<AddressEntry>> addrVec1, addrVec2;
      for (unsigned i = 0; i < 10; i++) {
         addrVec1.push_back(assetWlt->getNewAddress(accountID1));
         addrVec2.push_back(assetWlt->getNewAddress(accountID2));
      }

      //derive from seed
      {
         BIP32_Node seedNode;
         seedNode.initFromSeed(rawEntropy);
         for (auto& derId : derivationPath1) {
            seedNode.derivePrivate(derId);
         }

         for (unsigned i = 0; i < 10; i++) {
            auto nodeCopy = seedNode;
            nodeCopy.derivePrivate(i);
            auto pubkey = nodeCopy.getPublicKey();
            auto saltedKey = Cryptography::ECDSA::pubKeyScalarMultiply(
               pubkey, salt1);
            EXPECT_EQ(saltedKey, addrVec1[i]->getPreimage());
         }
      }

      {
         BIP32_Node seedNode;
         seedNode.initFromSeed(rawEntropy);
         for (auto& derId : derivationPath2) {
            seedNode.derivePrivate(derId);
         }
         for (unsigned i = 0; i < 10; i++) {
            auto nodeCopy = seedNode;
            nodeCopy.derivePrivate(i);
            auto pubkey = nodeCopy.getPublicKey();
            auto saltedKey = Cryptography::ECDSA::pubKeyScalarMultiply(
               pubkey, salt2);
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
         IO::ReadOnlyFileParams{filename, controlLbd_});
      auto wltSingle = std::dynamic_pointer_cast<AssetWallet_Single>(assetWlt);

      //check current address map
      EXPECT_EQ(addrHashSet, assetWlt->getAddrHashSet());

      //grab more 10 addresses
      std::vector<std::shared_ptr<AddressEntry>> addrVec1, addrVec2;
      for (unsigned i = 0; i < 10; i++) {
         addrVec1.push_back(wltSingle->getNewAddress(accountID1));
         addrVec2.push_back(wltSingle->getNewAddress(accountID2));
      }

      //derive from seed
      {
         BIP32_Node seedNode;
         seedNode.initFromSeed(rawEntropy);
         for (auto& derId : derivationPath1) {
            seedNode.derivePrivate(derId);
         }
         for (unsigned i = 0; i < 10; i++) {
            auto nodeCopy = seedNode;
            nodeCopy.derivePrivate(i + 10);
            auto pubkey = nodeCopy.getPublicKey();
            auto saltedKey = Cryptography::ECDSA::pubKeyScalarMultiply(
               pubkey, salt1);
            EXPECT_EQ(saltedKey, addrVec1[i]->getPreimage());
         }
      }

      {
         BIP32_Node seedNode;
         seedNode.initFromSeed(rawEntropy);
         for (auto& derId : derivationPath2) {
            seedNode.derivePrivate(derId);
         }
         for (unsigned i = 0; i < 10; i++) {
            auto nodeCopy = seedNode;
            nodeCopy.derivePrivate(i + 10);
            auto pubkey = nodeCopy.getPublicKey();
            auto saltedKey = Cryptography::ECDSA::pubKeyScalarMultiply(
               pubkey, salt2);
            EXPECT_EQ(saltedKey, addrVec2[i]->getPreimage());
         }
      }

      addrHashSet = assetWlt->getAddrHashSet();
      ASSERT_EQ(addrHashSet.size(), 80ULL);

      //create WO copy
      filename = AssetWallet::forkWatchingOnly(
         IO::ReadOnlyFileParams{filename, controlLbd_},
         Passphrase::SetNew{1ms, 0, controlPass_}
      );
   }

   {
      auto assetWlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{filename, controlLbd_});
      auto wltSingle = std::dynamic_pointer_cast<AssetWallet_Single>(assetWlt);

      ASSERT_TRUE(wltSingle->isWatchingOnly());
      EXPECT_EQ(addrHashSet, assetWlt->getAddrHashSet());

      auto accountSalted1 = wltSingle->getAccountForID(accountID1);
      auto accountSalted2 = wltSingle->getAccountForID(accountID2);

      //grab more 10 addresses
      std::vector<std::shared_ptr<AddressEntry>> addrVec1, addrVec2;
      for (unsigned i = 0; i < 10; i++) {
         addrVec1.push_back(wltSingle->getNewAddress(accountID1));
         addrVec2.push_back(wltSingle->getNewAddress(accountID2));
      }

      //derive from seed
      {
         BIP32_Node seedNode;
         seedNode.initFromSeed(rawEntropy);
         for (auto& derId : derivationPath1) {
            seedNode.derivePrivate(derId);
         }
         for (unsigned i = 0; i < 10; i++) {
            auto nodeCopy = seedNode;
            nodeCopy.derivePrivate(i + 20);
            auto pubkey = nodeCopy.getPublicKey();
            auto saltedKey = Cryptography::ECDSA::pubKeyScalarMultiply(
               pubkey, salt1);
            EXPECT_EQ(saltedKey, addrVec1[i]->getPreimage());
         }
      }

      {
         BIP32_Node seedNode;
         seedNode.initFromSeed(rawEntropy);
         for (auto& derId : derivationPath2) {
            seedNode.derivePrivate(derId);
         }
         for (unsigned i = 0; i < 10; i++) {
            auto nodeCopy = seedNode;
            nodeCopy.derivePrivate(i + 20);
            auto pubkey = nodeCopy.getPublicKey();
            auto saltedKey = Cryptography::ECDSA::pubKeyScalarMultiply(
               pubkey, salt2);
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

   auto privKey1 = READHEX(
      "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F");
   auto pubKey1 = Cryptography::ECDSA::computePublicKey(privKey1, true);

   auto privKey2 = READHEX(
      "101112131415161718191A1B1C1D1E1F202122232425262728292A2B2C2D2E2F");
   auto pubKey2 = Cryptography::ECDSA::computePublicKey(privKey2, true);


   auto passphrase = SecureBinaryData::fromString("password");

   std::map<unsigned, SecureBinaryData> saltMap1;
   std::map<unsigned, SecureBinaryData> saltMap2;

   AddressAccountId accID2;
   std::map<unsigned, BinaryData> addrMap1, addrMap2;

   {
      //create empty wallet
      IO::CreateWalletParams params{
         homedir_,
         Passphrase::SetNew{1ms, 0, passphrase},
         Passphrase::SetNew{1ms, 0, controlPass_},
         nullptr, 0
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_BIP32(
            Seeds::SeedType::BIP32_Virgin));
      auto assetWlt = AssetWallet_Single::createFromSeed(
         std::move(seed), params);

      auto passphraseLbd = [&passphrase](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { passphrase, true };
      };
      auto lock = assetWlt->lockDecryptedContainer(passphraseLbd);

      //create accounts
      auto ecdhAccType1 =
         std::make_shared<Accounts::AccountType_ECDH>(privKey1.getRef(), pubKey1);
      ecdhAccType1->setDefaultAddressType(
         AddressEntryType::P2WPKH);
      ecdhAccType1->addAddressType(AddressEntryType::P2WPKH);
      ecdhAccType1->setMain(true);

      auto ecdhAccType2 =
         std::make_shared<Accounts::AccountType_ECDH>(privKey2.getRef(), pubKey2);
      ecdhAccType2->setDefaultAddressType(
         AddressEntryType::P2WPKH);
      ecdhAccType2->addAddressType(AddressEntryType::P2WPKH);

      //add accounts
      auto accPtr1 = assetWlt->createAccount(ecdhAccType1, nullptr);
      auto assAccPtr1 = accPtr1->getOuterAccount();
      auto accEcdh1 = dynamic_cast<Accounts::AssetAccount_ECDH*>(assAccPtr1.get());
      if (accEcdh1 == nullptr) {
         throw std::runtime_error("unexpected account type 1");
      }
      auto accPtr2 = assetWlt->createAccount(ecdhAccType2, nullptr);
      auto assAccPtr2 = accPtr2->getOuterAccount();
      auto accEcdh2 = dynamic_cast<Accounts::AssetAccount_ECDH*>(assAccPtr2.get());
      if (accEcdh2 == nullptr) {
         throw std::runtime_error("unexpected account type 2");
      }
      accID2 = accPtr2->getID();

      {
         //add salts
         auto tx = assetWlt->beginSubDBTransaction(assetWlt->getID(), true);
         for (unsigned i = 0; i < 5; i++) {
            auto salt = Cryptography::PRNG::generateRandomStrong(32);
            auto index = accEcdh1->addSalt(tx, salt);
            saltMap1.emplace(index, salt);

            salt = Cryptography::PRNG::generateRandomStrong(32);
            index = accEcdh2->addSalt(tx, salt);
            saltMap2.emplace(index, salt);
         }
      }

      //grab addresses
      for (unsigned i = 0; i < 5; i++) {
         addrMap1.emplace(i, assetWlt->getNewAddress(accPtr1->getID())->getHash());
         addrMap2.emplace(i, assetWlt->getNewAddress(accPtr2->getID())->getHash());
      }
   
      //derive locally, check addresses match
      for (unsigned i = 0; i < 5; i++) {
         auto saltedKey = Cryptography::ECDSA::pubKeyScalarMultiply(
            pubKey1, saltMap1[i]);
         auto hash = BtcUtils::getHash160(saltedKey);
         EXPECT_EQ(addrMap1[i], hash);

         saltedKey = Cryptography::ECDSA::pubKeyScalarMultiply(
            pubKey2, saltMap2[i]);
         hash = BtcUtils::getHash160(saltedKey);
         EXPECT_EQ(addrMap2[i], hash);
      }
      filename = assetWlt->getDbFilename();
   }

   {
      //reload wallet
      auto wlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{filename, controlLbd_});
      auto assetWlt = std::dynamic_pointer_cast<AssetWallet_Single>(wlt);
      if (assetWlt == nullptr) {
         throw std::runtime_error("unexpected wallet type");
      }

      //check existing address set
      auto addrHashSet = assetWlt->getAddrHashSet();
      EXPECT_EQ(addrHashSet.size(), 10ULL);

      for (unsigned i = 0; i < 5; i++) {
         auto saltedKey = Cryptography::ECDSA::pubKeyScalarMultiply(
            pubKey1, saltMap1[i]);
         auto hash = BtcUtils::getHash160(saltedKey);
         BinaryWriter bwAddr;
         bwAddr.put_uint8_t((uint8_t)ScriptPrefix::P2WPKH);
         bwAddr.put_BinaryData(hash);

         auto iter = addrHashSet.find(bwAddr.getData());
         EXPECT_NE(iter, addrHashSet.end());

         //
         saltedKey = Cryptography::ECDSA::pubKeyScalarMultiply(
            pubKey2, saltMap2[i]);
         hash = BtcUtils::getHash160(saltedKey);
         BinaryWriter bwAddr2;
         bwAddr2.put_uint8_t((uint8_t)ScriptPrefix::P2WPKH);
         bwAddr2.put_BinaryData(hash);

         iter = addrHashSet.find(bwAddr2.getData());
         EXPECT_NE(iter, addrHashSet.end());
      }

      auto accID = assetWlt->getMainAccountID();
      auto accPtr = assetWlt->getAccountForID(accID);
      auto assAccPtr = accPtr->getOuterAccount();
      auto accEcdh = dynamic_cast<Accounts::AssetAccount_ECDH*>(assAccPtr.get());
      if (accEcdh == nullptr) {
         throw std::runtime_error("unexpected account type 3");
      }
      {
         auto tx = assetWlt->beginSubDBTransaction(assetWlt->getID(), true);
         auto salt = Cryptography::PRNG::generateRandomStrong(32);
         auto index = accEcdh->addSalt(tx, salt);
         saltMap1.insert(std::make_pair(index, salt));
      }

      {
         //grab another address & check it
         auto addr = assetWlt->getNewAddress()->getHash();
         auto saltedKey = Cryptography::ECDSA::pubKeyScalarMultiply(
            pubKey1, saltMap1[5]);
         auto hash = BtcUtils::getHash160(saltedKey);
         EXPECT_EQ(addr, hash);
      }

      {
         //grab an existing address from its settlement id
         auto tx = assetWlt->beginSubDBTransaction(assetWlt->getID(), true);
         auto id = accEcdh->addSalt(tx, saltMap1[3]);
         EXPECT_EQ(id, 3);

         auto assetPtr = accEcdh->getAssetForKey(id);
         auto assetSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(assetPtr);
         auto hash = BtcUtils::getHash160(
            assetSingle->getPubKey()->getCompressedKey());
         EXPECT_EQ(addrMap1[3], hash);
      }

      auto accPtr2 = assetWlt->getAccountForID(accID2);
      {
         //same with account 2
         auto assAcc2 = accPtr2->getOuterAccount();
         auto accEcdhPtr = dynamic_cast<Accounts::AssetAccount_ECDH*>(assAcc2.get());
         ASSERT_NE(accEcdhPtr, nullptr);

         auto tx = assetWlt->beginSubDBTransaction(assetWlt->getID(), true);
         auto id = accEcdhPtr->addSalt(tx, saltMap2[2]);
         EXPECT_EQ(id, 2);

         auto assetPtr = accEcdhPtr->getAssetForKey(id);
         auto assetSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(assetPtr);
         auto hash = BtcUtils::getHash160(
            assetSingle->getPubKey()->getCompressedKey());
         EXPECT_EQ(addrMap2[2], hash);
      }
   }

   woFilename = AssetWallet::forkWatchingOnly(
      IO::ReadOnlyFileParams{filename, controlLbd_},
      Passphrase::SetNew{1ms, 0, controlPass_}
   );

   //same with WO
   {
      //reload wallet
      auto wlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{woFilename, controlLbd_});
      auto assetWlt = std::dynamic_pointer_cast<AssetWallet_Single>(wlt);
      if (assetWlt == nullptr) {
         throw std::runtime_error("unexpected wallet type");
      }
      ASSERT_TRUE(assetWlt->isWatchingOnly());

      //check existing address set
      auto addrHashSet = assetWlt->getAddrHashSet();
      EXPECT_EQ(addrHashSet.size(), 11ULL);

      for (unsigned i = 0; i < 6; i++) {
         auto saltedKey = Cryptography::ECDSA::pubKeyScalarMultiply(
            pubKey1, saltMap1[i]);
         auto hash = BtcUtils::getHash160(saltedKey);
         BinaryWriter bwAddr;
         bwAddr.put_uint8_t((uint8_t)ScriptPrefix::P2WPKH);
         bwAddr.put_BinaryData(hash);

         auto iter = addrHashSet.find(bwAddr.getData());
         EXPECT_NE(iter, addrHashSet.end());
      }

      auto accID = assetWlt->getMainAccountID();
      auto accPtr = assetWlt->getAccountForID(accID);
      auto assAccPtr = accPtr->getOuterAccount();
      auto accEcdh = dynamic_cast<Accounts::AssetAccount_ECDH*>(assAccPtr.get());
      if (accEcdh == nullptr) {
         throw std::runtime_error("unexpected account type 4");
      }
      auto rootAsset = accEcdh->getRoot();
      auto rootSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(rootAsset);
      ASSERT_NE(rootSingle, nullptr);
      EXPECT_EQ(rootSingle->getPrivKey(), nullptr);

      {
         auto tx = assetWlt->beginSubDBTransaction(assetWlt->getID(), true);
         auto salt = Cryptography::PRNG::generateRandomStrong(32);
         auto index = accEcdh->addSalt(tx, salt);
         saltMap1.insert(std::make_pair(index, salt));
      }

      {
         //grab another address & check it
         auto addr = assetWlt->getNewAddress()->getHash();
         auto saltedKey = Cryptography::ECDSA::pubKeyScalarMultiply(
            pubKey1, saltMap1[6]);
         auto hash = BtcUtils::getHash160(saltedKey);
         EXPECT_EQ(addr, hash);
      }

      auto accID2 = assetWlt->getMainAccountID();
      auto accPtr2 = assetWlt->getAccountForID(accID2);

      for (unsigned i = 0; i < 5; i++) {
         auto saltedKey = Cryptography::ECDSA::pubKeyScalarMultiply(
            pubKey2, saltMap2[i]);
         auto hash = BtcUtils::getHash160(saltedKey);
         BinaryWriter bwAddr;
         bwAddr.put_uint8_t((uint8_t)ScriptPrefix::P2WPKH);
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
   auto rawEntropy = Cryptography::PRNG::generateRandomStrong(32);
   std::vector<uint32_t> derPath {
      0x800012ab,
      0x8000ff13,
      0x80050000
   };

   BIP32_Node node;
   node.initFromSeed(rawEntropy);
   auto seedFingerprint = node.getThisFingerprint();

   for (auto& step : derPath) {
      node.derivePrivate(step);
   }
   auto pubNode = node.getPublicCopy();
   node.derivePublic(0);
   node.derivePublic(5);

   auto pubkey = node.getPublicKey();
   auto xpub = node.getBase58();
   std::string xpubStr{xpub.getCharPtr(), xpub.getSize()};

   auto checkWlt = [&pubkey, &xpubStr, &derPath](std::shared_ptr<AssetWallet> wltPtr)->bool
   {
      auto fullPath = derPath;
      fullPath.push_back(0);
      fullPath.push_back(5);

      auto wlt_single = std::dynamic_pointer_cast<AssetWallet_Single>(wltPtr);
      auto resolver = std::make_shared<Signing::ResolverFeed_AssetWalletSingle>(wlt_single);
      auto assetPath = resolver->resolveBip32PathForPubkey(pubkey);
      auto pathFromSeed = assetPath.getDerivationPathFromSeed();

      if (fullPath.size() != pathFromSeed.size()) {
         return false;
      }

      for (unsigned i=0; i<pathFromSeed.size(); i++) {
         if (pathFromSeed[i] != fullPath[i]) {
            return false;
         }
      }

      auto pubkeyHash = BtcUtils::getHash160(pubkey);
      auto resolvedPubkey = resolver->getByVal(pubkeyHash);
      return resolvedPubkey == pubkey;
   };

   {
      //empty wallet + custom account
      IO::CreateWalletParams params{
         homedir_,
         Passphrase::SetNew{1ms, 0, {}},
         Passphrase::SetNew{1ms, 0, {}},
         nullptr, 0
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_BIP32(
            rawEntropy, Seeds::SeedType::BIP32_Virgin));
      auto wlt = AssetWallet_Single::createFromSeed(
         std::move(seed), params);

      {
         auto account = wlt->makeNewBip32AccTypeObject(derPath);
         account->setMain(true);
         account->setNodes({0});
         account->setDefaultAddressType(
            AddressEntryType(AddressEntryType::P2WPKH));
         account->addAddressType(AddressEntryType::P2WPKH);
         account->setAddressLookup(10);

         auto lock = wlt->lockDecryptedContainer({});
         wlt->createBIP32Account(account);
      }
      EXPECT_TRUE(checkWlt(wlt));

      //create a WO copy
      auto filename = wlt->getDbFilename();
      auto woFilename = AssetWallet::forkWatchingOnly(
         IO::ReadOnlyFileParams{filename, nullptr},
         Passphrase::SetNew{1ms, 0, {}}
      );

      //cleanup original wallet
      wlt.reset();
      std::filesystem::remove(filename);

      //check WO wallet
      auto wltWO = AssetWallet_Single::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{woFilename,
            [](const std::set<EncryptionKeyId>&)->Passphrase::Result
            { return { {}, false }; }
         });
      EXPECT_TRUE(checkWlt(wltWO));

      //cleanup WO
      wltWO.reset();
      std::filesystem::remove(woFilename);
   }

   {
      //empty WO wallet
      auto wltWO = AssetWallet_Single::createBlank(
         "walletWO1"sv, IO::CreateWalletParams{
            homedir_,
            Passphrase::SetNew{1ms, 0, {}},
            Passphrase::SetNew{1ms, 0, {}},
            nullptr, 0
         });

      //add account
      auto mainAccType = Accounts::AccountType_BIP32::makeFromDerPaths(
         seedFingerprint, {derPath});
      mainAccType->setMain(true);
      mainAccType->setAddressLookup(10);
      mainAccType->setNodes({0});
      mainAccType->setDefaultAddressType(
         AddressEntryType(AddressEntryType::P2WPKH));
      mainAccType->addAddressType(AddressEntryType::P2WPKH);

      auto b58sbd = pubNode.getBase58();
      std::string xpub{b58sbd.getCharPtr(), b58sbd.getSize()};
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
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, passphrase},
      Passphrase::SetNew{1ms, 0, controlPass_},
      nullptr, 10
   };

   //create regular wallet
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP32(
         Seeds::SeedType::BIP32_Structured));
   auto wlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //grab a bunch of addresses of various types
   std::map<AssetId, BinaryData> addrHashesInUse;

   //5 default addresses
   for (unsigned i = 0; i < 5; i++) {
      auto addrPtr = wlt->getNewAddress();
      addrHashesInUse.emplace(addrPtr->getID(), addrPtr->getPrefixedHash());
   }

   //5 p2wpkh
   for (unsigned i = 0; i < 5; i++) {
      auto addrPtr = wlt->getNewAddress(AddressEntryType::P2WPKH);
      addrHashesInUse.emplace(addrPtr->getID(), addrPtr->getPrefixedHash());
   }

   //grab all address hashes for wallet
   auto addrHashes = wlt->getAddrHashSet();

   ASSERT_EQ(addrHashesInUse.size(), 10ULL);
   ASSERT_EQ(addrHashes.size(), 80ULL);

   std::set<AssetId> detectedIds;
   for (const auto& addrIt : addrHashes) {
      const auto& idAndType = wlt->getAssetIDForScrAddr(addrIt);
      const auto& id = idAndType.first;

      //is this one of our grabbed addresses?
      auto inUseIt = addrHashesInUse.find(id);
      if (inUseIt == addrHashesInUse.end()) {
         //it isn't, should be seen as unused
         EXPECT_FALSE(wlt->isAssetUsed(id));
         continue;
      }

      EXPECT_TRUE(wlt->isAssetUsed(id));
      detectedIds.emplace(id);
   }

   //make sure we've seen every address
   for (const auto& id : detectedIds) {
      addrHashesInUse.erase(id);
   }
   ASSERT_TRUE(addrHashesInUse.empty());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, ImportPublicKeys)
{
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, {}},
      Passphrase::SetNew{1ms, 0, {}},
      nullptr, 0
   };

   //create a blank WO wallet
   auto wltWO = AssetWallet_Single::createBlank("walletWO1"sv, params);

   try {
      //setup the import account
      auto importAccId = wltWO->setupImportAccount();
      ASSERT_EQ(importAccId.toHexStr(), "00001201");

      auto importAddrAcc = wltWO->getAccountForID(importAccId);
      ASSERT_NE(importAddrAcc, nullptr);

      auto outerAcc = importAddrAcc->getOuterAccount();
      auto importAcc = dynamic_cast<Accounts::AssetAccount_ImportsWO*>(outerAcc.get());
      ASSERT_NE(importAcc, nullptr);
   } catch (const std::exception& e) {
      std::cout << e.what() << std::endl;
      ASSERT_TRUE(false);
   }

   std::map<BinaryData, AssetId> keyToAddrMap;
   auto checkAddresses = [&keyToAddrMap]
   (std::shared_ptr<AssetWallet_Single> wlt)->bool
   {
      auto addrMap = wlt->getUsedAddressMap();
      if (addrMap.size() != keyToAddrMap.size()) {
         return false;
      }

      for (const auto& addrPair : addrMap) {
         auto addrHash = addrPair.second->getPrefixedHash();
         auto iter = keyToAddrMap.find(addrHash);
         if (iter == keyToAddrMap.end()) {
            return false;
         }
         if (iter->second != addrPair.first) {
            return false;
         }
      }

      //grab address hashes from wallet, should match our map
      auto addrHashSet = wlt->getAddrHashSet();
      if (addrHashSet.size() != keyToAddrMap.size()) {
         return false;
      }
      for (const auto& addrHash : addrHashSet) {
         if (keyToAddrMap.find(addrHash) == keyToAddrMap.end()) {
            return false;
         }
      }
      return true;
   };

   //import addr B & C
   {
      //B
      auto pubKeyB = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrB);
      auto keyB = wltWO->importPublicKey(pubKeyB, AddressEntryType(
         AddressEntryType::P2PKH | AddressEntryType::Uncompressed));
      keyToAddrMap.emplace(TestChain::scrAddrB, keyB);

      //C
      auto pubKeyC = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrC);
      auto keyC = wltWO->importPublicKey(pubKeyC, AddressEntryType(
         AddressEntryType::P2PKH | AddressEntryType::Uncompressed));
      keyToAddrMap.emplace(TestChain::scrAddrC, keyC);
   }
   ASSERT_TRUE(checkAddresses(wltWO));

   //import addr D
   {
      auto pubKeyD = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrD);
      auto keyD = wltWO->importPublicKey(pubKeyD, AddressEntryType(
         AddressEntryType::P2PKH | AddressEntryType::Uncompressed));
      keyToAddrMap.emplace(TestChain::scrAddrD, keyD);
   }
   ASSERT_TRUE(checkAddresses(wltWO));

   /* shutdown the wallet, reload and check addresses again */
   const auto wltPath = wltWO->getDbFilename();
   wltWO.reset();
   ASSERT_EQ(wltWO, nullptr);

   {
      auto wlt = AssetWallet::loadMainWalletFromFile({wltPath, nullptr});
      wltWO = std::dynamic_pointer_cast<AssetWallet_Single>(wlt);
      ASSERT_NE(wltWO, nullptr);
   }
   ASSERT_TRUE(checkAddresses(wltWO));

   //import addr E
   {
      auto pubKeyE = Cryptography::ECDSA::computePublicKey(TestChain::privKeyAddrE);
      auto keyE = wltWO->importPublicKey(pubKeyE, AddressEntryType(
         AddressEntryType::P2PKH | AddressEntryType::Uncompressed));
      keyToAddrMap.emplace(TestChain::scrAddrE, keyE);
   }
   ASSERT_TRUE(checkAddresses(wltWO));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, ImportScrAddr)
{
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, {}},
      Passphrase::SetNew{1ms, 0, {}},
      nullptr, 0
   };

   //create a blank WO wallet
   auto wltWO = AssetWallet_Single::createBlank("walletWO1"sv, params);

   try {
      //setup the import account
      auto importAccId = wltWO->setupImportAccount();
      ASSERT_EQ(importAccId.toHexStr(), "00001201");

      auto importAddrAcc = wltWO->getAccountForID(importAccId);
      ASSERT_NE(importAddrAcc, nullptr);

      auto outerAcc = importAddrAcc->getOuterAccount();
      auto importAcc = dynamic_cast<Accounts::AssetAccount_ImportsWO*>(outerAcc.get());
      ASSERT_NE(importAcc, nullptr);
   } catch (const std::exception& e) {
      std::cout << e.what() << std::endl;
      ASSERT_TRUE(false);
   }

   std::map<BinaryData, AssetId> keyToAddrMap;
   auto checkAddresses = [&keyToAddrMap]
   (std::shared_ptr<AssetWallet_Single> wlt)->bool
   {
      auto addrMap = wlt->getUsedAddressMap();
      if (addrMap.size() != keyToAddrMap.size()) {
         return false;
      }

      for (const auto& addrPair : addrMap) {
         BinaryDataRef addrHash;
         try {
            addrHash = addrPair.second->getPrefixedHash();
         } catch (const AddressException&) {
            addrHash = addrPair.second->getScript();
         }
         auto iter = keyToAddrMap.find(addrHash);
         if (iter == keyToAddrMap.end()) {
            return false;
         }
         if (iter->second != addrPair.first) {
            return false;
         }
      }

      //grab address hashes from wallet, should match our map
      auto addrHashSet = wlt->getAddrHashSet();
      if (addrHashSet.size() != keyToAddrMap.size()) {
         return false;
      }
      for (const auto& addrHash : addrHashSet) {
         if (keyToAddrMap.find(addrHash) == keyToAddrMap.end()) {
            return false;
         }
      }

      return true;
   };

   //import addr B & C
   {
      //B
      auto keyB = wltWO->importScrAddr(TestChain::scrAddrB);
      keyToAddrMap.emplace(TestChain::scrAddrB, keyB);

      //C
      auto keyC = wltWO->importScrAddr(TestChain::scrAddrC);
      keyToAddrMap.emplace(TestChain::scrAddrC, keyC);

      //LB1 PS2H
      auto keyLB1_P2SH = wltWO->importScrAddr(TestChain::lb1ScrAddrP2SH);
      keyToAddrMap.emplace(TestChain::lb1ScrAddrP2SH, keyLB1_P2SH);

      //LB1
      auto keyLB1 = wltWO->importRawScript(TestChain::lb1ScrAddr);
      keyToAddrMap.emplace(TestChain::lb1ScrAddr, keyLB1);
   }
   ASSERT_TRUE(checkAddresses(wltWO));

   //import addr D
   {
      auto keyD = wltWO->importScrAddr(TestChain::scrAddrD);
      keyToAddrMap.emplace(TestChain::scrAddrD, keyD);
   }
   ASSERT_TRUE(checkAddresses(wltWO));

   /* shutdown the wallet, reload and check addresses again */
   const auto wltPath = wltWO->getDbFilename();
   wltWO.reset();
   ASSERT_EQ(wltWO, nullptr);

   {
      auto wlt = AssetWallet::loadMainWalletFromFile({wltPath, nullptr});
      wltWO = std::dynamic_pointer_cast<AssetWallet_Single>(wlt);
      ASSERT_NE(wltWO, nullptr);
   }
   ASSERT_TRUE(checkAddresses(wltWO));

   //import addr E
   {
      auto keyE = wltWO->importScrAddr(TestChain::scrAddrE);
      keyToAddrMap.emplace(TestChain::scrAddrE, keyE);

      //LB2 PS2H
      auto keyLB2_P2SH = wltWO->importScrAddr(TestChain::lb2ScrAddrP2SH);
      keyToAddrMap.emplace(TestChain::lb2ScrAddrP2SH, keyLB2_P2SH);

      //LB1
      auto keyLB2 = wltWO->importRawScript(TestChain::lb2ScrAddr);
      keyToAddrMap.emplace(TestChain::lb2ScrAddr, keyLB2);
   }
   ASSERT_TRUE(checkAddresses(wltWO));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletsTest, ImportPrivateKeys)
{
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{100ms, 0, SecureBinaryData::fromString("privpass")},
      Passphrase::SetNew{1ms, 0, {}},
      nullptr, 10
   };

   //create a full wallet
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP32(
         Seeds::SeedType::BIP32_Structured));
   auto wlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   //setup the import account
   try {
      auto importAccId = wlt->setupImportAccount();
      ASSERT_EQ(importAccId.toHexStr(), "00001200");

      auto importAddrAcc = wlt->getAccountForID(importAccId);
      ASSERT_NE(importAddrAcc, nullptr);

      auto outerAcc = importAddrAcc->getOuterAccount();
      auto importAcc = dynamic_cast<Accounts::AssetAccount_Imports*>(outerAcc.get());
      ASSERT_NE(importAcc, nullptr);
   } catch (const std::exception& e) {
      std::cout << e.what() << std::endl;
      ASSERT_TRUE(false);
   }

   //import priv keys for address B, C, D & E
   std::map<BinaryData, AssetId> keyToAddrMap;
   unsigned unlockCount = 0;
   {
      //set unlock lambda
      auto unlockFunc = [&unlockCount](const std::set<Wallets::EncryptionKeyId>&)
      ->Passphrase::Result
      {
         ++unlockCount;
         return {SecureBinaryData::fromString("privpass"), true};
      };
      auto lock = wlt->lockDecryptedContainer(unlockFunc);

      SecureBinaryData keyB{TestChain::privKeyAddrB};
      auto idB = wlt->importPrivateKey(keyB,
         AddressEntryType(AddressEntryType::P2PKH | AddressEntryType::Uncompressed));
      keyToAddrMap.emplace(TestChain::scrAddrB, idB);

      SecureBinaryData keyC{TestChain::privKeyAddrC};
      auto idC = wlt->importPrivateKey(keyC,
         AddressEntryType(AddressEntryType::P2PKH | AddressEntryType::Uncompressed));
      keyToAddrMap.emplace(TestChain::scrAddrC, idC);

      SecureBinaryData keyD{TestChain::privKeyAddrD};
      auto idD = wlt->importPrivateKey(keyD,
         AddressEntryType(AddressEntryType::P2PKH | AddressEntryType::Uncompressed));
      keyToAddrMap.emplace(TestChain::scrAddrD, idD);

      SecureBinaryData keyE{TestChain::privKeyAddrE};
      auto idE = wlt->importPrivateKey(keyE,
         AddressEntryType(AddressEntryType::P2PKH | AddressEntryType::Uncompressed));
      keyToAddrMap.emplace(TestChain::scrAddrE, idE);
   }
   ASSERT_EQ(unlockCount, 1);

   {
      auto hashSet = wlt->getAddrHashSet();
      EXPECT_EQ(hashSet.size(), 84);

      auto iter = hashSet.find(TestChain::scrAddrB);
      ASSERT_FALSE(iter == hashSet.end());

      iter = hashSet.find(TestChain::scrAddrC);
      ASSERT_FALSE(iter == hashSet.end());

      iter = hashSet.find(TestChain::scrAddrD);
      ASSERT_FALSE(iter == hashSet.end());

      iter = hashSet.find(TestChain::scrAddrE);
      ASSERT_FALSE(iter == hashSet.end());
   }

   //cleanup and reload the wallet
   const auto wltPath = wlt->getDbFilename();
   wlt.reset();
   ASSERT_EQ(wlt, nullptr);

   std::shared_ptr<AssetWallet_Single> wltLoaded;
   {
      auto load = AssetWallet::loadMainWalletFromFile({wltPath, nullptr});
      wltLoaded = std::dynamic_pointer_cast<AssetWallet_Single>(load);
   }
   ASSERT_NE(wltLoaded, nullptr);

   {
      auto hashSet = wltLoaded->getAddrHashSet();
      EXPECT_EQ(hashSet.size(), 84);

      auto iter = hashSet.find(TestChain::scrAddrB);
      ASSERT_FALSE(iter == hashSet.end());

      iter = hashSet.find(TestChain::scrAddrC);
      ASSERT_FALSE(iter == hashSet.end());

      iter = hashSet.find(TestChain::scrAddrD);
      ASSERT_FALSE(iter == hashSet.end());

      iter = hashSet.find(TestChain::scrAddrE);
      ASSERT_FALSE(iter == hashSet.end());
   }
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

      Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir" },
         Config::ProcessType::DB);
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      Config::reset();

      FileUtils::removeDirectory(homedir_);
   }
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletMetaDataTest, PeerStore)
{
   using namespace NetworkPeers;

   auto peersWalletPass = SecureBinaryData::fromString("authpeerpass");
   auto peerPassLbd = [&peersWalletPass]()->std::unique_ptr<Passphrase::Params>
   {
      return std::make_unique<Passphrase::Params>(
         100ms, 0, peersWalletPass);
   };
   IO::CreateFileParams createFileParams{homedir_ / "test.peers", {peerPassLbd}};
   IO::ReadOnlyFileParams roFileParams{
      homedir_ / "test.peers",
      [&peersWalletPass](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      { return { peersWalletPass, true }; }
   };
   PeerStore::bootstrapWallet(createFileParams);
   auto clientPeers = std::make_unique<ClientStore>(roFileParams);

   //auth meta account expects valid pubkeys
   auto privKey1 = Cryptography::PRNG::generateRandomStrong(32);
   auto pubkey1 = Cryptography::ECDSA::computePublicKey(privKey1, true);
   clientPeers->addPeer(
      PeerKey{pubkey1, PeerType::ServerOneWay},
      {"1.1.1.1", "0123::4567::89ab::cdef::", "test.com"}, "key1");

   auto privKey2 = Cryptography::PRNG::generateRandomStrong(32);
   auto pubkey2 = Cryptography::ECDSA::computePublicKey(privKey2, true);
   clientPeers->addPeer(
      PeerKey{pubkey2, PeerType::ServerOneWay},
      {"2.2.2.2", "domain.com"}, "key2");

   auto privKey3 = Cryptography::PRNG::generateRandomStrong(32);
   auto pubkey3 = Cryptography::ECDSA::computePublicKey(privKey3, true);
   std::string domain_name("anotherdomain.com");
   clientPeers->addPeer(
      PeerKey{pubkey3, PeerType::ServerOneWay},
      {"3.3.3.3", "test.com", domain_name}, "key3");

   try {
      auto privKeyFail = Cryptography::PRNG::generateRandomStrong(32);
      auto pubkeyFail = Cryptography::ECDSA::computePublicKey(privKeyFail, true);
      clientPeers->addPeer(
         PeerKey{pubkeyFail, PeerType::ServerOneWay},
         {"10.10.10.10", "test.com", "own"}, {});
   } catch (const NetworkPeers::Exception& e) {
      ASSERT_EQ(e.what(), std::string{"use of a reserved name"});
   }

   {
      //check peer object has expected values
      auto view = clientPeers->getView(PeerType::ServerOneWay);
      const auto& peerMap = view->getPeerNameMap();
      const auto& pubkeySet = view->getPublicKeyMap();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, iter3->second);
         EXPECT_EQ(iter1->second, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey1), std::string{"key1"});
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, pubkey2);
         EXPECT_TRUE(pubkeySet.find(pubkey2) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey2), std::string{"key2"});
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, iter3->second);
         EXPECT_EQ(iter1->second, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey3), std::string{"key3"});
      }
   }

   //delete auth peer object, reload and test again
   clientPeers.reset();
   clientPeers = std::make_unique<ClientStore>(roFileParams);

   {
      //check peer object has expected values
      auto view = clientPeers->getView(PeerType::ServerOneWay);
      const auto& peerMap = view->getPeerNameMap();
      const auto& pubkeySet = view->getPublicKeyMap();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, iter3->second);
         EXPECT_EQ(iter1->second, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey1), std::string{"key1"});
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, pubkey2);
         EXPECT_TRUE(pubkeySet.find(pubkey2) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey2), std::string{"key2"});
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, iter3->second);
         EXPECT_EQ(iter1->second, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey3), std::string{"key3"});
      }
   }

   //add more keys
   auto privKey4 = Cryptography::PRNG::generateRandomStrong(32);
   auto pubkey4 = Cryptography::ECDSA::computePublicKey(privKey4, true);
   btc_pubkey btckey4;
   btc_pubkey_init(&btckey4);
   std::memcpy(btckey4.pubkey, pubkey4.getPtr(), 33);
   btckey4.compressed = true;
   clientPeers->addPeer(
      PeerKey{btckey4, PeerType::ServerTwoWay},
      {"4.4.4.4", "more.com"}, "key4");

   auto privKey5 = Cryptography::PRNG::generateRandomStrong(32);
   auto pubkey5 = Cryptography::ECDSA::computePublicKey(privKey5, true);
   btc_pubkey btckey5;
   btc_pubkey_init(&btckey5);
   std::memcpy(btckey5.pubkey, pubkey5.getPtr(), 33);
   btckey5.compressed = true;
   clientPeers->addPeer(
      PeerKey{btckey5, PeerType::ServerTwoWay},
      {"5.5.5.5", "newdomain.com"}, "key5");

   {
      //check peer object has expected values
      auto view = clientPeers->getView(PeerType::ServerOneWay);
      const auto& peerMap = view->getPeerNameMap();
      const auto& pubkeySet = view->getPublicKeyMap();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, iter3->second);
         EXPECT_EQ(iter1->second, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey1), std::string{"key1"});
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, pubkey2);
         EXPECT_TRUE(pubkeySet.find(pubkey2) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey2), std::string{"key2"});
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, iter3->second);

         //convert btc_pubkey to sbd
         EXPECT_EQ(iter1->second, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey3), std::string{"key3"});
      }
   }

   {
      //check peer object has expected values
      auto view = clientPeers->getView(PeerType::ServerTwoWay);
      const auto& peerMap = view->getPeerNameMap();
      const auto& pubkeySet = view->getPublicKeyMap();

      {
         //4th peer
         auto iter1 = peerMap.find("4.4.4.4");
         auto iter2 = peerMap.find("more.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(memcmp(iter1->second.getPtr(), btckey4.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(pubkeySet.find(pubkey4) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey4), std::string{"key4"});
      }

      {
         //5th peer
         auto iter1 = peerMap.find("5.5.5.5");
         auto iter2 = peerMap.find("newdomain.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(memcmp(iter1->second.getPtr(), btckey5.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(pubkeySet.find(pubkey5) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey5), std::string{"key5"});
      }
   }

   //remove entries, check again
   clientPeers->eraseName(domain_name, PeerType::ServerOneWay);
   clientPeers->eraseKey(pubkey2, PeerType::ServerOneWay);
   clientPeers->eraseName("5.5.5.5", PeerType::ServerTwoWay);
   clientPeers->eraseKey(btckey4, PeerType::ServerTwoWay);
   clientPeers->setLabel(
      PeerKey{pubkey1, PeerType::ServerOneWay}, "updated key1");
   try {
      clientPeers->setLabel(PeerKey{pubkey2, PeerType::ServerOneWay}, "update key2");
      ASSERT_TRUE(false);
   } catch (const std::exception& e) {
      EXPECT_EQ(e.what(), std::string{"unknown peer key"});
   }
   clientPeers->setLabel(PeerKey{pubkey5, PeerType::ServerTwoWay}, "55key");

   {
      //check peer object has expected values
      auto view = clientPeers->getView(PeerType::ServerOneWay);
      const auto& peerMap = view->getPeerNameMap();
      const auto& pubkeySet = view->getPublicKeyMap();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, iter3->second);
         EXPECT_EQ(iter1->second, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey1), std::string{"updated key1"});
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_TRUE(iter1 == peerMap.end());
         EXPECT_TRUE(iter2 == peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey2) == pubkeySet.end());

         try {
            view->getLabel(pubkey2);
            ASSERT_TRUE(false);
         } catch (const std::exception& e) {
            EXPECT_EQ(e.what(), std::string{"unknown peer key"});
         }
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(iter1->second, iter2->second);
         EXPECT_TRUE(iter3 == peerMap.end());
         EXPECT_EQ(iter1->second, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey3), std::string{"key3"});
      }
   }

   {
      //check peer object has expected values
      auto view = clientPeers->getView(PeerType::ServerTwoWay);
      const auto& peerMap = view->getPeerNameMap();
      const auto& pubkeySet = view->getPublicKeyMap();

      {
         //4th peer
         auto iter1 = peerMap.find("4.4.4.4");
         auto iter2 = peerMap.find("more.com");

         EXPECT_EQ(iter1, peerMap.end());
         EXPECT_EQ(iter2, peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey4) == pubkeySet.end());

         try {
            view->getLabel(pubkey4);
            ASSERT_TRUE(false);
         } catch (const std::exception& e) {
            EXPECT_EQ(e.what(), std::string{"unknown peer key"});
         }
      }

      {
         //5th peer
         auto iter1 = peerMap.find("5.5.5.5");
         auto iter2 = peerMap.find("newdomain.com");

         EXPECT_EQ(iter1, peerMap.end());
         EXPECT_EQ(memcmp(iter2->second.getPtr(), btckey5.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(pubkeySet.find(pubkey5) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey5), std::string{"55key"});
      }

   }

   //delete auth peer object, reload and test again
   clientPeers.reset();
   clientPeers = std::make_unique<ClientStore>(roFileParams);

   {
      //check peer object has expected values
      auto view = clientPeers->getView(PeerType::ServerOneWay);
      const auto& peerMap = view->getPeerNameMap();
      const auto& pubkeySet = view->getPublicKeyMap();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, iter3->second);
         EXPECT_EQ(iter1->second, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey1), std::string{"updated key1"});
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_TRUE(iter1 == peerMap.end());
         EXPECT_TRUE(iter2 == peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey2) == pubkeySet.end());

         try {
            view->getLabel(pubkey2);
            ASSERT_TRUE(false);
         } catch (const std::exception& e) {
            EXPECT_EQ(e.what(), std::string{"unknown peer key"});
         }
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(iter1->second, iter2->second);
         EXPECT_TRUE(iter3 == peerMap.end());
         EXPECT_EQ(iter1->second, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey3), std::string{"key3"});
      }
   }

   {
      //check peer object has expected values
      auto view = clientPeers->getView(PeerType::ServerTwoWay);
      const auto& peerMap = view->getPeerNameMap();
      const auto& pubkeySet = view->getPublicKeyMap();

      {
         //4th peer
         auto iter1 = peerMap.find("4.4.4.4");
         auto iter2 = peerMap.find("more.com");

         EXPECT_EQ(iter1, peerMap.end());
         EXPECT_EQ(iter2, peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey4) == pubkeySet.end());

         try {
            view->getLabel(pubkey4);
            ASSERT_TRUE(false);
         } catch (const std::exception& e) {
            EXPECT_EQ(e.what(), std::string{"unknown peer key"});
         }
      }

      {
         //5th peer
         auto iter1 = peerMap.find("5.5.5.5");
         auto iter2 = peerMap.find("newdomain.com");

         EXPECT_EQ(iter1, peerMap.end());
         EXPECT_EQ(memcmp(iter2->second.getPtr(), btckey5.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(pubkeySet.find(pubkey5) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey5), std::string{"55key"});
      }
   }

   //remove last name of 5th peer, check keySet entry is gone too
   clientPeers->eraseName("newdomain.com", PeerType::ServerTwoWay);

   {
      //check peer object has expected values
      auto view = clientPeers->getView(PeerType::ServerOneWay);
      const auto& peerMap = view->getPeerNameMap();
      const auto& pubkeySet = view->getPublicKeyMap();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, iter3->second);
         EXPECT_EQ(iter1->second, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey1), std::string{"updated key1"});
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_TRUE(iter1 == peerMap.end());
         EXPECT_TRUE(iter2 == peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey2) == pubkeySet.end());

         try {
            view->getLabel(pubkey2);
            ASSERT_TRUE(false);
         } catch (const std::exception& e) {
            EXPECT_EQ(e.what(), std::string{"unknown peer key"});
         }
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(iter1->second, iter2->second);
         EXPECT_TRUE(iter3 == peerMap.end());
         EXPECT_EQ(iter1->second, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3) != pubkeySet.end());
         EXPECT_EQ(view->getLabel(pubkey3), std::string{"key3"});
      }
   }

   {
      //check peer object has expected values
      auto view = clientPeers->getView(PeerType::ServerTwoWay);
      const auto& peerMap = view->getPeerNameMap();
      const auto& pubkeySet = view->getPublicKeyMap();

      {
         //4th peer
         auto iter1 = peerMap.find("4.4.4.4");
         auto iter2 = peerMap.find("more.com");

         EXPECT_EQ(iter1, peerMap.end());
         EXPECT_EQ(iter2, peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey4) == pubkeySet.end());

         try {
            view->getLabel(pubkey4);
            ASSERT_TRUE(false);
         } catch (const std::exception& e) {
            EXPECT_EQ(e.what(), std::string{"unknown peer key"});
         }
      }

      {
         //5th peer
         auto iter1 = peerMap.find("5.5.5.5");
         auto iter2 = peerMap.find("newdomain.com");

         EXPECT_EQ(iter1, peerMap.end());
         EXPECT_EQ(iter2, peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey5) == pubkeySet.end());

         try {
            view->getLabel(pubkey5);
            ASSERT_TRUE(false);
         } catch (const std::exception& e) {
            EXPECT_EQ(e.what(), std::string{"unknown peer key"});
         }
      }
   }
}

TEST_F(WalletMetaDataTest, PeerStoreMasterKey)
{
   using namespace NetworkPeers;
   auto peersWalletPass = SecureBinaryData::fromString("authpeerpass");
   auto peerPassLbd = [&peersWalletPass]()->std::unique_ptr<Passphrase::Params>
   {
      return std::make_unique<Passphrase::Params>(
         100ms, 0, peersWalletPass);
   };
   IO::CreateFileParams createFileParams{homedir_ / "test.peers", {peerPassLbd}};
   IO::ReadOnlyFileParams roFileParams{
      homedir_ / "test.peers",
      [&peersWalletPass](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      { return { peersWalletPass, true }; }
   };
   PeerStore::bootstrapWallet(createFileParams);
   auto serverPeers = std::make_unique<ServerStore>(roFileParams);

   auto privKey1 = Cryptography::PRNG::generateRandomStrong(32);
   auto pubkey1 = Cryptography::ECDSA::computePublicKey(privKey1, true);
   serverPeers->addPeer(
      PeerKey{pubkey1, PeerType::Client},
      {"1.1.1.1", "0123::4567::89ab::cdef::", "test.com"}, "key1");

   auto privKey2 = Cryptography::PRNG::generateRandomStrong(32);
   auto pubkey2 = Cryptography::ECDSA::computePublicKey(privKey2, true);
   serverPeers->addPeer(
      PeerKey{pubkey2, PeerType::Client},
      {"2.2.2.2", "domain.com"}, "key2");

   auto privKey3 = Cryptography::PRNG::generateRandomStrong(32);
   auto pubkey3 = Cryptography::ECDSA::computePublicKey(privKey3, true);
   std::string domain_name("anotherdomain.com");
   serverPeers->addPeer(
      PeerKey{pubkey3, PeerType::Client},
      {"3.3.3.3", "test.com", domain_name}, "key3");

   auto privKey6 = Cryptography::PRNG::generateRandomStrong(32);
   auto pubkey6 = Cryptography::ECDSA::computePublicKey(privKey6, true);
   PeerKey masterKey6{pubkey6, PeerType::Client};

   ASSERT_FALSE(serverPeers->setMasterKey(masterKey6));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey1));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey6));

   //set key1 as master key
   PeerKey masterKey1{pubkey1, PeerType::Client};
   ASSERT_TRUE(serverPeers->setMasterKey(masterKey1));
   ASSERT_TRUE(serverPeers->isMasterKey(pubkey1));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey6));

   //set key3 as master key
   PeerKey masterKey3{pubkey3, PeerType::Client};
   ASSERT_TRUE(serverPeers->setMasterKey(masterKey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey1));
   ASSERT_TRUE(serverPeers->isMasterKey(pubkey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey6));

   //reload wallet, check persistence
   serverPeers.reset();
   serverPeers = std::make_unique<ServerStore>(roFileParams);
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey1));
   ASSERT_TRUE(serverPeers->isMasterKey(pubkey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey6));

   //change master key, reload peers wallet and check again
   ASSERT_TRUE(serverPeers->setMasterKey(masterKey1));
   ASSERT_TRUE(serverPeers->isMasterKey(pubkey1));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey6));

   serverPeers.reset();
   serverPeers = std::make_unique<ServerStore>(roFileParams);
   ASSERT_TRUE(serverPeers->isMasterKey(pubkey1));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey6));

   //delete key1, check it's not master key anymore
   serverPeers->eraseKey(pubkey1, PeerType::Client);
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey1));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey6));

   //reload wallet, check key1 isnt master key
   serverPeers.reset();
   serverPeers = std::make_unique<ServerStore>(roFileParams);
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey1));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey6));

   //TODO: re-add key1, check it isnt master key
   serverPeers->addPeer(
      PeerKey{pubkey1, PeerType::Client},
      {"1.1.1.1", "0123::4567::89ab::cdef::", "test.com"}, {});
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey1));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey6));

   //reload, check key1 isnt master key
   serverPeers.reset();
   serverPeers = std::make_unique<ServerStore>(roFileParams);
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey1));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey6));

   //set key3 as master
   ASSERT_TRUE(serverPeers->setMasterKey(masterKey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey1));
   ASSERT_TRUE(serverPeers->isMasterKey(pubkey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey6));

   //reload & check
   serverPeers.reset();
   serverPeers = std::make_unique<ServerStore>(roFileParams);
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey1));
   ASSERT_TRUE(serverPeers->isMasterKey(pubkey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey6));

   //erase master key
   serverPeers->eraseMasterKey();
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey1));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey6));

   //reload & check
   serverPeers.reset();
   serverPeers = std::make_unique<ServerStore>(roFileParams);
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey1));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey3));
   ASSERT_FALSE(serverPeers->isMasterKey(pubkey6));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletMetaDataTest, AuthPeers_Ephemeral)
{
   using namespace NetworkPeers;
   auto peers = std::make_unique<ClientStore>();

   //auth meta account expects valid pubkeys
   auto privKey1 = Cryptography::PRNG::generateRandomStrong(32);
   auto pubkey1 = Cryptography::ECDSA::computePublicKey(privKey1, true);
   peers->addPeer(PeerKey{pubkey1, PeerType::ServerTwoWay},
      {"1.1.1.1", "0123::4567::89ab::cdef::", "test.com"}, {});

   auto privKey2 = Cryptography::PRNG::generateRandomStrong(32);
   auto pubkey2 = Cryptography::ECDSA::computePublicKey(privKey2, true);
   peers->addPeer(PeerKey{pubkey2, PeerType::ServerTwoWay},
      {"2.2.2.2", "domain.com"}, {});

   auto privKey3 = Cryptography::PRNG::generateRandomStrong(32);
   auto pubkey3 = Cryptography::ECDSA::computePublicKey(privKey3, true);
   std::string domain_name{"anotherdomain.com"};
   peers->addPeer(PeerKey{pubkey3, PeerType::ServerTwoWay},
      {"3.3.3.3", "test.com", domain_name}, {});

   {
      //check peer object has expected values
      auto view = peers->getView(PeerType::ServerTwoWay);
      const auto& peerMap = view->getPeerNameMap();
      const auto& pubkeySet = view->getPublicKeyMap();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, iter3->second);
         EXPECT_EQ(iter1->second, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1) != pubkeySet.end());
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, pubkey2);
         EXPECT_TRUE(pubkeySet.find(pubkey2) != pubkeySet.end());
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, iter3->second);
         EXPECT_EQ(iter1->second, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3) != pubkeySet.end());
      }
   }

   //add more keys
   auto privKey4 = Cryptography::PRNG::generateRandomStrong(32);
   auto pubkey4 = Cryptography::ECDSA::computePublicKey(privKey4, true);
   btc_pubkey btckey4;
   btc_pubkey_init(&btckey4);
   std::memcpy(btckey4.pubkey, pubkey4.getPtr(), 33);
   btckey4.compressed = true;
   peers->addPeer(PeerKey{btckey4, PeerType::ServerTwoWay},
      {"4.4.4.4", "more.com"}, {});

   auto privKey5 = Cryptography::PRNG::generateRandomStrong(32);
   auto pubkey5 = Cryptography::ECDSA::computePublicKey(privKey5, true);
   btc_pubkey btckey5;
   btc_pubkey_init(&btckey5);
   std::memcpy(btckey5.pubkey, pubkey5.getPtr(), 33);
   btckey5.compressed = true;
   peers->addPeer(PeerKey{btckey5, PeerType::ServerTwoWay},
      {"5.5.5.5", "newdomain.com"}, {});

   {
      //check peer object has expected values
      auto view = peers->getView(PeerType::ServerTwoWay);
      const auto& peerMap = view->getPeerNameMap();
      const auto& pubkeySet = view->getPublicKeyMap();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, iter3->second);
         EXPECT_EQ(iter1->second, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1) != pubkeySet.end());
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, pubkey2);
         EXPECT_TRUE(pubkeySet.find(pubkey2) != pubkeySet.end());
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, iter3->second);
         EXPECT_EQ(iter1->second, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3) != pubkeySet.end());
      }

      {
         //4th peer
         auto iter1 = peerMap.find("4.4.4.4");
         auto iter2 = peerMap.find("more.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(memcmp(iter1->second.getPtr(), btckey4.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(pubkeySet.find(pubkey4) != pubkeySet.end());
      }

      {
         //5th peer
         auto iter1 = peerMap.find("5.5.5.5");
         auto iter2 = peerMap.find("newdomain.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(memcmp(iter1->second.getPtr(), btckey5.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(pubkeySet.find(pubkey5) != pubkeySet.end());
      }
   }

   //remove entries, check again
   peers->eraseName(domain_name, PeerType::ServerTwoWay);
   peers->eraseKey(pubkey2, PeerType::ServerTwoWay);
   peers->eraseName("5.5.5.5", PeerType::ServerTwoWay);
   peers->eraseKey(btckey4, PeerType::ServerTwoWay);

   {
      //check peer object has expected values
      auto view = peers->getView(PeerType::ServerTwoWay);
      const auto& peerMap = view->getPeerNameMap();
      const auto& pubkeySet = view->getPublicKeyMap();

      {
         //first peer
         auto iter1 = peerMap.find("1.1.1.1");
         auto iter2 = peerMap.find("0123::4567::89ab::cdef::");
         auto iter3 = peerMap.find("test.com");

         EXPECT_EQ(iter1->second, iter2->second);
         EXPECT_EQ(iter1->second, iter3->second);
         EXPECT_EQ(iter1->second, pubkey1);
         EXPECT_TRUE(pubkeySet.find(pubkey1) != pubkeySet.end());
      }

      {
         //second peer
         auto iter1 = peerMap.find("2.2.2.2");
         auto iter2 = peerMap.find("domain.com");

         EXPECT_TRUE(iter1 == peerMap.end());
         EXPECT_TRUE(iter2 == peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey2) == pubkeySet.end());
      }

      {
         //third peer
         auto iter1 = peerMap.find("3.3.3.3");
         auto iter2 = peerMap.find("test.com");
         auto iter3 = peerMap.find("anotherdomain.com");

         EXPECT_NE(iter1->second, iter2->second);
         EXPECT_TRUE(iter3 == peerMap.end());
         EXPECT_EQ(iter1->second, pubkey3);
         EXPECT_TRUE(pubkeySet.find(pubkey3) != pubkeySet.end());
      }

      {
         //4th peer
         auto iter1 = peerMap.find("4.4.4.4");
         auto iter2 = peerMap.find("more.com");

         EXPECT_EQ(iter1, peerMap.end());
         EXPECT_EQ(iter2, peerMap.end());
         EXPECT_TRUE(pubkeySet.find(pubkey4) == pubkeySet.end());
      }

      {
         //5th peer
         auto iter1 = peerMap.find("5.5.5.5");
         auto iter2 = peerMap.find("newdomain.com");

         EXPECT_EQ(iter1, peerMap.end());
         EXPECT_EQ(memcmp(iter2->second.getPtr(), btckey5.pubkey, BIP151PUBKEYSIZE), 0);
         EXPECT_TRUE(pubkeySet.find(pubkey5) != pubkeySet.end());
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletMetaDataTest, AuthPeers_Unlocked)
{
   using namespace NetworkPeers;
   auto path = homedir_ / "unlocked.peers";
   {
      //create auth peers db without a control pass
      auto peersWlt = PeerStore::bootstrapWallet(
         IO::CreateFileParams{path, Passphrase::SetNew{}
      });
      ASSERT_NE(peersWlt, nullptr);
   }

   //try to load it
   try {
      auto peers = std::make_shared<ClientStore>(
         IO::ReadOnlyFileParams{path, nullptr}
      );
      ASSERT_NE(peers, nullptr);
   } catch (const std::exception&) {
      ASSERT_TRUE(false);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(WalletMetaDataTest, Comments)
{
   auto passphrase = SecureBinaryData::fromString("password");
   auto controlPass = SecureBinaryData::fromString("control");

   auto controlLbd = [controlPass](const std::set<EncryptionKeyId>&)
   ->Passphrase::Result
   {
      return { controlPass, true };
   };

   //comments
   std::map<BinaryData, std::string> commentMap {
      {READHEX("aabbccdd"), "comment1"},
      {READHEX("eeff0011"), "comment2"},
      {READHEX("22334455"), "comment3"}
   };

   //create regular wallet
   std::filesystem::path filename;
   {
      IO::CreateWalletParams params{
         homedir_,
         Passphrase::SetNew{1ms, 0, passphrase},
         Passphrase::SetNew{1ms, 0, controlPass},
         nullptr, 10
      };

      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_BIP32(Seeds::SeedType::BIP32_Structured));
      auto wlt = AssetWallet_Single::createFromSeed(
         std::move(seed), params);
      filename = wlt->getDbFilename();

      //set comments
      for (auto& commentPair : commentMap) {
         wlt->setComment(commentPair.first, commentPair.second);
      }

      //check comments
      for (auto& commentPair : commentMap) {
         EXPECT_EQ(wlt->getComment(commentPair.first), commentPair.second);
      }
   }

   {
      //shut down wallet and reload it
      auto wlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{filename, controlLbd});
      auto wltSingle = std::dynamic_pointer_cast<AssetWallet_Single>(wlt);
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
      commentMap.emplace(READHEX("66778899aa"), "comment5");
      wlt->setComment(READHEX("66778899aa"), "comment5");

      //check
      grabMap = wltSingle->getCommentMap();
      EXPECT_EQ(grabMap, commentMap);
   }

   {
      //create WO copy
      auto woCopyPath = AssetWallet::forkWatchingOnly(
         IO::ReadOnlyFileParams{filename, controlLbd},
         Passphrase::SetNew{1ms, 0, controlPass}
      );
      auto woWlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{woCopyPath, controlLbd});
      auto woSingle = std::dynamic_pointer_cast<AssetWallet_Single>(woWlt);

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

      Config::parseArgs({
         "--offline",
         "--datadir=./fakehomedir",
         "--testnet" },
         Config::ProcessType::DB);
   }

   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      Config::reset();

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
         const std::set<EncryptionKeyId>&)->Passphrase::Result
      {
         ++controlPassCount;
         return { SecureBinaryData::fromString(control), true };
      };

      //load it, newCtrl should work for the control passphrase
      auto loadedWlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{path, controlPassLbd});
      METHOD_ASSERT_NE(loadedWlt, nullptr);
      if (control.empty()) {
         METHOD_ASSERT_EQ(controlPassCount, 0U);
      } else {
         METHOD_ASSERT_EQ(controlPassCount, 1U);
      }

      //check wallet id
      EXPECT_EQ(assetWlt->getID(), loadedWlt->getID());

      //compare account types between original and restored
      auto accountIDs = assetWlt->getAccountIDs();
      auto loadedIDs = loadedWlt->getAccountIDs();
      EXPECT_EQ(accountIDs, loadedIDs);

      //
      auto oldPassLbd = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("passphrase"), true };
      };

      unsigned keyPassCount = 0;
      auto newPassLbd = [&pass, &keyPassCount](
         const std::set<EncryptionKeyId>&)->Passphrase::Result
      {
         ++keyPassCount;
         return { SecureBinaryData::fromString(pass), true };
      };

      //compare a few assets, check priv keys if available to test passphrase
      bool hasPrivKeys = false;
      for (unsigned i=0; i<10; i++) {
         auto address = assetWlt->getNewAddress();
         auto assetID = assetWlt->getAssetIDForScrAddr(
            address->getPrefixedHash());

         //
         auto newAddr = loadedWlt->getNewAddress();
         auto newID = loadedWlt->getAssetIDForScrAddr(
            newAddr->getPrefixedHash());

         //
         EXPECT_EQ(address->getPrefixedHash(), newAddr->getPrefixedHash());
         EXPECT_EQ(assetID.first, newID.first);

         auto newAsset = loadedWlt->getAssetForID(newID.first);
         auto newAssetSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(newAsset);
         if (newAssetSingle->hasPrivateKey()) {
            hasPrivKeys = true;
            auto newLock = loadedWlt->lockDecryptedContainer(newPassLbd);
            auto singleWlt = std::dynamic_pointer_cast<AssetWallet_Single>(loadedWlt);
            auto newKey = singleWlt->getDecryptedPrivateKeyForAsset(newAssetSingle);

            auto asset = assetWlt->getAssetForID(assetID.first);
            auto assetSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset);
            auto lock = assetWlt->lockDecryptedContainer(oldPassLbd);
            auto privKey = assetWlt->getDecryptedPrivateKeyForAsset(assetSingle);

            EXPECT_EQ(privKey, newKey);
         }
      }

      if (hasPrivKeys) {
         METHOD_ASSERT_EQ(keyPassCount, 10U);
      } else {
         METHOD_ASSERT_EQ(keyPassCount, 0U);
      }
      return true;
   }
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, Easy16)
{
   for (const auto& index : Seeds::Easy16Codec::eligibleIndexes) {
      auto root = Cryptography::PRNG::generateRandomStrong(32);

      //encode the root
      auto encoded = Seeds::Easy16Codec::encode(root.getRef(), index);
      ASSERT_EQ(encoded.size(), 2ULL);

      auto decoded = Seeds::Easy16Codec::decode(encoded);
      ASSERT_EQ(decoded.checksumIndexes.size(), 2ULL);
      EXPECT_EQ(decoded.checksumIndexes[0], (uint8_t)index);
      EXPECT_EQ(decoded.checksumIndexes[1], (uint8_t)index);
      EXPECT_EQ(decoded.data, root);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, Easy16_Repair)
{
   /*NOTE: this test will lead to a lot of hashing*/
   auto corruptLine = [](std::vector<SecureBinaryData>& lines,
      uint8_t lineSelect, uint8_t wordSelect, uint8_t charSelect, uint8_t newVal)
   {
      auto& line = lines[lineSelect];
      auto wordPos = wordSelect * 5;
      if (wordSelect >= 4) {
         ++wordPos;
      }
      auto charPos = wordPos + charSelect;
      auto& val = line[charPos];
      char newChar;
      while (true) {
         newChar = Seeds::Easy16Codec::characters[newVal % 16];
         if (newChar != val) {
            break;
         }
         ++newVal;
      }
      val = newChar;
   };

   Cryptography::PRNG::Fortuna prng;

   //1 error, auto repair
   unsigned succesfulRepairs = 0;
   for (unsigned i=0; i<64; i++) {
      auto root = prng.generateRandom(32);

      //encode the root
      auto encoded = Seeds::Easy16Codec::encode(root.getRef(),
         Seeds::BackupType::Armory135c);
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
      auto decoded = Seeds::Easy16Codec::decode(corrupted);
      ASSERT_EQ(decoded.checksumIndexes.size(), 2ULL);
      if (lineSelect == 0) {
         EXPECT_NE(decoded.checksumIndexes[0], 0);
         EXPECT_EQ(decoded.checksumIndexes[1], 0);
      } else {
         EXPECT_EQ(decoded.checksumIndexes[0], 0);
         EXPECT_NE(decoded.checksumIndexes[1], 0);
      }
      EXPECT_NE(root, decoded.data);

      //attempt to repair, may fail because of collisions (no unique solution)
      try {
         auto result = Seeds::Easy16Codec::repair(decoded);
         if (result) {
            ASSERT_EQ(decoded.repairedIndexes.size(), 2ULL);
            EXPECT_EQ(decoded.repairedIndexes[0], 0);
            EXPECT_EQ(decoded.repairedIndexes[1], 0);
            EXPECT_EQ(root, decoded.data);
            ++succesfulRepairs;
         }
      } catch (const Seeds::Easy16RepairError&) {}
   }
   EXPECT_GE(succesfulRepairs, 20U);

   //2 errors, fail
   for (unsigned i=0; i<64; i++) {
      auto root = prng.generateRandom(32);

      //encode the root
      auto encoded = Seeds::Easy16Codec::encode(root.getRef(),
         Seeds::BackupType::Armory135c);
      ASSERT_EQ(encoded.size(), 2ULL);

      //corrupt 2 characters in one line
      auto randomSelection = prng.generateRandom(8);
      auto lineSelect = (uint8_t)(randomSelection.getPtr()[0]) % encoded.size();
      auto wordSelect1 = (uint8_t)(randomSelection.getPtr()[1]) % 8;
      auto charSelect1 = (uint8_t)(randomSelection.getPtr()[2]) % 4;
      auto newVal1 = (uint8_t)(randomSelection.getPtr()[3]) % 15 + 1;

      auto wordSelect2 = (uint8_t)(randomSelection.getPtr()[4]) % 8;
      if (wordSelect2 == wordSelect1) {
         wordSelect2 = (wordSelect2 + 1) % 8;
      }
      auto charSelect2 = (uint8_t)(randomSelection.getPtr()[5]) % 4;
      auto newVal2 = (uint8_t)(randomSelection.getPtr()[6]) % 15 + 1;

      auto corrupted = encoded;
      corruptLine(corrupted, lineSelect, wordSelect1, charSelect1, newVal1);
      corruptLine(corrupted, lineSelect, wordSelect2, charSelect2, newVal2);
      ASSERT_NE(encoded[lineSelect], corrupted[lineSelect]);

      //decode, should yield an incorrect value
      auto decoded = Seeds::Easy16Codec::decode(corrupted);
      ASSERT_EQ(decoded.checksumIndexes.size(), 2ULL);
      if (lineSelect == 0) {
         EXPECT_EQ(decoded.checksumIndexes[0], EASY16_INVALID_CHECKSUM_INDEX);
         EXPECT_EQ(decoded.checksumIndexes[1], 0);
      } else {
         EXPECT_EQ(decoded.checksumIndexes[0], 0);
         EXPECT_EQ(decoded.checksumIndexes[1], EASY16_INVALID_CHECKSUM_INDEX);
      }
      EXPECT_NE(root, decoded.data);

      //attempt to repair, should fail
      auto result = Seeds::Easy16Codec::repair(decoded);
      if (result) {
         EXPECT_NE(decoded.data, root);
      }
   }

   //1 error per line, broad spectrum fix attempt
   succesfulRepairs = 0;
   for (unsigned i=0; i<64; i++) {
      auto root = prng.generateRandom(32);

      //encode the root
      auto encoded = Seeds::Easy16Codec::encode(root.getRef(),
         Seeds::BackupType::Armory135c);
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
      auto decoded = Seeds::Easy16Codec::decode(corrupted);
      ASSERT_EQ(decoded.checksumIndexes.size(), 2ULL);
      EXPECT_NE(decoded.checksumIndexes[0], 0);
      EXPECT_NE(decoded.checksumIndexes[1], 0);

      //attempt to repair, may fail because of collisions (no evident solution)
      try {
         auto result = Seeds::Easy16Codec::repair(decoded);
         if (result) {
            ASSERT_EQ(decoded.repairedIndexes.size(), 2ULL);
            if (decoded.repairedIndexes[0] != decoded.repairedIndexes[1] ||
               decoded.repairedIndexes[0] != 0) {
               continue;
            }
            EXPECT_EQ(root, decoded.data);
            ++succesfulRepairs;
         }
      }
      catch (const Seeds::Easy16RepairError&) {}
   }
   EXPECT_GE(succesfulRepairs, 5U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, SecurePrint)
{
   auto root = Cryptography::PRNG::generateRandomStrong(32);
   
   //encrypt the root
   Seeds::SecurePrint spEncr;
   auto encryptedData = spEncr.encrypt(root, {});
   ASSERT_FALSE(spEncr.getPassphrase().empty());
   ASSERT_EQ(encryptedData.first.getSize(), 32ULL);
   ASSERT_EQ(encryptedData.second.getSize(), 0ULL);
   EXPECT_NE(encryptedData.first, root);

   Seeds::SecurePrint spDecr;
   auto decryptedData = spDecr.decrypt(encryptedData.first, spEncr.getPassphrase());

   ASSERT_EQ(decryptedData.getSize(), 32ULL);
   EXPECT_EQ(decryptedData, root);

   //with chaincode
   auto chaincode = Cryptography::PRNG::generateRandomStrong(32);

   Seeds::SecurePrint spWithCC;
   auto dataWithCC = spWithCC.encrypt(root, chaincode);

   ASSERT_FALSE(spWithCC.getPassphrase().empty());
   ASSERT_EQ(dataWithCC.first.getSize(), 32ULL);
   ASSERT_EQ(dataWithCC.second.getSize(), 32ULL);
   EXPECT_NE(dataWithCC.first, root);

   EXPECT_NE(spEncr.getPassphrase(), spWithCC.getPassphrase());
   EXPECT_NE(encryptedData.first, dataWithCC.first);

   Seeds::SecurePrint spDecrWithCC;
   auto decrRoot = spDecrWithCC.decrypt(dataWithCC.first, spWithCC.getPassphrase());

   ASSERT_EQ(decrRoot.getSize(), 32ULL);
   EXPECT_EQ(decrRoot, root);

   auto decrCC = spDecrWithCC.decrypt(dataWithCC.second, spWithCC.getPassphrase());
   ASSERT_EQ(decrCC.getSize(), 32ULL);
   EXPECT_EQ(decrCC, chaincode);

   //mangled passphrase
   try {
      auto mangledPass = spWithCC.getPassphrase();
      ASSERT_GE(mangledPass.getSize(), 4ULL);
      mangledPass.getPtr()[3] ^= 0xFF;

      Seeds::SecurePrint spDecrWithCC;
      auto decrypted = spDecrWithCC.decrypt(dataWithCC.first, mangledPass);
      ASSERT_FALSE(true);
   } catch (const std::runtime_error& e) {
      EXPECT_EQ(e.what(), std::string{"invalid SecurePrint passphrase"});
   }

   //jibberish passphrase
   try {
      auto passphrase = Cryptography::PRNG::generateRandomStrong(7);
      auto passhash = BtcUtils::getHash256(passphrase);
      passphrase.append(passhash.getPtr()[0] ^ 0xFF);

      auto passB58 = BinaryData::fromString(BtcUtils::base58_encode(passphrase));
      Seeds::SecurePrint spDecrWithCC;
      auto decrypted = spDecrWithCC.decrypt(dataWithCC.first, passB58);
      ASSERT_FALSE(true);
   } catch (const std::runtime_error& e) {
      EXPECT_EQ(e.what(), std::string{"invalid SecurePrint passphrase"});
   }

   //mismatched pass
   {
      Seeds::SecurePrint spDecrWithCC;
      auto decrypted = spDecrWithCC.decrypt(
         dataWithCC.first, spEncr.getPassphrase());
      EXPECT_NE(decrypted, dataWithCC.first);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_Legacy)
{
   //create a legacy wallet
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("passphrase")},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4
   };

   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory(Seeds::LegacyType::Armory135));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   std::unique_ptr<Seeds::WalletBackup> backupData;
   {
      auto passLbd = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("passphrase"), true };
      };
      auto lock = assetWlt->lockDecryptedContainer(passLbd);
      backupData = Seeds::Helpers::getWalletBackup(assetWlt, true);
   }
   auto backupEasy16 = dynamic_cast<Seeds::Backup_Easy16*>(backupData.get());

   auto newPass = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto callback = [&backupData](
      const Seeds::RestorePrompt& prompt)->Seeds::PromptReply
   {
      capnp::MallocMessageBuilder reply;
      switch (prompt.promptType)
      {
         case Seeds::RestorePromptType::ControlPassphrase:
            throw std::runtime_error("restore should not callback for a ctrl pass");

         case Seeds::RestorePromptType::PrivatePassphrase:
            throw std::runtime_error("restore should not callback for a priv pass");

         case Seeds::RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.walletId, backupData->getWalletId());
            EXPECT_EQ(prompt.backupType, Seeds::BackupType::Armory135c);
            return Seeds::PromptReply{ true, false, {} };
         }

         default:
            return Seeds::PromptReply{ false, false, {} };
      }
   };

   std::filesystem::path newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   std::filesystem::path filename;
   {
      //restore wallet
      auto backupCopy = Seeds::Backup_Easy16::fromLines({
         backupEasy16->getRoot(Seeds::LineIndex::One, false),
         backupEasy16->getRoot(Seeds::LineIndex::Two, false),
      });
      auto restoreResult = Seeds::Helpers::restoreFromBackup(
         std::move(backupCopy), callback, IO::CreateWalletParams{
            newHomeDir,
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newPass)},
            Passphrase::SetNew{},
            nullptr, 10});
      EXPECT_NE(restoreResult.wltPtr, nullptr);

      std::unique_ptr<Seeds::WalletBackup> backupData2;
      {
         auto passLbd2 = [&newPass](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         {
            return { SecureBinaryData::fromString(newPass), true };
         };
         auto lock = restoreResult.wltPtr->lockDecryptedContainer(passLbd2);

         auto newWalletSingle = std::dynamic_pointer_cast<AssetWallet_Single>(restoreResult.wltPtr);
         backupData2 = Seeds::Helpers::getWalletBackup(
            newWalletSingle, true);
      }
      auto backupEasy16_2 = dynamic_cast<Seeds::Backup_Easy16*>(backupData2.get());

      EXPECT_EQ(backupEasy16->getRoot(Seeds::LineIndex::One, false),
         backupEasy16_2->getRoot(Seeds::LineIndex::One, false));
      EXPECT_EQ(backupEasy16->getRoot(Seeds::LineIndex::Two, false),
         backupEasy16_2->getRoot(Seeds::LineIndex::Two, false));

      EXPECT_EQ(backupEasy16->getWalletId(), backupEasy16_2->getWalletId());
      filename = restoreResult.wltPtr->getDbFilename();
   }

   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, newPass, {}));
   FileUtils::removeDirectory(newHomeDir);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_Legacy_Armory200a)
{
   //create a legacy wallet
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("passphrase")},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4
   };

   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory());
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   std::unique_ptr<Seeds::WalletBackup> backupData;
   {
      auto passLbd = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("passphrase"), true };
      };
      auto lock = assetWlt->lockDecryptedContainer(passLbd);
      backupData = Seeds::Helpers::getWalletBackup(
         assetWlt, true);
   }
   auto backupEasy16 = dynamic_cast<Seeds::Backup_Easy16*>(backupData.get());

   auto newPass = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto newCtrl = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto callback = [&backupData](
      const Seeds::RestorePrompt& prompt)->Seeds::PromptReply
   {
      switch (prompt.promptType)
      {
         case Seeds::RestorePromptType::ControlPassphrase:
            throw std::runtime_error("restore should not callback for a ctrl pass");

         case Seeds::RestorePromptType::PrivatePassphrase:
            throw std::runtime_error("restore should not callback for a priv pass");

         case Seeds::RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.walletId, backupData->getWalletId());
            EXPECT_EQ(prompt.backupType, Seeds::BackupType::Armory200a);
            return Seeds::PromptReply{ true, false, {} };
         }

         default:
            return Seeds::PromptReply{ false, false, {} };
      }
   };

   std::filesystem::path newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   std::filesystem::path filename;
   {
      //restore wallet
      auto backupCopy = Seeds::Backup_Easy16::fromLines({
         backupEasy16->getRoot(Seeds::LineIndex::One, false),
         backupEasy16->getRoot(Seeds::LineIndex::Two, false),
      });
      auto restoreResult = Seeds::Helpers::restoreFromBackup(
         move(backupCopy), callback, IO::CreateWalletParams{
            newHomeDir,
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newPass)},
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newCtrl)},
            nullptr, 10});
      EXPECT_NE(restoreResult.wltPtr, nullptr);

      std::unique_ptr<Seeds::WalletBackup> backupData2;
      {
         auto passLbd2 = [&newPass](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         {
            return { SecureBinaryData::fromString(newPass), true };
         };
         auto lock = restoreResult.wltPtr->lockDecryptedContainer(passLbd2);

         auto newWalletSingle = std::dynamic_pointer_cast<AssetWallet_Single>(restoreResult.wltPtr);
         backupData2 = Seeds::Helpers::getWalletBackup(
            newWalletSingle, true);
      }
      auto backupEasy16_2 = dynamic_cast<Seeds::Backup_Easy16*>(backupData2.get());

      EXPECT_EQ(backupEasy16->getRoot(Seeds::LineIndex::One, false),
         backupEasy16_2->getRoot(Seeds::LineIndex::One, false));
      EXPECT_EQ(backupEasy16->getRoot(Seeds::LineIndex::Two, false),
         backupEasy16_2->getRoot(Seeds::LineIndex::Two, false));

      EXPECT_EQ(backupEasy16->getWalletId(), backupEasy16_2->getWalletId());
      filename = restoreResult.wltPtr->getDbFilename();
   }

   ASSERT_FALSE(filename.empty());
   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, newPass, newCtrl));
   FileUtils::removeDirectory(newHomeDir);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_Legacy_SecurePrint)
{
   //create a legacy wallet
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("passphrase")},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4
   };

   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory(Seeds::LegacyType::Armory135));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   std::unique_ptr<Seeds::WalletBackup> backupData;
   {
      auto passLbd = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("passphrase"), true };
      };
      auto lock = assetWlt->lockDecryptedContainer(passLbd);

      backupData = Seeds::Helpers::getWalletBackup(
         assetWlt, true);
   }
   auto backupEasy16 = dynamic_cast<Seeds::Backup_Easy16*>(backupData.get());

   auto newPass = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto newCtrl = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto callback = [&backupData](
      const Seeds::RestorePrompt& prompt)->Seeds::PromptReply
   {
      switch (prompt.promptType)
      {
         case Seeds::RestorePromptType::ControlPassphrase:
            throw std::runtime_error("restore should not callback for a ctrl pass");

         case Seeds::RestorePromptType::PrivatePassphrase:
            throw std::runtime_error("restore should not callback for a priv pass");

         case Seeds::RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.backupType, Seeds::BackupType::Armory135c);
            return Seeds::PromptReply{
               prompt.walletId == backupData->getWalletId(), false, {} };
         }

         default:
            return Seeds::PromptReply{ false, true, {} };
      }
   };

   std::filesystem::path newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   std::filesystem::path filename;
   {
      //try without sp pass
      try {
         auto backupCopy = Seeds::Backup_Easy16::fromLines({
            backupEasy16->getRoot(Seeds::LineIndex::One, true),
            backupEasy16->getRoot(Seeds::LineIndex::Two, true),
         });
         Seeds::Helpers::restoreFromBackup(
            move(backupCopy), callback, IO::CreateWalletParams{
               newHomeDir,
               Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newPass)},
               Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newCtrl)},
               nullptr, 10});
         ASSERT_TRUE(false);
      } catch (const Seeds::RestoreUserException& e) {
         EXPECT_EQ(e.what(), std::string{"user rejected id"});
      }

      //try with secure print now
      auto backupCopy = Seeds::Backup_Easy16::fromLines({
         backupEasy16->getRoot(Seeds::LineIndex::One, true),
         backupEasy16->getRoot(Seeds::LineIndex::Two, true)},
         backupEasy16->getSpPass());
      auto restoreResult = Seeds::Helpers::restoreFromBackup(
         std::move(backupCopy), callback, IO::CreateWalletParams{
            newHomeDir,
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newPass)},
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newCtrl)},
            nullptr, 10});
      EXPECT_NE(restoreResult.wltPtr, nullptr);

      std::unique_ptr<Seeds::WalletBackup> backupData2;
      {
         auto passLbd2 = [&newPass](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         {
            return { SecureBinaryData::fromString(newPass), true };
         };
         auto lock = restoreResult.wltPtr->lockDecryptedContainer(passLbd2);

         auto newWalletSingle = std::dynamic_pointer_cast<AssetWallet_Single>(
            restoreResult.wltPtr);
         backupData2 = Seeds::Helpers::getWalletBackup(
            newWalletSingle, true);
      }
      auto backupEasy16_2 = dynamic_cast<Seeds::Backup_Easy16*>(backupData2.get());

      EXPECT_EQ(backupEasy16->getRoot(Seeds::LineIndex::One, true),
         backupEasy16_2->getRoot(Seeds::LineIndex::One, true));
      EXPECT_EQ(backupEasy16->getRoot(Seeds::LineIndex::Two, true),
         backupEasy16_2->getRoot(Seeds::LineIndex::Two, true));
      EXPECT_EQ(backupEasy16->getWalletId(), backupEasy16_2->getWalletId());

      filename = restoreResult.wltPtr->getDbFilename();
   }

   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, newPass, newCtrl));
   FileUtils::removeDirectory(newHomeDir);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, Easy16_AutoRepair)
{
   /*NOTE: this test will lead to a lot of hashing*/
   auto corruptLine = [](std::vector<SecureBinaryData>& lines,
      uint8_t lineSelect, uint8_t wordSelect, uint8_t charSelect, uint8_t newVal)
   {
      auto& line = lines[lineSelect];
      auto wordPos = wordSelect * 5;
      if (wordSelect >= 4) {
         ++wordPos;
      }
      auto charPos = wordPos + charSelect;
      auto& val = line[charPos];
      char newChar;
      while (true) {
         newChar = Seeds::Easy16Codec::characters[newVal % 16];
         if (newChar != val) {
            break;
         }
         ++newVal;
      }
      val = newChar;
   };

   auto computeWalletID = [](const SecureBinaryData& root)->std::string
   {
      auto chaincode = BtcUtils::computeChainCode_ArmoryLegacy(root);
      auto derScheme = std::make_shared<Assets::DerivationScheme_ArmoryLegacy>(chaincode);

      auto pubkey = Cryptography::ECDSA::computePublicKey(root);
      auto asset_single = std::make_shared<Assets::AssetEntry_Single>(
         AssetId::getRootAssetId(), pubkey, nullptr);

      auto addrVec = derScheme->extendPublicChain(asset_single, 0, 0, nullptr);
      if (addrVec.size() != 1) {
         throw std::runtime_error("unexpected chain derivation output");
      }
      auto firstEntry = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(addrVec[0]);
      if (firstEntry == nullptr) {
         throw std::runtime_error("unexpected asset entry type");
      }

      auto h160 = BtcUtils::getHash160(
         firstEntry->getPubKey()->getUncompressedKey());

      BinaryWriter bw;
      bw.put_uint8_t(Config::BitcoinSettings::getPubkeyHashPrefix());
      bw.put_BinaryDataRef(h160.getSliceRef(0, 5));

      //now reverse it
      auto& data = bw.getData();
      auto ptr = data.getPtr();
      BinaryWriter bwReverse;
      for (unsigned i = 0; i < data.getSize(); i++) {
         bwReverse.put_uint8_t(ptr[data.getSize() - 1 - i]);
      }
      auto idBd = bwReverse.getData();
      return BtcUtils::base58_encode(idBd);
   };

   Cryptography::PRNG::Fortuna prng;

   //1 error, auto repair
   unsigned succesfulRepairs = 0;
   for (unsigned i=0; i<64; i++) {
      auto root = prng.generateRandom(32);
      auto wltID = computeWalletID(root);

      //encode the root
      auto encoded = Seeds::Easy16Codec::encode(
         root.getRef(), Seeds::BackupType::Armory135c);
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
      auto decoded = Seeds::Easy16Codec::decode(corrupted);
      ASSERT_EQ(decoded.checksumIndexes.size(), 2ULL);
      if (lineSelect == 0) {
         EXPECT_NE(decoded.checksumIndexes[0], 0);
         EXPECT_EQ(decoded.checksumIndexes[1], 0);
      } else {
         EXPECT_EQ(decoded.checksumIndexes[0], 0);
         EXPECT_NE(decoded.checksumIndexes[1], 0);
      }

      EXPECT_NE(root, decoded.data);

      //attempt to restore wallet from corrupted backup
      try {
         auto userPrompt = [&wltID, &decoded, &succesfulRepairs](
            const Seeds::RestorePrompt& prompt)->Seeds::PromptReply
         {
            switch (prompt.promptType)
            {
               case Seeds::RestorePromptType::ChecksumError:
               {
                  EXPECT_EQ(prompt.checksumResult.at(0), decoded.checksumIndexes[0]);
                  EXPECT_EQ(prompt.checksumResult.at(1), decoded.checksumIndexes[1]);
                  return Seeds::PromptReply{ false, false, {} };
               }

               case Seeds::RestorePromptType::Id:
               {
                  EXPECT_EQ(prompt.backupType, Seeds::BackupType::Armory135c);
                  if (prompt.walletId == wltID) {
                     ++succesfulRepairs;
                  }
                  return Seeds::PromptReply{ false, false, {} };
               }

               default:
                  return Seeds::PromptReply{ false, false, {} };
            }
         };

         auto backup = Seeds::Backup_Easy16::fromLines({
            std::string_view(corrupted[0].getCharPtr(), corrupted[0].getSize()),
            std::string_view(corrupted[1].getCharPtr(), corrupted[1].getSize())
         });
         Seeds::Helpers::restoreFromBackup(
            std::move(backup), userPrompt, IO::CreateWalletParams{
               homedir_,
               Passphrase::SetNew{1ms, 0, {}},
               Passphrase::SetNew{1ms, 0, {}},
               nullptr, 10
            });
      }
      catch (const std::exception&)
      {}
   }

   EXPECT_GE(succesfulRepairs, 20U);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_LegacyWithChaincode)
{
   //create a legacy wallet
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("passphrase")},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4
   };

   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory(
         Cryptography::PRNG::generateRandomStrong(32), Cryptography::PRNG::generateRandomStrong(32),
         Seeds::LegacyType::Armory135
   ));
   auto assetWlt = AssetWallet_Single::createFromSeed(std::move(seed), params);

   std::unique_ptr<Seeds::WalletBackup> backupData;
   {
      auto passLbd = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("passphrase"), true };
      };
      auto lock = assetWlt->lockDecryptedContainer(passLbd);

      backupData = Seeds::Helpers::getWalletBackup(
         assetWlt, true);
   }
   auto backupEasy16 = dynamic_cast<Seeds::Backup_Easy16*>(backupData.get());

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
         newChar = Seeds::Easy16Codec::characters[newVal % 16];
         if (newChar != val) {
            break;
         }
         ++newVal;
      }
      val = newChar;
   };

   auto newPass = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto newCtrl = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   std::vector<std::set<uint8_t>> corruptions{
      { 1 }, //corrupt second root line
      { 0, 2 }, //corrupt first line of root and chaincode
      { 2, 3 }, //both chaincode lines
      { 3 }, //last chaincode line
   };
   int corruptionCounter = 0;

   auto callback = [&backupData, &corruptions, &corruptionCounter](
      const Seeds::RestorePrompt& prompt)->Seeds::PromptReply
   {
      switch (prompt.promptType)
      {
         case Seeds::RestorePromptType::ControlPassphrase:
            throw std::runtime_error("restore should not callback for a ctrl pass");

         case Seeds::RestorePromptType::PrivatePassphrase:
            throw std::runtime_error("restore should not callback for a priv pass");

         case Seeds::RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.backupType, Seeds::BackupType::Armory135a);
            return Seeds::PromptReply{
               prompt.walletId == backupData->getWalletId(), false, {} };
         }

         case Seeds::RestorePromptType::ChecksumError:
         {
            auto corruptedLines = corruptions[corruptionCounter++];
            auto iter = corruptedLines.begin();
            for (const auto& linePair : prompt.checksumResult) {
               if (linePair.first == *iter) {
                  EXPECT_EQ(linePair.second, 255);
                  ++iter;
               } else {
                  EXPECT_EQ(linePair.second, 0);
               }
            }
            return Seeds::PromptReply{ false, false, {} };
         }

         default:
            return Seeds::PromptReply{ false, false, {} };
      }
   };

   std::filesystem::path newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);
   Cryptography::PRNG::Fortuna prng;

   std::filesystem::path filename;
   {
      std::vector<std::string> backupLines {
         std::string{backupEasy16->getRoot(Seeds::LineIndex::One, false)},
         std::string{backupEasy16->getRoot(Seeds::LineIndex::Two, false)},
         std::string{backupEasy16->getChaincode(Seeds::LineIndex::One, false)},
         std::string{backupEasy16->getChaincode(Seeds::LineIndex::Two, false)},
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
            auto corruptedBackup = Seeds::Backup_Easy16::fromLines({
               backupCopy[0], backupCopy[1], backupCopy[2], backupCopy[3]
            });
            Seeds::Helpers::restoreFromBackup(
               std::move(corruptedBackup), callback, IO::CreateWalletParams{
                  newHomeDir,
                  Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newPass)},
                  Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newCtrl)},
                  nullptr, 10});
            ASSERT_TRUE(false);
         } catch (const Seeds::RestoreUserException& e) {
            EXPECT_EQ(e.what(), std::string{"failed to create seed from backup"});
         }
      }

      //try with valid backup now
      auto validBackup = Seeds::Backup_Easy16::fromLines({
         backupLines[0], backupLines[1], backupLines[2], backupLines[3]});
      auto restoreResult = Seeds::Helpers::restoreFromBackup(
         std::move(validBackup), callback, IO::CreateWalletParams{
            newHomeDir,
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newPass)},
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newCtrl)},
            nullptr, 10});
      EXPECT_NE(restoreResult.wltPtr, nullptr);

      std::unique_ptr<Seeds::WalletBackup> backupData2;
      {
         auto passLbd2 = [&newPass](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         {
            return { SecureBinaryData::fromString(newPass), true };
         };
         auto lock = restoreResult.wltPtr->lockDecryptedContainer(passLbd2);

         auto newWalletSingle = std::dynamic_pointer_cast<AssetWallet_Single>(
            restoreResult.wltPtr);
         backupData2 = Seeds::Helpers::getWalletBackup(
            newWalletSingle, true);
      }
      auto backupEasy16_2 = dynamic_cast<Seeds::Backup_Easy16*>(backupData2.get());

      EXPECT_EQ(backupEasy16->getRoot(Seeds::LineIndex::One, false),
         backupEasy16_2->getRoot(Seeds::LineIndex::One, false));
      EXPECT_EQ(backupEasy16->getRoot(Seeds::LineIndex::Two, false),
         backupEasy16_2->getRoot(Seeds::LineIndex::Two, false));

      EXPECT_EQ(backupEasy16->getChaincode(Seeds::LineIndex::One, false),
         backupEasy16_2->getChaincode(Seeds::LineIndex::One, false));
      EXPECT_EQ(backupEasy16->getChaincode(Seeds::LineIndex::Two, false),
         backupEasy16_2->getChaincode(Seeds::LineIndex::Two, false));

      EXPECT_EQ(backupEasy16->getWalletId(), backupEasy16_2->getWalletId());
      filename = restoreResult.wltPtr->getDbFilename();
   }

   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, newPass, newCtrl));
   FileUtils::removeDirectory(newHomeDir);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_LegacyWithChaincode_SecurePrint)
{
   //create a legacy wallet
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("passphrase")},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4
   };

   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory(
      Cryptography::PRNG::generateRandomStrong(32), Cryptography::PRNG::generateRandomStrong(32),
      Seeds::LegacyType::Armory135));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   std::unique_ptr<Seeds::WalletBackup> backupData;
   {
      auto passLbd = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("passphrase"), true };
      };
      auto lock = assetWlt->lockDecryptedContainer(passLbd);

      backupData = Seeds::Helpers::getWalletBackup(
         assetWlt, true);
   }
   auto backupEasy16 = dynamic_cast<Seeds::Backup_Easy16*>(backupData.get());

   auto newPass = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto newCtrl = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto callback = [&backupData](
      const Seeds::RestorePrompt& prompt)->Seeds::PromptReply
   {
      switch (prompt.promptType)
      {
         case Seeds::RestorePromptType::ControlPassphrase:
            throw std::runtime_error("restore should not callback for a ctrl pass");

         case Seeds::RestorePromptType::PrivatePassphrase:
            throw std::runtime_error("restore should not callback for a priv pass");

         case Seeds::RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.backupType, Seeds::BackupType::Armory135a);
            return Seeds::PromptReply{
               prompt.walletId == backupData->getWalletId(), false, {} };
         }

         default:
            return Seeds::PromptReply{ false, false, {} };
      }
   };

   std::filesystem::path newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   std::filesystem::path filename;
   {
      //try without sp pass
      try {
         auto backupCopy = Seeds::Backup_Easy16::fromLines({
            backupEasy16->getRoot(Seeds::LineIndex::One, true),
            backupEasy16->getRoot(Seeds::LineIndex::Two, true),
            backupEasy16->getChaincode(Seeds::LineIndex::One, true),
            backupEasy16->getChaincode(Seeds::LineIndex::Two, true),
         });
         Seeds::Helpers::restoreFromBackup(
            std::move(backupCopy), callback, IO::CreateWalletParams{
               newHomeDir,
               Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newPass)},
               Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newCtrl)},
               nullptr, 10});
         ASSERT_TRUE(false);
      } catch (const Seeds::RestoreUserException& e) {
         EXPECT_EQ(e.what(), std::string{"user rejected id"});
      }

      //try with secure print now
      auto backupCopy = Seeds::Backup_Easy16::fromLines({
         backupEasy16->getRoot(Seeds::LineIndex::One, true),
         backupEasy16->getRoot(Seeds::LineIndex::Two, true),
         backupEasy16->getChaincode(Seeds::LineIndex::One, true),
         backupEasy16->getChaincode(Seeds::LineIndex::Two, true)},
         backupEasy16->getSpPass()
      );
      auto restoreResult = Seeds::Helpers::restoreFromBackup(
         std::move(backupCopy), callback, IO::CreateWalletParams{
            newHomeDir,
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newPass)},
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newCtrl)},
            nullptr, 10});
      EXPECT_NE(restoreResult.wltPtr, nullptr);

      std::unique_ptr<Seeds::WalletBackup> backupData2;
      {
         auto passLbd2 = [&newPass](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         {
            return { SecureBinaryData::fromString(newPass), true };
         };
         auto lock = restoreResult.wltPtr->lockDecryptedContainer(passLbd2);

         auto newWalletSingle = std::dynamic_pointer_cast<AssetWallet_Single>(
            restoreResult.wltPtr);
         backupData2 = Seeds::Helpers::getWalletBackup(
            newWalletSingle, true);
      }
      auto backupEasy16_2 = dynamic_cast<Seeds::Backup_Easy16*>(backupData2.get());

      EXPECT_EQ(backupEasy16->getRoot(Seeds::LineIndex::One, true),
         backupEasy16_2->getRoot(Seeds::LineIndex::One, true));
      EXPECT_EQ(backupEasy16->getRoot(Seeds::LineIndex::Two, true),
         backupEasy16_2->getRoot(Seeds::LineIndex::Two, true));

      EXPECT_EQ(backupEasy16->getChaincode(Seeds::LineIndex::One, true),
         backupEasy16_2->getChaincode(Seeds::LineIndex::One, true));
      EXPECT_EQ(backupEasy16->getChaincode(Seeds::LineIndex::Two, true),
         backupEasy16_2->getChaincode(Seeds::LineIndex::Two, true));

      EXPECT_EQ(backupEasy16->getWalletId(), backupEasy16_2->getWalletId());
      filename = restoreResult.wltPtr->getDbFilename();
   }

   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, newPass, newCtrl));
   FileUtils::removeDirectory(newHomeDir);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupString_LegacyWO)
{
   //create a legacy wallet
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("passphrase")},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4
   };

   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_Armory());
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   std::unique_ptr<Seeds::WalletBackup> backupData;
   {
      auto passLbd = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("passphrase"), true };
      };
      auto lock = assetWlt->lockDecryptedContainer(passLbd);
      backupData = Seeds::Helpers::getWalletBackup(
         assetWlt, false);
   }
   auto originalWalletId = assetWlt->getID();
   ASSERT_FALSE(originalWalletId.empty());
   auto backupEasy16 = dynamic_cast<Seeds::Backup_Easy16Public*>(backupData.get());
   ASSERT_NE(backupEasy16, nullptr);

   //cleanup the original wallet
   std::filesystem::path newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   std::filesystem::path filename;
   {
      auto backupCopy = Seeds::Backup_Easy16Public::fromLines({
         backupEasy16->getBackupId(),
         backupEasy16->getPublicRoot(Seeds::LineIndex::One),
         backupEasy16->getPublicRoot(Seeds::LineIndex::Two),
         backupEasy16->getChaincode(Seeds::LineIndex::One),
         backupEasy16->getChaincode(Seeds::LineIndex::Two)
      });

      auto callback = [&originalWalletId](
         const Seeds::RestorePrompt& prompt)->Seeds::PromptReply
      {
         switch (prompt.promptType)
         {
            case Seeds::RestorePromptType::ControlPassphrase:
               throw std::runtime_error("restore should not callback for a ctrl pass");

            case Seeds::RestorePromptType::PrivatePassphrase:
               throw std::runtime_error("restore should not callback for a priv pass");

            case Seeds::RestorePromptType::Id:
            {
               EXPECT_EQ(prompt.walletId, originalWalletId);
               EXPECT_EQ(prompt.backupType, Seeds::BackupType::Armory200a);
               return Seeds::PromptReply{ true, false, {} };
            }

            default:
               return Seeds::PromptReply{ false, false, {} };
         }
      };

      auto restoreResult = Seeds::Helpers::restoreFromBackup(
         std::move(backupCopy), callback, IO::CreateWalletParams{
            newHomeDir,
            Passphrase::SetNew{},
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("ctrl")},
            nullptr, 10
         });
      EXPECT_NE(restoreResult.wltPtr, nullptr);
      EXPECT_EQ(restoreResult.wltPtr->getID(), originalWalletId);
      filename = restoreResult.wltPtr->getDbFilename();

      auto newWalletSingle = std::dynamic_pointer_cast<AssetWallet_Single>(restoreResult.wltPtr);
      auto backupData2 = Seeds::Helpers::getWalletBackup(newWalletSingle, false);
      auto backupEasy16_2 = dynamic_cast<Seeds::Backup_Easy16Public*>(backupData2.get());
      ASSERT_NE(backupEasy16_2, nullptr);

      EXPECT_EQ(backupEasy16->getBackupId(), backupEasy16_2->getBackupId());
      EXPECT_EQ(backupEasy16->getPublicRoot(Seeds::LineIndex::One),
         backupEasy16_2->getPublicRoot(Seeds::LineIndex::One));
      EXPECT_EQ(backupEasy16->getPublicRoot(Seeds::LineIndex::Two),
         backupEasy16_2->getPublicRoot(Seeds::LineIndex::Two));
      EXPECT_EQ(backupEasy16->getChaincode(Seeds::LineIndex::One),
         backupEasy16_2->getChaincode(Seeds::LineIndex::One));
      EXPECT_EQ(backupEasy16->getChaincode(Seeds::LineIndex::Two),
         backupEasy16_2->getChaincode(Seeds::LineIndex::Two));
   }

   ASSERT_FALSE(filename.empty());
   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, {}, "ctrl"));
   FileUtils::removeDirectory(newHomeDir);
}

TEST_F(BackupTests, BackupString_LegacyStatic)
{
   const std::string walletId{"292AxMDBC"};
   std::vector<std::string_view> fullBackup{
      "oiow rfta wueg hewo  wuaj jawj rddi uufu  tusi"sv,
      "idnt enrd sjgo tgfi  esni eutw ktna ustg  arfe"sv,
      "jdtf fink jshs ewda  kkor daet kgtr eiha  ejgd"sv,
      "uaew ggod ngjk ejuu  rugf kufg awnn ofas  rhtf"sv
   };

   std::vector<std::string_view> woBackup{
      "wswg egsh oghk jnng so"sv,
      "kdrk ouid agee jttn tfaa ruun twsu jfgu nasj"sv,
      "otrr egjh farw dfuo gddh ugki fhrt dgeh geth"sv,
      "jdtf fink jshs ewda kkor daet kgtr eiha ejgd"sv,
      "uaew ggod ngjk ejuu rugf kufg awnn ofas rhtf"sv
   };

   auto backupFull = Seeds::Backup_Easy16::fromLines(fullBackup);
   auto backupWO = Seeds::Backup_Easy16Public::fromLines(woBackup);

   auto callback = [&walletId](
      const Seeds::RestorePrompt& prompt)->Seeds::PromptReply
   {
      switch (prompt.promptType)
      {
         case Seeds::RestorePromptType::ControlPassphrase:
            throw std::runtime_error("restore should not callback for a ctrl pass");

         case Seeds::RestorePromptType::PrivatePassphrase:
            throw std::runtime_error("restore should not callback for a priv pass");

         case Seeds::RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.walletId, walletId);
            EXPECT_EQ(prompt.backupType, Seeds::BackupType::Armory135a);
            return Seeds::PromptReply{ true, false, {} };
         }

         default:
            return Seeds::PromptReply{ false, false, {} };
      }
   };

   std::filesystem::path newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);
   auto fullRestore = Seeds::Helpers::restoreFromBackup(
      std::move(backupFull), callback, IO::CreateWalletParams{
         newHomeDir,
         Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("priv")},
         Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("ctrl")},
         nullptr, 10
      });

   auto woRestore = Seeds::Helpers::restoreFromBackup(
      std::move(backupWO), callback, IO::CreateWalletParams{
         newHomeDir,
         Passphrase::SetNew{},
         Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("woctrl")},
         nullptr, 10
      });
   auto filename = woRestore.wltPtr->getDbFilename();
   auto wltSingle = std::dynamic_pointer_cast<AssetWallet_Single>(
      fullRestore.wltPtr);

   std::unique_ptr<Seeds::WalletBackup> newFullBackup;
   {
      auto passLbd = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("priv"), true };
      };
      auto lock = wltSingle->lockDecryptedContainer(passLbd);

      newFullBackup = Seeds::Helpers::getWalletBackup(wltSingle, true);
   }
   auto newFullBackE16 = dynamic_cast<Seeds::Backup_Easy16*>(
      newFullBackup.get());
   ASSERT_NE(newFullBackE16, nullptr);
   EXPECT_EQ(newFullBackE16->getRoot(Seeds::LineIndex::One, false), fullBackup[0]);
   EXPECT_EQ(newFullBackE16->getRoot(Seeds::LineIndex::Two, false), fullBackup[1]);
   EXPECT_EQ(newFullBackE16->getChaincode(Seeds::LineIndex::One, false), fullBackup[2]);
   EXPECT_EQ(newFullBackE16->getChaincode(Seeds::LineIndex::Two, false), fullBackup[3]);
   EXPECT_TRUE(compareWalletWithBackup(wltSingle, filename, {}, "woctrl"));

   auto woWltSingle = std::dynamic_pointer_cast<AssetWallet_Single>(
      woRestore.wltPtr);
   auto newBackupWO = Seeds::Helpers::getWalletBackup(woWltSingle, false);
   auto newBackupPublic = dynamic_cast<Seeds::Backup_Easy16Public*>(
      newBackupWO.get());
   ASSERT_NE(newBackupPublic, nullptr);
   EXPECT_EQ(newBackupPublic->getBackupId(), woBackup[0]);
   EXPECT_EQ(newBackupPublic->getPublicRoot(Seeds::LineIndex::One), woBackup[1]);
   EXPECT_EQ(newBackupPublic->getPublicRoot(Seeds::LineIndex::Two), woBackup[2]);
   EXPECT_EQ(newBackupPublic->getChaincode(Seeds::LineIndex::One), woBackup[3]);
   EXPECT_EQ(newBackupPublic->getChaincode(Seeds::LineIndex::Two), woBackup[4]);

   auto fullWltWOBackup = Seeds::Helpers::getWalletBackup(wltSingle, false);
   auto fullBackupPublic = dynamic_cast<Seeds::Backup_Easy16Public*>(
      newBackupWO.get());
   ASSERT_NE(fullBackupPublic, nullptr);
   EXPECT_EQ(fullBackupPublic->getBackupId(), woBackup[0]);
   EXPECT_EQ(fullBackupPublic->getPublicRoot(Seeds::LineIndex::One), woBackup[1]);
   EXPECT_EQ(fullBackupPublic->getPublicRoot(Seeds::LineIndex::Two), woBackup[2]);
   EXPECT_EQ(fullBackupPublic->getChaincode(Seeds::LineIndex::One), woBackup[3]);
   EXPECT_EQ(fullBackupPublic->getChaincode(Seeds::LineIndex::Two), woBackup[4]);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_BIP32)
{
   //create a legacy wallet
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("passphrase")},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4
   };
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP32(
         Seeds::SeedType::BIP32_Structured));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   std::unique_ptr<Seeds::WalletBackup> backupData;
   {
      auto passLbd = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("passphrase"), true };
      };
      auto lock = assetWlt->lockDecryptedContainer(passLbd);

      backupData = Seeds::Helpers::getWalletBackup(
         assetWlt, true);
   }
   auto backupEasy16 = dynamic_cast<Seeds::Backup_Easy16*>(backupData.get());

   auto newPass = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto newCtrl = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto callback = [&backupData](
      const Seeds::RestorePrompt& prompt)->Seeds::PromptReply
   {
      switch (prompt.promptType)
      {
         case Seeds::RestorePromptType::ControlPassphrase:
            throw std::runtime_error("restore should not callback for a ctrl pass");

         case Seeds::RestorePromptType::PrivatePassphrase:
            throw std::runtime_error("restore should not callback for a priv pass");

         case Seeds::RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.walletId, backupData->getWalletId());
            EXPECT_EQ(prompt.backupType, Seeds::BackupType::Armory200b);
            return Seeds::PromptReply{ true, false, {} };
         }

         default:
            return Seeds::PromptReply{ false, false, {} };
      }
   };

   std::filesystem::path newHomeDir("./newhomedir");
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);
   unsigned restoreLookup = 10;

   std::filesystem::path filename;
   {
      //restore wallet
      auto backupCopy = Seeds::Backup_Easy16::fromLines({
         backupEasy16->getRoot(Seeds::LineIndex::One, false),
         backupEasy16->getRoot(Seeds::LineIndex::Two, false),
      });
      auto restoreResult = Seeds::Helpers::restoreFromBackup(
         std::move(backupCopy), callback, IO::CreateWalletParams{
            newHomeDir,
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newPass)},
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newCtrl)},
            nullptr, restoreLookup});
      ASSERT_NE(restoreResult.wltPtr, nullptr);

      std::unique_ptr<Seeds::WalletBackup> backupData2;
      {
         auto passLbd2 = [&newPass](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         {
            return { SecureBinaryData::fromString(newPass), true };
         };
         auto lock = restoreResult.wltPtr->lockDecryptedContainer(passLbd2);

         auto newWalletSingle = std::dynamic_pointer_cast<AssetWallet_Single>(
            restoreResult.wltPtr);
         backupData2 = Seeds::Helpers::getWalletBackup(
            newWalletSingle, true);
      }
      auto backupEasy16_2 = dynamic_cast<Seeds::Backup_Easy16*>(backupData2.get());

      EXPECT_EQ(backupEasy16->getRoot(Seeds::LineIndex::One, false),
         backupEasy16_2->getRoot(Seeds::LineIndex::One, false));
      EXPECT_EQ(backupEasy16->getRoot(Seeds::LineIndex::Two, false),
         backupEasy16_2->getRoot(Seeds::LineIndex::Two, false));

      EXPECT_EQ(backupEasy16->getWalletId(), backupEasy16_2->getWalletId());
      filename = restoreResult.wltPtr->getDbFilename();
   }
   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, newPass, newCtrl));

   /* BIP32 wallets should extend both main and change paths on restore */
   auto controlPassLbd = [&newCtrl](
      const std::set<EncryptionKeyId>&)->Passphrase::Result
   {
      return { SecureBinaryData::fromString(newCtrl), true };
   };
   auto restoredWlt = AssetWallet::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{filename, controlPassLbd});
   ASSERT_NE(restoredWlt, nullptr);

   //iterate through address accounts
   for (const auto& addrAccId : restoredWlt->getAccountIDs()) {
      auto addrAccPtr = restoredWlt->getAccountForID(addrAccId);
      ASSERT_NE(addrAccPtr, nullptr);

      //sanity checks
      ASSERT_GE(addrAccPtr->getAccountIdSet().size(), 2);
      auto outerAccId = addrAccPtr->getOuterAccountID();
      auto innerAccId = addrAccPtr->getInnerAccountID();
      ASSERT_TRUE(outerAccId.isValid());
      ASSERT_TRUE(innerAccId.isValid());
      ASSERT_NE(outerAccId, innerAccId);

      auto outerAcc = addrAccPtr->getAccountForID(outerAccId);
      ASSERT_NE(outerAcc, nullptr);
      auto innerAcc = addrAccPtr->getAccountForID(innerAccId);
      ASSERT_NE(innerAcc, nullptr);

      //both main and change asset accounts should be extended on restore
      EXPECT_EQ(outerAcc->getAssetCount(), restoreLookup);
      EXPECT_EQ(innerAcc->getAssetCount(), restoreLookup);
      EXPECT_EQ(outerAcc->getLastComputedIndex(), restoreLookup - 1);
      EXPECT_EQ(innerAcc->getLastComputedIndex(), restoreLookup - 1);
   }

   //cleanup
   FileUtils::removeDirectory(newHomeDir);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_BIP32_Virgin)
{
   //create a legacy wallet
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("passphrase")},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4
   };
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP32(
         Seeds::SeedType::BIP32_Virgin
      ));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   std::unique_ptr<Seeds::WalletBackup> backupData;
   {
      auto passLbd = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("passphrase"), true };
      };
      auto lock = assetWlt->lockDecryptedContainer(passLbd);

      backupData = Seeds::Helpers::getWalletBackup(
         assetWlt, true);
   }
   auto backupEasy16 = dynamic_cast<Seeds::Backup_Easy16*>(
      backupData.get());

   auto newPass = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto newCtrl = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto callback = [&backupData](
      const Seeds::RestorePrompt& prompt)->Seeds::PromptReply
   {
      switch (prompt.promptType)
      {
         case Seeds::RestorePromptType::ControlPassphrase:
            throw std::runtime_error("restore should not callback for a ctrl pass");

         case Seeds::RestorePromptType::PrivatePassphrase:
            throw std::runtime_error("restore should not callback for a priv pass");

         case Seeds::RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.walletId, backupData->getWalletId());
            EXPECT_EQ(prompt.backupType, Seeds::BackupType::Armory200c);
            return Seeds::PromptReply{ true, false, {} };
         }

         default:
            return Seeds::PromptReply{ false, false, {} };
      }
   };

   std::string newHomeDir{"./newhomedir"};
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   //restore wallet
   auto backupCopy = Seeds::Backup_Easy16::fromLines({
      backupEasy16->getRoot(Seeds::LineIndex::One, false),
      backupEasy16->getRoot(Seeds::LineIndex::Two, false),
   });
   auto restoreResult = Seeds::Helpers::restoreFromBackup(
      std::move(backupCopy), callback, IO::CreateWalletParams{
         newHomeDir,
         Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newPass)},
         Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newCtrl)},
         nullptr, 10});
   ASSERT_NE(restoreResult.wltPtr, nullptr);

   //check wallet id
   EXPECT_EQ(assetWlt->getID(), restoreResult.wltPtr->getID());

   //compare account types between original and restored
   auto loadedIDs = restoreResult.wltPtr->getAccountIDs();
   EXPECT_EQ(loadedIDs.size(), 0ULL);

   std::unique_ptr<Seeds::WalletBackup> backupData2;
   {
      auto passLbd2 = [&newPass](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString(newPass), true };
      };
      auto lock = restoreResult.wltPtr->lockDecryptedContainer(passLbd2);

      auto newWalletSingle = std::dynamic_pointer_cast<AssetWallet_Single>(
         restoreResult.wltPtr);
      backupData2 = Seeds::Helpers::getWalletBackup(
         newWalletSingle, true);
   }
   auto backupEasy16_2 = dynamic_cast<Seeds::Backup_Easy16*>(
      backupData2.get());

   EXPECT_EQ(backupEasy16->getRoot(Seeds::LineIndex::One, false),
      backupEasy16_2->getRoot(Seeds::LineIndex::One, false));
   EXPECT_EQ(backupEasy16->getRoot(Seeds::LineIndex::Two, false),
      backupEasy16_2->getRoot(Seeds::LineIndex::Two, false));

   EXPECT_EQ(backupEasy16->getWalletId(), backupEasy16_2->getWalletId());

   FileUtils::removeDirectory(newHomeDir);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_BIP32_FromBase58)
{
   auto b58seed = std::string_view{
      "tprv8ZgxMBicQKsPd9TeAdPADNnSyH9SSUUbTVeFszDE23Ki6TBB5nCefAdHkK8Fm3qMQR6sHwA56zqRmKmxnHk37JkiFzvncDqoKmPWubu7hDF"};

   auto newPass = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto newCtrl = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto callback = [](const Seeds::RestorePrompt& prompt)->Seeds::PromptReply
   {
      switch (prompt.promptType)
      {
         case Seeds::RestorePromptType::ControlPassphrase:
            throw std::runtime_error("restore should not callback for a ctrl pass");

         case Seeds::RestorePromptType::PrivatePassphrase:
            throw std::runtime_error("restore should not callback for a priv pass");

         case Seeds::RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.walletId, "poUtmfmp");
            EXPECT_EQ(prompt.backupType, Seeds::BackupType::Base58);
            return Seeds::PromptReply{ true, false, {} };
         }

         default:
            return Seeds::PromptReply{ false, false, {} };
      }
   };

   //create bip32 wallet from xpriv, check it yields same xpriv
   std::filesystem::path filename;
   {
      auto backup = Seeds::Backup_Base58::fromString(b58seed);
      auto restoreResult = Seeds::Helpers::restoreFromBackup(
         move(backup), callback, IO::CreateWalletParams{
            homedir_,
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newPass)},
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newCtrl)},
            nullptr, 10});
      ASSERT_NE(restoreResult.wltPtr, nullptr);

      std::unique_ptr<Seeds::WalletBackup> backupData;
      {
         auto passLbd = [newPass](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         {
            return { SecureBinaryData::fromString(newPass), true };
         };
         auto lock = restoreResult.wltPtr->lockDecryptedContainer(passLbd);

         auto walletSingle = std::dynamic_pointer_cast<AssetWallet_Single>(
            restoreResult.wltPtr);
         backupData = Seeds::Helpers::getWalletBackup(
            walletSingle, true);
      }
      auto backupBase58 = dynamic_cast<Seeds::Backup_Base58*>(backupData.get());
      EXPECT_EQ(backupBase58->getBase58String(), b58seed);
      filename = restoreResult.wltPtr->getDbFilename();
   }

   //load wallet from file and check xpriv again
   {
      auto controlPassLbd = [&newCtrl](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString(newCtrl), true };
      };

      //load it, newCtrl should work for the control passphrase
      auto loadedWlt = AssetWallet::loadMainWalletFromFile(
         IO::ReadOnlyFileParams{filename, controlPassLbd});
      ASSERT_NE(loadedWlt, nullptr);

      std::unique_ptr<Seeds::WalletBackup> backupData;
      {
         auto passLbd = [newPass](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         {
            return { SecureBinaryData::fromString(newPass), true };
         };
         auto lock = loadedWlt->lockDecryptedContainer(passLbd);

         auto walletSingle = std::dynamic_pointer_cast<AssetWallet_Single>(
            loadedWlt);
         backupData = Seeds::Helpers::getWalletBackup(
            walletSingle, true);
      }
      auto backupBase58 = dynamic_cast<Seeds::Backup_Base58*>(backupData.get());
      EXPECT_EQ(backupBase58->getBase58String(), b58seed);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BackupTests, BackupStrings_BIP39)
{
   //create a legacy wallet
   std::filesystem::path filename;
   IO::CreateWalletParams params{
      homedir_,
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("passphrase")},
      Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString("control")},
      nullptr, 4};
   std::unique_ptr<Seeds::ClearTextSeed> seed(
      new Seeds::ClearTextSeed_BIP39(
         Seeds::ClearTextSeed_BIP39::Dictionnary::English_Trezor));
   auto assetWlt = AssetWallet_Single::createFromSeed(
      std::move(seed), params);

   auto walletId = assetWlt->getID();
   std::unique_ptr<Seeds::WalletBackup> backupDataBIP39, backupDataArmory200d;
   {
      auto passLbd = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return { SecureBinaryData::fromString("passphrase"), true };
      };
      auto lock = assetWlt->lockDecryptedContainer(passLbd);
      backupDataBIP39 = Seeds::Helpers::getWalletBackup(
         assetWlt, true, Seeds::BackupType::BIP39);
      backupDataArmory200d = Seeds::Helpers::getWalletBackup(
         assetWlt, true, Seeds::BackupType::Armory200d);
   }
   EXPECT_EQ(walletId, backupDataBIP39->getWalletId());
   EXPECT_EQ(walletId, backupDataArmory200d->getWalletId());

   //restore Armory200d lambda
   auto newPass = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto newCtrl = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto callback = [&walletId](
      const Seeds::RestorePrompt& prompt)->Seeds::PromptReply
   {
      switch (prompt.promptType)
      {
         case Seeds::RestorePromptType::ControlPassphrase:
            throw std::runtime_error("restore should not callback for a ctrl pass");

         case Seeds::RestorePromptType::PrivatePassphrase:
            throw std::runtime_error("restore should not callback for a priv pass");

         case Seeds::RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.walletId, walletId);
            EXPECT_EQ(prompt.backupType, Seeds::BackupType::Armory200d);
            return Seeds::PromptReply{ true, false, {} };
         }

         default:
            return Seeds::PromptReply{ false, false, {} };
      }
   };

   std::string newHomeDir{"./newhomedir"};
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   {
      auto backupE16 = dynamic_cast<Seeds::Backup_Easy16*>(backupDataArmory200d.get());
      ASSERT_NE(backupE16, nullptr);
      auto backupE16Copy = Seeds::Backup_Easy16::fromLines({
         backupE16->getRoot(Seeds::LineIndex::One, false),
         backupE16->getRoot(Seeds::LineIndex::Two, false),
      });
      auto restoreResult = Seeds::Helpers::restoreFromBackup(
         std::move(backupE16Copy), callback, IO::CreateWalletParams{
            newHomeDir,
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newPass)},
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newCtrl)},
            nullptr, 10});
      ASSERT_NE(restoreResult.wltPtr, nullptr);

      std::unique_ptr<Seeds::WalletBackup> backupData2;
      {
         auto passLbd2 = [&newPass](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         {
            return { SecureBinaryData::fromString(newPass), true };
         };
         auto lock = restoreResult.wltPtr->lockDecryptedContainer(passLbd2);

         auto newWalletSingle = std::dynamic_pointer_cast<AssetWallet_Single>(restoreResult.wltPtr);
         backupData2 = Seeds::Helpers::getWalletBackup(
            newWalletSingle, true, Seeds::BackupType::Armory200d);
      }
      auto backupE16_2 = dynamic_cast<Seeds::Backup_Easy16*>(backupData2.get());

      EXPECT_EQ(backupE16->getRoot(Seeds::LineIndex::One, false),
         backupE16_2->getRoot(Seeds::LineIndex::One, false));
      EXPECT_EQ(backupE16->getRoot(Seeds::LineIndex::Two, false),
         backupE16_2->getRoot(Seeds::LineIndex::Two, false));
      EXPECT_EQ(backupE16->getWalletId(), backupE16_2->getWalletId());
      filename = restoreResult.wltPtr->getDbFilename();
   }

   EXPECT_TRUE(compareWalletWithBackup(assetWlt, filename, newPass, newCtrl));
   FileUtils::removeDirectory(newHomeDir);
   std::filesystem::create_directory(newHomeDir);

   //restore BIP39 lambda
   auto newPass2 = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto newCtrl2 = Cryptography::PRNG::generateRandomStrong(10).toHexStr();
   auto callbackBip39 = [&walletId](
      const Seeds::RestorePrompt& prompt)->Seeds::PromptReply
   {
      switch (prompt.promptType)
      {
         case Seeds::RestorePromptType::ControlPassphrase:
            throw std::runtime_error("restore should not callback for a ctrl pass");

         case Seeds::RestorePromptType::PrivatePassphrase:
            throw std::runtime_error("restore should not callback for a priv pass");

         case Seeds::RestorePromptType::Id:
         {
            EXPECT_EQ(prompt.walletId, walletId);
            EXPECT_EQ(prompt.backupType, Seeds::BackupType::BIP39);
            return Seeds::PromptReply{ true, false, {} };
         }

         default:
            return Seeds::PromptReply{ false, false, {} };
      }
   };

   //restore from mnemonic string
   {
      //restore wallet
      auto backupBIP39 = dynamic_cast<Seeds::Backup_BIP39*>(backupDataBIP39.get());
      ASSERT_NE(backupBIP39, nullptr);

      auto backupBIP39Copy = Seeds::Backup_BIP39::fromMnemonicString(
         backupBIP39->getMnemonicString());
      auto restoreResult = Seeds::Helpers::restoreFromBackup(
         std::move(backupBIP39Copy), callbackBip39, IO::CreateWalletParams{
            newHomeDir,
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newPass2)},
            Passphrase::SetNew{1ms, 0, SecureBinaryData::fromString(newCtrl2)},
            nullptr, 10});
      ASSERT_NE(restoreResult.wltPtr, nullptr);

      std::unique_ptr<Seeds::WalletBackup> backupData2;
      {
         auto passLbd2 = [&newPass2](const std::set<EncryptionKeyId>&)
         ->Passphrase::Result
         {
            return { SecureBinaryData::fromString(newPass2), true };
         };
         auto lock = restoreResult.wltPtr->lockDecryptedContainer(passLbd2);

         auto newWalletSingle = std::dynamic_pointer_cast<AssetWallet_Single>(
            restoreResult.wltPtr);
         backupData2 = Seeds::Helpers::getWalletBackup(
            newWalletSingle, true);
      }
      auto backupBIP39_2 = dynamic_cast<Seeds::Backup_BIP39*>(backupData2.get());

      EXPECT_EQ(backupBIP39->getMnemonicString(), backupBIP39_2->getMnemonicString());
      EXPECT_EQ(backupBIP39->getWalletId(), backupBIP39_2->getWalletId());
      filename = restoreResult.wltPtr->getDbFilename();

      //grab 10 addresses from restored wallet to get in sync with original
      //otherwise the comparison will fail
      for (int i=0; i<10; i++) {
         restoreResult.wltPtr->getNewAddress();
      }
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
#ifdef _WIN32
   WSADATA wsaData;
   WORD wVersion = MAKEWORD(2, 0);
   WSAStartup(wVersion, &wsaData);
#endif

   Cryptography::ECDSA::setupContext();

   srand(time(0));
   std::cout << "Running main() from gtest_main.cc\n";

   LOGDISABLESTDOUT();

   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();

   FLUSHLOG();
   CLEANUPLOG();

   Cryptography::ECDSA::shutdown();
   return exitCode;
}
