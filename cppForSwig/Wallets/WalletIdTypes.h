////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2021-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_WALLET_ID_TYPES
#define _H_WALLET_ID_TYPES

#include <string>
#include <stdexcept>
#include "../BinaryData.h"
#include "../BtcUtils.h"

#define KDF_PREFIX 0xC1

namespace Armory
{
   namespace Bridge
   {
      class BridgePassphrasePrompt;
   }

   namespace Assets
   {
      class AssetEntry;
      class DerivationScheme;
   }

   namespace Seeds
   {
      enum class SeedType;
   }

   namespace Wallets
   {
      ////////////////////////////////////////////////////////////////////////
      using AssetKeyType = int32_t;
      using AccountKeyType = int32_t;

      static const AccountKeyType rootAccountId = -1;
      static const AssetKeyType rootAssetId = -1;
      static const AccountKeyType dummyAccountId = -2;

      static const size_t EncryptionKeyIdLength = 16;

      ////
      struct IdException : public std::runtime_error
      {
         IdException(const std::string& err) :
            std::runtime_error(err)
         {}
      };

      ////////////////////////////////////////////////////////////////////////
      class AddressAccountId
      {
         friend class AssetAccountId;
         friend class AssetId;

      private:
         BinaryData data_;

      private:
         AddressAccountId(const BinaryData&);

      public:
         AddressAccountId(void);
         AddressAccountId(const AddressAccountId&);
         AddressAccountId(AccountKeyType);

         AddressAccountId& operator=(const AddressAccountId&);
         bool operator<(const AddressAccountId&) const;
         bool operator==(const AddressAccountId&) const;
         bool operator!=(const AddressAccountId&) const;

         bool isValid(void) const;
         AccountKeyType getAddressAccountKey(void) const;
         std::string toHexStr(void) const;
         static AddressAccountId fromHex(const std::string&);

         void serializeValue(BinaryWriter&) const;
         BinaryData getSerializedKey(uint8_t) const;

         static AddressAccountId deserializeValue(BinaryRefReader&);
         static AddressAccountId deserializeValue(const BinaryData&);
         static AddressAccountId deserializeKey(const BinaryData&, uint8_t);
         static AccountKeyType getRootKey(void) { return rootAccountId; }
      };

      ////////////////////////////////////////////////////////////////////////
      class AssetAccountId
      {
         friend class AssetId;

      private:
         BinaryData data_;

      private:
         AssetAccountId(const BinaryData&);

      public:
         AssetAccountId(void);
         AssetAccountId(AccountKeyType, AccountKeyType);
         AssetAccountId(const AddressAccountId&, AccountKeyType);
         AssetAccountId(const AssetAccountId&);

         AssetAccountId& operator=(const AssetAccountId&);
         bool operator<(const AssetAccountId&) const;
         bool operator==(const AssetAccountId&) const;
         bool operator!=(const AssetAccountId&) const;

         bool isValid(void) const;
         AddressAccountId getAddressAccountId(void) const;
         AccountKeyType getAddressAccountKey(void) const;
         AccountKeyType getAssetAccountKey(void) const;
         std::string toHexStr(void) const;

         void serializeValue(BinaryWriter&) const;
         BinaryData getSerializedKey(uint8_t) const;

         static AssetAccountId deserializeValue(BinaryRefReader&);
         static AssetAccountId deserializeValueOld(
            const AddressAccountId&, BinaryRefReader&);
         static AssetAccountId deserializeKey(const BinaryData&, uint8_t);
         static AccountKeyType getRootKey(void) { return rootAccountId; }
      };

      ////////////////////////////////////////////////////////////////////////
      class AssetId
      {
      private:
         static AssetKeyType dummyId_;
         BinaryData data_;

      private:
         AssetId(const BinaryData&);

      public:
         AssetId(void);
         AssetId(const AssetId&);
         AssetId(AccountKeyType, AccountKeyType, AssetKeyType);
         AssetId(const AssetAccountId&, AssetKeyType);
         AssetId(const AddressAccountId&, AccountKeyType, AssetKeyType);

         AssetId& operator=(const AssetId&);
         bool operator<(const AssetId&) const;
         bool operator==(const AssetId&) const;
         bool operator!=(const AssetId&) const;
         bool belongsTo(const AssetAccountId&) const;

         bool isValid(void) const;
         AssetKeyType getAssetKey(void) const;
         AccountKeyType getAddressAccountKey(void) const;
         AddressAccountId getAddressAccountId(void) const;
         AssetAccountId getAssetAccountId(void) const;

         void serializeValue(BinaryWriter&) const;
         BinaryData getSerializedKey(uint8_t) const;

         static AssetId deserializeValue(BinaryRefReader&);
         static AssetId deserializeKey(const BinaryData&, uint8_t);
         static AssetId deserializeKey(BinaryDataRef, uint8_t);
         static AssetKeyType getRootKey(void) { return rootAssetId; }
         static AssetId getRootAssetId(void);
         static AssetId getNextDummyId(void);
      };

      ////////////////////////////////////////////////////////////////////////
      class EncryptionKeyId
      {
         friend class Bridge::BridgePassphrasePrompt;

      private:
         BinaryData data_;

      private:
         EncryptionKeyId(const std::string&);

      public:
         EncryptionKeyId(void);
         EncryptionKeyId(const EncryptionKeyId&);
         EncryptionKeyId(const BinaryData&);

         EncryptionKeyId& operator=(const EncryptionKeyId&);
         bool operator<(const EncryptionKeyId&) const;
         bool operator==(const EncryptionKeyId&) const;
         bool operator!=(const EncryptionKeyId&) const;

         bool isValid(void) const;
         std::string toHexStr(void) const;

         void serializeValue(BinaryWriter&) const;
         BinaryData getSerializedKey(uint8_t) const;
         static EncryptionKeyId deserializeValue(BinaryRefReader&);
      };

      ////////////////////////////////////////////////////////////////////////
      class KdfId
      {
      private:
         BinaryData data_;

      private:
         KdfId(const std::string&);

      public:
         KdfId(void);
         KdfId(const std::string_view&);
         KdfId(const KdfId&);

         bool operator<(const KdfId&) const;
         bool operator==(const KdfId&) const;
         KdfId& operator=(const KdfId&);

         bool isValid(void) const;
         bool toHexStr(void) const;
         const BinaryData& data(void) const;

         BinaryData getSerializedKey(void) const;
         static KdfId fromBinaryData(BinaryData&);
      };

      ////////////////////////////////////////////////////////////////////////
      std::string generateWalletId(std::shared_ptr<Assets::DerivationScheme>,
         std::shared_ptr<Assets::AssetEntry>, Seeds::SeedType);
      std::string generateWalletId(SecureBinaryData, SecureBinaryData,
         Seeds::SeedType);
      std::string generateMasterId(const SecureBinaryData&,
         const SecureBinaryData&);

   }// namespace Wallets
}
#endif