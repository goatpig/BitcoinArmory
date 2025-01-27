////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2024, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "CppBridge.h"
#include "BridgeSocket.h"
#include "TerminalPassphrasePrompt.h"
#include "PassphrasePrompt.h"
#include "../Wallets/Seeds/Backups.h"
#include "../Signer/ResolverFeed_Wallets.h"
#include "../Wallets/WalletIdTypes.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/Bridge.capnp.h"
#include "capnp/Types.capnp.h"

using namespace Armory;
using namespace Armory::Codec::Bridge;
using namespace Armory::Codec::Types;
using namespace Armory::Bridge;

enum CppBridgeState
{
   CppBridge_Ready = 20,
   CppBridge_Registered
};

#define BRIDGE_CALLBACK_BDM         "bdm_callback"
#define BRIDGE_CALLBACK_PROGRESS    "progress"
#define DISCONNECTED_CALLBACK_ID    "disconnected"

#define PROTO_ASSETID_PREFIX 0xAFu

namespace
{
   BinaryData serializeCapnp(capnp::MallocMessageBuilder& msg)
   {
      auto flat = capnp::messageToFlatArray(msg);
      auto bytes = flat.asBytes();
      return BinaryData(bytes.begin(), bytes.end());
   }

   bool addressToCapnp(WalletData::AddressData::Builder& capnAddress,
      std::shared_ptr<AddressEntry> addrPtr,
      std::shared_ptr<Accounts::AddressAccount> addrAcc,
      const Wallets::EncryptionKeyId& keyId)
   {
      if (addrAcc == nullptr) {
         throw std::runtime_error("[addressToCapnp] null acc ptr");
      }

      const auto& assetID = addrPtr->getID();
      auto asset = addrAcc->getAssetForID(assetID);

      //address hash
      const auto& addrHash = addrPtr->getPrefixedHash();
      capnAddress.setPrefixedHash(capnp::Data::Builder(
         (uint8_t*)addrHash.getPtr(), addrHash.getSize()
      ));

      //type & pubkey
      BinaryDataRef pubKeyRef;
      std::shared_ptr<AddressEntry_WithAsset> addrWithAssetPtr = nullptr;
      uint32_t addrType = (uint32_t)addrPtr->getType();

      auto addrNested = std::dynamic_pointer_cast<AddressEntry_Nested>(addrPtr);
      if (addrNested != nullptr) {
         addrType |= (uint32_t)addrNested->getPredecessor()->getType();
         auto pred = addrNested->getPredecessor();
         pubKeyRef = pred->getPreimage().getRef();
         addrWithAssetPtr = std::dynamic_pointer_cast<AddressEntry_WithAsset>(pred);
      } else {
         pubKeyRef = addrPtr->getPreimage().getRef();
         addrWithAssetPtr = std::dynamic_pointer_cast<AddressEntry_WithAsset>(addrPtr);
      }

      capnAddress.setAddrType(addrType);
      capnAddress.setPublicKey(capnp::Data::Builder(
         (uint8_t*)pubKeyRef.getPtr(), pubKeyRef.getSize()
      ));

      //index
      capnAddress.setIndex(asset->getIndex());
      const auto& serAssetId = assetID.getSerializedKey(PROTO_ASSETID_PREFIX);
      capnAddress.setAssetId(capnp::Data::Builder(
         (uint8_t*)serAssetId.getPtr(), serAssetId.getSize()
      ));


      //address string, used flag, change flag
      capnAddress.setAddressString(addrPtr->getAddress());
      capnAddress.setIsUsed(addrAcc->isAssetInUse(addrPtr->getID()));
      capnAddress.setIsChange(addrAcc->isAssetChange(addrPtr->getID()));

      //priv key & encryption status
      bool isLocked = false;
      bool hasPrivKey = false;
      if (addrWithAssetPtr != nullptr) {
         auto theAsset = addrWithAssetPtr->getAsset();
         if (theAsset != nullptr) {
            if (theAsset->hasPrivateKey()) {
               hasPrivKey = true;
               try {
                  //the privkey is considered locked if it's encrypted by
                  //something else than the default encryption key, which
                  //lays in clear text in the wallet header
                  auto encryptionKeyId = theAsset->getPrivateEncryptionKeyId();
                  isLocked = (encryptionKeyId != keyId);
               } catch (const std::runtime_error&) {
                  //nothing to do, address has no encryption key
               }
            }
         }
      }
      capnAddress.setHasPrivKey(hasPrivKey);
      capnAddress.setUsesEncryption(isLocked);

      //precursor, if any
      if (addrNested == nullptr) {
         return isLocked;
      }

      const auto& precursor = addrNested->getPredecessor()->getScript();
      capnAddress.setPrecursorScript(capnp::Data::Builder(
         (uint8_t*)precursor.getPtr(), precursor.getSize()
      ));

      return isLocked;
   }

   void walletToCapnp(std::shared_ptr<Wallets::AssetWallet> wallet,
      const Wallets::AddressAccountId& accId,
      const std::map<BinaryData, std::string>& commentsMap,
      WalletData::Builder& capnWallet)
   {
      /* header */
      //wallet id (append the account id to it)
      capnWallet.setId({ wallet->getID() + ":" + accId.toHexStr() });

      //labels
      capnWallet.setLabel(wallet->getLabel());
      capnWallet.setDesc(wallet->getDescription());

      //does this wallet carry private keys?
      bool isWO = true;
      auto wltSingle = std::dynamic_pointer_cast<
         Wallets::AssetWallet_Single>(wallet);
      if (wltSingle != nullptr) {
         isWO = wltSingle->isWatchingOnly();
      }
      capnWallet.setWatchingOnly(isWO);

      /* addresses */
      auto accPtr = wltSingle->getAccountForID(accId);

      //address types
      const auto& addrTypes = accPtr->getAddressTypeSet();
      auto capnAddrTypes = capnWallet.initAddressTypes(addrTypes.size());
      unsigned i=0;
      for (const auto& addrType : addrTypes) {
         capnAddrTypes.set(i++, (uint32_t)addrType);
      }
      capnWallet.setDefaultAddressType(
         (uint32_t)accPtr->getDefaultAddressType());

      //address use count
      auto assetAccountPtr = accPtr->getOuterAccount();
      capnWallet.setLookupCount(assetAccountPtr->getLastComputedIndex());
      capnWallet.setUseCount(assetAccountPtr->getHighestUsedIndex());

      //address map
      auto addrMap = accPtr->getUsedAddressMap();
      bool useEncryption = true;
      auto capnAddresses = capnWallet.initAddressData(addrMap.size());
      i=0;
      for (const auto& addrPair : addrMap) {
         auto capnAddress = capnAddresses[i++];
         useEncryption &= addressToCapnp(capnAddress,
            addrPair.second, accPtr,
            wallet->getDefaultEncryptionKeyId());
      }

      /* encryption info */
      uint32_t kdfMem = 0;
      auto kdfPtr = wallet->getDefaultKdf();
      auto kdfRomix = std::dynamic_pointer_cast<
         Wallets::Encryption::KeyDerivationFunction_Romix>(kdfPtr);
      if (kdfRomix != nullptr) {
         kdfMem = kdfRomix->memTarget();
      }
      capnWallet.setUsesEncryption(useEncryption);
      capnWallet.setKdfMemReq(kdfMem);

      /* comments */
      auto capnComments = capnWallet.initComments(commentsMap.size());
      i=0;
      for (const auto& commentIt : commentsMap) {
         auto capnComment = capnComments[i++];
         capnComment.setKey(capnp::Data::Builder(
            (uint8_t*)commentIt.first.getPtr(), commentIt.first.getSize()
         ));
         capnComment.setVal(commentIt.second);
      }
   }

   void ledgerToCapnp(const DBClientClasses::LedgerEntry& ledger,
      Codec::Types::TxLedger::LedgerEntry::Builder& capnLedger)
   {
      capnLedger.setBalance(ledger.getValue());
      capnLedger.setTxHeight(ledger.getBlockHeight());
      capnLedger.setTxOutIndex(ledger.getTxOutIndex());
      capnLedger.setTxTime(ledger.getTxTime());
      capnLedger.setIsCoinbase(ledger.isCoinbase());
      capnLedger.setIsSTS(ledger.isSentToSelf());
      capnLedger.setIsOptInRBF(ledger.isOptInRBF());
      capnLedger.setIsChainedZC(ledger.isChainedZC());
      capnLedger.setIsWitness(ledger.isWitness());

      auto txHash = ledger.getTxHash();
      capnLedger.setTxHash(capnp::Data::Builder(
         (uint8_t*)txHash.getPtr(), txHash.getSize()
      ));

      capnLedger.setWalletId(ledger.getID());

      auto scrAddrList = ledger.getScrAddrList();
      auto capnAddrs = capnLedger.initScrAddrs(scrAddrList.size());
      unsigned i=0;
      for (const auto& scrAddr : scrAddrList) {
         capnAddrs.set(i++, capnp::Data::Builder(
            (uint8_t*)scrAddr.getPtr(), scrAddr.getSize()
         ));
      }
   }

   void ledgersToCapnp(
      const std::vector<std::shared_ptr<DBClientClasses::LedgerEntry>>& ledgers,
      Codec::Types::TxLedger::Builder& txLedger)
   {
      auto capnLedgers = txLedger.initLedgers(ledgers.size());
      for (unsigned i=0; i<ledgers.size(); i++) {
         auto capnLedger = capnLedgers[i];
         ledgerToCapnp(*ledgers[i], capnLedger);
      }
   }

   void ledgersToCapnp(const std::vector<DBClientClasses::HistoryPage>& pages,
      capnp::List<Codec::Types::TxLedger, capnp::Kind::STRUCT>::Builder& txLedgers)
   {
      for (unsigned i=0; i<pages.size(); i++) {
         auto txLedger = txLedgers[i];
         const auto& page = pages[i];

         auto capnLedgers = txLedger.initLedgers(page.size());
         for (unsigned y=0; y<page.size(); y++) {
            auto capnLedger = capnLedgers[y];
            ledgerToCapnp(page[y], capnLedger);
         }
      }
   }

   void nodeStatusToCapnp (std::shared_ptr<DBClientClasses::NodeStatus> nodeStatus,
      NodeStatus::Builder& capnNodeStatus)
   {
      capnNodeStatus.setNode((NodeStatus::NodeState)nodeStatus->state());
      capnNodeStatus.setRpc((NodeStatus::RpcState)nodeStatus->rpcState());
      capnNodeStatus.setIsSW(nodeStatus->isSegWitEnabled());

      auto capnChainState = capnNodeStatus.initChain();
      const auto& chainState = nodeStatus->chainStatus();
      capnChainState.setChainState((ChainStatus::ChainState)chainState.state());
      capnChainState.setBlockSpeed(chainState.getBlockSpeed());
      capnChainState.setProgress(chainState.getProgressPct());
      capnChainState.setEta(chainState.getETA());
      capnChainState.setBlocksLeft(chainState.getBlocksLeft());
   }

   void utxosToCapnp(const std::vector<::UTXO>& utxos,
      capnp::List<Codec::Bridge::UTXO, capnp::Kind::STRUCT>::Builder& capnOutputs)
   {
      for (unsigned i=0; i<utxos.size(); i++) {
         auto capnUtxo = capnOutputs[i];
         const auto& utxo = utxos[i];

         //scrAddr
         const auto& script = utxo.getScript();
         auto scrAddr = BtcUtils::getScrAddrForScript(script);
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
}

////////////////////////////////////////////////////////////////////////////////
////
////  CppBridge
////
////////////////////////////////////////////////////////////////////////////////
CppBridge::CppBridge(const std::filesystem::path& path, const std::string& dbAddr,
   const std::string& dbPort, bool oneWayAuth, bool offline) :
   path_(path), dbAddr_(dbAddr), dbPort_(dbPort),
   dbOneWayAuth_(oneWayAuth), dbOffline_(offline)
{}

std::shared_ptr<AsyncClient::BlockDataViewer> CppBridge::bdvPtr() const
{
   return bdvPtr_;
}

void CppBridge::reset()
{
   if (bdvPtr_) {
      bdvPtr_->unregisterFromDB();
   }
   bdvPtr_.reset();
   callbackPtr_.reset();
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::writeToClient(BinaryData& payload) const
{
   auto payloadPtr = std::make_unique<WritePayload_Bridge>();
   payloadPtr->data = std::move(payload);
   writeLambda_(std::move(payloadPtr));
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::callbackWriter(ServerPushWrapper& wrapper)
{
   setCallbackHandler(wrapper);
   writeToClient(wrapper.payload);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::loadWallets(const std::string& callbackId, MessageId msgId)
{
   if (wltManager_ != nullptr) {
      return;
   }

   auto thrLbd = [this, callbackId, msgId](void)->void
   {
      auto passPromptObj = std::make_shared<BridgePassphrasePrompt>(
         callbackId, [this](ServerPushWrapper wrapper) {
            this->callbackWriter(wrapper);
      });
      auto lbd = passPromptObj->getLambda();
      wltManager_ = std::make_shared<WalletManager>(path_, lbd);
      auto response = createWalletsPacket(msgId);
      writeToClient(response);
   };

   std::thread thr(thrLbd);
   if (thr.joinable()) {
      thr.detach();
   }
}

////////////////////////////////////////////////////////////////////////////////
WalletPtr CppBridge::getWalletPtr(const std::string& wltId) const
{
   auto wai = WalletAccountIdentifier::deserialize(wltId);
   auto wltContainer = wltManager_->getWalletContainer(
      wai.walletId, wai.accountId);
   return wltContainer->getWalletPtr();
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::createWalletsPacket(MessageId msgId)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   auto serviceReply = reply.initService();

   //grab wallet map
   auto accountIdMap = wltManager_->getAccountIdMap();
   size_t count = 0;
   for (const auto& idIt : accountIdMap) {
      if (idIt.first.empty() || idIt.second.empty()) {
         continue;
      }
      count += idIt.second.size();
   }
   auto wltPackets = serviceReply.initLoadWallets(count);

   unsigned i=0;
   for (const auto& idIt : accountIdMap) {
      if (idIt.first.empty() || idIt.second.empty()) {
         continue;
      }

      for (const auto& accId : idIt.second) {
         auto capnWallet = wltPackets[i++];
         auto firstCont = wltManager_->getWalletContainer(idIt.first, accId);
         auto wltPtr = firstCont->getWalletPtr();
         auto commentMap = wltPtr->getCommentMap();
         walletToCapnp(wltPtr, accId, commentMap, capnWallet);
      }
   }

   reply.setReferenceId(msgId);
   reply.setSuccess(true);
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
bool CppBridge::deleteWallet(const std::string& id)
{
   try {
      auto wai = WalletAccountIdentifier::deserialize(id);
      wltManager_->deleteWallet(wai.walletId, wai.accountId);
   } catch (const std::exception& e) {
      LOGWARN << "failed to delete wallet with error: " << e.what();
      return false;
   }
   return true;
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::setupDB()
{
   if (dbOffline_) {
      LOGWARN << "attempt to connect to DB in offline mode, ignoring";
      return;
   }

   auto lbd = [this](void)->void
   {
      //sanity check
      if (bdvPtr_ != nullptr) {
         return;
      }

      if (wltManager_ == nullptr) {
         throw std::runtime_error("wallet manager is not initialized");
      }

      //lambda to push notifications over to the gui socket
      auto pushNotif = [this](BinaryData& msg)->void
      {
         this->writeToClient(msg);
      };

      //setup bdv obj
      callbackPtr_ = std::make_shared<BridgeCallback>(wltManager_, pushNotif);
      bdvPtr_ = AsyncClient::BlockDataViewer::getNewBDV(
         dbAddr_, dbPort_, path_,
         TerminalPassphrasePrompt::getLambda("db identification key"),
         true, dbOneWayAuth_, callbackPtr_
      );

      //TODO: set gui prompt to accept server pubkeys
      bdvPtr_->setCheckServerKeyPromptLambda(
         [](const BinaryData&, const std::string&)->bool{return true;});

      //set bdvPtr in wallet manager
      wltManager_->setBdvPtr(bdvPtr_);

      //connect to db
      try {
         bdvPtr_->connectToRemote();
         bdvPtr_->registerWithDB(
            Config::BitcoinSettings::getMagicBytes().toHexStr());

         //notify setup is done
         callbackPtr_->notify_SetupDone();
      } catch (const std::exception& e) {
         LOGERR << "failed to connect to db with error: " << e.what();
      }
   };

   std::thread thr(lbd);
   if (thr.joinable()) {
      thr.join(); //set back to detach
   }
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::registerWallets()
{
   wltManager_->registerWallets();

   std::set<std::string> walletIds;
   auto accountIdMap = wltManager_->getAccountIdMap();
   for (const auto& idIt : accountIdMap) {
      for (const auto& accId : idIt.second) {
         WalletAccountIdentifier wai(idIt.first, accId);
         walletIds.insert(wai.serialize());
      }
   }
   callbackPtr_->notify_SetupRegistrationDone(walletIds);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::registerWallet(const std::string& id, bool isNew)
{
   try {
      auto wai = WalletAccountIdentifier::deserialize(id);
      wltManager_->registerWallet(
         wai.walletId, wai.accountId, isNew);
      callbackPtr_->waitOnId(id);
   } catch (const std::exception& e) {
      LOGERR << "failed to register wallet with error: " << e.what();
   }
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::createBackupStringForWallet(const std::string& waaId,
   const std::string& callbackId, MessageId msgId)
{
   auto wai = WalletAccountIdentifier::deserialize(waaId);
   auto backupStringLbd = [this, wai, msgId, callbackId]()->void
   {
      auto passPromptObj = std::make_shared<BridgePassphrasePrompt>(
         callbackId, [this](ServerPushWrapper wrapper){
            this->callbackWriter(wrapper);
         });
      auto lbd = passPromptObj->getLambda();

      std::unique_ptr<Seeds::WalletBackup> backupData;
      try {
         //grab wallet
         auto wltContainer = wltManager_->getWalletContainer(wai.walletId);

         //grab root
         backupData = move(wltContainer->getBackupStrings(lbd));
      } catch (const std::exception&) {
         backupData = nullptr;
      }

      //wind down passphrase prompt
      passPromptObj->cleanup();

      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(msgId);
      if (backupData == nullptr) {
         //return on error
         reply.setSuccess(false);
         auto payload = serializeCapnp(message);
         writeToClient(payload);
         return;
      }

      auto backupE16 = dynamic_cast<Armory::Seeds::Backup_Easy16*>(
         backupData.get());
      if (backupE16 == nullptr) {
         throw std::runtime_error("[createBackupStringForWallet]"
            " invalid backup type");
      }
      auto walletReply = reply.initWallet();
      auto backupStringCapnp = walletReply.initCreateBackupString();

      //cleartext root
      {
         auto line1 = backupE16->getRoot(
            Armory::Seeds::Backup_Easy16::LineIndex::One, false);
         auto line2 = backupE16->getRoot(
            Armory::Seeds::Backup_Easy16::LineIndex::Two, false);
         auto clearLines = backupStringCapnp.initRootClear(2);
         clearLines.set(0, capnp::Text::Reader(line1.data(), line1.size()));
         clearLines.set(1, capnp::Text::Reader(line2.data(), line2.size()));

         //encrypted root
         auto line3 = backupE16->getRoot(
            Armory::Seeds::Backup_Easy16::LineIndex::One, true);
         auto line4 = backupE16->getRoot(
            Armory::Seeds::Backup_Easy16::LineIndex::Two, true);
         auto encrLines = backupStringCapnp.initRootEncr(2);
         encrLines.set(0, capnp::Text::Reader(line3.data(), line3.size()));
         encrLines.set(1, capnp::Text::Reader(line4.data(), line4.size()));
      }

      if (backupE16->hasChaincode()) {
         //cleartext chaincode
         auto line1 = backupE16->getChaincode(
            Armory::Seeds::Backup_Easy16::LineIndex::One, false);
         auto line2 = backupE16->getChaincode(
            Armory::Seeds::Backup_Easy16::LineIndex::Two, false);
         auto clearLines = backupStringCapnp.initChainClear(2);
         clearLines.set(0, capnp::Text::Reader(line1.data(), line1.size()));
         clearLines.set(1, capnp::Text::Reader(line2.data(), line2.size()));

         //encrypted chaincode
         auto line3 = backupE16->getChaincode(
            Armory::Seeds::Backup_Easy16::LineIndex::One, true);
         auto line4 = backupE16->getChaincode(
            Armory::Seeds::Backup_Easy16::LineIndex::Two, true);
         auto encrLines = backupStringCapnp.initChainEncr(2);
         encrLines.set(0, capnp::Text::Reader(line3.data(), line3.size()));
         encrLines.set(1, capnp::Text::Reader(line4.data(), line4.size()));
      }

      //secure print passphrase
      auto spPass = backupE16->getSpPass();
      backupStringCapnp.setSpPass(
         capnp::Text::Reader(spPass.data(), spPass.size()));

      reply.setSuccess(true);
      auto payload = serializeCapnp(message);
      writeToClient(payload);
   };

   std::thread thr(backupStringLbd);
   if (thr.joinable()) {
      thr.detach();
   }
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::restoreWallet(
   const std::vector<std::string_view>& lines_sv,
   const std::string_view& spPass_sv,
   const std::string_view& callbackId)
{
   //NOTE: easy16 only for now, will need a dedicated call for BIP39

   /*
   Needs 2 lines for the root, possibly another 2 for the chaincode, possibly
   1 more for the SecurePrint passphrase.

   This call will block waiting on user replies to the prompt for the different
   steps in the wallet restoration process (checking id, checkums, passphrase
   requests). It has to run in its own thread.
   */

   auto backup = Seeds::Backup_Easy16::fromLines(lines_sv, spPass_sv);

   //
   auto restoreLbd = [this](
      std::unique_ptr<Seeds::Backup_Easy16> backup,
      const std::string callbackId)
   {
      auto createCallbackMessage = [callbackId](
         const Seeds::RestorePrompt& prompt, uint32_t notifCounter)->BinaryData
      {
         capnp::MallocMessageBuilder message;
         auto promptCapnp = message.initRoot<FromBridge>();
         auto notifCapnp = promptCapnp.initNotification();
         notifCapnp.setCallbackId(callbackId);
         notifCapnp.setCounter(notifCounter);

         auto restore = notifCapnp.initRestore();
         switch (prompt.promptType)
         {
            case Seeds::RestorePromptType::FormatError:
            case Seeds::RestorePromptType::Failure:
            {
               restore.setFailure(prompt.error);
               break;
            }

            case Seeds::RestorePromptType::ChecksumError:
            {
               auto chksumCapnp = restore.initChecksumError(
                  prompt.checksumResult.size());

               unsigned i=0;
               for (const auto& chkResult : prompt.checksumResult) {
                  auto capnChkResult = chksumCapnp[i++];
                  capnChkResult.setLineId(chkResult.first);
                  capnChkResult.setValue(chkResult.second);
               }
               break;
            }

            case Seeds::RestorePromptType::ChecksumMismatch:
            {
               auto chksumCapnp = restore.initChecksumMismatch(
                  prompt.checksumResult.size());

               unsigned i=0;
               for (const auto& chkResult : prompt.checksumResult) {
                  auto capnChkResult = chksumCapnp[i++];
                  capnChkResult.setLineId(chkResult.first);
                  capnChkResult.setValue(chkResult.second);
               }
               break;
            }

            case Seeds::RestorePromptType::DecryptError:
            {
               restore.setDecryptError();
               break;
            }

            case Seeds::RestorePromptType::Passphrases:
            {
               restore.setGetPassphrases();
               break;
            }

            case Seeds::RestorePromptType::Id:
            {
               auto metaCapnp = restore.initCheckWalletId();
               metaCapnp.setWalletId(prompt.walletId);
               metaCapnp.setBackupType((int)prompt.backupType);
               break;
            }

            case Seeds::RestorePromptType::TypeError:
            {
               restore.setTypeError(prompt.error);
               break;
            }

            case Seeds::RestorePromptType::Success:
            {
               restore.setSuccess();
               break;
            }

            default:
               throw std::runtime_error("invalid prompt type");
         }
         return serializeCapnp(message);
      };

      auto callback = [this, createCallbackMessage](
         const Seeds::RestorePrompt& prompt)->Seeds::PromptReply
      {
         if (prompt.needsReply()) {
            auto counterBd = BtcUtils::fortuna_.generateRandom(4);
            auto notifCounter = *(uint32_t*)counterBd.getPtr();
            auto message = createCallbackMessage(prompt, notifCounter);

            //setup reply lambda
            auto prom = std::make_shared<std::promise<Seeds::PromptReply>>();
            auto fut = prom->get_future();
            auto replyLbd = [prom](const Seeds::PromptReply& reply)->bool
            {
               prom->set_value(reply);
               return true;
            };

            //push prompt to caller
            ServerPushWrapper wrapper{notifCounter, replyLbd, std::move(message)};
            callbackWriter(wrapper);

            //wait on reply
            return fut.get();
         } else {
            auto message = createCallbackMessage(prompt, 0);
            ServerPushWrapper wrapper{0, nullptr, std::move(message)};
            callbackWriter(wrapper);
            return {false};
         }
      };

      try {
         //create wallet from backup
         Wallets::WalletCreationParams params{
            {}, //passphrase, leave empty so that it prompts the user
            {}, //control passphrase, same treatment
            wltManager_->getWalletDir() //folder where the wallet is created
            //TODO: add kdf params and lookup
         };
         auto wltPtr = Armory::Seeds::Helpers::restoreFromBackup(
            std::move(backup), callback, params);

         if (wltPtr == nullptr) {
            throw std::runtime_error("empty wallet");
         }

         //add wallet to manager
         auto accIds = wltPtr->getAccountIDs();
         for (const auto& accId : accIds) {
            wltManager_->addWallet(wltPtr, accId);
         }

         //signal caller of success
         callback(Seeds::RestorePrompt{Seeds::RestorePromptType::Success});
      } catch (const Armory::Seeds::RestoreUserException& e) {
         /*
         These type of errors are the result of user actions. They should have
         an opportunity to fix the issue. Consequently, no error flag will be
         pushed to the client.
         */

         LOGWARN << "[restoreFromBackup] user exception: " << e.what();
      } catch (const std::exception& e) {
         LOGERR << "[restoreFromBackup] fatal error: " << e.what();

         /*
         Report error to client
         */
         Seeds::RestorePrompt errorPrompt{Seeds::RestorePromptType::Failure};
         errorPrompt.error = e.what();
         callback(errorPrompt);
      }
   };

   auto worker = std::thread(restoreLbd,
      std::move(backup), std::string{callbackId});
   if (worker.joinable()) {
      worker.detach();
   }
}

////////////////////////////////////////////////////////////////////////////////
const std::string& CppBridge::getLedgerDelegateId()
{
   auto promPtr = std::make_shared<std::promise<AsyncClient::LedgerDelegate>>();
   auto fut = promPtr->get_future();
   auto lbd = [promPtr](
      ReturnMessage<AsyncClient::LedgerDelegate> result)->void
   {
      promPtr->set_value(std::move(result.get()));
   };

   bdvPtr_->getLedgerDelegate(lbd);
   auto delegate = std::move(fut.get());
   auto insertPair = delegateMap_.emplace(
      delegate.getID(), std::move(delegate));

   if (!insertPair.second) {
      insertPair.first->second = std::move(delegate);
   }
   return insertPair.first->second.getID();
}

////////////////////////////////////////////////////////////////////////////////
const std::string& CppBridge::getLedgerDelegateIdForWallet(
   const std::string& walletId)
{
   auto promPtr = std::make_shared<std::promise<AsyncClient::LedgerDelegate>>();
   auto fut = promPtr->get_future();
   auto lbd = [promPtr](
      ReturnMessage<AsyncClient::LedgerDelegate> result)->void
   {
      promPtr->set_value(std::move(result.get()));
   };

   auto walletObj = bdvPtr_->getWalletObj(walletId);
   walletObj.getLedgerDelegate(lbd);
   auto delegate = std::move(fut.get());
   auto insertPair = delegateMap_.emplace(
      delegate.getID(), std::move(delegate));

   if (!insertPair.second) {
      insertPair.first->second = std::move(delegate);
   }
   return insertPair.first->second.getID();
}

////////////////////////////////////////////////////////////////////////////////
const std::string& CppBridge::getLedgerDelegateIdForScrAddr(
   const std::string& walletId, const BinaryDataRef& addrHash)
{
   auto promPtr = std::make_shared<std::promise<AsyncClient::LedgerDelegate>>();
   auto fut = promPtr->get_future();
   auto lbd = [promPtr](
      ReturnMessage<AsyncClient::LedgerDelegate> result)->void
   {
      promPtr->set_value(std::move(result.get()));
   };

   auto walletObj = bdvPtr_->getWalletObj(walletId);
   auto scrAddrObj = walletObj.getScrAddrObj(addrHash, 0, 0, 0, 0);
   scrAddrObj.getLedgerDelegate(lbd);
   auto delegate = std::move(fut.get());
   auto insertPair = delegateMap_.emplace(
      delegate.getID(), std::move(delegate));

   if (!insertPair.second) {
      insertPair.first->second = std::move(delegate);
   }
   return insertPair.first->second.getID();
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::getHistoryPageForDelegate(
   const std::string& id, unsigned from, unsigned to, MessageId msgId)
{
   auto iter = delegateMap_.find(id);
   if (iter == delegateMap_.end()) {
      throw std::runtime_error("unknown delegate: " + id);
   }

   auto lbd = [this, msgId](
      ReturnMessage<std::vector<DBClientClasses::HistoryPage>> result)->void
   {
      auto histVec = result.get();

      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(msgId);
      auto delegate = reply.initDelegate();
      auto pages = delegate.initGetPages(histVec.size());
      ledgersToCapnp(histVec, pages);

      auto payload = serializeCapnp(message);
      this->writeToClient(payload);
   };
   iter->second.getHistoryPages(from, to, lbd);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getNodeStatus(MessageId msgId)
{
   //grab node status
   auto promPtr = std::make_shared<
      std::promise<std::shared_ptr<DBClientClasses::NodeStatus>>>();
   auto fut = promPtr->get_future();
   auto lbd = [promPtr](
      ReturnMessage<std::shared_ptr<DBClientClasses::NodeStatus>> result)->void
   {
      try {
         promPtr->set_value(result.get());
      } catch (const std::exception&) {
         promPtr->set_exception(std::current_exception());
      }
   };
   bdvPtr_->getNodeStatus(lbd);

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);

   try {
      auto nodeStatus = fut.get();
      auto serviceReply = reply.initService();
      auto nodeCapnp = serviceReply.initGetNodeStatus();
      nodeStatusToCapnp(nodeStatus, nodeCapnp);
      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setSuccess(false);
      reply.setError(e.what());
   }

   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getBalanceAndCount(const std::string& id, MessageId msgId)
{
   auto wai = WalletAccountIdentifier::deserialize(id);
   auto wltContainer = wltManager_->getWalletContainer(
      wai.walletId, wai.accountId);

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   auto walletReply = reply.initWallet();
   auto bnc = walletReply.initGetBalanceAndCount();

   bnc.setFull(wltContainer->getFullBalance());
   bnc.setSpendable(wltContainer->getSpendableBalance());
   bnc.setUnconfirmed(wltContainer->getUnconfirmedBalance());
   bnc.setTxnCount(wltContainer->getTxIOCount());

   reply.setSuccess(true);
   reply.setReferenceId(msgId);
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getAddrCombinedList(const std::string& id, MessageId msgId)
{
   auto wai = WalletAccountIdentifier::deserialize(id);
   auto wltContainer = wltManager_->getWalletContainer(
      wai.walletId, wai.accountId);
   auto addrMap = wltContainer->getAddrBalanceMap();

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   auto walletReply = reply.initWallet();
   auto combinedReply = walletReply.initGetAddrCombinedList();

   auto bncReply = combinedReply.initBalances(addrMap.size());
   unsigned i=0;
   for (auto& addrPair : addrMap) {
      auto addrReply = bncReply[i++];
      addrReply.setScrAddr(capnp::Data::Builder(
         (uint8_t*)addrPair.first.getPtr(), addrPair.first.getSize()
      ));

      auto bnc = addrReply.initBalances();
      bnc.setFull(addrPair.second[0]);
      bnc.setSpendable(addrPair.second[1]);
      bnc.setUnconfirmed(addrPair.second[2]);
      bnc.setTxnCount(addrPair.second[3]);
   }

   auto updatedMap = wltContainer->getUpdatedAddressMap();
   auto accPtr = wltContainer->getAddressAccount();

   auto addrDataReply = combinedReply.initUpdatedAssets(updatedMap.size());
   i=0;
   for (const auto& addrPair : updatedMap) {
      auto capnAddr = addrDataReply[i++];
      addressToCapnp(capnAddr,
         addrPair.second, accPtr,
         wltContainer->getDefaultEncryptionKeyId());
   }

   reply.setSuccess(true);
   reply.setReferenceId(msgId);
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getHighestUsedIndex(const std::string& id, MessageId msgId)
{
   auto wai = WalletAccountIdentifier::deserialize(id);
   auto wltContainer = wltManager_->getWalletContainer(
      wai.walletId, wai.accountId);

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   auto walletReply = reply.initWallet();
   walletReply.setGetHighestUsedIndex(wltContainer->getHighestUsedIndex());

   reply.setSuccess(true);
   reply.setReferenceId(msgId);
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::extendAddressPool(const std::string& wltId,
   unsigned count, const std::string& callbackId, MessageId msgId)
{
   auto wai = WalletAccountIdentifier::deserialize(wltId);
   auto wltContainer = wltManager_->getWalletContainer(
      wai.walletId, wai.accountId);
   auto wltPtr = wltContainer->getWalletPtr();
   const auto& accId = wai.accountId;

   //run chain extention in another thread
   auto extendChain =
      [this, wltPtr, accId, count, msgId, callbackId]()
   {
      auto accPtr = wltPtr->getAccountForID(accId);

      //setup progress reporting
      size_t tickTotal = count * accPtr->getNumAssetAccounts();
      size_t tickCount = 0;
      int reportedTicks = -1;
      auto now = std::chrono::system_clock::now();

      //progress callback
      auto updateProgress = [this, callbackId, tickTotal,
         &tickCount, &reportedTicks, now](int)
      {
         ++tickCount;
         auto msElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now() - now).count();

         //report an event every 250ms
         int eventCount = msElapsed / 250;
         if (eventCount < reportedTicks) {
            return;
         }
         reportedTicks = eventCount;

         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<FromBridge>();
         auto notif = fromBridge.getNotification();
         auto progressNotif = notif.initProgress();

         float progressFloat = float(tickCount) / float(tickTotal);
         progressNotif.setProgress(progressFloat);
         progressNotif.setNumericProgress(tickCount);
         auto notifIds = progressNotif.initIds(1);
         notifIds.set(0, callbackId);

         auto serialized = serializeCapnp(message);
         this->writeToClient(serialized);
      };

      //extend chain
      accPtr->extendPublicChain(wltPtr->getIface(), count, updateProgress);

      //shutdown progress dialog
      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto notif = fromBridge.getNotification();
      auto progressNotif = notif.initProgress();

      progressNotif.setProgress(0);
      progressNotif.setNumericProgress(0);
      progressNotif.setPhase((uint32_t)BDMPhase_Completed);
      auto notifIds = progressNotif.initIds(1);
      notifIds.set(0, callbackId);

      auto serialized = serializeCapnp(message);
      this->writeToClient(serialized);

      //complete process
      capnp::MallocMessageBuilder replyMessage;
      auto replyBridge = replyMessage.initRoot<FromBridge>();
      auto reply = replyBridge.initReply();
      reply.setSuccess(true);
      reply.setReferenceId(msgId);

      auto walletReply = reply.initWallet();
      auto capnWallet = walletReply.initExtendAddressPool();
      walletToCapnp(wltPtr, accId, {}, capnWallet);

      auto replySerialized = serializeCapnp(replyMessage);
      this->writeToClient(replySerialized);
   };

   std::thread thr(extendChain);
   if (thr.joinable()) {
      thr.detach();
   }
}

////////////////////////////////////////////////////////////////////////////////
WalletAccountIdentifier CppBridge::createWallet(uint32_t lookup,
   const std::string& label, const std::string& description,
   const SecureBinaryData& controlPassphrase,
   const SecureBinaryData& passphrase,
   const SecureBinaryData& extraEntropy)
{
   //sanity check
   if (wltManager_ == nullptr) {
      throw std::runtime_error("wallet manager is not initialized");
   }

   //create wallet
   auto wallet = wltManager_->createNewWallet(
      passphrase, controlPassphrase, extraEntropy, lookup);

   //set labels
   auto wltPtr = wallet->getWalletPtr();
   if (!label.empty()) {
      wltPtr->setLabel(label);
   }
   if (!description.empty()) {
      wltPtr->setDescription(description);
   }

   return WalletAccountIdentifier{wltPtr->getID(), wltPtr->getMainAccountID()};
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getWalletPacket(const std::string& id, MessageId msgId) const
{
   auto wai = WalletAccountIdentifier::deserialize(id);
   auto wltContainer = wltManager_->getWalletContainer(
      wai.walletId, wai.accountId);

   auto wltPtr = wltContainer->getWalletPtr();
   auto commentMap = wltPtr->getCommentMap();

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);
   reply.setSuccess(true);

   auto walletReply = reply.initWallet();
   auto capnWallet = walletReply.initGetData();
   walletToCapnp(wltPtr, wai.accountId, commentMap, capnWallet);

   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getAddress(const std::string& id,
   uint32_t addrType, uint32_t addrKind, MessageId msgId)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);

   auto wai = WalletAccountIdentifier::deserialize(id);
   auto wltContainer = wltManager_->getWalletContainer(
      wai.walletId, wai.accountId);
   auto wltPtr = wltContainer->getWalletPtr();
   auto accPtr = wltContainer->getAddressAccount();
   std::shared_ptr<AddressEntry> addrPtr;

   switch (addrKind)
   {
      case WalletRequest::AddressRequest::NEW:
      {
         addrPtr = accPtr->getNewAddress(
            wltPtr->getIface(), (AddressEntryType)addrType);
         break;
      }

      case WalletRequest::AddressRequest::CHANGE:
      {
         addrPtr = accPtr->getNewChangeAddress(
            wltPtr->getIface(), (AddressEntryType)addrType);
         break;
      }

      case WalletRequest::AddressRequest::PEEK_CHANGE:
      {
         addrPtr = accPtr->peekNextChangeAddress(
            wltPtr->getIface(), (AddressEntryType)addrType);
         break;
      }

      default:
         reply.setSuccess(false);
         reply.setError("invalid address kind");
   }

   if (addrPtr != nullptr) {
      auto walletReply = reply.initWallet();
      auto capnAddr = walletReply.initGetAddress();
      addressToCapnp(capnAddr, addrPtr, accPtr,
         wltContainer->getDefaultEncryptionKeyId());
   }
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::getTxsByHash(const std::set<BinaryData>& hashes, MessageId msgId)
{
   auto lbd = [this, msgId](ReturnMessage<AsyncClient::TxBatchResult> result)
   {
      AsyncClient::TxBatchResult txMap;
      bool valid = false;
      try {
         txMap = std::move(result.get());
         valid = true;
      } catch(const std::exception&) {}

      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(msgId);
      if (!valid) {
         reply.setSuccess(false);
      } else {
         auto service = reply.initService();
         auto capnTxs = service.initGetTxsByHash(txMap.size());
         unsigned i = 0;
         for (const auto& tx : txMap) {
            auto capnTx = capnTxs[i++];
            capnTx.setHash(capnp::Data::Builder(
               (uint8_t*)tx.first.getPtr(), tx.first.getSize()
            ));

            auto txRaw = tx.second->serialize();
            capnTx.setRaw(capnp::Data::Builder(
               (uint8_t*)txRaw.getPtr(), txRaw.getSize()));

            capnTx.setRbf(tx.second->isRBF());
            capnTx.setChainedZc(tx.second->isChained());
            capnTx.setHeight(tx.second->getTxHeight());
            capnTx.setTxIndex(tx.second->getTxIndex());
         }
         reply.setSuccess(true);
      }

      auto serialized = serializeCapnp(message);
      this->writeToClient(serialized);
   };

   bdvPtr_->getTxsByHash(hashes, lbd);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getTxInScriptType(
   const BinaryData& script, const BinaryData& hash, MessageId msgId) const
{
   auto typeInt = BtcUtils::getTxInScriptTypeInt(script, hash);

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   auto utilsReply = reply.initScriptUtils();
   utilsReply.setGetTxInScriptType(typeInt);

   reply.setSuccess(true);
   reply.setReferenceId(msgId);
   return serializeCapnp(message);;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getTxOutScriptType(
   const BinaryData& script, MessageId msgId) const
{
   auto typeInt = BtcUtils::getTxOutScriptTypeInt(script);

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   auto utilsReply = reply.initScriptUtils();
   utilsReply.setGetTxOutScriptType(typeInt);

   reply.setSuccess(true);
   reply.setReferenceId(msgId);
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getScrAddrForScript(
   const BinaryData& script, MessageId msgId) const
{
   auto scrAddr = BtcUtils::getScrAddrForScript(script);

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   auto utilsReply = reply.initScriptUtils();
   utilsReply.setGetScrAddrForScript(capnp::Data::Builder(
      (uint8_t*)scrAddr.getPtr(), scrAddr.getSize()
   ));

   reply.setSuccess(true);
   reply.setReferenceId(msgId);
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getScrAddrForAddrStr(
   const std::string& addrStr, MessageId msgId) const
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);

   try {
      auto scrAddr = BtcUtils::getScrAddrForAddrStr(addrStr);
      auto utilsReply = reply.initScriptUtils();
      utilsReply.setGetScrAddrForAddrStr(capnp::Data::Builder(
         (uint8_t*)scrAddr.getPtr(), scrAddr.getSize()
      ));
      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setSuccess(false);
      reply.setError(e.what());
   }
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getLastPushDataInScript(
   const BinaryData& script, MessageId msgId) const
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);

   auto pushData = BtcUtils::getLastPushDataInScript(script);
   if (pushData.empty()) {
      reply.setSuccess(false);
   } else {
      auto utilsReply = reply.initScriptUtils();
      utilsReply.setGetLastPushDataInScript(capnp::Data::Builder(
         (uint8_t*)pushData.getPtr(), pushData.getSize()
      ));
      reply.setSuccess(true);
   }
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getHash160(
   const BinaryDataRef& dataRef, MessageId msgId) const
{
   auto hash = BtcUtils::getHash160(dataRef);

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);
   reply.setSuccess(true);

   auto utilsReply = reply.initUtils();
   utilsReply.setGetHash160(capnp::Data::Builder(
      (uint8_t*)hash.getPtr(), hash.getSize()
   ));
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getTxOutScriptForScrAddr(
   const BinaryData& script, MessageId msgId) const
{
   auto result = BtcUtils::getTxOutScriptForScrAddr(script);

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);
   reply.setSuccess(true);

   auto utilsReply = reply.initScriptUtils();
   utilsReply.setGetTxOutScriptForScrAddr(capnp::Data::Builder(
      (uint8_t*)result.getPtr(), result.getSize()
   ));
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getAddrStrForScrAddr(
   const BinaryData& script, MessageId msgId) const
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);

   try {
      auto addrStr = BtcUtils::getAddressStrFromScrAddr(script);

      auto utilsReply = reply.initScriptUtils();
      utilsReply.setGetAddrStrForScrAddr(addrStr);
      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setSuccess(false);
      reply.setError(e.what());
   }
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
std::string CppBridge::getNameForAddrType(int addrTypeInt) const
{
   std::string result;

   auto nestedFlag = addrTypeInt & ADDRESS_NESTED_MASK;
   bool nested = false;
   switch (nestedFlag)
   {
      case 0:
         break;

      case AddressEntryType_P2SH:
         result += "P2SH";
         nested = true;
         break;

      case AddressEntryType_P2WSH:
         result += "P2WSH";
         nested = true;
         break;

      default:
         throw std::runtime_error("[getNameForAddrType] unknown nested flag");
   }

   auto addressType = addrTypeInt & ADDRESS_TYPE_MASK;
   if (addressType == 0) {
      return result;
   }

   if (nested)
      result += "-";

   switch (addressType)
   {
      case AddressEntryType_P2PKH:
         result += "P2PKH";
         break;

      case AddressEntryType_P2PK:
         result += "P2PK";
         break;

      case AddressEntryType_P2WPKH:
         result += "P2WPKH";
         break;

      case AddressEntryType_Multisig:
         result += "Multisig";
         break;

      default:
         throw std::runtime_error("[getNameForAddrType] unknown address type");
   }

   if (addrTypeInt & ADDRESS_COMPRESSED_MASK) {
      result += " (Uncompressed)";
   }

   if (result.empty()) {
      result = "N/A";
   }
   return result;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::setAddressTypeFor(
   const std::string& walletId, const BinaryDataRef& idRef,
   uint32_t addrType, MessageId msgId) const
{
   auto wai = WalletAccountIdentifier::deserialize(walletId);
   auto wltContainer = wltManager_->getWalletContainer(
      wai.walletId, wai.accountId);
   auto wltPtr = wltContainer->getWalletPtr();
   auto assetId = Armory::Wallets::AssetId::deserializeKey(
      idRef, PROTO_ASSETID_PREFIX);

   //set address type in wallet
   wltPtr->updateAddressEntryType(assetId, (AddressEntryType)addrType);

   //get address entry object
   auto accPtr = wltPtr->getAccountForID(assetId.getAddressAccountId());
   auto addrPtr = accPtr->getAddressEntryForID(assetId);

   //return address proto payload
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);
   reply.setSuccess(true);

   auto walletReply = reply.initWallet();
   auto capnAddr = walletReply.initSetAddressTypeFor();
   addressToCapnp(capnAddr, addrPtr, accPtr,
      wltContainer->getDefaultEncryptionKeyId());
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::getHeadersByHeight(
  const std::vector<unsigned>& heights, MessageId msgId)
{
   auto lbd = [this, msgId](
      ReturnMessage<std::vector<DBClientClasses::BlockHeader>> result)->void
   {
      auto headers = result.get();

      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(msgId);
      reply.setSuccess(true);

      auto service = reply.initService();
      auto capnHeaders = service.initGetHeadersByHeight(headers.size());
      for (unsigned i=0; i<headers.size(); i++) {
         capnHeaders.set(i, capnp::Data::Builder(
            (uint8_t*)headers[i].getPtr(), headers[i].getSize()
         ));
      }

      auto serialized = serializeCapnp(message);
      this->writeToClient(serialized);
   };

   auto bc = bdvPtr_->blockchain();
   bc.getHeadersByHeight(heights, lbd);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::setupNewCoinSelectionInstance(const std::string& id,
   unsigned height, MessageId msgId)
{
   auto wai = WalletAccountIdentifier::deserialize(id);
   auto wltContainer = wltManager_->getWalletContainer(
      wai.walletId, wai.accountId);

   auto csId = fortuna_.generateRandom(6).toHexStr();
   auto insertIter = csMap_.emplace(csId,
      std::shared_ptr<CoinSelection::CoinSelectionInstance>()).first;
   auto csPtr = &insertIter->second;

   auto lbd = [this, wltContainer, csPtr, csId, height, msgId](
      ReturnMessage<std::vector<::AddressBookEntry>> result)->void
   {
      auto fetchLbd = [wltContainer](uint64_t val)->std::vector<::UTXO>
      {
         auto promPtr = std::make_shared<std::promise<std::vector<::UTXO>>>();
         auto fut = promPtr->get_future();
         auto lbd = [promPtr](ReturnMessage<std::vector<::UTXO>> result)->void
         {
            promPtr->set_value(result.get());
         };
         wltContainer->getUTXOs(val, false, false, lbd);
         return fut.get();
      };

      auto aeVec = std::move(result.get());
      *csPtr = std::make_shared<CoinSelection::CoinSelectionInstance>(
         wltContainer->getWalletPtr(), fetchLbd, aeVec,
         wltContainer->getSpendableBalance(), height);

      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(msgId);
      reply.setSuccess(true);

      auto wallet = reply.initWallet();
      wallet.setSetupNewCoinSelectionInstance(csId);

      auto serialized = serializeCapnp(message);
      this->writeToClient(serialized);
   };

   wltContainer->createAddressBook(lbd);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::destroyCoinSelectionInstance(const std::string& csId)
{
   csMap_.erase(csId);
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<CoinSelection::CoinSelectionInstance>
CppBridge::coinSelectionInstance(
   const std::string& csId) const
{
   auto iter = csMap_.find(csId);
   if (iter == csMap_.end()) {
      return nullptr;
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::createAddressBook(const std::string& id, MessageId msgId)
{
   auto wai = WalletAccountIdentifier::deserialize(id);
   auto wltContainer = wltManager_->getWalletContainer(
      wai.walletId, wai.accountId);

   auto lbd = [this, msgId](
      ReturnMessage<std::vector<AddressBookEntry>> result)->void
   {
      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(msgId);
      reply.setSuccess(true);

      auto wallet = reply.initWallet();
      auto addrBook = wallet.initCreateAddressBook();

      auto aeVec = std::move(result.get());
      auto capnEntries = addrBook.initEntries(aeVec.size());
      unsigned i=0;
      for (const auto& ae : aeVec) {
         auto capnEntry = capnEntries[i++];

         const auto& scrAddr = ae.getScrAddr();
         capnEntry.setScrAddr(capnp::Data::Builder(
            (uint8_t*)scrAddr.getPtr(), scrAddr.getSize()
         ));

         const auto& hashList = ae.getTxHashList();
         auto capnHashes = capnEntry.initTxHashes(hashList.size());
         unsigned y=0;
         for (const auto& hash : hashList) {
            capnHashes.set(y++, capnp::Data::Builder(
               (uint8_t*)hash.getPtr(), hash.getSize()
            ));
         }
      }

      auto serialized = serializeCapnp(message);
      this->writeToClient(serialized);
   };

   wltContainer->createAddressBook(lbd);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::setComment(const std::string& walletId,
   const std::string& hash, const std::string& comment)
{
   auto wai = WalletAccountIdentifier::deserialize(walletId);
   auto wltContainer = wltManager_->getWalletContainer(
      wai.walletId, wai.accountId);
   wltContainer->setComment(hash, comment);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::setWalletLabels(const std::string& walletId,
   const std::string& label, const std::string& desc)
{
   auto wai = WalletAccountIdentifier::deserialize(walletId);
   auto wltContainer = wltManager_->getWalletContainer(
      wai.walletId, wai.accountId);
   wltContainer->setLabels(label, desc);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::getUTXOs(const std::string& id,
   uint64_t value, bool zc, bool rbf, MessageId msgId)
{
   auto wai = WalletAccountIdentifier::deserialize(id);
   auto wltContainer = wltManager_->getWalletContainer(
      wai.walletId, wai.accountId);

   auto lbd = [this, msgId](ReturnMessage<std::vector<::UTXO>> result)->void
   {
      auto utxoVec = std::move(result.get());

      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(msgId);
      reply.setSuccess(true);

      auto wallet = reply.initWallet();
      auto capnUtxos = wallet.initGetUtxos(utxoVec.size());
      utxosToCapnp(utxoVec, capnUtxos);

      auto serialized = serializeCapnp(message);
      this->writeToClient(serialized);
   };

   wltContainer->getUTXOs(value, zc, rbf, lbd);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::initNewSigner(MessageId msgId)
{
   auto id = fortuna_.generateRandom(6).toHexStr();
   signerMap_.emplace(make_pair(id,
      std::make_shared<CppBridgeSignerStruct>(
         [this](const std::string& wltId)->auto {
            return this->getWalletPtr(wltId); },
         [this](ServerPushWrapper wrapper) {
            callbackWriter(wrapper);
         })
   ));

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);
   reply.setSuccess(true);

   auto signer = reply.initSigner();
   signer.setGetNew(id);

   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::destroySigner(const std::string& id)
{
   signerMap_.erase(id);
}

////////////////////////////////////////////////////////////////////////////////
std::shared_ptr<CppBridgeSignerStruct> CppBridge::signerInstance(
   const std::string& id) const
{
   auto iter = signerMap_.find(id);
   if (iter == signerMap_.end()) {
      return nullptr;
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::broadcastTx(const std::vector<BinaryData>& rawTxVec)
{
   bdvPtr_->broadcastZC(rawTxVec);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::getBlockTimeByHeight(uint32_t height, MessageId msgId) const
{
   auto callback = [this, msgId, height](
      ReturnMessage<std::vector<DBClientClasses::BlockHeader>> result)->void
   {
      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(msgId);
      try {
         auto headers = std::move(result.get());
         if (headers.size() != 1) {
            throw ClientMessageError("unexpected header count in reply", -1);
         }
         auto service = reply.initService();
         service.setGetBlockTimeByHeight(headers[0].getTimestamp());
         reply.setSuccess(true);

      } catch (const ClientMessageError& e) {
         reply.setSuccess(false);
         reply.setError(e.what());
      }

      auto serialized = serializeCapnp(message);
      this->writeToClient(serialized);
   };

   auto bc = bdvPtr_->blockchain();
   bc.getHeadersByHeight({height}, callback);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::getFeeSchedule(const std::string& strat,
   MessageId msgId) const
{
   auto callback = [this, msgId](ReturnMessage<
      std::map<uint32_t, DBClientClasses::FeeEstimateStruct>> feeResult)
   {
      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(msgId);
      try {
         auto feeMap = feeResult.get();

         auto service = reply.initService();
         auto capnFees = service.initGetFeeSchedule(feeMap.size());
         unsigned i=0;
         for (const auto& fee : feeMap) {
            auto capnFee = capnFees[i++];
            capnFee.setTarget(fee.first);
            capnFee.setFeeByte(fee.second.val_);
            capnFee.setSmartFee(fee.second.isSmart_);
         }
         reply.setSuccess(true);
      } catch (const ClientMessageError& e) {
         reply.setSuccess(false);
         reply.setError(e.what());
      }

      auto serialized = serializeCapnp(message);
      this->writeToClient(serialized);
   };

   bdvPtr_->getFeeSchedule(strat, callback);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::setCallbackHandler(ServerPushWrapper& wrapper)
{
   if (wrapper.referenceId == 0 || wrapper.handler == nullptr) {
      return;
   }

   std::unique_lock<std::mutex> lock(callbackHandlerMu_);
   auto result = callbackHandlers_.emplace(
      wrapper.referenceId, std::move(wrapper.handler));
   if (!result.second) {
      throw std::runtime_error("handler collision");
   }
}

CallbackHandler CppBridge::getCallbackHandler(uint32_t id)
{
   std::unique_lock<std::mutex> lock(callbackHandlerMu_);
   auto handlerIter = callbackHandlers_.find(id);
   if (handlerIter == callbackHandlers_.end()) {
      throw std::runtime_error("missing handler");
   }

   auto handler = std::move(handlerIter->second);
   callbackHandlers_.erase(handlerIter);
   return handler;
}

SecureBinaryData CppBridge::generateRandom(size_t count) const
{
   return fortuna_.generateRandom(count);
}

////////////////////////////////////////////////////////////////////////////////
////
////  BridgeCallback
////
////////////////////////////////////////////////////////////////////////////////
void BridgeCallback::waitOnId(const std::string& id)
{
   std::string currentId;
   while (true) {
      if (currentId == id) {
         return;
      }

      std::unique_lock<std::mutex> lock(idMutex_);
      auto iter = validIds_.find(id);
      if (*iter == id) {
         validIds_.erase(iter);
         return;
      }

      validIds_.insert(currentId);
      currentId.clear();

      //TODO: implement queue wake up logic
      currentId = std::move(idQueue_.pop_front());
   }
}

////////////////////////////////////////////////////////////////////////////////
void BridgeCallback::run(BdmNotification notif)
{
   switch (notif.action_)
   {
      case BDMAction_NewBlock:
      {
         auto height = notif.height_;
         auto lbd = [this, height](void)->void
         {
            this->notify_NewBlock(height);
         };
         wltManager_->updateStateFromDB(lbd);
         break;
      }

      case BDMAction_ZC:
      {
         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<FromBridge>();
         auto capnNotif = fromBridge.initNotification();
         capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

         auto capnZCs = capnNotif.initZeroConfs();
         ledgersToCapnp(notif.ledgers_, capnZCs);

         auto serialized = serializeCapnp(message);
         pushNotifLbd_(serialized);
         break;
      }

      case BDMAction_InvalidatedZC:
      {
         //notify zc
         break;
      }

      case BDMAction_Refresh:
      {
         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<FromBridge>();
         auto capnNotif = fromBridge.initNotification();
         capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

         auto capnRefresh = capnNotif.initRefresh(notif.ids_.size());
         for (unsigned i=0; i<notif.ids_.size(); i++) {
            auto& id = notif.ids_[i];
            capnRefresh.set(i, capnp::Text::Builder(
               id.getCharPtr(), id.getSize()
            ));
         }

         auto serialized = serializeCapnp(message);
         pushNotifLbd_(serialized);
         break;
      }

      case BDMAction_Ready:
      {
         auto height = notif.height_;
         auto lbd = [this, height](void)->void
         {
            this->notify_Ready(height);
         };

         wltManager_->updateStateFromDB(lbd);
         break;
      }

      case BDMAction_NodeStatus:
      {
         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<FromBridge>();
         auto capnNotif = fromBridge.initNotification();
         capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

         auto capnNode = capnNotif.initNodeStatus();
         nodeStatusToCapnp(notif.nodeStatus_, capnNode);

         auto serialized = serializeCapnp(message);
         pushNotifLbd_(serialized);
         break;
      }

      case BDMAction_BDV_Error:
      {
         //notify error
         LOGINFO << "bdv error:";
         LOGINFO << "  code: " << notif.error_.errCode_;
         LOGINFO << "  data: " << notif.error_.errData_.toHexStr();

         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<FromBridge>();
         auto capnNotif = fromBridge.initNotification();
         capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);
         capnNotif.setError(notif.error_.errorStr_);

         auto serialized = serializeCapnp(message);
         pushNotifLbd_(serialized);
         break;
      }

      default:
         return;
   }
}

////////////////////////////////////////////////////////////////////////////////
void BridgeCallback::progress(
   BDMPhase phase,
   const std::vector<std::string> &walletIdVec,
   float progress, unsigned secondsRem,
   unsigned progressNumeric)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   auto capnProgress = capnNotif.initProgress();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_PROGRESS);

   capnProgress.setPhase((uint32_t)phase);
   capnProgress.setProgress(progress);
   capnProgress.setTime(secondsRem);
   capnProgress.setNumericProgress(progressNumeric);

   if (!walletIdVec.empty()) {
      auto capnIds = capnProgress.initIds(walletIdVec.size());
      for (unsigned i=0; i<walletIdVec.size(); i++) {
         capnIds.set(i, walletIdVec[i]);
      }
   }

   auto serialized = serializeCapnp(message);
   pushNotifLbd_(serialized);
}

////////////////////////////////////////////////////////////////////////////////
void BridgeCallback::notify_SetupDone()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setSetupDone();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

   auto serialized = serializeCapnp(message);
   pushNotifLbd_(serialized);
}

////////////////////////////////////////////////////////////////////////////////
void BridgeCallback::notify_SetupRegistrationDone(
   const std::set<std::string>& ids)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

   auto capnIds = capnNotif.initRegisterDone(ids.size());
   unsigned i=0;
   for (const auto& id : ids) {
      capnIds.set(i++, id);
   }

   auto serialized = serializeCapnp(message);
   pushNotifLbd_(serialized);
}

////////////////////////////////////////////////////////////////////////////////
void BridgeCallback::notify_RegistrationDone(const std::set<std::string>& ids)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

   auto capnIds = capnNotif.initRefresh(ids.size());
   unsigned i=0;
   for (const auto& id : ids) {
      capnIds.set(i++, id);
   }

   auto serialized = serializeCapnp(message);
   pushNotifLbd_(serialized);
}

////////////////////////////////////////////////////////////////////////////////
void BridgeCallback::notify_NewBlock(unsigned height)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setNewBlock(height);
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

   auto serialized = serializeCapnp(message);
   pushNotifLbd_(serialized);
}

////////////////////////////////////////////////////////////////////////////////
void BridgeCallback::notify_Ready(unsigned height)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setReady(height);
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

   auto serialized = serializeCapnp(message);
   pushNotifLbd_(serialized);
}

////////////////////////////////////////////////////////////////////////////////
void BridgeCallback::disconnected()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setDisconnected();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

   auto serialized = serializeCapnp(message);
   pushNotifLbd_(serialized);
}

////////////////////////////////////////////////////////////////////////////////
////
////  CppBridgeSignerStruct
////
////////////////////////////////////////////////////////////////////////////////
CppBridgeSignerStruct::CppBridgeSignerStruct(
   std::function<WalletPtr(const std::string&)> getWalletFunc,
   std::function<void(ServerPushWrapper)> writeFunc) :
   getWalletFunc_(getWalletFunc), writeFunc_(writeFunc)
{}

////////////////////////////////////////////////////////////////////////////////
void CppBridgeSignerStruct::signTx(const std::string& wltId,
   const std::string& callbackId, MessageId referenceId)
{
   //grab wallet
   auto wltPtr = getWalletFunc_(wltId);

   //run signature process in its own thread, as it's an async process
   auto signLbd = [this, wltPtr, callbackId, referenceId](void)->void
   {
      bool success = true;

      //create passphrase lambda
      auto passPromptObj = std::make_shared<BridgePassphrasePrompt>(
         callbackId, writeFunc_);
      auto passLbd = passPromptObj->getLambda();

      try {
         //cast wallet & create resolver
         auto wltSingle = std::dynamic_pointer_cast<
            Wallets::AssetWallet_Single>(wltPtr);
         auto feed = std::make_shared<
            Signing::ResolverFeed_AssetWalletSingle>(wltSingle);

         //set resolver
         signer_.resetFeed();
         signer_.setFeed(feed);

         //create & set passphrase lambda
         wltPtr->setPassphrasePromptLambda(passLbd);

         //lock decryption container
         auto lock = wltPtr->lockDecryptedContainer();

         //sign, this will prompt the passphrase lambda on demand
         signer_.sign();
      } catch (const std::exception&) {
         success = false;
      }
      catch (...) {
         LOGINFO << "false catch";
      }

      //send reply to caller
      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setSuccess(true);
      reply.setReferenceId(referenceId);

      ServerPushWrapper wrapper{ 0, nullptr, serializeCapnp(message) };
      writeFunc_(std::move(wrapper));

      //wind down passphrase prompt
      passPromptObj->cleanup();
   };

   std::thread thr(signLbd);
   if (thr.joinable()) {
      thr.detach();
   }
}

////////////////////////////////////////////////////////////////////////////////
bool CppBridgeSignerStruct::resolve(const std::string& wltId)
{
   //grab wallet
   auto wltPtr = getWalletFunc_(wltId);

   //get wallet feed
   auto wltSingle = std::dynamic_pointer_cast<Wallets::AssetWallet_Single>(wltPtr);
   auto feed = std::make_shared<Signing::ResolverFeed_AssetWalletSingle>(wltSingle);

   //set feed & resolve
   signer_.resetFeed();
   signer_.setFeed(feed);
   signer_.resolvePublicData();

   return true;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridgeSignerStruct::getSignedStateForInput(
   unsigned inputId, MessageId referenceId)
{
   if (signState_ == nullptr) {
      signState_ = std::make_unique<Signing::TxEvalState>(
         signer_.evaluateSignedState());
   }

   const auto signState = signState_.get();
   auto signStateInput = signState->getSignedStateForInput(inputId);

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   auto signerReply = reply.initSigner();
   auto signedState = signerReply.initGetSignedStateForInput();

   signedState.setIsValid(signStateInput.isValid());
   signedState.setMCount(signStateInput.getM());
   signedState.setNCount(signStateInput.getN());
   signedState.setSigCount(signStateInput.getSigCount());

   const auto& pubKeyMap = signStateInput.getPubKeyMap();
   auto sigs = signedState.initSignStates(pubKeyMap.size());
   unsigned i=0;
   for (const auto& pubkey : pubKeyMap) {
      auto sig = sigs[i++];
      sig.setPubKey(capnp::Data::Builder(
         (uint8_t*)pubkey.first.getPtr(), pubkey.first.getSize()
      ));
      sig.setHasSig(pubkey.second);
   }

   reply.setSuccess(true);
   reply.setReferenceId(referenceId);
   return serializeCapnp(message);
}
