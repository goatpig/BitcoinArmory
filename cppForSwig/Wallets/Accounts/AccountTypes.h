////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <set>
#include <map>
#include <string>

#include <Utils/SecureBinaryData.h>
#include "../WalletIdTypes.h"
#include "../Addresses.h"

#define ARMORY_LEGACY_ACCOUNTID        0xF6E10000
#define IMPORTS_ACCOUNT_PRIV           0x00001200
#define IMPORTS_ACCOUNT_PUB            0x00001201
#define IMPORTS_ACCOUNTID              0x00000000
#define ARMORY_LEGACY_ASSET_ACCOUNTID  0x00000001U
#define ECDH_ASSET_ACCOUNTID           0x20000000U

#define SEED_DEPTH                     0xFFFF

namespace Armory
{
   namespace Assets
   {
      class AssetEntry_BIP32Root;
   };

   namespace Wallets
   {
      namespace Encryption
      {
         class DecryptedDataContainer;
      };
   };

   namespace Accounts
   {
      class AccountException : public std::runtime_error
      {
      public:
         AccountException(const std::string& err) : std::runtime_error(err)
         {}
      };

      enum class AssetAccountType : int
      {
         Plain = 0,
         ECDH,
         Imports,
         ImportsWO
      };

      enum AccountTypeEnum
      {
         /*
         Armory derivation scheme
         outer and inner account are the same
         uncompressed P2PKH, compresed P2SH-P2PK, P2SH-P2WPKH
         */
         AccountTypeEnum_ArmoryLegacy = 0,

         /*
         BIP32 derivation scheme, derPath is used as is.
         no address type is assumed, this has to be provided at creation
         */
         AccountTypeEnum_BIP32,

         /*
         Derives from BIP32_Custom, ECDH all keys pairs with salt,
         carried by derScheme object.
         */
         AccountTypeEnum_BIP32_Salted,

         /*
         Stealth address account. Has a single key pair, ECDH it with custom
         salts per asset.
         */
         AccountTypeEnum_ECDH,

         /*
         As the name says, to import key pairs
         */
         AccountTypeEnum_Imports
      };

      enum class MetaAccountType : int
      {
         Unset = 0,
         Comments,
         Peers
      };

      //////////////////////////////////////////////////////////////////////////
      //////////////////////////////////////////////////////////////////////////
      class AccountType
      {
      protected:
         std::set<AddressEntryType> addressTypes_;
         AddressEntryType defaultAddressEntryType_;
         bool isMain_ = false;

      public:
         //tors
         AccountType(void);
         virtual ~AccountType(void) = 0;

         //locals
         void setMain(bool);
         bool isMain(void) const;

         const std::set<AddressEntryType>& getAddressTypes(void) const;
         AddressEntryType getDefaultAddressEntryType(void) const;
         void addAddressType(AddressEntryType);
         void setDefaultAddressType(AddressEntryType);

         //virtuals
         virtual AccountTypeEnum type(void) const = 0;
         virtual std::string name(void) const = 0;
         std::string getDisplayName(void) const { return name(); }
         virtual Wallets::AddressAccountId getAccountID(void) const = 0;
         virtual Wallets::AssetAccountId getOuterAccountID(void) const = 0;
         virtual Wallets::AssetAccountId getInnerAccountID(void) const = 0;
         virtual bool isWatchingOnly(void) const = 0;
      };

      ////////////////////
      class AccountType_ArmoryLegacy : public AccountType
      {
      public:
         AccountType_ArmoryLegacy(void);

         AccountTypeEnum type(void) const override;
         bool isWatchingOnly(void) const override;
         std::string name(void) const override;

         Wallets::AddressAccountId getAccountID(void) const override;
         Wallets::AssetAccountId getOuterAccountID(void) const override;
         Wallets::AssetAccountId getInnerAccountID(void) const override;

         static const Wallets::AddressAccountId addrAccountId;
      };

      //////////////////////////////////////////////////////////////////////////
      //////////////////////////////////////////////////////////////////////////
      struct NodeData
      {
         using Depth    = uint16_t;
         using BranchId = uint16_t;
         using NodeVal  = uint32_t;

         //depth of the node relative to the seed, always unique within a
         //branch or a path, can have duplicates within a tree
         const Depth depth;

         //id of the branch carrying the node, depths can duplicate
         //so we have to differentiate by branch too
         const BranchId branchId;

         //value of the node, this value is used as is to derive with
         const NodeVal value;

         //false for depth + branch id indexing (default behavior), true
         //for searching exclusively by depth (depth is unique within a
         //given branch)
         const bool depthOnly;

         NodeData(Depth, BranchId, NodeVal, bool dOnly=false);

         bool operator<(const NodeData&) const;
         bool operator==(const NodeData&) const;
         bool isHardDerviation(void) const;
      };

      ////////////////////
      class DerivationBranch
      {
         friend class DerivationTree;

      public:
         using Path = std::set<NodeData>;

      private:
         const NodeData parent_;
         const NodeData::BranchId id_;
         Path nodes_;

      private:
         DerivationBranch(const NodeData&, uint16_t);

      public:
         const NodeData& appendNode(uint32_t);
         const NodeData& getNodeByRelativeDepth(uint16_t);
         const Path& getNodes(void) const;
      };

      ////////////////////
      struct NodeRoot
      {
         const DerivationBranch::Path path;
         const SecureBinaryData b58Root;

         bool isInitialized(void) const;
      };

      ////////////////////
      class DerivationTree
      {
      private:
         std::map<NodeData::BranchId, DerivationBranch> branches_;
         std::map<NodeData, SecureBinaryData> b58Roots_;
         NodeData::BranchId branchCounter_{0};

      private:
         struct PathIt
         {
            std::vector<uint32_t>::const_iterator it;
            const std::vector<uint32_t>* theVector;

            PathIt(const std::vector<uint32_t>* theV) :
               theVector(theV)
            {
               it = theVector->begin();
            }

            bool isValid(void) const
            {
               return (it != theVector->end());
            }

         };
         using HeadsMap = std::map<int, PathIt>;
         void mergeDerPaths(DerivationBranch&, HeadsMap&);

      public:
         DerivationTree(uint32_t);

         DerivationBranch& getBranch(const NodeData&);
         DerivationBranch& getBranch(NodeData::BranchId);
         const DerivationBranch& getBranch(NodeData::BranchId) const;

         DerivationBranch& forkFromBranch(const NodeData&);
         DerivationBranch& forkFromBranch(const DerivationBranch&);
         DerivationBranch& forkFromBranch(NodeData::BranchId);

         const NodeData& getSeedNode(void) const;
         void addB58Root(const NodeData&, const SecureBinaryData&);

         static DerivationTree fromDerivationPaths(
            uint32_t, const std::vector<std::vector<uint32_t>>&);

         uint32_t getSeedFingerprint(void) const;

         std::vector<DerivationBranch::Path> getPaths(void) const;
         std::vector<NodeRoot> resolveNodeRoots(
            std::shared_ptr<Wallets::Encryption::DecryptedDataContainer>,
            std::shared_ptr<Assets::AssetEntry_BIP32Root>) const;

         static std::vector<uint32_t> toPath32(const DerivationBranch::Path&);
      };

      ////////////////////
      class PathAndRoot
      {
      private:
         const std::vector<uint32_t> path_;
         mutable std::string b58RootStr_;
         mutable SecureBinaryData b58RootSbd_;

      public:
         PathAndRoot(const std::vector<uint32_t>, const std::string&);
         PathAndRoot(const std::vector<uint32_t>, const SecureBinaryData&);

         const std::vector<uint32_t>& getPath(void) const;
         const SecureBinaryData& getRootSbd(void) const;
         const std::string& getRootStr(void) const;
      };

      ////
      class AccountType_BIP32 : public AccountType
      {
         friend struct AccountType_BIP32_Custom;

      private:
         DerivationTree derTree_;

         Wallets::AccountKeyType outerAccountKey_;
         Wallets::AccountKeyType innerAccountKey_;
         bool haveOuterAccId_ = false;
         bool haveInnerAccId_ = false;

      protected:
         unsigned addressLookup_ = UINT32_MAX;

      public:
         AccountType_BIP32(DerivationTree&);

         static std::shared_ptr<AccountType_BIP32> makeFromDerPaths(
            uint32_t, const std::vector<std::vector<uint32_t>>&);

         //AccountType virtuals
         Wallets::AddressAccountId getAccountID(void) const override;
         Wallets::AssetAccountId getOuterAccountID(void) const override;
         Wallets::AssetAccountId getInnerAccountID(void) const override;
         bool isWatchingOnly(void) const override { return false;}
         std::string name(void) const override;

         //bip32 locals
         unsigned getSeedFingerprint(void) const;
         unsigned getAddressLookup(void) const;
         void setAddressLookup(unsigned);

         void setNodes(const std::set<unsigned>&);
         void setOuterAccountID(const Wallets::AccountKeyType&);
         void setInnerAccountID(const Wallets::AccountKeyType&);
         void setRoots(const std::vector<PathAndRoot>&);
         void setSeedRoot(const SecureBinaryData&);

         virtual AccountTypeEnum type(void) const override;
         const DerivationTree& getDerivationTree(void) const;
      };

      ////////////////////////////////////////////////////////////////////////////////
      class AccountType_BIP32_Salted : public AccountType_BIP32
      {
      private:
         const SecureBinaryData salt_;

      public:
         AccountType_BIP32_Salted(DerivationTree&, const SecureBinaryData&);

         static std::shared_ptr<AccountType_BIP32_Salted> makeFromDerPaths(
            uint32_t, const std::vector<std::vector<unsigned>>&,
            const SecureBinaryData&);

         AccountTypeEnum type(void) const;
         const SecureBinaryData& getSalt(void) const;
      };

      ////////////////////////////////////////////////////////////////////////////////
      class AccountType_ECDH : public AccountType
      {
      private:
         const SecureBinaryData privateKey_;
         const SecureBinaryData publicKey_;

      public:
         //tor
         AccountType_ECDH(const SecureBinaryData&, const SecureBinaryData&);

         //local
         const SecureBinaryData& getPrivKey(void) const;
         const SecureBinaryData& getPubKey(void) const;

         //virtual
         AccountTypeEnum type(void) const override;
         Wallets::AddressAccountId getAccountID(void) const override;
         Wallets::AssetAccountId getOuterAccountID(void) const override;
         Wallets::AssetAccountId getInnerAccountID(void) const override;

         virtual bool isWatchingOnly(void) const override;
         std::string name(void) const override;
      };

      class AccountType_Imports : public AccountType
      {
         const bool hasPrivateKeys_;

      public:
         AccountType_Imports(bool);

         //virtual
         AccountTypeEnum type(void) const override;
         Wallets::AddressAccountId getAccountID(void) const override;
         Wallets::AssetAccountId getOuterAccountID(void) const override;
         Wallets::AssetAccountId getInnerAccountID(void) const override;

         virtual bool isWatchingOnly(void) const override;
         std::string name(void) const override;
      };
   }; //namespace Accounts
}; //namespace Armory
