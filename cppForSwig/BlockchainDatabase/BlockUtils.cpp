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

#include <algorithm>
#include <time.h>
#include <stdio.h>
#include "BlockUtils.h"
#include "lmdbpp.h"
#include "Progress.h"
#include "util.h"
#include "BlockchainScanner.h"
#include "DatabaseBuilder.h"
#include "gtest/MockedNode.h"

using namespace Armory::Config;
using namespace std::chrono_literals;

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
      if (to == lastSample_) {
         return;
      }

      const time_t now = time(0);
      if (now == then_) {
         return;
      }
      if (now < then_+10) {
         return;
      }

      double speed = (to-lastSample_)/double(now-then_);
      if (lastSample_ == 0) {
         avgSpeed_ = speed;
      }
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
private:
   BlockDataManager *const bdm_;

public:
   BDM_ScrAddrFilter(BlockDataManager *bdm, unsigned sdbiID=0)
      : ScrAddrFilter(bdm->getIFace(), sdbiID), bdm_(bdm)
   {}

protected:
   virtual bool bdmIsRunning() const
   {
      return bdm_->BDMstate_ != BDM_offline;
   }

   virtual bool applyBlockRangeToDB(
      uint32_t startBlock, const std::vector<std::string>& wltIDs,
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

      const auto progress = [&](
         BDMPhase phase, double prog, unsigned time, unsigned numericProgress)
      {
         if (!reportProgress) {
            return;
         }
         auto notifPtr = std::make_unique<BDV_Notification_Progress>(
            phase, prog, time, numericProgress, wltIDs);
         bdm_->notificationStack_.push_back(std::move(notifPtr));
      };
      auto result = bdm_->applyBlockRangeToDB(progress, startBlock, *this);
      if (result == false) {
         LOGERR << "ArmoryDB encountered a fatal error while scanning the chain";
         LOGERR << "It will now terminate. Restart it to auto-repair";

         auto notifPtr = std::make_unique<BDV_Notification_Error>(
            std::string{}, BDM_FATAL_ERROR_CODE, BinaryData{},
            std::string{"fatal error while scanning"}
         );
         bdm_->notificationStack_.push_back(std::move(notifPtr));
      }
      return result;
   }

   std::shared_ptr<Blockchain> blockchain(void) const
   {
      return bdm_->blockchain();
   }

   std::shared_ptr<ScrAddrFilter> getNew(unsigned sdbiID)
   {
      return std::make_shared<BDM_ScrAddrFilter>(bdm_, sdbiID);
   }
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
// Start BlockDataManager methods
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
BlockDataManager::BlockDataManager(std::function<bool(void)> shutdownLbd) :
   shutdownLbd_(shutdownLbd)
{
   blockchain_ = std::make_shared<Blockchain>(
      BitcoinSettings::getGenesisBlockHash());
   blockFiles_ = std::make_shared<BlockFiles>(Pathing::blkFilePath());
   iface_ = new LMDBBlockDatabase(blockchain_, Pathing::blkFilePath());
   nodeStatusPollMutex_ = std::make_shared<std::mutex>();

   try {
      openDatabase();

      processNode_ = NetworkSettings::bitcoinNodes().first;
      watchNode_ = NetworkSettings::bitcoinNodes().second;
      nodeRPC_ = NetworkSettings::rpcNode();
      if (processNode_ == nullptr) {
         throw DbErrorMsg("invalid node type in bdmConfig");
      }

      zeroConfCont_ = std::make_shared<ZeroConfContainer>(
         iface_, processNode_, DBSettings::zcThreadCount());
      zeroConfCont_->setWatcherNode(watchNode_);

      scrAddrData_ = std::make_shared<BDM_ScrAddrFilter>(this);
      scrAddrData_->init();
   } catch (...) {
      exceptPtr_ = std::current_exception();
   }
}

/////////////////////////////////////////////////////////////////////////////
void BlockDataManager::openDatabase()
{
   LOGINFO << "blkfile dir: " << Pathing::blkFilePath().string();
   LOGINFO << "lmdb dir: " << Pathing::dbDir().string();
   if (!BitcoinSettings::isInitialized()) {
      LOGERR << "ERROR: Genesis Block Hash not set!";
      throw std::runtime_error("ERROR: Genesis Block Hash not set!");
   }

   try {
      iface_->openDatabases(Pathing::dbDir());
   } catch (const std::runtime_error &e) {
      std::stringstream ss;
      ss << "DB failed to open, reporting the following error: " << e.what();
      throw std::runtime_error(ss.str());
   }
}

/////////////////////////////////////////////////////////////////////////////
BlockDataManager::~BlockDataManager()
{
   cleanup();
}

/////////////////////////////////////////////////////////////////////////////
void BlockDataManager::cleanup()
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
   iface_ = nullptr;
}

/////////////////////////////////////////////////////////////////////////////
void BlockDataManager::shutdown()
{
   disableZeroConf();
   notificationStack_.terminate();

   if (processNode_) {
      processNode_->shutdown();
   }
   if (watchNode_) {
      watchNode_->shutdown();
   }
   if (scrAddrData_) {
      scrAddrData_->shutdown();
   }
}

/////////////////////////////////////////////////////////////////////////////
void BlockDataManager::triggerShutdown()
{
   if (shutdownLbd_ != nullptr) {
      shutdownLbd_();
   }
}

/////////////////////////////////////////////////////////////////////////////
bool BlockDataManager::applyBlockRangeToDB(
   ProgressCallback prog, uint32_t blk0, ScrAddrFilter& scrAddrData)
{
   // Start scanning and timer
   BlockchainScanner bcs(blockchain_, iface_, &scrAddrData,
      blockFiles_,
      DBSettings::threadCount(), DBSettings::ramUsage(),
      prog, DBSettings::reportProgress());
   if (!bcs.scan_nocheck(blk0)) {
      return false;
   }

   bcs.updateSSH(false, blk0);
   bcs.resolveTxHashes();
   return true;
}

/////////////////////////////////////////////////////////////////////////////
void BlockDataManager::resetDatabases(ResetDBMode mode)
{
   if (mode == Reset_SSH) {
      iface_->resetSSHdb();
      return;
   }

   if (DBSettings::getDbType() != ARMORY_DB_SUPER) {
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

   if (DBSettings::getDbType() != ARMORY_DB_SUPER) {
      //reapply ssh map to the db
      scrAddrData_->resetSshDB();
   }
}

/////////////////////////////////////////////////////////////////////////////
bool BlockDataManager::doInitialSyncOnLoad(
   const ProgressCallback &progress)
{
   LOGINFO << "Executing: doInitialSyncOnLoad";
   return loadDiskState(progress);
}

/////////////////////////////////////////////////////////////////////////////
bool BlockDataManager::doInitialSyncOnLoad_Rescan(
   const ProgressCallback &progress)
{
   LOGINFO << "Executing: doInitialSyncOnLoad_Rescan";
   resetDatabases(Reset_Rescan);
   return loadDiskState(progress);
}

/////////////////////////////////////////////////////////////////////////////
bool BlockDataManager::doInitialSyncOnLoad_Rebuild(
   const ProgressCallback &progress)
{
   LOGINFO << "Executing: doInitialSyncOnLoad_Rebuild";
   resetDatabases(Reset_Rebuild);
   return loadDiskState(progress);
}

/////////////////////////////////////////////////////////////////////////////
bool BlockDataManager::doInitialSyncOnLoad_RescanBalance(
   const ProgressCallback &progress)
{
   LOGINFO << "Executing: doInitialSyncOnLoad_RescanBalance";
   resetDatabases(Reset_SSH);
   return loadDiskState(progress, true);
}

////////////////////////////////////////////////////////////////////////////////
bool BlockDataManager::loadDiskState(const ProgressCallback &progress,
   bool forceRescanSSH)
{
   BDMstate_ = BDM_initializing;
   dbBuilder_ = std::make_shared<DatabaseBuilder>(
      blockFiles_, *this, progress, forceRescanSSH);
   if (!dbBuilder_->init()) {
      //fatal error in db startup, terminate bdm
      return false;
   }

   if (DBSettings::checkChain()) {
      checkTransactionCount_ = dbBuilder_->getCheckedTxCount();
   }

   BDMstate_ = BDM_ready;
   LOGINFO << "BDM is ready";
   return true;
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
   if (!iface_->getStoredHeader(returnSBH, hgt, dup)) {
      return {};
   }
   return returnSBH;
}

////////////////////////////////////////////////////////////////////////////////
StoredHeader BlockDataManager::getMainBlockFromDB(uint32_t hgt) const
{
   uint8_t dupMain = iface_->getValidDupIDForHeight(hgt);
   return getBlockFromDB(hgt, dupMain);
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<ScrAddrFilter> BlockDataManager::getScrAddrFilter() const
{
   return scrAddrData_;
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataManager::enableZeroConf(bool clearMempool)
{
   if (zeroConfCont_ == nullptr) {
      throw std::runtime_error("null zc object");
   }
   zeroConfCont_->init(scrAddrData_, clearMempool);
}

////////////////////////////////////////////////////////////////////////////////
bool BlockDataManager::isZcEnabled(void) const
{
   if (zeroConfCont_ == nullptr) {
      return false;
   }
   return zeroConfCont_->isEnabled();
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataManager::disableZeroConf(void)
{
   if (zeroConfCont_ == nullptr) {
      return;
   }
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
   if (!nodeRPC_->canPoll()) {
      return;
   }
   std::unique_lock<std::mutex> lock(*nodeStatusPollMutex_, std::defer_lock);

   if (!lock.try_lock()) {
      return;
   }

   auto poll_thread = [this](void)->void
   {
      auto nodeRPC = this->nodeRPC_;
      auto mutexPtr = this->nodeStatusPollMutex_;
      std::unique_lock<std::mutex> lock(*mutexPtr);

      unsigned count = 0;
      while (nodeRPC->testConnection() != CoreRPC::RpcState_Online) {
         ++count;
         if (count > 10) {
            break; //give up after 20sec
         }
         std::this_thread::sleep_for(2s);
      }
   };

   std::thread pollThr(poll_thread);
   if (pollThr.joinable()) {
      pollThr.detach();
   }
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataManager::blockUntilReady() const
{
   while (true) {
      try {
         isReadyFuture_.wait();
         return;
      } catch (const std::future_error&) {
         std::this_thread::sleep_for(1s);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
bool BlockDataManager::isReady() const
{
   bool isready = false;

   while (true) {
      try {
         isready = isReadyFuture_.wait_for(0s) == std::future_status::ready;
         break;
      } catch (const std::future_error&) {
         std::this_thread::sleep_for(1s);
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
   try {
      while (true) {
         auto hookPtr = oneTimeHooks_.pop_front();
         if (hookPtr == nullptr) {
            continue;
         }
         hookPtr->lambda_(notifPtr);
      }
   } catch (const Armory::Threading::IsEmpty&) {}
}
