////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_STRING_SOCKETS
#define _H_STRING_SOCKETS

#include <string.h>
#include "SocketObject.h"
#include "FcgiMessage.h"

typedef vector<uint8_t>::iterator vecIter;

///////////////////////////////////////////////////////////////////////////////
struct HttpError : public SocketError
{
public:
   HttpError(const string& e) : SocketError(e)
   {}
};

///////////////////////////////////////////////////////////////////////////////
struct FcgiError : public SocketError
{
public:
   FcgiError(const string& e) : SocketError(e)
   {}
};

///////////////////////////////////////////////////////////////////////////////
class HttpSocket : public SimpleSocket
{
private:
   struct PacketData
   {
      vector<uint8_t> httpData_;
      int content_length_ = -1;
      size_t header_len_ = SIZE_MAX;

      void clear(void)
      {
         if (content_length_ == -1 || header_len_ == SIZE_MAX)
         {
            httpData_.clear();
         }
         else
         {
            if (content_length_ + header_len_ > httpData_.size())
            {
               vector<uint8_t> leftOverData;
               leftOverData.insert(leftOverData.end(),
                  move_iterator<vecIter>(httpData_.begin() + 
                     header_len_ + content_length_),
                  move_iterator<vecIter>(httpData_.end()));
               httpData_ = move(leftOverData);
            }
            else
            {
               httpData_.clear();
            }
         }

         content_length_ = -1;
         header_len_ = SIZE_MAX;
      }

      void get_content_len(const string& header_str)
      {
         string err504("HTTP/1.1 504");
         if (header_str.compare(0, err504.size(), err504) == 0)
            throw HttpError("connection timed out");

         string search_tok_caps("Content-Length: ");
         auto tokpos = header_str.find(search_tok_caps);
         if (tokpos != string::npos)
         {
            content_length_ = atoi(header_str.c_str() +
               tokpos + search_tok_caps.size());
            return;
         }

         string search_tok("content-length: ");
         tokpos = header_str.find(search_tok);
         if (tokpos != string::npos)
         {
            content_length_ = atoi(header_str.c_str() +
               tokpos + search_tok.size());
            return;
         }
      }
   };

private:
   PacketData currentRead_;
   unique_ptr<HttpMessage> messageWithPrecacheHeaders_;
   Queue<shared_ptr<Socket_ReadPayload>> readStack_;

protected:
   void addReadPayload(shared_ptr<Socket_ReadPayload>);
   string getHttpPayload(const char*, size_t);

public:
   HttpSocket(const string& addr, const string& port);

   static size_t getHttpBodyOffset(const char*, size_t);
   virtual SocketType type(void) const { return SocketHttp; }
   virtual bool processPacket(vector<uint8_t>&, vector<uint8_t>&);

   virtual void pushPayload(
      unique_ptr<Socket_WritePayload>,
      shared_ptr<Socket_ReadPayload>);
   virtual void respond(vector<uint8_t>&);

   void precacheHttpHeader(string& header) 
   {
      messageWithPrecacheHeaders_->addHeader(move(header));
   }
};

///////////////////////////////////////////////////////////////////////////////
class FcgiSocket : public HttpSocket
{
private:
   struct PacketStruct
   {
      vector<uint8_t> httpData_;

      int endpacket = 0;
      size_t ptroffset = 0;

      BinaryDataRef getHttpBody(void)
      {
         if (httpData_.size() == 0)
            return BinaryDataRef();

         auto offset = HttpSocket::
            getHttpBodyOffset((char*)&httpData_[0], httpData_.size());
         
         BinaryDataRef httpBody(&httpData_[0] + offset, 
            httpData_.size() - offset);
         
         return httpBody;
      }
   };

   map<uint16_t, PacketStruct> packetMap_;
   vector<uint8_t> leftOver_;

private:
   PacketStruct& getPacketStruct(uint16_t);
   void deletePacketStruct(uint16_t);

public:
   FcgiSocket(const string& addr, const string& port);
   SocketType type(void) const { return SocketFcgi; }

   bool processPacket(vector<uint8_t>&, vector<uint8_t>&);
   void pushPayload(
      unique_ptr<Socket_WritePayload>,
      shared_ptr<Socket_ReadPayload>);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_HttpBody : public CallbackReturn
{
private:
   function<void(string)> userCallbackLambda_;

public:
   CallbackReturn_HttpBody(function<void(string)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(BinaryDataRef);
};
#endif
