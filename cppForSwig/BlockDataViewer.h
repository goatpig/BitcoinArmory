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

#include <string>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include <functional>

#include <Utils/Types.h>
#include "bdmenums.h"

struct WalletRegistrationRequest
{
   std::string walletId;
   std::vector<Armory::Types::ScrAddr> addresses;
   bool isNew;

   WalletRegistrationRequest(const std::string&,
      std::vector<Armory::Types::ScrAddr>&, bool);
};

class ScrAddrFilter;
class BtcWallet;
class BlockDataManager;
class LMDBBlockDatabase;
class TxIOPair;

namespace Armory
{
   namespace ZeroConf
   {
      class ZeroConfContainer;
   }

   class Blockchain;
}

////////////////////////////////////////////////////////////////////////////////
class BlockDataViewer
{
public:
   BlockDataViewer(std::shared_ptr<BlockDataManager>);
   ~BlockDataViewer(void);
   void reset(void);

   std::shared_ptr<BtcWallet> getOrSetWallet(const std::string&);
   void registerAWallet(const WalletRegistrationRequest&,
      const std::function<void(bool)>&);
   bool unregisterWallet(const std::string&);

   bool hasWallet(const std::string&) const;
   std::shared_ptr<BtcWallet> getWallet(const std::string&) const;

   bool scrAddressIsRegistered(const Armory::Types::ScrAddr&) const;
   bool hasScrAddress(const Armory::Types::ScrAddr&) const;
   std::set<Armory::Types::ScrAddr> getAddrSet(void) const;

   LMDBBlockDatabase* getDB(void) const;
   std::shared_ptr<BlockDataManager> bdm(void) const;
   Armory::ZeroConf::ZeroConfContainer* zcContainer(void) const;
   const Armory::Blockchain& blockchain(void) const;
   std::shared_ptr<ScrAddrFilter> getSAF(void) const;

   bool isBDMRunning(void) const;
   void blockUntilBDMisReady(void) const;

   //txios
   std::map<Armory::Types::TxIOKey, TxIOPair>
   getTxioForRange(uint32_t) const;
   std::map<Armory::Types::TxIOKey, std::shared_ptr<const TxIOPair>>
   getZcTxios(void) const;

protected:
   static void unregisterAddresses(
      std::set<Armory::Types::ScrAddr>, const std::function<void(void)>&);

protected:
   std::shared_ptr<BlockDataManager> bdm_;
   LMDBBlockDatabase* db_;
   std::shared_ptr<Armory::Blockchain> bc_;
   std::shared_ptr<ScrAddrFilter> saf_;
   const std::shared_ptr<Armory::ZeroConf::ZeroConfContainer> zeroConfCont_;

   std::map<std::string, std::shared_ptr<BtcWallet>> wallets_;
};
