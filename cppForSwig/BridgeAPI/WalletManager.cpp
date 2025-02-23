////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2024, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <filesystem>
#include <string_view>

#include "WalletManager.h"
#include "Wallets/Seeds/Backups.h"
#include "PassphrasePrompt.h"
#include "../Wallets/Seeds/Seeds.h"

using namespace Armory;
using namespace std::string_view_literals;

#define WALLET_135_HEADER "\xbaWALLET\x00"
#define PYBTC_ADDRESS_SIZE 237

////////////////////////////////////////////////////////////////////////////////
////
//// WalletManager
////
////////////////////////////////////////////////////////////////////////////////
WalletManager::WalletManager(const std::filesystem::path& path,
   const PassphraseLambda& passLbd) :
   path_(path)
{
   loadWallets(passLbd);
}

////
bool WalletManager::hasWallet(const std::string& id)
{
   std::unique_lock<std::mutex> lock(mu_);
   auto wltIter = wallets_.find(id);
   return wltIter != wallets_.end();
}

////////////////////////////////////////////////////////////////////////////////
std::map<std::string, std::set<Wallets::AddressAccountId>>
WalletManager::getAccountIdMap() const
{
   std::map<std::string, std::set<Wallets::AddressAccountId>> result;
   for (const auto& wltIt : wallets_) {
      auto wltIter = result.emplace(
         wltIt.first, std::set<Wallets::AddressAccountId>{});
      for (const auto& accIt : wltIt.second) {
         wltIter.first->second.emplace(accIt.first);
      }
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<WalletContainer> WalletManager::getWalletContainer(
   const std::string& wltId) const
{
   auto iter = wallets_.find(wltId);
   if (iter == wallets_.end()) {
      std::string errStr{"no wallet for id "sv};
      errStr += wltId;
      throw std::runtime_error(errStr);
   }
   return iter->second.begin()->second;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<WalletContainer> WalletManager::getWalletContainer(
   const std::string& wltId, const Wallets::AddressAccountId& accId) const
{
   auto wltIter = wallets_.find(wltId);
   if (wltIter == wallets_.end()) {
      std::string errStr{"i do not know wallet "sv};
      errStr += wltId;
      throw std::runtime_error(errStr);
   }

   auto accIter = wltIter->second.find(accId);
   if (accIter == wltIter->second.end()) {
      std::string errStr{"there is no account "sv};
      errStr += accId.toHexStr() + std::string{" for wallet "sv} + wltId;
      throw std::runtime_error(errStr);
   }

   return accIter->second;
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::setBdvPtr(
   std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr)
{
   bdvPtr_ = bdvPtr;
   for (auto& wltIt : wallets_) {
      for (auto& accIt : wltIt.second) {
         accIt.second->setBdvPtr(bdvPtr);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::registerWallets()
{
   for (const auto& wltIt : wallets_) {
      for (const auto& accIt : wltIt.second) {
         accIt.second->registerWithBDV(false);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
const std::string& WalletManager::registerWallet(const std::string& wltId,
   const Wallets::AddressAccountId& accId, bool isNew)
{
   auto wltIter = wallets_.find(wltId);
   if (wltIter == wallets_.end()) {
      throw std::runtime_error("[WalletManager::registerWallet]");
   }

   auto accIter = wltIter->second.find(accId);
   if (accIter == wltIter->second.end()) {
      throw std::runtime_error("[WalletManager::registerWallet]");
   }

   accIter->second->registerWithBDV(isNew);
   return accIter->second->getDbId();
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<WalletContainer> WalletManager::addWallet(
   std::shared_ptr<Wallets::AssetWallet> wltPtr,
   const Wallets::AddressAccountId& accId)
{
   ReentrantLock lock(this);

   //check we dont have this wallet
   auto wltIter = wallets_.find(wltPtr->getID());
   if (wltIter == wallets_.end()) {
      wltIter = wallets_.emplace(wltPtr->getID(),
         std::map<Wallets::AddressAccountId, std::shared_ptr<WalletContainer>>{}).first;
   }

   auto accIter = wltIter->second.find(accId);
   if (accIter != wltIter->second.end()) {
      return accIter->second;
   }

   //create wrapper object
   auto wltContPtr = new WalletContainer(wltPtr->getID(), accId);
   std::shared_ptr<WalletContainer> wltCont;
   wltCont.reset(wltContPtr);

   //set bdvPtr if we have it
   if (bdvPtr_ != nullptr) {
      wltCont->setBdvPtr(bdvPtr_);
   }

   //set & add to map
   wltCont->setWalletPtr(wltPtr, accId);
   wltIter->second.emplace(accId, wltCont);
   walletsByDbId_.emplace(wltCont->getDbId(), wltCont);

   //return it
   return wltCont;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<WalletContainer> WalletManager::createNewWallet(
   const SecureBinaryData& pass, const SecureBinaryData& controlPass,
   const SecureBinaryData& extraEntropy, unsigned lookup)
{
   auto root = CryptoPRNG::generateRandom(32);
   if (extraEntropy.getSize() >= 32) {
      root.XOR(extraEntropy);
   }

   auto seed = std::make_unique<Seeds::ClearTextSeed_Armory135>(
      root, Seeds::ClearTextSeed_Armory135::LegacyType::Armory200);
   auto wallet = Wallets::AssetWallet_Single::createFromSeed(
      std::move(seed),
      Wallets::WalletCreationParams{
         pass, controlPass, path_, lookup
      });

   return addWallet(wallet, wallet->getMainAccountID());
}

////////////////////////////////////////////////////////////////////////////////
std::filesystem::path WalletManager::unloadWallet(const std::string& wltId)
{
   ReentrantLock lock(this);
   auto iter = wallets_.find(wltId);
   if (iter == wallets_.end()) {
      return {};
   }

   //unregister all accounts
   std::filesystem::path path;
   for (auto& acc : iter->second) {
      try {
         if (path.empty()) {
            path = acc.second->getWalletPtr()->getDbFilename();
         }
         acc.second->unregisterFromBDV();
      } catch (const std::exception&) {
         //we do not care if the unregister operation fails
      }
   }

   //remove containers from map, this should unload the underlying AssetWallet
   wallets_.erase(wltId);
   return path;
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::deleteWallet(const std::string& wltId)
{
   ReentrantLock lock(this);
   auto wltCont = getWalletContainer(wltId);
   wallets_.erase(wltId);

   //delete from disk
   wltCont->eraseFromDisk();
   try {
      //unregister from db
      wltCont->unregisterFromBDV();
   } catch (const std::exception&) {
      //we do not care if the unregister operation fails
   }
   wltCont.reset();
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::loadWallet(const std::filesystem::path& path,
   const PassphraseLambda& passLbd)
{
   try {
      auto wltPtr = Wallets::AssetWallet::loadMainWalletFromFile(path, passLbd);
      const auto& accIds = wltPtr->getAccountIDs();
      for (const auto& accId : accIds) {
         addWallet(wltPtr, accId);
      }
   } catch (const std::exception& e) {
      LOGERR << "Failed to open wallet at " << path.string() <<
         " with error:\n" << e.what();
   }
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::loadWallets(const PassphraseLambda& passLbd)
{
   //list .lmdb files in folder
   if (!FileUtils::isDir(path_)) {
      std::stringstream ss;
      ss << path_ << "is not a valid datadir";
      LOGERR << ss.str();
      throw std::runtime_error(ss.str());
   }

   std::vector<std::filesystem::path> walletPaths;
   std::vector<std::filesystem::path> a135Paths;
   for (const auto& dirEntry : std::filesystem::directory_iterator{path_} ) {
      const auto& path = dirEntry.path();
      const auto& extension = path.extension();
      if (extension == ".lmdb") {
         walletPaths.emplace_back(path);
      } else if (extension == ".wallet") {
         a135Paths.emplace_back(path);
      }
   }

   ReentrantLock lock(this);

   //read the files
   for (const auto& wltPath : walletPaths) {
      loadWallet(wltPath, passLbd);
   }

   //parse the potential armory 1.35 wallet files
   for (const auto& wltPath : a135Paths) {
      Armory135Header a135(wltPath);
      if (!a135.isInitialized()) {
         continue;
      }

      //an armory v1.35 wallet was loaded, check if we need to
      //migrate it to v3.x
      auto& id = a135.getID();
      auto iter = wallets_.find(id);
      if (iter != wallets_.end()) {
         continue;
      }

      //no equivalent v3.x wallet loaded, let's migrate it
      auto wltPtr = a135.migrate(passLbd);
      addWallet(wltPtr, wltPtr->getMainAccountID());
   }
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::updateStateFromDB(const std::function<void(void)>& callback)
{
   auto lbd = [this, callback](void)->void
   {
      ReentrantLock lock(this);

      //grab wallet balances
      auto promBal = std::make_shared<std::promise<std::map<
         std::string, AsyncClient::CombinedBalances>>>();
      auto futBal = promBal->get_future();
      auto lbdBal = [promBal]
         (ReturnMessage<std::map<std::string, AsyncClient::CombinedBalances>> result)->void
      {
         promBal->set_value(result.get());
      };
      bdvPtr_->getCombinedBalances(lbdBal);
      auto balances = std::move(futBal.get());

      //update wallet balances
      for (const auto& wltBalance : balances) {
         auto wltContIter = walletsByDbId_.find(wltBalance.first);
         if (wltContIter == walletsByDbId_.end()) {
            continue;
         }
         wltContIter->second->updateWalletBalanceState(wltBalance.second);
         wltContIter->second->updateAddressCountState(wltBalance.second);
      }

      //fire the lambda
      callback();
   };

   std::thread thr(lbd);
   if (thr.joinable()) {
      thr.detach();
   }
}

////////////////////////////////////////////////////////////////////////////////
////
//// WalletContainer
////
////////////////////////////////////////////////////////////////////////////////
WalletContainer::WalletContainer(const std::string& wltId,
   const Armory::Wallets::AddressAccountId& accId) :
   wltId_(wltId), accountId_(accId)
{
   dbId_ = BtcUtils::fortuna_.generateRandom(6).toHexStr();
}

////////////////////////////////////////////////////////////////////////////////
const std::string& WalletContainer::getDbId() const
{
   return dbId_;
}

////////////////////////////////////////////////////////////////////////////////
const Armory::Wallets::AddressAccountId& WalletContainer::getAccountId() const
{
   return accountId_;
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::setWalletPtr(std::shared_ptr<Wallets::AssetWallet> wltPtr,
   const Wallets::AddressAccountId& accId)
{
   wallet_ = wltPtr;
   auto acc = wallet_->getAccountForID(accId);
   auto assetAccountIds = acc->getAccountIdSet();

   for (const auto& aaId : assetAccountIds) {
      auto accPtr = acc->getAccountForID(aaId);
      if (accPtr == nullptr) {
         throw std::runtime_error("[setWalletPtr] missing asset account id");
      }
      highestUsedIndex_.emplace(aaId, accPtr->getHighestUsedIndex());
   }
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::setBdvPtr(std::shared_ptr<AsyncClient::BlockDataViewer> bdv)
{
   std::unique_lock<std::mutex> lock(stateMutex_);
   bdvPtr_ = bdv;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<Wallets::AssetWallet> WalletContainer::getWalletPtr() const
{
   return wallet_;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<Accounts::AddressAccount> WalletContainer::getAddressAccount() const
{
   auto accPtr = wallet_->getAccountForID(accountId_);
   return accPtr;
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::resetCache()
{
   std::unique_lock<std::mutex> lock(stateMutex_);

   totalBalance_ = 0;
   spendableBalance_ = 0;
   unconfirmedBalance_ = 0;
   balanceMap_.clear();
   countMap_.clear();
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::registerWithBDV(bool isNew)
{
   if (bdvPtr_ == nullptr) {
      throw std::runtime_error("bdvPtr is not set");
   }
   resetCache();

   auto accPtr = wallet_->getAccountForID(accountId_);
   const auto& addrMap = accPtr->getAddressHashMap();

   std::set<BinaryData> addrSet;
   for (const auto& addrIt : addrMap) {
      addrSet.emplace(addrIt.first);
   }

   //convert set to vector
   std::vector<BinaryData> addrVec;
   addrVec.insert(addrVec.end(), addrSet.begin(), addrSet.end());

   asyncWlt_ = std::make_shared<AsyncClient::BtcWallet>(
      bdvPtr_->getWalletObj(dbId_));
   asyncWlt_->registerAddresses(addrVec, isNew);
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::unregisterFromBDV()
{
   if (bdvPtr_ == nullptr) {
      throw std::runtime_error("bdvPtr is not set");
   }
   if (asyncWlt_ == nullptr) {
      throw std::runtime_error("asyncWlt is not set");
   }
   asyncWlt_->unregister();
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::updateWalletBalanceState(
   const AsyncClient::CombinedBalances& bal)
{
   std::unique_lock<std::mutex> lock(stateMutex_);

   totalBalance_        = bal.walletBalanceAndCount[0];
   spendableBalance_    = bal.walletBalanceAndCount[1];
   unconfirmedBalance_  = bal.walletBalanceAndCount[2];
   txioCount_           = bal.walletBalanceAndCount[3];

   for (const auto& addrPair : bal.addressBalances) {
      balanceMap_[addrPair.first] = addrPair.second;
   }
}

////////////////////////////////////////////////////////////////////////////////
Wallets::AssetKeyType WalletContainer::getHighestUsedIndex(void) const
{
   auto accPtr = wallet_->getAccountForID(accountId_);
   if (accPtr == nullptr) {
      throw std::runtime_error("[getHighestUsedIndex] invalid acc id");
   }

   auto outerAccId = accPtr->getOuterAccountID();
   if (!outerAccId.isValid()) {
      throw std::runtime_error("[getHighestUsedIndex] invalid outer acc id");
   }

   auto indexIter = highestUsedIndex_.find(outerAccId);
   if (indexIter == highestUsedIndex_.end()) {
      throw std::runtime_error("[getHighestUsedIndex] missing index for id");
   }

   return indexIter->second;
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::updateAddressCountState(
   const AsyncClient::CombinedBalances& cnt)
{
   std::unique_lock<std::mutex> lock(stateMutex_);

   std::map<Wallets::AssetAccountId, Wallets::AssetKeyType> topIndexMap;
   std::shared_ptr<Wallets::IO::WalletIfaceTransaction> dbtx;
   std::map<BinaryData, std::shared_ptr<AddressEntry>> updatedAddressMap;
   std::map<Wallets::AssetId, AddressEntryType> addrAndTypeMap;

   for (const auto& addrPair : cnt.addressBalances) {
      auto iter = countMap_.find(addrPair.first);
      if (iter != countMap_.end()) {
         //already tracking count for this address, just update the value
         iter->second = addrPair.second[3];
         continue;
      }

      const auto& ID = wallet_->getAssetIDForScrAddr(addrPair.first);
      auto topIdIter = topIndexMap.find(ID.first.getAssetAccountId());
      if (topIdIter == topIndexMap.end()) {
         topIdIter = topIndexMap.emplace(ID.first.getAssetAccountId(), -1).first;
      }

      //track top used index
      auto idKey = ID.first.getAssetKey();
      if (idKey > topIdIter->second) {
         topIdIter->second = idKey;
      }

      //mark newly seen addresses for further processing
      addrAndTypeMap.emplace(ID);

      //add count to map
      countMap_.emplace(addrPair.first, addrPair.second[3]);
   }

   std::map<Wallets::AssetId, AddressEntryType> unpulledAddresses;
   for (const auto& idPair : addrAndTypeMap) {
      //check scrAddr with on chain data matches scrAddr for
      //address entry in wallet
      if (!wallet_->isAssetUsed(idPair.first)) {
         //db has history for an address that hasn't been pulled
         //from the wallet yet, save it for further processing
         unpulledAddresses.insert(idPair);
         continue;
      }

      auto addrType = wallet_->getAddrTypeForID(idPair.first);
      if (addrType == idPair.second) {
         continue;
      }

      //if we don't have a db tx yet, get one, as we're about to update
      //the address type on disk
      if (dbtx == nullptr) {
         dbtx = wallet_->beginSubDBTransaction(wallet_->getID(), true);
      }

      //address type mismatches, update it
      wallet_->updateAddressEntryType(idPair.first, idPair.second);

      auto addrPtr = wallet_->getAddressEntryForID(idPair.first);
      updatedAddressMap.emplace(addrPtr->getPrefixedHash(), addrPtr);
   }

   //split unpulled addresses by their accounts
   std::map<Wallets::AssetAccountId,
      std::map<Wallets::AssetId, AddressEntryType>> accIDMap;
   for (const auto& idPair : unpulledAddresses) {
      auto accID = idPair.first.getAssetAccountId();
      auto iter = accIDMap.find(accID);
      if (iter == accIDMap.end()) {
         iter = accIDMap.emplace(accID,
            std::map<Wallets::AssetId, AddressEntryType>()).first;
      }
      iter->second.insert(idPair);
   }

   if (dbtx == nullptr) {
      dbtx = wallet_->beginSubDBTransaction(wallet_->getID(), true);
   }

   //run through each account, pulling addresses accordingly
   for (const auto& accData : accIDMap) {
      auto addrAccount = wallet_->getAccountForID(
         accData.first.getAddressAccountId());
      auto assAccount = addrAccount->getAccountForID(accData.first);

      auto currentTop = assAccount->getHighestUsedIndex();
      for (auto& idPair : accData.second) {
         const auto& assetKey = idPair.first.getAssetKey();
         while (assetKey > currentTop + 1) {
            auto addrEntry = wallet_->getNewAddress(
               accData.first, AddressEntryType_Default);
            updatedAddressMap.emplace(
               addrEntry->getPrefixedHash(), addrEntry);

            ++currentTop;
         }

         auto addrEntry = wallet_->getNewAddress(
            accData.first, idPair.second);
         updatedAddressMap.emplace(
            addrEntry->getPrefixedHash(), addrEntry);
         
         ++currentTop;
      }
   }

   for (const auto& topIndexIt : topIndexMap) {
      auto usedIndexIter = highestUsedIndex_.find(topIndexIt.first);
      if (usedIndexIter == highestUsedIndex_.end()) {
         LOGWARN << "[updateAddressCountState]" <<
            " missing asset account, skipping";
         continue;
      }

      usedIndexIter->second = std::max(
         topIndexIt.second,
         usedIndexIter->second);
   }

   for (const auto& addrPair : updatedAddressMap) {
      auto insertIter = updatedAddressMap_.insert(addrPair);
      if (!insertIter.second) {
         insertIter.first->second = addrPair.second;
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
std::map<BinaryData, std::vector<uint64_t>> WalletContainer::getAddrBalanceMap() const
{
   std::map<BinaryData, std::vector<uint64_t>> result;
   for (const auto& dataPair : countMap_) {
      std::vector<uint64_t> balVec;
      auto iter = balanceMap_.find(dataPair.first);
      if (iter == balanceMap_.end()) {
         balVec = {0, 0, 0};
      } else {
         balVec = iter->second;
      }

      balVec.emplace_back(dataPair.second);
      result.emplace(dataPair.first, balVec);
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::createAddressBook(const std::function<
   void(ReturnMessage<std::vector<AddressBookEntry>>)>& lbd)
{
   if (asyncWlt_ == nullptr) {
      throw std::runtime_error("empty asyncWlt");
   }
   asyncWlt_->createAddressBook(lbd);
}

////////////////////////////////////////////////////////////////////////////////
std::map<BinaryData, std::shared_ptr<AddressEntry>>
WalletContainer::getUpdatedAddressMap()
{
   auto mapMove = std::move(updatedAddressMap_);
   updatedAddressMap_.clear();

   return mapMove;
}

////////////////////////////////////////////////////////////////////////////////
std::unique_ptr<Seeds::WalletBackup> WalletContainer::getBackupStrings(
   const PassphraseLambda& passLbd) const
{
   auto wltSingle = std::dynamic_pointer_cast<Wallets::AssetWallet_Single>(wallet_);
   if (wltSingle == nullptr) {
      LOGERR << "WalletContainer::getBackupStrings: unexpected wallet type";
      throw std::runtime_error(
         "WalletContainer::getBackupStrings: unexpected wallet type");
   }

   wltSingle->setPassphrasePromptLambda(passLbd);
   auto backupStrings = Seeds::Helpers::getWalletBackup(wltSingle);
   wltSingle->resetPassphrasePromptLambda();

   return backupStrings;
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::eraseFromDisk()
{
   auto wltPtr = move(wallet_);
   Wallets::AssetWallet::eraseFromDisk(wltPtr.get());
   wltPtr.reset();
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::setComment(const std::string& key, const std::string& val)
{
   auto keyBd = BinaryData::fromString(key);
   wallet_->setComment(keyBd, val);
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::setLabels(const std::string& title, const std::string& desc)
{
   wallet_->setLabel(title);
   wallet_->setDescription(desc);
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::updateBalancesAndCount(uint32_t topBlockHeight)
{
   auto lbd = [this](ReturnMessage<std::vector<uint64_t>> vec)
   {
      std::unique_lock<std::mutex> lock(stateMutex_);
      auto balVec = std::move(vec.get());
      totalBalance_ = balVec[0];
      spendableBalance_ = balVec[1];
      unconfirmedBalance_ = balVec[2];
   };
   asyncWlt_->getBalancesAndCount(topBlockHeight, lbd);
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::extendAddressChainToIndex(
   const Armory::Wallets::AddressAccountId& id, unsigned count)
{
   wallet_->extendPublicChainToIndex(id, count);
}

////////////////////////////////////////////////////////////////////////////////
bool WalletContainer::hasAddress(const BinaryData& addr)
{
   return wallet_->hasScrAddr(addr);
}

////////////////////////////////////////////////////////////////////////////////
bool WalletContainer::hasAddress(const std::string& addr)
{
   return wallet_->hasAddrStr(addr);
}

////////////////////////////////////////////////////////////////////////////////
void WalletContainer::getUTXOs(uint64_t val, bool zc, bool rbf,
   const std::function<void(ReturnMessage<std::vector<UTXO>>)>& lbd)
{
   asyncWlt_->getUTXOs(val, zc, rbf, lbd);
}

////////////////////////////////////////////////////////////////////////////////
const Wallets::EncryptionKeyId& WalletContainer::getDefaultEncryptionKeyId() const
{
   return wallet_->getDefaultEncryptionKeyId();
}

////////////////////////////////////////////////////////////////////////////////
////
//// Armory135Header
////
////////////////////////////////////////////////////////////////////////////////
void Armory135Header::verifyChecksum(
   const BinaryDataRef& val, const BinaryDataRef& chkSum)
{
   if (val.isZero() && chkSum.isZero()) {
      return;
   }

   auto computedChkSum = BtcUtils::getHash256(val);
   if (computedChkSum.getSliceRef(0, 4) != chkSum) {
      throw std::runtime_error("failed checksum");
   }
}

////////////////////////////////////////////////////////////////////////////////
void Armory135Header::parseFile()
{
   /*
   Simply return on any failure, the version_ field will not be initialized 
   until the whole header is parsed and checksums pass
   */
   
   uint32_t version = UINT32_MAX;
   try {
      //grab root key & address chain length from python wallet
      auto fileMap = FileUtils::FileMap(path_, false);
      BinaryRefReader brr(fileMap.ptr(), fileMap.size());

      //file type
      auto fileTypeStr = brr.get_BinaryData(8);
      if (fileTypeStr != BinaryData::fromString(WALLET_135_HEADER, 8)) {
         return;
      }

      //version
      version = brr.get_uint32_t();

      //magic bytes
      auto magicBytes = brr.get_BinaryData(4);
      if (magicBytes != Config::BitcoinSettings::getMagicBytes()) {
         return;
      }

      //flags
      auto flags = brr.get_uint64_t();
      isEncrypted_  = flags & 0x0000000000000001;
      watchingOnly_ = flags & 0x0000000000000002;

      //wallet ID
      auto walletIDbin = brr.get_BinaryData(6);
      walletID_ = BtcUtils::base58_encode(walletIDbin);

      //creation timestamp
      timestamp_ = brr.get_uint64_t();

      //label name & description
      auto&& labelNameBd = brr.get_BinaryData(32);
      auto&& labelDescBd = brr.get_BinaryData(256);

      auto labelNameLen = strnlen(labelNameBd.toCharPtr(), 32);
      labelName_ = std::string(labelNameBd.toCharPtr(), labelNameLen);

      auto labelDescriptionLen = strnlen(labelNameBd.toCharPtr(), 256);
      labelDescription_ = std::string(
         labelDescBd.toCharPtr(), labelDescriptionLen);

      //highest used chain index
      highestUsedIndex_ = brr.get_int64_t();

      {
         /* kdf params */
         auto kdfPayload      = brr.get_BinaryDataRef(256);
         BinaryRefReader brrPayload(kdfPayload);
         auto allKdfData = brrPayload.get_BinaryDataRef(44);
         auto allKdfChecksum  = brrPayload.get_BinaryDataRef(4);

         //skip check if there is wallet is unencrypted
         if (isEncrypted_) {
            verifyChecksum(allKdfData, allKdfChecksum);

            BinaryRefReader brrKdf(allKdfData);
            kdfMem_    = brrKdf.get_uint64_t();
            kdfIter_  = brrKdf.get_uint32_t();
            kdfSalt_   = brrKdf.get_BinaryDataRef(32);
         }
      }

      //256 bytes skip
      brr.advance(256);

      /* root address */
      auto rootAddrRef = brr.get_BinaryDataRef(PYBTC_ADDRESS_SIZE);
      Armory135Address rootAddrObj;
      rootAddrObj.parseFromRef(rootAddrRef);
      addrMap_.emplace(BinaryData::fromString("ROOT"), rootAddrObj);

      //1024 bytes skip
      brr.advance(1024);

      {
         /* wallet entries */
         while (brr.getSizeRemaining() > 0) {
            auto entryType = brr.get_uint8_t();
            switch (entryType)
            {
               case WLT_DATATYPE_KEYDATA:
               {
                  auto key = brr.get_BinaryDataRef(20);
                  auto val = brr.get_BinaryDataRef(PYBTC_ADDRESS_SIZE);

                  Armory135Address addrObj;
                  addrObj.parseFromRef(val);
                  addrMap_.emplace(key, addrObj);
                  break;
               }

               case WLT_DATATYPE_ADDRCOMMENT:
               {
                  auto key = brr.get_BinaryDataRef(20);
                  auto len = brr.get_uint16_t();
                  auto val = brr.get_String(len);

                  commentMap_.insert(make_pair(key, val));
                  break;
               }

               case WLT_DATATYPE_TXCOMMENT:
               {
                  auto key = brr.get_BinaryDataRef(32);
                  auto len = brr.get_uint16_t();
                  auto val = brr.get_String(len);

                  commentMap_.insert(make_pair(key, val));
                  break;
               }

               case WLT_DATATYPE_OPEVAL:
                  throw std::runtime_error("not supported");

               case WLT_DATATYPE_DELETED:
               {
                  auto len = brr.get_uint16_t();
                  brr.advance(len);
                  break;
               }

               default:
                  throw std::runtime_error("invalid wallet entry");
            }
         }
      }
   } catch (const std::exception& e) {
      LOGWARN << "failed to load wallet at " << path_.string() << " with error: ";
      LOGWARN << "   " << e.what();
      return;
   }

   version_ = version;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<Wallets::AssetWallet_Single> Armory135Header::migrate(
   const PassphraseLambda& passLbd) const
{
   auto rootKey = BinaryData::fromString("ROOT");
   auto rootAddrIter = addrMap_.find(rootKey);
   if (rootAddrIter == addrMap_.end()) {
      throw std::runtime_error("no root entry");
   }

   auto& rootAddrObj = rootAddrIter->second;
   auto chaincodeCopy = rootAddrObj.chaincode();

   SecureBinaryData controlPass;
   SecureBinaryData privKeyPass;

   auto highestIndex = highestUsedIndex_;
   for (auto& addrPair : addrMap_) {
      if (highestIndex < addrPair.second.chainIndex()) {
         highestIndex = addrPair.second.chainIndex();
      }
   }
   ++highestIndex;

   //try to decrypt the private root
   SecureBinaryData decryptedRoot;
   {
      if (isEncrypted_ && rootAddrObj.hasPrivKey() &&
         rootAddrObj.isEncrypted()) {
         //decrypt lbd
         auto decryptPrivKey = [this, &privKeyPass](
            const PassphraseLambda& passLbd,
            const Armory135Address& rootAddrObj)->SecureBinaryData
         {
            std::set<Wallets::EncryptionKeyId> idSet = {
               BinaryData::fromString(walletID_)
            };
            while (true) {
               //prompt for passphrase
               auto passphrase = passLbd(idSet);
               if (passphrase.empty()) {
                  return {};
               }

               //kdf it
               KdfRomix myKdf(kdfMem_, kdfIter_, kdfSalt_);
               auto derivedPass = myKdf.DeriveKey(passphrase);

               //decrypt the privkey
               auto decryptedKey = CryptoAES::DecryptCFB(
                  rootAddrObj.privKey(), derivedPass, rootAddrObj.iv());

               //generate pubkey
               auto computedPubKey =
                  CryptoECDSA().ComputePublicKey(decryptedKey, false);

               if (rootAddrObj.pubKey() != computedPubKey) {
                  continue;
               }

               //compare pubkeys
               privKeyPass = std::move(passphrase);
               return decryptedKey;
            }
         };
         decryptedRoot = std::move(decryptPrivKey(passLbd, rootAddrObj));
      }

      //cleanup
      passLbd({});
   }

   //create wallet
   auto folder = std::filesystem::path(path_).parent_path();
   std::shared_ptr<Wallets::AssetWallet_Single> wallet;
   if (decryptedRoot.empty()) {
      auto pubKeyCopy = rootAddrObj.pubKey();
      wallet = Wallets::AssetWallet_Single::createFromPublicRoot_Armory135(
         folder, pubKeyCopy, chaincodeCopy,
         controlPass, highestIndex);
   } else {
      std::unique_ptr<Seeds::ClearTextSeed> seed(
         new Seeds::ClearTextSeed_Armory135(
            decryptedRoot,
            chaincodeCopy
      ));
      wallet = Wallets::AssetWallet_Single::createFromSeed(
         std::move(seed),
         Wallets::WalletCreationParams{
            privKeyPass, controlPass, folder, (uint32_t)highestIndex
      });
   }

   //main account id, check it matches armory wallet id
   if (wallet->getID() != walletID_) {
      throw std::runtime_error("wallet id mismatch");
   }

   //run through addresses, figure out script types
   auto accID = wallet->getMainAccountID();
   auto mainAccPtr = wallet->getAccountForID(accID);

   //TODO: deal with imports

   std::map<Wallets::AssetId, AddressEntryType> typeMap;
   for (const auto& addrPair : addrMap_) {
      if (addrPair.second.chainIndex() < 0 ||
         addrPair.second.chainIndex() > highestUsedIndex_) {
         continue;
      }

      const auto& addrTypePair = mainAccPtr->getAssetIDPairForAddrUnprefixed(
         addrPair.second.scrAddr());

      if (addrTypePair.second != mainAccPtr->getDefaultAddressType()) {
         typeMap.emplace(addrTypePair);
      }
   }

   {
      //set script types
      auto dbtx = wallet->beginSubDBTransaction(walletID_, true);
      Wallets::AssetKeyType lastIndex = -1;
      for (const auto& typePair : typeMap) {
         //get int index for pair
         while (typePair.first.getAssetKey() != lastIndex) {
            wallet->getNewAddress();
            ++lastIndex;
         }
         wallet->getNewAddress(typePair.second);
         ++lastIndex;
      }

      while (lastIndex < highestUsedIndex_) {
         wallet->getNewAddress();
         ++lastIndex;
      }
   }

   //set name & desc
   if (!labelName_.empty()) {
      wallet->setLabel(labelName_);
   }

   if (!labelDescription_.empty()) {
      wallet->setDescription(labelDescription_);
   }

   {
      //add comments
      auto dbtx = wallet->beginSubDBTransaction(walletID_, true);
      for (const auto& commentPair : commentMap_) {
         wallet->setComment(commentPair.first, commentPair.second);
      }
   }

   return wallet;
}


////////////////////////////////////////////////////////////////////////////////
////
//// Armory135Address
////
////////////////////////////////////////////////////////////////////////////////
void Armory135Address::parseFromRef(const BinaryDataRef& bdr)
{
   BinaryRefReader brrScrAddr(bdr);

   {
      //scrAddr, only to verify the checksum
      scrAddr_ = brrScrAddr.get_BinaryData(20);
      auto scrAddrChecksum = brrScrAddr.get_BinaryData(4);
      Armory135Header::verifyChecksum(scrAddr_, scrAddrChecksum);
   }

   //address version, unused
   brrScrAddr.get_uint32_t();

   //address flags
   auto addrFlags = brrScrAddr.get_uint64_t();
   hasPrivKey_ = addrFlags & 0x0000000000000001;
   hasPubKey_  = addrFlags & 0x0000000000000002;

   isEncrypted_ = addrFlags & 0x0000000000000004;

   //chaincode
   chaincode_ = brrScrAddr.get_BinaryData(32);
   auto chaincodeChecksum = brrScrAddr.get_BinaryDataRef(4);
   Armory135Header::verifyChecksum(chaincode_, chaincodeChecksum);

   //chain index
   chainIndex_       = brrScrAddr.get_int64_t();
   depth_            = brrScrAddr.get_int64_t();

   //iv
   iv_               = brrScrAddr.get_BinaryData(16);
   auto ivChecksum   = brrScrAddr.get_BinaryDataRef(4);
   if (isEncrypted_) {
      Armory135Header::verifyChecksum(iv_, ivChecksum);
   }

   //private key
   privKey_             = brrScrAddr.get_BinaryData(32);
   auto privKeyChecksum = brrScrAddr.get_BinaryDataRef(4);
   if (hasPrivKey_) {
      Armory135Header::verifyChecksum(privKey_, privKeyChecksum);
   }

   //pub key
   pubKey_              = brrScrAddr.get_BinaryData(65);
   auto pubKeyChecksum  = brrScrAddr.get_BinaryDataRef(4);
   Armory135Header::verifyChecksum(pubKey_, pubKeyChecksum);
}
