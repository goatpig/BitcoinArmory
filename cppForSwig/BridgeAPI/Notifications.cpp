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
using namespace Armory::Bridge::Notifications;

#define BRIDGE_CALLBACK_BDM         "bdm_callback"
#define BRIDGE_CALLBACK_PROGRESS    "progress"
#define DISCONNECTED_CALLBACK_ID    "disconnected"

////////////////////////////////////////////////////////////////////////////////
// anon namespace
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
// namespace Push
// NotifHandler
Push::NotifHandler::NotifHandler(std::shared_ptr<NotifQueue> queue) :
   RemoteCallback(), queue_(queue)
{}

////////
void Push::NotifHandler::run(BdmNotification notif)
{
   switch (notif.action)
   {
      case BDMAction_NewBlock:
      {
         queue_->push_back(std::make_unique<Notif_NewBlock>(
            notif.newBlock, false));
         break;
      }

      case BDMAction_Ready:
      {
         queue_->push_back(std::make_unique<Notif_NewBlock>(
            notif.newBlock, true));
         break;
      }

      case BDMAction_Registered:
      {
         //this notifies of bdv registering, not wallets
         //wallet registration notifs come as refresh, with the ids attached
         queue_->push_back(std::make_unique<Notif_Registered>());
         break;
      }

      case BDMAction_ZC:
      {
         queue_->push_back(std::make_unique<Notif_ZC>(
            std::move(notif.txios), std::move(notif.invalidatedZcHashes)));
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
         queue_->push_back(std::make_unique<Command_Refresh>(notif.ids));
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

         queue_->push_back(std::make_unique<Command_PassAlong>(serializeCapnp(message)));
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

         queue_->push_back(std::make_unique<Command_PassAlong>(serializeCapnp(message)));
         break;
      }

      default:
         return;
   }
}

////////
void Push::NotifHandler::progress(
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
   queue_->push_back(std::make_unique<Command_PassAlong>(serializeCapnp(message)));
}

////////
void Push::NotifHandler::disconnected()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setDisconnected();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);
   queue_->push_back(std::make_unique<Notif_Disconnected>());
}

////////
// notifs
Push::Payload::Payload(PayloadType t) :
   type(t)
{}

Push::Payload::~Payload()
{}

bool Push::Payload::syncWalletState() const
{
   return false;
}

////////
Push::Notif_Disconnected::Notif_Disconnected() :
   Payload(PayloadType::DISCONNECTED)
{}

////////
Push::Notif_ZC::Notif_ZC(
   std::vector<TxIOPair> txioVec, std::set<BinaryData> invalidatedZc) :
   Payload(PayloadType::ZC), txios(std::move(txioVec)),
   invalidatedZCHashes(std::move(invalidatedZc))
{}

////////
Push::Notif_NewBlock::Notif_NewBlock(
   const NewBlockNotif& notif, bool isReady) :
   Payload(PayloadType::NEWBLOCK),
   blockNotif(notif), isReadyNotif(isReady)
{}

bool Push::Notif_NewBlock::syncWalletState() const
{
   return isReadyNotif;
}

////////
Push::Notif_Registered::Notif_Registered() :
   Payload(PayloadType::IS_REGISTERED)
{}

////////
// commands
Push::Command_PassAlong::Command_PassAlong(BinaryData pushData) :
   Payload(PayloadType::PASS_TO_CLIENT), packet(std::move(pushData))
{}

////////
Push::Command_Refresh::Command_Refresh(const std::set<std::string>& ids) :
   Payload(PayloadType::REFRESH), refreshIds(ids)
{}

Push::Command_Refresh::Command_Refresh(const std::string& id) :
   Payload(PayloadType::REFRESH), refreshIds({id})
{}

bool Push::Command_Refresh::syncWalletState() const
{
   return true;
}

////////
Push::Command_ConnectToIp::Command_ConnectToIp(
   const std::string& IP, uint16_t PORT,
   const std::string& cbid, uint64_t msgid) :
   Payload{PayloadType::CONNECT_TO_IP},
   ip{IP}, port{PORT}, callbackId{cbid}, msgId{msgid}
{}

Push::Command_ConnectToPeer::Command_ConnectToPeer(
   const std::string& key, uint64_t msgid) :
   Payload{PayloadType::CONNECT_TO_PEER},
   peerKey{key}, msgId{msgid}
{}

////////
Push::Command_RunAutomationContext::Command_RunAutomationContext(
   const std::string& cbid, uint64_t msgid) :
   Payload{PayloadType::RUN_AUTOMATION_CONTEXT},
   callbackId{cbid}, msgId{msgid}
{}

////////////////////////////////////////////////////////////////////////////////
// namespace Serialization
BinaryData Serialization::getNewBlockPacket(
   std::shared_ptr<Push::Notif_NewBlock> notifPtr)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   if (notifPtr->isReadyNotif) {
      capnNotif.setReady(notifPtr->blockNotif.getHeight());
   } else {
      capnNotif.setNewBlock(notifPtr->blockNotif.getHeight());
   }
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);
   return serializeCapnp(message);
}

BinaryData Serialization::getRefreshPacket(
   std::shared_ptr<Push::Command_Refresh> notifPtr)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

   auto capnIds = capnNotif.initRefresh(notifPtr->refreshIds.size());
   unsigned i=0;
   for (const auto& id : notifPtr->refreshIds) {
      capnIds.set(i++, id);
   }
   return serializeCapnp(message);
}

////////
std::pair<BinaryData, BinaryData> Serialization::getZcPacket(
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

   //invalidated ZCs
   if (invalidatedZCs.empty()) {
      return std::make_pair(
         serializeCapnp(messageLedgers), BinaryData{});
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
   return std::make_pair(
      serializeCapnp(messageLedgers), serializeCapnp(messageIZC));
}

////////
BinaryData Serialization::getSetupDonePacket()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setSetupDone();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);
   return serializeCapnp(message);
}

BinaryData Serialization::getWalletsRegisteredPacket()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setRegisterDone();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);
   return serializeCapnp(message);
}

BinaryData Serialization::getServerDisconnectedPacket()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setDisconnected();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);
   return serializeCapnp(message);
}
