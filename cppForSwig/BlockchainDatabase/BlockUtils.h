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

#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <set>
#include <future>
#include <exception>
#include <functional>

#include <bdmenums.h>
#include "ScrAddrFilter.h"

class BlockFiles;
class DatabaseBuilder;
struct BDV_Notification;
struct BDVNotificationHook;
struct StoredHeader;

namespace Armory
{
   namespace ZeroConf
   {
      class ZeroConfCallbacks;
   }

   namespace Node
   {
      class BitcoinNodeInterface;
   }
   struct ReorganizationState;
}

namespace CoreRPC
{
   struct NodeStatus;
   class NodeRPCInterface;
}

enum class BDMState : int
{
   Offline,
   Initializing,
   Ready
};

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
   class BDM_ScrAddrFilter;

private:
   LMDBBlockDatabase* iface_ = nullptr;
   std::shared_ptr<BDM_ScrAddrFilter> scrAddrData_;
   std::shared_ptr<Armory::Blockchain> blockchain_;
   std::shared_ptr<BlockFiles> blockFiles_;
   std::shared_ptr<DatabaseBuilder> dbBuilder_;

   std::function<bool(void)> shutdownLbd_;
   BDMState BDMstate_ = BDMState::Offline;
   std::exception_ptr exceptPtr_ = nullptr;

   unsigned checkTransactionCount_ = 0;
   mutable std::shared_ptr<std::mutex> nodeStatusPollMutex_;
   Armory::Threading::Queue<std::shared_ptr<BDVNotificationHook>> oneTimeHooks_;

public:
   typedef std::function<void(BDMPhase, double,unsigned, unsigned)> ProgressCallback;
   std::shared_ptr<Armory::Node::BitcoinNodeInterface> processNode_, watchNode_;
   std::shared_future<bool> isReadyFuture_;
   mutable std::shared_ptr<CoreRPC::NodeRPCInterface> nodeRPC_;

   Armory::Threading::TimedQueue<std::unique_ptr<BDV_Notification>> notificationStack_;
   std::shared_ptr<Armory::ZeroConf::ZeroConfContainer> zeroConfCont_;

public:
   BlockDataManager(std::function<bool(void)>);
   ~BlockDataManager(void);

   std::shared_ptr<Armory::Blockchain> blockchain(void) const;
   LMDBBlockDatabase *getIFace(void) const;
   std::shared_ptr<BlockFiles> blockFiles(void) const;
   bool hasException(void) const;
   std::exception_ptr getException(void) const;

   void openDatabase(void);
   bool doInitialSyncOnLoad(BdmInitMode, const ProgressCallback&);

private:
   bool loadDiskState(const ProgressCallback&, bool=false);
   void pollNodeStatus() const;

public:
   Armory::ReorganizationState readBlkFileUpdate(void);
   bool applyBlockRangeToDB(ProgressCallback,
      uint32_t, ScrAddrFilter&);

   uint32_t getTopBlockHeight(void) const;
   uint8_t getValidDupIDForHeight(uint32_t) const;

   std::shared_ptr<ScrAddrFilter> getScrAddrFilter(void) const;
   StoredHeader getMainBlockFromDB(uint32_t) const;
   StoredHeader getBlockFromDB(uint32_t, uint8_t) const;

   void enableZeroConf(bool=false);
   void registerZcCallbacks(
      std::unique_ptr<Armory::ZeroConf::ZeroConfCallbacks>);
   void disableZeroConf(void);
   bool isZcEnabled(void) const;
   std::shared_ptr<Armory::ZeroConf::ZeroConfContainer> zeroConfCont(void) const;

   void triggerShutdown(void);
   void shutdown(void);
   void cleanup(void);
   bool isRunning(void) const;
   void blockUntilReady(void) const;
   bool isReady(void) const;
   void resetDatabases(BdmInitMode);

   unsigned getCheckedTxCount(void) const;
   std::shared_ptr<CoreRPC::NodeStatus> getNodeStatus(void) const;

   void registerOneTimeHook(std::shared_ptr<BDVNotificationHook>);
   void triggerOneTimeHooks(BDV_Notification*);
};
