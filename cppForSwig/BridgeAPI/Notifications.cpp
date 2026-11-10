////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Notifications.h"
#include <Ledgers/LedgerEntry.h>
#include "BDV_Notification.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/Bridge.capnp.h"
#include "capnp/Types.capnp.h"

using namespace Armory;
using namespace Armory::Bridge;
using namespace Armory::Bridge::Notifications;

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
      const Node::Status& nodeStatus,
      Codec::Types::NodeStatus::Builder& capnNodeStatus)
   {
      capnNodeStatus.setNode(
         (Codec::Types::NodeStatus::NodeState)nodeStatus.state);
      capnNodeStatus.setRpc(
         (Codec::Types::NodeStatus::RpcState)nodeStatus.rpcState);
      capnNodeStatus.setIsSW(nodeStatus.segWitEnabled);

      auto capnChainStatus = capnNodeStatus.initChain();
      const auto& chainStatus = nodeStatus.chainStatus;
      capnChainStatus.setChainState((
         Codec::Types::ChainStatus::ChainState)chainStatus.state);
      capnChainStatus.setBlockSpeed(chainStatus.blockSpeed);
      capnChainStatus.setProgress(chainStatus.progressPct);
      capnChainStatus.setEta(chainStatus.etaSeconds);
      capnChainStatus.setBlocksLeft(chainStatus.blocksLeft);
   }
}

////////////////////////////////////////////////////////////////////////////////
// namespace Push
// NotifHandler
Push::NotifHandler::NotifHandler(std::shared_ptr<NotifQueue> queue) :
   ServerPushHandler{}, queue_(queue)
{}

////////
void Push::NotifHandler::pushNewBlock(
   const SocketName& name, const NewBlockNotif& newBlock) const
{
   queue_->push_back(std::make_unique<Notif_NewBlock>(name, newBlock, false));
}

void Push::NotifHandler::pushReady(
   const SocketName& name, const NewBlockNotif& ready) const
{
   queue_->push_back(std::make_unique<Notif_NewBlock>(name, ready, true));
}

void Push::NotifHandler::pushRegistered(const SocketName& name) const
{
   //this notifies of bdv registering, not wallets
   //wallet registration notifs come as refresh, with the ids attached
   queue_->push_back(std::make_unique<Notif_Registered>(name));
}

void Push::NotifHandler::pushZeroConf(
   const SocketName& name,
   const std::string& requestId,
   std::vector<TxIOPair> txios,
   std::set<Armory::Types::TxHash> invalidatedHashes) const
{
   queue_->push_back(std::make_unique<Notif_ZC>(name,
      requestId, std::move(txios), std::move(invalidatedHashes)));
}

void Push::NotifHandler::pushRefresh(
   const SocketName& name, const std::set<std::string>& ids) const
{
   queue_->push_back(std::make_unique<Command_Refresh>(name,
      ids, SyncState::ScanWallets));
}

void Push::NotifHandler::pushNodeStatus(const Node::Status& nodeStatus) const
{
   LOGINFO << "got node status update:\n" << nodeStatus.toString();
   queue_->push_back(std::make_unique<Command_PassAlong>(
      Serialization::getNodeStatusPacket(nodeStatus)));
}

void Push::NotifHandler::pushError(const BdvError& error) const
{
   //notify error
   LOGERR << "bdv error:";
   LOGERR << "  code: " << error.errorCode;
   LOGERR << "  data: " << error.errorData.toHexStr();
   queue_->push_back(std::make_unique<Command_PassAlong>(
      Serialization::getErrorPacket(error)));
}

////////
void Push::NotifHandler::pushProgress(
   BDMPhase phase,
   const std::vector<std::string> &walletIdVec,
   float progress, unsigned secondsRem,
   unsigned progressNumeric) const
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   auto capnProgress = capnNotif.initScanProgress();

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
void Push::NotifHandler::pushDisconnected(const std::string& socketName) const
{
   queue_->push_back(std::make_unique<Notif_Disconnected>(socketName));
}

////////
// notifs
Push::Payload::Payload(PayloadType t, const std::string& name) :
   type(t), socketName{name}
{}

Push::Payload::~Payload()
{}

SyncState Push::Payload::syncWalletState() const
{
   return SyncState::NoSync;
}

////////
Push::Notif_Disconnected::Notif_Disconnected(const std::string& name) :
   Payload(PayloadType::DISCONNECTED, name)
{}

////////
Push::Notif_ZC::Notif_ZC(
   const std::string& socketname, std::string id,
   std::vector<TxIOPair> txioVec,
   std::set<BinaryData> invalidatedZc) :
   Payload(PayloadType::ZC, socketname),
   requestId{std::move(id)}, txios{std::move(txioVec)},
   invalidatedZCHashes{std::move(invalidatedZc)}
{}

SyncState Push::Notif_ZC::syncWalletState() const
{
   return SyncState::ScanWallets;
}

////////
Push::Notif_NewBlock::Notif_NewBlock(
   const std::string& socketname,
   const NewBlockNotif& notif, bool isReady) :
   Payload(PayloadType::NEWBLOCK, socketname),
   blockNotif(notif), isReadyNotif(isReady)
{}

SyncState Push::Notif_NewBlock::syncWalletState() const
{
   return isReadyNotif ? SyncState::SyncWallets : SyncState::ScanWallets;
}

////////
Push::Notif_Registered::Notif_Registered(const std::string& socketname) :
   Payload(PayloadType::IS_REGISTERED, socketname)
{}

////////
// commands
Push::Command_PassAlong::Command_PassAlong(BinaryData pushData) :
   Payload(PayloadType::PASS_TO_CLIENT, {}), packet(std::move(pushData))
{}

////////
Push::Command_Refresh::Command_Refresh(
   const std::string& socketname,
   const std::set<std::string>& ids, SyncState state) :
   Payload(PayloadType::REFRESH, socketname),
   refreshIds(ids), syncWalletsWithDB{state}
{}

Push::Command_Refresh::Command_Refresh(
   const std::string& socketname,
   const std::string& id, SyncState state) :
   Payload(PayloadType::REFRESH, socketname),
   refreshIds({id}), syncWalletsWithDB{state}
{}

SyncState Push::Command_Refresh::syncWalletState() const
{
   return syncWalletsWithDB;
}

////////
Push::Command_ConnectToIp::Command_ConnectToIp(
   const std::string& IP, uint16_t PORT,
   const std::string& cbid, uint64_t msgid) :
   Payload{PayloadType::CONNECT_TO_IP, {}},
   ip{IP}, port{PORT}, callbackId{cbid}, msgId{msgid}
{}

Push::Command_ConnectToPeer::Command_ConnectToPeer(
   const std::string& key, uint64_t msgid) :
   Payload{PayloadType::CONNECT_TO_PEER, {}},
   peerKey{key}, msgId{msgid}
{}

////////
Push::Command_RunAutomationContext::Command_RunAutomationContext(
   const std::string& cbid, uint64_t msgid) :
   Payload{PayloadType::RUN_AUTOMATION_CONTEXT, {}},
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
   return serializeCapnp(message);
}

BinaryData Serialization::getRefreshPacket(
   std::shared_ptr<Push::Command_Refresh> notifPtr)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();

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
   return serializeCapnp(message);
}

BinaryData Serialization::getWalletsRegisteredPacket()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setRegisterDone();
   return serializeCapnp(message);
}

BinaryData Serialization::getServerDisconnectedPacket()
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setDisconnected();
   return serializeCapnp(message);
}

BinaryData Serialization::getNodeStatusPacket(const Node::Status& nodeStatus)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   auto capnNode = capnNotif.initNodeStatus();
   nodeStatusToCapnp(nodeStatus, capnNode);
   return serializeCapnp(message);
}

BinaryData Serialization::getErrorPacket(const BdvError& error)
{
   capnp::MallocMessageBuilder message;
   auto fromBridge = message.initRoot<Codec::Bridge::FromBridge>();
   auto capnNotif = fromBridge.initNotification();
   capnNotif.setError(error.errorStr);
   return serializeCapnp(message);
}
