////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include <atomic>
#include <functional>
#include <memory>

#include <Utils/ArmoryErrors.h>
#include <Utils/ThreadSafeClasses.h>
#include <Utils/ReentrantLock.h>
#include <BlockchainDatabase/Blockchain.h>

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

using BdvIdKey = uint64_t;
class AddrAndHash;
class ScrAddrFilter;
struct ReorganizationState;
class Tx;
class TxIOPair;
class TxOut;
struct UTXO;

namespace Armory
{
   namespace Node
   {
      class BitcoinNodeInterface;
      class Payload;
      struct InvEntry;
   }

   namespace ZeroConf
   {
      class ParsedTx;
      struct FilteredZeroConfData;
      class MempoolSnapshot;

      struct WatcherTxBody;
      struct ZcPurgePacket;
      class ZeroConfCallbacks;

      ////////
      enum class ZcAction : int
      {
         NewTx,
         Purge,
         Shutdown
      };

      enum class ZcGetPacketType : int
      {
         Broadcast,
         Request,
         Payload,
         Reject
      };

      enum class ZcPreprocessPacketType : int
      {
         Inv
      };

      ////////
      struct ZeroConfBatchFallbackStruct
      {
         BinaryData txHash;
         std::shared_ptr<BinaryData> rawTxPtr;
         std::set<BdvIdKey> extraRequestors;
         ArmoryErrorCodes err;
      };

      using ZcBroadcastCallback = std::function<void(
         std::vector<ZeroConfBatchFallbackStruct>)>;

      struct ZcBatchError
      {};

      ////////
      struct ZeroConfBatch
      {
         //<zcKey ref, ParsedTx>, ParsedTx carries the key object
         std::map<BinaryData, std::shared_ptr<ParsedTx>> zcMap;

         //<txHash ref, zcKey ref>, ParsedTx carries both hash and key objects
         std::map<BinaryDataRef, BinaryDataRef> hashToKeyMap;

         std::shared_ptr<std::atomic<int>> counter;
         std::shared_ptr<std::promise<ArmoryErrorCodes>> isReadyPromise;
         std::shared_future<ArmoryErrorCodes> isReadyFut;

         unsigned timeout_ = UINT32_MAX;
         std::chrono::system_clock::time_point creationTime;
         ZcBroadcastCallback errorCallback;

         const bool hasWatcherEntries;

         //bdv id
         BdvIdKey requestor;

      public:
         ZeroConfBatch(bool);
      };

      ////////
      class ZcPreprocessPacket
      {
      private:
         const ZcPreprocessPacketType type_;

      public:
         ZcPreprocessPacket(ZcPreprocessPacketType);
         virtual ~ZcPreprocessPacket(void) = 0;

         ZcPreprocessPacketType type(void) const;
      };

      struct ZcInvPayload : public ZcPreprocessPacket
      {
         const bool watcher;
         std::vector<Node::InvEntry> invVec;

         ZcInvPayload(bool);
      };

      ////////
      struct ZcGetPacket
      {
         const ZcGetPacketType type;

         ZcGetPacket(ZcGetPacketType);
         virtual ~ZcGetPacket(void) = 0;
      };

      struct RequestZcPacket : public ZcGetPacket
      {
         std::vector<BinaryData> hashes;
         std::chrono::steady_clock::time_point timestamp;

         RequestZcPacket(void);
         bool ready(void) const;
      };

      struct ProcessPayloadTxPacket : public ZcGetPacket
      {
         std::shared_ptr<std::atomic<int>> batchCtr;
         std::shared_ptr<std::promise<ArmoryErrorCodes>> batchProm;

         const BinaryData txHash;
         std::shared_ptr<BinaryData> rawTx;
         std::shared_ptr<ParsedTx> pTx;

         ProcessPayloadTxPacket(const BinaryData&);
         void incrementCounter(void);
      };

      struct ZcBroadcastPacket : public ZcGetPacket
      {
         std::vector<std::shared_ptr<BinaryData>> zcVec;
         std::vector<BinaryData> hashes;

         ZcBroadcastPacket(void);
      };

      struct RejectPacket : public ZcGetPacket
      {
         const BinaryData txHash;
         char code;

         RejectPacket(const BinaryData&, char);
      };

      using ParsedTxMap =
         std::map<BinaryData, std::shared_ptr<ParsedTx>>;
      using PreprocessQueue =
         Threading::BlockingQueue<std::shared_ptr<ZcGetPacket>>;

      class ZcUpdateBatch
      {
      private:
         std::unique_ptr<std::promise<bool>> completed_;

      public:
         ParsedTxMap zcToWrite;
         std::set<BinaryData> txHashes;
         std::set<BinaryData> keysToDelete;
         std::set<BinaryData> txHashesToDelete;

         std::shared_future<bool> getCompletedFuture(void);
         void setCompleted(bool);
         bool hasData(void) const;
      };

      struct BatchTxMap
      {
         ParsedTxMap txMap_;
         std::map<BinaryData, std::shared_ptr<WatcherTxBody>> watcherMap_;

         //bdv id
         BdvIdKey requestor_;
      };

      struct ZcActionStruct
      {
         ZcAction action;
         std::shared_ptr<ZeroConfBatch> batch;
         std::unique_ptr<std::promise<std::shared_ptr<ZcPurgePacket>>> resultPromise;
         ReorganizationState reorgState;
      };

      ////////
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
         Threading::BlockingQueue<ZcActionStruct> newZcQueue_;

         //queue of batches for the matcher thread to populate its local map of
         //hashes to batches
         Threading::Queue<std::shared_ptr<ZeroConfBatch>> batchQueue_;

         //queue of getData response from the node
         Threading::BlockingQueue<
            std::shared_ptr<ZcGetPacket>> getDataResponseQueue_;

         //queue of hashes to clear from macther thread local map
         Threading::Queue<std::set<BinaryData>> hashesToClear_;

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
            const std::function<void(ZcActionStruct)>&,
            std::shared_ptr<PreprocessQueue>,
            unsigned);

         void start(void);
         void shutdown(void);

         std::shared_ptr<ZeroConfBatch> initiateZcBatch(
            const std::vector<BinaryData>&, unsigned,
            const ZcBroadcastCallback&, bool, uint64_t);

         std::shared_future<std::shared_ptr<ZcPurgePacket>>
         pushNewBlockNotification(ReorganizationState&);

         void queueGetDataResponse(std::shared_ptr<ZcGetPacket>);
         void queueBatch(std::shared_ptr<ZeroConfBatch>);

         unsigned getMatcherMapSize(void) const;
      };

      ////////
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
         std::shared_ptr<Node::BitcoinNodeInterface> networkNode_;

         std::shared_ptr<PreprocessQueue> zcPreprocessQueue_;
         Threading::TimedQueue<
            std::shared_ptr<ZcPreprocessPacket>> zcWatcherQueue_;
         Threading::BlockingQueue<ZcUpdateBatch> updateBatch_;

         std::mutex parserMutex_;
         std::mutex parserThreadMutex_;

         std::vector<std::thread> parserThreads_;
         std::atomic<bool> zcEnabled_;
         const unsigned maxZcThreadCount_;

         std::shared_ptr<Threading::TransactionalMap<
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
            const ReorganizationState&,
            std::shared_ptr<MempoolSnapshot>);
         std::map<BinaryData, std::shared_ptr<ParsedTx>> purgeToBranchpoint(
            const ReorganizationState&,
            std::shared_ptr<MempoolSnapshot>);

         void processTxGetDataReply(std::unique_ptr<Node::Payload>);
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
            std::map<BinaryData, std::shared_ptr<ParsedTx>>,
            std::shared_ptr<MempoolSnapshot>,
            bool, bool, BdvIdKey,
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
         ZeroConfContainer(LMDBBlockDatabase*,
            std::shared_ptr<Node::BitcoinNodeInterface>, unsigned);

         //action queue
         std::shared_future<std::shared_ptr<ZcPurgePacket>>
         pushNewBlockNotification(ReorganizationState);
         unsigned getMatcherMapSize(void) const;

         // setup methods
         void init(std::shared_ptr<ScrAddrFilter>, bool);
         void shutdown(void);
         void clear(void);
         bool isEnabled(void) const;

         void setWatcherNode(std::shared_ptr<Node::BitcoinNodeInterface>);
         void setZeroConfCallbacks(std::unique_ptr<ZeroConfCallbacks>);

         //broadcast
         void broadcastZC(const std::vector<BinaryDataRef>& rawzc,
            uint32_t timeout_ms, const ZcBroadcastCallback&,
            BdvIdKey);

         //broadcast helpers
         bool insertWatcherEntry(
            const BinaryData&, std::shared_ptr<BinaryData>,
            BdvIdKey, std::set<BdvIdKey>, bool=true);
         std::shared_ptr<WatcherTxBody> eraseWatcherEntry(const BinaryData&);

         std::shared_ptr<ZeroConfBatch> initiateZcBatch(
            const std::vector<BinaryData>&, unsigned,
            const ZcBroadcastCallback&, bool, uint64_t);

         //getters
         bool hasTxByHash(const BinaryData&) const;
         Tx getTxByHash(const BinaryData&) const;
         bool isTxOutSpentByZC(const BinaryData&) const;

         std::map<BinaryData, std::shared_ptr<const TxIOPair>>
            getUnspentZCforScrAddr(BinaryData) const;
         std::map<BinaryData, std::shared_ptr<const TxIOPair>>
            getRBFTxIOsforScrAddr(BinaryData) const;

         std::vector<TxOut> getZcTxOutsForKey(const std::set<BinaryData>&) const;
         std::vector<UTXO> getZcUTXOsForKey(const std::set<BinaryData>&) const;
         std::shared_ptr<const MempoolSnapshot> getSnapshot(void) const;

         //for unit tests
         unsigned getMergeCount(void) const;
      };
   } //namespace ZeroConf
} //namespace Armory
