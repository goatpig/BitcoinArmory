////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <exception>
#include <string>
#include <functional>

#include <Utils/BinaryData.h>
#include <Utils/Types.h>
#include <BlockchainDatabase/txio.h>
#include <Network/SocketObject.h>
#include <Node/nodeRPC.h>
#include "bdmenums.h"

#define FILTER_CHANGE_FLAG "wallet_filter_changed"

namespace capnp {
   class MessageReader;
}

////////////////////////////////////////////////////////////////////////////////
struct NoArmoryDBExcept : public std::runtime_error
{
   NoArmoryDBExcept(void) : runtime_error("")
   {}
};

struct BDVAlreadyRegistered : public std::runtime_error
{
   BDVAlreadyRegistered(void) : std::runtime_error("")
   {}
};

////////////////////////////////////////////////////////////////////////////////
namespace DBClientClasses
{
   void initLibrary(void);

   /////////////////////////////////////////////////////////////////////////////
   struct FeeEstimateStruct
   {
      std::string error_;
      float val_ = 0;
      bool isSmart_ = false;

      FeeEstimateStruct(float val, bool isSmart, const std::string& error) :
         error_(error), val_(val), isSmart_(isSmart)
      {}

      FeeEstimateStruct(void)
      {}
   };

   /////////////////////////////////////////////////////////////////////////////
   struct BlockHeader
   {
      const BinaryData thisHash;
      const BinaryData prevHash;

      const Armory::Types::BlockId blockId;
      const uint32_t blockHeight;
      const uint32_t timestamp;
      const uint32_t blockSize;
      const uint32_t numTxs;
      bool           isMainBranch;

      BlockHeader(BinaryDataRef, BinaryDataRef,
         Armory::Types::BlockId,
         uint32_t, uint32_t, uint32_t, uint32_t,
         bool);
   };

   ////////////////////////////////////////////////////////////////////////////
   class NodeChainStatus
   {
   private:
      const Node::ChainState chainState_;
      const float blockSpeed_;
      const float progressPct_;
      const uint64_t etaSeconds_;
      const unsigned blocksLeft_;

   public:
      NodeChainStatus(void);
      NodeChainStatus(Node::ChainState, float, float, uint64_t, unsigned);
      NodeChainStatus(NodeChainStatus&&) = default;

      Node::ChainState state(void) const;
      float getBlockSpeed(void) const;

      float getProgressPct(void) const;
      uint64_t getETA(void) const;
      unsigned getBlocksLeft(void) const;
   };

   ////////////////////////////////////////////////////////////////////////////
   class NodeStatus
   {
   private:
      const Node::NodeState nodeState_;
      const Node::RpcState rpcState_;
      const bool isSegWitEnabled_;
      const NodeChainStatus nodeChainStatus_;

   public:
      NodeStatus(Node::NodeState, Node::RpcState, bool, NodeChainStatus&);

      Node::NodeState state(void) const;
      bool isSegWitEnabled(void) const;
      Node::RpcState rpcState(void) const;
      const NodeChainStatus& chainStatus(void) const;
   };
}; //namespace DBClientClasses

///////////////////////////////////////////////////////////////////////////////
struct BDV_Error_Struct
{
   std::string errorStr_;
   BinaryData errData_;
   int errCode_;

   BinaryData serialize(void) const;
   void deserialize(const BinaryData&);
};

class NewBlockNotif
{
   using BlockIdVec = std::vector<Armory::Types::BlockId>;

private:
   uint32_t height_ = UINT32_MAX;
   uint32_t branchHeight_ = UINT32_MAX;
   BlockIdVec invalidatedBlockIds_;
   BlockIdVec newMainBranchBlockIds_;

public:
   NewBlockNotif(uint32_t, uint32_t, BlockIdVec, BlockIdVec);

   bool isValid(void) const;
   bool isReorg(void) const;
   uint32_t getHeight(void) const;
   uint32_t getBranchHeight(void) const;

   const BlockIdVec& invalidatedBlockIds(void) const;
   const BlockIdVec& newMainBranchBlockIds(void) const;
};

struct BdmNotification
{
   const BDMAction action;

   NewBlockNotif newBlock{UINT32_MAX, UINT32_MAX, {}, {}};

   std::vector<TxIOPair> txios;
   std::set<Armory::Types::TxHash> invalidatedZcHashes;
   std::set<std::string> ids;

   std::shared_ptr<DBClientClasses::NodeStatus> nodeStatus;
   BDV_Error_Struct error;

   std::string requestID;

   BdmNotification(BDMAction);
};

///////////////////////////////////////////////////////////////////////////////
class RemoteCallback
{
public:
   RemoteCallback(void) {}
   virtual ~RemoteCallback(void) = 0;

   virtual void run(BdmNotification) = 0;
   virtual void progress(
      BDMPhase phase,
      const std::vector<std::string> &walletIdVec,
      float progress, unsigned secondsRem,
      unsigned progressNumeric
   ) = 0;
   virtual void disconnected(void) = 0;

   bool processNotifications(std::unique_ptr<capnp::MessageReader>);
};
