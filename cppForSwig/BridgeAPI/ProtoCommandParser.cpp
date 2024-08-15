////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2024, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ProtoCommandParser.h"
#include "CppBridge.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/Bridge.capnp.h"

using namespace Armory;
using namespace Armory::Bridge;

namespace
{
   using namespace Codec::Bridge;

   // helpers //
   BinaryData serializeCapnp(capnp::MallocMessageBuilder& msg)
   {
      auto flat = capnp::messageToFlatArray(msg);
      auto bytes = flat.asBytes();
      return BinaryData(bytes.begin(), bytes.end());
   }

   void utxosToCapnp(const std::vector<UTXO>& utxos,
      capnp::List<Codec::Types::Output, capnp::Kind::STRUCT>::Builder& capnOutputs)
   {
      for (unsigned i=0; i<utxos.size(); i++) {
         auto capnOutput = capnOutputs[i];
         const auto& utxo = utxos[i];

         capnOutput.setValue(utxo.getValue());
         capnOutput.setTxHeight(utxo.getHeight());
         capnOutput.setTxIndex(utxo.getTxIndex());
         capnOutput.setTxOutIndex(utxo.getTxOutIndex());

         const auto& txHash = utxo.getTxHash();
         capnOutput.setTxHash(capnp::Data::Builder(
            (uint8_t*)txHash.getPtr(), txHash.getSize()
         ));

         const auto& script = utxo.getScript();
         capnOutput.setScript(capnp::Data::Builder(
            (uint8_t*)script.getPtr(), script.getSize()
         ));
      }
   }

   // switchers //
   bool processBlockchainServiceCommands(
      std::shared_ptr<CppBridge> bridge, MessageId referenceId,
      Codec::Bridge::BlockchainServiceRequest::Reader& request)
   {
      BinaryData response;
      switch (request.which())
      {
         case BlockchainServiceRequest::GET_FEE_SCHEDULE:
         {
            auto strat = request.getGetFeeSchedule();
            bridge->getFeeSchedule(strat, referenceId);
            break;
         }

         case BlockchainServiceRequest::LOAD_WALLETS:
         {
            auto callbackId = request.getLoadWallets();
            bridge->loadWallets(callbackId, referenceId);
            break;
         }

         case BlockchainServiceRequest::SETUP_DB:
         {
            bridge->setupDB();
            break;
         }

         case BlockchainServiceRequest::GO_ONLINE:
         {
            if (bridge->bdvPtr() == nullptr) {
               throw std::runtime_error("null bdv ptr");
            }
            bridge->bdvPtr()->goOnline();
            break;
         }

         case BlockchainServiceRequest::SHUTDOWN:
         {
            bridge->reset();
            return false;
         }

         case BlockchainServiceRequest::REGISTER_WALLETS:
         {
            bridge->registerWallets();
            break;
         }

         case BlockchainServiceRequest::REGISTER_WALLET:
         {
            auto regWallet = request.getRegisterWallet();
            auto id = regWallet.getId();
            bridge->registerWallet(id, regWallet.getIsNew());
            break;
         }

         case BlockchainServiceRequest::GET_LEDGER_DELEGATE_ID:
         {
            const auto& delegateId = bridge->getLedgerDelegateId();
            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            auto service = reply.getService();

            service.setGetLedgerDelegateId(delegateId);
            reply.setSuccess(true);
            reply.setReferenceId(referenceId);

            response = serializeCapnp(message);
            break;
         }

         case BlockchainServiceRequest::UPDATE_WALLETS_LEDGER_FILTER:
         {
            auto walletsId = request.getUpdateWalletsLedgerFilter();
            std::vector<std::string> idVec;
            idVec.reserve(walletsId.size());
            for (const auto& walletId : walletsId) {
               idVec.emplace_back(walletId);
            }
            bridge->bdvPtr()->updateWalletsLedgerFilter(idVec);
            break;
         }

         case BlockchainServiceRequest::GET_NODE_STATUS:
         {
            response = bridge->getNodeStatus(referenceId);
            break;
         }

         case BlockchainServiceRequest::GET_HEADERS_BY_HEIGHT:
         {
            auto capnHeights = request.getGetHeadersByHeight();
            std::vector<unsigned> heights;
            heights.reserve(capnHeights.size());
            for (const auto& height : capnHeights) {
               heights.emplace_back(height);
            }
            bridge->getHeadersByHeight(heights, referenceId);
            break;
         }

         case BlockchainServiceRequest::GET_TXS_BY_HASH:
         {
            std::set<BinaryData> hashes;
            auto capnHashes = request.getGetTxsByHash();
            for (auto hash : capnHashes) {
               hashes.emplace(BinaryData(hash.begin(), hash.end()));
            }
            bridge->getTxsByHash(hashes, referenceId);
            break;
         }

         case BlockchainServiceRequest::BROADCAST_TX:
         {
            auto capnTxs = request.getBroadcastTx();
            std::vector<BinaryData> bdVec;
            bdVec.reserve(capnTxs.size());
            for (auto capnTx : capnTxs) {
               bdVec.emplace_back(
                  BinaryData(capnTx.begin(), capnTx.end()));
            }

            bridge->broadcastTx(bdVec);
            break;
         }

         case BlockchainServiceRequest::GET_BLOCK_TIME_BY_HEIGHT:
         {
            auto height = request.getGetBlockTimeByHeight();
            bridge->getBlockTimeByHeight(height, referenceId);
            break;
         }
      }

      if (!response.empty()) {
         //write response to socket
         bridge->writeToClient(response);
      }

      return true;
   }

   bool processWalletCommands(
      std::shared_ptr<CppBridge> bridge, MessageId referenceId,
      WalletRequest::Reader& request)
   {
      auto walletId = request.getId();

      BinaryData response;
      switch (request.which())
      {
         case WalletRequest::CREATE_BACKUP_STRING:
         {
            auto callbackId = request.getCreateBackupString();
            bridge->createBackupStringForWallet(
               walletId, callbackId, referenceId);
            break;
         }

         case WalletRequest::GET_LEDGER_DELEGATE_ID:
         {
            const auto& delegateId =
               bridge->getLedgerDelegateIdForWallet(walletId);

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            auto walletReply = reply.initWallet();
            walletReply.setGetLedgerDelegateId(delegateId);
            reply.setSuccess(true);
            reply.setReferenceId(referenceId);

            response = serializeCapnp(message);
            break;
         }

         case WalletRequest::GET_LEDGER_DELEGATE_ID_FOR_SCR_ADDR:
         {
            auto capnAddr = request.getGetLedgerDelegateIdForScrAddr();
            BinaryDataRef addr(capnAddr.begin(), capnAddr.end());
            const auto& delegateId =
               bridge->getLedgerDelegateIdForScrAddr(walletId, addr);

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            auto walletReply = reply.initWallet();
            walletReply.setGetLedgerDelegateIdForScrAddr(delegateId);
            reply.setSuccess(true);
            reply.setReferenceId(referenceId);

            response = serializeCapnp(message);
            break;
         }

         case WalletRequest::GET_BALANCE_AND_COUNT:
         {
            response = bridge->getBalanceAndCount(
               walletId, referenceId);
            break;
         }

         case WalletRequest::SETUP_NEW_COIN_SELECTION_INSTANCE:
         {
            bridge->setupNewCoinSelectionInstance(walletId,
               request.getSetupNewCoinSelectionInstance(), referenceId);
            break;
         }

         case WalletRequest::GET_ADDR_COMBINED_LIST:
         {
            response = bridge->getAddrCombinedList(
               walletId, referenceId);
            break;
         }

         case WalletRequest::GET_HIGHEST_USED_INDEX:
         {
            response = bridge->getHighestUsedIndex(
               walletId, referenceId);
            break;
         }

         case WalletRequest::EXTEND_ADDRESS_POOL:
         {
            auto args = request.getExtendAddressPool();
            bridge->extendAddressPool(walletId, args.getCount(),
               args.getCallbackId(), referenceId);
            break;
         }

         case WalletRequest::DELETE:
         {
            auto result = bridge->deleteWallet(walletId);
            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            reply.setSuccess(true);
            reply.setReferenceId(referenceId);

            response = serializeCapnp(message);
            break;
         }

         case WalletRequest::GET_DATA:
         {
            response = bridge->getWalletPacket(
               walletId, referenceId);
            break;
         }

         case WalletRequest::SET_ADDRESS_TYPE_FOR:
         {
            auto args = request.getSetAddressTypeFor();
            auto capnAssetId = args.getAssetId();
            BinaryDataRef assetId(capnAssetId.begin(), capnAssetId.end());

            response = bridge->setAddressTypeFor(
               walletId, assetId, args.getAddressType(), referenceId);
            break;
         }

         case WalletRequest::CREATE_ADDRESS_BOOK:
         {
            bridge->createAddressBook(walletId, referenceId);
            break;
         }

         case WalletRequest::SET_COMMENT:
         {
            auto args = request.getSetComment();
            bridge->setComment(walletId, args.getKey(), args.getComment());
            break;
         }

         case WalletRequest::SET_LABELS:
         {
            auto args = request.getSetLabels();
            bridge->setWalletLabels(walletId,
               args.getTitle(), args.getDescription());
            break;
         }

         case WalletRequest::GET_UTXOS:
         {
            auto args = request.getGetUtxos();
            uint64_t value = 0;
            if (args.isValue()) {
               value = args.getValue();
            }
            bridge->getUTXOs(walletId,
               value, args.isZc(), args.isRbf(),
               referenceId);
            break;
         }

         case WalletRequest::GET_ADDRESS:
         {
            auto args = request.getGetAddress();
            response = bridge->getAddress(walletId,
               args.getType(), args.which(), referenceId);
            break;
         }
      }

      if (!response.empty()) {
         bridge->writeToClient(response);
      }
      return true;
   }

   bool processCoinSelectionCommands(
      std::shared_ptr<CppBridge> bridge, MessageId referenceId,
      CoinSelectionRequest::Reader& request)
   {
      auto csId = request.getId();
      auto cs = bridge->coinSelectionInstance(csId);
      if (cs == nullptr) {
         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<FromBridge>();
         auto reply = fromBridge.getReply();
         reply.setSuccess(false);
         reply.setReferenceId(referenceId);

         auto serialized = serializeCapnp(message);
         bridge->writeToClient(serialized);
         return true;
      }

      BinaryData response;
      switch (request.which())
      {
         case CoinSelectionRequest::CLEANUP:
         {
            bridge->destroyCoinSelectionInstance(csId);
            break;
         }

         case CoinSelectionRequest::RESET:
         {
            cs->resetRecipients();
            break;
         }

         case CoinSelectionRequest::SET_RECIPIENT:
         {
            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            reply.setReferenceId(referenceId);

            auto args = request.getSetRecipient();
            try {
               cs->updateRecipient(args.getId(),
                  args.getAddress(), args.getValue());
               reply.setSuccess(true);
            } catch (const std::exception& e) {
               reply.setSuccess(false);
               reply.setError(e.what());
            }

            response = serializeCapnp(message);
            break;
         }

         case CoinSelectionRequest::SELECT_UTXOS:
         {
            auto args = request.getSelectUtxos();
            uint64_t flatFee = 0;
            float feeByte = 0;
            switch (args.which())
            {
               case CoinSelectionRequest::SelectUTXOs::FLAT_FEE:
                  flatFee = args.getFlatFee();
                  break;

               case CoinSelectionRequest::SelectUTXOs::FEE_BYTE:
                  feeByte = args.getFeeByte();
                  break;
            }
            auto success = cs->selectUTXOs(flatFee, feeByte, args.getFlags());

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            reply.setReferenceId(referenceId);
            reply.setSuccess(success);

            response = serializeCapnp(message);
            break;
         }

         case CoinSelectionRequest::GET_UTXO_SELECTION:
         {
            auto utxoVec = cs->getUtxoSelection();

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            reply.setReferenceId(referenceId);
            reply.setSuccess(true);

            auto csReply = reply.initCoinSelection();
            auto capnUtxos = csReply.initGetUtxoSelection(utxoVec.size());
            utxosToCapnp(utxoVec, capnUtxos);

            response = serializeCapnp(message);
            break;
         }

         case CoinSelectionRequest::GET_FLAT_FEE:
         {
            auto flatFee = cs->getFlatFee();

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            reply.setReferenceId(referenceId);
            reply.setSuccess(true);

            auto csReply = reply.initCoinSelection();
            csReply.setGetFlatFee(flatFee);

            response = serializeCapnp(message);
            break;
         }

         case CoinSelectionRequest::GET_FEE_BYTE:
         {
            auto feeByte = cs->getFeeByte();

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            reply.setReferenceId(referenceId);
            reply.setSuccess(true);

            auto csReply = reply.initCoinSelection();
            csReply.setGetFeeByte(feeByte);

            response = serializeCapnp(message);
            break;
         }

         case CoinSelectionRequest::GET_SIZE_ESTIMATE:
         {
            auto sizeEstimate = cs->getSizeEstimate();

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            reply.setReferenceId(referenceId);
            reply.setSuccess(true);

            auto csReply = reply.initCoinSelection();
            csReply.setGetSizeEstimate(sizeEstimate);

            response = serializeCapnp(message);
            break;
         }

         case CoinSelectionRequest::PROCESS_CUSTOM_UTXO_LIST:
         {
            auto args = request.getProcessCustomUtxoList();

            auto capnUtxos = args.getUtxos();
            std::vector<UTXO> utxos;
            utxos.reserve(capnUtxos.size());
            for (auto capnUtxo : capnUtxos) {
               auto capnHash = capnUtxo.getTxHash();
               auto hash = BinaryDataRef(capnHash.begin(), capnHash.end());

               auto capnScript = capnUtxo.getScript();
               auto script = BinaryDataRef(capnScript.begin(), capnScript.end());

               utxos.emplace_back(UTXO{capnUtxo.getValue(),
                  capnUtxo.getTxHeight(), capnUtxo.getTxIndex(), capnUtxo.getTxOutIndex(),
                  hash, script});
            }

            uint64_t flatFee = 0;
            float feeByte = 0;
            if (args.isFlatFee()) {
               flatFee = args.getFlatFee();
            } else {
               feeByte = args.getFeeByte();
            }

            bool success = true;
            try {
               cs->processCustomUtxoList(utxos, flatFee, feeByte, args.getFlags());
            } catch (const std::exception&) {
               success = false;
            }

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            reply.setReferenceId(referenceId);
            reply.setSuccess(success);

            response = serializeCapnp(message);
            break;
         }

         case CoinSelectionRequest::GET_FEE_FOR_MAX_VAL:
         {
            auto args = request.getGetFeeForMaxVal();
            auto capnUtxos = args.getUtxos();
            float flatFee = 0;
            if (capnUtxos.size() == 0) {
               flatFee = cs->getFeeForMaxVal(args.getFeeByte());
            } else {
               std::vector<BinaryData> serUtxos;
               serUtxos.reserve(capnUtxos.size());
               for (auto capnUtxo : capnUtxos) {
                  auto capnHash = capnUtxo.getTxHash();
                  auto hash = BinaryDataRef(capnHash.begin(), capnHash.end());

                  auto capnScript = capnUtxo.getScript();
                  auto script = BinaryDataRef(capnScript.begin(), capnScript.end());

                  UTXO utxo(capnUtxo.getValue(), capnUtxo.getTxHeight(),
                     capnUtxo.getTxIndex(), capnUtxo.getTxOutIndex(),
                     hash, script);
                  serUtxos.emplace_back(utxo.serialize());
               }

               flatFee = cs->getFeeForMaxValUtxoVector(
                  serUtxos, args.getFeeByte());
            }

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            reply.setReferenceId(referenceId);
            reply.setSuccess(true);

            auto csReply = reply.initCoinSelection();
            csReply.setGetFeeForMaxVal(flatFee);

            response = serializeCapnp(message);
            break;
         }
      }

      if (!response.empty()) {
         //write response to socket
         bridge->writeToClient(response);
      }
      return true;
   }

   bool processSignerCommands(
      std::shared_ptr<CppBridge> bridge, MessageId referenceId,
      SignerRequest::Reader& request)
   {
      auto replySuccess = [referenceId, bridge](bool success)->void
      {
         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<FromBridge>();
         auto reply = fromBridge.getReply();
         reply.setSuccess(success);
         reply.setReferenceId(referenceId);

         //serialize & push reply
         auto serialized = serializeCapnp(message);
         bridge->writeToClient(serialized);
      };

      auto signerId = request.getId();
      auto signer = bridge->signerInstance(signerId);
      if (signer == nullptr) {
         replySuccess(false);
         return true;
      }

      BinaryData response;
      switch (request.which())
      {
         case SignerRequest::GET_NEW:
         {
            response = bridge->initNewSigner(referenceId);
            break;
         }

         case SignerRequest::CLEANUP:
         {
            bridge->destroySigner(signerId);
            break;
         }

         case SignerRequest::SET_VERSION:
         {
            signer->signer_.setVersion(request.getSetVersion());
            replySuccess(true);
            break;
         }

         case SignerRequest::SET_LOCK_TIME:
         {
            signer->signer_.setLockTime(request.getSetLockTime());
            replySuccess(true);
            break;
         }

         case SignerRequest::ADD_SPENDER_BY_OUTPOINT:
         {
            auto args = request.getAddSpenderByOutpoint();
            auto capnHash = args.getHash();
            BinaryDataRef hashRef(capnHash.begin(), capnHash.end());
            signer->signer_.addSpender_ByOutpoint(hashRef,
               args.getTxOutId(), args.getSequence());

            replySuccess(true);
            break;
         }

         case SignerRequest::POPULATE_UTXO:
         {
            auto args = request.getPopulateUtxo();

            auto capnHash = args.getHash();
            BinaryDataRef hashRef(capnHash.begin(), capnHash.end());

            auto capnScript = args.getScript();
            BinaryDataRef scriptRef(capnScript.begin(), capnScript.end());

            UTXO utxo(args.getValue(), UINT32_MAX, UINT32_MAX,
               args.getTxOutId(), hashRef, scriptRef);
            signer->signer_.populateUtxo(utxo);

            replySuccess(true);
            break;
         }

         case SignerRequest::ADD_SUPPORTING_TX:
         {
            auto capnTx = request.getAddSupportingTx();
            BinaryDataRef txRef(capnTx.begin(), capnTx.end());
            signer->signer_.addSupportingTx(txRef);

            replySuccess(true);
            break;
         }

         case SignerRequest::ADD_RECIPIENT:
         {
            auto args = request.getAddRecipient();

            //grab script, compute scrAddr hash from it
            auto capnScript = args.getScript();
            BinaryDataRef scriptRef(capnScript.begin(), capnScript.end());
            auto hash = BtcUtils::getTxOutScrAddr(scriptRef);

            signer->signer_.addRecipient(
               CoinSelection::CoinSelectionInstance::createRecipient(
                  hash, args.getValue()));

            replySuccess(true);
            break;
         }

         case SignerRequest::TO_TX_SIG_COLLECT:
         {
            auto txSigCollect = signer->signer_.toString(
               static_cast<Signing::SignerStringFormat>(
                  request.getToTxSigCollect()));

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            reply.setSuccess(true);
            reply.setReferenceId(referenceId);

            auto signerReply = reply.initSigner();
            signerReply.setToTxSigCollect(txSigCollect);

            response = serializeCapnp(message);
            break;
         }

         case SignerRequest::FROM_TX_SIG_COLLECT:
         {
            signer->signer_ = Signing::Signer::fromString(
               request.getFromTxSigCollect());

            replySuccess(true);
            break;
         }

         case SignerRequest::SIGN_TX:
         {
            auto args = request.getSignTx();
            signer->signTx(args.getWalletId(), args.getCallbackId(),
               referenceId);
            break;
         }

         case SignerRequest::GET_SIGNED_TX:
         {
            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            reply.setReferenceId(referenceId);

            try {
               auto signedTx = signer->signer_.serializeSignedTx();
               reply.setSuccess(true);

               auto signerReply = reply.initSigner();
               signerReply.setGetSignedTx(capnp::Data::Builder(
                  (uint8_t*)signedTx.getPtr(), signedTx.getSize()
               ));
            } catch (const std::exception&) {
               reply.setSuccess(false);
            }

            response = serializeCapnp(message);
            break;
         }

         case SignerRequest::GET_UNSIGNED_TX:
         {
            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            reply.setReferenceId(referenceId);

            try {
               auto signedTx = signer->signer_.serializeUnsignedTx();
               reply.setSuccess(true);

               auto signerReply = reply.initSigner();
               signerReply.setGetUnsignedTx(capnp::Data::Builder(
                  (uint8_t*)signedTx.getPtr(), signedTx.getSize()
               ));
            } catch (const std::exception&) {
               reply.setSuccess(false);
            }

            response = serializeCapnp(message);
            break;
         }

         case SignerRequest::RESOLVE:
         {
            auto result = signer->resolve(request.getResolve());
            replySuccess(result);
            break;
         }

         case SignerRequest::GET_SIGNED_STATE_FOR_INPUT:
         {
            response = signer->getSignedStateForInput(
               request.getGetSignedStateForInput());
            break;
         }

         case SignerRequest::FROM_TYPE:
         {
            auto result = signer->signer_.deserializedFromType();

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            reply.setReferenceId(referenceId);
            reply.setSuccess(true);

            auto signerReply = reply.initSigner();
            signerReply.setFromType((uint32_t)result);

            response = serializeCapnp(message);
            break;
         }

         case SignerRequest::CAN_LEGACY_SERIALIZE:
         {
            auto result = signer->signer_.canLegacySerialize();
            replySuccess(result);
            break;
         }
      }

      if (!response.empty()) {
         //write response to socket
         bridge->writeToClient(response);
      }
      return true;
   }

   bool processUtilsCommands(
      std::shared_ptr<CppBridge> bridge, MessageId referenceId,
      UtilsRequest::Reader& request)
   {
      BinaryData response;
      switch (request.which())
      {
         case UtilsRequest::CREATE_WALLET:
         {
            auto args = request.getCreateWallet();

            auto capnPassphrase = args.getPassphrase();
            SecureBinaryData sbdPass(
               (uint8_t*)capnPassphrase.begin(),
               (uint8_t*)capnPassphrase.end()
            );

            auto capnControlPass = args.getControlPassphrase();
            SecureBinaryData sbdControl(
               (uint8_t*)capnControlPass.begin(),
               (uint8_t*)capnControlPass.end()
            );

            auto capnEntropy = args.getExtraEntropy();
            SecureBinaryData sbdEntropy(
               (uint8_t*)capnEntropy.begin(),
               (uint8_t*)capnEntropy.end()
            );

            auto wltId = bridge->createWallet(
               args.getLookup(),
               args.getLabel(), args.getDescription(),
               sbdControl, sbdPass, sbdEntropy
            );

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            reply.setReferenceId(referenceId);
            reply.setSuccess(true);

            auto utilsReply = reply.initUtils();
            utilsReply.setCreateWallet(wltId);

            response = serializeCapnp(message);
            break;
         }

         case UtilsRequest::GENERATE_RANDOM_HEX:
         {
            auto str = bridge->generateRandom(
               request.getGenerateRandomHex()).toHexStr();

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            reply.setReferenceId(referenceId);
            reply.setSuccess(true);

            auto utilsReply = reply.initUtils();
            utilsReply.setGenerateRandomHex(str);

            response = serializeCapnp(message);
            break;
         }

         case UtilsRequest::GET_HASH160:
         {
            auto capnData = request.getGetHash160();
            BinaryDataRef dataRef(capnData.begin(), capnData.end());
            response = bridge->getHash160(dataRef, referenceId);
            break;
         }

         case UtilsRequest::GET_NAME_FOR_ADDR_TYPE:
         {
            auto typeName = bridge->getNameForAddrType(
               request.getGetNameForAddrType());

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.getReply();
            reply.setReferenceId(referenceId);
            reply.setSuccess(true);

            auto utilsReply = reply.initUtils();
            utilsReply.setGetNameForAddrType(typeName);

            response = serializeCapnp(message);
            break;
         }
      }

      if (!response.empty()) {
         //write response to socket
         bridge->writeToClient(response);
      }
      return true;
   }

   bool processScriptUtilsCommands(
      std::shared_ptr<CppBridge> bridge, MessageId referenceId,
      ScriptUtilsRequest::Reader& request)
   {
      auto capnScript = request.getScript();
      BinaryDataRef scriptRef(capnScript.begin(), capnScript.end());
      BinaryData response;

      switch (request.which())
      {
         case ScriptUtilsRequest::GET_TX_IN_SCRIPT_TYPE:
         {
            auto capnHash = request.getGetTxInScriptType();
            BinaryDataRef hashRef(capnHash.begin(), capnHash.end());
            response = bridge->getTxInScriptType(
               scriptRef, hashRef, referenceId);
            break;
         }

         case ScriptUtilsRequest::GET_TX_OUT_SCRIPT_TYPE:
         {
            response = bridge->getTxOutScriptType(
               scriptRef, referenceId);
            break;
         }

         case ScriptUtilsRequest::GET_SCR_ADDR_FOR_SCRIPT:
         {
            response = bridge->getScrAddrForScript(
               scriptRef, referenceId);
            break;
         }

         case ScriptUtilsRequest::GET_LAST_PUSH_DATA_IN_SCRIPT:
         {
            response = bridge->getLastPushDataInScript(
               scriptRef, referenceId);
            break;
         }

         case ScriptUtilsRequest::GET_TX_OUT_SCRIPT_FOR_SCR_ADDR:
         {
            response = bridge->getTxOutScriptForScrAddr(
               scriptRef, referenceId);
            break;
         }

         case ScriptUtilsRequest::GET_ADDR_STR_FOR_SCR_ADDR:
         {
            response = bridge->getAddrStrForScrAddr(
               scriptRef, referenceId);
            break;
         }

         case ScriptUtilsRequest::GET_SCR_ADDR_FOR_ADDR_STR:
         {
            response = bridge->getScrAddrForAddrStr(
               request.getGetScrAddrForAddrStr(), referenceId);
            break;
         }
      }

      if (!response.empty()) {
         //write response to socket
         bridge->writeToClient(response);
      }
      return true;
   }

   bool processNotificationReply(
      std::shared_ptr<CppBridge> bridge, MessageId referenceId,
      NotificationReply::Reader& notif)
   {
      try {
         auto handler = bridge->getCallbackHandler(referenceId);
         auto sbdPass = SecureBinaryData::fromString(notif.getPassphrase());
         return handler(notif.getSuccess(), sbdPass);
      } catch (const std::runtime_error&) {
         return false;
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
bool ProtoCommandParser::processData(
   std::shared_ptr<CppBridge> bridge, BinaryDataRef socketData)
{
   kj::ArrayPtr<const capnp::word> words(
      reinterpret_cast<const capnp::word*>(socketData.getPtr()),
      socketData.getSize() / sizeof(capnp::word));
   capnp::FlatArrayMessageReader reader(words);
   auto toBridge = reader.getRoot<Codec::Bridge::ToBridge>();
   auto referenceId = toBridge.getReferenceId();

   switch (toBridge.which())
   {
      case Codec::Bridge::ToBridge::SERVICE:
      {
         auto service = toBridge.getService();
         return processBlockchainServiceCommands(
            bridge, referenceId, service);
      }

      case Codec::Bridge::ToBridge::WALLET:
      {
         auto wallet = toBridge.getWallet();
         return processWalletCommands(
            bridge, referenceId, wallet);
      }

      case Codec::Bridge::ToBridge::COIN_SELECTION:
      {
         auto coinSelection = toBridge.getCoinSelection();
         return processCoinSelectionCommands(
            bridge, referenceId, coinSelection);
      }

      case Codec::Bridge::ToBridge::SIGNER:
      {
         auto signer = toBridge.getSigner();
         return processSignerCommands(
            bridge, referenceId, signer);
      }

      case Codec::Bridge::ToBridge::UTILS:
      {
         auto utils = toBridge.getUtils();
         return processUtilsCommands(
            bridge, referenceId, utils);
      }

      case Codec::Bridge::ToBridge::SCRIPT_UTILS:
      {
         auto scriptUtils = toBridge.getScriptUtils();
         return processScriptUtilsCommands(
            bridge, referenceId, scriptUtils);
      }

      case Codec::Bridge::ToBridge::NOTIFICATION:
      {
         auto notifReply = toBridge.getNotification();
         return processNotificationReply(
            bridge, referenceId, notifReply);
      }

      default:
         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<FromBridge>();
         auto reply = fromBridge.getReply();
         reply.setReferenceId(referenceId);
         reply.setSuccess(true);

         auto serialized = serializeCapnp(message);
         bridge->writeToClient(serialized);
         return false;
   }

   return true;
}
