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

#ifndef _BLOCKUTILS_H_
#define _BLOCKUTILS_H_

#include <iostream>
#include <fstream>
#include <vector>
#include <set>
#include <future>
#include <exception>

#include "Blockchain.h"
#include "StoredBlockObj.h"
#include "lmdb_wrapper.h"
#include "BinaryData.h"
#include "BtcUtils.h"
#include "BlockObj.h"
#include "ArmoryConfig.h"
#include "ScrAddrObj.h"
#include "bdmenums.h"

#include "UniversalTimer.h"

#include <functional>
#include "ScrAddrFilter.h"
#include "nodeRPC.h"
#include "BitcoinP2p.h"
#include "BDV_Notification.h"

#define NUM_BLKS_BATCH_THRESH 30
#define NUM_BLKS_IS_DIRTY 2016

class BlockDataManager;
class LSM;

typedef enum
{
  DB_BUILD_HEADERS,
  DB_BUILD_ADD_RAW,
  DB_BUILD_APPLY,
  DB_BUILD_SCAN
} DB_BUILD_PHASE;

typedef enum 
{
   BDM_offline,
   BDM_initializing,
   BDM_ready
}BDM_state;

enum ResetDBMode
{
   Reset_Rescan,
   Reset_Rebuild,
   Reset_SSH
};

class ProgressReporter;

typedef std::pair<size_t, uint64_t> BlockFilePosition;
class FoundAllBlocksException {};

class debug_replay_blocks {};

class BlockFiles;
class DatabaseBuilder;
class BDV_Server_Object;

///////////////////////////////////////////////////////////////////////////////
struct ProgressData
{
   BDMPhase phase_;
   double progress_;
   unsigned time_;
   unsigned numericProgress_;
   std::vector<std::string> wltIDs_;

   ProgressData(void)
   {}

   ProgressData(BDMPhase phase, double prog,
      unsigned time, unsigned numProg, std::vector<std::string> wltIDs) :
      phase_(phase), progress_(prog), time_(time),
      numericProgress_(numProg), wltIDs_(wltIDs)
   {}
};

////////////////////////////////////////////////////////////////////////////////
class BlockDataManager
{
private:
   std::function<bool(void)> shutdownLbd_;

   LMDBBlockDatabase* iface_ = nullptr;
   class BDM_ScrAddrFilter;
   std::shared_ptr<BDM_ScrAddrFilter> scrAddrData_;
   std::shared_ptr<Blockchain> blockchain_;
   std::shared_ptr<BlockFiles> blockFiles_;
   std::shared_ptr<DatabaseBuilder> dbBuilder_;

   BDM_state BDMstate_ = BDM_offline;
   std::exception_ptr exceptPtr_ = nullptr;

   unsigned checkTransactionCount_ = 0;
   mutable std::shared_ptr<std::mutex> nodeStatusPollMutex_;
   Armory::Threading::Queue<std::shared_ptr<BDVNotificationHook>> oneTimeHooks_;

public:
   typedef std::function<void(BDMPhase, double,unsigned, unsigned)> ProgressCallback;
   std::shared_ptr<BitcoinNodeInterface> processNode_, watchNode_;
   std::shared_future<bool> isReadyFuture_;
   mutable std::shared_ptr<CoreRPC::NodeRPCInterface> nodeRPC_;

   Armory::Threading::TimedQueue<std::unique_ptr<BDV_Notification>> notificationStack_;
   std::shared_ptr<ZeroConfContainer> zeroConfCont_;

public:
   BlockDataManager(std::function<bool(void)>);
   ~BlockDataManager(void);

   std::shared_ptr<Blockchain> blockchain(void) const { return blockchain_; }
   LMDBBlockDatabase *getIFace(void) const { return iface_; }
   std::shared_ptr<BlockFiles> blockFiles(void) const { return blockFiles_; }

   void openDatabase(void);
   bool doInitialSyncOnLoad(const ProgressCallback &progress);
   bool doInitialSyncOnLoad_Rescan(const ProgressCallback &progress);
   bool doInitialSyncOnLoad_Rebuild(const ProgressCallback &progress);
   bool doInitialSyncOnLoad_RescanBalance(
      const ProgressCallback &progress);

   // for testing only
   struct BlkFileUpdateCallbacks
   {
      std::function<void()> headersRead, headersUpdated, blockDataLoaded;
   };
   
   bool hasException(void) const { return exceptPtr_ != nullptr; }
   std::exception_ptr getException(void) const { return exceptPtr_; }

private:
   bool loadDiskState(const ProgressCallback &progress, bool forceRescanSSH = false);
   void pollNodeStatus() const;

public:
   Blockchain::ReorganizationState readBlkFileUpdate(void);
   bool applyBlockRangeToDB(ProgressCallback,
      uint32_t blk0, ScrAddrFilter& scrAddrData);

   uint32_t getTopBlockHeight() const {return blockchain_->top()->getBlockHeight();}
   uint8_t getValidDupIDForHeight(uint32_t blockHgt) const
   { return iface_->getValidDupIDForHeight(blockHgt); }

   std::shared_ptr<ScrAddrFilter> getScrAddrFilter(void) const;
   StoredHeader getMainBlockFromDB(uint32_t hgt) const;
   StoredHeader getBlockFromDB(uint32_t hgt, uint8_t dup) const;

   void enableZeroConf(bool cleanMempool = false);
   void disableZeroConf(void);
   bool isZcEnabled(void) const;
   std::shared_ptr<ZeroConfContainer> zeroConfCont(void) const
   {
      return zeroConfCont_;
   }

   void triggerShutdown(void);
   void shutdown(void);
   void cleanup(void);
   bool isRunning(void) const { return BDMstate_ != BDM_offline; }
   void blockUntilReady(void) const;
   bool isReady(void) const;
   void resetDatabases(ResetDBMode mode);

   unsigned getCheckedTxCount(void) const { return checkTransactionCount_; }
   CoreRPC::NodeStatus getNodeStatus(void) const;
   void registerZcCallbacks(std::unique_ptr<ZeroConfCallbacks> ptr)
   {
      zeroConfCont_->setZeroConfCallbacks(std::move(ptr));
   }

   void registerOneTimeHook(std::shared_ptr<BDVNotificationHook>);
   void triggerOneTimeHooks(BDV_Notification*);
};

///////////////////////////////////////////////////////////////////////////////
class BlockDataManagerThread
{
   struct BlockDataManagerThreadImpl
   {
      std::shared_ptr<BlockDataManager> bdm;
      int mode = 0;
      volatile bool run = false;
      bool failure = false;
      std::thread tID;
   };
   std::unique_ptr<BlockDataManagerThreadImpl> pimpl;

public:
   BlockDataManagerThread(void);
   ~BlockDataManagerThread(void);

   // start the BDM thread
   void start(BDM_INIT_MODE mode);
   std::shared_ptr<BlockDataManager> bdm(void);

   // return true if the caller should wait on callback notification
   bool shutdown();
   void join();

private:
   static void* thrun(void *);
   void run();

private:
   BlockDataManagerThread(const BlockDataManagerThread&);
};

#endif
