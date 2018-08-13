////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2018, goatpig                                          //            
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                   
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _ZEROCONF_H_
#define _ZEROCONF_H_

#include <vector>
#include <atomic>
#include <functional>
#include <memory>

#include "ThreadSafeClasses.h"
#include "BitcoinP2p.h"
#include "lmdb_wrapper.h"
#include "Blockchain.h"
#include "ScrAddrFilter.h"

#define GETZC_THREADCOUNT 5
#define TXGETDATA_TIMEOUT_MS 3000

enum ZcAction
{
   Zc_NewTx,
   Zc_Purge,
   Zc_Shutdown
};

enum ParsedTxStatus
{
   Tx_Uninitialized,
   Tx_Resolved,
   Tx_ResolveAgain,
   Tx_Unresolved,
   Tx_Mined,
   Tx_Invalid
};

////////////////////////////////////////////////////////////////////////////////
struct ZeroConfData
{
   Tx            txobj_;
   uint32_t      txtime_;

   bool operator==(const ZeroConfData& rhs) const
   {
      return (this->txobj_ == rhs.txobj_) && (this->txtime_ == rhs.txtime_);
   }
};

////////////////////////////////////////////////////////////////////////////////
class OutPointRef
{
private:
   BinaryData txHash_;
   unsigned txOutIndex_ = UINT16_MAX;
   BinaryData dbKey_;
   uint64_t time_ = UINT64_MAX;

public:
   void unserialize(uint8_t const * ptr, uint32_t remaining);
   void unserialize(BinaryDataRef bdr);

   void resolveDbKey(LMDBBlockDatabase* db);
   const BinaryData& getDbKey(void) const { return dbKey_; }

   bool isResolved(void) const { return dbKey_.getSize() == 8; }
   bool isInitialized(void) const;

   BinaryDataRef getTxHashRef(void) const { return txHash_.getRef(); }
   unsigned getIndex(void) const { return txOutIndex_; }

   BinaryData& getDbKey(void) { return dbKey_; }
   BinaryDataRef getDbTxKeyRef(void) const;

   void reset(void)
   {
      dbKey_.clear();
      time_ = UINT64_MAX;
   }

   bool isZc(void) const;

   void setTime(uint64_t t) { time_ = t; }
   uint64_t getTime(void) const { return time_; }
};

////////////////////////////////////////////////////////////////////////////////
struct ParsedTxIn
{
   OutPointRef opRef_;
   BinaryData scrAddr_;
   uint64_t value_ = UINT64_MAX;

public:
   bool isResolved(void) const;
};

////////////////////////////////////////////////////////////////////////////////
struct ParsedTxOut
{
   BinaryData scrAddr_;
   uint64_t value_ = UINT64_MAX;

   bool isInitialized(void) const
   {
      return scrAddr_.getSize() != 0 && value_ != UINT64_MAX; \
   }
};

////////////////////////////////////////////////////////////////////////////////
struct ParsedTx
{
private:
   mutable BinaryData txHash_;

public:
   Tx tx_;
   vector<ParsedTxIn> inputs_;
   vector<ParsedTxOut> outputs_;
   ParsedTxStatus state_ = Tx_Uninitialized;
   bool isRBF_ = false;
   bool isChainedZc_ = false;


public:
   ParsedTxStatus status(void) const { return state_; }
   bool isResolved(void) const;
   void reset(void);

   const BinaryData& getTxHash(void) const;
};

////////////////////////////////////////////////////////////////////////////////
struct ZeroConfBatch
{
   map<BinaryData, ParsedTx> txMap_;
   atomic<unsigned> counter_;
   promise<bool> isReadyPromise_;
   unsigned timeout_ = TXGETDATA_TIMEOUT_MS;

public:
   ZeroConfBatch(void)
   {
      counter_.store(0, memory_order_relaxed);
   }

   void incrementCounter(void)
   {
      auto val = counter_.fetch_add(1, memory_order_relaxed);
      if (val + 1 == txMap_.size())
         isReadyPromise_.set_value(true);
   }
};

////////////////////////////////////////////////////////////////////////////////
struct ZeroConfInvPacket
{
   BinaryData zcKey_;
   shared_ptr<ZeroConfBatch> batchPtr_;
   InvEntry invEntry_;
};

////////////////////////////////////////////////////////////////////////////////
struct ParsedZCData
{
   set<BinaryData> txioKeys_;
   set<BinaryData> invalidatedKeys_;

   void mergeTxios(ParsedZCData& pzd)
   {
      txioKeys_.insert(pzd.txioKeys_.begin(), pzd.txioKeys_.end());
   }
};

////////////////////////////////////////////////////////////////////////////////
struct ZcPurgePacket
{
   set<BinaryData> invalidatedZcKeys_;
   map<BinaryData, BinaryData> minedTxioKeys_;
};

class ZeroConfCallbacks;

////////////////////////////////////////////////////////////////////////////////
class ZeroConfContainer
{
private:
   struct BulkFilterData
   {
      map<BinaryData, shared_ptr<map<BinaryData, TxIOPair>>> scrAddrTxioMap_;
      map<BinaryData, map<unsigned, BinaryData>> outPointsSpentByKey_;
      set<BinaryData> txOutsSpentByZC_;
      map<BinaryData, set<BinaryData>> keyToSpentScrAddr_;
      map<BinaryData, set<BinaryData>> keyToFundedScrAddr_;

      map<string, ParsedZCData> flaggedBDVs_;

      bool isEmpty(void) { return scrAddrTxioMap_.size() == 0; }
   };

public:
   struct NotificationPacket
   {
      string bdvID_;
      map<BinaryData, shared_ptr<map<BinaryData, TxIOPair>>> txioMap_;
      shared_ptr<ZcPurgePacket> purgePacket_;
      set<BinaryData> newZcKeys_;

      NotificationPacket(const string& bdvID) :
         bdvID_(bdvID)
      {}
   };

   struct ZcActionStruct
   {
      ZcAction action_;
      shared_ptr<ZeroConfBatch> batch_;
      unique_ptr<promise<shared_ptr<ZcPurgePacket>>> resultPromise_ = nullptr;
      Blockchain::ReorganizationState reorgState_;
   };

private:
   TransactionalMap<HashString, HashString>     txHashToDBKey_;      //<txHash, dbKey>
   TransactionalMap<HashString, ParsedTx>       txMap_;              //<zcKey, zcTx>
   TransactionalSet<HashString>                 txOutsSpentByZC_;    //<txOutDbKeys>
   set<HashString>                              allZcTxHashes_;
   
   //<txHash, map<opId, ZcKeys>>
   map<BinaryData, map<unsigned, BinaryData>>   outPointsSpentByKey_;

   //<scrAddr,  <dbKeyOfOutput, TxIOPair>>
   TransactionalMap<BinaryData, shared_ptr<map<BinaryData, TxIOPair>>>  txioMap_;

   //<zcKey, vector<ScrAddr>>
   TransactionalMap<HashString, set<HashString>> keyToSpentScrAddr_;
   map<BinaryData, set<BinaryData>> keyToFundedScrAddr_;

   std::atomic<uint32_t> topId_;
   LMDBBlockDatabase* db_;

   static map<BinaryData, TxIOPair> emptyTxioMap_;
   bool enabled_ = false;

   set<BinaryData> emptySetBinData_;

   //stacks inv tx packets from node
   shared_ptr<BitcoinP2P> networkNode_;
   BlockingQueue<ZeroConfInvPacket> newInvTxStack_;

   mutex parserMutex_;

   vector<thread> parserThreads_;
   atomic<bool> zcEnabled_;
   const unsigned maxZcThreadCount_;

   shared_ptr<TransactionalMap<BinaryDataRef, shared_ptr<AddrAndHash>>> scrAddrMap_;

   unsigned parserThreadCount_ = 0;
   mutex parserThreadMutex_;
   unique_ptr<ZeroConfCallbacks> bdvCallbacks_;

private:
   BulkFilterData ZCisMineBulkFilter(ParsedTx & tx, const BinaryData& ZCkey,
      function<bool(const BinaryData&, BinaryData&)> getzckeyfortxhash,
      function<const ParsedTx&(const BinaryData&)> getzctxbykey);

   void preprocessTx(ParsedTx&) const;

   void loadZeroConfMempool(bool clearMempool);
   map<BinaryData, BinaryData> purge(
      const Blockchain::ReorganizationState&, map<BinaryData, ParsedTx>&);
   void reset(void);

   void processInvTxThread(void);
   void processInvTxThread(ZeroConfInvPacket&);

   void increaseParserThreadPool(unsigned);
   void preprocessZcMap(map<BinaryData, ParsedTx>&);

public:
   //stacks new zc Tx objects from node
   BinaryData getNewZCkey(void);
   BlockingQueue<ZcActionStruct> newZcStack_;

public:
   ZeroConfContainer(LMDBBlockDatabase* db,
      shared_ptr<BitcoinP2P> node, unsigned maxZcThread) :
      topId_(0), db_(db), maxZcThreadCount_(maxZcThread), networkNode_(node)
   {
      zcEnabled_.store(false, memory_order_relaxed);

      //register ZC callback
      auto processInvTx = [this](vector<InvEntry> entryVec)->void
      {
         this->processInvTxVec(entryVec, true);
      };

      networkNode_->registerInvTxLambda(processInvTx);
   }

   bool hasTxByHash(const BinaryData& txHash) const;
   Tx getTxByHash(const BinaryData& txHash) const;

   shared_ptr<map<BinaryData, shared_ptr<map<BinaryData, TxIOPair>>>>
      getFullTxioMap(void) const { return txioMap_.get(); }

   void dropZC(const set<BinaryData>& txHashes);
   void parseNewZC(void);
   void parseNewZC(map<BinaryData, ParsedTx> zcMap, bool updateDB, bool notify);
   bool isTxOutSpentByZC(const BinaryData& dbKey) const;

   void clear(void);

   map<BinaryData, TxIOPair> getUnspentZCforScrAddr(BinaryData scrAddr) const;
   map<BinaryData, TxIOPair> getRBFTxIOsforScrAddr(BinaryData scrAddr) const;

   vector<TxOut> getZcTxOutsForKey(const set<BinaryData>&) const;

   const set<BinaryData>& getSpentSAforZCKey(const BinaryData& zcKey) const;
   const shared_ptr<map<BinaryData, set<HashString>>> getKeyToSpentScrAddrMap(void) const;

   void updateZCinDB(
      const vector<BinaryData>& keysToWrite, const vector<BinaryData>& keysToDel);

   void processInvTxVec(vector<InvEntry>, bool extend,
      unsigned timeout = TXGETDATA_TIMEOUT_MS);

   void init(shared_ptr<ScrAddrFilter>, bool clearMempool);
   void shutdown();

   void setZeroConfCallbacks(unique_ptr<ZeroConfCallbacks> ptr)
   {
      bdvCallbacks_ = move(ptr);
   }

   void broadcastZC(const BinaryData& rawzc,
      const string& bdvId, uint32_t timeout_ms);

   bool isEnabled(void) const { return zcEnabled_.load(memory_order_relaxed); }
   void pushZcToParser(const BinaryDataRef& rawTx);

   shared_ptr<map<BinaryData, TxIOPair>> getTxioMapForScrAddr(const BinaryData&) const;
};

////////////////////////////////////////////////////////////////////////////////
class ZeroConfCallbacks
{
public:
   virtual ~ZeroConfCallbacks(void) = 0;

   virtual set<string> hasScrAddr(const BinaryDataRef&) const = 0;
   virtual void pushZcNotification(
      ZeroConfContainer::NotificationPacket& packet) = 0;
   virtual void errorCallback(
      const string& bdvId, string& errorStr, const string& txHash) = 0;
};

#endif