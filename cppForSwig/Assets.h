////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2019, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_ASSETS
#define _H_ASSETS

#include <set>
#include <string>

#include "BinaryData.h"
#include "EncryptionUtils.h"
#include "AssetEncryption.h"

class AssetException : public std::runtime_error
{
public:
   AssetException(const std::string& err) : std::runtime_error(err)
   {}
};

#define HMAC_KEY_ENCRYPTIONKEYS "EncyrptionKey"
#define HMAC_KEY_PRIVATEKEYS "PrivateKey"

#define ASSETENTRY_PREFIX        0x8A
#define PUBKEY_UNCOMPRESSED_BYTE 0x80
#define PUBKEY_COMPRESSED_BYTE   0x81
#define PRIVKEY_BYTE             0x82
#define ENCRYPTIONKEY_BYTE       0x83
#define WALLET_SEED_BYTE         0x84
#define ECDH_SALT_PREFIX         0x85

#define METADATA_COMMENTS_PREFIX 0x90
#define METADATA_AUTHPEER_PREFIX 0x91
#define METADATA_PEERROOT_PREFIX 0x92
#define METADATA_ROOTSIG_PREFIX  0x93

#define ROOT_ASSETENTRY_ID       0xFFFFFFFF
#define SEED_ID                  READHEX("0x5EEDDEE55EEDDEE5");


////////////////////////////////////////////////////////////////////////////////
enum AssetType
{
   AssetType_EncryptedData,
   AssetType_PublicKey,
   AssetType_PrivateKey,
};

enum MetaType
{
   MetaType_Comment,
   MetaType_AuthorizedPeer,
   MetaType_PeerRootKey,
   MetaType_PeerRootSig
};

////
enum AssetEntryType
{
   AssetEntryType_Single = 0x01,
   AssetEntryType_Multisig,
   AssetEntryType_BIP32Root
};

enum ScriptHashType
{
   ScriptHash_P2PKH_Uncompressed,
   ScriptHash_P2PKH_Compressed,
   ScriptHash_P2WPKH,
   ScriptHash_Nested_P2PK
};

////////////////////////////////////////////////////////////////////////////////
struct DecryptedEncryptionKey
{
   friend class DecryptedDataContainer;
   friend class Cipher_AES;
   friend class AssetWallet_Single;

private:
   const SecureBinaryData rawKey_;
   std::map<BinaryData, SecureBinaryData> derivedKeys_;

private:
   BinaryData computeId(const SecureBinaryData& key) const;
   const SecureBinaryData& getData(void) const { return rawKey_; }
   const SecureBinaryData& getDerivedKey(const BinaryData& id) const;

public:
   DecryptedEncryptionKey(SecureBinaryData& key) :
      rawKey_(std::move(key))
   {}

   void deriveKey(std::shared_ptr<KeyDerivationFunction> kdf);
   BinaryData getId(const BinaryData& kdfid) const;

   std::unique_ptr<DecryptedEncryptionKey> copy(void) const;
   bool hasData(void) const { return rawKey_.getSize() != 0; }
};

////////////////////////////////////////////////////////////////////////////////
struct DecryptedData
{
private:
   const BinaryData id_;
   const SecureBinaryData privateKey_;

private:
   const SecureBinaryData& getData(void) const { return privateKey_; }

public:
   DecryptedData(const BinaryData& id, SecureBinaryData& key) :
      id_(id), privateKey_(std::move(key))
   {}

   bool hasData(void) const { return privateKey_.getSize() != 0; }
   const BinaryData& getId(void) const { return id_; }
   const SecureBinaryData& getDataRef(void) const { return privateKey_; }
};

////////////////////////////////////////////////////////////////////////////////
struct Asset
{
   const AssetType type_;

   Asset(AssetType type) :
      type_(type)
   {}

   /*TODO: create a mlocked binarywriter class*/

   virtual ~Asset(void) = 0;
   virtual BinaryData serialize(void) const = 0;
};

////////////////////////////////////////////////////////////////////////////////
struct Asset_PublicKey : public Asset
{
public:
   SecureBinaryData uncompressed_;
   SecureBinaryData compressed_;

public:
   Asset_PublicKey(SecureBinaryData& pubkey) :
      Asset(AssetType_PublicKey)
   {
      switch (pubkey.getSize())
      {
      case 33:
      {
         uncompressed_ = std::move(CryptoECDSA().UncompressPoint(pubkey));
         compressed_ = std::move(pubkey);
         break;
      }

      case 65:
      {
         compressed_ = std::move(CryptoECDSA().CompressPoint(pubkey));
         uncompressed_ = std::move(pubkey);
         break;
      }

      default:
         throw AssetException("cannot compress/decompress pubkey of that size");
      }
   }

   Asset_PublicKey(SecureBinaryData& uncompressedKey,
      SecureBinaryData& compressedKey) :
      Asset(AssetType_PublicKey),
      uncompressed_(std::move(uncompressedKey)),
      compressed_(std::move(compressedKey))
   {
      if (uncompressed_.getSize() != 65 ||
         compressed_.getSize() != 33)
         throw AssetException("invalid pubkey size");
   }

   const SecureBinaryData& getUncompressedKey(void) const { return uncompressed_; }
   const SecureBinaryData& getCompressedKey(void) const { return compressed_; }

   BinaryData serialize(void) const;
};

////////////////////////////////////////////////////////////////////////////////
struct Asset_EncryptedData : public Asset
{
   /***
   This class is instantiated with the cipher text and related cipher object.
   It can only be used to yield the plain text on demand, it does not generate
   the cipher text from the plain text for you.

   Use Cipher::encrypt to generate the cipher text first. Pass that cipher with 
   the resulting cipher text to this class' ctor.
   ***/

   friend class DecryptedDataContainer;

protected:
   const SecureBinaryData cipherText_;
   std::unique_ptr<Cipher> cipher_;

public:
   Asset_EncryptedData(
      SecureBinaryData& cipherText, std::unique_ptr<Cipher> cipher) :
      Asset(AssetType_EncryptedData), cipherText_(std::move(cipherText))
   {
      if (cipherText_.getSize() == 0)
         return;

      if (cipher == nullptr)
         throw AssetException("null cipher for privkey");

      cipher_ = std::move(cipher);
   }

   //virtual
   virtual ~Asset_EncryptedData(void) = 0;
   virtual bool isSame(Asset_EncryptedData* const) const = 0;
   virtual BinaryData getId(void) const = 0;

   //local
   bool hasData(void) const
   {
      return (cipherText_.getSize() != 0);
   }

   std::unique_ptr<Cipher> copyCipher(void) const
   {
      if (cipher_ == nullptr)
         return nullptr;

      return cipher_->getCopy();
   }

   const SecureBinaryData& getIV(void) const
   {
      return cipher_->getIV();
   }

   const SecureBinaryData& getCipherText(void) const
   {
      return cipherText_;
   }

   const BinaryData& getEncryptionKeyID(void) const
   {
      return cipher_->getEncryptionKeyId();
   }

   virtual std::unique_ptr<DecryptedData> decrypt(
      const SecureBinaryData& key) const;

   //static
   static std::shared_ptr<Asset_EncryptedData> deserialize(const BinaryDataRef&);
   static std::shared_ptr<Asset_EncryptedData> deserialize(
      size_t len, const BinaryDataRef&);
};

////////////////////////////////////////////////////////////////////////////////
struct Asset_EncryptionKey : public Asset_EncryptedData
{
public:
   const BinaryData id_;

public:
   Asset_EncryptionKey(BinaryData& id, 
      SecureBinaryData& cipherText,
      std::unique_ptr<Cipher> cipher) :
      Asset_EncryptedData(cipherText, std::move(cipher)), id_(std::move(id))
   {}

   BinaryData serialize(void) const;
   BinaryData getId(void) const { return id_; }

   bool isSame(Asset_EncryptedData* const) const;

   std::unique_ptr<DecryptedData> decrypt(
      const SecureBinaryData& key) const
   {
      throw std::runtime_error("illegal for encryption keys");
   }
};

////////////////////////////////////////////////////////////////////////////////
struct Asset_PrivateKey : public Asset_EncryptedData
{
public:
   const BinaryData id_;

public:
   Asset_PrivateKey(const BinaryData& id,
      SecureBinaryData& data, std::unique_ptr<Cipher> cipher) :
      Asset_EncryptedData(data, std::move(cipher)), id_(id)
   {}

   BinaryData serialize(void) const;
   BinaryData getId(void) const { return id_; }

   bool isSame(Asset_EncryptedData* const) const;
};

////////////////////////////////////////////////////////////////////////////////
class EncryptedSeed : public Asset_EncryptedData
{
private:

public:
   //tors

   //setup encrypted data
   EncryptedSeed(
      SecureBinaryData cipherText, std::unique_ptr<Cipher> cipher) :
      Asset_EncryptedData(cipherText, move(cipher))
   {}

   //local
   BinaryData serialize(void) const;

   //virtual
   bool isSame(Asset_EncryptedData* const) const;
   BinaryData getId(void) const { return SEED_ID; }
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class AssetEntry
{
protected:
   AssetEntryType type_;
   const int index_;
   const BinaryData accountID_;
   BinaryData ID_; //accountID | index

   bool needsCommit_ = true;

public:
   //tors
   AssetEntry(AssetEntryType type, int id, const BinaryData& accountID) :
      type_(type), index_(id), accountID_(accountID)
   {
      ID_ = accountID_;
      ID_.append(WRITE_UINT32_BE(id));
   }

   virtual ~AssetEntry(void) = 0;

   //local
   int getIndex(void) const { return index_; }
   const BinaryData& getAccountID(void) const { return accountID_; }
   const BinaryData& getID(void) const { return ID_; }

   virtual const AssetEntryType getType(void) const { return type_; }
   bool needsCommit(void) const { return needsCommit_; }
   void doNotCommit(void) { needsCommit_ = false; }
   void flagForCommit(void) { needsCommit_ = true; }
   BinaryData getDbKey(void) const;

   //virtual
   virtual BinaryData serialize(void) const = 0;
   virtual bool hasPrivateKey(void) const = 0;
   virtual const BinaryData& getPrivateEncryptionKeyId(void) const = 0;

   //static
   static std::shared_ptr<AssetEntry> deserialize(
      BinaryDataRef key, BinaryDataRef value);
   static std::shared_ptr<AssetEntry> deserDBValue(
      int index, const BinaryData& account_id,
      BinaryDataRef value);
};

////////////////////////////////////////////////////////////////////////////////
class AssetEntry_Single : public AssetEntry
{
private:
   std::shared_ptr<Asset_PublicKey> pubkey_;
   std::shared_ptr<Asset_PrivateKey> privkey_;

public:
   //tors
   AssetEntry_Single(int id, const BinaryData& accountID,
      SecureBinaryData& pubkey,
      std::shared_ptr<Asset_PrivateKey> privkey) :
      AssetEntry(AssetEntryType_Single, id, accountID), 
      privkey_(privkey)
   {
      pubkey_ = std::make_shared<Asset_PublicKey>(pubkey);
   }

   AssetEntry_Single(int id, const BinaryData& accountID,
      SecureBinaryData& pubkeyUncompressed,
      SecureBinaryData& pubkeyCompressed,
      std::shared_ptr<Asset_PrivateKey> privkey) :
      AssetEntry(AssetEntryType_Single, id, accountID), 
      privkey_(privkey)
   {
      pubkey_ = std::make_shared<Asset_PublicKey>(
         pubkeyUncompressed, pubkeyCompressed);
   }

   AssetEntry_Single(int id, const BinaryData& accountID,
      std::shared_ptr<Asset_PublicKey> pubkey,
      std::shared_ptr<Asset_PrivateKey> privkey) :
      AssetEntry(AssetEntryType_Single, id, accountID),
      pubkey_(pubkey), privkey_(privkey)
   {}

   //local
   std::shared_ptr<Asset_PublicKey> getPubKey(void) const { return pubkey_; }
   std::shared_ptr<Asset_PrivateKey> getPrivKey(void) const { return privkey_; }

   //virtual
   virtual BinaryData serialize(void) const;
   bool hasPrivateKey(void) const;
   const BinaryData& getPrivateEncryptionKeyId(void) const;
   virtual std::shared_ptr<AssetEntry_Single> getPublicCopy(void);
};

////////////////////////////////////////////////////////////////////////////////
class AssetEntry_BIP32Root : public AssetEntry_Single
{
private:
   const uint8_t depth_;
   const unsigned leafID_;
   const unsigned fingerPrint_;
   const SecureBinaryData chaincode_;

public:
   //tors
   AssetEntry_BIP32Root(int id, const BinaryData& accountID,
      SecureBinaryData& pubkey,
      std::shared_ptr<Asset_PrivateKey> privkey,
      const SecureBinaryData& chaincode,
      uint8_t depth, unsigned leafID, unsigned fingerPrint) :
      AssetEntry_Single(id, accountID, pubkey, privkey),
      chaincode_(chaincode),
      depth_(depth), leafID_(leafID), fingerPrint_(fingerPrint)
   {}

   AssetEntry_BIP32Root(int id, const BinaryData& accountID,
      SecureBinaryData& pubkeyUncompressed,
      SecureBinaryData& pubkeyCompressed,
      std::shared_ptr<Asset_PrivateKey> privkey,
      const SecureBinaryData& chaincode,
      uint8_t depth, unsigned leafID, unsigned fingerPrint) :
      AssetEntry_Single(id, accountID,
         pubkeyUncompressed, pubkeyCompressed, privkey),
      chaincode_(chaincode),
      depth_(depth), leafID_(leafID), fingerPrint_(fingerPrint)
   {}

   AssetEntry_BIP32Root(int id, const BinaryData& accountID,
      std::shared_ptr<Asset_PublicKey> pubkey,
      std::shared_ptr<Asset_PrivateKey> privkey,
      const SecureBinaryData& chaincode,
      uint8_t depth, unsigned leafID, unsigned fingerPrint) :
      AssetEntry_Single(id, accountID, pubkey, privkey),
      chaincode_(chaincode),
      depth_(depth), leafID_(leafID), fingerPrint_(fingerPrint)
   {}

   //local
   uint8_t getDepth(void) const { return depth_; }
   unsigned getLeafID(void) const { return leafID_; }
   unsigned getFingerPrint(void) const { return fingerPrint_; }
   const SecureBinaryData& getChaincode(void) const { return chaincode_; }

   //virtual
   BinaryData serialize(void) const override;
   const AssetEntryType getType(void) const override 
   { return AssetEntryType_BIP32Root; }

   std::shared_ptr<AssetEntry_Single> getPublicCopy(void) override;
};

////////////////////////////////////////////////////////////////////////////////
class AssetEntry_Multisig : public AssetEntry
{
   friend class AddressEntry_Multisig;

private:
   //map<AssetWalletID, AssetEntryPtr>
   //ordering by wallet ids guarantees the ms script hash can be 
   //reconstructed deterministically
   const std::map<BinaryData, std::shared_ptr<AssetEntry>> assetMap_;

   const unsigned m_;
   const unsigned n_;

private:
   const std::map<BinaryData, std::shared_ptr<AssetEntry>> getAssetMap(void) const
   {
      return assetMap_;
   }

public:
   //tors
   AssetEntry_Multisig(int id, const BinaryData& accountID,
      const std::map<BinaryData, std::shared_ptr<AssetEntry>>& assetMap,
      unsigned m, unsigned n) :
      AssetEntry(AssetEntryType_Multisig, id, accountID),
      assetMap_(assetMap), m_(m), n_(n)
   {
      if (assetMap.size() != n)
         throw AssetException("asset count mismatch in multisig entry");
   }

   //local
   unsigned getM(void) const { return m_; }
   unsigned getN(void) const { return n_; }

   //virtual
   BinaryData serialize(void) const
   {
      throw AssetException("no serialization for MS assets");
   }

   bool hasPrivateKey(void) const;
   const BinaryData& getPrivateEncryptionKeyId(void) const;
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
struct MetaData
{
   friend class MetaDataAccount;

private:
   bool needsCommit_ = false;

protected:
   const MetaType type_;
   const BinaryData accountID_;
   const unsigned index_;

public:
   MetaData(MetaType type, const BinaryData& accountID, unsigned index) :
      type_(type), accountID_(accountID), index_(index)
   {}

   //virtuals
   virtual ~MetaData(void) = 0;
   virtual BinaryData serialize(void) const = 0;
   virtual BinaryData getDbKey(void) const = 0;
   virtual void deserializeDBValue(const BinaryDataRef&) = 0;
   virtual void clear(void) = 0;
   virtual std::shared_ptr<MetaData> copy(void) const = 0;

   //locals
   bool needsCommit(void) { return needsCommit_; }
   void flagForCommit(void) { needsCommit_ = true; }
   MetaType type(void) const { return type_; }

   const BinaryData& getAccountID(void) const { return accountID_; }
   unsigned getIndex(void) const { return index_; }

   //static
   static std::shared_ptr<MetaData> deserialize(
      const BinaryDataRef& key, const BinaryDataRef& data);
};

////////////////////////////////////////////////////////////////////////////////
class PeerPublicData : public MetaData
{
private:
   std::set<std::string> names_; //IPs, domain names
   SecureBinaryData publicKey_;

public:
   PeerPublicData(const BinaryData& accountID, unsigned index) :
      MetaData(MetaType_AuthorizedPeer, accountID, index)
   {}

   //virtuals
   BinaryData serialize(void) const;
   BinaryData getDbKey(void) const;
   void deserializeDBValue(const BinaryDataRef&);
   void clear(void);
   std::shared_ptr<MetaData> copy(void) const;

   //locals
   void addName(const std::string&);
   bool eraseName(const std::string&);
   void setPublicKey(const SecureBinaryData&);

   //
   const std::set<std::string> getNames(void) const { return names_; }
   const SecureBinaryData& getPublicKey(void) const { return publicKey_; }
};

////////////////////////////////////////////////////////////////////////////////
class PeerRootKey : public MetaData
{
   //carries the root key of authorized peers' parent public key
   //used to check signatures of child peer keys, typically a server with a
   //key pair cycling schedule

private:
   SecureBinaryData publicKey_;
   std::string description_;

public:
   PeerRootKey(const BinaryData& accountID, unsigned index) :
      MetaData(MetaType_PeerRootKey, accountID, index)
   {}

   //virtuals
   BinaryData serialize(void) const;
   BinaryData getDbKey(void) const;
   void deserializeDBValue(const BinaryDataRef&);
   void clear(void);
   std::shared_ptr<MetaData> copy(void) const;

   //locals
   void set(const std::string&, const SecureBinaryData&);
   const SecureBinaryData& getKey(void) const { return publicKey_; }
   const std::string& getDescription(void) const { return description_; }

};

////////////////////////////////////////////////////////////////////////////////
class PeerRootSignature : public MetaData
{
   // carries the peer wallet's key pair signature from a 'parent' wallet
   // typically only one per peer wallet

private:
   SecureBinaryData publicKey_;
   SecureBinaryData signature_;

public:
   PeerRootSignature(const BinaryData& accountID, unsigned index) :
      MetaData(MetaType_PeerRootSig, accountID, index)
   {}

   //virtuals
   BinaryData serialize(void) const;
   BinaryData getDbKey(void) const;
   void deserializeDBValue(const BinaryDataRef&);
   void clear(void);
   std::shared_ptr<MetaData> copy(void) const;

   //locals
   void set(const SecureBinaryData& key, const SecureBinaryData& sig);
   const SecureBinaryData& getKey(void) const { return publicKey_; }
   const SecureBinaryData& getSig(void) const { return signature_; }
};

#endif
