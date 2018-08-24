////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _WALLET_MANAGER_H
#define _WALLET_MANAGER_H

using namespace std;

#include <mutex>
#include <memory>
#include <string>
#include <map>
#include <iostream>

#include "log.h"
#include "Wallets.h"
#include "SwigClient.h"
#include "Signer.h"
#include "BlockDataManagerConfig.h"
#include "CoinSelection.h"
#include "Script.h"

enum AddressType
{
   AddressType_P2PKH,
   AddressType_P2SH_P2PK,
   AddressType_P2SH_P2WPKH,
   AddressType_Multisig
};

class WalletContainer;

////////////////////////////////////////////////////////////////////////////////
struct CoinSelectionInstance
{
private:
   CoinSelection cs_;

   map<unsigned, shared_ptr<ScriptRecipient> > recipients_;
   UtxoSelection selection_;
   WalletContainer* const walletContainer_;

   vector<UTXO> state_utxoVec_;
   uint64_t spendableBalance_;

private:
   static void decorateUTXOs(WalletContainer* const, vector<UTXO>&);
   static function<vector<UTXO>(uint64_t)> getFetchLambdaFromWalletContainer(
      WalletContainer* const walletContainer);

   static function<vector<UTXO>(uint64_t)> getFetchLambdaFromLockbox(
      SwigClient::Lockbox* const, unsigned M, unsigned N);

   uint64_t getSpendVal(void) const;
   void checkSpendVal(uint64_t) const;
   void addRecipient(unsigned, const BinaryData&, uint64_t);

   static shared_ptr<ScriptRecipient> createRecipient(const BinaryData&, uint64_t);
   
   void selectUTXOs(vector<UTXO>&, uint64_t fee, float fee_byte, unsigned flags);
public:
   CoinSelectionInstance(WalletContainer* const walletContainer,
      const vector<AddressBookEntry>& addrBook);
   CoinSelectionInstance(SwigClient::Lockbox* const, 
      unsigned M, unsigned N, uint64_t balance);

   unsigned addRecipient(const BinaryData&, uint64_t);
   void updateRecipient(unsigned, const BinaryData&, uint64_t);
   void updateOpReturnRecipient(unsigned, const BinaryData&);
   void removeRecipient(unsigned);
   void resetRecipients(void);

   void selectUTXOs(uint64_t fee, float fee_byte, unsigned flags);
   void processCustomUtxoList(
      const vector<BinaryData>& serializedUtxos, 
      uint64_t fee, float fee_byte,
      unsigned flags);

   void updateState(uint64_t fee, float fee_byte, unsigned flags);

   uint64_t getFeeForMaxValUtxoVector(const vector<BinaryData>& serializedUtxos, float fee_byte);
   uint64_t getFeeForMaxVal(float fee_byte);

   size_t getSizeEstimate(void) const { return selection_.size_; }
   vector<UTXO> getUtxoSelection(void) const { return selection_.utxoVec_; }
   uint64_t getFlatFee(void) const { return selection_.fee_; }
   float getFeeByte(void) const { return selection_.fee_byte_; }

   bool isSW(void) const { return selection_.witnessSize_ != 0; }

   void rethrow(void) { cs_.rethrow(); }
};

////////////////////////////////////////////////////////////////////////////////
class WalletContainer
{
   friend class WalletManager;
   friend class PythonSigner;
   friend struct CoinSelectionInstance;

private:
   const string id_;
   shared_ptr<AssetWallet> wallet_;
   shared_ptr<SwigClient::BtcWallet> swigWallet_;
   function<SwigClient::BlockDataViewer&(void)> getBDVlambda_;

   map<BinaryData, vector<uint64_t> > balanceMap_;
   map<BinaryData, uint32_t> countMap_;

   uint64_t totalBalance_ = 0;
   uint64_t spendableBalance_ = 0;
   uint64_t unconfirmedBalance_ = 0;

private:
   WalletContainer(const string& id,
      function<SwigClient::BlockDataViewer&(void)> bdvLbd) :
      id_(id), getBDVlambda_(bdvLbd)
   {}

   void reset(void)
   {
      totalBalance_ = 0;
      spendableBalance_ = 0;
      unconfirmedBalance_ = 0;
      balanceMap_.clear();
      countMap_.clear();
   }

protected:
   //need this for unit test, but can't have it exposed to SWIG for backwards
   //compatiblity with 2.x (because of the shared_ptr return type)
   virtual shared_ptr<AssetWallet> getWalletPtr(void) const
   {
      return wallet_;
   }

public:
   void registerWithBDV(bool isNew);

   vector<uint64_t> getBalancesAndCount(
      uint32_t topBlockHeight)
   {
      auto&& balVec =
         swigWallet_->getBalancesAndCount(topBlockHeight);

      totalBalance_ = balVec[0];
      spendableBalance_ = balVec[1];
      unconfirmedBalance_ = balVec[2];

      return balVec;
   }

   vector<UTXO> getSpendableTxOutListForValue(
      uint64_t val = UINT64_MAX)
   {
      return swigWallet_->getSpendableTxOutListForValue(val);
   }

   vector<UTXO> getSpendableZCList(void)
   {
      return swigWallet_->getSpendableZCList();
   }

   vector<UTXO> getRBFTxOutList(void)
   {
      return swigWallet_->getRBFTxOutList();
   }

   
   const map<BinaryData, uint32_t>& getAddrTxnCountsFromDB(void)
   {
      auto&& countmap = swigWallet_->getAddrTxnCountsFromDB();

      for (auto count : countmap)
      {
         if (count.first.getSize() == 0)
            continue;

         //save count
         countMap_[count.first] = count.second;

         //fetch the asset in wallet
         auto& assetID = wallet_->getAssetIDForAddr(count.first);
         auto addrType = wallet_->getAddrTypeForID(assetID);

         wallet_->getAddressEntryForID(assetID, addrType);
      }

      return countMap_;
   }
   
   const map<BinaryData, vector<uint64_t> >& getAddrBalancesFromDB(void)
   {

      auto&& balancemap = swigWallet_->getAddrBalancesFromDB();

      for (auto& balVec : balancemap)
      {
         if (balVec.first.getSize() == 0)
            continue;

         //save balance
         balanceMap_[balVec.first] = balVec.second;
      }

      return balanceMap_;
   }

   vector<::ClientClasses::LedgerEntry> getHistoryPage(uint32_t id)
   {
      return swigWallet_->getHistoryPage(id);
   }

   shared_ptr<::ClientClasses::LedgerEntry> getLedgerEntryForTxHash(
      const BinaryData& txhash)
   {
      return swigWallet_->getLedgerEntryForTxHash(txhash);
   }

   SwigClient::ScrAddrObj getScrAddrObjByKey(const BinaryData& scrAddr,
      uint64_t full, uint64_t spendable, uint64_t unconf, uint32_t count)
   {
      return swigWallet_->getScrAddrObjByKey(
         scrAddr, full, spendable, unconf, count);
   }

   vector<AddressBookEntry> createAddressBook(void) const
   {
      if (swigWallet_ == nullptr)
         return vector<AddressBookEntry>();

      return swigWallet_->createAddressBook();
   }

   BinaryData getNestedSWAddrForID(const BinaryData& ID)
   {
      auto addrPtr = wallet_->getAddressEntryForID(ID,
         AddressEntryType(AddressEntryType_P2WPKH | AddressEntryType_P2SH));
      return addrPtr->getAddress();
   }

   BinaryData getNestedP2PKAddrForIndex(const BinaryData& ID)
   {
      auto addrPtr = wallet_->getAddressEntryForID(ID,
         AddressEntryType(
            AddressEntryType_P2PK | 
            AddressEntryType_Compressed | 
            AddressEntryType_P2SH
            ));
      return addrPtr->getAddress();
   }

   BinaryData getP2PKHAddrForIndex(const BinaryData& ID)
   {
      auto addrPtr = wallet_->getAddressEntryForID(ID,
         AddressEntryType(AddressEntryType_P2PKH));
      return addrPtr->getAddress();
   }

   void extendAddressChain(unsigned count)
   {
      wallet_->extendPublicChain(count);
   }

   void extendAddressChainToIndex(const BinaryData& id, unsigned count)
   {
      wallet_->extendPublicChainToIndex(id, count);
   }

   bool hasScrAddr(const BinaryData& scrAddr)
   {
      return wallet_->hasScrAddr(scrAddr);
   }

   const BinaryData& getAssetIDForAddr(const BinaryData& scrAddr)
   {
      return wallet_->getAssetIDForAddr(scrAddr);
   }

   const BinaryData& getScriptHashPreimage(const BinaryData& hash)
   {
      auto& assetID = wallet_->getAssetIDForAddr(hash);
      auto addrPtr = wallet_->getAddressEntryForID(assetID);
      return addrPtr->getPreimage();
   }

   AddressEntryType getAddrTypeForID(const BinaryData& ID)
   {
      return wallet_->getAddrTypeForID(ID);
   }

   SwigClient::ScrAddrObj getAddrObjByID(const BinaryData& ID)
   {
      auto addrPtr = wallet_->getAddressEntryForID(ID);

      uint64_t full = 0, spend = 0, unconf = 0;
      auto balanceIter = balanceMap_.find(addrPtr->getPrefixedHash());
      if (balanceIter != balanceMap_.end())
      {
         full = balanceIter->second[0];
         spend = balanceIter->second[1];
         unconf = balanceIter->second[2];
      }

      uint32_t count = 0;
      auto countIter = countMap_.find(addrPtr->getPrefixedHash());
      if (countIter != countMap_.end())
         count = countIter->second;

      BinaryRefReader brr(ID.getRef());
      brr.advance(ID.getSize() - 4);
      auto index = brr.get_uint32_t();

      if (swigWallet_ != nullptr)
      {
         AsyncClient::ScrAddrObj saObj(
            &swigWallet_->asyncWallet_, addrPtr->getAddress(), index,
            full, spend, unconf, count);
         saObj.addrHash_ = addrPtr->getPrefixedHash();

         return SwigClient::ScrAddrObj(saObj);
      }
      else
      {
         AsyncClient::ScrAddrObj saObj(
            addrPtr->getAddress(), addrPtr->getPrefixedHash(), index);

         return SwigClient::ScrAddrObj(saObj);
      }
   }

   int detectHighestUsedIndex(void);

   CoinSelectionInstance getCoinSelectionInstance(void)
   {
      auto&& addrBookVector = createAddressBook();
      return CoinSelectionInstance(this, addrBookVector);
   }
};

class ResolverFeed_PythonWalletSingle;

////////////////////////////////////////////////////////////////////////////////
class PythonSigner
{
   friend class ResolverFeed_PythonWalletSingle;

private:
   shared_ptr<AssetWallet> walletPtr_;

protected:
   unique_ptr<Signer> signer_;
   shared_ptr<ResolverFeed_PythonWalletSingle> feedPtr_;

public:
   PythonSigner(WalletContainer& wltContainer)
   {
      walletPtr_ = wltContainer.wallet_;
      signer_ = make_unique<Signer>();
      signer_->setFlags(SCRIPT_VERIFY_SEGWIT);

      //create feed
      auto walletSingle = dynamic_pointer_cast<AssetWallet_Single>(walletPtr_);
      if (walletSingle == nullptr)
         throw WalletException("unexpected wallet type");

      feedPtr_ = make_shared<ResolverFeed_PythonWalletSingle>(
         walletSingle, this);
   }

   virtual void addSpender(
      uint64_t value, 
      uint32_t height, uint16_t txindex, uint16_t outputIndex, 
      const BinaryData& txHash, const BinaryData& script, unsigned sequence)
   {
      UTXO utxo(value, height, txindex, outputIndex, txHash, script);

      //set spenders
      auto spenderPtr = make_shared<ScriptSpender>(utxo, feedPtr_);
      spenderPtr->setSequence(sequence);

      signer_->addSpender(spenderPtr);
   }

   void addRecipient(const BinaryData& script, uint64_t value)
   {
      auto txOutRef = BtcUtils::getTxOutScrAddrNoCopy(script);

      auto p2pkh_prefix =
        SCRIPT_PREFIX(BlockDataManagerConfig::getPubkeyHashPrefix());
      auto p2sh_prefix =
         SCRIPT_PREFIX(BlockDataManagerConfig::getScriptHashPrefix());

      shared_ptr<ScriptRecipient> recipient;
      if (txOutRef.type_ == p2pkh_prefix)
         recipient = make_shared<Recipient_P2PKH>(txOutRef.scriptRef_, value);
      else if (txOutRef.type_ == p2sh_prefix)
         recipient = make_shared<Recipient_P2SH>(txOutRef.scriptRef_, value);
      else if (txOutRef.type_ == SCRIPT_PREFIX_OPRETURN)
         recipient = make_shared<Recipient_OPRETURN>(txOutRef.scriptRef_);
      else
         throw WalletException("unexpected output type");

      signer_->addRecipient(recipient);
   }

   void signTx(void)
   {
      signer_->sign();
      if (!signer_->verify())
         throw runtime_error("failed signature");
   }

   void setLockTime(unsigned locktime)
   {
      signer_->setLockTime(locktime);
   }

   BinaryData getSignedTx(void)
   {
      BinaryData finalTx(signer_->serialize());
      return finalTx;
   }

   const BinaryData& getSigForInputIndex(unsigned id) const
   {
      return signer_->getSigForInputIndex(id);
   }

   BinaryData getWitnessDataForInputIndex(unsigned id)
   {
      return BinaryData(signer_->getWitnessData(id));
   }

   bool isInptuSW(unsigned id) const
   {
      return signer_->isInputSW(id);
   }

   BinaryData serializeSignedTx() const
   {
      return signer_->serialize();
   }

   BinaryData serializeState(void) const
   {
      return signer_->serializeState();
   }

   virtual ~PythonSigner(void) = 0;
   virtual const SecureBinaryData& getPrivateKeyForIndex(unsigned) = 0;
   virtual const SecureBinaryData& getPrivateKeyForImportIndex(unsigned) = 0;
};

////////////////////////////////////////////////////////////////////////////////
class PythonSigner_BCH : public PythonSigner
{
public:
   PythonSigner_BCH(WalletContainer& wltContainer) :
      PythonSigner(wltContainer)
   {
      signer_ = make_unique<Signer_BCH>();
   }

   void addSpender(
      uint64_t value,
      uint32_t height, uint16_t txindex, uint16_t outputIndex,
      const BinaryData& txHash, const BinaryData& script, unsigned sequence)
   {
      UTXO utxo(value, height, txindex, outputIndex, txHash, script);

      //set spenders
      auto spenderPtr = make_shared<ScriptSpender_BCH>(utxo, feedPtr_);
      spenderPtr->setSequence(sequence);

      signer_->addSpender(spenderPtr);
   }
};

class ResolverFeed_Universal;

////////////////////////////////////////////////////////////////////////////////
class UniversalSigner
{
private:
   unique_ptr<Signer> signer_;
   shared_ptr<ResolverFeed_Universal> feedPtr_;

public:
   UniversalSigner(const string& signerType);

   virtual ~UniversalSigner(void) = 0;

   void updateSignerState(const BinaryData& state)
   {
      signer_->deserializeState(state);
   }

   void populateUtxo(const BinaryData& hash, unsigned txoId, 
                uint64_t value, const BinaryData& script)
   {
      UTXO utxo(value, UINT32_MAX, UINT32_MAX, txoId, hash, script);
      signer_->populateUtxo(utxo);
   }

   void signTx(void)
   {
      signer_->sign();
   }

   void setLockTime(unsigned locktime)
   {
      signer_->setLockTime(locktime);
   }

   void setVersion(unsigned version)
   {
      signer_->setVersion(version);
   }

   void addSpenderByOutpoint(
      const BinaryData& hash, unsigned index, unsigned sequence, uint64_t value)
   {
      signer_->addSpender_ByOutpoint(hash, index, sequence, value);
   }

   void addRecipient(uint64_t value, const BinaryData& script)
   {
      auto recipient = make_shared<Recipient_Universal>(script, value);
      signer_->addRecipient(recipient);
   }

   BinaryData getSignedTx(void)
   {
      BinaryData finalTx(signer_->serialize());
      return finalTx;
   }

   const BinaryData& getSigForInputIndex(unsigned id) const
   {
      return signer_->getSigForInputIndex(id);
   }

   BinaryData getWitnessDataForInputIndex(unsigned id)
   {
      return BinaryData(signer_->getWitnessData(id));
   }

   bool isInptuSW(unsigned id) const
   {
      return signer_->isInputSW(id);
   }

   BinaryData serializeState(void) const
   {
      return signer_->serializeState();
   }

   void deserializeState(const BinaryData& state)
   {
      signer_->deserializeState(state);
   }

   TxEvalState getSignedState(void) const
   {
      return signer_->evaluateSignedState();
   }

   BinaryData serializeSignedTx() const
   {
      return signer_->serialize();
   }

   virtual string getPublicDataForKey(const string&) = 0;
   virtual const SecureBinaryData& getPrivDataForKey(const string&) = 0;
};

////////////////////////////////////////////////////////////////////////////////
class PythonVerifier
{
private:
   unique_ptr<Signer> signer_;

public:
   PythonVerifier()
   {
      signer_ = make_unique<Signer>();
      signer_->setFlags(SCRIPT_VERIFY_SEGWIT);
   }

   bool verifySignedTx(const BinaryData& rawTx,
     const map<BinaryData, map<unsigned, BinaryData> >& utxoMap)
   {
      return signer_->verifyRawTx(rawTx, utxoMap);
   }
};

////////////////////////////////////////////////////////////////////////////////
class PythonVerifier_BCH
{
private:
   unique_ptr<Signer_BCH> signer_;

public:
   PythonVerifier_BCH()
   {
      signer_ = make_unique<Signer_BCH>();
   }

   bool verifySignedTx(const BinaryData& rawTx,
      const map<BinaryData, map<unsigned, BinaryData> >& utxoMap)
   {
      return signer_->verifyRawTx(rawTx, utxoMap);
   }
};

////////////////////////////////////////////////////////////////////////////////
class ResolverFeed_PythonWalletSingle : public ResolverFeed_AssetWalletSingle
{
private:
   PythonSigner* signerPtr_ = nullptr;

public:
   ResolverFeed_PythonWalletSingle(
      shared_ptr<AssetWallet_Single> walletPtr,
      PythonSigner* signerptr) :
      ResolverFeed_AssetWalletSingle(walletPtr),
      signerPtr_(signerptr)
   {
      if (signerPtr_ == nullptr)
         throw WalletException("null signer ptr");
   }

   const SecureBinaryData& getPrivKeyForPubkey(const BinaryData& pubkey)
   {
      auto pubkeyref = BinaryDataRef(pubkey);
      auto iter = pubkey_to_asset_.find(pubkeyref);
      if (iter == pubkey_to_asset_.end())
         throw runtime_error("invalid value");

      auto id = iter->second->getIndex();
      return signerPtr_->getPrivateKeyForIndex(id);
   }
};

////////////////////////////////////////////////////////////////////////////////
class ResolverFeed_Universal : public ResolverFeed
{
private:
   UniversalSigner* signerPtr_ = nullptr;

public:
   ResolverFeed_Universal(UniversalSigner* signerptr) :
      signerPtr_(signerptr)
   {
      if (signerPtr_ == nullptr)
         throw WalletException("null signer ptr");
   }

   const SecureBinaryData& getPrivKeyForPubkey(const BinaryData& pubkey)
   {
      auto&& pubkey_hex = pubkey.toHexStr();
      auto& data = signerPtr_->getPrivDataForKey(pubkey_hex);
      if (data.getSize() == 0)
         throw runtime_error("invalid value");
      return data;
   }

   BinaryData getByVal(const BinaryData& val)
   {
      auto&& val_str = val.toHexStr();
      auto data_str = signerPtr_->getPublicDataForKey(val_str);
      if (data_str.size() == 0)
         throw runtime_error("invalid value");
      BinaryData data_bd(data_str);
      return data_bd;
   }
};

////////////////////////////////////////////////////////////////////////////////
class WalletManager
{
private:
   mutable mutex mu_;

   const string path_;
   map<string, WalletContainer> wallets_;
   SwigClient::BlockDataViewer bdv_;

private:
   void loadWallets();
   SwigClient::BlockDataViewer& getBDVObj(void);

public:
   WalletManager(const string& path) :
      path_(path)
   {
      loadWallets();
   }

   bool hasWallet(const string& id)
   {
      unique_lock<mutex> lock(mu_);
      auto wltIter = wallets_.find(id);
      
      return wltIter != wallets_.end();
   }

   void setBDVObject(const SwigClient::BlockDataViewer& bdv)
   {
      bdv_ = bdv;
   }

   void synchronizeWallet(const string& id, unsigned chainLength);

   void duplicateWOWallet(
      const SecureBinaryData& pubRoot,
      const SecureBinaryData& chainCode,
      unsigned chainLength);

   WalletContainer& getCppWallet(const string& id);
};

#endif
