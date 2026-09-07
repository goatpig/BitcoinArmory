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

#include "BDM_mainthread.h"
#include <Utils/ArmoryConfig.h>
#include <BlockchainDatabase/BlockUtils.h>
#include <BlockchainDatabase/BlockObj.h>
#include <ZeroConf/Parser.h>

#include <Node/BitcoinP2P.h>
#include "BDV_Notification.h"

using namespace Armory::Config;

BDM_CallBack::~BDM_CallBack()
{}

BlockDataManagerThread::BlockDataManagerThread()
{
   pimpl = std::make_unique<BlockDataManagerThreadImpl>();
   pimpl->bdm = std::make_shared<BlockDataManager>([this]()->bool{
      return this->shutdown();
   });
}

BlockDataManagerThread::~BlockDataManagerThread()
{
   if (pimpl == nullptr) {
      return;
   }
   if (pimpl->run) {
      LOGERR << "Destroying BlockDataManagerThread without shutting down first";
   } else {
      pimpl.reset();
   }
}

void BlockDataManagerThread::start(BdmInitMode mode)
{
   pimpl->mode = mode;
   pimpl->run = true;
   pimpl->tID = std::thread(thrun, this);
}

std::shared_ptr<BlockDataManager> BlockDataManagerThread::bdm()
{
   return pimpl->bdm;
}

bool BlockDataManagerThread::shutdown()
{
   if (pimpl == nullptr) {
      return false;
   }
   if (pimpl->run) {
      pimpl->run = false;

      auto shutdownLbd = [bdmPtr=pimpl->bdm]()
      {
         bdmPtr->shutdown();
         bdmPtr->cleanup();
      };
      std::thread shutdownThr(shutdownLbd);
      if (shutdownThr.joinable()) {
         shutdownThr.join();
      }
   }

   join();
   return true;
}

void BlockDataManagerThread::join()
{
   if (pimpl->tID.joinable()) {
      pimpl->tID.join();
   }
}

void BlockDataManagerThread::run()
try {
   const auto bdm = this->bdm();
   try {
      if (bdm->hasException()) {
         std::rethrow_exception(bdm->getException());
      }
      bdm->openDatabase();
   } catch (const std::exception& e) {
      LOGERR << "error during bdm init: " << e.what();
      return;
   }

   std::promise<bool> isReadyPromise;
   bdm->isReadyFuture = isReadyPromise.get_future();

   auto updateNodeStatusLambda = [bdm]()->void
   {
      try {
         auto nodeStatus = bdm->getNodeStatus();
         auto notifPtr = std::make_unique<BDV_Notification_NodeStatus>(
            std::move(nodeStatus));
         bdm->notificationStack.push_back(std::move(notifPtr));
      } catch (const std::exception& e) {
         LOGERR << "Can't get node status: " << e.what();
      }
   };

   //connect to node as async, no need to wait for a succesful connection
   //to init the DB
   bdm->processNode->connectToNode(true);
   bdm->watchNode->connectToNode(true);

   //if RPC is running, wait on node init
   try {
      LOGINFO << "Waiting on node sync";
      bdm->nodeRPC->waitOnChainSync(
         updateNodeStatusLambda,
         DBSettings::automatedNode()
      );
   } catch (const std::exception& e) {
      LOGINFO << "Error occured while querying the RPC for sync status";
      LOGINFO << "Message: " << e.what();
   }

   const auto loadProgress
      = [&](BDMPhase phase, double prog, unsigned time, unsigned numericProgress)
   {
      //pass empty walletID for main build&scan calls
      auto notifPtr = std::make_unique<BDV_Notification_Progress>(
         phase, prog, time, numericProgress,
         std::vector<std::string>{}
      );
      bdm->notificationStack.push_back(std::move(notifPtr));
   };

   if (NetworkSettings::ephemeralPeers()) {
      //this is an automated db instance, wait on the master's signal
      //to start the db scan
      LOGINFO << "waiting on start signal from master";
      if (!bdm->waitOnStartSignal()) {
         LOGINFO << "master signaled to shutdown";
         return;
      }
      LOGINFO << "master signaled to start scanning";
   }

   if (!bdm->doInitialSyncOnLoad(pimpl->mode, loadProgress)) {
      //db init failed, exit
      return;
   }

   if (!DBSettings::checkChain()) {
      if (DBSettings::enableZC()) {
         bdm->enableZeroConf(DBSettings::clearMempool());
      }
   }
   isReadyPromise.set_value(true);

   if (DBSettings::checkChain()) {
      return;
   }

   auto updateChainLambda = [bdm, this]()->void
   {
      LOGINFO << "readBlkFileUpdate";
      auto reorgState = bdm->readBlkFileUpdate();
      if (reorgState.hasNewTop) {
         //purge zc container
         std::shared_ptr<Armory::ZeroConf::ZcPurgePacket> purgePacket = nullptr;
         if (DBSettings::enableZC()) {
            auto purgeFuture = bdm->zeroConfCont()->pushNewBlockNotification(
               reorgState);
            purgePacket = purgeFuture.get();
         }

         //notify bdvs
         auto notifPtr = std::make_unique<BDV_Notification_NewBlock>(
            std::move(reorgState), purgePacket);
         bdm->triggerOneTimeHooks(notifPtr.get());
         bdm->notificationStack.push_back(std::move(notifPtr));

         std::stringstream ss;
         ss << "found new top!" << std::endl;
         ss << "  hash: " << reorgState.newTop->getThisHash().toHexStr() << std::endl;
         ss << "  height: " << reorgState.newTop->getBlockHeight();
         LOGINFO << ss.str();
      }
   };

   bdm->processNode->registerNodeStatusCallback(updateNodeStatusLambda);
   bdm->nodeRPC->registerNodeStatusLambda(updateNodeStatusLambda);

   auto newBlockStack = bdm->processNode->getInvBlockStack();
   while (pimpl->run) {
      try {
         //wait on a new block InvEntry, blocking is on
         auto invVec = newBlockStack->pop_front();

         bool hasNewBlocks = true;
         while (hasNewBlocks) {
            //check blocks on disk, update chain state accordingly
            updateChainLambda();
            hasNewBlocks = false;

            while (true) {
               /*
               More new blocks may have appeared while we were parsing the
               current batch. The chain update code will grab as many blocks
               as it sees in a single call. Therefor, while N new blocks
               generate N new block notifications, a single call to
               updateChainLambda would cover them all.

               updateChainLambda is an expensive call and it is unnecessary to
               run it as many times as we have pending new block notifications.
               The notifications just indicate that updateChainLamda should be
               ran, not how often. Hence after a run to updateChainLambda, we
               want to deplete the block notification queue, run
               updateChainLambda one more time for good measure, and break out
               of the inner, non blocking queue wait loop once it is empty.

               The outer blocking queue wait will then once again act as the
               signal to check the chain and deplete the queue
               */

               try {
                  //wait on new block entry, do not block for the inner loop
                  invVec = move(newBlockStack->pop_front(false));
                  hasNewBlocks = true;
               } catch (const Armory::Threading::IsEmpty&) {
                  break;
               }
            }
         }
      } catch (const Armory::Threading::StopBlockingLoop&) {
         break;
      }
   }
} catch (const std::exception &e) {
   LOGERR << "BDM thread failed: " << e.what();
} catch (...) {
   LOGERR << "BDM thread failed: (unknown exception)";
}

void* BlockDataManagerThread::thrun(void *_self)
{
   BlockDataManagerThread *const self
      = static_cast<BlockDataManagerThread*>(_self);
   self->run();
   return 0;
}
