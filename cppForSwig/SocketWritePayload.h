////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _SOCKET_WRITE_PAYLOAD_H
#define _SOCKET_WRITE_PAYLOAD_H

#include "SocketObject.h"

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
   std::string serializeToText(void) {
      throw SocketError("raw payload cannot seriliaze to str");
   }
   size_t getSerializedSize(void) const { return data.size(); };
};

///////////////////////////////////////////////////////////////////////////////
struct WritePayload_String : public Socket_WritePayload
{
   std::string data_;

   void serialize(std::vector<uint8_t>&) {
      throw SocketError("string payload cannot serialize to raw binary");
   }

   std::string serializeToText(void) {
      return std::move(data_);
   }

   size_t getSerializedSize(void) const { return data_.size(); };
};

///////////////////////////////////////////////////////////////////////////////
struct WritePayload_StringPassthrough : public Socket_WritePayload
{
   std::string data_;

   void serialize(std::vector<uint8_t>& payload) {
      payload.reserve(data_.size() + 1);
      payload.insert(payload.end(), data_.begin(), data_.end());
      data_.push_back(0);
   }

   std::string serializeToText(void) {
      return move(data_);
   }

   size_t getSerializedSize(void) const { return data_.size(); };
};

#endif
