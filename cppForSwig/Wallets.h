////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2019, goatpig                                          //
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
#include "BIP32_Node.h"

#define WALLETTYPE_KEY        0x00000001
#define PARENTID_KEY          0x00000002
#define WALLETID_KEY          0x00000003
#define ROOTASSET_KEY         0x00000007
#define MAIN_ACCOUNT_KEY      0x00000008
#define WALLET_SEED_KEY       0x00000009

#define MASTERID_KEY          0x000000A0
#define MAINWALLET_KEY        0x000000A1

#define WALLETMETA_PREFIX     0xB0

#define WALLETMETA_DBNAME "WalletHeader"

#define VERSION_MAJOR      2
#define VERSION_MINOR      1
#define VERSION_REVISION   0

static SecureBinaryData cppyyDummySilencer;

class WalletException : public std::runtime_error
{
public:
   WalletException(const std::string& msg) : std::runtime_error(msg)
   {}
};

class NoAssetException : public std::runtime_error
{
public:
   NoAssetException(const std::string& msg) : std::runtime_error(msg)
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
   std::shared_ptr<LMDBEnv> dbEnv_;
   WalletMetaType type_;
   BinaryData parentID_;
   BinaryData walletID_;
   std::string dbName_;

   uint8_t versionMajor_ = 0;
   uint16_t versionMinor_ = 0;
   uint16_t revision_ = 0;

   SecureBinaryData defaultEncryptionKey_;
   SecureBinaryData defaultEncryptionKeyId_;

   SecureBinaryData defaultKdfId_;
   SecureBinaryData masterEncryptionKeyId_;

   //tors
   WalletMeta(std::shared_ptr<LMDBEnv> env, WalletMetaType type) :
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
   std::string getWalletIDStr(void) const
   {
      if (walletID_.getSize() == 0)
         throw WalletException("empty wallet id");

      std::string idStr(walletID_.getCharPtr(), walletID_.getSize());
      return idStr;
   }

   BinaryData serializeVersion(void) const;
   void unserializeVersion(BinaryRefReader&);

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
   static std::shared_ptr<WalletMeta> deserialize(std::shared_ptr<LMDBEnv> env,
      BinaryDataRef key, BinaryDataRef val);
};

////
struct WalletMeta_Single : public WalletMeta
{
   //tors
   WalletMeta_Single(std::shared_ptr<LMDBEnv> dbEnv) :
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
   WalletMeta_Multisig(std::shared_ptr<LMDBEnv> dbEnv) :
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
   WalletMeta_Subwallet(std::shared_ptr<LMDBEnv> dbEnv) :
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
   std::shared_ptr<LMDBEnv> dbEnv_ = nullptr;
   LMDB* db_ = nullptr;
   const std::string dbName_;

   std::shared_ptr<DecryptedDataContainer> decryptedData_;
   std::map<BinaryData, std::shared_ptr<AddressAccount>> accounts_;
   std::map<MetaAccountType, std::shared_ptr<MetaDataAccount>> metaDataAccounts_;
   BinaryData mainAccount_;

   ////
   BinaryData parentID_;
   BinaryData walletID_;
   
protected:
   //tors
   AssetWallet(std::shared_ptr<WalletMeta> metaPtr) :
      dbEnv_(metaPtr->dbEnv_), dbName_(metaPtr->dbName_)
   {
      db_ = new LMDB(dbEnv_.get(), dbName_);
      decryptedData_ = std::make_shared<DecryptedDataContainer>(
         dbEnv_.get(), db_,
         metaPtr->getDefaultEncryptionKey(),
         metaPtr->getDefaultEncryptionKeyId(),
         metaPtr->defaultKdfId_, metaPtr->masterEncryptionKeyId_);
   }

   static std::shared_ptr<LMDBEnv> getEnvFromFile(
      const std::string& path, unsigned dbCount = 3)
   {
      auto env = std::make_shared<LMDBEnv>(dbCount);
      env->open(path, MDB_WRITEMAP);

      return env;
   }

   //local
   BinaryDataRef getDataRefForKey(const BinaryData& key) const;
   void putData(const BinaryData& key, const BinaryData& data);
   void putData(BinaryWriter& key, BinaryWriter& data);

   //address type methods
   AddressEntryType getAddrTypeForAccount(const BinaryData& ID);

   void loadMetaAccounts(void);

   //virtual
   virtual void putHeaderData(
      const BinaryData& parentID,
      const BinaryData& walletID);

   virtual void updateHashMap(void);
   virtual void readFromFile(void) = 0;

   //static
   static BinaryDataRef getDataRefForKey(const BinaryData& key, LMDB* db);
   static unsigned getDbCountAndNames(
      std::shared_ptr<LMDBEnv> dbEnv,
      std::map<BinaryData, std::shared_ptr<WalletMeta>>&,
      BinaryData& masterID, 
      BinaryData& mainWalletID);
   static void putDbName(LMDB* db, std::shared_ptr<WalletMeta>);
   static void setMainWallet(LMDB* db, std::shared_ptr<WalletMeta>);
   static void putData(LMDB* db, const BinaryData& key, const BinaryData& data);
   static void initWalletMetaDB(std::shared_ptr<LMDBEnv>, const std::string&);

public:
   //tors
   virtual ~AssetWallet() = 0;

   //local
   std::shared_ptr<AddressEntry> getNewAddress(
      AddressEntryType aeType = AddressEntryType_Default);
   std::shared_ptr<AddressEntry> getNewAddress(const BinaryData& accountID,
      AddressEntryType);
   std::shared_ptr<AddressEntry> getNewChangeAddress(
      AddressEntryType aeType = AddressEntryType_Default);

   std::string getID(void) const;
   virtual ReentrantLock lockDecryptedContainer(void);
   bool isDecryptedContainerLocked(void) const;
   
   std::shared_ptr<AssetEntry> getAssetForID(const BinaryData&) const;
   
   void extendPublicChain(unsigned);
   void extendPublicChainToIndex(const BinaryData&, unsigned);
   
   void extendPrivateChain(unsigned);
   void extendPrivateChainToIndex(const BinaryData&, unsigned);

   bool hasScrAddr(const BinaryData& scrAddr);
   const std::pair<BinaryData, AddressEntryType>& 
      getAssetIDForAddr(const BinaryData& scrAddr);
   AddressEntryType getAddrTypeForID(const BinaryData& ID);
   std::shared_ptr<AddressEntry> 
      getAddressEntryForID(const BinaryData&) const;
   void shutdown(void);

   void setPassphrasePromptLambda(
      std::function<SecureBinaryData(const BinaryData&)> lambda)
   {
      decryptedData_->setPassphrasePromptLambda(lambda);
   }
   
   void resetPassphrasePromptLambda(void)
   {
      decryptedData_->resetPassphraseLambda();
   }

   void addMetaAccount(MetaAccountType);
   std::shared_ptr<MetaDataAccount> getMetaAccount(MetaAccountType);
   std::shared_ptr<AddressAccount> getAccountForID(const BinaryData& ID) const;
   
   const std::string& getDbFilename(void) const;
   std::shared_ptr<LMDBEnv> getDbEnv(void) const { return dbEnv_; }

   std::set<BinaryData> getAccountIDs(void) const;
   std::map<BinaryData, std::shared_ptr<AddressEntry>> 
      getUsedAddressMap(void) const;

   std::shared_ptr<AddressAccount> 
      createAccount(std::shared_ptr<AccountType>);

   //virtual
   virtual std::set<BinaryData> getAddrHashSet();
   virtual const SecureBinaryData& getDecryptedValue(
      std::shared_ptr<Asset_EncryptedData>) = 0;

   static std::string forkWathcingOnly(const std::string&);

   //static
   static std::shared_ptr<AssetWallet> loadMainWalletFromFile(
      const std::string& path);
};

////////////////////////////////////////////////////////////////////////////////
class AssetWallet_Single : public AssetWallet
{
   friend class AssetWallet;
   friend class AssetWallet_Multisig;

protected:
   std::shared_ptr<AssetEntry_Single> root_ = nullptr;
   std::shared_ptr<EncryptedSeed> seed_ = nullptr;

protected:
   //virtual
   void readFromFile(void);
   void putHeaderData(const BinaryData& parentID,
      const BinaryData& walletID);

   //static
   static std::shared_ptr<AssetWallet_Single> initWalletDb(
      std::shared_ptr<WalletMeta>,
      std::shared_ptr<KeyDerivationFunction> masterKdf,
      DecryptedEncryptionKey& masterEncryptionKey,
      std::unique_ptr<Cipher>,
      const SecureBinaryData& passphrase,
      const SecureBinaryData& privateRoot,
      const SecureBinaryData& chaincode,
      std::set<std::shared_ptr<AccountType>> accountTypes,
      unsigned lookup);

   static std::shared_ptr<AssetWallet_Single> initWalletDbFromPubRoot(
      std::shared_ptr<WalletMeta> metaPtr,
      SecureBinaryData& pubRoot,
      std::set<std::shared_ptr<AccountType>> accountTypes,
      unsigned lookup);

   static BinaryData computeWalletID(
      std::shared_ptr<DerivationScheme>,
      std::shared_ptr<AssetEntry>);

private:
   static void copyPublicData(
      std::shared_ptr<AssetWallet_Single>, std::shared_ptr<LMDBEnv>);
   void setSeed(const SecureBinaryData&, const SecureBinaryData&);

public:
   //tors
   AssetWallet_Single(std::shared_ptr<WalletMeta> metaPtr) :
      AssetWallet(metaPtr)
   {}

   //locals
   void changeMasterPassphrase(const SecureBinaryData&);
   std::shared_ptr<AssetEntry_Single> getRoot(void) const { return root_; }
   const SecureBinaryData& getPublicRoot(void) const;
   std::shared_ptr<AssetEntry> getAccountRoot(const BinaryData& accountID) const;
   const SecureBinaryData& getArmory135Chaincode(void) const;
   
   const BinaryData& createBIP32Account(
      std::shared_ptr<AssetEntry_BIP32Root> parentNode,
      std::vector<unsigned> derPath,
      bool isMain = false);
   const BinaryData& createBIP32Account(
      std::shared_ptr<AssetEntry_BIP32Root> parentNode,
      std::vector<unsigned> derPath,
      std::shared_ptr<AccountType_BIP32_Custom>);

   bool isWatchingOnly(void) const;

   std::shared_ptr<AssetEntry> getMainAccountAssetForIndex(unsigned) const;
   unsigned getMainAccountAssetCount(void) const;
   const BinaryData& getMainAccountID(void) const { return mainAccount_; }

   const SecureBinaryData& getDecryptedPrivateKeyForAsset(
      std::shared_ptr<AssetEntry_Single>);

   std::shared_ptr<EncryptedSeed> getEncryptedSeed(void) const { return seed_; }

   //virtual
   const SecureBinaryData& getDecryptedValue(
      std::shared_ptr<Asset_EncryptedData>);

   //static
   static std::shared_ptr<AssetWallet_Single> createFromBIP32Node(
      const BIP32_Node& node,
      std::set<std::shared_ptr<AccountType>> accountTypes,
      const SecureBinaryData& passphrase,
      const std::string& folder,
      unsigned lookup);

   static std::shared_ptr<AssetWallet_Single> createFromPrivateRoot_Armory135(
      const std::string& folder,
      const SecureBinaryData& privateRoot,
      const SecureBinaryData& passphrase,
      unsigned lookup);

   static std::shared_ptr<AssetWallet_Single> createFromPublicRoot_Armory135(
      const std::string& folder,
      SecureBinaryData& privateRoot,
      SecureBinaryData& chainCode,
      unsigned lookup);

   static std::shared_ptr<AssetWallet_Single> createFromSeed_BIP32(
      const std::string& folder,
      const SecureBinaryData& seed,
      const std::vector<unsigned>& derivationPath,
      const SecureBinaryData& passphrase,
      unsigned lookup);

   static std::shared_ptr<AssetWallet_Single> createFromBase58_BIP32(
      const std::string& folder,
      const SecureBinaryData& b58,
      const std::vector<unsigned>& derivationPath,
      const SecureBinaryData& passphrase,
      unsigned lookup);

   static std::shared_ptr<AssetWallet_Single> createFromSeed_BIP32_Blank(
      const std::string& folder,
      const SecureBinaryData& seed,
      const SecureBinaryData& passphrase);
};

////////////////////////////////////////////////////////////////////////////////
class AssetWallet_Multisig : public AssetWallet
{
   friend class AssetWallet;

private:
   std::atomic<unsigned> chainLength_;

protected:

   //virtual
   void readFromFile(void);
   const SecureBinaryData& getDecryptedValue(
      std::shared_ptr<Asset_EncryptedData>);

public:
   //tors
   AssetWallet_Multisig(std::shared_ptr<WalletMeta> metaPtr) :
      AssetWallet(metaPtr)
   {}

   //virtual
   bool setImport(int importID, const SecureBinaryData& pubkey);

   static std::shared_ptr<AssetWallet> createFromWallets(
      std::vector<std::shared_ptr<AssetWallet>> wallets,
      unsigned M,
      unsigned lookup = UINT32_MAX);

   //local
};

////////////////////////////////////////////////////////////////////////////////
class ResolverFeed_AssetWalletSingle : public ResolverFeed
{
private:
   std::shared_ptr<AssetWallet_Single> wltPtr_;

protected:
   std::map<BinaryData, BinaryData> hash_to_preimage_;
   std::map<BinaryData, std::shared_ptr<AssetEntry_Single>> pubkey_to_asset_;

private:

   void addToMap(std::shared_ptr<AddressEntry> addrPtr)
   {
      try
      {
         BinaryDataRef hash(addrPtr->getHash());
         BinaryDataRef preimage(addrPtr->getPreimage());

         hash_to_preimage_.insert(std::make_pair(hash, preimage));
      }
      catch (std::runtime_error&)
      {}

      auto addr_nested = std::dynamic_pointer_cast<AddressEntry_Nested>(addrPtr);
      if (addr_nested != nullptr)
      {
         addToMap(addr_nested->getPredecessor());
         return;
      }

      auto addr_with_asset = std::dynamic_pointer_cast<AddressEntry_WithAsset>(addrPtr);
      if (addr_with_asset != nullptr)
      {
         BinaryDataRef preimage(addrPtr->getPreimage());
         auto& asset = addr_with_asset->getAsset();

         auto asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(asset);
         if (asset_single == nullptr)
            throw WalletException("multisig asset in asset_single resolver");

         pubkey_to_asset_.insert(std::make_pair(preimage, asset_single));
      }
   }

   std::pair<std::shared_ptr<AssetEntry>, AddressEntryType>
      getAssetPairForKey(const BinaryData& key)
   {
      //run through accounts
      auto& accounts = wltPtr_->accounts_;
      for (auto& accPair : accounts)
      {
         /*
         Accounts store script hashes with their relevant prefix, resolver
         asks uses unprefixed hashes as found in the actual outputs. Hence,
         all possible script prefixes will be prepended to the key to
         look for the relevant asset ID
         */

         auto accPtr = accPair.second;

         auto prefixSet = accPtr->getAddressTypeSet();
         auto& hashMap = accPtr->getAddressHashMap();
         std::set<uint8_t> usedPrefixes;

         for (auto& addrType : prefixSet)
         {
            BinaryWriter prefixedKey;
            try
            {
               auto prefix = AddressEntry::getPrefixByte(addrType);

               //skip prefixes already used
               auto insertIter = usedPrefixes.insert(prefix);
               if (!insertIter.second)
                  continue;

               prefixedKey.put_uint8_t(prefix);
            }
            catch (AddressException&)
            {}

            prefixedKey.put_BinaryData(key);

            auto iter = hashMap.find(prefixedKey.getData());
            if (iter == hashMap.end())
               continue;

            /*
            We have a hit for this prefix, return the asset and its
            address type. 
            
            Note that we can't use addrType, as it may use a prefix 
            shared across several address types (i.e. P2SH-P2PK and 
            P2SH-P2WPKH).

            Therefor, we return the address type attached to hash 
            rather the one used to roll the prefix.
            */

            auto asset =
               accPtr->getAssetForID(iter->second.first.getSliceRef(4, 8));
            return std::make_pair(asset, iter->second.second);
         }
      }

      return std::make_pair(nullptr, AddressEntryType_Default);
   }

public:
   //tors
   ResolverFeed_AssetWalletSingle(std::shared_ptr<AssetWallet_Single> wltPtr) :
      wltPtr_(wltPtr)
   {}

   //virtual
   BinaryData getByVal(const BinaryData& key)
   {
      //check cached hits first
      auto iter = hash_to_preimage_.find(key);
      if (iter != hash_to_preimage_.end())
         return iter->second;

      //short of that, try to get the asset for this key
      auto assetPair = getAssetPairForKey(key);
      if (assetPair.first == nullptr ||
         assetPair.second == AddressEntryType_Default)
         throw std::runtime_error("could not resolve key");

      auto addrPtr = AddressEntry::instantiate(
         assetPair.first, assetPair.second);

      /*
      We cache all hits at this stage to speed up further resolution.

      In the case of nested addresses, we have to cache the predessors
      anyways as they are most likely going to be requested later, yet
      there is no guarantee the account address hashmap which our
      resolution is based on carries the predecessor hashes. addToMap
      takes care of this for us.
      */

      addToMap(addrPtr);
      return addrPtr->getPreimage();
   }


   virtual const SecureBinaryData& getPrivKeyForPubkey(const BinaryData& pubkey)
   {
      //check cache first
      auto pubkeyref = BinaryDataRef(pubkey);
      auto iter = pubkey_to_asset_.find(pubkeyref);
      if (iter != pubkey_to_asset_.end())
         return wltPtr_->getDecryptedPrivateKeyForAsset(iter->second);

      /*
      Lacking a cache hit, we need to get the asset for this pubkey. All
      pubkeys are carried as assets, and all assets are expressed as all
      possible script hash variations within an account's hash map.

      Therefor, converting this pubkey to one of the eligible script hash
      variation should yield a hit from the key to asset resolution logic.

      From that asset object, we can then get the private key.

      Conveniently, the only hash ever used on public keys is
      BtcUtils::getHash160
      */

      auto&& hash = BtcUtils::getHash160(pubkey);
      auto assetPair = getAssetPairForKey(hash);
      if (assetPair.first == nullptr)
         throw NoAssetException("invalid pubkey");

      auto assetSingle =
         std::dynamic_pointer_cast<AssetEntry_Single>(assetPair.first);
      if (assetSingle == nullptr)
         throw std::logic_error("invalid pubkey");

      return wltPtr_->getDecryptedPrivateKeyForAsset(assetSingle);

      /*
      In case of NoAssetException failure, it is still possible this public key 
      is used in an exotic script (multisig or other).
      Use ResolverFeed_AssetWalletSingle_Exotic for a wallet carrying
      that kind of scripts.

      logic_error means the asset was found but it does not carry the private 
      key.

      DecryptedDataContainerException means the wallet failed to decrypt the 
      encrypted pubkey (bad passphrase or unlocked wallet most likely).
      */
   }

   void seedFromAddressEntry(std::shared_ptr<AddressEntry> addrPtr)
   {
      try
      {
         //add hash to preimage pair
         auto& hash = addrPtr->getHash();
         auto& preimage = addrPtr->getPreimage();
         hash_to_preimage_.insert(std::make_pair(hash, preimage));
      }
      catch (AddressException&)
      {
         return;
      }

      //is this address nested?
      auto addrNested =
         std::dynamic_pointer_cast<AddressEntry_Nested>(addrPtr);
      if (addrNested == nullptr)
         return; //return if not

      //seed the predecessor too
      seedFromAddressEntry(addrNested->getPredecessor());
   }
};

////////////////////////////////////////////////////////////////////////////////
class ResolverFeed_AssetWalletSingle_Exotic : 
   public ResolverFeed_AssetWalletSingle
{
   //tors
   ResolverFeed_AssetWalletSingle_Exotic(
      std::shared_ptr<AssetWallet_Single> wltPtr) :
      ResolverFeed_AssetWalletSingle(wltPtr)
   {}

   //virtual
   const SecureBinaryData& getPrivKeyForPubkey(const BinaryData& pubkey)
   {
      try
      {
         return ResolverFeed_AssetWalletSingle::getPrivKeyForPubkey(pubkey);
      }
      catch (NoAssetException&)
      {}
      
      /*
      Failed to get the asset for the pukbey by hashing it, run through
      all assets linearly instead.
      */

      //grab account

      //grab asset account

      //run through assets, check pubkeys
      // Dummy here only to make cppyy happy. Need to remove.
      return cppyyDummySilencer;
   }
};

////////////////////////////////////////////////////////////////////////////////
class ResolverFeed_AssetWalletSingle_ForMultisig : public ResolverFeed
{
private:
   std::shared_ptr<AssetWallet> wltPtr_;

protected:
   std::map<BinaryDataRef, std::shared_ptr<AssetEntry_Single>> pubkey_to_asset_;

private:

   void addToMap(std::shared_ptr<AssetEntry> asset)
   {
      auto asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw WalletException("multisig asset in asset_single resolver");

      auto pubkey = asset_single->getPubKey();
      BinaryDataRef pubkey_compressed(pubkey->getCompressedKey());
      BinaryDataRef pubkey_uncompressed(pubkey->getUncompressedKey());

      pubkey_to_asset_.insert(std::make_pair(pubkey_compressed, asset_single));
      pubkey_to_asset_.insert(std::make_pair(pubkey_uncompressed, asset_single));
   }

public:
   //tors
   ResolverFeed_AssetWalletSingle_ForMultisig(std::shared_ptr<AssetWallet_Single> wltPtr) :
      wltPtr_(wltPtr)
   {
      for (auto& addr_account : wltPtr->accounts_)
      {
         for (auto& asset_account : addr_account.second->getAccountMap())
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
      throw std::runtime_error("no preimages in multisig feed");
      return BinaryData();
   }

   virtual const SecureBinaryData& getPrivKeyForPubkey(const BinaryData& pubkey)
   {
      auto pubkeyref = BinaryDataRef(pubkey);
      auto iter = pubkey_to_asset_.find(pubkeyref);
      if (iter == pubkey_to_asset_.end())
         throw std::runtime_error("invalid value");

      const auto& privkeyAsset = iter->second->getPrivKey();
      return wltPtr_->getDecryptedValue(privkeyAsset);
   }
};

#endif
