////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2022, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "LegacySigner.h"

using namespace std;
using namespace Armory::LegacySigner;

#define LEGACY_SIGNERTYPE_DEFAULT   'Default'
#define LEGACY_SIGNERTYPE_LEGACY    'Legacy'
#define LEGACY_SIGNERTYPE_CPP       '0.96 C++'
#define LEGACY_SIGNERTYPE_BCH       'Bcash'

#define SERIALIZED_SCRIPT_PREFIX    0x01
#define WITNESS_SCRIPT_PREFIX       0x02
#define LEGACY_STACK_PARTIAL        0x03
#define WITNESS_STACK_PARTIAL       0x04
#define PREFIX_UTXO                 0x05
#define PREFIX_OUTPOINT             0x06
#define USTX_EXT_SIGNERTYPE         0x20
#define USTX_EXT_SIGNERSTATE        0x30

////////////////////////////////////////////////////////////////////////////////
////
//// Signer
////
////////////////////////////////////////////////////////////////////////////////
Signer Signer::deserExtState(BinaryDataRef data)
{
   Signer signer;
   BinaryRefReader brr(data);

   while (brr.getSizeRemaining() != 0)
   {
      auto extType = brr.get_uint8_t();
      auto extSize = brr.get_var_int();
      auto extRef = brr.get_BinaryDataRef(extSize);

      switch (extType)
      {
      case USTX_EXT_SIGNERTYPE:
         //this isnt useful, it's only here to signify which signer
         //code to use, a distinction that is obsolete now
         break;

      case USTX_EXT_SIGNERSTATE:
         //deser legacy signer state, look for sigs
         signer.deser(extRef);
         break;

      default:
         continue;
      }
   }

   return signer;
}

////////////////////////////////////////////////////////////////////////////////
void Signer::deser(BinaryDataRef data)
{
   /*
   We're only here for signatures, we do not care for the tx structure as
   the python side of the serialized tx carries that data as well
   */
   BinaryRefReader brr(data);

   brr.advance(12); //version + locktime + flags
   isSegWit_ = brr.get_uint8_t();

   auto spenderCount = brr.get_var_int();
   for (unsigned i = 0; i < spenderCount; i++)
   {
      auto spenderLen = brr.get_var_int();
      auto spenderData = brr.get_BinaryDataRef(spenderLen);

      try
      {
         auto spenderPtr = ScriptSpender::deserExtState(spenderData);
         spenders_.push_back(spenderPtr);
      }
      catch (const exception &e)
      {
         LOGWARN << "failed to deser legacy spender";
         LOGWARN << "error: " << e.what();
      }
   }

   //ignore recipients
}

////////////////////////////////////////////////////////////////////////////////
map<unsigned, SecureBinaryData> Signer::getSigs() const
{
   map<unsigned, SecureBinaryData> result;
   for (unsigned i=0; i<spenders_.size(); i++)
   {
      const auto& spender = spenders_[i];
      auto sig = spender->getSig();
      if (sig.empty())
         continue;

      result.emplace(i, sig);
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
////
//// ScriptSpender
////
////////////////////////////////////////////////////////////////////////////////
shared_ptr<ScriptSpender> ScriptSpender::deserExtState(BinaryDataRef data)
{
   BinaryRefReader brr(data);

   //flags
   BitUnpacker<uint8_t> bup(brr.get_uint8_t());

   //sighash type, sequence
   brr.advance(5);

   //skip the utxo
   auto prefix = brr.get_uint8_t();
   switch (prefix)
   {
   case PREFIX_UTXO:
   {
      auto utxoLen = brr.get_var_int();
      brr.advance(utxoLen);
      break;
   }

   case PREFIX_OUTPOINT:
   {
      auto outpointLen = brr.get_var_int();
      brr.advance(outpointLen);
      brr.advance(8);
      break;
   }

   default:
      throw runtime_error("invalid prefix for utxo/outpoint deser");
   }

   //instantiate spender, set stack state
   shared_ptr<ScriptSpender> spender{ new ScriptSpender() };
   spender->legacyStatus_ = (SpenderStatus)bup.getBits(2);
   spender->segwitStatus_ = (SpenderStatus)bup.getBits(2);

   //cycle through stack items
   while (brr.getSizeRemaining() > 0)
   {
      auto prefix = brr.get_uint8_t();

      switch (prefix)
      {
      case SERIALIZED_SCRIPT_PREFIX:
      {
         auto len = brr.get_var_int();
         spender->serializedScript_ = move(brr.get_BinaryData(len));
         break;
      }

      case WITNESS_SCRIPT_PREFIX:
      {
         auto len = brr.get_var_int();
         spender->witnessData_ = move(brr.get_BinaryData(len));
         break;
      }

      case LEGACY_STACK_PARTIAL:
      {
         auto count = brr.get_var_int();
         for (unsigned i = 0; i < count; i++)
         {
            auto len = brr.get_var_int();
            auto stackItem = StackItem::deserialize(
               brr.get_BinaryDataRef(len));
            spender->partialStack_.emplace(
               stackItem->getId(), stackItem);
         }
         break;
      }

      case WITNESS_STACK_PARTIAL:
      {
         auto count = brr.get_var_int();
         for (unsigned i = 0; i < count; i++)
         {
            auto len = brr.get_var_int();
            auto stackItem = StackItem::deserialize(
               brr.get_BinaryDataRef(len));
            spender->partialWitnessStack_.emplace(
               stackItem->getId(), stackItem);
         }
         break;
      }

      default:
         throw LegacyScriptException("invalid spender state");
      }
   }

   return spender;
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData ScriptSpender::getSig() const
{
   if (!witnessData_.empty())
   {
      //segwit spender: witness is <count><sig><pubkey>
      BinaryRefReader brr(witnessData_.getRef());
      auto count = brr.get_var_int();
      if (count == 0)
         return {};
      auto sigSize = brr.get_var_int();
      return {brr.get_BinaryDataRef(sigSize)};
   }

   if (serializedScript_.empty())
      return {};

   //expect a straight forward single sig redeem script for now, the sig
   //should be the first item of the script
   BinaryRefReader brr(serializedScript_.getRef());
   auto sigSize = brr.get_uint8_t();
   return {brr.get_BinaryDataRef(sigSize)};
}

////////////////////////////////////////////////////////////////////////////////
////
//// StackItem
////
////////////////////////////////////////////////////////////////////////////////
StackItem::~StackItem()
{}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<StackItem> StackItem::deserialize(const BinaryDataRef& dataRef)
{
   shared_ptr<StackItem> itemPtr;

   BinaryRefReader brr(dataRef);

   auto id = brr.get_uint32_t();
   auto prefix = brr.get_uint8_t();

   switch (prefix)
   {
   case STACKITEM_PUSHDATA_PREFIX:
   {
      auto len = brr.get_var_int();
      auto&& data = brr.get_BinaryData(len);

      itemPtr = make_shared<StackItem_PushData>(id, move(data));
      break;
   }

   case STACKITEM_SIG_PREFIX:
   {
      auto len = brr.get_var_int();
      SecureBinaryData data(brr.get_BinaryData(len));

      itemPtr = make_shared<StackItem_Sig>(id, move(data));
      break;
   }

   case STACKITEM_MULTISIG_PREFIX:
   {
      auto m = brr.get_uint16_t();
      auto item_ms = make_shared<StackItem_MultiSig>(id, m);

      auto count = brr.get_var_int();
      for (unsigned i = 0; i < count; i++)
      {
         auto pos = brr.get_uint16_t();
         auto len = brr.get_var_int();
         SecureBinaryData data(brr.get_BinaryData(len));

         item_ms->setSig(pos, data);
      }

      itemPtr = item_ms;
      break;
   }

   case STACKITEM_OPCODE_PREFIX:
   {
      auto opcode = brr.get_uint8_t();

      itemPtr = make_shared<StackItem_OpCode>(id, opcode);
      break;
   }

   case STACKITEM_SERSCRIPT_PREFIX:
   {
      auto len = brr.get_var_int();
      auto&& data = brr.get_BinaryData(len);

      itemPtr = make_shared<StackItem_SerializedScript>(id, move(data));
      break;
   }

   default:
      throw LegacyScriptException("unexpected stack item prefix");
   }

   return itemPtr;
}

////////////////////////////////////////////////////////////////////////////////
bool StackItem_PushData::isSame(const StackItem* obj) const
{
   auto obj_cast = dynamic_cast<const StackItem_PushData*>(obj);
   if (obj_cast == nullptr)
      return false;
 
   return data_ == obj_cast->data_;
}

////////////////////////////////////////////////////////////////////////////////
bool StackItem_Sig::isSame(const StackItem* obj) const
{
   auto obj_cast = dynamic_cast<const StackItem_Sig*>(obj);
   if (obj_cast == nullptr)
      return false;

   return data_ == obj_cast->data_;
}

////////////////////////////////////////////////////////////////////////////////
bool StackItem_MultiSig::isSame(const StackItem* obj) const
{
   auto obj_cast = dynamic_cast<const StackItem_MultiSig*>(obj);
   if (obj_cast == nullptr)
      return false;

   return m_ == obj_cast->m_ &&
      sigs_ == obj_cast->sigs_;
}

////////////////////////////////////////////////////////////////////////////////
bool StackItem_OpCode::isSame(const StackItem* obj) const
{
   auto obj_cast = dynamic_cast<const StackItem_OpCode*>(obj);
   if (obj_cast == nullptr)
      return false;

   return opcode_ == obj_cast->opcode_;
}

////////////////////////////////////////////////////////////////////////////////
bool StackItem_SerializedScript::isSame(const StackItem* obj) const
{
   auto obj_cast = dynamic_cast<const StackItem_SerializedScript*>(obj);
   if (obj_cast == nullptr)
      return false;

   return data_ == obj_cast->data_;
}
