////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <functional>
#include <string>
#include <map>
#include <set>

#include <Utils/ReentrantLock.h>
#include <Utils/SecureBinaryData.h>
#include "../WalletIdTypes.h"
#include "../AddressEntryType.h"

#define ASSET_ACCOUNT_PREFIX        0xE1
#define ASSET_COUNT_PREFIX          0xE2
#define ASSET_TOP_INDEX_PREFIX_V1   0xE3
#define ASSET_TOP_INDEX_PREFIX_V2   0xE4

////////////////////////////////////////////////////////////////////////////////
namespace Armory
{
   namespace Assets
   {
      enum class DerivationSchemeType : int;
      class DerivationScheme;
      struct Asset_PrivateKey;
   };

   namespace Wallets
   {
      namespace IO
      {
         class WalletIfaceTransaction;
         class WalletDBInterface;
      };

      namespace Encryption
      {
         class DecryptedDataContainer;
         class Cipher;
      };
   };

   namespace Accounts
   {
      enum class AssetAccountType : int;

      using ProgressFunc = std::function<void(int)>;
      using AssetPtr = std::shared_ptr<Assets::AssetEntry>;

      struct AssetAccountData
      {
      public:
         const AssetAccountType type_;
         Wallets::AssetAccountId id_;

         AssetPtr root_;
         std::shared_ptr<Assets::DerivationScheme> derScheme_;

         const std::string dbName_;

         std::map<Wallets::AssetKeyType, AssetPtr> assets_;
         Wallets::AssetKeyType lastUsedIndex_ = -1;

         //<assetID, <address type, prefixed address hash>>
         using AddrHashMapType = std::map<Wallets::AssetId,
            std::map<AddressEntryType, BinaryData>>;
         AddrHashMapType addrHashMap_;
         Wallets::AssetKeyType lastHashedAsset_ = -1;

      public:
         AssetAccountData(
            const AssetAccountType,
            const Wallets::AssetAccountId&,
            AssetPtr,
            std::shared_ptr<Assets::DerivationScheme>,
            const std::string&);

         std::shared_ptr<AssetAccountData> copy(const std::string&) const;
      };

      //////////////////////////////////////////////////////////////////////////
      struct AssetAccountExtendedData
      {
         virtual ~AssetAccountExtendedData(void) = 0;
      };

      struct AssetAccountSaltMap : public AssetAccountExtendedData
      {
         std::map<Wallets::AssetKeyType, SecureBinaryData> salts_;
         ~AssetAccountSaltMap(void) override;
      };

      ////
      struct AssetAccountPublicData
      {
         const Wallets::AssetAccountId id_;

         const SecureBinaryData rootData_;
         const SecureBinaryData derivationData_;

         const Wallets::AssetKeyType lastUsedIndex_;
         const Wallets::AssetKeyType lastComputedIndex_;

         std::shared_ptr<AssetAccountExtendedData> extendedData;
      };

      //////////////////////////////////////////////////////////////////////////
      class AssetAccount : protected Lockable
      {
         friend class AssetAccount_ECDH;
         friend class AssetAccount_Imports;
         friend class AssetAccount_ImportsWO;
         friend class AddressAccount;

      private:
         std::shared_ptr<AssetAccountData> data_;

      private:
         size_t writeAssetEntry(AssetPtr,
            std::shared_ptr<Wallets::IO::WalletDBInterface>);
         void updateOnDiskAssets(
            std::shared_ptr<Wallets::IO::WalletDBInterface>);

         void updateHighestUsedIndex(
            std::shared_ptr<Wallets::IO::WalletDBInterface>);
         unsigned getAndBumpHighestUsedIndex(
            std::shared_ptr<Wallets::IO::WalletDBInterface>);

         virtual void commit(
            std::shared_ptr<Wallets::IO::WalletDBInterface>);
         void updateAssetCount(
            std::shared_ptr<Wallets::IO::WalletDBInterface>);

         void extendPublicChainToIndex(
            std::shared_ptr<Wallets::IO::WalletDBInterface>, int32_t,
            const ProgressFunc&);
         void extendPublicChain(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            AssetPtr, int32_t,
            const ProgressFunc&);
         std::vector<AssetPtr> extendPublicChain(
            AssetPtr, int32_t, int32_t,
            const ProgressFunc&);

         void extendPrivateChain(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            std::shared_ptr<Wallets::Encryption::DecryptedDataContainer>,
            int32_t);
         void extendPrivateChainToIndex(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            std::shared_ptr<Wallets::Encryption::DecryptedDataContainer>,
            int32_t);
         void extendPrivateChain(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            std::shared_ptr<Wallets::Encryption::DecryptedDataContainer>,
            AssetPtr, int32_t);
         std::vector<AssetPtr> extendPrivateChain(
            std::shared_ptr<Wallets::Encryption::DecryptedDataContainer>,
            AssetPtr,
            int32_t, int32_t);

         AssetPtr getOrSetAssetAtIndex(
            std::shared_ptr<Wallets::IO::WalletDBInterface>, unsigned,
            const ProgressFunc&);
         AssetPtr getNewAsset(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            const ProgressFunc&);
         AssetPtr peekNextAsset(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            const ProgressFunc&);

         std::shared_ptr<Assets::Asset_PrivateKey> fillPrivateKey(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            std::shared_ptr<Wallets::Encryption::DecryptedDataContainer>,
            const Wallets::AssetId&);

         virtual unsigned getLookup(void) const;
         virtual AssetAccountType type(void) const;

      public:
         AssetAccount(std::shared_ptr<AssetAccountData>);

         size_t getAssetCount(void) const;
         int32_t getLastComputedIndex(void) const;
         int32_t getHighestUsedIndex(void) const;
         void setHighestUsedIndex(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            const Wallets::AssetKeyType&);
         bool isAssetInUse(const Wallets::AssetId&) const;
         AssetPtr getLastAssetWithPrivateKey(void) const;

         AssetPtr getAssetForID(const Wallets::AssetId&) const;
         AssetPtr getAssetForKey(const Wallets::AssetKeyType&) const;
         bool isAssetIDValid(const Wallets::AssetId&) const;

         virtual void updateAddressHashMap(const std::set<AddressEntryType>&);
         virtual void updateAddressHashMap(
            const std::map<Wallets::AssetId, AddressEntryType>&);
         const AssetAccountData::AddrHashMapType& getAddressHashMap(void) const;

         const Wallets::AssetAccountId& getID(void) const;
         const SecureBinaryData& getChaincode(void) const;
         AssetPtr getRoot(void) const;

         void extendPublicChain(
            std::shared_ptr<Wallets::IO::WalletDBInterface>, int32_t,
            const std::function<void(int)>& = nullptr);

         //static
         static std::shared_ptr<AssetAccountData> loadFromDisk(
            const BinaryData& key,
            std::shared_ptr<Wallets::IO::WalletIfaceTransaction>);

         //Lockable virtuals
         void initAfterLock(void) {}
         void cleanUpBeforeUnlock(void) {}
      };

      //////////////////////////////////////////////////////////////////////////
      class AssetAccount_ECDH : public AssetAccount
      {
      private:
         unsigned getLookup(void) const override;
         AssetAccountType type(void) const override;
         void commit(std::shared_ptr<Wallets::IO::WalletDBInterface>) override;

      public:
         AssetAccount_ECDH(std::shared_ptr<AssetAccountData>);

         Wallets::AssetKeyType addSalt(
            std::shared_ptr<Wallets::IO::WalletIfaceTransaction>,
            const SecureBinaryData&);
         Wallets::AssetKeyType getSaltIndex(
            const SecureBinaryData&) const;
      };

      //////////////////////////////////////////////////////////////////////////
      class AssetAccount_Imports : public AssetAccount
      {
      private:
         unsigned getLookup(void) const override;
         AssetAccountType type(void) const override;

      public:
         AssetAccount_Imports(std::shared_ptr<AssetAccountData>);

         //virtuals
         void updateAddressHashMap(const std::set<AddressEntryType>&) override;
         void updateAddressHashMap(
            const std::map<Wallets::AssetId, AddressEntryType>&) override;

         //imports
         Wallets::AssetId importPrivateKey(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            std::shared_ptr<Wallets::Encryption::DecryptedDataContainer>,
            const SecureBinaryData&,
            std::unique_ptr<Wallets::Encryption::Cipher>);
      };

      //////////////////////////////////////////////////////////////////////////
      class AssetAccount_ImportsWO : public AssetAccount
      {
      private:
         unsigned getLookup(void) const override;
         AssetAccountType type(void) const override;

      public:
         AssetAccount_ImportsWO(std::shared_ptr<AssetAccountData>);

         //virtuals
         void updateAddressHashMap(const std::set<AddressEntryType>&) override;
         void updateAddressHashMap(
            const std::map<Wallets::AssetId, AddressEntryType>&) override;

         //imports
         Wallets::AssetId importPublicKey(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            SecureBinaryData&);
         Wallets::AssetId importScriptHash(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            const BinaryData&);
         Wallets::AssetId importRawScript(
            std::shared_ptr<Wallets::IO::WalletDBInterface>,
            const BinaryData&);
      };

   }; //namespace Accounts
}; //namespace Armory
