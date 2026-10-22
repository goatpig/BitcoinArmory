////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cstring>
#include <filesystem>
#include <string_view>
#include <algorithm>
#include <cstring>

#include "Manager.h"
#include <Utils/BtcUtils.h>
#include <Utils/FileUtils.h>
#include <Utils/Cryptography.h>
#include <Ledgers/LedgerEntry.h>
#include <Ledgers/Context.h>
#include <AsyncClient.h>
#include <BlockchainDatabase/txio.h>

#include <Wallets/Wallets.h>
#include <Wallets/IOHeader.h>
#include <Wallets/Accounts/AddressAccounts.h>
#include <Wallets/Seeds/Backups.h>
#include <Wallets/Seeds/Seeds.h>
#include <Wallets/EncryptedDB.h>

#include "TxIOCache.h"
#include "../Notifications.h"
#include "../PassphrasePrompt.h"

using namespace Armory;
using namespace Armory::Bridge;
using namespace std::string_view_literals;
using namespace std::chrono_literals;

namespace
{
   //ledger helpers
   const std::string mainDelegateId = "mainDelegateId";

   std::vector<Ledgers::Entry> getPageForId(WalletManager* mgr, uint32_t pageId)
   {
      ReentrantLock lock(mgr);

      std::vector<std::map<Types::TxKey, Ledgers::Entry>> walletLedgers;
      unsigned totalSize = 0;
      auto filteredMap = mgr->getFilteredContainerMap();
      walletLedgers.reserve(filteredMap.size());
      for (const auto& wltCont : filteredMap) {
         const auto txioMap = wltCont.second->getTxioMap();
         auto context = Ledgers::prepareContext(txioMap, mgr->getDbCache(), {});
         walletLedgers.emplace_back(std::move(Ledgers::computeLedgerMap(
            txioMap, 0, UINT32_MAX, wltCont.first, context)));
         totalSize += walletLedgers.back().size();
      }

      std::vector<Ledgers::Entry> result;
      result.reserve(totalSize);
      for (auto& ledgers : walletLedgers) {
         for (auto& lePair : ledgers) {
            result.emplace_back(std::move(lePair.second));
         }
      }
      std::sort(result.begin(), result.end(), Ledgers::DescendingOrder{});
      return result;
   }

   std::vector<Ledgers::Entry> getLedgersForZCs(WalletManager* mgr)
   {
      //this is only used to generate ledgers for ZC notifs
      auto txioCache = mgr->txioCache();
      std::vector<std::map<Types::TxKey, Ledgers::Entry>> walletLedgers;
      unsigned totalSize = 0;
      auto filteredMap = mgr->getFilteredContainerMap();
      walletLedgers.reserve(filteredMap.size());
      for (const auto& wltCont : filteredMap) {
         const auto& txioMap = txioCache->getZcTxios(
            [wltPtr=wltCont.second](const Types::ScrAddr& addr)->bool
            { return wltPtr->hasScrAddr(addr); }
         );
         auto context = Ledgers::prepareContext(txioMap, mgr->getDbCache(), {});
         walletLedgers.emplace_back(std::move(Ledgers::computeLedgerMap(
            txioMap, 0, UINT32_MAX, wltCont.first, context)));
         totalSize += walletLedgers.back().size();
      }

      std::vector<Ledgers::Entry> result;
      result.reserve(totalSize);
      for (auto& ledgers : walletLedgers) {
         for (auto& lePair : ledgers) {
            if (lePair.second.getBlockNum() != UINT32_MAX) {
               continue;
            }
            result.emplace_back(std::move(lePair.second));
         }
      }
      std::sort(result.begin(), result.end(), Ledgers::DescendingOrder{});
      return result;
   }
}

////////////////////////////////////////////////////////////////////////////////
////
//// WalletManager
////
////////////////////////////////////////////////////////////////////////////////
WalletManager::WalletManager(
   const std::filesystem::path& path,
   std::shared_ptr<NotifQueue> notifQueue) :
   walletsDir_(path), notifQueue_(notifQueue)
{
   if (!FileUtils::isDir(walletsDir_, 2)) {
      auto err = std::format("{} is not a valid datadir", walletsDir_.string());
      LOGERR << err;
      throw std::runtime_error(err);
   }

   //setup primary ledger delegate
   txioCache_ = std::make_shared<TxIOCache>();
   delegateMap_.emplace(mainDelegateId, Ledgers::Delegate{
      [this](uint32_t pageId)->std::vector<Ledgers::Entry>{
         return getPageForId(this, pageId);
      }, nullptr, nullptr,
      []()->uint32_t { return 1; }
   });
}

////
const std::filesystem::path& WalletManager::getWalletsDir() const
{
   return walletsDir_;
}

std::shared_ptr<const TxIOCache> WalletManager::txioCache() const
{
   return std::const_pointer_cast<const TxIOCache>(txioCache_);
}

////
bool WalletManager::hasWallet(const Wallets::WalletId& id)
{
   std::unique_lock<std::mutex> lock(mu_);
   auto wltIter = wallets_.find(id);
   return wltIter != wallets_.end();
}

std::map<std::string, ContainerPtr> WalletManager::getFilteredContainerMap() const
{
   std::map<std::string, ContainerPtr> result;
   for (const auto& wltPair : walletsByDbId_) {
      auto wltIter = mainLedgerFilter_.find(wltPair.second->getWalletId());
      if (wltIter == mainLedgerFilter_.end()) {
         continue;
      }
      auto accIter = wltIter->second.find(wltPair.second->getAccountId());
      if (accIter == wltIter->second.end()) {
         continue;
      }
      result.emplace(wltPair);
   }
   return result;
}

void WalletManager::updateMainLedgerFilter(
   const std::map<Wallets::WalletId, AAIdSet>& idMap)
{
   mainLedgerFilter_ = idMap;
   notifQueue_->push_back(
      std::make_unique<Notifications::Server::Notif_Refresh>(
         std::string{"wallet_filter_changed"},
         Notifications::SyncState::NoSync));
}

////////////////////////////////////////////////////////////////////////////////
std::set<Wallets::AddressAccountId> WalletManager::getAddressAccountIds(
   const Wallets::WalletId& wltId) const
{
   auto iter = wallets_.find(wltId);
   if (iter == wallets_.end()) {
      throw std::runtime_error(std::format("no wallet for id {}",
         static_cast<std::string>(wltId)));
   }

   std::set<Wallets::AddressAccountId> result;
   for (const auto& accPair : iter->second) {
      result.emplace(accPair.first);
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
const std::map<std::string, ContainerPtr>& WalletManager::getContainerMap() const
{
   return walletsByDbId_;
}

ContainerPtr WalletManager::getContainer(
   const Wallets::WalletId& wltId) const
{
   auto iter = wallets_.find(wltId);
   if (iter == wallets_.end()) {
      throw std::runtime_error(std::format("no wallet for id {}",
         static_cast<std::string>(wltId)));
   }
   return iter->second.begin()->second;
}

ContainerPtr WalletManager::getContainer(
   const Wallets::WalletId& wltId, const Wallets::AddressAccountId& accId) const
{
   auto wltIter = wallets_.find(wltId);
   if (wltIter == wallets_.end()) {
      throw std::runtime_error(std::format("i do not know wallet {}",
         static_cast<std::string>(wltId)));
   }

   auto accIter = wltIter->second.find(accId);
   if (accIter == wltIter->second.end()) {
      throw std::runtime_error(std::format(
         "there is no account {} for wallet {}",
         accId.toHexStr(), static_cast<std::string>(wltId))
      );
   }
   return accIter->second;
}

////////
void WalletManager::setBdvPtr(std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr)
{
   bdvPtr_ = bdvPtr;
   for (auto& wltIt : wallets_) {
      for (auto& accIt : wltIt.second) {
         accIt.second->setBdvPtr(bdvPtr);
      }
   }
   auto message = Notifications::Client::getSerializedSetupDoneNotif();
   notifQueue_->push_back(std::make_unique<Notifications::Server::Notif_Push>(
      std::move(message)));
}

void WalletManager::cleanupBdvPtr()
{
   //cleanup bdv shared_ptr in wallet container objects
   for (auto& wltIt : wallets_) {
      for (auto& accIt : wltIt.second) {
         accIt.second->cleanupBdvPtr();
      }
   }

   //clean up local reference to bdv shared_ptr
   bdvPtr_.reset();
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::registerWallets()
{
   for (const auto& wltIt : wallets_) {
      for (const auto& accIt : wltIt.second) {
         accIt.second->registerWithBDV(false);
      }
   }
   auto message = Notifications::Client::getSerializedWalletsRegisteredNotif();
   notifQueue_->push_back(std::make_unique<Notifications::Server::Notif_Push>(
      std::move(message)));
}

void WalletManager::registerWallet(const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId, bool isNew)
{
   using namespace Notifications;

   auto container = getContainer(wltId, accId);
   try {
      container->registerWithBDV(isNew);
   } catch (const OfflineException& e) {
      auto message = Client::getSerializedRefreshNotif(
         std::make_unique<Server::Notif_Refresh>(
            container->getDbId(), SyncState::NoSync));
      notifQueue_->push_back(std::make_unique<Server::Notif_Push>(
         std::move(message)));
      throw e;
   }
}

////////////////////////////////////////////////////////////////////////////////
ContainerPtr WalletManager::addAccount(
   std::shared_ptr<Wallets::AssetWallet> wltPtr,
   const Wallets::AddressAccountId& accId)
{
   ReentrantLock lock(this);

   //check we dont have this wallet
   auto wltIter = wallets_.find(wltPtr->getID());
   if (wltIter == wallets_.end()) {
      wltIter = wallets_.emplace(wltPtr->getID(),
         std::map<Wallets::AddressAccountId, ContainerPtr>{}).first;
   }
   auto filterIter = mainLedgerFilter_.emplace(wltPtr->getID(), AAIdSet{}).first;

   auto accIter = wltIter->second.find(accId);
   if (accIter != wltIter->second.end()) {
      return accIter->second;
   }

   //create wrapper object
   auto wltContPtr = new WalletContainer(wltPtr->getID(), accId, txioCache_);
   ContainerPtr wltCont;
   wltCont.reset(wltContPtr);

   //set bdvPtr if we have it
   if (bdvPtr_ != nullptr) {
      wltCont->setBdvPtr(bdvPtr_);
   }

   //set & add to map
   wltCont->setWalletPtr(wltPtr, accId);
   wltIter->second.emplace(accId, wltCont);
   walletsByDbId_.emplace(wltCont->getDbId(), wltCont);
   filterIter->second.emplace(accId);

   //return it
   return wltCont;
}

////
void WalletManager::addAllAccounts(std::shared_ptr<Wallets::AssetWallet> wltPtr)
{
   const auto& accIds = wltPtr->getAccountIDs();
   for (const auto& accId : accIds) {
      addAccount(wltPtr, accId);
   }
}

////////////////////////////////////////////////////////////////////////////////
Wallets::WalletId WalletManager::createNewWallet(
   Seeds::SeedType sType,
   const SecureBinaryData& extraEntropy,
   const Wallets::IO::CreateWalletParams& params)
{
   auto root = Cryptography::PRNG::generateRandomStrong(32);
   if (!extraEntropy.empty()) {
      auto hashTropy = BtcUtils::getHash256(extraEntropy);
      root.XOR(hashTropy);
   }

   switch (sType)
   {
      case Seeds::SeedType::ArmoryLegacy:
      {
         auto seed = std::make_unique<Seeds::ClearTextSeed_Armory>(
            root, SecureBinaryData{}, Seeds::LegacyType::Armory200);
         auto wallet = Wallets::AssetWallet_Single::createFromSeed(
            std::move(seed), params);

         //add wallet path to file map, so that it doesn't appear in
         //subsequent calls to listWallets
         auto wltPath = wallet->getDbFilename();
         walletFiles_.emplace(
            wltPath.filename().string(),
            std::make_shared<LMDBWalletInfo>(wltPath, nullptr, true)
         );

         //put first address in use, or the GUI will have nothing to display
         auto accPtr = wallet->getAccountForID(wallet->getMainAccountID());
         accPtr->getNewAddress(wallet->getIface());
         addAccount(wallet, wallet->getMainAccountID());
         return wallet->getID();
      }

      case Seeds::SeedType::BIP32_Structured:
      case Seeds::SeedType::BIP32_Virgin:
      {
         auto seed = std::make_unique<Seeds::ClearTextSeed_BIP32>(
            root, sType);
         auto wallet = Wallets::AssetWallet_Single::createFromSeed(
            std::move(seed), params);

         //add wallet path to file map
         auto wltPath = wallet->getDbFilename();
         walletFiles_.emplace(
            wltPath.filename().string(),
            std::make_shared<LMDBWalletInfo>(wltPath, nullptr, true)
         );

         //put first address in use for each account
         for (const auto& accId : wallet->getAccountIDs()) {
            auto accPtr = wallet->getAccountForID(accId);
            accPtr->getNewAddress(wallet->getIface());
            addAccount(wallet, accId);
         }
         return wallet->getID();
      }

      default:
         throw std::runtime_error("unsupported seed type");
   }
}

////////////////////////////////////////////////////////////////////////////////
std::filesystem::path WalletManager::unloadWallet(
   const Wallets::WalletId& wltId)
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
         walletsByDbId_.erase(acc.second->getDbId());
         acc.second->unregisterFromBDV();
      } catch (const OfflineException&) {
         //we do not care if the unregister operation fails
      }
   }

   //remove containers from map;
   wallets_.erase(wltId);
   return path;
}

////
void WalletManager::deleteWallet(const Wallets::WalletId& wltId)
{
   ReentrantLock lock(this);
   auto wltCont = getContainer(wltId);
   unloadWallet(wltId);

   //delete from disk & cleanup
   wltCont->eraseFromDisk();
   wltCont.reset();
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::loadWallet(const Wallets::IO::ReadOnlyFileParams& params)
{
   auto wltPtr = Wallets::AssetWallet::loadMainWalletFromFile(params);
   addAllAccounts(wltPtr);
}

////////
void WalletManager::loadAFile(const std::filesystem::path& path)
{
   ReentrantLock lock(this);
   try {
      auto wltPtr = Wallets::AssetWallet::loadMainWalletFromFile(
      Wallets::IO::ReadOnlyFileParams{path, {}});
      walletFiles_.emplace(
         path.filename().string(),
         std::make_shared<LMDBWalletInfo>(path, wltPtr)
      );
   } catch (const Wallets::Encryption::DecryptedDataContainerException& e) {
      //could not decrypt wallet control header, track as encrypted wallet
      std::string errStr{
         "failed to open wallet \"" + path.string() +
         "\" with decryption error: " + e.what()};
      LOGDEBUG << errStr;
      walletFiles_.emplace(
         path.filename().string(),
         std::make_shared<LMDBWalletInfo>(path, nullptr)
      );
   } catch (const Wallets::IO::NoEntryInWalletException&) {
      //wallet is missing a mandatory entry, typically a header
      LOGWARN << "failed to open wallet \"" << path.string()
         << "\" with missing entry error";
   }
}

////////
std::map<std::string, std::shared_ptr<WalletFileInfo>>
WalletManager::listWallets()
{
   //iterate over files in datadir
   std::vector<std::filesystem::path> walletPaths, a135Paths;
   for (const auto& dirEntry : std::filesystem::directory_iterator{walletsDir_} ) {
      const auto& path = dirEntry.path();

      //ignore folders
      if (FileUtils::isDir(path, 2)) {
         continue;
      }

      //ignore certain obvious extensions
      const auto& extension = path.extension();
      if (extension == ".peers" || extension == ".txt" || extension == ".log") {
         continue;
      }

      //ignore this file if we've parsed it before
      auto iter = walletFiles_.find(path.filename().string());
      if (iter != walletFiles_.end()) {
         continue;
      }

      //ignore LMDB lock files
      auto filename = path.filename().string();
      if (filename.size() > 5) {
         if (std::memcmp(filename.data() + filename.size() - 5,
            "-lock", 5) == 0) {
            continue;
         }
      }

      //track file for parsing
      walletPaths.emplace_back(path);
   }

   //read the potential wallet files
   ReentrantLock lock(this);
   for (const auto& wltPath : walletPaths) {
      try {
         loadAFile(wltPath);
      } catch (const std::exception&) {
         //couldnt load the file, maybe it's a legacy wallet?
         a135Paths.emplace_back(wltPath);
      }
   }

   //parse the potential armory 1.35 wallet files
   for (const auto& wltPath : a135Paths) {
      auto a135 = std::make_shared<Armory135Header>(wltPath);
      if (!a135->isInitialized()) {
         continue;
      }

      //an armory v1.35 wallet was loaded, check if we need to
      //migrate it to v3.x
      auto iter = wallets_.find(a135->getID());
      if (iter != wallets_.end()) {
         continue;
      }

      //no equivalent v3.x wallet loaded, add it to the list
      walletFiles_.emplace(
         wltPath.filename().string(),
         std::make_shared<A135FileInfo>(a135)
      );
   }

   std::map<std::string, std::shared_ptr<WalletFileInfo>> result;
   for (const auto& entry : walletFiles_) {
      switch (entry.second->state())
      {
         case WalletLoadState::Loaded:
            break;

         default:
            result.emplace(entry);
      }
   }
   return result;
}

////////
std::shared_ptr<WalletFileInfo> WalletManager::importFile(
   const std::filesystem::path& path)
{
   //lock container and try to load the file
   ReentrantLock lock(this);
   try {
      loadAFile(path);
   } catch (const std::exception&) {
      //potentially a legacy wallet
      auto a135 = std::make_shared<Armory135Header>(path);
      if (a135->isInitialized()) {
         auto idIter = wallets_.find(a135->getID());
         if (idIter != wallets_.end()) {
            throw std::runtime_error("this legacy wallet is already loaded");
         }

         walletFiles_.emplace(
            path.filename().string(),
            std::make_shared<A135FileInfo>(a135)
         );
      } else if (a135->errorCode() == A135_ERROR_MAGICBYTE) {
         throw std::runtime_error(
            "this legacy wallet is not for this network!");
      }
   }

   //if the file is loaded, it will be in files map
   auto fileIter = walletFiles_.find(path.filename().string());
   if (fileIter == walletFiles_.end()) {
      throw std::runtime_error("file isn't a wallet");
   }
   return fileIter->second;
}

/////////
void WalletManager::unlockControlHeader(const std::filesystem::path& path,
   const Passphrase::UnlockFunc& lbd)
{
   //sanity checks
   if (path.empty() || lbd == nullptr) {
      throw std::runtime_error("tried to unlock control header with empty id/lambda");
   }

   auto iter = walletFiles_.find(path.filename().string());
   if (iter == walletFiles_.end()) {
      throw std::runtime_error("this file is not a known wallet: " + path.string());
   }

   auto infoObj = std::dynamic_pointer_cast<LMDBWalletInfo>(iter->second);
   if (infoObj == nullptr) {
      throw std::runtime_error("invalid wallet info type");
   }

   infoObj->unlockControlHeader(lbd);
}

/////////
const Wallets::WalletId& WalletManager::migrateWallet(
   const std::filesystem::path& path,
   const Passphrase::UnlockFunc& lbd,
   const Wallets::IO::CreateWalletParams& params)
{
   //sanity checks
   if (path.empty() || lbd == nullptr) {
      throw std::runtime_error(
         "tried to migrate wallet with empty id/lambda");
   }

   auto iter = walletFiles_.find(path.filename().string());
   if (iter == walletFiles_.end()) {
      throw std::runtime_error(
         "this file is not a known wallet: " + path.string());
   }

   auto infoObj = std::dynamic_pointer_cast<A135FileInfo>(iter->second);
   if (infoObj == nullptr) {
      throw std::runtime_error("invalid wallet info type");
   }

   auto wltPtr = infoObj->migrate(lbd, params);
   auto migratedPath = wltPtr->getDbFilename();
   walletFiles_.emplace(migratedPath.filename().string(),
      std::make_shared<LMDBWalletInfo>(migratedPath, wltPtr));
   return wltPtr->getID();
}

/////////
bool WalletManager::stageWallet(const Wallets::WalletId& walletId, bool stage)
{
   for (auto& knownFile : walletFiles_) {
      try {
         if (knownFile.second->walletId() == walletId) {
            knownFile.second->setStaged(stage);
            return true;
         }
      } catch (const std::exception&) {
         continue;
      }
   }
   return false;
}

/////////
void WalletManager::loadWallets()
{
   ReentrantLock lock(this);
   listWallets();
   for (auto& entry : walletFiles_) {
      auto wltFile = std::dynamic_pointer_cast<LMDBWalletInfo>(entry.second);
      if (wltFile == nullptr) {
         continue;
      }
      try {
         auto wltPtr = wltFile->moveWltPtr();
         addAllAccounts(wltPtr);
      } catch (const std::exception&) {
         continue;
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::updateStateFromDB(
   std::unique_ptr<Notifications::Server::Notif> notif)
{
   using namespace Notifications;

   //update txio cache
   std::shared_ptr<Server::Notif> sharedNotif{std::move(notif)};

   if (sharedNotif->syncWalletState() != SyncState::NoSync) {
      auto checkFromHeight = txioCache_->update(bdvPtr_, sharedNotif);

      //the txio chache is updated, resolve txios for wallets
      ReentrantLock lock(this);
      for (auto wltCont : walletsByDbId_) {
         wltCont.second->resolveTxios(checkFromHeight);
         wltCont.second->resolveZcTxios();

         if (sharedNotif->syncWalletState() == SyncState::SyncWallets) {
            wltCont.second->synchronizeAddressChainState();
         }
      }
   }

      //fire callback
   switch (sharedNotif->type)
   {
      case Server::NotifType::NEWBLOCK:
      {
         auto blockPtr =
            std::dynamic_pointer_cast<Server::Notif_NewBlock>(sharedNotif);
         auto message = Client::getSerializedNewBlockNotif(blockPtr);
         notifQueue_->push_back(
            std::make_unique<Notifications::Server::Notif_Push>(
               std::move(message)));
         break;
      }

      case Server::NotifType::REFRESH:
      {
         auto refreshPtr =
            std::dynamic_pointer_cast<Server::Notif_Refresh>(sharedNotif);
         auto message = Client::getSerializedRefreshNotif(refreshPtr);
         notifQueue_->push_back(
            std::make_unique<Notifications::Server::Notif_Push>(
               std::move(message)));
         break;
      }

      case Server::NotifType::ZC:
      {
         //get ledgers for the zc txios
         auto ledgers = getLedgersForZCs(this);

         //feed them to callback
         auto zcPtr = std::dynamic_pointer_cast<Server::Notif_ZC>(sharedNotif);
         auto messages = Client::getSerializedZcNotif(
            ledgers, zcPtr->invalidatedZCHashes);
         notifQueue_->push_back(
            std::make_unique<Notifications::Server::Notif_Push>(
               std::move(messages.first)));
         if (!messages.second.empty()) {
            notifQueue_->push_back(
               std::make_unique<Notifications::Server::Notif_Push>(
                  std::move(messages.second)));
         }
         break;
      }

      default:
         return;
   }
}

////////////////////////////////////////////////////////////////////////////////
/***
Address creation should be called from WalletManager. It ensures data
consistency in the following ways:
   . Register the addresses with the db, if available
   . Update the balance and count cache
   . Check address types and top used count again chain data. This is
      critical for restored wallets, where the address chain wasn't
      extended far enough initially.
   . notify the caller to refresh its address data on completion
***/
void WalletManager::extendAddressChain(const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId, unsigned count, bool isNew,
   std::function<void(int)> progressFunc)
{
   auto container = getContainer(wltId, accId);
   container->extendAddressChain(count, progressFunc);
   try {
      registerWallet(wltId, accId, isNew);
   } catch (const OfflineException&) {
      //register wallet will notify the caller regardless of connection status
   }
}

////
std::shared_ptr<AddressEntry> WalletManager::getNewAddress(
   const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId,
   uint32_t addrType, uint32_t addrKind)
{
   #define ADDRESS_NEW     0
   #define ADDRESS_CHANGE  1
   #define ADDRESS_PEEK    2

   bool wasExtended = false;
   auto progFunc = [&wasExtended](int)
   {
      wasExtended = true;
   };

   auto wltContainer = getContainer(wltId, accId);
   auto wltPtr = wltContainer->getWalletPtr();
   auto accPtr = wltContainer->getAddressAccount();

   std::shared_ptr<AddressEntry> addrPtr;
   switch (addrKind)
   {
      case ADDRESS_NEW:
      {
         addrPtr = accPtr->getNewAddress(
            wltPtr->getIface(), (AddressEntryType)addrType, progFunc);
         break;
      }

      case ADDRESS_CHANGE:
      {
         addrPtr = accPtr->getNewChangeAddress(
            wltPtr->getIface(), (AddressEntryType)addrType, progFunc);
         break;
      }

      case ADDRESS_PEEK:
      {
         addrPtr = accPtr->peekNextChangeAddress(
            wltPtr->getIface(), (AddressEntryType)addrType, progFunc);
         break;
      }

      default:
         return nullptr;
   }

   if (wasExtended) {
      try {
         registerWallet(wltId, accId, true);
      } catch (const OfflineException&) {
         //register wallet will notify the caller regardless of connection status
      }
   }
   return addrPtr;
}

////////////////////////////////////////////////////////////////////////////////
// chain state stuff
std::shared_ptr<const Ledgers::DBCache> WalletManager::getDbCache() const
{
   return txioCache_->getDBCache();
}

const std::string& WalletManager::getDelegateId()
{
   return mainDelegateId;
}

const std::string& WalletManager::getDelegateIdForWallet(
   const Wallets::WalletId& wltId, const Wallets::AddressAccountId& accId)
{
   auto iter = delegateMap_.emplace(
      Cryptography::PRNG::fortuna.generateRandom(5).toHexStr(),
      Ledgers::Delegate{ [this, wltId, accId](uint32_t pageId)->
         std::vector<Ledgers::Entry> {
         auto wltCont = this->getContainer(wltId, accId);
         const auto txioMap = wltCont->getTxioMap();
         auto context = Ledgers::prepareContext(txioMap,
            this->getDbCache(), {});
         auto ledgers = Ledgers::computeLedgerMap(txioMap,
            0, UINT32_MAX, wltId, context);

         std::vector<Ledgers::Entry> result;
         result.reserve(ledgers.size());
         for (auto& ledger : ledgers) {
            result.emplace_back(std::move(ledger.second));
         }
         std::sort(result.begin(), result.end(), Ledgers::DescendingOrder{});
         return result;
      }, nullptr, nullptr,
      []()->uint32_t { return 1; }
   });
   return iter.first->first;
}

const std::string& WalletManager::getDelegateIdForScrAddr(
   const Wallets::WalletId& wltId, const Wallets::AddressAccountId& accId,
   const BinaryData& scrAddr)
{
   auto iter = delegateMap_.emplace(
      Cryptography::PRNG::fortuna.generateRandom(5).toHexStr(),
      Ledgers::Delegate{ [this, wltId, accId, scrAddr](uint32_t pageId)->
         std::vector<Ledgers::Entry> {
         auto wltCont = this->getContainer(wltId, accId);
         const auto txioMap = wltCont->getTxioMap();
         auto context = Ledgers::prepareContext(txioMap,
            this->getDbCache(), {scrAddr});
         auto ledgers = Ledgers::computeLedgerMap(txioMap,
            0, UINT32_MAX, wltId, context);

         std::vector<Ledgers::Entry> result;
         result.reserve(ledgers.size());
         for (auto& ledger : ledgers) {
            result.emplace_back(std::move(ledger.second));
         }
         std::sort(result.begin(), result.end(), Ledgers::DescendingOrder{});
         return result;
      }, nullptr, nullptr,
      []()->uint32_t { return 1; }
   });
   return iter.first->first;
}

////////
uint32_t WalletManager::getPageCountForDelegate(const std::string& id) const
{
   auto iter = delegateMap_.find(id);
   if (iter == delegateMap_.end()) {
      throw std::runtime_error(std::string{"invalid delegate id: " + id});
   }
   return iter->second.getPageCount();
}

std::vector<Ledgers::Entry> WalletManager::getPageForDelegate(
   const std::string& id, uint32_t pageId) const
{
   auto iter = delegateMap_.find(id);
   if (iter == delegateMap_.end()) {
      throw std::runtime_error(std::string{"invalid delegate id: " + id});
   }
   return iter->second.getHistoryPage(pageId);
}
