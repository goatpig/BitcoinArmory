////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2016-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Signer.h"
#include <Utils/BtcUtils.h>
#include <Utils/Cryptography.h>
#include <Utils/TxOutScrRef.h>
#include <Utils/BitcoinSettings.h>
#include <Utils/TxOutScrRef.h>

#include <Wallets/BIP32_Node.h>
#include <Wallets/Assets.h>
#include <Wallets/Addresses.h>

#include "Script.h"
#include "ScriptSpender.h"
#include "LegacySigner.h"
#include "Transactions.h"
#include "PSBT.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/Signer.capnp.h"
#include "capnp/Types.capnp.h"


#define TXSIGCOLLECT_VER_LEGACY  1
#define USTXI_VER_LEGACY         1
#define USTXO_VER_LEGACY         1
#define TXSIGCOLLECT_VER_MODERN  3
#define TXSIGCOLLECT_WIDTH       64
#define TXSIGCOLLECT_HEADER      "=====TXSIGCOLLECT-"

#define TXIN_EXT_P2SHSCRIPT      0x10

using namespace Armory;
using namespace Armory::Signing;

////////////////////////////////////////////////////////////////////////////////
// capnp helpers
namespace
{
   void stackItemToCapn(std::shared_ptr<StackItem> item,
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
               (uint8_t*)pushDataPtr->data.getPtr(), pushDataPtr->data.getSize()
            ));
            break;
         }

         case StackItemType::OpCode:
         {
            auto opCodePtr = std::dynamic_pointer_cast<StackItem_OpCode>(item);
            if (opCodePtr == nullptr) {
               throw std::runtime_error("failed StackItem_OpCode cast");
            }

            capnItem.setOpCode(opCodePtr->opcode);
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
               (uint8_t*)sigPtr->script.getPtr(), sigPtr->script.getSize()
            ));
            capnSig.setPubkey(capnp::Data::Builder(
               (uint8_t*)sigPtr->pubkey.getPtr(), sigPtr->pubkey.getSize()
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
               (uint8_t*)sigPtr->script.getPtr(), sigPtr->script.getSize()
            ));

            unsigned i=0;
            auto sigDatas = capnSig.initSigData(sigPtr->sigs.size());
            for (const auto& sig : sigPtr->sigs) {
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
               (uint8_t*)scriptPtr->data.getPtr(), scriptPtr->data.getSize()
            ));
            break;
         }

         default:
            throw std::runtime_error("unexpected stack item type");
      }
   }

   void bip32PathsToCapn(
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

   void spenderToCapn(std::shared_ptr<Signing::ScriptSpender> spender,
      Codec::Signer::ScriptSpender::Builder& capnSpender)
   {
      //header
      capnSpender.setVersionMax(SCRIPT_SPENDER_VERSION_MAX);
      capnSpender.setVersionMin(SCRIPT_SPENDER_VERSION_MIN);

      capnSpender.setLegacyStatus((uint8_t)spender->getLegacyStatus());
      capnSpender.setSegwitStatus((uint8_t)spender->getWitnessStatus());

      capnSpender.setSigHashType(spender->getSigHashType());
      capnSpender.setSequence(spender->getSequence());

      capnSpender.setIsP2sh(spender->isP2SH());
      capnSpender.setIsCsv(spender->isCSV());
      capnSpender.setIsCltv(spender->isCLTV());

      //utxo
      if (spender->hasUtxo()) {
         const auto& utxo = spender->getUtxo();
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
      if (spender->getLegacyStatus() == SpenderStatus::Signed) {
         capnSpender.setSigScript(capnp::Data::Builder(
            (uint8_t*)spender->getFinalInputScript().getPtr(),
            spender->getFinalInputScript().getSize()
         ));
      } else if (spender->getLegacyStatus() >= SpenderStatus::Resolved) {
         const auto& legacyStack = spender->getLegacyStack();
         auto capnStackEntries = capnSpender.initLegacyStack(
            legacyStack.size());

         //put legacy stack
         unsigned i=0;
         for (const auto stackItem : legacyStack) {
            auto capnStackEntry = capnStackEntries[i++];
            stackItemToCapn(stackItem.second, capnStackEntry);
         }
      }

      //segwit stack
      if (spender->getWitnessStatus() == SpenderStatus::Signed) {
         capnSpender.setWitnessData(capnp::Data::Builder(
            (uint8_t*)spender->getFinalizedWitnessData().getPtr(),
            spender->getFinalizedWitnessData().getSize()
         ));
      } else if (spender->getWitnessStatus() >= SpenderStatus::Resolved) {
         const auto& witnessStack = spender->getWitnessStack();
         auto capnStackEntries = capnSpender.initWitnessStack(
            witnessStack.size());

         //put witness stack
         unsigned i=0;
         for (const auto stackItem : witnessStack) {
            auto capnStackEntry = capnStackEntries[i++];
            stackItemToCapn(stackItem.second, capnStackEntry);
         }
      }

      //path data
      const auto& paths = spender->getBip32Paths();
      auto capnPaths = capnSpender.initBip32Paths(paths.size());
      bip32PathsToCapn(paths, capnPaths);
   }

   void recipientToCapn(std::shared_ptr<Signing::ScriptRecipient> recipient,
      unsigned groupId, Codec::Signer::Recipient::Builder& capnRecipient)
   {
      const auto& script = recipient->getSerializedScript();
      capnRecipient.setScript(capnp::Data::Builder(
         (uint8_t*)script.getPtr(), script.getSize()
      ));
      capnRecipient.setGroupId(groupId);

      const auto& paths = recipient->getBip32Paths();
      auto capnPaths = capnRecipient.initBip32Paths(paths.size());
      bip32PathsToCapn(paths, capnPaths);
   }

   std::shared_ptr<BIP32_PublicDerivedRoot> capnToBIP32Root(
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

   BIP32_AssetPath capnToBIP32Path(
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

   std::shared_ptr<StackItem> capnToStackItem(
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

   UTXO capnToUtxo(const Codec::Types::Output::Reader& capnOutput)
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

   std::shared_ptr<ScriptSpender> capnToSpender(
      const Codec::Signer::ScriptSpender::Reader& capnSpender)
   {
      //version sanity check
      auto maxVer = capnSpender.getVersionMax();
      auto minVer = capnSpender.getVersionMin();
      if (maxVer != SCRIPT_SPENDER_VERSION_MAX ||
         minVer != SCRIPT_SPENDER_VERSION_MIN) {
         throw SignerDeserializationError(
            "serialized spender version mismatch");
      }

      //utxo/outpoint
      std::shared_ptr<ScriptSpender> result;
      if (capnSpender.hasUtxo()) {
         auto capnUtxo = capnSpender.getUtxo();
         auto utxo = capnToUtxo(capnUtxo);
         result = std::make_shared<ScriptSpender>(utxo);
      } else if (capnSpender.hasOutpoint()) {
         auto outpoint = capnSpender.getOutpoint();
         auto capnHash = outpoint.getTxHash();
         BinaryDataRef outpointHash(capnHash.begin(), capnHash.end());
         if (outpointHash.getSize() != 32) {
            throw SignerDeserializationError("invalid outpoint hash");
         }
         result = std::make_shared<ScriptSpender>(
            outpointHash, outpoint.getIndex());
      } else {
         throw SignerDeserializationError("missing utxo/outpoint");
      }

      //stack flags
      result->setStates(
         (SpenderStatus)capnSpender.getLegacyStatus(),
         (SpenderStatus)capnSpender.getSegwitStatus(),
         capnSpender.getIsP2sh(),
         capnSpender.getIsCsv(),
         capnSpender.getIsCltv()
      );
      result->setSequence(capnSpender.getSequence());
      result->setSigHashType((SIGHASH_TYPE)capnSpender.getSigHashType());

      //finalized script & witness data
      if (capnSpender.hasSigScript()) {
         auto sigScript = capnSpender.getSigScript();
         result->setFinalScript(
            BinaryDataRef{sigScript.begin(), sigScript.end()});
      }

      if (capnSpender.hasWitnessData()) {
         auto witnessData = capnSpender.getWitnessData();
         result->setWitnessScript(BinaryDataRef{
            witnessData.begin(), witnessData.end()});
      }

      //legacy stack
      {
         auto capnLegacyStack = capnSpender.getLegacyStack();
         std::vector<std::shared_ptr<StackItem>> siVec;
         siVec.reserve(capnLegacyStack.size());
         for (auto capnStackItem : capnLegacyStack) {
            siVec.emplace_back(capnToStackItem(capnStackItem));
         }
         result->setLegacyData(siVec);
      }

      //witness stack
      {
         auto capnWitnessStack = capnSpender.getWitnessStack();
         std::vector<std::shared_ptr<StackItem>> siVec;
         siVec.reserve(capnWitnessStack.size());
         for (auto capnStackItem : capnWitnessStack) {
            siVec.emplace_back(capnToStackItem(capnStackItem));
         }
         result->setWitnessData(siVec);
      }

      //bip32 paths
      auto capnPaths = capnSpender.getBip32Paths();
      std::map<BinaryData, BIP32_AssetPath> paths;
      for (auto capnPath : capnPaths) {
         auto path = capnToBIP32Path(capnPath);
         paths.emplace(path.getPublicKey(), std::move(path));
      }
      result->setBip32Paths(paths);
      return result;
   }

   std::shared_ptr<ScriptRecipient> capnToRecipient(
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

   void capnToSigner(Signer& signer, BinaryDataRef raw)
   {
      //deser capn payload
      kj::ArrayPtr<const capnp::word> words(
         reinterpret_cast<const capnp::word*>(raw.getPtr()),
         raw.getSize() / sizeof(capnp::word));
      capnp::FlatArrayMessageReader reader(words);
      auto signerCapn = reader.getRoot<Codec::Signer::Signer>();

      //flags
      signer.setFlags(signerCapn.getFlags());
      signer.setVersion(signerCapn.getTxVersion());
      signer.setLockTime(signerCapn.getLocktime());

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
         signer.addSupportingTx(Tx{rawTxRef});
      }

      //roots
      auto capnRoots = signerCapn.getBip32Roots();
      for (auto capnRoot : capnRoots) {
         auto root = capnToBIP32Root(capnRoot);
         signer.addBip32Root(root);
      }
      signer.matchAssetPathsWithRoots();
   }
};

////////////////////////////////////////////////////////////////////////////////
// exceptions
SignerDeserializationError::SignerDeserializationError(const std::string& e) :
   std::runtime_error(e)
{}

////////////////////////////////////////////////////////////////////////////////
// Signer
Signer::Signer() :
   TransactionStub{
      SCRIPT_VERIFY_P2SH |
      SCRIPT_VERIFY_SEGWIT |
      SCRIPT_VERIFY_P2SH_SHA256
   }
{
   supportingTxMap_ = std::make_shared<std::map<BinaryData, Tx>>();
}

void Signer::setFeed(std::shared_ptr<ResolverFeed> feedPtr)
{
   resolverPtr_ = feedPtr;
}

void Signer::clearSpenders()
{
   spenders_.clear();
}

void Signer::clearRecipients()
{
   recipients_.clear();
}

void Signer::clear()
{
   clearSpenders();
   clearRecipients();
   resetFeed();
}

////////
BinaryDataRef Signer::getSerializedOutputScripts() const
{
   if (serializedOutputs_.empty()) {
      BinaryWriter bw;
      for (auto& recipient : getRecipientVector()) {
         auto serializedOutput = recipient->getSerializedScript();
         bw.put_BinaryData(serializedOutput);
      }
      serializedOutputs_ = std::move(bw.getData());
   }
   return serializedOutputs_.getRef();
}

std::vector<TxInData> Signer::getTxInsData() const
{
   std::vector<TxInData> tidVec;
   for (const auto& spender : spenders_) {
      TxInData tid;
      tid.outputHash = spender->getOutputHash();
      tid.outputIndex = spender->getOutputIndex();
      tid.sequence = spender->getSequence();
      tidVec.emplace_back(std::move(tid));
   }
   return tidVec;
}

uint32_t Signer::getLockTime() const
{
   return lockTime_;
}

void Signer::setLockTime(unsigned locktime)
{
   lockTime_ = locktime;
}

uint32_t Signer::getVersion() const
{
   return version_;
}

void Signer::setVersion(unsigned version)
{
   version_ = version;
}

////////
const Signer::RecipientMap& Signer::getRecipientMap() const
{
   return recipients_;
}

BinaryData Signer::getSubScript(unsigned index) const
{
   auto spender = getSpender(index);
   return spender->getOutputScript();
}

BinaryDataRef Signer::getWitnessData(unsigned index) const
{
   auto spender = getSpender(index);
   return spender->getFinalizedWitnessData();
}

bool Signer::isInputSW(unsigned index) const
{
   auto spender = getSpender(index);
   return spender->isSegWit();
}

////////
BinaryData Signer::serializeAllOutpoints() const
{
   BinaryWriter bw;
   for (auto& spender : spenders_) {
      bw.put_BinaryDataRef(spender->getOutpoint());
   }
   return bw.getData();
}

BinaryData Signer::serializeAllSequences() const
{
   BinaryWriter bw;
   for (auto& spender : spenders_) {
      bw.put_uint32_t(spender->getSequence());
   }
   return bw.getData();
}

BinaryDataRef Signer::getOutpoint(unsigned index) const
{
   if (index >= spenders_.size()) {
      throw std::runtime_error("invalid spender index");
   }
   return spenders_[index]->getOutpoint();
}

uint64_t Signer::getOutpointValue(unsigned index) const
{
   if (index >= spenders_.size()) {
      throw std::runtime_error("invalid spender index");
   }
   return spenders_[index]->getValue();
}

unsigned Signer::getTxInSequence(unsigned index) const
{
   if (index >= spenders_.size()) {
      throw std::runtime_error("invalid spender index");
   }
   return spenders_[index]->getSequence();
}

////////
void Signer::sign()
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
   if (spenders_.empty()) {
      throw std::runtime_error("tx has no spenders");
   }
   auto recVector = getRecipientVector();
   if (recVector.empty()) {
      throw std::runtime_error("tx has no recipients");
   }
   /*
   Try to check input value vs output value. We're not guaranteed to
   have this information, since we may be partially signing this
   transaction. In that case, skip this step
   */
   try {
      uint64_t inputVal = 0;
      for (const auto& spender : spenders_) {
         inputVal += spender->getValue();
      }

      uint64_t spendVal = 0;
      for (const auto& recipient : recVector) {
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
      spender->sign(
         [this, i, shb=spender->getSigHashByte()](
            BinaryDataRef script, const BinaryData& pubkey, bool sw)
         ->SecureBinaryData
         {
            //prepare data to sign
            auto SHD = this->getSigHashDataForSpender(sw);

            //get priv key for pubkey
            const auto& privKey = resolverPtr_->getPrivKeyForPubkey(pubkey);

            //sign
            auto sig = this->signScript(script, privKey, SHD, i);

            //append sighash byte
            BinaryData sbd_hashbyte(1);
            *sbd_hashbyte.getPtr() = shb;
            sig.append(sbd_hashbyte);
            return sig;
         }
      );
   }
}

std::set<unsigned> Signer::resolvePublicData()
{
   std::set<unsigned> resolvedSpenderIds;

   //run through each spenders
   for (unsigned i=0; i<spenders_.size(); i++) {
      auto& spender = spenders_[i];
      if (spender->isResolved()) {
         continue;
      }
      if (!spender->canBeResolved()) {
         continue;
      }

      //resolve spender script
      StackResolver resolver(
         spender->getOutputScript(),
         resolverPtr_);

      //check Script.h for signer flags
      resolver.setFlags(flags_);

      try {
         spender->parseScripts(resolver);
      } catch (const std::exception&) {}

      auto spenderBip32Paths = spender->getBip32Paths();
      for (const auto& pathPair : spenderBip32Paths) {
         const auto& assetPath = pathPair.second;
         if (assetPath.hasRoot()) {
            addBip32Root(assetPath.getRoot());
         }
      }
      resolvedSpenderIds.emplace(i);
   }

   if (resolverPtr_ == nullptr) {
      return resolvedSpenderIds;
   }

   for (auto& recipient : getRecipientVector()) {
      const auto& serializedOutput = recipient->getSerializedScript();
      BinaryRefReader brr(serializedOutput);
      brr.advance(8);
      auto len = brr.get_var_int();
      auto scriptRef = brr.get_BinaryDataRef(len);

      auto pubKeys = Signer::getPubkeysForScript(scriptRef, resolverPtr_);
      for (const auto& pubKeyPair : pubKeys) {
         try {
            auto bip32path = resolverPtr_->resolveBip32PathForPubkey(
               pubKeyPair.second);
            if (!bip32path.isValid()) {
               continue;
            }
            recipient->addBip32Path(bip32path);
         } catch (const std::exception&) {
            continue;
         }
      }
   }
   return resolvedSpenderIds;
}

SecureBinaryData Signer::signScript(
   BinaryDataRef script,
   const SecureBinaryData& privKey,
   std::shared_ptr<SigHashData> SHD, unsigned index)
{
   auto spender = spenders_[index];
   auto hashToSign = SHD->getDataForSigHash(
      spender->getSigHashType(), *this,
      script, index);

#ifdef SIGNER_DEBUG
   auto pubkey = Cryptography::ECDSA::computePublicKey(privKey);
   LOGWARN << "signing for: ";
   LOGWARN << "   pubkey: " << pubkey.toHexStr();

   auto msghash = BtcUtils::getHash256(dataToHash);
   LOGWARN << "   message: " << dataToHash.toHexStr();
#endif

   return Cryptography::ECDSA::signData(hashToSign, privKey);
}

////////
std::shared_ptr<ScriptSpender> Signer::getSpender(unsigned index) const
{
   if (index > spenders_.size()) {
      throw ScriptException("invalid spender index");
   }
   return spenders_[index];
}

std::shared_ptr<ScriptRecipient> Signer::getRecipient(unsigned index) const
{
   auto recVector = getRecipientVector();
   if (index >= recVector.size()) {
      throw ScriptException("invalid spender index");
   }
   return recVector[index];
}

BinaryDataRef Signer::serializeSignedTx() const
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
         if (witnessRef.empty()) {
            //account for empty witness data
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

BinaryDataRef Signer::serializeUnsignedTx(bool loose)
{
   if (!serializedUnsignedTx_.empty()) {
      return serializedUnsignedTx_.getRef();
   }
   resolvePublicData();

   BinaryWriter bw;

   //version
   bw.put_uint32_t(version_);

   bool isSW = isSegWit();
   if (isSW) {
      //marker and flag
      bw.put_uint8_t(0);
      bw.put_uint8_t(1);
   }

   //txin count
   if (spenders_.empty()) {
      if (!loose) {
         throw std::runtime_error("no spenders");
      }
   }

   //txins
   bw.put_var_int(spenders_.size());
   for (const auto& spender : spenders_) {
      bw.put_BinaryData(spender->getSerializedInput(false, loose));
   }

   //txout count
   auto recVector = getRecipientVector();
   if (recVector.empty()) {
      if (!loose) {
         throw std::runtime_error("no recipients");
      }
   }

   //txouts
   bw.put_var_int(recVector.size());
   for (const auto& recipient : recVector) {
      bw.put_BinaryData(recipient->getSerializedScript());
   }

   //no witness data for unsigned transactions
   if (isSW) {
      for (unsigned i=0; i < spenders_.size(); i++) {
         bw.put_uint8_t(0);\
      }
   }

   //lock time
   bw.put_uint32_t(lockTime_);
   serializedUnsignedTx_ = std::move(bw.getData());
   return serializedUnsignedTx_.getRef();
}

BinaryData Signer::serializeAvailableResolvedData() const
{
   try {
      auto&& serTx = serializeSignedTx();
      return serTx;
   }
   catch (const std::exception&) {}

   BinaryWriter bw;

   //version
   bw.put_uint32_t(version_);

   bool isSW = isSegWit();
   if (isSW) {
      //marker and flag
      bw.put_uint8_t(0);
      bw.put_uint8_t(1);
   }

   //txin count
   bw.put_var_int(spenders_.size());

   //txins
   for (const auto& spender : spenders_) {
      try {
         bw.put_BinaryData(spender->getSerializedInput(false, false));
      } catch (const std::exception&) {
         bw.put_BinaryData(spender->getEmptySerializedInput());
      }
   }

   //txout count
   auto recVector = getRecipientVector();
   bw.put_var_int(recVector.size());

   //txouts
   for (const auto& recipient : recVector) {
      bw.put_BinaryData(recipient->getSerializedScript());
   }

   if (isSW) {
      //witness data
      for (auto& spender : spenders_) {
         BinaryData witnessData = spender->serializeAvailableWitnessData();

         //account for empty witness data
         if (witnessData.empty()) {
            bw.put_uint8_t(0);
         } else {
            bw.put_BinaryData(witnessData);
         }
      }
   }

   //lock time
   bw.put_uint32_t(lockTime_);
   return bw.getData();
}

////////
std::shared_ptr<SigHashData> Signer::getSigHashDataForSpender(bool sw) const
{
   std::shared_ptr<SigHashData> SHD;
   if (sw) {
      if (sigHashDataObject_ == nullptr) {
         sigHashDataObject_ = std::make_shared<SigHashDataSegWit>();
      }
      SHD = sigHashDataObject_;
   } else {
      SHD = std::make_shared<SigHashDataLegacy>();
   }
   return SHD;
}

std::unique_ptr<TransactionVerifier> Signer::getVerifier(
   std::shared_ptr<BCTX> bctx,
   std::map<BinaryData, std::map<unsigned, UTXO>>& utxoMap)
{
   return std::make_unique<TransactionVerifier>(*bctx, utxoMap);
}

TxEvalState Signer::verify(const BinaryData& rawTx,
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

TxEvalState Signer::evaluateSignedState() const
{
   auto txdata = serializeAvailableResolvedData();

   std::map<BinaryData, std::map<unsigned, UTXO>> utxoMap;
   unsigned flags = 0;
   for (const auto& spender : spenders_) {
      auto& indexMap = utxoMap[spender->getOutputHash()];
      indexMap[spender->getOutputIndex()] = spender->getUtxo();
      flags |= spender->getFlags();
   }
   return verify(txdata, utxoMap, flags, true);
}

bool Signer::verify() const
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

bool Signer::verifyRawTx(const BinaryData& rawTx,
   const std::map<BinaryData, std::map<unsigned, BinaryData>>& rawUTXOs)
{
   std::map<BinaryData, std::map<unsigned, UTXO>> utxoMap;

   //deser utxos
   for (auto& utxoPair : rawUTXOs) {
      std::map<unsigned, UTXO> idMap;
      for (auto& rawUtxoPair : utxoPair.second) {
         UTXO utxo;
         utxo.unserializeRaw(rawUtxoPair.second);
         idMap.insert(std::move(std::make_pair(
            rawUtxoPair.first, std::move(utxo))));
      }
      utxoMap.emplace(utxoPair.first, std::move(idMap));
   }

   auto evalState = verify(
      rawTx, utxoMap, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_SEGWIT);
   return evalState.isValid();
}

BinaryData Signer::serializeState() const
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
      spenderToCapn(spender, capnSpender);
   }

   unsigned recipientCount = 0;
   for (const auto& group : recipients_) {
      recipientCount += group.second.size();
   }

   i=0;
   auto capnRecipients = capnMsg.initRecipients(recipientCount);
   for (const auto& group : recipients_) {
      for (auto& recipient : group.second) {
         auto capnRecipient = capnRecipients[i++];
         recipientToCapn(recipient, group.first, capnRecipient);
      }
   }

   if (supportingTxMap_ != nullptr && !supportingTxMap_->empty()) {
      i=0;
      auto capnTxns = capnMsg.initSupportingTxs(supportingTxMap_->size());
      for (const auto& supportingTx : *supportingTxMap_) {
         capnTxns.set(i, capnp::Data::Builder(
            (uint8_t*)supportingTx.second.getPtr(),
            supportingTx.second.getSize()
         ));
      }
   }

   i=0;
   auto capnRoots = capnMsg.initBip32Roots(bip32PublicRoots_.size());
   for (auto& bip32PublicRoot : bip32PublicRoots_) {
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

////////
void Signer::deserializeState(const BinaryDataRef& ref)
{
   Signer theSigner;
   capnToSigner(theSigner, ref);
   theSigner.fromType_ = SignerStringFormat::TxSigCollect_Modern;
   merge(theSigner);
}

void Signer::merge(const Signer& rhs)
{
   version_ = rhs.version_;
   lockTime_ = rhs.lockTime_;
   flags_ |= rhs.flags_;

   auto find_spender = [this](std::shared_ptr<ScriptSpender> obj)
      ->std::shared_ptr<ScriptSpender>
   {
      for (auto spd : this->spenders_) {
         if (*spd == *obj) {
            return spd;
         }
      }
      return nullptr;
   };

   auto find_recipient = [this](
      std::shared_ptr<ScriptRecipient> obj, unsigned groupid)
      ->std::shared_ptr<ScriptRecipient>
   {
      auto groupIter = this->recipients_.find(groupid);
      if (groupIter == this->recipients_.end()) {
         return nullptr;
      }
      const auto& scriptHash = obj->getSerializedScript();
      for (auto& rec : groupIter->second) {
         if (scriptHash == rec->getSerializedScript()) {
            return rec;
         }
      }
      return nullptr;
   };

   //Merge new signer with this. As a general rule, the added entries are all
   //pushed back.
   supportingTxMap_->insert(
      rhs.supportingTxMap_->begin(), rhs.supportingTxMap_->end());

   //merge spender
   for (auto& spender : rhs.spenders_) {
      auto local_spender = find_spender(spender);
      if (local_spender != nullptr) {
         local_spender->merge(*spender);
         if (!local_spender->verifyEvalState(flags_)) {
            throw SignerDeserializationError(
               "merged spender has inconsistent state");
         }
      } else {
         auto newSpender = std::make_shared<ScriptSpender>(*spender);
         newSpender->setTxMap(supportingTxMap_);
         spenders_.emplace_back(newSpender);
         if (!spenders_.back()->verifyEvalState(flags_)) {
            throw SignerDeserializationError(
               "unserialized spender has inconsistent state");
         }
      }
   }

   /*
   Recipients are told apart by their group id. Within a group, they are 
   differentiated by their script hash. Collisions within a group are 
   not tolerated.
   */

   for (auto& group : rhs.recipients_) {
      for (auto& recipient : group.second) {
         auto local_recipient = find_recipient(recipient, group.first);
         if (local_recipient == nullptr) {
            addRecipient(recipient, group.first);
         } else {
            local_recipient->merge(recipient);
         }
      }
   }

   //merge bip32 roots
   bip32PublicRoots_.insert(
      rhs.bip32PublicRoots_.begin(), rhs.bip32PublicRoots_.end());
   matchAssetPathsWithRoots();
}

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

         auto script = spender->getRedeemScriptFromStack(false);
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

void Signer::deserializeState_Legacy(const BinaryDataRef& ref)
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
   for (unsigned i=0; i < spenderCount; i++) {
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
      for (unsigned y=0; y < keyCount; y++) {
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
               for (unsigned y=0; y < keyCount; y++) {
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
         if (scriptHash == BtcUtils::BadAddress) {
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
         case TxOutScriptType::STDHASH160:
         {
            //p2pkh, we should have a pubkey
            if (keysAndSigs.size() == 1) {
               feed->hashMap.emplace(scriptHash, keysAndSigs.begin()->key);
            }
            break;
         }

         case TxOutScriptType::STDPUBKEY33:
         case TxOutScriptType::MULTISIG:
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

std::string Signer::getSigCollectID() const
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

////////
std::string Signer::toString(SignerStringFormat ustxFormat) const
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
         std::string psbtStr{psbtBin.getCharPtr(), psbtBin.getSize()};
         serializedSigner = BtcUtils::base64_encode(psbtStr);
         break;
      }

      default:
         throw std::runtime_error("unsupported serialization format");
   }
   return serializedSigner;
}

std::string Signer::toTxSigCollect(bool isLegacy) const
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

   std::string lsStr{signerState.getDataRef().toCharPtr(), signerState.getSize()};
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

Signer Signer::fromString(const std::string& signerState)
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
         capnToSigner(theSigner, signerStateRef);
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

////////
bool Signer::isResolved() const
{
   /*
   Returns true if all spenders carry all relevant public data referenced by 
   the utxo's script
   */
   for (auto& spender : spenders_) {
      if (!spender->isResolved()) {
         return false;
      }
   }
   return true;
}

bool Signer::isSigned() const
{
   /*
   Return true is all spenders carry enough signatures. Does not check sigs,
   use ::verify() to check those.
   */
   for (auto& spender : spenders_) {
      if (!spender->isSigned()) {
         return false;
      }
   }
   return true;
}

////////
void Signer::resetFeed(void)
{
   resolverPtr_ = nullptr;
}

void Signing::Signer::populateUtxo(const UTXO& utxo)
{
   for (auto& spender : spenders_) {
      try {
         const auto& spenderUtxo = spender->getUtxo();
         if (spenderUtxo.isInitialized()) {
            if (spenderUtxo == utxo) {
               return;
            }
         }
      } catch (const std::exception&) {}

      auto outpoint = spender->getOutpoint();
      BinaryRefReader brr(outpoint);
         
      auto&& hash = brr.get_BinaryDataRef(32);
      if (hash != utxo.getTxHash()) {
         continue;
      }
      auto txoutid = brr.get_uint32_t();
      if (txoutid != utxo.getTxOutIndex()) {
         continue;
      }
      spender->setUtxo(utxo);
      return;
   }
   throw std::runtime_error("could not match utxo to any spender");
}

BinaryData Signer::getTxId_const() const
{
   try {
      auto txdataref = serializeSignedTx();
      Tx tx(txdataref);
      return tx.getThisHash();
   } catch (const std::exception&) {}

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

////////
BinaryData Signer::getTxId()
{
   if (!isResolved()) {
      resolvePublicData();
   }
   return getTxId_const();
}

void Signer::addSpender_ByOutpoint(
   const BinaryData& hash, unsigned index, unsigned sequence)
{
   auto spender = std::make_shared<ScriptSpender>(hash, index);
   spender->setSequence(sequence);

   addSpender(spender);
}

void Signer::addSpender(std::shared_ptr<ScriptSpender> ptr)
{
   for (const auto& spender : spenders_) {
      if (*ptr == *spender) {
         throw ScriptException("already carrying this spender");
      }
   }
   ptr->setTxMap(supportingTxMap_);
   spenders_.emplace_back(ptr);
}

void Signer::addRecipient(std::shared_ptr<ScriptRecipient> rec)
{
   addRecipient(rec, DEFAULT_RECIPIENT_GROUP);
}

void Signer::addRecipient(std::shared_ptr<ScriptRecipient> rec,
   unsigned groupId)
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

std::vector<std::shared_ptr<ScriptRecipient>> Signer::getRecipientVector() const
{
   std::vector<std::shared_ptr<ScriptRecipient>> result;
   for (auto& group : recipients_) {
      for (auto& rec : group.second) {
         result.emplace_back(rec);
      }
   }
   return result;
}

////////
bool Signer::verifySpenderEvalState() const
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

bool Signer::isSegWit() const
{
   for (auto& spender : spenders_) {
      if (spender->isSegWit()) {
         return true;
      }
   }
   return false;
}

bool Signer::hasLegacyInputs() const
{
   for (auto& spender : spenders_) {
      if (!spender->isSegWit()) {
         return true;
      }
   }
   return false;
}

void Signer::injectSignature(
   unsigned inputIndex, SecureBinaryData& sig, unsigned sigId)
{
   if (spenders_.size() < inputIndex) {
      throw std::runtime_error("invalid spender index");
   }
   auto& spender = spenders_[inputIndex];
   spender->injectSignature(sig, sigId);
}

////////
BinaryData Signer::toPSBT() const
{
   //init
   BinaryWriter bw;
   PSBT::init(bw);

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
      for (const auto& spender : spenders_) {
         bw.put_BinaryData(spender->getEmptySerializedInput());
      }

      //txout count
      auto recVector = getRecipientVector();
      bw.put_var_int(recVector.size());

      //txouts
      for (const auto& recipient : recVector) {
         bw.put_BinaryData(recipient->getSerializedScript());
      }

      //lock time
      bw.put_uint32_t(lockTime_);
      unsignedTx = std::move(bw.getData());
   }

   //unsigned tx
   PSBT::setUnsignedTx(bw, unsignedTx);

   //proprietary data
   for (const auto& data : prioprietaryPSBTData_) {
      //key
      bw.put_var_int(data.first.getSize() + 1);
      bw.put_uint8_t(PSBT::ENUM_GLOBAL::PSBT_GLOBAL_PROPRIETARY);
      bw.put_BinaryData(data.first);

      //val
      bw.put_var_int(data.second.getSize());
      bw.put_BinaryData(data.second);
   }

   PSBT::setSeparator(bw);

   /*inputs*/
   for (const auto& spender : spenders_) {
      spender->toPSBT(bw);
   }

   /*outputs*/
   for (const auto& recipient : getRecipientVector()) {
      recipient->toPSBT(bw);
   }

   //return
   return bw.getData();
}

Signer Signer::fromPSBT(const std::string& psbtString)
{
   BinaryDataRef psbtRef;
   psbtRef.setRef(psbtString);
   return Signer::fromPSBT(psbtRef);
}

Signer Signer::fromPSBT(BinaryDataRef psbtRef)
{
   Signer signer;
   BinaryRefReader brr(psbtRef);

   /** header section **/

   //magic word
   auto magic = brr.get_uint32_t(BE);

   //separator
   auto separator = brr.get_uint8_t();

   if (magic != PSBT::ENUM_GLOBAL::PSBT_GLOBAL_MAGICWORD ||
      separator != PSBT::ENUM_GLOBAL::PSBT_GLOBAL_SEPARATOR) {
      throw PSBT::DeserError("invalid header");
   }

   /** global section **/
   BinaryDataRef unsignedTxRef;

   //getPSBTDataPairs guarantees keys aren't empty
   auto globalDataPairs = BtcUtils::getPSBTDataPairs(brr);
   for (const auto& dataPair : globalDataPairs) {
      const auto& key = dataPair.first;
      const auto& val = dataPair.second;

      //key type
      auto typePtr = key.getPtr();

      switch (*typePtr)
      {
         case PSBT::ENUM_GLOBAL::PSBT_GLOBAL_UNSIGNED_TX:
         {
            //key has to be 1 byte long
            if (key.getSize() != 1) {
               throw PSBT::DeserError("invalid unsigned tx key length");
            }
            unsignedTxRef = val;
            break;
         }

         case PSBT::ENUM_GLOBAL::PSBT_GLOBAL_XPUB:
         {
            //skip for now
            break;
         }

         case PSBT::ENUM_GLOBAL::PSBT_GLOBAL_VERSION:
         {
            //sanity checks
            if (key.getSize() != 1) {
               throw PSBT::DeserError("invalid version key length");
            }
            if (val.getSize() != 4) {
               throw PSBT::DeserError("invalid version val length");
            }
            break;
         }

         case PSBT::ENUM_GLOBAL::PSBT_GLOBAL_PROPRIETARY:
         {
            //skip for now
            break;
         }

         default:
            throw PSBT::DeserError("unexpected global key");
      }
   }

   //sanity check
   if (unsignedTxRef.empty()) {
      throw PSBT::DeserError("missing unsigned tx");
   }

   Tx unsignedTx(unsignedTxRef);
   signer.setVersion(unsignedTx.getVersion());

   /** txin section **/
   for (unsigned i=0; i < unsignedTx.getNumTxIn(); i++) {
      auto txinCopy = unsignedTx.getTxInCopy(i);
      auto spender = ScriptSpender::fromPSBT(
         brr, txinCopy, signer.supportingTxMap_);
      signer.addSpender(spender);
   }

   /** txout section **/
   for (unsigned i=0; i < unsignedTx.getNumTxOut(); i++) {
      auto txoutCopy = unsignedTx.getTxOutCopy(i);
      auto recipient = ScriptRecipient::fromPSBT(brr, txoutCopy);
      signer.addRecipient(recipient);
   }
   return signer;
}

////////
void Signer::addSupportingTx(BinaryDataRef rawTxRef)
{
   if (rawTxRef.empty()) {
      return;
   }

   try {
      Tx tx(rawTxRef);
      addSupportingTx(std::move(tx));
   } catch (const std::exception&) {}
}

void Signer::addSupportingTx(Tx tx)
{
   supportingTxMap_->emplace(tx.getThisHash(), std::move(tx));
}

const Tx& Signer::getSupportingTx(const BinaryData& hash) const
{
   auto iter = supportingTxMap_->find(hash);
   if (iter == supportingTxMap_->end()) {
      throw std::runtime_error("unknown supporting tx hash");
   }
   return iter->second;
}

std::map<unsigned, BinaryData> Signer::getPubkeysForScript(
   BinaryDataRef& scriptRef, std::shared_ptr<ResolverFeed> feedPtr)
{
   auto scriptType = BtcUtils::getTxOutScriptType(scriptRef);
   std::map<unsigned, BinaryData> pubkeyMap;

   switch (scriptType)
   {
      case TxOutScriptType::P2WPKH:
      {
         auto hash = scriptRef.getSliceRef(2, 20);
         if (feedPtr != nullptr) {
            try {
               pubkeyMap.emplace(0, feedPtr->getByVal(hash));
            } catch (const std::exception&) {}
         }
         break;
      }

      case TxOutScriptType::STDHASH160:
      {
         auto hash = scriptRef.getSliceRef(3, 20);
         if (feedPtr != nullptr) {
            try {
               pubkeyMap.emplace(0, feedPtr->getByVal(hash));
            } catch (const std::exception&) {}
         }
         break;
      }

      case TxOutScriptType::STDPUBKEY33:
      {
         pubkeyMap.emplace(0, scriptRef.getSliceRef(1, 33));
         break;
      }

      case TxOutScriptType::MULTISIG:
      {
         std::vector<BinaryData> pubKeys;
         BtcUtils::getMultisigPubKeyList(scriptRef, pubKeys);
         for (unsigned i=0; i<pubKeys.size(); i++) {
            pubkeyMap.emplace(i, std::move(pubKeys[i]));
         }
         break;
      }

      default:
         break;
   }
   return pubkeyMap;
}

////////
uint64_t Signer::getTotalInputsValue(void) const
{
   uint64_t val = 0;
   for (auto& spender : spenders_) {
      val += spender->getValue();
   }
   return val;
}

uint64_t Signer::getTotalOutputsValue(void) const
{
   uint64_t val = 0;
   for (const auto& group : recipients_) {
      for (const auto& recipient : group.second) {
         val += recipient->getValue();
      }
   }
   return val;
}

uint32_t Signer::getTxInCount() const
{
   return spenders_.size();
}

uint32_t Signer::getTxOutCount() const
{
   uint32_t count = 0;
   for (const auto& group : recipients_) {
      count += group.second.size();
   }
   return count;
}

////////
void Signer::addBip32Root(std::shared_ptr<BIP32_PublicDerivedRoot> rootPtr)
{
   if (rootPtr == nullptr) {
      return;
   }
   bip32PublicRoots_.emplace(rootPtr->getThisFingerprint(), rootPtr);
}

void Signer::matchAssetPathsWithRoots()
{
   for (auto& spender : spenders_) {
      auto& paths = spender->getBip32Paths();
      for (auto& pathPair : paths) {
         auto fingerprint = pathPair.second.getThisFingerprint();
         auto iter = bip32PublicRoots_.find(fingerprint);
         if (iter == bip32PublicRoots_.end()) {
            continue;
         }
         pathPair.second.setRoot(iter->second);
      }
   }
}

////////
void Signer::prettyPrint() const
{
   /* NOTE: WIP */

   auto signEvalState = evaluateSignedState();

   std::cout << std::endl;
   std::stringstream ss;
   unsigned i=0;
   for (const auto& spender : spenders_) {
      spender->prettyPrint(ss);
      if (spender->isSigned()) {
         auto txInEvalState = signEvalState.getSignedStateForInput(i);
         ss << "    signed state: " << txInEvalState.isValid() << std::endl;
      }
      ++i;
   }

   for (const auto& group : recipients_) {
      auto groupId = WRITE_UINT32_BE(group.first);
      ss << " recipient group: " << groupId.toHexStr() << std::endl;

      for (const auto& rec : group.second) {
         auto serTxOut = rec->getSerializedScript();
         BinaryRefReader brr(serTxOut);
         brr.advance(8);
         auto len = brr.get_var_int();
         auto txOutScript = brr.get_BinaryDataRef(len);

         auto scrRef = BtcUtils::getTxOutScrAddrNoCopy(txOutScript);
         auto addrStr = BtcUtils::getAddressStrFromScrAddr(
            scrRef.getScrAddr());
         ss <<  "  val: " << rec->getValue() <<
            ", addr: " << addrStr << std::endl;
      }
   }
   std::cout << ss.str();
}

SignerStringFormat Signer::deserializedFromType() const
{
   return fromType_;
}

bool Signer::canLegacySerialize() const
{
   return !isSegWit();
}

////////////////////////////////////////////////////////////////////////////////
// ResolverFeed_SpenderResolutionChecks
BinaryData ResolverFeed_SpenderResolutionChecks::getByVal(const BinaryData& val)
{
   auto iter = hashMap.find(val);
   if (iter == hashMap.end()) {
      throw std::runtime_error("invalid value");
   }
   return iter->second;
}

const SecureBinaryData&
ResolverFeed_SpenderResolutionChecks::getPrivKeyForPubkey(const BinaryData&)
{
   throw std::runtime_error("invalid value");
}

BIP32_AssetPath ResolverFeed_SpenderResolutionChecks::resolveBip32PathForPubkey(
   const BinaryData&)
{
   throw std::runtime_error("invalid pubkey");
}

void ResolverFeed_SpenderResolutionChecks::setBip32PathForPubkey(
   const BinaryData&, const BIP32_AssetPath&)
{
   throw std::runtime_error("implement me?");
}

////////////////////////////////////////////////////////////////////////////////
// namespace functions
BinaryData Signing::signMessage(
   const BinaryData& message, const BinaryData& scrAddr,
   std::shared_ptr<ResolverFeed> walletFeed)
{
   //get pubkey for scrAddr. Resolver takes unprefixed hashes
   if (scrAddr.getSize() < 21) {
      throw std::runtime_error("invalid scrAddr");
   }

   auto pubkey = walletFeed->getByVal(
      scrAddr.getSliceRef(1, scrAddr.getSize() - 1));
   bool compressed = true;
   if (pubkey.getSize() == 65) {
      compressed = false;
   }

   //get private key for pubkey
   const auto& privkey = walletFeed->getPrivKeyForPubkey(pubkey);

   //sign
   return Cryptography::ECDSA::signBitcoinMessage(
      message.getRef(), privkey, compressed);
}

bool Signing::verifyMessageSignature(
   const BinaryData& message, const BinaryData& scrAddr, const BinaryData& sig)
{
   BinaryData pubkey;
   try {
      pubkey = Cryptography::ECDSA::verifyBitcoinMessage(message, sig);
   } catch (const std::exception& e) {
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
      case TxOutScriptType::P2WPKH:
      {
         auto addrPtr = std::make_shared<AddressEntry_P2WPKH>(assetPtr);
         if (addrPtr->getPrefixedHash() == scrAddr) {
            return true;
         }
         break;
      }

      case TxOutScriptType::STDHASH160:
      {
         auto addrPtr = std::make_shared<AddressEntry_P2PKH>(
            assetPtr, (pubkey.getSize() == 33) ? true : false);
         if (addrPtr->getPrefixedHash() == scrAddr) {
            return true;
         }
         break;
      }

      case TxOutScriptType::P2SH:
      {
         /*
         This is a complicated case, the scrAddr provides no information as
         to what script type preceeds the p2sh hash. We'll try p2wpkh and p2pk
         since these are common in armory.
         */

         auto addrPtr1 = std::make_shared<AddressEntry_P2WPKH>(assetPtr);
         auto p2shAddr = std::make_shared<AddressEntry_P2SH>(addrPtr1);
         if (p2shAddr->getPrefixedHash() == scrAddr) {
            return true;
         }
         auto addrPtr2 = std::make_shared<AddressEntry_P2PK>(assetPtr, true);
         p2shAddr = std::make_shared<AddressEntry_P2SH>(addrPtr2);
         if (p2shAddr->getPrefixedHash() == scrAddr) {
            return true;
         }
         break;
      }

      default:
         LOGWARN << "could not generate scrAddr from pubkey";
         return false;
   }

   LOGWARN << "failed to match sig's pubkey to scrAddr";
   return false;
}
