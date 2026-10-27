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

#include <vector>
#include <map>
#include <functional>

#include <Utils/BinaryData.h>
#include "BlockObj.h"
#include "bdmenums.h"

#define ARMORY_DB_VERSION   0x9702
#define ARMORY_DB_DEFAULT   ARMORY_DB_FULL
#define UTXO_STORAGE        SCRIPT_UTXO_VECTOR

enum class DB_SELECT : int
{
   HEADERS,
   SCRADDR,
   TXOUTS,
   TXINS,
   TXHINTS,
   TXFILTERS,
   KNOWNHASHES,
   ZERO_CONF,
};

enum class TX_SERIALIZE_TYPE : int
{
   FULL = 1,
   FRAGGED,
   COUNTOUT
};

enum class SPENTNESS : int
{
   UNSPENT = 1,
   SPENT,
   SPENTUNK,
};

class Tx;
class TxIn;
class TxOut;
class TxIOPair;

namespace Armory
{
   enum class ScriptPrefix : uint8_t;
   class BlockHeader;
}

////////////////////////////////////////////////////////////////////////////////
struct StoredDBInfo
{
   StoredDBInfo(void);

   bool isInitialized(void) const;
   static BinaryData getDBKey(uint16_t = 0);

   void unserializeDBValue(BinaryRefReader&);
   void serializeDBValue(BinaryWriter&) const;
   void unserializeDBValue(const BinaryData&);
   void unserializeDBValue(BinaryDataRef);

public:
   Armory::Hash32 metaHash; //32 bytes
   Armory::Hash32 topScannedBlkHash; //32 bytes
   uint64_t metaInt = UINT64_MAX;
   uint32_t armoryVer = ARMORY_DB_VERSION;
   BinaryData magicBytes;
   ARMORY_DB_TYPE armoryType = ARMORY_DB_TYPE::Bare; //default db mode
};

////////////////////////////////////////////////////////////////////////////////
struct StoredTxOut
{
   StoredTxOut(void);

   bool isInitialized(void) const;
   void unserialize(const BinaryData&);
   void unserialize(BinaryDataRef);
   void unserialize(BinaryRefReader&);

   void unserializeDBValue(BinaryRefReader&);
   void serializeDBValue(BinaryWriter&) const;
   void unserializeDBValue(const BinaryData&);
   void unserializeDBValue(BinaryDataRef);

   static void serializeDBValue(
      BinaryWriter&, uint16_t, bool,
      const BinaryDataRef);
   const BinaryData& getSerializedTxOut(void) const;
   TxOut getTxOutCopy(void) const;
   uint64_t getValue(void) const;

   void pprintOneLine(uint32_t = 3) const;

public:
   uint32_t          txVersion;
   BinaryData        dataCopy;
   uint16_t          txIndex;
   uint16_t          txOutIndex;
   BinaryData        parentHash;
   bool              isCoinbase;

   uint32_t          unserArmVer;
};

struct TxOutData
{
   const Armory::Types::Amount amount;
   const Armory::Types::BlockId blockID;
   const Armory::Types::TxId txId;
   const Armory::Types::TxIOId txOutId;
};

////////////////////////////////////////////////////////////////////////////////
struct StoredTx
{
   bool isInitialized(void) const;

   StoredTx& createFromTx(const Tx&, bool = true, bool = true);
   StoredTx& createFromTx(BinaryDataRef, bool = true, bool = true);
   void serializeDBValue(BinaryWriter&, ARMORY_DB_TYPE) const;

   BinaryData getSerializedTxFragged(void) const;
   void unserialize(const BinaryData&, bool = false);
   void unserialize(BinaryDataRef, bool = false);
   void unserialize(BinaryRefReader&, bool = false);

   void unserializeDBValue(BinaryRefReader&);
   void unserializeDBValue(const BinaryData&);
   void unserializeDBValue(BinaryDataRef);
   const BinaryData& getThisHash(void) const;

   BinaryData getSerializedTx(void) const;
   Tx getTxCopy(Armory::Types::BlockId) const;

   bool haveAllTxOut(void) const;
   void pprintOneLine(uint32_t = 3) const;
   void pprintFullTx(uint32_t = 3) const;

public:
   BinaryData        dataCopy;
   BinaryData        thisHash;
   std::map<uint16_t, StoredTxOut> stxoMap;

   uint32_t          lockTime = 0;
   uint32_t          unixTime = 0;

   uint32_t          version = 0;
   Armory::Types::TxId txIndex = UINT16_MAX;
   uint16_t          numTxOut = UINT16_MAX;
   uint32_t          numBytes = UINT32_MAX;
   uint32_t          fragBytes = UINT32_MAX;
   size_t            txInCutOff = SIZE_MAX;

   // We don't actually enforce these members.  They're solely for recording
   // the values that were unserialized with everything else, so that we can
   // leter check that it
   uint32_t          unserArmVer;
   uint32_t          unserTxVer;
   TX_SERIALIZE_TYPE unserTxType;

   bool              isFragged = false;
   bool              rbfFlag = false;
};

////////////////////////////////////////////////////////////////////////////////
struct StoredHeader
{
   bool isInitialized(void) const;
   Armory::BlockHeader getBlockHeaderCopy(void) const;
   BinaryData getSerializedBlockHeader(void) const;
   void createFromBlockHeader(const Armory::BlockHeader&);
   uint32_t getNumTx(void);
   bool haveFullBlock(void) const;

   void setHeaderData(const BinaryData&);
   void serializeDBValue(BinaryWriter&) const;

   void pprintOneLine(uint32_t = 3) const;
   void pprintFullBlock(uint32_t = 3) const;

   void unserializeSimple(BinaryRefReader);
   void unserializeFullBlock(BinaryDataRef,
      bool = true, bool = false);
   void unserializeFullBlock(BinaryRefReader,
      bool = true, bool = false);

   Tx getTxCopy(Armory::Types::TxId);
   BinaryData getSerializedTx(Armory::Types::TxId);

public:
   BinaryData dataCopy;
   BinaryData thisHash;
   std::map<Armory::Types::TxId, StoredTx> stxMap;

   size_t                  offset = SIZE_MAX;
   size_t                  numBytes = UINT32_MAX;
   uint32_t                numTx = UINT32_MAX;
   Armory::Types::BlockId  uniqueID = Armory::Types::INVALID_BLOCK_ID;
   uint32_t                unserArmVer;
   uint16_t                fileID;
   bool                    merkleValid = true;
};
