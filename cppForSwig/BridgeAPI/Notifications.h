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
         namespace Server
         {
            enum class NotifType : int
            {
               PUSH,
               NEWBLOCK,
               ZC,
               REFRESH,
               IS_REGISTERED,
               SETUP_DONE,
               DISCONNECTED
            };

            struct Notif
            {
               const NotifType type;
               Notif(NotifType);
               virtual ~Notif(void) = 0;
               virtual bool syncWalletState(void) const;
            };

            struct Notif_Push : public Notif
            {
               BinaryData packet;
               Notif_Push(BinaryData);
            };

            struct Notif_Registered : public Notif
            {
               Notif_Registered(void);
            };

            struct Notif_Disconnected : public Notif
            {
               Notif_Disconnected(void);
            };

            struct Notif_NewBlock : public Notif
            {
               const NewBlockNotif blockNotif;
               const bool isReadyNotif;

               Notif_NewBlock(
                  const NewBlockNotif&, bool);
               bool syncWalletState(void) const override;
            };

            struct Notif_ZC : public Notif
            {
               std::vector<TxIOPair> txios;
               std::set<BinaryData> invalidatedZCHashes;

               Notif_ZC(std::vector<TxIOPair>, std::set<BinaryData>);
            };

            struct Notif_Refresh : public Notif
            {
               const std::set<std::string> notifIds;

               Notif_Refresh(const std::set<std::string>&);
               Notif_Refresh(const std::string&);
               bool syncWalletState(void) const override;
            };

            ////////
            class NotifHandler : public RemoteCallback
            {
               using NotifQueue = Threading::BlockingQueue<std::unique_ptr<Notif>>;
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

         namespace Client
         {
            //bridge client notif serialization
            BinaryData getSerializedNewBlockNotif(std::shared_ptr<Server::Notif_NewBlock>);
            BinaryData getSerializedRefreshNotif(std::shared_ptr<Server::Notif_Refresh>);
            std::pair<BinaryData, BinaryData> getSerializedZcNotif(
               const std::vector<Ledgers::Entry>&,
               const std::set<BinaryData>&);
            BinaryData getSerializedSetupDoneNotif(void);
            BinaryData getSerializedWalletsRegisteredNotif(void);
            BinaryData getSerializedServerDisconnectedNotif(void);
         }
      } // namespace Notifications
   } // namespace Bridge
} // namespace Armory
