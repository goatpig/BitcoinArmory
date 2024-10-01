////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2024, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BtcUtils.h"
#include "WebSocketMessage.h"
#include "libwebsockets.h"
#include "BIP15x_Handshake.h"
#include "SocketWritePayload.h"
#include "log.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/BDV.capnp.h"

//memory aligned start of payload
#define PAYLOAD_HEADER 16

using WordArray = kj::ArrayPtr<const capnp::word>;

namespace
{
   ////
   BinaryData reconstructFromFragments(
      const std::map<uint32_t, BinaryDataRef>& payloadMap)
   {
      if (payloadMap.empty()) {
         return {};
      }

      size_t total = (WEBSOCKET_MESSAGE_PACKET_SIZE - 48) * (payloadMap.size() - 1);
      total += payloadMap.rbegin()->second.getSize();
      BinaryData full(total);
      size_t pos = 0;
      for (const auto& payloadPair : payloadMap) {
         const auto& payload = payloadPair.second;
         memcpy(full.getPtr() + pos, payload.getPtr(), payload.getSize());
         pos += payload.getSize();
      }
      return full;
   }

   ////
   std::unique_ptr<capnp::MessageReader> getSegmentedReader(
      const std::map<uint32_t, BinaryDataRef>& payloadMap)
   {
      if (payloadMap.empty()) {
         return nullptr;
      }

      auto builder = kj::heapArrayBuilder<
         const kj::ArrayPtr<const capnp::word>>(
            payloadMap.size());

      for (auto& data_pair : payloadMap) {
         auto& dataRef = data_pair.second;
         kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(dataRef.getPtr()),
            dataRef.getSize() / sizeof(capnp::word));
         builder.add(words);
      }

      auto array = builder.finish();
      auto result = std::make_unique<
         capnp::SegmentArrayMessageReader>(
            array.asPtr());
      return result;
   }
}

////////////////////////////////////////////////////////////////////////////////
//
// WebSocketMessageCodec
//
////////////////////////////////////////////////////////////////////////////////
std::vector<BinaryData> WebSocketMessageCodec::serialize(
   const std::vector<uint8_t>& payload, BIP151Connection* connPtr,
   ArmoryAEAD::BIP151_PayloadType type, uint32_t id)
{
   BinaryDataRef bdr;
   if (!payload.empty()) {
      bdr.setRef(&payload[0], payload.size());
   }
   return serialize(bdr, connPtr, type, id);
}

////////////////////////////////////////////////////////////////////////////////
std::vector<BinaryData> WebSocketMessageCodec::serialize(
   const std::string& payload, BIP151Connection* connPtr,
   ArmoryAEAD::BIP151_PayloadType type, uint32_t id)
{
   BinaryDataRef bdr((uint8_t*)payload.c_str(), payload.size());
   return serialize(bdr, connPtr, type, id);
}

////////////////////////////////////////////////////////////////////////////////
std::vector<BinaryData> WebSocketMessageCodec::serializePacketWithoutId(
   const BinaryDataRef& payload, BIP151Connection* connPtr,
   ArmoryAEAD::BIP151_PayloadType type)
{
   /***
   no packet fragmentation, flat size serialization:
   uint32_t size
   uint8_t type
   nbytes payload
   ***/

   uint32_t size = payload.getSize() + PAYLOAD_HEADER - 4;
   BinaryData plainText(payload.getSize() + LWS_PRE + PAYLOAD_HEADER + POLY1305MACLEN);
   if (plainText.getSize() > WEBSOCKET_MESSAGE_PACKET_SIZE) {
      throw std::runtime_error("payload is too large to serialize");
   }

   //skip LWS_PRE, copy in packet size
   memcpy(plainText.getPtr() + LWS_PRE, &size, 4);

   //type
   memset(plainText.getPtr() + LWS_PRE + 12, (uint8_t)type, 1);

   //payload
   memcpy(plainText.getPtr() + LWS_PRE + PAYLOAD_HEADER, payload.getPtr(), payload.getSize());

   //encrypt if possible
   std::vector<BinaryData> result(1);
   if (connPtr != nullptr) {
      connPtr->assemblePacket(
         plainText.getPtr() + LWS_PRE,
         payload.getSize() + PAYLOAD_HEADER,
         plainText.getPtr() + LWS_PRE,
         plainText.getSize() - LWS_PRE);
   } else {
      plainText.resize(payload.getSize() + PAYLOAD_HEADER + LWS_PRE);
   }

   result[0] = std::move(plainText);
   return result;
}

////////////////////////////////////////////////////////////////////////////////
std::vector<BinaryData> WebSocketMessageCodec::serialize(
   const BinaryDataRef& payload, BIP151Connection* connPtr,
   ArmoryAEAD::BIP151_PayloadType type, uint32_t id)
{
   //is this payload carrying a msgid?
   if (type > ArmoryAEAD::BIP151_PayloadType::Threshold_Begin) {
      return serializePacketWithoutId(payload, connPtr, type);
   }

   /***
   Fragmented packet seralization:
   Overhead is 48 bytes:
      - LWS_PRE (16)
      - PAYLOAD_HEADER (16)
      - POLY1305MACLEN (16)

   Payload header is always 16 bytes, regardless of how many
   header bytes are used.

   If payload < WEBSOCKET_MESSAGE_PACKET_SIZE - overhead:
    Single packet header:
     uint32_t packet size
     uint8_t type (WS_MSGTYPE_SINGLEPACKET)
     uint32_t msgid
     nbytes payload (starts at PAYLOAD_HEADER)

   Else:
    Fragmented header:
     uint32_t packet size
     uint8_t type (WS_MSGTYPE_FRAGMENTEDPACKET_HEADER)
     uint32_t msgid
     uint16_t count (>= 2)
     nbytes payload fragment (starts at PAYLOAD_HEADER)

    Fragments:
     uint32_t packet size
     uint8_t type (WS_MSGTYPE_FRAGMENTEDPACKET_FRAGMENT)
     uint32_t msgid
     varint packet id (1 to 65535)
     nbytes payload fragment (starts at PAYLOAD_HEADER)

   NOTES:
    . All packets starts with empty LWS_PRE bytes .
    . Payload header is always 16 bytes, regardless of how many
      header bytes are used .
    . packet size is nbytes + PAYLOAD_HEADER - 4 .
    . nbytes is always min(
         payload.size(),
         WEBSOCKET_MESSAGE_PACKET_SIZE - overhead) .
   ***/

   //encrypt lambda
   std::vector<BinaryData> result;
   auto encryptAndAdd = [connPtr, &result](BinaryData& data)
   {
      size_t plainTextLen = data.getSize() - LWS_PRE - POLY1305MACLEN;
      size_t cipherTextLen = data.getSize() - LWS_PRE;

      if (connPtr != nullptr) {
         if (connPtr->assemblePacket(
            data.getPtr() + LWS_PRE, plainTextLen,
            data.getPtr() + LWS_PRE, cipherTextLen) != 0) {
            //failed to encrypt, abort
            throw std::runtime_error("failed to encrypt packet, aborting");
         }
      } else {
         data.resize(cipherTextLen);
      }

      result.emplace_back(data);
   };

   auto data_len = payload.getSize();
   const size_t overhead = PAYLOAD_HEADER + LWS_PRE + POLY1305MACLEN;
   const size_t payload_room = WEBSOCKET_MESSAGE_PACKET_SIZE - overhead;

   if (data_len <= payload_room) {
      //single packet serialization
      result.reserve(1);
      uint32_t size = data_len + PAYLOAD_HEADER - 4;
      BinaryData plainText(LWS_PRE + POLY1305MACLEN + PAYLOAD_HEADER + data_len);

      memcpy(plainText.getPtr() + LWS_PRE, &size, 4);
      memcpy(plainText.getPtr() + LWS_PRE + 4, &id, 4);
      memset(plainText.getPtr() + LWS_PRE + 12,
         (uint8_t)ArmoryAEAD::BIP151_PayloadType::SinglePacket, 1);

      if (!payload.empty()) {
         memcpy(plainText.getPtr() + LWS_PRE + PAYLOAD_HEADER, payload.getPtr(), data_len);
      }
      encryptAndAdd(plainText);
   } else {
      //figure out fragment count
      uint32_t fragment_count32 = (data_len + payload_room - 1) / payload_room;
      if (fragment_count32 > UINT16_MAX) {
         throw std::runtime_error("payload too large for serialization");
      }
      result.reserve(fragment_count32);

      //setup first fragment
      BinaryData header_packet(WEBSOCKET_MESSAGE_PACKET_SIZE);
      uint32_t header_size = payload_room + 12;

      //header
      memcpy(header_packet.getPtr() + LWS_PRE, &header_size, 4);
      memcpy(header_packet.getPtr() + LWS_PRE + 4, &id, 4);
      memcpy(header_packet.getPtr() + LWS_PRE + 8, &fragment_count32, 4);
      memset(header_packet.getPtr() + LWS_PRE + 12,
         (uint8_t)ArmoryAEAD::BIP151_PayloadType::FragmentHeader, 1);

      //fragment data
      memcpy(header_packet.getPtr() + LWS_PRE + PAYLOAD_HEADER, payload.getPtr(), payload_room);
      encryptAndAdd(header_packet);

      //now other fragments
      size_t pos = payload_room;
      for (unsigned i = 1; i < fragment_count32; i++) {
         //get fragment size
         size_t data_size = std::min(payload_room, data_len - pos);

         BinaryData fragment_packet(data_size + overhead);
         uint32_t packet_size = data_size + PAYLOAD_HEADER - 4;

         //set header
         memcpy(fragment_packet.getPtr() + LWS_PRE, &packet_size, 4);
         memcpy(fragment_packet.getPtr() + LWS_PRE + 4, &id, 4);
         memcpy(fragment_packet.getPtr() + LWS_PRE + 8, &i, 4);
         memset(fragment_packet.getPtr() + LWS_PRE + 12,
            (uint8_t)ArmoryAEAD::BIP151_PayloadType::FragmentPacket, 1);

         //copy fragment data
         memcpy(
            fragment_packet.getPtr() + LWS_PRE + PAYLOAD_HEADER,
            payload.getPtr() + pos,
            data_size
         );
         pos += data_size;

         //encrypt and append to vector
         encryptAndAdd(fragment_packet);
      }
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
uint32_t WebSocketMessageCodec::getMessageId(const BinaryDataRef& packet)
{
   //sanity check
   if (packet.getSize() < 8) {
      return UINT32_MAX;
   }
   return *(uint32_t*)(packet.getPtr() + 4);
}

///////////////////////////////////////////////////////////////////////////////
//
// SerializedMessage
//
///////////////////////////////////////////////////////////////////////////////
void SerializedMessage::construct(const std::vector<uint8_t>& data,
   BIP151Connection* connPtr, ArmoryAEAD::BIP151_PayloadType type, uint32_t id)
{
   packets_ = std::move(
      WebSocketMessageCodec::serialize(data, connPtr, type, id));
}

///////////////////////////////////////////////////////////////////////////////
void SerializedMessage::construct(const BinaryDataRef& data,
   BIP151Connection* connPtr, ArmoryAEAD::BIP151_PayloadType type, uint32_t id)
{
   packets_ = std::move(
      WebSocketMessageCodec::serialize(data, connPtr, type, id));
}

///////////////////////////////////////////////////////////////////////////////
void SerializedMessage::construct(std::unique_ptr<Socket_WritePayload> payload,
   BIP151Connection* connPtr, uint32_t id)
{
   if (payload->isSingleSegment()) {
      std::vector<uint8_t> data;
      payload->serialize(data);
      packets_ = std::move(
         WebSocketMessageCodec::serialize(data,
            connPtr, ArmoryAEAD::BIP151_PayloadType::FragmentHeader, id));
   } else {
      auto capnpPayload = dynamic_cast<WritePayload_Capnp*>(payload.get());
      if (capnpPayload == nullptr) {
         throw std::runtime_error("this should be a capnp payload!");
      }

      size_t overhead = LWS_PRE + PAYLOAD_HEADER + POLY1305MACLEN;
      const auto& segments = capnpPayload->builder->getSegmentsForOutput();
      packets_.reserve(segments.size());
      for (uint32_t i=0; i<segments.size(); i++) {
         const auto& segment = segments[i];
         auto segmentSize = segment.size() * sizeof(capnp::word);
         BinaryData encryptedSegment(overhead + segmentSize);

         uint32_t dataSize = segmentSize + PAYLOAD_HEADER - sizeof(uint32_t);
         memcpy(encryptedSegment.getPtr() + LWS_PRE,     &dataSize, 4);
         memcpy(encryptedSegment.getPtr() + LWS_PRE + 4, &id      , 4);

         if (i==0) {
            //first segment
            uint32_t segmentCount = segments.size();
            BinaryDataRef firstSegment(
               (const uint8_t*)segment.begin(), segmentSize);
            memcpy(encryptedSegment.getPtr() + LWS_PRE + 8, &segmentCount, 4);
            memset(encryptedSegment.getPtr() + LWS_PRE + 12,
               (uint8_t)ArmoryAEAD::BIP151_PayloadType::FirstSegment, 1);
         } else {
            //other segments
            memcpy(encryptedSegment.getPtr() + LWS_PRE + 8, &i, 4);
            memset(encryptedSegment.getPtr() + LWS_PRE + 12,
               (uint8_t)ArmoryAEAD::BIP151_PayloadType::Segment, 1);
         }

         //copy segment
         memcpy(
            encryptedSegment.getPtr() + LWS_PRE + PAYLOAD_HEADER,
            (uint8_t*)segment.begin(),
            segmentSize
         );

         if (connPtr != nullptr) {
            if (connPtr->assemblePacket(
               encryptedSegment.getPtr() + LWS_PRE, segmentSize + PAYLOAD_HEADER,
               encryptedSegment.getPtr() + LWS_PRE, encryptedSegment.getSize() - LWS_PRE) != 0) {
               //failed to encrypt, abort
               throw std::runtime_error("failed to encrypt packet, aborting");
            }
         } else {
            encryptedSegment.resize(dataSize - POLY1305MACLEN);
         }
         packets_.emplace_back(std::move(encryptedSegment));
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
BinaryData SerializedMessage::consumeNextPacket()
{
   auto val = std::move(packets_[index_++]);
   return val;
}

///////////////////////////////////////////////////////////////////////////////
//
// WebSocketMessagePartial
//
///////////////////////////////////////////////////////////////////////////////
WebSocketMessagePartial::WebSocketMessagePartial() :
   type_(ArmoryAEAD::BIP151_PayloadType::Undefined)
{}

///////////////////////////////////////////////////////////////////////////////
void WebSocketMessagePartial::reset()
{
   packets_.clear();
   id_ = UINT32_MAX;
   type_ = ArmoryAEAD::BIP151_PayloadType::Undefined;
   packetCount_ = UINT32_MAX;
   myType_ = SerializedType::Undefined;
}

///////////////////////////////////////////////////////////////////////////////
bool WebSocketMessagePartial::parsePacket(BinaryData& packet)
{
   if (packet.empty()) {
      return false;
   }

   BinaryRefReader brrPacket(packet);
   auto packetlen = brrPacket.get_uint32_t();
   if (packetlen != brrPacket.getSizeRemaining()) {
      return false;
   }

   if (packet.getSize() < PAYLOAD_HEADER) {
      //only aead init packets can be this short
      return parseMessageWithoutId(packet);
   }


   brrPacket.advance(8);
   auto msgType = (ArmoryAEAD::BIP151_PayloadType)brrPacket.get_uint8_t();
   switch (msgType)
   {
      case ArmoryAEAD::BIP151_PayloadType::SinglePacket:
         return parseSinglePacket(packet);

      case ArmoryAEAD::BIP151_PayloadType::FragmentHeader:
         return parseFragmentedMessageHeader(packet);

      case ArmoryAEAD::BIP151_PayloadType::FragmentPacket:
         return parseMessageFragment(packet);

      case ArmoryAEAD::BIP151_PayloadType::FirstSegment:
         return parseFirstSegment(packet);

      case ArmoryAEAD::BIP151_PayloadType::Segment:
         return parseSegment(packet);

      default:
         return parseMessageWithoutId(packet);
   }
}

///////////////////////////////////////////////////////////////////////////////
bool WebSocketMessagePartial::parseSinglePacket(BinaryData& packet)
{
   /*
   uint8_t type(WS_MSGTYPE_SINGLEPACKET)
   uint32_t msgid
   nbytes payload
   */

   if (id_ != UINT32_MAX) {
      return false;
   }
   BinaryRefReader brr(packet);
   brr.advance(4);
   id_ = brr.get_uint32_t();

   brr.advance(4);
   type_ = (ArmoryAEAD::BIP151_PayloadType)brr.get_uint8_t();
   if (type_ != ArmoryAEAD::BIP151_PayloadType::SinglePacket) {
      return false;
   }

   if (myType_ == SerializedType::Undefined) {
      myType_ = SerializedType::Single;
   } else if (myType_ != SerializedType::Single) {
      throw std::runtime_error("message type mismatch");
   }

   packets_.emplace(0, std::move(packet));
   packetCount_ = 1;
   return true;
}

///////////////////////////////////////////////////////////////////////////////
bool WebSocketMessagePartial::parseFragmentedMessageHeader(BinaryData& packet)
{
   /*
   uint8_t type (WS_MSGTYPE_FRAGMENTEDPACKET_HEADER)
   uint32_t msgid
   uint16_t count (>= 2)
   nbytes payload fragment
   */

   BinaryRefReader brr(packet);
   brr.advance(4);
   auto id = brr.get_uint32_t();
   packetCount_ = brr.get_uint32_t();

   type_ = (ArmoryAEAD::BIP151_PayloadType)brr.get_uint8_t();
   if (type_ != ArmoryAEAD::BIP151_PayloadType::FragmentHeader) {
      return false;
   }

   if (myType_ == SerializedType::Undefined) {
      myType_ = SerializedType::Fragmented;
   } else if (myType_ != SerializedType::Fragmented) {
      return false;
   }

   if (id_ != UINT32_MAX && id_ != id) {
      return false;
   }
   id_ = id;

   packets_.emplace(0, std::move(packet));
   return true;
}

///////////////////////////////////////////////////////////////////////////////
bool WebSocketMessagePartial::parseFirstSegment(BinaryData& packet)
{
   /*
   uint32_t size
   uint32_t msgid
   uint32_t segment count
   uint8_t  type (ArmoryAEAD::BIP151_PayloadType::FirstSegment)
   (size - 12) bytes of segment data
   */

   BinaryRefReader brr(packet);
   brr.advance(4);
   auto id = brr.get_uint32_t();
   packetCount_ = brr.get_uint32_t();

   type_ = (ArmoryAEAD::BIP151_PayloadType)brr.get_uint8_t();
   if (type_ != ArmoryAEAD::BIP151_PayloadType::FirstSegment) {
      return false;
   }

   if (myType_ == SerializedType::Undefined) {
      myType_ = SerializedType::Segmented;
   } else if (myType_ != SerializedType::Segmented) {
      return false;
   }

   if (id_ != UINT32_MAX && id_ != id) {
      return false;
   }
   id_ = id;

   packets_.emplace(0, std::move(packet));
   return true;
}

///////////////////////////////////////////////////////////////////////////////
bool WebSocketMessagePartial::parseMessageFragment(BinaryData& packet)
{
   /*
   uint8_t type (WS_MSGTYPE_FRAGMENTEDPACKET_FRAGMENT)
   uint32_t msgid
   varint packet id (1 to 65535)
   nbytes payload fragment
   */

   BinaryRefReader brr(packet);
   brr.advance(4);
   auto id = brr.get_uint32_t();
   auto packetId = (uint16_t)brr.get_uint32_t();

   auto type = (ArmoryAEAD::BIP151_PayloadType)brr.get_uint8_t();
   if (type != ArmoryAEAD::BIP151_PayloadType::FragmentPacket) {
      return false;
   }

   if (myType_ == SerializedType::Undefined) {
      myType_ = SerializedType::Fragmented;
   } else if (myType_ != SerializedType::Fragmented) {
      return false;
   }

   if (id_ != UINT32_MAX && id_ != id) {
      return false;
   }
   id_ = id;

   packets_.emplace(packetId, std::move(packet));
   return true;
}

///////////////////////////////////////////////////////////////////////////////
bool WebSocketMessagePartial::parseSegment(BinaryData& packet)
{
   /*
   uint32_t size
   uint32_t msgid
   uint32_t packetId
   uint8_t  type (ArmoryAEAD::BIP151_PayloadType::Segment)
   (size - 12) bytes of segment data
   */

   BinaryRefReader brr(packet);
   brr.advance(4);
   auto id = brr.get_uint32_t();
   auto packetId = (uint16_t)brr.get_uint32_t();

   auto type = (ArmoryAEAD::BIP151_PayloadType)brr.get_uint8_t();
   if (type != ArmoryAEAD::BIP151_PayloadType::Segment) {
      return false;
   }

   if (myType_ == SerializedType::Undefined) {
      myType_ = SerializedType::Segmented;
   } else if (myType_ != SerializedType::Segmented) {
      return false;
   }

   if (id_ != UINT32_MAX && id_ != id) {
      return false;
   }
   id_ = id;

   packets_.emplace(packetId, std::move(packet));
   return true;
}

///////////////////////////////////////////////////////////////////////////////
bool WebSocketMessagePartial::parseMessageWithoutId(BinaryData& packet)
{
   /*
   uint8_t type
   nbytes payload
   */

   BinaryRefReader brr(packet);
   if (packet.getSize() < PAYLOAD_HEADER) {
      brr.advance(4);
      type_ = (ArmoryAEAD::BIP151_PayloadType)brr.get_uint8_t();
   } else {
      brr.advance(12);
      type_ = (ArmoryAEAD::BIP151_PayloadType)brr.get_uint8_t();

      //set reader at data start
      brr.resetPosition();
      brr.advance(PAYLOAD_HEADER);
   }

   if (type_ <= ArmoryAEAD::BIP151_PayloadType::Threshold_Begin) {
      return false;
   }

   if (myType_ == SerializedType::Undefined) {
      myType_ = SerializedType::WithoutId;
   } else if (myType_ != SerializedType::WithoutId) {
      return false;
   }

   packets_.emplace(0, std::move(packet));
   packetCount_ = 1;
   return true;
}

///////////////////////////////////////////////////////////////////////////////
std::unique_ptr<CapnpReader> WebSocketMessagePartial::getReader() const
{
   if (!isReady()) {
      return nullptr;
   }

   switch (myType_)
   {
      case SerializedType::Single:
         return std::make_unique<CapnpFlatReader>(std::move(packets_));

      case SerializedType::Fragmented:
         return std::make_unique<CapnpFragmentedReader>(std::move(packets_));

      case SerializedType::Segmented:
         return std::make_unique<CapnpSegmentedReader>(std::move(packets_));

      default:
         return nullptr;
   }
}

///////////////////////////////////////////////////////////////////////////////
bool WebSocketMessagePartial::isReady() const
{
   if (packetCount_ == 0) {
      return false;
   }
   return packets_.size() == packetCount_;
}

///////////////////////////////////////////////////////////////////////////////
BinaryDataRef WebSocketMessagePartial::getSingleBinaryMessage(void) const
{
   if (packetCount_ != 1 || !isReady()) {
      return {};
   }
   const auto& packet = packets_.begin()->second;
   return packet.getSliceRef(PAYLOAD_HEADER, packet.getSize() - PAYLOAD_HEADER);
}

///////////////////////////////////////////////////////////////////////////////
ArmoryAEAD::BIP151_PayloadType WebSocketMessagePartial::readPacketType(
   const BinaryDataRef& bdr)
{
   if (bdr.getSize() < 5) {
      throw std::runtime_error("packet is too small to be serialized fragment");
   } else if (bdr.getSize() < PAYLOAD_HEADER) {
      return (ArmoryAEAD::BIP151_PayloadType)bdr.getPtr()[4];
   } else {
      return (ArmoryAEAD::BIP151_PayloadType)bdr.getPtr()[12];
   }
}

///////////////////////////////////////////////////////////////////////////////
uint32_t WebSocketMessagePartial::readMessageId(const BinaryData& data)
{
   if (data.getSize() < PAYLOAD_HEADER) {
      return UINT32_MAX;
   }

   BinaryRefReader brr(data);
   brr.advance(4);
   return brr.get_uint32_t();
}

///////////////////////////////////////////////////////////////////////////////
bool WebSocketMessagePartial::isSegmented() const
{
   if (myType_ == SerializedType::Undefined) {
      throw std::runtime_error("invalid message");
   }

   return myType_ == SerializedType::Segmented;
}

///////////////////////////////////////////////////////////////////////////////
// capnp readers
///////////////////////////////////////////////////////////////////////////////
CapnpReader::~CapnpReader()
{}

CapnpFlatReader::CapnpFlatReader(std::map<uint32_t, BinaryData> dataMap)
{
   if (dataMap.size() != 1) {
      throw std::runtime_error("dataMap should have a single packet!");
   }
   packet_ = std::move(dataMap.begin()->second);
   dataMap.clear();
}

std::unique_ptr<capnp::MessageReader> CapnpFlatReader::getReader() const
{
   auto dataRef = packet_.getSliceRef(
      PAYLOAD_HEADER,
      packet_.getSize() - PAYLOAD_HEADER
   );

   WordArray words(
      reinterpret_cast<const capnp::word*>(dataRef.getPtr()),
      dataRef.getSize() / sizeof(capnp::word)
   );
   return std::make_unique<capnp::FlatArrayMessageReader>(words);
}

////
CapnpFragmentedReader::CapnpFragmentedReader(
   std::map<uint32_t, BinaryData> dataMap)
{
   if (dataMap.empty()) {
      throw std::runtime_error("empty data map!");
   }

   size_t size = 0;
   for (const auto& data : dataMap) {
      size += data.second.getSize() - PAYLOAD_HEADER;
   }
   sequentialBuffer_.resize(size);

   size_t offset = 0;
   for (const auto& data : dataMap) {
      memcpy(
         sequentialBuffer_.getPtr() + offset,
         data.second.getPtr() + PAYLOAD_HEADER,
         data.second.getSize() - PAYLOAD_HEADER);
      offset += data.second.getSize() - PAYLOAD_HEADER;
   }
   dataMap.clear();
}

std::unique_ptr<capnp::MessageReader> CapnpFragmentedReader::getReader() const
{
   WordArray words(
      reinterpret_cast<const capnp::word*>(sequentialBuffer_.getPtr()),
      sequentialBuffer_.getSize() / sizeof(capnp::word)
   );
   return std::make_unique<capnp::FlatArrayMessageReader>(words);
}

////
CapnpSegmentedReader::CapnpSegmentedReader(
   std::map<uint32_t, BinaryData> dataMap) :
   segments_(std::move(dataMap))
{
   if (segments_.empty()) {
      throw std::runtime_error("empty segments!");
   }
}

CapnpSegmentedReader::~CapnpSegmentedReader()
{
   if (kjArrayPtr_ != nullptr) {
      auto kjArray = reinterpret_cast<WordArray*>(kjArrayPtr_);
      if (kjArray != nullptr) {
         free(kjArray);
      } else {
         LOGERR << "unexpected kjArray type, possible memleak!";
      }
      kjArrayPtr_ = nullptr;
   }
}

std::unique_ptr<capnp::MessageReader> CapnpSegmentedReader::getReader() const
{
   auto kjArrayPtr = (WordArray*)malloc(segments_.size() * sizeof(WordArray));
   for (const auto& segment : segments_) {
      kjArrayPtr[segment.first] = WordArray(
         reinterpret_cast<const capnp::word*>(segment.second.getPtr() + PAYLOAD_HEADER),
         (segment.second.getSize() - PAYLOAD_HEADER) / sizeof(capnp::word)
      );
   }
   kj::ArrayPtr<const WordArray> kjArray(kjArrayPtr, segments_.size());
   kjArrayPtr_ = kjArrayPtr;
   return std::make_unique<capnp::SegmentArrayMessageReader>(kjArray);
}
