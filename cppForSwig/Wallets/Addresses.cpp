////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Addresses.h"
#include <Utils/ArmoryConfig.h>
#include <Utils/BtcUtils.h>
#include <Utils/OpCodes.h>
#include <Signer/ScriptRecipient.h>
#include "Assets.h"


using namespace Armory;

////////////////////////////////////////////////////////////////////////////////
// exceptions
AddressException::AddressException(const std::string& err) :
   std::runtime_error(err)
{}

////////////////////////////////////////////////////////////////////////////////
// AddressEntry
AddressEntry::AddressEntry(AddressEntryType aetype) :
   type_(aetype)
{}

AddressEntry::~AddressEntry()
{}

AddressEntryType AddressEntry::getType() const
{
   return type_;
}

size_t AddressEntry::getWitnessDataSize() const
{
   throw std::runtime_error("no witness data");
}

////////////////////////////////////////////////////////////////////////////////
AddressEntry_WithAsset::AddressEntry_WithAsset(
   std::shared_ptr<Assets::AssetEntry> asset,
   bool isCompressed) :
   asset_(asset), isCompressed_(isCompressed)
{}

AddressEntry_WithAsset::~AddressEntry_WithAsset()
{}

const std::shared_ptr<Assets::AssetEntry>
AddressEntry_WithAsset::getAsset() const
{
   return asset_;
}

bool AddressEntry_WithAsset::isCompressed() const
{
   return isCompressed_;
}

////////////////////////////////////////////////////////////////////////////////
AddressEntry_Nested::AddressEntry_Nested(
   std::shared_ptr<AddressEntry> addrPtr) :
   addrPtr_(addrPtr)
{
   if (addrPtr_ == nullptr) {
      throw AddressException("empty predecessor");
   }
}

AddressEntry_Nested::~AddressEntry_Nested()
{}

std::shared_ptr<AddressEntry> AddressEntry_Nested::getPredecessor() const
{
   return addrPtr_;
}

////////////////////////////////////////////////////////////////////////////////
// P2PKH
AddressEntry_P2PKH::AddressEntry_P2PKH(
   std::shared_ptr<Assets::AssetEntry> asset,
   bool isCompressed) :
   AddressEntry(WITH_COMPRESSED_FLAG(AddressEntryType::P2PKH, isCompressed)),
   AddressEntry_WithAsset(asset, isCompressed)
{
   switch (asset->getType()) {
      case Assets::AssetEntryType::Single:
      case Assets::AssetEntryType::ScriptHash:
         break;

      default:
         throw AddressException("[AddressEntry_P2PKH] unexpected asset type");
   }
}

const BinaryData& AddressEntry_P2PKH::getPreimage() const
{
   auto assetSingle =
      std::dynamic_pointer_cast<Assets::AssetEntry_Single>(getAsset());
   if (assetSingle == nullptr) {
      throw AddressException("unexpected asset entry type");
   }

   if (isCompressed()) {
      return assetSingle->getPubKey()->getCompressedKey();
   } else {
      return assetSingle->getPubKey()->getUncompressedKey();
   }
}

const BinaryData& AddressEntry_P2PKH::getHash() const
{
   if (hash_.empty()) {
      switch (getAsset()->getType())
      {
      case Assets::AssetEntryType::ScriptHash:
      {
         auto assetSingle =
            std::dynamic_pointer_cast<Assets::AssetEntry_ScriptHash>(getAsset());
         if (assetSingle == nullptr) {
            throw AddressException("expected script hash asset");
         }
         hash_ = BinaryData{assetSingle->getScriptHash()};
         break;
      }

      default:
         const auto& preimage = getPreimage();
         auto hash1 = BtcUtils::getHash160(preimage);
         auto hash2 = BtcUtils::getHash160(preimage);

         if (hash1 != hash2) {
            throw AddressException("failed to hash preimage");
         }
         hash_ = hash1;
      }
   }
   return hash_;
}

const BinaryData& AddressEntry_P2PKH::getPrefixedHash() const
{
   if (prefixedHash_.empty()) {
      const auto& hash = getHash();

      //get and prepend network byte
      auto networkByte = Config::BitcoinSettings::getPubkeyHashPrefix();
      prefixedHash_.append(networkByte);
      prefixedHash_.append(hash);
   }
   return prefixedHash_;
}

const std::string& AddressEntry_P2PKH::getAddress() const
{
   if (address_.empty()) {
      address_ = std::move(BtcUtils::scrAddrToBase58(getPrefixedHash()));
   }
   return address_;
}

std::shared_ptr<Signing::ScriptRecipient> AddressEntry_P2PKH::getRecipient(
   uint64_t value) const
{
   const auto& hash = getHash();
   return std::make_shared<Signing::Recipient_P2PKH>(hash, value);
}

size_t AddressEntry_P2PKH::getInputSize() const
{
   size_t size = 114; //outpoint, sequence and sig + varint overhead
   if (isCompressed()) {
      size += 33;
   } else {
      size += 65;
   }
   return size;
}

const Wallets::AssetId& AddressEntry_P2PKH::getID() const
{
   return getAsset()->getID();
}

const BinaryData& AddressEntry_P2PKH::getScript() const
{
   if (script_.empty()) {
      const auto& hash = getHash();
      script_ = std::move(BtcUtils::getP2PKHScript(hash));
   }
   return script_;
}

////////////////////////////////////////////////////////////////////////////////
// P2PK
AddressEntry_P2PK::AddressEntry_P2PK(
   std::shared_ptr<Assets::AssetEntry> asset,
   bool isCompressed) :
   AddressEntry(WITH_COMPRESSED_FLAG(AddressEntryType::P2PK, isCompressed)),
   AddressEntry_WithAsset(asset, isCompressed)
{
   auto asset_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(
      asset);
   if (asset_single == nullptr) {
      throw AddressException("[AddressEntry_P2PK] unexpected asset type");
   }
}

const BinaryData& AddressEntry_P2PK::getPreimage() const
{
   auto asset_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(
      getAsset());
   if (asset_single == nullptr) {
      throw AddressException("invalid asset entry type");
   }

   if (isCompressed()) {
      return asset_single->getPubKey()->getCompressedKey();
   } else {
      return asset_single->getPubKey()->getUncompressedKey();
   }
}

const BinaryData& AddressEntry_P2PK::getHash() const
{
   throw AddressException("native P2PK doesnt come hashed");
}

const BinaryData& AddressEntry_P2PK::getPrefixedHash() const
{
   throw AddressException("native P2PK doesnt come hashed");
}

const std::string& AddressEntry_P2PK::getAddress() const
{
   throw AddressException("native P2PK doesnt have an address format");
}

std::shared_ptr<Signing::ScriptRecipient> AddressEntry_P2PK::getRecipient(
   uint64_t value) const
{
   const auto& preimage = getPreimage();
   return std::make_shared<Signing::Recipient_P2PK>(preimage, value);
}

size_t AddressEntry_P2PK::getInputSize() const
{
   size_t size = 114; //outpoint, sequence and sig + varint overhead
   if (isCompressed()) {
      size += 33;
   } else {
      size += 65;
   }
   return size;
}

const Wallets::AssetId& AddressEntry_P2PK::getID() const
{
   return getAsset()->getID();
}

const BinaryData& AddressEntry_P2PK::getScript() const
{
   if (script_.empty()) {
      const auto& preimage = getPreimage();
      script_ = std::move(BtcUtils::getP2PKScript(preimage));
   }
   return script_;
}

////////////////////////////////////////////////////////////////////////////////
// P2WPKH
AddressEntry_P2WPKH::AddressEntry_P2WPKH(
   std::shared_ptr<Assets::AssetEntry> asset) :
   AddressEntry(AddressEntryType::P2WPKH),
   AddressEntry_WithAsset(asset, true)
{
   auto asset_single = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(
      asset);
   if (asset_single == nullptr) {
      throw AddressException("[AddressEntry_P2WPKH] unexpected asset type");
   }
}

const BinaryData& AddressEntry_P2WPKH::getPreimage() const
{
   auto assetSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(
      getAsset());
   if (assetSingle == nullptr) {
      throw AddressException("unexpected asset entry type");
   }
   return assetSingle->getPubKey()->getCompressedKey();
}

const BinaryData& AddressEntry_P2WPKH::getHash() const
{
   if (hash_.empty()) {
      const auto& preimage = getPreimage();
      auto hash1 = BtcUtils::getHash160(preimage);
      auto hash2 = BtcUtils::getHash160(preimage);
      if (hash1 != hash2) {
         throw AddressException("failed to hash preimage");
      }
      hash_ = hash1;
   }
   return hash_;
}

const BinaryData& AddressEntry_P2WPKH::getPrefixedHash() const
{
   if (prefixedHash_.empty()) {
      const auto& hash = getHash();

      //get and prepend network byte
      auto networkByte = uint8_t(ScriptPrefix::P2WPKH);
      prefixedHash_.append(networkByte);
      prefixedHash_.append(hash);
   }
   return prefixedHash_;
}

const std::string& AddressEntry_P2WPKH::getAddress() const
{
   //prefixed has for SW is only for the db, using plain hash for SW
   if (address_.empty()) {
      address_ = std::move(BtcUtils::scrAddrToSegWitAddress(getHash()));
   }
   return address_;
}

std::shared_ptr<Signing::ScriptRecipient> AddressEntry_P2WPKH::getRecipient(
   uint64_t value) const
{
   const auto& hash = getHash();
   return std::make_shared<Signing::Recipient_P2WPKH>(hash, value);
}

const Wallets::AssetId& AddressEntry_P2WPKH::getID() const
{
   return getAsset()->getID();
}

const BinaryData& AddressEntry_P2WPKH::getScript() const
{
   if (script_.empty()) {
      const auto& hash = getHash();
      script_ = std::move(BtcUtils::getP2WPKHOutputScript(hash));
   }
   return script_;
}

size_t AddressEntry_P2WPKH::getInputSize() const
{
   return 40;
}

size_t AddressEntry_P2WPKH::getWitnessDataSize() const
{
   return 108;
}

////////////////////////////////////////////////////////////////////////////////
// Multisig
AddressEntry_Multisig::AddressEntry_Multisig(
   std::shared_ptr<Assets::AssetEntry> asset,
   bool compressed) :
   AddressEntry(WITH_COMPRESSED_FLAG(AddressEntryType::Multisig, compressed)),
   AddressEntry_WithAsset(asset, compressed)
{
   auto asset_ms = std::dynamic_pointer_cast<Assets::AssetEntry_Multisig>(
      asset);
   if (asset_ms == nullptr) {
      throw AddressException("[AddressEntry_Multisig] unexpected asset type");
   }
}

const BinaryData& AddressEntry_Multisig::getPreimage() const
{
   throw AddressException("native multisig scripts do not come hashed");
}

const BinaryData& AddressEntry_Multisig::getHash() const
{
   throw AddressException("native multisig scripts do not come hashed");
}

const BinaryData& AddressEntry_Multisig::getPrefixedHash() const
{
   throw AddressException("native multisig scripts do not come hashed");
}

const std::string& AddressEntry_Multisig::getAddress() const
{
   throw AddressException("no address format for native multisig");
}

std::shared_ptr<Signing::ScriptRecipient> AddressEntry_Multisig::getRecipient(
   uint64_t value) const
{
   const auto& script = getScript();
   return std::make_shared<Signing::Recipient_Universal>(script, value);
}

size_t AddressEntry_Multisig::getInputSize() const
{
   switch (getAsset()->getType())
   {
      case Assets::AssetEntryType::Multisig:
      {
         auto assetMS = std::dynamic_pointer_cast<Assets::AssetEntry_Multisig>(
            getAsset());
         if (assetMS == nullptr) {
            throw AddressException("unexpected asset entry type");
         }

         auto m = assetMS->getM();
         const auto& script = getScript();
         size_t size = script.getSize() + 2;
         size += 73 * m + 40; //m sigs + outpoint
         return size;
      }

      default:
         throw AddressException("unexpected asset type");
   }
   return SIZE_MAX;
}

const Wallets::AssetId& AddressEntry_Multisig::getID() const
{
   return getAsset()->getID();
}

const BinaryData& AddressEntry_Multisig::getScript() const
{
   if (script_.empty()) {
      auto asset_ms = std::dynamic_pointer_cast<Assets::AssetEntry_Multisig>(
         getAsset());
      if (asset_ms == nullptr) {
         throw AddressException("invalid asset entry type");
      }

      //convert m to opcode and push
      auto m = asset_ms->getM() + OP_1 - 1;
      if (m > OP_16) {
         throw Assets::AssetException("m exceeds OP_16");
      }
      BinaryWriter bw;
      bw.put_uint8_t(m);

      //put pub keys
      for (const auto& asset : asset_ms->getAssetMap()) {
         auto assetSingle = std::dynamic_pointer_cast<Assets::AssetEntry_Single>(
            asset.second);

         if (assetSingle == nullptr) {
            throw Assets::AssetException("unexpected asset entry type");
         }

         if (isCompressed()) {
            bw.put_uint8_t(33);
            bw.put_BinaryData(assetSingle->getPubKey()->getCompressedKey());
         } else {
            bw.put_uint8_t(65);
            bw.put_BinaryData(assetSingle->getPubKey()->getUncompressedKey());
         }
      }

      //convert n to opcode and push
      auto n = asset_ms->getN() + OP_1 - 1;
      if (n > OP_16 || n < m) {
         throw Assets::AssetException("invalid n");
      }
      bw.put_uint8_t(n);
      bw.put_uint8_t(OP_CHECKMULTISIG);
      script_ = bw.getData();
   }
   return script_;
}

////////////////////////////////////////////////////////////////////////////////
// ScriptHash
AddressEntry_ScriptHash::AddressEntry_ScriptHash(
   std::shared_ptr<Assets::AssetEntry> assetPtr) :
   AddressEntry(AddressEntryType::ScriptHash),
   AddressEntry_WithAsset(assetPtr, false)
{
   if (assetPtr->getType() != Assets::AssetEntryType::ScriptHash) {
      throw AddressException("expected ScriptHash asset");
   }
}

const Wallets::AssetId& AddressEntry_ScriptHash::getID() const
{
   return getAsset()->getID();
}

const std::string& AddressEntry_ScriptHash::getAddress() const
{
   throw AddressException("invalid for AddressEntry_ScriptHash");
}

std::shared_ptr<Signing::ScriptRecipient>
AddressEntry_ScriptHash::getRecipient(uint64_t) const
{
   throw AddressException("invalid for AddressEntry_ScriptHash");
}

const BinaryData& AddressEntry_ScriptHash::getHash() const
{
   auto assetSingle =
      std::dynamic_pointer_cast<Assets::AssetEntry_ScriptHash>(getAsset());
   return assetSingle->getScriptHash();
}

const BinaryData& AddressEntry_ScriptHash::getPrefixedHash() const
{
   throw AddressException("invalid for AddressEntry_ScriptHash");
}

const BinaryData& AddressEntry_ScriptHash::getPreimage() const
{
   throw AddressException("invalid for AddressEntry_ScriptHash");
}

const BinaryData& AddressEntry_ScriptHash::getScript() const
{
   throw AddressException("invalid for AddressEntry_ScriptHash");
}

size_t AddressEntry_ScriptHash::getInputSize() const
{
   throw AddressException("invalid for AddressEntry_ScriptHash");
}

////////////////////////////////////////////////////////////////////////////////
// RawScript
AddressEntry_RawScript::AddressEntry_RawScript(
   std::shared_ptr<Assets::AssetEntry> assetPtr) :
   AddressEntry(AddressEntryType::RawScript),
   AddressEntry_WithAsset(assetPtr, false)
{
   if (assetPtr->getType() != Assets::AssetEntryType::RawScript) {
      throw AddressException("expected RawScript asset");
   }
}

const Wallets::AssetId& AddressEntry_RawScript::getID() const
{
   return getAsset()->getID();
}

const std::string& AddressEntry_RawScript::getAddress() const
{
   throw AddressException("invalid for AddressEntry_RawScript");
}

std::shared_ptr<Signing::ScriptRecipient>
AddressEntry_RawScript::getRecipient(uint64_t amount) const
{
   return std::make_shared<Signing::Recipient_Universal>(
      getScript(), amount);
}

const BinaryData& AddressEntry_RawScript::getHash() const
{
   throw AddressException("invalid for AddressEntry_RawScript");
}

const BinaryData& AddressEntry_RawScript::getPrefixedHash() const
{
   throw AddressException("invalid for AddressEntry_RawScript");
}

const BinaryData& AddressEntry_RawScript::getPreimage() const
{
   throw AddressException("invalid for AddressEntry_RawScript");
}

const BinaryData& AddressEntry_RawScript::getScript() const
{
   auto assetSingle =
      std::dynamic_pointer_cast<Assets::AssetEntry_RawScript>(getAsset());
   return assetSingle->getScript();
}

size_t AddressEntry_RawScript::getInputSize() const
{
   throw AddressException("invalid for AddressEntry_RawScript");
}

////////////////////////////////////////////////////////////////////////////////
// P2SH
AddressEntry_P2SH::AddressEntry_P2SH(std::shared_ptr<AddressEntry> addrPtr) :
   AddressEntry(AddressEntryType::P2SH), AddressEntry_Nested(addrPtr)
{
   if (addrPtr->getType() & AddressEntryType::P2SH) {
      throw AddressException("cannot nest P2SH in P2SH");
   }
}

const BinaryData& AddressEntry_P2SH::getPreimage() const
{
   if (getPredecessor() == nullptr) {
      throw AddressException("missing predecessor");
   }
   return getPredecessor()->getScript();
}

const BinaryData& AddressEntry_P2SH::getHash() const
{
   if (hash_.empty()) {
      auto predecessor = getPredecessor();
      switch (predecessor->getType())
      {
         case AddressEntryType::ScriptHash:
         {
            hash_ = predecessor->getHash();
            break;
         }

         default:
            const auto& preimage = getPreimage();
            auto hash1 = BtcUtils::getHash160(preimage);
            auto hash2 = BtcUtils::getHash160(preimage);

            if (hash1 != hash2) {
               throw AddressException("failed to hash preimage");
            }
            hash_ = hash1;
      }
   }
   return hash_;
}

const BinaryData& AddressEntry_P2SH::getPrefixedHash() const
{
   if (prefixedHash_.empty()) {
      const auto& hash = getHash();
      BinaryWriter bw;
      bw.put_uint8_t(Config::BitcoinSettings::getScriptHashPrefix());
      bw.put_BinaryData(hash);
      prefixedHash_ = bw.getData();
   }
   return prefixedHash_;
}

const Wallets::AssetId& AddressEntry_P2SH::getID() const
{
   if (getPredecessor() == nullptr) {
      throw AddressException("missing predecessor");
   }
   return getPredecessor()->getID();
}

size_t AddressEntry_P2SH::getInputSize() const
{
   return getPredecessor()->getScript().getSize();
}

std::shared_ptr<Signing::ScriptRecipient> AddressEntry_P2SH::getRecipient(
   uint64_t value) const
{
   const auto& hash = getHash();
   return std::make_shared<Signing::Recipient_P2SH>(hash, value);
}

const BinaryData& AddressEntry_P2SH::getScript() const
{
   if (script_.empty()) {
      const auto& hash = getHash();
      script_ = std::move(BtcUtils::getP2SHScript(hash));
   }
   return script_;
}

const std::string& AddressEntry_P2SH::getAddress() const
{
   if (address_.empty()) {
      address_ = std::move(BtcUtils::scrAddrToBase58(getPrefixedHash()));
   }
   return address_;
}

AddressEntryType AddressEntry_P2SH::getType() const
{
   auto nestedType = AddressEntry::getType();
   auto baseType = getPredecessor()->getType();
   return AddressEntryType(baseType | nestedType);
}

////////////////////////////////////////////////////////////////////////////////
// P2WSH
AddressEntry_P2WSH::AddressEntry_P2WSH(std::shared_ptr<AddressEntry> addrPtr) :
   AddressEntry(AddressEntryType::P2WSH), AddressEntry_Nested(addrPtr)
{
   auto addrType = addrPtr->getType() & ADDRESS_TYPE_MASK;
   if (addrType == AddressEntryType::P2WPKH) {
      throw AddressException("cannot nest SW in P2WSH");
   }
   if (addrPtr->getType() & AddressEntryType::P2WSH) {
      throw AddressException("cannot nest P2WSH in P2WSH");
   }
}

const BinaryData& AddressEntry_P2WSH::getPreimage() const
{
   if (getPredecessor() == nullptr) {
      throw AddressException("missing predecessor");
   }
   return getPredecessor()->getScript();
}

const BinaryData& AddressEntry_P2WSH::getHash() const
{
   if (hash_.empty()) {
      const auto& preimage = getPreimage();
      auto hash1 = BtcUtils::getSha256(preimage);
      auto hash2 = BtcUtils::getSha256(preimage);

      if (hash1 != hash2) {
         throw AddressException("failed to compute hash");
      }
      hash_ = hash1;
   }
   return hash_;
}

const BinaryData& AddressEntry_P2WSH::getPrefixedHash() const
{
   if (prefixedHash_.empty()) {
      const auto& hash = getHash();

      //get and prepend network byte
      auto networkByte = uint8_t(ScriptPrefix::P2WSH);
      prefixedHash_.append(networkByte);
      prefixedHash_.append(hash);
   }
   return prefixedHash_;
}

const std::string& AddressEntry_P2WSH::getAddress() const
{
   //prefixed has for SW is only for the db, using plain hash for SW
   if (address_.empty()) {
      address_ = std::move(BtcUtils::scrAddrToSegWitAddress(getHash()));
   }
   return address_;
}

std::shared_ptr<Signing::ScriptRecipient> AddressEntry_P2WSH::getRecipient(
   uint64_t value) const
{
   const auto& hash = getHash();
   return std::make_shared<Signing::Recipient_P2WSH>(hash, value);
}

const Wallets::AssetId& AddressEntry_P2WSH::getID() const
{
   if (getPredecessor() == nullptr) {
      throw AddressException("missing predecessor");
   }
   return getPredecessor()->getID();
}

size_t AddressEntry_P2WSH::getWitnessDataSize() const
{
   return getScript().getSize();
}

const BinaryData& AddressEntry_P2WSH::getScript() const
{
   if (script_.empty()) {
      const auto& hash = getHash();
      script_ = std::move(BtcUtils::getP2WSHOutputScript(hash));
   }
   return script_;
}

AddressEntryType AddressEntry_P2WSH::getType() const
{
   auto nestedType = AddressEntry::getType();
   auto baseType = getPredecessor()->getType();
   return AddressEntryType(baseType | nestedType);
}

////////////////////////////////////////////////////////////////////////////////
// static methods
std::shared_ptr<AddressEntry> AddressEntry::instantiate(
   std::shared_ptr<Assets::AssetEntry> assetPtr, AddressEntryType aeType)
{
   /*creates an address entry based on an asset and an address type*/
   std::shared_ptr<AddressEntry> addressPtr = nullptr;

   bool isCompressed = (aeType & ADDRESS_COMPRESSED_MASK) == 0;
   switch (aeType & ADDRESS_TYPE_MASK)
   {
      case AddressEntryType::Default:
         throw AddressException("invalid address entry type");

      case AddressEntryType::P2PKH:
      {
         addressPtr = std::make_shared<AddressEntry_P2PKH>(
            assetPtr, isCompressed);
         break;
      }

      case AddressEntryType::P2PK:
      {
         addressPtr = std::make_shared<AddressEntry_P2PK>(
            assetPtr, isCompressed);
         break;
      }

      case AddressEntryType::P2WPKH:
      {
         addressPtr = std::make_shared<AddressEntry_P2WPKH>(assetPtr);
         break;
      }

      case AddressEntryType::Multisig:
      {
         addressPtr = std::make_shared<AddressEntry_Multisig>(
            assetPtr, isCompressed);
         break;
      }

      case AddressEntryType::ScriptHash:
      {
         addressPtr = std::make_shared<AddressEntry_ScriptHash>(assetPtr);
         break;
      }

      case AddressEntryType::RawScript:
      {
         addressPtr = std::make_shared<AddressEntry_RawScript>(assetPtr);
         break;
      }

      default:
         throw AddressException("invalid address entry type");
   }

   if (aeType & ADDRESS_NESTED_MASK) {
      std::shared_ptr<AddressEntry> nestedPtr = nullptr;
      switch (aeType & ADDRESS_NESTED_MASK)
      {
         case AddressEntryType::P2SH:
         {
            nestedPtr = std::make_shared<AddressEntry_P2SH>(addressPtr);
            break;
         }

         case AddressEntryType::P2WSH:
         {
            nestedPtr = std::make_shared<AddressEntry_P2WSH>(addressPtr);
            break;
         }

         default:
            throw AddressException("invalid nested flag");
      }
      addressPtr = nestedPtr;
   }
   return addressPtr;
}

////////////////////////////////////////////////////////////////////////////////
uint8_t AddressEntry::getPrefixByte(AddressEntryType aeType)
{
   /*return the prefix bye for a given AddressEntryType*/
   auto nested = aeType & ADDRESS_NESTED_MASK;
   if (nested != 0) {
      switch (nested)
      {
         case AddressEntryType::P2SH:
            return Config::BitcoinSettings::getScriptHashPrefix();

         case AddressEntryType::P2WSH:
            return (uint8_t)ScriptPrefix::P2WSH;

         default:
            throw AddressException("unexpected AddressEntry nested type");
      }
   }

   switch (aeType & ADDRESS_TYPE_MASK)
   {
      case AddressEntryType::Default:
         throw AddressException("invalid address entry type");

      case AddressEntryType::P2PKH:
         return Config::BitcoinSettings::getPubkeyHashPrefix();

      case AddressEntryType::P2PK:
         throw AddressException("native P2PK doesnt come hashed");

      case AddressEntryType::P2WPKH:
         return (uint8_t)ScriptPrefix::P2WPKH;

      case AddressEntryType::Multisig:
         throw AddressException("native multisig scripts do not come hashed");

      default:
         throw AddressException("invalid AddressEntryType");
   }
   throw AddressException("invalid AddressEntryType");
}

////////////////////////////////////////////////////////////////////////////////
std::string Armory::getNameForAddrType(int addrTypeInt)
{
   std::string result;

   auto nestedFlag = addrTypeInt & ADDRESS_NESTED_MASK;
   bool nested = false;
   switch (nestedFlag)
   {
      case 0:
         break;

      case AddressEntryType::P2SH:
         result += "P2SH";
         nested = true;
         break;

      case AddressEntryType::P2WSH:
         result += "P2WSH";
         nested = true;
         break;

      default:
         throw std::runtime_error("[getNameForAddrType] unknown nested flag");
   }

   auto addressType = addrTypeInt & ADDRESS_TYPE_MASK;
   if (addressType == 0) {
      return result;
   }

   if (nested) {
      result += "-";
   }

   switch (addressType)
   {
      case AddressEntryType::P2PKH:
         result += "P2PKH";
         break;

      case AddressEntryType::P2PK:
         result += "P2PK";
         break;

      case AddressEntryType::P2WPKH:
         result += "P2WPKH";
         break;

      case AddressEntryType::Multisig:
         result += "Multisig";
         break;

      case AddressEntryType::ScriptHash:
         result += "RawScriptHash";
         break;

      case AddressEntryType::RawScript:
         result += "RawScript";
         break;

      default:
         throw std::runtime_error("[getNameForAddrType] unknown address type");
   }

   if (addrTypeInt & ADDRESS_COMPRESSED_MASK) {
      result += " (Uncompressed)";
   }

   if (result.empty()) {
      result = "N/A";
   }
   return result;
}
