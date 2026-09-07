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
#include <map>
#include <set>

#include <Utils/Types.h>

class TxIOPair;
class Tx;


namespace DBClientClasses
{
   struct BlockHeader;
};

namespace Armory
{
   namespace Ledgers
   {
      using HeaderPtr = std::shared_ptr<DBClientClasses::BlockHeader>;
      class DBCache
      {
      private:
         std::map<Types::TxKey, Tx> txMap_;
         std::map<Types::TxHash, Types::TxKey> txHashToKey_;
         std::map<Types::BlockId, HeaderPtr> headers_;

      public:
         DBCache(void);
         void clear(void);

         void addHeaders(const std::vector<HeaderPtr>&);
         HeaderPtr getHeader(Types::BlockId) const;
         HeaderPtr getHeaderForHeight(uint32_t) const;
         const std::map<Types::BlockId, HeaderPtr>& getHeaderMap(void) const;

         void addTx(Tx&);
         void eraseTx(Types::TxKey);
         const Tx& getTx(Types::TxKey) const;
         const Tx& getTxByHash(const Types::TxHash&) const;
         std::map<Types::TxHash, Types::TxKey> getHashesStartingKey(
            Types::TxKey) const;
         std::set<Types::TxKey> purgeTxs(const std::set<Types::TxHash>&);
      };

      class Context
      {
      private:
         const std::map<Types::BlockId, HeaderPtr>& headers_;
         const std::map<Types::TxKey, Tx> txMap_;
         const std::set<Types::ScrAddr> scrAddrSet_;

      public:
         Context(
            const std::map<Types::BlockId, HeaderPtr>&,
            std::map<Types::TxKey, Tx>,
            std::set<Types::ScrAddr>
         );

         uint32_t getTimestampForBlockId(Types::BlockId) const;
         uint32_t getHeightForBlockId(Types::BlockId) const;
         const Types::ScrAddr& getTxHash(Types::TxKey) const;
         size_t getTxOutCount(Types::TxKey) const;
         const Tx& getTx(Types::TxKey) const;
         bool filterTxio(const TxIOPair&) const;
      };

      Context prepareContext(
         const std::map<Types::TxIOKey, TxIOPair>&,
         std::shared_ptr<const DBCache>,
         std::set<Types::ScrAddr>
      );
   }
}
