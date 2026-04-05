////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Utils.h"
#include <Utils/BtcUtils.h>
#include <Utils/DBUtils.h>
#include <Utils/ArmoryConfig.h>
#include <BlockchainDatabase/ScrAddrFilter.h>
#include <BlockchainDatabase/lmdb_wrapper.h>
#include <BlockchainDatabase/BlockObj.h>
#include <BlockchainDatabase/txio.h>
#include <BlockchainDatabase/StoredBlockObj.h>
#include <BlockchainDatabase/Blockchain.h>
#include "TxClasses.h"

#include "Notifications.h"

using namespace Armory::ZeroConf;
using namespace Armory;

////////////////////////////////////////////////////////////////////////////////
// namespace local functions
FilteredZeroConfData ZeroConf::filterParsedTx(
   std::shared_ptr<ParsedTx> parsedTxPtr,
   const std::function<bool(const BinaryData&)>& addrFilter,
   ZeroConfCallbacks* bdvCallbacks)
{
   FilteredZeroConfData result;
   result.txPtr = parsedTxPtr;
   auto zcKey = parsedTxPtr->getKeyRef();

   auto filter = [&addrFilter, bdvCallbacks]
      (const BinaryData& addr)->std::pair<bool, std::set<BdvIdKey>>
   {
      std::pair<bool, std::set<BdvIdKey>> flaggedBDVs;
      flaggedBDVs.first = false;

      //Check if this address is being watched before looking
      //for specific BDVs
      if (!addrFilter(addr)) {
         if (Config::DBSettings::getDbType() == ARMORY_DB_TYPE::Super) {
            /*
            We got this far because no BDV is watching this address and the
            DB is running as a supernode. In supernode we track all ZC
            regardless of watch status. Flag as true to process the ZC, but
            do not attach a bdv ID as no clients will be notified of this zc.
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
      BinaryData txiokey, std::shared_ptr<TxIOPair> txio,
      std::set<BdvIdKey> flaggedBDVs, bool consumesTxOut)->void
   {
      if (consumesTxOut) {
         result.txOutsSpentByZC.emplace(txiokey);
      }
      auto& key_txioPair = result.scrAddrTxioMap[sa];
      key_txioPair[txiokey] = std::move(txio);
      for (auto& bdvId : flaggedBDVs) {
         result.flaggedBDVs[bdvId].scrAddrs.emplace(sa);
      }
   };

   //spent txios
   uint32_t timeFromTx = UINT32_MAX;
   try {
      timeFromTx = parsedTxPtr->getTxObj().getTxTime();
   } catch (const std::runtime_error&) {
      //tx isn't set, ignore
   }

   unsigned iin = 0;
   for (const auto& input : parsedTxPtr->inputs) {
      bool skipTxIn = false;
      auto inputId = iin++;
      if (!input.isResolved()) {
         if (Config::DBSettings::getDbType() == ARMORY_DB_TYPE::Super) {
            parsedTxPtr->state = ParsedTxStatus::Invalid;
            return result;
         } else {
            parsedTxPtr->state = ParsedTxStatus::ResolveAgain;
         }
         skipTxIn = true;
      }

      //keep track of all outputs this ZC consumes
      auto& id_map = result.outPointsSpentByKey[input.opRef.getTxHashRef()];
      id_map.emplace(input.opRef.getIndex(), zcKey);

      //
      if (skipTxIn) {
         continue;
      }

      auto flaggedBDVs = filter(input.scrAddr);
      if (!parsedTxPtr->isChainedZc && !flaggedBDVs.first) {
         continue;
      }

      auto txio = std::make_shared<TxIOPair>(
         TxRef{input.opRef.getDbTxKeyRef()},
         input.opRef.getIndex(),
         input.value
      );

      txio->setTxIn(TxRef{zcKey}, inputId);
      auto txTime = input.opRef.getTime();
      if (txTime == UINT64_MAX) {
         txTime = timeFromTx;
      }
      txio->setTxTime(txTime);
      txio->setRBF(parsedTxPtr->isRBF);
      txio->setChained(parsedTxPtr->isChainedZc);

      auto txioKey = txio->getDBKeyOfOutput();
      insertNewZc(
         input.scrAddr,
         std::move(txioKey),
         std::move(txio),
         std::move(flaggedBDVs.second),
         true
      );

      auto& updateSet = result.keyToSpentScrAddr[zcKey];
      if (updateSet == nullptr) {
         updateSet = std::make_shared<std::set<BinaryDataRef>>();
      }
      updateSet->emplace(input.scrAddr.getRef());
   }

   //funded txios
   for (unsigned iout = 0; iout < parsedTxPtr->outputs.size(); iout++) {
      /*
      NOTE: there is a potential issue with this filtering. If a chained zc
         affects a zc previously filtered out here, how is that handled?
      */
      const auto& output = parsedTxPtr->outputs[iout];
      auto flaggedBDVs = filter(output.scrAddr);
      if (flaggedBDVs.first) {
         auto txio = std::make_shared<TxIOPair>(
            TxRef{zcKey}, iout, output.value);
         txio->setTxTime(timeFromTx);
         txio->setUTXO(true);
         txio->setRBF(parsedTxPtr->isRBF);
         txio->setChained(parsedTxPtr->isChainedZc);

         auto& fundedScrAddr = result.keyToFundedScrAddr[zcKey];
         fundedScrAddr.emplace(output.scrAddr.getRef());

         auto txioKey = txio->getDBKeyOfOutput();
         insertNewZc(output.scrAddr,
            std::move(txioKey),
            std::move(txio),
            std::move(flaggedBDVs.second),
            false
         );
      }
   }
   return result;
}

void ZeroConf::preprocessTx(ParsedTx& tx, LMDBBlockDatabase* db,
   Blockchain* bc)
{
   /*
   Resolves mined outpoints and sets reference fields.
   */

   const auto& txHash = tx.getTxHash();
   try {
      //sanity check: is this tx mined?
      db->getTxRef(txHash);
      tx.state = ParsedTxStatus::Mined;
      return;
   } catch (const std::exception&) {
      //tx isn't mined, proceed further
   }

   const auto& txObj = tx.getTxObj();
   const uint8_t* txStartPtr = txObj.getPtr();
   unsigned len = txObj.getSize();

   auto nTxIn = txObj.getNumTxIn();
   auto nTxOut = txObj.getNumTxOut();

   //try to resolve as many outpoints as we can. unresolved outpoints are
   //either invalid or (most likely) children of unconfirmed transactions
   if (nTxIn != tx.inputs.size()) {
      tx.inputs.clear();
      tx.inputs.resize(nTxIn);
   }

   if (nTxOut != tx.outputs.size()) {
      tx.outputs.clear();
      tx.outputs.resize(nTxOut);
   }

   auto isSuper = db->getDbType() == ARMORY_DB_TYPE::Super ? true : false;
   for (uint32_t iin = 0; iin < nTxIn; iin++) {
      auto& txIn = tx.inputs[iin];
      if (txIn.isResolved()) {
         continue;
      }

      auto& opRef = txIn.opRef;
      if (!opRef.isInitialized()) {
         auto offset = txObj.getTxInOffset(iin);
         if (offset > len) {
            throw std::runtime_error("invalid txin offset");
         }
         BinaryDataRef inputDataRef(txStartPtr + offset, len - offset);
         opRef.unserialize(inputDataRef);
      }

      if (!opRef.isResolved()) {
         //resolve outpoint to dbkey
         opRef.resolveDbKey(db);
         if (!opRef.isResolved()) {
            continue;
         }
      }

      //grab txout
      StoredTxOut stxOut;
      if (isSuper) {
         auto txOutKey = opRef.getDbKey();
         unsigned blockId; uint8_t dup; uint16_t txId; uint16_t txOutIdx;
         BinaryRefReader brrKey(txOutKey);
         DBUtils::readBlkDataKeyNoPrefix(brrKey, blockId, dup, txId, txOutIdx);
         auto header = bc->getHeaderForTxKey(txOutKey);
         db->getStoredTxOut(stxOut, header, txId, txOutIdx);
      } else {
         db->getStoredTxOut(stxOut, opRef.getDbKey());
      }
      if (!stxOut.isInitialized()) {
         continue;
      }

      if (isSuper) {
         auto stxOutKey = stxOut.getDBKey(false);
         opRef.setDbKey(stxOutKey.getSliceRef(0, 6));
      }

      if (stxOut.isSpent()) {
         tx.state = ParsedTxStatus::Invalid;
         return;
      }

      //set txin address and value
      txIn.scrAddr = stxOut.getScrAddress();
      txIn.value = stxOut.getValue();
   }

   for (uint32_t iout = 0; iout < nTxOut; iout++) {
      auto& txOut = tx.outputs[iout];
      if (txOut.isInitialized()) {
         continue;
      }

      auto offset = txObj.getTxOutOffset(iout);
      auto len = txObj.getTxOutOffset(iout + 1) - offset;

      BinaryRefReader brr(txStartPtr + offset, len);
      txOut.value = brr.get_uint64_t();

      auto scriptLen = brr.get_var_int();
      auto scriptRef = brr.get_BinaryDataRef(scriptLen);
      txOut.scrAddr = std::move(BtcUtils::getTxOutScrAddr(scriptRef));

      txOut.offset = offset;
      txOut.len = len;
   }
   tx.isRBF = txObj.isRBF();

   bool txInResolved = true;
   for (const auto& txin : tx.inputs) {
      if (txin.isResolved()) {
         continue;
      }
      txInResolved = false;
      break;
   }

   if (!txInResolved) {
      tx.state = ParsedTxStatus::Unresolved;
   } else {
      tx.state = ParsedTxStatus::Resolved;
   }
}

void Armory::ZeroConf::preprocessZcMap(
   const std::map<BinaryData, std::shared_ptr<ParsedTx>>& zcMap,
   LMDBBlockDatabase* db, Blockchain* bc)
{
   //run threads to preprocess the zcMap
   auto counter = std::make_shared<std::atomic<unsigned>>();
   counter->store(0, std::memory_order_relaxed);

   std::vector<std::shared_ptr<ParsedTx>> txVec;
   txVec.reserve(zcMap.size());
   for (const auto& txPair : zcMap) {
      txVec.push_back(txPair.second);
   }

   auto parserLdb = [db, bc, &txVec, counter]()->void
   {
      while (true) {
         auto id = counter->fetch_add(1, std::memory_order_relaxed);
         if (id >= txVec.size()) {
            return;
         }
         auto txIter = txVec.begin() + id;
         ::preprocessTx(*(*txIter), db, bc);
      }
   };

   std::vector<std::thread> parserThreads;
   for (unsigned i = 1; i < std::thread::hardware_concurrency(); i++) {
      parserThreads.emplace_back(std::thread(parserLdb));
   }
   parserLdb();

   for (auto& thr : parserThreads) {
      if (thr.joinable()) {
         thr.join();
      }
   }
}

void Armory::ZeroConf::finalizeParsedTxResolution(
   std::shared_ptr<ParsedTx> parsedTxPtr,
   LMDBBlockDatabase* db, const std::set<BinaryData>& allZcHashes,
   std::shared_ptr<MempoolSnapshot> ss)
{
   auto& parsedTx = *parsedTxPtr;
   bool isRBF = parsedTx.isRBF;
   bool isChained = parsedTx.isChainedZc;

   //if parsedTx has unresolved outpoint, they are most likely ZC
   for (auto& input : parsedTx.inputs) {
      if (input.isResolved()) {
         //check resolved key is valid
         if (input.opRef.isZc()) {
            isChained = true;
            auto chainedZC = ss->getTxByKey(input.opRef.getDbTxKeyRef());
            if (chainedZC == nullptr) {
               parsedTx.state = ParsedTxStatus::Invalid;
               return;
            } else if (chainedZC->state == ParsedTxStatus::Invalid) {
               throw std::runtime_error("invalid parent zc");
            }
         } else {
            auto keyRef = input.opRef.getDbKey().getSliceRef(0, 4);
            auto height = DBUtils::hgtxToHeight(keyRef);
            auto dupId = DBUtils::hgtxToDupID(keyRef);
            if (db->getValidDupIDForHeight(height) != dupId) {
               parsedTx.state = ParsedTxStatus::Invalid;
               return;
            }
         }
         continue;
      }

      auto opZcKey = ss->getKeyForHash(input.opRef.getTxHashRef());
      if (opZcKey.empty()) {
         if (Config::DBSettings::getDbType() == ARMORY_DB_TYPE::Super ||
            allZcHashes.find(input.opRef.getTxHashRef()) == allZcHashes.end()) {
            continue;
         }
      }

      isChained = true;
      auto chainedZC = ss->getTxByKey(opZcKey);
      if (chainedZC == nullptr) {
         continue;
      }
      const auto& chainedZcTxObj = chainedZC->getTxObj();

      //NOTE: avoid the copy
      auto chainedTxOut = chainedZcTxObj.getTxOutCopy(
         input.opRef.getIndex());

      input.value = chainedTxOut.getValue();
      input.scrAddr = chainedTxOut.getScrAddressStr();
      isRBF |= chainedZcTxObj.isRBF();
      input.opRef.setDbKey(opZcKey);
      input.opRef.setTime(chainedZcTxObj.getTxTime());
   }

   //check & update resolution state
   if (parsedTx.state != ParsedTxStatus::Resolved) {
      bool isResolved = true;
      for (const auto& input : parsedTx.inputs) {
         if (!input.isResolved()) {
            isResolved = false;
            break;
         }
      }
      if (isResolved) {
         parsedTx.state = ParsedTxStatus::Resolved;
      }
   }
   parsedTx.isRBF = isRBF;
   parsedTx.isChainedZc = isChained;
}

////////////////////////////////////////////////////////////////////////////////
// FilteredZeroConfData
bool FilteredZeroConfData::isEmpty() const
{
   return scrAddrTxioMap.empty();
}

bool FilteredZeroConfData::isValid() const
{
   if (txPtr == nullptr) {
      return false;
   }

   switch (Config::DBSettings::getDbType())
   {
      case ARMORY_DB_TYPE::Super:
         return txPtr->state == ParsedTxStatus::Resolved && !isEmpty();

      case ARMORY_DB_TYPE::Full:
      case ARMORY_DB_TYPE::Bare:
      {
         if (txPtr->state == ParsedTxStatus::Invalid ||
            txPtr->state == ParsedTxStatus::Mined ||
            txPtr->state == ParsedTxStatus::Unresolved) {
            return false;
         }
         return !isEmpty();
      }

      default:
         return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
// ParsedZCData
void ParsedZCData::mergeTxios(const ParsedZCData& pzd)
{
   scrAddrs.insert(pzd.scrAddrs.begin(), pzd.scrAddrs.end());
}

////////////////////////////////////////////////////////////////////////////////
// OutPointRef
void OutPointRef::unserialize(const uint8_t* ptr, uint32_t remaining)
{
   if (remaining < 36) {
      throw std::runtime_error("ptr is too short to be an outpoint");
   }
   BinaryDataRef bdr(ptr, remaining);
   BinaryRefReader brr(bdr);

   txHash_ = brr.get_BinaryDataRef(32);
   txOutIndex_ = brr.get_uint32_t();
}

void OutPointRef::unserialize(BinaryDataRef bdr)
{
   unserialize(bdr.getPtr(), bdr.getSize());
}

void OutPointRef::resolveDbKey(LMDBBlockDatabase *dbPtr)
{
   if (txHash_.empty() || txOutIndex_ == UINT16_MAX) {
      throw std::runtime_error("empty outpoint hash");
   }

   auto key = dbPtr->getDBKeyForHash(txHash_);
   if (key.getSize() != 6) {
      return;
   }
   setDbKey(key);
}

bool OutPointRef::isResolved() const
{
   return dbKey_.getSize() == 8;
}

void OutPointRef::setDbKey(const BinaryData& key)
{
   if (key.getSize() != 6) {
      throw std::runtime_error("invalid opref key size");
   }
   BinaryWriter bw;
   bw.put_BinaryData(key);
   bw.put_uint16_t(txOutIndex_, BE);
   dbKey_ = bw.getData();
}

BinaryDataRef OutPointRef::getTxHashRef() const
{
   return txHash_.getRef();
}

unsigned OutPointRef::getIndex() const
{
   return txOutIndex_;
}

const BinaryData& OutPointRef::getDbKey(void) const
{
   return dbKey_;
}

void OutPointRef::setTime(uint64_t t)
{
   time_ = t;
}

uint64_t OutPointRef::getTime() const
{
   return time_;
}

BinaryDataRef OutPointRef::getDbTxKeyRef() const
{
   if (!isResolved()) {
      throw std::runtime_error("unresolved outpoint key");
   }
   return dbKey_.getSliceRef(0, 6);
}

bool OutPointRef::isInitialized() const
{
   return txHash_.getSize() == 32 && txOutIndex_ != UINT16_MAX;
}

void OutPointRef::reset(InputResolution mode)
{
   if (mode != InputResolution::Both) {
      if (isZc() && mode == InputResolution::Mined) {
         return;
      }
   }
   dbKey_.clear();
   time_ = UINT64_MAX;
}

bool OutPointRef::isZc() const
{
   if (!isResolved()) {
      return false;
   }
   return dbKey_.startsWith(DBUtils::ZCPrefix);
}

////////////////////////////////////////////////////////////////////////////////
// ParsedTxIn
bool ParsedTxIn::isResolved() const
{
   if (!opRef.isResolved()) {
      return false;
   }
   if (scrAddr.empty() || value == UINT64_MAX) {
      return false;
   }
   return true;
}

////////////////////////////////////////////////////////////////////////////////
// ParsedTxOut
bool ParsedTxOut::isInitialized() const
{
   return !scrAddr.empty() && value != UINT64_MAX;
}

////////////////////////////////////////////////////////////////////////////////
// ParsedTx
ParsedTx::ParsedTx(BinaryData& key) :
   zcKey_(std::move(key))
{
   //set zc index in Tx object
   BinaryRefReader brr(zcKey_.getRef());
   brr.advance(2);
   txIndex_ = brr.get_uint32_t(BE);
}

void ParsedTx::setTx(BinaryDataRef data, uint32_t txTime)
{
   if (tx_ != nullptr) {
      throw std::runtime_error("tx is already set");
   }
   tx_ = std::make_shared<Tx>(data);
   tx_->setTxTime(txTime);
   tx_->setTxIndex(txIndex_);
}

void ParsedTx::setTxHash(const BinaryData& hash)
{
   txHash_ = hash;
}

BinaryDataRef ParsedTx::getKeyRef() const
{
   return zcKey_.getRef();
}

const BinaryData& ParsedTx::getKey() const
{
   return zcKey_;
}

bool ParsedTx::isResolved() const
{
   if (state == ParsedTxStatus::Uninitialized) {
      return false;
   }
   if (tx_ == nullptr) {
      return false;
   }
   if (inputs.size() != tx_->getNumTxIn() ||
      outputs.size() != tx_->getNumTxOut()) {
      return false;
   }
   for (const auto& input : inputs) {
      if (!input.isResolved()) {
         return false;
      }
   }
   return true;
}

void ParsedTx::resetInputResolution(InputResolution mode)
{
   for (auto& input : inputs) {
      input.opRef.reset(mode);
   }
   if (mode != InputResolution::Mined) {
      if (tx_ != nullptr) {
         tx_->setChainedZC(false);
      }
   }
   state = ParsedTxStatus::Uninitialized;
   isRBF = false;
   isChainedZc = false;
}

const BinaryData& ParsedTx::getTxHash() const
{
   if (txHash_.empty()) {
      txHash_ = std::move(tx_->getThisHash());
   }
   return txHash_;
}

const Tx& ParsedTx::getTxObj() const
{
   if (tx_ == nullptr) {
      throw std::runtime_error("tx is not set");
   }
   return *tx_;
}

///////////////////////////////////////////////////////////////////////////////
// MempoolData
/***
Mempool data is a chain of objects with a front object and cascading parents.
Requested data is first fetched from the front object first then trough parents
in a descending fashion, until the value closest to the front is returned, or
no value is found.

In maps, a key match with an empty value signifies a deletion.

scrAddrMap_ is handled differently.
***/
unsigned MempoolData::getParentCount() const
{
   auto parentPtr = parent_;
   unsigned count = 0;
   while (parentPtr != nullptr) {
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
std::shared_ptr<ParsedTx> MempoolData::getTx(BinaryDataRef key) const
{
   auto iter = txMap_.find(key);
   if (iter == txMap_.end()) {
      if (parent_ != nullptr) {
         return parent_->getTx(key);
      }
      return nullptr;
   }
   return iter->second;
}

///////////////////////////////////////////////////////////////////////////////
BinaryDataRef MempoolData::getKeyForHash(BinaryDataRef hash) const
{
   auto iter = txHashToDBKey_.find(hash);
   if (iter == txHashToDBKey_.end()) {
      if (parent_ != nullptr) {
         return parent_->getKeyForHash(hash);
      }
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
std::set<BinaryData>* MempoolData::getTxioKeysFromParent(
   BinaryDataRef scrAddr) const
{
   if (parent_ == nullptr) {
      return nullptr;
   }
   auto iter = parent_->scrAddrMap_.find(scrAddr);
   if (iter != parent_->scrAddrMap_.end()) {
      return &(iter->second);
   }
   return parent_->getTxioKeysFromParent(scrAddr);
}

///////////////////////////////////////////////////////////////////////////////
std::set<BinaryData>& MempoolData::getTxioKeysForScrAddr_NoThrow(
   BinaryDataRef scrAddr)
{
   auto iter = scrAddrMap_.find(scrAddr);
   if (iter == scrAddrMap_.end()) {
      //we don't have a key set for this scrAddr, look in parents
      auto parentSet = getTxioKeysFromParent(scrAddr);

      //copy the parent's set
      std::set<BinaryData> localKeySet;
      if (parentSet != nullptr) {
         localKeySet = *parentSet;
      }
      //insert into our own scrAddrMap whether we have a parent set or not
      auto insertIter = scrAddrMap_.emplace(scrAddr, move(localKeySet));

      //finally, set the iterator to the new entry
      iter = insertIter.first;
   }
   return iter->second;
}

///////////////////////////////////////////////////////////////////////////////
const std::set<BinaryData>& MempoolData::getTxioKeysForScrAddr(
   BinaryDataRef scrAddr) const
{
   auto iter = scrAddrMap_.find(scrAddr);
   if (iter == scrAddrMap_.end()) {
      if (parent_ != nullptr) {
         return parent_->getTxioKeysForScrAddr(scrAddr);
      }
      throw std::range_error("");
   }

   if (iter->second.empty()) {
      throw std::range_error("");
   }
   return iter->second;
}

///////////////////////////////////////////////////////////////////////////////
std::shared_ptr<const TxIOPair> MempoolData::getTxio(BinaryDataRef key) const
{
   auto iter = txioMap_.find(key);
   if (iter == txioMap_.end()) {
      if (parent_ != nullptr) {
         return parent_->getTxio(key);
      }
      return nullptr;
   }
   return std::const_pointer_cast<TxIOPair>(iter->second);
}

///////////////////////////////////////////////////////////////////////////////
bool MempoolData::isTxOutSpentByZC(BinaryDataRef key) const
{
   auto iter = txOutsSpentByZC_.find(key);
   if (iter == txOutsSpentByZC_.end()) {
      if (parent_ != nullptr) {
         return parent_->isTxOutSpentByZC(key);
      }
      return false;
   }
   return iter->second;
}

///////////////////////////////////////////////////////////////////////////////
void MempoolData::dropFromSpentTxOuts(BinaryDataRef key)
{
   bool inParents = false;
   if (parent_ != nullptr) {
      inParents = parent_->isTxOutSpentByZC(key);
   }

   if (!inParents) {
      txOutsSpentByZC_.erase(key);
      return;
   }

   auto iter = txOutsSpentByZC_.find(key);
   if (iter == txOutsSpentByZC_.end()) {
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
   while (keyIter != txioKeys.end()) {
      if (!keyIter->startsWith(zcKey)) {
         break;
      }
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
   if (zcPtr == nullptr) {
      throw std::range_error("");
   }

   for (unsigned i=0; i<zcPtr->outputs.size(); i++) {
      BinaryWriter bw(8);
      bw.put_BinaryData(key);
      bw.put_uint16_t(i, BE);
      txioMap_[bw.getData()] = nullptr;
   }
}

///////////////////////////////////////////////////////////////////////////////
void MempoolData::dropTxioInputs(BinaryDataRef zcKey,
   const std::set<BinaryData>& spentFromTxoutKeys)
{
   for (const auto& spentTxoutKey : spentFromTxoutKeys) {
      //look up the spendee by key
      auto txioPtr = getTxio(spentTxoutKey);
      if (txioPtr == nullptr) {
         throw std::range_error("");
      }

      //does this txio have a spender and is it our tx?
      if (!txioPtr->hasTxIn() ||
         txioPtr->getTxRefOfInput().getDBKeyRef() != zcKey) {
         continue;
      }

      if (!txioPtr->hasTxOutZC()) {
         //if the txout is mined, remove it entirely
         txioMap_[spentTxoutKey] = nullptr;
      } else {
         /*
         copy the txio, remove the txin and replace it in the map
         (so as to not disrupt the potential readers)
         */
         auto newTxio = std::make_shared<TxIOPair>(*(txioPtr));
         newTxio->setTxIn(BinaryData());
         txioMap_[spentTxoutKey] = newTxio;
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void MempoolData::dropTx(BinaryDataRef key)
{
   bool inParents = false;
   if (parent_ != nullptr) {
      auto txPtr = parent_->getTx(key);
      if (txPtr != nullptr) {
         inParents = true;
      }
   }

   if (!inParents) {
      txMap_.erase(key);
      return;
   }

   auto iter = txMap_.find(key);
   if (iter == txMap_.end()) {
      auto insertIter = txMap_.emplace(key, nullptr);
      iter = insertIter.first;
   }
   iter->second = nullptr;
}

///////////////////////////////////////////////////////////////////////////////
std::shared_ptr<MempoolData> MempoolData::mergeWithParent(
   std::shared_ptr<MempoolData> ptr)
{
   if (ptr->parent_ == nullptr) {
      return ptr;
   }
   auto newObj = std::make_shared<MempoolData>();

   //tx hashes
   {
      newObj->txHashToDBKey_ = ptr->txHashToDBKey_;
      for (const auto& hashPair : ptr->parent_->txHashToDBKey_) {
         newObj->txHashToDBKey_.emplace(hashPair);
      }

      auto hashIter = newObj->txHashToDBKey_.begin();
      while (hashIter != newObj->txHashToDBKey_.end()) {
         if (hashIter->second.empty()) {
            if (ptr->parent_->parent_ != nullptr) {
               if (ptr->parent_->parent_->getKeyForHash(
                  hashIter->first).empty()) {
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
      for (const auto& txPair : ptr->parent_->txMap_) {
         newObj->txMap_.emplace(txPair);
      }

      auto txIter = newObj->txMap_.begin();
      while (txIter != newObj->txMap_.end()) {
         if (txIter->second == nullptr) {
            if (ptr->parent_->parent_ != nullptr) {
               if (ptr->parent_->parent_->getTx(txIter->first) == nullptr) {
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
      for (const auto& keyPair : ptr->parent_->txOutsSpentByZC_) {
         newObj->txOutsSpentByZC_.emplace(keyPair);
      }

      auto keyIter = newObj->txOutsSpentByZC_.begin();
      while (keyIter != newObj->txOutsSpentByZC_.end()) {
         if (!keyIter->second) {
            if (ptr->parent_->parent_ != nullptr) {
               if (!ptr->parent_->parent_->isTxOutSpentByZC(
                  keyIter->first)) {
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
      for (const auto& addrPair : ptr->parent_->scrAddrMap_) {
         newObj->scrAddrMap_.emplace(addrPair);
      }

      auto addrIter = newObj->scrAddrMap_.begin();
      while (addrIter != newObj->scrAddrMap_.end()) {
         if (addrIter->second.empty()) {
            if (ptr->parent_->parent_ != nullptr) {
               try {
                  ptr->parent_->parent_->getTxioKeysForScrAddr(addrIter->first);
               } catch (const std::range_error&) {
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
      for (const auto& txioPair : ptr->parent_->txioMap_) {
         newObj->txioMap_.emplace(txioPair);
      }
      auto txioIter = newObj->txioMap_.begin();
      while (txioIter != newObj->txioMap_.end()) {
         if (txioIter->second == nullptr) {
            if (ptr->parent_->parent_ != nullptr) {
               if (ptr->parent_->parent_->getTxio(txioIter->first) == nullptr) {
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
// MempoolSnapshot
MempoolSnapshot::MempoolSnapshot(unsigned depth, unsigned threshold) :
   depth_(depth), threshold_(threshold)
{
   data_ = std::make_shared<MempoolData>();
}

///////////////////////////////////////////////////////////////////////////////
void MempoolSnapshot::preprocessZcMap(LMDBBlockDatabase* db, Blockchain* bc)
{
   Armory::ZeroConf::preprocessZcMap(data_->txMap_, db, bc);
}

///////////////////////////////////////////////////////////////////////////////
std::shared_ptr<ParsedTx> MempoolSnapshot::getTxByKey_NoConst(
   BinaryDataRef key) const
{
   return data_->getTx(key);
}

std::shared_ptr<const ParsedTx> MempoolSnapshot::getTxByKey(
   BinaryDataRef key) const
{
   auto txPtr = getTxByKey_NoConst(key);
   return std::const_pointer_cast<const ParsedTx>(txPtr);
}

std::shared_ptr<const ParsedTx> MempoolSnapshot::getTxByHash(
   BinaryDataRef hash) const
{
   auto key = getKeyForHash(hash);
   if (key.empty()) {
      return nullptr;
   }
   return getTxByKey(key);
}

TxOut MempoolSnapshot::getTxOutCopy(
   BinaryDataRef key, uint16_t outputId) const
{
   auto txPtr = getTxByKey(key);
   if (txPtr == nullptr) {
      throw std::range_error("invalid zc key");
   }
   if (outputId >= txPtr->outputs.size()) {
      throw std::range_error("invalid output id");
   }
   return txPtr->getTxObj().getTxOutCopy(outputId);
}

std::shared_ptr<const TxIOPair> MempoolSnapshot::getTxioByKey(
   BinaryDataRef txioKey) const
{
   return data_->getTxio(txioKey);
}

BinaryDataRef MempoolSnapshot::getKeyForHash(
   BinaryDataRef hash) const
{
   return data_->getKeyForHash(hash);
}

BinaryDataRef MempoolSnapshot::getHashForKey(
   BinaryDataRef key) const
{
   auto txPtr = getTxByKey(key);
   if (txPtr == nullptr) {
      return {};
   }
   return txPtr->getTxHash().getRef();
}

uint32_t MempoolSnapshot::getTopZcID() const
{
   return topID_;
}

bool MempoolSnapshot::hasHash(BinaryDataRef hash) const
{
   return !(data_->getKeyForHash(hash).empty());
}

bool MempoolSnapshot::isTxOutSpentByZC(BinaryDataRef key) const
{
   return data_->isTxOutSpentByZC(key);
}

const std::set<BinaryData>& MempoolSnapshot::getTxioKeysForScrAddr(
   BinaryDataRef scrAddr) const
{
   return data_->getTxioKeysForScrAddr(scrAddr);
}

std::map<BinaryDataRef, std::shared_ptr<const TxIOPair>>
   MempoolSnapshot::getTxioMapForScrAddr(BinaryDataRef scrAddr) const
{
   try {
      const auto& txioKeys = getTxioKeysForScrAddr(scrAddr);
      std::map<BinaryDataRef, std::shared_ptr<const TxIOPair>> result;

      for (const auto& txioKey : txioKeys) {
         auto txioPtr = getTxioByKey(txioKey);
         if (txioPtr == nullptr) {
            continue;
         }
         result.emplace(txioKey.getRef(), txioPtr);
      }
      return result;
   } catch (const std::range_error&) {
      return {};
   }
}

///////////////////////////////////////////////////////////////////////////////
std::set<BinaryData> MempoolSnapshot::findChildren(BinaryDataRef zcKey)
{
   auto zcPtr = data_->getTx(zcKey);
   if (zcPtr == nullptr) {
      throw std::range_error("");
   }

   //set zcKeys of all ZC spending from our parent
   std::set<BinaryData> children;
   for (unsigned i=0; i<zcPtr->outputs.size(); i++) {
      BinaryWriter bw(8);
      bw.put_BinaryData(zcKey);
      bw.put_uint16_t(i, BE);

      auto txioPtr = data_->getTxio(bw.getDataRef());
      if (txioPtr == nullptr) {
         continue;
      }

      //skip if this txio doesn't carry a txin (txout isn't spent)
      if (!txioPtr->hasTxIn()) {
         continue;
      }

      //grab the txin's TxRef object
      auto spenderRef = txioPtr->getTxRefOfInput();

      //save the Tx key (key of the txin's owner)
      children.emplace(spenderRef.getDBKey());
   }
   return children;
}

///////////////////////////////////////////////////////////////////////////////
std::map<BinaryData, std::shared_ptr<ParsedTx>> MempoolSnapshot::dropZc(
   BinaryDataRef zcKey)
{
   auto txPtr = getTxByKey_NoConst(zcKey);
   if (txPtr == nullptr) {
      return {};
   }

   std::set<BinaryData> spentFromTxoutKeys;
   std::map<BinaryData, std::shared_ptr<ParsedTx>> droppedZc;

   //drop from spent set
   for (const auto& input : txPtr->inputs) {
      if (!input.isResolved()) {
         continue;
      }
      data_->dropFromSpentTxOuts(input.opRef.getDbKey());
      spentFromTxoutKeys.emplace(input.opRef.getDbKey());

      //do not purge input keys from scrAddr map unless they're mined
      if (input.opRef.getDbKey().startsWith(DBUtils::ZCPrefix)) {
         continue;
      }
      data_->dropFromScrAddrMap(input.scrAddr, input.opRef.getDbKey());
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
   for (const auto& child : children) {
      auto droppedTx = dropZc(child);
      droppedZc.insert(droppedTx.begin(), droppedTx.end());
   }

   //drop outputs from scrAddrMap
   for (const auto& output : txPtr->outputs) {
      data_->dropFromScrAddrMap(output.scrAddr, zcKey);
   }

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
void MempoolSnapshot::stageNewZC(std::shared_ptr<ParsedTx> zcPtr,
   const FilteredZeroConfData& filteredData)
{
   const auto& dbKey = zcPtr->getKey();
   const auto& txHash = zcPtr->getTxHash();

   //set tx and hash to key entry
   data_->txHashToDBKey_[txHash.getRef()] = dbKey.getRef();
   data_->txMap_[dbKey.getRef()] = zcPtr;

   //merge spent outpoints
   for (const auto& txoutkey : filteredData.txOutsSpentByZC) {
      data_->txOutsSpentByZC_.emplace(txoutkey, true);
   }

   //updated txio and scraddr maps
   for (const auto& saTxios : filteredData.scrAddrTxioMap) {
      auto& keySet = data_->getTxioKeysForScrAddr_NoThrow(saTxios.first);
      for (const auto& txioPair : saTxios.second) {
         //add the txioKey to the affected scrAddr
         keySet.emplace(txioPair.first);

         //add to txio map
         data_->txioMap_[txioPair.first] = txioPair.second;
      }
   }

   BinaryReader brrKey(dbKey);
   brrKey.advance(2);
   auto zcId = brrKey.get_uint32_t(BE);
   if (zcId > topID_) {
      topID_ = zcId;
   }
}

///////////////////////////////////////////////////////////////////////////////
std::shared_ptr<MempoolSnapshot> MempoolSnapshot::copy(
   std::shared_ptr<MempoolSnapshot> ss, unsigned pool, unsigned threshold)
{
   auto ssCopy = std::make_shared<MempoolSnapshot>(pool, threshold);
   if (ss != nullptr) {
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
      data_->txioMap_.empty()) {
      return;
   }

   auto newData = std::make_shared<MempoolData>();
   newData->parent_ = data_;

   if (newData->getParentCount() > depth_) {
      auto toMerge = newData->parent_;

      //find the first parent above the size threshold, use
      //first parent otherwise
      auto objPtr = toMerge;
      while (objPtr != nullptr) {
         if (objPtr->txioMap_.size() > threshold_ &&
            objPtr->parent_ != nullptr) {
            toMerge = objPtr;
            break;
         }

         objPtr = objPtr->parent_;
      }

      //merge this data with its parent
      if (toMerge != nullptr) {
         auto mergedData = MempoolData::mergeWithParent(toMerge);

         //replace parent with the newly merged data
         auto objPtr = newData;
         while (objPtr != nullptr) {
            if (objPtr->parent_ == toMerge) {
               objPtr->parent_ = mergedData;
               break;
            }

            objPtr = objPtr->parent_;
         }

         ++mergeCount_;
      }
   }

   data_ = std::move(newData);
}
