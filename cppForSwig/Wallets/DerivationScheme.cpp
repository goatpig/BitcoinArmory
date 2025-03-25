////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ReentrantLock.h"
#include "DerivationScheme.h"
#include "EncryptedDB.h"
#include "DecryptedDataContainer.h"
#include "BIP32_Node.h"

#define DERSCHEME_LEGACY_VERSION 0x00000001
#define DERSCHEME_BIP32_VERSION  0x00000001
#define DERSCHEME_SALTED_VERSION 0x00000001
#define DERSCHEME_ECDH_VERSION   0x00000001

using namespace std;
using namespace Armory::Assets;
using namespace Armory::Wallets;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// DerivationScheme
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DerivationScheme::~DerivationScheme()
{}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<DerivationScheme> DerivationScheme::deserialize(BinaryDataRef data)
{
   BinaryRefReader brr(data);

   //version
   auto version = brr.get_uint32_t();

   //get derivation scheme type
   auto schemeType = brr.get_uint8_t();

   shared_ptr<DerivationScheme> derScheme;

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
         auto&& chainCode = SecureBinaryData(brr.get_BinaryDataRef(len));
         derScheme = make_shared<DerivationScheme_ArmoryLegacy>(
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
         auto&& chainCode = SecureBinaryData(brr.get_BinaryDataRef(len));

         //bip32 node meta data
         auto depth = brr.get_uint32_t();
         auto leafID = brr.get_uint32_t();

         //instantiate object
         derScheme = make_shared<DerivationScheme_BIP32>(
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
         auto&& chainCode = SecureBinaryData(brr.get_BinaryDataRef(len));

         //bip32 node meta data
         auto depth = brr.get_uint32_t();
         auto leafID = brr.get_uint32_t();

         //salt
         len = brr.get_var_int();
         auto&& salt = SecureBinaryData(brr.get_BinaryDataRef(len));

         //instantiate object
         derScheme = make_shared<DerivationScheme_BIP32_Salted>(
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
         derScheme = make_shared<DerivationScheme_ECDH>(id);

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
shared_ptr<AssetEntry_Single>
   DerivationScheme_ArmoryLegacy::computeNextPublicEntry(
   const SecureBinaryData& pubKey, AssetId id)
{
   auto nextPubkey = CryptoECDSA().ComputeChainedPublicKey(
      pubKey, chainCode_);
   return make_shared<AssetEntry_Single>(id, nextPubkey, nullptr);
}

////////////////////////////////////////////////////////////////////////////////
vector<shared_ptr<AssetEntry>> DerivationScheme_ArmoryLegacy::extendPublicChain(
   shared_ptr<AssetEntry> firstAsset, unsigned start, unsigned end,
   const std::function<void(int)>& progressCallback)
{
   auto nextAsset = [this](
      shared_ptr<AssetEntry> assetPtr)->shared_ptr<AssetEntry>
   {
      auto assetSingle =
         dynamic_pointer_cast<AssetEntry_Single>(assetPtr);

      //get pubkey
      auto pubkey = assetSingle->getPubKey();
      auto& pubkeyData = pubkey->getUncompressedKey();

      return computeNextPublicEntry(pubkeyData,
         AssetId(assetSingle->getAccountID(), assetSingle->getIndex() + 1));
   };

   vector<shared_ptr<AssetEntry>> assetVec;
   auto currentAsset = firstAsset;

   for (unsigned i = start; i <= end; i++)
   {
      currentAsset = nextAsset(currentAsset);
      assetVec.emplace_back(currentAsset);

      if (progressCallback)
         progressCallback(i-start+1);
   }

   return assetVec;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<AssetEntry_Single>
   DerivationScheme_ArmoryLegacy::computeNextPrivateEntry(
   shared_ptr<Encryption::DecryptedDataContainer> ddc,
   const SecureBinaryData& privKeyData,
   unique_ptr<Encryption::Cipher> cipher,
   AssetId id)
{
   //chain the private key
   auto&& nextPrivkeySBD = CryptoECDSA().ComputeChainedPrivateKey(
      privKeyData, chainCode_);

   //compute its pubkey
   auto&& nextPubkey = CryptoECDSA().ComputePublicKey(nextPrivkeySBD);

   //encrypt the new privkey
   auto&& newCipher = cipher->getCopy(); //copying a cipher cycles the IV
   auto&& encryptedNextPrivKey = ddc->encryptData(
      newCipher.get(), nextPrivkeySBD);

   //clear the unencrypted privkey object
   nextPrivkeySBD.clear();

   //instantiate new encrypted key object
   auto cipherData =
      make_unique<Encryption::CipherData>(encryptedNextPrivKey, move(newCipher));
   auto nextPrivKey = make_shared<Asset_PrivateKey>(id, move(cipherData));

   //instantiate and return new asset entry
   return make_shared<AssetEntry_Single>(id, nextPubkey, nextPrivKey);
}

////////////////////////////////////////////////////////////////////////////////
vector<shared_ptr<AssetEntry>>
   DerivationScheme_ArmoryLegacy::extendPrivateChain(
   shared_ptr<Encryption::DecryptedDataContainer> ddc,
   shared_ptr<AssetEntry> firstAsset,
   unsigned start, unsigned end)
{
   //throws if the wallet is locked or the asset is missing its private key

   auto nextAsset = [this, ddc](
      shared_ptr<AssetEntry> assetPtr)->shared_ptr<AssetEntry>
   {
      //sanity checks
      auto assetSingle =
         dynamic_pointer_cast<AssetEntry_Single>(assetPtr);

      auto privkey = assetSingle->getPrivKey();
      if (privkey == nullptr)
         throw AssetUnavailableException();
      auto& privkeyData =
         ddc->getClearTextAssetData(privkey);

      return computeNextPrivateEntry(
         ddc,
         privkeyData, move(privkey->getCipherDataPtr()->cipher_->getCopy()),
         AssetId(assetSingle->getAccountID(), assetSingle->getIndex() + 1));
   };

   if (ddc == nullptr || firstAsset == nullptr)
   {
      LOGERR << "missing asset, cannot extent private chain";
      throw AssetUnavailableException();
   }

   ReentrantLock lock(ddc.get());

   vector<shared_ptr<AssetEntry>> assetVec;
   auto currentAsset = firstAsset;

   for (unsigned i = start; i <= end; i++)
   {
      currentAsset = nextAsset(currentAsset);
      assetVec.push_back(currentAsset);
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
shared_ptr<AssetEntry_Single>
   DerivationScheme_BIP32::computeNextPrivateEntry(
   shared_ptr<Encryption::DecryptedDataContainer> ddc,
   const SecureBinaryData& privKeyData,
   unique_ptr<Encryption::Cipher> cipher,
   AssetId id)
{
   auto index = id.getAssetKey();

   //derScheme only allows for soft derivation
   if (index > 0x7FFFFFFF)
      throw DerivationSchemeException("illegal: hard derivation");

   BIP32_Node node;
   node.initFromPrivateKey(depth_, leafId_, 0, privKeyData, chainCode_);
   node.derivePrivate(index);

   //encrypt the new privkey
   auto&& newCipher = cipher->getCopy(); //copying a cypher cycles the IV
   auto&& encryptedNextPrivKey = ddc->encryptData(
      newCipher.get(), node.getPrivateKey());

   //instantiate new encrypted key object
   auto cipherData =
      make_unique<Encryption::CipherData>(
         encryptedNextPrivKey, move(newCipher));
   auto nextPrivKey = make_shared<Asset_PrivateKey>(id, move(cipherData));

   //instantiate and return new asset entry
   auto nextPubkey = node.movePublicKey();
   return make_shared<AssetEntry_Single>(id, nextPubkey, nextPrivKey);
}

////////////////////////////////////////////////////////////////////////////////
vector<shared_ptr<AssetEntry>>
   DerivationScheme_BIP32::extendPrivateChain(
   shared_ptr<Encryption::DecryptedDataContainer> ddc,
   shared_ptr<AssetEntry> rootAsset,
   unsigned start, unsigned end)
{
   //throws if the wallet is locked or the asset is missing its private key

   auto rootAsset_single = dynamic_pointer_cast<AssetEntry_Single>(rootAsset);
   if (rootAsset_single == nullptr)
      throw DerivationSchemeException("invalid root asset object");
   const auto& account_id = rootAsset_single->getAccountID();

   auto nextAsset = [this, ddc, rootAsset_single, &account_id](
      unsigned derivationIndex)->shared_ptr<AssetEntry>
   {
      //sanity checks
      auto privkey = rootAsset_single->getPrivKey();
      if (privkey == nullptr)
         throw AssetUnavailableException();
      auto& privkeyData = ddc->getClearTextAssetData(privkey);

      return computeNextPrivateEntry(
         ddc,
         privkeyData, move(privkey->getCipherDataPtr()->cipher_->getCopy()),
         AssetId(account_id, derivationIndex));
   };

   if (ddc == nullptr)
      throw AssetUnavailableException();

   ReentrantLock lock(ddc.get());

   vector<shared_ptr<AssetEntry>> assetVec;

   for (unsigned i = start; i <= end; i++)
   {
      auto newAsset = nextAsset(i);
      assetVec.push_back(newAsset);
   }

   return assetVec;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<AssetEntry_Single>
   DerivationScheme_BIP32::computeNextPublicEntry(
   const SecureBinaryData& pubKey, AssetId id)
{
   auto index = id.getAssetKey();

   //derScheme only allows for soft derivation
   if (index > 0x7FFFFFFF)
      throw DerivationSchemeException("illegal: hard derivation");

   BIP32_Node node;
   node.initFromPublicKey(depth_, leafId_, 0, pubKey, chainCode_);
   node.derivePublic(index);

   auto nextPubKey = node.movePublicKey();
   return make_shared<AssetEntry_Single>(id, nextPubKey, nullptr);
}

////////////////////////////////////////////////////////////////////////////////
vector<shared_ptr<AssetEntry>> DerivationScheme_BIP32::extendPublicChain(
   shared_ptr<AssetEntry> rootAsset, uint32_t start, uint32_t end,
   const std::function<void(int)>& progressCallback)
{
   auto rootSingle = dynamic_pointer_cast<AssetEntry_Single>(rootAsset);

   auto nextAsset = [this, rootSingle](
      uint32_t derivationIndex)->shared_ptr<AssetEntry>
   {
      //get pubkey
      auto pubkey = rootSingle->getPubKey();
      auto& pubkeyData = pubkey->getCompressedKey();

      return computeNextPublicEntry(pubkeyData,
         AssetId(rootSingle->getAccountID(), derivationIndex));
   };

   vector<shared_ptr<AssetEntry>> assetVec;

   for (unsigned i = start; i <= end; i++)
   {
      auto newAsset = nextAsset(i);
      assetVec.emplace_back(move(newAsset));

      if (progressCallback)
         progressCallback(i-start+1);
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
std::shared_ptr<AssetEntry_Single> 
DerivationScheme_BIP32_Salted::computeNextPrivateEntry(
   std::shared_ptr<Encryption::DecryptedDataContainer> ddc,
   const SecureBinaryData& privKey,
   std::unique_ptr<Encryption::Cipher> cipher,
   AssetId id)
{
   auto index = id.getAssetKey();

   //derScheme only allows for soft derivation
   if (index > 0x7FFFFFFF)
      throw DerivationSchemeException("illegal: hard derivation");

   BIP32_Node node;
   node.initFromPrivateKey(
      getDepth(), getLeafId(), 0, privKey, getChaincode());
   node.derivePrivate(index);

   //salt the key
   auto&& saltedPrivKey = CryptoECDSA::PrivKeyScalarMultiply(
      node.getPrivateKey(), salt_);

   //compute salted pubkey
   auto&& saltedPubKey = CryptoECDSA().ComputePublicKey(saltedPrivKey, true);

   //encrypt the new privkey
   auto&& newCipher = cipher->getCopy(); //copying a cypher cycles the IV
   auto&& encryptedNextPrivKey = ddc->encryptData(
      newCipher.get(), saltedPrivKey);

   //instantiate encrypted salted privkey object
   auto cipherData = make_unique<Encryption::CipherData>(
         encryptedNextPrivKey, move(newCipher));
   auto nextPrivKey = make_shared<Asset_PrivateKey>(id, move(cipherData));

   //instantiate and return new asset entry
   return make_shared<AssetEntry_Single>(id, saltedPubKey, nextPrivKey);
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<AssetEntry_Single>
DerivationScheme_BIP32_Salted::computeNextPublicEntry(
   const SecureBinaryData& pubKey, AssetId id)
{
   auto index = id.getAssetKey();

   //derScheme only allows for soft derivation
   if (index > 0x7FFFFFFF)
      throw DerivationSchemeException("illegal: hard derivation");

   //compute pub key
   BIP32_Node node;
   node.initFromPublicKey(getDepth(), getLeafId(), 0, pubKey, getChaincode());
   node.derivePublic(index);
   auto nextPubkey = node.movePublicKey();

   //salt it
   auto&& saltedPubkey = CryptoECDSA::PubKeyScalarMultiply(nextPubkey, salt_);

   return make_shared<AssetEntry_Single>(id, saltedPubkey, nullptr);
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
   shared_ptr<IO::DBIfaceTransaction> txPtr)
{
   if (salt.getSize() != 32)
      throw DerivationSchemeException("salt is too small");

   //return the salt id if it's already in there
   auto saltIter = saltMap_.find(salt);
   if (saltIter != saltMap_.end())
      return saltIter->second;

   unique_lock<mutex> lock(saltMutex_);

   unsigned id = ++topSaltIndex_;
   auto insertIter = saltMap_.insert(make_pair(salt, id));
   if (!insertIter.second)
      throw DerivationSchemeException("failed to insert salt");

   //update on disk if we have a db tx
   if (txPtr != nullptr)
      putSalt(id, salt, txPtr);

   //return insert index
   return id;
}

////////////////////////////////////////////////////////////////////////////////
void DerivationScheme_ECDH::putSalt(AssetKeyType id,
   const SecureBinaryData& salt, shared_ptr<IO::DBIfaceTransaction> txPtr)
{
   //update on disk
   BinaryWriter bwKey;
   bwKey.put_uint8_t(ECDH_SALT_PREFIX);
   bwKey.put_BinaryData(id_);
   bwKey.put_uint32_t(id, BE);

   auto dataRef = txPtr->getDataRef(bwKey.getData());
   if (!dataRef.empty())
   {
      //read the salt
      BinaryRefReader brr(dataRef);
      auto size = brr.get_var_int();
      auto saltRef = brr.get_BinaryDataRef(size);
      if (saltRef != salt)
      {
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
void DerivationScheme_ECDH::putAllSalts(shared_ptr<IO::DBIfaceTransaction> txPtr)
{
   //expects live read-write db tx
   for (auto& saltPair : saltMap_)
      putSalt(saltPair.second, saltPair.first, txPtr);
}

////////////////////////////////////////////////////////////////////////////////
void DerivationScheme_ECDH::getAllSalts(shared_ptr<IO::DBIfaceTransaction> txPtr)
{
   BinaryWriter bwKey;
   bwKey.put_uint8_t(ECDH_SALT_PREFIX);
   bwKey.put_BinaryData(id_);
   BinaryDataRef keyBdr = bwKey.getDataRef();

   auto dbIter = txPtr->getIterator();
   dbIter->seek(keyBdr);
   while (dbIter->isValid())
   {
      auto&& key = dbIter->key();
      if (!key.startsWith(keyBdr) ||
         key.getSize() != keyBdr.getSize() + 4)
      {
         break;
      }

      auto saltIdBdr = key.getSliceCopy(keyBdr.getSize(), 4);
      auto saltId = READ_UINT32_BE(saltIdBdr);

      auto value = dbIter->value();
      BinaryRefReader bdrData(value);
      auto len = bdrData.get_var_int();
      auto&& salt = bdrData.get_SecureBinaryData(len);

      saltMap_.emplace(make_pair(move(salt), saltId));
      dbIter->advance();
   }

   //sanity check
   set<unsigned> idSet;
   for (auto& saltPair : saltMap_)
   {
      auto insertIter = idSet.insert(saltPair.second);
      if (insertIter.second == false)
         throw DerivationSchemeException("ECDH id collision!");
   }

   if (idSet.empty())
      return;

   //set top index
   auto idIter = idSet.rbegin();
   topSaltIndex_ = *idIter;
}

////////////////////////////////////////////////////////////////////////////////
vector<shared_ptr<AssetEntry>> DerivationScheme_ECDH::extendPublicChain(
   shared_ptr<AssetEntry> root, unsigned start, unsigned end,
   const std::function<void(int)>& progressCallback)
{
   auto rootSingle = dynamic_pointer_cast<AssetEntry_Single>(root);
   if (rootSingle == nullptr)
      throw DerivationSchemeException("unexpected root asset type");

   auto nextAsset = [this, rootSingle](
      unsigned derivationIndex)->shared_ptr<AssetEntry>
   {
      //get pubkey
      auto pubkey = rootSingle->getPubKey();
      auto& pubkeyData = pubkey->getCompressedKey();

      return computeNextPublicEntry(pubkeyData,
         AssetId(rootSingle->getAccountID(), derivationIndex));
   };

   vector<shared_ptr<AssetEntry>> assetVec;

   for (unsigned i = start; i <= end; i++)
   {
      auto newAsset = nextAsset(i);
      assetVec.emplace_back(move(newAsset));

      if (progressCallback)
         progressCallback(i-start+1);
   }

   return assetVec;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<AssetEntry_Single> DerivationScheme_ECDH::computeNextPublicEntry(
   const SecureBinaryData& pubKey, AssetId id)
{
   if (pubKey.getSize() != 33)
      throw DerivationSchemeException("unexpected pubkey size");

   //get salt
   auto index = id.getAssetKey();
   auto saltIter = saltMap_.rbegin();
   while (saltIter != saltMap_.rend())
   {
      if (saltIter->second == index)
         break;

      ++saltIter;
   }

   if (saltIter == saltMap_.rend())
      throw DerivationSchemeException("missing salt for id");

   if (saltIter->first.getSize() != 32)
      throw DerivationSchemeException("unexpected salt size");

   //salt root pubkey
   auto&& saltedPubkey = CryptoECDSA::PubKeyScalarMultiply(
      pubKey, saltIter->first);

   return make_shared<AssetEntry_Single>(id, saltedPubkey, nullptr);
}

////////////////////////////////////////////////////////////////////////////////
vector<shared_ptr<AssetEntry>> DerivationScheme_ECDH::extendPrivateChain(
   shared_ptr<Encryption::DecryptedDataContainer> ddc,
   shared_ptr<AssetEntry> rootAsset, uint32_t start, uint32_t end)
{
   //throws if the wallet is locked or the asset is missing its private key

   auto rootAsset_single = dynamic_pointer_cast<AssetEntry_Single>(rootAsset);
   if (rootAsset_single == nullptr)
      throw DerivationSchemeException("invalid root asset object");

   auto nextAsset = [this, ddc, rootAsset_single](
      uint32_t derivationIndex)->shared_ptr<AssetEntry>
   {
      //sanity checks
      auto privkey = rootAsset_single->getPrivKey();
      if (privkey == nullptr)
         throw AssetUnavailableException();
      auto& privkeyData = ddc->getClearTextAssetData(privkey);

      return computeNextPrivateEntry(
         ddc,
         privkeyData, move(privkey->getCipherDataPtr()->cipher_->getCopy()),
         AssetId(rootAsset_single->getAccountID(), derivationIndex));
   };

   if (ddc == nullptr)
      throw AssetUnavailableException();

   ReentrantLock lock(ddc.get());

   vector<shared_ptr<AssetEntry>> assetVec;

   for (uint32_t i = start; i <= end; i++)
   {
      auto newAsset = nextAsset(i);
      assetVec.push_back(newAsset);
   }

   return assetVec;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<AssetEntry_Single>
DerivationScheme_ECDH::computeNextPrivateEntry(
   shared_ptr<Encryption::DecryptedDataContainer> ddc,
   const SecureBinaryData& privKeyData,
   unique_ptr<Encryption::Cipher> cipher, AssetId id)
{
   //get salt
   auto assetKey = id.getAssetKey();
   auto saltIter = saltMap_.rbegin();
   while (saltIter != saltMap_.rend())
   {
      if (saltIter->second == assetKey)
         break;

      ++saltIter;
   }

   if (saltIter == saltMap_.rend())
      throw DerivationSchemeException("missing salt for id");

   if (saltIter->first.getSize() != 32)
      throw DerivationSchemeException("unexpected salt size");

   //salt root privkey
   auto&& saltedPrivKey = CryptoECDSA::PrivKeyScalarMultiply(
      privKeyData, saltIter->first);

   //compute salted pubkey
   auto&& saltedPubKey = CryptoECDSA().ComputePublicKey(saltedPrivKey, true);

   //encrypt the new privkey
   auto&& newCipher = cipher->getCopy(); //copying a cypher cycles the IV
   auto&& encryptedNextPrivKey = ddc->encryptData(
      newCipher.get(), saltedPrivKey);

   //instantiate new encrypted key object
   auto cipherData = make_unique<Encryption::CipherData>(
      encryptedNextPrivKey, move(newCipher));
   auto nextPrivKey = make_shared<Asset_PrivateKey>(id, move(cipherData));

   //instantiate and return new asset entry
   return make_shared<AssetEntry_Single>(id, saltedPubKey, nextPrivKey);
}

////////////////////////////////////////////////////////////////////////////////
AssetKeyType DerivationScheme_ECDH::getIdForSalt(
   const SecureBinaryData& salt)
{
   auto iter = saltMap_.find(salt);
   if (iter == saltMap_.end())
      throw DerivationSchemeException("missing salt");

   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
const map<SecureBinaryData, AssetKeyType>& DerivationScheme_ECDH::getSaltMap() const
{
   return saltMap_;
}
