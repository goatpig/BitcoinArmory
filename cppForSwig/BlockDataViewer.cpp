////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2019, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#include "BlockDataViewer.h"

using namespace std;

/////////////////////////////////////////////////////////////////////////////
BlockDataViewer::BlockDataViewer(BlockDataManager* bdm) :
   rescanZC_(false), zeroConfCont_(bdm->zeroConfCont())
{
   db_ = bdm->getIFace();
   bc_ = bdm->blockchain();
   saf_ = bdm->getScrAddrFilter().get();
   zc_ = bdm->zeroConfCont().get();

   bdmPtr_ = bdm;

   groups_.push_back(WalletGroup(this, saf_));
   groups_.push_back(WalletGroup(this, saf_));

   flagRescanZC(false);
}

/////////////////////////////////////////////////////////////////////////////
BlockDataViewer::~BlockDataViewer()
{
   groups_.clear();
}

/////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::registerAWallet(WalletRegistrationRequest& request)
{
   switch (request.type)
   {
      case WalletRegType::WALLET:
         groups_[group_wallet].registerAddresses(request);
         break;

      case WalletRegType::LOCKBOX:
         groups_[group_lockbox].registerAddresses(request);
         break;

      default:
         LOGWARN << "invalid wallet registration group!";
   }
}

/////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::unregisterWallet(const string& walletID)
{
   for (auto& group : groups_) {
      if (group.hasID(walletID)) {
         group.unregisterWallet(walletID);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::scanWallets(shared_ptr<BDV_Notification> action)
{
   uint32_t startBlock = UINT32_MAX;
   uint32_t endBlock = UINT32_MAX;
   uint32_t prevTopBlock = UINT32_MAX;

   bool reorg = false;
   bool refresh = false;

   ScanWalletStruct scanData;
   vector<LedgerEntry>* leVecPtr = nullptr;

   switch (action->action_type())
   {
   case BDV_Init:
   {
      prevTopBlock = startBlock = 0;
      endBlock = blockchain().top()->getBlockHeight();
      refresh = true;
      break;
   }

   case BDV_NewBlock:
   {
      auto reorgNotif =
         dynamic_pointer_cast<BDV_Notification_NewBlock>(action);
      auto& reorgState = reorgNotif->reorgState_;
         
      if (!reorgState.hasNewTop_)
         return;
    
      if (!reorgState.prevTopStillValid_)
      {
         //reorg
         reorg = true;
         startBlock = reorgState.reorgBranchPoint_->getBlockHeight();
      }
      else
      {
         startBlock = reorgState.prevTop_->getBlockHeight();
      }
         
      endBlock = reorgState.newTop_->getBlockHeight();

      //set invalidated keys
      if (reorgNotif->zcPurgePacket_ != nullptr)
      {
         scanData.saStruct_.invalidatedZcKeys_ =
            &reorgNotif->zcPurgePacket_->invalidatedZcKeys_;

         //carry zc state
         scanData.saStruct_.zcState_ = reorgNotif->zcPurgePacket_->ssPtr_;
         scanData.saStruct_.scrAddrToTxioKeys_ = 
            reorgNotif->zcPurgePacket_->scrAddrToTxioKeys_;
      }

      prevTopBlock = reorgState.prevTop_->getBlockHeight() + 1;

      break;
   }
   
   case BDV_ZC:
   {
      auto zcAction = 
         dynamic_pointer_cast<BDV_Notification_ZC>(action);
      
      scanData.saStruct_.scrAddrToTxioKeys_ = 
         move(zcAction->packet_.scrAddrToTxioKeys_);

      scanData.saStruct_.zcState_ = zcAction->packet_.ssPtr_;

      scanData.saStruct_.newKeysAndScrAddr_ = 
         zcAction->packet_.newKeysAndScrAddr_;

      if (zcAction->packet_.purgePacket_ != nullptr)
      {
         scanData.saStruct_.invalidatedZcKeys_ =
            &zcAction->packet_.purgePacket_->invalidatedZcKeys_;
      }

      leVecPtr = &zcAction->leVec_;
      prevTopBlock = 
      startBlock = 
      endBlock = blockchain().top()->getBlockHeight();

      break;
   }

   case BDV_Refresh:
   {
      auto refreshNotif =
         dynamic_pointer_cast<BDV_Notification_Refresh>(action);

      if (refreshNotif->refresh_ == BDV_refreshSkipRescan)
      {
         //only flagged the wallet to send a refresh notification, do not
         //perform any other operations
         ++updateID_;
         return;
      }

      scanData.saStruct_.scrAddrToTxioKeys_ =
         move(refreshNotif->zcPacket_.scrAddrToTxioKeys_);

      scanData.saStruct_.zcState_ = refreshNotif->zcPacket_.ssPtr_;

      refresh = true;
      break;
   }

   default:
      return;
   }
   
   scanData.prevTopBlockHeight_ = prevTopBlock;
   scanData.endBlock_ = endBlock;
   scanData.action_ = action->action_type();
   scanData.reorg_ = reorg;

   vector<uint32_t> startBlocks;
   for (size_t i = 0; i < groups_.size(); i++)
      startBlocks.push_back(startBlock);

   auto sbIter = startBlocks.begin();
   for (auto& group : groups_)
   {
      if (group.pageHistory(refresh, false))
      {
         *sbIter = group.hist_.getPageBottom(0);
      }
         
      sbIter++;
   }

   //increment update id
   ++updateID_;

   sbIter = startBlocks.begin();
   for (auto& group : groups_)
   {
      scanData.startBlock_ = *sbIter;
      group.scanWallets(scanData, updateID_);

      sbIter++;
   }

   if (leVecPtr != nullptr)
   {
      for (auto& walletLedgerMap : scanData.saStruct_.zcLedgers_)
      {
         for(auto& lePair : walletLedgerMap.second)
            leVecPtr->push_back(lePair.second);
      }
   }

   lastScanned_ = endBlock;
}

////////////////////////////////////////////////////////////////////////////////
bool BlockDataViewer::hasWallet(const string& ID) const
{
   return groups_[group_wallet].hasID(ID);
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::registerAddresses(WalletRegistrationRequest& request)
{
   for (auto& group : groups_) {
      if (group.hasID(request.walletId)) {
         group.registerAddresses(request);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
Tx BlockDataViewer::getTxByHash(BinaryData const & txhash) const
{
   StoredTx stx;
   if (db_->getStoredTx_byHash(txhash, &stx))
   {
      auto tx = stx.getTxCopy();
      for (unsigned i=0; i<tx.getNumTxIn(); i++)
      {
         auto&& txin = tx.getTxInCopy(i);
         auto&& op = txin.getOutPoint();
         tx.pushBackOpId(db_->getHeightForTxHash(op.getTxHashRef()));
      }

      return tx;
   }
   else
      return zeroConfCont_->getTxByHash(txhash);
}

////////////////////////////////////////////////////////////////////////////////
tuple<uint32_t, uint32_t, vector<unsigned>> 
BlockDataViewer::getTxMetaData(
   const BinaryDataRef& txHash, bool withOpId) const
{
   unsigned txHeight = UINT32_MAX;
   unsigned txIndex = UINT32_MAX;
   vector<unsigned> opIds;

   auto dbKey = db_->getDBKeyForHash(txHash);
   switch (dbKey.getSize())
   {
   case 6:
   {
      BinaryRefReader brr(dbKey.getRef());
      brr.advance(4);
      txIndex = brr.get_uint16_t(BE);

      auto hgtx = dbKey.getSliceRef(0, 4);
      if (db_->getDbType() == ARMORY_DB_SUPER)
      {
         auto block_id = DBUtils::hgtxToHeight(hgtx);
         auto header = bc_->getHeaderById(block_id);
         txHeight = header->getBlockHeight();
      }
      else
      {
         txHeight = DBUtils::hgtxToHeight(hgtx);
      }

      //resolve outpoint heights too
      StoredTx stx;
      if (!db_->getStoredTx_byDBKey(stx, dbKey))
         throw runtime_error("missing tx");
      
      if (withOpId)
      {
         auto tx = stx.getTxCopy();
         for (unsigned i=0; i<tx.getNumTxIn(); i++)
         {
            auto&& txin = tx.getTxInCopy(i);
            auto&& op = txin.getOutPoint();
            opIds.push_back(db_->getHeightForTxHash(op.getTxHashRef()));
         }    
      }

      break;
   }
   case 0:
   {
      //possibly zc
      auto ss = zeroConfCont_->getSnapshot();
      auto keyRef = ss->getKeyForHash(txHash);
      if (keyRef.empty())
         break;

      BinaryRefReader brr(keyRef);
      brr.advance(2);
      txIndex = brr.get_uint32_t(BE);

      break;
   }

   default:
      throw runtime_error("unexpected db key size");
   }

   return make_tuple(txHeight, txIndex, move(opIds));
}


////////////////////////////////////////////////////////////////////////////////
TxOut BlockDataViewer::getPrevTxOut(TxIn & txin) const
{
   if (txin.isCoinbase())
      return TxOut();

   OutPoint op = txin.getOutPoint();
   Tx theTx = getTxByHash(op.getTxHash());
   if (!theTx.isInitialized())
      throw runtime_error("couldn't find prev tx");

   uint32_t idx = op.getTxOutIndex();
   return theTx.getTxOutCopy(idx);
}

////////////////////////////////////////////////////////////////////////////////
Tx BlockDataViewer::getPrevTx(TxIn & txin) const
{
   if (txin.isCoinbase())
      return Tx();

   OutPoint op = txin.getOutPoint();
   return getTxByHash(op.getTxHash());
}

////////////////////////////////////////////////////////////////////////////////
HashString BlockDataViewer::getSenderScrAddr(TxIn & txin) const
{
   if (txin.isCoinbase())
      return HashString(0);

   return getPrevTxOut(txin).getScrAddressStr();
}


////////////////////////////////////////////////////////////////////////////////
int64_t BlockDataViewer::getSentValue(TxIn & txin) const
{
   if (txin.isCoinbase())
      return -1;

   return getPrevTxOut(txin).getValue();

}

////////////////////////////////////////////////////////////////////////////////
LMDBBlockDatabase* BlockDataViewer::getDB(void) const
{
   return db_;
}

////////////////////////////////////////////////////////////////////////////////
uint32_t BlockDataViewer::getTopBlockHeight(void) const
{
   return bc_->top()->getBlockHeight();
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::reset()
{
   for (auto& group : groups_)
      group.reset();

   rescanZC_   = false;
   lastScanned_ = 0;
}

////////////////////////////////////////////////////////////////////////////////
size_t BlockDataViewer::getWalletsPageCount(void) const
{
   return groups_[group_wallet].getPageCount();
}

////////////////////////////////////////////////////////////////////////////////
vector<LedgerEntry> BlockDataViewer::getWalletsHistoryPage(uint32_t pageId,
   bool rebuildLedger, bool remapWallets)
{
   return groups_[group_wallet].getHistoryPage(pageId, 
      updateID_, rebuildLedger, remapWallets);
}

////////////////////////////////////////////////////////////////////////////////
size_t BlockDataViewer::getLockboxesPageCount(void) const
{
   return groups_[group_lockbox].getPageCount();
}

////////////////////////////////////////////////////////////////////////////////
vector<LedgerEntry> BlockDataViewer::getLockboxesHistoryPage(uint32_t pageId,
   bool rebuildLedger, bool remapWallets)
{
   return groups_[group_lockbox].getHistoryPage(pageId,
      updateID_, rebuildLedger, remapWallets);
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::updateWalletsLedgerFilter(
   const vector<string>& walletsList)
{
   groups_[group_wallet].updateLedgerFilter(walletsList);
}

////////////////////////////////////////////////////////////////////////////////
void BlockDataViewer::updateLockboxesLedgerFilter(
   const vector<string>& walletsList)
{
   groups_[group_lockbox].updateLedgerFilter(walletsList);
}

////////////////////////////////////////////////////////////////////////////////
StoredHeader BlockDataViewer::getMainBlockFromDB(uint32_t height) const
{
   uint8_t dupID = db_->getValidDupIDForHeight(height);
   
   return getBlockFromDB(height, dupID);
}

////////////////////////////////////////////////////////////////////////////////
StoredHeader BlockDataViewer::getBlockFromDB(
   uint32_t height, uint8_t dupID) const
{
   StoredHeader sbh;
   db_->getStoredHeader(sbh, height, dupID, true);

   return sbh;
}

////////////////////////////////////////////////////////////////////////////////
bool BlockDataViewer::scrAddressIsRegistered(const BinaryData& scrAddr) const
{
   auto scrAddrMap = saf_->getScanFilterAddrMap();
   auto saIter = scrAddrMap->find(scrAddr);

   if (saIter == scrAddrMap->end())
      return false;

   return true;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<BlockHeader> BlockDataViewer::getHeaderByHash(
   const BinaryData& blockHash) const
{
   return bc_->getHeaderByHash(blockHash);
}

////////////////////////////////////////////////////////////////////////////////
WalletGroup BlockDataViewer::getStandAloneWalletGroup(
   const vector<string>& wltIDs, HistoryOrdering order)
{
   WalletGroup wg(this, this->saf_);
   wg.order_ = order;

   auto wallets   = groups_[group_wallet].getWalletMap();
   auto lockboxes = groups_[group_lockbox].getWalletMap();

   for (const auto& wltid : wltIDs)
   {
      auto wltIter = wallets.find(wltid);
      if (wltIter != wallets.end())
      {
         wg.wallets_[wltid] = wltIter->second;
      }

      else
      {
         auto lbIter = lockboxes.find(wltid);
         if (lbIter != lockboxes.end())
         {
            wg.wallets_[wltid] = lbIter->second;
         }
      }
   }

   wg.pageHistory(true, false);

   return wg;
}

////////////////////////////////////////////////////////////////////////////////
uint32_t BlockDataViewer::getBlockTimeByHeight(uint32_t height) const
{
   auto bh = blockchain().getHeaderByHeight(height, 0xFF);

   return bh->getTimestamp();
}

////////////////////////////////////////////////////////////////////////////////
LedgerDelegate BlockDataViewer::getLedgerDelegateForWallets()
{
   auto getHist = [this](uint32_t pageID)->vector<LedgerEntry>
   { return this->getWalletsHistoryPage(pageID, false, false); };

   auto getBlock = [this](uint32_t block)->uint32_t
   { return this->groups_[group_wallet].getBlockInVicinity(block); };

   auto getPageId = [this](uint32_t block)->uint32_t
   { return this->groups_[group_wallet].getPageIdForBlockHeight(block); };

   auto getPageCount = [this](void)->uint32_t
   { return this->getWalletsPageCount(); };

   return LedgerDelegate(getHist, getBlock, getPageId, getPageCount);
}

////////////////////////////////////////////////////////////////////////////////
LedgerDelegate BlockDataViewer::getLedgerDelegateForLockboxes()
{
   auto getHist = [this](uint32_t pageID)->vector<LedgerEntry>
   { return this->getLockboxesHistoryPage(pageID, false, false); };

   auto getBlock = [this](uint32_t block)->uint32_t
   { return this->groups_[group_lockbox].getBlockInVicinity(block); };

   auto getPageId = [this](uint32_t block)->uint32_t
   { return this->groups_[group_lockbox].getPageIdForBlockHeight(block); };

   auto getPageCount = [this](void)->uint32_t
   { return this->getLockboxesPageCount(); };

   return LedgerDelegate(getHist, getBlock, getPageId, getPageCount);
}

////////////////////////////////////////////////////////////////////////////////
LedgerDelegate BlockDataViewer::getLedgerDelegateForWallet(
   const std::string& wltID)
{
   std::shared_ptr<BtcWallet> wlt;
   for (auto& group : groups_) {
      ReadWriteLock::WriteLock wl(group.lock_);
      auto wltIter = group.wallets_.find(wltID);
      if (wltIter != group.wallets_.end()) {
         wlt = wltIter->second;
         break;
      }
   }

   if (wlt == nullptr) {
      throw std::runtime_error("Unregistered wallet ID");
   }

   auto getHist = [wlt](uint32_t pageID)->vector<LedgerEntry>
   { return wlt->getHistoryPageAsVector(pageID); };

   auto getBlock = [wlt](uint32_t block)->uint32_t
   { return wlt->historyPager().getBlockInVicinity(block); };

   auto getPageId = [wlt](uint32_t block)->uint32_t
   { return wlt->historyPager().getPageIdForBlockHeight(block); };

   auto getPageCount = [wlt](void)->uint32_t
   { return wlt->historyPager().getPageCount(); };

   return LedgerDelegate(getHist, getBlock, getPageId, getPageCount);
}

////////////////////////////////////////////////////////////////////////////////
LedgerDelegate BlockDataViewer::getLedgerDelegateForScrAddr(
   const string& wltID, const BinaryData& scrAddr)
{
   std::shared_ptr<BtcWallet> wlt;
   for (auto& group : groups_) {
      ReadWriteLock::WriteLock wl(group.lock_);
      auto wltIter = group.wallets_.find(wltID);
      if (wltIter != group.wallets_.end()) {
         wlt = wltIter->second;
         break;
      }
   }

   if (wlt == nullptr) {
      throw runtime_error("Unregistered wallet ID");
   }

   ScrAddrObj& sca = wlt->getScrAddrObjRef(scrAddr);

   auto getHist = [&](uint32_t pageID)->vector<LedgerEntry>
   { return sca.getHistoryPageById(pageID); };

   auto getBlock = [&](uint32_t block)->uint32_t
   { return sca.getBlockInVicinity(block); };

   auto getPageId = [&](uint32_t block)->uint32_t
   { return sca.getPageIdForBlockHeight(block); };

   auto getPageCount = [&](void)->uint32_t
   { return sca.getPageCount(); };

   return LedgerDelegate(getHist, getBlock, getPageId, getPageCount);
}


////////////////////////////////////////////////////////////////////////////////
uint32_t BlockDataViewer::getClosestBlockHeightForTime(uint32_t timestamp)
{
   //get timestamp of genesis block
   auto genBlock = blockchain().getGenesisBlock();
   
   //sanity check
   if (timestamp < genBlock->getTimestamp())
      return 0;

   //get time diff and divide by average time per block (600 sec for Bitcoin)
   uint32_t diff = timestamp - genBlock->getTimestamp();
   int32_t blockHint = diff/600;

   //look for a block in the hint vicinity with a timestamp lower than ours
   while (blockHint > 0)
   {
      auto block = blockchain().getHeaderByHeight(blockHint, 0xFF);
      if (block->getTimestamp() < timestamp)
         break;

      blockHint -= 1000;
   }

   //another sanity check
   if (blockHint < 0)
      return 0;

   for (uint32_t id = blockHint; id < blockchain().top()->getBlockHeight() - 1; id++)
   {
      //not looking for a really precise block, 
      //anything within the an hour of the timestamp is enough
      auto block = blockchain().getHeaderByHeight(id, 0xFF);
      if (block->getTimestamp() + 3600 > timestamp)
         return block->getBlockHeight();
   }

   return blockchain().top()->getBlockHeight() - 1;
}

////////////////////////////////////////////////////////////////////////////////
TxOut BlockDataViewer::getTxOutCopy(
   const BinaryData& txHash, uint16_t index) const
{
   TxOut txOut;
   
   {
      auto&& tx = db_->beginTransaction(STXO, LMDB::ReadOnly);
      BinaryData bdkey = db_->getDBKeyForHash(txHash);
      if (bdkey.getSize() != 0)
         txOut = db_->getTxOutCopy(bdkey, index);
   }

   if (!txOut.isInitialized())
   {
      auto ss = zeroConfCont_->getSnapshot();
      auto&& zcKey = ss->getKeyForHash(txHash);
      txOut = ss->getTxOutCopy(zcKey, index);
   }

   return txOut;
}

////////////////////////////////////////////////////////////////////////////////
TxOut BlockDataViewer::getTxOutCopy(const BinaryData& dbKey) const
{
   if (dbKey.getSize() != 8)
      throw runtime_error("invalid txout key length");

   auto&& tx = db_->beginTransaction(STXO, LMDB::ReadOnly);

   auto&& bdkey = dbKey.getSliceRef(0, 6);
   auto index = READ_UINT16_BE(dbKey.getSliceRef(6, 2));

   auto&& txOut = db_->getTxOutCopy(bdkey, index);
   if (!txOut.isInitialized())
   {
      auto ss = zeroConfCont_->getSnapshot();
      txOut = ss->getTxOutCopy(bdkey, index);
   }

   return txOut;
}

////////////////////////////////////////////////////////////////////////////////
StoredTxOut BlockDataViewer::getStoredTxOut(const BinaryData& dbKey) const
{
   if (dbKey.getSize() != 8)
      throw runtime_error("invalid txout key length");

   auto&& tx = db_->beginTransaction(STXO, LMDB::ReadOnly);

   StoredTxOut stxo;
   db_->getStoredTxOut(stxo, dbKey);
   stxo.parentHash_ = move(db_->getTxHashForLdbKey(dbKey.getSliceRef(0, 6)));
   
   return stxo;
}

////////////////////////////////////////////////////////////////////////////////
Tx BlockDataViewer::getSpenderTxForTxOut(uint32_t height, uint32_t txindex,
   uint16_t txoutid) const
{
   StoredTxOut stxo;
   db_->getStoredTxOut(stxo, height, txindex, txoutid);

   if (!stxo.isSpent())
      return Tx();

   TxRef txref(stxo.spentByTxInKey_.getSliceCopy(0, 6));
   DBTxRef dbTxRef(txref, db_);
   return dbTxRef.getTxCopy();
}

////////////////////////////////////////////////////////////////////////////////
bool BlockDataViewer::isRBF(const BinaryData& txHash) const
{
   auto&& zctx = zeroConfCont_->getTxByHash(txHash);
   if (!zctx.isInitialized())
      return false;

   return zctx.isRBF();
}

////////////////////////////////////////////////////////////////////////////////
bool BlockDataViewer::hasScrAddress(const BinaryDataRef& scrAddr) const
{
   //TODO: make sure this is thread safe

   for (auto& group : groups_)
   {
      ReadWriteLock::WriteLock wl(group.lock_);

      for (auto& wlt : group.wallets_)
      {
         if (wlt.second->hasScrAddress(scrAddr))
            return true;
      }
   }

   return false;
}

////////////////////////////////////////////////////////////////////////////////
std::set<BinaryDataRef> BlockDataViewer::getAddrSet() const
{
   std::set<BinaryDataRef> addrSet;
   for (auto& group : groups_) {
      ReadWriteLock::WriteLock wl(group.lock_);
      for (auto& wlt : group.wallets_) {
         auto wltAddresses = wlt.second->getAddrSet();
         addrSet.insert(wltAddresses.begin(), wltAddresses.end());
      }
   }
   return addrSet;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<BtcWallet> BlockDataViewer::getWalletOrLockbox(
   const string& id) const
{
   auto wallet = groups_[group_wallet].getWalletByID(id);
   if (wallet != nullptr)
      return wallet;

   return groups_[group_lockbox].getWalletByID(id);
}

///////////////////////////////////////////////////////////////////////////////
tuple<uint64_t, uint64_t> BlockDataViewer::getAddrFullBalance(
   const BinaryData& scrAddr)
{
   StoredScriptHistory ssh;
   db_->getStoredScriptHistorySummary(ssh, scrAddr);

   return move(make_tuple(ssh.totalUnspent_, ssh.totalTxioCount_));
}

///////////////////////////////////////////////////////////////////////////////
unique_ptr<BDV_Notification_ZC> BlockDataViewer::createZcNotification(
   const set<BinaryDataRef>& addrSet)
{
   ZcNotificationPacket packet(getID());

   //grab zc map
   auto ss = zeroConfCont_->getSnapshot();
   if (ss != nullptr)
   {
      for (auto& addr : addrSet)
      try
      {
         const auto& keySet = ss->getTxioKeysForScrAddr(addr);

         auto iter = packet.scrAddrToTxioKeys_.emplace(addr, set<BinaryData>());
         for (auto& key : keySet)
            iter.first->second.emplace(key);
      }
      catch (range_error&)
      {
         continue;
      }
   }

   packet.ssPtr_ = ss;
   auto notifPtr = make_unique<BDV_Notification_ZC>(packet);
   return notifPtr;
}

///////////////////////////////////////////////////////////////////////////////
std::map<BinaryData, std::vector<Output>> BlockDataViewer::getAddressOutpoints(
   const std::set<BinaryDataRef>& scrAddrSet,
   unsigned& heightCutoff, unsigned& zcCutoff) const
{
   /*wallet agnostic method*/

   auto topHeight = getTopBlockHeader()->getBlockHeight();
   map<BinaryData, std::vector<Output>> outpointMap;

   //confirmed outputs, skip is heightCutoff is UINT32_MAX
   if (heightCutoff != UINT32_MAX) {
      for (const auto& scrAddr : scrAddrSet) {
         StoredScriptHistory ssh;
         if (!db_->getStoredScriptHistory(ssh, scrAddr, heightCutoff)) {
            continue;
         }
         if (ssh.subHistMap_.empty()) {
            continue;
         }

         auto firstPairIter = outpointMap.emplace(
            scrAddr, std::vector<Output>());
         auto& opVec = firstPairIter.first->second;

         /*
         Run decrementally to process spent txios first and ignore the
         younger, unspent counterparts.
         */

         set<BinaryData> processedKeys;
         auto rIter = ssh.subHistMap_.rbegin();
         while (rIter != ssh.subHistMap_.rend()) {
            auto& subssh = rIter->second;
            for (auto& txioPair : subssh.txioMap_) {
               //keep track of processed txios by their output key, 
               //skip if already in set
               auto&& txOutKey = txioPair.second.getDBKeyOfOutput();
               auto insertIter = processedKeys.emplace(txOutKey);
               if (!insertIter.second)
                  continue;

               StoredTxOut stxo;
               if (!db_->getStoredTxOut(stxo, txioPair.second.getDBKeyOfOutput())) {
                  throw std::runtime_error("failed to grab txout");
               }

               auto txHash = txioPair.second.getTxHashOfOutput(db_);
               BinaryData spenderHash;
               if (stxo.isSpent()) {
                  spenderHash = txioPair.second.getTxHashOfInput(db_);
               }

               opVec.emplace_back(Output(
                  stxo.getValue(), stxo.getHeight(),
                  stxo.txIndex_, stxo.txOutIndex_,
                  txHash, stxo.getScriptRef(), spenderHash
               ));
            }

            ++rIter;
         }
      }

      //update height cutoff
      heightCutoff = topHeight;
   }

   //zc outpoints, skip if zcCutoff is UINT32_MAX
   if (zcCutoff != UINT32_MAX) {
      auto zcSnapshot = zc_->getSnapshot();
      if (zcSnapshot == nullptr) {
         return outpointMap;
      }

      for (const auto& scrAddr : scrAddrSet) {
         //NOTE: getTxioMapForScrAddr is semi expensive
         auto txioMapFromSS = zcSnapshot->getTxioMapForScrAddr(scrAddr);
         for (const auto& txiopair : txioMapFromSS) {
            //grab txoutref, useful in all but 1 case
            auto txOutRef = txiopair.second->getTxRefOfOutput();

            //does this txio have a zc txin, txout or both?
            bool txOutZc = txiopair.second->hasTxOutZC();
            bool txInZc = txiopair.second->hasTxInZC();
            BinaryDataRef spenderHash;

            if (txInZc) {
               //has zc txin, check cutoff
               auto txInRef = txiopair.second->getTxRefOfInput();
               BinaryRefReader brr(txInRef.getDBKeyRef());
               brr.advance(2);

               auto zcID = brr.get_uint32_t(BE);
               if (zcID < zcCutoff)
                  continue;

               //spent zc, grab the spender tx hash
               auto txFromSS = zcSnapshot->getTxByKey(txInRef.getDBKeyRef());
               if (txFromSS == nullptr)
                  throw runtime_error("missing spender zc");
               spenderHash = txFromSS->getTxHash().getRef();
            } else if (txOutZc) {
               //has zc txout only (unspent), check cutoff
               BinaryRefReader brr(txOutRef.getDBKeyRef());
               brr.advance(2);

               auto zcID = brr.get_uint32_t(BE);
               if (zcID < zcCutoff)
                  continue;
            }

            //if we got this far, add this outpoint
            auto firstPairIter = outpointMap.find(scrAddr);
            if (firstPairIter == outpointMap.end()) {
               firstPairIter = outpointMap.emplace(
                  scrAddr, std::vector<Output>()).first;
            }

            if (!txOutZc) {
               auto txHash = txiopair.second->getTxHashOfOutput(db_);

               //mined txout, have to grab it from db
               StoredTxOut stxo;
               if (!db_->getStoredTxOut(stxo, txiopair.second->getDBKeyOfOutput())) {
                  throw std::runtime_error("failed to grab txout");
               }
               firstPairIter->second.emplace_back(Output(
                  stxo.getValue(), stxo.getHeight(),
                  stxo.txIndex_, stxo.txOutIndex_,
                  txHash, stxo.getScriptRef(), spenderHash)
               );
            } else {
               //zc txout, grab from snapshot
               auto txFromSS = zcSnapshot->getTxByKey(txOutRef.getDBKey());
               if (txFromSS == nullptr) {
                  throw std::runtime_error(
                     "can't find zc tx by txiopair output key");
               }

               const auto& txHash = txFromSS->getTxHash();
               auto outputIndex = txiopair.second->getIndexOfOutput();
               const auto& parsedTxOut = txFromSS->outputs_[outputIndex];

               BinaryDataRef spenderHashRef;
               if (txiopair.second->hasTxIn()) {
                  spenderHashRef.setRef(spenderHash);
               }

               //zc outpoints override mined ones
               firstPairIter->second.emplace_back(Output(
                  parsedTxOut.value_, UINT32_MAX,
                  UINT32_MAX, outputIndex,
                  txHash, {}, spenderHashRef
               ));
            }
         }
      }

      //update zc id cutoff
      zcCutoff = zcSnapshot->getTopZcID();
   }

   return outpointMap;
}

///////////////////////////////////////////////////////////////////////////////
vector<UTXO> BlockDataViewer::getUtxosForAddress(
   const BinaryDataRef& scrAddr, bool withZc) const
{
   /*wallet agnostic method*/

   vector<UTXO> result;

   //mined utxos
   StoredScriptHistory ssh;
   if (db_->getStoredScriptHistory(ssh, scrAddr))
   {
      for (auto& subssh : ssh.subHistMap_)
      {
         for (auto& txioPair : subssh.second.txioMap_)
         {
            if (!txioPair.second.isUTXO())
               continue;

            StoredTxOut stxo;
            if (!db_->getStoredTxOut(stxo, txioPair.second.getDBKeyOfOutput()))
               throw runtime_error("failed to grab txout");

            auto&& txHash = txioPair.second.getTxHashOfOutput(db_);
            UTXO utxo(stxo.getValue(), stxo.getHeight(), stxo.txIndex_, 
               stxo.txOutIndex_, txHash, stxo.getScriptRef());

            result.emplace_back(utxo);
         }
      }
   }

   if (!withZc)
      return result;

   //zc utxos
   auto zcSnapshot = zc_->getSnapshot();
   auto txioMapFromSS = zcSnapshot->getTxioMapForScrAddr(scrAddr);

   for (auto& txiopair : txioMapFromSS)
   {
      //grab txoutref, useful in all but 1 case
      auto&& txOutRef = txiopair.second->getTxRefOfOutput();

      //does this txio have a zc txin, txout or both?
      if (txiopair.second->hasTxInZC())
         continue;

      //zc txout, grab from snapshot
      auto txFromSS = zcSnapshot->getTxByKey(txOutRef.getDBKey());
      if (txFromSS == nullptr)
         throw runtime_error("can't find zc tx by txiopair output key");

      auto& txHash = txFromSS->getTxHash();
      auto outputIndex = txiopair.second->getIndexOfOutput();
      const auto& parsedTxOut = txFromSS->outputs_[outputIndex];

      //some of these copies can be easily avoided
      auto&& txOutCopy = txFromSS->tx_.getTxOutCopy(outputIndex);
      UTXO utxo(parsedTxOut.value_, UINT32_MAX, UINT32_MAX,
         outputIndex, txHash, txOutCopy.getScript());
      result.emplace_back(utxo);
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
vector<pair<StoredTxOut, BinaryDataRef>> BlockDataViewer::getOutputsForOutpoints(
   const map<BinaryDataRef, set<unsigned>>& outpoints, bool withZc) const
{
   vector<pair<StoredTxOut, BinaryDataRef>> result;
   shared_ptr<MempoolSnapshot> zcSS = nullptr;
   BinaryData zckey;
   if (withZc)
   {
      zckey = DBUtils::heightAndDupToHgtx(0xFFFFFFFF, 0xFF);
      zcSS = zc_->getSnapshot();
   }
   
   auto&& stxo_tx = db_->beginTransaction(STXO, LMDB::ReadOnly);

   for (auto& opSet : outpoints)
   {
      //get dbkey for this txhash
      auto&& dbkey = db_->getDBKeyForHash(opSet.first);
      if (dbkey.getSize() == 6)
      {
         for (auto& op : opSet.second)
         {
            //set txout index
            pair<StoredTxOut, BinaryDataRef> stxoPair;
            stxoPair.second = opSet.first;
            
            auto& stxo = stxoPair.first;
            stxo.txOutIndex_ = op;
            auto stxoKey = dbkey;
            stxoKey.append(WRITE_UINT16_BE(op));

            if (!db_->getStoredTxOut(stxo, stxoKey))
               throw runtime_error("invalid outpoint");
               
            result.emplace_back(stxoPair);
         }

         continue;
      }

      if (!withZc || zcSS == nullptr)
         throw runtime_error("invalid outpoint");

      auto txFromSS = zcSS->getTxByHash(opSet.first);
      if (txFromSS == nullptr)
         throw runtime_error("invalid outpoint");

      for (auto& op : opSet.second)
      {
         //set txout index
         pair<StoredTxOut, BinaryDataRef> stxoPair;
         stxoPair.second = opSet.first;
            
         auto& stxo = stxoPair.first;
         stxo.txOutIndex_ = op;
         if (txFromSS->outputs_.size() <= op)
            throw runtime_error("invalid outpoint");

         const auto& output = txFromSS->outputs_[op];
         BinaryRefReader brr(txFromSS->tx_.getPtr(), txFromSS->tx_.getSize());
         brr.advance(output.offset_);
         auto txOutRef = brr.get_BinaryDataRef(output.len_);
            
         stxo.unserialize(txOutRef);
         stxo.blockHeight_ = UINT32_MAX;
         stxo.txIndex_ = UINT16_MAX;
         stxo.hgtX_ = zckey;
         result.emplace_back(stxoPair);
      }
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
//// WalletGroup
////////////////////////////////////////////////////////////////////////////////
WalletGroup::~WalletGroup()
{
   for (auto& wlt : wallets_)
      wlt.second->unregister();
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<BtcWallet> WalletGroup::getOrSetWallet(const string& id)
{
   ReadWriteLock::WriteLock wl(lock_);
   shared_ptr<BtcWallet> theWallet;

   auto wltIter = wallets_.find(id);
   if (wltIter != wallets_.end())
   {
      theWallet = wltIter->second;
   }
   else
   {
      auto walletPtr = make_shared<BtcWallet>(bdvPtr_, id);
      auto insertResult = wallets_.insert(make_pair(
         id, walletPtr));

      theWallet = insertResult.first->second;
   }

   return theWallet;
}

////////////////////////////////////////////////////////////////////////////////
bool WalletGroup::unregisterWallet(const string& id)
{
   ReadWriteLock::WriteLock wl(lock_);

   auto wltIter = wallets_.find(id);
   if (wltIter == wallets_.end())
      return false;

   wallets_.erase(wltIter);
   return true;
}

////////////////////////////////////////////////////////////////////////////////
void WalletGroup::registerAddresses(WalletRegistrationRequest& request)
{
   if (request.walletId.empty()) {
      return;
   }

   auto theWallet = getOrSetWallet(request.walletId);
   if (theWallet == nullptr) {
      LOGWARN << "failed to get or set wallet";
      return;
   }

   //strip collisions from set of addresses to register
   auto addrMap = theWallet->scrAddrMap_.get();
   std::set<BinaryData> scrAddrSet;
   for (auto& addr : request.addresses) {
      if (addr.empty()) {
         continue;
      }
      if (addrMap->find(addr) != addrMap->end()) {
         continue;
      }
      scrAddrSet.emplace(std::move(addr));
   }

   auto callback = [theWallet](std::set<BinaryDataRef>& addrSet)->void
   {
      auto bdvPtr = theWallet->bdvPtr_;
      auto dbPtr = theWallet->bdvPtr_->getDB();
      auto bcPtr = &theWallet->bdvPtr_->blockchain();
      auto zcPtr = theWallet->bdvPtr_->zcContainer();

      std::map<BinaryDataRef, std::shared_ptr<ScrAddrObj>> saMap;
      {
         auto addrMapPtr = theWallet->scrAddrMap_.get();
         for (auto& addr : addrSet) {
            if (addrMapPtr->find(addr) != addrMapPtr->end()) {
               continue;
            }
            auto scrAddrPtr = make_shared<ScrAddrObj>(
               dbPtr, bcPtr, zcPtr, addr);
            saMap.emplace(addr, scrAddrPtr);
         }
      }

      std::unique_ptr<BDV_Notification_ZC> zcNotifPacket;
      if (!saMap.empty()) {
         zcNotifPacket = std::move(bdvPtr->createZcNotification(addrSet));
         theWallet->scrAddrMap_.update(saMap);
      }
      theWallet->setRegistered();

      BinaryDataRef wltIdRef;
      wltIdRef.setRef(theWallet->walletID());
      bdvPtr->flagRefresh(
         BDV_refreshAndRescan, wltIdRef, std::move(zcNotifPacket));
   };

   auto batch = make_shared<RegistrationBatch>();
   batch->scrAddrSet_ = move(scrAddrSet);
   batch->isNew_ = request.isNew;
   batch->callback_ = callback;

   saf_->pushAddressBatch(batch);
   theWallet->resetCounters();
}

////////////////////////////////////////////////////////////////////////////////
bool WalletGroup::hasID(const string& ID) const
{
   ReadWriteLock::ReadLock rl(lock_);
   return wallets_.find(ID) != wallets_.end();
}

/////////////////////////////////////////////////////////////////////////////
void WalletGroup::reset()
{
   ReadWriteLock::ReadLock rl(lock_);
   for (const auto& wlt : values(wallets_))
      wlt->reset();
}

////////////////////////////////////////////////////////////////////////////////
map<uint32_t, uint32_t> WalletGroup::computeWalletsSSHSummary(
   bool forcePaging, bool pageAnyway)
{
   map<uint32_t, uint32_t> fullSummary;

   ReadWriteLock::ReadLock rl(lock_);

   bool isAlreadyPaged = true;
   for (auto& wlt : values(wallets_))
   {
      if(forcePaging)
         wlt->mapPages();

      if (wlt->isPaged())
         isAlreadyPaged = false;
      else
         wlt->mapPages();
   }

   if (isAlreadyPaged)
   {
      if (!forcePaging && !pageAnyway)
         throw AlreadyPagedException();
   }

   for (auto& wlt : values(wallets_))
   {
      if (wlt->uiFilter_ == false)
         continue;

      const auto& wltSummary = wlt->getSSHSummary();

      for (auto summary : wltSummary)
         fullSummary[summary.first] += summary.second;
   }

   return fullSummary;
}

////////////////////////////////////////////////////////////////////////////////
bool WalletGroup::pageHistory(bool forcePaging, bool pageAnyway)
{
   auto computeSummary = [&](void)->map<uint32_t, uint32_t>
   { return this->computeWalletsSSHSummary(forcePaging, pageAnyway); };

   return hist_.mapHistory(computeSummary);
}

////////////////////////////////////////////////////////////////////////////////
vector<LedgerEntry> WalletGroup::getHistoryPage(
   uint32_t pageId, unsigned updateID, 
   bool rebuildLedger, bool remapWallets)
{
   unique_lock<mutex> mu(globalLedgerLock_);

   if (pageId >= hist_.getPageCount())
      throw std::range_error("pageId out of range");

   if (order_ == order_ascending)
      pageId = hist_.getPageCount() - pageId - 1;

   if (rebuildLedger || remapWallets)
      pageHistory(remapWallets, false);

   vector<LedgerEntry> vle;

   if (rebuildLedger || remapWallets)
      updateID = UINT32_MAX;

   {
      ReadWriteLock::ReadLock rl(lock_);

      set<string> localFilterSet;
      map<string, shared_ptr<BtcWallet>> localWalletMap;
      for (auto& wlt_pair : wallets_)
      {
         if (!wlt_pair.second->uiFilter_)
            continue;

         localFilterSet.insert(wlt_pair.first);
         localWalletMap.insert(wlt_pair);
      }

      if (localFilterSet != wltFilterSet_)
      {
         updateID = UINT32_MAX;
         wltFilterSet_ = move(localFilterSet);
      }

      auto getTxio = [&localWalletMap](
         uint32_t, uint32_t)->map<BinaryData, TxIOPair>
      {
         return map<BinaryData, TxIOPair>();
      };

      auto buildLedgers = [&localWalletMap](
         const map<BinaryData, TxIOPair>&,
         uint32_t startBlock, uint32_t endBlock)->map<BinaryData, LedgerEntry>
      {
         map<BinaryData, LedgerEntry> result;
         unsigned i = 0;
         for (auto& wlt_pair : localWalletMap)
         {
            auto&& txio_map = wlt_pair.second->getTxioForRange(
               startBlock, endBlock);
            auto&& ledgerMap = wlt_pair.second->updateWalletLedgersFromTxio(
               txio_map, startBlock, endBlock);

            for (auto& ledger : ledgerMap)
            {
               BinaryWriter bw;
               bw.put_uint32_t(i++);

               auto&& ledger_pair = make_pair(bw.getData(), move(ledger.second));
               result.insert(move(ledger_pair));
            }
         }

         return result;
      };

      auto leMap = hist_.getPageLedgerMap(
         getTxio, buildLedgers, pageId, updateID, nullptr);

      if (leMap != nullptr)
      {
         for (auto& le : *leMap)
            vle.push_back(le.second);
      }
   }

   if (order_ == order_ascending)
   {
      sort(vle.begin(), vle.end());
   }
   else
   {
      LedgerEntry_DescendingOrder desc;
      sort(vle.begin(), vle.end(), desc);
   }

   return vle;
}

////////////////////////////////////////////////////////////////////////////////
void WalletGroup::updateLedgerFilter(const vector<string>& walletsList)
{
   ReadWriteLock::ReadLock rl(lock_);

   vector<string> enabledIDs;
   for (auto& wlt_pair : wallets_)
   {
      if (wlt_pair.second->uiFilter_)
         enabledIDs.push_back(wlt_pair.first);
      wlt_pair.second->uiFilter_ = false;
   }


   for (auto walletID : walletsList)
   {
      auto iter = wallets_.find(walletID);
      if (iter == wallets_.end())
         continue;

      iter->second->uiFilter_ = true;
   }
   
   auto vec_copy = walletsList;
   sort(vec_copy.begin(), vec_copy.end());
   sort(enabledIDs.begin(), enabledIDs.end());

   if (vec_copy == enabledIDs)
      return;

   pageHistory(false, true);
   bdvPtr_->flagRefresh(BDV_filterChanged, BinaryData(), nullptr);
}

////////////////////////////////////////////////////////////////////////////////
void WalletGroup::scanWallets(ScanWalletStruct& scanData, 
   int32_t updateID)
{
   ReadWriteLock::ReadLock rl(lock_);

   for (auto& wlt : wallets_)
      wlt.second->scanWallet(scanData, updateID);
}

////////////////////////////////////////////////////////////////////////////////
map<string, shared_ptr<BtcWallet> > WalletGroup::getWalletMap(void) const
{
   ReadWriteLock::ReadLock rl(lock_);
   return wallets_;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<BtcWallet> WalletGroup::getWalletByID(const string& ID) const
{
   auto iter = wallets_.find(ID);
   if (iter != wallets_.end())
      return iter->second;

   return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
uint32_t WalletGroup::getBlockInVicinity(uint32_t blk) const
{
   //expect history has been computed, it will throw otherwise
   return hist_.getBlockInVicinity(blk);
}

////////////////////////////////////////////////////////////////////////////////
uint32_t WalletGroup::getPageIdForBlockHeight(uint32_t blk) const
{
   //same as above
   return hist_.getPageIdForBlockHeight(blk);
}
