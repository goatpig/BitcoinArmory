////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _WALLETS_H
#define _WALLETS_H

#include <atomic>
#include <thread>
#include <memory>
#include <set>
#include <map>
#include <string>
#include <filesystem>

#include "ReentrantLock.h"
#include "BinaryData.h"
#include "EncryptionUtils.h"
#include "WalletIdTypes.h"
#include "Script.h"
#include "Signer.h"
#include "Progress.h"

#include "DecryptedDataContainer.h"
#include "BIP32_Node.h"
#include "ResolverFeed.h"

#include "WalletHeader.h"
#include "Accounts/AccountTypes.h"
#include "Accounts/AddressAccounts.h"
#include "Accounts/MetaAccounts.h"

////
namespace Armory
{
   namespace Signing
   {
      class BIP32_AssetPath;
   }

   namespace Seeds
   {
      class EncryptedSeed;
      class ClearTextSeed;
      class ClearTextSeed_Armory135;
      class ClearTextSeed_BIP32;
   }

   namespace Wallets
   {
      namespace IO
      {
         class WalletDBInterface;
         struct WalletHeader;
         struct OpenFileParams;
         struct CreationParams;
      };

      //////////////////////////////////////////////////////////////////////////
      struct WalletPublicData
      {
      public:
         const std::string dbName_;
         const std::string masterID_;
         const std::string walletID_;
         const AddressAccountId mainAccountID_;

         std::shared_ptr<Assets::AssetEntry_Single> pubRoot_{};
         std::map<AddressAccountId,
            Accounts::AddressAccountPublicData> accounts_{};
         std::map<Accounts::MetaAccountType,
            std::shared_ptr<Accounts::MetaDataAccount>> metaAccounts_{};

      public:
         WalletPublicData(const std::string&, const std::string&,
            const std::string&, const AddressAccountId&);
      };

      //////////////////////////////////////////////////////////////////////////
      class AssetWallet : protected Lockable
      {
         friend class ResolverFeed_AssetWalletSingle;
         friend class ResolverFeed_AssetWalletSingle_ForMultisig;

      private:
         virtual void initAfterLock(void) {}
         virtual void cleanUpBeforeUnlock(void) {}

         static std::string getMasterID(std::shared_ptr<IO::WalletDBInterface>);
         void checkMasterID(const std::string& masterID);

      protected:
         std::shared_ptr<IO::WalletDBInterface> iface_;
         const std::string dbName_;

         std::shared_ptr<Encryption::DecryptedDataContainer> decryptedData_;
         std::map<AddressAccountId,
            std::shared_ptr<Accounts::AddressAccount>> accounts_;
         std::map<Accounts::MetaAccountType, std::shared_ptr<
            Accounts::MetaDataAccount>> metaDataAccounts_;

         AddressAccountId mainAccount_;

         ////
         std::string walletID_;
         std::string masterID_;

         ////
         std::string label_;
         std::string description_;

      protected:
         //tors
         AssetWallet(std::shared_ptr<IO::WalletDBInterface>,
            std::shared_ptr<IO::WalletHeader>, const std::string&);

         static std::shared_ptr<IO::WalletDBInterface> getIfaceFromFile(
            const IO::OpenFileParams&, const Progress::Func& prog=nullptr);

         //locals
         AddressEntryType getAddrTypeForAccount(const AssetId&) const;
         void loadMetaAccounts(void);

         //virtual
         virtual void readFromFile(void) = 0;

         //static
         static BinaryDataRef getDataRefForKey(
            IO::DBIfaceTransaction*, const BinaryData& key);

      public:
         //tors
         virtual ~AssetWallet() = 0;
         void shutdown(void);

         //local
         std::shared_ptr<AddressEntry> getNewAddress(
            AddressEntryType aeType = AddressEntryType_Default);
         std::shared_ptr<AddressEntry> getNewAddress(
            const AddressAccountId&,
            AddressEntryType aeType = AddressEntryType_Default);
         std::shared_ptr<AddressEntry> getNewAddress(
            const AssetAccountId&,
            AddressEntryType aeType = AddressEntryType_Default);
         std::shared_ptr<AddressEntry> getNewChangeAddress(
            AddressEntryType aeType = AddressEntryType_Default);
         std::shared_ptr<AddressEntry> peekNextChangeAddress(
            AddressEntryType aeType = AddressEntryType_Default);
         void updateAddressEntryType(const AssetId&,
            AddressEntryType);

         std::string getID(void) const;
         virtual ReentrantLock lockDecryptedContainer(void);
         bool isDecryptedContainerLocked(void) const;

         std::shared_ptr<Assets::AssetEntry> getAssetForID(
            const AssetId&) const;

         void extendPublicChain(unsigned);
         void extendPublicChainToIndex(const AddressAccountId&, unsigned,
            const std::function<void(int)>& = nullptr);
         void extendPrivateChain(unsigned);
         void extendPrivateChainToIndex(const AddressAccountId&, unsigned);

         bool hasScrAddr(const BinaryData& scrAddr) const;
         bool hasAddrStr(const std::string& scrAddr) const;
         bool isAssetUsed(const AssetId&) const;

         const std::pair<AssetId, AddressEntryType>&
            getAssetIDForAddrStr(const std::string& scrAddr) const;
         const std::pair<AssetId, AddressEntryType>&
            getAssetIDForScrAddr(const BinaryData& scrAddr) const;

         AddressEntryType getAddrTypeForID(const AssetId&) const;
         std::shared_ptr<AddressEntry> getAddressEntryForID(
            const AssetId&) const;

         void setPassphrasePromptLambda(PassphraseLambda lambda)
         {
            decryptedData_->setPassphrasePromptLambda(lambda);
         }

         void resetPassphrasePromptLambda(void)
         {
            decryptedData_->resetPassphraseLambda();
         }

         void addMetaAccount(Accounts::MetaAccountType);
         std::shared_ptr<Accounts::MetaDataAccount> getMetaAccount(
            Accounts::MetaAccountType) const;
         std::shared_ptr<Accounts::AddressAccount> getAccountForID(
            const AddressAccountId& ID) const;

         const std::filesystem::path& getDbFilename(void) const;
         const std::string& getDbName(void) const;

         std::set<AddressAccountId> getAccountIDs(void) const;
         std::map<AssetId, std::shared_ptr<AddressEntry>>
            getUsedAddressMap(void) const;

         std::shared_ptr<Accounts::AddressAccount> createAccount(
            std::shared_ptr<Accounts::AccountType>,
            const Progress::Func& prog);

         void addSubDB(const std::string& dbName, const PassphraseLambda&);
         std::shared_ptr<IO::WalletIfaceTransaction> beginSubDBTransaction(
            const std::string&, bool);

         void changeControlPassphrase(
            const std::function<SecureBinaryData(void)>&,
            const PassphraseLambda&);
         void eraseControlPassphrase(const PassphraseLambda&);

         void setComment(const BinaryData&, const std::string&);
         const std::string& getComment(const BinaryData&) const;
         std::map<BinaryData, std::string> getCommentMap(void) const;
         void deleteComment(const BinaryData&);

         const AddressAccountId& getMainAccountID(void) const;
         const EncryptionKeyId& getDefaultEncryptionKeyId(void) const;
         std::shared_ptr<Encryption::KeyDerivationFunction>
            getDefaultKdf(void) const;

         void setLabel(const std::string&);
         void setDescription(const std::string&);

         const std::string& getLabel(void) const;
         const std::string& getDescription(void) const;

         std::shared_ptr<IO::WalletDBInterface> getIface(void) const;

         //virtual
         virtual std::set<BinaryData> getAddrHashSet();
         virtual const SecureBinaryData& getDecryptedValue(
            std::shared_ptr<Encryption::EncryptedAssetData>) = 0;
         virtual std::shared_ptr<Assets::AssetEntry> getRoot(void) const = 0;

         //static
         static void setMainWallet(
            std::shared_ptr<IO::WalletDBInterface>, const std::string&);
         static std::string getMainWalletID(
            std::shared_ptr<IO::WalletDBInterface>);

         static std::filesystem::path forkWatchingOnly(
            const IO::OpenFileParams&);
         static std::shared_ptr<AssetWallet> loadMainWalletFromFile(
            const IO::OpenFileParams&);
         static void eraseFromDisk(AssetWallet*);
      };

      //////////////////////////////////////////////////////////////////////////
      class AssetWallet_Single : public AssetWallet
      {
         friend class AssetWallet;
         friend class AssetWallet_Multisig;

      protected:
         std::shared_ptr<Assets::AssetEntry_Single> root_ = nullptr;
         std::shared_ptr<Seeds::EncryptedSeed> seed_ = nullptr;

      protected:
         //virtual
         void readFromFile(void);

         //static
         static std::shared_ptr<AssetWallet_Single> initWalletDb(
            std::shared_ptr<IO::WalletDBInterface> iface,
            const std::string& masterID, const std::string& walletID,
            const SecureBinaryData& privateRoot,
            const SecureBinaryData& chaincode,
            const IO::CreationParams&,
            uint32_t seedFingerprint);

         static std::shared_ptr<AssetWallet_Single> initWalletDbWithPubRoot(
            std::shared_ptr<IO::WalletDBInterface>,
            const std::string& masterID, const std::string& walletID,
            std::shared_ptr<Assets::AssetEntry_Single> pubRoot,
            const IO::CreationParams&);

      private:
         static void importPublicData(const WalletPublicData&,
            std::shared_ptr<IO::WalletDBInterface>);

         void setSeed(std::unique_ptr<Armory::Seeds::ClearTextSeed>,
            const SecureBinaryData&);

         //wallet creation private statics
         static std::shared_ptr<AssetWallet_Single> createFromSeed(
            Seeds::ClearTextSeed_Armory135*,
            const IO::CreationParams&);

         static std::shared_ptr<AssetWallet_Single> createFromSeed(
            Seeds::ClearTextSeed_BIP32*,
            const IO::CreationParams&);

      public:
         //tors
         AssetWallet_Single(std::shared_ptr<IO::WalletDBInterface>,
            std::shared_ptr<IO::WalletHeader>, const std::string&);

         //locals
         void addPrivateKeyPassphrase(
            const std::function<SecureBinaryData(void)>&);
         void changePrivateKeyPassphrase(
            const std::function<SecureBinaryData(void)>&);
         void erasePrivateKeyPassphrase(void);

         std::shared_ptr<Assets::AssetEntry> getRoot(void) const override
         { return root_; }

         const SecureBinaryData& getPublicRoot(void) const;
         const SecureBinaryData& getArmory135Chaincode(void) const;

         const AddressAccountId& createBIP32Account(
            std::shared_ptr<Accounts::AccountType_BIP32>,
            const Progress::Func& prog=nullptr);

         const SecureBinaryData& getDecryptedPrivateKeyForAsset(
            std::shared_ptr<Assets::AssetEntry_Single>);
         const AssetId& derivePrivKeyFromPath(
            const Signing::BIP32_AssetPath&);
         const SecureBinaryData& getDecryptedPrivateKeyForId(
            const AssetId&) const;

         bool isWatchingOnly(void) const;
         std::shared_ptr<Seeds::EncryptedSeed> getEncryptedSeed(void) const;

         Signing::BIP32_AssetPath getBip32PathForAsset(
            std::shared_ptr<Assets::AssetEntry>) const;
         Signing::BIP32_AssetPath getBip32PathForAssetID(
            const AssetId&) const;

         std::string getXpubForAssetID(const AssetId&) const;
         std::shared_ptr<Accounts::AccountType_BIP32>
            makeNewBip32AccTypeObject(const std::vector<uint32_t>&) const;

         //virtual
         const SecureBinaryData& getDecryptedValue(
            std::shared_ptr<Encryption::EncryptedAssetData>);

         //static
         static std::shared_ptr<AssetWallet_Single> createFromSeed(
            std::unique_ptr<Armory::Seeds::ClearTextSeed>,
            const IO::CreationParams&);

         static std::shared_ptr<AssetWallet_Single>
         createFromPublicRoot_Armory135(
            SecureBinaryData&, //pub root
            SecureBinaryData&, //chaincode
            const IO::CreationParams&);

         static std::shared_ptr<AssetWallet_Single> createBlank(
            const std::string& walletID, const IO::CreationParams&);

         static WalletPublicData exportPublicData(
            std::shared_ptr<AssetWallet_Single>);
         static void mergePublicData(const IO::OpenFileParams&,
            const WalletPublicData&);
      };

      //////////////////////////////////////////////////////////////////////////
      class AssetWallet_Multisig : public AssetWallet
      {
         friend class AssetWallet;

      private:
         std::atomic<unsigned> chainLength_;

      protected:
         //virtual
         void readFromFile(void);
         const SecureBinaryData& getDecryptedValue(
            std::shared_ptr<Encryption::EncryptedAssetData>);

      public:
         //tors
         AssetWallet_Multisig(std::shared_ptr<IO::WalletDBInterface>,
            std::shared_ptr<IO::WalletHeader>, const std::string&);

         //virtual
         std::shared_ptr<Assets::AssetEntry> getRoot(void) const override
         { return nullptr; }

         //static
         static std::shared_ptr<AssetWallet> createFromWallets(
            std::vector<std::shared_ptr<AssetWallet>> wallets,
            unsigned M,
            unsigned lookup = UINT32_MAX);

      };
   }; //namespace Wallets
}; //namespace Armory

#endif
