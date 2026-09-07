////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2026, goatpig                                          //
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

         . send enc ack:
            present [own inSession pubkey]

         . send enc init:
            present [own outSession pubkey]

+++
server:  . process enc ack:
            create outSession sym key ([own outSession privkey] * [client inSession pubkey])

         . process enc init:
            create inSession sym key ([own inSession privkey]  * [client outSession pubkey])

         . send enc ack:
            present [own inSession pubkey]

         . mark shared encryption key setup as completed

---
client:  . process enc ack:
            create outSession sym key ([own outSession privkey] * [server inSession pubkey])

         . mark shared encryption key setup as completed

      ***********************************
      ** ENCRYPT ALL TRAFFIC FROM HERE **
      ***********************************

         . send auth challenge:
            present hash(outSession.id | 'i' | [server auth pubkey]))

+++
server:  . process auth challenge:
            check hash(inSession.id | 'i' | [own auth pubkey]) matches challenge

         . send auth reply:
            present sign(outSession.id, [own auth privkey])

---
client:  . process auth reply:
            verify sig(inSession.id, [server auth pubkey])

   ###################################################
   #### Peer authentication starts deviating here ####
   ###################################################

      ********************************
   ***** 2-WAY AUTH HANDSHAKE BEGIN *****
      ********************************

---
client:  . send auth propose:
            present hash(outSession.id | 'p' | [own auth pukbey])

+++
server:  . process auth propose:
            cycle through all known client pubkeys, generate hash(inSession.id | 'p' | [known client pubkey])
            check result vs auth propose hash
               -> select match as chosenPeerKey
               -> fail if no match, drop connection

         . send auth challenge:
            present hash(outSession.id | 'r' | [chosenPeerKey])

---
client:  . process auth challenge:
            check hash(inSession.id | 'r' | [own auth pubkey]) matches challenge
               -> on failure, send auth reply before killing connection

         . send auth reply:
            present sign(outSession.id, [own auth privkey])

         . rekey
         . mark auth handshake as completed

+++
server:  . process auth reply:
            verify sig(inSession.id, [chosenPeerKey])

         . rekey
         . mark auth handshake as completed

      ******************************
   ***** 2-WAY AUTH HANDSHAKE END *****
      ******************************


   ###################################################
   ### Pick up from deviation point for 1-way auth ###

      ********************************
   ***** 1-WAY AUTH HANDSHAKE BEGIN *****
      ********************************
---
client:  . send auth propose:
            present hash(outSession.id | 'p' | [0xFF **33])

+++
server:  . process auth propose
            check hash(inSession.id | 'p' | [0xFF **33]) vs propose
               -> fail on mismatch
                  do not allow 2-way auth with 1-way server, drop connection
               -> do not select a client pubkey

         . send auth challenge:
            present hash(outSession.id | 'r' | [0xFF **33])

---
client:  . process auth challenge:
            check hash(inSession.id | 'r' | [0xFF **33])
               -> on failure, send auth reply before killing connection

         . send auth reply:
            present [own auth pubkey]

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
      BinaryData encinitData;
      encinitData.resize(ENCINITMSGSIZE);
      if (connPtr->getEncinitData(
         encinitData, BIP151SymCiphers::CHACHA20POLY1305_OPENSSH) != 0) {
         //failed to init handshake, kill connection
         return HandshakeState::Error_GetEncInit;
      }

      writeCb(encinitData.getRef(), BIP151_PayloadType::EncInit, false);
      break;
   }

   case BIP151_PayloadType::Rekey:
   {
      if (connPtr->getBIP150State() != BIP150State::SUCCESS) {
         //can't rekey before auth, kill connection
         return HandshakeState::Error;
      }

      //process rekey
      if (connPtr->processEncack(msg, false) != 0) {
         //failed to rekey, kill connection
         LOGWARN << "failed to process rekey";
         return HandshakeState::Error_ProcessEncAck;
      }
      break;
   }

   case BIP151_PayloadType::EncInit:
   {
      //process client encinit
      if (connPtr->processEncinit(msg, false) != 0) {
         //failed to init handshake, kill connection
         return HandshakeState::Error_ProcessEncInit;
      }

      //return encack
      BinaryData encackData;
      encackData.resize(BIP151PUBKEYSIZE);
      if (connPtr->getEncackData(encackData) != 0) {
         //failed to init handshake, kill connection
         return HandshakeState::Error_GetEncAck;
      }

      writeCb(encackData.getRef(), BIP151_PayloadType::EncAck, false);
      break;
   }

   case BIP151_PayloadType::EncAck:
   {
      //process client encack
      if (connPtr->processEncack(msg, true) != 0) {
         //failed to init handshake, kill connection
         return HandshakeState::Error_ProcessEncAck;
      }

      break;
   }

   case BIP151_PayloadType::Challenge:
   {
      //true: step #1 of 6
      if (connPtr->processAuthchallenge(msg, true) == -1) {
         //auth fail, kill connection
         return HandshakeState::Error_ProcessAuthChallenge;
      }

      //true: step #2 of 6
      BinaryData authReply;
      authReply.resize(BIP151PRVKEYSIZE * 2);
      if (connPtr->getAuthreplyData(authReply, true) == -1) {
         //auth setup failure, kill connection
         return HandshakeState::Error_GetAuthReply;
      }
      writeCb(authReply, BIP151_PayloadType::Reply, true);
      break;
   }

   case BIP151_PayloadType::Propose:
   {
      if (connPtr->processAuthpropose(msg) == -1) {
         //auth setup failure, kill connection
         return HandshakeState::Error_ProcessAuthPropose;
      }

      BinaryData authchallengeBuf;
      authchallengeBuf.resize(BIP151PRVKEYSIZE);
      if (connPtr->getAuthchallengeData(
         authchallengeBuf,
         //empty string, use chosen key from processing auth propose
         "",
         //false: step #4 of 6
         false) == -1) {
         //auth setup failure, kill connection
         return HandshakeState::Error_GetAuthChallenge;
      }

      writeCb(authchallengeBuf.getRef(), BIP151_PayloadType::Challenge, true);

      break;
   }

   case BIP151_PayloadType::Reply:
   {
      //false: last step in server side handshake
      if (connPtr->processAuthreply(msg, false) != 0) {
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
   if (connPtr == nullptr) {
      return HandshakeState::Error;
   }

   switch (msgType)
   {
   case BIP151_PayloadType::EncInit:
   {
      if (connPtr->processEncinit(msg, false) != 0) {
         return HandshakeState::Error_ProcessEncInit;
      }

      //valid encinit, send client side encack
      BinaryData encackPayload;
      encackPayload.resize(BIP151PUBKEYSIZE);
      if (connPtr->getEncackData(encackPayload) != 0) {
         return HandshakeState::Error_GetEncAck;
      }

      writeCb(encackPayload, BIP151_PayloadType::EncAck, false);

      //start client side encinit
      BinaryData encinitPayload;
      encinitPayload.resize(ENCINITMSGSIZE);
      if (connPtr->getEncinitData(encinitPayload,
         BIP151SymCiphers::CHACHA20POLY1305_OPENSSH) != 0) {
         return HandshakeState::Error_GetEncInit;
      }

      writeCb(encinitPayload, BIP151_PayloadType::EncInit, false);
      break;
   }

   case BIP151_PayloadType::EncAck:
   {
      if (connPtr->processEncack(msg, true) == -1) {
         return HandshakeState::Error_ProcessEncAck;
      }

      //bip151 handshake completed, time for bip150
      //true: auth challenge step #1 of 6
      BinaryData authchallengeBuf;
      authchallengeBuf.resize(BIP151PRVKEYSIZE);
      if (connPtr->getAuthchallengeData(
         authchallengeBuf, servName, true) != 0) {
         return HandshakeState::Error_GetAuthChallenge;
      }

      writeCb(authchallengeBuf, BIP151_PayloadType::Challenge, true);
      break;
   }

   case BIP151_PayloadType::Rekey:
   {
      //rekey requests before auth are invalid
      if (connPtr->getBIP150State() != BIP150State::SUCCESS) {
         return HandshakeState::Error;
      }

      //if connection is already setup, we only accept enack rekey messages
      if (connPtr->processEncack(msg, false) == -1) {
         return HandshakeState::Error_ProcessEncAck;
      }
      return HandshakeState::RekeySuccessful;
   }

   case BIP151_PayloadType::Reply:
   {
      //true: step #2 out of 6
      if (connPtr->processAuthreply(msg, true) != 0) {
         return HandshakeState::Error_ProcessAuthReply;
      }

      BinaryData authproposeBuf;
      authproposeBuf.resize(BIP151PRVKEYSIZE);
      if (connPtr->getAuthproposeData(authproposeBuf) != 0) {
         return HandshakeState::Error_GetAuthPropose;
      }

      writeCb(authproposeBuf, BIP151_PayloadType::Propose, true);
      break;
   }

   case BIP151_PayloadType::Challenge:
   {
      //should return a reply packet to the server even if this step fails

      //false: step #4 of 6
      auto challengeResult = connPtr->processAuthchallenge(msg, false);

      //false: step #5 of 6
      BinaryData authReply;
      authReply.resize(BIP151PRVKEYSIZE * 2);
      auto validReply = connPtr->getAuthreplyData(authReply, false);
      writeCb(authReply, BIP151_PayloadType::Reply, true);

      if (challengeResult == -1) {
         //auth fail, kill connection
         return HandshakeState::Error_ProcessAuthChallenge;
      } else if (validReply != 0) {
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
