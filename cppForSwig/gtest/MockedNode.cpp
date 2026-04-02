////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <algorithm>

#include "MockedNode.h"
#include <Utils/ArmoryErrors.h>
#include <Utils/BtcUtils.h>
#include <Utils/DBUtils.h>
#include <Signer/Signer.h>
#include <Signer/Script.h>

#include <BlockchainDatabase/StoredBlockObj.h>
#include <BlockchainDatabase/lmdb_wrapper.h>
#include <BlockchainDatabase/Blockchain.h>
#include <BlockchainDatabase/BlockDataMap.h>
#include <BlockchainDatabase/BlockUtils.h>
#include "BDV_Notification.h"

using namespace Armory;
using namespace std::chrono_literals;

namespace
{
   Tx getFullTxCopy(const LMDBBlockDatabase* iface, std::shared_ptr<Blockchain> bc,
      const BinaryData& txHash)
   {
      auto dbKey = iface->getDBKeyForHash(txHash);
      unsigned id; uint8_t dup; uint16_t idx;
      BinaryRefReader brrKey(dbKey);
      DBUtils::readBlkDataKeyNoPrefix(brrKey, id, dup, idx);

      std::shared_ptr<BlockHeader> header;
      if (dup == 0x7F) {
         header = bc->getHeaderById(id);
      } else {
         header = bc->getHeaderByHeight(id, dup);
      }
      return iface->getFullTxCopy(idx, header);
   }

   int verifyTxSigs(const BinaryData& rawTx, const LMDBBlockDatabase* iface,
      std::shared_ptr<Blockchain> bc,
      const std::map<BinaryDataRef, std::shared_ptr<MempoolObject>>& mempool)
   {
      Tx tx(rawTx);
      std::map<BinaryData, std::map<unsigned, UTXO>> utxoMap;
      for (unsigned i=0; i<tx.getNumTxIn(); i++) {
         //grab all utxos
         auto txin = tx.getTxInCopy(i);
         auto outpoint = txin.getOutPoint();

         auto prevHash = outpoint.getTxHash();
         auto prevTx = getFullTxCopy(iface, bc, prevHash);
         if (prevTx.isInitialized()) {
            auto txOut = prevTx.getTxOutCopy(outpoint.getTxOutIndex());
            UTXO utxo(
               txOut.getValue(), prevTx.getTxHeight(),
               prevTx.getTxIndex(), outpoint.getTxOutIndex(),
               prevHash, txOut.getScriptRef());

            auto& idMap = utxoMap[prevHash];
            idMap.emplace(outpoint.getTxOutIndex(), utxo);
            continue; //got the output, on to the next outpoint
         }

         //see if this is a zc outpoint instead
         auto mempoolIter = mempool.find(prevHash);
         if (mempoolIter == mempool.end()) {
            //couldn't find utxo
            return (int)ArmoryErrorCodes::ZcBroadcast_Error;
         }

         Tx zcTx(mempoolIter->second->rawTx);
         if (outpoint.getTxOutIndex() >= zcTx.getNumTxOut()) {
            //couldn't find utxo
            return (int)ArmoryErrorCodes::ZcBroadcast_Error;
         }

         //grab output from tx, convert to utxo
         auto txOutCopy = zcTx.getTxOutCopy(outpoint.getTxOutIndex());
         UTXO utxo;
         utxo.unserializeRaw(txOutCopy.serializeRef());
         utxo.txOutIndex_ = outpoint.getTxOutIndex();

         auto& idMap = utxoMap[outpoint.getTxHash()];
         idMap.emplace(outpoint.getTxOutIndex(), utxo);
      }

      auto evalState = Signing::Signer::verify(
         rawTx, utxoMap,
         unsigned(SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_SEGWIT | SCRIPT_VERIFY_P2SH_SHA256),
         true);
      if (!evalState.isValid()) {
         //invalid sig
         return (int)ArmoryErrorCodes::ZcBroadcast_VerifyRejected;
      }
      return (int)ArmoryErrorCodes::Success;
   }
}

////////////////////////////////////////////////////////////////////////////////
// MempoolObject
bool MempoolObject::operator<(const MempoolObject& rhs) const
{
   return order < rhs.order;
}

////////////////////////////////////////////////////////////////////////////////
// NodeUnitTest
Threading::BlockingQueue<BinaryData> NodeUnitTest::watcherInvQueue_;

NodeUnitTest::NodeUnitTest(uint32_t magic_word, bool watcher) :
   BitcoinNodeInterface(magic_word, watcher)
{
   //0 is reserved for coinbase tx ordering in spoofed blocks
   counter_.store(1, std::memory_order_relaxed);

   if (!watcher) {
      return;
   }
   auto watcherLbd = [this](void)->void
   {
      this->watcherProcess();
   };
   watcherThread_ = std::thread(watcherLbd);
}

NodeUnitTest::~NodeUnitTest()
{
   shutdown();
}

void NodeUnitTest::shutdown()
{
   if (watcherThread_.joinable()) {
      watcherInvQueue_.terminate();
      watcherThread_.join();
   }
   BitcoinNodeInterface::shutdown();
}

////////
void NodeUnitTest::connectToNode(bool)
{
   run_.store(true, std::memory_order_relaxed);
}

bool NodeUnitTest::connected() const
{
   return run_.load(std::memory_order_relaxed);
}

////////
std::vector<UnitTestBlock> NodeUnitTest::getMinedBlocks() const
{
   return blocks_;
}

void NodeUnitTest::notifyNewBlock(void)
{
   Node::InvEntry ie;
   ie.invtype = Node::Inv_Msg_Block;

   std::vector<Node::InvEntry> vecIE;
   vecIE.push_back(ie);

   processInvBlock(std::move(vecIE));
}

std::map<unsigned, BinaryData> NodeUnitTest::mineNewBlock(
   std::shared_ptr<BlockDataManager> bdm,
   unsigned count, const BinaryData& h160, double diff)
{
   Signing::Recipient_P2PKH recipient(h160, 50 * COIN);
   return mineNewBlock(bdm, count, &recipient, diff);
}

std::map<unsigned, BinaryData> NodeUnitTest::mineNewBlock(
   std::shared_ptr<BlockDataManager> bdm,
   unsigned count, Signing::ScriptRecipient* recipient, double diff)
{
   auto diffBits = BtcUtils::convertDoubleToDiffBits(diff);
   if (header_.prevHash_.empty()) {
      auto top = blockchain_->top();
      header_.prevHash_ = top->getThisHash();
      header_.timestamp_ = top->getTimestamp();
      header_.diffBits_  = diffBits;
      header_.blockHeight_ = top->getBlockHeight() + 1;
   }

   std::map<unsigned, BinaryData> result;
   bool stagedZc = false;
   for (unsigned i = 0; i < count; i++) {
      UnitTestBlock block;

      //create coinbase tx
      BinaryWriter bwCoinbase;
      {
         //version
         bwCoinbase.put_uint32_t(1);

         //input count
         bwCoinbase.put_var_int(1);

         //outpoint
         BinaryData outpoint(36);
         memset(outpoint.getPtr(), 0, outpoint.getSize());
         bwCoinbase.put_BinaryData(outpoint);

         //txin script
         bwCoinbase.put_var_int(4);
         bwCoinbase.put_uint32_t(counter_.fetch_add(1, std::memory_order_relaxed));

         //sequence
         bwCoinbase.put_uint32_t(UINT32_MAX);

         //output count
         bwCoinbase.put_var_int(1);

         //output script
         auto& outputScript = recipient->getSerializedScript();
         bwCoinbase.put_BinaryData(outputScript);

         //locktime
         bwCoinbase.put_uint32_t(0);
      }

      auto coinbaseObj = std::make_shared<MempoolObject>();
      coinbaseObj->rawTx = bwCoinbase.getData();
      coinbaseObj->hash = BtcUtils::getHash256(coinbaseObj->rawTx);
      coinbaseObj->order = 0;
      block.coinbase = std::make_shared<Tx>(coinbaseObj->rawTx);
      block.height = header_.blockHeight_++;
      result.emplace(block.height, coinbaseObj->hash);

      //grab all tx in the mempool, respect ordering
      std::vector<std::shared_ptr<MempoolObject>> mempoolV;
      std::map<BinaryDataRef, std::shared_ptr<MempoolObject>> purgedMempool;
      mempoolV.push_back(coinbaseObj);
      for (auto& obj : mempool_) {
         if (obj.second->staged) {
            stagedZc = true;
         }
         if (obj.second->blocksUntilMined == 0) {
            mempoolV.push_back(obj.second);
         } else {
            --obj.second->blocksUntilMined;
            purgedMempool.emplace(obj.second->hash.getRef(), std::move(obj.second));
         }
      }

      struct SortStruct
      {
         bool operator()(const std::shared_ptr<MempoolObject>& lhs,
            const std::shared_ptr<MempoolObject>& rhs) const
         {
            return *lhs < *rhs;
         }
      };

      std::sort(mempoolV.begin(), mempoolV.end(), SortStruct());

      //compute merkle
      std::vector<BinaryData> txHashes;
      for (auto& obj : mempoolV) {
         txHashes.emplace_back(obj->hash);

         //purge spender set of this zc
         purgeSpender(obj->rawTx);
      }
      auto merkleRoot = BtcUtils::calculateMerkleRoot(txHashes);


      //clear mempool
      mempool_ = std::move(purgedMempool);

      //build block
      BinaryWriter bwBlock;

      {
         /* build header */

         //version
         bwBlock.put_uint32_t(1);

         //previous hash
         bwBlock.put_BinaryData(header_.prevHash_);

         //merkle root
         bwBlock.put_BinaryData(merkleRoot);

         //timestamp
         bwBlock.put_uint32_t(header_.timestamp_ + 600);

         //diff bits
         bwBlock.put_BinaryData(diffBits);

         //nonce
         bwBlock.put_uint32_t(0);

         //update prev hash and timestamp for the next block
         header_.prevHash_ = BtcUtils::getHash256(bwBlock.getDataRef());

         block.headerHash = header_.prevHash_;
         block.rawHeader = bwBlock.getDataRef();
         block.diffBits = diffBits;
         block.timestamp = header_.timestamp_;
         header_.timestamp_ += 600;
      }

      {
         /* serialize block */

         //tx count
         bwBlock.put_var_int(mempoolV.size());

         //tx
         for (const auto& txObj : mempoolV) {
            bwBlock.put_BinaryData(txObj->rawTx);
            block.transactions.emplace_back(Tx{txObj->rawTx});
         }
      }

      blocks_.emplace_back(block);

      {
         /* append to blocks data file */

         //get file stream
         auto lastFilePath = filesPtr_->getLastFilePath();
         auto fStream = std::ofstream(lastFilePath, std::ios::binary | std::ios::app);

         //magic byte
         BinaryWriter bwHeader;
         bwHeader.put_uint32_t(getMagicWord());

         //block size
         bwHeader.put_uint32_t(bwBlock.getSize());

         fStream.write(
            (const char*)bwHeader.getDataRef().getPtr(), bwHeader.getSize());

         //block data
         fStream.write(
            (const char*)bwBlock.getDataRef().getPtr(), bwBlock.getSize());

         fStream.close();
      }
   }

   if (stagedZc && !mempool_.empty()) {
      /*
      We have staged zc, need to push them. Have to wait for the mining to 
      complete first however, or the staged zc will most likely be rejected
      as invalid.
      */

      //create hook
      auto promPtr = std::make_shared<std::promise<bool>>();
      auto fut = promPtr->get_future();
      auto waitOnNotif = [promPtr](BDV_Notification* notifPtr)->void
      {
         auto blockNotif = dynamic_cast<BDV_Notification_NewBlock*>(notifPtr);
         if (blockNotif == nullptr) {
            promPtr->set_value(false);
         } else {
            promPtr->set_value(true);
         }
      };

      auto hookPtr = std::make_shared<BDVNotificationHook>();
      hookPtr->func = waitOnNotif;

      //set hook
      bdm->registerOneTimeHook(hookPtr);

      //push db block notification
      notifyNewBlock();

      //wait on hook
      if (!fut.get()) {
         throw std::runtime_error("unexpected bdm notificaiton");
      }

      //push the staged transactions
      std::vector<Node::InvEntry> invVec;
      std::map<BinaryData, BinaryData> rawTxMap;
      for (auto& tx : mempool_) {
         Node::InvEntry ie;
         ie.invtype = Node::Inv_Msg_Witness_Tx;
         memcpy(ie.hash, tx.first.getPtr(), 32);
         invVec.emplace_back(ie);
         rawTxMap.emplace(tx.first, tx.second->rawTx);
      }
      rawTxMap_.update(rawTxMap);
      processInvTx(invVec);
   } else {
      //push db block notification
      std::this_thread::sleep_for(100ms);
      notifyNewBlock();
   }
   return result;
}

void NodeUnitTest::setReorgBranchPoint(std::shared_ptr<BlockHeader> header)
{
   if (header == nullptr) {
      throw std::runtime_error("null header");
   }

   header_.prevHash_ = header->getThisHash();
   header_.blockHeight_ = header->getBlockHeight();
   header_.timestamp_ = header->getTimestamp();
   header_.diffBits_ = header->getDiffBits();

   //purge mempool
   mempool_.clear();
}

////////
void NodeUnitTest::checkSigs(bool check)
{
   checkSigs_ = check;
}

void NodeUnitTest::presentZcHash(const BinaryData& hash)
{
   seenHashes_.emplace(hash);
}

void NodeUnitTest::pushZC(
   const std::vector<std::pair<BinaryData, unsigned>>& txVec,
   bool stage)
{
   std::vector<Node::InvEntry> invVec;
   std::map<BinaryData, BinaryData> rawTxMap;

   //save tx to fake mempool
   for (auto& tx : txVec) {
      auto obj = std::make_shared<MempoolObject>();
      Tx txNew(tx.first);

      //skip if we've seen this hash before
      obj->rawTx = tx.first;
      obj->hash = txNew.getThisHash();
      obj->order = counter_.fetch_add(1, std::memory_order_relaxed);
      obj->blocksUntilMined = tx.second;
      obj->staged = stage;

      /***
      cheap zc replacement code: check for outpoint reuse, assume unit
      tests will not push conflicting transactions that aren't legit RBF
      ***/

      auto poolIter = mempool_.begin();
      while (poolIter != mempool_.end()) {
         Tx txMempool(poolIter->second->rawTx);
         if (txNew.getThisHash() == txMempool.getThisHash()) {
            return;
         }

         bool hasCollision = false;
         for (unsigned i = 0; i < txMempool.getNumTxIn(); i++) {
            auto txinMempool = txMempool.getTxInCopy(i);
            for (unsigned y = 0; y < txNew.getNumTxIn(); y++) {
               auto txinNew = txNew.getTxInCopy(y);
               if (txinMempool.getOutPoint() == txinNew.getOutPoint()) {
                  hasCollision = true;
                  break;
               }
            }

            if (hasCollision) {
               break;
            }
         }

         if (hasCollision) {
            mempool_.erase(poolIter++);
            continue;
         }
         ++poolIter;
      }

      //add to mempool
      auto insertIter = mempool_.emplace(obj->hash.getRef(), std::move(obj));

      //populate spender set
      for (unsigned i=0; i<txNew.getNumTxIn(); i++) {
         auto txin = txNew.getTxInCopy(i);
         auto op = txin.getOutPoint();

         auto& indexMap = spenderSet_[op.getTxHash()];
         indexMap.emplace(op.getTxOutIndex(), txNew.getThisHash());
      }

      if (!seenHashes_.insert(txNew.getThisHash()).second) {
         continue;
      }

      //add to inv vector
      Node::InvEntry ie;
      ie.invtype = Node::Inv_Msg_Witness_Tx;
      memcpy(ie.hash, insertIter.first->second->hash.getPtr(), 32);
      invVec.emplace_back(ie);

      //save tx to reply to getdata request
      rawTxMap.emplace(
         insertIter.first->second->hash,
         insertIter.first->second->rawTx);
   }
   rawTxMap_.update(rawTxMap);

   /*
   Do not push the tx to the db if it is flagged for staging. It will get
   pushed after the next mining call. This is mostly useful for reorgs 
   (where you can't push zc that conflicts with the still valid branch)
   */
   if (stage || invVec.empty()) {
      return;
   }
   processInvTx(invVec);
}

void NodeUnitTest::purgeSpender(const BinaryData& rawTx)
{
   Tx tx(rawTx);
   for (unsigned i=0; i<tx.getNumTxIn(); i++) {
      auto txin = tx.getTxInCopy(i);
      auto op = txin.getOutPoint();

      auto hashIter = spenderSet_.find(op.getTxHash());
      if (hashIter == spenderSet_.end()) {
         continue;
      }
      hashIter->second.erase(op.getTxOutIndex());

      if (hashIter->second.empty()) {
         spenderSet_.erase(hashIter);
      }
   }
}

void NodeUnitTest::evictZC(const BinaryData& txHash)
{
   /*remove a zc from the mempool*/

   //find in mempool
   auto iter = mempool_.find(txHash.getRef());
   if (iter == mempool_.end()) {
      return;
   }
   //remove from spender set
   purgeSpender(iter->second->rawTx);

   //remove from mempool
   mempool_.erase(iter);
}

////////
void NodeUnitTest::setBlockchain(std::shared_ptr<Blockchain> bcPtr)
{
   blockchain_ = bcPtr;
}

void NodeUnitTest::setBlockFiles(std::shared_ptr<BlockFiles> filesPtr)
{
   filesPtr_ = filesPtr;
}

void NodeUnitTest::setIface(LMDBBlockDatabase* iface)
{
   iface_ = iface;
}

////////
uint64_t NodeUnitTest::getFeeForTx(const Tx& tx) const
{
   if (iface_ == nullptr) {
      throw std::runtime_error("null db ptr");
   }

   //tally inputs value
   uint64_t inputsVal = 0;
   for (unsigned i=0; i<tx.getNumTxIn(); i++) {
      auto txin = tx.getTxInCopy(i);
      auto outpoint = txin.getOutPoint();

      auto tx = getFullTxCopy(iface_, blockchain_, outpoint.getTxHash());
      auto txOutCopy = tx.getTxOutCopy(outpoint.getTxOutIndex());
      if (txOutCopy.isInitialized()) {
         inputsVal += txOutCopy.getValue();
         continue;
      }

      //could not find output in db, check mempool
      auto iter = mempool_.find(outpoint.getTxHashRef());
      if (iter == mempool_.end()) {
         throw std::runtime_error("can't resolve outpoint");
      }

      Tx mempoolTx(iter->second->rawTx);
      auto txout = mempoolTx.getTxOutCopy(outpoint.getTxOutIndex());
      inputsVal += txout.getValue();
   }

   //tally outputs value
   uint64_t outputsVal = 0;
   for (unsigned i=0; i<tx.getNumTxOut(); i++) {
      auto txout = tx.getTxOutCopy(i);
      outputsVal += txout.getValue();
   }

   //return diff
   if (outputsVal > inputsVal) {
      throw std::runtime_error("sum of outputs is greater than sum of inputs");
   }
   return inputsVal - outputsVal;
}

////////
void NodeUnitTest::sendMessage(std::unique_ptr<Node::Payload> payload)
{
   //need access to the db to check zc validity
   if (iface_ == nullptr) {
      throw std::runtime_error("uninitialized lmdb_wrapper ptr");
   }
   std::unique_lock<std::mutex> lock(sendMessageMutex_);

   //mock the bitcoin node response to these sendMessage payloads
   switch (payload->type())
   {
      case Node::PayloadType::Inv:
      {
         /*
         Pushed inv payload from armorydb to bitcoin node
         */

         std::shared_ptr<Node::Payload> payloadSPtr(move(payload));
         auto payloadInv = std::dynamic_pointer_cast<Node::Payload_Inv>(payloadSPtr);
         if (payloadInv == nullptr) {
            throw std::runtime_error("unexpected payload type");
         }

         for (auto& entry : payloadInv->invVector_) {
            switch (entry.invtype)
            {
               case Node::Inv_Msg_Tx:
               case Node::Inv_Msg_Witness_Tx:
               {
                  //bail if we have seen this hash before
                  BinaryData hashBd(&entry.hash[0], sizeof(entry.hash));
                  if (!seenHashes_.insert(hashBd).second) {
                     break;
                  }

                  {
                     //or if we already have this tx
                     auto mempoolIter = mempool_.find(hashBd.getRef());
                     if (mempoolIter != mempool_.end()) {
                        break;
                     }
                  }

                  std::shared_ptr<Node::Payload_Tx> payloadTx;
                  {
                     //consume getDataMap entry
                     auto gdpMap = getDataPayloadMap_.get();
                     auto iter = gdpMap->find(hashBd);
                     if (iter == gdpMap->end()) {
                        break;
                     }
                     std::shared_ptr<Node::Payload> payloadTxSPtr(
                        std::move(iter->second->payload_));
                     payloadTx = std::dynamic_pointer_cast<Node::Payload_Tx>(
                        payloadTxSPtr);

                     //cleanup getdatapayload map
                     getDataPayloadMap_.erase(hashBd);
                  }

                  //bail if we have to skip a zc
                  bool skip = (skipZc_.load(std::memory_order_relaxed) != 0);
                  if (skip) {
                     skipZc_.fetch_sub(1, std::memory_order_relaxed);
                     break;
                  }

                  auto obj = std::make_shared<MempoolObject>();
                  auto rawTx = payloadTx->getRawTx();
                  obj->rawTx = BinaryData(&rawTx[0], rawTx.size());
                  obj->hash = hashBd;
                  obj->order = counter_.fetch_add(1, std::memory_order_relaxed);

                  unsigned delay = 0;
                  if (!zcDelays_.empty()) {
                     delay = zcDelays_.front();
                     zcDelays_.pop_front();
                  }

                  if (!zcStalls_.empty()) {
                     auto stall = zcStalls_.front();
                     zcStalls_.pop_front();
                     std::this_thread::sleep_for(std::chrono::seconds(stall));
                  }

                  obj->blocksUntilMined = delay;
                  obj->staged = false;

                  //check sigs
                  if (checkSigs_) {
                     if (verifyTxSigs(obj->rawTx, iface_, mempool_) !=
                        (int)ArmoryErrorCodes::Success) {
                        break;
                     }
                  }

                  Tx tx{obj->rawTx};

                  //check outpoints are valid & spendable
                  bool opFailure = false;
                  for (unsigned i=0; i<tx.getNumTxIn(); i++) {
                     auto txin = tx.getTxInCopy(i);
                     auto outpoint = txin.getOutPoint();

                     //does this outpoint exist?
                     auto zcIter = mempool_.find(outpoint.getTxHashRef());
                     if (zcIter != mempool_.end()) {
                        //points to a zc, keep going
                        continue;
                     }

                     auto dbKey = iface_->getDBKeyForHash(outpoint.getTxHash());
                     if (dbKey.empty()) {
                        //there is no tx with this hash, outpoint is invalid
                        opFailure = true;
                        break;
                     }

                     //check spentness for this outpoint
                     StoredTxOut stxo;
                     BinaryRefReader keyReader(dbKey);
                     uint32_t blockid; uint8_t dup;
                     DBUtils::readBlkDataKeyNoPrefix(keyReader,
                        blockid, dup, stxo.txIndex);

                     auto headerPtr = blockchain_->getHeaderById(blockid);
                     stxo.blockHeight = headerPtr->getBlockHeight();
                     stxo.duplicateID = headerPtr->getDuplicateID();
                     stxo.txOutIndex = outpoint.getTxOutIndex();

                     try {
                        iface_->getSpentness(stxo);
                        if (stxo.isSpent()) {
                           opFailure = true;
                           break;
                        }
                     } catch (const LmdbWrapperException&) {
                        //indulgent check in fullnode
                     }
                  }

                  if (opFailure) {
                     break;
                  }

                  //check for RBFs & add to utxo set
                  bool replaceFailure = false;
                  std::set<BinaryData> replacedHashes;
                  for (unsigned i=0; i<tx.getNumTxIn(); i++) {
                     auto txin = tx.getTxInCopy(i);
                     auto outpoint = txin.getOutPoint();
                     auto hashIter = spenderSet_.find(outpoint.getTxHash());

                     if (hashIter == spenderSet_.end()) {
                        hashIter = spenderSet_.emplace(outpoint.getTxHash(),
                           std::map<unsigned, BinaryData>()).first;
                     }

                     auto idIter = hashIter->second.find(outpoint.getTxOutIndex());
                     if (idIter == hashIter->second.end()) {
                        hashIter->second.emplace(
                           outpoint.getTxOutIndex(), hashBd);
                        continue;
                     }

                     /*
                     This outpoint is consumed by a zc already in the mempool, 
                     need to purge it
                     */
                  
                     //grab the replaceable tx
                     auto replaceIter = mempool_.find(idIter->second);
                     if (replaceIter == mempool_.end()) {
                        throw std::runtime_error("missing tx in mempool");
                     }
                     auto mempoolObj = replaceIter->second;

                     //check the rbf flag
                     Tx replaceTx(mempoolObj->rawTx);
                     if (!replaceTx.isRBF()) {
                        //replaced tx isn't RBF
                        replaceFailure = true;
                        break;
                     }

                     if (!tx.isRBF()) {
                        //replacing tx isn't RBF
                        replaceFailure = true;
                        break;
                     }

                     //unit test shortcut: replace if the new tx fee is > 1 btc
                     auto txFee = getFeeForTx(tx);
                     if (txFee < 100000000) {
                        //fee too low to replace, push reject packet
                        auto rejectPayload = std::make_unique<Node::Payload_Reject>();

                        rejectPayload->rejectType_ = Node::PayloadType::Tx;
                        rejectPayload->code_ =
                           (char)ArmoryErrorCodes::P2PReject_InsufficientFee;

                        rejectPayload->extra_.resize(32);
                        memcpy(&rejectPayload->extra_[0], hashBd.getPtr(), 32);

                        processGetTx(std::move(rejectPayload));
                        replaceFailure = true;
                        break;
                     }
                     
                     //replace spender
                     idIter->second = tx.getThisHash();

                     //flag replaced tx
                     replacedHashes.insert(idIter->second);
                  }

                  if (replaceFailure) {
                     break;
                  }

                  for (auto& hash : replacedHashes) {
                     //purge replaced ZCs
                     auto txIter = mempool_.find(hash.getRef());
                     if (txIter == mempool_.end()) {
                        continue;
                     }
                     auto& mempoolObj = txIter->second;
                     Tx purgeTx(mempoolObj->rawTx);

                     //cleanup spender set
                     for (unsigned i=0; i<purgeTx.getNumTxIn(); i++) {
                        auto txin = purgeTx.getTxInCopy(i);
                        auto op = txin.getOutPoint();

                        auto spenderIter = spenderSet_.find(op.getTxHash());
                        if (spenderIter == spenderSet_.end()) {
                           continue;
                        }

                        auto idIter = spenderIter->second.find(op.getTxOutIndex());
                        if (idIter == spenderIter->second.end()) {
                           continue;
                        }

                        if (idIter->second != hash) {
                           continue;
                        }

                        spenderIter->second.erase(idIter);
                        if (spenderIter->second.empty()) {
                           spenderSet_.erase(spenderIter);
                        }
                     }
                     mempool_.erase(txIter);
                  }

                  //add to mempool
                  mempool_.emplace(obj->hash.getRef(), obj);

                  //send out the inventory payload through the watcher
                  watcherInvQueue_.push_back(std::move(hashBd));
                  break;
               }

               default:
                  throw std::runtime_error(
                     "inventry type support not implemented yet");
            }
         }
         break;
      }

      case Node::PayloadType::GetData:
      {
         /*
         Pushed getdata payload from armorydb to bitcoin node
         */

         //looking to get data for a previous inv tx message
         auto payloadSPtr = std::shared_ptr<Node::Payload>(std::move(payload));
         auto payloadGetData =
         std::dynamic_pointer_cast<Node::Payload_GetData>(payloadSPtr);
         if (payloadGetData == nullptr) {
            throw std::runtime_error("invalid payload type");
         }

         std::vector<BinaryData> grabbedTxs;
         for (auto& inv : payloadGetData->getInvVector()) {
            auto txMap = rawTxMap_.get();
            BinaryDataRef hash(&inv.hash[0], sizeof(inv.hash));
            auto iter = txMap->find(hash);
            if (iter == txMap->end()) {
               continue;
            }
            grabbedTxs.emplace_back(iter->first);
            auto payloadTx = std::make_unique<Node::Payload_Tx>();

            std::vector<uint8_t> rawTx(iter->second.getSize());
            memcpy(&rawTx[0], iter->second.getPtr(), iter->second.getSize());
            payloadTx->setRawTx(rawTx);
            getTxDataLambda_(move(payloadTx));
         }
         rawTxMap_.erase(grabbedTxs);
         break;
      }

      default:
         throw std::runtime_error("payload type support not implemented yet");
   }
}

////////
void NodeUnitTest::watcherProcess()
{
   while (true) {
      BinaryData hash;
      try {
         hash = std::move(watcherInvQueue_.pop_front());
      } catch (const Threading::StopBlockingLoop&) {
         break;
      }

      std::vector<Node::InvEntry> invVec;
      Node::InvEntry inv;
      inv.invtype = Node::Inv_Msg_Witness_Tx;
      memcpy(inv.hash, hash.getPtr(), 32);
      invVec.push_back(std::move(inv));
      processInvTx(invVec);
   }
   watcherInvQueue_.clear();
}

////////
void NodeUnitTest::skipZc(unsigned count)
{
   //the next [count] inv_tx sendMessage payloads will be skipped
   skipZc_.fetch_add(count, std::memory_order_relaxed);
}

void NodeUnitTest::delayNextZc(unsigned count)
{
   //next p2p inved zc will skip [count] blocks before it mines
   std::unique_lock<std::mutex> lock(sendMessageMutex_);
   zcDelays_.push_back(count);
}

void NodeUnitTest::stallNextZc(unsigned seconds)
{
   //next p2p inved zc will take [seconds] before replying with the 
   //getData message
   std::unique_lock<std::mutex> lock(sendMessageMutex_);
   zcStalls_.push_back(seconds);
}

////////////////////////////////////////////////////////////////////////////////
// NodeRPC_UnitTest
NodeRPC_UnitTest::NodeRPC_UnitTest(
   std::shared_ptr<NodeUnitTest> primaryNode,
   std::shared_ptr<NodeUnitTest> watcherNode) :
   CoreRPC::NodeRPCInterface(),
   primaryNode_(primaryNode), watcherNode_(watcherNode)
{}

void NodeRPC_UnitTest::shutdown()
{}

CoreRPC::RpcState NodeRPC_UnitTest::testConnection()
{
   return CoreRPC::RpcState::Online;
}

bool NodeRPC_UnitTest::canPoll() const
{
   return false;
}

void NodeRPC_UnitTest::waitOnChainSync(std::function<void(void)>)
{}

////////
void NodeRPC_UnitTest::stallNextZc(unsigned seconds)
{
   //next RPC broadcast will take [seconds] before replying with the 
   //getData message
   std::unique_lock<std::mutex> lock(zcStallMutex_);
   zcStalls_.push_back(seconds);
}

int NodeRPC_UnitTest::broadcastTx(const BinaryDataRef& rawTx, std::string&)
{
   auto iface = primaryNode_->iface_;
   if (iface == nullptr) {
      throw std::runtime_error("null iface ptr");
   }

   auto nodeUT = std::dynamic_pointer_cast<NodeUnitTest>(primaryNode_);
   if (nodeUT == nullptr) {
      throw std::runtime_error("invalid node ptr");
   }
   {
      std::unique_lock<std::mutex> lock(zcStallMutex_);
      if (!zcStalls_.empty()) {
         auto stall = zcStalls_.front();
         zcStalls_.pop_front();
         std::this_thread::sleep_for(std::chrono::seconds(stall));
      }
   }

   //is this tx already mined?
   Tx tx(rawTx);
   auto dbKey = iface->getDBKeyForHash(tx.getThisHash());
   if (dbKey.getSize() == 6) {
      StoredTxOut stxo;
      BinaryRefReader keyReader(dbKey);
      uint32_t blockid; uint8_t dup;
      DBUtils::readBlkDataKeyNoPrefix(keyReader, blockid, dup, stxo.txIndex);
      auto headerPtr = nodeUT->blockchain_->getHeaderById(blockid);
      stxo.blockHeight = headerPtr->getBlockHeight();
      stxo.duplicateID = headerPtr->getDuplicateID();

      //are any of its outpouts spent?
      unsigned spentCount = 0;
      for (unsigned i = 0; i < tx.getNumTxOut(); i++) {
         stxo.txOutIndex = i;
         iface->getSpentness(stxo);
         if (stxo.isSpent()) {
            spentCount++;
         }
      }

      /*
      A mined tx with no output in the utxo set fails rpc broadcast with a
      -25 error (node only has a snapshot of the utxo set, it cannot resolve
      "archived" outputs). Txs with unspent outputs will return already-in-chain
      errors.
      */
      if (spentCount == tx.getNumTxOut()) {
         return (int)ArmoryErrorCodes::ZcBroadcast_Error;
      } else {
         return (int)ArmoryErrorCodes::ZcBroadcast_AlreadyInChain;
      }
   }

   //check this zc isn't already in the node's mempool
   {
      auto mempoolIter = nodeUT->mempool_.find(tx.getThisHash());
      if (mempoolIter != nodeUT->mempool_.end()) {
         return (int)ArmoryErrorCodes::Success;
      }
   }

   //check sigs
   if (nodeUT->checkSigs_) {
      auto sigState = verifyTxSigs(rawTx, iface, nodeUT->mempool_);
      if (sigState != (int)ArmoryErrorCodes::Success) {
         return sigState;
      }
   }

   //check zc outpoints are valid and spendable
   for (unsigned i=0; i<tx.getNumTxIn(); i++) {
      auto txin = tx.getTxInCopy(i);
      auto outpoint = txin.getOutPoint();

      //is another zc spending this outpoint?
      auto iter = nodeUT->spenderSet_.find(outpoint.getTxHash());
      if (iter != nodeUT->spenderSet_.end()) {
         //cut corners here: skipping RBF checks
         return (int)ArmoryErrorCodes::ZcBroadcast_VerifyRejected;
      }

      //is this outpoint pointing to a zc?
      auto mempoolIter = nodeUT->mempool_.find(outpoint.getTxHashRef());
      if (mempoolIter != nodeUT->mempool_.end()) {
         //spends from a zc, keep going
         continue;
      }

      //is it in the chain?
      auto dbKeyInput = iface->getDBKeyForHash(outpoint.getTxHash());
      if (dbKeyInput.empty()) {
         //outpoint does not exist
         return (int)ArmoryErrorCodes::ZcBroadcast_Error;
      }

      //is this outpoint spent?
      StoredTxOut stxo;
      BinaryRefReader keyReader(dbKeyInput);
      uint32_t blockid; uint8_t dup;
      DBUtils::readBlkDataKeyNoPrefix(keyReader,
         blockid, dup, stxo.txIndex);

      auto headerPtr = nodeUT->blockchain_->getHeaderById(blockid);
      stxo.blockHeight = headerPtr->getBlockHeight();
      stxo.duplicateID = headerPtr->getDuplicateID();
      stxo.txOutIndex = outpoint.getTxOutIndex();

      iface->getSpentness(stxo);
      if (stxo.isSpent()) {
         return (int)ArmoryErrorCodes::ZcBroadcast_Error;
      }
   }

   std::vector<std::pair<BinaryData, unsigned>> pushVec;
   pushVec.push_back(std::make_pair(BinaryData(rawTx), 0));
   primaryNode_->pushZC(pushVec, false);
   watcherNode_->pushZC(pushVec, false);
   return (int)ArmoryErrorCodes::Success;
}

CoreRPC::FeeEstimateResult NodeRPC_UnitTest::getFeeByte(
   unsigned, const std::string&) const
{
   return CoreRPC::FeeEstimateResult();
}
