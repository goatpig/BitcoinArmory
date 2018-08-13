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
#include "BlockDataManagerConfig.h"
#include "BtcUtils.h"
#include "StoredBlockObj.h"
#include "lmdb_wrapper.h"
#include "Blockchain.h"

#include <google/protobuf/message.h>

#define SIDESCAN_ID 0x100000ff

////////////////////////////////////////////////////////////////////////////////
struct AddressBatch
{
   function<void(set<BinaryDataRef>&)> callback_;
   
   set<BinaryDataRef> scrAddrSet_;
   shared_ptr<::google::protobuf::Message> msg_;
   const string batchID_;
   string walletID_;
   bool isNew_;

   AddressBatch(const string& id) :
      batchID_(id)
   {}
};

////////////////////////////////////////////////////////////////////////////////
class AddrAndHash
{
private:
   mutable BinaryData addrHash_;

public:
   const BinaryData scrAddr_;
   unsigned scannedHeight_ = 0;

public:
   AddrAndHash(BinaryDataRef addrRef) :
      scrAddr_(addrRef)
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

////////////////////////////////////////////////////////////////////////////////
class ScrAddrFilter
{
   friend class ZeroConfContainer;

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
   
private:
   const unsigned sdbiKey_;
   LMDBBlockDatabase *const lmdb_;

   shared_ptr<TransactionalMap<
      BinaryDataRef, shared_ptr<AddrAndHash>>> scrAddrMap_;
   BlockingQueue<shared_ptr<AddressBatch>> registrationStack_;

   thread thr_;

public:
   mutex mergeLock_;

private:
   static void cleanUpPreviousChildren(LMDBBlockDatabase* lmdb);
   void registrationThread(void);

   shared_ptr<TransactionalMap<BinaryDataRef, shared_ptr<AddrAndHash>>>
      getScrAddrMapPtr(void) const
   {
      return scrAddrMap_;
   }

   set<BinaryDataRef> updateAddrMap(const set<BinaryDataRef>&, unsigned);
   void setSSHLastScanned(set<BinaryDataRef>&, unsigned);

protected:
   function<void(
      const vector<string>& wltIDs, double prog, unsigned time)>
      scanThreadProgressCallback_;

public:

   ScrAddrFilter(LMDBBlockDatabase* lmdb, unsigned sdbiKey)
      : lmdb_(lmdb), sdbiKey_(sdbiKey)
   {
      scrAddrMap_ = make_shared<
         TransactionalMap<BinaryDataRef, shared_ptr<AddrAndHash>>>();
   }
   
   virtual ~ScrAddrFilter() { shutdown(); }
   
   LMDBBlockDatabase* db() { return lmdb_; }

   shared_ptr<map<BinaryDataRef, shared_ptr<AddrAndHash>>> 
      getScrAddrMap(void) const
   { 
      return scrAddrMap_->get(); 
   }
   
   size_t getScrAddrCount(void) const
   {
      return scrAddrMap_->size();
   }

   shared_ptr<map<TxOutScriptRef, int>> getOutScrRefMap(void);

   int32_t scanFrom(void) const;
   void registerAddressBatch(shared_ptr<AddressBatch>);

   void resetSshDB(void);

   void getScrAddrCurrentSyncState();
   void setSSHLastScanned(unsigned);
   void getAllScrAddrInDB(void);

   BinaryData getAddressMapMerkle(void) const;
   void updateAddressMerkleInDB(void);
   bool hasNewAddresses(void) const;
   
   StoredDBInfo getSubSshSDBI(void) const;
   void putSubSshSDBI(const StoredDBInfo&);
   StoredDBInfo getSshSDBI(void) const;
   void putSshSDBI(const StoredDBInfo&);
   
   set<BinaryData> getMissingHashes(void) const;
   void putMissingHashes(const set<BinaryData>&);

   void cleanUpSdbis(void);
   void shutdown(void);

   void init(void);

//virtuals
protected:
   virtual shared_ptr<ScrAddrFilter> getNew(unsigned) = 0;
   virtual BinaryData applyBlockRangeToDB(
      uint32_t startBlock, uint32_t endBlock, const vector<string>& wltIDs,
      bool reportProgress)=0;
   virtual shared_ptr<Blockchain> blockchain(void) const = 0;
   virtual bool bdmIsRunning(void) const = 0;
};

#endif
// kate: indent-width 3; replace-tabs on;
