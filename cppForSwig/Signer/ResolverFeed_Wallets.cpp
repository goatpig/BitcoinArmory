////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ResolverFeed_Wallets.h"
#include <Utils/BtcUtils.h>
#include <Wallets/Accounts/AddressAccounts.h>
#include <Wallets/Addresses.h>
#include <Wallets/Wallets.h>
#include <Wallets/WalletIdTypes.h>

using namespace Armory;
using namespace Armory::Signing;

namespace
{
   std::pair<std::shared_ptr<Assets::AssetEntry>, AddressEntryType>
   getAssetPairForKey(std::shared_ptr<Wallets::AssetWallet_Single> wltPtr,
      const BinaryData& key)
   {
      //run through accounts
      auto accountIDs = wltPtr->getAccountIDs();
      for (const auto& accID : accountIDs) {
         /*
         Accounts store script hashes with their relevant prefix, resolver
         uses unprefixed hashes as found in the actual outputs. Hence,
         all possible script prefixes will be prepended to the key to
         look for the relevant asset ID
         */

         auto accPtr = wltPtr->getAccountForID(accID);
         auto prefixSet = accPtr->getAddressTypeSet();
         const auto& hashMap = accPtr->getAddressHashMap();
         std::set<uint8_t> usedPrefixes;

         if (prefixSet.empty()) {
            /*
            edge case: if we do not have a prefix set for the account, such
            as imports, run through the hashmap with its advertized address
            types.
            */
            for (const auto& prefixedHash : hashMap) {
               auto rawHash = prefixedHash.first.getSliceRef(
                  1, prefixedHash.first.getSize() - 1);
               if (rawHash == key) {
                  auto asset = accPtr->getAssetForID(prefixedHash.second.first);
                  return { asset, prefixedHash.second.second };
               }
            }
         }

         for (const auto& addrType : prefixSet) {
            BinaryWriter prefixedKey;
            try {
               //skip prefixes already used
               auto prefix = AddressEntry::getPrefixByte(addrType);
               auto insertIter = usedPrefixes.insert(prefix);
               if (!insertIter.second) {
                  continue;
               }
               prefixedKey.put_uint8_t(prefix);
            } catch (const AddressException&) {}

            prefixedKey.put_BinaryData(key);
            auto iter = hashMap.find(prefixedKey.getData());
            if (iter == hashMap.end()) {
               continue;
            }

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
            return { asset, iter->second.second };
         }
      }
      return { nullptr, AddressEntryType::Default };
   }
}

////////////////////////////////////////////////////////////////////////////////
// ResolverFeed_AssetWalletSingle
ResolverFeed_AssetWalletSingle::ResolverFeed_AssetWalletSingle(
   std::shared_ptr<Wallets::AssetWallet_Single> wltPtr) :
   wltPtr_(wltPtr)
{
   if (wltPtr_ == nullptr) {
      throw std::runtime_error("null wallet ptr");
   }
}

void ResolverFeed_AssetWalletSingle::addToMap(
   std::shared_ptr<AddressEntry> addrPtr)
{
   try {
      hashToPreimage_.emplace(addrPtr->getHash(), addrPtr->getPreimage());
   } catch (const std::exception&) {}

   auto addrNested = std::dynamic_pointer_cast<AddressEntry_Nested>(addrPtr);
   if (addrNested != nullptr) {
      addToMap(addrNested->getPredecessor());
      return;
   }

   auto addrWithAsset = std::dynamic_pointer_cast<AddressEntry_WithAsset>(addrPtr);
   if (addrWithAsset != nullptr) {
      auto assetSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(
         addrWithAsset->getAsset());
      if (assetSingle == nullptr) {
         throw Wallets::WalletException("multisig asset in asset_single resolver");
      }
      pubkeyToAsset_.emplace(addrPtr->getPreimage(), assetSingle);
   }
}

BinaryData ResolverFeed_AssetWalletSingle::getByVal(const BinaryData& key)
{
   //check cached hits first
   auto iter = hashToPreimage_.find(key);
   if (iter != hashToPreimage_.end()) {
      return iter->second;
   }

   //short of that, try to get the asset for this key
   auto assetPair = getAssetPairForKey(wltPtr_, key);
   if (assetPair.first == nullptr ||
      assetPair.second == AddressEntryType::Default) {
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

const SecureBinaryData& ResolverFeed_AssetWalletSingle::getPrivKeyForPubkey(
   const BinaryData& pubkey)
{
   //check cache first
   {
      auto cacheIter = pubkeyToAsset_.find(pubkey);
      if (cacheIter != pubkeyToAsset_.end()) {
         return wltPtr_->getDecryptedPrivateKeyForAsset(
            cacheIter->second);
      }
   }

   //if we have a bip32 path hint for this pubkey, use that
   {
      auto pathIter = bip32Paths_.find(pubkey);
      if (pathIter != bip32Paths_.end()) {
         if (!pathIter->second.second.isValid()) {
            pathIter->second.second = wltPtr_->derivePrivKeyFromPath(
               pathIter->second.first);
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
   auto assetPair = getAssetPairForKey(wltPtr_, hash);
   if (assetPair.first == nullptr) {
      throw NoAssetException("invalid pubkey");
   }

   auto assetSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(
      assetPair.first);
   if (assetSingle == nullptr) {
      throw std::logic_error("invalid asset type");
   }
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

BIP32_AssetPath ResolverFeed_AssetWalletSingle::resolveBip32PathForPubkey(
   const BinaryData& pubkey)
{
   //check cache first
   {
      auto cacheIter = pubkeyToAsset_.find(pubkey);
      if (cacheIter != pubkeyToAsset_.end()) {
         return wltPtr_->getBip32PathForAsset(cacheIter->second);
      }
   }

   auto hash = BtcUtils::getHash160(pubkey);
   auto assetPair = getAssetPairForKey(wltPtr_, hash);
   if (assetPair.first == nullptr) {
      throw NoAssetException("invalid pubkey");
   }
   return wltPtr_->getBip32PathForAsset(assetPair.first);
}

void ResolverFeed_AssetWalletSingle::seedFromAddressEntry(
   std::shared_ptr<AddressEntry> addrPtr)
{
   try {
      //add hash to preimage pair
      hashToPreimage_.emplace(addrPtr->getHash(), addrPtr->getPreimage());
   } catch (const AddressException&) {
      return;
   }

   //is this address nested?
   auto addrNested = std::dynamic_pointer_cast<AddressEntry_Nested>(addrPtr);
   if (addrNested == nullptr) {
      //return if not
      return;
   }

   //seed the predecessor too
   seedFromAddressEntry(addrNested->getPredecessor());
}

void ResolverFeed_AssetWalletSingle::setBip32PathForPubkey(
   const BinaryData& pubkey, const BIP32_AssetPath& path)
{
   bip32Paths_.emplace(pubkey, std::make_pair(path, Wallets::AssetId{}));
}

////////////////////////////////////////////////////////////////////////////////
// ResolverFeed_AssetWalletSingle_ForMultisig
ResolverFeed_AssetWalletSingle_ForMultisig::ResolverFeed_AssetWalletSingle_ForMultisig(
   std::shared_ptr<Wallets::AssetWallet_Single> wltPtr) :
   wltPtr_(wltPtr)
{
   auto accountIDs = wltPtr->getAccountIDs();
   for (auto& accID : accountIDs) {
      auto addrAcc = wltPtr->getAccountForID(accID);
      for (auto& assID : addrAcc->getAccountIdSet()) {
         auto assAcc = addrAcc->getAccountForID(assID);
         for (unsigned i = 0; i < assAcc->getAssetCount(); i++) {
            auto asset = assAcc->getAssetForKey(i);
            addToMap(asset);
         }
      }
   }
}

void ResolverFeed_AssetWalletSingle_ForMultisig::addToMap(
   std::shared_ptr<Assets::AssetEntry> asset)
{
   auto assetSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(
      asset);
   if (assetSingle == nullptr) {
      throw NoAssetException("multisig asset in AssetSingle resolver");
   }
   auto pubkey = assetSingle->getPubKey();
   BinaryDataRef pubkey_compressed(pubkey->getCompressedKey());
   BinaryDataRef pubkey_uncompressed(pubkey->getUncompressedKey());

   pubkeyToAsset_.emplace(pubkey_compressed, assetSingle);
   pubkeyToAsset_.emplace(pubkey_uncompressed, assetSingle);
}

BinaryData ResolverFeed_AssetWalletSingle_ForMultisig::getByVal(
   const BinaryData&)
{
   //find id for the key
   throw std::runtime_error("no preimages in multisig feed");
}

const SecureBinaryData& ResolverFeed_AssetWalletSingle_ForMultisig::
getPrivKeyForPubkey(const BinaryData& pubkey)
{
   auto iter = pubkeyToAsset_.find(pubkey);
   if (iter == pubkeyToAsset_.end()) {
      throw std::runtime_error("invalid value");
   }
   const auto& privkeyAsset = iter->second->getPrivKey();
   return wltPtr_->getDecryptedValue(privkeyAsset);
}

BIP32_AssetPath ResolverFeed_AssetWalletSingle_ForMultisig::
resolveBip32PathForPubkey(const BinaryData&)
{
   throw std::runtime_error("invalid pubkey");
}

void ResolverFeed_AssetWalletSingle_ForMultisig::
setBip32PathForPubkey(const BinaryData&, const BIP32_AssetPath&)
{}

////////////////////////////////////////////////////////////////////////////////
// ResolverFeed_AssetWalletSingle_Exotic
ResolverFeed_AssetWalletSingle_Exotic::
ResolverFeed_AssetWalletSingle_Exotic(
   std::shared_ptr<Wallets::AssetWallet_Single> wltPtr) :
   ResolverFeed_AssetWalletSingle(wltPtr)
{}

const SecureBinaryData& ResolverFeed_AssetWalletSingle_Exotic::
getPrivKeyForPubkey(const BinaryData& pubkey)
{
   try {
      return ResolverFeed_AssetWalletSingle::getPrivKeyForPubkey(pubkey);
   } catch (const NoAssetException&) {}

   throw std::runtime_error("not implemented yet");

   /*
   Failed to get the asset for the pukbey by hashing it, run through
   all assets linearly instead.
   */

   //grab account

   //grab asset account

   //run through assets, check pubkeys
}
