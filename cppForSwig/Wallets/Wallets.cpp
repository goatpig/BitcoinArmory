////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Utils/ArmoryConfig.h"
#include "Utils/BtcUtils.h"
#include "Utils/DBUtils.h"
#include "Utils/Cryptography.h"

#include "Wallets.h"
#include "WalletFileInterface.h"
#include "Accounts/AccountTypes.h"
#include "Accounts/AddressAccounts.h"
#include "Accounts/MetaAccounts.h"

#include "Seeds/Seeds.h"
#include "Seeds/Backups.h"
#include "KDF.h"

using namespace Armory::Signing;
using namespace Armory::Assets;
using namespace Armory::Accounts;
using namespace Armory::Wallets;
using namespace Armory::Seeds;

using namespace std::chrono_literals;
using namespace std::string_view_literals;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// AssetWallet
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
AssetWallet::AssetWallet(
   std::shared_ptr<IO::WalletDBInterface> iface,
   std::shared_ptr<IO::WalletHeader> headerPtr,
   const WalletId& masterID) :
   iface_(iface),
   dbName_(headerPtr->getDbName()),
   walletID_(headerPtr->walletID_)
{
   auto ifaceCopy = iface_;
   auto getWriteTx = [ifaceCopy](const std::string& name)->
      std::unique_ptr<IO::DBIfaceTransaction>
   {
      return ifaceCopy->beginWriteTransaction(name);
   };

   decryptedData_ = std::make_shared<Encryption::DecryptedDataContainer>(
      getWriteTx, dbName_,
      headerPtr->getDefaultEncryptionKey(),
      headerPtr->getDefaultEncryptionKeyId(),
      headerPtr->defaultKdfId_, headerPtr->masterEncryptionKeyId_);
   checkMasterID(masterID);
}

////////////////////////////////////////////////////////////////////////////////
AssetWallet::~AssetWallet()
{
   accounts_.clear();
   iface_.reset();
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<IO::WalletDBInterface> AssetWallet::createIface(
   const IO::CreateFileParams& params, const Progress::Func& prog)
{
   /*
   This passphrase lambda is used to prompt the user for the wallet file's
   passphrase. Private keys use a different passphrase, with its own prompt.
   */
   if (prog) {
      auto prg = std::make_unique<Progress::CreateWalletFile>(params.filePath);
      prog(std::move(prg));
   }
   auto iface = std::make_shared<IO::WalletDBInterface>();
   iface->createEnv(params);
   iface->setupEnv(params.getOpenFileParams());
   return iface;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<IO::WalletDBInterface> AssetWallet::getIface() const
{
   return iface_;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AddressAccount> AssetWallet::createAccount(
   std::shared_ptr<AccountType> accountType,
   const Progress::Func& prog)
{
   if (prog) {
      auto prg = std::make_unique<Progress::CreateAccount>(accountType);
      prog(std::move(prg));
   }

   /*
   There is no need to apply a KDF to the master encryption key,
   it's a 256 bits rng pull already
   */
   auto cipher = std::make_unique<Encryption::Cipher_AES>(
      KdfId{Encryption::passthroughKdfId},
      decryptedData_->getMasterEncryptionKeyId());

   //instantiate AddressAccount object from AccountType
   auto ifaceCopy = iface_;
   auto getRootLbd = [this]()->std::shared_ptr<AssetEntry>
   {
      return this->getRoot();
   };

   auto account_ptr = AddressAccount::make_new(
      dbName_, accountType, decryptedData_, std::move(cipher), getRootLbd);

   auto accID = account_ptr->getID();
   if (accounts_.find(accID) != accounts_.end()) {
      throw WalletException("already have an address account with this path");
   }

   //commit to disk
   account_ptr->commit(iface_);
   if (accountType->isMain()) {
      mainAccountId_ = account_ptr->getID();

      BinaryWriter bwKey;
      bwKey.put_uint32_t(MAIN_ACCOUNT_KEY);

      BinaryWriter bwData;
      mainAccountId_.serializeValue(bwData);

      auto tx = iface_->beginWriteTransaction(dbName_);
      tx->insert(bwKey.getData(), bwData.getData());
   }

   std::shared_ptr<AddressAccount> sharedAcc(std::move(account_ptr));
   accounts_.emplace(accID, sharedAcc);
   return sharedAcc;
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::setMainWallet(std::shared_ptr<IO::WalletDBInterface> iface,
   const std::string& walletID)
{
   BinaryWriter bwKey;
   bwKey.put_uint32_t(MAINWALLET_KEY);

   BinaryWriter bwData;
   bwData.put_var_int(walletID.size());
   bwData.put_String(walletID);

   auto tx = iface->beginWriteTransaction(WALLETHEADER_DBNAME);
   tx->insert(bwKey.getData(), bwData.getData());
}

////////////////////////////////////////////////////////////////////////////////
std::string AssetWallet::getMainWalletID(
   std::shared_ptr<IO::WalletDBInterface> iface)
{
   BinaryWriter bwKey;
   bwKey.put_uint32_t(MAINWALLET_KEY);

   try {
      auto tx = iface->beginWriteTransaction(WALLETHEADER_DBNAME);
      auto dataRef = getDataRefForKey(tx.get(), bwKey.getData());

      std::string idStr{dataRef.toCharPtr(), dataRef.getSize()};
      return idStr;
   } catch (const IO::NoEntryInWalletException&) {
      LOGERR << "main wallet ID is not set in file: " << iface->getFilename();
      throw WalletException("main wallet ID is not set!");
   }
}

////////////////////////////////////////////////////////////////////////////////
WalletId AssetWallet::getMasterID(std::shared_ptr<IO::WalletDBInterface> iface)
{
   BinaryWriter bwKey;
   bwKey.put_uint32_t(MASTERID_KEY);

   auto tx = iface->beginReadTransaction(WALLETHEADER_DBNAME);
   auto dataRef = getDataRefForKey(tx.get(), bwKey.getData());
   return {dataRef.toCharPtr(), dataRef.getSize()};
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::checkMasterID(const WalletId& masterID)
{
   try {
      /*
      Grab ID from disk, check it matches arg.
      */
      auto fromDisk = getMasterID(iface_);

      //sanity check
      if (fromDisk.empty()) {
         LOGERR << "empty master ID";
         throw WalletException("empty master ID");
      }

      //only compare disk value with arg if the arg isn't empty
      if (!masterID.empty() && masterID != fromDisk) {
         LOGERR << "masterID mismatch, aborting";
         throw WalletException("masterID mismatch, aborting");
      }

      //set masterID_ from disk value
      masterID_ = fromDisk;
      return;
   } catch (const IO::NoEntryInWalletException&) {
      /*
      This wallet has no masterID entry if we got this far, let's set it.
      */

      //sanity check
      if (masterID.empty()) {
         LOGERR << "cannot set empty master ID";
         throw WalletException("cannot set empty master ID");
      }

      BinaryWriter bwKey;
      bwKey.put_uint32_t(MASTERID_KEY);

      BinaryWriter bwVal;
      bwVal.put_var_int(masterID.size());
      bwVal.put_String(masterID);

      auto tx = iface_->beginWriteTransaction(WALLETHEADER_DBNAME);
      tx->insert(bwKey.getData(), bwVal.getData());

      masterID_ = masterID;
   }
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet_Single::readFromFile()
{
   //sanity check
   if (iface_ == nullptr) {
      throw WalletException("uninitialized wallet object");
   }

   auto uniqueTx = iface_->beginReadTransaction(dbName_);
   std::shared_ptr<IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   auto walletTx = std::dynamic_pointer_cast<IO::WalletIfaceTransaction>(sharedTx);

   {
      //main account
      BinaryWriter bwKey;
      bwKey.put_uint32_t(MAIN_ACCOUNT_KEY);

      try {
         auto account_id = sharedTx->getDataRef(bwKey.getData());
         mainAccountId_ = AddressAccountId::deserializeValue(account_id);
      } catch (const IdException&) {}
   }

   {
      //root asset
      root_ = nullptr;

      try {
         BinaryWriter bwKey;
         bwKey.put_uint32_t(ROOTASSET_KEY);
         auto rootAssetRef = getDataRefForKey(sharedTx.get(), bwKey.getData());

         auto asset_root = AssetEntry::deserDBValue(
            AssetId::getRootAssetId(), rootAssetRef);
         root_ = std::dynamic_pointer_cast<AssetEntry_Single>(asset_root);
      } catch (const IO::NoEntryInWalletException&) {}
   }

   {
      //seed
      seed_ = nullptr;

      try {
         BinaryWriter bwKey;
         bwKey.put_uint32_t(WALLET_SEED_KEY);
         auto rootAssetRef = getDataRefForKey(sharedTx.get(), bwKey.getData());

         auto seedUPtr = EncryptedSeed::deserialize(rootAssetRef);
         seed_ = std::shared_ptr<EncryptedSeed>(move(seedUPtr));
         if (seed_ == nullptr) {
            throw WalletException("failed to deser wallet seed");
         }
      } catch (const IO::NoEntryInWalletException&) {}
   }

   {
      //label
      BinaryWriter bwKey;
      bwKey.put_uint32_t(WALLET_LABEL_KEY);
      try {
         auto labelRef = getDataRefForKey(sharedTx.get(), bwKey.getData());
         label_ = std::string{labelRef.toCharPtr(), labelRef.getSize()};
      } catch (const IO::NoEntryInWalletException&) {}
   }

   {
      //description
      BinaryWriter bwKey;
      bwKey.put_uint32_t(WALLET_DESCR_KEY);
      try {
         auto descrRef = getDataRefForKey(sharedTx.get(), bwKey.getData());
         description_ = std::string{descrRef.toCharPtr(), descrRef.getSize()};
      } catch (const IO::NoEntryInWalletException&) {}
   }

   //encryption keys and kdfs
   decryptedData_->readFromDisk(sharedTx);

   {
      //accounts
      BinaryWriter bwPrefix;
      bwPrefix.put_uint8_t(ADDRESS_ACCOUNT_PREFIX);
      auto dbIter = sharedTx->getIterator();
      dbIter->seek(bwPrefix.getDataRef());

      while (dbIter->isValid()) {
         //iterate through account keys
         const auto& key = dbIter->key();
         try {
            auto addrAccId = AddressAccountId::deserializeKey(
               key, ADDRESS_ACCOUNT_PREFIX);

            //instantiate account object and read data on disk
            auto addressAccount = AddressAccount::readFromDisk(
               walletTx, addrAccId);
            std::shared_ptr<AddressAccount> accPtr(std::move(addressAccount));

            //insert
            accounts_.emplace(accPtr->getID(), accPtr);
         } catch (const IdException&) {
            //in case of exception, the value for this key is not for an
            //account. Assume we ran out of accounts and break out.
            break;
         }
         dbIter->advance();
      }

      loadMetaAccounts();
   }
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AddressEntry> AssetWallet::getNewAddress(
   AddressEntryType aeType)
{
   /***
   The wallet will always try to deliver an address with the requested type if
   any of its accounts supports it. It will prioritize the main account, then
   try through all accounts in binary order.
   ***/

   //lock
   ReentrantLock lock(this);

   if (!mainAccountId_.isValid()) {
      throw WalletException("no main account for wallet");
   }

   auto mainAccount = getAccountForID(mainAccountId_);
   if (mainAccount->hasAddressType(aeType)) {
      return mainAccount->getNewAddress(iface_, aeType);
   }

   for (auto& account : accounts_) {
      if (account.second->hasAddressType(aeType)) {
         return account.second->getNewAddress(iface_, aeType);
      }
   }

   throw WalletException("[getNewAddress] unexpected address entry type");
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AddressEntry> AssetWallet::getNewChangeAddress(
   AddressEntryType aeType)
{
   ReentrantLock lock(this);
   if (!mainAccountId_.isValid()) {
      throw WalletException("no main account for wallet");
   }

   auto mainAccount = getAccountForID(mainAccountId_);
   if (mainAccount->hasAddressType(aeType)) {
      return mainAccount->getNewChangeAddress(iface_, aeType);
   }

   for (auto& account : accounts_) {
      if (account.second->hasAddressType(aeType)) {
         return account.second->getNewChangeAddress(iface_, aeType);
      }
   }

   throw WalletException("[getNewChangeAddress] unexpected address entry type");
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AddressEntry> AssetWallet::peekNextChangeAddress(
   AddressEntryType aeType)
{
   ReentrantLock lock(this);

   if (!mainAccountId_.isValid()) {
      throw WalletException("no main account for wallet");
   }

   auto mainAccount = getAccountForID(mainAccountId_);
   if (mainAccount->hasAddressType(aeType)) {
      return mainAccount->peekNextChangeAddress(iface_, aeType);
   }

   for (auto& account : accounts_) {
      if (account.second->hasAddressType(aeType)) {
         return account.second->peekNextChangeAddress(iface_, aeType);
      }
   }

   throw WalletException("[peekNextChangeAddress] unexpected address entry type");
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::updateAddressEntryType(
   const AssetId& assetID, AddressEntryType aeType)
{
   ReentrantLock lock(this);

   auto accPtr = getAccountForID(assetID.getAddressAccountId());
   accPtr->updateInstantiatedAddressType(iface_, assetID, aeType);
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AddressEntry> AssetWallet::getNewAddress(
   const AddressAccountId& accountID, AddressEntryType aeType)
{
   ReentrantLock lock(this);

   auto account = getAccountForID(accountID);
   return account->getNewAddress(iface_, aeType);
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AddressEntry> AssetWallet::getNewAddress(
   const AssetAccountId& accountID, AddressEntryType aeType)
{
   ReentrantLock lock(this);

   auto account = getAccountForID(accountID.getAddressAccountId());
   return account->getNewAddress(iface_, accountID, aeType, {});
}

////////////////////////////////////////////////////////////////////////////////
bool AssetWallet::hasAddrStr(const std::string& addrStr,
   const AddressAccountId& hint) const
{
   try {
      getAssetIDForAddrStr(addrStr, hint);
      return true;
   } catch (const std::runtime_error&) {
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
bool AssetWallet::hasScrAddr(const BinaryData& scrAddr,
   const AddressAccountId& hint) const
{
   try {
      getAssetIDForScrAddr(scrAddr, hint);
      return true;
   } catch (const std::runtime_error&) {
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
const std::pair<AssetId, AddressEntryType>& AssetWallet::getAssetIDForAddrStr(
   const std::string& addrStr, const AddressAccountId& hint) const
{
   //this takes b58 or bech32 addresses
   ReentrantLock lock(this);
   BinaryData scrAddr;

   try {
      scrAddr = std::move(BtcUtils::base58toScrAddr(addrStr));
   } catch (const std::runtime_error&) {
      scrAddr = std::move(BtcUtils::segWitAddressToScrAddr(addrStr).first);
   }
   return getAssetIDForScrAddr(scrAddr, hint);
}

////////////////////////////////////////////////////////////////////////////////
const std::pair<AssetId, AddressEntryType>&
   AssetWallet::getAssetIDForScrAddr(const BinaryData& scrAddr,
   const AddressAccountId& hint) const
{
   //this takes prefixed hashes
   ReentrantLock lock(this);

   if (hint.isValid()) {
      auto accIter = accounts_.find(hint);
      if (accIter != accounts_.end()) {
         return accIter->second->getAssetIDPairForAddr(scrAddr);
      }
   } else {
      for (auto acc : accounts_) {
         try {
            return acc.second->getAssetIDPairForAddr(scrAddr);
         } catch (const std::runtime_error&) {
            continue;
         }
      }
   }
   throw std::runtime_error("unknown scrAddr");
}

////////////////////////////////////////////////////////////////////////////////
AddressEntryType AssetWallet::getAddrTypeForID(const AssetId& id) const
{
   ReentrantLock lock(this);

   auto addrPtr = getAddressEntryForID(id);
   return addrPtr->getType();
}

////////////////////////////////////////////////////////////////////////////////
bool AssetWallet::isAssetUsed(const AssetId& id) const
{
   try {
      auto acc = getAccountForID(id.getAddressAccountId());
      if (acc == nullptr) {
         return false;
      }
      return acc->isAssetUsed(id);
   } catch (const std::exception&) {
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
const Armory::Wallets::AddressAccountId& AssetWallet::getMainAccountID() const
{
   if (!mainAccountId_.isValid()) {
      throw WalletException("[getMainAccountID] invalid account id");
   }
   return mainAccountId_;
}

////////////////////////////////////////////////////////////////////////////////
const EncryptionKeyId& AssetWallet::getDefaultEncryptionKeyId() const
{
   if (decryptedData_ == nullptr) {
      throw WalletException("[getDefaultEncryptionKeyId] unexpected error");
   }
   return decryptedData_->getDefaultEncryptionKeyId();
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<Encryption::KeyDerivationFunction>
   AssetWallet::getDefaultKdf() const
{
   return decryptedData_->getKdf(decryptedData_->getDefaultKdfId());
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AddressAccount> AssetWallet::getAccountForID(
   const AddressAccountId& id) const
{
   if (!id.isValid()) {
      throw WalletException("[getAccountForID] invalid account id");
   }
   ReentrantLock lock(this);

   auto iter = accounts_.find(id);
   if (iter == accounts_.end()) {
      throw WalletException("[getAccountForID] unknown account ID");
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
const std::filesystem::path& AssetWallet::getDbFilename() const
{ 
   if (iface_ == nullptr) {
      throw WalletException("uninitialized db environment");
   }
   return iface_->getFilename();
}

////////////////////////////////////////////////////////////////////////////////
const std::string& AssetWallet::getDbName() const
{ 
   return dbName_;
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::shutdown()
{
   if (iface_ == nullptr) {
      return;
   }
   iface_.reset();
}

////////////////////////////////////////////////////////////////////////////////
AddressEntryType AssetWallet::getAddrTypeForAccount(const AssetId& id) const
{
   auto acc = getAccountForID(id.getAddressAccountId());
   return acc->getDefaultAddressType();
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AddressEntry> AssetWallet::getAddressEntryForID(
   const AssetId& id) const
{
   ReentrantLock lock(this);
   if (!id.isValid()) {
      throw WalletException("invalid asset id");
   }
   auto accPtr = getAccountForID(id.getAddressAccountId());
   return accPtr->getAddressEntryForID(id);
}

////////////////////////////////////////////////////////////////////////////////
std::set<BinaryData> AssetWallet::getAddrHashSet() const
{
   ReentrantLock lock(this);
   std::set<BinaryData> addrHashSet;
   for (const auto account : accounts_) {
      const auto& hashes = account.second->getAddressHashMap();
      for (const auto& hashPair : hashes) {
         addrHashSet.emplace(hashPair.first);
      }
   }
   return addrHashSet;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AssetEntry> AssetWallet::getAssetForID(const AssetId& id) const
{
   if (!id.isValid()) {
      throw WalletException("invalid asset ID");
   }
   ReentrantLock lock(this);

   auto acc = getAccountForID(id.getAddressAccountId());
   return acc->getAssetForID(id);
}

////////////////////////////////////////////////////////////////////////////////
const WalletId& AssetWallet::getID() const
{
   return walletID_;
}

////
const WalletId& AssetWallet::getMasterID() const
{
   return masterID_;
}

////////////////////////////////////////////////////////////////////////////////
ReentrantLock AssetWallet::lockDecryptedContainer(void)
{
   return std::move(ReentrantLock(decryptedData_.get()));
}

////////////////////////////////////////////////////////////////////////////////
bool AssetWallet::isDecryptedContainerLocked() const
{
   try {
      auto lock = SingleLock(decryptedData_.get());
      return false;
   } catch (const AlreadyLocked&) {
      return true;
   }
}

////
void AssetWallet::setPassphrasePromptLambda(const Passphrase::UnlockFunc& func)
{
   decryptedData_->setPassphrasePromptLambda(func);
}

////
void AssetWallet::resetPassphrasePromptLambda()
{
   decryptedData_->resetPassphraseLambda();
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<Encryption::KeyDerivationFunction>
AssetWallet::getPrimaryKdf() const
{
   return decryptedData_->getMasterKdf();
}

////////////////////////////////////////////////////////////////////////////////
bool AssetWallet::isMasterRecordEncrypted() const
{
   return decryptedData_->isMasterKeyEncrypted();
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::extendPublicChain(int32_t count)
{
   for (auto& account : accounts_) {
      account.second->extendPublicChain(iface_, count);
   }
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::extendPublicChain(const AddressAccountId& accountId,
   int32_t count, const ProgressFunc& progressCallback)
{
   auto account = getAccountForID(accountId);
   account->extendPublicChain(iface_, count, progressCallback);
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::extendPrivateChain(int32_t count)
{
   for (auto& account : accounts_) {
      account.second->extendPrivateChain(iface_, decryptedData_, count);
   }
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::extendPublicChainToIndex(const AddressAccountId& accountId,
   int32_t count, const ProgressFunc& progressCallback)
{
   auto account = getAccountForID(accountId);
   account->extendPublicChainToIndex(iface_,
      account->getOuterAccount()->getID(), count, progressCallback);
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::extendPrivateChainToIndex(int32_t count)
{
   for (auto& account : accounts_) {
      extendPrivateChainToIndex(account.first, count);
   }
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::extendPrivateChainToIndex(
   const AddressAccountId& accountId, int32_t count)
{
   auto account = getAccountForID(accountId);
   account->extendPrivateChainToIndex(
      iface_, decryptedData_,
      account->getOuterAccount()->getID(), count
   );
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::addSubDB(const std::string& dbName,
   const Passphrase::UnlockFunc& passFunc)
{
   if (iface_->getFreeDbCount() == 0) {
      iface_->setDbCount(iface_->getDbCount() + 1);
   }
   auto headerPtr = std::make_shared<IO::WalletHeader_Custom>();
   headerPtr->walletID_ = WalletId{std::string_view{dbName}};

   try {
      iface_->lockControlContainer(passFunc);
      iface_->addHeader(headerPtr);
      iface_->unlockControlContainer();
   } catch (...) {
      iface_->unlockControlContainer();
      rethrow_exception(std::current_exception());
   }
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<IO::WalletIfaceTransaction> AssetWallet::beginSubDBTransaction(
   const std::string& dbName, bool write)
{
   std::shared_ptr<IO::DBIfaceTransaction> tx;
   if (!write) {
      tx = iface_->beginReadTransaction(dbName);
   } else {
      tx = iface_->beginWriteTransaction(dbName);
   }

   auto wltTx = std::dynamic_pointer_cast<IO::WalletIfaceTransaction>(tx);
   if (wltTx == nullptr) {
      throw WalletException("[beginSubDBTransaction] invalid dbtx type");
   }
   return wltTx;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AssetWallet> AssetWallet::loadMainWalletFromFile(
   const IO::ReadOnlyFileParams& params)
{
   auto iface = std::make_shared<IO::WalletDBInterface>();
   iface->setupEnv(params);
   auto mainWalletID = getMainWalletID(iface);
   auto headerPtr = iface->getWalletHeader(mainWalletID);

   std::shared_ptr<AssetWallet> wltPtr;
   switch (headerPtr->type_)
   {
      case IO::WalletHeaderType_Single:
      {
         auto wltSingle = std::make_shared<AssetWallet_Single>(
            iface, headerPtr, WalletId{});
         wltSingle->readFromFile();

         wltPtr = wltSingle;
         break;
      }

      case IO::WalletHeaderType_Multisig:
      {
         auto wltMS = std::make_shared<AssetWallet_Multisig>(
            iface, headerPtr, WalletId{});
         wltMS->readFromFile();

         wltPtr = wltMS;
         break;
      }

      default:
         throw WalletException("unexpected main wallet type");
   }
   return wltPtr;
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef AssetWallet::getDataRefForKey(
   IO::DBIfaceTransaction* tx, const BinaryData& key)
{
   /** The reference lifetime is tied to the db tx lifetime. The caller has to
   maintain the tx for as long as the data ref needs to be valid **/

   auto ref = tx->getDataRef(key);

   if (ref.empty()) {
      throw IO::NoEntryInWalletException();
   }
   return DBUtils::getDataRefForPacket(ref);
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::addMetaAccount(MetaAccountType type)
{
   auto account_ptr = std::make_shared<MetaDataAccount>(dbName_);
   account_ptr->make_new(type);

   //do not overwrite existing account of the same type
   if (metaDataAccounts_.find(type) != metaDataAccounts_.end()) {
      return;
   }

   auto tx = iface_->beginWriteTransaction(dbName_);
   account_ptr->commit(std::move(tx));
   metaDataAccounts_.emplace(type, account_ptr);
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::loadMetaAccounts()
{
   auto tx = iface_->beginReadTransaction(dbName_);

   //accounts
   BinaryWriter bwPrefix;
   bwPrefix.put_uint8_t(META_ACCOUNT_PREFIX);
   auto dbIter = tx->getIterator();
   dbIter->seek(bwPrefix.getDataRef());

   while (dbIter->isValid()) {
      //iterate through account keys
      const auto& key = dbIter->key();
      try {
         //instantiate account object and read data on disk
         auto metaAccount = std::make_shared<MetaDataAccount>(dbName_);
         metaAccount->readFromDisk(iface_, key);

         //insert
         metaDataAccounts_.emplace(metaAccount->getType(), metaAccount);
      } catch (const std::exception&) {
         //in case of exception, the value for this key is not for an
         //account. Assume we ran out of accounts and break out.
         break;
      }
      dbIter->advance();
   }
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<MetaDataAccount> AssetWallet::getMetaAccount(
   MetaAccountType type) const
{
   auto iter = metaDataAccounts_.find(type);
   if (iter == metaDataAccounts_.end()) {
      throw WalletException("no meta account for this type");
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
std::filesystem::path AssetWallet::forkWatchingOnly(
   const IO::ReadOnlyFileParams& params,
   const Passphrase::SetNew& woCtrlPassObj)
{
   //open original wallet db & new
   auto originIface = std::make_shared<IO::WalletDBInterface>();
   originIface->setupEnv(params);
   auto masterID = getMasterID(originIface);

   //cycle through wallet metas, copy wallet structure and assets
   for (auto& metaPtr : originIface->getHeaderMap()) {
      switch (metaPtr.second->type_)
      {
         case IO::WalletHeaderType_Single:
         {
            //load wallet
            auto wltSingle = std::make_shared<AssetWallet_Single>(
               originIface, metaPtr.second, masterID);
            wltSingle->readFromFile();

            //copy content
            auto wpd = AssetWallet_Single::exportPublicData(wltSingle);
            wltSingle.reset();
            return AssetWallet_Single::forkWatchingOnly(wpd, woCtrlPassObj);
         }

         default:
            break;
      }
   }

   throw std::runtime_error(
      "wallet contains header types that aren't covered by WO forking");
}

////////////////////////////////////////////////////////////////////////////////
std::set<AddressAccountId> AssetWallet::getAccountIDs() const
{
   std::set<AddressAccountId> result;
   for (auto& accPtr : accounts_) {
      result.insert(accPtr.second->getID());
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
std::map<AssetId, std::shared_ptr<AddressEntry>>
AssetWallet::getUsedAddressMap() const
{
   /***
   This is an expensive call, do not spam it.
   ***/

   std::map<AssetId, std::shared_ptr<AddressEntry>> result;
   for (auto& account : accounts_) {
      auto addrMap = account.second->getUsedAddressMap();
      result.insert(addrMap.begin(), addrMap.end());
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::changeControlPassphrase(
   Passphrase::SetNew& newPassObj,
   const Passphrase::UnlockFunc& passLbd)
{
   iface_->changeControlPassphrase(newPassObj, passLbd);
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::eraseControlPassphrase(const Passphrase::UnlockFunc& passLbd)
{
   iface_->eraseControlPassphrase(passLbd);
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::setComment(const BinaryData& key, const std::string& comment)
{
   auto accPtr = getMetaAccount(MetaAccountType::Comments);
   auto uniqueTx = iface_->beginWriteTransaction(dbName_);
   std::shared_ptr<IO::DBIfaceTransaction> sharedTx(move(uniqueTx));
   CommentAssetConversion::setAsset(accPtr.get(), key, comment, sharedTx);
}

////////////////////////////////////////////////////////////////////////////////
const std::string& AssetWallet::getComment(const BinaryData& key) const
{
   auto accPtr = getMetaAccount(MetaAccountType::Comments);
   auto assetPtr = CommentAssetConversion::getByKey(accPtr.get(), key);

   if (assetPtr == nullptr) {
      throw WalletException("no comment for key");
   }
   return assetPtr->getValue();
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::deleteComment(const BinaryData& key)
{
   auto accPtr = getMetaAccount(MetaAccountType::Comments);
   auto uniqueTx = iface_->beginWriteTransaction(dbName_);
   std::shared_ptr<IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   CommentAssetConversion::deleteAsset(accPtr.get(), key, sharedTx);
}

////////////////////////////////////////////////////////////////////////////////
std::map<BinaryData, std::string> AssetWallet::getCommentMap() const
{
   auto accPtr = getMetaAccount(MetaAccountType::Comments);
   return CommentAssetConversion::getCommentMap(accPtr.get());
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::setLabel(const std::string& str)
{
   label_ = str;

   BinaryWriter bwKey;
   bwKey.put_uint32_t(WALLET_LABEL_KEY);
   BinaryWriter bwData;
   bwData.put_var_int(str.size());
   bwData.put_String(str);

   auto tx = iface_->beginWriteTransaction(dbName_);
   tx->insert(bwKey.getDataRef(), bwData.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::setDescription(const std::string& str)
{
   description_ = str;

   BinaryWriter bwKey;
   bwKey.put_uint32_t(WALLET_DESCR_KEY);
   BinaryWriter bwData;
   bwData.put_var_int(str.size());
   bwData.put_String(str);

   auto tx = iface_->beginWriteTransaction(dbName_);
   tx->insert(bwKey.getDataRef(), bwData.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
const std::string& AssetWallet::getLabel() const
{
   return label_;
}

////////////////////////////////////////////////////////////////////////////////
const std::string& AssetWallet::getDescription() const
{
   return description_;
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet::eraseFromDisk(AssetWallet* wltPtr)
{
   if (wltPtr == nullptr) {
      throw std::runtime_error("null wltPtr");
   }
   auto ifaceCopy = std::move(wltPtr->iface_);
   ifaceCopy->eraseFromDisk();
   ifaceCopy.reset();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// AssetWallet_Single
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
AssetWallet_Single::AssetWallet_Single(
   std::shared_ptr<IO::WalletDBInterface> iface,
   std::shared_ptr<IO::WalletHeader> metaPtr,
   const WalletId& masterID) :
   AssetWallet(iface, metaPtr, masterID)
{
   if (metaPtr == nullptr ||
      metaPtr->magicBytes_ != Armory::Config::BitcoinSettings::getMagicBytes()) {
      throw WalletException(
         "[AssetWallet_Single] network magic bytes mismatch");
   }
}

////
std::shared_ptr<AssetEntry> AssetWallet_Single::getRoot() const
{
   return root_;
}

////////////////////////////////////////////////////////////////////////////////
const AddressAccountId& AssetWallet_Single::createBIP32Account(
   std::shared_ptr<AccountType_BIP32> accTypePtr,
   const Progress::Func& prog)
{
   auto accountPtr = createAccount(accTypePtr, prog);
   auto lookup = accTypePtr->getAddressLookup();
   if (lookup > 0) {
      if (prog) {
         auto prg = std::make_unique<Progress::ExtendChain>(lookup);
         prog(std::move(prg));
      }

      if (!isWatchingOnly()) {
         accountPtr->extendPrivateChain(iface_, decryptedData_, lookup);
      } else {
         accountPtr->extendPublicChain(iface_, lookup);
      }
   }
   return accountPtr->getID();
}

////////////////////////////////////////////////////////////////////////////////
// wallet creation
std::shared_ptr<AssetWallet_Single> AssetWallet_Single::createFromSeed(
   std::unique_ptr<ClearTextSeed> seed, const IO::CreateWalletParams& params)
{
   //sanity check
   if (seed == nullptr) {
      throw WalletException("[AssetWallet_Single::createFromSeed] null seed");
   }

   //determine wallet type from seed type
   std::shared_ptr<AssetWallet_Single> result;
   switch (seed->type())
   {
      case Seeds::SeedType::ArmoryLegacy:
      {
         auto seedLegacy = dynamic_cast<ClearTextSeed_Armory*>(seed.get());
         result = createFromSeed(seedLegacy, params);
         break;
      }

      case Seeds::SeedType::ArmoryLegacyPublic:
      {
         auto seedLegacy = dynamic_cast<ClearTextSeed_ArmoryPublic*>(seed.get());
         result = createFromPublicSeed(seedLegacy, params);
         break;
      }

      case Seeds::SeedType::BIP32_Structured:
      case Seeds::SeedType::BIP32_Virgin:
      case Seeds::SeedType::BIP32_base58Root:
      case Seeds::SeedType::BIP39:
      {
         auto seedBip32 = dynamic_cast<ClearTextSeed_BIP32*>(seed.get());
         result = createFromSeed(seedBip32, params);
         break;
      }

      default:
         throw WalletException("[AssetWallet_Single::createFromSeed]"
            " unexpected seed type");
   }
   return result;
}

//// from legacy seed
std::shared_ptr<AssetWallet_Single> AssetWallet_Single::createFromSeed(
   ClearTextSeed_Armory* seed, const IO::CreateWalletParams& params)
{
   if (seed == nullptr) {
      throw WalletException("[createFromSeed] null root");
   }

   //create wallet file and dbenv
   const auto& masterId = seed->getMasterId();
   const auto& walletId = seed->getWalletId();
   auto iface = createIface(
      params.getCreateFileParams(masterId),
      params.progressFunc
   );

   //create empty wallet
   auto walletPtr = initWalletDb(iface, seed, params);

   //set as main
   setMainWallet(iface, walletId);

   //create account
   auto account135 = std::make_shared<AccountType_ArmoryLegacy>();
   account135->setMain(true);
   walletPtr->decryptedData_->setPassphrasePromptLambda(
      params.setPrivPassObj.getUnlockFunc());

   //add primary account
   auto accountPtr = walletPtr->createAccount(account135, params.progressFunc);

   //compute lookup worth of address in primary account
   if (params.lookup > 0) {
      if (params.progressFunc) {
         auto prg = std::make_unique<Progress::ExtendChain>(params.lookup);
         params.progressFunc(std::move(prg));
      }

      //legacy derivation bootstraps the accounts with asset 0,
      //lookup should be reduced by 1
      accountPtr->extendPrivateChain(
         iface, walletPtr->decryptedData_, params.lookup - 1);
   }

   //clean up and return wallet
   walletPtr->resetPassphrasePromptLambda();
   return walletPtr;
}

//// from bip32 seed
std::shared_ptr<AssetWallet_Single> AssetWallet_Single::createFromSeed(
   Seeds::ClearTextSeed_BIP32* seed, const IO::CreateWalletParams& params)
{
   //sanity checks
   if (seed == nullptr) {
      throw WalletException("[createFromSeed] null seed");
   }

   auto rootNode = seed->getRootNode();
   if (rootNode->isPublic()) {
      throw WalletException("[createFromSeed]"
         " BIP32 seeds cannot lead to WO wallets");
   }

   const auto coinType = Armory::Config::BitcoinSettings::getCoinType();
   const auto fingerprint = rootNode->getThisFingerprint();

   //db env
   auto iface = createIface(
      params.getCreateFileParams(seed->getMasterId()),
      params.progressFunc
   );

   //wallet object
   auto walletPtr = initWalletDb(iface, seed, params);

   //set as main
   setMainWallet(iface, seed->getWalletId());

   //address accounts
   std::set<std::shared_ptr<AccountType_BIP32>> accountTypes;
   {
      //legacy account: 44
      std::vector<unsigned> path = { 0x8000002C, coinType, 0x80000000 };
      auto legacyAcc = AccountType_BIP32::makeFromDerPaths(
         fingerprint, {path});

      //nodes
      legacyAcc->setNodes({
         BIP32_OUTER_ACCOUNT_DERIVATIONID,
         BIP32_INNER_ACCOUNT_DERIVATIONID});
      legacyAcc->setOuterAccountID(
         BIP32_OUTER_ACCOUNT_DERIVATIONID);
      legacyAcc->setInnerAccountID(
         BIP32_INNER_ACCOUNT_DERIVATIONID);

      //lookup
      legacyAcc->setAddressLookup(params.lookup);

      //address types
      legacyAcc->addAddressType(AddressEntryType(
         AddressEntryType::P2PKH | AddressEntryType::Uncompressed));
      legacyAcc->addAddressType(AddressEntryType::P2PKH);
      legacyAcc->setDefaultAddressType(AddressEntryType::P2PKH);

      legacyAcc->setMain(true);
      accountTypes.emplace(legacyAcc);
   }

   {
      //nested sw account: 49
      std::vector<unsigned> path = { 0x80000031, coinType, 0x80000000 };
      auto nestedAcc = AccountType_BIP32::makeFromDerPaths(
         fingerprint, {path});

      //nodes
      nestedAcc->setNodes({
         BIP32_OUTER_ACCOUNT_DERIVATIONID,
         BIP32_INNER_ACCOUNT_DERIVATIONID});
      nestedAcc->setOuterAccountID(
         BIP32_OUTER_ACCOUNT_DERIVATIONID);
      nestedAcc->setInnerAccountID(
         BIP32_INNER_ACCOUNT_DERIVATIONID);

      //lookup
      nestedAcc->setAddressLookup(params.lookup);

      //address types
      nestedAcc->addAddressType(
         AddressEntryType(AddressEntryType::P2SH | AddressEntryType::P2WPKH));
      nestedAcc->setDefaultAddressType(
         AddressEntryType(AddressEntryType::P2SH | AddressEntryType::P2WPKH));
      accountTypes.emplace(nestedAcc);
   }

   {
      //sw account: 84
      std::vector<unsigned> path = { 0x80000054, coinType, 0x80000000 };
      auto segwitAcc = AccountType_BIP32::makeFromDerPaths(
         fingerprint, {path});

      //nodes
      segwitAcc->setNodes({
         BIP32_OUTER_ACCOUNT_DERIVATIONID,
         BIP32_INNER_ACCOUNT_DERIVATIONID});
      segwitAcc->setOuterAccountID(
         BIP32_OUTER_ACCOUNT_DERIVATIONID);
      segwitAcc->setInnerAccountID(
         BIP32_INNER_ACCOUNT_DERIVATIONID);

      //lookup
      segwitAcc->setAddressLookup(params.lookup);

      //address types
      segwitAcc->addAddressType(AddressEntryType::P2WPKH);
      segwitAcc->setDefaultAddressType(AddressEntryType::P2WPKH);
      accountTypes.emplace(segwitAcc);
   }

   //add accounts
   walletPtr->setPassphrasePromptLambda(params.setPrivPassObj.getUnlockFunc());
   switch (seed->type())
   {
      case SeedType::BIP32_Structured:
      case SeedType::BIP39:
      {
         for (auto accountPtr : accountTypes) {
            walletPtr->createBIP32Account(accountPtr, params.progressFunc);
         }
         break;
      }

      default:
         //no accounts structure for these seeds
         break;
   }

   //cleanup and return
   walletPtr->resetPassphrasePromptLambda();
   return walletPtr;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AssetWallet_Single>
AssetWallet_Single::createFromPublicSeed(
   Seeds::ClearTextSeed_ArmoryPublic* seed,
   const IO::CreateWalletParams& params)
{
   //create wallet file and dbenv
   auto masterID = seed->getMasterId();
   auto iface = createIface(
      params.getCreateFileParams(masterID, "WatchingOnly"),
      params.progressFunc
   );

   SecureBinaryData pubRoot = seed->getPublicRoot();
   auto rootPtr = std::make_shared<AssetEntry_ArmoryLegacyRoot>(
      AssetId::getRootAssetId(), pubRoot, nullptr, seed->getChaincode(),
      seed->getLegacyType());

   //create wallet
   auto walletID = seed->getWalletId();
   auto walletPtr = initWalletDbWithPubRoot(
      iface, masterID, walletID,
      rootPtr, params);

   //set as main
   setMainWallet(iface, walletID);

   //add account
   auto account135 = std::make_shared<AccountType_ArmoryLegacy>();
   account135->setMain(true);

   auto accountPtr = walletPtr->createAccount(account135, params.progressFunc);
   if (params.lookup > 0) {
      if (params.progressFunc) {
         auto prg = std::make_unique<Progress::ExtendChain>(params.lookup);
         params.progressFunc(std::move(prg));
      }
      //legacy derivation bootstraps the accounts with asset 0,
      //lookup should be reduce by 1
      accountPtr->extendPublicChain(iface, params.lookup - 1);
   }
   return walletPtr;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AssetWallet_Single> AssetWallet_Single::createBlank(
   const WalletId& walletID, const IO::CreateWalletParams& params)
{
   //create wallet file and dbenv
   auto masterID = walletID;
   auto iface = createIface(
      params.getCreateFileParams(masterID),
      params.progressFunc
   );

   //ctors move the arguments in, gotta create copies first
   auto walletPtr = initWalletDbWithPubRoot(
      iface,
      masterID, walletID,
      nullptr, params);

   //set as main
   setMainWallet(iface, walletID);
   return walletPtr;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AssetWallet_Single> AssetWallet_Single::initWalletDb(
   std::shared_ptr<IO::WalletDBInterface> iface,
   ClearTextSeed* seed, const IO::CreateWalletParams& params)
{
   if (params.progressFunc) {
      auto prg = std::make_unique<Progress::InitWalletFile>(seed->getMasterId());
      params.progressFunc(std::move(prg));
   }

   auto headerPtr = std::make_shared<IO::WalletHeader_Single>(
      Armory::Config::BitcoinSettings::getMagicBytes());
   headerPtr->walletID_ = seed->getWalletId();

   //init headerPtr object
   IO::MasterKeyStruct masterKeyStruct;
   try {
      auto paramsCopy = params.setPrivPassObj.get();
      if (paramsCopy.passphrase.empty()) {
         LOGWARN << "!! No private passphrase provided !!";
         LOGWARN << "!! Private keys in this wallet will not be encrypted !!";
      }
      masterKeyStruct = std::move(IO::WalletDBInterface::initWalletHeaderObject(
         headerPtr, paramsCopy));
   } catch (const Passphrase::PassphraseException& e) {
      //user rejected password request, cleanup and rethrow
      auto path = iface->getFilename();
      iface->shutdown();
      std::filesystem::remove(path);
      throw WalletException(e.what());
   }

   //copy cipher to cycle the IV then encrypt the private root
   auto encryptPrivateData = [&masterKeyStruct, &headerPtr]
   (const SecureBinaryData& privateData)->
   std::unique_ptr<Encryption::CipherData>
   {
      auto rootCipher = masterKeyStruct.cipher_->getCopy(
         headerPtr->masterEncryptionKeyId_);

      auto encryptedRoot = rootCipher->encrypt(
         masterKeyStruct.decryptedMasterKey_.get(),
         rootCipher->getKdfId(), privateData);
      return std::make_unique<Encryption::CipherData>(
         encryptedRoot, std::move(rootCipher));
   };

   //root asset for bip32 and armory seeds are handled seperately
   std::unique_ptr<AssetEntry> rootAssetEntry;
   switch (seed->type())
   {
      case SeedType::ArmoryLegacy:
      {
         auto seedLegacy = dynamic_cast<ClearTextSeed_Armory*>(seed);
         const auto& privateRoot = seedLegacy->getRoot();
         auto chaincode = seedLegacy->getChaincode();
         if (chaincode.empty()) {
            chaincode = BtcUtils::computeChainCode_ArmoryLegacy(privateRoot);
         }
         auto pubkey = Cryptography::ECDSA::computePublicKey(privateRoot);

         auto cipherData = encryptPrivateData(privateRoot);
         auto rootAsset = std::make_shared<Asset_PrivateKey>(
            AssetId::getRootAssetId(), std::move(cipherData));

         rootAssetEntry = std::make_unique<AssetEntry_ArmoryLegacyRoot>(
            AssetId::getRootAssetId(),
            pubkey, rootAsset, chaincode,
            seedLegacy->getLegacyType()
         );
         break;
      }

      case SeedType::BIP39:
      case SeedType::BIP32_Structured:
      case SeedType::BIP32_Virgin:
      case SeedType::BIP32_base58Root:
      {
         auto seedBip32 = dynamic_cast<ClearTextSeed_BIP32*>(seed);
         auto rootNode = seedBip32->getRootNode();
         auto pubkey = rootNode->getPublicKey();

         auto cipherData = encryptPrivateData(rootNode->getPrivateKey());
         auto rootAsset = std::make_shared<Asset_PrivateKey>(
            AssetId::getRootAssetId(), std::move(cipherData));

         rootAssetEntry = std::make_unique<AssetEntry_BIP32Root>(
            AssetId::getRootAssetId(),
            pubkey, rootAsset,
            rootNode->getChaincode(), 0, 0, 0,
            rootNode->getThisFingerprint(), std::vector<uint32_t>{}
         );
         break;
      }

      default:
         throw std::runtime_error("seed type not supported yet!");
   }

   //create wallet
   auto walletPtr = std::make_shared<AssetWallet_Single>(
      iface, headerPtr, seed->getMasterId());

   //add kdf & master key
   walletPtr->decryptedData_->addKdf(masterKeyStruct.kdf_);
   walletPtr->decryptedData_->addEncryptionKey(masterKeyStruct.masterKey_);

   //put wallet db name in meta db
   iface->lockControlContainer(params.setCtrlPassObj.getUnlockFunc());
   iface->addHeader(headerPtr);
   iface->unlockControlContainer();

   //insert the original entries
   {
      auto tx = iface->beginWriteTransaction(walletPtr->dbName_);

      {
         //decrypted data container
         walletPtr->decryptedData_->updateOnDisk();
      }

      {
         //seed
         BinaryWriter bw; //TODO: need SBD based bw
         seed->serialize(bw);
         auto cipherData = encryptPrivateData(bw.getDataRef());
         walletPtr->seed_ = std::make_unique<EncryptedSeed>(
            std::move(cipherData), seed->type());

         //write to disk
         BinaryWriter bwKey;
         bwKey.put_uint32_t(WALLET_SEED_KEY);
         tx->insert(bwKey.getData(), walletPtr->seed_->serialize());
      }

      {
         //root asset
         BinaryWriter bwKey;
         bwKey.put_uint32_t(ROOTASSET_KEY);

         auto data = rootAssetEntry->serialize();
         tx->insert(bwKey.getData(), data);
      }

      {
         //comment account
         walletPtr->addMetaAccount(
            MetaAccountType::Comments);
      }

      if (!params.label.empty()) {
         walletPtr->setLabel(params.label);
      }
      if (!params.description.empty()) {
         walletPtr->setDescription(params.description);
      }
   }

   //init walletptr from file
   if (params.progressFunc) {
      auto prg = std::make_unique<Progress::ReadWalletFile>(
         seed->getMasterId());
      params.progressFunc(std::move(prg));
   }
   walletPtr->readFromFile();
   return walletPtr;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AssetWallet_Single> AssetWallet_Single::initWalletDbWithPubRoot(
   std::shared_ptr<IO::WalletDBInterface> iface,
   const WalletId& masterID, const WalletId& walletID,
   std::shared_ptr<AssetEntry_Single> pubRoot,
   const IO::CreateWalletParams& params)
{
   if (pubRoot != nullptr) {
      if (pubRoot->hasPrivateKey()) {
         throw WalletException("[initWalletDbWithPubRoot] root has priv key");
      }
   }

   if (params.progressFunc) {
      auto prg = std::make_unique<Progress::InitWalletFile>(masterID);
      params.progressFunc(std::move(prg));
   }

   auto headerPtr = std::make_shared<IO::WalletHeader_Single>(
      Armory::Config::BitcoinSettings::getMagicBytes());
   headerPtr->walletID_ = walletID;

   try {
      auto ctrlParams = params.setCtrlPassObj.get();
      IO::WalletDBInterface::initWalletHeaderObject(headerPtr, ctrlParams);
   } catch (const std::exception&) {
      Passphrase::Params defaultParams{250ms, 0, {}};
      IO::WalletDBInterface::initWalletHeaderObject(headerPtr, defaultParams);
   }
   auto walletPtr = std::make_shared<AssetWallet_Single>(
      iface, headerPtr, masterID);

   iface->lockControlContainer(params.setCtrlPassObj.getUnlockFunc());
   iface->addHeader(headerPtr);
   iface->unlockControlContainer();

   /**insert the original entries**/
   {
      auto tx = iface->beginWriteTransaction(walletPtr->dbName_);

      if (pubRoot != nullptr) {
         //root asset
         BinaryWriter bwKey;
         bwKey.put_uint32_t(ROOTASSET_KEY);

         auto data = pubRoot->serialize();
         tx->insert(bwKey.getData(), data);
      }

      {
         //comment account
         walletPtr->addMetaAccount(
            MetaAccountType::Comments);
      }

      if (!params.label.empty()) {
         walletPtr->setLabel(params.label);
      }
      if (!params.description.empty()) {
         walletPtr->setDescription(params.description);
      }
   }

   //init walletptr from file
   if (params.progressFunc) {
      auto prg = std::make_unique<Progress::ReadWalletFile>(masterID);
      params.progressFunc(std::move(prg));
   }
   walletPtr->readFromFile();
   return walletPtr;
}

//////////////// -- decrypt private key methods -- /////////////////////////////
const SecureBinaryData& AssetWallet_Single::getDecryptedValue(
   std::shared_ptr<Encryption::EncryptedAssetData> assetPtr)
{
   //have to lock the decryptedData object before calling this method
   return decryptedData_->getClearTextAssetData(assetPtr);
}

////////
const SecureBinaryData& AssetWallet_Single::getDecryptedPrivateKeyForAsset(
   std::shared_ptr<AssetEntry_Single> assetPtr)
{
   auto assetPrivKey = assetPtr->getPrivKey();
   if (assetPrivKey == nullptr) {
      auto account = getAccountForID(assetPtr->getID().getAddressAccountId());
      assetPrivKey = account->fillPrivateKey(
         iface_, decryptedData_, assetPtr->getID());
   }
   return getDecryptedValue(assetPrivKey);
}

////////
const SecureBinaryData& AssetWallet_Single::getDecryptedPrivateKeyForId(
   const AssetId& id) const
{
   return decryptedData_->getClearTextAssetData(id);
}

////////
std::shared_ptr<EncryptedSeed> AssetWallet_Single::getEncryptedSeed() const
{
   return seed_;
}

////////////////////////////////////////////////////////////////////////////////
const AssetId& AssetWallet_Single::derivePrivKeyFromPath(
   const BIP32_AssetPath& path)
{
   auto derPath = path.getDerivationPathFromSeed();

   //grab wallet root
   auto rootBip32 = std::dynamic_pointer_cast<AssetEntry_BIP32Root>(root_);
   if (rootBip32 == nullptr) {
      throw std::runtime_error("missing root");
   }

   //check fingerprint
   auto rootFingerprint = rootBip32->getThisFingerprint();
   if (path.getSeedFingerprint() != rootFingerprint) {
      throw std::runtime_error("root mismatch");
   }

   //decrypt root
   auto privKey = decryptedData_->getClearTextAssetData(
      rootBip32->getPrivKey());
   auto chaincode = rootBip32->getChaincode();

   //derive
   auto hdNode = BIP32_Node::getHDNodeFromPrivateKey(
      0, 0, 0, privKey, chaincode);

   for (unsigned i=0; i<derPath.size(); i++) {
      if (!btc_hdnode_private_ckd(&hdNode, derPath[i])) {
         throw std::runtime_error("failed to derive bip32 private key");
      }
   }

   //add to decrypted data container and return id
   return decryptedData_->insertClearTextAssetData(
      hdNode.private_key, BTC_ECKEY_PKEY_LENGTH);
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet_Single::changePrivateKeyPassphrase(
   Passphrase::SetNew& newPassObj)
{
   auto masterKeyId = decryptedData_->getMasterEncryptionKeyId();
   decryptedData_->encryptEncryptionKey(masterKeyId, newPassObj, true);
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet_Single::addPrivateKeyPassphrase(
   Passphrase::SetNew& newPassObj)
{
   if (root_ == nullptr || !root_->hasPrivateKey()) {
      throw WalletException("wallet has no private root");
   }
   auto masterKeyId = decryptedData_->getMasterEncryptionKeyId();
   decryptedData_->encryptEncryptionKey(masterKeyId, newPassObj, false);
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet_Single::erasePrivateKeyPassphrase()
{
   if (root_ == nullptr || !root_->hasPrivateKey()) {
      throw WalletException("wallet has no private root");
   }

   auto masterKeyId = root_->getPrivateEncryptionKeyId();
   auto masterKdfId = root_->getKdfId();
   decryptedData_->eraseEncryptionKey(masterKeyId, masterKdfId);
}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& AssetWallet_Single::getPublicRoot() const
{
   if (root_ == nullptr) {
      throw WalletException("null root");
   }
   auto pubkey = root_->getPubKey();
   if (pubkey == nullptr) {
      throw WalletException("null pubkey");
   }
   return pubkey->getUncompressedKey();
}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& AssetWallet_Single::getArmory135Chaincode() const
{
   if (root_ == nullptr) {
      throw WalletException("[getArmory135Chaincode] null root");
   }
   auto root135 = std::dynamic_pointer_cast<AssetEntry_ArmoryLegacyRoot>(root_);
   if (root135 == nullptr) {
      throw WalletException("[getArmory135Chaincode] unexpected root type");
   }
   return root135->getChaincode();
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet_Single::importPublicData(const WalletPublicData& wpd,
   std::shared_ptr<IO::WalletDBInterface> iface, Progress::Func prog)
{
   //open the wallet
   auto headerPtr = std::make_shared<IO::WalletHeader_Single>(
      Armory::Config::BitcoinSettings::getMagicBytes());
   headerPtr->walletID_ = wpd.walletID;
   auto wltRecipient = std::make_unique<AssetWallet_Single>(
      iface, headerPtr, wpd.masterID);
   wltRecipient->readFromFile();

   //open the relevant db name
   auto tx = iface->beginWriteTransaction(wpd.dbName);

   if (wpd.mainAccountID.isValid() &&
      !wltRecipient->mainAccountId_.isValid()) {
      //main account
      BinaryWriter bwKey;
      bwKey.put_uint32_t(MAIN_ACCOUNT_KEY);

      BinaryWriter bwData;
      wpd.mainAccountID.serializeValue(bwData);
      tx->insert(bwKey.getData(), bwData.getData());
   }

   //does the wallet have a root entry?
   if (wpd.pubRoot != nullptr && wltRecipient->getRoot() == nullptr) {
      //wallet is missing a root, add it
      BinaryWriter bwKey;
      bwKey.put_uint32_t(ROOTASSET_KEY);

      auto data = wpd.pubRoot->serialize();
      tx->insert(bwKey.getData(), data);
      wltRecipient->root_ = wpd.pubRoot;
   }

   //label & description
   wltRecipient->setLabel(wpd.label);
   wltRecipient->setDescription(wpd.description);

   //report how many addresses we will be generating
   if (prog) {
      int32_t lookupAggregate = 0;
      for (const auto& addrAccPair : wpd.accounts) {
         try {
            auto addrAccPtr = wltRecipient->getAccountForID(addrAccPair.first);
            for (const auto& accPair : addrAccPair.second.accountDataMap_) {
               auto accPtr = addrAccPtr->getAccountForID(accPair.first);
               auto delta = accPair.second.lastComputedIndex_ -
                  accPtr->getLastComputedIndex();
               if (delta <= 0) {
                  continue;
               }
               lookupAggregate += delta;
            }
         } catch (const WalletException&) {
            for (const auto& accPair : addrAccPair.second.accountDataMap_) {
               lookupAggregate += accPair.second.lastComputedIndex_ + 1;
            }
         }
      }
      prog(std::make_unique<Progress::ExtendChain>(lookupAggregate));
   }

   //address accounts
   for (const auto& accPair : wpd.accounts) {
      std::shared_ptr<AddressAccount> accPtr;
      const auto& accData = accPair.second;

      try {
         accPtr = wltRecipient->getAccountForID(accPair.first);
      } catch (const WalletException&) {
         /* recipient wallet does not have this account, create it */

         //guess address account type
         auto outerAccIter = accData.accountDataMap_.find(accData.outerAccountId_);
         if (outerAccIter == accData.accountDataMap_.end()) {
            throw WalletException("[importPublicData] "
               "Address account data missing outer account");
         }

         //reconstruct derivation scheme object
         auto derData = DBUtils::getDataRefForPacket(
            outerAccIter->second.derivationData_);
         auto derScheme = DerivationScheme::deserialize(derData);

         //instantiate account type object
         std::shared_ptr<AccountType> accTypePtr;
         switch (derScheme->getType())
         {
            case DerivationSchemeType::ArmoryLegacy:
            {
               if (accData.accountDataMap_.size() != 1) {
                  throw WalletException("[importPublicData]"
                     " invalid account data map size");
               }
               accTypePtr = std::make_shared<AccountType_ArmoryLegacy>();
               break;
            }

            case DerivationSchemeType::BIP32:
            case DerivationSchemeType::BIP32_Salted:
            {
               //create derTree
               auto rootBip32 = std::dynamic_pointer_cast<AssetEntry_BIP32Root>(
                  wpd.pubRoot);
               if (rootBip32 == nullptr) {
                  throw WalletException("[importPublicData] invalid root");
               }

               //grab the path for each asset account
               std::vector<PathAndRoot> pathsAndRoots;
               for (const auto& acc : accData.accountDataMap_) {
                  //deser the root
                  auto accRootData = DBUtils::getDataRefForPacket(
                     acc.second.rootData_);
                  auto accRoot = AssetEntry::deserDBValue(
                     AssetId::getRootAssetId(), accRootData);
                  auto accRootBip32 =
                     std::dynamic_pointer_cast<AssetEntry_BIP32Root>(accRoot);
                  if (accRootBip32 == nullptr) {
                     throw WalletException("[importPublicData] "
                        "unexpected account root type");
                  }

                  //get der path from the root
                  pathsAndRoots.emplace_back(
                     accRootBip32->getDerivationPath(),
                     accRootBip32->getXPub()
                  );
               }

               //create account type object from paths
               std::vector<std::vector<uint32_t>> paths;
               for (auto& pathAndRootIt : pathsAndRoots) {
                  paths.emplace_back(pathAndRootIt.getPath());
               }

               std::shared_ptr<AccountType_BIP32> accTypeBip32;
               if (derScheme->getType() == DerivationSchemeType::BIP32) {
                  accTypeBip32 = AccountType_BIP32::makeFromDerPaths(
                     rootBip32->getSeedFingerprint(true), paths);
               } else if (derScheme->getType() == DerivationSchemeType::BIP32_Salted) {
                  auto derSchemeSalted =
                     std::dynamic_pointer_cast<DerivationScheme_BIP32_Salted>(derScheme);
                  if (derSchemeSalted == nullptr) {
                     throw WalletException("[importPublicData]"
                        " unexpected der scheme");
                  }

                  accTypeBip32 = AccountType_BIP32_Salted::makeFromDerPaths(
                     rootBip32->getSeedFingerprint(true), paths,
                     derSchemeSalted->getSalt());
               }

               //set the roots
               accTypeBip32->setRoots(pathsAndRoots);

               //address types
               for (auto& addrType : accData.addressTypes_) {
                  accTypeBip32->addAddressType(addrType);
               }
               accTypeBip32->setDefaultAddressType(accData.defaultAddressEntryType_);

               //account ids
               accTypeBip32->setOuterAccountID(
                  accData.outerAccountId_.getAssetAccountKey());
               accTypeBip32->setInnerAccountID(
                  accData.innerAccountId_.getAssetAccountKey());

               accTypePtr = accTypeBip32;
               break;
            }

            case DerivationSchemeType::ECDH:
            {
               if (accData.accountDataMap_.size() != 1) {
                  throw WalletException("[importPublicData]"
                     " invalid account data map size");
               }

               const auto& adm = accData.accountDataMap_.begin()->second;
               auto accRootData = DBUtils::getDataRefForPacket(adm.rootData_);
               auto accRoot = AssetEntry::deserDBValue(
                  AssetId::getRootAssetId(), accRootData);
               auto accRootEcdh =
                  std::dynamic_pointer_cast<AssetEntry_Single>(accRoot);
               if (accRootEcdh == nullptr) {
                  throw WalletException("[importPublicData] "
                     "unexpected account root type");
               }

               //address types
               auto accEcdh = std::make_shared<AccountType_ECDH>(
                  SecureBinaryData(), accRootEcdh->getPubKey()->getCompressedKey());
               for (auto& addrType : accData.addressTypes_) {
                  accEcdh->addAddressType(addrType);
               }
               accEcdh->setDefaultAddressType(accData.defaultAddressEntryType_);
               accTypePtr = accEcdh;
               break;
            }

            default:
               break;
         }

         if (accTypePtr == nullptr) {
            throw WalletException("[importPublicData] "
               "Failed to resolve address account type");
         }

         //flag main account
         if (accData.ID_ == wpd.mainAccountID) {
            accTypePtr->setMain(true);
         }

         //create the account
         accPtr = wltRecipient->createAccount(accTypePtr, nullptr);
      }

      //check the account matches the public data we're importing from
      if (accPtr->addressTypes_ != accData.addressTypes_ ||
         accPtr->defaultAddressEntryType_ != accData.defaultAddressEntryType_) {
         throw WalletException("[importPublicData] Address type mismtach");
      }

      if (accPtr->accountDataMap_.size() != accData.accountDataMap_.size()) {
         throw WalletException("[importPublicData] Account map mismatch");
      }
      auto newAccDataIter = accPtr->accountDataMap_.begin();
      auto accDataIter = accData.accountDataMap_.begin();
      while (newAccDataIter != accPtr->accountDataMap_.end()) {
         if (newAccDataIter->first != accDataIter->first) {
            throw WalletException("[importPublicData] Account map mismatch");
         }
         ++newAccDataIter;
         ++accDataIter;
      }

      if (accPtr->outerAccountId_ != accData.outerAccountId_ ||
         accPtr->innerAccountId_ != accData.innerAccountId_) {
         throw WalletException("[importPublicData] "
            "Mismtach in outer/inner accounts");
      }

      //synchronize the account
      accPtr->importPublicData(accData);

      //commit to disk
      accPtr->commit(iface);
   }

   //meta accounts
   for (const auto& metaAccPtr : wpd.metaAccounts) {
      auto accCopy = metaAccPtr.second->copy(wpd.dbName);
      auto metaTx = iface->beginWriteTransaction(wpd.dbName);
      accCopy->commit(std::move(metaTx));
   }
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet_Single::mergePublicData(const IO::ReadOnlyFileParams& params,
   const WalletPublicData& wpd, Progress::Func prog)
{
   auto iface = std::make_shared<IO::WalletDBInterface>();
   iface->setupEnv(params);
   importPublicData(wpd, iface, prog);
}

////////////////////////////////////////////////////////////////////////////////
WalletPublicData AssetWallet_Single::exportPublicData(
   std::shared_ptr<AssetWallet_Single> wlt)
{
   WalletPublicData wpd{
      wlt->getDbFilename(),
      wlt->dbName_,
      wlt->masterID_,
      wlt->walletID_,
      wlt->mainAccountId_
   };

   //root
   if (wlt->root_ != nullptr) {
      wpd.pubRoot = wlt->root_->getPublicCopy();
   }

   //address accounts
   for (auto& addrAccPtr : wlt->accounts_) {
      auto accData = addrAccPtr.second->exportPublicData();
      wpd.accounts.emplace(accData.ID_, accData);
   }

   //meta accounts
   for (auto& metaAccPtr : wlt->metaDataAccounts_) {
      auto accCopy = metaAccPtr.second->copy(wlt->dbName_);
      wpd.metaAccounts.emplace(accCopy->getType(), accCopy);
   }

   //label and description
   wpd.label = wlt->label_;
   wpd.description = wlt->description_;

   return wpd;
}

////////////////////////////////////////////////////////////////////////////////
bool AssetWallet_Single::isWatchingOnly() const
{
   if (root_ == nullptr) {
      return true;
   }
   return !root_->hasPrivateKey();
}

////////////////////////////////////////////////////////////////////////////////
BIP32_AssetPath AssetWallet_Single::getBip32PathForAssetID(
   const AssetId& id) const
{
   auto asset = getAssetForID(id);
   return getBip32PathForAsset(asset);
}

////////////////////////////////////////////////////////////////////////////////
BIP32_AssetPath AssetWallet_Single::getBip32PathForAsset(
   std::shared_ptr<AssetEntry> asset) const
{
   const auto& id = asset->getID();
   if (!id.isValid()) {
      throw WalletException("invalid asset id");
   }

   auto assetSingle = std::dynamic_pointer_cast<AssetEntry_Single>(asset);
   if (assetSingle == nullptr) {
      throw WalletException("unexpected asset type");
   }

   auto pubKeyPtr = assetSingle->getPubKey();
   if (pubKeyPtr == nullptr) {
      throw WalletException("asset is missing public key");
   }

   const auto& pubkey = pubKeyPtr->getCompressedKey();
   auto account = getAccountForID(id.getAddressAccountId());
   auto accountRoot = account->getBip32RootForAssetId(id);
   auto accountPath = accountRoot->getDerivationPath();

   //get root
   auto rootBip32 = std::dynamic_pointer_cast<AssetEntry_BIP32Root>(root_);
   if (rootBip32 == nullptr) {
      /* 
      Wallet has no root, we have to use the account's root instead. It should
      carry the path from its seed as well as the seed's fingerprint
      */

      auto rootObj = std::make_shared<BIP32_PublicDerivedRoot>(
         accountRoot->getXPub(),
         accountPath,
         accountRoot->getSeedFingerprint(true));

      return BIP32_AssetPath(
         pubkey,
         { (uint32_t)id.getAssetKey() },
         accountRoot->getThisFingerprint(),
         rootObj);
   } else {
      //wallet has a root, build path from that
      auto rootPath = accountRoot->getDerivationPath();
      rootPath.push_back(id.getAssetKey());

      return BIP32_AssetPath(
         pubkey,
         rootPath,
         rootBip32->getThisFingerprint(),
         nullptr);
   }
}

////////////////////////////////////////////////////////////////////////////////
std::string AssetWallet_Single::getXpubForAssetID(const AssetId& id) const
{
   if (!id.isValid()) {
      throw WalletException("invalid asset id");
   }

   //grab account
   auto addrAccount = getAccountForID(id.getAddressAccountId());
   auto accountPtr = addrAccount->getAccountForID(id);

   //setup bip32 node from root pubkey
   auto root = std::dynamic_pointer_cast<AssetEntry_BIP32Root>(
      accountPtr->getRoot());
   if (root == nullptr) {
      throw WalletException("unexpected type for account root");
   }

   BIP32_Node node;
   node.initFromPublicKey(
      root->getDepth(), root->getLeafID(), root->getParentFingerprint(),
      root->getPubKey()->getCompressedKey(), root->getChaincode());

   //derive with asset's step
   node.derivePublic(id.getAssetKey());

   auto b58sbd = node.getBase58();
   return std::string{b58sbd.getCharPtr(), b58sbd.getSize()};
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AccountType_BIP32> AssetWallet_Single::makeNewBip32AccTypeObject(
   const std::vector<uint32_t>& derPath) const
{
   auto rootBip32 = std::dynamic_pointer_cast<AssetEntry_BIP32Root>(root_);
   if (rootBip32 == nullptr) {
      throw WalletException("[makeNewBip32AccTypeObject] unexpected root ptr");
   }
   auto seedFingerprint = rootBip32->getSeedFingerprint(true);
   return AccountType_BIP32::makeFromDerPaths(seedFingerprint, {derPath});
}

////////////////////////////////////////////////////////////////////////////////
const AddressAccountId& AssetWallet_Single::setupImportAccount()
{
   ReentrantLock lock(this);

   auto accTypePtr = std::make_shared<AccountType_Imports>(!isWatchingOnly());
   if (!mainAccountId_.isValid()) {
      accTypePtr->setMain(true);
   }
   auto accountPtr = createAccount(accTypePtr, nullptr);
   return accountPtr->getID();
}

////////////////////////////////////////////////////////////////////////////////
AssetId AssetWallet_Single::importPublicKey(SecureBinaryData& pubkey,
   AddressEntryType aeType)
{
   //lock
   ReentrantLock lock(this);

   //grab WO import account
   auto addrAcc = getAccountForID({IMPORTS_ACCOUNT_PUB});
   auto assetAcc = addrAcc->getOuterAccount();
   auto importAcc = dynamic_cast<AssetAccount_ImportsWO*>(assetAcc.get());
   if (importAcc == nullptr) {
      throw WalletException("invalid WO import account");
   }

   auto tx = iface_->beginWriteTransaction(dbName_);
   auto assetId = importAcc->importPublicKey(iface_, pubkey);
   addrAcc->updateInstantiatedAddressType(iface_, assetId, aeType);
   return assetId;
}

AssetId AssetWallet_Single::importScrAddr(const BinaryData& scrAddr)
{
   //need to determine scrAddr type & extract the hash
   AddressEntryType aeType;
   BinaryData scriptHash;
   switch (BtcUtils::getScriptTypeForScrAddr(scrAddr))
   {
      case TxOutScriptType::STDHASH160:
         aeType = AddressEntryType::P2PKH;
         scriptHash = scrAddr.getSliceCopy(1, 20);
         break;

      case TxOutScriptType::P2WPKH:
         aeType = AddressEntryType::P2WPKH;
         scriptHash = scrAddr.getSliceCopy(1, 20);
         break;

      case TxOutScriptType::P2SH:
         aeType = AddressEntryType(
            AddressEntryType::P2SH | AddressEntryType::ScriptHash);
         scriptHash = scrAddr.getSliceCopy(1, 20);
         break;

      case TxOutScriptType::P2WSH:
         aeType = AddressEntryType(
            AddressEntryType::P2WSH | AddressEntryType::ScriptHash);
         scriptHash = scrAddr.getSliceCopy(1, 32);
         break;

      default:
         throw AddressException("unsupported scrAddr format");
   }

   //lock
   ReentrantLock lock(this);

   //grab WO import account
   auto addrAcc = getAccountForID({IMPORTS_ACCOUNT_PUB});
   auto assetAcc = addrAcc->getOuterAccount();
   auto importAcc = dynamic_cast<AssetAccount_ImportsWO*>(assetAcc.get());
   if (importAcc == nullptr) {
      throw WalletException("invalid WO import account");
   }

   auto tx = iface_->beginWriteTransaction(dbName_);
   auto assetId = importAcc->importScriptHash(iface_, scriptHash);
   addrAcc->updateInstantiatedAddressType(iface_, assetId, aeType);
   return assetId;
}

AssetId AssetWallet_Single::importRawScript(const BinaryData& script)
{
   //lock
   ReentrantLock lock(this);

   //grab WO import account
   auto addrAcc = getAccountForID({IMPORTS_ACCOUNT_PUB});
   auto assetAcc = addrAcc->getOuterAccount();
   auto importAcc = dynamic_cast<AssetAccount_ImportsWO*>(assetAcc.get());
   if (importAcc == nullptr) {
      throw WalletException("invalid WO import account");
   }

   auto tx = iface_->beginWriteTransaction(dbName_);
   auto assetId = importAcc->importRawScript(iface_, script);
   addrAcc->updateInstantiatedAddressType(
      iface_, assetId, AddressEntryType::RawScript);
   return assetId;
}

////////
AssetId AssetWallet_Single::importPrivateKey(SecureBinaryData& privkey,
   AddressEntryType aeType)
{
   //lock
   ReentrantLock lock(this);

   //grab import account
   auto addrAcc = getAccountForID({IMPORTS_ACCOUNT_PRIV});
   auto assetAcc = addrAcc->getOuterAccount();
   auto importAcc = dynamic_cast<AssetAccount_Imports*>(assetAcc.get());
   if (importAcc == nullptr) {
      throw WalletException("invalid WO import account");
   }

   //grab root, we need its cipher to encrypt the new key
   if (root_ == nullptr) {
      throw WalletException("full wallet has no root!");
   }

   auto rootEntry = std::dynamic_pointer_cast<AssetEntry_Single>(root_);
   if (rootEntry == nullptr || !rootEntry->hasPrivateKey()) {
      throw WalletException("need root cipher to encrypt imported");
   }

   auto* cipherPtr = rootEntry->getPrivKey()->getCipherDataPtr();
   if (cipherPtr == nullptr) {
      throw WalletException("root has no cipher");
   }
   auto cipherCopy = cipherPtr->cipher->getCopy();

   //import private key and set address type for its asset id
   auto ddc = lockDecryptedContainer();
   auto tx = iface_->beginWriteTransaction(dbName_);
   auto assetId = importAcc->importPrivateKey(iface_, decryptedData_,
      privkey, std::move(cipherCopy));
   addrAcc->updateInstantiatedAddressType(iface_, assetId, aeType);
   return assetId;
}

////////////////////////////////////////////////////////////////////////////////
std::filesystem::path AssetWallet_Single::forkWatchingOnly(
   const WalletPublicData& wpd,
   const Passphrase::SetNew& woCtrlPassObj)
{
   //strip '_wallet' extention
   auto filename = wpd.path.stem().string();
   auto underscoreIndex = filename.find_last_of("_");
   auto newname = filename.substr(0, underscoreIndex);

   //set WO suffix
   newname.append("_WatchingOnly.lmdb");
   auto newPath = wpd.path.parent_path() / newname;

   //check file does not exist
   if (FileUtils::fileExists(newPath, 0)) {
      throw WalletException("WO wallet filename already exists");
   }

   //init wallet db interface
   auto woIface = createIface(IO::CreateFileParams{newPath, woCtrlPassObj});
   woIface->lockControlContainer(woCtrlPassObj.getUnlockFunc());

   {
      //setup walletId header
      auto headerPtr = std::make_shared<IO::WalletHeader_Single>(
         Armory::Config::BitcoinSettings::getMagicBytes());
      headerPtr->walletID_ = wpd.walletID;
      Armory::Passphrase::Params params{};
      IO::WalletDBInterface::initWalletHeaderObject(headerPtr, params);
      woIface->addHeader(headerPtr);
   }

   //import public data into the WO wallet
   AssetWallet_Single::importPublicData(wpd, woIface);
   setMainWallet(woIface, wpd.walletID);

   //close dbs
   woIface->unlockControlContainer();
   woIface.reset();

   //return the file name of the wo wallet
   return newPath;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// AssetWallet_Multisig
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
AssetWallet_Multisig::AssetWallet_Multisig(
   std::shared_ptr<IO::WalletDBInterface> iface,
   std::shared_ptr<IO::WalletHeader> metaPtr, const WalletId& masterID) :
   AssetWallet(iface, metaPtr, masterID)
{
   if (metaPtr == nullptr ||
      metaPtr->magicBytes_ != Armory::Config::BitcoinSettings::getMagicBytes()) {
      throw WalletException(
         "[AssetWallet_Multisig] network magic bytes mismatch");
   }
}

////////////////////////////////////////////////////////////////////////////////
void AssetWallet_Multisig::readFromFile()
{
   //sanity check
   if (iface_ == nullptr) {
      throw WalletException("uninitialized wallet object");
   }

   {
      auto tx = iface_->beginReadTransaction(dbName_);

      //walletId
      BinaryWriter keyId;
      keyId.put_uint32_t(WALLETID_KEY);
      auto walletIdRef = getDataRefForKey(tx.get(), keyId.getData());
      walletID_ = WalletId{walletIdRef.toCharPtr(), walletIdRef.getSize()};

      //lookup
      BinaryWriter keyLookup;
      keyLookup.put_uint8_t(ASSETENTRY_PREFIX);
      auto lookupRef = getDataRefForKey(tx.get(), keyLookup.getData());

      BinaryRefReader brr(lookupRef);
      chainLength_ = brr.get_uint32_t();
   }

   unsigned n = 0;
   std::map<std::string, std::shared_ptr<AssetWallet_Single>> walletPtrs;
   for (unsigned i = 0; i < n; i++) {
      std::string strId{"Subwallet-" + std::to_string(i)};

      auto subWltMeta = std::make_shared<IO::WalletHeader_Subwallet>();
      subWltMeta->walletID_ = WalletId{std::string_view{strId}};

      auto subwalletPtr = std::make_shared<AssetWallet_Single>(
         iface_, subWltMeta, masterID_);
      subwalletPtr->readFromFile();
      walletPtrs[subwalletPtr->getID()] = subwalletPtr;
   }

   loadMetaAccounts();
}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& AssetWallet_Multisig::getDecryptedValue(
   std::shared_ptr<Encryption::EncryptedAssetData> assetPtr)
{
   return decryptedData_->getClearTextAssetData(assetPtr);
}

////////////////////////////////////////////////////////////////////////////////
//
//// WalletPublicData
//
////////////////////////////////////////////////////////////////////////////////
WalletPublicData::WalletPublicData(const std::filesystem::path& path,
   const std::string& dbName,
   const WalletId& masterID, const WalletId& walletID,
   const AddressAccountId& mainAccID) :
   path(path), dbName(dbName), masterID(masterID), walletID(walletID),
   mainAccountID(mainAccID)
{}
