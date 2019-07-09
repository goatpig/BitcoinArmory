////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018, goatpig.                                              //
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
#include "BlockDataManagerConfig.h"
#include "WebSocketClient.h"
#include "ClientClasses.h"
#include "SocketWritePayload.h"

class WalletManager;
class WalletContainer;

#define BIP150_COOKIE ".cookie_bip150_"

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
};

////
struct OutpointBatch
{
   unsigned heightCutoff_;
   unsigned zcIndexCutoff_;

   std::map<BinaryData, std::vector<OutpointData>> outpoints_;
};

///////////////////////////////////////////////////////////////////////////////
class ClientMessageError : public std::runtime_error
{
public:
   ClientMessageError(const std::string& err) :
      std::runtime_error(err)
   {}
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
struct ClientCache : public Lockable
{
private:
   std::map<BinaryData, Tx> txMap_;
   std::map<unsigned, BinaryData> rawHeaderMap_;
   std::map<BinaryData, unsigned> txHashToHeightMap_;

public:
   void insertTx(BinaryData&, Tx&);
   void insertRawHeader(unsigned&, BinaryDataRef);
   void insertHeightForTxHash(BinaryData&, unsigned&);

   const Tx& getTx(const BinaryDataRef&) const;
   const BinaryData& getRawHeader(const unsigned&) const;
   const unsigned& getHeightForTxHash(const BinaryData&) const;

   //virtuals
   void initAfterLock(void) {}
   void cleanUpBeforeUnlock(void) {}
};

class NoMatch
{};

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
namespace SwigClient
{
   class BlockDataViewer;
};

namespace AsyncClient
{
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
         std::function<void(ReturnMessage<std::vector<::ClientClasses::LedgerEntry>>)>);
      void getPageCount(std::function<void(ReturnMessage<uint64_t>)>) const;
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
      BinaryData addrHash_;
      const std::shared_ptr<SocketPrototype> sock_;

      const uint64_t fullBalance_;
      const uint64_t spendableBalance_;
      const uint64_t unconfirmedBalance_;
      const uint32_t count_;
      const int index_;

      std::string comment_;

   private:
      ScrAddrObj(const BinaryData& addr, const BinaryData& addrHash, int index) :
         bdvID_(std::string()), walletID_(std::string()),
         scrAddr_(addr), addrHash_(addrHash),
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

      void getSpendableTxOutList(bool, std::function<void(ReturnMessage<std::vector<UTXO>>)>);
      const BinaryData& getScrAddr(void) const { return scrAddr_; }
      const BinaryData& getAddrHash(void) const { return addrHash_; }

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

   public:
      BtcWallet(const BlockDataViewer&, const std::string&);
      
      void getBalancesAndCount(uint32_t topBlockHeight,
         std::function<void(ReturnMessage<std::vector<uint64_t>>)>);

      void getSpendableTxOutListForValue(uint64_t val, 
         std::function<void(ReturnMessage<std::vector<UTXO>>)>);
      void getSpendableZCList(std::function<void(ReturnMessage<std::vector<UTXO>>)>);
      void getRBFTxOutList(std::function<void(ReturnMessage<std::vector<UTXO>>)>);

      void getAddrTxnCountsFromDB(std::function<void(
         ReturnMessage<std::map<BinaryData, uint32_t>>)>);
      void getAddrBalancesFromDB(std::function<void(
         ReturnMessage<std::map<BinaryData, std::vector<uint64_t>>>)>);

      void getHistoryPage(uint32_t id, 
         std::function<void(ReturnMessage<std::vector<::ClientClasses::LedgerEntry>>)>);
      void getLedgerEntryForTxHash(
         const BinaryData& txhash, 
         std::function<void(ReturnMessage<std::shared_ptr<::ClientClasses::LedgerEntry>>)>);

      ScrAddrObj getScrAddrObjByKey(const BinaryData&,
         uint64_t, uint64_t, uint64_t, uint32_t);

      virtual std::string registerAddresses(
         const std::vector<BinaryData>& addrVec, bool isNew);
      void createAddressBook(
         std::function<void(ReturnMessage<std::vector<AddressBookEntry>>)>) const;

      std::string setUnconfirmedTarget(unsigned);
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
 
      std::string registerAddresses(
         const std::vector<BinaryData>& addrVec, bool isNew);
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
         std::function<void(ReturnMessage<ClientClasses::BlockHeader>)>);
      void getHeaderByHeight(
         unsigned height, 
         std::function<void(ReturnMessage<ClientClasses::BlockHeader>)>);
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
      friend class SwigClient::BlockDataViewer;

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

      bool connectToRemote(void);
      void addPublicKey(const SecureBinaryData&);
      BinaryData getPublicKey();
      BtcWallet instantiateWallet(const std::string& id);
      Lockbox instantiateLockbox(const std::string& id);

      std::string getID(void) const { return bdvID_; }
      std::shared_ptr<SocketPrototype> getSocketObject(void) const { return sock_; }
      std::string getPortStr() const { return sock_->getPortStr(); }
      BinaryData getBIP150ServerCookie(const std::string& dataDir);

      static std::shared_ptr<BlockDataViewer> getNewBDV(
         const std::string& addr, const std::string& port,
         const std::string& datadir, const bool& ephemeralPeers,
         std::shared_ptr<RemoteCallback> callbackPtr);

      void getLedgerDelegateForWallets(
         std::function<void(ReturnMessage<LedgerDelegate>)>);
      void getLedgerDelegateForLockboxes(
         std::function<void(ReturnMessage<LedgerDelegate>)>);
      void getLedgerDelegateForScrAddr(
         const std::string&, const BinaryData&,
         std::function<void(ReturnMessage<LedgerDelegate>)>);
      Blockchain blockchain(void);

      void goOnline(void);
      void registerWithDB(BinaryData magic_word);
      void unregisterFromDB(void);
      void shutdown(const std::string&);
      void shutdownNode(const std::string&);

      void broadcastZC(const BinaryData& rawTx);
      void getTxByHash(const BinaryData& txHash, 
         std::function<void(ReturnMessage<Tx>)>);
      void getRawHeaderForTxHash(
         const BinaryData& txHash, 
         std::function<void(ReturnMessage<BinaryData>)>);
      void getHeaderByHeight(
         unsigned height, 
         std::function<void(ReturnMessage<BinaryData>)>);

      void updateWalletsLedgerFilter(const std::vector<BinaryData>& wltIdVec);
      bool hasRemoteDB(void);

      void getNodeStatus(
         std::function<void(ReturnMessage<std::shared_ptr<::ClientClasses::NodeStatusStruct>>)>);
      void estimateFee(unsigned, const std::string&,
         std::function<void(ReturnMessage<ClientClasses::FeeEstimateStruct>)>);
      void getFeeSchedule(const std::string&, std::function<void(ReturnMessage<
            std::map<unsigned, ClientClasses::FeeEstimateStruct>>)>);

      void getHistoryForWalletSelection(
         const std::vector<std::string>&, const std::string& orderingStr,
         std::function<void(ReturnMessage<std::vector<::ClientClasses::LedgerEntry>>)>);

      void broadcastThroughRPC(const BinaryData& rawTx, 
         std::function<void(ReturnMessage<std::string>)>);

      void getUtxosForAddrVec(const std::vector<BinaryData>&,
         std::function<void(ReturnMessage<std::vector<UTXO>>)>);

      static std::unique_ptr<WritePayload_Protobuf> make_payload(
         ::Codec_BDVCommand::Methods, const std::string&);
      static std::unique_ptr<WritePayload_Protobuf> make_payload(
         ::Codec_BDVCommand::StaticMethods);

      std::pair<unsigned, unsigned> getRekeyCount(void) const;
      void setCheckServerKeyPromptLambda(
         std::function<bool(const BinaryData&, const std::string&)>);

      //combined methods
      void getCombinedBalances(
         const std::vector<std::string>&,
         std::function<void(
            ReturnMessage<std::map<std::string, CombinedBalances>>)>);
      
      void getCombinedAddrTxnCounts(
         const std::vector<std::string>&,
         std::function<void(
            ReturnMessage<std::map<std::string, CombinedCounts>>)>);

      void getCombinedSpendableTxOutListForValue(
         const std::vector<std::string>&, uint64_t value,
         std::function<void(ReturnMessage<std::vector<UTXO>>)>);
   
      void getCombinedSpendableZcOutputs(const std::vector<std::string>&, 
         std::function<void(ReturnMessage<std::vector<UTXO>>)>);

      void getCombinedRBFTxOuts(const std::vector<std::string>&, 
         std::function<void(ReturnMessage<std::vector<UTXO>>)>);

      //outpoints
      void getOutpointsForAddresses(const std::vector<BinaryData>&, 
         unsigned startHeight, unsigned zcIndexCutoff,
         std::function<void(ReturnMessage<OutpointBatch>)>);

      void getUTXOsForAddress(const BinaryData&, bool,
         std::function<void(ReturnMessage<std::vector<UTXO>>)>);
   };

   ////////////////////////////////////////////////////////////////////////////
   void deserialize(::google::protobuf::Message*, 
      const WebSocketMessagePartial&);
};


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//// callback structs for async networking
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_BinaryDataRef : public CallbackReturn_WebSocket
{
private:
   std::function<void(BinaryDataRef)> userCallbackLambda_;

public:
   CallbackReturn_BinaryDataRef(std::function<void(BinaryDataRef)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_String : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<std::string>)> userCallbackLambda_;

public:
   CallbackReturn_String(std::function<void(ReturnMessage<std::string>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_LedgerDelegate : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<AsyncClient::LedgerDelegate>)> userCallbackLambda_;
   std::shared_ptr<SocketPrototype> sockPtr_;
   const std::string& bdvID_;

public:
   CallbackReturn_LedgerDelegate(
      std::shared_ptr<SocketPrototype> sock, const std::string& bdvid,
      std::function<void(ReturnMessage<AsyncClient::LedgerDelegate>)> lbd) :
      userCallbackLambda_(lbd), sockPtr_(sock), bdvID_(bdvid)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_Tx : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<Tx>)> userCallbackLambda_;
   std::shared_ptr<ClientCache> cache_;
   BinaryData txHash_;

public:
   CallbackReturn_Tx(std::shared_ptr<ClientCache> cache,
      const BinaryData& txHash, std::function<void(ReturnMessage<Tx>)> lbd) :
      userCallbackLambda_(lbd), cache_(cache), txHash_(txHash)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_RawHeader : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<BinaryData>)> userCallbackLambda_;
   std::shared_ptr<ClientCache> cache_;
   BinaryData txHash_;
   unsigned height_;

public:
   CallbackReturn_RawHeader(
      std::shared_ptr<ClientCache> cache,
      unsigned height, const BinaryData& txHash, 
      std::function<void(ReturnMessage<BinaryData>)> lbd) :
      userCallbackLambda_(lbd),
      cache_(cache),txHash_(txHash), height_(height)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
class CallbackReturn_NodeStatusStruct : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<std::shared_ptr<::ClientClasses::NodeStatusStruct>>)>
      userCallbackLambda_;

public:
   CallbackReturn_NodeStatusStruct(std::function<void(
      ReturnMessage<std::shared_ptr<::ClientClasses::NodeStatusStruct>>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_FeeEstimateStruct : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<ClientClasses::FeeEstimateStruct>)>
      userCallbackLambda_;

public:
   CallbackReturn_FeeEstimateStruct(
      std::function<void(ReturnMessage<ClientClasses::FeeEstimateStruct>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_FeeSchedule : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<std::map<unsigned, ClientClasses::FeeEstimateStruct>>)>
      userCallbackLambda_;

public:
   CallbackReturn_FeeSchedule(std::function<void(ReturnMessage<
      std::map<unsigned, ClientClasses::FeeEstimateStruct>>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_VectorLedgerEntry : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<std::vector<::ClientClasses::LedgerEntry>>)>
      userCallbackLambda_;

public:
   CallbackReturn_VectorLedgerEntry(
      std::function<void(ReturnMessage<std::vector<::ClientClasses::LedgerEntry>>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_UINT64 : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<uint64_t>)> userCallbackLambda_;

public:
   CallbackReturn_UINT64(
      std::function<void(ReturnMessage<uint64_t>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_VectorUTXO : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<std::vector<UTXO>>)> userCallbackLambda_;

public:
   CallbackReturn_VectorUTXO(
      std::function<void(ReturnMessage<std::vector<UTXO>>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_VectorUINT64 : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<std::vector<uint64_t>>)> userCallbackLambda_;

public:
   CallbackReturn_VectorUINT64(
      std::function<void(ReturnMessage<std::vector<uint64_t>>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_Map_BD_U32 : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<std::map<BinaryData, uint32_t>>)> userCallbackLambda_;

public:
   CallbackReturn_Map_BD_U32(
      std::function<void(ReturnMessage<std::map<BinaryData, uint32_t>>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_Map_BD_VecU64 : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<std::map<BinaryData, std::vector<uint64_t>>>)>
      userCallbackLambda_;

public:
   CallbackReturn_Map_BD_VecU64(
      std::function<void(ReturnMessage<std::map<BinaryData, std::vector<uint64_t>>>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_LedgerEntry : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<std::shared_ptr<::ClientClasses::LedgerEntry>>)>
      userCallbackLambda_;

public:
   CallbackReturn_LedgerEntry(
      std::function<void(ReturnMessage<std::shared_ptr<::ClientClasses::LedgerEntry>>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_VectorAddressBookEntry : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<std::vector<AddressBookEntry>>)> userCallbackLambda_;

public:
   CallbackReturn_VectorAddressBookEntry(
      std::function<void(ReturnMessage<std::vector<AddressBookEntry>>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_Bool : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<bool>)> userCallbackLambda_;

public:
   CallbackReturn_Bool(std::function<void(ReturnMessage<bool>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_BlockHeader : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<ClientClasses::BlockHeader>)> userCallbackLambda_;
   const unsigned height_;

public:
   CallbackReturn_BlockHeader(unsigned height, 
      std::function<void(ReturnMessage<ClientClasses::BlockHeader>)> lbd) :
      userCallbackLambda_(lbd), height_(height)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_BDVCallback : public CallbackReturn_WebSocket
{
private:
   std::function<void(std::shared_ptr<::Codec_BDVCommand::BDVCallback>)>
      userCallbackLambda_;

public:
   CallbackReturn_BDVCallback(
      std::function<void(std::shared_ptr<::Codec_BDVCommand::BDVCallback>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_CombinedBalances : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<std::map<std::string, CombinedBalances>>)> 
      userCallbackLambda_;

public:
   CallbackReturn_CombinedBalances(
      std::function<void(
         ReturnMessage<std::map<std::string, CombinedBalances>>)> lbd) :
      userCallbackLambda_(lbd)
   {}
   
   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_CombinedCounts : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<std::map<std::string, CombinedCounts>>)> 
      userCallbackLambda_;

public:
   CallbackReturn_CombinedCounts(
      std::function<void(
         ReturnMessage<std::map<std::string, CombinedCounts>>)> lbd) :
      userCallbackLambda_(lbd)
   {}
   
   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_AddrOutpoints : public CallbackReturn_WebSocket
{
private:
   std::function<void(ReturnMessage<OutpointBatch>)>
      userCallbackLambda_;

public:
   CallbackReturn_AddrOutpoints(
      std::function<void(
         ReturnMessage<OutpointBatch>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

#endif
