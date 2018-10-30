////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _WALLETS_H
#define _WALLETS_H

#include <atomic>
#include <mutex>
#include <thread>
#include <memory>
#include <set>
#include <map>
#include <string>

#include "ReentrantLock.h"
#include "BinaryData.h"
#include "EncryptionUtils.h"
#include "lmdbpp.h"
#include "Script.h"
#include "Signer.h"

#include "DecryptedDataContainer.h"
#include "Accounts.h"

#define WALLETTYPE_KEY        0x00000001
#define PARENTID_KEY          0x00000002
#define WALLETID_KEY          0x00000003
#define ROOTASSET_KEY         0x00000007
#define MAIN_ACCOUNT_KEY      0x00000008

#define MASTERID_KEY          0x000000A0
#define MAINWALLET_KEY        0x000000A1

#define WALLETMETA_PREFIX     0xB0

#define WALLETMETA_DBNAME "WalletHeader"

#define VERSION_MAJOR      2
#define VERSION_MINOR      0
#define VERSION_REVISION   0
 
class WalletException : public runtime_error
{
public:
   WalletException(const string& msg) : runtime_error(msg)
   {}
};

class NoEntryInWalletException
{};



////////////////////////////////////////////////////////////////////////////////
enum WalletMetaType
{
   WalletMetaType_Single,
   WalletMetaType_Multisig,
   WalletMetaType_Subwallet
};

////
struct WalletMeta
{
   shared_ptr<LMDBEnv> dbEnv_;
   WalletMetaType type_;
   BinaryData parentID_;
   BinaryData walletID_;
   string dbName_;

   uint8_t versionMajor_ = 0;
   uint16_t versionMinor_ = 0;
   uint16_t revision_ = 0;

   SecureBinaryData defaultEncryptionKey_;
   SecureBinaryData defaultEncryptionKeyId_;

   //tors
   WalletMeta(shared_ptr<LMDBEnv> env, WalletMetaType type) :
      dbEnv_(env), type_(type)
   {
      versionMajor_ = VERSION_MAJOR;
      versionMinor_ = VERSION_MINOR;
      revision_ = VERSION_REVISION;
   }

   virtual ~WalletMeta(void) = 0;
   
   //local
   BinaryData getDbKey(void);
   const BinaryData& getWalletID(void) const { return walletID_; }
   string getWalletIDStr(void) const
   {
      if (walletID_.getSize() == 0)
         throw WalletException("empty wallet id");

      string idStr(walletID_.getCharPtr(), walletID_.getSize());
      return idStr;
   }

   BinaryData serializeVersion(void) const;
   void unseralizeVersion(BinaryRefReader&);

   BinaryData serializeEncryptionKey(void) const;
   void unserializeEncryptionKey(BinaryRefReader&);
   
   const SecureBinaryData& getDefaultEncryptionKey(void) const 
   { return defaultEncryptionKey_; }
   const BinaryData& getDefaultEncryptionKeyId(void) const
   { return defaultEncryptionKeyId_; }

   //virtual
   virtual BinaryData serialize(void) const = 0;
   virtual bool shouldLoad(void) const = 0;

   //static
   static shared_ptr<WalletMeta> deserialize(shared_ptr<LMDBEnv> env,
      BinaryDataRef key, BinaryDataRef val);
};

////
struct WalletMeta_Single : public WalletMeta
{
   //tors
   WalletMeta_Single(shared_ptr<LMDBEnv> dbEnv) :
      WalletMeta(dbEnv, WalletMetaType_Single)
   {}

   //virtual
   BinaryData serialize(void) const;
   bool shouldLoad(void) const;
};

////
struct WalletMeta_Multisig : public WalletMeta
{
   //tors
   WalletMeta_Multisig(shared_ptr<LMDBEnv> dbEnv) :
      WalletMeta(dbEnv, WalletMetaType_Multisig)
   {}

   //virtual
   BinaryData serialize(void) const;
   bool shouldLoad(void) const;
};

////
struct WalletMeta_Subwallet : public WalletMeta
{
   //tors
   WalletMeta_Subwallet(shared_ptr<LMDBEnv> dbEnv) :
      WalletMeta(dbEnv, WalletMetaType_Subwallet)
   {}

   //virtual
   BinaryData serialize(void) const;
   bool shouldLoad(void) const;
};

////////////////////////////////////////////////////////////////////////////////
class AssetWallet : protected Lockable
{
   friend class ResolverFeed_AssetWalletSingle;
   friend class ResolverFeed_AssetWalletSingle_ForMultisig;

private:
   virtual void initAfterLock(void) {}
   virtual void cleanUpBeforeUnlock(void) {}

protected:
   shared_ptr<LMDBEnv> dbEnv_ = nullptr;
   LMDB* db_ = nullptr;
   const string dbName_;

   map<BinaryData, shared_ptr<AddressEntry>> addresses_;
   shared_ptr<DecryptedDataContainer> decryptedData_;
   set<shared_ptr<AddressAccount>> accounts_;
   BinaryData mainAccount_;

   ////
   BinaryData parentID_;
   BinaryData walletID_;
   
protected:
   //tors
   AssetWallet(shared_ptr<WalletMeta> metaPtr) :
      dbEnv_(metaPtr->dbEnv_), dbName_(metaPtr->dbName_)
   {
      db_ = new LMDB(dbEnv_.get(), dbName_);
      decryptedData_ = make_shared<DecryptedDataContainer>(
         dbEnv_.get(), db_,
         metaPtr->getDefaultEncryptionKey(),
         metaPtr->getDefaultEncryptionKeyId());
   }

   static shared_ptr<LMDBEnv> getEnvFromFile(
      const string& path, unsigned dbCount = 3)
   {
      auto env = make_shared<LMDBEnv>(dbCount);
      env->open(path, MDB_WRITEMAP);

      return env;
   }

   //local
   BinaryDataRef getDataRefForKey(const BinaryData& key) const;
   void putData(const BinaryData& key, const BinaryData& data);
   void putData(BinaryWriter& key, BinaryWriter& data);

   //virtual
   virtual void putHeaderData(
      const BinaryData& parentID,
      const BinaryData& walletID);

   shared_ptr<AddressEntry> getAddressEntryForAsset(
      shared_ptr<AssetEntry>, 
      AddressEntryType);

   virtual void updateHashMap(void);

   //static
   static BinaryDataRef getDataRefForKey(const BinaryData& key, LMDB* db);
   static unsigned getDbCountAndNames(
      shared_ptr<LMDBEnv> dbEnv, 
      map<BinaryData, shared_ptr<WalletMeta>>&, 
      BinaryData& masterID, 
      BinaryData& mainWalletID);
   static void putDbName(LMDB* db, shared_ptr<WalletMeta>);
   static void setMainWallet(LMDB* db, shared_ptr<WalletMeta>);
   static void putData(LMDB* db, const BinaryData& key, const BinaryData& data);
   static void initWalletMetaDB(shared_ptr<LMDBEnv>, const string&);

   void updateAddressSet(shared_ptr<AddressEntry>);
   void writeAddressType(shared_ptr<AddressEntry>);
   AddressEntryType getAddrTypeForAccount(const BinaryData& ID);
   shared_ptr<AddressAccount> getAccountForID(const BinaryData& ID) const;

public:
   //tors
   virtual ~AssetWallet() = 0;

   //local
   shared_ptr<AddressEntry> getNewAddress(
      AddressEntryType aeType = AddressEntryType_Default);
   shared_ptr<AddressEntry> getNewAddress(const BinaryData& accountID,
      AddressEntryType);

   string getID(void) const; 
   virtual ReentrantLock lockDecryptedContainer(void);
   bool isDecryptedContainerLocked(void) const;
   
   shared_ptr<AssetEntry> getAssetForID(const BinaryData&) const;
   
   void extendPublicChain(unsigned);
   void extendPublicChainToIndex(const BinaryData&, unsigned);
   
   void extendPrivateChain(unsigned);
   void extendPrivateChainToIndex(const BinaryData&, unsigned);

   bool hasScrAddr(const BinaryData& scrAddr);
   const BinaryData& getAssetIDForAddr(const BinaryData& scrAddr);
   AddressEntryType getAddrTypeForID(const BinaryData& ID);
   shared_ptr<AddressEntry> getAddressEntryForID(
      const BinaryData&, AddressEntryType aeType = AddressEntryType_Default);
   const string& getFilename(void) const;

   void setPassphrasePromptLambda(
      function<SecureBinaryData(const BinaryData&)> lambda)
   {
      decryptedData_->setPassphrasePromptLambda(lambda);
   }

   //virtual
   virtual set<BinaryData> getAddrHashSet();
   virtual const SecureBinaryData& getDecryptedValue(
      shared_ptr<Asset_PrivateKey>) = 0;

   //static
   static shared_ptr<AssetWallet> loadMainWalletFromFile(const string& path);
};

////////////////////////////////////////////////////////////////////////////////
class AssetWallet_Single : public AssetWallet
{
   friend class AssetWallet;
   friend class AssetWallet_Multisig;

protected:
   shared_ptr<AssetEntry_Single> root_;

protected:
   //locals
   void readFromFile(void);

   //virtual
   void putHeaderData(const BinaryData& parentID,
      const BinaryData& walletID);

   //static
   static shared_ptr<AssetWallet_Single> initWalletDb(
      shared_ptr<WalletMeta>,
      shared_ptr<KeyDerivationFunction> masterKdf,
      DecryptedEncryptionKey& masterEncryptionKey,
      unique_ptr<Cypher>,
      const SecureBinaryData& passphrase,
      const SecureBinaryData& privateRoot,
      set<shared_ptr<AccountType>> accountTypes,
      unsigned lookup);

   static shared_ptr<AssetWallet_Single> initWalletDbFromPubRoot(
      shared_ptr<WalletMeta> metaPtr,
      SecureBinaryData& pubRoot,
      set<shared_ptr<AccountType>> accountTypes,
      unsigned lookup);

   static BinaryData computeWalletID(
      shared_ptr<DerivationScheme>,
      shared_ptr<AssetEntry>);

public:
   //tors
   AssetWallet_Single(shared_ptr<WalletMeta> metaPtr) :
      AssetWallet(metaPtr)
   {}

   //locals
   void changeMasterPassphrase(const SecureBinaryData&);
   const SecureBinaryData& getPublicRoot(void) const;
   const SecureBinaryData& getArmory135Chaincode(void) const;
   
   shared_ptr<AssetEntry> getMainAccountAssetForIndex(unsigned) const;
   unsigned getMainAccountAssetCount(void) const;
   const BinaryData& getMainAccountID(void) const { return mainAccount_; }

   //virtual
   const SecureBinaryData& getDecryptedValue(
      shared_ptr<Asset_PrivateKey>);

   //static
   static shared_ptr<AssetWallet_Single> createFromPrivateRoot_Armory135(
      const string& folder,
      const SecureBinaryData& privateRoot,
      const SecureBinaryData& passphrase,
      unsigned lookup);

   static shared_ptr<AssetWallet_Single> createFromPublicRoot_Armory135(
      const string& folder,
      SecureBinaryData& privateRoot,
      SecureBinaryData& chainCode,
      unsigned lookup);

   static shared_ptr<AssetWallet_Single> createFromPrivateRoot_BIP32(
      const string& folder,
      const SecureBinaryData& privateRoot,
      const vector<unsigned>& derivationPath,
      const SecureBinaryData& passphrase,
      unsigned lookup);

   static shared_ptr<AssetWallet_Single> createFromPublicRoot_BIP32(
      const string& folder,
      SecureBinaryData& privateRoot,
      SecureBinaryData& chainCode,
      unsigned lookup);
};

////////////////////////////////////////////////////////////////////////////////
class AssetWallet_Multisig : public AssetWallet
{
   friend class AssetWallet;

private:
   atomic<unsigned> chainLength_;

protected:
   //local
   void readFromFile(void);

   //virtual
   const SecureBinaryData& getDecryptedValue(
      shared_ptr<Asset_PrivateKey>);

public:
   //tors
   AssetWallet_Multisig(shared_ptr<WalletMeta> metaPtr) :
      AssetWallet(metaPtr)
   {}

   //virtual
   bool setImport(int importID, const SecureBinaryData& pubkey);

   static shared_ptr<AssetWallet> createFromWallets(
      vector<shared_ptr<AssetWallet>> wallets,
      unsigned M,
      unsigned lookup = UINT32_MAX);



   //local
};

////////////////////////////////////////////////////////////////////////////////
class ResolverFeed_AssetWalletSingle : public ResolverFeed
{
private:
   shared_ptr<AssetWallet> wltPtr_;

protected:
   map<BinaryDataRef, BinaryDataRef> hash_to_preimage_;
   map<BinaryDataRef, shared_ptr<AssetEntry_Single>> pubkey_to_asset_;

private:

   void addToMap(shared_ptr<AddressEntry> addrPtr)
   {
      try
      {
         BinaryDataRef hash(addrPtr->getHash());
         BinaryDataRef preimage(addrPtr->getPreimage());

         hash_to_preimage_.insert(make_pair(hash, preimage));
      }
      catch (runtime_error&)
      {}

      auto addr_nested = dynamic_pointer_cast<AddressEntry_Nested>(addrPtr);
      if (addr_nested != nullptr)
      {
         addToMap(addr_nested->getPredecessor());
         return;
      }

      auto addr_with_asset = dynamic_pointer_cast<AddressEntry_WithAsset>(addrPtr);
      if (addr_with_asset != nullptr)
      {
         BinaryDataRef preimage(addrPtr->getPreimage());
         auto& asset = addr_with_asset->getAsset();

         auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset);
         if (asset_single == nullptr)
            throw WalletException("multisig asset in asset_single resolver");

         pubkey_to_asset_.insert(make_pair(preimage, asset_single));
      }
   }

public:
   //tors
   ResolverFeed_AssetWalletSingle(shared_ptr<AssetWallet_Single> wltPtr) :
      wltPtr_(wltPtr)
   {
      auto& addrMap = wltPtr->addresses_;
      for (auto& addr : addrMap)
         addToMap(addr.second);
   }

   //virtual
   BinaryData getByVal(const BinaryData& key)
   {
      //find id for the key
      auto iter = hash_to_preimage_.find(key);
      if (iter == hash_to_preimage_.end())
         throw runtime_error("invalid value");

      return iter->second;
   }

   virtual const SecureBinaryData& getPrivKeyForPubkey(const BinaryData& pubkey)
   {
      auto pubkeyref = BinaryDataRef(pubkey);
      auto iter = pubkey_to_asset_.find(pubkeyref);
      if (iter == pubkey_to_asset_.end())
         throw runtime_error("invalid value");

      const auto& privkeyAsset = iter->second->getPrivKey();
      return wltPtr_->getDecryptedValue(privkeyAsset);
   }
};

////////////////////////////////////////////////////////////////////////////////
class ResolverFeed_AssetWalletSingle_ForMultisig : public ResolverFeed
{
private:
   shared_ptr<AssetWallet> wltPtr_;

protected:
   map<BinaryDataRef, shared_ptr<AssetEntry_Single>> pubkey_to_asset_;

private:

   void addToMap(shared_ptr<AssetEntry> asset)
   {
      auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw WalletException("multisig asset in asset_single resolver");

      auto pubkey = asset_single->getPubKey();
      BinaryDataRef pubkey_compressed(pubkey->getCompressedKey());
      BinaryDataRef pubkey_uncompressed(pubkey->getUncompressedKey());

      pubkey_to_asset_.insert(make_pair(pubkey_compressed, asset_single));
      pubkey_to_asset_.insert(make_pair(pubkey_uncompressed, asset_single));
   }

public:
   //tors
   ResolverFeed_AssetWalletSingle_ForMultisig(shared_ptr<AssetWallet_Single> wltPtr) :
      wltPtr_(wltPtr)
   {
      for (auto& addr_account : wltPtr->accounts_)
      {
         for (auto& asset_account : addr_account->getAccountMap())
         {
            for (unsigned i = 0; i < asset_account.second->getAssetCount(); i++)
            {
               auto asset = asset_account.second->getAssetForIndex(i);
               addToMap(asset);
            }
         }
      }
   }

   //virtual
   BinaryData getByVal(const BinaryData& key)
   {
      //find id for the key
      throw runtime_error("no preimages in multisig feed");
      return BinaryData();
   }

   virtual const SecureBinaryData& getPrivKeyForPubkey(const BinaryData& pubkey)
   {
      auto pubkeyref = BinaryDataRef(pubkey);
      auto iter = pubkey_to_asset_.find(pubkeyref);
      if (iter == pubkey_to_asset_.end())
         throw runtime_error("invalid value");

      const auto& privkeyAsset = iter->second->getPrivKey();
      return wltPtr_->getDecryptedValue(privkeyAsset);
   }
};

#endif
