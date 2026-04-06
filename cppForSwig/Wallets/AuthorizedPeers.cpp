////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cstring>

#include "AuthorizedPeers.h"
#include <Utils/BIP150_151.h>
#include <Utils/DBUtils.h>
#include <Utils/BtcUtils.h>

#include "Accounts/AccountTypes.h"
#include "Accounts/AddressAccounts.h"
#include "Accounts/MetaAccounts.h"
#include "Wallets.h"
#include "WalletFileInterface.h"
#include "Seeds/Seeds.h"
#include "TerminalPassphrasePrompt.h"
#include "BIP32_Node.h"

using namespace Armory::Assets;
using namespace Armory::Accounts;
using namespace Armory::Wallets;
using namespace Armory::Seeds;

using namespace std::chrono_literals;
using namespace std::string_view_literals;

#define ARMORY_PEERKEY_VERSION   0x01

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData AuthorizedPeers::setOwnPrivateKey(SecureBinaryData& privateKey)
{
   //compute the public key
   auto ownPubKey = Cryptography::ECDSA::computePublicKey(privateKey);
   auto ownPubKey_compressed = Cryptography::ECDSA::compressPoint(ownPubKey);

   //add to private keys map
   privateKeys_.emplace(ownPubKey_compressed, std::move(privateKey));
   return ownPubKey_compressed;
}

void AuthorizedPeers::initPeerMaps(const SecureBinaryData& pubkey)
{
   btc_pubkey btc_own;
   btc_pubkey_init(&btc_own);
   std::memcpy(btc_own.pubkey, pubkey.getPtr(), BIP151PUBKEYSIZE);
   btc_own.compressed = true;
   peerMapOneWay_ = std::make_unique<PeerMap>(wallet_, btc_own, true);
   peerMapTwoWay_ = std::make_unique<PeerMap>(wallet_, btc_own, false);
}

////////////////////////////////////////////////////////////////////////////////
AuthorizedPeers::AuthorizedPeers(std::shared_ptr<AssetWallet> wltPtr) :
   wallet_(wltPtr)
{
   initFromWallet();
}

////
AuthorizedPeers::AuthorizedPeers(const IO::ReadOnlyFileParams& params)
{
   loadWallet(params);
   initFromWallet();
}

////
AuthorizedPeers::AuthorizedPeers()
{
   //No filename was passed, create an ephemral peer db instead
   auto privateKey = Cryptography::PRNG::generateRandomStrong(32);
   auto pubkey = setOwnPrivateKey(privateKey);
   initPeerMaps(pubkey);
}

AuthorizedPeers::AuthorizedPeers(SecureBinaryData& privateKey)
{
   auto pubkey = setOwnPrivateKey(privateKey);
   initPeerMaps(pubkey);
}

////////
void AuthorizedPeers::loadWallet(const IO::ReadOnlyFileParams& params)
{
   if (!FileUtils::fileExists(params.filePath, 6)) {
      throw PeerFileMissing();
   }
   wallet_ = AssetWallet::loadMainWalletFromFile(params);
}

void AuthorizedPeers::initFromWallet()
{
   if (wallet_ == nullptr) {
      throw AuthorizedPeersException("failed to initialize peer wallet");
   }
   //grab all meta entries, populate public key map
   auto peerAccount = wallet_->getMetaAccount(MetaAccountType::AuthPeers);
   auto peerAssets = AuthPeerAssetConversion::getAssetMap(peerAccount.get());

   //get the private key
   SecureBinaryData ownPubKey_compressed;
   {
      //create & set password lambda
      auto passphrasePrompt = [](const std::set<EncryptionKeyId>&)
      ->Passphrase::Result
      {
         return {
            SecureBinaryData::fromString(PEERS_WALLET_PASSWORD),
            true
         };
      };
      wallet_->setPassphrasePromptLambda(passphrasePrompt);

      //grab decryption container lock
      auto lock = wallet_->lockDecryptedContainer();

      auto walletSingle = std::dynamic_pointer_cast<AssetWallet_Single>(wallet_);
      if (walletSingle == nullptr) {
         throw AuthorizedPeersException("unexpected wallet type");
      }

      //grab asset #1 on main peers chain (m'/PEERS_WALLET_BIP32_ACCOUNT'/0')
      auto mainAcc = walletSingle->getAccountForID(
         walletSingle->getMainAccountID());
      auto outerAccount = mainAcc->getOuterAccount();
      auto assetPtr = outerAccount->getAssetForKey(1);
      auto assetSingle = std::dynamic_pointer_cast<AssetEntry_Single>(assetPtr);

      auto privateKey = wallet_->getDecryptedValue(
         assetSingle->getPrivKey());
      ownPubKey_compressed = setOwnPrivateKey(privateKey);
   }

   //init peer maps
   initPeerMaps(ownPubKey_compressed);

   //populate name key pairs
   peerMapOneWay_->setupFromAssetMap(peerAssets);
   peerMapOneWay_->grabKeyIndexes(peerAccount);
   peerMapTwoWay_->setupFromAssetMap(peerAssets);
   peerMapTwoWay_->grabKeyIndexes(peerAccount);

   //set master key
   if (peerAssets.masterKey.empty()) {
      return;
   }
   masterKey_ = std::move(peerAssets.masterKey);
}

std::shared_ptr<AuthorizedPeers> AuthorizedPeers::createWallet(
   const IO::CreateFileParams& params)
{
   //Default peers wallet private keys password. Asset wallets always
   //encrypt private keys, we have to provide a password at creation.
   auto privPass = SecureBinaryData::fromString(PEERS_WALLET_PASSWORD);

   //wrap around the provided CreateFileParams to create the AssetWallet
   //the peers db will run off of
   IO::CreateWalletParams walletParams{
      params.filePath.parent_path(),
      Passphrase::SetNew{1ms, 0, privPass},
      params.setCtrlPassObj.copy(), nullptr,
      0, {}, {}
   };

   std::shared_ptr<AssetWallet_Single> wallet;
   {
      //Default peers wallet derivation path. Using m/'account/'0.
      std::vector<uint32_t> derPath;
      derPath.push_back(PEERS_WALLET_BIP32_ACCOUNT);
      derPath.push_back(0xF0000000);

      //generate bip32 node from random seed
      wallet = AssetWallet_Single::createFromSeed(
         std::make_unique<ClearTextSeed_BIP32>(
            Cryptography::PRNG::generateRandomStrong(32),
            SeedType::BIP32_Virgin),
         walletParams);
      auto wltSingle = std::dynamic_pointer_cast<AssetWallet_Single>(wallet);

      auto rootBip32 = std::dynamic_pointer_cast<AssetEntry_BIP32Root>(
         wltSingle->getRoot());
      if (rootBip32 == nullptr) {
         throw AuthorizedPeersException("[createWallet] invalid root");
      }
      auto account = AccountType_BIP32::makeFromDerPaths(
         rootBip32->getSeedFingerprint(false), {derPath});
      account->setMain(true);
      account->setAddressLookup(2);

      wallet->setPassphrasePromptLambda(
         walletParams.setPrivPassObj.getUnlockFunc());
      wltSingle->createBIP32Account(account, nullptr);
   }

   //add the peers meta account
   wallet->addMetaAccount(MetaAccountType::AuthPeers);

   //grab wallet filename
   auto currentname = wallet->getDbFilename();

   //destroying the wallet will shutdown the underlying db object
   wallet.reset();

   //rename peers wallet to desired name
   try {
      std::filesystem::rename(currentname, params.filePath);
   } catch (const std::filesystem::filesystem_error&) {
      throw AuthorizedPeersException("failed to setup peers wallet");
   }
   currentname.append("-lock");
   std::filesystem::remove(currentname);

   IO::ReadOnlyFileParams roFileParams{params.filePath,
      walletParams.setCtrlPassObj.getUnlockFunc()};
   return std::make_shared<AuthorizedPeers>(roFileParams);
}

std::shared_ptr<AuthorizedPeers> AuthorizedPeers::getNarrowSet(
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
      throw AuthorizedPeersException("unknown peer");
   }

   //get names for this key
   std::vector<std::string> peerNames;
   for (const auto& namePair : peerMap->getPeerNameMap()) {
      if (std::memcmp(namePair.second.pubkey,
         peerObj.getKey().getPtr(),
         BIP151PUBKEYSIZE) == 0) {
         peerNames.emplace_back(namePair.first);
      }
   }
   if (peerNames.empty()) {
      throw AuthorizedPeersException("no name for this key");
   }

   BinaryDataRef ownKeyRef{getOwnPublicKey().pubkey, BIP151PUBKEYSIZE};
   auto privateKey = getPrivateKey(ownKeyRef);
   auto result = std::shared_ptr<AuthorizedPeers>(
      new AuthorizedPeers(privateKey));
   result->addPeer(peerObj, peerNames, {});
   return result;
}

////////////////////////////////////////////////////////////////////////////////
const std::map<std::string, btc_pubkey>& AuthorizedPeers::getPeerNameMap(
   bool oneWay) const
{
   return oneWay ?
      peerMapOneWay_->getPeerNameMap() :
      peerMapTwoWay_->getPeerNameMap();
}

////////////////////////////////////////////////////////////////////////////////
const std::map<SecureBinaryData, std::string>&
AuthorizedPeers::getPublicKeyMap(bool oneWay) const
{
   return oneWay ?
      peerMapOneWay_->getPublicKeyMap() :
      peerMapTwoWay_->getPublicKeyMap();
}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& AuthorizedPeers::getPrivateKey(
   const BinaryDataRef& pubkey) const
{
   auto iter = privateKeys_.find(pubkey);
   if (iter == privateKeys_.end()) {
      throw AuthorizedPeersException("unknown private key");
   }
   return iter->second;
}

void AuthorizedPeers::addPeer(const PeerKey& peerKey,
   const std::vector<std::string>& names, const std::string& label)
{
   addPeer(peerKey.getKey(), names, label, peerKey.isOneWay());
}

////////
void AuthorizedPeers::addPeer(const btc_pubkey& pubkey,
   const std::vector<std::string>& names, const std::string& label, bool oneWay)
{
   SecureBinaryData keySbd{pubkey.pubkey, pubkey.compressed ? 33u : 65u};
   addPeer(keySbd, names, label, oneWay);
}

////////
void AuthorizedPeers::addPeer(const SecureBinaryData& pubkey,
   const std::vector<std::string>& names, const std::string& label, bool oneWay)
{
   if (oneWay) {
      peerMapOneWay_->addPeer(pubkey, names, label);
   } else {
      peerMapTwoWay_->addPeer(pubkey, names, label);
   }
}

////////////////////////////////////////////////////////////////////////////////
void AuthorizedPeers::eraseName(const std::string& name, bool oneWay)
{
   auto* peerMap = oneWay ? peerMapOneWay_.get() : peerMapTwoWay_.get();
   auto key = peerMap->eraseName(name);

   bool cleanupMasterKey = false;
   if (!masterKey_.empty() && std::memcmp(
      key.pubkey, masterKey_.getPtr(), BIP151PUBKEYSIZE)) {
      masterKey_.clear();
      cleanupMasterKey = true;
   }

   if (wallet_ == nullptr) {
      return;
   }

   auto metaAccount = wallet_->getMetaAccount(MetaAccountType::AuthPeers);
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   std::shared_ptr<IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   if (cleanupMasterKey) {
      AuthPeerAssetConversion::clearMasterKeyAssets(metaAccount.get());
   }
   metaAccount->updateOnDisk(sharedTx);
}

////////////////////////////////////////////////////////////////////////////////
void AuthorizedPeers::erasePeer(const PeerKey& peer)
{
   eraseKey(peer.getKey(), peer.isOneWay());
}

void AuthorizedPeers::eraseKey(const btc_pubkey& pubkey, bool oneWay)
{
   size_t size = 65;
   if (pubkey.compressed) {
      size = 33;
   }
   SecureBinaryData keySbd(size);
   std::memcpy(keySbd.getPtr(), pubkey.pubkey, size);
   eraseKey(keySbd, oneWay);
}

void AuthorizedPeers::eraseKey(const SecureBinaryData& pubkey, bool oneWay)
{
   SecureBinaryData pubkey_cmp;
   if (pubkey.getSize() == 65) {
      pubkey_cmp = Cryptography::ECDSA::compressPoint(pubkey);
   } else {
      pubkey_cmp = pubkey;
   }

   bool cleanupMasterKey = false;
   if (pubkey_cmp == masterKey_) {
      masterKey_.clear();
      cleanupMasterKey = true;
   }

   if (oneWay) {
      peerMapOneWay_->eraseKey(pubkey_cmp);
   } else {
      peerMapTwoWay_->eraseKey(pubkey_cmp);
   }

   if (wallet_ == nullptr) {
      return;
   }

   //update on disk
   auto metaAccount = wallet_->getMetaAccount(MetaAccountType::AuthPeers);
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   std::shared_ptr<IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   if (cleanupMasterKey) {
      AuthPeerAssetConversion::clearMasterKeyAssets(metaAccount.get());
   }
   metaAccount->updateOnDisk(sharedTx);
}

////////////////////////////////////////////////////////////////////////////////
const btc_pubkey& AuthorizedPeers::getOwnPublicKey() const
{
   const auto& nameMap = peerMapOneWay_->getPeerNameMap();
   auto iter = nameMap.find("own");
   if (iter == nameMap.end()) {
      throw AuthorizedPeersException("malformed authpeer object");
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
const std::string& AuthorizedPeers::getLabel(
   const SecureBinaryData& key, bool oneWay) const
{
   const auto& keyMap = oneWay ?
      peerMapOneWay_->getPublicKeyMap() :
      peerMapTwoWay_->getPublicKeyMap();
   auto iter = keyMap.find(key);
   if (iter == keyMap.end()) {
      throw std::runtime_error("unknown peer key");
   }
   return iter->second;
}

////
void AuthorizedPeers::setLabel(const PeerKey& key, const std::string& label)
{
   setLabel(key.getKey(), label, key.isOneWay());
}

void AuthorizedPeers::setLabel(
   const SecureBinaryData& key, const std::string& label, bool oneWay)
{
   if (oneWay) {
      peerMapOneWay_->setLabel(key, label);
   } else {
      peerMapTwoWay_->setLabel(key, label);
   }
}

////////////////////////////////////////////////////////////////////////////////
void AuthorizedPeers::changeControlPassphrase(const std::filesystem::path& path)
{
   //get a terminal prompt lambda
   auto promptPtr = TerminalPassphrasePrompt::getLambda("peers db");

   //load the wallet
   auto wlt = AssetWallet::loadMainWalletFromFile(
      IO::ReadOnlyFileParams{path, promptPtr});

   //change passphrase lambda
   auto changeLbd = [&promptPtr](void)->std::unique_ptr<Passphrase::Params>
   {
      auto result = promptPtr({ BinaryData::fromString("change-pass"sv) });
      if (!result.success) {
         throw std::runtime_error("authdb passphrase change was rejected");
      }
      return std::make_unique<Passphrase::Params>(
         250ms, 0, std::move(result.passphrase));
   };
   Passphrase::SetNew changePassObj{changeLbd};

   //change the passphrase
   wlt->changeControlPassphrase(changePassObj, promptPtr);
}

////////////////////////////////////////////////////////////////////////////////
AuthPeersLambdas AuthorizedPeers::getAuthPeersLambdas(
   std::shared_ptr<AuthorizedPeers> authPeers, bool oneWay)
{
   auto getMap = [authPeers, oneWay]()->const std::map<std::string, btc_pubkey>&
   {
      return authPeers->getPeerNameMap(oneWay);
   };

   auto getPrivKey = [authPeers](const BinaryDataRef& pubkey)
   ->const SecureBinaryData&
   {
      return authPeers->getPrivateKey(pubkey);
   };

   auto getAuthSet = [authPeers, oneWay]()->
   const std::map<SecureBinaryData, std::string>&
   {
      return authPeers->getPublicKeyMap(oneWay);
   };

   return AuthPeersLambdas(getMap, getPrivKey, getAuthSet);
}

////////////////////////////////////////////////////////////////////////////////
bool AuthorizedPeers::setMasterKey(const btc_pubkey& pubkey)
{
   SecureBinaryData keySbd{pubkey.pubkey, pubkey.compressed ? 33u : 65u};
   return setMasterKey(keySbd);
}

////
bool AuthorizedPeers::setMasterKey(const SecureBinaryData& pubkey)
{
   //blindly add the master key, the youngest asset takes precedence
   if (masterKey_ == pubkey) {
      return true;
   }

   if (!Cryptography::ECDSA::verifyPublicKeyValid(pubkey)) {
      //not a valid pubkey
      return false;
   }

   const auto& keyMap1Way = getPublicKeyMap(true);
   if (keyMap1Way.find(pubkey) == keyMap1Way.end()) {
      const auto& keyMap2Way = getPublicKeyMap(false);
      if (keyMap2Way.find(pubkey) == keyMap2Way.end()) {
         //master key isn't known to peers store, ignore
         return false;
      }
   }

   //set in wallet
   if (wallet_ != nullptr) {
      auto metaAccount = wallet_->getMetaAccount(MetaAccountType::AuthPeers);
      auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
         wallet_->getDbName());
      std::shared_ptr<IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
      AuthPeerAssetConversion::addMasterKey(metaAccount.get(), pubkey, sharedTx);
   }

   //set locally
   masterKey_ = pubkey;
   return true;
}

////
void AuthorizedPeers::eraseMasterKey()
{
   if (masterKey_.empty()) {
      return;
   }
   masterKey_.clear();

   if (wallet_ == nullptr) {
      return;
   }

   auto metaAccount = wallet_->getMetaAccount(MetaAccountType::AuthPeers);
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   std::shared_ptr<IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   AuthPeerAssetConversion::clearMasterKeyAssets(metaAccount.get());
   metaAccount->updateOnDisk(sharedTx);
}

////////////////////////////////////////////////////////////////////////////////
bool AuthorizedPeers::isMasterKey(const btc_pubkey& pubkey) const
{
   if (masterKey_.empty()) {
      return false;
   }

   BinaryDataRef keyRef{pubkey.pubkey, pubkey.compressed ? 33u : 65u};
   return masterKey_.getRef() == keyRef;
}

////
bool AuthorizedPeers::isMasterKey(const SecureBinaryData& pubkey) const
{
   if (masterKey_.empty()) {
      return false;
   }
   return masterKey_.getRef() == pubkey.getRef();
}

////////////////////////////////////////////////////////////////////////////////
// PeerKey
PeerKey::PeerKey(BinaryDataRef pubkey, bool isOneWay, bool isServer) :
   pubkey_(pubkey), oneWayAuth_(isOneWay), isServer_(isServer)
{}

PeerKey::PeerKey(const btc_pubkey& pubkey, bool isOneWay, bool isServer) :
   pubkey_{pubkey.pubkey, BIP151PUBKEYSIZE},
   oneWayAuth_(isOneWay), isServer_(isServer)
{}

const SecureBinaryData& PeerKey::getKey() const
{
   return pubkey_;
}

bool PeerKey::isServer() const
{
   return isServer_;
}

bool PeerKey::isOneWay() const
{
   return oneWayAuth_;
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

   if (isServer_) {
      if (oneWayAuth_) {
         //AR1 v1
         *ptr |= 0x00411D01;
      } else {
         //AR2 v1
         *ptr |= 0x00811D01;
      }
   } else {
      //ARc v1
      *ptr |= 0x00011701;
   }

   /*
   BinaryWriter bw;
   bw.reserve(36);
   bw.put_uint16_t(ARMORY_PEERKEY_PREFIX, BE);
   bw.put_uint8_t(ARMORY_PEERKEY_VERSION);
   uint8_t mode = isServer_ ? 0 : ARMORY_PEERKEY_ISCLIENT;
   if (oneWayAuth_ && isServer_) {
      mode |= ARMORY_PEERKEY_ISONEWAY;
   }
   bw.put_uint8_t(mode);
   bw.put_BinaryDataRef(pubkey_.getRef());
   */

   auto raw = bw.getDataRef();
   std::string_view rawView{raw.toCharPtr(), raw.getSize()};
   return BtcUtils::base64_encode(rawView);
}

PeerKey PeerKey::fromHumanReadable(const std::string& str)
{
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

   bool oneWay = false;
   bool isServer = false;
   switch (val[2]) {
      case 28:
      {
         oneWay = false;
         isServer = false;
         break;
      }

      case 53:
      {
         oneWay = true;
         isServer = true;
         break;
      }

      case 54:
      {
         oneWay = false;
         isServer = true;
         break;
      }

      default:
         throw std::runtime_error("mangled peer key");
   }

   auto version = val[3];

   switch (version) {
      case 1:
      {
         auto pubkey = brr.get_BinaryDataRef(33);
         return PeerKey{pubkey, oneWay, isServer};
      }

      default:
         throw std::runtime_error("unexpected peer key version");
   }
}

////////////////////////////////////////////////////////////////////////////////
// PeerMap
PeerMap::PeerMap(std::shared_ptr<AssetWallet> wallet,
   const btc_pubkey& ownKey, bool oneWay) :
   wallet_(wallet), oneWay_(oneWay)
{
   nameToKeyMap_.emplace("own", ownKey);
}

void PeerMap::setupFromAssetMap(const Accounts::AuthPeerAssetMap& peerAssets)
{
   const auto& nameMap = oneWay_ ?
      peerAssets.nameKeyMapOneWay : peerAssets.nameKeyMapTwoWay;
   for (auto& pubkey : nameMap) {
      btc_pubkey btckey;
      btc_pubkey_init(&btckey);

      SecureBinaryData pubkey_cmp;
      if (pubkey.second.first->getSize() != BIP151PUBKEYSIZE) {
         pubkey_cmp = Cryptography::ECDSA::compressPoint(*pubkey.second.first);
      } else {
         pubkey_cmp = *pubkey.second.first;
      }

      std::memcpy(btckey.pubkey, pubkey_cmp.getPtr(), BIP151PUBKEYSIZE);
      btckey.compressed = true;
      keyMap_.emplace(pubkey_cmp, *pubkey.second.second);
      nameToKeyMap_.emplace(pubkey.first, btckey);
   }
}

void PeerMap::grabKeyIndexes(
   std::shared_ptr<Accounts::MetaDataAccount> peerAccount)
{
   //grab public key to index map
   keyToAssetIndexMap_ = AuthPeerAssetConversion::getKeyIndexMap(
      peerAccount.get(), oneWay_);
}

void PeerMap::addPeer(const SecureBinaryData& pubkey,
   const std::vector<std::string>& names, const std::string& label)
{
   //sanity check
   if (!Cryptography::ECDSA::verifyPublicKeyValid(pubkey)) {
      throw AuthorizedPeersException("peer lacks a valid public key");
   }
   for (const auto& name : names) {
      if (name == "own") {
         throw AuthorizedPeersException("use of a reserved name");
      }
   }

   //convert sbd pubkey to libbtc pubkey
   SecureBinaryData pubkey_cmp;
   if (pubkey.getSize() == 65) {
      pubkey_cmp = Cryptography::ECDSA::compressPoint(pubkey);
   } else if (pubkey.getSize() == BIP151PUBKEYSIZE) {
      pubkey_cmp = pubkey;
   } else {
      throw AuthorizedPeersException("unexpected public key size");
   }

   btc_pubkey btckey;
   btc_pubkey_init(&btckey);
   std::memcpy(btckey.pubkey, pubkey_cmp.getPtr(), pubkey_cmp.getSize());
   btckey.compressed = true;

   //add all names to key list; using emplace means existing names are
   //not overwritten
   for (auto& name : names) {
      nameToKeyMap_.emplace(name, btckey);
   }
   keyMap_.emplace(pubkey_cmp, label);

   //if we dont have a wallet attached, we're done
   if (wallet_ == nullptr) {
      return;
   }

   //get a dbtx for the wallet & add the pubkey with its names
   auto peerAccount = wallet_->getMetaAccount(MetaAccountType::AuthPeers);
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   std::shared_ptr<IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   auto index = AuthPeerAssetConversion::addAsset(
      peerAccount.get(), pubkey_cmp, names, label, oneWay_, sharedTx);

   //track the asset index for the pubkey
   auto iter = keyToAssetIndexMap_.find(pubkey_cmp);
   if (iter == keyToAssetIndexMap_.end()) {
      iter = keyToAssetIndexMap_.emplace(
         pubkey_cmp, std::set<unsigned>{}).first;
   }
   iter->second.insert(index);
}

////////////////////////////////////////////////////////////////////////////////
btc_pubkey PeerMap::eraseName(const std::string& name)
{
   if (name == "own") {
      throw AuthorizedPeersException("invalid name");
   }

   //find pubkey
   auto keyIter = nameToKeyMap_.find(name);
   if (keyIter == nameToKeyMap_.end()) {
      return {};
   }

   //convert libbtc key to binarydataref
   auto pubkey = keyIter->second;
   BinaryDataRef bdrKey(pubkey.pubkey, BIP151PUBKEYSIZE);

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
         if (std::memcmp(
            namePair.second.pubkey, pubkey.pubkey, BIP151PUBKEYSIZE) == 0) {
            hasKey = true;
            break;
         }
      }

      if (!hasKey) {
         //erase from key set
         keyMap_.erase(bdrKey);
      }
      return pubkey;
   }

   //get the list of wallet assets this pub key appears in
   auto indexIter = keyToAssetIndexMap_.find(bdrKey);
   if (indexIter == keyToAssetIndexMap_.end()) {
      return {};
   }

   //grab metadata account from wallet, cycle through assets, clean up
   //indexMap as we go
   auto metaAccount = wallet_->getMetaAccount(MetaAccountType::AuthPeers);
   auto setIter = indexIter->second.begin();
   while (setIter != indexIter->second.end()) {
      const auto& index = *setIter;
      std::shared_ptr<MetaData> metaPtr;
      try {
         metaPtr = metaAccount->getMetaDataByIndex(index);
      } catch (const std::exception&) {
         indexIter->second.erase(setIter++);
         continue;
      }

      auto peerPtr = std::dynamic_pointer_cast<PeerPublicData>(metaPtr);
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

////
void PeerMap::eraseKey(const SecureBinaryData& pubkey)
{
   //make sure we're working with compressed keys only
   if (pubkey.getSize() != BIP151PUBKEYSIZE) {
      throw AuthorizedPeersException("invalid pubkey size");
   }

   btc_pubkey btckey;
   btc_pubkey_init(&btckey);
   std::memcpy(btckey.pubkey, pubkey.getPtr(), BIP151PUBKEYSIZE);
   btckey.compressed = true;

   //erase from public key set
   if (keyMap_.erase(pubkey) == 0) {
      return;
   }

   if (wallet_ == nullptr) {
      //lacking a wallet to build a set of names for this pubkey, scoure the
      //name-key map linearly, clear it and we're done

      auto keyIter = nameToKeyMap_.begin();
      while (keyIter != nameToKeyMap_.end()) {
         if (std::memcmp(keyIter->second.pubkey,
            btckey.pubkey,
            BIP151PUBKEYSIZE) == 0) {
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
   auto metaAccount = wallet_->getMetaAccount(MetaAccountType::AuthPeers);
   std::set<std::string> namesToDelete;

   for (auto& index : iter->second) {
      std::shared_ptr<MetaData> metaPtr;
      try {
         metaPtr = metaAccount->getMetaDataByIndex(index);
      } catch (const std::exception&) {
         continue;
      }

      auto peerPtr = std::dynamic_pointer_cast<PeerPublicData>(metaPtr);
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

////////////////////////////////////////////////////////////////////////////////
void PeerMap::setLabel(const SecureBinaryData& key, const std::string& label)
{
   auto iter = keyMap_.find(key);
   if (iter == keyMap_.end()) {
      throw std::runtime_error("unknown peer key");
   }
   iter->second = label;

   //if we dont have a wallet attached, we're done
   if (wallet_ == nullptr) {
      return;
   }

   //grab metadata account from wallet, cycle through assets, clean up
   //indexMap as we go
   auto metaAccount = wallet_->getMetaAccount(MetaAccountType::AuthPeers);

   //run through the set of wallet assets this pub key appears in
   auto indexIter = keyToAssetIndexMap_.find(key);
   auto setIter = indexIter->second.begin();
   while (setIter != indexIter->second.end()) {
      const auto& index = *setIter;
      std::shared_ptr<MetaData> metaPtr;
      try {
         metaPtr = metaAccount->getMetaDataByIndex(index);
      } catch (const std::exception&) {
         continue;
      }

      auto peerPtr = std::dynamic_pointer_cast<PeerPublicData>(metaPtr);
      if (peerPtr == nullptr) {
         continue;
      }

      peerPtr->setLabel(label);
      ++setIter;
   }

   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   std::shared_ptr<IO::DBIfaceTransaction> sharedTx(std::move(uniqueTx));
   metaAccount->updateOnDisk(sharedTx);
}

////
const std::map<std::string, btc_pubkey>& PeerMap::getPeerNameMap() const
{
   return nameToKeyMap_;
}

const std::map<SecureBinaryData, std::string>& PeerMap::getPublicKeyMap() const
{
   return keyMap_;
}
