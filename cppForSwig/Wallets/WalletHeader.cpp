////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "WalletHeader.h"
#include "Assets.h"
#include "AssetEncryption.h"
#include "DecryptedDataContainer.h"

#define HEADER_VERSION                    0x00000001

#define HEADER_ENCRYPTIONKEY_VERSION      0x00000001
#define HEADER_SALT_VERSION               0x00000001

#define WALLETHEADER_SINGLE_VERSION       0x00000002
#define WALLETHEADER_MULTISIG_VERSION     0x00000002
#define WALLETHEADER_SUBWALLET_VERSION    0x00000001
#define WALLETHEADER_CONTROL_VERSION      0x00000001
#define WALLETHEADER_CUSTOM_VERSION       0x00000001

using namespace Armory::Wallets::IO;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// WalletHeader
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
WalletHeader::~WalletHeader()
{}

////////////////////////////////////////////////////////////////////////////////
BinaryData WalletHeader::getDbKey()
{
   if (walletID_.empty()) {
      throw WalletException("empty master ID");
   }

   BinaryWriter bw;
   bw.put_uint8_t(WALLETHEADER_PREFIX);
   bw.put_String(walletID_);
   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
BinaryData WalletHeader_Control::serializeVersion() const
{
   BinaryWriter bw;
   bw.put_uint32_t(HEADER_VERSION);
   bw.put_uint8_t(versionMajor_);
   bw.put_uint16_t(versionMinor_);
   bw.put_uint16_t(revision_);
   bw.put_uint32_t(ENCRYPTION_TOPLAYER_VERSION);

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
void WalletHeader_Control::unseralizeVersion(BinaryRefReader& brr)
{
   auto version = brr.get_uint32_t();
   switch (version)
   {
      case 0x00000001:
      {
         versionMajor_ = brr.get_uint8_t();
         versionMinor_ = brr.get_uint16_t();
         revision_ = brr.get_uint16_t();
         encryptionVersion_ = brr.get_uint32_t();
         break;
      }

      default:
         throw WalletException("unsupported version packet");
   }
}

////////////////////////////////////////////////////////////////////////////////
BinaryData WalletHeader::serializeEncryptionKey() const
{
   BinaryWriter bw;
   bw.put_uint32_t(HEADER_ENCRYPTIONKEY_VERSION);

   defaultEncryptionKeyId_.serializeValue(bw);
   bw.put_var_int(defaultEncryptionKey_.getSize());
   bw.put_BinaryData(defaultEncryptionKey_);

   bw.put_var_int(defaultKdfId_.data().getSize());
   bw.put_BinaryData(defaultKdfId_.data());
   masterEncryptionKeyId_.serializeValue(bw);

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
void WalletHeader::unserializeEncryptionKey(BinaryRefReader& brr)
{
   auto version = brr.get_uint32_t();
   switch (version)
   {
      case 0x00000001:
      {
         auto len = brr.get_var_int();
         defaultEncryptionKeyId_ = std::move(brr.get_BinaryData(len));

         len = brr.get_var_int();
         defaultEncryptionKey_ = std::move(brr.get_BinaryData(len));

         len = brr.get_var_int();
         auto kdfId = brr.get_BinaryData(len);
         defaultKdfId_ = KdfId::fromBinaryData(kdfId);

         len = brr.get_var_int();
         masterEncryptionKeyId_ = std::move(brr.get_BinaryData(len));

         break;
      }

      default:
         throw WalletException("unsupported header encryption key version");
   }
}

////////////////////////////////////////////////////////////////////////////////
BinaryData WalletHeader::serializeControlSalt() const
{
   BinaryWriter bw;
   bw.put_uint32_t(HEADER_SALT_VERSION);
   bw.put_var_int(controlSalt_.getSize());
   bw.put_BinaryData(controlSalt_);
   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
void WalletHeader::unserializeControlSalt(BinaryRefReader& brr)
{
   auto version = brr.get_uint32_t();
   switch (version)
   {
      case 0x00000001:
      {
         auto len = brr.get_var_int();
         controlSalt_ = brr.get_SecureBinaryData(len);
         break;
      }

      default:
         throw WalletException("unsupported header salt version");
   }
}

////////////////////////////////////////////////////////////////////////////////
BinaryData WalletHeader_Single::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(WALLETHEADER_SINGLE_VERSION);
   bw.put_uint32_t(type_);
   bw.put_BinaryData(magicBytes_);
   bw.put_BinaryData(serializeEncryptionKey());
   bw.put_BinaryData(serializeControlSalt());

   BinaryWriter final_bw;
   final_bw.put_var_int(bw.getSize());
   final_bw.put_BinaryDataRef(bw.getDataRef());
   return final_bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
bool WalletHeader_Single::shouldLoad() const
{
   return true;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData WalletHeader_Multisig::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(WALLETHEADER_MULTISIG_VERSION);
   bw.put_uint32_t(type_);
   bw.put_BinaryData(magicBytes_);
   bw.put_BinaryData(serializeEncryptionKey());
   bw.put_BinaryData(serializeControlSalt());

   BinaryWriter final_bw;
   final_bw.put_var_int(bw.getSize());
   final_bw.put_BinaryDataRef(bw.getDataRef());
   return final_bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
bool WalletHeader_Multisig::shouldLoad() const
{
   return true;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData WalletHeader_Subwallet::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(WALLETHEADER_SUBWALLET_VERSION);
   bw.put_var_int(4);
   bw.put_uint32_t(type_);
   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
bool WalletHeader_Subwallet::shouldLoad() const
{
   return false;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData WalletHeader_Control::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(WALLETHEADER_CONTROL_VERSION);
   bw.put_uint32_t(type_);
   bw.put_BinaryData(serializeVersion());
   bw.put_BinaryData(serializeEncryptionKey());
   bw.put_BinaryData(serializeControlSalt());

   BinaryWriter final_bw;
   final_bw.put_var_int(bw.getSize());
   final_bw.put_BinaryDataRef(bw.getDataRef());
   return final_bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
bool WalletHeader_Control::shouldLoad() const
{
   return true;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData WalletHeader_Custom::serialize() const
{
   BinaryWriter bw;
   bw.put_uint32_t(WALLETHEADER_CUSTOM_VERSION);
   bw.put_uint32_t(type_);

   BinaryWriter final_bw;
   final_bw.put_var_int(bw.getSize());
   final_bw.put_BinaryDataRef(bw.getDataRef());
   return final_bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
bool WalletHeader_Custom::shouldLoad() const
{
   return true;
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<WalletHeader> WalletHeader::deserialize(
   BinaryDataRef key, BinaryDataRef val)
{
   if (key.getSize() < 2) {
      throw WalletException("invalid meta key");
   }

   BinaryRefReader brrKey(key);
   auto prefix = brrKey.get_uint8_t();
   if (prefix != WALLETHEADER_PREFIX) {
      throw WalletException("invalid wallet meta prefix");
   }
   std::string dbname{(char*)brrKey.getCurrPtr(), brrKey.getSizeRemaining()};

   BinaryRefReader brrVal(val);
   auto version = brrVal.get_uint32_t();
   auto wltType = (WalletHeaderType)brrVal.get_uint32_t();

   std::shared_ptr<WalletHeader> wltHeaderPtr;
   switch (wltType)
   {
      case WalletHeaderType_Single:
      {
         switch (version)
         {
            case 0x00000001:
            {
               wltHeaderPtr = std::make_shared<WalletHeader_Single>(
                  Armory::Config::BitcoinSettings::getMainnetMagicBytes());
               wltHeaderPtr->unserializeEncryptionKey(brrVal);
               wltHeaderPtr->unserializeControlSalt(brrVal);
               break;
            }

            case 0x00000002:
            {
               auto magicBytes = brrVal.get_BinaryData(4);
               wltHeaderPtr = std::make_shared<WalletHeader_Single>(magicBytes);
               wltHeaderPtr->unserializeEncryptionKey(brrVal);
               wltHeaderPtr->unserializeControlSalt(brrVal);
               break;
            }

            default:
               throw WalletException("unsupported wallet header version");
         }
         break;
      }

      case WalletHeaderType_Subwallet:
      {
         switch (version)
         {
            case 0x00000001:
            {
               wltHeaderPtr = std::make_shared<WalletHeader_Subwallet>();
               break;
            }

            default:
               throw WalletException("unsupported subwallet header version");
         }
         break;
      }

   case WalletHeaderType_Multisig:
   {
      switch (version)
      {
         case 0x00000001:
         {
            wltHeaderPtr = std::make_shared<WalletHeader_Multisig>(
               Armory::Config::BitcoinSettings::getMainnetMagicBytes());
            wltHeaderPtr->unserializeEncryptionKey(brrVal);
            wltHeaderPtr->unserializeControlSalt(brrVal);
            break;
         }

         case 0x00000002:
         {
            auto magicBytes = brrVal.get_BinaryData(4);
            wltHeaderPtr = std::make_shared<WalletHeader_Multisig>(magicBytes);
            wltHeaderPtr->unserializeEncryptionKey(brrVal);
            wltHeaderPtr->unserializeControlSalt(brrVal);
            break;
         }

         default:
            throw WalletException("unsupported mswallet header version");
      }
      break;
   }

   case WalletHeaderType_Control:
   {
      switch (version)
      {
         case 0x00000001:
         {
            auto headerPtr = std::make_shared<WalletHeader_Control>();
            headerPtr->unseralizeVersion(brrVal);
            headerPtr->unserializeEncryptionKey(brrVal);
            headerPtr->unserializeControlSalt(brrVal);

            wltHeaderPtr = headerPtr;
            break;
         }

         default:
            throw WalletException("unsupported control header version");
      }
      break;
   }

   case WalletHeaderType_Custom:
   {
      switch (version)
      {
         case 0x00000001:
         {
            wltHeaderPtr = std::make_shared<WalletHeader_Custom>();
            break;
         }

         default:
            throw WalletException("unsupported custom header version");
      }
      break;
   }

      default:
         throw WalletException("invalid wallet type");
   }

   wltHeaderPtr->walletID_ = brrKey.get_String(brrKey.getSizeRemaining());
   return wltHeaderPtr;
}