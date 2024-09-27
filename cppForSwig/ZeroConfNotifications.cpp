////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <list>

#include "ZeroConfNotifications.h"
#include "ZeroConfUtils.h"
#include "ZeroConf.h"
#include "BDM_Server.h"
#include "LedgerEntry.h"
#include "BlockchainDatabase/txio.h"

using namespace std;

///////////////////////////////////////////////////////////////////////////////
//
// ZeroConfCallbacks
//
///////////////////////////////////////////////////////////////////////////////
ZeroConfCallbacks_BDV::ZeroConfCallbacks_BDV(Clients* clientsPtr) :
   clientsPtr_(clientsPtr)
{
   auto requestLambda = [this](void)->void
   {
      processNotifRequests();
   };

   requestThread_ = thread(requestLambda);
}

///////////////////////////////////////////////////////////////////////////////
ZeroConfCallbacks_BDV::~ZeroConfCallbacks_BDV()
{
   requestQueue_.terminate();

   if (requestThread_.joinable())
      requestThread_.join();
}

///////////////////////////////////////////////////////////////////////////////
set<string> ZeroConfCallbacks_BDV::hasScrAddr(const BinaryDataRef& addr) const
{
   //this is slow, needs improved
   set<string> result;

   auto bdvMap = clientsPtr_->BDVs_.get();
   for (const auto& bdv_pair : bdvMap) {
      if (bdv_pair.second->hasScrAddress(addr)) {
         result.emplace(bdv_pair.first);
      }
   }
   return result;
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfCallbacks_BDV::pushZcNotification(
   shared_ptr<MempoolSnapshot> ss,
   shared_ptr<KeyAddrMap> newZcKeys,
   map<string, ParsedZCData> flaggedBDVs,
   const string& bdvId,
   map<BinaryData, shared_ptr<WatcherTxBody>>& watcherMap)
{
   auto requestPtr = make_shared<
      ZeroConfCallbacks_BDV::ZcNotifRequest_Success>(
      bdvId,
      ss, newZcKeys, flaggedBDVs,
      watcherMap);

   requestQueue_.push_back(move(requestPtr));
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfCallbacks_BDV::pushZcError(
   const string& bdvID, const BinaryData& hash,
   ArmoryErrorCodes errCode, const string& verbose)
{
   auto requestPtr = make_shared<ZeroConfCallbacks_BDV::ZcNotifRequest_Error>(
      bdvID, hash, errCode, verbose);
   requestQueue_.push_back(move(requestPtr));
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfCallbacks_BDV::processNotifRequests()
{
   while (true) {
      std::shared_ptr<ZeroConfCallbacks_BDV::ZcNotifRequest> notifReqPtr;
      try {
         notifReqPtr = requestQueue_.pop_front();
      } catch (const Armory::Threading::StopBlockingLoop&) {
         break;
      }

      switch (notifReqPtr->type_)
      {
         case ZcNotifRequestType::Success:
         {
            auto reqPtr = dynamic_pointer_cast<
               ZeroConfCallbacks_BDV::ZcNotifRequest_Success>(notifReqPtr);
            if (reqPtr == nullptr) {
               LOGWARN << "zc notification request type mismatch";
               break;
            }

            //build notifications for each BDV
            for (auto& bdvObj : reqPtr->flaggedBDVs_) {
               //get bdv object
               auto bdvPtr = clientsPtr_->BDVs_.get(bdvObj.first);
               if (bdvPtr == nullptr) {
                  LOGWARN << "pushing zc notification with invalid bdvid";
                  continue;
               }

               //create notif packet
               ZcNotificationPacket notificationPacket(bdvObj.first);
               notificationPacket.ssPtr_ = reqPtr->ssPtr_;

               //set txio map
               for (auto& sa : bdvObj.second.scrAddrs_) {
                  auto txioKeys = reqPtr->ssPtr_->getTxioKeysForScrAddr(sa);
                  if (txioKeys.empty()) {
                     continue;
                  }

                  //copy the txiomap for this scrAddr over to the notification object
                  notificationPacket.scrAddrToTxioKeys_.emplace(sa, txioKeys);
               }

               //set invalidated keys
               if (!bdvObj.second.invalidatedKeys_.empty()) {
                  notificationPacket.purgePacket_ = std::make_shared<ZcPurgePacket>();
                  notificationPacket.purgePacket_->invalidatedZcKeys_ =
                     std::move(bdvObj.second.invalidatedKeys_);
               }

               //set the primary requestor if this is the caller bdv
               if (bdvObj.first == reqPtr->bdvId_) {
                  notificationPacket.primaryRequestor_ = reqPtr->bdvId_;
               }

               //set new zc keys
               notificationPacket.newKeysAndScrAddr_ = reqPtr->newZcKeys_;

               //create notif and push to bdv
               auto notifPacket = make_shared<BDV_Notification_Packet>();
               notifPacket->bdvPtr_ = bdvPtr;
               notifPacket->notifPtr_ =
                  std::make_shared<BDV_Notification_ZC>(notificationPacket);
               clientsPtr_->innerBDVNotifStack_.push_back(std::move(notifPacket));
            }

            //process duplicate broadcast requests
            for (auto& watcherObj : reqPtr->watcherMap_) {
               if (watcherObj.second->extraRequestors_.empty()) {
                  continue;
               }

               if (!reqPtr->ssPtr_->hasHash(watcherObj.first)) {
                  //tx was not added to mempool, skip
                  continue;
               }

               //tx was added to mempool, report already-in-mempool error to
               //duplicate requestors
               for (auto& extra : watcherObj.second->extraRequestors_) {
                  pushZcError(extra, watcherObj.first,
                     ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool,
                     "Extra requestor broadcast error: Already in mempool");
               }
            }

            break;
         }

         case ZcNotifRequestType::Error:
         {
            auto reqPtr = dynamic_pointer_cast<
               ZeroConfCallbacks_BDV::ZcNotifRequest_Error>(notifReqPtr);
            if (reqPtr == nullptr) {
               LOGWARN << "zc notification request type mismatch";
               break;
            }

            auto bdvPtr = clientsPtr_->BDVs_.get(reqPtr->bdvId_);
            if (bdvPtr == nullptr) {
               LOGWARN << "pushed zc error with invalid bdvid";
               return;
            }

            auto notifPacket = make_shared<BDV_Notification_Packet>();
            notifPacket->bdvPtr_ = bdvPtr;
            notifPacket->notifPtr_ = make_shared<BDV_Notification_Error>(
               reqPtr->bdvId_,
               (int)reqPtr->errCode_, reqPtr->hash_, reqPtr->verbose_);
            clientsPtr_->innerBDVNotifStack_.push_back(move(notifPacket));

            break;
         }

         default:
            throw std::runtime_error("unexpected zc notification request type");
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
ZeroConfCallbacks_BDV::ZcNotifRequest::~ZcNotifRequest()
{}

///////////////////////////////////////////////////////////////////////////////
//
// ZcNotificationPacket
//
///////////////////////////////////////////////////////////////////////////////
/*void ZcNotificationPacket::toProtobufNotification(
   std::shared_ptr<::Codec_BDVCommand::BDVCallback> protoPtr, 
   const std::vector<LedgerEntry>& leVec) const
{
   //order ledger entries per request id
   map<string, list<const ::LedgerEntry*>> requestToLedgers;

   for (auto& le : leVec)
   {
      const auto& hash = le.getTxHash();
      
      auto iter = requestorMap_.find(hash);
      if (iter == requestorMap_.end())
      {
         //if this hash has no request id attached, pass it without one
         requestToLedgers[primaryRequestor_].push_back(&le);
      }
      else
      {
         requestToLedgers[iter->second].push_back(&le);
      }      
   }

   //create a notif per request
   for (auto& reqPair : requestToLedgers)
   {
      auto& leList = reqPair.second;

      if (!leList.empty())
      {
         auto notif = protoPtr->add_notification();
         notif->set_type(NotificationType::zc);
         auto ledgers = notif->mutable_ledgers();

         for (auto le : leList)
         {
            auto ledger_entry = ledgers->add_values();
            le->fillMessage(ledger_entry);
         }

         if (!reqPair.first.empty())
            notif->set_requestid(reqPair.first);
      }

   }

   if (purgePacket_ != nullptr &&
      !purgePacket_->invalidatedZcKeys_.empty())
   {
      auto notif = protoPtr->add_notification();
      notif->set_type(NotificationType::invalidated_zc);

      auto ids = notif->mutable_ids();
      for (auto& id : purgePacket_->invalidatedZcKeys_)
      {
         auto idPtr = ids->add_value();
         idPtr->set_data(id.second.getPtr(), id.second.getSize());
      }
   }
}*/