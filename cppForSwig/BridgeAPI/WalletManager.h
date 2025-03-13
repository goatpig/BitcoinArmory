////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-20, goatpig                                            //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _WALLET_MANAGER_H
#define _WALLET_MANAGER_H

#include <mutex>
#include <memory>
#include <string>
#include <map>
#include <iostream>
#include <filesystem>

#include "Wallets.h"
#include "Signer.h"
#include "ArmoryConfig.h"
#include "CoinSelection.h"
#include "Script.h"
#include "AsyncClient.h"
#include "ReentrantLock.h"


namespace Armory
{
   namespace Seeds
   {
      class WalletBackup;
   };

   namespace Accounts
   {
      class AddressAccount;
   }

   namespace Wallets
   {
      class AddressAccountId;
      class AssetWallet;
      class EncryptionKeyId;
   };
};

////////////////////////////////////////////////////////////////////////////////
class WalletContainer
{
   friend class WalletManager;

private:
   const std::string wltId_;
   const Armory::Wallets::AddressAccountId accountId_;
   std::string dbId_;
   std::shared_ptr<Armory::Wallets::AssetWallet> wallet_;

   std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr_;
   std::shared_ptr<AsyncClient::BtcWallet> asyncWlt_;

   std::map<BinaryData, std::vector<uint64_t>> balanceMap_;
   std::map<BinaryData, uint64_t> countMap_;

   uint64_t totalBalance_ = 0;
   uint64_t spendableBalance_ = 0;
   uint64_t unconfirmedBalance_ = 0;
   uint64_t txioCount_ = 0;

   std::map<Armory::Wallets::AssetAccountId,
      Armory::Wallets::AssetKeyType> highestUsedIndex_;
   std::mutex stateMutex_;

   std::map<BinaryData, std::shared_ptr<AddressEntry>> updatedAddressMap_;

private:
   WalletContainer(const std::string&, const Armory::Wallets::AddressAccountId&);
   void resetCache(void);
   void setBdvPtr(std::shared_ptr<AsyncClient::BlockDataViewer>);
   void setWalletPtr(std::shared_ptr<Armory::Wallets::AssetWallet>,
      const Armory::Wallets::AddressAccountId&);
   void eraseFromDisk(void);

public:
   void registerWithBDV(bool isNew);
   void unregisterFromBDV(void);
   const std::string& getDbId(void) const;

   virtual std::shared_ptr<Armory::Wallets::AssetWallet>
      getWalletPtr(void) const;
   std::shared_ptr<Armory::Accounts::AddressAccount>
      getAddressAccount(void) const;
   const Armory::Wallets::AddressAccountId& getAccountId(void) const;

   void updateBalancesAndCount(uint32_t topBlockHeight);
   void updateWalletBalanceState(const AsyncClient::CombinedBalances&);
   void updateAddressCountState(const AsyncClient::CombinedBalances&);

   void extendAddressChain(unsigned count)
   {
      wallet_->extendPublicChain(count);
   }

   void extendAddressChainToIndex(
      const Armory::Wallets::AddressAccountId& id,
      unsigned count);
   bool hasAddress(const BinaryData& addr);
   bool hasAddress(const std::string& addr);

   void createAddressBook(
      const std::function<void(ReturnMessage<std::vector<AddressBookEntry>>)>&);

   void getUTXOs(uint64_t, bool, bool,
      const std::function<void(ReturnMessage<std::vector<UTXO>>)>& lbd);

   uint64_t getFullBalance(void) const { return totalBalance_; }
   uint64_t getSpendableBalance(void) const { return spendableBalance_; }
   uint64_t getUnconfirmedBalance(void) const { return unconfirmedBalance_; }
   uint64_t getTxIOCount(void) const { return txioCount_; }

   std::map<BinaryData, std::vector<uint64_t>> getAddrBalanceMap(void) const;
   Armory::Wallets::AssetKeyType getHighestUsedIndex(void) const;
   std::map<BinaryData, std::shared_ptr<AddressEntry>> getUpdatedAddressMap();

   std::unique_ptr<Armory::Seeds::WalletBackup> getBackupStrings(
      const PassphraseLambda&) const;

   void setComment(const std::string&, const std::string&);
   void setLabels(const std::string&, const std::string&);

   const Armory::Wallets::EncryptionKeyId& getDefaultEncryptionKeyId() const;
};

////////////////////////////////////////////////////////////////////////////////
enum Armory135WalletEntriesEnum
{
   WLT_DATATYPE_KEYDATA = 0,
   WLT_DATATYPE_ADDRCOMMENT,
   WLT_DATATYPE_TXCOMMENT,
   WLT_DATATYPE_OPEVAL,
   WLT_DATATYPE_DELETED
};

////////////////////////////////////////////////////////////////////////////////
class Armory135Address
{
private:
   //public data
   BinaryData scrAddr_;
   SecureBinaryData pubKey_;
   SecureBinaryData chaincode_;

   //private data
   SecureBinaryData privKey_;
   SecureBinaryData decryptedPrivKey_;

   //encryption data
   SecureBinaryData iv_;

   //indexes
   int64_t chainIndex_;
   int64_t depth_;

   //flags
   bool hasPrivKey_ = false;
   bool hasPubKey_ = false;
   bool isEncrypted_ = false;

public:
   Armory135Address(void) {}

   void parseFromRef(const BinaryDataRef&);
   bool isEncrypted(void) const { return isEncrypted_; }
   bool hasPrivKey(void) const { return hasPrivKey_; }

   const SecureBinaryData& privKey(void) const { return privKey_; }
   const SecureBinaryData& pubKey(void) const { return pubKey_; }
   const SecureBinaryData& chaincode(void) const { return chaincode_; }
   const SecureBinaryData& iv(void) const { return iv_; }

   const BinaryData& scrAddr(void) const { return scrAddr_; }
   int64_t chainIndex(void) const { return chainIndex_; }
};

////////////////////////////////////////////////////////////////////////////////
class Armory135Header
{
private:
   //file system
   const std::filesystem::path path_;

   //meta data
   std::string walletID_;
   uint32_t version_ = UINT32_MAX;
   uint64_t timestamp_ = UINT32_MAX;

   std::string labelName_;
   std::string labelDescription_;
   
   int64_t highestUsedIndex_ = -1;

   //flags
   bool isEncrypted_ = false;
   bool watchingOnly_ = false;

   //encryption data
   uint64_t kdfMem_ = UINT64_MAX;
   uint32_t kdfIter_;
   SecureBinaryData kdfSalt_;

   //comments
   std::map<BinaryData, std::string> commentMap_;

   //address map
   std::map<BinaryData, Armory135Address> addrMap_;

private:
   void parseFile();


public:
   Armory135Header(const std::filesystem::path path) :
      path_(path)
   {
      parseFile();
   }

   bool isInitialized(void) { return version_ != UINT32_MAX; }
   const std::string& getID(void) const { return walletID_; }
   std::shared_ptr<Armory::Wallets::AssetWallet_Single> migrate(
      const PassphraseLambda&) const;

   //static
   static void verifyChecksum(
      const BinaryDataRef& val, const BinaryDataRef& chkSum);
};

////////////////////////////////////////////////////////////////////////////////
class WalletManager : public Lockable
{
private:
   const std::filesystem::path path_;
   std::map<std::string, std::map<
      Armory::Wallets::AddressAccountId,
         std::shared_ptr<WalletContainer>>> wallets_;
   std::map<std::string, std::shared_ptr<WalletContainer>> walletsByDbId_;

   PassphraseLambda passphraseLbd_;
   std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr_;

private:
   void loadWallets(const PassphraseLambda&);

public:
   void initAfterLock(void) override {}
   void cleanUpBeforeUnlock(void) override {}

public:
   WalletManager(const std::filesystem::path&, const PassphraseLambda&);

   bool hasWallet(const std::string&);
   void loadWallet(const std::filesystem::path&, const PassphraseLambda&);

   std::map<std::string, std::set<Armory::Wallets::AddressAccountId>>
      getAccountIdMap(void) const;
   std::shared_ptr<WalletContainer> getWalletContainer(
      const std::string&) const;
   std::shared_ptr<WalletContainer> getWalletContainer(
      const std::string&, const Armory::Wallets::AddressAccountId&) const;

   void setBdvPtr(std::shared_ptr<AsyncClient::BlockDataViewer>);
   void registerWallets(void);
   const std::string& registerWallet(const std::string&,
      const Armory::Wallets::AddressAccountId&, bool);

   std::shared_ptr<WalletContainer> addWallet(
      std::shared_ptr<Armory::Wallets::AssetWallet>,
      const Armory::Wallets::AddressAccountId&);

   void updateStateFromDB(const std::function<void(void)>&);
   std::shared_ptr<WalletContainer> createNewWallet(
      const SecureBinaryData&, const SecureBinaryData&, //pass, control pass
      const SecureBinaryData&, unsigned); //extra entropy, address lookup

   std::filesystem::path unloadWallet(const std::string&);
   void deleteWallet(const std::string&);

   const std::filesystem::path& getWalletDir(void) const { return path_; }
};

#endif
