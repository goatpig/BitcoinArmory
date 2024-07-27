////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ScriptRecipient.h"
#include "TxClasses.h"
#include "Signer.h"

using namespace std;
using namespace Armory::Signing;

////////////////////////////////////////////////////////////////////////////////
//
// ScriptRecipient
//
////////////////////////////////////////////////////////////////////////////////
ScriptRecipient::~ScriptRecipient()
{}

////////////////////////////////////////////////////////////////////////////////
const map<BinaryData, BIP32_AssetPath>& ScriptRecipient::getBip32Paths() const
{
   return bip32Paths_; 
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<ScriptRecipient> ScriptRecipient::fromScript(BinaryDataRef dataRef)
{
   shared_ptr<ScriptRecipient> result_ptr;

   BinaryRefReader brr(dataRef);

   auto value = brr.get_uint64_t();
   auto script = brr.get_BinaryDataRef(brr.getSizeRemaining());

   BinaryRefReader brr_script(script);

   auto byte0 = brr_script.get_uint8_t();
   auto byte1 = brr_script.get_uint8_t();
   auto byte2 = brr_script.get_uint8_t();

   if (byte0 == 25 && byte1 == OP_DUP && byte2 == OP_HASH160)
   {
      auto byte3 = brr_script.get_uint8_t();
      if (byte3 == 20)
      {
         auto&& hash160 = brr_script.get_BinaryData(20);
         result_ptr = make_shared<Recipient_P2PKH>(hash160, value);
      }
   }
   else if (byte0 == 22 && byte1 == 0 && byte2 == 20)
   {
      auto&& hash160 = brr_script.get_BinaryData(20);
      result_ptr = make_shared<Recipient_P2WPKH>(hash160, value);
   }
   else if (byte0 == 23 && byte1 == OP_HASH160 && byte2 == 20)
   {
      auto&& hash160 = brr_script.get_BinaryData(20);
      result_ptr = make_shared<Recipient_P2SH>(hash160, value);
   }
   else if (byte0 == 34 && byte1 == 0 && byte2 == 32)
   {
      auto&& hash256 = brr_script.get_BinaryData(32);
      result_ptr = make_shared<Recipient_P2WSH>(hash256, value);
   }
   else
   {
      //is this an OP_RETURN?
      if (byte0 == script.getSize() - 1 && byte1 == OP_RETURN)
      {
         if (byte2 == OP_PUSHDATA1)
            byte2 = brr_script.get_uint8_t();

         auto&& opReturnMessage = brr_script.get_BinaryData(byte2);
         result_ptr = make_shared<Recipient_OPRETURN>(opReturnMessage);
      }
   }

   if (result_ptr == nullptr)
      throw ScriptRecipientException("unexpected recipient script");

   return result_ptr;
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<ScriptRecipient> ScriptRecipient::fromPSBT(
   BinaryRefReader& brr, const TxOut& txout)
{
   auto globalDataPairs = BtcUtils::getPSBTDataPairs(brr);
   map<BinaryData, BIP32_AssetPath> bip32Paths;
   std::map<BinaryData, BinaryData> prioprietaryPSBTData;

   for (const auto& dataPair : globalDataPairs)
   {
      const auto& key = dataPair.first;
      const auto& val = dataPair.second;
      
      //key type
      auto typePtr = key.getPtr();

      switch (*typePtr)
      {
      case PSBT::ENUM_OUTPUT::PSBT_OUT_BIP32_DERIVATION:
      {
         auto assetPath = BIP32_AssetPath::fromPSBT(key, val);
         auto insertIter = bip32Paths.emplace(
            assetPath.getPublicKey(), move(assetPath));

         if (!insertIter.second)
         {
            throw PSBTDeserializationError(
               "txout pubkey collision");
         }

         break;
      }

      case PSBT::ENUM_OUTPUT::PSBT_OUT_PROPRIETARY:
      {
         prioprietaryPSBTData.emplace(
            key.getSliceRef(1, key.getSize() - 1), val);
         break;
      }

      default: 
         throw PSBTDeserializationError("unexpected txout key");
      }
   }

   auto scriptRecipient = ScriptRecipient::fromScript(txout.serializeRef());
   scriptRecipient->bip32Paths_ = move(bip32Paths);
   scriptRecipient->prioprietaryPSBTData_ = move(prioprietaryPSBTData);

   return scriptRecipient;
}

////////////////////////////////////////////////////////////////////////////////
void ScriptRecipient::toPSBT(BinaryWriter& bw) const
{
   for (auto& bip32Path : bip32Paths_)
   {
      bw.put_uint8_t(34); //key length
      bw.put_uint8_t( //key type
         PSBT::ENUM_OUTPUT::PSBT_OUT_BIP32_DERIVATION);
      bw.put_BinaryData(bip32Path.first);

      //path
      bip32Path.second.toPSBT(bw);
   }

   for (auto& data : prioprietaryPSBTData_)
   {
      //key
      bw.put_var_int(data.first.getSize() + 1);
      bw.put_uint8_t(
         PSBT::ENUM_OUTPUT::PSBT_OUT_PROPRIETARY);
      bw.put_BinaryData(data.first);

      //val
      bw.put_var_int(data.second.getSize());
      bw.put_BinaryData(data.second);
   }

   //terminate
   bw.put_uint8_t(0);
}

////////////////////////////////////////////////////////////////////////////////
void ScriptRecipient::addBip32Path(const BIP32_AssetPath& bip32Path)
{
   auto insertIter = bip32Paths_.emplace(bip32Path.getPublicKey(), bip32Path);
   if (!insertIter.second)
   {
      if (insertIter.first->second != bip32Path)
         throw ScriptRecipientException("bip32Path conflict");
   }
}

////////////////////////////////////////////////////////////////////////////////
void ScriptRecipient::merge(shared_ptr<ScriptRecipient> recipientPtr)
{
   if (type_ != recipientPtr->type_ || 
      value_ != recipientPtr->value_)
      throw ScriptRecipientException("recipient mismatch");

   serialize();
   recipientPtr->serialize();
   if (script_ != recipientPtr->script_)
      throw ScriptRecipientException("recipient mismatch");

   bip32Paths_.insert(
      recipientPtr->bip32Paths_.begin(),
      recipientPtr->bip32Paths_.end());

   prioprietaryPSBTData_.insert(
      recipientPtr->prioprietaryPSBTData_.begin(),
      recipientPtr->prioprietaryPSBTData_.end());
}

////////////////////////////////////////////////////////////////////////////////
//
// Recipient_P2PKH
//
////////////////////////////////////////////////////////////////////////////////
void Recipient_P2PKH::serialize() const
{
   BinaryWriter bw;
   bw.put_uint64_t(getValue());

   auto&& rawScript = BtcUtils::getP2PKHScript(h160_);
   bw.put_var_int(rawScript.getSize());
   bw.put_BinaryData(rawScript);

   script_ = std::move(bw.getData());
}

////////////////////////////////////////////////////////////////////////////////
size_t Recipient_P2PKH::getSize() const 
{ 
   return 34; 
}

////////////////////////////////////////////////////////////////////////////////
//
// Recipient_P2PK
//
////////////////////////////////////////////////////////////////////////////////
void Recipient_P2PK::serialize() const
{
   BinaryWriter bw;
   bw.put_uint64_t(getValue());

   auto&& rawScript = BtcUtils::getP2PKScript(pubkey_);

   bw.put_var_int(rawScript.getSize());
   bw.put_BinaryData(rawScript);

   script_ = std::move(bw.getData());
}

////////////////////////////////////////////////////////////////////////////////
size_t Recipient_P2PK::getSize() const
{
   return 10 + pubkey_.getSize();
}

////////////////////////////////////////////////////////////////////////////////
//
// Recipient_P2WPKH
//
////////////////////////////////////////////////////////////////////////////////
void Recipient_P2WPKH::serialize() const
{
   BinaryWriter bw;
   bw.put_uint64_t(getValue());

   auto&& rawScript = BtcUtils::getP2WPKHOutputScript(h160_);

   bw.put_var_int(rawScript.getSize());
   bw.put_BinaryData(rawScript);

   script_ = std::move(bw.getData());
}

////////////////////////////////////////////////////////////////////////////////
size_t Recipient_P2WPKH::getSize() const
{ 
   return 31; 
}

////////////////////////////////////////////////////////////////////////////////
//
// Recipient_P2SH
//
////////////////////////////////////////////////////////////////////////////////
void Recipient_P2SH::serialize() const
{
   BinaryWriter bw;
   bw.put_uint64_t(getValue());

   auto&& rawScript = BtcUtils::getP2SHScript(h160_);

   bw.put_var_int(rawScript.getSize());
   bw.put_BinaryData(rawScript);

   script_ = std::move(bw.getData());
}

////////////////////////////////////////////////////////////////////////////////
size_t Recipient_P2SH::getSize() const
{
   return 32;
}

////////////////////////////////////////////////////////////////////////////////
//
// Recipient_P2WSH
//
////////////////////////////////////////////////////////////////////////////////
void Recipient_P2WSH::serialize() const
{
   BinaryWriter bw;
   bw.put_uint64_t(getValue());

   auto&& rawScript = BtcUtils::getP2WSHOutputScript(h256_);

   bw.put_var_int(rawScript.getSize());
   bw.put_BinaryData(rawScript);

   script_ = std::move(bw.getData());
}

////////////////////////////////////////////////////////////////////////////////
size_t Recipient_P2WSH::getSize() const
{
   return 43;
}

////////////////////////////////////////////////////////////////////////////////
//
// Recipient_OPRETURN
//
////////////////////////////////////////////////////////////////////////////////
void Recipient_OPRETURN::serialize() const
{
   BinaryWriter bw;
   bw.put_uint64_t(0);

   BinaryWriter bw_msg;
   auto size = message_.getSize();
   if (size > 75)
   {
      bw_msg.put_uint8_t(OP_PUSHDATA1);
      bw_msg.put_uint8_t(size);
   }
   else if (size > 0)
   {
      bw_msg.put_uint8_t(size);
   }

   if (size > 0)
      bw_msg.put_BinaryData(message_);

   bw.put_uint8_t(bw_msg.getSize() + 1);
   bw.put_uint8_t(OP_RETURN);
   bw.put_BinaryData(bw_msg.getData());

   script_ = bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
size_t Recipient_OPRETURN::getSize() const
{
   auto size = message_.getSize();
   if (size > 75)
      size += 2;
   else if (size > 0)
      size += 1;

   size += 9; //8 for value, one for op_return
   return size;
}

////////////////////////////////////////////////////////////////////////////////
//
// Recipient_Universal
//
////////////////////////////////////////////////////////////////////////////////
void Recipient_Universal::serialize() const
{
   if (script_.getSize() != 0)
      return;

   BinaryWriter bw;
   bw.put_uint64_t(getValue());
   bw.put_var_int(binScript_.getSize());
   bw.put_BinaryData(binScript_);

   script_ = std::move(bw.getData());
}

////////////////////////////////////////////////////////////////////////////////
size_t Recipient_Universal::getSize() const
{
   size_t varint_len = 1;
   if (binScript_.getSize() >= 0xfd)
      varint_len = 3; //larger scripts would make the tx invalid

   return 8 + binScript_.getSize() + varint_len;
}