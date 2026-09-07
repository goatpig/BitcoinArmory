////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <string>
#include <map>
#include <filesystem>

#include <Utils/ReentrantLock.h>
#include "Loader.h"
#include "Container.h"

class Tx;
class NewBlockNotif;

namespace Armory
{
   namespace Seeds
   {
      enum class SeedType : int;
      class WalletBackup;
   };

   namespace Accounts
   {
      class AddressAccount;
   }

   namespace Wallets
   {
      class WalletId;
      class AddressAccountId;
      class AssetWallet;
      class EncryptionKeyId;

      namespace IO
      {
         struct CreateWalletParams;
         struct ReadOnlyFileParams;
      }
   };

   namespace Ledgers
   {
      class Entry;
      class Delegate;
      class DBCache;
   }

   ////////
   namespace Bridge
   {
      class Callback;
      class TxIOCache;
      struct NotifStruct;
      using AAIdSet = std::set<Wallets::AddressAccountId>;

      class WalletManager : public Lockable
      {
      private:
         const std::filesystem::path path_;
         std::map<std::string, std::shared_ptr<WalletFileInfo>> walletFiles_;

         std::map<Wallets::WalletId, std::map<
            Wallets::AddressAccountId,
            std::shared_ptr<WalletContainer>>> wallets_;
         std::map<std::string, std::shared_ptr<WalletContainer>> walletsByDbId_;

         std::shared_ptr<Callback> callbackPtr_;
         std::function<void(void)> cleanupCallback_;
         std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr_;
         bool automatesDB_ = false;

         //history
         std::shared_ptr<TxIOCache> txioCache_;
         std::map<std::string, Ledgers::Delegate> delegateMap_;
         std::map<Wallets::WalletId, AAIdSet> mainLedgerFilter_;

      private:
         void initAfterLock(void) override {}
         void cleanUpBeforeUnlock(void) override {}
         std::shared_ptr<WalletContainer> addAccount(
            std::shared_ptr<Wallets::AssetWallet>,
            const Wallets::AddressAccountId&);
         void addAllAccounts(std::shared_ptr<Wallets::AssetWallet>);
         void loadAFile(const std::filesystem::path&);

      public:
         WalletManager(const std::filesystem::path&);

         /* pre wallets loading calls */
         std::map<std::string, std::shared_ptr<WalletFileInfo>> listWallets(void);
         void unlockControlHeader(const std::filesystem::path&,
            const Passphrase::UnlockFunc&);
         const Wallets::WalletId& migrateWallet(const std::filesystem::path&,
            const Passphrase::UnlockFunc&,
            const Wallets::IO::CreateWalletParams&
         );
         bool stageWallet(const Wallets::WalletId&, bool);
         void loadWallets(void);
         std::shared_ptr<WalletFileInfo> importFile(const std::filesystem::path&);

         /* connection setup */
         void setBdvCallback(
            const std::function<void(BinaryData&)>&);
         void setCleanupCallback(
            const std::function<void(void)>&);
         std::shared_ptr<Callback> getBdvCallback(void) const;
         void setBdvPtr(std::shared_ptr<AsyncClient::BlockDataViewer>, bool=false);
         void cleanupBDV(void);

         /* utils */
         const std::filesystem::path& getWalletDir(void) const;
         void updateStateFromDB(std::shared_ptr<NotifStruct>);
         std::shared_ptr<const TxIOCache> txioCache(void) const;

         /* loaded wallet getters */
         bool hasWallet(const Wallets::WalletId&);
         std::shared_ptr<WalletContainer> getWalletContainer(
            const Wallets::WalletId&) const;
         std::shared_ptr<WalletContainer> getWalletContainer(
            const Wallets::WalletId&, const Wallets::AddressAccountId&) const;
         AAIdSet getAddressAccountIds(const Wallets::WalletId&) const;
         const std::map<std::string, std::shared_ptr<WalletContainer>>&
         getWalletContainerMap(void) const;

         /* wallet add/create/delete */
         void loadWallet(const Wallets::IO::ReadOnlyFileParams&);
         Wallets::WalletId createNewWallet(
            Seeds::SeedType,
            const SecureBinaryData&, //extra entropy
            const Wallets::IO::CreateWalletParams&);
         std::filesystem::path unloadWallet(const Wallets::WalletId&);
         void deleteWallet(const Wallets::WalletId&);
         void registerWallets(void);
         void registerWallet(const Wallets::WalletId&,
            const Wallets::AddressAccountId&, bool);

         /* address creation */
         void extendAddressChain(const Wallets::WalletId&,
            const Wallets::AddressAccountId&,
            unsigned, bool,
            std::function<void(int)>
         );
         std::shared_ptr<AddressEntry> getNewAddress(
            const Wallets::WalletId&,
            const Wallets::AddressAccountId&,
            uint32_t, uint32_t);

         /* ledgers */
         std::shared_ptr<const Ledgers::DBCache> getDbCache(void) const;
         const std::string& getDelegateId(void);
         const std::string& getDelegateIdForWallet(
            const Wallets::WalletId&, const Wallets::AddressAccountId&);
         const std::string& getDelegateIdForScrAddr(
            const Wallets::WalletId&, const Wallets::AddressAccountId&,
            const Types::ScrAddr&);

         std::map<std::string, std::shared_ptr<WalletContainer>>
         getFilteredContainerMap(void) const;
         void updateMainLedgerFilter(
            const std::map<Wallets::WalletId, AAIdSet>&);

         uint32_t getPageCountForDelegate(const std::string&) const;
         std::vector<Ledgers::Entry> getPageForDelegate(
            const std::string&, uint32_t) const;
      };
   } //namespace Bridge
} //namespace Armory
