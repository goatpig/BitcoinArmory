////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Copyright (C) 2026, goatpig                                               //
//  Distributed under the MIT license                                         //
//  See LICENSE-MIT or https://opensource.org/licenses/MIT                    //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "Types.h"

#ifdef _WIN32
   #include <winsock2.h>
#else
   #include <arpa/inet.h>
#endif

using namespace Armory;
using namespace Armory::Types;

/*
Tx keys that don't point to a block. Mined tx keys start with the block id
(big endian), these start with a 2 byte tag instead:
   - zc: a tx in the db's mempool
   - unpooled: a tx no db has given a key yet (offline payloads, txs the
     bridge broadcasts), only the bridge's dbCache makes them, they are
     never sent to a db
The keys are little endian, the id sits in bytes 2 to 5 (big endian), so
keys of a kind sort by id in LMDB's byte order. A mined key only carries one
of these tags for block ids 0xFEFF0000 to 0xFEFFFFFF and 0xFFFF0000 up, block
ids stay well below that.
*/
#define ZC_MASK         0xFFFF00000000FFFF
#define UNPOOLED_MASK   0xFFFF00000000FFFE
#define POOL_TAG_MASK   0x000000000000FFFF

////////////////////////////////////////////////////////////////////////////////
// key helpers
TxKey Types::constructTxKey(BlockId blockID, TxId txId)
{
   return
      0xFFFF000000000000 |
      (uint64_t)htonl(blockID) |
      (uint64_t)htons(txId) << 32;
}

TxIOKey Types::constructTxIOKey(BlockId blockID, TxId txId, TxIOId txIOId)
{
   return
      (uint64_t)htonl(blockID) |
      (uint64_t)htons(txId) << 32 |
      (uint64_t)htons(txIOId) << 48;
}

TxIOKey Types::constructTxIOKeyFromTxKey(TxKey txKey, TxIOId txIOId)
{
   return txKey & (0x0000FFFFFFFFFFFF | (uint64_t)htons((uint16_t)txIOId) << 48);
}

////////
BlockId Types::getBlockIDFromScrAddrKey(uint64_t key)
{
   return ntohl(uint32_t(key >> 32));
}

BlockId Types::getBlockIDFromTxKey(TxKey txKey)
{
   return ntohl((uint32_t)txKey);
}

////////
TxKey Types::constructZCKey(ZcId zcid)
{
   return ZC_MASK | (uint64_t)htonl(zcid) << 16;
}

ZcId Types::getZcIdFromTxKey(TxKey key)
{
   return ntohl((uint32_t)(key >> 16));
}

TxKey Types::constructUnpooledKey(UnpooledId unpooledId)
{
   return UNPOOLED_MASK | (uint64_t)htonl(unpooledId) << 16;
}

UnpooledId Types::getUnpooledIdFromTxKey(TxKey key)
{
   return ntohl((uint32_t)(key >> 16));
}

////////
TxIOId Types::getTxIOIndexFromTxIOKey(TxKey key)
{
   return ntohs((uint16_t)(key >> 48));
}

TxId Types::getTxIndexFromTxKey(TxKey key)
{
   return ntohs((uint16_t)(key >> 32));
}

TxKey Types::getTxKeyFromTxIOKey(TxIOKey key)
{
   return key | 0xFFFF000000000000;
}

////////
bool Types::isThisAZCKey(TxKey key)
{
   return key != INVALID_TX_KEY ?
      (key & POOL_TAG_MASK) == (ZC_MASK & POOL_TAG_MASK) :
      false;
}

bool Types::isThisAnUnpooledKey(TxKey key)
{
   return key != INVALID_TX_KEY ?
      (key & POOL_TAG_MASK) == (UNPOOLED_MASK & POOL_TAG_MASK) :
      false;
}

bool Types::isThisATxIOKey(TxIOKey key)
{
   return (key & 0xFFFF000000000000) != 0xFFFF000000000000;
}

////////
ScrAddrKey Types::constructScrAddrKey(ScrAddrId scrAddrId, BlockId blockId)
{
   return (uint64_t)scrAddrId | (uint64_t)htonl(blockId) << 32;
}

ScrAddrId Types::getScrAddrIdFromScrAddrKey(ScrAddrKey key)
{
   return (uint32_t)key;
}

////////
BlockKey Types::getBlockKeyFromId(BlockId blockId)
{
   return htonl(blockId);
}

BlockId Types::getBlockIdFromKey(BlockKey blockKey)
{
   return ntohl(blockKey);
}
