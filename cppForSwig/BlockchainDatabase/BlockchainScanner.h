////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/***
 - DB_BARE/DB_FULL chain scanner -

 Tracks the following, for a known set of scrAddr:
   . in DB_SELECT::TXOUTS, txouts as <key, txout>
      * key is 64 bits concatenation of
         [32 bits scrAddrId | 32 bits block uniqueID (BE)]
      * txout is tuple {
         uint64_t amount,
         uint16_t txId,
         uint16_t txOutId
      }
      * scrAddrId is a 32bit unique key assigned at first scrAddr
        registration, managed by ScrAddrFilter, tracked in DB_SELECT::SCRADDR

   . in DB_SELECT::TXINS, txins as <txoutkey, txinkey>
      * txoutkey is 64 bits concatenation of
         [32 bits block unique ID (BE) | 16 bits txId (BE) | 16 bits txOutId (BE)]
      * txinkey is 64 bits concatenation of
         [32 bits block unique ID (BE) | 16 bits txId (BE) | 16 bits outpointId (BE)]

   . in DB_SELECT::KNOWNHASHES, <hash32, txkey> for all tx carrying at least one
      txout in DB_SELECT::TXOUTS
      * hash32 is a 32 bytes txhash
      * txkey is 64 bits concatenation of
         [32 bits block unique ID (BE) | 16 bits txId (BE) | 0xFFFF]

   . in DB_BARE only, <key, uint64_t txkeys[]> for all tx hashes with at least
      one relevant output + all outpoint hash for these txs
      * key is 64 bits concatenation of
         [first 32 bits of Hash32 | 32 bits block unique ID (BE)]
      * an array of txkeys, where each txkey is a 64 bits concatention of
         [32 bits block unique ID (BE) | 16 bits txId (BE) | 0xFFFF]

   ####
   A scan requires a ScannerContext.
   Before its first use, a ScannerContext needs initialized via init(db).
   That call will seed the HashMap, which will be maintained by subsequent scans.
   After each scan, the ScannerContext object is left ready for the next scan.
   On each scan, update is called with the blockchain and scrAddrFilter objects.
***/

#pragma once

#include <future>
#include <atomic>
#include <exception>
#include <unordered_map>

#include <Utils/Types.h>
#include <Utils/ThreadSafeClasses.h>
#include <Utils/BinaryData.h>
#include "Progress.h"
#include "bdmenums.h"
#include "BlockObj.h"

#define BATCH_SIZE 1024 * 1024 * 128ULL

struct TxHashHints;
struct TxOutScrRef;
class BlockFiles;
class LMDBBlockDatabase;
class ScrAddrFilter;
class BlockData;
class StoredSubHistory;
struct TxOutData;

namespace Armory
{
   namespace FileUtils
   {
      class FileMap;
      class FileCopy;
   }

   class Blockchain;
   struct ReorganizationState;
}

////////////////////////////////////////////////////////////////////////////////
using HashMap = std::unordered_map<
   Armory::Hash32, std::set<Armory::Types::TxKey>,
   Armory::Hash32::Hasher, Armory::Hash32::IsEqual
>;

using ScrAddrIdMap = std::unordered_map<
   Armory::Types::ScrAddr, uint32_t,
   BinaryData::Hasher, BinaryData::IsEqual
>;

class ScannerContext
{
private:
   //tx hash to tx key map
   HashMap hashMap_;

   //copied from blockchain object
   std::vector<Armory::HeaderPtr> headersById_;

   //scrAddr and their respective unique int id
   ScrAddrIdMap scrAddrIds_;

   mutable std::mutex mu_;

public:
   void init(LMDBBlockDatabase*);
   void update(std::shared_ptr<Armory::Blockchain>);
   void update(ScrAddrFilter*);
   HashMap update(HashMap&);
   void merge(ScannerContext&);

   const HashMap& getHashMap(void) const;
   const ScrAddrIdMap& getScrAddrIdMap(void) const;
   bool isBlockIDValid(Armory::Types::BlockId) const;
};

////////////////////////////////////////////////////////////////////////////////
struct TxOutParsingResult
{
   HashMap hashMap;
   std::map<
      Armory::Types::ScrAddrId,
      std::map<Armory::Types::BlockId, std::deque<TxOutData>>>
   txOutMap;

   std::map<Armory::Types::BlockId, std::shared_ptr<BlockData>> blockMap;
};

struct TxInParsingResult
{
   HashMap diff;
   std::unordered_map<Armory::Types::TxIOKey, Armory::Types::TxIOKey> txInMap;
};

////////
struct ParserBatch
{
public:
   const unsigned start;
   const unsigned end;
   const Armory::Types::FileId startBlockFileID;
   const Armory::Types::FileId targetBlockFileID;
   const unsigned threadCount;
   ScannerContext& context;

   std::map<Armory::Types::FileId, std::shared_ptr<Armory::FileUtils::FileCopy>> fileCopies;
   std::atomic<unsigned> blockCounter;
   std::mutex mergeMutex;

   TxOutParsingResult outParserResult;
   TxInParsingResult inParserResult;

   std::promise<bool> completedPromise;
   std::promise<bool> preloadPromise;
   unsigned count;

public:
   ParserBatch(unsigned, unsigned,
      Armory::Types::FileId, Armory::Types::FileId,
      unsigned, ScannerContext&);
   std::future<bool> preloadFiles(
      std::shared_ptr<BlockFiles>,
      std::map<Armory::Types::FileId, std::shared_ptr<Armory::FileUtils::FileCopy>>&);
   void mergeResult(TxOutParsingResult&);
   void mergeResult(TxInParsingResult&);
};

////////////////////////////////////////////////////////////////////////////////
class BlockchainScanner
{
private:
   std::shared_ptr<Armory::Blockchain> blockchain_;
   LMDBBlockDatabase* db_;
   ScrAddrFilter* scrAddrFilter_;
   std::shared_ptr<BlockFiles> blockFiles_;

   const unsigned totalThreadCount_;
   const unsigned memTarget_;
   const unsigned totalBlockFileCount_;

   Armory::Hash32 topScannedBlockHash_;

   ProgressCallback progress_ = nullptr;
   bool reportProgress_ = false;

   unsigned startAt_ = 0;

   std::mutex resolverMutex_;

   Armory::Threading::BlockingQueue<std::unique_ptr<ParserBatch>> outputQueue_;
   Armory::Threading::BlockingQueue<std::unique_ptr<ParserBatch>> inputQueue_;
   Armory::Threading::BlockingQueue<std::unique_ptr<ParserBatch>> commitQueue_;

   std::atomic<uint32_t> completedBatches_;
   std::atomic<uint32_t> fatalError_;

private:
   void commitBatches(void);
   //void processAndCommitTxHints(ParserBatch*);

   int32_t check_merkle(int32_t);

   void processFilterHitsThread(
      std::map<uint32_t, std::map<uint32_t,
      std::set<const TxHashHints*>>>&,
      const std::set<BinaryData>&,
      std::atomic<int>&, std::map<BinaryData, BinaryData>&,
      std::function<void(size_t)>);

   std::shared_ptr<BlockData> getBlockData(
      ParserBatch*, unsigned);

   void processOutputs(void);
   void processOutputsThread(ParserBatch*);

   void processInputs(void);
   void processInputsThread(ParserBatch*);

public:
   BlockchainScanner(std::shared_ptr<Armory::Blockchain>,
      LMDBBlockDatabase*, ScrAddrFilter*,
      std::shared_ptr<BlockFiles>,
      unsigned, unsigned,
      ProgressCallback, bool);

   bool scan(ScannerContext&, int32_t);
   bool resolveTxHashes();

   const Armory::Hash32& getTopScannedBlockHash(void) const;
};
