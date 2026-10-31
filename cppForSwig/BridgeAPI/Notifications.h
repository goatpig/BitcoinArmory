////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <mutex>
#include <functional>
#include <map>
#include <set>
#include <string>

#include <DBClientClasses.h>

namespace Armory
{
   namespace Ledgers
   {
      class Entry;
   }

   namespace Bridge
   {
      namespace Notifications
      {
         namespace Push
         {
            enum class PayloadType : int
            {
               PASS_TO_CLIENT,
               NEWBLOCK,
               ZC,
               REFRESH,
               IS_REGISTERED,
               SETUP_DONE,
               CONNECT_TO_IP,
               CONNECT_TO_PEER,
               RUN_AUTOMATION_CONTEXT,
               DISCONNECTED
            };

            struct Payload
            {
               const PayloadType type;
               Payload(PayloadType);
               virtual ~Payload(void) = 0;
               virtual bool syncWalletState(void) const;
            };


            struct Notif_Registered : public Payload
            {
               Notif_Registered(void);
            };

            struct Notif_Disconnected : public Payload
            {
               Notif_Disconnected(void);
            };

            struct Notif_NewBlock : public Payload
            {
               const NewBlockNotif blockNotif;
               const bool isReadyNotif;

               Notif_NewBlock(
                  const NewBlockNotif&, bool);
               bool syncWalletState(void) const override;
            };

            struct Notif_ZC : public Payload
            {
               std::vector<TxIOPair> txios;
               std::set<BinaryData> invalidatedZCHashes;

               Notif_ZC(std::vector<TxIOPair>, std::set<BinaryData>);
            };

            ////////
            struct Command_PassAlong : public Payload
            {
               BinaryData packet;
               Command_PassAlong(BinaryData);
            };

            struct Command_Refresh : public Payload
            {
               const std::set<std::string> refreshIds;

               Command_Refresh(const std::set<std::string>&);
               Command_Refresh(const std::string&);
               bool syncWalletState(void) const override;
            };

            struct Command_ConnectToIp : public Payload
            {
               const std::string ip;
               const uint16_t port;
               const std::string callbackId;
               const uint64_t msgId;

               Command_ConnectToIp(const std::string&, uint16_t,
                  const std::string&, uint64_t);
            };

            struct Command_ConnectToPeer : public Payload
            {
               const std::string peerKey;
               const uint64_t msgId;

               Command_ConnectToPeer(const std::string&, uint64_t);
            };

            struct Command_RunAutomationContext : public Payload
            {
               const std::string callbackId;
               const uint64_t msgId;

               Command_RunAutomationContext(const std::string&, uint64_t);
            };

            ////////
            class NotifHandler : public RemoteCallback
            {
               using NotifQueue = Threading::BlockingQueue<std::unique_ptr<Payload>>;
            private:
               std::shared_ptr<NotifQueue> queue_;

            public:
               NotifHandler(std::shared_ptr<NotifQueue>);

               //virtuals
               void run(BdmNotification) override;
               void progress(
                  BDMPhase,
                  const std::vector<std::string>&,
                  float, unsigned, unsigned
               ) override;
               void disconnected(void) override;
            };
         }

         namespace Serialization
         {
            //bridge client notif serialization
            BinaryData getNewBlockPacket(std::shared_ptr<Push::Notif_NewBlock>);
            BinaryData getRefreshPacket(std::shared_ptr<Push::Command_Refresh>);
            std::pair<BinaryData, BinaryData> getZcPacket(
               const std::vector<Ledgers::Entry>&,
               const std::set<BinaryData>&);
            BinaryData getSetupDonePacket(void);
            BinaryData getWalletsRegisteredPacket(void);
            BinaryData getServerDisconnectedPacket(void);
         }
      } // namespace Notifications
   } // namespace Bridge
} // namespace Armory
