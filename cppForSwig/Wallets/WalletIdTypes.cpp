////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2021-2023, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "WalletIdTypes.h"
#include "DerivationScheme.h"
#include "Assets.h"
#include "WalletHeader.h"

using namespace Armory::Wallets;

////////////////////////////////////////////////////////////////////////////////
//
// AddressAccountId
//
////////////////////////////////////////////////////////////////////////////////
AddressAccountId::AddressAccountId()
{}

////////////////////////////////////////////////////////////////////////////////
AddressAccountId::AddressAccountId(const AddressAccountId& id) :
   data_(id.data_)
{}

////////////////////////////////////////////////////////////////////////////////
AddressAccountId::AddressAccountId(AccountKeyType key)
{
   data_ = BinaryData::IntToStrBE(key);
}

////////////////////////////////////////////////////////////////////////////////
AddressAccountId::AddressAccountId(const BinaryData& id)
{
   if (id.getSize() != sizeof(AccountKeyType))
      throw IdException("[AddressAccountId] initializing from invalid id");

   data_ = id;
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
   if (!isValid())
      throw IdException("[AddressAccountId] invalid id, cannot get key");

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
   if (!isValid())
      throw IdException("[AddressAccountId::serialize] invalid id");

   bw.put_var_int(data_.getSize());
   bw.put_BinaryData(data_);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData AddressAccountId::getSerializedKey(uint8_t prefix) const
{
   if (!isValid())
      throw IdException("[AddressAccountId::put] invalid id");

   BinaryWriter bw;
   bw.put_uint8_t(prefix);
   bw.put_BinaryData(data_);
   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
AddressAccountId AddressAccountId::deserializeValue(BinaryRefReader& brr)
{
   try
   {
      auto len = brr.get_var_int();
      return AddressAccountId(brr.get_BinaryData(len));
   }
   catch (const VarIntException&)
   {
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
   if (pref != prefix)
      throw IdException("[AddressAccountId::deserializeKey] prefix mismatch");

   const auto& idData = brr.get_BinaryData(sizeof(AccountKeyType));
   if (brr.getSizeRemaining() != 0)
      throw IdException("[AddressAccountId::deserializeKey] invalid key size");

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
   if (sizeof(AccountKeyType) != 4)
      throw IdException("[AssetAccountId] invalid account key type");

   BinaryWriter bw(sizeof(AccountKeyType) * 2);
   bw.put_int32_t(addressAccountKey, BE);
   bw.put_int32_t(assetAccountKey, BE);

   data_ = bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
AssetAccountId::AssetAccountId(const BinaryData& id)
{
   if (id.getSize() != sizeof(AccountKeyType) * 2)
      throw IdException("[AssetAccountId] initializing from invalid id");

   data_ = id;
}

////////////////////////////////////////////////////////////////////////////////
AssetAccountId::AssetAccountId(const AddressAccountId& id, AccountKeyType key)
{
   if (sizeof(AccountKeyType) != 4)
      throw IdException("[AssetAccountId] invalid account key type");

   if (!id.isValid())
      throw IdException("[AssetAccountId] invalid address account id");

   BinaryWriter bw(id.data_.getSize() + sizeof(AccountKeyType));
   bw.put_BinaryData(id.data_);
   bw.put_int32_t(key, BE);

   data_ = bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
AssetAccountId::AssetAccountId(const AssetAccountId& id)
{
   if (!id.isValid())
      throw IdException("[AssetAccountId] id is invalid");

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
   if (!isValid())
      throw IdException("[getAddressAccountId] invalid asset account id");

   return AddressAccountId(getAddressAccountKey());
}

////////////////////////////////////////////////////////////////////////////////
AccountKeyType AssetAccountId::getAddressAccountKey() const
{
   if (!isValid())
      throw IdException("[getAddressAccountKey] invalid asset account id");

   BinaryRefReader brr(data_);
   return brr.get_int32_t(BE);
}

////////////////////////////////////////////////////////////////////////////////
AccountKeyType AssetAccountId::getAssetAccountKey() const
{
   if (!isValid())
      throw IdException("[getAddressAccountKey] invalid asset account id");

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
   if (!isValid())
      throw IdException("[AssetAccountId::serialize] invalid id");

   bw.put_var_int(data_.getSize());
   bw.put_BinaryData(data_);
}

////////////////////////////////////////////////////////////////////////////////
AssetAccountId AssetAccountId::deserializeValue(BinaryRefReader& brr)
{
   auto pos = brr.getPosition();
   try
   {
      auto len = brr.get_var_int();
      return AssetAccountId(brr.get_BinaryData(len));
   }
   catch (const std::exception&)
   {
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
   if (len != sizeof(AccountKeyType))
      throw IdException("[AssetAccountId::deserializeValueOld] error");

   return AssetAccountId(id, brr.get_int32_t(BE));
}

////////////////////////////////////////////////////////////////////////////////
BinaryData AssetAccountId::getSerializedKey(uint8_t prefix) const
{
   if (!isValid())
      throw IdException("[AssetAccountId::put] invalid id");

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
   if (pref != prefix)
      throw IdException("[AssetAccountId::deserializeKey] prefix mismatch");

   const auto& idData = brr.get_BinaryData(sizeof(AccountKeyType)*2);
   if (brr.getSizeRemaining() != 0)
      throw IdException("[AssetAccountId::deserializeKey] invalid key size");

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
   if (id.getSize() != (sizeof(AccountKeyType) * 2 + sizeof(AssetKeyType)))
      throw IdException("[AssetId] invalid id");

   data_ = id;
}

////////////////////////////////////////////////////////////////////////////////
AssetId::AssetId(AccountKeyType addressAccountKey,
   AccountKeyType assetAccountKey,
   AssetKeyType assetKey)
{
   if (sizeof(AccountKeyType) != 4 || sizeof(AssetKeyType) != 4)
      throw IdException("[AssetId] invalid key type");

   BinaryWriter bw(sizeof(AccountKeyType) * 2 + sizeof(AssetKeyType));
   bw.put_int32_t(addressAccountKey, BE);
   bw.put_int32_t(assetAccountKey, BE);
   bw.put_int32_t(assetKey, BE);

   data_ = bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
AssetId::AssetId(const AssetAccountId& id, AssetKeyType key)
{
   if (sizeof(AssetKeyType) != 4)
      throw IdException("[AssetId] invalid asset key type");

   if (!id.isValid())
      throw IdException("[AssetId] invalid asset account id");

   BinaryWriter bw(id.data_.getSize() + sizeof(AssetKeyType));
   bw.put_BinaryData(id.data_);
   bw.put_int32_t(key, BE);

   data_ = bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
AssetId::AssetId(const AddressAccountId& accId,
   AssetKeyType accKey, AssetKeyType assKey)
{
   if (!accId.isValid())
      throw IdException("[AssetId] invalid address account id");

   BinaryWriter bw(
      accId.data_.getSize() + sizeof(AccountKeyType) + sizeof(AssetKeyType));
   bw.put_BinaryData(accId.data_);
   bw.put_int32_t(accKey, BE);
   bw.put_int32_t(assKey, BE);

   data_ = bw.getData();
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
         accId.data_.getPtr(),
         data_.getPtr(),
         sizeof(AccountKeyType) * 2
      ) == 0);
}

////////////////////////////////////////////////////////////////////////////////
bool AssetId::isValid() const
{
   return
      data_.getSize() == (sizeof(AccountKeyType) * 2 + sizeof(AssetKeyType));
}

////////////////////////////////////////////////////////////////////////////////
AssetKeyType AssetId::getAssetKey() const
{
   if (!isValid())
      throw IdException("[getAssetKey] invalid asset id");

   BinaryRefReader brr(data_);
   brr.advance(sizeof(AccountKeyType) * 2);
   return brr.get_int32_t(BE);
}

////////////////////////////////////////////////////////////////////////////////
AccountKeyType AssetId::getAddressAccountKey() const
{
   if (!isValid())
      throw IdException("[getAddressAccountKey] invalid asset id");

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
   if (!isValid())
      throw IdException("[getAssetAccountId] invalid asset id");

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
   if (!isValid())
      throw IdException("[AssetId::serialize] invalid id");

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
   if (!isValid())
      throw IdException("[AssetId::serialize] put id");

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
   if (pref != prefix)
      throw IdException("[AssetId::deserializeKey] prefix mismatch");

   const auto& idData = brr.get_BinaryData(
      sizeof(AccountKeyType)*2 + sizeof(AssetKeyType));
   if (brr.getSizeRemaining() != 0)
      throw IdException("[AssetId::deserializeKey] invalid key size");

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
   } catch (const VarIntException&) {
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

BinaryData KdfId::getSerializedKey() const
{
   auto dbKey = WRITE_UINT8_BE(KDF_PREFIX);
   dbKey.append(data_);
   return dbKey;
}

KdfId& KdfId::operator=(const KdfId& rhs)
{
   if (this != &rhs) {
      this->data_ = rhs.data_;
   }
   return *this;
}

KdfId KdfId::fromBinaryData(BinaryData& bd)
{
   KdfId result;
   result.data_ = std::move(bd);
   return result;
}

const BinaryData& KdfId::data() const
{
   return data_;
}

bool KdfId::isValid() const
{
   return !data_.empty();
}

bool KdfId::operator==(const KdfId& rhs) const
{
   return data_ == rhs.data_;
}

bool KdfId::operator<(const KdfId& rhs) const
{
   return data_ < rhs.data_;
}

///////////////////////// - wallet & master id - ///////////////////////////////
std::string Armory::Wallets::generateWalletId(
   std::shared_ptr<Armory::Assets::DerivationScheme> derScheme,
   std::shared_ptr<Armory::Assets::AssetEntry> rootEntry,
   Armory::Seeds::SeedType sType)
{
   auto addrVec = derScheme->extendPublicChain(rootEntry,
      1, 1 + (int)sType, nullptr);
   if (addrVec.size() != (int)sType+1)
      throw WalletException("unexpected chain derivation output");

   auto entry = std::dynamic_pointer_cast<Armory::Assets::AssetEntry_Single>(
      addrVec[int(sType)]);
   if (entry == nullptr)
      throw WalletException("unexpected asset entry type");

   return BtcUtils::computeID(entry->getPubKey()->getUncompressedKey());
}

////
std::string Armory::Wallets::generateWalletId(
   SecureBinaryData pubkey,
   SecureBinaryData chaincode,
   Armory::Seeds::SeedType sType)
{
   //sanity checks
   if (pubkey.empty())
      throw WalletException("[generateWalletId] empty pubkey");

   if (chaincode.empty())
      throw WalletException("[generateWalletId] empty chaincode");

   //create legacy armory derviation scheme from chaincode
   auto derScheme = std::make_shared<
      Armory::Assets::DerivationScheme_ArmoryLegacy>(chaincode);

   //create root pubkey asset
   auto asset_single = std::make_shared<
      Armory::Assets::AssetEntry_Single>(
         Armory::Wallets::AssetId::getRootAssetId(),
         pubkey,
         nullptr);

   //derive '(int)sType' amount of addresses, use last one as id
   return Armory::Wallets::generateWalletId(derScheme, asset_single, sType);
}

////////
std::string Armory::Wallets::generateMasterId(const SecureBinaryData& pubkey,
   const SecureBinaryData& chaincode)
{
   BinaryWriter bw;
   bw.put_BinaryData(pubkey);
   bw.put_BinaryData(chaincode);
   auto hmacMasterMsg = SecureBinaryData::fromString("MetaEntry");
   auto masterID_long = BtcUtils::getHMAC256(
      bw.getData(), hmacMasterMsg);
   return BtcUtils::computeID(masterID_long);
}
