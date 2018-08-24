////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig.                                              //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //                                      
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/***
Set of spoof classes that expose all BDV, wallet and address obj methods to SWIG
and handle the data transmission with the BDM server
***/

#ifndef _SWIGCLIENT_H
#define _SWIGCLIENT_H

#include "StringSockets.h"
#include "bdmenums.h"
#include "log.h"
#include "TxClasses.h"
#include "BlockDataManagerConfig.h"
#include "WebSocketClient.h"
#include "AsyncClient.h"

class WalletManager;
class WalletContainer;

namespace SwigClient
{

   inline void StartCppLogging(string fname, int lvl) { STARTLOGGING(fname, (LogLevel)lvl); }
   inline void ChangeCppLogLevel(int lvl) { SETLOGLEVEL((LogLevel)lvl); }
   inline void DisableCppLogging() { SETLOGLEVEL(LogLvlDisabled); }
   inline void EnableCppLogStdOut() { LOGENABLESTDOUT(); }
   inline void DisableCppLogStdOut() { LOGDISABLESTDOUT(); }

#include <thread>

   class BlockDataViewer;

   ///////////////////////////////////////////////////////////////////////////////
   class LedgerDelegate
   {
   private:
      AsyncClient::LedgerDelegate asyncLed_;

   public:
      LedgerDelegate(void)
      {}

      LedgerDelegate(AsyncClient::LedgerDelegate&);
      vector<::ClientClasses::LedgerEntry> getHistoryPage(uint32_t id);
      uint64_t getPageCount(void) const;
   };

   class BtcWallet;

   ///////////////////////////////////////////////////////////////////////////////
   class ScrAddrObj
   {
   private:
      AsyncClient::ScrAddrObj asyncAddr_;

   public:
      ScrAddrObj(AsyncClient::ScrAddrObj&);

      uint64_t getFullBalance(void) const;
      uint64_t getSpendableBalance(void) const;
      uint64_t getUnconfirmedBalance(void) const;

      uint64_t getTxioCount(void) const;

      vector<UTXO> getSpendableTxOutList(bool);
      const BinaryData& getScrAddr(void) const;
      const BinaryData& getAddrHash(void) const;

      void setComment(const string& comment);
      const string& getComment(void) const;
      int getIndex(void) const;
   };

   ///////////////////////////////////////////////////////////////////////////////
   class BtcWallet
   {
      friend class ScrAddrObj;
      friend class ::WalletContainer;

   protected:
      AsyncClient::BtcWallet asyncWallet_;

   public:
      BtcWallet(AsyncClient::BtcWallet& wlt);
      vector<uint64_t> getBalancesAndCount(
         uint32_t topBlockHeight);

      vector<UTXO> getSpendableTxOutListForValue(uint64_t val);
      vector<UTXO> getSpendableZCList();
      vector<UTXO> getRBFTxOutList();

      map<BinaryData, uint32_t> getAddrTxnCountsFromDB(void);
      map<BinaryData, vector<uint64_t> > getAddrBalancesFromDB(void);

      vector<::ClientClasses::LedgerEntry> getHistoryPage(uint32_t id);
      shared_ptr<::ClientClasses::LedgerEntry> getLedgerEntryForTxHash(
         const BinaryData& txhash);

      ScrAddrObj getScrAddrObjByKey(const BinaryData&,
         uint64_t, uint64_t, uint64_t, uint32_t);

      vector<AddressBookEntry> createAddressBook(void) const;

      virtual string registerAddresses(
         const vector<BinaryData>& addrVec, bool isNew);
   };

   ///////////////////////////////////////////////////////////////////////////////
   class Lockbox : public BtcWallet
   {
   private:
      AsyncClient::Lockbox asyncLockbox_;

   public:
      Lockbox(AsyncClient::Lockbox& asynclb);

      void getBalancesAndCountFromDB(uint32_t topBlockHeight);

      uint64_t getFullBalance(void) const;
      uint64_t getSpendableBalance(void) const;
      uint64_t getUnconfirmedBalance(void) const;
      uint64_t getWltTotalTxnCount(void) const;

      string registerAddresses(
         const vector<BinaryData>& addrVec, bool isNew);
   };

   ///////////////////////////////////////////////////////////////////////////////
   class Blockchain
   {
   private:
      AsyncClient::Blockchain asyncBlockchain_;

   public:
      Blockchain(const BlockDataViewer&);
      ::ClientClasses::BlockHeader getHeaderByHash(const BinaryData& hash);
      ClientClasses::BlockHeader getHeaderByHeight(unsigned height);
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

   private:
      AsyncClient::BlockDataViewer bdvAsync_;

   private:
      BlockDataViewer(void) {}
      BlockDataViewer(AsyncClient::BlockDataViewer&);
      const BlockDataViewer& operator=(const BlockDataViewer& rhs);
      bool isValid(void) const;

   public:
      ~BlockDataViewer(void);
      
      bool connectToRemote(void);
      BtcWallet instantiateWallet(const string& id);
      Lockbox instantiateLockbox(const string& id);

      const string& getID(void) const;

      static shared_ptr<BlockDataViewer> getNewBDV(
         const string& addr, const string& port, shared_ptr<RemoteCallback>);

      LedgerDelegate getLedgerDelegateForWallets(void);
      LedgerDelegate getLedgerDelegateForLockboxes(void);
      LedgerDelegate getLedgerDelegateForScrAddr(
         const string&, const BinaryData&);
      Blockchain blockchain(void);

      void goOnline(void);
      void registerWithDB(BinaryData magic_word);
      void unregisterFromDB(void);
      void shutdown(const string&);
      void shutdownNode(const string&);

      void broadcastZC(const BinaryData& rawTx);
      Tx getTxByHash(const BinaryData& txHash);
      BinaryData getRawHeaderForTxHash(const BinaryData& txHash);

      void updateWalletsLedgerFilter(const vector<BinaryData>& wltIdVec);
      bool hasRemoteDB(void);

      shared_ptr<::ClientClasses::NodeStatusStruct> getNodeStatus(void);
      ClientClasses::FeeEstimateStruct estimateFee(unsigned, const string&);

      vector<::ClientClasses::LedgerEntry> getHistoryForWalletSelection(
         const vector<string>& wldIDs, const string& orderingStr);

      string broadcastThroughRPC(const BinaryData& rawTx);

      vector<UTXO> getUtxosForAddrVec(const vector<BinaryData>&);
   };

   ///////////////////////////////////////////////////////////////////////////////
   class ProcessMutex
   {
   private:
      string addr_;
      string port_;
      thread holdThr_;

   private:

      void hodl();

   public:
      ProcessMutex(const string& addr, const string& port) :
         addr_(addr), port_(port)
      {}

      bool test(const string& uriStr);      
      bool acquire();
      
      virtual ~ProcessMutex(void) = 0;
      virtual void mutexCallback(const string&) = 0;
   };
};


#endif
