////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2024, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _SOCKET_WRITE_PAYLOAD_H
#define _SOCKET_WRITE_PAYLOAD_H

#include "SocketObject.h"

namespace capnp
{
   class MessageBuilder;
}

///////////////////////////////////////////////////////////////////////////////
struct WritePayload_Raw : public Socket_WritePayload
{
   std::vector<uint8_t> data;

   WritePayload_Raw(std::vector<uint8_t>& payload) :
      data(std::move(payload))
   {}

   WritePayload_Raw(WritePayload_Raw&& lhs) :
      data(std::move(lhs.data))
   {}

   void serialize(std::vector<uint8_t>&) override;
   std::string serializeToText(void) override
   {
      throw SocketError("raw payload cannot serialize to str");
   }

   size_t getSerializedSize(void) const override
   {
      return data.size();
   }
};

///////////////////////////////////////////////////////////////////////////////
struct WritePayload_String : public Socket_WritePayload
{
   std::string data_;

   void serialize(std::vector<uint8_t>&) override
   {
      throw SocketError("string payload cannot serialize to raw binary");
   }

   std::string serializeToText(void) override
   {
      return std::move(data_);
   }

   size_t getSerializedSize(void) const override
   {
      return data_.size();
   }
};

///////////////////////////////////////////////////////////////////////////////
struct WritePayload_StringPassthrough : public Socket_WritePayload
{
   std::string data_;

   void serialize(std::vector<uint8_t>& payload) override
   {
      payload.reserve(data_.size() + 1);
      payload.insert(payload.end(), data_.begin(), data_.end());
      data_.push_back(0);
   }

   std::string serializeToText(void) override
   {
      return move(data_);
   }

   size_t getSerializedSize(void) const override
   {
      return data_.size();
   };
};

///////////////////////////////////////////////////////////////////////////////
struct WritePayload_Capnp : public Socket_WritePayload
{
public:
   std::unique_ptr<capnp::MessageBuilder> builder;
   std::vector<uint8_t> firstSegment;

public:
   WritePayload_Capnp(
      std::unique_ptr<capnp::MessageBuilder>,
      std::vector<uint8_t>);
   ~WritePayload_Capnp(void);

   void serialize(std::vector<uint8_t>& payload) override;
   std::string serializeToText(void) override
   {
      throw SocketError("raw payload cannot serialize to str");
   }
   size_t getSerializedSize(void) const override;
   bool isSingleSegment(void) const override;
};

#endif
