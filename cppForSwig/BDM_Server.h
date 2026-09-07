////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig.                                         //
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
#include "BlockDataViewer.h"
#include "BDV_Notification.h"

#define MAX_CONTENT_LENGTH 1024*1024*1024
#define CALLBACK_EXPIRE_COUNT 5

class BDV_Server_Object;
using BdvPtr = std::shared_ptr<BDV_Server_Object>;

struct btc_pubkey_;
namespace Armory
{
   namespace ZeroConf
   {
      class ZeroConfCallbacks_BDV;
   }

   namespace Network
   {
      class WebSocketMessagePartial;
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
   const uint32_t messageId_;
   BinaryData packetData_;
   BdvPtr bdvPtr_;
   const Armory::Types::BdvId bdvID_;
   const BinaryDataRef pubkey_;
   uint32_t messageID_ = UINT32_MAX;

public:
   BDV_Payload(BinaryData, BdvPtr, Armory::Types::BdvId, const BinaryDataRef&);

   uint32_t getMessageID(void) const;
   uint64_t getBdvID(void) const;
   const BinaryDataRef& getPubkey(void) const;

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

   virtual void push(std::unique_ptr<Armory::Network::Socket_WritePayload>) = 0;
   virtual bool isValid(void) = 0;
   virtual void shutdown(void) = 0;
};

///////////////////////////////////////////////////////////////////////////////
class WS_Callback : public Callback
{
private:
   const Armory::Types::BdvId bdvID_;

public:
   WS_Callback(const uint64_t& bdvid) :
      bdvID_(bdvid)
   {}

   void push(std::unique_ptr<Armory::Network::Socket_WritePayload>) override;
   bool isValid(void) override { return true; }
   void shutdown(void) override {}
};

///////////////////////////////////////////////////////////////////////////////
class UnitTest_Callback : public Callback
{
private:
   Armory::Threading::BlockingQueue<
      std::unique_ptr<Armory::Network::Socket_WritePayload>> notifQueue_;

public:
   void push(std::unique_ptr<Armory::Network::Socket_WritePayload>) override;
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

   const Armory::Types::BdvId bdvID_;
   std::mutex registerWalletMutex_;
   std::mutex processPacketMutex_;
   std::deque<WalletRegistrationRequest> walletRegistrationQueue_;

   std::shared_ptr<std::promise<bool>> isReadyPromise_;
   std::shared_future<bool> isReadyFuture_;

   std::function<void(std::unique_ptr<BDV_Notification>)> notifLambda_;
   std::atomic<unsigned> packetProcess_threadLock_;
   std::atomic<unsigned> notificationProcess_threadLock_;

   std::map<unsigned, Armory::Network::WebSocketMessagePartial> messageMap_;
   unsigned lastValidMessageId_ = 0;
   std::vector<uint8_t> scratchPad_;

public:
   std::unique_ptr<Callback> notifications_;

private:
   BDV_Server_Object(BDV_Server_Object&) = delete; //no copies
   void setup(void);
   Armory::Network::WebSocketMessagePartial
   preparePayload(std::shared_ptr<BDV_Payload>);
   std::unique_ptr<BDV_Notification_ZC> createZcNotification(
      const std::set<Armory::Types::ScrAddr>&);

public:
   BDV_Server_Object(Armory::Types::BdvId, std::shared_ptr<BlockDataManager>);
   ~BDV_Server_Object(void);

   void startThreads(void);
   Armory::Types::BdvId getID(void) const;
   void registerWallet(WalletRegistrationRequest&);
   void processNotification(std::shared_ptr<BDV_Notification>);
   void init(void);
   void haltThreads(void);
   std::vector<uint8_t>& getScratchPad(void);
   void flagRefresh(BDV_refresh, const std::string&);
};

///////////////////////////////////////////////////////////////////////////////
struct BDVMap
{
   std::map<uint64_t, BdvPtr> bdvs;
   mutable std::mutex mu;

   void add(BdvPtr);
   void del(Armory::Types::BdvId);
   BdvPtr get(Armory::Types::BdvId) const;
   std::map<Armory::Types::BdvId, BdvPtr> get(void) const;
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
   Armory::Threading::BlockingQueue<Armory::Types::BdvId> unregBDVQueue_;
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
   BdvPtr get(Armory::Types::BdvId) const;
   bool registerBDV(const std::string&, Armory::Types::BdvId);
   void unregisterBDV(Armory::Types::BdvId);
   void shutdown(void);
   std::shared_ptr<BlockDataManager> bdm(void) const;

   void queuePayload(std::shared_ptr<BDV_Payload>&);
   std::unique_ptr<Armory::Network::Socket_WritePayload> processCommand(
      std::shared_ptr<BDV_Payload>);
   void rpcBroadcast(RpcBroadcastPacket&);
   void p2pBroadcast(Armory::Types::BdvId, std::vector<BinaryDataRef>&);
   void setMasterIsConnected(bool);
};
