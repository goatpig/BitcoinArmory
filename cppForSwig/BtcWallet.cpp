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
#include <algorithm>

#include "BtcWallet.h"
#include <BlockchainDatabase/lmdb_wrapper.h>
#include <BlockchainDatabase/txio.h>

using namespace Armory;

////////////////////////////////////////////////////////////////////////////////
// BtcWallet
BtcWallet::BtcWallet(const std::string& ID)
   : walletID_(ID)
{}

BtcWallet::~BtcWallet()
{}

////////
bool BtcWallet::hasScrAddress(const Types::ScrAddr& scrAddr) const
{
   auto addrMap = scrAddrMap_.get();
   return addrMap->find(scrAddr) != addrMap->end();
}

const ScrAddrObj* BtcWallet::getScrAddrObjByKey(const BinaryData& key) const
{
   auto addrMap = scrAddrMap_.get();
   auto saIter = addrMap->find(key);
   if (saIter == addrMap->end()) {
      LOGWARN << "unknown address in btcwallet";
      throw std::runtime_error("unknown address in btcwallet");
   }
   return saIter->second.get();
}

std::set<BinaryDataRef> BtcWallet::getAddrSet() const
{
   auto addrMap = scrAddrMap_.get();
   std::set<BinaryDataRef> addrSet;

   for (auto& addrPair : *addrMap) {
      addrSet.emplace(addrPair.first);
   }
   return addrSet;
}

////////
std::map<Types::TxIOKey, TxIOPair> BtcWallet::getTxioForRange(
   LMDBBlockDatabase* db, const std::set<Types::BlockId>& invalids,
   Types::BlockId start, Types::BlockId end) const
{
   std::map<Types::TxIOKey, TxIOPair> outMap;
   auto addrMap = scrAddrMap_.get();

   for (const auto& scrAddrPair : *addrMap) {
      auto saTxioMap = scrAddrPair.second->getTxios(
         db, invalids, start, end);
      outMap.insert(saTxioMap.begin(), saTxioMap.end());
   }
   return outMap;
}

////////
void BtcWallet::unregisterAddresses(const std::set<BinaryDataRef>& addrSet)
{
   std::vector<Types::ScrAddr> addrVec;
   addrVec.reserve(addrSet.size());
   for (const auto& addrRef : addrSet) {
      addrVec.emplace_back(addrRef);
   }
   scrAddrMap_.erase(addrVec);
}
