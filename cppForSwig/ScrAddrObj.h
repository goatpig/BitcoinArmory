////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <Utils/BinaryData.h>
#include <Ledgers/HistoryPager.h>

namespace Armory
{
   namespace ZeroConf
   {
      class MempoolSnapshot;
      class ZeroConfContainer;
   }

   namespace Ledgers
   {
      class Entry;
      class HistoryPager;
   }

   class Blockchain;
}

class LMDBBlockDatabase;
class UnspentTxOut;

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
////////////////////////////////////////////////////////////////////////////////
struct ScanAddressStruct
{
   std::map<BinaryData, BinaryData>* invalidatedZcKeys_ = nullptr;
   std::shared_ptr<const Armory::ZeroConf::MempoolSnapshot> zcState_;

   std::map<BinaryData, std::set<BinaryData>> scrAddrToTxioKeys_;
   std::vector<TxIOPair> txios;
   std::shared_ptr<std::map<BinaryData,
      std::shared_ptr<std::set<BinaryDataRef>>>> newKeysAndScrAddr_;
};

class ScrAddrObj
{
   friend class BtcWallet;

private:
   struct PagedUTXOs
   {
      static const uint32_t UTXOperFetch = 100;

      std::map<BinaryData, TxIOPair> utxoList;
      uint32_t topBlock = 0;
      uint64_t value = 0;

      /***We use a dedicate count here instead of map::size() so that a thread
      can update the map while another reading the struct won't be aware of the
      new entries until count_ is updated
      ***/
      uint32_t count = 0;
      const ScrAddrObj *scrAddrObj;

      PagedUTXOs(const ScrAddrObj*);

      const std::map<BinaryData, TxIOPair>& getUTXOs(void) const;
      bool fetchMoreUTXO(const std::function<bool(const BinaryData&)>&);
      uint32_t fetchMoreUTXO(uint32_t, uint32_t,
         const std::function<bool(const BinaryData&)>&);
      uint64_t getValue(void) const;
      uint32_t getCount(void) const;
      void reset(void);
      void addZcUTXOs(const std::map<BinaryData, TxIOPair>&);
   };

public:

   ScrAddrObj() :
      db_(nullptr),
      bc_(nullptr),
      totalTxioCount_(0), utxos_(this)
   {}

   ScrAddrObj(LMDBBlockDatabase*,
      const Armory::Blockchain*,
      Armory::ZeroConf::ZeroConfContainer*,
      BinaryDataRef);

   ScrAddrObj(const ScrAddrObj& rhs) :
      utxos_(nullptr)
   {
      *this = rhs;
   }

   const BinaryDataRef& getScrAddr(void) const { return scrAddr_; }

   // BlkNum is necessary for "unconfirmed" list, since it is dependent
   // on number of confirmations.  But for "spendable" TxOut list, it is
   // only a convenience, if you want to be able to calculate numConf from
   // the Utxos in the list.  If you don't care (i.e. you only want to 
   // know what TxOuts are available to spend, you can pass in 0 for currBlk
   uint64_t getFullBalance(unsigned=UINT32_MAX) const;
   uint64_t getSpendableBalance(uint32_t) const;
   uint64_t getUnconfirmedBalance(uint32_t, unsigned) const;

   std::vector<UnspentTxOut> getFullTxOutList(uint32_t=UINT32_MAX, bool=true) const;
   std::vector<UnspentTxOut> getSpendableTxOutList(bool=true) const;
   
   std::vector<Armory::Ledgers::Entry> getTxLedgerAsVector(
      const std::map<BinaryData, Armory::Ledgers::Entry>*) const;

   void clearBlkData(void);

   bool operator== (const ScrAddrObj& rhs) const
   { return (scrAddr_ == rhs.scrAddr_); }

   std::map<BinaryData, TxIOPair> scanZC(
      const ScanAddressStruct&, std::function<bool(const BinaryDataRef)>, int32_t);
   bool purgeZC(const std::set<BinaryData>&, const std::set<BinaryData>&);

   std::map<BinaryData, Armory::Ledgers::Entry> updateLedgers(
      const std::map<BinaryData, TxIOPair>&,
      uint32_t , uint32_t) const;

   void setTxioCount(uint64_t count) { totalTxioCount_ = count; }
   uint64_t getTxioCount(void) const { return getTxioCountFromSSH(true); }
   uint64_t getTxioCountFromSSH(bool) const;

   void mapHistory(void);

   const std::map<uint32_t, uint32_t>& getHistSSHsummary(void) const
   { return hist_.getSSHsummary(); }

   std::map<BinaryData, TxIOPair> getTxios(
      uint32_t, uint32_t, bool=false) const;

   size_t getPageCount(void) const { return hist_.getPageCount(); }
   std::vector<Armory::Ledgers::Entry> getHistoryPageById(uint32_t);

   ScrAddrObj& operator=(const ScrAddrObj&);

   const std::map<BinaryData, TxIOPair>& getPreparedTxOutList(void) const
   { return utxos_.getUTXOs(); }

   bool getMoreUTXOs(PagedUTXOs&,
      std::function<bool(const BinaryData&)>) const;
   bool getMoreUTXOs(std::function<bool(const BinaryData&)>);
   std::vector<UnspentTxOut> getAllUTXOs(
      std::function<bool(const BinaryData&)>) const;

   uint64_t getLoadedTxOutsValue(void) const { return utxos_.getValue(); }
   uint32_t getLoadedTxOutsCount(void) const { return utxos_.getCount(); }
   void resetTxOutHistory(void) { utxos_.reset(); }

   void addZcUTXOs(const std::map<BinaryData, TxIOPair>& txioMap,
      std::function<bool(const BinaryData&)>)
   { utxos_.addZcUTXOs(txioMap); }

   uint32_t getBlockInVicinity(uint32_t) const;
   uint32_t getPageIdForBlockHeight(uint32_t) const;
   uint32_t getTxioCountForLedgers(void);

private:
   LMDBBlockDatabase *db_;
   const Armory::Blockchain *bc_;
   Armory::ZeroConf::ZeroConfContainer *zc_;
   BinaryDataRef scrAddr_; //this includes the prefix byte!

   // Each address will store a list of pointers to its transactions
   mutable uint64_t totalTxioCount_ = 0;
   mutable uint32_t lastSeenBlock_ = 0;

   uint32_t txioCountForLedgers_ = UINT32_MAX;

   //prebuild history indexes for quick fetch from ssh
   Armory::Ledgers::HistoryPager hist_;

   //fetches and maintains utxos
   PagedUTXOs utxos_;

   std::map<BinaryData, BinaryData> zcInputKeys_;
   std::map<BinaryData, TxIOPair> zcTxios_;

   mutable int32_t updateID_ = 0;
   mutable uint64_t internalBalance_ = 0;
};
