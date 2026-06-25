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
#include <string>

#include <Utils/Types.h>

class Tx;

namespace Armory
{
   class Blockchain;
   class Hash32;
   class BlockHeader;

   ////////
   struct BlockchainDataException : public std::runtime_error
   {
      BlockchainDataException(const std::string&);
   };

   ////////
   class BlockchainData
   {
   private:
      std::shared_ptr<Blockchain> blockchain_;

   public:
      BlockchainData(std::shared_ptr<Blockchain>);

      Tx getTx(const Types::TxKey&) const;
      Tx getTx(const Types::BlockId&, Types::TxId) const;

      Hash32 getTxHashForTxKey(const Types::TxKey&) const;
      bool isTxKeyOnMainBranch(const Types::TxKey&) const;

      std::pair<std::vector<uint8_t>, size_t> getRawBlockForId(
         Types::BlockId) const;
      std::pair<std::vector<uint8_t>, size_t> getRawBlockForHeader(
         std::shared_ptr<BlockHeader>) const;
   };
}
