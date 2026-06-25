////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "TxOutScrRef.h"
#include "BitcoinSettings.h"

TxOutScriptRef::TxOutScriptRef(
   Armory::ScriptPrefix type, const BinaryDataRef& script) :
   type_(type), scriptRef_(script)
{}

TxOutScriptRef::TxOutScriptRef(
   Armory::ScriptPrefix type, BinaryData& script) :
   type_(type), scriptCopy_(std::move(script))
{
   scriptRef_.setRef(scriptCopy_);
}

TxOutScriptRef::TxOutScriptRef(const TxOutScriptRef& outscr) :
   type_(outscr.type_)
{
   if (!outscr.scriptCopy_.empty()) {
      scriptCopy_ = outscr.scriptCopy_;
      scriptRef_.setRef(scriptCopy_);
   } else {
      scriptRef_ = outscr.scriptRef_;
   }
}

TxOutScriptRef::TxOutScriptRef(TxOutScriptRef&& outscr) :
   type_(outscr.type_)
{
   if (!outscr.scriptCopy_.empty()) {
      scriptCopy_ = std::move(outscr.scriptCopy_);
      scriptRef_.setRef(scriptCopy_);
   } else {
      scriptRef_ = outscr.scriptRef_;
   }
   outscr.scriptRef_.reset();
}

TxOutScriptRef& TxOutScriptRef::operator=(const TxOutScriptRef& rhs)
{
   if (this != &rhs) {
      *this = TxOutScriptRef{rhs};
   }
   return *this;
}

bool TxOutScriptRef::operator==(const TxOutScriptRef& rhs) const
{
   if (this->type_ != rhs.type_) {
      return false;
   }
   return this->scriptRef_ == rhs.scriptRef_;
}

bool TxOutScriptRef::operator<(const TxOutScriptRef& rhs) const
{
   if (this->type_ == rhs.type_) {
      return this->scriptRef_ < rhs.scriptRef_;
   } else {
      return this->type_ < rhs.type_;
   }
}

BinaryData TxOutScriptRef::getScrAddr() const
{
   BinaryWriter bw(1 + scriptRef_.getSize());
   bw.put_uint8_t((uint8_t)type_);
   bw.put_BinaryDataRef(scriptRef_);
   return bw.getData();
}

BinaryDataRef TxOutScriptRef::getScrRef() const
{
   return scriptRef_;
}

std::size_t std::hash<TxOutScriptRef>::operator()(const TxOutScriptRef& key) const
{
   return BinaryData::Hasher()(key.getScrRef());
}

TxOutScriptRef TxOutScriptRef::fromRef(
   Armory::ScriptPrefix prefix, const BinaryDataRef& dataRef)
{
   return TxOutScriptRef{prefix, dataRef};
}

TxOutScriptRef TxOutScriptRef::fromScrAddr(BinaryDataRef scrAddr)
{
   return TxOutScriptRef{
      (Armory::ScriptPrefix)scrAddr[0],
      scrAddr.getSliceRef(1, scrAddr.getSize() - 1)
   };
}
