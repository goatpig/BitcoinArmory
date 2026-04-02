////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Utils/BtcUtils.h"
#include "Utils/Cryptography.h"
#include "Assets.h"
#include "ScriptRecipient.h"
#include "BIP32_Node.h"
#include "Seeds/Seeds.h"

#define ASSET_VERSION                  0x00000001

#define ASSETENTRY_SINGLE_VERSION      0x00000001
#define ASSETENTRY_BIP32ROOT_VERSION   0x00000002
#define ASSETENTRY_LEGACYROOT_VERSION  0x00000002
#define ASSETENTRY_SCRIPTHASH_VERSION  0x00000001
#define ASSETENTRY_RAWSCRIPT_VERSION   0x00000001

#define PRIVKEY_VERSION                0x00000002
#define PUBKEY_COMPRESSED_VERSION      0x00000001
#define PUBKEY_UNCOMPRESSED_VERSION    0x00000001

#define PEER_PUBLICDATA_VERSION        0x00000002
#define PEER_ROOTKEY_VERSION           0x00000001
#define PEER_ROOTSIG_VERSION           0x00000001
#define PEER_MASTERKEY_VERSION         0x00000001

#define COMMENT_DATA_VERSION           0x00000001

using namespace Armory;
using namespace Armory::Assets;

////////////////////////////////////////////////////////////////////////////////
AssetException::AssetException(const std::string& err) :
   std::runtime_error(err)
{}

////////////////////////////////////////////////////////////////////////////////
// AssetEntry
AssetEntry::AssetEntry(AssetEntryType type, Wallets::AssetId id) :
   type_(type), ID_(id)
{}

AssetEntry::~AssetEntry()
{}

////////
const Wallets::AssetId& AssetEntry::getID() const
{
   return ID_;
}

Wallets::AssetKeyType AssetEntry::getIndex() const
{
   return ID_.getAssetKey();
}

const Wallets::AssetAccountId AssetEntry::getAccountID(void) const
{
   return ID_.getAssetAccountId();
}

BinaryData AssetEntry::getDbKey() const
{
   return ID_.getSerializedKey(ASSETENTRY_PREFIX);
}

AssetEntryType AssetEntry::getType() const
{
   return type_;
}

////////
bool AssetEntry::needsCommit() const
{
   return needsCommit_;
}

void AssetEntry::doNotCommit()
{
   needsCommit_ = false;
}

void AssetEntry::flagForCommit()
{
   needsCommit_ = true;
}

////////
std::shared_ptr<AssetEntry> AssetEntry::deserialize(
   BinaryDataRef key, BinaryDataRef value)
{
   BinaryRefReader brrKey(key);
   auto assetId = Wallets::AssetId::deserializeKey(key, ASSETENTRY_PREFIX);
   auto assetPtr = deserDBValue(assetId, value);
   assetPtr->doNotCommit();
   return assetPtr;
}

std::shared_ptr<AssetEntry> AssetEntry::deserDBValue(
   const Wallets::AssetId& assetId, BinaryDataRef value)
{
   BinaryRefReader brrVal(value);
   auto version = brrVal.get_uint32_t();
   auto val = brrVal.get_uint8_t();
   auto entryType = AssetEntryType(val & 0x0F);

   auto getKeyData = [&assetId](BinaryRefReader& brr,
      std::shared_ptr<Asset_PrivateKey>& privKeyPtr,
      SecureBinaryData& pubKeyCompressed,
      SecureBinaryData& pubKeyUncompressed
   )->void
   {
      std::vector<BinaryDataRef> dataVec;
      while (brr.getSizeRemaining() > 0) {
         auto len = brr.get_var_int();
         auto valref = brr.get_BinaryDataRef(len);
         dataVec.push_back(valref);
      }

      for (auto& dataRef : dataVec) {
         BinaryRefReader brrData(dataRef);
         auto version = brrData.get_uint32_t();
         auto keybyte = brrData.get_uint8_t();

         switch (keybyte)
         {
            case PUBKEY_UNCOMPRESSED_BYTE:
            {
               switch (version)
               {
                  case 0x00000001:
                  {
                     if (dataRef.getSize() != 70) {
                        throw AssetException(
                           "invalid size for uncompressed pub key");
                     }
                     if (!pubKeyUncompressed.empty()) {
                        throw AssetException(
                           "multiple pub keys for entry");
                     }
                     pubKeyUncompressed = std::move(SecureBinaryData{
                        brrData.get_BinaryDataRef(
                           brrData.getSizeRemaining())});
                     break;
                  }

               default:
                  throw AssetException("unsupported pubkey version");
               }
               break;
            }

            case PUBKEY_COMPRESSED_BYTE:
            {
               switch (version)
               {
                  case 0x00000001:
                  {
                     if (dataRef.getSize() != 38) {
                        throw AssetException(
                           "invalid size for compressed pub key");
                     }
                     if (!pubKeyCompressed.empty()) {
                        throw AssetException("multiple pub keys for entry");
                     }
                     pubKeyCompressed = std::move(SecureBinaryData{
                        brrData.get_BinaryDataRef(brrData.getSizeRemaining())});
                     break;
                  }

                  default:
                     throw AssetException("unsupported pubkey version");
               }
               break;
            }

            case PRIVKEY_BYTE:
            {
               if (privKeyPtr != nullptr) {
                  throw AssetException("multiple priv keys for entry");
               }
               std::unique_ptr<Asset_PrivateKey> keyUPtr;
               try {
                  keyUPtr = Asset_PrivateKey::deserialize(dataRef);
               } catch (const Wallets::IdException&) {
                  //potentially an old id format, let's try that instead
                  keyUPtr = Asset_PrivateKey::deserializeOld(
                     assetId, dataRef);
               } catch (const AssetException&) {
                  //potentially an old id format, let's try that instead
                  keyUPtr = Asset_PrivateKey::deserializeOld(
                     assetId, dataRef);
               }

               if (keyUPtr->getAssetId() != assetId) {
                  throw AssetException("priv key asset mismatch");
               }
               privKeyPtr = std::shared_ptr<Asset_PrivateKey>(move(keyUPtr));
               if (privKeyPtr == nullptr) {
                  throw AssetException("deserialized to unexpected type");
               }
               break;
            }

            default:
               throw AssetException("unsupported key type byte");
         }
      }
   };

   switch (entryType)
   {
      case AssetEntryType::Single:
      {
         switch (version)
         {
            case 0x00000001:
            {
               std::shared_ptr<Asset_PrivateKey> privKeyPtr;
               SecureBinaryData pubKeyCompressed;
               SecureBinaryData pubKeyUncompressed;

               getKeyData(brrVal, privKeyPtr, pubKeyCompressed, pubKeyUncompressed);
               auto addrEntry = std::make_shared<AssetEntry_Single>(
                  assetId,
                  pubKeyUncompressed, pubKeyCompressed, privKeyPtr);

               addrEntry->doNotCommit();
               return addrEntry;
            }

            default:
               throw AssetException("unsupported asset single version");
         }
         break;
      }

      case AssetEntryType::ScriptHash:
      {
         switch (version)
         {
            case 0x00000001:
            {
               auto hashLen = brrVal.get_uint32_t();
               SecureBinaryData scriptHash{brrVal.get_BinaryDataRef(hashLen)};
               return std::make_shared<AssetEntry_ScriptHash>(
                  assetId, scriptHash);
            }

            default:
               throw AssetException("unsupported asset script hash version");
         }
      }

      case AssetEntryType::RawScript:
      {
         switch (version)
         {
            case 0x00000001:
            {
               auto hashLen = brrVal.get_uint32_t();
               SecureBinaryData script{brrVal.get_BinaryDataRef(hashLen)};
               return std::make_shared<AssetEntry_RawScript>(
                  assetId, script);
            }

            default:
               throw AssetException("unsupported asset script hash version");
         }
      }

      case AssetEntryType::BIP32Root:
      {
         switch (version)
         {
            case 0x00000001:
            case 0x00000002:
            {
               auto depth = brrVal.get_uint8_t();
               auto leafid = brrVal.get_uint32_t();
               auto fingerprint = brrVal.get_uint32_t();
               auto cclen = brrVal.get_var_int();
               SecureBinaryData chaincode{brrVal.get_BinaryDataRef(cclen)};
               unsigned seedFingerprint = UINT32_MAX;

               std::vector<uint32_t> derPath;
               if (version >= 0x00000002) {
                  seedFingerprint = brrVal.get_uint32_t();
                  auto count = brrVal.get_var_int();
                  for (unsigned i=0; i<count; i++) {
                     derPath.push_back(brrVal.get_uint32_t());
                  }
               }

               std::shared_ptr<Asset_PrivateKey> privKeyPtr;
               SecureBinaryData pubKeyCompressed;
               SecureBinaryData pubKeyUncompressed;
               getKeyData(brrVal, privKeyPtr, pubKeyCompressed, pubKeyUncompressed);

               std::shared_ptr<AssetEntry_BIP32Root> rootEntry;
               if (!pubKeyCompressed.empty()) {
                  rootEntry = std::make_shared<AssetEntry_BIP32Root>(
                     assetId,
                     pubKeyCompressed, privKeyPtr,
                     chaincode, depth, leafid, fingerprint, seedFingerprint,
                     derPath);
               } else {
                  rootEntry = std::make_shared<AssetEntry_BIP32Root>(
                     assetId,
                     pubKeyUncompressed, privKeyPtr,
                     chaincode, depth, leafid, fingerprint, seedFingerprint,
                     derPath);
               }

               rootEntry->doNotCommit();
               return rootEntry;
            }

            default:
               throw AssetException("unsupported bip32 root version");
         }
         break;
      }

      case AssetEntryType::ArmoryLegacyRoot:
      {
         switch (version)
         {
            case 0x00000001:
            {
               auto cclen = brrVal.get_var_int();
               auto chaincode = brrVal.get_BinaryDataRef(cclen);

               std::shared_ptr<Asset_PrivateKey> privKeyPtr;
               SecureBinaryData pubKeyCompressed;
               SecureBinaryData pubKeyUncompressed;
               getKeyData(
                  brrVal, privKeyPtr,
                  pubKeyCompressed, pubKeyUncompressed);

               auto rootEntry = std::make_shared<AssetEntry_ArmoryLegacyRoot>(
                  assetId,
                  pubKeyCompressed, privKeyPtr,
                  chaincode,
                  Seeds::LegacyType::Undefined);
               rootEntry->doNotCommit();
               return rootEntry;
            }

            case 0x00000002:
            {
               auto seedType = (Seeds::LegacyType)brrVal.get_uint8_t();
               auto cclen = brrVal.get_var_int();
               auto chaincode = brrVal.get_BinaryDataRef(cclen);

               std::shared_ptr<Asset_PrivateKey> privKeyPtr;
               SecureBinaryData pubKeyCompressed;
               SecureBinaryData pubKeyUncompressed;
               getKeyData(
                  brrVal, privKeyPtr,
                  pubKeyCompressed, pubKeyUncompressed);

               auto rootEntry = std::make_shared<AssetEntry_ArmoryLegacyRoot>(
                  assetId,
                  pubKeyCompressed, privKeyPtr,
                  chaincode,
                  seedType);
               rootEntry->doNotCommit();
               return rootEntry;
            }

            default:
               throw AssetException("unsupported legacy root version");
         }
      }

      default:
         throw AssetException("invalid asset entry type");
   }

   throw AssetException("invalid asset entry type");
   return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// AssetEntry_Single
AssetEntry_Single::AssetEntry_Single(Wallets::AssetId id,
   SecureBinaryData& pubkey, std::shared_ptr<Asset_PrivateKey> privkey) :
   AssetEntry(AssetEntryType::Single, id), privkey_(privkey)
{
   pubkey_ = std::make_shared<Asset_PublicKey>(pubkey);
}

AssetEntry_Single::AssetEntry_Single(Wallets::AssetId id,
   SecureBinaryData& pubkeyUncompressed,
   SecureBinaryData& pubkeyCompressed,
   std::shared_ptr<Asset_PrivateKey> privkey) :
   AssetEntry(AssetEntryType::Single, id), privkey_(privkey)
{
   pubkey_ = std::make_shared<Asset_PublicKey>(
   pubkeyUncompressed, pubkeyCompressed);
}

AssetEntry_Single::AssetEntry_Single(Wallets::AssetId id,
   std::shared_ptr<Asset_PublicKey> pubkey,
   std::shared_ptr<Asset_PrivateKey> privkey) :
   AssetEntry(AssetEntryType::Single, id),
   pubkey_(pubkey), privkey_(privkey)
{}

////////
std::shared_ptr<Asset_PublicKey> AssetEntry_Single::getPubKey() const
{
   return pubkey_;
}

std::shared_ptr<Asset_PrivateKey> AssetEntry_Single::getPrivKey() const
{
   return privkey_;
}

bool AssetEntry_Single::hasPrivateKey() const
{
   if (privkey_ != nullptr) {
      return privkey_->hasData();
   }
   return false;
}

////////
BinaryData AssetEntry_Single::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(ASSETENTRY_SINGLE_VERSION);

   auto entryType = getType();
   bw.put_uint8_t((uint8_t)entryType);

   bw.put_BinaryData(pubkey_->serialize());
   if (privkey_ != nullptr && privkey_->hasData()) {
      bw.put_BinaryData(privkey_->serialize());
   }

   BinaryWriter finalBw;
   finalBw.put_var_int(bw.getSize());
   finalBw.put_BinaryData(bw.getData());
   return finalBw.getData();
}

BinaryData AssetEntry_BIP32Root::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(ASSETENTRY_BIP32ROOT_VERSION);

   auto entryType = getType();
   bw.put_uint8_t((uint8_t)entryType);

   bw.put_uint8_t(depth_);
   bw.put_uint32_t(leafID_);
   bw.put_uint32_t(parentFingerprint_);
   
   bw.put_var_int(chaincode_.getSize());
   bw.put_BinaryData(chaincode_);

   auto pubkey = getPubKey();
   auto privkey = getPrivKey();

   bw.put_uint32_t(seedFingerprint_);
   bw.put_var_int(derivationPath_.size());
   for (auto& step : derivationPath_) {
      bw.put_uint32_t(step);
   }

   bw.put_BinaryData(pubkey->serialize());
   if (privkey != nullptr && privkey->hasData()) {
      bw.put_BinaryData(privkey->serialize());
   }

   BinaryWriter finalBw;
   finalBw.put_var_int(bw.getSize());
   finalBw.put_BinaryData(bw.getData());
   return finalBw.getData();
}

////////
const Wallets::EncryptionKeyId&
AssetEntry_Single::getPrivateEncryptionKeyId() const
{
   if (!hasPrivateKey()) {
      throw AssetException("no private key in this asset");
   }
   return privkey_->getEncryptionKeyId();
}

const Wallets::KdfId& AssetEntry_Single::getKdfId() const
{
   if (!hasPrivateKey()) {
      throw std::runtime_error("no private key in this asset");
   }
   return privkey_->getKdfId();
}

////////
std::shared_ptr<AssetEntry_Single> AssetEntry_Single::getPublicCopy()
{
   return std::make_shared<AssetEntry_Single>(getID(), pubkey_, nullptr);
}

////////////////////////////////////////////////////////////////////////////////
//AssetEntry_Multisig
AssetEntry_Multisig::AssetEntry_Multisig(Wallets::AssetId id,
   const std::map<BinaryData, std::shared_ptr<AssetEntry>>& assetMap,
   unsigned m, unsigned n) :
   AssetEntry(AssetEntryType::Multisig, id),
   assetMap_(assetMap), m_(m), n_(n)
{
   if (assetMap.size() != n) {
      throw AssetException("asset count mismatch in multisig entry");
   }
   if (m > n || m == 0) {
      throw AssetException("invalid m");
   }
}

////
unsigned AssetEntry_Multisig::getM() const
{
   return m_;
}

unsigned AssetEntry_Multisig::getN() const
{
   return n_;
}

////////
BinaryData AssetEntry_Multisig::serialize() const
{
   throw AssetException("no serialization for MS assets");
}

////////
bool AssetEntry_Multisig::hasPrivateKey() const
{
   for (auto& asset_pair : assetMap_) {
      auto asset_single = std::dynamic_pointer_cast<AssetEntry_Single>(
         asset_pair.second);
      if (asset_single == nullptr) {
         throw AssetException("unexpected asset entry type");
      }
      if (!asset_single->hasPrivateKey()) {
         return false;
      }
   }
   return true;
}

////////
const Wallets::EncryptionKeyId&
AssetEntry_Multisig::getPrivateEncryptionKeyId() const
{
   if (assetMap_.size() != n_) {
      throw AssetException("missing asset entries");
   }
   if (!hasPrivateKey()) {
      throw AssetException("no private key in this asset");
   }

   std::set<Wallets::EncryptionKeyId> idSet;
   for (auto& asset_pair : assetMap_) {
      auto asset_single =
         std::dynamic_pointer_cast<AssetEntry_Single>(asset_pair.second);
      if (asset_single == nullptr) {
         throw AssetException("unexpected asset entry type");
      }
      idSet.emplace(asset_pair.second->getPrivateEncryptionKeyId());
   }

   if (idSet.size() != 1) {
      throw AssetException("wallets use different encryption keys");
   }
   return *idSet.begin();
}

////////
const std::map<BinaryData, std::shared_ptr<AssetEntry>>
AssetEntry_Multisig::getAssetMap() const
{
   return assetMap_;
}

////////////////////////////////////////////////////////////////////////////////
// AssetEntry_ScriptHash
AssetEntry_ScriptHash::AssetEntry_ScriptHash(const Wallets::AssetId& id,
   SecureBinaryData& scriptHash) :
   AssetEntry(AssetEntryType::ScriptHash, id), scriptHash_(std::move(scriptHash))
{}

bool AssetEntry_ScriptHash::hasPrivateKey() const
{
   return false;
}

const Wallets::EncryptionKeyId&
AssetEntry_ScriptHash::getPrivateEncryptionKeyId() const
{
   throw AssetException("not prive key for imported script hash");
}

BinaryData AssetEntry_ScriptHash::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(ASSETENTRY_SCRIPTHASH_VERSION);

   auto entryType = getType();
   bw.put_uint8_t((uint8_t)entryType);
   bw.put_uint32_t(scriptHash_.getSize());
   bw.put_BinaryData(scriptHash_);

   BinaryWriter finalBw;
   finalBw.put_var_int(bw.getSize());
   finalBw.put_BinaryData(bw.getData());
   return finalBw.getData();
}

const SecureBinaryData& AssetEntry_ScriptHash::getScriptHash() const
{
   return scriptHash_;
}

////////////////////////////////////////////////////////////////////////////////
// AssetEntry_RawScript
AssetEntry_RawScript::AssetEntry_RawScript(const Wallets::AssetId& id,
   SecureBinaryData& script) :
   AssetEntry(AssetEntryType::RawScript, id), script_(std::move(script))
{}

bool AssetEntry_RawScript::hasPrivateKey() const
{
   return false;
}

const Wallets::EncryptionKeyId&
AssetEntry_RawScript::getPrivateEncryptionKeyId() const
{
   throw AssetException("not prive key for imported script hash");
}

BinaryData AssetEntry_RawScript::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(ASSETENTRY_RAWSCRIPT_VERSION);

   auto entryType = getType();
   bw.put_uint8_t((uint8_t)entryType);
   bw.put_uint32_t(script_.getSize());
   bw.put_BinaryData(script_);

   BinaryWriter finalBw;
   finalBw.put_var_int(bw.getSize());
   finalBw.put_BinaryData(bw.getData());
   return finalBw.getData();
}

const SecureBinaryData& AssetEntry_RawScript::getScript() const
{
   return script_;
}

////////////////////////////////////////////////////////////////////////////////
// AssetEntry_BIP32Root
AssetEntry_BIP32Root::AssetEntry_BIP32Root(const Wallets::AssetId& id,
   SecureBinaryData& pubkey, std::shared_ptr<Asset_PrivateKey> privkey,
   const SecureBinaryData& chaincode,
   uint8_t depth, uint32_t leafID,
   uint32_t fingerPrint, uint32_t seedFingerprint,
   const std::vector<uint32_t>& derPath) :
   AssetEntry_Single(id, pubkey, privkey),
   chaincode_(chaincode),
   depth_(depth), leafID_(leafID),
   parentFingerprint_(fingerPrint), seedFingerprint_(seedFingerprint),
   derivationPath_(derPath)
{
   checkSeedFingerprint(false);
}

AssetEntry_BIP32Root::AssetEntry_BIP32Root(const Wallets::AssetId& id,
   std::shared_ptr<Asset_PublicKey> pubkey,
   std::shared_ptr<Asset_PrivateKey> privkey,
   const SecureBinaryData& chaincode,
   uint8_t depth, uint32_t leafID,
   uint32_t fingerPrint, uint32_t seedFingerprint,
   const std::vector<uint32_t>& derPath) :
   AssetEntry_Single(id, pubkey, privkey),
   chaincode_(chaincode),
   depth_(depth), leafID_(leafID),
   parentFingerprint_(fingerPrint), seedFingerprint_(seedFingerprint),
   derivationPath_(derPath)
{
   checkSeedFingerprint(false);
}

////////
uint8_t AssetEntry_BIP32Root::getDepth() const
{
   return depth_;
}

unsigned AssetEntry_BIP32Root::getLeafID() const
{
   return leafID_;
}

const SecureBinaryData& AssetEntry_BIP32Root::getChaincode() const
{
   return chaincode_;
}

const std::vector<uint32_t>& AssetEntry_BIP32Root::getDerivationPath() const
{
   return derivationPath_;
}

////////
AssetEntryType AssetEntry_BIP32Root::getType() const
{
   return AssetEntryType::BIP32Root;
}

////////
unsigned AssetEntry_BIP32Root::getParentFingerprint() const
{
   return parentFingerprint_;
}

void AssetEntry_BIP32Root::checkSeedFingerprint(bool strongCheck) const
{
   if (seedFingerprint_ != 0) {
      return;
   }
   std::stringstream ss;
   ss << "BIP32 root " << getThisFingerprint() << 
      " is missing seed fingerprint. You should regenerate this wallet!";
   LOGWARN << ss.str();

   if (strongCheck) {
      throw std::runtime_error(ss.str());
   }
}

unsigned AssetEntry_BIP32Root::getThisFingerprint() const
{
   if (thisFingerprint_ == UINT32_MAX)
   {
      auto pubkey = getPubKey();
      if (pubkey == nullptr) {
         throw AssetException("null pubkey");
      }
      const auto& compressed = pubkey->getCompressedKey();
      if (compressed.empty()) {
         throw AssetException("missing pubkey data");
      }
      auto hash = BtcUtils::getHash160(compressed);
      thisFingerprint_ = *(uint32_t*)hash.getPtr();
   }
   return thisFingerprint_;
}

unsigned AssetEntry_BIP32Root::getSeedFingerprint(bool strongCheck) const
{
   checkSeedFingerprint(strongCheck);

   //if we have an explicit seed fingerpint, return it
   if (seedFingerprint_ != UINT32_MAX) {
      return seedFingerprint_;
   }
   if (parentFingerprint_ == 0) {
      //otherwise, if it this root is from the seed (parent is 0), return
      //this fingerprint
      return getThisFingerprint();
   }
   throw std::runtime_error("missing seed fingerprint");
}

////////
std::shared_ptr<AssetEntry_Single> AssetEntry_BIP32Root::getPublicCopy()
{
   auto pubkey = getPubKey();
   auto woCopy = std::make_shared<AssetEntry_BIP32Root>(
      getID(), pubkey, nullptr,
      chaincode_, depth_, leafID_, parentFingerprint_, seedFingerprint_,
      derivationPath_);
   return woCopy;
}

////////
std::string AssetEntry_BIP32Root::getXPub() const
{
   auto pubkey = getPubKey();
   BIP32_Node node;
   node.initFromPublicKey(
      depth_, leafID_, parentFingerprint_,
      pubkey->getCompressedKey(), chaincode_);
   auto base58 = node.getBase58();
   return {base58.getCharPtr(), base58.getSize()};
}

////////////////////////////////////////////////////////////////////////////////
// AssetEntry_ArmoryLegacyRoot
AssetEntry_ArmoryLegacyRoot::AssetEntry_ArmoryLegacyRoot(
   Wallets::AssetId id, SecureBinaryData& pubkey,
   std::shared_ptr<Asset_PrivateKey> privkey,
   const SecureBinaryData& chaincode,
   Armory::Seeds::LegacyType seedType):
   AssetEntry_Single(id, pubkey, privkey),
   chaincode_(chaincode), seedType_(seedType)
{}

////////
AssetEntryType AssetEntry_ArmoryLegacyRoot::getType() const
{
   return AssetEntryType::ArmoryLegacyRoot;
}

Armory::Seeds::LegacyType
AssetEntry_ArmoryLegacyRoot::getSeedType() const
{
   return seedType_;
}

////////
const SecureBinaryData& AssetEntry_ArmoryLegacyRoot::getChaincode() const
{
   return chaincode_;
}

////////
BinaryData AssetEntry_ArmoryLegacyRoot::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(ASSETENTRY_LEGACYROOT_VERSION);

   auto entryType = getType();
   bw.put_uint8_t((uint8_t)entryType);

   auto seedType = (uint8_t)getSeedType();
   bw.put_uint8_t(seedType);

   bw.put_var_int(chaincode_.getSize());
   bw.put_BinaryData(chaincode_);

   auto pubkey = getPubKey();
   auto privkey = getPrivKey();

   bw.put_BinaryData(pubkey->serialize());
   if (privkey != nullptr && privkey->hasData()) {
      bw.put_BinaryData(privkey->serialize());
   }

   BinaryWriter finalBw;
   finalBw.put_var_int(bw.getSize());
   finalBw.put_BinaryData(bw.getData());
   return finalBw.getData();
}

////////
std::shared_ptr<AssetEntry_Single> AssetEntry_ArmoryLegacyRoot::getPublicCopy()
{
   auto pubkey = getPubKey()->getUncompressedKey();
   if (pubkey.empty()) {
      throw AssetException("Armory legacy root missing uncompressed pubkey");
   }
   auto woCopy = std::make_shared<AssetEntry_ArmoryLegacyRoot>(
      getID(), pubkey, nullptr, chaincode_, getSeedType());
   return woCopy;
}

////////////////////////////////////////////////////////////////////////////////
// Asset
Asset::Asset(AssetType t) :
   type(t)
{}

Asset::~Asset()
{}

////////////////////////////////////////////////////////////////////////////////
// Asset_PublicKey
Asset_PublicKey::Asset_PublicKey(SecureBinaryData& pubkey) :
   Asset(AssetType::PublicKey)
{
   switch (pubkey.getSize())
   {
      case 33:
      {
         uncompressed_ = Cryptography::ECDSA::uncompressPoint(pubkey);
         compressed_ = std::move(pubkey);
         break;
      }

      case 65:
      {
         uncompressed_ = std::move(pubkey);
         compressed_ = Cryptography::ECDSA::compressPoint(pubkey);
         break;
      }

      default:
         throw AssetException(
            "cannot compress/decompress pubkey of that size");
   }
}

Asset_PublicKey::Asset_PublicKey(SecureBinaryData& uncompressedKey,
   SecureBinaryData& compressedKey) :
   Asset(AssetType::PublicKey),
   uncompressed_(std::move(uncompressedKey)),
   compressed_(std::move(compressedKey))
{
   if (uncompressed_.getSize() != 65 || compressed_.getSize() != 33) {
      throw AssetException("invalid pubkey size");
   }
}

////////
const SecureBinaryData& Asset_PublicKey::getUncompressedKey() const
{
   return uncompressed_;
}

////
const SecureBinaryData& Asset_PublicKey::getCompressedKey() const
{
   return compressed_;
}

////////
BinaryData Asset_PublicKey::serialize() const
{
   BinaryWriter bw;
   if (uncompressed_.getSize() == 65) {
      bw.put_var_int(uncompressed_.getSize() + 5);
      bw.put_uint32_t(PUBKEY_UNCOMPRESSED_VERSION);
      bw.put_uint8_t(PUBKEY_UNCOMPRESSED_BYTE);
      bw.put_BinaryData(uncompressed_);
   }

   if (compressed_.getSize() == 33) {
      bw.put_var_int(compressed_.getSize() + 5);
      bw.put_uint32_t(PUBKEY_COMPRESSED_VERSION);
      bw.put_uint8_t(PUBKEY_COMPRESSED_BYTE);
      bw.put_BinaryData(compressed_);
   }

   if (bw.getSize() == 0) {
      throw AssetException("empty pubkey");
   }
   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
// Asset_PrivateKey
Asset_PrivateKey::Asset_PrivateKey(const Wallets::AssetId& id,
   std::unique_ptr<Wallets::Encryption::CipherData> cipherData) :
   Wallets::Encryption::EncryptedAssetData(std::move(cipherData)),
   Asset(AssetType::PrivateKey), id_(id)
{}

////////
bool Asset_PrivateKey::isSame(
   Wallets::Encryption::EncryptedAssetData* const asset) const
{
   auto asset_ed = dynamic_cast<Asset_PrivateKey*>(asset);
   if (asset_ed == nullptr) {
      return false;
   }
   if (id_ != asset_ed->id_) {
      return false;
   }
   return Wallets::Encryption::EncryptedAssetData::isSame(asset);
}

const Wallets::AssetId& Asset_PrivateKey::getAssetId() const
{
   return id_;
}

////////
BinaryData Asset_PrivateKey::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(PRIVKEY_VERSION);
   bw.put_uint8_t(PRIVKEY_BYTE);
   id_.serializeValue(bw);

   auto&& cipherData = getCipherDataPtr()->serialize();
   bw.put_var_int(cipherData.getSize());
   bw.put_BinaryData(cipherData);

   BinaryWriter finalBw;
   finalBw.put_var_int(bw.getSize());
   finalBw.put_BinaryDataRef(bw.getDataRef());
   return finalBw.getData();
}

std::unique_ptr<Asset_PrivateKey> Asset_PrivateKey::deserializeOld(
   const Wallets::AssetId& id, const BinaryDataRef& data)
{
   BinaryRefReader brr(data);

   //return ptr
   std::unique_ptr<Asset_PrivateKey> assetPtr = nullptr;

   //version
   auto version = brr.get_uint32_t();

   //prefix
   auto prefix = brr.get_uint8_t();

   switch (prefix)
   {
      case PRIVKEY_BYTE:
      {
         switch(version)
         {
            case 0x00000001:
            {
               //id
               auto len = brr.get_var_int();
               auto onDiskId = brr.get_BinaryData(len);

               if (onDiskId.getSize() != 4) {
                  throw std::runtime_error("[Asset_PrivateKey::deserialize]"
                     " invalid id size");
               }

               BinaryRefReader keyRefReader(onDiskId);
               Wallets::AssetKeyType assetKey = keyRefReader.get_int32_t();
               if (id.getAssetKey() != assetKey) {
                  throw std::runtime_error(
                     "[Asset_PrivateKey::deserialize]"
                     " privkey id mismatch");
               }

               //cipher data
               len = brr.get_var_int();
               if (len > brr.getSizeRemaining()) {
                  throw std::runtime_error("[Asset_PrivateKey::deserialize]"
                     " invalid serialized encrypted data len");
               }

               auto cipherBdr = brr.get_BinaryDataRef(len);
               BinaryRefReader cipherBrr(cipherBdr);
               auto cipherData = Wallets::Encryption::CipherData::deserialize(cipherBrr);

               //ptr
               assetPtr = std::make_unique<Asset_PrivateKey>(
                  id, std::move(cipherData));
               break;
            }

            default:
               throw std::runtime_error("[Asset_PrivateKey::deserialize]"
                  "unsupported privkey version");
         }
         break;
      }

      default:
         throw std::runtime_error("unexpected encrypted data prefix");
   }

   if (assetPtr == nullptr) {
      throw std::runtime_error("[Asset_PrivateKey::deserialize]"
         " failed to deserialize encrypted asset");
   }
   return assetPtr;
}

std::unique_ptr<Asset_PrivateKey> Asset_PrivateKey::deserialize(
   const BinaryDataRef& data)
{
   BinaryRefReader brr(data);

   //return ptr
   std::unique_ptr<Asset_PrivateKey> assetPtr = nullptr;

   //version
   auto version = brr.get_uint32_t();

   //prefix
   auto prefix = brr.get_uint8_t();

   switch (prefix)
   {
      case PRIVKEY_BYTE:
      {
         switch (version)
         {
            case 0x00000001:
            case 0x00000002:
            {
               //id
               auto assetId = Wallets::AssetId::deserializeValue(brr);

               //cipher data
               auto len = brr.get_var_int();
               if (len > brr.getSizeRemaining()) {
                  throw std::runtime_error(
                     "invalid serialized encrypted data len");
               }
               auto cipherBdr = brr.get_BinaryDataRef(len);
               BinaryRefReader cipherBrr(cipherBdr);
               auto cipherData = Wallets::Encryption::CipherData::deserialize(cipherBrr);

               //ptr
               assetPtr = std::make_unique<Asset_PrivateKey>(
                  assetId, std::move(cipherData));
               break;
            }

            default:
               throw AssetException(
                  "[Asset_PrivateKey::deserialize] unsupported privkey version");
         }
         break;
      }

   default:
      throw std::runtime_error("unexpected encrypted data prefix");
   }

   if (assetPtr == nullptr) {
      throw std::runtime_error(
         "failed to deserialize encrypted asset");
   }
   return assetPtr;
}

////////////////////////////////////////////////////////////////////////////////
// MetaData
MetaData::MetaData(MetaType type, const BinaryData& accountID, uint32_t index) :
   type_(type), accountID_(accountID), index_(index)
{}

MetaData::~MetaData()
{}

////////
std::shared_ptr<MetaData> MetaData::deserialize(
   const BinaryDataRef& key, const BinaryDataRef& data)
{
   if (key.getSize() != 9) {
      throw AssetException("invalid metadata key size");
   }

   //deser key
   BinaryRefReader brrKey(key);
   auto keyPrefix = brrKey.get_uint8_t();
   auto accountID = brrKey.get_BinaryData(4);
   auto index = brrKey.get_uint32_t(BE);

   //construct object and deser data
   std::shared_ptr<MetaData> resultPtr;
   switch (keyPrefix)
   {
      case METADATA_COMMENTS_PREFIX:
      {
         resultPtr = std::make_shared<CommentData>(accountID, index);
         resultPtr->deserializeDBValue(data);
         break;
      }

      case METADATA_AUTHPEER_PREFIX:
      {
         resultPtr = std::make_shared<PeerPublicData>(accountID, index);
         resultPtr->deserializeDBValue(data);
         break;
      }

      case METADATA_PEERROOT_PREFIX:
      {
         resultPtr = std::make_shared<PeerRootKey>(accountID, index);
         resultPtr->deserializeDBValue(data);
         break;
      }

      case METADATA_ROOTSIG_PREFIX:
      {
         resultPtr = std::make_shared<PeerRootSignature>(accountID, index);
         resultPtr->deserializeDBValue(data);
         break;
      }

      case METADATA_PEERMASTER_PREFIX:
      {
         resultPtr = std::make_shared<PeerMasterKey>(accountID, index);
         resultPtr->deserializeDBValue(data);
         break;
      }

      default:
         throw AssetException("unexpected metadata prefix");
   }
   return resultPtr;
}

////////
bool MetaData::needsCommit()
{
   return needsCommit_;
}

void MetaData::flagForCommit()
{
   needsCommit_ = true;
}

////////
MetaType MetaData::type() const
{
   return type_;
}

const BinaryData& MetaData::getAccountID() const
{
   return accountID_;
}

uint32_t MetaData::getIndex() const
{
   return index_;
}

////////////////////////////////////////////////////////////////////////////////
// PeerPublicData
PeerPublicData::PeerPublicData(const BinaryData& accountID, uint32_t index) :
   MetaData(MetaType::AuthorizedPeer, accountID, index)
{}

////////
BinaryData PeerPublicData::getDbKey() const
{
   if (accountID_.getSize() != 4) {
      throw AssetException("invalid accountID");
   }

   BinaryWriter bw;
   bw.put_uint8_t(METADATA_AUTHPEER_PREFIX);
   bw.put_BinaryData(accountID_);
   bw.put_uint32_t(index_, BE);
   return bw.getData();
}

////////
BinaryData PeerPublicData::serialize() const
{
   //returning an empty serialized string will cause the key to be deleted
   if (names_.empty()) {
      return {};
   }

   BinaryWriter bw;
   bw.put_uint32_t(PEER_PUBLICDATA_VERSION);
   bw.put_var_int(publicKey_.getSize());
   bw.put_BinaryData(publicKey_);

   bw.put_var_int(names_.size());
   for (const auto& name : names_) {
      bw.put_var_int(name.size());

      BinaryDataRef bdrName;
      bdrName.setRef(name);
      bw.put_BinaryDataRef(bdrName);
   }
   bw.put_var_int(label_.size());
   bw.put_String(label_);
   bw.put_uint8_t((uint8_t)oneWay_);

   BinaryWriter bwWithSize;
   bwWithSize.put_var_int(bw.getSize());
   bwWithSize.put_BinaryDataRef(bw.getDataRef());
   return bwWithSize.getData();
}

void PeerPublicData::deserializeDBValue(const BinaryDataRef& data)
{
   BinaryRefReader brrData(data);

   auto len = brrData.get_var_int();
   if (len != brrData.getSizeRemaining()) {
      throw AssetException("size mismatch in metadata entry");
   }
   auto version = brrData.get_uint32_t();

   auto getKey = [&brrData]()->SecureBinaryData
   {
      auto keyLen = brrData.get_var_int();
      return SecureBinaryData{brrData.get_BinaryDataRef(keyLen)};
   };

   auto getNames = [&brrData]()->std::set<std::string>
   {
      std::set<std::string> result;
      auto count = brrData.get_var_int();
      for (unsigned i = 0; i < count; i++) {
         auto nameLen = brrData.get_var_int();
         auto bdrName = brrData.get_BinaryDataRef(nameLen);
         result.emplace(std::string{(char*)bdrName.getPtr(), nameLen});
      }
      return result;
   };

   switch (version)
   {
      case 0x00000001:
      {
         publicKey_ = getKey();
         names_ = getNames();
         break;
      }

      case 0x00000002:
      {
         publicKey_ = getKey();
         names_ = getNames();

         //v2 carries a label per key and mode
         auto labelLen = brrData.get_var_int();
         label_ = brrData.get_String(labelLen);
         oneWay_ = bool(brrData.get_uint8_t());
         break;
      }

   default:
      throw AssetException("unsupported peer data version");
   }

   if (!Cryptography::ECDSA::verifyPublicKeyValid(publicKey_)) {
      throw AssetException("invalid pubkey in peer metadata");
   }
}

////////
void PeerPublicData::setPublicKey(const SecureBinaryData& key)
{
   publicKey_ = key;
   flagForCommit();
}

void PeerPublicData::setLabel(const std::string& label)
{
   label_ = label;
   flagForCommit();
}

void PeerPublicData::setOneWay(bool val)
{
   oneWay_ = val;
}

void PeerPublicData::addName(const std::string& name)
{
   names_.emplace(name);
   flagForCommit();
}

bool PeerPublicData::eraseName(const std::string& name)
{
   auto iter = names_.find(name);
   if (iter == names_.end()) {
      return false;
   }
   names_.erase(iter);
   flagForCommit();
   return true;
}

void PeerPublicData::clear()
{
   names_.clear();
   label_.clear();
   flagForCommit();
}

////////
std::shared_ptr<MetaData> PeerPublicData::copy() const
{
   auto copyPtr = std::make_shared<PeerPublicData>(
      getAccountID(), getIndex());
   copyPtr->names_ = names_;
   copyPtr->publicKey_ = publicKey_;
   copyPtr->label_ = label_;
   copyPtr->oneWay_ = oneWay_;
   return copyPtr;
}

////////
const std::set<std::string>& PeerPublicData::getNames() const
{
   return names_;
}

const SecureBinaryData& PeerPublicData::getPublicKey() const
{
   return publicKey_;
}

const std::string& PeerPublicData::getLabel() const
{
   return label_;
}

bool PeerPublicData::oneWay() const
{
   return oneWay_;
}

////////////////////////////////////////////////////////////////////////////////
// PeerRootKey
PeerRootKey::PeerRootKey(const BinaryData& accountID, uint32_t index) :
   MetaData(MetaType::PeerRootKey, accountID, index)
{}

////////
BinaryData PeerRootKey::getDbKey() const
{
   if (accountID_.getSize() != 4) {
      throw AssetException("invalid accountID");
   }

   BinaryWriter bw;
   bw.put_uint8_t(METADATA_PEERROOT_PREFIX);
   bw.put_BinaryData(accountID_);
   bw.put_uint32_t(index_, BE);
   return bw.getData();
}

////////
BinaryData PeerRootKey::serialize() const
{
   //returning an empty serialized string will cause the key to be deleted
   if (publicKey_.empty()) {
      return BinaryData();
   }

   BinaryWriter bw;
   bw.put_uint32_t(PEER_ROOTKEY_VERSION);
   bw.put_var_int(publicKey_.getSize());
   bw.put_BinaryData(publicKey_);

   bw.put_var_int(description_.size());
   if (!description_.empty()) {
      BinaryDataRef descBdr;
      descBdr.setRef(description_);
      bw.put_BinaryDataRef(descBdr);
   }

   BinaryWriter bwWithSize;
   bwWithSize.put_var_int(bw.getSize());
   bwWithSize.put_BinaryDataRef(bw.getDataRef());
   return bwWithSize.getData();
}

void PeerRootKey::deserializeDBValue(const BinaryDataRef& data)
{
   BinaryRefReader brrData(data);
   auto len = brrData.get_var_int();
   if (len != brrData.getSizeRemaining()) {
      throw AssetException("size mismatch in metadata entry");
   }
   auto version = brrData.get_uint32_t();

   switch (version)
   {
      case 0x00000001:
      {
         auto keyLen = brrData.get_var_int();
         publicKey_ = SecureBinaryData{brrData.get_BinaryDataRef(keyLen)};

         //check pubkey is valid
         if (!Cryptography::ECDSA::verifyPublicKeyValid(publicKey_))
            throw AssetException("invalid pubkey in peer metadata");

         auto descLen = brrData.get_var_int();
         if (descLen == 0) {
            return;
         }
         auto descBdr = brrData.get_BinaryDataRef(descLen);
         description_ = std::string{descBdr.toCharPtr(), descBdr.getSize()};
         break;
      }

      default:
         throw AssetException("unsupported peer rootkey version");
   }
}

////////
void PeerRootKey::clear()
{
   publicKey_.clear();
   description_.clear();
   flagForCommit();
}

void PeerRootKey::set(const std::string& desc, const SecureBinaryData& key)
{
   if (!publicKey_.empty()) {
      throw AssetException("peer root key already set");
   }
   if (!Cryptography::ECDSA::verifyPublicKeyValid(key)) {
      throw AssetException("invalid pubkey for peer root");
   }
   publicKey_ = key;
   description_ = desc;
}

////////
std::shared_ptr<MetaData> PeerRootKey::copy() const
{
   auto copyPtr = std::make_shared<PeerRootKey>(getAccountID(), getIndex());
   copyPtr->publicKey_ = publicKey_;
   copyPtr->description_ = description_;
   return copyPtr;
}

////////
const SecureBinaryData& PeerRootKey::getKey() const
{
   return publicKey_;
}

const std::string& PeerRootKey::getDescription() const
{
   return description_;
}

////////////////////////////////////////////////////////////////////////////////
// PeerRootSignature
PeerRootSignature::PeerRootSignature(
   const BinaryData& accountID, unsigned index) :
   MetaData(MetaType::PeerRootSig, accountID, index)
{}

////////
const SecureBinaryData& PeerRootSignature::getKey() const
{
   return publicKey_;
}

const SecureBinaryData& PeerRootSignature::getSig() const
{
   return signature_;
}

////////
BinaryData PeerRootSignature::getDbKey() const
{
   if (accountID_.getSize() != 4) {
      throw AssetException("invalid accountID");
   }

   BinaryWriter bw;
   bw.put_uint8_t(METADATA_ROOTSIG_PREFIX);
   bw.put_BinaryData(accountID_);
   bw.put_uint32_t(index_, BE);
   return bw.getData();
}

BinaryData PeerRootSignature::serialize() const
{
   //returning an empty serialized string will cause the key to be deleted
   if (publicKey_.empty()) {
      return {};
   }

   BinaryWriter bw;
   bw.put_uint32_t(PEER_ROOTSIG_VERSION);
   bw.put_var_int(publicKey_.getSize());
   bw.put_BinaryData(publicKey_);

   bw.put_var_int(signature_.getSize());
   bw.put_BinaryData(signature_);

   BinaryWriter bwWithSize;
   bwWithSize.put_var_int(bw.getSize());
   bwWithSize.put_BinaryDataRef(bw.getDataRef());
   return bwWithSize.getData();
}

void PeerRootSignature::deserializeDBValue(const BinaryDataRef& data)
{
   BinaryRefReader brrData(data);
   auto len = brrData.get_var_int();
   if (len != brrData.getSizeRemaining()) {
      throw AssetException("size mismatch in metadata entry");
   }
   auto version = brrData.get_uint32_t();

   switch (version)
   {
      case 0x00000001:
      {
         auto keyLen = brrData.get_var_int();
         publicKey_ = SecureBinaryData{brrData.get_BinaryDataRef(keyLen)};

         //check pubkey is valid
         if (!Cryptography::ECDSA::verifyPublicKeyValid(publicKey_)) {
            throw AssetException("invalid pubkey in peer metadata");
         }
         len = brrData.get_var_int();
         signature_ = SecureBinaryData{brrData.get_BinaryDataRef(len)};
         break;
      }

      default:
         throw AssetException("unsupported peer rootsig version");
   }

   //cannot check sig is valid until full peer account is loaded
}

////////
void PeerRootSignature::clear()
{
   publicKey_.clear();
   signature_.clear();
   flagForCommit();
}

void PeerRootSignature::set(
   const SecureBinaryData& key, const SecureBinaryData& sig)
{
   if (!publicKey_.empty()) {
      throw AssetException("peer root key already set");
   }

   //check pubkey and sig prior to calling this
   publicKey_ = key;
   signature_ = sig;
}

////////
std::shared_ptr<MetaData> PeerRootSignature::copy() const
{
   auto copyPtr = std::make_shared<PeerRootSignature>(
      getAccountID(), getIndex());
   copyPtr->publicKey_ = publicKey_;
   copyPtr->signature_ = signature_;
   return copyPtr;
}

////////////////////////////////////////////////////////////////////////////////
// PeerMasterKey
PeerMasterKey::PeerMasterKey(
   const BinaryData& accountID, unsigned index) :
   MetaData(MetaType::PeerMasterKey, accountID, index)
{}

////////
const SecureBinaryData& PeerMasterKey::getKey() const
{
   return key_;
}

////////
BinaryData PeerMasterKey::getDbKey() const
{
   if (accountID_.getSize() != 4) {
      throw AssetException("invalid accountID");
   }

   BinaryWriter bw;
   bw.put_uint8_t(METADATA_PEERMASTER_PREFIX);
   bw.put_BinaryData(accountID_);
   bw.put_uint32_t(index_, BE);
   return bw.getData();
}

BinaryData PeerMasterKey::serialize() const
{
   //returning an empty serialized string will cause the key to be deleted
   if (key_.empty()) {
      return {};
   }

   BinaryWriter bw;
   bw.put_uint32_t(PEER_MASTERKEY_VERSION);
   bw.put_var_int(key_.getSize());
   bw.put_BinaryData(key_);

   BinaryWriter bwWithSize;
   bwWithSize.put_var_int(bw.getSize());
   bwWithSize.put_BinaryDataRef(bw.getDataRef());
   return bwWithSize.getData();
}

void PeerMasterKey::deserializeDBValue(const BinaryDataRef& data)
{
   BinaryRefReader brrData(data);
   auto len = brrData.get_var_int();
   if (len != brrData.getSizeRemaining()) {
      throw AssetException("size mismatch in metadata entry");
   }
   auto version = brrData.get_uint32_t();

   switch (version)
   {
      case 0x00000001:
      {
         auto keyLen = brrData.get_var_int();
         key_ = SecureBinaryData{brrData.get_BinaryDataRef(keyLen)};

         //check pubkey is valid
         if (!Cryptography::ECDSA::verifyPublicKeyValid(key_)) {
            throw AssetException("invalid peer master key");
         }
         break;
      }

      default:
         throw AssetException("unsupported peer master key version");
   }
}

////////
void PeerMasterKey::clear()
{
   key_.clear();
   flagForCommit();
}

void PeerMasterKey::set(const SecureBinaryData& key)
{
   if (!key_.empty()) {
      throw AssetException("peer master key already set");
   }

   //check pubkey and sig prior to calling this
   key_ = key;
}

////////
std::shared_ptr<MetaData> PeerMasterKey::copy() const
{
   auto copyPtr = std::make_shared<PeerMasterKey>(
      getAccountID(), getIndex());
   copyPtr->key_ = key_;
   return copyPtr;
}

////////////////////////////////////////////////////////////////////////////////
// CommentData
CommentData::CommentData(const BinaryData& accountID, uint32_t index) :
   MetaData(MetaType::AuthorizedPeer, accountID, index)
{}

////////
const std::string& CommentData::getValue(void) const
{
   return commentStr_;
}

void CommentData::setValue(const std::string& val)
{
   commentStr_ = val;
}

////////
const BinaryData& CommentData::getKey() const
{
   return key_;
}

void CommentData::setKey(const BinaryData& val)
{
   key_ = val;
}

////////
BinaryData CommentData::getDbKey() const
{
   if (accountID_.getSize() != 4) {
      throw AssetException("invalid accountID");
   }

   BinaryWriter bw;
   bw.put_uint8_t(METADATA_COMMENTS_PREFIX);
   bw.put_BinaryData(accountID_);
   bw.put_uint32_t(index_, BE);
   return bw.getData();
}

BinaryData CommentData::serialize() const
{
   //returning an empty serialized string will cause the key to be deleted
   if (commentStr_.empty()) {
      return {};
   }

   BinaryWriter bw;
   bw.put_uint32_t(COMMENT_DATA_VERSION);
   bw.put_var_int(key_.getSize());
   bw.put_BinaryData(key_);

   bw.put_var_int(commentStr_.size());
   bw.put_String(commentStr_);

   BinaryWriter bwWithSize;
   bwWithSize.put_var_int(bw.getSize());
   bwWithSize.put_BinaryDataRef(bw.getDataRef());
   return bwWithSize.getData();
}

void CommentData::deserializeDBValue(const BinaryDataRef& data)
{
   BinaryRefReader brrData(data);
   auto len = brrData.get_var_int();
   if (len != brrData.getSizeRemaining()) {
      throw AssetException("size mismatch in metadata entry");
   }

   auto version = brrData.get_uint32_t();
   switch (version)
   {
      case 0x00000001:
      {
         len = brrData.get_var_int();
         key_ = brrData.get_BinaryData(len);

         len = brrData.get_var_int();
         commentStr_ = brrData.get_String(len);
         break;
      }

      default:
         throw AssetException("unsupported comment version");
   }
}

////////
void CommentData::clear()
{
   commentStr_.clear();
   flagForCommit();
}

////////
std::shared_ptr<MetaData> CommentData::copy() const
{
   auto copyPtr = std::make_shared<CommentData>(getAccountID(), getIndex());
   copyPtr->commentStr_ = commentStr_;
   copyPtr->key_ = key_;
   return copyPtr;
}
