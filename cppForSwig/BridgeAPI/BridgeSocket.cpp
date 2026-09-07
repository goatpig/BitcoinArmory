////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#include <map>

#include "BridgeSocket.h"
#include <Utils/BIP15x_Handshake.h>
#include <Utils/BIP150_151.h>
#include <Utils/ArmoryConfig.h>
#include <Wallets/AuthorizedPeers.h>

#include "CppBridge.h"
#include "ProtoCommandParser.h"
#include <Network/WebSocketMessage.h>

using namespace Armory;
using namespace Armory::Bridge;

#define BRIDGE_SOCKET_MAXLEN 1024 * 1024 * 1024 //1MB

////////////////////////////////////////////////////////////////////////////////
////
////  CppBridgeSocket
////
////////////////////////////////////////////////////////////////////////////////
CppBridgeSocket::CppBridgeSocket(
   const std::string& addr, const std::string& port,
   std::shared_ptr<CppBridge> bridgePtr) :
   PersistentSocket(addr, port), bridgePtr_(bridgePtr),
   serverName_(addr + ":" + port)
{
   //setup auth peers db
   peers_ = std::make_shared<NetworkPeers::ClientStore>();

   auto uiPubKey = Config::NetworkSettings::uiPublicKey();
   if (uiPubKey.getSize() != 33) {
      LOGERR << "Invalid UI pubkey!";
      LOGERR << "The UI pubkey must be 33 bytes long (66 hexits), " <<
         "passed through --uiPubKey";
      throw std::runtime_error("invalid UI pubkey");
   }

   //inject UI key (UI is the server, bridge connects to it)
   NetworkPeers::PeerKey uiKey{uiPubKey, NetworkPeers::PeerType::ServerTwoWay};
   peers_->addPeer(uiKey, {serverName_}, {});

   //write own public key to cookie file
   {
      const auto& ownKey = peers_->getOwnPublicKey();
      std::fstream file;

      //on windows, we need to explicitly open the cookie file in binary
      //for writing, or it will stop at the first null byte
      file.open(Config::getDataDir() / "client_cookie",
         std::ios::out | std::ios::binary);
      file.write((const char*)ownKey.getPtr(), ownKey.getSize());
   }

   //init bip15x channel
   bip151Connection_ = std::make_shared<BIP151Connection>(
      peers_->getView(NetworkPeers::PeerType::ServerTwoWay), false);
}

SocketType CppBridgeSocket::type() const
{
   return SocketType::CppBridge;
}

////////////////////////////////////////////////////////////////////////////////
void CppBridgeSocket::respond(std::vector<uint8_t>& inMsg)
{
   static BinaryData emptySizeCheck{};
   if (inMsg.empty()) {
      //shutdown condition
      shutdown();
      return;
   }

   //append data to leftovers from previous iteration if applicable
   BinaryData data;
   if (!leftOverData_.empty()) {
      leftOverData_.append(&inMsg[0], inMsg.size());
      data = std::move(leftOverData_);
   } else {
      data = std::move(inMsg);
   }

   while (!data.empty()) {
      //for data that isn't encrypted, assume the payload is
      //a single whole packet
      bool encr = false;
      BinaryDataRef dataRef = data.getRef();
      auto packetSize = dataRef.getSize();

      if (bip151Connection_->connectionComplete()) {
         //get decrypted length
         auto decrLen = bip151Connection_->decryptPacket(
            data.getSliceRef(0, POLY1305MACLEN + AUTHASSOCDATAFIELDLEN),
            emptySizeCheck);

         if (decrLen == -1 || decrLen > BRIDGE_SOCKET_MAXLEN) {
            //fatal error
            LOGERR << "packet exceeds BRIDGE_SOCKET_MAXLEN, aborting";
            shutdown();
            return;
         }

         if (decrLen > (ssize_t)data.getSize() + POLY1305MACLEN) {
            //not enough data to decrypt, save it and continue
            leftOverData_ = std::move(data);
            return;
         }

         //decrypt the data
         bip151Connection_->decryptPacket(data.getRef(), data);

         //point to the head of the decrypted cleartext
         dataRef.setRef(data.getPtr() + AUTHASSOCDATAFIELDLEN, decrLen);

         //keep track of this packet's size
         packetSize = decrLen + AUTHASSOCDATAFIELDLEN + POLY1305MACLEN;
         encr = true;
      }

      if (dataRef.empty()) {
         //handshake failure
         LOGERR << "invalid packet size, aborting";
         shutdown();
         return;
      }

      auto dataType = (ArmoryAEAD::BIP151_PayloadType)dataRef[0];
      if (encr && dataType < ArmoryAEAD::BIP151_PayloadType::Threshold_Begin) {
         //we can only process user messages after the AEAD channel is auth'ed
         //and the data is encrypted
         if (bip151Connection_->getBIP150State() != BIP150State::SUCCESS) {
            shutdown();
            return;
         }

         BinaryData requestBody;
         requestBody.resize(dataRef.getSize() - 1);
         memcpy(requestBody.getPtr(), dataRef.getPtr() + 1, requestBody.getSize());
         if (!ProtoCommandParser::processData(bridgePtr_, requestBody)) {
            shutdown();
            return;
         }
      } else {
         //we can only get here if the data is part of an ongoing AEAD
         //handhsake or an incoming channel rekey
         if (!processAEADHandshake(dataRef)) {
            //handshake failure
            LOGERR << "AEAD handshake failed, aborting";
            shutdown();
            return;
         }
      }

      if (data.getSize() == packetSize) {
         return;
      }

      //payload is bigger than the packet we just processed, remove leading
      //packet from data and iterate over what's left
      data = data.getSliceCopy(packetSize, data.getSize() - packetSize);
   }
}

////////////////////////////////////////////////////////////////////////////////
void CppBridgeSocket::pushPayload(
   std::unique_ptr<Network::Socket_WritePayload> write_payload,
   std::shared_ptr<Network::Socket_ReadPayload>)
{
   if (write_payload == nullptr) {
      return;
   }

   //lock write mutex
   std::unique_lock<std::mutex> lock(writeMutex_);

   //check for rekeys
   {
      bool needs_rekey = false;
      auto rightnow = std::chrono::system_clock::now();

      if (bip151Connection_->rekeyNeeded(write_payload->getSerializedSize())) {
         needs_rekey = true;
      } else {
         auto time_sec = std::chrono::duration_cast<std::chrono::seconds>(
            rightnow - outKeyTimePoint_);
         if (time_sec.count() >= AEAD_REKEY_INTVERVAL_SECONDS) {
            needs_rekey = true;
         }
      }

      if (needs_rekey) {
         std::vector<uint8_t> rekeyPacket(BIP151PUBKEYSIZE + 5 + POLY1305MACLEN);
         memset(&rekeyPacket[5], 0, BIP151PUBKEYSIZE);

         uint32_t rekeyPacketLen = BIP151PUBKEYSIZE + 1;
         memcpy(&rekeyPacket[0], &rekeyPacketLen, sizeof(uint32_t));
         memset(&rekeyPacket[4],
            (uint8_t)ArmoryAEAD::BIP151_PayloadType::Rekey, 1);

         bip151Connection_->assemblePacket(
            BinaryDataRef{&rekeyPacket[0], rekeyPacket.size() - POLY1305MACLEN},
            &rekeyPacket[0], rekeyPacket.size());

         queuePayloadForWrite(rekeyPacket);
         bip151Connection_->rekeyOuterSession();
         outKeyTimePoint_ = rightnow;
      }
   }

   //serialize payload
   std::vector<uint8_t> data;
   write_payload->serialize(data);

   //set data flag
   memset(&data[4], 0, 1);

   //encrypt
   bip151Connection_->assemblePacket(
      BinaryDataRef{&data[0], data.size() - POLY1305MACLEN},
      &data[0], data.size());
   queuePayloadForWrite(data);
}

////////////////////////////////////////////////////////////////////////////////
bool CppBridgeSocket::processAEADHandshake(BinaryDataRef data)
{
   //write lambda
   auto writeData = [this](
      const BinaryData& payload, ArmoryAEAD::BIP151_PayloadType msgType, bool encrypt)
   {
      //prepend message type to payload
      size_t packetSize = 5 + payload.getSize() + POLY1305MACLEN;
      std::vector<uint8_t> cipherText(packetSize);

      unsigned index = 0;
      if (encrypt) {
         uint32_t sizeHeader = payload.getSize() + 1;
         memcpy(&cipherText[0], &sizeHeader, sizeof(uint32_t));
         index = 4;
      }

      memset(&cipherText[index], (uint8_t)msgType, 1);
      memcpy(&cipherText[index + 1], payload.getPtr(), payload.getSize());

      //encrypt if necessary
      if (encrypt) {
         bip151Connection_->assemblePacket(
            BinaryDataRef{&cipherText[0], packetSize - POLY1305MACLEN},
            &cipherText[0], packetSize);
      } else {
         cipherText.resize(payload.getSize() + 1);
      }

      //push
      queuePayloadForWrite(cipherText);
   };

   //first byte is the AEAD sequence
   auto seqId = (ArmoryAEAD::BIP151_PayloadType)data[0];
   switch (seqId)
   {
      case ArmoryAEAD::BIP151_PayloadType::PresentPubKey:
      {
         LOGERR << "Server presented pubkey, bridge does not tolerate 1-way auth";
         return false;
      }

      default:
         break;
   }

   //common client side handshake
   BinaryDataRef msgbdr = data.getSliceRef(1, data.getSize() - 1);
   auto status = ArmoryAEAD::BIP15x_Handshake::clientSideHandshake(
      bip151Connection_.get(), serverName_,
      seqId, msgbdr,
      writeData);

   switch (status)
   {
      case ArmoryAEAD::HandshakeState::StepSuccessful:
      case ArmoryAEAD::HandshakeState::RekeySuccessful:
         return true;

      case ArmoryAEAD::HandshakeState::Completed:
      {
         outKeyTimePoint_ = std::chrono::system_clock::now();

         //flag connection as ready
         return true;
      }

      default:
         return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
////
////  WritePayload_Bridge
////
////////////////////////////////////////////////////////////////////////////////
void WritePayload_Bridge::serialize(std::vector<uint8_t>& payload)
{
   if (data.empty()) {
      return;
   }
   payload.resize(data.getSize() + 8 + POLY1305MACLEN);

   //set packet size
   uint32_t sizeVal = data.getSize() + 4;
   memcpy(&payload[0], &sizeVal, sizeof(uint32_t));

   //serialize protobuf message
   memcpy(&payload[8], data.getPtr(), data.getSize());
}

////////////////////////////////////////////////////////////////////////////////
size_t WritePayload_Bridge::getSerializedSize(void) const
{
   return data.getSize() + 8 + POLY1305MACLEN;
}
