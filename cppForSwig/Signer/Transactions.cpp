////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Transactions.h"

using namespace std;
using namespace Armory::Signer;

////////////////////////////////////////////////////////////////////////////////
TransactionStub::~TransactionStub(void)
{}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// TransactionVerifier
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool Armory::Signer::TransactionVerifier::verify(bool noCatch, bool strict) const
{
   if (strict)
   {
      //check value in vs value out
      if (checkOutputs() == UINT64_MAX)
         return false;
   }

   //check signatures
   if (!noCatch)
      checkSigs();
   else
      checkSigs_NoCatch();

   return txEvalState_.isValid();
}

////////////////////////////////////////////////////////////////////////////////
TxEvalState Armory::Signer::TransactionVerifier::evaluateState(bool strict) const
{
   /*
   Strict checks verify spend value as well but require the full supporting
   utxo map. On by default.
   */
   verify(false, strict);

   return txEvalState_;
}

////////////////////////////////////////////////////////////////////////////////
uint64_t Armory::Signer::TransactionVerifier::checkOutputs() const
{
   /*check values and return fee, return UINT64_MAX on failure*/
   
   //tally spendVal
   uint64_t spendVal = 0;
   for (auto& txout : theTx_.txouts_)
   {
      //memcpy should TBAA optimized by compiler
      uint64_t val;
      memcpy(&val, theTx_.data_ + txout.first, sizeof(uint64_t));
      spendVal += val;
   }

   //tally input val
   uint64_t inputVal = 0;
   for (auto& txin : theTx_.txins_)
   {
      //grab outpoint hash
      BinaryDataRef opHashRef(theTx_.data_ + txin.first, 32);

      //look for the utxo's hash
      auto hashIter = utxos_.find(opHashRef);
      if (hashIter == utxos_.end())
         throw runtime_error("cannot verify tx cause a utxo is missing");

      //grab outpoint id, should be TBAA optimized
      uint32_t opId;
      memcpy(&opId, theTx_.data_ + txin.first + 32, sizeof(uint32_t));

      //look for this id amoung the utxos matching the tx hash
      auto idIter = hashIter->second.find(opId);
      if (idIter == hashIter->second.end())
         throw runtime_error("cannot verify tx cause a utxo is missing");

      inputVal += idIter->second.getValue();
   }

   if (inputVal < spendVal)
      return UINT64_MAX;

   return inputVal - spendVal;
}

////////////////////////////////////////////////////////////////////////////////
void Armory::Signer::TransactionVerifier::checkSigs() const
{
   txEvalState_.reset();

   for (unsigned i = 0; i < theTx_.txins_.size(); i++)
   {
      auto stack_ptr = getStackInterpreter(i);
      try
      {
         checkSig(i, stack_ptr.get());
      }
      catch (exception&)
      {}

      txEvalState_.updateState(i, stack_ptr->getTxInEvalState());
   }
}

////////////////////////////////////////////////////////////////////////////////
void Armory::Signer::TransactionVerifier::checkSigs_NoCatch() const
{
   txEvalState_.reset();

   for (unsigned i = 0; i < theTx_.txins_.size(); i++)
   {
      auto&& state = checkSig(i);
      txEvalState_.updateState(i, state);
   }
}

////////////////////////////////////////////////////////////////////////////////
unique_ptr<Armory::Signer::StackInterpreter>
Armory::Signer::TransactionVerifier::getStackInterpreter(unsigned inputid) const
{
   auto sstack = make_unique<Armory::Signer::StackInterpreter>(this, inputid);
   auto flags = sstack->getFlags();
   flags |= flags_;
   sstack->setFlags(flags);
   return sstack;
}

////////////////////////////////////////////////////////////////////////////////
TxInEvalState Armory::Signer::TransactionVerifier::checkSig(unsigned inputId,
   Armory::Signer::StackInterpreter* sstack_ptr) const
{
   //grab the uxto
   auto&& input = theTx_.getTxInRef(inputId);
   if (input.getSize() < 41)
      throw Armory::Signer::ScriptException("unexpected txin size");

   //grab input script
   BinaryRefReader inputBrr(input);
   auto&& txHashRef = inputBrr.get_BinaryDataRef(32);
   auto outputId = inputBrr.get_uint32_t();
   auto scriptSize = inputBrr.get_var_int();
   auto&& inputScript = inputBrr.get_BinaryDataRef(scriptSize);

   auto utxoIter = utxos_.find(txHashRef);
   if (utxoIter == utxos_.end())
      return TxInEvalState();

   auto& idMap = utxoIter->second;
   auto idIter = idMap.find(outputId);
   if (idIter == idMap.end())
      return TxInEvalState();

   //grab output script
   auto& utxo = idIter->second;
   auto& outputScript = utxo.getScript();

   //init stack
   unique_ptr<Armory::Signer::StackInterpreter> sstack;
   auto stackPtr = sstack_ptr;
   if (stackPtr == nullptr)
   {
      sstack = move(getStackInterpreter(inputId));
      stackPtr = sstack.get();
   }

   if (theTx_.usesWitness_)
   {
      //reuse the sighash data object with segwit tx to leverage the pre state
      if (sigHashDataObject_ == nullptr)
         sigHashDataObject_ = make_shared<SigHashDataSegWit>();

      stackPtr->setSegWitSigHashDataObject(sigHashDataObject_);
   }

   if ((flags_ & SCRIPT_VERIFY_SEGWIT) &&
      inputScript.getSize() == 0)
   {
      stackPtr->processSW(outputScript);
   }
   else
   {
      stackPtr->processScript(inputScript, false);
      stackPtr->processScript(outputScript, true);
   }

   stackPtr->checkState();

   return stackPtr->getTxInEvalState();
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef Armory::Signer::TransactionVerifier::getSerializedOutputScripts(void) const
{
   auto txOutCount = theTx_.txouts_.size();
   auto firstTxOutOffset = theTx_.txouts_[0].first;
   auto lastTxOutOffset = theTx_.txouts_[txOutCount - 1].first +
      theTx_.txouts_[txOutCount - 1].second;
   auto txOutsLen = lastTxOutOffset - firstTxOutOffset;

   return BinaryDataRef(theTx_.data_ + firstTxOutOffset, txOutsLen);
}

////////////////////////////////////////////////////////////////////////////////
vector<TxInData> Armory::Signer::TransactionVerifier::getTxInsData(void) const
{
   vector<TxInData> datavec;

   auto txInCount = theTx_.txins_.size();
   for (unsigned i = 0; i < txInCount; i++)
   {
      auto&& txinref = theTx_.getTxInRef(i);

      TxInData data;
      data.outputHash_ = txinref.getSliceRef(0, 32);

      memcpy(&data.outputIndex_,
         txinref.getPtr() + 32,
         sizeof(uint32_t));

      memcpy(&data.sequence_,
         txinref.getPtr() + txinref.getSize() - 4,
         sizeof(uint32_t));

      datavec.push_back(move(data));
   }

   return datavec;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Armory::Signer::TransactionVerifier::getSubScript(unsigned index) const
{
   auto&& txinref = theTx_.getTxInRef(index);
   auto&& outputHash = txinref.getSliceRef(0, 32);
   auto outputIndex = *(uint32_t*)(txinref.getPtr() + 32);

   auto utxoIter = utxos_.find(outputHash);
   if (utxoIter == utxos_.end())
      throw runtime_error("unknown outpoint");

   auto indexIter = utxoIter->second.find(outputIndex);
   if (indexIter == utxoIter->second.end())
      throw runtime_error("unknown outpoint");

   auto csOffset = getLastCodeSeparatorOffset(index);
   if (csOffset == 0)
      return indexIter->second.getScript();

   auto& pkScript = indexIter->second.getScript();
   auto len = pkScript.getSize() - csOffset;
   return pkScript.getSliceRef(csOffset, len);
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef Armory::Signer::TransactionVerifier::getWitnessData(unsigned inputId) const
{
   if (inputId >= theTx_.witnesses_.size())
      throw runtime_error("invalid witness data id");

   auto& witOffsetAndSize = theTx_.witnesses_[inputId];
   return BinaryDataRef(theTx_.data_ + witOffsetAndSize.first, 
      witOffsetAndSize.second);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Armory::Signer::TransactionVerifier::serializeAllOutpoints() const
{
   BinaryWriter bw;
   for (unsigned i = 0; i < theTx_.txins_.size(); i++)
      bw.put_BinaryDataRef(getOutpoint(i));

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Armory::Signer::TransactionVerifier::serializeAllSequences() const
{
   BinaryWriter bw;
   for (auto& txinOnS : theTx_.txins_)
   {
      auto sequenceOffset = txinOnS.first + txinOnS.second - 4;
      BinaryDataRef bdr(theTx_.data_ + sequenceOffset, 4);

      bw.put_BinaryDataRef(bdr);
   }

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef Armory::Signer::TransactionVerifier::getOutpoint(unsigned inputID) const
{
   if (inputID >= theTx_.txins_.size())
      throw runtime_error("invalid txin index");

   auto& inputOnS = theTx_.txins_[inputID];

   return BinaryDataRef(theTx_.data_ + inputOnS.first, 36);
}

////////////////////////////////////////////////////////////////////////////////
uint64_t Armory::Signer::TransactionVerifier::getOutpointValue(unsigned inputID) const
{
   auto outpoint = getOutpoint(inputID);

   auto&& outputHash = outpoint.getSliceRef(0, 32);
   uint32_t outputIndex;
   memcpy(&outputIndex, outpoint.getPtr() + 32, sizeof(uint32_t));

   auto utxoIter = utxos_.find(outputHash);
   if (utxoIter == utxos_.end())
      throw runtime_error("unknown outpoint");

   auto indexIter = utxoIter->second.find(outputIndex);
   if (indexIter == utxoIter->second.end())
      throw runtime_error("unknown outpoint");

   return indexIter->second.getValue();
}

////////////////////////////////////////////////////////////////////////////////
unsigned Armory::Signer::TransactionVerifier::getTxInSequence(unsigned inputID) const
{
   if (inputID >= theTx_.txins_.size())
      throw Armory::Signer::ScriptException("invalid txin index");

   auto& inputOnS = theTx_.txins_[inputID];
   auto sequenceOffset = inputOnS.first + inputOnS.second - 4;

   uint32_t sequence;
   memcpy(&sequence, theTx_.data_ + sequenceOffset, sizeof(uint32_t));
   return sequence;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// SigHashData
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
BinaryData SigHashData::getDataForSigHash(SIGHASH_TYPE hashType, const
   TransactionStub& stub, BinaryDataRef subScript, unsigned inputIndex)
{
   switch (hashType)
   {
   case SIGHASH_ALL:
      return getDataForSigHashAll(stub, subScript, inputIndex);

   default:
      LOGERR << "unknown sighash type: " << (int)hashType;
      throw UnsupportedSigHashTypeException("unhandled sighash type");
   }
}

////////////////////////////////////////////////////////////////////////////////
vector<BinaryDataRef> SigHashData::tokenize(
   const BinaryData& data, uint8_t token)
{
   vector<BinaryDataRef> tokens;

   BinaryRefReader brr(data.getRef());
   size_t start = 0;
   Armory::Signer::StackInterpreter ss;
   
   while (brr.getSizeRemaining())
   {
      auto offset = ss.seekToOpCode(brr, (OPCODETYPE)token);
      auto len = offset - start;

      BinaryDataRef bdr(data.getPtr() + start, len);
      tokens.push_back(move(bdr));

      start = brr.getPosition();
   }

   return tokens;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData SigHashDataLegacy::getDataForSigHashAll(const TransactionStub& stub, 
   BinaryDataRef subScript, unsigned inputIndex)
{
   //grab subscript
   auto lastCSoffset = stub.getLastCodeSeparatorOffset(inputIndex);
   auto subScriptLen = subScript.getSize() - lastCSoffset;
   auto&& presubscript = subScript.getSliceRef(lastCSoffset, subScriptLen);

   //tokenize op_cs chunks
   auto&& tokens = tokenize(presubscript, OP_CODESEPARATOR);

   BinaryData subscript;
   if (tokens.size() == 1)
   {
      subscript = move(presubscript);
   }
   else
   {
      for (auto& token : tokens)
      {
         subscript.append(token);
      }
   }

   //isolate outputs
   auto&& serializedOutputs = stub.getSerializedOutputScripts();

   //isolate inputs
   auto&& txinsData = stub.getTxInsData();
   auto txin_count = txinsData.size();
   BinaryWriter strippedTxins;

   for (unsigned i=0; i < txin_count; i++)
   {
      strippedTxins.put_BinaryData(txinsData[i].outputHash_);
      strippedTxins.put_uint32_t(txinsData[i].outputIndex_);

      if (inputIndex != i)
      {
         //put empty varint
         strippedTxins.put_var_int(0);

         //and sequence
         strippedTxins.put_uint32_t(txinsData[i].sequence_);
      }
      else
      {
         //scriptsig
         strippedTxins.put_var_int(subscript.getSize());
         strippedTxins.put_BinaryData(subscript);
         
         //sequence
         strippedTxins.put_uint32_t(txinsData[i].sequence_);
      }
   }

   //wrap it up
   BinaryWriter scriptSigData;

   //version
   scriptSigData.put_uint32_t(stub.getVersion());

   //txin count
   scriptSigData.put_var_int(txin_count);

   //txins
   scriptSigData.put_BinaryData(strippedTxins.getData());

   //txout count
   scriptSigData.put_var_int(stub.getTxOutCount());

   //txouts
   scriptSigData.put_BinaryData(serializedOutputs);

   //locktime
   scriptSigData.put_uint32_t(stub.getLockTime());

   //sighashall
   scriptSigData.put_uint32_t(1);

   return BinaryData(scriptSigData.getData());
}

////////////////////////////////////////////////////////////////////////////////
BinaryData SigHashDataSegWit::getDataForSigHashAll(const TransactionStub& stub,
   BinaryDataRef subScript, unsigned inputIndex)
{
   //grab subscript
   auto lastCSoffset = stub.getLastCodeSeparatorOffset(inputIndex);
   auto subScriptLen = subScript.getSize() - lastCSoffset;
   auto&& subscript = subScript.getSliceRef(lastCSoffset, subScriptLen);

   //pre state
   computePreState(stub);

   //serialize hashdata
   BinaryWriter hashdata;

   //version
   hashdata.put_uint32_t(stub.getVersion());

   //hashPrevouts
   hashdata.put_BinaryData(hashPrevouts_);

   //hashSequence
   hashdata.put_BinaryData(hashSequence_);

   //outpoint
   hashdata.put_BinaryDataRef(stub.getOutpoint(inputIndex));

   //script code
   hashdata.put_var_int(subScriptLen);
   hashdata.put_BinaryDataRef(subscript);

   //value
   hashdata.put_uint64_t(stub.getOutpointValue(inputIndex));

   //sequence
   hashdata.put_uint32_t(stub.getTxInSequence(inputIndex));

   //hashOutputs
   hashdata.put_BinaryData(hashOutputs_);

   //nLocktime
   hashdata.put_uint32_t(stub.getLockTime());

   //sighash type
   hashdata.put_uint32_t(getSigHashAll_4Bytes());

   return hashdata.getData();
}

////////////////////////////////////////////////////////////////////////////////
void SigHashDataSegWit::computePreState(const TransactionStub& txStub)
{
   if (initialized_)
      return;

   //hashPrevouts
   auto&& allOutpoints = txStub.serializeAllOutpoints();
   hashPrevouts_ = move(BtcUtils::getHash256(allOutpoints));

   //hashSequence
   auto&& allSequences = txStub.serializeAllSequences();
   hashSequence_ = move(BtcUtils::getHash256(allSequences));

   //hashOutputs
   auto allOutputs = txStub.getSerializedOutputScripts();
   hashOutputs_ = move(BtcUtils::getHash256(allOutputs));

   //flag
   initialized_ = true;
}
