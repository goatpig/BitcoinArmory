////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <vector>
#include <map>

#include <Utils/BinaryData.h>
#include "BitcoinP2P.h"
#include "nodeRPC.h"

class BlockFiles;
class LMDBBlockDatabase;
class BlockDataManager;
class BlockHeader;
class Tx;

namespace Armory
{
   namespace Signing
   {
      class ScriptRecipient;
   }
   class Blockchain;
}

////////////////////////////////////////////////////////////////////////////////
struct UnitTestBlock
{
   BinaryData rawHeader;
   BinaryData headerHash;

   std::shared_ptr<Tx> coinbase;
   std::vector<Tx> transactions;

   unsigned height;
   unsigned timestamp;
   BinaryData diffBits;
};

////////////////////////////////////////////////////////////////////////////////
struct MempoolObject
{
   BinaryData rawTx;
   BinaryData hash;
   unsigned order;
   unsigned blocksUntilMined = 0;
   bool staged;

   bool operator<(const MempoolObject&) const;
};

////////////////////////////////////////////////////////////////////////////////
class NodeUnitTest : public Armory::Node::BitcoinNodeInterface
{
   friend class NodeRPC_UnitTest;

private:
   struct MinedHeader
   {
      BinaryData prevHash_;
      unsigned blockHeight_;

      unsigned timestamp_;
      BinaryData diffBits_;
   };

   std::map<BinaryDataRef, std::shared_ptr<MempoolObject>> mempool_;
   std::map<BinaryData, std::map<unsigned, BinaryData>> spenderSet_;
   std::vector<UnitTestBlock> blocks_;
   std::atomic<unsigned> counter_;

   std::shared_ptr<Armory::Blockchain> blockchain_ = nullptr;
   std::shared_ptr<BlockFiles> filesPtr_ = nullptr;
   std::atomic<unsigned> skipZc_ = {0};
   std::mutex sendMessageMutex_;
   std::deque<unsigned> zcDelays_;
   std::deque<unsigned> zcStalls_;

   MinedHeader header_;

   Armory::Threading::TransactionalMap<BinaryData, BinaryData> rawTxMap_;

   static Armory::Threading::BlockingQueue<BinaryData> watcherInvQueue_;
   std::thread watcherThread_;
   LMDBBlockDatabase* iface_ = nullptr;

   std::set<BinaryData> seenHashes_;
   bool checkSigs_ = true;

private:
   void purgeSpender(const BinaryData&);

public:
   NodeUnitTest(uint32_t, bool);
   ~NodeUnitTest(void);

   //locals
   void updateNodeStatus(bool);
   void notifyNewBlock(void);
   void watcherProcess(void);

   std::map<unsigned, BinaryData> mineNewBlock(
      std::shared_ptr<BlockDataManager>,
      unsigned, const BinaryData&, double = 1.0);
   std::map<unsigned, BinaryData> mineNewBlock(
      std::shared_ptr<BlockDataManager>,
      unsigned, Armory::Signing::ScriptRecipient*, double = 1.0);

   std::vector<UnitTestBlock> getMinedBlocks(void) const;
   void setReorgBranchPoint(std::shared_ptr<BlockHeader>);
   void skipZc(unsigned);
   void delayNextZc(unsigned);
   void stallNextZc(unsigned);
   void checkSigs(bool);

   //<raw tx, blocks to wait until mining>
   void pushZC(const std::vector<std::pair<BinaryData, unsigned>>&, bool);
   void evictZC(const BinaryData&);
   uint64_t getFeeForTx(const Tx&) const;

   //set
   void setBlockchain(std::shared_ptr<Armory::Blockchain>);
   void setBlockFiles(std::shared_ptr<BlockFiles>);
   void setIface(LMDBBlockDatabase*);

   //virtuals
   void sendMessage(std::unique_ptr<Armory::Node::Payload>) override;

   void connectToNode(bool) override;
   bool connected(void) const override;
   void shutdown(void) override;

   //misc
   void presentZcHash(const BinaryData&);
};


////////////////////////////////////////////////////////////////////////////////
class NodeRPC_UnitTest : public CoreRPC::NodeRPCInterface
{
private:
   std::shared_ptr<NodeUnitTest> primaryNode_;
   std::shared_ptr<NodeUnitTest> watcherNode_;

   std::deque<unsigned> zcStalls_;
   std::mutex zcStallMutex_;

public:
   NodeRPC_UnitTest(
      std::shared_ptr<NodeUnitTest>,
      std::shared_ptr<NodeUnitTest>
   );

   //virtuals
   void shutdown(void) override;
   CoreRPC::RpcState testConnection(void) override;
   bool canPoll(void) const override;
   void waitOnChainSync(std::function<void(void)>);
   int broadcastTx(const BinaryDataRef&, std::string&) override;
   CoreRPC::FeeEstimateResult getFeeByte(
      unsigned, const std::string&) const override;

   //locals
   void stallNextZc(unsigned);
};
