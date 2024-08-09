////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _ZEROCONF_H_
#define _ZEROCONF_H_

#include <vector>
#include <atomic>
#include <functional>
#include <memory>

#include "ThreadSafeClasses.h"
#include "BitcoinP2p.h"
#include "BlockchainDatabase/lmdb_wrapper.h"
#include "BlockchainDatabase/Blockchain.h"
#include "BlockchainDatabase/ScrAddrFilter.h"
#include "ArmoryErrors.h"
#include "ZeroConfUtils.h"
#include "ZeroConfNotifications.h"

#define GETZC_THREADCOUNT 5

#ifdef UNIT_TESTS
   #define MEMPOOL_DEPTH         1
   #define POOL_MERGE_THRESHOLD  10
#else
   #define MEMPOOL_DEPTH         4
   #define POOL_MERGE_THRESHOLD  10000
#endif

#define ZC_BUFFER_LIFETIME_SEC 1
#ifndef UNIT_TESTS
   #define ZC_BUFFER_SIZE_THRESHOLD 30
#else
   //for unit tests, trigger zc buffers as soon as a single zc is in
   #define ZC_BUFFER_SIZE_THRESHOLD 1
#endif 

enum ZcAction
{
   Zc_NewTx,
   Zc_Purge,
   Zc_Shutdown
};

////////////////////////////////////////////////////////////////////////////////
struct ZeroConfData
{
   Tx            txobj_;
   uint32_t      txtime_;

   bool operator==(const ZeroConfData& rhs) const
   {
      return (this->txobj_ == rhs.txobj_) && (this->txtime_ == rhs.txtime_);
   }
};

////////////////////////////////////////////////////////////////////////////////
struct ZeroConfBatchFallbackStruct
{
   BinaryData txHash_;
   std::shared_ptr<BinaryData> rawTxPtr_;
   std::map<std::string, std::string> extraRequestors_;

   ArmoryErrorCodes err_;
};

////////////////////////////////////////////////////////////////////////////////
typedef std::function<void(
   std::vector<ZeroConfBatchFallbackStruct>)> ZcBroadcastCallback;

struct ZcBatchError
{};

////////////////////////////////////////////////////////////////////////////////
struct ZeroConfBatch
{
   //<zcKey ref, ParsedTx>, ParsedTx carries the key object
   std::map<BinaryData, std::shared_ptr<ParsedTx>> zcMap_;
   
   //<txHash ref, zcKey ref>, ParsedTx carries both hash and key objects
   std::map<BinaryDataRef, BinaryDataRef> hashToKeyMap_;

   std::shared_ptr<std::atomic<int>> counter_;
   std::shared_ptr<std::promise<ArmoryErrorCodes>> isReadyPromise_;
   std::shared_future<ArmoryErrorCodes> isReadyFut_;

   unsigned timeout_ = UINT32_MAX;
   std::chrono::system_clock::time_point creationTime_;
   ZcBroadcastCallback errorCallback_;

   const bool hasWatcherEntries_;

   //<request id, bdv id>
   std::pair<std::string, std::string> requestor_;

public:
   ZeroConfBatch(bool hasWatcherEntries) :
      hasWatcherEntries_(hasWatcherEntries)
   {
      counter_ = std::make_shared<std::atomic<int>>();
      isReadyPromise_ = std::make_shared<std::promise<ArmoryErrorCodes>>();
      isReadyFut_ = isReadyPromise_->get_future();
      creationTime_ = std::chrono::system_clock::now();
   }
};

////////////////////////////////////////////////////////////////////////////////
enum ZcPreprocessPacketType
{
   ZcPreprocessPacketType_Inv
};

////
class ZcPreprocessPacket
{
private:
   const ZcPreprocessPacketType type_;

public:
   ZcPreprocessPacket(ZcPreprocessPacketType type) :
      type_(type)
   {}

   virtual ~ZcPreprocessPacket(void) = 0;

   ZcPreprocessPacketType type(void) const { return type_; }
};

////
struct ZcInvPayload : public ZcPreprocessPacket
{
   const bool watcher_;

   ZcInvPayload(bool watcher) :
      ZcPreprocessPacket(ZcPreprocessPacketType_Inv), 
      watcher_(watcher)
   {}

   std::vector<InvEntry> invVec_;
};

////////////////////////////////////////////////////////////////////////////////
enum ZcGetPacketType
{
   ZcGetPacketType_Broadcast,
   ZcGetPacketType_Request,
   ZcGetPacketType_Payload,
   ZcGetPacketType_Reject
};

////
struct ZcGetPacket
{
   const ZcGetPacketType type_;

   ZcGetPacket(ZcGetPacketType type) :
      type_(type)
   {}

   virtual ~ZcGetPacket(void) = 0;
};

////
struct RequestZcPacket : public ZcGetPacket
{
   std::vector<BinaryData> hashes_;
   std::chrono::steady_clock::time_point timestamp_;

   RequestZcPacket(void) : 
      ZcGetPacket(ZcGetPacketType_Request)
   {
      timestamp_ = std::chrono::steady_clock::now();
   }

   bool ready(void) const
   {
      //skip if we have no hashes to request
      if (hashes_.empty())
         return false;

      /*
      Buffer zc from the network node until we have enough 
      to process or enough that has elapsed. This reduces
      the zc snapshot replacement frequency.
      */

      if (hashes_.size() >= ZC_BUFFER_SIZE_THRESHOLD)
      {
         //buffer is ready if we have over ZC_BUFFER_SIZE_THRESHOLD hashes
         return true;
      }

      auto timediff = std::chrono::steady_clock::now() - timestamp_;
      if (timediff >= std::chrono::seconds(ZC_BUFFER_LIFETIME_SEC))
      {
         //or if the buffer is older than ZC_BUFFER_LIFETIME_SEC seconds
         return true;
      }

      return false;
   }
};

////
struct ProcessPayloadTxPacket : public ZcGetPacket
{
   std::shared_ptr<std::atomic<int>> batchCtr_;
   std::shared_ptr<std::promise<ArmoryErrorCodes>> batchProm_;

   const BinaryData txHash_;
   std::shared_ptr<BinaryData> rawTx_;
   std::shared_ptr<ParsedTx> pTx_;

   ProcessPayloadTxPacket(const BinaryData& hash) : 
      ZcGetPacket(ZcGetPacketType_Payload), txHash_(hash)
   {}

   void incrementCounter(void)
   {
      if (batchCtr_ == nullptr)
      {
         LOGERR << "null batch ptr";
         throw std::runtime_error("null batch ptr");
      }

      auto val = batchCtr_->fetch_sub(1, std::memory_order_release);
      if (val == 1)
      try
      {
         batchProm_->set_value(ArmoryErrorCodes::Success);
      }
      catch (const std::future_error&)
      {
         LOGWARN << "batch promise already set";
      }
   }
};

////
struct ZcBroadcastPacket : public ZcGetPacket
{
   std::vector<std::shared_ptr<BinaryData>> zcVec_;
   std::vector<BinaryData> hashes_;

   ZcBroadcastPacket(void) :
      ZcGetPacket(ZcGetPacketType_Broadcast)
   {}
};

////
struct RejectPacket : public ZcGetPacket
{
   const BinaryData txHash_;
   char code_;

   RejectPacket(const BinaryData& hash, char code) :
      ZcGetPacket(ZcGetPacketType_Reject), 
      txHash_(hash), code_(code)
   {}
};

////////////////////////////////////////////////////////////////////////////////
struct ZcUpdateBatch
{
private:
   std::unique_ptr<std::promise<bool>> completed_;

public:
   std::map<BinaryData, std::shared_ptr<ParsedTx>> zcToWrite_;
   std::set<BinaryData> txHashes_;
   std::set<BinaryData> keysToDelete_;
   std::set<BinaryData> txHashesToDelete_;

   std::shared_future<bool> getCompletedFuture(void);
   void setCompleted(bool);
   bool hasData(void) const;
};

////////////////////////////////////////////////////////////////////////////////
struct BatchTxMap
{
   std::map<BinaryData, std::shared_ptr<ParsedTx>> txMap_;
   std::map<BinaryData, std::shared_ptr<WatcherTxBody>> watcherMap_;
   
   //<request id, bdv id>
   std::pair<std::string, std::string> requestor_;
};

////////////////////////////////////////////////////////////////////////////////
struct ZcActionStruct
{
   ZcAction action_;
   std::shared_ptr<ZeroConfBatch> batch_;
   std::unique_ptr<std::promise<std::shared_ptr<ZcPurgePacket>>> 
      resultPromise_ = nullptr;
   Blockchain::ReorganizationState reorgState_;
};

typedef Armory::Threading::BlockingQueue<std::shared_ptr<ZcGetPacket>> PreprocessQueue;

////////////////////////////////////////////////////////////////////////////////
class ZcActionQueue
{
private:
   //ready batches will be passed to this function
   std::function<void(ZcActionStruct)> newZcFunction_;

   //getData responses that have been matched to their batch will be posted 
   //to this queue
   std::shared_ptr<PreprocessQueue> zcPreprocessQueue_;

   //current top ZC id, incremented as new zc is pushed from the node/broadcasts
   std::atomic<uint32_t> topId_;

   std::vector<std::thread> processThreads_;

   //queue of batches served to newZcFunction_
   Armory::Threading::BlockingQueue<ZcActionStruct> newZcQueue_;

   //queue of batches for the matcher thread to populate its local map of 
   //hashes to batches
   Armory::Threading::Queue<std::shared_ptr<ZeroConfBatch>> batchQueue_;

   //queue of getData response from the node
   Armory::Threading::BlockingQueue<
      std::shared_ptr<ZcGetPacket>> getDataResponseQueue_;

   //queue of hashes to clear from macther thread local map
   Armory::Threading::Queue<std::set<BinaryData>> hashesToClear_;

   //tracks the size of the matcher thread local map, for unit test 
   //coverage purposes
   std::atomic<unsigned> matcherMapSize_;

private:
   void processNewZcQueue(void);
   BinaryData getNewZCkey(void);

   //matcher thread
   void getDataToBatchMatcherThread(void);

public:
   ZcActionQueue(
      std::function<void(ZcActionStruct)> func, 
      std::shared_ptr<PreprocessQueue> zcPreprocessQueue,      
      unsigned topId) :
      newZcFunction_(func), zcPreprocessQueue_(zcPreprocessQueue)
   {
      topId_.store(topId, std::memory_order_relaxed);
      matcherMapSize_.store(0, std::memory_order_relaxed);
      start();
   }

   void start(void);
   void shutdown(void);

   std::shared_ptr<ZeroConfBatch> initiateZcBatch(
      const std::vector<BinaryData>&, unsigned,
      const ZcBroadcastCallback&, bool,
      const std::string& = "", const std::string& = "");

   std::shared_future<std::shared_ptr<ZcPurgePacket>> pushNewBlockNotification(
      Blockchain::ReorganizationState);
   
   void queueGetDataResponse(std::shared_ptr<ZcGetPacket>);
   void queueBatch(std::shared_ptr<ZeroConfBatch>);

   unsigned getMatcherMapSize(void) const 
   { 
      return matcherMapSize_.load(std::memory_order_relaxed); 
   }
};

////////////////////////////////////////////////////////////////////////////////
class ZeroConfContainer
{
private:
   std::shared_ptr<MempoolSnapshot> snapshot_;
   
   //<txHash, map<opId, ZcKeys>>
   std::map<BinaryData, 
      std::map<unsigned, BinaryDataRef>> outPointsSpentByKey_;
   std::set<BinaryData> minedTxHashes_;

   //<zcKey, set<ScrAddr>>
   std::map<BinaryDataRef, 
      std::shared_ptr<std::set<BinaryDataRef>>> keyToSpentScrAddr_;
   
   std::set<BinaryData> allZcTxHashes_;
   std::map<BinaryDataRef, std::set<BinaryDataRef>> keyToFundedScrAddr_;

   LMDBBlockDatabase* db_;
   std::shared_ptr<BitcoinNodeInterface> networkNode_;

   std::shared_ptr<PreprocessQueue> zcPreprocessQueue_;
   Armory::Threading::TimedQueue<
      std::shared_ptr<ZcPreprocessPacket>> zcWatcherQueue_;
   Armory::Threading::BlockingQueue<ZcUpdateBatch> updateBatch_;

   std::mutex parserMutex_;
   std::mutex parserThreadMutex_;

   std::vector<std::thread> parserThreads_;
   std::atomic<bool> zcEnabled_;
   const unsigned maxZcThreadCount_;

   std::shared_ptr<Armory::Threading::TransactionalMap<
      BinaryData, std::shared_ptr<AddrAndHash>>> scrAddrMap_;

   unsigned parserThreadCount_ = 0;
   std::unique_ptr<ZeroConfCallbacks> bdvCallbacks_;
   std::unique_ptr<ZcActionQueue> actionQueue_;

   std::map<BinaryData, std::shared_ptr<WatcherTxBody>> watcherMap_;
   ArmoryMutex watcherMapMutex_;

   unsigned mergeCount_ = 0;

private:
   FilteredZeroConfData filterTransaction(
      std::shared_ptr<ParsedTx>,
      std::shared_ptr<MempoolSnapshot>) const;

   void increaseParserThreadPool(unsigned);
   unsigned loadZeroConfMempool(bool);
   void reset(void);

   std::map<BinaryData, std::shared_ptr<ParsedTx>> purge(
      const Blockchain::ReorganizationState&,
      std::shared_ptr<MempoolSnapshot>);
   std::map<BinaryData, std::shared_ptr<ParsedTx>> purgeToBranchpoint(
      const Blockchain::ReorganizationState&, 
      std::shared_ptr<MempoolSnapshot>);

   void processTxGetDataReply(std::unique_ptr<Payload>);
   void handleZcProcessingStructThread(void);
   void requestTxFromNode(RequestZcPacket&);
   void processPayloadTx(std::shared_ptr<ProcessPayloadTxPacket>);


   void pushZcPacketThroughP2P(ZcBroadcastPacket&);
   void pushZcPreprocessVec(std::shared_ptr<RequestZcPacket>);

   std::map<BinaryData, std::shared_ptr<ParsedTx>> dropZC(
      std::shared_ptr<MempoolSnapshot>, const BinaryDataRef&);
   std::map<BinaryData, std::shared_ptr<ParsedTx>> dropZCs(
      std::shared_ptr<MempoolSnapshot>, const std::set<BinaryData>&);

   void parseNewZC(ZcActionStruct);
   void parseNewZC(
      std::map<BinaryData, std::shared_ptr<ParsedTx>> zcMap,
      std::shared_ptr<MempoolSnapshot>,
      bool updateDB, bool notify,
      const std::pair<std::string, std::string>&,
      std::map<BinaryData, std::shared_ptr<WatcherTxBody>>&);
   void finalizePurgePacket(
      ZcActionStruct,
      std::shared_ptr<MempoolSnapshot>) const;
   std::map<BinaryData, std::shared_ptr<ParsedTx>> checkForCollisions(
      const std::map<BinaryDataRef, std::map<unsigned, BinaryDataRef>>&,
      std::shared_ptr<MempoolSnapshot>);

   void updateZCinDB(void);
   void handleInvTx();

   BatchTxMap getBatchTxMap(
      std::shared_ptr<ZeroConfBatch>,
      std::shared_ptr<MempoolSnapshot>);

public:
   ZeroConfContainer(LMDBBlockDatabase* db,
      std::shared_ptr<BitcoinNodeInterface> node, unsigned maxZcThread);

   //action queue
   std::shared_future<std::shared_ptr<ZcPurgePacket>> pushNewBlockNotification(
      Blockchain::ReorganizationState reorgState);
   unsigned getMatcherMapSize(void) const;

   // setup methods
   void init(std::shared_ptr<ScrAddrFilter>, bool clearMempool);
   void shutdown();
   void clear(void);
   bool isEnabled(void) const;

   void setWatcherNode(std::shared_ptr<BitcoinNodeInterface> watcherNode);
   void setZeroConfCallbacks(std::unique_ptr<ZeroConfCallbacks> ptr);
   //

   //broadcast      
   void broadcastZC(const std::vector<BinaryDataRef>& rawzc, 
      uint32_t timeout_ms, const ZcBroadcastCallback&,
      const std::string& bdvID, const std::string& requestID);

   //broadcast helpers
   bool insertWatcherEntry(
      const BinaryData&, std::shared_ptr<BinaryData>, 
      const std::string&, const std::string&,
      std::map<std::string, std::string>&,
      bool watchEntry = true);
   std::shared_ptr<WatcherTxBody> eraseWatcherEntry(const BinaryData&);

   std::shared_ptr<ZeroConfBatch> initiateZcBatch(
      const std::vector<BinaryData>&, unsigned,
      const ZcBroadcastCallback&, bool,
      const std::string&, const std::string&);
   //

   //getters
   bool hasTxByHash(const BinaryData& txHash) const;
   Tx getTxByHash(const BinaryData& txHash) const;
   bool isTxOutSpentByZC(const BinaryData& dbKey) const;

   std::map<BinaryData, std::shared_ptr<const TxIOPair>>
      getUnspentZCforScrAddr(BinaryData scrAddr) const;
   std::map<BinaryData, std::shared_ptr<const TxIOPair>>
      getRBFTxIOsforScrAddr(BinaryData scrAddr) const;

   std::vector<TxOut> getZcTxOutsForKey(const std::set<BinaryData>&) const;
   std::vector<UTXO> getZcUTXOsForKey(const std::set<BinaryData>&) const;

   std::shared_ptr<MempoolSnapshot> getSnapshot(void) const;

   //for unit tests
   unsigned getMergeCount(void) const;
};

#endif