////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "StringSockets.h"
#include "SocketWritePayload.h"
#include "bdmenums.h"

using namespace Armory;
using namespace Armory::Network;

///////////////////////////////////////////////////////////////////////////////
// HttpSocket
HttpSocket::HttpSocket(const std::string& addr, const std::string& port) :
   SimpleSocket(addr, port)
{
   messageWithPrecacheHeaders_ = make_unique<HttpMessage>(getAddrStr());
}

SocketType HttpSocket::type() const
{
   return SocketType::Http;
}

size_t HttpSocket::getHttpBodyOffset(const char* ptr, size_t len)
{
   for (unsigned i = 0; i < len; i++) {
      if (ptr[i] == '\r') {
         if (len - i < 3) {
            break;
         }
         if (ptr[i + 1] == '\n' &&
            ptr[i + 2] == '\r' &&
            ptr[i + 3] == '\n') {
            return i + 4;
            break;
         }
      }
   }
   return SIZE_MAX;
}

////////
bool HttpSocket::processPacket(
   std::vector<uint8_t>& packet, std::vector<uint8_t>& payload)
{
   /***TODO: avoid copying packets into a sequential buffer before 
       finding body offset and length***/

   //puts packets together till a full http payload is achieved. returns false
   //if there's no payload yet. return true and sets value in &payload if 
   //the full http message was received

   if (packet.empty()) {
      return false;
   }

   //copy the data locally
   auto& httpData = currentRead_.httpData_;
   httpData.insert(
      httpData.end(), packet.begin(), packet.end());

   //if content_length is -1, we have not read the content-length in the
   //http header yet, let's find that
   if (currentRead_.content_length_ == -1) {
      currentRead_.header_len_ = getHttpBodyOffset(
         (char*)&httpData[0], httpData.size());

      //we have not found the header terminator yet, keep reading
      if (currentRead_.header_len_ == SIZE_MAX) {
         return false;
      }
      std::string header_str((char*)&httpData[0], currentRead_.header_len_);
      currentRead_.get_content_len(header_str);
   }

   //no content-length header was found, abort
   if (currentRead_.content_length_ == -1) {
      throw HttpError("failed to find http header response packet");
   }

   //check the total amount of data accumulated matches the advertised
   //content-length
   if (httpData.size() >= currentRead_.content_length_ + currentRead_.header_len_) {
      //set result ptr
      payload.insert(payload.end(),
         httpData.begin() + currentRead_.header_len_,
         httpData.begin() + currentRead_.header_len_ + currentRead_.content_length_);
      currentRead_.clear();

      //return true to exit the read loop
      return true;
   }
   return false;
}

////////
std::string HttpSocket::getHttpPayload(const char* ptr, size_t len)
{
   /*TODO: 
      return value is a vector but http serialization method takes
      char**. At least one copy can be avoided by unifying the output types.
      HttpSocket is used by NodeRPC, which in turn can get a lot of data to 
      deal with if there's a constant stream of transactions to push to the
      node. Optimizing this copy out will save some performance.
   */

   //create http packet
   char* http_payload;
   auto payload_len = 
      messageWithPrecacheHeaders_->makeHttpPayload(&http_payload, ptr, len);

   std::string payload(http_payload, payload_len);
   delete[] http_payload;
   return payload;
}

////////
void HttpSocket::addReadPayload(std::shared_ptr<Socket_ReadPayload> read_payload)
{
   if (read_payload == nullptr) {
      return;
   }
   readStack_.push_back(std::move(read_payload));
}

////////
void HttpSocket::pushPayload(
   std::unique_ptr<Socket_WritePayload> write_payload,
   std::shared_ptr<Socket_ReadPayload> read_payload)
{
   auto&& str = write_payload->serializeToText();
   auto strPayload = std::make_unique<WritePayload_StringPassthrough>();
   strPayload->data_ = std::move(getHttpPayload(str.c_str(), str.size()));

   SimpleSocket::pushPayload(std::move(strPayload), read_payload);
}

void HttpSocket::respond(std::vector<uint8_t>& payload)
{
   try {
      auto callback = readStack_.pop_front();
      
      BinaryDataRef bdr;
      if (!payload.empty()) {
         bdr.setRef(&payload[0], payload.size());
      }
      callback->callbackReturn_->callback(bdr);
   } catch (Threading::IsEmpty&) {}
}

///////////////////////////////////////////////////////////////////////////////
// CallbackReturn_HttpBody
void CallbackReturn_HttpBody::callback(BinaryDataRef ref)
{
   std::string body;
   if (!ref.empty()) {
      auto offset = HttpSocket::getHttpBodyOffset(
         ref.toCharPtr(), ref.getSize());

      if (offset != SIZE_MAX)
         body = std::move(std::string(
            ref.toCharPtr() + offset, ref.getSize() - offset));
   }
   userCallbackLambda_(std::move(body));
}
