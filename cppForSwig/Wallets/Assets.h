////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_ASSETS
#define _H_ASSETS

#include <vector>
#include <set>
#include <string>
#include <memory>

#include "Utils/BinaryData.h"
#include "WalletIdTypes.h"
#include "AssetEncryption.h"

#define ASSETENTRY_PREFIX           0x8A
#define PUBKEY_UNCOMPRESSED_BYTE    0x80
#define PUBKEY_COMPRESSED_BYTE      0x81
#define ECDH_SALT_PREFIX            0x85

#define METADATA_COMMENTS_PREFIX    0x90
#define METADATA_AUTHPEER_PREFIX    0x91
#define METADATA_PEERROOT_PREFIX    0x92
#define METADATA_ROOTSIG_PREFIX     0x93
#define METADATA_PEERMASTER_PREFIX  0x94

class AddressEntry_Multisig;

namespace Armory
{
   namespace Wallets
   {
      namespace Encryption
      {
         struct CipherData;
         class EncryptedAssetData;
      };
   };

   namespace Accounts
   {
      class MetaDataAccount;
   };

   namespace Seeds
   {
      enum class LegacyType : int;
   }

   namespace Assets
   {
      class AssetException : public std::runtime_error
      {
      public:
         AssetException(const std::string&);
      };

      //////////////////////////////////////////////////////////////////////////
      enum class AssetType : int
      {
         EncryptedData,
         PublicKey,
         PrivateKey,
      };

      enum class MetaType : int
      {
         Comment,
         AuthorizedPeer,
         PeerRootKey,
         PeerRootSig,
         PeerMasterKey
      };

      ////
      enum class AssetEntryType : int
      {
         Single = 1,
         Multisig = 2,
         BIP32Root = 3,
         ArmoryLegacyRoot = 4,
         ScriptHash = 5,
         RawScript = 6
      };

      //////////////////////////////////////////////////////////////////////////
      struct Asset
      {
         /*TODO: create a mlocked binarywriter class*/
         const AssetType type;
         Asset(AssetType);

         virtual ~Asset(void) = 0;
         virtual BinaryData serialize(void) const = 0;
      };

      //////////////////////////////////////////////////////////////////////////
      struct Asset_PublicKey : public Asset
      {
      public:
         SecureBinaryData uncompressed_;
         SecureBinaryData compressed_;

      public:
         Asset_PublicKey(SecureBinaryData&);
         Asset_PublicKey(SecureBinaryData&, SecureBinaryData&);

         const SecureBinaryData& getUncompressedKey(void) const;
         const SecureBinaryData& getCompressedKey(void) const;
         BinaryData serialize(void) const override;
      };

      //////////////////////////////////////////////////////////////////////////
      struct Asset_PrivateKey :
         public Wallets::Encryption::EncryptedAssetData, public Asset
      {
      public:
         const Wallets::AssetId id_;

      public:
         Asset_PrivateKey(const Wallets::AssetId&,
            std::unique_ptr<Wallets::Encryption::CipherData>);

         //virtual
         bool isSame(EncryptedAssetData* const) const override;
         BinaryData serialize(void) const override;
         const Wallets::AssetId& getAssetId(void) const override;

         //static
         static std::unique_ptr<Asset_PrivateKey> deserialize(
            const BinaryDataRef&);
         static std::unique_ptr<Asset_PrivateKey> deserializeOld(
            const Wallets::AssetId&, const BinaryDataRef&);
      };

      //////////////////////////////////////////////////////////////////////////
      //////////////////////////////////////////////////////////////////////////
      class AssetEntry
      {
      protected:
         AssetEntryType type_;
         const Wallets::AssetId ID_;

         bool needsCommit_ = true;

      public:
         //tors
         AssetEntry(AssetEntryType, Wallets::AssetId);
         virtual ~AssetEntry(void) = 0;

         //local
         Wallets::AssetKeyType getIndex(void) const;
         const Wallets::AssetAccountId getAccountID(void) const;
         const Wallets::AssetId& getID(void) const;

         virtual AssetEntryType getType(void) const;
         bool needsCommit(void) const;
         void doNotCommit(void);
         void flagForCommit(void);
         BinaryData getDbKey(void) const;

         //virtual
         virtual BinaryData serialize(void) const = 0;
         virtual bool hasPrivateKey(void) const = 0;
         virtual const Wallets::EncryptionKeyId&
            getPrivateEncryptionKeyId(void) const = 0;

         //static
         static std::shared_ptr<AssetEntry> deserialize(
            BinaryDataRef key, BinaryDataRef value);
         static std::shared_ptr<AssetEntry> deserDBValue(
            const Wallets::AssetId&, BinaryDataRef);
      };

      //////////////////////////////////////////////////////////////////////////
      class AssetEntry_Single : public AssetEntry
      {
      private:
         std::shared_ptr<Asset_PublicKey> pubkey_;
         std::shared_ptr<Asset_PrivateKey> privkey_;

      public:
         //tors
         AssetEntry_Single(Wallets::AssetId,
            SecureBinaryData&, std::shared_ptr<Asset_PrivateKey>);
         AssetEntry_Single(Wallets::AssetId,
            SecureBinaryData&, SecureBinaryData&,
            std::shared_ptr<Asset_PrivateKey>);
         AssetEntry_Single(Wallets::AssetId,
            std::shared_ptr<Asset_PublicKey>,
            std::shared_ptr<Asset_PrivateKey>);

         //local
         std::shared_ptr<Asset_PublicKey> getPubKey(void) const;
         std::shared_ptr<Asset_PrivateKey> getPrivKey(void) const;

         //virtual
         virtual BinaryData serialize(void) const override;
         bool hasPrivateKey(void) const;
         const Wallets::EncryptionKeyId&
            getPrivateEncryptionKeyId(void) const override;
         const Wallets::KdfId& getKdfId(void) const;
         virtual std::shared_ptr<AssetEntry_Single> getPublicCopy(void);
      };

      //////////////////////////////////////////////////////////////////////////
      class AssetEntry_ArmoryLegacyRoot : public AssetEntry_Single
      {
      private:
         const SecureBinaryData chaincode_;
         const Seeds::LegacyType seedType_;

      public:
         //tors
         AssetEntry_ArmoryLegacyRoot(
            Wallets::AssetId, SecureBinaryData&,
            std::shared_ptr<Asset_PrivateKey>,
            const SecureBinaryData&, Seeds::LegacyType);

         BinaryData serialize(void) const override;
         AssetEntryType getType(void) const override;
         std::shared_ptr<AssetEntry_Single> getPublicCopy(void) override;

         Seeds::LegacyType getSeedType(void) const;
         const SecureBinaryData& getChaincode(void) const;
      };

      //////////////////////////////////////////////////////////////////////////
      class AssetEntry_BIP32Root : public AssetEntry_Single
      {
      private:
         const SecureBinaryData chaincode_;
         const uint8_t depth_;
         const unsigned leafID_;

         /*
         Fingerprint of the parent (see BIP32 specs), 0 for roots derived from
         a seed (there is no parent)
         */
         const uint32_t parentFingerprint_;

         /*
         Fingerprint of the node generated from a seed (no derivation), equal
         to thisFingerprint when parentFingerprint is 0
         */
         uint32_t seedFingerprint_ = UINT32_MAX;

         /*
         Own fingerprint, 4 first bytes of hash256 of the root's public key
         */
         mutable uint32_t thisFingerprint_ = UINT32_MAX;

         const std::vector<uint32_t> derivationPath_{};

      public:
         //tors
         AssetEntry_BIP32Root(
            const Wallets::AssetId&,
            SecureBinaryData&, //pubkey
            std::shared_ptr<Asset_PrivateKey>, //privkey
            const SecureBinaryData&, //chaincode
            uint8_t, uint32_t, //depth, leafID
            uint32_t, uint32_t, //fingerprint, seed fingerprint
            const std::vector<uint32_t>&); //der path

         AssetEntry_BIP32Root(
            const Wallets::AssetId&,
            std::shared_ptr<Asset_PublicKey>,
            std::shared_ptr<Asset_PrivateKey>,
            const SecureBinaryData&,
            uint8_t, uint32_t,
            uint32_t, uint32_t,
            const std::vector<uint32_t>&);

         //local
         uint8_t getDepth(void) const;
         unsigned getLeafID(void) const;
         unsigned getParentFingerprint(void) const;

         unsigned getThisFingerprint(void) const;
         unsigned getSeedFingerprint(bool) const;
         std::string getXPub(void) const;
         const SecureBinaryData& getChaincode(void) const;
         const std::vector<uint32_t>& getDerivationPath(void) const;

         //sanity check
         void checkSeedFingerprint(bool) const;

         //virtual
         BinaryData serialize(void) const override;
         AssetEntryType getType(void) const override;
         std::shared_ptr<AssetEntry_Single> getPublicCopy(void) override;
      };

      //////////////////////////////////////////////////////////////////////////
      class AssetEntry_Multisig : public AssetEntry
      {
         friend class ::AddressEntry_Multisig;

      private:
         //map<AssetWalletID, AssetEntryPtr>
         //ordering by wallet ids guarantees the ms script hash can be
         //reconstructed deterministically
         const std::map<BinaryData, std::shared_ptr<AssetEntry>> assetMap_;

         const unsigned m_;
         const unsigned n_;

      private:
         const std::map<BinaryData, std::shared_ptr<AssetEntry>>
         getAssetMap(void) const;

      public:
         //tors
         AssetEntry_Multisig(Wallets::AssetId,
            const std::map<BinaryData, std::shared_ptr<AssetEntry>>&,
            unsigned, unsigned);

         //local
         unsigned getM(void) const;
         unsigned getN(void) const;

         //virtual
         BinaryData serialize(void) const override;

         bool hasPrivateKey(void) const override;
         const Wallets::EncryptionKeyId&
         getPrivateEncryptionKeyId(void) const override;
      };

      //////////////////////////////////////////////////////////////////////////
      class AssetEntry_ScriptHash : public AssetEntry
      {
      private:
         SecureBinaryData scriptHash_;

      public:
         AssetEntry_ScriptHash(const Wallets::AssetId&, SecureBinaryData&);

         //virtuals
         BinaryData serialize(void) const override;
         bool hasPrivateKey(void) const override;
         const Wallets::EncryptionKeyId&
            getPrivateEncryptionKeyId(void) const override;

         //locals
         const SecureBinaryData& getScriptHash(void) const;
      };

      //////////////////////////////////////////////////////////////////////////
      class AssetEntry_RawScript : public AssetEntry
      {
      private:
         SecureBinaryData script_;

      public:
         AssetEntry_RawScript(const Wallets::AssetId&, SecureBinaryData&);

         //virtuals
         BinaryData serialize(void) const override;
         bool hasPrivateKey(void) const override;
         const Wallets::EncryptionKeyId&
            getPrivateEncryptionKeyId(void) const override;

         //locals
         const SecureBinaryData& getScript(void) const;
      };

      //////////////////////////////////////////////////////////////////////////
      //////////////////////////////////////////////////////////////////////////
      struct MetaData
      {
         friend class Accounts::MetaDataAccount;

      private:
         bool needsCommit_ = false;

      protected:
         const MetaType type_;
         const BinaryData accountID_;
         const uint32_t index_;

      public:
         MetaData(MetaType, const BinaryData&, uint32_t);

         //virtuals
         virtual ~MetaData(void) = 0;
         virtual BinaryData serialize(void) const = 0;
         virtual BinaryData getDbKey(void) const = 0;
         virtual void deserializeDBValue(const BinaryDataRef&) = 0;
         virtual void clear(void) = 0;
         virtual std::shared_ptr<MetaData> copy(void) const = 0;

         //locals
         bool needsCommit(void);
         void flagForCommit(void);
         MetaType type(void) const;

         const BinaryData& getAccountID(void) const;
         uint32_t getIndex(void) const;

         //static
         static std::shared_ptr<MetaData> deserialize(
            const BinaryDataRef&, const BinaryDataRef&);
      };

      //////////////////////////////////////////////////////////////////////////
      class PeerPublicData : public MetaData
      {
      private:
         std::set<std::string> names_; //IPs, domain names
         SecureBinaryData publicKey_;
         std::string label_;
         bool oneWay_;

      public:
         PeerPublicData(const BinaryData&, uint32_t);

         //virtuals
         BinaryData serialize(void) const override;
         BinaryData getDbKey(void) const override;
         void deserializeDBValue(const BinaryDataRef&) override;
         void clear(void) override;
         std::shared_ptr<MetaData> copy(void) const override;

         //locals
         void addName(const std::string&);
         bool eraseName(const std::string&);
         void setPublicKey(const SecureBinaryData&);
         void setLabel(const std::string&);
         void setOneWay(bool);

         //
         const std::set<std::string>& getNames(void) const;
         const SecureBinaryData& getPublicKey(void) const;
         const std::string& getLabel(void) const;
         bool oneWay(void) const;
      };

      //////////////////////////////////////////////////////////////////////////
      class PeerRootKey : public MetaData
      {
         //carries the root key of authorized peers' parent public key
         //used to check signatures of child peer keys, typically a server
         //with a key pair cycling schedule

      private:
         SecureBinaryData publicKey_;
         std::string description_;

      public:
         PeerRootKey(const BinaryData&, uint32_t);

         //virtuals
         BinaryData serialize(void) const override;
         BinaryData getDbKey(void) const override;
         void deserializeDBValue(const BinaryDataRef&) override;
         void clear(void) override;
         std::shared_ptr<MetaData> copy(void) const override;

         //locals
         void set(const std::string&, const SecureBinaryData&);
         const SecureBinaryData& getKey(void) const;
         const std::string& getDescription(void) const;

      };

      //////////////////////////////////////////////////////////////////////////
      class PeerRootSignature : public MetaData
      {
         // carries the peer wallet's key pair signature from a 'parent' wallet
         // typically only one per peer wallet

      private:
         SecureBinaryData publicKey_;
         SecureBinaryData signature_;

      public:
         PeerRootSignature(const BinaryData&, uint32_t);

         //virtuals
         BinaryData serialize(void) const override;
         BinaryData getDbKey(void) const override;
         void deserializeDBValue(const BinaryDataRef&) override;
         void clear(void) override;
         std::shared_ptr<MetaData> copy(void) const override;

         //locals
         void set(const SecureBinaryData&, const SecureBinaryData&);
         const SecureBinaryData& getKey(void) const;
         const SecureBinaryData& getSig(void) const;
      };

      //////////////////////////////////////////////////////////////////////////
      class PeerMasterKey : public MetaData
      {
         // carries the peer wallet's master key
         // typically only one per peer wallet

      private:
         SecureBinaryData key_;

      public:
         PeerMasterKey(const BinaryData&, uint32_t);

         //virtuals
         BinaryData serialize(void) const override;
         BinaryData getDbKey(void) const override;
         void deserializeDBValue(const BinaryDataRef&) override;
         void clear(void) override;
         std::shared_ptr<MetaData> copy(void) const override;

         //locals
         void set(const SecureBinaryData&);
         const SecureBinaryData& getKey(void) const;
      };

      //////////////////////////////////////////////////////////////////////////
      class CommentData : public MetaData
      {
      private:
         std::string commentStr_;
         BinaryData key_;

      public:
         CommentData(const BinaryData&, uint32_t);

         //virtuals
         BinaryData serialize(void) const override;
         BinaryData getDbKey(void) const;
         void deserializeDBValue(const BinaryDataRef&);
         void clear(void);
         std::shared_ptr<MetaData> copy(void) const;

         //locals
         const std::string& getValue(void) const;
         void setValue(const std::string&);

         const BinaryData& getKey(void) const;
         void setKey(const BinaryData&);
      };
   }; //namespace Assets
}; //namespace Armory
#endif
