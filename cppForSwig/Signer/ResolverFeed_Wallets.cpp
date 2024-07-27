////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2024, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ResolverFeed_Wallets.h"
#include "Wallets/Addresses.h"
#include "Wallets/Wallets.h"

using namespace Armory;

////////////////////////////////////////////////////////////////////////////////
////
//// ResolverFeed_AssetWalletSingle
////
////////////////////////////////////////////////////////////////////////////////
void Signing::ResolverFeed_AssetWalletSingle::addToMap(
   std::shared_ptr<AddressEntry> addrPtr)
{
   try
   {
      BinaryDataRef hash(addrPtr->getHash());
      BinaryDataRef preimage(addrPtr->getPreimage());

      hash_to_preimage_.insert(std::make_pair(hash, preimage));
   }
   catch (const std::exception&)
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

      auto asset_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset);
      if (asset_single == nullptr)
         throw Wallets::WalletException("multisig asset in asset_single resolver");

      pubkey_to_asset_.insert(std::make_pair(preimage, asset_single));
   }
}

////////////////////////////////////////////////////////////////////////////////
std::pair<std::shared_ptr<Assets::AssetEntry>, AddressEntryType>
Signing::ResolverFeed_AssetWalletSingle::getAssetPairForKey(
   const BinaryData& key) const
{
   //run through accounts
   auto accountIDs = wltPtr_->getAccountIDs();
   for (auto& accID : accountIDs)
   {
      /*
      Accounts store script hashes with their relevant prefix, resolver
      uses unprefixed hashes as found in the actual outputs. Hence,
      all possible script prefixes will be prepended to the key to
      look for the relevant asset ID
      */

      auto accPtr = wltPtr_->getAccountForID(accID);

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
         catch (const AddressException&)
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

         auto asset = accPtr->getAssetForID(iter->second.first);
         return std::make_pair(asset, iter->second.second);
      }
   }

   return std::make_pair(nullptr, AddressEntryType_Default);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::ResolverFeed_AssetWalletSingle::getByVal(
   const BinaryData& key)
{
   //check cached hits first
   auto iter = hash_to_preimage_.find(key);
   if (iter != hash_to_preimage_.end())
      return iter->second;

   //short of that, try to get the asset for this key
   auto assetPair = getAssetPairForKey(key);
   if (assetPair.first == nullptr ||
      assetPair.second == AddressEntryType_Default)
   {
      throw std::runtime_error("could not resolve key");
   }

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

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& Signing::ResolverFeed_AssetWalletSingle::getPrivKeyForPubkey(
   const BinaryData& pubkey)
{
   //check cache first
   {
      auto pubkeyref = pubkey.getRef();
      auto cacheIter = pubkey_to_asset_.find(pubkeyref);
      if (cacheIter != pubkey_to_asset_.end())
      {
         return wltPtr_->getDecryptedPrivateKeyForAsset(
            cacheIter->second);
      }
   }

   //if we have a bip32 path hint for this pubkey, use that
   {
      auto pathIter = bip32Paths_.find(pubkey);
      if (pathIter != bip32Paths_.end())
      {
         if (!pathIter->second.second.isValid())
         {
            pathIter->second.second =
               wltPtr_->derivePrivKeyFromPath(pathIter->second.first);
         }

         return wltPtr_->getDecryptedPrivateKeyForId(pathIter->second.second);
      }
   }

   /*
   Lacking a cache hit, we need to get the asset for this pubkey. All
   pubkeys are carried as assets, and all assets are expressed as all
   possible script hash variations within an account's hash map.

   Therefor, converting this pubkey to one of the eligible script hash
   variation should yield a hit from the key to asset resolution logic.

   From that asset object, we can then get the private key.
   */

   auto hash = BtcUtils::getHash160(pubkey);
   auto assetPair = getAssetPairForKey(hash);
   if (assetPair.first == nullptr)
      throw NoAssetException("invalid pubkey");

   auto assetSingle =
      std::dynamic_pointer_cast<Assets::AssetEntry_Single>(assetPair.first);
   if (assetSingle == nullptr)
      throw std::logic_error("invalid asset type");

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

////////////////////////////////////////////////////////////////////////////////
Signing::BIP32_AssetPath
Signing::ResolverFeed_AssetWalletSingle::resolveBip32PathForPubkey(
   const BinaryData& pubkey)
{
   //check cache first
   {
      auto pubkeyref = pubkey.getRef();
      auto cacheIter = pubkey_to_asset_.find(pubkeyref);
      if (cacheIter != pubkey_to_asset_.end())
         return wltPtr_->getBip32PathForAsset(cacheIter->second);
   }

   auto hash = BtcUtils::getHash160(pubkey);
   auto assetPair = getAssetPairForKey(hash);
   if (assetPair.first == nullptr)
      throw NoAssetException("invalid pubkey");

   return wltPtr_->getBip32PathForAsset(assetPair.first);
}

////////////////////////////////////////////////////////////////////////////////
void Signing::ResolverFeed_AssetWalletSingle::seedFromAddressEntry(
   std::shared_ptr<AddressEntry> addrPtr)
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

////////////////////////////////////////////////////////////////////////////////
void Signing::ResolverFeed_AssetWalletSingle::setBip32PathForPubkey(
   const BinaryData& pubkey, const BIP32_AssetPath& path)
{
   bip32Paths_.emplace(pubkey, std::make_pair(path, Wallets::AssetId()));
}

////////////////////////////////////////////////////////////////////////////////
////
//// ResolverFeed_AssetWalletSingle
////
////////////////////////////////////////////////////////////////////////////////
Signing::ResolverFeed_AssetWalletSingle_ForMultisig::
ResolverFeed_AssetWalletSingle_ForMultisig(
   std::shared_ptr<Wallets::AssetWallet_Single> wltPtr) :
   wltPtr_(wltPtr)
{
   auto accountIDs = wltPtr->getAccountIDs();
   for (auto& accID : accountIDs)
   {
      auto addrAcc = wltPtr->getAccountForID(accID);
      for (auto& assID : addrAcc->getAccountIdSet())
      {
         auto assAcc = addrAcc->getAccountForID(assID);
         for (unsigned i = 0; i < assAcc->getAssetCount(); i++)
         {
            auto asset = assAcc->getAssetForKey(i);
            addToMap(asset);
         }
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void Signing::ResolverFeed_AssetWalletSingle_ForMultisig::addToMap(
   std::shared_ptr<Assets::AssetEntry> asset)
{
   auto asset_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(asset);
   if (asset_single == nullptr)
      throw NoAssetException("multisig asset in asset_single resolver");

   auto pubkey = asset_single->getPubKey();
   BinaryDataRef pubkey_compressed(pubkey->getCompressedKey());
   BinaryDataRef pubkey_uncompressed(pubkey->getUncompressedKey());

   pubkey_to_asset_.insert(std::make_pair(pubkey_compressed, asset_single));
   pubkey_to_asset_.insert(std::make_pair(pubkey_uncompressed, asset_single));
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::ResolverFeed_AssetWalletSingle_ForMultisig::
   getByVal(const BinaryData&)
{
   //find id for the key
   throw std::runtime_error("no preimages in multisig feed");
}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& Signing::ResolverFeed_AssetWalletSingle_ForMultisig::
getPrivKeyForPubkey(const BinaryData& pubkey)
{
   auto pubkeyref = BinaryDataRef(pubkey);
   auto iter = pubkey_to_asset_.find(pubkeyref);
   if (iter == pubkey_to_asset_.end())
      throw std::runtime_error("invalid value");

   const auto& privkeyAsset = iter->second->getPrivKey();
   return wltPtr_->getDecryptedValue(privkeyAsset);
}

////////////////////////////////////////////////////////////////////////////////
Signing::BIP32_AssetPath Signing::ResolverFeed_AssetWalletSingle_ForMultisig::
resolveBip32PathForPubkey(const BinaryData&)
{
   throw std::runtime_error("invalid pubkey");
}

////////////////////////////////////////////////////////////////////////////////
void Signing::ResolverFeed_AssetWalletSingle_ForMultisig::
setBip32PathForPubkey(const BinaryData&, const BIP32_AssetPath&)
{}
