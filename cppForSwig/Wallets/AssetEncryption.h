////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2017-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#ifndef _H_ASSET_ENCRYPTION
#define _H_ASSET_ENCRYPTION

#include <memory>
#include <string_view>
#include "BinaryData.h"
#include "EncryptionUtils.h"
#include "WalletIdTypes.h"

#define KDF_PREFIX               0xC1
#define KDF_ROMIX_PREFIX         0xC100
#define CIPHER_BYTE              0xB2

#define PRIVKEY_BYTE             0x82
#define ENCRYPTIONKEY_BYTE       0x83

#define CIPHER_DATA_VERSION      0x00000001
#define ENCRYPTION_KEY_VERSION   0x00000001

#define HMAC_KEY_ENCRYPTIONKEYS  "EncyrptionKey"

enum CipherType
{
   CipherType_AES,
   CipherType_Serpent
};


namespace Armory
{
   namespace Wallets
   {
      class AssetWallet;
      class AssetWallet_Single;

      namespace Encryption
      {
         struct ClearTextEncryptionKey;
         class ClearTextAssetData;

         using namespace std::string_view_literals;
         constexpr std::string_view passthroughKdfId = "PASSTHROUGH_SENTINEL"sv;

         ///////////////////////////////////////////////////////////////////////
         class KeyDerivationFunction
         {
         public:
            KeyDerivationFunction(void)
            {}

            virtual ~KeyDerivationFunction(void) = 0;
            virtual SecureBinaryData deriveKey(
               const SecureBinaryData& rawKey) const = 0;
            virtual bool isSame(const KeyDerivationFunction*) const = 0;

            bool operator<(const KeyDerivationFunction& rhs)
            {
               return getId() < rhs.getId();
            }

            virtual const BinaryData& getId(void) const = 0;
            virtual BinaryData serialize(void) const = 0;
            static std::shared_ptr<KeyDerivationFunction>
               deserialize(const BinaryDataRef&);
         };

         ////////
         class KeyDerivationFunction_Romix : public KeyDerivationFunction
         {
         private:
            mutable BinaryData id_;
            unsigned iterations_;
            unsigned memTarget_;

            //NOTE: consider cycling salt per kdf, even though this is likely unnecessary
            const BinaryData salt_;

         private:
            BinaryData computeID(void) const;
            BinaryData initialize(const std::chrono::milliseconds&);

         public:
            KeyDerivationFunction_Romix(const std::chrono::milliseconds&);
            KeyDerivationFunction_Romix(unsigned, unsigned, SecureBinaryData);
            ~KeyDerivationFunction_Romix(void) override;

            //overrides
            SecureBinaryData deriveKey(const SecureBinaryData&) const override;
            bool isSame(const KeyDerivationFunction*) const override;
            BinaryData serialize(void) const override;
            const BinaryData& getId(void) const override;

            //locals
            unsigned memTarget(void) const;
            unsigned iterations(void) const;
            void prettyPrint(void) const;
         };

         ////////
         class KeyDerivationFunction_Passthrough : public KeyDerivationFunction
         {
            const BinaryData id_;

         public:
            KeyDerivationFunction_Passthrough(void);
            ~KeyDerivationFunction_Passthrough(void) override;

            //overrides
            SecureBinaryData deriveKey(const SecureBinaryData&) const override;
            bool isSame(const KeyDerivationFunction*) const override;
            BinaryData serialize(void) const override;
            const BinaryData& getId(void) const override;
         };

         ///////////////////////////////////////////////////////////////////////
         class CipherException : public std::runtime_error
         {
         public:
            CipherException(const std::string& msg) : std::runtime_error(msg)
            {}
         };

         ////
         class Cipher
         {
            /***
            Carries the cipher type and IV. Not copiable, this is by design
            to avoid IV reuse.

            Also carries identifiers to setup encryption state:
            - kdfId_: to apply the proper kdf on the encryption key
            - encryptionKeyId_: hash of the kdf'd passphrase

            A convenience getCopy method is provided to carry context over to
            a new object. This call will result in a fresh IV for the copy. IVs
            are never copied over, the iv has to be generated by the ctor instead.

            It is best to use this class within a std::unique_ptr
            ***/

         private:
            const static PRNG_Fortuna fortuna_;
            const CipherType type_;

         protected:
            const BinaryData kdfId_;
            const EncryptionKeyId encryptionKeyId_;
            const SecureBinaryData iv_;

         private:
            Cipher(const Cipher&) = delete;
            Cipher& operator=(const Cipher&) = delete;

         public:

            //tors
            Cipher(CipherType type,
               const BinaryData& kdfId,
               const EncryptionKeyId& encryptionKeyId) :
               type_(type), kdfId_(kdfId),
               encryptionKeyId_(encryptionKeyId),
               iv_(generateIV())
            {
               //General purpose ctor, IV is generated on the fly
            }

            Cipher(CipherType type,
               const BinaryData& kdfId,
               const EncryptionKeyId& encryptionKeyId,
               SecureBinaryData& iv) :
               type_(type), kdfId_(kdfId),
               encryptionKeyId_(encryptionKeyId),
               iv_(std::move(iv))
            {
               //for setting up a cipher object from existing
               //data (i.e. load from disk)

               if(iv_.getSize() != getBlockSize())
                  throw CipherException("invalid iv length");
            }

            virtual ~Cipher(void) = 0;

            //locals
            CipherType getType(void) const { return type_; }
            const BinaryData& getKdfId(void) const { return kdfId_; }
            const EncryptionKeyId& getEncryptionKeyId(void) const;
            const SecureBinaryData& getIV(void) const { return iv_; }
            SecureBinaryData generateIV(void) const;
            unsigned getBlockSize(void) const { return getBlockSize(getType()); }

            //virtuals
            virtual BinaryData serialize(void) const = 0;
            virtual std::unique_ptr<Cipher> getCopy(void) const = 0;
            virtual std::unique_ptr<Cipher> getCopy(
               const EncryptionKeyId&) const = 0;
            virtual bool isSame(Cipher* const) const = 0;

            virtual SecureBinaryData encrypt(
               ClearTextEncryptionKey* const,
               const BinaryData&, const SecureBinaryData&) const = 0;
            virtual SecureBinaryData encrypt(
               ClearTextEncryptionKey* const,
               const BinaryData&,
               ClearTextEncryptionKey* const) const = 0;

            virtual SecureBinaryData decrypt(const SecureBinaryData& key,
               const SecureBinaryData&) const = 0;

            //statics
            static std::unique_ptr<Cipher> deserialize(BinaryRefReader& brr);
            static unsigned getBlockSize(CipherType);
         };

         ///////////////////////////////////////////////////////////////////////
         class Cipher_AES : public Cipher
         {
         public:
            //tors
            Cipher_AES(const BinaryData& kdfId,
               const EncryptionKeyId& encryptionKeyId) :
               Cipher(CipherType_AES, kdfId, encryptionKeyId)
            {}

            Cipher_AES(const BinaryData& kdfId,
               const EncryptionKeyId& encryptionKeyId,
               SecureBinaryData& iv) :
               Cipher(CipherType_AES, kdfId, encryptionKeyId, iv)
            {}

            //virtuals
            BinaryData serialize(void) const override;
            std::unique_ptr<Cipher> getCopy(void) const override;
            std::unique_ptr<Cipher> getCopy(
               const EncryptionKeyId& keyId) const override;
            bool isSame(Cipher* const) const override;

            //encrypt
            SecureBinaryData encrypt(ClearTextEncryptionKey* const,
               const BinaryData& kdfId,
               const SecureBinaryData& data) const override;
            SecureBinaryData encrypt(ClearTextEncryptionKey* const,
               const BinaryData& kdfId,
               ClearTextEncryptionKey* const data) const override;

            //decrypt
            SecureBinaryData decrypt(const SecureBinaryData& key,
               const SecureBinaryData& data) const override;

            //utils
            unsigned getBlockSize(void) const;
         };

         ///////////////////////////////////////////////////////////////////////
         struct CipherData
         {
            /*
            Cipher text and its relevant cipher object
            */

            const SecureBinaryData cipherText_;
            std::unique_ptr<Cipher> cipher_;

            CipherData(SecureBinaryData&, std::unique_ptr<Cipher>);

            BinaryData serialize(void) const;
            bool isSame(CipherData* const) const;

            static std::unique_ptr<CipherData> deserialize(BinaryRefReader&);
         };

         ///////////////////////////////////////////////////////////////////////
         class EncryptionKey
         {
            /*
            May have multiple cipher data objects
            */

            friend class DecryptedDataContainer;

         protected:
            const Wallets::EncryptionKeyId id_;
            std::map<Wallets::EncryptionKeyId,
               std::unique_ptr<CipherData>> cipherDataMap_;

         private:
            Cipher* getCipherPtrForId(
               const Wallets::EncryptionKeyId&) const;
            bool removeCipherData(const Wallets::EncryptionKeyId&);
            bool addCipherData(std::unique_ptr<CipherData>);

         public:
            EncryptionKey(Wallets::EncryptionKeyId&,
               SecureBinaryData&,
               std::unique_ptr<Cipher>);

            EncryptionKey(Wallets::EncryptionKeyId&,
               std::map<Wallets::EncryptionKeyId,
                  std::unique_ptr<CipherData>>);

            ////
            bool isSame(EncryptionKey* const) const;
            const Wallets::EncryptionKeyId& getId(void) const
            { return id_; }
            BinaryData serialize(void) const;
            static std::unique_ptr<EncryptionKey> deserialize(
               const BinaryDataRef&);
            std::set<BinaryData> getKdfIds(void) const;

            /*
            TODO:
               - dedicated decrypt per sub class instead of virtual (
               otherwise the return type is always ClearTextAssetData)
               - dedicated encryption key id
            */
            std::unique_ptr<ClearTextAssetData> decrypt(
               const SecureBinaryData& key) const;
         };

         //////////////////////////////////////////////////////////////////////////
         struct ClearTextEncryptionKey
         {
            friend class DecryptedDataContainer;
            friend class Cipher_AES;
            friend class Wallets::AssetWallet_Single;
            friend class Wallets::AssetWallet;

         private:
            const SecureBinaryData rawKey_;
            std::map<BinaryData, SecureBinaryData> derivedKeys_;

         private:
            EncryptionKeyId computeId(const SecureBinaryData& key) const;
            const SecureBinaryData& getData(void) const { return rawKey_; }
            const SecureBinaryData& getDerivedKey(const BinaryData& id) const;

         public:
            ClearTextEncryptionKey(SecureBinaryData& key) :
               rawKey_(std::move(key))
            {}

            void deriveKey(
               std::shared_ptr<KeyDerivationFunction> kdf);
            EncryptionKeyId getId(const BinaryData& kdfid) const;

            std::unique_ptr<ClearTextEncryptionKey> copy(void) const;
            bool hasData(void) const { return !rawKey_.empty(); }
         };

         ///////////////////////////////////////////////////////////////////////
         class ClearTextAssetData
         {
         private:
            const AssetId id_;
            const SecureBinaryData clearText_;

         public:
            ClearTextAssetData(const AssetId& id,
               SecureBinaryData& clearText) :
               id_(id), clearText_(std::move(clearText))
            {}

            bool hasData(void) const { return !clearText_.empty(); }
            const AssetId& getId(void) const { return id_; }
            const SecureBinaryData& getData(void) const { return clearText_; }
         };

         ///////////////////////////////////////////////////////////////////////
         class EncryptedAssetData
         {
            /***
            This class holds the cipher data for an encrypted key. It can
            yield the clear text secret, provided the kdf'ed encryption key.
            It cannot generate the cipher text.

            Use Cipher::encrypt to generate the cipher text first. Pass that
            cipher data at construction time.
            ***/

            friend class DecryptedDataContainer;

         protected:
            const EncryptionKeyId& encryptionKeyId_;
            const std::unique_ptr<CipherData> cipherData_;

         public:
            EncryptedAssetData(
               std::unique_ptr<CipherData> cipherData) :
               encryptionKeyId_(cipherData->cipher_->getEncryptionKeyId()),
               cipherData_(std::move(cipherData))
            {
               if (cipherData_ == nullptr)
                  throw std::runtime_error("nullptr cipher data");
            }

            //virtual
            virtual ~EncryptedAssetData(void) = 0;

            virtual bool isSame(EncryptedAssetData* const) const;
            virtual BinaryData serialize(void) const = 0;
            virtual const AssetId& getAssetId(void) const = 0;

            virtual std::unique_ptr<ClearTextAssetData> decrypt(
               const SecureBinaryData& key) const;

            //local
            const SecureBinaryData& getCipherText(void) const;
            const SecureBinaryData& getIV(void) const;
            const EncryptionKeyId& getEncryptionKeyId(void) const;
            const BinaryData& getKdfId(void) const;

            bool hasData(void) const;
            const CipherData* getCipherDataPtr(void) const;
         };

      }; //namespace Encryption
   }; //namespace Wallets
}; //namespace Armory

#endif