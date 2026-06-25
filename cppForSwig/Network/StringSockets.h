////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string.h>
#include "SocketObject.h"
#include "Utils/HttpMessage.h"

typedef std::vector<uint8_t>::iterator vecIter;

namespace Armory
{
   namespace Network
   {
      ////////
      struct HttpError : public SocketError
      {
      public:
         HttpError(const std::string& e) : SocketError(e)
         {}
      };

      class HttpSocket : public SimpleSocket
      {
      private:
         struct PacketData
         {
            std::vector<uint8_t> httpData_;
            int content_length_ = -1;
            size_t header_len_ = SIZE_MAX;

            void clear(void)
            {
               if (content_length_ == -1 || header_len_ == SIZE_MAX) {
                  httpData_.clear();
               } else {
                  if (content_length_ + header_len_ > httpData_.size()) {
                     std::vector<uint8_t> leftOverData;
                     leftOverData.insert(leftOverData.end(),
                        std::move_iterator<vecIter>(httpData_.begin() +
                           header_len_ + content_length_),
                        std::move_iterator<vecIter>(httpData_.end()));
                     httpData_ = move(leftOverData);
                  } else {
                     httpData_.clear();
                  }
               }
               content_length_ = -1;
               header_len_ = SIZE_MAX;
            }

            void get_content_len(const std::string& header_str)
            {
               std::string err504("HTTP/1.1 504");
               if (header_str.compare(0, err504.size(), err504) == 0) {
                  throw HttpError("connection timed out");
               }

               std::string search_tok_caps("Content-Length: ");
               auto tokpos = header_str.find(search_tok_caps);
               if (tokpos != std::string::npos) {
                  content_length_ = atoi(header_str.c_str() +
                     tokpos + search_tok_caps.size());
                  return;
               }

               std::string search_tok("content-length: ");
               tokpos = header_str.find(search_tok);
               if (tokpos != std::string::npos) {
                  content_length_ = atoi(header_str.c_str() +
                     tokpos + search_tok.size());
                  return;
               }
            }
         };

      private:
         PacketData currentRead_;
         std::unique_ptr<HttpMessage> messageWithPrecacheHeaders_;
         Threading::Queue<std::shared_ptr<Socket_ReadPayload>> readStack_;

      protected:
         void addReadPayload(std::shared_ptr<Socket_ReadPayload>);
         std::string getHttpPayload(const char*, size_t);

      public:
         HttpSocket(const std::string& addr, const std::string& port);

         static size_t getHttpBodyOffset(const char*, size_t);
         SocketType type(void) const override;
         bool processPacket(std::vector<uint8_t>&, std::vector<uint8_t>&);

         void pushPayload(
            std::unique_ptr<Socket_WritePayload>,
            std::shared_ptr<Socket_ReadPayload>) override;
         void respond(std::vector<uint8_t>&);

         void precacheHttpHeader(std::string& header)
         {
            messageWithPrecacheHeaders_->addHeader(std::move(header));
         }
      };

      ////////
      struct CallbackReturn_HttpBody : public CallbackReturn
      {
      private:
         std::function<void(std::string)> userCallbackLambda_;

      public:
         CallbackReturn_HttpBody(std::function<void(std::string)> lbd) :
            userCallbackLambda_(lbd)
         {}

         //virtual
         void callback(BinaryDataRef);
      };
   } //namespace Network
} //namespace Armory
