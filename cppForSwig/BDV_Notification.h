////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>

#include <Utils/Types.h>
#include <BlockchainDatabase/Blockchain.h>
#include "bdmenums.h"
#include "DBClientClasses.h"

#define BDV_NOTIF_BROADCAST UINT64_MAX

namespace Node
{
   struct Status;
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

   struct ReorganizationState;
}

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification
{
private:
   //notificaton id set to BDV_NOTIF_BROADCAST means broadcast to all bdv
   const Armory::Types::BdvId bdvID_;

public:
   BDV_Notification(Armory::Types::BdvId);
   virtual ~BDV_Notification(void);

   virtual BDV_Action actionType(void) const = 0;
   Armory::Types::BdvId bdvID(void) const;
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
   Armory::ReorganizationState reorgState;
   std::shared_ptr<Armory::ZeroConf::ZcPurgePacket> zcPurgePacket;

   BDV_Notification_NewBlock(
      const Armory::ReorganizationState&,
      std::shared_ptr<Armory::ZeroConf::ZcPurgePacket>);
   BDV_Action actionType(void) const override;
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_ZC : public BDV_Notification
{
   const std::shared_ptr<Armory::ZeroConf::ZcNotificationPacket> packet;
   const std::vector<std::shared_ptr<const TxIOPair>> txios;

   BDV_Notification_ZC(
      std::shared_ptr<Armory::ZeroConf::ZcNotificationPacket>,
      std::vector<std::shared_ptr<const TxIOPair>>);
   BDV_Action actionType(void) const override;
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Refresh : public BDV_Notification
{
   const BDV_refresh refresh;
   const std::string refreshID;
   std::shared_ptr<Armory::ZeroConf::ZcNotificationPacket> zcPacket;

   BDV_Notification_Refresh(Armory::Types::BdvId, BDV_refresh, const std::string&);
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
   const std::shared_ptr<Node::Status> status;

   BDV_Notification_NodeStatus(std::shared_ptr<Node::Status>);
   BDV_Action actionType(void) const override;
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Error : public BDV_Notification
{
   BDV_Error_Struct errStruct;

   BDV_Notification_Error(Armory::Types::BdvId,
      int, const Armory::Types::TxHash&, const std::string&);
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
