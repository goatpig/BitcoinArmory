////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2011-2015, Armory Technologies, Inc.                        //
//  Distributed under the GNU Affero General Public License (AGPL v3)         //
//  See LICENSE-ATI or http://www.gnu.org/licenses/agpl.html                  //
//                                                                            //
//                                                                            //
//  Copyright (C) 2016-2026, goatpig                                          //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "BlockDataViewer.h"
#include <BlockchainDatabase/BlockUtils.h>
#include <BlockchainDatabase/lmdb_wrapper.h>
#include <BlockchainDatabase/txio.h>
#include <Utils/DBUtils.h>
#include <Utils/ArmoryConfig.h>
#include <ZeroConf/Parser.h>
#include <ZeroConf/Utils.h>
#include <ZeroConf/Notifications.h>
#include "BtcWallet.h"

using namespace Armory;

/////////////////////////////////////////////////////////////////////////////
// WalletRegistrationRequest
WalletRegistrationRequest::WalletRegistrationRequest(
   const std::string& wId, std::vector<Armory::Types::ScrAddr>& addrs,
   bool isnew) :
   walletId(wId), addresses(std::move(addrs)),
   isNew(isnew)
{}

/////////////////////////////////////////////////////////////////////////////
// BlockDataViewer
BlockDataViewer::BlockDataViewer(std::shared_ptr<BlockDataManager> bdm) :
   bdm_(bdm), db_(bdm->getIFace()), bc_(bdm->blockchain()),
   saf_{bdm->getScrAddrFilter()}, zeroConfCont_(bdm->zeroConfCont())
{}

BlockDataViewer::~BlockDataViewer()
{}

void BlockDataViewer::reset()
{
   wallets_.clear();
}

////////
std::shared_ptr<BlockDataManager> BlockDataViewer::bdm() const
{
   return bdm_;
}

bool BlockDataViewer::isBDMRunning() const
{
   if (bdm_ == nullptr) {
      return false;
   }
   return bdm_->isRunning();
}

void BlockDataViewer::blockUntilBDMisReady() const
{
   if (bdm_ == nullptr) {
      throw std::runtime_error("no bdmPtr_");
   }
   bdm_->blockUntilReady();
}

LMDBBlockDatabase* BlockDataViewer::getDB() const
{
   return db_;
}

const Blockchain& BlockDataViewer::blockchain() const
{
   return *bc_;
}

ZeroConf::ZeroConfContainer* BlockDataViewer::zcContainer() const
{
   return zeroConfCont_.get();
}

std::shared_ptr<ScrAddrFilter> BlockDataViewer::getSAF() const
{
   return saf_;
}

////////
bool BlockDataViewer::hasWallet(const std::string& ID) const
{
   return wallets_.find(ID) != wallets_.end();
}

void BlockDataViewer::registerAWallet(
   const WalletRegistrationRequest& request,
   const std::function<void(bool)>& callback)
{
   if (request.walletId.empty()) {
      if (callback) {
         callback(false);
      }
      return;
   }

   auto theWallet = getOrSetWallet(request.walletId);
   if (theWallet == nullptr) {
      LOGWARN << "failed to get or set wallet";
      if (callback) {
         callback(false);
      }
      return;
   }

   //strip collisions from set of addresses to register
   auto addrMap = theWallet->getAddrSet();
   std::vector<Types::ScrAddr> scrAddrVec;
   scrAddrVec.reserve(request.addresses.size());
   for (auto& addr : request.addresses) {
      if (addr.empty()) {
         continue;
      }
      if (addrMap.find(addr) != addrMap.end()) {
         continue;
      }
      scrAddrVec.emplace_back(addr);
   }

   auto registrationCompleteCB =
   [theWallet, callback, bdm=bdm_, addrVec=scrAddrVec](bool success)
   {
      if (!success) {
         if (callback) {
            callback(false);
         }
         return;
      }

      if (!addrVec.empty()) {
         auto aaMap = bdm->getScrAddrFilter()->getScanFilterAddrMap();
         std::map<Types::ScrAddr, std::shared_ptr<ScrAddrObj>> saMap;
         for (const auto& addr : addrVec) {
            auto aaIter = aaMap->find(addr);
            if (aaIter == aaMap->end()) {
               throw std::runtime_error("missing address in SAF");
            }
            saMap.emplace(aaIter->first, std::make_shared<ScrAddrObj>(
               aaIter->first, aaIter->second->id));
         }
         theWallet->scrAddrMap_.update(saMap);
      }

      if (callback) {
         callback(true);
      }
   };

   auto batch = std::make_shared<RegistrationBatch>(
      std::vector<std::string>{request.walletId}, std::move(scrAddrVec),
      request.isNew, registrationCompleteCB);
   saf_->pushAddressBatch(batch);
}

bool BlockDataViewer::unregisterWallet(const std::string& walletID)
{
   auto wltIter = wallets_.find(walletID);
   if (wltIter == wallets_.end()) {
      return false;
   }
   wallets_.erase(wltIter);
   return true;
}

////////
bool BlockDataViewer::scrAddressIsRegistered(const Types::ScrAddr& scrAddr) const
{
   auto scrAddrMap = saf_->getScanFilterAddrMap();
   auto saIter = scrAddrMap->find(scrAddr);
   if (saIter == scrAddrMap->end()) {
      return false;
   }
   return true;
}

bool BlockDataViewer::hasScrAddress(const Types::ScrAddr& scrAddr) const
{
   for (const auto& wlt : wallets_) {
      if (wlt.second->hasScrAddress(scrAddr)) {
         return true;
      }
   }
   return false;
}

std::set<Types::ScrAddr> BlockDataViewer::getAddrSet() const
{
   std::set<Types::ScrAddr> addrSet;
   for (const auto& wlt : wallets_) {
      auto wltAddresses = wlt.second->getAddrSet();
      addrSet.insert(wltAddresses.begin(), wltAddresses.end());
   }
   return addrSet;
}

////////
std::shared_ptr<BtcWallet> BlockDataViewer::getWallet(
   const std::string& wltID) const
{
   auto iter = wallets_.find(wltID);
   if (iter != wallets_.end()) {
      return iter->second;
   } else {
      throw std::runtime_error(std::format("unknown wallet id: {}", wltID));
   }
}

////////
std::shared_ptr<BtcWallet> BlockDataViewer::getOrSetWallet(
   const std::string& id)
{
   auto wltIter = wallets_.find(id);
   if (wltIter != wallets_.end()) {
      return wltIter->second;
   } else {
      auto insertResult = wallets_.emplace(id,
         std::make_shared<BtcWallet>(id));
      return insertResult.first->second;
   }
}

////////
std::map<Types::TxIOKey, TxIOPair> BlockDataViewer::getTxioForRange(
   uint32_t fromHeight) const
{
   try {
      //convert height to blockId
      auto header = bc_->getHeaderByHeight(fromHeight);
      auto invalidBlockIds = bc_->getInvalidBlockIds();
      std::map<Types::TxIOKey, TxIOPair> result;
      for (const auto& wlt : wallets_) {
         auto txioRange = wlt.second->getTxioForRange(
            db_, invalidBlockIds,
            header->getBlockHeight(), UINT32_MAX);
         result.insert(txioRange.begin(), txioRange.end());
      }
      return result;
   } catch (const std::range_error&) {
      return {};
   }
}

std::map<Types::TxIOKey, std::shared_ptr<const TxIOPair>>
BlockDataViewer::getZcTxios() const
{
   auto snapshot = zcContainer()->getSnapshot();
   if (snapshot == nullptr) {
      return {};
   }

   std::map<Types::TxIOKey, std::shared_ptr<const TxIOPair>> result;
   auto addrSet = getAddrSet();

   for (const auto& addr : addrSet) {
      auto txioMap = snapshot->getTxioMapForScrAddr(addr);
      if (txioMap.empty()) {
         continue;
      }
      result.insert(txioMap.begin(), txioMap.end());
   }
   return result;
}
