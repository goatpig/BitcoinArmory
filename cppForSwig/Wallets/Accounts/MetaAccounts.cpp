////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "MetaAccounts.h"
#include "../Assets.h"
#include "../EncryptedDB.h"
#include "../WalletFileInterface.h"

using namespace Armory::Assets;
using namespace Armory::Accounts;
using namespace Armory::Wallets;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// MetaDataAccount
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void MetaDataAccount::make_new(MetaAccountType type)
{
   type_ = type;
   switch (type_)
   {
      case MetaAccountType::Comments:
      {
         ID_ = WRITE_UINT32_BE(META_ACCOUNT_COMMENTS);
         break;
      }

      case MetaAccountType::Peers:
      {
         ID_ = WRITE_UINT32_BE(META_ACCOUNT_AUTHPEER);
         break;
      }

      default:
         throw AccountException("unexpected meta account type");
   }
}

////////////////////////////////////////////////////////////////////////////////
void MetaDataAccount::commit(
   std::unique_ptr<IO::DBIfaceTransaction> txPtr) const
{
   ReentrantLock lock(this);

   BinaryWriter bwKey;
   bwKey.put_uint8_t(META_ACCOUNT_PREFIX);
   bwKey.put_BinaryData(ID_);

   BinaryWriter bwData;
   bwData.put_var_int(4);
   bwData.put_uint32_t((uint32_t)type_);

   //commit assets
   std::shared_ptr<IO::DBIfaceTransaction> sharedTx(std::move(txPtr));
   for (auto& asset : assets_) {
      writeAssetToDisk(sharedTx, asset.second);
   }

   //commit serialized account data
   sharedTx->insert(bwKey.getData(), bwData.getData());
}

////////////////////////////////////////////////////////////////////////////////
bool MetaDataAccount::writeAssetToDisk(
   std::shared_ptr<IO::DBIfaceTransaction> txPtr,
   std::shared_ptr<MetaData> assetPtr) const
{
   if (!assetPtr->needsCommit()) {
      return true;
   }
   assetPtr->needsCommit_ = false;

   const auto& key = assetPtr->getDbKey();
   const auto& data = assetPtr->serialize();
   if (!data.empty()) {
      txPtr->insert(key, data);
      return true;
   } else {
      txPtr->erase(key);
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
void MetaDataAccount::updateOnDisk(
   std::shared_ptr<IO::DBIfaceTransaction> txPtr)
{
   ReentrantLock lock(this);

   bool needsCommit = false;
   for (const auto& asset : assets_) {
      needsCommit |= asset.second->needsCommit();
   }
   if (!needsCommit) {
      return;
   }
   auto iter = assets_.begin();
   while (iter != assets_.end()) {
      if (writeAssetToDisk(txPtr, iter->second)) {
         ++iter;
         continue;
      }
      assets_.erase(iter++);
   }
}

////////////////////////////////////////////////////////////////////////////////
void MetaDataAccount::reset()
{
   type_ = MetaAccountType::Unset;
   ID_.clear();
   assets_.clear();
}

////////////////////////////////////////////////////////////////////////////////
void MetaDataAccount::readFromDisk(
   std::shared_ptr<IO::WalletDBInterface> iface, const BinaryData& key)
{
   //sanity checks
   if (iface == nullptr || dbName_.empty()) {
      throw AccountException("invalid db pointers");
   }
   if (key.getSize() != 5) {
      throw AccountException("invalid key size");
   }
   if (key.getPtr()[0] != META_ACCOUNT_PREFIX) {
      throw AccountException("unexpected prefix for AssetAccount key");
   }

   LMDB::DataRef carKey(key.getSize(), key.getCharPtr());
   auto tx = iface->beginReadTransaction(dbName_);
   auto diskDataRef = tx->getDataRef(key);
   BinaryRefReader brr(diskDataRef);

   //wipe object prior to loading from disk
   reset();

   //set ID
   ID_ = key.getSliceCopy(1, 4);

   //getType
   brr.get_var_int();
   type_ = (MetaAccountType)brr.get_uint32_t();

   std::set<uint8_t> prefixes;
   switch (type_)
   {
      case MetaAccountType::Comments:
      {
         prefixes.emplace(METADATA_COMMENTS_PREFIX);
         break;
      }

      case MetaAccountType::Peers:
      {
         prefixes = {
            METADATA_AUTHPEER_PREFIX,
            METADATA_PEERROOT_PREFIX,
            METADATA_ROOTSIG_PREFIX,
            METADATA_PEERMASTER_PREFIX
         };
         break;
      }

      default:
         throw AccountException("unexpected meta account type");
   }

   //get assets
   for (const uint8_t prefix : prefixes) {
      BinaryWriter bwAssetKey;
      bwAssetKey.put_uint8_t(prefix);
      bwAssetKey.put_BinaryData(ID_);
      const auto& assetDbKey = bwAssetKey.getData();

      auto dbIter = tx->getIterator();
      dbIter->seek(assetDbKey);

      while (dbIter->isValid()) {
         const auto& key = dbIter->key();
         const auto& data = dbIter->value();

         //check key isnt prefix
         if (key == assetDbKey) {
            continue;
         }

         //check key starts with prefix
         if (!key.startsWith(assetDbKey)) {
            break;
         }

         //deser asset
         try {
            auto assetPtr = MetaData::deserialize(key, data);
            if (lastAssetId_ != UINT32_MAX) {
               lastAssetId_ = std::max(assetPtr->index_, lastAssetId_);
            } else {
               lastAssetId_ = assetPtr->index_;
            }
            assets_.emplace(assetPtr->index_, assetPtr);
         } catch (const std::exception&) {}
         dbIter->advance();
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<MetaData> MetaDataAccount::getMetaDataByIndex(uint32_t id) const
{
   auto iter = assets_.find(id);
   if (iter == assets_.end()) {
      throw AccountException("invalid asset index");
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
uint32_t MetaDataAccount::getNextAssetId()
{
   return ++lastAssetId_;
}

////
void MetaDataAccount::addAsset(std::shared_ptr<MetaData> asset)
{
   if (asset == nullptr) {
      throw AccountException("cannot add null asset");
   }
   assets_.emplace(asset->getIndex(), std::move(asset));
}

////
const std::map<uint32_t, std::shared_ptr<MetaData>>&
MetaDataAccount::getAssetMap() const
{
   return assets_;
}

////
MetaAccountType MetaDataAccount::getType() const
{
   return type_;
}

////
const BinaryData& MetaDataAccount::getID() const
{
   return ID_;
}

////////////////////////////////////////////////////////////////////////////////
void MetaDataAccount::eraseMetaDataByIndex(uint32_t id)
{
   auto iter = assets_.find(id);
   if (iter == assets_.end()) {
      return;
   }
   iter->second->clear();
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<MetaDataAccount> MetaDataAccount::copy(
   const std::string& dbName) const
{
   auto copyPtr = std::make_shared<MetaDataAccount>(dbName);
   copyPtr->type_ = type_;
   copyPtr->ID_ = ID_;

   for (auto& assetPair : assets_) {
      auto assetCopy = assetPair.second->copy();
      assetCopy->flagForCommit();
      copyPtr->assets_.insert(make_pair(assetPair.first, assetCopy));
   }
   return copyPtr;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// PeerAccountHelper
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
PeerAssetMap PeerAccountHelper::getAssetMap(
   const MetaDataAccount* account)
{
   if (account == nullptr || account->getType() != MetaAccountType::Peers) {
      throw AccountException("invalid metadata account ptr");
   }
   ReentrantLock lock(account);

   PeerAssetMap result;
   const auto& assets = account->getAssetMap();
   for (const auto& asset : assets) {
      switch (asset.second->type())
      {
         case MetaType::Peer:
         {
            auto assetPeer = std::dynamic_pointer_cast<PeerPublicData>(
               asset.second);
            if (assetPeer == nullptr) {
               continue;
            }

            const auto& names = assetPeer->getNames();
            const auto& pubKey = assetPeer->getPublicKey();
            const auto& label = assetPeer->getLabel();
            auto& destination = assetPeer->oneWay() ?
               result.nameKeyMapOneWay : result.nameKeyMapTwoWay;
            for (auto& name : names) {
               destination.emplace(name, std::make_pair(&pubKey, &label));
            }
            break;
         }

         case MetaType::PeerRootKey:
         {
            auto assetRoot = std::dynamic_pointer_cast<PeerRootKey>(
               asset.second);
            if (assetRoot == nullptr) {
               continue;
            }
            auto descPair = make_pair(assetRoot->getDescription(), asset.first);
            result.peerRootKeys.emplace(assetRoot->getKey(), descPair);
            break;
         }

         case MetaType::PeerRootSig:
         {
            auto assetSig = std::dynamic_pointer_cast<PeerRootSignature>(
               asset.second);
            if (assetSig == nullptr) {
               continue;
            }
            result.rootSignature = std::make_pair(
               assetSig->getKey(), assetSig->getSig());
            break;
         }

         case MetaType::PeerMasterKey:
         {
            auto assetMasterKey = std::dynamic_pointer_cast<PeerMasterKey>(
               asset.second);
            if (assetMasterKey == nullptr) {
               continue;
            }
            result.masterKey = assetMasterKey->getKey();
            break;
         }

         default:
            continue;
      }
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
std::map<SecureBinaryData, std::set<uint32_t>>
PeerAccountHelper::getKeyIndexMap(
   const MetaDataAccount* account, bool oneWay)
{
   if (account == nullptr || account->getType() != MetaAccountType::Peers) {
      throw AccountException("invalid metadata account ptr");
   }
   ReentrantLock lock(account);

   std::map<SecureBinaryData, std::set<uint32_t>> result;
   const auto& assets = account->getAssetMap();
   for (const auto& asset : assets) {
      auto assetPeer = std::dynamic_pointer_cast<PeerPublicData>(asset.second);
      if (assetPeer == nullptr) {
         continue;
      }

      if (assetPeer->oneWay() != oneWay) {
         continue;
      }
      const auto& pubKey = assetPeer->getPublicKey();
      auto iter = result.find(pubKey);
      if (iter == result.end()) {
         auto insertIter = result.emplace(pubKey, std::set<uint32_t>{});
         iter = insertIter.first;
      }
      iter->second.insert(asset.first);
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
int PeerAccountHelper::addAsset(
   MetaDataAccount* account, const SecureBinaryData& pubkey,
   const std::vector<std::string>& names,
   const std::string& label, bool oneWay,
   std::shared_ptr<IO::DBIfaceTransaction> txPtr)
{
   ReentrantLock lock(account);

   if (account == nullptr || account->getType() != MetaAccountType::Peers) {
      throw AccountException("invalid metadata account ptr");
   }
   uint32_t index = account->getNextAssetId();

   auto metaObject = std::make_shared<PeerPublicData>(account->getID(), index);
   metaObject->setPublicKey(pubkey);
   for (const auto& name : names) {
      metaObject->addName(name);
   }
   metaObject->setLabel(label);
   metaObject->setOneWay(oneWay);

   metaObject->flagForCommit();
   account->addAsset(metaObject);
   account->updateOnDisk(txPtr);
   return index;
}

////////////////////////////////////////////////////////////////////////////////
void PeerAccountHelper::addRootSignature(MetaDataAccount* account,
   const SecureBinaryData& key, const SecureBinaryData& sig,
   std::shared_ptr<IO::DBIfaceTransaction> txPtr)
{
   ReentrantLock lock(account);

   if (account == nullptr || account->getType() != MetaAccountType::Peers) {
      throw AccountException("invalid metadata account ptr");
   }
   const auto& accountID = account->getID();
   unsigned index = account->getNextAssetId();

   auto metaObject = std::make_shared<PeerRootSignature>(accountID, index);
   metaObject->set(key, sig);
   metaObject->flagForCommit();
   account->addAsset(metaObject);
   account->updateOnDisk(txPtr);
}

////////////////////////////////////////////////////////////////////////////////
unsigned PeerAccountHelper::addRootPeer(MetaDataAccount* account,
   const SecureBinaryData& key, const std::string& desc,
   std::shared_ptr<IO::DBIfaceTransaction> txPtr)
{
   ReentrantLock lock(account);

   if (account == nullptr || account->getType() != MetaAccountType::Peers) {
      throw AccountException("invalid metadata account ptr");
   }
   auto& accountID = account->getID();
   unsigned index = account->getNextAssetId();

   auto metaObject = std::make_shared<PeerRootKey>(accountID, index);
   metaObject->set(desc, key);
   metaObject->flagForCommit();
   account->addAsset(metaObject);
   account->updateOnDisk(txPtr);
   return index;
}

////////////////////////////////////////////////////////////////////////////////
void PeerAccountHelper::addMasterKey(MetaDataAccount* account,
   const SecureBinaryData& key, std::shared_ptr<IO::DBIfaceTransaction> txPtr)
{
   ReentrantLock lock(account);

   if (account == nullptr || account->getType() != MetaAccountType::Peers) {
      throw AccountException("invalid metadata account ptr");
   }
   clearMasterKeyAssets(account);

   const auto& accountID = account->getID();
   uint32_t index = account->getNextAssetId();

   auto metaObject = std::make_shared<PeerMasterKey>(accountID, index);
   metaObject->set(key);
   metaObject->flagForCommit();
   account->addAsset(metaObject);
   account->updateOnDisk(txPtr);
}

////////
void PeerAccountHelper::clearMasterKeyAssets(MetaDataAccount* account)
{
   ReentrantLock lock(account);

   if (account == nullptr || account->getType() != MetaAccountType::Peers) {
      throw AccountException("invalid metadata account ptr");
   }

   auto assets = account->getAssetMap();
   for (const auto& assetPair : assets) {
      if (assetPair.second->type() == MetaType::PeerMasterKey) {
         assetPair.second->clear();
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// CommentAccountHelper
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<CommentData> CommentAccountHelper::getByKey(
   MetaDataAccount* account, const BinaryData& key)
{
   ReentrantLock lock(account);

   if (account == nullptr || account->getType() != MetaAccountType::Comments) {
      throw AccountException("invalid metadata account ptr");
   }

   const auto& assets = account->getAssetMap();
   for (const auto& asset : assets) {
      auto objPtr = std::dynamic_pointer_cast<CommentData>(asset.second);
      if (objPtr == nullptr) {
         continue;
      }
      if (objPtr->getKey() == key) {
         return objPtr;
      }
   }
   return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
int CommentAccountHelper::setAsset(MetaDataAccount* account,
   const BinaryData& key, const std::string& comment,
   std::shared_ptr<IO::DBIfaceTransaction> txPtr)
{
   if (comment.empty()) {
      return INT32_MIN;
   }
   ReentrantLock lock(account);

   if (account == nullptr || account->getType() != MetaAccountType::Comments) {
      throw AccountException("invalid metadata account ptr");
   }

   auto metaObject = getByKey(account, key);
   if (metaObject == nullptr) {
      const auto& accountID = account->getID();
      auto index = account->getNextAssetId();
      metaObject = std::make_shared<CommentData>(accountID, index);
      metaObject->setKey(key);
      account->addAsset(metaObject);
   }

   metaObject->setValue(comment);
   metaObject->flagForCommit();
   account->updateOnDisk(txPtr);
   return metaObject->getIndex();
}

////////////////////////////////////////////////////////////////////////////////
int CommentAccountHelper::deleteAsset(
   MetaDataAccount* account, const BinaryData& key,
   std::shared_ptr<IO::DBIfaceTransaction> txPtr)
{
   auto metaObject = getByKey(account, key);
   if (metaObject == nullptr) {
      return -1;
   }
   metaObject->clear();
   account->updateOnDisk(txPtr);
   return metaObject->getIndex();
}

////////////////////////////////////////////////////////////////////////////////
std::map<BinaryData, std::string> CommentAccountHelper::getCommentMap(
   MetaDataAccount* account)
{
   ReentrantLock lock(account);

   if (account == nullptr || account->getType() != MetaAccountType::Comments) {
      throw AccountException("invalid metadata account ptr");
   }

   std::map<BinaryData, std::string> result;
   const auto& assets = account->getAssetMap();
   for (const auto& asset : assets) {
      auto objPtr = std::dynamic_pointer_cast<CommentData>(asset.second);
      if (objPtr == nullptr) {
         continue;
      }
      result.emplace(objPtr->getKey(), objPtr->getValue());
   }
   return result;
}
