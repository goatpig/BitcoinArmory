////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2024, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/***
Handle codec and socketing for armory client
***/

#ifndef _ASYNCCLIENT_H
#define _ASYNCCLIENT_H

#include <thread>

#include "StringSockets.h"
#include "bdmenums.h"
#include "log.h"
#include "TxClasses.h"
#include "ArmoryConfig.h"
#include "WebSocketClient.h"
#include "SocketWritePayload.h"
#include "Wallets/PassphraseLambda.h"

class WalletManager;
class WalletContainer;

////
struct OutputBatch
{
   unsigned heightCutoff;
   unsigned zcIndexCutoff;

   std::map<BinaryData, std::vector<Output>> addrMap;
};

///////////////////////////////////////////////////////////////////////////////
class ClientMessageError : public std::runtime_error
{
private:
   int errorCode_ = 0;

public:
   ClientMessageError(const std::string& err, unsigned errCode) :
      std::runtime_error(err), errorCode_(errCode)
   {}

   int errorCode(void) const { return errorCode_; }
};

///////////////////////////////////////////////////////////////////////////////
template<class U> class ReturnMessage
{
private:
   U value_;
   std::shared_ptr<ClientMessageError> error_;

public:
   ReturnMessage(void) :
      value_(U())
   {}

   ReturnMessage(U& val) :
      value_(std::move(val))
   {}

   ReturnMessage(const U& val) :
      value_(val)
   {}

   ReturnMessage(ClientMessageError& err)
   {
      error_ = std::make_shared<ClientMessageError>(err);
   }

   U get(void)
   { 
      if (error_ != nullptr)
         throw *error_;

      return std::move(value_);
   }
};


///////////////////////////////////////////////////////////////////////////////
namespace AsyncClient
{
   ////////////////////////////////////////////////////////////////////////////
   struct CombinedBalances
   {
      std::string walletId;

      /*
      {
         fullBalance,
         spendableBalance,
         unconfirmedBalance,
         wltTxnCount
      }
      */
      std::vector<uint64_t> walletBalanceAndCount;

      /*
      {
         scrAddr (prefixed):
         {
            fullBalance,
            spendableBalance,
            unconfirmedBalance,
            txnCount
         }
      }
      */
      std::map<BinaryData, std::vector<uint64_t>> addressBalances;

      bool operator<(const CombinedBalances& rhs) const
      {
         return walletId < rhs.walletId;
      }

      bool operator<(const std::string& rhs) const
      {
         return walletId < rhs;
      }
   };

   ////////////////////////////////////////////////////////////////////////////
   class ClientCache : public Lockable
   {
      friend struct CallbackReturn_Tx;
      friend struct CallbackReturn_TxBatch;
      
   private:
      std::map<BinaryData, std::shared_ptr<Tx>> txMap_;
      std::map<unsigned, BinaryData> rawHeaderMap_;
      std::map<BinaryData, unsigned> txHashToHeightMap_;

   private:
      std::shared_ptr<Tx> getTx_NoConst(const BinaryDataRef&);
      void insertTx(const BinaryData&, std::shared_ptr<Tx>);

   public:
      void insertTx(std::shared_ptr<Tx>);
      void insertRawHeader(unsigned&, BinaryDataRef);
      void insertHeightForTxHash(BinaryData&, unsigned&);

      std::shared_ptr<const Tx> getTx(const BinaryDataRef&) const;
      const BinaryData& getRawHeader(const unsigned&) const;
      const unsigned& getHeightForTxHash(const BinaryData&) const;

      //virtuals
      void initAfterLock(void) {}
      void cleanUpBeforeUnlock(void) {}
   };

   class NoMatch
   {};

   ///////////////////////////////////////////////////////////////////////////////
   typedef std::shared_ptr<Tx> TxResult;
   typedef std::function<void(ReturnMessage<TxResult>)> TxCallback;

   typedef std::map<BinaryData, TxResult> TxBatchResult;
   typedef std::function<void(ReturnMessage<TxBatchResult>)> TxBatchCallback;

   class BlockDataViewer;

   /////////////////////////////////////////////////////////////////////////////
   class LedgerDelegate
   {
   private:
      std::string delegateID_;
      std::string bdvID_;
      std::shared_ptr<SocketPrototype> sock_;

   public:
      LedgerDelegate(void) {}

      LedgerDelegate(std::shared_ptr<SocketPrototype>,
         const std::string&, const std::string&);

      void getHistoryPages(uint32_t from, uint32_t to,
         std::function<void(ReturnMessage<
            std::vector<DBClientClasses::HistoryPage>>)>);
      void getPageCount(std::function<void(ReturnMessage<uint64_t>)>) const;

      const std::string& getID(void) const { return delegateID_; }
   };

   class BtcWallet;

   /////////////////////////////////////////////////////////////////////////////
   class ScrAddrObj
   {
      friend class ::WalletContainer;

   private:
      const std::string bdvID_;
      const std::string walletID_;
      const BinaryData scrAddr_;
      const std::shared_ptr<SocketPrototype> sock_;

      const uint64_t fullBalance_;
      const uint64_t spendableBalance_;
      const uint64_t unconfirmedBalance_;
      const uint32_t count_;
      const int index_;

      std::string comment_;

   private:
      ScrAddrObj(const BinaryData& scrAddr, int index) :
         bdvID_(std::string()), walletID_(std::string()),
         scrAddr_(scrAddr),
         sock_(nullptr), 
         fullBalance_(0), spendableBalance_(0), unconfirmedBalance_(0),
         count_(0), index_(index)
      {}

   public:
      ScrAddrObj(BtcWallet*, const BinaryData&, int index,
         uint64_t, uint64_t, uint64_t, uint32_t);
      ScrAddrObj(std::shared_ptr<SocketPrototype>,
         const std::string&, const std::string&, const BinaryData&, int index,
         uint64_t, uint64_t, uint64_t, uint32_t);

      uint64_t getFullBalance(void) const { return fullBalance_; }
      uint64_t getSpendableBalance(void) const { return spendableBalance_; }
      uint64_t getUnconfirmedBalance(void) const { return unconfirmedBalance_; }

      uint64_t getTxioCount(void) const { return count_; }

      void getOutputs(uint64_t, bool, bool,
         std::function<void(ReturnMessage<std::vector<UTXO>>)>);
      const BinaryData& getScrAddr(void) const { return scrAddr_; }
      void getLedgerDelegate(
         std::function<void(ReturnMessage<LedgerDelegate>)>);

      void setComment(const std::string& comment) { comment_ = comment; }
      const std::string& getComment(void) const { return comment_; }
      int getIndex(void) const { return index_; }
   };

   /////////////////////////////////////////////////////////////////////////////
   class BtcWallet
   {
      friend class ScrAddrObj;

   protected:
      const std::string walletID_;
      const std::string bdvID_;
      const std::shared_ptr<SocketPrototype> sock_;
      std::string ledgerID_;

   public:
      BtcWallet(const BlockDataViewer&, const std::string&);
      void getBalancesAndCount(uint32_t topBlockHeight,
         std::function<void(ReturnMessage<std::vector<uint64_t>>)>);

      void getUTXOs(uint64_t val, bool, bool,
         std::function<void(ReturnMessage<std::vector<UTXO>>)>);

      ScrAddrObj getScrAddrObj(const BinaryData&,
         uint64_t, uint64_t, uint64_t, uint32_t);

      bool registerAddresses(
         const std::vector<BinaryData>& addrVec, bool isNew);
      void unregisterAddresses(const std::set<BinaryData>&);
      void unregister(void);

      void createAddressBook(
         std::function<void(ReturnMessage<std::vector<AddressBookEntry>>)>) const;
      void getLedgerDelegate(
         std::function<void(ReturnMessage<LedgerDelegate>)>);

      void setUnconfirmedTarget(unsigned);
      std::string walletID(void) const { return walletID_; }
   };

   /////////////////////////////////////////////////////////////////////////////
   class Lockbox : public BtcWallet
   {
   private:
      uint64_t fullBalance_ = 0;
      uint64_t spendableBalance_ = 0;
      uint64_t unconfirmedBalance_ = 0;

      uint64_t txnCount_ = 0;

   public:

      Lockbox(const BlockDataViewer& bdv, const std::string& id) :
         BtcWallet(bdv, id)
      {}

      void getBalancesAndCountFromDB(uint32_t topBlockHeight);

      uint64_t getFullBalance(void) const { return fullBalance_; }
      uint64_t getSpendableBalance(void) const { return spendableBalance_; }
      uint64_t getUnconfirmedBalance(void) const { return unconfirmedBalance_; }
      uint64_t getWltTotalTxnCount(void) const { return txnCount_; }
   };

   /////////////////////////////////////////////////////////////////////////////
   class Blockchain
   {
   private:
      const std::shared_ptr<SocketPrototype> sock_;
      const std::string bdvID_;

   public:
      Blockchain(const BlockDataViewer&);
      void getHeadersByHash(const std::set<BinaryDataRef>& hash,
         std::function<void(
            ReturnMessage<std::vector<DBClientClasses::BlockHeader>>)>);
      void getHeadersByHeight(const std::vector<unsigned> heights,
         std::function<void(
            ReturnMessage<std::vector<DBClientClasses::BlockHeader>>)>);
   };

   /////////////////////////////////////////////////////////////////////////////
   class BlockDataViewer
   {
      friend class ScrAddrObj;
      friend class BtcWallet;
      friend class RemoteCallback;
      friend class LedgerDelegate;
      friend class Blockchain;
      friend class ::WalletManager;

   private:
      std::string bdvID_;
      std::shared_ptr<SocketPrototype> sock_;
      std::shared_ptr<ClientCache> cache_;

   private:
      BlockDataViewer(void);
      BlockDataViewer(std::shared_ptr<SocketPrototype> sock);
      bool isValid(void) const { return sock_ != nullptr; }

      const BlockDataViewer& operator=(const BlockDataViewer& rhs)
      {
         bdvID_ = rhs.bdvID_;
         sock_ = rhs.sock_;
         cache_ = rhs.cache_;

         return *this;
      }

   public:
      ~BlockDataViewer(void);
      BtcWallet getWalletObj(const std::string& id);
      Lockbox getLockboxObj(const std::string& id);

      //BIP15x
      std::pair<unsigned, unsigned> getRekeyCount(void) const;
      void setCheckServerKeyPromptLambda(
         std::function<bool(const BinaryData&, const std::string&)>);
      void addPublicKey(const SecureBinaryData&);

      //connectivity
      bool connectToRemote(void);
      std::shared_ptr<SocketPrototype> getSocketObject(void) const { return sock_; }
      void goOnline(void);
      bool hasRemoteDB(void);

      //setup
      const std::string& getID(void) const { return bdvID_; }
      static std::shared_ptr<BlockDataViewer> getNewBDV(
         const std::string& addr, const std::string& port,
         const std::string& datadir, const PassphraseLambda&,
         bool ephemeralPeers, bool oneWayAuth,
         std::shared_ptr<RemoteCallback> callbackPtr);

      void registerWithDB(const std::string& magic_word);
      void unregisterFromDB(void);
      void shutdown(const std::string&);
      void shutdownNode(const std::string&);

      //ledgers
      void updateWalletsLedgerFilter(const std::vector<std::string>& wltIdVec);
      void getLedgerDelegate(
         std::function<void(ReturnMessage<LedgerDelegate>)>);

      //header data
      Blockchain blockchain(void);

      //node & fee
      void getNodeStatus(
         std::function<void(ReturnMessage<std::shared_ptr<DBClientClasses::NodeStatus>>)>);
      void getFeeSchedule(const std::string&, std::function<void(ReturnMessage<
            std::map<unsigned, DBClientClasses::FeeEstimateStruct>>)>);

      //balances & outputs
      void getCombinedBalances(std::function<void(
         ReturnMessage<std::map<std::string, CombinedBalances>>)>);

      void getOutputsForAddresses(std::set<BinaryData>&, uint32_t, uint32_t,
         std::function<void(ReturnMessage<OutputBatch>)>);
      void getOutputsForOutpoints(
         const std::map<BinaryData, std::set<unsigned>>&, bool,
         std::function<void(ReturnMessage<std::vector<Output>>)>);

      /*
      Broadcast methods:
        All broadcast methods generate and return a random BROADCAST_ID_LENGTH 
        bytes long ID. This ID will be attached to the broadcast notification 
        for the relevant transactions. Notifications for these transaction may
        come with no ID attached, in which case these notifications are not the
        result of your broadcast.
      */
      void broadcastZC(const std::vector<BinaryData>& rawTxVec);
      void broadcastThroughRPC(const BinaryData& rawTx);

      void getTxsByHash(
         const std::set<BinaryData>&, const TxBatchCallback&);
   };
};
#endif
