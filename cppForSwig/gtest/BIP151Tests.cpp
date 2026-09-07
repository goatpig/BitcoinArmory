////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <stdexcept>
#include <gtest/gtest.h>

#ifdef _WIN32
   #include <winsock2.h>
#endif

#include <Utils/BIP150_151.h>
#include <Utils/FileUtils.h>
#include <Utils/varint.h>
#include <Utils/Cryptography.h>
#include <Wallets/AuthorizedPeers.h>

using namespace Armory;
using namespace Armory::NetworkPeers;
using namespace std::string_view_literals;

class BIP151Message
{
private:
   BinaryData cmd_;
   BinaryData payload_;

public:
   BIP151Message(uint8_t* plaintextData, uint32_t plaintextDataSize)
   { setEncStruct(plaintextData, plaintextDataSize); }

   BIP151Message(const uint8_t* inCmd, size_t inCmdSize,
      const uint8_t* inPayload, size_t inPayloadSize)
   { setEncStructData(inCmd, inCmdSize, inPayload, inPayloadSize); }

   ////////
   void setEncStructData(const uint8_t* inCmd, size_t inCmdSize,
      const uint8_t* inPayload, size_t inPayloadSize)
   {
      cmd_.copyFrom(inCmd, inCmdSize);
      payload_.copyFrom(inPayload, inPayloadSize);
   }

   int setEncStruct(uint8_t* plaintextData,
      const uint32_t& plaintextDataSize)
   {
      BinaryReader inData(plaintextData, plaintextDataSize);

      // Do some basic sanity checking before proceeding.
      uint32_t msgSize = inData.get_uint32_t();
      if (msgSize != inData.getSizeRemaining()) {
         LOGERR << "BIP 151 - Incoming message size (" << msgSize << ") does not "
            << "match the data buffer size (" << inData.getSizeRemaining() << ").";
         return -1;
      }

      // uint64_t -> uint32_t is safe in this case. The spec disallows >4GB msgs.
      uint8_t cmdSize = inData.get_uint8_t();
      inData.get_BinaryData(cmd_, static_cast<uint32_t>(cmdSize));
      uint64_t payloadSize = inData.get_var_int();
      inData.get_BinaryData(payload_, static_cast<uint32_t>(payloadSize));
      return 0;
   }

   ////////
   void getEncStructMsg(uint8_t* outStruct, size_t outStructSize,
      size_t& finalStructSize)
   {
      assert(outStructSize >= messageSizeHint());

      size_t writerSize = messageSizeHint() - 4;
      BinaryWriter payloadWriter(writerSize);
      payloadWriter.put_var_int(cmd_.getSize());
      payloadWriter.put_BinaryData(cmd_);
      payloadWriter.put_uint32_t(payload_.getSize());
      payloadWriter.put_BinaryData(payload_);

      // Write a second, final buffer.
      finalStructSize = payloadWriter.getSize() + 4;
      BinaryWriter finalStruct(finalStructSize);
      finalStruct.put_uint32_t(payloadWriter.getSize());
      finalStruct.put_BinaryData(payloadWriter.getData());

      std::copy(finalStruct.getData().getPtr(),
         finalStruct.getData().getPtr() + finalStructSize,
         outStruct);
   }

   ////////
   void getCmd(uint8_t* cmdBuf, size_t cmdBufSize)
   {
      assert(cmd_.getSize() <= cmdBufSize);
      std::copy(cmd_.getPtr(), cmd_.getPtr() + cmd_.getSize(), cmdBuf);
   }

   size_t getCmdSize() const { return cmd_.getSize(); }
   const uint8_t* getCmdPtr() const { return cmd_.getPtr(); }
   const BinaryData& getPayload(void) const { return payload_; }
   size_t getPayloadSize() const { return payload_.getSize(); }
   const uint8_t* getPayloadPtr() const { return payload_.getPtr(); }
   size_t messageSizeHint() const
   {
      // Hint: Operand order is the same order as what's found in the struct.
      return 8 + BtcUtils::calcVarIntSize(cmd_.getSize()) +
         cmd_.getSize() + payload_.getSize();
   }
};

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
      std::string prvKeyClientIn_hexstr = "299ecf12fa716a9891903f05d2d22f483468c10f35cc448f5745e4ba00530e65";
      std::string prvKeyClientOut_hexstr = "31bb6f8dad3b2f3c76671f06cbe47ac634c47e9a6bd0f3c66e0bb6f85fbdd88c";
      std::string prvKeyServerIn_hexstr = "0e5e3671e90368ed865e9057ebb8cdbd0ffdaf8099bd0eb2414879f18eafacf6";
      std::string prvKeyServerOut_hexstr = "19a0eead9ae1d0167c6c4293a5a02de1712111f04007ae0587e0d978bb3b5010";
      std::string pubKeyClientIn_hexstr = "03c08a4e5a66478c65f7630162a64648dd1593e6588185ec0086e8c781398526b3";
      std::string pubKeyClientOut_hexstr = "0229fc11de5fe2a3b3a062a5ee6eb2e86aabb680a47128044cc1f4e92729dd8921";
      std::string pubKeyServerIn_hexstr = "0389cce55a124fc6de5689e23c6d64a5bb37f1a847d32a1afcdbd0e96cbb98a983";
      std::string pubKeyServerOut_hexstr = "02d786668c8fc58b8af96dd2567c857a4a83a76101429e3852d12c020a668c38cd";
      std::string ecdhCliInSrvOut_hexstr = "773d49e34bd65977b50b3f6b76a8236265fb489262d0cf3053f9152340646f00";
      std::string ecdhCliOutSrvIn_hexstr = "de3b244a80465b59d97f05eebb1af93eda0a667d5f0f2bc0dfa18d65d6e0c8a9";
      std::string k1CliInSrvOut_hexstr = "ae26351affd46a861890022eb60a4ebbfbca280e5eae425fa37dcf4406354d89";
      std::string k1CliOutSrvIn_hexstr = "eeaddf673bb62fa8e8a453e7aec56c8b50c03c5ff9c329319ae81f9b72be32ba";
      std::string k2CliInSrvOut_hexstr = "b70b3576c46477df45e8a7e8ffbd4aa2028f70c439ffb1c9f3040e20c5886d4f";
      std::string k2CliOutSrvIn_hexstr = "76773a0121079bfcf1fbf73a8476fc1861952b80d3e2a1e41dc8ba4e84f636be";
      std::string sesIDCliInSrvOut_hexstr = "71c425ce376162eb29e91744fbc1cbd86af52aad77490758382022bb0347585b";
      std::string sesIDCliOutSrvIn_hexstr = "ae60eb91ea2ea8cef36df26e4ab8c6cd609946ba6fd545adc21e4215af983d7d";
      std::string command_hexstr = "fake";
      std::string payload_hexstr = "deadbeef";
      std::string msg_hexstr = "0d0000000466616b6504000000deadbeef";
      std::string cliOutMsg1_hexstr = "8c7b743fc456d2f4c7cbb18ebb697ddfdb8308b29b9031fba2c50c5d160ec77bc0";
      std::string srvInMsg1_hexstr = "0d0000000466616b6504000000deadbeef";
      std::string cliOutMsg2_hexstr = "d5ce6ff902fa2936c8518ed503857134d7a062afe4c5868fd832188b8a5d84e576";
      std::string srvInMsg2_hexstr = "0d0000000466616b6504000000deadbeef";
      std::string cliOutMsg3_hexstr = "08c2b3592f53197bf1e81df1f2d36dadca27470f4f422e583e2f4ce32cd9719f1ac5a3a8e3e5a0c5f47e60cbdc81f314d030a545c31d9b632ab4e8740f756c00";
      std::string srvInMsg3_hexstr = "2c00000006656e6361636b21000000000000000000000000000000000000000000000000000000000000000000000000";
      std::string cliOutMsg4_hexstr = "c9056ffa96174f92a59e6aedc16af8a1fc394fe3a8c2639404e0dc700e5a58681c";
      std::string srvInMsg4_hexstr = "0d0000000466616b6504000000deadbeef";
      std::string srvOutMsg1_hexstr = "754bd639b31487e6e775fd336acf9cb2790323f4355ffc2cf17fcb2c6827d30a7a";
      std::string cliInMsg1_hexstr = "0d0000000466616b6504000000deadbeef";
      std::string srvOutMsg2_hexstr = "63c9868c88c78b7cdc30f9a23f1f7f8bbe2dec215a38df518c6880bf51ce11a35a";
      std::string cliInMsg2_hexstr = "0d0000000466616b6504000000deadbeef";
      std::string srvOutMsg3_hexstr = "367951da70abdc072956680a17fed98c54d4cd5fabc401576cbdce7a3e1b1bfd236152b4e55a1a9ff732f98b2b874477a25eeaf3264c0af42932c2eada06c5ab";
      std::string cliInMsg3_hexstr = "2c00000006656e6361636b21000000000000000000000000000000000000000000000000000000000000000000000000";
      std::string srvOutMsg4_hexstr = "39a790b8cc3bf027faf69622edc9ec1bfebce172d96c5bb52fc8a5f89df309f8a5";
      std::string cliInMsg4_hexstr = "0d0000000466616b6504000000deadbeef";

      // BIP 150
      std::string authchallenge1_hexstr = "68f35d94aacf218f8d73f4fcc82ab26f39af051c9fcf9af261eab8080bea6685";
      std::string authreply1_hexstr = "8144df9803527f833c9a628926fe99de04b15942d0d44e52d73dcdeb8c3d43412b26c1729405445bec9e35216b03a79cc51bb102cc351314fbb5a027298d3546";
      std::string authpropose_hexstr = "bde8e33de5a6b60651b82e2337112aebca11d351f84d9c027c7013f75701682b";
      std::string authpropose_1way_hexstr = "e42d5a3eec12c1b57e975ae877abd5a36ba84a7dd84eb7bda97b229ffdab5ef2";
      std::string authchallenge2_hexstr = "653f05a5e12a40579c8d9c782e04f3fff22c61888b8d67d7f783b1259cbf26cc";
      std::string authchallenge2_1way_hexstr = "2a9de34d8af544687a58b59e45d4007b1bf54643549343616f7f1281108913a5";
      std::string authreply2_hexstr = "0299a6086ab60af5fc4b5ccfa08d71c996cf0099a3ebb779cc42c94cfe3926294cf9505fd3835f73dcf88d114ed6c7e8956c8dec999617bb2b8b9a340c1eee22";

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

      baseDir_ = "./input_files";
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
   std::string cli150Fingerprint;

   std::string baseDir_;
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
   btc_key prvKeyCliIn;
   btc_key prvKeyCliOut;
   btc_key prvKeySrvIn;
   btc_key prvKeySrvOut;
   prvKeyClientIn.copyTo(prvKeyCliIn.privkey);
   prvKeyClientOut.copyTo(prvKeyCliOut.privkey);
   prvKeyServerIn.copyTo(prvKeySrvIn.privkey);
   prvKeyServerOut.copyTo(prvKeySrvOut.privkey);
   BIP151Connection cliCon(&prvKeyCliIn, &prvKeyCliOut,
      std::make_unique<PeerStoreView>(nullptr, std::make_shared<SecureBinaryData>()), false);
   BIP151Connection srvCon(&prvKeySrvIn, &prvKeySrvOut,
      std::make_unique<PeerStoreView>(nullptr, std::make_shared<SecureBinaryData>()), false);

   // Set up encinit/encack directly. (Initial encinit/encack will use regular
   // Bitcoin P2P messages, which we'll skip building.) Confirm all steps
   // function properly along the way.
   BinaryData cliInEncinitCliData; cliInEncinitCliData.resize(ENCINITMSGSIZE);   // SRV (Out) -> CLI (In)
   BinaryData cliInEncackCliData; cliInEncackCliData.resize(BIP151PUBKEYSIZE);  // CLI (In)  -> SRV (Out)
   BinaryData cliOutEncinitCliData; cliOutEncinitCliData.resize(ENCINITMSGSIZE);  // CLI (Out) -> SRV (In)
   BinaryData cliOutEncackCliData; cliOutEncackCliData.resize(BIP151PUBKEYSIZE); // SRV (In)  -> CLI (Out)
   int s1 = srvCon.getEncinitData(cliInEncinitCliData, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s1);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s2 = cliCon.processEncinit(cliInEncinitCliData, false);
   EXPECT_EQ(0, s2);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s3 = cliCon.getEncackData(cliInEncackCliData);
   EXPECT_EQ(0, s3);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s4 = srvCon.processEncack(cliInEncackCliData, true);
   EXPECT_EQ(0, s4);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s5 = cliCon.getEncinitData(cliOutEncinitCliData, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s5);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s6 = srvCon.processEncinit(cliOutEncinitCliData, false);
   EXPECT_EQ(0, s6);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s7 = srvCon.getEncackData(cliOutEncackCliData);
   EXPECT_EQ(0, s7);
   EXPECT_TRUE(srvCon.connectionComplete());
   int s8 = cliCon.processEncack(cliOutEncackCliData, true);
   EXPECT_EQ(0, s8);
   EXPECT_TRUE(cliCon.connectionComplete());

   // Check the encinit/encack data the client sends on its outbound session.
   BinaryData expectedCliEncinitData; expectedCliEncinitData.resize(34);
   std::copy(pubKeyClientOut.getPtr(),
      pubKeyClientOut.getPtr() + 33,
      expectedCliEncinitData.getPtr());
   expectedCliEncinitData[BIP151PUBKEYSIZE] =
      static_cast<uint8_t>(BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(pubKeyClientIn, cliInEncackCliData);
   EXPECT_EQ(expectedCliEncinitData, cliOutEncinitCliData);

   // Check the encinit/encack data the server sends on its outbound session.
   BinaryData expectedSrvEncinitData; expectedSrvEncinitData.resize(34);
   std::copy(pubKeyServerOut.getPtr(),
      pubKeyServerOut.getPtr() + 33,
      expectedSrvEncinitData.getPtr());
   expectedSrvEncinitData[BIP151PUBKEYSIZE] =
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
   auto&& cmd = BinaryData::fromString("fake"sv);
   std::array<uint8_t, 4> payload = {0xde, 0xad, 0xbe, 0xef};
   BinaryData testMsgData; testMsgData.resize(50);
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
   BinaryData encMsgBuffer; encMsgBuffer.resize(testMsgData.getSize() + 16);
   int encryptRes = cliCon.assemblePacket(testMsgData.getRef(),
      encMsgBuffer.getPtr(), encMsgBuffer.getSize());
   EXPECT_EQ(0, encryptRes);
   EXPECT_EQ(cliOutMsg1, encMsgBuffer);
   BinaryData decMsgBuffer; decMsgBuffer.resize(testMsgData.getSize());
   int decryptRes = srvCon.decryptPacket(encMsgBuffer.getRef(), decMsgBuffer);
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(srvInMsg1, decMsgBuffer);

   // Encrypt and decrypt the second CLI -> SRV packet.
   encMsgBuffer.resize(testMsgData.getSize() + 16);
   encryptRes = cliCon.assemblePacket(testMsgData.getRef(),
      encMsgBuffer.getPtr(), encMsgBuffer.getSize());
   EXPECT_EQ(0, encryptRes);
   EXPECT_EQ(cliOutMsg2, encMsgBuffer);

   decMsgBuffer.resize(testMsgData.getSize());
   decryptRes = srvCon.decryptPacket(encMsgBuffer.getRef(), decMsgBuffer);
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(srvInMsg2, decMsgBuffer);

   // Rekey (CLI -> SRV) and confirm that the results are correct.
   BinaryData rekeyBuf; rekeyBuf.resize(64);
   int rekeySendRes = cliCon.bip151RekeyConn(rekeyBuf);
   EXPECT_EQ(0, rekeySendRes);
   EXPECT_EQ(cliOutMsg3, rekeyBuf);
   decMsgBuffer.resize(rekeyBuf.getSize() - 16);
   decryptRes = srvCon.decryptPacket(rekeyBuf.getRef(), decMsgBuffer);
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(srvInMsg3, decMsgBuffer);
   BIP151Message decData1(decMsgBuffer.getPtr(), decMsgBuffer.getSize());
   int rekeyProcRes = srvCon.processEncack(decData1.getPayload().getRef(), false);
   EXPECT_EQ(0, rekeyProcRes);

   // Encrypt and decrypt the third CLI -> SRV packet.
   encMsgBuffer.resize(testMsgData.getSize() + 16);
   encryptRes = cliCon.assemblePacket(testMsgData.getRef(),
      encMsgBuffer.getPtr(), encMsgBuffer.getSize());
   EXPECT_EQ(0, encryptRes);
   EXPECT_EQ(cliOutMsg4, encMsgBuffer);
   decMsgBuffer.resize(testMsgData.getSize());
   decryptRes = srvCon.decryptPacket(encMsgBuffer.getRef(), decMsgBuffer);
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(srvInMsg4, decMsgBuffer);

   // Encrypt and decrypt the first SRV -> CLI packet.
   encMsgBuffer.resize(testMsgData.getSize() + 16);
   encryptRes = srvCon.assemblePacket(testMsgData.getRef(),
      encMsgBuffer.getPtr(), encMsgBuffer.getSize());
   EXPECT_EQ(0, encryptRes);
   EXPECT_EQ(srvOutMsg1, encMsgBuffer);

   decMsgBuffer.resize(testMsgData.getSize());
   decryptRes = cliCon.decryptPacket(encMsgBuffer.getRef(), decMsgBuffer);
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(cliInMsg1, decMsgBuffer);

   // Encrypt and decrypt the second SRV -> CLI packet.
   encMsgBuffer.resize(testMsgData.getSize() + 16);
   encryptRes = srvCon.assemblePacket(testMsgData.getRef(),
      encMsgBuffer.getPtr(), encMsgBuffer.getSize());
   EXPECT_EQ(0, encryptRes);
   EXPECT_EQ(srvOutMsg2, encMsgBuffer);

   decMsgBuffer.resize(testMsgData.getSize());
   decryptRes = cliCon.decryptPacket(encMsgBuffer.getRef(), decMsgBuffer);
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(cliInMsg2, decMsgBuffer);

   // Rekey (CLI -> SRV) and confirm that the results are correct.
   rekeySendRes = srvCon.bip151RekeyConn(rekeyBuf);
   EXPECT_EQ(0, rekeySendRes);
   EXPECT_EQ(srvOutMsg3, rekeyBuf);
   decMsgBuffer.resize(rekeyBuf.getSize() - 16);
   decryptRes = cliCon.decryptPacket(rekeyBuf.getRef(), decMsgBuffer);
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(cliInMsg3, decMsgBuffer);
   BIP151Message decData2(decMsgBuffer.getPtr(), decMsgBuffer.getSize());
   rekeyProcRes = cliCon.processEncack(decData2.getPayload().getRef(), false);
   EXPECT_EQ(0, rekeyProcRes);

   // Encrypt and decrypt the third SRV -> CLI packet.
   encMsgBuffer.resize(testMsgData.getSize() + 16);
   encryptRes = cliCon.assemblePacket(testMsgData.getRef(),
      encMsgBuffer.getPtr(), encMsgBuffer.getSize());
   EXPECT_EQ(0, encryptRes);
   EXPECT_EQ(srvOutMsg4, encMsgBuffer);

   decMsgBuffer.resize(testMsgData.getSize());
   decryptRes = srvCon.decryptPacket(encMsgBuffer.getRef(), decMsgBuffer);
   EXPECT_EQ(0, decryptRes);
   EXPECT_EQ(cliInMsg4, decMsgBuffer);
}

////////////////////////////////////////////////////////////////////////////////
// Test BIP 150 and BIP 151. Establish a 151 connection first and then confirm
// that BIP 150 functions properly, with a quick check to confirm that 151 is
// still functional afterwards.
TEST_F(BIP150_151Test, checkData_150_151)
{
   // Test IPv4, and then IPv6 later.
   // Ideally, the code would be smart enough to support two separate contexts
   // so that two separate key sets can be tested. There's no real reason to
   // support this in Armory right now, though, and it'd be a lot of work. For
   // now, just cheat and have two "separate" systems with the same input files.

   //grab serv private key from peer files
   auto servFilePath = std::filesystem::current_path() / baseDir_ /
      "bip150v0_srv1/identity-key-ipv4";
   ASSERT_TRUE(FileUtils::pathExists(servFilePath, 2));
   std::fstream serv_isf(servFilePath);
   char prvHex[65];
   serv_isf.getline(prvHex, 65);
   SecureBinaryData privServ(READHEX(prvHex));

   //grab client private key from peer files
   auto cliFilePath = std::filesystem::current_path() / baseDir_ /
      "bip150v0_cli1/identity-key-ipv4";
   ASSERT_TRUE(FileUtils::pathExists(cliFilePath, 2));
   std::fstream cli_isf(cliFilePath);
   char cliHex[65];
   cli_isf.getline(cliHex, 65);
   SecureBinaryData privCli(READHEX(cliHex));

   //compute public keys
   auto pubServ = Cryptography::ECDSA::computePublicKey(privServ, true);
   auto pubCli = Cryptography::ECDSA::computePublicKey(privCli, true);

   //setup peer stores
   auto serverStore = std::make_shared<ServerStore>(privServ);
   serverStore->addPeer(PeerKey{pubCli, PeerType::Client}, {"101.101.101.101:10101"}, {});

   auto clientStore = std::make_shared<ClientStore>(privCli);
   clientStore->addPeer(PeerKey{pubServ, PeerType::ServerTwoWay}, {"1.2.3.4:8333"}, {});

   startupBIP150CTX(4);

   btc_key prvKeyCliIn;
   btc_key prvKeyCliOut;
   btc_key prvKeySrvIn;
   btc_key prvKeySrvOut;
   prvKeyClientIn.copyTo(prvKeyCliIn.privkey);
   prvKeyClientOut.copyTo(prvKeyCliOut.privkey);
   prvKeyServerIn.copyTo(prvKeySrvIn.privkey);
   prvKeyServerOut.copyTo(prvKeySrvOut.privkey);
   BIP151Connection cliCon(&prvKeyCliIn, &prvKeyCliOut, clientStore->getView(PeerType::ServerTwoWay), false);
   BIP151Connection srvCon(&prvKeySrvIn, &prvKeySrvOut, serverStore->getView(), false);

   // Set up encinit/encack directly. (Initial encinit/encack will use regular
   // Bitcoin P2P messages, which we'll skip building.) Confirm all steps
   // function properly along the way.
   BinaryData cliInEncinitCliData; cliInEncinitCliData.resize(ENCINITMSGSIZE);   // SRV (Out) -> CLI (In)
   BinaryData cliInEncackCliData; cliInEncackCliData.resize(BIP151PUBKEYSIZE);  // CLI (In)  -> SRV (Out)
   BinaryData cliOutEncinitCliData; cliOutEncinitCliData.resize(ENCINITMSGSIZE);  // CLI (Out) -> SRV (In)
   BinaryData cliOutEncackCliData; cliOutEncackCliData.resize(BIP151PUBKEYSIZE); // SRV (In)  -> CLI (Out)
   int s1 = srvCon.getEncinitData(cliInEncinitCliData, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s1);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s2 = cliCon.processEncinit(cliInEncinitCliData.getRef(), false);
   EXPECT_EQ(0, s2);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s3 = cliCon.getEncackData(cliInEncackCliData);
   EXPECT_EQ(0, s3);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s4 = srvCon.processEncack(cliInEncackCliData.getRef(), true);
   EXPECT_EQ(0, s4);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s5 = cliCon.getEncinitData(cliOutEncinitCliData, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s5);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s6 = srvCon.processEncinit(cliOutEncinitCliData.getRef(), false);
   EXPECT_EQ(0, s6);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s7 = srvCon.getEncackData(cliOutEncackCliData);
   EXPECT_EQ(0, s7);
   EXPECT_TRUE(srvCon.connectionComplete());
   int s8 = cliCon.processEncack(cliOutEncackCliData.getRef(), true);
   EXPECT_EQ(0, s8);
   EXPECT_TRUE(cliCon.connectionComplete());

   // Get the fingerprint.
   std::string curFng = cliCon.getBIP150Fingerprint();
   EXPECT_EQ(cli150Fingerprint, curFng);

   ////////////////// Start the BIP 150 process for each side. /////////////////
   BinaryData authchallengeBuf; authchallengeBuf.resize(BIP151PRVKEYSIZE);
   BinaryData authreplyBuf; authreplyBuf.resize(BIP151PRVKEYSIZE*2);
   BinaryData authproposeBuf; authproposeBuf.resize(BIP151PRVKEYSIZE);
   EXPECT_EQ(BIP150State::INACTIVE, cliCon.getBIP150State());
   EXPECT_EQ(BIP150State::INACTIVE, srvCon.getBIP150State());

   // INACTIVE -> CHALLENGE1
   int b1 = cliCon.getAuthchallengeData(
      authchallengeBuf, "1.2.3.4:8333", true);
   EXPECT_EQ(0, b1);
   EXPECT_EQ(BIP150State::CHALLENGE1, cliCon.getBIP150State());
   EXPECT_EQ(authchallenge1Data, authchallengeBuf);
   int b2 = srvCon.processAuthchallenge(authchallengeBuf, true);
   EXPECT_EQ(0, b2);
   EXPECT_EQ(BIP150State::CHALLENGE1, srvCon.getBIP150State());

   // CHALLENGE1 -> REPLY1
   int b3 = srvCon.getAuthreplyData(authreplyBuf,true);
   EXPECT_EQ(0, b3);
   EXPECT_EQ(BIP150State::REPLY1, srvCon.getBIP150State());
   EXPECT_EQ(authreply1Data, authreplyBuf);
   int b4 = cliCon.processAuthreply(authreplyBuf, true);
   EXPECT_EQ(0, b4);
   EXPECT_EQ(BIP150State::REPLY1, cliCon.getBIP150State());

   // REPLY1 -> PROPOSE
   int b5 = cliCon.getAuthproposeData(authproposeBuf);
   EXPECT_EQ(0, b5);
   EXPECT_EQ(BIP150State::PROPOSE, cliCon.getBIP150State());
   EXPECT_EQ(authproposeData, authproposeBuf);
   int b6 = srvCon.processAuthpropose(authproposeBuf);
   EXPECT_EQ(0, b6);
   EXPECT_EQ(BIP150State::PROPOSE, srvCon.getBIP150State());

   // PROPOSE -> CHALLENGE2
   int b7 = srvCon.getAuthchallengeData(
      authchallengeBuf, "", false);
   EXPECT_EQ(0, b7);
   EXPECT_EQ(BIP150State::CHALLENGE2, srvCon.getBIP150State());
   EXPECT_EQ(authchallenge2Data, authchallengeBuf);
   int b8 = cliCon.processAuthchallenge(authchallengeBuf, false);
   EXPECT_EQ(0, b8);
   EXPECT_EQ(BIP150State::CHALLENGE2, cliCon.getBIP150State());

   // CHALLENGE2 -> REPLY2 (SUCCESS)
   int b9 = cliCon.getAuthreplyData(authreplyBuf, false);
   EXPECT_EQ(0, b9);

   cliCon.bip150HandshakeRekey();
   EXPECT_EQ(BIP150State::SUCCESS, cliCon.getBIP150State());
   EXPECT_EQ(authreply2Data, authreplyBuf);
   int b10 = srvCon.processAuthreply(authreplyBuf, false);
   EXPECT_EQ(0, b10);

   srvCon.bip150HandshakeRekey();
   EXPECT_EQ(BIP150State::SUCCESS, srvCon.getBIP150State());

   // See what happens when messages are received out of order.
   // INACTIVE -> CHALLENGE1  (Client)
   int b11 = cliCon.getAuthchallengeData(
      authchallengeBuf, "1.2.3.4:8333", true);
   EXPECT_EQ(0, b11);
   EXPECT_EQ(BIP150State::CHALLENGE1, cliCon.getBIP150State());
   EXPECT_EQ(authchallenge1Data, authchallengeBuf);

   // CHALLENGE1 -> PROPOSE  (Client)
   int b12 = cliCon.getAuthproposeData(authproposeBuf);
   EXPECT_EQ(-1, b12);
   EXPECT_EQ(BIP150State::ERR_STATE, cliCon.getBIP150State());
}

TEST_F(BIP150_151Test, checkData_150_151_1Way)
{
   // Test IPv4, and then IPv6 later.
   // Ideally, the code would be smart enough to support two separate contexts
   // so that two separate key sets can be tested. There's no real reason to
   // support this in Armory right now, though, and it'd be a lot of work. For
   // now, just cheat and have two "separate" systems with the same input files.

   //grab serv private key from peer files
   auto servFilePath = std::filesystem::current_path() / baseDir_ /
      "bip150v0_srv1/identity-key-ipv4";
   ASSERT_TRUE(FileUtils::pathExists(servFilePath, 2));
   std::fstream serv_isf(servFilePath);
   char prvHex[65];
   serv_isf.getline(prvHex, 65);
   SecureBinaryData privServ(READHEX(prvHex));

   //grab client private key from peer files
   auto cliFilePath = std::filesystem::current_path() / baseDir_ /
      "bip150v0_cli1/identity-key-ipv4";
   ASSERT_TRUE(FileUtils::pathExists(cliFilePath, 2));
   std::fstream cli_isf(cliFilePath);
   char cliHex[65];
   cli_isf.getline(cliHex, 65);
   SecureBinaryData privCli(READHEX(cliHex));

   //compute public keys
   auto pubServ = Cryptography::ECDSA::computePublicKey(privServ, true);
   auto pubCli = Cryptography::ECDSA::computePublicKey(privCli, true);

   //setup peer stores
   auto serverStore = std::make_shared<ServerStore>(privServ);
   auto clientStore = std::make_shared<ClientStore>(privCli);
   clientStore->addPeer(PeerKey{pubServ, PeerType::ServerOneWay}, {"1.2.3.4:8333"}, {});

   startupBIP150CTX(4);

   btc_key prvKeyCliIn;
   btc_key prvKeyCliOut;
   btc_key prvKeySrvIn;
   btc_key prvKeySrvOut;
   prvKeyClientIn.copyTo(prvKeyCliIn.privkey);
   prvKeyClientOut.copyTo(prvKeyCliOut.privkey);
   prvKeyServerIn.copyTo(prvKeySrvIn.privkey);
   prvKeyServerOut.copyTo(prvKeySrvOut.privkey);
   BIP151Connection cliCon(&prvKeyCliIn, &prvKeyCliOut, clientStore->getView(PeerType::ServerOneWay), true);
   BIP151Connection srvCon(&prvKeySrvIn, &prvKeySrvOut, serverStore->getView(), true);

   // Set up encinit/encack directly. (Initial encinit/encack will use regular
   // Bitcoin P2P messages, which we'll skip building.) Confirm all steps
   // function properly along the way.
   BinaryData cliInEncinitCliData; cliInEncinitCliData.resize(ENCINITMSGSIZE);   // SRV (Out) -> CLI (In)
   BinaryData cliInEncackCliData; cliInEncackCliData.resize(BIP151PUBKEYSIZE);  // CLI (In)  -> SRV (Out)
   BinaryData cliOutEncinitCliData; cliOutEncinitCliData.resize(ENCINITMSGSIZE);  // CLI (Out) -> SRV (In)
   BinaryData cliOutEncackCliData; cliOutEncackCliData.resize(BIP151PUBKEYSIZE); // SRV (In)  -> CLI (Out)
   int s1 = srvCon.getEncinitData(cliInEncinitCliData, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s1);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s2 = cliCon.processEncinit(cliInEncinitCliData.getRef(), false);
   EXPECT_EQ(0, s2);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s3 = cliCon.getEncackData(cliInEncackCliData);
   EXPECT_EQ(0, s3);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s4 = srvCon.processEncack(cliInEncackCliData.getRef(), true);
   EXPECT_EQ(0, s4);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s5 = cliCon.getEncinitData(cliOutEncinitCliData, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s5);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s6 = srvCon.processEncinit(cliOutEncinitCliData.getRef(), false);
   EXPECT_EQ(0, s6);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s7 = srvCon.getEncackData(cliOutEncackCliData);
   EXPECT_EQ(0, s7);
   EXPECT_TRUE(srvCon.connectionComplete());
   int s8 = cliCon.processEncack(cliOutEncackCliData.getRef(), true);
   EXPECT_EQ(0, s8);
   EXPECT_TRUE(cliCon.connectionComplete());

   // Get the fingerprint.
   std::string curFng = cliCon.getBIP150Fingerprint();
   EXPECT_EQ(cli150Fingerprint, curFng);

   ////////////////// Start the BIP 150 process for each side. /////////////////
   BinaryData authchallengeBuf; authchallengeBuf.resize(BIP151PRVKEYSIZE);
   BinaryData authreplyBuf; authreplyBuf.resize(BIP151PRVKEYSIZE*2);
   BinaryData authproposeBuf; authproposeBuf.resize(BIP151PRVKEYSIZE);
   EXPECT_EQ(BIP150State::INACTIVE, cliCon.getBIP150State());
   EXPECT_EQ(BIP150State::INACTIVE, srvCon.getBIP150State());

   // INACTIVE -> CHALLENGE1
   int b1 = cliCon.getAuthchallengeData(
      authchallengeBuf, "1.2.3.4:8333", true);
   EXPECT_EQ(0, b1);
   EXPECT_EQ(BIP150State::CHALLENGE1, cliCon.getBIP150State());
   EXPECT_EQ(authchallenge1Data, authchallengeBuf);
   int b2 = srvCon.processAuthchallenge(authchallengeBuf, true);
   EXPECT_EQ(0, b2);
   EXPECT_EQ(BIP150State::CHALLENGE1, srvCon.getBIP150State());

   // CHALLENGE1 -> REPLY1
   int b3 = srvCon.getAuthreplyData(authreplyBuf, true);
   EXPECT_EQ(0, b3);
   EXPECT_EQ(BIP150State::REPLY1, srvCon.getBIP150State());
   EXPECT_EQ(authreply1Data, authreplyBuf);
   int b4 = cliCon.processAuthreply(authreplyBuf, true);
   EXPECT_EQ(0, b4);
   EXPECT_EQ(BIP150State::REPLY1, cliCon.getBIP150State());

   // REPLY1 -> PROPOSE
   int b5 = cliCon.getAuthproposeData(authproposeBuf);
   EXPECT_EQ(0, b5);
   EXPECT_EQ(BIP150State::PROPOSE, cliCon.getBIP150State());
   EXPECT_EQ(authproposeData_1way, authproposeBuf);
   int b6 = srvCon.processAuthpropose(authproposeBuf);
   EXPECT_EQ(1, b6);
   EXPECT_EQ(BIP150State::PROPOSE, srvCon.getBIP150State());

   // PROPOSE -> CHALLENGE2
   int b7 = srvCon.getAuthchallengeData(
      authchallengeBuf, "", false);
   EXPECT_EQ(0, b7);
   EXPECT_EQ(BIP150State::CHALLENGE2, srvCon.getBIP150State());
   EXPECT_EQ(authchallenge2Data_1way, authchallengeBuf);
   int b8 = cliCon.processAuthchallenge(authchallengeBuf, false);
   EXPECT_EQ(0, b8);
   EXPECT_EQ(BIP150State::CHALLENGE2, cliCon.getBIP150State());

   // CHALLENGE2 -> REPLY2 (SUCCESS)
   int b9 = cliCon.getAuthreplyData(authreplyBuf, false);
   EXPECT_EQ(0, b9);

   cliCon.bip150HandshakeRekey();
   EXPECT_EQ(BIP150State::SUCCESS, cliCon.getBIP150State());
   EXPECT_EQ(memcmp(pubCli.getPtr(), authreplyBuf.getPtr(), BIP151PUBKEYSIZE), 0);
   int b10 = srvCon.processAuthreply(authreplyBuf, false);
   EXPECT_EQ(0, b10);

   srvCon.bip150HandshakeRekey();
   EXPECT_EQ(BIP150State::SUCCESS, srvCon.getBIP150State());
}

TEST_F(BIP150_151Test, checkData_150_151_privateClientToPublicServer)
{
   // Test IPv4, and then IPv6 later.
   // Ideally, the code would be smart enough to support two separate contexts
   // so that two separate key sets can be tested. There's no real reason to
   // support this in Armory right now, though, and it'd be a lot of work. For
   // now, just cheat and have two "separate" systems with the same input files.

   //grab serv private key from peer files
   auto servFilePath = std::filesystem::current_path() / baseDir_ /
      "bip150v0_srv1/identity-key-ipv4";
   ASSERT_TRUE(FileUtils::pathExists(servFilePath, 2));
   std::fstream serv_isf(servFilePath);
   char prvHex[65];
   serv_isf.getline(prvHex, 65);
   SecureBinaryData privServ(READHEX(prvHex));

   //grab client private key from peer files
   auto cliFilePath = std::filesystem::current_path() / baseDir_ /
      "bip150v0_cli1/identity-key-ipv4";
   ASSERT_TRUE(FileUtils::pathExists(cliFilePath, 2));
   std::fstream cli_isf(cliFilePath);
   char cliHex[65];
   cli_isf.getline(cliHex, 65);
   SecureBinaryData privCli(READHEX(cliHex));

   //compute public keys
   auto pubServ = Cryptography::ECDSA::computePublicKey(privServ, true);
   auto pubCli = Cryptography::ECDSA::computePublicKey(privCli, true);

   auto serverStore = std::make_shared<ServerStore>(privServ);
   serverStore->addPeer(PeerKey{pubCli, PeerType::Client}, {"101.101.101.101:10101"}, {});

   auto clientStore = std::make_shared<ClientStore>(privCli);
   clientStore->addPeer(PeerKey{pubServ, PeerType::ServerTwoWay}, {"1.2.3.4:8333"}, {});

   startupBIP150CTX(4);

   btc_key prvKeyCliIn;
   btc_key prvKeyCliOut;
   btc_key prvKeySrvIn;
   btc_key prvKeySrvOut;
   prvKeyClientIn.copyTo(prvKeyCliIn.privkey);
   prvKeyClientOut.copyTo(prvKeyCliOut.privkey);
   prvKeyServerIn.copyTo(prvKeySrvIn.privkey);
   prvKeyServerOut.copyTo(prvKeySrvOut.privkey);
   BIP151Connection cliCon(&prvKeyCliIn, &prvKeyCliOut, clientStore->getView(PeerType::ServerTwoWay), false);
   BIP151Connection srvCon(&prvKeySrvIn, &prvKeySrvOut, serverStore->getView(), true);

   // Set up encinit/encack directly. (Initial encinit/encack will use regular
   // Bitcoin P2P messages, which we'll skip building.) Confirm all steps
   // function properly along the way.
   BinaryData cliInEncinitCliData; cliInEncinitCliData.resize(ENCINITMSGSIZE);   // SRV (Out) -> CLI (In)
   BinaryData cliInEncackCliData; cliInEncackCliData.resize(BIP151PUBKEYSIZE);  // CLI (In)  -> SRV (Out)
   BinaryData cliOutEncinitCliData; cliOutEncinitCliData.resize(ENCINITMSGSIZE);  // CLI (Out) -> SRV (In)
   BinaryData cliOutEncackCliData; cliOutEncackCliData.resize(BIP151PUBKEYSIZE); // SRV (In)  -> CLI (Out)
   int s1 = srvCon.getEncinitData(cliInEncinitCliData, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s1);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s2 = cliCon.processEncinit(cliInEncinitCliData.getRef(), false);
   EXPECT_EQ(0, s2);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s3 = cliCon.getEncackData(cliInEncackCliData);
   EXPECT_EQ(0, s3);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s4 = srvCon.processEncack(cliInEncackCliData.getRef(), true);
   EXPECT_EQ(0, s4);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s5 = cliCon.getEncinitData(cliOutEncinitCliData, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s5);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s6 = srvCon.processEncinit(cliOutEncinitCliData.getRef(), false);
   EXPECT_EQ(0, s6);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s7 = srvCon.getEncackData(cliOutEncackCliData);
   EXPECT_EQ(0, s7);
   EXPECT_TRUE(srvCon.connectionComplete());
   int s8 = cliCon.processEncack(cliOutEncackCliData.getRef(), true);
   EXPECT_EQ(0, s8);
   EXPECT_TRUE(cliCon.connectionComplete());

   // Get the fingerprint.
   std::string curFng = cliCon.getBIP150Fingerprint();
   EXPECT_EQ(cli150Fingerprint, curFng);

   ////////////////// Start the BIP 150 process for each side. /////////////////
   BinaryData authchallengeBuf; authchallengeBuf.resize(BIP151PRVKEYSIZE);
   BinaryData authreplyBuf; authreplyBuf.resize(BIP151PRVKEYSIZE*2);
   BinaryData authproposeBuf; authproposeBuf.resize(BIP151PRVKEYSIZE);
   EXPECT_EQ(BIP150State::INACTIVE, cliCon.getBIP150State());
   EXPECT_EQ(BIP150State::INACTIVE, srvCon.getBIP150State());

   // INACTIVE -> CHALLENGE1
   int b1 = cliCon.getAuthchallengeData(
      authchallengeBuf, "1.2.3.4:8333", true);
   EXPECT_EQ(0, b1);
   EXPECT_EQ(BIP150State::CHALLENGE1, cliCon.getBIP150State());
   EXPECT_EQ(authchallenge1Data, authchallengeBuf);
   int b2 = srvCon.processAuthchallenge(authchallengeBuf, true);
   EXPECT_EQ(0, b2);
   EXPECT_EQ(BIP150State::CHALLENGE1, srvCon.getBIP150State());

   // CHALLENGE1 -> REPLY1
   int b3 = srvCon.getAuthreplyData(authreplyBuf, true);
   EXPECT_EQ(0, b3);
   EXPECT_EQ(BIP150State::REPLY1, srvCon.getBIP150State());
   EXPECT_EQ(authreply1Data, authreplyBuf);
   int b4 = cliCon.processAuthreply(authreplyBuf, true);
   EXPECT_EQ(0, b4);
   EXPECT_EQ(BIP150State::REPLY1, cliCon.getBIP150State());

   // REPLY1 -> PROPOSE
   int b5 = cliCon.getAuthproposeData(authproposeBuf);
   EXPECT_EQ(0, b5);
   EXPECT_EQ(BIP150State::PROPOSE, cliCon.getBIP150State());
   EXPECT_EQ(authproposeData, authproposeBuf);
   int b6 = srvCon.processAuthpropose(authproposeBuf);
   EXPECT_EQ(-1, b6);
   EXPECT_EQ(BIP150State::ERR_STATE, srvCon.getBIP150State());
}

TEST_F(BIP150_151Test, checkData_150_151_publicClientToPrivateServer)
{
   // Test IPv4, and then IPv6 later.
   // Ideally, the code would be smart enough to support two separate contexts
   // so that two separate key sets can be tested. There's no real reason to
   // support this in Armory right now, though, and it'd be a lot of work. For
   // now, just cheat and have two "separate" systems with the same input files.

   //grab serv private key from peer files
   auto servFilePath = std::filesystem::current_path() / baseDir_ /
      "bip150v0_srv1/identity-key-ipv4";
   ASSERT_TRUE(FileUtils::pathExists(servFilePath, 2));
   std::fstream serv_isf(servFilePath);
   char prvHex[65];
   serv_isf.getline(prvHex, 65);
   SecureBinaryData privServ(READHEX(prvHex));

   //grab client private key from peer files
   auto cliFilePath = std::filesystem::current_path() / baseDir_ /
      "bip150v0_cli1/identity-key-ipv4";
   ASSERT_TRUE(FileUtils::pathExists(cliFilePath, 2));
   std::fstream cli_isf(cliFilePath);
   char cliHex[65];
   cli_isf.getline(cliHex, 65);
   SecureBinaryData privCli(READHEX(cliHex));

   //compute public keys
   auto pubServ = Cryptography::ECDSA::computePublicKey(privServ, true);
   auto pubCli = Cryptography::ECDSA::computePublicKey(privCli, true);

   auto serverStore = std::make_shared<ServerStore>(privServ);
   serverStore->addPeer(PeerKey{pubCli, PeerType::Client}, {"101.101.101.101:10101"}, {});

   auto clientStore = std::make_shared<ClientStore>(privCli);
   clientStore->addPeer(PeerKey{pubServ, PeerType::ServerOneWay}, {"1.2.3.4:8333"}, {});

   startupBIP150CTX(4);

   btc_key prvKeyCliIn;
   btc_key prvKeyCliOut;
   btc_key prvKeySrvIn;
   btc_key prvKeySrvOut;
   prvKeyClientIn.copyTo(prvKeyCliIn.privkey);
   prvKeyClientOut.copyTo(prvKeyCliOut.privkey);
   prvKeyServerIn.copyTo(prvKeySrvIn.privkey);
   prvKeyServerOut.copyTo(prvKeySrvOut.privkey);
   BIP151Connection cliCon(&prvKeyCliIn, &prvKeyCliOut, clientStore->getView(PeerType::ServerOneWay), true);
   BIP151Connection srvCon(&prvKeySrvIn, &prvKeySrvOut, serverStore->getView(), false);

   // Set up encinit/encack directly. (Initial encinit/encack will use regular
   // Bitcoin P2P messages, which we'll skip building.) Confirm all steps
   // function properly along the way.
   BinaryData cliInEncinitCliData; cliInEncinitCliData.resize(ENCINITMSGSIZE);   // SRV (Out) -> CLI (In)
   BinaryData cliInEncackCliData; cliInEncackCliData.resize(BIP151PUBKEYSIZE);  // CLI (In)  -> SRV (Out)
   BinaryData cliOutEncinitCliData; cliOutEncinitCliData.resize(ENCINITMSGSIZE);  // CLI (Out) -> SRV (In)
   BinaryData cliOutEncackCliData; cliOutEncackCliData.resize(BIP151PUBKEYSIZE); // SRV (In)  -> CLI (Out)
   int s1 = srvCon.getEncinitData(cliInEncinitCliData, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s1);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s2 = cliCon.processEncinit(cliInEncinitCliData.getRef(), false);
   EXPECT_EQ(0, s2);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s3 = cliCon.getEncackData(cliInEncackCliData);
   EXPECT_EQ(0, s3);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s4 = srvCon.processEncack(cliInEncackCliData.getRef(), true);
   EXPECT_EQ(0, s4);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s5 = cliCon.getEncinitData(cliOutEncinitCliData, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s5);
   EXPECT_FALSE(cliCon.connectionComplete());
   int s6 = srvCon.processEncinit(cliOutEncinitCliData.getRef(), false);
   EXPECT_EQ(0, s6);
   EXPECT_FALSE(srvCon.connectionComplete());
   int s7 = srvCon.getEncackData(cliOutEncackCliData);
   EXPECT_EQ(0, s7);
   EXPECT_TRUE(srvCon.connectionComplete());
   int s8 = cliCon.processEncack(cliOutEncackCliData.getRef(), true);
   EXPECT_EQ(0, s8);
   EXPECT_TRUE(cliCon.connectionComplete());

   // Get the fingerprint.
   std::string curFng = cliCon.getBIP150Fingerprint();
   EXPECT_EQ(cli150Fingerprint, curFng);

   ////////////////// Start the BIP 150 process for each side. /////////////////
   BinaryData authchallengeBuf; authchallengeBuf.resize(BIP151PRVKEYSIZE);
   BinaryData authreplyBuf; authreplyBuf.resize(BIP151PRVKEYSIZE*2);
   BinaryData authproposeBuf; authproposeBuf.resize(BIP151PRVKEYSIZE);
   EXPECT_EQ(BIP150State::INACTIVE, cliCon.getBIP150State());
   EXPECT_EQ(BIP150State::INACTIVE, srvCon.getBIP150State());

   // INACTIVE -> CHALLENGE1
   int b1 = cliCon.getAuthchallengeData(
      authchallengeBuf, "1.2.3.4:8333", true);
   EXPECT_EQ(0, b1);
   EXPECT_EQ(BIP150State::CHALLENGE1, cliCon.getBIP150State());
   EXPECT_EQ(authchallenge1Data, authchallengeBuf);
   int b2 = srvCon.processAuthchallenge(authchallengeBuf, true);
   EXPECT_EQ(0, b2);
   EXPECT_EQ(BIP150State::CHALLENGE1, srvCon.getBIP150State());

   // CHALLENGE1 -> REPLY1
   int b3 = srvCon.getAuthreplyData(authreplyBuf, true);
   EXPECT_EQ(0, b3);
   EXPECT_EQ(BIP150State::REPLY1, srvCon.getBIP150State());
   EXPECT_EQ(authreply1Data, authreplyBuf);
   int b4 = cliCon.processAuthreply(authreplyBuf, true);
   EXPECT_EQ(0, b4);
   EXPECT_EQ(BIP150State::REPLY1, cliCon.getBIP150State());

   // REPLY1 -> PROPOSE
   int b5 = cliCon.getAuthproposeData(authproposeBuf);
   EXPECT_EQ(0, b5);
   EXPECT_EQ(BIP150State::PROPOSE, cliCon.getBIP150State());
   EXPECT_EQ(authproposeData_1way, authproposeBuf);
   int b6 = srvCon.processAuthpropose(authproposeBuf);
   EXPECT_EQ(-1, b6);
   EXPECT_EQ(BIP150State::ERR_STATE, srvCon.getBIP150State());
}

// Test handshake failure cases. All cases will fail eventually.
TEST_F(BIP150_151Test, handshakeCases_151_Only)
{
   // Try to generate an encack before generating an encinit.
   BIP151Connection cliCon1(std::make_unique<PeerStoreView>(
      nullptr, std::make_shared<SecureBinaryData>()), false);
   BIP151Connection srvCon1(std::make_unique<PeerStoreView>(
      nullptr, std::make_shared<SecureBinaryData>()), false);
   BinaryData dummy1; dummy1.resize(BIP151PUBKEYSIZE);
   int s1 = cliCon1.getEncackData(dummy1);
   EXPECT_EQ(-1, s1);

   // Try to process an encack before processing an encinit.
   dummy1[0] = 0x03;
   dummy1[1] = 0xff;
   int s2 = srvCon1.processEncack(dummy1.getRef(), true);
   EXPECT_EQ(-1, s2);

   // Attempt to set an incorrect ciphersuite.
   BIP151Connection cliCon2(std::make_unique<PeerStoreView>(
      nullptr, std::make_shared<SecureBinaryData>()), false);
   BIP151Connection srvCon2(std::make_unique<PeerStoreView>(
      nullptr, std::make_shared<SecureBinaryData>()), false);
   BinaryData dummy3; dummy3.resize(ENCINITMSGSIZE);
   BinaryData dummy4; dummy4.resize(64);
   int s3 = cliCon2.getEncinitData(dummy3, static_cast<BIP151SymCiphers>(0xda));
   EXPECT_EQ(-1, s3);

   // Attempt to rekey before the connection is complete.
   int s4 = cliCon2.getEncinitData(dummy3, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_EQ(0, s4);
   int s5 = srvCon2.processEncinit(dummy3.getRef(), false);
   EXPECT_EQ(0, s5);
   int s6 = srvCon2.bip151RekeyConn(dummy4);
   EXPECT_EQ(-1, s6);

   // Run after the final test has finished.
   shutdownBIP151CTX();
}

////////////////////////////////////////////////////////////////////////////////
// Test the BIP 151 auto-rekey code here. Because the timer code isn't in place
// yet, and 1GB of data must be processed before a rekey is required, separate
// this test from the main test suite.
class BIP151RekeyTest : public ::testing::Test
{
protected:
   virtual void SetUp(void)
   {
      std::string command_hexstr = "fake";
      std::string payload_hexstr = "deadbeef";
      std::string msg_hexstr = "0d0000000466616b6504000000deadbeef";

      command.copyFrom(command_hexstr);
      payload = READHEX(payload_hexstr);
      msg = READHEX(msg_hexstr);
   }

   BinaryData command;
   BinaryData payload;
   BinaryData msg;
};

////////////////////////////////////////////////////////////////////////////////
TEST_F(BIP151RekeyTest, rekeyRequired)
{
   // Run before the first test has been run. (SetUp/TearDown will be called
   // for each test. Context startup/shutdown multiple times leads to crashes.)
   startupBIP151CTX();

   // BIP 151 connection uses private keys we feed it. (Normally, we'd let it
   // generate its own private keys.)
   BIP151Connection cliCon(std::make_unique<PeerStoreView>(
      nullptr, std::make_shared<SecureBinaryData>()), false);
   BIP151Connection srvCon(std::make_unique<PeerStoreView>(
      nullptr, std::make_shared<SecureBinaryData>()), false);

   // Set up encinit/encack directly. (Initial encinit/encack will use regular
   // Bitcoin P2P messages, which we'll skip building.) Confirm all steps
   // function properly along the way.
   BinaryData cliInEncinitCliData; cliInEncinitCliData.resize(ENCINITMSGSIZE);   // SRV (Out) -> CLI (In)
   BinaryData cliInEncackCliData; cliInEncackCliData.resize(BIP151PUBKEYSIZE);  // CLI (In)  -> SRV (Out)
   BinaryData cliOutEncinitCliData; cliOutEncinitCliData.resize(ENCINITMSGSIZE);  // CLI (Out) -> SRV (In)
   BinaryData cliOutEncackCliData; cliOutEncackCliData.resize(BIP151PUBKEYSIZE); // SRV (In)  -> CLI (Out)
   srvCon.getEncinitData(cliInEncinitCliData, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_FALSE(srvCon.connectionComplete());
   cliCon.processEncinit(cliInEncinitCliData.getRef(), false);
   EXPECT_FALSE(cliCon.connectionComplete());
   cliCon.getEncackData(cliInEncackCliData);
   EXPECT_FALSE(cliCon.connectionComplete());
   srvCon.processEncack(cliInEncackCliData.getRef(), true);
   EXPECT_FALSE(srvCon.connectionComplete());
   cliCon.getEncinitData(cliOutEncinitCliData, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_FALSE(cliCon.connectionComplete());
   srvCon.processEncinit(cliOutEncinitCliData.getRef(), false);
   EXPECT_FALSE(srvCon.connectionComplete());
   srvCon.getEncackData(cliOutEncackCliData);
   EXPECT_TRUE(srvCon.connectionComplete());
   cliCon.processEncack(cliOutEncackCliData.getRef(), true);
   EXPECT_TRUE(cliCon.connectionComplete());

   // Our packet is 17 bytes. Over the course of 1200 bytes (unit test value 
   // to trigger rekeys, default is 1GB), we need 69 loops before we have 
   // to rekey.
   auto cmd = BinaryData::fromString("fake"sv);
   std::array<uint8_t, 4> payload{ 0xde, 0xad, 0xbe, 0xef };
   BinaryData testMsgData; testMsgData.resize(17);
   size_t finalMsgSize;
   BIP151Message testMsg(cmd.getPtr(), cmd.getSize(),
                         payload.data(), payload.size());
   testMsg.getEncStructMsg(testMsgData.getPtr(), testMsgData.getSize(),
                           finalMsgSize);
   BinaryData encMsgBuffer; encMsgBuffer.resize(testMsgData.getSize() + 16);
   BinaryData decMsgBuffer; decMsgBuffer.resize(testMsgData.getSize());
   for(uint32_t x = 0; x < 69; ++x)
   {
      cliCon.assemblePacket(testMsgData.getRef(),
         encMsgBuffer.getPtr(),  encMsgBuffer.getSize());
      srvCon.decryptPacket(encMsgBuffer.getRef(), decMsgBuffer);
      EXPECT_FALSE(cliCon.rekeyNeeded(testMsgData.getSize()));
      EXPECT_EQ(msg, decMsgBuffer);
   }
   cliCon.assemblePacket(testMsgData.getRef(),
      encMsgBuffer.getPtr(), encMsgBuffer.getSize());
   srvCon.decryptPacket(encMsgBuffer.getRef(), decMsgBuffer);
   EXPECT_TRUE(cliCon.rekeyNeeded(testMsgData.getSize()));
   EXPECT_EQ(msg, decMsgBuffer);

   // Do a rekey and confirm that everything has been reset.
   // Rekey (CLI -> SRV) and confirm that the results are correct.
   BinaryData rekeyBuf; rekeyBuf.resize(64);
   cliCon.bip151RekeyConn(rekeyBuf); // Cli rekey
   decMsgBuffer.resize(rekeyBuf.getSize() - 16);
   srvCon.decryptPacket(rekeyBuf.getRef(), decMsgBuffer);

   // Process the incoming rekey.
   BIP151Message inEncack(decMsgBuffer.getPtr(), decMsgBuffer.getSize());
   BinaryData inCmd; inCmd.resize(inEncack.getCmdSize());
   BinaryData inPayload; inPayload.resize(inEncack.getPayloadSize());
   inEncack.getCmd(inCmd.getPtr(), inCmd.getSize());
   EXPECT_EQ("encack", inCmd.toBinStr());
   srvCon.processEncack(inPayload.getRef(), false); // Srv rekey

   // Repeat the data Tx and confirm that a rekey can be re-triggered.
   encMsgBuffer.resize(testMsgData.getSize() + 16);
   decMsgBuffer.resize(testMsgData.getSize());
   for(uint32_t x = 0; x < 69; ++x)
   {
      cliCon.assemblePacket(testMsgData.getRef(),
         encMsgBuffer.getPtr(), encMsgBuffer.getSize());
      srvCon.decryptPacket(encMsgBuffer.getRef(), decMsgBuffer);
      EXPECT_FALSE(cliCon.rekeyNeeded(testMsgData.getSize()));
      EXPECT_EQ(msg, decMsgBuffer);
   }
   cliCon.assemblePacket(testMsgData.getRef(),
      encMsgBuffer.getPtr(), encMsgBuffer.getSize());
   srvCon.decryptPacket(encMsgBuffer.getRef(), decMsgBuffer);
   EXPECT_TRUE(cliCon.rekeyNeeded(testMsgData.getSize()));
   EXPECT_EQ(msg, decMsgBuffer);

   // Run after the final test has finished.
   shutdownBIP151CTX();
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

   std::cout << "Running main() from gtest_main.cc\n";

   LOGDISABLESTDOUT();

   // Required by libbtc.
   Cryptography::ECDSA::setupContext();

   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();

   FLUSHLOG();
   CLEANUPLOG();

   return exitCode;
}
