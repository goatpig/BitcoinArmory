////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/*******************
client outSession -> server inSession
client inSession  <- server OutSession

session keys are ephemeral
auth keys are static and preshared
order of sequence is strict

AEAD sequence:

+++
server:  . present [auth public key] (for public servers only)
         . enc init: 
            send server's [outSession pubkey]

---
client:  . process enc init: 
            create inSession symmetrical encryption key with 
            [server outSession pubkey] and [own inSession privkey]
         
         . enc ack:
            send [own inSession pubkey]

         . enc init:
            send [own outSession pubkey]

+++
server:  . process enc ack:
            create outSession sym key ([own outSession privkey] * [client inSession pubkey])
         
         . process enc init:
            create inSession sym key ([own inSession privkey]  * [client outSession pubkey])

         . enc ack:
            send [own inSession pubkey]

         . mark shared encryption key setup as completed

---
client:  . process enc ack:
            create outSession sym key ([own outSession privkey] * [server inSession pubkey])

         . mark shared encryption key setup as completed

      ***********************************
      ** ENCRYPT ALL TRAFFIC FROM HERE **
      ***********************************

         . auth challenge:
            send hash(outSession.id | 'i' | [server auth pubkey]))

+++
server:  . process auth challenge:
            check hash(inSession.id | 'i' | [own auth pubkey]) matches challenge

         . auth reply:
            send sign(outSession.id, [own auth privkey])

---
client:  . process auth reply:
            verify sig(inSession.id, [server auth pubkey])

      ********************************
   ***** 2-WAY AUTH HANDSHAKE BEGIN *****
      ********************************

---
client:  . auth propose:
            send hash(outSession.id | 'p' | [own auth pukbey])

+++
server:  . process auth propose:
            cycle through all known client pubkeys, generate hash(inSession.id | 'p' | [known client pubkey])
            check result vs auth propose hash
               -> select match as chosenPeerKey
               -> fail if no match, drop connection

         . auth challenge:
            send hash(outSession.id | 'r' | [chosenPeerKey])

---
client:  . process auth challenge:
            check hash(inSession.id | 'r' | [own auth pubkey]) matches challenge
               -> on failure, send auth reply before killing connection
         
         . send auth reply:
            send sign(outSession.id, [own auth privkey])
         
         . rekey
         . mark auth handshake as completed

+++
server:
         . process auth reply:
            verify sig(inSession.id, [chosenPeerKey])
         
         . rekey
         . mark auth handshake as completed

      ******************************
   ***** 2-WAY AUTH HANDSHAKE END *****
      ******************************



      ********************************
   ***** 1-WAY AUTH HANDSHAKE BEGIN *****
      ********************************
---
client:  . auth propose:
            send hash(outSession.id | 'p' | [0xFF **33])

+++
server:  . process auth propose
            check hash(inSession.id | 'p' | [0xFF **33]) vs propose
               -> fail on mismatch
                  do not allow 2-way auth with 1-way server, drop connection
               -> do not select a client pubkey
            
         . auth challenge:
            hash(outSession.id | 'r' | [0xFF **33])

---
client:  . process auth challenge:
            check hash(inSession.id | 'r' | [0xFF **33])
               -> on failure, send auth reply before killing connection
         
         . send auth reply:
            [own auth pubkey]
         
         . rekey
         . mark auth handshake as completed

+++
server:  . process auth reply:
            set chosenPeerKey
         
         . rekey
         . mark auth handshake as completed

      ******************************
   ***** 1-WAY AUTH HANDSHAKE END *****
      ******************************

********************/

#include "BIP15x_Handshake.h"
#include "BIP150_151.h"

using namespace std;
using namespace ArmoryAEAD;

////////////////////////////////////////////////////////////////////////////////
HandshakeState BIP15x_Handshake::serverSideHandshake(
   BIP151Connection* connPtr,
   BIP151_PayloadType msgType, const BinaryDataRef& msg,
   const WriteCallback& writeCb)
{
   switch (msgType)
   {
   case BIP151_PayloadType::Start:
   {
      //init bip151 handshake
      BinaryData encinitData(ENCINITMSGSIZE);
      if (connPtr->getEncinitData(
         encinitData.getPtr(), ENCINITMSGSIZE,
         BIP151SymCiphers::CHACHA20POLY1305_OPENSSH) != 0)
      {
         //failed to init handshake, kill connection
         return HandshakeState::Error_GetEncInit;
      }

      writeCb(encinitData.getRef(), BIP151_PayloadType::EncInit, false);
      break;
   }

   case BIP151_PayloadType::Rekey:
   {
      if (connPtr->getBIP150State() !=
         BIP150State::SUCCESS)
      {
         //can't rekey before auth, kill connection
         return HandshakeState::Error;
      }

      //process rekey
      if (connPtr->processEncack(
         msg.getPtr(), msg.getSize(), false) != 0)
      {
         //failed to rekey, kill connection
         LOGWARN << "failed to process rekey";
         return HandshakeState::Error_ProcessEncAck;
      }

      break;
   }

   case BIP151_PayloadType::EncInit:
   {
      //process client encinit
      if (connPtr->processEncinit(
         msg.getPtr(), msg.getSize(), false) != 0)
      {
         //failed to init handshake, kill connection
         return HandshakeState::Error_ProcessEncInit;
      }

      //return encack
      BinaryData encackData(BIP151PUBKEYSIZE);
      if (connPtr->getEncackData(
         encackData.getPtr(), BIP151PUBKEYSIZE) != 0)
      {
         //failed to init handshake, kill connection
         return HandshakeState::Error_GetEncAck;
      }

      writeCb(encackData.getRef(), BIP151_PayloadType::EncAck, false);
      break;
   }

   case BIP151_PayloadType::EncAck:
   {
      //process client encack
      if (connPtr->processEncack(
         msg.getPtr(), msg.getSize(), true) != 0)
      {
         //failed to init handshake, kill connection
         return HandshakeState::Error_ProcessEncAck;
      }

      break;
   }

   case BIP151_PayloadType::Challenge:
   {
      auto challengeResult = connPtr->processAuthchallenge(
         msg.getPtr(),
         msg.getSize(),
         true); //true: step #1 of 6

      if (challengeResult == -1)
      {
         //auth fail, kill connection
         return HandshakeState::Error_ProcessAuthChallenge;
      }

      BinaryData authreplyBuf(BIP151PRVKEYSIZE * 2);
      if (connPtr->getAuthreplyData(
         authreplyBuf.getPtr(),
         authreplyBuf.getSize(),
         true) == -1) //true: step #2 of 6
      {
         //auth setup failure, kill connection
         return HandshakeState::Error_GetAuthReply;
      }

      writeCb(authreplyBuf.getRef(), BIP151_PayloadType::Reply, true);

      break;
   }

   case BIP151_PayloadType::Propose:
   {
      auto proposeResult = connPtr->processAuthpropose(
         msg.getPtr(),
         msg.getSize());

      if (proposeResult == -1)
      {
         //auth setup failure, kill connection
         return HandshakeState::Error_ProcessAuthPropose;
      }

      BinaryData authchallengeBuf(BIP151PRVKEYSIZE);
      if (connPtr->getAuthchallengeData(
         authchallengeBuf.getPtr(),
         authchallengeBuf.getSize(),
         "", //empty string, use chosen key from processing auth propose
         //false: step #4 of 6
         false) == -1)
      {
         //auth setup failure, kill connection
         return HandshakeState::Error_GetAuthChallenge;
      }

      writeCb(authchallengeBuf.getRef(), BIP151_PayloadType::Challenge, true);

      break;
   }

   case BIP151_PayloadType::Reply:
   {
      if (connPtr->processAuthreply(
         msg.getPtr(),
         msg.getSize(),
         false) != 0)
      {
         //invalid auth setup, kill connection
         return HandshakeState::Error_ProcessAuthReply;
      }

      //rekey after succesful BIP150 handshake
      connPtr->bip150HandshakeRekey();

      //handshake successful
      return HandshakeState::Completed;
   }

   default:
      //unexpected msg id, kill connection
      return HandshakeState::Error;
   }

   return HandshakeState::StepSuccessful;
}

////////////////////////////////////////////////////////////////////////////////
HandshakeState BIP15x_Handshake::clientSideHandshake(
   BIP151Connection* connPtr, const string& servName,
   BIP151_PayloadType msgType, const BinaryDataRef& msg,
   const WriteCallback& writeCb)
{
   if (connPtr == nullptr)
      return HandshakeState::Error;
   
   switch (msgType)
   {
   case BIP151_PayloadType::EncInit:
   {
      if (connPtr->processEncinit(
         msg.getPtr(), msg.getSize(), false) != 0)
         return HandshakeState::Error_ProcessEncInit;

      //valid encinit, send client side encack
      BinaryData encackPayload(BIP151PUBKEYSIZE);
      if (connPtr->getEncackData(
         encackPayload.getPtr(), BIP151PUBKEYSIZE) != 0)
      {
         return HandshakeState::Error_GetEncAck;
      }

      writeCb(encackPayload, BIP151_PayloadType::EncAck, false);

      //start client side encinit
      BinaryData encinitPayload(ENCINITMSGSIZE);
      if (connPtr->getEncinitData(
         encinitPayload.getPtr(), ENCINITMSGSIZE,
         BIP151SymCiphers::CHACHA20POLY1305_OPENSSH) != 0)
      {
         return HandshakeState::Error_GetEncInit;
      }

      writeCb(encinitPayload, BIP151_PayloadType::EncInit, false);
      break;
   }

   case BIP151_PayloadType::EncAck:
   {
      if (connPtr->processEncack(
         msg.getPtr(), msg.getSize(), true) == -1)
      {
         return HandshakeState::Error_ProcessEncAck;
      }

      //bip151 handshake completed, time for bip150
      BinaryData authchallengeBuf(BIP151PRVKEYSIZE);
      if (connPtr->getAuthchallengeData(
         authchallengeBuf.getPtr(),
         authchallengeBuf.getSize(),
         servName,
         true) != 0) //true: auth challenge step #1 of 6
      {
         return HandshakeState::Error_GetAuthChallenge;
      }

      writeCb(authchallengeBuf, BIP151_PayloadType::Challenge, true);
      break;
   }

   case BIP151_PayloadType::Rekey:
   {
      //rekey requests before auth are invalid
      if (connPtr->getBIP150State() != BIP150State::SUCCESS)
         return HandshakeState::Error;

      //if connection is already setup, we only accept enack rekey messages
      if (connPtr->processEncack(msg.getPtr(), msg.getSize(), false) == -1)
         return HandshakeState::Error_ProcessEncAck;

      return HandshakeState::RekeySuccessful;
   }

   case BIP151_PayloadType::Reply:
   {
      if (connPtr->processAuthreply(
         msg.getPtr(),
         msg.getSize(),
         true) != 0) //true: step #2 out of 6
      {
         return HandshakeState::Error_ProcessAuthReply;
      }

      BinaryData authproposeBuf(BIP151PRVKEYSIZE);
      if (connPtr->getAuthproposeData(
         authproposeBuf.getPtr(),
         authproposeBuf.getSize()) != 0)
      {
         return HandshakeState::Error_GetAuthPropose;
      }

      writeCb(authproposeBuf, BIP151_PayloadType::Propose, true);
      break;
   }

   case BIP151_PayloadType::Challenge:
   {
      //should return a reply packet to the server even if this step fails

      auto challengeResult =
         connPtr->processAuthchallenge(
            msg.getPtr(),
            msg.getSize(),
            false); //false: step #4 of 6

      BinaryData authreplyBuf(BIP151PRVKEYSIZE * 2);
      auto validReply = connPtr->getAuthreplyData(
         authreplyBuf.getPtr(),
         authreplyBuf.getSize(),
         false); //false: step #5 of 6

      writeCb(authreplyBuf, BIP151_PayloadType::Reply, true);

      if (challengeResult == -1)
      {
         //auth fail, kill connection
         return HandshakeState::Error_ProcessAuthChallenge;
      }      
      else if (validReply != 0)
      {
         //auth setup failure, kill connection
         return HandshakeState::Error_GetAuthReply;
      }

      //rekey
      connPtr->bip150HandshakeRekey();

      //handshake done, connection is ready
      return HandshakeState::Completed;
   }

   default:
      return HandshakeState::Error;
   }

   return HandshakeState::StepSuccessful;
}
