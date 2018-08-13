////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _SSH_PARSER_H
#define _SSH_PARSER_H

#include "lmdb_wrapper.h"
#include "Blockchain.h"
#include "ScrAddrFilter.h"

////////////////////////////////////////////////////////////////////////////////
struct SshBatch
{
   unique_ptr<promise<bool>> waitOnWriter_ = nullptr;
   const unsigned shardId_;
   map<BinaryData, BinaryWriter> serializedSsh_;

   SshBatch(unsigned shardId) :
      shardId_(shardId)
   {}
};

////////////////////////////////////////////////////////////////////////////////
class ShardedSshParser
{
private:
   LMDBBlockDatabase* db_;
   atomic<unsigned> counter_;
   const unsigned scanFrom_;
   const unsigned scanTo_;
   const unsigned threadCount_;
   bool init_;

   mutex mu_;

   BlockingQueue<unique_ptr<SshBatch>> checkpointQueue_;
   BlockingQueue<unique_ptr<SshBatch>> serializedSshQueue_;
   BlockingQueue<pair<BinaryData, BinaryData>>  sshBoundsQueue_;

   map<unsigned, atomic<unsigned>> shardSemaphores_;

private:
   void compileCheckpoints(void);
   void tallySshThread(void);
   void parseShardsThread(const vector<pair<BinaryData, BinaryData>>&);
   void putSSH(void);
   void putCheckpoint(unsigned boundsCount);
   
   void undoCheckpoint(unsigned shardId);
   void resetCheckpoint(unsigned shardId);

private:
   vector<pair<BinaryData, BinaryData>> getBounds(
      bool withPrefix, uint8_t prefix);

public:
   ShardedSshParser(
      LMDBBlockDatabase* db,
      unsigned scanFrom, unsigned scanTo,
      unsigned threadCount, bool init)
      : db_(db),
      scanFrom_(scanFrom), scanTo_(scanTo),
      threadCount_(threadCount), init_(init)
   {
      counter_.store(0, memory_order_relaxed);
   }

   void updateSsh(void);
   void undoShards(const set<unsigned>&);
};

typedef pair<set<BinaryData>, map<BinaryData, StoredScriptHistory>> subSshParserResult;
subSshParserResult parseSubSsh(
   unique_ptr<LDBIter>, int32_t scanFrom, bool,
   function<uint8_t(unsigned)>,
   shared_ptr<map<BinaryDataRef, shared_ptr<AddrAndHash>>>,
   BinaryData upperBound);

#endif