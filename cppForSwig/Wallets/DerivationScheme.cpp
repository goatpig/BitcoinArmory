////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "DerivationScheme.h"
#include "Utils/ReentrantLock.h"
#include "Utils/Cryptography.h"
#include "EncryptedDB.h"
#include "DecryptedDataContainer.h"
#include "BIP32_Node.h"

#define DERSCHEME_LEGACY_VERSION 0x00000001
#define DERSCHEME_BIP32_VERSION  0x00000001
#define DERSCHEME_SALTED_VERSION 0x00000001
#define DERSCHEME_ECDH_VERSION   0x00000001

using namespace Armory::Assets;
using namespace Armory::Wallets;

////////////////////////////////////////////////////////////////////////////////
DerivationSchemeException::DerivationSchemeException(const std::string& msg) :
   std::runtime_error(msg)
{}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// DerivationScheme
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DerivationScheme::DerivationScheme(DerivationSchemeType type) :
   type_(type)
{}

////////////////////////////////////////////////////////////////////////////////
DerivationScheme::~DerivationScheme()
{}

////////////////////////////////////////////////////////////////////////////////
DerivationSchemeType DerivationScheme::getType() const
{
   return type_;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<DerivationScheme> DerivationScheme::deserialize(BinaryDataRef data)
{
   BinaryRefReader brr(data);

   //version
   auto version = brr.get_uint32_t();

   //get derivation scheme type
   auto schemeType = brr.get_uint8_t();

   std::shared_ptr<DerivationScheme> derScheme;
   switch (schemeType)
   {
      case DERIVATIONSCHEME_LEGACY:
      {
         switch (version)
         {
            case 0x00000001:
            {
               //get chaincode;
               auto len = brr.get_var_int();
               SecureBinaryData chainCode{brr.get_BinaryDataRef(len)};
               derScheme = std::make_shared<DerivationScheme_ArmoryLegacy>(
                  chainCode);
               break;
            }

            default:
               throw DerivationSchemeException("unsupported legacy scheme version");
         }

         break;
      }

      case DERIVATIONSCHEME_BIP32:
      {
         switch (version)
         {
            case 0x00000001:
            {
               //chaincode;
               auto len = brr.get_var_int();
               SecureBinaryData chainCode{brr.get_BinaryDataRef(len)};

               //bip32 node meta data
               auto depth = brr.get_uint32_t();
               auto leafID = brr.get_uint32_t();

               //instantiate object
               derScheme = std::make_shared<DerivationScheme_BIP32>(
                  chainCode, depth, leafID);
               break;
            }

            default:
               throw DerivationSchemeException("unsupported bip32 scheme version");
         }

         break;
      }

      case DERIVATIONSCHEME_BIP32_SALTED:
      {
         switch (version)
         {
            case 0x00000001:
            {
               //chaincode;
               auto len = brr.get_var_int();
               SecureBinaryData chainCode{brr.get_BinaryDataRef(len)};

               //bip32 node meta data
               auto depth = brr.get_uint32_t();
               auto leafID = brr.get_uint32_t();

               //salt
               len = brr.get_var_int();
               SecureBinaryData salt{brr.get_BinaryDataRef(len)};

               //instantiate object
               derScheme = std::make_shared<DerivationScheme_BIP32_Salted>(
                  salt, chainCode, depth, leafID);
               break;
            }

            default:
               throw DerivationSchemeException("unsupported salted scheme version");
         }

         break;
      }

      case DERIVATIONSCHEME_BIP32_ECDH:
      {
         switch (version)
         {
            case 0x00000001:
            {
               //id
               auto len = brr.get_var_int();
               auto id = brr.get_BinaryData(len);
               derScheme = std::make_shared<DerivationScheme_ECDH>(id);
               break;
            }

            default:
               throw DerivationSchemeException("unsupported ecdh scheme version");
         }

         break;
      }

      default:
         throw DerivationSchemeException("unsupported derivation scheme");
   }
   return derScheme;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// DerivationScheme_ArmoryLegacy
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DerivationScheme_ArmoryLegacy::DerivationScheme_ArmoryLegacy(
   SecureBinaryData& chainCode) :
   DerivationScheme(DerivationSchemeType::ArmoryLegacy),
   chainCode_(std::move(chainCode))
{}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& DerivationScheme_ArmoryLegacy::getChaincode() const
{
   return chainCode_;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AssetEntry_Single>
DerivationScheme_ArmoryLegacy::computeNextPublicEntry(
   const SecureBinaryData& pubKey, AssetId id)
{
   auto nextPubkey = Cryptography::ECDSA::computeChainedPublicKey(
      pubKey, chainCode_);
   return std::make_shared<AssetEntry_Single>(id, nextPubkey, nullptr);
}

////////////////////////////////////////////////////////////////////////////////
std::vector<std::shared_ptr<AssetEntry>>
DerivationScheme_ArmoryLegacy::extendPublicChain(
   std::shared_ptr<AssetEntry> firstAsset, int32_t, int32_t end,
   const std::function<void(int)>& progressCallback)
{
   auto nextAsset = [this](
      std::shared_ptr<AssetEntry> assetPtr)->std::shared_ptr<AssetEntry>
   {
      auto assetSingle = std::dynamic_pointer_cast<AssetEntry_Single>(assetPtr);
      auto pubkey = assetSingle->getPubKey();
      const auto& pubkeyData = pubkey->getUncompressedKey();

      return computeNextPublicEntry(pubkeyData, AssetId(
         assetSingle->getAccountID(), assetSingle->getIndex() + 1));
   };

   std::vector<std::shared_ptr<AssetEntry>> assetVec;
   assetVec.reserve(end - firstAsset->getIndex());
   auto currentAsset = firstAsset;

   for (int32_t i = firstAsset->getIndex(); i < end; i++) {
      currentAsset = nextAsset(currentAsset);
      assetVec.emplace_back(currentAsset);

      if (progressCallback) {
         progressCallback(i-firstAsset->getIndex()+1);
      }
   }
   return assetVec;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AssetEntry_Single>
DerivationScheme_ArmoryLegacy::computeNextPrivateEntry(
   std::shared_ptr<Encryption::DecryptedDataContainer> ddc,
   const SecureBinaryData& privKeyData,
   std::unique_ptr<Encryption::Cipher> cipher,
   AssetId id)
{
   //chain the private key
   auto nextPrivkeySBD = Cryptography::ECDSA::computeChainedPrivateKey(
      privKeyData, chainCode_);

   //compute its pubkey
   auto nextPubkey = Cryptography::ECDSA::computePublicKey(nextPrivkeySBD);

   //encrypt the new privkey
   auto encryptedNextPrivKey = ddc->encryptData(
      cipher.get(), nextPrivkeySBD);

   //clear the unencrypted privkey object
   nextPrivkeySBD.clear();

   //instantiate new encrypted key object
   auto cipherData = std::make_unique<Encryption::CipherData>(
      encryptedNextPrivKey, std::move(cipher));
   auto nextPrivKey = std::make_shared<Asset_PrivateKey>(
      id, std::move(cipherData));

   //instantiate and return new asset entry
   return std::make_shared<AssetEntry_Single>(id, nextPubkey, nextPrivKey);
}

////////////////////////////////////////////////////////////////////////////////
std::vector<std::shared_ptr<AssetEntry>>
DerivationScheme_ArmoryLegacy::extendPrivateChain(
   std::shared_ptr<Encryption::DecryptedDataContainer> ddc,
   std::shared_ptr<AssetEntry> firstAsset, int32_t, int32_t end)
{
   //throws if the wallet is locked or the asset is missing its private key
   if (ddc == nullptr || firstAsset == nullptr) {
      LOGERR << "missing asset, cannot extent private chain";
      throw AssetUnavailableException();
   }

   auto nextAsset = [this, ddc](
      std::shared_ptr<AssetEntry> assetPtr)->std::shared_ptr<AssetEntry>
   {
      //sanity checks
      auto assetSingle = std::dynamic_pointer_cast<AssetEntry_Single>(assetPtr);
      auto privkey = assetSingle->getPrivKey();
      if (privkey == nullptr) {
         throw AssetUnavailableException();
      }
      auto& privkeyData = ddc->getClearTextAssetData(privkey);

      return computeNextPrivateEntry(
         ddc, privkeyData,
         std::move(privkey->getCipherDataPtr()->cipher->getCopy()),
         AssetId(assetSingle->getAccountID(), assetSingle->getIndex() + 1)
      );
   };

   ReentrantLock lock(ddc.get());
   std::vector<std::shared_ptr<AssetEntry>> assetVec;
   assetVec.reserve(end - firstAsset->getIndex());

   auto currentAsset = firstAsset;
   for (int32_t i = firstAsset->getIndex(); i < end; i++) {
      currentAsset = nextAsset(currentAsset);
      assetVec.emplace_back(currentAsset);
   }
   return assetVec;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData DerivationScheme_ArmoryLegacy::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(DERSCHEME_LEGACY_VERSION);
   bw.put_uint8_t(DERIVATIONSCHEME_LEGACY);
   bw.put_var_int(chainCode_.getSize());
   bw.put_BinaryData(chainCode_);

   BinaryWriter final;
   final.put_var_int(bw.getSize());
   final.put_BinaryData(bw.getData());

   return final.getData();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// DerivationScheme_BIP32
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DerivationScheme_BIP32::DerivationScheme_BIP32(DerivationSchemeType type,
   SecureBinaryData& chainCode, unsigned depth, unsigned leafId) :
   DerivationScheme(type),
   chainCode_(std::move(chainCode)),
   depth_(depth), leafId_(leafId)
{}

////////////////////////////////////////////////////////////////////////////////
DerivationScheme_BIP32::DerivationScheme_BIP32(SecureBinaryData& chainCode,
   unsigned depth, unsigned leafId) :
   DerivationScheme(DerivationSchemeType::BIP32),
   chainCode_(std::move(chainCode)),
   depth_(depth), leafId_(leafId)
{}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& DerivationScheme_BIP32::getChaincode() const
{
   return chainCode_;
}

////
unsigned DerivationScheme_BIP32::getDepth() const
{
   return depth_;
}

////
unsigned DerivationScheme_BIP32::getLeafId() const
{
   return leafId_;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AssetEntry_Single>
DerivationScheme_BIP32::computeNextPrivateEntry(
   std::shared_ptr<Encryption::DecryptedDataContainer> ddc,
   const SecureBinaryData& privKeyData,
   std::unique_ptr<Encryption::Cipher> cipher,
   AssetId id)
{
   auto index = id.getAssetKey();

   //derScheme only allows for soft derivation
   if (index > 0x7FFFFFFF) {
      throw DerivationSchemeException("illegal: hard derivation");
   }
   BIP32_Node node;
   node.initFromPrivateKey(depth_, leafId_, 0, privKeyData, chainCode_);
   node.derivePrivate(index);

   //encrypt the new privkey
   auto encryptedNextPrivKey = ddc->encryptData(
      cipher.get(), node.getPrivateKey());

   //instantiate new encrypted key object
   auto cipherData = std::make_unique<Encryption::CipherData>(
      encryptedNextPrivKey, std::move(cipher));
   auto nextPrivKey = std::make_shared<Asset_PrivateKey>(
      id, std::move(cipherData));

   //instantiate and return new asset entry
   auto nextPubkey = node.movePublicKey();
   return std::make_shared<AssetEntry_Single>(id, nextPubkey, nextPrivKey);
}

////////////////////////////////////////////////////////////////////////////////
std::vector<std::shared_ptr<AssetEntry>>
DerivationScheme_BIP32::extendPrivateChain(
   std::shared_ptr<Encryption::DecryptedDataContainer> ddc,
   std::shared_ptr<AssetEntry> rootAsset,
   int32_t start, int32_t end)
{
   //throws if the wallet is locked or the asset is missing its private key
   auto rootAsset_single = std::dynamic_pointer_cast<AssetEntry_Single>(rootAsset);
   if (rootAsset_single == nullptr) {
      throw DerivationSchemeException("invalid root asset object");
   }
   if (ddc == nullptr) {
      throw AssetUnavailableException();
   }

   const auto& account_id = rootAsset_single->getAccountID();
   auto nextAsset = [this, ddc, rootAsset_single, &account_id](
      int32_t derivationIndex)->std::shared_ptr<AssetEntry>
   {
      //sanity checks
      auto privkey = rootAsset_single->getPrivKey();
      if (privkey == nullptr) {
         throw AssetUnavailableException();
      }
      auto& privkeyData = ddc->getClearTextAssetData(privkey);

      return computeNextPrivateEntry(
         ddc, privkeyData,
         std::move(privkey->getCipherDataPtr()->cipher->getCopy()),
         AssetId(account_id, derivationIndex));
   };

   ReentrantLock lock(ddc.get());
   std::vector<std::shared_ptr<AssetEntry>> assetVec;
   assetVec.reserve(end-start);

   for (int32_t i = start; i < end; i++) {
      auto newAsset = nextAsset(i+1);
      assetVec.emplace_back(newAsset);
   }
   return assetVec;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AssetEntry_Single>
DerivationScheme_BIP32::computeNextPublicEntry(
   const SecureBinaryData& pubKey, AssetId id)
{
   auto index = id.getAssetKey();

   //derScheme only allows for soft derivation
   if (index > 0x7FFFFFFF) {
      throw DerivationSchemeException("illegal: hard derivation");
   }
   BIP32_Node node;
   node.initFromPublicKey(depth_, leafId_, 0, pubKey, chainCode_);
   node.derivePublic(index);

   auto nextPubKey = node.movePublicKey();
   return std::make_shared<AssetEntry_Single>(id, nextPubKey, nullptr);
}

////////////////////////////////////////////////////////////////////////////////
std::vector<std::shared_ptr<AssetEntry>>
DerivationScheme_BIP32::extendPublicChain(
   std::shared_ptr<AssetEntry> rootAsset, int32_t start, int32_t end,
   const std::function<void(int)>& progressCallback)
{
   auto rootSingle = std::dynamic_pointer_cast<AssetEntry_Single>(rootAsset);
   auto nextAsset = [this, rootSingle](
      int32_t derivationIndex)->std::shared_ptr<AssetEntry>
   {
      //get pubkey
      auto pubkey = rootSingle->getPubKey();
      auto& pubkeyData = pubkey->getCompressedKey();

      return computeNextPublicEntry(pubkeyData,
         AssetId(rootSingle->getAccountID(), derivationIndex));
   };

   std::vector<std::shared_ptr<AssetEntry>> assetVec;
   assetVec.reserve(end - start);
   for (int32_t i = start; i < end; i++) {
      auto newAsset = nextAsset(i+1);
      assetVec.emplace_back(std::move(newAsset));

      if (progressCallback) {
         progressCallback(i-start+1);
      }
   }

   return assetVec;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData DerivationScheme_BIP32::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(DERSCHEME_BIP32_VERSION);
   bw.put_uint8_t(DERIVATIONSCHEME_BIP32);
   bw.put_var_int(chainCode_.getSize());
   bw.put_BinaryData(chainCode_);
   bw.put_uint32_t(depth_);
   bw.put_uint32_t(leafId_);

   BinaryWriter final;
   final.put_var_int(bw.getSize());
   final.put_BinaryData(bw.getData());

   return final.getData();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// DerivationScheme_BIP32_Salted
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DerivationScheme_BIP32_Salted::DerivationScheme_BIP32_Salted(
   SecureBinaryData& salt, SecureBinaryData& chainCode,
   unsigned depth, unsigned leafId) :
   DerivationScheme_BIP32(
      DerivationSchemeType::BIP32_Salted, chainCode, depth, leafId),
   salt_(std::move(salt))
{}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& DerivationScheme_BIP32_Salted::getSalt() const
{
   return salt_;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AssetEntry_Single>
DerivationScheme_BIP32_Salted::computeNextPrivateEntry(
   std::shared_ptr<Encryption::DecryptedDataContainer> ddc,
   const SecureBinaryData& privKey,
   std::unique_ptr<Encryption::Cipher> cipher,
   AssetId id)
{

   //derScheme only allows for soft derivation
   auto index = id.getAssetKey();
   if (index > 0x7FFFFFFF) {
      throw DerivationSchemeException("illegal: hard derivation");
   }

   BIP32_Node node;
   node.initFromPrivateKey(
      getDepth(), getLeafId(), 0, privKey, getChaincode());
   node.derivePrivate(index);

   //salt the key
   auto saltedPrivKey = Cryptography::ECDSA::privKeyScalarMultiply(
      node.getPrivateKey(), salt_);

   //compute salted pubkey
   auto saltedPubKey = Cryptography::ECDSA::computePublicKey(saltedPrivKey, true);

   //encrypt the new privkey
   auto newCipher = cipher->getCopy(); //copying a cypher cycles the IV
   auto encryptedNextPrivKey = ddc->encryptData(
      newCipher.get(), saltedPrivKey);

   //instantiate encrypted salted privkey object
   auto cipherData = std::make_unique<Encryption::CipherData>(
      encryptedNextPrivKey, std::move(newCipher));
   auto nextPrivKey = std::make_shared<Asset_PrivateKey>(
      id, std::move(cipherData));

   //instantiate and return new asset entry
   return std::make_shared<AssetEntry_Single>(id, saltedPubKey, nextPrivKey);
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AssetEntry_Single>
DerivationScheme_BIP32_Salted::computeNextPublicEntry(
   const SecureBinaryData& pubKey, AssetId id)
{
   //derScheme only allows for soft derivation
   auto index = id.getAssetKey();
   if (index > 0x7FFFFFFF) {
      throw DerivationSchemeException("illegal: hard derivation");
   }

   //compute pub key
   BIP32_Node node;
   node.initFromPublicKey(getDepth(), getLeafId(), 0, pubKey, getChaincode());
   node.derivePublic(index);
   auto nextPubkey = node.movePublicKey();

   //salt it
   auto saltedPubkey = Cryptography::ECDSA::pubKeyScalarMultiply(
      nextPubkey, salt_);
   return std::make_shared<AssetEntry_Single>(id, saltedPubkey, nullptr);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData DerivationScheme_BIP32_Salted::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(DERSCHEME_SALTED_VERSION);
   bw.put_uint8_t(DERIVATIONSCHEME_BIP32_SALTED);
   bw.put_var_int(getChaincode().getSize());
   bw.put_BinaryData(getChaincode());
   bw.put_uint32_t(getDepth());
   bw.put_uint32_t(getLeafId());

   bw.put_var_int(salt_.getSize());
   bw.put_BinaryData(salt_);

   BinaryWriter final;
   final.put_var_int(bw.getSize());
   final.put_BinaryData(bw.getData());

   return final.getData();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// DerivationScheme_ECDH
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DerivationScheme_ECDH::DerivationScheme_ECDH() :
   DerivationScheme(DerivationSchemeType::ECDH),
   id_(Cryptography::PRNG::fortuna.generateRandom(8))
{}

DerivationScheme_ECDH::DerivationScheme_ECDH(const BinaryData& id) :
   DerivationScheme(DerivationSchemeType::ECDH), id_(id)
{}

////////////////////////////////////////////////////////////////////////////////
const SecureBinaryData& DerivationScheme_ECDH::getChaincode() const
{
   throw DerivationSchemeException("no chaincode for ECDH derivation scheme");
}

////////////////////////////////////////////////////////////////////////////////
BinaryData DerivationScheme_ECDH::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(DERSCHEME_ECDH_VERSION);
   bw.put_uint8_t(DERIVATIONSCHEME_BIP32_ECDH);

   //id
   bw.put_var_int(id_.getSize());
   bw.put_BinaryData(id_);
   
   //length wrapper
   BinaryWriter final;
   final.put_var_int(bw.getSize());
   final.put_BinaryData(bw.getData());

   return final.getData();
}

////////////////////////////////////////////////////////////////////////////////
AssetKeyType DerivationScheme_ECDH::addSalt(const SecureBinaryData& salt,
   std::shared_ptr<IO::DBIfaceTransaction> txPtr)
{
   if (salt.getSize() != 32) {
      throw DerivationSchemeException("salt is too small");
   }

   //return the salt id if it's already in there
   std::unique_lock<std::mutex> lock(saltMutex_);
   auto saltIter = saltMap_.find(salt);
   if (saltIter != saltMap_.end()) {
      return saltIter->second;
   }

   unsigned id = ++topSaltIndex_;
   auto insertIter = saltMap_.insert(std::make_pair(salt, id));
   if (!insertIter.second) {
      throw DerivationSchemeException("failed to insert salt");
   }

   //update on disk if we have a db tx
   if (txPtr != nullptr) {
      putSalt(id, salt, txPtr);
   }

   //return insert index
   return id;
}

////////////////////////////////////////////////////////////////////////////////
void DerivationScheme_ECDH::putSalt(AssetKeyType id,
   const SecureBinaryData& salt, std::shared_ptr<IO::DBIfaceTransaction> txPtr)
{
   //update on disk
   BinaryWriter bwKey;
   bwKey.put_uint8_t(ECDH_SALT_PREFIX);
   bwKey.put_BinaryData(id_);
   bwKey.put_uint32_t(id, BE);

   auto dataRef = txPtr->getDataRef(bwKey.getData());
   if (!dataRef.empty()) {
      //read the salt
      BinaryRefReader brr(dataRef);
      auto size = brr.get_var_int();
      auto saltRef = brr.get_BinaryDataRef(size);
      if (saltRef != salt.getRef()) {
         throw DerivationSchemeException(
            "trying to write a salt different from the one on disk");
      }

      //no point rewriting a salt to disk
      return;
   }

   BinaryWriter bwData;
   bwData.put_var_int(salt.getSize());
   bwData.put_BinaryData(salt);
   txPtr->insert(bwKey.getData(), bwData.getData());
}

////////////////////////////////////////////////////////////////////////////////
void DerivationScheme_ECDH::putAllSalts(
   std::shared_ptr<IO::DBIfaceTransaction> txPtr)
{
   //expects live read-write db tx
   for (auto& saltPair : saltMap_) {
      putSalt(saltPair.second, saltPair.first, txPtr);
   }
}

////////////////////////////////////////////////////////////////////////////////
void DerivationScheme_ECDH::getAllSalts(
   std::shared_ptr<IO::DBIfaceTransaction> txPtr)
{
   BinaryWriter bwKey;
   bwKey.put_uint8_t(ECDH_SALT_PREFIX);
   bwKey.put_BinaryData(id_);
   BinaryDataRef keyBdr = bwKey.getDataRef();

   auto dbIter = txPtr->getIterator();
   dbIter->seek(keyBdr);
   while (dbIter->isValid()) {
      auto key = dbIter->key();
      if (!key.startsWith(keyBdr) || key.getSize() != keyBdr.getSize() + 4) {
         break;
      }

      auto saltIdBdr = key.getSliceCopy(keyBdr.getSize(), 4);
      auto saltId = READ_UINT32_BE(saltIdBdr);

      auto value = dbIter->value();
      BinaryRefReader bdrData(value);
      auto len = bdrData.get_var_int();
      SecureBinaryData salt{bdrData.get_BinaryDataRef(len)};

      saltMap_.emplace(std::move(salt), saltId);
      dbIter->advance();
   }

   //sanity check
   std::set<unsigned> idSet;
   for (auto& saltPair : saltMap_) {
      auto insertIter = idSet.insert(saltPair.second);
      if (insertIter.second == false) {
         throw DerivationSchemeException("ECDH id collision!");
      }
   }

   if (idSet.empty()) {
      return;
   }

   //set top index
   auto idIter = idSet.rbegin();
   topSaltIndex_ = *idIter;
}

////////////////////////////////////////////////////////////////////////////////
std::vector<std::shared_ptr<AssetEntry>>
DerivationScheme_ECDH::extendPublicChain(
   std::shared_ptr<AssetEntry> root, int32_t start, int32_t end,
   const std::function<void(int)>& progressCallback)
{
   auto rootSingle = std::dynamic_pointer_cast<AssetEntry_Single>(root);
   if (rootSingle == nullptr) {
      throw DerivationSchemeException("unexpected root asset type");
   }

   auto nextAsset = [this, rootSingle](
      int32_t derivationIndex)->std::shared_ptr<AssetEntry>
   {
      //get pubkey
      auto pubkey = rootSingle->getPubKey();
      auto& pubkeyData = pubkey->getCompressedKey();

      return computeNextPublicEntry(pubkeyData,
         AssetId(rootSingle->getAccountID(), derivationIndex));
   };

   std::vector<std::shared_ptr<AssetEntry>> assetVec;
   for (int32_t i = start; i < end; i++) {
      auto newAsset = nextAsset(i+1);
      assetVec.emplace_back(std::move(newAsset));

      if (progressCallback) {
         progressCallback(i-start+1);
      }
   }
   return assetVec;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AssetEntry_Single> DerivationScheme_ECDH::computeNextPublicEntry(
   const SecureBinaryData& pubKey, AssetId id)
{
   if (pubKey.getSize() != 33) {
      throw DerivationSchemeException("unexpected pubkey size");
   }

   //get salt
   auto index = id.getAssetKey();
   auto saltIter = saltMap_.rbegin();
   while (saltIter != saltMap_.rend()) {
      if (saltIter->second == index) {
         break;
      }
      ++saltIter;
   }

   if (saltIter == saltMap_.rend()) {
      throw DerivationSchemeException("missing salt for id");
   }

   if (saltIter->first.getSize() != 32) {
      throw DerivationSchemeException("unexpected salt size");
   }

   //salt root pubkey
   auto saltedPubkey = Cryptography::ECDSA::pubKeyScalarMultiply(
      pubKey, saltIter->first);
   return std::make_shared<AssetEntry_Single>(id, saltedPubkey, nullptr);
}

////////////////////////////////////////////////////////////////////////////////
std::vector<std::shared_ptr<AssetEntry>>
DerivationScheme_ECDH::extendPrivateChain(
   std::shared_ptr<Encryption::DecryptedDataContainer> ddc,
   std::shared_ptr<AssetEntry> rootAsset, int32_t start, int32_t end)
{
   //throws if the wallet is locked or the asset is missing its private key

   auto rootAsset_single = std::dynamic_pointer_cast<AssetEntry_Single>(
      rootAsset);
   if (rootAsset_single == nullptr) {
      throw DerivationSchemeException("invalid root asset object");
   }

   auto nextAsset = [this, ddc, rootAsset_single](
      int32_t derivationIndex)->std::shared_ptr<AssetEntry>
   {
      //sanity checks
      auto privkey = rootAsset_single->getPrivKey();
      if (privkey == nullptr) {
         throw AssetUnavailableException();
      }
      auto& privkeyData = ddc->getClearTextAssetData(privkey);

      return computeNextPrivateEntry(
         ddc,
         privkeyData, move(privkey->getCipherDataPtr()->cipher->getCopy()),
         AssetId(rootAsset_single->getAccountID(), derivationIndex));
   };

   if (ddc == nullptr) {
      throw AssetUnavailableException();
   }
   ReentrantLock lock(ddc.get());

   std::vector<std::shared_ptr<AssetEntry>> assetVec;
   for (int32_t i = start; i < end; i++) {
      auto newAsset = nextAsset(i+1);
      assetVec.push_back(newAsset);
   }
   return assetVec;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AssetEntry_Single>
DerivationScheme_ECDH::computeNextPrivateEntry(
   std::shared_ptr<Encryption::DecryptedDataContainer> ddc,
   const SecureBinaryData& privKeyData,
   std::unique_ptr<Encryption::Cipher> cipher, AssetId id)
{
   //get salt
   auto assetKey = id.getAssetKey();
   auto saltIter = saltMap_.rbegin();
   while (saltIter != saltMap_.rend()) {
      if (saltIter->second == assetKey) {
         break;
      }
      ++saltIter;
   }

   if (saltIter == saltMap_.rend()) {
      throw DerivationSchemeException("missing salt for id");
   }
   if (saltIter->first.getSize() != 32) {
      throw DerivationSchemeException("unexpected salt size");
   }

   //salt root privkey
   auto saltedPrivKey = Cryptography::ECDSA::privKeyScalarMultiply(
      privKeyData, saltIter->first);

   //compute salted pubkey
   auto saltedPubKey = Cryptography::ECDSA::computePublicKey(
      saltedPrivKey, true);

   //encrypt the new privkey
   auto newCipher = cipher->getCopy(); //copying a cypher cycles the IV
   auto encryptedNextPrivKey = ddc->encryptData(
      newCipher.get(), saltedPrivKey);

   //instantiate new encrypted key object
   auto cipherData = std::make_unique<Encryption::CipherData>(
      encryptedNextPrivKey, move(newCipher));
   auto nextPrivKey = std::make_shared<Asset_PrivateKey>(
      id, std::move(cipherData));

   //instantiate and return new asset entry
   return std::make_shared<AssetEntry_Single>(id, saltedPubKey, nextPrivKey);
}

////////////////////////////////////////////////////////////////////////////////
AssetKeyType DerivationScheme_ECDH::getIdForSalt(
   const SecureBinaryData& salt)
{
   auto iter = saltMap_.find(salt);
   if (iter == saltMap_.end()) {
      throw DerivationSchemeException("missing salt");
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
const std::map<SecureBinaryData, AssetKeyType>&
DerivationScheme_ECDH::getSaltMap() const
{
   return saltMap_;
}
