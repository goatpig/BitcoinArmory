////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2025, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <vector>
#include <map>
#include <set>
#include <stdint.h>

class BinaryData;
class BinaryDataRef;
class TxIOPair;
class LMDBBlockDatabase;
class Tx;

namespace DBClientClasses
{
   class BlockHeader;
};

namespace Armory
{
   namespace ZeroConf
   {
      class MempoolSnapshot;
   }
   class Blockchain;

   namespace Ledgers
   {
      struct DBCache
      {
         struct Blocks
         {
            uint8_t mainChain = 0;
            std::map<uint8_t, DBClientClasses::BlockHeader> blocks;
         };

         std::map<BinaryData, Tx> txMap;
         std::map<uint32_t, Blocks> blocks;

         void addBlocks(std::vector<DBClientClasses::BlockHeader>&);
         bool isHeightValid(uint32_t, uint8_t) const;
      };

      class Context
      {
      private:
         const std::map<uint32_t, uint32_t> timestamps_;

         //TODO: setup with transparent bdr/bd comparator
         const std::map<BinaryData, Tx> txMap_;
         const std::map<BinaryData, std::map<uint32_t, BinaryData>> txioKeyToScrAddr_;
         const std::set<BinaryData> scrAddrSet_;

      public:
         Context(
            std::map<uint32_t, uint32_t>,
            std::map<BinaryData, Tx>&,
            std::map<BinaryData, std::map<uint32_t, BinaryData>>&,
            std::set<BinaryData>
         );

         uint32_t getTimestampForBlockHeight(uint32_t) const;
         const BinaryData& getTxHash(BinaryDataRef) const;
         size_t getTxOutCount(BinaryDataRef) const;
         const Tx& getTx(BinaryDataRef) const;
         const BinaryData& getScrAddrForTxOut(const TxIOPair&) const;
         bool filterTxio(const TxIOPair&) const;
      };

      Context prepareContext(
         const std::map<BinaryData, TxIOPair>&,
         const Blockchain&, LMDBBlockDatabase*,
         std::shared_ptr<const ZeroConf::MempoolSnapshot>
      );

      Context prepareContext(
         const std::map<BinaryData, TxIOPair>&,
         std::shared_ptr<const DBCache>, std::set<BinaryData>
      );
   }
}
