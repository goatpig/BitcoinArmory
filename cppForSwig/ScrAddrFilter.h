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

#ifndef _SCRADDRFILTER_H_
#define _SCRADDRFILTER_H_

#include <vector>
#include <atomic>
#include <functional>
#include <memory>

#include "ThreadSafeClasses.h"
#include "BinaryData.h"
#include "BtcWallet.h"

class ScrAddrFilter
{
   /***
   This class keeps track of all registered scrAddr to be scanned by the DB.
   If the DB isn't running in supernode, this class also acts as a helper to
   filter transactions, which is required in order to save only relevant ssh

   The transaction filter isn't exact however. It gets more efficient as it
   encounters more UTxO.

   The basic principle of the filter is that it expect to have a complete
   list of UTxO's starting a given height, usually where the DB picked up
   at initial load. It can then guarantee a TxIn isn't spending a tracked
   UTxO by checking the UTxO DBkey instead of fetching the entire stored TxOut.
   If the DBkey carries a height lower than the cut off, the filter will
   fail to give a definitive answer, in which case the TxOut script will be
   pulled from the DB, using the DBkey, as it would have otherwise.

   Registering addresses while the BDM isn't initialized will return instantly
   Otherwise, the following steps are taken:

   1) Check ssh entries in the DB for this scrAddr. If there is none, this
   DB never saw this address (full/lite node). Else mark the top scanned block.

   -- Non supernode operations --
   2.a) If the address is new, create an empty ssh header for that scrAddr
   in the DB, marked at the current top height
   2.b) If the address isn't new, scan it from its last seen block, or its
   block creation, or 0 if none of the above is available. This will create
   the ssh entries for the address, which will have the current top height as
   its scanned height.
   --

   3) Add address to scrAddrMap_

   4) Signal the wallet that the address is ready. Wallet object will take it
   up from there.
   ***/

   friend class BlockDataViewer;
   friend class ZeroConfContainer;

public:
   struct WalletInfo
   {
      static atomic<unsigned> idCounter_; //no need to init this

      function<void(bool)> callback_;
      set<BinaryData> scrAddrSet_;
      string ID_;
      const unsigned intID_;


      WalletInfo(void) :
         intID_(idCounter_.fetch_add(1, memory_order_relaxed))
      {}

      bool operator<(const ScrAddrFilter::WalletInfo& rhs) const
      {
         return intID_ < rhs.intID_;
      }
   };

   struct ScrAddrSideScanData
   {
      int32_t startScanFrom_= -1;
      vector<shared_ptr<WalletInfo>> wltInfoVec_;
      bool doScan_ = true;
      unsigned uniqueID_ = UINT32_MAX;

      BinaryData lastScannedBlkHash_;

      ScrAddrSideScanData(void) {}
      ScrAddrSideScanData(uint32_t height) :
         startScanFrom_(height) {}

      vector<string> getWalletIDString(void)
      {
         vector<string> strVec;
         for (auto& wltInfo : wltInfoVec_)
         {
            if (wltInfo->ID_.size() == 0)
               continue;

            strVec.push_back(wltInfo->ID_);
         }

         return strVec;
      }
   };

   struct AddrAndHash
   {
   private:
      mutable BinaryData addrHash_;

   public:
      const BinaryData scrAddr_;

   public:
      AddrAndHash(const BinaryData& addr) :
         scrAddr_(addr)
      {}

      AddrAndHash(const BinaryDataRef& addrref) :
         scrAddr_(addrref)
      {}

      const BinaryData& getHash(void) const
      {
         if (addrHash_.getSize() == 0)
            addrHash_ = move(BtcUtils::getHash256(scrAddr_));

         return addrHash_;
      }

      bool operator<(const AddrAndHash& rhs) const
      {
         return this->scrAddr_ < rhs.scrAddr_;
      }

      bool operator<(const BinaryDataRef& rhs) const
      {
         return this->scrAddr_.getRef() < rhs;
      }
   };
   
public:
   mutex mergeLock_;
   BinaryData lastScannedHash_;
   const ARMORY_DB_TYPE           armoryDbType_;

private:

   static atomic<unsigned> keyCounter_;
   static atomic<bool> run_;

   shared_ptr<TransactionalMap<AddrAndHash, int>>   scrAddrMap_;

   LMDBBlockDatabase *const       lmdb_;

   const unsigned uniqueKey_;

   //
   ScrAddrFilter*                 parent_ = nullptr;
   ScrAddrSideScanData            scrAddrDataForSideScan_;
   
   //false: dont scan
   //true: wipe existing ssh then scan
   bool                           isScanning_ = false;

   Pile<ScrAddrSideScanData> scanDataPile_;
   set<shared_ptr<WalletInfo>> scanningAddresses_;

private:
   static void cleanUpPreviousChildren(LMDBBlockDatabase* lmdb);

   shared_ptr<TransactionalMap<AddrAndHash, int>>
      getScrAddrTransactionalMap(void) const
   {
      return scrAddrMap_;
   }

protected:
   function<void(const vector<string>& wltIDs, double prog, unsigned time)>
      scanThreadProgressCallback_;

   static unsigned getUniqueKey(void)
   {
      return keyCounter_.fetch_add(1, memory_order_relaxed);
   }

public:

   static void init(void);

   ScrAddrFilter(LMDBBlockDatabase* lmdb, ARMORY_DB_TYPE armoryDbType)
      : lmdb_(lmdb), armoryDbType_(armoryDbType), 
      uniqueKey_(getUniqueKey())
   {
      //make sure we are running off of a clean SDBI set when instantiating the first
      //SAF object (held by the BDM object)
      if (uniqueKey_ == 0) 
         cleanUpPreviousChildren(lmdb);

      scrAddrMap_ = make_shared<TransactionalMap<AddrAndHash, int>>();
      scanThreadProgressCallback_ = 
         [](const vector<string>&, double, unsigned)->void {};
   }

   ScrAddrFilter(const ScrAddrFilter& sca) //copy constructor
      : lmdb_(sca.lmdb_), armoryDbType_(sca.armoryDbType_),
      uniqueKey_(getUniqueKey()) //even copies' keys are unique
   {
      scrAddrMap_ = make_shared<TransactionalMap<AddrAndHash, int>>();
   }
   
   virtual ~ScrAddrFilter() { }
   
   LMDBBlockDatabase* lmdb() { return lmdb_; }

   shared_ptr<map<AddrAndHash, int>> getScrAddrMap(void) const
   { 
      if (!run_.load(memory_order_relaxed))
      {
         LOGERR << "ScrAddrFilter flagged for termination";
         throw runtime_error("ScrAddrFilter flagged for termination");
      }
      return scrAddrMap_->get(); 
   }

   shared_ptr<map<TxOutScriptRef, int>> getOutScrRefMap(void)
   {
      getScrAddrCurrentSyncState();
      auto outset = make_shared<map<TxOutScriptRef, int>>();

      auto scrAddrMap = scrAddrMap_->get();

      for (auto& scrAddr : *scrAddrMap)
      {
         if (scrAddr.first.scrAddr_.getSize() == 0)
            continue;

         TxOutScriptRef scrRef;
         scrRef.setRef(scrAddr.first.scrAddr_);

         outset->insert(move(make_pair(scrRef, scrAddr.second)));
      }

      return outset;
   }

   size_t numScrAddr(void) const
   {
      return scrAddrMap_->size();
   }

   int32_t scanFrom(void) const;
   bool registerAddresses(const set<BinaryData>&, string, bool,
      function<void(bool)>);
   bool registerAddressBatch(
      vector<shared_ptr<WalletInfo>>&& wltInfoVec, bool areNew);

   void clear(void);

   void getScrAddrCurrentSyncState();
   int getScrAddrCurrentSyncState(BinaryData const & scrAddr);

   void setSSHLastScanned(uint32_t height);

   void regScrAddrVecForScan(
      const vector<pair<BinaryData, unsigned>>& addrVec)
   { 
      map<AddrAndHash, int> saMap;
      for (auto& addrpair : addrVec)
      {
         AddrAndHash aah(addrpair.first);
         saMap.insert(move(make_pair(move(aah), addrpair.second)));
      }

      scrAddrMap_->update(saMap);
   }

   static void scanFilterInNewThread(shared_ptr<ScrAddrFilter> sca);

   //pointer to the SAF object held by the bdm
   void setParent(ScrAddrFilter* sca) { parent_ = sca; }

   void addToMergePile(const BinaryData& lastScannedBlkHash);
   void mergeSideScanPile(void);

   void getAllScrAddrInDB(void);
   void putAddrMapInDB(void);

   BinaryData getAddressMapMerkle(void) const;
   bool hasNewAddresses(void) const;

   void updateAddressMerkleInDB(void);
   
   StoredDBInfo getSubSshSDBI(void) const;
   void putSubSshSDBI(const StoredDBInfo&);
   StoredDBInfo getSshSDBI(void) const;
   void putSshSDBI(const StoredDBInfo&);
   
   set<BinaryData> getMissingHashes(void) const;
   void putMissingHashes(const set<BinaryData>&);

   static void shutdown(void)
   {
      run_.store(false, memory_order_relaxed);
   }

public:
   virtual shared_ptr<ScrAddrFilter> copy()=0;

protected:
   virtual bool bdmIsRunning() const=0;
   virtual BinaryData applyBlockRangeToDB(
      uint32_t startBlock, uint32_t endBlock, const vector<string>& wltIDs,
      bool reportProgress)=0;
   virtual uint32_t currentTopBlockHeight() const=0;
   virtual void wipeScrAddrsSSH(const vector<BinaryData>& saVec) = 0;
   virtual shared_ptr<Blockchain> blockchain(void) = 0;
   virtual BlockDataManagerConfig config(void) = 0;

private:
   void scanScrAddrThread(void);
   void buildSideScanData(
      const vector<shared_ptr<WalletInfo>>& wltInfoSet, bool areNew);
};

#endif
// kate: indent-width 3; replace-tabs on;
