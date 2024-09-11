////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ZeroConfUtils.h"
#include "ZeroConfNotifications.h"
#include "BlockchainDatabase/ScrAddrFilter.h"
#include "BlockchainDatabase/lmdb_wrapper.h"

using namespace std;
using namespace Armory::Config;

///////////////////////////////////////////////////////////////////////////////
void preprocessTx(ParsedTx& tx, LMDBBlockDatabase* db)
{
   /*
   Resolves mined outpoints and sets reference fields.
   
   exhaustiveSearch affects the txhash resolution process. This argument has
   no effect in supernode.
    - false: check narrow set of hashes from known wallet's transaction
    - true: resolve hash through txhint db
   */
   auto& txHash = tx.getTxHash();
   auto&& txref = db->getTxRef(txHash);

   if (txref.isInitialized())
   {
      tx.state_ = ParsedTxStatus::Mined;
      return;
   }

   uint8_t const * txStartPtr = tx.tx_.getPtr();
   unsigned len = tx.tx_.getSize();

   auto nTxIn = tx.tx_.getNumTxIn();
   auto nTxOut = tx.tx_.getNumTxOut();

   //try to resolve as many outpoints as we can. unresolved outpoints are 
   //either invalid or (most likely) children of unconfirmed transactions
   if (nTxIn != tx.inputs_.size())
   {
      tx.inputs_.clear();
      tx.inputs_.resize(nTxIn);
   }

   if (nTxOut != tx.outputs_.size())
   {
      tx.outputs_.clear();
      tx.outputs_.resize(nTxOut);
   }

   for (uint32_t iin = 0; iin < nTxIn; iin++)
   {
      auto& txIn = tx.inputs_[iin];
      if (txIn.isResolved())
         continue;

      auto& opRef = txIn.opRef_;

      if (!opRef.isInitialized())
      {
         auto offset = tx.tx_.getTxInOffset(iin);
         if (offset > len)
            throw runtime_error("invalid txin offset");
         BinaryDataRef inputDataRef(txStartPtr + offset, len - offset);
         opRef.unserialize(inputDataRef);
      }

      if (!opRef.isResolved())
      {
         //resolve outpoint to dbkey
         txIn.opRef_.resolveDbKey(db);
         if (!opRef.isResolved())
            continue;
      }

      //grab txout
      StoredTxOut stxOut;
      if (!db->getStoredTxOut(stxOut, opRef.getDbKey()))
         continue;

      if (DBSettings::getDbType() == ARMORY_DB_SUPER)
         opRef.getDbKey() = stxOut.getDBKey(false);

      if (stxOut.isSpent())
      {
         tx.state_ = ParsedTxStatus::Invalid;
         return;
      }

      //set txin address and value
      txIn.scrAddr_ = stxOut.getScrAddress();
      txIn.value_ = stxOut.getValue();
   }

   for (uint32_t iout = 0; iout < nTxOut; iout++)
   {
      auto& txOut = tx.outputs_[iout];
      if (txOut.isInitialized())
         continue;

      auto offset = tx.tx_.getTxOutOffset(iout);
      auto len = tx.tx_.getTxOutOffset(iout + 1) - offset;

      BinaryRefReader brr(txStartPtr + offset, len);
      txOut.value_ = brr.get_uint64_t();

      auto scriptLen = brr.get_var_int();
      auto scriptRef = brr.get_BinaryDataRef(scriptLen);
      txOut.scrAddr_ = move(BtcUtils::getTxOutScrAddr(scriptRef));

      txOut.offset_ = offset;
      txOut.len_ = len;
   }

   tx.isRBF_ = tx.tx_.isRBF();


   bool txInResolved = true;
   for (auto& txin : tx.inputs_)
   {
      if (txin.isResolved())
         continue;

      txInResolved = false;
      break;
   }

   if (!txInResolved)
      tx.state_ = ParsedTxStatus::Unresolved;
   else
      tx.state_ = ParsedTxStatus::Resolved;
}

///////////////////////////////////////////////////////////////////////////////
void preprocessZcMap(
   const map<BinaryData, shared_ptr<ParsedTx>>& zcMap,
   LMDBBlockDatabase* db)
{
   //run threads to preprocess the zcMap
   auto counter = make_shared<atomic<unsigned>>();
   counter->store(0, memory_order_relaxed);

   vector<shared_ptr<ParsedTx>> txVec;
   txVec.reserve(zcMap.size());

   for (const auto& txPair : zcMap)
      txVec.push_back(txPair.second);

   auto parserLdb = [db, &txVec, counter](void)->void
   {
      while (1)
      {
         auto id = counter->fetch_add(1, memory_order_relaxed);
         if (id >= txVec.size())
            return;

         auto txIter = txVec.begin() + id;
         preprocessTx(*(*txIter), db);
      }
   };

   vector<thread> parserThreads;
   for (unsigned i = 1; i < thread::hardware_concurrency(); i++)
      parserThreads.push_back(thread(parserLdb));
   parserLdb();

   for (auto& thr : parserThreads)
   {
      if (thr.joinable())
         thr.join();
   }
}

///////////////////////////////////////////////////////////////////////////////
void finalizeParsedTxResolution(
   shared_ptr<ParsedTx> parsedTxPtr,
   LMDBBlockDatabase* db, const set<BinaryData>& allZcHashes,
   shared_ptr<MempoolSnapshot> ss)
{
   auto& parsedTx = *parsedTxPtr;
   bool isRBF = parsedTx.isRBF_;
   bool isChained = parsedTx.isChainedZc_;

   //if parsedTx has unresolved outpoint, they are most likely ZC
   for (auto& input : parsedTx.inputs_)
   {
      if (input.isResolved())
      {
         //check resolved key is valid
         if (input.opRef_.isZc())
         {
            isChained = true;
            auto chainedZC = ss->getTxByKey(input.opRef_.getDbTxKeyRef());
            if (chainedZC == nullptr)
            {
               parsedTx.state_ = ParsedTxStatus::Invalid;
               return;
            }
            else if (chainedZC->status() == ParsedTxStatus::Invalid)
            {
               throw runtime_error("invalid parent zc");
            }
         }
         else
         {
            auto&& keyRef = input.opRef_.getDbKey().getSliceRef(0, 4);
            auto height = DBUtils::hgtxToHeight(keyRef);
            auto dupId = DBUtils::hgtxToDupID(keyRef);

            if (db->getValidDupIDForHeight(height) != dupId)
            {
               parsedTx.state_ = ParsedTxStatus::Invalid;
               return;
            }
         }

         continue;
      }

      auto& opZcKey = input.opRef_.getDbKey();
      opZcKey = ss->getKeyForHash(input.opRef_.getTxHashRef());
      if (opZcKey.empty())
      {
         if (DBSettings::getDbType() == ARMORY_DB_SUPER ||
            allZcHashes.find(input.opRef_.getTxHashRef()) == allZcHashes.end())
            continue;
      }

      isChained = true;

      auto chainedZC = ss->getTxByKey(opZcKey);
      if (chainedZC == nullptr)
         continue;

      //NOTE: avoid the copy
      auto chainedTxOut = chainedZC->tx_.getTxOutCopy(input.opRef_.getIndex());

      input.value_ = chainedTxOut.getValue();
      input.scrAddr_ = chainedTxOut.getScrAddressStr();
      isRBF |= chainedZC->tx_.isRBF();
      input.opRef_.setTime(chainedZC->tx_.getTxTime());

      opZcKey.append(WRITE_UINT16_BE(input.opRef_.getIndex()));
   }

   //check & update resolution state
   if (parsedTx.state_ != ParsedTxStatus::Resolved)
   {
      bool isResolved = true;
      for (auto& input : parsedTx.inputs_)
      {
         if (!input.isResolved())
         {
            isResolved = false;
            break;
         }
      }

      if (isResolved)
         parsedTx.state_ = ParsedTxStatus::Resolved;
   }

   parsedTx.isRBF_ = isRBF;
   parsedTx.isChainedZc_ = isChained;
}

///////////////////////////////////////////////////////////////////////////////
FilteredZeroConfData filterParsedTx(
   shared_ptr<ParsedTx> parsedTxPtr,
   const map<BinaryData, shared_ptr<AddrAndHash>>& mainAddressMap,
   ZeroConfCallbacks* bdvCallbacks)
{
   auto& parsedTx = *parsedTxPtr;
   auto zcKey = parsedTxPtr->getKeyRef();

   FilteredZeroConfData result; 
   result.txPtr_ = parsedTxPtr;
   auto& txHash = parsedTx.getTxHash();

   auto filter = [mainAddressMap, bdvCallbacks]
      (const BinaryData& addr)->pair<bool, set<string>>
   {
      pair<bool, set<string>> flaggedBDVs;
      flaggedBDVs.first = false;

      
      //Check if this address is being watched before looking for specific BDVs
      auto addrIter = mainAddressMap.find(addr.getRef());
      if (addrIter == mainAddressMap.end())
      {
         if (DBSettings::getDbType() == ARMORY_DB_SUPER)
         {
            /*
            We got this far because no BDV is watching this address and the DB
            is running as a supernode. In supernode we track all ZC regardless 
            of watch status. Flag as true to process the ZC, but do not attach
            a bdv ID as no clients will be notified of this zc.
            */
            flaggedBDVs.first = true;
         }

         return flaggedBDVs;
      }

      flaggedBDVs.first = true;
      flaggedBDVs.second = bdvCallbacks->hasScrAddr(addr.getRef());
      return flaggedBDVs;
   };

   auto insertNewZc = [&result](const BinaryData& sa,
      BinaryData txiokey, shared_ptr<TxIOPair> txio,
      set<string> flaggedBDVs, bool consumesTxOut)->void
   {
      if (consumesTxOut)
         result.txOutsSpentByZC_.insert(txiokey);

      auto& key_txioPair = result.scrAddrTxioMap_[sa];
      key_txioPair[txiokey] = move(txio);

      for (auto& bdvId : flaggedBDVs)
         result.flaggedBDVs_[bdvId].scrAddrs_.insert(sa);
   };

   //spent txios
   unsigned iin = 0;
   for (auto& input : parsedTx.inputs_)
   {
      bool skipTxIn = false;
      auto inputId = iin++;
      if (!input.isResolved())
      {
         if (DBSettings::getDbType() == ARMORY_DB_SUPER)
         {
            parsedTx.state_ = ParsedTxStatus::Invalid;
            return result;
         }
         else
         {
            parsedTx.state_ = ParsedTxStatus::ResolveAgain;
         }

         skipTxIn = true;
      }

      //keep track of all outputs this ZC consumes
      auto& id_map = result.outPointsSpentByKey_[input.opRef_.getTxHashRef()];
      id_map.insert(make_pair(input.opRef_.getIndex(), zcKey));

      if (skipTxIn)
         continue;

      auto&& flaggedBDVs = filter(input.scrAddr_);
      if (!parsedTx.isChainedZc_ && !flaggedBDVs.first)
         continue;

      auto txio = make_shared<TxIOPair>(
         TxRef(input.opRef_.getDbTxKeyRef()), input.opRef_.getIndex(),
         TxRef(zcKey), inputId);

      txio->setTxHashOfOutput(input.opRef_.getTxHashRef());
      txio->setTxHashOfInput(txHash);
      txio->setValue(input.value_);
      auto tx_time = input.opRef_.getTime();
      if (tx_time == UINT64_MAX)
         tx_time = parsedTx.tx_.getTxTime();
      txio->setTxTime(tx_time);
      txio->setRBF(parsedTx.isRBF_);
      txio->setChained(parsedTx.isChainedZc_);

      auto&& txioKey = txio->getDBKeyOfOutput();
      insertNewZc(input.scrAddr_, move(txioKey), move(txio),
         move(flaggedBDVs.second), true);

      auto& updateSet = result.keyToSpentScrAddr_[zcKey];
      if (updateSet == nullptr)
         updateSet = make_shared<set<BinaryDataRef>>();
      updateSet->insert(input.scrAddr_.getRef());
   }

   //funded txios
   unsigned iout = 0;
   for (auto& output : parsedTx.outputs_)
   {
      auto outputId = iout++;

      auto&& flaggedBDVs = filter(output.scrAddr_);
      if (flaggedBDVs.first)
      {
         auto txio = make_shared<TxIOPair>(TxRef(zcKey), outputId);

         txio->setValue(output.value_);
         txio->setTxHashOfOutput(txHash);
         txio->setTxTime(parsedTx.tx_.getTxTime());
         txio->setUTXO(true);
         txio->setRBF(parsedTx.isRBF_);
         txio->setChained(parsedTx.isChainedZc_);

         auto& fundedScrAddr = result.keyToFundedScrAddr_[zcKey];
         fundedScrAddr.insert(output.scrAddr_.getRef());

         auto&& txioKey = txio->getDBKeyOfOutput();
         insertNewZc(output.scrAddr_, move(txioKey),
            move(txio), move(flaggedBDVs.second), false);
      }
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
//
// FilteredZeroConfData
//
////////////////////////////////////////////////////////////////////////////////
bool FilteredZeroConfData::isValid() const
{
   if (txPtr_ == nullptr)
      return false;

   switch (DBSettings::getDbType())
   {
   case ARMORY_DB_SUPER:
      return txPtr_->status() == ParsedTxStatus::Resolved && !isEmpty();

   case ARMORY_DB_FULL:
   case ARMORY_DB_BARE:
   {
      if (txPtr_->status() == ParsedTxStatus::Invalid ||
         txPtr_->status() == ParsedTxStatus::Mined ||
         txPtr_->status() == ParsedTxStatus::Unresolved)
      {
         return false;
      }

      return !isEmpty();
   }

   default:
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
//
// OutPointRef
//
////////////////////////////////////////////////////////////////////////////////
void OutPointRef::unserialize(uint8_t const * ptr, uint32_t remaining)
{
   if (remaining < 36)
      throw runtime_error("ptr is too short to be an outpoint");

   BinaryDataRef bdr(ptr, remaining);
   BinaryRefReader brr(bdr);

   txHash_ = brr.get_BinaryDataRef(32);
   txOutIndex_ = brr.get_uint32_t();
}

////////////////////////////////////////////////////////////////////////////////
void OutPointRef::unserialize(BinaryDataRef bdr)
{
   unserialize(bdr.getPtr(), bdr.getSize());
}

////////////////////////////////////////////////////////////////////////////////
void OutPointRef::resolveDbKey(LMDBBlockDatabase *dbPtr)
{
   if (txHash_.getSize() == 0 || txOutIndex_ == UINT16_MAX)
      throw runtime_error("empty outpoint hash");

   auto key = dbPtr->getDBKeyForHash(txHash_);
   if (key.getSize() != 6)
      return;

   setDbKey(key);
}

////////////////////////////////////////////////////////////////////////////////
void OutPointRef::setDbKey(const BinaryData& key)
{
   BinaryWriter bw;
   bw.put_BinaryData(key);
   bw.put_uint16_t(txOutIndex_, BE);

   dbKey_ = bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef OutPointRef::getDbTxKeyRef() const
{
   if (!isResolved())
      throw runtime_error("unresolved outpoint key");

   return dbKey_.getSliceRef(0, 6);
}

////////////////////////////////////////////////////////////////////////////////
bool OutPointRef::isInitialized() const
{
   return txHash_.getSize() == 32 && txOutIndex_ != UINT16_MAX;
}

////////////////////////////////////////////////////////////////////////////////
void OutPointRef::reset(InputResolution mode)
{
   if (mode != InputResolution::Both)
   {
      if (isZc() && mode == InputResolution::Mined)
         return;
   }

   dbKey_.clear();
   time_ = UINT64_MAX;
}

////////////////////////////////////////////////////////////////////////////////
bool OutPointRef::isZc() const
{
   if (!isResolved())
      return false;

   return dbKey_.startsWith(DBUtils::ZeroConfHeader_);
}

////////////////////////////////////////////////////////////////////////////////
//
// ParsedTxIn
//
////////////////////////////////////////////////////////////////////////////////
bool ParsedTxIn::isResolved() const
{
   if (!opRef_.isResolved())
      return false;

   if (scrAddr_.getSize() == 0 || value_ == UINT64_MAX)
      return false;

   return true;
}

////////////////////////////////////////////////////////////////////////////////
//
// ParsedTx
//
////////////////////////////////////////////////////////////////////////////////
bool ParsedTx::isResolved() const
{
   if (state_ == ParsedTxStatus::Uninitialized)
      return false;

   if (!tx_.isInitialized())
      return false;

   if (inputs_.size() != tx_.getNumTxIn() ||
      outputs_.size() != tx_.getNumTxOut())
      return false;

   for (auto& input : inputs_)
   {
      if (!input.isResolved())
         return false;
   }

   return true;
}

////////////////////////////////////////////////////////////////////////////////
void ParsedTx::resetInputResolution(InputResolution mode)
{
   for (auto& input : inputs_)
      input.opRef_.reset(mode);

   if (mode != InputResolution::Mined)
      tx_.setChainedZC(false);

   state_ = ParsedTxStatus::Uninitialized;
   isRBF_ = false;
   isChainedZc_ = false;
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& ParsedTx::getTxHash(void) const
{
   if (txHash_.getSize() == 0)
      txHash_ = move(tx_.getThisHash());
   return txHash_;
}

///////////////////////////////////////////////////////////////////////////////
//
// MempoolData
//
///////////////////////////////////////////////////////////////////////////////
/***
Mempool data is a chain of objects with a front object and cascading parents.
Requested data is first fetched from the front object first then trough parents
in a descending fashion, until the value closest to the front is returned, or
no value is found.

In maps, a key match with an empty value signifies a deletion.

scrAddrMap_ is handled differently.
***/

///////////////////////////////////////////////////////////////////////////////
unsigned MempoolData::getParentCount() const
{
   auto parentPtr = parent_;
   unsigned count = 0;

   while (parentPtr != nullptr)
   {
      parentPtr = parentPtr->parent_;
      ++count;
   }

   return count;
}

///////////////////////////////////////////////////////////////////////////////
void MempoolData::copyFrom(const MempoolData& orig)
{
   txHashToDBKey_ = orig.txHashToDBKey_;
   txMap_ = orig.txMap_;
   txOutsSpentByZC_ = orig.txOutsSpentByZC_;
   scrAddrMap_ = orig.scrAddrMap_;
   txioMap_ = orig.txioMap_;

   parent_ = orig.parent_;
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<ParsedTx> MempoolData::getTx(BinaryDataRef key) const
{
   auto iter = txMap_.find(key);
   if (iter == txMap_.end())
   {
      if (parent_ != nullptr)
         return parent_->getTx(key);

      return nullptr;
   }

   return iter->second;
}

///////////////////////////////////////////////////////////////////////////////
BinaryDataRef MempoolData::getKeyForHash(BinaryDataRef hash) const
{
   auto iter = txHashToDBKey_.find(hash);
   if (iter == txHashToDBKey_.end())
   {
      if (parent_ != nullptr)
         return parent_->getKeyForHash(hash);

      return {};
   }

   return iter->second;
}

///////////////////////////////////////////////////////////////////////////////
/***
The scrAddrMap_ needs special handling:

   * This map carries the txio keys affecting each scrAddr. A same scrAddr may
     be affected by several parents so to get the true set of relevant txio
     keys, history across all parents needs to be merged together first. This 
     creates copies on each read. To avoid this, we "bring forward" the txio
     keys from the nearest parent when the front MempoolData is missing the
     requested for scrAddr.
***/
set<BinaryData>* MempoolData::getTxioKeysFromParent(
   BinaryDataRef scrAddr) const
{
   if (parent_ == nullptr)
      return nullptr;

   auto iter = parent_->scrAddrMap_.find(scrAddr);
   if (iter != parent_->scrAddrMap_.end())
      return &(iter->second);

   return parent_->getTxioKeysFromParent(scrAddr);
}

///////////////////////////////////////////////////////////////////////////////
set<BinaryData>& MempoolData::getTxioKeysForScrAddr_NoThrow(
   BinaryDataRef scrAddr)
{
   auto iter = scrAddrMap_.find(scrAddr);
   if (iter == scrAddrMap_.end())
   {
      //we don't have a key set for this scrAddr, look in parents
      auto parentSet = getTxioKeysFromParent(scrAddr);

      //copy the parent's set
      set<BinaryData> localKeySet;
      if (parentSet != nullptr)
         localKeySet = *parentSet;

      //insert into our own scrAddrMap whether we have a parent set or not
      auto insertIter = scrAddrMap_.emplace(scrAddr, move(localKeySet));

      //finally, set the iterator to the new entry
      iter = insertIter.first;
   }

   return iter->second;
}

///////////////////////////////////////////////////////////////////////////////
const set<BinaryData>& MempoolData::getTxioKeysForScrAddr(
   BinaryDataRef scrAddr) const
{
   auto iter = scrAddrMap_.find(scrAddr);
   if (iter == scrAddrMap_.end())
   {
      if (parent_ != nullptr)
         return parent_->getTxioKeysForScrAddr(scrAddr);

      throw range_error("");
   }

   if (iter->second.empty())
      throw range_error("");
   
   return iter->second;
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<const TxIOPair> MempoolData::getTxio(BinaryDataRef key) const
{
   auto iter = txioMap_.find(key);
   if (iter == txioMap_.end())
   {
      if (parent_ != nullptr)
         return parent_->getTxio(key);

      return nullptr;
   }

   return const_pointer_cast<TxIOPair>(iter->second);
}

///////////////////////////////////////////////////////////////////////////////
bool MempoolData::isTxOutSpentByZC(BinaryDataRef key) const
{
   auto iter = txOutsSpentByZC_.find(key);
   if (iter == txOutsSpentByZC_.end())
   {
      if (parent_ != nullptr)
         return parent_->isTxOutSpentByZC(key);

      return false;
   }

   return iter->second;
}

///////////////////////////////////////////////////////////////////////////////
void MempoolData::dropFromSpentTxOuts(BinaryDataRef key)
{
   bool inParents = false;
   if (parent_ != nullptr)
      inParents = parent_->isTxOutSpentByZC(key);

   if (!inParents)
   {
      txOutsSpentByZC_.erase(key);
      return;
   }

   auto iter = txOutsSpentByZC_.find(key);
   if (iter == txOutsSpentByZC_.end())
   {
      auto insertIter = txOutsSpentByZC_.emplace(key, false);
      iter = insertIter.first;
   }

   iter->second = false;
}

///////////////////////////////////////////////////////////////////////////////
void MempoolData::dropFromScrAddrMap(
   BinaryDataRef scrAddr, BinaryDataRef zcKey)
{
   //this scrAddr is funded by outputs from this zc, remove them
   auto& txioKeys = getTxioKeysForScrAddr_NoThrow(scrAddr);

   //look for txio keys belonging to our zc
   auto keyIter = txioKeys.lower_bound(zcKey);
   while (keyIter != txioKeys.end())
   {
      if (!keyIter->startsWith(zcKey))
         break;

      //remove all entries that begin with our zcKey
      txioKeys.erase(keyIter++);
   }
}

///////////////////////////////////////////////////////////////////////////////
void MempoolData::dropTxHashToDBKey(BinaryDataRef hash)
{
   bool hasHash = false;
   if (parent_ != nullptr)
   {
      auto key = parent_->getKeyForHash(hash);
      if (!key.empty())
         hasHash = true;
   }

   if (!hasHash)
   {
      txHashToDBKey_.erase(hash);
      return;
   }

   auto iter = txHashToDBKey_.find(hash);
   if (iter == txHashToDBKey_.end())
   {
      auto insertIter = txHashToDBKey_.emplace(hash, BinaryDataRef());
      iter = insertIter.first;
   }

   iter->second.reset();
}

///////////////////////////////////////////////////////////////////////////////
void MempoolData::dropTxiosForZC(BinaryDataRef key)
{
   auto zcPtr = getTx(key);
   if (zcPtr == nullptr)
      throw range_error("");

   for (unsigned i=0; i<zcPtr->outputs_.size(); i++)
   {
      BinaryWriter bw(8);
      bw.put_BinaryData(key);
      bw.put_uint16_t(i, BE);

      txioMap_[bw.getData()] = nullptr;
   }
}

///////////////////////////////////////////////////////////////////////////////
void MempoolData::dropTxioInputs(
   BinaryDataRef zcKey,
   const set<BinaryData>& spentFromTxoutKeys)
{
   for (auto& spentTxoutKey : spentFromTxoutKeys)
   {
      //look up the spendee by key
      auto txioPtr = getTxio(spentTxoutKey);
      if (txioPtr == nullptr)
         throw range_error("");

      //does this txio have a spender and is it our tx?
      if (!txioPtr->hasTxIn() || 
         txioPtr->getTxRefOfInput().getDBKeyRef() != zcKey)
         continue;

      if (!txioPtr->hasTxOutZC())
      {
         //if the txout is mined, remove it entirely
         txioMap_[spentTxoutKey] = nullptr;
      }
      else
      {
         /*
         copy the txio, remove the txin and replace it in the map
         (so as to not disrupt the potential readers)
         */
         auto newTxio = make_shared<TxIOPair>(*(txioPtr));
         newTxio->setTxIn(BinaryData());
         txioMap_[spentTxoutKey] = newTxio;
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void MempoolData::dropTx(BinaryDataRef key)
{
   bool inParents = false;
   if (parent_ != nullptr)
   {
      auto txPtr = parent_->getTx(key);
      if (txPtr != nullptr)
         inParents = true;
   }

   if (!inParents)
   {
      txMap_.erase(key);
      return;
   }

   auto iter = txMap_.find(key);
   if (iter == txMap_.end())
   {
      auto insertIter = txMap_.emplace(key, nullptr);
      iter = insertIter.first;
   }

   iter->second = nullptr;
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<MempoolData> MempoolData::mergeWithParent(
   shared_ptr<MempoolData> ptr)
{
   if (ptr->parent_ == nullptr)
      return ptr;

   auto newObj = make_shared<MempoolData>();
   
   //tx hashes
   {
      newObj->txHashToDBKey_ = ptr->txHashToDBKey_;
      for (const auto& hashPair : ptr->parent_->txHashToDBKey_)
         newObj->txHashToDBKey_.emplace(hashPair);

      auto hashIter = newObj->txHashToDBKey_.begin();
      while (hashIter != newObj->txHashToDBKey_.end())
      {
         if (hashIter->second.empty())
         {
            if (ptr->parent_->parent_ != nullptr)
            {
               if (ptr->parent_->parent_->getKeyForHash(hashIter->first).empty())
               {
                  newObj->txHashToDBKey_.erase(hashIter++);
                  continue;
               }
            }
         }

         ++hashIter;
      }
   }

   //tx map
   {
      newObj->txMap_ = ptr->txMap_;
      for (const auto& txPair : ptr->parent_->txMap_)
         newObj->txMap_.emplace(txPair);

      auto txIter = newObj->txMap_.begin();
      while (txIter != newObj->txMap_.end())
      {
         if (txIter->second == nullptr)
         {
            if (ptr->parent_->parent_ != nullptr)
            {
               if (ptr->parent_->parent_->getTx(txIter->first) == nullptr)
               {
                  newObj->txMap_.erase(txIter++);
                  continue;
               }
            }
         }

         ++txIter;
      }
   }

   //txouts spentness
   {
      newObj->txOutsSpentByZC_ = ptr->txOutsSpentByZC_;
      for (const auto& keyPair : ptr->parent_->txOutsSpentByZC_)
         newObj->txOutsSpentByZC_.emplace(keyPair);

      auto keyIter = newObj->txOutsSpentByZC_.begin();
      while (keyIter != newObj->txOutsSpentByZC_.end())
      {
         if (!keyIter->second)
         {
            if (ptr->parent_->parent_ != nullptr)
            {
               if (!ptr->parent_->parent_->isTxOutSpentByZC(keyIter->first))
               {
                  newObj->txOutsSpentByZC_.erase(keyIter++);
                  continue;
               }
            }
         }

         ++keyIter;
      }
   }

   //scrAddr map
   {
      newObj->scrAddrMap_ = ptr->scrAddrMap_;
      for (const auto& addrPair : ptr->parent_->scrAddrMap_)
         newObj->scrAddrMap_.emplace(addrPair);

      auto addrIter = newObj->scrAddrMap_.begin();
      while (addrIter != newObj->scrAddrMap_.end())
      {
         if (addrIter->second.empty())
         {
            if (ptr->parent_->parent_ != nullptr)
            {
               try
               {
                  ptr->parent_->parent_->getTxioKeysForScrAddr(addrIter->first);
               }
               catch (range_error&)
               {
                  newObj->scrAddrMap_.erase(addrIter++);
                  continue;
               }
            }
         }

         ++addrIter;
      }
   }

   //txio map
   {
      newObj->txioMap_ = ptr->txioMap_;
      for (const auto& txioPair : ptr->parent_->txioMap_)
         newObj->txioMap_.emplace(txioPair);
      
      auto txioIter = newObj->txioMap_.begin();
      while (txioIter != newObj->txioMap_.end())
      {
         if (txioIter->second == nullptr)
         {
            if (ptr->parent_->parent_ != nullptr)
            {
               if (ptr->parent_->parent_->getTxio(txioIter->first) == nullptr)
               {
                  newObj->txioMap_.erase(txioIter++);
                  continue;
               }
            }
         }

         ++txioIter;
      }
   }

   newObj->parent_ = ptr->parent_->parent_;
   return newObj;
}

///////////////////////////////////////////////////////////////////////////////
//
// MempoolSnapshot
//
///////////////////////////////////////////////////////////////////////////////
MempoolSnapshot::MempoolSnapshot(unsigned depth, unsigned threshold) :
   depth_(depth), threshold_(threshold)
{
   data_ = make_shared<MempoolData>();
}

///////////////////////////////////////////////////////////////////////////////
void MempoolSnapshot::preprocessZcMap(LMDBBlockDatabase* db)
{
   ::preprocessZcMap(data_->txMap_, db);
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<ParsedTx> MempoolSnapshot::getTxByKey_NoConst(
   BinaryDataRef key) const
{
   return data_->getTx(key);
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<const ParsedTx> MempoolSnapshot::getTxByKey(
   BinaryDataRef key) const
{
   auto txPtr = getTxByKey_NoConst(key);
   return const_pointer_cast<const ParsedTx>(txPtr);
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<const ParsedTx> MempoolSnapshot::getTxByHash(
   BinaryDataRef hash) const
{
   auto key = getKeyForHash(hash);
   if (key.empty())
      return nullptr;

   return getTxByKey(key);
}

///////////////////////////////////////////////////////////////////////////////
TxOut MempoolSnapshot::getTxOutCopy(
   BinaryDataRef key, uint16_t outputId) const
{
   auto txPtr = getTxByKey(key);
   if (txPtr == nullptr)
      throw range_error("invalid zc key");

   if (outputId >= txPtr->outputs_.size())
      throw range_error("invalid output id");

   return txPtr->tx_.getTxOutCopy(outputId);
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<const TxIOPair> MempoolSnapshot::getTxioByKey(
   BinaryDataRef txioKey) const
{
   return data_->getTxio(txioKey);
}

///////////////////////////////////////////////////////////////////////////////
BinaryDataRef MempoolSnapshot::getKeyForHash(
   BinaryDataRef hash) const
{
   return data_->getKeyForHash(hash);
}

///////////////////////////////////////////////////////////////////////////////
BinaryDataRef MempoolSnapshot::getHashForKey(
   BinaryDataRef key) const
{
   auto txPtr = getTxByKey(key);
   if (txPtr == nullptr)
      return BinaryDataRef();

   return txPtr->getTxHash().getRef();
}

///////////////////////////////////////////////////////////////////////////////
uint32_t MempoolSnapshot::getTopZcID(void) const
{
   return topID_;
}

///////////////////////////////////////////////////////////////////////////////
bool MempoolSnapshot::hasHash(BinaryDataRef hash) const
{
   return !(data_->getKeyForHash(hash).empty());
}

///////////////////////////////////////////////////////////////////////////////
bool MempoolSnapshot::isTxOutSpentByZC(BinaryDataRef key) const
{
   return data_->isTxOutSpentByZC(key);
}

///////////////////////////////////////////////////////////////////////////////
const set<BinaryData>& MempoolSnapshot::getTxioKeysForScrAddr(
   BinaryDataRef scrAddr) const
{
   return data_->getTxioKeysForScrAddr(scrAddr);
}

///////////////////////////////////////////////////////////////////////////////
map<BinaryDataRef, shared_ptr<const TxIOPair>> 
   MempoolSnapshot::getTxioMapForScrAddr(BinaryDataRef scrAddr) const
{
   try
   {
      const auto& txioKeys = getTxioKeysForScrAddr(scrAddr);
      map<BinaryDataRef, shared_ptr<const TxIOPair>> result;

      for (const auto& txioKey : txioKeys)
      {
         auto txioPtr = getTxioByKey(txioKey);
         if (txioPtr == nullptr)
            continue;

         result.emplace(txioKey.getRef(), txioPtr);
      }

      return result;
   }
   catch (range_error&)
   {}

   return {};
}

///////////////////////////////////////////////////////////////////////////////
set<BinaryData> MempoolSnapshot::findChildren(BinaryDataRef zcKey)
{
   auto zcPtr = data_->getTx(zcKey);
   if (zcPtr == nullptr)
      throw range_error("");

   //set zcKeys of all ZC spending from our parent
   set<BinaryData> children;

   for (unsigned i=0; i<zcPtr->outputs_.size(); i++)
   {
      BinaryWriter bw(8);
      bw.put_BinaryData(zcKey);
      bw.put_uint16_t(i, BE);

      auto txioPtr = data_->getTxio(bw.getDataRef());

      if (txioPtr == nullptr)
         continue;

      //skip if this txio doesn't carry a txin (txout isn't spent)
      if (!txioPtr->hasTxIn())
         continue;

      //grab the txin's TxRef object
      auto spenderRef = txioPtr->getTxRefOfInput();

      //save the Tx key (key of the txin's owner)
      children.emplace(spenderRef.getDBKey());
   }

   return children;
}

///////////////////////////////////////////////////////////////////////////////
map<BinaryData, std::shared_ptr<ParsedTx>> MempoolSnapshot::dropZc(
   BinaryDataRef zcKey)
{
   auto txPtr = getTxByKey_NoConst(zcKey);
   if (txPtr == nullptr)
      return {};

   std::set<BinaryData> spentFromTxoutKeys;
   map<BinaryData, shared_ptr<ParsedTx>> droppedZc;

   //drop from spent set
   for (auto& input : txPtr->inputs_)
   {
      if (!input.isResolved())
         continue;
      data_->dropFromSpentTxOuts(input.opRef_.getDbKey());
      spentFromTxoutKeys.emplace(input.opRef_.getDbKey());

      //do not purge input keys from scrAddr map unless they're mined
      if (input.opRef_.getDbKey().startsWith(DBUtils::ZeroConfHeader_))
         continue;
      data_->dropFromScrAddrMap(input.scrAddr_, input.opRef_.getDbKey());
   }

   /*
   Find the children and drop them. A child evicted as a consequence
   of the parent's invalidation isn't necessarely invalid too, 
   the parent may just have been mined.

   Make sure eviction is followed by reparsing. The cost to reparse
   isn't so dire as to justify the complexity of changing txin 
   resolution on the fly only for the children.

   NOTE #1: the child purging atm is recursive and exhaustive. It could
   be improved if the reason for the eviction is specified: ZCs that
   are mined do not need their entire descendancy evicted from the mempool, 
   only the direct descendants need reparsed to point to the mined output
   instead of the unconfirmed ones.

   NOTE #2: the full reparsing of children will trigger undesirable
   ZC notifications, these should be suppressed. Only final eviction
   from the mempool should be notified to the BDV objects, on all
   occasions.
   */
   auto children = findChildren(zcKey);
   for (auto& child : children)
   {
      auto droppedTx = dropZc(child);
      droppedZc.insert(droppedTx.begin(), droppedTx.end());
   }

   //drop outputs from scrAddrMap
   for (auto& output : txPtr->outputs_)
      data_->dropFromScrAddrMap(output.scrAddr_, zcKey);


   //drop all txios this ZC created (where our tx holds the txout)
   data_->dropTxiosForZC(zcKey);

   //drop all spending from other txios (where our tx holds the txin)
   data_->dropTxioInputs(zcKey, spentFromTxoutKeys);

   //drop hash
   data_->dropTxHashToDBKey(txPtr->getTxHash().getRef());

   //delete tx
   data_->dropTx(zcKey);

   //save this tx as dropped from the mempool and return
   droppedZc.emplace(txPtr->getKeyRef(), txPtr);
   return droppedZc;
}

///////////////////////////////////////////////////////////////////////////////
void MempoolSnapshot::stageNewZC(
   shared_ptr<ParsedTx> zcPtr, const FilteredZeroConfData& filteredData)
{
   const auto& dbKey = zcPtr->getKey();
   const auto& txHash = zcPtr->getTxHash();

   //set tx and hash to key entry
   data_->txHashToDBKey_[txHash.getRef()] = dbKey.getRef();
   data_->txMap_[dbKey.getRef()] = zcPtr;

   //merge spent outpoints
   for (auto& txoutkey : filteredData.txOutsSpentByZC_)
      data_->txOutsSpentByZC_.emplace(txoutkey, true);

   //updated txio and scraddr maps
   for (auto& saTxios : filteredData.scrAddrTxioMap_)
   {
      auto& keySet = data_->getTxioKeysForScrAddr_NoThrow(saTxios.first);
      for (auto& txioPair : saTxios.second)
      {
         //add the txioKey to the affected scrAddr
         keySet.emplace(txioPair.first);

         //add to txio map
         data_->txioMap_[txioPair.first] = txioPair.second;
      }
   }

   BinaryReader brrKey(dbKey);
   brrKey.advance(2);
   auto zcId = brrKey.get_uint32_t(BE);
   if (zcId > topID_)
      topID_ = zcId;
}

///////////////////////////////////////////////////////////////////////////////
shared_ptr<MempoolSnapshot> MempoolSnapshot::copy(
   shared_ptr<MempoolSnapshot> ss, unsigned pool, unsigned threshold)
{
   auto ssCopy = make_shared<MempoolSnapshot>(pool, threshold);
   if (ss != nullptr)
   {
      ssCopy->topID_ = ss->topID_;
      ssCopy->mergeCount_ = ss->mergeCount_;
      ssCopy->data_->copyFrom(*ss->data_);
   }

   return ssCopy;
}

///////////////////////////////////////////////////////////////////////////////
void MempoolSnapshot::commitNewZCs()
{
   //figure out depth and size of each mempool obj, merge if necessary
   if (data_->txioMap_.empty() &&
      data_->scrAddrMap_.empty() &&
      data_->txioMap_.empty())
   {
      return;
   }

   auto newData = make_shared<MempoolData>();
   newData->parent_ = data_;

   if (newData->getParentCount() > depth_)
   {
      auto toMerge = newData->parent_;

      //find the first parent above the size threshold, use
      //first parent otherwise
      auto objPtr = toMerge;
      while (objPtr != nullptr)
      {
         if (objPtr->txioMap_.size() > threshold_ && objPtr->parent_ != nullptr)
         {
            toMerge = objPtr;
            break;
         }

         objPtr = objPtr->parent_;
      }

      //merge this data with its parent
      if (toMerge != nullptr)
      {
         auto mergedData = MempoolData::mergeWithParent(toMerge);

         //replace parent with the newly merged data
         auto objPtr = newData;
         while (objPtr != nullptr)
         {
            if (objPtr->parent_ == toMerge)
            {
               objPtr->parent_ = mergedData;
               break;
            }

            objPtr = objPtr->parent_;
         }

         ++mergeCount_;
      }
   }

   data_ = move(newData);
}