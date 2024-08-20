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

#include <capnp/message.h>
#include <capnp/serialize.h>

//memory aligned start of payload
#define PAYLOAD_HEADER 16

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
   if(payload.size() > 0) {
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

   uint32_t size = payload.getSize() + 1;
   BinaryData plainText(PAYLOAD_HEADER + size + LWS_PRE + POLY1305MACLEN);
   if (plainText.getSize() > WEBSOCKET_MESSAGE_PACKET_SIZE) {
      throw std::runtime_error("payload is too large to serialize");
   }

   //skip LWS_PRE, copy in packet size
   memcpy(plainText.getPtr() + LWS_PRE, &size, 4);
   size += 4;

   //type
   memset(plainText.getPtr() + LWS_PRE + 4, (uint8_t)type, 1);

   //payload
   memcpy(plainText.getPtr() + LWS_PRE + PAYLOAD_HEADER, payload.getPtr(), payload.getSize());

   //encrypt if possible
   std::vector<BinaryData> result;
   if (connPtr != nullptr) {
      connPtr->assemblePacket(plainText.getPtr() + LWS_PRE, size,
         plainText.getPtr() + LWS_PRE, size + PAYLOAD_HEADER + POLY1305MACLEN);
   } else {
      plainText.resize(size + LWS_PRE);
   }

   result.emplace_back(plainText);
   return result;
}

////////////////////////////////////////////////////////////////////////////////
std::vector<BinaryData> WebSocketMessageCodec::serialize(
   const BinaryDataRef& payload, BIP151Connection* connPtr,
   ArmoryAEAD::BIP151_PayloadType type, uint32_t id)
{
   //is this payload carrying a msgid?
   if (type > ArmoryAEAD::BIP151_PayloadType::Threshold_Begin)
      return serializePacketWithoutId(payload, connPtr, type);

   /***
   Fragmented packet seralization

   If the payload is less than (WEBSOCKET_MESSAGE_PACKET_SIZE - 16 - LWS_PRE -
   POLY1305MACLEN), use:
    Single packet header:
     uint32_t packet size
     uint8_t type (WS_MSGTYPE_SINGLEPACKET)
     uint32_t msgid
     nbytes payload

   Otherwise, use:
    Fragmented header:
     uint32_t packet size
     uint8_t type (WS_MSGTYPE_FRAGMENTEDPACKET_HEADER)
     uint32_t msgid
     uint16_t count (>= 2)
     nbytes payload fragment

    Fragments:
     uint32_t packet size
     uint8_t type (WS_MSGTYPE_FRAGMENTEDPACKET_FRAGMENT)
     uint32_t msgid
     varint packet id (1 to 65535)
     nbytes payload fragment
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
   static size_t payload_room =
      WEBSOCKET_MESSAGE_PACKET_SIZE - LWS_PRE - POLY1305MACLEN - PAYLOAD_HEADER;

   if (data_len <= payload_room) {
      //single packet serialization
      uint32_t size = data_len;
      BinaryData plainText(LWS_PRE + POLY1305MACLEN + PAYLOAD_HEADER + data_len);

      memcpy(plainText.getPtr() + LWS_PRE, &size, 4);
      memset(plainText.getPtr() + LWS_PRE + 4,
         (uint8_t)ArmoryAEAD::BIP151_PayloadType::SinglePacket, 1);
      memcpy(plainText.getPtr() + LWS_PRE + 5, &id, 4);

      if (!payload.empty()) {
         memcpy(plainText.getPtr() + LWS_PRE + PAYLOAD_HEADER, payload.getPtr(), data_len);
      }
      encryptAndAdd(plainText);
   } else {
      //2 extra bytes for fragment count
      uint32_t header_room = payload_room - 2;
      size_t left_over = data_len - header_room;
      
      //1 extra bytes for fragment count < 253
      size_t fragment_room = payload_room - 1;
      uint32_t fragment_count32 = left_over / fragment_room + 1;
      if (fragment_count32 >= 253)
      {
         left_over -= 252 * fragment_room;
         
         //3 extra bytes for fragment count >= 253
         fragment_room = payload_room - 3; 
         fragment_count32 = 253 + left_over / fragment_room;
      }

      if (left_over % fragment_room != 0)
         ++fragment_count32;

      if (fragment_count32 > UINT16_MAX) {
         throw std::runtime_error("payload too large for serialization");
      }
      uint16_t fragment_count = (uint16_t)fragment_count32;

      BinaryData header_packet(WEBSOCKET_MESSAGE_PACKET_SIZE);

      //-2 for fragment count
      size_t pos = payload_room - 2;

      //+4 to shave off payload size, +1 for type
      header_room = payload_room + 5;

      memcpy(header_packet.getPtr() + LWS_PRE, &header_room, 4);
      memset(header_packet.getPtr() + LWS_PRE + 4,
         (uint8_t)ArmoryAEAD::BIP151_PayloadType::FragmentHeader, 1);
      memcpy(header_packet.getPtr() + LWS_PRE + 5, &id, 4);
      memcpy(header_packet.getPtr() + LWS_PRE + 9, &fragment_count, 2);
      memcpy(header_packet.getPtr() + LWS_PRE + PAYLOAD_HEADER, payload.getPtr(), pos);
      encryptAndAdd(header_packet);

      size_t fragment_overhead = 10 + LWS_PRE + POLY1305MACLEN;
      for (unsigned i = 1; i < fragment_count; i++) {
         if (i == 253) {
            fragment_overhead += 2;
         }

         //figure out data size
         size_t data_size = std::min(
            WEBSOCKET_MESSAGE_PACKET_SIZE - fragment_overhead,
            data_len - pos);

         BinaryData fragment_packet(data_size + fragment_overhead);
         uint32_t packet_size =
            data_size + fragment_overhead - LWS_PRE - POLY1305MACLEN - 4;

         memcpy(fragment_packet.getPtr() + LWS_PRE, &packet_size, 4);
         memset(fragment_packet.getPtr() + LWS_PRE + 4,
            (uint8_t)ArmoryAEAD::BIP151_PayloadType::FragmentPacket, 1);
         memcpy(fragment_packet.getPtr() + LWS_PRE + 5, &id, 4);

         size_t offset = LWS_PRE + 9;
         if (i < 253) {
            uint8_t frag_id = i;
            memcpy(fragment_packet.getPtr() + offset, &frag_id, 1);
            ++offset;
         } else {
            uint16_t frag_id = i;
            fragment_packet.getPtr()[offset++] = 0xFD;
            memcpy(fragment_packet.getPtr() + offset, &frag_id, 2);
            offset += 2;
         }

         memcpy(fragment_packet.getPtr() + offset, payload.getPtr() + pos, data_size);
         pos += data_size;

         encryptAndAdd(fragment_packet);
      }
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
std::unique_ptr<capnp::MessageReader>
WebSocketMessageCodec::getFragmentedReader(
   const std::map<uint16_t, BinaryDataRef>& payloadMap)
{
   if (payloadMap.empty()) {
      return nullptr;
   }

   auto builder = kj::heapArrayBuilder<const kj::ArrayPtr<const capnp::word>>(
      payloadMap.size());
   for (auto& data_pair : payloadMap) {
      auto& dataRef = data_pair.second;
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(dataRef.getPtr()),
         dataRef.getSize() / sizeof(capnp::word));
      builder.add(words);
   }

   auto array = builder.finish();
   return std::make_unique<capnp::SegmentArrayMessageReader>(array.asPtr());
}

////////////////////////////////////////////////////////////////////////////////
uint32_t WebSocketMessageCodec::getMessageId(const BinaryDataRef& packet)
{
   //sanity check
   if (packet.getSize() < 7)
      return UINT32_MAX;

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
}

///////////////////////////////////////////////////////////////////////////////
bool WebSocketMessagePartial::parsePacket(const BinaryDataRef& dataRef)
{
   if (dataRef.empty()) {
      return false;
   }

   BinaryRefReader brrPacket(dataRef);
   auto packetlen = brrPacket.get_uint32_t();
   if (packetlen != brrPacket.getSizeRemaining() - PAYLOAD_HEADER + 4) {
      LOGERR << "invalid packet size";
      return false;
   }

   auto dataSlice = brrPacket.get_BinaryDataRef(packetlen);
   BinaryRefReader brrSlice(dataSlice);

   auto msgType = (ArmoryAEAD::BIP151_PayloadType)brrSlice.get_uint8_t();
   switch (msgType)
   {
      case ArmoryAEAD::BIP151_PayloadType::SinglePacket:
         return parseSinglePacket(dataRef);

      case ArmoryAEAD::BIP151_PayloadType::FragmentHeader:
         return parseFragmentedMessageHeader(dataSlice);

      case ArmoryAEAD::BIP151_PayloadType::FragmentPacket:
         return parseMessageFragment(dataSlice);

      case ArmoryAEAD::BIP151_PayloadType::Start:
      case ArmoryAEAD::BIP151_PayloadType::PresentPubKey:
      case ArmoryAEAD::BIP151_PayloadType::PresentPubKeyChild:
      case ArmoryAEAD::BIP151_PayloadType::EncInit:
      case ArmoryAEAD::BIP151_PayloadType::EncAck:
      case ArmoryAEAD::BIP151_PayloadType::Rekey:
      case ArmoryAEAD::BIP151_PayloadType::Challenge:
      case ArmoryAEAD::BIP151_PayloadType::Reply:
      case ArmoryAEAD::BIP151_PayloadType::Propose:
         return parseMessageWithoutId(dataSlice);

      default:
         LOGERR << "invalid packet type";
   }

   return false;
}

///////////////////////////////////////////////////////////////////////////////
bool WebSocketMessagePartial::parseSinglePacket(const BinaryDataRef& bdr)
{
   /*
   uint8_t type(WS_MSGTYPE_SINGLEPACKET)
   uint32_t msgid
   nbytes payload
   */

   if (id_ != UINT32_MAX) {
      return false;
   }
   BinaryRefReader brr(bdr);
   brr.advance(4);

   type_ = (ArmoryAEAD::BIP151_PayloadType)brr.get_uint8_t();
   if (type_ != ArmoryAEAD::BIP151_PayloadType::SinglePacket) {
      return false;
   }

   id_ = brr.get_uint32_t();
   brr.resetPosition();
   brr.advance(PAYLOAD_HEADER);
   packets_.emplace(0, brr.get_BinaryDataRef(brr.getSizeRemaining()));

   packetCount_ = 1;
   return true;
}

///////////////////////////////////////////////////////////////////////////////
bool WebSocketMessagePartial::parseFragmentedMessageHeader(
   const BinaryDataRef& bdr)
{
   /*
   uint8_t type (WS_MSGTYPE_FRAGMENTEDPACKET_HEADER)
   uint32_t msgid
   uint16_t count (>= 2)
   nbytes payload fragment
   */

   BinaryRefReader brr(bdr);

   type_ = (ArmoryAEAD::BIP151_PayloadType)brr.get_uint8_t();
   if (type_ != ArmoryAEAD::BIP151_PayloadType::FragmentHeader) {
      return false;
   }

   auto id = brr.get_uint32_t();
   if (id_ != UINT32_MAX && id_ != id) {
      return false;
   }
   id_ = id;

   packetCount_ = brr.get_uint16_t();
   packets_.emplace(0, brr.get_BinaryDataRef(brr.getSizeRemaining()));
   return true;
}

///////////////////////////////////////////////////////////////////////////////
bool WebSocketMessagePartial::parseMessageFragment(const BinaryDataRef& bdr)
{
   /*
   uint8_t type (WS_MSGTYPE_FRAGMENTEDPACKET_FRAGMENT)
   uint32_t msgid
   varint packet id (1 to 65535)
   nbytes payload fragment
   */

   BinaryRefReader brr(bdr);

   auto type = (ArmoryAEAD::BIP151_PayloadType)brr.get_uint8_t();
   if (type != ArmoryAEAD::BIP151_PayloadType::FragmentPacket) {
      return false;
   }

   auto id = brr.get_uint32_t();
   if (id_ != UINT32_MAX && id_ != id) {
      return false;
   }
   id_ = id;

   auto packetId = (uint16_t)brr.get_var_int();
   packets_.emplace(packetId, brr.get_BinaryDataRef(brr.getSizeRemaining()));
   return true;
}

///////////////////////////////////////////////////////////////////////////////
bool WebSocketMessagePartial::parseMessageWithoutId(const BinaryDataRef& bdr)
{
   /*
   uint8_t type
   nbytes payload
   */

   BinaryRefReader brr(bdr);

   type_ = (ArmoryAEAD::BIP151_PayloadType)brr.get_uint8_t();
   if (type_ <= ArmoryAEAD::BIP151_PayloadType::Threshold_Begin) {
      return false;
   }

   packets_.emplace(0, brr.get_BinaryDataRef(brr.getSizeRemaining()));
   packetCount_ = 1;
   return true;
}

///////////////////////////////////////////////////////////////////////////////
std::unique_ptr<capnp::MessageReader> WebSocketMessagePartial::getReader() const
{
   if (!isReady()) {
      return nullptr;
   }

   if (packets_.size() == 1) {
      auto& dataRef = packets_.begin()->second;
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(dataRef.getPtr()),
         dataRef.getSize() / sizeof(capnp::word));
      return std::make_unique<capnp::FlatArrayMessageReader>(words);
   } else {
      return WebSocketMessageCodec::getFragmentedReader(packets_);
   }
}

///////////////////////////////////////////////////////////////////////////////
bool WebSocketMessagePartial::isReady() const
{
   return packets_.size() == packetCount_;
}

///////////////////////////////////////////////////////////////////////////////
BinaryDataRef WebSocketMessagePartial::getSingleBinaryMessage(void) const
{
   if (packetCount_ != 1 || !isReady()) {
      return {};
   }
   return packets_.begin()->second;
}

///////////////////////////////////////////////////////////////////////////////
ArmoryAEAD::BIP151_PayloadType WebSocketMessagePartial::getPacketType(
   const BinaryDataRef& bdr)
{
   if (bdr.getSize() < 5) {
      throw std::runtime_error("packet is too small to be serialized fragment");
   }
   return (ArmoryAEAD::BIP151_PayloadType)bdr.getPtr()[4];
}

///////////////////////////////////////////////////////////////////////////////
unsigned WebSocketMessagePartial::getMessageId(const BinaryDataRef& bdr)
{
   if (bdr.getSize() < 9) {
      return UINT32_MAX;
   }

   BinaryRefReader brr(bdr);
   brr.advance(4);

   auto type = (ArmoryAEAD::BIP151_PayloadType)brr.get_uint8_t();
   switch (type)
   {
      case ArmoryAEAD::BIP151_PayloadType::SinglePacket:
      case ArmoryAEAD::BIP151_PayloadType::FragmentHeader:
      case ArmoryAEAD::BIP151_PayloadType::FragmentPacket:
         return brr.get_uint32_t();

      default:
         return UINT32_MAX;
   }
   return UINT32_MAX;
}