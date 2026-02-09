////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2025, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "CppBridge.h"
#include "BridgeSocket.h"
#include "../SocketIncludes.h"
#include "./Wallets/Manager.h"

#include <Utils/ArmoryConfig.h>
#include <Utils/BtcUtils.h>
#include <Utils/DBUtils.h>
#include <Signer/Signer.h>
#include <Signer/ResolverFeed_Wallets.h>

#include <Wallets/Seeds/Backups.h>
#include <Wallets/IOHeader.h>
#include <Wallets/WalletIdTypes.h>
#include <Wallets/KDF.h>
#include <Wallets/Wallets.h>
#include <Wallets/AuthorizedPeers.h>
#include <Wallets/Addresses.h>
#include <Wallets/Accounts/AccountTypes.h>
#include <Wallets/Accounts/AddressAccounts.h>

#include "BlockchainDbClient.h"
#include "PassphrasePrompt.h"
#include "AsyncClient.h"
#include "CoinSelection.h"
#include "TerminalPassphrasePrompt.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/Bridge.capnp.h"
#include "capnp/Types.capnp.h"

using namespace Armory;
using namespace Armory::Codec::Bridge;
using namespace Armory::Codec::Types;
using namespace Armory::Bridge;
using namespace std::chrono_literals;

enum CppBridgeState
{
   CppBridge_Ready = 20,
   CppBridge_Registered
};

#define PROTO_ASSETID_PREFIX 0xAFu

namespace
{
   Cryptography::PRNG::Fortuna fortuna;

   ////
   BinaryData serializeCapnp(capnp::MallocMessageBuilder& msg)
   {
      auto flat = capnp::messageToFlatArray(msg);
      auto bytes = flat.asBytes();
      return BinaryData(bytes.begin(), bytes.end());
   }

   void addressToCapnp(WalletData::AddressData::Builder& capnAddress,
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

      //precursor, if any
      if (addrNested == nullptr) {
         return;
      }

      const auto& precursor = addrNested->getPredecessor()->getScript();
      capnAddress.setPrecursorScript(capnp::Data::Builder(
         (uint8_t*)precursor.getPtr(), precursor.getSize()
      ));
   }

   void walletToCapnp(std::shared_ptr<Wallets::AssetWallet> wallet,
      const Wallets::AddressAccountId& accId, const std::string& dbId,
      WalletData::Builder& capnWallet)
   {
      /* header */
      //ids
      capnWallet.setMasterId(wallet->getMasterID());
      capnWallet.setWalletId(wallet->getID());
      capnWallet.setAccountId(accId.toHexStr());
      capnWallet.setDbId(dbId);

      //path
      capnWallet.setPath(wallet->getDbFilename().string());

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
      capnWallet.setLookupCount(assetAccountPtr->getLastComputedIndex() + 1);
      capnWallet.setUseCount(assetAccountPtr->getHighestUsedIndex());

      //address map
      auto addrMap = accPtr->getUsedAddressMap();
      auto capnAddresses = capnWallet.initAddressData(addrMap.size());
      i=0;
      for (const auto& addrPair : addrMap) {
         auto capnAddress = capnAddresses[i++];
         addressToCapnp(capnAddress,
            addrPair.second, accPtr,
            wallet->getDefaultEncryptionKeyId()
         );
      }

      /* encryption info */
      bool usesEncryption = wallet->isMasterRecordEncrypted();
      capnWallet.setUsesEncryption(usesEncryption);
      if (usesEncryption) {
         auto kdfPtr = wallet->getDefaultKdf();
         auto kdfRomix = std::dynamic_pointer_cast<
            Wallets::Encryption::KeyDerivationFunction_Romix>(kdfPtr);
         if (kdfRomix != nullptr) {
            uint32_t kdfMem = kdfRomix->memTarget() / 1024 / 1024;
            capnWallet.setKdfMemReq(kdfMem);
         }
      }

      /* comments */
      const auto& commentsMap = wallet->getCommentMap();
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

   Wallets::Progress::Func getWalletProgressLbd(
      CppBridge* bridgePtr, const std::string& callbackId)
   {
      return [bridgePtr, callbackId](
         std::unique_ptr<Wallets::Progress::State> statePtr)
      {
         capnp::MallocMessageBuilder notifMessage;
         auto fromBridge = notifMessage.initRoot<FromBridge>();
         auto notif = fromBridge.initNotification();
         notif.setCallbackId(callbackId);
         auto prog = notif.initWalletProgress();

         switch (statePtr->type())
         {
            case Wallets::Progress::StateEnum::CreateWalletFile:
            {
               auto stateCf = dynamic_cast<Wallets::Progress::CreateWalletFile*>(
                  statePtr.get());
               if (stateCf != nullptr) {
                  prog.setCreateFile(stateCf->path().filename().string());
               }
               break;
            }

            case Wallets::Progress::StateEnum::InitWalletFile:
            {
               auto stateInit = dynamic_cast<Wallets::Progress::InitWalletFile*>(
                  statePtr.get());
               if (stateInit != nullptr) {
                  prog.setInitFile(stateInit->masterId());
               }
               break;
            }

            case Wallets::Progress::StateEnum::ReadWalletFile:
            {
               auto stateRead = dynamic_cast<Wallets::Progress::ReadWalletFile*>(
                  statePtr.get());
               if (stateRead != nullptr) {
                  prog.setReadFile(stateRead->masterId());
               }
               break;
            }

            case Wallets::Progress::StateEnum::CreateAccount:
            {
               auto stateAcc = dynamic_cast<Wallets::Progress::CreateAccount*>(
                  statePtr.get());
               if (stateAcc != nullptr) {
                  prog.setCreateAccount(stateAcc->accPtr()->name());
               }
               break;
            }

            case Wallets::Progress::StateEnum::ExtendChain:
            {
               auto stateExt = dynamic_cast<Wallets::Progress::ExtendChain*>(
                  statePtr.get());
               if (stateExt != nullptr) {
                  auto extNotif = prog.initExtendChain();
                  extNotif.setTotal(stateExt->lookup());
               }
               break;
            }
         }

         auto notifSerialized = serializeCapnp(notifMessage);
         bridgePtr->writeToClient(notifSerialized);
      };
   }

   void sendSuccess(CppBridge* bridgePtr, MessageId refId,
      bool success=true, const std::string& error={})
   {
      capnp::MallocMessageBuilder message;
      auto reply = message.initRoot<FromBridge>().initReply();
      reply.setReferenceId(refId);
      reply.setSuccess(success);
      if (!success) {
         reply.setError(error);
      }
      auto serialized = serializeCapnp(message);
      bridgePtr->writeToClient(serialized);
   }

   void sendCallbackCleanup(CppBridge* bridgePtr, const std::string& callbackId)
   {
      capnp::MallocMessageBuilder message;
      auto notif = message.initRoot<FromBridge>().initNotification();
      notif.setCallbackId(callbackId);
      notif.setCleanup();
      auto serialized = serializeCapnp(message);
      bridgePtr->writeToClient(serialized);
   }

   WalletBackup::Type toCapnBackupType(const Seeds::BackupType& bType)
   {
      switch (bType)
      {
         case Seeds::BackupType::Armory135a:
            return WalletBackup::Type::LEGACY135_A;

         case Seeds::BackupType::Armory135c:
            return WalletBackup::Type::LEGACY135_C;

         case Seeds::BackupType::Armory200a:
            return WalletBackup::Type::LEGACY200_A;

         case Seeds::BackupType::Armory200b:
            return WalletBackup::Type::LEGACY200_B;

         case Seeds::BackupType::Armory200c:
            return WalletBackup::Type::LEGACY200_C;

         case Seeds::BackupType::Armory200d:
            return WalletBackup::Type::LEGACY200_D;

         case Seeds::BackupType::BIP39:
            return WalletBackup::Type::BIP39;

         default:
            return WalletBackup::Type::UNKNOWN;
      }
   }

   std::function<std::unique_ptr<Passphrase::Params>(void)> getSetPassFunc(
      CppBridge* bridgePtr, const std::string& callbackId, bool priv)
   {
      return [bridgePtr, callbackId, priv]()->std::unique_ptr<Passphrase::Params>
      {
         auto counterBd = bridgePtr->generateRandom(4);
         auto notifCounter = *(uint32_t*)counterBd.getPtr();

         //create set passphrase notif
         capnp::MallocMessageBuilder notifMessage;
         auto fromBridge = notifMessage.initRoot<FromBridge>();
         auto notif = fromBridge.initNotification();
         notif.setCallbackId(callbackId);
         notif.setCounter(notifCounter);
         auto wltCrt = notif.initSetPassphrase();
         if (priv) {
            wltCrt.setPrivatePass();
         } else {
            wltCrt.setControlPass();
         }
         auto notifSerialized = serializeCapnp(notifMessage);

         //reply handler
         auto prom = std::make_shared<std::promise<Seeds::PromptReply>>();
         auto fut = prom->get_future();
         auto replyLbd = [prom](const Seeds::PromptReply& reply)->bool
         {
            prom->set_value(reply);
            return true;
         };

         //push prompt to caller
         ServerPushWrapper wrapper{notifCounter, replyLbd, std::move(notifSerialized)};
         bridgePtr->callbackWriter(wrapper);

         //wait on reply
         auto reply = std::move(fut.get());
         if (!reply.success) {
            return std::make_unique<Passphrase::Params>();
         }
         return std::make_unique<Passphrase::Params>(reply.passParams);
      };
   }
}

////////////////////////////////////////////////////////////////////////////////
////
////  CppBridge
////
////////////////////////////////////////////////////////////////////////////////
CppBridge::CppBridge() :
   path_(Config::getDataDir()),
   dbOffline_(Config::NetworkSettings::isOffline())
{
   wltManager_ = std::make_shared<WalletManager>(path_);
   wltManager_->setupBdvCallback([this](BinaryData& data)
      { writeToClient(data); }
   );
}

void CppBridge::setWriteLambda(
   const std::function<void(std::unique_ptr<WritePayload_Bridge>)>& lbd)
{
   writeLambda_ = lbd;
}

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
}

bool CppBridge::isOffline() const
{
   if (dbOffline_) {
      return true;
   }

   if (bdvPtr_ == nullptr) {
      return true;
   }
   return !bdvPtr_->isValid();
}

const std::filesystem::path& CppBridge::getDataDir() const
{
   return path_;
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
// WalletManager init methods
////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::listWallets(MessageId msgId)
{
   auto wltList = wltManager_->listWallets();

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   auto mgrReply = reply.initWalletManager();
   auto capnWltList = mgrReply.initListWallets(wltList.size());

   unsigned i=0;
   for (const auto& wltObj : wltList) {
      auto capnWltObj = capnWltList[i++];
      capnWltObj.setState(WalletManagerReply::WalletLoadState(
         (int)wltObj.second->state()));
      capnWltObj.setPath(wltObj.first);
      capnWltObj.setStaged(wltObj.second->staged());
      capnWltObj.setWatchingOnly(wltObj.second->isWatchingOnly());

      try {
         const auto& walletId = wltObj.second->walletId();
         if (!walletId.empty()) {
            capnWltObj.setWalletId(walletId);
         }
      } catch (const std::exception&) {
         capnWltObj.setWalletId("N/A");
      }

      if (wltObj.second->state() == WalletLoadState::Legacy) {
         auto a135Info = std::dynamic_pointer_cast<A135FileInfo>(wltObj.second);
         auto extendedData = capnWltObj.initLegacy();
         extendedData.setWalletId(a135Info->walletId());
         extendedData.setLabel(a135Info->name());
         extendedData.setDescription(a135Info->description());
         extendedData.setHighestUsedIndex(a135Info->highestUsedIndex());
         extendedData.setAddressCount(a135Info->addressCount());
         extendedData.setTimestamp(a135Info->timestamp());
         extendedData.setWatchingOnly(a135Info->isWatchingOnly());
         extendedData.setEncrypted(a135Info->isEncrypted());
         extendedData.setSeedVersion(a135Info->version());
         extendedData.setKdfMem(a135Info->kdfMem());
         extendedData.setLegacy();
      }
   }

   reply.setReferenceId(msgId);
   reply.setSuccess(true);
   return serializeCapnp(message);
}

////////
void CppBridge::unlockControlHeader(const std::string& path,
   const std::string& callbackId, MessageId refId)
{
   auto thrLbd = [this, callbackId, path, refId](void)->void
   {
      auto passPromptObj = std::make_shared<BridgePassphrasePrompt>(
         callbackId, [this](ServerPushWrapper wrapper) {
            this->callbackWriter(wrapper);
      });
      auto lbd = passPromptObj->getLambda();

      auto notifySuccess = [refId, this](bool success, const std::string& error)
      {
         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<FromBridge>();
         auto reply = fromBridge.initReply();
         reply.setReferenceId(refId);
         reply.setSuccess(success);
         if (!success) {
            reply.setError(error);
         }

         auto response = serializeCapnp(message);
         this->writeToClient(response);
      };

      try {
         wltManager_->unlockControlHeader(path, lbd);
         notifySuccess(true, {});
      } catch (const std::exception& e) {
         notifySuccess(false, e.what());
      }

      //call lbd with empty id set to cleanup passphrase prompt
      lbd({});
   };

   std::thread thr(thrLbd);
   if (thr.joinable()) {
      thr.detach();
   }
}

////////
bool CppBridge::stageWallet(const Wallets::WalletId& walletId, bool stage)
{
   return wltManager_->stageWallet(walletId, stage);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::migrateWallet(const std::filesystem::path& wltPath,
   const std::string& callbackId, MessageId refId)
{
   auto migrateLbd = [this] (const std::filesystem::path wltPath,
      const std::string callbackId, MessageId refId)
   {
      //prepare reply
      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(refId);

      //set passphrase functions
      auto setCtrlPassFunc = getSetPassFunc(this, callbackId, false);
      auto setPrivPassFunc = getSetPassFunc(this, callbackId, true);

      //creation params for the wallet receiving the migration
      Wallets::IO::CreateWalletParams params{
         wltManager_->getWalletDir(),
         Passphrase::SetNew{setPrivPassFunc},
         Passphrase::SetNew{setCtrlPassFunc},
         getWalletProgressLbd(this, callbackId), 0
      };

      //function to unlock private keys in the origin wallet
      auto passPromptObj = std::make_shared<BridgePassphrasePrompt>(
         callbackId, [this](ServerPushWrapper wrapper) {
            this->callbackWriter(wrapper);
      });
      auto unlockLbd = passPromptObj->getLambda();

      try {
         auto resultId = wltManager_->migrateWallet(
            wltPath, unlockLbd, params);

         auto mgrReply = reply.initWalletManager();
         mgrReply.setMigrateWallet(resultId);
         reply.setSuccess(true);
      } catch (const std::exception& e) {
         reply.setSuccess(false);
         reply.setError(e.what());
      }

      sendCallbackCleanup(this, callbackId);
      auto response = serializeCapnp(message);
      this->writeToClient(response);
   };

   std::thread thr(migrateLbd, wltPath, callbackId, refId);
   if (thr.joinable()) {
      thr.detach();
   }
}

void CppBridge::forkWatchingOnly(const Wallets::WalletId& wltId,
   const std::string& callbackId, MessageId refId)
{
   auto forkLbd = [this](const Wallets::WalletId wltId,
      const std::string callbackId, MessageId refId)
   {
      //prepare reply
      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(refId);

      auto wltCont = wltManager_->getWalletContainer(wltId);
      try {
         auto ctrlPassFunc = getSetPassFunc(this, callbackId, false);
         auto woPath = wltCont->forkWatchingOnly(
            Passphrase::SetNew{ctrlPassFunc});

         auto wltReply = reply.initWallet();
         wltReply.setForkWatchingOnly(woPath.string());
         reply.setSuccess(true);
      } catch (const std::exception& e) {
         reply.setSuccess(false);
         reply.setError(e.what());
      }

      sendCallbackCleanup(this, callbackId);
      auto response = serializeCapnp(message);
      this->writeToClient(response);
   };

   std::thread thr(forkLbd, wltId, callbackId, refId);
   if (thr.joinable()) {
      thr.detach();
   }
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::loadWallets(MessageId msgId)
{
   wltManager_->loadWallets();
   return createWalletsPacket(msgId);
}

////////////////////////////////////////////////////////////////////////////////
WalletPtr CppBridge::getWalletPtr(const Wallets::WalletId& wltId) const
{
   auto wltContainer = wltManager_->getWalletContainer(wltId);
   return wltContainer->getWalletPtr();
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::createWalletsPacket(MessageId msgId)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   auto mgrReply = reply.initWalletManager();

   //grab wallet map
   auto accountIdMap = wltManager_->getAccountIdMap();
   size_t count = 0;
   for (const auto& idIt : accountIdMap) {
      if (idIt.first.empty() || idIt.second.empty()) {
         continue;
      }
      count += idIt.second.size();
   }
   auto wltPackets = mgrReply.initLoadWallets(count);

   unsigned i=0;
   for (const auto& idIt : accountIdMap) {
      if (idIt.first.empty() || idIt.second.empty()) {
         continue;
      }

      for (const auto& accId : idIt.second) {
         auto capnWallet = wltPackets[i++];
         auto firstCont = wltManager_->getWalletContainer(idIt.first, accId);
         auto wltPtr = firstCont->getWalletPtr();
         walletToCapnp(wltPtr, accId, firstCont->getDbId(), capnWallet);
      }
   }

   reply.setReferenceId(msgId);
   reply.setSuccess(true);
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
bool CppBridge::unloadWallet(const Wallets::WalletId& wltId)
{
   try {
      wltManager_->deleteWallet(wltId);
   } catch (const std::exception& e) {
      LOGWARN << "failed to unload wallet with error: " << e.what();
      return false;
   }
   return true;
}

////
bool CppBridge::deleteWallet(const Wallets::WalletId& wltId)
{
   try {
      wltManager_->deleteWallet(wltId);
   } catch (const std::exception& e) {
      LOGWARN << "failed to delete wallet with error: " << e.what();
      return false;
   }
   return true;
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::setupDB(MessageId refId)
{
   /*
   * should call this method from a dedicated thread
   */
   LOGINFO << "connecting to ArmoryDB";

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(refId);

   if (dbOffline_) {
      LOGWARN << "attempt to connect to DB in offline mode, ignoring";
      reply.setError("cannot setup db offline mode");
      reply.setSuccess(false);
      auto response = serializeCapnp(message);
      this->writeToClient(response);
      return;
   }

   if (bdvPtr_ != nullptr) {
      LOGWARN << "already connected to db!";
      reply.setError("already connected to db!");
      reply.setSuccess(false);
      auto response = serializeCapnp(message);
      this->writeToClient(response);
      return;
   }

   if (wltManager_ == nullptr) {
      LOGERR << "null wallet manager!";
      reply.setError("null wallet manager!");
      reply.setSuccess(false);
      auto response = serializeCapnp(message);
      this->writeToClient(response);
      return;
   }

   //do we have to spawn the db?
   std::shared_ptr<Wallets::AuthorizedPeers> peers = nullptr;
   if (Config::NetworkSettings::automateDb()) {
      peers = spawnDb();
   } else {
      try {
         peers = std::make_shared<Wallets::AuthorizedPeers>(
            Wallets::IO::ReadOnlyFileParams{
               path_ / CLIENT_AUTH_PEER_FILENAME,
               TerminalPassphrasePrompt::getLambda("db identification key")
         });
      } catch (const Wallets::PeerFileMissing&) {
         //NOTE (SHORT TERM SOLUTION): auto generation of auth peer db
         Wallets::IO::CreateFileParams parms{
            path_ / CLIENT_AUTH_PEER_FILENAME, {}};
         peers = Wallets::AuthorizedPeers::createWallet(
            Wallets::IO::CreateFileParams{
               path_ / CLIENT_AUTH_PEER_FILENAME, {}
         });
      }
   }

   //connect to db
   try {
      if (peers == nullptr) {
         throw std::runtime_error("failed to setup client peers db");
      }

      bdvPtr_ = setupClientConnection(peers, wltManager_);
      if (bdvPtr_ == nullptr) {
         throw std::runtime_error("failed to instantiate bdv object");
      }

      //reply to caller
      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setSuccess(false);
      reply.setError(e.what());
   }

   auto response = serializeCapnp(message);
   this->writeToClient(response);
}

////
void CppBridge::cleanupDb(MessageId refId)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(refId);

   if (!Config::NetworkSettings::automateDb()) {
      reply.setSuccess(false);
      reply.setError("db is not automated");
   } else if (bdvPtr_ == nullptr) {
      reply.setSuccess(false);
      reply.setError("no connection to db");
   } else if (!isDbRunning()) {
      reply.setSuccess(false);
      reply.setError("db is not running");
   } else {
      bdvPtr_->shutdown();
      reply.setSuccess(true);
   }

   auto response = serializeCapnp(message);
   this->writeToClient(response);
}

////
void CppBridge::goOnline()
{
   bdvPtr_->goOnline();
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::registerWallets()
{
   wltManager_->registerWallets();
}

////
void CppBridge::registerWallet(const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId, bool isNew)
{
   if (isOffline()) {
      LOGDEBUG << "Armory is offline, cannot register wallet";
      return;
   }

   try {
      wltManager_->registerWallet(wltId, accId, isNew);
   } catch (const std::exception& e) {
      LOGERR << "failed to register wallet with error: " << e.what();
   }
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::createBackupStringForWallet(const Wallets::WalletId& wltId,
   bool isPriv, const std::string& callbackId, MessageId msgId)
{
   auto func = [this, wltId, isPriv, callbackId, msgId]()
   {
      std::unique_ptr<Seeds::WalletBackup> backupData;
      std::string error;
      try {
         //grab wallet
         auto wltContainer = wltManager_->getWalletContainer(wltId);

         //grab the backup
         if (isPriv) {
            //setup passphrase prompt
            auto passPromptObj = std::make_shared<BridgePassphrasePrompt>(
               callbackId, [this](ServerPushWrapper wrapper){
                  this->callbackWriter(wrapper);
               });
            auto lbd = passPromptObj->getLambda();

            //generate private root backup
            backupData = std::move(wltContainer->getBackupStrings(
               true, lbd));

            //cleanup
            passPromptObj->cleanup();
         } else {
            //nothing to setup when generating public root backups
            backupData = std::move(wltContainer->getBackupStrings(
               false, nullptr));
         }
      } catch (const std::exception& e) {
         error = e.what();
         backupData = nullptr;
      }

      //prepare reply
      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(msgId);

      if (backupData == nullptr) {
         //return on error
         reply.setSuccess(false);
         reply.setError(error);
         auto payload = serializeCapnp(message);
         writeToClient(payload);
         return;
      }

      auto walletReply = reply.initWallet();
      auto backupStringCapnp = walletReply.initCreateBackupString();
      auto backupE16 = dynamic_cast<Seeds::Backup_Easy16*>(
         backupData.get());
      if (backupE16 != nullptr) {
         //secure print passphrase
         auto spPass = backupE16->getSpPass();
         backupStringCapnp.setSpPass(
            capnp::Text::Reader(spPass.data(), spPass.size()));

         {
            //cleartext root
            auto line1 = backupE16->getRoot(Seeds::LineIndex::One, false);
            auto line2 = backupE16->getRoot(Seeds::LineIndex::Two, false);
            auto clearLines = backupStringCapnp.initRootClear(2);
            clearLines.set(0, capnp::Text::Reader(line1.data(), line1.size()));
            clearLines.set(1, capnp::Text::Reader(line2.data(), line2.size()));

            //encrypted root
            auto line3 = backupE16->getRoot(Seeds::LineIndex::One, true);
            auto line4 = backupE16->getRoot(Seeds::LineIndex::Two, true);
            auto encrLines = backupStringCapnp.initRootEncr(2);
            encrLines.set(0, capnp::Text::Reader(line3.data(), line3.size()));
            encrLines.set(1, capnp::Text::Reader(line4.data(), line4.size()));
         }

         if (backupE16->hasChaincode()) {
            //cleartext chaincode
            auto line1 = backupE16->getChaincode(Seeds::LineIndex::One, false);
            auto line2 = backupE16->getChaincode(Seeds::LineIndex::Two, false);
            auto clearLines = backupStringCapnp.initChainClear(2);
            clearLines.set(0, capnp::Text::Reader(line1.data(), line1.size()));
            clearLines.set(1, capnp::Text::Reader(line2.data(), line2.size()));

            //encrypted chaincode
            auto line3 = backupE16->getChaincode(Seeds::LineIndex::One, true);
            auto line4 = backupE16->getChaincode(Seeds::LineIndex::Two, true);
            auto encrLines = backupStringCapnp.initChainEncr(2);
            encrLines.set(0, capnp::Text::Reader(line3.data(), line3.size()));
            encrLines.set(1, capnp::Text::Reader(line4.data(), line4.size()));
         }
      } else {
         auto backupE16Public = dynamic_cast<Seeds::Backup_Easy16Public*>(
            backupData.get());
         if (backupE16Public == nullptr) {
            reply.setSuccess(false);
            reply.setError("invalid backup type!");
            auto payload = serializeCapnp(message);
            writeToClient(payload);
            return;
         }

         //pubroot
         auto line1 = backupE16Public->getPublicRoot(Seeds::LineIndex::One);
         auto line2 = backupE16Public->getPublicRoot(Seeds::LineIndex::Two);
         auto rootLines = backupStringCapnp.initRootClear(2);
         rootLines.set(0, capnp::Text::Reader(line1.data(), line1.size()));
         rootLines.set(1, capnp::Text::Reader(line2.data(), line2.size()));

         //chaincode
         auto line3 = backupE16Public->getChaincode(Seeds::LineIndex::One);
         auto line4 = backupE16Public->getChaincode(Seeds::LineIndex::Two);
         auto ccLines = backupStringCapnp.initChainClear(2);
         ccLines.set(0, capnp::Text::Reader(line3.data(), line3.size()));
         ccLines.set(1, capnp::Text::Reader(line4.data(), line4.size()));

         //backupId
         auto backupId = backupE16Public->getBackupId();
         backupStringCapnp.setBackupId(capnp::Text::Reader(
            backupId.data(), backupId.size()));
      }

      //backup type
      backupStringCapnp.setBackupType(toCapnBackupType(backupData->type()));

      reply.setSuccess(true);
      auto payload = serializeCapnp(message);
      writeToClient(payload);
   };

   std::thread thr(func);
   if (thr.joinable()) {
      thr.detach();
   }
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::changeWalletPassphrase(const Wallets::WalletId& wltId,
   const std::string& callbackId, bool isPriv, MessageId msgId)
{
   auto func = [this, wltId, callbackId, isPriv, msgId]()
   {
      std::shared_ptr<BridgePassphrasePrompt> unlockObj;
      unlockObj = std::make_shared<BridgePassphrasePrompt>(
         callbackId, [this](ServerPushWrapper wrapper){
            this->callbackWriter(wrapper);
         });
      Armory::Passphrase::SetNew setNewFunc = getSetPassFunc(
         this, callbackId, isPriv);

      try {
         //grab wallet
         auto wltContainer = wltManager_->getWalletContainer(wltId);

         //trigger passphrase change
         auto unlockFunc = unlockObj->getLambda();
         wltContainer->changePassphrase(unlockFunc, setNewFunc, isPriv);
         sendSuccess(this, msgId);
      } catch (const std::exception& e) {
         sendSuccess(this, msgId, false, e.what());
      }

      //tell caller to cleanup the callback id
      sendCallbackCleanup(this, callbackId);
   };

   std::thread thr(func);
   if (thr.joinable()) {
      thr.detach();
   }
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::restoreWallet(
   const std::vector<std::string_view>& lines_sv,
   const std::string_view& spPass_sv,
   const std::string_view& callbackId, MessageId refId)
{
   //NOTE: easy16 only for now, will need a dedicated call for BIP39

   /*
   Needs 2 lines for the root, possibly another 2 for the chaincode, possibly
   1 more for the SecurePrint passphrase.

   This call will block waiting on user replies to the prompt for the different
   steps in the wallet restoration process (checking id, checkums, passphrase
   requests). It has to run in its own thread.
   */

   std::unique_ptr<Seeds::WalletBackup> backup;
   bool isWO = lines_sv.size() == 5;
   if (isWO) {
      backup = Seeds::Backup_Easy16Public::fromLines(lines_sv);
   } else {
      backup = Seeds::Backup_Easy16::fromLines(lines_sv, spPass_sv);
   }

   //
   auto restoreLbd = [
      this, refId, callbackId=std::string{callbackId}, isWO](
      std::unique_ptr<Seeds::WalletBackup> backup)
   {
      auto createCallbackMessage = [callbackId](
         const Seeds::RestorePrompt& prompt, uint32_t notifCounter)->BinaryData
      {
         capnp::MallocMessageBuilder message;
         auto promptCapnp = message.initRoot<FromBridge>();
         auto notifCapnp = promptCapnp.initNotification();
         notifCapnp.setCallbackId(callbackId);
         notifCapnp.setCounter(notifCounter);

         switch (prompt.promptType)
         {
            case Seeds::RestorePromptType::FormatError:
            case Seeds::RestorePromptType::Failure:
            {
               auto restore = notifCapnp.initRestore();
               restore.setFailure(prompt.error);
               break;
            }

            case Seeds::RestorePromptType::ChecksumError:
            {
               auto restore = notifCapnp.initRestore();
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
               auto restore = notifCapnp.initRestore();
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
               auto restore = notifCapnp.initRestore();
               restore.setDecryptError();
               break;
            }

            case Seeds::RestorePromptType::ControlPassphrase:
            {
               auto wltCreation = notifCapnp.initSetPassphrase();
               wltCreation.setControlPass();
               break;
            }

            case Seeds::RestorePromptType::PrivatePassphrase:
            {
               auto wltCreation = notifCapnp.initSetPassphrase();
               wltCreation.setPrivatePass();
               break;
            }

            case Seeds::RestorePromptType::Id:
            {
               auto restore = notifCapnp.initRestore();
               auto metaCapnp = restore.initCheckWalletId();
               metaCapnp.setWalletId(prompt.walletId);
               metaCapnp.setBackupType(toCapnBackupType(prompt.backupType));
               break;
            }

            case Seeds::RestorePromptType::TypeError:
            {
               auto restore = notifCapnp.initRestore();
               restore.setTypeError(prompt.error);
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
            auto counterBd = fortuna.generateRandom(4);
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

      auto setNewCtrlFunc = [callback]()->std::unique_ptr<Passphrase::Params>
      {
         auto reply = callback(Seeds::RestorePrompt{
            Seeds::RestorePromptType::ControlPassphrase});
         if (!reply.success) {
            return std::make_unique<Passphrase::Params>();
         }
         return std::make_unique<Passphrase::Params>(reply.passParams);
      };

      auto setNewPrivFunc = [callback]()->std::unique_ptr<Passphrase::Params>
      {
         auto reply = callback(Seeds::RestorePrompt{
            Seeds::RestorePromptType::PrivatePassphrase});
         if (!reply.success) {
            return std::make_unique<Passphrase::Params>();
         }
         return std::make_unique<Passphrase::Params>(reply.passParams);
      };

      auto tempDir = wltManager_->getWalletDir() / "temp";
      try {
         //create a temp folder where the wallet will be generated
         FileUtils::createDirectory(tempDir);

         //create wallet from backup
         auto progFunc = getWalletProgressLbd(this, callbackId);
         Wallets::IO::CreateWalletParams params{
            tempDir,
            Passphrase::SetNew{setNewPrivFunc},
            Passphrase::SetNew{setNewCtrlFunc},
            progFunc,
            0 //address lookup
         };

         auto restoreResult = Armory::Seeds::Helpers::restoreFromBackup(
            std::move(backup), callback, params);

         if (restoreResult.wltPtr == nullptr) {
            throw std::runtime_error("empty wallet");
         }

         //get new wallet path
         auto newWltID = restoreResult.wltPtr->getID();
         auto newWltPath = restoreResult.wltPtr->getDbFilename();

         //are we merging this wallet or overwriting an existing one?
         std::filesystem::path oldWltPath;
         if (restoreResult.merge && wltManager_->hasWallet(newWltID)) {
            //we want to merge the old wallet data in the new one,
            //close the new file first
            restoreResult.wltPtr.reset();

            //grab the old wallet
            auto oldWlt = wltManager_->getWalletContainer(newWltID);
            auto oldWltSingle = std::dynamic_pointer_cast<Wallets::AssetWallet_Single>(
               oldWlt->getWalletPtr());

            //merge old into new one
            if (oldWltSingle == nullptr) {
               LOGWARN << "replaced wallet is not single, merging not implemented yet!";
            } else {
               auto oldWltData = Wallets::AssetWallet_Single::exportPublicData(
                  oldWltSingle);
               Wallets::AssetWallet_Single::mergePublicData(
                  Wallets::IO::ReadOnlyFileParams{
                     newWltPath, params.setCtrlPassObj.getUnlockFunc()},
                  oldWltData, progFunc
               );
            }
         } else {
            //we didnt have an old wallet merge into the new one, extend
            //the address chain for some baseline count
            progFunc(std::make_unique<Wallets::Progress::ExtendChain>(500));
            if (isWO) {
               restoreResult.wltPtr->extendPublicChain(499);
            } else {
               restoreResult.wltPtr->setPassphrasePromptLambda(
                  params.setPrivPassObj.getUnlockFunc());
               restoreResult.wltPtr->extendPrivateChainToIndex(499);
            }
            restoreResult.wltPtr.reset();
         }

         //unload old wallet & rename it
         if (wltManager_->hasWallet(newWltID)) {
            oldWltPath = wltManager_->unloadWallet(newWltID);
            if (!oldWltPath.empty()) {
               oldWltPath = FileUtils::appendTagToPath(oldWltPath, "_old");
            }
         }

         //move new wallet
         auto newPath = wltManager_->getWalletDir() / newWltPath.filename();
         std::filesystem::rename(newWltPath, newPath);

         //reload new wallet
         wltManager_->loadWallet(Wallets::IO::ReadOnlyFileParams{
            newPath, params.setCtrlPassObj.getUnlockFunc()});

         //delete old wallet if there's one
         if (!oldWltPath.empty() && std::filesystem::exists(oldWltPath)) {
            std::filesystem::remove(oldWltPath);
         }

         //put first address in use, or the GUI will have nothing to display
         if (!restoreResult.merge) {
            auto wltContainer = wltManager_->getWalletContainer(newWltID);
            auto wltPtr = wltContainer->getWalletPtr();
            auto accPtr = wltContainer->getAddressAccount();
            accPtr->getNewAddress(wltPtr->getIface());
         }

         //cleanup & success
         sendCallbackCleanup(this, callbackId);
         sendSuccess(this, refId);
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

      //delete the temp folder
      FileUtils::removeDirectory(tempDir);
   };

   auto worker = std::thread(restoreLbd, std::move(backup));
   if (worker.joinable()) {
      worker.detach();
   }
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::importWallet(const std::filesystem::path& path, MessageId msgId)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);

   try {
      auto wltInfo = wltManager_->importFile(path);
      auto utilsReply = reply.initUtils();
      auto importReply = utilsReply.initImportWallet();

      importReply.setWalletId(wltInfo->walletId());
      importReply.setLabel(wltInfo->name());
      switch (wltInfo->state())
      {
         case WalletLoadState::Legacy:
         {
            //set the union
            importReply.setLegacy();

            //cast the info ptr
            auto legacyInfo = std::dynamic_pointer_cast<A135FileInfo>(wltInfo);
            if (legacyInfo == nullptr) {
               throw std::runtime_error("failed to grab legacy wallet info");
            }

            //fill up the rest of the data
            importReply.setDescription(legacyInfo->description());
            importReply.setWatchingOnly(legacyInfo->isWatchingOnly());
            importReply.setEncrypted(legacyInfo->isEncrypted());
            importReply.setTimestamp(legacyInfo->timestamp());
            importReply.setHighestUsedIndex(legacyInfo->highestUsedIndex());
            importReply.setAddressCount(legacyInfo->addressCount());
            importReply.setKdfMem(legacyInfo->kdfMem());
            importReply.setSeedVersion(legacyInfo->version());
            break;
         }

         case WalletLoadState::Encrypted:
         {
            break;
         }

         case WalletLoadState::Ready:
         {
            importReply.setReady();
            break;
         }

         default:
            throw std::runtime_error("unexpected file state");
      }

      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setSuccess(false);
      reply.setError(e.what());
   }

   auto payload = serializeCapnp(message);
   writeToClient(payload);
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
   const Wallets::WalletId& walletId, const Wallets::AddressAccountId& accId)
{
   auto promPtr = std::make_shared<std::promise<AsyncClient::LedgerDelegate>>();
   auto fut = promPtr->get_future();
   auto lbd = [promPtr](
      ReturnMessage<AsyncClient::LedgerDelegate> result)->void
   {
      promPtr->set_value(std::move(result.get()));
   };

   //grab the wallet container
   auto wltCont = wltManager_->getWalletContainer(walletId, accId);
   auto walletObj = bdvPtr_->getWalletObj(wltCont->getDbId());

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
   const Wallets::WalletId& wltId, const Wallets::AddressAccountId& accId,
   const BinaryDataRef& addrHash)
{
   auto promPtr = std::make_shared<std::promise<AsyncClient::LedgerDelegate>>();
   auto fut = promPtr->get_future();
   auto lbd = [promPtr](
      ReturnMessage<AsyncClient::LedgerDelegate> result)->void
   {
      promPtr->set_value(std::move(result.get()));
   };

   auto wltCont = wltManager_->getWalletContainer(wltId, accId);
   auto walletObj = bdvPtr_->getWalletObj(wltCont->getDbId());

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
void CppBridge::getHistoryPageForDelegate(const std::string& id,
   unsigned from, unsigned to, MessageId msgId)
{
   auto iter = delegateMap_.find(id);
   if (iter == delegateMap_.end()) {
      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(msgId);
      reply.setSuccess(false);
      reply.setError(std::string{"unknown delegate id: "} + id);

      auto payload = serializeCapnp(message);
      this->writeToClient(payload);
      return;
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
      reply.setSuccess(true);

      auto payload = serializeCapnp(message);
      this->writeToClient(payload);
   };
   iter->second.getHistoryPages(from, to, lbd);
}

void CppBridge::getPageCountForDelegate(const std::string& id, MessageId msgId)
{
   auto iter = delegateMap_.find(id);
   if (iter == delegateMap_.end()) {
      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(msgId);
      reply.setSuccess(false);
      reply.setError(std::string{"unknown delegate id: "} + id);

      auto payload = serializeCapnp(message);
      this->writeToClient(payload);
      return;
   }

   auto lbd = [this, msgId](ReturnMessage<uint64_t> result)->void
   {
      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(msgId);
      auto delegate = reply.initDelegate();
      delegate.setGetPageCount(result.get());
      reply.setSuccess(true);

      auto payload = serializeCapnp(message);
      this->writeToClient(payload);
   };
   iter->second.getPageCount(lbd);
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
BinaryData CppBridge::getBalanceAndCount(const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId, MessageId msgId)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   auto walletReply = reply.initWallet();
   auto bnc = walletReply.initGetBalanceAndCount();

   try {
      auto wltContainer = wltManager_->getWalletContainer(wltId, accId);
      bnc.setFull(wltContainer->getFullBalance());
      bnc.setSpendable(wltContainer->getSpendableBalance());
      bnc.setUnconfirmed(wltContainer->getUnconfirmedBalance());
      bnc.setTxnCount(wltContainer->getTxIOCount());
      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setError(e.what());
      reply.setSuccess(false);
   }

   reply.setReferenceId(msgId);
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getAddrCombinedList(const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId, MessageId msgId)
{
   auto wltContainer = wltManager_->getWalletContainer(wltId, accId);
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
   const auto& keyId = wltContainer->getDefaultEncryptionKeyId();
   for (const auto& addrPair : updatedMap) {
      auto capnAddr = addrDataReply[i++];
      addressToCapnp(capnAddr,
         addrPair.second, accPtr,
         keyId
      );
   }

   reply.setSuccess(true);
   reply.setReferenceId(msgId);
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getHighestUsedIndex(const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId, MessageId msgId)
{
   auto wltContainer = wltManager_->getWalletContainer(wltId, accId);

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
void CppBridge::extendAddressPool(const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId, unsigned count, bool isNew,
   const std::string& callbackId, MessageId msgId)
{
   auto extendChain = [this, msgId, callbackId](
      const Wallets::WalletId& wltId,
      const Wallets::AddressAccountId accId,
      unsigned count, bool isNew)
   {
      //setup progress reporting
      size_t tickTotal = count;
      int reportedTicks = -1;
      auto now = std::chrono::system_clock::now();

      //count notif
      auto notifyCount = [this, callbackId, tickTotal](int currentCount)
      {
         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<FromBridge>();
         auto notif = fromBridge.initNotification();
         notif.setCallbackId(callbackId);

         auto progressNotif = notif.initWalletProgress();
         auto countNotif = progressNotif.initExtendChain();
         countNotif.setTotal(tickTotal);
         countNotif.setCurrent(currentCount);

         auto serialized = serializeCapnp(message);
         this->writeToClient(serialized);
      };

      //progress callback
      auto updateProgress = [this, &reportedTicks, now, &notifyCount](int)
      {
         auto msElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now() - now).count();

         //report an event every 250ms
         int eventCount = msElapsed / 250;
         if (eventCount <= reportedTicks) {
            return;
         }
         reportedTicks = eventCount;
         notifyCount(0);
      };

      //this will extend the address chain and register the new addresses
      //the registration will trigger a notification to the UI to update
      //address data accordingly
      wltManager_->extendAddressChain(
         wltId, accId,
         count, isNew, updateProgress);

      //send last count update
      notifyCount(tickTotal);

      //complete process
      capnp::MallocMessageBuilder replyMessage;
      auto replyBridge = replyMessage.initRoot<FromBridge>();
      auto reply = replyBridge.initReply();
      reply.setSuccess(true);
      reply.setReferenceId(msgId);
      auto replySerialized = serializeCapnp(replyMessage);
      this->writeToClient(replySerialized);

      //cleanup
      sendCallbackCleanup(this, callbackId);
   };

   //run chain extention in another thread
   std::thread thr(extendChain, wltId, accId, count, isNew);
   if (thr.joinable()) {
      thr.detach();
   }
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::createWallet(SecureBinaryData extraEntropy,
   Wallets::IO::CreateWalletParams params, const std::string& callbackId,
   MessageId refId)
{
   auto setCtrlPassFunc = getSetPassFunc(this, callbackId, false);
   auto setPrivPassFunc = getSetPassFunc(this, callbackId, true);

   auto paramsCopy = std::make_unique<Wallets::IO::CreateWalletParams>(
      params.folder,
      Passphrase::SetNew{setPrivPassFunc},
      Passphrase::SetNew(setCtrlPassFunc),
      getWalletProgressLbd(this, callbackId),
      params.lookup, params.label, params.description
   );

   //this is a long process (several seconds), run in its own thread
   auto createWltFunc = [this, callbackId,
      extraEntropy=std::move(extraEntropy),
      refId](std::unique_ptr<Wallets::IO::CreateWalletParams> paramPtr) {
      //prepare reply
      capnp::MallocMessageBuilder replyMessage;
      auto fromBridge = replyMessage.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(refId);

      try {
         //create wallet
         auto wltContainer = wltManager_->createNewWallet(extraEntropy, *paramPtr);

         //put first address in use, or the GUI will have nothing to display
         auto wltPtr = wltContainer->getWalletPtr();
         auto accPtr = wltContainer->getAddressAccount();
         accPtr->getNewAddress(wltPtr->getIface());

         reply.setSuccess(true);
         auto utilsReply = reply.initUtils();
         auto wltId = wltPtr->getID();
         utilsReply.setCreateWallet(wltId);
      } catch (const std::exception& e) {
         reply.setSuccess(false);
         reply.setError(e.what());
      }

      //callback cleanup
      sendCallbackCleanup(this, callbackId);

      //reply to caller
      auto replySerialized = serializeCapnp(replyMessage);
      this->writeToClient(replySerialized);
   };

   std::thread thr(createWltFunc, std::move(paramsCopy));
   if (thr.joinable()) {
      thr.detach();
   }
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getWalletPacket(const Wallets::WalletId& wltId,
   Wallets::AddressAccountId accId, MessageId msgId) const
{
   std::shared_ptr<WalletContainer> wltContainer = nullptr;
   if (accId.isValid()) {
      wltContainer = wltManager_->getWalletContainer(wltId, accId);
   } else {
      wltContainer = wltManager_->getWalletContainer(wltId);
      accId = wltContainer->getAccountId();
   }

   if (wltContainer == nullptr) {
      throw std::runtime_error("could not get wallet container");
   }
   auto wltPtr = wltContainer->getWalletPtr();

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);
   reply.setSuccess(true);

   auto walletReply = reply.initWallet();
   auto capnWallet = walletReply.initGetData();
   walletToCapnp(wltPtr, accId, wltContainer->getDbId(), capnWallet);

   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::getAddress(const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId,
   uint32_t addrType, uint32_t addrKind,
   MessageId msgId)
{
   bool wasExtended = false;
   auto progFunc = [&wasExtended](int)
   {
      wasExtended = true;
   };

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);

   auto addrPtr = wltManager_->getNewAddress(wltId, accId, addrType, addrKind);
   if (addrPtr) {
      auto wltContainer = wltManager_->getWalletContainer(wltId, accId);
      auto wltPtr = wltContainer->getWalletPtr();
      auto accPtr = wltContainer->getAddressAccount();

      auto walletReply = reply.initWallet();
      auto capnAddr = walletReply.initGetAddress();
      addressToCapnp(capnAddr, addrPtr, accPtr,
         wltContainer->getDefaultEncryptionKeyId());
      reply.setSuccess(true);
   } else {
      reply.setSuccess(false);
      reply.setError("requested invalid address type");
   }

   auto serialized = serializeCapnp(message);
   this->writeToClient(serialized);
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
   auto type = BtcUtils::getTxInScriptType(script, hash);

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   auto utilsReply = reply.initScriptUtils();
   utilsReply.setGetTxInScriptType((uint32_t)type);

   reply.setSuccess(true);
   reply.setReferenceId(msgId);
   return serializeCapnp(message);;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getTxOutScriptType(
   const BinaryData& script, MessageId msgId) const
{
   auto type = BtcUtils::getTxOutScriptType(script);

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   auto utilsReply = reply.initScriptUtils();
   utilsReply.setGetTxOutScriptType((uint32_t)type);

   reply.setSuccess(true);
   reply.setReferenceId(msgId);
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::getScrAddrForScript(
   const BinaryData& script, MessageId msgId) const
{
   auto scrAddr = BtcUtils::getTxOutScrAddr(script);

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
   return Armory::getNameForAddrType(addrTypeInt);
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridge::setAddressTypeFor(const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId, const BinaryDataRef& idRef,
   uint32_t addrType, MessageId msgId) const
{
   auto wltContainer = wltManager_->getWalletContainer(wltId, accId);
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
void CppBridge::setupNewCoinSelectionInstance(const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId,
   unsigned height, MessageId msgId)
{
   auto wltContainer = wltManager_->getWalletContainer(wltId, accId);
   auto csId = fortuna.generateRandom(6).toHexStr();
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
CppBridge::coinSelectionInstance(const std::string& csId) const
{
   auto iter = csMap_.find(csId);
   if (iter == csMap_.end()) {
      return nullptr;
   }
   return iter->second;
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::createAddressBook(const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId, MessageId msgId)
{
   auto wltContainer = wltManager_->getWalletContainer(wltId, accId);
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
void CppBridge::setComment(const Wallets::WalletId& wltId,
   const std::string& hash, const std::string& comment)
{
   auto wltContainer = wltManager_->getWalletContainer(wltId);
   wltContainer->setComment(hash, comment);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::setWalletLabels(const Wallets::WalletId& wltId,
   const std::string& label, const std::string& desc)
{
   auto wltContainer = wltManager_->getWalletContainer(wltId);
   wltContainer->setLabels(label, desc);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::getUTXOs(const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId,
   uint64_t value, bool zc, bool rbf, MessageId msgId)
{
   auto wltContainer = wltManager_->getWalletContainer(wltId, accId);
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
   auto id = fortuna.generateRandom(6).toHexStr();
   signerMap_.emplace(id, std::make_shared<CppBridgeSignerStruct>(
      [this](const Wallets::WalletId& wltId)->auto {
         return this->getWalletPtr(wltId); },
      [this](ServerPushWrapper wrapper) {
         callbackWriter(wrapper);
      }
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
void CppBridge::getFeeSchedule(const std::string& strat, MessageId msgId) const
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

////
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

////////////////////////////////////////////////////////////////////////////////
SecureBinaryData CppBridge::generateRandom(size_t count) const
{
   return fortuna.generateRandom(count);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::getUnlockTime(const Wallets::WalletId& walletId, MessageId refId)
{
   using namespace std::chrono;
   auto testUnlockTime = [this, walletId, refId]
   {
      try {
         //get wallet
         auto wltContainer = wltManager_->getWalletContainer(walletId);
         auto wlt = wltContainer->getWalletPtr();

         //get kdf
         auto kdf = wlt->getPrimaryKdf();
         if (kdf == nullptr) {
            throw std::runtime_error("kdf is null!");
         }

         //time the derivation of a dummy key
         auto start = system_clock::now();
         kdf->deriveKey(SecureBinaryData::fromString("test key"));
         auto end = system_clock::now();
         auto result = duration_cast<milliseconds>(end-start);

         //reply
         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<FromBridge>();
         auto reply = fromBridge.initReply();
         reply.setReferenceId(refId);
         reply.setSuccess(true);
         auto wltReply = reply.initWallet();
         wltReply.setGetUnlockTime(result.count());

         auto serialized = serializeCapnp(message);
         this->writeToClient(serialized);
      } catch (const std::exception& e) {
         //report error
         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<FromBridge>();
         auto reply = fromBridge.initReply();
         reply.setReferenceId(refId);
         reply.setSuccess(false);
         reply.setError(e.what());
         auto serialized = serializeCapnp(message);
         this->writeToClient(serialized);
      }
   };

   std::thread thr(testUnlockTime);
   if (thr.joinable()) {
      thr.detach();
   }
}

////////////////////////////////////////////////////////////////////////////////
////
////  CppBridgeSignerStruct
////
////////////////////////////////////////////////////////////////////////////////
CppBridgeSignerStruct::CppBridgeSignerStruct(
   std::function<WalletPtr(const Wallets::WalletId&)> getWalletFunc,
   std::function<void(ServerPushWrapper)> writeFunc) :
   getWalletFunc_(getWalletFunc), writeFunc_(writeFunc)
{
   signer = std::make_unique<Signing::Signer>();
}

////////////////////////////////////////////////////////////////////////////////
void CppBridgeSignerStruct::signTx(const Wallets::WalletId& wltId,
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
         signer->resetFeed();
         signer->setFeed(feed);

         //create & set passphrase lambda
         wltPtr->setPassphrasePromptLambda(passLbd);

         //lock decryption container
         auto lock = wltPtr->lockDecryptedContainer();

         //sign, this will prompt the passphrase lambda on demand
         signer->sign();
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
bool CppBridgeSignerStruct::resolve(const Wallets::WalletId& wltId)
{
   //grab wallet
   auto wltPtr = getWalletFunc_(wltId);

   //get wallet feed
   auto wltSingle = std::dynamic_pointer_cast<Wallets::AssetWallet_Single>(wltPtr);
   auto feed = std::make_shared<Signing::ResolverFeed_AssetWalletSingle>(wltSingle);

   //set feed & resolve
   signer->resetFeed();
   signer->setFeed(feed);
   signer->resolvePublicData();
   return true;
}

////////////////////////////////////////////////////////////////////////////////
BinaryData CppBridgeSignerStruct::getSignedStateForInput(
   unsigned inputId, MessageId referenceId)
{
   if (signState_ == nullptr) {
      signState_ = std::make_unique<Signing::TxEvalState>(
         signer->evaluateSignedState());
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
