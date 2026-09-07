////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2026, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <vector>
#include <set>
#include <map>
#include <functional>

#include <Utils/BinaryData.h>
#include <Utils/ReentrantLock.h>
#include <Utils/Types.h>

class TxIOPair;
class Tx;
class NewBlockNotif;
struct UTXO;

namespace AsyncClient
{
   class BlockDataViewer;
}

namespace Armory
{
   namespace Ledgers
   {
      struct DBCache;
   }

   namespace Bridge
   {
      struct NotifStruct;
      using AddressFilter = std::function<bool(const Types::ScrAddr&)>;

      //////////////////////////////////////////////////////////////////////////
      struct CacheResolveResult
      {
         const uint32_t topBlock;
         const bool isZC;
         std::map<Types::TxIOKey, TxIOPair> txioMap;
         std::map<Types::ScrAddr, std::vector<TxIOPair*>> addrTxioMap;
         std::shared_ptr<Ledgers::DBCache> dbCache;

         CacheResolveResult(
            uint32_t, bool, std::shared_ptr<Ledgers::DBCache>);
         void addTxio(const Types::TxIOKey&,
            const TxIOPair&,
            const Types::ScrAddr&);
      };

      //////////////////////////////////////////////////////////////////////////
      struct Values
      {
         const Types::Value fullBalance;
         const Types::Value spendableBalance;
         const Types::Value unconfirmedBalance;
         const size_t txCount;
      };

      struct ChainData
      {
         const std::map<Types::TxIOKey, TxIOPair> txioMap;
         std::map<Types::ScrAddr, Values> valueMap;

         Types::Value totalBalance       = 0;
         Types::Value spendableBalance   = 0;
         Types::Value unconfirmedBalance = 0;
         size_t txCount                  = 0;

         ChainData(CacheResolveResult&);
      };

      //////////////////////////////////////////////////////////////////////////
      class TxIOCache : public Lockable
      {
      private:
         std::map<Types::TxIOKey, TxIOPair> unspentTxios_;
         std::map<Types::TxIOKey, TxIOPair> spentTxios_;
         std::map<Types::TxIOKey, TxIOPair> zcTxios_;
         std::shared_ptr<Ledgers::DBCache> dbCache_;
         uint32_t lastKnownBlock_ = UINT32_MAX;

      private:
         void initAfterLock(void) override {}
         void cleanUpBeforeUnlock(void) override {}

         bool txKeyIsValid(const Types::TxKey&) const;
         std::pair<std::set<Types::TxKey>, std::set<Types::BlockId>>
         addTxios(std::vector<TxIOPair>&, uint32_t);
         std::set<Types::TxKey> updateZC(
            std::shared_ptr<AsyncClient::BlockDataViewer>,
            const std::vector<TxIOPair>&,
            const std::set<BinaryData>&, bool);
         std::vector<UTXO> getZcUTXOs(bool, const AddressFilter&) const;
         void updateBlockBranching(const NewBlockNotif&);

      public:
         TxIOCache(void);

         void purge(void);
         uint32_t update(std::shared_ptr<AsyncClient::BlockDataViewer>,
            std::shared_ptr<NotifStruct>);
         std::shared_ptr<const Ledgers::DBCache> getDBCache(void) const;
         CacheResolveResult resolve(const AddressFilter&, uint32_t) const;
         CacheResolveResult resolveZC(const AddressFilter&) const;
         std::map<Types::ScrAddr, std::set<Types::TxHash>> getAddressBook(
            const AddressFilter&) const;
         std::vector<UTXO> getUTXOs(uint64_t, bool, bool,
            const AddressFilter&) const;
         std::map<Types::TxIOKey, TxIOPair> getZcTxios(const AddressFilter&) const;
      };
   } //namespace Bridge
} //namespace Armory
