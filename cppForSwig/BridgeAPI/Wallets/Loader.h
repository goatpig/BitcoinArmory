////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <memory>
#include <map>

#include <Utils/SecureBinaryData.h>
#include <Wallets/GetPassphrase.h>
#include <Wallets/WalletIdTypes.h>

#define A135_NOERROR          0
#define A135_ERROR_NOTAWALLET -1
#define A135_ERROR_MAGICBYTE  -2
#define A135_ERROR_CUSTOM     -3

namespace Armory
{
   namespace Wallets
   {
      class AssetWallet;
      class AssetWallet_Single;
      class AddressAccountId;

      namespace IO
      {
         struct CreateWalletParams;
      }
   }

   namespace Bridge
   {
      ////////////////////////////////////////////////////////////////////////////////
      enum class WalletLoadState : int
      {
         Unknown = 0,
         Legacy,
         Encrypted,
         Ready,
         Loaded
      };

      ////////////////////////////////////////////////////////////////////////////////
      class WalletFileInfo
      {
      private:
         const std::filesystem::path path_;
         WalletLoadState loadState_;
         bool staged_=false;

      protected:
         void setState(WalletLoadState);

      public:
         WalletFileInfo(const std::filesystem::path&, WalletLoadState);

         const std::filesystem::path& path(void) const;
         bool staged(void) const;
         void setStaged(bool);
         WalletLoadState state(void) const;

         virtual const Wallets::WalletId& walletId(void) const = 0;
         virtual bool hasAccountIds(void) const = 0;
         virtual std::set<Wallets::AddressAccountId> getAccountIds(void) const = 0;
         virtual const std::string& name(void) const = 0;
         virtual bool isWatchingOnly(void) const = 0;
      };

      ////////////////////////////////////////////////////////////////////////////////
      class LMDBWalletInfo : public WalletFileInfo
      {
      private:
         std::shared_ptr<Wallets::AssetWallet> wltPtr_;
         mutable Wallets::WalletId walletId_;
         mutable std::string name_;

      public:
         LMDBWalletInfo(const std::filesystem::path&,
            std::shared_ptr<Wallets::AssetWallet>,
            bool = false);

         void unlockControlHeader(const Passphrase::UnlockFunc&);
         const Wallets::WalletId& walletId(void) const override;
         bool hasAccountIds(void) const override;
         std::set<Wallets::AddressAccountId> getAccountIds(void) const override;
         const std::string& name(void) const override;
         bool isWatchingOnly(void) const override;

         std::shared_ptr<Wallets::AssetWallet>&& moveWltPtr(void);
      };

      ////////////////////////////////////////////////////////////////////////////////
      enum Armory135WalletEntriesEnum
      {
         WLT_DATATYPE_KEYDATA = 0,
         WLT_DATATYPE_ADDRCOMMENT,
         WLT_DATATYPE_TXCOMMENT,
         WLT_DATATYPE_OPEVAL,
         WLT_DATATYPE_DELETED
      };

      ////////////////////////////////////////////////////////////////////////////////
      class Armory135Address
      {
      private:
         //public data
         BinaryData scrAddr_;
         SecureBinaryData pubKey_;
         SecureBinaryData chaincode_;

         //private data
         SecureBinaryData privKey_;
         SecureBinaryData decryptedPrivKey_;

         //encryption data
         SecureBinaryData iv_;

         //indexes
         int64_t chainIndex_;
         int64_t depth_;

         //flags
         bool hasPrivKey_ = false;
         bool hasPubKey_ = false;
         bool isEncrypted_ = false;

      public:
         Armory135Address(void) {}

         void parseFromRef(const BinaryDataRef&);
         bool isEncrypted(void) const { return isEncrypted_; }
         bool hasPrivKey(void) const { return hasPrivKey_; }

         const SecureBinaryData& privKey(void) const { return privKey_; }
         const SecureBinaryData& pubKey(void) const { return pubKey_; }
         const SecureBinaryData& chaincode(void) const { return chaincode_; }
         const SecureBinaryData& iv(void) const { return iv_; }

         const BinaryData& scrAddr(void) const { return scrAddr_; }
         int64_t chainIndex(void) const { return chainIndex_; }
      };

      ////////////////////////////////////////////////////////////////////////////////
      class Armory135Header
      {
         friend class A135FileInfo;

      private:
         //file system
         const std::filesystem::path path_;

         //meta data
         Wallets::WalletId walletID_;
         uint32_t version_ = UINT32_MAX;
         uint64_t timestamp_ = UINT64_MAX;
         int32_t errorCode_ = INT32_MAX;

         std::string labelName_;
         std::string labelDescription_;

         int64_t highestUsedIndex_ = -1;

         //flags
         bool isEncrypted_ = false;
         bool watchingOnly_ = false;

         //encryption data
         uint32_t kdfMem_ = UINT32_MAX;
         uint32_t kdfIter_;
         SecureBinaryData kdfSalt_;

         //comments
         std::map<BinaryData, std::string> commentMap_;

         //address map
         std::map<BinaryData, Armory135Address> addrMap_;

      private:
         void parseFile(void);
         std::shared_ptr<Armory::Wallets::AssetWallet_Single> migrate(
            const Passphrase::UnlockFunc&,
            const Wallets::IO::CreateWalletParams&) const;

      public:
         Armory135Header(const std::filesystem::path&);

         const std::filesystem::path& path(void) const;
         bool isInitialized(void) const;
         int errorCode(void) const;
         const Wallets::WalletId& getID(void) const;
         const std::string& getLabel(void) const;

         //static
         static void verifyChecksum(const BinaryDataRef&, const BinaryDataRef&);
      };

      ////////////////////////////////////////////////////////////////////////////////
      class A135FileInfo : public WalletFileInfo
      {
      private:
         std::shared_ptr<Armory135Header> a135Ptr_;

      public:
         A135FileInfo(std::shared_ptr<Armory135Header>);

         std::shared_ptr<Armory::Wallets::AssetWallet_Single> migrate(
            const Passphrase::UnlockFunc&,
            const Wallets::IO::CreateWalletParams&) const;

         const Wallets::WalletId& walletId(void) const override;
         bool hasAccountIds(void) const override;
         std::set<Wallets::AddressAccountId> getAccountIds(void) const override;
         const std::string& name(void) const override;
         bool isWatchingOnly(void) const override;

         bool isEncrypted(void) const;
         uint32_t kdfMem(void) const;
         int64_t highestUsedIndex(void) const;
         size_t addressCount(void) const;
         const std::string& description(void) const;
         uint64_t timestamp(void) const;
         std::string version(void) const;
      };
   } //namespace Bridge
} //namespace Armory
