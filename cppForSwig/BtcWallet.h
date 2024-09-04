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
#ifndef _BTCWALLET_H
#define _BTCWALLET_H

#include "BinaryData.h"
#include "BlockchainDatabase/BlockObj.h"
#include "BlockchainDatabase/StoredBlockObj.h"
#include "ScrAddrObj.h"
#include "bdmenums.h"
#include "ThreadSafeClasses.h"
#include "TxClasses.h"

class BlockDataManager;
class BlockDataViewer;

struct ScanWalletStruct
{
   BDV_Action action_;
   
   unsigned prevTopBlockHeight_;
   unsigned startBlock_;
   unsigned endBlock_ = UINT32_MAX;
   bool reorg_ = false;

   ScanAddressStruct saStruct_;
};

////////////////////////////////////////////////////////////////////////////////
//
// BtcWallet
//
////////////////////////////////////////////////////////////////////////////////
class BtcWallet
{
   friend class WalletGroup;
   friend class BDV_Server_Object;

   static const uint32_t MIN_UTXO_PER_TXN = 100;

public:
   BtcWallet(BlockDataViewer* bdv, const std::string ID)
      : bdvPtr_(bdv), walletID_(ID)
   {}

   ~BtcWallet(void)
   {}

   BtcWallet(const BtcWallet& wlt) = delete;

   /////////////////////////////////////////////////////////////////////////////
   // addScrAddr when blockchain rescan req'd, addNewScrAddr for just-created
   void removeAddressBulk(const std::vector<BinaryDataRef>&);
   bool hasScrAddress(const BinaryDataRef&) const;
   std::set<BinaryDataRef> getAddrSet(void) const;

   // BlkNum is necessary for "unconfirmed" list, since it is dependent
   // on number of confirmations.  But for "spendable" TxOut list, it is
   // only a convenience, if you want to be able to calculate numConf from
   // the Utxos in the list.  If you don't care (i.e. you only want to 
   // know what TxOuts are available to spend, you can pass in 0 for currBlk
   uint64_t getFullBalance(void) const;
   uint64_t getFullBalanceFromDB(unsigned) const;
   uint64_t getSpendableBalance(uint32_t currBlk) const;
   uint64_t getUnconfirmedBalance(uint32_t currBlk) const;

   std::map<BinaryData, uint32_t> getAddrTxnCounts(int32_t updateID) const;
   std::map<BinaryData, std::tuple<uint64_t, uint64_t, uint64_t>>
      getAddrBalances(int32_t updateID, unsigned blockheight) const;

   uint64_t getWltTotalTxnCount(void) const;

   void prepareTxOutHistory(uint64_t val);
   void prepareFullTxOutHistory(void);
   std::vector<UTXO> getSpendableTxOutListForValue(uint64_t val = UINT64_MAX);
   std::vector<UTXO> getSpendableTxOutListZC(void);
   std::vector<UTXO> getRBFTxOutList(void);

   void clearBlkData(void);
   
   std::vector<AddressBookEntry> createAddressBook(void);

   void reset(void);
   
   const ScrAddrObj* getScrAddrObjByKey(const BinaryData& key) const;
   ScrAddrObj& getScrAddrObjRef(const BinaryData& key);

   void setWalletID(const std::string &wltId) { walletID_ = wltId; }
   const std::string& walletID() const { return walletID_; }

   const HistoryPager& historyPager(void) const;
   std::shared_ptr<const std::map<BinaryData, LedgerEntry>> getHistoryPage(uint32_t);
   std::vector<LedgerEntry> getHistoryPageAsVector(uint32_t);
   size_t getHistoryPageCount(void) const { return histPages_.getPageCount(); }

   void needsRefresh(bool refresh);
   bool hasBdvPtr(void) const { return bdvPtr_ != nullptr; }

   void setRegistrationCallback(std::function<void(void)> lbd)
   {
      doneRegisteringCallback_ = lbd;
   }

   void setConfTarget(unsigned);

   std::shared_ptr<const std::map<BinaryDataRef, std::shared_ptr<ScrAddrObj>>>
      getAddrMap(void) const { return scrAddrMap_.get(); }
   void unregisterAddresses(const std::set<BinaryDataRef>&);

private:
   //returns true on bootstrap and new block, false on ZC
   bool scanWallet(ScanWalletStruct&, int32_t);

   //wallet side reorg processing
   //void updateAfterReorg(uint32_t lastValidBlockHeight);
   std::map<BinaryData, TxIOPair> scanWalletZeroConf(
      const ScanWalletStruct&, int32_t);

   void setRegistered(bool isTrue = true) { isRegistered_ = isTrue; }

   std::map<BinaryData, LedgerEntry> updateWalletLedgersFromTxio(
      const std::map<BinaryData, TxIOPair>& txioMap,
      uint32_t startBlock, uint32_t endBlock) const;

   void mapPages(void);
   bool isPaged(void) const;

   BlockDataViewer* getBdvPtr(void) const
   { return bdvPtr_; }

   std::map<uint32_t, uint32_t> computeScrAddrMapHistSummary(void);
   std::map<uint32_t, uint32_t> computeScrAddrMapHistSummary_Super(void);

   const std::map<uint32_t, uint32_t>& getSSHSummary(void) const
   { return histPages_.getSSHsummary(); }

   std::map<BinaryData, TxIOPair> getTxioForRange(uint32_t, uint32_t) const;
   void unregister(void) { isRegistered_ = false; }
   void resetTxOutHistory(void);
   void resetCounters(void);

private:

   BlockDataViewer* const        bdvPtr_;
   Armory::Threading::TransactionalMap<
      BinaryDataRef, std::shared_ptr<ScrAddrObj>> scrAddrMap_;
   
   bool ignoreLastScanned_ = true;
   bool isRegistered_ = false;
   
   //manages history pages
   HistoryPager                  histPages_;

   //wallet id
   std::string walletID_;

   uint64_t                      balance_ = 0;

   //set to true to add wallet paged history to global ledgers 
   bool                          uiFilter_ = true;

   //call this lambda once a wallet is done registering and scanning 
   //for the first time
   std::function<void(void)> doneRegisteringCallback_ = [](void)->void{};

   mutable int lastPulledCountsID_ = -1;
   mutable int lastPulledBalancesID_ = -1;
   int32_t updateID_ = 0;
   unsigned confTarget_ = MIN_CONFIRMATIONS;
};

#endif
// kate: indent-width 3; replace-tabs on;
