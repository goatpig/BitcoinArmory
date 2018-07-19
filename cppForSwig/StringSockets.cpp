////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "StringSockets.h"

///////////////////////////////////////////////////////////////////////////////
//
// HttpSocket
//
///////////////////////////////////////////////////////////////////////////////
HttpSocket::HttpSocket(const string& addr, const string& port) :
   PersistentSocket(addr, port)
{
   messageWithPrecacheHeaders_ = make_unique<HttpMessage>(getAddrStr());
}

///////////////////////////////////////////////////////////////////////////////
size_t HttpSocket::getHttpBodyOffset(const char* ptr, size_t len)
{
   for (unsigned i = 0; i < len; i++)
   {
      if (ptr[i] == '\r')
      {
         if (len - i < 3)
            break;

         if (ptr[i + 1] == '\n' &&
            ptr[i + 2] == '\r' &&
            ptr[i + 3] == '\n')
         {
            return i + 4;
            break;
         }
      }
   }

   return SIZE_MAX;
}

///////////////////////////////////////////////////////////////////////////////
bool HttpSocket::processPacket(
   vector<uint8_t>& packet, vector<uint8_t>& payload)
{
   //puts packets together till a full http payload is achieved. returns false
   //if there's no payload yet. return true and sets value in &payload if 
   //the full http message was received

   if (packet.size() == 0)
      return false;

   //copy the data locally
   auto& httpData = currentRead_.httpData_;
   httpData.insert(
      httpData.end(), packet.begin(), packet.end());

   //if content_length is -1, we have not read the content-length in the
   //http header yet, let's find that
   if (currentRead_.content_length_ == -1)
   {
      currentRead_.header_len_ = getHttpBodyOffset(
         (char*)&httpData[0], httpData.size());

      //we have not found the header terminator yet, keep reading
      if (currentRead_.header_len_ == SIZE_MAX)
         return false;

      string header_str((char*)&httpData[0], currentRead_.header_len_);
      currentRead_.get_content_len(header_str);
   }

   //no content-length header was found, abort
   if (currentRead_.content_length_ == -1)
      throw HttpError("failed to find http header response packet");

   //check the total amount of data accumulated matches the advertised
   //content-length
   if (httpData.size() >= currentRead_.content_length_ + currentRead_.header_len_)
   {
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

///////////////////////////////////////////////////////////////////////////////
vector<uint8_t> HttpSocket::getHttpPayload(const char* ptr, size_t len)
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
   
   vector<uint8_t> payload;
   payload.reserve(payload_len);
   payload.insert(payload.end(), 
      (uint8_t*)http_payload, 
      (uint8_t*)(http_payload + payload_len));

   delete[] http_payload;
   return payload;
}

///////////////////////////////////////////////////////////////////////////////
void HttpSocket::addReadPayload(shared_ptr<Socket_ReadPayload> read_payload)
{
   if (read_payload == nullptr)
      return;

   readStack_.push_back(move(read_payload));
}

///////////////////////////////////////////////////////////////////////////////
void HttpSocket::pushPayload(
   unique_ptr<Socket_WritePayload> write_payload,
   shared_ptr<Socket_ReadPayload> read_payload)
{
   auto&& str = write_payload->serializeToText();
   auto&& payload = getHttpPayload(str.c_str(), str.size());

   addReadPayload(read_payload);
   queuePayloadForWrite(payload);
}

///////////////////////////////////////////////////////////////////////////////
void HttpSocket::respond(vector<uint8_t>& payload)
{
   try
   {
      auto&& callback = readStack_.pop_front();
      
      BinaryDataRef bdr;
      if (payload.size() != 0)
         bdr.setRef(&payload[0], payload.size());

      callback->callbackReturn_->callback(bdr);
   }
   catch(IsEmpty&)
   { }
}

///////////////////////////////////////////////////////////////////////////////
//
// FcgiSocket
//
///////////////////////////////////////////////////////////////////////////////
FcgiSocket::FcgiSocket(const string& addr, const string& port) :
   HttpSocket(addr, port)
{}

///////////////////////////////////////////////////////////////////////////////
bool FcgiSocket::processPacket(
   vector<uint8_t>& packet, vector<uint8_t>& payload)
{
   if (packet.size() == 0)
      return false;

   vector<uint8_t> packetToProcess;
   if (leftOver_.size() > 0)
   {
      leftOver_.insert(leftOver_.end(), packet.begin(), packet.end());
      packetToProcess = move(leftOver_);
      leftOver_.clear();
   }
   else
   {
      packetToProcess = move(packet);
   }

   size_t offset = 0;
   while (offset + FCGI_HEADER_LEN <=
      packetToProcess.size())
   {
      //grab fcgi header
      auto* fcgiheader = &packetToProcess[offset];
      offset += FCGI_HEADER_LEN;

      //make sure fcgi version id is valid, otherwise we are misaligned
      if (fcgiheader[0] != FCGI_VERSION_1)
         continue;

      uint16_t requestid;
      requestid = (uint8_t)fcgiheader[3] + (uint8_t)fcgiheader[2] * 256;

      auto& packetStruct = getPacketStruct(requestid);

      //check packet type
      bool abortParse = false;
      switch (fcgiheader[1])
      {
      case FCGI_END_REQUEST:
      {
         offset += FCGI_HEADER_LEN;
         //save data left over in packet if any
         if (packetToProcess.size() > offset)
         {
            leftOver_.insert(leftOver_.end(),
               move_iterator<vecIter>(packetToProcess.begin() + offset),
               move_iterator<vecIter>(packetToProcess.end()));
         }

         //completed request, return with payload
         auto&& bodyRef = packetStruct.getHttpBody();
         if (bodyRef.getSize() > 0)
         {
            BinaryData bodyBin(bodyRef);
            payload = move(bodyBin.release());
         }

         deletePacketStruct(requestid);
         return true;
      }

      case FCGI_STDOUT:
      {
         //get packetsize and padding
         uint16_t packetsize = 0, padding;

         packetsize |= (uint8_t)fcgiheader[5];
         packetsize |= (uint16_t)(fcgiheader[4] << 8);
         padding = (uint8_t)fcgiheader[6];

         if (packetsize > 0)
         {
            //do not process this fcgi packet if we dont have enough
            //data in the read buffer to cover the advertized length
            //save that for the next call to processPacket
            if (packetsize + padding + offset >
               packetToProcess.size())
            {
               offset -= FCGI_HEADER_LEN;
               leftOver_.insert(leftOver_.end(),
                  move_iterator<vecIter>(packetToProcess.begin() + offset),
                  move_iterator<vecIter>(packetToProcess.end()));
               abortParse = true;
               break;
            }

            //extract http data
            packetStruct.httpData_.insert(packetStruct.httpData_.end(),
               packetToProcess.begin() + offset,
               packetToProcess.begin() + offset + packetsize);

            //advance index to next header
            offset += packetsize + padding;
         }

         break;
      }

      case FCGI_ABORT_REQUEST:
      {
         deletePacketStruct(requestid);
         break;
      }

      //misaligned or unsupported request type
      default:
         continue;
      }

      if (abortParse)
         break;
   }

   return false;
}

///////////////////////////////////////////////////////////////////////////////
FcgiSocket::PacketStruct& FcgiSocket::getPacketStruct(uint16_t id)
{
   auto iter = packetMap_.find(id);
   if (iter != packetMap_.end())
      return iter->second;

   auto insertIter = packetMap_.insert(move(make_pair(id, PacketStruct())));
   return insertIter.first->second;
}

///////////////////////////////////////////////////////////////////////////////
void FcgiSocket::deletePacketStruct(uint16_t id)
{
   packetMap_.erase(id);
}

///////////////////////////////////////////////////////////////////////////////
void FcgiSocket::pushPayload(
   unique_ptr<Socket_WritePayload> write_payload,
   shared_ptr<Socket_ReadPayload> read_payload)
{
   stringstream data;
   uint16_t id = rand() % 65536;
   BinaryDataRef bdr((uint8_t*)&id, 2);

   if (read_payload != nullptr)
      read_payload->id_ = id;

   data << bdr.toHexStr();
   data << write_payload->serializeToText();
   auto&& str = data.str();

   BinaryDataRef dataRef((uint8_t*)str.c_str(), str.size());
   auto&& fcgi_message = FcgiMessage::makePacket(dataRef);
   auto&& payload = fcgi_message.serialize();

   addReadPayload(read_payload);
   queuePayloadForWrite(payload);
}

///////////////////////////////////////////////////////////////////////////////
//
// CallbackReturn_HttpBody
//
///////////////////////////////////////////////////////////////////////////////
void CallbackReturn_HttpBody::callback(BinaryDataRef ref)
{
   string body(ref.toCharPtr(), ref.getSize());
   userCallbackLambda_(move(body));
}
