////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2024, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "DBClientClasses.h"
#include "WebSocketClient.h"
#include "btc/ecc.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/BDV.capnp.h"

using namespace DBClientClasses;

namespace {
   std::vector<std::shared_ptr<LedgerEntry>> capnToLedgers(
      Armory::Codec::Types::TxLedger::Reader& page)
   {
      std::vector<std::shared_ptr<LedgerEntry>> result;
      auto ledgers = page.getLedgers();
      result.reserve(ledgers.size());

      for (const auto& ledger : ledgers) {
         //tx hash
         auto capnTxHash = ledger.getTxHash();
         auto hashBytes = capnTxHash.asBytes();
         BinaryData txHash(hashBytes.begin(), hashBytes.end());

         //scrAddr list
         auto capnScrAddrs = ledger.getScrAddrs();
         std::vector<BinaryData> scrAddrList;
         scrAddrList.reserve(capnScrAddrs.size());
         for (const auto& scrAddr : capnScrAddrs) {
            auto asBytes = scrAddr.asBytes();
            scrAddrList.emplace_back(BinaryData(
               scrAddr.begin(), scrAddr.end()
            ));
         }

         //instantiate ledger entry
         result.emplace_back(std::make_shared<LedgerEntry>(
            ledger.getWalletId(), ledger.getBalance(), ledger.getTxHeight(),
            txHash, ledger.getTxOutIndex(), ledger.getTxTime(),
            ledger.getIsCoinbase(), ledger.getIsSTS(),
            ledger.getIsChangeBack(), ledger.getIsOptInRBF(),
            ledger.getIsChainedZC(), ledger.getIsWitness(),
            scrAddrList
         ));
      }
      return result;
   }

   std::shared_ptr<NodeStatus> capnToNodeStatus(
      Armory::Codec::Types::NodeStatus::Reader nodeStatus)
   {
      if (nodeStatus.hasChain()) {
         auto chainCapn = nodeStatus.getChain();
         NodeChainStatus ncs(
            CoreRPC::ChainState(chainCapn.getChainState()),
            chainCapn.getBlockSpeed(), chainCapn.getProgress(),
            chainCapn.getEta(), chainCapn.getBlocksLeft()
         );

         auto result = std::make_shared<NodeStatus>(
            CoreRPC::NodeState(nodeStatus.getNode()),
            CoreRPC::RpcState(nodeStatus.getRpc()),
            nodeStatus.getIsSW(), ncs
         );

         return result;
      } else {
         DBClientClasses::NodeChainStatus ncs;
         auto result = std::make_shared<NodeStatus>(
            CoreRPC::NodeState(nodeStatus.getNode()),
            CoreRPC::RpcState(nodeStatus.getRpc()),
            nodeStatus.getIsSW(), ncs
         );

         return result;
      }
   }
}

///////////////////////////////////////////////////////////////////////////////
void initLibrary()
{
   startupBIP150CTX(4);
   startupBIP151CTX();
   CryptoECDSA::setupContext();
}

///////////////////////////////////////////////////////////////////////////////
//
// BlockHeader
//
///////////////////////////////////////////////////////////////////////////////
DBClientClasses::BlockHeader::BlockHeader(
   const BinaryData& rawheader, unsigned height)
{
   unserialize(rawheader.getRef());
   blockHeight_ = height;
}

////////////////////////////////////////////////////////////////////////////////
void DBClientClasses::BlockHeader::unserialize(uint8_t const * ptr, uint32_t size)
{
   if (size < HEADER_SIZE)
      throw BlockDeserializingException();
   dataCopy_.copyFrom(ptr, HEADER_SIZE);
   BtcUtils::getHash256(dataCopy_.getPtr(), HEADER_SIZE, thisHash_);
   difficultyDbl_ = BtcUtils::convertDiffBitsToDouble(
      BinaryDataRef(dataCopy_.getPtr() + 72, 4));
   isInitialized_ = true;
   blockHeight_ = UINT32_MAX;
}

///////////////////////////////////////////////////////////////////////////////
//
// LedgerEntry
//
///////////////////////////////////////////////////////////////////////////////
DBClientClasses::LedgerEntry::LedgerEntry(const std::string& id, int64_t value,
   uint32_t blockHeight, BinaryData& txHash, uint32_t txOutIndex,
   uint32_t timestamp, bool isCoinbase, bool isSentToSelf, bool isChangeBack,
   bool isOptInRBF, bool isChainedZC, bool isWitness,
   std::vector<BinaryData>& scrAddrList) :
   id_(std::move(id)), value_(value), blockHeight_(blockHeight),
   txHash_(std::move(txHash)), txOutIndex_(txOutIndex), timestamp_(timestamp),
   isCoinbase_(isCoinbase), isSentToSelf_(isSentToSelf),
   isChangeBack_(isChangeBack), isOptInRBF_(isOptInRBF),
   isChainedZC_(isChainedZC), isWitness_(isWitness),
   scrAddrList_(std::move(scrAddrList))
{}

///////////////////////////////////////////////////////////////////////////////
const std::string& LedgerEntry::getID() const
{
   return id_;
}

///////////////////////////////////////////////////////////////////////////////
int64_t LedgerEntry::getValue() const
{
   return value_;
}

///////////////////////////////////////////////////////////////////////////////
uint32_t LedgerEntry::getBlockHeight() const
{
   return blockHeight_;
}

///////////////////////////////////////////////////////////////////////////////
BinaryDataRef LedgerEntry::getTxHash() const
{
   return BinaryDataRef(txHash_);
}

///////////////////////////////////////////////////////////////////////////////
uint32_t LedgerEntry::getTxOutIndex() const
{
   return txOutIndex_;
}

///////////////////////////////////////////////////////////////////////////////
uint32_t LedgerEntry::getTxTime() const
{
   return timestamp_;
}

///////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::isCoinbase() const
{
   return isCoinbase_;
}

///////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::isSentToSelf() const
{
   return isSentToSelf_;
}

///////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::isChangeBack() const
{
   return isChangeBack_;
}

///////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::isOptInRBF() const
{
   return isOptInRBF_;
}

///////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::isChainedZC() const
{
   return isChainedZC_;
}

///////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::isWitness() const
{
   return isWitness_;
}

///////////////////////////////////////////////////////////////////////////////
bool LedgerEntry::operator==(const LedgerEntry& rhs)
{
   if (getTxHash() != rhs.getTxHash())
      return false;

   if (getTxOutIndex() != rhs.getTxOutIndex())
      return false;

   return true;
}

///////////////////////////////////////////////////////////////////////////////
const std::vector<BinaryData>& LedgerEntry::getScrAddrList() const
{
   return scrAddrList_;
}

///////////////////////////////////////////////////////////////////////////////
//
// RemoteCallback
//
///////////////////////////////////////////////////////////////////////////////
RemoteCallback::~RemoteCallback(void)
{}

///////////////////////////////////////////////////////////////////////////////
bool RemoteCallback::processNotifications(
   std::unique_ptr<capnp::MessageReader> reader)
{
   using namespace Armory::Codec;

   auto notifsCapn = reader->getRoot<BDV::Notifications>();
   auto notifs = notifsCapn.getNotifs();

   for (auto notif : notifs) {
      switch (notif.which())
      {
         case BDV::Notification::Which::CONTINUE_POLLING:
            break;

         case BDV::Notification::Which::NEW_BLOCK:
         {
            auto newblock = notif.getNewBlock();
            auto height = newblock.getHeight();
            if (height != 0)
            {
               BdmNotification bdmNotif(BDMAction_NewBlock);

               bdmNotif.height = height;
               bdmNotif.branchHeight = newblock.getBranchHeight();
               run(std::move(bdmNotif));
            }

            break;
         }

         case BDV::Notification::Which::ZC:
         {
            auto page = notif.getZc();
            BdmNotification bdmNotif(BDMAction_ZC);
            bdmNotif.ledgers = capnToLedgers(page);
            bdmNotif.requestID = notif.getRequestId();

            run(std::move(bdmNotif));
            break;
         }

         case BDV::Notification::Which::INVALIDATED_ZC:
         {
            auto ids = notif.getInvalidatedZc();
            std::set<BinaryData> idSet;

            BdmNotification bdmNotif(BDMAction_InvalidatedZC);
            for (auto id : ids) {
               bdmNotif.invalidatedZc.emplace(BinaryData(
                  id.begin(), id.end()
               ));
            }

            run(std::move(bdmNotif));
            break;
         }

         case BDV::Notification::Which::REFRESH:
         {
            auto refresh = notif.getRefresh();
            auto refreshType = (BDV_refresh)refresh.getType();
            
            BdmNotification bdmNotif(BDMAction_Refresh);
            if (refreshType != BDV_filterChanged) {
               auto ids = refresh.getIds();
               for (auto id : ids) {
                  bdmNotif.ids.emplace(id);
               }
            } else {
               bdmNotif.ids.emplace(FILTER_CHANGE_FLAG);
            }

            run(std::move(bdmNotif));
            break;
         }

         case BDV::Notification::Which::READY:
         {
            BdmNotification bdmNotif(BDMAction_Ready);
            auto newBlock = notif.getReady();
            bdmNotif.height = newBlock.getHeight();

            run(std::move(bdmNotif));
            break;
         }

         case BDV::Notification::Which::PROGRESS:
         {
            auto capnProgress = notif.getProgress();
            auto capnIds = capnProgress.getIds();
            std::vector<std::string> ids;
            ids.reserve(capnIds.size());
            for (auto capnId : capnIds) {
               ids.emplace_back(capnId);
            }

            auto phase = (BDMPhase)capnProgress.getPhase();
            progress(phase, ids, capnProgress.getProgress(),
               capnProgress.getTime(), capnProgress.getNumericProgress());
            break;
         }

         case BDV::Notification::Which::TERMINATE:
         {
            //shut down command from server
            return false;
         }

         case BDV::Notification::Which::NODE_STATUS:
         {
            BdmNotification bdmNotif(BDMAction_NodeStatus);
            auto capnNodeStatus = notif.getNodeStatus();
            bdmNotif.nodeStatus = capnToNodeStatus(capnNodeStatus);

            run(std::move(bdmNotif));
            break;
         }

         case BDV::Notification::Which::ERROR:
         {
            auto error = notif.getError();

            BdmNotification bdmNotif(BDMAction_BDV_Error);
            bdmNotif.error.errCode_ = error.getCode();
            bdmNotif.error.errorStr_ = error.getErrStr();
            bdmNotif.requestID = notif.getRequestId();

            auto errData = error.getErrData();
            bdmNotif.error.errData_ = BinaryData(
               errData.begin(), errData.end()
            );

            run(std::move(bdmNotif));
            break;
         }

      default:
         continue;
      }
   }

   return true;
}

///////////////////////////////////////////////////////////////////////////////
//
// NodeStatus
//
///////////////////////////////////////////////////////////////////////////////
NodeStatus::NodeStatus(CoreRPC::NodeState nodeState,
   CoreRPC::RpcState rpcState, bool isSW, NodeChainStatus& nodeChainState) :
   nodeState_(nodeState), rpcState_(rpcState), isSegWitEnabled_(isSW),
   nodeChainStatus_(std::move(nodeChainState))
{}

///////////////////////////////////////////////////////////////////////////////
CoreRPC::NodeState NodeStatus::state() const
{
   return nodeState_;
}

///////////////////////////////////////////////////////////////////////////////
bool NodeStatus::isSegWitEnabled() const
{
   return isSegWitEnabled_;
}

///////////////////////////////////////////////////////////////////////////////
CoreRPC::RpcState NodeStatus::rpcState() const
{
   return rpcState_;
}

///////////////////////////////////////////////////////////////////////////////
const NodeChainStatus& NodeStatus::chainStatus() const
{
   return nodeChainStatus_;
}

///////////////////////////////////////////////////////////////////////////////
//
// NodeChainStatus
//
///////////////////////////////////////////////////////////////////////////////
NodeChainStatus::NodeChainStatus() :
   chainState_(CoreRPC::ChainState_Unknown), blockSpeed_(0), progressPct_(0),
   etaSeconds_(UINT64_MAX), blocksLeft_(UINT32_MAX)
{}

NodeChainStatus::NodeChainStatus(CoreRPC::ChainState chainState,
   float speed, float pct, uint64_t eta, unsigned blocksLeft) :
   chainState_(chainState), blockSpeed_(speed), progressPct_(pct),
   etaSeconds_(eta), blocksLeft_(blocksLeft)
{}

///////////////////////////////////////////////////////////////////////////////
CoreRPC::ChainState NodeChainStatus::state() const
{
   return chainState_;
}

///////////////////////////////////////////////////////////////////////////////
float NodeChainStatus::getBlockSpeed() const
{
   return blockSpeed_;
}

///////////////////////////////////////////////////////////////////////////////
float NodeChainStatus::getProgressPct() const
{
   return progressPct_;
}

///////////////////////////////////////////////////////////////////////////////
uint64_t NodeChainStatus::getETA() const
{
   return etaSeconds_;
}

///////////////////////////////////////////////////////////////////////////////
unsigned NodeChainStatus::getBlocksLeft() const
{
   return blocksLeft_;
}
