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
#include <filesystem>

#include <Utils/BinaryData.h>
#include <Utils/ReentrantLock.h>

class HttpSocket;

namespace JSON
{
   class Object;
}

namespace CoreRPC
{
   /***
   NOTE:
      "state" suffix is for enums
      "status" suffix is for classes/structs
   ***/

   static constexpr std::string_view FEE_STRAT_CONSERVATIVE{"CONSERVATIVE"};
   static constexpr std::string_view FEE_STRAT_ECONOMICAL{"ECONOMICAL"};

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
   struct FeeEstimateResult
   {
      bool smartFee = false;
      float feeByte = 0.0f;
      std::string error;
   };

   using EstimateCache = std::map<std::string,
      std::map<unsigned, FeeEstimateResult>>;

   ////////
   class NodeChainStatus
   {
      friend class NodeRPC;

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
   struct NodeStatus
   {
      NodeState state = NodeState::Offline;
      RpcState rpcState = RpcState::Disabled;
      bool segWitEnabled = false;
      NodeChainStatus chainStatus;
   };

   ////////
   class NodeRPCInterface : public Lockable
   {
   protected:
      std::function<void(void)> nodeStatusLambda_;
      NodeChainStatus nodeChainStatus_;
      std::atomic<std::shared_ptr<EstimateCache>> currentEstimateCache_;

   private:
      void initAfterLock(void) override {}
      void cleanUpBeforeUnlock(void) override {}

   protected:
      void callback(void) const;

   public:
      NodeRPCInterface(void);
      virtual ~NodeRPCInterface(void) = 0;
      virtual void shutdown(void) = 0;

      virtual int broadcastTx(const BinaryDataRef&, std::string&) = 0;
      virtual bool canPoll(void) const = 0;
      virtual RpcState testConnection() = 0;
      virtual void waitOnChainSync(std::function<void(void)>) = 0;
      virtual FeeEstimateResult getFeeByte(
         unsigned, const std::string&) const = 0;

      //locals
      const NodeChainStatus& getChainStatus(void) const;
      void registerNodeStatusLambda(const std::function<void(void)>&);
      const std::map<unsigned, FeeEstimateResult>& getFeeSchedule(
         const std::string&) const;
   };

   ////////
   class NodeRPC : public NodeRPCInterface
   {
   private:
      std::string basicAuthString64_;

      RpcState previousState_ = RpcState::Disabled;
      std::condition_variable pollCondVar_;
      std::vector<std::thread> thrVec_;
      std::atomic<bool> run_ = { true };

   private:
      std::string getAuthString(void);
      std::filesystem::path getDatadir(void);

      std::string queryRPC(JSON::Object&);
      std::string queryRPC(HttpSocket&, JSON::Object&);
      void pollThread(void);
      
      float queryFeeByte(HttpSocket&, unsigned);
      FeeEstimateResult queryFeeByteSmart(
         HttpSocket&, unsigned&, const std::string&);
      void aggregateFeeEstimates(void);
      void resetAuthString(void);
      bool updateChainStatus(void);

   public:
      NodeRPC(void);
      ~NodeRPC(void);

      bool setupConnection(HttpSocket&);

      //virtuals
      void shutdown(void) override;
      RpcState testConnection(void) override;
      bool canPoll(void) const override;

      FeeEstimateResult getFeeByte(unsigned, const std::string&) const override;
      int broadcastTx(const BinaryDataRef&, std::string&) override;
      void waitOnChainSync(std::function<void(void)>) override;
   };
} //namespace CoreRPC
