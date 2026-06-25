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
   return 0xFFFF00000000FFFF | (uint64_t)htonl(zcid) << 16;
}

ZcId Types::getZcIdFromTxKey(TxKey key)
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
   return (key & 0x000000000000FFFF) == 0x000000000000FFFF;
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
