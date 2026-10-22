////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <condition_variable>
#include <memory>
#include <list>
#include <string>
#include <map>
#include <functional>

#include <Utils/BinaryData.h>
#include <Utils/ReentrantLock.h>

namespace Armory
{
   namespace Network
   {
      class HttpSocket;
   }
}

namespace JSON
{
   class Object;
}

////////
namespace Node
{
   enum class NodeState : int
   {
      Offline = 0,
      Online,
      OffSync
   };

   enum class RpcState : int
   {
      Disabled = 0,
      BadAuth,
      Online,
      Error_28
   };

   enum class ChainState : int
   {
      Unknown = 0,
      Syncing,
      Ready
   };

   ////////
   class RpcError : public std::runtime_error
   {
   public:
      RpcError(void);
      RpcError(const std::string&);
   };

   ////////
   namespace Core
   {
      namespace RPC
      {
         class Client;
      }
   }
   class ChainStatus
   {
      friend class Core::RPC::Client;

   private:
      std::list<std::tuple<unsigned, uint64_t, uint64_t>> heightTimeVec_;
      ChainState state_ = ChainState::Unknown;
      float blockSpeed_ = 0.0f;
      uint64_t eta_ = 0;
      float pct_ = 0.0f;
      unsigned blocksLeft_ = 0;
      unsigned prevPctInt_ = 0;

   private:
      bool processState(const JSON::Object&);

   public:
      void reset(void);
      void appendHeightAndTime(unsigned, uint64_t);

      unsigned getTopBlock(void) const;
      ChainState state(void) const;
      float getBlockSpeed(void) const;
      float getProgressPct(void) const;
      uint64_t getETA(void) const;
      unsigned getBlocksLeft(void) const;
   };

   ////////
   struct Status
   {
      NodeState state = NodeState::Offline;
      RpcState rpcState = RpcState::Disabled;
      bool segWitEnabled = false;
      ChainStatus chainStatus;
   };

   ////////
   namespace Core
   {
      namespace RPC
      {
         /***
         NOTE:
            "state" suffix is for enums
            "status" suffix is for classes/structs
         ***/

         static constexpr std::string_view FEE_STRAT_CONSERVATIVE{"CONSERVATIVE"};
         static constexpr std::string_view FEE_STRAT_ECONOMICAL{"ECONOMICAL"};

         ////////
         struct FeeEstimate
         {
            const bool smartFee;
            const float feeByte;
            const std::string error;
         };

         struct FeeEstimateCache
         {
            using EstimateMap = std::map<std::string,
               std::map<unsigned, FeeEstimate>>;
            const EstimateMap estimates;
            const uint32_t timestamp;

            FeeEstimateCache(EstimateMap);
            bool isValid(void) const;
         };

         ////////
         class Iface : public Lockable
         {
         protected:
            ChainStatus nodeChainStatus_;
            std::unique_ptr<FeeEstimateCache> cachedFees_;
            std::atomic<bool> enabled_ = { true };
            std::condition_variable sleepCV_;
            std::mutex sleepMutex_;

         private:
            void initAfterLock(void) override {}
            void cleanUpBeforeUnlock(void) override {}

         public:
            Iface(void);
            virtual ~Iface(void) = 0;
            virtual bool requestNodeShutdown(void) = 0;
            void disable(void);

            virtual int broadcastTx(const BinaryDataRef&, std::string&) = 0;
            virtual RpcState testConnection() = 0;
            virtual void waitOnChainSync(std::function<void(void)>, bool) = 0;
            virtual FeeEstimate getFeeByte(
               unsigned, const std::string&) = 0;
            virtual const std::map<unsigned, FeeEstimate>& getFeeSchedule(
               const std::string&) = 0;

            //locals
            const ChainStatus& getChainStatus(void) const;
         };

         ////////
         class Client : public Iface
         {
         private:
            std::string basicAuthString64_;
            bool canResetAuthString_;
            RpcState previousState_ = RpcState::Disabled;

         private:
            std::string queryRPC(JSON::Object&);
            std::string queryRPC(Armory::Network::HttpSocket&, JSON::Object&);
            bool setupConnection(Armory::Network::HttpSocket&);

            float queryFeeByte(Armory::Network::HttpSocket&, unsigned);
            FeeEstimate queryFeeByteSmart(
               Armory::Network::HttpSocket&, unsigned&, const std::string&);
            void aggregateFeeEstimates(void);
            void resetAuthString(void);
            bool updateChainStatus(void);

         public:
            Client(const std::string&, const std::string&);

            //virtuals
            bool requestNodeShutdown(void) override;
            RpcState testConnection(void) override;

            const std::map<unsigned, FeeEstimate>& getFeeSchedule(
               const std::string&) override;
            FeeEstimate getFeeByte(unsigned, const std::string&) override;
            int broadcastTx(const BinaryDataRef&, std::string&) override;
            void waitOnChainSync(std::function<void(void)>, bool) override;
         };
      } //namespace RPC
   } //namespace Core
} //namespace Node
