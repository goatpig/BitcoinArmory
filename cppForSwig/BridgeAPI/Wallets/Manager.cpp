////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cstring>
#include <filesystem>
#include <string_view>

#include "Manager.h"
#include "Utils/BtcUtils.h"
#include "Utils/DBUtils.h"

#include "Wallets/Wallets.h"
#include "Wallets/IOHeader.h"
#include "Wallets/Accounts/AddressAccounts.h"
#include "Wallets/Seeds/Backups.h"
#include "Wallets/Seeds/Seeds.h"

#include "Notifications.h"
#include "../PassphrasePrompt.h"
#include "AsyncClient.h"

using namespace Armory;
using namespace Armory::Bridge;
using namespace std::string_view_literals;
using namespace std::chrono_literals;

#include "capnp/Bridge.capnp.h"

////////////////////////////////////////////////////////////////////////////////
////
//// WalletManager
////
////////////////////////////////////////////////////////////////////////////////
WalletManager::WalletManager(const std::filesystem::path& path) :
   path_(path)
{
   if (!FileUtils::isDir(path_)) {
      std::string err{path_.string() + std::string{"is not a valid datadir"sv}};
      LOGERR << err;
      throw std::runtime_error(err);
   }
}

////
const std::filesystem::path& WalletManager::getWalletDir() const
{
   return path_;
}

////
bool WalletManager::hasWallet(const Wallets::WalletId& id)
{
   std::unique_lock<std::mutex> lock(mu_);
   auto wltIter = wallets_.find(id);
   return wltIter != wallets_.end();
}

////////////////////////////////////////////////////////////////////////////////
std::map<Wallets::WalletId, std::set<Wallets::AddressAccountId>>
WalletManager::getAccountIdMap() const
{
   std::map<Wallets::WalletId, std::set<Wallets::AddressAccountId>> result;
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
   const Wallets::WalletId& wltId) const
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
   const Wallets::WalletId& wltId, const Wallets::AddressAccountId& accId) const
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
void WalletManager::setupBdvCallback(
   const std::function<void(BinaryData&)>& writeFunc)
{
   if (callbackPtr_ != nullptr) {
      throw std::runtime_error("callback already instantiated!");
   }
   /***
   The bridge feeds a RemoteCallback object to the WebSocketClient object
   that bdvPtr_ wraps around. On pushes from ArmoryDB, the wsclient passes
   the packets to the RemoteCallback.

   Most of the push actions require pushing the data up the chain, to the
   client. The pushNotif lambda deals with that.

   The handler is very simple for now: either pass a capnp message along
   to the client or call updateStateFromDB.
   ***/
   auto pushNotif = [writeFunc, this](NotifStruct notif)->void
   {
      switch (notif.type)
      {
         case NotifType::PUSH:
         {
            if (notif.packet.empty()) {
               throw std::runtime_error("empty packet in push notif!");
            }
            writeFunc(notif.packet);
            return;
         }

         case NotifType::UPDATE:
         {
            if (notif.lbd == nullptr) {
               throw std::runtime_error("notif lbd is not set!");
            }
            updateStateFromDB(notif.lbd);
            return;
         }

         default:
            throw std::runtime_error("invalid pushNotif type");
      }
   };
   callbackPtr_ = std::make_shared<Callback>(pushNotif);
}

////
std::shared_ptr<Callback> WalletManager::getBdvCallback() const
{
   return callbackPtr_;
}

////
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
   callbackPtr_->notifySetupRegistrationDone();
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::registerWallet(const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId, bool isNew)
{
   auto container = getWalletContainer(wltId, accId);
   auto dbId = container->getDbId();

   try {
      callbackPtr_->registerRefreshCallback(dbId,
         [this, dbId]() {
            updateStateFromDB(
               [this, dbId]() {
                  callbackPtr_->notifyRefresh({dbId});
               });
         });
      container->registerWithBDV(isNew);
   } catch (const OfflineException& e) {
      callbackPtr_->unregisterCallback(dbId);
      throw e;
   }
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<WalletContainer> WalletManager::addAccount(
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

////
void WalletManager::addAllAccounts(std::shared_ptr<Wallets::AssetWallet> wltPtr)
{
   const auto& accIds = wltPtr->getAccountIDs();
   for (const auto& accId : accIds) {
      addAccount(wltPtr, accId);
   }
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<WalletContainer> WalletManager::createNewWallet(
   const SecureBinaryData& extraEntropy,
   const Wallets::IO::CreateWalletParams& params)
{
   auto root = Cryptography::PRNG::generateRandomStrong(32);
   if (!extraEntropy.empty()) {
      auto hashTropy = BtcUtils::getHash256(extraEntropy);
      root.XOR(hashTropy);
   }

   auto seed = std::make_unique<Seeds::ClearTextSeed_Armory>(
      root, SecureBinaryData{}, Seeds::LegacyType::Armory200);
   auto wallet = Wallets::AssetWallet_Single::createFromSeed(
      std::move(seed), params);
   return addAccount(wallet, wallet->getMainAccountID());
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
         acc.second->unregisterFromBDV();
      } catch (const OfflineException&) {
         //we do not care if the unregister operation fails
      }
   }

   //remove containers from map;
   wallets_.erase(wltId);
   return path;
}

////////////////////////////////////////////////////////////////////////////////
void WalletManager::deleteWallet(const Wallets::WalletId& wltId)
{
   ReentrantLock lock(this);
   auto wltCont = getWalletContainer(wltId);
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

////////////////////////////////////////////////////////////////////////////////
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
   }
}

////////
std::map<std::string, std::shared_ptr<WalletFileInfo>>
WalletManager::listWallets()
{
   //iterate over files in datadir
   std::vector<std::filesystem::path> walletPaths, a135Paths;
   for (const auto& dirEntry : std::filesystem::directory_iterator{path_} ) {
      const auto& path = dirEntry.path();

      //ignore folders
      if (FileUtils::isDir(path)) {
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
void WalletManager::unlockControlHeader(const std::string& path,
   const Passphrase::UnlockFunc& lbd)
{
   //sanity checks
   if (path.empty() || lbd == nullptr) {
      throw std::runtime_error("tried to unlock control header with empty id/lambda");
   }

   auto iter = walletFiles_.find(path);
   if (iter == walletFiles_.end()) {
      throw std::runtime_error("this file is not a known wallet: " + path);
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
         "tried to unlock control header with empty id/lambda");
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
   auto container = getWalletContainer(wltId, accId);
   container->extendAddressChain(count, progressFunc);
   try {
      registerWallet(wltId, accId, isNew);
   } catch (const OfflineException&) {
      //if we are not connected to a db, we are done, notify the caller
      callbackPtr_->notifyRefresh({container->getDbId()});
   }
}

////
std::shared_ptr<AddressEntry> WalletManager::getNewAddress(
   const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId,
   uint32_t addrType, uint32_t addrKind)
{
   using namespace Armory::Codec::Bridge;

   bool wasExtended = false;
   auto progFunc = [&wasExtended](int)
   {
      wasExtended = true;
   };

   auto wltContainer = getWalletContainer(wltId, accId);
   auto wltPtr = wltContainer->getWalletPtr();
   auto accPtr = wltContainer->getAddressAccount();

   std::shared_ptr<AddressEntry> addrPtr;
   switch (addrKind)
   {
      case WalletRequest::AddressRequest::NEW:
      {
         addrPtr = accPtr->getNewAddress(
            wltPtr->getIface(), (AddressEntryType)addrType, progFunc);
         break;
      }

      case WalletRequest::AddressRequest::CHANGE:
      {
         addrPtr = accPtr->getNewChangeAddress(
            wltPtr->getIface(), (AddressEntryType)addrType, progFunc);
         break;
      }

      case WalletRequest::AddressRequest::PEEK_CHANGE:
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
         //if we are not connected to a db, we are done, notify the caller
         callbackPtr_->notifyRefresh({wltContainer->getDbId()});
      }
   }
   return addrPtr;
}
