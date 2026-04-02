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
      enum class NotifType : int
      {
         PUSH,
         NEWBLOCK,
         ZC,
         REFRESH
      };

      struct NotifStruct
      {
         const NotifType type;
         NotifStruct(NotifType);
         virtual ~NotifStruct(void) = 0;
         virtual bool syncWalletState(void) const;
      };
      typedef std::function<void(std::shared_ptr<NotifStruct>)> NotifFunc;

      struct NotifStruct_Push : public NotifStruct
      {
         BinaryData packet;
         NotifStruct_Push(BinaryData);
      };

      struct NotifStruct_NewBlock : public NotifStruct
      {
         const NewBlockNotif blockNotif;
         const bool isReadyNotif;
         const std::function<void(void)> callback;

         NotifStruct_NewBlock(
            const NewBlockNotif&, const std::function<void(void)>&, bool);
         bool syncWalletState(void) const override;
      };

      struct NotifStruct_ZC : public NotifStruct
      {
         std::vector<TxIOPair> txios;
         std::set<BinaryData> invalidatedZCs;
         const std::function<void(
            const std::vector<Ledgers::Entry>&,
            const std::set<BinaryData>&)> callback;
         NotifStruct_ZC(std::vector<TxIOPair>, std::set<BinaryData>,
            const std::function<void(
               const std::vector<Ledgers::Entry>&,
               const std::set<BinaryData>&)>&
         );
      };

      struct NotifStruct_Refresh : public NotifStruct
      {
         const std::function<void(void)> callback;
         NotifStruct_Refresh(const std::function<void(void)>&);
         bool syncWalletState(void) const override;
      };

      ////////
      class Callback : public RemoteCallback
      {
      private:
         //to push packets to the gui
         NotifFunc notifFunc_;

         //id members
         std::mutex idMutex_;
         std::unordered_map<std::string, std::function<void(void)>> idCallbacks_;

      private:
         void processRefreshCallbacks(std::set<std::string>&);

      public:
         Callback(const NotifFunc&);

         //virtuals
         void run(BdmNotification) override;
         void progress(
            BDMPhase,
            const std::vector<std::string>&,
            float, unsigned, unsigned
         ) override;
         void disconnected(void) override;

         void notifySetupDone(void);
         void notifySetupRegistrationDone(void);
         void notifyRefresh(const std::set<std::string>&);
         void registerRefreshCallback(const std::string&,
            const std::function<void(void)>&);
         void unregisterCallback(const std::string&);
      };
   }
}
