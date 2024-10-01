////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_ADDRESS_ACCOUNTS
#define _H_ADDRESS_ACCOUNTS

#include <set>
#include <map>
#include <memory>


#include "../../ReentrantLock.h"
#include "../../BinaryData.h"
#include "../WalletIdTypes.h"
#include "../../EncryptionUtils.h"
#include "../Assets.h"
#include "../Addresses.h"
#include "../DerivationScheme.h"

#include "AssetAccounts.h"

#define BIP32_OUTER_ACCOUNT_DERIVATIONID 0x00000000
#define BIP32_INNER_ACCOUNT_DERIVATIONID 0x00000001

#define ADDRESS_ACCOUNT_PREFIX 0xD0

////
namespace Armory
{
   namespace Signing
   {
      class BIP32_AssetPath;
   };

   namespace Wallets
   {
      class AssetWallet;
      class AssetWallet_Single;

      namespace IO
      {
         class WalletDBInterface;
         class WalletIfaceTransaction;
      };
   }

   namespace Accounts
   {
      //////////////////////////////////////////////////////////////////////////
      struct UnrequestedAddressException
      {};

      //////////////////////////////////////////////////////////////////////////
      struct AddressAccountPublicData
      {
         const Wallets::AddressAccountId ID_;

         const Wallets::AssetAccountId outerAccountId_;
         const Wallets::AssetAccountId innerAccountId_;

         AddressEntryType defaultAddressEntryType_ = AddressEntryType_P2PKH;
         std::set<AddressEntryType> addressTypes_;

         std::map<Wallets::AssetId,
            AddressEntryType> instantiatedAddressTypes_;

         //<accountID, <account data>
         std::map<Wallets::AssetAccountId,
            AssetAccountPublicData> accountDataMap_;

         AddressAccountPublicData(const Wallets::AddressAccountId&,
            const Wallets::AssetAccountId&,
            const Wallets::AssetAccountId&);
      };

      //////////////////////////////////////////////////////////////////////////
      class AddressAccount : public Lockable
      {
         friend class Wallets::AssetWallet;
         friend class Wallets::AssetWallet_Single;

      private:
         const std::string dbName_;
         const Wallets::AddressAccountId ID_;

         std::map<Wallets::AssetAccountId,
            std::shared_ptr<AssetAccountData>> accountDataMap_;
         std::map<Wallets::AssetId,
            AddressEntryType> instantiatedAddressTypes_;

         Wallets::AssetAccountId outerAccountId_;
         Wallets::AssetAccountId innerAccountId_;

         AddressEntryType defaultAddressEntryType_ = AddressEntryType_P2PKH;
         std::set<AddressEntryType> addressTypes_;

         //<prefixed address hash, <assetID, address type>>
         std::map<BinaryData,
            std::pair<Wallets::AssetId, AddressEntryType>> addressHashes_;

         std::map<Wallets::AssetAccountId,
            Wallets::AssetId> topHashedAssetId_;

         //temp placeholder for fetching comments
         std::function<const std::string&(const BinaryData&)> getComment_ = nullptr;

      private:
         AddressAccount(const std::string&,
            const Wallets::AddressAccountId&);

         //used for initial commit to disk
         void commit(std::shared_ptr<Wallets::IO::WalletDBInterface>);

         void addAccount(std::shared_ptr<AssetAccount>);
         void addAccount(std::shared_ptr<AssetAccountData>);

         void updateInstantiatedAddressType(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            std::shared_ptr<AddressEntry>);
         void updateInstantiatedAddressType(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            const Wallets::AssetId&, AddressEntryType);
         void eraseInstantiatedAddressType(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            const Wallets::AssetId&);

         void writeAddressType(std::shared_ptr<Wallets::IO::WalletDBInterface>,
            const Wallets::AssetId&, AddressEntryType);
         void writeAddressType(std::shared_ptr<Wallets::IO::DBIfaceTransaction>,
            const Wallets::AssetId&, AddressEntryType);

         std::shared_ptr<Assets::Asset_PrivateKey> fillPrivateKey(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            std::shared_ptr<Wallets::Encryption::DecryptedDataContainer>,
            const Wallets::AssetId&);

         std::shared_ptr<Assets::AssetEntry_BIP32Root> getBip32RootForAssetId(
            const Wallets::AssetId&) const;

         const std::shared_ptr<AssetAccountData>& getAccountDataForID(
            const Wallets::AssetAccountId&) const;

      public:
         const Wallets::AddressAccountId& getID(void) const { return ID_; }

         static std::unique_ptr<AddressAccount> make_new(
            const std::string&,
            std::shared_ptr<AccountType>,
            std::shared_ptr<Wallets::Encryption::DecryptedDataContainer>,
            std::unique_ptr<Wallets::Encryption::Cipher>,
            const std::function<std::shared_ptr<Assets::AssetEntry>(void)>&);

         static std::unique_ptr<AddressAccount> readFromDisk(
            std::shared_ptr<Wallets::IO::WalletIfaceTransaction>,
            const Wallets::AddressAccountId&);

         void extendPublicChain(
            std::shared_ptr<Wallets::IO::WalletDBInterface>, unsigned,
            const std::function<void(int)>& = nullptr);
         void extendPublicChain(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            const Wallets::AssetAccountId&, unsigned,
            const std::function<void(int)>& = nullptr);
         void extendPublicChainToIndex(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            const Wallets::AssetAccountId&, unsigned,
            const std::function<void(int)>& = nullptr);

         void extendPrivateChain(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            std::shared_ptr<Wallets::Encryption::DecryptedDataContainer>,
            unsigned);
         void extendPrivateChainToIndex(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            std::shared_ptr<Wallets::Encryption::DecryptedDataContainer>,
            const Wallets::AssetAccountId&, unsigned);

         std::shared_ptr<AddressEntry> getNewAddress(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            AddressEntryType aeType = AddressEntryType_Default);
         std::shared_ptr<AddressEntry> getNewAddress(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            const Wallets::AssetAccountId&, AddressEntryType);
         std::shared_ptr<AddressEntry> getNewChangeAddress(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            AddressEntryType aeType = AddressEntryType_Default);
         std::shared_ptr<AddressEntry> peekNextChangeAddress(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            AddressEntryType aeType = AddressEntryType_Default);
         bool isAssetChange(const Wallets::AssetId&) const;
         bool isAssetInUse(const Wallets::AssetId&) const;

         std::shared_ptr<Assets::AssetEntry> getOuterAssetRoot(void) const;


         AddressEntryType getDefaultAddressType(void) const
            { return defaultAddressEntryType_; }
         const std::set<AddressEntryType>& getAddressTypeSet(void) const
            { return addressTypes_; }
         bool hasAddressType(AddressEntryType);

         std::shared_ptr<Assets::AssetEntry> getAssetForID(
            const Wallets::AssetId&) const;

         const std::pair<Wallets::AssetId, AddressEntryType>&
            getAssetIDPairForAddr(const BinaryData&);
         const std::pair<Wallets::AssetId, AddressEntryType>&
            getAssetIDPairForAddrUnprefixed(const BinaryData&);

         void updateAddressHashMap(void);
         const std::map<BinaryData,
            std::pair<Wallets::AssetId, AddressEntryType>>&
               getAddressHashMap(void);

         size_t getNumAssetAccounts(void) const;
         std::set<Wallets::AssetAccountId> getAccountIdSet(void) const;
         std::unique_ptr<AssetAccount> getAccountForID(
            const Wallets::AssetId&) const;
         std::unique_ptr<AssetAccount> getAccountForID(
            const Wallets::AssetAccountId&) const;
         std::unique_ptr<AssetAccount> getOuterAccount(void) const;

         const Wallets::AssetAccountId& getOuterAccountID(void) const
         { return outerAccountId_; }
         const Wallets::AssetAccountId& getInnerAccountID(void) const
         { return innerAccountId_; }

         AddressAccountPublicData exportPublicData() const;
         void importPublicData(const AddressAccountPublicData&);

         std::shared_ptr<AddressEntry> getAddressEntryForID(
            const Wallets::AssetId&) const;
         std::map<Wallets::AssetId, std::shared_ptr<AddressEntry>>
            getUsedAddressMap(void) const;
         bool isAssetUsed(const Wallets::AssetId&) const;

         //Lockable virtuals
         void initAfterLock(void) {}
         void cleanUpBeforeUnlock(void) {}

         bool hasBip32Path(const Signing::BIP32_AssetPath&) const;
         bool isLegacy(void) const;
      };
   }; //namespace Accounts
}; //namespace Armory

#endif