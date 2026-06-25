////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2021-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <cstring>

#include "WalletIdTypes.h"
#include <Utils/BtcUtils.h>
#include <Utils/varint.h>
#include <Utils/BitcoinSettings.h>
#include "DerivationScheme.h"
#include "Assets.h"
#include "WalletHeader.h"
#include "Seeds/Seeds.h"

using namespace Armory;
using namespace Armory::Wallets;

namespace
{
   BinaryData computeID(BinaryDataRef pubkey)
   {
      auto h160 = BtcUtils::getHash160(pubkey);

      BinaryWriter bw;
      bw.put_uint8_t(Armory::Config::BitcoinSettings::getPubkeyHashPrefix());
      bw.put_BinaryDataRef(h160.getSliceRef(0, 5));

      //now reverse it
      auto& data = bw.getData();
      auto ptr = data.getPtr();
      BinaryWriter bwReverse;
      for (unsigned i = 0; i < data.getSize(); i++) {
         bwReverse.put_uint8_t(ptr[data.getSize() - 1 - i]);
      }
      return bwReverse.getData();
   }

   BinaryData generateWalletIdRaw(
      std::shared_ptr<Armory::Assets::DerivationScheme> derScheme,
      std::shared_ptr<Armory::Assets::AssetEntry> rootEntry,
      Armory::Seeds::SeedType sType)
   {
      if (sType == Armory::Seeds::SeedType::ArmoryLegacyPublic) {
         /*
         Legacy wallets root and seeds are essentially the same.
         A public (watching-only) legacy wallet is a root pubkey + chaincode.
         Legacy wallets may or may not have a deterministic chaincode, but you
         cannot tell with WO root, so we distinguish on disk between full roots
         (with a private key) and WO roots (pubkey + cc), by using different
         SeedType prefix.

         However this prefix is used to compute wallet ids, and we do not want
         WO wallets to have different ids from their full counterparts, so the
         public seed type is always set to a full one id computations.
         */
         sType = Armory::Seeds::SeedType::ArmoryLegacy;
      }

      //derive '(int)sType' amount of addresses, use last one as id
      auto addrVec = derScheme->extendPublicChain(rootEntry,
         0, (int)sType, nullptr);
      if (addrVec.size() != (size_t)sType+1) {
         throw WalletException("unexpected chain derivation output");
      }

      auto entry = std::dynamic_pointer_cast<Armory::Assets::AssetEntry_Single>(
         addrVec[int(sType)]);
      if (entry == nullptr) {
         throw WalletException("unexpected asset entry type");
      }
      return computeID(entry->getPubKey()->getUncompressedKey());
   }
}

IdException::IdException(const std::string& err) :
   std::runtime_error(err)
{}

////////////////////////////////////////////////////////////////////////////////
//
// WalletId
//
////////////////////////////////////////////////////////////////////////////////
WalletId::WalletId() :
   std::string()
{}

WalletId::WalletId(const char* cstr, size_t size)
   : std::string(cstr, size)
{}

WalletId::WalletId(const std::string_view& strv)
   : std::string(strv)
{}

WalletId::WalletId(const BinaryDataRef& bdr) :
   std::string(bdr.toCharPtr(), bdr.getSize())
{}

////////////////////////////////////////////////////////////////////////////////
//
// AddressAccountId
//
////////////////////////////////////////////////////////////////////////////////
AddressAccountId::AddressAccountId()
{}

////////////////////////////////////////////////////////////////////////////////
AddressAccountId::AddressAccountId(const AddressAccountId& id) :
   data_{id.data_}
{}

////////////////////////////////////////////////////////////////////////////////
AddressAccountId::AddressAccountId(AccountKeyType key) :
   data_{BinaryData::IntToStrBE(key)}
{}

////////////////////////////////////////////////////////////////////////////////
AddressAccountId::AddressAccountId(const BinaryData& id) :
   data_(id)
{
   if (id.getSize() != sizeof(AccountKeyType)) {
      throw IdException("[AddressAccountId] initializing from invalid id");
   }
}

////////////////////////////////////////////////////////////////////////////////
AddressAccountId& AddressAccountId::operator=(const AddressAccountId& rhs)
{
   data_ = rhs.data_;
   return *this;
}

////////////////////////////////////////////////////////////////////////////////
bool AddressAccountId::operator<(const AddressAccountId& rhs) const
{
   return data_ < rhs.data_;
}

////////////////////////////////////////////////////////////////////////////////
bool AddressAccountId::operator==(const AddressAccountId& rhs) const
{
   return data_ == rhs.data_;
}

////////////////////////////////////////////////////////////////////////////////
bool AddressAccountId::operator!=(const AddressAccountId& rhs) const
{
   return data_ != rhs.data_;
}

////////////////////////////////////////////////////////////////////////////////
bool AddressAccountId::isValid() const
{
   return data_.getSize() == sizeof(AccountKeyType);
}

////////////////////////////////////////////////////////////////////////////////
AccountKeyType AddressAccountId::getAddressAccountKey() const
{
   if (!isValid()) {
      throw IdException("[AddressAccountId] invalid id, cannot get key");
   }
   BinaryRefReader brr(data_.getRef());
   return brr.get_int32_t(BE);
}

////////////////////////////////////////////////////////////////////////////////
std::string AddressAccountId::toHexStr() const
{
   return data_.toHexStr();
}

////////////////////////////////////////////////////////////////////////////////
AddressAccountId AddressAccountId::fromHex(const std::string& hexStr)
{
   auto id = READHEX(hexStr);
   return AddressAccountId(id);
}

////////////////////////////////////////////////////////////////////////////////
void AddressAccountId::serializeValue(BinaryWriter& bw) const
{
   if (!isValid()) {
      throw IdException("[AddressAccountId::serialize] invalid id");
   }
   bw.put_var_int(data_.getSize());
   bw.put_BinaryData(data_);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData AddressAccountId::getSerializedKey(uint8_t prefix) const
{
   if (!isValid()) {
      throw IdException("[AddressAccountId::put] invalid id");
   }
   BinaryWriter bw;
   bw.put_uint8_t(prefix);
   bw.put_BinaryData(data_);
   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
AddressAccountId AddressAccountId::deserializeValue(BinaryRefReader& brr)
{
   try {
      auto len = brr.get_var_int();
      return AddressAccountId(brr.get_BinaryData(len));
   } catch (const BtcUtils::VarIntException&) {
      throw IdException("[AddressAccountId::deserializeValue] invalid varint");
   }
}

////////////////////////////////////////////////////////////////////////////////
AddressAccountId AddressAccountId::deserializeValue(const BinaryData& bd)
{
   BinaryRefReader brr(bd.getRef());
   return deserializeValue(brr);
}

////////////////////////////////////////////////////////////////////////////////
AddressAccountId AddressAccountId::deserializeKey(
   const BinaryData& bd, uint8_t prefix)
{
   BinaryRefReader brr(bd.getRef());
   auto pref = brr.get_uint8_t();
   if (pref != prefix) {
      throw IdException("[AddressAccountId::deserializeKey] prefix mismatch");
   }

   const auto& idData = brr.get_BinaryData(sizeof(AccountKeyType));
   if (brr.getSizeRemaining() != 0) {
      throw IdException("[AddressAccountId::deserializeKey] invalid key size");
   }
   return AddressAccountId(idData);
}

////////////////////////////////////////////////////////////////////////////////
//
// AssetAccountId
//
////////////////////////////////////////////////////////////////////////////////
AssetAccountId::AssetAccountId()
{}

////////////////////////////////////////////////////////////////////////////////
AssetAccountId::AssetAccountId(
   AccountKeyType addressAccountKey, AccountKeyType assetAccountKey)
{
   if (sizeof(AccountKeyType) != 4) {
      throw IdException("[AssetAccountId] invalid account key type");
   }

   BinaryWriter bw(sizeof(AccountKeyType) * 2);
   bw.put_int32_t(addressAccountKey, BE);
   bw.put_int32_t(assetAccountKey, BE);
   data_ = bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
AssetAccountId::AssetAccountId(const BinaryData& id)
{
   if (id.getSize() != sizeof(AccountKeyType) * 2) {
      throw IdException("[AssetAccountId] initializing from invalid id");
   }
   data_ = id;
}

////////////////////////////////////////////////////////////////////////////////
AssetAccountId::AssetAccountId(const AddressAccountId& id, AccountKeyType key)
{
   if (sizeof(AccountKeyType) != 4) {
      throw IdException("[AssetAccountId] invalid account key type");
   }
   if (!id.isValid()) {
      throw IdException("[AssetAccountId] invalid address account id");
   }

   BinaryWriter bw(id.data_.getSize() + sizeof(AccountKeyType));
   bw.put_BinaryData(id.data_);
   bw.put_int32_t(key, BE);
   data_ = bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
AssetAccountId::AssetAccountId(const AssetAccountId& id)
{
   if (!id.isValid()) {
      throw IdException("[AssetAccountId] id is invalid");
   }
   data_ = id.data_;
}

////////////////////////////////////////////////////////////////////////////////
AssetAccountId& AssetAccountId::operator=(const AssetAccountId& rhs)
{
   data_ = rhs.data_;
   return *this;
}

////////////////////////////////////////////////////////////////////////////////
bool AssetAccountId::operator<(const AssetAccountId& rhs) const
{
   return data_ < rhs.data_;
}

////////////////////////////////////////////////////////////////////////////////
bool AssetAccountId::operator==(const AssetAccountId& rhs) const
{
   return data_ == rhs.data_;
}

////////////////////////////////////////////////////////////////////////////////
bool AssetAccountId::operator!=(const AssetAccountId& rhs) const
{
   return data_ != rhs.data_;
}

////////////////////////////////////////////////////////////////////////////////
bool AssetAccountId::isValid() const
{
   return data_.getSize() == (sizeof(AccountKeyType) * 2);
}

////////////////////////////////////////////////////////////////////////////////
AddressAccountId AssetAccountId::getAddressAccountId() const
{
   if (!isValid()) {
      throw IdException("[getAddressAccountId] invalid asset account id");
   }
   return AddressAccountId(getAddressAccountKey());
}

////////////////////////////////////////////////////////////////////////////////
AccountKeyType AssetAccountId::getAddressAccountKey() const
{
   if (!isValid()) {
      throw IdException("[getAddressAccountKey] invalid asset account id");
   }
   BinaryRefReader brr(data_);
   return brr.get_int32_t(BE);
}

////////////////////////////////////////////////////////////////////////////////
AccountKeyType AssetAccountId::getAssetAccountKey() const
{
   if (!isValid()) {
      throw IdException("[getAddressAccountKey] invalid asset account id");
   }
   BinaryRefReader brr(data_);
   brr.advance(sizeof(AccountKeyType));
   return brr.get_int32_t(BE);
}

////////////////////////////////////////////////////////////////////////////////
std::string AssetAccountId::toHexStr() const
{
   return data_.toHexStr();
}

////////////////////////////////////////////////////////////////////////////////
void AssetAccountId::serializeValue(BinaryWriter& bw) const
{
   if (!isValid()) {
      throw IdException("[AssetAccountId::serialize] invalid id");
   }
   bw.put_var_int(data_.getSize());
   bw.put_BinaryData(data_);
}

////////////////////////////////////////////////////////////////////////////////
AssetAccountId AssetAccountId::deserializeValue(BinaryRefReader& brr)
{
   auto pos = brr.getPosition();
   try {
      auto len = brr.get_var_int();
      return AssetAccountId(brr.get_BinaryData(len));
   } catch (const std::exception&) {
      //reset reader to its original position
      brr.resetPosition();
      brr.advance(pos);
      throw IdException("[AssetAccountId::deserializeValue]");
   }
}

////////////////////////////////////////////////////////////////////////////////
AssetAccountId AssetAccountId::deserializeValueOld(
   const AddressAccountId& id, BinaryRefReader& brr)
{
   auto len = brr.get_var_int();
   if (len != sizeof(AccountKeyType)) {
      throw IdException("[AssetAccountId::deserializeValueOld] error");
   }
   return AssetAccountId(id, brr.get_int32_t(BE));
}

////////////////////////////////////////////////////////////////////////////////
BinaryData AssetAccountId::getSerializedKey(uint8_t prefix) const
{
   if (!isValid()) {
      throw IdException("[AssetAccountId::put] invalid id");
   }
   BinaryWriter bw;
   bw.put_uint8_t(prefix);
   bw.put_BinaryData(data_);
   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
AssetAccountId AssetAccountId::deserializeKey(
   const BinaryData& data, uint8_t prefix)
{
   BinaryRefReader brr(data.getRef());
   auto pref = brr.get_uint8_t();
   if (pref != prefix) {
      throw IdException("[AssetAccountId::deserializeKey] prefix mismatch");
   }

   const auto& idData = brr.get_BinaryData(sizeof(AccountKeyType)*2);
   if (brr.getSizeRemaining() != 0) {
      throw IdException("[AssetAccountId::deserializeKey] invalid key size");
   }
   return AssetAccountId(idData);
}

////////////////////////////////////////////////////////////////////////////////
//
// AssetId
//
////////////////////////////////////////////////////////////////////////////////
AssetKeyType AssetId::dummyId_ = 0;

////////////////////////////////////////////////////////////////////////////////
AssetId::AssetId()
{}

////////////////////////////////////////////////////////////////////////////////
AssetId::AssetId(const AssetId& id) :
   data_(id.data_)
{}

////////////////////////////////////////////////////////////////////////////////
AssetId::AssetId(const BinaryData& id)
{
   if (id.getSize() != (sizeof(AccountKeyType) * 2 + sizeof(AssetKeyType))) {
      throw IdException("[AssetId] invalid id");
   }
   data_ = id;
}

////////////////////////////////////////////////////////////////////////////////
AssetId::AssetId(AccountKeyType addressAccountKey,
   AccountKeyType assetAccountKey,
   AssetKeyType assetKey)
{
   if (sizeof(AccountKeyType) != 4 || sizeof(AssetKeyType) != 4) {
      throw IdException("[AssetId] invalid key type");
   }

   BinaryWriter bw(sizeof(AccountKeyType) * 2 + sizeof(AssetKeyType));
   bw.put_int32_t(addressAccountKey, BE);
   bw.put_int32_t(assetAccountKey, BE);
   bw.put_int32_t(assetKey, BE);
   data_ = bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
AssetId::AssetId(const AssetAccountId& id, AssetKeyType key)
{
   if (sizeof(AssetKeyType) != 4) {
      throw IdException("[AssetId] invalid asset key type");
   }
   if (!id.isValid()) {
      throw IdException("[AssetId] invalid asset account id");
   }

   BinaryWriter bw(id.data_.getSize() + sizeof(AssetKeyType));
   bw.put_BinaryData(id.data_);
   bw.put_int32_t(key, BE);
   data_ = bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
AssetId::AssetId(const AddressAccountId& accId,
   AssetKeyType accKey, AssetKeyType assKey)
{
   if (!accId.isValid()) {
      throw IdException("[AssetId] invalid address account id");
   }

   BinaryWriter bw(
      accId.data_.getSize() +
      sizeof(AccountKeyType) +
      sizeof(AssetKeyType)
   );
   bw.put_BinaryData(accId.data_);
   bw.put_int32_t(accKey, BE);
   bw.put_int32_t(assKey, BE);
   data_ = bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
AssetKeyType AssetId::getRootKey()
{
   return rootAssetId;
}

////////////////////////////////////////////////////////////////////////////////
AssetId& AssetId::operator=(const AssetId& rhs)
{
   data_ = rhs.data_;
   return *this;
}

////////////////////////////////////////////////////////////////////////////////
bool AssetId::operator<(const AssetId& rhs) const
{
   return data_ < rhs.data_;
}

////////////////////////////////////////////////////////////////////////////////
bool AssetId::operator==(const AssetId& rhs) const
{
   return data_ == rhs.data_;
}

////////////////////////////////////////////////////////////////////////////////
bool AssetId::operator!=(const AssetId& rhs) const
{
   return data_ != rhs.data_;
}

////////////////////////////////////////////////////////////////////////////////
bool AssetId::belongsTo(const AssetAccountId& accId) const
{
   if (!accId.isValid() || !isValid())
      return false;

   return (memcmp(
      accId.data_.getPtr(), data_.getPtr(), sizeof(AccountKeyType) * 2) == 0);
}

////////////////////////////////////////////////////////////////////////////////
bool AssetId::isValid() const
{
   return data_.getSize() ==
      (sizeof(AccountKeyType) * 2 + sizeof(AssetKeyType));
}

////////////////////////////////////////////////////////////////////////////////
AssetKeyType AssetId::getAssetKey() const
{
   if (!isValid()) {
      throw IdException("[getAssetKey] invalid asset id");
   }

   BinaryRefReader brr(data_);
   brr.advance(sizeof(AccountKeyType) * 2);
   return brr.get_int32_t(BE);
}

////////////////////////////////////////////////////////////////////////////////
AccountKeyType AssetId::getAddressAccountKey() const
{
   if (!isValid()) {
      throw IdException("[getAddressAccountKey] invalid asset id");
   }

   BinaryRefReader brr(data_);
   return brr.get_int32_t(BE);
}

////////////////////////////////////////////////////////////////////////////////
AddressAccountId AssetId::getAddressAccountId() const
{
   return AddressAccountId(getAddressAccountKey());
}

////////////////////////////////////////////////////////////////////////////////
AssetAccountId AssetId::getAssetAccountId() const
{
   if (!isValid()) {
      throw IdException("[getAssetAccountId] invalid asset id");
   }
   return AssetAccountId(data_.getSliceCopy(0, sizeof(AccountKeyType) * 2));
}

////////////////////////////////////////////////////////////////////////////////
AssetId AssetId::getNextDummyId()
{
   return AssetId(dummyAccountId, dummyAccountId, dummyId_++);
}

////////////////////////////////////////////////////////////////////////////////
AssetId AssetId::getRootAssetId()
{
   return AssetId(rootAccountId, rootAccountId, rootAssetId);
}

////////////////////////////////////////////////////////////////////////////////
void AssetId::serializeValue(BinaryWriter& bw) const
{
   if (!isValid()) {
      throw IdException("[AssetId::serialize] invalid id");
   }

   bw.put_var_int(data_.getSize());
   bw.put_BinaryData(data_);
}

////////////////////////////////////////////////////////////////////////////////
AssetId AssetId::deserializeValue(BinaryRefReader& brr)
{
   auto len = brr.get_var_int();
   return AssetId(brr.get_BinaryData(len));
}

////////////////////////////////////////////////////////////////////////////////
BinaryData AssetId::getSerializedKey(uint8_t prefix) const
{
   if (!isValid()) {
      throw IdException("[AssetId::serialize] put id");
   }

   BinaryWriter bw;
   bw.put_uint8_t(prefix);
   bw.put_BinaryData(data_);
   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
AssetId AssetId::deserializeKey(BinaryDataRef data, uint8_t prefix)
{
   BinaryRefReader brr(data);
   auto pref = brr.get_uint8_t();
   if (pref != prefix) {
      throw IdException("[AssetId::deserializeKey] prefix mismatch");
   }

   const auto& idData = brr.get_BinaryData(
      sizeof(AccountKeyType)*2 + sizeof(AssetKeyType));
   if (brr.getSizeRemaining() != 0) {
      throw IdException("[AssetId::deserializeKey] invalid key size");
   }
   return AssetId(idData);
}

////////////////////////////////////////////////////////////////////////////////
AssetId AssetId::deserializeKey(const BinaryData& data, uint8_t prefix)
{
   return deserializeKey(data.getRef(), prefix);
}

////////////////////////////////////////////////////////////////////////////////
//
// EncryptionKeyId
//
////////////////////////////////////////////////////////////////////////////////
EncryptionKeyId::EncryptionKeyId()
{}

////////////////////////////////////////////////////////////////////////////////
EncryptionKeyId::EncryptionKeyId(const EncryptionKeyId& id) :
   data_(id.data_)
{}

////////////////////////////////////////////////////////////////////////////////
EncryptionKeyId::EncryptionKeyId(const BinaryData& data)
{
   if (data.getSize() != EncryptionKeyIdLength) {
      throw IdException("[EncryptionKeyId] invalid key size");
   }
   data_ = data;
}

////////////////////////////////////////////////////////////////////////////////
EncryptionKeyId::EncryptionKeyId(const std::string& str)
{
   //private ctor to init keys of any size
   data_ = BinaryData::fromString(str);
}

////////////////////////////////////////////////////////////////////////////////
EncryptionKeyId& EncryptionKeyId::operator=(const EncryptionKeyId& rhs)
{
   data_ = rhs.data_;
   return *this;
}

////////////////////////////////////////////////////////////////////////////////
bool EncryptionKeyId::operator<(const EncryptionKeyId& rhs) const
{
   return data_ < rhs.data_;
}

////////////////////////////////////////////////////////////////////////////////
bool EncryptionKeyId::operator==(const EncryptionKeyId& rhs) const
{
   return data_ == rhs.data_;
}

////////////////////////////////////////////////////////////////////////////////
bool EncryptionKeyId::operator!=(const EncryptionKeyId& rhs) const
{
   return data_ != rhs.data_;
}

////////////////////////////////////////////////////////////////////////////////
bool EncryptionKeyId::isValid() const
{
   return data_.getSize() == EncryptionKeyIdLength;
}

////////////////////////////////////////////////////////////////////////////////
std::string EncryptionKeyId::toHexStr() const
{
   return data_.toHexStr();
}

////////////////////////////////////////////////////////////////////////////////
void EncryptionKeyId::serializeValue(BinaryWriter& bw) const
{
   bw.put_var_int(data_.getSize());
   bw.put_BinaryData(data_);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData EncryptionKeyId::getSerializedKey(uint8_t prefix) const
{
   BinaryWriter bw;
   bw.put_uint8_t(prefix);
   bw.put_BinaryData(data_);

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
EncryptionKeyId EncryptionKeyId::deserializeValue(BinaryRefReader& brr)
{
   try {
      auto len = brr.get_var_int();
      return EncryptionKeyId(brr.get_BinaryData(len));
   } catch (const BtcUtils::VarIntException&) {
      throw IdException("EncryptionKeyId::deserializeValue");
   }
}

////////////////////////////////////////////////////////////////////////////////
//
// KdfId
//
////////////////////////////////////////////////////////////////////////////////
KdfId::KdfId()
{}

KdfId::KdfId(const std::string_view& strV) :
   data_(BinaryData::fromString(strV))
{}

KdfId::KdfId(const KdfId& rhs) :
   data_(rhs.data_)
{}

KdfId KdfId::fromBinaryData(BinaryData& bd)
{
   KdfId result;
   result.data_ = std::move(bd);
   return result;
}

KdfId& KdfId::operator=(const KdfId& rhs)
{
   if (this != &rhs) {
      this->data_ = rhs.data_;
   }
   return *this;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData KdfId::getSerializedKey() const
{
   auto dbKey = WRITE_UINT8_BE(KDF_PREFIX);
   dbKey.append(data_);
   return dbKey;
}

const BinaryData& KdfId::data() const
{
   return data_;
}

////////////////////////////////////////////////////////////////////////////////
bool KdfId::operator==(const KdfId& rhs) const
{
   return data_ == rhs.data_;
}

bool KdfId::operator==(const std::string_view& rhs) const
{
   if (rhs.size() != data_.getSize()) {
      return false;
   }
   return (std::memcmp(data_.getPtr(), rhs.data(), rhs.size()) == 0);
}

bool KdfId::operator!=(const std::string_view& rhs) const
{
   if (rhs.size() != data_.getSize()) {
      return true;
   }
   return (std::memcmp(data_.getPtr(), rhs.data(), rhs.size()) != 0);
}

bool KdfId::operator<(const KdfId& rhs) const
{
   return data_ < rhs.data_;
}

bool KdfId::isValid() const
{
   return !data_.empty();
}

std::string KdfId::toHexStr() const
{
   return data_.toHexStr();
}

///////////////////////// - wallet & master id - ///////////////////////////////
BinaryData Armory::Wallets::generateWalletIdRaw(
   SecureBinaryData pubkey,
   SecureBinaryData chaincode,
   Armory::Seeds::SeedType sType)
{
   //sanity checks
   if (pubkey.empty()) {
      throw WalletException("[generateWalletId] empty pubkey");
   }
   if (chaincode.empty()) {
      throw WalletException("[generateWalletId] empty chaincode");
   }

   //create legacy armory derviation scheme from chaincode
   auto derScheme = std::make_shared<
      Armory::Assets::DerivationScheme_ArmoryLegacy>(chaincode);

   //create root pubkey asset
   auto asset_single = std::make_shared<Armory::Assets::AssetEntry_Single>(
      AssetId::getRootAssetId(), pubkey, nullptr
   );
   return ::generateWalletIdRaw(derScheme, asset_single, sType);
}

WalletId Armory::Wallets::generateWalletId(
   SecureBinaryData pubkey,
   SecureBinaryData chaincode,
   Armory::Seeds::SeedType sType)
{
   auto idBd = generateWalletIdRaw(pubkey, chaincode, sType);
   auto idB58 = BtcUtils::base58_encode(idBd);
   return {std::string_view{idB58}};
}

////////
WalletId Armory::Wallets::generateMasterId(const SecureBinaryData& pubkey,
   const SecureBinaryData& chaincode)
{
   BinaryWriter bw;
   bw.put_BinaryData(pubkey);
   bw.put_BinaryData(chaincode);
   auto hmacMasterMsg = SecureBinaryData::fromString("MetaEntry");
   auto masterID_long = BtcUtils::getHMAC256(
      bw.getData(), hmacMasterMsg);

   auto idBd = computeID(masterID_long);
   auto idB58 = BtcUtils::base58_encode(idBd);
   return {std::string_view{idB58}};
}
