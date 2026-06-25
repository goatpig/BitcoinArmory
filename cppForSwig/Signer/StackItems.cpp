////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "StackItems.h"
#include <Utils/BtcUtils.h>

using namespace Armory::Signing;

////////////////////////////////////////////////////////////////////////////////
// ScriptException
ScriptException::ScriptException(const std::string& what) :
   std::runtime_error(what)
{}

////////////////////////////////////////////////////////////////////////////////
// StackItem
StackItem::StackItem(StackItemType type, unsigned id) :
   type_(type), id_(id)
{}

StackItem::~StackItem()
{}

////////
StackItemType StackItem::type() const
{
   return type_;
}

unsigned StackItem::getId() const
{
   return id_;
}

bool StackItem::isValid() const
{
   return true;
}

////////////////////////////////////////////////////////////////////////////////
// StackItem_PushData
StackItem_PushData::StackItem_PushData(unsigned id, BinaryData&& data) :
   StackItem(StackItemType::PushData, id), data(std::move(data))
{}

bool StackItem_PushData::isValid() const
{
   return !data.empty();
}

bool StackItem_PushData::isSame(const StackItem* obj) const
{
   auto obj_cast = dynamic_cast<const StackItem_PushData*>(obj);
   if (obj_cast == nullptr) {
      return false;
   }
   return data == obj_cast->data;
}

////////////////////////////////////////////////////////////////////////////////
// StackItem_Sig
StackItem_Sig::StackItem_Sig(
   unsigned id, BinaryData& pkey, BinaryData& scr) :
   StackItem(StackItemType::Sig, id),
   pubkey(std::move(pkey)),
   script(std::move(scr))
{}

bool StackItem_Sig::isValid() const
{
   return !sig.empty();
}

bool StackItem_Sig::isSame(const StackItem* obj) const
{
   auto obj_cast = dynamic_cast<const StackItem_Sig*>(obj);
   if (obj_cast == nullptr) {
      return false;
   }
   return pubkey == obj_cast->pubkey && script == obj_cast->script;
}

void StackItem_Sig::merge(const StackItem *obj)
{
   auto obj_cast = dynamic_cast<const StackItem_Sig*>(obj);
   if (obj_cast == nullptr) {
      throw ScriptException("unexpected StackItem type");
   }

   if (script.empty()) {
      script = obj_cast->script;
   } else if (script != obj_cast->script) {
      throw ScriptException("sig item script mismatch");
   }

   if (pubkey.empty()) {
      pubkey = obj_cast->pubkey;
   } else if (pubkey != obj_cast->pubkey) {
      throw ScriptException("sig item pubkey mismatch");
   }
}

void StackItem_Sig::injectSig(SecureBinaryData& signature)
{
   sig = std::move(signature);
}

////////////////////////////////////////////////////////////////////////////////
// StackItem_MultiSig
StackItem_MultiSig::StackItem_MultiSig(unsigned id, BinaryData& scr) :
   StackItem(StackItemType::MultiSig, id), script(std::move(scr))
{
   m = BtcUtils::getMultisigPubKeyList(script, pubkeyVec);
   if (m < 1 || m >= 16) {
      throw std::runtime_error("invalid m");
   }
   if (pubkeyVec.size() < m) {
      throw std::runtime_error("invalid pubkey count");
   }
}

bool StackItem_MultiSig::isSame(const StackItem* obj) const
{
   auto obj_cast = dynamic_cast<const StackItem_MultiSig*>(obj);
   if (obj_cast == nullptr) {
      return false;
   }
   return m == obj_cast->m && sigs == obj_cast->sigs;
}

void StackItem_MultiSig::merge(const StackItem* obj)
{
   auto obj_cast = dynamic_cast<const StackItem_MultiSig*>(obj);
   if (obj_cast == nullptr) {
      throw ScriptException("unexpected StackItem type");
   }
   if (m != obj_cast->m) {
      throw ScriptException("m mismatch");
   }
   sigs.insert(obj_cast->sigs.begin(), obj_cast->sigs.end());
}

void StackItem_MultiSig::setSig(unsigned id, SecureBinaryData& sig)
{
   sigs.emplace(id, std::move(sig));
}

bool StackItem_MultiSig::isValid() const
{
   return sigs.size() == m;
}

////////////////////////////////////////////////////////////////////////////////
// StackItem_OpCode
StackItem_OpCode::StackItem_OpCode(unsigned ID, uint8_t oc) :
   StackItem(StackItemType::OpCode, ID),
   opcode(oc)
{}

bool StackItem_OpCode::isSame(const StackItem* obj) const
{
   auto obj_cast = dynamic_cast<const StackItem_OpCode*>(obj);
   if (obj_cast == nullptr) {
      return false;
   }
   return opcode == obj_cast->opcode;
}

////////////////////////////////////////////////////////////////////////////////
// StackItem_SerializedScript
StackItem_SerializedScript::StackItem_SerializedScript(
   unsigned id, BinaryData&& d) :
   StackItem(StackItemType::SerializedScript, id),
   data(std::move(d))
{}

bool StackItem_SerializedScript::isSame(const StackItem* obj) const
{
   auto obj_cast = dynamic_cast<const StackItem_SerializedScript*>(obj);
   if (obj_cast == nullptr) {
      return false;
   }
   return data == obj_cast->data;
}

////////////////////////////////////////////////////////////////////////////////
// OpCode
OpCode::~OpCode()
{}

ExtendedOpCode::ExtendedOpCode(const OpCode& oc) :
   OpCode(oc)
{}

////////////////////////////////////////////////////////////////////////////////
// StackValue
StackValue::StackValue(StackValueType type) :
   type_(type)
{}

StackValue::~StackValue()
{}

StackValueType StackValue::type() const
{
   return type_;
}

////////
StackValue_Static::StackValue_Static(BinaryData val) :
   StackValue(StackValueType::Static), value_(std::move(val))
{}

StackValue_Reference::StackValue_Reference(
   std::shared_ptr<ReversedStackEntry> rsePtr) :
   StackValue(StackValueType::Reference), valueReference_(rsePtr)
{}

StackValue_FromFeed::StackValue_FromFeed(const BinaryData& bd) :
   StackValue(StackValueType::FromFeed), requestString_(bd)
{}

StackValue_Sig::StackValue_Sig(std::shared_ptr<ReversedStackEntry> ref) :
   StackValue(StackValueType::Sig), pubkeyRef_(ref)
{}

StackValue_Multisig::StackValue_Multisig(const BinaryData& script) :
   StackValue(StackValueType::Multisig), script_(script)
{}

////////////////////////////////////////////////////////////////////////////////
// ReversedStackEntry
ReversedStackEntry::ReversedStackEntry()
{}

ReversedStackEntry::ReversedStackEntry(const BinaryData& data) :
   static_(true), staticData_(data)
{}

bool ReversedStackEntry::push_opcode(std::shared_ptr<OpCode> ocptr)
{
   if (static_ && parent_ == nullptr) {
      return false;
   }
   if (parent_ != nullptr) {
      parent_->push_opcode(ocptr);
      return false;
   }

   opcodes_.emplace_back(ocptr);
   return true;
}
