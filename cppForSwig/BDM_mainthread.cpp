////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BDM_mainthread.h"
#include "BlockDataViewer.h"

#include "nodeRPC.h"
#include "BitcoinP2p.h"

#include <ctime>

using namespace std;
using namespace Armory::Config;

BDM_CallBack::~BDM_CallBack()
{}

BlockDataManagerThread::BlockDataManagerThread()
{
   pimpl = new BlockDataManagerThreadImpl;
   pimpl->bdm = new BlockDataManager();
}

BlockDataManagerThread::~BlockDataManagerThread()
{
   if (pimpl == nullptr)
      return;

   if (pimpl->run)
   {
      LOGERR << "Destroying BlockDataManagerThread without shutting down first";
   }
   else
   {
      delete pimpl;
      pimpl = nullptr;
   }
}

void BlockDataManagerThread::start(BDM_INIT_MODE mode)
{
   pimpl->mode = mode;
   pimpl->run = true;
   pimpl->tID = thread(thrun, this);
}

BlockDataManager *BlockDataManagerThread::bdm()
{
   return pimpl->bdm;
}

bool BlockDataManagerThread::shutdown()
{
   if (pimpl == nullptr)
      return false;
   
   pimpl->bdm->shutdownNotifications();

   if (!pimpl->run)
      return true;

   pimpl->run = false;
   pimpl->bdm->shutdownNode();

   if (pimpl->tID.joinable())
      pimpl->tID.join();

   return true;
}

void BlockDataManagerThread::join()
{
   if (pimpl->run) {
      if (pimpl->tID.joinable()) {
         pimpl->tID.join();
      }
   }
}

void BlockDataManagerThread::run()
try {
   BlockDataManager *const bdm = this->bdm();
   if (bdm->hasException()) {
      return;
   }

   promise<bool> isReadyPromise;
   bdm->isReadyFuture_ = isReadyPromise.get_future();

   auto updateNodeStatusLambda = [bdm]()->void
   {
      try {
         auto nodeStatus = bdm->getNodeStatus();
         auto notifPtr = std::make_unique<BDV_Notification_NodeStatus>(
            std::move(nodeStatus));
         bdm->notificationStack_.push_back(std::move(notifPtr));
      } catch (const std::exception& e) {
         LOGERR << "Can't get node status: " << e.what();
      }
   };

   //connect to node as async, no need to wait for a succesful connection
   //to init the DB
   bdm->processNode_->connectToNode(true);
   bdm->watchNode_->connectToNode(true);

   //if RPC is running, wait on node init
   try {
      bdm->nodeRPC_->waitOnChainSync(updateNodeStatusLambda);
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
      bdm->notificationStack_.push_back(std::move(notifPtr));
   };

   unsigned mode = pimpl->mode & 0x00000003;
   bool clearZc = DBSettings::clearMempool();

   switch (mode)
   {
      case 0:
         bdm->doInitialSyncOnLoad(loadProgress);
         break;

      case 1:
         bdm->doInitialSyncOnLoad_Rescan(loadProgress);
         break;

      case 2:
         bdm->doInitialSyncOnLoad_Rebuild(loadProgress);
         break;

      case 3:
         bdm->doInitialSyncOnLoad_RescanBalance(loadProgress);
         break;

      default:
         throw runtime_error("invalid bdm init mode");
   }

   if (!DBSettings::checkChain()) {
      bdm->enableZeroConf(clearZc);
   }
   isReadyPromise.set_value(true);

   if (DBSettings::checkChain()) {
      return;
   }

   auto updateChainLambda = [bdm, this]()->void
   {
      LOGINFO << "readBlkFileUpdate";
      auto reorgState = bdm->readBlkFileUpdate();
      if (reorgState.hasNewTop_) {
         //purge zc container
         auto purgeFuture =
            bdm->zeroConfCont_->pushNewBlockNotification(reorgState);
         auto purgePacket = purgeFuture.get();

         //notify bdvs
         auto notifPtr = make_unique<BDV_Notification_NewBlock>(
            std::move(reorgState), purgePacket);
         bdm->triggerOneTimeHooks(notifPtr.get());
         bdm->notificationStack_.push_back(std::move(notifPtr));

         stringstream ss;
         ss << "found new top!" << endl;
         ss << "  hash: " << reorgState.newTop_->getThisHash().toHexStr() << endl;
         ss << "  height: " << reorgState.newTop_->getBlockHeight();

         LOGINFO << ss.str();
      }
   };

   bdm->processNode_->registerNodeStatusLambda(updateNodeStatusLambda);
   bdm->nodeRPC_->registerNodeStatusLambda(updateNodeStatusLambda);

   auto newBlockStack = bdm->processNode_->getInvBlockStack();
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


// kate: indent-width 3; replace-tabs on;

