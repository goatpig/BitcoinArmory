////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "DBClientClasses.h"
#include <Utils/BtcUtils.h>
#include <Utils/varint.h>
#include <btc/ecc.h>
#include <Network/WebSocketClient.h>

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/BDV.capnp.h"

using namespace DBClientClasses;
using namespace Armory;

namespace {
   std::shared_ptr<NodeStatus> capnToNodeStatus(
      Armory::Codec::Types::NodeStatus::Reader nodeStatus)
   {
      if (nodeStatus.hasChain()) {
         auto chainCapn = nodeStatus.getChain();
         NodeChainStatus ncs(
            Node::ChainState(chainCapn.getChainState()),
            chainCapn.getBlockSpeed(), chainCapn.getProgress(),
            chainCapn.getEta(), chainCapn.getBlocksLeft()
         );

         auto result = std::make_shared<NodeStatus>(
            Node::NodeState(nodeStatus.getNode()),
            Node::RpcState(nodeStatus.getRpc()),
            nodeStatus.getIsSW(), ncs
         );

         return result;
      } else {
         DBClientClasses::NodeChainStatus ncs;
         auto result = std::make_shared<NodeStatus>(
            Node::NodeState(nodeStatus.getNode()),
            Node::RpcState(nodeStatus.getRpc()),
            nodeStatus.getIsSW(), ncs
         );

         return result;
      }
   }

   std::vector<TxIOPair> capnToTxios(
      const capnp::List<Codec::Types::TxioPair, capnp::Kind::STRUCT>::Reader& capnTxios)
   {
      std::vector<TxIOPair> txios;
      txios.reserve(capnTxios.size());

      for (auto capnTxio : capnTxios) {
         auto capnAddr = capnTxio.getScrAddr();
         BinaryDataRef scrAddr{capnAddr.begin(), capnAddr.end()};
         TxIOPair txio{
            capnTxio.getTxOut(), capnTxio.getAmount(),
            scrAddr, capnTxio.getTxIn()
         };
         txio.setRBF(capnTxio.getRbf());
         txios.emplace_back(std::move(txio));
      }
      return txios;
   }
}

///////////////////////////////////////////////////////////////////////////////
void initLibrary()
{
   startupBIP150CTX(4);
   startupBIP151CTX();
   Cryptography::ECDSA::setupContext();
}

///////////////////////////////////////////////////////////////////////////////
// BlockHeader
BlockHeader::BlockHeader(BinaryDataRef thishash, BinaryDataRef prevhash,
   Types::BlockId blockId, uint32_t height,
   uint32_t time, uint32_t size, uint32_t ntx,
   bool ismain) :
   thisHash{thishash}, prevHash{prevhash},
   blockId{blockId}, blockHeight{height},
   timestamp{time}, blockSize{size}, numTxs{ntx},
   isMainBranch{ismain}
{}

///////////////////////////////////////////////////////////////////////////////
//
// RemoteCallback
//
///////////////////////////////////////////////////////////////////////////////
RemoteCallback::~RemoteCallback()
{}

BdmNotification::BdmNotification(BDMAction action) :
   action(action)
{}

///////////////////////////////////////////////////////////////////////////////
bool RemoteCallback::processNotifications(
   std::unique_ptr<capnp::MessageReader> reader)
{
   using namespace Armory::Codec;

   auto notifsCapn = reader->getRoot<BDV::Notifications>();
   auto notifs = notifsCapn.getNotifs();

   for (unsigned i = 0; i < notifs.size(); i++) {
      auto notif = notifs[i];
      switch (notif.which())
      {
         case BDV::Notification::CONTINUE_POLLING:
            break;

         case BDV::Notification::NEW_BLOCK:
         {
            auto newblock = notif.getNewBlock();
            auto height = newblock.getHeight();
            if (height != 0) {
               std::vector<Armory::Types::BlockId> invalidatedIds;
               std::vector<Armory::Types::BlockId> newMainIds;
               auto branchHeight = newblock.getBranchHeight();
               if (branchHeight != UINT32_MAX) {
                  for (const auto& blockId : newblock.getInvalidatedIds()) {
                     invalidatedIds.emplace_back(blockId);
                  }
                  for (const auto& blockId : newblock.getNewMainBranchIds()) {
                     newMainIds.emplace_back(blockId);
                  }
               }

               BdmNotification bdmNotif(BDMAction_NewBlock);
               bdmNotif.newBlock = NewBlockNotif{
                  height, branchHeight,
                  std::move(invalidatedIds), std::move(newMainIds)};
               run(std::move(bdmNotif));
            }
            break;
         }

         case BDV::Notification::ZC:
         {
            auto zcTxios = notif.getZc();
            BdmNotification bdmNotif(BDMAction_ZC);
            bdmNotif.txios = capnToTxios(zcTxios);
            bdmNotif.requestID = notif.getRequestId();

            //zc notifs are sometimes delivered along with an
            //invalidated zc notif, let's package both together
            if (i < notifs.size() - 1) {
               auto peekNext = notifs[i+1];
               if (peekNext.which() != BDV::Notification::INVALIDATED_ZC) {
                  continue;
               }
               ++i;

               auto ids = peekNext.getInvalidatedZc();
               for (auto id : ids) {
                  bdmNotif.invalidatedZcHashes.emplace(BinaryData{
                     id.begin(), id.end()
                  });
               }
            }

            run(std::move(bdmNotif));
            break;
         }

         case BDV::Notification::INVALIDATED_ZC:
         {
            auto ids = notif.getInvalidatedZc();
            std::set<BinaryData> idSet;

            BdmNotification bdmNotif(BDMAction_InvalidatedZC);
            for (auto id : ids) {
               bdmNotif.invalidatedZcHashes.emplace(BinaryData{
                  id.begin(), id.end()
               });
            }

            run(std::move(bdmNotif));
            break;
         }

         case BDV::Notification::REFRESH:
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

         case BDV::Notification::READY:
         {
            BdmNotification bdmNotif(BDMAction_Ready);
            auto newBlock = notif.getReady();
            bdmNotif.newBlock = NewBlockNotif{
               newBlock.getHeight(), newBlock.getBranchHeight(), {}, {}};
            run(std::move(bdmNotif));
            break;
         }

         case BDV::Notification::PROGRESS:
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

         case BDV::Notification::TERMINATE:
         {
            //shut down command from server
            return false;
         }

         case BDV::Notification::NODE_STATUS:
         {
            BdmNotification bdmNotif(BDMAction_NodeStatus);
            auto capnNodeStatus = notif.getNodeStatus();
            bdmNotif.nodeStatus = capnToNodeStatus(capnNodeStatus);

            run(std::move(bdmNotif));
            break;
         }

         case BDV::Notification::ERROR:
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
NodeStatus::NodeStatus(Node::NodeState nodeState,
   Node::RpcState rpcState, bool isSW, NodeChainStatus& nodeChainState) :
   nodeState_(nodeState), rpcState_(rpcState), isSegWitEnabled_(isSW),
   nodeChainStatus_(std::move(nodeChainState))
{}

///////////////////////////////////////////////////////////////////////////////
Node::NodeState NodeStatus::state() const
{
   return nodeState_;
}

///////////////////////////////////////////////////////////////////////////////
bool NodeStatus::isSegWitEnabled() const
{
   return isSegWitEnabled_;
}

///////////////////////////////////////////////////////////////////////////////
Node::RpcState NodeStatus::rpcState() const
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
   chainState_(Node::ChainState::Unknown), blockSpeed_(0), progressPct_(0),
   etaSeconds_(UINT64_MAX), blocksLeft_(UINT32_MAX)
{}

NodeChainStatus::NodeChainStatus(Node::ChainState chainState,
   float speed, float pct, uint64_t eta, unsigned blocksLeft) :
   chainState_(chainState), blockSpeed_(speed), progressPct_(pct),
   etaSeconds_(eta), blocksLeft_(blocksLeft)
{}

///////////////////////////////////////////////////////////////////////////////
Node::ChainState NodeChainStatus::state() const
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

////////////////////////////////////////////////////////////////////////////////
// BDV_Error_Struct
BinaryData BDV_Error_Struct::serialize(void) const
{
   BinaryWriter bw;
   bw.put_int32_t(errCode_);

   bw.put_var_int(errData_.getSize());
   bw.put_BinaryData(errData_);

   bw.put_var_int(errorStr_.size());
   bw.put_String(errorStr_);

   return bw.getData();
}

////////////////////////////////////////////////////////////////////////////////
void BDV_Error_Struct::deserialize(const BinaryData& data)
{
   BinaryRefReader brr(data);
   errCode_ = brr.get_int32_t();

   auto len = brr.get_var_int();
   errData_ = brr.get_BinaryData(len);

   len = brr.get_var_int();
   errorStr_ = brr.get_String(len);
}

////////////////////////////////////////////////////////////////////////////////
// NewBlockNotif
NewBlockNotif::NewBlockNotif(uint32_t hgt, uint32_t branch,
   BlockIdVec invalidIds, BlockIdVec mainIds) :
   height_(hgt), branchHeight_(branch),
   invalidatedBlockIds_{std::move(invalidIds)},
   newMainBranchBlockIds_{std::move(mainIds)}
{}

bool NewBlockNotif::isValid() const
{
   return height_ != UINT32_MAX;
}

bool NewBlockNotif::isReorg() const
{
   return isValid() && branchHeight_ != UINT32_MAX;
}

uint32_t NewBlockNotif::getHeight() const
{
   if (!isValid()) {
      throw std::runtime_error("invalid block notif");
   }
   return height_;
}

uint32_t NewBlockNotif::getBranchHeight() const
{
   if (!isReorg()) {
      throw std::runtime_error("not a reorg!");
   }
   return branchHeight_;
}

const NewBlockNotif::BlockIdVec& NewBlockNotif::invalidatedBlockIds() const
{
   return invalidatedBlockIds_;
}

const NewBlockNotif::BlockIdVec& NewBlockNotif::newMainBranchBlockIds() const
{
   return newMainBranchBlockIds_;
}
