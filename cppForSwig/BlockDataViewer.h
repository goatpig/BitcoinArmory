////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <string>

#include <Ledgers/HistoryPager.h>
#include "BDV_Notification.h"
#include "bdmenums.h"

typedef enum
{
   order_ascending,
   order_descending
} HistoryOrdering;

enum class WalletRegType : int
{
   UNSET    = 0,
   WALLET   = 1,
   LOCKBOX  = 2
};

struct WalletRegistrationRequest
{
   const std::string& walletId;
   const std::vector<BinaryData> addresses;
   const bool isNew;
   const WalletRegType type;
   std::function<void(const std::set<BinaryDataRef>&)> zcCallback;

   WalletRegistrationRequest(const std::string& wId,
      std::vector<BinaryData>& addrs,
      bool isnew, WalletRegType wType) :
      walletId(wId), addresses(std::move(addrs)),
      isNew(isnew), type(wType),
      zcCallback(nullptr)
   {}
};

struct CombinedBalances
{
   struct BalanceAndCount
   {
      const uint64_t full;
      const uint64_t spendable;
      const uint64_t unconfirmed;
      const uint32_t txnCount;
   };

   struct Wallet
   {
      const BalanceAndCount bnc;
      const std::map<BinaryData, BalanceAndCount> addresses;
   };
   std::map<std::string, Wallet> wallets;
};

class ScrAddrFilter;
class BtcWallet;
class BlockDataManager;
struct StoredHeader;
class Tx;
class TxOut;
class TxIn;
struct StoredTxOut;
struct ScanWalletStruct;
struct UTXO;
struct Output;

namespace Armory
{
   namespace ZeroConf
   {
      class ZeroConfContainer;
   }

   namespace Ledgers
   {
      class Delegate;
      class HistoryPager;
   }

   class Blockchain;
}

////////////////////////////////////////////////////////////////////////////////
class ReadWriteLock
{
   /***
   You have to make sure a read lock request from a thread already holding
   a read lock ignores write lock requests, otherwise you could end up in
   a deadlock where a write lock is requested before a child read lock is.

   Example:
   T1 creates a read lock. T2 requests a write lock. At this point no new
   read locks can be created, until the write lock is fulfilled. The
   write lock can only be fulfilled if all current read locks are released.

   Within T1's first read lock, a new read lock is requested. This new lock
   will never be acquired, waiting forever on T2's write lock to be
   fulfilled first.

   T2's write lock will never be fulfilled, as it is waiting on T1's
   currently held read lock to be released. T1's current lock won't be
   released as it will never exist its scope, waiting for T1's second
   lock to be acquired.

   Incidentally, in the scope of a same thread, requesting a write lock
   within a read lock will always deadlock. The condition should be tested
   and thrown. Requesting a read lock within a write lock can and should
   be accomodated.
   ***/

   std::mutex all_lock;
   unsigned num_readers = 0;
   bool has_writer=false;
   std::condition_variable no_readers, no_writers;
   std::map<std::thread::id, unsigned> thread_ids_;

public:
   void lockRead(void);
   void unlockRead(void);
   void lockWrite(void);
   void unlockWrite(void);

   class ReadLock
   {
      ReadWriteLock *const l;
      bool locked=true;

   public:
      ReadLock(ReadWriteLock&);
      ~ReadLock(void);

      void unlock(void);
   };

   class WriteLock
   {
      ReadWriteLock *const l;
      bool locked=true;

   public:
      WriteLock(ReadWriteLock&);
      ~WriteLock(void);

      void unlock(void);
   };
};

////////////////////////////////////////////////////////////////////////////////
class BlockDataViewer;
struct WalletGroup
{
   std::map<std::string, std::shared_ptr<BtcWallet>> wallets;
   mutable ReadWriteLock lock;

   //The globalLedger (used to render the main transaction ledger) is
   //different from wallet ledgers. While each wallet only has a single
   //entry per transactions (wallets merge all of their scrAddr txn into
   //a single one), the globalLedger does not merge wallet level txn. It
   //can thus have several entries under the same transaction. Thus, this
   //cannot be a map nor a set.
   Armory::Ledgers::HistoryPager hist;
   HistoryOrdering order = order_descending;

   BlockDataViewer* bdvPtr = nullptr;
   std::shared_ptr<ScrAddrFilter> saf;

public:
   WalletGroup(BlockDataViewer*, std::shared_ptr<ScrAddrFilter>);
   ~WalletGroup(void);

   std::shared_ptr<BtcWallet> getOrSetWallet(const std::string&);
   void registerAddresses(WalletRegistrationRequest&);
   bool unregisterWallet(const std::string&);

   bool hasID(const std::string&) const;
   std::shared_ptr<BtcWallet> getWalletByID(const std::string&) const;

   void reset(void);
   size_t getPageCount(void) const;
   std::vector<Armory::Ledgers::Entry> getHistoryPage(
      uint32_t, unsigned, bool, bool);
   std::map<BinaryData, TxIOPair> getTxioForRange(uint32_t, uint32_t) const;

   std::map<uint32_t, uint32_t> computeWalletsSSHSummary(bool, bool);
   bool pageHistory(bool, bool);
   void updateLedgerFilter(const std::vector<std::string>&);

   void scanWallets(ScanWalletStruct&, int32_t);
   uint32_t getBlockInVicinity(uint32_t) const;
   uint32_t getPageIdForBlockHeight(uint32_t) const;
};

////////////////////////////////////////////////////////////////////////////////
class BlockDataViewer
{
public:
   BlockDataViewer(std::shared_ptr<BlockDataManager>);
   ~BlockDataViewer(void);
   void reset(void);

   /////////////////////////////////////////////////////////////////////////////
   // If you register you wallet with the BDM, it will automatically maintain 
   // tx lists relevant to that wallet.  You can get away without registering
   // your wallet objects (using scanBlockchainForTx), but without the full 
   // blockchain in RAM, each scan will take 30-120 seconds.  Registering makes 
   // sure that the intial blockchain scan picks up wallet-relevant stuff as 
   // it goes, and does a full [re-]scan of the blockchain only if necessary.
   void registerAWallet(WalletRegistrationRequest&);
   void registerAddresses(WalletRegistrationRequest&);
   void unregisterWallet(const std::string&);

   void scanWallets(std::shared_ptr<BDV_Notification>);
   bool hasWallet(const std::string&) const;
   Tx getTxByHash(BinaryDataRef) const;
   Tx getTxByKey(BinaryDataRef) const;
   TxOut getPrevTxOut(const TxIn&) const;
   Tx getPrevTx(const TxIn&) const;
   BinaryData getSenderScrAddr(const TxIn&) const;
   int64_t getSentValue(const TxIn&) const;

   LMDBBlockDatabase* getDB(void) const;
   BinaryData getTxHashForDbKey(const BinaryData&) const;
   Armory::ZeroConf::ZeroConfContainer* zcContainer(void) const;
   const Armory::Blockchain& blockchain(void) const;
   uint32_t getTopBlockHeight(void) const;
   const std::shared_ptr<BlockHeader> getTopBlockHeader(void) const;
   std::shared_ptr<BlockHeader> getHeaderByHash(const BinaryData&) const;

   size_t getWalletsPageCount(void) const;
   std::vector<Armory::Ledgers::Entry> getWalletsHistoryPage(
      uint32_t, bool, bool);

   size_t getLockboxesPageCount(void) const;
   std::vector<Armory::Ledgers::Entry> getLockboxesHistoryPage(
      uint32_t, bool, bool);

   StoredHeader getMainBlockFromDB(uint32_t height) const;
   StoredHeader getBlockFromDB(uint32_t height, uint8_t dupID) const;
   bool scrAddressIsRegistered(const BinaryData& scrAddr) const;

   bool isBDMRunning(void) const;
   void blockUntilBDMisReady(void) const;

   bool isTxOutSpentByZC(const BinaryData&) const;
   std::map<BinaryData, std::shared_ptr<const TxIOPair>>
   getUnspentZCForScrAddr(const BinaryData&) const;
   std::map<BinaryData, std::shared_ptr<const TxIOPair>>
   getRBFTxIOsforScrAddr(const BinaryData&) const;
   std::vector<TxOut> getZcTxOutsForKeys(const std::set<BinaryData>&) const;
   std::vector<UTXO> getZcUTXOsForKeys(const std::set<BinaryData>&) const;
   std::shared_ptr<ScrAddrFilter> getSAF(void) const;

   void updateWalletsLedgerFilter(const std::vector<std::string>&);
   void updateLockboxesLedgerFilter(const std::vector<std::string>&);

   uint32_t getBlockTimeByHeight(uint32_t) const;
   uint32_t getClosestBlockHeightForTime(uint32_t);

   std::shared_ptr<BtcWallet> getWalletOrLockbox(const std::string&) const;
   Armory::Ledgers::Delegate getLedgerDelegateForWallets(void);
   Armory::Ledgers::Delegate getLedgerDelegateForLockboxes(void);
   Armory::Ledgers::Delegate getLedgerDelegateForWallet(const std::string&);
   Armory::Ledgers::Delegate getLedgerDelegateForScrAddr(
      const std::string&, const BinaryData&);

   TxOut getTxOutCopy(const BinaryData&, uint16_t) const;
   TxOut getTxOutCopy(const BinaryData&) const;
   StoredTxOut getStoredTxOut(const BinaryData&) const;

   Tx getSpenderTxForTxOut(uint32_t, uint32_t, uint16_t) const;

   bool isZcEnabled(void) const;
   void flagRescanZC(bool);
   bool getZCflag(void) const;

   bool isRBF(const BinaryData&) const;
   bool hasScrAddress(const BinaryDataRef&) const;
   std::set<BinaryDataRef> getAddrSet(void) const;
   std::tuple<uint64_t, uint64_t> getAddrFullBalance(const BinaryData&);

   //wallet agnostic methods
   std::vector<UTXO> getUtxosForAddress(const BinaryDataRef&, bool) const;
   std::map<BinaryData, std::vector<Output>> getAddressOutpoints(
      const std::set<BinaryDataRef>&, unsigned&, unsigned&) const;
   std::vector<std::pair<StoredTxOut, BinaryDataRef>> getOutputsForOutpoints(
      const std::map<BinaryDataRef, std::set<unsigned>>&, bool) const;
   CombinedBalances getCombinedBalances(void) const;

   std::map<BinaryData, TxIOPair> getTxioForRange(uint32_t) const;
   std::map<BinaryData, std::shared_ptr<const TxIOPair>> getZcTxios(void) const;

protected:
   static void unregisterAddresses(
      std::set<BinaryData>, const std::function<void(void)>&);

protected:
   std::atomic<bool> rescanZC_;

   std::shared_ptr<BlockDataManager> bdm_;
   LMDBBlockDatabase* db_;
   std::shared_ptr<Armory::Blockchain> bc_;
   std::shared_ptr<ScrAddrFilter> saf_;

   uint32_t lastScanned_ = 0;
   const std::shared_ptr<Armory::ZeroConf::ZeroConfContainer> zeroConfCont_;
   int32_t updateID_ = 0;

   WalletGroup wallets_;
   WalletGroup lockboxes_;
};
