////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/***
   - New ZC events are handled as batches by the ZC parser. Each new ZC event 
     is assigned a batch that is then pushed to the ZC parser queue for 
     processing.

   - New ZC has to be processed in order of appearance because of ZC parenthood
     (a ZC can spend from a ZC, therefor the parent has to be processed before
     the child, to be able to resolve the child's outpoint). Therefor, the zc
     parser is single threaded, and entries in the parser's queue are consumed
     in a FIFO ordering.

   - There are only 2 new ZC events: notification from the P2P node (which
     has no ID) and user broadcasts (which have a broadcast ID).

   - Broadcasting a transaction will result in a notification for all BDVs that 
     have registered addresses affected by that transaction.

   - Broadcasting a transaction that is already in the mempool will result in
     no ZC notification. Instead an error notification will be pushed to the 
     client, with an AlreadyInMempool error code.

   - Broadcast requests (as opposed to zc from the P2P node) are tracked in 
     ZC parser's watcherMap for the duration of the underyling batch.

   #####

   - A requestor is the <request ID, bdv ID> pair for the client pushing 
     transactions to the mempool (requesting a broadcast). This is the primary 
     requestor.
   
   - An extra requestor is the <request ID, bdv ID> pair for a client that 
     requests a broadcast for transactions already in an outstanding broadcast 
     batch. This is a secondary requestor.
   
   - There can be any amount of extra requestors. Therefor the extra 
     requestors are attached to the outstanding batch if any. This prevents
     flooding the zc parser queue with the same ZC broadcast.

   #####

   - Extra requestor examples: 
      Client_1 and client_2 both watch a subset of the same addresses. 
       zc1 affects this subset. 
       zc2 affects this subset. 
       zc3 only affects addresses for client_1. 
       zc4 only affects addresses for client_2.

     case 1 (simple):
     . client_1 pushes zc1
     . zc1 is processed, client_1 receives a notification with its
       request ID attached
     . client_2 receives a notification with no request ID attached
     . client_2 pushes zc1
     . zc1 fails to process, client_2 receives an error with its request ID
       attached and the AlreadyInMempool error code

     case 2 (intermediate):
     . client_1 pushes zc1
     . client_2 pushes zc1
     . zc1 is still being processed as part of the batch from
       client_1's broadcast request (batch #b1)
     . The watcher map is checked and the request & bdv ID for client_2 are
       added to #b1's extraRequestor map
     . No batch is created for client_2's request, as all the requested 
       broadcasts are already covered in other outstanding batches

     . #b1 parses, the following notifications are pushed:
       * client_1 gets a new zc notification with its requestID attached
       * client_2 gets a new zc notification with its requestID attached
       * client_2 gets a error notification with its requestID attached
         and a AlreadyInMempool error code
      
       This is consistent with the notification behavior of case 1 (a new zc
       notification and an error)

     case 3 (convoluted):
     . client_1 pushes zc1, zc2 and zc3
     . client_2 pushes zc1 and zc4
     . zc1 is still being processed as part of the batch from
       client_1's broadcast request (batch #b1)
     . The watcher map is checked and the request & bdv ID for client_2 are
       added to #b1's extraRequestor map
     . batch #b2 is created carrying client_2's request & bdvID with zc4
       (zc1 and zc2 are processed as part of #b1, won't be carried by #b2)

     . #b1 parses, the following notifications are pushed:
       * client_1 gets a new zc notification (for zc1, zc2 and zc3) with its 
         requestID attached
       * client_2 gets a new zc notification (for zc1) with its requestID 
         attached
       * client_2 gets a new zc notification (for zc2) with _no_ requestID 
         attached
       * client_2 gets a error notification (for zc1) with its requestID 
         attached and a AlreadyInMempool error code

     . #b2 parses, the following notifications are pushed:
       * client_2 gets a new zc notification (for zc4) with its requestID
         attached

     case 4 (mismatch):
     . client_1 pushes zc4
     . #b1 parses, the following notifications are pushed:
       * client_2 gets a new zc notification (for zc4) with _no_
         requestID attached
       * client_1 gets no notification

     case 5 (mismatch):
     . zc4 is already in the mempool
     . client_1 pushes zc4
     . zc4 fails to parse, client_1 receives an error with its requestID and
       a AlreadyInMempool error code.
     . client_2 receives no notification
     

   #####

   Scenarios for dishing out ZC notifications:

   1. No requestor, this packet is coming from the P2P node, notify all BDVs 
      accordingly and set all requestor IDs as empty.

   2. Primary requestor set, no extra requestors in the watcher map. Notify all 
      BDVs accordingly, set the requestor ID only for the relevant BDV.

   3. Primary requestor with extra requestors:
      - Requestor's ID is passed to the relevant BDV for all zc (they were 
        all pushed by this BDV)

      - secondary requestors notifications are broken down in 3 to avoid leaking 
      info about the primary requestor: 

         a. a notification for all zc that were requested as extra, with the
            request ID attached

         b. an error notification for all zc were requested as extra, with the
            request ID attached and a AlreadyInMempool error code

         c. a notification for all zc that were not requested but which addresses
            are watched, with no request ID attached
***/

#ifndef ZEROCONF_NOTIFICATIONS_H_
#define ZEROCONF_NOTIFICATIONS_H_

#include <memory>
#include <set>
#include <map>
#include <string>

#include "ThreadSafeClasses.h"
#include "BinaryData.h"
#include "ArmoryErrors.h"

class MempoolSnapshot;
class LedgerEntry;
class TxIOPair;
struct ParsedZCData;

namespace capnp {
   class MessageReader;
}

////////////////////////////////////////////////////////////////////////////////
struct ZcPurgePacket
{
   std::map<BinaryData, BinaryData> invalidatedZcKeys_;
   std::map<BinaryData, std::set<BinaryData>> scrAddrToTxioKeys_;
   std::shared_ptr<MempoolSnapshot> ssPtr_;
};

////////////////////////////////////////////////////////////////////////////////
struct WatcherTxBody
{
   std::shared_ptr<BinaryData> rawTxPtr_;
   bool inved_ = false;
   bool ignoreWatcherNodeInv_ = false;

   //<bdv id>
   std::set<std::string> extraRequestors_;

   WatcherTxBody(std::shared_ptr<BinaryData> rawTx) :
      rawTxPtr_(rawTx)
   {}
};

typedef std::map<BinaryData, std::shared_ptr<std::set<BinaryDataRef>>> KeyAddrMap;

////////////////////////////////////////////////////////////////////////////////
struct ZcNotificationPacket
{
   std::string bdvID_;
   std::map<BinaryData, std::set<BinaryData>> scrAddrToTxioKeys_;

   std::shared_ptr<ZcPurgePacket> purgePacket_;
   std::shared_ptr<KeyAddrMap> newKeysAndScrAddr_;

   //<tx hash, requestor id>
   std::map<BinaryData, std::string> requestorMap_;

   std::string primaryRequestor_;

   //keep a reference to the snapshot so that other references live as long 
   //as this object
   std::shared_ptr<MempoolSnapshot> ssPtr_;

public:
   ZcNotificationPacket(const std::string& bdvID) :
      bdvID_(bdvID)
   {}
};

////////////////////////////////////////////////////////////////////////////////
class ZeroConfCallbacks
{
public:
   virtual ~ZeroConfCallbacks(void) = 0;

   virtual std::set<std::string> hasScrAddr(const BinaryDataRef&) const = 0;
   virtual void pushZcNotification(
      std::shared_ptr<MempoolSnapshot>,
      std::shared_ptr<KeyAddrMap>,
      std::map<std::string, ParsedZCData>, //flaggedBDVs
      const std::string&, //bdvid
      std::map<BinaryData, std::shared_ptr<WatcherTxBody>>&) = 0;
   virtual void pushZcError(const std::string&, const BinaryData&,
      ArmoryErrorCodes, const std::string&) = 0;
};

class Clients;

///////////////////////////////////////////////////////////////////////////////
class ZeroConfCallbacks_BDV : public ZeroConfCallbacks
{
private:
   enum ZcNotifRequestType
   {
      Success,
      Error
   };

   struct ZcNotifRequest
   {
      const ZcNotifRequestType type_;
      const std::string bdvId_;

      ////
      ZcNotifRequest(
         ZcNotifRequestType type,
         const std::string& bdvId) :
         type_(type), bdvId_(bdvId)
      {}

      virtual ~ZcNotifRequest(void) = 0;
   };

   struct ZcNotifRequest_Success : public ZcNotifRequest
   {
      std::shared_ptr<MempoolSnapshot> ssPtr_;
      std::shared_ptr<KeyAddrMap> newZcKeys_;
      std::map<std::string, ParsedZCData> flaggedBDVs_;
      std::map<BinaryData, std::shared_ptr<WatcherTxBody>> watcherMap_;

      ////
      ZcNotifRequest_Success(
         const std::string& bdvId,
         std::shared_ptr<MempoolSnapshot> ssPtr,
         std::shared_ptr<KeyAddrMap> newZcKeys,
         std::map<std::string, ParsedZCData> flaggedBDVs,
         std::map<BinaryData, std::shared_ptr<WatcherTxBody>>& watcherMap) :
         ZcNotifRequest(ZcNotifRequestType::Success, bdvId),
         ssPtr_(ssPtr), newZcKeys_(newZcKeys),
         flaggedBDVs_(std::move(flaggedBDVs)),
         watcherMap_(std::move(watcherMap))
      {}
   };

   struct ZcNotifRequest_Error : public ZcNotifRequest
   {
      const BinaryData hash_;
      ArmoryErrorCodes errCode_;
      const std::string verbose_;

      ////
      ZcNotifRequest_Error(
         const std::string& bdvId,
         const BinaryData& hash, ArmoryErrorCodes errCode,
         const std::string& verbose) :
         ZcNotifRequest(ZcNotifRequestType::Error, bdvId),
         hash_(hash), errCode_(errCode), verbose_(verbose)
      {}
   };

private:
   Clients * clientsPtr_;
   Armory::Threading::BlockingQueue<
      std::shared_ptr<ZeroConfCallbacks_BDV::ZcNotifRequest>> requestQueue_;

   std::thread requestThread_;

private:
   void processNotifRequests(void);

public:
   ZeroConfCallbacks_BDV(Clients* clientsPtr);
   ~ZeroConfCallbacks_BDV(void);

   std::set<std::string> hasScrAddr(const BinaryDataRef&) const override;
   void pushZcError(const std::string&, const BinaryData&,
      ArmoryErrorCodes, const std::string&) override;

   void pushZcNotification(
      std::shared_ptr<MempoolSnapshot>,
      std::shared_ptr<KeyAddrMap>,
      std::map<std::string, ParsedZCData>,
      const std::string&,
      std::map<BinaryData, std::shared_ptr<WatcherTxBody>>&) override;
};

#endif