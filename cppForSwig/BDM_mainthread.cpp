////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016, goatpig                                               //            
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                   
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BDM_mainthread.h"
#include "BlockUtils.h"
#include "BlockDataViewer.h"

#include <ctime>

////////////////////////////////////////////////////////////////////////////////
void BlockDataManager::registerBDVwithZCcontainer(
   shared_ptr<BDV_Server_Object> bdvPtr)
{
   auto filter = [bdvPtr](const BinaryData& scrAddr)->bool
   {
      return bdvPtr->hasScrAddress(scrAddr);
   };

   auto newzc = [bdvPtr](
      map<BinaryData, shared_ptr<map<BinaryData, TxIOPair>>> zcMap)->void
   {
      bdvPtr->zcCallback(move(zcMap));
   };

   ZeroConfContainer::BDV_Callbacks callbacks;
   callbacks.addressFilter_ = filter;
   callbacks.newZcCallback_ = newzc;

   zeroConfCont_->insertBDVcallback(move(bdvPtr->getID()), move(callbacks));
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataManager::unregisterBDVwithZCcontainer(
   const string& bdvID)
{
   zeroConfCont_->eraseBDVcallback(bdvID);
}

BDM_CallBack::~BDM_CallBack()
{}

struct BlockDataManagerThread::BlockDataManagerThreadImpl
{
   BlockDataManager *bdm=nullptr;
   int mode=0;
   volatile bool run=false;
   bool failure=false;
   thread tID;

   ~BlockDataManagerThreadImpl()
   {
      delete bdm;
   }
};

BlockDataManagerThread::BlockDataManagerThread(const BlockDataManagerConfig &config)
{
   pimpl = new BlockDataManagerThreadImpl;
   pimpl->bdm = new BlockDataManager(config);
}

BlockDataManagerThread::~BlockDataManagerThread()
{
   if (pimpl->run)
   {
      LOGERR << "Destroying BlockDataManagerThread without shutting down first";
   }
   else
   {
      delete pimpl;
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

void BlockDataManagerThread::setConfig(const BlockDataManagerConfig &config)
{
   pimpl->bdm->setConfig(config);
}


// stop the BDM thread
void BlockDataManagerThread::shutdownAndWait()
{
   requestShutdown();
   
   if (pimpl->tID.joinable())
      pimpl->tID.join();
}

bool BlockDataManagerThread::requestShutdown()
{
   if (pimpl->run)
   {
      pimpl->run = false;
      //pimpl->inject->notify();

      return true;
   }

   return false;
}

namespace
{
class OnFinish
{
   const function<void()> fn;
public:
   OnFinish(const function<void()> &fn)
      : fn(fn) { }
   ~OnFinish()
   {
      fn();
   }
};


}

void BlockDataManagerThread::run()
try
{
   BlockDataManager *const bdm = this->bdm();
      
   promise<bool> isReadyPromise;
   bdm->isReadyFuture_ = isReadyPromise.get_future();
   
   {
      bdm->networkNode_->connectToNode();

      tuple<BDMPhase, double, unsigned, unsigned> lastvalues;
      time_t lastProgressTime=0;
      
      class BDMStopRequest
      {
      public:
         virtual ~BDMStopRequest() { }
      };
     
      const auto loadProgress
         = [&] (BDMPhase phase, double prog,unsigned time, unsigned numericProgress)
      {
         const tuple<BDMPhase, double, unsigned, unsigned> currentvalues
            { phase, prog, time, numericProgress };
         if (currentvalues == lastvalues)
            return; // don't go to python if nothing's changed
         
         // also, don't go to the python if the phase is the same and it's been
         // less than 1 second since the last time this has been called
         // python is a lot slower than C++, so we don't want to invoke
         // the python interpreter to frequently
         const time_t currentProgressTime = std::time(nullptr);
         if (phase == get<0>(lastvalues)
            && currentProgressTime <= lastProgressTime+1
            && fabs(get<1>(lastvalues)-get<1>(currentvalues)) <= .01 )
            return;
            
         lastProgressTime = currentProgressTime;
         
         lastvalues = currentvalues;
         
         //pass empty walletID for main build&scan calls
         //callback->progress(phase, vector<string>(), prog, time, numericProgress);

         if (!pimpl->run)
         {
            LOGINFO << "Stop requested detected";
            throw BDMStopRequest();
         }
         
      };
      
      try
      {
         bdm->openDatabase();

         unsigned mode = pimpl->mode & 0x00000003;
         bool clearZc = pimpl->mode & 0x00000004;

         if (mode == 0) bdm->doInitialSyncOnLoad(loadProgress);
         else if (mode == 1) bdm->doInitialSyncOnLoad_Rescan(loadProgress);
         else if (mode == 2) bdm->doInitialSyncOnLoad_Rebuild(loadProgress);
         else if (mode == 3) bdm->doInitialSyncOnLoad_RescanBalance(loadProgress);

         bdm->enableZeroConf(clearZc);
      }
      catch (BDMStopRequest&)
      {
         LOGINFO << "UI asked build/scan thread to finish";
         return;
      }
   }

   isReadyPromise.set_value(true);
   
   auto updateChainLambda = [bdm, this]()->bool
   {
      auto reorgState = bdm->readBlkFileUpdate();
      if (reorgState.hasNewTop)
      {
         bdm->newBlocksStack_.push_back(move(reorgState));
         
         ZeroConfContainer::ZcActionStruct zcaction;
         zcaction.action_ = Zc_Purge;

         bdm->zeroConfCont_->newZcStack_.push_back(move(zcaction));
         return true;
      }

      return false;
   };

   while(pimpl->run)
   {
      /*if(bdv->getZCflag())
      {
         bdv->flagRescanZC(false);
         auto&& newZCTxHash = bdv->parseNewZeroConfTx();
         if (newZCTxHash.size() > 0)
         {
            bdv->scanWallets();

            vector<LedgerEntry> newZCLedgers;

            for (const auto& txHash : newZCTxHash)
            {
               auto& le_w = bdv->getTxLedgerByHash_FromWallets(txHash);
               if (le_w.getTxTime() != 0)
                  newZCLedgers.push_back(le_w);

               auto& le_lb = bdv->getTxLedgerByHash_FromLockboxes(txHash);
               if (le_lb.getTxTime() != 0)
                  newZCLedgers.push_back(le_lb);
            }

            LOGINFO << newZCLedgers.size() << " new ZC Txn";
            //notify ZC
            callback->run(BDMAction_ZC, &newZCLedgers);
         }
      }*/

      //register promise with p2p interface
      promise<bool> newBlocksPromise;
      auto newBlocksFuture = newBlocksPromise.get_future();
      
      auto newBlocksCallback = 
         [&newBlocksPromise](const vector<InvEntry>&)->void
      {
         newBlocksPromise.set_value(true);
      };

      bdm->networkNode_->registerInvBlockLambda(newBlocksCallback);

      //keep updating until there are no more new blocks
      while (updateChainLambda());

      //wait on future
      newBlocksFuture.get();
   }
}
catch (std::exception &e)
{
   LOGERR << "BDM thread failed: " << e.what();
   string errstr(e.what());
   /*pimpl->callback->run(BDMAction_ErrorMsg, &errstr);
   pimpl->inject->setFailureFlag();
   pimpl->inject->notify();*/
}
catch (...)
{
   LOGERR << "BDM thread failed: (unknown exception)";
   /*pimpl->inject->setFailureFlag();
   pimpl->inject->notify();*/
}

void* BlockDataManagerThread::thrun(void *_self)
{
   BlockDataManagerThread *const self
      = static_cast<BlockDataManagerThread*>(_self);
   self->run();
   return 0;
}


// kate: indent-width 3; replace-tabs on;

