////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Addresses.h"

using namespace std;
using namespace Armory::Config;
using namespace Armory::Signing;
using namespace Armory::Assets;

////////////////////////////////////////////////////////////////////////////////
AddressEntry::~AddressEntry()
{}

AddressEntry_WithAsset::~AddressEntry_WithAsset()
{}

AddressEntry_Nested::~AddressEntry_Nested()
{}

////////////////////////////////////////////////////////////////////////////////
//// P2PKH
////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2PKH::getPreimage() const
{
   auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(getAsset());
   if (assetSingle == nullptr)
      throw AddressException("unexpected asset entry type");

   if (isCompressed())
      return assetSingle->getPubKey()->getCompressedKey();
   else
      return assetSingle->getPubKey()->getUncompressedKey();
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2PKH::getHash() const
{
   if (hash_.getSize() == 0)
   {
      auto& preimage = getPreimage();
      auto&& hash1 = BtcUtils::getHash160(preimage);
      auto&& hash2 = BtcUtils::getHash160(preimage);

      if (hash1 != hash2)
         throw AddressException("failed to hash preimage");

      hash_ = hash1;
   }

   return hash_;
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2PKH::getPrefixedHash() const
{
   if (prefixedHash_.getSize() == 0)
   {
      auto& hash = getHash();

      //get and prepend network byte
      auto networkByte = BitcoinSettings::getPubkeyHashPrefix();

      prefixedHash_.append(networkByte);
      prefixedHash_.append(hash);
   }

   return prefixedHash_;
}

////////////////////////////////////////////////////////////////////////////////
const string& AddressEntry_P2PKH::getAddress() const
{
   if (address_.size() == 0)
      address_ = move(BtcUtils::scrAddrToBase58(getPrefixedHash()));

   return address_;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<ScriptRecipient> AddressEntry_P2PKH::getRecipient(
   uint64_t value) const
{
   auto& hash = getHash();
   return make_shared<Recipient_P2PKH>(hash, value);
}

////////////////////////////////////////////////////////////////////////////////
size_t AddressEntry_P2PKH::getInputSize() const
{
   size_t size = 114; //outpoint, sequence and sig + varint overhead

   if (isCompressed())
      size += 33;
   else
      size += 65;

   return size;
}

////////////////////////////////////////////////////////////////////////////////
const Armory::Wallets::AssetId& AddressEntry_P2PKH::getID() const
{
   return getAsset()->getID();
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2PKH::getScript() const
{
   if (script_.getSize() == 0)
   {
      auto& hash = getHash();
      script_ = move(BtcUtils::getP2PKHScript(hash));
   }

   return script_;
}

////////////////////////////////////////////////////////////////////////////////
//// P2PK
////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2PK::getPreimage() const
{
   auto asset_single = dynamic_pointer_cast<AssetEntry_Single>(getAsset());
   if (asset_single == nullptr)
      throw AddressException("invalid asset entry type");

   if (isCompressed())
      return asset_single->getPubKey()->getCompressedKey();
   else
      return asset_single->getPubKey()->getUncompressedKey();
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2PK::getHash() const
{
   throw AddressException("native P2PK doesnt come hashed");
   return hash_;
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2PK::getPrefixedHash() const
{
   throw AddressException("native P2PK doesnt come hashed");
   return prefixedHash_;
}

////////////////////////////////////////////////////////////////////////////////
const string& AddressEntry_P2PK::getAddress() const
{
   throw AddressException("native P2PK doesnt have an address format");
   return address_;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<ScriptRecipient> AddressEntry_P2PK::getRecipient(
   uint64_t value) const
{
   auto& preimage = getPreimage();
   return make_shared<Recipient_P2PK>(preimage, value);
}

////////////////////////////////////////////////////////////////////////////////
size_t AddressEntry_P2PK::getInputSize() const
{
   size_t size = 114; //outpoint, sequence and sig + varint overhead

   if (isCompressed())
      size += 33;
   else
      size += 65;

   return size;
}

////////////////////////////////////////////////////////////////////////////////
const Armory::Wallets::AssetId& AddressEntry_P2PK::getID() const
{
   return getAsset()->getID();
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2PK::getScript() const
{
   if (script_.getSize() == 0)
   {
      auto& preimage = getPreimage();
      script_ = move(BtcUtils::getP2PKScript(preimage));
   }

   return script_;
}

////////////////////////////////////////////////////////////////////////////////
//// P2WPKH
////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2WPKH::getPreimage() const
{
   auto assetSingle = dynamic_pointer_cast<AssetEntry_Single>(getAsset());
   if (assetSingle == nullptr)
      throw AddressException("unexpected asset entry type");

   return assetSingle->getPubKey()->getCompressedKey();

}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2WPKH::getHash() const
{
   if (hash_.getSize() == 0)
   {
      auto& preimage = getPreimage();
      auto&& hash1 = BtcUtils::getHash160(preimage);
      auto&& hash2 = BtcUtils::getHash160(preimage);

      if (hash1 != hash2)
         throw AddressException("failed to hash preimage");

      hash_ = hash1;
   }

   return hash_;
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2WPKH::getPrefixedHash() const
{
   if (prefixedHash_.getSize() == 0)
   {
      auto& hash = getHash();

      //get and prepend network byte
      auto networkByte = uint8_t(SCRIPT_PREFIX_P2WPKH);

      prefixedHash_.append(networkByte);
      prefixedHash_.append(hash);
   }

   return prefixedHash_;
}

////////////////////////////////////////////////////////////////////////////////
const string& AddressEntry_P2WPKH::getAddress() const
{
   //prefixed has for SW is only for the db, using plain hash for SW
   if (address_.size() == 0)
      address_ = move(BtcUtils::scrAddrToSegWitAddress(getHash()));
   return address_;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<ScriptRecipient> AddressEntry_P2WPKH::getRecipient(
   uint64_t value) const
{
   auto& hash = getHash();
   return make_shared<Recipient_P2WPKH>(hash, value);
}

////////////////////////////////////////////////////////////////////////////////
const Armory::Wallets::AssetId& AddressEntry_P2WPKH::getID() const
{
   return getAsset()->getID();
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2WPKH::getScript() const
{
   if (script_.getSize() == 0)
   {
      auto& hash = getHash();
      script_ = move(BtcUtils::getP2WPKHOutputScript(hash));
   }

   return script_;
}

////////////////////////////////////////////////////////////////////////////////
//// Multisig
////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_Multisig::getPreimage() const
{
   throw AddressException("native multisig scripts do not come hashed");
   return hash_;
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_Multisig::getHash() const
{
   throw AddressException("native multisig scripts do not come hashed");
   return hash_;
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_Multisig::getPrefixedHash() const
{
   throw AddressException("native multisig scripts do not come hashed");
   return prefixedHash_;
}

////////////////////////////////////////////////////////////////////////////////
const string& AddressEntry_Multisig::getAddress() const
{
   throw AddressException("no address format for native multisig");
   return address_;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<ScriptRecipient> AddressEntry_Multisig::getRecipient(
   uint64_t value) const
{
   auto& script = getScript();
   return make_shared<Recipient_Universal>(script, value);
}

////////////////////////////////////////////////////////////////////////////////
size_t AddressEntry_Multisig::getInputSize() const
{
   switch (getAsset()->getType())
   {
   case AssetEntryType_Multisig:
   {
      auto assetMS = dynamic_pointer_cast<AssetEntry_Multisig>(getAsset());
      if (assetMS == nullptr)
         throw AddressException("unexpected asset entry type");

      auto m = assetMS->getM();

      auto& script = getScript();

      size_t size = script.getSize() + 2;
      size += 73 * m + 40; //m sigs + outpoint

      return size;
   }

   default:
      throw AddressException("unexpected asset type");
   }

   return SIZE_MAX;
}

////////////////////////////////////////////////////////////////////////////////
const Armory::Wallets::AssetId& AddressEntry_Multisig::getID() const
{
   return getAsset()->getID();
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_Multisig::getScript() const
{
   if (script_.getSize() == 0)
   {
      auto asset_ms = dynamic_pointer_cast<AssetEntry_Multisig>(getAsset());
      if (asset_ms == nullptr)
         throw AddressException("invalid asset entry type");

      BinaryWriter bw;

      //convert m to opcode and push
      auto m = asset_ms->getM() + OP_1 - 1;
      if (m > OP_16)
         throw AssetException("m exceeds OP_16");
      bw.put_uint8_t(m);

      //put pub keys
      for (auto& asset : asset_ms->getAssetMap())
      {
         auto assetSingle =
            dynamic_pointer_cast<AssetEntry_Single>(asset.second);

         if (assetSingle == nullptr)
            throw AssetException("unexpected asset entry type");

         if (isCompressed())
         {
            bw.put_uint8_t(33);
            bw.put_BinaryData(assetSingle->getPubKey()->getCompressedKey());
         }
         else
         {
            bw.put_uint8_t(65);
            bw.put_BinaryData(assetSingle->getPubKey()->getUncompressedKey());
         }
      }

      //convert n to opcode and push
      auto n = asset_ms->getN() + OP_1 - 1;
      if (n > OP_16 || n < m)
         throw AssetException("invalid n");
      bw.put_uint8_t(n);

      bw.put_uint8_t(OP_CHECKMULTISIG);
      script_ = bw.getData();
   }

   return script_;
}

////////////////////////////////////////////////////////////////////////////////
//// P2SH
////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2SH::getPreimage() const
{
   if (getPredecessor() == nullptr)
      throw AddressException("missing predecessor");

   return getPredecessor()->getScript();
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2SH::getHash() const
{
   if (hash_.getSize() == 0)
   {
      auto& preimage = getPreimage();
      auto&& hash1 = BtcUtils::getHash160(preimage);
      auto&& hash2 = BtcUtils::getHash160(preimage);

      if (hash1 != hash2)
         throw AddressException("failed to hash preimage");

      hash_ = hash1;
   }

   return hash_;
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2SH::getPrefixedHash() const
{
   if (prefixedHash_.getSize() == 0)
   {
      auto& hash = getHash();

      BinaryWriter bw;
      bw.put_uint8_t(BitcoinSettings::getScriptHashPrefix());
      bw.put_BinaryData(hash);

      prefixedHash_ = bw.getData();
   }

   return prefixedHash_;
}

////////////////////////////////////////////////////////////////////////////////
const Armory::Wallets::AssetId& AddressEntry_P2SH::getID() const
{
   if (getPredecessor() == nullptr)
      throw AddressException("missing predecessor");

   return getPredecessor()->getID();
}

////////////////////////////////////////////////////////////////////////////////
size_t AddressEntry_P2SH::getInputSize() const
{
   return getPredecessor()->getScript().getSize();
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<ScriptRecipient> AddressEntry_P2SH::getRecipient(
   uint64_t value) const
{
   auto& hash = getHash();
   return make_shared<Recipient_P2SH>(hash, value);
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2SH::getScript() const
{
   if (script_.getSize() == 0)
   {
      auto& hash = getHash();
      script_ = move(BtcUtils::getP2SHScript(hash));
   }

   return script_;
}

////////////////////////////////////////////////////////////////////////////////
const string& AddressEntry_P2SH::getAddress() const
{
   if (address_.size() == 0)
      address_ = move(BtcUtils::scrAddrToBase58(getPrefixedHash()));

   return address_;
}

////////////////////////////////////////////////////////////////////////////////
AddressEntryType AddressEntry_P2SH::getType() const
{
   auto nestedType = AddressEntry::getType();
   auto baseType = getPredecessor()->getType();

   return AddressEntryType(baseType | nestedType);
}

////////////////////////////////////////////////////////////////////////////////
//// P2WSH
////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2WSH::getPreimage() const
{
   if (getPredecessor() == nullptr)
      throw AddressException("missing predecessor");

   return getPredecessor()->getScript();
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2WSH::getHash() const
{
   if (hash_.getSize() == 0)
   {
      auto& preimage = getPreimage();
      auto&& hash1 = BtcUtils::getSha256(preimage);
      auto&& hash2 = BtcUtils::getSha256(preimage);

      if (hash1 != hash2)
         throw AddressException("failed to compute hash");

      hash_ = hash1;
   }

   return hash_;
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2WSH::getPrefixedHash() const
{
   if (prefixedHash_.getSize() == 0)
   {
      auto& hash = getHash();

      //get and prepend network byte
      auto networkByte = uint8_t(SCRIPT_PREFIX_P2WSH);

      prefixedHash_.append(networkByte);
      prefixedHash_.append(hash);
   }

   return prefixedHash_;
}

////////////////////////////////////////////////////////////////////////////////
const string& AddressEntry_P2WSH::getAddress() const
{
   //prefixed has for SW is only for the db, using plain hash for SW
   if (address_.size() == 0)
      address_ = move(BtcUtils::scrAddrToSegWitAddress(getHash()));
   return address_;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<ScriptRecipient> AddressEntry_P2WSH::getRecipient(
   uint64_t value) const
{
   auto& hash = getHash();
   return make_shared<Recipient_P2WSH>(hash, value);
}

////////////////////////////////////////////////////////////////////////////////
const Armory::Wallets::AssetId& AddressEntry_P2WSH::getID() const
{
   if (getPredecessor() == nullptr)
      throw AddressException("missing predecessor");

   return getPredecessor()->getID();
}

////////////////////////////////////////////////////////////////////////////////
size_t AddressEntry_P2WSH::getWitnessDataSize() const
{
   return getScript().getSize();
}

////////////////////////////////////////////////////////////////////////////////
const BinaryData& AddressEntry_P2WSH::getScript() const
{
   if (script_.getSize() == 0)
   {
      auto& hash = getHash();
      script_ = move(BtcUtils::getP2WSHOutputScript(hash));
   }

   return script_;
}

////////////////////////////////////////////////////////////////////////////////
AddressEntryType AddressEntry_P2WSH::getType() const
{
   auto nestedType = AddressEntry::getType();
   auto baseType = getPredecessor()->getType();

   return AddressEntryType(baseType | nestedType);
}


////////////////////////////////////////////////////////////////////////////////
//// static methods
////////////////////////////////////////////////////////////////////////////////
shared_ptr<AddressEntry> AddressEntry::instantiate(
   shared_ptr<AssetEntry> assetPtr, AddressEntryType aeType)
{
   /*creates an address entry based on an asset and an address type*/
   shared_ptr<AddressEntry> addressPtr = nullptr;

   bool isCompressed = (aeType & ADDRESS_COMPRESSED_MASK) == 0;

   switch (aeType & ADDRESS_TYPE_MASK)
   {
   case AddressEntryType_Default:
      throw AddressException("invalid address entry type");
      break;

   case AddressEntryType_P2PKH:
      addressPtr = make_shared<AddressEntry_P2PKH>(assetPtr, isCompressed);
      break;

   case AddressEntryType_P2PK:
      addressPtr = make_shared<AddressEntry_P2PK>(assetPtr, isCompressed);
      break;

   case AddressEntryType_P2WPKH:
      addressPtr = make_shared<AddressEntry_P2WPKH>(assetPtr);
      break;

   case AddressEntryType_Multisig:
      addressPtr = make_shared<AddressEntry_Multisig>(assetPtr, isCompressed);
      break;

   default:
      throw AddressException("invalid address entry type");
   }

   if (aeType & ADDRESS_NESTED_MASK)
   {
      shared_ptr<AddressEntry> nestedPtr = nullptr;

      switch (aeType & ADDRESS_NESTED_MASK)
      {
      case AddressEntryType_P2SH:
         nestedPtr = make_shared<AddressEntry_P2SH>(addressPtr);
         break;

      case AddressEntryType_P2WSH:
         nestedPtr = make_shared<AddressEntry_P2WSH>(addressPtr);
         break;

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
   if (nested != 0)
   {
      switch (nested)
      {
      case AddressEntryType_P2SH:
         return BitcoinSettings::getScriptHashPrefix();

      case AddressEntryType_P2WSH:
         return SCRIPT_PREFIX_P2WSH;

      default:
         throw AddressException("unexpected AddressEntry nested type");
      }
   }

   switch (aeType & ADDRESS_TYPE_MASK)
   {
   case AddressEntryType_Default:
      throw AddressException("invalid address entry type");
      break;

   case AddressEntryType_P2PKH:
      return BitcoinSettings::getPubkeyHashPrefix();

   case AddressEntryType_P2PK:
      throw AddressException("native P2PK doesnt come hashed");

   case AddressEntryType_P2WPKH:
      return SCRIPT_PREFIX_P2WPKH;

   case AddressEntryType_Multisig:
      throw AddressException("native multisig scripts do not come hashed");

   default:
      throw AddressException("invalid AddressEntryType");
   }

   throw AddressException("invalid AddressEntryType");
   return UINT8_MAX;
}
