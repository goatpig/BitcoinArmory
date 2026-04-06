////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include <set>
#include <functional>

#include "ScriptSpender.h"
#include <Utils/BtcUtils.h>

#include "PSBT.h"
#include "Script.h"

using namespace Armory::Signing;

////////////////////////////////////////////////////////////////////////////////
// exceptions
SpenderException::SpenderException(const std::string& e) :
   std::runtime_error(e)
{}

////////////////////////////////////////////////////////////////////////////////
// ScriptSpender
ScriptSpender::ScriptSpender()
{}

ScriptSpender::ScriptSpender(const BinaryDataRef txHash, unsigned index)
{
   BinaryWriter bw;
   bw.put_BinaryDataRef(txHash);
   bw.put_uint32_t(index);
   outpoint_ = bw.getData();
}

ScriptSpender::ScriptSpender(const UTXO& utxo) :
   utxo_(utxo)
{}

ScriptSpender::ScriptSpender(const ScriptSpender& ss)
{
   outpoint_ = ss.getOutpoint();
   sequence_ = ss.sequence_;
   merge(ss);
}

bool ScriptSpender::operator==(const ScriptSpender& rhs) const
{
   try {
      return this->getOutpoint() == rhs.getOutpoint();
   } catch (const std::exception&) {
      return false;
   }
}

////////
const UTXO& ScriptSpender::getUtxo() const
{
   if (!utxo_.isInitialized()) {
      if (!haveSupportingTx()) {
         throw SpenderException("missing both utxo & supporting tx");
      }
      utxo_.txHash_ = getOutputHash();
      utxo_.txOutIndex_ = getOutputIndex();

      const auto& supportingTx = getSupportingTx();
      auto opId = getOutputIndex();
      auto txOutCopy = supportingTx.getTxOutCopy(opId);
      utxo_.unserializeRaw(txOutCopy.serializeRef());
   }
   return utxo_;
}

BinaryDataRef ScriptSpender::getOutputScript() const
{
   const auto& utxo = getUtxo();
   return utxo.getScript();
}

BinaryDataRef ScriptSpender::getOutputHash() const
{
   if (utxo_.isInitialized()) {
      return utxo_.getTxHash();
   }

   if (outpoint_.getSize() != 36) {
      throw SpenderException("missing utxo");
   }

   BinaryRefReader brr(outpoint_);
   return brr.get_BinaryDataRef(32);
}

unsigned ScriptSpender::getOutputIndex() const
{
   if (utxo_.isInitialized()) {
      return utxo_.getTxOutIndex();
   }

   if (outpoint_.getSize() != 36) {
      throw SpenderException("missing utxo");
   }

   BinaryRefReader brr(outpoint_);
   brr.advance(32);
   return brr.get_uint32_t();
}

BinaryDataRef ScriptSpender::getOutpoint() const
{
   if (outpoint_.empty()) {
      BinaryWriter bw;
      bw.put_BinaryDataRef(getOutputHash());
      bw.put_uint32_t(getOutputIndex());
      outpoint_ = bw.getData();
   }
   return outpoint_.getRef();
}

SIGHASH_TYPE ScriptSpender::getSigHashType() const
{
   return sigHashType_;
}

unsigned ScriptSpender::getSequence() const
{
   return sequence_;
}

unsigned ScriptSpender::getFlags() const
{
   unsigned flags = SCRIPT_VERIFY_SEGWIT;
   if (isP2SH_) {
      flags |= SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_P2SH_SHA256;
   }
   if (isCSV_) {
      flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
   }
   if (isCLTV_) {
      flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
   }
   return flags;
}

uint8_t ScriptSpender::getSigHashByte() const
{
   uint8_t hashbyte;
   switch (sigHashType_)
   {
      case SIGHASH_ALL:
         hashbyte = 1;
         break;

      default:
         throw ScriptException("unsupported sighash type");
   }
   return hashbyte;
}

SpenderStatus ScriptSpender::getLegacyStatus() const
{
   return legacyStatus_;
}

const ScriptSpender::StackItemMap& ScriptSpender::getLegacyStack() const
{
   return legacyStack_;
}

SpenderStatus ScriptSpender::getWitnessStatus() const
{
   return segwitStatus_;
}

const ScriptSpender::StackItemMap& ScriptSpender::getWitnessStack() const
{
   return witnessStack_;
}

const BinaryData& ScriptSpender::getFinalInputScript() const
{
   return finalInputScript_;
}

std::map<BinaryData, BIP32_AssetPath>& ScriptSpender::getBip32Paths()
{
   return bip32Paths_;
}

////////
BinaryData ScriptSpender::serializeScript(
   const std::vector<std::shared_ptr<StackItem>>& stack, bool no_throw)
{
   BinaryWriter bwStack;
   for (const auto& stackItem : stack) {
      switch (stackItem->type())
      {
         case StackItemType::PushData:
         {
            auto stackItem_pushdata =
               std::dynamic_pointer_cast<StackItem_PushData>(stackItem);
            if (stackItem_pushdata == nullptr) {
               if (!no_throw) {
                  throw ScriptException("unexpected StackItem type");
               }
               bwStack.put_uint8_t(0);
               break;
            }

            bwStack.put_BinaryData(BtcUtils::getPushDataHeader(
               stackItem_pushdata->data));
            bwStack.put_BinaryData(stackItem_pushdata->data);
            break;
         }

         case StackItemType::SerializedScript:
         {
            auto stackItem_ss =
               std::dynamic_pointer_cast<StackItem_SerializedScript>(stackItem);
            if (stackItem_ss == nullptr) {
               if (!no_throw) {
                  throw ScriptException("unexpected StackItem type");
               }
               break;
            }

            bwStack.put_BinaryData(stackItem_ss->data);
            break;
         }

         case StackItemType::Sig:
         {
            auto stackItem_sig =
               std::dynamic_pointer_cast<StackItem_Sig>(stackItem);
            if (stackItem_sig == nullptr) {
               if (!no_throw) {
                  throw ScriptException("unexpected StackItem type");
               }
               bwStack.put_uint8_t(0);
               break;
            }

            bwStack.put_BinaryData(BtcUtils::getPushDataHeader(
               stackItem_sig->sig));
            bwStack.put_BinaryData(stackItem_sig->sig);
            break;
         }

         case StackItemType::MultiSig:
         {
            auto stackItem_sig =
               std::dynamic_pointer_cast<StackItem_MultiSig>(stackItem);
            if (stackItem_sig == nullptr) {
               if (!no_throw) {
                  throw ScriptException("unexpected StackItem type");
               }
               bwStack.put_uint8_t(0);
               break;
            }

            if (stackItem_sig->sigs.size() < stackItem_sig->m) {
               if (!no_throw) {
                  throw ScriptException("missing sigs for ms script");
               }
            }

            for (const auto& sigpair : stackItem_sig->sigs) {
               bwStack.put_BinaryData(BtcUtils::getPushDataHeader(
                  sigpair.second));
               bwStack.put_BinaryData(sigpair.second);
            }
            break;
         }

         case StackItemType::OpCode:
         {
            auto stackItem_opcode =
               std::dynamic_pointer_cast<StackItem_OpCode>(stackItem);
            if (stackItem_opcode == nullptr) {
               if (no_throw) {
                  throw ScriptException("unexpected StackItem type");
               }
               bwStack.put_uint8_t(0);
               break;
            }

            bwStack.put_uint8_t(stackItem_opcode->opcode);
            break;
         }

         default:
            if (!no_throw) {
               throw ScriptException("unexpected StackItem type");
            }
      }
   }
   return bwStack.getData();
}

BinaryData ScriptSpender::serializeWitnessData(
   const std::vector<std::shared_ptr<StackItem>>& stack,
   unsigned &itemCount, bool no_throw)
{
   itemCount = 0;
   BinaryWriter bwStack;
   for (const auto& stackItem : stack) {
      switch (stackItem->type())
      {
         case StackItemType::PushData:
         {
            ++itemCount;

            auto stackItem_pushdata =
               std::dynamic_pointer_cast<StackItem_PushData>(stackItem);
            if (stackItem_pushdata == nullptr) {
               if (!no_throw) {
                  throw ScriptException("unexpected StackItem type");
               }
               bwStack.put_uint8_t(0);
               break;
            }

            bwStack.put_var_int(stackItem_pushdata->data.getSize());
            bwStack.put_BinaryData(stackItem_pushdata->data);
            break;
         }

         case StackItemType::SerializedScript:
         {

            auto stackItem_ss =
               std::dynamic_pointer_cast<StackItem_SerializedScript>(stackItem);
            if (stackItem_ss == nullptr) {
               if (!no_throw) {
                  throw ScriptException("unexpected StackItem type");
               }
               break;
            }

            bwStack.put_BinaryData(stackItem_ss->data);
            ++itemCount;
            break;
         }

         case StackItemType::Sig:
         {
            ++itemCount;
            auto stackItem_sig =
               std::dynamic_pointer_cast<StackItem_Sig>(stackItem);
            if (stackItem_sig == nullptr) {
               if (!no_throw) {
                  throw ScriptException("unexpected StackItem type");
               }
               bwStack.put_uint8_t(0);
               break;
            }

            bwStack.put_var_int(stackItem_sig->sig.getSize());
            bwStack.put_BinaryData(stackItem_sig->sig);
            break;
         }

         case StackItemType::MultiSig:
         {
            auto stackItem_sig =
               std::dynamic_pointer_cast<StackItem_MultiSig>(stackItem);
            if (stackItem_sig == nullptr) {
               if (!no_throw) {
                  throw ScriptException("unexpected StackItem type");
               }
               bwStack.put_uint8_t(0);
               break;
            }

            if (stackItem_sig->sigs.size() < stackItem_sig->m && !no_throw) {
               throw ScriptException("missing sigs for ms script");
            }

            for (auto& sigpair : stackItem_sig->sigs) {
               bwStack.put_BinaryData(
                  BtcUtils::getPushDataHeader(sigpair.second));
               bwStack.put_BinaryData(sigpair.second);
               ++itemCount;
            }
            break;
         }

         case StackItemType::OpCode:
         {
            ++itemCount;
            auto stackItem_opcode =
               std::dynamic_pointer_cast<StackItem_OpCode>(stackItem);
            if (stackItem_opcode == nullptr) {
               if (!no_throw) {
                  throw ScriptException("unexpected StackItem type");
               }
               bwStack.put_uint8_t(0);
               break;
            }
            bwStack.put_uint8_t(stackItem_opcode->opcode);
            break;
         }

         default:
            if (!no_throw) {
               throw ScriptException("unexpected StackItem type");
            }
      }
   }
   return bwStack.getData();
}

bool ScriptSpender::isResolved() const
{
   if (!canBeResolved()) {
      return false;
   }
   if (!isSegWit()) {
      if (legacyStatus_ >= SpenderStatus::Resolved) {
         return true;
      }
   } else {
      //If this spender is SW, only emtpy (native sw) and resolved (nested sw) 
      //states are valid. The SW stack should not be empty for a SW input
      if ((legacyStatus_ == SpenderStatus::Empty ||
         legacyStatus_ == SpenderStatus::Resolved) &&
         segwitStatus_ >= SpenderStatus::Resolved) {
         return true;
      }
   }
   return false;
}

bool ScriptSpender::isSigned() const
{
   /*
   Valid combos are:
      legacy: Signed, SW: empty
      legacy: empty, SW: signed
      legacy: resolved, SW: signed
   */
   if (!canBeResolved()) {
      return false;
   }

   if (!isSegWit()) {
      if (legacyStatus_ == SpenderStatus::Signed &&
         segwitStatus_ == SpenderStatus::Empty) {
         return true;
      }
   } else {
      if (segwitStatus_ == SpenderStatus::Signed) {
         if (legacyStatus_ == SpenderStatus::Empty ||
            legacyStatus_ == SpenderStatus::Resolved) {
            return true;
         }
      }
   }
   return false;
}

BinaryData ScriptSpender::getSerializedOutpoint() const
{
   if (utxo_.isInitialized()) {
      BinaryWriter bw;
      bw.put_BinaryData(utxo_.getTxHash());
      bw.put_uint32_t(utxo_.getTxOutIndex());
      return bw.getData();
   }

   if (outpoint_.getSize() != 36) {
      throw SpenderException("missing outpoint");
   }
   return outpoint_;
}

BinaryData ScriptSpender::getAvailableInputScript() const
{
   //if we have a serialized script already, return that
   if (!finalInputScript_.empty()) {
      return finalInputScript_;
   }

   //otherwise, serialize it from the stack
   std::vector<std::shared_ptr<StackItem>> stack;
   for (const auto& stack_item : legacyStack_) {
      stack.emplace_back(stack_item.second);
   }
   return serializeScript(stack, true);
}

BinaryData ScriptSpender::getSerializedInput(
   bool withSig, bool loose) const
{
   if (legacyStatus_ == SpenderStatus::Unknown && !loose) {
      throw SpenderException("unresolved spender");
   }

   if (withSig) {
      if (!isSegWit()) {
         if (legacyStatus_ != SpenderStatus::Signed) {
            throw SpenderException("spender is missing sigs");
         }
      } else {
         if (legacyStatus_ != SpenderStatus::Empty &&
            legacyStatus_ != SpenderStatus::Resolved) {
            throw SpenderException("invalid legacy state for sw spender");
         }
      }
   }

   BinaryWriter bw;
   auto serializedScript = getAvailableInputScript();
   bw.put_BinaryData(getSerializedOutpoint());

   bw.put_var_int(serializedScript.getSize());
   bw.put_BinaryData(serializedScript);
   bw.put_uint32_t(sequence_);
   return bw.getData();
}

BinaryData ScriptSpender::getEmptySerializedInput() const
{
   BinaryWriter bw;
   bw.put_BinaryData(getSerializedOutpoint());
   bw.put_uint8_t(0);
   bw.put_uint32_t(sequence_);
   return bw.getData();
}

const BinaryData& ScriptSpender::getFinalizedWitnessData() const
{
   if (isSegWit()) {
      if (segwitStatus_ != SpenderStatus::Signed) {
         throw std::runtime_error("witness data missing signature");
      }
   } else if (segwitStatus_ != SpenderStatus::Empty) {
      throw std::runtime_error("unresolved witness");
   }
   return finalWitnessData_;
}

BinaryData ScriptSpender::serializeAvailableWitnessData() const
{
   try {
      return getFinalizedWitnessData();
   } catch (const std::exception&) {}

   std::vector<std::shared_ptr<StackItem>> stack;
   for (auto& stack_item : witnessStack_) {
      stack.push_back(stack_item.second);
   }

   //serialize and get item count
   unsigned itemCount = 0;
   auto data = serializeWitnessData(stack, itemCount, true);

   //put stack item count
   BinaryWriter bw;
   bw.put_var_int(itemCount);

   //put serialized stack
   bw.put_BinaryData(data);
   return bw.getData();
}

////////
void ScriptSpender::setUtxo(const UTXO& utxo)
{
   utxo_ = utxo;
}

void ScriptSpender::setStates(
   SpenderStatus legacyStatus, SpenderStatus witnessStatus,
   bool p2sh, bool csv, bool cltv)
{
   legacyStatus_ = legacyStatus;
   segwitStatus_ = witnessStatus;

   isP2SH_ = p2sh;
   isCSV_  = csv;
   isCLTV_ = cltv;
}

void ScriptSpender::setSigHashType(SIGHASH_TYPE sht)
{
   sigHashType_ = sht;
}

void ScriptSpender::setSequence(unsigned s)
{
   sequence_ = s;
}

void ScriptSpender::flagP2SH(bool flag)
{
   isP2SH_ = flag;
}

void ScriptSpender::setFinalScript(BinaryDataRef script)
{
   finalInputScript_ = script;
}

void ScriptSpender::setWitnessScript(BinaryDataRef script)
{
   finalWitnessData_ = script;
}

void ScriptSpender::setLegacyData(
   const std::vector<std::shared_ptr<StackItem>>& stack)
{
   unsigned i=0;
   for (const auto& si : stack) {
      legacyStack_.emplace(i++, si);
   }
}

void ScriptSpender::setWitnessData(
   const std::vector<std::shared_ptr<StackItem>>& stack)
{
   unsigned i=0;
   for (const auto& si : stack) {
      witnessStack_.emplace(i++, si);
   }
}

void ScriptSpender::setBip32Paths(std::map<BinaryData, BIP32_AssetPath>& paths)
{
   bip32Paths_ = std::move(paths);
}

////////
void ScriptSpender::updateStack(
   std::map<unsigned, std::shared_ptr<StackItem>>& stackMap,
   const std::vector<std::shared_ptr<StackItem>>& stackVec)
{
   for (auto& stack_item : stackVec) {
      auto iter_pair = stackMap.emplace(stack_item->getId(), stack_item);
      if (iter_pair.second == true) {
         continue;
      }

      //already have a stack item for this id, let's compare them
      if (iter_pair.first->second->isSame(stack_item.get())) {
         continue;
      }

      //if we get this, stack items differ, are they multisig items?
      switch (iter_pair.first->second->type())
      {
         case StackItemType::PushData:
         {
            if (!iter_pair.first->second->isValid()) {
               iter_pair.first->second = stack_item;
            } else if (stack_item->isValid()) {
               throw ScriptException("invalid push_data");
            }
            break;
         }

         case StackItemType::MultiSig:
         {
            auto stack_item_ms = std::dynamic_pointer_cast<StackItem_MultiSig>(
               iter_pair.first->second);
            stack_item_ms->merge(stack_item.get());
            break;
         }

         case StackItemType::Sig:
         {
            auto stack_item_sig = std::dynamic_pointer_cast<StackItem_Sig>(
               iter_pair.first->second);
            stack_item_sig->merge(stack_item.get());
            break;
         }

         default:
            throw ScriptException("unexpected StackItem type inequality");
      }
   }
}

void ScriptSpender::processStacks()
{
   /*
   Process the respective stacks, set the serialized input scripts if the 
   stacks carry enough data and clear the stacks. Otherwise, leave the 
   input/witness script empty and preserve the stack as is.
   */

   auto parseStack = [](
      const std::map<unsigned, std::shared_ptr<StackItem>>& stack)
      ->SpenderStatus
   {
      SpenderStatus stackState = SpenderStatus::Resolved;
      for (auto& item_pair : stack) {
         auto& stack_item = item_pair.second;
         switch (stack_item->type())
         {
            case StackItemType::MultiSig:
            {
               if (stack_item->isValid()) {
                  stackState = SpenderStatus::Signed;
                  break;
               }

               auto stack_item_ms = std::dynamic_pointer_cast<StackItem_MultiSig>(
                  stack_item);

               if (stack_item_ms == nullptr) {
                  throw std::runtime_error("unexpected stack item type");
               }
               if (!stack_item_ms->sigs.empty()) {
                  stackState = SpenderStatus::PartiallySigned;
               }
               break;
            }

            case StackItemType::Sig:
            {
               if (stack_item->isValid()) {
                  stackState = SpenderStatus::Signed;
               }
               break;
            }

            default:
               if (!stack_item->isValid()) {
                  return SpenderStatus::Unknown;
               }
         }
      }
      return stackState;
   };

   auto updateState = [&parseStack](
      std::map<unsigned, std::shared_ptr<StackItem>>& stack,
      SpenderStatus& spenderState,
      const std::function<void(const std::vector<std::shared_ptr<StackItem>>&)>& setScript)
      ->void
   {
      auto stackState = parseStack(stack);
      if (stackState >= spenderState) {
         switch (stackState)
         {
            case SpenderStatus::Resolved:
            case SpenderStatus::PartiallySigned:
            {
               //do not set the script, keep the stack
               break;
            }

            case SpenderStatus::Signed:
            {
               //set the script, clear the stack
               std::vector<std::shared_ptr<StackItem>> stack_vec;
               for (auto& item_pair : stack) {
                  stack_vec.emplace_back(item_pair.second);
               }
               setScript(stack_vec);
               stack.clear();
               break;
            }

            default:
               //do not set the script, keep the stack
               break;
         }
         spenderState = stackState;
      }
   };

   if (!legacyStack_.empty()) {
      updateState(legacyStack_, legacyStatus_, [this](
         const std::vector<std::shared_ptr<StackItem>>& stackVec)
         {
            finalInputScript_ = std::move(serializeScript(stackVec));
         }
      );
   }

   if (!witnessStack_.empty()) {
      updateState(witnessStack_, segwitStatus_, [this]
         (const std::vector<std::shared_ptr<StackItem>>& stackVec)
         {
            unsigned itemCount = 0;
            auto data = serializeWitnessData(stackVec, itemCount);

            BinaryWriter bw;
            bw.put_var_int(itemCount);
            bw.put_BinaryData(data);
            finalWitnessData_ = bw.getData();
         }
      );
   }
}

////////
void ScriptSpender::merge(const ScriptSpender& obj)
{
   /*
   Do not tolerate sequence mismatch. Sequence should be updated explicitly
   if the transaction scheme calls for it.
   */
   if (sequence_ != obj.sequence_) {
      throw std::runtime_error("sequence mismatch");
   }

   //nothing to merge if the spender is already signed
   if (isSigned()) {
      return;
   }

   //do we have supporting data?
   {
      //sanity check on obj
      BinaryDataRef objOpHash;
      uint64_t objOpVal;
      try {
         objOpHash = obj.getOutputHash();
         objOpVal = obj.getValue();
      } catch (const std::exception&) {
         //obj has no supporting data, it doesn't carry anything to merge
         return;
      }

      try {
         if (getOutputHash() != objOpHash) {
            throw std::runtime_error("spender output hash mismatch");
         }
         if (getOutputIndex() != obj.getOutputIndex()) {
            throw std::runtime_error("spender output index mismatch");
         }
         if (getValue() != objOpVal) {
            throw std::runtime_error("spender output value mismatch");
         }
      } catch (const SpenderException&) {
         //missing supporting data, get it from obj
         if (obj.utxo_.isInitialized()) {
            utxo_ = obj.utxo_;
         } else if (!obj.outpoint_.empty()) {
            outpoint_ = obj.outpoint_;
         } else {
            throw std::runtime_error("impossible condition, how did we get here??");
         }
      }
   }

   isP2SH_ |= obj.isP2SH_;
   isCLTV_ |= obj.isCLTV_;
   isCSV_  |= obj.isCSV_;

   //legacy stack
   if (legacyStatus_ != SpenderStatus::Signed) {
      switch (obj.legacyStatus_)
      {
      case SpenderStatus::Resolved:
      case SpenderStatus::PartiallySigned:
      {
         //merge the stacks
         std::vector<std::shared_ptr<StackItem>> objStackVec;
         for (auto& stackItemPtr : obj.legacyStack_) {
            objStackVec.emplace_back(stackItemPtr.second);
         }
         updateStack(legacyStack_, objStackVec);
         processStacks();

         /*
         processStacks will set the relevant legacy status,
         therefor we break out of the switch scope so as to not overwrite
         the status unnecessarely
         */
         break;
      }

      case SpenderStatus::Signed:
      {
         finalInputScript_ = obj.finalInputScript_;
         [[fallthrough]];
      }
      
      default:
         //set the legacy status
         if (obj.legacyStatus_ > legacyStatus_) {
            legacyStatus_ = obj.legacyStatus_;
         }
      }
   }

   //segwit stack
   if (segwitStatus_ != SpenderStatus::Signed) {
      switch (obj.segwitStatus_)
      {
         case SpenderStatus::Resolved:
         case SpenderStatus::PartiallySigned:
         {
            //merge the stacks
            std::vector<std::shared_ptr<StackItem>> objStackVec;
            for (auto& stackItemPtr : obj.witnessStack_) {
               objStackVec.emplace_back(stackItemPtr.second);
            }
            updateStack(witnessStack_, objStackVec);
            processStacks();
            break;
         }

         case SpenderStatus::Signed:
         {
            finalWitnessData_ = obj.finalWitnessData_;
            [[fallthrough]];
         }

         default:
            if (obj.segwitStatus_ > segwitStatus_) {
               segwitStatus_ = obj.segwitStatus_;
            }
      }
   }

   //bip32 paths
   bip32Paths_.insert(obj.bip32Paths_.begin(), obj.bip32Paths_.end());
}

bool ScriptSpender::compareEvalState(const ScriptSpender& rhs) const
{
   /*
   This is meant to compare the publicly resolved data between 2 spenders for 
   the same utxo. It cannot compare sigs in a stateful fashion because it
   cannot generate the sighash data without the rest of the transaction.

   Use signer::verify to check sigs
   */

   //lambdas
   auto getResolvedItems = [](const BinaryData& script,
      bool isWitnessData)->
      std::vector<BinaryDataRef>
   {
      std::vector<BinaryDataRef> resolvedScriptItems;
      BinaryRefReader brr(script);
      try {
         if (isWitnessData) {
            brr.get_var_int(); //drop witness item count
         }

         while (brr.getSizeRemaining() > 0) {
            auto len = brr.get_var_int();
            if (len == 0) {
               resolvedScriptItems.push_back(BinaryDataRef());
               continue;
            }

            auto dataRef = brr.get_BinaryDataRef(len);
            if (dataRef.getSize() > 68 &&
               dataRef.getPtr()[0] == 0x30 &&
               dataRef.getPtr()[2] == 0x02) {
               //this is a sig, set an empty place holder instead
               resolvedScriptItems.push_back(BinaryDataRef());
               continue;
            }
            resolvedScriptItems.push_back(dataRef);
         }
      } catch (const std::exception&) {}

      return resolvedScriptItems;
   };

   auto isStackMultiSig = [](
      const std::map<unsigned, std::shared_ptr<StackItem>>& stack)->bool
   {
      for (auto& stack_item : stack) {
         if (stack_item.second->type() == StackItemType::MultiSig) {
            return true;
         }
      }
      return false;
   };

   auto compareScriptItems = [](
      const std::vector<BinaryDataRef>& ours,
      const std::vector<BinaryDataRef>& theirs,
      bool isMultiSig)->bool
   {
      if (ours == theirs) {
         return true;
      }
      if (theirs.empty()) {
         /*
         If ours isn't empty, theirs cannot be empty (it needs the 
         resolved data at least). Edge case: ours carry only empty
         data vectors.
         */
         bool empty = true;
         for (const auto& ourItem : ours) {
            if (!ourItem.empty()) {
               empty = false;
               break;
            }
         }
         return empty;
      }

      if (isMultiSig) {
         //multisig script, tally 0s and compare
         std::vector<BinaryDataRef> oursStripped;
         unsigned ourZeroCount = 0;
         for (auto& ourItem : ours) {
            if (ourItem.empty()) {
               ++ourZeroCount;
            } else {
               oursStripped.emplace_back(ourItem);
            }
         }

         std::vector<BinaryDataRef> theirsStripped;
         unsigned theirZeroCount = 0;
         for (auto& theirItem : theirs) {
            if (theirItem.empty()) {
               ++theirZeroCount;
            } else {
               theirsStripped.emplace_back(theirItem);
            }
         }

         if (oursStripped == theirsStripped) {
            if (ourZeroCount > 1 && theirZeroCount >= 1) {
               return true;
            }
         }
      }
      return false;
   };

   //check utxos
   if (getOutputHash() != rhs.getOutputHash() ||
      getOutputIndex() != rhs.getOutputIndex() ||
      getValue() != getValue()) {
      return false;
   }

   //legacy status
   if (legacyStatus_ != rhs.legacyStatus_) {
      if (legacyStatus_ >= SpenderStatus::Resolved &&
         rhs.legacyStatus_ != SpenderStatus::Resolved) {
         /*
         This checks resolved state. Signed spenders are resolved.
         */
         return false;
      }
   }

   //legacy stack
   {
      //grab our resolved items from the script
      BinaryData ourSigScript = getAvailableInputScript();
      auto ourScriptItems = getResolvedItems(ourSigScript, false);

      //theirs cannot have a serialized script because theirs cannot be signed
      //grab the resolved data from the partial stack instead
      auto isMultiSig = isStackMultiSig(rhs.legacyStack_);
      auto theirSigScript = rhs.getAvailableInputScript();
      auto theirScriptItems = getResolvedItems(theirSigScript, false);

      //compare
      if (!compareScriptItems(ourScriptItems, theirScriptItems, isMultiSig)) {
         return false;
      }
   }

   //segwit status
   if (segwitStatus_ != rhs.segwitStatus_) {
      if (segwitStatus_ >= SpenderStatus::Resolved &&
         rhs.segwitStatus_ != SpenderStatus::Resolved) {
         /*
         This call checks resolved state. Signed spenders are resolved.
         */
         return false;
      }
   }

   //witness stack
   {
      //grab our resolved items from the witness data
      BinaryData ourWitnessData = serializeAvailableWitnessData();
      auto ourScriptItems = getResolvedItems(ourWitnessData, true);

      //grab theirs
      auto isMultiSig = isStackMultiSig(rhs.witnessStack_);
      auto theirWitnessData = rhs.serializeAvailableWitnessData();
      auto theirScriptItems = getResolvedItems(theirWitnessData, true);

      //compare
      if (!compareScriptItems(ourScriptItems, theirScriptItems, isMultiSig)) {
         return false;
      }
   }

   if (isP2SH_ != rhs.isP2SH_) {
      return false;
   }
   if (isCSV_ != rhs.isCSV_ || isCLTV_ != rhs.isCLTV_) {
      return false;
   }
   return true;
}

bool ScriptSpender::isInitialized() const
{
   if (legacyStatus_ == SpenderStatus::Unknown &&
      segwitStatus_ == SpenderStatus::Unknown &&
      isP2SH_ == false && legacyStack_.empty() && witnessStack_.empty() &&
      finalInputScript_.empty() && finalWitnessData_.empty()) {
      return false;
   } else {
      return true;
   }
}

bool ScriptSpender::verifyEvalState(unsigned flags)
{
   /*
   check resolution state from public data is consistent with the serialized
   script
   */

   //uninitialized spender, nothing to check
   if (!isInitialized()) {
      return true;
   }

   //sanity check: needs a utxo set to be resolved
   if (!canBeResolved()) {
      return false;
   }

   ScriptSpender spenderVerify;
   spenderVerify.sequence_ = sequence_;
   if (utxo_.isInitialized()) {
      spenderVerify.utxo_ = utxo_;
   } else {
      spenderVerify.outpoint_ = outpoint_;
   }
   spenderVerify.txMap_ = txMap_;

   /*construct public resolver from the serialized script*/
   auto feed = std::make_shared<ResolverFeed_SpenderResolutionChecks>();

   //look for push data in the sigScript
   auto legacyScript = getAvailableInputScript();

   try {
      auto pushDataVec = BtcUtils::splitPushOnlyScriptRefs(legacyScript);
      for (const auto& pushData : pushDataVec) {
         //hash it and add to the feed's hash map
         auto hash = BtcUtils::getHash160(pushData);
         feed->hashMap.emplace(hash, pushData);
      }
   } catch (const std::runtime_error&) {
      //just exit the loop on deser error
   }

   //same with the witness data
   BinaryReader brSW;
   if (finalWitnessData_.empty()) {
      std::vector<std::shared_ptr<StackItem>> stack;
      for (const auto& stack_item : witnessStack_) {
         stack.push_back(stack_item.second);
      }

      //serialize and get item count
      unsigned itemCount = 0;
      auto data = serializeWitnessData(stack, itemCount, true);

      //put stack item count
      BinaryWriter bw;
      bw.put_var_int(itemCount);

      //put serialized stack
      bw.put_BinaryData(data);
      brSW.setNewData(bw.getData());
   } else {
      brSW.setNewData(finalWitnessData_);
   }

   try {
      auto itemCount = brSW.get_var_int();
      for (unsigned i=0; i<itemCount; i++) {
         //grab next data from the script as if it's push data
         auto len = brSW.get_var_int();
         auto val = brSW.get_BinaryDataRef(len);

         //hash it and add to the feed's hash map
         auto hash160 = BtcUtils::getHash160(val);
         feed->hashMap.emplace(hash160, val);

         //sha256 in case it's a p2wsh preimage
         auto hash256 = BtcUtils::getSha256(val);
         feed->hashMap.emplace(hash256, val);
      }

      if (brSW.getSizeRemaining() > 0) {
         //unparsed data remains in the witness data script, 
         //this shouldn't happen
         return false;
      }
   } catch (const std::runtime_error&) {
      //just exit the loop on deser error
   }

   //create resolver with mock feed and process it

   try {
      StackResolver resolver(getOutputScript(), feed);
      resolver.setFlags(flags);
      spenderVerify.parseScripts(resolver);
   } catch (const std::exception&) {}

   if (!compareEvalState(spenderVerify)) {
      return false;
   }
   return true;
}

////////
void ScriptSpender::updateLegacyStack(
   const std::vector<std::shared_ptr<StackItem>>& stack)
{
   if (legacyStatus_ >= SpenderStatus::Resolved) {
      return;
   }
   if (!stack.empty()) {
      updateStack(legacyStack_, stack);
   } else {
      legacyStatus_ = SpenderStatus::Empty;
   }
}

void ScriptSpender::updateWitnessStack(
   const std::vector<std::shared_ptr<StackItem>>& stack)
{
   if (segwitStatus_ >= SpenderStatus::Resolved) {
      return;
   }
   updateStack(witnessStack_, stack);
}

////////
void ScriptSpender::parseScripts(StackResolver& resolver)
{
   /*parse the utxo scripts, fill the relevant stacks*/

   auto resolvedStack = resolver.getResolvedStack();
   if (resolvedStack == nullptr) {
      throw std::runtime_error("null resolved stack");
   }
   flagP2SH(resolvedStack->isP2SH());

   //push the legacy resolved data into the local legacy stack
   updateLegacyStack(resolvedStack->getStack());

   //parse the legacy stack, will set the legacy status
   processStacks();

   //same with the witness stack
   auto resolvedStackWitness = resolvedStack->getWitnessStack();
   if (resolvedStackWitness == nullptr) {
      if (legacyStatus_ >= SpenderStatus::Resolved &&
         segwitStatus_ < SpenderStatus::Resolved) {
         //this is a pure legacy redeem script
         segwitStatus_ = SpenderStatus::Empty;
      }
   } else {
      updateWitnessStack(resolvedStackWitness->getStack());
      processStacks();
   }

   //resolve pubkeys
   auto feed = resolver.getFeed();
   if (feed == nullptr) {
      return;
   }

   auto pubKeys = getRelevantPubkeys();
   for (const auto& pubKeyPair : pubKeys) {
      try {
         auto bip32path = feed->resolveBip32PathForPubkey(pubKeyPair.second.pubkey);
         if (!bip32path.isValid()) {
            continue;
         }
         bip32Paths_.emplace(pubKeyPair.second.pubkey, bip32path);
      } catch (const std::exception&) {
         continue;
      }
   }
}

void ScriptSpender::sign(const SignerFunc& signerFunc)
{
   auto signStack = [signerFunc](
      std::map<unsigned, std::shared_ptr<StackItem>>& stackMap, bool isSW)->void
   {
      for (auto& stackEntryPair : stackMap) {
         auto stackItem = stackEntryPair.second;
         switch (stackItem->type())
         {
            case StackItemType::Sig:
            {
               if (stackItem->isValid()) {
                  throw SpenderException("stack sig entry already filled");
               }

               auto sigItem = std::dynamic_pointer_cast<StackItem_Sig>(stackItem);
               if (sigItem == nullptr) {
                  throw std::runtime_error("unexpected stack item type");
               }

               sigItem->sig = std::move(signerFunc(
                  sigItem->script, sigItem->pubkey, isSW));
               break;
            }

            case StackItemType::MultiSig:
            {
               auto msEntryPtr =
                  std::dynamic_pointer_cast<StackItem_MultiSig>(stackItem);
               if (msEntryPtr == nullptr) {
                  throw SpenderException("invalid ms stack entry");
               }

               for (unsigned i=0; i < msEntryPtr->pubkeyVec.size(); i++) {
                  if (msEntryPtr->sigs.find(i) != msEntryPtr->sigs.end()) {
                     continue;
                  }

                  const auto& pubkey = msEntryPtr->pubkeyVec[i];
                  try {
                     auto sig = signerFunc(msEntryPtr->script, pubkey, isSW);
                     msEntryPtr->sigs.emplace(i, std::move(sig));
                     if (msEntryPtr->sigs.size() >= msEntryPtr->m) {
                        break;
                     }
                  } catch (const std::runtime_error&) {
                     //feed is missing private key, nothing to do
                  }
               }

               break;
            }

            default:
               break;
         }
      }
   };

   try {
      signStack(legacyStack_, false);
      signStack(witnessStack_, true);
   } catch (const std::exception&) {}
   processStacks();
}

////////
bool ScriptSpender::isP2SH() const
{
   return isP2SH_;
}

bool ScriptSpender::isCLTV() const
{
   return isCLTV_;
}

bool ScriptSpender::isCSV() const
{
   return isCSV_;
}

bool ScriptSpender::hasUtxo() const
{
   return utxo_.isInitialized();
}

bool ScriptSpender::isSegWit() const
{
   switch (legacyStatus_)
   {
      case SpenderStatus::Empty:
         return true; //empty legacy input means sw

      case SpenderStatus::Resolved:
      {
         //resolved legacy status could mean nested sw
         if (segwitStatus_ >= SpenderStatus::Resolved) {
            return true;
         }
      }

      default:
         return false;
   }
}

////////
void ScriptSpender::injectSignature(SecureBinaryData& sig, unsigned sigId)
{
   //sanity checks
   if (!isResolved()) {
      throw std::runtime_error("cannot inject sig into unresolved spender");
   } else if (isSigned()) {
      throw std::runtime_error("spender is already signed!");
   }
   auto& stack = isSegWit() ? witnessStack_ : legacyStack_;

   //find the stack sig object
   bool injected = false;
   for (auto& stackItemPair : stack) {
      auto& stackItem = stackItemPair.second;
      switch (stackItem->type())
      {
         case StackItemType::Sig:
         {
            if (stackItem->isValid()) {
               throw SpenderException("stack sig entry already filled");
            }
            auto stackItemSig = std::dynamic_pointer_cast<StackItem_Sig>(
               stackItem);
            if (stackItemSig == nullptr) {
               throw SpenderException("unexpected stack item type");
            }
            stackItemSig->injectSig(sig);
            injected = true;
            break;
         }

         case StackItemType::MultiSig:
         {
            if (sigId == UINT32_MAX) {
               throw SpenderException("unset sig id");
            }
            auto msEntry = std::dynamic_pointer_cast<StackItem_MultiSig>(
               stackItem);
            if (msEntry == nullptr) {
               throw SpenderException("invalid ms stack entry");
            }
            msEntry->setSig(sigId, sig);
            injected = true;
            break;
         }

         default:
            break;
      }
   }

   if (!injected) {
      throw SpenderException("failed to find sig entry in stack");
   }
   processStacks();
}

BinaryDataRef ScriptSpender::getRedeemScriptFromStack(bool isSW) const
{
   const auto& stackMap = isSW ? witnessStack_ : legacyStack_;
   std::shared_ptr<StackItem> firstPushData;

   //look for redeem script from sig stack items
   for (const auto& stackPair : stackMap) {
      auto stackItem = stackPair.second;
      switch (stackItem->type())
      {
         case StackItemType::PushData:
         {
            //grab first push data entry in stack
            if (firstPushData == nullptr) {
               firstPushData = stackItem;
            }
            break;
         }

         case StackItemType::Sig:
         {
            auto sig = std::dynamic_pointer_cast<StackItem_Sig>(
               stackItem);
            if (sig == nullptr) {
               break;
            }
            return sig->script.getRef();
         }

         case StackItemType::MultiSig:
         {
            auto msig = std::dynamic_pointer_cast<StackItem_MultiSig>(
               stackItem);
            if (msig == nullptr) {
               break;
            }
            return msig->script.getRef();
         }

         default:
            break;
      }
   }

   //if we couldn't find sig entries, let's try the first push data entry
   if (firstPushData == nullptr || !firstPushData->isValid()) {
      return {};
   }

   auto pushdata = std::dynamic_pointer_cast<StackItem_PushData>(firstPushData);
   if (pushdata == nullptr) {
      return {};
   }
   return pushdata->data;
}

std::map<BinaryData, BinaryData> ScriptSpender::getPartialSigs() const
{
   const std::map<unsigned, std::shared_ptr<StackItem>>* stackPtr = nullptr;
   if (!isSegWit()) {
      stackPtr = &legacyStack_;
   } else {
      stackPtr = &witnessStack_;
   }

   //look for multsig stack entry
   std::shared_ptr<StackItem_MultiSig> stackItemMultisig = nullptr;
   for (const auto& stackObj : *stackPtr) {
      auto stackItem = stackObj.second;
      if (stackItem->type() == StackItemType::MultiSig) {
         stackItemMultisig = std::dynamic_pointer_cast<StackItem_MultiSig>(
            stackItem);
         break;
      }
   }

   if (stackItemMultisig == nullptr) {
      return {};
   }

   std::map<BinaryData, BinaryData> sigMap;
   for (const auto& sigPair : stackItemMultisig->sigs) {
      if (sigPair.first > stackItemMultisig->pubkeyVec.size()) {
         LOGWARN << "sig index out of bounds";
         break;
      }
      const auto& pubkey = stackItemMultisig->pubkeyVec[sigPair.first];
      sigMap.emplace(pubkey, sigPair.second);
   }
   return sigMap;
}

////////
std::map<unsigned, KeyAndSig> ScriptSpender::getRelevantPubkeys() const
{
   if (!isResolved()) {
      return {};
   }

   auto stack = &legacyStack_;
   if (isSegWit()) {
      stack = &witnessStack_;
   }

   if (stack->empty()) {
      /*spender is signed, we have to parse finalInputScript_*/
      if (finalInputScript_.empty()) {
         throw std::runtime_error("both stack and final script are empty!");
      }

      int keyCount = 0;
      int sigCount = 0;
      std::map<unsigned, KeyAndSig> result;
      auto splitScript = BtcUtils::splitPushOnlyScriptRefs(finalInputScript_);
      for (const auto& scriptData : splitScript) {
         uint8_t firstByte = scriptData[0];
         if (firstByte == 0x30) {
            //sig
            result[sigCount++].sig = scriptData;
         } else if (firstByte == 0x02 ||
            firstByte == 0x03 ||
            firstByte == 0x04) {
            //pubkey
            result[keyCount++].pubkey = scriptData;
         }
      }
      return result;
   } else {
      for (auto& stackEntryPair : *stack) {
         const auto& stackItem = stackEntryPair.second;
         switch (stackItem->type())
         {
            case StackItemType::Sig:
            {
               auto sig = std::dynamic_pointer_cast<StackItem_Sig>(stackItem);
               if (stackItem == nullptr) {
                  break;
               }
               std::map<unsigned, KeyAndSig> pubkeyMap;
               pubkeyMap.emplace(0, KeyAndSig{ sig->pubkey, sig->sig });
               return pubkeyMap;
            }

            case StackItemType::MultiSig:
            {
               auto msig = std::dynamic_pointer_cast<StackItem_MultiSig>(stackItem);
               if (stackItem == nullptr) {
                  break;
               }
               std::map<unsigned, KeyAndSig> pubkeyMap;
               for (unsigned i=0; i<msig->pubkeyVec.size(); i++) {
                  const auto& pubkey = msig->pubkeyVec[i];
                  pubkeyMap.emplace(i, KeyAndSig{ pubkey, {} });

                  auto sigIter = msig->sigs.find(i);
                  if (sigIter != msig->sigs.end()) {
                     pubkeyMap[i].sig = sigIter->second;
                  }
               }
               return pubkeyMap;
            }

            default:
               break;
         }
      }
   }
   return {};
}

////////
void ScriptSpender::toPSBT(BinaryWriter& bw) const
{
   //supporting tx or utxo
   bool hasSupportingOutput = false;
   if (haveSupportingTx()) {
      //key length
      bw.put_uint8_t(1);

      //supporting tx key
      bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_NON_WITNESS_UTXO);

      //tx
      const auto& supportingTx = getSupportingTx();
      bw.put_var_int(supportingTx.getSize());
      bw.put_BinaryData(supportingTx.getPtr(), supportingTx.getSize());
      hasSupportingOutput = true;
   } else if (isSegWit() && utxo_.isInitialized()) {
      //utxo
      bw.put_uint8_t(1);
      bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_WITNESS_UTXO);

      auto rawUtxo = utxo_.serializeTxOut();
      bw.put_var_int(rawUtxo.getSize());
      bw.put_BinaryData(rawUtxo);

      hasSupportingOutput = true;
   }

   //partial sigs
   {
      /*
      This section only applies to MS or exotic scripts that can be
      partially signed. Single sig scripts go to the finalized
      section right away.
      */

      auto partialSigs = getPartialSigs();
      for (auto& sigPair : partialSigs) {
         bw.put_var_int(sigPair.first.getSize() + 1);
         bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_PARTIAL_SIG);
         bw.put_BinaryData(sigPair.first);
         bw.put_var_int(sigPair.second.getSize());
         bw.put_BinaryData(sigPair.second);
      }
   }

   //sig hash, conditional on utxo/prevTx presence
   if (hasSupportingOutput && !isSigned()) {
      bw.put_uint8_t(1);
      bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_SIGHASH_TYPE);
      bw.put_uint8_t(4);
      bw.put_uint32_t((uint32_t)sigHashType_);
   }

   //redeem script
   if (!isSigned()) {
      auto redeemScript = getRedeemScriptFromStack(false);
      if (!redeemScript.empty()) {
         bw.put_uint8_t(1);
         bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_REDEEM_SCRIPT);
         bw.put_var_int(redeemScript.getSize());
         bw.put_BinaryDataRef(redeemScript);
      }
   }

   //witness script
   if (isSegWit()) {
      auto witnessScript = getRedeemScriptFromStack(true);
      if (!witnessScript.empty()) {
         bw.put_uint8_t(1);
         bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_WITNESS_SCRIPT);
         bw.put_var_int(witnessScript.getSize());
         bw.put_BinaryDataRef(witnessScript);
      }
   }

   if (!isSigned()) {
      //pubkeys
      for (auto& bip32Path : bip32Paths_) {
         if (!bip32Path.second.isValid()) {
            continue;
         }
         bw.put_uint8_t(34);
         bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_BIP32_DERIVATION);
         bw.put_BinaryData(bip32Path.first);

         //path
         bip32Path.second.toPSBT(bw);
      }
   } else {
      //scriptSig
      auto finalizedInputScript = getAvailableInputScript();
      if (!finalizedInputScript.empty()) {
         bw.put_uint8_t(1);
         bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_FINAL_SCRIPTSIG);
         bw.put_var_int(finalizedInputScript.getSize());
         bw.put_BinaryData(finalizedInputScript);
      }

      auto finalizedWitnessData = getFinalizedWitnessData();
      if (!finalizedWitnessData.empty()) {
         bw.put_uint8_t(1);
         bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_FINAL_SCRIPTWITNESS);
         bw.put_var_int(finalizedWitnessData.getSize());
         bw.put_BinaryData(finalizedWitnessData);
      }
   }

   //proprietary data
   for (const auto& data : prioprietaryPSBTData_) {
      //key
      bw.put_var_int(data.first.getSize() + 1);
      bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_PROPRIETARY);
      bw.put_BinaryData(data.first);

      //val
      bw.put_var_int(data.second.getSize());
      bw.put_BinaryData(data.second);
   }

   //terminate
   bw.put_uint8_t(0);
}

std::shared_ptr<ScriptSpender> ScriptSpender::fromPSBT(
   BinaryRefReader& brr, const TxIn& txin,
   std::shared_ptr<std::map<BinaryData, Tx>> txMap)
{
   UTXO utxo;
   bool haveSupportingTx = false;

   std::map<BinaryDataRef, BinaryDataRef> partialSigs;
   std::map<BinaryData, BIP32_AssetPath> bip32paths;
   std::map<BinaryData, BinaryData> prioprietaryPSBTData;

   BinaryDataRef redeemScript;
   BinaryDataRef witnessScript;
   BinaryDataRef finalRedeemScript;
   BinaryDataRef finalWitnessScript;

   uint32_t sigHash = (uint32_t)SIGHASH_ALL;

   auto inputDataPairs = BtcUtils::getPSBTDataPairs(brr);
   for (const auto& dataPair : inputDataPairs) {
      const auto& key = dataPair.first;
      const auto& val = dataPair.second;

      //key type
      auto typePtr = key.getPtr();
      switch (*typePtr)
      {
         case PSBT::ENUM_INPUT::PSBT_IN_NON_WITNESS_UTXO:
         {
            if (txMap == nullptr) {
               throw PSBT::DeserError("null txmap");
            }

            //supporting tx, key has to be 1 byte long
            if (key.getSize() != 1) {
               throw PSBT::DeserError("unvalid supporting tx key len");
            }

            Tx tx(val);
            txMap->emplace(tx.getThisHash(), std::move(tx));
            haveSupportingTx = true;
            break;
         }

         case PSBT::ENUM_INPUT::PSBT_IN_WITNESS_UTXO:
         {
            //utxo, key has to be 1 byte long
            if (key.getSize() != 1) {
               throw PSBT::DeserError("unvalid utxo key len");
            }
            utxo.unserializeRaw(val);
            break;
         }

         case PSBT::ENUM_INPUT::PSBT_IN_PARTIAL_SIG:
         {
            partialSigs.emplace(key.getSliceRef(1, key.getSize() - 1), val);
            break;
         }

         case PSBT::ENUM_INPUT::PSBT_IN_SIGHASH_TYPE:
         {
            if (key.getSize() != 1) {
               throw PSBT::DeserError("unvalid sighash key len");
            }
            if (val.getSize() != 4) {
               throw PSBT::DeserError("invalid sighash val length");
            }

            memcpy(&sigHash, val.getPtr(), sizeof(uint32_t));
            break;
         }

         case PSBT::ENUM_INPUT::PSBT_IN_REDEEM_SCRIPT:
         {
            if (key.getSize() != 1) {
               throw PSBT::DeserError("unvalid redeem script key len");
            }
            redeemScript = val;
            break;
         }

         case PSBT::ENUM_INPUT::PSBT_IN_WITNESS_SCRIPT:
         {
            if (key.getSize() != 1) {
               throw PSBT::DeserError("unvalid witness script key len");
            }
            witnessScript = val;
            break;
         }

         case PSBT::ENUM_INPUT::PSBT_IN_BIP32_DERIVATION:
         {
            auto assetPath = BIP32_AssetPath::fromPSBT(key, val);
            auto insertIter = bip32paths.emplace(
               assetPath.getPublicKey(), assetPath);

            if (!insertIter.second) {
               throw PSBT::DeserError("bip32 path collision");
            }
            break;
         }

         case PSBT::ENUM_INPUT::PSBT_IN_FINAL_SCRIPTSIG:
         {
            if (key.getSize() != 1) {
               throw PSBT::DeserError("unvalid finalized input script key len");
            }
            finalRedeemScript = val;
            break;
         }

         case PSBT::ENUM_INPUT::PSBT_IN_FINAL_SCRIPTWITNESS:
         {
            if (key.getSize() != 1) {
               throw PSBT::DeserError("unvalid finalized witness script key len");
            }
            finalWitnessScript = val;
            break;
         }

         case PSBT::ENUM_INPUT::PSBT_IN_PROPRIETARY:
         {
            //proprietary data doesn't have to be interpreted but
            //it needs carried over
            prioprietaryPSBTData.emplace(
               key.getSliceRef(1, key.getSize() - 1), val);
            break;
         }

         default:
            throw PSBT::DeserError("unexpected txin key");
      }
   }

   //create spender
   std::shared_ptr<ScriptSpender> spender;
   auto outpoint = txin.getOutPoint();

   if (!haveSupportingTx && utxo.isInitialized()) {
      utxo.txHash_ = outpoint.getTxHash();
      utxo.txOutIndex_ = outpoint.getTxOutIndex();
      spender = std::make_shared<ScriptSpender>(utxo);
   } else {
      spender = std::make_shared<ScriptSpender>(
         outpoint.getTxHash(), outpoint.getTxOutIndex());
   }

   spender->setTxMap(txMap);
   auto feed = std::make_shared<ResolverFeed_SpenderResolutionChecks>();

   bool isSigned = false;
   if (!finalRedeemScript.empty()) {
      spender->finalInputScript_ = finalRedeemScript;
      spender->legacyStatus_ = SpenderStatus::Signed;
      spender->segwitStatus_ = SpenderStatus::Empty;
      isSigned = true;
   }

   if (!finalWitnessScript.empty()) {
      spender->finalWitnessData_ = finalWitnessScript;
      spender->segwitStatus_ = SpenderStatus::Signed;
      if (isSigned) {
         spender->legacyStatus_ = SpenderStatus::Resolved;
      } else {
         spender->legacyStatus_ = SpenderStatus::Empty;
      }
      isSigned = true;
   }

   if (!isSigned) {
      //redeem scripts
      if (!redeemScript.empty()) {
         //add to custom feed
         auto hash = BtcUtils::getHash160(redeemScript);
         feed->hashMap.emplace(hash, redeemScript);
      }

      if (!witnessScript.empty()) {
         //add to custom feed
         auto hash = BtcUtils::getHash160(witnessScript);
         feed->hashMap.emplace(hash, witnessScript);

         hash = BtcUtils::getSha256(witnessScript);
         feed->hashMap.emplace(hash, witnessScript);
      }

      //resolve
      try {
         StackResolver resolver(spender->getOutputScript(), feed);
         resolver.setFlags(
            SCRIPT_VERIFY_P2SH | 
            SCRIPT_VERIFY_SEGWIT | 
            SCRIPT_VERIFY_P2SH_SHA256);

         spender->parseScripts(resolver);
      } catch (const std::exception&) {}

      //get pubkeys
      auto pubkeys = spender->getRelevantPubkeys();

      //check pubkeys are relevant
      {
         std::set<BinaryDataRef> pubkeyRefs;
         for (const auto& pubkey : pubkeys) {
            pubkeyRefs.emplace(pubkey.second.pubkey.getRef());
         }

         for (auto& bip32path : bip32paths) {
            auto iter = pubkeyRefs.find(bip32path.first);
            if (iter == pubkeyRefs.end()) {
               throw PSBT::DeserError("have bip32path for unrelated pubkey");
            }
            spender->bip32Paths_.emplace(bip32path);
         }
      }

      //inject partial sigs
      if (!partialSigs.empty()) {
         for (auto& pubkey : pubkeys) {
            auto iter = partialSigs.find(pubkey.second.pubkey);
            if (iter == partialSigs.end()) {
               continue;
            }

            SecureBinaryData sig(iter->second);
            spender->injectSignature(sig, pubkey.first);
            partialSigs.erase(iter);
         }

         if (!partialSigs.empty()) {
            throw PSBT::DeserError("couldn't inject sigs");
         }
      }

      spender->setSigHashType((SIGHASH_TYPE)sigHash);
   }

   spender->prioprietaryPSBTData_ = std::move(prioprietaryPSBTData);
   return spender;
}

////////
void ScriptSpender::setTxMap(
   std::shared_ptr<std::map<BinaryData, Tx>> txMap)
{
   txMap_ = txMap;
}

bool ScriptSpender::setSupportingTx(BinaryDataRef rawTx)
{
   if (rawTx.empty()) {
      return false;
   }

   try {
      Tx tx(rawTx);
      return setSupportingTx(std::move(tx));
   } catch (const std::exception&) {}
   return false;
}

bool ScriptSpender::setSupportingTx(Tx supportingTx)
{
   /*
   Returns true if the supporting tx is relevant to this spender, false 
   otherwise
   */
   if (supportingTx.getThisHash() != getOutputHash()) {
      return false;
   }
   auto insertIter = txMap_->emplace(
      supportingTx.getThisHash(), std::move(supportingTx));
   return insertIter.second;
}

bool ScriptSpender::haveSupportingTx() const
{
   if (txMap_ == nullptr) {
      return false;
   }

   try {
      auto hash = getOutputHash();
      auto iter = txMap_->find(hash);
      return (iter != txMap_->end());
   } catch (const std::exception&) {}
   return false;
}

const Tx& ScriptSpender::getSupportingTx() const
{
   if (txMap_ == nullptr) {
      throw SpenderException("missing tx map");;
   }

   auto hash = getOutputHash();
   auto iter = txMap_->find(hash);
   if (iter == txMap_->end()) {
      throw SpenderException("missing supporting tx");
   }
   return iter->second;
}

////////
bool ScriptSpender::canBeResolved() const
{
   if (utxo_.isInitialized()) {
      return true;
   }
   if (outpoint_.getSize() != 36) {
      return false;
   }
   return haveSupportingTx();
}

uint64_t ScriptSpender::getValue() const
{
   if (utxo_.isInitialized()) {
      return utxo_.getValue();
   }
   if (!haveSupportingTx()) {
      throw SpenderException("missing both supporting tx and utxo");
   }

   auto index = getOutputIndex();
   const auto& supportingTx = getSupportingTx();
   auto txOutCopy = supportingTx.getTxOutCopy(index);
   return txOutCopy.getValue();
}

void ScriptSpender::seedResolver(std::shared_ptr<ResolverFeed> feedPtr,
   bool seedLegacyAssets) const
{
   for (auto& bip32Path : bip32Paths_) {
      feedPtr->setBip32PathForPubkey(bip32Path.first, bip32Path.second);
   }
   if (!seedLegacyAssets) {
      return;
   }
   if (!bip32Paths_.empty()) {
      return;
   }
   if (!isP2SH()) {
      return;
   }
   /***
   Covering for a ResolverFeed edge case:

   When a P2SH spender is resolved for the first time, its P2SH script is
   processed, the hash we're paying to (P2SH stands for Pay-2-Script-Hash)
   is extracted then passed to the resolver feed to get the preimage used
   to construct that hash.
   The resolver will find the asset for this hash and cache to relation to
   the public key as part of the operation. It will also cache the bip32
   path to the asset if available. This works because Armory wallets keep
   track of assets by their final script hash.

   Later, at signature time, the signer will present pubkeys to the resolver,
   expecting private keys in return. This does not work for P2SH natively.
   This is because there is no direct translation from a pubkey to a P2SH
   script. The resolver cannot find the asset for a pubkey by simply hashing
   it, and Armory wallets do not track assets by their pubkey. This holds
   true for all script hashes that do not directly descend from their pubkey.

   However, thanks to the caching that occured previously (caching the pubkey
   when looking for the asset by hash), this isn't an issue when *the resolver
   state is carried along from resolution to signing*. This is typically the
   case when signing a single sig input, but cannot be guaranteed when
   signing across multiple wallets.

   Since the resolver knows to look for data in its cache, a simple solution
   is to preseed the resolver feed cache with the resolved data. For bip32
   assets, this is a straight forward operation (pass the bip32 path
   for each known pubkey to the resolver). This also happens to make the
   signer compliant with PSBT (which requires the BIP32 path for each key to
   sign for).

   This would be the end of it if Armory only used BIP32 wallets, but it
   doesn't. Signers do not carry any data specifically tying back to legacy
   Armory assets (1.xx wallets).

   The best solution is to carry such data. In the meantime, a stopgap
   solution is to present those script hashes from legacy assets to the
   resolver so as to trigger resolution and pubkey hashing, as if processed
   for the first time.

   TODO: carry dedicated identifiers for resolved legacy armory assets
         as part of resolvers and signer states
   ***/
   if (!utxo_.isInitialized()) {
      LOGWARN << "[seedResolver] missing utxo";
      return;
   }

   auto hash = BtcUtils::getTxOutRecipientAddr(utxo_.script_);
   try {
      feedPtr->getByVal(hash);
   } catch (const std::exception&) {
      LOGWARN << "[seedResolver] failed to preseed cache";
   }
}

////////
void ScriptSpender::prettyPrint(std::ostream& os) const
{
   auto statusStrLbd = [](SpenderStatus status)->std::string
   {
      switch (status)
      {
         case SpenderStatus::Unknown:
            return std::string("Unknown");

         case SpenderStatus::Empty:
            return std::string("Empty");

         case SpenderStatus::Resolved:
            return std::string("Resolved");

         case SpenderStatus::PartiallySigned:
            return std::string("Partially signed");

         case SpenderStatus::Signed:
            return std::string("Signed");

         default:
            break;
      }
      return std::string("N/A");
   };

   //hash and id
   os << "  * hash: " << getOutputHash().toHexStr(true) <<
      ", id: " << getOutputIndex() << std::endl;

   os << "    Legacy status: " << statusStrLbd(legacyStatus_) <<
      ", Segwit status: " << statusStrLbd(segwitStatus_) << std::endl;
}
