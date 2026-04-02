////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <string>
#include <set>
#include <map>
#include <filesystem>

#include <Utils/SecureBinaryData.h>
#include <btc/ecc_key.h>

#define PEERS_WALLET_PASSWORD "password"
#define PEERS_WALLET_BIP32_ACCOUNT 0xFF005618

struct AuthPeersLambdas;

namespace Armory
{
   namespace Accounts
   {
      class MetaDataAccount;
      struct AuthPeerAssetMap;
   }

   namespace Wallets
   {
      class AssetWallet;

      namespace IO
      {
         struct ReadOnlyFileParams;
         struct CreateFileParams;
      };

      //////////////////////////////////////////////////////////////////////////
      class PeerFileMissing
      {
      public:
         PeerFileMissing(void)
         {}
      };

      ////////
      class AuthorizedPeersException : public std::runtime_error
      {
      public:
         AuthorizedPeersException(const std::string& err) :
            std::runtime_error(err)
         {}
      };

      ////////
      class PeerKey
      {
      private:
         const SecureBinaryData pubkey_;
         const bool oneWayAuth_;
         const bool isServer_;

      public:
         PeerKey(BinaryDataRef, bool, bool);
         PeerKey(const btc_pubkey&, bool, bool);

         const SecureBinaryData& getKey(void) const;
         bool isServer(void) const;
         bool isOneWay(void) const;

         std::string toHumanReadable(void) const;
         static PeerKey fromHumanReadable(const std::string&);
      };

      class PeerMap
      {
      private:
         std::map<std::string, btc_pubkey> nameToKeyMap_;
         std::map<SecureBinaryData, std::string> keyMap_;
         std::map<SecureBinaryData, std::set<unsigned>> keyToAssetIndexMap_;

         const std::shared_ptr<AssetWallet> wallet_;
         const bool oneWay_;

      public:
         PeerMap(std::shared_ptr<AssetWallet>, const btc_pubkey&, bool);

         void setupFromAssetMap(const Accounts::AuthPeerAssetMap&);
         void grabKeyIndexes(std::shared_ptr<Accounts::MetaDataAccount>);

         const std::map<std::string, btc_pubkey>& getPeerNameMap(void) const;
         const std::map<SecureBinaryData, std::string>& getPublicKeyMap(void) const;

         void addPeer(const SecureBinaryData&,
            const std::vector<std::string>&, const std::string&);
         void setLabel(const SecureBinaryData&, const std::string&);

         btc_pubkey eraseName(const std::string&);
         void eraseKey(const SecureBinaryData&);
      };

      //////////////////////////////////////////////////////////////////////////
      class AuthorizedPeers
      {
      private:
         std::unique_ptr<PeerMap> peerMapOneWay_;
         std::unique_ptr<PeerMap> peerMapTwoWay_;
         std::map<BinaryData, SecureBinaryData> privateKeys_;

         //for wallet management
         std::shared_ptr<AssetWallet> wallet_ = nullptr;

         //public key of master ACL; a client that completes a 2-way
         //AEAD handshake with this key will receive master credentials
         SecureBinaryData masterKey_;

      private:
         AuthorizedPeers(std::shared_ptr<AssetWallet>);
         AuthorizedPeers(SecureBinaryData&);

         void initPeerMaps(const SecureBinaryData&);
         SecureBinaryData setOwnPrivateKey(SecureBinaryData&);
         void loadWallet(const IO::ReadOnlyFileParams&);
         void initFromWallet(void);
         void erasePeerRootKey(const SecureBinaryData&);

      public:
         AuthorizedPeers(const IO::ReadOnlyFileParams&);
         AuthorizedPeers(void);

         const std::map<std::string, btc_pubkey>& getPeerNameMap(bool) const;
         const std::map<SecureBinaryData, std::string>& getPublicKeyMap(bool) const;
         const SecureBinaryData& getPrivateKey(const BinaryDataRef&) const;
         std::shared_ptr<AuthorizedPeers> getNarrowSet(const PeerKey&) const;
         const btc_pubkey& getOwnPublicKey(void) const;
         const std::string& getLabel(const SecureBinaryData&, bool) const;

         /* addPeer:
         input:
         - pubkey as SecureBinaryData/btc_pubkey/PeerKey; secp256k1 un/compressed
           public key
         - vector<std::string> of names; at least 1
         */
         void addPeer(const PeerKey&,
            const std::vector<std::string>&, const std::string&);
         void addPeer(const btc_pubkey&,
            const std::vector<std::string>&, const std::string&, bool);
         void addPeer(const SecureBinaryData&,
            const std::vector<std::string>&, const std::string&, bool);

         //
         void eraseName(const std::string&, bool);
         void erasePeer(const PeerKey&);
         void eraseKey(const SecureBinaryData&, bool);
         void eraseKey(const btc_pubkey&, bool);

         bool setMasterKey(const btc_pubkey&);
         bool setMasterKey(const SecureBinaryData&);
         void eraseMasterKey(void);
         bool isMasterKey(const btc_pubkey&) const;
         bool isMasterKey(const SecureBinaryData&) const;
         void setLabel(const PeerKey&, const std::string&);
         void setLabel(const SecureBinaryData&, const std::string&, bool);

         //takes path to peers db, passphrase lambdas are handled internally
         static void changeControlPassphrase(const std::filesystem::path&);
         static AuthPeersLambdas getAuthPeersLambdas(
            std::shared_ptr<AuthorizedPeers>, bool);
         static std::shared_ptr<AuthorizedPeers> createWallet(
            const IO::CreateFileParams&);
      };
   } //namespace Wallets
} //namespace Armory
