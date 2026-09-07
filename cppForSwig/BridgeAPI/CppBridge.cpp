////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2019-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "CppBridge.h"
#include "BridgeSocket.h"
#include "./Wallets/Manager.h"
#include "./Wallets/TxIOCache.h"
#include "./Wallets/Notifications.h"

#include <Utils/ArmoryConfig.h>
#include <Utils/BtcUtils.h>
#include <Utils/FileUtils.h>
#include <Utils/Cryptography.h>
#include <Signer/Signer.h>
#include <Signer/ResolverFeed_Wallets.h>
#include <Signer/CoinSelection.h>
#include <Ledgers/LedgerEntry.h>
#include <Ledgers/Context.h>
#include <BlockchainDatabase/txio.h>

#include <Wallets/Seeds/Backups.h>
#include <Wallets/Seeds/Seeds.h>
#include <Wallets/IOHeader.h>
#include <Wallets/WalletIdTypes.h>
#include <Wallets/KDF.h>
#include <Wallets/Wallets.h>
#include <Wallets/AuthorizedPeers.h>
#include <Wallets/Addresses.h>
#include <Wallets/Accounts/AccountTypes.h>
#include <Wallets/Accounts/AddressAccounts.h>

#include <AsyncClient.h>
#include <TxClasses.h>

#include "DBSetup.h"
#include "PassphrasePrompt.h"

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

   std::string chainRoleForAssetAccount(
      const std::shared_ptr<Accounts::AddressAccount>& accPtr,
      const Wallets::AssetAccountId& assetAccId);

   void addressToCapnp(WalletData::AddressData::Builder& capnAddress,
      std::shared_ptr<AddressEntry> addrPtr,
      std::shared_ptr<Accounts::AddressAccount> addrAcc)
   {
      if (addrAcc == nullptr) {
         throw std::runtime_error("[addressToCapnp] null acc ptr");
      }

      const auto& assetID = addrPtr->getID();
      auto asset = addrAcc->getAssetForID(assetID);

      //address hash
      try {
         const auto& addrHash = addrPtr->getPrefixedHash();
         capnAddress.setPrefixedHash(capnp::Data::Builder(
            (uint8_t*)addrHash.getPtr(), addrHash.getSize()
         ));
      } catch (const AddressException&) {
         const auto& rawScript = addrPtr->getScript();
         capnAddress.setRawScript(capnp::Data::Builder(
            (uint8_t*)rawScript.getPtr(), rawScript.getSize()
         ));
      }

      //type & pubkey
      BinaryDataRef pubKeyRef;
      std::shared_ptr<AddressEntry_WithAsset> addrWithAssetPtr = nullptr;
      uint32_t addrType = (uint32_t)addrPtr->getType();

      auto addrNested = std::dynamic_pointer_cast<AddressEntry_Nested>(addrPtr);
      if (addrNested != nullptr) {
         addrType |= (uint32_t)addrNested->getPredecessor()->getType();
         auto pred = addrNested->getPredecessor();
         try {
            pubKeyRef = pred->getPreimage().getRef();
         } catch (const AddressException&) {
            // nothing to do, the underlying asset does not carry a
            // preimage (likely a nested raw script)
         }
         addrWithAssetPtr = std::dynamic_pointer_cast<AddressEntry_WithAsset>(pred);
      } else {
         try {
            pubKeyRef = addrPtr->getPreimage().getRef();
         } catch (const AddressException&) {
            // nothing to do, the underlying asset does not carry a
            // preimage (likely an imported p2pkh lacking the pubkey)
         }
         addrWithAssetPtr = std::dynamic_pointer_cast<AddressEntry_WithAsset>(addrPtr);
      }

      capnAddress.setAddrType(addrType);
      if (!pubKeyRef.empty()) {
         capnAddress.setPublicKey(capnp::Data::Builder(
            (uint8_t*)pubKeyRef.getPtr(), pubKeyRef.getSize()
         ));
      }

      //index
      capnAddress.setIndex(asset->getIndex());
      const auto& serAssetId = assetID.getSerializedKey(PROTO_ASSETID_PREFIX);
      capnAddress.setAssetId(capnp::Data::Builder(
         (uint8_t*)serAssetId.getPtr(), serAssetId.getSize()
      ));


      //address string, used flag, change flag
      try {
         capnAddress.setAddressString(addrPtr->getAddress());
      } catch (const AddressException&) {
         //nothing to do, address type does not yield human
         //readable strings
      }
      capnAddress.setIsUsed(addrAcc->isAssetInUse(addrPtr->getID()));
      capnAddress.setIsChange(addrAcc->isAssetChange(addrPtr->getID()));

      //priv key & encryption status
      bool hasPrivKey = false;
      if (addrWithAssetPtr != nullptr) {
         auto theAsset = addrWithAssetPtr->getAsset();
         if (theAsset != nullptr) {
            if (theAsset->hasPrivateKey()) {
               hasPrivKey = true;
            }
         }
      }
      capnAddress.setHasPrivKey(hasPrivKey);

      //precursor, if any
      if (addrNested == nullptr) {
         return;
      }

      try {
         const auto& precursor = addrNested->getPredecessor()->getScript();
         capnAddress.setPrecursorScript(capnp::Data::Builder(
            (uint8_t*)precursor.getPtr(), precursor.getSize()
         ));
      } catch (const AddressException&) {
         // no precursor script, nothing to do
      }
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

      capnWallet.setAccountName(accPtr->getDisplayName());
      capnWallet.setSeedTypeName(wltSingle->getSeedTypeDisplayName());
      capnWallet.setDerivationScheme(accPtr->getDerivationSchemeDisplay());

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
            addrPair.second, accPtr);
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
         capnOutput.setValue(utxo.getAmount());
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
      CppBridge* bridgePtr, const CallbackId& callbackId)
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

   void sendCallbackCleanup(CppBridge* bridgePtr, const CallbackId& callbackId)
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
            return WalletBackup::Type::ARMORY200_A;

         case Seeds::BackupType::Armory200b:
            return WalletBackup::Type::ARMORY200_B;

         case Seeds::BackupType::Armory200c:
            return WalletBackup::Type::ARMORY200_C;

         case Seeds::BackupType::Armory200d:
            return WalletBackup::Type::ARMORY200_D;

         case Seeds::BackupType::BIP39:
            return WalletBackup::Type::BIP39;

         default:
            return WalletBackup::Type::UNKNOWN;
      }
   }

   std::function<std::unique_ptr<Passphrase::Params>(void)> getSetPassFunc(
      CppBridge* bridgePtr, const CallbackId& callbackId, bool priv)
   {
      return [bridgePtr, callbackId, priv]()->std::unique_ptr<Passphrase::Params>
      {
         auto counterBd = fortuna.generateRandom(4);
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
         auto reply = fut.get();
         if (!reply.success) {
            return std::make_unique<Passphrase::Params>();
         }
         return std::make_unique<Passphrase::Params>(reply.passParams);
      };
   }

   std::function<void(AutomationStep)> getAutomationNotifFunc(
      CppBridge* bridgePtr, const CallbackId& callbackId)
   {
      return [bridgePtr, callbackId](AutomationStep step)
      {
         if (callbackId.empty()) {
            return;
         }

         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<FromBridge>();
         auto notif = fromBridge.initNotification();
         notif.setCallbackId(callbackId);

         if (step == AutomationStep::Cleanup) {
            notif.setCleanup();
         } else {
            auto automationNotif = notif.initAutomation();
            switch (step)
            {
               case AutomationStep::SpawnNode:
                  automationNotif.setSpawnNode();
                  break;

               case AutomationStep::SpawnDb:
                  automationNotif.setSpawnDb();
                  break;

               case AutomationStep::ConnectToDb:
                  automationNotif.setConnectToDb();
                  break;

               case AutomationStep::ShutdownDb:
                  automationNotif.setShutdownDb();
                  break;

               case AutomationStep::ShutdownNode:
                  automationNotif.setShutdownNode();
                  break;

               case AutomationStep::Done:
                  automationNotif.setDone();
                  break;
            }
         }
         auto response = serializeCapnp(message);
         bridgePtr->writeToClient(response);
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
   wltManager_->setBdvCallback([this](BinaryData& data)
      { writeToClient(data); }
   );
   wltManager_->setCleanupCallback([this]()
      { reset(); }
   );
}

CppBridge::~CppBridge()
{
   wltManager_->cleanupBDV();
}

void CppBridge::disconnectFromDb()
{
   if (bdvPtr_ && bdvPtr_->isValid()) {
      bdvPtr_->unregisterFromDB();
   }
   wltManager_->cleanupBDV();
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

bool CppBridge::isDbRunning()
{
   if (automationContext_ == nullptr) {
      return false;
   }
   return automationContext_->isDbRunning();
}

////////
void CppBridge::writeToClient(BinaryData& payload) const
{
   auto payloadPtr = std::make_unique<WritePayload_Bridge>();
   payloadPtr->data = std::move(payload);
   writeLambda_(std::move(payloadPtr));
}

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

      if (wltObj.second->hasAccountIds()) {
         const auto& accountIds = wltObj.second->getAccountIds();
         auto capnAccIds = capnWltObj.initAccountIds(accountIds.size());
         unsigned i = 0;
         for (const auto& accId : accountIds) {
            capnAccIds.set(i++, accId.toHexStr());
         }
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
   const CallbackId& callbackId, MessageId refId)
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
         wltManager_->unlockControlHeader(std::filesystem::path(path), lbd);
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
   const CallbackId& callbackId, MessageId refId)
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
   const CallbackId& callbackId, MessageId refId)
{
   auto forkLbd = [this](const Wallets::WalletId wltId,
      const CallbackId callbackId, MessageId refId)
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
namespace {
   struct ExportedKeyData
   {
      BinaryData assetId;
      BinaryDataRef privKeyRef;
      BinaryData pubKey;
      std::string addressString;
      uint32_t addrType = 0;
      int32_t index = 0;
      std::string accountId;
      bool isUsed = false;
      std::string chainRole;
   };

   std::string chainRoleForAssetAccount(
      const std::shared_ptr<Accounts::AddressAccount>& accPtr,
      const Wallets::AssetAccountId& assetAccId)
   {
      const auto& outerId = accPtr->getOuterAccountID();
      const auto& innerId = accPtr->getInnerAccountID();
      if (outerId == innerId) {
         return {};
      }
      if (assetAccId == outerId) {
         return "Receive";
      }
      if (assetAccId == innerId) {
         return "Change";
      }
      return {};
   }

   void fillExportedKeyMetadata(ExportedKeyData& entry,
      const std::shared_ptr<Accounts::AddressAccount>& accPtr,
      const Wallets::AssetId& assetID)
   {
      try {
         auto addrEntry = accPtr->getAddressEntryForID(assetID);
         if (addrEntry == nullptr) {
            return;
         }

         uint32_t addrType = (uint32_t)addrEntry->getType();
         auto addrNested = std::dynamic_pointer_cast<
            AddressEntry_Nested>(addrEntry);
         if (addrNested != nullptr) {
            addrType |= (uint32_t)addrNested->getPredecessor()->getType();
         }
         entry.addrType = addrType;

         try {
            if (addrNested != nullptr) {
               entry.pubKey = addrNested->getPredecessor()->getPreimage();
            } else {
               entry.pubKey = addrEntry->getPreimage();
            }
         } catch (const AddressException&) {
            //no public key for this address type, leave empty
         }

         try {
            entry.addressString = addrEntry->getAddress();
         } catch (const AddressException&) {
            //address type yields no human readable string
         }
      } catch (const std::exception&) {
         //asset has no instantiable address, leave blank
      }
   }

   void collectExportedKeys(
      const std::shared_ptr<Wallets::AssetWallet_Single>& wltSingle,
      const Wallets::AddressAccountId& scopeAccId,
      bool includePrivateKeys,
      std::vector<ExportedKeyData>& exportedKeys)
   {
      std::vector<Wallets::AddressAccountId> accountIdsToWalk;
      if (scopeAccId.isValid()) {
         accountIdsToWalk.push_back(scopeAccId);
      } else {
         for (const auto& addrAccId : wltSingle->getAccountIDs()) {
            accountIdsToWalk.push_back(addrAccId);
         }
      }

      auto walkWallet = [&]()
      {
         for (const auto& addrAccId : accountIdsToWalk) {
            auto accPtr = wltSingle->getAccountForID(addrAccId);
            if (!accPtr) {
               continue;
            }

            for (const auto& assetAccId : accPtr->getAccountIdSet()) {
               auto assetAcc = accPtr->getAccountForID(assetAccId);
               if (!assetAcc) {
                  continue;
               }

               switch (assetAcc->type()) {
                  case Accounts::AssetAccountType::Plain:
                  case Accounts::AssetAccountType::Imports:
                     break;

                  case Accounts::AssetAccountType::ECDH:
                  case Accounts::AssetAccountType::ImportsWO:
                     continue;

                  default:
                     LOGWARN << "trying to export keys" <<
                        " from unsupported account type";
                     continue;
               }

               //getDecryptedPrivateKeyForAsset will fill missing private keys
               //run backwards the assets to compute missing keys in batch
               //rather than sequentially
               //TODO: add progressCallback logic to extend/fillPrivateKey
               auto lastIdx = assetAcc->getLastComputedIndex();
               for (int32_t i = lastIdx; i >= 0; i--) {
                  auto assetPtr = assetAcc->getAssetForKey(i);
                  auto assetSingle = std::dynamic_pointer_cast<
                     Assets::AssetEntry_Single>(assetPtr);
                  if (!assetSingle) {
                     continue;
                  }

                  const auto& assetID = assetPtr->getID();

                  ExportedKeyData entry;
                  entry.assetId = assetID.getSerializedKey(PROTO_ASSETID_PREFIX);
                  entry.index = assetSingle->getIndex();
                  entry.accountId = addrAccId.toHexStr();
                  entry.isUsed = accPtr->isAssetInUse(assetID);
                  entry.chainRole = chainRoleForAssetAccount(accPtr, assetAccId);

                  if (includePrivateKeys) {
                     entry.privKeyRef =
                        wltSingle->getDecryptedPrivateKeyForAsset(assetSingle);
                  }

                  fillExportedKeyMetadata(entry, accPtr, assetID);
                  exportedKeys.emplace_back(std::move(entry));
               }
            }
         }
      };

      walkWallet();
   }

   void populateExportedKey(WalletReply::ExportedPrivateKey::Builder& capnKey,
      const ExportedKeyData& entry)
   {
      capnKey.setAssetId(capnp::Data::Builder(
         (uint8_t*)entry.assetId.getPtr(), entry.assetId.getSize()));
      if (!entry.privKeyRef.empty()) {
         capnKey.setPrivKey(capnp::Data::Builder(
            (uint8_t*)entry.privKeyRef.getPtr(), entry.privKeyRef.getSize()));
      }
      if (!entry.pubKey.empty()) {
         capnKey.setPublicKey(capnp::Data::Builder(
            (uint8_t*)entry.pubKey.getPtr(), entry.pubKey.getSize()));
      }
      capnKey.setAddressString(entry.addressString);
      capnKey.setAddrType(entry.addrType);
      capnKey.setIndex(entry.index);
      capnKey.setAccountId(entry.accountId);
      capnKey.setIsUsed(entry.isUsed);
      capnKey.setChainRole(entry.chainRole);
   }

   BinaryData buildExportedKeysReply(MessageId msgId,
      const std::vector<ExportedKeyData>& exportedKeys,
      const std::string& error)
   {
      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(msgId);

      if (!error.empty()) {
         reply.setSuccess(false);
         reply.setError(error);
      } else {
         reply.setSuccess(true);
         auto walletReply = reply.initWallet();
         auto capnKeys = walletReply.initExportKeys(exportedKeys.size());
         for (size_t i = 0; i < exportedKeys.size(); i++) {
            auto capnKey = capnKeys[i];
            populateExportedKey(capnKey, exportedKeys[i]);
         }
      }

      return serializeCapnp(message);
   }
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::exportKeys(const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId,
   bool includePrivateKeys, const CallbackId& callbackId, MessageId msgId)
{
   auto func = [this, wltId, accId, includePrivateKeys, callbackId, msgId]()
   {
      std::vector<ExportedKeyData> exportedKeys;
      BinaryData serialized;

      std::shared_ptr<BridgePassphrasePrompt> passPromptObj;

      struct ExportKeysCleanup
      {
         std::shared_ptr<BridgePassphrasePrompt> prompt;

         ~ExportKeysCleanup()
         {
            if (prompt) {
               prompt->cleanup();
            }
         }
      };

      try {
         auto wltContainer = wltManager_->getWalletContainer(wltId);
         auto wltPtr = wltContainer->getWalletPtr();
         auto wltSingle = std::dynamic_pointer_cast<
            Wallets::AssetWallet_Single>(wltPtr);
         if (!wltSingle) {
            throw std::runtime_error("wallet is not an AssetWallet_Single");
         }

         if (includePrivateKeys) {
            if (!wltSingle->getRoot()->hasPrivateKey()) {
               throw std::runtime_error("this is a WO wallet");
            }

            passPromptObj = std::make_shared<BridgePassphrasePrompt>(
               callbackId, [this](ServerPushWrapper wrapper){
                  this->callbackWriter(wrapper);
               });
            auto lbd = passPromptObj->getLambda();
            ExportKeysCleanup cleanupGuard{passPromptObj};
            auto lock = wltSingle->lockDecryptedContainer(lbd);
            collectExportedKeys(wltSingle, accId, true, exportedKeys);
            serialized = buildExportedKeysReply(msgId, exportedKeys, "");
         } else {
            collectExportedKeys(wltSingle, accId, false, exportedKeys);
            serialized = buildExportedKeysReply(msgId, exportedKeys, "");
         }

      } catch (const std::exception& e) {
         serialized = buildExportedKeysReply(msgId, {}, e.what());
      }

      this->writeToClient(serialized);
   };

   std::thread thr(func);
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
   auto wltContMap = wltManager_->getWalletContainerMap();
   auto wltPackets = mgrReply.initLoadWallets(wltContMap.size());

   unsigned i=0;
   for (const auto& wltContPair : wltContMap) {
      auto capnWallet = wltPackets[i++];
      walletToCapnp(
         wltContPair.second->getWalletPtr(),
         wltContPair.second->getAccountId(),
         wltContPair.first,
         capnWallet
      );
   }

   reply.setReferenceId(msgId);
   reply.setSuccess(true);
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
bool CppBridge::unloadWallet(const Wallets::WalletId& wltId)
{
   try {
      wltManager_->unloadWallet(wltId);
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
// peers db stuff
void CppBridge::loadPeersDb(const CallbackId& callbackId, MessageId refId)
{
   /*
   * Should call this method from a dedicated thread
   */
   if (peersDb_ != nullptr) {
      return;
   }

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(refId);

   try {
      auto passPromptObj = std::make_shared<BridgePassphrasePrompt>(
         callbackId, [this](ServerPushWrapper wrapper) {
            this->callbackWriter(wrapper);
      });
      peersDb_ = std::make_shared<NetworkPeers::ClientStore>(
         Wallets::IO::ReadOnlyFileParams{
            path_ / CLIENT_AUTH_PEER_FILENAME,
            passPromptObj->getLambda()
      });
      reply.setSuccess(true);
   } catch (const NetworkPeers::FileMissing&) {
      /***
      NOTE:
         short term solution: auto generate the auth peer db if its missing
         long term soution: implement UI for user to boostrap/encrypt the store
      ***/
      auto setCtrlPassFunc = getSetPassFunc(this, callbackId, false);
      auto wlt = NetworkPeers::PeerStore::bootstrapWallet(
         Wallets::IO::CreateFileParams{
            path_ / CLIENT_AUTH_PEER_FILENAME,
            Passphrase::SetNew{setCtrlPassFunc}
      });
      peersDb_ = std::make_shared<NetworkPeers::ClientStore>(wlt);
      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setError(
         std::string{"failed to setup peers db with error: "} + e.what());
      reply.setSuccess(false);
   }

   auto response = serializeCapnp(message);
   this->writeToClient(response);
}

void CppBridge::listPeers(MessageId refId)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(refId);

   try {
      if (peersDb_ == nullptr) {
         throw std::runtime_error("have to load peers db before listing peers");
      }

      struct KeyData
      {
         std::set<std::string> names;
         std::string label;
      };

      auto getKeyData = []
      (std::unique_ptr<NetworkPeers::PeerStoreView> view)
      ->std::map<BinaryDataRef, KeyData>
      {
         std::map<BinaryDataRef, KeyData> result;
         for (const auto& namePair : view->getPeerNameMap()) {
            if (namePair.first == "own") {
               continue;
            }
            auto iter = result.find(namePair.second.getRef());
            if (iter == result.end()) {
               iter = result.emplace(namePair.second.getRef(), KeyData{}).first;
            }
            iter->second.names.emplace(namePair.first);
         }

         for (const auto& label : view->getPublicKeyMap()) {
            auto iter = result.find(label.first.getRef());
            if (iter == result.end()) {
               continue;
            }
            iter->second.label = label.second;
         }
         return result;
      };

      //grab 1way peers, sort by key
      auto keysOneWay = getKeyData(
         peersDb_->getView(NetworkPeers::PeerType::ServerOneWay));
      auto keysTwoWay = getKeyData(
         peersDb_->getView(NetworkPeers::PeerType::ServerTwoWay));

      //prepare capnp reply list
      auto setupReply = reply.initSetup();
      auto peersCapnp = setupReply.initListPeers(
         keysOneWay.size() + keysTwoWay.size() + 1);

      //set own key
      auto ownKey = peersDb_->getOwnPublicKey();
      NetworkPeers::PeerKey ownPeer{ownKey, NetworkPeers::PeerType::Client};
      auto ownKeyCapnp = peersCapnp[0];
      ownKeyCapnp.setOneWay(false);
      auto ownKeyDataCapnp = ownKeyCapnp.initPeer();
      ownKeyDataCapnp.setKey(ownPeer.toHumanReadable());
      auto keyNames = ownKeyDataCapnp.initNames(1);
      keyNames.set(0, "own");
      ownKeyDataCapnp.setLabel("N/A");

      //function to populate the capnp peer list
      size_t counter = 1;
      auto addKeys = [this, &counter, &peersCapnp]
      (std::map<BinaryDataRef, KeyData> keyMap, bool oneWay)
      {
         for (const auto& keyData : keyMap) {
            auto peerDataCapnp = peersCapnp[counter++];
            peerDataCapnp.setOneWay(oneWay);

            auto peerCapnp = peerDataCapnp.initPeer();
            auto namesCapnp = peerCapnp.initNames(keyData.second.names.size());
            unsigned y = 0;
            for (const auto& name : keyData.second.names) {
               namesCapnp.set(y++, name);
            }

            NetworkPeers::PeerKey peer{keyData.first, oneWay ?
               NetworkPeers::PeerType::ServerOneWay : NetworkPeers::PeerType::ServerTwoWay};
            peerCapnp.setKey(peer.toHumanReadable());
            if (keyData.second.label.empty()) {
               peerCapnp.setLabel("N/A");
            } else {
               peerCapnp.setLabel(keyData.second.label);
            }
         }
      };

      //feed the keymaps to the function
      addKeys(keysOneWay, true);
      addKeys(keysTwoWay, false);

      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setError(std::string{"failed to list peers with error: "} + e.what());
      reply.setSuccess(false);
   }

   auto response = serializeCapnp(message);
   this->writeToClient(response);
}

void CppBridge::addPeer(const std::string& peerKey,
   std::vector<std::string>& names, const std::string& label, MessageId refId)
{
   LOGINFO << "adding peer";

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(refId);

   try {
      if (peersDb_ == nullptr) {
         throw std::runtime_error("have to load peers db before adding a peer");
      }
      auto peer = NetworkPeers::PeerKey::fromHumanReadable(peerKey);
      if (!peer.isServer()) {
         throw std::runtime_error("cannot add a client key to a client peer store");
      }
      peersDb_->addPeer(peer, names, label);
      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setError(std::string{"failed to add peer with error: "} + e.what());
      reply.setSuccess(false);
   }

   auto response = serializeCapnp(message);
   this->writeToClient(response);
}

void CppBridge::removePeer(const std::string& peerKey, MessageId refId)
{
   LOGINFO << "removing peer";

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(refId);

   try {
      if (peersDb_ == nullptr) {
         throw std::runtime_error("have to load peers db before adding a peer");
      }
      auto peer = NetworkPeers::PeerKey::fromHumanReadable(peerKey);
      if (!peer.isServer()) {
         throw std::runtime_error("cannot remove a client key from a client peer store");
      }
      peersDb_->erasePeer(peer);
      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setError(std::string{"failed to remove peer with error: "} + e.what());
      reply.setSuccess(false);
   }

   auto response = serializeCapnp(message);
   this->writeToClient(response);
}

void CppBridge::setPeerLabel(
   const std::string& peerKey, const std::string& label, MessageId refId)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(refId);

   try {
      if (peersDb_ == nullptr) {
         throw std::runtime_error("have to load peers db before updating labels");
      }
      auto peer = NetworkPeers::PeerKey::fromHumanReadable(peerKey);
      peersDb_->setLabel(peer, label);
      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setError(std::string{"failed to remove peer with error: "} + e.what());
      reply.setSuccess(false);
   }

   auto response = serializeCapnp(message);
   this->writeToClient(response);
}

////////////////////////////////////////////////////////////////////////////////
// db connection routines
void CppBridge::connectToIp(const std::string& ip, const std::string& port,
   const CallbackId& callbackId, MessageId refId)
{
   /*
   * Should call this method from a dedicated thread
   * Connect to db by port + ip. Expect db to present it's public key, which
   * will be served to the caller by callback. 1-way authentication only (client
   * auths the db)
   * Peers store is ignored, a throw away public key is generated by the client
   * at connection time.
   */
   LOGINFO << "connecting to ip";

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(refId);

   if (dbOffline_) {
      LOGWARN << "attempt to connect to DB in offline mode, ignoring";
      reply.setError("cannot setup db in offline mode");
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

   auto presentServerKeyCallback = [callbackId, this]
   (const BinaryData& key)->bool
   {
      auto counterBd = fortuna.generateRandom(4);
      auto notifCounter = *(uint32_t*)counterBd.getPtr();
      NetworkPeers::PeerKey peerKey{key, NetworkPeers::PeerType::ServerOneWay};

      //create present pubkey notif
      capnp::MallocMessageBuilder notifMessage;
      auto fromBridge = notifMessage.initRoot<FromBridge>();
      auto notif = fromBridge.initNotification();
      notif.setCallbackId(callbackId);
      notif.setCounter(notifCounter);
      notif.setPresentPubkey(peerKey.toHumanReadable());
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
      ServerPushWrapper wrapper{
         notifCounter, replyLbd,
         std::move(notifSerialized)};
      callbackWriter(wrapper);

      //wait on reply
      auto reply = fut.get();
      return reply.success;
   };

   //connect to db
   try {
      bdvPtr_ = setupClientConnection(
         std::make_shared<NetworkPeers::ClientStore>(),
         ip, port, true, presentServerKeyCallback,
         wltManager_->getBdvCallback());
      if (bdvPtr_ == nullptr) {
         throw std::runtime_error("connectToIP failed");
      }
      wltManager_->setBdvPtr(bdvPtr_);

      //reply to caller
      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setSuccess(false);
      reply.setError(e.what());
   }

   auto response = serializeCapnp(message);
   this->writeToClient(response);
}

void CppBridge::connectToPeer(const std::string& peerKey, MessageId refId)
{
   /*
   * Should call this method from a dedicated thread
   * Connect to a db by resolving the peer's ip and port from the
   * peers db.
   * The peers db has to be loaded prior to calling this. Can operate in
   * both 1-way or 2-way auth
   * TODO: add way to stored associated 1-way or 2-way auth mode with peer entry
   */
   LOGINFO << "connecting to peer";

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(refId);

   if (dbOffline_) {
      LOGWARN << "attempt to connect to DB in offline mode, ignoring";
      reply.setError("cannot setup db in offline mode");
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

   //connect to db
   try {
      if (peersDb_ == nullptr) {
         throw std::runtime_error(
            "have to load peers db before attemping connection to a peer");
      }
      auto peerObj = NetworkPeers::PeerKey::fromHumanReadable(peerKey);
      auto peers = peersDb_->getNarrowSet(peerObj);

      bdvPtr_ = setupClientConnection(peers, peerObj,
         wltManager_->getBdvCallback());
      if (bdvPtr_ == nullptr) {
         throw std::runtime_error("connecToPeer failed");
      }
      wltManager_->setBdvPtr(bdvPtr_);

      //reply to caller
      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setSuccess(false);
      reply.setError(std::string{"failed connect to peer with error: "} +
         e.what());
   }

   auto response = serializeCapnp(message);
   this->writeToClient(response);
}

////
void CppBridge::beginDbSession()
{
   if (bdvPtr_ == nullptr) {
      throw std::runtime_error("have to connect to db first!");
   }
   bdvPtr_->goOnline();
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::setAutomationContext(std::unique_ptr<AutomationContext> ctxPtr)
{
   if (automationContext_ != nullptr) {
      throw std::runtime_error("already have a context");
   }
   automationContext_ = std::move(ctxPtr);
}

void CppBridge::runAutomationContext(CallbackId cbId, MessageId refId)
{
   auto threadBody = [this, cbId, refId]()
   {
      LOGINFO << "running automation context";

      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(refId);

      //sanity checks
      if (dbOffline_) {
         reply.setError("cannot automate db in offline mode");
         reply.setSuccess(false);
         auto response = serializeCapnp(message);
         this->writeToClient(response);
         return;
      }

      if (automationContext_ == nullptr) {
         reply.setError("missing automation context");
         reply.setSuccess(false);
         auto response = serializeCapnp(message);
         this->writeToClient(response);
         return;
      }

      if (bdvPtr_ != nullptr) {
         reply.setError("already connected to db!");
         reply.setSuccess(false);
         auto response = serializeCapnp(message);
         this->writeToClient(response);
         return;
      }

      //run the context
      auto notifyFunc = getAutomationNotifFunc(this, cbId);
      try {
         if (!automationContext_->run(notifyFunc)) {
            //no automation, return
            reply.setSuccess(true);
            auto response = serializeCapnp(message);
            this->writeToClient(response);
            return;
         }

         //connect to db
         notifyFunc(AutomationStep::ConnectToDb);
         auto peers = automationContext_->getPeerStore();
         auto port = std::to_string(automationContext_->getDbPort());
         bdvPtr_ = setupClientConnection(peers,
            "127.0.0.1", port,
            false, nullptr,
            wltManager_->getBdvCallback());
         if (bdvPtr_ == nullptr) {
            throw std::runtime_error("automatedDb connection failed");
         }
         wltManager_->setBdvPtr(bdvPtr_, true);

         //reply to caller
         notifyFunc(AutomationStep::Done);
         reply.setSuccess(true);
      } catch (const std::exception& e) {
         LOGWARN << "automation context failure: " << e.what();
         reply.setSuccess(false);
         reply.setError(e.what());
      }

      notifyFunc(AutomationStep::Cleanup);
      auto response = serializeCapnp(message);
      this->writeToClient(response);
   };

   std::thread thr{threadBody};
   if (thr.joinable()) {
      thr.detach();
   }
}

void CppBridge::cleanupAutomationContext(CallbackId cbId, MessageId refId)
{
   auto threadBody = [this, refId, cbId]()
   {
      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(refId);

      auto notifFunc = getAutomationNotifFunc(this, cbId);
      try {
         if (automationContext_ != nullptr) {
            automationContext_->cleanup(notifFunc);
            automationContext_.reset();
         }
         reply.setSuccess(true);
      } catch (const std::exception& e) {
         reply.setSuccess(false);
         reply.setError(e.what());
      }

      notifFunc(AutomationStep::Cleanup);
      auto response = serializeCapnp(message);
      this->writeToClient(response);
   };

   std::thread thr{threadBody};
   if (thr.joinable()) {
      thr.detach();
   }
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
   bool isPriv, const CallbackId& callbackId, MessageId msgId)
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
   const CallbackId& callbackId, bool isPriv, MessageId msgId)
{
   auto func = [this, wltId, callbackId, isPriv, msgId]()
   {
      std::shared_ptr<BridgePassphrasePrompt> unlockObj;
      unlockObj = std::make_shared<BridgePassphrasePrompt>(
         callbackId, [this](ServerPushWrapper wrapper){
            this->callbackWriter(wrapper);
         });
      Passphrase::SetNew setNewFunc = getSetPassFunc(
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
   const CallbackId& callbackId, MessageId refId)
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
      this, refId, callbackId, isWO](
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
            return {false, false, {}};
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

         auto restoreResult = Seeds::Helpers::restoreFromBackup(
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
            //we dont have an existing wallet to merge into the new one,
            //extend the address chain for some baseline count
            unsigned lookup = 500;
            progFunc(std::make_unique<Wallets::Progress::ExtendChain>(lookup));
            if (isWO) {
               restoreResult.wltPtr->extendPublicChain(lookup);
            } else {
               auto lock = restoreResult.wltPtr->lockDecryptedContainer(
                  params.setPrivPassObj.getUnlockFunc());
               restoreResult.wltPtr->extendPrivateChain(lookup);
            }
            restoreResult.wltPtr.reset();
         }

         if (wltManager_->hasWallet(newWltID)) {
            //unload existing wallet & rename it
            oldWltPath = wltManager_->unloadWallet(newWltID);
            if (!oldWltPath.empty()) {
               oldWltPath = FileUtils::appendTagToPath(oldWltPath, "_old");
            }
         }

         //move new wallet file in datadir
         auto newPath = wltManager_->getWalletDir() / newWltPath.filename();
         std::filesystem::rename(newWltPath, newPath);

         //reload new wallet
         wltManager_->loadWallet(Wallets::IO::ReadOnlyFileParams{
            newPath, params.setCtrlPassObj.getUnlockFunc()});

         if (!oldWltPath.empty() && std::filesystem::exists(oldWltPath)) {
            //delete existing wallet
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
      } catch (const Seeds::RestoreUserException& e) {
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
   return wltManager_->getDelegateId();
}

const std::string& CppBridge::getLedgerDelegateIdForWallet(
   const Wallets::WalletId& walletId, const Wallets::AddressAccountId& accId)
{
   return wltManager_->getDelegateIdForWallet(walletId, accId);
}

const std::string& CppBridge::getLedgerDelegateIdForScrAddr(
   const Wallets::WalletId& wltId, const Wallets::AddressAccountId& accId,
   const BinaryDataRef& addrHash)
{
   return wltManager_->getDelegateIdForScrAddr(wltId, accId, addrHash);
}

void CppBridge::updateWalletsLedgerFilter(
   const std::map<Wallets::WalletId, std::set<Wallets::AddressAccountId>>& idMap)
{
   wltManager_->updateMainLedgerFilter(idMap);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::getHistoryPageForDelegate(const std::string& id,
   unsigned from, unsigned to, MessageId msgId)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);

   if (from > to) {
      reply.setSuccess(false);
      reply.setError("from > to");
      auto payload = serializeCapnp(message);
      this->writeToClient(payload);
      return;
   }

   try {
      auto delegate = reply.initDelegate();
      auto pages = delegate.initGetPages(to - from + 1);

      for (unsigned i = from; i <= to; i++) {
         auto ledgers = wltManager_->getPageForDelegate(id, i);

         auto capnPage = pages[i - from];
         auto capnLedgers = capnPage.initLedgers(ledgers.size());
         unsigned y = 0;
         for (const auto& ledger : ledgers) {
            auto capnLedger = capnLedgers[y++];
            capnLedger.setBalance(ledger.getValue());
            capnLedger.setTxHeight(ledger.getBlockNum());
            capnLedger.setTxOutIndex(ledger.getIndex());
            capnLedger.setTxTime(ledger.getTxTime());
            capnLedger.setIsCoinbase(ledger.isCoinbase());
            capnLedger.setIsSTS(ledger.isSentToSelf());
            capnLedger.setIsOptInRBF(ledger.isOptInRBF());
            capnLedger.setIsChainedZC(ledger.isChainedZC());

            auto txHash = ledger.getTxHash();
            capnLedger.setTxHash(capnp::Data::Builder(
               (uint8_t*)txHash.getPtr(), txHash.getSize()
            ));

            capnLedger.setWalletId(ledger.getWalletID());

            auto scrAddrList = ledger.getScrAddrList();
            auto capnAddrs = capnLedger.initScrAddrs(scrAddrList.size());
            unsigned i=0;
            for (const auto& scrAddr : scrAddrList) {
               capnAddrs.set(i++, capnp::Data::Builder(
                  (uint8_t*)scrAddr.getPtr(), scrAddr.getSize()
               ));
            }
         }
      }
      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setSuccess(false);
      reply.setError(e.what());
   }

   auto payload = serializeCapnp(message);
   this->writeToClient(payload);
}

void CppBridge::getPageCountForDelegate(const std::string& id, MessageId msgId)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);

   try {
      auto pageCount = wltManager_->getPageCountForDelegate(id);
      auto delegate = reply.initDelegate();
      delegate.setGetPageCount(pageCount);
      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setSuccess(false);
      reply.setError(e.what());
   }

   auto payload = serializeCapnp(message);
   this->writeToClient(payload);
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
      bnc.setTxnCount(wltContainer->getTxCount());
      reply.setSuccess(true);
   } catch (const std::exception& e) {
      reply.setError(e.what());
      reply.setSuccess(false);
   }

   reply.setReferenceId(msgId);
   return serializeCapnp(message);
}

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
      bnc.setFull(addrPair.second.fullBalance);
      bnc.setSpendable(addrPair.second.spendableBalance);
      bnc.setUnconfirmed(addrPair.second.unconfirmedBalance);
      bnc.setTxnCount(addrPair.second.txCount);
   }

   auto updatedMap = wltContainer->getUpdatedAddressMap();
   auto accPtr = wltContainer->getAddressAccount();

   auto addrDataReply = combinedReply.initUpdatedAssets(updatedMap.size());
   i=0;
   for (const auto& addrPair : updatedMap) {
      auto capnAddr = addrDataReply[i++];
      addressToCapnp(capnAddr, addrPair.second, accPtr);
   }

   reply.setSuccess(true);
   reply.setReferenceId(msgId);
   return serializeCapnp(message);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::extendAddressPool(const Wallets::WalletId& wltId,
   const Wallets::AddressAccountId& accId, unsigned count, bool isNew,
   const CallbackId& callbackId, MessageId msgId)
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
void CppBridge::createWallet(Seeds::SeedType sType,
   SecureBinaryData extraEntropy, Wallets::IO::CreateWalletParams params,
   const CallbackId& callbackId, MessageId refId)
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
   auto createWltFunc = [this, sType, callbackId,
      extraEntropy=std::move(extraEntropy),
      refId](std::unique_ptr<Wallets::IO::CreateWalletParams> paramPtr) {
      //prepare reply
      capnp::MallocMessageBuilder replyMessage;
      auto fromBridge = replyMessage.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(refId);

      try {
         //create wallet
         auto wltId = wltManager_->createNewWallet(
            sType, extraEntropy, *paramPtr);

         reply.setSuccess(true);
         auto utilsReply = reply.initUtils();
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
BinaryData CppBridge::getAccountIds(const Wallets::WalletId& wltId,
   MessageId msgId) const
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);

   try {
      auto addrAccountIds = wltManager_->getAddressAccountIds(wltId);
      reply.setSuccess(true);
      auto walletReply = reply.initWallet();
      auto accList = walletReply.initGetAccountIds(addrAccountIds.size());

      unsigned i = 0;
      for (const auto& addrAcc : addrAccountIds) {
         accList.set(i++, addrAcc.toHexStr());
      }
   } catch (const std::exception& e) {
      reply.setSuccess(false);
      reply.setError(e.what());
   }
   return serializeCapnp(message);
}

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
      addressToCapnp(capnAddr, addrPtr, accPtr);
      reply.setSuccess(true);
   } else {
      reply.setSuccess(false);
      reply.setError("requested invalid address type");
   }

   auto serialized = serializeCapnp(message);
   this->writeToClient(serialized);
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::getTxsByHash(const std::set<Types::TxHash>& hashes, MessageId msgId)
{
   auto func = [this, hashes, msgId]()
   {
      auto txioCache = wltManager_->txioCache();
      ReentrantLock lock(txioCache.get());
      auto dbCache = txioCache->getDBCache();

      std::vector<::Tx> txs;
      txs.reserve(hashes.size());
      for (const auto& txHash : hashes) {
         try {
            txs.emplace_back(dbCache->getTxByHash(txHash));
         } catch (const std::out_of_range&) {
            //no tx for this hash
            continue;
         }
      }

      capnp::MallocMessageBuilder message;
      auto fromBridge = message.initRoot<FromBridge>();
      auto reply = fromBridge.initReply();
      reply.setReferenceId(msgId);
      if (txs.empty()) {
         reply.setSuccess(false);
         reply.setError("no txs found for requested hashes");
      } else {
         auto serviceReply = reply.initService();
         auto capnTxs = serviceReply.initGetTxsByHash(txs.size());

         size_t count = 0;
         for (const auto& tx : txs) {
            auto capnTx = capnTxs[count++];
            auto txKey = tx.getDBKey();
            if (Types::isThisAZCKey(txKey)) {
               capnTx.setHeight(UINT32_MAX);
               capnTx.setTimestamp(tx.getTxTime());
               capnTx.setTxIndex(UINT32_MAX);
            } else {
               auto header = dbCache->getHeader(
                  Types::getBlockIDFromTxKey(txKey));
               capnTx.setHeight(header->blockHeight);
               capnTx.setTimestamp(header->timestamp);
               capnTx.setTxIndex(tx.getTxIndex());
            }
            const auto& txHash = tx.getThisHash();
            capnTx.setHash(capnp::Data::Builder(
               (uint8_t*)txHash.getPtr(), txHash.getSize()));

            auto capnTxBody = capnTx.initBody();
            capnTxBody.setRaw(capnp::Data::Builder(
               (uint8_t*)tx.getPtr(), tx.getSize()));
            capnTxBody.setKey(txKey);
            capnTxBody.setIsChainedZc(tx.isChained());
            capnTxBody.setIsRbf(tx.isRBF());
         }
         reply.setSuccess(true);
      }

      auto serialized = serializeCapnp(message);
      this->writeToClient(serialized);
   };

   std::thread run{func};
   if (run.joinable()) {
      run.detach();
   }
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
   auto assetId = Wallets::AssetId::deserializeKey(
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
   addressToCapnp(capnAddr, addrPtr, accPtr);
   return serializeCapnp(message);
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
   auto aeVec = wltContainer->getAddressBook();

   auto fetchLbd = [wltContainer](uint64_t val)->std::vector<::UTXO>
   {
      return wltContainer->getUTXOs(val * 10U, false, false);
   };

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
   auto addrBook = wltContainer->getAddressBook();

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);
   reply.setSuccess(true);

   auto walletReply = reply.initWallet();
   auto capnAddrBook = walletReply.initCreateAddressBook();
   auto capnEntries = capnAddrBook.initEntries(addrBook.size());
   unsigned i=0;
   for (const auto& ae : addrBook) {
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
   auto utxos = wltContainer->getUTXOs(value, zc, rbf);

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);
   reply.setSuccess(true);

   auto wallet = reply.initWallet();
   auto capnUtxos = wallet.initGetUtxos(utxos.size());
   utxosToCapnp(utxos, capnUtxos);

   auto serialized = serializeCapnp(message);
   this->writeToClient(serialized);
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
void CppBridge::broadcastTxs(const std::vector<BinaryData>& rawTxVec, bool isRPC)
{
   if (!isRPC) {
      bdvPtr_->broadcastZC(rawTxVec);
   } else {
      for (const auto& rawTx : rawTxVec) {
         bdvPtr_->broadcastThroughRPC(rawTx);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void CppBridge::getBlockTimeByHeight(uint32_t height, MessageId msgId) const
{
   auto dbCache = wltManager_->getDbCache();
   auto header = dbCache->getHeaderForHeight(height);

   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<FromBridge>();
   auto reply = fromBridge.initReply();
   reply.setReferenceId(msgId);

   if (header != nullptr) {
      auto service = reply.initService();
      service.setGetBlockTimeByHeight(header->timestamp);
      reply.setSuccess(true);
   } else {
      reply.setSuccess(false);
      reply.setError(std::format("could not find header for height {}", height));
   }

   auto serialized = serializeCapnp(message);
   this->writeToClient(serialized);
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
void CppBridge::getUnlockTime(
   const Wallets::WalletId& walletId, MessageId refId) const
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

bool CppBridge::doesWalletHaveImports(const Wallets::WalletId& walletId) const
{
   //get wallet
   auto wltContainer = wltManager_->getWalletContainer(walletId);
   auto wlt = wltContainer->getWalletPtr();

   //look for address imports
   try {
      auto addrAccPtr = wlt->getAccountForID(IMPORTS_ACCOUNT_PUB);
      auto accPtr = addrAccPtr->getOuterAccount();
      if (accPtr->getAssetCount() > 0) {
         return true;
      }
   } catch (const std::exception&) {
      //no WO import account, move on
   }

   //look for priv key imports
   try {
      auto addrAccPtr = wlt->getAccountForID(IMPORTS_ACCOUNT_PRIV);
      auto accPtr = addrAccPtr->getOuterAccount();
      if (accPtr->getAssetCount() > 0) {
         return true;
      }
   } catch (const std::exception&) {
      //no priv import account
   }

   //no imports were found
   return false;
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
   const CallbackId& callbackId, MessageId referenceId)
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

         //lock decryption container
         auto lock = wltPtr->lockDecryptedContainer(passLbd);

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
      reply.setSuccess(success);
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
   auto signState = signer->evaluateSignedState();
   auto signStateInput = signState.getSignedStateForInput(inputId);

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
