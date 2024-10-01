////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#ifndef SCRADDROBJ_H
#define SCRADDROBJ_H

#include "BinaryData.h"
#include "BlockchainDatabase/lmdb_wrapper.h"
#include "BlockchainDatabase/Blockchain.h"
#include "BlockchainDatabase/txio.h"
#include "BlockchainDatabase/BlockObj.h"
#include "ZeroConf.h"
#include "LedgerEntry.h"
#include "HistoryPager.h"

////////////////////////////////////////////////////////////////////////////////
//
// ScrAddrObj  
//
// This class is only for scanning the blockchain (information only).  It has
// no need to keep track of the public and private keys of various addresses,
// which is done by the python code leveraging this class.
//
// I call these as "scraddresses".  In most contexts, it represents an
// "address" that people use to send coins per-to-person, but it could actually
// represent any kind of TxOut script.  Multisig, P2SH, or any non-standard,
// unusual, escrow, whatever "address."  While it might be more technically
// correct to just call this class "Script" or "TxOutScript", I felt like 
// "address" is a term that will always exist in the Bitcoin ecosystem, and 
// frequently used even when not preferred.
//
// Similarly, we refer to the member variable scraddr_ as a "scradder". It
// is actually a reduction of the TxOut script to a form that is identical
// regardless of whether pay-to-pubkey or pay-to-pubkey-hash is used. 
//
//
////////////////////////////////////////////////////////////////////////////////
struct ScanAddressStruct
{
   std::map<BinaryData, BinaryData>* invalidatedZcKeys_ = nullptr;
   std::shared_ptr<MempoolSnapshot> zcState_;

   std::map<BinaryData, std::set<BinaryData>> scrAddrToTxioKeys_;
   std::map<std::string, std::map<BinaryData, LedgerEntry>> zcLedgers_;
   std::shared_ptr<std::map<BinaryData, 
      std::shared_ptr<std::set<BinaryDataRef>>>> newKeysAndScrAddr_;
};

class ScrAddrObj
{
   friend class BtcWallet;

private:
   struct pagedUTXOs
   {
      static const uint32_t UTXOperFetch = 100;

      std::map<BinaryData, TxIOPair> utxoList_;
      uint32_t topBlock_ = 0;
      uint64_t value_ = 0;

      /***We use a dedicate count here instead of map::size() so that a thread
      can update the map while another reading the struct won't be aware of the
      new entries until count_ is updated
      ***/
      uint32_t count_ = 0;
      
      const ScrAddrObj *scrAddrObj_;

      pagedUTXOs(const ScrAddrObj* scrAddrObj) : 
         scrAddrObj_(scrAddrObj)
      {}

      const std::map<BinaryData, TxIOPair>& getUTXOs(void) const
      { return utxoList_; }

      bool fetchMoreUTXO(std::function<bool(const BinaryData&)> spentByZC)
      {
         //return true if more UTXO were found, false otherwise
         if (topBlock_ < scrAddrObj_->bc_->top()->getBlockHeight())
         {
            uint32_t rangeTop;
            uint32_t count = 0;
            do
            {
               rangeTop = scrAddrObj_->hist_.getRangeForHeightAndCount(
                                                topBlock_, UTXOperFetch);
               count += fetchMoreUTXO(topBlock_, rangeTop, spentByZC);
            } 
            while (count < UTXOperFetch && rangeTop != UINT32_MAX);

            if (count > 0)
               return true;
         }

         return false;
      }

      uint32_t fetchMoreUTXO(uint32_t start, uint32_t end,
         std::function<bool(const BinaryData&)> spentByZC)
      {
         uint32_t nutxo = 0;
         uint64_t val = 0;

         StoredScriptHistory ssh;
         scrAddrObj_->db_->getStoredScriptHistory(ssh, 
            scrAddrObj_->scrAddr_, start, end);

         for (const auto& subsshPair : ssh.subHistMap_)
         {
            for (const auto& txioPair : subsshPair.second.txioMap_)
            {
               if (txioPair.second.isUTXO())
               {
                  //isMultisig only signifies this scrAddr was used in the
                  //composition of a funded multisig transaction. This is purely
                  //meta-data and shouldn't be returned as a spendable txout
                  if (txioPair.second.isMultisig())
                     continue;

                  if (spentByZC(txioPair.second.getDBKeyOfOutput()) == true)
                     continue;

                  auto txioAdded = utxoList_.insert(txioPair);

                  if (txioAdded.second == true)
                  {
                     val += txioPair.second.getValue();
                     nutxo++;
                  }
               }
            }
         }

         topBlock_ = end;
         value_ += val;
         count_ += nutxo;

         return nutxo;
      }

      uint64_t getValue(void) const { return value_; }
      uint32_t getCount(void) const { return count_; }

      void reset(void)
      {
         topBlock_ = 0;
         value_ = 0;
         count_ = 0;

         utxoList_.clear();
      }

      void addZcUTXOs(const std::map<BinaryData, TxIOPair>& txioMap)
      {
         BinaryData ZCheader(WRITE_UINT16_LE(0xFFFF));

         for (const auto& txio : txioMap)
         {
            if (!txio.first.startsWith(ZCheader))
               continue;

            if (txio.second.hasTxIn())
               continue;

            utxoList_.insert(txio);
         }
      }
   };

public:

   ScrAddrObj() :
      db_(nullptr),
      bc_(nullptr),
      totalTxioCount_(0), utxos_(this)
   {}

   ScrAddrObj(LMDBBlockDatabase *db, Blockchain *bc, ZeroConfContainer *zc,
      BinaryDataRef addr);

   ScrAddrObj(const ScrAddrObj& rhs) :
      utxos_(nullptr)
   {
      *this = rhs;
   }

   const BinaryDataRef& getScrAddr(void) const { return scrAddr_; }
//   void setScrAddr(LMDBBlockDatabase *db, BinaryData bd) { db_ = db; scrAddr_.copyFrom(bd);}

   // BlkNum is necessary for "unconfirmed" list, since it is dependent
   // on number of confirmations.  But for "spendable" TxOut list, it is
   // only a convenience, if you want to be able to calculate numConf from
   // the Utxos in the list.  If you don't care (i.e. you only want to 
   // know what TxOuts are available to spend, you can pass in 0 for currBlk
   uint64_t getFullBalance(unsigned updateID = UINT32_MAX) const;
   uint64_t getSpendableBalance(uint32_t currBlk) const;
   uint64_t getUnconfirmedBalance(uint32_t currBlk, unsigned confTarget) const;

   std::vector<UnspentTxOut> getFullTxOutList(uint32_t currBlk=UINT32_MAX, bool ignoreZC=true) const;
   std::vector<UnspentTxOut> getSpendableTxOutList(bool ignoreZC=true) const;
   
   std::vector<LedgerEntry> getTxLedgerAsVector(
      const std::map<BinaryData, LedgerEntry>* leMap) const;

   void clearBlkData(void);

   bool operator== (const ScrAddrObj& rhs) const
   { return (scrAddr_ == rhs.scrAddr_); }

   std::map<BinaryData, TxIOPair> scanZC(
      const ScanAddressStruct&, std::function<bool(const BinaryDataRef)>, int32_t);
   bool purgeZC(const std::set<BinaryData>&, const std::set<BinaryData>&);

   std::map<BinaryData, LedgerEntry> updateLedgers(
                      const std::map<BinaryData, TxIOPair>& txioMap,
                      uint32_t startBlock, uint32_t endBlock) const;

   void setTxioCount(uint64_t count) { totalTxioCount_ = count; }
   uint64_t getTxioCount(void) const { return getTxioCountFromSSH(true); }
   uint64_t getTxioCountFromSSH(bool withZc) const;

   void mapHistory(void);

   const std::map<uint32_t, uint32_t>& getHistSSHsummary(void) const
   { return hist_.getSSHsummary(); }

   std::map<BinaryData, TxIOPair> getHistoryForScrAddr(
      uint32_t startBlock, uint32_t endBlock,
      bool update,
      bool withMultisig = false) const;
   std::map<BinaryData, TxIOPair> getTxios(void) const;

   size_t getPageCount(void) const { return hist_.getPageCount(); }
   std::vector<LedgerEntry> getHistoryPageById(uint32_t id);

   ScrAddrObj& operator= (const ScrAddrObj& rhs);

   const std::map<BinaryData, TxIOPair>& getPreparedTxOutList(void) const
   { return utxos_.getUTXOs(); }

   bool getMoreUTXOs(pagedUTXOs&, 
      std::function<bool(const BinaryData&)> hasTxOutInZC) const;
   bool getMoreUTXOs(std::function<bool(const BinaryData&)> hasTxOutInZC);
   std::vector<UnspentTxOut> getAllUTXOs(
      std::function<bool(const BinaryData&)> hasTxOutInZC) const;

   uint64_t getLoadedTxOutsValue(void) const { return utxos_.getValue(); }
   uint32_t getLoadedTxOutsCount(void) const { return utxos_.getCount(); }

   void resetTxOutHistory(void) { utxos_.reset(); }

   void addZcUTXOs(const std::map<BinaryData, TxIOPair>& txioMap,
      std::function<bool(const BinaryData&)>)
   { utxos_.addZcUTXOs(txioMap); }

   uint32_t getBlockInVicinity(uint32_t blk) const;
   uint32_t getPageIdForBlockHeight(uint32_t blk) const;

   uint32_t getTxioCountForLedgers(void)
   {
      //return UINT32_MAX unless count has changed since last call
      //(or it's the first call)
      auto count = getTxioCountFromSSH(false);
      if (count == txioCountForLedgers_)
         return UINT32_MAX;

      txioCountForLedgers_ = count;
      return count;
   }

private:
   LMDBBlockDatabase *db_;
   Blockchain        *bc_;
   ZeroConfContainer *zc_;
   
   BinaryDataRef scrAddr_; //this includes the prefix byte!

   // Each address will store a list of pointers to its transactions   
   mutable uint64_t totalTxioCount_=0;
   mutable uint32_t lastSeenBlock_=0;

   uint32_t txioCountForLedgers_ = UINT32_MAX;

   //prebuild history indexes for quick fetch from ssh
   HistoryPager hist_;

   //fetches and maintains utxos
   pagedUTXOs   utxos_;

   std::map<BinaryData, BinaryData> zcInputKeys_;
   std::map<BinaryData, TxIOPair> zcTxios_;

   mutable int32_t updateID_ = 0;
   mutable uint64_t internalBalance_ = 0;
};

#endif
