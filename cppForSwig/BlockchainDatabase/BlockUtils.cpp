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
#include <Utils/BtcUtils.h>
#include <Utils/ArmoryConfig.h>
#include <Utils/BitcoinSettings.h>
#include <ZeroConf/Parser.h>
#include <ZeroConf/Notifications.h>
#include <ZeroConf/Utils.h>
#include <gtest/MockedNode.h>

#include "Progress.h"
#include "lmdb_wrapper.h"
#include "BlockchainScanner.h"
#include "DatabaseBuilder.h"
#include "BDV_Notification.h"
#include "txio.h"
#include "StoredBlockObj.h"
#include "BlockchainData.h"

using namespace Armory;
using namespace std::chrono_literals;

class BlockDataManager::BDM_ScrAddrFilter : public ScrAddrFilter
{
private:
   BlockDataManager *const bdm_;

public:
   BDM_ScrAddrFilter(BlockDataManager *bdm, unsigned sdbiID=0)
      : ScrAddrFilter(bdm->getIFace(), sdbiID), bdm_(bdm)
   {}

protected:
   bool bdmIsRunning() const override
   {
      return bdm_->isRunning();
   }

   Hash32 applyBlockRangeToDB(
      uint32_t startBlock, const std::vector<std::string>& wltIDs,
      bool reportProgress) override
   {
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
      if (!result.valid()) {
         LOGERR << "ArmoryDB encountered a fatal error while scanning the chain";
         LOGERR << "It will now terminate. Restart it to auto-repair";

         auto notifPtr = std::make_unique<BDV_Notification_Error>(
            BDV_NOTIF_BROADCAST, BDM_FATAL_ERROR_CODE, BinaryData{},
            std::string{"fatal error while scanning"}
         );
         bdm_->notificationStack_.push_back(std::move(notifPtr));
      }
      return result;
   }

   std::shared_ptr<Blockchain> blockchain() const override
   {
      return bdm_->blockchain();
   }

   std::shared_ptr<ScrAddrFilter> getNew(unsigned sdbiID) override
   {
      return std::make_shared<BDM_ScrAddrFilter>(bdm_, sdbiID);
   }
};

////////////////////////////////////////////////////////////////////////////////
// BlockDataManager
BlockDataManager::BlockDataManager(std::function<bool(void)> shutdownLbd) :
   shutdownLbd_(shutdownLbd)
{
   blockchain_ = std::make_shared<Blockchain>(
      Config::BitcoinSettings::getGenesisBlockHash());
   blockchainData_ = std::make_shared<BlockchainData>(blockchain_);
   blockFiles_ = std::make_shared<BlockFiles>(Config::Pathing::blkFilePath());
   iface_ = new LMDBBlockDatabase(Config::Pathing::dbDir());
   nodeStatusPollMutex_ = std::make_shared<std::mutex>();

   try {
      openDatabase();

      processNode_ = Config::NetworkSettings::bitcoinNodes().first;
      watchNode_ = Config::NetworkSettings::bitcoinNodes().second;
      nodeRPC_ = Config::NetworkSettings::rpcNode();
      if (processNode_ == nullptr) {
         throw DbErrorMsg("invalid node type in bdmConfig");
      }

      zeroConfCont_ = std::make_shared<ZeroConf::ZeroConfContainer>(
         iface_, blockchain_, blockchainData_, processNode_,
         Config::DBSettings::zcThreadCount());
      zeroConfCont_->setWatcherNode(watchNode_);

      scrAddrData_ = std::make_shared<BDM_ScrAddrFilter>(this);
   } catch (const std::exception& e) {
      std::cout << "db open error: " << e.what() << std::endl;
   } catch (...) {
      exceptPtr_ = std::current_exception();
   }
}

BlockDataManager::~BlockDataManager()
{
   cleanup();
}

////////
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

void BlockDataManager::triggerShutdown()
{
   if (shutdownLbd_ != nullptr) {
      shutdownLbd_();
   }
}

////////
void BlockDataManager::openDatabase()
{
   LOGINFO << "blkfile dir: " << Config::Pathing::blkFilePath().string();
   LOGINFO << "lmdb dir: " << Config::Pathing::dbDir().string();
   if (!Config::BitcoinSettings::isInitialized()) {
      LOGERR << "ERROR: Genesis Block Hash not set!";
      throw std::runtime_error("ERROR: Genesis Block Hash not set!");
   }

   try {
      iface_->openDatabases();
   } catch (const std::runtime_error &e) {
      std::stringstream ss;
      ss << "DB failed to open, reporting the following error: " << e.what();
      throw std::runtime_error(ss.str());
   }
}

void BlockDataManager::resetDatabases(BdmInitMode mode)
{
   if (mode == BdmInitMode::RESUME) {
      return;
   }

   switch (mode)
   {
      case BdmInitMode::RESCAN:
      {
         iface_->resetHistoryDatabases();
         scrAddrData_->updateScannedHash(Hash32{});
         break;
      }

      case BdmInitMode::REBUILD:
      {
         iface_->destroyAndResetDatabases();
         scrAddrData_->resetSDBI();
         break;
      }

      default:
         break;
   }
}

////////
bool BlockDataManager::doInitialSyncOnLoad(BdmInitMode mode,
   const ProgressCallback &progress)
{
   LOGINFO << "Executing: doInitialSyncOnLoad";
   resetDatabases(mode);
   return loadDiskState(progress);
}

bool BlockDataManager::loadDiskState(const ProgressCallback &progress)
{
   std::promise<bool> readyProm;
   scrAddrData_->start(readyProm.get_future());

   BDMstate_ = BDMState::Initializing;
   dbBuilder_ = std::make_shared<Database::Builder>(*this, progress);
   if (!dbBuilder_->init()) {
      //fatal error in db startup, terminate bdm
      readyProm.set_value(false);
      return false;
   }

   if (Config::DBSettings::checkChain()) {
      checkTransactionCount_ = dbBuilder_->getCheckedTxCount();
   }

   BDMstate_ = BDMState::Ready;
   readyProm.set_value(true);
   LOGINFO << "BDM is ready";
   return true;
}

ReorganizationState BlockDataManager::readBlkFileUpdate()
{
   return dbBuilder_->update();
}

Hash32 BlockDataManager::applyBlockRangeToDB(
   ProgressCallback prog, uint32_t blk0, ScrAddrFilter& scrAddrData)
{
   // Start scanning and timer
   BlockchainScanner bcs(blockchain_, iface_, &scrAddrData,
      blockFiles_,
      Config::DBSettings::threadCount(), Config::DBSettings::ramUsage(),
      prog, Config::DBSettings::reportProgress());

   //no need to setup a context for a side scan, it assumes
   //address history is fresh
   ScannerContext ctx;
   if (!bcs.scan(ctx, blk0)) {
      return Hash32{};
   }

   //need to merge hashmap with main context now
   dbBuilder_->mergeContext(ctx);
   return bcs.getTopScannedBlockHash();
}

std::pair<Hash32, Hash32> BlockDataManager::getLastScannedRange() const
{
   return dbBuilder_->lastScanRange;
}

////////
void BlockDataManager::registerZcCallbacks(
   std::unique_ptr<ZeroConf::ZeroConfCallbacks> ptr)
{
   zeroConfCont_->setZeroConfCallbacks(std::move(ptr));
}

void BlockDataManager::enableZeroConf(bool clearMempool)
{
   if (zeroConfCont_ == nullptr) {
      throw std::runtime_error("null zc object");
   }
   zeroConfCont_->init(scrAddrData_, clearMempool);
}

bool BlockDataManager::isZcEnabled() const
{
   if (zeroConfCont_ == nullptr) {
      return false;
   }
   return zeroConfCont_->isEnabled();
}

void BlockDataManager::disableZeroConf()
{
   if (zeroConfCont_ == nullptr) {
      return;
   }
   zeroConfCont_->shutdown();
}

////////
std::shared_ptr<Node::Status> BlockDataManager::getNodeStatus() const
{
   if (processNode_ == nullptr) {
      return nullptr;
   }

   auto nss = std::make_shared<Node::Status>();
   if (processNode_->connected()) {
      nss->state = Node::NodeState::Online;
   }

   if (processNode_->isSegWit()) {
      nss->segWitEnabled = true;
   }

   if (nodeRPC_ == nullptr) {
      return nss;
   }

   nss->rpcState = nodeRPC_->testConnection();
   if (nss->rpcState != Node::RpcState::Online) {
      pollNodeStatus();
   }
   nss->chainStatus = nodeRPC_->getChainStatus();
   return nss;
}

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
      while (nodeRPC->testConnection() != Node::RpcState::Online) {
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

////////
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

bool BlockDataManager::isRunning() const
{
   return BDMstate_ != BDMState::Uninitialized;
}

////////
void BlockDataManager::registerOneTimeHook(
   std::shared_ptr<BDVNotificationHook> hook)
{
   oneTimeHooks_.push_back(move(hook));
}

void BlockDataManager::triggerOneTimeHooks(BDV_Notification* notifPtr)
{
   try {
      while (true) {
         auto hookPtr = oneTimeHooks_.pop_front();
         if (hookPtr == nullptr) {
            continue;
         }
         hookPtr->func(notifPtr);
      }
   } catch (const Threading::IsEmpty&) {}
}

////////
std::shared_ptr<Blockchain> BlockDataManager::blockchain() const
{
   return blockchain_;
}

std::shared_ptr<BlockchainData> BlockDataManager::blockchainData() const
{
   return blockchainData_;
}

LMDBBlockDatabase* BlockDataManager::getIFace() const
{
   return iface_;
}

std::shared_ptr<BlockFiles> BlockDataManager::blockFiles() const
{
   return blockFiles_;
}

std::shared_ptr<ScrAddrFilter> BlockDataManager::getScrAddrFilter() const
{
   return scrAddrData_;
}

std::shared_ptr<ZeroConf::ZeroConfContainer>
BlockDataManager::zeroConfCont() const
{
   return zeroConfCont_;
}

////////
bool BlockDataManager::hasException() const
{
   return exceptPtr_ != nullptr;
}

std::exception_ptr BlockDataManager::getException() const
{
   return exceptPtr_;
}

////////
unsigned BlockDataManager::getCheckedTxCount() const
{
   return checkTransactionCount_;
}
