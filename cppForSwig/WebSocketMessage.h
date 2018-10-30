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
#include <google/protobuf/message.h>
#include "SocketObject.h"

#define WEBSOCKET_MESSAGE_PACKET_SIZE 1500
#define WEBSOCKET_CALLBACK_ID 0xFFFFFFFE
#define WEBSOCKET_MAGIC_WORD 0x56E1

class LWS_Error : public runtime_error
{
public:
   LWS_Error(const string& err) :
      runtime_error(err)
   {}
};

///////////////////////////////////////////////////////////////////////////////
class WebSocketMessageCodec
{
public:
   static vector<BinaryData> serialize(uint32_t, const BinaryDataRef&);
   static vector<BinaryData> serialize(uint32_t, const vector<uint8_t>&);
   static vector<BinaryData> serialize(uint32_t, const string&);
   
   static uint32_t getMessageId(const BinaryDataRef&);
    
   static BinaryDataRef getSingleMessage(const BinaryDataRef&);
   static bool reconstructFragmentedMessage(
      const map<size_t, BinaryDataRef>&, 
      ::google::protobuf::Message*);
};

///////////////////////////////////////////////////////////////////////////////
class WebSocketMessage
{
private:
   mutable unsigned index_ = 0;
   vector<BinaryData> packets_;

public:
   WebSocketMessage()
   {}

   void construct(uint32_t msgid, vector<uint8_t> data);
   bool isDone(void) const { return index_ >= packets_.size(); }
   const BinaryData& getNextPacket(void) const;
};

///////////////////////////////////////////////////////////////////////////////
class WebSocketMessagePartial
{
private:
   map<size_t, BinaryDataRef> packets_;
   size_t pos_;

   uint32_t id_ = UINT32_MAX;
   size_t len_;

public:
   void reset(void);
   bool parsePacket(const size_t& id, const BinaryDataRef&);
   bool isReady(void) const { return pos_ == len_ && packets_.size() != 0; }
   bool getMessage(::google::protobuf::Message*) const;
   const uint32_t& getId(void) const { return id_; }

   const map<size_t, BinaryDataRef>& getPacketMap(void) const { return packets_; }
};

///////////////////////////////////////////////////////////////////////////////
class CallbackReturn_WebSocket : public CallbackReturn
{
private:
   void callback(BinaryDataRef bdr) {}

public:
   virtual void callback(const WebSocketMessagePartial&) = 0;
};

#endif

