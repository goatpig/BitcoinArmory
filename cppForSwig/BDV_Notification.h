////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _BDV_NOTIFICATION_H_
#define _BDV_NOTIFICATION_H_

#include <memory>

#include "log.h"
#include "bdmenums.h"
#include "BlockchainDatabase/Blockchain.h"
#include "LedgerEntry.h"
#include "ZeroConf.h"
#include "nodeRPC.h"
#include "ZeroConfNotifications.h"

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification
{
private:
   //notificaiton with empty ID means broadcast to all bdv
   const std::string bdvID_;

public:
   BDV_Notification(const std::string& id) :
      bdvID_(id)
   {}

   virtual ~BDV_Notification(void) {}
   virtual BDV_Action action_type(void) = 0;

   const std::string& bdvID(void) const { return bdvID_; }
   virtual bool fatal(void) const { return false; }
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Init : public BDV_Notification
{
   BDV_Notification_Init(void) :
      BDV_Notification("")
   {}

   BDV_Action action_type(void)
   {
      return BDV_Init;
   }
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_NewBlock : public BDV_Notification
{
   Blockchain::ReorganizationState reorgState_;
   std::shared_ptr<ZcPurgePacket> zcPurgePacket_;

   BDV_Notification_NewBlock(
      const Blockchain::ReorganizationState& ref, 
      std::shared_ptr<ZcPurgePacket> purgePacket) :
      BDV_Notification(""),
      reorgState_(ref), zcPurgePacket_(purgePacket)
   {}

   BDV_Action action_type(void)
   {
      return BDV_NewBlock;
   }
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_ZC : public BDV_Notification
{
   const ZcNotificationPacket packet_;
   std::vector<LedgerEntry> leVec_;

   BDV_Notification_ZC(ZcNotificationPacket& packet) :
      BDV_Notification(packet.bdvID_), packet_(std::move(packet))
   {}

   BDV_Action action_type(void)
   {
      return BDV_ZC;
   }
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Refresh : public BDV_Notification
{
   const BDV_refresh refresh_;
   const std::string refreshID_;
   ZcNotificationPacket zcPacket_;

   BDV_Notification_Refresh(const std::string& bdvID,
      BDV_refresh refresh, const std::string& refreshID) :
      BDV_Notification(bdvID),
      refresh_(refresh), refreshID_(refreshID), zcPacket_(bdvID)
   {}

   BDV_Action action_type(void)
   {
      return BDV_Refresh;
   }
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Progress : public BDV_Notification
{
   BDMPhase phase_;
   double progress_;
   unsigned time_;
   unsigned numericProgress_;
   const std::vector<std::string> walletIDs_;

   BDV_Notification_Progress(BDMPhase phase, double prog,
      unsigned time, unsigned numProg, const std::vector<std::string>& walletIDs) :
      BDV_Notification(""),
      phase_(phase), progress_(prog), time_(time),
      numericProgress_(numProg), walletIDs_(walletIDs)
   {}

   BDV_Action action_type(void)
   {
      return BDV_Progress;
   }
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_NodeStatus : public BDV_Notification
{
   const CoreRPC::NodeStatus status_;

   BDV_Notification_NodeStatus(CoreRPC::NodeStatus nss) :
      BDV_Notification(""), status_(nss)
   {}

   BDV_Action action_type(void)
   {
      return BDV_NodeStatus;
   }
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Error : public BDV_Notification
{
   BDV_Error_Struct errStruct;

   BDV_Notification_Error(const std::string& bdvID,
      int errCode, const BinaryData& errData, const std::string& errStr) :
      BDV_Notification(bdvID)
   {
      errStruct.errCode_ = errCode;
      errStruct.errData_ = errData;
      errStruct.errorStr_ = errStr;
   }

   BDV_Action action_type(void)
   {
      return BDV_Action::BDV_Error;
   }

   bool fatal(void) const override
   {
      return errStruct.errCode_ == BDM_FATAL_ERROR_CODE;
   }
};

class BDV_Server_Object;

///////////////////////////////////////////////////////////////////////////////
struct BDV_Notification_Packet
{
   std::shared_ptr<BDV_Server_Object> bdvPtr_;
   std::shared_ptr<BDV_Notification> notifPtr_;
};

///////////////////////////////////////////////////////////////////////////////
struct BDVNotificationHook
{
   std::function<void(BDV_Notification*)> lambda_;
};

#endif