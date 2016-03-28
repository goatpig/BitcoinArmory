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

#include "BDM_supportClasses.h"
#include "BlockUtils.h"
#include "txio.h"
#include <thread>


///////////////////////////////////////////////////////////////////////////////
//ScrAddrScanData Methods
///////////////////////////////////////////////////////////////////////////////

void ScrAddrFilter::getScrAddrCurrentSyncState()
{
   LMDBEnv::Transaction tx;
   lmdb_->beginDBTransaction(&tx, SSH, LMDB::ReadOnly);

   for (auto scrAddrPair : scrAddrMap_)
      getScrAddrCurrentSyncState(scrAddrPair.first);
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::getScrAddrCurrentSyncState(
   BinaryData const & scrAddr)
{
   //grab ssh for scrAddr
   StoredScriptHistory ssh;
   lmdb_->getStoredScriptHistorySummary(ssh, scrAddr);

   //update scrAddrData lowest scanned block
   setScrAddrLastScanned(scrAddr, ssh.alreadyScannedUpToBlk_);
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::setSSHLastScanned(uint32_t height)
{
   LOGWARN << "Updating ssh last scanned";
   LMDBEnv::Transaction tx;
   lmdb_->beginDBTransaction(&tx, SSH, LMDB::ReadWrite);
   for (const auto scrAddrPair : scrAddrMap_)
   {
      StoredScriptHistory ssh;
      lmdb_->getStoredScriptHistorySummary(ssh, scrAddrPair.first);
      if (!ssh.isInitialized())
         ssh.uniqueKey_ = scrAddrPair.first;

      ssh.alreadyScannedUpToBlk_ = height;

      lmdb_->putStoredScriptHistory(ssh);
   }
}

///////////////////////////////////////////////////////////////////////////////
bool ScrAddrFilter::registerAddresses(const vector<BinaryData>& saVec, 
   shared_ptr<BtcWallet> wlt, bool areNew)
{
   map<shared_ptr<BtcWallet>, vector<BinaryData>> wltNAddrMap;
   wltNAddrMap.insert(make_pair(wlt, saVec));

   return registerAddressBatch(wltNAddrMap, areNew);
}


///////////////////////////////////////////////////////////////////////////////
bool ScrAddrFilter::registerAddressBatch(
   const map<shared_ptr<BtcWallet>, vector<BinaryData>>& wltNAddrMap, 
   bool areNew)
{
   /***
   Gets a scrAddr ready for loading. Returns false if the BDM is initialized,
   in which case wltPtr will be called back with the address once it is ready

   doScan: 
      1: don't scan, new addresses
      0: scan while taking count of the existing history
     -1: wipe existing history then scan
   ***/

   //check if the BDM is initialized. There ought to be a better way than
   //checking the top block
   if (bdmIsRunning())
   {
      //BDM is initialized and maintenance thread is running, check mode
      if (armoryDbType_ == ARMORY_DB_SUPER)
      {
         //supernode: nothing to do, signal the wallet that its scrAddr bulk 
         //is ready by passing isNew as true. Pass a blank BinaryData for the 
         //top scanned block hash in this case, it will be ignored anyways      
         
         {
            unique_lock<mutex> lock(mergeLock_);
            for (auto& batch : wltNAddrMap)
            {
               for (auto& sa : batch.second)
                  scrAddrDataForSideScan_.scrAddrsToMerge_.insert({ sa, 0 });
               mergeFlag_ = true;
            }
         }

         for (auto& batch : wltNAddrMap)
         {
            batch.first->prepareScrAddrForMerge(
               batch.second, true, BinaryData());

            batch.first->needsRefresh();
         }

         return false;
      }

      //check DB for the scrAddr's ssh
      StoredScriptHistory ssh;
         
      ScrAddrFilter* topChild = this;
      while (topChild->child_)
         topChild = topChild->child_.get();

      topChild->child_ = shared_ptr<ScrAddrFilter>(copy());
      ScrAddrFilter* sca = topChild->child_.get();

      sca->setRoot(this);
        
      if (!areNew)
      {
         //mark existing history for wipe and rescan from block 0
         sca->doScan_ = true;

         for (auto& batch : wltNAddrMap)
         {
            for (const auto& scrAddr : batch.second)
               sca->regScrAddrForScan(scrAddr, 0);
         }
      }
      else
      {
         //mark addresses as fresh to skip DB scan
         sca->doScan_ = false;
         for (auto& batch : wltNAddrMap)
         {
            for (const auto& scrAddr : batch.second)
               sca->regScrAddrForScan(scrAddr, 0);
         }
      }

      sca->buildSideScanData(wltNAddrMap);
      flagForScanThread();

      return false;
   }
   else
   {
      //BDM isnt initialized yet, the maintenance thread isnt running, 
      //just register the scrAddr and return true.
      for (auto& batch : wltNAddrMap)
      {
         for (const auto& scrAddr : batch.second)
            scrAddrMap_.insert(make_pair(scrAddr, 0));
      }

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
      LMDBEnv::Transaction tx;
      lmdb_->beginDBTransaction(&tx, HEADERS, LMDB::ReadOnly);
      StoredHeader sbh;
      lmdb_->getBareHeader(sbh, endBlock);
      topScannedBlockHash = sbh.thisHash_;
   }

   if(doScan_ == false)
   {
      //new addresses, set their last seen block in the ssh entries
      setSSHLastScanned(currentTopBlockHeight());
   }
   else
   {
      //wipe ssh
      vector<BinaryData> saVec;
      for (const auto& scrAddrPair : scrAddrMap_)
         saVec.push_back(scrAddrPair.first);
      wipeScrAddrsSSH(saVec);
      saVec.clear();

      //scan from 0
      topScannedBlockHash =
         applyBlockRangeToDB(0, endBlock, wltIDs);
   }

   for (auto& batch : scrAddrDataForSideScan_.wltNAddrMap_)
   {
      if (batch.first->hasBdvPtr())
      {
         //merge with main ScrAddrScanData object
         merge(topScannedBlockHash);

         vector<BinaryData> addressVec;
         addressVec.reserve(scrAddrMap_.size());

         //notify the wallets that the scrAddr are ready
         for (auto& scrAddrPair : scrAddrMap_)
         {
            addressVec.push_back(scrAddrPair.first);
         }

         if (!scrAddrMap_.empty())
         {
            batch.first->prepareScrAddrForMerge(addressVec, !((bool)doScan_),
               topScannedBlockHash);

            //notify the bdv that it needs to refresh through the wallet
            batch.first->needsRefresh();
         }
      }
   }

   //clean up
   if (root_ != nullptr)
   {
      ScrAddrFilter* root = root_;
      shared_ptr<ScrAddrFilter> newChild = child_;
      root->child_ = newChild;

      root->isScanning_ = false;

      if (root->child_)
         root->flagForScanThread();
   }

   for (const auto& wID : wltIDs)
      LOGINFO << "Done with side scan of wallet " << wID;
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::scanScrAddrMapInNewThread()
{
   auto scanMethod = [this](void)->void
   { this->scanScrAddrThread(); };

   thread scanThread(scanMethod);
   scanThread.detach();
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::merge(const BinaryData& lastScannedBlkHash)
{
   /***
   Merge in the scrAddrMap and UTxOs scanned in a side thread with the BDM's
   main ScrAddrScanData
   ***/

   if (root_)
   {
      unique_lock<mutex> lock(root_->mergeLock_);

      //merge scrAddrMap_
      root_->scrAddrDataForSideScan_.lastScannedBlkHash_ = lastScannedBlkHash;
      root_->scrAddrDataForSideScan_.scrAddrsToMerge_.insert(
         scrAddrMap_.begin(), scrAddrMap_.end());

      //set mergeFlag
      root_->mergeFlag_ = true;
   }
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::checkForMerge()
{
   if (mergeFlag_ == true)
   {
      /***
      We're about to add a set of newly registered scrAddrs to the BDM's
      ScrAddrFilter map. Make sure they are scanned up to the last known
      top block first, then merge it in.
      ***/

      //create SAF to scan the addresses to merge
      std::shared_ptr<ScrAddrFilter> sca(copy());
      for (auto& scraddr : scrAddrDataForSideScan_.scrAddrsToMerge_)
         sca->scrAddrMap_.insert(scraddr);

      if (config().armoryDbType != ARMORY_DB_SUPER)
      {
         BinaryData lastScannedBlockHash = scrAddrDataForSideScan_.lastScannedBlkHash_;

         uint32_t topBlock = currentTopBlockHeight();
         uint32_t startBlock;

         //check last scanned blk hash against the blockchain      
         Blockchain& bc = blockchain();
         const BlockHeader& bh = bc.getHeaderByHash(lastScannedBlockHash);

         if (bh.isMainBranch())
         {
            //last scanned block is still on main branch
            startBlock = bh.getBlockHeight() + 1;
         }
         else
         {
            throw runtime_error("needs reimplemented");

            //last scanned block is off the main branch, undo till branch point
            const Blockchain::ReorganizationState state =
               bc.findReorgPointFromBlock(lastScannedBlockHash);

            startBlock = state.reorgBranchPoint->getBlockHeight() + 1;
         }

         if (startBlock < topBlock)
            sca->applyBlockRangeToDB(startBlock, topBlock + 1, vector<string>());
      }

      //grab merge lock
      unique_lock<mutex> lock(mergeLock_);

      scrAddrMap_.insert(sca->scrAddrMap_.begin(), sca->scrAddrMap_.end());
      scrAddrDataForSideScan_.scrAddrsToMerge_.clear();

      mergeFlag_ = false;
   }
}

///////////////////////////////////////////////////////////////////////////////
uint32_t ScrAddrFilter::scanFrom() const
{
   uint32_t lowestBlock = 0;

   if (scrAddrMap_.size())
   {
      lowestBlock = scrAddrMap_.begin()->second;

      for (auto scrAddr : scrAddrMap_)
      {
         if (lowestBlock != scrAddr.second)
         {
            lowestBlock = 0;
            break;
         }
      }
   }

   if (lowestBlock != 0)
      lowestBlock++;

   return lowestBlock;
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::clear()
{
   checkForMerge();

   for (auto& regScrAddr : scrAddrMap_)
      regScrAddr.second = 0;
}

///////////////////////////////////////////////////////////////////////////////
bool ScrAddrFilter::startSideScan(
   function<void(const vector<string>&, double prog, unsigned time)> progress)
{
   ScrAddrFilter* sca = child_.get();

   if (sca != nullptr && !isScanning_)
   {
      isScanning_ = true;
      sca->scanThreadProgressCallback_ = progress;
      sca->scanScrAddrMapInNewThread();

      if (sca->doScan_ != false)
         return true;
   }

   return false;
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::buildSideScanData(
   const map<shared_ptr<BtcWallet>, vector<BinaryData>>& wltNAddrMap)
{
   scrAddrDataForSideScan_.startScanFrom_ = UINT32_MAX;
   for (const auto& scrAddrPair : scrAddrMap_)
      scrAddrDataForSideScan_.startScanFrom_ = 
      min(scrAddrDataForSideScan_.startScanFrom_, scrAddrPair.second);

   scrAddrDataForSideScan_.wltNAddrMap_ = wltNAddrMap;
}

///////////////////////////////////////////////////////////////////////////////
void ScrAddrFilter::getAllScrAddrInDB()
{
   unique_lock<mutex> lock(mergeLock_);

   LMDBEnv::Transaction tx;
   lmdb_->beginDBTransaction(&tx, SSH, LMDB::ReadOnly);
   auto dbIter = lmdb_->getIterator(SSH);   

   //iterate over ssh DB
   while(dbIter.advanceAndRead(DB_PREFIX_SCRIPT))
   {
      auto keyRef = dbIter.getKeyRef();
      StoredScriptHistory ssh;
      ssh.unserializeDBKey(dbIter.getKeyRef());
      ssh.unserializeDBValue(dbIter.getValueRef());

      scrAddrMap_[ssh.uniqueKey_] = 0;
   } 

   for (auto scrAddrPair : scrAddrMap_)
      getScrAddrCurrentSyncState(scrAddrPair.first);
}

///////////////////////////////////////////////////////////////////////////////
BinaryData ScrAddrFilter::getAddressMapMerkle(void) const
{
   vector<BinaryData> addrVec;
   addrVec.reserve(scrAddrMap_.size());
   for (auto& addrPair : scrAddrMap_)
      addrVec.push_back(addrPair.first);

   return BtcUtils::calculateMerkleRoot(addrVec);
}

///////////////////////////////////////////////////////////////////////////////
bool ScrAddrFilter::hasNewAddresses(void) const
{
   if (scrAddrMap_.size() == 0)
      return false;

   //do not run before getAllScrAddrInDB
   auto&& currentmerkle = getAddressMapMerkle();
   BinaryData dbMerkle;

   {
      LMDBEnv::Transaction tx;
      lmdb_->beginDBTransaction(&tx, SSH, LMDB::ReadOnly);
      
      StoredDBInfo sdbi;
      lmdb_->getStoredDBInfo(SSH, sdbi);

      dbMerkle = sdbi.metaHash_;
   }

   if (dbMerkle == currentmerkle)
      return false;

   //merkles don't match, check height in each address
   auto scanfrom = scrAddrMap_.begin()->second;
   for (auto& scrAddrPair : scrAddrMap_)
   {
      if (scanfrom != scrAddrPair.second)
         return true;
   }

   return false;
}

///////////////////////////////////////////////////////////////////////////////
const vector<string> ScrAddrFilter::getNextWalletIDToScan(void)
{
   if (child_.get() != nullptr)
      return child_->scrAddrDataForSideScan_.getWalletIDString();
   
   return vector<string>();
}

///////////////////////////////////////////////////////////////////////////////
//ZeroConfContainer Methods
///////////////////////////////////////////////////////////////////////////////
map<BinaryData, TxIOPair> ZeroConfContainer::emptyTxioMap_;

BinaryData ZeroConfContainer::getNewZCkey()
{
   uint32_t newId = topId_.fetch_add(1, memory_order_relaxed);
   BinaryData newKey = READHEX("ffff");
   newKey.append(WRITE_UINT32_BE(newId));

   return newKey;
}

///////////////////////////////////////////////////////////////////////////////
Tx ZeroConfContainer::getTxByHash(const BinaryData& txHash) const
{
   Tx rt;
   const auto keyIter = txHashToDBKey_.find(txHash);

   if (keyIter == txHashToDBKey_.end())
      return rt;

   return txMap_.find(keyIter->second)->second;
}
///////////////////////////////////////////////////////////////////////////////
bool ZeroConfContainer::hasTxByHash(const BinaryData& txHash) const
{
   return (txHashToDBKey_.find(txHash) != txHashToDBKey_.end());
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::addRawTx(const BinaryData& rawTx, uint32_t txtime)
{
   /***
   Saves new ZC by txtime. txtime will always be unique, as it is grabbed
   locally and the protocol enforces a limit of 7 Tx per seconds, guaranteeing
   sufficient time granularity.
   ***/

   if (enabled_ == false)
      return;

   //convert raw ZC to a Tx object
   BinaryData ZCkey = getNewZCkey();
   Tx zcTx(rawTx);
   zcTx.setTxTime(txtime);

   unique_lock<mutex> lock(mu_);
   newZCMap_[ZCkey] = zcTx;
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::purge(function<bool(const BinaryData&)> filter)
{
   map<BinaryData, vector<BinaryData>> invalidatedKeys;

   if (!db_)
      return;

   /***
   For ZC chains to be parsed properly, it is important ZC transactions are
   parsed in the order they appeared.
   ***/
   SCOPED_TIMER("purgeZeroConfPool");

   //keep a copy of old containers
   auto oldtxHashToDBKey = txHashToDBKey_;
   {
      unique_lock<mutex> lock(mu_);
      newZCMap_.insert(txMap_.begin(), txMap_.end());
   }

   LMDBEnv::Transaction tx;
   db_->beginDBTransaction(&tx, ZERO_CONF, LMDB::ReadOnly);

   //get all txhashes for the new blocks
   set<BinaryData> minedHashes;
   auto bcPtr = db_->blockchain();
   try
   {
      const BlockHeader* lastKnownHeader =
         &bcPtr->getHeaderByHash(lastParsedBlockHash_);

      while (!lastKnownHeader->isMainBranch())
      {
         //trace back to the branch point
         auto&& bhash = lastKnownHeader->getPrevHash();
         lastKnownHeader = &bcPtr->getHeaderByHash(bhash);
      }

      //get the next header
      auto height = lastKnownHeader->getBlockHeight() + 1;
      lastKnownHeader = &bcPtr->getHeaderByHeight(height);

      while (lastKnownHeader != nullptr)
      {
         //grab block
         StoredHeader sbh;
         db_->getStoredHeader(sbh,
            lastKnownHeader->getBlockHeight(),
            lastKnownHeader->getDuplicateID(),
            false);

         //build up hash set
         for (auto& stx : sbh.stxMap_)
            minedHashes.insert(stx.second.thisHash_);

         //next block
         auto& bhash = lastKnownHeader->getNextHash();
         lastKnownHeader = &bcPtr->getHeaderByHash(bhash);
      }
   }
   catch (...)
   {
   }

   vector<BinaryData> keysToWrite, keysToDelete;

   //compare minedHashes to allZCTxHashes_, mark keys for deletion
   for (auto& minedHash : minedHashes)
   {
      auto iter = allZcTxHashes_.find(minedHash);
      if (iter != allZcTxHashes_.end())
      {
         //if this is a ZC we own, remove it from newZCMap_ too
         auto hashIter = txHashToDBKey_.find(*iter);
         if (hashIter != txHashToDBKey_.end())
            newZCMap_.erase(hashIter->second);
         
         keysToDelete.push_back(*iter);
         allZcTxHashes_.erase(iter);
      }
   }

   //reset containers
   txHashToDBKey_.clear();
   txMap_.clear();
   txioMap_.clear();
   keyToSpentScrAddr_.clear();
   txOutsSpentByZC_.clear();
   outPointsSpentByKey_.clear();

   //parse all ZC anew
   parseNewZC(filter);

   //build the set of invalidated zc dbKeys and delete them from db
   for (auto& txhash : oldtxHashToDBKey)
   {
      auto txIter = txHashToDBKey_.find(txhash.first);
      if (txIter == txHashToDBKey_.end())
         keysToDelete.push_back(txhash.second);
   }

   auto delFromDB = [&, this](void)->void
   { this->updateZCinDB(keysToWrite, keysToDelete); };

   //run in dedicated thread to make sure we can get a RW tx
   thread delFromDBthread(delFromDB);
   delFromDBthread.join();
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::dropZC(const set<BinaryData>& txHashes)
{
   vector<BinaryData> keysToDelete;

   for (auto& hash : txHashes)
   {
      //resolve zcKey
      auto hashIter = txHashToDBKey_.find(hash);
      if (hashIter == txHashToDBKey_.end())
         continue;

      auto zcKey = hashIter->second;
      txHashToDBKey_.erase(hashIter);

      //drop from txMap_
      txMap_.erase(zcKey);

      //drop from keyToSpendScrAddr_
      auto&& scrAddrVec = keyToSpentScrAddr_[zcKey];
      keyToSpentScrAddr_.erase(zcKey);

      //drop from txioMap_
      for (auto& sa : scrAddrVec)
      {
         auto mapIter = txioMap_.find(sa);
         if (mapIter == txioMap_.end())
            continue;

         auto& txiomap = mapIter->second;

         auto txioIter = txiomap.begin();
         while (txioIter != txiomap.end())
         {
            if (txioIter->first.startsWith(sa))
               txiomap.erase(txioIter++);
            else
               ++txioIter;
         }
      }

      //drop from txOutsSpentByZC_
      auto txOutIter = txOutsSpentByZC_.begin();
      while (txOutIter != txOutsSpentByZC_.end())
      {
         if ((*txOutIter).startsWith(zcKey))
            txOutsSpentByZC_.erase(txOutIter++);
         else
            ++txOutIter;
      }

      //mark for deletion
      keysToDelete.push_back(zcKey);
   }

   //delete keys from DB
   auto deleteKeys = [&](void)->void
   {
      this->updateZCinDB(vector<BinaryData>(), keysToDelete);
   };

   thread deleteKeyThread(deleteKeys);
   if (deleteKeyThread.joinable())
      deleteKeyThread.join();
}

///////////////////////////////////////////////////////////////////////////////
set<BinaryData> ZeroConfContainer::parseNewZC(
   function<bool(const BinaryData&)> filter, bool updateDb)
{
   /***
   ZC transcations are pushed to the BDM by another thread (usually the thread
   managing network connections). This is processed by addRawTx, which is meant
   to return fast. It grabs the container lock, inserts the new Tx object in
   the newZCMap_ and return, and sets the new ZC flag.

   The BDM main thread checks the ZC flag and calls this method. This method
   processes all new ZC and clears the newZCMap_. It checks how many ZC have
   been processed against the newZCMap_ size to make sure it can clear the map
   without deleting any new ZC that may have been added during the process.

   Note: there is no concurency interference with purging the container
   (for reorgs and new blocks), as both methods are called by the BDM main thread.
   ***/
   uint32_t nProcessed = 0;

   unique_lock<mutex> lock(mu_);

   //copy new ZC map
   map<BinaryData, Tx> zcMap = newZCMap_;

   set<BinaryData> newZcByHash;

   lock.unlock();

   while (1)
   {
      vector<BinaryData> keysToWrite, keysToDelete;

      for (auto& newZCPair : zcMap)
      {
         const BinaryData&& txHash = newZCPair.second.getThisHash();
         auto insertIter = allZcTxHashes_.insert(txHash);
         if(insertIter.second)
            keysToWrite.push_back(newZCPair.first);
      }

      for (auto& newZCPair : zcMap)
      {
         const BinaryData&& txHash = newZCPair.second.getThisHash();
         if (txHashToDBKey_.find(txHash) != txHashToDBKey_.end())
            continue; //already have this ZC
         
         //flag RBF on whole tx
         auto& zctx = newZCPair.second;
         zctx.setRBF(false);
         auto datacopy = zctx.getPtr();
         unsigned txinCount = zctx.getNumTxIn();
         
         for (unsigned i = 0; i < txinCount; i++)
         {
            BinaryDataRef consumedHash(datacopy + zctx.getTxInOffset(i), 32);
            auto hashiter = allZcTxHashes_.find(consumedHash);
            if (hashiter != allZcTxHashes_.end())
            {
               //a ZC spending a ZC output is marked as replaceable regardless
               //of sequence flagging
               zctx.setRBF(true);
               break;
            }
         }

         //process ZC
         nProcessed++;

         {
            auto&& bulkData =
               ZCisMineBulkFilter(newZCPair.second,
                  newZCPair.first,
                  newZCPair.second.getTxTime(),
                  filter);

            //check for replacement
            {
               //loop through all outpoints consumed by this ZC
               set<BinaryData> replacedHashes;
               for (auto& idSet : bulkData.outPointsSpentByKey_)
               {
                  //compare them to the list of currently spent outpoints
                  auto hashIter = outPointsSpentByKey_.find(idSet.first);
                  if (hashIter == outPointsSpentByKey_.end())
                     continue;

                  for (auto opId : idSet.second)
                  {
                     auto idIter = hashIter->second.find(opId.first);
                     if (idIter != hashIter->second.end())
                     {
                        //if 2 outpoints match, this ZC is replacing another
                        //flag the replaced key and clean up the entry
                        replacedHashes.insert(idSet.first);
                        hashIter->second.erase(idIter);
                     }
                  }
               }

               //drop the replacedKeys if any
               if (replacedHashes.size() > 0)
                  dropZC(replacedHashes);
            }

            //add ZC if its relevant
            if (!bulkData.isEmpty())
            {
               //merge spent outpoints
               txOutsSpentByZC_.insert(
                  bulkData.txOutsSpentByZC_.begin(), 
                  bulkData.txOutsSpentByZC_.end());

               for (auto& idmap : bulkData.outPointsSpentByKey_)
               {
                  //cant use insert, have to replace values if they already exist
                  auto& thisIdMap = outPointsSpentByKey_[idmap.first];
                  for (auto& idpair : idmap.second)
                     thisIdMap[idpair.first] = idpair.second;
               }

               //merge new txios
               txHashToDBKey_[txHash] = newZCPair.first;
               txMap_[newZCPair.first] = newZCPair.second;
               
               for (const auto& saTxio : bulkData.scrAddrTxioMap_)
               {
                  //again, can't use insert, have to overwrite existing data
                  auto& txioPair = txioMap_[saTxio.first];
                  for (auto txio : saTxio.second)
                     txioPair[txio.first] = txio.second;
               }

               newZcByHash.insert(txHash);
            }
         }
      }

      if (updateDb)
      {
         //write ZC in the new thread to guaranty we can get a RW tx
         auto writeNewZC = [&, this](void)->void
         { this->updateZCinDB(keysToWrite, keysToDelete); };

         thread writeNewZCthread(writeNewZC);
         writeNewZCthread.join();
      }

      unique_lock<mutex> loopLock(mu_);

      //check if newZCMap_ doesnt have new Txn
      if (nProcessed >= newZCMap_.size())
      {
         //clear map
         newZCMap_.clear();

         //break out of the loop
         break;
      }

      //else search the new ZC container for unseen ZC
      auto newZcIter = newZCMap_.begin();

      while (newZcIter != newZCMap_.begin())
      {
         if (ITER_IN_MAP(zcMap.find(newZcIter->first), zcMap))
            newZCMap_.erase(newZcIter++);
         else
            ++newZcIter;
      }

      zcMap = newZCMap_;

      //reset counter
      nProcessed = 0;
   }

   lastParsedBlockHash_ = db_->getTopBlockHash();

   return newZcByHash;
}

///////////////////////////////////////////////////////////////////////////////
bool ZeroConfContainer::getKeyForTxHash(const BinaryData& txHash,
   BinaryData& zcKey) const
{
   const auto& hashPair = txHashToDBKey_.find(txHash);
   if (hashPair != txHashToDBKey_.end())
   {
      zcKey = hashPair->second;
      return true;
   }
   return false;
}

///////////////////////////////////////////////////////////////////////////////
ZeroConfContainer::BulkFilterData 
ZeroConfContainer::ZCisMineBulkFilter(const Tx & tx,
   const BinaryData & ZCkey, uint32_t txtime, 
   function<bool(const BinaryData&)> filter, 
   bool withSecondOrderMultisig)
{
   // Since 99.999%+ of all transactions are not ours, let's do the 
   // fastest bulk filter possible, even though it will add 
   // redundant computation to the tx that are ours.  In fact,
   // we will skip the TxIn/TxOut convenience methods and follow the
   // pointers directly to the data we want

   /***filter is a pointer to a function that takes in a scrAddr (21 bytes,
   including the prefix) and returns a bool. For supernode, it should return
   true all the time.
   ***/

   BinaryData txHash = tx.getThisHash();
   TxRef txref = db_->getTxRef(txHash);

   ZeroConfContainer::BulkFilterData bulkData;

   if (txref.isInitialized())
   {
      //Found this tx in the db. It is already part of a block thus 
      //is invalid as a ZC
      return bulkData;
   }

   bool isRBF = tx.isRBF();

   uint8_t const * txStartPtr = tx.getPtr();
   for (uint32_t iin = 0; iin<tx.getNumTxIn(); iin++)
   {
      OutPoint op;
      op.unserialize(txStartPtr + tx.getTxInOffset(iin), 36);

      //keep track of all outputs this ZC consumes
      auto& idSet = bulkData.outPointsSpentByKey_[op.getTxHash()];
      idSet.insert(make_pair(op.getTxOutIndex(), ZCkey));

      //check ZC txhash first, always cheaper than grabing a stxo from DB,
      //and will always be checked if the tx doesn't hit in DB outpoints.
      BinaryData opZcKey;
      if (getKeyForTxHash(op.getTxHash(), opZcKey))
      {
         TxRef outPointRef(opZcKey);
         uint16_t outPointId = op.getTxOutIndex();
         TxIOPair txio(outPointRef, outPointId,
            TxRef(ZCkey), iin);

         Tx chainedZC = getTxByHash(op.getTxHash());

         const TxOut& chainedTxOut = chainedZC.getTxOutCopy(outPointId);

         txio.setTxHashOfOutput(op.getTxHash());
         txio.setTxHashOfInput(txHash);

         txio.setValue(chainedTxOut.getValue());
         txio.setTxTime(txtime);
         txio.setRBF(chainedZC.isRBF());

         BinaryData spentSA = chainedTxOut.getScrAddressStr();
         auto& key_txioPair = bulkData.scrAddrTxioMap_[spentSA];
         key_txioPair[txio.getDBKeyOfOutput()] = txio;

         bulkData.txOutsSpentByZC_.insert(txio.getDBKeyOfOutput());

         auto& wltIdVec = keyToSpentScrAddr_[ZCkey];
         wltIdVec.push_back(spentSA);

         continue;
      }


      //fetch the TxOut from DB
      BinaryData opKey = op.getDBkey(db_);
      if (opKey.getSize() == 8)
      {
         //found outPoint DBKey, grab the StoredTxOut
         StoredTxOut stxOut;
         if (db_->getStoredTxOut(stxOut, opKey))
         {
            if (stxOut.isSpent())
            {
               //invalid ZC, dump it
               return ZeroConfContainer::BulkFilterData();
            }

            BinaryData sa = stxOut.getScrAddress();
            if (filter(sa))
            {
               TxIOPair txio(
                  TxRef(opKey.getSliceRef(0, 6)), op.getTxOutIndex(),
                  TxRef(ZCkey), iin);

               txio.setTxHashOfOutput(op.getTxHash());
               txio.setTxHashOfInput(txHash);
               txio.setValue(stxOut.getValue());
               txio.setTxTime(txtime);
               txio.setRBF(isRBF);

               auto& key_txioPair = bulkData.scrAddrTxioMap_[sa];
               key_txioPair[opKey] = txio;

               bulkData.txOutsSpentByZC_.insert(opKey);

               auto& wltIdVec = keyToSpentScrAddr_[ZCkey];
               wltIdVec.push_back(sa);
            }
         }
      }
   }

   // Simply convert the TxOut scripts to scrAddrs and check if registered
   for (uint32_t iout = 0; iout<tx.getNumTxOut(); iout++)
   {
      auto&& txout = tx.getTxOutCopy(iout);
      BinaryData scrAddr = txout.getScrAddressStr();
      if (filter(scrAddr))
      {
         TxIOPair txio(TxRef(ZCkey), iout);

         txio.setValue(txout.getValue());
         txio.setTxHashOfOutput(txHash);
         txio.setTxTime(txtime);
         txio.setUTXO(true);
         txio.setRBF(isRBF);

         auto& key_txioPair = bulkData.scrAddrTxioMap_[scrAddr];

         key_txioPair[txio.getDBKeyOfOutput()] = txio;
      }
   }

   // If we got here, it's either non std or not ours
   return bulkData;
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::clear()
{
   txHashToDBKey_.clear();
   txMap_.clear();
   txioMap_.clear();
   newZCMap_.clear();
}

///////////////////////////////////////////////////////////////////////////////
bool ZeroConfContainer::isTxOutSpentByZC(const BinaryData& dbkey) 
   const
{
   if (txOutsSpentByZC_.find(dbkey) != txOutsSpentByZC_.end())
      return true;

   return false;
}

///////////////////////////////////////////////////////////////////////////////
const map<BinaryData, TxIOPair> ZeroConfContainer::getZCforScrAddr(
   BinaryData scrAddr) const
{
   auto saIter = txioMap_.find(scrAddr);

   if (ITER_IN_MAP(saIter, txioMap_))
   {
      auto& zcMap = saIter->second;
      map<BinaryData, TxIOPair> returnMap;

      for (auto& zcPair : zcMap)
      {
         if (isTxOutSpentByZC(zcPair.second.getDBKeyOfOutput()))
            continue;

         returnMap.insert(zcPair);
      }

      return returnMap;
   }

   return emptyTxioMap_;
}

///////////////////////////////////////////////////////////////////////////////
const vector<BinaryData>& ZeroConfContainer::getSpentSAforZCKey(
   const BinaryData& zcKey) const
{
   auto iter = keyToSpentScrAddr_.find(zcKey);
   if (iter == keyToSpentScrAddr_.end())
      return emptyVecBinData_;

   return iter->second;
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::updateZCinDB(const vector<BinaryData>& keysToWrite, 
   const vector<BinaryData>& keysToDelete)
{
   //TODO: write all ZC data in dedicated ZC db

   //should run in its own thread to make sure we can get a write tx
   DB_SELECT dbs = ZERO_CONF;

   LMDBEnv::Transaction tx;
   db_->beginDBTransaction(&tx, dbs, LMDB::ReadWrite);

   for (auto& key : keysToWrite)
   {
      auto iter = txMap_.find(key);
      if (iter != txMap_.end())
      {
         StoredTx zcTx;
         zcTx.createFromTx(txMap_[key], true, true);
         db_->putStoredZC(zcTx, key);
      }
      else
      {
         //if the key is not to be found in the txMap_, this is a ZC txhash
         db_->putValue(ZERO_CONF, key, BinaryData());
      }
   }

   for (auto& key : keysToDelete)
   {
      BinaryData keyWithPrefix;
      if (key.getSize() == 6)
      {
         keyWithPrefix.resize(7);
         uint8_t* keyptr = keyWithPrefix.getPtr();
         keyptr[0] = DB_PREFIX_ZCDATA;
         memcpy(keyptr + 1, key.getPtr(), 6);
      }
      else
         keyWithPrefix = key;

      LDBIter dbIter(db_->getIterator(dbs));

      if (!dbIter.seekTo(keyWithPrefix))
         continue;

      vector<BinaryData> ktd;

      do
      {
         BinaryDataRef thisKey = dbIter.getKeyRef();
         if (!thisKey.startsWith(keyWithPrefix))
            break;

         ktd.push_back(thisKey);
      } 
      while (dbIter.advanceAndRead(DB_PREFIX_ZCDATA));

      for (auto Key : ktd)
         db_->deleteValue(dbs, Key);
   }
}

///////////////////////////////////////////////////////////////////////////////
void ZeroConfContainer::loadZeroConfMempool(
   function<bool(const BinaryData&)> filter,
   bool clearMempool)
{
   //run this in its own scope so the iter and tx are closed in order to open
   //RW tx afterwards
   {
      auto dbs = ZERO_CONF;

      LMDBEnv::Transaction tx;
      db_->beginDBTransaction(&tx, dbs, LMDB::ReadOnly);
      LDBIter dbIter(db_->getIterator(dbs));

      if (!dbIter.seekToStartsWith(DB_PREFIX_ZCDATA))
      {
         enabled_ = true;
         return;
      }

      do
      {
         BinaryDataRef zcKey = dbIter.getKeyRef();

         if (zcKey.getSize() == 7)
         {
            //Tx, grab it from DB
            StoredTx zcStx;
            db_->getStoredZcTx(zcStx, zcKey);

            //add to newZCMap_
            Tx& zcTx = newZCMap_[zcKey.getSliceCopy(1, 6)];
            zcTx = Tx(zcStx.getSerializedTx());
            zcTx.setTxTime(zcStx.unixTime_);
         }
         else if (zcKey.getSize() == 9)
         {
            //TxOut, ignore it
            continue;
         }
         else if (zcKey.getSize() == 32)
         {
            //tx hash
            allZcTxHashes_.insert(zcKey);
         }
         else
         {
            //shouldn't hit this
            LOGERR << "Unknown key found in ZC mempool";
            break;
         }
      } while (dbIter.advanceAndRead(DB_PREFIX_ZCDATA));
   }

   if (clearMempool == true)
   {
      vector<BinaryData> keysToWrite, keysToDelete;

      for (const auto& zcTx : newZCMap_)
         keysToDelete.push_back(zcTx.first);

      newZCMap_.clear();
      updateZCinDB(keysToWrite, keysToDelete);
   }
   else if (newZCMap_.size())
   {   

      //copy newZCmap_ to keep the pre parse ZC map
      auto oldZCMap = newZCMap_;

      //now parse the new ZC
      parseNewZC(filter);
      
      //set the zckey to the highest used index
      if (txMap_.size() > 0)
      {
         BinaryData topZcKey = txMap_.rbegin()->first;
         topId_.store(READ_UINT32_BE(topZcKey.getSliceCopy(2, 4)) +1);
      }

      //intersect oldZCMap and txMap_ to figure out the invalidated ZCs
      vector<BinaryData> keysToWrite, keysToDelete;

      for (const auto& zcTx : oldZCMap)
      {
         if (txMap_.find(zcTx.first) == txMap_.end())
            keysToDelete.push_back(zcTx.first);
      }

      //no need to run this in a side thread, this code only runs when we have 
      //full control over the main thread
      updateZCinDB(keysToWrite, keysToDelete);
   }

   enabled_ = true;
}


// kate: indent-width 3; replace-tabs on;
