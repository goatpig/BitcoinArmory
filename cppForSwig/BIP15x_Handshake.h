////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _BIP15x_HANDSHAKE_H
#define _BIP15x_HANDSHAKE_H

#include <functional>
#include "BinaryData.h"

class BIP151Connection;

namespace ArmoryAEAD
{
enum HandshakeState
{
   //general error, for out of order sequence and setup snafus
   Error = 0,

   /*
   Handshake sequence step failures. These are for code readability/debuging.
   The specific error should not be returned to the requestor.
   */

   Error_GetEncInit,
   Error_ProcessEncInit,

   Error_GetEncAck,
   Error_ProcessEncAck,

   Error_GetAuthChallenge,
   Error_ProcessAuthChallenge,

   Error_GetAuthReply,
   Error_ProcessAuthReply,

   Error_GetAuthPropose,
   Error_ProcessAuthPropose,

   /*
   Success states
   */

   //handshake sequence step successful, proceed further
   StepSuccessful,
   
   //unit tests cover rekey counts (client side only)
   RekeySuccessful,

   //handshake success, channel encrypted and authenticated   
   Completed
};

enum class BIP151_PayloadType : uint8_t
{
   Undefined            = 0,

   SinglePacket         = 1,
   FragmentHeader       = 2,
   FragmentPacket       = 3,
   FirstSegment         = 4,
   Segment              = 5,

   Threshold_Begin      = 100,
   Start                = 101,
   PresentPubKey        = 102,
   PresentPubKeyChild   = 103, //unused

   Threshold_Enc        = 110,
   EncInit              = 111,
   EncAck               = 112,
   Rekey                = 113,

   Threshold_Auth       = 130,
   Challenge            = 131,
   Reply                = 132,
   Propose              = 133,

   Threshold_End        = 150
};

class BIP15x_Handshake
{
public:
   //args: msg, msg type, encrypt
   using WriteCallback = std::function<void(
      const BinaryData&, BIP151_PayloadType, bool)>;

   //args: bip15x object, ip:port, msg type, msg, write callback
   static HandshakeState serverSideHandshake(
      BIP151Connection*, BIP151_PayloadType,
      const BinaryDataRef&, const WriteCallback&);
   static HandshakeState clientSideHandshake(
      BIP151Connection*, const std::string&, BIP151_PayloadType,
      const BinaryDataRef&, const WriteCallback&);
};
}; //namespace ArmoryAEAD
#endif
