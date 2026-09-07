////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cstring>
#include <btc/ecc.h>

#include "AuthorizedPeers.h"
#include <Utils/FileUtils.h>
#include <Utils/BtcUtils.h>
#include <Utils/Cryptography.h>

#include "Accounts/AccountTypes.h"
#include "Accounts/AddressAccounts.h"
#include "Accounts/MetaAccounts.h"
#include "Wallets.h"
#include "WalletFileInterface.h"
#include "Seeds/Seeds.h"
#include "BIP32_Node.h"

using namespace Armory;
using namespace Armory::NetworkPeers;

using namespace std::chrono_literals;
using namespace std::string_view_literals;

#define ARMORY_PEERKEY_VERSION   0x01

namespace {
   std::shared_ptr<Assets::AssetEntry_Single> getPrimaryAssetFromWallet(
      std::shared_ptr<const Wallets::AssetWallet> wallet)
   {
      if (wallet == nullptr) {
         throw Armory::NetworkPeers::Exception("null peer wallet");
      }

      //grab primary asset: index #1 on main peers chain (m'/PEERS_WALLET_BIP32_ACCOUNT'/0')
      auto mainAcc = wallet->getAccountForID(wallet->getMainAccountID());
      auto outerAccount = mainAcc->getOuterAccount();
      auto assetPtr = outerAccount->getAssetForKey(1);
      auto assetSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(assetPtr);
      if (assetSingle == nullptr) {
         throw Armory::NetworkPeers::Exception(
            "failed to grab primary key for peer store");
      }
      return assetSingle;
   }

   const SecureBinaryData& getOwnPubkeyFromWallet(
      std::shared_ptr<Wallets::AssetWallet> wallet)
   {
      auto assetSingle = getPrimaryAssetFromWallet(wallet);
      auto assetPubKey = assetSingle->getPubKey();
      if (assetPubKey == nullptr || !assetSingle->hasPrivateKey()) {
         //primary asset should have both public and private key
         throw Exception("invalid primary key asset");
      }

      //return the compressed public key
      return assetPubKey->getCompressedKey();
   }
}

////////////////////////////////////////////////////////////////////////////////
// PeerStore
PeerStore::PeerStore()
{
   //No filename was passed, create an ephemral peer db instead
   ephemeralPrivateKey_ = std::make_shared<const SecureBinaryData>(
      Cryptography::PRNG::generateRandomStrong(32));
}

PeerStore::PeerStore(const SecureBinaryData& privateKey)
{
   ephemeralPrivateKey_ = std::make_shared<const SecureBinaryData>(privateKey);
}

PeerStore::PeerStore(std::shared_ptr<Wallets::AssetWallet> wltPtr) :
   wallet_(wltPtr)
{}

PeerStore::PeerStore(const Wallets::IO::ReadOnlyFileParams& params)
{
   loadWallet(params);
}

PeerStore::~PeerStore()
{}

////////
void PeerStore::loadWallet(const Wallets::IO::ReadOnlyFileParams& params)
{
   if (!FileUtils::pathExists(params.filePath, 6)) {
      throw FileMissing();
   }
   wallet_ = Wallets::AssetWallet::loadMainWalletFromFile(params);
}

////////
std::shared_ptr<Wallets::AssetWallet> PeerStore::bootstrapWallet(
   const Wallets::IO::CreateFileParams& params)
{
   //Default peers wallet private keys password. Asset wallets always
   //encrypt private keys, we have to provide a password at creation.
   auto privPass = SecureBinaryData::fromString(PEERS_WALLET_PASSWORD);

   //wrap around the provided CreateFileParams to create the AssetWallet
   //the peers db will run off of
   Wallets::IO::CreateWalletParams walletParams{
      params.filePath.parent_path(),
      Passphrase::SetNew{1ms, 0, privPass},
      params.setCtrlPassObj.copy(), nullptr,
      0, {}, {}
   };

   std::shared_ptr<Wallets::AssetWallet_Single> wallet;
   {
      //Default peers wallet derivation path. Using m/'account/'0.
      std::vector<uint32_t> derPath;
      derPath.push_back(PEERS_WALLET_BIP32_ACCOUNT);
      derPath.push_back(0xF0000000);

      //generate bip32 node from random seed
      wallet = Wallets::AssetWallet_Single::createFromSeed(
         std::make_unique<Seeds::ClearTextSeed_BIP32>(
            Cryptography::PRNG::generateRandomStrong(32),
            Seeds::SeedType::BIP32_Virgin),
         walletParams);
      auto wltSingle = std::dynamic_pointer_cast<Wallets::AssetWallet_Single>(wallet);

      auto rootBip32 = std::dynamic_pointer_cast<Assets::AssetEntry_BIP32Root>(
         wltSingle->getRoot());
      if (rootBip32 == nullptr) {
         throw Exception("[bootstrapWallet] invalid root");
      }
      auto account = Accounts::AccountType_BIP32::makeFromDerPaths(
         rootBip32->getSeedFingerprint(false), {derPath});
      account->setMain(true);
      account->setAddressLookup(2);

      auto lock = wallet->lockDecryptedContainer(
         walletParams.setPrivPassObj.getUnlockFunc());
      wltSingle->createBIP32Account(account, nullptr);
   }

   //add the peers meta account
   wallet->addMetaAccount(Accounts::MetaAccountType::Peers);

   //grab wallet filename
   auto currentname = wallet->getDbFilename();

   //destroying the wallet object will shutdown the underlying db object
   wallet.reset();

   //rename peers wallet to desired name
   try {
      std::filesystem::rename(currentname, params.filePath);
   } catch (const std::filesystem::filesystem_error&) {
      throw Exception("failed to setup peers wallet");
   }
   currentname.append("-lock");
   std::filesystem::remove(currentname);

   Wallets::IO::ReadOnlyFileParams roFileParams{params.filePath,
      walletParams.setCtrlPassObj.getUnlockFunc()};
   return Wallets::AssetWallet::loadMainWalletFromFile(roFileParams);
}

////////////////////////////////////////////////////////////////////////////////
// ClientStore
ClientStore::ClientStore() :
   PeerStore()
{
   initPeerMap();
}

ClientStore::ClientStore(const SecureBinaryData& privateKey) :
   PeerStore(privateKey)
{
   initPeerMap();
}

ClientStore::ClientStore(const Wallets::IO::ReadOnlyFileParams& roFileParams) :
   PeerStore(roFileParams)
{
   initPeerMap();
   setupFromWallet();
}

ClientStore::ClientStore(std::shared_ptr<Wallets::AssetWallet> wlt) :
   PeerStore(wlt)
{
   initPeerMap();
   setupFromWallet();
}

////////
void ClientStore::initPeerMap()
{
   SecureBinaryData ownPubKey;
   if (wallet_ != nullptr) {
      ownPubKey = getOwnPubkeyFromWallet(wallet_);
   } else {
      if (ephemeralPrivateKey_ == nullptr) {
         throw Exception("no wallet nor private key set for store");
      }
      ownPubKey = Cryptography::ECDSA::computePublicKey(
         *ephemeralPrivateKey_, true);
   }
   peerMapOneWay_ = std::make_shared<PeerMap>(wallet_, ownPubKey, true);
   peerMapTwoWay_ = std::make_shared<PeerMap>(wallet_, ownPubKey, false);
}

////////
void ClientStore::setupFromWallet()
{
   if (wallet_ == nullptr) {
      return;
   }

   //grab all meta entries, populate public key map
   auto peerAccount = wallet_->getMetaAccount(Accounts::MetaAccountType::Peers);
   auto peerAssets = Accounts::PeerAccountHelper::getAssetMap(peerAccount.get());

   peerMapOneWay_->setupFromAssetMap(peerAssets);
   peerMapTwoWay_->setupFromAssetMap(peerAssets);
   peerMapOneWay_->grabKeyIndexes(peerAccount);
   peerMapTwoWay_->grabKeyIndexes(peerAccount);
}

////////
const SecureBinaryData& ClientStore::getOwnPublicKey() const
{
   const auto& nameMap = peerMapOneWay_->getPeerNameMap();
   auto iter = nameMap.find("own");
   if (iter == nameMap.end()) {
      throw Exception("malformed authpeer object");
   }
   return iter->second;
}

////////
void ClientStore::addPeer(const PeerKey& peerKey,
   const std::vector<std::string>& names, const std::string& label)
{
   if (!peerKey.isServer()) {
      throw Exception("client store only takes server keys");
   }
   if (peerKey.isOneWay()) {
      peerMapOneWay_->addPeer(peerKey.getKey(), names, label);
   } else {
      peerMapTwoWay_->addPeer(peerKey.getKey(), names, label);
   }
}

void ClientStore::erasePeer(const PeerKey& peerKey)
{
   //sanity check
   if (!peerKey.isServer()) {
      throw Exception("client store only carries server keys");
   }
   const auto& pubkey = peerKey.getKey();

   if (peerKey.isOneWay()) {
      peerMapOneWay_->eraseKey(pubkey);
   } else {
      peerMapTwoWay_->eraseKey(pubkey);
   }
   if (wallet_ == nullptr) {
      return;
   }

   //update on disk
   auto metaAccount = wallet_->getMetaAccount(Accounts::MetaAccountType::Peers);
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   std::shared_ptr<Wallets::IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   metaAccount->updateOnDisk(sharedTx);
}

////////
void ClientStore::eraseName(const std::string& name, PeerType peerType)
{
   std::shared_ptr<PeerMap> peerMapPtr;
   switch (peerType)
   {
      case PeerType::ServerOneWay:
         peerMapPtr = peerMapOneWay_;
         break;

      case PeerType::ServerTwoWay:
         peerMapPtr = peerMapTwoWay_;
         break;

      default:
         throw Exception("invalid peer type for client store");
   }
   peerMapPtr->eraseName(name);
   if (wallet_ == nullptr) {
      return;
   }

   auto metaAccount = wallet_->getMetaAccount(Accounts::MetaAccountType::Peers);
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   std::shared_ptr<Wallets::IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   metaAccount->updateOnDisk(sharedTx);
}

void ClientStore::eraseKey(const btc_pubkey& pubkey, PeerType peerType)
{
   BinaryDataRef keyBdr{pubkey.pubkey, pubkey.compressed ? 33u : 65u};
   eraseKey(keyBdr, peerType);
}

void ClientStore::eraseKey(BinaryDataRef key, PeerType peerType)
{
   switch (peerType)
   {
      case PeerType::ServerOneWay:
         peerMapOneWay_->eraseKey(key);
         break;

      case PeerType::ServerTwoWay:
         peerMapTwoWay_->eraseKey(key);
         break;

      default:
         throw Exception("invalid peer type for client store");
   }

   if (wallet_ == nullptr) {
      return;
   }

   //update on disk
   auto metaAccount = wallet_->getMetaAccount(Accounts::MetaAccountType::Peers);
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   std::shared_ptr<Wallets::IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   metaAccount->updateOnDisk(sharedTx);
}

////////
std::shared_ptr<ClientStore> ClientStore::getNarrowSet(
   const PeerKey& peerObj) const
{
   /*
   return a walletless auth peer db with its own private key
   and only the one peer
   */
   const auto* peerMap = peerObj.isOneWay() ?
      peerMapOneWay_.get() : peerMapTwoWay_.get();
   const auto& keyMap = peerMap->getPublicKeyMap();
   if (keyMap.find(peerObj.getKey()) == keyMap.end()) {
      throw Exception("unknown peer");
   }

   //get names for this key
   std::vector<std::string> peerNames;
   for (const auto& namePair : peerMap->getPeerNameMap()) {
      if (namePair.second == peerObj.getKey()) {
         peerNames.emplace_back(namePair.first);
      }
   }
   if (peerNames.empty()) {
      throw Exception("no name for this key");
   }

   std::shared_ptr<ClientStore> narrowStore;
   if (wallet_) {
      //grab primary asset
      auto primaryAsset = getPrimaryAssetFromWallet(wallet_);

      //grab clear text priv key from asset
      auto lock = wallet_->lockDecryptedContainer(
         [](const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result
         { return {SecureBinaryData::fromString(PEERS_WALLET_PASSWORD), true}; }
      );
      const auto& privateKey = wallet_->getDecryptedValue(
         primaryAsset->getPrivKey());

      //seed adhoc narrow client store with the privkey
      narrowStore = std::make_shared<ClientStore>(privateKey);
   } else {
      if (ephemeralPrivateKey_ == nullptr) {
         throw Exception("no private key for this peer store!");
      }
      narrowStore = std::make_shared<ClientStore>(*ephemeralPrivateKey_);
   }

   narrowStore->addPeer(peerObj, peerNames, {});
   return narrowStore;
}

////////
void ClientStore::setLabel(const PeerKey& key, const std::string& label)
{
   if (key.isOneWay()) {
      peerMapOneWay_->setLabel(key.getKey(), label);
   } else {
      peerMapTwoWay_->setLabel(key.getKey(), label);
   }
}

std::unique_ptr<PeerStoreView> ClientStore::getView(PeerType peerType) const
{
   switch (peerType)
   {
      case PeerType::ServerOneWay:
      {
         if (wallet_ == nullptr) {
            return std::make_unique<PeerStoreView>(
               peerMapOneWay_, ephemeralPrivateKey_);
         } else {
            return std::make_unique<PeerStoreView>(
               peerMapOneWay_, wallet_);
         }
      }

      case PeerType::ServerTwoWay:
      {
         if (wallet_ == nullptr) {
            return std::make_unique<PeerStoreView>(
               peerMapTwoWay_, ephemeralPrivateKey_);
         } else {
            return std::make_unique<PeerStoreView>(
               peerMapTwoWay_, wallet_);
         }
      }

      default:
         throw Exception("invalid peer type for client store");
   }
}

////////////////////////////////////////////////////////////////////////////////
// ServerStore
ServerStore::ServerStore() :
   PeerStore()
{
   initPeerMap();
}

ServerStore::ServerStore(const SecureBinaryData& privKey) :
   PeerStore(privKey)
{
   initPeerMap();
}

ServerStore::ServerStore(std::shared_ptr<Wallets::AssetWallet> wlt) :
   PeerStore(wlt)
{
   initPeerMap();
   setupFromWallet();
}

ServerStore::ServerStore(const Wallets::IO::ReadOnlyFileParams& roFileParams) :
   PeerStore(roFileParams)
{
   initPeerMap();
   setupFromWallet();
}

////////
void ServerStore::initPeerMap()
{
   SecureBinaryData ownPubKey;
   if (wallet_ != nullptr) {
      ownPubKey = getOwnPubkeyFromWallet(wallet_);
   } else {
      if (ephemeralPrivateKey_ == nullptr) {
         throw Exception("no wallet nor private key set for store");
      }
      ownPubKey = Cryptography::ECDSA::computePublicKey(
         *ephemeralPrivateKey_, true);
   }
   peerMap_ = std::make_shared<PeerMap>(wallet_, ownPubKey, true);
}

void ServerStore::setupFromWallet()
{
   if (wallet_ == nullptr) {
      return;
   }

   //grab all meta entries, populate public key map
   auto peerAccount = wallet_->getMetaAccount(Accounts::MetaAccountType::Peers);
   auto peerAssets = Accounts::PeerAccountHelper::getAssetMap(peerAccount.get());

   peerMap_->setupFromAssetMap(peerAssets);
   if (!peerAssets.masterKey.empty()) {
      masterKey_ = std::move(peerAssets.masterKey);
   }
   peerMap_->grabKeyIndexes(peerAccount);
}

////////
const SecureBinaryData& ServerStore::getOwnPublicKey() const
{
   const auto& nameMap = peerMap_->getPeerNameMap();
   auto iter = nameMap.find("own");
   if (iter == nameMap.end()) {
      throw Exception("malformed authpeer object");
   }
   return iter->second;
}

////////
void ServerStore::addPeer(const PeerKey& peerKey,
   const std::vector<std::string>& names, const std::string& label)
{
   if (peerKey.isServer()) {
      throw Exception("server store only takes client keys");
   }
   peerMap_->addPeer(peerKey.getKey(), names, label);
}

void ServerStore::erasePeer(const PeerKey& peerKey)
{
   //sanity check
   if (peerKey.isServer()) {
      throw Exception("server store only carries client keys");
   }
   const auto& pubkey = peerKey.getKey();

   //is this the master key?
   bool cleanupMasterKey = false;
   if (pubkey == masterKey_) {
      masterKey_.clear();
      cleanupMasterKey = true;
   }

   peerMap_->eraseKey(pubkey);
   if (wallet_ == nullptr) {
      return;
   }

   //update on disk
   auto metaAccount = wallet_->getMetaAccount(Accounts::MetaAccountType::Peers);
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   std::shared_ptr<Wallets::IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   if (cleanupMasterKey) {
      Accounts::PeerAccountHelper::clearMasterKeyAssets(metaAccount.get());
   }
   metaAccount->updateOnDisk(sharedTx);
}

////////
void ServerStore::eraseName(const std::string& name, PeerType peerType)
{
   if (peerType != PeerType::Client) {
      throw Exception("invalid peer type for server store");
   }

   auto key = peerMap_->eraseName(name);
   bool cleanupMasterKey = false;
   if (!masterKey_.empty() && key == masterKey_) {
      masterKey_.clear();
      cleanupMasterKey = true;
   }

   if (wallet_ == nullptr) {
      return;
   }

   auto metaAccount = wallet_->getMetaAccount(Accounts::MetaAccountType::Peers);
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   std::shared_ptr<Wallets::IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   if (cleanupMasterKey) {
      Accounts::PeerAccountHelper::clearMasterKeyAssets(metaAccount.get());
   }
   metaAccount->updateOnDisk(sharedTx);
}

////////
void ServerStore::eraseKey(const btc_pubkey& pubkey, PeerType peerType)
{
   BinaryDataRef keyBdr{pubkey.pubkey, pubkey.compressed ? 33u : 65u};
   eraseKey(keyBdr, peerType);
}

void ServerStore::eraseKey(BinaryDataRef key, PeerType peerType)
{
   if (peerType != PeerType::Client) {
      throw Exception("invalid peer type for server store");
   }

   bool cleanupMasterKey = false;
   if (masterKey_ == key) {
      masterKey_.clear();
      cleanupMasterKey = true;
   }
   peerMap_->eraseKey(key);

   if (wallet_ == nullptr) {
      return;
   }

   //update on disk
   auto metaAccount = wallet_->getMetaAccount(Accounts::MetaAccountType::Peers);
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   std::shared_ptr<Wallets::IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   if (cleanupMasterKey) {
      Accounts::PeerAccountHelper::clearMasterKeyAssets(metaAccount.get());
   }
   metaAccount->updateOnDisk(sharedTx);
}

////////
bool ServerStore::setMasterKey(const PeerKey& mKey)
{
   /* blindly add the master key, the youngest asset takes precedence */

   //sanity checks
   if (mKey.isServer()) {
      //only client keys can be masters
      return false;
   }

   const auto& pubkey = mKey.getKey();
   if (masterKey_ == pubkey) {
      return true;
   }

   const auto& keyMap = peerMap_->getPublicKeyMap();
   if (keyMap.find(pubkey) == keyMap.end()) {
      //master key isn't known to peers store, ignore
      return false;
   }

   //set in wallet
   if (wallet_ != nullptr) {
      auto metaAccount = wallet_->getMetaAccount(Accounts::MetaAccountType::Peers);
      auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
         wallet_->getDbName());
      std::shared_ptr<Wallets::IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
      Accounts::PeerAccountHelper::addMasterKey(metaAccount.get(), pubkey, sharedTx);
   }

   //set locally
   masterKey_ = pubkey;
   return true;
}

void ServerStore::eraseMasterKey()
{
   if (masterKey_.empty()) {
      return;
   }
   masterKey_.clear();

   if (wallet_ == nullptr) {
      return;
   }

   auto metaAccount = wallet_->getMetaAccount(Accounts::MetaAccountType::Peers);
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   std::shared_ptr<Wallets::IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   Accounts::PeerAccountHelper::clearMasterKeyAssets(metaAccount.get());
   metaAccount->updateOnDisk(sharedTx);
}

bool ServerStore::isMasterKey(BinaryDataRef pubkey) const
{
   if (masterKey_.empty()) {
      return false;
   }
   return masterKey_ == pubkey;
}

////////
void ServerStore::setLabel(const PeerKey& key, const std::string& label)
{
   peerMap_->setLabel(key.getKey(), label);
}

////////
std::unique_ptr<PeerStoreView> ServerStore::getView() const
{
   if (wallet_) {
      return std::make_unique<PeerStoreView>(peerMap_, wallet_);
   } else {
      return std::make_unique<PeerStoreView>(peerMap_, ephemeralPrivateKey_);
   }
}

////////////////////////////////////////////////////////////////////////////////
// PeerKey
PeerKey::PeerKey(BinaryDataRef pubkey, PeerType type) :
   pubkey_(pubkey), type_{type}
{
   if (pubkey.getSize() != 33 ||
      !Cryptography::ECDSA::verifyPublicKeyValid(pubkey)) {
      //not a valid pubkey
      throw Exception("invalid peer key");
   }
}

PeerKey::PeerKey(const btc_pubkey& pubkey, PeerType type) :
   pubkey_{pubkey.pubkey, pubkey.compressed ? 33u : 65u}, type_{type}
{
   if (!Cryptography::ECDSA::verifyPublicKeyValid(pubkey)) {
      //not a valid pubkey
      throw Exception("invalid peer key");
   }
}

const SecureBinaryData& PeerKey::getKey() const
{
   return pubkey_;
}

bool PeerKey::isServer() const
{
   switch (type_)
   {
      case PeerType::ServerOneWay:
      case PeerType::ServerTwoWay:
         return true;

      default:
         return false;
   }
}

bool PeerKey::isOneWay() const
{
   switch (type_)
   {
      case PeerType::ServerOneWay:
         return true;

      default:
         return false;
   }
}

////////
std::string PeerKey::toHumanReadable() const
{
   BinaryWriter bw;
   bw.reserve(36);

   //reserve space for header
   bw.put_uint16_t(0);
   bw.put_uint8_t(0);

   //put in key body
   bw.put_BinaryDataRef(pubkey_.getRef());

   //grab header ptr
   uint32_t* ptr = (uint32_t*)bw.getDataRef().getPtr();

   /* header section */

   switch (type_)
   {
      case PeerType::ServerOneWay:
         //AR1 v1
         *ptr |= 0x00411D01;
         break;

      case PeerType::ServerTwoWay:
         //AR2 v1
         *ptr |= 0x00811D01;
         break;

      case PeerType::Client:
         //ARc v1
         *ptr |= 0x00011701;
         break;

      default:
         throw std::runtime_error("unhandled peer type");
   }

   auto raw = bw.getDataRef();
   std::string_view rawView{raw.toCharPtr(), raw.getSize()};
   return BtcUtils::base64_encode(rawView);
}

PeerKey PeerKey::fromHumanReadable(const std::string& str)
{
   //sanity check
   if (str.size() < 40) {
      throw std::runtime_error("invalid peer key size");
   }

   //decode b64
   auto raw = BtcUtils::base64_decode(str);
   BinaryRefReader brr((const uint8_t*)raw.c_str(), raw.size());

   if (brr.getSizeRemaining() != 36) {
      throw std::runtime_error("invalid peer key size");
   }


   auto header = brr.get_BinaryDataRef(3);
   uint32_t headerInt =
      uint32_t(header[0]) << 24 |
      uint32_t(header[1]) << 16 |
      uint32_t(header[2]) << 8;

   auto getVal = [&headerInt]()->uint32_t
   {
      uint32_t result = (headerInt & 0xFC000000) >> 26;
      headerInt <<= 6;
      return result;
   };

   uint32_t val[4];
   for (unsigned i=0; i<4; i++) {
      val[i] = getVal();
   }

   if (val[0] != 0 || val[1] != 17) {
      //doesnt start with AR
      throw std::runtime_error("this isn't a peer key");
   }

   PeerType type;
   switch (val[2]) {
      case 28:
         type = PeerType::Client;
         break;

      case 53:
         type = PeerType::ServerOneWay;
         break;

      case 54:
         type = PeerType::ServerTwoWay;
         break;

      default:
         throw std::runtime_error("mangled peer key");
   }

   auto version = val[3];
   switch (version) {
      case 1:
      {
         auto pubkey = brr.get_BinaryDataRef(33);
         return PeerKey{pubkey, type};
      }

      default:
         throw std::runtime_error("unexpected peer key version");
   }
}

////////////////////////////////////////////////////////////////////////////////
// PeerMap
PeerMap::PeerMap(std::shared_ptr<Wallets::AssetWallet> wallet,
   const SecureBinaryData& ownKey, bool oneWay) :
   wallet_(wallet), oneWay_(oneWay)
{
   nameToKeyMap_.emplace("own", ownKey);
}

void PeerMap::setupFromAssetMap(const Accounts::PeerAssetMap& peerAssets)
{
   const auto& nameMap = oneWay_ ?
      peerAssets.nameKeyMapOneWay : peerAssets.nameKeyMapTwoWay;
   for (auto& pubkey : nameMap) {
      SecureBinaryData pubkey_cmp;
      if (pubkey.second.first->getSize() != 33) {
         pubkey_cmp = Cryptography::ECDSA::compressPoint(*pubkey.second.first);
      } else {
         pubkey_cmp = *pubkey.second.first;
      }

      keyMap_.emplace(pubkey_cmp, *pubkey.second.second);
      nameToKeyMap_.emplace(pubkey.first, pubkey_cmp);
   }
}

void PeerMap::grabKeyIndexes(
   std::shared_ptr<Accounts::MetaDataAccount> peerAccount)
{
   //grab public key to index map
   keyToAssetIndexMap_ = Accounts::PeerAccountHelper::getKeyIndexMap(
      peerAccount.get(), oneWay_);
}

////////
void PeerMap::addPeer(const SecureBinaryData& pubkey,
   const std::vector<std::string>& names, const std::string& label)
{
   //sanity check
   if (!Cryptography::ECDSA::verifyPublicKeyValid(pubkey)) {
      throw Exception("peer lacks a valid public key");
   }
   for (const auto& name : names) {
      if (name == "own") {
         throw Exception("use of a reserved name");
      }
   }

   //convert sbd pubkey to libbtc pubkey
   SecureBinaryData pubkey_cmp;
   if (pubkey.getSize() == 65) {
      pubkey_cmp = Cryptography::ECDSA::compressPoint(pubkey);
   } else if (pubkey.getSize() == 33) {
      pubkey_cmp = pubkey;
   } else {
      throw Exception("unexpected public key size");
   }

   //add all names to key list; using emplace means existing names are
   //not overwritten
   for (auto& name : names) {
      nameToKeyMap_.emplace(name, pubkey_cmp);
   }
   keyMap_.emplace(pubkey_cmp, label);

   //if we dont have a wallet attached, we're done
   if (wallet_ == nullptr) {
      return;
   }

   //get a dbtx for the wallet & add the pubkey with its names
   auto peerAccount = wallet_->getMetaAccount(Accounts::MetaAccountType::Peers);
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   std::shared_ptr<Wallets::IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   auto index = Accounts::PeerAccountHelper::addAsset(
      peerAccount.get(), pubkey_cmp, names, label, oneWay_, sharedTx);

   //track the asset index for the pubkey
   auto iter = keyToAssetIndexMap_.find(pubkey_cmp);
   if (iter == keyToAssetIndexMap_.end()) {
      iter = keyToAssetIndexMap_.emplace(
         pubkey_cmp, std::set<unsigned>{}).first;
   }
   iter->second.insert(index);
}

////////
SecureBinaryData PeerMap::eraseName(const std::string& name)
{
   if (name == "own") {
      throw Exception("invalid name");
   }

   //find pubkey
   auto keyIter = nameToKeyMap_.find(name);
   if (keyIter == nameToKeyMap_.end()) {
      return {};
   }

   //convert libbtc key to binarydataref
   auto pubkey = std::move(keyIter->second);

   //erase name from map
   nameToKeyMap_.erase(keyIter);

   if (wallet_ == nullptr) {
      /*
      We need to know if the name to erase is the last one refering to its
      relevant pubkey. If so, we need to delete the pubkey from the keySet
      as well, as it doesn't represent an valid peer anymore.

      In the absence of a wallet, we can't rely on it to sort public keys
      by name. Instead, parse nameToKeyMap linearly for other instances of
      the key
      */

      bool hasKey = false;
      for (auto& namePair : nameToKeyMap_) {
         if (namePair.second == pubkey) {
            hasKey = true;
            break;
         }
      }

      if (!hasKey) {
         //erase from key set
         keyMap_.erase(pubkey);
      }
      return pubkey;
   }

   //get the list of wallet assets this pub key appears in
   auto indexIter = keyToAssetIndexMap_.find(pubkey);
   if (indexIter == keyToAssetIndexMap_.end()) {
      return {};
   }

   //grab metadata account from wallet, cycle through assets, clean up
   //indexMap as we go
   auto metaAccount = wallet_->getMetaAccount(Accounts::MetaAccountType::Peers);
   auto setIter = indexIter->second.begin();
   while (setIter != indexIter->second.end()) {
      const auto& index = *setIter;
      std::shared_ptr<Assets::MetaData> metaPtr;
      try {
         metaPtr = metaAccount->getMetaDataByIndex(index);
      } catch (const std::exception&) {
         indexIter->second.erase(setIter++);
         continue;
      }

      auto peerPtr = std::dynamic_pointer_cast<Assets::PeerPublicData>(metaPtr);
      if (peerPtr == nullptr) {
         indexIter->second.erase(setIter++);
         continue;
      }

      if (peerPtr->eraseName(name)) {
         if (peerPtr->getNames().empty()) {
            indexIter->second.erase(setIter++);
            continue;
         }
      }
      ++setIter;
   }

   //remove public key from index map if it isn't related to any assets
   if (indexIter->second.empty()) {
      keyMap_.erase(indexIter->first);
      keyToAssetIndexMap_.erase(indexIter);
      return pubkey;
   }

   return {};
}

void PeerMap::eraseKey(const SecureBinaryData& pubkey)
{
   //erase from public key set
   if (keyMap_.erase(pubkey) == 0) {
      return;
   }

   if (wallet_ == nullptr) {
      //lacking a wallet to build a set of names for this pubkey, scoure the
      //name-key map linearly, clear it and we're done
      auto keyIter = nameToKeyMap_.begin();
      while (keyIter != nameToKeyMap_.end()) {
         if (keyIter->second == pubkey) {
            nameToKeyMap_.erase(keyIter++);
            continue;
         }
         ++keyIter;
      }
      return;
   }

   //we have a wallet, need to clear entries on disk and compile name list for
   //the public key
   auto iter = keyToAssetIndexMap_.find(pubkey);
   if (iter == keyToAssetIndexMap_.end()) {
      return;
   }
   auto metaAccount = wallet_->getMetaAccount(Accounts::MetaAccountType::Peers);
   std::set<std::string> namesToDelete;

   for (auto& index : iter->second) {
      std::shared_ptr<Assets::MetaData> metaPtr;
      try {
         metaPtr = metaAccount->getMetaDataByIndex(index);
      } catch (const std::exception&) {
         continue;
      }

      auto peerPtr = std::dynamic_pointer_cast<Assets::PeerPublicData>(metaPtr);
      if (peerPtr == nullptr) {
         continue;
      }
      auto& assetNames = peerPtr->getNames();
      namesToDelete.insert(assetNames.begin(), assetNames.end());
      metaAccount->eraseMetaDataByIndex(index);
   }

   //erase from index map
   keyToAssetIndexMap_.erase(iter);

   //erase names
   for (const auto& name : namesToDelete) {
      nameToKeyMap_.erase(name);
   }
}

////////
const std::string& PeerMap::getLabel(const SecureBinaryData& key) const
{
   auto iter = keyMap_.find(key);
   if (iter == keyMap_.end()) {
      throw Exception("unknown peer key");
   }
   return iter->second;
}

void PeerMap::setLabel(const SecureBinaryData& key, const std::string& label)
{
   auto iter = keyMap_.find(key);
   if (iter == keyMap_.end()) {
      throw Exception("unknown peer key");
   }
   iter->second = label;

   //if we dont have a wallet attached, we're done
   if (wallet_ == nullptr) {
      return;
   }

   //grab metadata account from wallet, cycle through assets, clean up
   //indexMap as we go
   auto metaAccount = wallet_->getMetaAccount(Accounts::MetaAccountType::Peers);

   //run through the set of wallet assets this pub key appears in
   auto indexIter = keyToAssetIndexMap_.find(key);
   auto setIter = indexIter->second.begin();
   while (setIter != indexIter->second.end()) {
      const auto& index = *setIter;
      std::shared_ptr<Assets::MetaData> metaPtr;
      try {
         metaPtr = metaAccount->getMetaDataByIndex(index);
      } catch (const std::exception&) {
         continue;
      }

      auto peerPtr = std::dynamic_pointer_cast<Assets::PeerPublicData>(metaPtr);
      if (peerPtr == nullptr) {
         continue;
      }

      peerPtr->setLabel(label);
      ++setIter;
   }

   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   std::shared_ptr<Wallets::IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   metaAccount->updateOnDisk(sharedTx);
}

///////
const std::map<std::string, SecureBinaryData>& PeerMap::getPeerNameMap() const
{
   return nameToKeyMap_;
}

const std::map<SecureBinaryData, std::string>& PeerMap::getPublicKeyMap() const
{
   return keyMap_;
}

////////////////////////////////////////////////////////////////////////////////
// PeerStoreView
PeerStoreView::PeerStoreView(
   std::shared_ptr<const PeerMap> peerMap,
   std::shared_ptr<const Wallets::AssetWallet> wallet) :
   peerMap_{peerMap}, wallet_{wallet}, ephemeralPrivateKey_{nullptr}
{
   if (wallet_ == nullptr) {
      throw Exception("no wallet for store view");
   }
}

PeerStoreView::PeerStoreView(
   std::shared_ptr<const PeerMap> peerMap,
   std::shared_ptr<const SecureBinaryData> privKey) :
   peerMap_{peerMap}, wallet_{nullptr}, ephemeralPrivateKey_{privKey}
{
   if (ephemeralPrivateKey_ == nullptr) {
      throw Exception("no private key for store view");
   }
}

////////
bool PeerStoreView::signChallenge(
   const uint8_t* challenge, BinaryData& output) const
{
   if (output.getSize() != 64) {
      return false;
   }

   if (wallet_ == nullptr) {
      //no wallet, use the ephemeral private key instead
      size_t resSize;
      if (btc_ecc_sign_compact(
         ephemeralPrivateKey_->getPtr(),
         challenge,
         output.getPtr(),
         &resSize) == false) {
         return false;
      }
      return resSize == output.getSize();
   }

   //1. grab primary asset from wallet
   auto primaryAsset = getPrimaryAssetFromWallet(wallet_);

   //2. grab clear text priv key from asset
   auto lock = wallet_->lockDecryptedContainer(
      [](const std::set<Wallets::EncryptionKeyId>&)->Passphrase::Result
      { return {SecureBinaryData::fromString(PEERS_WALLET_PASSWORD), true}; }
   );
   const auto& privateKey = wallet_->getDecryptedValue(
      primaryAsset->getPrivKey());

   //3. sign the challenge
   size_t resSize;
   if (btc_ecc_sign_compact(
      privateKey.getPtr(),
      challenge,
      output.getPtr(),
      &resSize) == false) {
      return false;
   }
   return resSize == output.getSize();
}

BinaryDataRef PeerStoreView::getPubKeyRef(const std::string& name) const
{
   const auto& nameMap = peerMap_->getPeerNameMap();
   auto iter = nameMap.find(name);
   if (iter == nameMap.end()) {
      throw std::runtime_error("unknown name");
   }
   return iter->second.getRef();
}

const std::string& PeerStoreView::getLabel(const SecureBinaryData& key) const
{
   return peerMap_->getLabel(key);
}

////////
const std::map<SecureBinaryData, std::string>& PeerStoreView::getPublicKeyMap() const
{
   return peerMap_->getPublicKeyMap();
}

const std::map<std::string, SecureBinaryData>& PeerStoreView::getPeerNameMap() const
{
   return peerMap_->getPeerNameMap();
}
