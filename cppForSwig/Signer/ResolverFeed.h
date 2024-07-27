////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2021, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_RESOLVER_FEED
#define _H_RESOLVER_FEED

#include <vector>
#include <string>
#include <memory>

#include "../BinaryData.h"
#include "../SecureBinaryData.h"
#include "../Wallets/BIP32_Node.h"

////
class NoAssetException : public std::runtime_error
{
public:
   NoAssetException(const std::string& msg) : std::runtime_error(msg)
   {}
};

namespace Armory
{
   namespace Signing
   {
      //////////////////////////////////////////////////////////////////////////
      class BIP32_PublicDerivedRoot
      {
      private:
         std::string xpub_;

         //path from seed to xpub
         std::vector<uint32_t> path_;

         //seed's fingerprint
         uint32_t seedFingerprint_ = UINT32_MAX;
         mutable uint32_t thisFingerprint_ = UINT32_MAX;

      public:
         BIP32_PublicDerivedRoot(
            const std::string&, //xpub
            const std::vector<uint32_t>&, //path
            uint32_t); //fingerprint

         bool isValid(void) const;
         uint32_t getSeedFingerprint(void) const;
         uint32_t getThisFingerprint(void) const;
         const std::vector<uint32_t>& getPath(void) const;
         const std::string& getXPub(void) const;
      };

      //////////////////////////////////////////////////////////////////////////
      class BIP32_AssetPath
      {
      private:
         const BinaryData pubkey_;
         const std::vector<uint32_t> path_;
         const uint32_t fingerprint_;

         /*
         Empty root means the root is implicit, i.e. the wallet has to carry
         the root pointed at by rootFingerprint_ to be able to generate this
         asset.

         A set root means this object carries all the necessary data to
         generate the asset public key. The wallet needs the private root data
         to generate the asset private key.
         */
         std::shared_ptr<BIP32_PublicDerivedRoot> root_;

      public:
         BIP32_AssetPath(
            const BinaryData&, //pubkey
            const std::vector<uint32_t>&, //path
            uint32_t, //fingerprint
            std::shared_ptr<BIP32_PublicDerivedRoot>);

         bool operator==(const BIP32_AssetPath& rhs) const;
         bool operator!=(const BIP32_AssetPath& rhs) const;
         bool isValid(void) const;

         uint32_t getSeedFingerprint(void) const;
         uint32_t getThisFingerprint(void) const;
         std::vector<uint32_t> getDerivationPathFromSeed(void) const;
         const std::vector<uint32_t>& getPath(void) const;
         const BinaryData& getPublicKey(void) const;

         bool hasRoot(void) const;
         void setRoot(std::shared_ptr<BIP32_PublicDerivedRoot>);
         std::shared_ptr<BIP32_PublicDerivedRoot> getRoot(void) const;

         ////
         void toPSBT(BinaryWriter&) const;
         static BIP32_AssetPath fromPSBT(
            const BinaryDataRef&, const BinaryDataRef&);
      };

      //////////////////////////////////////////////////////////////////////////
      class ResolverFeed
      {
      public:
         virtual ~ResolverFeed(void) = 0;

         virtual BinaryData getByVal(const BinaryData&) = 0;
         virtual const SecureBinaryData& getPrivKeyForPubkey(
            const BinaryData&) = 0;

         virtual void setBip32PathForPubkey(
            const BinaryData&, const BIP32_AssetPath&) = 0;
         virtual BIP32_AssetPath resolveBip32PathForPubkey(
            const BinaryData&) = 0;
      };
   }; //namespace Signer
}; //namespace Armory
#endif