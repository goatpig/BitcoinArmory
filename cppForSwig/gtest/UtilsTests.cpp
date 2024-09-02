////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2021, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-17, goatpig                                            //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#include <chrono>
#include "TestUtils.h"
#include "hkdf.h"
#include "BlockchainDatabase/TxHashFilters.h"

using namespace std;
using namespace Armory::Signing;
using namespace Armory::Config;
using namespace Armory::Wallets::Encryption;

////////////////////////////////////////////////////////////////////////////////
// RFC 5869 (HKDF) unit tests for SHA-256.
class HKDF256Test : public ::testing::Test
{
protected:
   virtual void SetUp(void)
   {
      // Official SHA-256 test vector data from RFC 5869.
      string ikm1_hexstr = "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b";
      string salt1_hexstr = "000102030405060708090a0b0c";
      string info1_hexstr = "f0f1f2f3f4f5f6f7f8f9";
      string okm1_hexstr = "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865";
      string ikm2_hexstr = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f";
      string salt2_hexstr = "606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeaf";
      string info2_hexstr = "b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecfd0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff";
      string okm2_hexstr = "b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71cc30c58179ec3e87c14c01d5c1f3434f1d87";
      string ikm3_hexstr = "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b";
      string okm3_hexstr = "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8";

      ikm1 = READHEX(ikm1_hexstr);
      salt1 = READHEX(salt1_hexstr);
      info1 = READHEX(info1_hexstr);
      okm1 = READHEX(okm1_hexstr);
      ikm2 = READHEX(ikm2_hexstr);
      salt2 = READHEX(salt2_hexstr);
      info2 = READHEX(info2_hexstr);
      okm2 = READHEX(okm2_hexstr);
      ikm3 = READHEX(ikm3_hexstr);
      okm3 = READHEX(okm3_hexstr);
   }

   BinaryData ikm1;
   BinaryData salt1;
   BinaryData info1;
   SecureBinaryData okm1;
   BinaryData ikm2;
   BinaryData salt2;
   BinaryData info2;
   SecureBinaryData okm2;
   BinaryData ikm3;
   SecureBinaryData okm3;
};

////////////////////////////////////////////////////////////////////////////////
// Check the official RFC 5869 test vectors.
TEST_F(HKDF256Test, RFC5869Vectors)
{
   BinaryData results1(42);
   BinaryData results2(82);
   BinaryData results3(42);
   hkdf_sha256(results1.getPtr(), results1.getSize(), salt1.getPtr(),
               salt1.getSize(), ikm1.getPtr(), ikm1.getSize(), info1.getPtr(),
               info1.getSize());
   hkdf_sha256(results2.getPtr(), results2.getSize(), salt2.getPtr(),
               salt2.getSize(), ikm2.getPtr(), ikm2.getSize(), info2.getPtr(),
               info2.getSize());
   hkdf_sha256(results3.getPtr(), results3.getSize(), nullptr, 0, ikm3.getPtr(),
               ikm3.getSize(), nullptr, 0);

   EXPECT_EQ(okm1, results1);
   EXPECT_EQ(okm2, results2);
   EXPECT_EQ(okm3, results3);
}

////////////////////////////////////////////////////////////////////////////////
// Test the BIP 150/151 code here.
// BIP 151 test vectors partially taken from an old Bcoin test suite.
class BIP150_151Test : public ::testing::Test
{
protected:
   virtual void SetUp(void)
   {
      // Test vector data. Unfortunately, there are no test suites for BIP 151.
      // Test data was generated using a combination of Bcoin test results for
      // BIP 151, and private runs of libchacha20poly1305. Despite cobbling data
      // together, assume the external libraries used in BIP 151 are functioning
      // properly. This can be verified by running their test suites.
      string prvKeyClientIn_hexstr = "299ecf12fa716a9891903f05d2d22f483468c10f35cc448f5745e4ba00530e65";
      string prvKeyClientOut_hexstr = "31bb6f8dad3b2f3c76671f06cbe47ac634c47e9a6bd0f3c66e0bb6f85fbdd88c";
      string prvKeyServerIn_hexstr = "0e5e3671e90368ed865e9057ebb8cdbd0ffdaf8099bd0eb2414879f18eafacf6";
      string prvKeyServerOut_hexstr = "19a0eead9ae1d0167c6c4293a5a02de1712111f04007ae0587e0d978bb3b5010";
      string pubKeyClientIn_hexstr = "03c08a4e5a66478c65f7630162a64648dd1593e6588185ec0086e8c781398526b3";
      string pubKeyClientOut_hexstr = "0229fc11de5fe2a3b3a062a5ee6eb2e86aabb680a47128044cc1f4e92729dd8921";
      string pubKeyServerIn_hexstr = "0389cce55a124fc6de5689e23c6d64a5bb37f1a847d32a1afcdbd0e96cbb98a983";
      string pubKeyServerOut_hexstr = "02d786668c8fc58b8af96dd2567c857a4a83a76101429e3852d12c020a668c38cd";
      string ecdhCliInSrvOut_hexstr = "773d49e34bd65977b50b3f6b76a8236265fb489262d0cf3053f9152340646f00";
      string ecdhCliOutSrvIn_hexstr = "de3b244a80465b59d97f05eebb1af93eda0a667d5f0f2bc0dfa18d65d6e0c8a9";
      string k1CliInSrvOut_hexstr = "ae26351affd46a861890022eb60a4ebbfbca280e5eae425fa37dcf4406354d89";
      string k1CliOutSrvIn_hexstr = "eeaddf673bb62fa8e8a453e7aec56c8b50c03c5ff9c329319ae81f9b72be32ba";
      string k2CliInSrvOut_hexstr = "b70b3576c46477df45e8a7e8ffbd4aa2028f70c439ffb1c9f3040e20c5886d4f";
      string k2CliOutSrvIn_hexstr = "76773a0121079bfcf1fbf73a8476fc1861952b80d3e2a1e41dc8ba4e84f636be";
      string sesIDCliInSrvOut_hexstr = "71c425ce376162eb29e91744fbc1cbd86af52aad77490758382022bb0347585b";
      string sesIDCliOutSrvIn_hexstr = "ae60eb91ea2ea8cef36df26e4ab8c6cd609946ba6fd545adc21e4215af983d7d";
      string command_hexstr = "fake";
      string payload_hexstr = "deadbeef";
      string msg_hexstr = "0d0000000466616b6504000000deadbeef";
      string cliOutMsg1_hexstr = "8c7b743fc456d2f4c7cbb18ebb697ddfdb8308b29b9031fba2c50c5d160ec77bc0";
      string srvInMsg1_hexstr = "0d0000000466616b6504000000deadbeef";
      string cliOutMsg2_hexstr = "d5ce6ff902fa2936c8518ed503857134d7a062afe4c5868fd832188b8a5d84e576";
      string srvInMsg2_hexstr = "0d0000000466616b6504000000deadbeef";
      string cliOutMsg3_hexstr = "08c2b3592f53197bf1e81df1f2d36dadca27470f4f422e583e2f4ce32cd9719f1ac5a3a8e3e5a0c5f47e60cbdc81f314d030a545c31d9b632ab4e8740f756c00";
      string srvInMsg3_hexstr = "2c00000006656e6361636b21000000000000000000000000000000000000000000000000000000000000000000000000";
      string cliOutMsg4_hexstr = "c9056ffa96174f92a59e6aedc16af8a1fc394fe3a8c2639404e0dc700e5a58681c";
      string srvInMsg4_hexstr = "0d0000000466616b6504000000deadbeef";
      string srvOutMsg1_hexstr = "754bd639b31487e6e775fd336acf9cb2790323f4355ffc2cf17fcb2c6827d30a7a";
      string cliInMsg1_hexstr = "0d0000000466616b6504000000deadbeef";
      string srvOutMsg2_hexstr = "63c9868c88c78b7cdc30f9a23f1f7f8bbe2dec215a38df518c6880bf51ce11a35a";
      string cliInMsg2_hexstr = "0d0000000466616b6504000000deadbeef";
      string srvOutMsg3_hexstr = "367951da70abdc072956680a17fed98c54d4cd5fabc401576cbdce7a3e1b1bfd236152b4e55a1a9ff732f98b2b874477a25eeaf3264c0af42932c2eada06c5ab";
      string cliInMsg3_hexstr = "2c00000006656e6361636b21000000000000000000000000000000000000000000000000000000000000000000000000";
      string srvOutMsg4_hexstr = "39a790b8cc3bf027faf69622edc9ec1bfebce172d96c5bb52fc8a5f89df309f8a5";
      string cliInMsg4_hexstr = "0d0000000466616b6504000000deadbeef";

      // BIP 150
      string authchallenge1_hexstr = "68f35d94aacf218f8d73f4fcc82ab26f39af051c9fcf9af261eab8080bea6685";
      string authreply1_hexstr = "8144df9803527f833c9a628926fe99de04b15942d0d44e52d73dcdeb8c3d43412b26c1729405445bec9e35216b03a79cc51bb102cc351314fbb5a027298d3546";
      string authpropose_hexstr = "bde8e33de5a6b60651b82e2337112aebca11d351f84d9c027c7013f75701682b";
      string authpropose_1way_hexstr = "e42d5a3eec12c1b57e975ae877abd5a36ba84a7dd84eb7bda97b229ffdab5ef2";
      string authchallenge2_hexstr = "653f05a5e12a40579c8d9c782e04f3fff22c61888b8d67d7f783b1259cbf26cc";
      string authchallenge2_1way_hexstr = "2a9de34d8af544687a58b59e45d4007b1bf54643549343616f7f1281108913a5";
      string authreply2_hexstr = "0299a6086ab60af5fc4b5ccfa08d71c996cf0099a3ebb779cc42c94cfe3926294cf9505fd3835f73dcf88d114ed6c7e8956c8dec999617bb2b8b9a340c1eee22";

      prvKeyClientIn = READHEX(prvKeyClientIn_hexstr);
      prvKeyClientOut = READHEX(prvKeyClientOut_hexstr);
      prvKeyServerIn = READHEX(prvKeyServerIn_hexstr);
      prvKeyServerOut = READHEX(prvKeyServerOut_hexstr);
      pubKeyClientIn = READHEX(pubKeyClientIn_hexstr);
      pubKeyClientOut = READHEX(pubKeyClientOut_hexstr);
      pubKeyServerIn = READHEX(pubKeyServerIn_hexstr);
      pubKeyServerOut = READHEX(pubKeyServerOut_hexstr);
      ecdhCliInSrvOut = READHEX(ecdhCliInSrvOut_hexstr);
      ecdhCliOutSrvIn = READHEX(ecdhCliOutSrvIn_hexstr);
      k1CliInSrvOut = READHEX(k1CliInSrvOut_hexstr);
      k1CliOutSrvIn = READHEX(k1CliOutSrvIn_hexstr);
      k2CliInSrvOut = READHEX(k2CliInSrvOut_hexstr);
      k2CliOutSrvIn = READHEX(k2CliOutSrvIn_hexstr);
      sesIDCliInSrvOut = READHEX(sesIDCliInSrvOut_hexstr);
      sesIDCliOutSrvIn = READHEX(sesIDCliOutSrvIn_hexstr);
      command.copyFrom(command_hexstr);
      payload = READHEX(payload_hexstr);
      msg = READHEX(msg_hexstr);
      cliOutMsg1 = READHEX(cliOutMsg1_hexstr);
      srvInMsg1 = READHEX(srvInMsg1_hexstr);
      cliOutMsg2 = READHEX(cliOutMsg2_hexstr);
      srvInMsg2 = READHEX(srvInMsg2_hexstr);
      cliOutMsg3 = READHEX(cliOutMsg3_hexstr);
      srvInMsg3 = READHEX(srvInMsg3_hexstr);
      cliOutMsg4 = READHEX(cliOutMsg4_hexstr);
      srvInMsg4 = READHEX(srvInMsg4_hexstr);
      srvOutMsg1 = READHEX(srvOutMsg1_hexstr);
      cliInMsg1 = READHEX(cliInMsg1_hexstr);
      srvOutMsg2 = READHEX(srvOutMsg2_hexstr);
      cliInMsg2 = READHEX(cliInMsg2_hexstr);
      srvOutMsg3 = READHEX(srvOutMsg3_hexstr);
      cliInMsg3 = READHEX(cliInMsg3_hexstr);
      srvOutMsg4 = READHEX(srvOutMsg4_hexstr);
      cliInMsg4 = READHEX(cliInMsg4_hexstr);

      // BIP 150
      authchallenge1Data = READHEX(authchallenge1_hexstr);
      authreply1Data = READHEX(authreply1_hexstr);
      authproposeData = READHEX(authpropose_hexstr);
      authproposeData_1way = READHEX(authpropose_1way_hexstr);
      authchallenge2Data = READHEX(authchallenge2_hexstr);
      authchallenge2Data_1way = READHEX(authchallenge2_1way_hexstr);
      authreply2Data = READHEX(authreply2_hexstr);
      cli150Fingerprint = "3APoaDH59ANeNt6WbGNksbcWSpdUsZhCqrANS";

#ifndef _MSC_VER
      baseDir_ = "./input_files";
#else
      baseDir_ = "../gtest/input_files";
#endif
   }

   BinaryData prvKeyClientIn;
   BinaryData prvKeyClientOut;
   BinaryData prvKeyServerIn;
   BinaryData prvKeyServerOut;
   BinaryData pubKeyClientIn;
   BinaryData pubKeyClientOut;
   BinaryData pubKeyServerIn;
   BinaryData pubKeyServerOut;
   BinaryData ecdhCliInSrvOut;
   BinaryData ecdhCliOutSrvIn;
   BinaryData k1CliInSrvOut;
   BinaryData k1CliOutSrvIn;
   BinaryData k2CliInSrvOut;
   BinaryData k2CliOutSrvIn;
   BinaryData sesIDCliInSrvOut;
   BinaryData sesIDCliOutSrvIn;
   BinaryData command;
   BinaryData payload;
   BinaryData msg;
   BinaryData cliOutMsg1;
   BinaryData srvInMsg1;
   BinaryData cliOutMsg2;
   BinaryData srvInMsg2;
   BinaryData cliOutMsg3;
   BinaryData srvInMsg3;
   BinaryData cliOutMsg4;
   BinaryData srvInMsg4;
   BinaryData srvOutMsg1;
   BinaryData cliInMsg1;
   BinaryData srvOutMsg2;
   BinaryData cliInMsg2;
   BinaryData srvOutMsg3;
   BinaryData cliInMsg3;
   BinaryData srvOutMsg4;
   BinaryData cliInMsg4;
   BinaryData authchallenge1Data;
   BinaryData authreply1Data;
   BinaryData authproposeData;
   BinaryData authproposeData_1way;
   BinaryData authchallenge2Data;
   BinaryData authchallenge2Data_1way;
   BinaryData authreply2Data;
   string cli150Fingerprint;

   string baseDir_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BIP150_151Test, checkData_151_Only)
{
   // Run before the first test has been run. (SetUp/TearDown will be called
   // for each test. Multiple context startups/shutdowns leads to crashes.)
   startupBIP151CTX();
   startupBIP150CTX(4);

   // BIP 151 connection uses private keys we feed it. (Normally, we'd let it
   // generate its own private keys.)
   auto getpubkeymap = [](void)->const map<string, btc_pubkey>&
   {
      throw runtime_error("");
   };

   auto getprivkey = [](const BinaryDataRef&)->const SecureBinaryData&
   {
      throw runtime_error("");
   };

   auto getauthset = [](void)->const set<SecureBinaryData>&
   {
      throw runtime_error("");
   };

   AuthPeersLambdas akl1(getpubkeymap, getprivkey, getauthset);
   AuthPeersLambdas akl2(getpubkeymap, getprivkey, getauthset);

   btc_key prvKeyCliIn;
   btc_key prvKeyCliOut;
   btc_key prvKeySrvIn;
   btc_key prvKeySrvOut;
   prvKeyClientIn.copyTo(prvKeyCliIn.privkey);
   prvKeyClientOut.copyTo(prvKeyCliOut.privkey);
   prvKeyServerIn.copyTo(prvKeySrvIn.privkey);
   prvKeyServerOut.copyTo(prvKeySrvOut.privkey);
   BIP151Connection cliCon(&prvKeyCliIn, &prvKeyCliOut, akl1, false);
   BIP151Connection srvCon(&prvKeySrvIn, &prvKeySrvOut, akl2, false);

   // Set up encinit/encack directly. (Initial encinit/encack will use regular
   // Bitcoin P2P messages, which we'll skip building.) Confirm all steps
   // function properly along the way.
   BinaryData cliInEncinitCliData(ENCINITMSGSIZE);   // SRV (Out) -> CLI (In)
   BinaryData cliInEncackCliData(BIP151PUBKEYSIZE);  // CLI (In)  -> SRV (Out)
   BinaryData cliOutEncinitCliData(ENCINITMSGSIZE);  // CLI (Out) -> SRV (In)
   BinaryData cliOutEncackCliData(BIP151PUBKEYSIZE); // SRV (In)  -> CLI (Out)
   int s1 = srvCon.getEncinitData(cliInEncinitCliData.getPtr(),
                                  cliInEncinitCliData.getSize(),
                                  BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s1);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s2 = cliCon.processEncinit(cliInEncinitCliData.getPtr(),
                                  cliInEncinitCliData.getSize(),
                                  false);
   EXPECT_EQ(0, s2);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s3 = cliCon.getEncackData(cliInEncackCliData.getPtr(),
                                 cliInEncackCliData.getSize());
   EXPECT_EQ(0, s3);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s4 = srvCon.processEncack(cliInEncackCliData.getPtr(),
                                 cliInEncackCliData.getSize(),
                                 true);
   EXPECT_EQ(0, s4);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s5 = cliCon.getEncinitData(cliOutEncinitCliData.getPtr(),
                                  cliOutEncinitCliData.getSize(),
                                  BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s5);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s6 = srvCon.processEncinit(cliOutEncinitCliData.getPtr(),
                                  cliOutEncinitCliData.getSize(),
                                  false);
   EXPECT_EQ(0, s6);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s7 = srvCon.getEncackData(cliOutEncackCliData.getPtr(),
                                 cliOutEncackCliData.getSize());
   EXPECT_EQ(0, s7);
   EXPECT_TRUE(srvCon.connectionComplete());
   int s8 = cliCon.processEncack(cliOutEncackCliData.getPtr(),
                                 cliOutEncackCliData.getSize(),
                                 true);
   EXPECT_EQ(0, s8);
   EXPECT_TRUE(cliCon.connectionComplete());

   // Check the encinit/encack data the client sends on its outbound session.
   BinaryData expectedCliEncinitData(34);
   std::copy(pubKeyClientOut.getPtr(),
             pubKeyClientOut.getPtr() + 33,
             expectedCliEncinitData.getPtr());
   expectedCliEncinitData[BIP151PUBKEYSIZE] = \
      static_cast<uint8_t>(BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(pubKeyClientIn, cliInEncackCliData);
   EXPECT_EQ(expectedCliEncinitData, cliOutEncinitCliData);

   // Check the encinit/encack data the server sends on its outbound session.
   BinaryData expectedSrvEncinitData(34);
   std::copy(pubKeyServerOut.getPtr(),
             pubKeyServerOut.getPtr() + 33,
             expectedSrvEncinitData.getPtr());
   expectedSrvEncinitData[BIP151PUBKEYSIZE] = \
      static_cast<uint8_t>(BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(pubKeyServerIn, cliOutEncackCliData);
   EXPECT_EQ(expectedSrvEncinitData, cliInEncinitCliData);

   // Check the session IDs.
   BinaryData inSesID(cliCon.getSessionID(false), 32);
   BinaryData outSesID(cliCon.getSessionID(true), 32);
   EXPECT_EQ(sesIDCliInSrvOut, inSesID);
   EXPECT_EQ(sesIDCliOutSrvIn, outSesID);

   // Get that the size of the encrypted packet will be correct. The message
   // buffer is intentionally missized at first.
   auto&& cmd = BinaryData::fromString("fake");
   std::array<uint8_t, 4> payload = {0xde, 0xad, 0xbe, 0xef};
   BinaryData testMsgData(50);
   size_t finalMsgSize;
   BIP151Message testMsg(cmd.getPtr(), cmd.getSize(),
                         payload.data(), payload.size());
   testMsg.getEncStructMsg(testMsgData.getPtr(), testMsgData.getSize(),
                           finalMsgSize);
   testMsgData.resize(finalMsgSize);
   EXPECT_EQ(finalMsgSize, 17ULL);
   EXPECT_EQ(msg, testMsgData);

   // Encrypt and decrypt the first CLI -> SRV packet. Buffer is intentionally
   // oversized to show that the code works properly.
   BinaryData encMsgBuffer(testMsgData.getSize() + 16);
   int encryptRes = cliCon.assemblePacket(testMsgData.getPtr(),
                                          testMsgData.getSize(),
                                          encMsgBuffer.getPtr(),
                                          encMsgBuffer.getSize());
   EXPECT_EQ(0, encryptRes);
   EXPECT_EQ(cliOutMsg1, encMsgBuffer);
   BinaryData decMsgBuffer(testMsgData.getSize());
   int decryptRes = srvCon.decryptPacket(encMsgBuffer.getPtr(),
                                         encMsgBuffer.getSize(),
                                         decMsgBuffer.getPtr(),
                                         decMsgBuffer.getSize());
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(srvInMsg1, decMsgBuffer);

   // Encrypt and decrypt the second CLI -> SRV packet.
   encMsgBuffer.resize(testMsgData.getSize() + 16);
   encryptRes = cliCon.assemblePacket(testMsgData.getPtr(),
                                      testMsgData.getSize(),
                                      encMsgBuffer.getPtr(),
                                      encMsgBuffer.getSize());
   EXPECT_EQ(0, encryptRes);
   EXPECT_EQ(cliOutMsg2, encMsgBuffer);

   decMsgBuffer.resize(testMsgData.getSize());
   decryptRes = srvCon.decryptPacket(encMsgBuffer.getPtr(),
                                     encMsgBuffer.getSize(),
                                     decMsgBuffer.getPtr(),
                                     decMsgBuffer.getSize());
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(srvInMsg2, decMsgBuffer);

   // Rekey (CLI -> SRV) and confirm that the results are correct.
   BinaryData rekeyBuf(64);
   int rekeySendRes = cliCon.bip151RekeyConn(rekeyBuf.getPtr(),
                                             rekeyBuf.getSize());
   EXPECT_EQ(0, rekeySendRes);
   EXPECT_EQ(cliOutMsg3, rekeyBuf);
   decMsgBuffer.resize(rekeyBuf.getSize() - 16);
   decryptRes = srvCon.decryptPacket(rekeyBuf.getPtr(),
                                     rekeyBuf.getSize(),
                                     decMsgBuffer.getPtr(),
                                     decMsgBuffer.getSize());
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(srvInMsg3, decMsgBuffer);
   BIP151Message decData1(decMsgBuffer.getPtr(), decMsgBuffer.getSize());
   int rekeyProcRes = srvCon.processEncack(decData1.getPayloadPtr(),
                                           decData1.getPayloadSize(),
                                           false);
   EXPECT_EQ(0, rekeyProcRes);

   // Encrypt and decrypt the third CLI -> SRV packet.
   encMsgBuffer.resize(testMsgData.getSize() + 16);
   encryptRes = cliCon.assemblePacket(testMsgData.getPtr(),
                                      testMsgData.getSize(),
                                      encMsgBuffer.getPtr(),
                                      encMsgBuffer.getSize());
   EXPECT_EQ(0, encryptRes);
   EXPECT_EQ(cliOutMsg4, encMsgBuffer);
   decMsgBuffer.resize(testMsgData.getSize());
   decryptRes = srvCon.decryptPacket(encMsgBuffer.getPtr(),
                                     encMsgBuffer.getSize(),
                                     decMsgBuffer.getPtr(),
                                     decMsgBuffer.getSize());
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(srvInMsg4, decMsgBuffer);

   // Encrypt and decrypt the first SRV -> CLI packet.
   encMsgBuffer.resize(testMsgData.getSize() + 16);
   encryptRes = srvCon.assemblePacket(testMsgData.getPtr(),
                                      testMsgData.getSize(),
                                      encMsgBuffer.getPtr(),
                                      encMsgBuffer.getSize());
   EXPECT_EQ(0, encryptRes);
   EXPECT_EQ(srvOutMsg1, encMsgBuffer);

   decMsgBuffer.resize(testMsgData.getSize());
   decryptRes = cliCon.decryptPacket(encMsgBuffer.getPtr(),
                                     encMsgBuffer.getSize(),
                                     decMsgBuffer.getPtr(),
                                     decMsgBuffer.getSize());
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(cliInMsg1, decMsgBuffer);

   // Encrypt and decrypt the second SRV -> CLI packet.
   encMsgBuffer.resize(testMsgData.getSize() + 16);
   encryptRes = srvCon.assemblePacket(testMsgData.getPtr(),
                                      testMsgData.getSize(),
                                      encMsgBuffer.getPtr(),
                                      encMsgBuffer.getSize());
   EXPECT_EQ(0, encryptRes);
   EXPECT_EQ(srvOutMsg2, encMsgBuffer);

   decMsgBuffer.resize(testMsgData.getSize());
   decryptRes = cliCon.decryptPacket(encMsgBuffer.getPtr(),
                                     encMsgBuffer.getSize(),
                                     decMsgBuffer.getPtr(),
                                     decMsgBuffer.getSize());
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(cliInMsg2, decMsgBuffer);

   // Rekey (CLI -> SRV) and confirm that the results are correct.
   rekeySendRes = srvCon.bip151RekeyConn(rekeyBuf.getPtr(),
                                         rekeyBuf.getSize());
   EXPECT_EQ(0, rekeySendRes);
   EXPECT_EQ(srvOutMsg3, rekeyBuf);
   decMsgBuffer.resize(rekeyBuf.getSize() - 16);
   decryptRes = cliCon.decryptPacket(rekeyBuf.getPtr(),
                                     rekeyBuf.getSize(),
                                     decMsgBuffer.getPtr(),
                                     decMsgBuffer.getSize());
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(cliInMsg3, decMsgBuffer);
   BIP151Message decData2(decMsgBuffer.getPtr(), decMsgBuffer.getSize());
   rekeyProcRes = cliCon.processEncack(decData2.getPayloadPtr(),
                                       decData2.getPayloadSize(),
                                       false);
   EXPECT_EQ(0, rekeyProcRes);

   // Encrypt and decrypt the third SRV -> CLI packet.
   encMsgBuffer.resize(testMsgData.getSize() + 16);
   encryptRes = cliCon.assemblePacket(testMsgData.getPtr(),
                                      testMsgData.getSize(),
                                      encMsgBuffer.getPtr(),
                                      encMsgBuffer.getSize());
   EXPECT_EQ(0, encryptRes);
   EXPECT_EQ(srvOutMsg4, encMsgBuffer);

   decMsgBuffer.resize(testMsgData.getSize());
   decryptRes = srvCon.decryptPacket(encMsgBuffer.getPtr(),
                                     encMsgBuffer.getSize(),
                                     decMsgBuffer.getPtr(),
                                     decMsgBuffer.getSize());
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(cliInMsg4, decMsgBuffer);
}

////////////////////////////////////////////////////////////////////////////////
// Test BIP 150 and BIP 151. Establish a 151 connection first and then confirm
// that BIP 150 functions properly, with a quick check to confirm that 151 is
// still functional afterwards.
TEST_F(BIP150_151Test, checkData_150_151)
{
   // Get test files from the current (gtest) directory. C++17 would be nice
   // since filesystem::current_path() has been added. Alas, for now....
   // Test IPv4, and then IPv6 later.
   // Ideally, the code would be smart enough to support two separate contexts
   // so that two separate key sets can be tested. There's no real reason to
   // support this in Armory right now, though, and it'd be a lot of work. For
   // now, just cheat and have two "separate" systems with the same input files.

   //grab serv private key from peer files
   auto servFilePath = baseDir_;
   servFilePath.append("/bip150v0_srv1/identity-key-ipv4");
   fstream serv_isf(servFilePath);
   char prvHex[65];
   serv_isf.getline(prvHex, 65);
   SecureBinaryData privServ(READHEX(prvHex));

   //grab client private key from peer files
   auto cliFilePath = baseDir_;
   cliFilePath.append("/bip150v0_cli1/identity-key-ipv4");
   fstream cli_isf(cliFilePath);
   char cliHex[65];
   cli_isf.getline(cliHex, 65);
   SecureBinaryData privCli(READHEX(cliHex));
   
   //compute public keys
   auto&& pubServ = CryptoECDSA().ComputePublicKey(privServ);
   pubServ = CryptoECDSA().CompressPoint(pubServ);
   
   auto&& pubCli = CryptoECDSA().ComputePublicKey(privCli);
   pubCli = CryptoECDSA().CompressPoint(pubCli);

   btc_pubkey servKey;
   btc_pubkey_init(&servKey);
   memcpy(servKey.pubkey, pubServ.getPtr(), BIP151PUBKEYSIZE);
   servKey.compressed = true;

   btc_pubkey clientKey;
   btc_pubkey_init(&clientKey);
   memcpy(clientKey.pubkey, pubCli.getPtr(), BIP151PUBKEYSIZE);
   clientKey.compressed = true;

   //create pubkey maps
   map<string, btc_pubkey> servMap;
   servMap.insert(make_pair("own", servKey));
   servMap.insert(make_pair("101.101.101.101:10101", clientKey));

   map<string, btc_pubkey> cliMap;
   cliMap.insert(make_pair("own", clientKey));
   cliMap.insert(make_pair("1.2.3.4:8333", servKey));

   //create privkey maps
   map<SecureBinaryData, SecureBinaryData> servPrivMap;
   servPrivMap.insert(make_pair(pubServ, privServ));

   map<SecureBinaryData, SecureBinaryData> cliPrivMap;
   cliPrivMap.insert(make_pair(pubCli, privCli));

   //create auth peer sets
   set<SecureBinaryData> servSet;
   servSet.insert(pubCli);

   set<SecureBinaryData> clientSet;
   clientSet.insert(pubServ);

   //create server auth key lambdas
   auto serv_getPubKeyMap = [&servMap](void)->const map<string, btc_pubkey>&
   {
      return servMap;
   };

   auto serv_getPrivKey = [&servPrivMap](const BinaryDataRef& pub)->const SecureBinaryData&
   {
      auto iter = servPrivMap.find(pub);
      if (iter == servPrivMap.end())
         throw runtime_error("invalid key");
      return iter->second;
   };

   auto serv_getauthset = [servSet](void)->const set<SecureBinaryData>&
   {
      return servSet;
   };

   //create client auth key lambdas
   auto cli_getPubKeyMap = [&cliMap](void)->const map<string, btc_pubkey>&
   {
      return cliMap;
   };

   auto cli_getPrivKey = [&cliPrivMap](const BinaryDataRef& pub)->const SecureBinaryData&
   {
      auto iter = cliPrivMap.find(pub);
      if (iter == cliPrivMap.end())
         throw runtime_error("invalid key");
      return iter->second;
   };

   auto cli_getauthset = [clientSet](void)->const set<SecureBinaryData>&
   {
      return clientSet;
   };

   //create AKL objects
   AuthPeersLambdas aklServ(serv_getPubKeyMap, serv_getPrivKey, serv_getauthset);
   AuthPeersLambdas aklCli(cli_getPubKeyMap, cli_getPrivKey, cli_getauthset);


   startupBIP150CTX(4);

   btc_key prvKeyCliIn;
   btc_key prvKeyCliOut;
   btc_key prvKeySrvIn;
   btc_key prvKeySrvOut;
   prvKeyClientIn.copyTo(prvKeyCliIn.privkey);
   prvKeyClientOut.copyTo(prvKeyCliOut.privkey);
   prvKeyServerIn.copyTo(prvKeySrvIn.privkey);
   prvKeyServerOut.copyTo(prvKeySrvOut.privkey);
   BIP151Connection cliCon(&prvKeyCliIn, &prvKeyCliOut, aklCli, false);
   BIP151Connection srvCon(&prvKeySrvIn, &prvKeySrvOut, aklServ, false);

   // Set up encinit/encack directly. (Initial encinit/encack will use regular
   // Bitcoin P2P messages, which we'll skip building.) Confirm all steps
   // function properly along the way.
   BinaryData cliInEncinitCliData(ENCINITMSGSIZE);   // SRV (Out) -> CLI (In)
   BinaryData cliInEncackCliData(BIP151PUBKEYSIZE);  // CLI (In)  -> SRV (Out)
   BinaryData cliOutEncinitCliData(ENCINITMSGSIZE);  // CLI (Out) -> SRV (In)
   BinaryData cliOutEncackCliData(BIP151PUBKEYSIZE); // SRV (In)  -> CLI (Out)
   int s1 = srvCon.getEncinitData(cliInEncinitCliData.getPtr(),
                                  cliInEncinitCliData.getSize(),
                                  BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s1);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s2 = cliCon.processEncinit(cliInEncinitCliData.getPtr(),
                                  cliInEncinitCliData.getSize(),
                                  false);
   EXPECT_EQ(0, s2);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s3 = cliCon.getEncackData(cliInEncackCliData.getPtr(),
                                 cliInEncackCliData.getSize());
   EXPECT_EQ(0, s3);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s4 = srvCon.processEncack(cliInEncackCliData.getPtr(),
                                 cliInEncackCliData.getSize(),
                                 true);
   EXPECT_EQ(0, s4);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s5 = cliCon.getEncinitData(cliOutEncinitCliData.getPtr(),
                                  cliOutEncinitCliData.getSize(),
                                  BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s5);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s6 = srvCon.processEncinit(cliOutEncinitCliData.getPtr(),
                                  cliOutEncinitCliData.getSize(),
                                  false);
   EXPECT_EQ(0, s6);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s7 = srvCon.getEncackData(cliOutEncackCliData.getPtr(),
                                 cliOutEncackCliData.getSize());
   EXPECT_EQ(0, s7);
   EXPECT_TRUE(srvCon.connectionComplete());
   int s8 = cliCon.processEncack(cliOutEncackCliData.getPtr(),
                                 cliOutEncackCliData.getSize(),
                                 true);
   EXPECT_EQ(0, s8);
   EXPECT_TRUE(cliCon.connectionComplete());

   // Get the fingerprint.
   string curFng = cliCon.getBIP150Fingerprint();
   EXPECT_EQ(cli150Fingerprint, curFng);

   ////////////////// Start the BIP 150 process for each side. /////////////////
   BinaryData authchallengeBuf(BIP151PRVKEYSIZE);
   BinaryData authreplyBuf(BIP151PRVKEYSIZE*2);
   BinaryData authproposeBuf(BIP151PRVKEYSIZE);
   EXPECT_EQ(BIP150State::INACTIVE, cliCon.getBIP150State());
   EXPECT_EQ(BIP150State::INACTIVE, srvCon.getBIP150State());

   // INACTIVE -> CHALLENGE1
   int b1 = cliCon.getAuthchallengeData(authchallengeBuf.getPtr(),
                                        authchallengeBuf.getSize(),
                                        "1.2.3.4:8333",
                                        true);
   EXPECT_EQ(0, b1);
   EXPECT_EQ(BIP150State::CHALLENGE1, cliCon.getBIP150State());
   EXPECT_EQ(authchallenge1Data, authchallengeBuf);
   int b2 = srvCon.processAuthchallenge(authchallengeBuf.getPtr(),
                                        authchallengeBuf.getSize(),
                                        true);
   EXPECT_EQ(0, b2);
   EXPECT_EQ(BIP150State::CHALLENGE1, srvCon.getBIP150State());

   // CHALLENGE1 -> REPLY1
   int b3 = srvCon.getAuthreplyData(authreplyBuf.getPtr(),
                                    authreplyBuf.getSize(),
                                    true);
   EXPECT_EQ(0, b3);
   EXPECT_EQ(BIP150State::REPLY1, srvCon.getBIP150State());
   EXPECT_EQ(authreply1Data, authreplyBuf);
   int b4 = cliCon.processAuthreply(authreplyBuf.getPtr(),
                                    authreplyBuf.getSize(),
                                    true);
   EXPECT_EQ(0, b4);
   EXPECT_EQ(BIP150State::REPLY1, cliCon.getBIP150State());

   // REPLY1 -> PROPOSE
   int b5 = cliCon.getAuthproposeData(authproposeBuf.getPtr(),
                                      authproposeBuf.getSize());
   EXPECT_EQ(0, b5);
   EXPECT_EQ(BIP150State::PROPOSE, cliCon.getBIP150State());
   EXPECT_EQ(authproposeData, authproposeBuf);
   int b6 = srvCon.processAuthpropose(authproposeBuf.getPtr(),
                                      authproposeBuf.getSize());
   EXPECT_EQ(0, b6);
   EXPECT_EQ(BIP150State::PROPOSE, srvCon.getBIP150State());

   // PROPOSE -> CHALLENGE2
   int b7 = srvCon.getAuthchallengeData(authchallengeBuf.getPtr(),
                                        authchallengeBuf.getSize(),
                                        "",
                                        false);
   EXPECT_EQ(0, b7);
   EXPECT_EQ(BIP150State::CHALLENGE2, srvCon.getBIP150State());
   EXPECT_EQ(authchallenge2Data, authchallengeBuf);
   int b8 = cliCon.processAuthchallenge(authchallengeBuf.getPtr(),
                                        authchallengeBuf.getSize(),
                                        false);
   EXPECT_EQ(0, b8);
   EXPECT_EQ(BIP150State::CHALLENGE2, cliCon.getBIP150State());

   // CHALLENGE2 -> REPLY2 (SUCCESS)
   int b9 = cliCon.getAuthreplyData(authreplyBuf.getPtr(),
                                    authreplyBuf.getSize(),
                                    false);
   EXPECT_EQ(0, b9);

   cliCon.bip150HandshakeRekey();
   EXPECT_EQ(BIP150State::SUCCESS, cliCon.getBIP150State());
   EXPECT_EQ(authreply2Data, authreplyBuf);
   int b10 = srvCon.processAuthreply(authreplyBuf.getPtr(),
                                     authreplyBuf.getSize(),
                                     false);
   EXPECT_EQ(0, b10);

   srvCon.bip150HandshakeRekey();
   EXPECT_EQ(BIP150State::SUCCESS, srvCon.getBIP150State());

   // See what happens when messages are received out of order.
   // INACTIVE -> CHALLENGE1  (Client)
   int b11 = cliCon.getAuthchallengeData(authchallengeBuf.getPtr(),
                                        authchallengeBuf.getSize(),
                                        "1.2.3.4:8333",
                                        true);
   EXPECT_EQ(0, b11);
   EXPECT_EQ(BIP150State::CHALLENGE1, cliCon.getBIP150State());
   EXPECT_EQ(authchallenge1Data, authchallengeBuf);

   // CHALLENGE1 -> PROPOSE  (Client)
   int b12 = cliCon.getAuthproposeData(authproposeBuf.getPtr(),
                                       authproposeBuf.getSize());
   EXPECT_EQ(-1, b12);
   EXPECT_EQ(BIP150State::ERR_STATE, cliCon.getBIP150State());
}

TEST_F(BIP150_151Test, checkData_150_151_1Way)
{
   // Get test files from the current (gtest) directory. C++17 would be nice
   // since filesystem::current_path() has been added. Alas, for now....
   // Test IPv4, and then IPv6 later.
   // Ideally, the code would be smart enough to support two separate contexts
   // so that two separate key sets can be tested. There's no real reason to
   // support this in Armory right now, though, and it'd be a lot of work. For
   // now, just cheat and have two "separate" systems with the same input files.

   //grab serv private key from peer files
   auto servFilePath = baseDir_;
   servFilePath.append("/bip150v0_srv1/identity-key-ipv4");
   fstream serv_isf(servFilePath);
   char prvHex[65];
   serv_isf.getline(prvHex, 65);
   SecureBinaryData privServ(READHEX(prvHex));

   //grab client private key from peer files
   auto cliFilePath = baseDir_;
   cliFilePath.append("/bip150v0_cli1/identity-key-ipv4");
   fstream cli_isf(cliFilePath);
   char cliHex[65];
   cli_isf.getline(cliHex, 65);
   SecureBinaryData privCli(READHEX(cliHex));
   
   //compute public keys
   auto&& pubServ = CryptoECDSA().ComputePublicKey(privServ);
   pubServ = CryptoECDSA().CompressPoint(pubServ);
   
   auto&& pubCli = CryptoECDSA().ComputePublicKey(privCli);
   pubCli = CryptoECDSA().CompressPoint(pubCli);

   btc_pubkey servKey;
   btc_pubkey_init(&servKey);
   memcpy(servKey.pubkey, pubServ.getPtr(), BIP151PUBKEYSIZE);
   servKey.compressed = true;

   btc_pubkey clientKey;
   btc_pubkey_init(&clientKey);
   memcpy(clientKey.pubkey, pubCli.getPtr(), BIP151PUBKEYSIZE);
   clientKey.compressed = true;

   //create pubkey maps
   map<string, btc_pubkey> servMap;
   servMap.insert(make_pair("own", servKey));
   servMap.insert(make_pair("101.101.101.101:10101", clientKey));

   map<string, btc_pubkey> cliMap;
   cliMap.insert(make_pair("own", clientKey));
   cliMap.insert(make_pair("1.2.3.4:8333", servKey));

   //create privkey maps
   map<SecureBinaryData, SecureBinaryData> servPrivMap;
   servPrivMap.insert(make_pair(pubServ, privServ));

   map<SecureBinaryData, SecureBinaryData> cliPrivMap;
   cliPrivMap.insert(make_pair(pubCli, privCli));

   //create auth peer sets
   set<SecureBinaryData> servSet;
   servSet.insert(pubCli);

   set<SecureBinaryData> clientSet;
   clientSet.insert(pubServ);

   //create server auth key lambdas
   auto serv_getPubKeyMap = [&servMap](void)->const map<string, btc_pubkey>&
   {
      return servMap;
   };

   auto serv_getPrivKey = [&servPrivMap](const BinaryDataRef& pub)->const SecureBinaryData&
   {
      auto iter = servPrivMap.find(pub);
      if (iter == servPrivMap.end())
         throw runtime_error("invalid key");
      return iter->second;
   };

   auto serv_getauthset = [servSet](void)->const set<SecureBinaryData>&
   {
      return servSet;
   };

   //create client auth key lambdas
   auto cli_getPubKeyMap = [&cliMap](void)->const map<string, btc_pubkey>&
   {
      return cliMap;
   };

   auto cli_getPrivKey = [&cliPrivMap](const BinaryDataRef& pub)->const SecureBinaryData&
   {
      auto iter = cliPrivMap.find(pub);
      if (iter == cliPrivMap.end())
         throw runtime_error("invalid key");
      return iter->second;
   };

   auto cli_getauthset = [clientSet](void)->const set<SecureBinaryData>&
   {
      return clientSet;
   };

   //create AKL objects
   AuthPeersLambdas aklServ(serv_getPubKeyMap, serv_getPrivKey, serv_getauthset);
   AuthPeersLambdas aklCli(cli_getPubKeyMap, cli_getPrivKey, cli_getauthset);


   startupBIP150CTX(4);

   btc_key prvKeyCliIn;
   btc_key prvKeyCliOut;
   btc_key prvKeySrvIn;
   btc_key prvKeySrvOut;
   prvKeyClientIn.copyTo(prvKeyCliIn.privkey);
   prvKeyClientOut.copyTo(prvKeyCliOut.privkey);
   prvKeyServerIn.copyTo(prvKeySrvIn.privkey);
   prvKeyServerOut.copyTo(prvKeySrvOut.privkey);
   BIP151Connection cliCon(&prvKeyCliIn, &prvKeyCliOut, aklCli, true);
   BIP151Connection srvCon(&prvKeySrvIn, &prvKeySrvOut, aklServ, true);

   // Set up encinit/encack directly. (Initial encinit/encack will use regular
   // Bitcoin P2P messages, which we'll skip building.) Confirm all steps
   // function properly along the way.
   BinaryData cliInEncinitCliData(ENCINITMSGSIZE);   // SRV (Out) -> CLI (In)
   BinaryData cliInEncackCliData(BIP151PUBKEYSIZE);  // CLI (In)  -> SRV (Out)
   BinaryData cliOutEncinitCliData(ENCINITMSGSIZE);  // CLI (Out) -> SRV (In)
   BinaryData cliOutEncackCliData(BIP151PUBKEYSIZE); // SRV (In)  -> CLI (Out)
   int s1 = srvCon.getEncinitData(cliInEncinitCliData.getPtr(),
                                  cliInEncinitCliData.getSize(),
                                  BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s1);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s2 = cliCon.processEncinit(cliInEncinitCliData.getPtr(),
                                  cliInEncinitCliData.getSize(),
                                  false);
   EXPECT_EQ(0, s2);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s3 = cliCon.getEncackData(cliInEncackCliData.getPtr(),
                                 cliInEncackCliData.getSize());
   EXPECT_EQ(0, s3);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s4 = srvCon.processEncack(cliInEncackCliData.getPtr(),
                                 cliInEncackCliData.getSize(),
                                 true);
   EXPECT_EQ(0, s4);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s5 = cliCon.getEncinitData(cliOutEncinitCliData.getPtr(),
                                  cliOutEncinitCliData.getSize(),
                                  BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s5);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s6 = srvCon.processEncinit(cliOutEncinitCliData.getPtr(),
                                  cliOutEncinitCliData.getSize(),
                                  false);
   EXPECT_EQ(0, s6);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s7 = srvCon.getEncackData(cliOutEncackCliData.getPtr(),
                                 cliOutEncackCliData.getSize());
   EXPECT_EQ(0, s7);
   EXPECT_TRUE(srvCon.connectionComplete());
   int s8 = cliCon.processEncack(cliOutEncackCliData.getPtr(),
                                 cliOutEncackCliData.getSize(),
                                 true);
   EXPECT_EQ(0, s8);
   EXPECT_TRUE(cliCon.connectionComplete());

   // Get the fingerprint.
   string curFng = cliCon.getBIP150Fingerprint();
   EXPECT_EQ(cli150Fingerprint, curFng);

   ////////////////// Start the BIP 150 process for each side. /////////////////
   BinaryData authchallengeBuf(BIP151PRVKEYSIZE);
   BinaryData authreplyBuf(BIP151PRVKEYSIZE*2);
   BinaryData authproposeBuf(BIP151PRVKEYSIZE);
   EXPECT_EQ(BIP150State::INACTIVE, cliCon.getBIP150State());
   EXPECT_EQ(BIP150State::INACTIVE, srvCon.getBIP150State());

   // INACTIVE -> CHALLENGE1
   int b1 = cliCon.getAuthchallengeData(authchallengeBuf.getPtr(),
                                        authchallengeBuf.getSize(),
                                        "1.2.3.4:8333",
                                        true);
   EXPECT_EQ(0, b1);
   EXPECT_EQ(BIP150State::CHALLENGE1, cliCon.getBIP150State());
   EXPECT_EQ(authchallenge1Data, authchallengeBuf);
   int b2 = srvCon.processAuthchallenge(authchallengeBuf.getPtr(),
                                        authchallengeBuf.getSize(),
                                        true);
   EXPECT_EQ(0, b2);
   EXPECT_EQ(BIP150State::CHALLENGE1, srvCon.getBIP150State());

   // CHALLENGE1 -> REPLY1
   int b3 = srvCon.getAuthreplyData(authreplyBuf.getPtr(),
                                    authreplyBuf.getSize(),
                                    true);
   EXPECT_EQ(0, b3);
   EXPECT_EQ(BIP150State::REPLY1, srvCon.getBIP150State());
   EXPECT_EQ(authreply1Data, authreplyBuf);
   int b4 = cliCon.processAuthreply(authreplyBuf.getPtr(),
                                    authreplyBuf.getSize(),
                                    true);
   EXPECT_EQ(0, b4);
   EXPECT_EQ(BIP150State::REPLY1, cliCon.getBIP150State());

   // REPLY1 -> PROPOSE
   int b5 = cliCon.getAuthproposeData(authproposeBuf.getPtr(),
                                      authproposeBuf.getSize());
   EXPECT_EQ(0, b5);
   EXPECT_EQ(BIP150State::PROPOSE, cliCon.getBIP150State());
   EXPECT_EQ(authproposeData_1way, authproposeBuf);
   int b6 = srvCon.processAuthpropose(authproposeBuf.getPtr(),
                                      authproposeBuf.getSize());
   EXPECT_EQ(1, b6);
   EXPECT_EQ(BIP150State::PROPOSE, srvCon.getBIP150State());

   // PROPOSE -> CHALLENGE2
   int b7 = srvCon.getAuthchallengeData(authchallengeBuf.getPtr(),
                                        authchallengeBuf.getSize(),
                                        "",
                                        false);
   EXPECT_EQ(0, b7);
   EXPECT_EQ(BIP150State::CHALLENGE2, srvCon.getBIP150State());
   EXPECT_EQ(authchallenge2Data_1way, authchallengeBuf);
   int b8 = cliCon.processAuthchallenge(authchallengeBuf.getPtr(),
                                        authchallengeBuf.getSize(),
                                        false);
   EXPECT_EQ(0, b8);
   EXPECT_EQ(BIP150State::CHALLENGE2, cliCon.getBIP150State());

   // CHALLENGE2 -> REPLY2 (SUCCESS)
   int b9 = cliCon.getAuthreplyData(authreplyBuf.getPtr(),
                                    authreplyBuf.getSize(),
                                    false);
   EXPECT_EQ(0, b9);

   cliCon.bip150HandshakeRekey();
   EXPECT_EQ(BIP150State::SUCCESS, cliCon.getBIP150State());
   EXPECT_EQ(memcmp(pubCli.getPtr(), authreplyBuf.getPtr(), BIP151PUBKEYSIZE), 0);
   int b10 = srvCon.processAuthreply(authreplyBuf.getPtr(),
                                     authreplyBuf.getSize(),
                                     false);
   EXPECT_EQ(0, b10);

   srvCon.bip150HandshakeRekey();
   EXPECT_EQ(BIP150State::SUCCESS, srvCon.getBIP150State());
}

TEST_F(BIP150_151Test, checkData_150_151_privateClientToPublicServer)
{
   // Get test files from the current (gtest) directory. C++17 would be nice
   // since filesystem::current_path() has been added. Alas, for now....
   // Test IPv4, and then IPv6 later.
   // Ideally, the code would be smart enough to support two separate contexts
   // so that two separate key sets can be tested. There's no real reason to
   // support this in Armory right now, though, and it'd be a lot of work. For
   // now, just cheat and have two "separate" systems with the same input files.

   //grab serv private key from peer files
   auto servFilePath = baseDir_;
   servFilePath.append("/bip150v0_srv1/identity-key-ipv4");
   fstream serv_isf(servFilePath);
   char prvHex[65];
   serv_isf.getline(prvHex, 65);
   SecureBinaryData privServ(READHEX(prvHex));

   //grab client private key from peer files
   auto cliFilePath = baseDir_;
   cliFilePath.append("/bip150v0_cli1/identity-key-ipv4");
   fstream cli_isf(cliFilePath);
   char cliHex[65];
   cli_isf.getline(cliHex, 65);
   SecureBinaryData privCli(READHEX(cliHex));
   
   //compute public keys
   auto&& pubServ = CryptoECDSA().ComputePublicKey(privServ);
   pubServ = CryptoECDSA().CompressPoint(pubServ);
   
   auto&& pubCli = CryptoECDSA().ComputePublicKey(privCli);
   pubCli = CryptoECDSA().CompressPoint(pubCli);

   btc_pubkey servKey;
   btc_pubkey_init(&servKey);
   memcpy(servKey.pubkey, pubServ.getPtr(), BIP151PUBKEYSIZE);
   servKey.compressed = true;

   btc_pubkey clientKey;
   btc_pubkey_init(&clientKey);
   memcpy(clientKey.pubkey, pubCli.getPtr(), BIP151PUBKEYSIZE);
   clientKey.compressed = true;

   //create pubkey maps
   map<string, btc_pubkey> servMap;
   servMap.insert(make_pair("own", servKey));
   servMap.insert(make_pair("101.101.101.101:10101", clientKey));

   map<string, btc_pubkey> cliMap;
   cliMap.insert(make_pair("own", clientKey));
   cliMap.insert(make_pair("1.2.3.4:8333", servKey));

   //create privkey maps
   map<SecureBinaryData, SecureBinaryData> servPrivMap;
   servPrivMap.insert(make_pair(pubServ, privServ));

   map<SecureBinaryData, SecureBinaryData> cliPrivMap;
   cliPrivMap.insert(make_pair(pubCli, privCli));

   //create auth peer sets
   set<SecureBinaryData> servSet;
   servSet.insert(pubCli);

   set<SecureBinaryData> clientSet;
   clientSet.insert(pubServ);

   //create server auth key lambdas
   auto serv_getPubKeyMap = [&servMap](void)->const map<string, btc_pubkey>&
   {
      return servMap;
   };

   auto serv_getPrivKey = [&servPrivMap](const BinaryDataRef& pub)->const SecureBinaryData&
   {
      auto iter = servPrivMap.find(pub);
      if (iter == servPrivMap.end())
         throw runtime_error("invalid key");
      return iter->second;
   };

   auto serv_getauthset = [servSet](void)->const set<SecureBinaryData>&
   {
      return servSet;
   };

   //create client auth key lambdas
   auto cli_getPubKeyMap = [&cliMap](void)->const map<string, btc_pubkey>&
   {
      return cliMap;
   };

   auto cli_getPrivKey = [&cliPrivMap](const BinaryDataRef& pub)->const SecureBinaryData&
   {
      auto iter = cliPrivMap.find(pub);
      if (iter == cliPrivMap.end())
         throw runtime_error("invalid key");
      return iter->second;
   };

   auto cli_getauthset = [clientSet](void)->const set<SecureBinaryData>&
   {
      return clientSet;
   };

   //create AKL objects
   AuthPeersLambdas aklServ(serv_getPubKeyMap, serv_getPrivKey, serv_getauthset);
   AuthPeersLambdas aklCli(cli_getPubKeyMap, cli_getPrivKey, cli_getauthset);


   startupBIP150CTX(4);

   btc_key prvKeyCliIn;
   btc_key prvKeyCliOut;
   btc_key prvKeySrvIn;
   btc_key prvKeySrvOut;
   prvKeyClientIn.copyTo(prvKeyCliIn.privkey);
   prvKeyClientOut.copyTo(prvKeyCliOut.privkey);
   prvKeyServerIn.copyTo(prvKeySrvIn.privkey);
   prvKeyServerOut.copyTo(prvKeySrvOut.privkey);
   BIP151Connection cliCon(&prvKeyCliIn, &prvKeyCliOut, aklCli, false);
   BIP151Connection srvCon(&prvKeySrvIn, &prvKeySrvOut, aklServ, true);

   // Set up encinit/encack directly. (Initial encinit/encack will use regular
   // Bitcoin P2P messages, which we'll skip building.) Confirm all steps
   // function properly along the way.
   BinaryData cliInEncinitCliData(ENCINITMSGSIZE);   // SRV (Out) -> CLI (In)
   BinaryData cliInEncackCliData(BIP151PUBKEYSIZE);  // CLI (In)  -> SRV (Out)
   BinaryData cliOutEncinitCliData(ENCINITMSGSIZE);  // CLI (Out) -> SRV (In)
   BinaryData cliOutEncackCliData(BIP151PUBKEYSIZE); // SRV (In)  -> CLI (Out)
   int s1 = srvCon.getEncinitData(cliInEncinitCliData.getPtr(),
                                  cliInEncinitCliData.getSize(),
                                  BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s1);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s2 = cliCon.processEncinit(cliInEncinitCliData.getPtr(),
                                  cliInEncinitCliData.getSize(),
                                  false);
   EXPECT_EQ(0, s2);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s3 = cliCon.getEncackData(cliInEncackCliData.getPtr(),
                                 cliInEncackCliData.getSize());
   EXPECT_EQ(0, s3);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s4 = srvCon.processEncack(cliInEncackCliData.getPtr(),
                                 cliInEncackCliData.getSize(),
                                 true);
   EXPECT_EQ(0, s4);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s5 = cliCon.getEncinitData(cliOutEncinitCliData.getPtr(),
                                  cliOutEncinitCliData.getSize(),
                                  BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s5);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s6 = srvCon.processEncinit(cliOutEncinitCliData.getPtr(),
                                  cliOutEncinitCliData.getSize(),
                                  false);
   EXPECT_EQ(0, s6);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s7 = srvCon.getEncackData(cliOutEncackCliData.getPtr(),
                                 cliOutEncackCliData.getSize());
   EXPECT_EQ(0, s7);
   EXPECT_TRUE(srvCon.connectionComplete());
   int s8 = cliCon.processEncack(cliOutEncackCliData.getPtr(),
                                 cliOutEncackCliData.getSize(),
                                 true);
   EXPECT_EQ(0, s8);
   EXPECT_TRUE(cliCon.connectionComplete());

   // Get the fingerprint.
   string curFng = cliCon.getBIP150Fingerprint();
   EXPECT_EQ(cli150Fingerprint, curFng);

   ////////////////// Start the BIP 150 process for each side. /////////////////
   BinaryData authchallengeBuf(BIP151PRVKEYSIZE);
   BinaryData authreplyBuf(BIP151PRVKEYSIZE*2);
   BinaryData authproposeBuf(BIP151PRVKEYSIZE);
   EXPECT_EQ(BIP150State::INACTIVE, cliCon.getBIP150State());
   EXPECT_EQ(BIP150State::INACTIVE, srvCon.getBIP150State());

   // INACTIVE -> CHALLENGE1
   int b1 = cliCon.getAuthchallengeData(authchallengeBuf.getPtr(),
                                        authchallengeBuf.getSize(),
                                        "1.2.3.4:8333",
                                        true);
   EXPECT_EQ(0, b1);
   EXPECT_EQ(BIP150State::CHALLENGE1, cliCon.getBIP150State());
   EXPECT_EQ(authchallenge1Data, authchallengeBuf);
   int b2 = srvCon.processAuthchallenge(authchallengeBuf.getPtr(),
                                        authchallengeBuf.getSize(),
                                        true);
   EXPECT_EQ(0, b2);
   EXPECT_EQ(BIP150State::CHALLENGE1, srvCon.getBIP150State());

   // CHALLENGE1 -> REPLY1
   int b3 = srvCon.getAuthreplyData(authreplyBuf.getPtr(),
                                    authreplyBuf.getSize(),
                                    true);
   EXPECT_EQ(0, b3);
   EXPECT_EQ(BIP150State::REPLY1, srvCon.getBIP150State());
   EXPECT_EQ(authreply1Data, authreplyBuf);
   int b4 = cliCon.processAuthreply(authreplyBuf.getPtr(),
                                    authreplyBuf.getSize(),
                                    true);
   EXPECT_EQ(0, b4);
   EXPECT_EQ(BIP150State::REPLY1, cliCon.getBIP150State());

   // REPLY1 -> PROPOSE
   int b5 = cliCon.getAuthproposeData(authproposeBuf.getPtr(),
                                      authproposeBuf.getSize());
   EXPECT_EQ(0, b5);
   EXPECT_EQ(BIP150State::PROPOSE, cliCon.getBIP150State());
   EXPECT_EQ(authproposeData, authproposeBuf);
   int b6 = srvCon.processAuthpropose(authproposeBuf.getPtr(),
                                      authproposeBuf.getSize());
   EXPECT_EQ(-1, b6);
   EXPECT_EQ(BIP150State::ERR_STATE, srvCon.getBIP150State());
}

TEST_F(BIP150_151Test, checkData_150_151_publicClientToPrivateServer)
{
   // Get test files from the current (gtest) directory. C++17 would be nice
   // since filesystem::current_path() has been added. Alas, for now....
   // Test IPv4, and then IPv6 later.
   // Ideally, the code would be smart enough to support two separate contexts
   // so that two separate key sets can be tested. There's no real reason to
   // support this in Armory right now, though, and it'd be a lot of work. For
   // now, just cheat and have two "separate" systems with the same input files.

   //grab serv private key from peer files
   auto servFilePath = baseDir_;
   servFilePath.append("/bip150v0_srv1/identity-key-ipv4");
   fstream serv_isf(servFilePath);
   char prvHex[65];
   serv_isf.getline(prvHex, 65);
   SecureBinaryData privServ(READHEX(prvHex));

   //grab client private key from peer files
   auto cliFilePath = baseDir_;
   cliFilePath.append("/bip150v0_cli1/identity-key-ipv4");
   fstream cli_isf(cliFilePath);
   char cliHex[65];
   cli_isf.getline(cliHex, 65);
   SecureBinaryData privCli(READHEX(cliHex));
   
   //compute public keys
   auto&& pubServ = CryptoECDSA().ComputePublicKey(privServ);
   pubServ = CryptoECDSA().CompressPoint(pubServ);
   
   auto&& pubCli = CryptoECDSA().ComputePublicKey(privCli);
   pubCli = CryptoECDSA().CompressPoint(pubCli);

   btc_pubkey servKey;
   btc_pubkey_init(&servKey);
   memcpy(servKey.pubkey, pubServ.getPtr(), BIP151PUBKEYSIZE);
   servKey.compressed = true;

   btc_pubkey clientKey;
   btc_pubkey_init(&clientKey);
   memcpy(clientKey.pubkey, pubCli.getPtr(), BIP151PUBKEYSIZE);
   clientKey.compressed = true;

   //create pubkey maps
   map<string, btc_pubkey> servMap;
   servMap.insert(make_pair("own", servKey));
   servMap.insert(make_pair("101.101.101.101:10101", clientKey));

   map<string, btc_pubkey> cliMap;
   cliMap.insert(make_pair("own", clientKey));
   cliMap.insert(make_pair("1.2.3.4:8333", servKey));

   //create privkey maps
   map<SecureBinaryData, SecureBinaryData> servPrivMap;
   servPrivMap.insert(make_pair(pubServ, privServ));

   map<SecureBinaryData, SecureBinaryData> cliPrivMap;
   cliPrivMap.insert(make_pair(pubCli, privCli));

   //create auth peer sets
   set<SecureBinaryData> servSet;
   servSet.insert(pubCli);

   set<SecureBinaryData> clientSet;
   clientSet.insert(pubServ);

   //create server auth key lambdas
   auto serv_getPubKeyMap = [&servMap](void)->const map<string, btc_pubkey>&
   {
      return servMap;
   };

   auto serv_getPrivKey = [&servPrivMap](const BinaryDataRef& pub)->const SecureBinaryData&
   {
      auto iter = servPrivMap.find(pub);
      if (iter == servPrivMap.end())
         throw runtime_error("invalid key");
      return iter->second;
   };

   auto serv_getauthset = [servSet](void)->const set<SecureBinaryData>&
   {
      return servSet;
   };

   //create client auth key lambdas
   auto cli_getPubKeyMap = [&cliMap](void)->const map<string, btc_pubkey>&
   {
      return cliMap;
   };

   auto cli_getPrivKey = [&cliPrivMap](const BinaryDataRef& pub)->const SecureBinaryData&
   {
      auto iter = cliPrivMap.find(pub);
      if (iter == cliPrivMap.end())
         throw runtime_error("invalid key");
      return iter->second;
   };

   auto cli_getauthset = [clientSet](void)->const set<SecureBinaryData>&
   {
      return clientSet;
   };

   //create AKL objects
   AuthPeersLambdas aklServ(serv_getPubKeyMap, serv_getPrivKey, serv_getauthset);
   AuthPeersLambdas aklCli(cli_getPubKeyMap, cli_getPrivKey, cli_getauthset);


   startupBIP150CTX(4);

   btc_key prvKeyCliIn;
   btc_key prvKeyCliOut;
   btc_key prvKeySrvIn;
   btc_key prvKeySrvOut;
   prvKeyClientIn.copyTo(prvKeyCliIn.privkey);
   prvKeyClientOut.copyTo(prvKeyCliOut.privkey);
   prvKeyServerIn.copyTo(prvKeySrvIn.privkey);
   prvKeyServerOut.copyTo(prvKeySrvOut.privkey);
   BIP151Connection cliCon(&prvKeyCliIn, &prvKeyCliOut, aklCli, true);
   BIP151Connection srvCon(&prvKeySrvIn, &prvKeySrvOut, aklServ, false);

   // Set up encinit/encack directly. (Initial encinit/encack will use regular
   // Bitcoin P2P messages, which we'll skip building.) Confirm all steps
   // function properly along the way.
   BinaryData cliInEncinitCliData(ENCINITMSGSIZE);   // SRV (Out) -> CLI (In)
   BinaryData cliInEncackCliData(BIP151PUBKEYSIZE);  // CLI (In)  -> SRV (Out)
   BinaryData cliOutEncinitCliData(ENCINITMSGSIZE);  // CLI (Out) -> SRV (In)
   BinaryData cliOutEncackCliData(BIP151PUBKEYSIZE); // SRV (In)  -> CLI (Out)
   int s1 = srvCon.getEncinitData(cliInEncinitCliData.getPtr(),
                                  cliInEncinitCliData.getSize(),
                                  BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s1);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s2 = cliCon.processEncinit(cliInEncinitCliData.getPtr(),
                                  cliInEncinitCliData.getSize(),
                                  false);
   EXPECT_EQ(0, s2);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s3 = cliCon.getEncackData(cliInEncackCliData.getPtr(),
                                 cliInEncackCliData.getSize());
   EXPECT_EQ(0, s3);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s4 = srvCon.processEncack(cliInEncackCliData.getPtr(),
                                 cliInEncackCliData.getSize(),
                                 true);
   EXPECT_EQ(0, s4);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s5 = cliCon.getEncinitData(cliOutEncinitCliData.getPtr(),
                                  cliOutEncinitCliData.getSize(),
                                  BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s5);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s6 = srvCon.processEncinit(cliOutEncinitCliData.getPtr(),
                                  cliOutEncinitCliData.getSize(),
                                  false);
   EXPECT_EQ(0, s6);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s7 = srvCon.getEncackData(cliOutEncackCliData.getPtr(),
                                 cliOutEncackCliData.getSize());
   EXPECT_EQ(0, s7);
   EXPECT_TRUE(srvCon.connectionComplete());
   int s8 = cliCon.processEncack(cliOutEncackCliData.getPtr(),
                                 cliOutEncackCliData.getSize(),
                                 true);
   EXPECT_EQ(0, s8);
   EXPECT_TRUE(cliCon.connectionComplete());

   // Get the fingerprint.
   string curFng = cliCon.getBIP150Fingerprint();
   EXPECT_EQ(cli150Fingerprint, curFng);

   ////////////////// Start the BIP 150 process for each side. /////////////////
   BinaryData authchallengeBuf(BIP151PRVKEYSIZE);
   BinaryData authreplyBuf(BIP151PRVKEYSIZE*2);
   BinaryData authproposeBuf(BIP151PRVKEYSIZE);
   EXPECT_EQ(BIP150State::INACTIVE, cliCon.getBIP150State());
   EXPECT_EQ(BIP150State::INACTIVE, srvCon.getBIP150State());

   // INACTIVE -> CHALLENGE1
   int b1 = cliCon.getAuthchallengeData(authchallengeBuf.getPtr(),
                                        authchallengeBuf.getSize(),
                                        "1.2.3.4:8333",
                                        true);
   EXPECT_EQ(0, b1);
   EXPECT_EQ(BIP150State::CHALLENGE1, cliCon.getBIP150State());
   EXPECT_EQ(authchallenge1Data, authchallengeBuf);
   int b2 = srvCon.processAuthchallenge(authchallengeBuf.getPtr(),
                                        authchallengeBuf.getSize(),
                                        true);
   EXPECT_EQ(0, b2);
   EXPECT_EQ(BIP150State::CHALLENGE1, srvCon.getBIP150State());

   // CHALLENGE1 -> REPLY1
   int b3 = srvCon.getAuthreplyData(authreplyBuf.getPtr(),
                                    authreplyBuf.getSize(),
                                    true);
   EXPECT_EQ(0, b3);
   EXPECT_EQ(BIP150State::REPLY1, srvCon.getBIP150State());
   EXPECT_EQ(authreply1Data, authreplyBuf);
   int b4 = cliCon.processAuthreply(authreplyBuf.getPtr(),
                                    authreplyBuf.getSize(),
                                    true);
   EXPECT_EQ(0, b4);
   EXPECT_EQ(BIP150State::REPLY1, cliCon.getBIP150State());

   // REPLY1 -> PROPOSE
   int b5 = cliCon.getAuthproposeData(authproposeBuf.getPtr(),
                                      authproposeBuf.getSize());
   EXPECT_EQ(0, b5);
   EXPECT_EQ(BIP150State::PROPOSE, cliCon.getBIP150State());
   EXPECT_EQ(authproposeData_1way, authproposeBuf);
   int b6 = srvCon.processAuthpropose(authproposeBuf.getPtr(),
                                      authproposeBuf.getSize());
   EXPECT_EQ(-1, b6);
   EXPECT_EQ(BIP150State::ERR_STATE, srvCon.getBIP150State());
}

// Test handshake failure cases. All cases will fail eventually.
TEST_F(BIP150_151Test, handshakeCases_151_Only)
{
   // Try to generate an encack before generating an encinit.
   auto getpubkeymap = [](void)->const map<string, btc_pubkey>&
   {
      throw runtime_error("");
   };

   auto getprivkey = [](const BinaryDataRef&)->const SecureBinaryData&
   {
      throw runtime_error("");
   };

   auto getauthset = [](void)->const set<SecureBinaryData>&
   {
      throw runtime_error("");
   };

   AuthPeersLambdas akl1(getpubkeymap, getprivkey, getauthset);
   AuthPeersLambdas akl2(getpubkeymap, getprivkey, getauthset);

   BIP151Connection cliCon1(akl1, false);
   BIP151Connection srvCon1(akl2, false);
   std::array<uint8_t, BIP151PUBKEYSIZE> dummy1{};
   int s1 = cliCon1.getEncackData(dummy1.data(),
                                  dummy1.size());
   EXPECT_EQ(-1, s1);

   // Try to process an encack before processing an encinit.
   dummy1[0] = 0x03;
   dummy1[1] = 0xff;
   int s2 = srvCon1.processEncack(dummy1.data(),
                                  dummy1.size(),
                                  true);
   EXPECT_EQ(-1, s2);

   // Attempt to set an incorrect ciphersuite.
   AuthPeersLambdas akl3(getpubkeymap, getprivkey, getauthset);
   AuthPeersLambdas akl4(getpubkeymap, getprivkey, getauthset);

   BIP151Connection cliCon2(akl3, false);
   BIP151Connection srvCon2(akl4, false);
   std::array<uint8_t, ENCINITMSGSIZE> dummy3{};
   std::array<uint8_t, 64> dummy4{};
   int s3 = cliCon2.getEncinitData(dummy3.data(),
                                   dummy3.size(),
                                   static_cast<BIP151SymCiphers>(0xda));
   EXPECT_EQ(-1, s3);

   // Attempt to rekey before the connection is complete.
   int s4 = cliCon2.getEncinitData(dummy3.data(),
                                   dummy3.size(),
                                   BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s4);
   int s5 = srvCon2.processEncinit(dummy3.data(),
                                   dummy3.size(),
                                   false);
   EXPECT_EQ(0, s5);
   int s6 = srvCon2.bip151RekeyConn(dummy4.data(),
                                    dummy4.size());
   EXPECT_EQ(-1, s6);

   // Run after the final test has finished.
   shutdownBIP151CTX();
}

////////////////////////////////////////////////////////////////////////////////
class BinaryDataTest : public ::testing::Test
{
protected:
   virtual void SetUp(void) 
   {
      str0_ = "";
      str4_ = "1234abcd";
      str5_ = "1234abcdef";

      bd0_ = READHEX(str0_);
      bd4_ = READHEX(str4_);
      bd5_ = READHEX(str5_);
   }

   string str0_;
   string str4_;
   string str5_;

   BinaryData bd0_;
   BinaryData bd4_;
   BinaryData bd5_;

};


////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataTest, Constructor)
{
   uint8_t* ptr = new uint8_t[4];
   ptr[0]='0'; // random junk
   ptr[1]='1';
   ptr[2]='2';
   ptr[3]='3';

   BinaryData a;
   BinaryData b(4);
   BinaryData c(ptr, 2);
   BinaryData d(ptr, 4);
   BinaryData e(b);
   auto&& f = BinaryData::fromString("xyza");

   EXPECT_EQ(a.getSize(), 0ULL);
   EXPECT_EQ(b.getSize(), 4ULL);
   EXPECT_EQ(c.getSize(), 2ULL);
   EXPECT_EQ(d.getSize(), 4ULL);
   EXPECT_EQ(e.getSize(), 4ULL);
   EXPECT_EQ(f.getSize(), 4ULL);

   EXPECT_TRUE( a.empty());
   EXPECT_FALSE(b.empty());
   EXPECT_FALSE(c.empty());
   EXPECT_FALSE(d.empty());
   EXPECT_FALSE(e.empty());

   BinaryDataRef g(f);
   BinaryDataRef h(d);
   BinaryData    i(g);

   EXPECT_EQ(   g.getSize(), 4ULL);
   EXPECT_EQ(   i.getSize(), 4ULL);
   EXPECT_TRUE( g==f);
   EXPECT_FALSE(g==h);
   EXPECT_TRUE( i==g);

   delete[] ptr;
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataTest, CopyFrom)
{
   BinaryData a,b,c,d,e,f;
   a.copyFrom((uint8_t*)bd0_.getPtr(), bd0_.getSize());
   b.copyFrom((uint8_t*)bd4_.getPtr(), (uint8_t*)bd4_.getPtr()+4);
   c.copyFrom((uint8_t*)bd4_.getPtr(), bd4_.getSize());
   d.copyFrom(str5_);
   e.copyFrom(a);

   BinaryDataRef i(b);
   f.copyFrom(i);

   EXPECT_EQ(a.getSize(), 0ULL);
   EXPECT_EQ(b.getSize(), 4ULL);
   EXPECT_EQ(c.getSize(), 4ULL);
   EXPECT_EQ(a,e);
   EXPECT_EQ(b,c);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataTest, CopyTo)
{
   BinaryData a,b,c,d,e,f,g,h;
   bd0_.copyTo(a);
   bd4_.copyTo(b);

   c.resize(bd5_.getSize());
   bd5_.copyTo(c.getPtr());

   size_t sz = 2;
   d.resize(sz);
   e.resize(sz);
   bd5_.copyTo(d.getPtr(), sz);
   bd5_.copyTo(e.getPtr(), bd5_.getSize()-sz, sz);

   f.copyFrom(bd5_.getPtr(), bd5_.getPtr()+sz);

   EXPECT_TRUE(a==bd0_);
   EXPECT_TRUE(b==bd4_);
   EXPECT_TRUE(c==bd5_);
   EXPECT_TRUE(bd5_.startsWith(d));
   EXPECT_TRUE(bd5_.endsWith(e));
   EXPECT_TRUE(d==f);

   EXPECT_EQ(a.getSize(), 0ULL);
   EXPECT_EQ(b.getSize(), 4ULL);
   EXPECT_EQ(c.getSize(), 5ULL);
   EXPECT_EQ(d.getSize(), 2ULL);
   EXPECT_NE(b,c);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataTest, Fill)
{
   BinaryData a(0), b(1), c(4);
   BinaryData aAns = READHEX("");
   BinaryData bAns = READHEX("aa");
   BinaryData cAns = READHEX("aaaaaaaa");

   a.fill(0xaa);
   b.fill(0xaa);
   c.fill(0xaa);

   EXPECT_EQ(a, aAns);
   EXPECT_EQ(b, bAns);
   EXPECT_EQ(c, cAns);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataTest, IndexOp)
{
   EXPECT_EQ(bd4_[0], 0x12);
   EXPECT_EQ(bd4_[1], 0x34);
   EXPECT_EQ(bd4_[2], 0xab);
   EXPECT_EQ(bd4_[3], 0xcd);

   EXPECT_EQ(bd4_[-4], 0x12);
   EXPECT_EQ(bd4_[-3], 0x34);
   EXPECT_EQ(bd4_[-2], 0xab);
   EXPECT_EQ(bd4_[-1], 0xcd);

   bd4_[1] = 0xff;
   EXPECT_EQ(bd4_[0], 0x12);
   EXPECT_EQ(bd4_[1], 0xff);
   EXPECT_EQ(bd4_[2], 0xab);
   EXPECT_EQ(bd4_[3], 0xcd);

   EXPECT_EQ(bd4_[-4], 0x12);
   EXPECT_EQ(bd4_[-3], 0xff);
   EXPECT_EQ(bd4_[-2], 0xab);
   EXPECT_EQ(bd4_[-1], 0xcd);

   EXPECT_EQ(bd4_.toHexStr(), string("12ffabcd"));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataTest, StartsEndsWith)
{
   BinaryData a = READHEX("abcd");
   EXPECT_TRUE( bd0_.startsWith(bd0_));
   EXPECT_TRUE( bd4_.startsWith(bd0_));
   EXPECT_TRUE( bd5_.startsWith(bd4_));
   EXPECT_TRUE( bd5_.startsWith(bd5_));
   EXPECT_FALSE(bd4_.startsWith(bd5_));
   EXPECT_TRUE( bd0_.startsWith(bd0_));
   EXPECT_FALSE(bd0_.startsWith(bd4_));
   EXPECT_FALSE(bd5_.endsWith(a));
   EXPECT_TRUE( bd4_.endsWith(a));
   EXPECT_FALSE(bd0_.endsWith(a));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataTest, Append)
{
   BinaryData a = READHEX("ef");

   BinaryData static4 = bd4_;

   BinaryData b = bd4_ + a;
   BinaryData c = bd4_.append(a);

   BinaryDataRef d(a);
   bd4_.copyFrom(static4);
   BinaryData e = bd4_.append(d);
   bd4_.copyFrom(static4);
   BinaryData f = bd4_.append(a.getPtr(), 1);
   bd4_.copyFrom(static4);
   BinaryData g = bd4_.append(0xef);

   BinaryData h = bd0_ + a;
   BinaryData i = bd0_.append(a);
   bd0_.resize(0);
   BinaryData j = bd0_.append(a.getPtr(), 1);
   bd0_.resize(0);
   BinaryData k = bd0_.append(0xef);
   
   EXPECT_EQ(bd5_, b);
   EXPECT_EQ(bd5_, c);
   EXPECT_EQ(bd5_, e);
   EXPECT_EQ(bd5_, f);
   EXPECT_EQ(bd5_, g);

   EXPECT_NE(bd5_, h);
   EXPECT_EQ(a, h);
   EXPECT_EQ(a, i);
   EXPECT_EQ(a, j);
   EXPECT_EQ(a, k);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataTest, Inequality)
{
   EXPECT_FALSE(bd0_ < bd0_);
   EXPECT_TRUE( bd0_ < bd4_);
   EXPECT_TRUE( bd0_ < bd5_);

   EXPECT_FALSE(bd4_ < bd0_);
   EXPECT_FALSE(bd4_ < bd4_);
   EXPECT_TRUE( bd4_ < bd5_);

   EXPECT_FALSE(bd5_ < bd0_);
   EXPECT_FALSE(bd5_ < bd4_);
   EXPECT_FALSE(bd5_ < bd5_);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataTest, Equality)
{
   EXPECT_TRUE( bd0_==bd0_);
   EXPECT_TRUE( bd4_==bd4_);
   EXPECT_FALSE(bd4_==bd5_);
   EXPECT_TRUE( bd0_!=bd4_);
   EXPECT_TRUE( bd0_!=bd5_);
   EXPECT_TRUE( bd4_!=bd5_);
   EXPECT_FALSE(bd4_!=bd4_);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataTest, ToString)
{
   EXPECT_EQ(bd0_.toHexStr(), str0_);
   EXPECT_EQ(bd4_.toHexStr(), str4_);
   EXPECT_EQ(bd4_.toHexStr(), str4_);

   string a,b;
   bd0_.copyTo(a);
   bd4_.copyTo(b);
   EXPECT_EQ(bd0_.toBinStr(), a);
   EXPECT_EQ(bd4_.toBinStr(), b);

   string stra("cdab3412");
   BinaryData bda = READHEX(stra);

   EXPECT_EQ(bd4_.toHexStr(true), stra);
   EXPECT_EQ(bd4_.toBinStr(true), bda.toBinStr());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataTest, Endianness)
{
   BinaryData a = READHEX("cdab3412");
   BinaryData b = READHEX("1234cdab");

   BinaryData static4 = bd4_;

   EXPECT_EQ(   a.copySwapEndian(), bd4_);
   EXPECT_EQ(bd4_.copySwapEndian(),    a);
   EXPECT_EQ(bd0_.copySwapEndian(), bd0_);


   bd4_ = static4;
   bd4_.swapEndian();
   EXPECT_EQ(bd4_, a);

   bd4_ = static4;
   bd4_.swapEndian(2);
   EXPECT_EQ(bd4_, b);

   bd4_ = static4;
   bd4_.swapEndian(2,2);
   EXPECT_EQ(bd4_, b);

   bd4_ = static4;
   bd4_.swapEndian(2,4);
   EXPECT_EQ(bd4_, b);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataTest, IntToBinData)
{
   // 0x1234 in src code is always interpreted by the compiler as
   // big-endian, regardless of the underlying architecture.  So 
   // writing 0x1234 will be interpretted as an integer with value
   // 4660 on all architectures.  
   BinaryData a,b;

   a = BinaryData::IntToStrLE<uint8_t>(0xab);
   b = BinaryData::IntToStrBE<uint8_t>(0xab);
   EXPECT_EQ(a, READHEX("ab"));
   EXPECT_EQ(b, READHEX("ab"));

   a = BinaryData::IntToStrLE<uint16_t>(0xabcd);
   b = BinaryData::IntToStrBE<uint16_t>(0xabcd);
   EXPECT_EQ(a, READHEX("cdab"));
   EXPECT_EQ(b, READHEX("abcd"));

   a = BinaryData::IntToStrLE((uint16_t)0xabcd);
   b = BinaryData::IntToStrBE((uint16_t)0xabcd);
   EXPECT_EQ(a, READHEX("cdab"));
   EXPECT_EQ(b, READHEX("abcd"));

   // This fails b/c it auto "promotes" non-suffix literals to 4-byte ints
   a = BinaryData::IntToStrLE(0xabcd);
   b = BinaryData::IntToStrBE(0xabcd);
   EXPECT_NE(a, READHEX("cdab"));
   EXPECT_NE(b, READHEX("abcd"));

   a = BinaryData::IntToStrLE(0xfec38a11);
   b = BinaryData::IntToStrBE(0xfec38a11);
   EXPECT_EQ(a, READHEX("118ac3fe"));
   EXPECT_EQ(b, READHEX("fec38a11"));

   a = BinaryData::IntToStrLE(0x00000000fec38a11ULL);
   b = BinaryData::IntToStrBE(0x00000000fec38a11ULL);
   EXPECT_EQ(a, READHEX("118ac3fe00000000"));
   EXPECT_EQ(b, READHEX("00000000fec38a11"));

}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataTest, BinDataToInt)
{
   uint8_t   a8,  b8;
   uint16_t a16, b16;
   uint32_t a32, b32;
   uint64_t a64, b64;

   a8 = BinaryData::StrToIntBE<uint8_t>(READHEX("ab"));
   b8 = BinaryData::StrToIntLE<uint8_t>(READHEX("ab"));
   EXPECT_EQ(a8, 0xab);
   EXPECT_EQ(b8, 0xab);

   a16 = BinaryData::StrToIntBE<uint16_t>(READHEX("abcd"));
   b16 = BinaryData::StrToIntLE<uint16_t>(READHEX("abcd"));
   EXPECT_EQ(a16, 0xabcd);
   EXPECT_EQ(b16, 0xcdab);

   a32 = BinaryData::StrToIntBE<uint32_t>(READHEX("fec38a11"));
   b32 = BinaryData::StrToIntLE<uint32_t>(READHEX("fec38a11"));
   EXPECT_EQ(a32, 0xfec38a11ULL);
   EXPECT_EQ(b32, 0x118ac3feULL);

   a64 = BinaryData::StrToIntBE<uint64_t>(READHEX("00000000fec38a11"));
   b64 = BinaryData::StrToIntLE<uint64_t>(READHEX("00000000fec38a11"));
   EXPECT_EQ(a64, 0x00000000fec38a11ULL);
   EXPECT_EQ(b64, 0x118ac3fe00000000ULL);
    
   // These are really just identical tests, I have no idea whether it
   // was worth spending the time to write these, and even this comment
   // here explaining how it was probably a waste of time...
   a8 = READ_UINT8_BE(READHEX("ab"));
   b8 = READ_UINT8_LE(READHEX("ab"));
   EXPECT_EQ(a8, 0xab);
   EXPECT_EQ(b8, 0xab);

   a16 = READ_UINT16_BE(READHEX("abcd"));
   b16 = READ_UINT16_LE(READHEX("abcd"));
   EXPECT_EQ(a16, 0xabcd);
   EXPECT_EQ(b16, 0xcdab);

   a32 = READ_UINT32_BE(READHEX("fec38a11"));
   b32 = READ_UINT32_LE(READHEX("fec38a11"));
   EXPECT_EQ(a32, 0xfec38a11);
   EXPECT_EQ(b32, 0x118ac3feULL);

   a64 = READ_UINT64_BE(READHEX("00000000fec38a11"));
   b64 = READ_UINT64_LE(READHEX("00000000fec38a11"));
   EXPECT_EQ(a64, 0x00000000fec38a11);
   EXPECT_EQ(b64, 0x118ac3fe00000000ULL);

   // Test the all-on-one read-int macros
   a8 = READ_UINT8_HEX_BE("ab");
   b8 = READ_UINT8_HEX_LE("ab");
   EXPECT_EQ(a8, 0xab);
   EXPECT_EQ(b8, 0xab);

   a16 = READ_UINT16_HEX_BE("abcd");
   b16 = READ_UINT16_HEX_LE("abcd");
   EXPECT_EQ(a16, 0xabcd);
   EXPECT_EQ(b16, 0xcdab);

   a32 = READ_UINT32_HEX_BE("fec38a11");
   b32 = READ_UINT32_HEX_LE("fec38a11");
   EXPECT_EQ(a32, 0xfec38a11);
   EXPECT_EQ(b32, 0x118ac3feULL);

   a64 = READ_UINT64_HEX_BE("00000000fec38a11");
   b64 = READ_UINT64_HEX_LE("00000000fec38a11");
   EXPECT_EQ(a64, 0x00000000fec38a11);
   EXPECT_EQ(b64, 0x118ac3fe00000000ULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataTest, Find)
{
   BinaryData a = READHEX("12");
   BinaryData b = READHEX("34");
   BinaryData c = READHEX("abcd");
   BinaryData d = READHEX("ff");

   EXPECT_EQ(bd0_.find(bd0_),     0);
   EXPECT_EQ(bd0_.find(bd4_),    -1);
   EXPECT_EQ(bd0_.find(bd4_, 2), -1);
   EXPECT_EQ(bd4_.find(bd0_),     0);
   EXPECT_EQ(bd4_.find(bd0_, 2),  2);

   EXPECT_EQ(bd4_.find(a),  0);
   EXPECT_EQ(bd4_.find(b),  1);
   EXPECT_EQ(bd4_.find(c),  2);
   EXPECT_EQ(bd4_.find(d), -1);

   EXPECT_EQ(bd4_.find(a, 0),  0);
   EXPECT_EQ(bd4_.find(b, 0),  1);
   EXPECT_EQ(bd4_.find(c, 0),  2);
   EXPECT_EQ(bd4_.find(d, 0), -1);

   EXPECT_EQ(bd4_.find(a, 1), -1);
   EXPECT_EQ(bd4_.find(b, 1),  1);
   EXPECT_EQ(bd4_.find(c, 1),  2);
   EXPECT_EQ(bd4_.find(d, 1), -1);

   EXPECT_EQ(bd4_.find(a, 4), -1);
   EXPECT_EQ(bd4_.find(b, 4), -1);
   EXPECT_EQ(bd4_.find(c, 4), -1);
   EXPECT_EQ(bd4_.find(d, 4), -1);

   EXPECT_EQ(bd4_.find(a, 8), -1);
   EXPECT_EQ(bd4_.find(b, 8), -1);
   EXPECT_EQ(bd4_.find(c, 8), -1);
   EXPECT_EQ(bd4_.find(d, 8), -1);
}

TEST_F(BinaryDataTest, Contains)
{
   BinaryData a = READHEX("12");
   BinaryData b = READHEX("34");
   BinaryData c = READHEX("abcd");
   BinaryData d = READHEX("ff");

   EXPECT_TRUE( bd0_.contains(bd0_));
   EXPECT_FALSE(bd0_.contains(bd4_));
   EXPECT_FALSE(bd0_.contains(bd4_, 2));

   EXPECT_TRUE( bd4_.contains(a));
   EXPECT_TRUE( bd4_.contains(b));
   EXPECT_TRUE( bd4_.contains(c));
   EXPECT_FALSE(bd4_.contains(d));

   EXPECT_TRUE( bd4_.contains(a, 0));
   EXPECT_TRUE( bd4_.contains(b, 0));
   EXPECT_TRUE( bd4_.contains(c, 0));
   EXPECT_FALSE(bd4_.contains(d, 0));

   EXPECT_FALSE(bd4_.contains(a, 1));
   EXPECT_TRUE( bd4_.contains(b, 1));
   EXPECT_TRUE( bd4_.contains(c, 1));
   EXPECT_FALSE(bd4_.contains(d, 1));

   EXPECT_FALSE(bd4_.contains(a, 4));
   EXPECT_FALSE(bd4_.contains(b, 4));
   EXPECT_FALSE(bd4_.contains(c, 4));
   EXPECT_FALSE(bd4_.contains(d, 4));

   EXPECT_FALSE(bd4_.contains(a, 8));
   EXPECT_FALSE(bd4_.contains(b, 8));
   EXPECT_FALSE(bd4_.contains(c, 8));
   EXPECT_FALSE(bd4_.contains(d, 8));
}

TEST_F(BinaryDataTest, DISABLED_CompareBench)
{
   auto start = chrono::system_clock::now();

   unsigned setSize = 5000000;
   unsigned compareSize = 100000;

   //setup
   set<BinaryData> dataSet;
   unordered_set<BinaryData> udSet;
   set<BinaryData> compareSet;
   for (unsigned i=0; i<setSize; i++)
   {
      auto hash = BtcUtils::fortuna_.generateRandom(32);

      if ((hash.getPtr()[0] % 8) == 0 && compareSet.size() < compareSize)
         compareSet.emplace(hash);

      udSet.emplace(hash);
      dataSet.emplace(move(hash));
   }

   for (unsigned i=0; i<compareSize; i++)
      compareSet.emplace(move(BtcUtils::fortuna_.generateRandom(32)));

   ASSERT_EQ(dataSet.size(), setSize);
   ASSERT_EQ(compareSet.size(), compareSize*2);

   auto stop = chrono::system_clock::now();
   auto duration = chrono::duration_cast<chrono::milliseconds>(stop - start);
   std::cout << "setup in " << duration.count() << " ms" << std::endl;

   //set
   start = chrono::system_clock::now();
   unsigned hits = 0;
   for (const auto& hash : compareSet)
   {
      auto iter = dataSet.find(hash);
      if (iter != dataSet.end())
         hits++;
   }

   EXPECT_EQ(hits, compareSize);
   stop = chrono::system_clock::now();
   duration = chrono::duration_cast<chrono::milliseconds>(stop - start);
   std::cout << "compared set in " << duration.count() << " ms" << std::endl;

   //unordered set
   start = chrono::system_clock::now();
   hits = 0;
   for (const auto& hash : compareSet)
   {
      auto iter = udSet.find(hash);
      if (iter != udSet.end())
         hits++;
   }

   EXPECT_EQ(hits, compareSize);
   stop = chrono::system_clock::now();
   duration = chrono::duration_cast<chrono::milliseconds>(stop - start);
   std::cout << "compared unordered set in " << duration.count() << " ms" << std::endl;

}

////////////////////////////////////////////////////////////////////////////////
class BinaryDataRefTest : public ::testing::Test
{
protected:
   virtual void SetUp(void) 
   {
      str0_ = "";
      str4_ = "1234abcd";
      str5_ = "1234abcdef";

      bd0_ = READHEX(str0_);
      bd4_ = READHEX(str4_);
      bd5_ = READHEX(str5_);

      bdr__ = BinaryDataRef();
      bdr0_ = BinaryDataRef(bd0_);
      bdr4_ = BinaryDataRef(bd4_);
      bdr5_ = BinaryDataRef(bd5_);
   }

   string str0_;
   string str4_;
   string str5_;

   BinaryData bd0_;
   BinaryData bd4_;
   BinaryData bd5_;

   BinaryDataRef bdr__;
   BinaryDataRef bdr0_;
   BinaryDataRef bdr4_;
   BinaryDataRef bdr5_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataRefTest, Constructor)
{
   BinaryDataRef a;
   BinaryDataRef b((uint8_t*)bd0_.getPtr(), bd0_.getSize());
   BinaryDataRef c((uint8_t*)bd0_.getPtr(), (uint8_t*)bd0_.getPtr());
   BinaryDataRef d((uint8_t*)bd4_.getPtr(), bd4_.getSize());
   BinaryDataRef e((uint8_t*)bd4_.getPtr(), (uint8_t*)bd4_.getPtr()+4);
   BinaryDataRef f(bd0_);
   BinaryDataRef g(bd4_);
   auto&& h = BinaryData::fromString(str0_);
   auto&& i = BinaryData::fromString(str4_);

   EXPECT_TRUE(a.getPtr()==NULL);
   EXPECT_EQ(a.getSize(), 0ULL);

   EXPECT_TRUE(b.getPtr()==NULL);
   EXPECT_EQ(b.getSize(), 0ULL);

   EXPECT_TRUE(c.getPtr()==NULL);
   EXPECT_EQ(c.getSize(), 0ULL);

   EXPECT_FALSE(d.getPtr()==NULL);
   EXPECT_EQ(d.getSize(), 4ULL);

   EXPECT_FALSE(e.getPtr()==NULL);
   EXPECT_EQ(e.getSize(), 4ULL);

   EXPECT_TRUE(f.getPtr()==NULL);
   EXPECT_EQ(f.getSize(), 0ULL);

   EXPECT_FALSE(g.getPtr()==NULL);
   EXPECT_EQ(g.getSize(), 4ULL);

   EXPECT_TRUE(h.getPtr()==NULL);
   EXPECT_EQ(h.getSize(), 0ULL);

   EXPECT_FALSE(i.getPtr()==NULL);
   EXPECT_EQ(i.getSize(), 8ULL);

   EXPECT_TRUE( a.empty());
   EXPECT_TRUE( b.empty());
   EXPECT_TRUE( c.empty());
   EXPECT_FALSE(d.empty());
   EXPECT_FALSE(e.empty());
   EXPECT_TRUE( f.empty());
   EXPECT_FALSE(g.empty());
   EXPECT_TRUE( h.empty());
   EXPECT_FALSE(i.empty());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataRefTest, PostConstruct)
{
   BinaryDataRef a,b,c,d,e,f,g,h,i;

   b.setRef((uint8_t*)bd0_.getPtr(), bd0_.getSize());
   c.setRef((uint8_t*)bd0_.getPtr(), (uint8_t*)bd0_.getPtr());
   d.setRef((uint8_t*)bd4_.getPtr(), bd4_.getSize());
   e.setRef((uint8_t*)bd4_.getPtr(), (uint8_t*)bd4_.getPtr()+4);
   f.setRef(bd0_);
   g.setRef(bd4_);
   h.setRef(str0_);
   i.setRef(str4_);

   EXPECT_TRUE(a.getPtr()==NULL);
   EXPECT_EQ(a.getSize(), 0ULL);

   EXPECT_TRUE(b.getPtr()==NULL);
   EXPECT_EQ(b.getSize(), 0ULL);

   EXPECT_TRUE(c.getPtr()==NULL);
   EXPECT_EQ(c.getSize(), 0ULL);

   EXPECT_FALSE(d.getPtr()==NULL);
   EXPECT_EQ(d.getSize(), 4ULL);

   EXPECT_FALSE(e.getPtr()==NULL);
   EXPECT_EQ(e.getSize(), 4ULL);

   EXPECT_TRUE(f.getPtr()==NULL);
   EXPECT_EQ(f.getSize(), 0ULL);

   EXPECT_FALSE(g.getPtr()==NULL);
   EXPECT_EQ(g.getSize(), 4ULL);

   EXPECT_FALSE(h.getPtr()==NULL);
   EXPECT_EQ(h.getSize(), 0ULL);

   EXPECT_FALSE(i.getPtr()==NULL);
   EXPECT_EQ(i.getSize(), 8ULL);

   EXPECT_TRUE( a.empty());
   EXPECT_TRUE( b.empty());
   EXPECT_TRUE( c.empty());
   EXPECT_FALSE(d.empty());
   EXPECT_FALSE(e.empty());
   EXPECT_TRUE( f.empty());
   EXPECT_FALSE(g.empty());
   EXPECT_TRUE(h.empty());
   EXPECT_FALSE(i.empty());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataRefTest, CopyTo)
{
   BinaryData a,b,c,d,e,f,g,h;
   bdr0_.copyTo(a);
   bdr4_.copyTo(b);

   c.resize(bdr5_.getSize());
   bdr5_.copyTo(c.getPtr());

   size_t sz = 2;
   d.resize(sz);
   e.resize(sz);
   bdr5_.copyTo(d.getPtr(), sz);
   bdr5_.copyTo(e.getPtr(), bdr5_.getSize()-sz, sz);

   f.copyFrom(bdr5_.getPtr(), bdr5_.getPtr()+sz);

   EXPECT_TRUE(a==bdr0_);
   EXPECT_TRUE(b==bdr4_);
   EXPECT_TRUE(c==bdr5_);
   EXPECT_TRUE(bdr5_.startsWith(d));
   EXPECT_TRUE(bdr5_.endsWith(e));
   EXPECT_TRUE(d==f);

   EXPECT_EQ(a.getSize(), 0ULL);
   EXPECT_EQ(b.getSize(), 4ULL);
   EXPECT_EQ(c.getSize(), 5ULL);
   EXPECT_EQ(d.getSize(), 2ULL);
   EXPECT_NE(b,c);

   g = bdr0_.copy();
   h = bdr4_.copy();

   EXPECT_EQ(g, bdr0_);
   EXPECT_EQ(h, bdr4_);
   EXPECT_EQ(g, bdr0_.copy());
   EXPECT_EQ(h, bdr4_.copy());

   EXPECT_EQ(bdr0_, g);
   EXPECT_EQ(bdr4_, h);
   EXPECT_EQ(bdr0_.copy(), g);
   EXPECT_EQ(bdr4_.copy(), h);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataRefTest, ToString)
{
   EXPECT_EQ(bdr0_.toHexStr(), str0_);
   EXPECT_EQ(bdr4_.toHexStr(), str4_);
   EXPECT_EQ(bdr4_.toHexStr(), str4_);

   string a,b;
   bdr0_.copyTo(a);
   bdr4_.copyTo(b);
   EXPECT_EQ(bd0_.toBinStr(), a);
   EXPECT_EQ(bd4_.toBinStr(), b);

   string stra("cdab3412");
   BinaryData bda = READHEX(stra);

   EXPECT_EQ(bdr4_.toHexStr(true), stra);
   EXPECT_EQ(bdr4_.toBinStr(true), bda.toBinStr());

}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataRefTest, Find)
{
   BinaryData a = READHEX("12");
   BinaryData b = READHEX("34");
   BinaryData c = READHEX("abcd");
   BinaryData d = READHEX("ff");

   EXPECT_EQ(bdr0_.find(bdr0_),     0);
   EXPECT_EQ(bdr0_.find(bdr4_),    -1);
   EXPECT_EQ(bdr0_.find(bdr4_, 2), -1);
   EXPECT_EQ(bdr4_.find(bdr0_),     0);
   EXPECT_EQ(bdr4_.find(bdr0_, 2),  2);

   EXPECT_EQ(bdr4_.find(a),  0);
   EXPECT_EQ(bdr4_.find(b),  1);
   EXPECT_EQ(bdr4_.find(c),  2);
   EXPECT_EQ(bdr4_.find(d), -1);

   EXPECT_EQ(bdr4_.find(a, 0),  0);
   EXPECT_EQ(bdr4_.find(b, 0),  1);
   EXPECT_EQ(bdr4_.find(c, 0),  2);
   EXPECT_EQ(bdr4_.find(d, 0), -1);

   EXPECT_EQ(bdr4_.find(a, 1), -1);
   EXPECT_EQ(bdr4_.find(b, 1),  1);
   EXPECT_EQ(bdr4_.find(c, 1),  2);
   EXPECT_EQ(bdr4_.find(d, 1), -1);

   EXPECT_EQ(bdr4_.find(a, 4), -1);
   EXPECT_EQ(bdr4_.find(b, 4), -1);
   EXPECT_EQ(bdr4_.find(c, 4), -1);
   EXPECT_EQ(bdr4_.find(d, 4), -1);

   EXPECT_EQ(bdr4_.find(a, 8), -1);
   EXPECT_EQ(bdr4_.find(b, 8), -1);
   EXPECT_EQ(bdr4_.find(c, 8), -1);
   EXPECT_EQ(bdr4_.find(d, 8), -1);

   EXPECT_EQ(bdr4_.find(a.getRef(), 0),  0);
   EXPECT_EQ(bdr4_.find(b.getRef(), 0),  1);
   EXPECT_EQ(bdr4_.find(c.getRef(), 0),  2);
   EXPECT_EQ(bdr4_.find(d.getRef(), 0), -1);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataRefTest, Contains)
{
   BinaryData a = READHEX("12");
   BinaryData b = READHEX("34");
   BinaryData c = READHEX("abcd");
   BinaryData d = READHEX("ff");

   EXPECT_TRUE( bdr0_.contains(bdr0_));
   EXPECT_FALSE(bdr0_.contains(bdr4_));
   EXPECT_FALSE(bdr0_.contains(bdr4_, 2));

   EXPECT_TRUE( bdr4_.contains(a));
   EXPECT_TRUE( bdr4_.contains(b));
   EXPECT_TRUE( bdr4_.contains(c));
   EXPECT_FALSE(bdr4_.contains(d));

   EXPECT_TRUE( bdr4_.contains(a, 0));
   EXPECT_TRUE( bdr4_.contains(b, 0));
   EXPECT_TRUE( bdr4_.contains(c, 0));
   EXPECT_FALSE(bdr4_.contains(d, 0));

   EXPECT_FALSE(bdr4_.contains(a, 1));
   EXPECT_TRUE( bdr4_.contains(b, 1));
   EXPECT_TRUE( bdr4_.contains(c, 1));
   EXPECT_FALSE(bdr4_.contains(d, 1));

   EXPECT_FALSE(bdr4_.contains(a, 4));
   EXPECT_FALSE(bdr4_.contains(b, 4));
   EXPECT_FALSE(bdr4_.contains(c, 4));
   EXPECT_FALSE(bdr4_.contains(d, 4));

   EXPECT_FALSE(bdr4_.contains(a, 8));
   EXPECT_FALSE(bdr4_.contains(b, 8));
   EXPECT_FALSE(bdr4_.contains(c, 8));
   EXPECT_FALSE(bdr4_.contains(d, 8));

   EXPECT_TRUE( bdr4_.contains(a.getRef(), 0));
   EXPECT_TRUE( bdr4_.contains(b.getRef(), 0));
   EXPECT_TRUE( bdr4_.contains(c.getRef(), 0));
   EXPECT_FALSE(bdr4_.contains(d.getRef(), 0));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataRefTest, StartsEndsWith)
{
   BinaryData a = READHEX("abcd");
   EXPECT_TRUE( bdr0_.startsWith(bdr0_));
   EXPECT_TRUE( bdr4_.startsWith(bdr0_));
   EXPECT_TRUE( bdr5_.startsWith(bdr4_));
   EXPECT_TRUE( bdr5_.startsWith(bdr5_));
   EXPECT_FALSE(bdr4_.startsWith(bdr5_));
   EXPECT_TRUE( bdr0_.startsWith(bdr0_));
   EXPECT_FALSE(bdr0_.startsWith(bdr4_));

   EXPECT_TRUE( bdr0_.startsWith(bd0_));
   EXPECT_TRUE( bdr4_.startsWith(bd0_));
   EXPECT_TRUE( bdr5_.startsWith(bd4_));
   EXPECT_TRUE( bdr5_.startsWith(bd5_));
   EXPECT_FALSE(bdr4_.startsWith(bd5_));
   EXPECT_TRUE( bdr0_.startsWith(bd0_));
   EXPECT_FALSE(bdr0_.startsWith(bd4_));
   EXPECT_FALSE(bdr5_.endsWith(a));
   EXPECT_TRUE( bdr4_.endsWith(a));
   EXPECT_FALSE(bdr0_.endsWith(a));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataRefTest, Inequality)
{
   EXPECT_FALSE(bdr0_ < bdr0_);
   EXPECT_TRUE( bdr0_ < bdr4_);
   EXPECT_TRUE( bdr0_ < bdr5_);

   EXPECT_FALSE(bdr4_ < bdr0_);
   EXPECT_FALSE(bdr4_ < bdr4_);
   EXPECT_TRUE( bdr4_ < bdr5_);

   EXPECT_FALSE(bdr5_ < bdr0_);
   EXPECT_FALSE(bdr5_ < bdr4_);
   EXPECT_FALSE(bdr5_ < bdr5_);

   EXPECT_FALSE(bdr0_ < bd0_);
   EXPECT_TRUE( bdr0_ < bd4_);
   EXPECT_TRUE( bdr0_ < bd5_);

   EXPECT_FALSE(bdr4_ < bd0_);
   EXPECT_FALSE(bdr4_ < bd4_);
   EXPECT_TRUE( bdr4_ < bd5_);

   EXPECT_FALSE(bdr5_ < bd0_);
   EXPECT_FALSE(bdr5_ < bd4_);
   EXPECT_FALSE(bdr5_ < bd5_);

   EXPECT_FALSE(bdr0_ > bdr0_);
   EXPECT_TRUE( bdr4_ > bdr0_);
   EXPECT_TRUE( bdr5_ > bdr0_);

   EXPECT_FALSE(bdr0_ > bdr4_);
   EXPECT_FALSE(bdr4_ > bdr4_);
   EXPECT_TRUE( bdr5_ > bdr4_);

   EXPECT_FALSE(bdr0_ > bdr5_);
   EXPECT_FALSE(bdr4_ > bdr5_);
   EXPECT_FALSE(bdr5_ > bdr5_);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BinaryDataRefTest, Equality)
{
   EXPECT_TRUE( bdr0_==bdr0_);
   EXPECT_TRUE( bdr4_==bdr4_);
   EXPECT_FALSE(bdr4_==bdr5_);
   EXPECT_TRUE( bdr0_!=bdr4_);
   EXPECT_TRUE( bdr0_!=bdr5_);
   EXPECT_TRUE( bdr4_!=bdr5_);
   EXPECT_FALSE(bdr4_!=bdr4_);

   EXPECT_TRUE( bdr0_==bd0_);
   EXPECT_TRUE( bdr4_==bd4_);
   EXPECT_FALSE(bdr4_==bd5_);
   EXPECT_TRUE( bdr0_!=bd4_);
   EXPECT_TRUE( bdr0_!=bd5_);
   EXPECT_TRUE( bdr4_!=bd5_);
   EXPECT_FALSE(bdr4_!=bd4_);
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
TEST(BitReadWriteTest, Writer8)
{
   BitPacker<uint8_t> bitp;

   //EXPECT_EQ( bitp.getValue(), 0);
   EXPECT_EQ( bitp.getBitsUsed(), 0ULL);
   EXPECT_EQ( bitp.getBinaryData(), READHEX("00"));

   bitp.putBit(true);
   //EXPECT_EQ( bitp.getValue(), 128);
   EXPECT_EQ( bitp.getBitsUsed(), 1ULL);
   EXPECT_EQ( bitp.getBinaryData(), READHEX("80"));

   bitp.putBit(false);
   //EXPECT_EQ( bitp.getValue(), 128);
   EXPECT_EQ( bitp.getBitsUsed(), 2ULL);
   EXPECT_EQ( bitp.getBinaryData(), READHEX("80"));

   bitp.putBit(true);
   //EXPECT_EQ( bitp.getValue(), 160);
   EXPECT_EQ( bitp.getBitsUsed(), 3ULL);
   EXPECT_EQ( bitp.getBinaryData(), READHEX("a0"));

   bitp.putBits(0, 2);
   //EXPECT_EQ( bitp.getValue(),  160);
   EXPECT_EQ( bitp.getBitsUsed(), 5ULL);
   EXPECT_EQ( bitp.getBinaryData(), READHEX("a0"));

   bitp.putBits(3, 3);
   //EXPECT_EQ( bitp.getValue(),  163);
   EXPECT_EQ( bitp.getBitsUsed(), 8ULL);
   EXPECT_EQ( bitp.getBinaryData(), READHEX("a3"));
}


////////////////////////////////////////////////////////////////////////////////
TEST(BitReadWriteTest, Writer16)
{
   BitPacker<uint16_t> bitp;

   //EXPECT_EQ( bitp.getValue(), 0);
   EXPECT_EQ( bitp.getBitsUsed(), 0ULL);
   EXPECT_EQ( bitp.getBinaryData(), READHEX("0000"));

   bitp.putBit(true);
   //EXPECT_EQ( bitp.getValue(), 0x8000);
   EXPECT_EQ( bitp.getBitsUsed(), 1ULL);
   EXPECT_EQ( bitp.getBinaryData(), READHEX("8000"));

   bitp.putBit(false);
   //EXPECT_EQ( bitp.getValue(), 0x8000);
   EXPECT_EQ( bitp.getBitsUsed(), 2ULL);
   EXPECT_EQ( bitp.getBinaryData(), READHEX("8000"));

   bitp.putBit(true);
   //EXPECT_EQ( bitp.getValue(), 0xa000);
   EXPECT_EQ( bitp.getBitsUsed(), 3ULL);
   EXPECT_EQ( bitp.getBinaryData(), READHEX("a000"));

   bitp.putBits(0, 2);
   //EXPECT_EQ( bitp.getValue(),  0xa000);
   EXPECT_EQ( bitp.getBitsUsed(), 5ULL);
   EXPECT_EQ( bitp.getBinaryData(), READHEX("a000"));

   bitp.putBits(3, 3);
   //EXPECT_EQ( bitp.getValue(),  0xa300);
   EXPECT_EQ( bitp.getBitsUsed(), 8ULL);
   EXPECT_EQ( bitp.getBinaryData(), READHEX("a300"));

   bitp.putBits(3, 8);
   //EXPECT_EQ( bitp.getValue(),  0xa303);
   EXPECT_EQ( bitp.getBitsUsed(), 16ULL);
   EXPECT_EQ( bitp.getBinaryData(), READHEX("a303"));
}


////////////////////////////////////////////////////////////////////////////////
TEST(BitReadWriteTest, Writer32)
{
   BitPacker<uint32_t> bitp;

   bitp.putBits(0xffffff00, 32);
   //EXPECT_EQ( bitp.getValue(),  0xffffff00);
   EXPECT_EQ( bitp.getBitsUsed(), 32ULL);
   EXPECT_EQ( bitp.getBinaryData(), READHEX("ffffff00"));
}

////////////////////////////////////////////////////////////////////////////////
TEST(BitReadWriteTest, Writer64)
{
   BitPacker<uint64_t> bitp;

   bitp.putBits(0xffffff00ffffffaaULL, 64);
   //EXPECT_EQ( bitp.getValue(),  0xffffff00ffffffaaULL);
   EXPECT_EQ( bitp.getBitsUsed(), 64ULL);
   EXPECT_EQ( bitp.getBinaryData(), READHEX("ffffff00ffffffaa"));

   BitPacker<uint64_t> bitp2;
   bitp2.putBits(0xff, 32);
   bitp2.putBits(0xff, 32);
   //EXPECT_EQ( bitp2.getValue(),  0x000000ff000000ffULL);
   EXPECT_EQ( bitp2.getBitsUsed(), 64ULL);
   EXPECT_EQ( bitp2.getBinaryData(), READHEX("000000ff000000ff"));
}

////////////////////////////////////////////////////////////////////////////////
TEST(BitReadWriteTest, Reader8)
{
   BitUnpacker<uint8_t> bitu;
   
   bitu.setValue(0xa3);
   EXPECT_TRUE( bitu.getBit());
   EXPECT_FALSE(bitu.getBit());
   EXPECT_TRUE( bitu.getBit());
   EXPECT_EQ(   bitu.getBits(2), 0);
   EXPECT_EQ(   bitu.getBits(3), 3);
}

////////////////////////////////////////////////////////////////////////////////
TEST(BitReadWriteTest, Reader16)
{
   BitUnpacker<uint16_t> bitu;
   
   bitu.setValue(0xa303);
   
   EXPECT_TRUE( bitu.getBit());
   EXPECT_FALSE(bitu.getBit());
   EXPECT_TRUE( bitu.getBit());
   EXPECT_EQ(   bitu.getBits(2), 0);
   EXPECT_EQ(   bitu.getBits(3), 3);
   EXPECT_EQ(   bitu.getBits(8), 3);
}


////////////////////////////////////////////////////////////////////////////////
TEST(BitReadWriteTest, Reader32)
{
   BitUnpacker<uint32_t> bitu(0xffffff00);
   EXPECT_EQ(bitu.getBits(32), 0xffffff00);
}

////////////////////////////////////////////////////////////////////////////////
TEST(BitReadWriteTest, Reader64)
{
   BitUnpacker<uint64_t> bitu(0xffffff00ffffffaaULL);
   EXPECT_EQ( bitu.getBits(64),  0xffffff00ffffffaaULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST(BinaryReadWriteTest, Writer)
{
   BinaryData out = READHEX("01""0100""013200aa""ff00ff00ff00ff00"
                            "ab""fdffff""fe013200aa""ffff00ff00ff00ff00");

   BinaryWriter bw;
   bw.put_uint8_t(1);                       EXPECT_EQ(bw.getSize(), 1ULL);
   bw.put_uint16_t(1);                      EXPECT_EQ(bw.getSize(), 3ULL);
   bw.put_uint32_t(0xaa003201);             EXPECT_EQ(bw.getSize(), 7ULL);
   bw.put_uint64_t(0x00ff00ff00ff00ffULL);  EXPECT_EQ(bw.getSize(), 15ULL);
   bw.put_var_int(0xab);                    EXPECT_EQ(bw.getSize(), 16ULL);
   bw.put_var_int(0xffff);                  EXPECT_EQ(bw.getSize(), 19ULL);
   bw.put_var_int(0xaa003201);              EXPECT_EQ(bw.getSize(), 24ULL);
   bw.put_var_int(0x00ff00ff00ff00ffULL);   EXPECT_EQ(bw.getSize(), 33ULL);

   EXPECT_EQ(bw.getData(), out);
   EXPECT_EQ(bw.getDataRef(), out.getRef());
}

////////////////////////////////////////////////////////////////////////////////
TEST(BinaryReadWriteTest, WriterEndian)
{
   BinaryData out = READHEX("01""0100""013200aa""ff00ff00ff00ff00"
                            "ab""fdffff""fe013200aa""ffff00ff00ff00ff00");

   BinaryWriter bw;
   bw.put_uint8_t(1);                          EXPECT_EQ(bw.getSize(), 1ULL);
   bw.put_uint16_t(0x0100, BE);                EXPECT_EQ(bw.getSize(), 3ULL);
   bw.put_uint32_t(0x013200aa, BE);            EXPECT_EQ(bw.getSize(), 7ULL);
   bw.put_uint64_t(0xff00ff00ff00ff00ULL, BE); EXPECT_EQ(bw.getSize(), 15ULL);
   bw.put_var_int(0xab);                       EXPECT_EQ(bw.getSize(), 16ULL);
   bw.put_var_int(0xffff);                     EXPECT_EQ(bw.getSize(), 19ULL);
   bw.put_var_int(0xaa003201);                 EXPECT_EQ(bw.getSize(), 24ULL);
   bw.put_var_int(0x00ff00ff00ff00ffULL);      EXPECT_EQ(bw.getSize(), 33ULL);
   EXPECT_EQ(bw.getData(), out);
   EXPECT_EQ(bw.getDataRef(), out.getRef());

   BinaryWriter bw2;
   bw2.put_uint8_t(1);                          EXPECT_EQ(bw2.getSize(), 1ULL);
   bw2.put_uint16_t(0x0001, LE);                EXPECT_EQ(bw2.getSize(), 3ULL);
   bw2.put_uint32_t(0xaa003201, LE);            EXPECT_EQ(bw2.getSize(), 7ULL);
   bw2.put_uint64_t(0x00ff00ff00ff00ffULL, LE); EXPECT_EQ(bw2.getSize(), 15ULL);
   bw2.put_var_int(0xab);                       EXPECT_EQ(bw2.getSize(), 16ULL);
   bw2.put_var_int(0xffff);                     EXPECT_EQ(bw2.getSize(), 19ULL);
   bw2.put_var_int(0xaa003201);                 EXPECT_EQ(bw2.getSize(), 24ULL);
   bw2.put_var_int(0x00ff00ff00ff00ffULL);      EXPECT_EQ(bw2.getSize(), 33ULL);
   EXPECT_EQ(bw2.getData(), out);
   EXPECT_EQ(bw2.getDataRef(), out.getRef());
}

////////////////////////////////////////////////////////////////////////////////
TEST(BinaryReadWriteTest, Reader)
{
   BinaryData in = READHEX("01""0100""013200aa""ff00ff00ff00ff00"
                           "ab""fdffff""fe013200aa""ffff00ff00ff00ff00");

   BinaryReader br(in);
   EXPECT_EQ(br.get_uint8_t(), 1ULL);
   EXPECT_EQ(br.get_uint16_t(), 1ULL);
   EXPECT_EQ(br.get_uint32_t(), 0xaa003201);
   EXPECT_EQ(br.get_uint64_t(), 0x00ff00ff00ff00ffULL);
   EXPECT_EQ(br.get_var_int(), 0xabULL);
   EXPECT_EQ(br.get_var_int(), 0xffffULL);
   EXPECT_EQ(br.get_var_int(), 0xaa003201ULL);
   EXPECT_EQ(br.get_var_int(), 0x00ff00ff00ff00ffULL);

   BinaryRefReader brr(in);
   EXPECT_EQ(brr.get_uint8_t(), 1ULL);
   EXPECT_EQ(brr.get_uint16_t(), 1ULL);
   EXPECT_EQ(brr.get_uint32_t(), 0xaa003201ULL);
   EXPECT_EQ(brr.get_uint64_t(), 0x00ff00ff00ff00ffULL);
   EXPECT_EQ(brr.get_var_int(), 0xabULL);
   EXPECT_EQ(brr.get_var_int(), 0xffffULL);
   EXPECT_EQ(brr.get_var_int(), 0xaa003201ULL);
   EXPECT_EQ(brr.get_var_int(), 0x00ff00ff00ff00ffULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST(BinaryReadWriteTest, ReaderEndian)
{
   BinaryData in = READHEX("01""0100""013200aa""ff00ff00ff00ff00"
                           "ab""fdffff""fe013200aa""ffff00ff00ff00ff00");

   BinaryReader br(in);
   auto val8 = br.get_uint8_t();
   EXPECT_EQ(val8, 1);                       

   auto val16 = br.get_uint16_t(LE);
   EXPECT_EQ(val16, 1);   

   auto val32 = br.get_uint32_t(LE);
   EXPECT_EQ(val32, 0xaa003201);   

   auto val64 = br.get_uint64_t(LE);
   EXPECT_EQ(val64, 0x00ff00ff00ff00ffULL);

   EXPECT_EQ(br.get_var_int(), 0xabULL);
   EXPECT_EQ(br.get_var_int(), 0xffffULL);
   EXPECT_EQ(br.get_var_int(), 0xaa003201ULL);
   EXPECT_EQ(br.get_var_int(), 0x00ff00ff00ff00ffULL);

   BinaryRefReader brr(in);
   val8 = brr.get_uint8_t();
   EXPECT_EQ(val8, 1);

   val16 = brr.get_uint16_t(LE);
   EXPECT_EQ(val16, 1ULL);

   val32 = brr.get_uint32_t(LE);
   EXPECT_EQ(val32, 0xaa003201ULL);

   val64 = brr.get_uint64_t(LE);
   EXPECT_EQ(val64, 0x00ff00ff00ff00ffULL);
   EXPECT_EQ(brr.get_var_int(), 0xabULL);
   EXPECT_EQ(brr.get_var_int(), 0xffffULL);
   EXPECT_EQ(brr.get_var_int(), 0xaa003201ULL);
   EXPECT_EQ(brr.get_var_int(), 0x00ff00ff00ff00ffULL);

   BinaryReader br2(in);
   EXPECT_EQ(br2.get_uint8_t(), 1);
   EXPECT_EQ(br2.get_uint16_t(ENDIAN_LITTLE), 1);
   EXPECT_EQ(br2.get_uint32_t(ENDIAN_LITTLE), 0xaa003201);
   EXPECT_EQ(br2.get_uint64_t(ENDIAN_LITTLE), 0x00ff00ff00ff00ffULL);
   EXPECT_EQ(br2.get_var_int(), 0xabULL);
   EXPECT_EQ(br2.get_var_int(), 0xffffULL);
   EXPECT_EQ(br2.get_var_int(), 0xaa003201ULL);
   EXPECT_EQ(br2.get_var_int(), 0x00ff00ff00ff00ffULL);

   BinaryRefReader brr2(in);
   EXPECT_EQ(brr2.get_uint8_t(), 1);
   EXPECT_EQ(brr2.get_uint16_t(ENDIAN_LITTLE), 1);
   EXPECT_EQ(brr2.get_uint32_t(ENDIAN_LITTLE), 0xaa003201);
   EXPECT_EQ(brr2.get_uint64_t(ENDIAN_LITTLE), 0x00ff00ff00ff00ffULL);
   EXPECT_EQ(brr2.get_var_int(), 0xabULL);
   EXPECT_EQ(brr2.get_var_int(), 0xffffULL);
   EXPECT_EQ(brr2.get_var_int(), 0xaa003201ULL);
   EXPECT_EQ(brr2.get_var_int(), 0x00ff00ff00ff00ffULL);

   BinaryReader brBE(in);
   EXPECT_EQ(brBE.get_uint8_t(), 1);
   EXPECT_EQ(brBE.get_uint16_t(BE), 0x0100);
   EXPECT_EQ(brBE.get_uint32_t(BE), 0x013200aaULL);
   EXPECT_EQ(brBE.get_uint64_t(BE), 0xff00ff00ff00ff00ULL);
   EXPECT_EQ(brBE.get_var_int(), 0xabULL);
   EXPECT_EQ(brBE.get_var_int(), 0xffffULL);
   EXPECT_EQ(brBE.get_var_int(), 0xaa003201ULL);
   EXPECT_EQ(brBE.get_var_int(), 0x00ff00ff00ff00ffULL);

   BinaryRefReader brrBE(in);
   EXPECT_EQ(brrBE.get_uint8_t(), 1);
   EXPECT_EQ(brrBE.get_uint16_t(BE), 0x0100);
   EXPECT_EQ(brrBE.get_uint32_t(BE), 0x013200aaULL);
   EXPECT_EQ(brrBE.get_uint64_t(BE), 0xff00ff00ff00ff00ULL);
   EXPECT_EQ(brrBE.get_var_int(), 0xabULL);
   EXPECT_EQ(brrBE.get_var_int(), 0xffffULL);
   EXPECT_EQ(brrBE.get_var_int(), 0xaa003201ULL);
   EXPECT_EQ(brrBE.get_var_int(), 0x00ff00ff00ff00ffULL);

   BinaryReader brBE2(in);
   EXPECT_EQ(brBE2.get_uint8_t(), 1);
   EXPECT_EQ(brBE2.get_uint16_t(ENDIAN_BIG), 0x0100);
   EXPECT_EQ(brBE2.get_uint32_t(ENDIAN_BIG), 0x013200aaULL);
   EXPECT_EQ(brBE2.get_uint64_t(ENDIAN_BIG), 0xff00ff00ff00ff00ULL);
   EXPECT_EQ(brBE2.get_var_int(), 0xabULL);
   EXPECT_EQ(brBE2.get_var_int(), 0xffffULL);
   EXPECT_EQ(brBE2.get_var_int(), 0xaa003201ULL);
   EXPECT_EQ(brBE2.get_var_int(), 0x00ff00ff00ff00ffULL);

   BinaryRefReader brrBE2(in);
   EXPECT_EQ(brrBE2.get_uint8_t(), 1);
   EXPECT_EQ(brrBE2.get_uint16_t(ENDIAN_BIG), 0x0100);
   EXPECT_EQ(brrBE2.get_uint32_t(ENDIAN_BIG), 0x013200aaULL);
   EXPECT_EQ(brrBE2.get_uint64_t(ENDIAN_BIG), 0xff00ff00ff00ff00ULL);
   EXPECT_EQ(brrBE2.get_var_int(), 0xabULL);
   EXPECT_EQ(brrBE2.get_var_int(), 0xffffULL);
   EXPECT_EQ(brrBE2.get_var_int(), 0xaa003201ULL);
   EXPECT_EQ(brrBE2.get_var_int(), 0x00ff00ff00ff00ffULL);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BtcUtilsTest : public ::testing::Test
{
protected:
   virtual void SetUp(void) 
   {
      homedir_ = string("./fakehomedir");
      DBUtils::removeDirectory(homedir_);
      mkdir(homedir_);
         
      Armory::Config::parseArgs({
         "--datadir=./fakehomedir",
         "--offline" },
         Armory::Config::ProcessType::DB);

      rawHead_ = READHEX(
         "010000001d8f4ec0443e1f19f305e488c1085c95de7cc3fd25e0d2c5bb5d0000"
         "000000009762547903d36881a86751f3f5049e23050113f779735ef82734ebf0"
         "b4450081d8c8c84db3936a1a334b035b");
      headHashLE_ = READHEX(
         "1195e67a7a6d0674bbd28ae096d602e1f038c8254b49dfe79d47000000000000");
      headHashBE_ = READHEX(
         "000000000000479de7df494b25c838f0e102d696e08ad2bb74066d7a7ae69511");

      satoshiPubKey_ = READHEX( "04"
         "fc9702847840aaf195de8442ebecedf5b095cdbb9bc716bda9110971b28a49e0"
         "ead8564ff0db22209e0374782c093bb899692d524e9d6a6956e7c5ecbcd68284");
      satoshiHash160_ = READHEX("65a4358f4691660849d9f235eb05f11fabbd69fa");

      prevHashCB_  = READHEX(
         "0000000000000000000000000000000000000000000000000000000000000000");
      prevHashReg_ = READHEX(
         "894862e362905c6075074d9ec4b4e2dc34720089b1e9ef4738ee1b13f3bdcdb7");
   }

   virtual void TearDown(void)
   {
      DBUtils::removeDirectory(homedir_);
      Armory::Config::reset();
   }

   BinaryData rawHead_;
   BinaryData headHashLE_;
   BinaryData headHashBE_;

   BinaryData satoshiPubKey_;
   BinaryData satoshiHash160_;

   BinaryData prevHashCB_;
   BinaryData prevHashReg_;

   string homedir_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, ReadVarInt)
{
   BinaryData vi0 = READHEX("00");
   BinaryData vi1 = READHEX("21");
   BinaryData vi3 = READHEX("fdff00");
   BinaryData vi5 = READHEX("fe00000100");
   BinaryData vi9 = READHEX("ff0010a5d4e8000000");

   uint64_t v = 0;
   uint64_t w = 33;
   uint64_t x = 255;
   uint64_t y = 65536;
   uint64_t z = 1000000000000ULL;

   BinaryRefReader brr;
   pair<uint64_t, uint8_t> a;

   brr.setNewData(vi0);
   a = BtcUtils::readVarInt(brr);
   EXPECT_EQ(a.first,   v);
   EXPECT_EQ(a.second,  1);

   brr.setNewData(vi1);
   a = BtcUtils::readVarInt(brr);
   EXPECT_EQ(a.first,   w);
   EXPECT_EQ(a.second,  1);

   brr.setNewData(vi3);
   a = BtcUtils::readVarInt(brr);
   EXPECT_EQ(a.first,   x);
   EXPECT_EQ(a.second,  3);

   brr.setNewData(vi5);
   a = BtcUtils::readVarInt(brr);
   EXPECT_EQ(a.first,   y);
   EXPECT_EQ(a.second,  5);

   brr.setNewData(vi9);
   a = BtcUtils::readVarInt(brr);
   EXPECT_EQ(a.first,   z);
   EXPECT_EQ(a.second,  9);

   // Just the length
   EXPECT_EQ(BtcUtils::readVarIntLength(vi0.getPtr()), 1ULL);
   EXPECT_EQ(BtcUtils::readVarIntLength(vi1.getPtr()), 1ULL);
   EXPECT_EQ(BtcUtils::readVarIntLength(vi3.getPtr()), 3ULL);
   EXPECT_EQ(BtcUtils::readVarIntLength(vi5.getPtr()), 5ULL);
   EXPECT_EQ(BtcUtils::readVarIntLength(vi9.getPtr()), 9ULL);

   EXPECT_EQ(BtcUtils::calcVarIntSize(v), 1ULL);
   EXPECT_EQ(BtcUtils::calcVarIntSize(w), 1ULL);
   EXPECT_EQ(BtcUtils::calcVarIntSize(x), 3ULL);
   EXPECT_EQ(BtcUtils::calcVarIntSize(y), 5ULL);
   EXPECT_EQ(BtcUtils::calcVarIntSize(z), 9ULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, Num2Str)
{
   EXPECT_EQ(BtcUtils::numToStrWCommas(0),         string("0"));
   EXPECT_EQ(BtcUtils::numToStrWCommas(100),       string("100"));
   EXPECT_EQ(BtcUtils::numToStrWCommas(-100),      string("-100"));
   EXPECT_EQ(BtcUtils::numToStrWCommas(999),       string("999"));
   EXPECT_EQ(BtcUtils::numToStrWCommas(1234),      string("1,234"));
   EXPECT_EQ(BtcUtils::numToStrWCommas(-1234),     string("-1,234"));
   EXPECT_EQ(BtcUtils::numToStrWCommas(12345678),  string("12,345,678"));
   EXPECT_EQ(BtcUtils::numToStrWCommas(-12345678), string("-12,345,678"));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, PackBits)
{
   list<bool>::iterator iter, iter2;
   list<bool> bitList;

   bitList = BtcUtils::UnpackBits( READHEX("00"), 0);
   EXPECT_EQ(bitList.size(), 0ULL);

   bitList = BtcUtils::UnpackBits( READHEX("00"), 3);
   EXPECT_EQ(bitList.size(), 3ULL);
   iter = bitList.begin(); 
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   
   
   bitList = BtcUtils::UnpackBits( READHEX("00"), 8);
   EXPECT_EQ(bitList.size(), 8ULL);
   iter = bitList.begin(); 
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;

   bitList = BtcUtils::UnpackBits( READHEX("017f"), 8);
   EXPECT_EQ(bitList.size(), 8ULL);
   iter = bitList.begin(); 
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_TRUE( *iter);  iter++;


   bitList = BtcUtils::UnpackBits( READHEX("017f"), 12);
   EXPECT_EQ(bitList.size(), 12ULL);
   iter = bitList.begin(); 
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_TRUE( *iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_TRUE( *iter);  iter++;
   EXPECT_TRUE( *iter);  iter++;
   EXPECT_TRUE( *iter);  iter++;

   bitList = BtcUtils::UnpackBits( READHEX("017f"), 16);
   EXPECT_EQ(bitList.size(), 16ULL);
   iter = bitList.begin(); 
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_TRUE( *iter);  iter++;
   EXPECT_FALSE(*iter);  iter++;
   EXPECT_TRUE( *iter);  iter++;
   EXPECT_TRUE( *iter);  iter++;
   EXPECT_TRUE( *iter);  iter++;
   EXPECT_TRUE( *iter);  iter++;
   EXPECT_TRUE( *iter);  iter++;
   EXPECT_TRUE( *iter);  iter++;
   EXPECT_TRUE( *iter);  iter++;


   BinaryData packed;
   packed = BtcUtils::PackBits(bitList);
   EXPECT_EQ(packed, READHEX("017f"));

   bitList = BtcUtils::UnpackBits( READHEX("017f"), 12);
   packed = BtcUtils::PackBits(bitList);
   EXPECT_EQ(packed, READHEX("0170"));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, SimpleHash)
{
   BinaryData hashOut;

   // sha256(sha256(X))
   BtcUtils::getHash256(rawHead_.getPtr(), rawHead_.getSize(), hashOut);
   EXPECT_EQ(hashOut, headHashLE_);
   EXPECT_EQ(hashOut, headHashBE_.copySwapEndian());

   BtcUtils::getHash256(rawHead_.getPtr(), rawHead_.getSize(), hashOut);
   EXPECT_EQ(hashOut, headHashLE_);
   EXPECT_EQ(hashOut, headHashBE_.copySwapEndian());

   hashOut = BtcUtils::getHash256(rawHead_.getPtr(), rawHead_.getSize());
   EXPECT_EQ(hashOut, headHashLE_);

   BtcUtils::getHash256(rawHead_, hashOut);
   EXPECT_EQ(hashOut, headHashLE_);

   BtcUtils::getHash256(rawHead_.getRef(), hashOut);
   EXPECT_EQ(hashOut, headHashLE_);

   hashOut = BtcUtils::getHash256(rawHead_);
   EXPECT_EQ(hashOut, headHashLE_);

   // ripemd160(sha256(X))
   BtcUtils::getHash160(satoshiPubKey_.getPtr(), satoshiPubKey_.getSize(), hashOut);
   EXPECT_EQ(hashOut, satoshiHash160_);

   BtcUtils::getHash160(satoshiPubKey_.getPtr(), satoshiPubKey_.getSize(), hashOut);
   EXPECT_EQ(hashOut, satoshiHash160_);

   hashOut = BtcUtils::getHash160(satoshiPubKey_.getPtr(), satoshiPubKey_.getSize());
   EXPECT_EQ(hashOut, satoshiHash160_);

   BtcUtils::getHash160(satoshiPubKey_, hashOut);
   EXPECT_EQ(hashOut, satoshiHash160_);

   BtcUtils::getHash160(satoshiPubKey_.getRef(), hashOut);
   EXPECT_EQ(hashOut, satoshiHash160_);

   hashOut = BtcUtils::getHash160(satoshiPubKey_);
   EXPECT_EQ(hashOut, satoshiHash160_);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, BotchedArmoryHMAC)
{
   auto sha_abcd = READHEX("88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589");
   auto sha_efgh = READHEX("e5e088a0b66163a0a26a5e053d2a4496dc16ab6e0e3dd1adf2d16aa84a078c9d");
   auto hmac_1 = READHEX("edd2c945dc57a5eecdb4dbb2db8ae4f33f9669046e47acb517c8f6bcdf6ee591");

   auto abcd = BinaryData::fromString("abcd");
   auto efgh = BinaryData::fromString("efgh");

   EXPECT_EQ(BtcUtils::getSha256(abcd), sha_abcd);
   EXPECT_EQ(BtcUtils::getSha256(efgh), sha_efgh);
   EXPECT_EQ(BtcUtils::getBotchedArmoryHMAC256(abcd, efgh), hmac_1);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, TxOutScriptID_Hash160)
{
   //TXOUT_SCRIPT_STDHASH160,
   //TXOUT_SCRIPT_STDPUBKEY65,
   //TXOUT_SCRIPT_STDPUBKEY33,
   //TXOUT_SCRIPT_MULTISIG,
   //TXOUT_SCRIPT_P2SH,
   //TXOUT_SCRIPT_NONSTANDARD,

   BinaryData script = READHEX("76a914a134408afa258a50ed7a1d9817f26b63cc9002cc88ac");
   BinaryData a160   = READHEX(  "a134408afa258a50ed7a1d9817f26b63cc9002cc");
   BinaryData unique = READHEX("00a134408afa258a50ed7a1d9817f26b63cc9002cc");
   TXOUT_SCRIPT_TYPE scrType = BtcUtils::getTxOutScriptType(script);
   EXPECT_EQ(scrType, TXOUT_SCRIPT_STDHASH160 );
   EXPECT_EQ(BtcUtils::getTxOutRecipientAddr(script), a160 );
   EXPECT_EQ(BtcUtils::getTxOutRecipientAddr(script, scrType), a160 );
   EXPECT_EQ(BtcUtils::getTxOutScrAddr(script), unique );
   EXPECT_EQ(BtcUtils::getTxOutScrAddr(script, scrType), unique );
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, TxOutScriptID_PubKey65)
{
   BinaryData script = READHEX(
      "4104b0bd634234abbb1ba1e986e884185c61cf43e001f9137f23c2c409273eb1"
      "6e6537a576782eba668a7ef8bd3b3cfb1edb7117ab65129b8a2e681f3c1e0908ef7bac");
   BinaryData a160   = READHEX(  "e24b86bff5112623ba67c63b6380636cbdf1a66d");
   BinaryData unique = READHEX("00e24b86bff5112623ba67c63b6380636cbdf1a66d");
   TXOUT_SCRIPT_TYPE scrType = BtcUtils::getTxOutScriptType(script);
   EXPECT_EQ(scrType, TXOUT_SCRIPT_STDPUBKEY65 );
   EXPECT_EQ(BtcUtils::getTxOutRecipientAddr(script), a160 );
   EXPECT_EQ(BtcUtils::getTxOutRecipientAddr(script, scrType), a160 );
   EXPECT_EQ(BtcUtils::getTxOutScrAddr(script), unique );
   EXPECT_EQ(BtcUtils::getTxOutScrAddr(script, scrType), unique );
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, TxOutScriptID_PubKey33)
{
   BinaryData script = READHEX(
      "21024005c945d86ac6b01fb04258345abea7a845bd25689edb723d5ad4068ddd3036ac");
   BinaryData a160   = READHEX(  "0c1b83d01d0ffb2bccae606963376cca3863a7ce");
   BinaryData unique = READHEX("000c1b83d01d0ffb2bccae606963376cca3863a7ce");
   TXOUT_SCRIPT_TYPE scrType = BtcUtils::getTxOutScriptType(script);
   EXPECT_EQ(scrType, TXOUT_SCRIPT_STDPUBKEY33 );
   EXPECT_EQ(BtcUtils::getTxOutRecipientAddr(script), a160 );
   EXPECT_EQ(BtcUtils::getTxOutRecipientAddr(script, scrType), a160 );
   EXPECT_EQ(BtcUtils::getTxOutScrAddr(script), unique );
   EXPECT_EQ(BtcUtils::getTxOutScrAddr(script, scrType), unique );
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, TxOutScriptID_NonStd)
{
   // This was from block 150951 which was erroneously produced by MagicalTux
   // This is not only non-standard, it's non-spendable
   BinaryData script = READHEX("76a90088ac");
   BinaryData a160   = BtcUtils::BadAddress();
   BinaryData unique = READHEX("ff") + BtcUtils::getHash160(READHEX("76a90088ac"));
   TXOUT_SCRIPT_TYPE scrType = BtcUtils::getTxOutScriptType(script);
   EXPECT_EQ(scrType, TXOUT_SCRIPT_NONSTANDARD );
   EXPECT_EQ(BtcUtils::getTxOutRecipientAddr(script), a160 );
   EXPECT_EQ(BtcUtils::getTxOutRecipientAddr(script, scrType), a160 );
   EXPECT_EQ(BtcUtils::getTxOutScrAddr(script), unique );
   EXPECT_EQ(BtcUtils::getTxOutScrAddr(script, scrType), unique );
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, TxOutScriptID_P2SH)
{
   // P2SH script from tx: 4ac04b4830d115eb9a08f320ef30159cc107dfb72b29bbc2f370093f962397b4 (TxOut: 1)
   // Spent in tx:         fd16d6bbf1a3498ca9777b9d31ceae883eb8cb6ede1fafbdd218bae107de66fe (TxIn: 1)
   // P2SH address:        3Lip6sxQymNr9LD2cAVp6wLrw8xdKBdYFG
   // Hash160:             d0c15a7d41500976056b3345f542d8c944077c8a
   BinaryData script = READHEX("a914d0c15a7d41500976056b3345f542d8c944077c8a87"); // send to P2SH
   BinaryData a160 =   READHEX(  "d0c15a7d41500976056b3345f542d8c944077c8a");
   BinaryData unique = READHEX("05d0c15a7d41500976056b3345f542d8c944077c8a");
   TXOUT_SCRIPT_TYPE scrType = BtcUtils::getTxOutScriptType(script);
   EXPECT_EQ(scrType, TXOUT_SCRIPT_P2SH);
   EXPECT_EQ(BtcUtils::getTxOutRecipientAddr(script), a160 );
   EXPECT_EQ(BtcUtils::getTxOutRecipientAddr(script, scrType), a160 );
   EXPECT_EQ(BtcUtils::getTxOutScrAddr(script), unique );
   EXPECT_EQ(BtcUtils::getTxOutScrAddr(script, scrType), unique );
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, TxOutScriptID_Multisig)
{
   BinaryData script = READHEX(
      "5221034758cefcb75e16e4dfafb32383b709fa632086ea5ca982712de6add93"
      "060b17a2103fe96237629128a0ae8c3825af8a4be8fe3109b16f62af19cec0b1"
      "eb93b8717e252ae");
   BinaryData pub1   = READHEX(
      "034758cefcb75e16e4dfafb32383b709fa632086ea5ca982712de6add93060b17a");
   BinaryData pub2   = READHEX(
      "03fe96237629128a0ae8c3825af8a4be8fe3109b16f62af19cec0b1eb93b8717e2");
   BinaryData addr1  = READHEX("b3348abf9dd2d1491359f937e2af64b1bb6d525a");
   BinaryData addr2  = READHEX("785652a6b8e721e80ffa353e5dfd84f0658284a9");
   BinaryData a160   = BtcUtils::BadAddress();
   BinaryData unique = READHEX(
      "fe0202785652a6b8e721e80ffa353e5dfd84f0658284a9b3348abf9dd2d14913"
      "59f937e2af64b1bb6d525a");

   TXOUT_SCRIPT_TYPE scrType = BtcUtils::getTxOutScriptType(script);
   EXPECT_EQ(scrType, TXOUT_SCRIPT_MULTISIG);
   EXPECT_EQ(BtcUtils::getTxOutRecipientAddr(script), a160 );
   EXPECT_EQ(BtcUtils::getTxOutRecipientAddr(script, scrType), a160 );
   EXPECT_EQ(BtcUtils::getTxOutScrAddr(script), unique );
   EXPECT_EQ(BtcUtils::getTxOutScrAddr(script, scrType), unique );
}


////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, TxOutScriptID_MultiList)
{
   BinaryData script = READHEX(
      "5221034758cefcb75e16e4dfafb32383b709fa632086ea5ca982712de6add930"
      "60b17a2103fe96237629128a0ae8c3825af8a4be8fe3109b16f62af19cec0b1e"
      "b93b8717e252ae");
   BinaryData addr0  = READHEX("785652a6b8e721e80ffa353e5dfd84f0658284a9");
   BinaryData addr1  = READHEX("b3348abf9dd2d1491359f937e2af64b1bb6d525a");
   BinaryData a160   = BtcUtils::BadAddress();
   BinaryData unique = READHEX(
      "fe0202785652a6b8e721e80ffa353e5dfd84f0658284a9b3348abf9dd2d14913"
      "59f937e2af64b1bb6d525a");

   BinaryData pub0 = READHEX(
      "034758cefcb75e16e4dfafb32383b709fa632086ea5ca982712de6add93060b17a");
   BinaryData pub1 = READHEX(
      "03fe96237629128a0ae8c3825af8a4be8fe3109b16f62af19cec0b1eb93b8717e2");

   vector<BinaryData> a160List;
   uint32_t M;

   M = BtcUtils::getMultisigAddrList(script, a160List);
   EXPECT_EQ(M, 2ULL);
   EXPECT_EQ(a160List.size(), 2ULL); // N
   
   EXPECT_EQ(a160List[0], addr0);
   EXPECT_EQ(a160List[1], addr1);

   vector<BinaryData> pkList;
   M = BtcUtils::getMultisigPubKeyList(script, pkList);
   EXPECT_EQ(M, 2ULL);
   EXPECT_EQ(pkList.size(), 2ULL); // N
   
   EXPECT_EQ(pkList[0], pub0);
   EXPECT_EQ(pkList[1], pub1);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, TxInScriptID_StdUncompr)
{
   BinaryData script = READHEX(
      "493046022100b9daf2733055be73ae00ee0c5d78ca639d554fe779f163396c1a"
      "39b7913e7eac02210091f0deeb2e510c74354afb30cc7d8fbac81b1ca8b39406"
      "13379adc41a6ffd226014104b1537fa5bc2242d25ebf54f31e76ebabe0b3de4a"
      "4dccd9004f058d6c2caa5d31164252e1e04e5df627fae7adec27fa9d40c271fc"
      "4d30ff375ef6b26eba192bac");
   BinaryData a160 = READHEX("c42a8290196b2c5bcb35471b45aa0dc096baed5e");
   BinaryData prevHash = prevHashReg_;

   TXIN_SCRIPT_TYPE scrType = BtcUtils::getTxInScriptType( script, prevHash);
   EXPECT_EQ(scrType,  TXIN_SCRIPT_STDUNCOMPR);
   EXPECT_EQ(BtcUtils::getTxInAddr(script, prevHash), a160);
   EXPECT_EQ(BtcUtils::getTxInAddr(script, prevHash, scrType), a160);
   EXPECT_EQ(BtcUtils::getTxInAddrFromType(script,  scrType), a160);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, TxInScriptID_StdCompr)
{
   BinaryData script = READHEX(
      "47304402205299224886e5e3402b0e9fa3527bcfe1d73c4e2040f18de8dd17f1"
      "16e3365a1102202590dcc16c4b711daae6c37977ba579ca65bcaa8fba2bd7168"
      "a984be727ccf7a01210315122ff4d41d9fe3538a0a8c6c7f813cf12a901069a4"
      "3d6478917246dc92a782");
   BinaryData a160 = READHEX("03214fc1433a287e964d6c4242093c34e4ed0001");
   BinaryData prevHash = prevHashReg_;

   TXIN_SCRIPT_TYPE scrType = BtcUtils::getTxInScriptType(script, prevHash);
   EXPECT_EQ(scrType,  TXIN_SCRIPT_STDCOMPR);
   EXPECT_EQ(BtcUtils::getTxInAddr(script, prevHash), a160);
   EXPECT_EQ(BtcUtils::getTxInAddr(script, prevHash, scrType), a160);
   EXPECT_EQ(BtcUtils::getTxInAddrFromType(script,  scrType), a160);
}


////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, TxInScriptID_Coinbase)
{
   BinaryData script = READHEX(
      "0310920304000071c3124d696e656420627920425443204775696c640800b75f950e000000");
   BinaryData a160 =  BtcUtils::BadAddress();
   BinaryData prevHash = prevHashCB_;

   TXIN_SCRIPT_TYPE scrType = BtcUtils::getTxInScriptType(script, prevHash);
   EXPECT_EQ(scrType, TXIN_SCRIPT_COINBASE);
   EXPECT_EQ(BtcUtils::getTxInAddr(script, prevHash), a160);
   EXPECT_EQ(BtcUtils::getTxInAddr(script, prevHash, scrType), a160);
   EXPECT_EQ(BtcUtils::getTxInAddrFromType(script,  scrType), a160);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, TxInScriptID_SpendPubKey)
{
   BinaryData script = READHEX(
      "47304402201ffc44394e5a3dd9c8b55bdc12147e18574ac945d15dac026793bf"
      "3b8ff732af022035fd832549b5176126f735d87089c8c1c1319447a458a09818"
      "e173eaf0c2eef101");
   BinaryData a160 =  BtcUtils::BadAddress();
   BinaryData prevHash = prevHashReg_;

   TXIN_SCRIPT_TYPE scrType = BtcUtils::getTxInScriptType(script, prevHash);
   EXPECT_EQ(scrType, TXIN_SCRIPT_SPENDPUBKEY);
   EXPECT_EQ(BtcUtils::getTxInAddr(script, prevHash), a160);
   EXPECT_EQ(BtcUtils::getTxInAddr(script, prevHash, scrType), a160);
   EXPECT_EQ(BtcUtils::getTxInAddrFromType(script,  scrType), a160);
   //txInHash160s.push_back( READHEX("957efec6af757ccbbcf9a436f0083c5ddaa3bf1d")); // this one can't be determined
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, TxInScriptID_SpendMultisig)
{

   BinaryData script = READHEX(
      "004830450221009254113fa46918f299b1d18ec918613e56cffbeba0960db05f"
      "66b51496e5bf3802201e229de334bd753a2b08b36cc3f38f5263a23e9714a737"
      "520db45494ec095ce80148304502206ee62f539d5cd94f990b7abfda77750f58"
      "ff91043c3f002501e5448ef6dba2520221009d29229cdfedda1dd02a1a90bb71"
      "b30b77e9c3fc28d1353f054c86371f6c2a8101");
   BinaryData a160 =  BtcUtils::BadAddress();
   BinaryData prevHash = prevHashReg_;
   TXIN_SCRIPT_TYPE scrType = BtcUtils::getTxInScriptType(script, prevHash);
   EXPECT_EQ(scrType, TXIN_SCRIPT_SPENDMULTI);
   EXPECT_EQ(BtcUtils::getTxInAddr(script, prevHash), a160);
   EXPECT_EQ(BtcUtils::getTxInAddr(script, prevHash, scrType), a160);
   EXPECT_EQ(BtcUtils::getTxInAddrFromType(script,  scrType), a160);


   vector<BinaryDataRef> scrParts = BtcUtils::splitPushOnlyScriptRefs(script);
   BinaryData zero = READHEX("00");
   BinaryData sig1 = READHEX(
      "30450221009254113fa46918f299b1d18ec918613e56cffbeba0960db05f66b5"
      "1496e5bf3802201e229de334bd753a2b08b36cc3f38f5263a23e9714a737520d"
      "b45494ec095ce801");
   BinaryData sig2 = READHEX(
      "304502206ee62f539d5cd94f990b7abfda77750f58ff91043c3f002501e5448e"
      "f6dba2520221009d29229cdfedda1dd02a1a90bb71b30b77e9c3fc28d1353f05"
      "4c86371f6c2a8101");

   EXPECT_EQ(scrParts.size(), 3ULL);
   EXPECT_EQ(scrParts[0], zero);
   EXPECT_EQ(scrParts[1], sig1);
   EXPECT_EQ(scrParts[2], sig2);

   //BinaryData p2sh = READHEX("5221034758cefcb75e16e4dfafb32383b709fa632086ea5ca982712de6add93060b17a2103fe96237629128a0ae8c3825af8a4be8fe3109b16f62af19cec0b1eb93b8717e252ae");
   //BinaryData pub1 = READHEX("034758cefcb75e16e4dfafb32383b709fa632086ea5ca982712de6add93060b17a");
   //BinaryData pub1 = READHEX("03fe96237629128a0ae8c3825af8a4be8fe3109b16f62af19cec0b1eb93b8717e2");
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, TxInScriptID_SpendP2SH)
{

   // Spending P2SH output as above:  fd16d6bbf1a3498ca9777b9d31ceae883eb8cb6ede1fafbdd218bae107de66fe (TxIn: 1, 219 B)
   // Leading 0x00 byte is required due to a bug in OP_CHECKMULTISIG
   BinaryData script = READHEX(
      "004830450221009254113fa46918f299b1d18ec918613e56cffbeba0960db05f"
      "66b51496e5bf3802201e229de334bd753a2b08b36cc3f38f5263a23e9714a737"
      "520db45494ec095ce80148304502206ee62f539d5cd94f990b7abfda77750f58"
      "ff91043c3f002501e5448ef6dba2520221009d29229cdfedda1dd02a1a90bb71"
      "b30b77e9c3fc28d1353f054c86371f6c2a8101475221034758cefcb75e16e4df"
      "afb32383b709fa632086ea5ca982712de6add93060b17a2103fe96237629128a"
      "0ae8c3825af8a4be8fe3109b16f62af19cec0b1eb93b8717e252ae");
   BinaryData a160 =  READHEX("d0c15a7d41500976056b3345f542d8c944077c8a");
   BinaryData prevHash = prevHashReg_;
   TXIN_SCRIPT_TYPE scrType = BtcUtils::getTxInScriptType(script, prevHash);
   EXPECT_EQ(scrType, TXIN_SCRIPT_SPENDP2SH);
   EXPECT_EQ(BtcUtils::getTxInAddr(script, prevHash), a160);
   EXPECT_EQ(BtcUtils::getTxInAddr(script, prevHash, scrType), a160);
   EXPECT_EQ(BtcUtils::getTxInAddrFromType(script,  scrType), a160);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, BitsToDifficulty)
{

   double a = BtcUtils::convertDiffBitsToDouble(READHEX("ffff001d"));
   double b = BtcUtils::convertDiffBitsToDouble(READHEX("be2f021a"));
   double c = BtcUtils::convertDiffBitsToDouble(READHEX("3daa011a"));

   EXPECT_DOUBLE_EQ(a, 1.0);
   EXPECT_DOUBLE_EQ(b, 7672999.920164138);
   EXPECT_DOUBLE_EQ(c, 10076292.883418716);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BtcUtilsTest, ScriptToOpCodes)
{
   BinaryData complexScript = READHEX(
      "526b006b7dac7ca9143cd1def404e12a85ead2b4d3f5f9f817fb0d46ef879a6c"
      "936b7dac7ca9146a4e7d5f798e90e84db9244d4805459f87275943879a6c936b"
      "7dac7ca914486efdd300987a054510b4ce1148d4ad290d911e879a6c936b6c6ca2");

   vector<string> opstr;
   opstr.reserve(40);
   opstr.push_back(string("OP_2"));
   opstr.push_back(string("OP_TOALTSTACK"));
   opstr.push_back(string("OP_0"));
   opstr.push_back(string("OP_TOALTSTACK"));
   opstr.push_back(string("OP_TUCK"));
   opstr.push_back(string("OP_CHECKSIG"));
   opstr.push_back(string("OP_SWAP"));
   opstr.push_back(string("OP_HASH160"));
   opstr.push_back(string("[PUSHDATA -- 20 BYTES:]"));
   opstr.push_back(string("3cd1def404e12a85ead2b4d3f5f9f817fb0d46ef"));
   opstr.push_back(string("OP_EQUAL"));
   opstr.push_back(string("OP_BOOLAND"));
   opstr.push_back(string("OP_FROMALTSTACK"));
   opstr.push_back(string("OP_ADD"));
   opstr.push_back(string("OP_TOALTSTACK"));
   opstr.push_back(string("OP_TUCK"));
   opstr.push_back(string("OP_CHECKSIG"));
   opstr.push_back(string("OP_SWAP"));
   opstr.push_back(string("OP_HASH160"));
   opstr.push_back(string("[PUSHDATA -- 20 BYTES:]"));
   opstr.push_back(string("6a4e7d5f798e90e84db9244d4805459f87275943"));
   opstr.push_back(string("OP_EQUAL"));
   opstr.push_back(string("OP_BOOLAND"));
   opstr.push_back(string("OP_FROMALTSTACK"));
   opstr.push_back(string("OP_ADD"));
   opstr.push_back(string("OP_TOALTSTACK"));
   opstr.push_back(string("OP_TUCK"));
   opstr.push_back(string("OP_CHECKSIG"));
   opstr.push_back(string("OP_SWAP"));
   opstr.push_back(string("OP_HASH160"));
   opstr.push_back(string("[PUSHDATA -- 20 BYTES:]"));
   opstr.push_back(string("486efdd300987a054510b4ce1148d4ad290d911e"));
   opstr.push_back(string("OP_EQUAL"));
   opstr.push_back(string("OP_BOOLAND"));
   opstr.push_back(string("OP_FROMALTSTACK"));
   opstr.push_back(string("OP_ADD"));
   opstr.push_back(string("OP_TOALTSTACK"));
   opstr.push_back(string("OP_FROMALTSTACK"));
   opstr.push_back(string("OP_FROMALTSTACK"));
   opstr.push_back(string("OP_GREATERTHANOREQUAL"));

   vector<string> output = BtcUtils::convertScriptToOpStrings(complexScript);
   ASSERT_EQ(output.size(), opstr.size());
   for(uint32_t i=0; i<opstr.size(); i++)
      EXPECT_EQ(output[i], opstr[i]);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class BlockObjTest : public ::testing::Test
{
protected:
   virtual void SetUp(void) 
   {
      rawHead_ = READHEX(
         "01000000"
         "1d8f4ec0443e1f19f305e488c1085c95de7cc3fd25e0d2c5bb5d000000000000"
         "9762547903d36881a86751f3f5049e23050113f779735ef82734ebf0b4450081"
         "d8c8c84d"
         "b3936a1a"
         "334b035b");
      headHashLE_ = READHEX(
         "1195e67a7a6d0674bbd28ae096d602e1f038c8254b49dfe79d47000000000000");
      headHashBE_ = READHEX(
         "000000000000479de7df494b25c838f0e102d696e08ad2bb74066d7a7ae69511");

      rawTx0_ = READHEX( 
         "01000000016290dce984203b6a5032e543e9e272d8bce934c7de4d15fa0fe44d"
         "d49ae4ece9010000008b48304502204f2fa458d439f957308bca264689aa175e"
         "3b7c5f78a901cb450ebd20936b2c500221008ea3883a5b80128e55c9c6070aa6"
         "264e1e0ce3d18b7cd7e85108ce3d18b7419a0141044202550a5a6d3bb81549c4"
         "a7803b1ad59cdbba4770439a4923624a8acfc7d34900beb54a24188f7f0a4068"
         "9d905d4847cc7d6c8d808a457d833c2d44ef83f76bffffffff0242582c0a0000"
         "00001976a914c1b4695d53b6ee57a28647ce63e45665df6762c288ac80d1f008"
         "000000001976a9140e0aec36fe2545fb31a41164fb6954adcd96b34288ac0000"
         "0000");

      rawTx1_ = READHEX( 
         "0100000001f658dbc28e703d86ee17c9a2d3b167a8508b082fa0745f55be5144"
         "a4369873aa010000008c49304602210041e1186ca9a41fdfe1569d5d807ca7ff"
         "6c5ffd19d2ad1be42f7f2a20cdc8f1cc0221003366b5d64fe81e53910e156914"
         "091d12646bc0d1d662b7a65ead3ebe4ab8f6c40141048d103d81ac9691cf13f3"
         "fc94e44968ef67b27f58b27372c13108552d24a6ee04785838f34624b294afee"
         "83749b64478bb8480c20b242c376e77eea2b3dc48b4bffffffff0200e1f50500"
         "0000001976a9141b00a2f6899335366f04b277e19d777559c35bc888ac40aeeb"
         "02000000001976a9140e0aec36fe2545fb31a41164fb6954adcd96b34288ac00"
         "000000");

      rawBlock_ = READHEX(
         // Header (80 bytes in 6 fields)
         "01000000"
         "eb10c9a996a2340a4d74eaab41421ed8664aa49d18538bab5901000000000000"
         "5a2f06efa9f2bd804f17877537f2080030cadbfa1eb50e02338117cc604d91b9"
         "b7541a4e"
         "cfbb0a1a"
         "64f1ade7"
         // NumTx (3)
         "03"
         // Tx0 (Coinbase)
         "0100000001000000000000000000000000000000000000000000000000000000"
         "0000000000ffffffff0804cfbb0a1a02360affffffff0100f2052a0100000043"
         "4104c2239c4eedb3beb26785753463be3ec62b82f6acd62efb65f452f8806f2e"
         "de0b338e31d1f69b1ce449558d7061aa1648ddc2bf680834d3986624006a272d"
         "c21cac00000000"
         // Tx1 (Regular)
         "0100000003e8caa12bcb2e7e86499c9de49c45c5a1c6167ea4"
         "b894c8c83aebba1b6100f343010000008c493046022100e2f5af5329d1244807"
         "f8347a2c8d9acc55a21a5db769e9274e7e7ba0bb605b26022100c34ca3350df5"
         "089f3415d8af82364d7f567a6a297fcc2c1d2034865633238b8c014104129e42"
         "2ac490ddfcb7b1c405ab9fb42441246c4bca578de4f27b230de08408c64cad03"
         "af71ee8a3140b40408a7058a1984a9f246492386113764c1ac132990d1ffffff"
         "ff5b55c18864e16c08ef9989d31c7a343e34c27c30cd7caa759651b0e08cae01"
         "06000000008c4930460221009ec9aa3e0caf7caa321723dea561e232603e0068"
         "6d4bfadf46c5c7352b07eb00022100a4f18d937d1e2354b2e69e02b18d11620a"
         "6a9332d563e9e2bbcb01cee559680a014104411b35dd963028300e36e82ee8cf"
         "1b0c8d5bf1fc4273e970469f5cb931ee07759a2de5fef638961726d04bd5eb4e"
         "5072330b9b371e479733c942964bb86e2b22ffffffff3de0c1e913e6271769d8"
         "c0172cea2f00d6d3240afc3a20f9fa247ce58af30d2a010000008c4930460221"
         "00b610e169fd15ac9f60fe2b507529281cf2267673f4690ba428cbb2ba3c3811"
         "fd022100ffbe9e3d71b21977a8e97fde4c3ba47b896d08bc09ecb9d086bb5917"
         "5b5b9f03014104ff07a1833fd8098b25f48c66dcf8fde34cbdbcc0f5f21a8c20"
         "05b160406cbf34cc432842c6b37b2590d16b165b36a3efc9908d65fb0e605314"
         "c9b278f40f3e1affffffff0240420f00000000001976a914adfa66f57ded1b65"
         "5eb4ccd96ee07ca62bc1ddfd88ac007d6a7d040000001976a914981a0c9ae61f"
         "a8f8c96ae6f8e383d6e07e77133e88ac00000000"
         // Tx2 (Regular)
         "010000000138e7586e078428"
         "0df58bd3dc5e3d350c9036b1ec4107951378f45881799c92a4000000008a4730"
         "4402207c945ae0bbdaf9dadba07bdf23faa676485a53817af975ddf85a104f76"
         "4fb93b02201ac6af32ddf597e610b4002e41f2de46664587a379a0161323a853"
         "89b4f82dda014104ec8883d3e4f7a39d75c9f5bb9fd581dc9fb1b7cdf7d6b5a6"
         "65e4db1fdb09281a74ab138a2dba25248b5be38bf80249601ae688c90c6e0ac8"
         "811cdb740fcec31dffffffff022f66ac61050000001976a914964642290c194e"
         "3bfab661c1085e47d67786d2d388ac2f77e200000000001976a9141486a7046a"
         "ffd935919a3cb4b50a8a0c233c286c"
         "88ac00000000");

      rawTxIn_ = READHEX(
         // OutPoint
         "0044fbc929d78e4203eed6f1d3d39c0157d8e5c100bbe0886779c0ebf6a69324"
         "01000000"
         // Script Size
         "8a"
         // SigScript
         "47304402206568144ed5e7064d6176c74738b04c08ca19ca54ddeb480084b77f"
         "45eebfe57802207927d6975a5ac0e1bb36f5c05356dcda1f521770511ee5e032"
         "39c8e1eecf3aed0141045d74feae58c4c36d7c35beac05eddddc78b3ce4b0249"
         "1a2eea72043978056a8bc439b99ddaad327207b09ef16a8910828e805b0cc8c1"
         "1fba5caea2ee939346d7"
         // Sequence
         "ffffffff");

      rawTxOut_ = READHEX(
         // Value
         "ac4c8bd500000000"
         // Script size (var_int)
         "19"
         // Script
         "76""a9""14""8dce8946f1c7763bb60ea5cf16ef514cbed0633b""88""ac");
         bh_.unserialize(rawHead_);
         tx1_.unserialize(rawTx0_);
         tx2_.unserialize(rawTx1_);
   }

   BinaryData rawHead_;
   BinaryData headHashLE_;
   BinaryData headHashBE_;

   BinaryData rawBlock_;

   BinaryData rawTx0_;
   BinaryData rawTx1_;
   BinaryData rawTxIn_;
   BinaryData rawTxOut_;

   ::BlockHeader bh_;
   Tx tx1_;
   Tx tx2_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockObjTest, HeaderNoInit)
{
   BlockHeader bh;
   EXPECT_FALSE(bh.isInitialized());
   EXPECT_EQ(bh.getNumTx(), UINT32_MAX);
   EXPECT_EQ(bh.getBlockSize(), UINT32_MAX);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockObjTest, HeaderUnserialize)
{
   bool boolFalse = false;
   EXPECT_NE(bh_.isInitialized(), boolFalse);
   EXPECT_EQ(bh_.getNumTx(), UINT32_MAX);
   EXPECT_EQ(bh_.getBlockSize(), UINT32_MAX);
   EXPECT_EQ(bh_.getVersion(), 1ULL);
   EXPECT_EQ(bh_.getThisHash(), headHashLE_);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockObjTest, HeaderProperties)
{
   BinaryData prevHash = READHEX(
      "1d8f4ec0443e1f19f305e488c1085c95de7cc3fd25e0d2c5bb5d000000000000");
   BinaryData merkleRoot = READHEX(
      "9762547903d36881a86751f3f5049e23050113f779735ef82734ebf0b4450081");

   // The values are actually little-endian in the serialization, but 
   // 0x____ notation requires big-endian
   uint32_t   timestamp =        0x4dc8c8d8;
   uint32_t   nonce     =        0x5b034b33;
   BinaryData diffBits  = READHEX("b3936a1a");

   EXPECT_EQ(bh_.getPrevHash(), prevHash);
   EXPECT_EQ(bh_.getTimestamp(), timestamp);
   EXPECT_EQ(bh_.getDiffBits(), diffBits);
   EXPECT_EQ(bh_.getNonce(), nonce);
   EXPECT_DOUBLE_EQ(bh_.getDifficulty(), 157416.40184364893);

   BinaryDataRef bdrThis(headHashLE_);
   BinaryDataRef bdrPrev(rawHead_.getPtr()+4, 32);
   EXPECT_EQ(bh_.getThisHashRef(), bdrThis);
   EXPECT_EQ(bh_.getPrevHashRef(), bdrPrev);

   EXPECT_EQ(BlockHeader(rawHead_).serialize(), rawHead_);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockObjTest, OutPointProperties)
{
   BinaryData rawOP = READHEX(
      "0044fbc929d78e4203eed6f1d3d39c0157d8e5c100bbe0886779c0ebf6a69324"
      "01000000");
   BinaryData prevHash = READHEX(
      "0044fbc929d78e4203eed6f1d3d39c0157d8e5c100bbe0886779c0ebf6a69324");
   BinaryData prevIdx = READHEX(
      "01000000");

   OutPoint op;
   EXPECT_EQ(op.getTxHash().getSize(), 32ULL);
   EXPECT_EQ(op.getTxOutIndex(), UINT32_MAX);

   op.setTxHash(prevHash);
   EXPECT_EQ(op.getTxHash().getSize(), 32ULL);
   EXPECT_EQ(op.getTxOutIndex(), UINT32_MAX);
   EXPECT_EQ(op.getTxHash(), prevHash);
   EXPECT_EQ(op.getTxHashRef(), prevHash.getRef());

   op.setTxOutIndex(12);
   EXPECT_EQ(op.getTxHash().getSize(), 32ULL);
   EXPECT_EQ(op.getTxOutIndex(), 12ULL);
   EXPECT_EQ(op.getTxHash(), prevHash);
   EXPECT_EQ(op.getTxHashRef(), prevHash.getRef());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockObjTest, OutPointSerialize)
{
   BinaryData rawOP = READHEX(
      "0044fbc929d78e4203eed6f1d3d39c0157d8e5c100bbe0886779c0ebf6a69324"
      "01000000");
   BinaryData prevHash = READHEX(
      "0044fbc929d78e4203eed6f1d3d39c0157d8e5c100bbe0886779c0ebf6a69324");
   BinaryData prevIdx = READHEX(
      "01000000");

   OutPoint op(rawOP.getPtr(), rawOP.getSize());
   EXPECT_EQ(op.getTxHash().getSize(), 32ULL);
   EXPECT_EQ(op.getTxOutIndex(), 1ULL);
   EXPECT_EQ(op.getTxHash(), prevHash);
   EXPECT_EQ(op.getTxHashRef(), prevHash.getRef());

   EXPECT_EQ(op.serialize(), rawOP);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockObjTest, TxUnserialize)
{
   uint32_t len = rawTx0_.getSize();
   BinaryData tx0hash = READHEX(
      "aa739836a44451be555f74a02f088b50a867b1d3a2c917ee863d708ec2db58f6");

   BinaryData tx0_In0  = READHEX("aff189b24a36a1b93de2ea4d157c13d18251270a");
   BinaryData tx0_Out0 = READHEX("c1b4695d53b6ee57a28647ce63e45665df6762c2");
   BinaryData tx0_Out1 = READHEX("0e0aec36fe2545fb31a41164fb6954adcd96b342");
   BinaryData tx0_Val0 = READHEX("42582c0a00000000");
   BinaryData tx0_Val1 = READHEX("80d1f00800000000");
   BinaryRefReader brr(rawTx0_);

   uint64_t v0 = *(uint64_t*)tx0_Val0.getPtr();
   uint64_t v1 = *(uint64_t*)tx0_Val1.getPtr();

   Tx tx;
   vector<Tx> txs(10);
   txs[0] = Tx(rawTx0_.getPtr(), len); 
   txs[1] = Tx(brr);  brr.resetPosition();
   txs[2] = Tx(rawTx0_);
   txs[3] = Tx(rawTx0_.getRef());
   txs[4].unserialize(rawTx0_.getPtr(), len);
   txs[5].unserialize(rawTx0_);
   txs[6].unserialize(rawTx0_.getRef());
   txs[7].unserialize(brr);  brr.resetPosition();
   txs[8].unserialize_swigsafe_(rawTx0_);
   txs[9] = Tx::createFromStr(rawTx0_);

   for(uint32_t i=0; i<10; i++)
   {
      EXPECT_TRUE( txs[i].isInitialized());
      EXPECT_EQ(   txs[i].getSize(), len);

      EXPECT_EQ(   txs[i].getVersion(), 1ULL);
      EXPECT_EQ(   txs[i].getNumTxIn(), 1ULL);
      EXPECT_EQ(   txs[i].getNumTxOut(), 2ULL);
      EXPECT_EQ(   txs[i].getThisHash(), tx0hash.copySwapEndian());

      EXPECT_EQ(   txs[i].getTxInOffset(0),    5ULL);
      EXPECT_EQ(   txs[i].getTxInOffset(1),  185ULL);
      EXPECT_EQ(   txs[i].getTxOutOffset(0), 186ULL);
      EXPECT_EQ(   txs[i].getTxOutOffset(1), 220ULL);
      EXPECT_EQ(   txs[i].getTxOutOffset(2), 254ULL);

      EXPECT_EQ(   txs[i].getLockTime(), 0ULL);

      EXPECT_EQ(   txs[i].serialize(), rawTx0_);
      EXPECT_EQ(   txs[0].getTxInCopy(0).getSenderScrAddrIfAvail(), tx0_In0);
      EXPECT_EQ(   txs[i].getTxOutCopy(0).getScrAddressStr(), HASH160PREFIX+tx0_Out0);
      EXPECT_EQ(   txs[i].getTxOutCopy(1).getScrAddressStr(), HASH160PREFIX+tx0_Out1);
      EXPECT_EQ(   txs[i].getScrAddrForTxOut(0), HASH160PREFIX+tx0_Out0);
      EXPECT_EQ(   txs[i].getScrAddrForTxOut(1), HASH160PREFIX+tx0_Out1);
      EXPECT_EQ(   txs[i].getTxOutCopy(0).getValue(), v0);
      EXPECT_EQ(   txs[i].getTxOutCopy(1).getValue(), v1);
      EXPECT_EQ(   txs[i].getSumOfOutputs(),  v0+v1);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockObjTest, DISABLED_FullBlock)
{
   EXPECT_TRUE(false);

   BinaryRefReader brr(rawBlock_);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockObjTest, DISABLED_TxIOPairStuff)
{
   EXPECT_TRUE(false);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(BlockObjTest, DISABLED_RegisteredTxStuff)
{
   EXPECT_TRUE(false);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class StoredBlockObjTest : public ::testing::Test
{
protected:
   virtual void SetUp(void) 
   {
      rawHead_ = READHEX(
         "01000000"
         "1d8f4ec0443e1f19f305e488c1085c95de7cc3fd25e0d2c5bb5d000000000000"
         "9762547903d36881a86751f3f5049e23050113f779735ef82734ebf0b4450081"
         "d8c8c84d"
         "b3936a1a"
         "334b035b");
      headHashLE_ = READHEX(
         "1195e67a7a6d0674bbd28ae096d602e1f038c8254b49dfe79d47000000000000");
      headHashBE_ = READHEX(
         "000000000000479de7df494b25c838f0e102d696e08ad2bb74066d7a7ae69511");

      rawTx0_ = READHEX( 
         "01000000016290dce984203b6a5032e543e9e272d8bce934c7de4d15fa0fe44d"
         "d49ae4ece9010000008b48304502204f2fa458d439f957308bca264689aa175e"
         "3b7c5f78a901cb450ebd20936b2c500221008ea3883a5b80128e55c9c6070aa6"
         "264e1e0ce3d18b7cd7e85108ce3d18b7419a0141044202550a5a6d3bb81549c4"
         "a7803b1ad59cdbba4770439a4923624a8acfc7d34900beb54a24188f7f0a4068"
         "9d905d4847cc7d6c8d808a457d833c2d44ef83f76bffffffff0242582c0a0000"
         "00001976a914c1b4695d53b6ee57a28647ce63e45665df6762c288ac80d1f008"
         "000000001976a9140e0aec36fe2545fb31a41164fb6954adcd96b34288ac0000"
         "0000");
      rawTx1_ = READHEX( 
         "0100000001f658dbc28e703d86ee17c9a2d3b167a8508b082fa0745f55be5144"
         "a4369873aa010000008c49304602210041e1186ca9a41fdfe1569d5d807ca7ff"
         "6c5ffd19d2ad1be42f7f2a20cdc8f1cc0221003366b5d64fe81e53910e156914"
         "091d12646bc0d1d662b7a65ead3ebe4ab8f6c40141048d103d81ac9691cf13f3"
         "fc94e44968ef67b27f58b27372c13108552d24a6ee04785838f34624b294afee"
         "83749b64478bb8480c20b242c376e77eea2b3dc48b4bffffffff0200e1f50500"
         "0000001976a9141b00a2f6899335366f04b277e19d777559c35bc888ac40aeeb"
         "02000000001976a9140e0aec36fe2545fb31a41164fb6954adcd96b34288ac00"
         "000000");

      rawBlock_ = READHEX(
         "01000000eb10c9a996a2340a4d74eaab41421ed8664aa49d18538bab59010000"
         "000000005a2f06efa9f2bd804f17877537f2080030cadbfa1eb50e02338117cc"
         "604d91b9b7541a4ecfbb0a1a64f1ade703010000000100000000000000000000"
         "00000000000000000000000000000000000000000000ffffffff0804cfbb0a1a"
         "02360affffffff0100f2052a01000000434104c2239c4eedb3beb26785753463"
         "be3ec62b82f6acd62efb65f452f8806f2ede0b338e31d1f69b1ce449558d7061"
         "aa1648ddc2bf680834d3986624006a272dc21cac000000000100000003e8caa1"
         "2bcb2e7e86499c9de49c45c5a1c6167ea4b894c8c83aebba1b6100f343010000"
         "008c493046022100e2f5af5329d1244807f8347a2c8d9acc55a21a5db769e927"
         "4e7e7ba0bb605b26022100c34ca3350df5089f3415d8af82364d7f567a6a297f"
         "cc2c1d2034865633238b8c014104129e422ac490ddfcb7b1c405ab9fb4244124"
         "6c4bca578de4f27b230de08408c64cad03af71ee8a3140b40408a7058a1984a9"
         "f246492386113764c1ac132990d1ffffffff5b55c18864e16c08ef9989d31c7a"
         "343e34c27c30cd7caa759651b0e08cae0106000000008c4930460221009ec9aa"
         "3e0caf7caa321723dea561e232603e00686d4bfadf46c5c7352b07eb00022100"
         "a4f18d937d1e2354b2e69e02b18d11620a6a9332d563e9e2bbcb01cee559680a"
         "014104411b35dd963028300e36e82ee8cf1b0c8d5bf1fc4273e970469f5cb931"
         "ee07759a2de5fef638961726d04bd5eb4e5072330b9b371e479733c942964bb8"
         "6e2b22ffffffff3de0c1e913e6271769d8c0172cea2f00d6d3240afc3a20f9fa"
         "247ce58af30d2a010000008c493046022100b610e169fd15ac9f60fe2b507529"
         "281cf2267673f4690ba428cbb2ba3c3811fd022100ffbe9e3d71b21977a8e97f"
         "de4c3ba47b896d08bc09ecb9d086bb59175b5b9f03014104ff07a1833fd8098b"
         "25f48c66dcf8fde34cbdbcc0f5f21a8c2005b160406cbf34cc432842c6b37b25"
         "90d16b165b36a3efc9908d65fb0e605314c9b278f40f3e1affffffff0240420f"
         "00000000001976a914adfa66f57ded1b655eb4ccd96ee07ca62bc1ddfd88ac00"
         "7d6a7d040000001976a914981a0c9ae61fa8f8c96ae6f8e383d6e07e77133e88"
         "ac00000000010000000138e7586e0784280df58bd3dc5e3d350c9036b1ec4107"
         "951378f45881799c92a4000000008a47304402207c945ae0bbdaf9dadba07bdf"
         "23faa676485a53817af975ddf85a104f764fb93b02201ac6af32ddf597e610b4"
         "002e41f2de46664587a379a0161323a85389b4f82dda014104ec8883d3e4f7a3"
         "9d75c9f5bb9fd581dc9fb1b7cdf7d6b5a665e4db1fdb09281a74ab138a2dba25"
         "248b5be38bf80249601ae688c90c6e0ac8811cdb740fcec31dffffffff022f66"
         "ac61050000001976a914964642290c194e3bfab661c1085e47d67786d2d388ac"
         "2f77e200000000001976a9141486a7046affd935919a3cb4b50a8a0c233c286c"
         "88ac00000000");

      rawTxUnfrag_ = READHEX(
         // Version
         "01000000"
         // NumTxIn
         "02"
         // Start TxIn0
         "0044fbc929d78e4203eed6f1d3d39c0157d8e5c100bbe0886779c0"
         "ebf6a69324010000008a47304402206568144ed5e7064d6176c74738b04c08ca"
         "19ca54ddeb480084b77f45eebfe57802207927d6975a5ac0e1bb36f5c05356dc"
         "da1f521770511ee5e03239c8e1eecf3aed0141045d74feae58c4c36d7c35beac"
         "05eddddc78b3ce4b02491a2eea72043978056a8bc439b99ddaad327207b09ef1"
         "6a8910828e805b0cc8c11fba5caea2ee939346d7ffffffff"
         // Start TxIn1
         "45c866b219b17695"
         "2508f8e5aea728f950186554fc4a5807e2186a8e1c4009e5000000008c493046"
         "022100bd5d41662f98cfddc46e86ea7e4a3bc8fe9f1dfc5c4836eaf7df582596"
         "cfe0e9022100fc459ae4f59b8279d679003b88935896acd10021b6e2e4619377"
         "e336b5296c5e014104c00bab76a708ba7064b2315420a1c533ca9945eeff9754"
         "cdc574224589e9113469b4e71752146a10028079e04948ecdf70609bf1b9801f"
         "6b73ab75947ac339e5ffffffff"
         // NumTxOut
         "02"
         // Start TxOut0
         "ac4c8bd5000000001976a9148dce8946f1c7763bb60ea5cf16ef514cbed0633b88ac"
         // Start TxOut1
         "002f6859000000001976a9146a59ac0e8f553f292dfe5e9f3aaa1da93499c15e88ac"
         // Locktime
         "00000000");

      rawTxFragged_ = READHEX(
         //"01000000020044fbc929d78e4203eed6f1d3d39c0157d8e5c100bbe0886779c0"
         //"ebf6a69324010000008a47304402206568144ed5e7064d6176c74738b04c08ca"
         //"19ca54ddeb480084b77f45eebfe57802207927d6975a5ac0e1bb36f5c05356dc"
         //"da1f521770511ee5e03239c8e1eecf3aed0141045d74feae58c4c36d7c35beac"
         //"05eddddc78b3ce4b02491a2eea72043978056a8bc439b99ddaad327207b09ef1"
         //"6a8910828e805b0cc8c11fba5caea2ee939346d7ffffffff45c866b219b17695"
         //"2508f8e5aea728f950186554fc4a5807e2186a8e1c4009e5000000008c493046"
         //"022100bd5d41662f98cfddc46e86ea7e4a3bc8fe9f1dfc5c4836eaf7df582596"
         //"cfe0e9022100fc459ae4f59b8279d679003b88935896acd10021b6e2e4619377"
         //"e336b5296c5e014104c00bab76a708ba7064b2315420a1c533ca9945eeff9754"
         //"cdc574224589e9113469b4e71752146a10028079e04948ecdf70609bf1b9801f"
         //"6b73ab75947ac339e5ffffffff0200000000");
         // Version
         "01000000"
         // NumTxIn
         "02"
         // Start TxIn0
         "0044fbc929d78e4203eed6f1d3d39c0157d8e5c100bbe0886779c0"
         "ebf6a69324010000008a47304402206568144ed5e7064d6176c74738b04c08ca"
         "19ca54ddeb480084b77f45eebfe57802207927d6975a5ac0e1bb36f5c05356dc"
         "da1f521770511ee5e03239c8e1eecf3aed0141045d74feae58c4c36d7c35beac"
         "05eddddc78b3ce4b02491a2eea72043978056a8bc439b99ddaad327207b09ef1"
         "6a8910828e805b0cc8c11fba5caea2ee939346d7ffffffff"
         // Start TxIn1
         "45c866b219b17695"
         "2508f8e5aea728f950186554fc4a5807e2186a8e1c4009e5000000008c493046"
         "022100bd5d41662f98cfddc46e86ea7e4a3bc8fe9f1dfc5c4836eaf7df582596"
         "cfe0e9022100fc459ae4f59b8279d679003b88935896acd10021b6e2e4619377"
         "e336b5296c5e014104c00bab76a708ba7064b2315420a1c533ca9945eeff9754"
         "cdc574224589e9113469b4e71752146a10028079e04948ecdf70609bf1b9801f"
         "6b73ab75947ac339e5ffffffff"
         // NumTxOut
         "02"
         // ... TxOuts fragged out 
         // Locktime
         "00000000");

      rawTxOut0_ = READHEX(
         // Value
         "ac4c8bd500000000"
         // Script size (var_int)
         "19"
         // Script
         "76""a9""14""8dce8946f1c7763bb60ea5cf16ef514cbed0633b""88""ac");
      rawTxOut1_ = READHEX(
         // Value 
         "002f685900000000"
         // Script size (var_int)
         "19"
         // Script
         "76""a9""14""6a59ac0e8f553f292dfe5e9f3aaa1da93499c15e""88""ac");

      bh_.unserialize(rawHead_);
      tx1_.unserialize(rawTx0_);
      tx2_.unserialize(rawTx1_);


      sbh_.setHeaderData(rawHead_);
   }

   BinaryData PREFBYTE(DB_PREFIX pref) 
   { 
      BinaryWriter bw;
      bw.put_uint8_t((uint8_t)pref);
      return bw.getData();
   }

   BinaryData rawHead_;
   BinaryData headHashLE_;
   BinaryData headHashBE_;

   BinaryData rawBlock_;

   BinaryData rawTx0_;
   BinaryData rawTx1_;

   ::BlockHeader bh_;
   Tx tx1_;
   Tx tx2_;

   BinaryData rawTxUnfrag_;
   BinaryData rawTxFragged_;
   BinaryData rawTxOut0_;
   BinaryData rawTxOut1_;

   StoredHeader sbh_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, StoredObjNoInit)
{
   StoredHeader        sbh;
   StoredTx            stx;
   StoredTxOut         stxo;
   StoredScriptHistory ssh;
   StoredUndoData      sud;
   StoredHeadHgtList   hhl;
   StoredTxHints       sths;

   EXPECT_FALSE( sbh.isInitialized() );
   EXPECT_FALSE( stx.isInitialized() );
   EXPECT_FALSE( stxo.isInitialized() );
   EXPECT_FALSE( ssh.isInitialized() );
   EXPECT_FALSE( sud.isInitialized() );
   EXPECT_FALSE( hhl.isInitialized() );
   EXPECT_FALSE( sths.isInitialized() );
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, GetDBKeys)
{
   StoredHeader        sbh;
   StoredTx            stx;
   StoredTxOut         stxo;
   StoredScriptHistory ssh1;
   StoredScriptHistory ssh2;
   StoredUndoData      sud;
   StoredHeadHgtList   hhl;
   StoredTxHints       sths;

   BinaryData key    = READHEX("aaaaffff");
   uint32_t   hgt    = 123000;
   uint8_t    dup    = 15;
   uint8_t    txi    = 7;
   uint8_t    txo    = 1;
   BinaryData hgtx   = READHEX("01e0780f");
   BinaryData txidx  = WRITE_UINT16_BE(txi);
   BinaryData txoidx = WRITE_UINT16_BE(txo);

   sbh.blockHeight_  = hgt;
   sbh.duplicateID_  = dup;

   stx.blockHeight_  = hgt;
   stx.duplicateID_  = dup;
   stx.txIndex_      = txi;

   stxo.blockHeight_ = hgt;
   stxo.duplicateID_ = dup;
   stxo.txIndex_     = txi;
   stxo.txOutIndex_  = txo;

   ssh1.uniqueKey_   = key;
   ssh2.uniqueKey_   = key;
   sud.blockHeight_  = hgt;
   sud.duplicateID_  = dup;
   hhl.height_       = hgt;
   sths.txHashPrefix_= key;

   BinaryData TXB = PREFBYTE(DB_PREFIX_TXDATA);
   BinaryData SSB = PREFBYTE(DB_PREFIX_SCRIPT);
   BinaryData UDB = PREFBYTE(DB_PREFIX_UNDODATA);
   BinaryData HHB = PREFBYTE(DB_PREFIX_HEADHGT);
   BinaryData THB = PREFBYTE(DB_PREFIX_TXHINTS);
   EXPECT_EQ(sbh.getDBKey(  true ),   TXB + hgtx);
   EXPECT_EQ(stx.getDBKey(  true ),   TXB + hgtx + txidx);
   EXPECT_EQ(stxo.getDBKey( true ),   TXB + hgtx + txidx + txoidx);
   EXPECT_EQ(ssh1.getDBKey( true ),   SSB + key);
   EXPECT_EQ(ssh2.getDBKey( true ),   SSB + key);
   EXPECT_EQ(sud.getDBKey(  true ),   UDB + hgtx);
   EXPECT_EQ(hhl.getDBKey(  true ),   HHB + WRITE_UINT32_BE(hgt));
   EXPECT_EQ(sths.getDBKey( true ),   THB + key);

   EXPECT_EQ(sbh.getDBKey(  false ),         hgtx);
   EXPECT_EQ(stx.getDBKey(  false ),         hgtx + txidx);
   EXPECT_EQ(stxo.getDBKey( false ),         hgtx + txidx + txoidx);
   EXPECT_EQ(ssh1.getDBKey( false ),         key);
   EXPECT_EQ(ssh2.getDBKey( false ),         key);
   EXPECT_EQ(sud.getDBKey(  false ),         hgtx);
   EXPECT_EQ(hhl.getDBKey(  false ),         WRITE_UINT32_BE(hgt));
   EXPECT_EQ(sths.getDBKey( false ),         key);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, LengthUnfrag)
{
   StoredTx tx;
   vector<size_t> offin, offout;

   uint32_t lenUnfrag  = BtcUtils::StoredTxCalcLength( rawTxUnfrag_.getPtr(), 
      rawTxUnfrag_.getSize(), false,  &offin, &offout, nullptr);

   ASSERT_EQ(lenUnfrag,  438ULL);

   ASSERT_EQ(offin.size(),    3ULL);
   EXPECT_EQ(offin[0],        5ULL);
   EXPECT_EQ(offin[1],      184ULL);
   EXPECT_EQ(offin[2],      365ULL);

   ASSERT_EQ(offout.size(),   3ULL);
   EXPECT_EQ(offout[0],     366ULL);
   EXPECT_EQ(offout[1],     400ULL);
   EXPECT_EQ(offout[2],     434ULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, LengthFragged)
{
   vector<size_t> offin, offout;

   uint32_t lenFragged = BtcUtils::StoredTxCalcLength(rawTxFragged_.getPtr(),
      rawTxFragged_.getSize(), true, &offin, &offout, nullptr);

   ASSERT_EQ(lenFragged, 370ULL);

   ASSERT_EQ(offin.size(),    3ULL);
   EXPECT_EQ(offin[0],        5ULL);
   EXPECT_EQ(offin[1],      184ULL);
   EXPECT_EQ(offin[2],      365ULL);
   
   ASSERT_EQ(offout.size(),   3ULL);
   EXPECT_EQ(offout[0],     366ULL);
   EXPECT_EQ(offout[1],     366ULL);
   EXPECT_EQ(offout[2],     366ULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, BlkDataKeys)
{
   const uint32_t hgt = 0x001a332b;
   const uint8_t  dup = 0x01;
   const uint16_t tix = 0x0102;
   const uint16_t tox = 0x0021;
   
   EXPECT_EQ(DBUtils::getBlkDataKey(hgt, dup),           
                                               READHEX("031a332b01"));
   EXPECT_EQ(DBUtils::getBlkDataKey(hgt, dup, tix),      
                                               READHEX("031a332b010102"));
   EXPECT_EQ(DBUtils::getBlkDataKey(hgt, dup, tix, tox), 
                                               READHEX("031a332b0101020021"));

   EXPECT_EQ(DBUtils::getBlkDataKeyNoPrefix(hgt, dup),           
                                               READHEX("1a332b01"));
   EXPECT_EQ(DBUtils::getBlkDataKeyNoPrefix(hgt, dup, tix),      
                                               READHEX("1a332b010102"));
   EXPECT_EQ(DBUtils::getBlkDataKeyNoPrefix(hgt, dup, tix, tox), 
                                               READHEX("1a332b0101020021"));
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, ReadBlkKeyData)
{
   BinaryData TXP  = WRITE_UINT8_BE((uint8_t)DB_PREFIX_TXDATA);
   BinaryData key5p = TXP + READHEX("01e078""0f");
   BinaryData key7p = TXP + READHEX("01e078""0f""0007");
   BinaryData key9p = TXP + READHEX("01e078""0f""0007""0001");
   BinaryData key5 =        READHEX("01e078""0f");
   BinaryData key7 =        READHEX("01e078""0f""0007");
   BinaryData key9 =        READHEX("01e078""0f""0007""0001");
   BinaryRefReader brr;

   uint32_t hgt;
   uint8_t  dup;
   uint16_t txi;
   uint16_t txo;

   BLKDATA_TYPE bdtype;

   /////////////////////////////////////////////////////////////////////////////
   // 5 bytes, with prefix
   brr.setNewData(key5p);
   bdtype = DBUtils::readBlkDataKey(brr, hgt, dup);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_HEADER);

   brr.setNewData(key5p);
   bdtype = DBUtils::readBlkDataKey(brr, hgt, dup, txi);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( txi, UINT16_MAX);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_HEADER);
   
   brr.setNewData(key5p);
   bdtype = DBUtils::readBlkDataKey(brr, hgt, dup, txi, txo);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( txi, UINT16_MAX);
   EXPECT_EQ( txo, UINT16_MAX);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_HEADER);


   /////////////////////////////////////////////////////////////////////////////
   // 7 bytes, with prefix
   brr.setNewData(key7p);
   bdtype = DBUtils::readBlkDataKey(brr, hgt, dup);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_TX);

   brr.setNewData(key7p);
   bdtype = DBUtils::readBlkDataKey(brr, hgt, dup, txi);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( txi,          7);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_TX);
   
   brr.setNewData(key7p);
   bdtype = DBUtils::readBlkDataKey(brr, hgt, dup, txi, txo);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( txi,          7);
   EXPECT_EQ( txo, UINT16_MAX);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_TX);


   /////////////////////////////////////////////////////////////////////////////
   // 9 bytes, with prefix
   brr.setNewData(key9p);
   bdtype = DBUtils::readBlkDataKey(brr, hgt, dup);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_TXOUT);

   brr.setNewData(key9p);
   bdtype = DBUtils::readBlkDataKey(brr, hgt, dup, txi);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( txi,          7);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_TXOUT);
   
   brr.setNewData(key9p);
   bdtype = DBUtils::readBlkDataKey(brr, hgt, dup, txi, txo);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( txi,          7);
   EXPECT_EQ( txo,          1);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_TXOUT);


   /////////////////////////////////////////////////////////////////////////////
   // 5 bytes, no prefix
   brr.setNewData(key5);
   bdtype = DBUtils::readBlkDataKeyNoPrefix(brr, hgt, dup);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_HEADER);

   brr.setNewData(key5);
   bdtype = DBUtils::readBlkDataKeyNoPrefix(brr, hgt, dup, txi);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( txi, UINT16_MAX);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_HEADER);
   
   brr.setNewData(key5);
   bdtype = DBUtils::readBlkDataKeyNoPrefix(brr, hgt, dup, txi, txo);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( txi, UINT16_MAX);
   EXPECT_EQ( txo, UINT16_MAX);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_HEADER);


   /////////////////////////////////////////////////////////////////////////////
   // 7 bytes, no prefix
   brr.setNewData(key7);
   bdtype = DBUtils::readBlkDataKeyNoPrefix(brr, hgt, dup);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_TX);

   brr.setNewData(key7);
   bdtype = DBUtils::readBlkDataKeyNoPrefix(brr, hgt, dup, txi);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( txi,          7);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_TX);

   brr.setNewData(key7);
   bdtype = DBUtils::readBlkDataKeyNoPrefix(brr, hgt, dup, txi, txo);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( txi,          7);
   EXPECT_EQ( txo, UINT16_MAX);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_TX);


   /////////////////////////////////////////////////////////////////////////////
   // 9 bytes, no prefix
   brr.setNewData(key9);
   bdtype = DBUtils::readBlkDataKeyNoPrefix(brr, hgt, dup);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_TXOUT);

   brr.setNewData(key9);
   bdtype = DBUtils::readBlkDataKeyNoPrefix(brr, hgt, dup, txi);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( txi,          7);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_TXOUT);

   brr.setNewData(key9);
   bdtype = DBUtils::readBlkDataKeyNoPrefix(brr, hgt, dup, txi, txo);
   EXPECT_EQ( hgt,     123000ULL);
   EXPECT_EQ( dup,         15);
   EXPECT_EQ( txi,          7);
   EXPECT_EQ( txo,          1);
   EXPECT_EQ( brr.getSizeRemaining(), 0ULL);
   EXPECT_EQ( bdtype, BLKDATA_TXOUT);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, SHeaderUnserialize)
{
   // SetUp already contains sbh_.unserialize(rawHead_);
   EXPECT_TRUE( sbh_.isInitialized());
   EXPECT_FALSE(sbh_.isMainBranch_);
   EXPECT_FALSE(sbh_.haveFullBlock());
   EXPECT_FALSE(sbh_.isMerkleCreated());
   EXPECT_EQ(   sbh_.numTx_,       UINT32_MAX);
   EXPECT_EQ(   sbh_.numBytes_,    UINT32_MAX);
   EXPECT_EQ(   sbh_.blockHeight_, UINT32_MAX);
   EXPECT_EQ(   sbh_.duplicateID_, UINT8_MAX);
   EXPECT_EQ(   sbh_.merkle_.getSize(), 0ULL);
   EXPECT_EQ(   sbh_.stxMap_.size(), 0ULL);
}


////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, SHeaderDBSerFull_H)
{
   sbh_.blockHeight_      = 65535;
   sbh_.duplicateID_      = 1;
   sbh_.merkle_           = READHEX("deadbeef");
   sbh_.merkleIsPartial_  = false;
   sbh_.isMainBranch_     = true;
   sbh_.numTx_            = 15;
   sbh_.numBytes_         = 0xdeadbeef;
   sbh_.fileID_ = 25;
   sbh_.offset_ = 0xffffeeee;

   // SetUp already contains sbh_.unserialize(rawHead_);
   BinaryData last4 = READHEX("00ffff01efbeadde" "0f000000" "1900eeeeffff00000000" "ffffffff");
   EXPECT_EQ(serializeDBValue(sbh_, HEADERS, ARMORY_DB_FULL), rawHead_ + last4);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, SHeaderDBSerFull_B1)
{
   // ARMORY_DB_FULL means no merkle string (cause all Tx are in the DB
   // so the merkle tree would be redundant.
//    DBUtils::setArmoryDbType(ARMORY_DB_FULL);
//    DBUtils::setDbPruneType(DB_PRUNE_NONE);

   sbh_.blockHeight_      = 65535;
   sbh_.duplicateID_      = 1;
   sbh_.merkle_           = READHEX("deadbeef");
   sbh_.merkleIsPartial_  = false;
   sbh_.isMainBranch_     = true;
   sbh_.numTx_            = 15;
   sbh_.numBytes_         = 65535;

   // SetUp already contains sbh_.unserialize(rawHead_);
   BinaryData flags = READHEX("97011100");
   BinaryData ntx   = READHEX("0f000000");
   BinaryData nbyte = READHEX("ffff0000");

   BinaryData headBlkData = flags + rawHead_ + ntx + nbyte;
   EXPECT_EQ(serializeDBValue(sbh_, BLKDATA, ARMORY_DB_FULL), headBlkData);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, SHeaderDBUnserFull_H)
{
   BinaryData dbval = READHEX(
      "010000001d8f4ec0443e1f19f305e488c1085c95de7cc3fd25e0d2c5bb5d0000"
      "000000009762547903d36881a86751f3f5049e23050113f779735ef82734ebf0"
      "b4450081d8c8c84db3936a1a334b035b00ffff01ee110000"
      "0000000000000000000000000000000000000000000000000000");

   BinaryRefReader brr(dbval);
   sbh_.unserializeDBValue(HEADERS, brr);

   EXPECT_EQ(sbh_.blockHeight_, 65535ULL);
   EXPECT_EQ(sbh_.numBytes_, 0x11eeULL);
   EXPECT_EQ(sbh_.duplicateID_, 1);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, SHeaderDBUnserFull_B1)
{
   BinaryData dbval = READHEX(
      "97011100010000001d8f4ec0443e1f19f305e488c1085c95de7cc3fd25e0d2c5"
      "bb5d0000000000009762547903d36881a86751f3f5049e23050113f779735ef8"
      "2734ebf0b4450081d8c8c84db3936a1a334b035b0f000000ffff0000");

   BinaryRefReader brr(dbval);
   sbh_.unserializeDBValue(BLKDATA, brr);
   sbh_.setHeightAndDup(65535, 1);

   EXPECT_EQ(sbh_.blockHeight_,  65535ULL);
   EXPECT_EQ(sbh_.duplicateID_,  1);
   EXPECT_EQ(sbh_.merkle_     ,  READHEX(""));
   EXPECT_EQ(sbh_.numTx_      ,  15ULL);
   EXPECT_EQ(sbh_.numBytes_   ,  65535ULL);
   EXPECT_EQ(sbh_.unserArmVer_,  0x9701ULL);
   EXPECT_EQ(sbh_.unserBlkVer_,  1ULL);
   EXPECT_EQ(sbh_.unserDbType_,  ARMORY_DB_FULL);
   EXPECT_EQ(sbh_.unserMkType_,  MERKLE_SER_NONE);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, SHeaderDBUnserFull_B2)
{
   BinaryData dbval = READHEX(
      "97011180010000001d8f4ec0443e1f19f305e488c1085c95de7cc3fd25e0d2c5"
      "bb5d0000000000009762547903d36881a86751f3f5049e23050113f779735ef8"
      "2734ebf0b4450081d8c8c84db3936a1a334b035b0f000000ffff0000deadbeef");

   BinaryRefReader brr(dbval);
   sbh_.unserializeDBValue(BLKDATA, brr);
   sbh_.setHeightAndDup(65535, 1);

   EXPECT_EQ(sbh_.blockHeight_ , 65535ULL);
   EXPECT_EQ(sbh_.duplicateID_ , 1);
   EXPECT_EQ(sbh_.merkle_      , READHEX("deadbeef"));
   EXPECT_EQ(sbh_.numTx_       , 15ULL);
   EXPECT_EQ(sbh_.numBytes_    , 65535ULL);
   EXPECT_EQ(sbh_.unserArmVer_,  0x9701ULL);
   EXPECT_EQ(sbh_.unserBlkVer_,  1ULL);
   EXPECT_EQ(sbh_.unserDbType_,  ARMORY_DB_FULL);
   EXPECT_EQ(sbh_.unserMkType_,  MERKLE_SER_FULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, SHeaderDBUnserFull_B3)
{
   BinaryData dbval = READHEX(
      "97011100010000001d8f4ec0443e1f19f305e488c1085c95de7cc3fd25e0d2c5"
      "bb5d0000000000009762547903d36881a86751f3f5049e23050113f779735ef8"
      "2734ebf0b4450081d8c8c84db3936a1a334b035b0f000000ffff0000");

   BinaryRefReader brr(dbval);
   sbh_.unserializeDBValue(BLKDATA, brr);
   sbh_.setHeightAndDup(65535, 1);

   EXPECT_EQ(sbh_.blockHeight_,  65535ULL);
   EXPECT_EQ(sbh_.duplicateID_,  1);
   EXPECT_EQ(sbh_.merkle_     ,  READHEX(""));
   EXPECT_EQ(sbh_.numTx_      ,  15ULL);
   EXPECT_EQ(sbh_.numBytes_   ,  65535ULL);
   EXPECT_EQ(sbh_.unserArmVer_,  0x9701ULL);
   EXPECT_EQ(sbh_.unserBlkVer_,  1ULL);
   EXPECT_EQ(sbh_.unserMkType_,  MERKLE_SER_NONE);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxUnserUnfrag)
{
   Tx regTx(rawTx0_);

   StoredTx stx;
   stx.createFromTx(regTx, false);

   EXPECT_TRUE( stx.isInitialized());
   EXPECT_TRUE( stx.haveAllTxOut());
   EXPECT_FALSE(stx.isFragged_);
   EXPECT_EQ(   stx.version_, 1ULL);
   EXPECT_EQ(   stx.blockHeight_, UINT32_MAX);
   EXPECT_EQ(   stx.duplicateID_,  UINT8_MAX);
   EXPECT_EQ(   stx.txIndex_,     UINT16_MAX);
   EXPECT_EQ(   stx.dataCopy_.getSize(), 258ULL);
   EXPECT_EQ(   stx.numBytes_,    258ULL);
   EXPECT_EQ(   stx.fragBytes_,   190ULL);

   ASSERT_EQ(   stx.stxoMap_.size(), 2ULL);
   EXPECT_TRUE( stx.stxoMap_[0].isInitialized());
   EXPECT_TRUE( stx.stxoMap_[1].isInitialized());
   EXPECT_EQ(   stx.stxoMap_[0].txIndex_, UINT16_MAX);
   EXPECT_EQ(   stx.stxoMap_[1].txIndex_, UINT16_MAX);
   EXPECT_EQ(   stx.stxoMap_[0].txOutIndex_, 0);
   EXPECT_EQ(   stx.stxoMap_[1].txOutIndex_, 1);
}


////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxUnserFragged)
{
   Tx regTx(rawTx0_);

   StoredTx stx;
   stx.createFromTx(regTx, true);

   EXPECT_TRUE( stx.isInitialized());
   EXPECT_TRUE( stx.haveAllTxOut());
   EXPECT_TRUE( stx.isFragged_);
   EXPECT_EQ(   stx.version_, 1ULL);
   EXPECT_EQ(   stx.blockHeight_, UINT32_MAX);
   EXPECT_EQ(   stx.duplicateID_,  UINT8_MAX);
   EXPECT_EQ(   stx.txIndex_,     UINT16_MAX);
   EXPECT_EQ(   stx.dataCopy_.getSize(), 190ULL);

   ASSERT_EQ(   stx.stxoMap_.size(), 2ULL);
   EXPECT_TRUE( stx.stxoMap_[0].isInitialized());
   EXPECT_TRUE( stx.stxoMap_[1].isInitialized());
   EXPECT_EQ(   stx.stxoMap_[0].txIndex_, UINT16_MAX);
   EXPECT_EQ(   stx.stxoMap_[1].txIndex_, UINT16_MAX);
   EXPECT_EQ(   stx.stxoMap_[0].txOutIndex_, 0);
   EXPECT_EQ(   stx.stxoMap_[1].txOutIndex_, 1);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxReconstruct)
{
   Tx regTx, reconTx;
   StoredTx stx;

   // Reconstruct an unfragged tx
   regTx.unserialize(rawTx0_);
   stx.createFromTx(regTx, false);

   reconTx = stx.getTxCopy();
   EXPECT_EQ(reconTx.serialize(),   rawTx0_);
   EXPECT_EQ(stx.getSerializedTx(), rawTx0_);

   // Reconstruct an fragged tx
   regTx.unserialize(rawTx0_);
   stx.createFromTx(regTx, true);

   reconTx = stx.getTxCopy();
   EXPECT_EQ(reconTx.serialize(),   rawTx0_);
   EXPECT_EQ(stx.getSerializedTx(), rawTx0_);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxSerUnfragToFrag)
{
   StoredTx stx;
   stx.unserialize(rawTxUnfrag_);

   EXPECT_EQ(stx.getSerializedTx(),        rawTxUnfrag_);
   EXPECT_EQ(stx.getSerializedTxFragged(), rawTxFragged_);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxSerDBValue_1)
{
   Tx origTx(rawTxUnfrag_);

   StoredTx stx;
   stx.unserialize(rawTxUnfrag_);

   BinaryData  first2  = READHEX("97014400"); // little-endian, of course
   BinaryData  txHash  = origTx.getThisHash();
   BinaryData  fragged = stx.getSerializedTxFragged();
   BinaryData  output  = first2 + txHash + fragged;
   EXPECT_EQ(serializeDBValue(stx, ARMORY_DB_FULL), output);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxUnserDBValue_1)
{
   Tx origTx(rawTxUnfrag_);

   BinaryData toUnser = READHEX(
      "97014400e471262336aa67391e57c8c6fe03bae29734079e06ff75c7fa4d0a873c83"
      "f03c01000000020044fbc929d78e4203eed6f1d3d39c0157d8e5c100bbe08867"
      "79c0ebf6a69324010000008a47304402206568144ed5e7064d6176c74738b04c"
      "08ca19ca54ddeb480084b77f45eebfe57802207927d6975a5ac0e1bb36f5c053"
      "56dcda1f521770511ee5e03239c8e1eecf3aed0141045d74feae58c4c36d7c35"
      "beac05eddddc78b3ce4b02491a2eea72043978056a8bc439b99ddaad327207b0"
      "9ef16a8910828e805b0cc8c11fba5caea2ee939346d7ffffffff45c866b219b1"
      "76952508f8e5aea728f950186554fc4a5807e2186a8e1c4009e5000000008c49"
      "3046022100bd5d41662f98cfddc46e86ea7e4a3bc8fe9f1dfc5c4836eaf7df58"
      "2596cfe0e9022100fc459ae4f59b8279d679003b88935896acd10021b6e2e461"
      "9377e336b5296c5e014104c00bab76a708ba7064b2315420a1c533ca9945eeff"
      "9754cdc574224589e9113469b4e71752146a10028079e04948ecdf70609bf1b9"
      "801f6b73ab75947ac339e5ffffffff0200000000");

   BinaryRefReader brr(toUnser);

   StoredTx stx;
   stx.unserializeDBValue(brr);

   EXPECT_TRUE( stx.isInitialized());
   EXPECT_EQ(   stx.thisHash_,    origTx.getThisHash());
   EXPECT_EQ(   stx.lockTime_,    origTx.getLockTime());
   EXPECT_EQ(   stx.dataCopy_,    rawTxFragged_);
   EXPECT_TRUE( stx.isFragged_);
   EXPECT_EQ(   stx.version_,     1ULL);
   EXPECT_EQ(   stx.blockHeight_, UINT32_MAX);
   EXPECT_EQ(   stx.duplicateID_, UINT8_MAX);
   EXPECT_EQ(   stx.txIndex_,     UINT16_MAX);
   EXPECT_EQ(   stx.numTxOut_,    origTx.getNumTxOut());
   EXPECT_EQ(   stx.numBytes_,    UINT32_MAX);
   EXPECT_EQ(   stx.fragBytes_,   370ULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxUnserDBValue_2)
{
   Tx origTx(rawTxUnfrag_);

   BinaryData toUnser = READHEX(
      "97010040e471262336aa67391e57c8c6fe03bae29734079e06ff75c7fa4d0a873c83"
      "f03c01000000020044fbc929d78e4203eed6f1d3d39c0157d8e5c100bbe08867"
      "79c0ebf6a69324010000008a47304402206568144ed5e7064d6176c74738b04c"
      "08ca19ca54ddeb480084b77f45eebfe57802207927d6975a5ac0e1bb36f5c053"
      "56dcda1f521770511ee5e03239c8e1eecf3aed0141045d74feae58c4c36d7c35"
      "beac05eddddc78b3ce4b02491a2eea72043978056a8bc439b99ddaad327207b0"
      "9ef16a8910828e805b0cc8c11fba5caea2ee939346d7ffffffff45c866b219b1"
      "76952508f8e5aea728f950186554fc4a5807e2186a8e1c4009e5000000008c49"
      "3046022100bd5d41662f98cfddc46e86ea7e4a3bc8fe9f1dfc5c4836eaf7df58"
      "2596cfe0e9022100fc459ae4f59b8279d679003b88935896acd10021b6e2e461"
      "9377e336b5296c5e014104c00bab76a708ba7064b2315420a1c533ca9945eeff"
      "9754cdc574224589e9113469b4e71752146a10028079e04948ecdf70609bf1b9"
      "801f6b73ab75947ac339e5ffffffff02ac4c8bd5000000001976a9148dce8946"
      "f1c7763bb60ea5cf16ef514cbed0633b88ac002f6859000000001976a9146a59"
      "ac0e8f553f292dfe5e9f3aaa1da93499c15e88ac00000000");

   BinaryRefReader brr(toUnser);

   StoredTx stx;
   stx.unserializeDBValue(brr);

   EXPECT_TRUE( stx.isInitialized());
   EXPECT_EQ(   stx.thisHash_,    origTx.getThisHash());
   EXPECT_EQ(   stx.lockTime_,    origTx.getLockTime());
   EXPECT_EQ(   stx.dataCopy_,    rawTxUnfrag_);
   EXPECT_FALSE(stx.isFragged_);
   EXPECT_EQ(   stx.version_,     1ULL);
   EXPECT_EQ(   stx.blockHeight_, UINT32_MAX);
   EXPECT_EQ(   stx.duplicateID_,  UINT8_MAX);
   EXPECT_EQ(   stx.txIndex_,     UINT16_MAX);
   EXPECT_EQ(   stx.numTxOut_,    origTx.getNumTxOut());
   EXPECT_EQ(   stx.numBytes_,    origTx.getSize());
   EXPECT_EQ(   stx.fragBytes_,   370ULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxOutUnserialize)
{
   TxOut        txo0,  txo1;
   StoredTxOut stxo0, stxo1;

   stxo0.unserialize(rawTxOut0_);
   stxo1.unserialize(rawTxOut1_);
    txo0.unserialize(rawTxOut0_);
    txo1.unserialize(rawTxOut1_);

   uint64_t val0 = READ_UINT64_HEX_LE("ac4c8bd500000000");
   uint64_t val1 = READ_UINT64_HEX_LE("002f685900000000");

   EXPECT_EQ(stxo0.getSerializedTxOut(), rawTxOut0_);
   EXPECT_EQ(stxo0.getSerializedTxOut(), txo0.serialize());
   EXPECT_EQ(stxo1.getSerializedTxOut(), rawTxOut1_);
   EXPECT_EQ(stxo1.getSerializedTxOut(), txo1.serialize());

   EXPECT_EQ(stxo0.getValue(), val0);
   EXPECT_EQ(stxo1.getValue(), val1);
   
   TxOut txoRecon = stxo0.getTxOutCopy();
   EXPECT_EQ(txoRecon.serialize(), rawTxOut0_);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxOutSerDBValue_1)
{
//    DBUtils::setArmoryDbType(ARMORY_DB_FULL);
//    DBUtils::setDbPruneType(DB_PRUNE_NONE);

   StoredTxOut stxo0;

   stxo0.unserialize(rawTxOut0_);

   stxo0.txVersion_ = 1;
   stxo0.spentness_ = TXOUT_UNSPENT;

   //   0123   45    67   0  123 4567 
   //  |----| |--|  |--| |-|
   //   DBVer TxVer Spnt  CB
   //
   // For this example:  DBVer=0, TxVer=1, TxSer=FRAGGED[1]
   //   0000   01    00   0  --- ----
   EXPECT_EQ(serializeDBValue(stxo0),  
      READHEX("1400") + rawTxOut0_);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxOutSerDBValue_2)
{
//    DBUtils::setArmoryDbType(ARMORY_DB_FULL);
//    DBUtils::setDbPruneType(DB_PRUNE_NONE);

   StoredTxOut stxo0;
   stxo0.unserialize(rawTxOut0_);
   stxo0.txVersion_ = 1;
   stxo0.spentness_ = TXOUT_UNSPENT;

   // Test a spent TxOut
   //   0000   01    01   0  --- ----
   BinaryData spentStr = DBUtils::getBlkDataKeyNoPrefix( 100000, 1, 127, 15);
   stxo0.spentness_ = TXOUT_SPENT;
   stxo0.spentByTxInKey_ = spentStr;
   EXPECT_EQ(
      serializeDBValue(stxo0),
      READHEX("1500")+rawTxOut0_+spentStr
   );
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxOutSerDBValue_3)
{
//    DBUtils::setArmoryDbType(ARMORY_DB_FULL);
//    DBUtils::setDbPruneType(DB_PRUNE_NONE);

   StoredTxOut stxo0;
   stxo0.unserialize(rawTxOut0_);
   stxo0.txVersion_ = 1;
   stxo0.isCoinbase_ = true;

   // Test a spent TxOut but in lite mode where we don't record spentness
   //   0000   01    01   1  --- ----
   BinaryData spentStr = DBUtils::getBlkDataKeyNoPrefix( 100000, 1, 127, 15);
   stxo0.spentness_ = TXOUT_SPENT;
   stxo0.spentByTxInKey_ = spentStr;
   EXPECT_EQ(
      serializeDBValue(stxo0),
      READHEX("1580") + rawTxOut0_ + spentStr
   );
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxOutUnserDBValue_1)
{
   BinaryData input = READHEX( "0400ac4c8bd5000000001976a9148dce8946f1c7763b"
                               "b60ea5cf16ef514cbed0633b88ac");
   StoredTxOut stxo;
   stxo.unserializeDBValue(input);

   EXPECT_TRUE( stxo.isInitialized());
   EXPECT_EQ(   stxo.txVersion_,    1ULL);
   EXPECT_EQ(   stxo.dataCopy_,     rawTxOut0_);
   EXPECT_EQ(   stxo.blockHeight_,  UINT32_MAX);
   EXPECT_EQ(   stxo.duplicateID_,   UINT8_MAX);
   EXPECT_EQ(   stxo.txIndex_,      UINT16_MAX);
   EXPECT_EQ(   stxo.txOutIndex_,   UINT16_MAX);
   EXPECT_EQ(   stxo.spentness_,    TXOUT_UNSPENT);
   EXPECT_EQ(   stxo.spentByTxInKey_.getSize(), 0ULL);
   EXPECT_FALSE(stxo.isCoinbase_);
   EXPECT_EQ(   stxo.unserArmVer_,  0ULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxOutUnserDBValue_2)
{
   BinaryData input = READHEX( "0500ac4c8bd5000000001976a9148dce8946f1c7763b"
                               "b60ea5cf16ef514cbed0633b88ac01a086017f000f00");
   StoredTxOut stxo;
   stxo.unserializeDBValue(input);

   EXPECT_TRUE( stxo.isInitialized());
   EXPECT_EQ(   stxo.txVersion_,    1ULL);
   EXPECT_EQ(   stxo.dataCopy_,     rawTxOut0_);
   EXPECT_EQ(   stxo.blockHeight_,  UINT32_MAX);
   EXPECT_EQ(   stxo.duplicateID_,   UINT8_MAX);
   EXPECT_EQ(   stxo.txIndex_,      UINT16_MAX);
   EXPECT_EQ(   stxo.txOutIndex_,   UINT16_MAX);
   EXPECT_EQ(   stxo.spentness_,    TXOUT_SPENT);
   EXPECT_FALSE(stxo.isCoinbase_);
   EXPECT_EQ(   stxo.spentByTxInKey_, READHEX("01a086017f000f00"));
   EXPECT_EQ(   stxo.unserArmVer_,  0ULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxOutUnserDBValue_3)
{
   BinaryData input = READHEX( "0680ac4c8bd5000000001976a9148dce8946f1c7763b"
                               "b60ea5cf16ef514cbed0633b88ac");
   StoredTxOut stxo;
   stxo.unserializeDBValue(input);

   EXPECT_TRUE( stxo.isInitialized());
   EXPECT_EQ(   stxo.txVersion_,    1ULL);
   EXPECT_EQ(   stxo.dataCopy_,     rawTxOut0_);
   EXPECT_EQ(   stxo.blockHeight_,  UINT32_MAX);
   EXPECT_EQ(   stxo.duplicateID_,   UINT8_MAX);
   EXPECT_EQ(   stxo.txIndex_,      UINT16_MAX);
   EXPECT_EQ(   stxo.txOutIndex_,   UINT16_MAX);
   EXPECT_EQ(   stxo.spentness_,    TXOUT_SPENTUNK);
   EXPECT_TRUE( stxo.isCoinbase_);
   EXPECT_EQ(   stxo.spentByTxInKey_.getSize(), 0ULL);
   EXPECT_EQ(   stxo.unserArmVer_,  0ULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, SHeaderFullBlock)
{
   // I'll make this more robust later... kind of tired of writing tests...
   StoredHeader sbh;
   sbh.unserializeFullBlock(rawBlock_.getRef());

   BinaryWriter bw;
   sbh.serializeFullBlock(bw);

   EXPECT_EQ(bw.getDataRef(), rawBlock_.getRef());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, SUndoDataSer)
{
//    DBUtils::setArmoryDbType(ARMORY_DB_FULL);
//    DBUtils::setDbPruneType(DB_PRUNE_NONE);

   BinaryData arbHash  = READHEX("11112221111222111122222211112222"
                                 "11112221111222111122211112221111");
   BinaryData op0_str  = READHEX("aaaabbbbaaaabbbbaaaabbbbaaaabbbb"
                                 "aaaabbbbaaaabbbbaaaabbbbaaaabbbb");
   BinaryData op1_str  = READHEX("ffffbbbbffffbbbbffffbbbbffffbbbb"
                                 "ffffbbbbffffbbbbffffbbbbffffbbbb");

   
   StoredUndoData sud;
   OutPoint op0(op0_str, 1);
   OutPoint op1(op1_str, 2);

   StoredTxOut stxo0, stxo1;
   stxo0.unserialize(rawTxOut0_);
   stxo1.unserialize(rawTxOut1_);

   stxo0.txVersion_  = 1;
   stxo1.txVersion_  = 1;
   stxo0.blockHeight_ = 100000;
   stxo1.blockHeight_ = 100000;
   stxo0.duplicateID_ = 2;
   stxo1.duplicateID_ = 2;
   stxo0.txIndex_ = 17;
   stxo1.txIndex_ = 17;
   stxo0.parentHash_ = arbHash;
   stxo1.parentHash_ = arbHash;
   stxo0.txOutIndex_ = 5;
   stxo1.txOutIndex_ = 5;

   sud.stxOutsRemovedByBlock_.clear();
   sud.stxOutsRemovedByBlock_.push_back(stxo0);
   sud.stxOutsRemovedByBlock_.push_back(stxo1);
   sud.outPointsAddedByBlock_.clear();
   sud.outPointsAddedByBlock_.push_back(op0);
   sud.outPointsAddedByBlock_.push_back(op1);

   sud.blockHash_ = arbHash;
   sud.blockHeight_ = 123000; // unused for this test
   sud.duplicateID_ = 15;     // unused for this test

   BinaryData flags = READHEX("04");
   BinaryData str2  = WRITE_UINT32_LE(2);
   BinaryData str5  = WRITE_UINT32_LE(5);
   BinaryData answer = 
         arbHash + 
            str2 + 
               flags + stxo0.getDBKey(false) + arbHash + str5 + rawTxOut0_ +
               flags + stxo1.getDBKey(false) + arbHash + str5 + rawTxOut1_ +
            str2 +
               op0.serialize() +
               op1.serialize();

   EXPECT_EQ(serializeDBValue(sud), answer);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, SUndoDataUnser)
{
//    DBUtils::setArmoryDbType(ARMORY_DB_FULL);
//    DBUtils::setDbPruneType(DB_PRUNE_NONE);

   BinaryData arbHash  = READHEX("11112221111222111122222211112222"
                                 "11112221111222111122211112221111");
   BinaryData op0_str  = READHEX("aaaabbbbaaaabbbbaaaabbbbaaaabbbb"
                                 "aaaabbbbaaaabbbbaaaabbbbaaaabbbb");
   BinaryData op1_str  = READHEX("ffffbbbbffffbbbbffffbbbbffffbbbb"
                                 "ffffbbbbffffbbbbffffbbbbffffbbbb");
   OutPoint op0(op0_str, 1);
   OutPoint op1(op1_str, 2);

   //BinaryData sudToUnser = READHEX( 
      //"1111222111122211112222221111222211112221111222111122211112221111"
      //"0200000024111122211112221111222222111122221111222111122211112221"
      //"111222111105000000ac4c8bd5000000001976a9148dce8946f1c7763bb60ea5"
      //"cf16ef514cbed0633b88ac241111222111122211112222221111222211112221"
      //"11122211112221111222111105000000002f6859000000001976a9146a59ac0e"
      //"8f553f292dfe5e9f3aaa1da93499c15e88ac02000000aaaabbbbaaaabbbbaaaa"
      //"bbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbb01000000ffffbbbbffff"
      //"bbbbffffbbbbffffbbbbffffbbbbffffbbbbffffbbbbffffbbbb02000000");

   BinaryData sudToUnser = READHEX( 
      "1111222111122211112222221111222211112221111222111122211112221111"
      "02000000240186a0020011000511112221111222111122222211112222111122"
      "2111122211112221111222111105000000ac4c8bd5000000001976a9148dce89"
      "46f1c7763bb60ea5cf16ef514cbed0633b88ac240186a0020011000511112221"
      "1112221111222222111122221111222111122211112221111222111105000000"
      "002f6859000000001976a9146a59ac0e8f553f292dfe5e9f3aaa1da93499c15e"
      "88ac02000000aaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaa"
      "bbbbaaaabbbb01000000ffffbbbbffffbbbbffffbbbbffffbbbbffffbbbbffff"
      "bbbbffffbbbbffffbbbb02000000");

   StoredUndoData sud;
   sud.unserializeDBValue(sudToUnser);

   ASSERT_EQ(sud.outPointsAddedByBlock_.size(), 2ULL);
   ASSERT_EQ(sud.stxOutsRemovedByBlock_.size(), 2ULL);

   EXPECT_EQ(sud.outPointsAddedByBlock_[0].serialize(), op0.serialize());
   EXPECT_EQ(sud.outPointsAddedByBlock_[1].serialize(), op1.serialize());
   EXPECT_EQ(sud.stxOutsRemovedByBlock_[0].getSerializedTxOut(), rawTxOut0_);
   EXPECT_EQ(sud.stxOutsRemovedByBlock_[1].getSerializedTxOut(), rawTxOut1_);

   EXPECT_EQ(sud.stxOutsRemovedByBlock_[0].parentHash_, arbHash);
   EXPECT_EQ(sud.stxOutsRemovedByBlock_[1].parentHash_, arbHash);

   EXPECT_EQ(sud.stxOutsRemovedByBlock_[0].blockHeight_, 100000ULL);
   EXPECT_EQ(sud.stxOutsRemovedByBlock_[1].blockHeight_, 100000ULL);
   EXPECT_EQ(sud.stxOutsRemovedByBlock_[0].duplicateID_, 2ULL);
   EXPECT_EQ(sud.stxOutsRemovedByBlock_[1].duplicateID_, 2ULL);
   EXPECT_EQ(sud.stxOutsRemovedByBlock_[0].txIndex_, 17ULL);
   EXPECT_EQ(sud.stxOutsRemovedByBlock_[1].txIndex_, 17ULL);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxHintsSer)
{
   BinaryData hint0 = DBUtils::getBlkDataKeyNoPrefix(123000,  7, 255);
   BinaryData hint1 = DBUtils::getBlkDataKeyNoPrefix(123000, 15, 127);
   BinaryData hint2 = DBUtils::getBlkDataKeyNoPrefix(183922, 15,   3);

   StoredTxHints sths;
   sths.txHashPrefix_ = READHEX("aaaaffff");
   sths.dbKeyList_.clear();

   /////
   BinaryWriter ans0;
   ans0.put_var_int(0);
   EXPECT_EQ(sths.serializeDBValue(), ans0.getData());

   /////
   sths.dbKeyList_.push_back(hint0);
   sths.preferredDBKey_ = hint0;
   BinaryWriter ans1;
   ans1.put_var_int(1);
   ans1.put_BinaryData(hint0);
   EXPECT_EQ(sths.dbKeyList_.size(), 1ULL);
   EXPECT_EQ(sths.preferredDBKey_, hint0);
   EXPECT_EQ(sths.serializeDBValue(), ans1.getData());

   /////
   sths.dbKeyList_.push_back(hint1);
   sths.dbKeyList_.push_back(hint2);
   BinaryWriter ans3;
   ans3.put_var_int(3);
   ans3.put_BinaryData(hint0);
   ans3.put_BinaryData(hint1);
   ans3.put_BinaryData(hint2);
   EXPECT_EQ(sths.dbKeyList_.size(), 3ULL);
   EXPECT_EQ(sths.preferredDBKey_, hint0);
   EXPECT_EQ(sths.serializeDBValue(), ans3.getData());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxHintsReorder)
{
   BinaryData hint0 = DBUtils::getBlkDataKeyNoPrefix(123000,  7, 255);
   BinaryData hint1 = DBUtils::getBlkDataKeyNoPrefix(123000, 15, 127);
   BinaryData hint2 = DBUtils::getBlkDataKeyNoPrefix(183922, 15,   3);

   StoredTxHints sths;
   sths.txHashPrefix_ = READHEX("aaaaffff");
   sths.dbKeyList_.clear();
   sths.dbKeyList_.push_back(hint0);
   sths.dbKeyList_.push_back(hint1);
   sths.dbKeyList_.push_back(hint2);
   sths.preferredDBKey_ = hint1;

   BinaryWriter expectedOut;
   expectedOut.put_var_int(3);
   expectedOut.put_BinaryData(hint1);
   expectedOut.put_BinaryData(hint0);
   expectedOut.put_BinaryData(hint2);

   EXPECT_EQ(sths.serializeDBValue(), expectedOut.getData());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, STxHintsUnser)
{
   BinaryData hint0 = DBUtils::getBlkDataKeyNoPrefix(123000,  7, 255);
   BinaryData hint1 = DBUtils::getBlkDataKeyNoPrefix(123000, 15, 127);
   BinaryData hint2 = DBUtils::getBlkDataKeyNoPrefix(183922, 15,   3);

   BinaryData in0 = READHEX("00");
   BinaryData in1 = READHEX("01""01e0780700ff");
   BinaryData in3 = READHEX("03""01e0780700ff""01e0780f007f""02ce720f0003");

   StoredTxHints sths0, sths1, sths3;

   sths0.unserializeDBValue(in0);

   EXPECT_EQ(sths0.dbKeyList_.size(), 0ULL);
   EXPECT_EQ(sths0.preferredDBKey_.getSize(), 0ULL);

   sths1.unserializeDBValue(in1);

   EXPECT_EQ(sths1.dbKeyList_.size(),  1ULL);
   EXPECT_EQ(sths1.dbKeyList_[0],      hint0);
   EXPECT_EQ(sths1.preferredDBKey_,    hint0);

   sths3.unserializeDBValue(in3);
   EXPECT_EQ(sths3.dbKeyList_.size(),  3ULL);
   EXPECT_EQ(sths3.dbKeyList_[0],      hint0);
   EXPECT_EQ(sths3.dbKeyList_[1],      hint1);
   EXPECT_EQ(sths3.dbKeyList_[2],      hint2);
   EXPECT_EQ(sths3.preferredDBKey_,    hint0);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, SHeadHgtListSer)
{
   StoredHeadHgtList baseHHL, testHHL;
   baseHHL.height_ = 123000;
   baseHHL.dupAndHashList_.resize(0);
   BinaryData hash0 = READHEX("aaaabbbbaaaabbbbaaaabbbbaaaabbbb"
                              "aaaabbbbaaaabbbbaaaabbbbaaaabbbb");
   BinaryData hash1 = READHEX("2222bbbb2222bbbb2222bbbb2222bbbb"
                              "2222bbbb2222bbbb2222bbbb2222bbbb");
   BinaryData hash2 = READHEX("2222ffff2222ffff2222ffff2222ffff"
                              "2222ffff2222ffff2222ffff2222ffff");

   uint8_t dup0 = 0;
   uint8_t dup1 = 1;
   uint8_t dup2 = 7;

   BinaryWriter expectOut;

   // Test writing empty list
   expectOut.reset();
   expectOut.put_uint8_t(0);
   EXPECT_EQ(testHHL.serializeDBValue(), expectOut.getData());

   
   // Test writing list with one entry but no preferred dupID
   expectOut.reset();
   testHHL = baseHHL;
   testHHL.dupAndHashList_.push_back(pair<uint8_t, BinaryData>(dup0, hash0)); 
   expectOut.put_uint8_t(1);
   expectOut.put_uint8_t(dup0);
   expectOut.put_BinaryData(hash0);
   EXPECT_EQ(testHHL.serializeDBValue(), expectOut.getData());
   
   // Test writing list with one entry which is a preferred dupID
   expectOut.reset();
   testHHL = baseHHL;
   testHHL.preferredDup_ = 0;
   testHHL.dupAndHashList_.push_back(pair<uint8_t, BinaryData>(dup0, hash0)); 
   expectOut.put_uint8_t(1);
   expectOut.put_uint8_t(dup0 | 0x80);
   expectOut.put_BinaryData(hash0);
   EXPECT_EQ(testHHL.serializeDBValue(), expectOut.getData());

   // Test writing list with one entry preferred dupID but that dup isn't avail
   expectOut.reset();
   testHHL = baseHHL;
   testHHL.preferredDup_ = 1;
   testHHL.dupAndHashList_.push_back(pair<uint8_t, BinaryData>(dup0, hash0)); 
   expectOut.put_uint8_t(1);
   expectOut.put_uint8_t(dup0);
   expectOut.put_BinaryData(hash0);
   EXPECT_EQ(testHHL.serializeDBValue(), expectOut.getData());

   // Test writing with three entries, no preferred
   expectOut.reset();
   testHHL = baseHHL;
   testHHL.dupAndHashList_.push_back(pair<uint8_t, BinaryData>(dup0, hash0)); 
   testHHL.dupAndHashList_.push_back(pair<uint8_t, BinaryData>(dup1, hash1)); 
   testHHL.dupAndHashList_.push_back(pair<uint8_t, BinaryData>(dup2, hash2)); 
   expectOut.put_uint8_t(3);
   expectOut.put_uint8_t(dup0); expectOut.put_BinaryData(hash0);
   expectOut.put_uint8_t(dup1); expectOut.put_BinaryData(hash1);
   expectOut.put_uint8_t(dup2); expectOut.put_BinaryData(hash2);
   EXPECT_EQ(testHHL.serializeDBValue(), expectOut.getData());


   // Test writing with three entries, with preferred
   expectOut.reset();
   testHHL = baseHHL;
   testHHL.dupAndHashList_.push_back(pair<uint8_t, BinaryData>(dup0, hash0)); 
   testHHL.dupAndHashList_.push_back(pair<uint8_t, BinaryData>(dup1, hash1)); 
   testHHL.dupAndHashList_.push_back(pair<uint8_t, BinaryData>(dup2, hash2)); 
   testHHL.preferredDup_ = 1;
   expectOut.put_uint8_t(3);
   expectOut.put_uint8_t(dup1 | 0x80); expectOut.put_BinaryData(hash1);
   expectOut.put_uint8_t(dup0);        expectOut.put_BinaryData(hash0);
   expectOut.put_uint8_t(dup2);        expectOut.put_BinaryData(hash2);
   EXPECT_EQ(testHHL.serializeDBValue(), expectOut.getData());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, SHeadHgtListUnser)
{
   BinaryData hash0 = READHEX("aaaabbbbaaaabbbbaaaabbbbaaaabbbb"
                              "aaaabbbbaaaabbbbaaaabbbbaaaabbbb");
   BinaryData hash1 = READHEX("2222bbbb2222bbbb2222bbbb2222bbbb"
                              "2222bbbb2222bbbb2222bbbb2222bbbb");
   BinaryData hash2 = READHEX("2222ffff2222ffff2222ffff2222ffff"
                              "2222ffff2222ffff2222ffff2222ffff");

   vector<BinaryData> tests;
   tests.push_back( READHEX(
      "0100aaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbb"));
   tests.push_back( READHEX(
      "0180aaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbb"));
   tests.push_back( READHEX(
      "0300aaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaa"
      "bbbb012222bbbb2222bbbb2222bbbb2222bbbb2222bbbb2222bbbb2222bbbb22"
      "22bbbb072222ffff2222ffff2222ffff2222ffff2222ffff2222ffff2222ffff"
      "2222ffff"));
   tests.push_back( READHEX(
      "03812222bbbb2222bbbb2222bbbb2222bbbb2222bbbb2222bbbb2222bbbb2222"
      "bbbb00aaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbbaaaabbbbaa"
      "aabbbb072222ffff2222ffff2222ffff2222ffff2222ffff2222ffff2222ffff"
      "2222ffff"));

   uint8_t dup0 = 0;
   uint8_t dup1 = 1;
   uint8_t dup2 = 7;

   for(uint32_t i=0; i<tests.size(); i++)
   {
      BinaryRefReader brr(tests[i]);
      StoredHeadHgtList hhl;
      hhl.unserializeDBValue(brr);

      if(i==0)
      {
         ASSERT_EQ(hhl.dupAndHashList_.size(), 1ULL);
         EXPECT_EQ(hhl.dupAndHashList_[0].first,  dup0);
         EXPECT_EQ(hhl.dupAndHashList_[0].second, hash0);
         EXPECT_EQ(hhl.preferredDup_,  UINT8_MAX);
      }
      else if(i==1)
      {
         ASSERT_EQ(hhl.dupAndHashList_.size(), 1ULL);
         EXPECT_EQ(hhl.dupAndHashList_[0].first,  dup0);
         EXPECT_EQ(hhl.dupAndHashList_[0].second, hash0);
         EXPECT_EQ(hhl.preferredDup_,  0);
      }
      else if(i==2)
      {
         ASSERT_EQ(hhl.dupAndHashList_.size(), 3ULL);
         EXPECT_EQ(hhl.dupAndHashList_[0].first,  dup0);
         EXPECT_EQ(hhl.dupAndHashList_[0].second, hash0);
         EXPECT_EQ(hhl.dupAndHashList_[1].first,  dup1);
         EXPECT_EQ(hhl.dupAndHashList_[1].second, hash1);
         EXPECT_EQ(hhl.dupAndHashList_[2].first,  dup2);
         EXPECT_EQ(hhl.dupAndHashList_[2].second, hash2);
         EXPECT_EQ(hhl.preferredDup_,  UINT8_MAX);
      }
      else if(i==3)
      {
         ASSERT_EQ(hhl.dupAndHashList_.size(), 3ULL);
         EXPECT_EQ(hhl.dupAndHashList_[0].first,  dup1);
         EXPECT_EQ(hhl.dupAndHashList_[0].second, hash1);
         EXPECT_EQ(hhl.dupAndHashList_[1].first,  dup0);
         EXPECT_EQ(hhl.dupAndHashList_[1].second, hash0);
         EXPECT_EQ(hhl.dupAndHashList_[2].first,  dup2);
         EXPECT_EQ(hhl.dupAndHashList_[2].second, hash2);
         EXPECT_EQ(hhl.preferredDup_,  1);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, SScriptHistorySer)
{
   StoredScriptHistory ssh;
   ssh.uniqueKey_ = READHEX("00""1234abcde1234abcde1234abcdefff1234abcdef");
   ssh.version_ = 1;
   ssh.scanHeight_ = 65535;

   /////////////////////////////////////////////////////////////////////////////
   // Empty ssh (shouldn't be written in supernode, should be in full node)
   BinaryData expect, expSub1, expSub2;
   expect = READHEX("0000""ffff0000ffffffff""00""0000000000000000""00000000");
   EXPECT_EQ(serializeDBValue(ssh, ARMORY_DB_BARE), expect);

   /////////////////////////////////////////////////////////////////////////////
   // With a single TxIO
   TxIOPair txio0(READHEX("0000ff00""0001""0001"), READ_UINT64_HEX_LE("0100000000000000"));
   txio0.setFromCoinbase(false);
   txio0.setTxOutFromSelf(false);
   txio0.setMultisig(false);
   ssh.insertTxio(txio0);

   expect = READHEX("0000""ffff0000ffffffff""01""0100000000000000""00000000");
   EXPECT_EQ(serializeDBValue(ssh, ARMORY_DB_BARE), expect);

   /////////////////////////////////////////////////////////////////////////////
   // Added a second one, different subSSH
   TxIOPair txio1(READHEX("00010000""0002""0002"), READ_UINT64_HEX_LE("0002000000000000"));
   ssh.insertTxio(txio1);
   expect  = READHEX("0000""ffff0000ffffffff""02""0102000000000000""00000000");
   expSub1 = READHEX("01""00""0100000000000000""0001""0001");
   expSub2 = READHEX("01""00""0002000000000000""0002""0002");
   EXPECT_EQ(serializeDBValue(ssh, ARMORY_DB_BARE), expect);
   EXPECT_EQ(serializeDBValue(ssh.subHistMap_[READHEX("0000ff00")]), expSub1);
   EXPECT_EQ(serializeDBValue(ssh.subHistMap_[READHEX("00010000")]), expSub2);

   /////////////////////////////////////////////////////////////////////////////
   // Added another TxIO to the second subSSH
   TxIOPair txio2(READHEX("00010000""0004""0004"), READ_UINT64_HEX_LE("0000030000000000"));
   ssh.insertTxio(txio2);
   expect  = READHEX("0000""ffff0000ffffffff""03""0102030000000000""00000000");
   expSub1 = READHEX("01"
                       "00""0100000000000000""0001""0001");
   expSub2 = READHEX("02"
                       "00""0002000000000000""0002""0002"
                       "00""0000030000000000""0004""0004");
   EXPECT_EQ(serializeDBValue(ssh, ARMORY_DB_BARE), expect);
   EXPECT_EQ(serializeDBValue(ssh.subHistMap_[READHEX("0000ff00")]), expSub1);
   EXPECT_EQ(serializeDBValue(ssh.subHistMap_[READHEX("00010000")]), expSub2);

   /////////////////////////////////////////////////////////////////////////////
   // Now we explicitly delete a TxIO (with pruning, this should be basically
   // equivalent to marking it spent, but we are DB-mode-agnostic here, testing
   // just the base insert/erase operations)
   ssh.eraseTxio(txio1);
   expect  = READHEX("0000""ffff0000ffffffff""02""0100030000000000""00000000");
   expSub1 = READHEX("01"
                       "00""0100000000000000""0001""0001");
   expSub2 = READHEX("01"
                       "00""0000030000000000""0004""0004");
   EXPECT_EQ(serializeDBValue(ssh, ARMORY_DB_BARE), expect);
   EXPECT_EQ(serializeDBValue(ssh.subHistMap_[READHEX("0000ff00")]), expSub1);
   EXPECT_EQ(serializeDBValue(ssh.subHistMap_[READHEX("00010000")]), expSub2);
   
   /////////////////////////////////////////////////////////////////////////////
   // Insert a multisig TxIO -- this should increment totalTxioCount_, but not 
   // the value 
   TxIOPair txio3(READHEX("00010000""0006""0006"), READ_UINT64_HEX_LE("0000000400000000"));
   txio3.setMultisig(true);
   ssh.insertTxio(txio3);
   expect  = READHEX("0000""ffff0000ffffffff""03""0100030000000000""00000000");
   expSub1 = READHEX("01"
                       "00""0100000000000000""0001""0001");
   expSub2 = READHEX("02"
                       "00""0000030000000000""0004""0004"
                       "10""0000000400000000""0006""0006");
   EXPECT_EQ(serializeDBValue(ssh, ARMORY_DB_BARE), expect);
   EXPECT_EQ(serializeDBValue(ssh.subHistMap_[READHEX("0000ff00")]), expSub1);
   EXPECT_EQ(serializeDBValue(ssh.subHistMap_[READHEX("00010000")]), expSub2);
   
   /////////////////////////////////////////////////////////////////////////////
   // Remove the multisig
   ssh.eraseTxio(txio3);
   expect  = READHEX("0000""ffff0000ffffffff""02""0100030000000000""00000000");
   expSub1 = READHEX("01"
                       "00""0100000000000000""0001""0001");
   expSub2 = READHEX("01"
                       "00""0000030000000000""0004""0004");
   EXPECT_EQ(serializeDBValue(ssh, ARMORY_DB_BARE), expect);
   EXPECT_EQ(serializeDBValue(ssh.subHistMap_[READHEX("0000ff00")]), expSub1);
   EXPECT_EQ(serializeDBValue(ssh.subHistMap_[READHEX("00010000")]), expSub2);

   /////////////////////////////////////////////////////////////////////////////
   // Remove a full subSSH (it shouldn't be deleted, though, that will be done
   // by BlockUtils in a post-processing step
   ssh.eraseTxio(txio0);
   expect  = READHEX("0000""ffff0000ffffffff""01""0000030000000000""00000000");
   expSub1 = READHEX("00");
   expSub2 = READHEX("01"
                       "00""0000030000000000""0004""0004");
   EXPECT_EQ(serializeDBValue(ssh, ARMORY_DB_BARE), expect);
   EXPECT_EQ(serializeDBValue(ssh.subHistMap_[READHEX("0000ff00")]), expSub1);
   EXPECT_EQ(serializeDBValue(ssh.subHistMap_[READHEX("00010000")]), expSub2);
   
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(StoredBlockObjTest, SScriptHistoryUnser)
{
   StoredScriptHistory ssh, sshorig;
   StoredSubHistory subssh1, subssh2;
   BinaryData toUnser;
   BinaryData hgtX0 = READHEX("0000ff00");
   BinaryData hgtX1 = READHEX("00010000");
   BinaryData uniq  = READHEX("00""0000ffff0000ffff0000ffff0000ffff0000ffff");

   sshorig.uniqueKey_ = uniq;
   sshorig.version_  = 1;

   BinaryWriter bw;
   bw.put_uint8_t(DB_PREFIX_SCRIPT);
   BinaryData DBPREF = bw.getData();

   /////////////////////////////////////////////////////////////////////////////
   ssh = sshorig;
   toUnser = READHEX("0400""ffff0000ffffffff""00""00000000");
   ssh.unserializeDBKey(DBPREF + uniq);
   ssh.unserializeDBValue(toUnser);

   EXPECT_EQ(   ssh.subHistMap_.size(), 0ULL);
   EXPECT_EQ(   ssh.scanHeight_, 65535);
   EXPECT_EQ(   ssh.tallyHeight_, -1);
   EXPECT_EQ(   ssh.totalTxioCount_, 0ULL);
   EXPECT_EQ(   ssh.totalUnspent_, 0ULL);

   /////////////////////////////////////////////////////////////////////////////
   ssh = sshorig;
   toUnser = READHEX("0400""ffff0000ffffffff""01""0100000000000000""00000000");
   ssh.unserializeDBKey(DBPREF + uniq);
   ssh.unserializeDBValue(toUnser);
   BinaryData txioKey = hgtX0 + READHEX("00010001");

   EXPECT_EQ(   ssh.scanHeight_, 65535);
   EXPECT_EQ(   ssh.tallyHeight_, -1);
   EXPECT_EQ(   ssh.totalTxioCount_, 1ULL);
   EXPECT_EQ(   ssh.totalUnspent_, READ_UINT64_HEX_LE("0100000000000000"));


   /////////////////////////////////////////////////////////////////////////////
   // Test reading a subSSH and merging it with the regular ssh
   ssh = sshorig;
   subssh1 = StoredSubHistory();

   ssh.unserializeDBKey(DBPREF + uniq);
   ssh.unserializeDBValue(READHEX("0400""ffff0000ffffffff""02""0000030400000000""00000000"));
   subssh1.unserializeDBKey(DBPREF + uniq + hgtX0);
   subssh1.unserializeDBValue(READHEX("02"
                                        "00""0000030000000000""0004""0004"
                                        "00""0000000400000000""0006""0006"));

   BinaryData last4_0 = READHEX("0004""0004");
   BinaryData last4_1 = READHEX("0006""0006");
   BinaryData txio0key = hgtX0 + last4_0;
   BinaryData txio1key = hgtX0 + last4_1;
   uint64_t val0 = READ_UINT64_HEX_LE("0000030000000000");
   uint64_t val1 = READ_UINT64_HEX_LE("0000000400000000");

   // Unmerged, so ssh doesn't have the subSSH as part of it yet.
   EXPECT_EQ(   ssh.subHistMap_.size(), 0ULL);
   EXPECT_EQ(   ssh.scanHeight_, 65535);
   EXPECT_EQ(   ssh.totalTxioCount_, 2ULL);
   EXPECT_EQ(   ssh.totalUnspent_, READ_UINT64_HEX_LE("0000030400000000"));

   EXPECT_EQ(   subssh1.uniqueKey_,  uniq);
   EXPECT_EQ(   subssh1.hgtX_,       hgtX0);
   EXPECT_EQ(   subssh1.txioMap_.size(), 2ULL);
   ASSERT_NE(   subssh1.txioMap_.find(txio0key), subssh1.txioMap_.end());
   ASSERT_NE(   subssh1.txioMap_.find(txio1key), subssh1.txioMap_.end());
   EXPECT_EQ(   subssh1.txioMap_[txio0key].getValue(), val0);
   EXPECT_EQ(   subssh1.txioMap_[txio1key].getValue(), val1);
   EXPECT_EQ(   subssh1.txioMap_[txio0key].getDBKeyOfOutput(), txio0key);
   EXPECT_EQ(   subssh1.txioMap_[txio1key].getDBKeyOfOutput(), txio1key);

   ssh.mergeSubHistory(subssh1);
   EXPECT_EQ(   ssh.subHistMap_.size(), 1ULL);
   ASSERT_NE(   ssh.subHistMap_.find(hgtX0), ssh.subHistMap_.end());

   StoredSubHistory & subref = ssh.subHistMap_[hgtX0];
   EXPECT_EQ(   subref.uniqueKey_, uniq);
   EXPECT_EQ(   subref.hgtX_,      hgtX0);
   EXPECT_EQ(   subref.txioMap_.size(), 2ULL);
   ASSERT_NE(   subref.txioMap_.find(txio0key), subref.txioMap_.end());
   ASSERT_NE(   subref.txioMap_.find(txio1key), subref.txioMap_.end());
   EXPECT_EQ(   subref.txioMap_[txio0key].getValue(), val0);
   EXPECT_EQ(   subref.txioMap_[txio1key].getValue(), val1);
   EXPECT_EQ(   subref.txioMap_[txio0key].getDBKeyOfOutput(), txio0key);
   EXPECT_EQ(   subref.txioMap_[txio1key].getDBKeyOfOutput(), txio1key);


   /////////////////////////////////////////////////////////////////////////////
   // Try it with two sub-SSHs and a multisig object
   //ssh = sshorig;
   //subssh1 = StoredSubHistory();
   //subssh2 = StoredSubHistory();
   //expSub1 = READHEX("01"
                       //"00""0100000000000000""0001""0001");
   //expSub2 = READHEX("02"
                       //"00""0000030000000000""0004""0004"
                       //"10""0000000400000000""0006""0006");
}

////////////////////////////////////////////////////////////////////////////////
class testBlockHeader : public ::BlockHeader
{
public:
   void setBlockHeight(uint32_t height)
   {
      blockHeight_ = height;
   }
};

class LMDBTest : public ::testing::Test
{
protected:
   virtual void SetUp(void) 
   {
      homedir_ = string("./fakehomedir");
      DBUtils::removeDirectory(homedir_);
      mkdir(homedir_);
      mkdir(homedir_ + "/databases");

      zeros_ = READHEX("00000000");
         
      Armory::Config::parseArgs({
         "--datadir=./fakehomedir",
         "--offline" },
         Armory::Config::ProcessType::DB);

      magic_ = BitcoinSettings::getMagicBytes();
      iface_ = new LMDBBlockDatabase(nullptr, string());

      rawHead_ = READHEX(
         "01000000"
         "1d8f4ec0443e1f19f305e488c1085c95de7cc3fd25e0d2c5bb5d000000000000"
         "9762547903d36881a86751f3f5049e23050113f779735ef82734ebf0b4450081"
         "d8c8c84d"
         "b3936a1a"
         "334b035b");
      headHashLE_ = READHEX(
         "1195e67a7a6d0674bbd28ae096d602e1f038c8254b49dfe79d47000000000000");
      headHashBE_ = READHEX(
         "000000000000479de7df494b25c838f0e102d696e08ad2bb74066d7a7ae69511");

      rawTx0_ = READHEX( 
         "01000000016290dce984203b6a5032e543e9e272d8bce934c7de4d15fa0fe44d"
         "d49ae4ece9010000008b48304502204f2fa458d439f957308bca264689aa175e"
         "3b7c5f78a901cb450ebd20936b2c500221008ea3883a5b80128e55c9c6070aa6"
         "264e1e0ce3d18b7cd7e85108ce3d18b7419a0141044202550a5a6d3bb81549c4"
         "a7803b1ad59cdbba4770439a4923624a8acfc7d34900beb54a24188f7f0a4068"
         "9d905d4847cc7d6c8d808a457d833c2d44ef83f76bffffffff0242582c0a0000"
         "00001976a914c1b4695d53b6ee57a28647ce63e45665df6762c288ac80d1f008"
         "000000001976a9140e0aec36fe2545fb31a41164fb6954adcd96b34288ac0000"
         "0000");
      rawTx1_ = READHEX( 
         "0100000001f658dbc28e703d86ee17c9a2d3b167a8508b082fa0745f55be5144"
         "a4369873aa010000008c49304602210041e1186ca9a41fdfe1569d5d807ca7ff"
         "6c5ffd19d2ad1be42f7f2a20cdc8f1cc0221003366b5d64fe81e53910e156914"
         "091d12646bc0d1d662b7a65ead3ebe4ab8f6c40141048d103d81ac9691cf13f3"
         "fc94e44968ef67b27f58b27372c13108552d24a6ee04785838f34624b294afee"
         "83749b64478bb8480c20b242c376e77eea2b3dc48b4bffffffff0200e1f50500"
         "0000001976a9141b00a2f6899335366f04b277e19d777559c35bc888ac40aeeb"
         "02000000001976a9140e0aec36fe2545fb31a41164fb6954adcd96b34288ac00"
         "000000");

      rawBlock_ = READHEX(
         // Header
         "01000000eb10c9a996a2340a4d74eaab41421ed8664aa49d18538bab59010000"
         "000000005a2f06efa9f2bd804f17877537f2080030cadbfa1eb50e02338117cc"
         "604d91b9b7541a4ecfbb0a1a64f1ade7"
         // 3 transactions
         "03"  
         ///// Tx0, version
         "01000000"
         "01"
         // Tx0, Txin0
         "0000000000000000000000000000000000000000000000000000000000000000"
         "ffffffff"
         "08""04cfbb0a1a02360a""ffffffff"  
         // Tx0, 1 TxOut
         "01"
         // Tx0, TxOut0
         "00f2052a01000000"
         "434104c2239c4eedb3beb26785753463be3ec62b82f6acd62efb65f452f8806f"
         "2ede0b338e31d1f69b1ce449558d7061aa1648ddc2bf680834d3986624006a27"
         "2dc21cac"
         // Tx0, Locktime
         "00000000"
         ///// Tx1, Version 
         "01000000"
         // Tx1, 3 txins
         "03"
         // Tx1, TxIn0
         "e8caa12bcb2e7e86499c9de49c45c5a1c6167ea4b894c8c83aebba1b6100f343"
         "01000000"
         "8c493046022100e2f5af5329d1244807f8347a2c8d9acc55a21a5db769e9274e"
         "7e7ba0bb605b26022100c34ca3350df5089f3415d8af82364d7f567a6a297fcc"
         "2c1d2034865633238b8c014104129e422ac490ddfcb7b1c405ab9fb42441246c"
         "4bca578de4f27b230de08408c64cad03af71ee8a3140b40408a7058a1984a9f2"
         "46492386113764c1ac132990d1""ffffffff" 
         // Tx1, TxIn1
         "5b55c18864e16c08ef9989d31c7a343e34c27c30cd7caa759651b0e08cae0106"
         "00000000"
         "8c4930460221009ec9aa3e0caf7caa321723dea561e232603e00686d4bfadf46"
         "c5c7352b07eb00022100a4f18d937d1e2354b2e69e02b18d11620a6a9332d563"
         "e9e2bbcb01cee559680a014104411b35dd963028300e36e82ee8cf1b0c8d5bf1"
         "fc4273e970469f5cb931ee07759a2de5fef638961726d04bd5eb4e5072330b9b"
         "371e479733c942964bb86e2b22""ffffffff" 
         // Tx1, TxIn2
         "3de0c1e913e6271769d8c0172cea2f00d6d3240afc3a20f9fa247ce58af30d2a"
         "01000000"
         "8c493046022100b610e169fd15ac9f60fe2b507529281cf2267673f4690ba428"
         "cbb2ba3c3811fd022100ffbe9e3d71b21977a8e97fde4c3ba47b896d08bc09ec"
         "b9d086bb59175b5b9f03014104ff07a1833fd8098b25f48c66dcf8fde34cbdbc"
         "c0f5f21a8c2005b160406cbf34cc432842c6b37b2590d16b165b36a3efc9908d"
         "65fb0e605314c9b278f40f3e1a""ffffffff" 
         // Tx1, 2 TxOuts
         "02"
         // Tx1, TxOut0
         "40420f0000000000""19""76a914adfa66f57ded1b655eb4ccd96ee07ca62bc1ddfd88ac"
         // Tx1, TxOut1
         "007d6a7d04000000""19""76a914981a0c9ae61fa8f8c96ae6f8e383d6e07e77133e88ac"
         // Tx1 Locktime
         "00000000"
         ///// Tx2 Version
         "01000000"
         // Tx2 1 TxIn
         "01"
         "38e7586e0784280df58bd3dc5e3d350c9036b1ec4107951378f45881799c92a4"
         "00000000"
         "8a47304402207c945ae0bbdaf9dadba07bdf23faa676485a53817af975ddf85a"
         "104f764fb93b02201ac6af32ddf597e610b4002e41f2de46664587a379a01613"
         "23a85389b4f82dda014104ec8883d3e4f7a39d75c9f5bb9fd581dc9fb1b7cdf7"
         "d6b5a665e4db1fdb09281a74ab138a2dba25248b5be38bf80249601ae688c90c"
         "6e0ac8811cdb740fcec31d""ffffffff" 
         // Tx2, 2 TxOuts
         "02"
         // Tx2, TxOut0
         "2f66ac6105000000""19""76a914964642290c194e3bfab661c1085e47d67786d2d388ac"
         // Tx2, TxOut1
         "2f77e20000000000""19""76a9141486a7046affd935919a3cb4b50a8a0c233c286c88ac"
         // Tx2 Locktime
         "00000000");

      rawTxUnfrag_ = READHEX(
         // Version
         "01000000"
         // NumTxIn
         "02"
         // Start TxIn0
         "0044fbc929d78e4203eed6f1d3d39c0157d8e5c100bbe0886779c0"
         "ebf6a69324010000008a47304402206568144ed5e7064d6176c74738b04c08ca"
         "19ca54ddeb480084b77f45eebfe57802207927d6975a5ac0e1bb36f5c05356dc"
         "da1f521770511ee5e03239c8e1eecf3aed0141045d74feae58c4c36d7c35beac"
         "05eddddc78b3ce4b02491a2eea72043978056a8bc439b99ddaad327207b09ef1"
         "6a8910828e805b0cc8c11fba5caea2ee939346d7ffffffff"
         // Start TxIn1
         "45c866b219b17695"
         "2508f8e5aea728f950186554fc4a5807e2186a8e1c4009e5000000008c493046"
         "022100bd5d41662f98cfddc46e86ea7e4a3bc8fe9f1dfc5c4836eaf7df582596"
         "cfe0e9022100fc459ae4f59b8279d679003b88935896acd10021b6e2e4619377"
         "e336b5296c5e014104c00bab76a708ba7064b2315420a1c533ca9945eeff9754"
         "cdc574224589e9113469b4e71752146a10028079e04948ecdf70609bf1b9801f"
         "6b73ab75947ac339e5ffffffff"
         // NumTxOut
         "02"
         // Start TxOut0
         "ac4c8bd5000000001976a9148dce8946f1c7763bb60ea5cf16ef514cbed0633b88ac"
         // Start TxOut1
         "002f6859000000001976a9146a59ac0e8f553f292dfe5e9f3aaa1da93499c15e88ac"
         // Locktime
         "00000000");

      rawTxFragged_ = READHEX(
         // Version
         "01000000"
         // NumTxIn
         "02"
         // Start TxIn0
         "0044fbc929d78e4203eed6f1d3d39c0157d8e5c100bbe0886779c0"
         "ebf6a69324010000008a47304402206568144ed5e7064d6176c74738b04c08ca"
         "19ca54ddeb480084b77f45eebfe57802207927d6975a5ac0e1bb36f5c05356dc"
         "da1f521770511ee5e03239c8e1eecf3aed0141045d74feae58c4c36d7c35beac"
         "05eddddc78b3ce4b02491a2eea72043978056a8bc439b99ddaad327207b09ef1"
         "6a8910828e805b0cc8c11fba5caea2ee939346d7ffffffff"
         // Start TxIn1
         "45c866b219b17695"
         "2508f8e5aea728f950186554fc4a5807e2186a8e1c4009e5000000008c493046"
         "022100bd5d41662f98cfddc46e86ea7e4a3bc8fe9f1dfc5c4836eaf7df582596"
         "cfe0e9022100fc459ae4f59b8279d679003b88935896acd10021b6e2e4619377"
         "e336b5296c5e014104c00bab76a708ba7064b2315420a1c533ca9945eeff9754"
         "cdc574224589e9113469b4e71752146a10028079e04948ecdf70609bf1b9801f"
         "6b73ab75947ac339e5ffffffff"
         // NumTxOut
         "02"
         // ... TxOuts fragged out 
         // Locktime
         "00000000");

      rawTxOut0_ = READHEX(
         // Value
         "ac4c8bd500000000"
         // Script size (var_int)
         "19"
         // Script
         "76""a9""14""8dce8946f1c7763bb60ea5cf16ef514cbed0633b""88""ac");
      rawTxOut1_ = READHEX(
         // Value 
         "002f685900000000"
         // Script size (var_int)
         "19"
         // Script
         "76""a9""14""6a59ac0e8f553f292dfe5e9f3aaa1da93499c15e""88""ac");

      bh_.unserialize(rawHead_);
      tx1_.unserialize(rawTx0_);
      tx2_.unserialize(rawTx1_);
      sbh_.setHeaderData(rawHead_);
   }

   /////
   virtual void TearDown(void)
   {
      // This seem to be the best way to remove a dir tree in C++ (in Linux)
      iface_->closeDatabases();
      delete iface_;
      iface_ = NULL;

      DBUtils::removeDirectory(homedir_);
      Armory::Config::reset();

      CLEANUP_ALL_TIMERS();
   }

   /////
   void addOutPairH(BinaryData key, BinaryData val)
   { 
      expectOutH_.push_back( pair<BinaryData,BinaryData>(key,val));
   }

   /////
   void addOutPairB(BinaryData key, BinaryData val)
   { 
      expectOutB_.push_back( pair<BinaryData,BinaryData>(key,val));
   }

   /////
   void replaceTopOutPairB(BinaryData key, BinaryData val)
   { 
      uint32_t last = expectOutB_.size() -1;
      expectOutB_[last] = pair<BinaryData,BinaryData>(key,val);
   }

   /////
   void printOutPairs(void)
   {
      cout << "Num Houts: " << expectOutH_.size() << endl;
      for(uint32_t i=0; i<expectOutH_.size(); i++)
      {
         cout << "   \"" << expectOutH_[i].first.toHexStr() << "\"  ";
         cout << "   \"" << expectOutH_[i].second.toHexStr() << "\"    " << endl;
      }
      cout << "Num Bouts: " << expectOutB_.size() << endl;
      for(uint32_t i=0; i<expectOutB_.size(); i++)
      {
         cout << "   \"" << expectOutB_[i].first.toHexStr() << "\"  ";
         cout << "   \"" << expectOutB_[i].second.toHexStr() << "\"    " << endl;
      }
   }

   /////
   bool compareKVListRange(uint32_t startH, uint32_t endplus1H,
                           uint32_t startB, uint32_t endplus1B,
                           DB_SELECT db2 = HISTORY)
   {
      KVLIST fromDB = iface_->getAllDatabaseEntries(HEADERS);

      if(fromDB.size() < endplus1H || expectOutH_.size() < endplus1H)
      {
         LOGERR << "Headers DB not the correct size";
         LOGERR << "DB  size:  " << (int)fromDB.size();
         LOGERR << "Expected:  " << (int)expectOutH_.size();
         return false;
      }

      for(uint32_t i=startH; i<endplus1H; i++)
         if(fromDB[i].first  != expectOutH_[i].first || 
            fromDB[i].second != expectOutH_[i].second)
      {
         LOGERR << "Mismatch of DB keys/values: " << i;
         LOGERR << "KEYS: ";
         LOGERR << "   Database:   " << fromDB[i].first.toHexStr();
         LOGERR << "   Expected:   " << expectOutH_[i].first.toHexStr();
         LOGERR << "VALUES: ";
         LOGERR << "   Database:   " << fromDB[i].second.toHexStr();
         LOGERR << "   Expected:   " << expectOutH_[i].second.toHexStr();
         return false;
      }

      fromDB = iface_->getAllDatabaseEntries(db2);
      if(fromDB.size() < endplus1B || expectOutB_.size() < endplus1B)
      {
         LOGERR << "BLKDATA DB not the correct size";
         LOGERR << "DB  size:  " << (int)fromDB.size();
         LOGERR << "Expected:  " << (int)expectOutB_.size();
         return false;
      }

      for(uint32_t i=startB; i<endplus1B; i++)
         if(fromDB[i].first  != expectOutB_[i].first || 
            fromDB[i].second != expectOutB_[i].second)
      {
         LOGERR << "Mismatch of DB keys/values: " << i;
         LOGERR << "KEYS: ";
         LOGERR << "   Database:   " << fromDB[i].first.toHexStr();
         LOGERR << "   Expected:   " << expectOutB_[i].first.toHexStr();
         LOGERR << "VALUES: ";
         LOGERR << "   Database:   " << fromDB[i].second.toHexStr();
         LOGERR << "   Expected:   " << expectOutB_[i].second.toHexStr();
         return false;
      }

      return true;
   }

   /////
   bool standardOpenDBs(void)
   {
      iface_->openDatabases(Pathing::dbDir());
      auto&& tx = iface_->beginTransaction(HISTORY, LMDB::ReadWrite);

      BinaryData DBINFO = StoredDBInfo().getDBKey();
      BinaryData flags = READHEX("95021000");
      BinaryData val0 = magic_ + flags + zeros_ + zeros_ + BtcUtils::EmptyHash_;
      addOutPairH(DBINFO, val0);
      addOutPairB(DBINFO, val0);

      return iface_->databasesAreOpen();
   }


   LMDBBlockDatabase* iface_;
   vector<pair<BinaryData, BinaryData> > expectOutH_;
   vector<pair<BinaryData, BinaryData> > expectOutB_;

   BinaryData magic_;
   BinaryData zeros_;

   string homedir_;

   BinaryData rawHead_;
   BinaryData headHashLE_;
   BinaryData headHashBE_;
   BinaryData rawBlock_;
   BinaryData rawTx0_;
   BinaryData rawTx1_;
   ::BlockHeader bh_;
   Tx tx1_;
   Tx tx2_;
   StoredHeader sbh_;
   BinaryData rawTxUnfrag_;
   BinaryData rawTxFragged_;
   BinaryData rawTxOut0_;
   BinaryData rawTxOut1_;
};


////////////////////////////////////////////////////////////////////////////////
TEST_F(LMDBTest, OpenClose)
{
   iface_->openDatabases(Pathing::dbDir());
   ASSERT_TRUE(iface_->databasesAreOpen());

   EXPECT_EQ(DBTestUtils::getTopBlockHeight(iface_, HEADERS), 0ULL);

   KVLIST HList = iface_->getAllDatabaseEntries(HEADERS);
   KVLIST BList = iface_->getAllDatabaseEntries(HISTORY);

   // 0123 4567 0123 4567
   // 0000 0010 0001 ---- ---- ---- ---- ----
   BinaryData flags = READHEX("97011000");
   BinaryData ff = READHEX("ffffffffffffffff");

   for(uint32_t i=0; i<HList.size(); i++)
   {
      EXPECT_EQ(HList[i].first,  READHEX("000000"));
      EXPECT_EQ(BList[i].second, magic_ + flags + zeros_ + zeros_ + 
         BtcUtils::EmptyHash_ + BtcUtils::EmptyHash_ + ff);
   }

   for(uint32_t i=0; i<BList.size(); i++)
   {
      EXPECT_EQ(HList[i].first,  READHEX("000000"));
      EXPECT_EQ(BList[i].second, magic_ + flags + zeros_ + zeros_ + 
         BtcUtils::EmptyHash_ + BtcUtils::EmptyHash_ + ff);
   }

   iface_->closeDatabases();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(LMDBTest, OpenCloseOpenNominal)
{
   // 0123 4567 0123 4567
   // 0000 0010 0001 ---- ---- ---- ---- ----
   BinaryData flags = READHEX("97011000");
   BinaryData ff = READHEX("ffffffffffffffff");

   iface_->openDatabases(Pathing::dbDir());
   iface_->closeDatabases();
   iface_->openDatabases(Pathing::dbDir());

   ASSERT_TRUE(iface_->databasesAreOpen());

   KVLIST HList = iface_->getAllDatabaseEntries(HEADERS);
   KVLIST BList = iface_->getAllDatabaseEntries(HISTORY);

   for(uint32_t i=0; i<HList.size(); i++)
   {
      EXPECT_EQ(HList[i].first,  READHEX("000000"));
      EXPECT_EQ(BList[i].second, magic_ + flags + zeros_ + zeros_ +
         BtcUtils::EmptyHash_ + BtcUtils::EmptyHash_ + ff);
   }

   for(uint32_t i=0; i<BList.size(); i++)
   {
      EXPECT_EQ(HList[i].first,  READHEX("000000"));
      EXPECT_EQ(BList[i].second, magic_ + flags + zeros_ + zeros_ + 
         BtcUtils::EmptyHash_ + BtcUtils::EmptyHash_ + ff);
   }

   iface_->closeDatabases();
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(LMDBTest, PutGetDelete)
{
   BinaryData flags = READHEX("97011000");
   BinaryData ff = READHEX("ffffffffffffffff");

   iface_->openDatabases(Pathing::dbDir());
   ASSERT_TRUE(iface_->databasesAreOpen());
   
   auto&& txh = iface_->beginTransaction(HEADERS, LMDB::ReadWrite);
   auto&& txH = iface_->beginTransaction(HISTORY, LMDB::ReadWrite);

   DB_PREFIX TXDATA = DB_PREFIX_TXDATA;
   BinaryData DBINFO = StoredDBInfo().getDBKey();
   BinaryData PREFIX = WRITE_UINT8_BE((uint8_t)TXDATA);
   BinaryData val0 = magic_ + flags + zeros_ + zeros_ +
      BtcUtils::EmptyHash_ + BtcUtils::EmptyHash_ + ff;

   BinaryData commonValue = READHEX("abcd1234");
   BinaryData keyAB = READHEX("0100");
   BinaryData nothing = READHEX("0000");

   addOutPairH(DBINFO,         val0);

   addOutPairB(DBINFO,         val0);
   addOutPairB(         keyAB, commonValue);
   addOutPairB(PREFIX + keyAB, commonValue);

   ASSERT_TRUE( compareKVListRange(0,1, 0,1));

   iface_->putValue(HISTORY, keyAB, commonValue);
   ASSERT_TRUE( compareKVListRange(0,1, 0,2));

   iface_->putValue(HISTORY, DB_PREFIX_TXDATA, keyAB, commonValue);
   ASSERT_TRUE( compareKVListRange(0,1, 0,3));

   // Now test a bunch of get* methods
   ASSERT_EQ(iface_->getValueNoCopy(HISTORY, PREFIX + keyAB), commonValue);
   ASSERT_EQ(iface_->getValueRef(   HISTORY, DB_PREFIX_DBINFO, nothing), val0);
   ASSERT_EQ(iface_->getValueNoCopy(HISTORY, DBINFO), val0);
   ASSERT_EQ(iface_->getValueNoCopy(HISTORY, PREFIX + keyAB), commonValue);
   ASSERT_EQ(iface_->getValueRef(   HISTORY, TXDATA, keyAB), commonValue);
   ASSERT_EQ(iface_->getValueReader(HISTORY, PREFIX + keyAB).getRawRef(), commonValue);
   ASSERT_EQ(iface_->getValueReader(HISTORY, TXDATA, keyAB).getRawRef(), commonValue);

   iface_->deleteValue(HISTORY, DB_PREFIX_TXDATA, keyAB);
   ASSERT_TRUE( compareKVListRange(0,1, 0,2));

   iface_->deleteValue(HISTORY, PREFIX + keyAB);
   ASSERT_TRUE( compareKVListRange(0,1, 0,1));

   iface_->deleteValue(HISTORY, PREFIX + keyAB);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(LMDBTest, DISABLED_STxOutPutGet)
{
   BinaryData TXP     = WRITE_UINT8_BE((uint8_t)DB_PREFIX_TXDATA);
   BinaryData stxoVal = READHEX("2420") + rawTxOut0_;
   BinaryData stxoKey = TXP + READHEX("01e078""0f""0007""0001");
   
   ASSERT_TRUE(standardOpenDBs());
   auto&& txh = iface_->beginTransaction(HEADERS, LMDB::ReadWrite);
   auto&& txH = iface_->beginTransaction(STXO, LMDB::ReadWrite);

   StoredTxOut stxo0;
   stxo0.txVersion_   = 1;
   stxo0.spentness_   = TXOUT_UNSPENT;
   stxo0.blockHeight_ = 123000;
   stxo0.duplicateID_ = 15;
   stxo0.txIndex_     = 7;
   stxo0.txOutIndex_  = 1;
   stxo0.unserialize(rawTxOut0_);
   iface_->putStoredTxOut(stxo0);

   // Construct expected output
   addOutPairB(stxoKey, stxoVal);
   ASSERT_TRUE(compareKVListRange(0,1, 0,2, STXO));

   StoredTxOut stxoGet;
   iface_->getStoredTxOut(stxoGet, 123000, 15, 7, 1);
   EXPECT_EQ(
      serializeDBValue(stxoGet),
      serializeDBValue(stxo0)
   );

   //iface_->validDupByHeight_[123000] = 15;
   //iface_->getStoredTxOut(stxoGet, 123000, 7, 1);
   //EXPECT_EQ(serializeDBValue(stxoGet), serializeDBValue(stxo0));
   
   StoredTxOut stxo1;
   stxo1.txVersion_   = 1;
   stxo1.spentness_   = TXOUT_UNSPENT;
   stxo1.blockHeight_ = 200333;
   stxo1.duplicateID_ = 3;
   stxo1.txIndex_     = 7;
   stxo1.txOutIndex_  = 1;
   stxo1.unserialize(rawTxOut1_);
   stxoVal = READHEX("2420") + rawTxOut1_;
   stxoKey = TXP + READHEX("030e8d""03""00070001");
   iface_->putStoredTxOut(stxo1);

   iface_->getStoredTxOut(stxoGet, 123000, 15, 7, 1);
   EXPECT_EQ(
      serializeDBValue(stxoGet),
      serializeDBValue(stxo0)
   );
   iface_->getStoredTxOut(stxoGet, 200333,  3, 7, 1);
   EXPECT_EQ(
      serializeDBValue(stxoGet),
      serializeDBValue(stxo1)
   );

   addOutPairB(stxoKey, stxoVal);
   ASSERT_TRUE(compareKVListRange(0,1, 0,3, STXO));

}

////////////////////////////////////////////////////////////////////////////////
TEST_F(LMDBTest, PutGetBareHeader)
{
   StoredHeader sbh;
   BinaryRefReader brr(rawBlock_);
   sbh.unserializeFullBlock(brr);
   sbh.setKeyData(123000, UINT8_MAX);
   BinaryData header0 = sbh.thisHash_;

   ASSERT_TRUE(standardOpenDBs());
   auto&& txh = iface_->beginTransaction(HEADERS, LMDB::ReadWrite);
   auto&& txH = iface_->beginTransaction(HISTORY, LMDB::ReadWrite);

   uint8_t sdup = iface_->putBareHeader(sbh);
   EXPECT_EQ(sdup, 0);
   EXPECT_EQ(sbh.duplicateID_, 0);

   // Add a new header and make sure duplicate ID is done correctly
   BinaryData newHeader = READHEX( 
      "0000000105d3571220ef5f87c6ac0bc8bf5b33c02a9e6edf83c84d840109592c"
      "0000000027523728e15f5fe1ac507bff92499eada4af8a0c485d5178e3f96568"
      "c18f84994e0e4efc1c0175d646a91ad4");
   BinaryData header1 = BtcUtils::getHash256(newHeader);

   StoredHeader sbh2;
   sbh2.setHeaderData(newHeader);
   sbh2.setKeyData(123000, UINT8_MAX);
   
   uint8_t newDup = iface_->putBareHeader(sbh2);
   EXPECT_EQ(newDup, 1);
   EXPECT_EQ(sbh2.duplicateID_, 1);
   
   // Now add a new, isMainBranch_ header
   StoredHeader sbh3;
   BinaryData anotherHead = READHEX(
      "010000001d8f4ec0443e1f19f305e488c1085c95de7cc3fd25e0d2c5bb5d0000"
      "000000009762547903d36881a86751f3f5049e23050113f779735ef82734ebf0"
      "b4450081d8c8c84db3936a1a334b035b");
   BinaryData header2 = BtcUtils::getHash256(anotherHead);

   sbh3.setHeaderData(anotherHead);
   sbh3.setKeyData(123000, UINT8_MAX);
   sbh3.isMainBranch_ = true;
   uint8_t anotherDup = iface_->putBareHeader(sbh3);
   EXPECT_EQ(anotherDup, 2);
   EXPECT_EQ(sbh3.duplicateID_, 2);
   EXPECT_EQ(iface_->getValidDupIDForHeight(123000), 0xFF);

   map<unsigned, uint8_t> dupIDs;
   dupIDs.insert(make_pair(sbh3.blockHeight_, sbh3.duplicateID_));
   iface_->setValidDupIDForHeight(dupIDs);
   
   // Now test getting bare headers
   StoredHeader sbh4;
   iface_->getBareHeader(sbh4, 123000);
   EXPECT_EQ(sbh4.thisHash_, header2);
   EXPECT_EQ(sbh4.duplicateID_, 2);
   
   iface_->getBareHeader(sbh4, 123000, 1);
   EXPECT_EQ(sbh4.thisHash_, header1);
   EXPECT_EQ(sbh4.duplicateID_, 1);

   // Re-add the same SBH3, make sure nothing changes
   iface_->putBareHeader(sbh3);
   EXPECT_EQ(sbh3.duplicateID_, 2);
   EXPECT_EQ(iface_->getValidDupIDForHeight(123000), 2);
}
////////////////////////////////////////////////////////////////////////////////
TEST_F(LMDBTest, PutGetStoredTxHints)
{
   ASSERT_TRUE(standardOpenDBs());
   auto&& tx = iface_->beginTransaction(TXHINTS, LMDB::ReadWrite);

   BinaryData prefix = READHEX("aabbccdd");

   StoredTxHints sths;
   EXPECT_FALSE(iface_->getStoredTxHints(sths, prefix));

   sths.txHashPrefix_ = prefix;
   
   ASSERT_TRUE(iface_->putStoredTxHints(sths));

   BinaryData THP = WRITE_UINT8_BE((uint8_t)DB_PREFIX_TXHINTS);
   addOutPairB(THP + prefix, READHEX("00"));

   compareKVListRange(0,1, 0,2, TXHINTS);
   
   /////
   sths.dbKeyList_.push_back(READHEX("abcd1234ffff"));
   replaceTopOutPairB(THP + prefix,  READHEX("01""abcd1234ffff"));
   EXPECT_TRUE(iface_->putStoredTxHints(sths));
   compareKVListRange(0,1, 0,2, TXHINTS);

   /////
   sths.dbKeyList_.push_back(READHEX("00002222aaaa"));
   replaceTopOutPairB(THP + prefix,  READHEX("02""abcd1234ffff""00002222aaaa"));
   EXPECT_TRUE(iface_->putStoredTxHints(sths));
   compareKVListRange(0,1, 0,2, TXHINTS);

   /////
   sths.preferredDBKey_ = READHEX("00002222aaaa");
   replaceTopOutPairB(THP + prefix,  READHEX("02""00002222aaaa""abcd1234ffff"));
   EXPECT_TRUE(iface_->putStoredTxHints(sths));
   compareKVListRange(0,1, 0,2, TXHINTS);

   // Now test the get methods
   EXPECT_TRUE( iface_->getStoredTxHints(sths, prefix));
   EXPECT_EQ(   sths.txHashPrefix_,  prefix);
   EXPECT_EQ(   sths.dbKeyList_.size(),  2ULL);
   EXPECT_EQ(   sths.preferredDBKey_, READHEX("00002222aaaa"));

   //
   sths.dbKeyList_.resize(0);
   sths.preferredDBKey_.resize(0);
   EXPECT_TRUE( iface_->putStoredTxHints(sths));
   EXPECT_TRUE( iface_->getStoredTxHints(sths, prefix));
   EXPECT_EQ(   sths.txHashPrefix_,  prefix);
   EXPECT_EQ(   sths.dbKeyList_.size(),  0ULL);
   EXPECT_EQ(   sths.preferredDBKey_.getSize(), 0ULL);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class TxRefTest : public ::testing::Test
{
protected:
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(TxRefTest, TxRefNoInit)
{
   TxRef txr;
   EXPECT_FALSE(txr.isInitialized());
   //EXPECT_FALSE(txr.isBound());

   EXPECT_EQ(txr.getDBKey(),     BinaryData(0));
   EXPECT_EQ(txr.getDBKeyRef(),  BinaryDataRef());
   //EXPECT_EQ(txr.getBlockTimestamp(), UINT32_MAX);
   EXPECT_EQ(txr.getBlockHeight(),    UINT32_MAX);
   EXPECT_EQ(txr.getDuplicateID(),    UINT8_MAX );
   EXPECT_EQ(txr.getBlockTxIndex(),   UINT16_MAX);
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(TxRefTest, TxRefKeyParts)
{
   TxRef txr;
   BinaryData    newKey = READHEX("e3c4027f000f");
   BinaryDataRef newRef(newKey);


   txr.setDBKey(newKey);
   EXPECT_EQ(txr.getDBKey(),    newKey);
   EXPECT_EQ(txr.getDBKeyRef(), newRef);

   EXPECT_EQ(txr.getBlockHeight(),  0xe3c402ULL);
   EXPECT_EQ(txr.getDuplicateID(),  127ULL);
   EXPECT_EQ(txr.getBlockTxIndex(), 15ULL);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class TestCryptoECDSA : public ::testing::Test
{
protected:
   /////////////////////////////////////////////////////////////////////////////
   virtual void SetUp(void)
   {
      verifyX = READHEX("39a36013301597daef41fbe593a02cc513d0b55527ec2df1050e2e8ff49c85c2");
      verifyY = READHEX("3cbe7ded0e7ce6a594896b8f62888fdbc5c8821305e2ea42bf01e37300116281");

      multScalarA = READHEX("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
      multScalarB = READHEX("483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8");
      multRes = READHEX("805714a252d0c0b58910907e85b5b801fff610a36bdf46847a4bf5d9ae2d10ed");

      multScalar = READHEX("04bfb2dd60fa8921c2a4085ec15507a921f49cdc839f27f0f280e9c1495d44b5");
      multPointX = READHEX("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
      multPointY = READHEX("483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8");
      multPointRes = READHEX("7f8bd85f90169a606b0b4323c70e5a12e8a89cbc76647b6ed6a39b4b53825214c590a32f111f857573cf8f2c85d969815e4dd35ae0dc9c7e868195c309b8bada");

      addAX = READHEX("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
      addAY = READHEX("483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8");
      addBX = READHEX("5a784662a4a20a65bf6aab9ae98a6c068a81c52e4b032c0fb5400c706cfccc56");
      addBY = READHEX("7f717885be239daadce76b568958305183ad616ff74ed4dc219a74c26d35f839");
      addRes = READHEX("fe2f7c8109d9ae628856d51a02ab25300a8757e088fc336d75cb8dc4cc2ce3339013be71e57c3abeee6ad158646df81d92f8c0778f88100eeb61535f9ff9776d");

      invAX = READHEX("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
      invAY = READHEX("483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8");
      invRes = READHEX("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798b7c52588d95c3b9aa25b0403f1eef75702e84bb7597aabe663b82f6f04ef2777");

      compPointPrv1 = READHEX("000f479245fb19a38a1954c5c7c0ebab2f9bdfd96a17563ef28a6a4b1a2a764ef4");
      compPointPub1 = READHEX("02e8445082a72f29b75ca48748a914df60622a609cacfce8ed0e35804560741d29");
      uncompPointPub1 = READHEX("04e8445082a72f29b75ca48748a914df60622a609cacfce8ed0e35804560741d292728ad8d58a140050c1016e21f285636a580f4d2711b7fac3957a594ddf416a0");

      compPointPrv2 = READHEX("00e8f32e723decf4051aefac8e2c93c9c5b214313817cdb01a1494b917c8436b35");
      compPointPub2 = READHEX("0339a36013301597daef41fbe593a02cc513d0b55527ec2df1050e2e8ff49c85c2");
      uncompPointPub2 = READHEX("0439a36013301597daef41fbe593a02cc513d0b55527ec2df1050e2e8ff49c85c23cbe7ded0e7ce6a594896b8f62888fdbc5c8821305e2ea42bf01e37300116281");

      invModRes = READHEX("000000000000000000000000000000000000000000000000000000000000006b");

      LOGDISABLESTDOUT();
   }


   /////////////////////////////////////////////////////////////////////////////
   virtual void TearDown(void)
   {
      CLEANUP_ALL_TIMERS();
   }


   SecureBinaryData verifyX;
   SecureBinaryData verifyY;

   SecureBinaryData multScalarA;
   SecureBinaryData multScalarB;
   SecureBinaryData multRes;

   SecureBinaryData multScalar;
   SecureBinaryData multPointX;
   SecureBinaryData multPointY;
   SecureBinaryData multPointRes;

   SecureBinaryData addAX;
   SecureBinaryData addAY;
   SecureBinaryData addBX;
   SecureBinaryData addBY;
   SecureBinaryData addRes;

   SecureBinaryData invAX;
   SecureBinaryData invAY;
   SecureBinaryData invRes;

   SecureBinaryData compPointPrv1;
   SecureBinaryData uncompPointPub1;
   SecureBinaryData compPointPub1;
   SecureBinaryData compPointPrv2;
   SecureBinaryData uncompPointPub2;
   SecureBinaryData compPointPub2;

   SecureBinaryData invModRes;
};

// Verify that a point known to be on the secp256k1 curve is recognized as such.
////////////////////////////////////////////////////////////////////////////////
TEST_F(TestCryptoECDSA, VerifySECP256K1Point)
{
   EXPECT_TRUE(CryptoECDSA().ECVerifyPoint(verifyX, verifyY));
}

// Verify that some public keys (compressed and uncompressed) are valid.
////////////////////////////////////////////////////////////////////////////////
TEST_F(TestCryptoECDSA, VerifyPubKeyValidity)
{
   EXPECT_TRUE(CryptoECDSA().VerifyPublicKeyValid(compPointPub1));
   EXPECT_TRUE(CryptoECDSA().VerifyPublicKeyValid(compPointPub2));
   EXPECT_TRUE(CryptoECDSA().VerifyPublicKeyValid(uncompPointPub1));
   EXPECT_TRUE(CryptoECDSA().VerifyPublicKeyValid(uncompPointPub2));
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class TestTxHashFilters : public ::testing::Test
{
protected:
   virtual void SetUp(void)
   {
      homedir_ = string("./fakehomedir");
      DBUtils::removeDirectory(homedir_);
      mkdir(homedir_);
      mkdir(homedir_ + "/databases");

      Armory::Config::parseArgs({
         "--datadir=./fakehomedir",
         "--offline" },
         Armory::Config::ProcessType::DB);

      iface_ = new LMDBBlockDatabase(nullptr, string());
   }

   virtual void TearDown(void)
   {
      iface_->closeDatabases();
      delete iface_;
      iface_ = NULL;

      DBUtils::removeDirectory(homedir_);
      Armory::Config::reset();

      CLEANUP_ALL_TIMERS();
   }

   bool standardOpenDBs(void)
   {
      iface_->openDatabases(Pathing::dbDir());
      return iface_->databasesAreOpen();
   }

   LMDBBlockDatabase* iface_;
   string homedir_;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(TestTxHashFilters, SerializeWriter)
{
   unsigned bucketCount = 10;
   unsigned hashCount = 10;
   ASSERT_TRUE(standardOpenDBs());

   map<uint32_t, list<BinaryData>> hashMap;

   {
      //build the pool
      TxFilterPoolWriter pool;
      map<uint32_t, BlockHashVector> bucketMap;
      for (unsigned i=0; i<bucketCount; i++)
      {
         BlockHashVector bucket(i);
         auto insertIt = hashMap.emplace(i, list<BinaryData>{});
         auto& hashList = insertIt.first->second;
         for (unsigned y=0; y<hashCount; y++)
         {
            auto hash = BtcUtils::fortuna_.generateRandom(32);
            bucket.update(hash);
            hashList.emplace_back(hash);
         }

         bucketMap.emplace(i, move(bucket));
      }

      pool.update(bucketMap);

      //write the pool
      iface_->putFilterPoolForFileNum(0, pool);
   }

   {
      //read the pool
      TxFilterPoolWriter pool(iface_->getFilterPoolDataRef(0));

      //append the pool
      map<uint32_t, BlockHashVector> bucketMap;
      for (unsigned i=bucketCount; i<bucketCount*2; i++)
      {
         BlockHashVector bucket(i);
         auto insertIt = hashMap.emplace(i, list<BinaryData>{});
         auto& hashList = insertIt.first->second;
         for (unsigned y=0; y<hashCount; y++)
         {
            auto hash = BtcUtils::fortuna_.generateRandom(32);
            bucket.update(hash);
            hashList.emplace_back(hash);
         }

         bucketMap.emplace(i, move(bucket));
      }

      pool.update(bucketMap);

      //write the pool again
      iface_->putFilterPoolForFileNum(0, pool);
   }

   //reconstruct serialized pool locally
   BinaryWriter bw;
   bw.put_uint32_t(bucketCount*2);

   for (const auto& it : hashMap)
   {
      auto size = 12 + it.second.size() * 4;
      bw.put_uint32_t(size);
      bw.put_uint32_t(it.first);
      bw.put_uint32_t(it.second.size());

      for (const auto& hash : it.second)
      {
         uint32_t shortHand;
         memcpy(&shortHand, hash.getPtr(), 4);
         bw.put_uint32_t(shortHand);
      }
   }

   //checked serialized data matches data on disk
   const auto& serData = bw.getData();
   auto poolDataRef = iface_->getFilterPoolDataRef(0);
   EXPECT_EQ(poolDataRef, serData.getRef());
}

////////////////////////////////////////////////////////////////////////////////
TEST_F(TestTxHashFilters, FilterALot)
{
   ASSERT_TRUE(standardOpenDBs());

   auto start = chrono::system_clock::now();
   unsigned poolSize = 100;
   unsigned bucketCount = 10000;
   unsigned poolCount = bucketCount / poolSize;
   unsigned hashCount = 30000;
   unsigned hashPerBlock = 1000;
   unsigned hashesPerBucket = hashCount/bucketCount;

   //create 30MIL hashes in 10000 buckets of 3000 hashes each,
   //save in pools on disk
   map<BinaryData, pair<uint32_t, uint32_t>> hashes;
   map<BinaryData, pair<uint32_t, uint32_t>> hashes5k;
   map<BinaryData, pair<uint32_t, uint32_t>> hashes1k;
   map<BinaryData, pair<uint32_t, uint32_t>> hashes100;

   auto counter = make_shared<atomic<uint32_t>>();
   counter->store(0, memory_order_relaxed);

   //worker lambda
   auto createHashes = [this, &poolCount, &hashesPerBucket,
      &poolSize, &hashPerBlock, counter]()->
      map<BinaryData, pair<uint32_t, uint32_t>>
   {
      map<uint32_t, TxFilterPoolWriter> pools;
      map<BinaryData, pair<uint32_t, uint32_t>> hashes;

      while (true)
      {
         auto poolId = counter->fetch_add(1, memory_order_relaxed);
         if (poolId >= poolCount)
            break;

         map<unsigned, BlockHashVector> filters;

         for (unsigned i=0; i<poolSize; i++)
         {
            map<BinaryData, pair<uint32_t, uint32_t>> localHashes;
            uint32_t bucketId = poolId * poolSize + i;

            BlockHashVector bucket(bucketId);
            bucket.reserve(hashPerBlock);
            for (unsigned y=0; y<hashPerBlock; y++)
            {
               auto hash = BtcUtils::fortuna_.generateRandom(32);
               bucket.update(hash);

               if (hash.getPtr()[y%32] < 10 &&
                  localHashes.size() < hashesPerBucket)
               {
                  localHashes.emplace(hash, make_pair(bucketId, y));
               }
            }

            filters.emplace(bucketId, move(bucket));
            hashes.insert(localHashes.begin(), localHashes.end());
         }
         
         TxFilterPoolWriter filterPool;
         filterPool.update(filters);
         EXPECT_TRUE(filterPool.isValid());

         pools.emplace(poolId, move(filterPool));
      }

      //write pools to disk
      auto tx = iface_->beginTransaction(TXFILTERS, LMDB::ReadWrite);
      for (const auto& pool : pools)
         iface_->putFilterPoolForFileNum(pool.first, pool.second);

      return hashes;
   };

   {
      auto mutexPtr = make_shared<mutex>();
      auto worker = [&createHashes, &hashes, mutexPtr]()
      {
         auto poolHashes = createHashes();

         auto lock = unique_lock<mutex>(*mutexPtr);
         hashes.insert(poolHashes.begin(), poolHashes.end());
      };

      //start the worker threads
      vector<thread> threads;
      for (unsigned i=1; i<thread::hardware_concurrency()/2; i++)
         threads.emplace_back(thread(worker));
      worker();

      //join on them
      for (auto& thr : threads)
         thr.join();
   }

   //setup the hash maps
   for (const auto& hash : hashes)
   {
      if (hashes100.size() < 100)
         hashes100.insert(hash);

      if (hashes1k.size() < 1000)
         hashes1k.insert(hash);

      if (hashes5k.size() == 5000)
         break;

      hashes5k.insert(hash);
   }

   EXPECT_EQ(hashes.size(), hashCount);
   EXPECT_EQ(hashes5k.size(), 5000u);
   EXPECT_EQ(hashes1k.size(), 1000u);
   EXPECT_EQ(hashes100.size(), 100u);

   auto stop = chrono::system_clock::now();
   auto duration = chrono::duration_cast<chrono::milliseconds>(stop - start);
   std::cout << "--- setup in " << duration.count() << " ms ---" << std::endl;
   std::cout << "--- running with " << hashes.size() << " hashes" << std::endl;

   //search vector
   auto searchPoolVec = [](const TxFilterPoolReader& pool,
      const map<BinaryData, pair<uint32_t, uint32_t>>& hashes)
      ->set<BinaryDataRef>
   {
      set<BinaryDataRef> hits;
      for (const auto& hashIt : hashes)
      {
         auto result = pool.compare(hashIt.first);
         auto resultIt = result.find(hashIt.second.first);
         if (resultIt != result.end())
         {
            auto txidIt = resultIt->second.find(hashIt.second.second);
            if (txidIt != resultIt->second.end())
            {
               hits.emplace(hashIt.first.getRef());
               continue;
            }
         }
      }

      return hits;
   };

   //search map
   auto searchPoolMap = [](const TxFilterPoolReader& pool,
      const map<BinaryData, pair<uint32_t, uint32_t>>& hashes)
      ->set<BinaryDataRef>
   {
      set<BinaryDataRef> hits;
      for (const auto& hashIt : hashes)
      {
         auto result = pool.compare(hashIt.first);
         auto resultIt = result.find(hashIt.second.first);
         if (resultIt != result.end())
         {
            auto txidIt = resultIt->second.find(hashIt.second.second);
            if (txidIt != resultIt->second.end())
            {
               hits.emplace(hashIt.first.getRef());
               continue;
            }
         }
      }

      return hits;
   };

   //load pools as vectors & search
   {
      std::cout << std::endl;
      start = chrono::system_clock::now();
      map<unsigned, TxFilterPoolReader> vectorPools;
      for (unsigned i=0; i<poolCount; i++)
      {
         TxFilterPoolReader pool(iface_->getFilterPoolDataRef(i),
            TxFilterPoolMode::Bucket_Vector);

         vectorPools.emplace(i, move(pool));
      }

      stop = chrono::system_clock::now();
      duration = chrono::duration_cast<chrono::milliseconds>(stop - start);
      std::cout << "1. loaded bucket vector in " << duration.count() << " ms" << std::endl;

      //search
      auto search = [&searchPoolVec, &vectorPools](
         const map<BinaryData, pair<uint32_t, uint32_t>>& hashes)
      {
         auto start = chrono::system_clock::now();

         set<BinaryDataRef> foundHashes;
         for (const auto& pool : vectorPools)
         {
            auto hits = searchPoolVec(pool.second, hashes);
            foundHashes.insert(hits.begin(), hits.end());
         }
         EXPECT_EQ(foundHashes.size(), hashes.size());

         auto stop = chrono::system_clock::now();
         auto duration = chrono::duration_cast<chrono::milliseconds>(stop - start);
         std::cout << "1. filtered vector (" << hashes.size() << ") in " <<
            duration.count() << " ms" << std::endl;
      };

      //search(hashes);
      //search(hashes5k);
      search(hashes1k);
      search(hashes100);
   }

   //load pools as maps (mode 1) & search
   {
      std::cout << std::endl;
      start = chrono::system_clock::now();
      map<unsigned, TxFilterPoolReader> mapPools;
      for (unsigned i=0; i<poolCount; i++)
      {
         TxFilterPoolReader pool(iface_->getFilterPoolDataRef(i),
            TxFilterPoolMode::Bucket_Map);

         mapPools.emplace(i, move(pool));
      }

      stop = chrono::system_clock::now();
      duration = chrono::duration_cast<chrono::milliseconds>(stop - start);
      std::cout << "2. loaded bucket maps in " << duration.count() << " ms" << std::endl;

      //search the pool map
      auto search = [&searchPoolMap, &mapPools](
         const map<BinaryData, pair<uint32_t, uint32_t>>& hashes)
      {
         auto start = chrono::system_clock::now();

         set<BinaryDataRef> foundHashes;
         for (const auto& pool : mapPools)
         {
            auto hits = searchPoolMap(pool.second, hashes);
            foundHashes.insert(hits.begin(), hits.end());
         }
         EXPECT_EQ(foundHashes.size(), hashes.size());

         auto stop = chrono::system_clock::now();
         auto duration = chrono::duration_cast<chrono::milliseconds>(stop - start);
         std::cout << "2. filtered map (" << hashes.size() << ") in " <<
            duration.count() << " ms" << std::endl;
      };

      search(hashes);
      search(hashes5k);
      search(hashes1k);
      search(hashes100);
   }

   //load pools as maps (mode 2) & search
   {
      std::cout << std::endl;
      start = chrono::system_clock::now();
      map<unsigned, TxFilterPoolReader> mapPools;
      for (unsigned i=0; i<poolCount; i++)
      {
         TxFilterPoolReader pool(iface_->getFilterPoolDataRef(i),
            TxFilterPoolMode::Pool_Map);

         mapPools.emplace(i, move(pool));
      }

      stop = chrono::system_clock::now();
      duration = chrono::duration_cast<chrono::milliseconds>(stop - start);
      std::cout << "3. loaded pool maps in " << duration.count() << " ms" << std::endl;

      //search the pool map
      auto search = [&searchPoolMap, &mapPools](
         const map<BinaryData, pair<uint32_t, uint32_t>>& hashes)
      {
         auto start = chrono::system_clock::now();

         set<BinaryDataRef> foundHashes;
         for (const auto& pool : mapPools)
         {
            auto hits = searchPoolMap(pool.second, hashes);
            foundHashes.insert(hits.begin(), hits.end());
         }
         EXPECT_EQ(foundHashes.size(), hashes.size());

         auto stop = chrono::system_clock::now();
         auto duration = chrono::duration_cast<chrono::milliseconds>(stop - start);
         std::cout << "3. filtered map (" << hashes.size() << ") in " <<
            duration.count() << " ms" << std::endl;
      };

      search(hashes);
      search(hashes5k);
      search(hashes1k);
      search(hashes100);
   }

   //search via multithreaded func
   {
      auto search = [this, &poolCount](
         const map<BinaryData, pair<uint32_t, uint32_t>>& hashes,
         TxFilterPoolMode mode)
      {
         auto start = chrono::system_clock::now();

         set<BinaryData> hashSet;
         for (const auto& hash : hashes)
            hashSet.emplace(hash.first);

         auto fetchFunc = [this](uint32_t fileID)->BinaryDataRef
         {
            return this->iface_->getFilterPoolDataRef(fileID);
         };
         auto filterResult = TxFilterPoolReader::scanHashes(
            poolCount, fetchFunc, hashSet, mode);

         auto hashesCopy = hashes;
         auto hashIt = hashesCopy.begin();
         while (hashIt != hashesCopy.end())
         {
            bool found = false;
            for (const auto& resultIt : filterResult)
            {
               auto filterIter = resultIt.second.find(hashIt->first);
               if (filterIter == resultIt.second.end())
                  continue;

               auto blockIter = filterIter->filterHits_.find(hashIt->second.first);
               if (blockIter == filterIter->filterHits_.end())
                  continue;

               auto txIter = blockIter->second.find(hashIt->second.second);
               if (txIter != blockIter->second.end())
               {
                  found = true;
                  break;
               }
            }

            if (found)
            {
               hashesCopy.erase(hashIt++);
               continue;
            }

            ++hashIt;
         }

         EXPECT_TRUE(hashesCopy.empty());

         auto stop = chrono::system_clock::now();
         auto duration = chrono::duration_cast<chrono::milliseconds>(stop - start);
         std::cout << ". filtered map (" << hashes.size() <<
            ", " << (int)mode << ") in " <<
            duration.count() << " ms" << std::endl;
      };

      std::cout << std::endl;
      search(hashes1k, TxFilterPoolMode::Bucket_Vector);
      search(hashes100, TxFilterPoolMode::Bucket_Vector);

      std::cout << std::endl;
      search(hashes, TxFilterPoolMode::Bucket_Map);
      search(hashes5k, TxFilterPoolMode::Bucket_Map);
      search(hashes1k, TxFilterPoolMode::Bucket_Map);
      search(hashes100, TxFilterPoolMode::Bucket_Map);

      std::cout << std::endl;
      search(hashes, TxFilterPoolMode::Pool_Map);
      search(hashes5k, TxFilterPoolMode::Pool_Map);
      search(hashes1k, TxFilterPoolMode::Pool_Map);
      search(hashes100, TxFilterPoolMode::Pool_Map);

      std::cout << std::endl;
      search(hashes, TxFilterPoolMode::Auto);
      search(hashes5k, TxFilterPoolMode::Auto);
      search(hashes1k, TxFilterPoolMode::Auto);
      search(hashes100, TxFilterPoolMode::Auto);
   }
}

////////////////////////////////////////////////////////////////////////////////
class KdfTests : public ::testing::Test
{
protected:
   virtual void SetUp(void)
   {}
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(KdfTests, Romix)
{
   uint32_t targetUnlock_ms = 2000;

   //create a KDF object
   KeyDerivationFunction_Romix kdfRom(targetUnlock_ms);
   EXPECT_GE(kdfRom.memTarget(), 8092);

   //derive key with it, check it takes over 2sec
   auto keyToDerive = SecureBinaryData::fromString("0123456789AB");
   {
      auto start = chrono::system_clock::now();
      auto derivedKey = kdfRom.deriveKey(keyToDerive);
      auto end = chrono::system_clock::now();

      EXPECT_NE(derivedKey, keyToDerive);
      auto diff = chrono::duration_cast<chrono::milliseconds>(end - start);
      EXPECT_GE(diff.count(), targetUnlock_ms);
      EXPECT_LE(diff.count(), targetUnlock_ms + 1000);
   }
   kdfRom.prettyPrint();

   //deser kdf object into a copy, check it load correctly
   auto serializedKdf = kdfRom.serialize();
   auto kdfCopy = KeyDerivationFunction::deserialize(serializedKdf);
   auto kdfRom2 = dynamic_pointer_cast<KeyDerivationFunction_Romix>(kdfCopy);
   ASSERT_NE(kdfRom2, nullptr);
   EXPECT_TRUE(kdfRom.isSame(kdfRom2.get()));

   //derive key with kdf copy, check it takes over 2sec
   {
      auto start = chrono::system_clock::now();
      auto derivedKey = kdfRom2->deriveKey(keyToDerive);
      auto end = chrono::system_clock::now();

      EXPECT_NE(derivedKey, keyToDerive);
      auto diff = chrono::duration_cast<chrono::milliseconds>(end - start);
      EXPECT_GE(diff.count(), targetUnlock_ms);
      EXPECT_LE(diff.count(), targetUnlock_ms + 1000);
   }
   kdfRom2->prettyPrint();

   //create kdf with same params but its own salt
   KeyDerivationFunction_Romix kdfRom3(
      kdfRom.iterations(),
      kdfRom.memTarget(),
      CryptoPRNG::generateRandom(32)
   );

   //derive key with 3rd kdf, check it takes over 2sec
   {
      auto start = chrono::system_clock::now();
      auto derivedKey = kdfRom3.deriveKey(keyToDerive);
      auto end = chrono::system_clock::now();

      EXPECT_NE(derivedKey, keyToDerive);
      auto diff = chrono::duration_cast<chrono::milliseconds>(end - start);
      EXPECT_GE(diff.count(), targetUnlock_ms);
      EXPECT_LE(diff.count(), targetUnlock_ms + 1000);
   }
   kdfRom3.prettyPrint();
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
