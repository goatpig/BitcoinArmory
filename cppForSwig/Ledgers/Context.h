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
      struct DBCache
      {
         std::map<Types::TxKey, Tx> txMap;
         std::map<Types::TxHash, Types::TxKey> txHashToKey;
         std::map<Types::BlockId, HeaderPtr> headers;

         void addHeaders(const std::vector<HeaderPtr>&);
         HeaderPtr getHeaderForHeight(uint32_t) const;
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
