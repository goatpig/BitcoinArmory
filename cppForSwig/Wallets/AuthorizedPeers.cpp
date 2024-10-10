////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2024, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <cstdarg>
#include <filesystem>

#include "BIP150_151.h"
#include "BIP32_Node.h"
#include "AuthorizedPeers.h"
#include "btc/ecc.h"
#include "WalletFileInterface.h"
#include "Seeds/Seeds.h"

#include "TerminalPassphrasePrompt.h"

using namespace std;
using namespace Armory::Assets;
using namespace Armory::Accounts;
using namespace Armory::Wallets;
using namespace Armory::Seeds;

////////////////////////////////////////////////////////////////////////////////
AuthorizedPeers::AuthorizedPeers(
   const string& datadir, const string& filename,
   const PassphraseLambda& passLbd)
{
   auto path = std::filesystem::path(datadir) / filename;

   try {
      //try to load wallet
      loadWallet(path, passLbd);
   } catch (const PeerFileMissing&) {
      //the wallet hasn't be setup to begin with, create it
      createWallet(datadir, filename, passLbd);
   }

   if (wallet_ == nullptr) {
      throw AuthorizedPeersException("failed to initialize peer wallet");
   }
   //grab all meta entries, populate public key map
   auto peerAccount = wallet_->getMetaAccount(MetaAccount_AuthPeers);
   auto peerAssets = AuthPeerAssetConversion::getAssetMap(peerAccount.get());

   //root signature
   rootSignature_ = std::move(peerAssets.rootSignature_);

   //name key pairs
   for (auto& pubkey : peerAssets.nameKeyPair_) {
      btc_pubkey btckey;
      btc_pubkey_init(&btckey);

      SecureBinaryData pubkey_cmp;
      if (pubkey.second->getSize() != BIP151PUBKEYSIZE)
         pubkey_cmp = CryptoECDSA().CompressPoint(*pubkey.second);
      else
         pubkey_cmp = *pubkey.second;

      std::memcpy(btckey.pubkey, pubkey_cmp.getPtr(), BIP151PUBKEYSIZE);
      btckey.compressed = true;
      keySet_.insert(pubkey_cmp);
      nameToKeyMap_.emplace(make_pair(pubkey.first, btckey));
   }

   //peer root public keys
   peerRootKeys_ = move(peerAssets.peerRootKeys_);
   
   //get the private key
   SecureBinaryData privateKey;
   {
      //create & set password lambda
      auto passphrasePrompt = [](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return SecureBinaryData::fromString(PEERS_WALLET_PASSWORD);
      };

      wallet_->setPassphrasePromptLambda(passphrasePrompt);

      //grab decryption container lock
      auto lock = wallet_->lockDecryptedContainer();

      auto walletSingle = dynamic_pointer_cast<AssetWallet_Single>(wallet_);
      if (walletSingle == nullptr)
         throw AuthorizedPeersException("unexpected wallet type");
      
      //grab asset #1 on main peers chain (m'/PEERS_WALLET_BIP32_ACCOUNT'/0')
      auto mainAcc = walletSingle->getAccountForID(
         walletSingle->getMainAccountID());
      auto outerAccount = mainAcc->getOuterAccount();
      auto assetPtr = outerAccount->getAssetForKey(1);
      auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(assetPtr);

      privateKey =
         wallet_->getDecryptedValue(assetSingle->getPrivKey());
   }

   //compute the public key
   auto ownPubKey = CryptoECDSA().ComputePublicKey(privateKey);
   auto ownPubKey_compressed = CryptoECDSA().CompressPoint(ownPubKey);

   //add to private keys map
   privateKeys_.emplace(make_pair(BinaryData(ownPubKey_compressed), privateKey));

   //add to public key map as own
   btc_pubkey btc_own;
   btc_pubkey_init(&btc_own);
   std::memcpy(btc_own.pubkey, ownPubKey_compressed.getPtr(), BIP151PUBKEYSIZE);
   btc_own.compressed = true;

   nameToKeyMap_.emplace(make_pair("own", btc_own));

   //grab public key to index map
   keyToAssetIndexMap_ =
      move(AuthPeerAssetConversion::getKeyIndexMap(peerAccount.get()));
}

////////////////////////////////////////////////////////////////////////////////
AuthorizedPeers::AuthorizedPeers()
{
   //No filename was passed, create an ephemral peer db instead
   auto privateKey = CryptoPRNG::generateRandom(32);

   //compute the public key
   auto ownPubKey = CryptoECDSA().ComputePublicKey(privateKey);
   auto ownPubKey_compressed = CryptoECDSA().CompressPoint(ownPubKey);

   //add to private keys map
   privateKeys_.emplace(make_pair(BinaryData(ownPubKey_compressed), privateKey));

   //add to public key map as own
   btc_pubkey btc_own;
   btc_pubkey_init(&btc_own);
   std::memcpy(btc_own.pubkey, ownPubKey_compressed.getPtr(), BIP151PUBKEYSIZE);
   btc_own.compressed = true;

   nameToKeyMap_.emplace(make_pair("own", btc_own));
}

////////////////////////////////////////////////////////////////////////////////
void AuthorizedPeers::loadWallet(
   const string& path, const PassphraseLambda& passLbd)
{
   if (!DBUtils::fileExists(path, 6)) {
      throw PeerFileMissing();
   }
   wallet_ = AssetWallet::loadMainWalletFromFile(path, passLbd);
}

////////////////////////////////////////////////////////////////////////////////
void AuthorizedPeers::createWallet(
   const string& baseDir, const string& filename,
   const PassphraseLambda& passLbd)
{
   //Default peers wallet password. Asset wallets always encrypt private keys,
   //have to provide a password at creation.
   auto password = SecureBinaryData::fromString(PEERS_WALLET_PASSWORD);
   auto controlPassphrase = passLbd({});

   {
      //Default peers wallet derivation path. Using m/'account/'0.
      vector<unsigned> derPath;
      derPath.push_back(PEERS_WALLET_BIP32_ACCOUNT);
      derPath.push_back(0xF0000000);

      //generate bip32 node from random seed
      wallet_ = AssetWallet_Single::createFromSeed(
         make_unique<ClearTextSeed_BIP32>(
            CryptoPRNG::generateRandom(32), SeedType::BIP32_Virgin),
         WalletCreationParams{
            password, controlPassphrase, baseDir, 0, 100, 250
         });
      auto wltSingle = dynamic_pointer_cast<AssetWallet_Single>(wallet_);

      auto rootBip32 = dynamic_pointer_cast<AssetEntry_BIP32Root>(
         wltSingle->getRoot());
      if (rootBip32 == nullptr) {
         throw AuthorizedPeersException("[createWallet] invalid root");
      }
      auto account = AccountType_BIP32::makeFromDerPaths(
         rootBip32->getSeedFingerprint(false), {derPath});
      account->setMain(true);
      account->setAddressLookup(2);

      auto privKeyPassLbd = [&password](const set<EncryptionKeyId>&)->SecureBinaryData
      {
         return password;
      };
      wallet_->setPassphrasePromptLambda(privKeyPassLbd);
      wltSingle->createBIP32Account(account);
   }

   //add the peers meta account
   wallet_->addMetaAccount(MetaAccount_AuthPeers);

   //grab wallet filename
   auto currentname = wallet_->getDbFilename();
   
   //destroying the wallet will shutdown the underlying db object
   wallet_.reset();

   //create desired full path filename
   auto path = std::filesystem::path(baseDir) / filename;

   //rename peers wallet to desired name
   try {
      std::filesystem::rename(currentname, path);
   } catch (const std::filesystem::filesystem_error&) {
      throw AuthorizedPeersException("failed to setup peers wallet");
   }
   currentname.append("-lock");
   remove(currentname.c_str());

   //load from new file path in order to have valid db object
   //capture the controlPass in local lambda to avoid prompting the user again
   auto passLbdCycle = [&controlPassphrase](const set<EncryptionKeyId>&)
      ->SecureBinaryData
   {
      return controlPassphrase;
   };
   wallet_ = AssetWallet::loadMainWalletFromFile(path, passLbdCycle);
}

////////////////////////////////////////////////////////////////////////////////
const map<string, btc_pubkey>& AuthorizedPeers::getPeerNameMap() const
{
   return nameToKeyMap_;
}

////////////////////////////////////////////////////////////////////////////////
const set<SecureBinaryData>& AuthorizedPeers::getPublicKeySet() const
{
   return keySet_;
}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& AuthorizedPeers::getPrivateKey(
   const BinaryDataRef& pubkey) const
{
   auto iter = privateKeys_.find(pubkey);
   if (iter == privateKeys_.end())
      throw AuthorizedPeersException("unknown private key");

   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
void AuthorizedPeers::addPeer(const SecureBinaryData& pubkey, 
   const std::initializer_list<std::string>& names)
{
   vector<string> namesVec(names);
   addPeer(pubkey, namesVec);
}

////////////////////////////////////////////////////////////////////////////////
void AuthorizedPeers::addPeer(const SecureBinaryData& pubkey, 
   const std::vector<std::string>& names)
{
   //convert sbd pubkey to libbtc pubkey
   SecureBinaryData pubkey_cmp;
   if (pubkey.getSize() == 65)
      pubkey_cmp = CryptoECDSA().CompressPoint(pubkey);
   else if (pubkey.getSize() == BIP151PUBKEYSIZE)
      pubkey_cmp = pubkey;
   else
      throw AuthorizedPeersException("unexpected public key size");

   btc_pubkey btckey;
   btc_pubkey_init(&btckey);
   std::memcpy(btckey.pubkey, pubkey_cmp.getPtr(), pubkey_cmp.getSize());
   btckey.compressed = true;

   //add all names to key list; using insert means existing names are
   //not overwritten
   for (auto& name : names)
      nameToKeyMap_.insert(make_pair(name, btckey));
   keySet_.insert(pubkey_cmp);

   if (wallet_ == nullptr)
      return;

   auto peerAccount = wallet_->getMetaAccount(MetaAccount_AuthPeers);
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   shared_ptr<IO::DBIfaceTransaction> sharedTx(move(uniqueTx));
   auto index = AuthPeerAssetConversion::addAsset(
      peerAccount.get(), pubkey_cmp, names, sharedTx);

   auto iter = keyToAssetIndexMap_.find(pubkey_cmp);
   if (iter == keyToAssetIndexMap_.end())
   {
      auto insertIter = keyToAssetIndexMap_.insert(make_pair(
         pubkey_cmp, set<unsigned>()));
      iter = insertIter.first;
   }

   iter->second.insert(index);
}

////////////////////////////////////////////////////////////////////////////////
void AuthorizedPeers::addPeer(const btc_pubkey& pubkey,
   const std::initializer_list<std::string>& names)
{
   btc_pubkey pubkey_cmp;
   const btc_pubkey* keyPtr;

   //convert sbd pubkey to libbtc pubkey
   if (!pubkey.compressed)
   {
      pubkey_cmp = CryptoECDSA::CompressPoint(pubkey);
      keyPtr = &pubkey_cmp;
   }
   else
   {
      keyPtr = &pubkey;
   }

   //add all names to key list; using insert means existing names are
   //not overwritten
   for (auto& name : names)
      nameToKeyMap_.insert(make_pair(name, *keyPtr));
   
   SecureBinaryData keySbd(keyPtr->pubkey, BIP151PUBKEYSIZE);
   keySet_.insert(keySbd);

   if (wallet_ == nullptr)
      return;

   auto peerAccount = wallet_->getMetaAccount(MetaAccount_AuthPeers);   
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   shared_ptr<IO::DBIfaceTransaction> sharedTx(move(uniqueTx));
   auto index = AuthPeerAssetConversion::addAsset(
      peerAccount.get(), keySbd, names, sharedTx);

   auto iter = keyToAssetIndexMap_.find(keySbd);
   if (iter == keyToAssetIndexMap_.end())
   {
      auto insertIter = keyToAssetIndexMap_.insert(make_pair(
         keySbd, set<unsigned>()));
      iter = insertIter.first;
   }

   iter->second.insert(index);
}

////////////////////////////////////////////////////////////////////////////////
void AuthorizedPeers::eraseName(const string& name)
{
   if (name == "own")
      throw AuthorizedPeersException("invalid name");

   //find pubkey
   auto keyIter = nameToKeyMap_.find(name);
   if (keyIter == nameToKeyMap_.end())
      return;

   auto pubkey = keyIter->second;

   //convert libbtc key to binarydataref
   BinaryDataRef bdrKey(pubkey.pubkey, BIP151PUBKEYSIZE);

   //get the list wallet assets this pub key appears in
   auto indexIter = keyToAssetIndexMap_.find(bdrKey);

   //erase name from map
   nameToKeyMap_.erase(keyIter);

   if (wallet_ == nullptr)
   {
      /*
      We need to know if the name to erase is the last one refering to its
      relevant pubkey. If so, we need to delete the pubkey from the keySet
      as well, as it doesn't represent an valid peer anymore.

      In the absence of a wallet, we can't rely on it to sort public keys 
      by name. Instead, parse nameToKeyMap linearly for other instances of
      the key
      */

      bool hasKey = false;
      for (auto& namePair : nameToKeyMap_)
      {
         if (std::memcmp(
            namePair.second.pubkey, pubkey.pubkey, BIP151PUBKEYSIZE) == 0)
         {
            hasKey = true;
            break;
         }
      }
        
      if(!hasKey)
      {
         //erase from key set
         BinaryDataRef bdr(pubkey.pubkey, BIP151PUBKEYSIZE);
         keySet_.erase(bdr);
      }

      return;
   }

   if (indexIter == keyToAssetIndexMap_.end())
      return;

   //grab metadata account from wallet, cycle through assets, clean up
   //indexMap as we go
   auto metaAccount = wallet_->getMetaAccount(MetaAccount_AuthPeers);
   auto setIter = indexIter->second.begin();
   while(setIter != indexIter->second.end())
   {
      auto& index = *setIter;
      shared_ptr<MetaData> metaPtr;
      try
      {
         metaPtr = metaAccount->getMetaDataByIndex(index);
      }
      catch (exception&)
      {
         indexIter->second.erase(setIter++);
         continue;
      }

      auto peerPtr = dynamic_pointer_cast<PeerPublicData>(metaPtr);
      if (peerPtr == nullptr)
      {
         indexIter->second.erase(setIter++);
         continue;
      }

      if (peerPtr->eraseName(name))
      {
         if (peerPtr->getNames().size() == 0)
         {
            indexIter->second.erase(setIter++);
            continue;
         }
      }

      ++setIter;
   }

   //remove public key from index map if it isn't related to any assets
   if (indexIter->second.size() == 0)
   {
      keySet_.erase(indexIter->first);
      keyToAssetIndexMap_.erase(indexIter);
   }

   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   shared_ptr<IO::DBIfaceTransaction> sharedTx(move(uniqueTx));
   metaAccount->updateOnDisk(sharedTx);
}

////////////////////////////////////////////////////////////////////////////////
void AuthorizedPeers::eraseKey(const btc_pubkey& pubkey)
{
   size_t size = 65;
   if (pubkey.compressed)
      size = 33;

   SecureBinaryData keySbd(size);
   std::memcpy(keySbd.getPtr(), pubkey.pubkey, size);
   eraseKey(keySbd);
}

////////////////////////////////////////////////////////////////////////////////
void AuthorizedPeers::eraseKey(const SecureBinaryData& pubkey)
{
   //make sure we're working with compressed keys only
   SecureBinaryData pubkey_cmp;
   if (pubkey.getSize() == 65)
      pubkey_cmp = CryptoECDSA().CompressPoint(pubkey);
   else
      pubkey_cmp = pubkey;

   btc_pubkey btckey;
   btc_pubkey_init(&btckey);
   std::memcpy(btckey.pubkey, pubkey_cmp.getPtr(), BIP151PUBKEYSIZE);
   btckey.compressed = true;

   //erase from public key set
   if (keySet_.erase(pubkey_cmp) == 0)
   {
      erasePeerRootKey(pubkey);
      return;
   }

   if (wallet_ == nullptr)
   {
      //lacking a wallet to build a set of names for this pubkey, scoure the 
      //name-key map linearly, clear it and we're done

      auto keyIter = nameToKeyMap_.begin();
      while(keyIter != nameToKeyMap_.end())
      {
         if (std::memcmp(keyIter->second.pubkey, btckey.pubkey, BIP151PUBKEYSIZE) == 0)
         {
            nameToKeyMap_.erase(keyIter++);
            continue;
         }

         ++keyIter;
      }

      return;
   }

   //we have a wallet, need to clear entries on disk and compile name list for
   //the public key
   auto iter = keyToAssetIndexMap_.find(pubkey_cmp);
   if (iter == keyToAssetIndexMap_.end())
      return;

   auto metaAccount = wallet_->getMetaAccount(MetaAccount_AuthPeers);
   set<string> namesToDelete;

   for (auto& index : iter->second)
   {
      shared_ptr<MetaData> metaPtr;
      try
      {
         metaPtr = metaAccount->getMetaDataByIndex(index);
      }
      catch (exception&)
      {
         continue;
      }

      auto peerPtr = dynamic_pointer_cast<PeerPublicData>(metaPtr);
      if (peerPtr == nullptr)
         continue;

      auto& assetNames = peerPtr->getNames();
      namesToDelete.insert(assetNames.begin(), assetNames.end());

      metaAccount->eraseMetaDataByIndex(index);
   }

   //update on disk
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   shared_ptr<IO::DBIfaceTransaction> sharedTx(move(uniqueTx));
   metaAccount->updateOnDisk(sharedTx);

   //erase from index map
   keyToAssetIndexMap_.erase(iter);

   //erase names
   for (auto& name : namesToDelete)
      nameToKeyMap_.erase(name);
}

////////////////////////////////////////////////////////////////////////////////
const btc_pubkey& AuthorizedPeers::getOwnPublicKey() const
{
   auto iter = nameToKeyMap_.find("own");
   if (iter == nameToKeyMap_.end())
      throw AuthorizedPeersException("malformed authpeer object");

   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
void AuthorizedPeers::addRootSignature(
   const SecureBinaryData& key, const SecureBinaryData& sig)
{
   //check key is valid
   if(!CryptoECDSA().VerifyPublicKeyValid(key))
      throw AuthorizedPeersException("invalid root pubkey");

   //check sig is valid
   auto ownKey = getOwnPublicKey();
   BinaryDataRef ownKeyBdr(ownKey.pubkey, 33);
   if(!CryptoECDSA().VerifyData(ownKeyBdr, sig, key))
      throw AuthorizedPeersException("invalid root signature");

   rootSignature_ = make_pair(key, sig);

   if (wallet_ == nullptr)
      return;

   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   shared_ptr<IO::DBIfaceTransaction> sharedTx(move(uniqueTx));
   auto peerAccount = wallet_->getMetaAccount(MetaAccount_AuthPeers);
   AuthPeerAssetConversion::addRootSignature(
      peerAccount.get(), key, sig, sharedTx);
}

////////////////////////////////////////////////////////////////////////////////
void AuthorizedPeers::addPeerRootKey(
   const SecureBinaryData& key, std::string description)
{
   //check key is valid
   if (!CryptoECDSA().VerifyPublicKeyValid(key))
      throw AuthorizedPeersException("invalid root pubkey");

   if (wallet_ == nullptr)
   {
      auto descPair = make_pair(description, 0);
      peerRootKeys_.insert(make_pair(key, descPair));
      return;
   }

   auto peerAccount = wallet_->getMetaAccount(MetaAccount_AuthPeers);
   auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
      wallet_->getDbName());
   shared_ptr<IO::DBIfaceTransaction> sharedTx(move(uniqueTx));
   auto index = AuthPeerAssetConversion::addRootPeer(
      peerAccount.get(), key, description, sharedTx);

   auto descPair = make_pair(description, index);
   peerRootKeys_.insert(make_pair(key, descPair));
}

////////////////////////////////////////////////////////////////////////////////
void AuthorizedPeers::erasePeerRootKey(const SecureBinaryData& key)
{
   auto iter = peerRootKeys_.find(key);
   if (iter == peerRootKeys_.end())
      return;

   if (wallet_ != nullptr)
   {
      //update wallet to reflect erasure
      auto metaAccount = wallet_->getMetaAccount(MetaAccount_AuthPeers);
      metaAccount->eraseMetaDataByIndex(iter->second.second);

      //update on disk
      auto uniqueTx = wallet_->getIface()->beginWriteTransaction(
         wallet_->getDbName());
      shared_ptr<IO::DBIfaceTransaction> sharedTx(move(uniqueTx));
      metaAccount->updateOnDisk(sharedTx);
   }

   peerRootKeys_.erase(iter);
}

////////////////////////////////////////////////////////////////////////////////
void AuthorizedPeers::changeControlPassphrase(const string& path)
{
   //get a terminal prompt lambda
   auto promptPtr = TerminalPassphrasePrompt::getLambda("peers db");
   
   //load the wallet
   auto wlt = AssetWallet::loadMainWalletFromFile(path, promptPtr);

   //change passphrase lambda
   auto changeLbd = [&promptPtr](void)->SecureBinaryData
   {      
      return promptPtr({ BinaryData::fromString("change-pass") });
   };

   //change the passphrase
   wlt->changeControlPassphrase(changeLbd, promptPtr);
}

////////////////////////////////////////////////////////////////////////////////
AuthPeersLambdas AuthorizedPeers::getAuthPeersLambdas(
   shared_ptr<AuthorizedPeers> authPeers)
{
   auto getMap = [authPeers](void)->const map<string, btc_pubkey>&
   {
      return authPeers->getPeerNameMap();
   };

   auto getPrivKey = [authPeers](
      const BinaryDataRef& pubkey)->const SecureBinaryData&
   {
      return authPeers->getPrivateKey(pubkey);
   };

   auto getAuthSet = [authPeers](void)->const set<SecureBinaryData>&
   {
      return authPeers->getPublicKeySet();
   };

   return AuthPeersLambdas(getMap, getPrivKey, getAuthSet);
}
