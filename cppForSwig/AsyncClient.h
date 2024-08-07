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

///////////////////////////////////////////////////////////////////////////////
struct OutpointData
{
   BinaryData txHash_;
   unsigned txOutIndex_;
   
   unsigned txHeight_ = UINT32_MAX;
   unsigned txIndex_ = UINT32_MAX;

   uint64_t value_;
   bool isSpent_;

   BinaryData spenderHash_;

   //debug
   void prettyPrint(std::ostream&) const;
};

////
struct OutpointBatch
{
   unsigned heightCutoff_;
   unsigned zcIndexCutoff_;

   std::map<BinaryData, std::vector<OutpointData>> outpoints_;

   //debug
   void prettyPrint(void) const;
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
struct CombinedBalances
{
   BinaryData walletId_;
      
   /*
   {
      fullBalance,
      spendableBalance,
      unconfirmedBalance,
      wltTxnCount
   }
   */
   std::vector<uint64_t> walletBalanceAndCount_;

   /*
   {
      scrAddr (prefixed):
         {
            fullBalance,
            spendableBalance,
            unconfirmedBalance
         }
   }
   */

   std::map<BinaryData, std::vector<uint64_t>> addressBalances_;

   bool operator<(const CombinedBalances& rhs) const
   {
      return walletId_ < rhs.walletId_;
   }

   bool operator<(const BinaryData& rhs) const
   {
      return walletId_ < rhs;
   }
};

///////////////////////////////////////////////////////////////////////////////
struct CombinedCounts
{
   BinaryData walletId_;
      
   /*
   {
      scrAddr (prefixed): txn count
   }
   */
      
   std::map<BinaryData, uint64_t> addressTxnCounts_;

   bool operator<(const CombinedCounts& rhs) const
   {
      return walletId_ < rhs.walletId_;
   }
};

///////////////////////////////////////////////////////////////////////////////
namespace AsyncClient
{
   ///////////////////////////////////////////////////////////////////////////////
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
   typedef std::shared_ptr<const Tx> TxResult;
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

      void getHistoryPage(uint32_t id,
         std::function<void(ReturnMessage<std::vector<DBClientClasses::LedgerEntry>>)>);
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

      void getOutputs(std::function<void(ReturnMessage<std::vector<UTXO>>)>);
      const BinaryData& getScrAddr(void) const { return scrAddr_; }

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

      void getUTXOsForValue(uint64_t val, 
         std::function<void(ReturnMessage<std::vector<UTXO>>)>);

      void getHistoryPage(uint32_t id,
         std::function<void(ReturnMessage<std::vector<DBClientClasses::LedgerEntry>>)>);
      ScrAddrObj getScrAddrObjByKey(const BinaryData&,
         uint64_t, uint64_t, uint64_t, uint32_t);

      virtual void registerAddresses(
         const std::vector<BinaryData>& addrVec, bool isNew);
      void unregisterAddresses(const std::set<BinaryData>&);
      void unregister(void);

      void createAddressBook(
         std::function<void(ReturnMessage<std::vector<AddressBookEntry>>)>) const;

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
      void getHeaderByHash(const BinaryData& hash, 
         std::function<void(ReturnMessage<DBClientClasses::BlockHeader>)>);
      void getHeaderByHeight(
         unsigned height, 
         std::function<void(ReturnMessage<DBClientClasses::BlockHeader>)>);
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
      BtcWallet instantiateWallet(const std::string& id);
      Lockbox instantiateLockbox(const std::string& id);

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

      //header data
      Blockchain blockchain(void);

      //node & fee
      void getNodeStatus(
         std::function<void(ReturnMessage<std::shared_ptr<DBClientClasses::NodeStatus>>)>);
      void getFeeSchedule(const std::string&, std::function<void(ReturnMessage<
            std::map<unsigned, DBClientClasses::FeeEstimateStruct>>)>);

      //outputs
      void getUTXOsForAddress(std::set<BinaryData>&,
         std::function<void(ReturnMessage<std::vector<UTXO>>)>);

      void getOutputsForOutpoints(
         const std::map<BinaryData, std::set<unsigned>>&, bool,
         std::function<void(ReturnMessage<std::vector<UTXO>>)>);

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

      void getTxByHash(
         const std::set<BinaryData>&, const TxBatchCallback&);
   };
};
#endif
