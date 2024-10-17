////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_NODERPC_
#define _H_NODERPC_

#include <mutex>
#include <memory>
#include <string>
#include <functional>
#include <filesystem>

#include "SocketObject.h"
#include "StringSockets.h"
#include "BtcUtils.h"

#include "JSON_codec.h"

#include "ReentrantLock.h"
#include "ArmoryConfig.h"

namespace CoreRPC
{
   /***
   Node: "state" suffix is for enums
         "status" suffix is for classes/structs
   ***/

////
enum NodeState
{
   NodeState_Offline = 0,
   NodeState_Online,
   NodeState_OffSync
};

////
enum RpcState
{
   RpcState_Disabled = 0,
   RpcState_BadAuth,
   RpcState_Online,
   RpcState_Error_28
};

////
enum ChainState
{
   ChainState_Unknown = 0,
   ChainState_Syncing,
   ChainState_Ready
};

////
class RpcError : public std::runtime_error
{
public:
   RpcError(void) : 
      std::runtime_error("RpcError")
   {}

   RpcError(const std::string& err) :
      std::runtime_error(err)
   {}
};

class ConfMismatch
{
   const unsigned expected_;
   const unsigned actual_;

public:
   ConfMismatch(unsigned expected, unsigned actual) :
      expected_(expected), actual_(actual)
   {}

   unsigned expected(void) const { return expected_; }
   unsigned actual(void) const { return actual_; }
};

////////////////////////////////////////////////////////////////////////////////
struct FeeEstimateResult
{
   bool smartFee_ = false;
   float feeByte_ = 0;

   std::string error_;
};

typedef std::map<std::string, std::map<unsigned, FeeEstimateResult>> EstimateCache;

////////////////////////////////////////////////////////////////////////////////
class NodeChainStatus
{
   friend class NodeRPC;

private:
   std::list<std::tuple<unsigned, uint64_t, uint64_t> > heightTimeVec_;
   ChainState state_ = ChainState_Unknown;
   float blockSpeed_ = 0.0f;
   uint64_t eta_ = 0;
   float pct_ = 0.0f;
   unsigned blocksLeft_ = 0;

   unsigned prev_pct_int_ = 0;

private:
   bool processState(std::shared_ptr<JSON_object> const getblockchaininfo_obj);

public:
   void appendHeightAndTime(unsigned, uint64_t);
   unsigned getTopBlock(void) const;
   ChainState state(void) const { return state_; }
   float getBlockSpeed(void) const { return blockSpeed_; }

   void reset();

   float getProgressPct(void) const { return pct_; }
   uint64_t getETA(void) const { return eta_; }
   unsigned getBlocksLeft(void) const { return blocksLeft_; }
};

////////////////////////////////////////////////////////////////////////////////
struct NodeStatus
{
   NodeState state_ = NodeState_Offline;
   bool SegWitEnabled_ = false;
   RpcState rpcState_ = RpcState_Disabled;
   NodeChainStatus chainStatus_;
};

////////////////////////////////////////////////////////////////////////////////
class NodeRPCInterface : public Lockable
{
protected:
   std::function<void(void)> nodeStatusLambda_;
   NodeChainStatus nodeChainStatus_;
   std::shared_ptr<EstimateCache> currentEstimateCache_ = nullptr;

private:
   void initAfterLock(void) override {}
   void cleanUpBeforeUnlock(void) override {}

protected:
   void callback(void)
   {
      if (nodeStatusLambda_)
         nodeStatusLambda_();
   }

public:
   virtual ~NodeRPCInterface(void) = 0;
   virtual void shutdown(void) = 0;

   virtual int broadcastTx(const BinaryDataRef&, std::string&) = 0;
   virtual bool canPoll(void) const = 0;
   virtual RpcState testConnection() = 0;
   virtual void waitOnChainSync(std::function<void(void)>) = 0;

   virtual FeeEstimateResult getFeeByte(
      unsigned confTarget, const std::string& strategy) = 0;

   //locals
   const NodeChainStatus& getChainStatus(void) const;
   void registerNodeStatusLambda(std::function<void(void)> lbd) 
   { nodeStatusLambda_ = lbd; }

   std::map<unsigned, FeeEstimateResult> getFeeSchedule(
      const std::string& strategy); 
};

////////////////////////////////////////////////////////////////////////////////
class NodeRPC : public NodeRPCInterface
{
private:
   std::string basicAuthString64_;

   RpcState previousState_ = RpcState_Disabled;
   std::condition_variable pollCondVar_;


   std::vector<std::thread> thrVec_;
   std::atomic<bool> run_ = { true };

private:
   std::string getAuthString(void);
   std::filesystem::path getDatadir(void);

   std::string queryRPC(JSON_object&);
   std::string queryRPC(HttpSocket&, JSON_object&);
   void pollThread(void);
   
   float queryFeeByte(HttpSocket&, unsigned);
   FeeEstimateResult queryFeeByteSmart(HttpSocket&,
      unsigned& confTarget, std::string& strategy);
   void aggregateFeeEstimates(void);
   void resetAuthString(void);
   bool updateChainStatus(void);

public:
   NodeRPC(void);
   ~NodeRPC(void);
   
   bool setupConnection(HttpSocket&);

   //virtuals
   void shutdown(void) override;
   RpcState testConnection() override;
   bool canPoll(void) const override { return true; }

   FeeEstimateResult getFeeByte(
      unsigned confTarget, const std::string& strategy) override;

   int broadcastTx(const BinaryDataRef&, std::string&) override;
   void waitOnChainSync(std::function<void(void)>) override;
};
}; //namespace CoreRPC
#endif