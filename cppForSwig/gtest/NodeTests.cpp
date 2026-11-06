////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2026, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
   #include <winsock2.h>
   #include <windows.h>
#endif

#include <cstring>

#include "TestUtils.h"
#include <Node/BitcoinP2P.h>

using namespace std;
using namespace Armory;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// P2P framing and payload parsing: the bytes go to Payload::deserialize the
// way Peer::processDataStackThread hands them over, with the previous result
// when it holds the start of a message the socket read cut short
namespace
{
   using P2PPayload = ::Node::Core::P2P::Payload;
   using P2PPayloadType = ::Node::Core::P2P::PayloadType;
   const uint32_t p2pMagic = 0xD9B4BEF9;

   //header (magic, type, length, checksum of the payload) + payload
   std::vector<uint8_t> p2pMessage(const std::string& type,
      const std::vector<uint8_t>& payload, uint32_t length)
   {
      std::vector<uint8_t> msg(24, 0);
      memcpy(&msg[0], &p2pMagic, 4);
      memcpy(&msg[4], type.c_str(), std::min(type.size(), size_t(12)));
      memcpy(&msg[16], &length, 4);
      auto checksum = BtcUtils::getHash256(payload.data(), payload.size());
      memcpy(&msg[20], checksum.getPtr(), 4);
      msg.insert(msg.end(), payload.begin(), payload.end());
      return msg;
   }

   std::vector<uint8_t> p2pMessage(const std::string& type,
      const std::vector<uint8_t>& payload)
   {
      return p2pMessage(type, payload, payload.size());
   }

   //deserializes data, prev holds the result when there's a spill to
   //carry to the next call
   std::shared_ptr<P2PPayload::DeserializedPayloads> p2pParse(
      std::vector<uint8_t> data,
      std::shared_ptr<P2PPayload::DeserializedPayloads>* prev = nullptr)
   {
      //no spare capacity, so that ASan sees reads past the end
      data.shrink_to_fit();
      auto result = P2PPayload::deserialize(
         data, p2pMagic, prev != nullptr ? *prev : nullptr);
      if (prev != nullptr) {
         *prev = result->spillOffset_ != SIZE_MAX ? result : nullptr;
      }
      return result;
   }

   //number of payloads deserialized from data
   size_t p2pDeserialize(std::vector<uint8_t> data,
      std::shared_ptr<P2PPayload::DeserializedPayloads>* prev = nullptr)
   {
      return p2pParse(std::move(data), prev)->payloads_.size();
   }

   //a version payload: fixed part, user agent, start height
   std::vector<uint8_t> versionPayload(const std::string& userAgent,
      uint32_t startHeight)
   {
      std::vector<uint8_t> version(80, 0);
      uint32_t protoVersion = 70016;
      memcpy(&version[0], &protoVersion, 4);
      version.push_back((uint8_t)userAgent.size());
      version.insert(version.end(), userAgent.begin(), userAgent.end());
      version.insert(version.end(), (uint8_t*)&startHeight,
         (uint8_t*)&startHeight + 4);
      return version;
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST(P2PPayloadTests, ShortHeader)
{
   //a verack followed by 6 bytes of the next header: the header fields
   //were read without checking 24 bytes were left
   auto data = p2pMessage("verack", {});
   data.insert(data.end(), {0xf9, 0xbe, 0xb4, 0xd9, 'v', 'e'});
   EXPECT_EQ(p2pDeserialize(data), 1U);
}

////////////////////////////////////////////////////////////////////////////////
TEST(P2PPayloadTests, LengthWrap)
{
   //length + 24 wrapped around in 32 bits, the checksum was then computed
   //over 4 GiB from the start of the payload
   EXPECT_EQ(p2pDeserialize(p2pMessage("inv", {}, 0xFFFFFFF0)), 0U);
}

////////////////////////////////////////////////////////////////////////////////
TEST(P2PPayloadTests, Version)
{
   auto result = p2pParse(p2pMessage("version",
      versionPayload("/Satoshi:28.0.0/", 900000)));
   ASSERT_EQ(result->payloads_.size(), 1U);
   ASSERT_EQ(result->payloads_[0]->type(), P2PPayloadType::Version);
   auto version = dynamic_cast<::Node::Core::P2P::Payload_Version*>(
      result->payloads_[0].get());
   ASSERT_NE(version, nullptr);
   EXPECT_EQ(version->vheader_.version_, 70016U);
   EXPECT_EQ(version->userAgent_, "/Satoshi:28.0.0/");
   EXPECT_EQ(version->startHeight_, 900000U);

   //no user agent
   EXPECT_EQ(p2pDeserialize(p2pMessage("version", versionPayload("", 1))),
      1U);
}

////////////////////////////////////////////////////////////////////////////////
TEST(P2PPayloadTests, Version_TooShort)
{
   //the version payload was parsed without any size check
   EXPECT_EQ(p2pDeserialize(p2pMessage("version", {})), 0U);
   EXPECT_EQ(p2pDeserialize(p2pMessage("version", {0x01})), 0U);

   //fixed part, then a user agent length (0xffff) past the payload
   std::vector<uint8_t> version(80, 0);
   version.insert(version.end(), {0xfd, 0xff, 0xff, 0, 0, 0, 0});
   EXPECT_EQ(p2pDeserialize(p2pMessage("version", version)), 0U);

   //a user agent that leaves no room for the start height
   auto noHeight = versionPayload("/Satoshi:28.0.0/", 1);
   noHeight.resize(noHeight.size() - 4);
   EXPECT_EQ(p2pDeserialize(p2pMessage("version", noHeight)), 0U);
}

////////////////////////////////////////////////////////////////////////////////
TEST(P2PPayloadTests, Tx_Empty)
{
   EXPECT_EQ(p2pDeserialize(p2pMessage("tx", {})), 0U);
   EXPECT_EQ(p2pDeserialize(p2pMessage("tx", {0x01})), 1U);
}

////////////////////////////////////////////////////////////////////////////////
TEST(P2PPayloadTests, Spill_ShortHeader)
{
   /*
   a verack, then 10 bytes: the start of the next message spills over to
   the next read. The spill offset doesn't go through processPacket and
   its header was read whole whatever was left.
   */
   auto data = p2pMessage("verack", {});
   data.insert(data.end(), {1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
   std::shared_ptr<P2PPayload::DeserializedPayloads> prev;
   EXPECT_EQ(p2pDeserialize(data, &prev), 1U);
   EXPECT_EQ(p2pDeserialize({0, 0, 0}, &prev), 0U);
}

////////////////////////////////////////////////////////////////////////////////
TEST(P2PPayloadTests, Spill_SplitMessage)
{
   //a ping cut by the socket read, inside its header and inside its
   //payload: it is parsed once the rest of it comes in
   std::vector<uint8_t> nonce{1, 2, 3, 4, 5, 6, 7, 8};
   auto ping = p2pMessage("ping", nonce);

   for (size_t cut : {10, 30}) {
      auto data = p2pMessage("verack", {});
      data.insert(data.end(), ping.begin(), ping.begin() + cut);
      std::shared_ptr<P2PPayload::DeserializedPayloads> prev;
      EXPECT_EQ(p2pDeserialize(data, &prev), 1U) << "cut at " << cut;
      ASSERT_NE(prev, nullptr);

      std::vector<uint8_t> rest(ping.begin() + cut, ping.end());
      auto result = p2pParse(rest, &prev);
      ASSERT_EQ(result->payloads_.size(), 1U) << "cut at " << cut;
      EXPECT_EQ(result->payloads_[0]->type(), P2PPayloadType::Ping);
   }
}

////////////////////////////////////////////////////////////////////////////////
TEST(P2PPayloadTests, Reject)
{
   //type "tx", code 0x10, reason "dust", 32 bytes of extra data
   std::vector<uint8_t> reject{0x02, 't', 'x', 0x10, 0x04, 'd', 'u', 's', 't'};
   reject.insert(reject.end(), 32, 0xaa);
   auto result = p2pParse(p2pMessage("reject", reject));
   ASSERT_EQ(result->payloads_.size(), 1U);
   auto payload = dynamic_cast<::Node::Core::P2P::Payload_Reject*>(
      result->payloads_[0].get());
   ASSERT_NE(payload, nullptr);
   EXPECT_EQ(payload->rejectType(), P2PPayloadType::Tx);
   EXPECT_EQ(payload->getReasonStr(), "dust");
   EXPECT_EQ(payload->getExtra().size(), 32U);
}

////////////////////////////////////////////////////////////////////////////////
TEST(P2PPayloadTests, Reject_ReasonLength)
{
   //the reason length (0xffff) was used without checking the payload
   EXPECT_EQ(p2pDeserialize(p2pMessage("reject",
      {0x02, 't', 'x', 0x10, 0xfd, 0xff, 0xff, 'x'})), 0U);

   //a type length past the payload
   EXPECT_EQ(p2pDeserialize(p2pMessage("reject",
      {0x20, 't', 'x', 0x10, 0x00})), 0U);

   //no reason length after the code, then a reason length varint cut short:
   //dropped like the other malformed rejects
   EXPECT_EQ(p2pDeserialize(p2pMessage("reject", {0x02, 't', 'x', 0x10})), 0U);
   EXPECT_EQ(p2pDeserialize(p2pMessage("reject",
      {0x02, 't', 'x', 0x10, 0xfd, 0x01})), 0U);

   //an empty reject
   EXPECT_EQ(p2pDeserialize(p2pMessage("reject", {})), 0U);
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

   srand(time(0));
   std::cout << "Running main() from gtest_main.cc\n";

   // Required by libbtc.
   Cryptography::ECDSA::setupContext();

   //LOGENABLESTDOUT();
   LOGDISABLESTDOUT();

   testing::InitGoogleTest(&argc, argv);
   int exitCode = RUN_ALL_TESTS();

   // Required by libbtc.
   Cryptography::ECDSA::shutdown();

   FLUSHLOG();
   CLEANUPLOG();

   return exitCode;
}
