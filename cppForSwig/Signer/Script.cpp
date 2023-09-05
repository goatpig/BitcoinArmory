////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Script.h"
#include "Transactions.h"
#include "Signer.h"

using namespace std;
using namespace Armory::Signer;

//dtors
StackValue::~StackValue()
{}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// StackItem
////////////////////////////////////////////////////////////////////////////////
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

   return pubkey_ == obj_cast->pubkey_ && script_ == obj_cast->script_;
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

////////////////////////////////////////////////////////////////////////////////
void StackItem_Sig::merge(const StackItem *obj)
{
   auto obj_cast = dynamic_cast<const StackItem_Sig*>(obj);
   if (obj_cast == nullptr)
      throw ScriptException("unexpected StackItem type");

   if (script_.empty())
      script_ = obj_cast->script_;
   else if (script_ != obj_cast->script_)
      throw ScriptException("sig item script mismatch");
   
   if (pubkey_.empty())
      pubkey_ = obj_cast->pubkey_;
   else if (pubkey_ != obj_cast->pubkey_)
      throw ScriptException("sig item pubkey mismatch");
}

////////////////////////////////////////////////////////////////////////////////
void StackItem_MultiSig::merge(const StackItem* obj)
{
   auto obj_cast = dynamic_cast<const StackItem_MultiSig*>(obj);
   if (obj_cast == nullptr)
      throw ScriptException("unexpected StackItem type");

   if (m_ != obj_cast->m_)
      throw ScriptException("m mismatch");

   sigs_.insert(obj_cast->sigs_.begin(), obj_cast->sigs_.end());
}

////////////////////////////////////////////////////////////////////////////////
void StackItem_PushData::serialize(
   Codec_SignerState::StackEntryState& protoMsg) const
{
   protoMsg.set_entry_type(Codec_SignerState::StackEntryState_Types::PushData);
   protoMsg.set_entry_id(id_);

   protoMsg.set_stackentry_data(data_.getPtr(), data_.getSize());
}

////////////////////////////////////////////////////////////////////////////////
void StackItem_Sig::serialize(
   Codec_SignerState::StackEntryState& protoMsg) const
{
   protoMsg.set_entry_type(Codec_SignerState::StackEntryState_Types::SingleSig);
   protoMsg.set_entry_id(id_);

   auto sigEntry = protoMsg.mutable_sig_data();
   sigEntry->set_pubkey(pubkey_.getPtr(), pubkey_.getSize());
   sigEntry->set_script(script_.getPtr(), script_.getSize());
}

////////////////////////////////////////////////////////////////////////////////
void StackItem_MultiSig::serialize(
   Codec_SignerState::StackEntryState& protoMsg) const
{
   protoMsg.set_entry_type(Codec_SignerState::StackEntryState_Types::MultiSig);
   protoMsg.set_entry_id(id_);
   
   auto stackEntry = protoMsg.mutable_multisig_data();
   stackEntry->set_script(script_.getPtr(), script_.getSize());

   for (auto& sig_pair : sigs_)
   {
      stackEntry->add_sig_index(sig_pair.first);
      stackEntry->add_sig_data(
         sig_pair.second.getPtr(), sig_pair.second.getSize());
   }
}

////////////////////////////////////////////////////////////////////////////////
void StackItem_OpCode::serialize(
   Codec_SignerState::StackEntryState& protoMsg) const
{
   protoMsg.set_entry_type(Codec_SignerState::StackEntryState_Types::OpCode);
   protoMsg.set_entry_id(id_);

   protoMsg.set_opcode(opcode_);
}

////////////////////////////////////////////////////////////////////////////////
void StackItem_SerializedScript::serialize(
   Codec_SignerState::StackEntryState& protoMsg) const
{
   protoMsg.set_entry_type(Codec_SignerState::StackEntryState_Types::Script);
   protoMsg.set_entry_id(id_);

   protoMsg.set_stackentry_data(data_.getPtr(), data_.getSize());
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<StackItem> StackItem::deserialize(
   const Codec_SignerState::StackEntryState& protoMsg)
{
   shared_ptr<StackItem> itemPtr;

   auto id = protoMsg.entry_id();

   switch (protoMsg.entry_type())
   {
   case Codec_SignerState::StackEntryState_Types::PushData:
   {
      if (!protoMsg.has_stackentry_data())
         throw runtime_error("missing push data field");

      auto&& data = BinaryDataRef::fromString(protoMsg.stackentry_data());
      itemPtr = make_shared<StackItem_PushData>(id, move(data));

      break;
   }

   case Codec_SignerState::StackEntryState_Types::SingleSig:
   {
      if (!protoMsg.has_sig_data())
         throw runtime_error("missing sig data field");

      const auto& sigData = protoMsg.sig_data();

      auto pubkey = BinaryData::fromString(sigData.pubkey());
      auto script = BinaryData::fromString(sigData.script());

      itemPtr = make_shared<StackItem_Sig>(id, pubkey, script);
      break;
   }

   case Codec_SignerState::StackEntryState_Types::MultiSig:
   {
      if (!protoMsg.has_multisig_data())
         throw runtime_error("missing multisig data field");

      const auto& msData = protoMsg.multisig_data();
      if (msData.sig_data_size() != msData.sig_index_size())
         throw runtime_error("multisig data mismatch");

      //script
      auto script = BinaryData::fromString(msData.script());

      //instantiate stack object
      auto itemMs = make_shared<StackItem_MultiSig>(id, script);

      //fill it with carried over sigs
      for (int i = 0; i < msData.sig_index_size(); i++)
      {
         auto pos = msData.sig_index(i);
         auto&& data = SecureBinaryData::fromString(msData.sig_data(i));

         itemMs->setSig(pos, data);
      }

      itemPtr = itemMs;
      break;
   }

   case Codec_SignerState::StackEntryState_Types::OpCode:
   {
      if (!protoMsg.has_opcode())
         throw runtime_error("missing opcode data field");
      
      auto opcode = protoMsg.opcode();
      itemPtr = make_shared<StackItem_OpCode>(id, opcode);

      break;
   }

   case Codec_SignerState::StackEntryState_Types::Script:
   {
      if (!protoMsg.has_stackentry_data())
         throw runtime_error("missing push data field");

      auto&& data = BinaryDataRef::fromString(protoMsg.stackentry_data());
      itemPtr = make_shared<StackItem_SerializedScript>(id, move(data));

      break;
   }

   default:
      throw runtime_error("unexpected stack item prefix");
   }

   return itemPtr;
}

////////////////////////////////////////////////////////////////////////////////
StackItem_MultiSig::StackItem_MultiSig(unsigned id, BinaryData& script) :
   StackItem(StackItemType_MultiSig, id), script_(std::move(script))
{
   m_ = BtcUtils::getMultisigPubKeyList(script_, pubkeyVec_);

   if (m_ < 1 || m_ >= 16)
      throw runtime_error("invalid m");

   if (pubkeyVec_.size() < m_)
      throw runtime_error("invalid pubkey count");
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// ScriptParser
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
OpCode ScriptParser::getNextOpcode(BinaryRefReader& brr) const
{
   OpCode val;
   val.offset_ = brr.getPosition();
   val.opcode_ = brr.get_uint8_t();
   if (val.opcode_ <= 75 && val.opcode_ > 0)
   {
      val.dataRef_ = brr.get_BinaryDataRef(val.opcode_);
   }
   else
   {
      unsigned len = 0;
      switch (val.opcode_)
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

      val.dataRef_ = brr.get_BinaryDataRef(len);
   }

   return val;
}

////////////////////////////////////////////////////////////////////////////////
size_t ScriptParser::seekToOpCode(BinaryRefReader& brr, OPCODETYPE opcode) const
{
   while (brr.getSizeRemaining() > 0)
   {
      auto&& oc = getNextOpcode(brr);
      if (oc.opcode_ == opcode)
         return brr.getPosition() - 1 - oc.dataRef_.getSize();
   }

   return brr.getPosition();
}

void ScriptParser::parseScript(BinaryRefReader& brr)
{
   while (brr.getSizeRemaining() != 0)
   {
      auto&& oc = getNextOpcode(brr);
      processOpCode(oc);
   }
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// StackInterpreter
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void StackInterpreter::processScript(
   const BinaryDataRef& script, bool isOutputScript)
{
   BinaryRefReader brr(script);
   processScript(brr, isOutputScript);
}

////////////////////////////////////////////////////////////////////////////////
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

////////////////////////////////////////////////////////////////////////////////
void StackInterpreter::processOpCode(const OpCode& oc)
{
   ++opcount_;

   //handle push data by itself, doesn't play well with switch
   if (oc.opcode_ == 0)
   {
      op_0();
      return;
   }

   if (oc.opcode_ <= 75)
   {
      stack_.push_back(oc.dataRef_);
      return;
   }

   if (oc.opcode_ < 79)
   {
      //op push data
      stack_.push_back(oc.dataRef_);
      return;
   }

   if (oc.opcode_ == OP_1NEGATE)
   {
      op_1negate();
      return;
   }

   if (oc.opcode_ <= 96 && oc.opcode_ >= 81)
   {
      //op_1 - op_16
      uint8_t val = oc.opcode_ - 80;
      stack_.push_back(move(intToRawBinary(val)));
      return;
   }

   //If we got this far this op code is not push data. If this is the input
   //script, set the flag as per P2SH parsing rules (only push data in inputs)
   if (outputScriptRef_.getSize() == 0)
      onlyPushDataInInput_ = false;

   switch (oc.opcode_)
   {
   case OP_NOP:
      break;

   case OP_IF:
   {
      BinaryRefReader brr(oc.dataRef_);
      op_if(brr, false);
      break;
   }

   case OP_NOTIF:
   {
      op_not();
      BinaryRefReader brr(oc.dataRef_);
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
      if (onlyPushDataInInput_ && p2shScript_.getSize() != 0)
      {
         //check the op_equal result
         op_verify();
         if (!isValid_)
            break;

         if (flags_ & SCRIPT_VERIFY_SEGWIT)
            if (p2shScript_.getSize() == 22 ||
               p2shScript_.getSize() == 34)
            {
               auto versionByte = p2shScript_.getPtr();
               if (*versionByte <= 16)
               {
                  processSW(p2shScript_);
                  return;
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
      if (flags_ & SCRIPT_VERIFY_P2SH_SHA256)
         if (opcount_ == 1 && onlyPushDataInInput_)
            p2shScript_ = stack_back();

      op_sha256();
      break;
   }

   case OP_HASH160:
   {
      //save the script if this output is a possible p2sh
      if (flags_ & SCRIPT_VERIFY_P2SH)
         if (opcount_ == 1 && onlyPushDataInInput_)
            p2shScript_ = stack_back();

      op_hash160();
      break;
   }

   case OP_HASH256:
      op_hash256();
      break;

   case OP_CODESEPARATOR:
   {
      opcount_ = 0;
      if (outputScriptRef_.getSize() != 0)
         txStubPtr_->setLastOpCodeSeparator(inputIndex_, oc.offset_);
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
   }

   case OP_NOP1:
      break;

   case OP_NOP2:
   {
      if (!(flags_ & SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY))
         break; // not enabled; treat as a NOP

      //CLTV mechanics
      throw ScriptException("OP_CLTV not supported");
   }

   case OP_NOP3:
   {
      if (!(flags_ & SCRIPT_VERIFY_CHECKSEQUENCEVERIFY))
         break; // not enabled; treat as a NOP

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
      stringstream ss;
      ss << "unknown opcode: " << (unsigned)oc.opcode_;
      throw runtime_error(ss.str());
   }
   }
}

////////////////////////////////////////////////////////////////////////////////
SIGHASH_TYPE StackInterpreter::getSigHashSingleByte(uint8_t sighashbyte) const
{
   return SIGHASH_TYPE(sighashbyte);
}

////////////////////////////////////////////////////////////////////////////////
void StackInterpreter::op_min(void)
{
   auto&& b = pop_back();
   auto&& a = pop_back();

   auto aI = rawBinaryToInt(a);
   auto bI = rawBinaryToInt(b);

   auto cI = min(aI, bI);
   stack_.push_back(move(intToRawBinary(cI)));
}

////////////////////////////////////////////////////////////////////////////////
void StackInterpreter::op_max(void)
{
   auto&& b = pop_back();
   auto&& a = pop_back();

   auto aI = rawBinaryToInt(a);
   auto bI = rawBinaryToInt(b);

   auto cI = max(aI, bI);
   stack_.push_back(move(intToRawBinary(cI)));
}

////////////////////////////////////////////////////////////////////////////////
void StackInterpreter::op_checksig()
{
   //pop sig and pubkey from the stack
   if (stack_.size() < 2)
      throw ScriptException("insufficient stack size for checksig operation");

   txInEvalState_.n_ = 1;
   txInEvalState_.m_ = 1;

   auto&& pubkey = pop_back();
   auto&& sigScript = pop_back();
   if (sigScript.getSize() < 65)
   {
      txInEvalState_.pubKeyState_.insert(make_pair(pubkey, false));
      stack_.push_back(move(intToRawBinary(false)));
      return;
   }

   //extract sig and sighash type
   BinaryRefReader brrSig(sigScript);
   auto sigsize = sigScript.getSize() - 1;
   auto sig = brrSig.get_BinaryDataRef(sigsize);
   auto hashType = getSigHashSingleByte(brrSig.get_uint8_t());

   //get data for sighash
   if (sigHashDataObject_ == nullptr)
      sigHashDataObject_ = make_shared<SigHashDataLegacy>();
   auto&& sighashdata =
      sigHashDataObject_->getDataForSigHash(hashType, *txStubPtr_,
      outputScriptRef_, inputIndex_);

   if(!CryptoECDSA().VerifyPublicKeyValid(pubkey))
      throw runtime_error("invalid pubkey");

   //check signature
   auto result = CryptoECDSA().VerifyData(sighashdata, sig, pubkey);
   stack_.push_back(move(intToRawBinary(result)));

   if (result)
      txInEvalState_.pubKeyState_.insert(make_pair(pubkey, true));
}

////////////////////////////////////////////////////////////////////////////////
void StackInterpreter::op_checkmultisig()
{
   //stack needs to have at least m, n, output script
   if (stack_.size() < 3)
      throw ScriptException("insufficient stack size for checkmultisig operation");

   //pop n
   auto&& n = pop_back();
   auto nI = rawBinaryToInt(n);
   if (nI < 0 || nI > 20)
      throw ScriptException("invalid n");

   //pop pubkeys
   map<unsigned, BinaryData> pubkeys;
   for (unsigned i = 0; i < nI; i++)
   {
      auto&& pubkey = pop_back();
      if(CryptoECDSA().VerifyPublicKeyValid(pubkey))
      {
         txInEvalState_.pubKeyState_.insert(make_pair(pubkey, false));
         pubkeys.insert(move(make_pair(i, pubkey)));        
      }
   }

   //pop m
   auto&& m = pop_back();
   auto mI = rawBinaryToInt(m);
   if (mI < 0 || mI > nI)
      throw ScriptException("invalid m");

   txInEvalState_.n_ = nI;
   txInEvalState_.m_ = mI;

   //pop sigs
   struct sigData
   {
      BinaryData sig_;
      SIGHASH_TYPE hashType_;
   };
   vector<sigData> sigVec;

   while (stack_.size() > 0)
   {
      auto&& sig = pop_back();
      if (sig.getSize() == 0)
         break;

      sigData sdata;

      sdata.sig_ = sig.getSliceCopy(0, sig.getSize() - 1);

      //grab hash type
      sdata.hashType_ = 
         getSigHashSingleByte(*(sig.getPtr() + sig.getSize() - 1));

      //push to vector
      sigVec.push_back(move(sdata));
   }

   //should have at least as many sigs as m
   /*if (sigVec.size() < mI)
      throw ScriptException("invalid sig count");*/

   //check sigs
   map<SIGHASH_TYPE, BinaryData> dataToHash;

   //check sighashdata object
   if (sigHashDataObject_ == nullptr)
      sigHashDataObject_ = make_shared<SigHashDataLegacy>();

   unsigned validSigCount = 0;
   int index = nI - 1;
   auto sigIter = sigVec.rbegin();
   while(sigIter != sigVec.rend())
   {
      auto& sigD = *sigIter++;

      //get data to hash
      auto& hashdata = dataToHash[sigD.hashType_];
      if (hashdata.getSize() == 0)
      {
         hashdata = sigHashDataObject_->getDataForSigHash(
            sigD.hashType_, *txStubPtr_, outputScriptRef_, inputIndex_);
      }

      //prepare sig
      auto&& rs = BtcUtils::extractRSFromDERSig(sigD.sig_);

      //pop pubkeys from deque to verify against sig
      while (pubkeys.size() > 0)
      {
         auto pubkey = pubkeys[index];
         pubkeys.erase(index--);

#ifdef SIGNER_DEBUG
         LOGWARN << "Verifying sig for: ";
         LOGWARN << "   pubkey: " << pubkey.second.toHexStr();

         auto&& msg_hash = BtcUtils::getHash256(hashdata);
         LOGWARN << "   message: " << hashdata.toHexStr();
#endif
         if(CryptoECDSA().VerifyData(hashdata, sigD.sig_, pubkey))
         {
            txInEvalState_.pubKeyState_[pubkey] = true;
            validSigCount++;
            break;
         }        
      }
   }

   if (validSigCount >= mI)
      op_true();
   else
      op_0();
}

////////////////////////////////////////////////////////////////////////////////
void StackInterpreter::processSW(BinaryDataRef outputScript)
{
   if (flags_ & SCRIPT_VERIFY_SEGWIT)
   {
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

         if (brr.getSizeRemaining() > 0)
            throw ScriptException("invalid v0 SW ouput size");

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
   }
   else
      throw ScriptException("not flagged for SW parsing");
}

////////////////////////////////////////////////////////////////////////////////
void StackInterpreter::checkState()
{
   if (!isValid_)
      op_verify();

   txInEvalState_.validStack_ = true;
}

////////////////////////////////////////////////////////////////////////////////
void StackInterpreter::process_p2wpkh(const BinaryData& scriptHash)
{
   //get witness data
   auto witnessData = txStubPtr_->getWitnessData(inputIndex_);

   //prepare stack
   BinaryRefReader brr(witnessData);
   auto itemCount = brr.get_uint8_t();
   if (itemCount != 2)
      throw ScriptException("v0 P2WPKH witness has to be 2 items");

   for (unsigned i = 0; i < itemCount; i++)
   {
      auto len = brr.get_var_int();
      stack_.push_back(brr.get_BinaryData(len));
   }
   
   if (brr.getSizeRemaining() != 0)
      throw ScriptException("witness size mismatch");

   //construct output script
   auto&& swScript = BtcUtils::getP2WPKHWitnessScript(scriptHash);
   processScript(swScript, true);
}

////////////////////////////////////////////////////////////////////////////////
void StackInterpreter::process_p2wsh(const BinaryData& scriptHash)
{
   //get witness data
   auto witnessData = txStubPtr_->getWitnessData(inputIndex_);
   BinaryData witBD(witnessData);

   //prepare stack
   BinaryRefReader brr(witnessData);
   auto itemCount = brr.get_uint8_t();
   
   for (unsigned i = 0; i < itemCount; i++)
   {
      auto len = brr.get_var_int();
      stack_.push_back(brr.get_BinaryData(len));
   }

   if (brr.getSizeRemaining() != 0)
      throw ScriptException("witness size mismatch");

   flags_ |= SCRIPT_VERIFY_P2SH_SHA256;

   //construct output script
   auto&& swScript = BtcUtils::getP2WSHWitnessScript(scriptHash);
   processScript(swScript, true);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// ReversedStackInterpreter
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void StackResolver::processScript(BinaryRefReader& brr)
{
   while (brr.getSizeRemaining() != 0)
   {
      auto&& oc = getNextOpcode(brr);
      processOpCode(oc);
   }
}

////////////////////////////////////////////////////////////////////////////////
void StackResolver::processOpCode(const OpCode& oc)
{
   if (oc.opcode_ >= 1 && oc.opcode_ <= 75)
   {
      pushdata(oc.dataRef_);
      return;
   }

   if (oc.opcode_ >= 81 && oc.opcode_ <= 96)
   {
      unsigned val = oc.opcode_ - 80;
      push_int(val);
      return;
   }

   opCodeCount_++;
   switch (oc.opcode_)
   {
   case OP_0:
      pushdata(BinaryData());
      break;

   case OP_PUSHDATA1:
   case OP_PUSHDATA2:
   case OP_PUSHDATA4:
      pushdata(oc.dataRef_);
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
      if (opCodeCount_ == 2 && opHash_)
         isP2SH_ = true;
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

////////////////////////////////////////////////////////////////////////////////
BinaryData resolveReferenceValue(shared_ptr<ReversedStackEntry> inPtr)
{
   auto currentPtr = inPtr;
   while (true)
   {
      if (currentPtr->parent_ != nullptr)
      {
         currentPtr = currentPtr->parent_;
      }
      else if (currentPtr->static_)
      {
         return currentPtr->staticData_;
      }
      else
      {
         switch (currentPtr->resolvedValue_->type())
         {
         case StackValueType_Static:
         {
            auto staticVal = dynamic_pointer_cast<StackValue_Static>(
               currentPtr->resolvedValue_);

            return staticVal->value_;
         }

         case StackValueType_FromFeed:
         {
            auto feedVal = dynamic_pointer_cast<StackValue_FromFeed>(
               currentPtr->resolvedValue_);

            return feedVal->value_;
         }

         case StackValueType_Reference:
         {
            auto refVal = dynamic_pointer_cast<StackValue_Reference>(
               currentPtr->resolvedValue_);

            currentPtr = refVal->valueReference_;
            break;
         }

         default:
            throw ScriptException("unexpected StackValue type \
               during reference resolution");
         }
      }

      if (currentPtr == inPtr)
         throw ScriptException("infinite loop in reference resolution");
   }
}

////////////////////////////////////////////////////////////////////////////////
void StackResolver::resolveStack()
{
   unsigned static_count = 0;

   auto stackIter = stack_.rbegin();
   while (stackIter != stack_.rend())
   {
      auto stackItem = *stackIter++;

      if (stackItem->static_)
      {
         static_count++;
         continue;
      }

      //resolve the stack item value by reverting the effect of the opcodes 
      //it goes through
      auto opcodeIter = stackItem->opcodes_.begin();
      while (opcodeIter != stackItem->opcodes_.end())
      {
         auto opcodePtr = *opcodeIter++;
         switch (opcodePtr->opcode_)
         {

         case OP_EQUAL:
         case OP_EQUALVERIFY:
         {
            auto opcodeExPtr = dynamic_pointer_cast<ExtendedOpCode>(opcodePtr);
            if (opcodeExPtr == nullptr || 
               opcodeExPtr->referenceStackItemVec_.size() != 1)
            {
               throw ScriptException(
                  "invalid stack item reference count for op_equal resolution");
            }

            auto& stackItemRefPtr = opcodeExPtr->referenceStackItemVec_[0];

            if (stackItem->resolvedValue_ == nullptr)
            {
               if (stackItemRefPtr->static_)
               {
                  //references a static item, just copy the value
                  stackItem->resolvedValue_ =
                     make_shared<StackValue_Static>(stackItemRefPtr->staticData_);
               }
               else
               {
                  //references a dynamic item, point to it
                  stackItem->resolvedValue_ =
                     make_shared<StackValue_Reference>(stackItemRefPtr);
               }
            }
            else
            {
               auto vrPtr = dynamic_pointer_cast<StackValue_Reference>(
                  stackItem->resolvedValue_);
               if (vrPtr != nullptr)
               {
                  vrPtr->valueReference_ = stackItemRefPtr;
                  break;
               }

               auto ffPtr = dynamic_pointer_cast<StackValue_FromFeed>(
                  stackItem->resolvedValue_);
               if (ffPtr != nullptr)
               {
                  if (!stackItemRefPtr->static_)
                     throw ScriptException("unexpected StackValue type in op_equal");
                  ffPtr->requestString_ = stackItemRefPtr->staticData_;
                  break;
               }

               throw ScriptException("unexpected StackValue type in op_equal");
            }

            break;
         }

         case OP_HASH160:
         case OP_HASH256:
         case OP_RIPEMD160:
         case OP_SHA256:
         {
            auto stackItemValPtr =
               dynamic_pointer_cast<StackValue_Static>(
               stackItem->resolvedValue_);
            if (stackItemValPtr != nullptr)
            {
               stackItem->resolvedValue_ =
                  make_shared<StackValue_FromFeed>(
                  stackItemValPtr->value_);
            }
            else
            {
               stackItem->resolvedValue_ =
                  make_shared<StackValue_FromFeed>(
                  BinaryData());
            }

            break;
         }

         case OP_CHECKSIG:
         case OP_CHECKSIGVERIFY:
         {
            auto opcodeExPtr = dynamic_cast<ExtendedOpCode*>(opcodePtr.get());
            if (opcodeExPtr == nullptr)
            {
               throw ScriptException(
               "expected extended op code entry for op_checksig resolution");
            }

            //second item of checksigs are pubkeys, skip
            if (opcodeExPtr->itemIndex_ == 2)
               break;

            if (opcodeExPtr->referenceStackItemVec_.size() != 1)
               throw ScriptException(
               "invalid stack item reference count for op_checksig resolution");

            //first items are always signatures, overwrite any stackvalue object
            auto& refItem = opcodeExPtr->referenceStackItemVec_[0];
            stackItem->resolvedValue_ =
               make_shared<StackValue_Sig>(refItem);

            break;
         }

         case OP_CHECKMULTISIG:
         case OP_CHECKMULTISIGVERIFY:
         {
            auto getStackItem = [&, this](void)->shared_ptr<ReversedStackEntry>
            {
               if (stackIter == stack_.rend())
                  throw ScriptException("stack is too small for OP_CMS");
               
               auto stack_item = *stackIter++;
               if (!stack_item->static_)
                  throw ScriptException("OP_CMS item is not static");

               return stack_item;
            };

            auto n_item = getStackItem();
            auto n_item_val = rawBinaryToInt(n_item->staticData_);

            vector<BinaryData> pubKeyVec;
            for (unsigned y = 0; y < n_item_val; y++)
            {
               auto pubkey = getStackItem();
               pubKeyVec.emplace_back(pubkey->staticData_.getRef());
            }

            auto m_sig = getStackItem();
            auto m_sig_val = rawBinaryToInt(m_sig->staticData_);

            if (m_sig_val > n_item_val)
               throw ScriptException("OP_CMS m > n");

            stackItem->resolvedValue_ = 
               make_shared<StackValue_Multisig>(script_);

            break;
         }

         default:
            throw ScriptException("no resolution rule for opcode");
         }
      }
         
      //fulfill resolution
      switch (stackItem->resolvedValue_->type())
      {
      case StackValueType_FromFeed:
      {
         //grab from feed
         if (feed_ == nullptr)
            break;

         auto fromFeed = dynamic_pointer_cast<StackValue_FromFeed>(
            stackItem->resolvedValue_);
         fromFeed->value_ = feed_->getByVal(fromFeed->requestString_);

         if (isP2SH_)
         {
            //if this output is flagged as p2sh, this value is the script
            //process that script and set the resolved stack
            StackResolver resolver(fromFeed->value_, feed_);
            resolver.setFlags(flags_);
            resolver.isSW_ = isSW_;

            auto stackptr = move(resolver.getResolvedStack());
            resolvedStack_ = stackptr;
         }

         break;
      }

      case StackValueType_Sig:
      {
         auto ref = dynamic_pointer_cast<StackValue_Sig>(
            stackItem->resolvedValue_);
         ref->script_ = script_;
         break;
      }

      case StackValueType_Multisig:
      {
         //nothing to do
         break;
      }

      case StackValueType_Reference:
      {
         //grab from reference
         auto ref = dynamic_pointer_cast<StackValue_Reference>(
            stackItem->resolvedValue_);
         ref->value_ = move(resolveReferenceValue(ref->valueReference_));
         break;
      }

      default:
         //nothing to do
         continue;
      }
   }

   if (flags_ & SCRIPT_VERIFY_SEGWIT)
   {
      if (static_count == 2 && stack_.size() == 2)
      {
         auto _stackIter = stack_.begin();
         auto firstStackItem = *_stackIter;

         auto header = rawBinaryToInt(firstStackItem->staticData_);

         if (header == 0)
         {
            ++_stackIter;
            auto secondStackItem = *_stackIter;

            BinaryData swScript;

            if (secondStackItem->staticData_.getSize() == 20)
            {
               //resolve P2WPKH script
               swScript =
                  BtcUtils::getP2WPKHWitnessScript(secondStackItem->staticData_);
            }
            else if (secondStackItem->staticData_.getSize() == 32)
            {
               //resolve P2WSH script
               swScript =
                  BtcUtils::getP2WSHWitnessScript(secondStackItem->staticData_);
               isP2SH_ = true;
            }
            else
            {
               throw ScriptException("invalid SW script format");
            }
            
            StackResolver resolver(swScript, feed_);
            resolver.setFlags(flags_);
            resolver.isSW_ = true;

            shared_ptr<ResolvedStack> stackptr;
            
            try
            {
               //failed SW should just result in an empty stack instead of an actual throw
               stackptr = move(resolver.getResolvedStack());
            }
            catch (exception&)
            { }

            if (resolvedStack_ == nullptr)
               resolvedStack_ = make_shared<ResolvedStack>();
            resolvedStack_->setWitnessStack(stackptr);
         }
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
shared_ptr<ResolvedStack> StackResolver::getResolvedStack()
{
   BinaryRefReader brr(script_);
   processScript(brr);
   resolveStack();

   unsigned count = 0;
   if (resolvedStack_ != nullptr)
      count = resolvedStack_->stackSize();

   vector<shared_ptr<StackItem>> stackItemVec;

   for (auto& stackItem : stack_)
   {
      if (stackItem->static_)
         continue;

      switch (stackItem->resolvedValue_->type())
      {
      case StackValueType_Static:
      {
         auto val = dynamic_pointer_cast<StackValue_Static>(
            stackItem->resolvedValue_);

         stackItemVec.push_back(
            make_shared<StackItem_PushData>(
               count++, move(val->value_)));
         break;
      }

      case StackValueType_FromFeed:
      {
         auto val = dynamic_pointer_cast<StackValue_FromFeed>(
            stackItem->resolvedValue_);

         stackItemVec.push_back(
            make_shared<StackItem_PushData>(
               count++, move(val->value_)));
         break;
      }

      case StackValueType_Reference:
      {
         auto val = dynamic_pointer_cast<StackValue_Reference>(
            stackItem->resolvedValue_);

         stackItemVec.push_back(
            make_shared<StackItem_PushData>(
               count++, move(val->value_)));
         break;
      }

      case StackValueType_Sig:
      {
         auto val = dynamic_pointer_cast<StackValue_Sig>(
            stackItem->resolvedValue_);

         auto pubkey = resolveReferenceValue(val->pubkeyRef_);
         stackItemVec.push_back(
            make_shared<StackItem_Sig>(
               count++, pubkey, val->script_));

         break;
      }

      case StackValueType_Multisig:
      {
         auto msObj = dynamic_pointer_cast<StackValue_Multisig>(
            stackItem->resolvedValue_);

         //push lead 0 to cover for OP_CMS bug
         stackItemVec.push_back(
            make_shared<StackItem_OpCode>(count++, 0));

         auto stackitem_ms = 
            make_shared<StackItem_MultiSig>(count++, msObj->script_);
         stackItemVec.push_back(stackitem_ms);
         
         break;
      }

      default:
         throw runtime_error("unexpected stack value type");
      }
   }

   if (resolvedStack_ == nullptr)
      resolvedStack_ = make_shared<ResolvedStack>();
   
   resolvedStack_->setStackData(move(stackItemVec));
   resolvedStack_->flagP2SH(isP2SH_);

   return resolvedStack_;
}
