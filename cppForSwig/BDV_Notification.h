////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <BlockchainDatabase/Blockchain.h>
#include "bdmenums.h"
#include "DBClientClasses.h"

#define BDV_NOTIF_BROADCAST UINT64_MAX

namespace CoreRPC
{
   struct NodeStatus;
}

namespace Armory
{
   namespace ZeroConf
   {
      struct ZcPurgePacket;
      struct ZcNotificationPacket;
   }

   namespace Ledgers
   {
      class Entry;
   }
}

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification
{
private:
   //notificaton id set to BDV_NOTIF_BROADCAST means broadcast to all bdv
   const BdvIdKey bdvID_;

public:
   BDV_Notification(BdvIdKey);
   virtual ~BDV_Notification(void);

   virtual BDV_Action actionType(void) const = 0;
   BdvIdKey bdvID(void) const;
   virtual bool fatal(void) const;
   bool broadcast(void) const;
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Init : public BDV_Notification
{
   BDV_Notification_Init(void);
   BDV_Action actionType(void) const override;
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_NewBlock : public BDV_Notification
{
   ReorganizationState reorgState;
   std::shared_ptr<Armory::ZeroConf::ZcPurgePacket> zcPurgePacket;

   BDV_Notification_NewBlock(
      const ReorganizationState&,
      std::shared_ptr<Armory::ZeroConf::ZcPurgePacket>);
   BDV_Action actionType(void) const override;
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_ZC : public BDV_Notification
{
   const std::shared_ptr<Armory::ZeroConf::ZcNotificationPacket> packet;
   std::vector<TxIOPair> txios;

   BDV_Notification_ZC(std::shared_ptr<Armory::ZeroConf::ZcNotificationPacket>);
   BDV_Action actionType(void) const override;
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Refresh : public BDV_Notification
{
   const BDV_refresh refresh;
   const std::string refreshID;
   std::shared_ptr<Armory::ZeroConf::ZcNotificationPacket> zcPacket;

   BDV_Notification_Refresh(BdvIdKey, BDV_refresh, const std::string&);
   BDV_Action actionType(void) const override;
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Progress : public BDV_Notification
{
   BDMPhase phase;
   double progress;
   unsigned time;
   unsigned numericProgress;
   const std::vector<std::string> walletIDs;

   BDV_Notification_Progress(BDMPhase, double,
      unsigned, unsigned, const std::vector<std::string>&);
   BDV_Action actionType(void) const override;
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_NodeStatus : public BDV_Notification
{
   const std::shared_ptr<CoreRPC::NodeStatus> status;

   BDV_Notification_NodeStatus(std::shared_ptr<CoreRPC::NodeStatus>);
   BDV_Action actionType(void) const override;
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Error : public BDV_Notification
{
   BDV_Error_Struct errStruct;

   BDV_Notification_Error(BdvIdKey, int, const BinaryData&, const std::string&);
   BDV_Action actionType(void) const override;
   bool fatal(void) const override;
};

class BDV_Server_Object;

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Packet
{
   std::shared_ptr<BDV_Server_Object> bdvPtr;
   std::shared_ptr<BDV_Notification> notifPtr;
};

///////////////////////////////////////////////////////////////////////////////
struct BDVNotificationHook
{
   std::function<void(BDV_Notification*)> func;
};
