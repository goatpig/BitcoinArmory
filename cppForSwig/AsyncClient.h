////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <thread>
#include <list>

#include <Utils/ReentrantLock.h>
#include <Utils/Types.h>
#include <Network/StringSockets.h>
#include <Network/WebSocketClient.h>
#include <Network/SocketWritePayload.h>
#include "TxClasses.h"
#include "DBClientClasses.h"

namespace Armory
{
   namespace Bridge
   {
      class WalletManager;
      class WalletContainer;
   }

   namespace Wallets
   {
      namespace IO
      {
         struct ReadOnlyFileParams;
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
class ClientMessageError : public std::runtime_error
{
private:
   int errorCode_ = 0;

public:
   ClientMessageError(const std::string& err, unsigned errCode) :
      std::runtime_error(err), errorCode_(errCode)
   {}

   int errorCode(void) const { return errorCode_; }
};

///////////////////////////////////////////////////////////////////////////////
template<class U> class ReturnMessage
{
private:
   U value_;
   std::shared_ptr<ClientMessageError> error_;

public:
   ReturnMessage(void) :
      value_(U())
   {}

   ReturnMessage(U& val) :
      value_(std::move(val))
   {}

   ReturnMessage(const U& val) :
      value_(val)
   {}

   ReturnMessage(ClientMessageError& err)
   {
      error_ = std::make_shared<ClientMessageError>(err);
   }

   U get(void)
   {
      if (error_ != nullptr) {
         throw *error_;
      }
      return std::move(value_);
   }
};

namespace AsyncClient
{
   ///////////////////////////////////////////////////////////////////////////////
   typedef std::shared_ptr<Tx> TxResult;
   typedef std::function<void(ReturnMessage<TxResult>)> TxCallback;

   typedef std::map<BinaryData, TxResult> TxBatchResult;
   typedef std::function<void(ReturnMessage<TxBatchResult>)> TxBatchCallback;

   class BlockDataViewer;

   /////////////////////////////////////////////////////////////////////////////
   class BtcWallet
   {
   protected:
      const std::string walletID_;
      const std::shared_ptr<Armory::Network::SocketPrototype> sock_;
      std::string ledgerID_;

   public:
      BtcWallet(const BlockDataViewer&, const std::string&);

      bool registerAddresses(
         const std::vector<Armory::Types::ScrAddr>&, bool);
      void unregisterAddresses(const std::set<Armory::Types::ScrAddr>&);
      void unregister(void);

      std::string walletID(void) const { return walletID_; }
   };

   /////////////////////////////////////////////////////////////////////////////
   using HeaderVec = std::vector<std::shared_ptr<DBClientClasses::BlockHeader>>;
   class Blockchain
   {
   private:
      const std::shared_ptr<Armory::Network::SocketPrototype> sock_;

   public:
      Blockchain(const BlockDataViewer&);
      void getHeadersByHeight(const std::set<unsigned>&,
         const std::function<void(ReturnMessage<HeaderVec>)>&);
      void getHeadersById(const std::set<Armory::Types::BlockId>&,
         const std::function<void(ReturnMessage<HeaderVec>)>&);
   };

   /////////////////////////////////////////////////////////////////////////////
   class BlockDataViewer
   {
      friend class BtcWallet;
      friend class RemoteCallback;
      friend class LedgerDelegate;
      friend class Blockchain;
      friend class Armory::Bridge::WalletManager;

   private:
      bool registered_ = false;
      std::shared_ptr<Armory::Network::SocketPrototype> sock_;

   private:
      BlockDataViewer(void);
      BlockDataViewer(std::shared_ptr<Armory::Network::SocketPrototype>);
      BlockDataViewer& operator=(const BlockDataViewer&);

   public:
      ~BlockDataViewer(void);
      bool isValid(void) const;
      BtcWallet getWalletObj(const std::string& id);

      //BIP15x
      std::pair<unsigned, unsigned> getRekeyCount(void) const;
      void setCheckServerKeyPromptLambda(
         const std::function<bool(const BinaryData&)>&);
      void addPublicKey(const SecureBinaryData&, bool);

      //connectivity
      bool connectToRemote(void);
      std::shared_ptr<Armory::Network::SocketPrototype>
      getSocketObject(void) const { return sock_; }
      void goOnline(void);
      bool hasRemoteDB(void);

      //setup
      static std::shared_ptr<BlockDataViewer> getNewBDV(
         const std::string&, const std::string&,
         std::shared_ptr<Armory::NetworkPeers::ClientStore>, bool,
         std::shared_ptr<RemoteCallback>);

      void registerWithDB(const std::string&);
      void unregisterFromDB(void);
      void start(void);
      void shutdown(void);
      void shutdownNode(void);

      //ledgers
      void getTxios(uint32_t,
         std::function<void(ReturnMessage<std::vector<TxIOPair>>)>);

      //header data
      Blockchain blockchain(void);

      //node & fee
      void getNodeStatus(
         std::function<void(ReturnMessage<std::shared_ptr<DBClientClasses::NodeStatus>>)>);
      void getFeeSchedule(const std::string&, std::function<void(ReturnMessage<
            std::map<unsigned, DBClientClasses::FeeEstimateStruct>>)>);

      void broadcastZC(const std::vector<BinaryData>&);
      void broadcastThroughRPC(const BinaryData&);

      //db cache methods
      void getTxsByHash(const std::set<Armory::Types::TxHash>&,
         const TxBatchCallback&);
      void getTxsByKey(const std::set<Armory::Types::TxKey>&,
         const std::function<void(ReturnMessage<std::vector<Tx>>)>&);
   };
} //namespace AsyncClient
