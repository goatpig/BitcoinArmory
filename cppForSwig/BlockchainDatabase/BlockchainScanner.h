////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _BLOCKCHAINSCANNER_H
#define _BLOCKCHAINSCANNER_H

#include "Blockchain.h"
#include "lmdb_wrapper.h"
#include "ScrAddrFilter.h"
#include "BlockDataMap.h"
#include "Progress.h"
#include "bdmenums.h"
#include "ThreadSafeClasses.h"

#include "SshParser.h"

#include <future>
#include <atomic>
#include <exception>

#define BATCH_SIZE  1024 * 1024 * 512ULL

class ScanningException : public std::runtime_error
{
private:
   const unsigned badHeight_;

public:
   ScanningException(unsigned badHeight, const std::string &what = "")
      : std::runtime_error(what), badHeight_(badHeight)
   { }
};

struct TxHashHints;
struct TxOutScrRef;

////////////////////////////////////////////////////////////////////////////////
struct ParserBatch
{
public:
   std::map<unsigned, std::shared_ptr<FileUtils::FileMap>> fileMaps_;

   std::atomic<unsigned> blockCounter_;
   std::mutex mergeMutex_;

   const unsigned start_;
   const unsigned end_;

   const unsigned startBlockFileID_;
   const unsigned targetBlockFileID_;

   std::map<unsigned, std::shared_ptr<BlockData>> blockMap_;
   std::map<BinaryData, std::map<unsigned, StoredTxOut>> outputMap_;
   std::map<BinaryData, std::map<BinaryData, StoredSubHistory>> sshMap_;
   std::vector<StoredTxOut> spentOutputs_;

   const std::shared_ptr<std::unordered_map<TxOutScriptRef, int>> scriptRefMap_;
   std::promise<bool> completedPromise_;
   unsigned count_;

public:
   ParserBatch(unsigned start, unsigned end,
      unsigned startID, unsigned endID,
      std::shared_ptr<std::unordered_map<TxOutScriptRef, int>> scriptRefMap) :
      start_(start), end_(end), 
      startBlockFileID_(startID), targetBlockFileID_(endID),
      scriptRefMap_(scriptRefMap)
   {
      if (end < start) {
         throw std::runtime_error("end > start");
      }
      blockCounter_.store(start_, std::memory_order_relaxed);
   }
};

////////////////////////////////////////////////////////////////////////////////
class BlockchainScanner
{
private:
   std::shared_ptr<Blockchain> blockchain_;
   LMDBBlockDatabase* db_;
   ScrAddrFilter* scrAddrFilter_;
   std::shared_ptr<BlockFiles> blockFiles_;

   const unsigned totalThreadCount_;
   const unsigned writeQueueDepth_;
   const unsigned totalBlockFileCount_;

   BinaryData topScannedBlockHash_;

   ProgressCallback progress_ = nullptr;
   bool reportProgress_ = false;

   //only for relevant utxos
   std::map<BinaryData, std::map<unsigned, StoredTxOut>> utxoMap_;

   unsigned startAt_ = 0;

   std::mutex resolverMutex_;

   Armory::Threading::BlockingQueue<std::unique_ptr<ParserBatch>> outputQueue_;
   Armory::Threading::BlockingQueue<std::unique_ptr<ParserBatch>> inputQueue_;
   Armory::Threading::BlockingQueue<std::unique_ptr<ParserBatch>> commitQueue_;

   std::atomic<unsigned> completedBatches_;
   std::atomic_uint32_t fatalError_;

private:
   void writeBlockData(void);
   void processAndCommitTxHints(ParserBatch*);
   void preloadUtxos(void);

   int32_t check_merkle(int32_t startHeight);

   void processFilterHitsThread(
      std::map<uint32_t, std::map<uint32_t,
      std::set<const TxHashHints*>>>& filtersResultMap,
      Armory::Threading::TransactionalSet<BinaryData>& missingHashes,
      std::atomic<int>& counter, std::map<BinaryData, BinaryData>& results,
      std::function<void(size_t)> prog);

   std::shared_ptr<BlockData> getBlockData(
      ParserBatch*, unsigned);

   void processOutputs(void);
   void processOutputsThread(ParserBatch*);

   void processInputs(void);
   void processInputsThread(ParserBatch*);

public:
   BlockchainScanner(std::shared_ptr<Blockchain> bc,
      LMDBBlockDatabase* db, ScrAddrFilter* saf,
      std::shared_ptr<BlockFiles> bf,
      unsigned threadcount, unsigned queue_depth,
      ProgressCallback prg, bool reportProgress);

   bool scan(int32_t startHeight);
   bool scan_nocheck(int32_t startHeight);

   void undo(Blockchain::ReorganizationState& reorgState);
   void updateSSH(bool, int32_t startHeight);
   bool resolveTxHashes();

   const BinaryData& getTopScannedBlockHash(void) const
   {
      return topScannedBlockHash_;
   }
};

#endif