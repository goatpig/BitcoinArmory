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
// namespace Server
// NotifHandler
Server::NotifHandler::NotifHandler(std::shared_ptr<NotifQueue> queue) :
   RemoteCallback(), queue_(queue)
{}

////////
void Server::NotifHandler::run(BdmNotification notif)
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
         queue_->push_back(std::make_unique<Notif_Refresh>(notif.ids));
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

         queue_->push_back(std::make_unique<Notif_Push>(serializeCapnp(message)));
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

         queue_->push_back(std::make_unique<Notif_Push>(serializeCapnp(message)));
         break;
      }

      default:
         return;
   }
}

////////
void Server::NotifHandler::progress(
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
   queue_->push_back(std::make_unique<Notif_Push>(serializeCapnp(message)));
}

////////
void Server::NotifHandler::disconnected()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setDisconnected();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);
   queue_->push_back(std::make_unique<Notif_Disconnected>());
}

////////////////////////////////////////////////////////////////////////////////
// NotifStruct
Server::Notif::Notif(NotifType t) :
   type(t)
{}

Server::Notif::~Notif()
{}

bool Server::Notif::syncWalletState() const
{
   return false;
}

////////
Server::Notif_Push::Notif_Push(BinaryData pushData) :
   Notif(NotifType::PUSH), packet(std::move(pushData))
{}

////////
Server::Notif_Disconnected::Notif_Disconnected() :
   Notif(NotifType::DISCONNECTED)
{}

////////
Server::Notif_ZC::Notif_ZC(
   std::vector<TxIOPair> txioVec, std::set<BinaryData> invalidatedZc) :
   Notif(NotifType::ZC), txios(std::move(txioVec)),
   invalidatedZCHashes(std::move(invalidatedZc))
{}

////////
Server::Notif_Refresh::Notif_Refresh(const std::set<std::string>& ids) :
   Notif(NotifType::REFRESH), notifIds(ids)
{}

Server::Notif_Refresh::Notif_Refresh(const std::string& id) :
   Notif(NotifType::REFRESH), notifIds({id})
{}

bool Server::Notif_Refresh::syncWalletState() const
{
   return true;
}

////////
Server::Notif_NewBlock::Notif_NewBlock(
   const NewBlockNotif& notif, bool isReady) :
   Notif(NotifType::NEWBLOCK),
   blockNotif(notif), isReadyNotif(isReady)
{}

bool Server::Notif_NewBlock::syncWalletState() const
{
   return isReadyNotif;
}

////////
Server::Notif_Registered::Notif_Registered() :
   Notif(NotifType::IS_REGISTERED)
{}

////////////////////////////////////////////////////////////////////////////////
// namespace Client
BinaryData Client::getSerializedNewBlockNotif(
   std::shared_ptr<Server::Notif_NewBlock> notifPtr)
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

BinaryData Client::getSerializedRefreshNotif(
   std::shared_ptr<Server::Notif_Refresh> notifPtr)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);

   auto capnIds = capnNotif.initRefresh(notifPtr->notifIds.size());
   unsigned i=0;
   for (const auto& id : notifPtr->notifIds) {
      capnIds.set(i++, id);
   }
   return serializeCapnp(message);
}

////////
std::pair<BinaryData, BinaryData> Client::getSerializedZcNotif(
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
BinaryData Client::getSerializedSetupDoneNotif()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setSetupDone();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);
   return serializeCapnp(message);
}

BinaryData Client::getSerializedWalletsRegisteredNotif()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setRegisterDone();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);
   return serializeCapnp(message);
}

BinaryData Client::getSerializedServerDisconnectedNotif()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setDisconnected();
   capnNotif.setCallbackId(BRIDGE_CALLBACK_BDM);
   return serializeCapnp(message);
}
