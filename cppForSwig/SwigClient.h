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
#include "BIP150_151.h"

class WalletManager;
class WalletContainer;

namespace SwigClient
{

   inline void StartCppLogging(std::string fname, int lvl) { STARTLOGGING(fname, (LogLevel)lvl); }
   inline void ChangeCppLogLevel(int lvl) { SETLOGLEVEL((LogLevel)lvl); }
   inline void DisableCppLogging() { SETLOGLEVEL(LogLvlDisabled); }
   inline void EnableCppLogStdOut() { LOGENABLESTDOUT(); }
   inline void DisableCppLogStdOut() { LOGDISABLESTDOUT(); }
   inline void EnableBIP150(const uint32_t& ipVer)
   {
      startupBIP150CTX(ipVer);
   }
   inline void EnableBIP151() { startupBIP151CTX(); }
   inline void DisableBIP151() { shutdownBIP151CTX(); }

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
      std::vector<::ClientClasses::LedgerEntry> getHistoryPage(uint32_t id);
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

      std::vector<UTXO> getSpendableTxOutList(bool);
      const BinaryData& getScrAddr(void) const;
      const BinaryData& getAddrHash(void) const;

      void setComment(const std::string& comment);
      const std::string& getComment(void) const;
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
      std::vector<uint64_t> getBalancesAndCount(
         uint32_t topBlockHeight);

      std::vector<UTXO> getSpendableTxOutListForValue(uint64_t val);
      std::vector<UTXO> getSpendableZCList();
      std::vector<UTXO> getRBFTxOutList();

      std::map<BinaryData, uint32_t> getAddrTxnCountsFromDB(void);
      std::map<BinaryData, std::vector<uint64_t> > getAddrBalancesFromDB(void);

      std::vector<::ClientClasses::LedgerEntry> getHistoryPage(uint32_t id);
      std::shared_ptr<::ClientClasses::LedgerEntry> getLedgerEntryForTxHash(
         const BinaryData& txhash);

      ScrAddrObj getScrAddrObjByKey(const BinaryData&,
         uint64_t, uint64_t, uint64_t, uint32_t);

      std::vector<AddressBookEntry> createAddressBook(void) const;

      virtual std::string registerAddresses(
         const std::vector<BinaryData>& addrVec, bool isNew);
      std::string setUnconfirmedTarget(unsigned);
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

      std::string registerAddresses(
         const std::vector<BinaryData>& addrVec, bool isNew);
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
      void addPublicKey(const SecureBinaryData&);
      BtcWallet instantiateWallet(const std::string& id);
      Lockbox instantiateLockbox(const std::string& id);

      const std::string& getID(void) const;

      static std::shared_ptr<BlockDataViewer> getNewBDV(
         const std::string& addr, const std::string& port,
         const std::string& datadir, const bool& ephemeralPeers,
         std::shared_ptr<RemoteCallback> callbackPtr);

      LedgerDelegate getLedgerDelegateForWallets(void);
      LedgerDelegate getLedgerDelegateForLockboxes(void);
      LedgerDelegate getLedgerDelegateForScrAddr(
         const std::string&, const BinaryData&);
      Blockchain blockchain(void);

      void goOnline(void);
      void registerWithDB(BinaryData magic_word);
      void unregisterFromDB(void);
      void shutdown(const std::string&);
      void shutdownNode(const std::string&);

      void broadcastZC(const BinaryData& rawTx);
      Tx getTxByHash(const BinaryData& txHash);
      BinaryData getRawHeaderForTxHash(const BinaryData& txHash);

      void updateWalletsLedgerFilter(const std::vector<BinaryData>& wltIdVec);
      bool hasRemoteDB(void);

      std::shared_ptr<::ClientClasses::NodeStatusStruct> getNodeStatus(void);
      ClientClasses::FeeEstimateStruct estimateFee(unsigned, const std::string&);

      std::vector<::ClientClasses::LedgerEntry> getHistoryForWalletSelection(
         const std::vector<std::string>& wldIDs, const std::string& orderingStr);

      std::string broadcastThroughRPC(const BinaryData& rawTx);

      std::vector<UTXO> getUtxosForAddrVec(const std::vector<BinaryData>&);

      std::pair<unsigned, unsigned> getRekeyCount(void) const {
         return bdvAsync_.getRekeyCount();
      }

      void setCheckServerKeyPromptLambda(
         std::function<bool(const BinaryData&, const std::string&)> lbd) {
         bdvAsync_.setCheckServerKeyPromptLambda(lbd);
      }
   };

   ///////////////////////////////////////////////////////////////////////////////
   class ProcessMutex
   {
   private:
      std::string addr_;
      std::string port_;
      std::thread holdThr_;

   private:

      void hodl();

   public:
      ProcessMutex(const std::string& addr, const std::string& port) :
         addr_(addr), port_(port)
      {}

      bool test(const std::string& uriStr);
      bool acquire();
      
      virtual ~ProcessMutex(void) = 0;
      virtual void mutexCallback(const std::string&) = 0;
   };
};


#endif
