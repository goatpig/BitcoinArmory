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

   bool bdmIsReady() const override
   {
      return bdm_->isReady();
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
         bdm_->notificationStack.push_back(std::move(notifPtr));
      };
      auto result = bdm_->applyBlockRangeToDB(progress, startBlock, *this);
      if (!result.valid()) {
         LOGERR << "ArmoryDB encountered a fatal error while scanning the chain";
         LOGERR << "It will now terminate. Restart it to auto-repair";

         auto notifPtr = std::make_unique<BDV_Notification_Error>(
            BDV_NOTIF_BROADCAST, BDM_FATAL_ERROR_CODE, BinaryData{},
            std::string{"fatal error while scanning"}
         );
         bdm_->notificationStack.push_back(std::move(notifPtr));
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
   BDMstate_.store((int)BDMState::Uninitialized, std::memory_order_relaxed);
   blockchain_ = std::make_shared<Blockchain>(
      Config::BitcoinSettings::getGenesisBlockHash());
   blockchainData_ = std::make_shared<BlockchainData>(blockchain_);
   blockFiles_ = std::make_shared<BlockFiles>(Config::Pathing::blkFilePath());
   iface_ = new LMDBBlockDatabase(Config::Pathing::dbDir());
   nodeStatusPollMutex_ = std::make_shared<std::mutex>();
   startPromise_ = std::make_unique<std::promise<bool>>();

   try {
      processNode = Config::NetworkSettings::bitcoinNodes().first;
      watchNode = Config::NetworkSettings::bitcoinNodes().second;
      nodeRPC = Config::NetworkSettings::rpcNode();
      if (processNode == nullptr) {
         throw DbErrorMsg("invalid node type in bdmConfig");
      }

      zeroConfCont_ = std::make_shared<ZeroConf::ZeroConfContainer>(
         iface_, blockchain_, blockchainData_, processNode,
         Config::DBSettings::zcThreadCount());
      zeroConfCont_->setWatcherNode(watchNode);

      scrAddrData_ = std::make_shared<BDM_ScrAddrFilter>(this);
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
   processNode.reset();
   watchNode.reset();
   scrAddrData_.reset();

   if (iface_ != nullptr) {
      iface_->closeDatabases();
   }
   delete iface_;
   iface_ = nullptr;
}

////////
void BlockDataManager::signalStart(bool success)
{
   try {
      startPromise_->set_value(success);
   } catch (const std::future_error&) {
      //promise already set, nothing to do
   }
}

bool BlockDataManager::waitOnStartSignal()
{
   auto fut = startPromise_->get_future();
   return fut.get();
}

////////
void BlockDataManager::shutdown()
{
   try {
      if (startPromise_ != nullptr) {
         startPromise_->set_value(false);
      }
   } catch (const std::future_error&) {
      //promise already set, ignore
   }
   disableZeroConf();
   notificationStack.terminate();

   if (processNode) {
      processNode->shutdown();
   }
   if (watchNode) {
      watchNode->shutdown();
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
   iface_->openDatabases();
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
         try {
            scrAddrData_->updateScannedHash(Hash32{});
         } catch (const LmdbWrapperException&) {
            //no addresses yet in the filter, reset it entirely
            scrAddrData_->resetSDBI();
         }
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

   BDMstate_.store((int)BDMState::Initializing, std::memory_order_relaxed);
   dbBuilder_ = std::make_shared<Database::Builder>(*this, progress);
   if (!dbBuilder_->init()) {
      //fatal error in db startup, terminate bdm
      readyProm.set_value(false);
      return false;
   }

   if (Config::DBSettings::checkChain()) {
      checkTransactionCount_ = dbBuilder_->getCheckedTxCount();
   }

   BDMstate_.store((int)BDMState::Ready, std::memory_order_relaxed);
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
   if (processNode == nullptr) {
      return nullptr;
   }

   auto nss = std::make_shared<Node::Status>();
   if (processNode->connected()) {
      nss->state = Node::NodeState::Online;
   }

   if (processNode->isSegWit()) {
      nss->segWitEnabled = true;
   }

   if (nodeRPC == nullptr) {
      return nss;
   }

   nss->rpcState = nodeRPC->testConnection();
   if (nss->rpcState != Node::RpcState::Online) {
      return nss;
   }
   nss->chainStatus = nodeRPC->getChainStatus();
   return nss;
}

////////
void BlockDataManager::blockUntilReady() const
{
   while (true) {
      try {
         isReadyFuture.wait();
         return;
      } catch (const std::future_error&) {
         std::this_thread::sleep_for(100ms);
      }
   }
}

bool BlockDataManager::isReady() const
{
   return (BDMState)BDMstate_.load(std::memory_order_relaxed) ==
      BDMState::Ready;
}

bool BlockDataManager::isRunning() const
{
   return (BDMState)BDMstate_.load(std::memory_order_relaxed) !=
      BDMState::Uninitialized;
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
