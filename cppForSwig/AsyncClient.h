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

class WalletManager;
class WalletContainer;

class ClientMessageError : public runtime_error
{
public:
   ClientMessageError(const string& err) :
      runtime_error(err)
   {}
};

namespace SwigClient
{
   class BlockDataViewer;
};

namespace AsyncClient
{
   static bool textSerialization_ = false;

   class BlockDataViewer;

   ///////////////////////////////////////////////////////////////////////////////
   class LedgerDelegate
   {
   private:
      string delegateID_;
      string bdvID_;
      shared_ptr<SocketPrototype> sock_;

   public:
      LedgerDelegate(void)
      {}

      LedgerDelegate(shared_ptr<SocketPrototype>, const string&, const string&);

      void getHistoryPage(uint32_t id, 
         function<void(vector<::ClientClasses::LedgerEntry>)>);
   };

   class BtcWallet;

   ///////////////////////////////////////////////////////////////////////////////
   class ScrAddrObj
   {
      friend class ::WalletContainer;

   private:
      const string bdvID_;
      const string walletID_;
      const BinaryData scrAddr_;
      BinaryData addrHash_;
      const shared_ptr<SocketPrototype> sock_;

      const uint64_t fullBalance_;
      const uint64_t spendableBalance_;
      const uint64_t unconfirmedBalance_;
      const uint32_t count_;
      const int index_;

      string comment_;

   private:
      ScrAddrObj(const BinaryData& addr, const BinaryData& addrHash, int index) :
         bdvID_(string()), walletID_(string()), index_(index),
         scrAddr_(addr), addrHash_(addrHash),
         sock_(nullptr), count_(0),
         fullBalance_(0), spendableBalance_(0), unconfirmedBalance_(0)
      {}

   public:
      ScrAddrObj(BtcWallet*, const BinaryData&, int index,
         uint64_t, uint64_t, uint64_t, uint32_t);
      ScrAddrObj(shared_ptr<SocketPrototype>,
         const string&, const string&, const BinaryData&, int index,
         uint64_t, uint64_t, uint64_t, uint32_t);

      uint64_t getFullBalance(void) const { return fullBalance_; }
      uint64_t getSpendableBalance(void) const { return spendableBalance_; }
      uint64_t getUnconfirmedBalance(void) const { return unconfirmedBalance_; }

      uint64_t getTxioCount(void) const { return count_; }

      void getSpendableTxOutList(bool, function<void(vector<UTXO>)>);
      const BinaryData& getScrAddr(void) const { return scrAddr_; }
      const BinaryData& getAddrHash(void) const { return addrHash_; }

      void setComment(const string& comment) { comment_ = comment; }
      const string& getComment(void) const { return comment_; }
      int getIndex(void) const { return index_; }
   };

   ///////////////////////////////////////////////////////////////////////////////
   class BtcWallet
   {
      friend class ScrAddrObj;

   protected:
      const string walletID_;
      const string bdvID_;
      const shared_ptr<SocketPrototype> sock_;

   public:
      BtcWallet(const BlockDataViewer&, const string&);
      
      void getBalancesAndCount(uint32_t topBlockHeight,
         function<void(vector<uint64_t>)>);

      void getSpendableTxOutListForValue(uint64_t val, 
         function<void(vector<UTXO>)>);
      void getSpendableZCList(function<void(vector<UTXO>)>);
      void getRBFTxOutList(function<void(vector<UTXO>)>);

      void getAddrTxnCountsFromDB(function<void(map<BinaryData, uint32_t>)>);
      void getAddrBalancesFromDB(function<void(map<BinaryData, vector<uint64_t>>)>);

      void getHistoryPage(uint32_t id, 
         function<void(vector<::ClientClasses::LedgerEntry>)>);
      void getLedgerEntryForTxHash(
         const BinaryData& txhash, 
         function<void(shared_ptr<::ClientClasses::LedgerEntry>)>);

      ScrAddrObj getScrAddrObjByKey(const BinaryData&,
         uint64_t, uint64_t, uint64_t, uint32_t);

      virtual string registerAddresses(
         const vector<BinaryData>& addrVec, bool isNew);
      void createAddressBook(function<void(vector<AddressBookEntry>)>) const;
   };

   ///////////////////////////////////////////////////////////////////////////////
   class Lockbox : public BtcWallet
   {
   private:
      uint64_t fullBalance_ = 0;
      uint64_t spendableBalance_ = 0;
      uint64_t unconfirmedBalance_ = 0;

      uint64_t txnCount_ = 0;

   public:

      Lockbox(const BlockDataViewer& bdv, const string& id) :
         BtcWallet(bdv, id)
      {}

      void getBalancesAndCountFromDB(uint32_t topBlockHeight);

      uint64_t getFullBalance(void) const { return fullBalance_; }
      uint64_t getSpendableBalance(void) const { return spendableBalance_; }
      uint64_t getUnconfirmedBalance(void) const { return unconfirmedBalance_; }
      uint64_t getWltTotalTxnCount(void) const { return txnCount_; }
 
      string registerAddresses(
         const vector<BinaryData>& addrVec, bool isNew);
   };

   ///////////////////////////////////////////////////////////////////////////////
   class Blockchain
   {
   private:
      const shared_ptr<SocketPrototype> sock_;
      const string bdvID_;

   public:
      Blockchain(const BlockDataViewer&);
      void getHeaderByHash(const BinaryData& hash, 
         function<void(ClientClasses::BlockHeader)>);
      void getHeaderByHeight(
         unsigned height, function<void(ClientClasses::BlockHeader)>);
   };

   ///////////////////////////////////////////////////////////////////////////////
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
      string bdvID_;
      shared_ptr<SocketPrototype> sock_;

      //save all tx we fetch by hash to reduce resource cost on redundant fetches
      shared_ptr<map<BinaryData, Tx>> txMap_;
      shared_ptr<map<BinaryData, BinaryData>> rawHeaderMap_;

      mutable unsigned topBlock_ = 0;

   private:
      BlockDataViewer(void);
      BlockDataViewer(const shared_ptr<SocketPrototype> sock);
      bool isValid(void) const { return sock_ != nullptr; }

      const BlockDataViewer& operator=(const BlockDataViewer& rhs)
      {
         bdvID_ = rhs.bdvID_;
         sock_ = rhs.sock_;
         txMap_ = rhs.txMap_;

         return *this;
      }

      void setTopBlock(unsigned block) const { topBlock_ = block; }

   public:
      ~BlockDataViewer(void);
      BtcWallet instantiateWallet(const string& id);
      Lockbox instantiateLockbox(const string& id);

      const string& getID(void) const { return bdvID_; }
      shared_ptr<SocketPrototype> getSocketObject(void) const { return sock_; }

      static BlockDataViewer getNewBDV(
         const string& addr, const string& port, SocketType);

      void getLedgerDelegateForWallets(function<void(LedgerDelegate)>);
      void getLedgerDelegateForLockboxes(function<void(LedgerDelegate)>);
      void getLedgerDelegateForScrAddr(
         const string&, const BinaryData&, function<void(LedgerDelegate)>);
      Blockchain blockchain(void);

      void goOnline(void);
      void registerWithDB(BinaryData magic_word);
      void unregisterFromDB(void);
      void shutdown(const string&);
      void shutdownNode(const string&);

      void broadcastZC(const BinaryData& rawTx);
      void getTxByHash(const BinaryData& txHash, function<void(Tx)>);
      void getRawHeaderForTxHash(
         const BinaryData& txHash, function<void(BinaryData)>);

      void updateWalletsLedgerFilter(const vector<BinaryData>& wltIdVec);
      bool hasRemoteDB(void);

      void getNodeStatus(
         function<void(shared_ptr<::ClientClasses::NodeStatusStruct>)>);
      unsigned getTopBlock(void) const { return topBlock_; }
      void estimateFee(unsigned, const string&, 
         function<void(ClientClasses::FeeEstimateStruct)>);

      void getHistoryForWalletSelection(
         const vector<string>& wldIDs, const string& orderingStr,
         function<void(vector<::ClientClasses::LedgerEntry>)>);

      void broadcastThroughRPC(const BinaryData& rawTx, function<void(string)>);

      void getUtxosForAddrVec(const vector<BinaryData>&, 
         function<void(vector<UTXO>)>);

      RemoteCallbackSetupStruct getRemoteCallbackSetupStruct(void) const;

      static unique_ptr<WritePayload_Protobuf> make_payload(
         ::Codec_BDVCommand::Methods, const string&);
      static unique_ptr<WritePayload_Protobuf> make_payload(
         ::Codec_BDVCommand::StaticMethods);
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
   function<void(BinaryDataRef)> userCallbackLambda_;

public:
   CallbackReturn_BinaryDataRef(function<void(BinaryDataRef)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_String : public CallbackReturn_WebSocket
{
private:
   function<void(string)> userCallbackLambda_;

public:
   CallbackReturn_String(function<void(string)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_LedgerDelegate : public CallbackReturn_WebSocket
{
private:
   function<void(AsyncClient::LedgerDelegate)> userCallbackLambda_;
   shared_ptr<SocketPrototype> sockPtr_;
   const string& bdvID_;

public:
   CallbackReturn_LedgerDelegate(
      shared_ptr<SocketPrototype> sock, const string& bdvid,
      function<void(AsyncClient::LedgerDelegate)> lbd) :
      sockPtr_(sock), bdvID_(bdvid), userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_Tx : public CallbackReturn_WebSocket
{
private:
   function<void(Tx)> userCallbackLambda_;
   shared_ptr<map<BinaryData, Tx>> txMap_;
   BinaryData txHash_;

public:
   CallbackReturn_Tx(shared_ptr<map<BinaryData, Tx>> mapPtr,
      const BinaryData& txHash, function<void(Tx)> lbd) :
      txMap_(mapPtr), txHash_(txHash), userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_RawHeader : public CallbackReturn_WebSocket
{
private:
   function<void(BinaryData)> userCallbackLambda_;
   shared_ptr<map<BinaryData, BinaryData>> rawHeaderMap_;   
   BinaryData txHash_;

public:
   CallbackReturn_RawHeader(shared_ptr<map<BinaryData, BinaryData>> headerMap,
      const BinaryData& txHash, function<void(BinaryData)> lbd) :
      rawHeaderMap_(headerMap), txHash_(txHash), userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_NodeStatusStruct : public CallbackReturn_WebSocket
{
private:
   function<void(shared_ptr<::ClientClasses::NodeStatusStruct>)> 
      userCallbackLambda_;

public:
   CallbackReturn_NodeStatusStruct(
      function<void(shared_ptr<::ClientClasses::NodeStatusStruct>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_FeeEstimateStruct : public CallbackReturn_WebSocket
{
private:
   function<void(ClientClasses::FeeEstimateStruct)> userCallbackLambda_;

public:
   CallbackReturn_FeeEstimateStruct(
      function<void(ClientClasses::FeeEstimateStruct)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_VectorLedgerEntry : public CallbackReturn_WebSocket
{
private:
   function<void(vector<::ClientClasses::LedgerEntry>)> userCallbackLambda_;

public:
   CallbackReturn_VectorLedgerEntry(
      function<void(vector<::ClientClasses::LedgerEntry>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_UINT64 : public CallbackReturn_WebSocket
{
private:
   function<void(uint64_t)> userCallbackLambda_;

public:
   CallbackReturn_UINT64(
      function<void(uint64_t)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_VectorUTXO : public CallbackReturn_WebSocket
{
private:
   function<void(vector<UTXO>)> userCallbackLambda_;

public:
   CallbackReturn_VectorUTXO(
      function<void(vector<UTXO>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_VectorUINT64 : public CallbackReturn_WebSocket
{
private:
   function<void(vector<uint64_t>)> userCallbackLambda_;

public:
   CallbackReturn_VectorUINT64(
      function<void(vector<uint64_t>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_Map_BD_U32 : public CallbackReturn_WebSocket
{
private:
   function<void(map<BinaryData, uint32_t>)> userCallbackLambda_;

public:
   CallbackReturn_Map_BD_U32(
      function<void(map<BinaryData, uint32_t>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_Map_BD_VecU64 : public CallbackReturn_WebSocket
{
private:
   function<void(map<BinaryData, vector<uint64_t>>)> userCallbackLambda_;

public:
   CallbackReturn_Map_BD_VecU64(
      function<void(map<BinaryData, vector<uint64_t>>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_LedgerEntry : public CallbackReturn_WebSocket
{
private:
   function<void(shared_ptr<::ClientClasses::LedgerEntry>)> userCallbackLambda_;

public:
   CallbackReturn_LedgerEntry(
      function<void(shared_ptr<::ClientClasses::LedgerEntry>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_VectorAddressBookEntry : public CallbackReturn_WebSocket
{
private:
   function<void(vector<AddressBookEntry>)> userCallbackLambda_;

public:
   CallbackReturn_VectorAddressBookEntry(
      function<void(vector<AddressBookEntry>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_Bool : public CallbackReturn_WebSocket
{
private:
   function<void(bool)> userCallbackLambda_;

public:
   CallbackReturn_Bool(function<void(bool)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_BlockHeader : public CallbackReturn_WebSocket
{
private:
   function<void(ClientClasses::BlockHeader)> userCallbackLambda_;
   const unsigned height_;

public:
   CallbackReturn_BlockHeader(unsigned height, 
      function<void(ClientClasses::BlockHeader)> lbd) :
      height_(height), userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

///////////////////////////////////////////////////////////////////////////////
struct CallbackReturn_BDVCallback : public CallbackReturn_WebSocket
{
private:
   function<void(shared_ptr<::Codec_BDVCommand::BDVCallback>)>
      userCallbackLambda_;

public:
   CallbackReturn_BDVCallback(
      function<void(shared_ptr<::Codec_BDVCommand::BDVCallback>)> lbd) :
      userCallbackLambda_(lbd)
   {}

   //virtual
   void callback(const WebSocketMessagePartial&);
};

#endif
