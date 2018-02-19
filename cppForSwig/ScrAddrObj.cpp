////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#include "ScrAddrObj.h"

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//
// ScrAddrObj Methods
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
ScrAddrObj::ScrAddrObj(LMDBBlockDatabase *db, Blockchain *bc,
                       HashString    addr, 
                       uint32_t      firstBlockNum,
                       uint32_t      firstTimestamp,
                       uint32_t      lastBlockNum,
                       uint32_t      lastTimestamp) :
      db_(db),
      bc_(bc),
      scrAddr_(addr), 
      firstBlockNum_(firstBlockNum), 
      firstTimestamp_(firstTimestamp),
      lastBlockNum_(lastBlockNum), 
      lastTimestamp_(lastTimestamp),
      utxos_(this)
{ 
   relevantTxIO_.clear();
} 



////////////////////////////////////////////////////////////////////////////////
uint64_t ScrAddrObj::getSpendableBalance(uint32_t currBlk) const
{
   //ignoreing the currBlk for now, until the partial history loading is solid
   uint64_t balance = getFullBalance();

   for (auto txio : relevantTxIO_)
   {
      if (!txio.second.hasTxIn() &&
          !txio.second.isSpendable(db_, currBlk))
         balance -= txio.second.getValue();
   }

   return balance;
}


////////////////////////////////////////////////////////////////////////////////
uint64_t ScrAddrObj::getUnconfirmedBalance(uint32_t currBlk) const
{
   uint64_t balance = 0;
   for (auto txio : relevantTxIO_)
   {
      if(txio.second.isMineButUnconfirmed(db_, currBlk))
         balance += txio.second.getValue();
   }
   return balance;
}

////////////////////////////////////////////////////////////////////////////////
uint64_t ScrAddrObj::getFullBalance() const
{
   StoredScriptHistory ssh;
   db_->getStoredScriptHistorySummary(ssh, scrAddr_);
   uint64_t balance = ssh.getScriptBalance(false);

   for (auto txio : relevantTxIO_)
   {
      if (txio.second.hasTxOutZC())
         balance += txio.second.getValue();
      if (txio.second.hasTxInZC())
         balance -= txio.second.getValue();
   }

   return balance;
}
   
////////////////////////////////////////////////////////////////////////////////
void ScrAddrObj::addTxIO(TxIOPair& txio, bool isZeroConf)
{ 
   relevantTxIO_[txio.getDBKeyOfOutput()] = txio;
}

////////////////////////////////////////////////////////////////////////////////
void ScrAddrObj::clearBlkData(void)
{
   relevantTxIO_.clear();
   hist_.reset();
   totalTxioCount_ = 0;
}

////////////////////////////////////////////////////////////////////////////////
void ScrAddrObj::updateTxIOMap(map<BinaryData, TxIOPair>& txio_map)
{
   for (auto txio : txio_map)
      relevantTxIO_[txio.first] = txio.second;
}

////////////////////////////////////////////////////////////////////////////////
void ScrAddrObj::scanZC(const ScanAddressStruct& scanInfo,
   function<bool(const BinaryDataRef)> isZcFromWallet, int32_t updateID)
{
   //Dont use a reference for this loop. We check and set the isFromSelf flag
   //in this operation, which is based on the wallet this scrAddr belongs to.
   //The txio comes straight from the ZC container object, which only deals 
   //with scrAddr. Since several wallets may reference the same scrAddr, we 
   //can't modify original txio, so we use a copy.

   map<BinaryData, BinaryDataRef> invalidatedZCMap;

   //look for invalidated keys, delete from validZcKeys_ as we go
   bool purge = false;

   if (scanInfo.invalidatedZcKeys_.size() != 0)
   {
      auto keyIter = validZCKeys_.begin();
      while (keyIter != validZCKeys_.end())
      {
         auto zcIter = scanInfo.invalidatedZcKeys_.find(
            keyIter->first.getSliceRef(0, 6));
         if (zcIter != scanInfo.invalidatedZcKeys_.end())
         {
            purge = true;

            for (auto& txiokey : keyIter->second)
            {
               auto&& insertIter = invalidatedZCMap.insert(make_pair(
                  txiokey, zcIter->getRef()));

               if (insertIter.second == false)
               {
                  //If we got here, this zctxio entry is flagged 
                  //twice, i.e. for both input and output. Set the
                  //IO reference to null so that the purge code 
                  //knows to get rid of the entire entry
                  insertIter.first->second = BinaryDataRef();
               }
            }

            validZCKeys_.erase(keyIter++);
            continue;
         }

         ++keyIter;
      }
   }

   //purge if necessary
   if (purge)
   {
      if (purgeZC(invalidatedZCMap, scanInfo.minedTxioKeys_))
         updateID_ = updateID;
   }

   auto haveIter = scanInfo.zcMap_.find(scrAddr_);
   if (haveIter == scanInfo.zcMap_.end())
      return;

   if (haveIter->second == nullptr)
   {
      LOGWARN << "empty zc notification txio map";
      return;
   }

   auto& zcTxIOMap = *haveIter->second;

   //look for new keys
   map<BinaryData, TxIOPair> newZC;

   for (auto& txiopair : zcTxIOMap)
   {
      auto& newtxio = txiopair.second;
      auto _keyIter = relevantTxIO_.find(txiopair.first);
      if (_keyIter != relevantTxIO_.end())
      {
         //dont replace a zc that is spent with a zc that is unspent. 
         //zc revocation is handled in the purge segment         
         auto& txio = _keyIter->second;
         if (txio.hasTxIn())
         {
            if (txio.getDBKeyOfInput() == newtxio.getDBKeyOfInput())
               continue;
         }
      }

      newZC[txiopair.first] = newtxio;

      if (txiopair.second.hasTxOutZC())
      {
         auto& zckeyset = 
            validZCKeys_[txiopair.second.getDBKeyOfOutput()];
         zckeyset.insert(txiopair.first);
      }

      if (txiopair.second.hasTxInZC())
      {
         auto& zckeyset =
            validZCKeys_[txiopair.second.getDBKeyOfInput()];
         zckeyset.insert(txiopair.first);
      }
   }

   //nothing to do if we didn't find new ZC
   if (newZC.size() == 0)
      return;

   updateID_ = updateID;

   for (auto& txioPair : newZC)
   {
      if (txioPair.second.hasTxOutZC() &&
          isZcFromWallet(move(txioPair.second.getDBKeyOfOutput().getSliceRef(0, 6))))
         txioPair.second.setTxOutFromSelf(true);

      txioPair.second.setScrAddrLambda(
         [this](void)->const BinaryData&{ return this->getScrAddr(); });

      relevantTxIO_[txioPair.first] = move(txioPair.second);
   }
}

////////////////////////////////////////////////////////////////////////////////
bool ScrAddrObj::purgeZC(
   const map<BinaryData, BinaryDataRef>& invalidatedTxOutKeys,
   const map<BinaryData, BinaryData>& minedKeys)
{
   bool purged = false;
   for (auto zc : invalidatedTxOutKeys)
   {
      auto txioIter = relevantTxIO_.find(zc.first);
      if (txioIter == relevantTxIO_.end())
         continue;

      if (zc.second.isNull())
      {
         //the entire entry needs to go
         relevantTxIO_.erase(txioIter);
         purged = true;
         continue;
      }

      TxIOPair& txio = txioIter->second;
      if (txio.getTxRefOfOutput().getDBKeyRef() == zc.second)
      {
         BinaryData txInKey;

         //is this ZC purged because it was mined?
         auto minedKeyIter = minedKeys.find(zc.first);
         if (minedKeyIter != minedKeys.end())
            txInKey = move(txioIter->second.getDBKeyOfInput());

         //purged ZC chain, remove the TxIO
         relevantTxIO_.erase(txioIter);
         purged = true;

         //was this txio carrying an input?
         if (txInKey.getSize() == 0)
            continue;

         //zc txio had a spend and was mined, reciprocate the spend on the now
         //mined txio
         auto minedIter = relevantTxIO_.find(minedKeyIter->second);
         if (minedIter == relevantTxIO_.end())
         {
            LOGWARN << "missing mined txio";
            continue;
         }

         minedIter->second.setTxIn(txInKey);
         continue;
      }

      if (txio.hasTxInZC() && txio.getTxRefOfInput().getDBKeyRef() == zc.second)
      {
         txio.setTxIn(BinaryData(0));
         txio.setTxHashOfInput(BinaryData(0));
         purged = true;
      }
   }

   return purged;
}

////////////////////////////////////////////////////////////////////////////////
void ScrAddrObj::updateAfterReorg(uint32_t lastValidBlockHeight)
{
   auto txioIter = relevantTxIO_.begin();

   uint32_t height;
   while (txioIter != relevantTxIO_.end())
   {
      //txio pairs are saved by TxOut DBkey, if the key points to a block 
      //higher than the reorg point, delete the txio
      height = DBUtils::hgtxToHeight(txioIter->first.getSliceCopy(0, 4));

      if (height >= 0xFF000000)
      {
         //ZC chain, already dealt with by the call to purgeZC from 
         //readBlkFileUpdate
         continue;
      }
      else if (height <= lastValidBlockHeight)
      {
         TxIOPair& txio = txioIter->second;
         if (txio.hasTxIn())
         {
            //if the txio is spent, check the block of the txin
            height = DBUtils::hgtxToHeight(
               txio.getDBKeyOfInput().getSliceCopy(0, 4));

            if (height > lastValidBlockHeight && height < 0xFF000000)
            {
               //clear the TxIn by setting it to an empty BinaryData
               txio.setTxIn(BinaryData(0));
               txio.setTxHashOfInput(BinaryData(0));
            }
         }

         ++txioIter;
      }
      else
         relevantTxIO_.erase(txioIter++);
   }
}

////////////////////////////////////////////////////////////////////////////////
map<BinaryData, LedgerEntry> ScrAddrObj::updateLedgers(
                               const map<BinaryData, TxIOPair>& txioMap,
                               uint32_t startBlock, uint32_t endBlock) const
{
   return LedgerEntry::computeLedgerMap(txioMap, startBlock, endBlock,
                                 scrAddr_, db_, bc_);
}

////////////////////////////////////////////////////////////////////////////////
uint64_t ScrAddrObj::getTxioCountFromSSH(void) const
{
   StoredScriptHistory ssh;
   db_->getStoredScriptHistorySummary(ssh, scrAddr_);

   return ssh.totalTxioCount_;
}

////////////////////////////////////////////////////////////////////////////////
void ScrAddrObj::fetchDBScrAddrData(uint32_t startBlock,
   uint32_t endBlock, int32_t updateID)
{
   //maintains first page worth of TxIO in RAM. This call purges ZC, so you 
   //should rescan ZC right after

   auto hist = getHistoryForScrAddr(startBlock, endBlock, true);
   
   if (hist.size() != 0)
      updateID_ = updateID;
   
   updateTxIOMap(hist);
}

///////////////////////////////////////////////////////////////////////////////
map<BinaryData, TxIOPair> ScrAddrObj::getHistoryForScrAddr(
   uint32_t startBlock, uint32_t endBlock,
   bool update,
   bool withMultisig) const
{
   map<BinaryData, TxIOPair> outMap;

   //check relevantTxio_ first to see if it has some of the TxIOs
   uint32_t localTxioBottom = hist_.getPageBottom(0);
   if (update==false && endBlock > localTxioBottom && lastSeenBlock_ != 0)
   {
      if (startBlock <= localTxioBottom)
      {
         for (auto txioPair : relevantTxIO_)
         {
            auto& txio = outMap[txioPair.first];
            txio = txioPair.second;
            txio.setScrAddrLambda(
               [this](void)->const BinaryData&
               { return this->getScrAddr(); });
         }
      }
      else
      {
         BinaryData startHeight = DBUtils::heightAndDupToHgtx(startBlock, 0);
         startHeight.append(WRITE_UINT16_BE(0));

         for (auto& txioPair : relevantTxIO_)
         {
            if (txioPair.second >= startHeight)
            {
               auto& txio = outMap[txioPair.first];
               txio = txioPair.second;
               txio.setScrAddrLambda(
                  [this](void)->const BinaryData&
                  { return this->getScrAddr(); });
            }
         }
      }
         
      if (startBlock >= localTxioBottom)
         return outMap;

      endBlock = localTxioBottom -1;
   }

   //grab txio range from ssh
   StoredScriptHistory ssh;
   auto start = startBlock;
   if (startBlock == UINT32_MAX && lastSeenBlock_ == 0)
      start = lastSeenBlock_;
   db_->getStoredScriptHistory(ssh, scrAddr_, start, endBlock);

   //update scrAddrObj containers
   totalTxioCount_ = ssh.totalTxioCount_;

   if (endBlock != UINT32_MAX)
      lastSeenBlock_ = endBlock;
   else if (lastSeenBlock_ == 0)
      lastSeenBlock_ = bc_->top()->getBlockHeight();

   if (scrAddr_[0] == SCRIPT_PREFIX_MULTISIG)
      withMultisig = true;

   if (!ssh.isInitialized())
      return outMap;

   //Serve content as a map. Do not overwrite existing TxIOs to avoid wiping ZC
   //data, Since the data isn't overwritten, iterate the map from its end to make
   //sure newer txio aren't ignored due to older ones being inserted first.
   auto subSSHiter = ssh.subHistMap_.rbegin();
   while (subSSHiter != ssh.subHistMap_.rend())
   {
      StoredSubHistory & subssh = subSSHiter->second;

      for (auto &txiop : subssh.txioMap_)
      {
         if (withMultisig || !txiop.second.isMultisig())
         {
            auto& txio = outMap[txiop.first];
            txio.setScrAddrLambda(
                  [this](void)->const BinaryData&
               { return this->getScrAddr(); });
            
            if (!txio.hasValue())
               txio = txiop.second;
         }
      }

      ++subSSHiter;
   }

   return outMap;
}

////////////////////////////////////////////////////////////////////////////////
vector<LedgerEntry> ScrAddrObj::getHistoryPageById(uint32_t id)
{
   if (id > hist_.getPageCount())
      throw std::range_error("pageId out of range");

   auto getTxio = [this](
      uint32_t start, uint32_t end)->map<BinaryData, TxIOPair>
      { return this->getHistoryForScrAddr(start, end, false); };

   auto buildLedgers = [this](const map<BinaryData, TxIOPair>& txioMap,
      uint32_t start, uint32_t end)->map<BinaryData, LedgerEntry>
      { return this->updateLedgers(txioMap, start, end); };

   auto leMap = hist_.getPageLedgerMap(getTxio, buildLedgers, id, updateID_);
   return getTxLedgerAsVector(leMap.get());
}

////////////////////////////////////////////////////////////////////////////////
void ScrAddrObj::mapHistory()
{
   //create history map
   auto getSummary = [this]()->map<uint32_t, uint32_t>
      { return db_->getSSHSummary(this->getScrAddr(), UINT32_MAX); };

   hist_.mapHistory(getSummary); 
}

////////////////////////////////////////////////////////////////////////////////
ScrAddrObj& ScrAddrObj::operator= (const ScrAddrObj& rhs)
{
   if (&rhs == this)
      return *this;

   this->db_ = rhs.db_;
   this->bc_ = rhs.bc_;

   this->scrAddr_ = rhs.scrAddr_;
   this->firstBlockNum_ = rhs.firstBlockNum_;
   this->firstTimestamp_ = rhs.firstTimestamp_;
   this->lastBlockNum_ = rhs.lastBlockNum_;
   this->lastTimestamp_ = rhs.lastTimestamp_;

   this->hasMultisigEntries_ = rhs.hasMultisigEntries_;

   this->relevantTxIO_ = rhs.relevantTxIO_;

   this->totalTxioCount_ = rhs.totalTxioCount_;
   this->lastSeenBlock_ = rhs.lastSeenBlock_;

   //prebuild history indexes for quick fetch from ssh
   this->hist_ = rhs.hist_;
   this->utxos_.reset();
   this->utxos_.scrAddrObj_ = this;
   
   return *this;
}

////////////////////////////////////////////////////////////////////////////////
vector<LedgerEntry> ScrAddrObj::getTxLedgerAsVector(
   const map<BinaryData, LedgerEntry>* leMap) const
{
   vector<LedgerEntry>le;

   if (leMap == nullptr)
      return le;

   for (auto& lePair : *leMap)
      le.push_back(lePair.second);

   return le;
}

////////////////////////////////////////////////////////////////////////////////
bool ScrAddrObj::getMoreUTXOs(function<bool(const BinaryData&)> spentByZC)
{
   return getMoreUTXOs(utxos_, spentByZC);
}

////////////////////////////////////////////////////////////////////////////////
bool ScrAddrObj::getMoreUTXOs(pagedUTXOs& utxos,
   function<bool(const BinaryData&)> spentByZC) const
{
   return utxos.fetchMoreUTXO(spentByZC);
}

////////////////////////////////////////////////////////////////////////////////
vector<UnspentTxOut> ScrAddrObj::getAllUTXOs(
   function<bool(const BinaryData&)> hasTxOutInZC) const
{
   pagedUTXOs utxos(this);
   while (getMoreUTXOs(utxos, hasTxOutInZC));

   //start a RO txn to grab the txouts from DB
   auto&& tx = db_->beginTransaction(STXO, LMDB::ReadOnly);

   vector<UnspentTxOut> utxoList;
   uint32_t blk = bc_->top()->getBlockHeight();

   for (const auto& txioPair : utxos.utxoList_)
   {
      if (!txioPair.second.isSpendable(db_, blk))
         continue;

      TxOut txout = txioPair.second.getTxOutCopy(db_);
      UnspentTxOut UTXO = UnspentTxOut(db_, txout, blk);
      utxoList.push_back(UTXO);
   }

   return move(utxoList);
}

////////////////////////////////////////////////////////////////////////////////
vector<UnspentTxOut> ScrAddrObj::getFullTxOutList(uint32_t currBlk,
   bool ignoreZc) const
{
   if (currBlk == 0)
      currBlk = UINT32_MAX;

   if (currBlk != UINT32_MAX)
      ignoreZc = true;

   auto utxoVec = getSpendableTxOutList(ignoreZc);

   auto utxoIter = utxoVec.rbegin();
   uint32_t cutOff = UINT32_MAX;

   while (utxoIter != utxoVec.rend())
   {
      if (utxoIter->getTxHeight() <= currBlk)
      {
         cutOff = utxoVec.size() - (utxoIter - utxoVec.rbegin());
         break;
      }
   }

   utxoVec.erase(utxoVec.begin() + cutOff, utxoVec.end());

   return utxoVec;
}

////////////////////////////////////////////////////////////////////////////////
vector<UnspentTxOut> ScrAddrObj::getSpendableTxOutList(
   bool ignoreZc) const
{
   //deliberately slow, only trying to support the old bdm behavior until the
   //Python side has been reworked to ask for paged UTXO history



   StoredScriptHistory ssh;
   map<BinaryData, UnspentTxOut> utxoMap;
   db_->getStoredScriptHistory(ssh, scrAddr_);
   db_->getFullUTXOMapForSSH(ssh, utxoMap, false);

   vector<UnspentTxOut> utxoVec;

   for (auto& utxo : utxoMap)
   {
      auto txioIter = relevantTxIO_.find(utxo.first);
      if (txioIter != relevantTxIO_.end())
         if (txioIter->second.hasTxInZC())
            continue;

      utxoVec.push_back(utxo.second);
   }

   if (ignoreZc)
      return utxoVec;

   auto&& tx = db_->beginTransaction(STXO, LMDB::ReadOnly);

   for (auto& txio : relevantTxIO_)
   {
      if (!txio.second.hasTxOutZC())
         continue;
      if (txio.second.hasTxInZC())
         continue;

      TxOut txout = txio.second.getTxOutCopy(db_);
      uint32_t blk = DBUtils::hgtxToHeight(
         txio.second.getDBKeyOfOutput().getSliceCopy(0, 4));
      UnspentTxOut UTXO = UnspentTxOut(db_, txout, blk);

      utxoVec.push_back(UTXO);
   }

   return utxoVec;
}

////////////////////////////////////////////////////////////////////////////////
uint32_t ScrAddrObj::getBlockInVicinity(uint32_t blk) const
{
   //expect history has been computed, it will throw otherwise
   return hist_.getBlockInVicinity(blk);
}

////////////////////////////////////////////////////////////////////////////////
uint32_t ScrAddrObj::getPageIdForBlockHeight(uint32_t blk) const
{
   //same as above
   return hist_.getPageIdForBlockHeight(blk);
}
