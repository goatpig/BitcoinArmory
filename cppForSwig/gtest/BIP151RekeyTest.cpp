////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#include "TestUtils.h"


////////////////////////////////////////////////////////////////////////////////
// Test the BIP 151 auto-rekey code here. Because the timer code isn't in place
// yet, and 1GB of data must be processed before a rekey is required, separate
// this test from the main test suite.
class BIP151RekeyTest : public ::testing::Test
{
protected:
   virtual void SetUp(void)
   {
      string command_hexstr = "fake";
      string payload_hexstr = "deadbeef";
      string msg_hexstr = "0d0000000466616b6504000000deadbeef";

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

   bip151Connection cliCon;
   bip151Connection srvCon;

   // Set up encinit/encack directly. (Initial encinit/encack will use regular
   // Bitcoin P2P messages, which we'll skip building.) Confirm all steps
   // function properly along the way.
   BinaryData cliInEncinitCliData(ENCINITMSGSIZE);   // SRV (Out) -> CLI (In)
   BinaryData cliInEncackCliData(BIP151PUBKEYSIZE);  // CLI (In)  -> SRV (Out)
   BinaryData cliOutEncinitCliData(ENCINITMSGSIZE);  // CLI (Out) -> SRV (In)
   BinaryData cliOutEncackCliData(BIP151PUBKEYSIZE); // SRV (In)  -> CLI (Out)
   srvCon.getEncinitData(cliInEncinitCliData.getPtr(),
                         cliInEncinitCliData.getSize(),
                         bip151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_FALSE(srvCon.connectionComplete());
   cliCon.processEncinit(cliInEncinitCliData.getPtr(),
                         cliInEncinitCliData.getSize(),
                         false);
   EXPECT_FALSE(cliCon.connectionComplete());
   cliCon.getEncackData(cliInEncackCliData.getPtr(),
                        cliInEncackCliData.getSize());
   EXPECT_FALSE(cliCon.connectionComplete());
   srvCon.processEncack(cliInEncackCliData.getPtr(),
                        cliInEncackCliData.getSize(),
                        true);
   EXPECT_FALSE(srvCon.connectionComplete());
   cliCon.getEncinitData(cliOutEncinitCliData.getPtr(),
                         cliOutEncinitCliData.getSize(),
                         bip151SymCiphers::CHACHA20POLY1305_OPENSSH);
   EXPECT_FALSE(cliCon.connectionComplete());
   srvCon.processEncinit(cliOutEncinitCliData.getPtr(),
                         cliOutEncinitCliData.getSize(),
                         false);
   EXPECT_FALSE(srvCon.connectionComplete());
   srvCon.getEncackData(cliOutEncackCliData.getPtr(),
                        cliOutEncackCliData.getSize());
   EXPECT_TRUE(srvCon.connectionComplete());
   cliCon.processEncack(cliOutEncackCliData.getPtr(),
                        cliOutEncackCliData.getSize(),
                        true);
   EXPECT_TRUE(cliCon.connectionComplete());

   // Our packet is 17 bytes. Over the course of 1Gb (not 1 Gib!), we need
   // 58,823,530 loops before we have to rekey.
   BinaryData cmd("fake");
   std::array<uint8_t, 4> payload = {0xde, 0xad, 0xbe, 0xef};
   BinaryData testMsgData(17);
   size_t finalMsgSize;
   bip151Message testMsg(cmd.getPtr(), cmd.getSize(),
                         payload.data(), payload.size());
   testMsg.getEncStructMsg(testMsgData.getPtr(), testMsgData.getSize(),
                           finalMsgSize);
   BinaryData encMsgBuffer(testMsgData.getSize() + 16);
   BinaryData decMsgBuffer(testMsgData.getSize());
   for(uint32_t x = 0; x < 58823529; ++x)
   {
      cliCon.assemblePacket(testMsgData.getPtr(),
                            testMsgData.getSize(),
                            encMsgBuffer.getPtr(),
                            encMsgBuffer.getSize());
      srvCon.decryptPacket(encMsgBuffer.getPtr(),
                           encMsgBuffer.getSize(),
                           decMsgBuffer.getPtr(),
                           decMsgBuffer.getSize());
      EXPECT_FALSE(cliCon.rekeyNeeded());
      EXPECT_EQ(msg, decMsgBuffer);
   }
   cliCon.assemblePacket(testMsgData.getPtr(),
                         testMsgData.getSize(),
                         encMsgBuffer.getPtr(),
                         encMsgBuffer.getSize());
   srvCon.decryptPacket(encMsgBuffer.getPtr(),
                        encMsgBuffer.getSize(),
                        decMsgBuffer.getPtr(),
                        decMsgBuffer.getSize());
   EXPECT_TRUE(cliCon.rekeyNeeded());
   EXPECT_EQ(msg, decMsgBuffer);

   // Do a rekey and confirm that everything has been reset.
   // Rekey (CLI -> SRV) and confirm that the results are correct.
   BinaryData rekeyBuf(64);
   cliCon.rekeyConn(rekeyBuf.getPtr(), rekeyBuf.getSize()); // Cli rekey
   decMsgBuffer.resize(rekeyBuf.getSize() - 16);
   srvCon.decryptPacket(rekeyBuf.getPtr(),
                        rekeyBuf.getSize(),
                        decMsgBuffer.getPtr(),
                        decMsgBuffer.getSize());

   // Process the incoming rekey.
   bip151Message inEncack(decMsgBuffer.getPtr(), decMsgBuffer.getSize());
   BinaryData inCmd(inEncack.getCmdSize());
   BinaryData inPayload(inEncack.getPayloadSize());
   inEncack.getCmd(inCmd.getPtr(), inCmd.getSize());
   EXPECT_EQ("encack", inCmd.toBinStr());
   srvCon.processEncack(inPayload.getPtr(), inPayload.getSize(), false); // Srv rekey

   // Repeat the data Tx and confirm that a rekey can be re-triggered.
   encMsgBuffer.resize(testMsgData.getSize() + 16);
   decMsgBuffer.resize(testMsgData.getSize());
   for(uint32_t x = 0; x < 58823529; ++x)
   {
      cliCon.assemblePacket(testMsgData.getPtr(),
                            testMsgData.getSize(),
                            encMsgBuffer.getPtr(),
                            encMsgBuffer.getSize());
      srvCon.decryptPacket(encMsgBuffer.getPtr(),
                           encMsgBuffer.getSize(),
                           decMsgBuffer.getPtr(),
                           decMsgBuffer.getSize());
      EXPECT_FALSE(cliCon.rekeyNeeded());
      EXPECT_EQ(msg, decMsgBuffer);
   }
   cliCon.assemblePacket(testMsgData.getPtr(),
                         testMsgData.getSize(),
                         encMsgBuffer.getPtr(),
                         encMsgBuffer.getSize());
   srvCon.decryptPacket(encMsgBuffer.getPtr(),
                        encMsgBuffer.getSize(),
                        decMsgBuffer.getPtr(),
                        decMsgBuffer.getSize());
   EXPECT_TRUE(cliCon.rekeyNeeded());
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

   // Required by libbtc.
   btc_ecc_start();

   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();

   FLUSHLOG();
   CLEANUPLOG();

   return exitCode;
}
