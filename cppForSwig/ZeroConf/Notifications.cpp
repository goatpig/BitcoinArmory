////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <list>

#include "Notifications.h"
#include <BlockchainDatabase/txio.h>
#include <Network/WebSocketMessage.h>
#include <BDM_Server.h>

#include "Utils.h"

using namespace Armory;
using namespace Armory::ZeroConf;

////////////////////////////////////////////////////////////////////////////////
// WatcherTxBody
WatcherTxBody::WatcherTxBody(std::shared_ptr<BinaryData> rawTx) :
   rawTxPtr(rawTx)
{}

////////////////////////////////////////////////////////////////////////////////
// ZcNotificationPacket
ZcNotificationPacket::ZcNotificationPacket(Types::BdvId id) :
   bdvID(id)
{}

////////////////////////////////////////////////////////////////////////////////
// ZeroConfCallbacks
ZeroConfCallbacks::~ZeroConfCallbacks()
{}

////////
ZeroConfCallbacks_BDV::ZeroConfCallbacks_BDV(Clients* clientsPtr) :
   clientsPtr_(clientsPtr)
{
   auto requestLambda = [this](void)->void
   {
      processNotifRequests();
   };
   requestThread_ = std::thread(requestLambda);
}

ZeroConfCallbacks_BDV::~ZeroConfCallbacks_BDV()
{
   requestQueue_.terminate();
   if (requestThread_.joinable()) {
      requestThread_.join();
   }
}

////////
std::set<Types::BdvId> ZeroConfCallbacks_BDV::hasScrAddr(
   const Types::ScrAddr& addr) const
{
   //this is slow, needs improved
   std::set<Types::BdvId> result;
   auto bdvMap = clientsPtr_->BDVs_.get();
   for (const auto& bdv_pair : bdvMap) {
      if (bdv_pair.second->hasScrAddress(addr)) {
         result.emplace(bdv_pair.first);
      }
   }
   return result;
}

////////
void ZeroConfCallbacks_BDV::pushZcNotification(
   std::shared_ptr<MempoolSnapshot> ss,
   std::shared_ptr<KeyAddrMap> newZcKeys,
   std::map<Types::BdvId, ParsedZCData> flaggedBDVs,
   Types::BdvId bdvId,
   std::map<Types::TxHash, std::shared_ptr<WatcherTxBody>>& watcherMap)
{
   auto requestPtr = std::make_shared<
      ZeroConfCallbacks_BDV::ZcNotifRequest_Success>(
      bdvId,
      ss, newZcKeys, flaggedBDVs,
      watcherMap);
   requestQueue_.push_back(std::move(requestPtr));
}

void ZeroConfCallbacks_BDV::pushZcError(
   Types::BdvId bdvID, const Types::TxHash& hash,
   ArmoryErrorCodes errCode, const std::string& verbose)
{
   auto requestPtr = std::make_shared<ZcNotifRequest_Error>(
      bdvID, hash, errCode, verbose);
   requestQueue_.push_back(move(requestPtr));
}

////////
void ZeroConfCallbacks_BDV::processNotifRequests()
{
   while (true) {
      std::shared_ptr<ZeroConfCallbacks_BDV::ZcNotifRequest> notifReqPtr;
      try {
         notifReqPtr = requestQueue_.pop_front();
      } catch (const Threading::StopBlockingLoop&) {
         break;
      }

      switch (notifReqPtr->type)
      {
         case ZcNotifRequestType::Success:
         {
            auto reqPtr = std::dynamic_pointer_cast<
               ZeroConfCallbacks_BDV::ZcNotifRequest_Success>(notifReqPtr);
            if (reqPtr == nullptr) {
               LOGWARN << "zc notification request type mismatch";
               break;
            }

            //build notifications for each BDV
            for (auto& bdvObj : reqPtr->flaggedBDVs) {
               //get bdv object
               auto bdvPtr = clientsPtr_->BDVs_.get(bdvObj.first);
               if (bdvPtr == nullptr) {
                  LOGWARN << "pushing zc notification with invalid bdvid";
                  continue;
               }

               //create notif packet
               auto notifPacket = std::make_shared<ZcNotificationPacket>(
                  bdvObj.first);
               notifPacket->ssPtr = reqPtr->ssPtr;

               //set txio map
               std::vector<std::shared_ptr<const TxIOPair>> txioVec;
               txioVec.reserve(5);
               for (const auto& sa : bdvObj.second.scrAddrs) {
                  auto txioMap = reqPtr->ssPtr->getTxioMapForScrAddr(sa);
                  if (txioMap.empty()) {
                     continue;
                  }

                  //copy the txiomap for this scrAddr over to the notification object
                  TxIOKeys keySet;
                  for (const auto& txioPair : txioMap) {
                     keySet.emplace(txioPair.first);
                     txioVec.emplace_back(txioPair.second);
                  }
                  notifPacket->scrAddrToTxioKeys.emplace(sa, std::move(keySet));
               }

               //set invalidated keys
               if (!bdvObj.second.invalidatedKeys.empty()) {
                  notifPacket->purgePacket = std::make_shared<ZcPurgePacket>();
                  notifPacket->purgePacket->invalidatedZcKeys = std::move(
                     bdvObj.second.invalidatedKeys);
               }

               //set the primary requestor if this is the caller bdv
               if (bdvObj.first == reqPtr->bdvId) {
                  notifPacket->primaryRequestor = reqPtr->bdvId;
               }

               //set new zc keys
               notifPacket->newKeysAndScrAddr = reqPtr->newZcKeys;

               //create notif and push to bdv
               auto bdvPacket = std::make_shared<BDV_Notification_Packet>();
               bdvPacket->bdvPtr = bdvPtr;
               bdvPacket->notifPtr = std::make_shared<BDV_Notification_ZC>(
                  notifPacket, std::move(txioVec));
               clientsPtr_->innerBDVNotifStack_.push_back(std::move(bdvPacket));
            }

            //process duplicate broadcast requests
            for (auto& watcherObj : reqPtr->watcherMap) {
               if (watcherObj.second->extraRequestors.empty()) {
                  continue;
               }

               if (!reqPtr->ssPtr->hasHash(watcherObj.first)) {
                  //tx was not added to mempool, skip
                  continue;
               }

               //tx was added to mempool, report already-in-mempool error to
               //duplicate requestors
               for (auto& extra : watcherObj.second->extraRequestors) {
                  pushZcError(extra, watcherObj.first,
                     ArmoryErrorCodes::ZcBroadcast_AlreadyInMempool,
                     "Extra requestor broadcast error: Already in mempool");
               }
            }
            break;
         }

         case ZcNotifRequestType::Error:
         {
            auto reqPtr = std::dynamic_pointer_cast<
               ZeroConfCallbacks_BDV::ZcNotifRequest_Error>(notifReqPtr);
            if (reqPtr == nullptr) {
               LOGWARN << "zc notification request type mismatch";
               break;
            }

            auto bdvPtr = clientsPtr_->BDVs_.get(reqPtr->bdvId);
            if (bdvPtr == nullptr) {
               LOGWARN << "pushed zc error with invalid bdvid";
               return;
            }

            auto notifPacket = std::make_shared<BDV_Notification_Packet>();
            notifPacket->bdvPtr = bdvPtr;
            notifPacket->notifPtr = std::make_shared<BDV_Notification_Error>(
               reqPtr->bdvId, (int)reqPtr->errCode,
               reqPtr->hash, reqPtr->verbose);
            clientsPtr_->innerBDVNotifStack_.push_back(std::move(notifPacket));

            break;
         }

         default:
            throw std::runtime_error("unexpected zc notification request type");
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
// ZcNotifRequest
ZeroConfCallbacks_BDV::ZcNotifRequest::ZcNotifRequest(
   ZcNotifRequestType zcType, Types::BdvId id) :
   type(zcType), bdvId(id)
{}

ZeroConfCallbacks_BDV::ZcNotifRequest::~ZcNotifRequest()
{}

////////
ZeroConfCallbacks_BDV::ZcNotifRequest_Success::ZcNotifRequest_Success(
   Types::BdvId bdvId,
   std::shared_ptr<MempoolSnapshot> mempoolSs,
   std::shared_ptr<KeyAddrMap> newKeys,
   std::map<Types::BdvId, ParsedZCData> flagged,
   std::map<Types::TxHash, std::shared_ptr<WatcherTxBody>>& watchers) :
   ZcNotifRequest(ZcNotifRequestType::Success, bdvId),
   ssPtr(mempoolSs), newZcKeys(newKeys),
   flaggedBDVs(std::move(flagged)),
   watcherMap(std::move(watchers))
{}

////////
ZeroConfCallbacks_BDV::ZcNotifRequest_Error::ZcNotifRequest_Error(
   Types::BdvId bdvId,
   const Types::TxHash& h, ArmoryErrorCodes err,
   const std::string& v) :
   ZcNotifRequest(ZcNotifRequestType::Error, bdvId),
   hash(h), errCode(err), verbose(v)
{}
