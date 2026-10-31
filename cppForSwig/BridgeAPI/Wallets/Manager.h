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
#include <Utils/ThreadSafeClasses.h>
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
      class TxIOCache;
      namespace Notifications
      {
         namespace Server
         {
            class NotifHandler;
            class Notif;
         }
      }

      using AAIdSet = std::set<Wallets::AddressAccountId>;
      using ContainerPtr = std::shared_ptr<WalletContainer>;
      using NotifQueue = Threading::BlockingQueue<
         std::unique_ptr<Notifications::Server::Notif>>;

      class WalletManager : public Lockable
      {
      private:
         const std::filesystem::path walletsDir_;
         const std::shared_ptr<NotifQueue> notifQueue_;

         std::map<std::string, std::shared_ptr<WalletFileInfo>> walletFiles_;
         std::map<Wallets::WalletId, std::map<
            Wallets::AddressAccountId, ContainerPtr>> wallets_;
         std::map<std::string, ContainerPtr> walletsByDbId_;
         std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr_;

         //history
         std::shared_ptr<TxIOCache> txioCache_;
         std::map<std::string, Ledgers::Delegate> delegateMap_;
         std::map<Wallets::WalletId, AAIdSet> mainLedgerFilter_;

      private:
         void initAfterLock(void) override {}
         void cleanUpBeforeUnlock(void) override {}
         ContainerPtr addAccount(
            std::shared_ptr<Wallets::AssetWallet>,
            const Wallets::AddressAccountId&);
         void addAllAccounts(std::shared_ptr<Wallets::AssetWallet>);
         void loadAFile(const std::filesystem::path&);
         void purgeZC(void);

      public:
         WalletManager(
            const std::filesystem::path&,
            std::shared_ptr<NotifQueue>);

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
         void setBdvPtr(std::shared_ptr<AsyncClient::BlockDataViewer>);
         void cleanupBdvPtr(void);

         /* utils */
         const std::filesystem::path& getWalletsDir(void) const;
         void updateStateFromDB(std::unique_ptr<Notifications::Server::Notif>);
         std::shared_ptr<const TxIOCache> txioCache(void) const;
         void addUnpooledTxs(const std::vector<BinaryData>&);
         void fetchTxs(std::shared_ptr<AsyncClient::BlockDataViewer>,
            const std::set<Types::TxHash>&);

         /* loaded wallet getters */
         bool hasWallet(const Wallets::WalletId&);
         ContainerPtr getContainer(const Wallets::WalletId&) const;
         ContainerPtr getContainer(const Wallets::WalletId&,
            const Wallets::AddressAccountId&) const;
         AAIdSet getAddressAccountIds(const Wallets::WalletId&) const;
         const std::map<std::string, ContainerPtr>& getContainerMap(void) const;

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

         std::map<std::string, ContainerPtr> getFilteredContainerMap(void) const;
         void updateMainLedgerFilter(
            const std::map<Wallets::WalletId, AAIdSet>&);

         uint32_t getPageCountForDelegate(const std::string&) const;
         std::vector<Ledgers::Entry> getPageForDelegate(
            const std::string&, uint32_t) const;
      };
   } //namespace Bridge
} //namespace Armory
