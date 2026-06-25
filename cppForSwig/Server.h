////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _SERVER_H_
#define _SERVER_H_

#include <string>
#include <memory>
#include <atomic>
#include <vector>
#include <list>

#include <Utils/ThreadSafeClasses.h>
#include <Utils/BinaryData.h>
#include <Network/SocketService.h>
#include <libwebsockets.h>

#define SERVER_AUTH_PEER_FILENAME "server.peers"

class SecureBinaryData;
class Clients;
class BlockDataManager;
class BIP151Connection;
struct AuthPeersLambdas;
struct btc_pubkey_;

namespace Armory
{
   namespace Wallets
   {
      class AuthorizedPeers;

      namespace IO
      {
         struct ReadOnlyFileParams;
      }
   }

   namespace Network
   {
      struct Socket_WritePayload;
      class SerializedMessage;
   }
}

///////////////////////////////////////////////////////////////////////////////
struct per_session_data__http {
   lws_fop_fd_t fop_fd;
};

struct per_session_data__bdv {
   static const unsigned rcv_size = 8000;
   uint64_t id_;
};

enum demo_protocols {
   /* always first */
   PROTOCOL_HTTP = 0,

   PROTOCOL_ARMORY_BDM,

   /* always last */
   DEMO_PROTOCOL_COUNT
};

int callback_http(struct lws *wsi, enum lws_callback_reasons reason, void *user,
   void *in, size_t len);

///////////////////////////////////////////////////////////////////////////////
struct BDV_packet
{
   uint64_t bdvID_;
   BinaryData data_;

   BDV_packet(const uint64_t& id) :
      bdvID_(id)
   {}
};

///////////////////////////////////////////////////////////////////////////////
struct PendingMessage
{
   const uint64_t id;
   const uint32_t msgid;
   std::unique_ptr<Armory::Network::Socket_WritePayload> payload;

   PendingMessage(uint64_t, uint32_t, std::unique_ptr<
      Armory::Network::Socket_WritePayload>);
};

///////////////////////////////////////////////////////////////////////////////
class ClientConnection
{
public:
   struct lws *wsiPtr_ = nullptr;

private:
   const uint64_t id_;
   BinaryData readLeftOverData_;

public:
   std::shared_ptr<BIP151Connection> bip151Connection_;
   std::shared_ptr<std::atomic<unsigned>> writeLock_, readLock_;
   std::chrono::time_point<std::chrono::system_clock> outKeyTimePoint_;
   std::shared_ptr<std::atomic<int>> run_;

   std::shared_ptr<Armory::Threading::Queue<BinaryData>> readQueue_;

private:
   void processAEADHandshake(BinaryData);

public:
   ClientConnection(struct lws*, uint64_t, AuthPeersLambdas&, bool);

   void closeConnection(void);
   void processReadQueue(std::shared_ptr<Clients>);
   bool isMaster(void) const;
};

///////////////////////////////////////////////////////////////////////////////
class WebSocketServer
{
private:
   std::vector<std::thread> threads_;
   Armory::Threading::BlockingQueue<std::shared_ptr<BDV_packet>> packetQueue_;
   Armory::Threading::TransactionalMap<uint64_t, ClientConnection> clientStateMap_;

   static std::atomic<WebSocketServer*> instance_;
   static std::mutex mu_;
   static std::promise<bool> shutdownPromise_;
   static std::shared_future<bool> shutdownFuture_;
   BinaryData encInitPacket_;

   std::shared_ptr<Clients> clients_;
   std::atomic<unsigned> run_;
   std::promise<bool> isReadyProm_;

   Armory::Threading::BlockingQueue<std::unique_ptr<PendingMessage>> msgQueue_;
   Armory::Threading::BlockingQueue<uint64_t> clientConnectionInterruptQueue_;

   std::shared_ptr<Armory::Wallets::AuthorizedPeers> authorizedPeers_;
   std::map<struct lws*, std::list<std::list<BinaryData>>> writeMap_;
   lws_context* contextPtr_;
   Armory::Threading::Queue<std::pair<struct lws*, std::list<BinaryData>>> writeQueue_;

   std::set<struct lws*> pendingWrites_;
   std::set<struct lws*>::const_iterator pendingWritesIter_;

   //default to 2-way auth
   bool oneWayAuth_ = false;

public:
   void writeToSocket(struct lws*, Armory::Network::SerializedMessage&);

private:
   void webSocketService(int port);
   void commandThread(void);
   void setIsReady(void);

   void prepareWriteThread(void);

   AuthPeersLambdas getAuthPeerLambda(bool) const;
   void closeClientConnection(uint64_t);
   void clientInterruptThread(void);

   void updateWriteMap(void);

public:
   WebSocketServer(void);

   static WebSocketServer* getInstance(void);
   static int callback(
      struct lws *wsi, enum lws_callback_reasons reason,
      void *user, void *in, size_t len);

   static void init(void);
   static void initAuthPeers(const Armory::Wallets::IO::ReadOnlyFileParams&);
   static void initAuthPeers(std::shared_ptr<Armory::Wallets::AuthorizedPeers>);
   static void start(std::shared_ptr<BlockDataManager>, bool);
   static void shutdown(void);
   static void waitOnShutdown(void);
   static SecureBinaryData getPublicKey(void);
   static bool isMasterKey(const btc_pubkey_&);

   static void write(const uint64_t&, const uint32_t&,
      std::unique_ptr<Armory::Network::Socket_WritePayload>);

   std::shared_ptr<const std::map<uint64_t, ClientConnection>>
      getConnectionStateMap(void) const;
   void addId(const uint64_t&, struct lws* ptr);
   void eraseId(const uint64_t&, struct lws* ptr);
};

#endif
