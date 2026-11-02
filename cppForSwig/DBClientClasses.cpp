////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2018-2026, goatpig.                                         //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "DBClientClasses.h"
#include <Utils/Cryptography.h>
#include <Utils/BIP150_151.h>
#include "BDV_Notification.h"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include "capnp/BDV.capnp.h"

using namespace DBClientClasses;
using namespace Armory;
using namespace std::string_view_literals;

namespace {
   Node::Status capnToNodeStatus(
      Armory::Codec::Types::NodeStatus::Reader nodeStatus)
   {
      if (nodeStatus.hasChain()) {
         auto chainCapn = nodeStatus.getChain();
         Node::ChainStatus ncs(
            Node::ChainState(chainCapn.getChainState()),
            chainCapn.getProgress(), chainCapn.getBlockSpeed(),
            chainCapn.getEta(), chainCapn.getBlocksLeft()
         );

         return Node::Status{
            Node::NodeState(nodeStatus.getNode()),
            Node::RpcState(nodeStatus.getRpc()),
            ncs, nodeStatus.getIsSW()};
      } else {
         return Node::Status{
            Node::NodeState(nodeStatus.getNode()),
            Node::RpcState(nodeStatus.getRpc()),
            {}, nodeStatus.getIsSW()
         };
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
         txio.setTxTime(capnTxio.getTxTime());
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
DBClientClasses::BlockHeader::BlockHeader(
   BinaryDataRef thishash, BinaryDataRef prevhash,
   Types::BlockId blockId, uint32_t height,
   uint32_t time, uint32_t size, uint32_t ntx,
   bool ismain) :
   thisHash{thishash}, prevHash{prevhash},
   blockId{blockId}, blockHeight{height},
   timestamp{time}, blockSize{size}, numTxs{ntx},
   isMainBranch{ismain}
{}

///////////////////////////////////////////////////////////////////////////////
// ServerPushHandler
ServerPushHandler::ServerPushHandler()
{}

ServerPushHandler::~ServerPushHandler()
{}

////////
bool ServerPushHandler::parseCapnPayload(
   std::unique_ptr<capnp::MessageReader> reader,
   const SocketName& socketName)
{
   using namespace Armory::Codec;
   auto notifsCapn = reader->getRoot<BDV::Notifications>();
   auto notifs = notifsCapn.getNotifs();

   for (unsigned i = 0; i < notifs.size(); i++) {
      auto notif = notifs[i];
      switch (notif.which())
      {
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

               pushNewBlock(socketName, NewBlockNotif{
                  height, branchHeight,
                  std::move(invalidatedIds), std::move(newMainIds)
               });
            }
            break;
         }

         case BDV::Notification::ZC:
         {
            auto capnTxios = notif.getZc();
            auto txios = capnToTxios(capnTxios);
            std::set<Armory::Types::TxHash> invalidatedHashes;
            std::string requestID = notif.getRequestId();

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
                  invalidatedHashes.emplace(BinaryData{id.begin(), id.end()});
               }
            }
            pushZeroConf(socketName, requestID,
               std::move(txios), std::move(invalidatedHashes));
            break;
         }

         //deprecated
         /*
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
         */

         case BDV::Notification::REFRESH:
         {
            auto refresh = notif.getRefresh();
            auto refreshType = (BDV_refresh)refresh.getType();

            std::set<std::string> ids;
            if (refreshType != BDV_filterChanged) {
               auto capnIds = refresh.getIds();
               for (auto id : capnIds) {
                  ids.emplace(id);
               }
            }
            pushRefresh(socketName, ids);
            break;
         }

         case BDV::Notification::CONNECTION_IS_READY:
         {
            auto newBlock = notif.getConnectionIsReady();
            NewBlockNotif readyNotif{
               newBlock.getHeight(), newBlock.getBranchHeight(), {}, {}};
            pushReady(socketName, readyNotif);
            break;
         }

         case BDV::Notification::REGISTRATION_DONE:
         {
            pushRegistered(socketName);
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
            pushProgress(phase, ids, capnProgress.getProgress(),
               capnProgress.getTime(), capnProgress.getNumericProgress());
            break;
         }

         case BDV::Notification::NODE_STATUS:
         {
            auto capnNodeStatus = notif.getNodeStatus();
            pushNodeStatus(capnToNodeStatus(capnNodeStatus));
            break;
         }

         case BDV::Notification::ERROR:
         {
            auto capnError = notif.getError();

            auto capnErrData = capnError.getErrData();
            BinaryData errorData{capnErrData.begin(), capnErrData.end()};
            BdvError error{
               std::string{notif.getRequestId()},
               std::string{capnError.getErrStr()},
               errorData, capnError.getCode()
            };
            pushError(error);
            break;
         }

      default:
         continue;
      }
   }

   return true;
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
