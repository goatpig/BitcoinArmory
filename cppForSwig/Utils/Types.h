////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2026, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>

class BinaryData;

namespace Armory
{
   namespace Types
   {
      //////////////////////////////////////////////////////////////////////////
      // ids
      using TxId        = uint16_t;
      using TxIOId      = uint16_t;
      using ZcId        = uint32_t;
      using BlockId     = uint32_t;
      using FileId      = uint16_t;
      using BdvId       = uint64_t;
      using ScrAddrId   = uint32_t;

      //////////////////////////////////////////////////////////////////////////
      // types
      using ScrAddr     = BinaryData;
      using TxHash      = BinaryData;
      using Amount      = uint64_t;
      using Value       = int64_t;

      //////////////////////////////////////////////////////////////////////////
      // keys

      //BlockID (BE) | TxId (BE) | 0xFFFF
      using TxKey       = uint64_t;

      //BlockID (BE) | TxId (BE) | TxIOKey (BE)
      using TxIOKey     = uint64_t;

      //ScrAddrId | BlockID (BE)
      using ScrAddrKey  = uint64_t;

      //BlockID (BE)
      using BlockKey    = uint32_t;

      //////////////////////////////////////////////////////////////////////////
      // validity checks
      constexpr static TxKey INVALID_TX_KEY = UINT64_MAX;
      static constexpr bool isTxKeyValid(TxKey key)
      {
         return key != INVALID_TX_KEY;
      }

      constexpr static TxIOKey INVALID_TXIO_KEY = UINT64_MAX;
      static constexpr bool isTxIOKeyValid(TxIOKey key)
      {
         return key != INVALID_TXIO_KEY;
      }

      constexpr static BlockId INVALID_BLOCK_ID = UINT32_MAX;
      static constexpr bool isBlockIdValid(BlockId blockid)
      {
         return blockid != INVALID_BLOCK_ID;
      }

      constexpr static FileId INVALID_FILE_ID = UINT16_MAX;
      static bool constexpr isFileIdValid(FileId fileid)
      {
         return fileid != INVALID_FILE_ID;
      }

      //////////////////////////////////////////////////////////////////////////
      // key helpers
      TxKey       constructTxKey(BlockId, TxId);
      TxKey       constructZCKey(ZcId);
      TxIOKey     constructTxIOKey(BlockId, TxId, TxIOId);
      TxIOKey     constructTxIOKeyFromTxKey(TxKey, TxIOId);
      ScrAddrKey  constructScrAddrKey(ScrAddrId, BlockId);

      TxKey       getTxKeyFromTxIOKey(TxKey);
      TxId        getTxIndexFromTxKey(TxKey);
      TxIOId      getTxIOIndexFromTxIOKey(TxKey);
      ZcId        getZcIdFromTxKey(TxKey);
      BlockId     getBlockIDFromScrAddrKey(ScrAddrKey);
      BlockId     getBlockIDFromTxKey(TxKey);
      ScrAddrId   getScrAddrIdFromScrAddrKey(ScrAddrKey);

      BlockKey    getBlockKeyFromId(BlockId);
      BlockId     getBlockIdFromKey(BlockKey);

      bool        isThisATxIOKey(TxIOKey);
      bool        isThisAZCKey(TxKey);
   }
}
