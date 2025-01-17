////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _BLOCKCHAINSCANNER_SUPER_H
#define _BLOCKCHAINSCANNER_SUPER_H

#include "Blockchain.h"
#include "lmdb_wrapper.h"
#include "BlockDataMap.h"
#include "Progress.h"
#include "bdmenums.h"
#include "ThreadSafeClasses.h"

#include "SshParser.h"

#include <future>
#include <atomic>
#include <exception>

#define COMMIT_SSH_SIZE 1024 * 1024 * 256ULL
#define LEFTOVER_THRESHOLD 10000000

#ifndef UNIT_TESTS
#define BATCH_SIZE_SUPER 1024 * 1024 * 128ULL
#else
#define BATCH_SIZE_SUPER 1024
#endif

enum BLOCKDATA_ORDER
{
   BD_ORDER_INCREMENT,
   BD_ORDER_DECREMENT
};

////////////////////////////////////////////////////////////////////////////////
struct ThreadSubSshResult
{
   std::map<BinaryData, std::map<BinaryData, StoredSubHistory>> subSshMap_;
   unsigned spent_offset_ = 0;
};

////////////////////////////////////////////////////////////////////////////////
struct BlockDataBatch
{
   const BLOCKDATA_ORDER order_;
   std::atomic<int> blockCounter_;
   const int start_;
   const int end_;

   std::map<unsigned, std::shared_ptr<FileUtils::FileMap>> fileMaps_;
   std::map<unsigned, std::shared_ptr<BlockData>> blockMap_;

   std::set<unsigned> blockDataFileIDs_;
   std::shared_ptr<BlockFiles> blockFiles_;
   std::shared_ptr<Blockchain> blockchain_;

   BlockDataBatch(int start, int end, std::set<unsigned>& ids,
      BLOCKDATA_ORDER order,
      std::shared_ptr<BlockFiles> bfl, std::shared_ptr<Blockchain> bcPtr) :
      order_(order),
      start_(start), end_(end), blockDataFileIDs_(std::move(ids)),
      blockFiles_(bfl), blockchain_(bcPtr)
   {}

   void populateFileMap(void);
   std::shared_ptr<BlockData> getBlockData(unsigned);
   void resetCounter(void);
   std::shared_ptr<BlockData> getNext(void);
};

////////////////////////////////////////////////////////////////////////////////
struct ParserBatch_Ssh
{
public:
   std::unique_ptr<BlockDataBatch> bdb_;

   std::atomic<unsigned> sshKeyCounter_;
   std::mutex mergeMutex_;

   std::map<BinaryData, BinaryData> hashToDbKey_;

   std::map<BinaryDataRef, std::pair<BinaryWriter, BinaryWriter>> serializedSubSsh_;
   std::vector<BinaryDataRef> keyRefs_;
   unsigned batch_id_;

   std::vector<ThreadSubSshResult> txOutSshResults_;
   std::vector<ThreadSubSshResult> txInSshResults_;

   std::promise<bool> completedPromise_;
   unsigned count_;
   unsigned spent_offset_;

   std::chrono::system_clock::time_point parseTxOutStart_;
   std::chrono::system_clock::time_point parseTxOutEnd_;

   std::chrono::system_clock::time_point parseTxInStart_;
   std::chrono::system_clock::time_point parseTxInEnd_;
   std::chrono::duration<double> serializeSsh_;

   std::chrono::system_clock::time_point writeSshStart_;
   std::chrono::system_clock::time_point writeSshEnd_;

   std::chrono::system_clock::time_point processStart_;
   std::chrono::system_clock::time_point insertToCommitQueue_;

public:
   ParserBatch_Ssh(std::unique_ptr<BlockDataBatch> blockDataBatch) :
      bdb_(std::move(blockDataBatch))
   {}

   void resetCounter(void) { bdb_->resetCounter(); }
};

////////////////////////////////////////////////////////////////////////////////
struct ParserBatch_Spentness
{
   std::unique_ptr<BlockDataBatch> bdb_;

   std::map<BinaryData, BinaryData> keysToCommit_;
   std::map<BinaryData, BinaryData> keysToCommitLater_;
   std::mutex mergeMutex_;

   std::promise<bool> prom_;

   ParserBatch_Spentness(std::unique_ptr<BlockDataBatch> blockDataBatch) :
      bdb_(std::move(blockDataBatch))
   {}
};

////////////////////////////////////////////////////////////////////////////////
struct StxoRef
{
   BinaryDataRef theData_;
   BinaryDataRef scriptRef_;
   BinaryDataRef hashRef_;

   unsigned height_;
   uint8_t dup_;
   uint16_t txIndex_, txOutIndex_;

   void unserializeDBValue(const BinaryDataRef&);
   void reset(void) { scriptRef_.reset(); }
   bool isInitialized(void) const { return scriptRef_.isValid(); }

   BinaryData getScrAddressCopy(void) const;
   BinaryData getDBKey(void) const;

   uint64_t getValue(void) const;
};

////////////////////////////////////////////////////////////////////////////////
class BlockchainScanner_Super
{
private:
   int startAt_ = 0;
   bool withUpdateSshHints_ = false;
   bool init_;
   unsigned batch_counter_ = 0;

   std::shared_ptr<Blockchain> blockchain_;
   LMDBBlockDatabase* db_;
   std::shared_ptr<BlockFiles> blockFiles_;

   Armory::Threading::BlockingQueue<
      std::unique_ptr<ParserBatch_Ssh>> commitQueue_;
   Armory::Threading::BlockingQueue<
      std::pair<BinaryData, BinaryData>> sshBoundsQueue_;
   Armory::Threading::BlockingQueue<
      std::unique_ptr<std::map<BinaryData, BinaryWriter>>> serializedSshQueue_;
   Armory::Threading::BlockingQueue<
      std::unique_ptr<ParserBatch_Spentness>> spentnessQueue_;

   std::set<BinaryData> updateSshHints_;

   const unsigned totalThreadCount_;
   const unsigned writeQueueDepth_;
   const unsigned totalBlockFileCount_;
   std::map<unsigned, HeightAndDup> heightAndDupMap_;

   BinaryData topScannedBlockHash_;

   ProgressCallback progress_ = nullptr;
   bool reportProgress_ = false;
   std::atomic<unsigned> completedBatches_;
   std::atomic<uint64_t> addrPrefixCounter_;
   std::map<unsigned, unsigned> heightToId_;

private:
   void commitSshBatch(void);
   void writeSubSsh(ParserBatch_Ssh*);

   void processOutputs(ParserBatch_Ssh*);
   void processOutputsThread(ParserBatch_Ssh*, unsigned);

   void processInputs(ParserBatch_Ssh*);
   void processInputsThread(ParserBatch_Ssh*, unsigned);

   void serializeSubSsh(std::unique_ptr<ParserBatch_Ssh>);
   void serializeSubSshThread(ParserBatch_Ssh*);

   void writeSpentness(void);

   bool getTxKeyForHash(const BinaryDataRef&, BinaryData&);
   StxoRef getStxoByHash(
      const BinaryDataRef&, uint16_t,
      ParserBatch_Ssh*);
   
   void parseSpentness(ParserBatch_Spentness*);
   void parseSpentnessThread(ParserBatch_Spentness*);

public:
   BlockchainScanner_Super(
      std::shared_ptr<Blockchain>, LMDBBlockDatabase*,
      std::shared_ptr<BlockFiles>, bool init,
      unsigned threadcount, unsigned,
      ProgressCallback prg, bool reportProgress);

   void scan(void);
   void scanSpentness(void);
   void updateSSH(bool);
   void undo(Blockchain::ReorganizationState&);

   const BinaryData& getTopScannedBlockHash(void) const
   {
      return topScannedBlockHash_;
   }
};

#endif
