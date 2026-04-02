////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <future>

#include <Utils/ArmoryErrors.h>
#include "BitcoinP2P.h"
#include "BlockDataViewer.h"
#include "BDV_Notification.h"

#define MAX_CONTENT_LENGTH 1024*1024*1024
#define CALLBACK_EXPIRE_COUNT 5

class BDV_Server_Object;
class WebSocketMessagePartial;
using BdvPtr = std::shared_ptr<BDV_Server_Object>;

struct btc_pubkey_;
namespace Armory
{
   namespace ZeroConf
   {
      class ZeroConfCallbacks_BDV;
   }
}

///////////////////////////////////////////////////////////////////////////////
struct RpcBroadcastPacket
{
   BdvPtr bdvPtr_;
   std::shared_ptr<BinaryData> rawTx_;
   std::set<BdvPtr> extraRequestors_;
};

///////////////////////////////////////////////////////////////////////////////
class BDV_Payload
{
private:
   BinaryData packetData_;
   BdvPtr bdvPtr_;
   const BdvIdKey bdvID_;
   const btc_pubkey_& pubkey_;
   uint32_t messageID_ = UINT32_MAX;

public:
   BDV_Payload(BinaryData, BdvPtr, BdvIdKey, const btc_pubkey_&);

   uint32_t getMessageID(void) const;
   void setMessageID(uint32_t);

   uint64_t getBdvID(void) const;
   const btc_pubkey_& getPubkey(void) const;

   const BinaryData& getData(void) const;
   BinaryData&& moveData(void);

   BdvPtr getBdvPtr(void) const;
   BdvPtr&& moveBdvPtr(void);
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
   const BdvIdKey bdvID_;

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

   const BdvIdKey bdvID_;
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
   std::map<std::string, Armory::Ledgers::Delegate> delegateMap_;
   std::unique_ptr<Callback> notifications_;

private:
   BDV_Server_Object(BDV_Server_Object&) = delete; //no copies
   void populateWallets(std::map<std::string, WalletRegistrationRequest>&);
   void setup(void);
   WebSocketMessagePartial preparePayload(std::shared_ptr<BDV_Payload>);
   std::unique_ptr<BDV_Notification_ZC> createZcNotification(
      const std::set<BinaryDataRef>&);

public:
   BDV_Server_Object(BdvIdKey, std::shared_ptr<BlockDataManager>);
   ~BDV_Server_Object(void);

   void startThreads(void);
   BdvIdKey getID(void) const;
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

   void flagRefresh(BDV_refresh, const std::string&,
      std::unique_ptr<BDV_Notification_ZC>);
};

///////////////////////////////////////////////////////////////////////////////
struct BDVMap
{
   std::map<uint64_t, BdvPtr> bdvs;
   mutable std::mutex mu;

   void add(BdvPtr);
   void del(BdvIdKey);
   BdvPtr get(BdvIdKey) const;
   std::map<BdvIdKey, BdvPtr> get(void) const;
};

////
class Clients
{
   friend class Armory::ZeroConf::ZeroConfCallbacks_BDV;

private:
   BDVMap BDVs_;
   mutable Armory::Threading::BlockingQueue<bool> gcCommands_;
   std::shared_ptr<BlockDataManager> bdm_;
   std::atomic<bool> run_;
   std::atomic<bool> masterIsConnected_;

   std::vector<std::thread> controlThreads_;
   std::thread unregThread_;

   mutable Armory::Threading::BlockingQueue<std::shared_ptr<BDV_Notification>> outerBDVNotifStack_;
   Armory::Threading::BlockingQueue<std::shared_ptr<BDV_Notification_Packet>> innerBDVNotifStack_;
   Armory::Threading::BlockingQueue<std::shared_ptr<BDV_Payload>> packetQueue_;
   Armory::Threading::BlockingQueue<BdvIdKey> unregBDVQueue_;
   Armory::Threading::BlockingQueue<RpcBroadcastPacket> rpcBroadcastQueue_;

   std::mutex shutdownMutex_;

private:
   void notificationThread(void);
   void unregisterAllBDVs(void);
   void bdvMaintenanceLoop(void);
   void bdvMaintenanceThread(void);
   void messageParserThread(void);
   void unregisterBDVThread(void);

   void broadcastThroughRPC(void);
   void parseStandAlonePayload(std::shared_ptr<BDV_Payload>);

public:
   Clients(std::shared_ptr<BlockDataManager>);

   void init(void);
   BdvPtr get(BdvIdKey) const;
   bool registerBDV(const std::string&, BdvIdKey);
   void unregisterBDV(BdvIdKey);
   void shutdown(void);
   std::shared_ptr<BlockDataManager> bdm(void) const;

   void queuePayload(std::shared_ptr<BDV_Payload>&);
   std::unique_ptr<Socket_WritePayload> processCommand(
      std::shared_ptr<BDV_Payload>);
   void rpcBroadcast(RpcBroadcastPacket&);
   void p2pBroadcast(BdvIdKey, std::vector<BinaryDataRef>&);
   void setMasterIsConnected(bool);
};
