////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_NODEUNITTEST
#define _H_NODEUNITTEST

#include <memory>
#include <vector>
#include <map>

#include "../BinaryData.h"
#include "../BtcUtils.h"

#include "../BlockchainDatabase/BlockUtils.h"
#include "../BitcoinP2p.h"
#include "../BlockchainDatabase/Blockchain.h"
#include "../Signer/ScriptRecipient.h"
#include "../BlockchainDatabase/BlockDataMap.h"
#include "../nodeRPC.h"

////////////////////////////////////////////////////////////////////////////////
struct UnitTestBlock
{
   BinaryData rawHeader_;
   BinaryData headerHash_;

   Tx coinbase_;
   std::vector<Tx> transactions_;

   unsigned height_;
   unsigned timestamp_;
   BinaryData diffBits_;
};

////////////////////////////////////////////////////////////////////////////////
struct MempoolObject
{
   BinaryData rawTx_;
   BinaryData hash_;
   unsigned order_;
   unsigned blocksUntilMined_ = 0;
   bool staged_;

   bool operator<(const MempoolObject& rhs) const 
   { return order_ < rhs.order_; }
};

////////////////////////////////////////////////////////////////////////////////
class NodeUnitTest : public BitcoinNodeInterface
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
   
   std::shared_ptr<Blockchain> blockchain_ = nullptr;
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
   NodeUnitTest(uint32_t magic_word, bool watcher);
   ~NodeUnitTest(void);

   //locals
   void updateNodeStatus(bool connected);
   void notifyNewBlock(void);
   void watcherProcess(void);

   std::map<unsigned, BinaryData> mineNewBlock(
      std::shared_ptr<BlockDataManager> bdm,
      unsigned count, const BinaryData& h160, double diff = 1.0);
   std::map<unsigned, BinaryData> mineNewBlock(
      std::shared_ptr<BlockDataManager> bdm,
      unsigned, Armory::Signing::ScriptRecipient*, double diff = 1.0);

   std::vector<UnitTestBlock> getMinedBlocks(void) const { return blocks_; }
   void setReorgBranchPoint(std::shared_ptr<BlockHeader>);
   void skipZc(unsigned);
   void delayNextZc(unsigned);
   void stallNextZc(unsigned);

   void checkSigs(bool check) { checkSigs_ = check; }

   //<raw tx, blocks to wait until mining>
   void pushZC(const std::vector<std::pair<BinaryData, unsigned>>&, bool);
   void evictZC(const BinaryData&);
   uint64_t getFeeForTx(const Tx&) const;

   //set
   void setBlockchain(std::shared_ptr<Blockchain>);
   void setBlockFiles(std::shared_ptr<BlockFiles>);
   void setIface(LMDBBlockDatabase* iface) { iface_ = iface; }

   //virtuals
   void sendMessage(std::unique_ptr<Payload>) override;

   void connectToNode(bool) override;
   bool connected(void) const override { return true; }
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
      std::shared_ptr<NodeUnitTest> primaryNode,
      std::shared_ptr<NodeUnitTest> watcherNode) :
      CoreRPC::NodeRPCInterface(), 
      primaryNode_(primaryNode), watcherNode_(watcherNode)
   {}

   //virtuals
   void shutdown(void) override {}
   CoreRPC::RpcState testConnection(void) override 
   { return CoreRPC::RpcState_Online; }
   
   bool canPoll(void) const override { return false; }

   void waitOnChainSync(std::function<void(void)>) {}
   int broadcastTx(const BinaryDataRef&, std::string&) override;

   CoreRPC::FeeEstimateResult getFeeByte(
      unsigned, const std::string&) override
   { return CoreRPC::FeeEstimateResult(); }

   //locals
   void stallNextZc(unsigned);
};

#endif