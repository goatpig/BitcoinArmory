////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_RESOLVER_FEED_WALLETS
#define _H_RESOLVER_FEED_WALLETS

#include <memory>
#include <map>

#include "../BinaryData.h"
#include "ResolverFeed.h"
#include "../Wallets/WalletIdTypes.h"
#include "../Wallets/Addresses.h"

////////////////////////////////////////////////////////////////////////////////
namespace Armory
{
   namespace Assets
   {
      class AssetEntry;
      class AssetEntry_Single;
   };

   namespace Wallets
   {
      class AssetWallet;
      class AssetWallet_Single;
   };

   namespace Signing
   {
      class ResolverFeed_AssetWalletSingle : public ResolverFeed
      {
      private:
         std::shared_ptr<Wallets::AssetWallet_Single> wltPtr_;

      protected:
         std::map<BinaryData, BinaryData> hash_to_preimage_;
         std::map<BinaryData,
            std::shared_ptr<Assets::AssetEntry_Single>> pubkey_to_asset_;
         std::map<BinaryData,
            std::pair<BIP32_AssetPath, Armory::Wallets::AssetId>> bip32Paths_;

      private:

         void addToMap(std::shared_ptr<AddressEntry>);

      public:
         //tors
         ResolverFeed_AssetWalletSingle(
            std::shared_ptr<Wallets::AssetWallet_Single> wltPtr) :
            wltPtr_(wltPtr)
         {
            if (wltPtr_ == nullptr)
               throw std::runtime_error("null wallet ptr");
         }

         //virtual
         BinaryData getByVal(const BinaryData&) override;
         virtual const SecureBinaryData& getPrivKeyForPubkey(
            const BinaryData&) override;
         BIP32_AssetPath resolveBip32PathForPubkey(
            const BinaryData&) override;

         //local
         void seedFromAddressEntry(std::shared_ptr<AddressEntry>);
         void setBip32PathForPubkey(const BinaryData& pubkey, 
            const BIP32_AssetPath& path) override;

         std::pair<std::shared_ptr<Assets::AssetEntry>, AddressEntryType> 
            getAssetPairForKey(const BinaryData&) const;
      };

      //////////////////////////////////////////////////////////////////////////
      class ResolverFeed_AssetWalletSingle_Exotic :
         public ResolverFeed_AssetWalletSingle
      {
         //tors
         ResolverFeed_AssetWalletSingle_Exotic(
            std::shared_ptr<Wallets::AssetWallet_Single> wltPtr) :
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

            throw std::runtime_error("not implemented yet");

            /*
            Failed to get the asset for the pukbey by hashing it, run through
            all assets linearly instead.
            */

            //grab account

            //grab asset account

            //run through assets, check pubkeys
         }
      };

      //////////////////////////////////////////////////////////////////////////
      class ResolverFeed_AssetWalletSingle_ForMultisig : public ResolverFeed
      {
      private:
         std::shared_ptr<Wallets::AssetWallet> wltPtr_;

      protected:
         std::map<BinaryDataRef,
            std::shared_ptr<Assets::AssetEntry_Single>> pubkey_to_asset_;

      private:

         void addToMap(std::shared_ptr<Assets::AssetEntry> asset);

      public:
         //tors
         ResolverFeed_AssetWalletSingle_ForMultisig(
            std::shared_ptr<Wallets::AssetWallet_Single>);

         //virtual
         BinaryData getByVal(const BinaryData&) override;
         virtual const SecureBinaryData& getPrivKeyForPubkey(
            const BinaryData& pubkey) override;
         BIP32_AssetPath resolveBip32PathForPubkey(const BinaryData&) override;
         void setBip32PathForPubkey(
            const BinaryData&, const BIP32_AssetPath&) override;
      };
   }; //namespace Signer
}; //namespace Armory

#endif