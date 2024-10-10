////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2024, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Signer.h"
#include "Script.h"
#include "LegacySigner.h"
#include "Transactions.h"

#include "BIP32_Node.h"
#include "Assets.h"

#include "Addresses.h"
#include "../TxOutScrRef.h"
#include "../BitcoinSettings.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/Signer.capnp.h"
#include "capnp/Types.capnp.h"

using namespace Armory;

#define TXSIGCOLLECT_VER_LEGACY  1
#define USTXI_VER_LEGACY         1
#define USTXO_VER_LEGACY         1
#define TXSIGCOLLECT_VER_MODERN  3
#define TXSIGCOLLECT_WIDTH       64
#define TXSIGCOLLECT_HEADER      "=====TXSIGCOLLECT-"

#define TXIN_EXT_P2SHSCRIPT      0x10

Signing::StackItem::~StackItem()
{}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// capnp helpers
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
struct Signing::Serializer
{
   static void stackItemToCapn(std::shared_ptr<StackItem> item,
      Codec::Signer::StackItem::Builder& capnItem)
   {
      capnItem.setType((Codec::Signer::StackItemType)item->type());
      capnItem.setId(item->getId());

      switch (item->type()) {
         case StackItemType::PushData:
         {
            auto pushDataPtr =
               std::dynamic_pointer_cast<StackItem_PushData>(item);
            if (pushDataPtr == nullptr) {
               throw std::runtime_error("failed StackItem_PushData cast");
            }

            capnItem.setStackData(capnp::Data::Builder(
               (uint8_t*)pushDataPtr->data_.getPtr(), pushDataPtr->data_.getSize()
            ));
            break;
         }

         case StackItemType::OpCode:
         {
            auto opCodePtr = std::dynamic_pointer_cast<StackItem_OpCode>(item);
            if (opCodePtr == nullptr) {
               throw std::runtime_error("failed StackItem_OpCode cast");
            }

            capnItem.setOpCode(opCodePtr->opcode_);
            break;
         }

         case StackItemType::Sig:
         {
            auto sigPtr = std::dynamic_pointer_cast<StackItem_Sig>(item);
            if (sigPtr == nullptr) {
               throw std::runtime_error("failed StackItem_Sig cast");
            }

            auto capnSig = capnItem.initSingleSigData();
            capnSig.setScript(capnp::Data::Builder(
               (uint8_t*)sigPtr->script_.getPtr(), sigPtr->script_.getSize()
            ));
            capnSig.setPubkey(capnp::Data::Builder(
               (uint8_t*)sigPtr->pubkey_.getPtr(), sigPtr->pubkey_.getSize()
            ));
            break;
         }

         case StackItemType::MultiSig:
         {
            auto sigPtr = std::dynamic_pointer_cast<StackItem_MultiSig>(item);
            if (sigPtr == nullptr) {
               throw std::runtime_error("failed StackItem_MultiSig cast");
            }

            auto capnSig = capnItem.initMultiSigData();
            capnSig.setScript(capnp::Data::Builder(
               (uint8_t*)sigPtr->script_.getPtr(), sigPtr->script_.getSize()
            ));

            unsigned i=0;
            auto sigDatas = capnSig.initSigData(sigPtr->sigs_.size());
            for (const auto& sig : sigPtr->sigs_) {
               auto sigData = sigDatas[i++];
               sigData.setIndex(sig.first);
               sigData.setSig(capnp::Data::Builder(
                  (uint8_t*)sig.second.getPtr(), sig.second.getSize()
               ));
            }
            break;
         }

         case StackItemType::SerializedScript:
         {
            auto scriptPtr =
               std::dynamic_pointer_cast<StackItem_SerializedScript>(item);
            if (scriptPtr == nullptr) {
               throw std::runtime_error(
                  "failed StackItem_SerializedScript cast");
            }

            capnItem.setStackData(capnp::Data::Builder(
               (uint8_t*)scriptPtr->data_.getPtr(), scriptPtr->data_.getSize()
            ));
            break;
         }

         default:
            throw std::runtime_error("unexpected stack item type");
      }
   }

   static void bip32PathsToCapn(
      const std::map<BinaryData, BIP32_AssetPath>& paths,
      capnp::List<Codec::Signer::PubkeyBIP32Path>::Builder& capnPaths)
   {
      unsigned i=0;
      for (const auto& path : paths) {
         auto capnPath = capnPaths[i++];

         const auto& pubkey = path.second.getPublicKey();
         capnPath.setPubkey(capnp::Data::Builder(
            (uint8_t*)pubkey.getPtr(), pubkey.getSize()
         ));

         capnPath.setFingerprint(path.second.getThisFingerprint());

         const auto& steps = path.second.getPath();
         auto capnSteps = capnPath.initPath(steps.size());
         for (unsigned y=0; y<steps.size(); y++) {
            capnSteps.set(y, steps[y]);
         }
      }
   }

   static void spenderToCapn(std::shared_ptr<Signing::ScriptSpender> spender,
      Codec::Signer::ScriptSpender::Builder& capnSpender)
   {
      //header
      capnSpender.setVersionMax(SCRIPT_SPENDER_VERSION_MAX);
      capnSpender.setVersionMin(SCRIPT_SPENDER_VERSION_MIN);

      capnSpender.setLegacyStatus((uint8_t)spender->legacyStatus_);
      capnSpender.setSegwitStatus((uint8_t)spender->segwitStatus_);

      capnSpender.setSigHashType(spender->sigHashType_);
      capnSpender.setSequence(spender->sequence_);

      capnSpender.setIsP2sh(spender->isP2SH_);
      capnSpender.setIsCsv(spender->isCSV_);
      capnSpender.setIsCltv(spender->isCLTV_);

      //utxo
      if (spender->utxo_.isInitialized()) {
         const auto& utxo = spender->utxo_;
         auto capnUtxo = capnSpender.initUtxo();
         capnUtxo.setValue(utxo.value_);
         capnUtxo.setTxHeight(utxo.txHeight_);
         capnUtxo.setTxIndex(utxo.txIndex_);
         capnUtxo.setTxOutIndex(utxo.txOutIndex_);

         capnUtxo.setTxHash(capnp::Data::Builder(
            (uint8_t*)utxo.txHash_.getPtr(), utxo.txHash_.getSize()
         ));

         capnUtxo.setScript(capnp::Data::Builder(
            (uint8_t*)utxo.script_.getPtr(), utxo.script_.getSize()
         ));
      } else {
         auto outputHash = spender->getOutputHash();
         auto outpoint = capnSpender.initOutpoint();

         outpoint.setIndex(spender->getOutputIndex());
         outpoint.setTxHash(capnp::Data::Builder(
            (uint8_t*)outputHash.getPtr(), outputHash.getSize()
         ));
      }

      //legacy state
      if (spender->legacyStatus_ == SpenderStatus::Signed) {
         capnSpender.setSigScript(capnp::Data::Builder(
            (uint8_t*)spender->finalInputScript_.getPtr(),
            spender->finalInputScript_.getSize()
         ));
      }
      else if (spender->legacyStatus_ >= SpenderStatus::Resolved)
      {
         auto capnStackEntries = capnSpender.initLegacyStack(
            spender->legacyStack_.size());

         //put legacy stack
         unsigned i=0;
         for (auto stackItem : spender->legacyStack_)
         {
            auto capnStackEntry = capnStackEntries[i++];
            stackItemToCapn(stackItem.second, capnStackEntry);
         }
      }

      //segwit stack
      if (spender->segwitStatus_ == SpenderStatus::Signed) {
         capnSpender.setWitnessData(capnp::Data::Builder(
            (uint8_t*)spender->finalWitnessData_.getPtr(),
            spender->finalWitnessData_.getSize()
         ));
      }
      else if (spender->segwitStatus_ >= SpenderStatus::Resolved) {
         auto capnStackEntries = capnSpender.initWitnessStack(
            spender->witnessStack_.size());

         //put witness stack
         unsigned i=0;
         for (auto stackItem : spender->witnessStack_)
         {
            auto capnStackEntry = capnStackEntries[i++];
            stackItemToCapn(stackItem.second, capnStackEntry);
         }
      }

      //path data
      const auto& paths = spender->bip32Paths_;
      auto capnPaths = capnSpender.initBip32Paths(paths.size());
      bip32PathsToCapn(paths, capnPaths);
   }

   static void recipientToCapn(std::shared_ptr<Signing::ScriptRecipient> recipient,
      unsigned groupId, Codec::Signer::Recipient::Builder& capnRecipient)
   {
      const auto& script = recipient->getSerializedScript();
      capnRecipient.setScript(capnp::Data::Builder(
         (uint8_t*)script.getPtr(), script.getSize()
      ));
      capnRecipient.setGroupId(groupId);

      const auto& paths = recipient->bip32Paths_;
      auto capnPaths = capnRecipient.initBip32Paths(paths.size());
      bip32PathsToCapn(paths, capnPaths);
   }
};

struct Signing::Deserializer
{
////////////////////////////////////////////////////////////////////////////////
   static std::shared_ptr<BIP32_PublicDerivedRoot> capnToBIP32Root(
      Codec::Signer::BIP32PublicRoot::Reader& capnRoot)
   {
      auto capnPath = capnRoot.getPath();
      std::vector<unsigned> path;
      path.reserve(capnPath.size());
      for (auto step : capnPath) {
         path.push_back(step);
      }

      return std::make_shared<BIP32_PublicDerivedRoot>(
         capnRoot.getXpub(), path, capnRoot.getFingerprint());
   }

   static BIP32_AssetPath capnToBIP32Path(
      const Codec::Signer::PubkeyBIP32Path::Reader& capnPath)
   {
      auto capnPubkey = capnPath.getPubkey();
      BinaryData pubkey(capnPubkey.begin(), capnPubkey.end());

      auto capnSteps = capnPath.getPath();
      std::vector<uint32_t> path;
      path.reserve(capnSteps.size());
      for (auto step : capnSteps) {
         path.push_back(step);
      }

      return BIP32_AssetPath(pubkey, path, capnPath.getFingerprint(), nullptr);
   }

   static std::shared_ptr<StackItem> capnToStackItem(
      const Codec::Signer::StackItem::Reader& capnStackItem)
   {
      std::shared_ptr<StackItem> result;
      auto type = (StackItemType)capnStackItem.getType();
      switch (type) {
         case StackItemType::PushData:
         {
            if (!capnStackItem.isStackData()) {
               throw SignerDeserializationError("expected stack data");
            }

            auto capnStackData = capnStackItem.getStackData();
            BinaryData stackData(capnStackData.begin(), capnStackData.end());
            result = std::make_shared<StackItem_PushData>(
               capnStackItem.getId(), std::move(stackData));
            break;
         }

         case StackItemType::OpCode:
         {
            if (!capnStackItem.isOpCode()) {
               throw SignerDeserializationError("expected opcode");
            }

            result = std::make_shared<StackItem_OpCode>(
               capnStackItem.getId(), capnStackItem.getOpCode());
            break;
         }

         case StackItemType::Sig:
         {
            if (!capnStackItem.isSingleSigData()) {
               throw SignerDeserializationError("expected single sig data");
            }

            auto singleSigData = capnStackItem.getSingleSigData();
            auto capnScript = singleSigData.getScript();
            BinaryData script(capnScript.begin(), capnScript.end());

            auto capnPubkey = singleSigData.getPubkey();
            BinaryData pubkey(capnPubkey.begin(), capnPubkey.end());

            result = std::make_shared<StackItem_Sig>(
               capnStackItem.getId(), pubkey, script);
            break;
         }

         case StackItemType::MultiSig:
         {
            if (!capnStackItem.isMultiSigData()) {
               throw SignerDeserializationError("expected multi sig data");
            }

            auto multiSigData = capnStackItem.getMultiSigData();
            auto capnScript = multiSigData.getScript();
            BinaryData script(capnScript.begin(), capnScript.end());
            auto resultMs = std::make_shared<StackItem_MultiSig>(
               capnStackItem.getId(), script);

            auto capnSigs = multiSigData.getSigData();
            for (const auto& capnSig : capnSigs) {
               auto sigData = capnSig.getSig();
               SecureBinaryData sig(sigData.begin(), sigData.end());
               resultMs->setSig(capnSig.getIndex(), sig);
            }

            result = resultMs;
            break;
         }

         case StackItemType::SerializedScript:
         {
            if (!capnStackItem.isStackData()) {
               throw SignerDeserializationError("expected stack data");
            }

            auto capnStackData = capnStackItem.getStackData();
            BinaryData stackData(capnStackData.begin(), capnStackData.end());
            result = std::make_shared<StackItem_SerializedScript>(
               capnStackItem.getId(), std::move(stackData));
            break;
         }

         default:
            throw SignerDeserializationError("unexpected stack item type");
      }

      return result;
   }

   static UTXO capnToUtxo(const Codec::Types::Output::Reader& capnOutput)
   {
      UTXO result;
      result.value_ = capnOutput.getValue();
      result.txHeight_ = capnOutput.getTxHeight();
      result.txIndex_ = capnOutput.getTxIndex();
      result.txOutIndex_ = capnOutput.getTxOutIndex();

      auto capnScript = capnOutput.getScript();
      result.script_ = BinaryDataRef(capnScript.begin(), capnScript.end());

      auto capnHash = capnOutput.getTxHash();
      result.txHash_ = BinaryDataRef(capnHash.begin(), capnHash.end());
      if (result.txHash_.getSize() != 32) {
         throw std::runtime_error("invalid utxo hash size");
      }

      return result;
   }

   static std::shared_ptr<ScriptSpender> capnToSpender(
      const Codec::Signer::ScriptSpender::Reader& capnSpender)
   {
      //version sanity check
      auto maxVer = capnSpender.getVersionMax();
      auto minVer = capnSpender.getVersionMin();
      if (maxVer != SCRIPT_SPENDER_VERSION_MAX ||
         minVer != SCRIPT_SPENDER_VERSION_MIN)
      {
         throw SignerDeserializationError("serialized spender version mismatch");
      }

      //utxo/outpoint
      std::shared_ptr<ScriptSpender> result;
      if (capnSpender.hasUtxo()) {
         auto capnUtxo = capnSpender.getUtxo();
         auto utxo = capnToUtxo(capnUtxo);
         result = std::make_shared<ScriptSpender>(utxo);
      }
      else if (capnSpender.hasOutpoint()) {
         auto outpoint = capnSpender.getOutpoint();
         auto capnHash = outpoint.getTxHash();
         BinaryDataRef outpointHash(capnHash.begin(), capnHash.end());
         if (outpointHash.getSize() != 32)
            throw SignerDeserializationError("invalid outpoint hash");

         result = std::make_shared<ScriptSpender>(
            outpointHash, outpoint.getIndex());
      }
      else
      {
         throw SignerDeserializationError("missing utxo/outpoint");
      }

      //stack flags
      result->legacyStatus_ = (SpenderStatus)capnSpender.getLegacyStatus();
      result->segwitStatus_ = (SpenderStatus)capnSpender.getSegwitStatus();

      result->isP2SH_ = capnSpender.getIsP2sh();
      result->isCSV_  = capnSpender.getIsCsv();
      result->isCLTV_ = capnSpender.getIsCltv();

      result->sequence_ = capnSpender.getSequence();
      result->sigHashType_ = (SIGHASH_TYPE)capnSpender.getSigHashType();

      //finalized script & witness data
      if (capnSpender.hasSigScript()) {
         auto sigScript = capnSpender.getSigScript();
         result->finalInputScript_ = BinaryDataRef(
            sigScript.begin(), sigScript.end());
      }

      if (capnSpender.hasWitnessData()) {
         auto witnessData = capnSpender.getWitnessData();
         result->finalWitnessData_ = BinaryDataRef(
            witnessData.begin(), witnessData.end());
      }

      //legacy stack
      auto capnLegacyStack = capnSpender.getLegacyStack();
      for (auto capnStackItem : capnLegacyStack) {
         auto stackItem = capnToStackItem(capnStackItem);
         result->legacyStack_.emplace(stackItem->getId(), stackItem);
      }

      //witness stack
      auto capnWitnessStack = capnSpender.getWitnessStack();
      for (auto capnStackItem : capnWitnessStack) {
         auto stackItem = capnToStackItem(capnStackItem);
         result->witnessStack_.emplace(stackItem->getId(), stackItem);
      }

      //paths
      auto capnPaths = capnSpender.getBip32Paths();
      for (auto capnPath : capnPaths) {
         auto path = capnToBIP32Path(capnPath);
         result->bip32Paths_.emplace(path.getPublicKey(), std::move(path));
      }

      return result;
   }

   static std::shared_ptr<ScriptRecipient> capnToRecipient(
      const Codec::Signer::Recipient::Reader& capnRecipient)
   {
      auto capnScript = capnRecipient.getScript();
      BinaryDataRef scriptRef(capnScript.begin(), capnScript.end());
      auto result = ScriptRecipient::fromScript(scriptRef);

      auto capnPaths = capnRecipient.getBip32Paths();
      for (auto capnPath : capnPaths) {
         auto path = capnToBIP32Path(capnPath);
         result->addBip32Path(path);
      }

      return result;
   }

   static void capnToSigner(Signer& signer, BinaryDataRef raw)
   {
      //deser capn payload
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(raw.getPtr()),
         raw.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto signerCapn = reader.getRoot<Codec::Signer::Signer>();

      //flags
      signer.resetFlags();
      signer.version_ = signerCapn.getTxVersion();
      signer.lockTime_ = signerCapn.getLocktime();
      signer.flags_ = signerCapn.getFlags();

      //spenders
      auto capnSpenders = signerCapn.getSpenders();
      for (auto capnSpender : capnSpenders) {
         auto spender = capnToSpender(capnSpender);
         signer.addSpender(spender);
      }

      //recipients
      auto capnRecipients = signerCapn.getRecipients();
      for (auto capnRecipient : capnRecipients) {
         auto recipient = capnToRecipient(capnRecipient);
         signer.addRecipient(recipient, capnRecipient.getGroupId());
      }

      //txmap
      auto capnTxns = signerCapn.getSupportingTxs();
      for (auto capnTx : capnTxns) {
         BinaryDataRef rawTxRef(capnTx.begin(), capnTx.end());
         Tx tx(rawTxRef);
         signer.supportingTxMap_->emplace(tx.getThisHash(), std::move(tx));
      }

      //roots
      auto capnRoots = signerCapn.getBip32Roots();
      for (auto capnRoot : capnRoots) {
         auto root = capnToBIP32Root(capnRoot);
         signer.bip32PublicRoots_.emplace(root->getThisFingerprint(), root);
      }
      signer.matchAssetPathsWithRoots();
   }
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// ScriptSpender
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
const UTXO& Signing::ScriptSpender::getUtxo() const
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

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef Signing::ScriptSpender::getOutputScript() const
{
   const auto& utxo = getUtxo();
   return utxo.getScript();
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef Signing::ScriptSpender::getOutputHash() const
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

////////////////////////////////////////////////////////////////////////////////
unsigned Signing::ScriptSpender::getOutputIndex() const
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

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef Signing::ScriptSpender::getOutpoint() const
{
   if (outpoint_.empty()) {
      BinaryWriter bw;
      bw.put_BinaryDataRef(getOutputHash());
      bw.put_uint32_t(getOutputIndex());

      outpoint_ = bw.getData();
   }
   return outpoint_.getRef();
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::ScriptSpender::serializeScript(
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

            bwStack.put_BinaryData(
               BtcUtils::getPushDataHeader(stackItem_pushdata->data_));
            bwStack.put_BinaryData(stackItem_pushdata->data_);
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

            bwStack.put_BinaryData(stackItem_ss->data_);
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

            bwStack.put_BinaryData(
               BtcUtils::getPushDataHeader(stackItem_sig->sig_));
            bwStack.put_BinaryData(stackItem_sig->sig_);
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

            if (stackItem_sig->sigs_.size() < stackItem_sig->m_) {
               if (!no_throw) {
                  throw ScriptException("missing sigs for ms script");
               }
            }

            for (const auto& sigpair : stackItem_sig->sigs_) {
               bwStack.put_BinaryData(
                  BtcUtils::getPushDataHeader(sigpair.second));
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

            bwStack.put_uint8_t(stackItem_opcode->opcode_);
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

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::ScriptSpender::serializeWitnessData(
   const std::vector<std::shared_ptr<StackItem>>& stack, 
   unsigned &itemCount, bool no_throw)
{
   itemCount = 0;

   BinaryWriter bwStack;
   for (auto& stackItem : stack)
   {
      switch (stackItem->type())
      {
      case StackItemType::PushData:
      {
         ++itemCount;

         auto stackItem_pushdata =
            std::dynamic_pointer_cast<StackItem_PushData>(stackItem);
         if (stackItem_pushdata == nullptr)
         {
            if (!no_throw)
               throw ScriptException("unexpected StackItem type");

            bwStack.put_uint8_t(0);
            break;
         }

         bwStack.put_var_int(stackItem_pushdata->data_.getSize());
         bwStack.put_BinaryData(stackItem_pushdata->data_);
         break;
      }

      case StackItemType::SerializedScript:
      {

         auto stackItem_ss =
            std::dynamic_pointer_cast<StackItem_SerializedScript>(stackItem);
         if (stackItem_ss == nullptr)
         {
            if (!no_throw)
               throw ScriptException("unexpected StackItem type");

            break;
         }

         bwStack.put_BinaryData(stackItem_ss->data_);
         ++itemCount;
         break;
      }

      case StackItemType::Sig:
      {
         ++itemCount;
         auto stackItem_sig =
            std::dynamic_pointer_cast<StackItem_Sig>(stackItem);
         if (stackItem_sig == nullptr)
         {
            if (!no_throw)
               throw ScriptException("unexpected StackItem type");

            bwStack.put_uint8_t(0);
            break;
         }

         bwStack.put_var_int(stackItem_sig->sig_.getSize());
         bwStack.put_BinaryData(stackItem_sig->sig_);
         break;
      }

      case StackItemType::MultiSig:
      {
         auto stackItem_sig =
            std::dynamic_pointer_cast<StackItem_MultiSig>(stackItem);
         if (stackItem_sig == nullptr)
         {
            if (!no_throw)
               throw ScriptException("unexpected StackItem type");

            bwStack.put_uint8_t(0);
            break;
         }

         if (stackItem_sig->sigs_.size() < stackItem_sig->m_)
         {
            if (!no_throw)
               throw ScriptException("missing sigs for ms script");
         }

         for (auto& sigpair : stackItem_sig->sigs_)
         {
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
         if (stackItem_opcode == nullptr)
         {
            if (!no_throw)
               throw ScriptException("unexpected StackItem type");

            bwStack.put_uint8_t(0);
            break;
         }

         bwStack.put_uint8_t(stackItem_opcode->opcode_);
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

////////////////////////////////////////////////////////////////////////////////
bool Signing::ScriptSpender::isResolved() const
{
   if (!canBeResolved())
      return false;

   if (!isSegWit())
   {
      if (legacyStatus_ >= SpenderStatus::Resolved)
         return true;
   }
   else
   {
      //If this spender is SW, only emtpy (native sw) and resolved (nested sw) 
      //states are valid. The SW stack should not be empty for a SW input
      if ((legacyStatus_ == SpenderStatus::Empty ||
         legacyStatus_ == SpenderStatus::Resolved) &&
         segwitStatus_ >= SpenderStatus::Resolved)
      {
         return true;
      }

   }
   return false;
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::ScriptSpender::isSigned() const
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

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::ScriptSpender::getSerializedOutpoint() const
{
   if (utxo_.isInitialized())
   {
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

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::ScriptSpender::getAvailableInputScript() const
{
   //if we have a serialized script already, return that
   if (!finalInputScript_.empty())
      return finalInputScript_;

   //otherwise, serialize it from the stack
   std::vector<std::shared_ptr<StackItem>> stack;
   for (const auto& stack_item : legacyStack_) {
      stack.push_back(stack_item.second);
   }
   return serializeScript(stack, true);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::ScriptSpender::getSerializedInput(
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

   auto serializedScript = getAvailableInputScript();

   BinaryWriter bw;
   bw.put_BinaryData(getSerializedOutpoint());

   bw.put_var_int(serializedScript.getSize());
   bw.put_BinaryData(serializedScript);
   bw.put_uint32_t(sequence_);

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::ScriptSpender::getEmptySerializedInput() const
{
   BinaryWriter bw;
   bw.put_BinaryData(getSerializedOutpoint());
   bw.put_uint8_t(0);
   bw.put_uint32_t(sequence_);

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef Signing::ScriptSpender::getFinalizedWitnessData() const
{
   if (isSegWit()) {
      if (segwitStatus_ != SpenderStatus::Signed) {
         throw std::runtime_error("witness data missing signature");
      }
   } else if (segwitStatus_ != SpenderStatus::Empty) {
      throw std::runtime_error("unresolved witness");
   }

   return finalWitnessData_.getRef();
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::ScriptSpender::serializeAvailableWitnessData() const
{
   try
   {
      return getFinalizedWitnessData();
   }
   catch (const std::exception&)
   {}

   std::vector<std::shared_ptr<StackItem>> stack;
   for (auto& stack_item : witnessStack_)
      stack.push_back(stack_item.second);

   //serialize and get item count
   unsigned itemCount = 0;
   auto&& data = serializeWitnessData(stack, itemCount, true);

   //put stack item count
   BinaryWriter bw;
   bw.put_var_int(itemCount);

   //put serialized stack
   bw.put_BinaryData(data);

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
void Signing::ScriptSpender::setWitnessData(
   const std::vector<std::shared_ptr<StackItem>>& stack)
{
   //serialize to get item count
   unsigned itemCount = 0;
   auto&& data = serializeWitnessData(stack, itemCount);

   //put stack item count
   BinaryWriter bw;
   bw.put_var_int(itemCount);

   //put serialized stack
   bw.put_BinaryData(data);

   finalWitnessData_ = bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
void Signing::ScriptSpender::updateStack(
   std::map<unsigned, std::shared_ptr<StackItem>>& stackMap,
   const std::vector<std::shared_ptr<StackItem>>& stackVec)
{
   for (auto& stack_item : stackVec)
   {
      auto iter_pair = stackMap.insert(
         std::make_pair(stack_item->getId(), stack_item));

      if (iter_pair.second == true)
         continue;

      //already have a stack item for this id, let's compare them
      if (iter_pair.first->second->isSame(stack_item.get()))
         continue;

      //stack items differ, are they multisig items?

      switch (iter_pair.first->second->type())
      {
      case StackItemType::PushData:
      {
         if (!iter_pair.first->second->isValid())
            iter_pair.first->second = stack_item;
         else if (stack_item->isValid())
            throw ScriptException("invalid push_data");

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

////////////////////////////////////////////////////////////////////////////////
void Signing::ScriptSpender::processStacks()
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
      for (auto& item_pair : stack)
      {
         auto& stack_item = item_pair.second;
         switch (stack_item->type())
         {
            case StackItemType::MultiSig:
            {
               if (stack_item->isValid())
               {
                  stackState = SpenderStatus::Signed;
                  break;
               }

               auto stack_item_ms = std::dynamic_pointer_cast<StackItem_MultiSig>(
                  stack_item);

               if (stack_item_ms == nullptr)
                  throw std::runtime_error("unexpected stack item type");

               if (stack_item_ms->sigs_.size() > 0)
                  stackState = SpenderStatus::PartiallySigned;
                  
               break;
            }

            case StackItemType::Sig:
            {
               if (stack_item->isValid())
                  stackState = SpenderStatus::Signed;
               break;
            }

            default:
            {
               if (!stack_item->isValid())
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

      if (stackState >= spenderState)
      {
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
               for (auto& item_pair : stack)
                  stack_vec.push_back(item_pair.second);

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

   if (!legacyStack_.empty())
   {
      updateState(legacyStack_, legacyStatus_, [this](
         const std::vector<std::shared_ptr<StackItem>>& stackVec)
         { finalInputScript_ = std::move(serializeScript(stackVec)); }
      );
   }

   if (!witnessStack_.empty())
   {
      updateState(witnessStack_, segwitStatus_, [this](
         const std::vector<std::shared_ptr<StackItem>>& stackVec)
         { this->setWitnessData(stackVec); }
      );
   }
}

////////////////////////////////////////////////////////////////////////////////
void Signing::ScriptSpender::merge(const ScriptSpender& obj)
{
   /*
   Do not tolerate sequence mismatch. Sequence should be updated explicitly
   if the transaction scheme calls for it.
   */
   if (sequence_ != obj.sequence_)
      throw std::runtime_error("sequence mismatch");

   //nothing to merge if the spender is already signed
   if (isSigned())
      return;

   //do we have supporting data?
   {
      //sanity check on obj
      BinaryDataRef objOpHash;
      uint64_t objOpVal;
      try
      {
         objOpHash = obj.getOutputHash();
         objOpVal = obj.getValue();
      }
      catch (const std::exception&)
      {
         //obj has no supporting data, it doesn't carry anything to merge
         return;
      }

      try
      {
         if (getOutputHash() != objOpHash)
            throw std::runtime_error("spender output hash mismatch");

         if (getOutputIndex() != obj.getOutputIndex())
            throw std::runtime_error("spender output index mismatch");

         if (getValue() != objOpVal)
            throw std::runtime_error("spender output value mismatch");           
      }
      catch (const SpenderException&)
      {
         //missing supporting data, get it from obj
         if (obj.utxo_.isInitialized())
            utxo_ = obj.utxo_;
         else if (!obj.outpoint_.empty())
            outpoint_ = obj.outpoint_;
         else
            throw std::runtime_error("impossible condition, how did we get here??");
      }
   }

   isP2SH_ |= obj.isP2SH_;
   isCLTV_ |= obj.isCLTV_;
   isCSV_  |= obj.isCSV_;

   //legacy stack
   if (legacyStatus_ != SpenderStatus::Signed)
   {
      switch (obj.legacyStatus_)
      {
      case SpenderStatus::Resolved:
      case SpenderStatus::PartiallySigned:
      {
         //merge the stacks
         std::vector<std::shared_ptr<StackItem>> objStackVec;
         for (auto& stackItemPtr : obj.legacyStack_)
            objStackVec.emplace_back(stackItemPtr.second);

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
         if (obj.legacyStatus_ > legacyStatus_)
            legacyStatus_ = obj.legacyStatus_;
      }
   }

   //segwit stack
   if (segwitStatus_ != SpenderStatus::Signed)
   {
      switch (obj.segwitStatus_)
      {
      case SpenderStatus::Resolved:
      case SpenderStatus::PartiallySigned:
      {
         //merge the stacks
         std::vector<std::shared_ptr<StackItem>> objStackVec;
         for (auto& stackItemPtr : obj.witnessStack_)
            objStackVec.emplace_back(stackItemPtr.second);

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

////////////////////////////////////////////////////////////////////////////////
bool Signing::ScriptSpender::compareEvalState(const ScriptSpender& rhs) const
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

      try
      {
         if (isWitnessData)
            brr.get_var_int(); //drop witness item count

         while (brr.getSizeRemaining() > 0)
         {
            auto len = brr.get_var_int();
            if (len == 0)
            {
               resolvedScriptItems.push_back(BinaryDataRef());
               continue;
            }

            auto dataRef = brr.get_BinaryDataRef(len);

            if (dataRef.getSize() > 68 && 
               dataRef.getPtr()[0] == 0x30 &&
               dataRef.getPtr()[2] == 0x02)
            {
               //this is a sig, set an empty place holder instead
               resolvedScriptItems.push_back(BinaryDataRef());
               continue;
            }

            resolvedScriptItems.push_back(dataRef);
         }
      }
      catch (const std::exception&)
      {}

      return resolvedScriptItems;
   };

   auto isStackMultiSig = [](
      const std::map<unsigned, std::shared_ptr<StackItem>>& stack)->bool
   {
      for (auto& stack_item : stack)
      {
         if (stack_item.second->type() == StackItemType::MultiSig)
            return true;
      }

      return false;
   };

   auto compareScriptItems = [](
      const std::vector<BinaryDataRef>& ours,
      const std::vector<BinaryDataRef>& theirs,
      bool isMultiSig)->bool
   {
      if (ours == theirs)
         return true;

      if (theirs.empty())
      {
         /*
         If ours isn't empty, theirs cannot be empty (it needs the 
         resolved data at least). Edge case: ours carry only empty
         data vectors.
         */

         bool empty = true;
         for (const auto& ourItem : ours)
         {
            if (!ourItem.empty())
            {
               empty = false;
               break;
            }
         }

         return empty;
      }

      if (isMultiSig)
      {
         //multisig script, tally 0s and compare
         std::vector<BinaryDataRef> oursStripped;
         unsigned ourZeroCount = 0;
         for (auto& ourItem : ours)
         {
            if (ourItem.empty())
               ++ourZeroCount;
            else
               oursStripped.push_back(ourItem);
         }

         std::vector<BinaryDataRef> theirsStripped;
         unsigned theirZeroCount = 0;
         for (auto& theirItem : theirs)
         {
            if (theirItem.empty())
               ++theirZeroCount;
            else
               theirsStripped.push_back(theirItem);
         }

         if (oursStripped == theirsStripped)
         {
            if (ourZeroCount > 1 && theirZeroCount >= 1)
               return true;
         }
      }

      return false;
   };

   //check utxos
   {
      if (getOutputHash() != rhs.getOutputHash() ||
         getOutputIndex() != rhs.getOutputIndex() ||
         getValue() != getValue())
         return false;
   }
   
   //legacy status
   if (legacyStatus_ != rhs.legacyStatus_)
   {
      if (legacyStatus_ >= SpenderStatus::Resolved &&
         rhs.legacyStatus_ != SpenderStatus::Resolved)
      {
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
      if (!compareScriptItems(ourScriptItems, theirScriptItems, isMultiSig))
         return false;
   }

   //segwit status
   if (segwitStatus_ != rhs.segwitStatus_)
   {
      if (segwitStatus_ >= SpenderStatus::Resolved &&
         rhs.segwitStatus_ != SpenderStatus::Resolved)
      {
         /*
         This checks resolved state. Signed spenders are resolved.
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
      if (!compareScriptItems(ourScriptItems, theirScriptItems, isMultiSig))
         return false;
   }

   if (isP2SH_ != rhs.isP2SH_)
      return false;

   if (isCSV_ != rhs.isCSV_ || isCLTV_ != rhs.isCLTV_)
      return false;

   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::ScriptSpender::isInitialized() const
{
   if (legacyStatus_ == SpenderStatus::Unknown &&
      segwitStatus_ == SpenderStatus::Unknown &&
      isP2SH_ == false && legacyStack_.empty() && witnessStack_.empty() &&
      finalInputScript_.empty() && finalWitnessData_.empty())
   {
      return false;
   }

   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::ScriptSpender::verifyEvalState(unsigned flags)
{
   /*
   check resolution state from public data is consistent with the serialized
   script
   */

   //uninitialized spender, nothing to check
   if (!isInitialized())
   {
      return true;
   }

   //sanity check: needs a utxo set to be resolved
   if (!canBeResolved())
   {
      return false;
   }

   ScriptSpender spenderVerify;
   spenderVerify.sequence_ = sequence_;

   if (utxo_.isInitialized())
      spenderVerify.utxo_ = utxo_;
   else
      spenderVerify.outpoint_ = outpoint_;

   spenderVerify.txMap_ = txMap_;

   /*construct public resolver from the serialized script*/
   auto feed = std::make_shared<ResolverFeed_SpenderResolutionChecks>();

   //look for push data in the sigScript
   auto&& legacyScript = getAvailableInputScript();

   try
   {
      auto pushDataVec = BtcUtils::splitPushOnlyScriptRefs(legacyScript);
      for (auto& pushData : pushDataVec)
      {
         //hash it and add to the feed's hash map
         auto hash = BtcUtils::getHash160(pushData);
         feed->hashMap.emplace(hash, pushData);
      }
   }
   catch (const std::runtime_error&)
   {
      //just exit the loop on deser error
   }
   
   //same with the witness data

   BinaryReader brSW;
   if (finalWitnessData_.empty())
   {
      std::vector<std::shared_ptr<StackItem>> stack;
      for (const auto& stack_item : witnessStack_)
         stack.push_back(stack_item.second);

      //serialize and get item count
      unsigned itemCount = 0;
      auto&& data = serializeWitnessData(stack, itemCount, true);

      //put stack item count
      BinaryWriter bw;
      bw.put_var_int(itemCount);

      //put serialized stack
      bw.put_BinaryData(data);

      brSW.setNewData(bw.getData());
   }
   else
   {
      brSW.setNewData(finalWitnessData_);
   }

   try
   {
      auto itemCount = brSW.get_var_int();

      for (unsigned i=0; i<itemCount; i++)
      {
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

      if (brSW.getSizeRemaining() > 0)
      {
         //unparsed data remains in the witness data script, 
         //this shouldn't happen
         return false;
      }
   }
   catch (const std::runtime_error&)
   {
      //just exit the loop on deser error
   }

   //create resolver with mock feed and process it

   try
   {
      StackResolver resolver(getOutputScript(), feed);
      resolver.setFlags(flags);
      spenderVerify.parseScripts(resolver);
   }
   catch (const std::exception&)
   {}

   if (!compareEvalState(spenderVerify))
      return false;

   return true;
}

////////////////////////////////////////////////////////////////////////////////
void Signing::ScriptSpender::updateLegacyStack(
   const std::vector<std::shared_ptr<StackItem>>& stack)
{
   if (legacyStatus_ >= SpenderStatus::Resolved)
      return;

   if (!stack.empty()) {
      updateStack(legacyStack_, stack);
   } else {
      legacyStatus_ = SpenderStatus::Empty;
   }
}

////////////////////////////////////////////////////////////////////////////////
void Signing::ScriptSpender::updateWitnessStack(
   const std::vector<std::shared_ptr<StackItem>>& stack)
{
   if (segwitStatus_ >= SpenderStatus::Resolved) {
      return;
   }

   updateStack(witnessStack_, stack);
}

////////////////////////////////////////////////////////////////////////////////
void Signing::ScriptSpender::parseScripts(StackResolver& resolver)
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

////////////////////////////////////////////////////////////////////////////////
void Signing::ScriptSpender::sign(std::shared_ptr<SignerProxy> proxy)
{
   auto signStack = [proxy](
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

               sigItem->sig_ = std::move(
                  proxy->sign(sigItem->script_, sigItem->pubkey_, isSW));
               break;
            }

            case StackItemType::MultiSig:
            {
               auto msEntryPtr =
                  std::dynamic_pointer_cast<StackItem_MultiSig>(stackItem);
               if (msEntryPtr == nullptr) {
                  throw SpenderException("invalid ms stack entry");
               }

               for (unsigned i=0; i < msEntryPtr->pubkeyVec_.size(); i++) {
                  if (msEntryPtr->sigs_.find(i) != msEntryPtr->sigs_.end()) {
                     continue;
                  }

                  const auto& pubkey = msEntryPtr->pubkeyVec_[i];
                  try {
                     auto sig = proxy->sign(msEntryPtr->script_, pubkey, isSW);
                     msEntryPtr->sigs_.emplace(i, std::move(sig));
                     if (msEntryPtr->sigs_.size() >= msEntryPtr->m_) {
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

////////////////////////////////////////////////////////////////////////////////
bool Signing::ScriptSpender::isSegWit() const
{
   switch (legacyStatus_)
   {
   case SpenderStatus::Empty:
      return true; //empty legacy input means sw

   case SpenderStatus::Resolved:
   {
      //resolved legacy status could mean nested sw
      if (segwitStatus_ >= SpenderStatus::Resolved)
         return true;
   }

   default:
      break;
   }
   
   return false;
}

////////////////////////////////////////////////////////////////////////////////
void Signing::ScriptSpender::injectSignature(
   SecureBinaryData& sig, unsigned sigId)
{
   //sanity checks
   if (!isResolved())
      throw std::runtime_error("cannot inject sig into unresolved spender");

   if (isSigned())
      throw std::runtime_error("spender is already signed!");

   std::map<unsigned, std::shared_ptr<StackItem>>* stackPtr = nullptr;

   //grab the stack carrying the sig(s)
   if (isSegWit())
      stackPtr = &witnessStack_;
   else
      stackPtr = &legacyStack_;

   //find the stack sig object
   bool injected = false;
   for (auto& stackItemPair : *stackPtr)
   {
      auto& stackItem = stackItemPair.second;
      switch (stackItem->type())
      {
      case StackItemType::Sig:
      {
         if (stackItem->isValid())
            throw SpenderException("stack sig entry already filled");

         auto stackItemSig = std::dynamic_pointer_cast<StackItem_Sig>(stackItem);
         if (stackItemSig == nullptr)
            throw SpenderException("unexpected stack item type");

         stackItemSig->injectSig(sig);
         injected = true;

         break;
      }

      case StackItemType::MultiSig:
      {
         if (sigId == UINT32_MAX)
            throw SpenderException("unset sig id");
         
         auto msEntryPtr =
            std::dynamic_pointer_cast<StackItem_MultiSig>(stackItem);
         if (msEntryPtr == nullptr)
            throw SpenderException("invalid ms stack entry");

         msEntryPtr->setSig(sigId, sig);
         injected = true;

         break;
      }

      default:
         break;
      }
   }

   if (!injected)
      throw SpenderException("failed to find sig entry in stack");

   processStacks();
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef Signing::ScriptSpender::getRedeemScriptFromStack(
   const std::map<unsigned, std::shared_ptr<StackItem>>* stackPtr) const
{
   if (stackPtr == nullptr)
      return BinaryDataRef();

   std::shared_ptr<StackItem> firstPushData;

   //look for redeem script from sig stack items
   for (auto stackPair : *stackPtr)
   {
      auto stackItem = stackPair.second;
      switch (stackItem->type())
      {
      case StackItemType::PushData:
      {
         //grab first push data entry in stack
         if (firstPushData == nullptr)
            firstPushData = stackItem;
         break;
      }

      case StackItemType::Sig:
      {
         auto sig = std::dynamic_pointer_cast<StackItem_Sig>(stackItem);
         if (sig == nullptr)
            break;

         return sig->script_.getRef();
      }

      case StackItemType::MultiSig:
      {
         auto msig = std::dynamic_pointer_cast<StackItem_MultiSig>(stackItem);
         if (msig == nullptr)
            break;

         return msig->script_.getRef();
      }

      default:
         break;
      }
   }

   //if we couldn't find sig entries, let's try the first push data entry
   if (firstPushData == nullptr || !firstPushData->isValid())
      return BinaryDataRef();

   auto pushdata = std::dynamic_pointer_cast<StackItem_PushData>(firstPushData);
   if (pushdata == nullptr)
      return BinaryDataRef();

   return pushdata->data_;
}

////////////////////////////////////////////////////////////////////////////////
std::map<BinaryData, BinaryData> Signing::ScriptSpender::getPartialSigs() const
{
   const std::map<unsigned, std::shared_ptr<StackItem>>* stackPtr = nullptr;
   if (!isSegWit())
      stackPtr = &legacyStack_;
   else
      stackPtr = &witnessStack_;

   //look for multsig stack entry
   std::shared_ptr<StackItem_MultiSig> stackItemMultisig = nullptr;
   for (const auto& stackObj : *stackPtr)
   {
      auto stackItem = stackObj.second;
      if (stackItem->type() == StackItemType::MultiSig)
      {
         stackItemMultisig =
            std::dynamic_pointer_cast<StackItem_MultiSig>(stackItem);
         break;
      }
   }

   if (stackItemMultisig == nullptr)
      return {};

   std::map<BinaryData, BinaryData> sigMap;
   for (const auto& sigPair : stackItemMultisig->sigs_)
   {
      if (sigPair.first > stackItemMultisig->pubkeyVec_.size())
      {
         LOGWARN << "sig index out of bounds";
         break;
      }

      const auto& pubkey = stackItemMultisig->pubkeyVec_[sigPair.first];
      sigMap.emplace(pubkey, sigPair.second);
   }

   return sigMap;
}

////////////////////////////////////////////////////////////////////////////////
std::map<unsigned, Armory::Signing::KeyAndSig>
Signing::ScriptSpender::getRelevantPubkeys() const
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
               pubkeyMap.emplace(0, KeyAndSig{ sig->pubkey_, sig->sig_ });
               return pubkeyMap;
            }

            case StackItemType::MultiSig:
            {
               auto msig = std::dynamic_pointer_cast<StackItem_MultiSig>(stackItem);
               if (stackItem == nullptr) {
                  break;
               }

               std::map<unsigned, KeyAndSig> pubkeyMap;
               for (unsigned i=0; i<msig->pubkeyVec_.size(); i++) {
                  const auto& pubkey = msig->pubkeyVec_[i];
                  pubkeyMap.emplace(i, KeyAndSig{ pubkey, {} });

                  auto sigIter = msig->sigs_.find(i);
                  if (sigIter != msig->sigs_.end()) {
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

////////////////////////////////////////////////////////////////////////////////
void Signing::ScriptSpender::toPSBT(BinaryWriter& bw) const
{
   //supporting tx or utxo
   bool hasSupportingOutput = false;
   if (haveSupportingTx())
   {
      //key length
      bw.put_uint8_t(1);
      
      //supporting tx key
      bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_NON_WITNESS_UTXO);

      //tx
      const auto& supportingTx = getSupportingTx();
      bw.put_var_int(supportingTx.getSize());
      bw.put_BinaryData(supportingTx.getPtr(), supportingTx.getSize());
      
      hasSupportingOutput = true;
   }
   else if (isSegWit() && utxo_.isInitialized())
   {
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
      for (auto& sigPair : partialSigs)
      {
         bw.put_var_int(sigPair.first.getSize() + 1);
         bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_PARTIAL_SIG);
         bw.put_BinaryData(sigPair.first);

         bw.put_var_int(sigPair.second.getSize());
         bw.put_BinaryData(sigPair.second);
      }
   }

   //sig hash, conditional on utxo/prevTx presence
   if (hasSupportingOutput && !isSigned())
   {
      bw.put_uint8_t(1);
      bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_SIGHASH_TYPE);

      bw.put_uint8_t(4);
      bw.put_uint32_t((uint32_t)sigHashType_);
   }

   //redeem script
   if (!isSigned())
   {
      auto redeemScript = getRedeemScriptFromStack(&legacyStack_);
      if (!redeemScript.empty())
      {
         bw.put_uint8_t(1);
         bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_REDEEM_SCRIPT);
         
         bw.put_var_int(redeemScript.getSize());
         bw.put_BinaryDataRef(redeemScript);
      }
   }

   //witness script
   if (isSegWit())
   {
      auto witnessScript = getRedeemScriptFromStack(&witnessStack_);
      if (!witnessScript.empty())
      {
         bw.put_uint8_t(1);
         bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_WITNESS_SCRIPT);
         
         bw.put_var_int(witnessScript.getSize());
         bw.put_BinaryDataRef(witnessScript);
      }
   }

   if (!isSigned())
   {
      //pubkeys
      for (auto& bip32Path : bip32Paths_)
      {
         if (!bip32Path.second.isValid())
            continue;

         bw.put_uint8_t(34);
         bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_BIP32_DERIVATION);
         bw.put_BinaryData(bip32Path.first);

         //path
         bip32Path.second.toPSBT(bw);
      }
   }
   else
   {
      //scriptSig
      auto finalizedInputScript = getAvailableInputScript();
      if (!finalizedInputScript.empty())
      {
         bw.put_uint8_t(1);
         bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_FINAL_SCRIPTSIG);

         bw.put_var_int(finalizedInputScript.getSize());
         bw.put_BinaryData(finalizedInputScript);
      }

      auto finalizedWitnessData = getFinalizedWitnessData();
      if (!finalizedWitnessData.empty())
      {
         bw.put_uint8_t(1);
         bw.put_uint8_t(PSBT::ENUM_INPUT::PSBT_IN_FINAL_SCRIPTWITNESS);

         bw.put_var_int(finalizedWitnessData.getSize());
         bw.put_BinaryData(finalizedWitnessData);
      }
   }

   //proprietary data
   for (auto& data : prioprietaryPSBTData_)
   {
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

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<Signing::ScriptSpender> Signing::ScriptSpender::fromPSBT(
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
               throw PSBTDeserializationError("null txmap");
            }

            //supporting tx, key has to be 1 byte long
            if (key.getSize() != 1) {
               throw PSBTDeserializationError("unvalid supporting tx key len");
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
               throw PSBTDeserializationError("unvalid utxo key len");
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
               throw PSBTDeserializationError("unvalid sighash key len");
            }
            if (val.getSize() != 4) {
               throw PSBTDeserializationError("invalid sighash val length");
            }

            memcpy(&sigHash, val.getPtr(), sizeof(uint32_t));
            break;
         }

         case PSBT::ENUM_INPUT::PSBT_IN_REDEEM_SCRIPT:
         {
            if (key.getSize() != 1) {
               throw PSBTDeserializationError("unvalid redeem script key len");
            }
            redeemScript = val;
            break;
         }

         case PSBT::ENUM_INPUT::PSBT_IN_WITNESS_SCRIPT:
         {
            if (key.getSize() != 1) {
               throw PSBTDeserializationError("unvalid witness script key len");
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
               throw PSBTDeserializationError("bip32 path collision");
            }
            break;
         }

         case PSBT::ENUM_INPUT::PSBT_IN_FINAL_SCRIPTSIG:
         {
            if (key.getSize() != 1) {
               throw PSBTDeserializationError("unvalid finalized input script key len");
            }
            finalRedeemScript = val;
            break;
         }

         case PSBT::ENUM_INPUT::PSBT_IN_FINAL_SCRIPTWITNESS:
         {
            if (key.getSize() != 1) {
               throw PSBTDeserializationError("unvalid finalized witness script key len");
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
            throw PSBTDeserializationError("unexpected txin key");
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
               throw PSBTDeserializationError(
                  "have bip32path for unrelated pubkey");
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
            throw PSBTDeserializationError("couldn't inject sigs");
         }
      }

      spender->setSigHashType((SIGHASH_TYPE)sigHash);
   }

   spender->prioprietaryPSBTData_ = std::move(prioprietaryPSBTData);
   return spender;
}

////////////////////////////////////////////////////////////////////////////////
void Signing::ScriptSpender::setTxMap(
   std::shared_ptr<std::map<BinaryData, Tx>> txMap)
{
   txMap_ = txMap;
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::ScriptSpender::setSupportingTx(BinaryDataRef rawTx)
{
   if (rawTx.empty())
      return false;

   try
   {
      Tx tx(rawTx);
      return setSupportingTx(std::move(tx));
   }
   catch (const std::exception&)
   {}

   return false;
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::ScriptSpender::setSupportingTx(Tx supportingTx)
{
   /*
   Returns true if the supporting tx is relevant to this spender, false 
   otherwise
   */
   if (supportingTx.getThisHash() != getOutputHash())
      return false;

   auto insertIter = txMap_->emplace(
      supportingTx.getThisHash(), std::move(supportingTx));
   
   return insertIter.second;
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::ScriptSpender::haveSupportingTx() const
{
   if (txMap_ == nullptr)
      return false;

   try
   {        
      auto hash = getOutputHash();
      auto iter = txMap_->find(hash);
      return (iter != txMap_->end());
   }
   catch (const std::exception&)
   {}

   return false;
}

////////////////////////////////////////////////////////////////////////////////
const Tx& Signing::ScriptSpender::getSupportingTx() const
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

////////////////////////////////////////////////////////////////////////////////
bool Signing::ScriptSpender::canBeResolved() const
{
   if (utxo_.isInitialized()) {
      return true;
   }
   if (outpoint_.getSize() != 36) {
      return false;
   }
   return haveSupportingTx();
}

////////////////////////////////////////////////////////////////////////////////
uint64_t Signing::ScriptSpender::getValue() const
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

////////////////////////////////////////////////////////////////////////////////
void Signing::ScriptSpender::seedResolver(std::shared_ptr<ResolverFeed> feedPtr,
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

////////////////////////////////////////////////////////////////////////////////
void Signing::ScriptSpender::prettyPrint(std::ostream& os) const
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

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// Signer
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
BinaryDataRef Signing::Signer::getSerializedOutputScripts() const
{
   if (serializedOutputs_.empty())
   {
      BinaryWriter bw;
      for (auto& recipient : getRecipientVector())
      {
         auto&& serializedOutput = recipient->getSerializedScript();
         bw.put_BinaryData(serializedOutput);
      }

      serializedOutputs_ = std::move(bw.getData());
   }

   return serializedOutputs_.getRef();
}

////////////////////////////////////////////////////////////////////////////////
std::vector<Signing::TxInData> Signing::Signer::getTxInsData() const
{
   std::vector<TxInData> tidVec;

   for (auto& spender : spenders_)
   {
      TxInData tid;
      tid.outputHash_ = spender->getOutputHash();
      tid.outputIndex_ = spender->getOutputIndex();
      tid.sequence_ = spender->getSequence();

      tidVec.emplace_back(std::move(tid));
   }

   return tidVec;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::Signer::getSubScript(unsigned index) const
{
   auto spender = getSpender(index);
   return spender->getOutputScript();
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef Signing::Signer::getWitnessData(unsigned index) const
{
   auto spender = getSpender(index);
   return spender->getFinalizedWitnessData();
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::Signer::isInputSW(unsigned index) const
{
   auto spender = getSpender(index);
   return spender->isSegWit();
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::Signer::serializeAllOutpoints() const
{
   BinaryWriter bw;
   for (auto& spender : spenders_)
   {
      bw.put_BinaryDataRef(spender->getOutpoint());
   }

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::Signer::serializeAllSequences() const
{
   BinaryWriter bw;
   for (auto& spender : spenders_)
   {
      bw.put_uint32_t(spender->getSequence());
   }

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef Signing::Signer::getOutpoint(unsigned index) const
{
   if (index >= spenders_.size()) {
      throw std::runtime_error("invalid spender index");
   }
   return spenders_[index]->getOutpoint();
}

////////////////////////////////////////////////////////////////////////////////
uint64_t Signing::Signer::getOutpointValue(unsigned index) const
{
   if (index >= spenders_.size()) {
      throw std::runtime_error("invalid spender index");
   }
   return spenders_[index]->getValue();
}

////////////////////////////////////////////////////////////////////////////////
unsigned Signing::Signer::getTxInSequence(unsigned index) const
{
   if (index >= spenders_.size()) {
      throw std::runtime_error("invalid spender index");
   }
   return spenders_[index]->getSequence();
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::sign()
{ 
   /***
   About the SegWit perma flagging:
   Armory SegWit support was implemented prior to the soft fork activation
   (April 2016). At the time it was uncertain whether SegWit would be activated.

   The chain was also getting hardforked to a ruleset specifically blocking
   SegWit (Bcash).

   As a result, Armory had a responsibility to allow users to spend the
   airdropped coins. Since Bcash does not support SegWit and such scripts are
   otherwise anyone-can-spend, there had to be a toggle for this feature,
   which applies to script resolution rules as well.

   Since SegWit is a done deal and Armory has no pretention to support Bcash,
   SW can now be on by default, which reduces potential client side or unit
   test snafus.
   ***/

   //perma flag for segwit verification
   flags_ |= SCRIPT_VERIFY_SEGWIT;

   /* sanity checks begin */

   //sizes
   if (spenders_.size() == 0)
      throw std::runtime_error("tx has no spenders");

   auto recVector = getRecipientVector();
   if (recVector.size() == 0)
      throw std::runtime_error("tx has no recipients");

   /*
   Try to check input value vs output value. We're not guaranteed to
   have this information, since we may be partially signing this
   transaction. In that case, skip this step
   */
   try {
      uint64_t inputVal = 0;
      for (unsigned i=0; i < spenders_.size(); i++) {
         inputVal += spenders_[i]->getValue();
      }

      uint64_t spendVal = 0;
      for (auto& recipient : recVector) {
         spendVal += recipient->getValue();
      }

      if (inputVal < spendVal) {
         throw std::runtime_error("invalid spendVal");
      }
   } catch (const SpenderException&) {
      //missing input value data, skip the spendVal check
   }

   /* sanity checks end */

   //resolve
   auto resolvedSpenderIds = resolvePublicData();

   //sign sig stack entries in each spender
   for (unsigned i=0; i < spenders_.size(); i++) {
      auto& spender = spenders_[i];
      if (!spender->isResolved() || spender->isSigned()) {
         continue;
      }

      bool seedLegacyAssets = false;
      if (resolvedSpenderIds.find(i) == resolvedSpenderIds.end()) {
         seedLegacyAssets = true;
      }

      spender->seedResolver(resolverPtr_, seedLegacyAssets);
      auto proxy = std::make_shared<SignerProxyFromSigner>(
         this, i, resolverPtr_);
      spender->sign(proxy);
   }
}

////////////////////////////////////////////////////////////////////////////////
std::set<unsigned> Signing::Signer::resolvePublicData()
{
   std::set<unsigned> resolvedSpenderIds;

   //run through each spenders
   for (unsigned i=0; i<spenders_.size(); i++)
   {
      auto& spender = spenders_[i];
      if (spender->isResolved())
         continue;

      if (!spender->canBeResolved())
         continue;

      //resolve spender script
      StackResolver resolver(
         spender->getOutputScript(),
         resolverPtr_);

      //check Script.h for signer flags
      resolver.setFlags(flags_);

      try
      {
         spender->parseScripts(resolver);
      }
      catch (const std::exception&)
      {}

      auto spenderBip32Paths = spender->getBip32Paths();
      for (const auto& pathPair : spenderBip32Paths)
      {
         const auto& assetPath = pathPair.second;
         if (assetPath.hasRoot())
            addBip32Root(assetPath.getRoot());
      }

      resolvedSpenderIds.emplace(i);
   }

   if (resolverPtr_ == nullptr)
      return resolvedSpenderIds;

   for (auto& recipient : getRecipientVector())
   {
      const auto& serializedOutput = recipient->getSerializedScript();
      BinaryRefReader brr(serializedOutput);
      brr.advance(8);
      auto len = brr.get_var_int();
      auto scriptRef = brr.get_BinaryDataRef(len);

      auto pubKeys = Signer::getPubkeysForScript(scriptRef, resolverPtr_);
      for (const auto& pubKeyPair : pubKeys)
      {
         try
         {
            auto bip32path =
               resolverPtr_->resolveBip32PathForPubkey(pubKeyPair.second);
            if (!bip32path.isValid())
               continue;

            recipient->addBip32Path(bip32path);
         }
         catch (const std::exception&)
         {
            continue;
         }
      }
   }

   return resolvedSpenderIds;
}

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData Signing::Signer::signScript(
   BinaryDataRef script,
   const SecureBinaryData& privKey,
   std::shared_ptr<SigHashData> SHD, unsigned index)
{
   auto spender = spenders_[index];

   auto hashToSign = SHD->getDataForSigHash(
      spender->getSigHashType(), *this,
      script, index);

#ifdef SIGNER_DEBUG
   auto&& pubkey = CryptoECDSA().ComputePublicKey(privKey);
   LOGWARN << "signing for: ";
   LOGWARN << "   pubkey: " << pubkey.toHexStr();

   auto&& msghash = BtcUtils::getHash256(dataToHash);
   LOGWARN << "   message: " << dataToHash.toHexStr();
#endif

   return CryptoECDSA().SignData(hashToSign, privKey, false);
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<Signing::ScriptSpender> Signing::Signer::getSpender(
   unsigned index) const
{
   if (index > spenders_.size())
      throw ScriptException("invalid spender index");

   return spenders_[index];
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<Signing::ScriptRecipient> Signing::Signer::getRecipient(
   unsigned index) const
{
   auto recVector = getRecipientVector();
   if (index >= recVector.size())
      throw ScriptException("invalid spender index");

   return recVector[index];
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef Signing::Signer::serializeSignedTx() const
{
   if (!serializedSignedTx_.empty()) {
      return serializedSignedTx_.getRef();
   }

   //version
   BinaryWriter bw;
   bw.put_uint32_t(version_);

   bool isSW = isSegWit();
   if (isSW) {
      //marker and flag
      bw.put_uint8_t(0);
      bw.put_uint8_t(1);
   }

   //txin count
   if (spenders_.empty()) {
      throw std::runtime_error("no spenders");
   }
   bw.put_var_int(spenders_.size());

   //txins
   for (auto& spender : spenders_) {
      bw.put_BinaryData(spender->getSerializedInput(true, false));
   }

   //txout count
   auto recVector = getRecipientVector();
   if (recVector.empty()) {
      throw std::runtime_error("no recipients");
   }
   bw.put_var_int(recVector.size());

   //txouts
   for (auto& recipient : recVector) {
      bw.put_BinaryData(recipient->getSerializedScript());
   }

   if (isSW) {
      //witness data
      for (auto& spender : spenders_) {
         BinaryDataRef witnessRef = spender->getFinalizedWitnessData();

         //account for empty witness data
         if (witnessRef.empty()) {
            bw.put_uint8_t(0);
         } else {
            bw.put_BinaryDataRef(witnessRef);
         }
      }
   }

   //lock time
   bw.put_uint32_t(lockTime_);
   serializedSignedTx_ = std::move(bw.getData());
   return serializedSignedTx_.getRef();
}

////////////////////////////////////////////////////////////////////////////////
BinaryDataRef Signing::Signer::serializeUnsignedTx(bool loose)
{
   if (serializedUnsignedTx_.getSize() != 0)
      return serializedUnsignedTx_.getRef();

   resolvePublicData();

   BinaryWriter bw;

   //version
   bw.put_uint32_t(version_);

   bool isSW = isSegWit();
   if (isSW)
   {
      //marker and flag
      bw.put_uint8_t(0);
      bw.put_uint8_t(1);
   }

   //txin count
   if (spenders_.size() == 0)
   {
      if (!loose)
         throw std::runtime_error("no spenders");
   }

   bw.put_var_int(spenders_.size());

   //txins
   for (auto& spender : spenders_)
      bw.put_BinaryData(spender->getSerializedInput(false, loose));

   //txout count
   auto recVector = getRecipientVector();
   if (recVector.size() == 0)
   {
      if (!loose)
         throw std::runtime_error("no recipients");
   }

   bw.put_var_int(recVector.size());

   //txouts
   for (auto& recipient : recVector)
      bw.put_BinaryData(recipient->getSerializedScript());

   //no witness data for unsigned transactions
   if (isSW)
   {
      for (unsigned i=0; i < spenders_.size(); i++)
         bw.put_uint8_t(0);
   }

   //lock time
   bw.put_uint32_t(lockTime_);

   serializedUnsignedTx_ = std::move(bw.getData());

   return serializedUnsignedTx_.getRef();
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::Signer::serializeAvailableResolvedData() const
{
   try
   {
      auto&& serTx = serializeSignedTx();
      return serTx;
   }
   catch (const std::exception&)
   {}
   
   BinaryWriter bw;

   //version
   bw.put_uint32_t(version_);

   bool isSW = isSegWit();
   if (isSW)
   {
      //marker and flag
      bw.put_uint8_t(0);
      bw.put_uint8_t(1);
   }

   //txin count
   bw.put_var_int(spenders_.size());

   //txins
   for (auto& spender : spenders_)
   {
      try
      {
         bw.put_BinaryData(spender->getSerializedInput(false, false));
      }
      catch (const std::exception&)
      {
         bw.put_BinaryData(spender->getEmptySerializedInput());
      }
   }

   //txout count
   auto recVector = getRecipientVector();
   bw.put_var_int(recVector.size());

   //txouts
   for (auto& recipient : recVector)
      bw.put_BinaryData(recipient->getSerializedScript());

   if (isSW)
   {
      //witness data
      for (auto& spender : spenders_)
      {
         BinaryData witnessData = spender->serializeAvailableWitnessData();

         //account for empty witness data
         if (witnessData.getSize() == 0)
            bw.put_uint8_t(0);
         else
            bw.put_BinaryData(witnessData);
      }
   }

   //lock time
   bw.put_uint32_t(lockTime_);

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<Signing::SigHashData> Signing::Signer::getSigHashDataForSpender(
   bool sw) const
{
   std::shared_ptr<SigHashData> SHD;
   if (sw)
   {
      if (sigHashDataObject_ == nullptr)
         sigHashDataObject_ = std::make_shared<SigHashDataSegWit>();

      SHD = sigHashDataObject_;
   }
   else
   {
      SHD = std::make_shared<SigHashDataLegacy>();
   }

   return SHD;
}

////////////////////////////////////////////////////////////////////////////////
std::unique_ptr<Signing::TransactionVerifier> Signing::Signer::getVerifier(
   std::shared_ptr<BCTX> bctx,
   std::map<BinaryData, std::map<unsigned, UTXO>>& utxoMap)
{
   return std::make_unique<TransactionVerifier>(*bctx, utxoMap);
}

////////////////////////////////////////////////////////////////////////////////
Signing::TxEvalState Signing::Signer::verify(const BinaryData& rawTx,
   std::map<BinaryData, std::map<unsigned, UTXO>>& utxoMap, 
   unsigned flags, bool strict)
{
   auto bctx = BCTX::parse(rawTx);

   //setup verifier
   auto tsv = getVerifier(bctx, utxoMap);
   auto tsvFlags = tsv->getFlags();
   tsvFlags |= flags;
   tsv->setFlags(tsvFlags);

   return tsv->evaluateState(strict);
}

////////////////////////////////////////////////////////////////////////////////
Signing::TxEvalState Signing::Signer::evaluateSignedState(void) const
{
   auto&& txdata = serializeAvailableResolvedData();

   std::map<BinaryData, std::map<unsigned, UTXO>> utxoMap;
   unsigned flags = 0;
   for (auto& spender : spenders_)
   {
      auto& indexMap = utxoMap[spender->getOutputHash()];
      indexMap[spender->getOutputIndex()] = spender->getUtxo();

      flags |= spender->getFlags();
   }

   return verify(txdata, utxoMap, flags, true);
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::Signer::verify() const
{
   //serialize signed tx
   BinaryData txdata;
   try {
      txdata = std::move(serializeSignedTx());
   } catch (const std::exception& e) {
      return false;
   }

   std::map<BinaryData, std::map<unsigned, UTXO>> utxoMap;

   //gather utxos and spender flags
   unsigned flags = 0;
   for (auto& spender : spenders_) {
      auto& indexMap = utxoMap[spender->getOutputHash()];
      indexMap[spender->getOutputIndex()] = spender->getUtxo();
      flags |= spender->getFlags();
   }

   auto evalState = verify(txdata, utxoMap, flags);
   return evalState.isValid();
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::Signer::verifyRawTx(const BinaryData& rawTx,
   const std::map<BinaryData, std::map<unsigned, BinaryData>>& rawUTXOs)
{
   std::map<BinaryData, std::map<unsigned, UTXO>> utxoMap;

   //deser utxos
   for (auto& utxoPair : rawUTXOs)
   {
      std::map<unsigned, UTXO> idMap;
      for (auto& rawUtxoPair : utxoPair.second)
      {
         UTXO utxo;
         utxo.unserializeRaw(rawUtxoPair.second);
         idMap.insert(std::move(std::make_pair(
            rawUtxoPair.first, std::move(utxo))));
      }

      utxoMap.insert(move(make_pair(utxoPair.first, std::move(idMap))));
   }

   auto&& evalState =
      verify(rawTx, utxoMap, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_SEGWIT);

   return evalState.isValid();
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::Signer::serializeState() const
{
   capnp::MallocMessageBuilder message;
   auto capnMsg = message.initRoot<Codec::Signer::Signer>();

   capnMsg.setFlags(flags_);
   capnMsg.setTxVersion(version_);
   capnMsg.setLocktime(lockTime_);

   unsigned i=0;
   auto capnSpenders = capnMsg.initSpenders(spenders_.size());
   for (auto& spender : spenders_) {
      auto capnSpender = capnSpenders[i++];
      Serializer::spenderToCapn(spender, capnSpender);
   }

   unsigned recipientCount = 0;
   for (auto& group : recipients_) {
      recipientCount += group.second.size();
   }

   i=0;
   auto capnRecipients = capnMsg.initRecipients(recipientCount);
   for (auto& group : recipients_) {
      for (auto& recipient : group.second) {
         auto capnRecipient = capnRecipients[i++];
         Serializer::recipientToCapn(recipient, group.first, capnRecipient);
      }
   }

   if (supportingTxMap_ != nullptr && !supportingTxMap_->empty()) {
      i=0;
      auto capnTxns = capnMsg.initSupportingTxs(supportingTxMap_->size());
      for (const auto& supportingTx : *supportingTxMap_) {
         capnTxns.set(i, capnp::Data::Builder(
            (uint8_t*)supportingTx.second.getPtr(), supportingTx.second.getSize()
         ));
      }
   }

   i=0;
   auto capnRoots = capnMsg.initBip32Roots(bip32PublicRoots_.size());
   for (auto& bip32PublicRoot : bip32PublicRoots_)
   {
      auto capnRoot = capnRoots[i++];
      auto& rootPtr = bip32PublicRoot.second;

      capnRoot.setXpub(rootPtr->getXPub());
      capnRoot.setFingerprint(rootPtr->getSeedFingerprint());

      const auto& path = rootPtr->getPath();
      auto capnPaths = capnRoot.initPath(path.size());
      for (unsigned y=0; y<path.size(); y++) {
         capnPaths.set(y, path[y]);
      }
   }

   auto flat = capnp::messageToFlatArray(message);
   auto bytes = flat.asBytes();
   return BinaryData(bytes.begin(), bytes.end());
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::deserializeState(const BinaryDataRef& ref)
{
   Signer theSigner;
   Deserializer::capnToSigner(theSigner, ref);
   theSigner.fromType_ = SignerStringFormat::TxSigCollect_Modern;

   merge(theSigner);
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::merge(const Signer& rhs)
{
   version_ = rhs.version_;
   lockTime_ = rhs.lockTime_;
   flags_ |= rhs.flags_;

   auto find_spender = [this](std::shared_ptr<ScriptSpender> obj)
      ->std::shared_ptr<ScriptSpender>
   {
      for (auto spd : this->spenders_)
      {
         if (*spd == *obj)
            return spd;
      }

      return nullptr;
   };

   auto find_recipient = [this](
      std::shared_ptr<ScriptRecipient> obj, unsigned groupid)
      ->std::shared_ptr<ScriptRecipient>
   {
      auto groupIter = this->recipients_.find(groupid);
      if (groupIter == this->recipients_.end())
         return nullptr;

      auto& scriptHash = obj->getSerializedScript();
      for (auto& rec : groupIter->second)
      {
         if (scriptHash == rec->getSerializedScript())
            return rec;
      }

      return nullptr;
   };

   //Merge new signer with this. As a general rule, the added entries are all 
   //pushed back.
   supportingTxMap_->insert(
      rhs.supportingTxMap_->begin(), rhs.supportingTxMap_->end());

   //merge spender
   for (auto& spender : rhs.spenders_)
   {
      auto local_spender = find_spender(spender);
      if (local_spender != nullptr)
      {
         local_spender->merge(*spender);
         if (!local_spender->verifyEvalState(flags_))
            throw SignerDeserializationError("merged spender has inconsistent state");
      }
      else
      {
         auto newSpender = std::make_shared<ScriptSpender>(*spender);
         newSpender->txMap_ = supportingTxMap_;
         spenders_.push_back(newSpender);
         if (!spenders_.back()->verifyEvalState(flags_))
            throw SignerDeserializationError("unserialized spender has inconsistent state");
      }
   }


   /*
   Recipients are told apart by their group id. Within a group, they are 
   differentiated by their script hash. Collisions within a group are 
   not tolerated.
   */

   for (auto& group : rhs.recipients_)
   {
      for (auto& recipient : group.second)
      {
         auto local_recipient = find_recipient(recipient, group.first);

         if (local_recipient == nullptr)
            addRecipient(recipient, group.first);
         else
            local_recipient->merge(recipient);
      }
   }

   //merge bip32 roots
   bip32PublicRoots_.insert(
      rhs.bip32PublicRoots_.begin(), rhs.bip32PublicRoots_.end());
   matchAssetPathsWithRoots();
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::Signer::serializeState_Legacy() const
{
   if (isSegWit()) {
      throw std::runtime_error("SW txs cannot be serialized to legacy format");
   }

   BinaryWriter bw;
   auto magicBytes = Config::BitcoinSettings::getMagicBytes();
   bw.put_BinaryData(magicBytes);
   bw.put_uint32_t(0); //4 empty bytes

   //inputs
   bw.put_var_int(spenders_.size());
   for (const auto& spender : spenders_) {
      BinaryWriter bwTxIn;
      bwTxIn.put_uint32_t(USTXI_VER_LEGACY);
      bwTxIn.put_BinaryData(magicBytes);
      bwTxIn.put_BinaryData(spender->getOutpoint());

      //supporting tx, legacy format needs all supporting transactions
      const auto& tx = spender->getSupportingTx();
      bwTxIn.put_var_int(tx.getSize());
      bwTxIn.put_BinaryData(tx.serialize());

      //p2sh map BASE_SCRIPT
      if (!spender->isP2SH()) {
         bwTxIn.put_var_int(0);
      } else {
         //we assume the spender is resolved since it's flagged as p2sh
         if (spender->isSigned()) {
            //if the spender is signed then the stack is empty, we'll have
            //to retrieve the base script from the finalized stack. Let's
            //keep it simple for now and look at it later.
            throw std::runtime_error(
               "Legacy signing across multiple wallets not supported yet");
         }

         auto script = spender->getRedeemScriptFromStack(
            &spender->legacyStack_);
         bwTxIn.put_var_int(script.getSize());
         bwTxIn.put_BinaryData(script);
      }

      //contribID & label (lockbox fields, leaving them empty)
      bwTxIn.put_var_int(0);
      bwTxIn.put_var_int(0);

      //sequence
      bwTxIn.put_uint32_t(spender->getSequence());

      //key & sig list
      auto keysAndSigs = spender->getRelevantPubkeys();
      bwTxIn.put_var_int(keysAndSigs.size());

      for (const auto& pubkeyIt : keysAndSigs) {
         //pubkey
         bwTxIn.put_var_int(pubkeyIt.second.pubkey.getSize());
         bwTxIn.put_BinaryData(pubkeyIt.second.pubkey);

         //sig, skipping for now
         bwTxIn.put_var_int(pubkeyIt.second.sig.getSize());
         bwTxIn.put_BinaryData(pubkeyIt.second.sig);

         //wallet locator, skipping for now
         bwTxIn.put_var_int(0);
      }

      //rest of p2sh map, for nested SW
      //we'll ignore this as we dont allow legacy ser for SW txs

      //finalize
      bw.put_var_int(bwTxIn.getSize());
      bw.put_BinaryData(bwTxIn.getData());
   }

   //outputs
   std::list<BinaryWriter> serializedRecipients;
   for (const auto& recipientList : recipients_) {
      BinaryWriter bwTxOut;
      for (const auto& recipient : recipientList.second) {
         bwTxOut.put_uint32_t(USTXO_VER_LEGACY);
         bwTxOut.put_BinaryData(magicBytes);

         auto output = recipient->getSerializedScript();
         auto script = output.getSliceRef(8, output.getSize()-8);

         bwTxOut.put_BinaryData(script);
         bwTxOut.put_uint64_t(recipient->getValue());

         //p2sh script (ignore for now)
         bwTxOut.put_var_int(0);

         //wltLocator
         bwTxOut.put_var_int(0);

         //auth method & data, ignore
         bwTxOut.put_var_int(0);
         bwTxOut.put_var_int(0);

         //contrib id & label (lockbox stuff, ignore)
         bwTxOut.put_var_int(0);
         bwTxOut.put_var_int(0);
      
         //add to list
         serializedRecipients.emplace_back(std::move(bwTxOut));
      }
   }

   //finalize outputs
   bw.put_var_int(serializedRecipients.size());
   for (const auto& rec : serializedRecipients) {
      bw.put_var_int(rec.getSize());
      bw.put_BinaryData(rec.getData());
   }

   //locktime
   bw.put_uint32_t(lockTime_);

   //done
   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::deserializeState_Legacy(const BinaryDataRef& ref)
{
   BinaryRefReader brr(ref);

   auto magicBytes = Config::BitcoinSettings::getMagicBytes();
   auto magicBytesRef = brr.get_BinaryDataRef(4);
   if (magicBytes != magicBytesRef) {
      throw SignerDeserializationError("legacy deser: magic bytes mismatch!");
   }

   auto emptyBytes = brr.get_uint32_t();
   if (emptyBytes != 0) {
      throw SignerDeserializationError("legacy deser: missing empty bytes");
   }

   auto spenderCount = brr.get_var_int();
   for (unsigned i=0; i<spenderCount; i++) {
      auto spenderDataSize = brr.get_var_int();
      auto spenderData = brr.get_BinaryDataRef(spenderDataSize);
      BinaryRefReader brrSpender(spenderData);

      //version
      auto version = brrSpender.get_uint32_t();
      if (version != USTXI_VER_LEGACY) {
         throw SignerDeserializationError(
            "legacy deser: ustxi version mismatch");
      }

      //magic bytes
      auto ustxi_magic = brrSpender.get_BinaryDataRef(4);
      if (ustxi_magic != magicBytes) {
         throw SignerDeserializationError(
            "legacy deser: ustxi magic bytes mismatch!");
      }

      //outpoint
      auto outpointRef = brrSpender.get_BinaryDataRef(36);

      //supporting tx
      auto txSize = brrSpender.get_var_int();
      auto supportingTxRaw = brrSpender.get_BinaryDataRef(txSize);

      //p2sh preimage
      auto preimageSize = brrSpender.get_var_int();
      auto p2shPreimage = brrSpender.get_BinaryDataRef(preimageSize);

      //contribID & label
      auto contribIdSz = brrSpender.get_var_int();
      if (contribIdSz != 0) {
         brrSpender.advance(contribIdSz);
      }

      auto labelIdSz = brrSpender.get_var_int();
      if (labelIdSz != 0) {
         brrSpender.advance(labelIdSz);
      }

      //sequence
      auto sequence = brrSpender.get_uint32_t();

      //pubkey & sig list
      struct KeysAndSigs
      {
         BinaryDataRef key;
         BinaryDataRef sig;
         BinaryDataRef wltLocator;
      };
      std::vector<KeysAndSigs> keysAndSigs;
      auto keyCount = brrSpender.get_var_int();
      keysAndSigs.resize(keyCount);

      for (unsigned y=0; y<keyCount; y++) {
         auto& kas = keysAndSigs[y];

         auto pubkeySize = brrSpender.get_var_int();
         kas.key = brrSpender.get_BinaryDataRef(pubkeySize);

         auto sigSize = brrSpender.get_var_int();
         kas.sig = brrSpender.get_BinaryDataRef(sigSize);

         auto wltLocatorSize = brrSpender.get_var_int();
         kas.wltLocator = brrSpender.get_BinaryDataRef(wltLocatorSize);
      }

      //p2sh extended map
      std::map<BinaryData, BinaryData> p2shExtMap;
      while (brrSpender.getSizeRemaining() != 0) {
         auto extFlag = brrSpender.get_uint8_t();
         auto extSize = brrSpender.get_var_int();
         auto extRef = brrSpender.get_BinaryDataRef(extSize);

         switch (extFlag)
         {
            case TXIN_EXT_P2SHSCRIPT:
            {
               BinaryRefReader brrExt(extRef);
               auto keyCount = brrExt.get_var_int();

               for (unsigned y=0; y<keyCount; y++) {
                  auto keySize = brrExt.get_var_int();
                  auto key = brrExt.get_BinaryData(keySize);

                  auto valSize = brrExt.get_var_int();
                  auto val = brrExt.get_BinaryData(valSize);

                  p2shExtMap.emplace(key, val);
               }
               break;
            }

            default:
               continue;
         }
      }

      if (!p2shExtMap.empty()) {
         LOGINFO << "spender " << i << "has extended p2sh data";
      }

      //setup spender
      BinaryRefReader brrOutpoint(outpointRef);
      auto hashRef = brrOutpoint.get_BinaryDataRef(32);
      auto outpointIndex = brrOutpoint.get_uint32_t();
      auto spender = std::make_shared<ScriptSpender>(hashRef, outpointIndex);
      addSpender(spender);

      spender->setSupportingTx(supportingTxRaw);
      auto supportingTx = spender->getSupportingTx();
      auto output = supportingTx.getTxOutCopy(outpointIndex);

      /***
      Resolve the spender state the legacy way:

      We assume the eligible output types are known. We expect the supporting
      tx is present and grab the redeemScript from the relevant output. The
      redeemScript is either a base script or a nested script. We expect the
      following data is provided in the USTXI depending on the redeemScript:

         base script types:
            - P2PKH: input should carry the public key
            - P2PK: input should carry pubkey
            - Multisig: input should carry the many pubkeys

         nested scripts:
            - P2SH: input should carry script preimage. We have to parse the
              p2sh preimage as the redeemScript to progress.

      The resolver will be fed the relevant <hash, preimage> entries at which
      point it should have the correct state to setup the spender.
      ***/

      auto feed = std::make_shared<ResolverFeed_SpenderResolutionChecks>();

      //grab base script
      BinaryDataRef baseScript = output.getScriptRef();
      if (!p2shPreimage.empty()) {
         /*
         Output script is p2sh, it embeds a hash and we have the preimage
         for it. Grab the hash from the script and add the <hash, preimage>
         pair to the feed
         */

         //grab hash from nested script
         auto scriptHash = BtcUtils::getTxOutRecipientAddr(baseScript);
         if (scriptHash == BtcUtils::BadAddress()) {
            throw SignerDeserializationError("invalid nested script");
         }

         //populate feed
         feed->hashMap.emplace(scriptHash, p2shPreimage);

         //set the preimage as the base script
         baseScript = p2shPreimage;
      }

      //get base script type
      auto scriptType = BtcUtils::getTxOutScriptType(baseScript);
      auto scriptHash = BtcUtils::getTxOutRecipientAddr(baseScript, scriptType);
      switch (scriptType)
      {
         case TXOUT_SCRIPT_STDHASH160:
         {
            //p2pkh, we should have a pubkey
            if (keysAndSigs.size() == 1) {
               feed->hashMap.emplace(scriptHash, keysAndSigs.begin()->key);
            }
            break;
         }

         case TXOUT_SCRIPT_STDPUBKEY33:
         case TXOUT_SCRIPT_MULTISIG:
         {
            //these script types carry the pubkey directly
            break;
         }

         default:
            throw SignerDeserializationError(
               "unsupported redeem script for legacy utsxi");
      }

      //resolve the spender
      try {
         StackResolver resolver(spender->getOutputScript(), feed);
         resolver.setFlags(
            SCRIPT_VERIFY_P2SH |
            SCRIPT_VERIFY_SEGWIT |
            SCRIPT_VERIFY_P2SH_SHA256);

         spender->parseScripts(resolver);
      } catch (const std::exception&) {}

      //inject sigs, will throw on failure
      for (const auto& kas : keysAndSigs) {
         SecureBinaryData sig(kas.sig);
         spender->injectSignature(sig, 0);
      }

      //TODO: sighash type

      //sequence
      spender->setSequence(sequence);
   }

   auto recipientCount = brr.get_var_int();
   for (unsigned i=0; i<recipientCount; i++) {
      auto recipientDataSize = brr.get_var_int();
      auto recipientData = brr.get_BinaryDataRef(recipientDataSize);
      BinaryRefReader brrRecipient(recipientData);

      //version
      auto version = brrRecipient.get_uint32_t();
      if (version != USTXO_VER_LEGACY) {
         throw SignerDeserializationError(
            "legacy deser: ustxo version mismatch");
      }

      //magic bytes
      auto ustxo_magic = brrRecipient.get_BinaryDataRef(4);
      if (ustxo_magic != magicBytes) {
         throw SignerDeserializationError(
            "legacy deser: ustxo magic bytes mismatch!");
      }

      //script
      auto scriptLen = brrRecipient.get_var_int();
      auto script = brrRecipient.get_BinaryDataRef(scriptLen);

      //value
      auto amount = brrRecipient.get_uint64_t();

      //recreate output
      BinaryWriter outputData;
      outputData.put_uint64_t(amount);
      outputData.put_var_int(scriptLen);
      outputData.put_BinaryDataRef(script);

      addRecipient(ScriptRecipient::fromScript(outputData.getDataRef()));
   }

   //lock time
   if (brr.getSizeRemaining() >= 4) {
      lockTime_ = brr.get_uint32_t();
   }

   //look for legacy signer state in extended data
   auto legacySigner = LegacySigner::Signer::deserExtState(
      brr.get_BinaryDataRef(brr.getSizeRemaining()));

   //get the sigs if any
   auto sigsFromLegacySigner = legacySigner.getSigs();

   //inject them
   for (auto& sigPair : sigsFromLegacySigner) {
      if (sigPair.first >= spenders_.size()) {
         throw SignerDeserializationError("legacy deser: invalid spender id");
      }
      auto& spender = spenders_[sigPair.first];
      spender->injectSignature(sigPair.second, 0);
   }
}

////////////////////////////////////////////////////////////////////////////////
std::string Signing::Signer::getSigCollectID() const
{
   //legacy unsigned serialization with hardcoded version
   BinaryWriter bw;
   bw.put_uint32_t(1); //version

   //inputs
   bw.put_var_int(spenders_.size());
   for (const auto& spender : spenders_) {
      //outpoint
      bw.put_BinaryData(spender->getOutpoint());

      //empty scriptsig
      bw.put_uint8_t(0);

      //sequence
      bw.put_uint32_t(spender->getSequence());
   }

   //outputs
   std::list<BinaryWriter> serializedRecipients;
   for (const auto& recipientList : recipients_) {
      BinaryWriter bwTxOut;
      for (const auto& recipient : recipientList.second) {
         auto output = recipient->getSerializedScript();
         auto script = output.getSliceRef(8, output.getSize()-8);

         //value
         bwTxOut.put_uint64_t(recipient->getValue());

         //script
         bwTxOut.put_BinaryData(script);

         //add to list
         serializedRecipients.emplace_back(std::move(bwTxOut));
      }
   }

   //finalize outputs
   bw.put_var_int(serializedRecipients.size());
   for (const auto& rec : serializedRecipients) {
      bw.put_BinaryData(rec.getData());
   }

   //locktime
   bw.put_uint32_t(0);

   auto serializedTx = bw.getData();
   if (serializedTx.getSize() < 4) {
      throw std::runtime_error("invalid serialized tx");
   }

   auto hashedTxPrefix = BtcUtils::getHash256(serializedTx);
   return BtcUtils::base58_encode(hashedTxPrefix).substr(0, 8);
}

////////////////////////////////////////////////////////////////////////////////
std::string Signing::Signer::toString(SignerStringFormat ustxFormat) const
{
   std::string serializedSigner;
   switch (ustxFormat)
   {
      case SignerStringFormat::TxSigCollect_Modern:
      {
         serializedSigner = toTxSigCollect(false);
         break;
      }

      case SignerStringFormat::TxSigCollect_Legacy:
      {
         serializedSigner = toTxSigCollect(true);
         break;
      }

      case SignerStringFormat::PSBT:
      {
         auto psbtBin = toPSBT();
         std::string psbtStr(psbtBin.toCharPtr(), psbtBin.getSize());
         serializedSigner = BtcUtils::base64_encode(psbtStr);
         break;
      }

      default:
         throw std::runtime_error("unsupported serialization format");
   }

   return serializedSigner;
}

////////////////////////////////////////////////////////////////////////////////
std::string Signing::Signer::toTxSigCollect(bool isLegacy) const
{
   BinaryWriter signerState;
   if (isLegacy) {
      auto legacyState = serializeState_Legacy();

      //txsig collect version, hardcoded to 1 for legacy
      signerState.put_uint32_t(TXSIGCOLLECT_VER_LEGACY);
      signerState.put_BinaryData(legacyState);
   } else {
      auto serializedCapn = serializeState();

      //txsig collect version
      signerState.put_uint32_t(TXSIGCOLLECT_VER_MODERN);
      signerState.put_uint32_t(0);
      signerState.put_BinaryData(serializedCapn);
   }

   //get sigcollect b58id
   auto legacyB58ID = getSigCollectID();

   std::string lsStr(signerState.getDataRef().toCharPtr(), signerState.getSize());
   auto stateB64 = BtcUtils::base64_encode(lsStr);

   std::stringstream txcollect;
   txcollect << TXSIGCOLLECT_HEADER;
   txcollect << std::setw(46) << std::setfill('=') << std::left;
   txcollect << legacyB58ID << std::endl;

   size_t offset = 0;
   size_t width = 64;
   while (offset < stateB64.size()) {
      size_t charCount = std::min(stateB64.size() - offset, width);
      auto substr = stateB64.substr(offset, charCount);
      txcollect << substr << std::endl;
      offset += charCount;
   }

   txcollect << std::setw(64) << std::setfill('=')
      << std::left << "=" << std::endl;
   return txcollect.str();
}

////////////////////////////////////////////////////////////////////////////////
Signing::Signer Signing::Signer::fromString(const std::string& signerState)
{
   //try a base 64 deser
   try {
      auto binState = BtcUtils::base64_decode(signerState);
      auto signer = Signer::fromPSBT(binState);
      signer.fromType_ = SignerStringFormat::PSBT;
      return signer;
   } catch (const std::runtime_error&) {
      //not a PSBT, try TxSigCollect instead
   }

   auto validateHeader = [](const BinaryDataRef& header)->std::string
   {
      std::string headerStr(header.toCharPtr(), strlen(TXSIGCOLLECT_HEADER));
      if (headerStr != TXSIGCOLLECT_HEADER) {
         return {};
      }

      unsigned pos=headerStr.size();
      while (header.toCharPtr()[pos] != '=' && pos < header.getSize()) {
         ++pos;
      }

      if (pos < headerStr.size()) {
         return {};
      }

      return std::string(
         header.toCharPtr() + headerStr.size(),
         header.toCharPtr() + pos);
   };

   auto validateFooter = [](const BinaryDataRef& footer)->bool
   {
      if (footer.empty()) {
         return false;
      }

      //skip line break if present
      auto footerLen = footer.getSize();
      if (footer.getPtr()[footerLen - 1] == '\n') {
         --footerLen;
      }

      //check size
      if (footerLen != TXSIGCOLLECT_WIDTH) {
         return false;
      }

      //footer should be all '='
      for (unsigned i = 0; i<footerLen; i++) {
         if (footer.toCharPtr()[i] != '=') {
            return false;
         }
      }

      return true;
   };

   //check size for header and footer: 64x2 + 1 for the first line break
   if (signerState.size() < TXSIGCOLLECT_WIDTH * 2 + 1) {
      throw SignerDeserializationError("too short to be a TxSigCollect");
   }

   auto header = signerState.substr(0, TXSIGCOLLECT_WIDTH + 1);

   auto sigCollectRef = BinaryDataRef::fromString(signerState);
   BinaryRefReader brr(sigCollectRef);

   //header: 64 characters + 1 for the line break
   auto headerRef = brr.get_BinaryDataRef(TXSIGCOLLECT_WIDTH + 1);
   auto sigCollectId = validateHeader(headerRef);
   if (sigCollectId.empty()) {
      throw SignerDeserializationError("invalid TxSigCollect header");
   }

   //body: rest of the data - last 64 characters (and possibly a line break)
   auto sigCollectSize = sigCollectRef.getSize();
   unsigned footerLength = TXSIGCOLLECT_WIDTH;
   if (sigCollectRef.getPtr()[sigCollectSize - 1] == '\n') {
      //last character is a line break, account for it
      ++footerLength;
   }
   if (footerLength > sigCollectSize) {
      throw SignerDeserializationError("invalid TxSigCollect length");
   }

   //get body and footer ref
   auto bodyRef = brr.get_BinaryDataRef(
      brr.getSizeRemaining() - footerLength);
   auto footerRef = brr.get_BinaryDataRef(footerLength);

   //validate footer
   if (!validateFooter(footerRef)) {
      throw SignerDeserializationError("invalid TxSigCollect footer");
   }

   //reconstruct base64 string from lines, evict line breaks
   std::string bodyStr;
   unsigned pos = 0;
   while (pos < bodyRef.getSize()) {
      //grab the line break as well
      auto len = std::min((size_t)TXSIGCOLLECT_WIDTH + 1, bodyRef.getSize() - pos);

      //do not copy the line break
      bodyStr += std::string(bodyRef.toCharPtr() + pos, len - 1);

      //assume there's a line break after each 64 characters
      pos += len;
   }

   //convert to binary
   auto bodyBin = BtcUtils::base64_decode(bodyStr);
   auto bodyBinRef = BinaryDataRef::fromString(bodyBin);
   BinaryRefReader bodyRR(bodyBinRef);

   //version
   auto version = bodyRR.get_uint32_t();
   Signer theSigner;
   switch (version)
   {
      case TXSIGCOLLECT_VER_LEGACY:
      {
         //legacy txsig collect
         auto signerStateRef = bodyRR.get_BinaryDataRef(bodyRR.getSizeRemaining());
         theSigner.deserializeState_Legacy(signerStateRef);
         theSigner.fromType_ = SignerStringFormat::TxSigCollect_Legacy;
         break;
      }

      case TXSIGCOLLECT_VER_MODERN:
      {
         //regular proto packet
         bodyRR.advance(4);
         auto signerStateRef = bodyRR.get_BinaryDataRef(bodyRR.getSizeRemaining());
         Deserializer::capnToSigner(theSigner, signerStateRef);
         theSigner.fromType_ = SignerStringFormat::TxSigCollect_Modern;
         break;
      }

      default:
         throw SignerDeserializationError("unsupported TxSigCollect version");
   }

   //check vs signer id
   auto signerId = theSigner.getSigCollectID();
   if (signerId != sigCollectId) {
      std::string errStr("tx sig collect id mismatch, ");
      errStr = errStr + "expected: " + sigCollectId + ", got: " + signerId;
      throw SignerDeserializationError(errStr);
   }
   return theSigner;
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::Signer::isResolved() const
{
   /*
   Returns true if all spenders carry all relevant public data referenced by 
   the utxo's script
   */
   for (auto& spender : spenders_)
   {
      if (!spender->isResolved())
         return false;
   }

   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::Signer::isSigned() const
{
   /*
   Return true is all spenders carry enough signatures. Does not check sigs,
   use ::verify() to check those.
   */
   for (auto& spender : spenders_)
   {
      if (!spender->isSigned())
         return false;
   }

   return true;
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::resetFeed(void)
{
   resolverPtr_ = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::populateUtxo(const UTXO& utxo)
{
   for (auto& spender : spenders_)
   {
      try
      {
         const auto& spenderUtxo = spender->getUtxo();
         if (spenderUtxo.isInitialized())
         {
            if (spenderUtxo == utxo)
               return;
         }
      }
      catch (const std::exception&)
      {}

      auto outpoint = spender->getOutpoint();
      BinaryRefReader brr(outpoint);
         
      auto&& hash = brr.get_BinaryDataRef(32);
      if (hash != utxo.getTxHash())
         continue;

      auto txoutid = brr.get_uint32_t();
      if (txoutid != utxo.getTxOutIndex())
         continue;

      spender->setUtxo(utxo);
      return;
   }

   throw std::runtime_error("could not match utxo to any spender");
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::Signer::getTxId_const() const
{
   try {
      auto txdataref = serializeSignedTx();
      Tx tx(txdataref);
      return tx.getThisHash();
   }
   catch (const std::exception&) {}

   BinaryWriter bw;

   //version
   bw.put_uint32_t(version_);
   
   //inputs
   bw.put_var_int(spenders_.size());
   for (auto spender : spenders_) {
      if (!spender->isSegWit() && !spender->isSigned()) {
         throw std::runtime_error("cannot get hash for unsigned legacy input");
      }
      bw.put_BinaryData(spender->getSerializedInput(false, false));
   }

   //outputs
   auto recipientVec = getRecipientVector();
   bw.put_var_int(recipientVec.size());
   for (auto recipient : recipientVec) {
      bw.put_BinaryData(recipient->getSerializedScript());
   }

   //locktime
   bw.put_uint32_t(lockTime_);

   //hash and return
   return BtcUtils::getHash256(bw.getDataRef());
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::Signer::getTxId()
{
   if (!isResolved()) {
      resolvePublicData();
   }
   return getTxId_const();
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::addSpender_ByOutpoint(
   const BinaryData& hash, unsigned index, unsigned sequence)
{
   auto spender = std::make_shared<ScriptSpender>(hash, index);
   spender->setSequence(sequence);

   addSpender(spender);
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::addSpender(std::shared_ptr<ScriptSpender> ptr)
{
   for (const auto& spender : spenders_) {
      if (*ptr == *spender) {
         throw ScriptException("already carrying this spender");
      }
   }

   ptr->setTxMap(supportingTxMap_);
   spenders_.emplace_back(ptr);
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::addRecipient(std::shared_ptr<ScriptRecipient> rec)
{
   addRecipient(rec, DEFAULT_RECIPIENT_GROUP);
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::addRecipient(
   std::shared_ptr<ScriptRecipient> rec, unsigned groupId)
{
   //do not tolerate recipient duplication within a same group
   auto iter = recipients_.find(groupId);
   if (iter == recipients_.end()) {
      auto insertIter = recipients_.emplace(
         groupId, std::vector<std::shared_ptr<ScriptRecipient>>());

      iter = insertIter.first;
   }

   auto& recVector = iter->second;
   for (const auto& recFromVector : recVector) {
      if (recFromVector->isSame(*rec)) {
         throw std::runtime_error(
            "recipient duplication is not tolerated within groups");
      }
   }

   recVector.emplace_back(rec);
}

////////////////////////////////////////////////////////////////////////////////
std::vector<std::shared_ptr<Signing::ScriptRecipient>>
   Signing::Signer::getRecipientVector() const
{
   std::vector<std::shared_ptr<ScriptRecipient>> result;
   for (auto& group : recipients_) {
      for (auto& rec : group.second) {
         result.emplace_back(rec);
      }
   }

   return result;
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::Signer::verifySpenderEvalState() const
{
   /*
   Checks the integrity of spenders evaluation state. This is meant as a 
   sanity check for signers restored from a serialized state.
   */

   for (unsigned i = 0; i < spenders_.size(); i++) {
      auto& spender = spenders_[i];

      if (!spender->verifyEvalState(flags_)) {
         return false;
      }
   }

   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::Signer::isSegWit() const
{
   for (auto& spender : spenders_) {
      if (spender->isSegWit()) {
         return true;
      }
   }

   return false;
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::Signer::hasLegacyInputs() const
{
   for (auto& spender : spenders_) {
      if (!spender->isSegWit()) {
         return true;
      }
   }

   return false;
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::injectSignature(
   unsigned inputIndex, SecureBinaryData& sig, unsigned sigId)
{
   if (spenders_.size() < inputIndex) {
      throw std::runtime_error("invalid spender index");
   }
   auto& spender = spenders_[inputIndex];
   spender->injectSignature(sig, sigId);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::Signer::toPSBT() const
{
   //init
   BinaryWriter bw;
   Signing::PSBT::init(bw);

   /*
   Serialize the unsigned tx. PSBT requires non SW formating for this field
   and preimages are carried in dedicated input fields so we'll be using 
   dedicated serialization instead of relying on the existing unsigned tx
   code (which is used to yield hashes from unsigned SW transactions).
   */
   BinaryData unsignedTx;
   {
      BinaryWriter bw;

      //version
      bw.put_uint32_t(version_);

      //txin count
      bw.put_var_int(spenders_.size());

      //txins
      for (auto& spender : spenders_)
         bw.put_BinaryData(spender->getEmptySerializedInput());

      //txout count
      auto recVector = getRecipientVector();
      bw.put_var_int(recVector.size());

      //txouts
      for (auto& recipient : recVector)
         bw.put_BinaryData(recipient->getSerializedScript());

      //lock time
      bw.put_uint32_t(lockTime_);

      unsignedTx = std::move(bw.getData());
   }

   //unsigned tx
   Signing::PSBT::setUnsignedTx(bw, unsignedTx);

   //proprietary data
   for (auto& data : prioprietaryPSBTData_)
   {
      //key
      bw.put_var_int(data.first.getSize() + 1);
      bw.put_uint8_t(Signing::PSBT::ENUM_GLOBAL::PSBT_GLOBAL_PROPRIETARY);
      bw.put_BinaryData(data.first);

      //val
      bw.put_var_int(data.second.getSize());
      bw.put_BinaryData(data.second);
   }

   Signing::PSBT::setSeparator(bw);

   /*inputs*/
   for (auto& spender : spenders_)
      spender->toPSBT(bw);

   /*outputs*/
   for (auto recipient : getRecipientVector())
      recipient->toPSBT(bw);

   //return
   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
Signing::Signer Signing::Signer::fromPSBT(const std::string& psbtString)
{
   BinaryDataRef psbtRef;
   psbtRef.setRef(psbtString);

   return Signer::fromPSBT(psbtRef);
}

////////////////////////////////////////////////////////////////////////////////
Signing::Signer Signing::Signer::fromPSBT(BinaryDataRef psbtRef)
{
   Signer signer;
   BinaryRefReader brr(psbtRef);

   /** header section **/

   //magic word
   auto magic = brr.get_uint32_t(BE);

   //separator
   auto separator = brr.get_uint8_t();

   if (magic != Signing::PSBT::ENUM_GLOBAL::PSBT_GLOBAL_MAGICWORD ||
      separator != Signing::PSBT::ENUM_GLOBAL::PSBT_GLOBAL_SEPARATOR)
   {
      throw PSBTDeserializationError("invalid header");
   }

   /** global section **/
   BinaryDataRef unsignedTxRef;

   //getPSBTDataPairs guarantees keys aren't empty
   auto globalDataPairs = BtcUtils::getPSBTDataPairs(brr);

   for (const auto& dataPair : globalDataPairs)
   {
      const auto& key = dataPair.first;
      const auto& val = dataPair.second;
      
      //key type
      auto typePtr = key.getPtr();

      switch (*typePtr)
      {
      case Signing::PSBT::ENUM_GLOBAL::PSBT_GLOBAL_UNSIGNED_TX:
      {
         //key has to be 1 byte long
         if (key.getSize() != 1)
            throw PSBTDeserializationError("invalid unsigned tx key length");

         unsignedTxRef = val;
         break;
      }

      case Signing::PSBT::ENUM_GLOBAL::PSBT_GLOBAL_XPUB:
      {
         //skip for now

         break;
      }

      case Signing::PSBT::ENUM_GLOBAL::PSBT_GLOBAL_VERSION:
      {
         //sanity checks
         if (key.getSize() != 1)
            throw PSBTDeserializationError("invalid version key length");
         
         if (val.getSize() != 4)
            throw PSBTDeserializationError("invalid version val length");

         break;
      }

      case Signing::PSBT::ENUM_GLOBAL::PSBT_GLOBAL_PROPRIETARY:
      {
         //skip for now

         break;
      }

      default:
         throw PSBTDeserializationError("unexpected global key");
      }
   }

   //sanity check
   if (unsignedTxRef.empty())
      throw PSBTDeserializationError("missing unsigned tx");

   Tx unsignedTx(unsignedTxRef);
   signer.setVersion(unsignedTx.getVersion());

   /** txin section **/
   for (unsigned i=0; i<unsignedTx.getNumTxIn(); i++)
   {
      auto txinCopy = unsignedTx.getTxInCopy(i);
      auto spender = ScriptSpender::fromPSBT(
         brr, txinCopy, signer.supportingTxMap_);

      signer.addSpender(spender);
   }

   /** txout section **/
   for (unsigned i=0; i<unsignedTx.getNumTxOut(); i++)
   {
      auto txoutCopy = unsignedTx.getTxOutCopy(i);
      auto recipient = ScriptRecipient::fromPSBT(brr, txoutCopy);
      signer.addRecipient(recipient);
   }

   return signer;
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::addSupportingTx(BinaryDataRef rawTxRef)
{
   if (rawTxRef.empty()) {
      return;
   }

   try {
      Tx tx(rawTxRef);
      addSupportingTx(std::move(tx));
   } catch (const std::exception&) {}
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::addSupportingTx(Tx tx)
{
   if (!tx.isInitialized()) {
      return;
   }
   supportingTxMap_->emplace(tx.getThisHash(), std::move(tx));
}

////////////////////////////////////////////////////////////////////////////////
const Tx& Signing::Signer::getSupportingTx(const BinaryData& hash) const
{
   auto iter = supportingTxMap_->find(hash);
   if (iter == supportingTxMap_->end()) {
      throw std::runtime_error("unknown supporting tx hash");
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
std::map<unsigned, BinaryData> Signing::Signer::getPubkeysForScript(
   BinaryDataRef& scriptRef, std::shared_ptr<ResolverFeed> feedPtr)
{
   auto scriptType = BtcUtils::getTxOutScriptType(scriptRef);
   std::map<unsigned, BinaryData> pubkeyMap;

   switch (scriptType)
   {
   case TXOUT_SCRIPT_P2WPKH:
   {
      auto hash = scriptRef.getSliceRef(2, 20);
      if (feedPtr != nullptr)
      {
         try
         {
            pubkeyMap.emplace(0, feedPtr->getByVal(hash));
         }
         catch (const std::exception&)
         {}
      }
      break;
   }

   case TXOUT_SCRIPT_STDHASH160:
   {
      auto hash = scriptRef.getSliceRef(3, 20);
      if (feedPtr != nullptr)
      {
         try
         {
            pubkeyMap.emplace(0, feedPtr->getByVal(hash));
         }
         catch (const std::exception&)
         {}
      }
      break;
   }

   case TXOUT_SCRIPT_STDPUBKEY33:
   {
      pubkeyMap.emplace(0, scriptRef.getSliceRef(1, 33));
      break;
   }

   case TXOUT_SCRIPT_MULTISIG:
   {
      std::vector<BinaryData> pubKeys;
      BtcUtils::getMultisigPubKeyList(scriptRef, pubKeys);

      for (unsigned i=0; i<pubKeys.size(); i++)
         pubkeyMap.emplace(i, std::move(pubKeys[i]));
      break;
   }

   default:
      break;
   }

   return pubkeyMap;
}

////////////////////////////////////////////////////////////////////////////////
uint64_t Signing::Signer::getTotalInputsValue(void) const
{
   uint64_t val = 0;
   for (auto& spender : spenders_)
      val += spender->getValue();

   return val;
}

////////////////////////////////////////////////////////////////////////////////
uint64_t Signing::Signer::getTotalOutputsValue(void) const
{
   uint64_t val = 0;
   for (const auto& group : recipients_)
   {
      for (const auto& recipient : group.second)
         val += recipient->getValue();
   }

   return val;
}

////////////////////////////////////////////////////////////////////////////////
uint32_t Signing::Signer::getTxOutCount() const
{
   uint32_t count = 0;
   for (const auto& group : recipients_)
      count += group.second.size();

   return count;
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::addBip32Root(std::shared_ptr<BIP32_PublicDerivedRoot> rootPtr)
{
   if (rootPtr == nullptr)
      return;

   bip32PublicRoots_.emplace(rootPtr->getThisFingerprint(), rootPtr);
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::matchAssetPathsWithRoots()
{
   for (auto& spender : spenders_)
   {
      auto& paths = spender->getBip32Paths();

      for (auto& pathPair : paths)
      {
         auto fingerprint = pathPair.second.getThisFingerprint();
      
         auto iter = bip32PublicRoots_.find(fingerprint);
         if (iter == bip32PublicRoots_.end())
            continue;

         pathPair.second.setRoot(iter->second);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
BinaryData Signing::Signer::signMessage(
   const BinaryData& message, const BinaryData& scrAddr,
   std::shared_ptr<ResolverFeed> walletFeed)
{
   //get pubkey for scrAddr. Resolver takes unprefixed hashes
   if (scrAddr.getSize() < 21)
      throw std::runtime_error("invalid scrAddr");

   auto pubkey = walletFeed->getByVal(
      scrAddr.getSliceRef(1, scrAddr.getSize() - 1));
   bool compressed = true;
   if (pubkey.getSize() == 65)
      compressed = false;

   //get private key for pubkey
   auto privkey = walletFeed->getPrivKeyForPubkey(pubkey);

   //sign
   return CryptoECDSA::SignBitcoinMessage(
      message.getRef(), privkey, compressed);
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::Signer::verifyMessageSignature(
   const BinaryData& message, const BinaryData& scrAddr, const BinaryData& sig)
{
   BinaryData pubkey;
   try
   {
      pubkey = CryptoECDSA::VerifyBitcoinMessage(message, sig);
   }
   catch (const std::exception& e)
   {
      LOGWARN << "failed to verify bitcoin message "
         "signature with the following error: ";
      LOGWARN << "   " << e.what();

      return false;
   }

   /*
   The sig carries a pubkey. VerifyBitcoinMessage generates that pubkey.
   We need to convert it to an address hash to check it against the expected 
   scrAddr
   */

   //create asset from pubkey
   SecureBinaryData sbdPubkey(pubkey);
   auto assetPubkey = std::make_shared<Assets::Asset_PublicKey>(sbdPubkey);
   auto assetPtr = std::make_shared<Assets::AssetEntry_Single>(
      Wallets::AssetId(-1, -1, -1), assetPubkey, nullptr);

   //check scrAddr type, try to generate equivalent address hash
   auto scrType = BtcUtils::getScriptTypeForScrAddr(scrAddr.getRef());
   switch (scrType)
   {
      case TXOUT_SCRIPT_P2WPKH:
      {
         auto addrPtr = std::make_shared<AddressEntry_P2WPKH>(assetPtr);
         if (addrPtr->getPrefixedHash() == scrAddr)
            return true;
         
         break;
      }

      case TXOUT_SCRIPT_STDHASH160:
      {
         auto addrPtr = std::make_shared<AddressEntry_P2PKH>(
            assetPtr, (pubkey.getSize() == 33) ? true : false);
            
         if (addrPtr->getPrefixedHash() == scrAddr)
            return true;
         
         break;
      }

      case TXOUT_SCRIPT_P2SH:
      {
         /*
         This is a complicated case, the scrAddr provides no information as
         to what script type preceeds the p2sh hash. We'll try p2wpkh and p2pk
         since these are common in armory.
         */

         auto addrPtr1 = std::make_shared<AddressEntry_P2WPKH>(assetPtr);
         auto p2shAddr = std::make_shared<AddressEntry_P2SH>(addrPtr1);
         if (p2shAddr->getPrefixedHash() == scrAddr)
            return true;

         auto addrPtr2 = std::make_shared<AddressEntry_P2PK>(assetPtr, true);
         p2shAddr = std::make_shared<AddressEntry_P2SH>(addrPtr2);
         if (p2shAddr->getPrefixedHash() == scrAddr)
            return true;

         break;
      }

      default:
         LOGWARN << "could not generate scrAddr from pubkey";
         return false;
   }

   LOGWARN << "failed to match sig's pubkey to scrAddr";
   return false;
}

////////////////////////////////////////////////////////////////////////////////
void Signing::Signer::prettyPrint() const
{
   //WIP

   auto signEvalState = evaluateSignedState();

   std::cout << std::endl;
   std::stringstream ss;
   unsigned i=0;
   for (auto& spender : spenders_)
   {
      spender->prettyPrint(ss);
      if (spender->isSigned())
      {
         auto txInEvalState = signEvalState.getSignedStateForInput(i);
         ss << "    signed state: " << txInEvalState.isValid() << std::endl;
      }

      ++i;
   }

   for (auto& group : recipients_)
   {
      auto groupId = WRITE_UINT32_BE(group.first);
      ss << " recipient group: " << groupId.toHexStr() << std::endl;

      for (const auto& rec : group.second)
      {
         auto serTxOut = rec->getSerializedScript();
         BinaryRefReader brr(serTxOut);
         brr.advance(8);
         auto len = brr.get_var_int();
         auto txOutScript = brr.get_BinaryDataRef(len);

         auto scrRef = BtcUtils::getTxOutScrAddrNoCopy(txOutScript);
         auto addrStr = BtcUtils::getAddressStrFromScrAddr(scrRef.getScrAddr());

         ss <<  "  val: " << rec->getValue() <<
            ", addr: " << addrStr << std::endl;
      }
   }

   std::cout << ss.str();
}

////////////////////////////////////////////////////////////////////////////////
Signing::SignerStringFormat Signing::Signer::deserializedFromType() const
{
   return fromType_;
}

////////////////////////////////////////////////////////////////////////////////
bool Signing::Signer::canLegacySerialize() const
{
   return !isSegWit();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// SignerProxy
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
Signing::SignerProxy::~SignerProxy(void)
{}

////////////////////////////////////////////////////////////////////////////////
void Signing::SignerProxyFromSigner::setLambda(
   Signing::Signer* signer, std::shared_ptr<Signing::ScriptSpender> spender,
   unsigned index, std::shared_ptr<Signing::ResolverFeed> feedPtr)
{
   auto signerLBD = [signer, spender, index, feedPtr]
      (BinaryDataRef script, const BinaryData& pubkey, bool sw)->SecureBinaryData
   {
      if (signer == nullptr || feedPtr == nullptr || spender == nullptr)
         throw std::runtime_error("proxy carries null pointers");

      auto SHD = signer->getSigHashDataForSpender(sw);

      //get priv key for pubkey
      const auto& privKey = feedPtr->getPrivKeyForPubkey(pubkey);

      //sign
      auto&& sig = signer->signScript(script, privKey, SHD, index);

      //append sighash byte
      SecureBinaryData sbd_hashbyte(1);
      *sbd_hashbyte.getPtr() = spender->getSigHashByte();
      sig.append(sbd_hashbyte);
      return sig;
   };

   signerLambda_ = signerLBD;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// ResolverFeed_SpenderResolutionChecks
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
Signing::BIP32_AssetPath Signing::ResolverFeed_SpenderResolutionChecks::resolveBip32PathForPubkey(
   const BinaryData&)
{
   throw std::runtime_error("invalid pubkey");
}

////////////////////////////////////////////////////////////////////////////////
void Signing::ResolverFeed_SpenderResolutionChecks::setBip32PathForPubkey(
   const BinaryData&, const BIP32_AssetPath&)
{}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//// PSBT
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void Signing::PSBT::init(BinaryWriter& bw)
{
   bw.put_uint32_t(Signing::PSBT::ENUM_GLOBAL::PSBT_GLOBAL_MAGICWORD, BE);
   bw.put_uint8_t(Signing::PSBT::ENUM_GLOBAL::PSBT_GLOBAL_SEPARATOR);
}

////////////////////////////////////////////////////////////////////////////////
void Signing::PSBT::setUnsignedTx(BinaryWriter& bw, const BinaryData& unsignedTx)
{
   bw.put_uint8_t(1);
   bw.put_uint8_t(Signing::PSBT::ENUM_GLOBAL::PSBT_GLOBAL_UNSIGNED_TX);

   bw.put_var_int(unsignedTx.getSize());
   bw.put_BinaryData(unsignedTx);
}

////////////////////////////////////////////////////////////////////////////////
void Signing::PSBT::setSeparator(BinaryWriter& bw)
{
   bw.put_uint8_t(0);
}