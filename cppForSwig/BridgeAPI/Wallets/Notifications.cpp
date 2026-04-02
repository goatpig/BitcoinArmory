////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Notifications.h"
#include <Ledgers/LedgerEntry.h>

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/Bridge.capnp.h"
#include "capnp/Types.capnp.h"

using namespace Armory;
using namespace Armory::Bridge;

#define BRIDGE_CALLBACK_BDM         "bdm_callback"
#define BRIDGE_CALLBACK_PROGRESS    "progress"
#define DISCONNECTED_CALLBACK_ID    "disconnected"

////////////////////////////////////////////////////////////////////////////////
namespace
{
   BinaryData serializeCapnp(capnp::MallocMessageBuilder& msg)
   {
      auto flat = capnp::messageToFlatArray(msg);
      auto bytes = flat.asBytes();
      return BinaryData(bytes.begin(), bytes.end());
   }

   void ledgerToCapnp(const Ledgers::Entry& ledger,
      Codec::Types::TxLedger::LedgerEntry::Builder& capnLedger)
   {
      capnLedger.setBalance(ledger.getValue());
      capnLedger.setTxHeight(ledger.getBlockNum());
      capnLedger.setTxOutIndex(ledger.getIndex());
      capnLedger.setTxTime(ledger.getTxTime());
      capnLedger.setIsCoinbase(ledger.isCoinbase());
      capnLedger.setIsSTS(ledger.isSentToSelf());
      capnLedger.setIsOptInRBF(ledger.isOptInRBF());
      capnLedger.setIsChainedZC(ledger.isChainedZC());
      capnLedger.setIsWitness(ledger.usesWitness());

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

   void ledgersToCapnp(
      const std::vector<Ledgers::Entry>& ledgers,
      Codec::Types::TxLedger::Builder& txLedger)
   {
      auto capnLedgers = txLedger.initLedgers(ledgers.size());
      for (unsigned i = 0; i < ledgers.size(); i++) {
         auto capnLedger = capnLedgers[i];
         ledgerToCapnp(ledgers[i], capnLedger);
      }
   }

   void nodeStatusToCapnp(
      std::shared_ptr<DBClientClasses::NodeStatus> nodeStatus,
      Codec::Types::NodeStatus::Builder& capnNodeStatus)
   {
      capnNodeStatus.setNode(
         (Codec::Types::NodeStatus::NodeState)nodeStatus->state());
      capnNodeStatus.setRpc(
         (Codec::Types::NodeStatus::RpcState)nodeStatus->rpcState());
      capnNodeStatus.setIsSW(nodeStatus->isSegWitEnabled());

      auto capnChainState = capnNodeStatus.initChain();
      const auto& chainState = nodeStatus->chainStatus();
      capnChainState.setChainState((
         Codec::Types::ChainStatus::ChainState)chainState.state());
      capnChainState.setBlockSpeed(chainState.getBlockSpeed());
      capnChainState.setProgress(chainState.getProgressPct());
      capnChainState.setEta(chainState.getETA());
      capnChainState.setBlocksLeft(chainState.getBlocksLeft());
   }
}

////////////////////////////////////////////////////////////////////////////////
// Callback
Callback::Callback(const NotifFunc& lbd) :
   RemoteCallback(), notifFunc_(lbd)
{}

////////
void Callback::registerRefreshCallback(const std::string& id,
   const std::function<void(void)>& callback)
{
   std::unique_lock<std::mutex> lock(idMutex_);
   if (idCallbacks_.find(id) != idCallbacks_.end()) {
      throw std::runtime_error(
         "we already have a refresh callback for this id: " + id);
   }
   idCallbacks_.emplace(id, callback);
}

void Callback::unregisterCallback(const std::string& id)
{
   std::unique_lock<std::mutex> lock(idMutex_);
   idCallbacks_.erase(id);
}

void Callback::processRefreshCallbacks(std::set<std::string>& ids)
{
   std::unique_lock<std::mutex> lock(idMutex_);
   if (idCallbacks_.empty()) {
      return;
   }

   auto iter = ids.begin();
   while (iter != ids.end()) {
      auto cbIter = idCallbacks_.find(*iter);
      if (cbIter != idCallbacks_.end()) {
         auto thr = std::thread(cbIter->second);
         if (thr.joinable()) {
            thr.detach();
         }
         idCallbacks_.erase(cbIter);
         ids.erase(iter++);
      } else {
         ++iter;
      }
   }
}

////////
void Callback::run(BdmNotification notif)
{
   switch (notif.action)
   {
      case BDMAction_NewBlock:
      {
         auto lbd = [pushLbd = notifFunc_, height = notif.newBlock.getHeight()]()
         {
            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
            auto capnNotif = fromBridge.initNotification();
            capnNotif.setNewBlock(height);
            capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);
            pushLbd(std::make_shared<NotifStruct_Push>(serializeCapnp(message)));
         };

         notifFunc_(std::make_shared<NotifStruct_NewBlock>(
            notif.newBlock, lbd, false));
         break;
      }

      case BDMAction_Ready:
      {
         auto lbd = [pushLbd = notifFunc_, height = notif.newBlock.getHeight()]()
         {
            capnp::MallocMessageBuilder message;
            auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
            auto capnNotif = fromBridge.initNotification();
            capnNotif.setReady(height);
            capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);
            pushLbd(std::make_shared<NotifStruct_Push>(serializeCapnp(message)));
         };

         notifFunc_(std::make_shared<NotifStruct_NewBlock>(
            notif.newBlock, lbd, true));
         break;
      }

      case BDMAction_ZC:
      {
         auto lbd = [pushLbd = notifFunc_](
            const std::vector<Ledgers::Entry>& zcLedgers,
            const std::set<BinaryData>& invalidatedZCs)
         {
            //ledgers
            capnp::MallocMessageBuilder messageLedgers;
            auto fromBridge = messageLedgers.initRoot<Codec::Bridge::FromBridge>();
            auto capnNotif = fromBridge.initNotification();
            capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

            auto capnZCs = capnNotif.initZeroConfs();
            ledgersToCapnp(zcLedgers, capnZCs);
            pushLbd(std::make_shared<NotifStruct_Push>(
               serializeCapnp(messageLedgers)));

            //invalidated ZCs
            if (invalidatedZCs.empty()) {
               return;
            }

            capnp::MallocMessageBuilder messageIZC;
            fromBridge = messageIZC.initRoot<Codec::Bridge::FromBridge>();
            capnNotif = fromBridge.initNotification();
            capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

            auto zcHashes = capnNotif.initInvalidatedZcs(
               invalidatedZCs.size());
            unsigned i = 0;
            for (const auto& hash : invalidatedZCs) {
               zcHashes.set(i++, capnp::Data::Builder(
                  (uint8_t*)hash.getPtr(), hash.getSize()));
            }
            pushLbd(std::make_shared<NotifStruct_Push>(
               serializeCapnp(messageIZC)));
         };
         notifFunc_(std::make_shared<NotifStruct_ZC>(
            std::move(notif.txios), std::move(notif.invalidatedZc), lbd));
         break;
      }

      case BDMAction_InvalidatedZC:
      {
         //Ignore these for now, they come along new blocks to signal
         //mined zcs. We handle zc mining differently in bridge.
         break;
      }

      case BDMAction_Refresh:
      {
         processRefreshCallbacks(notif.ids);
         if (notif.ids.empty()) {
            return;
         }
         notifyRefresh(notif.ids);
         break;
      }

      case BDMAction_NodeStatus:
      {
         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
         auto capnNotif = fromBridge.initNotification();
         capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

         auto capnNode = capnNotif.initNodeStatus();
         nodeStatusToCapnp(notif.nodeStatus, capnNode);

         notifFunc_(std::make_shared<NotifStruct_Push>(serializeCapnp(message)));
         break;
      }

      case BDMAction_BDV_Error:
      {
         //notify error
         LOGINFO << "bdv error:";
         LOGINFO << "  code: " << notif.error.errCode_;
         LOGINFO << "  data: " << notif.error.errData_.toHexStr();

         capnp::MallocMessageBuilder message;
         auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
         auto capnNotif = fromBridge.initNotification();
         capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);
         capnNotif.setError(notif.error.errorStr_);

         notifFunc_(std::make_shared<NotifStruct_Push>(serializeCapnp(message)));
         break;
      }

      default:
         return;
   }
}

////////
void Callback::progress(
   BDMPhase phase,
   const std::vector<std::string> &walletIdVec,
   float progress, unsigned secondsRem,
   unsigned progressNumeric)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   auto capnProgress = capnNotif.initScanProgress();
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

   notifFunc_(std::make_shared<NotifStruct_Push>(serializeCapnp(message)));
}

////////
void Callback::notifySetupDone()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setSetupDone();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

   notifFunc_(std::make_shared<NotifStruct_Push>(serializeCapnp(message)));
}

void Callback::notifySetupRegistrationDone()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setRegisterDone();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

   notifFunc_(std::make_shared<NotifStruct_Push>(serializeCapnp(message)));
}

void Callback::notifyRefresh(const std::set<std::string>& ids)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

   auto capnIds = capnNotif.initRefresh(ids.size());
   unsigned i=0;
   for (const auto& id : ids) {
      capnIds.set(i++, id);
   }

   notifFunc_(std::make_shared<NotifStruct_Push>(serializeCapnp(message)));
}

void Callback::disconnected()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setDisconnected();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

   notifFunc_(std::make_shared<NotifStruct_Push>(serializeCapnp(message)));
}

////////////////////////////////////////////////////////////////////////////////
// NotifStruct
NotifStruct::NotifStruct(NotifType t) :
   type(t)
{}

NotifStruct::~NotifStruct()
{}

bool NotifStruct::syncWalletState() const
{
   return false;
}

////////
NotifStruct_Push::NotifStruct_Push(BinaryData pushData) :
   NotifStruct(NotifType::PUSH), packet(std::move(pushData))
{}

NotifStruct_ZC::NotifStruct_ZC(
   std::vector<TxIOPair> txioVec, std::set<BinaryData> invalidatedZc,
   const std::function<void(
      const std::vector<Ledgers::Entry>&,
      const std::set<BinaryData>&)>& lbd) :
   NotifStruct(NotifType::ZC), txios(std::move(txioVec)),
   invalidatedZCs(std::move(invalidatedZc)), callback(lbd)
{}

////////
NotifStruct_Refresh::NotifStruct_Refresh(const std::function<void(void)>& lbd) :
   NotifStruct(NotifType::REFRESH), callback(lbd)
{}

bool NotifStruct_Refresh::syncWalletState() const
{
   return true;
}

////////
NotifStruct_NewBlock::NotifStruct_NewBlock(
   const NewBlockNotif& notif,
   const std::function<void(void)>& lbd,
   bool isReady) :
   NotifStruct(NotifType::NEWBLOCK),
   blockNotif(notif), isReadyNotif(isReady), callback(lbd)
{}

bool NotifStruct_NewBlock::syncWalletState() const
{
   return isReadyNotif;
}
