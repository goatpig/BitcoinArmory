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

#include "ScrAddrFilter.h"
#include "BlockUtils.h"
#include "txio.h"
#include <thread>


///////////////////////////////////////////////////////////////////////////////
//ScrAddrScanData Methods
///////////////////////////////////////////////////////////////////////////////
atomic<unsigned> ScrAddrFilter::keyCounter_;
atomic<unsigned> ScrAddrFilter::WalletInfo::idCounter_;
atomic<bool> ScrAddrFilter::run_;

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::init()
{
   keyCounter_.store(0, memory_order_relaxed);
   run_.store(true, memory_order_relaxed);
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::cleanUpPreviousChildren(LMDBBlockDatabase* lmdb)
{
   //get rid of sdbi entries created by side scans that have not been 
   //cleaned up during the previous run

   set<BinaryData> sdbiKeys;

   //clean up SUBSSH SDBIs
   {
      auto&& tx = lmdb->beginTransaction(SSH, LMDB::ReadWrite);
      auto dbIter = lmdb->getIterator(SSH);

      while (dbIter->advanceAndRead(DB_PREFIX_DBINFO))
      {
         auto&& keyRef = dbIter->getKeyRef();
         if (keyRef.getSize() != 3)
            throw runtime_error("invalid sdbi key in SSH db");

         auto id = (uint16_t*)(keyRef.getPtr() + 1);
         if (*id == 0)
            continue;

         sdbiKeys.insert(keyRef);
      }

      for (auto& keyRef : sdbiKeys)
         lmdb->deleteValue(SSH, keyRef);
   }

   //clean up SSH SDBIs
   sdbiKeys.clear();
   {
      auto&& tx = lmdb->beginTransaction(SUBSSH, LMDB::ReadWrite);
      auto dbIter = lmdb->getIterator(SUBSSH);

      while (dbIter->advanceAndRead(DB_PREFIX_DBINFO))
      {
         auto&& keyRef = dbIter->getKeyRef();
         if (keyRef.getSize() != 3)
            throw runtime_error("invalid sdbi key in SSH db");

         auto id = (uint16_t*)(keyRef.getPtr() + 1);
         if (*id == 0)
            continue;

         sdbiKeys.insert(keyRef);
      }

      for (auto& keyRef : sdbiKeys)
         lmdb->deleteValue(SUBSSH, keyRef);
   }

   //clean up missing hashes entries in TXFILTERS
   set<BinaryData> missingHashKeys;
   {
      auto&& tx = lmdb->beginTransaction(TXFILTERS, LMDB::ReadWrite);
      auto dbIter = lmdb->getIterator(TXFILTERS);

      while (dbIter->advanceAndRead(DB_PREFIX_MISSING_HASHES))
      {
         auto&& keyRef = dbIter->getKeyRef();
         if (keyRef.getSize() != 4)
            throw runtime_error("invalid missing hashes key");

         auto id = (uint32_t*)(keyRef.getPtr());
         if ((*id & 0x00FFFFFF) == 0)
            continue;

         sdbiKeys.insert(keyRef);
      }

      for (auto& keyRef : sdbiKeys)
         lmdb->deleteValue(TXFILTERS, keyRef);
   }
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::updateAddressMerkleInDB()
{
   auto&& addrMerkle = getAddressMapMerkle();

   StoredDBInfo sshSdbi;
   auto&& tx = lmdb_->beginTransaction(SSH, LMDB::ReadWrite);

   try
   {
      sshSdbi = move(lmdb_->getStoredDBInfo(SSH, uniqueKey_));
   }
   catch (runtime_error&)
   {
      sshSdbi.magic_ = lmdb_->getMagicBytes();
      sshSdbi.metaHash_ = BtcUtils::EmptyHash_;
      sshSdbi.topBlkHgt_ = 0;
      sshSdbi.armoryType_ = ARMORY_DB_BARE;
   }

   sshSdbi.metaHash_ = addrMerkle;
   lmdb_->putStoredDBInfo(SSH, sshSdbi, uniqueKey_);
}

///////////////////////////////////////////////////////////////////////////////
StoredDBInfo ScrAddrFilter::getSubSshSDBI(void) const
{
   StoredDBInfo sdbi;
   auto&& tx = lmdb_->beginTransaction(SUBSSH, LMDB::ReadOnly);

   sdbi = move(lmdb_->getStoredDBInfo(SUBSSH, uniqueKey_));
   return sdbi;
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::putSubSshSDBI(const StoredDBInfo& sdbi)
{
   auto&& tx = lmdb_->beginTransaction(SUBSSH, LMDB::ReadWrite);
   lmdb_->putStoredDBInfo(SUBSSH, sdbi, uniqueKey_);
}

///////////////////////////////////////////////////////////////////////////////
StoredDBInfo ScrAddrFilter::getSshSDBI(void) const
{
   auto&& tx = lmdb_->beginTransaction(SSH, LMDB::ReadOnly);
   return lmdb_->getStoredDBInfo(SSH, uniqueKey_);
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::putSshSDBI(const StoredDBInfo& sdbi)
{
   auto&& tx = lmdb_->beginTransaction(SSH, LMDB::ReadWrite);
   lmdb_->putStoredDBInfo(SSH, sdbi, uniqueKey_);
}

///////////////////////////////////////////////////////////////////////////////
set<BinaryData> ScrAddrFilter::getMissingHashes(void) const
{
   return lmdb_->getMissingHashes(uniqueKey_);
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::putMissingHashes(const set<BinaryData>& hashSet)
{
   auto&& tx = lmdb_->beginTransaction(TXFILTERS, LMDB::ReadWrite);
   lmdb_->putMissingHashes(hashSet, uniqueKey_);
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::getScrAddrCurrentSyncState()
{
   map<AddrAndHash, int> newSaMap;

   {
      auto scraddrmap = scrAddrMap_->get();
      auto&& tx = lmdb_->beginTransaction(SSH, LMDB::ReadOnly);

      for (auto& scrAddr : *scraddrmap)
      {
         auto aah = scrAddr.first;
         int height = getScrAddrCurrentSyncState(scrAddr.first.scrAddr_);
         newSaMap.insert(move(make_pair(move(aah), height)));
      }
   }

   scrAddrMap_->update(newSaMap);
}

///////////////////////////////////////////////////////////////////////////////
int ScrAddrFilter::getScrAddrCurrentSyncState(
   BinaryData const & scrAddr)
{
   //grab ssh for scrAddr
   StoredScriptHistory ssh;
   lmdb_->getStoredScriptHistorySummary(ssh, scrAddr);

   //update scrAddrData lowest scanned block
   return ssh.scanHeight_;
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::setSSHLastScanned(uint32_t height)
{
   LOGWARN << "Updating ssh last scanned";
   
   auto scraddrmap = scrAddrMap_->get();
   auto&& tx = lmdb_->beginTransaction(SSH, LMDB::ReadWrite);
   for (const auto scrAddr : *scraddrmap)
   {
      StoredScriptHistory ssh;
      lmdb_->getStoredScriptHistorySummary(ssh, scrAddr.first.scrAddr_);
      if (!ssh.isInitialized())
         ssh.uniqueKey_ = scrAddr.first.scrAddr_;

      ssh.scanHeight_ = height;

      lmdb_->putStoredScriptHistorySummary(ssh);
   }
}

///////////////////////////////////////////////////////////////////////////////
bool ScrAddrFilter::registerAddresses(const set<BinaryData>& saSet, string ID,
   bool areNew, function<void(bool)> callback)
{
   shared_ptr<WalletInfo> wltInfo = make_shared<WalletInfo>();
   wltInfo->scrAddrSet_ = saSet;
   wltInfo->ID_ = ID;
   wltInfo->callback_ = callback;

   vector<shared_ptr<WalletInfo>> wltInfoVec;
   wltInfoVec.push_back(wltInfo);

   return registerAddressBatch(move(wltInfoVec), areNew);
}


///////////////////////////////////////////////////////////////////////////////
bool ScrAddrFilter::registerAddressBatch(
   vector<shared_ptr<WalletInfo>>&& wltInfoVec, bool areNew)
{
   /***
   return true if addresses were registered without the need for scanning
   ***/

   if (armoryDbType_ == ARMORY_DB_SUPER)
   {
      unique_lock<mutex> lock(mergeLock_);

      map<AddrAndHash, int> updateMap;

      for (auto& batch : wltInfoVec)
      {
         for (auto& sa : batch->scrAddrSet_)
         {
            AddrAndHash aah(sa);

            updateMap.insert(make_pair(move(aah), 0));
         }

         batch->callback_(true);
      }

      scrAddrMap_->update(updateMap);

      return true;
   }

   {
      unique_lock<mutex> lock(mergeLock_);
      
      //check against already scanning addresses
      for (auto& wlt : wltInfoVec)
      {
         for (auto& wltInfo : scanningAddresses_)
         {
            bool has = false;
            auto addrIter = wlt->scrAddrSet_.begin();
            while (addrIter != wlt->scrAddrSet_.end())
            {
               auto checkIter = wltInfo->scrAddrSet_.find(*addrIter);
               if (checkIter == wltInfo->scrAddrSet_.end())
               {
                  ++addrIter;
                  continue;
               }

               wlt->scrAddrSet_.erase(addrIter++);
               has = true;
            }

            if (!has)
               continue;

            //there were address collisions between the set to scan and
            //what's already scanning, let's bind the completion callback
            //conditions to this concurent address set

            shared_ptr<promise<bool>> parentSetPromise = 
               make_shared<promise<bool>>();
            shared_future<bool> childSetFuture = parentSetPromise->get_future();
            auto originalParentCallback = wltInfo->callback_;
            auto originalChildCallback = wlt->callback_;

            auto parentCallback = [parentSetPromise, originalParentCallback]
               (bool flag)->void
            {
               parentSetPromise->set_value(true);
               originalParentCallback(flag);
            };

            auto childCallback = [childSetFuture, originalChildCallback]
               (bool flag)->void
            {
               childSetFuture.wait();
               originalChildCallback(flag);
            };

            wltInfo->callback_ = parentCallback;
            wlt->callback_ = childCallback;
         }
      }

      //add to scanning address container
      scanningAddresses_.insert(wltInfoVec.begin(), wltInfoVec.end());
   }

   auto scraddrmapptr = scrAddrMap_->get();

   struct pred
   {
      shared_ptr<map<AddrAndHash, int>> saMap_;
      function<void(shared_ptr<WalletInfo>)> eraseLambda_;

      pred(shared_ptr<map<AddrAndHash, int>> saMap,
         function<void(shared_ptr<WalletInfo>)> eraselambda)
         : saMap_(saMap), eraseLambda_(eraselambda)
      {}

      bool operator()(shared_ptr<WalletInfo> wltInfo) const
      {
         auto saIter = wltInfo->scrAddrSet_.begin();
         while (saIter != wltInfo->scrAddrSet_.end())
         {
            if (saMap_->find(*saIter) == saMap_->end())
            {
               ++saIter;
               continue;
            }

            wltInfo->scrAddrSet_.erase(saIter++);
         }

         if (wltInfo->scrAddrSet_.size() == 0)
         {
            wltInfo->callback_(true);

            //clean up from scanning addresses container            
            eraseLambda_(wltInfo);

            return false;
         }

         return true;
      }
   };

   auto eraseAddrSetLambda = [&](shared_ptr<WalletInfo> wltInfo)->void
   {
      unique_lock<mutex> lock(mergeLock_);
      scanningAddresses_.erase(wltInfo);
   };
   
   auto removeIter = remove_if(wltInfoVec.begin(), wltInfoVec.end(), 
      pred(scraddrmapptr, eraseAddrSetLambda));
   wltInfoVec.erase(wltInfoVec.begin(), removeIter);
   
   if (wltInfoVec.size() == 0)
      return true;

   LOGINFO << "Starting address registration process";

   if (bdmIsRunning())
   {
      //BDM is initialized and maintenance thread is running, check mode

      //create ScrAddrFilter for side scan         
      shared_ptr<ScrAddrFilter> sca = copy();
      sca->setParent(this);
      bool hasNewSA = false;

      vector<pair<BinaryData, unsigned>> saVec;
      for (auto& batch : wltInfoVec)
      {
         if (batch->scrAddrSet_.size() == 0)
            continue;

         for (const auto& scrAddr : batch->scrAddrSet_)
            saVec.push_back(make_pair(scrAddr, 0));

         hasNewSA = true;
      }

      sca->regScrAddrVecForScan(saVec);
      sca->buildSideScanData(wltInfoVec, areNew);
      scanFilterInNewThread(sca);

      if (!hasNewSA)
         return true;

      return false;
   }
   else
   {
      //BDM isnt initialized yet, the maintenance thread isnt running, 
      //just register the scrAddr and return true.
      map<AddrAndHash, int> newSaMap;
      for (auto& batch : wltInfoVec)
      {
         for (const auto& scrAddr : batch->scrAddrSet_)
         {
            AddrAndHash aah(scrAddr);
            newSaMap.insert(move(make_pair(move(aah), -1)));
         }

         batch->callback_(true);
      }

      scrAddrMap_->update(newSaMap);
      return true;
   }
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::scanScrAddrThread()
{
   //Only one wallet at a time         
   uint32_t endBlock = currentTopBlockHeight();
   vector<string> wltIDs = scrAddrDataForSideScan_.getWalletIDString();

   BinaryData topScannedBlockHash;
   {
      auto&& tx = lmdb_->beginTransaction(HEADERS, LMDB::ReadOnly);
      StoredHeader sbh;
      lmdb_->getBareHeader(sbh, endBlock);
      topScannedBlockHash = sbh.thisHash_;
   }

   if(scrAddrDataForSideScan_.doScan_ == false)
   {
      //new addresses, set their last seen block in the ssh entries
      setSSHLastScanned(currentTopBlockHeight());
   }
   else
   {
      //wipe ssh
      auto scraddrmap = scrAddrMap_->get();
      vector<BinaryData> saVec;
      for (const auto& scrAddrPair : *scraddrmap)
         saVec.push_back(scrAddrPair.first.scrAddr_);
      wipeScrAddrsSSH(saVec);
      saVec.clear();

      //scan from 0
      topScannedBlockHash =
         applyBlockRangeToDB(0, endBlock, wltIDs, true);
   }
      
   addToMergePile(topScannedBlockHash);

   for (const auto& wID : wltIDs)
      LOGINFO << "Completed scan of wallet " << wID;
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::scanFilterInNewThread(shared_ptr<ScrAddrFilter> sca)
{
   auto scanMethod = [sca](void)->void
   { sca->scanScrAddrThread(); };

   thread scanThread(scanMethod);
   scanThread.detach();
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::addToMergePile(const BinaryData& lastScannedBlkHash)
{
   if (parent_ == nullptr)
      throw runtime_error("scf invalid parent");

   scrAddrDataForSideScan_.lastScannedBlkHash_ = lastScannedBlkHash;
   scrAddrDataForSideScan_.uniqueID_ = uniqueKey_;
   parent_->scanDataPile_.push_back(scrAddrDataForSideScan_);
   parent_->mergeSideScanPile();
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::mergeSideScanPile()
{
   /***
   We're about to add a set of newly registered scrAddrs to the BDM's
   ScrAddrFilter map. Make sure they are scanned up to the last known
   top block first, then merge it in.
   ***/

   vector<ScrAddrSideScanData> scanDataVec;
   map<AddrAndHash, int> newScrAddrMap;

   unique_lock<mutex> lock(mergeLock_);

   try
   {
      //pop all we can from the pile
      while (1)
         scanDataVec.push_back(move(scanDataPile_.pop_back()));
   }
   catch (IsEmpty&)
   {
      //pile is empty
   }

   if (scanDataVec.size() == 0)
      return;

   vector<string> walletIDs;

   auto bcptr = blockchain();
   bool reportProgress = false;

   uint32_t startHeight = bcptr->top()->getBlockHeight();
   for (auto& scanData : scanDataVec)
   {
      auto& topHash = scanData.lastScannedBlkHash_;
      auto&& idStrings = scanData.getWalletIDString();
      if (scanData.doScan_)
      {
         walletIDs.insert(walletIDs.end(), idStrings.begin(), idStrings.end());
         reportProgress = true;
      }

      try
      {
         auto header = bcptr->getHeaderByHash(topHash);
         auto headerHeight = header->getBlockHeight();
         if (startHeight > headerHeight)
            startHeight = headerHeight;

         for (auto& wltInfo : scanData.wltInfoVec_)
         {
            for (auto& scannedAddr : wltInfo->scrAddrSet_)
            {
               AddrAndHash aah(scannedAddr);
               newScrAddrMap.insert(move(make_pair(move(aah), headerHeight)));
            }
         }
      }
      catch (range_error&)
      {
         throw runtime_error("Couldn't grab top block from parallel scan by hash");
      }
   }

   //add addresses to main filter map
   scrAddrMap_->update(newScrAddrMap);

   //scan it all to sync all subssh and ssh to the same height
   applyBlockRangeToDB(
      startHeight + 1, 
      bcptr->top()->getBlockHeight(),
      walletIDs, reportProgress);
   updateAddressMerkleInDB();

   //clean up SDBI entries
   {
      //SSH
      {
         auto&& tx = lmdb_->beginTransaction(SSH, LMDB::ReadWrite);
         for (auto& scanData : scanDataVec)
            lmdb_->deleteValue(SSH, 
               StoredDBInfo::getDBKey(scanData.uniqueID_));
      }

      //SUBSSH
      {
         auto&& tx = lmdb_->beginTransaction(SUBSSH, LMDB::ReadWrite);
         for (auto& scanData : scanDataVec)
            lmdb_->deleteValue(SUBSSH,
               StoredDBInfo::getDBKey(scanData.uniqueID_));
      }

      //TXFILTERS
      {
         auto&& tx = lmdb_->beginTransaction(TXFILTERS, LMDB::ReadWrite);
         for (auto& scanData : scanDataVec)
            lmdb_->deleteValue(TXFILTERS,
               DBUtils::getMissingHashesKey(scanData.uniqueID_));
      }
   }

   //hit callbacks and clean up
   for (auto& scandata : scanDataVec)
   {
      for (auto wltinfo : scandata.wltInfoVec_)
      {
         wltinfo->callback_(true);
         scanningAddresses_.erase(wltinfo);
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
int32_t ScrAddrFilter::scanFrom() const
{
   int32_t lowestBlock = -1;

   if (scrAddrMap_->size() > 0)
   {
      auto scraddrmap = scrAddrMap_->get();
      lowestBlock = scraddrmap->begin()->second;

      for (auto scrAddr : *scraddrmap)
      {
         if (lowestBlock != scrAddr.second)
         {
            lowestBlock = -1;
            break;
         }
      }
   }

   if (lowestBlock != -1)
      lowestBlock++;

   return lowestBlock;
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::clear()
{
   map<AddrAndHash, int> newSaMap;

   {
      scanDataPile_.clear();
      auto scraddrmap = scrAddrMap_->get();

      for (const auto& regScrAddr : *scraddrmap)
      {
         auto aah = regScrAddr.first;
         newSaMap.insert(move(make_pair(move(aah), 0)));
      }
   }

   scrAddrMap_->update(newSaMap);
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::buildSideScanData(
   const vector<shared_ptr<WalletInfo>>& wltInfoVec,
   bool areNew)
{
   scrAddrDataForSideScan_.startScanFrom_ = INT32_MAX;
   auto scraddrmap = scrAddrMap_->get();
   for (const auto& scrAddr : *scraddrmap)
      scrAddrDataForSideScan_.startScanFrom_ = 
      min(scrAddrDataForSideScan_.startScanFrom_, scrAddr.second);

   scrAddrDataForSideScan_.wltInfoVec_ = wltInfoVec;
   scrAddrDataForSideScan_.doScan_ = !areNew;
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::getAllScrAddrInDB()
{
   auto&& tx = lmdb_->beginTransaction(SSH, LMDB::ReadOnly);
   auto dbIter = lmdb_->getIterator(SSH);   

   map<AddrAndHash, int> scrAddrMap;

   //iterate over ssh DB
   while(dbIter->advanceAndRead(DB_PREFIX_SCRIPT))
   {
      auto keyRef = dbIter->getKeyRef();
      StoredScriptHistory ssh;
      ssh.unserializeDBKey(dbIter->getKeyRef());

      AddrAndHash aah(ssh.uniqueKey_);
      auto insertResult = scrAddrMap.insert(move(make_pair(move(aah), 0)));
      if (!insertResult.second)
      {
         insertResult.second = 0;
      }
   } 

   scrAddrMap_->update(scrAddrMap);
   getScrAddrCurrentSyncState();
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::putAddrMapInDB()
{
   auto&& tx = lmdb_->beginTransaction(SSH, LMDB::ReadWrite);

   auto scraddrmap = scrAddrMap_->get();
   for (const auto& scrAddrObj : *scraddrmap)
   {
      StoredScriptHistory ssh;
      ssh.uniqueKey_ = scrAddrObj.first.scrAddr_;

      auto&& sshKey = ssh.getDBKey();

      BinaryWriter bw;
      ssh.serializeDBValue(bw, ARMORY_DB_BARE);

      lmdb_->putValue(SSH, sshKey.getRef(), bw.getDataRef());
   }
}

///////////////////////////////////////////////////////////////////////////////
BinaryData ScrAddrFilter::getAddressMapMerkle(void) const
{
   vector<BinaryData> addrVec;
   addrVec.reserve(scrAddrMap_->size());

   auto scraddrmap = scrAddrMap_->get();
   for (const auto& addr : *scraddrmap)
      addrVec.push_back(addr.first.getHash());

   if (addrVec.size() > 0)
      return BtcUtils::calculateMerkleRoot(addrVec);

   return BinaryData();
}

///////////////////////////////////////////////////////////////////////////////
bool ScrAddrFilter::hasNewAddresses(void) const
{
   if (scrAddrMap_->size() == 0)
      return false;

   //do not run before getAllScrAddrInDB
   auto&& currentmerkle = getAddressMapMerkle();
   BinaryData dbMerkle;

   {
      auto&& tx = lmdb_->beginTransaction(SSH, LMDB::ReadOnly);
      
      auto&& sdbi = getSshSDBI();

      dbMerkle = sdbi.metaHash_;
   }

   if (dbMerkle == currentmerkle)
      return false;

   //merkles don't match, check height in each address
   auto scraddrmap = scrAddrMap_->get();
   auto scanfrom = scraddrmap->begin()->second;
   for (const auto& scrAddr : *scraddrmap)
   {
      if (scanfrom != scrAddr.second)
         return true;
   }

   return false;
}
