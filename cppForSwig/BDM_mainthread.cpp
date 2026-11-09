////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2026, goatpig                                          //
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
   pimpl->bdm = std::make_shared<BlockDataManager>();
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
   std::promise<void> isReadyPromise;
   pimpl->bdm->isReadyFuture = isReadyPromise.get_future();
   pimpl->tID = std::thread(
      [this](std::promise<void> prom){ this->run(std::move(prom)); },
      std::move(isReadyPromise)
   );
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
      pimpl->bdm->shutdown();
      pimpl->bdm->cleanup();
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

void BlockDataManagerThread::run(std::promise<void> isReadyPromise)
{
   const auto bdm = this->bdm();

   //node status notif callback
   auto updateNodeStatusLambda = [queue=bdm->notificationQueue]()
   {
      try {
         auto notifPtr = std::make_unique<BDV_Notification_UpdateNodeStatus>();
         queue->push_back(std::move(notifPtr));
      } catch (const std::exception& e) {
         LOGERR << "Can't get node status: " << e.what();
      }
   };

   //new block parsing helper
   auto newBlockStack = bdm->processNode->getInvBlockStack();
   auto updateChainLambda = [bdm]()
   {
      LOGDEBUG << "readBlkFileUpdate";
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
         bdm->notificationQueue->push_back(std::move(notifPtr));

         std::stringstream ss;
         ss << "found new top!" << std::endl;
         ss << "  hash: " << reorgState.newTop->getThisHash().toHexStr() << std::endl;
         ss << "  height: " << reorgState.newTop->getBlockHeight();
         LOGINFO << ss.str();
      }
   };

   //state enum
   enum class Sequence : int
   {
      INIT,
      WAIT_ON_CHAIN_SYNC,
      WAIT_ON_START_SIGNAL,
      SYNC_DB,
      ENABLE_ZC,
      LOOP_OVER_BLOCKS,
      ERROR,
      EXIT
   };

   //bdmThread star sequence
   auto bdmStep = Sequence::INIT;
   while (pimpl->run) {
      switch (bdmStep)
      {
         case Sequence::INIT:
         {
            try {
               if (bdm->hasException()) {
                  std::rethrow_exception(bdm->getException());
               }
               bdm->openDatabase();
            } catch (const std::exception& e) {
               LOGERR << "error during bdm init: " << e.what();
               bdmStep = Sequence::ERROR;
               break;
            }

            //connect to node as async, no need to wait for a succesful
            //connection to init the DB
            bdm->processNode->connectToNode(true);
            bdm->processNode->registerNodeStatusCallback(
               updateNodeStatusLambda);
            bdm->watchNode->connectToNode(true);
            bdmStep = Sequence::WAIT_ON_CHAIN_SYNC;
            break;
         }

         case Sequence::WAIT_ON_CHAIN_SYNC:
         {
            //if RPC is running, wait on chain sync
            try {
               LOGINFO << "Waiting on node sync";
               bdm->nodeRPC->waitOnChainSync(
                  updateNodeStatusLambda,
                  DBSettings::automatedNode()
               );
            } catch (const std::exception& e) {
               LOGWARN << "Error occured while querying the RPC for sync status";
               LOGWARN << "Message: " << e.what();
            }
            bdmStep = Sequence::WAIT_ON_START_SIGNAL;
            break;
         }

         case Sequence::WAIT_ON_START_SIGNAL:
         {
            if (NetworkSettings::ephemeralPeers()) {
               //this is an automated db instance, wait on the master's signal
               //to start the db scan
               LOGINFO << "waiting on start signal from master";
               if (!bdm->waitOnStartSignal()) {
                  LOGINFO << "master signaled to shutdown";
                  bdmStep = Sequence::ERROR;
                  break;
               } else {
                  LOGINFO << "master signaled to start operations";
               }
            }
            bdmStep = Sequence::SYNC_DB;
            break;
         }

         case Sequence::SYNC_DB:
         {
            const auto loadProgress = [queue=bdm->notificationQueue](
               BDMPhase phase, double prog, unsigned time, unsigned numericProgress)
            {
               //pass empty walletID for main build&scan calls
               auto notifPtr = std::make_unique<BDV_Notification_Progress>(
                  phase, prog, time, numericProgress,
                  std::vector<std::string>{}
               );
               queue->push_back(std::move(notifPtr));
            };

            if (!bdm->doInitialSyncOnLoad(pimpl->mode, loadProgress)) {
               //db init failed, exit
               bdmStep = Sequence::ERROR;
            } else {
               bdmStep = Sequence::ENABLE_ZC;
            }
            break;
         }

         case Sequence::ENABLE_ZC:
         {
            if (DBSettings::enableZC()) {
               bdm->enableZeroConf(DBSettings::clearMempool());
            }
            isReadyPromise.set_value();
            bdmStep = Sequence::LOOP_OVER_BLOCKS;
            break;
         }

         case Sequence::LOOP_OVER_BLOCKS:
         {
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
               bdmStep = Sequence::EXIT;
            }
            break;
         }

         case Sequence::ERROR:
         case Sequence::EXIT:
         {
            pimpl->run = false;
            break;
         }
      }
   }

   if (bdmStep == Sequence::ERROR) {
      //on errors, isReadyPromise has yet to be set
      isReadyPromise.set_value();
      LOGERR << "bdm thread sequence exited with an error";
   }
   LOGINFO << "exiting bdm thread";
}
