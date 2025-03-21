////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2023, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "../AssetEncryption.h"
#include "../DecryptedDataContainer.h"
#include "../DerivationScheme.h"
#include "../Assets.h"
#include "../WalletIdTypes.h"
#include "../BIP32_Node.h"
#include "Seeds.h"
#include "Backups.h"
#include "BtcUtils.h"
extern "C" {
#include <trezor-crypto/bip39.h>
}

using namespace std;
using namespace Armory;
using namespace Armory::Seeds;
using namespace Armory::Wallets;
using namespace Armory::Assets;

#define ENCRYPTED_SEED_VERSION_1 0x00000001
#define ENCRYPTED_SEED_VERSION_2 0x00000002
#define WALLET_SEED_BYTE         0x84

////////////////////////////////////////////////////////////////////////////////
//
//// EncryptedSeed
//
////////////////////////////////////////////////////////////////////////////////
const Wallets::AssetId EncryptedSeed::seedAssetId_(0x5EED, 0xDEE5, 0x5EED);

EncryptedSeed::EncryptedSeed(CipherText cipher, SeedType sType) :
   Encryption::EncryptedAssetData(move(cipher)), type_(sType)
{}

EncryptedSeed::~EncryptedSeed()
{}

SeedType EncryptedSeed::type() const
{
   return type_;
}

//////////////////////////////-- overrides --///////////////////////////////////
BinaryData EncryptedSeed::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(ENCRYPTED_SEED_VERSION_2);
   bw.put_uint8_t(WALLET_SEED_BYTE);
   bw.put_int32_t((int32_t)type());

   auto&& cipherData = getCipherDataPtr()->serialize();
   bw.put_var_int(cipherData.getSize());
   bw.put_BinaryData(cipherData);

   BinaryWriter finalBw;
   finalBw.put_var_int(bw.getSize());
   finalBw.put_BinaryDataRef(bw.getDataRef());
   return finalBw.getData();
}

////////
bool EncryptedSeed::isSame(Encryption::EncryptedAssetData* const seed) const
{
   auto asset_ed = dynamic_cast<EncryptedSeed*>(seed);
   if (asset_ed == nullptr)
      return false;

   return Encryption::EncryptedAssetData::isSame(seed);
}

//////////////////////////////-- statics --/////////////////////////////////////
const AssetId& EncryptedSeed::getAssetId() const
{
   return seedAssetId_;
}

////
unique_ptr<EncryptedSeed> EncryptedSeed::deserialize(
   const BinaryDataRef& data)
{
   BinaryRefReader brr(data);

   //return ptr
   unique_ptr<EncryptedSeed> assetPtr = nullptr;

   //version
   auto version = brr.get_uint32_t();

   //prefix
   auto prefix = brr.get_uint8_t();

   switch (prefix)
   {
   case WALLET_SEED_BYTE:
   {
      switch (version)
      {
      case 0x00000001:
      {
         //cipher data
         auto len = brr.get_var_int();
         if (len > brr.getSizeRemaining())
            throw runtime_error("[EncryptedSeed::deserialize]"
               " invalid serialized encrypted data len");

         auto cipherBdr = brr.get_BinaryDataRef(len);
         BinaryRefReader cipherBrr(cipherBdr);
         auto cipherData = Encryption::CipherData::deserialize(cipherBrr);

         //seed object
         assetPtr = make_unique<EncryptedSeed>(move(cipherData), SeedType::Raw);
         break;
      }

      case 0x00000002:
      {
         //seed type
         auto sType = (SeedType)brr.get_int32_t();

         //cipher data
         auto len = brr.get_var_int();
         if (len > brr.getSizeRemaining())
            throw runtime_error("[EncryptedSeed::deserialize]"
               " invalid serialized encrypted data len");

         auto cipherBdr = brr.get_BinaryDataRef(len);
         BinaryRefReader cipherBrr(cipherBdr);
         auto cipherData = Encryption::CipherData::deserialize(cipherBrr);
         assetPtr = make_unique<EncryptedSeed>(move(cipherData), sType);
         break;
      }

      default:
         throw runtime_error("[EncryptedSeed::deserialize]"
            " unsupported seed version");
      }

      break;
   }

   default:
      throw runtime_error("[EncryptedSeed::deserialize]"
         " unexpected encrypted data prefix");
   }

   if (assetPtr == nullptr)
   {
      throw runtime_error("[EncryptedSeed::deserialize]"
         " failed to deserialize encrypted asset");
   }

   return assetPtr;
}

////
std::unique_ptr<EncryptedSeed> EncryptedSeed::fromClearTextSeed(
   std::unique_ptr<ClearTextSeed> seed,
   std::unique_ptr<Wallets::Encryption::Cipher> cipher,
   std::shared_ptr<Wallets::Encryption::DecryptedDataContainer> decrCont)
{
   //sanity checks
   if (seed == nullptr) {
      throw std::runtime_error("[fromClearTextSeed] null seed");
   }
   if (cipher == nullptr) {
      throw std::runtime_error("[fromClearTextSeed] null cipher");
   }
   if (decrCont == nullptr) {
      throw std::runtime_error("[fromClearTextSeed] null decrypted data container");
   }

   //copy the cipher to cycle the IV
   auto cipherCopy = cipher->getCopy();

   //serialized clear text seed
   BinaryWriter bw; //TODO: need SBD based bw
   seed->serialize(bw);

   //encrypt it
   auto cipherText = decrCont->encryptData(cipherCopy.get(), bw.getData());
   auto cipherData = std::make_unique<Encryption::CipherData>(
      cipherText, std::move(cipherCopy));

   //instantiate encrypted seed object
   return std::make_unique<EncryptedSeed>(std::move(cipherData), seed->type());
}

////////////////////////////////////////////////////////////////////////////////
//
//// ClearTextSeed
//
////////////////////////////////////////////////////////////////////////////////
ClearTextSeed::ClearTextSeed(SeedType sType) :
   type_(sType)
{}

ClearTextSeed::~ClearTextSeed()
{}

SeedType ClearTextSeed::type() const
{
   return type_;
}

////
const string& ClearTextSeed::getWalletId() const
{
   if (walletId_.empty())
      walletId_ = computeWalletId();
   return walletId_;
}

const string& ClearTextSeed::getMasterId() const
{
   if (masterId_.empty())
      masterId_ = computeMasterId();
   return masterId_;
}

unique_ptr<ClearTextSeed> ClearTextSeed::deserialize(
   const SecureBinaryData& serializedData)
{
   BinaryRefReader brr(serializedData);
   auto type = (SeedType)brr.get_uint8_t();

   //sanity check
   auto len = brr.get_var_int();
   if (len != brr.getSizeRemaining())
   {
      throw runtime_error("[ClearTextSeed::deserialize]"
         " size mismatch in serialized seed");
   }

   switch (type)
   {
      case SeedType::Armory135:
      {
         ClearTextSeed_Armory135::LegacyType lType =
            ClearTextSeed_Armory135::LegacyType::Armory200;
         BinaryDataRef root;
         BinaryDataRef chaincode;
         while (!brr.isEndOfStream())
         {
            auto prefix = (Prefix)brr.get_uint8_t();
            switch (prefix)
            {
               case Prefix::LegacyType:
               {
                  lType =
                     (ClearTextSeed_Armory135::LegacyType)brr.get_uint8_t();
                  break;
               }

               case Prefix::Root:
               {
                  auto rootLen = brr.get_var_int();
                  root = brr.get_BinaryDataRef(rootLen);
                  break;
               }

               case Prefix::Chaincode:
               {
                  auto chLen = brr.get_var_int();
                  chaincode = brr.get_BinaryDataRef(chLen);
                  break;
               }

               default:
                  throw runtime_error("[ClearTextSeed::deserialize]"
                     " invalid prefix for Armory135 seed");
            }
         }
         return make_unique<ClearTextSeed_Armory135>(root, chaincode, lType);
      }

      case SeedType::BIP32_Structured:
      case SeedType::BIP32_Virgin:
      {
         BinaryDataRef rawEntropy;
         while (!brr.isEndOfStream())
         {
            auto prefix = (Prefix)brr.get_uint8_t();
            switch (prefix)
            {
               case Prefix::RawEntropy:
               {
                  auto entLen = brr.get_var_int();
                  rawEntropy = brr.get_BinaryDataRef(entLen);
                  break;
               }

               default:
                  throw runtime_error("[ClearTextSeed::deserialize]"
                     " invalid prefix for BIP32 seed");
            }
         }
         return make_unique<ClearTextSeed_BIP32>(rawEntropy, type);
      }

      case SeedType::BIP32_base58Root:
      {
         BinaryDataRef b58root;
         while (!brr.isEndOfStream())
         {
            auto prefix = (Prefix)brr.get_uint8_t();
            switch (prefix)
            {
               case Prefix::Base58Root:
               {
                  auto entLen = brr.get_var_int();
                  b58root = brr.get_BinaryDataRef(entLen);
                  break;
               }

               default:
                  throw runtime_error("[ClearTextSeed::deserialize]"
                     " invalid prefix for BIP32 seed");
            }
         }
         return ClearTextSeed_BIP32::fromBase58(b58root);
      }

      case SeedType::BIP39:
      {
         BinaryDataRef rawEntropy;
         ClearTextSeed_BIP39::Dictionnary dictionnary;
         while (!brr.isEndOfStream())
         {
            auto prefix = (Prefix)brr.get_uint8_t();
            switch (prefix)
            {
               case Prefix::RawEntropy:
               {
                  auto entLen = brr.get_var_int();
                  rawEntropy = brr.get_BinaryDataRef(entLen);
                  break;
               }

               case Prefix::Dictionnary:
               {
                  dictionnary =
                     (ClearTextSeed_BIP39::Dictionnary)brr.get_uint32_t();
                  break;
               }

               default:
                  throw runtime_error("[ClearTextSeed::deserialize]"
                     " invalid prefix for BIP39 seed");
            }
         }
         return make_unique<ClearTextSeed_BIP39>(rawEntropy, dictionnary);
      }

      default:
         throw runtime_error("[ClearTextSeed::deserialize]"
            " unexpected seed type");
   }
}

////////////////////////////////////////////////////////////////////////////////
ClearTextSeed_Armory135::ClearTextSeed_Armory135(LegacyType lType) :
   ClearTextSeed_Armory135(CryptoPRNG::generateRandom(32), lType)
{}

ClearTextSeed_Armory135::ClearTextSeed_Armory135(
   const SecureBinaryData& root, LegacyType lType) :
   ClearTextSeed(SeedType::Armory135),
   root_(root), chaincode_({}),
   legacyType_(lType)
{}

ClearTextSeed_Armory135::ClearTextSeed_Armory135(const SecureBinaryData& root,
   const SecureBinaryData& chaincode, LegacyType lType) :
   ClearTextSeed(SeedType::Armory135),
   root_(root), chaincode_(chaincode),
   legacyType_(lType)
{}

ClearTextSeed_Armory135::~ClearTextSeed_Armory135()
{}

////
const SecureBinaryData& ClearTextSeed_Armory135::getRoot() const
{
   return root_;
}

const SecureBinaryData& ClearTextSeed_Armory135::getChaincode() const
{
   return chaincode_;
}

////
string ClearTextSeed_Armory135::computeWalletId() const
{
   auto chaincodeCopy = chaincode_;
   if (chaincode_.empty())
      chaincodeCopy = BtcUtils::computeChainCode_Armory135(root_);

   auto pubkey = CryptoECDSA().ComputePublicKey(root_);
   return Armory::Wallets::generateWalletId(pubkey, chaincodeCopy, type());
}

string ClearTextSeed_Armory135::computeMasterId() const
{
   //uncompressed pubkey
   auto pubkey = CryptoECDSA().ComputePublicKey(root_);
   return generateMasterId(pubkey, chaincode_);
}

////
void ClearTextSeed_Armory135::serialize(BinaryWriter& bw) const
{
   /* serialize the seed */
   BinaryWriter inner;

   //legacy type
   inner.put_uint8_t((uint8_t)Prefix::LegacyType);
   inner.put_uint8_t((uint8_t)legacyType_);

   //root
   inner.put_uint8_t((uint8_t)Prefix::Root);
   inner.put_var_int(root_.getSize());
   inner.put_BinaryData(root_);

   //chaincode
   inner.put_uint8_t((uint8_t)Prefix::Chaincode);
   inner.put_var_int(chaincode_.getSize());
   if (!chaincode_.empty())
      inner.put_BinaryData(chaincode_);

   /* append to writer */

   //seed type
   bw.put_uint8_t((uint8_t)type());

   //packet size
   bw.put_var_int(inner.getSize());

   //packet
   bw.put_BinaryData(inner.getData());
}

////
bool ClearTextSeed_Armory135::isBackupTypeEligible(BackupType bType) const
{
   switch (legacyType_)
   {
      case LegacyType::Armory135:
         return bType == BackupType::Armory135;

      case LegacyType::Armory200:
         return bType == BackupType::Armory200a;

      default:
         break;
   }
   return false;
}

BackupType ClearTextSeed_Armory135::getPreferedBackupType() const
{
   switch (legacyType_)
   {
      case LegacyType::Armory135:
         return BackupType::Armory135;

      case LegacyType::Armory200:
         return BackupType::Armory200a;

      default:
         break;
   }
   return BackupType::Invalid;
}

////////////////////////////////////////////////////////////////////////////////
ClearTextSeed_BIP32::ClearTextSeed_BIP32(SeedType sType) :
   ClearTextSeed_BIP32(CryptoPRNG::generateRandom(32), sType)
{}

ClearTextSeed_BIP32::ClearTextSeed_BIP32(const SecureBinaryData& raw,
   SeedType sType) :
   ClearTextSeed(sType), rawEntropy_(raw)
{
   switch (sType)
   {
      case SeedType::BIP32_Structured:
      case SeedType::BIP32_Virgin:
      case SeedType::BIP32_base58Root:
      case SeedType::BIP39:
         break;

      default:
         throw runtime_error("invalid bip32 seed type");
   }
}

ClearTextSeed_BIP32::~ClearTextSeed_BIP32()
{}

unique_ptr<ClearTextSeed_BIP32> ClearTextSeed_BIP32::fromBase58(
   const BinaryDataRef& b58)
{
   auto result = make_unique<ClearTextSeed_BIP32>(SeedType::BIP32_base58Root);
   result->rootNode_ = make_shared<BIP32_Node>();
   result->rootNode_->initFromBase58(b58);
   return move(result);
}

////
string ClearTextSeed_BIP32::computeWalletId() const
{
   const auto& rootNode = getRootNode();
   return Armory::Wallets::generateWalletId(rootNode->getPublicKey(),
      rootNode->getChaincode(), type());
}

string ClearTextSeed_BIP32::computeMasterId() const
{
   //uncompressed pubkey
   const auto& rootNode = getRootNode();
   return generateMasterId(rootNode->getPublicKey(), rootNode->getChaincode());
}

////
std::shared_ptr<BIP32_Node> ClearTextSeed_BIP32::getRootNode() const
{
   if (rootNode_ == nullptr)
   {
      rootNode_ = make_shared<BIP32_Node>();
      rootNode_->initFromSeed(rawEntropy_);
   }
   return rootNode_;
}

const SecureBinaryData& ClearTextSeed_BIP32::getRawEntropy() const
{
   return rawEntropy_;
}

////
void ClearTextSeed_BIP32::serialize(BinaryWriter& bw) const
{
   /* serialize the seed */
   BinaryWriter inner;

   //root
   switch (type())
   {
      case SeedType::BIP32_Structured:
      case SeedType::BIP32_Virgin:
      {
         inner.put_uint8_t((uint8_t)Prefix::RawEntropy);
         inner.put_var_int(rawEntropy_.getSize());
         inner.put_BinaryData(rawEntropy_);
         break;
      }

      case SeedType::BIP32_base58Root:
      {
         inner.put_uint8_t((uint8_t)Prefix::Base58Root);
         auto root = getRootNode()->getBase58();
         inner.put_var_int(root.getSize());
         inner.put_BinaryData(root);
         break;
      }

      default:
         throw runtime_error("[ClearTextSeed_BIP32::serialize]"
            " unexpected seed type");
   }

   /* append to writer */

   //seed type
   bw.put_uint8_t((uint8_t)type());

   //packet size
   bw.put_var_int(inner.getSize());

   //packet
   bw.put_BinaryData(inner.getData());
}

////
bool ClearTextSeed_BIP32::isBackupTypeEligible(BackupType bType) const
{
   switch (type())
   {
      case SeedType::BIP32_Structured:
         return bType == BackupType::Armory200b;

      case SeedType::BIP32_Virgin:
         return bType == BackupType::Armory200c;

      case SeedType::BIP32_base58Root:
         return bType == BackupType::Base58;

      default:
         break;
   }

   return false;
}

BackupType ClearTextSeed_BIP32::getPreferedBackupType() const
{
   switch (type())
   {
      case SeedType::BIP32_Structured:
         return BackupType::Armory200b;

      case SeedType::BIP32_Virgin:
         return BackupType::Armory200c;

      case SeedType::BIP32_base58Root:
         return BackupType::Base58;

      default:
         break;
   }
   return BackupType::Invalid;
}

////////////////////////////////////////////////////////////////////////////////
ClearTextSeed_BIP39::ClearTextSeed_BIP39(const SecureBinaryData& raw,
   Dictionnary dictType) :
   ClearTextSeed_BIP32(raw, SeedType::BIP39), dictionnary_(dictType)
{}

ClearTextSeed_BIP39::ClearTextSeed_BIP39(Dictionnary dictType) :
   ClearTextSeed_BIP32(CryptoPRNG::generateRandom(32), SeedType::BIP39),
   dictionnary_(dictType)
{}

ClearTextSeed_BIP39::~ClearTextSeed_BIP39()
{}

////
std::shared_ptr<BIP32_Node> ClearTextSeed_BIP39::getRootNode() const
{
   if (rootNode_ == nullptr)
      setupRootNode();
   return rootNode_;
}

////
void ClearTextSeed_BIP39::setupRootNode() const
{
   //sanity checks
   if (rawEntropy_.empty())
   {
      throw runtime_error("[ClearTextSeed_BIP39::setupRootNode]"
         " missing raw entropy");
   }

   if (rootNode_ != nullptr)
   {
      throw runtime_error("[ClearTextSeed_BIP39::setupRootNode]"
         " already have root node");
   }

   switch (dictionnary_)
   {
      case Dictionnary::English_Trezor:
      {
         //clear libbtc/trezor bip39 mnemonic buffer
         mnemonic_clear();

         //convert raw entropy to mnemonic phrase
         auto mnemonicPtr = mnemonic_from_data(
            rawEntropy_.getPtr(), rawEntropy_.getSize());

         //convert mnemonic phrase to seed
         SecureBinaryData seed64(64);
         mnemonic_to_seed(
            mnemonicPtr, //the mnemonic string
            "", //passphrase, null for now
            seed64.getPtr(), //result buffer
            nullptr); //progress callback, dont care for now

         //clean up libbtc buffer
         mnemonic_clear();

         //setup root node
         rootNode_ = make_shared<BIP32_Node>();
         rootNode_->initFromSeed(seed64);
         break;
      }

      default:
         throw runtime_error("[ClearTextSeed_BIP39::setupRootNode]"
            " unexpected dictionnary id");
   }
}

ClearTextSeed_BIP39::Dictionnary ClearTextSeed_BIP39::getDictionnaryId() const
{
   return dictionnary_;
}

////
void ClearTextSeed_BIP39::serialize(BinaryWriter& bw) const
{
   /* serialize the seed */
   BinaryWriter inner;

   //root
   inner.put_uint8_t((uint8_t)Prefix::RawEntropy);
   inner.put_var_int(rawEntropy_.getSize());
   inner.put_BinaryData(rawEntropy_);

   //dictionnary id
   inner.put_uint8_t((uint8_t)Prefix::Dictionnary);
   inner.put_uint32_t((uint32_t)dictionnary_);

   /* append to writer */

   //seed type
   bw.put_uint8_t((uint8_t)type());

   //packet size
   bw.put_var_int(inner.getSize());

   //packet
   bw.put_BinaryData(inner.getData());
}

////
bool ClearTextSeed_BIP39::isBackupTypeEligible(BackupType bType) const
{
   //BIP39 seeds can be backed up to either the easy16 format or the
   //mnemonic phrase, interchangeably
   return bType == BackupType::Armory200d || bType == BackupType::BIP39;
}

BackupType ClearTextSeed_BIP39::getPreferedBackupType() const
{
   return BackupType::BIP39;
}
