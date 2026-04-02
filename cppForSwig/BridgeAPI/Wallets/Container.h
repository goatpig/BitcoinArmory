////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <map>
#include <Wallets/WalletIdTypes.h>
#include <Wallets/GetPassphrase.h>

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
         const Wallets::AddressAccountId& getAccountId(void) const;

         void synchronizeAddressChainState(void);
         void extendAddressChain(unsigned, const std::function<void(int)>&);
         void extendAddressChainToIndex(unsigned);
         bool hasScrAddr(const BinaryData&) const;
         bool hasAddress(const std::string&) const;

         std::vector<AddressBookEntry> getAddressBook(void) const;
         const std::map<BinaryData, TxIOPair> getTxioMap(void) const;
         void resolveTxios(uint32_t);
         void resolveZcTxios(void);
         std::vector<UTXO> getUTXOs(uint64_t, bool, bool);

         uint64_t getFullBalance(void) const;
         uint64_t getSpendableBalance(void) const;
         uint64_t getUnconfirmedBalance(void) const;
         uint64_t getTxIOCount(void) const;

         std::map<BinaryData, std::vector<uint64_t>> getAddrBalanceMap(void) const;
         Wallets::AssetKeyType getHighestUsedIndex(void) const;
         std::map<BinaryData, std::shared_ptr<AddressEntry>> getUpdatedAddressMap();

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
