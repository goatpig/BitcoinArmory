////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BDV_Notification.h"
#include <Utils/log.h>
#include <ZeroConf/Utils.h>
#include <ZeroConf/Notifications.h>
#include <Node/BitcoinP2P.h>
#include <Node/nodeRPC.h>

///////////////////////////////////////////////////////////////////////////////
// BDV_Notification
BDV_Notification::BDV_Notification(Armory::Types::BdvId id) :
   bdvID_(id)
{}

BDV_Notification::~BDV_Notification()
{}

////
Armory::Types::BdvId BDV_Notification::bdvID() const
{
   return bdvID_;
}

////
bool BDV_Notification::fatal() const
{
   return false;
}

bool BDV_Notification::broadcast() const
{
   return bdvID() == BDV_NOTIF_BROADCAST;
}

///////////////////////////////////////////////////////////////////////////////
// BDV_Notification_Init
BDV_Notification_Init::BDV_Notification_Init() :
   BDV_Notification(BDV_NOTIF_BROADCAST)
{}

BDV_Action BDV_Notification_Init::actionType() const
{
   return BDV_Init;
}

///////////////////////////////////////////////////////////////////////////////
// BDV_Notification_NewBlock
BDV_Notification_NewBlock::BDV_Notification_NewBlock(
   const Armory::ReorganizationState& ref,
   std::shared_ptr<Armory::ZeroConf::ZcPurgePacket> purgePacket) :
   BDV_Notification(BDV_NOTIF_BROADCAST),
   reorgState(ref), zcPurgePacket(purgePacket)
{}

BDV_Action BDV_Notification_NewBlock::actionType() const
{
   return BDV_NewBlock;
}

///////////////////////////////////////////////////////////////////////////////
// BDV_Notification_ZC
BDV_Notification_ZC::BDV_Notification_ZC(
   std::shared_ptr<Armory::ZeroConf::ZcNotificationPacket> zcPacketPtr,
   std::vector<std::shared_ptr<const TxIOPair>> txioV) :
   BDV_Notification(zcPacketPtr->bdvID),
   packet(zcPacketPtr), txios(std::move(txioV))
{}

BDV_Action BDV_Notification_ZC::actionType() const
{
   return BDV_ZC;
}

///////////////////////////////////////////////////////////////////////////////
// BDV_Notification_Refresh
BDV_Notification_Refresh::BDV_Notification_Refresh(Armory::Types::BdvId bdvID,
   BDV_refresh refresh, const std::string& refreshID) :
   BDV_Notification(bdvID), refresh(refresh), refreshID(refreshID)
{
   zcPacket = std::make_shared<Armory::ZeroConf::ZcNotificationPacket>(bdvID);
}

BDV_Action BDV_Notification_Refresh::actionType() const
{
   return BDV_Refresh;
}

///////////////////////////////////////////////////////////////////////////////
// BDV_Notification_Progress
BDV_Notification_Progress::BDV_Notification_Progress(
   BDMPhase phase, double prog, unsigned time, unsigned numProg,
   const std::vector<std::string>& walletIDs) :
   BDV_Notification(BDV_NOTIF_BROADCAST),
   phase(phase), progress(prog), time(time),
   numericProgress(numProg), walletIDs(walletIDs)
{}

BDV_Action BDV_Notification_Progress::actionType() const
{
   return BDV_Progress;
}

///////////////////////////////////////////////////////////////////////////////
// BDV_Notification_NodeStatus
BDV_Notification_NodeStatus::BDV_Notification_NodeStatus(
   std::shared_ptr<Node::Status> nss) :
   BDV_Notification(BDV_NOTIF_BROADCAST), status(nss)
{}

BDV_Action BDV_Notification_NodeStatus::actionType() const
{
   return BDV_NodeStatus;
}

///////////////////////////////////////////////////////////////////////////////
// BDV_Notification_Error
BDV_Notification_Error::BDV_Notification_Error(Armory::Types::BdvId bdvID,
   int errCode, const BinaryData& errData, const std::string& errStr) :
   BDV_Notification(bdvID)
{
   errStruct.errCode_ = errCode;
   errStruct.errData_ = errData;
   errStruct.errorStr_ = errStr;
}

BDV_Action BDV_Notification_Error::actionType() const
{
   return BDV_Action::BDV_Error;
}

bool BDV_Notification_Error::fatal() const
{
   return errStruct.errCode_ == BDM_FATAL_ERROR_CODE;
}
