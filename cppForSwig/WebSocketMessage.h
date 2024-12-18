////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2024, goatpig.                                         //
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

//64 bit aligned
#define WEBSOCKET_MESSAGE_PACKET_SIZE 1496
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

struct Socket_WritePayload;

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
   void construct(std::unique_ptr<Socket_WritePayload>, BIP151Connection*,
      uint32_t id = 0);

   bool isDone(void) const { return index_ >= packets_.size(); }
   BinaryData consumeNextPacket(void);
   unsigned count(void) const { return packets_.size(); }
   void clear(void) { packets_.clear(); }
};

///////////////////////////////////////////////////////////////////////////////
// capnp readers
///////////////////////////////////////////////////////////////////////////////
class CapnpReader
{
public:
   virtual ~CapnpReader(void) = 0;
   virtual std::unique_ptr<capnp::MessageReader> getReader(void) const = 0;
};

////
class CapnpFlatReader : public CapnpReader
{
   /*
   Message is a single sequential packet, read as is.
   */

private:
   BinaryData packet_;

public:
   CapnpFlatReader(std::map<uint32_t, BinaryData>);
   ~CapnpFlatReader(void) {}
   std::unique_ptr<capnp::MessageReader> getReader(void) const override;
};

////
class CapnpFragmentedReader : public CapnpReader
{
   /*
   Message is a set of fragments that has to be reconstructed
   into a as single sequential buffer to be read.
   */

private:
   BinaryData sequentialBuffer_;

public:
   CapnpFragmentedReader(std::map<uint32_t, BinaryData>);
   ~CapnpFragmentedReader(void) {}

   std::unique_ptr<capnp::MessageReader> getReader(void) const override;
};

////
class CapnpSegmentedReader : public CapnpReader
{
   /*
   Message is a set of capnp segments that has to read as an
   array of word arrays.
   */

private:
   std::map<uint32_t, BinaryData> segments_;
   mutable void* kjArrayPtr_ = nullptr;

public:
   CapnpSegmentedReader(std::map<uint32_t, BinaryData>);
   ~CapnpSegmentedReader(void);
   std::unique_ptr<capnp::MessageReader> getReader(void) const override;
};

///////////////////////////////////////////////////////////////////////////////
class WebSocketMessagePartial
{
   enum class SerializedType : int
   {
      Undefined   = 1,
      WithoutId   = 2,
      Single      = 3,
      Fragmented  = 4,
      Segmented   = 5
   };

private:
   std::map<uint32_t, BinaryData> packets_;

   uint32_t id_ = UINT32_MAX;
   ArmoryAEAD::BIP151_PayloadType type_;
   uint32_t packetCount_ = UINT32_MAX;

   SerializedType myType_ = SerializedType::Undefined;

private:
   bool parseSinglePacket(BinaryData&);
   bool parseFragmentedMessageHeader(BinaryData&);
   bool parseMessageFragment(BinaryData&);
   bool parseFirstSegment(BinaryData&);
   bool parseSegment(BinaryData&);
   bool parseMessageWithoutId(BinaryData&);

public:
   WebSocketMessagePartial(void);

   void reset(void);
   bool parsePacket(BinaryData&);
   bool isReady(void) const;
   std::unique_ptr<CapnpReader> getReader(void) const;
   BinaryDataRef getSingleBinaryMessage(void) const;
   bool isSegmented(void) const;
   const uint32_t& getId(void) const { return id_; }
   ArmoryAEAD::BIP151_PayloadType getType(void) const { return type_; }

   static ArmoryAEAD::BIP151_PayloadType readPacketType(const BinaryDataRef&);
   static uint32_t readMessageId(const BinaryData&);
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