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
      struct PeerAssetMap;
   }

   namespace Wallets
   {
      class AssetWallet;

      namespace IO
      {
         struct ReadOnlyFileParams;
         struct CreateFileParams;
      };
   }

   /////////////////////////////////////////////////////////////////////////////
   namespace NetworkPeers
   {
      struct FileMissing
      {
         FileMissing(void)
         {}
      };

      ////////
      struct Exception : public std::runtime_error
      {
         Exception(const std::string& err) :
            std::runtime_error(err)
         {}
      };

      ////////
      enum class PeerType : int {
         Client = 0,
         ServerOneWay,
         ServerTwoWay
      };

      class PeerKey
      {
      private:
         const SecureBinaryData pubkey_;
         const PeerType type_;

      public:
         PeerKey(BinaryDataRef, PeerType);
         PeerKey(const btc_pubkey&, PeerType);

         const SecureBinaryData& getKey(void) const;
         bool isServer(void) const;
         bool isOneWay(void) const;

         std::string toHumanReadable(void) const;
         static PeerKey fromHumanReadable(const std::string&);
      };

      class PeerMap
      {
      private:
         std::map<std::string, SecureBinaryData> nameToKeyMap_;
         std::map<SecureBinaryData, std::string> keyMap_;
         std::map<SecureBinaryData, std::set<unsigned>> keyToAssetIndexMap_;

         const std::shared_ptr<Wallets::AssetWallet> wallet_;
         const bool oneWay_;

      public:
         PeerMap(std::shared_ptr<Wallets::AssetWallet>, const SecureBinaryData&, bool);

         void setupFromAssetMap(const Accounts::PeerAssetMap&);
         void grabKeyIndexes(std::shared_ptr<Accounts::MetaDataAccount>);

         const std::map<std::string, SecureBinaryData>& getPeerNameMap(void) const;
         const std::map<SecureBinaryData, std::string>& getPublicKeyMap(void) const;

         void addPeer(const SecureBinaryData&,
            const std::vector<std::string>&, const std::string&);
         const std::string& getLabel(const SecureBinaryData&) const;
         void setLabel(const SecureBinaryData&, const std::string&);

         SecureBinaryData eraseName(const std::string&);
         void eraseKey(const SecureBinaryData&);
      };

      ////////
      class PeerStoreView;

      class PeerStore
      {
      protected:
         //std::map<SecureBinaryData, SecureBinaryData> privateKeys_;
         std::shared_ptr<Wallets::AssetWallet> wallet_ = nullptr;
         std::shared_ptr<const SecureBinaryData> ephemeralPrivateKey_ = nullptr;

      protected:
         PeerStore(const Wallets::IO::ReadOnlyFileParams&);
         PeerStore(std::shared_ptr<Wallets::AssetWallet>);
         PeerStore(const SecureBinaryData&);
         PeerStore(void);
         virtual ~PeerStore(void) = 0;

         void loadWallet(const Wallets::IO::ReadOnlyFileParams&);
         void erasePeerRootKey(const SecureBinaryData&);

      public:
         virtual const SecureBinaryData& getOwnPublicKey(void) const = 0;

         virtual void addPeer(const PeerKey&,
            const std::vector<std::string>&, const std::string&) = 0;
         virtual void erasePeer(const PeerKey&) = 0;
         virtual void eraseName(const std::string&, PeerType) = 0;
         virtual void eraseKey(const btc_pubkey&, PeerType) = 0;
         virtual void eraseKey(BinaryDataRef, PeerType) = 0;
         virtual void setLabel(const PeerKey&, const std::string&) = 0;

         //takes path to peers db, passphrase lambdas are handled internally
         static void changeControlPassphrase(const std::filesystem::path&);
         static std::shared_ptr<Wallets::AssetWallet> bootstrapWallet(
            const Wallets::IO::CreateFileParams&);
      };

      ////
      class ServerStore : public PeerStore
      {
      private:
         std::shared_ptr<PeerMap> peerMap_;

         //public key of master ACL; a client that completes a 2-way
         //AEAD handshake with this key will receive master credentials
         SecureBinaryData masterKey_;

      private:
         void initPeerMap(void);
         void setupFromWallet(void);

      public:
         ServerStore(void);
         ServerStore(const SecureBinaryData&);
         ServerStore(std::shared_ptr<Wallets::AssetWallet>);
         ServerStore(const Wallets::IO::ReadOnlyFileParams&);

         std::unique_ptr<PeerStoreView> getView(void) const;
         bool setMasterKey(const PeerKey&);
         void eraseMasterKey(void);
         bool isMasterKey(BinaryDataRef) const;

         const SecureBinaryData& getOwnPublicKey(void) const override;
         void addPeer(const PeerKey&,
            const std::vector<std::string>&, const std::string&) override;
         void erasePeer(const PeerKey&) override;
         void eraseName(const std::string&, PeerType) override;
         void eraseKey(const btc_pubkey&, PeerType) override;
         void eraseKey(BinaryDataRef, PeerType) override;
         void setLabel(const PeerKey&, const std::string&) override;
      };

      ////
      class ClientStore : public PeerStore
      {
      private:
         std::shared_ptr<PeerMap> peerMapOneWay_;
         std::shared_ptr<PeerMap> peerMapTwoWay_;

      private:
         void initPeerMap(void);
         void setupFromWallet(void);

      public:
         ClientStore(void);
         ClientStore(const SecureBinaryData&);
         ClientStore(std::shared_ptr<Wallets::AssetWallet>);
         ClientStore(const Wallets::IO::ReadOnlyFileParams&);

         std::unique_ptr<PeerStoreView> getView(PeerType) const;
         std::shared_ptr<ClientStore> getNarrowSet(const PeerKey&) const;

         const SecureBinaryData& getOwnPublicKey(void) const override;
         void addPeer(const PeerKey&,
            const std::vector<std::string>&, const std::string&) override;
         void erasePeer(const PeerKey&) override;
         void eraseName(const std::string&, PeerType) override;
         void eraseKey(const btc_pubkey&, PeerType) override;
         void eraseKey(BinaryDataRef, PeerType) override;
         void setLabel(const PeerKey&, const std::string&) override;
      };

      ////////
      class PeerStoreView
      {
      private:
         const std::shared_ptr<const PeerMap> peerMap_;
         const std::shared_ptr<const Wallets::AssetWallet> wallet_;
         const std::shared_ptr<const SecureBinaryData> ephemeralPrivateKey_;

      public:
         PeerStoreView(std::shared_ptr<const PeerMap>,
            std::shared_ptr<const Wallets::AssetWallet>);
         PeerStoreView(std::shared_ptr<const PeerMap>,
            std::shared_ptr<const SecureBinaryData>);

         bool signChallenge(const uint8_t*, BinaryData&) const;
         BinaryDataRef getPubKeyRef(const std::string&) const;
         const std::string& getLabel(const SecureBinaryData&) const;

         const std::map<SecureBinaryData, std::string>& getPublicKeyMap(void) const;
         const std::map<std::string, SecureBinaryData>& getPeerNameMap(void) const;
      };
   } //namespace NetworkPeers
} //namespace Armory
