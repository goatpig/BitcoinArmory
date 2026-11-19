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

#pragma once

#include <Utils/BinaryData.h>
#include <Utils/ThreadSafeClasses.h>
#include <BlockchainDatabase/BlockObj.h>
#include <BlockchainDatabase/StoredBlockObj.h>
#include <Ledgers/LedgerEntry.h>
#include <Ledgers/HistoryPager.h>
#include "ScrAddrObj.h"
#include "TxClasses.h"

////////////////////////////////////////////////////////////////////////////////
class BtcWallet
{
   friend class BlockDataViewer;

private:
   BtcWallet(const BtcWallet&) = delete;

public:
   BtcWallet(const std::string&);
   ~BtcWallet(void);

   bool hasScrAddress(const Armory::Types::ScrAddr&) const;
   std::set<BinaryDataRef> getAddrSet(void) const;

   const ScrAddrObj* getScrAddrObjByKey(const BinaryData&) const;
   const std::string& walletID() const { return walletID_; }

   std::shared_ptr<const std::map<
      Armory::Types::ScrAddr, std::shared_ptr<ScrAddrObj>>>
   getAddrMap(void) const;
   void unregisterAddresses(const std::set<BinaryDataRef>&);

private:
   std::map<Armory::Types::TxIOKey, TxIOPair> getTxioForRange(
      LMDBBlockDatabase*, const std::set<Armory::Types::BlockId>&,
      Armory::Types::BlockId, Armory::Types::BlockId) const;

private:
   const std::string walletID_;
   Armory::Threading::TransactionalMap<
      Armory::Types::ScrAddr, std::shared_ptr<ScrAddrObj>> scrAddrMap_;
};
