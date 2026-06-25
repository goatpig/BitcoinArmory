////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <map>
#include <Wallets/WalletIdTypes.h>
#include <Wallets/GetPassphrase.h>
#include <Utils/Types.h>

namespace AsyncClient
{
   class BtcWallet;
   class BlockDataViewer;
   struct CombinedBalances;
}

template<class U> class ReturnMessage;
class AddressEntry;
class AddressBookEntry;
struct UTXO;
class TxIOPair;

namespace Armory
{
   namespace Accounts
   {
      class AddressAccount;
   }

   namespace Wallets
   {
      class AssetWallet;
   }

   namespace Seeds
   {
      class WalletBackup;
   }

   namespace Bridge
   {
      class TxIOCache;
      struct ChainData;

      struct Amounts
      {
         const Types::Amount fullBalance;
         const Types::Amount spendableBalance;
         const Types::Amount unconfirmedBalance;
         const size_t txCount;
      };

      ////////
      struct OfflineException
      {};

      class WalletContainer
      {
         friend class WalletManager;

      private:
         const Wallets::WalletId wltId_;
         const Wallets::AddressAccountId accountId_;
         std::shared_ptr<TxIOCache> cache_;

         std::string dbId_;
         std::shared_ptr<Wallets::AssetWallet> wallet_;

         std::shared_ptr<AsyncClient::BlockDataViewer> bdvPtr_;
         std::shared_ptr<AsyncClient::BtcWallet> asyncWlt_;

         std::unique_ptr<ChainData> chainDataMain_;
         std::unique_ptr<ChainData> chainDataZC_;

         std::map<Wallets::AssetAccountId, Wallets::AssetKeyType>
            highestUsedIndex_;
         std::map<BinaryData, std::shared_ptr<AddressEntry>> updatedAddressMap_;

      private:
         WalletContainer(
            const Wallets::WalletId&,
            const Wallets::AddressAccountId&,
            const std::shared_ptr<TxIOCache>);

         void resetCache(void);
         void setBdvPtr(std::shared_ptr<AsyncClient::BlockDataViewer>);
         void cleanupBDV(void);
         void setWalletPtr(std::shared_ptr<Wallets::AssetWallet>,
            const Wallets::AddressAccountId&);
         void eraseFromDisk(void);

      public:
         void registerWithBDV(bool isNew);
         void unregisterFromBDV(void);
         const std::string& getDbId(void) const;

         virtual std::shared_ptr<Wallets::AssetWallet>
            getWalletPtr(void) const;
         std::shared_ptr<Accounts::AddressAccount>
            getAddressAccount(void) const;
         const Wallets::WalletId& getWalletId(void) const;
         const Wallets::AddressAccountId& getAccountId(void) const;

         void synchronizeAddressChainState(void);
         void extendAddressChain(unsigned, const std::function<void(int)>&);
         void extendAddressChainToIndex(unsigned);
         bool hasScrAddr(const Types::ScrAddr&) const;
         bool hasAddress(const std::string&) const;

         std::vector<AddressBookEntry> getAddressBook(void) const;
         const std::map<Types::TxIOKey, TxIOPair> getTxioMap(void) const;
         void resolveTxios(uint32_t);
         void resolveZcTxios(void);
         std::vector<UTXO> getUTXOs(Types::Amount, bool, bool);

         Types::Amount getFullBalance(void) const;
         Types::Amount getSpendableBalance(void) const;
         Types::Amount getUnconfirmedBalance(void) const;
         size_t getTxCount(void) const;

         std::map<Types::ScrAddr, Amounts> getAddrBalanceMap(void) const;
         Wallets::AssetKeyType getHighestUsedIndex(void) const;
         std::map<Types::ScrAddr, std::shared_ptr<AddressEntry>>
         getUpdatedAddressMap(void);

         std::unique_ptr<Seeds::WalletBackup> getBackupStrings(
            bool, const Passphrase::UnlockFunc&) const;
         void changePassphrase(const Passphrase::UnlockFunc&,
            Passphrase::SetNew&, bool);

         void setComment(const std::string&, const std::string&);
         void setLabels(const std::string&, const std::string&);

         const Wallets::EncryptionKeyId& getDefaultEncryptionKeyId() const;
         std::filesystem::path forkWatchingOnly(const Passphrase::SetNew&);
      };
   } //namespace Bridge
} //namespace Armory
