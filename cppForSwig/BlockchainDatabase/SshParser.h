////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _SSH_PARSER_H
#define _SSH_PARSER_H

#include <atomic>
#include <condition_variable>

#include "lmdb_wrapper.h"
#include "Blockchain.h"
#include "ScrAddrFilter.h"

#ifndef UNIT_TESTS
#define SSH_BOUNDS_BATCH_SIZE 100000
#else
#define SSH_BOUNDS_BATCH_SIZE 2
#endif

////////////////////////////////////////////////////////////////////////////////
struct SshBatch
{
   std::unique_ptr<std::promise<bool>> waitOnWriter_ = nullptr;
   const unsigned shardId_;
   std::map<BinaryData, BinaryWriter> serializedSsh_;

   SshBatch(unsigned shardId) :
      shardId_(shardId)
   {}
};

////////////////////////////////////////////////////////////////////////////////
struct SshBounds
{
   std::pair<BinaryData, BinaryData> bounds_;
   std::map<BinaryData, BinaryWriter> serializedSsh_;
   std::chrono::duration<double> time_;
   uint64_t count_ = 0;

   std::unique_ptr<std::promise<bool>> completed_;
   std::shared_future<bool> fut_;

   SshBounds(void)
   {
      completed_ = std::make_unique<std::promise<bool>>();
      fut_ = completed_->get_future();
   }

   void serializeResult(std::map<BinaryDataRef, StoredScriptHistory>&);
};

struct SshMapping
{
   std::map<uint8_t, std::shared_ptr<SshMapping>> map_;
   uint64_t count_ = 0;

   std::shared_ptr<SshMapping> getMappingForKey(uint8_t);
   void prettyPrint(std::stringstream&, unsigned);
   void merge(SshMapping&);
};


////////////////////////////////////////////////////////////////////////////////
class ShardedSshParser
{
private:
   LMDBBlockDatabase* db_;
   std::atomic<unsigned> counter_;
   const unsigned firstHeight_;
   unsigned firstShard_;
   const unsigned threadCount_;
   bool init_;
   bool undo_ = false;

   std::vector<std::unique_ptr<SshBounds>> boundsVector_;

   std::atomic<unsigned> commitedBoundsCounter_;
   std::atomic<unsigned> fetchBoundsCounter_;
   std::condition_variable writeThreadCV_;
   std::mutex cvMutex_;

   std::atomic<unsigned> mapCount_;
   std::vector<SshMapping> mappingResults_;

private:
   void putSSH(void);
   SshBounds* getNext();
   
private:
   void setupBounds();
   SshMapping mapSubSshDB();
   void mapSubSshDBThread(unsigned);
   void parseSshThread(void);

public:
   ShardedSshParser(
      LMDBBlockDatabase* db,
      unsigned firstHeight, 
      unsigned threadCount, bool init)
      : db_(db),
      firstHeight_(firstHeight),
      threadCount_(threadCount), init_(init)
   {
      counter_.store(0, std::memory_order_relaxed);
   }

   void updateSsh(void);
   void undo(void);
};

typedef std::pair<std::set<BinaryData>, std::map<BinaryData, StoredScriptHistory>> subSshParserResult;
subSshParserResult parseSubSsh(
   std::unique_ptr<LDBIter>, int32_t scanFrom, bool,
   std::function<uint8_t(unsigned)>,
   std::shared_ptr<const std::map<BinaryData, std::shared_ptr<AddrAndHash>>>,
   BinaryData upperBound);

#endif