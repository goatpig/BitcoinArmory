////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _WEBSOCKET_MESSAGE_H
#define _WEBSOCKET_MESSAGE_H

#include <stdexcept>
#include <string>
#include <memory>

#include "BinaryData.h"
#include "SocketObject.h"

#include "BIP150_151.h"

#define WEBSOCKET_MESSAGE_PACKET_SIZE 1500
#define WEBSOCKET_CALLBACK_ID 0xFFFFFFFE
#define WEBSOCKET_AEAD_HANDSHAKE_ID 0xFFFFFFFD
#define WEBSOCKET_MAGIC_WORD 0x56E1
#define AEAD_REKEY_INVERVAL_SECONDS 600

class LWS_Error : public std::runtime_error
{
public:
   LWS_Error(const std::string& err) :
      std::runtime_error(err)
   {}
};

namespace ArmoryAEAD
{
   enum class BIP151_PayloadType : uint8_t;
};

namespace capnp {
   class MessageReader;
}

///////////////////////////////////////////////////////////////////////////////
class WebSocketMessageCodec
{
public:
   static std::vector<BinaryData> serialize(
      const BinaryDataRef&, BIP151Connection*,
      ArmoryAEAD::BIP151_PayloadType, uint32_t);
   static std::vector<BinaryData> serialize(
      const std::vector<uint8_t>&, BIP151Connection*,
      ArmoryAEAD::BIP151_PayloadType, uint32_t);
   static std::vector<BinaryData> serialize(
      const std::string&, BIP151Connection*,
      ArmoryAEAD::BIP151_PayloadType, uint32_t);
   static std::vector<BinaryData> serializePacketWithoutId(
      const BinaryDataRef&, BIP151Connection*,
      ArmoryAEAD::BIP151_PayloadType);

   static uint32_t getMessageId(const BinaryDataRef&);

   static std::unique_ptr<capnp::MessageReader> getFragmentedReader(
      const std::map<uint16_t, BinaryDataRef>&);
};

///////////////////////////////////////////////////////////////////////////////
class SerializedMessage
{
private:
   mutable unsigned index_ = 0;
   std::vector<BinaryData> packets_;

public:
   SerializedMessage()
   {}

   void construct(const std::vector<uint8_t>& data, BIP151Connection*,
      ArmoryAEAD::BIP151_PayloadType, uint32_t id = 0);
   void construct(const BinaryDataRef& data, BIP151Connection*,
      ArmoryAEAD::BIP151_PayloadType, uint32_t id = 0);

   bool isDone(void) const { return index_ >= packets_.size(); }
   BinaryData consumeNextPacket(void);
   unsigned count(void) const { return packets_.size(); }
   void clear(void) { packets_.clear(); }
};

///////////////////////////////////////////////////////////////////////////////
class WebSocketMessagePartial
{
private:
   std::map<uint16_t, BinaryDataRef> packets_;

   uint32_t id_ = UINT32_MAX;
   ArmoryAEAD::BIP151_PayloadType type_;
   uint32_t packetCount_ = UINT32_MAX;

private:
   bool parseSinglePacket(const BinaryDataRef& bdr);
   bool parseFragmentedMessageHeader(const BinaryDataRef& bdr);
   bool parseMessageFragment(const BinaryDataRef& bdr);
   bool parseMessageWithoutId(const BinaryDataRef& bdr);

public:
   WebSocketMessagePartial(void);

   void reset(void);
   bool parsePacket(const BinaryDataRef&);
   bool isReady(void) const;
   std::unique_ptr<capnp::MessageReader> getReader(void) const;
   BinaryDataRef getSingleBinaryMessage(void) const;
   const uint32_t& getId(void) const { return id_; }
   ArmoryAEAD::BIP151_PayloadType getType(void) const { return type_; }

   const std::map<uint16_t, BinaryDataRef>& getPacketMap(void) const
   { return packets_; }

   static ArmoryAEAD::BIP151_PayloadType getPacketType(const BinaryDataRef&);
   static unsigned getMessageId(const BinaryDataRef&);
};

///////////////////////////////////////////////////////////////////////////////
class CallbackReturn_WebSocket : public CallbackReturn
{
   bool runInCaller_ = false;

private:
   void callback(BinaryDataRef) {} //this is so bad... redo this later!

public:
   virtual void callback(const WebSocketMessagePartial&) = 0;
   bool runInCaller(void) const { return runInCaller_; }
   void setRunInCaller(bool val) { runInCaller_ = val; }
};

#endif