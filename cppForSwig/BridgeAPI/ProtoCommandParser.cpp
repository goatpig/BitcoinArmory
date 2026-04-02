////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2020-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "ProtoCommandParser.h"

#include <Utils/log.h>
#include <Utils/BtcUtils.h>

#include <Wallets/IOHeader.h>
#include <Wallets/WalletIdTypes.h>
#include <Wallets/Seeds/Backups.h>
#include <Wallets/Seeds/Seeds.h>
#include <Signer/Signer.h>
#include <Signer/CoinSelection.h>
#include <AsyncClient.h>

#include "CppBridge.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/Bridge.capnp.h"

using namespace Armory;
using namespace Armory::Bridge;
using namespace std::chrono_literals;

namespace
{
   using namespace Codec::Bridge;
   Cryptography::PRNG::Fortuna fortuna;

   // helpers //
   BinaryData serializeCapnp(capnp::MallocMessageBuilder& msg)
   {
      auto flat = capnp::messageToFlatArray(msg);
      auto bytes = flat.asBytes();
      return BinaryData(bytes.begin(), bytes.end());
   }

   void utxosToCapnp(const std::vector<::UTXO>& utxos,
      capnp::List<Codec::Bridge::UTXO, capnp::Kind::STRUCT>::Builder& capnOutputs)
   {
      for (unsigned i=0; i<utxos.size(); i++) {
         auto capnUtxo = capnOutputs[i];
         const auto& utxo = utxos[i];

         //scrAddr
         const auto& script = utxo.getScript();
         auto scrAddr = BtcUtils::getTxOutScrAddr(script);
         capnUtxo.setScrAddr(capnp::Data::Builder(
            (uint8_t*)scrAddr.getPtr(), scrAddr.getSize()
         ));

         //output body
         auto capnOutput = capnUtxo.initOutput();
         capnOutput.setValue(utxo.getValue());
         capnOutput.setTxHeight(utxo.getHeight());
         capnOutput.setTxIndex(utxo.getTxIndex());
         capnOutput.setTxOutIndex(utxo.getTxOutIndex());

         const auto& txHash = utxo.getTxHash();
         capnOutput.setTxHash(capnp::Data::Builder(
            (uint8_t*)txHash.getPtr(), txHash.getSize()
         ));

         capnOutput.setScript(capnp::Data::Builder(
            (uint8_t*)script.getPtr(), script.getSize()
         ));
      }
   }

   // switchers //
   bool processDbSetupCommands(
      std::shared_ptr<CppBridge> bridge, MessageId referenceId,
      Codec::Bridge::DbSetupRequest::Reader& request)
   {
      BinaryData response;
      switch (request.which())
      {
         case DbSetupRequest::AUTOMATE_DB:
         {
            auto autoDbReq = request.getAutomateDb();
            std::filesystem::path satoshiPath{std::string(autoDbReq.getSatoshiPath())};
            std::filesystem::path dbDir{std::string(autoDbReq.getDbDir())};

            std::thread thr([bridge, satoshiPath, dbDir, referenceId]{
               bridge->automateDb(satoshiPath, dbDir, referenceId);});
            if (thr.joinable()) {
               thr.detach();
            }
            return true;
         }

         case DbSetupRequest::CONNECT_TO_IP:
         {
            auto connectReq = request.getConnectToIp();
            std::string ip = connectReq.getIp();
            std::string port = connectReq.getPort();
            std::string callbackId = connectReq.getCallbackId();

            std::thread thr([bridge, ip, port, callbackId, referenceId]{
               bridge->connectToIp(ip, port, callbackId, referenceId);});
            if (thr.joinable()) {
               thr.detach();
            }
            return true;
         }

         case DbSetupRequest::CONNECT_TO_PEER:
         {
            std::string key = request.getConnectToPeer();

            std::thread thr([bridge, key=std::move(key), referenceId]{
               bridge->connectToPeer(key, referenceId);});
            if (thr.joinable()) {
               thr.detach();
            }
            return true;
         }

         case DbSetupRequest::CLEANUP_DB:
         {
            std::thread thr([bridge, referenceId]{
               bridge->cleanupDb(referenceId);});
            if (thr.joinable()) {
               thr.detach();
            }
            return true;
         }

         case DbSetupRequest::GO_ONLINE:
         {
            bridge->goOnline();
            break;
         }

         case DbSetupRequest::DISCONNECT:
         {
            bridge->disconnect();
            break;
         }

         case DbSetupRequest::SHUTDOWN:
         {
            bridge->reset();
            return false;
         }

         case DbSetupRequest::LOAD_PEERS_DB:
         {
            std::string callbackId(request.getLoadPeersDb());
            std::thread thr([bridge, callbackId, referenceId]() {
               bridge->loadPeersDb(callbackId, referenceId);
            });
            if (thr.joinable()) {
               thr.detach();
            }
            return true;
         }

         case DbSetupRequest::LIST_PEERS:
         {
            bridge->listPeers(referenceId);
            return true;
         }

         case DbSetupRequest::ADD_PEER:
         {
            auto peerReq = request.getAddPeer();
            std::vector<std::string> names;
            for (auto capnName : peerReq.getNames()) {
               names.emplace_back(std::string(capnName));
            }
            bridge->addPeer(peerReq.getKey(),
               names, peerReq.getLabel(), referenceId);
            return true;
         }

         case DbSetupRequest::REMOVE_PEER:
         {
            auto peer = std::string(request.getRemovePeer());
            bridge->removePeer(peer, referenceId);
            return true;
         }

         case DbSetupRequest::SET_LABEL:
         {
            auto labelReq = request.getSetLabel();
            bridge->setPeerLabel(
               labelReq.getKey(), labelReq.getLabel(), referenceId);
            return true;
         }
      }

      if (!response.empty()) {
         //write response to socket
         bridge->writeToClient(response);
      }

      return true;
   }

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

         case BlockchainServiceRequest::REGISTER_WALLETS:
         {
            bridge->registerWallets();
            break;
         }

         case BlockchainServiceRequest::REGISTER_WALLET:
         {
            auto regWallet = request.getRegisterWallet();
            auto capnId = regWallet.getWalletId();
            Wallets::WalletId wltId{capnId.begin(), capnId.size()};
            auto accIdCapn = regWallet.getAccountId();
            auto accId = Wallets::AddressAccountId::fromHex(accIdCapn);
            bridge->registerWallet(wltId, accId, regWallet.getIsNew());
            break;
         }

         case BlockchainServiceRequest::GET_LEDGER_DELEGATE_ID:
         {
            const auto& delegateId = bridge->getLedgerDelegateId();
            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            auto service = reply.initService();

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
            std::set<unsigned> heights;
            for (const auto& height : capnHeights) {
               heights.emplace(height);
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
            auto broadcastObj = request.getBroadcastTx();

            auto capnTxs = broadcastObj.getRawTxs();
            std::vector<BinaryData> rawTxs;
            rawTxs.reserve(capnTxs.size());
            for (auto capnTx : capnTxs) {
               rawTxs.emplace_back(
                  BinaryData{capnTx.begin(), capnTx.end()});
            }
            bool isRPC = broadcastObj.which() ==
               BlockchainServiceRequest::BroadcastRequest::VIA_RPC ?
               true : false;

            bridge->broadcastTxs(rawTxs, isRPC);
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

   bool processWalletManagerCommands(
      std::shared_ptr<CppBridge> bridge, MessageId referenceId,
      WalletManagerRequest::Reader& request)
   {
      BinaryData response;
      switch (request.which())
      {
         case WalletManagerRequest::LIST_WALLETS:
         {
            response = bridge->listWallets(referenceId);
            break;
         }

         case WalletManagerRequest::UNLOCK_CONTROL_HEADER:
         {
            auto unlockReq = request.getUnlockControlHeader();
            std::string path = unlockReq.getWalletPath();
            std::string callbackId = unlockReq.getCallbackId();
            bridge->unlockControlHeader(path, callbackId, referenceId);
            break;
         }

         case WalletManagerRequest::STAGE_WALLET:
         {
            auto stageRequest = request.getStageWallet();
            auto capnId = stageRequest.getWalletId();
            const Wallets::WalletId wltId{capnId.begin(), capnId.size()};
            auto success = bridge->stageWallet(wltId, stageRequest.getStage());

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            reply.setSuccess(success);
            reply.setReferenceId(referenceId);

            response = serializeCapnp(message);
            break;
         }

         case WalletManagerRequest::LOAD_WALLETS:
         {
            response = bridge->loadWallets(referenceId);
            break;
         }

         case WalletManagerRequest::MIGRATE_WALLET:
         {
            auto migrateReq = request.getMigrateWallet();
            const std::filesystem::path walletPath(std::string{migrateReq.getWalletPath()});
            const std::string callbackId(migrateReq.getCallbackId());
            bridge->migrateWallet(walletPath, callbackId, referenceId);
            break;
         }

         case WalletManagerRequest::UNLOAD_WALLET:
         {
            auto capnId = request.getUnloadWallet();
            const Wallets::WalletId wltId{capnId.begin(), capnId.size()};
            auto success = bridge->unloadWallet(wltId);

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            reply.setSuccess(success);
            reply.setReferenceId(referenceId);

            response = serializeCapnp(message);
            break;
         }

         case WalletManagerRequest::DELETE_WALLET:
         {
            auto capnId = request.getDeleteWallet();
            const Wallets::WalletId wltId{capnId.begin(), capnId.size()};
            auto success = bridge->deleteWallet(wltId);

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            reply.setSuccess(success);
            reply.setReferenceId(referenceId);

            response = serializeCapnp(message);
            break;
         }

         default:
            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            auto walletReply = reply.initWalletManager();
            reply.setSuccess(false);
            reply.setReferenceId(referenceId);
            reply.setError("invalid WalletManager request");
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
      auto capnId = request.getWalletId();
      const Wallets::WalletId walletId{capnId.begin(), capnId.size()};
      Wallets::AddressAccountId accountId;
      try {
         accountId = Wallets::AddressAccountId::fromHex(
            request.getAccountId());
      } catch (const Wallets::IdException&) {
         //nothing to do, accountId wont be set
      }

      BinaryData response;
      auto checkOnline = [&response, &referenceId, bridge](void)->bool
      {
         if (bridge->isOffline()) {
            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            reply.setSuccess(false);
            reply.setReferenceId(referenceId);
            reply.setError("Armory is offline");
            response = serializeCapnp(message);
            return false;
         }
         return true;
      };

      switch (request.which())
      {
         case WalletRequest::CREATE_BACKUP_STRING:
         {
            std::string callbackId;
            bool isPriv = false;
            auto backupStruct = request.getCreateBackupString();
            if (backupStruct.which() == WalletRequest::BackupStringStruct::PRIVATE) {
               callbackId = backupStruct.getPrivate();
               isPriv = true;
            }

            bridge->createBackupStringForWallet(walletId, isPriv,
               callbackId, referenceId);
            break;
         }

         case WalletRequest::CHANGE_PASSPHRASE:
         {
            auto passStruct = request.getChangePassphrase();
            std::string callbackId = passStruct.getCallbackId();
            bool isPriv =
               passStruct.which() == WalletRequest::ChangePassphraseRequest::PRIVATE ?
               true : false;

            bridge->changeWalletPassphrase(walletId, callbackId, isPriv, referenceId);
            break;
         }

         case WalletRequest::GET_LEDGER_DELEGATE_ID:
         {
            if (!checkOnline()) {
               break;
            }

            const auto& delegateId = bridge->getLedgerDelegateIdForWallet(
               walletId, accountId);

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
            if (!checkOnline()) {
               break;
            }

            auto capnAddr = request.getGetLedgerDelegateIdForScrAddr();
            BinaryDataRef addr(capnAddr.begin(), capnAddr.end());
            const auto& delegateId = bridge->getLedgerDelegateIdForScrAddr(
               walletId, accountId, addr);

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
            if (!checkOnline()) {
               break;
            }

            response = bridge->getBalanceAndCount(
               walletId, accountId, referenceId);
            break;
         }

         case WalletRequest::SETUP_NEW_COIN_SELECTION_INSTANCE:
         {
            bridge->setupNewCoinSelectionInstance(walletId, accountId,
               request.getSetupNewCoinSelectionInstance(), referenceId);
            break;
         }

         case WalletRequest::GET_ADDR_COMBINED_LIST:
         {
            if (!checkOnline()) {
               break;
            }

            response = bridge->getAddrCombinedList(
               walletId, accountId, referenceId);
            break;
         }

         case WalletRequest::EXTEND_ADDRESS_POOL:
         {
            auto args = request.getExtendAddressPool();
            bridge->extendAddressPool(walletId, accountId,
               args.getCount(), args.getIsNew(), args.getCallbackId(),
               referenceId);
            break;
         }

         case WalletRequest::GET_ACCOUNT_IDS:
         {
            response = bridge->getAccountIds(
               walletId, referenceId);
            break;
         }

         case WalletRequest::GET_DATA:
         {
            response = bridge->getWalletPacket(
               walletId, accountId, referenceId);
            break;
         }

         case WalletRequest::SET_ADDRESS_TYPE_FOR:
         {
            auto args = request.getSetAddressTypeFor();
            auto capnAssetId = args.getAssetId();
            BinaryDataRef assetId(capnAssetId.begin(), capnAssetId.end());

            response = bridge->setAddressTypeFor(walletId, accountId,
               assetId, args.getAddressType(), referenceId);
            break;
         }

         case WalletRequest::CREATE_ADDRESS_BOOK:
         {
            if (!checkOnline()) {
               break;
            }

            bridge->createAddressBook(walletId, accountId, referenceId);
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
            if (!checkOnline()) {
               break;
            }

            auto args = request.getGetUtxos();
            uint64_t value = 0;
            if (args.isValue()) {
               value = args.getValue();
            }
            bridge->getUTXOs(walletId, accountId,
               value, args.isZc(), args.isRbf(),
               referenceId);
            break;
         }

         case WalletRequest::GET_ADDRESS:
         {
            auto args = request.getGetAddress();
            bridge->getAddress(walletId, accountId,
               args.getType(), args.which(), referenceId);
            break;
         }

         case WalletRequest::GET_UNLOCK_TIME:
         {
            bridge->getUnlockTime(walletId, referenceId);
            break;
         }

         case WalletRequest::FORK_WATCHING_ONLY:
         {
            std::string callbackId{request.getForkWatchingOnly()};
            bridge->forkWatchingOnly(walletId, callbackId, referenceId);
            break;
         }

         default:
            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            reply.setSuccess(false);
            reply.setReferenceId(referenceId);
            reply.setError("unknown wallet method");
            response = serializeCapnp(message);

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
         auto reply = fromBridge.initReply();
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

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            reply.setSuccess(true);
            reply.setReferenceId(referenceId);
            response = serializeCapnp(message);
            break;
         }

         case CoinSelectionRequest::RESET:
         {
            cs->resetRecipients();

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            reply.setSuccess(true);
            reply.setReferenceId(referenceId);
            response = serializeCapnp(message);
            break;
         }

         case CoinSelectionRequest::SET_RECIPIENT:
         {
            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
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

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            reply.setReferenceId(referenceId);
            try {
               cs->selectUTXOs(flatFee, feeByte, args.getFlags());
               reply.setSuccess(true);
            } catch (const std::exception& e) {
               reply.setSuccess(true);
               reply.setError(e.what());
            }
            response = serializeCapnp(message);
            break;
         }

         case CoinSelectionRequest::GET_UTXO_SELECTION:
         {
            auto utxoVec = cs->getUtxoSelection();

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
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
            auto reply = fromBridge.initReply();
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
            auto reply = fromBridge.initReply();
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
            auto reply = fromBridge.initReply();
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
            std::vector<::UTXO> utxos;
            utxos.reserve(capnUtxos.size());
            for (auto capnUtxo : capnUtxos) {
               auto capnHash = capnUtxo.getTxHash();
               auto hash = BinaryDataRef(capnHash.begin(), capnHash.end());

               auto capnScript = capnUtxo.getScript();
               auto script = BinaryDataRef(capnScript.begin(), capnScript.end());

               utxos.emplace_back(::UTXO{ capnUtxo.getValue(),
                  capnUtxo.getTxHeight(),
                  capnUtxo.getTxIndex(), capnUtxo.getTxOutIndex(),
                  hash, script
               });
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
            auto reply = fromBridge.initReply();
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

                  ::UTXO utxo(capnUtxo.getValue(), capnUtxo.getTxHeight(),
                     capnUtxo.getTxIndex(), capnUtxo.getTxOutIndex(),
                     hash, script);
                  serUtxos.emplace_back(utxo.serialize());
               }

               flatFee = cs->getFeeForMaxValUtxoVector(
                  serUtxos, args.getFeeByte());
            }

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
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
         auto reply = fromBridge.initReply();
         reply.setSuccess(success);
         reply.setError("generic signer error message");
         reply.setReferenceId(referenceId);

         //serialize & push reply
         auto serialized = serializeCapnp(message);
         bridge->writeToClient(serialized);
      };

      auto signerId = request.getId();
      auto signer = bridge->signerInstance(signerId);
      if (signer == nullptr && request.which() != SignerRequest::GET_NEW) {
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
            replySuccess(true);
            break;
         }

         case SignerRequest::SET_VERSION:
         {
            signer->signer->setVersion(request.getSetVersion());
            replySuccess(true);
            break;
         }

         case SignerRequest::SET_LOCK_TIME:
         {
            signer->signer->setLockTime(request.getSetLockTime());
            replySuccess(true);
            break;
         }

         case SignerRequest::ADD_SPENDER_BY_OUTPOINT:
         {
            auto args = request.getAddSpenderByOutpoint();
            auto capnHash = args.getHash();
            BinaryDataRef hashRef(capnHash.begin(), capnHash.end());
            signer->signer->addSpender_ByOutpoint(hashRef,
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

            ::UTXO utxo(args.getValue(), UINT32_MAX, UINT32_MAX,
               args.getTxOutId(), hashRef, scriptRef);
            signer->signer->populateUtxo(utxo);

            replySuccess(true);
            break;
         }

         case SignerRequest::ADD_SUPPORTING_TX:
         {
            auto capnTx = request.getAddSupportingTx();
            BinaryDataRef txRef(capnTx.begin(), capnTx.end());
            signer->signer->addSupportingTx(txRef);

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

            signer->signer->addRecipient(
               CoinSelection::CoinSelectionInstance::createRecipient(
                  hash, args.getValue()));

            replySuccess(true);
            break;
         }

         case SignerRequest::TO_TX_SIG_COLLECT:
         {
            auto txSigCollect = signer->signer->toString(
               static_cast<Signing::SignerStringFormat>(
                  request.getToTxSigCollect()));

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            reply.setSuccess(true);
            reply.setReferenceId(referenceId);

            auto signerReply = reply.initSigner();
            signerReply.setToTxSigCollect(txSigCollect);

            response = serializeCapnp(message);
            break;
         }

         case SignerRequest::FROM_TX_SIG_COLLECT:
         {
            auto signerObj = Signing::Signer::fromString(
               request.getFromTxSigCollect());
            signer->signer = std::make_unique<Signing::Signer>(std::move(signerObj));

            replySuccess(true);
            break;
         }

         case SignerRequest::SIGN_TX:
         {
            auto args = request.getSignTx();
            auto capnId = args.getWalletId();
            const Wallets::WalletId wltId{capnId.begin(), capnId.size()};
            signer->signTx(wltId, args.getCallbackId(),
               referenceId);
            break;
         }

         case SignerRequest::GET_SIGNED_TX:
         {
            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            reply.setReferenceId(referenceId);

            try {
               auto signedTx = signer->signer->serializeSignedTx();
               reply.setSuccess(true);

               auto signerReply = reply.initSigner();
               signerReply.setGetSignedTx(capnp::Data::Builder(
                  (uint8_t*)signedTx.getPtr(), signedTx.getSize()
               ));
            } catch (const std::exception& e) {
               reply.setError(e.what());
               reply.setSuccess(false);
            }

            response = serializeCapnp(message);
            break;
         }

         case SignerRequest::GET_UNSIGNED_TX:
         {
            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            reply.setReferenceId(referenceId);

            try {
               auto signedTx = signer->signer->serializeUnsignedTx();
               reply.setSuccess(true);

               auto signerReply = reply.initSigner();
               signerReply.setGetUnsignedTx(capnp::Data::Builder(
                  (uint8_t*)signedTx.getPtr(), signedTx.getSize()
               ));
            } catch (const std::exception& e) {
               reply.setError(e.what());
               reply.setSuccess(false);
            }

            response = serializeCapnp(message);
            break;
         }

         case SignerRequest::RESOLVE:
         {
            auto capnId = request.getResolve();
            const Wallets::WalletId wltId{capnId.begin(), capnId.size()};
            auto result = signer->resolve(wltId);
            replySuccess(result);
            break;
         }

         case SignerRequest::GET_SIGNED_STATE_FOR_INPUT:
         {
            response = signer->getSignedStateForInput(
               request.getGetSignedStateForInput(),
               referenceId);
            break;
         }

         case SignerRequest::FROM_TYPE:
         {
            auto result = signer->signer->deserializedFromType();

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
            reply.setReferenceId(referenceId);
            reply.setSuccess(true);

            auto signerReply = reply.initSigner();
            signerReply.setFromType((uint32_t)result);

            response = serializeCapnp(message);
            break;
         }

         case SignerRequest::CAN_LEGACY_SERIALIZE:
         {
            auto result = signer->signer->canLegacySerialize();
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
            std::string callbackId = args.getCallbackId();

            auto capnEntropy = args.getExtraEntropy();
            SecureBinaryData sbdEntropy{
               (uint8_t*)capnEntropy.begin(),
               (uint8_t*)capnEntropy.end()
            };

            auto seedType = Seeds::SeedType::Raw;
            switch (args.getWalletType())
            {
               case UtilsRequest::WalletType::LEGACY:
                  seedType = Seeds::SeedType::ArmoryLegacy;
                  break;

               case UtilsRequest::WalletType::STRUCTURED_BIP32:
                  seedType = Seeds::SeedType::BIP32_Structured;
                  break;

               case UtilsRequest::WalletType::RAW_BIP32:
                  seedType = Seeds::SeedType::BIP32_Virgin;
                  break;
            }

            bridge->createWallet(
               seedType, std::move(sbdEntropy),
               Wallets::IO::CreateWalletParams{
                  bridge->getDataDir(),
                  Passphrase::SetNew{}, Passphrase::SetNew{},
                  nullptr,
                  args.getLookup(),
                  args.getLabel(), args.getDescription()},
               callbackId, referenceId
            );
            break;
         }

         case UtilsRequest::RESTORE_WALLET:
         {
            auto walletRequest = request.getRestoreWallet();
            std::vector<std::string_view> lines;
            lines.reserve(5);

            if (walletRequest.hasBackupId()) {
               auto backupId = walletRequest.getBackupId();
               lines.emplace_back(std::string_view{
                  backupId.begin(), backupId.size()});
            }

            if (walletRequest.hasRoot()) {
               for (const auto& root : walletRequest.getRoot()) {
                  lines.emplace_back(std::string_view{
                     root.begin(), root.size()});
               }
            }

            if (walletRequest.hasChaincode()) {
               for (const auto& chaincode : walletRequest.getChaincode()) {
                  lines.emplace_back(std::string_view{
                     chaincode.begin(), chaincode.size()});
               }
            }

            auto spPassCapnp = walletRequest.getSpPass();
            std::string_view spPass{spPassCapnp.begin(), spPassCapnp.size()};

            auto cbIdCapnp = walletRequest.getCallbackId();
            std::string callbackId{cbIdCapnp.begin(), cbIdCapnp.size()};

            bridge->restoreWallet(lines, spPass,
               callbackId, referenceId);
            break;
         }

         case UtilsRequest::IMPORT_WALLET:
         {
            std::filesystem::path importPath(std::string{request.getImportWallet()});
            bridge->importWallet(importPath, referenceId);
            break;
         }

         case UtilsRequest::GENERATE_RANDOM_HEX:
         {
            auto str = fortuna.generateRandom(
               request.getGenerateRandomHex()).toHexStr();

            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<FromBridge>();
            auto reply = fromBridge.initReply();
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
            auto reply = fromBridge.initReply();
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

   bool processDelegateCommands(
      std::shared_ptr<CppBridge> bridge, MessageId referenceId,
      LedgerDelegateRequest::Reader& request)
   {
      std::string delegateId = request.getId();
      switch (request.which())
      {
         case LedgerDelegateRequest::GET_PAGES:
         {
            auto getPages = request.getGetPages();
            bridge->getHistoryPageForDelegate(delegateId,
               getPages.getFirst(), getPages.getLast(),
               referenceId);
            break;
         }

         case LedgerDelegateRequest::GET_PAGE_COUNT:
         {
            bridge->getPageCountForDelegate(delegateId, referenceId);
            break;
         }
      }
      return true;
   }

   bool processNotificationReply(
      std::shared_ptr<CppBridge> bridge,
      NotificationReply::Reader& notif)
   {
      try {
         auto handler = bridge->getCallbackHandler(notif.getCounter());
         switch (notif.which())
         {
            case NotificationReply::SET_PASSPHRASE:
            {
               auto capnpPassStruct = notif.getSetPassphrase();
               auto pass = SecureBinaryData::fromString(
                  capnpPassStruct.getPassphrase());

               if (notif.getSuccess() == false) {
                  return handler(Seeds::PromptReply{
                     false, false, Passphrase::Params{}
                  });
               }

               switch (capnpPassStruct.which())
               {
                  case NotificationReply::SetPassphraseReply::KDF_TARGET_MS:
                  {
                     auto kdfMs = std::chrono::milliseconds{
                        capnpPassStruct.getKdfTargetMs()};
                     return handler(Seeds::PromptReply{
                        true, false, Passphrase::Params{
                           kdfMs, 1, std::move(pass) }
                     });
                  }

                  case NotificationReply::SetPassphraseReply::KDF_TARGET_M_B:
                  {
                     uint32_t kdfMB = capnpPassStruct.getKdfTargetMB();
                     return handler(Seeds::PromptReply{
                        true, false, Passphrase::Params{
                           1ms, kdfMB, std::move(pass) }
                     });
                  }

                  case NotificationReply::SetPassphraseReply::REUSE_KDF:
                  {
                     return handler(Seeds::PromptReply{
                        true, false, Passphrase::Params{
                           std::move(pass), true }
                     });
                  }
               }
            }

            case NotificationReply::RESTORE:
            {
               return handler(Seeds::PromptReply{
                  notif.getSuccess(),
                  notif.getRestore() == NotificationReply::RestoreMode::MERGE ?
                     true : false
               });
            }

            case NotificationReply::UNLOCK_REQUEST:
            {
               auto pass = SecureBinaryData::fromString(notif.getUnlockRequest());
               return handler(Seeds::PromptReply{
                  notif.getSuccess(), false,
                  Passphrase::Params{std::move(pass)}
               });
            }

            case NotificationReply::PRESENT_PUBKEY:
            {
               return handler(Seeds::PromptReply{notif.getSuccess(), false, {}});
            }

            default:
               throw std::runtime_error("invalid NotificationReply which");
         }
         return false;
      } catch (const std::runtime_error& e) {
         LOGERR << "failed notif handling with error: " << e.what();
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
      case Codec::Bridge::ToBridge::SETUP:
      {
         auto setup = toBridge.getSetup();
         return processDbSetupCommands(
            bridge, referenceId, setup);
      }

      case Codec::Bridge::ToBridge::SERVICE:
      {
         auto service = toBridge.getService();
         return processBlockchainServiceCommands(
            bridge, referenceId, service);
      }

      case Codec::Bridge::ToBridge::WALLET_MANAGER:
      {
         auto mgr = toBridge.getWalletManager();
         return processWalletManagerCommands(
            bridge, referenceId, mgr);
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

      case Codec::Bridge::ToBridge::DELEGATE:
      {
         auto delegate = toBridge.getDelegate();
         return processDelegateCommands(
            bridge, referenceId, delegate);
      }

      case Codec::Bridge::ToBridge::NOTIFICATION:
      {
         auto notifReply = toBridge.getNotification();
         return processNotificationReply(
            bridge, notifReply);
      }

      default:
         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<FromBridge>();
         auto reply = fromBridge.initReply();
         reply.setReferenceId(referenceId);
         reply.setSuccess(false);
         reply.setError("invalid request");

         auto serialized = serializeCapnp(message);
         bridge->writeToClient(serialized);
         return false;
   }

   return true;
}
