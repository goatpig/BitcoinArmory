////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Utils/BtcUtils.h"
#include "Utils/BinaryData.h"
#include "Utils/Cryptography.h"

#include "Script.h"
#include "Transactions.h"
#include "Signer.h"

using namespace Armory;
using namespace Armory::Signing;

namespace
{
   bool notZero(const BinaryData& data)
   {
      //TODO: check for negative zero as well
      if (!data.empty()) {
         auto ptr = data.getPtr();
         for (unsigned i = 0; i < data.getSize(); i++) {
            if (*(ptr++) != 0) {
               return true;
            }
         }
      }
      return false;
   }

   int64_t rawBinaryToInt(const BinaryData& bd)
   {
      auto len = bd.getSize();
      if (len == 0) {
         return 0;
      }

      if (len > 4) {
         throw ScriptException("int overflow");
      }

      int64_t val = 0;
      memcpy(&val, bd.getPtr(), len);

      auto valptr = (uint8_t*)&val;
      --len;
      if (valptr[len] & 0x80) {
         valptr[len] &= 0x7F;
         val *= -1;
      }
      return val;
   }

   OpCode getNextOpcode(BinaryRefReader& brr)
   {
      OpCode val;
      val.offset = brr.getPosition();
      val.opcode = brr.get_uint8_t();
      if (val.opcode <= 75 && val.opcode > 0) {
         val.dataRef = brr.get_BinaryDataRef(val.opcode);
      } else {
         unsigned len = 0;
         switch (val.opcode)
         {
            case OP_PUSHDATA1:
               len = brr.get_uint8_t();
               break;

            case OP_PUSHDATA2:
               len = brr.get_uint16_t();
               break;

            case OP_PUSHDATA4:
               len = brr.get_uint32_t();
               break;

            case OP_IF:
            case OP_NOTIF:
               len = brr.getSizeRemaining();
               break;

            default:
               return val;
         }
         val.dataRef = brr.get_BinaryDataRef(len);
      }
      return val;
   }

   BinaryData intToRawBinary(int64_t val)
   {
      //op_code outputs are allowed to overflow the 32 bit int limitation
      if (val == 0) {
         return BinaryData{};
      }

      auto absval = abs(val);
      bool neg = val < 0;
      int mostSignificantByteOffset = 7;

      auto intptr = (uint8_t*)&absval;
      while (mostSignificantByteOffset > 0) {
         auto byteval = *(intptr + mostSignificantByteOffset);
         if (byteval > 0) {
            if (byteval & 0x80) {
               ++mostSignificantByteOffset;
            }
            break;
         }
         --mostSignificantByteOffset;
      }

      if (mostSignificantByteOffset > 7) {
         throw ScriptException("int overflow");
      }

      if (neg) {
         intptr[mostSignificantByteOffset] |= 0x80;
      }

      ++mostSignificantByteOffset;
      BinaryData bd;
      bd.resize(mostSignificantByteOffset);
      memcpy(bd.getPtr(), &absval, mostSignificantByteOffset);
      return bd;
   }

   void seekToNextIfSwitch(BinaryRefReader& brr)
   {
      int depth = 0;
      while (brr.getSizeRemaining() > 0) {
         auto data = getNextOpcode(brr);
         switch (data.opcode)
         {
            case OP_IF:
            case OP_NOTIF:
               depth++;
               break;

            case OP_ENDIF:
               if (depth-- > 0) {
                  break;
               }
               [[fallthrough]];

            case OP_ELSE:
            {
               if (depth > 0) {
                  break;
               }
               brr.rewind(1 + data.dataRef.getSize());
               return;
            }
         }
      }
      throw ScriptException("no extra if switches");
   }

   void seekToEndIf(BinaryRefReader& brr)
   {
      while (brr.getSizeRemaining() > 0) {
         seekToNextIfSwitch(brr);
         auto opcode = brr.get_uint8_t();
         if (opcode == OP_ENDIF) {
            return;
         }
      }
      throw ScriptException("couldn't not find ENDIF opcode");
   }

   BinaryData resolveReferenceValue(std::shared_ptr<ReversedStackEntry> inPtr)
   {
      auto currentPtr = inPtr;
      while (true) {
         if (currentPtr->parent_ != nullptr) {
            currentPtr = currentPtr->parent_;
         } else if (currentPtr->static_) {
            return currentPtr->staticData_;
         } else {
            switch (currentPtr->resolvedValue_->type())
            {
               case StackValueType::Static:
               {
                  auto staticVal = std::dynamic_pointer_cast<StackValue_Static>(
                     currentPtr->resolvedValue_);
                  return staticVal->value_;
               }

               case StackValueType::FromFeed:
               {
                  auto feedVal = std::dynamic_pointer_cast<StackValue_FromFeed>(
                     currentPtr->resolvedValue_);
                  return feedVal->value_;
               }

               case StackValueType::Reference:
               {
                  auto refVal = std::dynamic_pointer_cast<StackValue_Reference>(
                     currentPtr->resolvedValue_);
                  currentPtr = refVal->valueReference_;
                  break;
               }

               default:
                  throw ScriptException("unexpected StackValue type \
                     during reference resolution");
            }
         }

         if (currentPtr == inPtr) {
            throw ScriptException("infinite loop in reference resolution");
         }
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
// ScriptParser
size_t ScriptParser::seekToOpCode(BinaryRefReader& brr, OPCODETYPE opcode) const
{
   while (brr.getSizeRemaining() > 0) {
      auto oc = getNextOpcode(brr);
      if (oc.opcode == opcode) {
         return brr.getPosition() - 1 - oc.dataRef.getSize();
      }
   }
   return brr.getPosition();
}

void ScriptParser::parseScript(BinaryRefReader& brr)
{
   while (brr.getSizeRemaining() != 0) {
      auto oc = getNextOpcode(brr);
      processOpCode(oc);
   }
}

////////////////////////////////////////////////////////////////////////////////
// StackInterpreter
StackInterpreter::StackInterpreter() :
   txStubPtr_(nullptr), inputIndex_(-1)
{
   //TODO: figure out rule detection
   flags_ = SCRIPT_VERIFY_P2SH;
}

StackInterpreter::StackInterpreter(
   const TransactionStub* stubPtr, unsigned inputId) :
   txStubPtr_(stubPtr), inputIndex_(inputId)
{}

void StackInterpreter::push_back(const BinaryData& data)
{
   stack_.emplace_back(data);
}

BinaryData StackInterpreter::pop_back()
{
   if (stack_.empty()) {
      throw ScriptException("tried to pop an empty stack");
   }
   auto data = stack_.back();
   stack_.pop_back();
   return data;
}

const BinaryData& StackInterpreter::stack_back() const
{
   if (stack_.empty()) {
      throw ScriptException("tried to peak an empty stack");
   }
   return stack_.back();
}

void StackInterpreter::setSegWitSigHashDataObject(
   std::shared_ptr<SigHashDataSegWit> shdo)
{
   SHD_SW_ = shdo;
}

unsigned StackInterpreter::getFlags() const
{
   return flags_;
}

void StackInterpreter::setFlags(unsigned flags)
{
   flags_ = flags;
}

const TxInEvalState& StackInterpreter::getTxInEvalState() const
{
   return txInEvalState_;
}

////////
void StackInterpreter::processScript(
   const BinaryDataRef& script, bool isOutputScript)
{
   BinaryRefReader brr(script);
   processScript(brr, isOutputScript);
}

void StackInterpreter::processScript(BinaryRefReader& brr, bool isOutputScript)
{
   if (txStubPtr_ == nullptr)
      throw ScriptException("uninitialized stack");

   if (isOutputScript)
      outputScriptRef_ = brr.getRawRef();

   opcount_ = 0;
   isValid_ = false;

   ScriptParser::parseScript(brr);
}

////////
void StackInterpreter::op_if(BinaryRefReader& brr, bool isOutputScript)
{
   //find next if switch offset
   auto innerBlock = brr.fork();
   seekToNextIfSwitch(innerBlock);

   //get block ref for this if block
   BinaryRefReader thisIfBlock(
      brr.get_BinaryDataRef(innerBlock.getPosition()));

   try {
      //verify top stack item
      op_verify();

      //reset isValid flag
      isValid_ = false;

      //process block
      processScript(thisIfBlock, isOutputScript);

      //exit if statement
      seekToEndIf(brr);
   } catch (const ScriptException&) {
      //move to next opcode
      auto opcode = brr.get_uint8_t();
      if (opcode == OP_ENDIF) {
         return;
      }

      if (opcode != OP_ELSE) {
         throw ScriptException("expected OP_ELSE");
      }

      //look for else or endif opcode
      innerBlock = brr.fork();
      seekToNextIfSwitch(innerBlock);

      thisIfBlock = BinaryRefReader(
         brr.get_BinaryDataRef(innerBlock.getPosition()));

      //process block
      processScript(thisIfBlock, isOutputScript);

      //exit if statement
      seekToEndIf(brr);
   }
}

void StackInterpreter::op_0()
{
   stack_.emplace_back(BinaryData{});
}

void StackInterpreter::op_true()
{
   BinaryData btrue;
   btrue.append(1);
   stack_.emplace_back(std::move(btrue));
}

void StackInterpreter::op_1negate()
{
   stack_.emplace_back(std::move(intToRawBinary(-1)));
}

void StackInterpreter::op_depth()
{
   stack_.emplace_back(std::move(intToRawBinary(stack_.size())));
}

void StackInterpreter::op_dup()
{
   stack_.emplace_back(stack_back());
}

void StackInterpreter::op_nip()
{
   auto data1 = pop_back();
   auto data2 = pop_back();
   stack_.emplace_back(std::move(data1));
}

void StackInterpreter::op_over()
{
   if (stack_.size() < 2) {
      throw ScriptException("stack is too small for op_over");
   }
   auto stackIter = stack_.rbegin();
   auto data = *(stackIter + 1);
   stack_.emplace_back(std::move(data));
}

void StackInterpreter::op_2dup()
{
   if (stack_.size() < 2) {
      throw ScriptException("stack is too small for op_2dup");
   }
   auto stackIter = stack_.rbegin();
   auto i0 = *(stackIter + 1);
   auto i1 = *stackIter;

   stack_.emplace_back(i0);
   stack_.emplace_back(i1);
}

void StackInterpreter::op_3dup()
{
   if (stack_.size() < 3) {
      throw ScriptException("stack is too small for op_3dup");
   }
   auto stackIter = stack_.rbegin();
   auto i0 = *(stackIter + 2);
   auto i1 = *(stackIter + 1);
   auto i2 = *stackIter;

   stack_.emplace_back(i0);
   stack_.emplace_back(i1);
   stack_.emplace_back(i2);
}

void StackInterpreter::op_2over()
{
   if (stack_.size() < 4) {
      throw ScriptException("stack is too small for op_2over");
   }
   auto stackIter = stack_.rbegin();
   auto i0 = *(stackIter + 3);
   auto i1 = *(stackIter + 2);

   stack_.emplace_back(i0);
   stack_.emplace_back(i1);
}

void StackInterpreter::op_toaltstack()
{
   auto a = pop_back();
   altstack_.emplace_back(std::move(a));
}

void StackInterpreter::op_fromaltstack()
{
   if (altstack_.empty()) {
      throw ScriptException("tried to pop an empty altstack");
   }
   const auto& a = altstack_.back();
   stack_.emplace_back(a);
   altstack_.pop_back();
}

void StackInterpreter::op_ifdup()
{
   auto& data = stack_back();
   if (notZero(data)) {
      stack_.emplace_back(data);
   }
}

void StackInterpreter::op_pick()
{
   auto a = pop_back();
   auto aI = rawBinaryToInt(a);

   if (aI >= (int64_t)stack_.size()) {
      throw ScriptException("op_pick index exceeds stack size");
   }
   auto stackIter = stack_.rbegin() + aI;
   stack_.emplace_back(*stackIter);
}

void StackInterpreter::op_roll()
{
   auto a = pop_back();
   auto rollindex = rawBinaryToInt(a);

   if (rollindex >= (int64_t)stack_.size()) {
      throw ScriptException("op_roll index exceeds stack size");
   }

   std::vector<BinaryData> dataVec;
   while (rollindex-- > 0) {
      dataVec.emplace_back(std::move(pop_back()));
   }
   auto rolldata = pop_back();

   auto dataIter = dataVec.rbegin();
   while (dataIter != dataVec.rend()) {
      stack_.emplace_back(std::move(*dataIter));
      ++dataIter;
   }
   stack_.emplace_back(rolldata);
}

void StackInterpreter::op_rot()
{
   auto c = pop_back();
   auto b = pop_back();
   auto a = pop_back();

   stack_.emplace_back(std::move(b));
   stack_.emplace_back(std::move(c));
   stack_.emplace_back(std::move(a));
}

void StackInterpreter::op_swap()
{
   auto data1 = pop_back();
   auto data2 = pop_back();

   stack_.emplace_back(std::move(data1));
   stack_.emplace_back(std::move(data2));
}

void StackInterpreter::op_tuck()
{
   auto b = pop_back();
   auto a = pop_back();

   stack_.emplace_back(std::move(b));
   stack_.emplace_back(std::move(a));
   stack_.emplace_back(std::move(b));
}

void StackInterpreter::op_ripemd160()
{
   auto data = pop_back();
   auto hash = BtcUtils::ripemd160(data);
   stack_.emplace_back(std::move(hash));
}

void StackInterpreter::op_sha256()
{
   auto data = pop_back();
   auto sha256 = BtcUtils::getSha256(data);
   stack_.emplace_back(std::move(sha256));
}

void StackInterpreter::op_hash160()
{
   auto data = pop_back();
   auto hash160 = BtcUtils::getHash160(data);
   stack_.emplace_back(std::move(hash160));
}

void StackInterpreter::op_hash256()
{
   auto data = pop_back();
   auto hash256 = BtcUtils::getHash256(data);
   stack_.emplace_back(std::move(hash256));
}

void StackInterpreter::op_size()
{
   const auto& data = stack_back();
   stack_.emplace_back(std::move(intToRawBinary(data.getSize())));
}

void StackInterpreter::op_equal()
{
   auto data1 = pop_back();
   auto data2 = pop_back();
   bool state = (data1 == data2);

   BinaryData bd;
   bd.append(state);
   stack_.emplace_back(std::move(bd));
}

void StackInterpreter::op_1add()
{
   auto a = pop_back();
   auto aI = rawBinaryToInt(a);

   stack_.emplace_back(std::move(intToRawBinary(aI + 1)));
}

void StackInterpreter::op_1sub()
{
   auto a = pop_back();
   auto aI = rawBinaryToInt(a);
   stack_.emplace_back(std::move(intToRawBinary(aI - 1)));
}

void StackInterpreter::op_negate()
{
   auto a = pop_back();
   auto aI = rawBinaryToInt(a);
   stack_.emplace_back(std::move(intToRawBinary(-aI)));
}

void StackInterpreter::op_abs()
{
   auto a = pop_back();
   auto aI = rawBinaryToInt(a);
   auto negA = intToRawBinary(abs(aI));
   stack_.emplace_back(negA);
}

void StackInterpreter::op_not()
{
   auto a = pop_back();
   auto aI = rawBinaryToInt(a);
   if (aI != 0) {
      aI = 0;
   } else {
      aI = 1;
   }
   stack_.emplace_back(std::move(intToRawBinary(aI)));
}

void StackInterpreter::op_0notequal()
{
   auto a = pop_back();
   auto aI = rawBinaryToInt(a);
   if (aI != 0) {
      aI = 1;
   }
   stack_.emplace_back(std::move(intToRawBinary(aI)));
}

void StackInterpreter::op_numequal()
{
   auto b = pop_back();
   auto a = pop_back();

   auto aI = rawBinaryToInt(a);
   auto bI = rawBinaryToInt(b);
   bool state = (aI == bI);

   BinaryData bd;
   bd.append(state);
   stack_.emplace_back(std::move(bd));
}

void StackInterpreter::op_numnotequal()
{
   auto b = pop_back();
   auto a = pop_back();

   auto aI = rawBinaryToInt(a);
   auto bI = rawBinaryToInt(b);
   bool state = (aI != bI);

   BinaryData bd;
   bd.append(state);
   stack_.emplace_back(std::move(bd));
}

void StackInterpreter::op_lessthan()
{
   auto b = pop_back();
   auto a = pop_back();

   auto aI = rawBinaryToInt(a);
   auto bI = rawBinaryToInt(b);
   bool state = (aI < bI);

   BinaryData bd;
   bd.append(state);
   stack_.emplace_back(std::move(bd));
}

void StackInterpreter::op_lessthanorequal()
{
   auto b = pop_back();
   auto a = pop_back();

   auto aI = rawBinaryToInt(a);
   auto bI = rawBinaryToInt(b);
   bool state = (aI <= bI);

   BinaryData bd;
   bd.append(state);
   stack_.emplace_back(std::move(bd));
}

void StackInterpreter::op_greaterthan()
{
   auto b = pop_back();
   auto a = pop_back();

   auto aI = rawBinaryToInt(a);
   auto bI = rawBinaryToInt(b);
   bool state = (aI > bI);

   BinaryData bd;
   bd.append(state);
   stack_.emplace_back(std::move(bd));
}

void StackInterpreter::op_greaterthanorequal()
{
   auto b = pop_back();
   auto a = pop_back();

   auto aI = rawBinaryToInt(a);
   auto bI = rawBinaryToInt(b);
   bool state = (aI >= bI);

   BinaryData bd;
   bd.append(state);
   stack_.emplace_back(std::move(bd));
}

void StackInterpreter::op_within()
{
   auto top = pop_back();
   auto bot = pop_back();
   auto x = pop_back();

   auto xI = rawBinaryToInt(x);
   auto topI = rawBinaryToInt(top);
   auto botI = rawBinaryToInt(bot);
   bool state = (xI >= botI && xI < topI);

   BinaryData bd;
   bd.append(state);
   stack_.emplace_back(std::move(bd));
}

void StackInterpreter::op_booland()
{
   auto b = pop_back();
   auto a = pop_back();

   auto aI = rawBinaryToInt(a);
   auto bI = rawBinaryToInt(b);
   uint8_t val = 0;
   if (aI != 0 && bI != 0) {
      val = 1;
   }

   BinaryData bd;
   bd.append(val);
   stack_.emplace_back(std::move(bd));
}

void StackInterpreter::op_boolor()
{
   auto b = pop_back();
   auto a = pop_back();

   auto aI = rawBinaryToInt(a);
   auto bI = rawBinaryToInt(b);
   uint8_t val = 0;
   if (aI != 0 || bI != 0) {
      val = 1;
   }

   BinaryData bd;
   bd.append(val);
   stack_.emplace_back(std::move(bd));
}

void StackInterpreter::op_add()
{
   auto b = pop_back();
   auto a = pop_back();

   auto aI = rawBinaryToInt(a);
   auto bI = rawBinaryToInt(b);
   auto cI = aI + bI;
   stack_.emplace_back(std::move(intToRawBinary(cI)));
}

void StackInterpreter::op_sub()
{
   auto b = pop_back();
   auto a = pop_back();

   auto aI = rawBinaryToInt(a);
   auto bI = rawBinaryToInt(b);
   auto cI = aI - bI;
   stack_.emplace_back(std::move(intToRawBinary(cI)));
}

void StackInterpreter::op_verify()
{
   auto data = pop_back();
   isValid_ = notZero(data);

   if (!isValid_) {
      throw ScriptException("op_verify returned false");
   }
}

void StackInterpreter::op_min(void)
{
   auto b = pop_back();
   auto a = pop_back();

   auto aI = rawBinaryToInt(a);
   auto bI = rawBinaryToInt(b);
   auto cI = std::min(aI, bI);
   stack_.emplace_back(std::move(intToRawBinary(cI)));
}

void StackInterpreter::op_max(void)
{
   auto b = pop_back();
   auto a = pop_back();

   auto aI = rawBinaryToInt(a);
   auto bI = rawBinaryToInt(b);
   auto cI = std::max(aI, bI);
   stack_.emplace_back(std::move(intToRawBinary(cI)));
}

void StackInterpreter::op_checksig()
{
   //pop sig and pubkey from the stack
   if (stack_.size() < 2) {
      throw ScriptException("insufficient stack size for checksig operation");
   }

   txInEvalState_.n_ = 1;
   txInEvalState_.m_ = 1;

   auto pubkey = pop_back();
   auto sigScript = pop_back();
   if (sigScript.getSize() < 65) {
      txInEvalState_.pubKeyState_.emplace(pubkey, false);
      stack_.emplace_back(std::move(intToRawBinary(false)));
      return;
   }

   //extract sig and sighash type
   BinaryRefReader brrSig(sigScript);
   auto sigsize = sigScript.getSize() - 1;
   auto sig = brrSig.get_BinaryDataRef(sigsize);
   auto hashType = getSigHashSingleByte(brrSig.get_uint8_t());

   //get data for sighash
   if (sigHashDataObject_ == nullptr) {
      sigHashDataObject_ = std::make_shared<SigHashDataLegacy>();
   }
   auto sighashdata = sigHashDataObject_->getDataForSigHash(
      hashType, *txStubPtr_,
      outputScriptRef_, inputIndex_);

   if (!Cryptography::ECDSA::verifyPublicKeyValid(pubkey.getRef())) {
      throw std::runtime_error("invalid pubkey");
   }

   //check signature
   auto result = Cryptography::ECDSA::verifyData(sighashdata, sig, pubkey);
   stack_.emplace_back(intToRawBinary(result));

   if (result) {
      txInEvalState_.pubKeyState_.emplace(pubkey, true);
   }
}

void StackInterpreter::op_checkmultisig()
{
   //stack needs to have at least m, n, output script
   if (stack_.size() < 3) {
      throw ScriptException("insufficient stack size for checkmultisig operation");
   }

   //pop n
   auto n = pop_back();
   auto nI = rawBinaryToInt(n);
   if (nI < 0 || nI > 20) {
      throw ScriptException("invalid n");
   }

   //pop pubkeys
   std::map<unsigned, BinaryData> pubkeys;
   for (unsigned i = 0; i < nI; i++) {
      auto pubkey = pop_back();
      if (Cryptography::ECDSA::verifyPublicKeyValid(pubkey)) {
         txInEvalState_.pubKeyState_.emplace(pubkey, false);
         pubkeys.emplace(i, std::move(pubkey));
      }
   }

   //pop m
   auto&& m = pop_back();
   auto mI = rawBinaryToInt(m);
   if (mI < 0 || mI > nI) {
      throw ScriptException("invalid m");
   }

   txInEvalState_.n_ = nI;
   txInEvalState_.m_ = mI;

   //pop sigs
   struct SigData
   {
      BinaryData sig;
      SIGHASH_TYPE hashType;
   };
   std::vector<SigData> sigVec;
   sigVec.reserve(stack_.size());

   while (!stack_.empty()) {
      auto sig = pop_back();
      if (sig.empty()) {
         break;
      }
      sigVec.emplace_back(SigData{
         sig.getSliceCopy(0, sig.getSize() - 1),
         getSigHashSingleByte(*(sig.getPtr() + sig.getSize() - 1))
      });
   }

   //check sigs
   std::map<SIGHASH_TYPE, BinaryData> dataToHash;

   //check sighashdata object
   if (sigHashDataObject_ == nullptr) {
      sigHashDataObject_ = std::make_shared<SigHashDataLegacy>();
   }

   unsigned validSigCount = 0;
   int index = nI - 1;
   for (unsigned i=0; i < sigVec.size(); i++) {
      const auto& sigD = sigVec[sigVec.size() - i - 1];

      //get data to hash
      auto& hashdata = dataToHash[sigD.hashType];
      if (hashdata.empty()) {
         hashdata = sigHashDataObject_->getDataForSigHash(
            sigD.hashType, *txStubPtr_, outputScriptRef_, inputIndex_);
      }

      //prepare sig
      auto rs = BtcUtils::extractRSFromDERSig(sigD.sig);

      //pop pubkeys from deque to verify against sig
      while (!pubkeys.empty()) {
         auto pubkey = pubkeys[index];
         pubkeys.erase(index--);

#ifdef SIGNER_DEBUG
         LOGWARN << "Verifying sig for: ";
         LOGWARN << "   pubkey: " << pubkey.second.toHexStr();

         auto msg_hash = BtcUtils::getHash256(hashdata);
         LOGWARN << "   message: " << hashdata.toHexStr();
#endif
         if (Cryptography::ECDSA::verifyData(hashdata, sigD.sig, pubkey)) {
            txInEvalState_.pubKeyState_[pubkey] = true;
            validSigCount++;
            break;
         }
      }
   }

   if (validSigCount >= mI) {
      op_true();
   } else {
      op_0();
   }
}

////////
void StackInterpreter::processOpCode(const OpCode& oc)
{
   ++opcount_;

   //handle push data by itself, doesn't play well with switch
   if (oc.opcode == 0) {
      op_0();
      return;
   }

   if (oc.opcode <= 75) {
      stack_.emplace_back(oc.dataRef);
      return;
   }

   if (oc.opcode < 79) {
      //op push data
      stack_.emplace_back(oc.dataRef);
      return;
   }

   if (oc.opcode == OP_1NEGATE) {
      op_1negate();
      return;
   }

   if (oc.opcode <= 96 && oc.opcode >= 81) {
      //op_1 - op_16
      uint8_t val = oc.opcode - 80;
      stack_.emplace_back(std::move(intToRawBinary(val)));
      return;
   }

   //If we got this far this op code is not push data. If this is the input
   //script, set the flag as per P2SH parsing rules (only push data in inputs)
   if (outputScriptRef_.empty()) {
      onlyPushDataInInput_ = false;
   }

   switch (oc.opcode)
   {
      case OP_NOP:
         break;

      case OP_IF:
      {
         BinaryRefReader brr(oc.dataRef);
         op_if(brr, false);
         break;
      }

      case OP_NOTIF:
      {
         op_not();
         BinaryRefReader brr(oc.dataRef);
         op_if(brr, false);
         break;
      }

      case OP_ELSE:
         //processed by opening if statement
         throw ScriptException("a wild else appears");

      case OP_ENDIF:
         //processed by opening if statement
         throw ScriptException("a wild endif appears");

      case OP_VERIFY:
         op_verify();
         break;

      case OP_TOALTSTACK:
         op_toaltstack();
         break;

      case OP_FROMALTSTACK:
         op_fromaltstack();
         break;

      case OP_IFDUP:
         op_ifdup();
         break;

      case OP_2DROP:
      {
         stack_.pop_back();
         stack_.pop_back();
         break;
      }

      case OP_2DUP:
         op_2dup();
         break;

      case OP_3DUP:
         op_3dup();
         break;

      case OP_2OVER:
         op_2over();
         break;

      case OP_DEPTH:
         op_depth();
         break;

      case OP_DROP:
         stack_.pop_back();
         break;

      case OP_DUP:
         op_dup();
         break;

      case OP_NIP:
         op_nip();
         break;

      case OP_OVER:
         op_over();
         break;

      case OP_PICK:
         op_pick();
         break;

      case OP_ROLL:
         op_roll();
         break;

      case OP_ROT:
         op_rot();
         break;

      case OP_SWAP:
         op_swap();
         break;

      case OP_TUCK:
         op_tuck();
         break;

      case OP_SIZE:
         op_size();
         break;

      case OP_EQUAL:
      {
         op_equal();
         if (onlyPushDataInInput_ && !p2shScript_.empty()) {
            //check the op_equal result
            op_verify();
            if (!isValid_) {
               break;
            }
            if (flags_ & SCRIPT_VERIFY_SEGWIT) {
               if (p2shScript_.getSize() == 22 ||
                  p2shScript_.getSize() == 34) {
                  auto versionByte = p2shScript_.getPtr();
                  if (*versionByte <= 16) {
                     processSW(p2shScript_);
                     return;
                  }
               }
            }
            processScript(p2shScript_, true);
         }
         break;
      }

      case OP_EQUALVERIFY:
      {
         op_equal();
         op_verify();
         break;
      }

      case OP_1ADD:
         op_1add();
         break;

      case OP_1SUB:
         op_1sub();
         break;

      case OP_NEGATE:
         op_negate();
         break;

      case OP_ABS:
         op_abs();
         break;

      case OP_NOT:
         op_not();
         break;

      case OP_0NOTEQUAL:
         op_0notequal();
         break;

      case OP_ADD:
         op_add();
         break;

      case OP_SUB:
         op_sub();
         break;

      case OP_BOOLAND:
         op_booland();
         break;

      case OP_BOOLOR:
         op_boolor();
         break;

      case OP_NUMEQUAL:
         op_numequal();
         break;

      case OP_NUMEQUALVERIFY:
      {
         op_numequal();
         op_verify();
         break;
      }

      case OP_NUMNOTEQUAL:
         op_numnotequal();
         break;

      case OP_LESSTHAN:
         op_lessthan();
         break;

      case OP_GREATERTHAN:
         op_greaterthan();
         break;

      case OP_LESSTHANOREQUAL:
         op_lessthanorequal();
         break;

      case OP_GREATERTHANOREQUAL:
         op_greaterthanorequal();
         break;

      case OP_MIN:
         op_min();
         break;

      case OP_MAX:
         op_max();
         break;

      case OP_WITHIN:
         op_within();
         break;

      case OP_RIPEMD160:
         op_ripemd160();
         break;

      case OP_SHA256:
      {
         //save the script if this output is a possible p2sh
         if (flags_ & SCRIPT_VERIFY_P2SH_SHA256) {
            if (opcount_ == 1 && onlyPushDataInInput_) {
               p2shScript_ = stack_back();
            }
         }
         op_sha256();
         break;
      }

      case OP_HASH160:
      {
         //save the script if this output is a possible p2sh
         if (flags_ & SCRIPT_VERIFY_P2SH) {
            if (opcount_ == 1 && onlyPushDataInInput_) {
               p2shScript_ = stack_back();
            }
         }
         op_hash160();
         break;
      }

      case OP_HASH256:
         op_hash256();
         break;

      case OP_CODESEPARATOR:
      {
         opcount_ = 0;
         if (outputScriptRef_.getSize() != 0) {
            txStubPtr_->setLastOpCodeSeparator(inputIndex_, oc.offset);
         }
         break;
      }

      case OP_CHECKSIG:
         op_checksig();
         break;

      case OP_CHECKSIGVERIFY:
      {
         op_checksig();
         op_verify();
         break;
      }

      case OP_CHECKMULTISIG:
         op_checkmultisig();
         break;

      case OP_CHECKMULTISIGVERIFY:
      {
         op_checkmultisig();
         op_verify();
         break;
      }

      case OP_NOP1:
         break;

      case OP_NOP2:
      {
         if (!(flags_ & SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY)) {
            break; // not enabled; treat as a NOP
         }
         //CLTV mechanics
         throw ScriptException("OP_CLTV not supported");
      }

      case OP_NOP3:
      {
         if (!(flags_ & SCRIPT_VERIFY_CHECKSEQUENCEVERIFY)) {
            break; // not enabled; treat as a NOP
         }
         //CSV mechanics
         throw ScriptException("OP_CSV not supported");
      }

      case OP_NOP4:
         break;

      case OP_NOP5:
         break;

      case OP_NOP6:
         break;

      case OP_NOP7:
         break;

      case OP_NOP8:
         break;

      case OP_NOP9:
         break;

      case OP_NOP10:
         break;

      default:
      {
         std::stringstream ss;
         ss << "unknown opcode: " << (unsigned)oc.opcode;
         throw std::runtime_error(ss.str());
      }
   }
}

SIGHASH_TYPE StackInterpreter::getSigHashSingleByte(uint8_t sighashbyte) const
{
   return SIGHASH_TYPE(sighashbyte);
}

////////
void StackInterpreter::processSW(BinaryDataRef outputScript)
{
   if (flags_ & SCRIPT_VERIFY_SEGWIT) {
      //set sig hash object to sw if it's missing
      sigHashDataObject_ = SHD_SW_;

      BinaryRefReader brr(outputScript);
      auto versionByte = brr.get_uint8_t();
      switch (versionByte)
      {
         case 0:
         {
            auto&& scriptSize = brr.get_uint8_t();
            auto&& scriptHash = brr.get_BinaryDataRef(scriptSize);

            if (brr.getSizeRemaining() > 0) {
               throw ScriptException("invalid v0 SW ouput size");
            }

            switch (scriptSize)
            {
               case 20:
               {
                  //P2WPKH
                  process_p2wpkh(scriptHash);
                  break;
               }

               case 32:
               {
                  //P2WSH
                  process_p2wsh(scriptHash);
                  break;
               }

               default:
                  throw ScriptException("invalid data size for version 0 SW");
            }
            break;
         }

         default:
            throw ScriptException("unsupported SW versions");
      }
   } else {
      throw ScriptException("not flagged for SW parsing");
   }
}

////////
void StackInterpreter::process_p2wpkh(const BinaryData& scriptHash)
{
   //get witness data
   auto witnessData = txStubPtr_->getWitnessData(inputIndex_);

   //prepare stack
   BinaryRefReader brr(witnessData);
   auto itemCount = brr.get_uint8_t();
   if (itemCount != 2) {
      throw ScriptException("v0 P2WPKH witness has to be 2 items");
   }
   for (unsigned i = 0; i < itemCount; i++) {
      auto len = brr.get_var_int();
      stack_.push_back(brr.get_BinaryData(len));
   }

   if (brr.getSizeRemaining() != 0) {
      throw ScriptException("witness size mismatch");
   }
   //construct output script
   auto swScript = BtcUtils::getP2WPKHWitnessScript(scriptHash);
   processScript(swScript, true);
}

void StackInterpreter::process_p2wsh(const BinaryData& scriptHash)
{
   //get witness data
   auto witnessData = txStubPtr_->getWitnessData(inputIndex_);
   BinaryData witBD(witnessData);

   //prepare stack
   BinaryRefReader brr(witnessData);
   auto itemCount = brr.get_uint8_t();
   for (unsigned i = 0; i < itemCount; i++) {
      auto len = brr.get_var_int();
      stack_.push_back(brr.get_BinaryData(len));
   }

   if (brr.getSizeRemaining() != 0) {
      throw ScriptException("witness size mismatch");
   }
   flags_ |= SCRIPT_VERIFY_P2SH_SHA256;

   //construct output script
   auto swScript = BtcUtils::getP2WSHWitnessScript(scriptHash);
   processScript(swScript, true);
}

////////
void StackInterpreter::checkState()
{
   if (!isValid_) {
      op_verify();
   }
   txInEvalState_.validStack_ = true;
}

////////////////////////////////////////////////////////////////////////////////
// ResolvedStack
bool ResolvedStack::isP2SH() const
{
   return isP2SH_;
}

void ResolvedStack::flagP2SH(bool flag)
{
   isP2SH_ = flag;
}

size_t ResolvedStack::stackSize() const
{
   return stack_.size();
}

std::shared_ptr<ResolvedStack> ResolvedStack::getWitnessStack() const
{
   return witnessStack_;
}

void ResolvedStack::setWitnessStack(std::shared_ptr<ResolvedStack> stack)
{
   witnessStack_ = stack;
}

void ResolvedStack::setStackData(std::vector<std::shared_ptr<StackItem>> stack)
{
   stack_.insert(stack_.end(), stack.begin(), stack.end());
}

const std::vector<std::shared_ptr<StackItem>>& ResolvedStack::getStack() const
{
   return stack_;
}

////////////////////////////////////////////////////////////////////////////////
// StackResolver
StackResolver::StackResolver(BinaryDataRef script,
   std::shared_ptr<ResolverFeed> feed) :
   script_(script), feed_(feed)
{}

StackResolver::~StackResolver()
{
   for (auto& stackEntry : stack_) {
      stackEntry->parent_ = nullptr;
      stackEntry->opcodes_.clear();
   }
}

unsigned StackResolver::getFlags() const
{
   return flags_;
}

void StackResolver::setFlags(unsigned flags)
{
   flags_ = flags;
}

std::shared_ptr<ResolverFeed> StackResolver::getFeed() const
{
   return feed_;
}

void StackResolver::processScript(BinaryRefReader& brr)
{
   while (brr.getSizeRemaining() != 0) {
      auto oc = getNextOpcode(brr);
      processOpCode(oc);
   }
}

std::shared_ptr<ReversedStackEntry> StackResolver::pop_back()
{
   std::shared_ptr<ReversedStackEntry> item;
   if (!stack_.empty()) {
      item = stack_.back();
      stack_.pop_back();
   } else {
      item = std::make_shared<ReversedStackEntry>();
   }
   return item;
}

std::shared_ptr<ReversedStackEntry> StackResolver::getTopStackEntryPtr()
{
   if (stack_.empty()) {
      stack_.emplace_back(std::make_shared<ReversedStackEntry>());
   }
   return stack_.back();
}

////////
void StackResolver::push_int(unsigned i)
{
   auto valBD = intToRawBinary(i);
   pushdata(valBD);
}

void StackResolver::pushdata(const BinaryData& data)
{
   auto rse = std::make_shared<ReversedStackEntry>(data);
   stack_.emplace_back(rse);
}

void StackResolver::op_dup()
{
   auto rsePtr = getTopStackEntryPtr();
   auto rseDup = std::make_shared<ReversedStackEntry>();
   rseDup->static_ = true;
   rseDup->parent_ = rsePtr;
   stack_.emplace_back(rseDup);
}

void StackResolver::push_op_code(const OpCode& oc)
{
   auto rsePtr = std::make_shared<ReversedStackEntry>();
   auto ocPtr = std::make_shared<OpCode>(oc);

   rsePtr->push_opcode(ocPtr);
   stack_.emplace_back(rsePtr);
}

void StackResolver::op_1item(const OpCode& oc)
{
   /***
   op_1item always preserves the item. 1 item operations only modify
   the existing item, they do not establish a relationship between several
   items, such operations should not reduce the stack depth.
   ***/

   auto ocPtr = std::make_shared<OpCode>(oc);
   auto item1 = getTopStackEntryPtr();
   item1->push_opcode(ocPtr);
   push_int(1);
}

void StackResolver::op_1item_verify(const OpCode& oc)
{
   op_1item(oc);
   pop_back();
}

void StackResolver::op_2items(const OpCode& oc)
{
   /***
   op_2items will always link 2 items. static items and references
   are culled.
   ***/

   auto item2 = pop_back();
   auto item1 = pop_back();

   if (item1->parent_ != item2) {
      auto eoc1 = std::make_shared<ExtendedOpCode>(oc);
      eoc1->itemIndex = 1;
      eoc1->referenceStackItemVec.emplace_back(item2);
      if (item1->push_opcode(eoc1)) {
         stack_.emplace_back(item1);
      }
   }

   if (item2->parent_ != item1) {
      auto eoc2 = std::make_shared<ExtendedOpCode>(oc);
      eoc2->itemIndex = 2;
      eoc2->referenceStackItemVec.emplace_back(item1);
      if (item2->push_opcode(eoc2)) {
         stack_.emplace_back(item2);
      }
   }
   push_int(1);
}

void StackResolver::op_2items_verify(const OpCode& oc)
{
   op_2items(oc);
   pop_back();
}

////////
void StackResolver::processOpCode(const OpCode& oc)
{
   if (oc.opcode >= 1 && oc.opcode <= 75) {
      pushdata(oc.dataRef);
      return;
   }

   if (oc.opcode >= 81 && oc.opcode <= 96) {
      unsigned val = oc.opcode - 80;
      push_int(val);
      return;
   }

   opCodeCount_++;
   switch (oc.opcode)
   {
      case OP_0:
         pushdata(BinaryData());
         break;

      case OP_PUSHDATA1:
      case OP_PUSHDATA2:
      case OP_PUSHDATA4:
         pushdata(oc.dataRef);
         break;

      case OP_DUP:
         op_dup();
         break;

      case OP_HASH160:
      case OP_SHA256:
      {
         opHash_ = true;
         op_1item_verify(oc);
         break;
      }

      case OP_RIPEMD160:
      case OP_HASH256:
         op_1item_verify(oc);
         break;

      case OP_EQUAL:
      {
         if (opCodeCount_ == 2 && opHash_) {
            isP2SH_ = true;
         }
         op_2items(oc);
         break;
      }

      case OP_CHECKSIG:
         op_2items(oc);
         break;

      case OP_EQUALVERIFY:
      case OP_CHECKSIGVERIFY:
         op_2items_verify(oc);
         break;

      case OP_CHECKMULTISIG:
      case OP_CHECKMULTISIGVERIFY:
         push_op_code(oc);
         break;

      default:
         throw ScriptException("opcode not implemented with reverse stack");
   }
}

////////
void StackResolver::resolveStack()
{
   unsigned static_count = 0;
   auto stackIter = stack_.rbegin();
   while (stackIter != stack_.rend()) {
      auto stackItem = *stackIter++;
      if (stackItem->static_) {
         static_count++;
         continue;
      }

      //resolve the stack item value by reverting the effect of the opcodes
      //it goes through
      auto opcodeIter = stackItem->opcodes_.begin();
      while (opcodeIter != stackItem->opcodes_.end()) {
         auto opcodePtr = *opcodeIter++;
         switch (opcodePtr->opcode)
         {
            case OP_EQUAL:
            case OP_EQUALVERIFY:
            {
               auto opcodeExPtr =
                  std::dynamic_pointer_cast<ExtendedOpCode>(opcodePtr);
               if (opcodeExPtr == nullptr ||
                  opcodeExPtr->referenceStackItemVec.size() != 1) {
                  throw ScriptException(
                     "invalid stack item reference count"
                     "for op_equal resolution");
               }

               const auto& stackItemRefPtr = opcodeExPtr->referenceStackItemVec[0];
               if (stackItem->resolvedValue_ == nullptr) {
                  if (stackItemRefPtr->static_) {
                     //references a static item, just copy the value
                     stackItem->resolvedValue_ =
                        std::make_shared<StackValue_Static>(
                           stackItemRefPtr->staticData_);
                  } else {
                     //references a dynamic item, point to it
                     stackItem->resolvedValue_ =
                        std::make_shared<StackValue_Reference>(stackItemRefPtr);
                  }
               } else {
                  auto vrPtr = std::dynamic_pointer_cast<StackValue_Reference>(
                     stackItem->resolvedValue_);
                  if (vrPtr != nullptr) {
                     vrPtr->valueReference_ = stackItemRefPtr;
                     break;
                  }

                  auto ffPtr = std::dynamic_pointer_cast<StackValue_FromFeed>(
                     stackItem->resolvedValue_);
                  if (ffPtr != nullptr) {
                     if (!stackItemRefPtr->static_) {
                        throw ScriptException(
                           "unexpected StackValue type in op_equal");
                     }
                     ffPtr->requestString_ = stackItemRefPtr->staticData_;
                     break;
                  }
                  throw ScriptException(
                     "unexpected StackValue type in op_equal");
               }

               break;
            }

            case OP_HASH160:
            case OP_HASH256:
            case OP_RIPEMD160:
            case OP_SHA256:
            {
               auto stackItemValPtr =
                  std::dynamic_pointer_cast<StackValue_Static>(
                     stackItem->resolvedValue_);
               if (stackItemValPtr != nullptr) {
                  stackItem->resolvedValue_ =
                     std::make_shared<StackValue_FromFeed>(
                        stackItemValPtr->value_);
               } else {
                  stackItem->resolvedValue_ =
                     std::make_shared<StackValue_FromFeed>(BinaryData{});
               }
               break;
            }

            case OP_CHECKSIG:
            case OP_CHECKSIGVERIFY:
            {
               auto opcodeExPtr = dynamic_cast<ExtendedOpCode*>(opcodePtr.get());
               if (opcodeExPtr == nullptr) {
                  throw ScriptException(
                     "expected extended op code entry "
                     "for op_checksig resolution");
               }

               //second item of checksigs are pubkeys, skip
               if (opcodeExPtr->itemIndex == 2) {
                  break;
               }

               if (opcodeExPtr->referenceStackItemVec.size() != 1) {
                  throw ScriptException(
                     "invalid stack item reference count "
                     "for op_checksig resolution");
               }

               //first items are always signatures
               //overwrite any stackvalue object
               const auto& refItem = opcodeExPtr->referenceStackItemVec[0];
               stackItem->resolvedValue_ = std::make_shared<StackValue_Sig>(
                  refItem);
               break;
            }

            case OP_CHECKMULTISIG:
            case OP_CHECKMULTISIGVERIFY:
            {
               auto getStackItem = [this, &stackIter]()->std::shared_ptr<ReversedStackEntry>
               {
                  if (stackIter == stack_.rend()) {
                     throw ScriptException("stack is too small for OP_CMS");
                  }

                  auto stack_item = *stackIter++;
                  if (!stack_item->static_) {
                     throw ScriptException("OP_CMS item is not static");
                  }
                  return stack_item;
               };

               auto n_item = getStackItem();
               auto n_item_val = rawBinaryToInt(n_item->staticData_);

               std::vector<BinaryData> pubKeyVec;
               for (unsigned y = 0; y < n_item_val; y++) {
                  auto pubkey = getStackItem();
                  pubKeyVec.emplace_back(pubkey->staticData_.getRef());
               }

               auto m_sig = getStackItem();
               auto m_sig_val = rawBinaryToInt(m_sig->staticData_);
               if (m_sig_val > n_item_val) {
                  throw ScriptException("OP_CMS m > n");
               }

               stackItem->resolvedValue_ =
                  std::make_shared<StackValue_Multisig>(script_);
               break;
            }

            default:
               throw ScriptException("no resolution rule for opcode");
            }
      }

      //fulfill resolution
      switch (stackItem->resolvedValue_->type())
      {
         case StackValueType::FromFeed:
         {
            //grab from feed
            if (feed_ == nullptr) {
               break;
            }
            auto fromFeed = std::dynamic_pointer_cast<StackValue_FromFeed>(
               stackItem->resolvedValue_);
            fromFeed->value_ = feed_->getByVal(fromFeed->requestString_);

            if (isP2SH_) {
               //if this output is flagged as p2sh, this value is the script
               //process that script and set the resolved stack
               StackResolver resolver(fromFeed->value_, feed_);
               resolver.setFlags(flags_);
               resolver.isSW_ = isSW_;

               auto stackptr = resolver.getResolvedStack();
               resolvedStack_ = stackptr;
            }
            break;
         }

         case StackValueType::Sig:
         {
            auto ref = std::dynamic_pointer_cast<StackValue_Sig>(
               stackItem->resolvedValue_);
            ref->script_ = script_;
            break;
         }

         case StackValueType::Multisig:
         {
            //nothing to do
            break;
         }

         case StackValueType::Reference:
         {
            //grab from reference
            auto ref = std::dynamic_pointer_cast<StackValue_Reference>(
               stackItem->resolvedValue_);
            ref->value_ = std::move(resolveReferenceValue(ref->valueReference_));
            break;
         }

         default:
            //nothing to do
            continue;
      }
   }

   if (flags_ & SCRIPT_VERIFY_SEGWIT) {
      if (static_count == 2 && stack_.size() == 2) {
         auto _stackIter = stack_.begin();
         auto firstStackItem = *_stackIter;
         auto header = rawBinaryToInt(firstStackItem->staticData_);

         if (header == 0) {
            ++_stackIter;
            auto secondStackItem = *_stackIter;

            BinaryData swScript;
            if (secondStackItem->staticData_.getSize() == 20) {
               //resolve P2WPKH script
               swScript = BtcUtils::getP2WPKHWitnessScript(
                  secondStackItem->staticData_);
            } else if (secondStackItem->staticData_.getSize() == 32) {
               //resolve P2WSH script
               swScript = BtcUtils::getP2WSHWitnessScript(
                  secondStackItem->staticData_);
               isP2SH_ = true;
            } else {
               throw ScriptException("invalid SW script format");
            }

            StackResolver resolver(swScript, feed_);
            resolver.setFlags(flags_);
            resolver.isSW_ = true;
            std::shared_ptr<ResolvedStack> stackptr;

            try {
               //failed SW should just result in an empty stack
               //instead of an actual throw
               stackptr = std::move(resolver.getResolvedStack());
            } catch (const std::exception&) {}

            if (resolvedStack_ == nullptr) {
               resolvedStack_ = std::make_shared<ResolvedStack>();
            }
            resolvedStack_->setWitnessStack(stackptr);
         }
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<ResolvedStack> StackResolver::getResolvedStack()
{
   BinaryRefReader brr(script_);
   processScript(brr);
   resolveStack();

   unsigned count = 0;
   if (resolvedStack_ != nullptr) {
      count = resolvedStack_->stackSize();
   }
   std::vector<std::shared_ptr<StackItem>> stackItemVec;

   for (const auto& stackItem : stack_) {
      if (stackItem->static_) {
         continue;
      }
      switch (stackItem->resolvedValue_->type())
      {
         case StackValueType::Static:
         {
            auto val = std::dynamic_pointer_cast<StackValue_Static>(
               stackItem->resolvedValue_);

            stackItemVec.emplace_back(
               std::make_shared<StackItem_PushData>(
                  count++, std::move(val->value_)
               ));
            break;
         }

         case StackValueType::FromFeed:
         {
            auto val = std::dynamic_pointer_cast<StackValue_FromFeed>(
               stackItem->resolvedValue_);

            stackItemVec.emplace_back(
               std::make_shared<StackItem_PushData>(
                  count++, std::move(val->value_)));
            break;
         }

         case StackValueType::Reference:
         {
            auto val = std::dynamic_pointer_cast<StackValue_Reference>(
               stackItem->resolvedValue_);

            stackItemVec.emplace_back(
               std::make_shared<StackItem_PushData>(
                  count++, std::move(val->value_)));
            break;
         }

         case StackValueType::Sig:
         {
            auto val = std::dynamic_pointer_cast<StackValue_Sig>(
               stackItem->resolvedValue_);

            auto pubkey = resolveReferenceValue(val->pubkeyRef_);
            stackItemVec.emplace_back(
               std::make_shared<StackItem_Sig>(
                  count++, pubkey, val->script_));
            break;
         }

         case StackValueType::Multisig:
         {
            auto msObj = std::dynamic_pointer_cast<StackValue_Multisig>(
               stackItem->resolvedValue_);

            //push lead 0 to cover for OP_CMS bug
            stackItemVec.emplace_back(
               std::make_shared<StackItem_OpCode>(count++, 0));

            auto stackitem_ms = std::make_shared<StackItem_MultiSig>(
               count++, msObj->script_);
            stackItemVec.emplace_back(stackitem_ms);
            break;
         }

         default:
            throw std::runtime_error("unexpected stack value type");
      }
   }

   if (resolvedStack_ == nullptr) {
      resolvedStack_ = std::make_shared<ResolvedStack>();
   }
   resolvedStack_->setStackData(std::move(stackItemVec));
   resolvedStack_->flagP2SH(isP2SH_);
   return resolvedStack_;
}
