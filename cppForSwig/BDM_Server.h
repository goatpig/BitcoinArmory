////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2021, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _BDM_SERVER_H
#define _BDM_SERVER_H

#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <future>

#include "BitcoinP2p.h"
#include "BlockDataViewer.h"
#include "EncryptionUtils.h"
#include "LedgerEntry.h"
#include "DbHeader.h"
#include "BDV_Notification.h"
#include "ZeroConf.h"
#include "Server.h"
#include "BtcWallet.h"
#include "ArmoryErrors.h"
#include "ZeroConfNotifications.h"

#define MAX_CONTENT_LENGTH 1024*1024*1024
#define CALLBACK_EXPIRE_COUNT 5

class BDV_Server_Object;

///////////////////////////////////////////////////////////////////////////////
struct RpcBroadcastPacket
{
   std::shared_ptr<BDV_Server_Object> bdvPtr_;
   std::shared_ptr<BinaryData> rawTx_;
   std::string requestID_;

   std::map<std::string, std::shared_ptr<BDV_Server_Object>> extraRequestors_;
};

///////////////////////////////////////////////////////////////////////////////
struct BDV_Payload
{
   BinaryData packetData_;
   std::shared_ptr<BDV_Server_Object> bdvPtr_;
   uint32_t messageID_;
   uint64_t bdvID_;
   std::string hexID;
};

///////////////////////////////////////////////////////////////////////////////
class Callback
{
public:

   virtual ~Callback() = 0;

   virtual void push(std::unique_ptr<Socket_WritePayload>) = 0;
   virtual bool isValid(void) = 0;
   virtual void shutdown(void) = 0;
};

///////////////////////////////////////////////////////////////////////////////
class WS_Callback : public Callback
{
private:
   const uint64_t bdvID_;

public:
   WS_Callback(const uint64_t& bdvid) :
      bdvID_(bdvid)
   {}

   void push(std::unique_ptr<Socket_WritePayload>) override;
   bool isValid(void) override { return true; }
   void shutdown(void) override {}
};

///////////////////////////////////////////////////////////////////////////////
class UnitTest_Callback : public Callback
{
private:
   Armory::Threading::BlockingQueue<std::unique_ptr<Socket_WritePayload>> notifQueue_;

public:
   void push(std::unique_ptr<Socket_WritePayload>) override;
   bool isValid(void) override { return true; }
   void shutdown(void) override {}

   BinaryData getNotification(void);
};

///////////////////////////////////////////////////////////////////////////////
class BDV_Server_Object : public BlockDataViewer
{
   friend class Clients;

private:
   std::atomic<unsigned> started_;
   std::thread initT_;

   std::string bdvID_;
   BlockDataManagerThread* bdmT_;

   std::mutex registerWalletMutex_;
   std::mutex processPacketMutex_;
   std::map<std::string, WalletRegistrationRequest> wltRegMap_;

   std::shared_ptr<std::promise<bool>> isReadyPromise_;
   std::shared_future<bool> isReadyFuture_;

   std::function<void(std::unique_ptr<BDV_Notification>)> notifLambda_;
   std::atomic<unsigned> packetProcess_threadLock_;
   std::atomic<unsigned> notificationProcess_threadLock_;

   std::map<unsigned, WebSocketMessagePartial> messageMap_;
   unsigned lastValidMessageId_ = 0;
   std::vector<uint8_t> scratchPad_;

public:
   std::map<std::string, LedgerDelegate> delegateMap_;
   std::unique_ptr<Callback> notifications_;

private:
   BDV_Server_Object(BDV_Server_Object&) = delete; //no copies
   void populateWallets(std::map<std::string, WalletRegistrationRequest>&);
   void setup(void);
   WebSocketMessagePartial preparePayload(std::shared_ptr<BDV_Payload>);
   std::unique_ptr<BDV_Notification_ZC> createZcNotification(
      const std::set<BinaryDataRef>&);

public:
   BDV_Server_Object(const std::string& id, BlockDataManagerThread *bdmT);
   ~BDV_Server_Object(void)
   { 
      haltThreads();
   }

   void startThreads(void);
   const std::string& getID(void) const { return bdvID_; }
   void registerWallet(WalletRegistrationRequest&);
   void processNotification(std::shared_ptr<BDV_Notification>);
   void init(void);
   void haltThreads(void);
   std::vector<uint8_t>& getScratchPad(void);

   /*
   Creates a delegate, inserts it in the delegate map and returns the id.
   Also checks if the delegate already exists
   */
   const std::string& getLedgerDelegate(void); //the bdv itself
   const std::string& getLedgerDelegate(const std::string&); //walletId
   const std::string& getLedgerDelegate(
      const std::string&, const BinaryData&); //walletId, address

   void flagRefresh(
      BDV_refresh refresh, const BinaryData& refreshId,
      std::unique_ptr<BDV_Notification_ZC> zcPtr);
};

///////////////////////////////////////////////////////////////////////////////
class Clients
{
   friend class ZeroConfCallbacks_BDV;

private:
   Armory::Threading::TransactionalMap<std::string, std::shared_ptr<BDV_Server_Object>> BDVs_;
   mutable Armory::Threading::BlockingQueue<bool> gcCommands_;
   BlockDataManagerThread* bdmT_ = nullptr;

   std::function<void(void)> shutdownCallback_;

   std::atomic<bool> run_;

   std::vector<std::thread> controlThreads_;
   std::thread unregThread_;

   mutable Armory::Threading::BlockingQueue<std::shared_ptr<BDV_Notification>> outerBDVNotifStack_;
   Armory::Threading::BlockingQueue<std::shared_ptr<BDV_Notification_Packet>> innerBDVNotifStack_;
   Armory::Threading::BlockingQueue<std::shared_ptr<BDV_Payload>> packetQueue_;
   Armory::Threading::BlockingQueue<std::string> unregBDVQueue_;
   Armory::Threading::BlockingQueue<RpcBroadcastPacket> rpcBroadcastQueue_;

   std::mutex shutdownMutex_;

private:
   void notificationThread(void) const;
   void unregisterAllBDVs(void);
   void bdvMaintenanceLoop(void);
   void bdvMaintenanceThread(void);
   void messageParserThread(void);
   void unregisterBDVThread(void);

   void broadcastThroughRPC(void);
   void parseStandAlonePayload(std::shared_ptr<BDV_Payload>);

public:
   Clients(void)
   {}

   Clients(BlockDataManagerThread* bdmT,
      std::function<void(void)> shutdownLambda)
   {
      init(bdmT, shutdownLambda);
   }

   void init(BlockDataManagerThread* bdmT,
      std::function<void(void)> shutdownLambda);

   std::shared_ptr<BDV_Server_Object> get(const std::string& id) const;
   bool registerBDV(const std::string&, const std::string&);
   void unregisterBDV(std::string bdvId);
   void shutdown(void);
   void exitRequestLoop(void);
   BlockDataManagerThread* bdmT(void) const;

   void queuePayload(std::shared_ptr<BDV_Payload>& payload)
   {
      packetQueue_.push_back(move(payload));
   }

   std::unique_ptr<Socket_WritePayload> processCommand(
      std::shared_ptr<BDV_Payload>);
   void rpcBroadcast(RpcBroadcastPacket&);
   void p2pBroadcast(const std::string&, std::vector<BinaryDataRef>&);
};

#endif
