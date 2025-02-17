////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2024, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <algorithm>
#include <time.h>
#include <stdio.h>
#include "BlockUtils.h"
#include "lmdbpp.h"
#include "Progress.h"
#include "util.h"
#include "BlockchainScanner.h"
#include "DatabaseBuilder.h"
#include "gtest/NodeUnitTest.h"

using namespace std;
using namespace Armory::Config;

////////////////////////////////////////////////////////////////////////////////
class ProgressMeasurer
{
   const uint64_t total_;
   
   time_t then_;
   uint64_t lastSample_=0;
   
   double avgSpeed_=0.0;
   
   
public:
   ProgressMeasurer(uint64_t total)
      : total_(total)
   {
      then_ = time(0);
   }
   
   void advance(uint64_t to)
   {
      static const double smoothingFactor=.75;
      
      if (to == lastSample_) return;
      const time_t now = time(0);
      if (now == then_) return;
      
      if (now < then_+10) return;
      
      double speed = (to-lastSample_)/double(now-then_);
      
      if (lastSample_ == 0)
         avgSpeed_ = speed;
      lastSample_ = to;

      avgSpeed_ = smoothingFactor*speed + (1-smoothingFactor)*avgSpeed_;
      
      then_ = now;
   }

   double fractionCompleted() const { return lastSample_/double(total_); }
   
   double unitsPerSecond() const { return avgSpeed_; }
   
   time_t remainingSeconds() const
   {
      return (total_-lastSample_)/unitsPerSecond();
   }
};

class BlockDataManager::BDM_ScrAddrFilter : public ScrAddrFilter
{
   BlockDataManager *const bdm_;
   
public:
   BDM_ScrAddrFilter(BlockDataManager *bdm, unsigned sdbiID = 0)
      : ScrAddrFilter(bdm->getIFace(), sdbiID), bdm_(bdm)
   {}

protected:
   virtual bool bdmIsRunning() const
   {
      return bdm_->BDMstate_ != BDM_offline;
   }

   virtual BinaryData applyBlockRangeToDB(
      uint32_t startBlock, const vector<string>& wltIDs,
      bool reportProgress)
   {
      //make sure sdbis are initialized (fresh ids wont have sdbi entries)
      try {
         getSshSDBI();
      } catch (const std::runtime_error&) {
         StoredDBInfo sdbi;
         sdbi.magic_ = BitcoinSettings::getMagicBytes();
         sdbi.metaHash_ = BtcUtils::EmptyHash_;
         sdbi.topBlkHgt_ = 0;
         sdbi.armoryType_ = DBSettings::getDbType();

         //write sdbi
         putSshSDBI(sdbi);
      }

      try {
         getSubSshSDBI();
      } catch (const std::runtime_error&) {
         StoredDBInfo sdbi;
         sdbi.magic_ = BitcoinSettings::getMagicBytes();
         sdbi.metaHash_ = BtcUtils::EmptyHash_;
         sdbi.topBlkHgt_ = 0;
         sdbi.armoryType_ = DBSettings::getDbType();

         //write sdbi
         putSubSshSDBI(sdbi);
      }

      const auto progress
         = [&](BDMPhase phase, double prog, unsigned time, unsigned numericProgress)
      {
         if (!reportProgress) {
            return;
         }

         auto notifPtr = std::make_unique<BDV_Notification_Progress>(
            phase, prog, time, numericProgress, wltIDs);
         bdm_->notificationStack_.push_back(move(notifPtr));
      };
      return bdm_->applyBlockRangeToDB(progress, startBlock, *this);
   }

   shared_ptr<Blockchain> blockchain(void) const
   {
      return bdm_->blockchain();
   }

   shared_ptr<ScrAddrFilter> getNew(unsigned sdbiID)
   {
      return make_shared<BDM_ScrAddrFilter>(bdm_, sdbiID);
   }
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
// Start BlockDataManager methods
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
BlockDataManager::BlockDataManager()
{
   blockchain_ = make_shared<Blockchain>(BitcoinSettings::getGenesisBlockHash());

   blockFiles_ = make_shared<BlockFiles>(Pathing::blkFilePath());
   iface_ = new LMDBBlockDatabase(blockchain_, Pathing::blkFilePath());
   nodeStatusPollMutex_ = make_shared<mutex>();

   try {
      openDatabase();

      processNode_ = NetworkSettings::bitcoinNodes().first;
      watchNode_ = NetworkSettings::bitcoinNodes().second;
      nodeRPC_ = NetworkSettings::rpcNode();

      if(processNode_ == nullptr) {
         throw DbErrorMsg("invalid node type in bdmConfig");
      }

      zeroConfCont_ = make_shared<ZeroConfContainer>(
         iface_, processNode_, DBSettings::zcThreadCount());
      zeroConfCont_->setWatcherNode(watchNode_);

      scrAddrData_ = make_shared<BDM_ScrAddrFilter>(this);
      scrAddrData_->init();
   } catch (...) {
      exceptPtr_ = current_exception();
   }
}

/////////////////////////////////////////////////////////////////////////////
void BlockDataManager::openDatabase()
{
   LOGINFO << "blkfile dir: " << Pathing::blkFilePath().string();
   LOGINFO << "lmdb dir: " << Pathing::dbDir().string();
   if (!BitcoinSettings::isInitialized()) {
      LOGERR << "ERROR: Genesis Block Hash not set!";
      throw runtime_error("ERROR: Genesis Block Hash not set!");
   }

   try {
      iface_->openDatabases(Pathing::dbDir());
   } catch (const runtime_error &e) {
      stringstream ss;
      ss << "DB failed to open, reporting the following error: " << e.what();
      throw runtime_error(ss.str());
   }
}

/////////////////////////////////////////////////////////////////////////////
BlockDataManager::~BlockDataManager()
{
   zeroConfCont_.reset();
   blockFiles_.reset();
   dbBuilder_.reset();
   processNode_.reset();
   watchNode_.reset();
   scrAddrData_.reset();

   if (iface_ != nullptr) {
      iface_->closeDatabases();
   }
   delete iface_;
}

/////////////////////////////////////////////////////////////////////////////
BinaryData BlockDataManager::applyBlockRangeToDB(
   ProgressCallback prog,
   uint32_t blk0,
   ScrAddrFilter& scrAddrData)
{
   // Start scanning and timer
   BlockchainScanner bcs(blockchain_, iface_, &scrAddrData,
      blockFiles_,
      DBSettings::threadCount(), DBSettings::ramUsage(),
      prog, DBSettings::reportProgress());
   bcs.scan_nocheck(blk0);
   bcs.updateSSH(false, blk0);
   bcs.resolveTxHashes();

   return bcs.getTopScannedBlockHash();
}

/////////////////////////////////////////////////////////////////////////////
void BlockDataManager::resetDatabases(ResetDBMode mode)
{
   if (mode == Reset_SSH)
   {
      iface_->resetSSHdb();
      return;
   }

   if (DBSettings::getDbType() != ARMORY_DB_SUPER)
   {
      //we keep all scrAddr data in between db reset/clear
      scrAddrData_->getAllScrAddrInDB();
   }
   
   switch (mode)
   {
   case Reset_Rescan:
      iface_->resetHistoryDatabases();
      break;

   case Reset_Rebuild:
      iface_->destroyAndResetDatabases();
      blockchain_->clear();
      break;
   
   default:
      break;
   }

   if (DBSettings::getDbType() != ARMORY_DB_SUPER)
   {
      //reapply ssh map to the db
      scrAddrData_->resetSshDB();
   }
}

/////////////////////////////////////////////////////////////////////////////
void BlockDataManager::doInitialSyncOnLoad(
   const ProgressCallback &progress
)
{
   LOGINFO << "Executing: doInitialSyncOnLoad";
   loadDiskState(progress);
}

/////////////////////////////////////////////////////////////////////////////
void BlockDataManager::doInitialSyncOnLoad_Rescan(
   const ProgressCallback &progress
)
{
   LOGINFO << "Executing: doInitialSyncOnLoad_Rescan";
   resetDatabases(Reset_Rescan);
   loadDiskState(progress);
}

/////////////////////////////////////////////////////////////////////////////
void BlockDataManager::doInitialSyncOnLoad_Rebuild(
   const ProgressCallback &progress
)
{
   LOGINFO << "Executing: doInitialSyncOnLoad_Rebuild";
   resetDatabases(Reset_Rebuild);
   loadDiskState(progress);
}

/////////////////////////////////////////////////////////////////////////////
void BlockDataManager::doInitialSyncOnLoad_RescanBalance(
   const ProgressCallback &progress
   )
{
   LOGINFO << "Executing: doInitialSyncOnLoad_RescanBalance";
   resetDatabases(Reset_SSH);
   loadDiskState(progress, true);
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataManager::loadDiskState(const ProgressCallback &progress,
   bool forceRescanSSH)
{
   BDMstate_ = BDM_initializing;
   dbBuilder_ = make_shared<DatabaseBuilder>(
      blockFiles_, *this, progress, forceRescanSSH);
   dbBuilder_->init();

   if (DBSettings::checkChain())
      checkTransactionCount_ = dbBuilder_->getCheckedTxCount();

   BDMstate_ = BDM_ready;
   LOGINFO << "BDM is ready";
}

////////////////////////////////////////////////////////////////////////////////
Blockchain::ReorganizationState BlockDataManager::readBlkFileUpdate()
{ 
   return dbBuilder_->update();
}

////////////////////////////////////////////////////////////////////////////////
StoredHeader BlockDataManager::getBlockFromDB(uint32_t hgt, uint8_t dup) const
{

   // Get the full block from the DB
   StoredHeader returnSBH;
   if(!iface_->getStoredHeader(returnSBH, hgt, dup))
      return {};

   return returnSBH;

}

////////////////////////////////////////////////////////////////////////////////
StoredHeader BlockDataManager::getMainBlockFromDB(uint32_t hgt) const
{
   uint8_t dupMain = iface_->getValidDupIDForHeight(hgt);
   return getBlockFromDB(hgt, dupMain);
}
   
////////////////////////////////////////////////////////////////////////////////
shared_ptr<ScrAddrFilter> BlockDataManager::getScrAddrFilter(void) const
{
   return scrAddrData_;
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataManager::enableZeroConf(bool clearMempool)
{
   if (zeroConfCont_ == nullptr)
      throw runtime_error("null zc object");

   zeroConfCont_->init(scrAddrData_, clearMempool);
}

////////////////////////////////////////////////////////////////////////////////
bool BlockDataManager::isZcEnabled(void) const
{
   if (zeroConfCont_ == nullptr)
      return false;

   return zeroConfCont_->isEnabled();
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataManager::disableZeroConf(void)
{
   if (zeroConfCont_ == nullptr)
      return;

   zeroConfCont_->shutdown();
}

////////////////////////////////////////////////////////////////////////////////
CoreRPC::NodeStatus BlockDataManager::getNodeStatus() const
{
   if (processNode_ == nullptr) {
      return {};
   }

   CoreRPC::NodeStatus nss;
   if (processNode_->connected()) {
      nss.state_ = CoreRPC::NodeState_Online;
   }

   if (processNode_->isSegWit()) {
      nss.SegWitEnabled_ = true;
   }

   if (nodeRPC_ == nullptr) {
      return nss;
   }

   nss.rpcState_ = nodeRPC_->testConnection();
   if (nss.rpcState_ != CoreRPC::RpcState_Online) {
      pollNodeStatus();
   }
   nss.chainStatus_ = nodeRPC_->getChainStatus();
   return nss;
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataManager::pollNodeStatus() const
{
   if (!nodeRPC_->canPoll())
      return;

   unique_lock<mutex> lock(*nodeStatusPollMutex_, defer_lock);

   if (!lock.try_lock())
      return;

   auto poll_thread = [this](void)->void
   {
      auto nodeRPC = this->nodeRPC_;
      auto mutexPtr = this->nodeStatusPollMutex_;

      unique_lock<mutex> lock(*mutexPtr);

      unsigned count = 0;
      while (nodeRPC->testConnection() != CoreRPC::RpcState_Online)
      {
         ++count;
         if (count > 10)
            break; //give up after 20sec

         this_thread::sleep_for(chrono::seconds(2));
      }
   };

   thread pollThr(poll_thread);
   if (pollThr.joinable())
      pollThr.detach();
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataManager::blockUntilReady() const
{
   while (1)
   {
      try
      {
         isReadyFuture_.wait();
         return;
      }
      catch (future_error&)
      {
         this_thread::sleep_for(chrono::seconds(1));
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
bool BlockDataManager::isReady() const
{
   bool isready = false;

   while (1)
   {
      try
      {
         isready = isReadyFuture_.wait_for(chrono::seconds(0)) ==
            std::future_status::ready;
         break;
      }
      catch (future_error&)
      {
         this_thread::sleep_for(chrono::seconds(1));
      }
   }

   return isready;
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataManager::registerOneTimeHook(
   std::shared_ptr<BDVNotificationHook> hook)
{
   oneTimeHooks_.push_back(move(hook));
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataManager::triggerOneTimeHooks(BDV_Notification* notifPtr)
{
   try
   {
      while (true)
      {
         auto&& hookPtr = oneTimeHooks_.pop_front();
         if (hookPtr == nullptr)
            continue;

         hookPtr->lambda_(notifPtr);
      }
   }
   catch(Armory::Threading::IsEmpty&)
   {}
}
