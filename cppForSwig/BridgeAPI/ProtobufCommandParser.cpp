////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-23, goatpig                                            //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ProtobufCommandParser.h"
#include "CppBridge.h"
#include "../protobuf/BridgeProto.pb.h"
#include "ProtobufConversions.h"

using namespace std;
using namespace Armory::Bridge;
using namespace ::google::protobuf;
using namespace ::BridgeProto;

////////////////////////////////////////////////////////////////////////////////
bool ProtobufCommandParser::processBlockchainServiceCommands(CppBridge* bridge,
   unsigned referenceId, const BridgeProto::BlockchainService& msg)
{
   BridgePayload response;
   switch (msg.method_case())
   {
      case BridgeProto::BlockchainService::kEstimateFee:
      {
         const auto& estimateFeeMsg = msg.estimate_fee();
         bridge->estimateFee(estimateFeeMsg.blocks(),
            estimateFeeMsg.strat(), referenceId);
         break;
      }

      case BridgeProto::BlockchainService::kLoadWallets:
      {
         bridge->loadWallets(msg.load_wallets().callback_id(),
            referenceId);
         break;
      }

      case BridgeProto::BlockchainService::kSetupDb:
      {
         bridge->setupDB();
         break;
      }

      case BridgeProto::BlockchainService::kGoOnline:
      {
         if (bridge->bdvPtr_ == nullptr)
            throw runtime_error("null bdv ptr");
         bridge->bdvPtr_->goOnline();
         break;
      }

      case BridgeProto::BlockchainService::kShutdown:
      {
         if (bridge->bdvPtr_ != nullptr)
         {
            bridge->bdvPtr_->unregisterFromDB();
            bridge->bdvPtr_.reset();
            bridge->callbackPtr_.reset();
         }
         return false;
      }

      case BridgeProto::BlockchainService::kRegisterWallets:
      {
         bridge->registerWallets();
         break;
      }

      case BridgeProto::BlockchainService::kRegisterWallet:
      {
         const auto& registerWalletMsg = msg.register_wallet();
         bridge->registerWallet(registerWalletMsg.id(),
            registerWalletMsg.is_new());
         break;
      }

      case BridgeProto::BlockchainService::kGetLedgerDelegateIdForWallets:
      {
         auto& delegateId = bridge->getLedgerDelegateIdForWallets();
         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);

         reply->mutable_service()->set_ledger_delegate_id(delegateId);
         response = move(payload);
         break;
      }

      case BridgeProto::BlockchainService::kUpdateWalletsLedgerFilter:
      {
         vector<BinaryData> idVec;
         const auto& updateWalletsMsg = msg.update_wallets_ledger_filter();
         idVec.reserve(updateWalletsMsg.wallet_id_size());

         for (int i=0; i<updateWalletsMsg.wallet_id_size(); i++)
         {
            idVec.emplace_back((BinaryData::fromString(
               updateWalletsMsg.wallet_id(i))));
         }

         bridge->bdvPtr_->updateWalletsLedgerFilter(idVec);
         break;
      }

      case BridgeProto::BlockchainService::kGetHistoryPageForDelegate:
      {
         const auto& getHistoryPageMsg = msg.get_history_page_for_delegate();
         bridge->getHistoryPageForDelegate(getHistoryPageMsg.delegate_id(),
            getHistoryPageMsg.page_id(), referenceId);
         break;
      }

      case BridgeProto::BlockchainService::kGetHistoryForWalletSelection:
      {
         const auto& getHistoryMsg = msg.get_history_for_wallet_selection();
         vector<string> wltIdVec;
         wltIdVec.reserve(getHistoryMsg.wallet_id_size());

         for (int i=1; i<getHistoryMsg.wallet_id_size(); i++)
            wltIdVec.emplace_back(getHistoryMsg.wallet_id(i));

         bridge->getHistoryForWalletSelection(getHistoryMsg.order(),
            wltIdVec, referenceId);
         break;
      }

      case BridgeProto::BlockchainService::kGetNodeStatus:
      {
         response = move(bridge->getNodeStatus());
         break;
      }

      case BridgeProto::BlockchainService::kGetHeaderByHeight:
      {
         bridge->getHeaderByHeight(msg.get_header_by_height().height(),
            referenceId);
         break;
      }

      case BridgeProto::BlockchainService::kGetTxByHash:
      {
         const auto& hash = BinaryData::fromString(
            msg.get_tx_by_hash().tx_hash());
         bridge->getTxByHash(hash, referenceId);
         break;
      }

      case BridgeProto::BlockchainService::kBroadcastTx:
      {
         const auto& broadcastMsg = msg.broadcast_tx();
         vector<BinaryData> bdVec;
         bdVec.reserve(broadcastMsg.raw_tx_size());
         for (int i=0; i<broadcastMsg.raw_tx_size(); i++)
         {
            bdVec.emplace_back(
               BinaryData::fromString(broadcastMsg.raw_tx(i)));
         }

         bridge->broadcastTx(bdVec);
         break;
      }

      case BridgeProto::BlockchainService::kGetBlockTimeByHeight:
      {
         bridge->getBlockTimeByHeight(
            msg.get_block_time_by_height().height(), referenceId);
         break;
      }
   }

   if (response != nullptr)
   {
      //write response to socket
      response->mutable_reply()->set_reference_id(referenceId);
      bridge->writeToClient(move(response));
   }
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool ProtobufCommandParser::processWalletCommands(CppBridge* bridge,
   unsigned referenceId, const BridgeProto::Wallet& msg)
{
   BridgePayload response;
   switch (msg.method_case())
   {
      case BridgeProto::Wallet::kCreateBackupString:
      {
         bridge->createBackupStringForWallet(msg.id(),
            msg.create_backup_string().callback_id(), referenceId);
         break;
      }

      case BridgeProto::Wallet::kGetLedgerDelegateIdForScraddr:
      {
         auto addrHashRef = BinaryDataRef::fromString(
            msg.get_ledger_delegate_id_for_scraddr().hash());
         const auto& delegateId = bridge->getLedgerDelegateIdForScrAddr(
            msg.id(), addrHashRef);

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);

         reply->mutable_wallet()->set_ledger_delegate_id(delegateId);
         response = move(payload);
         break;
      }

      case BridgeProto::Wallet::kGetBalanceAndCount:
      {
         response = move(bridge->getBalanceAndCount(msg.id()));
         break;
      }

      case BridgeProto::Wallet::kSetupNewCoinSelectionInstance:
      {
         bridge->setupNewCoinSelectionInstance(msg.id(),
            msg.setup_new_coin_selection_instance().height(), referenceId);
         break;
      }

      case BridgeProto::Wallet::kGetAddrCombinedList:
      {
         response = move(bridge->getAddrCombinedList(msg.id()));
         break;
      }

      case BridgeProto::Wallet::kGetHighestUsedIndex:
      {
         response = move(bridge->getHighestUsedIndex(msg.id()));
         break;
      }

      case BridgeProto::Wallet::kExtendAddressPool:
      {
         const auto& extendMsg = msg.extend_address_pool();
         bridge->extendAddressPool(msg.id(), extendMsg.count(),
            extendMsg.callback_id(), referenceId);
         break;
      }

      case BridgeProto::Wallet::kDelete:
      {
         auto result = bridge->deleteWallet(msg.id());
         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(result);
         response = move(payload);
         break;
      }

      case BridgeProto::Wallet::kGetData:
      {
         response = move(bridge->getWalletPacket(msg.id()));
         break;
      }

      case BridgeProto::Wallet::kSetAddressTypeFor:
      {
         const auto& setAddrMsg = msg.set_address_type_for();
         response = bridge->setAddressTypeFor(
            msg.id(), setAddrMsg.asset_id(), setAddrMsg.address_type());
         response->mutable_reply()->set_reference_id(referenceId);
         break;
      }

      case BridgeProto::Wallet::kCreateAddressBook:
      {
         bridge->createAddressBook(msg.id(), referenceId);
         break;
      }

      case BridgeProto::Wallet::kSetComment:
      {
         bridge->setComment(msg.id(), msg.set_comment());
         break;
      }

      case BridgeProto::Wallet::kSetLabels:
      {
         bridge->setWalletLabels(msg.id(), msg.set_labels());
         break;
      }

      case BridgeProto::Wallet::kGetUtxosForValue:
      {
         bridge->getUtxosForValue(msg.id(),
            msg.get_utxos_for_value().value(), referenceId);
         break;
      }

      case BridgeProto::Wallet::kGetSpendableZcList:
      {
         bridge->getSpendableZCList(msg.id(), referenceId);
         break;
      }

      case BridgeProto::Wallet::kGetRbfTxoutList:
      {
         bridge->getRBFTxOutList(msg.id(), referenceId);
         break;
      }

      case BridgeProto::Wallet::kGetNewAddress:
      {
         response = bridge->getNewAddress(msg.id(),
            msg.get_new_address().type());
         break;
      }

      case BridgeProto::Wallet::kGetChangeAddress:
      {
         response = bridge->getChangeAddress(msg.id(),
            msg.get_change_address().type());
         break;
      }

      case BridgeProto::Wallet::kPeekChangeAddress:
      {
         response = bridge->peekChangeAddress(msg.id(),
            msg.peek_change_address().type());
         break;
      }
   }

   if (response != nullptr)
   {
      //write response to socket
      response->mutable_reply()->set_reference_id(referenceId);
      bridge->writeToClient(move(response));
   }
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool ProtobufCommandParser::processCoinSelectionCommands(CppBridge* bridge,
   unsigned referenceId, const BridgeProto::CoinSelection& msg)
{
   auto cs = bridge->coinSelectionInstance(msg.id());
   if (cs == nullptr)
   {
      auto payload = make_unique<BridgeProto::Payload>();
      auto reply = payload->mutable_reply();
      reply->set_success(false);
      bridge->writeToClient(move(payload));
      return true;
   }

   BridgePayload response;
   switch (msg.method_case())
   {
      case BridgeProto::CoinSelection::kCleanup:
      {
         bridge->destroyCoinSelectionInstance(msg.id());
         break;
      }

      case BridgeProto::CoinSelection::kReset:
      {
         cs->resetRecipients();
         break;
      }

      case BridgeProto::CoinSelection::kSetRecipient:
      {
         const auto& setRecipientMsg = msg.set_recipient();
         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         try
         {
            cs->updateRecipient(setRecipientMsg.id(),
               setRecipientMsg.address(), setRecipientMsg.value());
            reply->set_success(true);
         }
         catch (const std::exception& e)
         {
            reply->set_success(false);
            reply->set_error(e.what());
         }

         response = move(payload);
         break;
      }

      case BridgeProto::CoinSelection::kSelectUtxos:
      {
         const auto& selectMsg = msg.select_utxos();
         uint64_t flatFee = 0;
         float feeByte = 0;
         switch (selectMsg.fee_case())
         {
            case BridgeProto::CoinSelection::SelectUTXOs::kFlatFee:
               flatFee = selectMsg.flat_fee();
               break;

            case BridgeProto::CoinSelection::SelectUTXOs::kFeeByte:
               feeByte = selectMsg.fee_byte();
               break;
         }
         cs->selectUTXOs(flatFee, feeByte, selectMsg.flags());

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);
         response = move(payload);
         break;
      }

      case BridgeProto::CoinSelection::kGetUtxoSelection:
      {
         auto&& utxoVec = cs->getUtxoSelection();

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);

         auto utxoList = reply->mutable_coin_selection()->mutable_utxo_list();
         for (auto& utxo : utxoVec)
         {
            auto utxoProto = utxoList->add_utxo();
            CppToProto::utxo(utxoProto, utxo);
         }
         response = move(payload);
         break;
      }

      case BridgeProto::CoinSelection::kGetFlatFee:
      {
         auto flatFee = cs->getFlatFee();

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);

         reply->mutable_coin_selection()->set_flat_fee(flatFee);
         response = move(payload);
         break;
      }

      case BridgeProto::CoinSelection::kGetFeeByte:
      {
         auto feeByte = cs->getFeeByte();

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);

         reply->mutable_coin_selection()->set_fee_byte(feeByte);
         response = move(payload);
         break;
      }

      case BridgeProto::CoinSelection::kGetSizeEstimate:
      {
         auto sizeEstimate = cs->getSizeEstimate();

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);

         reply->mutable_coin_selection()->set_size_estimate(sizeEstimate);
         response = move(payload);
         break;
      }

      case BridgeProto::CoinSelection::kProcessCustomUtxoList:
      {
         const auto& processMsg = msg.process_custom_utxo_list();

         vector<UTXO> utxos;
         utxos.reserve(processMsg.utxos_size());
         for (int i=0; i<processMsg.utxos_size(); i++)
         {
            const auto& utxoProto = processMsg.utxos(i);
            const auto& hash = BinaryDataRef::fromString(utxoProto.tx_hash());
            const auto& script = BinaryDataRef::fromString(utxoProto.script());
            UTXO utxo(utxoProto.value(), 
               utxoProto.tx_height(), utxoProto.tx_index(), utxoProto.txout_index(),
               hash, script);

            utxos.emplace_back(utxo);
         }

         uint64_t flatFee = 0;
         float feeByte = 0;
         switch (processMsg.fee_case())
         {
            case BridgeProto::CoinSelection::ProcessCustomUtxoList::kFlatFee:
               flatFee = processMsg.flat_fee();
               break;

            case BridgeProto::CoinSelection::ProcessCustomUtxoList::kFeeByte:
               feeByte = processMsg.fee_byte();
               break;
         }

         bool success = true;
         try
         {
            cs->processCustomUtxoList(utxos, flatFee, feeByte, processMsg.flags());
         }
         catch (exception&)
         {
            success = false;
         }

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(success);
         response = move(payload);
         break;
      }

      case BridgeProto::CoinSelection::kGetFeeForMaxVal:
      {
         const auto& getFeeMsg = msg.get_fee_for_max_val();
         auto feeByte = getFeeMsg.fee_byte();

         float flatFee = 0;
         if (getFeeMsg.utxos_size() == 0)
         {
            flatFee = cs->getFeeForMaxVal(feeByte);
         }
         else
         {
            vector<BinaryData> serUtxos;
            serUtxos.reserve(getFeeMsg.utxos_size());
            for (int i=0; i<getFeeMsg.utxos_size(); i++)
            {
               const auto& utxoProto = getFeeMsg.utxos(i);
               const auto& hash = BinaryDataRef::fromString(utxoProto.tx_hash());
               const auto& script = BinaryDataRef::fromString(utxoProto.script());
               UTXO utxo(utxoProto.value(), utxoProto.tx_height(),
                  utxoProto.tx_index(), utxoProto.txout_index(),
                  hash, script);

               serUtxos.emplace_back(utxo.serialize());
            }

            flatFee = cs->getFeeForMaxValUtxoVector(serUtxos, feeByte);
         }

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);

         reply->mutable_coin_selection()->set_flat_fee(flatFee);
         response = move(payload);
         break;
      }
   }

   if (response != nullptr)
   {
      //write response to socket
      response->mutable_reply()->set_reference_id(referenceId);
      bridge->writeToClient(move(response));
   }
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool ProtobufCommandParser::processSignerCommands(CppBridge* bridge,
   unsigned referenceId, const BridgeProto::Signer& msg)
{
   auto signer = bridge->signerInstance(msg.id());
   if (signer == nullptr)
   {
      if (msg.id().empty())
      {
         auto response = bridge->initNewSigner();
         response->mutable_reply()->set_reference_id(referenceId);

         bridge->writeToClient(move(response));
         return true;
      }

      auto payload = make_unique<BridgeProto::Payload>();
      auto reply = payload->mutable_reply();
      reply->set_success(false);
      reply->set_reference_id(referenceId);

      bridge->writeToClient(move(payload));
      return true;
   }

   BridgePayload response;
   switch (msg.method_case())
   {
      case BridgeProto::Signer::kGetNew:
      {
         throw runtime_error("invalid get_new request");
      }

      case BridgeProto::Signer::kCleanup:
      {
         bridge->destroySigner(msg.id());
         break;
      }

      case BridgeProto::Signer::kSetVersion:
      {
         signer->signer_.setVersion(msg.set_version().version());

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);
         response = move(payload);
         break;
      }

      case BridgeProto::Signer::kSetLockTime:
      {
         signer->signer_.setLockTime(msg.set_lock_time().lock_time());

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);
         response = move(payload);
         break;
      }

      case BridgeProto::Signer::kAddSpenderByOutpoint:
      {
         const auto& addSpenderMsg = msg.add_spender_by_outpoint();
         const auto& hash = BinaryData::fromString(addSpenderMsg.hash());
         signer->signer_.addSpender_ByOutpoint(hash,
            addSpenderMsg.tx_out_id(), addSpenderMsg.sequence());

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);
         response = move(payload);
         break;
      }

      case BridgeProto::Signer::kPopulateUtxo:
      {
         const auto& populateUtxoMsg = msg.populate_utxo();
         const auto& hash = BinaryData::fromString(populateUtxoMsg.hash());
         const auto& script = BinaryData::fromString(populateUtxoMsg.script());
         UTXO utxo(populateUtxoMsg.value(), UINT32_MAX, UINT32_MAX,
            populateUtxoMsg.tx_out_id(), hash, script);

         signer->signer_.populateUtxo(utxo);
         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);
         response = move(payload);
         break;
      }

      case BridgeProto::Signer::kAddSupportingTx:
      {
         BinaryDataRef rawTxData; rawTxData.setRef(
            msg.add_supporting_tx().raw_tx());
         signer->signer_.addSupportingTx(rawTxData);

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);
         response = move(payload);
         break;
      }

      case BridgeProto::Signer::kAddRecipient:
      {
         const auto& addMsg = msg.add_recipient();
         const auto& script = BinaryDataRef::fromString(addMsg.script());
         const auto hash = BtcUtils::getTxOutScrAddr(script);
         signer->signer_.addRecipient(
            Armory::CoinSelection::CoinSelectionInstance::createRecipient(
               hash, addMsg.value()));

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);
         response = move(payload);
         break;
      }

      case BridgeProto::Signer::kToTxSigCollect:
      {
         auto txSigCollect = signer->signer_.toString(
            static_cast<Signer::SignerStringFormat>(
               msg.to_tx_sig_collect().ustx_type()));

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);
         reply->mutable_signer()->set_tx_sig_collect(txSigCollect);
         response = move(payload);
         break;
      }

      case BridgeProto::Signer::kFromTxSigCollect:
      {
         signer->signer_ = Armory::Signer::Signer::fromString(
            msg.from_tx_sig_collect().tx_sig_collect());

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);
         response = move(payload);
         break;
      }

      case BridgeProto::Signer::kSignTx:
      {
         signer->signTx(msg.sign_tx().wallet_id(),
            msg.sign_tx().callback_id(), referenceId);
         break;
      }

      case BridgeProto::Signer::kGetSignedTx:
      {
         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         try
         {
            auto data = signer->signer_.serializeSignedTx();
            reply->mutable_signer()->set_tx_data(
               data.toCharPtr(), data.getSize());
            reply->set_success(true);
         }
         catch (const exception&)
         {
            reply->set_success(false);
         }

         response = move(payload);
         break;
      }

      case BridgeProto::Signer::kGetUnsignedTx:
      {
         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         try
         {
            auto data = signer->signer_.serializeUnsignedTx();
            reply->mutable_signer()->set_tx_data(
               data.toCharPtr(), data.getSize());
            reply->set_success(true);
         }
         catch (const exception&)
         {
            reply->set_success(false);
         }

         response = move(payload);
         break;
      }

      case BridgeProto::Signer::kResolve:
      {
         auto result = signer->resolve(msg.resolve().wallet_id());

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(result);
         response = move(payload);
         break;
      }

      case BridgeProto::Signer::kGetSignedStateForInput:
      {
         response = signer->getSignedStateForInput(
            msg.get_signed_state_for_input().input_id());
         break;
      }

      case BridgeProto::Signer::kFromType:
      {
         auto result = signer->signer_.deserializedFromType();

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);

         reply->mutable_signer()->set_from_type((int)result);
         response = move(payload);
         break;
      }

      case BridgeProto::Signer::kCanLegacySerialize:
      {
         auto result = signer->signer_.canLegacySerialize();
         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(result);
         response = move(payload);
         break;
      }
   }

   if (response != nullptr)
   {
      //write response to socket
      response->mutable_reply()->set_reference_id(referenceId);
      bridge->writeToClient(move(response));
   }
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool ProtobufCommandParser::processUtilsCommands(CppBridge* bridge,
   unsigned referenceId, const BridgeProto::Utils& msg)
{
   BridgePayload response;
   switch (msg.method_case())
   {
      case BridgeProto::Utils::kCreateWallet:
      {
         auto wltId = bridge->createWallet(msg.create_wallet());
         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);

         reply->mutable_utils()->set_wallet_id(wltId);
         response = move(payload);
         break;
      }

      case BridgeProto::Utils::kGenerateRandomHex:
      {
         auto size = msg.generate_random_hex().length();
         auto str = bridge->fortuna_.generateRandom(size).toHexStr();

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);

         reply->mutable_utils()->set_random_hex(str);
         response = move(payload);
         break;
      }

      case BridgeProto::Utils::kGetHash160:
      {
         const auto& getHashMsg = msg.get_hash_160();
         const auto& data = BinaryDataRef::fromString(getHashMsg.data());
         response = bridge->getHash160(data);
         break;
      }

      case BridgeProto::Utils::kGetScraddrForAddrstr:
      {
         response = bridge->getScrAddrForAddrStr(
            msg.get_scraddr_for_addrstr().address());
         break;
      }

      case BridgeProto::Utils::kGetNameForAddrType:
      {
         auto addrTypeInt = msg.get_name_for_addr_type().address_type();
         auto typeName = bridge->getNameForAddrType(addrTypeInt);

         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(true);

         reply->mutable_utils()->set_address_type_name(typeName);
         response = move(payload);
         break;
      }
   }

   if (response != nullptr)
   {
      //write response to socket
      response->mutable_reply()->set_reference_id(referenceId);
      bridge->writeToClient(move(response));
   }
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool ProtobufCommandParser::processScriptUtilsCommands(CppBridge* bridge,
   unsigned referenceId, const BridgeProto::ScriptUtils& msg)
{
   BridgePayload response;
   const auto& script = BinaryDataRef::fromString(msg.script());

   switch (msg.method_case())
   {
      case BridgeProto::ScriptUtils::kGetTxinScriptType:
      {
         const auto& getTxInMsg = msg.get_txin_script_type();
         const auto& hash = BinaryDataRef::fromString(getTxInMsg.hash());

         response = bridge->getTxInScriptType(script, hash);
         break;
      }

      case BridgeProto::ScriptUtils::kGetTxoutScriptType:
      {
         response = bridge->getTxOutScriptType(script);
         break;
      }

      case BridgeProto::ScriptUtils::kGetScraddrForScript:
      {
         response = bridge->getScrAddrForScript(script);
         break;
      }

      case BridgeProto::ScriptUtils::kGetLastPushDataInScript:
      {
         response = bridge->getLastPushDataInScript(script);
         break;
      }

      case BridgeProto::ScriptUtils::kGetTxoutScriptForScraddr:
      {
         response = bridge->getTxOutScriptForScrAddr(script);
         break;
      }

      case BridgeProto::ScriptUtils::kGetAddrstrForScraddr:
      {
         response = bridge->getAddrStrForScrAddr(script);
         break;
      }
   }

   if (response != nullptr)
   {
      //write response to socket
      response->mutable_reply()->set_reference_id(referenceId);
      bridge->writeToClient(move(response));
   }
   return true;
}

////////////////////////////////////////////////////////////////////////////////
bool ProtobufCommandParser::processCallbackReply(CppBridge* bridge,
   const BridgeProto::CallbackReply& msg)
{
   try
   {
      auto handler = bridge->getCallbackHandler(msg.reference_id());
      return handler(msg);
   }
   catch (const std::runtime_error&)
   {
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
bool ProtobufCommandParser::processData(
   CppBridge* bridge, BinaryDataRef socketData)
{
   BridgeProto::Request msg;
   if (!msg.ParseFromArray(socketData.getPtr() + 1, socketData.getSize() - 1))
   {
      LOGERR << "failed to parse protobuf msg";
      return false;
   }

   const auto& id = msg.reference_id();
   switch (msg.method_case())
   {
      case BridgeProto::Request::kService:
         return processBlockchainServiceCommands(bridge, id, msg.service());
      case BridgeProto::Request::kWallet:
         return processWalletCommands(bridge, id, msg.wallet());
      case BridgeProto::Request::kCoinSelection:
         return processCoinSelectionCommands(bridge, id, msg.coin_selection());
      case BridgeProto::Request::kSigner:
         return processSignerCommands(bridge, id, msg.signer());
      case BridgeProto::Request::kUtils:
         return processUtilsCommands(bridge, id, msg.utils());
      case BridgeProto::Request::kScriptUtils:
         return processScriptUtilsCommands(bridge, id, msg.script_utils());
      case BridgeProto::Request::kCallbackReply:
         return processCallbackReply(bridge, msg.callback_reply());

      case BridgeProto::Request::METHOD_NOT_SET:
      {
         auto payload = make_unique<BridgeProto::Payload>();
         auto reply = payload->mutable_reply();
         reply->set_success(false);
         reply->set_reference_id(id);
         bridge->writeToClient(move(payload));
         return false;
      }
   }

   return true;
}
